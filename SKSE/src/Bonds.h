#pragma once

// IntelEngine: how somebody stands with the player when no relationship mod keeps it (BondBridge.cpp reads the game).
// Pure logic: no game types, no engine calls, so SKSE/tests/BondsCheck.cpp can run it.
//
// The user (2026-11-21): "we have no reliable fallback for bonds if the relationship mod is missing like how many
// memory records you have with a specific npc and judging those memories if they ever hinted to friendship or a
// flirt". Without SkyrimNet Relationships the mod knew the game's own relationship rank, a marriage, and whether
// somebody once followed the player: nothing of a friendship that grew in talk, of a flirt, of a no.
//
// So, where no relationship mod is installed:
//  - how often the two have had to do with each other is counted: by SkyrimNet (MemoryDB::TimesWithPlayer), and by
//    IntelEngine itself, which counts every line somebody says to the player (`times`: SkyrimNet's count came back
//    empty in a game whose database held the lines, 2026-11-21). The larger counts. A count says "acquainted" and no
//    more: a quarrel counts as much as a kindness;
//  - what their dealings came to is read: the dialogue model is given what that person remembers of the player and
//    their last talk together, and names one of a few standings and whether anything of the heart is in it
//    (decisions/intel_bond.prompt). It chooses among named answers; no words are searched for, so it works in any
//    language the game is played in;
//  - one reading is not believed on its word: the same talk, read three times, came out friend, acquaintance, friend
//    (played 2026-11-21). A reading is taken when the reader is sure of it (the weight it gives its own answer), or
//    when the reading before it said the same; until then it is only noted, and how they stand stays as it was;
//  - the answer is kept with the save (record 'IEBN') and read again only when much more has passed between the two.
// Nothing of anybody's state is written: this is IntelEngine's own note of what it read. Who may be part of a story
// of the heart stays Hearts.h's to say (never a child, never kin, never against a no).
//
// Beside a relationship mod (SkyrimNet Relationships), person by person (See, below; 2026-11-21):
//  - whoever that mod watches: its word, and nothing is read here. It weighs every exchange as it happens, by that
//    person's own character, and nothing of the heart begins there unless they raise it and the player answers; a
//    reading of what was said afterwards is the poorer witness;
//  - whoever it does not watch (it watches followers, and whom the player chooses): the reading says friend,
//    confidant, cold or foe, and nothing of the heart. An innkeeper the player has drunk with for a month is a friend
//    in both games; a courtship is that mod's to begin, by its own rules, and is not begun here behind its back;
//  - at the moment it takes somebody on it knows nothing of them yet (it starts everybody as a stranger): a friend of
//    a month would be a stranger overnight. What was read before stays, as a floor for friendship and for ill will,
//    until that mod's own tier has caught up, or goes down, or it reports ill will itself: from then its word alone.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

namespace IntelEngine::Bonds {

    // The same ladder, with the same numbers, as Hearts::Tier up to Lover. A reading never says more than that.
    enum Standing : std::uint8_t { Stranger = 0, Acquaintance = 1, Friend = 2, Confidant = 3, Lover = 4 };
    enum Sour : std::uint8_t { NotSour = 0, Cold = 1, Foe = 2 };
    enum Answer : std::int8_t { Declined = -1, Unanswered = 0, Welcomed = 1 };

    struct Bond {
        std::uint8_t standing = Stranger;
        std::uint8_t sour = NotSour;        // they dislike the player (Cold) or wish them harm (Foe)
        bool drawn = false;                 // something of the heart, on their side
        std::int8_t answer = Unanswered;    // the player's answer to it
        float judgedAt = -1.0f;             // game days of the reading; below 0: never read
        float declinedAt = -1.0f;           // game days at which the player was first read to have turned them down
        std::int32_t eventsAt = 0;          // how often the two had had to do with each other at the reading
        std::int32_t times = 0;             // lines they have said to the player, counted here since this was installed
        std::uint8_t noted = 0xFF;          // what the last reading said and was not yet believed (Code below); 0xFF: nothing
        bool handed = false;                // a relationship mod watches them and its own word has caught up: the reading rests
        std::uint8_t theirsSeen = 0;        // the highest tier that mod has shown for them since it took them on
    };
    constexpr std::uint8_t kNothingNoted = 0xFF;

    constexpr int kFirstAt = 6;             // read for the first time once this much has passed between the two
    constexpr int kAgainAfter = 8;          // and again when this much more has
    constexpr int kConfirmAfter = 3;        // sooner when the last reading was noted and not yet believed
    constexpr float kAgainHours = 2.0f;     // game hours at least between two readings of one person
    constexpr float kConfirmHours = 0.5f;
    constexpr float kSure = 0.75f;          // the weight the reader gives its own answer from which one reading is enough
    constexpr float kPauseSeconds = 45.0f;  // real seconds between any two readings: it costs the player a model call
    constexpr int kMemories = 24;           // how many memories and how many exchanges of talk a reading is given
    constexpr int kExchanges = 12;

    inline constexpr bool Judged(const Bond& b) { return b.judgedAt >= 0.0f; }

    // Is it time to read what passed between them (again)?
    inline constexpr bool Due(const Bond& b, int eventsNow, float nowDays) {
        if (eventsNow < kFirstAt) return false;
        if (!Judged(b)) return true;
        if (nowDays < b.judgedAt) return true;   // the clock was set back: what was read then may not have happened
        const bool unsettled = b.noted != kNothingNoted;
        return eventsNow - b.eventsAt >= (unsettled ? kConfirmAfter : kAgainAfter) &&
               (nowDays - b.judgedAt) * 24.0f >= (unsettled ? kConfirmHours : kAgainHours);
    }

    // Before anything is read the count says this much and no more.
    inline constexpr std::uint8_t ByCount(int events) { return events >= kFirstAt ? Acquaintance : Stranger; }

    // Nothing to read (no memory, no talk on record): acquainted by the count, and looked at again later.
    inline constexpr Bond NothingToRead(Bond before, int eventsNow, float nowDays) {
        if (!Judged(before)) before.standing = ByCount(eventsNow);
        before.judgedAt = nowDays;
        before.eventsAt = eventsNow;
        return before;
    }

    // What a reading said, in one number, to compare it with the next one's.
    inline constexpr std::uint8_t Code(const Bond& b) {
        const std::uint8_t standing = b.sour == Foe ? 6 : b.sour == Cold ? 5 : b.standing;
        return static_cast<std::uint8_t>(standing | (b.drawn ? 8 : 0) | (static_cast<std::uint8_t>(b.answer + 1) << 4));
    }

    enum class Reading : std::uint8_t {
        Unreadable = 0,   // the answer was none of the names: nothing changed
        Taken = 1,        // how they stand is what this reading said (it may be what it was)
        Noted = 2,        // it said something else and was not sure: kept in mind, believed if the next one says the same
    };

    // What the reader answered: one named choice for each of the three questions (the names are the contract with
    // decisions/intel_bond.prompt and intel_bond_chat.prompt), and how sure it was (the weight it gave its answers, 0
    // to 1; the chat form gives none: 0). Unreadable, and nothing changed, when the first answer is none of the names:
    // no reading is better than a guessed one.
    // `heartAsked` false: the two questions of the heart were not put (a relationship mod is installed and keeps
    // that): what was read of the heart before is left as it was, and "lover", which was not offered, is the closest
    // friend the question knows.
    inline Reading Read(std::string_view standing, std::string_view heart, std::string_view answer, float sure, Bond& b, int eventsNow,
                        float nowDays, bool heartAsked = true) {
        Bond next = b;
        next.sour = NotSour;
        if (standing == "stranger") next.standing = Stranger;
        else if (standing == "acquaintance") next.standing = Acquaintance;
        else if (standing == "friend") next.standing = Friend;
        else if (standing == "confidant") next.standing = Confidant;
        else if (standing == "lover") next.standing = heartAsked ? Lover : Confidant;
        else if (standing == "cold") {
            next.standing = Acquaintance;
            next.sour = Cold;
        } else if (standing == "foe") {
            next.standing = Acquaintance;
            next.sour = Foe;
        } else {
            return Reading::Unreadable;
        }
        // Lovers by the word of both: the heart is in it and welcome, whatever the other two answers say.
        const bool lovers = next.standing == Lover;
        const bool drawn = lovers || heart == "drawn";
        const bool welcomed = lovers || (drawn && answer == "welcomed");
        if (!heartAsked) {
            // Not asked: nothing of the heart is changed by this reading.
        } else if (b.answer == Declined && !welcomed) {
            // A no stands until the player is plainly read to have taken it back: a later reading that finds nothing
            // of the heart, or finds it unanswered, does not wipe it out (the talk it was said in has scrolled away).
            next.drawn = true;
            next.answer = Declined;
        } else if (drawn) {
            next.drawn = true;
            next.answer = welcomed ? Welcomed : answer == "declined" ? Declined : Unanswered;
            if (next.answer == Declined && b.answer != Declined) next.declinedAt = nowDays;
            if (next.answer != Declined) next.declinedAt = -1.0f;
        } else {
            next.drawn = false;
            next.answer = Unanswered;
            next.declinedAt = -1.0f;
        }
        // Somebody who wishes the player harm is no friend, whatever else was read.
        if (next.sour != NotSour && next.standing > Acquaintance) next.standing = Acquaintance;
        next.judgedAt = nowDays;
        next.eventsAt = eventsNow;
        next.noted = kNothingNoted;
        const std::uint8_t said = Code(next);
        if (Judged(b) && said == Code(b)) {
            b = next;                    // as it was
            return Reading::Taken;
        }
        if (sure >= kSure || said == b.noted) {
            b = next;                    // sure of it, or said twice running
            return Reading::Taken;
        }
        // Said once, and not sure: how they stand stays as it was (acquainted by the count when nothing was known yet).
        if (!Judged(b)) b.standing = ByCount(eventsNow);
        b.judgedAt = nowDays;
        b.eventsAt = eventsNow;
        b.noted = said;
        return Reading::Noted;
    }

    // Close enough to count as bonded (who may come from afar, who runs to the player): a confidant or more.
    inline constexpr bool Close(const Bond& b) { return Judged(b) && b.sour == NotSour && b.standing >= Confidant; }
    // They have had to do with each other, for good or ill.
    inline constexpr bool Knows(const Bond& b) { return Judged(b) && b.standing >= Acquaintance; }
    // Somebody who dislikes the player does not bring their trouble to them.
    inline constexpr bool WouldAsk(const Bond& b) { return b.sour == NotSour; }

    // ------------------------------------------------------------------ beside a relationship mod

    // What a relationship mod installed beside this one says of the same person (SkyrimNet Relationships: its bond
    // faction's rank and its enmity faction's, which its README invites other mods to read).
    struct Theirs {
        bool installed = false;   // the mod is in the game
        bool tracked = false;     // and watches this person
        std::uint8_t tier = 0;    // its tier for them: 0 stranger, 1 acquaintance, 2 friend, 3 confidant, 4 lover, 5 beloved
        std::uint8_t ill = 0;     // its ill will: 0 none, 1 disliked, 2 resentful, 3 enemy
    };

    // What the reading says of somebody where it counts (who is close, who knows the player, who would not ask them
    // for help, a story's picture of the two): the part of it that is this mod's to say for that person.
    struct View {
        std::uint8_t standing = Stranger;   // a floor: the game's rank, or the relationship mod's tier, may say more
        std::uint8_t sour = NotSour;
        bool known = false;                 // they have had to do with each other
        bool heart = false;                 // what was read of the heart counts too (no relationship mod keeps it)
        bool theirs = false;                // a relationship mod watches them: it is not read here
    };

    inline constexpr std::uint8_t SourOf(std::uint8_t ill) { return ill >= 3 ? Foe : ill >= 1 ? Cold : NotSour; }

    // The rule, person by person (the top of this file says why). It keeps two notes in the bond of somebody a
    // relationship mod has taken on: the highest tier that mod has shown, and whether its word has caught up.
    inline constexpr View See(Bond& b, const Theirs& t) {
        View v;
        const std::uint8_t friendship = b.standing < Confidant ? b.standing : static_cast<std::uint8_t>(Confidant);
        if (!t.installed) {
            // Nobody else keeps it: all that was read counts, the heart too.
            v.heart = true;
            if (Judged(b)) {
                v.standing = b.standing;
                v.sour = b.sour;
                v.known = Knows(b);
            }
            return v;
        }
        if (!t.tracked) {
            // Not one of those the mod watches (or no longer): friendship and ill will as read, nothing of the heart.
            b.handed = false;
            b.theirsSeen = 0;
            if (Judged(b)) {
                v.standing = friendship;
                v.sour = b.sour;
                v.known = Knows(b);
            }
            return v;
        }
        // Watched by the mod: its word. Its ill will is told here (its tier is read where tiers are read).
        v.theirs = true;
        v.sour = SourOf(t.ill);
        v.known = t.tier >= Acquaintance || t.ill > 0;
        if (!Judged(b) || b.handed) return v;
        // It took them on after they were read here, and starts everybody as a stranger: what was read stays until
        // its own word says as much (for ill will: until it finds them on speaking terms), or its tier goes down
        // (something happened that it saw and the reading did not), or it reports ill will itself.
        const bool readIll = b.sour != NotSour;
        const bool caughtUp = readIll ? t.tier >= Acquaintance : t.tier >= friendship;
        if (caughtUp || t.tier < b.theirsSeen || t.ill > 0) {
            b.handed = true;
            return v;
        }
        if (t.tier > b.theirsSeen) b.theirsSeen = t.tier;
        v.standing = friendship;
        v.sour = b.sour;
        v.known = v.known || Knows(b);
        return v;
    }

    inline const char* Word(const Bond& b) {
        if (!Judged(b)) return "not read";
        if (b.sour == Foe) return "a foe";
        if (b.sour == Cold) return "cold";
        switch (b.standing) {
            case Acquaintance: return "an acquaintance";
            case Friend: return "a friend";
            case Confidant: return "a confidant";
            case Lover: return "a lover";
            default: return "a stranger";
        }
    }

    // Who is read next, of those it is time for: whoever is with the player now, then people of the town the player is
    // in, then whoever has had most to do with the player since their last reading.
    struct Candidate {
        std::uint32_t id = 0;
        int events = 0;
        int since = 0;       // events since their last reading (all of them when never read)
        bool with = false;   // with the player now
        bool here = false;   // of the town the player is in
    };
    inline constexpr bool Before(const Candidate& a, const Candidate& b) {
        if (a.with != b.with) return a.with;
        if (a.here != b.here) return a.here;
        if (a.since != b.since) return a.since > b.since;
        return a.id < b.id;
    }

    // ------------------------------------------------------------------ kept with the save (record 'IEBN')

    inline constexpr std::uint32_t kRecordType = 'IEBN';
    inline constexpr std::uint32_t kRecordVersion = 2;   // 2: handed, theirsSeen (a record of version 1 is still read)
    inline constexpr std::size_t kMost = 4096;

    using Book = std::map<std::uint32_t, Bond>;

    inline std::vector<std::uint8_t> Serialize(const Book& book) {
        std::vector<std::uint8_t> out;
        const auto put32 = [&out](std::uint32_t v) {
            for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        };
        const auto putFloat = [&put32](float f) {
            std::uint32_t v = 0;
            static_assert(sizeof(v) == sizeof(f));
            const auto* p = reinterpret_cast<const unsigned char*>(&f);
            for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
            put32(v);
        };
        put32(static_cast<std::uint32_t>(book.size()));
        for (const auto& [id, b] : book) {
            put32(id);
            out.push_back(b.standing);
            out.push_back(b.sour);
            out.push_back(b.drawn ? 1 : 0);
            out.push_back(static_cast<std::uint8_t>(b.answer));
            putFloat(b.judgedAt);
            putFloat(b.declinedAt);
            put32(static_cast<std::uint32_t>(b.eventsAt));
            put32(static_cast<std::uint32_t>(b.times));
            out.push_back(b.noted);
            out.push_back(b.handed ? 1 : 0);
            out.push_back(b.theirsSeen);
        }
        return out;
    }

    inline bool Deserialize(const std::vector<std::uint8_t>& bytes, Book& book, std::uint32_t version = kRecordVersion) {
        if (version < 1 || version > kRecordVersion) return false;
        std::size_t at = 0;
        const auto get32 = [&bytes, &at](std::uint32_t& v) {
            if (at + 4 > bytes.size()) return false;
            v = 0;
            for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(bytes[at + i]) << (8 * i);
            at += 4;
            return true;
        };
        const auto getFloat = [&get32](float& f) {
            std::uint32_t v = 0;
            if (!get32(v)) return false;
            auto* p = reinterpret_cast<unsigned char*>(&f);
            for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
            return true;
        };
        Book loaded;
        std::uint32_t count = 0;
        if (!get32(count) || count > kMost) return false;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t id = 0, events = 0, times = 0;
            Bond b;
            if (!get32(id) || at + 4 > bytes.size()) return false;
            b.standing = bytes[at];
            b.sour = bytes[at + 1];
            b.drawn = bytes[at + 2] != 0;
            b.answer = static_cast<std::int8_t>(bytes[at + 3]);
            at += 4;
            if (!getFloat(b.judgedAt) || !getFloat(b.declinedAt) || !get32(events) || !get32(times) || at + 1 > bytes.size()) return false;
            b.eventsAt = static_cast<std::int32_t>(events);
            b.times = static_cast<std::int32_t>(times);
            b.noted = bytes[at++];
            if (version >= 2) {
                if (at + 2 > bytes.size()) return false;
                b.handed = bytes[at] != 0;
                b.theirsSeen = bytes[at + 1];
                at += 2;
            }
            // What cannot be: a record written by something else, or damaged.
            if (b.standing > Lover || b.sour > Foe || b.answer < Declined || b.answer > Welcomed || b.theirsSeen > 5) return false;
            loaded.emplace(id, b);
        }
        book = std::move(loaded);
        return true;
    }

}
