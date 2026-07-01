// VeDirectHexProtocol.cpp — see VeDirectHexProtocol.h
//
// Part of the VeDirect_Arduino library. MIT licensed.

#include "VeDirectHexProtocol.h"

namespace {

char nibbleToHex(uint8_t n) {
    n &= 0x0F;
    return (n < 10) ? (char)('0' + n) : (char)('A' + (n - 10));
}

// Returns 0..15, or 0xFF if not a hex digit.
uint8_t hexToNibble(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0xFF;
}

} // namespace

VeDirectHexProtocol::VeDirectHexProtocol() {
    reset();
}

void VeDirectHexProtocol::reset() {
    _len      = 0;
    _active   = false;
    _overflow = false;
}

// ---------------------------------------------------------------------------
// Building
// ---------------------------------------------------------------------------

size_t VeDirectHexProtocol::buildCommand(uint8_t* out, uint8_t cmd,
                                         const uint8_t* payload,
                                         size_t payloadLen) {
    size_t i = 0;
    out[i++] = ':';
    out[i++] = (uint8_t)nibbleToHex(cmd);

    uint8_t sum = (uint8_t)(cmd & 0x0F);
    for (size_t p = 0; p < payloadLen; ++p) {
        uint8_t b = payload[p];
        sum += b;
        out[i++] = (uint8_t)nibbleToHex(b >> 4);
        out[i++] = (uint8_t)nibbleToHex(b);
    }

    uint8_t checksum = (uint8_t)(VeDirectHex::CHECKSUM_TARGET - sum);
    out[i++] = (uint8_t)nibbleToHex(checksum >> 4);
    out[i++] = (uint8_t)nibbleToHex(checksum);
    out[i++] = '\n';
    return i;
}

size_t VeDirectHexProtocol::buildGet(uint8_t* out, uint16_t reg) {
    // payload: register (LE) + flags(0x00)
    uint8_t payload[3];
    payload[0] = (uint8_t)(reg & 0xFF);
    payload[1] = (uint8_t)(reg >> 8);
    payload[2] = 0x00;
    return buildCommand(out, VeDirectHex::CMD_GET, payload, 3);
}

size_t VeDirectHexProtocol::buildSet(uint8_t* out, uint16_t reg,
                                     uint32_t value, uint8_t valueLen) {
    if (valueLen > 4) valueLen = 4;
    // payload: register (LE) + flags(0x00) + value (LE)
    uint8_t payload[3 + 4];
    payload[0] = (uint8_t)(reg & 0xFF);
    payload[1] = (uint8_t)(reg >> 8);
    payload[2] = 0x00;
    for (uint8_t v = 0; v < valueLen; ++v) {
        payload[3 + v] = (uint8_t)((value >> (8 * v)) & 0xFF);
    }
    return buildCommand(out, VeDirectHex::CMD_SET, payload, (size_t)(3 + valueLen));
}

size_t VeDirectHexProtocol::buildPing(uint8_t* out) {
    return buildCommand(out, VeDirectHex::CMD_PING, nullptr, 0);
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

bool VeDirectHexProtocol::feed(uint8_t b) {
    if (b == ':') {
        _len      = 0;
        _active   = true;
        _overflow = false;
        return false;
    }
    if (!_active) {
        return false;
    }
    if (b == '\n') {
        _active = false;
        decode();
        return true;
    }
    if (b == '\r') {
        return false;  // tolerate stray CR
    }
    if (_len < kBufMax) {
        _buf[_len++] = (char)b;
    } else {
        _overflow = true;
    }
    return false;
}

void VeDirectHexProtocol::decode() {
    _frame = Frame();  // reset to defaults (checksumOk = false)

    if (_overflow || _len < 1) {
        return;
    }
    // After the nibble, the remainder must be an even number of hex chars
    // (byte pairs), and there must be at least one byte (the checksum).
    if (((_len - 1) % 2) != 0) {
        return;
    }

    uint8_t nibble = hexToNibble(_buf[0]);
    if (nibble == 0xFF) {
        return;
    }

    size_t  nBytes = (_len - 1) / 2;
    if (nBytes < 1) {
        return;  // need at least a checksum byte
    }

    // Decode all byte pairs.
    uint8_t bytes[VeDirectHexProtocol::kMaxFrame];
    for (size_t k = 0; k < nBytes; ++k) {
        uint8_t hi = hexToNibble(_buf[1 + 2 * k]);
        uint8_t lo = hexToNibble(_buf[1 + 2 * k + 1]);
        if (hi == 0xFF || lo == 0xFF) {
            return;  // invalid hex char
        }
        bytes[k] = (uint8_t)((hi << 4) | lo);
    }

    // Checksum: nibble + all decoded bytes (incl. the checksum byte) == 0x55.
    uint8_t sum = nibble;
    for (size_t k = 0; k < nBytes; ++k) {
        sum += bytes[k];
    }

    _frame.respType   = nibble;
    _frame.checksumOk = (sum == VeDirectHex::CHECKSUM_TARGET);

    size_t payloadLen = nBytes - 1;  // exclude checksum byte

    if (nibble == VeDirectHex::RSP_GET ||
        nibble == VeDirectHex::RSP_SET ||
        nibble == VeDirectHex::RSP_ASYNC) {
        // register (2, LE) + flags (1) + value (LE)
        if (payloadLen < 3) {
            _frame.checksumOk = false;  // malformed
            return;
        }
        _frame.hasReg   = true;
        _frame.reg      = (uint16_t)(bytes[0] | (bytes[1] << 8));
        _frame.flags    = bytes[2];
        size_t vlen     = payloadLen - 3;
        if (vlen > 4) vlen = 4;
        uint32_t value = 0;
        for (size_t v = 0; v < vlen; ++v) {
            value |= (uint32_t)bytes[3 + v] << (8 * v);
        }
        _frame.value    = value;
        _frame.valueLen = (uint8_t)vlen;
    } else {
        // No register field (Done / Ping / Error / Unknown). The payload, if
        // any, is a little-endian value.
        _frame.hasReg = false;
        size_t vlen = payloadLen;
        if (vlen > 4) vlen = 4;
        uint32_t value = 0;
        for (size_t v = 0; v < vlen; ++v) {
            value |= (uint32_t)bytes[v] << (8 * v);
        }
        _frame.value    = value;
        _frame.valueLen = (uint8_t)vlen;
    }
}
