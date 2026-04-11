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
            s.agentFormID = newFormId;
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
            auto reason = BuildBusyReason(taskType, targetName);
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
            s.agentFormID = 0;
            s.state = 0;
            s.taskType.clear();
            s.targetName.clear();
            s.speed = 0;
            s.deadline = 0.0f;
            s.offscreenArrival = 0.0f;
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
                s.agentFormID = 0;
                s.state = 0;
                s.taskType.clear();
                s.targetName.clear();
                s.cooldownExpiry = 0.0f;
                s.speed = 0;
                s.deadline = 0.0f;
                s.offscreenArrival = 0.0f;
            }
            m_cooldowns.clear();
            m_hasCoSaveData.store(false, std::memory_order_release);
        }
        // Clear SkyrimNet busy state outside lock (avoids lock ordering issues)
        if (SkyrimNetAPI::ClearActorBusy) {
            for (auto formId : busyFormIds) {
                SkyrimNetAPI::ClearActorBusy(formId);
            }
        }
        logger::info("SlotTracker: All slots cleared ({} busy states released)", busyFormIds.size());
    }

    std::string SlotTracker::BuildBusyReason(const std::string& taskType, const std::string& targetName) {
        if (taskType == "travel") return "traveling to " + targetName;
        if (taskType == "fetch_npc") return "fetching " + targetName;
        if (taskType == "deliver_message") return "delivering a message to " + targetName;
        if (taskType == "escort_target") return "escorting " + targetName;
        if (taskType == "search_for_actor") return "searching for " + targetName;
        if (taskType == "assassination") return "on an assassination task";
        if (taskType == "npc_social") return "going to talk with " + targetName;
        if (taskType == "story") return "heading to speak with " + targetName;
        if (taskType == "story_npc") return "going to meet " + targetName;
        return taskType + ": " + targetName;
    }

    // --- Persistence ---

    static constexpr uint32_t SLOT_RECORD_TYPE = 'IETK';
    static constexpr uint32_t SLOT_RECORD_VERSION = 1;

    void SlotTracker::Save(SKSE::SerializationInterface* a_intfc) {
        if (!a_intfc->OpenRecord(SLOT_RECORD_TYPE, SLOT_RECORD_VERSION)) {
            logger::error("SlotTracker::Save: Failed to open IETK record");
            return;
        }

        std::shared_lock lock(m_mutex);

        int activeCount = 0;
        for (int i = 0; i < MAX_SLOTS; ++i) {
            const auto& s = m_slots[i];
            // Serialize: slot index, formID, state, taskType, targetName, speed, deadline, offscreenArrival
            uint32_t formID = s.agentFormID;
            int32_t state = s.state;
            int32_t speed = s.speed;
            float deadline = s.deadline;
            float offscreen = s.offscreenArrival;

            a_intfc->WriteRecordData(&formID, sizeof(formID));
            a_intfc->WriteRecordData(&state, sizeof(state));

            uint32_t typeLen = static_cast<uint32_t>(s.taskType.size());
            a_intfc->WriteRecordData(&typeLen, sizeof(typeLen));
            if (typeLen > 0) a_intfc->WriteRecordData(s.taskType.data(), typeLen);

            uint32_t nameLen = static_cast<uint32_t>(s.targetName.size());
            a_intfc->WriteRecordData(&nameLen, sizeof(nameLen));
            if (nameLen > 0) a_intfc->WriteRecordData(s.targetName.data(), nameLen);

            a_intfc->WriteRecordData(&speed, sizeof(speed));
            a_intfc->WriteRecordData(&deadline, sizeof(deadline));
            a_intfc->WriteRecordData(&offscreen, sizeof(offscreen));

            if (state != 0) activeCount++;
        }

        logger::info("SlotTracker::Save: Wrote {} slots ({} active)", MAX_SLOTS, activeCount);
    }

    void SlotTracker::Load(SKSE::SerializationInterface* a_intfc) {
        // Phase 1: Read raw data from co-save (no lock needed — serialization is single-threaded)
        struct RawSlot {
            uint32_t formID = 0;
            int32_t state = 0;
            std::string taskType;
            std::string targetName;
            int32_t speed = 0;
            float deadline = 0.0f;
            float offscreen = 0.0f;
        };
        std::array<RawSlot, MAX_SLOTS> raw{};

        for (int i = 0; i < MAX_SLOTS; ++i) {
            auto& r = raw[i];
            if (!a_intfc->ReadRecordData(&r.formID, sizeof(r.formID))) break;
            if (!a_intfc->ReadRecordData(&r.state, sizeof(r.state))) break;

            uint32_t typeLen = 0;
            if (!a_intfc->ReadRecordData(&typeLen, sizeof(typeLen))) break;
            if (typeLen > 0) {
                if (typeLen >= 256) {
                    logger::error("SlotTracker::Load: taskType length {} too large for slot {}, aborting", typeLen, i);
                    break;
                }
                r.taskType.resize(typeLen);
                if (!a_intfc->ReadRecordData(r.taskType.data(), typeLen)) break;
            }

            uint32_t nameLen = 0;
            if (!a_intfc->ReadRecordData(&nameLen, sizeof(nameLen))) break;
            if (nameLen > 0) {
                if (nameLen >= 256) {
                    logger::error("SlotTracker::Load: targetName length {} too large for slot {}, aborting", nameLen, i);
                    break;
                }
                r.targetName.resize(nameLen);
                if (!a_intfc->ReadRecordData(r.targetName.data(), nameLen)) break;
            }

            if (!a_intfc->ReadRecordData(&r.speed, sizeof(r.speed))) break;
            if (!a_intfc->ReadRecordData(&r.deadline, sizeof(r.deadline))) break;
            if (!a_intfc->ReadRecordData(&r.offscreen, sizeof(r.offscreen))) break;
        }

        // Phase 2: Resolve FormIDs and populate slots (engine calls outside lock)
        int recovered = 0;
        for (int i = 0; i < MAX_SLOTS; ++i) {
            const auto& r = raw[i];
            if (r.formID == 0 || r.state == 0) continue;

            uint32_t resolvedFormID = 0;
            if (!a_intfc->ResolveFormID(r.formID, resolvedFormID)) {
                logger::warn("SlotTracker::Load: Failed to resolve FormID {:X} for slot {}", r.formID, i);
                continue;
            }

            auto* actor = RE::TESForm::LookupByID<RE::Actor>(resolvedFormID);
            if (!actor || actor->IsDead()) {
                logger::warn("SlotTracker::Load: Actor {:X} not found or dead for slot {}", resolvedFormID, i);
                continue;
            }

            // Write to slot under lock
            {
                std::unique_lock lock(m_mutex);
                auto& s = m_slots[i];
                s.agent = actor;
                s.agentFormID = resolvedFormID;
                s.state = r.state;
                s.taskType = r.taskType;
                s.targetName = r.targetName;
                s.speed = r.speed;
                s.deadline = r.deadline;
                s.offscreenArrival = r.offscreen;
            }
            recovered++;

            logger::info("SlotTracker::Load: Recovered slot {} -> {} ({}), state={}, target={}",
                        i, actor->GetDisplayFullName(), r.taskType, r.state, r.targetName);
        }

        m_hasCoSaveData.store(true, std::memory_order_release);
        logger::info("SlotTracker::Load: Recovered {} active slots from co-save", recovered);
    }

    // --- Per-field setters ---

    void SlotTracker::SetSlotSpeed(int slot, int speed) {
        if (slot < 0 || slot >= MAX_SLOTS) return;
        std::unique_lock lock(m_mutex);
        m_slots[slot].speed = speed;
    }

    void SlotTracker::SetSlotDeadline(int slot, float deadline) {
        if (slot < 0 || slot >= MAX_SLOTS) return;
        std::unique_lock lock(m_mutex);
        m_slots[slot].deadline = deadline;
    }

    void SlotTracker::SetSlotOffscreenArrival(int slot, float arrival) {
        if (slot < 0 || slot >= MAX_SLOTS) return;
        std::unique_lock lock(m_mutex);
        m_slots[slot].offscreenArrival = arrival;
    }

}  // namespace IntelEngine
