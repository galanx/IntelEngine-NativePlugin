#pragma once

// IntelEngine: what stands between the player and someone, read from the game (docs/STORY_ARCS.md, "Hearts").
// Pure logic: no game types, no engine calls, so SKSE/tests/HeartsCheck.cpp can run it.
//
// The user (2026-11-19): stories should lead to "pursuing romance, marriage and having kids (if fertility mode
// installed, preferably with ostim), affairs (if ostim is installed)". IntelEngine owns none of that state. A
// relationship mod does, or the game itself; all of it is read softly (HeartBridge.cpp) and nothing of it is written.
// What IntelEngine adds is the story: who may be part of one is decided here, from facts, never by the model.
//
// Hard rules, whatever is installed and whatever anyone asks for:
//  - nobody who is a child, or looks or sounds like one, is ever part of a story of the heart;
//  - nobody of the player's own family;
//  - nobody the player has turned down, while that stands (the user, 2026-11-20: "a failed courtship might still
//    become something if the player and the npc find the sparks": after a long while, and only once the two have
//    grown closer than they were, a story may leave the player room to show that something has changed; nobody
//    ever presses them);
//  - the story is told in words and ends at the door: what happens behind it is another mod's to tell, or nobody's.

#include <cstdint>
#include <string>

namespace IntelEngine::Hearts {

    // How close someone stands to the player. With SkyrimNet Relationships it is that mod's own tier; without, the
    // game's relationship rank mapped onto the same ladder.
    enum Tier : std::uint8_t { Stranger = 0, Acquaintance = 1, Friend = 2, Confidant = 3, Lover = 4, Beloved = 5 };

    enum class Wed : std::uint8_t {
        None = 0,
        Engaged = 1,
        Married = 2,
        Ended = 3,     // broken off, divorced, separated
        Widowed = 4,
    };

    // What is true of them and the player right now.
    struct Heart {
        std::uint8_t tier = Stranger;
        bool tracked = false;        // a relationship mod keeps their bond (else it is the game's own rank)
        bool sparked = false;        // there is something of the heart between the two, on their side
        std::int8_t stance = 0;      // the player's answer to it: -1 turned down, 0 unanswered, 1 welcomed
        float refusedDays = -1.0f;   // game days since the player was first seen to have turned them down; below 0 = not known
        Wed wed = Wed::None;         // with the player
        bool wedElsewhere = false;   // married to somebody else
        std::uint8_t withChild = 0;  // 0 no, 1 to 3 the trimester
        bool childIsPlayers = false;
        std::uint8_t children = 0;   // children they have with the player
    };

    // Who they are, as far as it matters here.
    struct Who {
        bool person = false;     // a named, unique adult of the world (not a creature, not a nameless guard)
        bool childlike = false;  // a child by race, by flag, by voice or by size
        bool kin = false;        // of the player's own family
    };

    // What a story between the two would be about.
    enum class Lean : std::uint8_t {
        None = 0,
        Courtship = 1,  // something has begun between them and wants to be found out
        Together = 2,   // lovers, betrothed or wed: a story of the life they share
        Family = 3,     // wed, with a child coming or children: a story of the household
        Affair = 4,     // they are married to another, and the player has welcomed them all the same
    };

    struct Rules {
        bool stories = true;   // stories of the heart are told at all (off at "Classic")
        bool affairs = false;  // only where an intimacy mod is installed: the user's own condition
    };

    inline bool MayHaveAHeartStory(const Who& who) { return who.person && !who.childlike && !who.kin; }

    // A no is not for ever, and is never worn down either: it stands for a season, and after that only where the two
    // have since grown close (closer than a first courtship asks for) may a story leave the door ajar again.
    constexpr float kRefusalRestDays = 30.0f;

    inline bool SecondChance(const Heart& h) {
        return h.stance < 0 && h.sparked && h.refusedDays >= kRefusalRestDays && h.tier >= Confidant && h.wed == Wed::None && !h.wedElsewhere;
    }

    inline Lean LeanOf(const Who& who, const Heart& h, const Rules& rules) {
        if (!rules.stories || !MayHaveAHeartStory(who)) return Lean::None;
        if (h.stance < 0) return SecondChance(h) ? Lean::Courtship : Lean::None;   // turned down is turned down, while it stands
        if (h.wed == Wed::Married) return (h.children > 0 || (h.withChild && h.childIsPlayers)) ? Lean::Family : Lean::Together;
        if (h.wed == Wed::Engaged) return Lean::Together;
        if (h.wed == Wed::Ended || h.wed == Wed::Widowed) return Lean::None;   // what ended is not reopened by a story
        if (h.wedElsewhere) {
            // Only where the player has plainly welcomed it, and never as a first step.
            return rules.affairs && h.sparked && h.stance > 0 && h.tier >= Confidant ? Lean::Affair : Lean::None;
        }
        if (h.tier >= Lover) return Lean::Together;
        if (h.sparked && h.tier >= Friend) return Lean::Courtship;
        return Lean::None;
    }

    // Somebody drawn to the player who has never said so and would not come up to them: the story of a shy admirer,
    // in which somebody close to them comes in their stead. Only while a courtship could begin at all (LeanOf: never
    // a child, never kin, never somebody wed, a friend at least) and the player has not answered either way: answered
    // yes there is no secret left, and a no stands. The bold ask for themselves (`confidence`: the game's own scale,
    // 0 cowardly, 1 cautious, 2 average, 3 brave, 4 foolhardy).
    inline bool ShyAdmirer(const Who& who, const Heart& h, const Rules& rules, int confidence) {
        return LeanOf(who, h, rules) == Lean::Courtship && h.stance == 0 && confidence <= 2;
    }

    inline const char* LeanWord(Lean lean) {
        switch (lean) {
            case Lean::Courtship: return "courtship";
            case Lean::Together: return "a life shared";
            case Lean::Family: return "the household";
            case Lean::Affair: return "an affair";
            default: return "";
        }
    }

    inline const char* TierWord(std::uint8_t tier) {
        switch (tier) {
            case Acquaintance: return "an acquaintance";
            case Friend: return "a friend";
            case Confidant: return "a close confidant";
            case Lover: return "a lover";
            case Beloved: return "deeply in love";
            default: return "a stranger";
        }
    }

    // What the author is told about them and the player: only what is true, in plain words. Empty when there is
    // nothing to tell (a stranger, an acquaintance with no spark).
    inline std::string Words(const Heart& h, const std::string& player) {
        std::string out;
        const auto add = [&](const std::string& s) { out += (out.empty() ? "" : " ") + s; };
        if (h.wed == Wed::Married) add("Married to " + player + ".");
        else if (h.wed == Wed::Engaged) add("Betrothed to " + player + ": a wedding is ahead.");
        else if (h.wed == Wed::Ended) add("Was once bound to " + player + "; it ended.");
        else if (h.wed == Wed::Widowed) add("Widowed.");
        if (h.wed != Wed::Married && h.wed != Wed::Engaged) {
            if (h.tier >= Lover) add(std::string("To ") + player + ": " + TierWord(h.tier) + ".");
            else if (h.tier >= Friend) add(std::string("To ") + player + ": " + TierWord(h.tier) + (h.sparked ? ", and there is more than friendship in it." : "."));
        }
        if (h.sparked && h.stance < 0 && SecondChance(h)) {
            add(player + " turned them down once. That was long ago, and the two have grown close since. They have never pressed it and never "
                "will; whether anything has changed is " + player + "'s alone to show.");
        } else if (h.sparked && h.stance < 0) add(player + " has turned them down, kindly or not. That is settled and is not reopened.");
        else if (h.sparked && h.stance == 0 && h.tier < Lover && h.wed == Wed::None) add(player + " has not yet answered what they feel.");
        if (h.wedElsewhere) add("Married to someone else.");
        if (h.withChild) {
            add(std::string("With child (") + (h.withChild == 1 ? "early" : h.withChild == 2 ? "halfway" : "near her time") + ")" +
                (h.childIsPlayers ? "; the child is " + player + "'s." : "."));
        }
        if (h.children == 1) add("Has a child with " + player + ".");
        else if (h.children > 1) add("Has " + std::to_string(static_cast<int>(h.children)) + " children with " + player + ".");
        return out;
    }

    // The game's own relationship rank with the player (-4 archnemesis .. 4 lover) on the same ladder, for a game
    // without a relationship mod. A candidate for marriage as the game itself defines one, on friendly terms, is the
    // nearest thing to a spark the game knows.
    inline Heart FromTheGame(int rank, bool marriageCandidate, bool wedToPlayer, bool wedElsewhere) {
        Heart h;
        h.tier = rank >= 4 ? Lover : rank == 3 ? Confidant : rank == 2 ? Confidant : rank == 1 ? Friend : Stranger;
        h.wed = wedToPlayer ? Wed::Married : Wed::None;
        h.wedElsewhere = wedElsewhere && !wedToPlayer;
        h.sparked = !wedToPlayer && !h.wedElsewhere && marriageCandidate && rank >= 1;
        return h;
    }

    // Growing closer or apart through a story changes the game's own rank by one step, within friendship: a story
    // never makes anyone a lover or a foe by itself (the first belongs to the player's own choices and to whichever
    // mod keeps hearts; the second to deeds).
    inline int RankAfter(int rank, bool closer) {
        if (closer) return rank >= 3 ? rank : rank + 1;
        return rank <= 0 ? rank : rank - 1;
    }

}
