#pragma once

#include <stdint.h>

#include "pico/types.h"

// Pico GPIO assignments, taken from the PicoTide schematic.
namespace Pin {
constexpr uint AdrData0 = 0;     // GPIO0-7: A0-A7 via U1, or D0-D7 via U3
constexpr uint IoChrdy = 20;     // high pulls ISA IO CH RDY low (U8, open drain)
constexpr uint Reset = 21;       // ISA RESET DRV, via U6
constexpr uint AdrDataSel = 22;  // high: U1 (address) owns GPIO0-7, low: U3 (data)
constexpr uint DataDir = 26;     // U3 DIR. high: Pico -> ISA, low: ISA -> Pico
constexpr uint ValidWrite = 27;  // NOR(SMEMW#, AEN), via U4
constexpr uint ValidRead = 28;   // NOR(SMEMR#, AEN), via U7
}  // namespace Pin

constexpr uint32_t DataMask = 0xFFu << Pin::AdrData0;

// What GPIO0-7 are connected to.
enum class BusMode {
    Address,  // U1 drives GPIO0-7 with ISA A0-A7
    DataIn,   // U3 drives GPIO0-7 with ISA D0-D7
    DataOut,  // Pico drives GPIO0-7, U3 drives ISA D0-D7
};

// One line the Pico can read, with where to find it on the card.
struct Signal {
    const char* name;     // ISA signal name
    const char* edgePin;  // gold finger on the ISA edge connector
    uint gpio;
};

// Lines readable in the given mode, in display order.
struct SignalList {
    const Signal* items;
    int count;
    uint32_t mask;  // GPIO bits of all items

    const Signal* find(uint gpio) const;
};

SignalList signalsFor(BusMode mode);

void cardInit();
void setMode(BusMode mode);
BusMode currentMode();

// Drives D0-D7. Only valid in BusMode::DataOut.
void driveData(uint8_t value);

void setIoChrdyAsserted(bool asserted);

// Heuristic check that the translators have 5 V on VCCB. Without it their
// Pico-side outputs float and the pins follow the Pico's own pulls.
struct PullReadings {
    uint32_t mask;  // GPIOs tested
    uint32_t low;   // read with the pull-down on
    uint32_t high;  // read with the pull-up on

    // GPIOs that followed the pulls (0 means the card looks powered).
    uint32_t followed() const { return ~low & high & mask; }
};

PullReadings readUnderPulls();
