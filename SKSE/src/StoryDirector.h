#pragma once

// IntelEngine 4.0 staged director (the living-world levels; Classic keeps the single dungeon-master prompt of 3.8).
//
//  1. The code ranks the candidates as before (NPCIndex::RankStoryDMCandidates): bond, absence, rotation, novelty.
//  2. Each candidate is judged on their own, from their own context only, by SkyrimNet's decision model
//     (prompts/decisions/intel_story_reason.prompt): how strong a reason of their own they have to act now, and what
//     kind of moment fits. Without a decision provider the same questions go to the chat model
//     (intel_story_reason_chat.prompt). All candidates are asked at once.
//  3. The code picks who acts (StoryDirectorPick.h): a real reason or nobody.
//  4. The dungeon master writes that one moment, seeing only that person and that kind of moment
//     (intel_story_dm.prompt with decided_npc / decided_type), and the usual checks dispatch it.

#include "NPCIndex.h"

#include <functional>
#include <string>
#include <unordered_set>

namespace IntelEngine::StoryDirector {

    /** The staged director runs instead of the single prompt: a living-world level and a way to ask SkyrimNet. */
    bool ShouldRun();

    /** The dungeon master's request for a context (JSON-escaped markdown) and an exclusion set, with the decided
     *  person and kind ("" when the dungeon master is to choose). */
    using BuildRequest = std::function<std::string(const std::string& md, const std::unordered_set<std::string>& excluded,
                                                   const std::string& decidedNpc, const std::string& decidedType)>;

    /** Worker thread. done(requestJson) is called exactly once, from a worker thread; "" = nobody has a real reason. */
    void Run(NPCIndex::StoryDMTickSnapshot snap, std::unordered_set<std::string> excluded, BuildRequest build,
             std::function<void(std::string)> done);
}
