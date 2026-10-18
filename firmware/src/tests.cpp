#include <stdio.h>
#include <string>
#include <vector>

#include "pico/stdlib.h"

#include "card.h"
#include "config.h"
#include "tests.h"

namespace {

constexpr uint32_t GateMask = (1u << Pin::ValidRead) | (1u << Pin::ValidWrite);
constexpr int Escape = 27;

int pollKey() {
    int c = getchar_timeout_us(0);
    return c == PICO_ERROR_TIMEOUT ? -1 : c;
}

bool isQuit(int c) { return c == 'q' || c == 'Q' || c == Escape; }

void printLines(const SignalList& list, uint32_t bits) {
    bool any = false;
    for (int i = 0; i < list.count; i++) {
        if (bits & (1u << list.items[i].gpio)) {
            printf(" %s", list.items[i].name);
            any = true;
        }
    }
    if (!any) printf(" (none)");
}

// Reports the inputs only once they have held still for DEBOUNCE_MS.
struct Debouncer {
    uint32_t mask;
    uint32_t last;
    absolute_time_t since;

    Debouncer(uint32_t mask)
        : mask(mask), last(gpio_get_all() & mask), since(get_absolute_time()) {}

    bool sample(uint32_t& stable) {
        uint32_t now = gpio_get_all() & mask;
        if (now != last) {
            last = now;
            since = get_absolute_time();
            return false;
        }
        if (absolute_time_diff_us(since, get_absolute_time()) < DEBOUNCE_MS * 1000) return false;
        stable = last;
        return true;
    }
};

struct Step {
    std::string instruction;
    const Signal* signal;  // line that must change
    bool level;            // level that passes the step
    std::string hint;      // shown if the line already sits at that level
};

enum class Outcome { Pass, Skipped, Aborted };

struct Result {
    const Step* step;
    Outcome outcome;
    uint32_t alsoChanged;
};

// The line must be seen at the opposite level before the step can pass, so a
// floating input that happens to sit at the right level is not a false pass.
Outcome runStep(const SignalList& list, uint32_t watch, const Step& step, uint32_t& alsoChanged) {
    uint32_t bit = 1u << step.signal->gpio;
    printf("\n> %s\n", step.instruction.c_str());

    Debouncer in{watch};
    bool armed = false;
    bool hinted = false;
    uint32_t armedAt = 0;
    uint32_t shown = 0;
    absolute_time_t shownAt = nil_time;
    uint32_t stable;
    while (true) {
        int c = pollKey();
        if (c == 's' || c == 'S') {
            printf("  skipped\n");
            return Outcome::Skipped;
        }
        if (isQuit(c)) return Outcome::Aborted;
        if (!in.sample(stable)) continue;

        bool atLevel = ((stable & bit) != 0) == step.level;
        if (!armed && !atLevel) {
            armed = true;
            armedAt = stable;
        } else if (!armed && !hinted) {
            printf("  %s is already %s: %s\n", step.signal->name, step.level ? "HIGH" : "LOW",
                   step.hint.c_str());
            hinted = true;
        }

        if (armed && atLevel) {
            // Lines that moved the same way as the target point to a short.
            uint32_t moved = step.level ? (~armedAt & stable) : (armedAt & ~stable);
            alsoChanged = moved & ~bit;
            printf("  OK, %s reads %s", step.signal->name, step.level ? "HIGH" : "LOW");
            if (alsoChanged) {
                printf(" -- but these changed too:");
                printLines(list, alsoChanged);
            }
            printf("\n");
            return Outcome::Pass;
        }

        // Show what the Pico sees, without flooding the terminal.
        if (stable != shown && absolute_time_diff_us(shownAt, get_absolute_time()) > 250000) {
            printf("  high now:");
            printLines(list, stable);
            printf("\n");
            shown = stable;
            shownAt = get_absolute_time();
        }
    }
}

void runSteps(const char* title, BusMode mode, uint32_t watch, const std::vector<Step>& steps) {
    setMode(mode);
    SignalList list = signalsFor(mode);
    printf("\n=== %s ===\n", title);
    printf("Keys: s = skip this step, q = abort the test.\n");

    std::vector<Result> results;
    for (const Step& step : steps) {
        uint32_t alsoChanged = 0;
        Outcome outcome = runStep(list, watch, step, alsoChanged);
        if (outcome == Outcome::Aborted) {
            printf("\nAborted.\n");
            break;
        }
        results.push_back({&step, outcome, alsoChanged});
    }

    int passed = 0;
    for (const Result& r : results) {
        if (r.outcome == Outcome::Pass && !r.alsoChanged) passed++;
    }
    printf("\n--- %s: %d of %d steps clean ---\n", title, passed, static_cast<int>(steps.size()));
    for (const Result& r : results) {
        if (r.outcome == Outcome::Skipped) {
            printf("  SKIPPED  %s\n", r.step->instruction.c_str());
        } else if (r.alsoChanged) {
            printf("  SUSPECT  %s -- also changed:", r.step->instruction.c_str());
            printLines(list, r.alsoChanged);
            printf("\n");
        }
    }
    if (results.size() < steps.size()) {
        printf("  %d steps not run\n", static_cast<int>(steps.size() - results.size()));
    }
    setMode(BusMode::Address);
}

// Follows one line through the GND, +5 V, GND pattern, each level held for
// PATTERN_HOLD_MS. Changes shorter than GLITCH_MS are ignored, so a floating
// neighbour flickering while the probe moves never advances.
struct PatternTracker {
    enum class Stage { Low, High, LowAgain, Done };

    Stage stage = Stage::Low;
    bool level = false;  // filtered
    absolute_time_t levelSince = nil_time;
    bool raw = false;
    absolute_time_t rawSince = nil_time;

    void reset(bool now, absolute_time_t t) {
        stage = Stage::Low;
        level = raw = now;
        levelSince = rawSince = t;
    }

    bool held(absolute_time_t t) const {
        return absolute_time_diff_us(levelSince, t) >= PATTERN_HOLD_MS * 1000;
    }

    // Returns true when the stage advanced.
    bool update(bool now, absolute_time_t t) {
        if (now != raw) {
            raw = now;
            rawSince = t;
        }
        if (raw != level && absolute_time_diff_us(rawSince, t) >= GLITCH_MS * 1000) {
            level = raw;
            levelSince = rawSince;
        }
        if (!held(t)) return false;

        Stage before = stage;
        if (stage == Stage::Low && !level)
            stage = Stage::High;
        else if (stage == Stage::High && level)
            stage = Stage::LowAgain;
        else if (stage == Stage::LowAgain && !level)
            stage = Stage::Done;
        return stage != before;
    }
};

#ifdef PICO_DEFAULT_LED_PIN
void setLed(bool on) { gpio_put(PICO_DEFAULT_LED_PIN, on); }
#else
void setLed(bool) {}
#endif

void flashLed() {
    for (int i = 0; i < 3; i++) {
        setLed(true);
        sleep_ms(80);
        setLed(false);
        sleep_ms(80);
    }
}

enum class PinResult { NotRun, Pass, Skipped };

// Walks the lines in table order. Every line is tracked at once, so the test
// can tell a short (another line follows the pattern with the target) from a
// wrong finger (another line does the pattern instead of the target).
void patternTest(const char* title, BusMode mode, uint32_t lines) {
    setMode(mode);
    SignalList list = signalsFor(mode);
    std::vector<const Signal*> targets;
    for (int i = 0; i < list.count; i++) {
        if (lines & (1u << list.items[i].gpio)) targets.push_back(&list.items[i]);
    }
    int count = static_cast<int>(targets.size());

    printf(
        "\n=== %s ===\n"
        "On each pin: GND, then +5 V, then GND, holding each level %.1f s.\n"
        "The Pico's LED lights once a level has been held long enough.\n"
        "Keys: s = skip pin, b = back one pin, r = restart pin, q = quit.\n",
        title, PATTERN_HOLD_MS / 1000.0);

    std::vector<PinResult> results(count, PinResult::NotRun);
    std::vector<uint32_t> followers(count, 0);
    PatternTracker trackers[32];
    int index = 0;
    bool quit = false;

    while (index < count && !quit) {
        const Signal& target = *targets[index];
        uint32_t targetBit = 1u << target.gpio;
        printf("\n[%d/%d] %s on edge pin %s: GND, +5 V, GND\n", index + 1, count, target.name,
               target.edgePin);

        absolute_time_t t = get_absolute_time();
        uint32_t bits = gpio_get_all();
        for (uint gpio = 0; gpio < 32; gpio++) {
            if (lines & (1u << gpio)) trackers[gpio].reset(bits & (1u << gpio), t);
        }
        uint32_t warned = 0;
        int next = index;

        while (next == index && !quit) {
            int c = pollKey();
            if (c == 's' || c == 'S') {
                printf("  skipped\n");
                results[index] = PinResult::Skipped;
                next = index + 1;
                break;
            }
            if ((c == 'b' || c == 'B') && index > 0) {
                next = index - 1;
                break;
            }
            if (c == 'r' || c == 'R') break;
            if (isQuit(c)) {
                quit = true;
                break;
            }

            t = get_absolute_time();
            bits = gpio_get_all();
            uint32_t done = 0;
            for (uint gpio = 0; gpio < 32; gpio++) {
                uint32_t bit = 1u << gpio;
                if (!(lines & bit)) continue;
                PatternTracker& tracker = trackers[gpio];
                if (tracker.update(bits & bit, t) && bit == targetBit) {
                    static const char* const reached[] = {"", "  GND held, now +5 V",
                                                          "  +5 V held, now GND", ""};
                    if (tracker.stage != PatternTracker::Stage::Done) {
                        printf("%s\n", reached[static_cast<int>(tracker.stage)]);
                    }
                }
                if (tracker.stage == PatternTracker::Stage::Done) done |= bit;
            }

            if (done & targetBit) {
                followers[index] = done & ~targetBit;
                results[index] = PinResult::Pass;
                printf("\a  PASS %s", target.name);
                if (followers[index]) {
                    printf(" -- but these followed it (short?):");
                    printLines(list, followers[index]);
                }
                printf("\n");
                flashLed();
                next = index + 1;
                break;
            }

            // Another line finished while the target is not about to: the probe
            // is on the wrong finger, or the lines are swapped.
            PatternTracker::Stage targetStage = trackers[target.gpio].stage;
            if (done && targetStage != PatternTracker::Stage::LowAgain) {
                uint32_t fresh = done & ~warned;
                if (fresh) {
                    printf("  pattern seen on");
                    printLines(list, fresh);
                    printf(" instead of %s -- wrong finger or swapped lines?\n", target.name);
                    warned |= fresh;
                }
                for (uint gpio = 0; gpio < 32; gpio++) {
                    if (done & (1u << gpio)) trackers[gpio].reset(bits & (1u << gpio), t);
                }
            }

            setLed(trackers[target.gpio].held(t));
        }
        setLed(false);
        index = next;
    }

    int passed = 0;
    for (int i = 0; i < count; i++) {
        if (results[i] == PinResult::Pass && !followers[i]) passed++;
    }
    printf("\n--- %s: %d of %d pins clean ---\n", title, passed, count);
    for (int i = 0; i < count; i++) {
        const Signal& s = *targets[i];
        if (results[i] == PinResult::Skipped) {
            printf("  SKIPPED  %s (%s)\n", s.name, s.edgePin);
        } else if (results[i] == PinResult::NotRun) {
            printf("  NOT RUN  %s (%s)\n", s.name, s.edgePin);
        } else if (followers[i]) {
            printf("  SUSPECT  %s (%s) -- followed by:", s.name, s.edgePin);
            printLines(list, followers[i]);
            printf("\n");
        }
    }
    setMode(BusMode::Address);
}

}  // namespace

void ledInit() {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif
}

int waitKey() {
    while (true) {
        int c = getchar_timeout_us(100000);
        if (c != PICO_ERROR_TIMEOUT) return c;
    }
}

void printHookup() {
    printf(
        "\nCard power, on the ISA edge (A side = component side):\n"
        "  +5 V: B3 or B29\n"
        "  GND:  B1, B10 or B31\n"
        "Either run the card from a bench 5 V supply with USB plugged in as well\n"
        "(D1 and the Pico's own VBUS diode keep the two supplies apart), or jumper\n"
        "the Pico's VBUS (pin 40) to +5 V on the card. Remove that jumper before\n"
        "the card goes into a PC.\n"
        "For the input tests, use a +5 V wire and a GND wire from the same supply.\n");
}

bool powerCheck() {
    PullReadings r = readUnderPulls();
    uint32_t followed = r.followed();
    printf("\nPower check readings (GPIO: pull-down/pull-up):\n ");
    for (uint gpio = 0; gpio < 32; gpio++) {
        if (!(r.mask & (1u << gpio))) continue;
        printf(" %u:%d/%d", gpio, (r.low >> gpio) & 1, (r.high >> gpio) & 1);
    }
    printf("\n");
    if (!followed) {
        printf("\nPower check: translators are driving their outputs, card 5 V looks present.\n");
        return true;
    }
    printf("\nPower check: WARNING, these lines follow the Pico's pulls, so their\n");
    printf("translator looks unpowered:");
    printLines(signalsFor(BusMode::Address), followed);
    printf("\nCheck +5 V on B3/B29 and the VCCB pins (23, 24) of U2 and U6.\n");
    return false;
}

void addressInputTest() {
    SignalList list = signalsFor(BusMode::Address);
    patternTest("Address and RESET input test", BusMode::Address, list.mask & ~GateMask);
}

void dataInputTest() { patternTest("Data input test", BusMode::DataIn, DataMask); }

void gateTest() {
    SignalList list = signalsFor(BusMode::Address);
    const Signal* read = list.find(Pin::ValidRead);
    const Signal* write = list.find(Pin::ValidWrite);
    std::string pulse = "briefly move one of its inputs to +5 V, then back to GND";

    std::vector<Step> steps = {
        {"Connect AEN (A11) and SMEMR# (B12) to GND and leave them there", read, true, pulse},
        {"Move SMEMR# (B12) to +5 V", read, false, ""},
        {"Move SMEMR# (B12) back to GND", read, true, pulse},
        {"Move AEN (A11) to +5 V", read, false, ""},
        {"Disconnect SMEMR#. Connect AEN (A11) and SMEMW# (B11) to GND", write, true, pulse},
        {"Move SMEMW# (B11) to +5 V", write, false, ""},
        {"Move SMEMW# (B11) back to GND", write, true, pulse},
        {"Move AEN (A11) to +5 V", write, false, ""},
    };
    runSteps("Gate test (VALID_READ = SMEMR# NOR AEN, VALID_WRITE = SMEMW# NOR AEN)",
             BusMode::Address, GateMask, steps);
}

void dataOutputTest() {
    printf(
        "\n=== Data output test ===\n"
        "The card now drives D0-D7 (edge pins A9..A2). Remove any wire touching\n"
        "them first. Measure each pin against GND.\n"
        "Keys: Enter = next pattern, b = previous, q = quit.\n");

    std::vector<uint8_t> patterns = {0x00, 0xFF};
    for (int bit = 0; bit < 8; bit++) patterns.push_back(1u << bit);

    setMode(BusMode::DataOut);
    int index = 0;
    while (index < static_cast<int>(patterns.size())) {
        uint8_t value = patterns[index];
        driveData(value);
        printf("\nPattern %d/%d, data = 0x%02X:\n ", index + 1, static_cast<int>(patterns.size()),
               value);
        for (int bit = 0; bit < 8; bit++) {
            printf(" A%d(D%d)=%s", 9 - bit, bit, (value >> bit) & 1 ? "5V" : "0V");
        }
        printf("\n");

        int c = waitKey();
        if (isQuit(c)) break;
        if (c == 'b' || c == 'B') {
            if (index > 0) index--;
        } else {
            index++;
        }
    }
    setMode(BusMode::Address);
    printf("\nData output test done, D0-D7 released.\n");
}

void ioChrdyTest() {
    printf(
        "\n=== IO CH RDY output test ===\n"
        "U8 is open drain, so it can only pull IO CH RDY (A10) low. Put a 1k-10k\n"
        "resistor from A10 to +5 V to see the released state (a PC's own pull-up\n"
        "does this in a slot). Keys: Enter = toggle, q = quit.\n");

    bool asserted = false;
    while (true) {
        setIoChrdyAsserted(asserted);
        printf("\n%s: A10 should read %s\n", asserted ? "ASSERTED" : "RELEASED",
               asserted ? "~0 V" : "~5 V through your pull-up");
        if (isQuit(waitKey())) break;
        asserted = !asserted;
    }
    setIoChrdyAsserted(false);
    printf("\nIO CH RDY released.\n");
}

void inputMonitor() {
    printf(
        "\n=== Live input monitor ===\n"
        "Prints the lines that read HIGH whenever they change.\n"
        "Keys: a = GPIO0-7 show A0-A7, d = GPIO0-7 show D0-D7, q = quit.\n");

    BusMode mode = BusMode::Address;
    while (true) {
        setMode(mode);
        SignalList list = signalsFor(mode);
        printf("\n[%s mode]\n", mode == BusMode::Address ? "address" : "data");

        Debouncer in{list.mask};
        uint32_t shown = ~0u;
        uint32_t stable;
        int c = -1;
        while (c != 'a' && c != 'd' && !isQuit(c)) {
            c = pollKey();
            if (in.sample(stable) && stable != shown) {
                printf("high:");
                printLines(list, stable);
                printf("\n");
                shown = stable;
            }
        }
        if (isQuit(c)) break;
        mode = c == 'a' ? BusMode::Address : BusMode::DataIn;
    }
    setMode(BusMode::Address);
}
