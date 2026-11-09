#pragma once
#include <atomic>
inline std::atomic_bool intelEngineStoryLoadRecovered{false};
inline bool IntelEngineStoryLoadRecovered(bool complete) { if (complete) intelEngineStoryLoadRecovered.store(true); return intelEngineStoryLoadRecovered.load(); }
