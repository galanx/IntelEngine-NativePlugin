#pragma once

#include <string_view>

namespace IntelEngine {

inline bool IsSameFactionPair(std::string_view factionA, std::string_view factionB) {
    return !factionB.empty() && factionA == factionB;
}

}  // namespace IntelEngine
