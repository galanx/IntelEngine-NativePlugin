#include "ClockTime.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using IntelEngine::FindClockTime;
using IntelEngine::NormalizeTimeText;

static void Expect(const std::string& text, float currentHour, float expected) {
    const float got = FindClockTime(NormalizeTimeText(text), currentHour);
    if (std::fabs(got - expected) > 0.01f) {
        throw std::runtime_error("'" + text + "' at " + std::to_string(currentHour) + ": expected " +
                                 std::to_string(expected) + ", got " + std::to_string(got));
    }
}

int main() {
    try {
        // am / pm in every spelling, any hour of the day
        Expect("6am", 12, 6);
        Expect("6 am", 12, 6);
        Expect("6 a.m.", 12, 6);
        Expect("at 5am", 12, 5);
        Expect("1 am", 12, 1);
        Expect("12am", 12, 0);
        Expect("12pm", 3, 12);
        Expect("6:30 pm", 3, 18.5f);
        Expect("six a.m.", 12, 6);
        Expect("11pm", 3, 23);
        // the day part decides when there is no am / pm
        Expect("6 in the morning", 20, 6);
        Expect("six in the evening", 3, 18);
        Expect("9 tonight", 10, 21);
        Expect("2 at night", 10, 2);
        Expect("3 in the afternoon", 20, 15);
        Expect("morning, around 7", 20, 7);
        // 24-hour notation as written
        Expect("06:00", 20, 6);
        Expect("06:00", 3, 6);
        Expect("18:30", 3, 18.5f);
        Expect("at 18", 3, 18);
        Expect("0:30", 12, 0.5f);
        // a plain hour is the next time the clock shows it
        Expect("at 6", 14, 18);
        Expect("at 6", 20, 6);
        Expect("at 6", 3, 6);
        Expect("6 o'clock", 7, 18);
        Expect("6:00", 23, 6);
        Expect("6:00", 14, 6);
        Expect("at 6:00", -1, 6);
        Expect("12:30", 3, 12.5f);
        Expect("6:30 in the evening", 3, 18.5f);
        Expect("6", 14, 18);
        // not today: waking hours
        Expect("at 6", -1, 18);
        Expect("at 9", -1, 9);
        Expect("at 12", -1, 12);
        // counts and other numbers are not clock times
        Expect("in 2 hours", 12, -1);
        Expect("45 minutes", 12, -1);
        Expect("in 3 days", 12, -1);
        Expect("someone", 12, -1);
        Expect("evening", 12, -1);
        Expect("room 101", 12, -1);
        Expect("at 99", 12, -1);
        if (NormalizeTimeText("in two hours") != "in 2 hours") throw std::runtime_error("number words");
        if (NormalizeTimeText("someone at noon") != "someone at noon") throw std::runtime_error("whole words only");
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    std::cout << "PASS: clock times with am/pm, day parts, 24-hour notation, next occurrence and counts\n";
    return 0;
}
