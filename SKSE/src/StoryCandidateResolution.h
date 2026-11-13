#pragma once

#include <cctype>
#include <cstdint>
#include <cmath>
#include <functional>
#include <limits>
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

struct StoryCandidateRejection {
    const char* branch = "no-match";
    bool exact = true;
};

// Pool keys are lowercased full display names. Never consult the global NPC index.
inline std::uint32_t ResolveStoryCandidateFormID(std::string_view name,
                                                const StoryCandidatePool& dm,
                                                const StoryCandidatePool& npc,
                                                StoryCandidateRejection* rejection = nullptr,
                                                const std::function<double(std::uint32_t)>& distance = {}) {
    if (rejection) *rejection = {};
    if (dm.empty() && npc.empty()) {
        if (rejection) rejection->branch = "empty-pools";
        return 0;
    }
    std::string lower(name);
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const bool exact : {true, false}) {
        const auto normalized = exact ? std::string{} : NormalizeStoryCandidateName(name);
        if (rejection) rejection->exact = exact;
        if (!exact && normalized.empty()) return 0;
        std::uint32_t match = 0;
        bool found = false;
        double best = std::numeric_limits<double>::infinity();
        const auto sameName = [&](const std::string& candidate) {
            return exact ? candidate == lower : NormalizeStoryCandidateName(candidate) == normalized;
        };
        // Distance tie-break: actors the DM was shown (dm pool) win over same-name actors it never saw.
        bool shown = false;
        if (distance) for (const auto& entry : dm) if (sameName(entry.first)) { shown = true; break; }
        for (const auto* pool : {&dm, &npc}) {
            if (shown && pool == &npc) continue;
            for (const auto& [candidate, formId] : *pool) {
                if (!sameName(candidate)) continue;
                if (distance && formId) {
                    found = true;
                    const double score = distance(formId);
                    if (std::isfinite(score) && score >= 0 && (score < best || (score == best && formId < match))) { best = score; match = formId; }
                    continue;
                }
                if (formId == 0 || (found && match != formId)) {
                    if (rejection) rejection->branch = formId == 0 ? "zero-formid" : "ambiguous";
                    return 0;
                }
                match = formId;
                found = true;
            }
        }
        if (found) return match;
    }
    return 0;
}

}  // namespace IntelEngine
