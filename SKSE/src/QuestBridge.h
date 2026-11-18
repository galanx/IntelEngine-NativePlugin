#pragma once

// The game side of the 4.0 quests (Quests.h holds the logic, docs/QUESTS.md the design): the book in the co-save,
// the places and items a quest may use, the facts read from the game for each step, the stamped item, the journal
// titles, what the people in a quest know, and the natives IntelEngine_Quests.psc calls.

#include "Quests.h"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace IntelEngine {

    class QuestBridge {
    public:
        static QuestBridge* GetSingleton() {
            static QuestBridge instance;
            return &instance;
        }

        /** At the main menu: the places (dungeons with a boss room) and the keepsakes (plain jewellery) quests use. */
        void Catalogue();

        /** Plans a quest for this giver (main thread): a template that fits, a place, an item, the truth. 0 = none
         *  fits or one may not be planned now; why is logged. */
        std::uint32_t PlanFor(RE::Actor* giver);
        /** Plans a venture (main thread): a former follower living a life of their own sets out for a dungeon of the
         *  player's hold, now and then with another former follower, and is met on the road. 0 = none fits. */
        std::uint32_t PlanVenture(RE::Actor* ally);
        /** Whether this actor gives, or goes on, a quest that is planned or open (the lives leave them be). */
        bool Involves(RE::FormID actor) const;
        /** A venture was put on the road: what the two remember of falling in together is written now. */
        void Placed(std::uint32_t id);

        bool Accept(std::uint32_t id);
        bool Decline(std::uint32_t id);
        /** Planned quests whose giver never reached the player: dropped, nobody remembers. */
        std::vector<std::uint32_t> Lapse();
        std::vector<std::uint32_t> OpenIds() const;
        /** The planned quest this giver is on the way to offer, 0 = none. */
        std::uint32_t PlannedFor(RE::FormID giver) const;

        /** Reads the facts and moves the quest on: (ending << 8) | the bits of the steps done now; 0 = no change. */
        std::int32_t Advance(std::uint32_t id);
        /** The handover may be offered now: the step is the handover, the stamped item is on the player, the giver is
         *  right here and has just spoken, and it was not offered in the last minute. */
        bool MayHandOver(std::uint32_t id);
        /** Moves the stamped item from the player to the giver; true when it moved. */
        bool HandOver(std::uint32_t id);

        int Field(std::uint32_t id, const std::string& name) const;
        RE::TESForm* Form(std::uint32_t id, const std::string& name) const;
        std::string Text(std::uint32_t id, const std::string& name) const;
        /** The foe was placed: their name (given now, by their sex), recorded with them. */
        std::string SetFoe(std::uint32_t id, RE::Actor* foe);
        void SetPlaced(std::uint32_t id, int bits);

        /** Every journal quest with a record gets its title (on accepting, and after every load: titles are not saved). */
        void ApplyTitles() const;

        /** What this NPC knows of a quest they give or go on, first person lines ("- ..."), for their bio. */
        std::string LinesFor(RE::Actor* actor) const;
        /** One line for the dungeon master's candidate list, empty when none. */
        std::string BusinessLine(RE::FormID actor) const;

        void Save(SKSE::SerializationInterface* intfc);
        void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
        void Revert();

    private:
        QuestBridge() = default;
        static float NowDays();
        std::string Fill(std::string text, const Quests::Quest& q) const;
        void Ended(const Quests::Quest& q);   // memories, the deed in the hold, the open rumor withdrawn (lock held)
        void Spread(const Quests::Quest& q);  // the rumor of an open quest (lock held)

        struct Place {
            RE::FormID location = 0;
            RE::FormID anchor = 0;
            RE::FormID hold = 0;
            RE::NiPoint3 at{};      // the location's map marker, in Tamriel
            bool hasAt = false;
            bool crypt = false;
        };

        mutable std::mutex m_mutex;
        Quests::Book m_book;
        std::vector<Place> m_places;
        std::vector<RE::FormID> m_keepsakes;
        std::vector<RE::FormID> m_followerTypes;   // references of the follower types of the load order (ventures)
        bool m_catalogued = false;
        float m_handOverOfferedAt = -1.0f;   // real seconds (the popup at most once a minute)
    };

    namespace QuestNatives {
        /** The quest natives of script IntelEngine (IntelEngine_Quests.psc calls them). */
        void Register(RE::BSScript::IVirtualMachine* a_vm, int& count);
    }
}
