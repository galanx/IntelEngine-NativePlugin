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
    };

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

    inline bool Qualifies(const Verdict& v) {
        return v.answered && v.level >= kRealLevel && v.expected >= kRealExpected && Allowed(v);
    }

    // Index of the one who acts, or -1 (nobody has a real reason: no moment now).
    // timesPicked: how often a kind of moment was picked this session (fewer = fresher).
    inline int Pick(const std::vector<Verdict>& verdicts, const std::function<int(const std::string&)>& timesPicked) {
        int best = -1;
        for (int i = 0; i < static_cast<int>(verdicts.size()); ++i) {
            const auto& v = verdicts[i];
            if (!Qualifies(v)) continue;
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
