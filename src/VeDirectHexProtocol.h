// VeDirectHexProtocol.h
//
// VE.Direct *HEX* protocol frame builder and response decoder. Internal to the
// VeDirect_Arduino library. No Arduino dependency (only <stdint.h>/<stddef.h>)
// so it can be unit-tested on a host.
//
// Frame format (verified against Victron "VE.Direct Protocol", Rev 18):
//
//   :<cmd-nibble><payload-bytes...><checksum-byte>\n
//
//   - Everything after ':' is ASCII hex, uppercase. The command is a single
//     nibble; every following field is transmitted as byte pairs.
//   - Multi-byte values (register id, data) are little-endian (LSB first).
//   - Checksum: (cmd + sum(payload bytes) + checksum) & 0xFF == 0x55.
//     The ':' and trailing '\n' are NOT included in the sum.
//
// Part of the VeDirect_Arduino library. MIT licensed.

#ifndef VEDIRECT_HEX_PROTOCOL_H
#define VEDIRECT_HEX_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

namespace VeDirectHex {
    // Command nibbles (host -> device)
    constexpr uint8_t CMD_BOOT         = 0x0;
    constexpr uint8_t CMD_PING         = 0x1;
    constexpr uint8_t CMD_APP_VERSION  = 0x3;
    constexpr uint8_t CMD_PRODUCT_ID   = 0x4;
    constexpr uint8_t CMD_RESTART      = 0x6;
    constexpr uint8_t CMD_GET          = 0x7;
    constexpr uint8_t CMD_SET          = 0x8;
    constexpr uint8_t CMD_ASYNC        = 0xA;

    // Response nibbles (device -> host)
    constexpr uint8_t RSP_DONE         = 0x1;  // App-version / Product-id result
    constexpr uint8_t RSP_UNKNOWN      = 0x3;
    constexpr uint8_t RSP_ERROR        = 0x4;  // frame error (payload 0xAAAA)
    constexpr uint8_t RSP_PING         = 0x5;  // ping reply (payload = version)
    constexpr uint8_t RSP_GET          = 0x7;
    constexpr uint8_t RSP_SET          = 0x8;
    constexpr uint8_t RSP_ASYNC        = 0xA;

    // Get/Set reply flags (byte after the register id in a 7/8/A frame)
    constexpr uint8_t FLAG_OK            = 0x00;
    constexpr uint8_t FLAG_UNKNOWN_ID    = 0x01;
    constexpr uint8_t FLAG_NOT_SUPPORTED = 0x02;
    constexpr uint8_t FLAG_PARAM_ERROR   = 0x04;

    constexpr uint8_t CHECKSUM_TARGET  = 0x55;
}

class VeDirectHexProtocol {
public:
    // Maximum encoded frame length in bytes (':' + nibble + up to ~10 byte
    // pairs + checksum pair + '\n'). Generously sized.
    static const size_t kMaxFrame = 40;

    // A decoded incoming HEX frame.
    struct Frame {
        uint8_t  respType;    // response nibble
        bool     hasReg;      // true for Get/Set/Async responses (7/8/A)
        uint16_t reg;         // register id (valid when hasReg)
        uint8_t  flags;       // reply flags (valid when hasReg)
        uint32_t value;       // decoded little-endian value
        uint8_t  valueLen;    // number of value bytes decoded
        bool     checksumOk;  // frame checksum validated

        Frame() : respType(0), hasReg(false), reg(0), flags(0),
                  value(0), valueLen(0), checksumOk(false) {}
    };

    VeDirectHexProtocol();

    // --- Incoming frame decoding ---

    // Discard any partially received frame.
    void reset();

    // Feed one raw byte of an incoming HEX frame (from the leading ':' through
    // the terminating '\n'). Returns true when '\n' completes a frame; retrieve
    // it with lastFrame(). A completed frame with checksumOk == false is still
    // reported (returns true) so the caller can count/ignore it.
    bool feed(uint8_t b);

    const Frame& lastFrame() const { return _frame; }

    // --- Outgoing frame building ---
    // Each writes an encoded frame (including ':' and trailing '\n') into `out`
    // (must hold at least kMaxFrame bytes) and returns its length.

    static size_t buildGet(uint8_t* out, uint16_t reg);
    static size_t buildSet(uint8_t* out, uint16_t reg,
                           uint32_t value, uint8_t valueLen);
    static size_t buildPing(uint8_t* out);
    // Command with an arbitrary (possibly empty) payload, e.g. CMD_PRODUCT_ID.
    static size_t buildCommand(uint8_t* out, uint8_t cmd,
                               const uint8_t* payload, size_t payloadLen);

private:
    void decode();

    static const size_t kBufMax = kMaxFrame * 2;

    char    _buf[kBufMax];  // accumulated ASCII hex chars (excludes ':' and '\n')
    size_t  _len;
    bool    _active;        // between ':' and '\n'
    bool    _overflow;
    Frame   _frame;
};

#endif // VEDIRECT_HEX_PROTOCOL_H
