#pragma once

/**
 * Slot Tracker - Authoritative C++ Task State
 *
 * Singleton that owns task slot state. Persisted via SKSE co-save serialization
 * so task recovery on game load happens in C++ without StorageUtil reads.
 * Papyrus arrays are synced FROM here on load (not the other way around).
 *
 * During gameplay, Papyrus pushes state changes via native functions
 * (UpdateSlot/ClearSlot). C++ decorators and eligibility callbacks read
 * synchronously from here for SkyrimNet integration.
 */

#include "Plugin.h"
#include <atomic>
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

        // Persisted via co-save for task recovery (eliminates StorageUtil dependency on load)
        RE::FormID agentFormID = 0;     // Serialized; agent pointer resolved on load
        int speed = 0;                  // 0=walk, 1=jog, 2=run
        float deadline = 0.0f;         // Game time deadline
        float offscreenArrival = 0.0f; // Game time estimate for off-screen travel
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

        // --- Persistence (SKSE co-save) ---

        /** Save slot state to SKSE co-save. Called from Plugin SaveCallback. */
        void Save(SKSE::SerializationInterface* a_intfc);

        /** Load slot state from SKSE co-save. Called from Plugin LoadCallback.
         *  Resolves FormIDs for load-order changes, validates actors. */
        void Load(SKSE::SerializationInterface* a_intfc);

        /** Returns true if slot state was loaded from co-save (skip StorageUtil recovery). */
        bool HasCoSaveData() const { return m_hasCoSaveData.load(std::memory_order_acquire); }

        /** Build a human-readable busy reason from task type/target.
         *  Used by UpdateSlot and kPostLoadGame busy sync — single source of truth. */
        static std::string BuildBusyReason(const std::string& taskType, const std::string& targetName);

        // --- Per-field setters (called from Papyrus during gameplay) ---

        void SetSlotSpeed(int slot, int speed);
        void SetSlotDeadline(int slot, float deadline);
        void SetSlotOffscreenArrival(int slot, float arrival);

        /** Thread-safe snapshot of slot data (for SyncArraysFromSlotTracker / busy sync). */
        SlotData GetSlotDataCopy(int slot) const {
            std::shared_lock lock(m_mutex);
            if (slot < 0 || slot >= MAX_SLOTS) return {};
            return m_slots[slot];
        }

    private:
        SlotTracker() = default;

        mutable std::shared_mutex m_mutex;
        std::array<SlotData, MAX_SLOTS> m_slots;
        std::unordered_map<RE::FormID, float> m_cooldowns;  // FormID -> real-time expiry
        std::atomic<bool> m_hasCoSaveData{false};

    public:
        // Load generation counter — incremented on each RevertCallback to cancel
        // stale deferred Maintenance dispatches from previous loads.
        std::atomic<uint32_t> loadGeneration{0};
    };

}  // namespace IntelEngine
