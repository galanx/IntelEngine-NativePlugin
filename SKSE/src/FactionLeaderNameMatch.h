#pragma once

#include "LetterFold.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace IntelEngine {

// Both names in plain letters first (LetterFold.h): "Legatin Rikke" written by the game and "legatin rikke" by a
// user, a Cyrillic name with its capitals or without, "Elisif" for the name with an acute E, are one name.
inline bool IsFactionLeaderNameMatch(std::string_view displayNameAsGiven, std::string_view configuredNameAsGiven) {
    const std::string displayPlain = LetterFold::Plain(displayNameAsGiven);
    const std::string configuredPlain = LetterFold::Plain(configuredNameAsGiven);
    const std::string_view displayName = displayPlain;
    const std::string_view configuredName = configuredPlain;
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

// The records of the people the shipped factions name as their leaders, under the names the shipped configuration
// spells them with. A configuration's names are English; the game's are in the game's language ("Legatin Rikke"):
// there nobody of that name is found, no leader could be met, attacked or counted dead, and no faction ever lost its
// last one. Where the name finds nobody, this says which record is meant. 0: not one of them. (A leader of a faction
// somebody adds can be given by record in the configuration: "0x0001327E", "MyMod.esp|0x000801".)
inline std::uint32_t KnownLeaderRecord(std::string_view configuredName) {
    struct Known {
        std::string_view name;
        std::uint32_t record;
    };
    static constexpr Known kKnown[] = {
        {"general tullius", 0x0001327E},       {"legate rikke", 0x000132A1},        {"ulfric stormcloak", 0x0001414D},
        {"galmar stone-fist", 0x00014128},     {"elenwen", 0x00013269},             {"ondolemar", 0x000133AF},
        {"kodlak whitemane", 0x0001A68E},      {"vilkas", 0x0001A694},              {"mercer frey", 0x0001B07C},
        {"brynjolf", 0x0001B07D},              {"savos aren", 0x0001C19F},          {"mirabelle ervine", 0x0001C1A0},
        {"astrid", 0x0001BDB4},                {"nazir", 0x0001C3AB},               {"thongvor silver-blood", 0x000133BB},
        {"thonar silver-blood", 0x000133BA},   {"maven black-briar", 0x0001336A}};
    for (const auto& known : kKnown) {
        if (known.name.size() != configuredName.size()) continue;
        if (std::equal(known.name.begin(), known.name.end(), configuredName.begin(), [](char lhs, char rhs) {
                return lhs == static_cast<char>(std::tolower(static_cast<unsigned char>(rhs)));
            })) {
            return known.record;
        }
    }
    return 0;
}

}  // namespace IntelEngine
