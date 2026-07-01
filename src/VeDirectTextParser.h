// VeDirectTextParser.h
//
// Byte-level state machine for the VE.Direct *Text* protocol. Internal to the
// VeDirect_Arduino library. Has no Arduino dependency (only <stdint.h>) so it
// can be unit-tested on a host.
//
// The state machine follows Victron's published reference algorithm. It also
// transparently handles HEX frames interleaved in the byte stream: when a ':'
// is seen outside of the Text checksum byte, subsequent bytes (through the
// terminating '\n') are handed to an optional "hex byte sink" instead of being
// parsed as Text. This is how Text and HEX frames sharing one UART are
// demultiplexed (spec §6.4).
//
// Part of the VeDirect_Arduino library. MIT licensed.

#ifndef VEDIRECT_TEXT_PARSER_H
#define VEDIRECT_TEXT_PARSER_H

#include <stdint.h>
#include "VeDirectData.h"

class VeDirectTextParser {
public:
    // Sink for bytes belonging to an interleaved HEX frame. Called once per
    // byte, starting with the leading ':' and continuing through the frame.
    // Must return true when the byte just consumed terminates the HEX frame
    // (i.e. the '\n'); the Text state machine then resumes.
    typedef bool (*HexByteSink)(uint8_t b, void* ctx);

    VeDirectTextParser();

    // Discard any in-progress frame and return to the idle state.
    void reset();

    // Route interleaved HEX bytes to this sink. Pass nullptr to disable
    // (in which case HEX frames are simply skipped, which is what the
    // standalone Text unit tests rely on).
    void setHexByteSink(HexByteSink sink, void* ctx);

    // Feed a single received byte. Returns true exactly once per completed
    // Text frame — at the moment the checksum byte is consumed — regardless of
    // whether the checksum was valid. Query lastFrameValid() to distinguish.
    bool parse(uint8_t b);

    // The most recently *validated* frame's data. Data from a frame that fails
    // checksum is not committed here, so this always reflects the last good
    // frame (or defaults if none yet).
    const VeDirectData& data() const { return _data; }

    // Whether the most recently completed frame passed checksum validation.
    bool lastFrameValid() const { return _lastFrameValid; }

private:
    enum State {
        IDLE,          // waiting for the '\n' that begins a record
        RECORD_BEGIN,  // first char of a label
        RECORD_NAME,   // accumulating label until '\t'
        RECORD_VALUE,  // accumulating value until '\r'/'\n'
        CHECKSUM,      // the "Checksum" label was seen; next byte is the value
        RECORD_HEX     // a ':' was seen; bytes go to the hex sink
    };

    static const uint8_t kNameMax  = 16;  // longest label + NUL
    static const uint8_t kValueMax = 33;  // longest value + NUL

    void commitField(const char* name, const char* value);

    State   _state;
    uint8_t _checksum;                 // running 8-bit sum of the current frame
    char    _name[kNameMax];
    uint8_t _nameIdx;
    char    _value[kValueMax];
    uint8_t _valueIdx;
    bool    _overflow;                 // label/value overflowed this frame

    // Fields are accumulated into a scratch struct and only copied into _data
    // once the frame's checksum validates.
    VeDirectData _scratch;
    VeDirectData _data;
    bool         _lastFrameValid;

    HexByteSink _hexSink;
    void*       _hexCtx;
};

#endif // VEDIRECT_TEXT_PARSER_H
