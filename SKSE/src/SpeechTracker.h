#pragma once

/**
 * SpeechTracker — who SkyrimNet is about to make speak, who is speaking, and when they stopped.
 *
 * SkyrimNet sends SKSE ModEvents for every generated line (ai_docs/GAME_INTEGRATION.md, "Outbound Mod
 * Events"): SkyrimNet_SpeechStarted when a segment is queued for synthesis, SkyrimNet_AudioStarted when it
 * becomes audible, SkyrimNet_AudioEnded when it stops. The engine's own "is talking" test only knows the
 * middle of that, so a script that waits for an NPC to finish used to miss the line that was still being
 * synthesized, and sent them off before they had answered.
 *
 * The state is per session (not saved): a load starts with nobody speaking. Narration segments are not an
 * NPC's own speech and are ignored. A segment that was queued and never became audible (purged, failed)
 * stops counting after kQueuedGraceSeconds.
 */

#include "Plugin.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace IntelEngine {

    class SpeechTracker final : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
    public:
        using Clock = std::chrono::steady_clock;

        /** What one event means for one speaker; separated from the sink so it can be tested without the game. */
        enum class Kind { Queued, AudioStarted, AudioEnded };

        static SpeechTracker* GetSingleton() {
            static SpeechTracker instance;
            return &instance;
        }

        /** Start listening (kDataLoaded). Safe to call twice. */
        void Register();
        /** A load or a new game: nobody is speaking. */
        void Reset();

        /** SkyrimNet has sent a speech event this session, so "not pending" is knowledge and not absence. */
        bool HasSeenEvents() const { return seenEvents_.load(); }

        /** A line of this actor is queued for synthesis, being synthesized, or audible. */
        bool IsSpeechPending(RE::FormID actor) const { return IsSpeechPendingAt(actor, Clock::now()); }
        /** Seconds since this actor's last generated line stopped being audible; -1 if none this session. */
        float SecondsSinceSpeechEnded(RE::FormID actor) const { return SecondsSinceSpeechEndedAt(actor, Clock::now()); }

        // The same with the time given, and the state change itself: used by the sink and by the offline check.
        void Apply(RE::FormID actor, Kind kind, Clock::time_point now);
        bool IsSpeechPendingAt(RE::FormID actor, Clock::time_point now) const;
        float SecondsSinceSpeechEndedAt(RE::FormID actor, Clock::time_point now) const;

        /** The event kind for a ModEvent name, false for any other event. */
        static bool KindOf(const char* eventName, Kind& kind);
        /** Whether the payload says the segment is narration (not the actor's own speech). Bad JSON: false. */
        static bool IsNarration(const char* payloadJson);

        RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event,
                                              RE::BSTEventSource<SKSE::ModCallbackEvent>* source) override;

        /** A queued segment that never became audible stops counting after this long. */
        static constexpr double kQueuedGraceSeconds = 20.0;
        /** An audible segment whose end notice never came stops counting after this long. */
        static constexpr double kAudibleMaxSeconds = 60.0;

    private:
        struct State {
            int queued = 0;                    // segments queued and not yet audible
            bool audible = false;
            bool everEnded = false;
            Clock::time_point lastQueued{};
            Clock::time_point audibleSince{};
            Clock::time_point lastEnded{};
        };

        SpeechTracker() = default;

        mutable std::mutex mutex_;
        std::unordered_map<RE::FormID, State> states_;
        std::atomic<bool> seenEvents_{false};
        bool registered_ = false;
    };

}  // namespace IntelEngine
