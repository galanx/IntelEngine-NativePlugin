#pragma once

// The game side of the 4.0 storylines (Storylines.h holds the logic): the book in the co-save, the natives the
// scripts call to open storylines and report what happened, the text SkyrimNet shows an NPC about what they asked
// of the player, and unfinished business for the director.

#include "Storylines.h"

#include <mutex>
#include <string>
#include <vector>

namespace IntelEngine {

    class StorylineBridge {
    public:
        static StorylineBridge* GetSingleton() {
            static StorylineBridge instance;
            return &instance;
        }

        std::uint32_t Open(Storylines::Storyline storyline, bool urgent);
        std::string Refusal(Storylines::Kind kind, Storylines::Request request, RE::FormID owner, bool urgent) const;
        bool Apply(std::uint32_t id, Storylines::Event event);
        std::vector<std::uint32_t> OpenIds() const;
        std::vector<std::uint32_t> Overdue() const;
        std::optional<Storylines::Storyline> Get(std::uint32_t id) const;
        std::uint32_t OpenIdFor(RE::FormID owner) const;

        /** Unfinished business between this NPC and the player (open, or an unhappy ending not yet followed up). */
        bool Unresolved(RE::FormID owner) const;
        void MarkFollowedUp(RE::FormID owner);

        /** What this NPC asked of the player, for their bio (SkyrimNet decorator). Empty when nothing. */
        std::string DescribeFor(RE::Actor* actor) const;

        /** One line for the dungeon master's candidate list: the open storyline or the latest unhappy ending not yet
         *  followed up, in the third person. Empty when there is none. */
        std::string BusinessLine(RE::FormID owner) const;

        /** The one who asked's first-person memory of how it ended, names filled in. Empty when none. */
        std::string MemoryFor(std::uint32_t id) const;

        // 4.0 branches from conversation (Storylines::Book::CallOff, Amend, Reply).
        bool CallOff(std::uint32_t id, const std::string& note);
        /** Empty = changed; otherwise why not. The gold offered now is filled in here. */
        std::string Amend(std::uint32_t id, Storylines::Book::Amendment amendment);
        std::string Reply(std::uint32_t id, RE::FormID speaker, const std::string& text, bool comesAlong);
        /** Why it was asked, in the asker's words (recorded when they ask). */
        bool SetAbout(std::uint32_t id, const std::string& text);
        /** The open request this person is the one to find or to speak with, 0 = none. */
        std::uint32_t AboutId(RE::FormID person) const;
        /** The id of a request this NPC may answer through the player now (the player is with them, no answer
         *  sent yet), empty when none: the eligibility of the SendWordBack action. */
        std::string AnswerPending(RE::Actor* actor) const;
        /** What the one it was about remembers of their answer, names filled in. Empty when nothing. */
        std::string OtherMemoryFor(std::uint32_t id) const;
        int OfferedGold(std::uint32_t id, int playerLevel) const;

        void Save(SKSE::SerializationInterface* intfc);
        void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
        void Revert();

    private:
        StorylineBridge() = default;
        static float NowDays();

        mutable std::mutex m_mutex;
        Storylines::Book m_book;
    };
}
