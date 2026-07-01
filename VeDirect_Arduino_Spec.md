# VeDirect_Arduino — Library Specification

**Project:** VeDirect_Arduino — Standalone Arduino Library  
**Author:** Christopher E. Lee / United Consulting  
**Status:** Draft — Capture in Progress  
**Last Updated:** 2026-06-30  
**Relationship:** Dependency of UnitedOSM v2 firmware. Developed as a separate project; consumed via PlatformIO library dependency.

---

## 1. Project Overview

### Purpose

VeDirect_Arduino is a portable Arduino library implementing the Victron Energy VE.Direct serial protocol for microcontrollers. It provides:

- Full **VE.Direct Text protocol** parsing — one-way telemetry from the controller at 1-second intervals
- Full **VE.Direct HEX protocol** — bidirectional register read/write, required for remote load output control

The library is designed to be platform-portable across any Arduino-compatible target with a hardware `HardwareSerial` instance. Primary development target is the STM32L433 (Blues Swan on Notecarrier CX). The API design does not assume any RTOS, threading, or heap allocation beyond what the Arduino framework provides.

### Why This Exists

No well-maintained Arduino library implements both VE.Direct Text and HEX protocols with STM32 compatibility. Existing libraries either implement Text protocol only, are ESP32-specific, or are unmaintained. This library fills that gap and is designed to be useful beyond UnitedOSM.

### Scope

| In scope | Out of scope |
|---|---|
| VE.Direct Text protocol — all standard MPPT fields | VE.Direct MPPT HEX register map beyond load output control |
| VE.Direct HEX protocol — load output register (`0xEDA8`) | Victron BMS / Smart Battery integration |
| STM32L433 primary target | VE.Direct Bluetooth or VE.Can |
| Arduino-compatible portability (any HardwareSerial) | Native USB or WiFi transport |
| MPPT controller support (75/15 verified) | BMV battery monitor protocol |

### Intended Consumers

- UnitedOSM v2 firmware (`VeDirectDriver` wrapper — see UnitedOSM Software Spec §3.8)
- Any maker or engineer integrating a Victron MPPT controller with an Arduino-compatible MCU

### License

MIT. No attribution requirement for end products.

### Repository

Hosted separately from UnitedOSM. Suggested name: `VeDirect_Arduino`. Published to the Arduino Library Registry and PlatformIO Registry on first stable release.

---

## 2. Supported Hardware

### Verified Controller

| Controller | Firmware tested | Notes |
|---|---|---|
| Victron BlueSolar MPPT 75/15 | TBD at bench test | Primary target for UnitedOSM |

### Expected Compatibility

All Victron MPPT controllers that implement VE.Direct Text and HEX protocols use the same frame format. The library should be compatible with the full MPPT product line (75/10, 75/15, 100/15, 100/20, etc.) without modification. Untested models are noted as expected-compatible in the README.

### VE.Direct Electrical Interface

| Parameter | Value |
|---|---|
| Baud rate | 19200 |
| Data format | 8N1 |
| Logic levels | 3.3V TTL |
| Connector | Victron VE.Direct 4-pin JST PH |
| Level shifting | Not required when interfacing with 3.3V MCU |

VE.Direct pinout (Victron JST PH 4-pin):

| Pin | Signal | Direction |
|---|---|---|
| 1 | GND | — |
| 2 | VE.Direct TX | Controller → MCU RX |
| 3 | VE.Direct RX | MCU TX → Controller |
| 4 | +5V (power, do not use for logic) | — |

Only pins 1, 2, and 3 are used. Pin 4 (+5V) is not connected to the MCU.

---

## 3. VE.Direct Text Protocol

### 3.1 Frame Format

The controller transmits ASCII key-value frames continuously at 1-second intervals. Each frame consists of multiple `\r\n`-terminated label-value lines followed by a checksum line.

```
:Label\t<value>\r\n
:Label\t<value>\r\n
...
Checksum\t<byte>\r\n
```

A complete frame is delimited by the `Checksum` label. The checksum byte is the value that makes the sum of all bytes in the frame (including `Checksum\t`) equal to zero modulo 256.

### 3.2 Parsed Fields

The library parses the following fields from the MPPT 75/15 text frame. Fields not present in a given controller's output are left at their zero/default values.

| VE.Direct Label | Type | Unit | Library field | Notes |
|---|---|---|---|---|
| `V` | int | mV | `battV_mV` | Battery terminal voltage |
| `I` | int | mA | `battI_mA` | Battery current; positive = charge, negative = discharge |
| `VPV` | int | mV | `panelV_mV` | Panel voltage |
| `PPV` | int | W | `panelW` | Panel power |
| `IL` | int | mA | `loadI_mA` | Load current |
| `LOAD` | string | ON/OFF | `loadOn` | Load output state; parsed to bool |
| `CS` | int | enum | `chargeState` | Charge state — see §3.3 |
| `MPPT` | int | enum | `mpptMode` | MPPT operating mode — see §3.4 |
| `ERR` | int | enum | `errorCode` | Error code — see §3.5 |
| `H20` | int | 0.01 kWh | `yieldTodayWh` | Yield today; converted to Wh |
| `H22` | int | 0.01 kWh | `yieldYesterdayWh` | Yield yesterday; converted to Wh |
| `Hsds` | int | — | `daySequence` | Day sequence number |

**Note on battery SOC:** The MPPT 75/15 does not report state of charge via VE.Direct Text. SOC is a battery monitor feature (BMV series). The library does not synthesize SOC from voltage. Consumers that need SOC must either: (a) use a connected BMV, or (b) implement a voltage-to-SOC lookup appropriate for their battery chemistry. UnitedOSM firmware implements a 3S LiIon voltage-to-SOC lookup table in the `VeDirectDriver` wrapper — not in this library.

### 3.3 Charge State Values (`CS`)

| Value | Meaning |
|---|---|
| 0 | Off |
| 2 | Fault |
| 3 | Bulk |
| 4 | Absorption |
| 5 | Float |
| 7 | Equalize (manual) |
| 245 | Starting |
| 247 | Auto equalize / Recondition |
| 252 | External control |

### 3.4 MPPT Mode Values

| Value | Meaning |
|---|---|
| 0 | Off |
| 1 | Voltage/current limited |
| 2 | MPPT active |

### 3.5 Error Code Values

| Value | Meaning |
|---|---|
| 0 | No error |
| 2 | Battery voltage too high |
| 17 | Charger temperature too high |
| 18 | Charger over-current |
| 19 | Charger current reversed |
| 20 | Bulk time limit exceeded |
| 21 | Current sensor issue |
| 26 | Terminals overheated |
| 33 | Input voltage too high (solar panel) |
| 34 | Input current too high (solar panel) |
| 38 | Input shutdown (due to excessive battery voltage) |
| 39 | Input shutdown (due to current flow during off mode) |
| 65 | Lost communication with one of devices |
| 67 | Synchronised charging device configuration issue |
| 68 | BMS connection lost |
| 116 | Factory calibration data lost |
| 117 | Invalid/incompatible firmware |
| 119 | User settings invalid |

### 3.6 Checksum Validation

The parser validates each frame checksum before updating the data struct. A frame that fails checksum is discarded — the previous valid frame's data remains in the struct. The `lastFrameValid` flag is set accordingly.

---

## 4. VE.Direct HEX Protocol

### 4.1 Overview

The HEX protocol is a request/response layer multiplexed on the same UART as the Text protocol. HEX frames are distinguished by a leading `:` character.

HEX frames may arrive asynchronously during Text protocol reception. The parser must handle interleaved Text and HEX frames on the same byte stream without corruption.

### 4.2 Frame Format

```
:<command><register><flags><value><checksum>\n
```

All fields are ASCII hexadecimal. The frame is terminated with `\n` (0x0A).

| Field | Length | Description |
|---|---|---|
| `:` | 1 byte | Frame start delimiter |
| Command | 1 nibble | Operation type — see §4.3 |
| Register | 4 nibbles | 16-bit register address, little-endian |
| Flags | 2 nibbles | 8-bit flags byte; typically `0x00` |
| Value | variable | Register value, little-endian |
| Checksum | 2 nibbles | 8-bit checksum |
| `\n` | 1 byte | Frame terminator |

**Checksum calculation:** The sum of all bytes in the frame from `:` through the last value nibble, plus the checksum byte, must equal `0x55`.

### 4.3 Command Types

| Value | Name | Direction | Description |
|---|---|---|---|
| `1` | Get | MCU → Controller | Read register |
| `3` | Set | MCU → Controller | Write register |
| `4` | Async | Controller → MCU | Unsolicited notification |
| `7` | Response | Controller → MCU | Response to Get or Set |
| `8` | Error | Controller → MCU | Error response |
| `A` | Ping | MCU → Controller | Connectivity check |
| `5` | Ping response | Controller → MCU | Response to Ping |

### 4.4 Register Map

All register addresses must be verified against the Victron VE.Direct HEX Protocol document before implementation. Addresses marked **verify** are organizationally correct but should be confirmed against the official document. Addresses marked **confirmed** are well-established in Victron's published documentation.

The `VeDirectRegisters` namespace (see §5.3) exposes all of these as named constants. Consumers should always use the named constants rather than raw hex literals.

#### System Information (read-only)

| Register | Address | Type | Unit | Status | Description |
|---|---|---|---|---|---|
| Product ID | `0x0100` | uint16 | — | Confirmed | Controller model identifier |
| Firmware version | `0x0101` | uint16 | — | Confirmed | Running firmware version |
| Serial number | `0x010A` | string | — | Verify | ASCII serial number; requires string read handling |

#### Charger Settings (read/write)

| Register | Address | Type | Unit | Status | Description |
|---|---|---|---|---|---|
| Absorption voltage | `0xED8D` | uint16 | mV | Verify | Target absorption charge voltage |
| Float voltage | `0xED8E` | uint16 | mV | Verify | Target float voltage |
| Equalization voltage | `0xED8F` | uint16 | mV | Verify | Target equalization voltage |
| Max absorption time | `0xED90` | uint16 | min | Verify | Maximum time in absorption phase |
| Max charge current | `0xEDA0` | uint16 | mA | Verify | Charge current limit |

#### Load Output (read/write)

| Register | Address | Type | Unit | Status | Description |
|---|---|---|---|---|---|
| Load output control | `0xEDA8` | uint8 | enum | Confirmed | Switching mode — see values below |
| Load output voltage | `0xEDA9` | uint16 | mV | Verify | Load disconnect voltage threshold |
| Load output current | `0xEDAB` | uint16 | mA | Verify | Load current (read-only on some models) |

**Load output control values (`0xEDA8`):**

| Value | Mode | Description |
|---|---|---|
| `0x00` | Auto | Load managed by MPPT algorithm |
| `0x01` | On | Load forced on |
| `0x02` | Off | Load forced off |

#### History (read-only)

| Register | Address | Type | Unit | Status | Description |
|---|---|---|---|---|---|
| Yield total | `0xEDB1` | uint32 | 0.01 kWh | Verify | Cumulative yield since installation |
| Yield today | `0xEDB2` | uint16 | 0.01 kWh | Verify | Yield in current day |
| Yield yesterday | `0xEDB4` | uint16 | 0.01 kWh | Verify | Yield in previous day |
| Max power today | `0xEDB7` | uint16 | W | Verify | Peak panel power today |
| Max power yesterday | `0xEDB8` | uint16 | W | Verify | Peak panel power yesterday |

#### Real-Time Data (read-only via HEX — also available in Text frames)

These registers can be polled via HEX Get, but their values are also delivered every second via the Text protocol. Prefer Text protocol values for real-time telemetry; use HEX Get only when an on-demand single read is needed.

| Register | Address | Type | Unit | Status | Description |
|---|---|---|---|---|---|
| Battery voltage | `0xED8B` | uint16 | mV | Verify | Same as Text `V` field |
| Battery current | `0xED8C` | int16 | mA | Verify | Same as Text `I` field |
| Panel voltage | `0xEDBB` | uint16 | mV | Verify | Same as Text `VPV` field |
| Panel power | `0xEDB9` | uint16 | W | Verify | Same as Text `PPV` field |
| Charge state | `0xEDA1` | uint8 | enum | Verify | Same as Text `CS` field |
| Error code | `0xEDB0` | uint8 | enum | Verify | Same as Text `ERR` field |

### 4.5 Async Notifications

The controller sends unsolicited HEX frames (command type `4`) when register values change — for example, when charge state transitions from Bulk to Absorption, or when the load output switches. These are distinct from responses to Get/Set requests.

The library handles async frames during `loop()` processing. Two consumption models are supported:

**Callback model (preferred):** Register a function to be called whenever an async frame arrives. The callback receives the register address and new value.

**Poll model (fallback):** After each `loop()` call, check `asyncPending()` and consume queued notifications via `popAsync()`. A fixed-size queue (default 4 entries) holds notifications until consumed. Overflow discards the oldest entry.

The library does not filter or interpret async notifications — it delivers the raw register address and value. The consumer maps register addresses to meaning using `VeDirectRegisters` constants.

### 4.6 Ping

The HEX Ping command (type `A`) verifies that a controller is present and responding to HEX protocol requests without reading or writing any register. Useful for connectivity checks distinct from Text frame reception.

`hexPing()` sends a Ping frame and blocks for up to `timeoutMs` milliseconds waiting for a Ping Response (type `5`). Returns `true` if a response is received.

---

## 5. Public API

### 5.1 Class: `VeDirectArduino`

```cpp
#include <VeDirectArduino.h>

// Async notification callback signature
typedef void (*VeDirectAsyncCallback)(uint16_t reg, uint32_t value, void* ctx);

class VeDirectArduino {
public:

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    // Initialize with a HardwareSerial instance.
    // Probes for a valid Text frame for up to timeoutMs milliseconds.
    // Returns true if a valid frame was received — controller present and active.
    // Returns false on timeout — no controller detected.
    bool begin(HardwareSerial& serial,
               unsigned long   baud      = 19200,
               unsigned long   timeoutMs = 3000);

    // Call in loop(). Processes all available serial bytes without blocking.
    // Returns true if a complete valid Text frame was parsed since the last call.
    bool loop();


    // -------------------------------------------------------------------------
    // Text Protocol — Data Access
    // -------------------------------------------------------------------------

    // Returns a const reference to the most recently parsed Text frame data.
    // All fields are zero/default until the first valid frame is received.
    const VeDirectData& data() const;

    // True if the most recently completed Text frame passed checksum validation.
    bool lastFrameValid() const;

    // Milliseconds elapsed since the last valid Text frame.
    // Returns ULONG_MAX if no valid frame has been received since begin().
    unsigned long msSinceLastFrame() const;


    // -------------------------------------------------------------------------
    // HEX Protocol — Generic Interface
    // -------------------------------------------------------------------------

    // Write a value to any HEX register.
    // valueLen: byte width of the value (1, 2, or 4 bytes).
    // Blocks until a Response or Error frame arrives, or timeoutMs elapses.
    // Retries once on timeout before returning false.
    bool hexSet(uint16_t      reg,
                uint32_t      value,
                uint8_t       valueLen  = 1,
                unsigned long timeoutMs = 500);

    // Read any HEX register.
    // Writes the result to *valueOut on success.
    // Blocks until a Response or Error frame arrives, or timeoutMs elapses.
    bool hexGet(uint16_t      reg,
                uint32_t*     valueOut,
                unsigned long timeoutMs = 500);

    // Send a HEX Ping and wait for a Ping Response.
    // Returns true if the controller responds within timeoutMs.
    bool hexPing(unsigned long timeoutMs = 500);


    // -------------------------------------------------------------------------
    // HEX Protocol — Async Notifications
    // -------------------------------------------------------------------------

    // Register a callback invoked whenever an async (type 4) HEX frame arrives.
    // ctx is passed through to the callback unchanged — use for object pointer.
    // Pass nullptr to clear a previously registered callback.
    void onAsync(VeDirectAsyncCallback cb, void* ctx = nullptr);

    // Returns true if there are unread async notifications in the queue.
    bool asyncPending() const;

    // Pop the oldest async notification from the queue.
    // Returns false if the queue is empty.
    bool popAsync(uint16_t* reg, uint32_t* value);


    // -------------------------------------------------------------------------
    // HEX Protocol — Convenience Methods
    // -------------------------------------------------------------------------

    // --- System info ---
    bool getProductId(uint16_t*     pid, unsigned long timeoutMs = 500);
    bool getFirmwareVersion(uint16_t* ver, unsigned long timeoutMs = 500);

    // --- Load output ---
    bool setLoadOutput(LoadMode mode,    unsigned long timeoutMs = 500);
    bool getLoadOutput(LoadMode* mode,   unsigned long timeoutMs = 500);

    // --- Charger settings ---
    // All voltages in mV; all currents in mA.
    bool setChargeCurrentLimit(uint16_t mA,  unsigned long timeoutMs = 500);
    bool getChargeCurrentLimit(uint16_t* mA, unsigned long timeoutMs = 500);
    bool setAbsorptionVoltage(uint16_t mV,   unsigned long timeoutMs = 500);
    bool getAbsorptionVoltage(uint16_t* mV,  unsigned long timeoutMs = 500);
    bool setFloatVoltage(uint16_t mV,        unsigned long timeoutMs = 500);
    bool getFloatVoltage(uint16_t* mV,       unsigned long timeoutMs = 500);
    bool setEqualisationVoltage(uint16_t mV, unsigned long timeoutMs = 500);
    bool getEqualisationVoltage(uint16_t* mV,unsigned long timeoutMs = 500);


    // -------------------------------------------------------------------------
    // Enums
    // -------------------------------------------------------------------------

    enum LoadMode : uint8_t {
        LOAD_AUTO = 0x00,
        LOAD_ON   = 0x01,
        LOAD_OFF  = 0x02
    };

    enum ChargeState : int {
        CS_OFF              = 0,
        CS_FAULT            = 2,
        CS_BULK             = 3,
        CS_ABSORPTION       = 4,
        CS_FLOAT            = 5,
        CS_EQUALIZE         = 7,
        CS_STARTING         = 245,
        CS_AUTO_EQUALIZE    = 247,
        CS_EXTERNAL_CONTROL = 252
    };
};
```

### 5.2 Struct: `VeDirectData`

```cpp
struct VeDirectData {
    // Battery
    int32_t battV_mV;           // Battery voltage (mV)
    int32_t battI_mA;           // Battery current (mA); positive = charging

    // Panel
    int32_t panelV_mV;          // Panel voltage (mV)
    int32_t panelW;             // Panel power (W)

    // Load
    int32_t loadI_mA;           // Load current (mA)
    bool    loadOn;             // Load output state

    // Status
    int     chargeState;        // CS field value — use ChargeState enum
    int     mpptMode;           // MPPT operating mode (0=off, 1=limited, 2=active)
    int     errorCode;          // ERR field value

    // Yield
    int32_t yieldTodayWh;       // Yield today (Wh; converted from 0.01 kWh Text field)
    int32_t yieldYesterdayWh;   // Yield yesterday (Wh)

    // Metadata
    int     daySequence;        // Hsds day sequence number
    bool    frameValid;         // true if last frame passed checksum
};
```

### 5.3 Namespace: `VeDirectRegisters`

Centralized register address constants. Consumers should always reference these by name rather than using raw hex literals.

```cpp
namespace VeDirectRegisters {

    // System info (read-only)
    constexpr uint16_t PRODUCT_ID            = 0x0100;  // uint16
    constexpr uint16_t FIRMWARE_VERSION      = 0x0101;  // uint16
    constexpr uint16_t SERIAL_NUMBER         = 0x010A;  // string; requires special read

    // Charger settings (read/write) — verify addresses against Victron HEX protocol doc
    constexpr uint16_t ABSORPTION_VOLTAGE    = 0xED8D;  // uint16, mV
    constexpr uint16_t FLOAT_VOLTAGE         = 0xED8E;  // uint16, mV
    constexpr uint16_t EQUALISATION_VOLTAGE  = 0xED8F;  // uint16, mV
    constexpr uint16_t MAX_ABSORPTION_TIME   = 0xED90;  // uint16, min
    constexpr uint16_t MAX_CHARGE_CURRENT    = 0xEDA0;  // uint16, mA

    // Load output (read/write) — verify addresses against Victron HEX protocol doc
    constexpr uint16_t LOAD_OUTPUT_CONTROL   = 0xEDA8;  // uint8; confirmed
    constexpr uint16_t LOAD_OUTPUT_VOLTAGE   = 0xEDA9;  // uint16, mV
    constexpr uint16_t LOAD_OUTPUT_CURRENT   = 0xEDAB;  // uint16, mA (read-only on some models)

    // History (read-only) — verify addresses against Victron HEX protocol doc
    constexpr uint16_t YIELD_TOTAL           = 0xEDB1;  // uint32, 0.01 kWh
    constexpr uint16_t YIELD_TODAY           = 0xEDB2;  // uint16, 0.01 kWh
    constexpr uint16_t YIELD_YESTERDAY       = 0xEDB4;  // uint16, 0.01 kWh
    constexpr uint16_t MAX_POWER_TODAY       = 0xEDB7;  // uint16, W
    constexpr uint16_t MAX_POWER_YESTERDAY   = 0xEDB8;  // uint16, W

    // Real-time data via HEX — prefer Text protocol for telemetry; use these for on-demand reads
    // Verify addresses against Victron HEX protocol doc
    constexpr uint16_t BATTERY_VOLTAGE       = 0xED8B;  // uint16, mV
    constexpr uint16_t BATTERY_CURRENT       = 0xED8C;  // int16, mA
    constexpr uint16_t PANEL_VOLTAGE         = 0xEDBB;  // uint16, mV
    constexpr uint16_t PANEL_POWER           = 0xEDB9;  // uint16, W
    constexpr uint16_t CHARGE_STATE          = 0xEDA1;  // uint8, enum
    constexpr uint16_t ERROR_CODE            = 0xEDB0;  // uint8, enum

} // namespace VeDirectRegisters
```

### 5.4 Usage Examples

#### Basic telemetry

```cpp
#include <VeDirectArduino.h>

VeDirectArduino vedirect;

void setup() {
    Serial.begin(115200);
    if (!vedirect.begin(Serial1, 19200, 3000)) {
        Serial.println("VE.Direct: no controller detected");
    }
}

void loop() {
    if (vedirect.loop()) {
        const VeDirectData& d = vedirect.data();
        Serial.printf("Batt: %.3fV  Panel: %dW  CS: %d  Load: %s\n",
            d.battV_mV / 1000.0f,
            d.panelW,
            d.chargeState,
            d.loadOn ? "ON" : "OFF");
    }
}
```

#### Load control via convenience method

```cpp
// Force load off
if (!vedirect.setLoadOutput(VeDirectArduino::LOAD_OFF)) {
    Serial.println("Load OFF command failed or timed out");
}

// Return to auto (MPPT managed)
vedirect.setLoadOutput(VeDirectArduino::LOAD_AUTO);
```

#### Generic register access for any register

```cpp
// Read product ID using generic hexGet
uint32_t pid = 0;
if (vedirect.hexGet(VeDirectRegisters::PRODUCT_ID, &pid)) {
    Serial.printf("Product ID: 0x%04X\n", (uint16_t)pid);
}

// Read and set absorption voltage
uint32_t absV = 0;
vedirect.hexGet(VeDirectRegisters::ABSORPTION_VOLTAGE, &absV);
Serial.printf("Absorption voltage: %.3fV\n", absV / 1000.0f);

// Nudge absorption voltage up by 100mV
vedirect.hexSet(VeDirectRegisters::ABSORPTION_VOLTAGE, absV + 100, 2);
```

#### Async notification handling via callback

```cpp
void onVeDirectAsync(uint16_t reg, uint32_t value, void* ctx) {
    if (reg == VeDirectRegisters::CHARGE_STATE) {
        Serial.printf("Charge state changed to: %d\n", (int)value);
    }
    if (reg == VeDirectRegisters::LOAD_OUTPUT_CONTROL) {
        Serial.printf("Load output changed to: %d\n", (int)value);
    }
}

void setup() {
    vedirect.begin(Serial1);
    vedirect.onAsync(onVeDirectAsync, nullptr);
}
```

---

## 6. Internal Architecture

### 6.1 File Structure

```
VeDirect_Arduino/
├── library.properties                  — Arduino/PlatformIO library manifest
├── README.md                           — wiring, usage, supported products, examples
├── LICENSE                             — MIT
├── src/
│   ├── VeDirectArduino.h               — public API and VeDirectRegisters namespace (single include)
│   ├── VeDirectArduino.cpp             — public class implementation
│   ├── VeDirectData.h                  — VeDirectData struct (included by VeDirectArduino.h)
│   ├── VeDirectRegisters.h             — register address constants namespace (included by VeDirectArduino.h)
│   ├── VeDirectTextParser.h            — Text protocol frame parser (internal)
│   ├── VeDirectTextParser.cpp
│   ├── VeDirectHexProtocol.h           — HEX frame builder, response parser, async queue (internal)
│   └── VeDirectHexProtocol.cpp
└── examples/
    ├── BasicTelemetry/
    │   └── BasicTelemetry.ino          — read Text frames, print all fields to Serial
    ├── LoadControl/
    │   └── LoadControl.ino             — toggle load output on/off via HEX convenience method
    ├── RegisterAccess/
    │   └── RegisterAccess.ino          — generic hexGet/hexSet using VeDirectRegisters constants
    └── AsyncNotifications/
        └── AsyncNotifications.ino      — register async callback; log charge state transitions
```

### 6.2 Parser State Machine — Text Protocol

The Text parser runs as a byte-level state machine inside `loop()`. It does not block — it processes whatever bytes are available in the serial buffer and returns immediately.

```
States:
  IDLE          — waiting for start of label
  READING_LABEL — accumulating label characters until \t
  READING_VALUE — accumulating value characters until \r
  READING_CHECKSUM — reading the single checksum byte after "Checksum\t"
  FRAME_COMPLETE — full frame received; validate checksum; update data struct; reset
```

No dynamic allocation. Fixed-size character buffers sized to the longest expected label (16 bytes) and value (32 bytes). If a label or value overflows the buffer, the frame is discarded and the state machine resets.

### 6.3 HEX Frame Builder

HEX frames are built into a fixed stack buffer, transmitted synchronously, then the parser listens for a Response frame. During the wait, incoming Text protocol bytes are also processed to avoid buffer overflow — the state machine handles interleaved frames correctly.

```
hexSet(reg, value) sequence:
  1. Build Set frame in stack buffer
  2. Calculate and append checksum
  3. Transmit frame via Serial.write()
  4. Enter blocking wait loop (up to timeoutMs):
       a. Call loop() to process incoming bytes
       b. If HEX Response received for this register: return true
       c. If HEX Error received: return false
       d. If timeout: retry once, then return false
```

### 6.4 Interleaved Frame Handling

HEX frames may arrive between or within Text protocol lines. The byte-level state machine detects `:` at any point and switches to HEX frame accumulation mode, then returns to Text parsing after the `\n` terminator. The current Text frame accumulation state is preserved across the interruption.

---

## 7. PlatformIO Integration

### 7.1 library.properties

```ini
name=VeDirect_Arduino
version=0.1.0
author=Christopher E. Lee <clee@unitedconsulting.io>
maintainer=Christopher E. Lee <clee@unitedconsulting.io>
sentence=Arduino library for the Victron Energy VE.Direct serial protocol — Text and HEX.
paragraph=Implements VE.Direct Text protocol telemetry parsing and the full VE.Direct HEX protocol for register read/write and async notifications. Includes named register constants for all standard MPPT registers. Compatible with any Arduino platform with HardwareSerial. Verified on STM32L433; expected-compatible with all Victron MPPT charge controllers.
category=Communication
url=https://github.com/unitedconsulting/VeDirect_Arduino
architectures=*
```

### 7.2 Consuming in UnitedOSM platformio.ini

```ini
[env:swan_r5]
platform  = ststm32
board     = blues_swan_r5
framework = arduino
lib_deps  =
    blues/Blues Wireless Notecard
    sensirion/Sensirion I2C SEN6x
    unitedconsulting/VeDirect_Arduino @ ^0.1.0
```

Until the library is published to the PlatformIO registry, reference directly via GitHub:

```ini
lib_deps =
    https://github.com/unitedconsulting/VeDirect_Arduino.git
```

---

## 8. Testing

### 8.1 Unit Tests — Text Parser

Test `VeDirectTextParser` in isolation using synthetic frame data. No hardware required.

| Test | Input | Expected |
|---|---|---|
| Valid frame | Known-good byte sequence | All fields parsed correctly |
| Checksum fail | One byte modified | Frame discarded; previous data retained |
| Partial frame | Incomplete frame followed by complete frame | No stale data; clean parse of second frame |
| Buffer overflow | Label or value exceeding buffer size | Frame discarded; state machine resets |
| Multi-frame sequence | N consecutive frames | State resets cleanly between each frame |
| All CS values | Synthetic frames with each CS code | Correct `chargeState` integer in struct |
| Yield conversion | `H20` field with known 0.01kWh value | Correct Wh conversion in struct |

### 8.2 Unit Tests — HEX Protocol

Test `VeDirectHexProtocol` in isolation using injected byte sequences.

| Test | Action | Expected |
|---|---|---|
| Set frame — load off | Build Set for `0xEDA8`, value `0x02` | Byte sequence matches VE.Direct HEX spec |
| Set frame — any register | Build Set for each register in `VeDirectRegisters` | Valid frame format; correct checksum |
| Get frame | Build Get for `PRODUCT_ID` | Correct command nibble, register, no value field |
| Checksum correctness | Any frame | Sum of all bytes including checksum = `0x55` |
| Response parsing | Inject valid Response frame | `hexGet` returns true; correct value written to output |
| Error response | Inject Error frame | `hexGet`/`hexSet` returns false |
| Timeout | No response injected | Single retry; returns false after `timeoutMs × 2` |
| Async frame | Inject type-4 async frame during `loop()` | Callback fires with correct register and value |
| Async queue | Inject 5 async frames with queue depth 4 | Oldest discarded; 4 newest retrievable via `popAsync()` |
| Interleaved frames | Text bytes + HEX bytes in arbitrary order | Both parsed correctly; no corruption |
| Ping / Ping response | Send Ping; inject Ping Response | `hexPing()` returns true |

### 8.3 Loopback Integration Test — STM32

Wire `TX` to `RX` on the Notecarrier CX header. Transmit a known Text frame out `TX` and verify reception and parsing on `RX`. Confirms the Arduino Serial instance assignment and baud rate are correct before connecting live hardware.

Separate loopback test for HEX: transmit a Get frame and inject a synthetic Response on the loopback — verifies the HEX frame builder and response parser are wired correctly on the target hardware.

### 8.4 Hardware Bench Test — MPPT 75/15

Full integration against a real Victron MPPT 75/15 with a bench power supply simulating panel input and a resistive load on the load output.

| Step | Action | Pass criterion |
|---|---|---|
| 1 | Power up | Text frames received within 3 seconds |
| 2 | Read all Text fields | Values consistent with bench conditions |
| 3 | `hexPing()` | Returns true within 500ms |
| 4 | `getProductId()` | Returns non-zero; matches known 75/15 PID |
| 5 | `getFirmwareVersion()` | Returns non-zero |
| 6 | `getAbsorptionVoltage()` | Returns a plausible voltage (e.g., 14,400 mV for 12V system) |
| 7 | `setLoadOutput(LOAD_OFF)` | Returns true; load disconnects; `loadOn` false in next Text frame |
| 8 | `setLoadOutput(LOAD_ON)` | Returns true; load reconnects |
| 9 | `setLoadOutput(LOAD_AUTO)` | Returns true; MPPT resumes load control |
| 10 | `hexGet(YIELD_TODAY)` | Returns a value; no error or timeout |
| 11 | Async notification | Induce charge state transition; verify callback fires with correct register and value |
| 12 | Checksum failure (loopback) | Corrupt one byte; verify frame rejected and `lastFrameValid()` false |

---

## 9. UnitedOSM Integration Notes

The `VeDirectDriver` class in UnitedOSM wraps this library and implements `ISensorDriver`. See UnitedOSM Software Spec §3.8 for the wrapper implementation.

**SOC derivation:** The library does not provide battery SOC. `VeDirectDriver` derives an estimated SOC from `battV_mV` using a 3S LiIon voltage curve:

```cpp
// 3S LiIon voltage-to-SOC approximation
// Full charge: 12.6V = 100%; Cutoff: 9.0V = 0%
float voltageToSOC(int32_t battV_mV) {
    float v = battV_mV / 1000.0f;
    if (v >= 12.6f) return 100.0f;
    if (v <= 9.0f)  return 0.0f;
    return ((v - 9.0f) / (12.6f - 9.0f)) * 100.0f;
}
```

This is a linear approximation. LiIon discharge curves are nonlinear — a proper lookup table may be substituted at a later firmware revision. The linear approximation is acceptable for dashboard SOC display at this stage.

**Charge mode mapping:** `VeDirectDriver.buildNote()` maps the `chargeState` integer to the string values used in the `vedirect.qo` note body:

```cpp
const char* chargeStateToString(int cs) {
    switch (cs) {
        case 0:  return "Off";
        case 2:  return "Fault";
        case 3:  return "Bulk";
        case 4:  return "Absorption";
        case 5:  return "Float";
        case 7:  return "Equalize";
        default: return "Unknown";
    }
}
```

---

## 10. Open Items

| # | Item | Status |
|---|---|---|
| 1 | Verify all register addresses in `VeDirectRegisters` namespace against Victron VE.Direct HEX protocol document — particularly charger settings, load output voltage/current, and history registers | Before HEX implementation |
| 2 | Confirm Serial1 maps to Notecarrier CX RX/TX header pins on Blues Swan — see UnitedOSM Hardware Spec §10.4 | Before bench test |
| 3 | Determine async notification queue depth — default 4 entries; verify this is sufficient for typical controller notification burst | At bench test |
| 4 | Confirm MPPT 75/15 firmware version constraints on HEX protocol support — some early firmware versions have limited HEX register coverage | At bench test |
| 5 | String register read handling for SERIAL_NUMBER (`0x010A`) — HEX string reads require different value-length handling than numeric registers; design and test separately | Before v1.0 release |
| 6 | Publish to Arduino Library Registry and PlatformIO Registry | At first stable release |
| 7 | Validate expected-compatible controller list (75/10, 100/15, 100/20, SmartSolar series) — register map and frame format expected identical; confirm at bench test with available hardware | Community / future testing |

---

*End of VeDirect_Arduino Library Specification — v0.1 Draft*
