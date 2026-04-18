#include "AsyncDispatch.h"
#include "Plugin.h"

namespace IntelEngine::AsyncDispatch {

    namespace {
        std::mutex g_mutex;
        std::condition_variable g_cv;
        std::queue<std::function<void()>> g_queue;
        std::thread g_worker;
        std::atomic<bool> g_running{false};

        void WorkerLoop() {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(g_mutex);
                    g_cv.wait(lock, [] { return !g_queue.empty() || !g_running.load(std::memory_order_acquire); });
                    if (!g_running.load(std::memory_order_acquire) && g_queue.empty()) return;
                    task = std::move(g_queue.front());
                    g_queue.pop();
                }
                try {
                    task();
                } catch (const std::exception& e) {
                    logger::error("AsyncDispatch worker: exception: {}", e.what());
                } catch (...) {
                    logger::error("AsyncDispatch worker: unknown exception");
                }
            }
        }
    }  // namespace

    void Initialize() {
        bool expected = false;
        if (!g_running.compare_exchange_strong(expected, true)) return;
        g_worker = std::thread(WorkerLoop);
        logger::info("AsyncDispatch: worker started");
    }

    void Shutdown() {
        bool expected = true;
        if (!g_running.compare_exchange_strong(expected, false)) return;
        g_cv.notify_all();
        if (g_worker.joinable()) g_worker.join();
        std::lock_guard<std::mutex> lock(g_mutex);
        std::queue<std::function<void()>> empty;
        g_queue.swap(empty);
        logger::info("AsyncDispatch: worker stopped");
    }

    void Submit(std::function<void()> work) {
        // Recheck g_running under the lock — closes the TOCTOU between an unlocked
        // load() and the queue push if Shutdown() runs concurrently. AsyncDispatch
        // is initialized once in SKSEPluginLoad before any Submit can happen, so
        // the not-initialized branch is a hard error not a fallback.
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_running.load(std::memory_order_acquire)) {
                logger::error("AsyncDispatch::Submit before Initialize or after Shutdown — work dropped");
                return;
            }
            g_queue.push(std::move(work));
        }
        g_cv.notify_one();
    }

    // -------------------------------------------------------------------------
    // ExecuteQuestFunctionString — mirror of SkyrimNet's Papyrus::ExecuteQuestFunction
    // pattern, trimmed to a single-String argument shape.
    // Source reference: Mods/SkyrimNet/src/Skyrim/Papyrus/PapyrusQuestScript.cpp:130-205
    // -------------------------------------------------------------------------

    static RE::TESQuest* FindQuestByEditorID(const std::string& editorId) {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return nullptr;
        auto& quests = dataHandler->GetFormArray<RE::TESQuest>();
        for (auto* quest : quests) {
            if (quest && quest->GetFormEditorID() == editorId) return quest;
        }
        return nullptr;
    }

    bool ExecuteQuestFunctionString(const std::string& questEditorId,
                                    const std::string& scriptName,
                                    const std::string& functionName,
                                    const std::string& stringArg) {
        try {
            auto* quest = FindQuestByEditorID(questEditorId);
            if (!quest) {
                logger::error("ExecuteQuestFunctionString: quest '{}' not found", questEditorId);
                return false;
            }

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) {
                logger::error("ExecuteQuestFunctionString: VM not available");
                return false;
            }

            auto* policy = vm->GetObjectHandlePolicy1();
            if (!policy) {
                logger::error("ExecuteQuestFunctionString: handle policy not available");
                return false;
            }

            auto handle = policy->GetHandleForObject(RE::FormType::Quest, quest);
            if (handle == policy->EmptyHandle()) {
                logger::error("ExecuteQuestFunctionString: failed to get VM handle for quest '{}'", questEditorId);
                return false;
            }

            RE::BSTSmartPointer<RE::BSScript::Object> script;
            if (!vm->FindBoundObject(handle, scriptName.c_str(), script) || !script) {
                logger::error("ExecuteQuestFunctionString: script '{}' not bound on quest '{}'",
                              scriptName, questEditorId);
                return false;
            }

            auto* args = RE::MakeFunctionArguments(RE::BSFixedString(stringArg.c_str()));
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            RE::BSFixedString fnName(functionName.c_str());

            bool dispatched = vm->DispatchMethodCall1(script, fnName, args, callback);
            if (!dispatched) {
                logger::error("ExecuteQuestFunctionString: DispatchMethodCall1 failed for {}::{}",
                              scriptName, functionName);
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            logger::error("ExecuteQuestFunctionString: exception: {}", e.what());
            return false;
        }
    }

}  // namespace IntelEngine::AsyncDispatch
