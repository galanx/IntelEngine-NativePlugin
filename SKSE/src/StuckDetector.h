#pragma once

/**
 * Stuck Detection System
 *
 * Tracks per-slot NPC position state and provides escalating stuck recovery.
 * Replaces duplicated Papyrus arrays in NPCTasks.psc and Travel.psc with
 * a single C++ singleton for performance and reliability.
 *
 * Three-level escalation:
 * 1. Position polling detects no movement
 * 2. Soft recovery (Papyrus re-evaluates packages)
 * 3. Teleport fallback (progressive distance)
 */

#include "Plugin.h"

#include <array>
#include <shared_mutex>

namespace IntelEngine {

    class StuckDetector {
    public:
        static StuckDetector* GetSingleton() {
            static StuckDetector instance;
            return &instance;
        }

        /**
         * Check if an actor is stuck in the given slot.
         *
         * Auto-initializes on first call if slot has not been reset.
         * Reads actor position via RE::Actor::GetPosition() and computes
         * 3D distance from last recorded position.
         *
         * @param actor The actor to check
         * @param slot Slot index (0-4)
         * @param threshold Minimum distance to consider "moving"
         * @return 0=moving, 1=soft recovery needed, 3=teleport needed
         */
        int CheckStuckStatus(RE::Actor* actor, int slot, float threshold);

        /**
         * Reset stuck tracking for a slot.
         * Stores actor's current position as baseline and clears all counters.
         *
         * @param slot Slot index (0-4)
         * @param actor The actor occupying this slot (may be nullptr to clear)
         */
        void ResetSlot(int slot, RE::Actor* actor);

        /**
         * Get progressive teleport distance for a slot.
         * Distance decreases with each teleport attempt: 2000 -> 1000 -> 500 -> 250.
         *
         * @param slot Slot index (0-4)
         * @return Distance in game units
         */
        float GetTeleportDistance(int slot) const;

        /**
         * Get current recovery attempt count for a slot.
         *
         * @param slot Slot index (0-4)
         * @return Number of recovery attempts since last reset
         */
        int GetRecoveryAttempts(int slot) const;

    private:
        StuckDetector() = default;
        ~StuckDetector() = default;
        StuckDetector(const StuckDetector&) = delete;
        StuckDetector& operator=(const StuckDetector&) = delete;

        static constexpr int MAX_SLOTS = 5;

        struct SlotState {
            float lastX = 0.0f;
            float lastY = 0.0f;
            float lastZ = 0.0f;
            int stuckCheckCount = 0;
            int recoveryAttempts = 0;
            int teleportAttempts = 0;
            bool initialized = false;
        };

        std::array<SlotState, MAX_SLOTS> m_slots{};
        mutable std::shared_mutex m_mutex;
    };

}  // namespace IntelEngine
