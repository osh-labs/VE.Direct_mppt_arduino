/*
 * AsyncNotifications — register an async callback and log register changes.
 *
 * The controller emits unsolicited HEX "async" frames (command type 0xA) when
 * register values change — e.g. a charge-state transition or a load switch.
 * This sketch logs those changes. The library delivers the raw register address
 * and value; the consumer maps them to meaning using VeDirectRegisters.
 *
 * See BasicTelemetry.ino for wiring.
 *
 * Part of the VeDirect_Arduino library. MIT licensed.
 */

#include <VeDirectArduino.h>

VeDirectArduino vedirect;

const char* chargeStateToString(uint32_t cs) {
    switch (cs) {
        case VeDirectArduino::CS_OFF:        return "Off";
        case VeDirectArduino::CS_FAULT:      return "Fault";
        case VeDirectArduino::CS_BULK:       return "Bulk";
        case VeDirectArduino::CS_ABSORPTION: return "Absorption";
        case VeDirectArduino::CS_FLOAT:      return "Float";
        case VeDirectArduino::CS_STORAGE:    return "Storage";
        case VeDirectArduino::CS_EQUALIZE:   return "Equalize";
        default:                             return "Other";
    }
}

// Called from within vedirect.loop() whenever an async frame arrives.
void onVeDirectAsync(uint16_t reg, uint32_t value, void* ctx) {
    (void)ctx;
    switch (reg) {
        case VeDirectRegisters::CHARGE_STATE:  // == DEVICE_STATE (0x0201)
            Serial.print(F("[async] Charge state -> "));
            Serial.print(value);
            Serial.print(F(" ("));
            Serial.print(chargeStateToString(value));
            Serial.println(F(")"));
            break;
        case VeDirectRegisters::LOAD_OUTPUT_CONTROL:
            Serial.print(F("[async] Load control -> "));
            Serial.println(value);
            break;
        default:
            Serial.print(F("[async] reg 0x"));
            Serial.print(reg, HEX);
            Serial.print(F(" = "));
            Serial.println(value);
            break;
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}

    vedirect.begin(Serial1, 19200, 3000);
    vedirect.onAsync(onVeDirectAsync, nullptr);

    Serial.println(F("Listening for async notifications..."));
}

void loop() {
    // Async callbacks fire from inside loop() as frames are parsed.
    vedirect.loop();

    // (Poll model alternative, if no callback were registered:)
    // uint16_t reg; uint32_t val;
    // while (vedirect.popAsync(&reg, &val)) { /* handle */ }
}
