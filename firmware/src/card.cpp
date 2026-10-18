#include "pico/stdlib.h"

#include "card.h"

namespace {

// Edge pins: A side is the component side. Ax sits on A(31-x), Dn on A(9-n).
// The gate outputs have no edge pin, so the chip that drives them is shown.
const Signal addressSignals[] = {
    {"A0", "A31", 0},
    {"A1", "A30", 1},
    {"A2", "A29", 2},
    {"A3", "A28", 3},
    {"A4", "A27", 4},
    {"A5", "A26", 5},
    {"A6", "A25", 6},
    {"A7", "A24", 7},
    {"A8", "A23", 8},
    {"A9", "A22", 9},
    {"A10", "A21", 10},
    {"A11", "A20", 11},
    {"A12", "A19", 12},
    {"A13", "A18", 13},
    {"A14", "A17", 14},
    {"A15", "A16", 15},
    {"A16", "A15", 16},
    {"A17", "A14", 17},
    {"A18", "A13", 18},
    {"A19", "A12", 19},
    {"RESET DRV", "B2", Pin::Reset},
    {"VALID_READ", "U7", Pin::ValidRead},
    {"VALID_WRITE", "U4", Pin::ValidWrite},
};

const Signal dataInSignals[] = {
    {"D0", "A9", 0},
    {"D1", "A8", 1},
    {"D2", "A7", 2},
    {"D3", "A6", 3},
    {"D4", "A5", 4},
    {"D5", "A4", 5},
    {"D6", "A3", 6},
    {"D7", "A2", 7},
    {"A8", "A23", 8},
    {"A9", "A22", 9},
    {"A10", "A21", 10},
    {"A11", "A20", 11},
    {"A12", "A19", 12},
    {"A13", "A18", 13},
    {"A14", "A17", 14},
    {"A15", "A16", 15},
    {"A16", "A15", 16},
    {"A17", "A14", 17},
    {"A18", "A13", 18},
    {"A19", "A12", 19},
    {"RESET DRV", "B2", Pin::Reset},
    {"VALID_READ", "U7", Pin::ValidRead},
    {"VALID_WRITE", "U4", Pin::ValidWrite},
};

const Signal dataOutSignals[] = {
    {"A8", "A23", 8},
    {"A9", "A22", 9},
    {"A10", "A21", 10},
    {"A11", "A20", 11},
    {"A12", "A19", 12},
    {"A13", "A18", 13},
    {"A14", "A17", 14},
    {"A15", "A16", 15},
    {"A16", "A15", 16},
    {"A17", "A14", 17},
    {"A18", "A13", 18},
    {"A19", "A12", 19},
    {"RESET DRV", "B2", Pin::Reset},
    {"VALID_READ", "U7", Pin::ValidRead},
    {"VALID_WRITE", "U4", Pin::ValidWrite},
};

template <int N>
SignalList makeList(const Signal (&items)[N]) {
    uint32_t mask = 0;
    for (const Signal& s : items) mask |= 1u << s.gpio;
    return SignalList{items, N, mask};
}

const SignalList lists[] = {makeList(addressSignals), makeList(dataInSignals),
                            makeList(dataOutSignals)};

// Pins U2 and U6 drive whatever the mode, used by the power check.
constexpr uint32_t alwaysDrivenMask = (0xFFFu << 8) | (1u << Pin::Reset);

BusMode mode = BusMode::Address;

}  // namespace

const Signal* SignalList::find(uint gpio) const {
    for (int i = 0; i < count; i++) {
        if (items[i].gpio == gpio) return &items[i];
    }
    return nullptr;
}

SignalList signalsFor(BusMode m) { return lists[static_cast<int>(m)]; }

void cardInit() {
    uint32_t inputs = lists[static_cast<int>(BusMode::DataIn)].mask;
    gpio_init_mask(inputs);
    for (uint gpio = 0; gpio < 32; gpio++) {
        // Every input is actively driven by a translator or gate.
        if (inputs & (1u << gpio)) gpio_disable_pulls(gpio);
    }

    // Match the board's pull resistors: R2 selects address, R3 sets ISA -> Pico,
    // R1 leaves IO CH RDY released.
    gpio_init(Pin::AdrDataSel);
    gpio_put(Pin::AdrDataSel, true);
    gpio_set_dir(Pin::AdrDataSel, GPIO_OUT);
    gpio_init(Pin::DataDir);
    gpio_put(Pin::DataDir, false);
    gpio_set_dir(Pin::DataDir, GPIO_OUT);
    gpio_init(Pin::IoChrdy);
    gpio_put(Pin::IoChrdy, false);
    gpio_set_dir(Pin::IoChrdy, GPIO_OUT);

    mode = BusMode::Address;
}

void setMode(BusMode m) {
    // Release GPIO0-7 first, and only drive them once U3 points away from the
    // Pico, so the Pico never fights U1 or U3.
    gpio_set_dir_in_masked(DataMask);
    switch (m) {
        case BusMode::Address:
            gpio_put(Pin::DataDir, false);
            gpio_put(Pin::AdrDataSel, true);
            break;
        case BusMode::DataIn:
            gpio_put(Pin::DataDir, false);
            gpio_put(Pin::AdrDataSel, false);
            break;
        case BusMode::DataOut:
            gpio_put(Pin::AdrDataSel, false);
            gpio_put(Pin::DataDir, true);
            sleep_us(10);
            gpio_put_masked(DataMask, 0);
            gpio_set_dir_out_masked(DataMask);
            break;
    }
    sleep_us(10);
    mode = m;
}

BusMode currentMode() { return mode; }

void driveData(uint8_t value) {
    if (mode != BusMode::DataOut) return;
    gpio_put_masked(DataMask, static_cast<uint32_t>(value) << Pin::AdrData0);
}

void setIoChrdyAsserted(bool asserted) { gpio_put(Pin::IoChrdy, asserted); }

PullReadings readUnderPulls() {
    // On the RP2350 (erratum E9) a floating pin with its input buffer on can
    // stick near 2 V, which the weak pull-down cannot overcome. Pull down with
    // the input buffer off, then turn it on just long enough to read.
    for (uint gpio = 0; gpio < 32; gpio++) {
        if (!(alwaysDrivenMask & (1u << gpio))) continue;
        gpio_set_input_enabled(gpio, false);
        gpio_pull_down(gpio);
    }
    sleep_ms(2);
    for (uint gpio = 0; gpio < 32; gpio++) {
        if (alwaysDrivenMask & (1u << gpio)) gpio_set_input_enabled(gpio, true);
    }
    busy_wait_us(1);
    uint32_t low = gpio_get_all() & alwaysDrivenMask;

    for (uint gpio = 0; gpio < 32; gpio++) {
        if (alwaysDrivenMask & (1u << gpio)) gpio_pull_up(gpio);
    }
    sleep_ms(2);
    uint32_t high = gpio_get_all() & alwaysDrivenMask;

    for (uint gpio = 0; gpio < 32; gpio++) {
        if (alwaysDrivenMask & (1u << gpio)) gpio_disable_pulls(gpio);
    }
    return PullReadings{alwaysDrivenMask, low, high};
}
