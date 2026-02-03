/**
 * Off-Screen Travel Tracker Implementation
 *
 * Lightweight position-based tracker for NPCs traveling off-screen.
 * Phase 1 (before estimated arrival): passive — just keeps baseline fresh.
 * Phase 2 (after estimated arrival): active — detects stationary NPCs.
 */

#include "OffScreenTracker.h"

#include <cmath>

namespace IntelEngine {

    void OffScreenTracker::InitSlot(int slot, float estimatedArrivalGameTime,
                                    float startX, float startY) {
        if (slot < 0 || slot >= MAX_SLOTS) return;

        std::unique_lock lock(m_mutex);
        auto& state = m_slots[slot];
        state.estimatedArrival = estimatedArrivalGameTime;
        state.lastX = startX;
        state.lastY = startY;
        state.stationaryTicks = 0;
        state.active = true;

        logger::debug("OffScreenTracker: slot {} init, est. arrival={:.4f}",
                      slot, estimatedArrivalGameTime);
    }

    int OffScreenTracker::CheckProgress(int slot, float currentGameTime,
                                        float npcX, float npcY) {
        if (slot < 0 || slot >= MAX_SLOTS) return 0;

        std::unique_lock lock(m_mutex);
        auto& state = m_slots[slot];
        if (!state.active) return 0;

        // Phase 1: Before estimated arrival — NPC is "in transit".
        // Update position baseline so the first post-deadline check
        // compares against the most recent position, not the start.
        if (currentGameTime < state.estimatedArrival) {
            state.lastX = npcX;
            state.lastY = npcY;
            state.stationaryTicks = 0;
            return 0;
        }

        // Phase 2: Past estimated arrival — check if NPC is moving.
        // NPCs in loaded-but-not-rendered cells may still be pathfinding
        // (engine updates their position). Unloaded NPCs are frozen.
        float dx = npcX - state.lastX;
        float dy = npcY - state.lastY;
        float dist = std::sqrt(dx * dx + dy * dy);

        state.lastX = npcX;
        state.lastY = npcY;

        if (dist >= MOVEMENT_THRESHOLD) {
            // Moving — reset counter, let them continue
            state.stationaryTicks = 0;
            return 0;
        }

        // Stationary — accumulate ticks
        state.stationaryTicks++;
        if (state.stationaryTicks >= STATIONARY_TICKS) {
            logger::debug("OffScreenTracker: slot {} stationary for {} ticks, "
                          "signaling teleport", slot, state.stationaryTicks);
            return 1;
        }

        return 0;
    }

    void OffScreenTracker::ResetSlot(int slot) {
        if (slot < 0 || slot >= MAX_SLOTS) return;

        std::unique_lock lock(m_mutex);
        m_slots[slot] = SlotState{};
    }

}  // namespace IntelEngine
