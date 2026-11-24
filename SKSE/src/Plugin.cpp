/**
 * IntelEngine SKSE Plugin
 *
 * Main entry point for the SKSE plugin.
 * Handles initialization, Papyrus registration, and event hooks.
 */

#include "Plugin.h"
#include "Papyrus.h"
#include "NPCIndex.h"
#include "LanguageBridge.h"
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
#include "SpeechTracker.h"
#include "LivingWorldBridge.h"
#include "BondBridge.h"
#include "StorylineBridge.h"
#include "QuestBridge.h"
#include "ArcBridge.h"
#include "RumorBridge.h"
#include "QuestStateTracker.h"
#include "StringUtils.h"
#include "AsyncDispatch.h"
#include "ProximityMonitor.h"
#include "PoliticalEngineBridge.h"

#include <fstream>
#include <chrono>
#include <random>
#include <sstream>
#include <filesystem>
#include <thread>

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

        // 4.0 living world: the record of known NPCs and the director's pacing
        LivingWorldBridge::GetSingleton()->Save(a_intfc);
        StorylineBridge::GetSingleton()->Save(a_intfc);
        QuestBridge::GetSingleton()->Save(a_intfc);
        ArcBridge::GetSingleton()->Save(a_intfc);
        RumorBridge::GetSingleton()->Save(a_intfc);
        BattleManager::GetSingleton()->Save(a_intfc);
        FactionPolitics::GetSingleton()->Save(a_intfc);
        FactionPolitics::GetSingleton()->SaveHeld(a_intfc);
        FactionPolitics::GetSingleton()->SaveRanks(a_intfc);
        BondBridge::GetSingleton()->Save(a_intfc);
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
                if (a_intfc->ReadRecordData(&strLen, sizeof(strLen)) != sizeof(strLen)) {
                    continue;
                }
                if (strLen > 0 && strLen < 256) {
                    std::string id(strLen, '\0');
                    if (a_intfc->ReadRecordData(id.data(), strLen) != strLen) {
                        continue;
                    }
                    g_currentSaveID = id;
                    logger::info("LoadCallback: Loaded save ID '{}'", g_currentSaveID);
                }
            } else if (type == 'IETK') {
                SlotTracker::GetSingleton()->Load(a_intfc);
            } else if (type == LivingWorld::kRecordType) {
                LivingWorldBridge::GetSingleton()->Load(a_intfc, version, length);
            } else if (type == Storylines::kRecordType) {
                StorylineBridge::GetSingleton()->Load(a_intfc, version, length);
            } else if (type == Quests::kRecordType) {
                QuestBridge::GetSingleton()->Load(a_intfc, version, length);
            } else if (type == Arcs::kRecordType) {
                ArcBridge::GetSingleton()->Load(a_intfc, version, length);
            } else if (type == kRumorRecordType) {
                RumorBridge::GetSingleton()->Load(a_intfc, version, length);
            } else if (type == kPendingBattleRecord) {
                BattleManager::GetSingleton()->Load(a_intfc, version, length);
            } else if (type == kFallenLeadersRecord) {
                FactionPolitics::GetSingleton()->Load(a_intfc, version, length);
            } else if (type == kHeldEventRecord) {
                FactionPolitics::GetSingleton()->LoadHeld(a_intfc, version, length);
            } else if (type == Ranks::kRecordType) {
                FactionPolitics::GetSingleton()->LoadRanks(a_intfc, version, length);
            } else if (type == Bonds::kRecordType) {
                BondBridge::GetSingleton()->Load(a_intfc, version, length);
            }
        }
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        intelEngineStoryLoadRecovered.store(false); IntelEngineNewLoad();
        PoliticalEngineBridge::GetSingleton()->MarkDirty(true);
        PoliticalDB::GetSingleton()->BeginLoad();
        g_currentSaveID.clear();
        g_hasReverted = true;
        // Cancel deferred Maintenance and NPC tick snapshots/results from the previous load.
        auto* tracker = SlotTracker::GetSingleton();
        tracker->loadGeneration.fetch_add(1, std::memory_order_relaxed);
        NPCTickResult emptyNPCTick;
        emptyNPCTick.loadGeneration = tracker->loadGeneration.load(std::memory_order_relaxed);
        NPCIndex::GetSingleton()->PublishNPCTickResult(emptyNPCTick);
        // Clear SlotTracker here (before LoadCallback repopulates from co-save).
        // Previously this was in kPostLoadGame, but that ran AFTER LoadCallback
        // and wiped the co-save data we just loaded.
        tracker->ClearAll();
        LivingWorldBridge::GetSingleton()->Revert();
        StorylineBridge::GetSingleton()->Revert();
        QuestBridge::GetSingleton()->Revert();
        ArcBridge::GetSingleton()->Revert();
        RumorBridge::GetSingleton()->Revert();
        BattleManager::GetSingleton()->Revert();
        FactionPolitics::GetSingleton()->ForgetHeldEvent();
        FactionPolitics::GetSingleton()->ForgetFallen();
        FactionPolitics::GetSingleton()->ForgetRanks();
        BondBridge::GetSingleton()->Revert();
        ProximityMonitor::GetSingleton()->DisarmAll();
        logger::info("RevertCallback: Save ID and SlotTracker cleared (gen={})",
            tracker->loadGeneration.load(std::memory_order_relaxed));
    }

    /** Initialize PoliticalDB with per-save database path + timeline cleanup. */
    static void InitializePoliticalDB() {
        FactionPolitics::GetSingleton()->ClearCaches();
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

        if (!db->Initialize(PathToUtf8(dbPath), saveID)) {
            logger::error("Failed to initialize PoliticalDB");
            return;
        }

        // Timeline cleanup: delete events from the future (save-scumming)
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar) logger::warn("PoliticalDB: No calendar for cleanup; pipe 2 remains not ready");
        if (calendar) {
            float currentGameTime = calendar->GetCurrentGameTime();
            int cleaned = db->CleanupFutureEvents(currentGameTime);
            if (cleaned > 0) {
                logger::info("PoliticalDB: Cleaned {} future events (game time: {:.2f})", cleaned, currentGameTime);
            }
        }

        // Recalculate player standings from history (handles save-scumming correctly)
        db->RecalculatePlayerStandings();
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
            case SKSE::MessagingInterface::kPreLoadGame:
                intelEngineStoryLoadRecovered.store(false); IntelEngineNewLoad();
                SpeechTracker::GetSingleton()->Reset();
                PoliticalDB::GetSingleton()->BeginLoad();
                break;
            case SKSE::MessagingInterface::kDataLoaded:
                // Game data is loaded - initialize SkyrimNet API and build NPC index
                // Note: PoliticalDB + FactionPolitics init deferred to kNewGame/kPostLoadGame
                // (requires save ID for per-save database path)
                logger::info("Data loaded - initializing SkyrimNet API and NPC index");
                MemoryDB::GetSingleton()->InitializeAPI();
                DialogueTracker::GetSingleton()->Initialize();
                SpeechTracker::GetSingleton()->Register();
                LivingWorldBridge::GetSingleton()->Register();
                LivingWorldBridge::GetSingleton()->CatalogPlaces();   // at the main menu, not in a frame of play
                Papyrus::BuildItemBook();                             // the same
                PoliticalEngineBridge::GetSingleton()->onLoaded = [] { Papyrus::PollAndSyncPE(); };
                PoliticalEngineBridge::GetSingleton()->Initialize();

                // 4.0: what an NPC asked of the player, for their bio (prompt submodule 0202_intel_storylines)
                if (SkyrimNetAPI::RegisterDecorator) {
                    const bool registered = SkyrimNetAPI::RegisterDecorator(
                        "get_intelengine_storylines",
                        "Returns what this NPC asked of the player through IntelEngine and how it went.",
                        [](RE::Actor* actor) -> std::string { return StorylineBridge::GetSingleton()->DescribeFor(actor); });
                    logger::info("{} SkyrimNet decorator: get_intelengine_storylines", registered ? "Registered" : "Failed to register");
                    // Whether this NPC has a request of the player open: the storyline actions' eligibility (is_empty).
                    const bool openRegistered = SkyrimNetAPI::RegisterDecorator(
                        "get_intelengine_open_storyline",
                        "Returns the id of the request this NPC has open with the player, empty when none.",
                        [](RE::Actor* actor) -> std::string {
                            const auto id = actor ? StorylineBridge::GetSingleton()->OpenIdFor(actor->GetFormID()) : 0;
                            return id ? std::to_string(id) : std::string();
                        });
                    logger::info("{} SkyrimNet decorator: get_intelengine_open_storyline", openRegistered ? "Registered" : "Failed to register");
                    // Whether this NPC is the one a request is about and may answer it through the player now: the
                    // SendWordBack action's eligibility (is_not_empty).
                    const bool aboutRegistered = SkyrimNetAPI::RegisterDecorator(
                        "get_intelengine_storyline_about",
                        "Returns the id of a request about this NPC they may answer through the player now, empty when none.",
                        [](RE::Actor* actor) -> std::string { return StorylineBridge::GetSingleton()->AnswerPending(actor); });
                    logger::info("{} SkyrimNet decorator: get_intelengine_storyline_about", aboutRegistered ? "Registered" : "Failed to register");
                    // 4.0 quests: whether a former follower met on the way to a venture may ask the player along now
                    // (the InviteAlong action's eligibility, is_not_empty).
                    const bool inviteRegistered = SkyrimNetAPI::RegisterDecorator(
                        "get_intelengine_venture_invite",
                        "Returns the id of the venture this NPC may ask the player along on now, empty when none.",
                        [](RE::Actor* actor) -> std::string { return QuestBridge::GetSingleton()->InviteFor(actor); });
                    logger::info("{} SkyrimNet decorator: get_intelengine_venture_invite", inviteRegistered ? "Registered" : "Failed to register");
                    // Whether a venture's meeting with the player is going on for this NPC (planned, not yet taken on):
                    // the travel action is not offered to them meanwhile, the meeting moves them. Mid-meeting, Jenassa's
                    // own model sent her into Embershard on a trip of her own and the venture was lost (2026-11-18).
                    const bool meetingRegistered = SkyrimNetAPI::RegisterDecorator(
                        "get_intelengine_in_venture",
                        "Returns the id of the venture meeting this NPC is in with the player now, empty when none.",
                        [](RE::Actor* actor) -> std::string {
                            const auto id = actor ? QuestBridge::GetSingleton()->VentureOf(actor->GetFormID()) : 0u;
                            return id ? std::to_string(id) : std::string();
                        });
                    logger::info("{} SkyrimNet decorator: get_intelengine_in_venture", meetingRegistered ? "Registered" : "Failed to register");
                    // The story arc this NPC has a part in (planned or open), empty when none: their own request is not
                    // offered meanwhile. Arrived to ask for help with her stolen circlet, Ysolda's model also chose
                    // AskPlayerForHelp, and a second request for the same circlet opened (2026-11-18). (Their trips are
                    // offered again since 2026-11-23: the player may send them; a trip of their own idea is refused by
                    // Travel.GoToLocation until the story is over.)
                    const bool arcRegistered = SkyrimNetAPI::RegisterDecorator(
                        "get_intelengine_arc",
                        "Returns the id of the story arc this NPC has a part in now, empty when none.",
                        [](RE::Actor* actor) -> std::string {
                            const auto id = actor ? ArcBridge::GetSingleton()->ArcOf(actor->GetFormID()) : 0u;
                            return id ? std::to_string(id) : std::string();
                        });
                    logger::info("{} SkyrimNet decorator: get_intelengine_arc", arcRegistered ? "Registered" : "Failed to register");
                    // A former follower's own life since the player left them (prompt submodule 0203_intel_life).
                    const bool lifeRegistered = SkyrimNetAPI::RegisterDecorator(
                        "get_intelengine_life",
                        "Returns what this NPC has done with their life since the player left them, first person; empty when nothing.",
                        [](RE::Actor* actor) -> std::string { return LivingWorldBridge::GetSingleton()->OwnLifeLine(actor); });
                    logger::info("{} SkyrimNet decorator: get_intelengine_life", lifeRegistered ? "Registered" : "Failed to register");
                    // The hold an NPC is in: who hears a rumor of a former follower (world-knowledge conditions).
                    const bool holdRegistered = SkyrimNetAPI::RegisterDecorator(
                        "get_intelengine_hold",
                        "Returns the name of the hold this NPC is in, empty when none.",
                        [](RE::Actor* actor) -> std::string { return LivingWorldBridge::HoldName(actor); });
                    logger::info("{} SkyrimNet decorator: get_intelengine_hold", holdRegistered ? "Registered" : "Failed to register");
                    // The talk of the day: what this NPC has heard and could pass on (prompt submodule 0204_intel_rumors).
                    const bool rumorsRegistered = SkyrimNetAPI::RegisterDecorator(
                        "get_intelengine_rumors",
                        "Returns what this NPC has heard lately and could pass on (rumors going round their hold); empty when nothing.",
                        [](RE::Actor* actor) -> std::string { return RumorBridge::GetSingleton()->Lines(actor); });
                    logger::info("{} SkyrimNet decorator: get_intelengine_rumors", rumorsRegistered ? "Registered" : "Failed to register");
                    // A soldier of a battle: what they know of where they stand (prompt submodule 0205_intel_battle), and
                    // whether the player may still join their line by saying so (action JoinBattle).
                    const bool battleRegistered = SkyrimNetAPI::RegisterDecorator(
                        "get_intelengine_battle",
                        "Returns what this NPC knows of the battle they are drawn up or fighting in right now; empty for everyone who is not a soldier of one.",
                        [](RE::Actor* actor) -> std::string { return BattleManager::GetSingleton()->SoldierBrief(actor); });
                    const bool battleSideRegistered = SkyrimNetAPI::RegisterDecorator(
                        "get_intelengine_battle_side",
                        "Returns the name of the army this NPC stands in when the player may still join its line by saying so; empty otherwise.",
                        [](RE::Actor* actor) -> std::string { return BattleManager::GetSingleton()->SideOpenTo(actor); });
                    logger::info("{} SkyrimNet decorators: get_intelengine_battle, get_intelengine_battle_side",
                                 battleRegistered && battleSideRegistered ? "Registered" : "Failed to register");
                }

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
                LanguageBridge::GetSingleton()->Load();     // before the indexes: the place index tells it the names' script
                NPCIndex::GetSingleton()->BuildIndex();
                LocationResolver::GetSingleton()->BuildLocationIndex();
                ItemIndex::GetSingleton()->BuildIndex();
                QuestBridge::GetSingleton()->Catalogue();   // after the dungeon index; at the main menu
                ArcBridge::GetSingleton()->Catalogue();     // after the NPC index and the homes
                ArcBridge::GetSingleton()->Listen();
                FactionPolitics::GetSingleton()->LoadSettings();
                break;

            case SKSE::MessagingInterface::kNewGame:
                // New game — clear SlotTracker state, force DB re-discovery
                logger::info("New game - clearing SlotTracker, clearing MemoryDB caches");
                SpeechTracker::GetSingleton()->Reset();
                PoliticalEngineBridge::GetSingleton()->MarkDirty(true);
                SlotTracker::GetSingleton()->ClearAll();
                ProximityMonitor::GetSingleton()->DisarmAll();
                LivingWorldBridge::GetSingleton()->OnGameLoaded(true);
                NPCIndex::GetSingleton()->RefreshIndex();
                MemoryDB::GetSingleton()->ClearCaches();
                // Initialize per-save political DB (new save ID generated)
                InitializePoliticalDB();
                FactionPolitics::GetSingleton()->Initialize();
                // Bootstrap: start quest and call Maintenance for first install
                {
                    auto* task = SKSE::GetTaskInterface();
                    if (task) {
                        task->AddTask([]() { FactionPolitics::GetSingleton()->NoteTheDeadQuietly(); });   // a new game: nobody yet
                        task->AddTask([]() { DispatchMaintenanceCall(true); });
                        // A new game has its first story request too: the same first lookups, made now.
                        task->AddTask([]() {
                            ArcBridge::GetSingleton()->WarmUp();
                            BattleManager::GetSingleton()->WarmUp();
                            NPCIndex::GetSingleton()->WarmUpSocial();
                        });
                        AsyncDispatch::Submit([] { NPCIndex::ReadStoryFilters(); });
                    }
                }
                break;

            case SKSE::MessagingInterface::kPostLoadGame:
                // Game loaded — SlotTracker already populated from co-save (LoadCallback).
                // ClearAll moved to RevertCallback to avoid wiping co-save data.
                logger::info("Game loaded - refreshing indexes, clearing MemoryDB caches");
                // What a session's first story request used to read in the middle of play: the user's lists on the worker
                // (after that nothing reads them inside a frame), the first lookups on the game's thread while the save
                // comes in.
                AsyncDispatch::Submit([] { NPCIndex::ReadStoryFilters(); });
                if (auto* warm = SKSE::GetTaskInterface()) {
                    warm->AddTask([] {
                        ArcBridge::GetSingleton()->WarmUp();
                        BattleManager::GetSingleton()->WarmUp();      // a session's first battle: its lookups by name
                        NPCIndex::GetSingleton()->WarmUpSocial();     // the social engine's first look: back rooms, first calls
                    });
                }
                LanguageBridge::GetSingleton()->Decide();   // the settings may differ from when the data was loaded
                LivingWorldBridge::GetSingleton()->OnGameLoaded(false);
                // Journal titles are not in the save: the quests' records give them back.
                if (auto* titles = SKSE::GetTaskInterface()) {
                    titles->AddTask([] {
                        QuestBridge::GetSingleton()->ApplyTitles();
                        ArcBridge::GetSingleton()->ApplyTitles();
                    });
                }
                PoliticalEngineBridge::GetSingleton()->MarkDirty(true);
                AsyncDispatch::Submit([] { Papyrus::PollAndSyncPE(); });  // no-op unless PE data is already ready; PE's own "loaded" event covers the other order
                NPCIndex::GetSingleton()->RefreshIndex();
                MemoryDB::GetSingleton()->ClearCaches();
                // Initialize per-save political DB (save ID restored via serialization)
                if (g_hasReverted) {
                    g_hasReverted = false;
                    InitializePoliticalDB();
                    FactionPolitics::GetSingleton()->Initialize();
                }
                // A save of 3.8 keeps no record of the fallen: its dead are remembered without being told as news (the
                // game's thread, once the indexes above are fresh; nothing when the record was there).
                if (auto* quiet = SKSE::GetTaskInterface()) {
                    quiet->AddTask([] { FactionPolitics::GetSingleton()->NoteTheDeadQuietly(); });
                }
                // Re-sync SkyrimNet busy state from co-save-loaded SlotTracker
                // (LoadCallback fires before SkyrimNet is ready, so we sync here)
                {
                    auto* tracker = SlotTracker::GetSingleton();
                    if (tracker->HasCoSaveData() && SkyrimNetAPI::SetActorBusy) {
                        for (int i = 0; i < MAX_SLOTS; ++i) {
                            auto slot = tracker->GetSlotDataCopy(i);
                            // The same measure as while the game runs (SlotTracker): on the way (1) or coming back
                            // (3) is busy; arrived and waiting (2, 5, 8) is not. Every slot used to be marked busy
                            // here, so after a load someone waiting somewhere offered no task actions at all ("the
                            // deliver-message action simply does not show up").
                            const bool idle = slot.state == 0 || slot.state == 2 || slot.state == 5 || slot.state == 8;
                            if (!idle && slot.agentFormID) {
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
        // Logging must never cost the game a frame. Every line used to be written and flushed to disk at once, on
        // whatever thread logged it, the game's own included: a director round alone logs some forty lines. Lines
        // now go into a queue that spdlog's own thread writes out; a full queue drops the oldest rather than make the
        // game wait. Warnings and errors are flushed straight away, the rest within a second.
        spdlog::init_thread_pool(8192, 1);
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::async_logger>("global log", std::move(sink), spdlog::thread_pool(),
                                                          spdlog::async_overflow_policy::overrun_oldest);
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::warn);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::flush_every(std::chrono::seconds(1));
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

    // Start async worker for off-main-thread DM tick context building.
    // Register Shutdown via atexit so the worker is joined before static destructors
    // tear down singletons / SQLite connections / spdlog. Without this, an in-flight
    // Phase B task could touch destroyed state at process exit.
    IntelEngine::PoliticalEngineBridge::GetSingleton(); // Outlive the async worker's atexit drain.
    IntelEngine::AsyncDispatch::Initialize();
    std::atexit([]() { IntelEngine::AsyncDispatch::Shutdown(); });

    // Start proximity monitor — 150ms tick detects NPC arrival within 30-60
    // units of the target, replacing Papyrus's 3s poll that lets running NPCs
    // overshoot by 900-1800 units (face-bumping).
    IntelEngine::ProximityMonitor::GetSingleton()->Start();
    std::atexit([]() { IntelEngine::ProximityMonitor::GetSingleton()->Stop(); });

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
