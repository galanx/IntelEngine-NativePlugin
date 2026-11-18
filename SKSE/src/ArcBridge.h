#pragma once

#include "Arcs.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace IntelEngine {

    /**
     * Story arcs in the game (docs/STORY_ARCS.md): casts an arc from the real people of a town, reads the facts of the
     * game once a look, says where its props go and who the journal points at, and keeps the arcs in the co-save
     * (record 'IEAR'). The decisions are Arcs.h's; IntelEngine_Arcs.psc offers, shows and carries out.
     *
     * Every function that touches the game runs on the main thread (Papyrus natives, the SKSE task queue). LinesFor
     * is read by SkyrimNet's threads: it reads only the record, under the lock.
     */
    class ArcBridge {
    public:
        static ArcBridge* GetSingleton() {
            static ArcBridge instance;
            return &instance;
        }

        /** At data load: the townsfolk with a home in a town, and who a quest of the journal names (never cast as a
         *  culprit or a victim while that quest is not done). */
        void Catalogue();

        /** Plans an arc of this pattern (0: whichever fits) with this asker (main thread). 0 = none fits. */
        std::uint32_t PlanFor(RE::Actor* asker, int pattern);
        bool Accept(std::uint32_t id);
        bool Decline(std::uint32_t id);
        std::vector<std::uint32_t> Lapse();
        std::vector<std::uint32_t> OpenIds() const;

        /** Reads the facts and moves the arc on. Bits 0-7 the nodes done now, 8-15 the ending, 16-19 the effect, 20-23
         *  its role; 24 a prop is to be put in place now (PlantAt), 25 the choice may be offered, 26 the hand-over may
         *  be offered; 27-30 a node left by a shortcut, plus one. 0 = nothing. */
        std::int32_t Advance(std::uint32_t id);
        /** The answer to the choice popup, by the index of the node's options (-1: not now). */
        bool Answer(std::uint32_t id, int option);
        /** The prop of the current hand-over goes from the player to who the node gives it to. */
        bool HandOver(std::uint32_t id);
        /** Gold that reached the player from this actor (the inventory event): counted toward an open arc's reward. */
        void GoldToPlayer(RE::FormID from, std::int32_t count);
        /** At data load: the inventory event that sees gold reach the player. */
        void Listen();
        /** The script put prop `prop` at this reference (a container, or the floor beside it). */
        void Planted(std::uint32_t id, int prop, RE::TESObjectREFR* at);

        int Field(std::uint32_t id, const std::string& name) const;
        RE::TESForm* Form(std::uint32_t id, const std::string& name) const;
        std::string Text(std::uint32_t id, const std::string& name) const;

        /** Whether this actor has a part in an arc that is planned or open. */
        bool Involves(RE::FormID actor) const;
        /** The id of the planned or open arc this actor has a part in, 0 = none. */
        std::uint32_t ArcOf(RE::FormID actor) const;
        /** What this NPC knows of the arcs they are in, first person lines ("- ..."), for their bio. */
        std::string LinesFor(RE::Actor* actor) const;
        /** Every journal quest of an arc gets its title (on accepting, and after every load). */
        void ApplyTitles() const;

        void Save(SKSE::SerializationInterface* intfc);
        void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
        void Revert();

    private:
        ArcBridge() = default;
        static float NowDays();

        struct Folk {
            RE::FormID ref = 0;
            RE::FormID base = 0;
            RE::FormID home = 0;   // the interior cell of their bed
            RE::FormID town = 0;   // the town (location) that home is in
        };
        // What one look learned that the record does not keep (the popups' answers, where a prop goes now).
        struct Pending {
            int answer = -1;
            std::uint8_t handed = 0;
            int brawl = 0;
            std::uint16_t log = 0;
            RE::FormID plantAt = 0;
            int plantProp = -1;
            bool plantFloor = false;
            float chooseHold = -1.0f;   // real seconds: no choice popup before this
            float giveHold = -1.0f;
            RE::FormID checkedCell = 0;  // the interior last looked at for the culprit's home, and what it was
            bool homeHere = false;
        };

        std::string Fill(std::string text, const Arcs::Arc& a) const;
        const Folk* FolkOf(RE::FormID ref) const;
        bool Usable(RE::Actor* actor, int need, std::string& why) const;
        bool Cast(Arcs::Arc& a, RE::Actor* asker, std::string& why) const;
        bool IsHomeOf(RE::TESObjectCELL* cell, const Arcs::Arc& a, Arcs::Role role) const;
        void Ended(const Arcs::Arc& a);   // memories, the rumor in the hold (lock held)

        void Reconcile();   // withdraws rumors of arcs this save does not know (lock not held)

        mutable std::mutex m_mutex;
        Arcs::Book m_book;
        bool m_reconciled = true;                                   // after a load: not yet looked at
        std::chrono::steady_clock::time_point m_loadedAt{};
        std::atomic<int> m_writing{0};                              // rumors being written now
        std::unordered_map<std::uint32_t, Pending> m_pending;
        std::vector<Folk> m_folk;
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> m_journalAliases;   // actor base -> quests naming it
        bool m_catalogued = false;

    public:
        /** The arc natives of script IntelEngine (IntelEngine_Arcs.psc calls them). */
        static void Register(RE::BSScript::IVirtualMachine* a_vm, int& count);
    };
}
