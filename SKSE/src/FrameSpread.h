#pragma once

#include "AsyncDispatch.h"

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <thread>

/**
 * Work on the game's thread that is too long for one frame, done a part a frame without being cut into separate
 * functions: a coroutine that gives the frame back with `co_await NextFrame{}` and goes on a frame later, with
 * everything it had in hand.
 *
 * Why: anything of IntelEngine's that takes more than half a millisecond of one frame is a hitch the player can
 * feel when the frame was already full. Asking for a tale took 0.74 to 0.85 ms in a small town and 2.4 ms on a large
 * mod list (the play session of 2026-11-22), all of it reading the game, which only the game's thread may do.
 *
 * How the frame is given back: the worker waits about one frame and then queues the rest on the game's thread, as
 * the world snapshot's slices do (NPCIndex::NextStoryDMSlice). A task queued straight from the game's thread could
 * run in the very same pass of the queue, and two parts would share a frame after all.
 *
 * What the coroutine must mind: no lock held across a pause; a save may be loaded between two parts (ask the load
 * generation after every pause and stop when it changed); anything pointed at by an argument of the first call is
 * gone after the first pause (take arguments by value).
 */
namespace IntelEngine {

    /** The coroutine's type: it runs at once when called, up to its first pause, and cleans up after itself. */
    struct FrameJob {
        struct promise_type {
            FrameJob get_return_object() noexcept { return {}; }
            std::suspend_never initial_suspend() noexcept { return {}; }
            std::suspend_never final_suspend() noexcept { return {}; }
            void return_void() noexcept {}
            void unhandled_exception() noexcept { logger::error("FrameJob: an exception ended a job spread over frames"); }
        };
    };

    /** `co_await NextFrame{}`: the rest of the coroutine runs on the game's thread about a frame later. */
    struct NextFrame {
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> rest) const {
            AsyncDispatch::Submit([rest]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                // Without the task queue the game is going down: the job is simply never finished.
                if (auto* task = SKSE::GetTaskInterface()) task->AddTask([rest]() { rest.resume(); });
            });
        }
        void await_resume() const noexcept {}
    };

    /** How much of each frame a job spread over frames took: the share now running, the largest, the sum. */
    struct FrameShare {
        std::chrono::steady_clock::time_point at;
        double total = 0.0;
        double worst = 0.0;
        int frames = 1;

        explicit FrameShare(std::chrono::steady_clock::time_point from = std::chrono::steady_clock::now()) : at(from) {}
        /** Milliseconds of this frame's share so far. */
        double Ms() const { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - at).count(); }
        /** This frame's share is over (before a pause, and at the very end). */
        void End() {
            const double ms = Ms();
            total += ms;
            worst = std::max(worst, ms);
        }
        /** A new frame's share begins (after a pause). */
        void Begin() {
            at = std::chrono::steady_clock::now();
            ++frames;
        }
    };
}
