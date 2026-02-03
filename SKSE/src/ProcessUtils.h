#pragma once

/**
 * Process List Utilities
 *
 * Helper for iterating loaded actors across all four process list tiers
 * (high, middleHigh, middleLow, low) without duplicating the 4-loop pattern.
 */

#include "Plugin.h"

namespace IntelEngine::ProcessUtils {

    /**
     * Iterate all loaded actors across all 4 process list tiers.
     *
     * @param fn Callback receiving RE::Actor*. Return true to stop, false to continue.
     */
    template<typename Func>
    void ForEachLoadedActor(Func&& fn) {
        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return;

        auto iterateTier = [&fn](auto& handles) -> bool {
            for (auto& handle : handles) {
                auto actorPtr = handle.get();
                if (actorPtr) {
                    if (fn(actorPtr.get())) return true;
                }
            }
            return false;
        };

        if (iterateTier(processLists->highActorHandles)) return;
        if (iterateTier(processLists->middleHighActorHandles)) return;
        if (iterateTier(processLists->middleLowActorHandles)) return;
        iterateTier(processLists->lowActorHandles);
    }

}  // namespace IntelEngine::ProcessUtils
