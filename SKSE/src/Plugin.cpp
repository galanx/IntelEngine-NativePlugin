/**
 * IntelEngine SKSE Plugin
 *
 * Main entry point for the SKSE plugin.
 * Handles initialization, Papyrus registration, and event hooks.
 */

#include "Plugin.h"
#include "Papyrus.h"
#include "NPCIndex.h"
#include "BattleManager.h"
#include "ItemIndex.h"
#include "LocationResolver.h"
#include "SlotTracker.h"
#include "MemoryDB.h"
#include "Settings.h"
#include "DashboardConfig.h"
#include "DashboardUIManager.h"
#include "PoliticalDB.h"
#include "FactionPolitics.h"
#include "SkyrimNetAPI.h"
#include "DialogueTracker.h"
#include "QuestStateTracker.h"
#include "StringUtils.h"

#include <fstream>
#include <chrono>
#include <random>
#include <sstream>
#include <filesystem>
#include <thread>
#include "ProximityMonitor.h"

namespace IntelEngine {

    /** Convert a filesystem path to a UTF-8 narrow string (avoids ANSI code page crash on Korean Windows). */
    static std::string PathToUtf8(const std::filesystem::path& p) {
        auto wide = p.wstring();
        if (wide.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                      nullptr, 0, nullptr, nullptr);
        if (len <= 0) return "";
        std::string result(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                            result.data(), len, nullptr, nullptr);
        return result;
    }

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

        // Persist slot tracker state (task recovery without StorageUtil on load)
        SlotTracker::GetSingleton()->Save(a_intfc);
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
            } else if (type == 'IETK') {
                SlotTracker::GetSingleton()->Load(a_intfc);
            }
        }
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        g_currentSaveID.clear();
        g_hasReverted = true;
        // Increment load generation to cancel any pending deferred Maintenance dispatch
        // from a previous load (prevents double-dispatch on rapid save reload).
        auto* tracker = SlotTracker::GetSingleton();
        tracker->loadGeneration.fetch_add(1, std::memory_order_relaxed);
        // Clear SlotTracker here (before LoadCallback repopulates from co-save).
        // Previously this was in kPostLoadGame, but that ran AFTER LoadCallback
        // and wiped the co-save data we just loaded.
        tracker->ClearAll();
        // Stop ProximityMonitor — will restart on kPostLoadGame
        ProximityMonitor::GetSingleton()->Stop();
        logger::info("RevertCallback: Save ID, SlotTracker, ProximityMonitor cleared (gen={})",
            tracker->loadGeneration.load(std::memory_order_relaxed));
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

        if (!db->Initialize(PathToUtf8(dbPath))) {
            logger::error("Failed to initialize PoliticalDB");
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

        // Recalculate player standings from history (handles save-scumming correctly)
        db->RecalculatePlayerStandings();

        // Clear FactionPolitics in-memory caches
        FactionPolitics::GetSingleton()->ClearCaches();
    }

    // =========================================================================
    // Papyrus Maintenance Bootstrap
    // =========================================================================

    // Definition — declaration in Plugin.h
    QuestHandleResult ResolveQuestHandle(bool startIfStopped) {
        QuestHandleResult r;

        auto* handler = RE::TESDataHandler::GetSingleton();
        if (!handler) return r;

        auto* modFile = handler->LookupModByName("IntelEngine.esp"sv);
        if (!modFile) {
            logger::warn("ResolveQuestHandle: IntelEngine.esp not loaded");
            return r;
        }

        RE::FormID questFormId;
        if (modFile->IsLight()) {
            questFormId = 0xFE000000 |
                (static_cast<RE::FormID>(modFile->GetSmallFileCompileIndex()) << 12) |
                (0x0D61 & 0x0FFF);
        } else {
            questFormId = (static_cast<RE::FormID>(modFile->GetCompileIndex()) << 24) | 0x000D61;
        }

        r.quest = RE::TESForm::LookupByID<RE::TESQuest>(questFormId);
        if (!r.quest) {
            logger::warn("ResolveQuestHandle: Quest {:08X} not found", questFormId);
            return r;
        }

        if (startIfStopped && !r.quest->IsRunning()) {
            logger::info("ResolveQuestHandle: Quest not running, starting it");
            r.quest->Start();
        }

        r.vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!r.vm) {
            logger::error("ResolveQuestHandle: Papyrus VM not available");
            return r;
        }

        auto* policy = r.vm->GetObjectHandlePolicy1();
        if (!policy) {
            logger::error("ResolveQuestHandle: Handle policy not available");
            return r;
        }

        r.handle = policy->GetHandleForObject(RE::FormType::Quest, r.quest);
        if (r.handle == policy->EmptyHandle()) {
            logger::error("ResolveQuestHandle: Could not get VM handle for quest");
            return r;
        }

        r.valid = true;
        return r;
    }

    /**
     * Call Maintenance() on IntelEngine_Core via the Papyrus VM.
     */
    void DispatchMaintenanceCall(bool firstInstall) {
        auto qh = ResolveQuestHandle(true);
        if (!qh.valid) return;

        auto* args = RE::MakeFunctionArguments(static_cast<bool>(firstInstall));
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;

        RE::BSFixedString className("IntelEngine_Core");
        RE::BSFixedString fnName("Maintenance");

        bool ok = qh.vm->DispatchMethodCall2(qh.handle, className, fnName, args, callback);
        logger::info("DispatchMaintenance: {} (firstInstall={})",
            ok ? "queued successfully" : "FAILED to queue", firstInstall);

        // Force-clear any stale battle state in C++ BattleManager.
        // The Papyrus side may have stale bytecode that never called EndBattle/ResetState,
        // leaving IsBattleActive() stuck on true forever. The C++ side is authoritative —
        // clear it here so new battles can start on this load.
        auto* bm = BattleManager::GetSingleton();
        if (bm) {
            // Clean up stale guard teammate/faction flags from previous session
            bm->CleanupStaleBattleState();
            if (bm->IsBattleActive()) {
                logger::warn("DispatchMaintenance: clearing stale battle (save had active battle)");
                bm->ResetBattleState();
            }
        }

        // NOTE: Subsystem restarts (Travel/NPCTasks/Schedule/StoryEngine/Politics/Battle)
        // are handled by Maintenance() itself (Core.psc lines 278-296).
        // Previously we dispatched them here as a "safety net" against stale bytecode,
        // but DispatchMethodCall2 uses the same save bytecode — no additional protection.
        // Worse, the safety-net dispatches ran BEFORE Maintenance's self-heal code,
        // causing None-cast errors and flooding the VM with 9 concurrent calls on load.
        // This contention interfered with RealNames Extended's StorageUtil persistence,
        // causing NPC names to regenerate on every save load.
    }

    // =========================================================================
    // Script Property Fixup (existing saves)
    // =========================================================================

    /**
     * Fix script cross-references that the Papyrus VM cannot resolve on
     * existing saves.  When a new script property is added to an ESP's VMAD
     * mid-playthrough, the save's serialised script state doesn't include
     * the new property.  The Papyrus VM deserialises from the save and
     * never falls back to the ESP for missing properties, so the value
     * stays None forever.  Papyrus self-heal code can't help either —
     * the save also caches stale bytecode, so new .pex code on disk is
     * ignored for existing script instances.
     *
     * The DLL is always loaded fresh from disk, so this is the correct
     * (and only) place to patch properties on existing saves.
     */
    void FixupScriptProperties() {
        auto qh = ResolveQuestHandle();
        if (!qh.valid) return;

        // Helper: bind scriptB into scriptA's property named propName
        auto fixProperty = [&](const char* scriptA, const char* propName,
                               const char* scriptB) {
            RE::BSTSmartPointer<RE::BSScript::Object> objA;
            if (!qh.vm->FindBoundObject(qh.handle, scriptA, objA) || !objA)
                return;

            auto* prop = objA->GetProperty(propName);
            if (!prop) return;

            // Already set — nothing to do
            if (prop->IsObject()) {
                auto existing = prop->GetObject();
                if (existing && existing.get()) return;
            }

            RE::BSTSmartPointer<RE::BSScript::Object> objB;
            if (!qh.vm->FindBoundObject(qh.handle, scriptB, objB) || !objB) {
                logger::warn("FixupScriptProperties: {} not bound on quest", scriptB);
                return;
            }

            prop->SetObject(objB);
            logger::info("FixupScriptProperties: {}.{} = {} (recovered)", scriptA, propName, scriptB);
        };

        // Properties added mid-playthrough — existing saves have them as None.
        // The Papyrus self-heal in Maintenance handles stopquest/startquest,
        // but stale save bytecode prevents it from running on first load.
        fixProperty("IntelEngine_Core", "Battle", "IntelEngine_Battle");
        fixProperty("IntelEngine_Core", "Politics", "IntelEngine_Politics");
        fixProperty("IntelEngine_Politics", "Battle", "IntelEngine_Battle");
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
                DialogueTracker::GetSingleton()->Initialize();

                // Register quest decorator with SkyrimNet (soft dependency — skipped if API unavailable)
                if (SkyrimNetAPI::RegisterDecorator) {
                    bool ok = SkyrimNetAPI::RegisterDecorator(
                        "get_intelengine_quests",
                        "Returns active IntelEngine quest details (location, type, briefing, objectives). "
                        "Use instead of generic quest list entry for IntelEngine quests.",
                        [](RE::Actor*) -> std::string {
                            return QuestStateTracker::GetSingleton()->GetFormattedQuestInfo();
                        });
                    if (ok) {
                        logger::info("Registered SkyrimNet decorator: get_intelengine_quests");
                    } else {
                        logger::warn("Failed to register SkyrimNet decorator: get_intelengine_quests");
                    }
                }

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
                // Game loaded — SlotTracker already populated from co-save (LoadCallback).
                // ClearAll moved to RevertCallback to avoid wiping co-save data.
                logger::info("Game loaded - refreshing indexes, clearing MemoryDB caches");
                NPCIndex::GetSingleton()->RefreshIndex();
                MemoryDB::GetSingleton()->ClearCaches();
                // Initialize per-save political DB (save ID restored via serialization)
                if (g_hasReverted) {
                    g_hasReverted = false;
                    InitializePoliticalDB();
                    FactionPolitics::GetSingleton()->Initialize();
                }
                // Re-sync SkyrimNet busy state from co-save-loaded SlotTracker
                // (LoadCallback fires before SkyrimNet is ready, so we sync here)
                {
                    auto* tracker = SlotTracker::GetSingleton();
                    if (tracker->HasCoSaveData() && SkyrimNetAPI::SetActorBusy) {
                        for (int i = 0; i < MAX_SLOTS; ++i) {
                            auto slot = tracker->GetSlotDataCopy(i);
                            if (slot.state != 0 && slot.agentFormID) {
                                auto reason = SlotTracker::BuildBusyReason(slot.taskType, slot.targetName);
                                SkyrimNetAPI::SetActorBusy(slot.agentFormID, reason.c_str());
                            }
                        }
                    }
                }
                // Fix stale script properties immediately (C++ only, no VM contention)
                {
                    auto* task = SKSE::GetTaskInterface();
                    if (task) {
                        task->AddTask([]() {
                            FixupScriptProperties();
                        });
                    }
                }
                // Start C++ ProximityMonitor (frame-rate distance/deadline checking)
                ProximityMonitor::GetSingleton()->Start();

                // Defer Maintenance dispatch — gives RealNames Extended and other mods
                // time to complete their load-time StorageUtil reads before IntelEngine
                // starts its StorageUtil-heavy recovery (RecoverActiveTasks).
                // Without this delay, IntelEngine's 9 concurrent VM dispatches caused
                // contention that made RealNames' HasStringValue check fail intermittently.
                {
                    // Capture current load generation to detect stale dispatches
                    // (player loads another save within the 3s window).
                    uint32_t gen = SlotTracker::GetSingleton()->loadGeneration.load(
                        std::memory_order_relaxed);
                    std::thread([gen]() {
                        std::this_thread::sleep_for(std::chrono::seconds(3));
                        // Check if a new load/revert happened while we slept
                        if (SlotTracker::GetSingleton()->loadGeneration.load(
                                std::memory_order_relaxed) != gen) {
                            logger::info("Deferred Maintenance cancelled (load generation changed)");
                            return;
                        }
                        auto* deferredTask = SKSE::GetTaskInterface();
                        if (deferredTask) {
                            deferredTask->AddTask([gen]() {
                                // Double-check on main thread too
                                if (SlotTracker::GetSingleton()->loadGeneration.load(
                                        std::memory_order_relaxed) != gen) {
                                    return;
                                }
                                DispatchMaintenanceCall(false);
                            });
                        }
                    }).detach();
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

        // NOTE: spdlog uses fopen() which expects ANSI, not UTF-8. Use path->string() here
        // (ANSI) so fopen can open the file. PathToUtf8 would produce UTF-8 that fopen misinterprets.
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
