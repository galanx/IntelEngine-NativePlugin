// Standalone C++20 check; no game ABI or test framework.
#include "NPCTickResult.h"
#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using namespace IntelEngine;
    std::atomic<std::uint32_t> generation{1};
    StoryCandidatePool pool;
    NPCTickResult first{1, "context", {{"aela", 1}, {"aela", 2}}};
    Require(first.Publish(generation, pool), "current tick must publish");
    Require(pool.count("aela") == 2, "publication must preserve duplicate-name ambiguity");
    NPCTickResult inFlight{1, "old context", {{"torvar", 3}}};
    generation.fetch_add(1, std::memory_order_relaxed);
    NPCTickResult revert{2, {}, {}};
    Require(revert.Publish(generation, pool) && pool.empty(), "revert must clear old candidates");
    NPCTickResult fresh{2, "new context", {{"jenassa", 4}}};
    Require(fresh.Publish(generation, pool), "new-save tick must publish");
    Require(!inFlight.Publish(generation, pool), "pre-load result must reject after fresh publication");
    Require(pool.size() == 1 && pool.find("jenassa")->second == 4,
            "stale publication must not overwrite the new-save pool");
    generation.fetch_add(1, std::memory_order_relaxed);
    Require(!fresh.Publish(generation, pool), "a second load must invalidate a queued callback result");
    NPCTickResult empty{3, {}, {}};
    Require(empty.Publish(generation, pool) && pool.empty(), "current empty tick must clear previous pool");
    std::cout << "PASS: current/empty publication, duplicate preservation, revert clearing and stale-result rejection across loads\n";
}
