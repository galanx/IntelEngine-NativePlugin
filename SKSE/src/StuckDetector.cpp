/**
 * Stuck Detection System Implementation
 *
 * Consolidates ~230 lines of duplicated Papyrus stuck detection
 * (NPCTasks.psc + Travel.psc) into a single native implementation.
 */

#include "StuckDetector.h"
#include "Settings.h"

#include <cmath>

namespace IntelEngine {

    int StuckDetector::CheckStuckStatus(RE::Actor* actor, int slot, float threshold) {
        if (!actor || slot < 0 || slot >= MAX_SLOTS) {
            return 0;
        }

        std::unique_lock lock(m_mutex);
        auto& state = m_slots[slot];

        // Auto-initialize on first call
        if (!state.initialized) {
            auto pos = actor->GetPosition();
            state.lastX = pos.x;
            state.lastY = pos.y;
            state.lastZ = pos.z;
            state.stuckCheckCount = 0;
            state.recoveryAttempts = 0;
            state.teleportAttempts = 0;
            state.initialized = true;
            return 0;
        }

        // Read current position
        auto pos = actor->GetPosition();
        float dx = pos.x - state.lastX;
        float dy = pos.y - state.lastY;
        float dz = pos.z - state.lastZ;
        float distanceMoved = std::sqrt(dx * dx + dy * dy + dz * dz);

        // Always update last known position
        state.lastX = pos.x;
        state.lastY = pos.y;
        state.lastZ = pos.z;

        if (distanceMoved >= threshold) {
            // Moving — reset all stuck counters
            state.stuckCheckCount = 0;
            state.teleportAttempts = 0;
            return 0;
        }

        // Not moving — increment stuck counter
        state.stuckCheckCount++;

        auto* settings = Settings::GetSingleton();
        int maxChecks = settings->stuckMaxChecks;
        int maxRecovery = settings->stuckMaxRecovery;

        if (state.stuckCheckCount < maxChecks) {
            return 0;  // Not stuck yet — need more consecutive checks
        }

        // Stuck threshold reached — escalate
        state.recoveryAttempts++;
        state.stuckCheckCount = 0;

        if (state.recoveryAttempts < maxRecovery) {
            return 1;  // Soft recovery (Papyrus: re-apply package + EvaluatePackage)
        }

        // Recovery exhausted — teleport
        state.recoveryAttempts = 0;
        state.teleportAttempts++;
        return 3;
    }

    void StuckDetector::ResetSlot(int slot, RE::Actor* actor) {
        if (slot < 0 || slot >= MAX_SLOTS) {
            return;
        }

        std::unique_lock lock(m_mutex);
        auto& state = m_slots[slot];

        if (actor) {
            auto pos = actor->GetPosition();
            state.lastX = pos.x;
            state.lastY = pos.y;
            state.lastZ = pos.z;
            state.initialized = true;
        } else {
            state.lastX = 0.0f;
            state.lastY = 0.0f;
            state.lastZ = 0.0f;
            state.initialized = false;
        }

        state.stuckCheckCount = 0;
        state.recoveryAttempts = 0;
        state.teleportAttempts = 0;
    }

    float StuckDetector::GetTeleportDistance(int slot) const {
        if (slot < 0 || slot >= MAX_SLOTS) {
            return 2000.0f;
        }

        std::shared_lock lock(m_mutex);
        int attempts = m_slots[slot].teleportAttempts;

        // Progressive distance: each attempt halves the distance so the NPC
        // lands closer if the previous spot was behind a wall / off-navmesh.
        // teleportAttempts is already incremented by CheckStuckStatus before
        // Papyrus calls this, so: 1=2000, 2=1000, 3=500, 4+=250
        if (attempts <= 1) return 2000.0f;
        if (attempts == 2) return 1000.0f;
        if (attempts == 3) return 500.0f;
        return 250.0f;
    }

    int StuckDetector::GetRecoveryAttempts(int slot) const {
        if (slot < 0 || slot >= MAX_SLOTS) {
            return 0;
        }

        std::shared_lock lock(m_mutex);
        return m_slots[slot].recoveryAttempts;
    }

}  // namespace IntelEngine
