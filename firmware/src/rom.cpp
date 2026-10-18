// Option ROM server. Core 1 answers ISA memory reads (SMEMR# with AEN low) in
// BUSROM_BASE..+romImageSize by driving the byte from romImage onto the data
// bus; core 0 reports over USB each time the BIOS reads the ROM signature. The
// ROM is served from power-up whether or not a terminal is connected.
//
// With BUSROM_DEBUG set, it also reports how much of the ROM the BIOS has
// read, the first read in each 2 KB block of C0000-FFFFF (to show where the
// BIOS looks for ROMs) and every pulse on RESET DRV.

#include <stdio.h>

#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "card.h"
#include "config.h"
#include "rom_image.h"

namespace {

constexpr uint32_t AddressMask = 0xFFFFFu << Pin::AdrData0;  // A0-A19 in address mode
constexpr uint32_t MaxImageSize = 0x10000;

static_assert(Pin::AdrData0 == 0, "the serve loop assumes D0-D7 are GPIO0-7");

// Written by core 1 only. Offset 0 is only read by option ROM scans, so each
// signature read is one BIOS (or tool) finding the ROM.
volatile uint32_t signatureReads;
volatile uint32_t signatureTimeUs;  // when the last one happened

#if BUSROM_DEBUG
constexpr uint32_t LogEnd = BUSROM_LOG_BASE + BUSROM_LOG_SIZE;
constexpr uint32_t BlockCount = BUSROM_LOG_SIZE / BUSLOG_BLOCK_SIZE;

// One bit per ROM byte, set the first time the BIOS reads it.
volatile uint32_t seen[MaxImageSize / 32];

struct FirstRead {
    uint32_t block;  // index into the log window
    uint32_t address;
    uint32_t timeUs;
};

// Written by core 1 only, except clearRequested.
volatile FirstRead firstReads[BlockCount];  // in the order the blocks were first read
volatile uint32_t blocksSeen;               // entries in firstReads
volatile uint32_t blockReads[BlockCount];   // all reads, by block
volatile uint32_t romReads;                 // reads answered since the last reset
volatile uint32_t firstAddress;             // first address read since the last reset
volatile uint32_t firstTimeUs;              // and when
volatile uint32_t resets;                   // RESET DRV pulses accepted
volatile uint32_t resetPulses;              // all high pulses on GPIO21, accepted or not
volatile uint32_t lastPulseUs;              // width of the last one
volatile uint32_t lastPulseEndUs;           // and when it ended
volatile uint32_t longestPulseUs;           // widest pulse since power-up
volatile uint32_t bootTimeUs;               // when RESET DRV last ended
volatile bool clearRequested;               // set by core 0, cleared by core 1 when done

void __not_in_flash_func(clearStats)() {
    for (uint32_t i = 0; i < MaxImageSize / 32; i++) seen[i] = 0;
    for (uint32_t i = 0; i < BlockCount; i++) blockReads[i] = 0;
    blocksSeen = 0;
    romReads = 0;
    __dmb();
}

// Counts a read in the log window. Called while SMEMR# is still low, which
// leaves several hundred nanoseconds to spare.
void __not_in_flash_func(logRead)(uint32_t address) {
    uint32_t block = (address - BUSROM_LOG_BASE) / BUSLOG_BLOCK_SIZE;
    if (block >= BlockCount) return;
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
}

// Handles a high RESET DRV, returning once it falls.
void __not_in_flash_func(handleReset)(uint32_t resetBit) {
    // A real RESET DRV lasts many milliseconds; shorter pulses are noise
    // while the card's 5 V rail comes up.
    uint32_t startUs = time_us_32();
    while (sio_hw->gpio_in & resetBit) tight_loop_contents();
    uint32_t endUs = time_us_32();
    uint32_t widthUs = endUs - startUs;
    lastPulseUs = widthUs;
    lastPulseEndUs = endUs;
    if (widthUs > longestPulseUs) longestPulseUs = widthUs;
    __dmb();
    resetPulses = resetPulses + 1;
    if (widthUs < BUSLOG_MIN_RESET_US) return;
    clearStats();
    bootTimeUs = endUs;
    resets = resets + 1;
}
#endif

// Must stay in RAM and tight: the byte has to be on the bus well before
// SMEMR# rises, under a microsecond after it falls.
void __not_in_flash_func(serveLoop)() {
    const uint32_t readBit = 1u << Pin::ValidRead;
    const uint32_t selBit = 1u << Pin::AdrDataSel;
    const uint32_t dirBit = 1u << Pin::DataDir;
    const uint32_t size = romImageSize;
#if BUSROM_DEBUG
    const uint32_t resetBit = 1u << Pin::Reset;
    // Waits for a read to end also give up on RESET DRV. With the machine off,
    // SMEMR# and AEN sit at 0 V, so VALID_READ reads high until power-on.
    const uint32_t waitMask = readBit | resetBit;
#else
    const uint32_t waitMask = readBit;
#endif
    while (true) {
        uint32_t in = sio_hw->gpio_in;
#if BUSROM_DEBUG
        if (in & resetBit) {
            handleReset(resetBit);
            continue;
        }
        if (clearRequested) {
            clearStats();
            bootTimeUs = time_us_32();
            clearRequested = false;
            continue;
        }
#endif
        if (!(in & readBit)) continue;

        // The address is valid before SMEMR# falls, so this sample holds it.
        uint32_t address = (in & AddressMask) >> Pin::AdrData0;
        uint32_t offset = address - BUSROM_BASE;
        if (offset >= size) {
#if BUSROM_DEBUG
            logRead(address);
#endif
            while ((sio_hw->gpio_in & waitMask) == readBit) tight_loop_contents();
            continue;
        }

        // Point U3 at the ISA bus while it is still off, then swap U1 out for
        // U3, and only drive GPIO0-7 once U1 has let go of them.
        sio_hw->gpio_togl = (sio_hw->gpio_out ^ romImage[offset]) & DataMask;
        sio_hw->gpio_set = dirBit;
        sio_hw->gpio_clr = selBit;
        __asm volatile("nop\n nop\n nop");
        sio_hw->gpio_oe_set = DataMask;

        // The byte is on the bus, so the bookkeeping can run while it is read.
        if (offset == 0) {
            signatureTimeUs = time_us_32();
            __dmb();
            signatureReads = signatureReads + 1;
        }
#if BUSROM_DEBUG
        logRead(address);
        uint32_t n = romReads;
        if (n == 0) {
            firstAddress = address;
            firstTimeUs = time_us_32();
            __dmb();
        }
        seen[offset >> 5] = seen[offset >> 5] | (1u << (offset & 31));
        romReads = n + 1;
#endif

        while ((sio_hw->gpio_in & waitMask) == readBit) tight_loop_contents();

        // Release in reverse: GPIO0-7 first, then U3 off and U1 back.
        sio_hw->gpio_oe_clr = DataMask;
        sio_hw->gpio_set = selBit;
        sio_hw->gpio_clr = dirBit;
    }
}

#if BUSROM_DEBUG
double msSinceBoot(uint32_t us) { return static_cast<int32_t>(us - bootTimeUs) / 1000.0; }

uint32_t nowMs() { return to_ms_since_boot(get_absolute_time()); }

uint32_t bytesSeen() {
    uint32_t total = 0;
    for (uint32_t i = 0; i < (romImageSize + 31) / 32; i++) total += __builtin_popcount(seen[i]);
    return total;
}

uint32_t blockStart(uint32_t block) { return BUSROM_LOG_BASE + block * BUSLOG_BLOCK_SIZE; }
#else
double msSinceBoot(uint32_t us) { return us / 1000.0; }
#endif

struct Reporter {
    uint32_t seenSignatures = 0;
#if BUSROM_DEBUG
    uint32_t lastReads = 0;
    uint32_t lastStatusMs = 0;
    uint32_t printedBlocks = 0;  // firstReads entries already printed
    bool firstShown = false;
    bool waitingShown = false;
    uint32_t seenResets = 0;
    uint32_t seenPulses = 0;

    void restart(const char* banner) {
        lastReads = 0;
        printedBlocks = 0;
        firstShown = false;
        waitingShown = false;
        lastStatusMs = nowMs();
        printf("\n--- %s ---\n", banner);
    }

    void printBlocks() {
        uint32_t n = blocksSeen;
        __dmb();
        for (; printedBlocks < n; printedBlocks++) {
            volatile FirstRead& r = firstReads[printedBlocks];
            printf("  %9.3f ms  block %05lX  first read %05lX\n", msSinceBoot(r.timeUs),
                   static_cast<unsigned long>(blockStart(r.block)),
                   static_cast<unsigned long>(r.address));
        }
    }

    // Read counts per block, in the order the blocks were first read.
    void printSummary() {
        uint32_t n = blocksSeen;
        __dmb();
        printf("\nGPIO21 (RESET DRV): %lu pulses since power-up, longest %lu us, now %s.\n",
               static_cast<unsigned long>(resetPulses), static_cast<unsigned long>(longestPulseUs),
               gpio_get(Pin::Reset) ? "high" : "low");
        printf("Reads per block (%lu of %lu blocks read):\n", static_cast<unsigned long>(n),
               static_cast<unsigned long>(BlockCount));
        for (uint32_t i = 0; i < n; i++) {
            uint32_t block = firstReads[i].block;
            printf("  %05lX-%05lX  %lu\n", static_cast<unsigned long>(blockStart(block)),
                   static_cast<unsigned long>(blockStart(block) + BUSLOG_BLOCK_SIZE - 1),
                   static_cast<unsigned long>(blockReads[block]));
        }
    }

    void pollDebug() {
        int c = getchar_timeout_us(0);
        if (c == 'r') {
            clearRequested = true;
            while (clearRequested) tight_loop_contents();
            restart("Counts cleared (no reset), times are from this key");
        } else if (c == 's') {
            printSummary();
        }
        // Every pulse on GPIO21, so a reset the filter rejects still shows.
        uint32_t pulses = resetPulses;
        __dmb();
        if (pulses != seenPulses) {
            uint32_t widthUs = lastPulseUs;
            printf(
                "  %9.3f ms  GPIO21 (RESET DRV) high for %lu us, %s (%lu pulses since power-up)\n",
                msSinceBoot(lastPulseEndUs), static_cast<unsigned long>(widthUs),
                widthUs < BUSLOG_MIN_RESET_US ? "ignored as noise" : "accepted",
                static_cast<unsigned long>(pulses));
            seenPulses = pulses;
        }
        if (resets != seenResets) {
            seenResets = resets;
            restart("RESET DRV: counts cleared, times are from the end of reset");
        }

        uint32_t reads = romReads;
        __dmb();
        printBlocks();
        if (reads && !firstShown) {
            printf("  %9.3f ms  first ROM read at %05lX\n", msSinceBoot(firstTimeUs),
                   static_cast<unsigned long>(firstAddress));
            firstShown = true;
        }
        if (nowMs() - lastStatusMs < BUSROM_STATUS_MS) return;
        lastStatusMs = nowMs();
        if (reads != lastReads) {
            printf("  %9.3f ms  %lu ROM reads, %lu of %lu bytes read at least once\n",
                   msSinceBoot(time_us_32()), static_cast<unsigned long>(reads),
                   static_cast<unsigned long>(bytesSeen()),
                   static_cast<unsigned long>(romImageSize));
            lastReads = reads;
        } else if (!reads && !waitingShown) {
            printf("  waiting for the BIOS to read %05X\n", BUSROM_BASE);
            waitingShown = true;
        }
    }
#endif

    void poll() {
        uint32_t signatures = signatureReads;
        __dmb();
        if (signatures != seenSignatures) {
            seenSignatures = signatures;
            printf("  %9.3f ms  option ROM found: signature read at %05X\n",
                   msSinceBoot(signatureTimeUs), BUSROM_BASE);
        }
#if BUSROM_DEBUG
        pollDebug();
#endif
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

    // An image too big to track would leave the BIOS reading FF past the end.
    uint32_t size = romImageSize <= MaxImageSize ? romImageSize : 0;
    if (size) multicore_launch_core1(serveLoop);

    // Serving runs regardless; reporting waits for a terminal.
    while (!stdio_usb_connected()) sleep_ms(100);
    sleep_ms(500);
    if (!size) {
        printf("ROM image is %lu bytes, more than %lu: not serving it.\n",
               static_cast<unsigned long>(romImageSize), static_cast<unsigned long>(MaxImageSize));
        while (true) sleep_ms(1000);
    }
    printf(
        "\n=== PicoTide option ROM server ===\n"
        "Serving a %lu byte image at %05lX-%05lX (signature %02X %02X, %u KB declared).\n",
        static_cast<unsigned long>(romImageSize), static_cast<unsigned long>(BUSROM_BASE),
        static_cast<unsigned long>(BUSROM_BASE + romImageSize - 1), romImage[0], romImage[1],
        romImage[2] / 2);
#if BUSROM_DEBUG
    printf(
        "Debug: also lists the first read in each %d KB block of %05X-%05X.\n"
        "Key s prints reads per block, key r clears the counts (RESET DRV does too).\n"
        "Until a RESET DRV, times are from Pico power-up. GPIO21 (RESET DRV) is %s.\n",
        BUSLOG_BLOCK_SIZE / 1024, BUSROM_LOG_BASE, LogEnd - 1,
        gpio_get(Pin::Reset) ? "high" : "low");
    reporter.restart("Waiting for RESET DRV or ROM reads");
#else
    printf("Times are from Pico power-up.\n\n");
#endif

    while (true) {
        reporter.poll();
        sleep_ms(BUSLOG_POLL_MS);
    }
}
