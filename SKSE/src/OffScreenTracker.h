#pragma once

/**
 * Off-Screen Travel Tracker
 *
 * Tracks estimated arrival time and position for NPCs traveling off-screen.
 * Skyrim doesn't simulate movement for unloaded NPCs — their position is
 * frozen until the cell loads again. This tracker waits until enough game
 * time has passed (distance-based estimate), then confirms the NPC is
 * stationary before signaling Papyrus to teleport.
 *
 * Two-phase approach:
 * 1. Before estimated arrival: returns 0 (wait), keeps position baseline fresh.
 * 2. After estimated arrival: checks for movement. If stationary for
 *    STATIONARY_TICKS consecutive calls, returns 1 (teleport).
 *
 * Follows StuckDetector/DepartureDetector pattern:
 * slot-indexed arrays, shared_mutex, Meyer's singleton.
 * Papyrus calls C++ for position math and timing; Papyrus handles
 * engine-specific responses (MoveTo, EvaluatePackage, OnArrival).
 */

#include "Plugin.h"

#include <array>
#include <shared_mutex>

namespace IntelEngine {

    class OffScreenTracker {
    public:
        static OffScreenTracker* GetSingleton() {
            static OffScreenTracker instance;
            return &instance;
        }

        /**
         * Initialize off-screen tracking for a slot.
         *
         * @param slot  Slot index (0-4)
         * @param estimatedArrivalGameTime  Absolute game time (days) when NPC
         *        should have arrived, computed by CalculateDeadlineFromDistance.
         * @param startX  NPC's starting X position
         * @param startY  NPC's starting Y position
         */
        void InitSlot(int slot, float estimatedArrivalGameTime,
                      float startX, float startY);

        /**
         * Check off-screen travel progress.
         *
         * Before estimated arrival: updates baseline, returns 0.
         * After estimated arrival: checks position delta against threshold.
         * Returns 1 after STATIONARY_TICKS consecutive no-movement checks.
         *
         * @param slot  Slot index (0-4)
         * @param currentGameTime  Current game time (days)
         * @param npcX  NPC's current X position (from GetPosition())
         * @param npcY  NPC's current Y position
         * @return 0=still in transit (wait), 1=should teleport to destination
         */
        int CheckProgress(int slot, float currentGameTime,
                          float npcX, float npcY);

        /**
         * Reset/clear a slot. Called on task completion, cancellation, or load.
         *
         * @param slot  Slot index (0-4)
         */
        void ResetSlot(int slot);

    private:
        OffScreenTracker() = default;
        ~OffScreenTracker() = default;
        OffScreenTracker(const OffScreenTracker&) = delete;
        OffScreenTracker& operator=(const OffScreenTracker&) = delete;

        static constexpr int MAX_SLOTS = 5;
        static constexpr int STATIONARY_TICKS = 3;      // ~9s at 3s poll interval
        static constexpr float MOVEMENT_THRESHOLD = 50.0f;  // Same as StuckDetector

        struct SlotState {
            float estimatedArrival = 0.0f;
            float lastX = 0.0f;
            float lastY = 0.0f;
            int stationaryTicks = 0;
            bool active = false;
        };

        std::array<SlotState, MAX_SLOTS> m_slots{};
        mutable std::shared_mutex m_mutex;
    };

}  // namespace IntelEngine
