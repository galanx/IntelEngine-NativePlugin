#pragma once

/**
 * Slot Tracker - C++ Mirror of Papyrus Slot State
 *
 * Lightweight singleton that mirrors the task slot state from Papyrus arrays.
 * Papyrus pushes state changes via native functions (UpdateSlot/ClearSlot).
 * C++ decorators and eligibility callbacks read from here.
 *
 * This avoids the need to read StorageUtil from C++ (not directly accessible)
 * and provides synchronous access for SkyrimNet decorator callbacks.
 */

#include "Plugin.h"
#include <shared_mutex>
#include <unordered_map>
#include <string>
#include <array>

namespace IntelEngine {

    static constexpr int MAX_SLOTS = 5;

    struct SlotData {
        RE::Actor* agent = nullptr;
        int state = 0;          // 0=empty, 1=traveling, 2=at_destination, 3=returning, 5=search_wait, 8=at_target
        std::string taskType;   // "travel", "fetch_npc", "deliver_message", "search_for_actor"
        std::string targetName; // Target NPC/location name
        float cooldownExpiry = 0.0f;  // Real-time expiry for cooldown check
    };

    class SlotTracker {
    public:
        static SlotTracker* GetSingleton() {
            static SlotTracker instance;
            return &instance;
        }

        /**
         * Update a slot with new state from Papyrus.
         * Called by native function when AllocateSlot/SetSlotState fires.
         */
        void UpdateSlot(int slot, RE::Actor* agent, int state,
                       const std::string& taskType, const std::string& targetName);

        /**
         * Clear a slot. Called by native function when ClearSlot fires.
         */
        void ClearSlot(int slot);

        /**
         * Set cooldown expiry for an actor. Called when task completes.
         */
        void SetCooldown(RE::Actor* actor, float durationSeconds);

        /**
         * Check if an actor has an active task.
         */
        bool HasActiveTask(RE::Actor* actor) const;

        /**
         * Check if an actor is on cooldown.
         */
        bool IsOnCooldown(RE::Actor* actor) const;

        /**
         * Get the task type for an actor, or empty string.
         */
        std::string GetTaskType(RE::Actor* actor) const;

        /**
         * Get the target name for an actor's current task.
         */
        std::string GetTargetName(RE::Actor* actor) const;

        /**
         * Find slot by actor. Returns -1 if not found.
         */
        int FindSlotByActor(RE::Actor* actor) const;

        /**
         * Serialize all slots to JSON for the dashboard UI.
         * Returns a JSON array of slot objects.
         */
        std::string SerializeToJson() const;

        /**
         * Clear all slots (on game load / new game).
         */
        void ClearAll();

    private:
        SlotTracker() = default;

        mutable std::shared_mutex m_mutex;
        std::array<SlotData, MAX_SLOTS> m_slots;
        std::unordered_map<RE::FormID, float> m_cooldowns;  // FormID -> real-time expiry
    };

}  // namespace IntelEngine
