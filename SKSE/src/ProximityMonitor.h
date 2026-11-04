#pragma once

/**
 * Proximity Monitor
 *
 * Fast-path arrival detection for dispatched NPCs. Papyrus OnUpdate polls at
 * 3.0s, which lets a running NPC overshoot the arrival threshold by 900-1800
 * units before detection — NPCs bump into the player's face before Papyrus
 * fires OnArrival.
 *
 * This singleton polls every 150ms on the main thread (marshalled from an
 * owned worker thread via SKSE TaskInterface). When an armed slot's agent
 * enters the arrival threshold around its target, we dispatch a Papyrus
 * callback synchronously via AsyncDispatch::ExecuteQuestFunctionString.
 *
 * The 150ms cadence combines with a speed-aware actor threshold: base 200u
 * plus actual world speed times an estimated 0.50s response budget. Marker
 * targets retain their wider 300-unit threshold.
 *
 * Papyrus keeps its 3s poll for stuck detection, off-screen time-based
 * arrival estimation, and linger monitoring. Loaded actor-arrival checks use the
 * same speed-aware radial threshold so the callback is accepted by the state machine.
 */

#include "Plugin.h"

#include <array>
#include <atomic>
#include <shared_mutex>
#include <string>
#include <thread>

namespace IntelEngine {

    class ProximityMonitor {
    public:
        static constexpr int MAX_SLOTS = 5;

        /** Tick cadence — 150ms = ~7Hz. At run speed (300-600 u/s) NPC moves
         *  45-90 units between checks, within the 200u actor threshold. */
        static constexpr int TICK_INTERVAL_MS = 150;

        /** Default radial threshold for Actor->Actor arrivals, matching Papyrus GetDistance. */
        static constexpr float DEFAULT_ACTOR_THRESHOLD = 200.0f;

        /** Default horizontal threshold for Actor->Marker arrivals (doors, furniture).
         *  Wider than actor threshold because navmesh around markers often terminates
         *  with extra clearance, and the legacy Papyrus 300u check worked for them. */
        static constexpr float DEFAULT_MARKER_THRESHOLD = 300.0f;

        /** Z tolerance for actor targets. Tight enough to reject arrival when the
         *  agent is on a different floor (Skyrim interior floors are ~192-256u). */
        static constexpr float DEFAULT_ACTOR_Z_TOLERANCE = 120.0f;

        /** Z tolerance for marker targets. Generous because door/furniture markers
         *  are often placed at the floor while the pathfind endpoint is the doorway. */
        static constexpr float DEFAULT_MARKER_Z_TOLERANCE = 200.0f;

        static ProximityMonitor* GetSingleton() {
            static ProximityMonitor instance;
            return &instance;
        }

        /**
         * Arm proximity arrival for a slot. Replaces any existing watch on
         * the slot. The callback fires once when the agent enters the
         * threshold around the target with both references 3D-loaded.
         *
         * @param slot Slot index (0-4)
         * @param agent The moving NPC (must be 3D-loaded to fire)
         * @param target The thing being approached (actor or marker)
         * @param threshold Actor radial distance or marker XY distance to consider "arrived"
         * @param zTolerance Max |dZ| to consider same-floor
         * @param questEditorId Quest EditorID for VM dispatch (e.g. "IntelEngine_Quest")
         * @param scriptName Papyrus script name (e.g. "IntelEngine_Travel")
         * @param callbackFn Function to call with slot index as its String arg
         */
        void Arm(int slot, RE::TESObjectREFR* agent, RE::TESObjectREFR* target,
                 float threshold, float zTolerance,
                 const std::string& questEditorId,
                 const std::string& scriptName,
                 const std::string& callbackFn);

        /** Return the accepted threshold for a claimed callback, or zero if none. */
        float GetAcceptedThreshold(RE::FormID agentFormID, RE::FormID targetFormID) const;

        /** Disarm a specific slot. Safe to call on an unarmed slot. */
        void Disarm(int slot);

        /** Disarm all slots. Called on game load / revert. */
        void DisarmAll();

        /** Start the background worker thread. Idempotent. */
        void Start();

        /** Stop and join the worker thread. */
        void Stop();

        /** Main-thread tick: iterate armed slots, check distance, dispatch callbacks. */
        void Tick();

    private:
        ProximityMonitor() = default;
        ~ProximityMonitor() = default;
        ProximityMonitor(const ProximityMonitor&) = delete;
        ProximityMonitor& operator=(const ProximityMonitor&) = delete;

        struct Watch {
            bool armed = false;
            RE::FormID agentFormID = 0;
            RE::FormID targetFormID = 0;
            float threshold = DEFAULT_ACTOR_THRESHOLD;
            float acceptedThreshold = 0.0f;
            float zTolerance = DEFAULT_ACTOR_Z_TOLERANCE;
            std::string questEditorId;
            std::string scriptName;
            std::string callbackFn;
        };

        void WorkerLoop();

        std::array<Watch, MAX_SLOTS> m_slots{};
        mutable std::shared_mutex m_mutex;
        std::atomic<bool> m_running{false};
        std::thread m_thread;
    };

}  // namespace IntelEngine
