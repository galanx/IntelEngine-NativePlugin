#pragma once

#include "GameText.h"
#include "NameForms.h"
#include "Utf8Fold.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
            result += static_cast<char>(c);
            space = false;
        }
    }
    // Lower case for every script: the pools' keys are folded this way (NPCIndex), and a Cyrillic name lowered byte
    // by byte kept its capital and matched nobody.
    return FoldUtf8(GameText::In(result));
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
    const std::string lower = FoldUtf8(GameText::In(std::string(name)));
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
    // The name in another form (NameForms.h: a case ending, an article, a particle): the one name of the pools it
    // stands for, those the dungeon master was shown first; then as that name, by the rules above.
    for (const auto* pool : {&dm, &npc}) {
        std::vector<std::string> keys;
        keys.reserve(pool->size());
        for (const auto& entry : *pool) {
            if (std::find(keys.begin(), keys.end(), entry.first) == keys.end()) keys.push_back(entry.first);
        }
        const auto forms = NameForms::Find(lower, keys, NameForms::PartForms);
        if (forms && keys[static_cast<std::size_t>(forms.index)] != lower) {
            return ResolveStoryCandidateFormID(keys[static_cast<std::size_t>(forms.index)], dm, npc, rejection, distance);
        }
    }
    return 0;
}

// ---- The social engine's answer: two people of one group of its request ------------------------------------------
//
// The request offers people in groups, one per place, and asks for two of one group. A load order may hold several
// people of one name: a mod's copy of Sven kept in a back room, another mod's own Sven, three "Imperial Soldier" in a
// camp. A name alone then says nobody, and the old rule took the one nearest the player, which is nobody when the
// scene is somewhere the player is not: on a list of some 1,500 plugins every pair the model chose at Riverwood's inn
// was thrown away after the model had been paid for it, fourteen times in seventy minutes (2026-11-23). So the two
// are looked for together, in the group they were offered in.

// Which group of the request (0 is the first offered) each offered person stood in.
using StoryCandidateGroups = std::unordered_map<std::uint32_t, std::uint32_t>;

// Everyone of the pool this name can stand for, by the first rule that finds anybody: the name as it is, the name
// without its bracketed role, the name in another form (NameForms.h). In order of their ids, each once.
inline std::vector<std::uint32_t> StoryCandidatesNamed(std::string_view name, const StoryCandidatePool& pool) {
    std::vector<std::uint32_t> found;
    const auto take = [&](const auto& same) {
        for (const auto& [candidate, formId] : pool) {
            if (formId != 0 && same(candidate) && std::find(found.begin(), found.end(), formId) == found.end()) found.push_back(formId);
        }
        std::sort(found.begin(), found.end());
        return !found.empty();
    };
    const std::string lower = FoldUtf8(GameText::In(std::string(name)));
    if (lower.empty()) return found;
    if (take([&](const std::string& candidate) { return candidate == lower; })) return found;
    const std::string normalized = NormalizeStoryCandidateName(name);
    if (normalized.empty()) return found;
    if (take([&](const std::string& candidate) { return NormalizeStoryCandidateName(candidate) == normalized; })) return found;
    std::vector<std::string> keys;
    for (const auto& entry : pool) {
        if (std::find(keys.begin(), keys.end(), entry.first) == keys.end()) keys.push_back(entry.first);
    }
    std::sort(keys.begin(), keys.end());   // the pool has no order of its own: the same answer every time
    const auto forms = NameForms::Find(lower, keys, NameForms::PartForms);
    if (forms) {
        const std::string& key = keys[static_cast<std::size_t>(forms.index)];
        take([&](const std::string& candidate) { return candidate == key; });
    }
    return found;
}

struct StoryPair {
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    // "same-group": both of one group. "by-name": no group holds both, and each name stands for exactly one person.
    // Otherwise nobody: "no-match" (a name nobody offered carries), "ambiguous" (several of a name, no group holds
    // both), "same-person" (both names are the one person).
    const char* how = "no-match";
    bool told = false;      // a name stood for several people, and the group told which
    explicit operator bool() const { return first != 0 && second != 0; }
};

inline StoryPair ResolveStoryPair(std::string_view name1, std::string_view name2, const StoryCandidatePool& pool,
                                  const StoryCandidateGroups& groupOf) {
    StoryPair pair;
    const auto a = StoryCandidatesNamed(name1, pool);
    const auto b = StoryCandidatesNamed(name2, pool);
    if (a.empty() || b.empty()) return pair;
    // The groups in the order they were offered; in a group the lowest ids, so that the answer is the same every time.
    std::vector<std::uint32_t> groups;
    for (const auto id : a) {
        const auto it = groupOf.find(id);
        if (it != groupOf.end() && std::find(groups.begin(), groups.end(), it->second) == groups.end()) groups.push_back(it->second);
    }
    std::sort(groups.begin(), groups.end());
    const auto in = [&](std::uint32_t id, std::uint32_t group) {
        const auto it = groupOf.find(id);
        return it != groupOf.end() && it->second == group;
    };
    for (const auto group : groups) {
        for (const auto x : a) {
            if (!in(x, group)) continue;
            for (const auto y : b) {
                if (y == x || !in(y, group)) continue;
                pair.first = x;
                pair.second = y;
                pair.how = "same-group";
                pair.told = a.size() > 1 || b.size() > 1;
                return pair;
            }
        }
    }
    if (a.size() == 1 && b.size() == 1) {
        if (a[0] == b[0]) {
            pair.how = "same-person";
            return pair;
        }
        pair.first = a[0];
        pair.second = b[0];
        pair.how = "by-name";
        return pair;
    }
    pair.how = "ambiguous";
    return pair;
}

// The second of a pair whose first is known: of the first one's group; where the group holds nobody of that name,
// the one person of the pool it stands for. Never the first one again.
inline std::uint32_t ResolveStoryPartner(std::string_view name, std::uint32_t partner, const StoryCandidatePool& pool,
                                         const StoryCandidateGroups& groupOf) {
    auto named = StoryCandidatesNamed(name, pool);
    named.erase(std::remove(named.begin(), named.end(), partner), named.end());
    if (named.empty()) return 0;
    const auto group = groupOf.find(partner);
    if (group != groupOf.end()) {
        for (const auto id : named) {
            const auto it = groupOf.find(id);
            if (it != groupOf.end() && it->second == group->second) return id;
        }
    }
    return named.size() == 1 ? named[0] : 0;
}

}  // namespace IntelEngine
