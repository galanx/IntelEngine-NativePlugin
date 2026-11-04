#pragma once

#include <algorithm>
#include <cctype>
#include <string_view>

namespace IntelEngine {

inline bool IsFactionLeaderNameMatch(std::string_view displayName, std::string_view configuredName) {
    if (displayName.empty() || configuredName.empty() || configuredName.size() > displayName.size()) return false;

    const auto equalIgnoringCase = [](char lhs, char rhs) {
        return std::tolower(static_cast<unsigned char>(lhs)) ==
               std::tolower(static_cast<unsigned char>(rhs));
    };
    const auto isWordCharacter = [](char value) {
        const auto byte = static_cast<unsigned char>(value);
        return byte >= 0x80 || std::isalnum(byte) || value == '_';
    };

    for (auto it = displayName.begin(); it != displayName.end(); ++it) {
        const auto match = std::search(it, displayName.end(), configuredName.begin(), configuredName.end(), equalIgnoringCase);
        if (match == displayName.end()) return false;

        const auto after = match + configuredName.size();
        const bool startsAtBoundary = match == displayName.begin() || !isWordCharacter(*(match - 1));
        const bool endsAtBoundary = after == displayName.end() || !isWordCharacter(*after);
        if (startsAtBoundary && endsAtBoundary) return true;
        it = match;
    }
    return false;
}

}  // namespace IntelEngine
