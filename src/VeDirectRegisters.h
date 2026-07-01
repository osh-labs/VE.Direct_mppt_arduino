// VeDirectRegisters.h
//
// Centralized VE.Direct HEX register address constants for Victron MPPT
// charge controllers. Consumers should always reference these by name rather
// than using raw hex literals.
//
// All addresses below were verified against the official Victron document
// "VE.Direct Protocol — BlueSolar and SmartSolar MPPT chargers", Rev 18
// (https://www.victronenergy.com/upload/documents/BlueSolar-HEX-protocol.pdf).
//
// IMPORTANT — several addresses differ from the original library spec draft.
// The spec draft was marked "verify against Victron docs" and a number of its
// addresses were incorrect. The corrected, document-verified values are used
// here. Where an address changed, the spec's original (incorrect) value is
// noted in a comment. The single most important correction: the load-output
// *control* register is 0xEDAB (0xEDA8 is the read-only on/off *state*).
//
// Part of the VeDirect_Arduino library. MIT licensed.

#ifndef VEDIRECT_REGISTERS_H
#define VEDIRECT_REGISTERS_H

#include <stdint.h>

namespace VeDirectRegisters {

    // --- Product information (read-only) ---
    // Note: the product id and firmware version are most reliably obtained via
    // the dedicated HEX commands (VeDirectArduino::getProductId() /
    // getFirmwareVersion()), which Victron recommends over a register read.
    constexpr uint16_t PRODUCT_ID           = 0x0100;  // un32 (un16 on old fw)
    constexpr uint16_t GROUP_ID             = 0x0104;  // un8
    constexpr uint16_t SERIAL_NUMBER        = 0x010A;  // string (special read)
    constexpr uint16_t MODEL_NAME           = 0x010B;  // string
    constexpr uint16_t CAPABILITIES         = 0x0140;  // un32 bit-mask

    // --- Device state (read) ---
    constexpr uint16_t DEVICE_MODE          = 0x0200;  // un8 (0/4=off, 1=on)
    constexpr uint16_t DEVICE_STATE         = 0x0201;  // un8 enum — HEX equivalent
                                                       // of the Text "CS" field
    constexpr uint16_t CHARGE_STATE         = DEVICE_STATE; // spec alias
                                                       // (spec had 0xEDA1 — wrong)

    // --- Battery settings (read/write) ---
    // WARNING: these are stored in non-volatile memory. Do NOT write them
    // continuously from a control loop — that will wear out the flash.
    // To change absorption/float/equalisation the battery type (0xEDF1) must be
    // set to user-defined (0xFF), otherwise the controller rejects the write
    // with error 119.
    constexpr uint16_t ABSORPTION_VOLTAGE   = 0xEDF7;  // un16, 0.01 V (spec: 0xED8D)
    constexpr uint16_t FLOAT_VOLTAGE        = 0xEDF6;  // un16, 0.01 V (spec: 0xED8E)
    constexpr uint16_t EQUALISATION_VOLTAGE = 0xEDF4;  // un16, 0.01 V (spec: 0xED8F)
    constexpr uint16_t MAX_ABSORPTION_TIME  = 0xEDFB;  // un16, 0.01 hours
                                                       // (spec: 0xED90, "min")
    constexpr uint16_t MAX_CHARGE_CURRENT   = 0xEDF0;  // un16, 0.1 A — "Battery
                                                       // maximum current" (settable)
                                                       // (spec: 0xEDA0)
    constexpr uint16_t BATTERY_TYPE         = 0xEDF1;  // un8 (0xFF = user defined)

    // --- Load output (read/write) — only on 10A/15A/20A models ---
    constexpr uint16_t LOAD_OUTPUT_CONTROL  = 0xEDAB;  // un8 enum — switching mode
                                                       // (spec: 0xEDA8 — WRONG)
    constexpr uint16_t LOAD_OUTPUT_STATE    = 0xEDA8;  // un8, 0=off 1=on (read-only)
    constexpr uint16_t LOAD_OUTPUT_VOLTAGE  = 0xEDA9;  // un16, 0.01 V
    constexpr uint16_t LOAD_OUTPUT_CURRENT  = 0xEDAD;  // un16, 0.1 A (spec: 0xEDAB)

    // --- Charger real-time data (read-only) ---
    // Also delivered every second via the Text protocol — prefer Text values
    // for telemetry and use these only for on-demand single reads.
    constexpr uint16_t BATTERY_VOLTAGE      = 0xEDD5;  // un16, 0.01 V — "Charger
                                                       // voltage" (spec: 0xED8B)
    constexpr uint16_t BATTERY_CURRENT      = 0xEDD7;  // un16, 0.1 A — "Charger
                                                       // current" = battery + load;
                                                       // subtract LOAD_OUTPUT_CURRENT
                                                       // for true battery current
                                                       // (spec: 0xED8C)
    constexpr uint16_t PANEL_VOLTAGE        = 0xEDBB;  // un16, 0.01 V (confirmed)
    constexpr uint16_t PANEL_POWER          = 0xEDBC;  // un32, 0.01 W (spec: 0xEDB9)
    constexpr uint16_t ERROR_CODE           = 0xEDDA;  // un8 enum (spec: 0xEDB0)

    // --- History (read-only) ---
    constexpr uint16_t YIELD_TOTAL          = 0xEDDD;  // un32, 0.01 kWh — system
                                                       // yield (spec: 0xEDB1)
    constexpr uint16_t YIELD_USER           = 0xEDDC;  // un32, 0.01 kWh (resettable)
    constexpr uint16_t YIELD_TODAY          = 0xEDD3;  // un16, 0.01 kWh (spec: 0xEDB2)
    constexpr uint16_t YIELD_YESTERDAY      = 0xEDD1;  // un16, 0.01 kWh (spec: 0xEDB4)
    constexpr uint16_t MAX_POWER_TODAY      = 0xEDD2;  // un16, 1 W (spec: 0xEDB7)
    constexpr uint16_t MAX_POWER_YESTERDAY  = 0xEDD0;  // un16, 1 W (spec: 0xEDB8)

} // namespace VeDirectRegisters

#endif // VEDIRECT_REGISTERS_H
