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

        /** The one who asked's first-person memory of how it ended, names filled in. Empty when none. */
        std::string MemoryFor(std::uint32_t id) const;

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
