#pragma once

// The English ladder of LocationResolver::DetectSemanticIntent as it stood until 4.0's place words became data
// (PlaceWords.h), lifted out word for word by a script (only the reading of language packs in front of it and the
// lower-casing are taken off: the check hands text in lower case). PlaceWordsCheck holds the two side by side: what
// this ladder understood, the rows of data must understand the same way. Not built into the DLL.

#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace OldLadder {

    enum class HomeOwner { NONE, NPC, PLAYER, NAMED };

    struct SemanticIntent {
        enum Type { NONE, UPSTAIRS, DOWNSTAIRS, OUTSIDE, INSIDE, BACK, CELLAR, BEDROOM, KITCHEN, HOME, WATER };
        Type type = NONE;
        std::string locationContext;
        bool preferBeds = false;
        HomeOwner homeOwner = HomeOwner::NONE;
        std::string homeOwnerHint;
        bool preferInterior = false;
    };

    inline std::string TrimStd(const std::string& text) {
        const char* whitespace = " \t\n\r\f\v";
        const auto first = text.find_first_not_of(whitespace);
        if (first == std::string::npos) return "";
        const auto last = text.find_last_not_of(whitespace);
        return text.substr(first, last - first + 1);
    }

    inline SemanticIntent DetectSemanticIntent(const std::string& said) {
        std::string lower = said;   // the check hands it in lower case

        // Helper: check if string mentions bed/bedroom
        auto hasBedContext = [&lower]() {
            return lower.find("bedroom") != std::string::npos ||
                   lower.find("my room") != std::string::npos ||
                   lower.find("my bed") != std::string::npos;
        };

        // === Exact semantic terms (fast path) ===
        if (lower == "upstairs" || lower == "up" || lower == "above")
            return {SemanticIntent::UPSTAIRS, "", false};
        if (lower == "downstairs" || lower == "down" || lower == "below")
            return {SemanticIntent::DOWNSTAIRS, "", false};
        if (lower == "outside" || lower == "out" || lower == "exterior")
            return {SemanticIntent::OUTSIDE, "", false};
        if (lower == "inside" || lower == "in" || lower == "interior")
            return {SemanticIntent::INSIDE, "", false};
        if (lower == "the back" || lower == "back room" || lower == "back")
            return {SemanticIntent::BACK, "", false};
        if (lower == "cellar" || lower == "basement" || lower == "below stairs")
            return {SemanticIntent::CELLAR, "", false};

        // === Stairs terms (exact) — map to DOWNSTAIRS (NPC goes toward stairwell area) ===
        if (lower == "stairs" || lower == "stairwell" || lower == "staircase")
            return {SemanticIntent::DOWNSTAIRS, "", false};

        // === Bedroom terms (exact) ===
        if (lower == "bedroom" || lower == "the bedroom" || lower == "my room" ||
            lower == "my bed" || lower == "bed" || lower == "room")
            return {SemanticIntent::BEDROOM, "", true};

        // === Kitchen terms (exact) ===
        if (lower == "kitchen" || lower == "the kitchen")
            return {SemanticIntent::KITCHEN, "", false};

        // === Water/bath terms (exact) — interior preferred ===
        if (lower == "bath" || lower == "the bath" || lower == "bathe" ||
            lower == "hot spring" || lower == "hot springs" || lower == "pool" ||
            lower == "the pool")
            return {SemanticIntent::WATER, "", false, HomeOwner::NONE, "", true};

        // === Water terms (exact) — exterior ===
        if (lower == "the river" || lower == "river" || lower == "the lake" ||
            lower == "lake" || lower == "stream" || lower == "the stream" ||
            lower == "the water" || lower == "water" || lower == "the shore")
            return {SemanticIntent::WATER, "", false};

        // === Water action phrases ===
        if (lower == "wash" || lower == "wash up" || lower == "go wash" ||
            lower == "clean up" || lower == "take a bath" ||
            lower.find("wash yourself") != std::string::npos ||
            lower.find("wash myself") != std::string::npos ||
            lower.find("clean yourself") != std::string::npos)
            return {SemanticIntent::WATER, "", false, HomeOwner::NONE, "", true};

        if (lower == "swim" || lower == "go swim" || lower == "go swimming" ||
            lower == "go fishing" || lower == "fishing" ||
            lower.find("go to the river") != std::string::npos ||
            lower.find("go to the lake") != std::string::npos)
            return {SemanticIntent::WATER, "", false};

        // === Home terms (exact) — NPC's own home ===
        if (lower == "home" || lower == "my home" || lower == "my house" || lower == "my place" ||
            lower == "go home" || lower == "head home" || lower == "back home")
            return {SemanticIntent::HOME, "", false, HomeOwner::NPC, ""};

        // === Home terms (exact) — player's home ===
        if (lower == "your home" || lower == "your house" || lower == "your place" ||
            lower == "the player's home" || lower == "player's home")
            return {SemanticIntent::HOME, "", false, HomeOwner::PLAYER, ""};

        // === Possessive and bare-name home patterns ===
        // Matches: "Alvor's home", "Camilla Valerius's house", "Camilla Valerius home"
        {
            std::vector<std::string> homeSuffixes = {
                "'s home", "'s house", "'s place",   // possessive: "Alvor's home"
                " home", " house", " place"          // bare: "Camilla Valerius home"
            };

            // Words that precede "home" but are NOT names — prevents
            // "go home", "back home", "head home" from being misdetected.
            std::unordered_set<std::string> nonNameWords = {
                "go", "my", "your", "head", "back", "the", "get",
                "at", "to", "a", "our", "their", "its", "this"
            };

            for (const auto& suffix : homeSuffixes) {
                auto pos = lower.find(suffix);
                if (pos == std::string::npos || pos == 0) continue;

                std::string ownerName = lower.substr(0, pos);

                // Strip leading travel prefixes
                std::vector<std::string> travelPrefixes = {"go to ", "head to ", "travel to ", "get to "};
                for (const auto& tp : travelPrefixes) {
                    if (ownerName.length() > tp.length() && ownerName.substr(0, tp.length()) == tp) {
                        ownerName = ownerName.substr(tp.length());
                        break;
                    }
                }
                ownerName = TrimStd(ownerName);
                if (ownerName.empty()) continue;

                // For bare suffixes (no apostrophe), reject single non-name words
                // to avoid "go home" → NAMED("go") or "my home" → NAMED("my").
                // Multi-word names like "camilla valerius" are always accepted.
                bool isBare = (suffix.front() == ' ');
                if (isBare && ownerName.find(' ') == std::string::npos) {
                    if (nonNameWords.count(ownerName)) continue;
                }

                return {SemanticIntent::HOME, "", false, HomeOwner::NAMED, ownerName};
            }
        }

        // === Compound phrase patterns ===

        // "out of X" / "outside of X" / "outside X" → OUTSIDE + context
        std::vector<std::string> outsidePrefixes = {"out of ", "outside of ", "outside "};
        for (const auto& prefix : outsidePrefixes) {
            if (lower.length() > prefix.length() && lower.substr(0, prefix.length()) == prefix) {
                std::string context = lower.substr(prefix.length());
                if (context == "here" || context == "this place" || context == "this building")
                    return {SemanticIntent::OUTSIDE, "", false};
                return {SemanticIntent::OUTSIDE, context, false};
            }
        }

        // "leave" / "get out" / "away" → OUTSIDE (no context)
        if (lower == "leave" || lower == "get out" || lower == "get out of here" || lower == "away")
            return {SemanticIntent::OUTSIDE, "", false};

        // "leave X" → OUTSIDE + context
        if (lower.length() > 6 && lower.substr(0, 6) == "leave ") {
            std::string context = lower.substr(6);
            if (context == "here" || context == "this place")
                return {SemanticIntent::OUTSIDE, "", false};
            return {SemanticIntent::OUTSIDE, context, false};
        }

        // === Embedded keywords (catches "go upstairs", "head downstairs", etc.) ===
        // Also detects bed context for compound phrases like "upstairs bedroom"
        if (lower.find("upstairs") != std::string::npos || lower.find("upper floor") != std::string::npos)
            return {SemanticIntent::UPSTAIRS, "", hasBedContext()};
        if (lower.find("downstairs") != std::string::npos || lower.find("basement") != std::string::npos ||
            lower.find("cellar") != std::string::npos || lower.find("stairwell") != std::string::npos ||
            lower.find("staircase") != std::string::npos || lower.find("stairs") != std::string::npos)
            return {SemanticIntent::DOWNSTAIRS, "", hasBedContext()};
        if (lower.find("outside") != std::string::npos || lower.find("exterior") != std::string::npos)
            return {SemanticIntent::OUTSIDE, "", false};

        // === Bedroom embedded (catches "the bedroom", "go to bed")
        if (lower.find("bedroom") != std::string::npos || lower.find("my bed") != std::string::npos)
            return {SemanticIntent::BEDROOM, "", true};

        // === Kitchen embedded (catches "the kitchen", "go to the kitchen")
        if (lower.find("kitchen") != std::string::npos)
            return {SemanticIntent::KITCHEN, "", false};

        // === Water embedded (catches "head to the river", "find a river") ===
        if (lower.find("river") != std::string::npos || lower.find("lake") != std::string::npos ||
            lower.find("swim") != std::string::npos || lower.find("fishing") != std::string::npos)
            return {SemanticIntent::WATER, "", false};
        if (lower.find("bath") != std::string::npos || lower.find("hot spring") != std::string::npos)
            return {SemanticIntent::WATER, "", false, HomeOwner::NONE, "", true};

        // === Home embedded — catch phrases like "go to my home", "head to your place" ===
        if (lower.find("my home") != std::string::npos || lower.find("my house") != std::string::npos ||
            lower.find("my place") != std::string::npos)
            return {SemanticIntent::HOME, "", false, HomeOwner::NPC, ""};
        if (lower.find("your home") != std::string::npos || lower.find("your house") != std::string::npos ||
            lower.find("your place") != std::string::npos)
            return {SemanticIntent::HOME, "", false, HomeOwner::PLAYER, ""};
        // Bare "home" as embedded keyword (e.g., "head home now") — NPC's own home
        // Must be a standalone word — not part of a compound like "breezehome" or "homebrew"
        {
            auto homePos = lower.find("home");
            if (homePos != std::string::npos) {
                bool wordStart = (homePos == 0 || !std::isalpha(static_cast<unsigned char>(lower[homePos - 1])));
                bool wordEnd = (homePos + 4 >= lower.length() || !std::isalpha(static_cast<unsigned char>(lower[homePos + 4])));
                if (wordStart && wordEnd)
                    return {SemanticIntent::HOME, "", false, HomeOwner::NPC, ""};
            }
        }

        return {SemanticIntent::NONE, "", false};
    }
}
