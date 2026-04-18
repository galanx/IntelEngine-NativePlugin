#pragma once

/**
 * AsyncDispatch — Background worker for off-main-thread context building.
 *
 * The three DM ticks (NPC, Story, Politics) build their LLM context in two phases:
 *   Phase A (main thread): snapshot engine state into value-only structs.
 *   Phase B (worker thread, here): SQL queries + markdown formatting.
 *   Phase C (back to main thread): dispatch a Papyrus callback that fires the LLM call.
 *
 * Phase A and Phase C MUST stay on the main game thread — Skyrim engine RE::* getters
 * and Papyrus VM dispatch are not thread-safe. Phase B can run anywhere because
 * SkyrimNet's PublicGet* SQLite queries are documented thread-safe.
 *
 * One owned worker thread is enough for the current tick rate (interval >= 1 game hour).
 * Owned (not detached) so SKSE plugin teardown can join cleanly.
 */

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace IntelEngine::AsyncDispatch {

    /** Start the worker thread. Idempotent. */
    void Initialize();

    /** Stop and join the worker thread. Pending tasks are dropped. */
    void Shutdown();

    /** Submit work to run on the worker thread. */
    void Submit(std::function<void()> work);

    /**
     * Dispatch a Papyrus quest function from C++ via DispatchMethodCall1.
     *
     * MUST be called on the main game thread (use SKSE::GetTaskInterface()->AddTask
     * to marshal from a worker thread). Calling off-thread will silently corrupt the VM.
     *
     * Currently supports a single String argument — the only shape we need for the
     * three OnXxxContextReady(String) handlers. Extend if needed.
     *
     * @param questEditorId  Editor ID of the quest the script is attached to.
     * @param scriptName     Papyrus script (e.g., "IntelEngine_StoryEngine").
     * @param functionName   Function to invoke on the script.
     * @param stringArg      String argument to pass to the function.
     * @return true if dispatch was queued successfully.
     */
    bool ExecuteQuestFunctionString(const std::string& questEditorId,
                                    const std::string& scriptName,
                                    const std::string& functionName,
                                    const std::string& stringArg);

}  // namespace IntelEngine::AsyncDispatch
