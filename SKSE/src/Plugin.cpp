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
#include "PoliticalDB.h"
#include "FactionPolitics.h"

#include <fstream>
#include <chrono>
#include <random>
#include <sstream>
#include <filesystem>

namespace IntelEngine {

    // =========================================================================
    // SKSE Serialization — Per-Save Unique ID
    // =========================================================================

    constexpr uint32_t SERIALIZATION_ID = 'IEPS';  // IntelEngine Plugin Serialization
    constexpr uint32_t SAVE_ID_RECORD = 'IEID';    // IntelEngine Save ID
    constexpr uint32_t SAVE_ID_VERSION = 1;

    static std::string g_currentSaveID;
    static bool g_hasReverted = false;

    /** Generate a unique per-save ID (timestamp + random suffix) for DB path isolation. */
    static std::string GenerateUniqueID() {
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();

        std::mt19937 rng(static_cast<unsigned int>(ms));
        std::uniform_int_distribution<int> dist(0, 999999);

        std::stringstream ss;
        ss << ms << "-" << dist(rng);
        logger::info("Generated new IntelEngine Save ID: {}", ss.str());
        return ss.str();
    }

    std::string GetSaveUniqueID() {
        if (g_currentSaveID.empty()) {
            g_currentSaveID = GenerateUniqueID();
        }
        return g_currentSaveID;
    }

    void SaveCallback(SKSE::SerializationInterface* a_intfc) {
        if (!a_intfc->OpenRecord(SAVE_ID_RECORD, SAVE_ID_VERSION)) {
            logger::error("SaveCallback: Failed to open IEID record");
            return;
        }

        std::string id = GetSaveUniqueID();
        uint32_t len = static_cast<uint32_t>(id.size());
        a_intfc->WriteRecordData(&len, sizeof(len));
        a_intfc->WriteRecordData(id.data(), len);
        logger::info("SaveCallback: Wrote save ID '{}'", id);
    }

    void LoadCallback(SKSE::SerializationInterface* a_intfc) {
        uint32_t type, version, length;
        while (a_intfc->GetNextRecordInfo(type, version, length)) {
            if (type == SAVE_ID_RECORD) {
                if (version != SAVE_ID_VERSION) {
                    logger::warn("LoadCallback: Unknown IEID version {}", version);
                    continue;
                }
                uint32_t strLen = 0;
                a_intfc->ReadRecordData(&strLen, sizeof(strLen));
                if (strLen > 0 && strLen < 256) {
                    std::string id(strLen, '\0');
                    a_intfc->ReadRecordData(id.data(), strLen);
                    g_currentSaveID = id;
                    logger::info("LoadCallback: Loaded save ID '{}'", g_currentSaveID);
                }
            }
        }
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        g_currentSaveID.clear();
        g_hasReverted = true;
        logger::info("RevertCallback: Save ID cleared");
    }

    /** Initialize PoliticalDB with per-save database path + timeline cleanup. */
    static void InitializePoliticalDB() {
        std::string saveID = GetSaveUniqueID();

        // Build path: Data/SKSE/Plugins/IntelEngine/data/IntelEngine-{saveID}.db
        std::filesystem::path dbDir = std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "IntelEngine" / "data";
        if (!std::filesystem::exists(dbDir)) {
            std::filesystem::create_directories(dbDir);
        }
        std::filesystem::path dbPath = dbDir / ("IntelEngine-" + saveID + ".db");

        auto* db = PoliticalDB::GetSingleton();

        // Close existing connection if switching saves
        if (db->IsReady()) {
            db->Shutdown();
        }

        if (!db->Initialize(dbPath.string())) {
            logger::error("Failed to initialize PoliticalDB at: {}", dbPath.string());
            return;
        }

        // Timeline cleanup: delete events from the future (save-scumming)
        auto* calendar = RE::Calendar::GetSingleton();
        if (calendar) {
            float currentGameTime = calendar->GetCurrentGameTime();
            int cleaned = db->CleanupFutureEvents(currentGameTime);
            if (cleaned > 0) {
                logger::info("PoliticalDB: Cleaned {} future events (game time: {:.2f})", cleaned, currentGameTime);
            }
        }

        // Clear FactionPolitics in-memory caches
        FactionPolitics::GetSingleton()->ClearCaches();
    }

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
                // Note: PoliticalDB + FactionPolitics init deferred to kNewGame/kPostLoadGame
                // (requires save ID for per-save database path)
                logger::info("Data loaded - initializing SkyrimNet API and NPC index");
                MemoryDB::GetSingleton()->InitializeAPI();
                DashboardConfig::GetSingleton()->Load();
                DashboardUIManager::GetSingleton()->Initialize();
                NPCIndex::GetSingleton()->BuildIndex();
                LocationResolver::GetSingleton()->BuildLocationIndex();
                ItemIndex::GetSingleton()->BuildIndex();
                FactionPolitics::GetSingleton()->LoadSettings();
                break;

            case SKSE::MessagingInterface::kNewGame:
                // New game — clear SlotTracker state, force DB re-discovery
                logger::info("New game - clearing SlotTracker, clearing MemoryDB caches");
                SlotTracker::GetSingleton()->ClearAll();
                NPCIndex::GetSingleton()->RefreshIndex();
                MemoryDB::GetSingleton()->ClearCaches();
                // Initialize per-save political DB (new save ID generated)
                InitializePoliticalDB();
                FactionPolitics::GetSingleton()->Initialize();
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
                // Initialize per-save political DB (save ID restored via serialization)
                if (g_hasReverted) {
                    g_hasReverted = false;
                    InitializePoliticalDB();
                    FactionPolitics::GetSingleton()->Initialize();
                }
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

    // Register SKSE serialization for per-save unique ID
    auto serialization = SKSE::GetSerializationInterface();
    serialization->SetUniqueID(IntelEngine::SERIALIZATION_ID);
    serialization->SetSaveCallback(IntelEngine::SaveCallback);
    serialization->SetLoadCallback(IntelEngine::LoadCallback);
    serialization->SetRevertCallback(IntelEngine::RevertCallback);
    logger::info("SKSE serialization registered (per-save DB support)");

    logger::info("IntelEngine loaded successfully");
    return true;
}
