/*
 * RegisterAccess — generic hexGet/hexSet using named VeDirectRegisters constants.
 *
 * Reads product id, firmware version, and several charger settings, then shows
 * a (commented-out) example of writing a setting. Always reference registers by
 * their VeDirectRegisters:: name rather than raw hex.
 *
 * See BasicTelemetry.ino for wiring.
 *
 * Part of the VeDirect_Arduino library. MIT licensed.
 */

#include <VeDirectArduino.h>

VeDirectArduino vedirect;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}

    vedirect.begin(Serial1, 19200, 3000);

    uint16_t pid = 0, fw = 0;
    if (vedirect.getProductId(&pid)) {
        Serial.print(F("Product ID  : 0x")); Serial.println(pid, HEX);
    }
    if (vedirect.getFirmwareVersion(&fw)) {
        // e.g. 0x0116 -> firmware 1.16
        Serial.print(F("Firmware    : ")); Serial.println(fw, HEX);
    }

    // Charger settings, converted to mV / mA by the convenience methods.
    uint16_t mV = 0, mA = 0;
    if (vedirect.getAbsorptionVoltage(&mV)) {
        Serial.print(F("Absorption  : ")); Serial.print(mV / 1000.0f, 3); Serial.println(F(" V"));
    }
    if (vedirect.getFloatVoltage(&mV)) {
        Serial.print(F("Float       : ")); Serial.print(mV / 1000.0f, 3); Serial.println(F(" V"));
    }
    if (vedirect.getChargeCurrentLimit(&mA)) {
        Serial.print(F("Charge limit: ")); Serial.print(mA / 1000.0f, 1); Serial.println(F(" A"));
    }

    // Raw generic read of any register (yield today, 0.01 kWh units):
    uint32_t raw = 0;
    if (vedirect.hexGet(VeDirectRegisters::YIELD_TODAY, &raw)) {
        Serial.print(F("Yield today : ")); Serial.print(raw * 10); Serial.println(F(" Wh"));
    }

    // --- Writing a setting (DANGEROUS: stored in NON-VOLATILE memory) ---
    // These registers wear out with repeated writes — never write from a loop.
    // The battery type must be user-defined (0xFF) for voltage writes to stick.
    //
    // uint16_t absV = 0;
    // if (vedirect.getAbsorptionVoltage(&absV)) {
    //     vedirect.setAbsorptionVoltage(absV + 100);  // nudge up 100 mV, once
    // }
}

void loop() {
    vedirect.loop();  // keep the parser serviced
}
