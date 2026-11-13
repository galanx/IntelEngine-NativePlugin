#include "SpeechTracker.h"

#include <cctype>

#include <nlohmann/json.hpp>

namespace IntelEngine {

    namespace {
        double SecondsBetween(SpeechTracker::Clock::time_point from, SpeechTracker::Clock::time_point to) {
            return std::chrono::duration<double>(to - from).count();
        }

        // Event names travel through the game's string pool, which keeps one spelling per name whatever its
        // case: a script that registered "skyrimnet_audiostarted" first decides how the name arrives here.
        bool SameName(const char* a, const char* b) {
            for (; *a && *b; ++a, ++b) {
                if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
            }
            return *a == *b;
        }
    }  // namespace

    bool SpeechTracker::KindOf(const char* eventName, Kind& kind) {
        if (!eventName) return false;
        if (SameName(eventName, "SkyrimNet_SpeechStarted")) {
            kind = Kind::Queued;
            return true;
        }
        if (SameName(eventName, "SkyrimNet_AudioStarted")) {
            kind = Kind::AudioStarted;
            return true;
        }
        if (SameName(eventName, "SkyrimNet_AudioEnded")) {
            kind = Kind::AudioEnded;
            return true;
        }
        return false;
    }

    bool SpeechTracker::IsNarration(const char* payloadJson) {
        if (!payloadJson || !*payloadJson) return false;
        const auto payload = nlohmann::json::parse(payloadJson, nullptr, false);
        if (!payload.is_object()) return false;
        const auto it = payload.find("isNarration");
        return it != payload.end() && it->is_boolean() && it->get<bool>();
    }

    void SpeechTracker::Apply(RE::FormID actor, Kind kind, Clock::time_point now) {
        if (actor == 0) return;
        std::lock_guard<std::mutex> lock(mutex_);
        State& state = states_[actor];
        switch (kind) {
            case Kind::Queued:
                // Segments left over from a line that never played do not add up with a new line's.
                if (state.queued > 0 && SecondsBetween(state.lastQueued, now) > kQueuedGraceSeconds) state.queued = 0;
                ++state.queued;
                state.lastQueued = now;
                break;
            case Kind::AudioStarted:
                if (state.queued > 0) --state.queued;
                state.audible = true;
                state.audibleSince = now;
                break;
            case Kind::AudioEnded:
                state.audible = false;
                state.everEnded = true;
                state.lastEnded = now;
                break;
        }
    }

    bool SpeechTracker::IsSpeechPendingAt(RE::FormID actor, Clock::time_point now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = states_.find(actor);
        if (it == states_.end()) return false;
        const State& state = it->second;
        if (state.audible && SecondsBetween(state.audibleSince, now) <= kAudibleMaxSeconds) return true;
        return state.queued > 0 && SecondsBetween(state.lastQueued, now) <= kQueuedGraceSeconds;
    }

    float SpeechTracker::SecondsSinceSpeechEndedAt(RE::FormID actor, Clock::time_point now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = states_.find(actor);
        if (it == states_.end() || !it->second.everEnded) return -1.0f;
        return static_cast<float>(SecondsBetween(it->second.lastEnded, now));
    }

    void SpeechTracker::Register() {
        if (registered_) return;
        if (auto* source = SKSE::GetModCallbackEventSource()) {
            source->AddEventSink(this);
            registered_ = true;
            logger::info("SpeechTracker: listening for SkyrimNet speech events");
        } else {
            logger::warn("SpeechTracker: no ModCallbackEvent source; speech state falls back to the engine's talking test");
        }
    }

    void SpeechTracker::Reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        states_.clear();
    }

    RE::BSEventNotifyControl SpeechTracker::ProcessEvent(const SKSE::ModCallbackEvent* event,
                                                         RE::BSTEventSource<SKSE::ModCallbackEvent>*) {
        if (!event) return RE::BSEventNotifyControl::kContinue;
        Kind kind;
        if (!KindOf(event->eventName.c_str(), kind)) return RE::BSEventNotifyControl::kContinue;
        if (!seenEvents_.exchange(true)) {
            logger::info("SpeechTracker: first SkyrimNet speech event received ({})", event->eventName.c_str());
        }
        // The sender is the speaking actor (for the audio events: the actor the line came from).
        if (!event->sender || IsNarration(event->strArg.c_str())) return RE::BSEventNotifyControl::kContinue;
        Apply(event->sender->GetFormID(), kind, Clock::now());
        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace IntelEngine
