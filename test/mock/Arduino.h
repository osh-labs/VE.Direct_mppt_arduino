// Arduino.h — minimal host mock of the Arduino core, used ONLY for native
// (host) unit tests via `pio test -e native`. It is placed on the include path
// only for the native environment (see platformio.ini) and is never compiled
// into a real firmware build.
//
// Provides:
//   - millis(): a monotonic counter that auto-advances 1 ms per call, so the
//     library's blocking timeout loops terminate deterministically in tests.
//   - HardwareSerial: an injectable/inspectable UART mock. Tests push controller
//     bytes with injectRx() and read back what the library transmitted via the
//     txBuffer() accessor.
//
// Part of the VeDirect_Arduino library. MIT licensed.

#ifndef VEDIRECT_MOCK_ARDUINO_H
#define VEDIRECT_MOCK_ARDUINO_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <deque>
#include <vector>
#include <string>

// --- Time ---------------------------------------------------------------

inline unsigned long g_mockMillis = 0;

// Each call advances the virtual clock by 1 ms. This guarantees that the
// library's `while (millis() - start < timeout)` loops make progress and time
// out even when no external code advances the clock.
inline unsigned long millis() {
    return g_mockMillis++;
}

inline void mockResetMillis() { g_mockMillis = 0; }
inline void mockAdvanceMillis(unsigned long ms) { g_mockMillis += ms; }

inline void delay(unsigned long ms) { g_mockMillis += ms; }

// --- HardwareSerial mock -----------------------------------------------

class HardwareSerial {
public:
    HardwareSerial() : _lastBaud(0) {}

    // Arduino API used by the library.
    void begin(unsigned long baud) { _lastBaud = baud; }
    void end() {}

    int available() { return (int)_rx.size(); }

    int read() {
        if (_rx.empty()) return -1;
        uint8_t b = _rx.front();
        _rx.pop_front();
        return (int)b;
    }

    int peek() {
        if (_rx.empty()) return -1;
        return (int)_rx.front();
    }

    size_t write(uint8_t b) {
        _tx.push_back(b);
        if (_loopback) _rx.push_back(b);
        return 1;
    }

    size_t write(const uint8_t* buf, size_t n) {
        for (size_t i = 0; i < n; ++i) write(buf[i]);
        return n;
    }

    void flush() {}

    // --- Test helpers ---
    void injectRx(const uint8_t* data, size_t n) {
        for (size_t i = 0; i < n; ++i) _rx.push_back(data[i]);
    }
    void injectRx(const char* s) {
        injectRx(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
    }
    void injectRx(const std::string& s) {
        injectRx(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
    void injectByte(uint8_t b) { _rx.push_back(b); }

    // Bytes the library has transmitted, in order.
    const std::vector<uint8_t>& txBuffer() const { return _tx; }
    std::string txAsString() const {
        return std::string(_tx.begin(), _tx.end());
    }
    void clearTx() { _tx.clear(); }
    void clearRx() { _rx.clear(); }

    // When enabled, every transmitted byte is echoed back into RX (used by the
    // loopback integration-style tests).
    void setLoopback(bool on) { _loopback = on; }

    unsigned long lastBaud() const { return _lastBaud; }

private:
    std::deque<uint8_t> _rx;
    std::vector<uint8_t> _tx;
    unsigned long _lastBaud;
    bool _loopback = false;
};

#endif // VEDIRECT_MOCK_ARDUINO_H
