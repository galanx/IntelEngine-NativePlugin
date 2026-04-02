/**
 * Slot Tracker Implementation
 *
 * C++ mirror of Papyrus slot state for synchronous SkyrimNet decorator access.
 * Papyrus pushes state changes via native functions; C++ reads from here.
 */

#include "SlotTracker.h"
#include "SkyrimNetAPI.h"
#include <chrono>
#include <nlohmann/json.hpp>

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

        RE::FormID prevFormId = 0;
        RE::FormID newFormId = agent ? agent->GetFormID() : 0;
        {
            std::unique_lock lock(m_mutex);
            auto& s = m_slots[slot];
            if (s.agent && s.agent != agent) {
                prevFormId = s.agent->GetFormID();
            }
            s.agent = agent;
            s.state = state;
            s.taskType = taskType;
            s.targetName = targetName;
        }

        // SkyrimNet busy API calls outside lock (avoids lock ordering issues)
        if (prevFormId && SkyrimNetAPI::ClearActorBusy) {
            SkyrimNetAPI::ClearActorBusy(prevFormId);
        }

        // States where the NPC is idle (arrived, lingering, waiting) — clear busy so they
        // can accept new tasks without the player needing to cancel first.
        // State 1 = traveling, 3 = returning — these are active movement, keep busy.
        // State 2 = waiting at dest, 5 = group wait, 8 = at-target interaction — idle, clear busy.
        bool isIdleState = (state == 2 || state == 5 || state == 8 || state == 0);
        if (newFormId && isIdleState && SkyrimNetAPI::ClearActorBusy) {
            SkyrimNetAPI::ClearActorBusy(newFormId);
        } else if (newFormId && !isIdleState && SkyrimNetAPI::SetActorBusy) {
            // Human-readable reason so busy_reason() decorator gives the LLM useful context
            std::string reason;
            if (taskType == "travel") reason = "traveling to " + targetName;
            else if (taskType == "fetch_npc") reason = "fetching " + targetName;
            else if (taskType == "deliver_message") reason = "delivering a message to " + targetName;
            else if (taskType == "escort_target") reason = "escorting " + targetName;
            else if (taskType == "search_for_actor") reason = "searching for " + targetName;
            else if (taskType == "assassination") reason = "on an assassination task";
            else reason = taskType + ": " + targetName;
            SkyrimNetAPI::SetActorBusy(newFormId, reason.c_str());
        }

        logger::debug("SlotTracker: Updated slot {} -> agent={}, state={}, type={}, target={}",
                     slot, agent ? agent->GetDisplayFullName() : "null", state, taskType, targetName);
    }

    void SlotTracker::ClearSlot(int slot) {
        if (slot < 0 || slot >= MAX_SLOTS) return;

        RE::FormID busyFormId = 0;
        {
            std::unique_lock lock(m_mutex);
            auto& s = m_slots[slot];
            if (s.agent) {
                busyFormId = s.agent->GetFormID();
            }
            s.agent = nullptr;
            s.state = 0;
            s.taskType.clear();
            s.targetName.clear();
            // Note: cooldown is NOT cleared here — it's per-actor, not per-slot
        }

        // Clear busy outside lock
        if (busyFormId && SkyrimNetAPI::ClearActorBusy) {
            SkyrimNetAPI::ClearActorBusy(busyFormId);
        }

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

    std::string SlotTracker::SerializeToJson() const {
        std::shared_lock lock(m_mutex);

        float now = GetRealTimeSeconds();
        nlohmann::json slots = nlohmann::json::array();

        for (int i = 0; i < MAX_SLOTS; ++i) {
            const auto& s = m_slots[i];
            nlohmann::json slot;
            slot["index"] = i;
            slot["state"] = s.state;
            slot["taskType"] = s.taskType;
            slot["targetName"] = s.targetName;
            slot["agentName"] = (s.agent && s.state != 0)
                ? s.agent->GetDisplayFullName() : "";
            slot["agentFormId"] = (s.agent && s.state != 0)
                ? static_cast<int>(s.agent->GetFormID()) : 0;

            // Remaining cooldown in seconds (0 if not on cooldown)
            float cooldownRemaining = 0.0f;
            if (s.agent && s.cooldownExpiry > now) {
                cooldownRemaining = s.cooldownExpiry - now;
            } else if (s.agent) {
                auto it = m_cooldowns.find(s.agent->GetFormID());
                if (it != m_cooldowns.end() && it->second > now) {
                    cooldownRemaining = it->second - now;
                }
            }
            slot["cooldownRemaining"] = cooldownRemaining;

            slots.push_back(slot);
        }

        return slots.dump();
    }

    void SlotTracker::ClearAll() {
        // Collect busy actors under lock, then clear busy outside lock
        std::vector<RE::FormID> busyFormIds;
        {
            std::unique_lock lock(m_mutex);
            for (auto& s : m_slots) {
                if (s.agent) {
                    busyFormIds.push_back(s.agent->GetFormID());
                }
                s.agent = nullptr;
                s.state = 0;
                s.taskType.clear();
                s.targetName.clear();
                s.cooldownExpiry = 0.0f;
            }
            m_cooldowns.clear();
        }
        // Clear SkyrimNet busy state outside lock (avoids lock ordering issues)
        if (SkyrimNetAPI::ClearActorBusy) {
            for (auto formId : busyFormIds) {
                SkyrimNetAPI::ClearActorBusy(formId);
            }
        }
        logger::info("SlotTracker: All slots cleared ({} busy states released)", busyFormIds.size());
    }

}  // namespace IntelEngine
