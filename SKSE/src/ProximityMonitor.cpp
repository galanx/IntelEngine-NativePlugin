#include "ProximityMonitor.h"
#include <cmath>
#include <chrono>

namespace IntelEngine {

    void ProximityMonitor::Start() {
        if (m_running.load(std::memory_order_acquire)) return;

        // Ensure previous thread is fully joined before spawning a new one
        // (assigning to a joinable std::thread calls std::terminate)
        if (m_thread.joinable()) {
            m_thread.join();
        }

        m_running.store(true, std::memory_order_release);
        m_thread = std::thread([this]() {
            logger::info("ProximityMonitor: Started (500ms interval)");
            while (m_running.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (!m_running.load(std::memory_order_acquire)) break;

                auto* task = SKSE::GetTaskInterface();
                if (task) {
                    task->AddTask([this]() {
                        Tick();
                    });
                }
            }
            logger::info("ProximityMonitor: Stopped");
        });
    }

    void ProximityMonitor::Stop() {
        m_running.store(false, std::memory_order_release);
        if (m_thread.joinable()) {
            m_thread.join();
        }
        ClearAll();
    }

    void ProximityMonitor::RegisterDistanceWatch(int id, RE::FormID source, RE::FormID target,
                                                  float threshold, const std::string& eventType,
                                                  bool greaterThan, float zTolerance, bool oneShot) {
        std::unique_lock lock(m_mutex);
        // Remove existing watch with same id + eventType (prevent duplicates)
        m_watches.erase(
            std::remove_if(m_watches.begin(), m_watches.end(),
                [&](const Watch& w) { return w.id == id && w.eventType == eventType; }),
            m_watches.end());

        Watch w{};
        w.id = id;
        w.type = WatchType::Distance;
        w.direction = greaterThan ? WatchDirection::GreaterThan : WatchDirection::LessThan;
        w.sourceFormID = source;
        w.targetFormID = target;
        w.threshold = threshold;
        w.zTolerance = zTolerance;
        w.eventType = eventType;
        w.oneShot = oneShot;
        w.fired = false;
        m_watches.push_back(std::move(w));

        logger::debug("ProximityMonitor: Registered distance watch id={} type={} threshold={} dir={}",
                      id, eventType, threshold, greaterThan ? "GT" : "LT");
    }

    void ProximityMonitor::RegisterPlayerWatch(int id, RE::FormID source, float threshold,
                                               const std::string& eventType,
                                               bool greaterThan, bool oneShot) {
        std::unique_lock lock(m_mutex);
        m_watches.erase(
            std::remove_if(m_watches.begin(), m_watches.end(),
                [&](const Watch& w) { return w.id == id && w.eventType == eventType; }),
            m_watches.end());

        Watch w{};
        w.id = id;
        w.type = WatchType::PlayerDistance;
        w.direction = greaterThan ? WatchDirection::GreaterThan : WatchDirection::LessThan;
        w.sourceFormID = source;
        w.targetFormID = 0;
        w.threshold = threshold;
        w.eventType = eventType;
        w.oneShot = oneShot;
        w.fired = false;
        m_watches.push_back(std::move(w));

        logger::debug("ProximityMonitor: Registered player watch id={} type={} threshold={} dir={}",
                      id, eventType, threshold, greaterThan ? "GT" : "LT");
    }

    void ProximityMonitor::RegisterDeadlineWatch(int id, float gameTime,
                                                  const std::string& eventType, bool oneShot) {
        std::unique_lock lock(m_mutex);
        m_watches.erase(
            std::remove_if(m_watches.begin(), m_watches.end(),
                [&](const Watch& w) { return w.id == id && w.eventType == eventType; }),
            m_watches.end());

        Watch w{};
        w.id = id;
        w.type = WatchType::Deadline;
        w.direction = WatchDirection::GreaterThan;  // currentTime >= deadline
        w.sourceFormID = 0;
        w.targetFormID = 0;
        w.threshold = gameTime;
        w.eventType = eventType;
        w.oneShot = oneShot;
        w.fired = false;
        m_watches.push_back(std::move(w));

        logger::debug("ProximityMonitor: Registered deadline watch id={} type={} gameTime={}",
                      id, eventType, gameTime);
    }

    void ProximityMonitor::ClearWatches(int id) {
        std::unique_lock lock(m_mutex);
        auto before = m_watches.size();
        m_watches.erase(
            std::remove_if(m_watches.begin(), m_watches.end(),
                [id](const Watch& w) { return w.id == id; }),
            m_watches.end());
        auto removed = before - m_watches.size();
        if (removed > 0) {
            logger::debug("ProximityMonitor: Cleared {} watches for id={}", removed, id);
        }
    }

    void ProximityMonitor::ClearWatch(int id, const std::string& eventType) {
        std::unique_lock lock(m_mutex);
        m_watches.erase(
            std::remove_if(m_watches.begin(), m_watches.end(),
                [&](const Watch& w) { return w.id == id && w.eventType == eventType; }),
            m_watches.end());
    }

    void ProximityMonitor::ClearAll() {
        std::unique_lock lock(m_mutex);
        auto count = m_watches.size();
        m_watches.clear();
        if (count > 0) {
            logger::info("ProximityMonitor: Cleared all {} watches", count);
        }
    }

    int ProximityMonitor::GetActiveWatchCount() const {
        std::shared_lock lock(m_mutex);
        return static_cast<int>(m_watches.size());
    }

    void ProximityMonitor::Tick() {
        // Snapshot watches under lock, then process without lock
        // (engine position reads and ModEvent sends must not hold our lock)
        std::vector<Watch> snapshot;
        {
            std::shared_lock lock(m_mutex);
            if (m_watches.empty()) return;
            snapshot = m_watches;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        // Get game time once for all deadline checks
        auto* calendar = RE::Calendar::GetSingleton();
        float gameTime = calendar ? calendar->GetCurrentGameTime() : 0.0f;

        std::vector<int> firedIndices;

        for (int i = 0; i < static_cast<int>(snapshot.size()); ++i) {
            auto& w = snapshot[i];
            if (w.fired) continue;

            bool triggered = false;

            if (w.type == WatchType::Deadline) {
                // Game time comparison
                if (gameTime >= w.threshold) {
                    triggered = true;
                }
            } else {
                // Distance check — resolve refs
                RE::TESObjectREFR* sourceRef = nullptr;
                RE::TESObjectREFR* targetRef = nullptr;

                if (w.sourceFormID) {
                    sourceRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(w.sourceFormID);
                }
                if (w.type == WatchType::PlayerDistance) {
                    targetRef = player;
                } else if (w.targetFormID) {
                    targetRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(w.targetFormID);
                }

                // Both refs must exist and have 3D loaded
                if (!sourceRef || !targetRef) continue;
                if (!sourceRef->Is3DLoaded() || !targetRef->Is3DLoaded()) continue;

                auto posA = sourceRef->GetPosition();
                auto posB = targetRef->GetPosition();

                // Z tolerance check (multi-floor interior validation)
                if (w.zTolerance > 0.0f) {
                    float zDiff = std::abs(posA.z - posB.z);
                    if (zDiff > w.zTolerance) continue;  // Wrong floor, skip
                }

                float dx = posA.x - posB.x;
                float dy = posA.y - posB.y;
                float dz = posA.z - posB.z;
                float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

                if (w.direction == WatchDirection::LessThan) {
                    triggered = (dist < w.threshold);
                } else {
                    triggered = (dist > w.threshold);
                }
            }

            if (triggered) {
                firedIndices.push_back(i);
            }
        }

        // Fire events and clean up — needs write lock
        if (!firedIndices.empty()) {
            // Fire events first (outside lock)
            for (int idx : firedIndices) {
                FireEvent(snapshot[idx]);
            }

            // Now remove one-shot watches under write lock
            std::unique_lock lock(m_mutex);
            for (int idx : firedIndices) {
                const auto& fired = snapshot[idx];
                if (fired.oneShot) {
                    m_watches.erase(
                        std::remove_if(m_watches.begin(), m_watches.end(),
                            [&](const Watch& w) {
                                return w.id == fired.id && w.eventType == fired.eventType;
                            }),
                        m_watches.end());
                } else {
                    // Mark as fired in the actual vector (for persistent watches)
                    for (auto& w : m_watches) {
                        if (w.id == fired.id && w.eventType == fired.eventType) {
                            w.fired = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    void ProximityMonitor::FireEvent(const Watch& watch) {
        auto* eventSource = SKSE::GetModCallbackEventSource();
        if (!eventSource) return;

        SKSE::ModCallbackEvent modEvent{};
        modEvent.eventName = "IntelEngine_ProximityEvent";
        modEvent.strArg = watch.eventType;
        modEvent.numArg = static_cast<float>(watch.id);

        // Set sender to the source actor if available
        if (watch.sourceFormID) {
            modEvent.sender = RE::TESForm::LookupByID(watch.sourceFormID);
        }

        eventSource->SendEvent(&modEvent);

        logger::info("ProximityMonitor: Fired '{}' for id={} (source={:X})",
                     watch.eventType, watch.id, watch.sourceFormID);
    }

}  // namespace IntelEngine
