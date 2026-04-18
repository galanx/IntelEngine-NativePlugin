/**
 * Proximity Monitor Implementation
 *
 * Main-thread distance checks at 150ms cadence. See header for rationale.
 */

#include "ProximityMonitor.h"
#include "AsyncDispatch.h"

#include <chrono>
#include <cmath>

namespace IntelEngine {

    void ProximityMonitor::Arm(int slot, RE::TESObjectREFR* agent, RE::TESObjectREFR* target,
                               float threshold, float zTolerance,
                               const std::string& questEditorId,
                               const std::string& scriptName,
                               const std::string& callbackFn) {
        if (slot < 0 || slot >= MAX_SLOTS) {
            logger::warn("ProximityMonitor::Arm: invalid slot {}", slot);
            return;
        }
        if (!agent || !target) {
            logger::warn("ProximityMonitor::Arm: null agent or target for slot {}", slot);
            return;
        }

        // Auto-select defaults based on target kind. Actor targets need a tighter
        // conversational threshold and a tighter Z window (an NPC a floor above
        // the player shouldn't fire arrival). Marker targets (doors, furniture)
        // use the legacy Papyrus 300u radius and a looser Z window because markers
        // are often placed at floor level while the navmesh endpoint sits higher.
        const bool targetIsActor = target->As<RE::Actor>() != nullptr;
        if (threshold <= 0.0f) {
            threshold = targetIsActor ? DEFAULT_ACTOR_THRESHOLD : DEFAULT_MARKER_THRESHOLD;
        }
        if (zTolerance <= 0.0f) {
            zTolerance = targetIsActor ? DEFAULT_ACTOR_Z_TOLERANCE : DEFAULT_MARKER_Z_TOLERANCE;
        }

        std::unique_lock lock(m_mutex);
        auto& w = m_slots[slot];
        w.armed = true;
        w.agentFormID = agent->GetFormID();
        w.targetFormID = target->GetFormID();
        w.threshold = threshold;
        w.zTolerance = zTolerance;
        w.questEditorId = questEditorId;
        w.scriptName = scriptName;
        w.callbackFn = callbackFn;

        logger::info("ProximityMonitor::Arm slot={} agent={:08X} target={:08X} "
                     "actor={} threshold={:.0f} z={:.0f} -> {}::{}",
                     slot, w.agentFormID, w.targetFormID, targetIsActor,
                     w.threshold, w.zTolerance, scriptName, callbackFn);
    }

    void ProximityMonitor::Disarm(int slot) {
        if (slot < 0 || slot >= MAX_SLOTS) return;

        std::unique_lock lock(m_mutex);
        auto& w = m_slots[slot];
        if (!w.armed) return;

        logger::info("ProximityMonitor::Disarm slot={}", slot);
        w = Watch{};
    }

    void ProximityMonitor::DisarmAll() {
        std::unique_lock lock(m_mutex);
        for (auto& w : m_slots) w = Watch{};
        logger::info("ProximityMonitor::DisarmAll");
    }

    void ProximityMonitor::Start() {
        bool expected = false;
        if (!m_running.compare_exchange_strong(expected, true)) return;
        m_thread = std::thread(&ProximityMonitor::WorkerLoop, this);
        logger::info("ProximityMonitor: worker started (tick={}ms)", TICK_INTERVAL_MS);
    }

    void ProximityMonitor::Stop() {
        bool expected = true;
        if (!m_running.compare_exchange_strong(expected, false)) return;
        if (m_thread.joinable()) m_thread.join();
    }

    void ProximityMonitor::WorkerLoop() {
        // Sleep-poll loop. Each wake submits a main-thread task. RE::* getters
        // (GetPosition, Is3DLoaded, LookupByID) are not thread-safe, so all
        // the work happens in Tick() on the main thread.
        while (m_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(TICK_INTERVAL_MS));
            if (!m_running.load(std::memory_order_acquire)) break;

            auto* task = SKSE::GetTaskInterface();
            if (!task) continue;

            task->AddTask([]() {
                ProximityMonitor::GetSingleton()->Tick();
            });
        }
    }

    /**
     * Per-tick distance check on the main thread.
     *
     * 1. Take a snapshot of armed slots under shared_lock (fast, no writers blocked).
     * 2. Early-out if nothing is armed — the common case is zero active dispatches.
     * 3. For each armed slot, resolve agent+target via FormID lookup (returns nullptr
     *    if the form was deleted). Skip until both references are 3D-loaded; off-screen
     *    arrival is handled by the Papyrus 3s poll's time-based estimator (OffScreenTracker).
     * 4. Compute horizontal (XY) distance and absolute Z delta. Fire when both are
     *    within the slot's threshold.
     * 5. On fire: claim the slot by clearing its armed flag under unique_lock and
     *    capture the callback, then invoke the Papyrus VM synchronously. Clearing
     *    inside the lock makes this a single-shot per Arm without needing a separate
     *    fired flag; a concurrent Arm will re-enable the slot with fresh state.
     *
     * Post-Stop() safety: the worker thread is joined, but any Tick already queued on
     * the SKSE task interface may run after Stop(). The m_running guard at entry
     * prevents that queued task from touching slot state during teardown.
     */
    void ProximityMonitor::Tick() {
        if (!m_running.load(std::memory_order_acquire)) return;

        std::array<Watch, MAX_SLOTS> snapshot;
        bool anyArmed = false;
        {
            std::shared_lock lock(m_mutex);
            for (int i = 0; i < MAX_SLOTS; ++i) {
                snapshot[i] = m_slots[i];
                if (snapshot[i].armed) anyArmed = true;
            }
        }
        if (!anyArmed) return;

        for (int slot = 0; slot < MAX_SLOTS; ++slot) {
            const Watch& w = snapshot[slot];
            if (!w.armed) continue;

            auto* agent = RE::TESForm::LookupByID<RE::TESObjectREFR>(w.agentFormID);
            auto* target = RE::TESForm::LookupByID<RE::TESObjectREFR>(w.targetFormID);
            if (!agent || !target) continue;

            // Both must be in the rendered world. Off-screen arrival is handled by
            // Papyrus's game-time estimator in OffScreenTracker.
            if (!agent->Is3DLoaded() || !target->Is3DLoaded()) continue;

            auto aPos = agent->GetPosition();
            auto tPos = target->GetPosition();
            float dx = aPos.x - tPos.x;
            float dy = aPos.y - tPos.y;
            float horiz = std::sqrt(dx * dx + dy * dy);
            float dz = std::fabs(aPos.z - tPos.z);

            if (horiz > w.threshold || dz > w.zTolerance) continue;

            // Claim + disarm atomically; a concurrent Arm on the same slot would
            // have bumped the agentFormID, so we re-check to avoid firing a stale
            // callback against the new watch's agent.
            std::string qeid, script, fn;
            {
                std::unique_lock lk(m_mutex);
                auto& live = m_slots[slot];
                if (!live.armed || live.agentFormID != w.agentFormID ||
                    live.targetFormID != w.targetFormID) {
                    continue;
                }
                qeid = live.questEditorId;
                script = live.scriptName;
                fn = live.callbackFn;
                live = Watch{};  // single-shot per Arm
            }

            logger::info("ProximityMonitor: slot {} arrived agent={:08X} target={:08X} "
                         "horiz={:.1f}u dz={:.1f}u -> {}::{}",
                         slot, w.agentFormID, w.targetFormID, horiz, dz, script, fn);

            AsyncDispatch::ExecuteQuestFunctionString(qeid, script, fn, std::to_string(slot));
        }
    }

}  // namespace IntelEngine
