#pragma once

/**
 * Dashboard UI Manager
 *
 * Manages the PrismaUI-based task dashboard overlay.
 * Follows the same pattern as SkyrimNet's ChatUIManager:
 * - Dynamic PrismaUI loading (optional dependency)
 * - View lifecycle (create -> show/hide -> destroy)
 * - JS interop for pushing state and receiving commands
 * - Hotkey polling for toggle (GetAsyncKeyState)
 *
 * If PrismaUI is not installed, the dashboard simply doesn't appear.
 */

#include "Plugin.h"
#include "PrismaUI_API.h"
#include <mutex>
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <unordered_map>

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
        bool IsAvailable() const { return prismaUI_ != nullptr; }

        // Push slot-only data (legacy, backwards compat)
        void PushSlotData();

        // Push comprehensive dashboard state JSON from Papyrus
        void PushFullState(const std::string& json);

        // Hot-reload UI from disk + re-read config
        void ReloadView();

    private:
        DashboardUIManager() = default;
        ~DashboardUIManager() = default;
        DashboardUIManager(const DashboardUIManager&) = delete;
        DashboardUIManager& operator=(const DashboardUIManager&) = delete;

        PRISMA_UI_API::IVPrismaUI1* prismaUI_ = nullptr;
        PrismaView dashboardView_ = 0;
        std::atomic<bool> isOpen_{false};
        std::atomic<bool> domReady_{false};
        bool initialized_ = false;
        std::mutex mutex_;

        std::thread hotkeyThread_;
        std::atomic<bool> stopPolling_{false};
        void HotkeyPollLoop();

        bool CreateViewIfNeeded();
        void Show();
        void Hide();

        // Static PrismaUI callbacks
        static void OnDomReadyStatic(PrismaView view);
        static void OnCloseStatic(const char* jsonArg);
        static void OnRequestRefreshStatic(const char* jsonArg);
        static void OnReloadUIStatic(const char* jsonArg);

        // Dashboard action callbacks (JS -> C++ -> Papyrus via ModEvent)
        static void OnDashboardAction(const char* actionName, const char* jsonArg);
        static void OnCancelTaskStatic(const char* jsonArg);
        static void OnCancelScheduleStatic(const char* jsonArg);
        static void OnToggleStoryTypeStatic(const char* jsonArg);
        static void OnChangeSettingStatic(const char* jsonArg);
        static void OnRemovePackagesStatic(const char* jsonArg);
        static void OnChangePluginConfigStatic(const char* jsonArg);
        static void OnDispatchStoryStatic(const char* jsonArg);
        static void OnExecuteActionStatic(const char* jsonArg);
        static void OnToggleActionStatic(const char* jsonArg);

        void HandleDomReady(PrismaView view);
        void HandleClose();
        void HandleRequestRefresh();

        // Send SKSE ModEvent to Papyrus
        void SendModEvent(const std::string& eventName, const std::string& strArg = "", float numArg = 0.0f);

        // Action YAML metadata for Director/Actions tabs
        struct ActionParam {
            std::string name;
            std::string description;
        };
        struct ActionMeta {
            std::string name;
            std::string fileName;       // YAML filename without extension
            std::string description;
            bool enabled = true;
            std::vector<ActionParam> params;  // dynamic params only
        };

        std::vector<ActionMeta> actionCache_;
        bool actionCacheLoaded_ = false;
        std::mutex actionCacheMutex_;
        void LoadActionYAMLs();

        // Pending director params (thread-safe storage for Papyrus retrieval)
        static std::unordered_map<std::string, std::string> pendingParams_;
        static std::mutex pendingParamsMutex_;

    public:
        // Called from Papyrus native functions
        static std::string GetPendingParam(const std::string& key);
        static void ClearPendingParams();
        // Atomic claim: reads all params as JSON and clears in one lock.
        // Returns "" if already claimed (prevents double-handler race).
        static std::string ClaimPendingParams();
    };

}  // namespace IntelEngine
