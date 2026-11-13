#pragma once

/**
 * Dashboard UI Manager
 *
 * Manages the dashboard lifecycle and state flow independently of its UI host.
 */

#include "Plugin.h"
#include "UiHost.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <cstdint>
#include <vector>

namespace IntelEngine {

    class DashboardUIManager {
    public:
        static DashboardUIManager* GetSingleton() {
            static DashboardUIManager instance;
            return &instance;
        }

        void Initialize();
        void Shutdown();
        void Toggle();
        bool IsOpen() const { return isOpen_.load(); }
        bool IsAvailable() const;

        void PushSlotData();
        void PushFullState(const std::string& json);
        void ReloadView();

        // Timed prompt: a card with a text, up to three answers and a draining time bar, shown by the
        // dashboard's own page (alone when the dashboard is closed, on top of it when it is open).
        // ShowPrompt returns an id, or 0 when no prompt can be shown now (no UI host, or one is already up).
        // GetPromptResult: -1 while it is open, the chosen answer (0-based) once decided, kPromptUnavailable
        // when the view could not be shown at all, so the caller can fall back to a message box.
        static constexpr int kPromptUnavailable = -2;
        int ShowPrompt(const std::string& title, const std::string& body,
                       const std::vector<std::string>& options, float timeoutSeconds, int timeoutChoice);
        int GetPromptResult(int promptId);

    private:
        DashboardUIManager() = default;
        ~DashboardUIManager() = default;
        DashboardUIManager(const DashboardUIManager&) = delete;
        DashboardUIManager& operator=(const DashboardUIManager&) = delete;

        std::unique_ptr<IUiHost> host_;
        std::atomic<bool> acceptingCallbacks_{false};
        std::atomic<bool> available_{false};
        std::atomic<bool> isOpen_{false};
        std::atomic<bool> domReady_{false};
        std::atomic<bool> stopPolling_{false};
        bool initialized_ = false;
        bool shuttingDown_ = false;
        bool requestedOpen_ = false;
        bool openedEventSent_ = false;
        bool pendingOpen_ = false;
        bool pendingClose_ = false;
        bool pendingReload_ = false;
        bool pendingFlush_ = false;
        bool pendingRefresh_ = false;
        bool pendingFullStateSet_ = false;
        bool gameTaskQueued_ = false;
        bool gameTaskRetry_ = false;
        bool pendingSlotPayloadSet_ = false;
        bool pendingFullStatePayloadSet_ = false;
        std::uint64_t generation_ = 0;
        std::uint64_t intentGeneration_ = 0;
        std::uint64_t pendingCloseIntentGeneration_ = 0;
        std::uint64_t expectedExitIntentGeneration_ = 0;
        bool expectedExitPending_ = false;
        std::uint64_t slotPayloadGeneration_ = 0;
        std::uint64_t fullStatePayloadGeneration_ = 0;
        std::string pendingFullState_;
        std::string pendingSlotPayload_;
        std::vector<std::pair<std::uint64_t, std::string>> pendingDirectorResults_;
        std::string pendingFullStatePayload_;
        std::size_t knownNpcsSize_ = static_cast<std::size_t>(-1);
        mutable std::mutex mutex_;
        std::condition_variable pollCondition_;
        std::thread hotkeyThread_;
        int promptId_ = 0;
        int promptCounter_ = 0;
        int promptFallback_ = 0;
        int lastPromptId_ = 0;
        int lastPromptResult_ = -1;
        int promptAbortResult_ = kPromptUnavailable;
        bool promptSent_ = false;
        bool promptWantsOpen_ = false;
        bool promptOpenedView_ = false;
        bool promptAbort_ = false;
        std::string promptJson_;
        std::chrono::steady_clock::time_point promptDeadline_{};
        void ServicePrompt();
        void FinishPrompt(int result);
        static void OnPromptResultStatic(const char* jsonArg);

        void HotkeyPollLoop();
        void OnHostEvent(UiHostEvent event, std::uint64_t expectedIntentGeneration = 0);
        void RequestClose();
        void FlushState(std::uint64_t generation);
        void SendSlotData(std::uint64_t generation);
        void ProcessFullState(const std::string& json, std::uint64_t generation);

        static void OnCloseStatic(const char* jsonArg);
        static void OnRequestRefreshStatic(const char* jsonArg);
        static void OnReloadUIStatic(const char* jsonArg);

        // Dashboard action callbacks (JS -> C++ -> Papyrus via ModEvent)
        static void OnCancelTaskStatic(const char* jsonArg);
        static void OnCancelScheduleStatic(const char* jsonArg);
        static void OnCancelQuestStatic(const char* jsonArg);
        static void OnToggleStoryTypeStatic(const char* jsonArg);
        static void OnChangeSettingStatic(const char* jsonArg);
        static void OnRemovePackagesStatic(const char* jsonArg);
        static void OnChangePluginConfigStatic(const char* jsonArg);
        static void OnDispatchStoryStatic(const char* jsonArg);
        static void OnDispatchNpcSocialStatic(const char* jsonArg);
        static void OnDispatchPoliticsStatic(const char* jsonArg);
        static void OnExecuteActionStatic(const char* jsonArg);
        static void OnGenerateDirectorStatic(const char* jsonArg);
        static void OnToggleActionStatic(const char* jsonArg);

        void SendModEvent(const std::string& eventName, const std::string& strArg = "", float numArg = 0.0f);

        struct ActionParam {
            std::string name;
            std::string description;
            std::vector<std::string> enumValues;
        };
        struct ActionMeta {
            std::string name;
            std::string fileName;
            std::string description;
            bool enabled = true;
            std::vector<ActionParam> params;
            std::string category;
            bool isCategory = true;
        };

        std::vector<ActionMeta> actionCache_;
        bool actionCacheLoaded_ = false;
        std::mutex actionCacheMutex_;
        void LoadActionYAMLs();
    };

}  // namespace IntelEngine
