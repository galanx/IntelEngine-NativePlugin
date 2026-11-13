#pragma once

/**
 * Pure natural-language time parsing (no Papyrus / RE:: dependencies).
 *
 * Returns a game hour in [0, 24), or hour + 24*N for explicit day offsets
 * (e.g. "tomorrow noon" → 36, "one week later" → 180). Returns -1 on failure.
 *
 * currentHour is only used for relative phrases ("in 2 hours", "soon").
 * Pass currentHour < 0 when no clock is available; relatives then return -1
 * (absolute phrases still work; day/week offsets do not need a clock).
 */

#include <string_view>

namespace IntelEngine::NaturalTime {

    float Parse(std::string_view condition, float currentHour);

}
