#pragma once

#include "StoryCandidateResolution.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>

namespace IntelEngine {
    struct NPCTickResult {
        std::uint32_t loadGeneration = 0;
        std::string context;
        StoryCandidatePool candidates;

        // Caller serializes publication with load/revert on the main thread.
        bool Publish(const std::atomic<std::uint32_t>& currentGeneration, StoryCandidatePool& pool) {
            if (loadGeneration != currentGeneration.load(std::memory_order_relaxed)) return false;
            pool = std::move(candidates);
            return true;
        }
    };
}
