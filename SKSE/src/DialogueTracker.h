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

namespace IntelEngine {

    class DialogueTracker {
    public:
        static DialogueTracker* GetSingleton() {
            static DialogueTracker instance;
            return &instance;
        }

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

    private:
        DialogueTracker() = default;
        ~DialogueTracker() = default;
        DialogueTracker(const DialogueTracker&) = delete;
        DialogueTracker& operator=(const DialogueTracker&) = delete;

        void OnDialogueEvent(const char* json);

        mutable std::mutex mutex_;
        std::unordered_map<RE::FormID, int> lineCounts_;
        std::atomic<int> threshold_{20};
        std::atomic<bool> enabled_{false};
        uint64_t callbackId_ = 0;
    };

}  // namespace IntelEngine
