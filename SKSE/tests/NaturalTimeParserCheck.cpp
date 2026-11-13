// Checks for src/NaturalTimeParser.cpp, run as a plain executable like the other offline checks.
//
// The parser and the case tables up to "prompt / yaml example corpus" are by Lanosity
// (github.com/Lanosity/IntelEngine-NativePlugin, "Fix natural time parser + add unit tests"), written there
// as Catch2 tests; they are kept here case for case without the Catch2 dependency. The sections after that
// cover what was added on top: a bare hour means the next time the clock shows it, small hours "at night"
// are after midnight, "N o'clock" and a lone number.

#include "NaturalTimeParser.h"

#include <cmath>
#include <iostream>
#include <string>

using IntelEngine::NaturalTime::Parse;

namespace {

    constexpr float kNow = 14.0f;  // 2 PM
    int failures = 0;

    void ExpectHour(const char* input, float expected, float now = kNow) {
        const float got = Parse(input, now);
        if (std::fabs(got - expected) > 0.001f) {
            ++failures;
            std::cerr << "FAIL: '" << input << "' at " << now << ": expected " << expected << ", got " << got << "\n";
        }
    }

    struct Case { const char* input; float expected; };
    struct TimedCase { const char* input; float now; float expected; };

    template <std::size_t N>
    void ExpectAll(const Case (&cases)[N], float now = kNow) {
        for (const auto& c : cases) ExpectHour(c.input, c.expected, now);
    }

}  // namespace

int main() {
    // empty and unknown fail
    ExpectHour("", -1.0f);
    ExpectHour("gibberish", -1.0f);
    ExpectHour("sometime later maybe", -1.0f);

    // named times
    static const Case named[] = {
        {"midnight", 0.0f}, {"dawn", 5.0f}, {"at dawn", 5.0f}, {"first light", 5.0f}, {"sunrise", 6.0f},
        {"sun rise", 6.0f}, {"breakfast", 8.0f}, {"morning", 8.0f}, {"noon", 12.0f}, {"midday", 12.0f},
        {"mid-day", 12.0f}, {"afternoon", 14.0f}, {"dinner", 18.0f}, {"supper", 18.0f}, {"evening", 18.0f},
        {"in the evening", 18.0f}, {"sundown", 19.0f}, {"sunset", 19.0f}, {"sun set", 19.0f},
        {"after sunset", 19.0f}, {"twilight", 20.0f}, {"dusk", 20.0f}, {"tonight", 22.0f}, {"night", 22.0f},
        {"dark", 22.0f},
    };
    ExpectAll(named);

    // tomorrow offset
    static const Case tomorrow[] = {
        {"tomorrow", 36.0f}, {"tomorrow noon", 36.0f}, {"tomorrow morning", 32.0f}, {"tomorrow night", 46.0f},
        {"tomorrow at 10 am", 34.0f}, {"tomorrow at 10 in the morning", 34.0f}, {"tomorrow at dawn", 29.0f},
        {"tomorrow at 14:00", 38.0f},
    };
    ExpectAll(tomorrow);

    // long offsets: hour + 24 * days (1 day = 24, 1 week = 168)
    static const Case offsets[] = {
        {"one week later", 180.0f}, {"a week later", 180.0f}, {"in a week", 180.0f}, {"in one week", 180.0f},
        {"1 week later", 180.0f}, {"in 1 week", 180.0f}, {"two weeks later", 348.0f}, {"in 2 weeks", 348.0f},
        {"one week later noon", 180.0f}, {"one week later morning", 176.0f}, {"one week later at 10 am", 178.0f},
        {"a day later", 36.0f}, {"one day later", 36.0f}, {"in 3 days", 84.0f}, {"3 days later", 84.0f},
        {"in three days at 13 hours", 85.0f},
    };
    ExpectAll(offsets);

    // 12h and 24h clock times
    static const Case clock[] = {
        {"10 am", 10.0f}, {"10am", 10.0f}, {"at 10 a.m.", 10.0f}, {"12 am", 0.0f}, {"12 pm", 12.0f},
        {"10:30 pm", 22.5f}, {"3 p.m.", 15.0f}, {"14:00", 14.0f}, {"at 14:30", 14.5f}, {"at 22", 22.0f},
        {"18:00", 18.0f}, {"0:00", 0.0f},
    };
    ExpectAll(clock);

    // hour with period
    static const Case period[] = {
        {"10 in the morning", 10.0f}, {"12 in the morning", 0.0f}, {"3 in the afternoon", 15.0f},
        {"12 in the afternoon", 12.0f}, {"7 in the evening", 19.0f}, {"tonight at 10", 22.0f},
        {"10 tonight", 22.0f}, {"10 at night", 22.0f}, {"three in the afternoon", 15.0f},
        {"tomorrow at 10 in the morning", 34.0f},
    };
    ExpectAll(period);

    // bare at-hour, asked before that hour has come round: the morning reading (the original table)
    static const Case bare[] = {
        {"at 1", 1.0f}, {"at 10", 10.0f}, {"at10", 10.0f}, {"at 12", 12.0f}, {"at noon", 12.0f}, {"at three", 3.0f},
    };
    ExpectAll(bare, 0.5f);

    // relative times use the injected clock
    static const TimedCase relative[] = {
        {"soon", 14.0f, 14.25f}, {"shortly", 23.75f, 0.0f}, {"half an hour", 10.0f, 10.5f},
        {"30 minutes", 10.0f, 10.5f}, {"in 2 hours", 14.0f, 16.0f}, {"in an hour", 14.0f, 15.0f},
        {"in one hour", 14.0f, 15.0f}, {"in 1 hour", 14.0f, 15.0f}, {"a few hours", 20.0f, 22.0f},
        {"couple of hours", 23.0f, 1.0f}, {"in 10 hours", 14.0f, 0.0f},
    };
    for (const auto& c : relative) ExpectHour(c.input, c.expected, c.now);

    // relative without a clock fails, absolute still works
    ExpectHour("in 2 hours", -1.0f, -1.0f);
    ExpectHour("soon", -1.0f, -1.0f);
    ExpectHour("noon", 12.0f, -1.0f);

    // case insensitive
    ExpectHour("NOON", 12.0f);
    ExpectHour("Tomorrow Morning", 32.0f);
    ExpectHour("At 10 AM", 10.0f);

    // prompt / yaml example corpus (intel_schedulemeeting.yaml, safety net, README)
    static const Case corpus[] = {
        {"at dawn", 5.0f}, {"at noon", 12.0f}, {"in the evening", 18.0f}, {"tonight", 22.0f},
        {"after sunset", 19.0f}, {"tomorrow morning", 32.0f}, {"tomorrow at 10 am", 34.0f},
        {"tomorrow at 10 in the morning", 34.0f}, {"at 14:00", 14.0f},
    };
    ExpectAll(corpus);
    ExpectHour("in 2 hours", 16.0f, 14.0f);

    // ---- added on top of the original ----

    // A bare hour is the next time the clock shows it.
    static const TimedCase nextOccurrence[] = {
        {"at 6", 14.0f, 18.0f}, {"at 6", 20.0f, 6.0f}, {"at 6", 3.0f, 6.0f}, {"at six", 8.5f, 18.0f},
        {"at 10", 14.0f, 22.0f}, {"at 1", 14.0f, 1.0f}, {"at 12", 14.0f, 12.0f},
    };
    for (const auto& c : nextOccurrence) ExpectHour(c.input, c.expected, c.now);

    // Without a clock, or for another day, the waking hours decide: 1 to 6 are the afternoon and evening,
    // 7 to 12 are as written. (The 3.7.2 time reader's rule; for a while the morning stood here, which made
    // "tomorrow at 3" three in the night.)
    static const TimedCase wakingHours[] = {
        {"at 6", -1.0f, 18.0f}, {"at 9", -1.0f, 9.0f}, {"at 12", -1.0f, 12.0f}, {"at 1", -1.0f, 13.0f},
        {"tomorrow at 6", 14.0f, 42.0f}, {"tomorrow at 3", 14.0f, 39.0f}, {"tomorrow at 9", 14.0f, 33.0f},
        {"tomorrow at 12", 14.0f, 36.0f}, {"tomorrow at six o'clock", 14.0f, 42.0f}, {"in 3 days at 5", 14.0f, 89.0f},
        // said outright, the small hours stay the small hours
        {"tomorrow at 6 am", 14.0f, 30.0f}, {"tomorrow at 6 in the morning", 14.0f, 30.0f}, {"tomorrow at 6:00", 14.0f, 30.0f},
    };
    for (const auto& c : wakingHours) ExpectHour(c.input, c.expected, c.now);

    // A spelled-out hour with am / pm, and H:MM with the part of the day (both read by 3.7.2, then lost:
    // "six a.m." came back unread and "6:30 in the evening" as half past six in the morning).
    static const Case spelledAndPeriod[] = {
        {"six a.m.", 6.0f}, {"six am", 6.0f}, {"at six a.m.", 6.0f}, {"ten p.m.", 22.0f}, {"ten pm", 22.0f},
        {"twelve am", 0.0f}, {"twelve pm", 12.0f}, {"one pm", 13.0f}, {"tomorrow at six pm", 42.0f},
        {"6:30 in the evening", 18.5f}, {"9:30 tonight", 21.5f}, {"2:30 in the afternoon", 14.5f},
        {"7:15 in the morning", 7.25f}, {"12:30 at night", 0.5f}, {"12:30 in the afternoon", 12.5f},
        {"tomorrow morning at 6:30", 30.5f}, {"18:30 in the evening", 18.5f}, {"6:30", 6.5f},
    };
    ExpectAll(spelledAndPeriod);
    ExpectHour("someone amazing", -1.0f);
    ExpectHour("ten amulets", -1.0f);

    // The cases of the 3.7.2 time reader's own check (tests/ClockTimeCheck.cpp of that branch), as hours of
    // the day: what it understood, this parser understands the same way.
    static const TimedCase from372[] = {
        {"6am", 12, 6}, {"6 am", 12, 6}, {"6 a.m.", 12, 6}, {"at 5am", 12, 5}, {"1 am", 12, 1}, {"12am", 12, 0},
        {"12pm", 3, 12}, {"6:30 pm", 3, 18.5f}, {"six a.m.", 12, 6}, {"11pm", 3, 23},
        {"6 in the morning", 20, 6}, {"six in the evening", 3, 18}, {"9 tonight", 10, 21}, {"2 at night", 10, 2},
        {"3 in the afternoon", 20, 15}, {"morning, around 7", 20, 7},
        {"06:00", 20, 6}, {"06:00", 3, 6}, {"18:30", 3, 18.5f}, {"at 18", 3, 18}, {"0:30", 12, 0.5f},
        {"at 6", 14, 18}, {"at 6", 20, 6}, {"at 6", 3, 6}, {"6 o'clock", 7, 18}, {"6:00", 23, 6}, {"6:00", 14, 6},
        {"at 6:00", -1, 6}, {"12:30", 3, 12.5f}, {"6:30 in the evening", 3, 18.5f}, {"6", 14, 18},
        {"at 6", -1, 18}, {"at 9", -1, 9}, {"at 12", -1, 12},
        {"someone", 12, -1}, {"room 101", 12, -1},
    };
    for (const auto& c : from372) ExpectHour(c.input, c.expected, c.now);

    // Spellings a language model produces for "six in the morning"
    static const Case spellings[] = {
        {"6am", 6.0f}, {"6 a.m.", 6.0f}, {"5am", 5.0f}, {"1 am", 1.0f}, {"6:00", 6.0f}, {"06:00", 6.0f},
        {"6am tomorrow morning", 30.0f}, {"tonight at 9pm", 21.0f}, {"six in the evening", 18.0f},
    };
    ExpectAll(spellings);

    // The small hours "at night" are after midnight, not in the afternoon.
    ExpectHour("2 at night", 2.0f);
    ExpectHour("1 tonight", 1.0f);
    ExpectHour("12 at night", 0.0f);
    ExpectHour("9 at night", 21.0f);

    // "N o'clock" and a lone number read like "at N".
    ExpectHour("6 o'clock", 18.0f, 7.0f);
    ExpectHour("six o'clock", 6.0f, 2.0f);
    ExpectHour("at 6 o'clock", 18.0f, 14.0f);
    ExpectHour("6", 18.0f, 14.0f);
    ExpectHour("room 101", -1.0f);
    ExpectHour("someone", -1.0f);

    if (failures != 0) {
        std::cerr << failures << " time parser case(s) failed\n";
        return 1;
    }
    std::cout << "PASS: named, clock, period, bare, relative and offset time phrases\n";
    return 0;
}
