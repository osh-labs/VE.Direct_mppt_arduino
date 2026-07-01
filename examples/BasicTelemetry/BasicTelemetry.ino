/*
 * BasicTelemetry — read VE.Direct Text frames and print all fields.
 *
 * Wiring (Victron VE.Direct 4-pin JST-PH -> 3.3V MCU, no level shifting):
 *   VE.Direct pin 1 (GND)  -> MCU GND
 *   VE.Direct pin 2 (TX)   -> MCU RX  (Serial1 RX)
 *   VE.Direct pin 3 (RX)   -> MCU TX  (Serial1 TX)
 *   VE.Direct pin 4 (+5V)  -> leave disconnected
 *
 * Adjust the UART instance (Serial1 here) to match your board's VE.Direct port.
 *
 * Part of the VeDirect_Arduino library. MIT licensed.
 */

#include <VeDirectArduino.h>

VeDirectArduino vedirect;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) { /* wait for USB serial, optionally */ }

    if (!vedirect.begin(Serial1, 19200, 3000)) {
        Serial.println("VE.Direct: no controller detected (will keep trying)");
    } else {
        Serial.println("VE.Direct: controller detected");
    }
}

void loop() {
    // Process serial bytes every iteration; a full valid frame arrives ~1/sec.
    if (vedirect.loop()) {
        const VeDirectData& d = vedirect.data();

        Serial.println(F("---- VE.Direct frame ----"));
        Serial.print(F("Battery : ")); Serial.print(d.battV_mV / 1000.0f, 3);
        Serial.print(F(" V  "));       Serial.print(d.battI_mA / 1000.0f, 3);
        Serial.println(F(" A"));

        Serial.print(F("Panel   : ")); Serial.print(d.panelV_mV / 1000.0f, 2);
        Serial.print(F(" V  "));       Serial.print(d.panelW);
        Serial.println(F(" W"));

        Serial.print(F("Load    : ")); Serial.print(d.loadOn ? F("ON ") : F("OFF"));
        Serial.print(F("  "));         Serial.print(d.loadI_mA / 1000.0f, 3);
        Serial.println(F(" A"));

        Serial.print(F("State   : CS=")); Serial.print(d.chargeState);
        Serial.print(F("  MPPT="));       Serial.print(d.mpptMode);
        Serial.print(F("  ERR="));        Serial.println(d.errorCode);

        Serial.print(F("Yield   : today ")); Serial.print(d.yieldTodayWh);
        Serial.print(F(" Wh  yesterday ")); Serial.print(d.yieldYesterdayWh);
        Serial.println(F(" Wh"));
    }

    // Warn if telemetry has gone stale (no valid frame for >5 s).
    static unsigned long lastWarn = 0;
    if (vedirect.msSinceLastFrame() > 5000 && millis() - lastWarn > 5000) {
        lastWarn = millis();
        Serial.println(F("VE.Direct: no valid frame in >5s"));
    }
}
