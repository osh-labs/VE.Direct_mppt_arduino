// Host unit tests for the VE.Direct HEX protocol: frame builder/decoder
// (VeDirectHexProtocol) and the transceive/async behaviour of VeDirectArduino
// (spec §8.2). Run with:  pio test -e native
//
// Part of the VeDirect_Arduino library. MIT licensed.

#include <unity.h>
#include <string>
#include <vector>

#include "VeDirectArduino.h"          // pulls in the mock <Arduino.h>
#include "VeDirectHexProtocol.h"

// --- Helpers ------------------------------------------------------------

static char hexChar(uint8_t n) {
    n &= 0x0F;
    return (n < 10) ? (char)('0' + n) : (char)('A' + n - 10);
}

// Build a HEX frame string (":...\n") for a given nibble and payload, with the
// correct trailing checksum byte (cmd + payload + checksum == 0x55).
static std::string makeHexFrame(uint8_t nibble, const std::vector<uint8_t>& payload) {
    uint8_t sum = (uint8_t)(nibble & 0x0F);
    for (uint8_t b : payload) sum += b;
    uint8_t cks = (uint8_t)(0x55 - sum);

    std::string s = ":";
    s += hexChar(nibble);
    for (uint8_t b : payload) { s += hexChar(b >> 4); s += hexChar(b); }
    s += hexChar(cks >> 4);
    s += hexChar(cks);
    s += "\n";
    return s;
}

static std::string builtToString(const uint8_t* buf, size_t len) {
    return std::string(reinterpret_cast<const char*>(buf), len);
}

// A minimal valid Text frame carrying a single V value.
static std::string makeTextFrame(int mv) {
    std::string f = "\r\nV\t" + std::to_string(mv);
    f += "\r\nChecksum\t";
    uint8_t sum = 0;
    for (char c : f) sum += (uint8_t)c;
    f += (char)((256 - sum) & 0xFF);
    return f;
}

// Async capture target passed through the callback ctx pointer.
struct AsyncCapture {
    int count = 0;
    uint16_t lastReg = 0;
    uint32_t lastValue = 0;
};
static void asyncCb(uint16_t reg, uint32_t value, void* ctx) {
    AsyncCapture* c = static_cast<AsyncCapture*>(ctx);
    c->count++;
    c->lastReg = reg;
    c->lastValue = value;
}

void setUp() { mockResetMillis(); }
void tearDown() {}

// --- Frame building -----------------------------------------------------

void test_build_get_matches_doc_example() {
    // Victron doc worked example: Get "Battery maximum current" (0xEDF0).
    uint8_t buf[VeDirectHexProtocol::kMaxFrame];
    size_t len = VeDirectHexProtocol::buildGet(buf, 0xEDF0);
    TEST_ASSERT_EQUAL_STRING(":7F0ED0071\n", builtToString(buf, len).c_str());
}

void test_build_ping() {
    uint8_t buf[VeDirectHexProtocol::kMaxFrame];
    size_t len = VeDirectHexProtocol::buildPing(buf);
    TEST_ASSERT_EQUAL_STRING(":154\n", builtToString(buf, len).c_str());
}

void test_build_set_load_off_roundtrip() {
    uint8_t buf[VeDirectHexProtocol::kMaxFrame];
    size_t len = VeDirectHexProtocol::buildSet(
        buf, VeDirectRegisters::LOAD_OUTPUT_CONTROL, 0x00, 1);

    // Decode the frame we just built and confirm its fields + checksum.
    VeDirectHexProtocol dec;
    bool done = false;
    for (size_t i = 0; i < len; ++i) done = dec.feed(buf[i]);
    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_TRUE(dec.lastFrame().checksumOk);
    TEST_ASSERT_EQUAL_HEX8(VeDirectHex::CMD_SET, dec.lastFrame().respType);
    TEST_ASSERT_TRUE(dec.lastFrame().hasReg);
    TEST_ASSERT_EQUAL_HEX16(0xEDAB, dec.lastFrame().reg);
    TEST_ASSERT_EQUAL_UINT32(0x00, dec.lastFrame().value);
}

void test_build_all_registers_valid_checksum() {
    const uint16_t regs[] = {
        VeDirectRegisters::ABSORPTION_VOLTAGE,
        VeDirectRegisters::FLOAT_VOLTAGE,
        VeDirectRegisters::EQUALISATION_VOLTAGE,
        VeDirectRegisters::MAX_CHARGE_CURRENT,
        VeDirectRegisters::LOAD_OUTPUT_CONTROL,
        VeDirectRegisters::YIELD_TODAY,
        VeDirectRegisters::PANEL_POWER,
        VeDirectRegisters::BATTERY_VOLTAGE,
    };
    for (uint16_t reg : regs) {
        uint8_t buf[VeDirectHexProtocol::kMaxFrame];

        size_t len = VeDirectHexProtocol::buildSet(buf, reg, 0x1234, 2);
        VeDirectHexProtocol d1;
        bool done = false;
        for (size_t i = 0; i < len; ++i) done = d1.feed(buf[i]);
        TEST_ASSERT_TRUE(done);
        TEST_ASSERT_TRUE(d1.lastFrame().checksumOk);
        TEST_ASSERT_EQUAL_HEX16(reg, d1.lastFrame().reg);
        TEST_ASSERT_EQUAL_UINT32(0x1234, d1.lastFrame().value);

        len = VeDirectHexProtocol::buildGet(buf, reg);
        VeDirectHexProtocol d2;
        done = false;
        for (size_t i = 0; i < len; ++i) done = d2.feed(buf[i]);
        TEST_ASSERT_TRUE(done);
        TEST_ASSERT_TRUE(d2.lastFrame().checksumOk);
        TEST_ASSERT_EQUAL_HEX16(reg, d2.lastFrame().reg);
        TEST_ASSERT_EQUAL_UINT8(0, d2.lastFrame().valueLen);  // Get has no value
    }
}

void test_decode_rejects_bad_checksum() {
    // Corrupt the doc example's checksum.
    VeDirectHexProtocol dec;
    std::string bad = ":7F0ED0072\n";  // 72 instead of 71
    bool done = false;
    for (char c : bad) done = dec.feed((uint8_t)c);
    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_FALSE(dec.lastFrame().checksumOk);
}

// --- Transceive via VeDirectArduino ------------------------------------

void test_hexget_response() {
    HardwareSerial serial;
    VeDirectArduino ve;
    ve.begin(serial, 19200, 3);  // no controller; probe returns false — fine

    // Controller replies to Get 0xEDF0 with value 0x0096 (150).
    serial.injectRx(makeHexFrame(VeDirectHex::RSP_GET, {0xF0, 0xED, 0x00, 0x96, 0x00}));

    uint32_t value = 0;
    bool ok = ve.hexGet(0xEDF0, &value, 200);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(0x0096, value);
}

void test_hexset_response_ok() {
    HardwareSerial serial;
    VeDirectArduino ve;
    ve.begin(serial, 19200, 3);

    // Echoed Set response with OK flags.
    serial.injectRx(makeHexFrame(VeDirectHex::RSP_SET, {0xAB, 0xED, 0x00, 0x00}));
    bool ok = ve.hexSet(VeDirectRegisters::LOAD_OUTPUT_CONTROL, 0x00, 1, 200);
    TEST_ASSERT_TRUE(ok);
}

void test_hexget_error_flags() {
    HardwareSerial serial;
    VeDirectArduino ve;
    ve.begin(serial, 19200, 3);

    // Response for the register but with "unknown id" flag set.
    serial.injectRx(makeHexFrame(VeDirectHex::RSP_GET,
                                 {0xF0, 0xED, VeDirectHex::FLAG_UNKNOWN_ID}));
    uint32_t value = 0;
    TEST_ASSERT_FALSE(ve.hexGet(0xEDF0, &value, 200));
}

void test_hexget_error_frame() {
    HardwareSerial serial;
    VeDirectArduino ve;
    ve.begin(serial, 19200, 3);

    // Explicit Error response (frame error, payload 0xAAAA).
    serial.injectRx(makeHexFrame(VeDirectHex::RSP_ERROR, {0xAA, 0xAA}));
    uint32_t value = 0;
    TEST_ASSERT_FALSE(ve.hexGet(0xEDF0, &value, 200));
}

void test_hexget_timeout() {
    HardwareSerial serial;
    VeDirectArduino ve;
    ve.begin(serial, 19200, 3);

    // Nothing injected: must return false (after one retry).
    uint32_t value = 0;
    unsigned long t0 = millis();
    bool ok = ve.hexGet(0xEDF0, &value, 50);
    unsigned long elapsed = millis() - t0;
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(100, elapsed);  // ~2 x timeout (retry)
}

void test_ping_response() {
    HardwareSerial serial;
    VeDirectArduino ve;
    ve.begin(serial, 19200, 3);

    serial.injectRx(makeHexFrame(VeDirectHex::RSP_PING, {0x16, 0x01}));  // v1.16
    TEST_ASSERT_TRUE(ve.hexPing(200));
}

void test_get_product_id() {
    HardwareSerial serial;
    VeDirectArduino ve;
    ve.begin(serial, 19200, 3);

    // 4-byte Done reply: instance(0x00), product id 0xA053 (LE 53 A0), reserved 0xFF.
    serial.injectRx(makeHexFrame(VeDirectHex::RSP_DONE, {0x00, 0x53, 0xA0, 0xFF}));
    uint16_t pid = 0;
    TEST_ASSERT_TRUE(ve.getProductId(&pid, 200));
    TEST_ASSERT_EQUAL_HEX16(0xA053, pid);
}

void test_set_load_output_convenience() {
    HardwareSerial serial;
    VeDirectArduino ve;
    ve.begin(serial, 19200, 3);

    serial.injectRx(makeHexFrame(VeDirectHex::RSP_SET, {0xAB, 0xED, 0x00, 0x04}));
    TEST_ASSERT_TRUE(ve.setLoadOutput(VeDirectArduino::LOAD_ON, 200));

    // Verify the transmitted control value was 0x04 (LOAD_ON), written to 0xEDAB.
    std::string tx = serial.txAsString();
    TEST_ASSERT_TRUE(tx.find(":8ABED000400") != std::string::npos ||
                     tx.find(":8ABED0004") != std::string::npos);
}

void test_async_callback() {
    HardwareSerial serial;
    VeDirectArduino ve;
    ve.begin(serial, 19200, 3);

    AsyncCapture cap;
    ve.onAsync(asyncCb, &cap);

    // Async: CHARGE_STATE (0x0201) -> Absorption (4).
    serial.injectRx(makeHexFrame(VeDirectHex::RSP_ASYNC, {0x01, 0x02, 0x00, 0x04}));
    ve.loop();

    TEST_ASSERT_EQUAL_INT(1, cap.count);
    TEST_ASSERT_EQUAL_HEX16(0x0201, cap.lastReg);
    TEST_ASSERT_EQUAL_UINT32(4, cap.lastValue);
}

void test_async_queue_overflow() {
    HardwareSerial serial;
    VeDirectArduino ve;
    ve.begin(serial, 19200, 3);
    // No callback -> poll queue (depth 4). Inject 5 notifications.
    for (uint8_t i = 1; i <= 5; ++i) {
        serial.injectRx(makeHexFrame(VeDirectHex::RSP_ASYNC, {i, 0x00, 0x00, i}));
    }
    ve.loop();

    // Oldest (reg 1) discarded; regs 2,3,4,5 retained in order.
    TEST_ASSERT_TRUE(ve.asyncPending());
    uint16_t reg; uint32_t val;
    for (uint16_t expect = 2; expect <= 5; ++expect) {
        TEST_ASSERT_TRUE(ve.popAsync(&reg, &val));
        TEST_ASSERT_EQUAL_HEX16(expect, reg);
        TEST_ASSERT_EQUAL_UINT32(expect, val);
    }
    TEST_ASSERT_FALSE(ve.popAsync(&reg, &val));  // now empty
}

void test_interleaved_text_and_hex() {
    HardwareSerial serial;
    VeDirectArduino ve;
    ve.begin(serial, 19200, 3);

    AsyncCapture cap;
    ve.onAsync(asyncCb, &cap);

    // A complete Text frame, then a HEX async frame, then another Text frame —
    // all in one contiguous byte stream.
    std::string stream;
    stream += makeTextFrame(12000);
    stream += makeHexFrame(VeDirectHex::RSP_ASYNC, {0x01, 0x02, 0x00, 0x05});  // CS=Float
    stream += makeTextFrame(13500);
    serial.injectRx(stream);

    ve.loop();

    TEST_ASSERT_EQUAL_INT(1, cap.count);
    TEST_ASSERT_EQUAL_HEX16(0x0201, cap.lastReg);
    TEST_ASSERT_EQUAL_UINT32(5, cap.lastValue);
    TEST_ASSERT_EQUAL_INT32(13500, ve.data().battV_mV);  // second Text frame
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_build_get_matches_doc_example);
    RUN_TEST(test_build_ping);
    RUN_TEST(test_build_set_load_off_roundtrip);
    RUN_TEST(test_build_all_registers_valid_checksum);
    RUN_TEST(test_decode_rejects_bad_checksum);
    RUN_TEST(test_hexget_response);
    RUN_TEST(test_hexset_response_ok);
    RUN_TEST(test_hexget_error_flags);
    RUN_TEST(test_hexget_error_frame);
    RUN_TEST(test_hexget_timeout);
    RUN_TEST(test_ping_response);
    RUN_TEST(test_get_product_id);
    RUN_TEST(test_set_load_output_convenience);
    RUN_TEST(test_async_callback);
    RUN_TEST(test_async_queue_overflow);
    RUN_TEST(test_interleaved_text_and_hex);
    return UNITY_END();
}
