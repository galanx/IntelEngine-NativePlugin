#pragma once

// Reading a clock time out of the free text a language model passes as a meeting time: "6am", "6 a.m.",
// "6:30 pm", "06:00", "18:30", "at 6", "six o'clock", "6 in the morning", "9 tonight".
// Pure functions without game types, so they can be checked on their own (tests/ClockTimeCheck.cpp).

#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>

namespace IntelEngine {

    namespace ClockTimeDetail {
        inline bool IsWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }
        inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }

        inline void ReplaceAll(std::string& text, std::string_view what, std::string_view with) {
            size_t pos = 0;
            while ((pos = text.find(what, pos)) != std::string::npos) {
                text.replace(pos, what.size(), with);
                pos += with.size();
            }
        }

        // Whole words only: "six" becomes "6", the "one" in "someone" stays.
        inline void ReplaceWord(std::string& text, std::string_view word, std::string_view with) {
            size_t pos = 0;
            while ((pos = text.find(word, pos)) != std::string::npos) {
                const size_t end = pos + word.size();
                const bool startOk = pos == 0 || !IsWordChar(text[pos - 1]);
                const bool endOk = end >= text.size() || !IsWordChar(text[end]);
                if (startOk && endOk) {
                    text.replace(pos, word.size(), with);
                    pos += with.size();
                } else {
                    pos = end;
                }
            }
        }

        inline bool ContainsAny(const std::string& text, std::initializer_list<std::string_view> words) {
            for (std::string_view word : words) {
                if (text.find(word) != std::string::npos) return true;
            }
            return false;
        }
    }

    // Lower-case text in; the same text with "a.m." / "p.m." written "am" / "pm" and the numbers one to twelve
    // written as digits, so "six a.m." and "in two hours" read like "6 am" and "in 2 hours".
    inline std::string NormalizeTimeText(std::string text) {
        using namespace ClockTimeDetail;
        ReplaceAll(text, "a.m.", "am");
        ReplaceAll(text, "p.m.", "pm");
        ReplaceAll(text, "a.m", "am");
        ReplaceAll(text, "p.m", "pm");
        struct Number { std::string_view word; std::string_view digits; };
        static constexpr Number numbers[] = {
            {"twelve", "12"}, {"eleven", "11"}, {"ten", "10"}, {"nine", "9"}, {"eight", "8"}, {"seven", "7"},
            {"six", "6"}, {"five", "5"}, {"four", "4"}, {"three", "3"}, {"two", "2"}, {"one", "1"},
        };
        for (const auto& number : numbers) ReplaceWord(text, number.word, number.digits);
        return text;
    }

    // The hour of day the text names (0 up to 24, minutes as the fraction), or -1 when it names no clock time.
    // Expects NormalizeTimeText output.
    //
    // A time without am/pm takes its half of the day from the words around it ("6 in the evening"). Without
    // such a word, a time with minutes or in 24-hour notation ("6:00", "06:00", "18:30", "at 18") is taken as
    // written, and a plain "at 6" means the next time the clock shows 6. currentHour is the hour of day now; pass a negative value when the time
    // is not for today, and a plain hour is then placed in waking hours (7 to 11 in the morning, else after noon).
    inline float FindClockTime(const std::string& text, float currentHour) {
        using namespace ClockTimeDetail;
        const bool nightWords = ContainsAny(text, {"night", "dark"});
        const bool lateWords = nightWords || ContainsAny(text, {"evening", "afternoon", "dinner", "supper", "dusk",
                                                                "sundown", "sunset", "sun set", "twilight"});
        const bool earlyWords = ContainsAny(text, {"morning", "dawn", "sunrise", "sun rise", "breakfast", "first light"});
        const size_t size = text.size();

        size_t i = 0;
        while (i < size) {
            if (!IsDigit(text[i])) { ++i; continue; }
            const size_t start = i;
            size_t j = start;
            while (j < size && IsDigit(text[j])) ++j;
            const size_t digits = j - start;
            i = j;                                              // whatever this number is, go on after it
            if (start > 0 && (IsWordChar(text[start - 1]) || text[start - 1] == ':' || text[start - 1] == '.')) continue;
            if (digits > 2) continue;

            int hour = 0;
            for (size_t d = start; d < j; ++d) hour = hour * 10 + (text[d] - '0');
            int minute = 0;
            bool hasMinutes = false;
            if (j + 2 < size && (text[j] == ':' || text[j] == '.') && IsDigit(text[j + 1]) &&
                IsDigit(text[j + 2]) && (j + 3 >= size || !IsDigit(text[j + 3]))) {
                minute = (text[j + 1] - '0') * 10 + (text[j + 2] - '0');
                hasMinutes = true;
                j += 3;
                i = j;
            }
            if (minute > 59) continue;
            const float fraction = static_cast<float>(minute) / 60.0f;

            size_t k = j;
            while (k < size && text[k] == ' ') ++k;
            const auto startsAt = [&](std::string_view word) { return text.compare(k, word.size(), word) == 0; };
            const auto wordAt = [&](std::string_view word) {
                return startsAt(word) && (k + word.size() >= size || !IsWordChar(text[k + word.size()]));
            };

            if (wordAt("am") || wordAt("pm")) {
                if (hour < 1 || hour > 12) continue;
                return static_cast<float>(hour % 12 + (text[k] == 'p' ? 12 : 0)) + fraction;
            }

            // A count, not a time: "2 hours", "45 minutes", "3 days".
            if (startsAt("hour") || startsAt("min") || startsAt("day") || startsAt("week") || startsAt("sec")) continue;
            if (hour > 24) continue;

            const bool oclock = startsAt("o'clock") || startsAt("oclock") || startsAt("o clock");
            bool led = false;
            for (std::string_view lead : {std::string_view("at "), std::string_view("by "), std::string_view("around "),
                                          std::string_view("about "), std::string_view("until "), std::string_view("till ")}) {
                if (start >= lead.size() && text.compare(start - lead.size(), lead.size(), lead) == 0) led = true;
            }
            const bool alone = start == 0 && k >= size;
            if (!(hasMinutes || oclock || led || alone || lateWords || earlyWords)) continue;

            // 24-hour notation is taken as written: "18:30", "at 18", "06:00", "0:30".
            if (hour >= 13 || hour == 0 || (hasMinutes && text[start] == '0')) {
                return static_cast<float>(hour % 24) + fraction;
            }

            const float morning = static_cast<float>(hour % 12) + fraction;     // 12 counts as 0
            const float evening = morning + 12.0f;
            if (earlyWords && !lateWords) return morning;
            if (lateWords && !earlyWords) {
                // "2 at night" is two in the morning; "9 at night", "6 in the evening", "3 in the afternoon" are PM.
                if (nightWords && hour % 12 <= 4) return morning;
                return evening;
            }
            // Written with minutes and nothing else to go by, it is read as the clock shows it: "6:00" is six
            // in the morning (a model that means the evening writes "18:00" or "6:00 pm").
            if (hasMinutes) return static_cast<float>(hour) + fraction;
            if (currentHour < 0.0f) return (hour % 12 >= 7) ? morning : evening;
            if (currentHour < morning) return morning;
            if (currentHour < evening) return evening;
            return morning;
        }
        return -1.0f;
    }

}  // namespace IntelEngine
