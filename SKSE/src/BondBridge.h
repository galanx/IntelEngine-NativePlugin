#pragma once

// IntelEngine: how somebody stands with the player, read from what they remember of the player, for a game without a
// relationship mod (Bonds.h has the rules and the user's words). SkyrimNet counts how often the two have had to do
// with each other and keeps the memories and the talk; the dialogue model reads them and names a standing; the answer
// is kept with the save (record 'IEBN'). Nothing of anybody's state is written.

#include "Bonds.h"

#include <atomic>
#include <mutex>
#include <string>

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

        /** Does the reading count: no relationship mod keeps bonds (SkyrimNet Relationships is not installed), or a
         *  test said to go on as if. Any thread once the game's data is loaded. */
        bool Used() const;
        /** What was last read of this person (never read: `judgedAt` below 0). Any thread. */
        Bonds::Bond Of(std::uint32_t actor) const;
        /** Somebody said a line to the player (DialogueTracker): counted, with or without a relationship mod. Any thread. */
        void Heard(std::uint32_t speaker);
        /** How often the two have had to do with each other: SkyrimNet's count or IntelEngine's own, whichever is
         *  larger. Any thread. */
        int Times(std::uint32_t actor) const;

        /** Every look of the living world (some ten seconds), on the game's thread: when it is time for somebody, their
         *  memories are fetched on the worker and put to the model. One reading at a time, with a pause between. */
        void Tick();
        /** Test hook: read this person now, whatever the count, the pause and the mods installed. False when a
         *  reading is under way or they are nobody to read. Game thread. */
        bool JudgeNow(RE::Actor* who);
        /** Test hook: go on as if no relationship mod were installed (this session only). */
        void Pretend(bool noRelationshipMod);
        /** One line for the log and for tests: "a friend, drawn to the player, unanswered (read at day 12.4, 23 times together)". */
        std::string Describe(std::uint32_t actor) const;

        void Save(SKSE::SerializationInterface* intfc) const;
        void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
        void Revert();

    private:
        BondBridge() = default;

        bool Start(RE::Actor* who, int events, bool forced);
        void Finish(std::uint32_t actor, const std::string& standing, const std::string& heart, const std::string& answer, float sure,
                    int events, float nowDays, int memories, bool talk, const std::string& failure);

        mutable std::mutex m_mutex;
        Bonds::Book m_book;
        std::atomic<bool> m_busy{false};
        std::atomic<bool> m_pretend{false};
        std::atomic<bool> m_counting{false};   // the counts are being read on the worker
        double m_lastRead = -1.0e9;            // real seconds (game thread)
        double m_lastCount = -1.0e9;
    };

}
