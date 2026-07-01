// Host unit tests for VeDirectTextParser (spec §8.1). Run with:
//   pio test -e native
//
// Part of the VeDirect_Arduino library. MIT licensed.

#include <unity.h>
#include <string>
#include <vector>
#include <utility>

#include "VeDirectTextParser.h"

// --- Frame construction helper -----------------------------------------
//
// Builds a VE.Direct Text frame from label/value pairs, appending the correct
// trailing checksum byte so the whole frame sums to zero (mod 256) — matching
// what the parser accumulates (leading "\r\n" of each record included).
static std::string makeFrame(const std::vector<std::pair<std::string,std::string>>& fields) {
    std::string f;
    for (const auto& kv : fields) {
        f += "\r\n";
        f += kv.first;
        f += "\t";
        f += kv.second;
    }
    f += "\r\nChecksum\t";
    uint8_t sum = 0;
    for (char c : f) sum += (uint8_t)c;
    uint8_t cksum = (uint8_t)((256 - sum) & 0xFF);
    f += (char)cksum;
    return f;
}

// Feed an entire byte string into the parser. Returns the number of completed
// frames (parse() returning true).
static int feedAll(VeDirectTextParser& p, const std::string& s) {
    int frames = 0;
    for (char c : s) {
        if (p.parse((uint8_t)c)) frames++;
    }
    return frames;
}

// A representative full MPPT 75/15 frame.
static std::vector<std::pair<std::string,std::string>> sampleFields() {
    return {
        {"PID",  "0xA053"},
        {"V",    "26201"},
        {"I",    "1400"},
        {"VPV",  "38000"},
        {"PPV",  "35"},
        {"IL",   "500"},
        {"LOAD", "ON"},
        {"CS",   "3"},
        {"MPPT", "2"},
        {"ERR",  "0"},
        {"H20",  "12"},
        {"H22",  "34"},
        {"HSDS", "77"},
    };
}

void setUp() {}
void tearDown() {}

void test_valid_frame() {
    VeDirectTextParser p;
    int frames = feedAll(p, makeFrame(sampleFields()));
    TEST_ASSERT_EQUAL_INT(1, frames);
    TEST_ASSERT_TRUE(p.lastFrameValid());

    const VeDirectData& d = p.data();
    TEST_ASSERT_EQUAL_INT32(26201, d.battV_mV);
    TEST_ASSERT_EQUAL_INT32(1400,  d.battI_mA);
    TEST_ASSERT_EQUAL_INT32(38000, d.panelV_mV);
    TEST_ASSERT_EQUAL_INT32(35,    d.panelW);
    TEST_ASSERT_EQUAL_INT32(500,   d.loadI_mA);
    TEST_ASSERT_TRUE(d.loadOn);
    TEST_ASSERT_EQUAL_INT(3, d.chargeState);
    TEST_ASSERT_EQUAL_INT(2, d.mpptMode);
    TEST_ASSERT_EQUAL_INT(0, d.errorCode);
    TEST_ASSERT_EQUAL_INT32(120, d.yieldTodayWh);      // 12 * 10 Wh
    TEST_ASSERT_EQUAL_INT32(340, d.yieldYesterdayWh);  // 34 * 10 Wh
    TEST_ASSERT_EQUAL_INT(77, d.daySequence);
    TEST_ASSERT_TRUE(d.frameValid);
}

void test_load_off_parsed() {
    VeDirectTextParser p;
    auto fields = sampleFields();
    for (auto& kv : fields) if (kv.first == "LOAD") kv.second = "OFF";
    feedAll(p, makeFrame(fields));
    TEST_ASSERT_TRUE(p.lastFrameValid());
    TEST_ASSERT_FALSE(p.data().loadOn);
}

void test_checksum_fail_retains_previous() {
    VeDirectTextParser p;
    // First a good frame.
    feedAll(p, makeFrame(sampleFields()));
    TEST_ASSERT_TRUE(p.lastFrameValid());
    int32_t goodV = p.data().battV_mV;

    // Now a frame with a corrupted checksum byte (last byte +1).
    auto fields = sampleFields();
    for (auto& kv : fields) if (kv.first == "V") kv.second = "12345";
    std::string bad = makeFrame(fields);
    bad[bad.size() - 1] = (char)(bad[bad.size() - 1] + 1);  // corrupt checksum

    int frames = feedAll(p, bad);
    TEST_ASSERT_EQUAL_INT(1, frames);            // frame completed...
    TEST_ASSERT_FALSE(p.lastFrameValid());       // ...but invalid
    TEST_ASSERT_EQUAL_INT32(goodV, p.data().battV_mV);  // previous data retained
}

void test_partial_then_complete() {
    VeDirectTextParser p;
    // A receiver that joins the stream mid-frame sees an incomplete fragment
    // (no Checksum record). That fragment's bytes contaminate the running
    // checksum of the *next* frame boundary, so exactly one following frame is
    // rejected — after which the parser recovers cleanly. Critically, the
    // fragment's field values must never be committed as valid data.
    std::string partial = "\r\nV\t99999\r\nI\t100";  // never terminated
    feedAll(p, partial);
    TEST_ASSERT_FALSE(p.lastFrameValid());
    TEST_ASSERT_INT32_WITHIN(0, 0, p.data().battV_mV);  // still default 0, not 99999

    auto fields = sampleFields();
    for (auto& kv : fields) if (kv.first == "V") kv.second = "13000";
    // Two complete frames: the first may be rejected due to fragment
    // contamination; the second is always clean.
    feedAll(p, makeFrame(fields));
    feedAll(p, makeFrame(fields));

    TEST_ASSERT_TRUE(p.lastFrameValid());
    TEST_ASSERT_EQUAL_INT32(13000, p.data().battV_mV);  // clean parse; never 99999
}

void test_buffer_overflow_resets() {
    VeDirectTextParser p;
    // A value far exceeding the 32-char value buffer must discard the frame.
    auto fields = sampleFields();
    std::string huge(80, '7');
    for (auto& kv : fields) if (kv.first == "V") kv.second = huge;
    feedAll(p, makeFrame(fields));
    TEST_ASSERT_FALSE(p.lastFrameValid());

    // Parser must recover and parse the next good frame.
    auto ok = sampleFields();
    for (auto& kv : ok) if (kv.first == "V") kv.second = "14000";
    feedAll(p, makeFrame(ok));
    TEST_ASSERT_TRUE(p.lastFrameValid());
    TEST_ASSERT_EQUAL_INT32(14000, p.data().battV_mV);
}

void test_multi_frame_sequence() {
    VeDirectTextParser p;
    std::string stream;
    for (int i = 0; i < 5; ++i) {
        auto fields = sampleFields();
        for (auto& kv : fields) if (kv.first == "V") kv.second = std::to_string(12000 + i);
        stream += makeFrame(fields);
    }
    int frames = feedAll(p, stream);
    TEST_ASSERT_EQUAL_INT(5, frames);
    TEST_ASSERT_TRUE(p.lastFrameValid());
    TEST_ASSERT_EQUAL_INT32(12004, p.data().battV_mV);  // last frame's value
}

void test_all_cs_values() {
    const int codes[] = {0, 2, 3, 4, 5, 6, 7, 245, 247, 252};
    for (int code : codes) {
        VeDirectTextParser p;
        auto fields = sampleFields();
        for (auto& kv : fields) if (kv.first == "CS") kv.second = std::to_string(code);
        feedAll(p, makeFrame(fields));
        TEST_ASSERT_TRUE(p.lastFrameValid());
        TEST_ASSERT_EQUAL_INT(code, p.data().chargeState);
    }
}

void test_yield_conversion() {
    VeDirectTextParser p;
    auto fields = sampleFields();
    for (auto& kv : fields) {
        if (kv.first == "H20") kv.second = "250";   // 2.50 kWh
        if (kv.first == "H22") kv.second = "0";
    }
    feedAll(p, makeFrame(fields));
    TEST_ASSERT_TRUE(p.lastFrameValid());
    TEST_ASSERT_EQUAL_INT32(2500, p.data().yieldTodayWh);  // 250 * 10 Wh
    TEST_ASSERT_EQUAL_INT32(0, p.data().yieldYesterdayWh);
}

void test_negative_current() {
    VeDirectTextParser p;
    auto fields = sampleFields();
    for (auto& kv : fields) if (kv.first == "I") kv.second = "-2500";  // discharging
    feedAll(p, makeFrame(fields));
    TEST_ASSERT_TRUE(p.lastFrameValid());
    TEST_ASSERT_EQUAL_INT32(-2500, p.data().battI_mA);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_frame);
    RUN_TEST(test_load_off_parsed);
    RUN_TEST(test_checksum_fail_retains_previous);
    RUN_TEST(test_partial_then_complete);
    RUN_TEST(test_buffer_overflow_resets);
    RUN_TEST(test_multi_frame_sequence);
    RUN_TEST(test_all_cs_values);
    RUN_TEST(test_yield_conversion);
    RUN_TEST(test_negative_current);
    return UNITY_END();
}
