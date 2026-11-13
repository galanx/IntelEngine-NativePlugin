/**
 * Natural-language time condition → game hour.
 * Host-testable; no Papyrus / CommonLibSSE dependencies.
 */

#include "NaturalTimeParser.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace IntelEngine::NaturalTime {
    namespace {

        std::string ToLower(std::string_view text) {
            std::string result(text);
            std::transform(result.begin(), result.end(), result.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }

        float RelativeHour(float currentHour, float offset) {
            float target = currentHour + offset;
            while (target >= 24.0f) target -= 24.0f;
            while (target < 0.0f) target += 24.0f;
            return target;
        }

    }  // namespace

    float Parse(std::string_view condition, float currentHour) {
        std::string input = ToLower(condition);
        if (input.empty()) return -1.0f;

        auto isTokenChar = [](unsigned char c) {
            return std::isalnum(c) != 0;
        };

        auto trimInput = [&]() {
            while (!input.empty() && input.front() == ' ') input.erase(input.begin());
            while (!input.empty() && input.back() == ' ') input.pop_back();
        };

        // Day offsets are returned as hour + 24*N (absolute phrases still pick the clock hour).
        // Supports: "tomorrow", "one week later", "in 3 days", "in three days at 13 hours", etc.
        float dayOffset = 0.0f;
        auto tomorrowPos = input.find("tomorrow");
        if (tomorrowPos != std::string::npos) {
            dayOffset = 24.0f;
            input.erase(tomorrowPos, 8);  // "tomorrow" = 8 chars
            trimInput();
            if (input.empty()) {
                return 12.0f + dayOffset;  // "tomorrow" alone → noon tomorrow
            }
        }

        // "N day(s)/week(s) [later]" / "in N day(s)/week(s)"
        if (dayOffset == 0.0f) {
            struct CountWord {
                const char* text;
                int count;
            };
            static const CountWord countWords[] = {
                {"twelve", 12}, {"eleven", 11}, {"ten", 10},
                {"nine", 9},    {"eight", 8},   {"seven", 7}, {"six", 6},
                {"five", 5},    {"four", 4},    {"three", 3}, {"two", 2},
                {"one", 1},     {"an", 1},      {"a", 1},
            };

            auto tryUnitOffset = [&](const char* unit, float hoursPerUnit) -> bool {
                const size_t unitBaseLen = std::strlen(unit);
                size_t pos = 0;
                while ((pos = input.find(unit, pos)) != std::string::npos) {
                    const char before = (pos > 0) ? input[pos - 1] : ' ';
                    if (isTokenChar(static_cast<unsigned char>(before))) {
                        ++pos;
                        continue;
                    }
                    size_t unitEnd = pos + unitBaseLen;
                    if (unitEnd < input.size() && input[unitEnd] == 's') ++unitEnd;
                    const char afterUnit = (unitEnd < input.size()) ? input[unitEnd] : ' ';
                    if (isTokenChar(static_cast<unsigned char>(afterUnit))) {
                        ++pos;
                        continue;
                    }

                    // Count token immediately before the unit (digits or word).
                    size_t countEnd = pos;
                    while (countEnd > 0 && input[countEnd - 1] == ' ') --countEnd;

                    int count = 0;
                    size_t eraseStart = std::string::npos;

                    size_t digitStart = countEnd;
                    while (digitStart > 0 &&
                           std::isdigit(static_cast<unsigned char>(input[digitStart - 1]))) {
                        --digitStart;
                    }
                    if (digitStart < countEnd) {
                        int value = 0;
                        for (size_t i = digitStart; i < countEnd; ++i) {
                            value = value * 10 + (input[i] - '0');
                        }
                        if (value > 0) {
                            count = value;
                            eraseStart = digitStart;
                        }
                    } else {
                        for (const auto& cw : countWords) {
                            const size_t len = std::strlen(cw.text);
                            if (countEnd < len) continue;
                            const size_t start = countEnd - len;
                            if (input.compare(start, len, cw.text) != 0) continue;
                            const char beforeCount = (start > 0) ? input[start - 1] : ' ';
                            if (isTokenChar(static_cast<unsigned char>(beforeCount))) continue;
                            count = cw.count;
                            eraseStart = start;
                            break;
                        }
                    }
                    if (count <= 0 || eraseStart == std::string::npos) {
                        ++pos;
                        continue;
                    }

                    // Optional leading "in "
                    size_t beforeCount = eraseStart;
                    while (beforeCount > 0 && input[beforeCount - 1] == ' ') --beforeCount;
                    if (beforeCount >= 2 && input.compare(beforeCount - 2, 2, "in") == 0) {
                        const char beforeIn =
                            (beforeCount >= 3) ? input[beforeCount - 3] : ' ';
                        if (!isTokenChar(static_cast<unsigned char>(beforeIn))) {
                            eraseStart = beforeCount - 2;
                        }
                    }

                    // Optional trailing " later"
                    size_t eraseEnd = unitEnd;
                    size_t laterScan = eraseEnd;
                    while (laterScan < input.size() && input[laterScan] == ' ') ++laterScan;
                    if (laterScan + 5 <= input.size() &&
                        input.compare(laterScan, 5, "later") == 0) {
                        const char afterLater =
                            (laterScan + 5 < input.size()) ? input[laterScan + 5] : ' ';
                        if (!isTokenChar(static_cast<unsigned char>(afterLater))) {
                            eraseEnd = laterScan + 5;
                        }
                    }

                    dayOffset = static_cast<float>(count) * hoursPerUnit;
                    input.erase(eraseStart, eraseEnd - eraseStart);
                    trimInput();
                    return true;
                }
                return false;
            };

            // Weeks before days so "weekday" is not an issue; "week" is more specific.
            if (!tryUnitOffset("week", 168.0f)) {
                tryUnitOffset("day", 24.0f);
            }
            if (dayOffset > 0.0f && input.empty()) {
                return 12.0f + dayOffset;  // bare offset → noon that day
            }
        }

        auto relativeHour = [&](float offset) -> float {
            if (currentHour < 0.0f) return -1.0f;
            return RelativeHour(currentHour, offset) + dayOffset;
        };

        auto findBounded = [&](const char* text) -> bool {
            const size_t len = std::strlen(text);
            size_t pos = 0;
            while ((pos = input.find(text, pos)) != std::string::npos) {
                const char before = (pos > 0) ? input[pos - 1] : ' ';
                size_t afterPos = pos + len;
                // Allow plural "hour"/"minute" → "hours"/"minutes"
                if (afterPos < input.size() && input[afterPos] == 's') ++afterPos;
                const char after = (afterPos < input.size()) ? input[afterPos] : ' ';
                if (!isTokenChar(static_cast<unsigned char>(before)) &&
                    !isTokenChar(static_cast<unsigned char>(after))) {
                    return true;
                }
                pos += len;
            }
            return false;
        };

        // --- Relative time patterns ---
        if (input.find("soon") != std::string::npos ||
            input.find("shortly") != std::string::npos ||
            input.find("a moment") != std::string::npos ||
            input.find("right away") != std::string::npos ||
            input.find("immediately") != std::string::npos) {
            return relativeHour(0.25f);
        }

        if (findBounded("half an hour") || findBounded("30 minute")) {
            return relativeHour(0.5f);
        }

        if (input.find("few hour") != std::string::npos ||
            input.find("couple hour") != std::string::npos ||
            input.find("couple of hour") != std::string::npos) {
            return relativeHour(2.0f);
        }

        // "N hour(s)" — check from high to low; token bounds avoid "3 hour" inside "13 hours"
        struct HourPattern {
            const char* text;
            float hours;
        };
        static const HourPattern hourPatterns[] = {
            {"12 hour", 12.0f}, {"11 hour", 11.0f}, {"10 hour", 10.0f},
            {"9 hour", 9.0f},   {"8 hour", 8.0f},   {"7 hour", 7.0f},
            {"6 hour", 6.0f},   {"5 hour", 5.0f},   {"4 hour", 4.0f},
            {"3 hour", 3.0f},   {"2 hour", 2.0f},
        };
        for (const auto& hp : hourPatterns) {
            if (findBounded(hp.text)) {
                return relativeHour(hp.hours);
            }
        }

        // "1 hour" / "an hour" / "one hour" — after multi-digit patterns
        if (findBounded("1 hour") || findBounded("an hour") || findBounded("one hour")) {
            return relativeHour(1.0f);
        }

        // Normalize a.m. / p.m. variants so "10 a.m." and "10 am" share one path
        {
            auto replaceAll = [](std::string& s, const char* from, const char* to) {
                const size_t fromLen = std::strlen(from);
                const size_t toLen = std::strlen(to);
                size_t pos = 0;
                while ((pos = s.find(from, pos)) != std::string::npos) {
                    s.replace(pos, fromLen, to);
                    pos += toLen;
                }
            };
            replaceAll(input, "a.m.", "am");
            replaceAll(input, "p.m.", "pm");
            replaceAll(input, "a.m", "am");
            replaceAll(input, "p.m", "pm");
        }

        // --- Explicit clock times ---
        // Supports:
        //   12h: "10 am", "10am", "10:30 pm", "at 10 a.m."
        //   24h: "14:00", "at 14:30", "at 22", "18:00"
        // Bare 1-12 without am/pm is left for period/"at N" handlers below.
        {
            for (size_t i = 0; i < input.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(input[i]))) continue;
                if (i > 0 && std::isdigit(static_cast<unsigned char>(input[i - 1]))) continue;

                size_t j = i;
                int hour = 0;
                int digitCount = 0;
                while (j < input.size() && std::isdigit(static_cast<unsigned char>(input[j])) && digitCount < 2) {
                    hour = hour * 10 + (input[j] - '0');
                    ++j;
                    ++digitCount;
                }
                // Skip longer numeric tokens (e.g. years) and 3–4 digit blobs without a colon
                if (j < input.size() && std::isdigit(static_cast<unsigned char>(input[j]))) {
                    continue;
                }

                int minute = 0;
                bool hasColon = false;
                if (j < input.size() && input[j] == ':') {
                    hasColon = true;
                    ++j;
                    if (j + 1 >= input.size() ||
                        !std::isdigit(static_cast<unsigned char>(input[j])) ||
                        !std::isdigit(static_cast<unsigned char>(input[j + 1]))) {
                        continue;
                    }
                    minute = (input[j] - '0') * 10 + (input[j + 1] - '0');
                    if (minute >= 60) continue;
                    j += 2;
                    // Reject trailing extra digits ("14:001")
                    if (j < input.size() && std::isdigit(static_cast<unsigned char>(input[j]))) continue;
                }

                // Optional meridiem immediately after or after spaces: "10am", "10 am", "10:00 pm"
                size_t k = j;
                while (k < input.size() && input[k] == ' ') ++k;
                auto meridiemAt = [&](size_t pos) -> int {
                    // returns 1=am, 2=pm, 0=none
                    if (pos + 1 >= input.size()) return 0;
                    if (input[pos] == 'a' && input[pos + 1] == 'm') {
                        if (pos + 2 < input.size() && isTokenChar(static_cast<unsigned char>(input[pos + 2]))) return 0;
                        return 1;
                    }
                    if (input[pos] == 'p' && input[pos + 1] == 'm') {
                        if (pos + 2 < input.size() && isTokenChar(static_cast<unsigned char>(input[pos + 2]))) return 0;
                        return 2;
                    }
                    return 0;
                };
                const int meridiem = meridiemAt(k);

                float hourFloat = -1.0f;
                if (meridiem != 0) {
                    if (hour < 1 || hour > 12) continue;
                    if (meridiem == 1) {
                        hourFloat = (hour == 12) ? 0.0f : static_cast<float>(hour);
                    } else {
                        hourFloat = (hour == 12) ? 12.0f : static_cast<float>(hour + 12);
                    }
                    hourFloat += static_cast<float>(minute) / 60.0f;
                } else if (hasColon) {
                    // 24h HH:MM (also accepts 0:00–23:59)
                    if (hour > 23) continue;
                    hourFloat = static_cast<float>(hour) + static_cast<float>(minute) / 60.0f;
                } else if (hour >= 13 && hour <= 23) {
                    // Bare 24h hour: "at 14", "22"
                    hourFloat = static_cast<float>(hour);
                } else {
                    continue;  // bare 0-12 → period / "at N" handlers
                }

                return hourFloat + dayOffset;
            }
        }

        // --- Clock hour + period: "10 in the morning", "3 in the afternoon" ---
        // Must run BEFORE bare named times so the numeric hour wins over period defaults.
        struct ClockPattern {
            const char* text;
            int hour;
        };
        auto matchClockWithPeriod = [&](const char* periodKey, int (*toHour24)(int)) -> float {
            // Digits first (longer first), then word numbers
            static const ClockPattern digitHours[] = {
                {"12", 12}, {"11", 11}, {"10", 10},
                {"9", 9},   {"8", 8},   {"7", 7},   {"6", 6},
                {"5", 5},   {"4", 4},   {"3", 3},   {"2", 2}, {"1", 1},
            };
            static const ClockPattern wordHours[] = {
                {"twelve", 12}, {"eleven", 11}, {"ten", 10},
                {"nine", 9},    {"eight", 8},   {"seven", 7}, {"six", 6},
                {"five", 5},    {"four", 4},    {"three", 3}, {"two", 2}, {"one", 1},
            };

            auto hasPeriodToken = [&](const char* key) -> bool {
                const size_t keyLen = std::strlen(key);
                size_t pos = 0;
                while ((pos = input.find(key, pos)) != std::string::npos) {
                    const char before = (pos > 0) ? input[pos - 1] : ' ';
                    const char after = (pos + keyLen < input.size()) ? input[pos + keyLen] : ' ';
                    if (!isTokenChar(static_cast<unsigned char>(before)) &&
                        !isTokenChar(static_cast<unsigned char>(after))) {
                        return true;
                    }
                    pos += keyLen;
                }
                return false;
            };
            if (!hasPeriodToken(periodKey)) return -1.0f;

            auto tryList = [&](const ClockPattern* list, size_t count) -> float {
                for (size_t i = 0; i < count; ++i) {
                    const auto& ch = list[i];
                    size_t pos = 0;
                    const size_t len = std::strlen(ch.text);
                    while ((pos = input.find(ch.text, pos)) != std::string::npos) {
                        const char before = (pos > 0) ? input[pos - 1] : ' ';
                        const char after = (pos + len < input.size()) ? input[pos + len] : ' ';
                        if (isTokenChar(static_cast<unsigned char>(before)) ||
                            isTokenChar(static_cast<unsigned char>(after))) {
                            pos += len;
                            continue;
                        }
                        return static_cast<float>(toHour24(ch.hour)) + dayOffset;
                    }
                }
                return -1.0f;
            };

            float hit = tryList(digitHours, sizeof(digitHours) / sizeof(digitHours[0]));
            if (hit >= 0.0f) return hit;
            return tryList(wordHours, sizeof(wordHours) / sizeof(wordHours[0]));
        };

        float periodHit = matchClockWithPeriod("morning", [](int h) {
            return (h == 12) ? 0 : h;  // "12 in the morning" → midnight
        });
        if (periodHit >= 0.0f) return periodHit;

        periodHit = matchClockWithPeriod("afternoon", [](int h) {
            return (h == 12) ? 12 : h + 12;
        });
        if (periodHit >= 0.0f) return periodHit;

        periodHit = matchClockWithPeriod("evening", [](int h) {
            return (h == 12) ? 12 : h + 12;
        });
        if (periodHit >= 0.0f) return periodHit;

        // "tonight at 10" / "10 tonight" → 22:00; bare "tonight" falls through to named times
        periodHit = matchClockWithPeriod("tonight", [](int h) {
            return (h == 12) ? 0 : h + 12;
        });
        if (periodHit >= 0.0f) return periodHit;

        // "10 at night" / "10 in the night"
        periodHit = matchClockWithPeriod("night", [](int h) {
            return (h == 12) ? 0 : h + 12;
        });
        if (periodHit >= 0.0f) return periodHit;

        // --- Bare "at N" / "at N o'clock" (no am/pm) ---
        // Prefer morning hours for 1-11; "at 12" → noon. Past-hour roll handled by CalculateTargetGameTime.
        static const ClockPattern atPatterns[] = {
            {"at 12", 12}, {"at12", 12},
            {"at 11", 11}, {"at11", 11},
            {"at 10", 10}, {"at10", 10},
            {"at 9", 9},   {"at9", 9},
            {"at 8", 8},   {"at8", 8},
            {"at 7", 7},   {"at7", 7},
            {"at 6", 6},   {"at6", 6},
            {"at 5", 5},   {"at5", 5},
            {"at 4", 4},   {"at4", 4},
            {"at 3", 3},   {"at3", 3},
            {"at 2", 2},   {"at2", 2},
            {"at 1", 1},   {"at1", 1},
            {"at twelve", 12}, {"at eleven", 11}, {"at ten", 10},
            {"at nine", 9},    {"at eight", 8},   {"at seven", 7}, {"at six", 6},
            {"at five", 5},    {"at four", 4},    {"at three", 3}, {"at two", 2}, {"at one", 1},
        };
        for (const auto& cp : atPatterns) {
            auto pos = input.find(cp.text);
            if (pos == std::string::npos) continue;
            const size_t len = std::strlen(cp.text);
            const char after = (pos + len < input.size()) ? input[pos + len] : ' ';
            // Don't treat "at 10" as a hit inside "at 100" / "at 10th" / "at tenth"
            if (std::isalnum(static_cast<unsigned char>(after))) continue;
            return static_cast<float>(cp.hour) + dayOffset;
        }

        // --- Named time patterns (substring match) ---
        // Order matters: more specific terms before substrings they contain
        // e.g., "midnight" before "night", "afternoon" before "noon"
        struct NamedTime {
            const char* keyword;
            float hour;
        };
        static const NamedTime namedTimes[] = {
            {"midnight", 0.0f},
            {"first light", 5.0f},
            {"dawn", 5.0f},
            {"sunrise", 6.0f},
            {"sun rise", 6.0f},
            {"breakfast", 8.0f},
            {"morning", 8.0f},
            {"midday", 12.0f},
            {"mid-day", 12.0f},
            {"afternoon", 14.0f},  // should be checked before "noon"
            {"noon", 12.0f},
            {"dinner", 18.0f},
            {"supper", 18.0f},
            {"evening", 18.0f},
            {"sundown", 19.0f},
            {"sun set", 19.0f},
            {"sunset", 19.0f},
            {"twilight", 20.0f},
            {"dusk", 20.0f},
            {"tonight", 22.0f},  // should be checked before "night"
            {"night", 22.0f},
            {"dark", 22.0f},
        };
        for (const auto& nt : namedTimes) {
            if (input.find(nt.keyword) != std::string::npos) {
                return nt.hour + dayOffset;
            }
        }

        return -1.0f;
    }

}  // namespace IntelEngine::NaturalTime
