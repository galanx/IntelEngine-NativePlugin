#pragma once

#include "Rumors.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace IntelEngine {

    // The talk of the day, in the game (Rumors.h has the rules): kept with the save, written into SkyrimNet for every
    // hold a rumor has reached, told by the people who hear everything, and brought up by a publican when the player
    // walks in. The hold's long memory (the entries the quests, stories and lives write for themselves) stays theirs;
    // what they write is noted here as well, so that it is talked of, travels and is forgotten.
    class RumorBridge {
    public:
        static RumorBridge* GetSingleton();

        /** Something new is going round the hold `location` lies in (a town, a place, the hold itself). `ownEntry`:
         *  write the hold's SkyrimNet entry for it here (the caller wrote none of its own). Returns its id; 0 when the
         *  place lies in no hold of the list or there is nothing to say. Any thread. */
        std::uint32_t Note(Rumors::Kind kind, const std::string& text, RE::FormID location, RE::FormID about, RE::FormID lead,
                           Rumors::Owner owner, std::uint32_t ownerId, float weight, bool ownEntry);
        /** News of the world (politics): it begins in the hold a city or hold of this name lies in ("Solitude",
         *  "Haafingar"), or where the player last was when the name says nothing. Any thread. */
        std::uint32_t NoteNews(const std::string& text, const std::string& where, float weight, std::uint32_t ownerId);
        /** The hold a name means: a hold's own name ("The Reach", "Whiterun Hold") or its capital's ("Markarth").
         *  Rumors::NoHold for anything else. */
        static std::uint8_t HoldByName(const std::string& where);
        /** The hold the player is in, by the game's own records (never by a name): Rumors::NoHold outside the holds.
         *  Main thread. */
        static std::uint8_t HoldOfPlayer();
        /** A hold's name as the game shows it, for a hold or a capital named in English in a data file ("Solitude"
         *  gives "Haafingar" in the game's language); empty when the name means no hold. */
        static std::string HoldShown(const std::string& where);
        /** Everything of one owner goes (a story given up, taken on or ended; a quest closed). Any thread. */
        void Withdraw(Rumors::Owner owner, std::uint32_t ownerId, Rumors::Kind only = Rumors::Kind{});
        /** The owners (ids) that have a rumor of this kind out: so their keeper can take it back when its time is over. */
        std::vector<std::uint32_t> Owners(Rumors::Owner owner, Rumors::Kind kind) const;

        /** Time passes: rumors travel and are forgotten. Called from a look that runs anyway; does nothing more often
         *  than once a game hour. The game's thread. */
        void Tick();

        /** What this person has heard and could pass on (the prompt block get_intelengine_rumors): several things for
         *  those who hear everything (innkeepers, their servers, bards, merchants), the talk of the day for anyone
         *  else, nothing where nothing is going round. Any thread (SkyrimNet's). */
        std::string Lines(RE::Actor* actor);
        /** For the author of a town's story: what goes round the hold the player is in now, the weightiest first,
         *  `most` at the most. Somebody's trouble is left out (that is its own story's to offer): deeds, news of
         *  the world, what became of people. Any thread. */
        std::vector<std::string> TalkForTales(std::size_t most);
        /** The one who would pass the talk on in the place the player stands in (the innkeeper, else a server or the
         *  bard), awake and at peace, or null. The game's thread. */
        RE::Actor* Publican() const;
        /** What `teller` brings up to the player unasked, now counted as told; empty when there is nothing worth a word.
         *  The game's thread. */
        std::string BringUp(RE::Actor* teller);
        /** For the dashboard: everything going round (a JSON array), each with where it is heard, whether that
         *  is where the player is, how old it is and how often someone has brought it up to the player. */
        std::string DashboardJson() const;

        void Save(SKSE::SerializationInterface* intfc);
        void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
        void Revert();

        static void Register(RE::BSScript::IVirtualMachine* a_vm, int& count);

    private:
        RumorBridge() = default;
        void Write(std::vector<Rumors::Event> events, std::vector<Rumors::Rumor> gone);   // SkyrimNet's side, off the game's thread
        void Reconcile();

        mutable std::mutex m_mutex;
        Rumors::Ledger m_ledger;
        float m_lastTick = -1.0f;                 // game days
        std::atomic<float> m_now{0.0f};           // the game's time at the last tick, for readers on other threads
        std::atomic<std::uint8_t> m_playerHold{0};  // the hold the player was last in (Tick), for news that names no place
        std::chrono::steady_clock::time_point m_loadedAt{};
        bool m_reconciled = false;
    };

    constexpr std::uint32_t kRumorRecordType = 'IERU';
}
