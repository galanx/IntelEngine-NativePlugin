#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace IntelEngine {

using StoryCandidatePool = std::unordered_multimap<std::string, std::uint32_t>;

inline std::string NormalizeStoryCandidateName(std::string_view name) {
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.remove_suffix(1);
    if (!name.empty() && name.back() == ']') {
        const auto bracket = name.rfind('[');
        if (bracket != std::string_view::npos && bracket > 0 &&
            std::isspace(static_cast<unsigned char>(name[bracket - 1]))) {
            name = name.substr(0, bracket);
        }
    }
    std::string result;
    result.reserve(name.size());
    bool space = false;
    for (const unsigned char c : name) {
        if (std::isspace(c)) {
            space = !result.empty();
        } else {
            if (space) result += ' ';
            result += static_cast<char>(std::tolower(c));
            space = false;
        }
    }
    return result;
}

// Pool keys are lowercased full display names. Never consult the global NPC index.
inline std::uint32_t ResolveStoryCandidateFormID(std::string_view name,
                                                const StoryCandidatePool& dm,
                                                const StoryCandidatePool& npc) {
    std::string lower(name);
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const bool exact : {true, false}) {
        const auto normalized = exact ? std::string{} : NormalizeStoryCandidateName(name);
        if (!exact && normalized.empty()) return 0;
        std::uint32_t match = 0;
        bool found = false;
        for (const auto* pool : {&dm, &npc}) {
            for (const auto& [candidate, formId] : *pool) {
                if (exact ? candidate != lower : NormalizeStoryCandidateName(candidate) != normalized) continue;
                if (formId == 0 || found) return 0;
                match = formId;
                found = true;
            }
        }
        if (found) return match;
    }
    return 0;
}

}  // namespace IntelEngine
