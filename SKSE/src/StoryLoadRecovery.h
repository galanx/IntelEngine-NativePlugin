#pragma once
#include <atomic>
inline std::atomic_bool intelEngineStoryLoadRecovered{false};
inline bool IntelEngineStoryLoadRecovered(bool complete) { if (complete) intelEngineStoryLoadRecovered.store(true); return intelEngineStoryLoadRecovered.load(); }

// Which load of which session this is: a number no other load has (the second the game's session began, and a count
// of its loads). The scripts keep times of the game's own clock of real seconds, which restarts with the game, in
// the save; such a time means something only when it was taken after this very load (Core.Maintenance's guard
// against being called twice took a time of an earlier session for a call a moment ago, and skipped the load).
#include <chrono>
inline std::atomic_int intelEngineLoadStamp{0};
inline int IntelEngineNewLoad() {
    static const int session = static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() % 2000000);
    static std::atomic_int loads{0};
    const int stamp = session * 1000 + (loads.fetch_add(1) + 1) % 1000;
    intelEngineLoadStamp.store(stamp);
    return stamp;
}
