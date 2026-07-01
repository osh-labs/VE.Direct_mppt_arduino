// VeDirectData.h
//
// Plain-old-data struct holding the most recently parsed VE.Direct Text frame.
// Included by VeDirectArduino.h. Contains no Arduino dependencies so it can be
// used in host-side unit tests.
//
// Part of the VeDirect_Arduino library. MIT licensed.

#ifndef VEDIRECT_DATA_H
#define VEDIRECT_DATA_H

#include <stdint.h>

// Snapshot of the fields parsed from a single VE.Direct Text frame.
//
// All numeric fields are stored in the base SI-ish units documented below and
// are left at their zero/default values until the first valid frame arrives.
// Fields not present in a given controller's output remain at zero.
struct VeDirectData {
    // --- Battery ---
    int32_t battV_mV;         // Battery terminal voltage (mV). Text label "V".
    int32_t battI_mA;         // Battery current (mA); positive = charge,
                              // negative = discharge. Text label "I".

    // --- Panel ---
    int32_t panelV_mV;        // Panel (PV) voltage (mV). Text label "VPV".
    int32_t panelW;           // Panel (PV) power (W). Text label "PPV".

    // --- Load ---
    int32_t loadI_mA;         // Load output current (mA). Text label "IL".
    bool    loadOn;           // Load output state. Text label "LOAD" (ON/OFF).

    // --- Status ---
    int     chargeState;      // CS field value — use VeDirectArduino::ChargeState.
    int     mpptMode;         // MPPT tracker mode (0=off, 1=limited, 2=active).
    int     errorCode;        // ERR field value — see spec §3.5.

    // --- Yield ---
    int32_t yieldTodayWh;     // Yield today (Wh; converted from the 0.01 kWh
                              // "H20" Text field).
    int32_t yieldYesterdayWh; // Yield yesterday (Wh; from "H22").

    // --- Metadata ---
    int     daySequence;      // "Hsds" day sequence number.
    bool    frameValid;       // true if the last completed frame passed checksum.

    VeDirectData()
        : battV_mV(0), battI_mA(0),
          panelV_mV(0), panelW(0),
          loadI_mA(0), loadOn(false),
          chargeState(0), mpptMode(0), errorCode(0),
          yieldTodayWh(0), yieldYesterdayWh(0),
          daySequence(0), frameValid(false) {}
};

#endif // VEDIRECT_DATA_H
