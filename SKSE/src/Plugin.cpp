/**
 * IntelEngine SKSE Plugin
 *
 * Main entry point for the SKSE plugin.
 * Handles initialization, Papyrus registration, and event hooks.
 */

#include "Plugin.h"
#include "Papyrus.h"
#include "NPCIndex.h"
#include "ItemIndex.h"
#include "LocationResolver.h"
#include "SlotTracker.h"
#include "MemoryDB.h"
#include "Settings.h"
#include "DashboardConfig.h"
#include "DashboardUIManager.h"

#include <fstream>

namespace IntelEngine {

    // =========================================================================
    // Papyrus Maintenance Bootstrap
    // =========================================================================

    /**
     * Call Maintenance() on IntelEngine_Core via the Papyrus VM.
     *
     * OnPlayerLoadGame on the PlayerAlias doesn't fire reliably (alias fill
     * issue). This C++ safety net ensures Maintenance always runs on every
     * game load, re-registering timers and recovering tasks.
     */
    void DispatchMaintenanceCall(bool firstInstall) {
        auto* handler = RE::TESDataHandler::GetSingleton();
        if (!handler) return;

        auto* modFile = handler->LookupModByName("IntelEngine.esp"sv);
        if (!modFile) {
            logger::warn("DispatchMaintenance: IntelEngine.esp not loaded");
            return;
        }

        // Compute runtime FormID for quest (local ID 0x000D61)
        RE::FormID questFormId;
        if (modFile->IsLight()) {
            questFormId = 0xFE000000 |
                (static_cast<RE::FormID>(modFile->GetSmallFileCompileIndex()) << 12) |
                (0x0D61 & 0x0FFF);
        } else {
            questFormId = (static_cast<RE::FormID>(modFile->GetCompileIndex()) << 24) | 0x000D61;
        }

        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(questFormId);
        if (!quest) {
            logger::warn("DispatchMaintenance: Quest {:08X} not found", questFormId);
            return;
        }

        if (!quest->IsRunning()) {
            logger::info("DispatchMaintenance: Quest not running, starting it");
            quest->Start();
        }

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            logger::error("DispatchMaintenance: Papyrus VM not available");
            return;
        }

        auto* policy = vm->GetObjectHandlePolicy1();
        if (!policy) {
            logger::error("DispatchMaintenance: Handle policy not available");
            return;
        }

        auto handle = policy->GetHandleForObject(RE::FormType::Quest, quest);
        if (handle == policy->EmptyHandle()) {
            logger::error("DispatchMaintenance: Could not get VM handle for quest");
            return;
        }

        auto* args = RE::MakeFunctionArguments(static_cast<bool>(firstInstall));
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;

        RE::BSFixedString className("IntelEngine_Core");
        RE::BSFixedString fnName("Maintenance");

        bool ok = vm->DispatchMethodCall2(handle, className, fnName, args, callback);
        logger::info("DispatchMaintenance: {} (firstInstall={})",
            ok ? "queued successfully" : "FAILED to queue", firstInstall);
    }

    // =========================================================================
    // SKSE Message Handler
    // =========================================================================

    void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                // Game data is loaded - initialize SkyrimNet API and build NPC index
                logger::info("Data loaded - initializing SkyrimNet API and NPC index");
                MemoryDB::GetSingleton()->InitializeAPI();
                DashboardConfig::GetSingleton()->Load();
                DashboardUIManager::GetSingleton()->Initialize();
                NPCIndex::GetSingleton()->BuildIndex();
                LocationResolver::GetSingleton()->BuildLocationIndex();
                ItemIndex::GetSingleton()->BuildIndex();
                break;

            case SKSE::MessagingInterface::kNewGame:
                // New game — clear SlotTracker state, force DB re-discovery
                logger::info("New game - clearing SlotTracker, clearing MemoryDB caches");
                SlotTracker::GetSingleton()->ClearAll();
                NPCIndex::GetSingleton()->RefreshIndex();
                MemoryDB::GetSingleton()->ClearCaches();
                // Bootstrap: start quest and call Maintenance for first install
                {
                    auto* task = SKSE::GetTaskInterface();
                    if (task) {
                        task->AddTask([]() { DispatchMaintenanceCall(true); });
                    }
                }
                break;

            case SKSE::MessagingInterface::kPostLoadGame:
                // Game loaded — clear SlotTracker, invalidate MemoryDB (lazy reconnect on first query)
                logger::info("Game loaded - clearing SlotTracker, clearing MemoryDB caches");
                SlotTracker::GetSingleton()->ClearAll();
                NPCIndex::GetSingleton()->RefreshIndex();
                MemoryDB::GetSingleton()->ClearCaches();
                // Bootstrap: call Maintenance since OnPlayerLoadGame doesn't fire reliably
                {
                    auto* task = SKSE::GetTaskInterface();
                    if (task) {
                        task->AddTask([]() { DispatchMaintenanceCall(false); });
                    }
                }
                break;

            default:
                break;
        }
    }

    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* a_vm) {
        if (!a_vm) {
            logger::error("Failed to get VM for Papyrus registration");
            return false;
        }

        Papyrus::Register(a_vm);
        logger::info("Papyrus functions registered");
        return true;
    }

}  // namespace IntelEngine

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    // Initialize logging
    try {
        auto path = logger::log_directory();
        if (!path) {
            SKSE::stl::report_and_fail(
                "IntelEngine: SKSE log directory could not be determined.\n"
                "Ensure SKSE is installed correctly.");
        }

        *path /= "IntelEngine.log";

        // Ensure log directory exists
        std::filesystem::create_directories(path->parent_path());

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    } catch (const std::exception& e) {
        // spdlog failed - write diagnostic file since logging isn't available
        try {
            std::ofstream diag("Data/SKSE/Plugins/IntelEngine_log_error.txt");
            diag << "IntelEngine: Logging initialization failed" << std::endl;
            diag << "Error: " << e.what() << std::endl;
            diag << "Plugin will continue without file logging." << std::endl;
        } catch (...) {}
    }

    logger::info("IntelEngine v{} loading", INTELENGINE_VERSION);

    // Load settings
    IntelEngine::Settings::GetSingleton()->Load();

    // Register for SKSE messages
    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging->RegisterListener("SKSE", IntelEngine::MessageHandler)) {
        logger::error("Failed to register message listener");
        return false;
    }

    // Register Papyrus functions
    auto papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus->Register(IntelEngine::RegisterPapyrusFunctions)) {
        logger::error("Failed to register Papyrus functions");
        return false;
    }

    logger::info("IntelEngine loaded successfully");
    return true;
}
