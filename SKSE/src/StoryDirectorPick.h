#pragma once

// IntelEngine 4.0 staged director: the rule that picks who acts, from what the decision model said about each
// candidate on their own (StoryDirector.cpp asks; decision template prompts/decisions/intel_story_reason.prompt).
// Pure logic without game types, checked offline by SKSE/tests/StoryDirectorCheck.cpp.
//
// Nobody is chosen without a real reason of their own: the model has to name a reason at least "real" and its
// probabilities have to agree on average. Of those, the strongest reason acts; close calls go to the kind of moment
// picked least this session, then to the code's own rank. No one at all is a valid answer: then nothing happens.

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace IntelEngine::StoryDirector {

    // Reason levels, weakest first, as the decision template lists them (its choice keys).
    inline constexpr std::array<std::string_view, 6> kLevels{"none", "idle", "mild", "real", "pressing", "urgent"};
    inline constexpr int kRealLevel = 3;            // "real": the least that may send someone to the player
    inline constexpr float kRealExpected = 2.5f;    // and the probabilities, on average, at least half-way there
    inline constexpr float kCloseCall = 0.5f;       // levels this close are a tie

    inline int LevelOf(std::string_view key) {
        for (std::size_t i = 0; i < kLevels.size(); ++i) {
            if (kLevels[i] == key) return static_cast<int>(i);
        }
        return -1;
    }

    // The probability-weighted level; the chosen key alone when there are no probabilities.
    inline float ExpectedLevel(const std::vector<std::pair<std::string, float>>& probabilities, std::string_view chosen) {
        float sum = 0.0f;
        float weight = 0.0f;
        for (const auto& [key, p] : probabilities) {
            const int level = LevelOf(key);
            if (level < 0 || !(p > 0.0f)) continue;
            sum += static_cast<float>(level) * p;
            weight += p;
        }
        if (weight > 0.0f) return sum / weight;
        const int level = LevelOf(chosen);
        return level < 0 ? 0.0f : static_cast<float>(level);
    }

    struct Verdict {
        std::uint32_t formId = 0;
        std::string name;
        float rank = 0.0f;          // the code's own score for them (bond, absence, rotation ...)
        bool answered = false;      // the model answered for them
        int level = 0;              // the reason level the model chose
        float expected = 0.0f;      // the probability-weighted level
        std::string kind;           // the kind of moment the model chose for them; "" or "none" = none
        std::string allowed;        // the kinds they may be used for, ", "-separated (the candidate's Eligible line)
        float sincePicked = 1.0e6f; // game days since the last moment they were part of
    };

    // Nobody acts again this soon unless their reason became pressing: Elrindir came after the player twice running
    // over the same unpaid bow (2026-11-17), the "same unresolved question on every visit" the old dungeon master
    // was faulted for in the user's own play data.
    inline constexpr float kRepeatDays = 2.0f;
    inline constexpr int kPressingLevel = 4;

    inline bool Allowed(const Verdict& v) {
        if (v.kind.empty() || v.kind == "none") return false;
        std::size_t at = 0;
        while (at <= v.allowed.size()) {
            auto end = v.allowed.find(',', at);
            if (end == std::string::npos) end = v.allowed.size();
            auto item = std::string_view(v.allowed).substr(at, end - at);
            while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
            while (!item.empty() && item.back() == ' ') item.remove_suffix(1);
            if (item == v.kind) return true;
            at = end + 1;
        }
        return false;
    }

    // A world where nothing has happened for this long may bring a gentle moment on a mild reason: someone drops by,
    // passes on news, sends word. Never a quest, an ambush or a stalker on a mild reason. In the first play test
    // (2026-11-17) a whole game day went by with every candidate judged "none" or "mild": nobody ever came, the
    // silence the rework set out to end.
    inline constexpr int kMildLevel = 2;
    inline constexpr float kMildExpected = 1.5f;
    inline constexpr float kQuietDays = 1.5f;

    inline bool Gentle(std::string_view kind) { return kind == "seek_player" || kind == "informant" || kind == "message"; }

    inline bool Qualifies(const Verdict& v, bool quiet = false) {
        if (!v.answered || !Allowed(v)) return false;
        if (v.sincePicked < kRepeatDays && v.level < kPressingLevel) return false;
        if (v.level >= kRealLevel && v.expected >= kRealExpected) return true;
        return quiet && Gentle(v.kind) && v.level >= kMildLevel && v.expected >= kMildExpected;
    }

    // Index of the one who acts, or -1 (nobody has a reason enough: no moment now).
    // timesPicked: how often a kind of moment was picked this session (fewer = fresher).
    // quiet: no moment for kQuietDays or more (a mild reason may then bring a gentle moment).
    inline int Pick(const std::vector<Verdict>& verdicts, const std::function<int(const std::string&)>& timesPicked,
                    bool quiet = false) {
        int best = -1;
        for (int i = 0; i < static_cast<int>(verdicts.size()); ++i) {
            const auto& v = verdicts[i];
            if (!Qualifies(v, quiet)) continue;
            if (best < 0) {
                best = i;
                continue;
            }
            const auto& b = verdicts[best];
            if (v.expected > b.expected + kCloseCall) {
                best = i;
            } else if (v.expected + kCloseCall >= b.expected) {
                // a close call: the fresher kind of moment, then the code's own rank
                const int vt = timesPicked ? timesPicked(v.kind) : 0;
                const int bt = timesPicked ? timesPicked(b.kind) : 0;
                if (vt < bt || (vt == bt && v.rank > b.rank)) best = i;
            }
        }
        return best;
    }
}
