// Option ROM scan logger. Core 1 watches VALID_READ and, for ISA memory reads
// inside the window (C0000-EFFFF by default), records the first read in each
// 2 KB block and counts the rest; core 0 reports them over USB. Nothing is
// recorded until RESET DRV has pulsed, so the Pico can be connected and
// logging before the machine is switched on.

#include <stdio.h>

#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "card.h"
#include "config.h"

namespace {

constexpr uint32_t AddressMask = 0xFFFFFu << Pin::AdrData0;  // A0-A19 in address mode
constexpr uint32_t WindowEnd = BUSLOG_WINDOW_BASE + BUSLOG_WINDOW_SIZE;
constexpr uint32_t BlockCount = BUSLOG_WINDOW_SIZE / BUSLOG_BLOCK_SIZE;

static_assert((BUSLOG_BLOCK_SIZE & (BUSLOG_BLOCK_SIZE - 1)) == 0,
              "block size must be a power of 2");
static_assert(BUSLOG_WINDOW_SIZE % BUSLOG_BLOCK_SIZE == 0, "window must be whole blocks");

struct FirstRead {
    uint32_t block;  // index into the window
    uint32_t address;
    uint32_t timeUs;
};

// Written by core 1 only, except clearRequested.
volatile FirstRead firstReads[BlockCount];  // in the order the blocks were first read
volatile uint32_t blocksSeen;               // entries in firstReads
volatile uint32_t blockReads[BlockCount];   // all reads, by block
volatile uint32_t windowReads;              // all reads inside the window
volatile uint32_t busReads;                 // all reads anywhere
volatile uint32_t resets;                   // RESET DRV pulses accepted
volatile bool armed;                        // a reset (or key r) has started the log
volatile uint32_t bootTimeUs;               // when RESET DRV last ended
volatile bool clearRequested;               // set by core 0, cleared by core 1 when done

void __not_in_flash_func(clearCapture)() {
    for (uint32_t i = 0; i < BlockCount; i++) blockReads[i] = 0;
    blocksSeen = 0;
    windowReads = 0;
    busReads = 0;
    __dmb();
}

// Must stay in RAM and tight: an 8-bit ISA read holds SMEMR# low for well
// under a microsecond.
void __not_in_flash_func(captureLoop)() {
    const uint32_t readBit = 1u << Pin::ValidRead;
    const uint32_t resetBit = 1u << Pin::Reset;
    // Waits for a read to end also give up on RESET DRV. With the machine off,
    // SMEMR# and AEN sit at 0 V, so VALID_READ reads high until power-on.
    const uint32_t waitMask = readBit | resetBit;
    while (true) {
        uint32_t in = sio_hw->gpio_in;
        if (in & resetBit) {
            // A real RESET DRV lasts many milliseconds; shorter pulses are
            // noise while the card's 5 V rail comes up.
            uint32_t startUs = time_us_32();
            while (sio_hw->gpio_in & resetBit) tight_loop_contents();
            uint32_t endUs = time_us_32();
            if (endUs - startUs < BUSLOG_MIN_RESET_US) continue;
            clearCapture();
            bootTimeUs = endUs;
            armed = true;
            resets = resets + 1;
            continue;
        }
        if (clearRequested) {
            clearCapture();
            bootTimeUs = time_us_32();
            armed = true;
            clearRequested = false;
            continue;
        }
        if (!armed || !(in & readBit)) continue;

        // The address is valid before SMEMR# falls, so this sample holds it.
        uint32_t address = (in & AddressMask) >> Pin::AdrData0;
        busReads = busReads + 1;
        if (address >= BUSLOG_WINDOW_BASE && address < WindowEnd) {
            uint32_t block = (address - BUSLOG_WINDOW_BASE) / BUSLOG_BLOCK_SIZE;
            uint32_t count = blockReads[block];
            if (count == 0) {
                uint32_t n = blocksSeen;
                firstReads[n].block = block;
                firstReads[n].address = address;
                firstReads[n].timeUs = time_us_32();
                __dmb();
                blocksSeen = n + 1;
            }
            blockReads[block] = count + 1;
            windowReads = windowReads + 1;
        }
        while ((sio_hw->gpio_in & waitMask) == readBit) tight_loop_contents();
    }
}

double msSinceBoot(uint32_t us) { return static_cast<int32_t>(us - bootTimeUs) / 1000.0; }

uint32_t nowMs() { return to_ms_since_boot(get_absolute_time()); }

uint32_t blockStart(uint32_t block) { return BUSLOG_WINDOW_BASE + block * BUSLOG_BLOCK_SIZE; }

struct Reporter {
    uint32_t printed = 0;  // firstReads entries already printed
    uint32_t lastWindowReads = 0;
    uint32_t lastActivityMs = 0;
    uint32_t lastStatusMs = 0;
    bool logging = true;
    uint32_t seenResets = 0;

    void restart() {
        printed = 0;
        lastWindowReads = 0;
        logging = true;
        lastActivityMs = lastStatusMs = nowMs();
    }

    // Read counts per block, in the order the blocks were first read.
    void printSummary() {
        uint32_t n = blocksSeen;
        __dmb();
        printf("Reads per block:\n");
        for (uint32_t i = 0; i < n; i++) {
            uint32_t block = firstReads[i].block;
            printf("  %05lX-%05lX  %lu\n", static_cast<unsigned long>(blockStart(block)),
                   static_cast<unsigned long>(blockStart(block) + BUSLOG_BLOCK_SIZE - 1),
                   static_cast<unsigned long>(blockReads[block]));
        }
    }

    void stop(const char* why) {
        logging = false;
        printf("\n--- Logging stopped: %s ---\n", why);
        printf("%lu of %lu blocks read, %lu reads in %05X-%05X, %lu bus reads in all.\n",
               static_cast<unsigned long>(blocksSeen), static_cast<unsigned long>(BlockCount),
               static_cast<unsigned long>(windowReads), BUSLOG_WINDOW_BASE, WindowEnd - 1,
               static_cast<unsigned long>(busReads));
        printSummary();
        printf("The next RESET DRV starts a new log (key r starts one now).\n");
    }

    void printFirstReads() {
        uint32_t n = blocksSeen;
        __dmb();
        for (; printed < n; printed++) {
            volatile FirstRead& r = firstReads[printed];
            printf("  %9.3f ms  block %05lX  first read %05lX\n", msSinceBoot(r.timeUs),
                   static_cast<unsigned long>(blockStart(r.block)),
                   static_cast<unsigned long>(r.address));
        }
        // Repeat reads in known blocks also count as activity.
        uint32_t reads = windowReads;
        if (reads != lastWindowReads) {
            lastWindowReads = reads;
            lastActivityMs = nowMs();
        }
    }

    // Until the scan arrives, show the card is seeing the bus at all.
    void printStatus() {
        if (windowReads || nowMs() - lastStatusMs < BUSLOG_STATUS_MS) return;
        lastStatusMs = nowMs();
        if (!armed) {
            printf("  waiting for RESET DRV: switch the machine on (or press r)\n");
            return;
        }
        printf("  waiting: %lu bus reads so far, none in %05X-%05X\n",
               static_cast<unsigned long>(busReads), BUSLOG_WINDOW_BASE, WindowEnd - 1);
    }

    void poll() {
        int c = getchar_timeout_us(0);
        if (c == 'r') {
            clearRequested = true;
            while (clearRequested) tight_loop_contents();
            restart();
            printf("\n--- Logging now (no reset), times are from this key ---\n");
        }

        if (resets != seenResets) {
            seenResets = resets;
            restart();
            printf("\n--- RESET DRV: logging, times are from the end of reset ---\n");
        }
        if (!logging) return;

        printFirstReads();
        printStatus();
        if (windowReads && nowMs() - lastActivityMs >= BUSLOG_IDLE_STOP_MS) {
            char why[64];
            snprintf(why, sizeof why, "no reads in the window for %d s",
                     BUSLOG_IDLE_STOP_MS / 1000);
            stop(why);
        }
    }
};

Reporter reporter;

}  // namespace

int main() {
    stdio_init_all();
    cardInit();  // address mode: U3 off, nothing drives the ISA bus

    // Hold the strobes low while the card is unpowered so its floating outputs
    // do not look like reads. The gates easily override the pulls.
    gpio_pull_down(Pin::ValidRead);
    gpio_pull_down(Pin::ValidWrite);
    gpio_pull_down(Pin::Reset);

    multicore_launch_core1(captureLoop);

    // Capture runs regardless; reporting waits for a terminal.
    while (!stdio_usb_connected()) sleep_ms(100);
    sleep_ms(500);
    printf(
        "\n=== PicoTide option ROM scan logger ===\n"
        "Logs the first ISA memory read (SMEMR# with AEN low) in each %d KB block\n"
        "of %05X-%05X.\n"
        "Logging starts when RESET DRV pulses: switch the machine on whenever ready.\n"
        "Key r starts logging now instead (for a machine that is already running).\n\n",
        BUSLOG_BLOCK_SIZE / 1024, BUSLOG_WINDOW_BASE, WindowEnd - 1);
    reporter.restart();

    while (true) {
        reporter.poll();
        sleep_ms(BUSLOG_POLL_MS);
    }
}
