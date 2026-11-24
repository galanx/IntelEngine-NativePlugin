#pragma once

/**
 * DialogueTracker — Per-NPC dialogue line counter for auto bio updates.
 *
 * Registers a SkyrimNet event callback for "dialogue" events, counts lines per NPC,
 * and triggers a dynamic_bio_update via ModEvent when the threshold is reached.
 * The threshold and enabled state are controlled from Papyrus (MCM/Dashboard).
 */

#include "Plugin.h"
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <deque>
#include <string>
#include <vector>

namespace IntelEngine {

    class DialogueTracker {
    public:
        static DialogueTracker* GetSingleton() {
            static DialogueTracker instance;
            return &instance;
        }

        /** Real seconds since somebody last said a line to the player, or the player said one; below 0: nobody has
         *  this session. A SkyrimNet conversation has no menu of the game to tell it by. Any thread. */
        static float SecondsSincePlayerTalk();

        /** Real seconds since somebody other than `except` last said a line to the player; below 0: nobody among
         *  the lines kept. `who`: that speaker. What the player and `except` say to each other does not count. Any
         *  thread. */
        float SecondsSinceOtherSpokeToPlayer(RE::FormID except, RE::FormID* who = nullptr) const;

        /** Real seconds since this one last said a line to the player; below 0: not among the lines kept. Any thread. */
        float SecondsSinceSpokeToPlayer(RE::FormID who) const;

        /** Everybody who said a line to the player in the last `seconds` real seconds (each once). Any thread. */
        std::vector<RE::FormID> SpeakersToPlayer(float seconds) const;

        /** Register the dialogue event callback with SkyrimNet. Call after API init. */
        void Initialize();

        /** Unregister the callback (cleanup). */
        void Shutdown();

        /** Set the line threshold for triggering a bio update. 0 = disabled. */
        void SetThreshold(int lines) { threshold_.store(lines); }
        int GetThreshold() const { return threshold_.load(); }

        /** Enable/disable the tracker. */
        void SetEnabled(bool enabled) { enabled_.store(enabled); }
        bool IsEnabled() const { return enabled_.load(); }

        /** Get the current dialogue count for an NPC. */
        int GetCount(RE::FormID formId) const;

        /** Reset the counter for a specific NPC. */
        void ResetCount(RE::FormID formId);

        /** Set the counter for a specific NPC (used to warm from StorageUtil on load). */
        void SetCount(RE::FormID formId, int count);

        /** Get all tracked FormIDs and their counts (for saving to StorageUtil). */
        std::vector<std::pair<RE::FormID, int>> GetAllCounts() const;

        /** A line said lately: who said it, to whom (0 = nobody named: the player's typed and spoken lines), what. */
        struct Line {
            std::chrono::steady_clock::time_point at;
            RE::FormID speaker = 0;
            RE::FormID target = 0;
            std::string text;
        };
        /** The lines of a conversation between the player and this NPC in the last `seconds`: theirs to anyone, the
         *  player's (whose lines name nobody). Oldest first, at most `max`. */
        std::vector<Line> ConversationWith(RE::FormID npc, float seconds, std::size_t max) const;

    private:
        DialogueTracker() = default;
        ~DialogueTracker() = default;
        DialogueTracker(const DialogueTracker&) = delete;
        DialogueTracker& operator=(const DialogueTracker&) = delete;

        void OnDialogueEvent(const char* json);
        void Record(const char* json, bool player);

        mutable std::mutex mutex_;
        std::deque<Line> lines_;   // the last lines said around the player (story tales read their talks here)
        std::unordered_map<RE::FormID, int> lineCounts_;
        std::atomic<int> threshold_{20};
        std::atomic<bool> enabled_{false};
        std::vector<uint64_t> playerLineCallbackIds_;     // the player's own lines, passed on to SpeechTracker
        uint64_t callbackId_ = 0;
    };

}  // namespace IntelEngine
