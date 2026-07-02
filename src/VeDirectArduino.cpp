// VeDirectArduino.cpp — see VeDirectArduino.h
//
// Part of the VeDirect_Arduino library. MIT licensed.

#include "VeDirectArduino.h"

#include <limits.h>  // ULONG_MAX

VeDirectArduino::VeDirectArduino()
    : _serial(nullptr),
      _haveFrame(false),
      _lastFrameMs(0),
      _waiting(false),
      _expectResp(0),
      _matchReg(false),
      _expectReg(0),
      _matched(false),
      _errored(false),
      _asyncCb(nullptr),
      _asyncCtx(nullptr),
      _asyncHead(0),
      _asyncCount(0) {}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool VeDirectArduino::begin(HardwareSerial& serial,
                            unsigned long   baud,
                            unsigned long   timeoutMs) {
    _serial = &serial;
    _serial->begin(baud);

    _text.reset();
    _hex.reset();
    _text.setHexByteSink(&VeDirectArduino::_hexSinkTrampoline, this);

    _haveFrame   = false;
    _lastFrameMs = 0;

    // Probe for a valid Text frame.
    unsigned long start = millis();
    while ((millis() - start) < timeoutMs) {
        if (_service()) {
            return true;
        }
    }
    return false;
}

bool VeDirectArduino::loop() {
    return _service();
}

unsigned long VeDirectArduino::msSinceLastFrame() const {
    if (!_haveFrame) {
        return ULONG_MAX;
    }
    return millis() - _lastFrameMs;
}

// ---------------------------------------------------------------------------
// Byte servicing / HEX dispatch
// ---------------------------------------------------------------------------

bool VeDirectArduino::_service() {
    bool validCompleted = false;
    if (!_serial) {
        return false;
    }
    while (_serial->available() > 0) {
        int c = _serial->read();
        if (c < 0) {
            break;
        }
        if (_text.parse((uint8_t)c)) {
            // A Text frame completed. Record the timestamp only for valid ones.
            if (_text.lastFrameValid()) {
                _haveFrame   = true;
                _lastFrameMs = millis();
                validCompleted = true;
            }
        }
    }
    return validCompleted;
}

bool VeDirectArduino::_hexSinkTrampoline(uint8_t b, void* ctx) {
    return static_cast<VeDirectArduino*>(ctx)->_hexFeed(b);
}

bool VeDirectArduino::_hexFeed(uint8_t b) {
    bool complete = _hex.feed(b);
    if (complete) {
        _handleHexFrame(_hex.lastFrame());
    }
    return complete;
}

void VeDirectArduino::_handleHexFrame(const VeDirectHexProtocol::Frame& f) {
    if (!f.checksumOk) {
        return;  // ignore corrupt frames
    }

    if (_waiting) {
        bool regOk = (!_matchReg) || (f.hasReg && f.reg == _expectReg);
        if (f.respType == _expectResp && regOk) {
            _respFrame = f;
            _matched   = true;
            return;
        }
        if (f.respType == VeDirectHex::RSP_ERROR) {
            _errored = true;
            return;
        }
    }

    // Async notifications may arrive at any time.
    if (f.respType == VeDirectHex::RSP_ASYNC && f.hasReg) {
        _dispatchAsync(f.reg, f.value);
    }
}

void VeDirectArduino::_dispatchAsync(uint16_t reg, uint32_t value) {
    if (_asyncCb) {
        _asyncCb(reg, value, _asyncCtx);
        return;  // callback consumes it; do not queue
    }
    // Poll model: enqueue, discarding the oldest on overflow.
    if (_asyncCount < VEDIRECT_ASYNC_QUEUE_DEPTH) {
        uint8_t tail = (uint8_t)((_asyncHead + _asyncCount) % VEDIRECT_ASYNC_QUEUE_DEPTH);
        _asyncQueue[tail].reg   = reg;
        _asyncQueue[tail].value = value;
        _asyncCount++;
    } else {
        // Overflow: overwrite oldest, advance head.
        _asyncQueue[_asyncHead].reg   = reg;
        _asyncQueue[_asyncHead].value = value;
        _asyncHead = (uint8_t)((_asyncHead + 1) % VEDIRECT_ASYNC_QUEUE_DEPTH);
    }
}

// ---------------------------------------------------------------------------
// Async accessors
// ---------------------------------------------------------------------------

void VeDirectArduino::onAsync(VeDirectAsyncCallback cb, void* ctx) {
    _asyncCb  = cb;
    _asyncCtx = ctx;
}

bool VeDirectArduino::popAsync(uint16_t* reg, uint32_t* value) {
    if (_asyncCount == 0) {
        return false;
    }
    if (reg)   *reg   = _asyncQueue[_asyncHead].reg;
    if (value) *value = _asyncQueue[_asyncHead].value;
    _asyncHead = (uint8_t)((_asyncHead + 1) % VEDIRECT_ASYNC_QUEUE_DEPTH);
    _asyncCount--;
    return true;
}

// ---------------------------------------------------------------------------
// Blocking transceive
// ---------------------------------------------------------------------------
//
// NOTE: HEX Error responses (RSP_ERROR) carry no register id (the wire format
// has none), so a stale Error frame from an already-abandoned prior exchange
// could in principle be misattributed to a *new* request if that new request
// is issued for a different register with no intervening loop()/hexGet/hexSet
// call to drain it first. Deliberately not "fixed" by pre-draining the RX
// buffer here: that would also discard any Text protocol bytes mid-flight at
// that moment, which is worse than this narrow, low-probability race.

VeDirectArduino::WaitResult
VeDirectArduino::_attempt(const uint8_t* frame, size_t len,
                          uint8_t expectResp, bool matchReg, uint16_t reg,
                          unsigned long timeoutMs) {
    if (!_serial) {
        return WAIT_TIMEOUT;
    }

    _waiting    = true;
    _matched    = false;
    _errored    = false;
    _expectResp = expectResp;
    _matchReg   = matchReg;
    _expectReg  = reg;

    _serial->write(frame, len);
    _serial->flush();

    unsigned long start = millis();
    WaitResult result = WAIT_TIMEOUT;
    while ((millis() - start) < timeoutMs) {
        _service();  // pumps bytes; may set _matched/_errored via dispatch
        if (_matched) { result = WAIT_MATCHED; break; }
        if (_errored) { result = WAIT_ERROR;   break; }
    }

    _waiting = false;
    return result;
}

bool VeDirectArduino::_transceive(const uint8_t* frame, size_t len,
                                  uint8_t expectResp, bool matchReg,
                                  uint16_t reg, unsigned long timeoutMs) {
    WaitResult r = _attempt(frame, len, expectResp, matchReg, reg, timeoutMs);
    if (r == WAIT_TIMEOUT) {
        r = _attempt(frame, len, expectResp, matchReg, reg, timeoutMs);  // retry once
    }
    return r == WAIT_MATCHED;
}

// ---------------------------------------------------------------------------
// Generic HEX interface
// ---------------------------------------------------------------------------

bool VeDirectArduino::hexGet(uint16_t reg, uint32_t* valueOut,
                             unsigned long timeoutMs) {
    uint8_t frame[VeDirectHexProtocol::kMaxFrame];
    size_t len = VeDirectHexProtocol::buildGet(frame, reg);
    if (!_transceive(frame, len, VeDirectHex::RSP_GET, true, reg, timeoutMs)) {
        return false;
    }
    if (_respFrame.flags != VeDirectHex::FLAG_OK) {
        return false;  // unknown id / not supported / param error
    }
    if (valueOut) {
        *valueOut = _respFrame.value;
    }
    return true;
}

bool VeDirectArduino::hexSet(uint16_t reg, uint32_t value, uint8_t valueLen,
                             unsigned long timeoutMs) {
    uint8_t frame[VeDirectHexProtocol::kMaxFrame];
    size_t len = VeDirectHexProtocol::buildSet(frame, reg, value, valueLen);
    if (!_transceive(frame, len, VeDirectHex::RSP_SET, true, reg, timeoutMs)) {
        return false;
    }
    return _respFrame.flags == VeDirectHex::FLAG_OK;
}

bool VeDirectArduino::hexPing(unsigned long timeoutMs) {
    uint8_t frame[VeDirectHexProtocol::kMaxFrame];
    size_t len = VeDirectHexProtocol::buildPing(frame);
    return _transceive(frame, len, VeDirectHex::RSP_PING, false, 0, timeoutMs);
}

// ---------------------------------------------------------------------------
// Convenience methods
// ---------------------------------------------------------------------------

bool VeDirectArduino::getProductId(uint16_t* pid, unsigned long timeoutMs) {
    uint8_t frame[VeDirectHexProtocol::kMaxFrame];
    size_t len = VeDirectHexProtocol::buildCommand(
        frame, VeDirectHex::CMD_PRODUCT_ID, nullptr, 0);
    if (!_transceive(frame, len, VeDirectHex::RSP_DONE, false, 0, timeoutMs)) {
        return false;
    }
    if (pid) {
        // 4-byte reply: byte0=instance(0x00), bytes1-2=product id, byte3=0xFF.
        // 2-byte reply (old fw): product id directly (endianness unreliable).
        if (_respFrame.valueLen >= 3) {
            *pid = (uint16_t)((_respFrame.value >> 8) & 0xFFFF);
        } else {
            *pid = (uint16_t)(_respFrame.value & 0xFFFF);
        }
    }
    return true;
}

bool VeDirectArduino::getFirmwareVersion(uint16_t* ver, unsigned long timeoutMs) {
    uint8_t frame[VeDirectHexProtocol::kMaxFrame];
    size_t len = VeDirectHexProtocol::buildCommand(
        frame, VeDirectHex::CMD_APP_VERSION, nullptr, 0);
    if (!_transceive(frame, len, VeDirectHex::RSP_DONE, false, 0, timeoutMs)) {
        return false;
    }
    if (ver) {
        // e.g. 0x0116 -> v1.16. Top two bits of the high byte are the fw type.
        *ver = (uint16_t)(_respFrame.value & 0xFFFF);
    }
    return true;
}

bool VeDirectArduino::setLoadOutput(LoadMode mode, unsigned long timeoutMs) {
    return hexSet(VeDirectRegisters::LOAD_OUTPUT_CONTROL,
                  (uint32_t)mode, 1, timeoutMs);
}

bool VeDirectArduino::getLoadOutput(LoadMode* mode, unsigned long timeoutMs) {
    uint32_t v = 0;
    if (!hexGet(VeDirectRegisters::LOAD_OUTPUT_CONTROL, &v, timeoutMs)) {
        return false;
    }
    if (mode) {
        *mode = (LoadMode)(v & 0x0F);  // lower nibble = mode; upper bits reserved
    }
    return true;
}

// --- Charger settings: mV <-> 0.01 V (÷10), mA <-> 0.1 A (÷100) ---

bool VeDirectArduino::setChargeCurrentLimit(uint16_t mA, unsigned long timeoutMs) {
    return hexSet(VeDirectRegisters::MAX_CHARGE_CURRENT,
                  (uint32_t)(mA / 100), 2, timeoutMs);
}

bool VeDirectArduino::getChargeCurrentLimit(uint16_t* mA, unsigned long timeoutMs) {
    uint32_t v = 0;
    if (!hexGet(VeDirectRegisters::MAX_CHARGE_CURRENT, &v, timeoutMs)) {
        return false;
    }
    if (mA) *mA = (uint16_t)(v * 100);
    return true;
}

bool VeDirectArduino::setAbsorptionVoltage(uint16_t mV, unsigned long timeoutMs) {
    return hexSet(VeDirectRegisters::ABSORPTION_VOLTAGE,
                  (uint32_t)(mV / 10), 2, timeoutMs);
}

bool VeDirectArduino::getAbsorptionVoltage(uint16_t* mV, unsigned long timeoutMs) {
    uint32_t v = 0;
    if (!hexGet(VeDirectRegisters::ABSORPTION_VOLTAGE, &v, timeoutMs)) {
        return false;
    }
    if (mV) *mV = (uint16_t)(v * 10);
    return true;
}

bool VeDirectArduino::setFloatVoltage(uint16_t mV, unsigned long timeoutMs) {
    return hexSet(VeDirectRegisters::FLOAT_VOLTAGE,
                  (uint32_t)(mV / 10), 2, timeoutMs);
}

bool VeDirectArduino::getFloatVoltage(uint16_t* mV, unsigned long timeoutMs) {
    uint32_t v = 0;
    if (!hexGet(VeDirectRegisters::FLOAT_VOLTAGE, &v, timeoutMs)) {
        return false;
    }
    if (mV) *mV = (uint16_t)(v * 10);
    return true;
}

bool VeDirectArduino::setEqualisationVoltage(uint16_t mV, unsigned long timeoutMs) {
    return hexSet(VeDirectRegisters::EQUALISATION_VOLTAGE,
                  (uint32_t)(mV / 10), 2, timeoutMs);
}

bool VeDirectArduino::getEqualisationVoltage(uint16_t* mV, unsigned long timeoutMs) {
    uint32_t v = 0;
    if (!hexGet(VeDirectRegisters::EQUALISATION_VOLTAGE, &v, timeoutMs)) {
        return false;
    }
    if (mV) *mV = (uint16_t)(v * 10);
    return true;
}
