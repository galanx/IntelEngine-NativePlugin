#pragma once

// IntelEngine: how somebody stands with the player, read from what they remember of the player (Bonds.h has the rules
// and the user's words). SkyrimNet counts how often the two have had to do with each other and keeps the memories and
// the talk; the dialogue model reads them and names a standing; the answer is kept with the save (record 'IEBN').
// Nothing of anybody's state is written.
//
// Beside SkyrimNet Relationships it is decided person by person (Bonds::See): whoever that mod watches is its to speak
// for and is not read here; everybody else is read for friendship and ill will, and never for anything of the heart.

#include "Bonds.h"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

namespace RE {
    class Actor;
}
namespace SKSE {
    class SerializationInterface;
}

namespace IntelEngine {

    class BondBridge {
    public:
        static BondBridge* GetSingleton();

        /** What a relationship mod installed beside this one says of this person: whether it is there at all, whether
         *  it watches them, its tier and its ill will (SkyrimNet Relationships' two factions). Nothing installed, or
         *  a test said to go on as if: all false. Game thread (it reads what they belong to). */
        Bonds::Theirs TheirsOf(RE::Actor* who) const;
        /** What counts of the reading for this person, by the rule of Bonds::See: all of it without a relationship
         *  mod; friendship and ill will for whoever that mod does not watch; for whoever it watches its ill will, and
         *  what was read before it took them on as a floor until its own word has caught up. Game thread. */
        Bonds::View Seen(RE::Actor* who);
        /** What was last read of this person, as it was read (never read: `judgedAt` below 0). What of it counts is
         *  Seen's to say. Any thread. */
        Bonds::Bond Of(std::uint32_t actor) const;
        /** Somebody said a line to the player (DialogueTracker): counted, with or without a relationship mod. Any thread. */
        void Heard(std::uint32_t speaker);
        /** How often the two have had to do with each other: SkyrimNet's count or IntelEngine's own, whichever is
         *  larger. Any thread. */
        int Times(std::uint32_t actor) const;

        /** Every look of the living world (some ten seconds), on the game's thread: when it is time for somebody, their
         *  memories are fetched on the worker and put to the model. One reading at a time, with a pause between. */
        void Tick();
        /** Test hook: read this person now, whatever the count and the pause, and whether or not a relationship mod
         *  watches them. False when a reading is under way or they are nobody to read. Game thread. */
        bool JudgeNow(RE::Actor* who);
        /** Test hook: go on as if no relationship mod were installed (this session only). */
        void Pretend(bool noRelationshipMod);
        /** One line for the log and for tests: "a friend, drawn to the player, unanswered (read at day 12.4, 23 times together)". */
        std::string Describe(std::uint32_t actor) const;

        /** For the dashboard's World page: everybody whose standing was read here and counts (not those a
         *  relationship mod speaks for alone), closest first, forty at the most: [{name, word, standing, sour, heart,
         *  source}]. `heart` is what was read of the heart (shown only with "show everything"). Game thread. */
        std::string DashboardJson() const;

        void Save(SKSE::SerializationInterface* intfc) const;
        void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
        void Revert();

    private:
        BondBridge() = default;

        bool Start(RE::Actor* who, int events, bool forced);
        void Finish(std::uint32_t actor, const std::string& standing, const std::string& heart, const std::string& answer, float sure,
                    int events, float nowDays, int memories, bool talk, bool heartAsked, const std::string& failure);

        mutable std::mutex m_mutex;
        Bonds::Book m_book;
        std::atomic<bool> m_busy{false};
        std::atomic<bool> m_pretend{false};
        std::atomic<bool> m_counting{false};   // the counts are being read on the worker
        double m_lastRead = -1.0e9;            // real seconds (game thread)
        double m_lastCount = -1.0e9;
        // Whoever a relationship mod was found to watch is not asked about again before this (real seconds; game thread).
        std::unordered_map<std::uint32_t, double> m_theirsUntil;
    };

}
