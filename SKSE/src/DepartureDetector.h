#pragma once

/**
 * Departure Detection System
 *
 * Tracks per-slot NPC starting positions and detects whether they've
 * successfully departed. Replaces StorageUtil-based departure tracking
 * (Intel_DepartX/Y/Tick/Retries) with a native C++ singleton.
 *
 * Matches the StuckDetector pattern: slot-indexed arrays, shared_mutex,
 * Meyer's singleton. Papyrus calls C++ for position math and status;
 * Papyrus handles engine-specific responses (EvaluatePackage, PathTo).
 *
 * Return codes from CheckDepartureStatus:
 *   0 = too early (still accumulating ticks before first check)
 *   1 = departed (NPC moved beyond threshold from start position)
 *   2 = soft recovery needed (first failure — caller should nudge AI)
 *   3 = escalate (repeated failure — caller should handle departure failure)
 */

#include "Plugin.h"

#include <array>
#include <shared_mutex>

namespace IntelEngine {

    class DepartureDetector {
    public:
        static DepartureDetector* GetSingleton() {
            static DepartureDetector instance;
            return &instance;
        }

        /**
         * Check if an actor has departed from their starting position.
         *
         * Increments internal tick counter each call. After minChecks ticks,
         * computes 2D distance from stored start position and escalates if
         * the NPC hasn't moved.
         *
         * @param actor  The actor to check
         * @param slot   Slot index (0-4)
         * @param threshold  Minimum 2D distance to consider "departed"
         * @return 0=too early, 1=departed, 2=soft recovery, 3=escalate
         */
        int CheckDepartureStatus(RE::Actor* actor, int slot, float threshold);

        /**
         * Reset departure tracking for a slot.
         * Stores actor's current XY position as baseline and clears counters.
         *
         * @param slot   Slot index (0-4)
         * @param actor  The actor occupying this slot (nullptr to clear)
         */
        void ResetSlot(int slot, RE::Actor* actor);

        /**
         * Get current retry count for a slot (for debug logging).
         *
         * @param slot  Slot index (0-4)
         * @return Number of soft recovery retries since last reset
         */
        int GetRetryCount(int slot) const;

    private:
        DepartureDetector() = default;
        ~DepartureDetector() = default;
        DepartureDetector(const DepartureDetector&) = delete;
        DepartureDetector& operator=(const DepartureDetector&) = delete;

        static constexpr int MAX_SLOTS = 5;

        struct SlotState {
            float startX = 0.0f;
            float startY = 0.0f;
            int tickCount = 0;
            int retries = 0;
            bool active = false;
            bool departed = false;
        };

        std::array<SlotState, MAX_SLOTS> m_slots{};
        mutable std::shared_mutex m_mutex;
    };

}  // namespace IntelEngine
