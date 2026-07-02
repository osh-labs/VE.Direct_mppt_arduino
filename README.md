# VeDirect_Arduino

A portable Arduino library for the **Victron Energy VE.Direct** serial protocol —
both the **Text** telemetry protocol and the **HEX** register protocol
(read/write + async notifications) — for MPPT solar charge controllers.

- **Text protocol:** one-way telemetry parsed from the controller's 1 Hz frames
  (battery/panel voltage & current, load state, charge state, yield, errors).
- **HEX protocol:** bidirectional register read/write, load-output control,
  connectivity ping, and unsolicited async notifications.
- **Portable:** any Arduino target with a `HardwareSerial`. No RTOS, no heap, no
  dynamic allocation in the hot path. Primary target: **STM32L4 (Blues Swan)**;
  verified to compile for STM32 and ESP32; expected-compatible with the whole
  Victron MPPT line.

> Status: `v0.1.0` draft. The register map has been verified against Victron's
> official *VE.Direct Protocol* document (Rev 18), but on-hardware validation
> against a real MPPT 75/15 is still pending (see [Testing](#testing)).

## Contents

- [Supported hardware](#supported-hardware)
- [Wiring](#wiring)
- [Installation](#installation)
- [Quick start](#quick-start)
- [API overview](#api-overview)
- [Register map](#register-map)
- [Protocol notes](#protocol-notes--corrections)
- [Examples](#examples)
- [Testing](#testing)
- [License](#license)

## Supported hardware

| Controller | Status |
|---|---|
| Victron BlueSolar / SmartSolar MPPT **75/15** | Primary target (bench test pending) |
| MPPT 75/10, 100/15, 100/20 (models **with a load output**) | Expected-compatible |
| Larger MPPTs **without** a load output | Text + HEX work; `setLoadOutput()` is a no-op / unsupported (see [Load control](#load-output-control)) |

### VE.Direct electrical interface

| Parameter | Value |
|---|---|
| Baud rate | 19200 |
| Format | 8N1 |
| Logic levels | 3.3 V TTL (no level shifting to a 3.3 V MCU) |
| Connector | Victron VE.Direct 4-pin JST-PH |

## Wiring

Only pins 1–3 are used. **Do not** connect pin 4 (+5 V) to MCU logic.

| VE.Direct pin | Signal | Connect to |
|---|---|---|
| 1 | GND | MCU GND |
| 2 | TX (controller → MCU) | MCU UART **RX** |
| 3 | RX (MCU → controller) | MCU UART **TX** |
| 4 | +5 V | leave disconnected |

On the Blues Swan the VE.Direct port is `Serial1`. Adjust the `HardwareSerial`
instance in your sketch to match your board.

**Adafruit Feather ESP32 V2:** the labeled `RX`/`TX` header pins are GPIO7/GPIO8,
mapped to `Serial1` by the board's pin definitions, and are physically separate
from the USB debug UART (`Serial`). `Serial1.begin(19200)` uses these pins by
default on this board, so `vedirect.begin(Serial1, ...)` works with no extra pin
configuration:

| VE.Direct pin | Signal | Feather V2 pin |
|---|---|---|
| 1 | GND | GND |
| 2 | TX | `RX` (GPIO7) |
| 3 | RX | `TX` (GPIO8) |
| 4 | +5 V | leave disconnected |

## Installation

### PlatformIO

```ini
lib_deps =
    https://github.com/osh-labs/VE.Direct_mppt_arduino.git
```

Pinned to a released version (recommended once a tag exists):

```ini
lib_deps =
    https://github.com/osh-labs/VE.Direct_mppt_arduino.git#v0.1.0
```

Once published to the PlatformIO Registry:

```ini
lib_deps =
    VeDirect_Arduino @ ^0.1.0
```

### Arduino IDE

Download the repository as a ZIP and use *Sketch → Include Library → Add .ZIP
Library…*, or clone it into your `libraries/` folder.

## Quick start

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
    if (vedirect.loop()) {                 // true once per valid Text frame
        const VeDirectData& d = vedirect.data();
        Serial.print("Batt "); Serial.print(d.battV_mV / 1000.0f, 3);
        Serial.print(" V  Panel "); Serial.print(d.panelW);
        Serial.print(" W  CS "); Serial.println(d.chargeState);
    }
}
```

Force the load output off, then back to automatic:

```cpp
vedirect.setLoadOutput(VeDirectArduino::LOAD_OFF);
vedirect.setLoadOutput(VeDirectArduino::LOAD_AUTO);
```

## API overview

Single include: `#include <VeDirectArduino.h>`.

### Lifecycle
- `bool begin(HardwareSerial& serial, unsigned long baud = 19200, unsigned long timeoutMs = 3000)`
  — starts the port and probes for a valid Text frame; returns `true` if the
  controller is present.
- `bool loop()` — call every iteration; non-blocking; returns `true` when a
  complete, checksum-valid Text frame was parsed.

### Text telemetry
- `const VeDirectData& data()` — latest parsed values (see below).
- `bool lastFrameValid()` — did the last completed frame pass checksum?
- `unsigned long msSinceLastFrame()` — staleness watchdog (`ULONG_MAX` if none).

### HEX — generic
- `bool hexGet(uint16_t reg, uint32_t* valueOut, unsigned long timeoutMs = 500)`
- `bool hexSet(uint16_t reg, uint32_t value, uint8_t valueLen = 1, unsigned long timeoutMs = 500)`
- `bool hexPing(unsigned long timeoutMs = 500)`

Blocking calls pump the receiver while waiting and retry once on timeout.

### HEX — async notifications
- `void onAsync(VeDirectAsyncCallback cb, void* ctx = nullptr)` — callback model.
- `bool asyncPending()` / `bool popAsync(uint16_t* reg, uint32_t* value)` — poll
  model (fixed queue, depth 4 by default, overridable via
  `VEDIRECT_ASYNC_QUEUE_DEPTH`). When a callback is registered, notifications are
  delivered to it and **not** queued.

### HEX — convenience
- `getProductId()`, `getFirmwareVersion()`
- `setLoadOutput(LoadMode)`, `getLoadOutput(LoadMode*)`
- `setChargeCurrentLimit(mA)` / `getChargeCurrentLimit(mA*)`
- `setAbsorptionVoltage(mV)` / `getAbsorptionVoltage(mV*)`
- `setFloatVoltage(mV)` / `getFloatVoltage(mV*)`
- `setEqualisationVoltage(mV)` / `getEqualisationVoltage(mV*)`

Voltages are exchanged in **mV**, currents in **mA**; the library converts
to/from the controller's `0.01 V` / `0.1 A` register units.

### `VeDirectData` fields

| Field | Unit | Text label |
|---|---|---|
| `battV_mV` | mV | `V` |
| `battI_mA` | mA (+charge / −discharge) | `I` |
| `panelV_mV` | mV | `VPV` |
| `panelW` | W | `PPV` |
| `loadI_mA` | mA | `IL` |
| `loadOn` | bool | `LOAD` |
| `chargeState` | enum (`ChargeState`) | `CS` |
| `mpptMode` | 0/1/2 | `MPPT` |
| `errorCode` | enum | `ERR` |
| `yieldTodayWh` | Wh | `H20` |
| `yieldYesterdayWh` | Wh | `H22` |
| `daySequence` | — | `HSDS` |

> **State of charge (SOC)** is not provided — the MPPT does not report it over
> VE.Direct Text. Derive it from `battV_mV` for your battery chemistry, or use a
> connected BMV battery monitor.

## Register map

All addresses live in the `VeDirectRegisters` namespace — always use the names,
never raw hex. Every address was verified against Victron *VE.Direct Protocol*
Rev 18.

| Name | Address | Type / scale |
|---|---|---|
| `PRODUCT_ID` | `0x0100` | un32 (prefer `getProductId()`) |
| `SERIAL_NUMBER` | `0x010A` | string |
| `DEVICE_STATE` / `CHARGE_STATE` | `0x0201` | un8 enum |
| `ABSORPTION_VOLTAGE` | `0xEDF7` | un16, 0.01 V |
| `FLOAT_VOLTAGE` | `0xEDF6` | un16, 0.01 V |
| `EQUALISATION_VOLTAGE` | `0xEDF4` | un16, 0.01 V |
| `MAX_ABSORPTION_TIME` | `0xEDFB` | un16, 0.01 h |
| `MAX_CHARGE_CURRENT` | `0xEDF0` | un16, 0.1 A |
| `LOAD_OUTPUT_CONTROL` | `0xEDAB` | un8 enum |
| `LOAD_OUTPUT_STATE` | `0xEDA8` | un8 (0/1, read-only) |
| `LOAD_OUTPUT_CURRENT` | `0xEDAD` | un16, 0.1 A |
| `BATTERY_VOLTAGE` | `0xEDD5` | un16, 0.01 V |
| `BATTERY_CURRENT` | `0xEDD7` | un16, 0.1 A (charger output = battery + load) |
| `PANEL_VOLTAGE` | `0xEDBB` | un16, 0.01 V |
| `PANEL_POWER` | `0xEDBC` | un32, 0.01 W |
| `ERROR_CODE` | `0xEDDA` | un8 enum |
| `YIELD_TOTAL` | `0xEDDD` | un32, 0.01 kWh |
| `YIELD_TODAY` | `0xEDD3` | un16, 0.01 kWh |
| `YIELD_YESTERDAY` | `0xEDD1` | un16, 0.01 kWh |
| `MAX_POWER_TODAY` | `0xEDD2` | un16, 1 W |
| `MAX_POWER_YESTERDAY` | `0xEDD0` | un16, 1 W |

> **WARNING — non-volatile settings:** the charger *setting* registers
> (absorption/float/equalisation/current) are stored in flash. Do **not** write
> them from a control loop; repeated writes wear out the memory. Voltage writes
> also require the battery type (`0xEDF1`) to be set to user-defined (`0xFF`),
> otherwise the controller rejects them with error 119.

## Protocol notes / corrections

This library corrects several details that were wrong in the original library
spec draft. If you are cross-referencing that draft, note:

### Load output control
The control register is **`0xEDAB`**, *not* `0xEDA8` (which is the read-only
on/off *state*). The `LoadMode` enum therefore uses the controller's real
encoding:

| `LoadMode` | Value | Meaning |
|---|---|---|
| `LOAD_OFF` | 0 | forced off |
| `LOAD_AUTO` | 1 | automatic / BatteryLife (default) |
| `LOAD_ON` | 4 | forced on (no low-voltage disconnect guard) |
| `LOAD_ALT1/ALT2/USER1/USER2/AES` | 2/3/5/6/7 | additional modes |

Load output exists **only on 10 A/15 A/20 A models**. On larger models without a
load output there is no `0xEDAB`; some offer a "virtual load output" driven from
the VE.Direct TX pin instead. Check the capabilities register (`0x0140`, bit 0)
before assuming load control works.

### HEX wire format
- Command nibbles: **Get = 7, Set = 8, Async = 0xA, Ping = 1** (ping reply = 5).
- Frame: `:<cmd><payload…><checksum>\n`, ASCII hex, multi-byte values
  little-endian.
- Checksum: `(cmd + Σ payload bytes + checksum) & 0xFF == 0x55`.
- Text and HEX frames share one UART; the parser demultiplexes them
  byte-by-byte, so HEX responses/async frames interleaved with Text telemetry
  are handled without corruption.

## Examples

| Example | Shows |
|---|---|
| `BasicTelemetry` | Parse and print every Text field |
| `LoadControl` | Toggle the load output via `setLoadOutput()` |
| `RegisterAccess` | Generic `hexGet`/`hexSet` with named registers |
| `AsyncNotifications` | Register an async callback; log charge-state changes |

## Testing

Host-side unit tests (no hardware) cover the Text parser and the HEX
builder/decoder, async queue, timeout/retry, and interleaved frames:

```sh
pio test -e native
```

Examples are compile-checked in CI for both the STM32 (`blues_swan_r5`) and
ESP32 (`esp32dev`) targets — see `.github/workflows/ci.yml`.

The bench-test plan against a real MPPT 75/15 (register values, load switching,
async transitions) is documented in `VeDirect_Arduino_Spec.md` §8.4.

### Flashing an example to real hardware

Each example has a standalone companion PlatformIO project under `bench/` that
depends on this library via a relative symlink, so edits to `src/` take effect
immediately without reinstalling anything:

| Example | Companion project |
|---|---|
| `BasicTelemetry` | `bench/feather_v2_basic` |
| `LoadControl` | `bench/feather_v2_loadcontrol` |
| `RegisterAccess` | `bench/feather_v2_registers` |
| `AsyncNotifications` | `bench/feather_v2_async` |

```sh
cd bench/feather_v2_basic
pio run -t upload -t monitor
```

These target `adafruit_feather_esp32_v2`. To bench-test a different board,
copy one of these folders and change the `board =` line in its
`platformio.ini` (find the exact ID with `pio boards | grep -i <name>`).

## License

MIT — see [LICENSE](LICENSE). No attribution required for end products.
