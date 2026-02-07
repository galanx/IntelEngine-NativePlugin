/**
 * Slot Tracker Implementation
 *
 * C++ mirror of Papyrus slot state for synchronous SkyrimNet decorator access.
 * Papyrus pushes state changes via native functions; C++ reads from here.
 */

#include "SlotTracker.h"
#include <chrono>

namespace IntelEngine {

    // Helper: get monotonic real-time seconds (survives game pause)
    static float GetRealTimeSeconds() {
        using clock = std::chrono::steady_clock;
        static auto start = clock::now();
        auto now = clock::now();
        return std::chrono::duration<float>(now - start).count();
    }

    void SlotTracker::UpdateSlot(int slot, RE::Actor* agent, int state,
                                  const std::string& taskType, const std::string& targetName) {
        if (slot < 0 || slot >= MAX_SLOTS) return;

        std::unique_lock lock(m_mutex);
        auto& s = m_slots[slot];
        s.agent = agent;
        s.state = state;
        s.taskType = taskType;
        s.targetName = targetName;

        logger::debug("SlotTracker: Updated slot {} -> agent={}, state={}, type={}, target={}",
                     slot, agent ? agent->GetDisplayFullName() : "null", state, taskType, targetName);
    }

    void SlotTracker::ClearSlot(int slot) {
        if (slot < 0 || slot >= MAX_SLOTS) return;

        std::unique_lock lock(m_mutex);
        auto& s = m_slots[slot];
        s.agent = nullptr;
        s.state = 0;
        s.taskType.clear();
        s.targetName.clear();
        // Note: cooldown is NOT cleared here — it's per-actor, not per-slot

        logger::debug("SlotTracker: Cleared slot {}", slot);
    }

    void SlotTracker::SetCooldown(RE::Actor* actor, float durationSeconds) {
        if (!actor) return;

        std::unique_lock lock(m_mutex);

        float now = GetRealTimeSeconds();
        float expiry = now + durationSeconds;

        // Store on the slot data if actor has an active slot
        for (auto& s : m_slots) {
            if (s.agent == actor) {
                s.cooldownExpiry = expiry;
                break;
            }
        }

        // Also store in a separate map for post-clear lookups
        m_cooldowns[actor->GetFormID()] = expiry;

        logger::debug("SlotTracker: Cooldown set for {} -> {}s", actor->GetDisplayFullName(), durationSeconds);
    }

    bool SlotTracker::HasActiveTask(RE::Actor* actor) const {
        if (!actor) return false;

        std::shared_lock lock(m_mutex);
        for (const auto& s : m_slots) {
            if (s.agent == actor && s.state != 0) {
                return true;
            }
        }
        return false;
    }

    bool SlotTracker::IsOnCooldown(RE::Actor* actor) const {
        if (!actor) return false;

        std::shared_lock lock(m_mutex);

        float now = GetRealTimeSeconds();

        // Check per-slot cooldown
        for (const auto& s : m_slots) {
            if (s.agent == actor && s.cooldownExpiry > now) {
                return true;
            }
        }

        // Check post-clear cooldown map
        auto it = m_cooldowns.find(actor->GetFormID());
        if (it != m_cooldowns.end() && it->second > now) {
            return true;
        }

        return false;
    }

    std::string SlotTracker::GetTaskType(RE::Actor* actor) const {
        if (!actor) return "";

        std::shared_lock lock(m_mutex);
        for (const auto& s : m_slots) {
            if (s.agent == actor && s.state != 0) {
                return s.taskType;
            }
        }
        return "";
    }

    std::string SlotTracker::GetTargetName(RE::Actor* actor) const {
        if (!actor) return "";

        std::shared_lock lock(m_mutex);
        for (const auto& s : m_slots) {
            if (s.agent == actor && s.state != 0) {
                return s.targetName;
            }
        }
        return "";
    }

    int SlotTracker::FindSlotByActor(RE::Actor* actor) const {
        if (!actor) return -1;

        std::shared_lock lock(m_mutex);
        for (int i = 0; i < MAX_SLOTS; ++i) {
            if (m_slots[i].agent == actor) {
                return i;
            }
        }
        return -1;
    }

    void SlotTracker::ClearAll() {
        std::unique_lock lock(m_mutex);
        for (auto& s : m_slots) {
            s.agent = nullptr;
            s.state = 0;
            s.taskType.clear();
            s.targetName.clear();
            s.cooldownExpiry = 0.0f;
        }
        m_cooldowns.clear();
        logger::info("SlotTracker: All slots cleared");
    }

}  // namespace IntelEngine
