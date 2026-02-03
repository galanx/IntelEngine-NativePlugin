/**
 * Departure Detection System Implementation
 *
 * Replaces ~50 lines of StorageUtil-based departure tracking in
 * IntelEngine_Core.psc with a native C++ singleton. Eliminates
 * 5-8 StorageUtil reads/writes per check cycle (every 3 seconds
 * per active slot).
 */

#include "DepartureDetector.h"
#include "Settings.h"

#include <cmath>

namespace IntelEngine {

    int DepartureDetector::CheckDepartureStatus(RE::Actor* actor, int slot, float threshold) {
        if (!actor || slot < 0 || slot >= MAX_SLOTS) {
            return 0;
        }

        std::unique_lock lock(m_mutex);
        auto& state = m_slots[slot];

        if (!state.active) {
            return 0;
        }

        if (state.departed) {
            return 1;
        }

        // Increment tick counter
        state.tickCount++;

        auto* settings = Settings::GetSingleton();
        if (state.tickCount < settings->departureMinChecks) {
            return 0;  // Too early — need more ticks before first distance check
        }

        // Compute 2D distance from start position
        auto pos = actor->GetPosition();
        float dx = pos.x - state.startX;
        float dy = pos.y - state.startY;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist >= threshold) {
            // NPC has moved past threshold — departed successfully
            state.departed = true;
            return 1;
        }

        // NPC hasn't moved — check retry count for escalation
        if (state.retries < settings->departureMaxRetries) {
            // Soft recovery: reset position baseline and tick counter for another round
            state.retries++;
            state.startX = pos.x;
            state.startY = pos.y;
            state.tickCount = 0;
            return 2;
        }

        // Retries exhausted — escalate to caller
        return 3;
    }

    void DepartureDetector::ResetSlot(int slot, RE::Actor* actor) {
        if (slot < 0 || slot >= MAX_SLOTS) {
            return;
        }

        std::unique_lock lock(m_mutex);
        auto& state = m_slots[slot];

        if (actor) {
            auto pos = actor->GetPosition();
            state.startX = pos.x;
            state.startY = pos.y;
            state.active = true;
        } else {
            state.startX = 0.0f;
            state.startY = 0.0f;
            state.active = false;
        }

        state.tickCount = 0;
        state.retries = 0;
        state.departed = false;
    }

    int DepartureDetector::GetRetryCount(int slot) const {
        if (slot < 0 || slot >= MAX_SLOTS) {
            return 0;
        }

        std::shared_lock lock(m_mutex);
        return m_slots[slot].retries;
    }

}  // namespace IntelEngine
