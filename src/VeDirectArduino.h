// VeDirectArduino.h
//
// Public API for the VeDirect_Arduino library: a portable Arduino
// implementation of the Victron Energy VE.Direct serial protocol (Text
// telemetry + HEX register read/write and async notifications).
//
// Single include: #include <VeDirectArduino.h>
//
// See README.md and the examples/ folder for usage. Verified against the
// official Victron "VE.Direct Protocol", Rev 18.
//
// Part of the VeDirect_Arduino library. MIT licensed.

#ifndef VEDIRECT_ARDUINO_H
#define VEDIRECT_ARDUINO_H

#include <Arduino.h>
#include <stdint.h>

#include "VeDirectData.h"
#include "VeDirectRegisters.h"
#include "VeDirectTextParser.h"
#include "VeDirectHexProtocol.h"

// Depth of the async-notification fallback queue (poll model). Override by
// defining VEDIRECT_ASYNC_QUEUE_DEPTH before including this header.
#ifndef VEDIRECT_ASYNC_QUEUE_DEPTH
#define VEDIRECT_ASYNC_QUEUE_DEPTH 4
#endif

// Async notification callback signature. `ctx` is the pointer supplied to
// onAsync() and is passed through unchanged (typically an object pointer).
typedef void (*VeDirectAsyncCallback)(uint16_t reg, uint32_t value, void* ctx);

class VeDirectArduino {
public:
    // -------------------------------------------------------------------------
    // Enums
    // -------------------------------------------------------------------------

    // Load-output switching mode — register 0xEDAB (LOAD_OUTPUT_CONTROL).
    // NOTE: these values are the controller's real register encoding (verified
    // against Victron Rev 18) and differ from the original spec draft, whose
    // values (AUTO=0/ON=1/OFF=2) were derived from an incorrect register.
    enum LoadMode : uint8_t {
        LOAD_OFF   = 0x00,  // load output forced off
        LOAD_AUTO  = 0x01,  // automatic control / BatteryLife (default)
        LOAD_ALT1  = 0x02,  // off < 11.1 V, on > 13.1 V
        LOAD_ALT2  = 0x03,  // off < 11.8 V, on > 14.0 V
        LOAD_ON    = 0x04,  // load forced on (no low-voltage disconnect guard)
        LOAD_USER1 = 0x05,  // user-defined levels (0xED9D / 0xED9C)
        LOAD_USER2 = 0x06,
        LOAD_AES   = 0x07   // automatic energy selector
    };

    // CS field / device-state values (Text "CS", HEX register 0x0201).
    enum ChargeState : int {
        CS_OFF              = 0,
        CS_FAULT            = 2,
        CS_BULK             = 3,
        CS_ABSORPTION       = 4,
        CS_FLOAT            = 5,
        CS_STORAGE          = 6,
        CS_EQUALIZE         = 7,
        CS_STARTING         = 245,   // "Wake-up" in newer docs
        CS_AUTO_EQUALIZE    = 247,
        CS_EXTERNAL_CONTROL = 252
    };

    VeDirectArduino();

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    // Initialize with a HardwareSerial instance. Calls serial.begin(baud), then
    // probes for a valid Text frame for up to timeoutMs milliseconds.
    // Returns true if a valid frame was received (controller present/active);
    // false on timeout (no controller detected). A false return does not
    // prevent later use — the controller may simply connect later.
    bool begin(HardwareSerial& serial,
               unsigned long   baud      = 19200,
               unsigned long   timeoutMs = 3000);

    // Call from loop(). Processes all available serial bytes without blocking.
    // Returns true if a complete, checksum-valid Text frame was parsed during
    // this call.
    bool loop();

    // -------------------------------------------------------------------------
    // Text Protocol — Data Access
    // -------------------------------------------------------------------------

    const VeDirectData& data() const { return _text.data(); }
    bool lastFrameValid() const { return _text.lastFrameValid(); }

    // Milliseconds since the last valid Text frame, or ULONG_MAX if none since
    // begin().
    unsigned long msSinceLastFrame() const;

    // -------------------------------------------------------------------------
    // HEX Protocol — Generic Interface
    // -------------------------------------------------------------------------

    // Write a value to any HEX register. valueLen is the value width (1/2/4).
    // Blocks until a Set response or Error frame arrives, or timeoutMs elapses;
    // retries once on timeout. Returns true only on an OK response.
    bool hexSet(uint16_t      reg,
                uint32_t      value,
                uint8_t       valueLen  = 1,
                unsigned long timeoutMs = 500);

    // Read any HEX register into *valueOut. Blocks until a Get response or
    // Error frame arrives, or timeoutMs elapses; retries once on timeout.
    bool hexGet(uint16_t      reg,
                uint32_t*     valueOut,
                unsigned long timeoutMs = 500);

    // Send a HEX Ping and wait for a Ping response. Returns true if the
    // controller responds within timeoutMs.
    bool hexPing(unsigned long timeoutMs = 500);

    // -------------------------------------------------------------------------
    // HEX Protocol — Async Notifications
    // -------------------------------------------------------------------------

    // Register a callback for async (type-A) HEX frames. Pass nullptr to clear.
    // When a callback is set it is invoked directly and notifications are NOT
    // queued; when cleared, notifications accumulate in the poll queue.
    void onAsync(VeDirectAsyncCallback cb, void* ctx = nullptr);

    bool asyncPending() const { return _asyncCount > 0; }

    // Pop the oldest queued async notification. Returns false if empty.
    bool popAsync(uint16_t* reg, uint32_t* value);

    // -------------------------------------------------------------------------
    // HEX Protocol — Convenience Methods
    // -------------------------------------------------------------------------

    // System info (use the dedicated Victron commands, not a register read).
    bool getProductId(uint16_t* pid, unsigned long timeoutMs = 500);
    bool getFirmwareVersion(uint16_t* ver, unsigned long timeoutMs = 500);

    // Load output.
    bool setLoadOutput(LoadMode mode,  unsigned long timeoutMs = 500);
    bool getLoadOutput(LoadMode* mode, unsigned long timeoutMs = 500);

    // Charger settings. Voltages in mV, currents in mA (converted to/from the
    // controller's 0.01 V / 0.1 A register units internally).
    // WARNING: these registers live in non-volatile memory — do not write them
    // repeatedly from a control loop.
    bool setChargeCurrentLimit(uint16_t mA,  unsigned long timeoutMs = 500);
    bool getChargeCurrentLimit(uint16_t* mA, unsigned long timeoutMs = 500);
    bool setAbsorptionVoltage(uint16_t mV,   unsigned long timeoutMs = 500);
    bool getAbsorptionVoltage(uint16_t* mV,  unsigned long timeoutMs = 500);
    bool setFloatVoltage(uint16_t mV,        unsigned long timeoutMs = 500);
    bool getFloatVoltage(uint16_t* mV,       unsigned long timeoutMs = 500);
    bool setEqualisationVoltage(uint16_t mV, unsigned long timeoutMs = 500);
    bool getEqualisationVoltage(uint16_t* mV,unsigned long timeoutMs = 500);

    // Battery type. To change any of the user charge parameters above the
    // controller requires the battery type to be user-defined (0xFF); otherwise
    // it rejects the write with error 119. setBatteryTypeUser() switches it.
    bool setBatteryTypeUser(unsigned long timeoutMs = 500);
    bool getBatteryType(uint8_t* out, unsigned long timeoutMs = 500);

    // Battery temperature compensation, in 0.01 mV/K (register unit). Signed:
    // 0 disables compensation. Stored as sn16 on the wire.
    bool setTempCompensation(int16_t centiMvPerK, unsigned long timeoutMs = 500);
    bool getTempCompensation(int16_t* out,        unsigned long timeoutMs = 500);

    // Automatic equalisation mode — days between cycles, 0 = off.
    bool setAutoEqualisation(uint8_t mode,  unsigned long timeoutMs = 500);
    bool getAutoEqualisation(uint8_t* out,  unsigned long timeoutMs = 500);

    // Nominal system voltage, in whole volts (e.g. 12 for a 12 V system).
    bool setSystemVoltage(uint8_t volts,  unsigned long timeoutMs = 500);
    bool getSystemVoltage(uint8_t* out,   unsigned long timeoutMs = 500);

    // Maximum absorption time, in 0.01 hours (register unit).
    bool setMaxAbsorptionTime(uint16_t centiHours, unsigned long timeoutMs = 500);
    bool getMaxAbsorptionTime(uint16_t* out,       unsigned long timeoutMs = 500);

    // Charger on/off via remote control. setCharger() first (re-)enables the
    // remote-control on/off feature (REMOTE_CONTROL_USED bit 1) and then writes
    // DEVICE_MODE. The enable mask is re-sent on every call by design: it clears
    // when the controller powers up, and the bits are set-only so a re-send is
    // harmless. isCharging() reads DEVICE_STATE — any non-zero state is charging
    // (0 = OFF / not charging).
    bool setCharger(bool on,   unsigned long timeoutMs = 500);
    bool isCharging(bool* out, unsigned long timeoutMs = 500);

private:
    enum WaitResult { WAIT_MATCHED, WAIT_ERROR, WAIT_TIMEOUT };

    // Read and process all currently-available serial bytes. Returns true if a
    // valid Text frame completed during processing.
    bool _service();

    // Feed one HEX byte to the decoder; on a completed frame, dispatch it.
    // Returns true when the byte terminated a HEX frame.
    bool _hexFeed(uint8_t b);
    static bool _hexSinkTrampoline(uint8_t b, void* ctx);

    void _handleHexFrame(const VeDirectHexProtocol::Frame& f);
    void _dispatchAsync(uint16_t reg, uint32_t value);

    // Transmit an already-built frame and wait for the expected response.
    WaitResult _attempt(const uint8_t* frame, size_t len,
                        uint8_t expectResp, bool matchReg, uint16_t reg,
                        unsigned long timeoutMs);
    // _attempt with one retry on timeout.
    bool _transceive(const uint8_t* frame, size_t len,
                     uint8_t expectResp, bool matchReg, uint16_t reg,
                     unsigned long timeoutMs);

    HardwareSerial*    _serial;
    VeDirectTextParser _text;
    VeDirectHexProtocol _hex;

    bool          _haveFrame;
    unsigned long _lastFrameMs;

    // Blocking-wait state.
    bool     _waiting;
    uint8_t  _expectResp;
    bool     _matchReg;
    uint16_t _expectReg;
    bool     _matched;
    bool     _errored;
    VeDirectHexProtocol::Frame _respFrame;

    // Async callback + fallback queue.
    VeDirectAsyncCallback _asyncCb;
    void*                 _asyncCtx;
    struct AsyncEntry { uint16_t reg; uint32_t value; };
    AsyncEntry _asyncQueue[VEDIRECT_ASYNC_QUEUE_DEPTH];
    uint8_t    _asyncHead;   // index of oldest
    uint8_t    _asyncCount;  // number queued
};

#endif // VEDIRECT_ARDUINO_H
