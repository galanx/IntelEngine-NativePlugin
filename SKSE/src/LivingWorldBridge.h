#pragma once

// The game side of the 4.0 living world (LivingWorld.h holds the logic): keeps the record in the co-save,
// turns the player's arrivals into seams for the story engine, asks the director whether a moment may land
// now, and lets the director rank the story engine's candidates. At the Classic level it changes nothing.

#include "LivingWorld.h"

#include <mutex>
#include <vector>

namespace IntelEngine {

    class LivingWorldBridge final : public RE::BSTEventSink<RE::TESActorLocationChangeEvent>,
                                    public RE::BSTEventSink<RE::BGSActorCellEvent>,
                                    public RE::BSTEventSink<RE::TESSleepStopEvent> {
    public:
        static LivingWorldBridge* GetSingleton() {
            static LivingWorldBridge instance;
            return &instance;
        }

        /** Listen for the player's arrivals and waking up (kDataLoaded). Safe to call twice. */
        void Register();

        LivingWorld::Settings GetSettings() const;
        /** From the scripts: on load and whenever a living-world setting changes. */
        void SetSettings(const LivingWorld::Settings& settings);
        bool IsClassic() const { return GetSettings().level == LivingWorld::WorldLevel::Classic; }

        /** May a story moment land now? Seam ids are LivingWorld::Seam values; 0 is the old timer. */
        bool MayDeliver(int seam, bool urgent);

        /** The director's score for one story candidate known from SkyrimNet's history (async thread). */
        float ScoreStoryCandidate(RE::FormID actor, int interactionCount, float daysSinceLastMet, bool sameHold,
                                  bool friendOfFriend, float nowDays, std::uint32_t seed);

        /** A story moment was dispatched with this NPC: count it for pacing and rotation. */
        void OnStoryDispatched(RE::FormID actor, const std::string& storyType);

        void Save(SKSE::SerializationInterface* intfc);
        void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
        void Revert();

        RE::BSEventNotifyControl ProcessEvent(const RE::TESActorLocationChangeEvent* event,
                                              RE::BSTEventSource<RE::TESActorLocationChangeEvent>*) override;
        RE::BSEventNotifyControl ProcessEvent(const RE::BGSActorCellEvent* event,
                                              RE::BSTEventSource<RE::BGSActorCellEvent>*) override;
        RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStopEvent* event,
                                              RE::BSTEventSource<RE::TESSleepStopEvent>*) override;

    private:
        LivingWorldBridge() = default;

        LivingWorld::Life& Touch(RE::FormID actor);  // m_mutex held
        void SendSeam(LivingWorld::Seam seam);
        void OnPlayerLocation(const RE::BGSLocation* location);
        static float NowDays();

        mutable std::mutex m_mutex;
        std::vector<LivingWorld::Life> m_lives;
        LivingWorld::DirectorState m_state;
        LivingWorld::Settings m_settings;
        bool m_settingsKnown = false;
        float m_lastRoadSeamDay = -1.0e6f;
        bool m_registered = false;
        const RE::BGSLocation* m_lastLocation = nullptr;  // m_mutex held
        bool m_lastLocationKnown = false;                  // false after a load: the first place is no arrival
    };
}
