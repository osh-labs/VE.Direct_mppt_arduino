// VeDirectTextParser.cpp — see VeDirectTextParser.h
//
// Part of the VeDirect_Arduino library. MIT licensed.

#include "VeDirectTextParser.h"

#include <string.h>
#include <stdlib.h>

VeDirectTextParser::VeDirectTextParser()
    : _hexSink(nullptr), _hexCtx(nullptr) {
    reset();
}

void VeDirectTextParser::reset() {
    _state          = IDLE;
    _checksum       = 0;
    _nameIdx        = 0;
    _valueIdx       = 0;
    _overflow       = false;
    _lastFrameValid = false;
    _name[0]        = '\0';
    _value[0]       = '\0';
    // Note: reset() intentionally does not clear _data — the last good frame
    // must remain readable across a reset.
}

void VeDirectTextParser::setHexByteSink(HexByteSink sink, void* ctx) {
    _hexSink = sink;
    _hexCtx  = ctx;
}

// Parse one integer field value with strtol (handles optional sign). VE.Direct
// numeric fields are plain decimal; hex fields (e.g. PID) are not parsed here.
static int32_t parseInt(const char* s) {
    return (int32_t)strtol(s, nullptr, 10);
}

void VeDirectTextParser::commitField(const char* name, const char* value) {
    // Map each known VE.Direct label into the scratch struct. Unknown labels
    // (PID, FW, SER#, etc.) are ignored.
    if (strcmp(name, "V") == 0) {
        _scratch.battV_mV = parseInt(value);
    } else if (strcmp(name, "I") == 0) {
        _scratch.battI_mA = parseInt(value);
    } else if (strcmp(name, "VPV") == 0) {
        _scratch.panelV_mV = parseInt(value);
    } else if (strcmp(name, "PPV") == 0) {
        _scratch.panelW = parseInt(value);
    } else if (strcmp(name, "IL") == 0) {
        _scratch.loadI_mA = parseInt(value);
    } else if (strcmp(name, "LOAD") == 0) {
        _scratch.loadOn = (strcmp(value, "ON") == 0);
    } else if (strcmp(name, "CS") == 0) {
        _scratch.chargeState = (int)parseInt(value);
    } else if (strcmp(name, "MPPT") == 0) {
        _scratch.mpptMode = (int)parseInt(value);
    } else if (strcmp(name, "ERR") == 0) {
        _scratch.errorCode = (int)parseInt(value);
    } else if (strcmp(name, "H20") == 0) {
        // 0.01 kWh units -> Wh: 0.01 kWh = 10 Wh.
        _scratch.yieldTodayWh = parseInt(value) * 10;
    } else if (strcmp(name, "H22") == 0) {
        _scratch.yieldYesterdayWh = parseInt(value) * 10;
    } else if (strcmp(name, "HSDS") == 0 || strcmp(name, "Hsds") == 0) {
        _scratch.daySequence = (int)parseInt(value);
    }
}

bool VeDirectTextParser::parse(uint8_t inbyte) {
    // A ':' anywhere except while reading the single checksum byte marks the
    // start of an interleaved HEX frame. (The checksum byte can legitimately be
    // 0x3A ':', so it must not be treated as a HEX start.)
    if (inbyte == ':' && _state != CHECKSUM) {
        _state = RECORD_HEX;
    }

    // The running checksum covers every Text byte in the frame. HEX bytes are
    // excluded.
    if (_state != RECORD_HEX) {
        _checksum += inbyte;
    }

    switch (_state) {
    case IDLE:
        // Records are separated by "\r\n"; wait for the '\n'.
        if (inbyte == '\n') {
            _state = RECORD_BEGIN;
        }
        break;

    case RECORD_BEGIN:
        _nameIdx = 0;
        _name[_nameIdx++] = (char)inbyte;
        _state = RECORD_NAME;
        break;

    case RECORD_NAME:
        if (inbyte == '\t') {
            // A '\t' terminates the label. The special label "Checksum" marks
            // end-of-frame; its value is a single raw checksum byte.
            if (_nameIdx < kNameMax) {
                _name[_nameIdx] = '\0';
                if (strcmp(_name, "Checksum") == 0) {
                    _state = CHECKSUM;
                    break;
                }
            }
            _valueIdx = 0;
            _state = RECORD_VALUE;
        } else {
            if (_nameIdx < kNameMax - 1) {
                _name[_nameIdx++] = (char)inbyte;
            } else {
                _overflow = true;
            }
        }
        break;

    case RECORD_VALUE:
        if (inbyte == '\n') {
            // '\r' was skipped below; '\n' terminates the record.
            if (_valueIdx < kValueMax && !_overflow) {
                _value[_valueIdx] = '\0';
                commitField(_name, _value);
            }
            _state = RECORD_BEGIN;
        } else if (inbyte == '\r') {
            // skip
        } else {
            if (_valueIdx < kValueMax - 1) {
                _value[_valueIdx++] = (char)inbyte;
            } else {
                _overflow = true;
            }
        }
        break;

    case CHECKSUM: {
        // The checksum byte has just been added to _checksum above. A valid
        // frame sums to zero (mod 256).
        bool valid = (_checksum == 0) && !_overflow;
        _lastFrameValid = valid;
        if (valid) {
            _scratch.frameValid = true;
            _data = _scratch;              // commit only on success
        }
        // Prepare a fresh scratch/frame regardless of validity.
        _scratch  = VeDirectData();
        _checksum = 0;
        _overflow = false;
        _state    = IDLE;
        return true;                        // one frame completed
    }

    case RECORD_HEX:
        // A HEX frame interrupts (and abandons) any in-progress Text frame;
        // discard the partial Text scratch so it cannot be mistaken for a
        // completed frame once Text parsing resumes.
        if (_hexSink) {
            if (_hexSink(inbyte, _hexCtx)) {
                _scratch  = VeDirectData();
                _checksum = 0;
                _overflow = false;
                _state    = IDLE;
            }
        } else {
            // No sink registered: just skip the HEX frame.
            if (inbyte == '\n') {
                _scratch  = VeDirectData();
                _checksum = 0;
                _overflow = false;
                _state    = IDLE;
            }
        }
        break;
    }

    return false;
}
