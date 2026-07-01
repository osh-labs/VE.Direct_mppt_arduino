/*
 * LoadControl — toggle the MPPT load output on/off via the HEX convenience API.
 *
 * Demonstrates setLoadOutput() with the LoadMode enum. The load output control
 * register is 0xEDAB; note that the enum values are the controller's real
 * encoding (OFF=0, AUTO=1, ON=4). Load output exists only on 10A/15A/20A models
 * (e.g. MPPT 75/15).
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

    // Confirm the controller answers the HEX protocol before commanding it.
    if (vedirect.hexPing(500)) {
        Serial.println(F("HEX ping OK"));
    } else {
        Serial.println(F("HEX ping failed — controller may not support HEX"));
    }
}

void loop() {
    // Keep the Text parser fed so telemetry (incl. load state) stays current.
    vedirect.loop();

    // Every 10 seconds, cycle OFF -> ON -> AUTO.
    static unsigned long last = 0;
    static uint8_t step = 0;
    if (millis() - last >= 10000) {
        last = millis();

        bool ok = false;
        switch (step) {
            case 0:
                Serial.println(F("Forcing load OFF"));
                ok = vedirect.setLoadOutput(VeDirectArduino::LOAD_OFF);
                break;
            case 1:
                Serial.println(F("Forcing load ON"));
                ok = vedirect.setLoadOutput(VeDirectArduino::LOAD_ON);
                break;
            case 2:
                Serial.println(F("Returning load to AUTO"));
                ok = vedirect.setLoadOutput(VeDirectArduino::LOAD_AUTO);
                break;
        }
        Serial.println(ok ? F("  command acknowledged") : F("  command FAILED/timeout"));
        step = (step + 1) % 3;
    }

    // Report the load state as reported by Text telemetry.
    static unsigned long lastReport = 0;
    if (millis() - lastReport >= 2000) {
        lastReport = millis();
        Serial.print(F("Text LOAD state: "));
        Serial.println(vedirect.data().loadOn ? F("ON") : F("OFF"));
    }
}
