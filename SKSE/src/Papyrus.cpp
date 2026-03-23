/**
 * Papyrus Native Function Implementation
 *
 * Implements all native functions exposed to Papyrus scripts.
 */

#include "Papyrus.h"
#include "NPCIndex.h"
#include "ItemIndex.h"
#include <random>
#include <algorithm>
#include <chrono>
#include <mutex>
#include "LocationResolver.h"
#include "StringUtils.h"
#include "CellAnalyzer.h"
#include "ActionValidator.h"
#include "DepartureDetector.h"
#include "StuckDetector.h"
#include "OffScreenTracker.h"
#include "SlotTracker.h"
#include "MemoryDB.h"
#include "Settings.h"
#include "DashboardConfig.h"
#include "DashboardUIManager.h"
#include "FactionPolitics.h"
#include "PoliticalDB.h"
#include "BattleManager.h"
#include "ProcessUtils.h"
#include <Windows.h>
#include <nlohmann/json.hpp>

namespace IntelEngine::Papyrus {

    // Forward declarations for functions defined after Register()
    RE::Actor* ResolveStoryCandidate(RE::StaticFunctionTag*, RE::BSFixedString);
    RE::Actor* FindMessengerForSender(RE::StaticFunctionTag*, RE::Actor*);
    void NotifyStoryCooldown(RE::StaticFunctionTag*, RE::Actor*, float);
    bool IsActorOnStoryCooldown(RE::StaticFunctionTag*, RE::Actor*);
    void NotifySocialCooldown(RE::StaticFunctionTag*, RE::Actor*, float, float);
    void NotifyStoryTypePicked(RE::StaticFunctionTag*, RE::BSFixedString);
    void WarmStoryTypeCountsFromCSV(RE::StaticFunctionTag*, RE::BSFixedString);
    void SetRecentGossipContext(RE::StaticFunctionTag*, RE::BSFixedString);
    std::vector<int> GetDMCandidatePoolFormIDs(RE::StaticFunctionTag*);
    std::vector<int> GetNPCCandidatePoolFormIDs(RE::StaticFunctionTag*);
    RE::BSFixedString ScanActorsWithPackages(RE::StaticFunctionTag*, std::vector<int>);
    RE::BSFixedString RenderFactsSection(RE::StaticFunctionTag*, std::vector<RE::BSFixedString>, std::vector<float>, float);
    RE::BSFixedString RenderGossipHeardSection(RE::StaticFunctionTag*, std::vector<RE::BSFixedString>, std::vector<RE::BSFixedString>, std::vector<float>, float);
    RE::BSFixedString RenderGossipToldSection(RE::StaticFunctionTag*, std::vector<RE::BSFixedString>, std::vector<RE::BSFixedString>, std::vector<float>, float);
    RE::BSFixedString RenderTaskHistorySection(RE::StaticFunctionTag*, std::vector<RE::BSFixedString>, std::vector<float>, float);
    void SetDangerZonePolicy(RE::StaticFunctionTag*, int);
    void SetPlayerHomePolicy(RE::StaticFunctionTag*, int);
    void SetHoldRestrictionPolicy(RE::StaticFunctionTag*, RE::BSFixedString, int);
    bool CheckHoldRestriction(RE::StaticFunctionTag*, RE::Actor*, RE::BSFixedString);
    RE::BSFixedString GetActorHoldName(RE::StaticFunctionTag*, RE::Actor*);
    bool NPCKnowsPlayer(RE::StaticFunctionTag*, RE::Actor*);
    bool IsPotentialFollower(RE::StaticFunctionTag*, RE::Actor*);
    bool IsPlayerInBlockedLocation(RE::StaticFunctionTag*);
    bool IsPlayerInWhitelistedLocation(RE::StaticFunctionTag*);
    RE::TESObjectREFR* FindRescueAnchor(RE::StaticFunctionTag*, RE::Actor*);
    RE::TESObjectREFR* FindUsablePrisonerFurniture(RE::StaticFunctionTag*, RE::Actor*);
    RE::TESObjectREFR* ScanAheadForAnchor(RE::StaticFunctionTag*, RE::Actor*);
    RE::TESObjectREFR* GetDungeonBossAnchor(RE::StaticFunctionTag*, RE::BSFixedString);
    void NotifyDashboardSlotChanged(RE::StaticFunctionTag*);
    int GetDashboardHotkey(RE::StaticFunctionTag*);
    bool SetDashboardHotkey(RE::StaticFunctionTag*, int);
    void ReloadDashboardUI(RE::StaticFunctionTag*);
    void ReloadDashboardConfig(RE::StaticFunctionTag*);
    void PushDashboardFullState(RE::StaticFunctionTag*, RE::BSFixedString);
    bool IsDashboardOpen(RE::StaticFunctionTag*);
    RE::BSFixedString GetPendingDirectorParam(RE::StaticFunctionTag*, RE::BSFixedString);
    void ClearPendingDirectorParams(RE::StaticFunctionTag*);
    RE::BSFixedString ClaimPendingDirectorParams(RE::StaticFunctionTag*);

    // Political system forward declarations
    int GetFactionRelation(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString);
    int AdjustFactionRelation(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, int);
    int GetPlayerFactionStanding(RE::StaticFunctionTag*, RE::BSFixedString);
    int AdjustPlayerFactionStanding(RE::StaticFunctionTag*, RE::BSFixedString, int);
    bool IsFactionAtWar(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString);
    int GetWarMorale(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString);
    RE::BSFixedString GetRelationStatus(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString);
    RE::BSFixedString BuildPoliticalContext(RE::StaticFunctionTag*, float);
    RE::BSFixedString BuildPoliticalDashboardJson(RE::StaticFunctionTag*);
    int RecordPoliticalEvent(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString,
                             RE::BSFixedString, RE::BSFixedString, int, float);
    bool IsPoliticsEnabled(RE::StaticFunctionTag*);
    int GetPoliticsTickInterval(RE::StaticFunctionTag*);
    void ReloadFactionConfig(RE::StaticFunctionTag*);
    std::vector<RE::Actor*> GetFactionLeaderActors(RE::StaticFunctionTag*, RE::BSFixedString);
    std::vector<int> GetFactionLeaderFormIds(RE::StaticFunctionTag*, RE::BSFixedString);
    int ApplyPlayerStandingChanges(RE::StaticFunctionTag*, RE::BSFixedString);
    int ProcessPlayerConduct(RE::StaticFunctionTag*, RE::Actor*, RE::BSFixedString,
                              RE::BSFixedString, RE::BSFixedString);
    int CheckCrimeGoldStandings(RE::StaticFunctionTag*);
    int DecayPlayerStandings(RE::StaticFunctionTag*, int);
    void WritePoliticalStateFile(RE::StaticFunctionTag*);
    void SetPoliticsEnabled(RE::StaticFunctionTag*, bool);
    void SetPoliticsTickInterval(RE::StaticFunctionTag*, int);
    int DeclareWar(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, float);
    RE::BSFixedString ProcessWarTick(RE::StaticFunctionTag*, float);
    bool EndFactionWar(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString, float);
    int GetActiveWarCount(RE::StaticFunctionTag*);
    int GetActiveWarId(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString);
    int GetWarStrength(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString);
    int RecordOffScreenBattle(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString,
                              RE::BSFixedString, RE::BSFixedString, RE::BSFixedString, int, int, RE::BSFixedString);

    // Event manifestation forward declarations
    RE::BSFixedString CheckEventManifestation(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString);
    void ConfirmManifestationCooldown(RE::StaticFunctionTag*);

    // Faction query forward declarations
    bool IsHighStatusNPC(RE::StaticFunctionTag*, RE::Actor*);
    RE::BSFixedString ExtractFactionId(RE::StaticFunctionTag*, RE::BSFixedString);
    RE::BSFixedString GetFactionDisplayName(RE::StaticFunctionTag*, RE::BSFixedString);
    RE::BSFixedString GetFactionRival(RE::StaticFunctionTag*, RE::BSFixedString);
    RE::BSFixedString GetFactionWarEnemy(RE::StaticFunctionTag*, RE::BSFixedString);
    RE::BSFixedString GetNPCPoliticalFactionId(RE::StaticFunctionTag*, RE::Actor*);
    RE::Actor* FindFactionMember(RE::StaticFunctionTag*, RE::BSFixedString);

    // Battle system forward declarations
    RE::BSFixedString GetFactionSoldierTemplate(RE::StaticFunctionTag*, RE::BSFixedString);
    std::vector<RE::Actor*> SpawnBattleSoldiers(RE::StaticFunctionTag*, RE::BSFixedString, RE::TESObjectREFR*);
    RE::BSFixedString ExecuteFullBattleSpawn(RE::StaticFunctionTag*, RE::BSFixedString, RE::Actor*, float, RE::TESObjectREFR*);
    RE::BSFixedString SpawnReinforcements(RE::StaticFunctionTag*, int, RE::Actor*, RE::TESObjectREFR*);
    int GetJsonArrayInt(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, int);
    void SetActorProtected(RE::StaticFunctionTag*, RE::Actor*, bool);
    RE::BSFixedString GetBattleSoldierFormIds(RE::StaticFunctionTag*, RE::BSFixedString);
    void SetBattleSoldiersAsTeammates(RE::StaticFunctionTag*, RE::BSFixedString);
    int CountDeadBattleSoldiers(RE::StaticFunctionTag*, RE::BSFixedString);
    int CleanupBattleSoldiers(RE::StaticFunctionTag*, float, float, float, float, bool);
    void ForceCleanupAllSoldiers(RE::StaticFunctionTag*);
    void SnapshotBounties(RE::StaticFunctionTag*);
    void ClearBattleTeammates(RE::StaticFunctionTag*);
    void ClearAllHoldBounties(RE::StaticFunctionTag*);

    // Phase 2 Migration: FactionPolitics C++ logic
    RE::BSFixedString ProcessPoliticalDMResponse(RE::StaticFunctionTag*, RE::BSFixedString, int);
    RE::BSFixedString RunStandingMechanicsNative(RE::StaticFunctionTag*);

    // Remaining Migration: Text builders, math, display formatters
    RE::BSFixedString BuildTaskHistoryDesc(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString);
    RE::BSFixedString GetSlotStatusNative(RE::StaticFunctionTag*, RE::BSFixedString, int, RE::BSFixedString, RE::BSFixedString);
    RE::BSFixedString GetPreciseTimeDescriptionNative(RE::StaticFunctionTag*, float, float);
    RE::BSFixedString GetTimeDescriptionNative(RE::StaticFunctionTag*, float);
    RE::BSFixedString DetermineLatenessOutcomeNative(RE::StaticFunctionTag*, float, float, float);
    RE::BSFixedString BuildStuckNarration(RE::StaticFunctionTag*, RE::BSFixedString);
    bool IsUrgentMessage(RE::StaticFunctionTag*, RE::BSFixedString);
    RE::BSFixedString GetScheduleDisplayNative(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString);
    RE::BSFixedString GetScheduleStatusNative(RE::StaticFunctionTag*, int, float, float);
    float CalculateDepartureBuffer(RE::StaticFunctionTag*, float, float);

    // Phase 3 Migration: StoryEngine helpers
    RE::BSFixedString BuildExcludeListNative(RE::StaticFunctionTag*, int, int);
    RE::BSFixedString ValidateStoryResponse(RE::StaticFunctionTag*, RE::BSFixedString, int, int);
    RE::BSFixedString BuildFactionBattleDispatchFact(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString);
    RE::BSFixedString RecordFactionBattleCompletion(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString);
    RE::BSFixedString BuildBattleExpiryFact(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString);

    // Phase 1 Migration: BattleManager C++ logic (replaces Papyrus game logic)
    RE::BSFixedString FinalizeBattle(RE::StaticFunctionTag*, int, RE::BSFixedString, RE::BSFixedString, int, int, RE::BSFixedString, float);
    RE::BSFixedString CalculateReinforcementPositions(RE::StaticFunctionTag*, float, float, float, float, float, int);
    RE::BSFixedString EvaluatePlayerJoinBattle(RE::StaticFunctionTag*, RE::BSFixedString);
    RE::BSFixedString GetBattleNotification(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString, bool);
    RE::BSFixedString ValidateFactionBattleDispatch(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString);
    RE::BSFixedString CalculateMidBattleState(RE::StaticFunctionTag*, float, float);
    RE::BSFixedString GetPollAction(RE::StaticFunctionTag*, RE::BSFixedString);
    void ResetBattleState(RE::StaticFunctionTag*);
    RE::BSFixedString CalculateBattleMarkerPosition(RE::StaticFunctionTag*, float, float, float, float, float, float);

    // BattleManager forward declarations
    int StartBattle(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString, int);
    void EndBattle(RE::StaticFunctionTag*, int, RE::BSFixedString, RE::BSFixedString);
    bool RegisterBattleActor(RE::StaticFunctionTag*, RE::Actor*, RE::BSFixedString, int);
    RE::BSFixedString PollBattleState(RE::StaticFunctionTag*);
    int GetBattleMorale(RE::StaticFunctionTag*, RE::BSFixedString);
    void AdjustBattleMorale(RE::StaticFunctionTag*, RE::BSFixedString, int);
    bool IsBattleActive(RE::StaticFunctionTag*);
    int GetBattleAliveCount(RE::StaticFunctionTag*, RE::BSFixedString);
    int GetActiveBattleId(RE::StaticFunctionTag*);
    int GetBattleCurrentWave(RE::StaticFunctionTag*);
    void AdvanceBattleWave(RE::StaticFunctionTag*);
    bool SetPlayerBattleSide(RE::StaticFunctionTag*, RE::BSFixedString);
    void RemovePlayerCrimeFactions(RE::StaticFunctionTag*);
    void RestorePlayerCrimeFactions(RE::StaticFunctionTag*);
    RE::BSFixedString GetPlayerBattleSide(RE::StaticFunctionTag*);
    RE::BSFixedString GetFactionBattleSide(RE::StaticFunctionTag*, RE::BSFixedString);
    bool HasPlayerParticipatedInBattle(RE::StaticFunctionTag*);
    bool IsBattleFaction(RE::StaticFunctionTag*, RE::BSFixedString);

    // Pending Battle forward declarations
    int AddPendingBattle(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString, RE::BSFixedString);
    int PollPendingBattles(RE::StaticFunctionTag*);
    void RemovePendingBattle(RE::StaticFunctionTag*, int);
    void ClearPendingBattles(RE::StaticFunctionTag*);
    RE::BSFixedString GetPendingBattleInfo(RE::StaticFunctionTag*, int);
    int GetPendingBattleCount(RE::StaticFunctionTag*);
    RE::BSFixedString GetLastExpiredBattleResult(RE::StaticFunctionTag*);

    // Battle witness forward declarations
    std::vector<RE::Actor*> GetNearbyWitnessNPCs(RE::StaticFunctionTag*, RE::TESObjectREFR*, float);

    // JSON array helper forward declarations
    int GetJsonArrayLength(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString);
    RE::BSFixedString GetJsonArrayItem(RE::StaticFunctionTag*, RE::BSFixedString, RE::BSFixedString, int);

    bool Register(RE::BSScript::IVirtualMachine* a_vm) {
        if (!a_vm) {
            return false;
        }

        int count = 0;

        // NPC Search Functions
        a_vm->RegisterFunction("FindNPCByName", SCRIPT_NAME, FindNPCByName); ++count;
        a_vm->RegisterFunction("FindNPCByNameNear", SCRIPT_NAME, FindNPCByNameNear); ++count;
        a_vm->RegisterFunction("ResolveStoryCandidate", SCRIPT_NAME, ResolveStoryCandidate); ++count;
        a_vm->RegisterFunction("FindMessengerForSender", SCRIPT_NAME, FindMessengerForSender); ++count;
        a_vm->RegisterFunction("GetNPCCurrentLocation", SCRIPT_NAME, GetNPCCurrentLocation); ++count;
        a_vm->RegisterFunction("IsNPCAccessible", SCRIPT_NAME, IsNPCAccessible); ++count;
        a_vm->RegisterFunction("GetNPCNameSuggestion", SCRIPT_NAME, GetNPCNameSuggestion); ++count;

        // Actor Location Functions
        a_vm->RegisterFunction("GetActorParentLocationName", SCRIPT_NAME, GetActorParentLocationName); ++count;

        // Location Resolution Functions
        a_vm->RegisterFunction("ResolveLocationToCell", SCRIPT_NAME, ResolveLocationToCell); ++count;
        a_vm->RegisterFunction("ResolveLocationToBGSLocation", SCRIPT_NAME, ResolveLocationToBGSLocation); ++count;
        a_vm->RegisterFunction("FindDoorToLocation", SCRIPT_NAME, FindDoorToLocation); ++count;
        a_vm->RegisterFunction("ResolveSemanticLocation", SCRIPT_NAME, ResolveSemanticLocation); ++count;
        a_vm->RegisterFunction("ResolveAnyDestination", SCRIPT_NAME, ResolveAnyDestination); ++count;
        a_vm->RegisterFunction("GetCellSpatialInfo", SCRIPT_NAME, GetCellSpatialInfo); ++count;
        a_vm->RegisterFunction("IsSemanticTerm", SCRIPT_NAME, IsSemanticTerm); ++count;
        a_vm->RegisterFunction("GetAvailableSemanticDirections", SCRIPT_NAME, GetAvailableSemanticDirections); ++count;
        a_vm->RegisterFunction("GetLocationSuggestion", SCRIPT_NAME, GetLocationSuggestion); ++count;

        // Action Validation Functions
        a_vm->RegisterFunction("ValidateAction", SCRIPT_NAME, ValidateAction); ++count;
        a_vm->RegisterFunction("GetActionFailureReason", SCRIPT_NAME, GetActionFailureReason); ++count;

        // String Utility Functions
        a_vm->RegisterFunction("StringToLower", SCRIPT_NAME, StringToLower); ++count;
        a_vm->RegisterFunction("StringToUpper", SCRIPT_NAME, StringToUpper); ++count;
        a_vm->RegisterFunction("StringContains", SCRIPT_NAME, StringContains); ++count;
        a_vm->RegisterFunction("StringStartsWith", SCRIPT_NAME, StringStartsWith); ++count;
        a_vm->RegisterFunction("StringEndsWith", SCRIPT_NAME, StringEndsWith); ++count;
        a_vm->RegisterFunction("LevenshteinDistance", SCRIPT_NAME, LevenshteinDistance); ++count;
        a_vm->RegisterFunction("StringTrim", SCRIPT_NAME, StringTrim); ++count;
        a_vm->RegisterFunction("StringEscapeJson", SCRIPT_NAME, StringEscapeJson); ++count;
        a_vm->RegisterFunction("StringSplit", SCRIPT_NAME, StringSplit); ++count;

        // Index Management Functions
        a_vm->RegisterFunction("IsIndexLoaded", SCRIPT_NAME, IsIndexLoaded); ++count;
        a_vm->RegisterFunction("GetIndexStats", SCRIPT_NAME, GetIndexStats); ++count;
        a_vm->RegisterFunction("RebuildNPCIndex", SCRIPT_NAME, RebuildNPCIndex); ++count;
        a_vm->RegisterFunction("RebuildLocationIndex", SCRIPT_NAME, RebuildLocationIndex); ++count;

        // Cell Analysis Functions
        a_vm->RegisterFunction("GetCellDoors", SCRIPT_NAME, GetCellDoors); ++count;
        a_vm->RegisterFunction("GetDoorDestination", SCRIPT_NAME, GetDoorDestination); ++count;
        a_vm->RegisterFunction("GetDoorDestinationRef", SCRIPT_NAME, GetDoorDestinationRef); ++count;
        a_vm->RegisterFunction("IsDoorExterior", SCRIPT_NAME, IsDoorExterior); ++count;
        a_vm->RegisterFunction("IsDoorUpward", SCRIPT_NAME, IsDoorUpward); ++count;
        a_vm->RegisterFunction("IsDoorDownward", SCRIPT_NAME, IsDoorDownward); ++count;

        // Item Search Functions
        a_vm->RegisterFunction("FindItemInInventory", SCRIPT_NAME, FindItemInInventory); ++count;
        a_vm->RegisterFunction("FindNearbyItemByName", SCRIPT_NAME, FindNearbyItemByName); ++count;

        // Distance / Math Utility Functions
        a_vm->RegisterFunction("GetDistance3D", SCRIPT_NAME, GetDistance3D); ++count;
        a_vm->RegisterFunction("GetDistance2D", SCRIPT_NAME, GetDistance2D); ++count;
        a_vm->RegisterFunction("CalculateDeadlineFromDistance", SCRIPT_NAME, CalculateDeadlineFromDistance); ++count;
        a_vm->RegisterFunction("GetOffsetBehind", SCRIPT_NAME, GetOffsetBehind); ++count;

        // Time Parsing Functions
        a_vm->RegisterFunction("ParseTimeCondition", SCRIPT_NAME, ParseTimeCondition); ++count;
        a_vm->RegisterFunction("CalculateTargetGameTime", SCRIPT_NAME, CalculateTargetGameTime); ++count;

        // Departure Detection Functions
        a_vm->RegisterFunction("CheckDepartureStatus", SCRIPT_NAME, CheckDepartureStatus); ++count;
        a_vm->RegisterFunction("ResetDepartureSlot", SCRIPT_NAME, ResetDepartureSlot); ++count;
        a_vm->RegisterFunction("GetDepartureRetries", SCRIPT_NAME, GetDepartureRetries); ++count;

        // Stuck Detection Functions
        a_vm->RegisterFunction("CheckStuckStatus", SCRIPT_NAME, CheckStuckStatus); ++count;
        a_vm->RegisterFunction("ResetStuckSlot", SCRIPT_NAME, ResetStuckSlot); ++count;
        a_vm->RegisterFunction("GetTeleportDistance", SCRIPT_NAME, GetTeleportDistance); ++count;
        a_vm->RegisterFunction("GetStuckRecoveryAttempts", SCRIPT_NAME, GetStuckRecoveryAttempts); ++count;

        // Off-Screen Travel Detection Functions
        a_vm->RegisterFunction("InitOffScreenTravel", SCRIPT_NAME, InitOffScreenTravel); ++count;
        a_vm->RegisterFunction("CheckOffScreenProgress", SCRIPT_NAME, CheckOffScreenProgress); ++count;
        a_vm->RegisterFunction("ResetOffScreenSlot", SCRIPT_NAME, ResetOffScreenSlot); ++count;

        // Waypoint Navigation Functions
        a_vm->RegisterFunction("FindNearestWaypointToward", SCRIPT_NAME, FindNearestWaypointToward); ++count;

        // Home Door Access Functions (anti-trespass)
        a_vm->RegisterFunction("SetHomeDoorAccess", SCRIPT_NAME, SetHomeDoorAccess); ++count;
        a_vm->RegisterFunction("SetHomeDoorAccessForCell", SCRIPT_NAME, SetHomeDoorAccessForCell); ++count;
        a_vm->RegisterFunction("GetLastResolvedHomeCellId", SCRIPT_NAME, GetLastResolvedHomeCellId); ++count;

        // Slot Tracker Functions (C++ state mirror for SkyrimNet decorators)
        a_vm->RegisterFunction("UpdateSlotState", SCRIPT_NAME, UpdateSlotState); ++count;
        a_vm->RegisterFunction("ClearSlotState", SCRIPT_NAME, ClearSlotState); ++count;
        a_vm->RegisterFunction("IsActorAvailable", SCRIPT_NAME, IsActorAvailable); ++count;
        a_vm->RegisterFunction("HasBaseAIPackages", SCRIPT_NAME, HasBaseAIPackages); ++count;
        a_vm->RegisterFunction("HasNonSandboxAI", SCRIPT_NAME, HasNonSandboxAI); ++count;
        a_vm->RegisterFunction("GetEditorLocationRef", SCRIPT_NAME, GetEditorLocationRef); ++count;

        // Story Engine Functions
        a_vm->RegisterFunction("GetRandomStoryCandidate", SCRIPT_NAME, GetRandomStoryCandidate); ++count;
        a_vm->RegisterFunction("GetMemoryDrivenCandidate", SCRIPT_NAME, GetMemoryDrivenCandidate); ++count;
        a_vm->RegisterFunction("GetRelatedCandidate", SCRIPT_NAME, GetRelatedCandidate); ++count;
        a_vm->RegisterFunction("GetActorUUID", SCRIPT_NAME, GetActorUUID); ++count;
        a_vm->RegisterFunction("IsPlayerInDangerousLocation", SCRIPT_NAME, IsPlayerInDangerousLocation); ++count;
        a_vm->RegisterFunction("HasNearbyDungeonEntrance", SCRIPT_NAME, HasNearbyDungeonEntrance); ++count;
        a_vm->RegisterFunction("IsPlayerInOwnHome", SCRIPT_NAME, IsPlayerInOwnHome); ++count;
        a_vm->RegisterFunction("GetPlayerHomeExteriorDoor", SCRIPT_NAME, GetPlayerHomeExteriorDoor); ++count;
        a_vm->RegisterFunction("GetPlayerHomeInteriorDoor", SCRIPT_NAME, GetPlayerHomeInteriorDoor); ++count;
        a_vm->RegisterFunction("IsCivilianClass", SCRIPT_NAME, IsCivilianClass); ++count;
        a_vm->RegisterFunction("IsJarl", SCRIPT_NAME, IsJarl); ++count;
        a_vm->RegisterFunction("SetDangerZonePolicy", SCRIPT_NAME, SetDangerZonePolicy); ++count;
        a_vm->RegisterFunction("SetPlayerHomePolicy", SCRIPT_NAME, SetPlayerHomePolicy); ++count;
        a_vm->RegisterFunction("SetHoldRestrictionPolicy", SCRIPT_NAME, SetHoldRestrictionPolicy); ++count;
        a_vm->RegisterFunction("CheckHoldRestriction", SCRIPT_NAME, CheckHoldRestriction); ++count;
        a_vm->RegisterFunction("GetActorHoldName", SCRIPT_NAME, GetActorHoldName); ++count;
        a_vm->RegisterFunction("NPCKnowsPlayer", SCRIPT_NAME, NPCKnowsPlayer); ++count;
        a_vm->RegisterFunction("IsPotentialFollower", SCRIPT_NAME, IsPotentialFollower); ++count;
        a_vm->RegisterFunction("IsPlayerInBlockedLocation", SCRIPT_NAME, IsPlayerInBlockedLocation); ++count;
        a_vm->RegisterFunction("IsPlayerInWhitelistedLocation", SCRIPT_NAME, IsPlayerInWhitelistedLocation); ++count;
        a_vm->RegisterFunction("StoryResponseShouldAct", SCRIPT_NAME, StoryResponseShouldAct); ++count;
        a_vm->RegisterFunction("StoryResponseGetField", SCRIPT_NAME, StoryResponseGetField); ++count;
        a_vm->RegisterFunction("BuildActorContextJson", SCRIPT_NAME, BuildActorContextJson); ++count;
        a_vm->RegisterFunction("BuildDungeonMasterContext", SCRIPT_NAME, BuildDungeonMasterContext); ++count;
        a_vm->RegisterFunction("BuildNPCInteractionContext", SCRIPT_NAME, BuildNPCInteractionContext); ++count;
        a_vm->RegisterFunction("BuildNPCInteractionRequestJson", SCRIPT_NAME, BuildNPCInteractionRequestJson); ++count;
        a_vm->RegisterFunction("NotifyStoryCooldown", SCRIPT_NAME, NotifyStoryCooldown); ++count;
        a_vm->RegisterFunction("IsActorOnStoryCooldown", SCRIPT_NAME, IsActorOnStoryCooldown); ++count;
        a_vm->RegisterFunction("NotifySocialCooldown", SCRIPT_NAME, NotifySocialCooldown); ++count;
        a_vm->RegisterFunction("NotifyStoryTypePicked", SCRIPT_NAME, NotifyStoryTypePicked); ++count;
        a_vm->RegisterFunction("WarmStoryTypeCountsFromCSV", SCRIPT_NAME, WarmStoryTypeCountsFromCSV); ++count;
        a_vm->RegisterFunction("SetRecentGossipContext", SCRIPT_NAME, SetRecentGossipContext); ++count;
        a_vm->RegisterFunction("GetDMCandidatePoolFormIDs", SCRIPT_NAME, GetDMCandidatePoolFormIDs); ++count;
        a_vm->RegisterFunction("GetNPCCandidatePoolFormIDs", SCRIPT_NAME, GetNPCCandidatePoolFormIDs); ++count;
        a_vm->RegisterFunction("ScanActorsWithPackages", SCRIPT_NAME, ScanActorsWithPackages); ++count;
        a_vm->RegisterFunction("SpawnQuestEnemies", SCRIPT_NAME, SpawnQuestEnemies); ++count;
        a_vm->RegisterFunction("SpawnQuestBoss", SCRIPT_NAME, SpawnQuestBoss); ++count;
        a_vm->RegisterFunction("SpawnQuestChest", SCRIPT_NAME, SpawnQuestChest); ++count;
        a_vm->RegisterFunction("FindDeeperSpawnPoint", SCRIPT_NAME, FindDeeperSpawnPoint); ++count;
        a_vm->RegisterFunction("FindPrisonerFurniture", SCRIPT_NAME, FindPrisonerFurniture); ++count;
        a_vm->RegisterFunction("FindUsablePrisonerFurniture", SCRIPT_NAME, FindUsablePrisonerFurniture); ++count;
        a_vm->RegisterFunction("FindRescueAnchor", SCRIPT_NAME, FindRescueAnchor); ++count;
        a_vm->RegisterFunction("ScanAheadForAnchor", SCRIPT_NAME, ScanAheadForAnchor); ++count;
        a_vm->RegisterFunction("GetDungeonBossAnchor", SCRIPT_NAME, GetDungeonBossAnchor); ++count;
        a_vm->RegisterFunction("IsQuestItemInChest", SCRIPT_NAME, IsQuestItemInChest); ++count;
        a_vm->RegisterFunction("ValidateQuestItem", SCRIPT_NAME, ValidateQuestItem); ++count;
        a_vm->RegisterFunction("GetRandomQuestItemName", SCRIPT_NAME, GetRandomQuestItemName); ++count;
        a_vm->RegisterFunction("NotifyQuestItemUsed", SCRIPT_NAME, NotifyQuestItemUsed); ++count;
        a_vm->RegisterFunction("NotifyRescueVictimUsed", SCRIPT_NAME, NotifyRescueVictimUsed); ++count;
        a_vm->RegisterFunction("NotifyQuestLocationUsed", SCRIPT_NAME, NotifyQuestLocationUsed); ++count;

        // MemoryDB Functions (SkyrimNet SQLite reader)
        a_vm->RegisterFunction("GetNPCMemories", SCRIPT_NAME, GetNPCMemories); ++count;
        a_vm->RegisterFunction("GetRecentWorldEvents", SCRIPT_NAME, GetRecentWorldEvents); ++count;
        a_vm->RegisterFunction("GetActiveStoryNPCs", SCRIPT_NAME, GetActiveStoryNPCs); ++count;
        a_vm->RegisterFunction("GetNPCRelationshipSummary", SCRIPT_NAME, GetNPCRelationshipSummary); ++count;
        a_vm->RegisterFunction("IsMemoryDBConnected", SCRIPT_NAME, IsMemoryDBConnected); ++count;
        a_vm->RegisterFunction("GetPlayerInteractionCount", SCRIPT_NAME, GetPlayerInteractionCount); ++count;

        // Dialogue Safety Net Functions
        a_vm->RegisterFunction("RunSafetyNetCheck", SCRIPT_NAME, RunSafetyNetCheck); ++count;
        a_vm->RegisterFunction("NotifyNewDialogue", SCRIPT_NAME, NotifyNewDialogue); ++count;
        a_vm->RegisterFunction("GetSafetyNetNPC", SCRIPT_NAME, GetSafetyNetNPC); ++count;
        a_vm->RegisterFunction("GetLastConversationPartner", SCRIPT_NAME, GetLastConversationPartner); ++count;
        a_vm->RegisterFunction("GetRecentDialogue", SCRIPT_NAME, GetRecentDialogue); ++count;
        a_vm->RegisterFunction("HasScheduleKeywords", SCRIPT_NAME, HasScheduleKeywords); ++count;
        a_vm->RegisterFunction("BuildSafetyNetContextJson", SCRIPT_NAME, BuildSafetyNetContextJson); ++count;
        a_vm->RegisterFunction("BuildStoryDMRequestJson", SCRIPT_NAME, BuildStoryDMRequestJson); ++count;

        // Bio Section Pre-Rendering Functions
        a_vm->RegisterFunction("RenderFactsSection", SCRIPT_NAME, RenderFactsSection); ++count;
        a_vm->RegisterFunction("RenderGossipHeardSection", SCRIPT_NAME, RenderGossipHeardSection); ++count;
        a_vm->RegisterFunction("RenderGossipToldSection", SCRIPT_NAME, RenderGossipToldSection); ++count;
        a_vm->RegisterFunction("RenderTaskHistorySection", SCRIPT_NAME, RenderTaskHistorySection); ++count;

        // Dashboard Config Functions
        a_vm->RegisterFunction("NotifyDashboardSlotChanged", SCRIPT_NAME, NotifyDashboardSlotChanged); ++count;
        a_vm->RegisterFunction("GetDashboardHotkey", SCRIPT_NAME, GetDashboardHotkey); ++count;
        a_vm->RegisterFunction("SetDashboardHotkey", SCRIPT_NAME, SetDashboardHotkey); ++count;
        a_vm->RegisterFunction("ReloadDashboardUI", SCRIPT_NAME, ReloadDashboardUI); ++count;
        a_vm->RegisterFunction("ReloadDashboardConfig", SCRIPT_NAME, ReloadDashboardConfig); ++count;
        a_vm->RegisterFunction("PushDashboardFullState", SCRIPT_NAME, PushDashboardFullState); ++count;
        a_vm->RegisterFunction("IsDashboardOpen", SCRIPT_NAME, IsDashboardOpen); ++count;
        a_vm->RegisterFunction("GetPendingDirectorParam", SCRIPT_NAME, GetPendingDirectorParam); ++count;
        a_vm->RegisterFunction("ClearPendingDirectorParams", SCRIPT_NAME, ClearPendingDirectorParams); ++count;
        a_vm->RegisterFunction("ClaimPendingDirectorParams", SCRIPT_NAME, ClaimPendingDirectorParams); ++count;

        // Faction Politics Functions
        a_vm->RegisterFunction("GetFactionRelation", SCRIPT_NAME, GetFactionRelation); ++count;
        a_vm->RegisterFunction("AdjustFactionRelation", SCRIPT_NAME, AdjustFactionRelation); ++count;
        a_vm->RegisterFunction("GetPlayerFactionStanding", SCRIPT_NAME, GetPlayerFactionStanding); ++count;
        a_vm->RegisterFunction("AdjustPlayerFactionStanding", SCRIPT_NAME, AdjustPlayerFactionStanding); ++count;
        a_vm->RegisterFunction("IsFactionAtWar", SCRIPT_NAME, IsFactionAtWar); ++count;
        a_vm->RegisterFunction("GetWarMorale", SCRIPT_NAME, GetWarMorale); ++count;
        a_vm->RegisterFunction("GetRelationStatus", SCRIPT_NAME, GetRelationStatus); ++count;
        a_vm->RegisterFunction("BuildPoliticalContext", SCRIPT_NAME, BuildPoliticalContext); ++count;
        a_vm->RegisterFunction("BuildPoliticalDashboardJson", SCRIPT_NAME, BuildPoliticalDashboardJson); ++count;
        a_vm->RegisterFunction("RecordPoliticalEvent", SCRIPT_NAME, RecordPoliticalEvent); ++count;
        a_vm->RegisterFunction("IsPoliticsEnabled", SCRIPT_NAME, IsPoliticsEnabled); ++count;
        a_vm->RegisterFunction("GetPoliticsTickInterval", SCRIPT_NAME, GetPoliticsTickInterval); ++count;
        a_vm->RegisterFunction("ReloadFactionConfig", SCRIPT_NAME, ReloadFactionConfig); ++count;
        a_vm->RegisterFunction("GetFactionLeaderActors", SCRIPT_NAME, GetFactionLeaderActors); ++count;
        a_vm->RegisterFunction("GetFactionLeaderFormIds", SCRIPT_NAME, GetFactionLeaderFormIds); ++count;
        a_vm->RegisterFunction("ApplyPlayerStandingChanges", SCRIPT_NAME, ApplyPlayerStandingChanges); ++count;
        a_vm->RegisterFunction("ProcessPlayerConduct", SCRIPT_NAME, ProcessPlayerConduct); ++count;
        a_vm->RegisterFunction("CheckCrimeGoldStandings", SCRIPT_NAME, CheckCrimeGoldStandings); ++count;
        a_vm->RegisterFunction("DecayPlayerStandings", SCRIPT_NAME, DecayPlayerStandings); ++count;
        a_vm->RegisterFunction("WritePoliticalStateFile", SCRIPT_NAME, WritePoliticalStateFile); ++count;
        a_vm->RegisterFunction("SetPoliticsEnabled", SCRIPT_NAME, SetPoliticsEnabled); ++count;

        // Phase 2 Migration: FactionPolitics C++ logic
        a_vm->RegisterFunction("ProcessPoliticalDMResponse", SCRIPT_NAME, ProcessPoliticalDMResponse); ++count;
        a_vm->RegisterFunction("RunStandingMechanics", SCRIPT_NAME, RunStandingMechanicsNative); ++count;

        // Remaining Migration: Text builders, math, display formatters
        a_vm->RegisterFunction("BuildTaskHistoryDesc", SCRIPT_NAME, BuildTaskHistoryDesc); ++count;
        a_vm->RegisterFunction("GetSlotStatusNative", SCRIPT_NAME, GetSlotStatusNative); ++count;
        a_vm->RegisterFunction("GetPreciseTimeDescription", SCRIPT_NAME, GetPreciseTimeDescriptionNative); ++count;
        a_vm->RegisterFunction("GetTimeDescription", SCRIPT_NAME, GetTimeDescriptionNative); ++count;
        a_vm->RegisterFunction("DetermineLatenessOutcome", SCRIPT_NAME, DetermineLatenessOutcomeNative); ++count;
        a_vm->RegisterFunction("IsUrgentMessage", SCRIPT_NAME, IsUrgentMessage); ++count;
        a_vm->RegisterFunction("BuildStuckNarration", SCRIPT_NAME, BuildStuckNarration); ++count;

        // Phase 3 Migration: StoryEngine helpers
        a_vm->RegisterFunction("BuildExcludeList", SCRIPT_NAME, BuildExcludeListNative); ++count;
        a_vm->RegisterFunction("ValidateStoryResponse", SCRIPT_NAME, ValidateStoryResponse); ++count;
        a_vm->RegisterFunction("BuildFactionBattleDispatchFact", SCRIPT_NAME, BuildFactionBattleDispatchFact); ++count;
        a_vm->RegisterFunction("RecordFactionBattleCompletion", SCRIPT_NAME, RecordFactionBattleCompletion); ++count;
        a_vm->RegisterFunction("BuildBattleExpiryFact", SCRIPT_NAME, BuildBattleExpiryFact); ++count;
        a_vm->RegisterFunction("SetPoliticsTickInterval", SCRIPT_NAME, SetPoliticsTickInterval); ++count;
        a_vm->RegisterFunction("DeclareWar", SCRIPT_NAME, DeclareWar); ++count;
        a_vm->RegisterFunction("ProcessWarTick", SCRIPT_NAME, ProcessWarTick); ++count;
        a_vm->RegisterFunction("EndFactionWar", SCRIPT_NAME, EndFactionWar); ++count;
        a_vm->RegisterFunction("GetActiveWarCount", SCRIPT_NAME, GetActiveWarCount); ++count;
        a_vm->RegisterFunction("GetActiveWarId", SCRIPT_NAME, GetActiveWarId); ++count;
        a_vm->RegisterFunction("GetWarStrength", SCRIPT_NAME, GetWarStrength); ++count;
        a_vm->RegisterFunction("RecordOffScreenBattle", SCRIPT_NAME, RecordOffScreenBattle); ++count;
        a_vm->RegisterFunction("CheckEventManifestation", SCRIPT_NAME, CheckEventManifestation); ++count;
        a_vm->RegisterFunction("ConfirmManifestationCooldown", SCRIPT_NAME, ConfirmManifestationCooldown); ++count;

        // NPC Status Check
        a_vm->RegisterFunction("IsHighStatusNPC", SCRIPT_NAME, IsHighStatusNPC); ++count;

        // Faction Query Functions
        a_vm->RegisterFunction("ExtractFactionId", SCRIPT_NAME, ExtractFactionId); ++count;
        a_vm->RegisterFunction("GetFactionDisplayName", SCRIPT_NAME, GetFactionDisplayName); ++count;
        a_vm->RegisterFunction("GetFactionRival", SCRIPT_NAME, GetFactionRival); ++count;
        a_vm->RegisterFunction("GetFactionWarEnemy", SCRIPT_NAME, GetFactionWarEnemy); ++count;
        a_vm->RegisterFunction("GetNPCPoliticalFactionId", SCRIPT_NAME, GetNPCPoliticalFactionId); ++count;
        a_vm->RegisterFunction("FindFactionMember", SCRIPT_NAME, FindFactionMember); ++count;

        // Battle System Functions
        a_vm->RegisterFunction("GetFactionSoldierTemplate", SCRIPT_NAME, GetFactionSoldierTemplate); ++count;
        a_vm->RegisterFunction("SpawnBattleSoldiers", SCRIPT_NAME, SpawnBattleSoldiers); ++count;
        a_vm->RegisterFunction("ExecuteFullBattleSpawn", SCRIPT_NAME, ExecuteFullBattleSpawn); ++count;
        a_vm->RegisterFunction("SpawnReinforcements", SCRIPT_NAME, SpawnReinforcements); ++count;
        a_vm->RegisterFunction("GetJsonArrayInt", SCRIPT_NAME, GetJsonArrayInt); ++count;
        a_vm->RegisterFunction("SetActorProtected", SCRIPT_NAME, SetActorProtected); ++count;
        a_vm->RegisterFunction("GetBattleSoldierFormIds", SCRIPT_NAME, GetBattleSoldierFormIds); ++count;
        a_vm->RegisterFunction("SetBattleSoldiersAsTeammates", SCRIPT_NAME, SetBattleSoldiersAsTeammates); ++count;
        a_vm->RegisterFunction("CountDeadBattleSoldiers", SCRIPT_NAME, CountDeadBattleSoldiers); ++count;
        a_vm->RegisterFunction("CleanupBattleSoldiers", SCRIPT_NAME, CleanupBattleSoldiers); ++count;
        a_vm->RegisterFunction("ForceCleanupAllSoldiers", SCRIPT_NAME, ForceCleanupAllSoldiers); ++count;
        a_vm->RegisterFunction("SnapshotBounties", SCRIPT_NAME, SnapshotBounties); ++count;
        a_vm->RegisterFunction("ClearBattleTeammates", SCRIPT_NAME, ClearBattleTeammates); ++count;
        a_vm->RegisterFunction("ClearAllHoldBounties", SCRIPT_NAME, ClearAllHoldBounties); ++count;
        a_vm->RegisterFunction("StartBattle", SCRIPT_NAME, StartBattle); ++count;
        a_vm->RegisterFunction("EndBattle", SCRIPT_NAME, EndBattle); ++count;
        a_vm->RegisterFunction("RegisterBattleActor", SCRIPT_NAME, RegisterBattleActor); ++count;
        a_vm->RegisterFunction("PollBattleState", SCRIPT_NAME, PollBattleState); ++count;
        a_vm->RegisterFunction("GetBattleMorale", SCRIPT_NAME, GetBattleMorale); ++count;
        a_vm->RegisterFunction("AdjustBattleMorale", SCRIPT_NAME, AdjustBattleMorale); ++count;
        a_vm->RegisterFunction("IsBattleActive", SCRIPT_NAME, IsBattleActive); ++count;
        a_vm->RegisterFunction("GetBattleAliveCount", SCRIPT_NAME, GetBattleAliveCount); ++count;
        a_vm->RegisterFunction("GetActiveBattleId", SCRIPT_NAME, GetActiveBattleId); ++count;
        a_vm->RegisterFunction("GetBattleCurrentWave", SCRIPT_NAME, GetBattleCurrentWave); ++count;
        a_vm->RegisterFunction("AdvanceBattleWave", SCRIPT_NAME, AdvanceBattleWave); ++count;
        a_vm->RegisterFunction("SetPlayerBattleSide", SCRIPT_NAME, SetPlayerBattleSide); ++count;
        a_vm->RegisterFunction("RemovePlayerCrimeFactions", SCRIPT_NAME, RemovePlayerCrimeFactions); ++count;
        a_vm->RegisterFunction("RestorePlayerCrimeFactions", SCRIPT_NAME, RestorePlayerCrimeFactions); ++count;
        // Phase 4: used by intel_join_battle, intel_accept_recruitment, and intel_broker_peace actions
        a_vm->RegisterFunction("GetPlayerBattleSide", SCRIPT_NAME, GetPlayerBattleSide); ++count;
        a_vm->RegisterFunction("GetFactionBattleSide", SCRIPT_NAME, GetFactionBattleSide); ++count;
        a_vm->RegisterFunction("HasPlayerParticipatedInBattle", SCRIPT_NAME, HasPlayerParticipatedInBattle); ++count;
        a_vm->RegisterFunction("IsBattleFaction", SCRIPT_NAME, IsBattleFaction); ++count;

        // Phase 1 Migration: BattleManager C++ logic
        a_vm->RegisterFunction("FinalizeBattle", SCRIPT_NAME, FinalizeBattle); ++count;
        a_vm->RegisterFunction("CalculateReinforcementPositions", SCRIPT_NAME, CalculateReinforcementPositions); ++count;
        a_vm->RegisterFunction("EvaluatePlayerJoinBattle", SCRIPT_NAME, EvaluatePlayerJoinBattle); ++count;
        a_vm->RegisterFunction("GetBattleNotification", SCRIPT_NAME, GetBattleNotification); ++count;
        a_vm->RegisterFunction("ValidateFactionBattleDispatch", SCRIPT_NAME, ValidateFactionBattleDispatch); ++count;
        a_vm->RegisterFunction("CalculateMidBattleState", SCRIPT_NAME, CalculateMidBattleState); ++count;
        a_vm->RegisterFunction("GetBattlePollAction", SCRIPT_NAME, GetPollAction); ++count;
        a_vm->RegisterFunction("ResetBattleState", SCRIPT_NAME, ResetBattleState); ++count;
        a_vm->RegisterFunction("CalculateBattleMarkerPosition", SCRIPT_NAME, CalculateBattleMarkerPosition); ++count;

        // Pending Battle Functions
        a_vm->RegisterFunction("AddPendingBattle", SCRIPT_NAME, AddPendingBattle); ++count;
        a_vm->RegisterFunction("PollPendingBattles", SCRIPT_NAME, PollPendingBattles); ++count;
        a_vm->RegisterFunction("RemovePendingBattle", SCRIPT_NAME, RemovePendingBattle); ++count;
        a_vm->RegisterFunction("ClearPendingBattles", SCRIPT_NAME, ClearPendingBattles); ++count;
        a_vm->RegisterFunction("GetPendingBattleInfo", SCRIPT_NAME, GetPendingBattleInfo); ++count;
        a_vm->RegisterFunction("GetPendingBattleCount", SCRIPT_NAME, GetPendingBattleCount); ++count;
        a_vm->RegisterFunction("GetLastExpiredBattleResult", SCRIPT_NAME, GetLastExpiredBattleResult); ++count;

        // Battle Witness Functions
        a_vm->RegisterFunction("GetNearbyWitnessNPCs", SCRIPT_NAME, GetNearbyWitnessNPCs); ++count;

        // JSON Array Helper Functions
        a_vm->RegisterFunction("GetJsonArrayLength", SCRIPT_NAME, GetJsonArrayLength); ++count;
        a_vm->RegisterFunction("GetJsonArrayItem", SCRIPT_NAME, GetJsonArrayItem); ++count;

        // Debug Functions
        a_vm->RegisterFunction("TestNPCSearch", SCRIPT_NAME, TestNPCSearch); ++count;
        a_vm->RegisterFunction("TestLocationResolve", SCRIPT_NAME, TestLocationResolve); ++count;
        a_vm->RegisterFunction("TestSemanticResolve", SCRIPT_NAME, TestSemanticResolve); ++count;
        a_vm->RegisterFunction("TestValidation", SCRIPT_NAME, TestValidation); ++count;
        a_vm->RegisterFunction("SetDebugLevel", SCRIPT_NAME, SetDebugLevel); ++count;
        a_vm->RegisterFunction("GetVersion", SCRIPT_NAME, GetVersion); ++count;

        logger::info("Registered {} Papyrus functions", count);
        return true;
    }

    // ==========================================================================
    // NPC Search Functions
    // ==========================================================================

    RE::Actor* FindNPCByName(RE::StaticFunctionTag*, RE::BSFixedString searchTerm) {
        auto* str = searchTerm.c_str();
        if (!str || !*str) return nullptr;
        return NPCIndex::GetSingleton()->FindByName(str);
    }

    RE::Actor* FindNPCByNameNear(RE::StaticFunctionTag*, RE::BSFixedString searchTerm, RE::Actor* nearActor) {
        auto* str = searchTerm.c_str();
        if (!str || !*str) return nullptr;
        return NPCIndex::GetSingleton()->FindByNameNear(str, nearActor);
    }

    RE::Actor* ResolveStoryCandidate(RE::StaticFunctionTag*, RE::BSFixedString name) {
        auto* str = name.c_str();
        if (!str || !*str) return nullptr;
        return NPCIndex::GetSingleton()->ResolveStoryCandidate(str);
    }

    RE::Actor* FindMessengerForSender(RE::StaticFunctionTag*, RE::Actor* sender) {
        return NPCIndex::GetSingleton()->FindMessengerForSender(sender);
    }

    RE::BSFixedString GetNPCCurrentLocation(RE::StaticFunctionTag*, RE::Actor* akNPC) {
        if (!akNPC) {
            return "";
        }
        return LocationResolver::GetSingleton()->GetActorLocationName(akNPC);
    }

    bool IsNPCAccessible(RE::StaticFunctionTag*, RE::Actor* akNPC) {
        if (!akNPC) {
            return false;
        }
        return NPCIndex::GetSingleton()->IsAccessible(akNPC);
    }

    RE::BSFixedString GetNPCNameSuggestion(RE::StaticFunctionTag*, RE::BSFixedString searchTerm) {
        auto* str = searchTerm.c_str();
        if (!str || !*str) return "";
        return NPCIndex::GetSingleton()->GetSuggestion(str);
    }

    // ==========================================================================
    // Actor Location Functions
    // ==========================================================================

    RE::BSFixedString GetActorParentLocationName(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return "";

        auto* loc = actor->GetCurrentLocation();
        if (!loc) return "";

        // Walk up to parent location (e.g., The Resting Pilgrim -> Helgen)
        auto* parent = loc->parentLoc;
        if (parent) {
            auto name = parent->GetFullName();
            if (name && strlen(name) > 0) {
                return name;
            }
        }

        // No parent, return the location's own name
        auto name = loc->GetFullName();
        return (name && strlen(name) > 0) ? name : "";
    }

    // ==========================================================================
    // Location Resolution Functions
    // ==========================================================================

    RE::TESObjectCELL* ResolveLocationToCell(RE::StaticFunctionTag*, RE::BSFixedString locationName) {
        return LocationResolver::GetSingleton()->ResolveCell(locationName.c_str());
    }

    RE::BGSLocation* ResolveLocationToBGSLocation(RE::StaticFunctionTag*, RE::BSFixedString locationName) {
        return LocationResolver::GetSingleton()->ResolveLocation(locationName.c_str());
    }

    RE::TESObjectREFR* FindDoorToLocation(RE::StaticFunctionTag*, RE::BSFixedString locationName) {
        return LocationResolver::GetSingleton()->FindDoorTo(locationName.c_str());
    }

    RE::TESObjectREFR* ResolveSemanticLocation(RE::StaticFunctionTag*,
                                                RE::Actor* akNPC,
                                                RE::BSFixedString semanticTerm) {
        if (!akNPC) {
            return nullptr;
        }
        return LocationResolver::GetSingleton()->ResolveSemantic(akNPC, semanticTerm.c_str());
    }

    RE::TESObjectREFR* ResolveAnyDestination(RE::StaticFunctionTag*,
                                              RE::Actor* akNPC,
                                              RE::BSFixedString destination) {
        if (!akNPC) {
            return nullptr;
        }
        return LocationResolver::GetSingleton()->ResolveAnyDestination(akNPC, destination.c_str());
    }

    RE::BSFixedString GetCellSpatialInfo(RE::StaticFunctionTag*, RE::Actor* akNPC) {
        if (!akNPC) {
            return "{}";
        }
        return CellAnalyzer::GetSingleton()->GetSpatialInfoJSON(akNPC);
    }

    bool IsSemanticTerm(RE::StaticFunctionTag*, RE::BSFixedString term) {
        return LocationResolver::GetSingleton()->IsSemanticTerm(term.c_str());
    }

    std::vector<RE::BSFixedString> GetAvailableSemanticDirections(RE::StaticFunctionTag*,
                                                                   RE::Actor* akNPC) {
        if (!akNPC) {
            return {};
        }
        return LocationResolver::GetSingleton()->GetAvailableDirections(akNPC);
    }

    RE::BSFixedString GetLocationSuggestion(RE::StaticFunctionTag*, RE::BSFixedString searchTerm) {
        return LocationResolver::GetSingleton()->GetLocationSuggestion(searchTerm.c_str());
    }

    // ==========================================================================
    // Action Validation Functions
    // ==========================================================================

    bool ValidateAction(RE::StaticFunctionTag*,
                        RE::Actor* akNPC,
                        RE::BSFixedString actionType,
                        RE::BSFixedString targetParam) {
        return ActionValidator::GetSingleton()->Validate(akNPC, actionType.c_str(), targetParam.c_str());
    }

    RE::BSFixedString GetActionFailureReason(RE::StaticFunctionTag*,
                                              RE::Actor* akNPC,
                                              RE::BSFixedString actionType,
                                              RE::BSFixedString targetParam) {
        return ActionValidator::GetSingleton()->GetFailureReason(akNPC, actionType.c_str(), targetParam.c_str());
    }

    // ==========================================================================
    // String Utility Functions
    // ==========================================================================

    RE::BSFixedString StringToLower(RE::StaticFunctionTag*, RE::BSFixedString text) {
        return StringUtils::ToLower(text.c_str());
    }

    RE::BSFixedString StringToUpper(RE::StaticFunctionTag*, RE::BSFixedString text) {
        return StringUtils::ToUpper(text.c_str());
    }

    bool StringContains(RE::StaticFunctionTag*, RE::BSFixedString haystack, RE::BSFixedString needle) {
        return StringUtils::Contains(haystack.c_str(), needle.c_str());
    }

    bool StringStartsWith(RE::StaticFunctionTag*, RE::BSFixedString text, RE::BSFixedString prefix) {
        return StringUtils::StartsWith(text.c_str(), prefix.c_str());
    }

    bool StringEndsWith(RE::StaticFunctionTag*, RE::BSFixedString text, RE::BSFixedString suffix) {
        return StringUtils::EndsWith(text.c_str(), suffix.c_str());
    }

    int LevenshteinDistance(RE::StaticFunctionTag*, RE::BSFixedString a, RE::BSFixedString b) {
        return StringUtils::LevenshteinDistance(a.c_str(), b.c_str());
    }

    RE::BSFixedString StringTrim(RE::StaticFunctionTag*, RE::BSFixedString text) {
        return StringUtils::Trim(text.c_str());
    }

    RE::BSFixedString StringEscapeJson(RE::StaticFunctionTag*, RE::BSFixedString text) {
        std::string input(text.c_str());
        std::string output;
        output.reserve(input.size() + 8);
        for (char c : input) {
            switch (c) {
                case '"':  output += "\\\""; break;
                case '\\': output += "\\\\"; break;
                case '\n': output += "\\n"; break;
                case '\r': output += "\\r"; break;
                case '\t': output += "\\t"; break;
                default:   output += c; break;
            }
        }
        return RE::BSFixedString(output);
    }

    std::vector<RE::BSFixedString> StringSplit(RE::StaticFunctionTag*,
                                                RE::BSFixedString text,
                                                RE::BSFixedString delimiter) {
        auto parts = StringUtils::Split(text.c_str(), delimiter.c_str());
        std::vector<RE::BSFixedString> result;
        result.reserve(parts.size());
        for (const auto& part : parts) {
            result.push_back(RE::BSFixedString(part));
        }
        return result;
    }

    // ==========================================================================
    // Index Management Functions
    // ==========================================================================

    bool IsIndexLoaded(RE::StaticFunctionTag*) {
        return LocationResolver::GetSingleton()->IsIndexBuilt() &&
               NPCIndex::GetSingleton()->IsIndexBuilt();
    }

    RE::BSFixedString GetIndexStats(RE::StaticFunctionTag*) {
        return LocationResolver::GetSingleton()->GetStatsJSON();
    }

    void RebuildNPCIndex(RE::StaticFunctionTag*) {
        NPCIndex::GetSingleton()->RebuildIndex();
    }

    void RebuildLocationIndex(RE::StaticFunctionTag*) {
        LocationResolver::GetSingleton()->BuildLocationIndex();
    }

    // ==========================================================================
    // Cell Analysis Functions
    // ==========================================================================

    std::vector<RE::TESObjectREFR*> GetCellDoors(RE::StaticFunctionTag*, RE::Actor* akNPC) {
        if (!akNPC) {
            return {};
        }
        return CellAnalyzer::GetSingleton()->GetDoors(akNPC);
    }

    RE::BSFixedString GetDoorDestination(RE::StaticFunctionTag*, RE::TESObjectREFR* akDoor) {
        if (!akDoor) {
            return "";
        }
        return CellAnalyzer::GetSingleton()->GetDoorDestinationName(akDoor);
    }

    RE::TESObjectREFR* GetDoorDestinationRef(RE::StaticFunctionTag*, RE::TESObjectREFR* akDoor) {
        if (!akDoor) {
            return nullptr;
        }
        return CellAnalyzer::GetSingleton()->GetDoorDestination(akDoor);
    }

    bool IsDoorExterior(RE::StaticFunctionTag*, RE::TESObjectREFR* akDoor) {
        if (!akDoor) {
            return false;
        }
        return CellAnalyzer::GetSingleton()->IsDoorExterior(akDoor);
    }

    bool IsDoorUpward(RE::StaticFunctionTag*, RE::TESObjectREFR* akDoor) {
        if (!akDoor) {
            return false;
        }
        return CellAnalyzer::GetSingleton()->IsDoorUpward(akDoor);
    }

    bool IsDoorDownward(RE::StaticFunctionTag*, RE::TESObjectREFR* akDoor) {
        if (!akDoor) {
            return false;
        }
        return CellAnalyzer::GetSingleton()->IsDoorDownward(akDoor);
    }

    // ==========================================================================
    // Item Search Functions
    // ==========================================================================

    RE::TESForm* FindItemInInventory(RE::StaticFunctionTag*, RE::Actor* akActor, RE::BSFixedString itemName) {
        if (!akActor || itemName.empty()) {
            return nullptr;
        }

        std::string searchTerm = StringUtils::ToLowerStd(itemName.c_str());
        auto inventory = akActor->GetInventory();

        RE::TESForm* bestMatch = nullptr;
        int bestNameScore = 999;
        int maxDist = Settings::GetSingleton()->fuzzyMatchThreshold;

        for (const auto& [item, data] : inventory) {
            if (!item || data.first <= 0) continue;

            const char* name = item->GetName();
            if (!name || strlen(name) == 0) continue;

            std::string lowerName = StringUtils::ToLowerStd(name);
            int nameScore = 999;

            // Exact match — return immediately
            if (lowerName == searchTerm) {
                return item;
            }
            // Contains match — prefer shorter names (more specific)
            if (lowerName.find(searchTerm) != std::string::npos) {
                nameScore = 1;
            }
            // Fuzzy match
            else {
                int dist = StringUtils::LevenshteinDistance(searchTerm, lowerName);
                if (dist <= maxDist) {
                    nameScore = 2 + dist;
                }
            }

            if (nameScore < bestNameScore) {
                bestNameScore = nameScore;
                bestMatch = item;
            }
        }

        if (bestMatch) {
            logger::debug("FindItemInInventory('{}') -> Found: {}", itemName.c_str(), bestMatch->GetName());
        }

        return bestMatch;
    }

    RE::TESObjectREFR* FindNearbyItemByName(RE::StaticFunctionTag*,
                                             RE::Actor* akActor,
                                             RE::BSFixedString itemName,
                                             float radius) {
        if (!akActor || itemName.empty()) {
            return nullptr;
        }

        auto* cell = akActor->GetParentCell();
        if (!cell) {
            return nullptr;
        }

        std::string searchTerm = StringUtils::ToLowerStd(itemName.c_str());
        RE::NiPoint3 actorPos = akActor->GetPosition();

        RE::TESObjectREFR* bestMatch = nullptr;
        float bestDistance = radius + 1.0f;
        int bestNameScore = 999;

        // Iterate through cell references
        // Note: CommonLibSSE-NG ForEachReference takes TESObjectREFR& (reference), not pointer
        cell->ForEachReference([&](RE::TESObjectREFR& ref) -> RE::BSContainer::ForEachResult {
            if (&ref == akActor) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Skip non-item references (actors, doors, etc.)
            auto* baseObj = ref.GetBaseObject();
            if (!baseObj) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Check if it's a takeable item (misc, ingredient, potion, food, etc.)
            auto formType = baseObj->GetFormType();
            if (formType != RE::FormType::Misc &&
                formType != RE::FormType::Ingredient &&
                formType != RE::FormType::AlchemyItem &&
                formType != RE::FormType::Armor &&
                formType != RE::FormType::Weapon &&
                formType != RE::FormType::Book &&
                formType != RE::FormType::Ammo &&
                formType != RE::FormType::KeyMaster &&
                formType != RE::FormType::SoulGem) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Check distance
            float dist = ref.GetPosition().GetDistance(actorPos);
            if (dist > radius) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Check name match
            const char* name = baseObj->GetName();
            if (!name || strlen(name) == 0) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            std::string lowerName = StringUtils::ToLowerStd(name);
            int nameScore = 999;

            // Exact match
            if (lowerName == searchTerm) {
                nameScore = 0;
            }
            // Contains match
            else if (lowerName.find(searchTerm) != std::string::npos) {
                nameScore = 1;
            }
            // Fuzzy match
            else {
                int levenDist = StringUtils::LevenshteinDistance(searchTerm, lowerName);
                if (levenDist <= Settings::GetSingleton()->fuzzyMatchThreshold) {
                    nameScore = 2 + levenDist;
                }
            }

            // Update best match (prefer better name match, then closer distance)
            if (nameScore < bestNameScore || (nameScore == bestNameScore && dist < bestDistance)) {
                bestMatch = &ref;  // Take address of reference
                bestDistance = dist;
                bestNameScore = nameScore;
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (bestMatch) {
            logger::debug("FindNearbyItemByName('{}', {}) -> Found: {} at distance {}",
                         itemName.c_str(), radius, bestMatch->GetBaseObject()->GetName(), bestDistance);
        }

        return bestMatch;
    }

    // ==========================================================================
    // Distance / Math Utility Functions
    // ==========================================================================

    float GetDistance3D(RE::StaticFunctionTag*, RE::TESObjectREFR* ref1, RE::TESObjectREFR* ref2) {
        if (!ref1 || !ref2) return 0.0f;
        return ref1->GetPosition().GetDistance(ref2->GetPosition());
    }

    float GetDistance2D(RE::StaticFunctionTag*, RE::TESObjectREFR* ref1, RE::TESObjectREFR* ref2) {
        if (!ref1 || !ref2) return 0.0f;
        auto p1 = ref1->GetPosition();
        auto p2 = ref2->GetPosition();
        float dx = p1.x - p2.x;
        float dy = p1.y - p2.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    float CalculateDeadlineFromDistance(RE::StaticFunctionTag*,
                                        RE::TESObjectREFR* source, RE::TESObjectREFR* target,
                                        bool isRoundTrip, float minHours, float maxHours) {
        if (!source || !target) return 0.0f;

        auto p1 = source->GetPosition();
        auto p2 = target->GetPosition();
        float dx = p1.x - p2.x;
        float dy = p1.y - p2.y;
        float straightLine = std::sqrt(dx * dx + dy * dy);

        // Interior cells use a separate coordinate space — straight-line distance
        // between an interior NPC and an exterior target is meaningless.
        // Use minHours as fallback when source/target are in different cell types.
        auto* sourceCell = source->GetParentCell();
        auto* targetCell = target->GetParentCell();
        bool crossCellType = false;
        if (sourceCell && targetCell) {
            crossCellType = sourceCell->IsInteriorCell() != targetCell->IsInteriorCell();
        } else if (sourceCell) {
            crossCellType = sourceCell->IsInteriorCell();
        }
        if (crossCellType) {
            logger::debug("CalculateDeadlineFromDistance: cross-cell-type (interior<->exterior), "
                          "using minHours={}", minHours);
            auto* cal = RE::Calendar::GetSingleton();
            return cal ? cal->GetCurrentGameTime() + (minHours / 24.0f) : 0.0f;
        }

        // Pathfinding adds ~50% over straight-line (doors, stairs, detours)
        float pathDist = straightLine * 1.5f;
        if (isRoundTrip) {
            pathDist *= 2.0f;
        }

        // Convert to game hours: 18000 units per game hour (walking speed at 20:1 timescale)
        float estimatedHours = pathDist / 18000.0f;

        // 3x safety margin — give pathfinding every chance before deadline kicks in
        estimatedHours *= 3.0f;

        // Clamp to bounds
        if (estimatedHours < minHours) estimatedHours = minHours;
        if (estimatedHours > maxHours) estimatedHours = maxHours;

        // Return as absolute game time (days)
        auto* cal = RE::Calendar::GetSingleton();
        if (!cal) return 0.0f;

        float deadline = cal->GetCurrentGameTime() + (estimatedHours / 24.0f);
        logger::debug("CalculateDeadlineFromDistance: dist={}, path={}, hours={}, roundTrip={}",
                      straightLine, pathDist, estimatedHours, isRoundTrip);
        return deadline;
    }

    std::vector<float> GetOffsetBehind(RE::StaticFunctionTag*, RE::TESObjectREFR* akRef, float distance) {
        if (!akRef) return {0.0f, 0.0f};

        float angle = akRef->GetAngleZ() + 180.0f;
        if (angle >= 360.0f) angle -= 360.0f;

        // Convert degrees to radians for C++ trig
        constexpr float DEG_TO_RAD = 3.14159265358979f / 180.0f;
        float radians = angle * DEG_TO_RAD;
        float offsetX = std::sin(radians) * distance;
        float offsetY = std::cos(radians) * distance;

        return {offsetX, offsetY};
    }

    // ==========================================================================
    // Time Parsing Functions
    // ==========================================================================

    float ParseTimeCondition(RE::StaticFunctionTag*, RE::BSFixedString condition) {
        std::string input = StringUtils::ToLowerStd(condition.c_str());
        if (input.empty()) return -1.0f;

        // Check for "tomorrow" — adds 24h offset to force next-day scheduling
        // Supports: "tomorrow", "tomorrow morning", "tomorrow night", "tomorrow at 3pm", etc.
        float dayOffset = 0.0f;
        auto tomorrowPos = input.find("tomorrow");
        if (tomorrowPos != std::string::npos) {
            dayOffset = 24.0f;
            input.erase(tomorrowPos, 8);  // "tomorrow" = 8 chars
            while (!input.empty() && input.front() == ' ') input.erase(input.begin());
            while (!input.empty() && input.back() == ' ') input.pop_back();
            if (input.empty()) {
                logger::debug("ParseTimeCondition: '{}' -> noon tomorrow (36.0)", condition.c_str());
                return 12.0f + dayOffset;  // "tomorrow" alone → noon tomorrow
            }
            logger::debug("ParseTimeCondition: stripped 'tomorrow', parsing sub-time '{}'", input);
        }

        // Helper: compute relative hour offset from current time, wrapped at 24
        // Uses GetCurrentGameTime() to be timescale-aware (compatible with DTS mods)
        auto relativeHour = [](float offset) -> float {
            auto* cal = RE::Calendar::GetSingleton();
            if (!cal) return -1.0f;

            // Derive current hour from timescale-aware GetCurrentGameTime()
            float currentGameTime = cal->GetCurrentGameTime();
            float currentHour = (currentGameTime - std::floor(currentGameTime)) * 24.0f;

            float target = currentHour + offset;
            while (target >= 24.0f) target -= 24.0f;
            return target;
        };

        // --- Relative time patterns ---
        if (input.find("soon") != std::string::npos ||
            input.find("shortly") != std::string::npos ||
            input.find("a moment") != std::string::npos ||
            input.find("right away") != std::string::npos ||
            input.find("immediately") != std::string::npos) {
            return relativeHour(0.25f) + dayOffset;
        }

        if (input.find("half an hour") != std::string::npos ||
            input.find("30 minute") != std::string::npos) {
            return relativeHour(0.5f) + dayOffset;
        }

        if (input.find("few hour") != std::string::npos ||
            input.find("couple hour") != std::string::npos ||
            input.find("couple of hour") != std::string::npos) {
            return relativeHour(2.0f) + dayOffset;
        }

        // "N hour(s)" — check from high to low to avoid "1 hour" matching inside "10 hour"
        struct HourPattern { const char* text; float hours; };
        static const HourPattern hourPatterns[] = {
            {"12 hour", 12.0f}, {"11 hour", 11.0f}, {"10 hour", 10.0f},
            {"9 hour", 9.0f}, {"8 hour", 8.0f}, {"7 hour", 7.0f},
            {"6 hour", 6.0f}, {"5 hour", 5.0f}, {"4 hour", 4.0f},
            {"3 hour", 3.0f}, {"2 hour", 2.0f},
        };
        for (const auto& hp : hourPatterns) {
            if (input.find(hp.text) != std::string::npos) {
                return relativeHour(hp.hours) + dayOffset;
            }
        }

        // "1 hour" / "an hour" / "one hour" — after multi-digit patterns
        if (input.find("1 hour") != std::string::npos ||
            input.find("an hour") != std::string::npos ||
            input.find("one hour") != std::string::npos) {
            return relativeHour(1.0f) + dayOffset;
        }

        // --- Named time patterns (substring match) ---
        // Order matters: more specific terms before substrings they contain
        // e.g., "midnight" before "night", "afternoon" before "noon"
        struct NamedTime { const char* keyword; float hour; };
        static const NamedTime namedTimes[] = {
            {"midnight", 0.0f},
            {"first light", 5.0f},
            {"dawn", 5.0f},
            {"sunrise", 6.0f},
            {"sun rise", 6.0f},
            {"breakfast", 8.0f},
            {"morning", 8.0f},
            {"midday", 12.0f},
            {"mid-day", 12.0f},
            {"afternoon", 14.0f},   // before "noon"
            {"noon", 12.0f},
            {"dinner", 18.0f},
            {"supper", 18.0f},
            {"evening", 18.0f},
            {"sundown", 19.0f},
            {"sun set", 19.0f},
            {"sunset", 19.0f},
            {"twilight", 20.0f},
            {"dusk", 20.0f},
            {"tonight", 22.0f},     // before "night"
            {"night", 22.0f},
            {"dark", 22.0f},
        };
        for (const auto& nt : namedTimes) {
            if (input.find(nt.keyword) != std::string::npos) {
                return nt.hour + dayOffset;
            }
        }

        // --- Specific hour patterns: "Xpm"/"X pm"/"Xam"/"X am" ---
        struct ClockPattern { const char* text; int hour; };
        static const ClockPattern pmPatterns[] = {
            {"12pm", 12}, {"12 pm", 12},
            {"11pm", 23}, {"11 pm", 23},
            {"10pm", 22}, {"10 pm", 22},
            {"9pm", 21}, {"9 pm", 21},
            {"8pm", 20}, {"8 pm", 20},
            {"7pm", 19}, {"7 pm", 19},
            {"6pm", 18}, {"6 pm", 18},
            {"5pm", 17}, {"5 pm", 17},
            {"4pm", 16}, {"4 pm", 16},
            {"3pm", 15}, {"3 pm", 15},
            {"2pm", 14}, {"2 pm", 14},
            {"1pm", 13}, {"1 pm", 13},
        };
        for (const auto& cp : pmPatterns) {
            if (input.find(cp.text) != std::string::npos) {
                return static_cast<float>(cp.hour) + dayOffset;
            }
        }

        static const ClockPattern amPatterns[] = {
            {"12am", 0}, {"12 am", 0},
            {"11am", 11}, {"11 am", 11},
            {"10am", 10}, {"10 am", 10},
            {"9am", 9}, {"9 am", 9},
            {"8am", 8}, {"8 am", 8},
            {"7am", 7}, {"7 am", 7},
            {"6am", 6}, {"6 am", 6},
        };
        for (const auto& cp : amPatterns) {
            if (input.find(cp.text) != std::string::npos) {
                return static_cast<float>(cp.hour) + dayOffset;
            }
        }

        // Nothing matched
        logger::debug("ParseTimeCondition: could not parse '{}'", condition.c_str());
        return -1.0f;
    }

    float CalculateTargetGameTime(RE::StaticFunctionTag*, float targetHour, float currentHour) {
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar) return 0.0f;

        // Handle day offset: targetHour >= 24 means "tomorrow at (targetHour - 24)"
        int extraDays = 0;
        while (targetHour >= 24.0f) {
            targetHour -= 24.0f;
            extraDays++;
        }

        float currentGameTime = calendar->GetCurrentGameTime();
        float dayPart = std::floor(currentGameTime);
        float targetDayFraction = targetHour / 24.0f;
        float targetTime = dayPart + targetDayFraction + static_cast<float>(extraDays);

        // Only auto-advance to tomorrow if NO explicit day offset and hour already passed
        if (extraDays == 0 && targetHour <= currentHour) {
            targetTime += 1.0f;
        }

        return targetTime;
    }

    // ==========================================================================
    // Departure Detection Functions
    // ==========================================================================

    int CheckDepartureStatus(RE::StaticFunctionTag*, RE::Actor* akActor, int slot, float threshold) {
        if (!akActor) {
            return 0;
        }
        return DepartureDetector::GetSingleton()->CheckDepartureStatus(akActor, slot, threshold);
    }

    void ResetDepartureSlot(RE::StaticFunctionTag*, int slot, RE::Actor* akActor) {
        DepartureDetector::GetSingleton()->ResetSlot(slot, akActor);
    }

    int GetDepartureRetries(RE::StaticFunctionTag*, int slot) {
        return DepartureDetector::GetSingleton()->GetRetryCount(slot);
    }

    // ==========================================================================
    // Stuck Detection Functions
    // ==========================================================================

    int CheckStuckStatus(RE::StaticFunctionTag*, RE::Actor* akActor, int slot, float threshold) {
        if (!akActor) {
            return 0;
        }
        return StuckDetector::GetSingleton()->CheckStuckStatus(akActor, slot, threshold);
    }

    void ResetStuckSlot(RE::StaticFunctionTag*, int slot, RE::Actor* akActor) {
        StuckDetector::GetSingleton()->ResetSlot(slot, akActor);
    }

    float GetTeleportDistance(RE::StaticFunctionTag*, int slot) {
        return StuckDetector::GetSingleton()->GetTeleportDistance(slot);
    }

    int GetStuckRecoveryAttempts(RE::StaticFunctionTag*, int slot) {
        return StuckDetector::GetSingleton()->GetRecoveryAttempts(slot);
    }

    // ==========================================================================
    // Off-Screen Travel Detection Functions
    // ==========================================================================

    void InitOffScreenTravel(RE::StaticFunctionTag*, int slot,
                             float estimatedArrivalGameTime, RE::Actor* actor) {
        if (!actor) return;
        auto pos = actor->GetPosition();
        OffScreenTracker::GetSingleton()->InitSlot(
            slot, estimatedArrivalGameTime, pos.x, pos.y);
    }

    int CheckOffScreenProgress(RE::StaticFunctionTag*, int slot,
                               RE::Actor* actor, float currentGameTime) {
        if (!actor) return 0;
        auto pos = actor->GetPosition();
        return OffScreenTracker::GetSingleton()->CheckProgress(
            slot, currentGameTime, pos.x, pos.y);
    }

    void ResetOffScreenSlot(RE::StaticFunctionTag*, int slot) {
        OffScreenTracker::GetSingleton()->ResetSlot(slot);
    }

    // ==========================================================================
    // Waypoint Navigation Functions
    // ==========================================================================

    RE::TESObjectREFR* FindNearestWaypointToward(RE::StaticFunctionTag*,
            RE::Actor* actor, RE::TESObjectREFR* destination, float maxRadius) {
        if (!actor || !destination) return nullptr;
        return LocationResolver::GetSingleton()->FindNearestWaypointToward(
            actor, destination, maxRadius);
    }

    // ==========================================================================
    // Home Door Access Functions (anti-trespass)
    // ==========================================================================

    RE::TESObjectREFR* SetHomeDoorAccess(RE::StaticFunctionTag*, RE::Actor* akNPC, bool unlock) {
        if (!akNPC) return nullptr;
        return LocationResolver::GetSingleton()->SetHomeDoorAccess(akNPC, unlock);
    }

    RE::TESObjectREFR* SetHomeDoorAccessForCell(RE::StaticFunctionTag*, int cellFormId, bool unlock) {
        if (cellFormId == 0) return nullptr;
        return LocationResolver::GetSingleton()->SetHomeDoorAccessForCell(
            static_cast<RE::FormID>(cellFormId), unlock);
    }

    int GetLastResolvedHomeCellId(RE::StaticFunctionTag*) {
        return static_cast<int>(LocationResolver::GetSingleton()->GetLastResolvedHomeCellId());
    }

    // ==========================================================================
    // Slot Tracker Functions (C++ state mirror for SkyrimNet decorators)
    // ==========================================================================

    void UpdateSlotState(RE::StaticFunctionTag*, int slot, RE::Actor* agent, int newState,
                         RE::BSFixedString taskType, RE::BSFixedString targetName) {
        SlotTracker::GetSingleton()->UpdateSlot(slot, agent, newState,
            taskType.c_str(), targetName.c_str());
    }

    void ClearSlotState(RE::StaticFunctionTag*, int slot) {
        SlotTracker::GetSingleton()->ClearSlot(slot);
    }

    bool IsActorAvailable(RE::StaticFunctionTag*, RE::Actor* akActor) {
        if (!akActor) return false;

        auto* tracker = SlotTracker::GetSingleton();
        bool hasTask = tracker->HasActiveTask(akActor);
        bool onCooldown = tracker->IsOnCooldown(akActor);
        bool available = !hasTask && !onCooldown;

        logger::info("IsActorAvailable({}): hasTask={}, onCooldown={}, available={}",
                    akActor->GetDisplayFullName(), hasTask, onCooldown, available);

        return available;
    }

    bool HasBaseAIPackages(RE::StaticFunctionTag*, RE::Actor* akActor) {
        if (!akActor) return false;
        auto* npc = akActor->GetActorBase();
        if (!npc) return false;

        std::uint32_t count = 0;
        for ([[maybe_unused]] auto* pkg : npc->aiPackages.packages) {
            if (pkg) count++;
        }
        bool hasPackages = count > 0;
        logger::info("HasBaseAIPackages({}): {} base AI package(s), formID={:08X}",
                    akActor->GetDisplayFullName(), count, akActor->GetFormID());
        return hasPackages;
    }

    bool HasNonSandboxAI(RE::StaticFunctionTag*, RE::Actor* akActor) {
        if (!akActor) return false;
        auto* npc = akActor->GetActorBase();
        if (!npc) return false;

        for (auto* pkg : npc->aiPackages.packages) {
            if (!pkg) continue;
            auto type = pkg->packData.packType.get();
            // Anything other than sandbox/wander/doNothing counts as "schedule" AI
            if (type != RE::PACKAGE_PROCEDURE_TYPE::kSandbox &&
                type != RE::PACKAGE_PROCEDURE_TYPE::kWander &&
                type != RE::PACKAGE_PROCEDURE_TYPE::kDoNothing) {
                logger::info("HasNonSandboxAI({}): TRUE — found package type {}",
                            akActor->GetDisplayFullName(), static_cast<int>(type));
                return true;
            }
        }
        logger::info("HasNonSandboxAI({}): FALSE — all packages are sandbox/wander",
                    akActor->GetDisplayFullName());
        return false;
    }

    RE::TESObjectREFR* GetEditorLocationRef(RE::StaticFunctionTag*, RE::Actor* akActor) {
        if (!akActor) return nullptr;

        auto* editorLoc = akActor->GetEditorLocation();
        if (!editorLoc) {
            logger::info("GetEditorLocationRef({}): no editor location",
                        akActor->GetDisplayFullName());
            return nullptr;
        }

        // Try this location's world marker
        auto markerPtr = editorLoc->worldLocMarker.get();
        if (markerPtr) {
            auto* marker = markerPtr.get();
            if (marker) {
                logger::info("GetEditorLocationRef({}): found marker at location '{}'",
                            akActor->GetDisplayFullName(),
                            editorLoc->GetFullName() ? editorLoc->GetFullName() : "unnamed");
                return marker;
            }
        }

        // Try parent location's marker (e.g., interior shop → settlement)
        if (editorLoc->parentLoc) {
            auto parentPtr = editorLoc->parentLoc->worldLocMarker.get();
            if (parentPtr) {
                auto* parentMarker = parentPtr.get();
                if (parentMarker) {
                    logger::info("GetEditorLocationRef({}): found marker at parent location '{}'",
                                akActor->GetDisplayFullName(),
                                editorLoc->parentLoc->GetFullName() ? editorLoc->parentLoc->GetFullName() : "unnamed");
                    return parentMarker;
                }
            }
        }

        logger::info("GetEditorLocationRef({}): no usable world marker found",
                    akActor->GetDisplayFullName());
        return nullptr;
    }

    // ==========================================================================
    // Story Engine Functions
    // ==========================================================================

    RE::Actor* GetRandomStoryCandidate(RE::StaticFunctionTag*) {
        auto* candidate = NPCIndex::GetSingleton()->GetRandomStoryCandidate();
        if (candidate) {
            logger::info("GetRandomStoryCandidate: Selected '{}'", candidate->GetDisplayFullName());
        } else {
            logger::debug("GetRandomStoryCandidate: No eligible candidates found");
        }
        return candidate;
    }

    RE::Actor* GetMemoryDrivenCandidate(RE::StaticFunctionTag*) {
        return NPCIndex::GetSingleton()->GetMemoryDrivenCandidate();
    }

    RE::Actor* GetRelatedCandidate(RE::StaticFunctionTag*, RE::Actor* relatedTo) {
        return NPCIndex::GetSingleton()->GetRelatedCandidate(relatedTo);
    }

    RE::BSFixedString GetActorUUID(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return "";
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%08X", actor->GetFormID());
        return RE::BSFixedString(buf);
    }

    bool IsPlayerInDangerousLocation(RE::StaticFunctionTag*) {
        return CellAnalyzer::GetSingleton()->IsPlayerInDangerousLocation();
    }

    // =========================================================================
    // Quest Interior Anchor Discovery
    // Scans exterior doors near a quest marker for dangerous interior cells.
    // Priority: prisoner furniture > door destination in dangerous interior > nullptr.
    // =========================================================================

    bool HasNearbyDungeonEntrance(RE::StaticFunctionTag*, RE::TESObjectREFR* questLocation) {
        if (!questLocation) return false;

        auto* cell = questLocation->GetParentCell();
        if (!cell || cell->IsInteriorCell()) return false;

        auto* analyzer = CellAnalyzer::GetSingleton();
        auto markerPos = questLocation->GetPosition();
        bool found = false;

        cell->ForEachReference([&](RE::TESObjectREFR& doorRef) {
            if (found) return RE::BSContainer::ForEachResult::kStop;
            if (doorRef.IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;

            auto* base = doorRef.GetBaseObject();
            if (!base || base->GetFormType() != RE::FormType::Door)
                return RE::BSContainer::ForEachResult::kContinue;

            if (markerPos.GetDistance(doorRef.GetPosition()) > 3000.0f)
                return RE::BSContainer::ForEachResult::kContinue;

            auto* teleport = doorRef.extraList.GetByType<RE::ExtraTeleport>();
            if (!teleport || !teleport->teleportData)
                return RE::BSContainer::ForEachResult::kContinue;

            auto* linkedDoor = teleport->teleportData->linkedDoor.get().get();
            if (!linkedDoor) return RE::BSContainer::ForEachResult::kContinue;

            auto* destCell = linkedDoor->GetSaveParentCell();
            if (!destCell) destCell = linkedDoor->GetParentCell();
            if (!destCell || !destCell->IsInteriorCell()) return RE::BSContainer::ForEachResult::kContinue;

            auto* destLoc = destCell->GetLocation();
            if (destLoc && analyzer->IsLocationDangerous(destLoc)) {
                found = true;
                logger::info("[IntelEngine] HasNearbyDungeonEntrance: door to dangerous interior '{}'",
                            destCell->GetName());
            }

            return found ? RE::BSContainer::ForEachResult::kStop
                         : RE::BSContainer::ForEachResult::kContinue;
        });

        if (!found) {
            logger::info("[IntelEngine] HasNearbyDungeonEntrance: no dungeon entrance near quest marker");
        }
        return found;
    }

    bool IsPlayerInOwnHome(RE::StaticFunctionTag*) {
        return CellAnalyzer::GetSingleton()->IsPlayerInOwnHome();
    }

    RE::TESObjectREFR* GetPlayerHomeExteriorDoor(RE::StaticFunctionTag*) {
        return LocationResolver::GetSingleton()->GetPlayerHomeExteriorDoorRef();
    }

    RE::TESObjectREFR* GetPlayerHomeInteriorDoor(RE::StaticFunctionTag*) {
        return LocationResolver::GetSingleton()->GetPlayerHomeInteriorDoorRef();
    }

    bool IsCivilianClass(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return true;
        return NPCIndex::ClassifyNPCArchetype(actor) == "CIVILIAN";
    }

    bool IsJarl(RE::StaticFunctionTag*, RE::Actor* actor) {
        return NPCIndex::IsJarl(actor);
    }

    void SetDangerZonePolicy(RE::StaticFunctionTag*, int policy) {
        NPCIndex::GetSingleton()->SetDangerZonePolicy(policy);
    }

    void SetPlayerHomePolicy(RE::StaticFunctionTag*, int policy) {
        NPCIndex::GetSingleton()->SetPlayerHomePolicy(policy);
    }

    void SetHoldRestrictionPolicy(RE::StaticFunctionTag*, RE::BSFixedString storyType, int policy) {
        NPCIndex::GetSingleton()->SetHoldRestrictionPolicy(storyType.c_str(), policy);
    }

    bool CheckHoldRestriction(RE::StaticFunctionTag*, RE::Actor* actor, RE::BSFixedString storyType) {
        if (!actor) return true;
        int policy = NPCIndex::GetSingleton()->GetHoldRestrictionPolicy(storyType.c_str());
        if (policy == 0) return true;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return true;
        std::string playerHold = NPCIndex::GetNPCHoldName(player);
        return NPCIndex::PassesHoldRestriction(actor, playerHold, policy);
    }

    RE::BSFixedString GetActorHoldName(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return "";
        return RE::BSFixedString(NPCIndex::GetNPCHoldName(actor).c_str());
    }

    bool NPCKnowsPlayer(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return false;
        auto* memDB = MemoryDB::GetSingleton();
        if (!memDB) return false;
        RE::FormID formId = actor->GetFormID();

        // Check dialogue history (most reliable — they've spoken to the player directly)
        auto dialogue = memDB->GetRecentDialogueForActor(formId, 1);
        if (!dialogue.empty()) return true;

        // Check memories — but only if they mention the player by name.
        // GetFormattedMemories returns ALL NPC memories (not just player-related),
        // so an NPC who only has memories about third parties would false-positive without this filter.
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            std::string playerName = player->GetDisplayFullName();
            if (!playerName.empty()) {
                auto memories = memDB->GetFormattedMemories(formId, 5);
                if (!memories.empty()) {
                    // Case-insensitive search for player name in memory text
                    std::string memLower = StringUtils::ToLowerStd(memories);
                    std::string nameLower = StringUtils::ToLowerStd(playerName);
                    if (memLower.find(nameLower) != std::string::npos) return true;
                }
            }
        }

        return false;
    }

    bool IsPotentialFollower(RE::StaticFunctionTag*, RE::Actor* actor) {
        return NPCIndex::IsPotentialFollower(actor);
    }

    bool IsPlayerInBlockedLocation(RE::StaticFunctionTag*) {
        return NPCIndex::IsPlayerInBlockedLocation();
    }

    bool IsPlayerInWhitelistedLocation(RE::StaticFunctionTag*) {
        return NPCIndex::IsPlayerInWhitelistedLocation();
    }

    // Strip non-JSON content from LLM responses.
    // Handles: markdown code fences (```json...```), reasoning text before/after JSON.
    // Modifies the string in-place. Returns true if stripping occurred.
    static bool StripToJson(std::string& s) {
        bool stripped = false;

        // Try markdown code fences first
        auto fenceStart = s.find("```");
        if (fenceStart != std::string::npos) {
            auto contentStart = s.find('\n', fenceStart);
            if (contentStart != std::string::npos) {
                contentStart++;
                auto fenceEnd = s.rfind("```");
                if (fenceEnd != std::string::npos && fenceEnd > fenceStart) {
                    s = s.substr(contentStart, fenceEnd - contentStart);
                    stripped = true;
                }
            }
        }

        // Strip any text before the first '{' (LLM reasoning preamble)
        auto jsonStart = s.find('{');
        if (jsonStart != std::string::npos && jsonStart > 0) {
            s = s.substr(jsonStart);
            stripped = true;
        }

        // Strip any text after the last '}' (trailing commentary)
        auto jsonEnd = s.rfind('}');
        if (jsonEnd != std::string::npos && jsonEnd < s.size() - 1) {
            s = s.substr(0, jsonEnd + 1);
            stripped = true;
        }

        // Trim whitespace
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
            s.pop_back();

        return stripped;
    }

    bool StoryResponseShouldAct(RE::StaticFunctionTag*, RE::BSFixedString response) {
        std::string jsonStr(response.c_str());
        StripToJson(jsonStr);
        bool result = jsonStr.find("\"should_act\":true") != std::string::npos ||
                      jsonStr.find("\"should_act\": true") != std::string::npos;
        if (!result) {
            // Log rejection reason if present
            auto reasonPos = jsonStr.find("\"reason\"");
            if (reasonPos != std::string::npos) {
                auto valStart = jsonStr.find(':', reasonPos);
                if (valStart != std::string::npos) {
                    auto qStart = jsonStr.find('"', valStart + 1);
                    auto qEnd = (qStart != std::string::npos) ? jsonStr.find('"', qStart + 1) : std::string::npos;
                    if (qStart != std::string::npos && qEnd != std::string::npos) {
                        logger::info("[StoryDM] Rejected: {}", jsonStr.substr(qStart + 1, qEnd - qStart - 1));
                    }
                }
            } else {
                logger::info("[StoryDM] Rejected (no reason given)");
            }
        }
        logger::info("[StoryDM] ShouldAct={} — input len={}", result, jsonStr.size());
        return result;
    }

    // Parse-once cache for StoryResponseGetField. Keyed by JSON string hash +
    // equality check to avoid collisions. Thread-safe via mutex.
    constexpr std::size_t kLogTruncLen = 80;
    static std::mutex s_jsonCacheMutex;
    static std::size_t s_cachedJsonHash = 0;
    static std::string s_cachedJsonStr;
    static std::unordered_map<std::string, std::string> s_cachedFields;  // lowercased key -> value

    RE::BSFixedString StoryResponseGetField(RE::StaticFunctionTag*, RE::BSFixedString json,
                                            RE::BSFixedString fieldName) {
        std::string jsonStr(json.c_str());
        std::string field(fieldName.c_str());

        if (jsonStr.empty() || field.empty()) return "";

        // Strip markdown code fences if LLM wrapped the response
        if (StripToJson(jsonStr)) {
            logger::info("[StoryDM] Stripped markdown code fence from response");
        }

        std::string fieldLower = field;
        std::transform(fieldLower.begin(), fieldLower.end(), fieldLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // Result copied out of cache under lock; BSFixedString constructed outside
        // to avoid lock-ordering with BSStringPool's internal lock.
        std::string result;
        bool found = false;
        std::string warnKeys;

        {
            std::lock_guard<std::mutex> lock(s_jsonCacheMutex);

            auto hash = std::hash<std::string>{}(jsonStr);
            if (hash != s_cachedJsonHash || jsonStr != s_cachedJsonStr) {
                // Cache miss — parse and populate
                s_cachedFields.clear();
                s_cachedJsonHash = 0;
                s_cachedJsonStr.clear();

                try {
                    auto j = nlohmann::json::parse(jsonStr);
                    if (!j.is_object()) {
                        // Cache the miss so we don't re-parse non-objects
                        s_cachedJsonHash = hash;
                        s_cachedJsonStr = jsonStr;
                        logger::warn("[StoryDM] GetField('{}') — response is not a JSON object, len={}",
                                     field, jsonStr.size());
                        return "";
                    }

                    for (auto& [key, value] : j.items()) {
                        std::string keyLower = key;
                        std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(),
                                       [](unsigned char c) { return std::tolower(c); });

                        if (value.is_string()) {
                            s_cachedFields[keyLower] = value.get<std::string>();
                        } else if (value.is_null()) {
                            s_cachedFields[keyLower] = "";
                        } else {
                            s_cachedFields[keyLower] = value.dump();
                        }
                    }

                    s_cachedJsonHash = hash;
                    s_cachedJsonStr = jsonStr;
                } catch (const std::exception& e) {
                    logger::warn("[StoryDM] GetField('{}') — JSON error: {} — input len={}, first80='{}'",
                                 field, e.what(), jsonStr.size(),
                                 jsonStr.size() > kLogTruncLen ? jsonStr.substr(0, kLogTruncLen) : jsonStr);
                    return "";
                }
            }

            // Lookup field from cache
            auto it = s_cachedFields.find(fieldLower);
            if (it != s_cachedFields.end()) {
                result = it->second;
                found = true;
            } else {
                for (auto& [k, _] : s_cachedFields) {
                    if (!warnKeys.empty()) warnKeys += ", ";
                    warnKeys += k;
                }
            }
        }
        // Lock released — safe to construct BSFixedString and log

        if (found) {
            return RE::BSFixedString(result.c_str());
        }

        logger::warn("[StoryDM] GetField('{}') NOT FOUND in parsed JSON — keys: {}",
                     field, warnKeys);
        return "";
    }

    RE::BSFixedString BuildActorContextJson(RE::StaticFunctionTag*, RE::Actor* actor,
                                            int slot) {
        if (!actor) {
            logger::warn("[IntelEngine] BuildActorContextJson: actor is null");
            return "";
        }

        // slot 0 = "actor" (single), 1 = "actor1", 2 = "actor2"
        const char* pfx;
        switch (slot) {
            case 1:  pfx = "actor1"; break;
            case 2:  pfx = "actor2"; break;
            default: pfx = "actor";  break;
        }

        std::string name = actor->GetDisplayFullName();
        logger::info("[IntelEngine] BuildActorContextJson: slot={}, actor='{}'", slot, name);

        // Race
        std::string race;
        if (auto* raceForm = actor->GetRace()) {
            const char* raceName = raceForm->GetFullName();
            if (raceName && raceName[0] != '\0') {
                race = raceName;
            }
        }

        // Gender
        auto* base = actor->GetActorBase();
        bool isMale = base ? (base->GetSex() == RE::SEX::kMale) : true;

        // Build JSON fragment — hardcoded keys, zero Papyrus string involvement
        std::string json;
        json.reserve(256);
        json += ",\"";  json += pfx;  json += "Name\":\"";    json += name;  json += "\"";
        json += ",\"";  json += pfx;  json += "Race\":\"";    json += race;  json += "\"";
        json += ",\"";  json += pfx;  json += "Gender\":\"";  json += (isMale ? "Male" : "Female");  json += "\"";

        // Numeric form ID for SkyrimNet decorators (get_relevant_memories, etc.)
        json += ",\"";  json += pfx;  json += "FormID\":";  json += std::to_string(actor->GetFormID());

        // Single-actor prompts get pronouns
        if (slot == 0) {
            if (isMale) {
                json += ",\"subj\":\"he\",\"obj\":\"him\",\"poss\":\"his\"";
            } else {
                json += ",\"subj\":\"she\",\"obj\":\"her\",\"poss\":\"her\"";
            }
        }

        // Inject NPC memories from SkyrimNet database
        auto* settings = Settings::GetSingleton();
        auto memories = MemoryDB::GetSingleton()->GetFormattedMemories(
            actor->GetFormID(), settings->maxMemoriesInContext);
        if (!memories.empty()) {
            json += ",\"";  json += pfx;  json += "Memories\":\"";
            json += MemoryDB::EscapeJsonString(memories);  json += "\"";
        }

        logger::info("[IntelEngine] BuildActorContextJson: result='{}' ({} chars)", json, json.size());
        return RE::BSFixedString(json);
    }

    RE::BSFixedString BuildDungeonMasterContext(RE::StaticFunctionTag*, int maxCandidates,
                                                float absenceDays) {
        if (maxCandidates <= 0) maxCandidates = 5;
        if (absenceDays <= 0.0f) absenceDays = 3.0f;
        auto result = NPCIndex::GetSingleton()->BuildDungeonMasterContext(maxCandidates, absenceDays);
        return RE::BSFixedString(result);
    }

    RE::BSFixedString BuildNPCInteractionContext(RE::StaticFunctionTag*, int maxPairs) {
        if (maxPairs <= 0) maxPairs = 4;
        auto result = NPCIndex::GetSingleton()->BuildNPCInteractionContext(maxPairs);
        return RE::BSFixedString(result);
    }

    RE::BSFixedString BuildNPCInteractionRequestJson(RE::StaticFunctionTag*,
                                                      RE::BSFixedString npcContext) {
        // npcContext is already JSON-escaped by BuildNPCInteractionContext — do NOT double-escape
        auto preferred = NPCIndex::GetSingleton()->GetPreferredNPCType();
        std::string json = "{";
        json += "\"npcPairPool\":\"" + std::string(npcContext.c_str()) + "\"";
        json += ",\"preferredType\":\"" + preferred + "\"";

        json += ",\"player_at_inn\":\"" + std::string(FactionPolitics::IsPlayerAtInn() ? "1" : "0") + "\"";

        // Latest witnessable event — anchors political gossip to a specific event
        std::string witnessEvent = FactionPolitics::GetSingleton()->GetLatestWitnessableEvent();
        if (!witnessEvent.empty()) {
            json += ",\"latest_witness_event\":\"" + MemoryDB::EscapeJsonString(witnessEvent) + "\"";
        } else {
            json += ",\"latest_witness_event\":\"\"";
        }

        json += "}";
        return RE::BSFixedString(json);
    }

    void NotifyStoryCooldown(RE::StaticFunctionTag*, RE::Actor* akActor, float gameTime) {
        if (!akActor) return;
        NPCIndex::GetSingleton()->NotifyStoryCooldown(akActor->GetFormID(), gameTime);
    }

    bool IsActorOnStoryCooldown(RE::StaticFunctionTag*, RE::Actor* akActor) {
        if (!akActor) return true;  // null = treat as on cooldown (reject)
        return NPCIndex::GetSingleton()->IsOnStoryCooldown(
            akActor->GetFormID(), NPCIndex::GetStoryCooldownHours());
    }

    void NotifySocialCooldown(RE::StaticFunctionTag*, RE::Actor* akActor, float gameTime, float cooldownHours) {
        if (!akActor) return;
        NPCIndex::GetSingleton()->NotifySocialCooldown(akActor->GetFormID(), gameTime, cooldownHours);
    }

    void NotifyStoryTypePicked(RE::StaticFunctionTag*, RE::BSFixedString storyType) {
        std::string type(storyType.c_str());
        if (type.empty()) return;

        // If Papyrus sends just "quest" (stale bytecode), enhance with cached subtype from DM response.
        // C++ caches the subtype when StoryResponseShouldAct parses the response.
        if (type == "quest") {
            std::lock_guard<std::mutex> lock(s_jsonCacheMutex);
            if (!s_cachedFields.empty()) {
                auto it = s_cachedFields.find("questsubtype");
                if (it != s_cachedFields.end() && !it->second.empty()) {
                    type = "quest/" + it->second;
                    logger::info("[StoryDM] Enhanced type tracking: quest -> {}", type);
                }
            }
        }
        NPCIndex::GetSingleton()->NotifyStoryTypePicked(type);
    }

    void WarmStoryTypeCountsFromCSV(RE::StaticFunctionTag*, RE::BSFixedString csv) {
        // Parse pipe-separated entries, extract type prefix before ':'
        std::string input(csv.c_str());
        auto* npcIdx = NPCIndex::GetSingleton();
        int count = 0;
        size_t pos = 0;
        while (pos < input.size()) {
            size_t pipePos = input.find('|', pos);
            if (pipePos == std::string::npos) pipePos = input.size();
            std::string entry = input.substr(pos, pipePos - pos);
            auto colonPos = entry.find(':');
            if (colonPos != std::string::npos && colonPos > 0) {
                npcIdx->NotifyStoryTypePicked(entry.substr(0, colonPos));
                count++;
            }
            pos = pipePos + 1;
        }
        if (count > 0) {
            logger::info("[StoryDM] Warmed {} type counts from CSV", count);
        }
    }

    void SetRecentGossipContext(RE::StaticFunctionTag*, RE::BSFixedString gossipLines) {
        NPCIndex::GetSingleton()->SetRecentGossipContext(gossipLines.c_str());
    }

    std::vector<int> GetDMCandidatePoolFormIDs(RE::StaticFunctionTag*) {
        auto formIds = NPCIndex::GetSingleton()->GetDMCandidatePoolFormIDs();
        std::vector<int> result;
        result.reserve(formIds.size());
        for (auto fid : formIds) {
            result.push_back(static_cast<int>(fid));
        }
        return result;
    }

    std::vector<int> GetNPCCandidatePoolFormIDs(RE::StaticFunctionTag*) {
        auto formIds = NPCIndex::GetSingleton()->GetNPCCandidatePoolFormIDs();
        std::vector<int> result;
        result.reserve(formIds.size());
        for (auto fid : formIds) {
            result.push_back(static_cast<int>(fid));
        }
        return result;
    }

    RE::BSFixedString ScanActorsWithPackages(RE::StaticFunctionTag*, std::vector<int> packageFormIDs) {
        std::vector<RE::FormID> formIds;
        formIds.reserve(packageFormIDs.size());
        for (auto fid : packageFormIDs) {
            formIds.push_back(static_cast<RE::FormID>(fid));
        }
        auto result = NPCIndex::GetSingleton()->ScanActorsWithPackages(formIds);
        return result;
    }

    // ==========================================================================
    // MemoryDB Functions (SkyrimNet SQLite reader)
    // ==========================================================================

    RE::BSFixedString GetNPCMemories(RE::StaticFunctionTag*, RE::Actor* akActor, int maxCount) {
        if (!akActor) {
            logger::warn("[IntelEngine] GetNPCMemories: actor is null");
            return "";
        }
        auto result = MemoryDB::GetSingleton()->GetFormattedMemories(akActor->GetFormID(), maxCount);
        return RE::BSFixedString(result);
    }

    RE::BSFixedString GetRecentWorldEvents(RE::StaticFunctionTag*, int maxCount, RE::BSFixedString eventTypeFilter) {
        auto result = MemoryDB::GetSingleton()->GetFormattedRecentEvents(maxCount, eventTypeFilter.c_str());
        return RE::BSFixedString(result);
    }

    RE::BSFixedString GetActiveStoryNPCs(RE::StaticFunctionTag*, int maxCount) {
        auto result = MemoryDB::GetSingleton()->GetActiveStoryNPCs(maxCount);
        return RE::BSFixedString(result);
    }

    RE::BSFixedString GetNPCRelationshipSummary(RE::StaticFunctionTag*, RE::Actor* akActor1, RE::Actor* akActor2) {
        if (!akActor1 || !akActor2) {
            logger::warn("[IntelEngine] GetNPCRelationshipSummary: actor is null");
            return "";
        }
        auto result = MemoryDB::GetSingleton()->GetRelationshipSummary(
            akActor1->GetFormID(), akActor2->GetFormID());
        return RE::BSFixedString(result);
    }

    RE::BSFixedString IsMemoryDBConnected(RE::StaticFunctionTag*) {
        return MemoryDB::GetSingleton()->IsConnected() ? "true" : "false";
    }

    int GetPlayerInteractionCount(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return 0;

        auto* memDB = MemoryDB::GetSingleton();
        if (!memDB->IsConnected()) {
            logger::warn("GetPlayerInteractionCount: MemoryDB not connected");
            return 0;
        }

        // Returns 3-tier awareness level:
        //   0 = true stranger (no record at all)
        //   1 = seen before (shared events but no direct dialogue or memories)
        //   2 = acquainted (has dialogue, memories, or direct history)
        RE::FormID formId = actor->GetFormID();

        // Tier 2: direct dialogue with the player
        auto dialogue = memDB->GetRecentDialogueForActor(formId, 1);
        if (!dialogue.empty()) {
            logger::info("GetPlayerInteractionCount: {} (0x{:X}) tier=2 (has dialogue)",
                actor->GetName(), formId);
            return 2;
        }

        // Tier 2: has memories (memories are player-facing — if they exist, NPC knows the player)
        auto memories = memDB->GetFormattedMemories(formId, 1);
        if (!memories.empty()) {
            logger::info("GetPlayerInteractionCount: {} (0x{:X}) tier=2 (has memories)",
                actor->GetName(), formId);
            return 2;
        }

        // Tier 1: shared events with the player (witnessed, nearby, narrations)
        auto related = memDB->GetRelatedCandidateFormIDs(formId, 15);
        for (const auto& rel : related) {
            if (rel.formId == 0x14) {  // player FormID
                logger::info("GetPlayerInteractionCount: {} (0x{:X}) tier=1 (shared events, no dialogue)",
                    actor->GetName(), formId);
                return 1;
            }
        }

        logger::info("GetPlayerInteractionCount: {} (0x{:X}) tier=0 (true stranger)",
            actor->GetName(), formId);
        return 0;
    }

    // ==========================================================================
    // Dialogue Safety Net Functions
    // ==========================================================================

    // Static state for tick-based safety net (transient — does not survive save/load)
    static float s_lastCheckedDialogueTime = 0.0f;
    static RE::FormID s_safetyNetNPCFormId = 0;
    static std::chrono::steady_clock::time_point s_lastApiCallTime{};
    // Consecutive unchanged results — used for adaptive backoff
    static int s_unchangedCount = 0;

    void NotifyNewDialogue(RE::StaticFunctionTag*) {
        // Reset backoff so the next tick checks immediately
        s_unchangedCount = 0;
    }

    int RunSafetyNetCheck(RE::StaticFunctionTag*) {
        // Adaptive backoff: if recent checks found nothing new, slow down.
        //  0 misses → check every 5s  (active conversation)
        //  1 miss   → check every 15s (just finished talking)
        //  2+ misses → check every 60s (idle — no dialogue in a while)
        auto now = std::chrono::steady_clock::now();
        int cooldownSec = (s_unchangedCount == 0) ? 5 : (s_unchangedCount == 1) ? 15 : 60;
        if (std::chrono::duration_cast<std::chrono::seconds>(now - s_lastApiCallTime).count() < cooldownSec) {
            return 0;
        }
        s_lastApiCallTime = now;

        auto* db = MemoryDB::GetSingleton();
        auto info = db->GetLatestDialogueInfo();

        // No dialogue found, or same conversation already checked
        if (info.npcFormId == 0 || info.gameTimeHours == s_lastCheckedDialogueTime) {
            if (s_unchangedCount < 3) s_unchangedCount++;
            return 0;
        }

        // New dialogue detected — reset backoff
        s_unchangedCount = 0;

        // Mark as checked regardless of outcome
        s_lastCheckedDialogueTime = info.gameTimeHours;

        // Get dialogue text and run keyword matching
        auto dialogue = db->GetRecentDialogueForActor(info.npcFormId, 4);
        if (dialogue.empty()) return 0;

        int keywordHint = MemoryDB::CheckScheduleKeywords(dialogue);
        if (keywordHint == 0) return 0;

        // Validate NPC is alive and reachable
        auto* form = RE::TESForm::LookupByID(info.npcFormId);
        if (!form) return 0;
        auto* actor = form->As<RE::Actor>();
        if (!actor || actor->IsDead()) return 0;

        // NOTE: Schedule slot check happens in Papyrus (Schedule.FindScheduleSlotByAgent)
        // because schedule slots are not mirrored in C++ SlotTracker.
        // Task slots (HasActiveTask) are intentionally NOT checked here — an NPC with
        // an active task can still receive a schedule that fires later.

        // All C++ gates passed — store NPC and return hint for Papyrus
        s_safetyNetNPCFormId = info.npcFormId;
        logger::info("[IntelEngine] SafetyNet: keywords={} for FormID {:X}",
            keywordHint, info.npcFormId);
        return keywordHint;
    }

    RE::Actor* GetSafetyNetNPC(RE::StaticFunctionTag*) {
        if (s_safetyNetNPCFormId == 0) return nullptr;
        auto* form = RE::TESForm::LookupByID(s_safetyNetNPCFormId);
        if (!form) return nullptr;
        return form->As<RE::Actor>();
    }

    RE::Actor* GetLastConversationPartner(RE::StaticFunctionTag*) {
        auto info = MemoryDB::GetSingleton()->GetLatestDialogueInfo();
        if (info.npcFormId == 0) return nullptr;
        auto* form = RE::TESForm::LookupByID(info.npcFormId);
        if (!form) return nullptr;
        return form->As<RE::Actor>();
    }

    RE::BSFixedString GetRecentDialogue(RE::StaticFunctionTag*, RE::Actor* npc, int maxExchanges) {
        if (!npc) {
            logger::warn("[IntelEngine] GetRecentDialogue: actor is null");
            return "";
        }
        if (maxExchanges < 1) maxExchanges = 4;

        auto result = MemoryDB::GetSingleton()->GetRecentDialogueForActor(
            npc->GetFormID(), maxExchanges);
        // JSON-escape for safe embedding in Papyrus contextJson strings
        return RE::BSFixedString(MemoryDB::EscapeJsonString(result));
    }

    int HasScheduleKeywords(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) return 0;

        auto* db = MemoryDB::GetSingleton();
        auto dialogue = db->GetRecentDialogueForActor(npc->GetFormID(), 4);
        if (dialogue.empty()) return 0;

        return MemoryDB::CheckScheduleKeywords(dialogue);
    }

    RE::BSFixedString BuildSafetyNetContextJson(RE::StaticFunctionTag*,
                                                 RE::Actor* npc, int keywordHint) {
        if (!npc) return "";

        auto* db = MemoryDB::GetSingleton();
        auto dialogue = db->GetRecentDialogueForActor(npc->GetFormID(), 4);
        if (dialogue.empty()) return "";

        // Get location name (same logic as GetActorParentLocationName)
        std::string locName;
        if (auto* loc = npc->GetCurrentLocation()) {
            auto* parent = loc->parentLoc;
            if (parent && parent->GetFullName() && strlen(parent->GetFullName()) > 0) {
                locName = parent->GetFullName();
            } else if (loc->GetFullName()) {
                locName = loc->GetFullName();
            }
        }

        const char* keywordType = "meeting";
        if (keywordHint == 2) keywordType = "fetch";
        else if (keywordHint == 3) keywordType = "delivery";

        auto* player = RE::PlayerCharacter::GetSingleton();

        // Build JSON with proper escaping on all string values
        std::string json = "{";
        json += "\"npcName\":\"" + MemoryDB::EscapeJsonString(npc->GetDisplayFullName()) + "\",";
        json += "\"playerName\":\"" + MemoryDB::EscapeJsonString(player ? player->GetDisplayFullName() : "Player") + "\",";
        json += "\"location\":\"" + MemoryDB::EscapeJsonString(locName) + "\",";
        json += "\"dialogue\":\"" + MemoryDB::EscapeJsonString(dialogue) + "\",";
        json += "\"keywordHint\":\"" + std::string(keywordType) + "\"}";

        return RE::BSFixedString(json);
    }

    RE::BSFixedString BuildStoryDMRequestJson(RE::StaticFunctionTag*,
                                               RE::BSFixedString dmContext,
                                               RE::BSFixedString excludedTypes) {
        // dmContext is already JSON-escaped by BuildDungeonMasterContext — do NOT double-escape

        // Parse comma-separated excluded types into a set for per-type show flags.
        // Prompt template uses {% if show_X == "1" %} to conditionally render each type.
        std::unordered_set<std::string> excludedSet;
        {
            std::string excl = excludedTypes.c_str();
            size_t pos = 0;
            while (pos < excl.size()) {
                size_t comma = excl.find(',', pos);
                if (comma == std::string::npos) comma = excl.size();
                std::string token = excl.substr(pos, comma - pos);
                // Trim whitespace
                size_t start = token.find_first_not_of(" \t");
                size_t end = token.find_last_not_of(" \t");
                if (start != std::string::npos) {
                    std::string trimmed = token.substr(start, end - start + 1);
                    // Lowercase: Papyrus VM may capitalize string literals
                    // (e.g., "ambush" → "Ambush") so we normalize to match allTypes[].
                    std::transform(trimmed.begin(), trimmed.end(), trimmed.begin(), ::tolower);
                    excludedSet.insert(trimmed);
                }
                pos = comma + 1;
            }
        }

        static const char* allTypes[] = {
            "seek_player", "informant", "road_encounter",
            "ambush", "stalker", "message", "quest",
            "quest_combat", "quest_rescue", "quest_find_item",
            "faction_ambush",
            "quest_faction_combat", "quest_faction_rescue", "quest_faction_battle"
        };

        // Log exclude set for diagnostics
        {
            std::string excludeLog;
            for (const auto& e : excludedSet) {
                if (!excludeLog.empty()) excludeLog += ", ";
                excludeLog += e;
            }
            logger::info("[StoryDM] Exclude set: [{}]", excludeLog);
        }

        std::string json = "{";
        json += "\"candidatePool\":\"" + std::string(dmContext.c_str()) + "\",";

        // Per-type show flags: "1" if allowed, "0" if excluded.
        // MUST always include ALL keys with non-empty values. Inja's variable
        // resolution falls back to no-argument callbacks/decorators when a key
        // is missing or empty in the data. SkyrimNet registers decorators that
        // can shadow template variables (e.g., "show_ambush", "show_message"),
        // causing Inja to call the decorator and return a truthy value instead
        // of the intended empty/missing value. Using "0" (always present,
        // never matches "1") prevents the callback fallback entirely.
        for (const auto* t : allTypes) {
            json += "\"show_" + std::string(t) + "\":\"" +
                    (excludedSet.count(t) ? "0" : "1") + "\",";
        }

        json += "\"player_at_inn\":\"" + std::string(FactionPolitics::IsPlayerAtInn() ? "1" : "0") + "\",";

        // Latest witnessable political event (assassination, brawl, sabotage, etc.)
        std::string witnessEvent = FactionPolitics::GetSingleton()->GetLatestWitnessableEvent();
        if (!witnessEvent.empty()) {
            std::string escaped = MemoryDB::EscapeJsonString(witnessEvent);
            json += "\"latest_witness_event\":\"" + escaped + "\",";
        } else {
            json += "\"latest_witness_event\":\"\",";
        }

        // Hostile factions — factions hostile to the player OR enemies of the player's friendly factions.
        // Used by faction_ambush (player-hostile only) and faction_combat/rescue (ally's enemies too).
        {
            auto* db = PoliticalDB::GetSingleton();
            auto* fp = FactionPolitics::GetSingleton();
            if (db && db->IsReady() && fp) {
                auto standings = db->GetAllPlayerStandings();

                // Build friendly faction list first (needed for hostile derivation)
                std::vector<std::string> friendlyIds;
                std::string friendlyList;
                for (const auto& ps : standings) {
                    if (ps.standing >= 40) {
                        friendlyIds.push_back(ps.factionId);
                        auto factionOpt = fp->GetFaction(ps.factionId);
                        std::string displayName = factionOpt ? factionOpt->name : ps.factionId;
                        if (!friendlyList.empty()) friendlyList += ", ";
                        friendlyList += displayName + " [" + ps.factionId + "] (" + std::to_string(ps.standing) + ")";
                    }
                }

                // Hostile = player standing <= -40 OR enemy of any friendly faction (Tense or worse relation)
                std::set<std::string> hostileSet;
                for (const auto& ps : standings) {
                    if (ps.standing <= -40) {
                        hostileSet.insert(ps.factionId);
                    }
                }
                // Also include rivals/enemies of friendly factions
                auto allRelations = db->GetAllRelations();
                for (const auto& fid : friendlyIds) {
                    for (const auto& r : allRelations) {
                        if (r.relationScore >= 0) continue;  // only hostile relations
                        if (r.factionA == fid && hostileSet.find(r.factionB) == hostileSet.end()) {
                            hostileSet.insert(r.factionB);
                        } else if (r.factionB == fid && hostileSet.find(r.factionA) == hostileSet.end()) {
                            hostileSet.insert(r.factionA);
                        }
                    }
                }

                std::string hostileList;
                for (const auto& hid : hostileSet) {
                    auto factionOpt = fp->GetFaction(hid);
                    std::string displayName = factionOpt ? factionOpt->name : hid;
                    int playerStanding = 0;
                    for (const auto& ps : standings) {
                        if (ps.factionId == hid) { playerStanding = ps.standing; break; }
                    }
                    if (!hostileList.empty()) hostileList += ", ";
                    hostileList += displayName + " [" + hid + "] (" + std::to_string(playerStanding) + ")";
                }

                json += "\"hostile_factions\":\"" + MemoryDB::EscapeJsonString(hostileList) + "\",";
                json += "\"friendly_factions\":\"" + MemoryDB::EscapeJsonString(friendlyList) + "\",";
            } else {
                json += "\"hostile_factions\":\"\",";
                json += "\"friendly_factions\":\"\",";
            }
        }

        // World quiet flag — true if last dispatch was > 3 game days ago (or never)
        {
            auto* npcIdx = NPCIndex::GetSingleton();
            float lastDispatch = npcIdx->GetLastDispatchGameTime();
            auto* cal = RE::Calendar::GetSingleton();
            float now = cal ? cal->GetCurrentGameTime() : 0.f;
            bool worldQuiet = (lastDispatch <= 0.f) || ((now - lastDispatch) > 3.f);
            json += "\"world_quiet\":\"" + std::string(worldQuiet ? "1" : "0") + "\",";
        }

        // Remove trailing comma, close
        if (json.back() == ',') json.pop_back();
        json += "}";

        return RE::BSFixedString(json);
    }

    // ==========================================================================
    // Bio Section Pre-Rendering Functions
    // ==========================================================================
    // papyrus_util("GetStringList") can't see StorageUtil lists newly created
    // during the current session (only co-save data). These functions pre-render
    // list-based bio sections as single strings, stored via SetStringValue which
    // IS visible to papyrus_util("GetStringValue") immediately.
    // Only first-person rendering — templates fall back to list-based for 3P.

    static std::string FormatRelativeTimeFromDays(float currentGameDays, float eventGameDays) {
        float hoursPassed = (currentGameDays - eventGameDays) * 24.0f;
        if (hoursPassed < 0.1f) return "just now";
        if (hoursPassed < 1.0f) return "a few minutes ago";
        if (hoursPassed < 3.0f) return "a short while ago";
        if (hoursPassed < 12.0f) return "earlier today";
        if (hoursPassed < 24.0f) return "yesterday";
        if (hoursPassed < 48.0f) return "a day ago";
        if (hoursPassed < 72.0f) return "a couple of days ago";
        if (hoursPassed < 120.0f) return "a few days ago";
        if (hoursPassed < 168.0f) return "several days ago";
        return "some time ago";
    }

    RE::BSFixedString RenderFactsSection(RE::StaticFunctionTag*,
                                          std::vector<RE::BSFixedString> facts,
                                          std::vector<float> factTimes,
                                          float currentGameDays) {
        if (facts.empty()) return RE::BSFixedString("");

        std::string result = "## Things I Know\n\n";

        // Chronological order (oldest first, newest last) — LLMs weight
        // end-of-prompt content more, so recent facts get higher attention.
        for (int i = 0; i < static_cast<int>(facts.size()); ++i) {
            auto idx = static_cast<size_t>(i);
            std::string timeLabel = "some time ago";
            if (idx < factTimes.size()) {
                timeLabel = FormatRelativeTimeFromDays(currentGameDays, factTimes[idx]);
            }
            result += "- ";
            result += facts[idx].c_str();
            result += " (";
            result += timeLabel;
            result += ")\n";
        }

        return RE::BSFixedString(result);
    }

    RE::BSFixedString RenderGossipHeardSection(RE::StaticFunctionTag*,
                                                std::vector<RE::BSFixedString> rumors,
                                                std::vector<RE::BSFixedString> sources,
                                                std::vector<float> times,
                                                float currentGameDays) {
        if (rumors.empty()) return RE::BSFixedString("");

        std::string result = "## Rumors I've Heard\n\n";

        for (int i = static_cast<int>(rumors.size()) - 1; i >= 0; --i) {
            auto idx = static_cast<size_t>(i);
            std::string source = (idx < sources.size()) ? sources[idx].c_str() : "someone";
            std::string timeLabel = "some time ago";
            if (idx < times.size()) {
                timeLabel = FormatRelativeTimeFromDays(currentGameDays, times[idx]);
            }
            result += "- ";
            result += source;
            result += " told me: ";
            result += rumors[idx].c_str();
            result += " (";
            result += timeLabel;
            result += ")\n";
        }

        return RE::BSFixedString(result);
    }

    RE::BSFixedString RenderGossipToldSection(RE::StaticFunctionTag*,
                                               std::vector<RE::BSFixedString> rumors,
                                               std::vector<RE::BSFixedString> recipients,
                                               std::vector<float> times,
                                               float currentGameDays) {
        if (rumors.empty()) return RE::BSFixedString("");

        std::string result = "## Rumors I've Shared\n\n";

        for (int i = static_cast<int>(rumors.size()) - 1; i >= 0; --i) {
            auto idx = static_cast<size_t>(i);
            std::string recipient = (idx < recipients.size()) ? recipients[idx].c_str() : "someone";
            std::string timeLabel = "some time ago";
            if (idx < times.size()) {
                timeLabel = FormatRelativeTimeFromDays(currentGameDays, times[idx]);
            }
            result += "- I told ";
            result += recipient;
            result += ": ";
            result += rumors[idx].c_str();
            result += " (";
            result += timeLabel;
            result += ")\n";
        }

        return RE::BSFixedString(result);
    }

    RE::BSFixedString RenderTaskHistorySection(RE::StaticFunctionTag*,
                                                std::vector<RE::BSFixedString> descs,
                                                std::vector<float> descTimes,
                                                float currentGameDays) {
        if (descs.empty()) return RE::BSFixedString("");

        std::string result = "### Past Tasks\nWhat I've done:\n";

        // Chronological order (oldest first, newest last) — LLMs weight
        // end-of-prompt content more, so recent tasks get higher attention.
        for (int i = 0; i < static_cast<int>(descs.size()); ++i) {
            auto idx = static_cast<size_t>(i);
            std::string timeLabel = "some time ago";
            if (idx < descTimes.size()) {
                timeLabel = FormatRelativeTimeFromDays(currentGameDays, descTimes[idx]);
            }
            result += "- ";
            result += descs[idx].c_str();
            result += " (";
            result += timeLabel;
            result += ")\n";
        }

        return RE::BSFixedString(result);
    }

    // ==========================================================================
    // Dashboard Config Functions
    // ==========================================================================

    void NotifyDashboardSlotChanged(RE::StaticFunctionTag*) {
        DashboardUIManager::GetSingleton()->PushSlotData();
    }

    int GetDashboardHotkey(RE::StaticFunctionTag*) {
        // SkyUI MCM expects DirectInput scancodes; we store VK codes internally
        int vk = DashboardConfig::GetSingleton()->GetHotkey();
        if (vk <= 0) return vk;
        return static_cast<int>(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC));
    }

    bool SetDashboardHotkey(RE::StaticFunctionTag*, int dxScancode) {
        // SkyUI MCM gives DirectInput scancodes; convert to VK for storage
        if (dxScancode == -1) {
            return DashboardConfig::GetSingleton()->SetHotkey(-1);
        }
        if (dxScancode < 0 || dxScancode > 255) {
            logger::warn("SetDashboardHotkey: Invalid scancode {}", dxScancode);
            return false;
        }
        int vk = static_cast<int>(MapVirtualKeyA(dxScancode, MAPVK_VSC_TO_VK));
        return DashboardConfig::GetSingleton()->SetHotkey(vk);
    }

    void ReloadDashboardUI(RE::StaticFunctionTag*) {
        DashboardUIManager::GetSingleton()->ReloadView();
    }

    void ReloadDashboardConfig(RE::StaticFunctionTag*) {
        DashboardConfig::GetSingleton()->Reload();
        logger::info("[Dashboard] Config reloaded from settings.yaml");
    }

    void PushDashboardFullState(RE::StaticFunctionTag*, RE::BSFixedString json) {
        DashboardUIManager::GetSingleton()->PushFullState(json.c_str());
    }

    bool IsDashboardOpen(RE::StaticFunctionTag*) {
        return DashboardUIManager::GetSingleton()->IsOpen();
    }

    RE::BSFixedString GetPendingDirectorParam(RE::StaticFunctionTag*, RE::BSFixedString key) {
        return DashboardUIManager::GetPendingParam(key.c_str());
    }

    void ClearPendingDirectorParams(RE::StaticFunctionTag*) {
        DashboardUIManager::ClearPendingParams();
    }

    RE::BSFixedString ClaimPendingDirectorParams(RE::StaticFunctionTag*) {
        return DashboardUIManager::ClaimPendingParams();
    }

    // ==========================================================================
    // Debug Functions
    // ==========================================================================

    void TestNPCSearch(RE::StaticFunctionTag*, RE::BSFixedString searchTerm) {
        auto* npc = NPCIndex::GetSingleton()->FindByName(searchTerm.c_str());
        if (npc) {
            logger::info("TestNPCSearch('{}') -> Found: {}", searchTerm.c_str(), npc->GetDisplayFullName());
        } else {
            auto suggestion = NPCIndex::GetSingleton()->GetSuggestion(searchTerm.c_str());
            logger::info("TestNPCSearch('{}') -> Not found. Suggestion: {}", searchTerm.c_str(), suggestion.c_str());
        }
    }

    void TestLocationResolve(RE::StaticFunctionTag*, RE::BSFixedString locationName) {
        auto* ref = LocationResolver::GetSingleton()->Resolve(locationName.c_str());
        if (ref) {
            logger::info("TestLocationResolve('{}') -> Found marker at ({}, {}, {})",
                         locationName.c_str(),
                         ref->GetPositionX(),
                         ref->GetPositionY(),
                         ref->GetPositionZ());
        } else {
            logger::info("TestLocationResolve('{}') -> Not found", locationName.c_str());
        }
    }

    void TestSemanticResolve(RE::StaticFunctionTag*, RE::Actor* akNPC, RE::BSFixedString term) {
        if (!akNPC) {
            logger::info("TestSemanticResolve: No actor provided");
            return;
        }

        auto* ref = LocationResolver::GetSingleton()->ResolveSemantic(akNPC, term.c_str());
        if (ref) {
            auto destName = CellAnalyzer::GetSingleton()->GetDoorDestinationName(ref);
            logger::info("TestSemanticResolve('{}', '{}') -> Found: {}",
                         akNPC->GetDisplayFullName(),
                         term.c_str(),
                         destName.c_str());
        } else {
            logger::info("TestSemanticResolve('{}', '{}') -> Cannot resolve",
                         akNPC->GetDisplayFullName(),
                         term.c_str());
        }
    }

    void TestValidation(RE::StaticFunctionTag*, RE::BSFixedString actionType, RE::BSFixedString target) {
        auto valid = ActionValidator::GetSingleton()->Validate(nullptr, actionType.c_str(), target.c_str());
        auto reason = ActionValidator::GetSingleton()->GetFailureReason(nullptr, actionType.c_str(), target.c_str());
        logger::info("TestValidation('{}', '{}') -> {} ({})",
                     actionType.c_str(),
                     target.c_str(),
                     valid ? "valid" : "invalid",
                     reason.c_str());
    }

    void SetDebugLevel(RE::StaticFunctionTag*, int level) {
        Settings::ApplyDebugLevel(level);
        logger::info("Debug level set to {}", level);
    }

    RE::BSFixedString GetVersion(RE::StaticFunctionTag*) {
        return INTELENGINE_VERSION;
    }

    // ==========================================================================
    // Quest Enemy Spawning (replaces 5 CK ActorBase properties)
    // Returns formID array so Papyrus can persist via StorageUtil (save-safe).
    // ==========================================================================

    static thread_local std::mt19937 s_rng{std::random_device{}()};

    // Exact-match cache (EditorID → single form)
    static std::unordered_map<std::string, RE::TESBoundObject*> s_leveledActorCache;
    // Prefix-match cache (EditorID → list of matching forms for random selection)
    static std::unordered_map<std::string, std::vector<RE::TESBoundObject*>> s_prefixMatchCache;
    // Guards both caches — lookups can occur from multiple threads
    static std::mutex s_lookupCacheMutex;

    static bool IsPrefixSkippable(const char* eid) {
        // Skip Requiem-nullified forms (REQ_NULL_) and ERDP mod-specific variants
        return (_strnicmp(eid, "REQ_NULL_", 9) == 0 ||
                _strnicmp(eid, "ERDP", 4) == 0 ||
                _strnicmp(eid, "manny_GF_", 9) == 0 ||
                _strnicmp(eid, "QVK", 3) == 0 ||
                _strnicmp(eid, "SocDLC", 6) == 0);
    }

    // FormID fallback for vanilla Skyrim.esm forms whose EditorIDs aren't
    // queryable at runtime (GetFormEditorID() returns empty for base-game forms).
    static const std::unordered_map<std::string, RE::FormID> s_vanillaFormIDs = {
        {"LvlBanditMeleeAny",      0x01E79C},
        {"LvlBanditMelee2H",       0x01E79D},
        {"LvlBanditMissile",       0x01E79E},
        {"LvlBanditBoss",          0x03DF17},
        {"LvlBanditMelee1H",       0x03DECB},
        {"LvlBanditMeleeTank",     0x015BE5},
        {"LvlDraugrMeleeAllMale",  0x055954},
        {"LvlDraugrMelee1HMale",   0x055953},
        {"LvlDraugrMissileMale",   0x0A6851},
        {"LvlDraugrWarlockMale",   0x01E7AC},
        {"EncDragon01Fire",        0x01CA03},
        {"EncDragon01Frost",       0x0F80FA},
        // Civil War / faction soldiers (for battle system)
        {"LCharSoldierImperial",   0x01FC5B},
        {"LCharSoldierSons",       0x01FC5C},
        {"LCharThalmorMelee1H",    0x02B129},
        {"LCharThalmorMagic",      0x02B128},
        {"LCharThalmorMissile",    0x02B12A},
        // Guild / political faction members
        {"LCharBanditMeleeAny",    0x03DECD},
        {"WEThiefSubChar",         0x104DC9},
        {"LCharBanditWizard",      0x01E771},
        {"WEAssassinSubChar",      0x1051F4},
        {"LCharForswornMelee1H",   0x01E792},
    };

    static RE::TESBoundObject* LookupLeveledActor(const char* editorID) {
        std::lock_guard<std::mutex> lock(s_lookupCacheMutex);
        std::string key(editorID);

        // Check exact cache first (includes negative results)
        auto it = s_leveledActorCache.find(key);
        if (it != s_leveledActorCache.end()) {
            return it->second;
        }

        // Check prefix cache (picks random match each call)
        auto pit = s_prefixMatchCache.find(key);
        if (pit != s_prefixMatchCache.end()) {
            if (pit->second.empty()) return nullptr;
            return pit->second[s_rng() % pit->second.size()];
        }

        // Fast path: engine EditorID map
        auto* form = RE::TESForm::LookupByEditorID(editorID);
        if (form) {
            auto ft = form->GetFormType();
            if (ft == RE::FormType::LeveledNPC || ft == RE::FormType::NPC) {
                auto* result = static_cast<RE::TESBoundObject*>(form);
                s_leveledActorCache[key] = result;
                return result;
            }
            logger::warn("[IntelEngine] LookupLeveledActor: '{}' is FormType {} (expected LeveledNPC/NPC)",
                        editorID, static_cast<int>(ft));
        }

        // Enumeration fallback: exact match by GetFormEditorID()
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (dh) {
            for (auto* f : dh->GetFormArray<RE::TESLevCharacter>()) {
                if (f) {
                    auto eid = f->GetFormEditorID();
                    if (eid && eid[0] != '\0' && _stricmp(eid, editorID) == 0) {
                        auto* result = static_cast<RE::TESBoundObject*>(f);
                        s_leveledActorCache[key] = result;
                        logger::info("[IntelEngine] LookupLeveledActor: found '{}' via enumeration (FormID {:08X})",
                                    editorID, f->GetFormID());
                        return result;
                    }
                }
            }
            for (auto* f : dh->GetFormArray<RE::TESNPC>()) {
                if (f) {
                    auto eid = f->GetFormEditorID();
                    if (eid && eid[0] != '\0' && _stricmp(eid, editorID) == 0) {
                        auto* result = static_cast<RE::TESBoundObject*>(f);
                        s_leveledActorCache[key] = result;
                        logger::info("[IntelEngine] LookupLeveledActor: found '{}' via NPC enumeration (FormID {:08X})",
                                    editorID, f->GetFormID());
                        return result;
                    }
                }
            }

            // Prefix fallback: Lorerim/Requiem replaces leveled characters with individual
            // NPC variants (e.g., "LvlBanditMelee" → "LvlBanditMelee1HCold", "LvlBanditMelee2HCold").
            // Collect all prefix matches, skip mod-specific/nullified forms, pick randomly for variety.
            // Scans both LeveledNpc and NPC form arrays for maximum compatibility across modlists.
            size_t prefixLen = strlen(editorID);
            std::vector<RE::TESBoundObject*> matches;
            for (auto* f : dh->GetFormArray<RE::TESLevCharacter>()) {
                if (f) {
                    auto eid = f->GetFormEditorID();
                    if (eid && eid[0] != '\0' && _strnicmp(eid, editorID, prefixLen) == 0) {
                        if (!IsPrefixSkippable(eid)) {
                            matches.push_back(static_cast<RE::TESBoundObject*>(f));
                        }
                    }
                }
            }
            for (auto* f : dh->GetFormArray<RE::TESNPC>()) {
                if (f) {
                    auto eid = f->GetFormEditorID();
                    if (eid && eid[0] != '\0' && _strnicmp(eid, editorID, prefixLen) == 0) {
                        if (!IsPrefixSkippable(eid)) {
                            matches.push_back(static_cast<RE::TESBoundObject*>(f));
                        }
                    }
                }
            }
            if (!matches.empty()) {
                logger::info("[IntelEngine] LookupLeveledActor: '{}' not found exactly, using {} prefix matches",
                            editorID, matches.size());
                for (auto* m : matches) {
                    logger::info("  - {} (FormID {:08X})", m->GetName(), m->GetFormID());
                }
                s_prefixMatchCache[key] = matches;
                return matches[s_rng() % matches.size()];
            }
        }

        // FormID fallback: vanilla Skyrim.esm forms whose EditorIDs aren't
        // available at runtime (SSE strips them to save memory).
        auto fid = s_vanillaFormIDs.find(key);
        if (fid != s_vanillaFormIDs.end() && dh) {
            auto* fallback = dh->LookupForm(fid->second, "Skyrim.esm");
            if (fallback) {
                auto ft = fallback->GetFormType();
                if (ft == RE::FormType::LeveledNPC || ft == RE::FormType::NPC) {
                    auto* result = static_cast<RE::TESBoundObject*>(fallback);
                    s_leveledActorCache[key] = result;
                    logger::info("[IntelEngine] LookupLeveledActor: '{}' found via FormID fallback (0x{:06X})",
                                editorID, fid->second);
                    return result;
                }
            }
        }

        // Complete miss — cache negative result in exact cache
        logger::warn("[IntelEngine] LookupLeveledActor: '{}' not found by any method", editorID);
        s_leveledActorCache[key] = nullptr;
        return nullptr;
    }

    std::vector<RE::Actor*> SpawnQuestEnemies(RE::StaticFunctionTag*, RE::TESObjectREFR* location,
                                        RE::BSFixedString enemyType) {
        std::vector<RE::Actor*> result;
        if (!location) {
            logger::error("[IntelEngine] SpawnQuestEnemies: location is null");
            return result;
        }

        std::string type = StringUtils::ToLowerStd(enemyType.c_str());

        const char* primaryID = nullptr;
        const char* secondaryID = nullptr;
        int minCount = 1;
        int maxCount = 1;

        // Faction soldier spawning: enemyType format "faction:FactionId" (e.g., "faction:ThalmorFaction")
        bool isFactionSpawn = (type.substr(0, 8) == "faction:");
        if (isFactionSpawn) {
            std::string factionId = std::string(enemyType.c_str()).substr(8);  // Use original casing
            auto templateId = FactionPolitics::GetSingleton()->GetSoldierTemplate(factionId);
            RE::TESBoundObject* base = nullptr;
            if (!templateId.empty()) {
                base = LookupLeveledActor(templateId.c_str());
            }
            if (!base) {
                logger::error("[IntelEngine] SpawnQuestEnemies: faction template not found for '{}'", factionId);
                base = LookupLeveledActor("LvlBanditMeleeAny");
            }
            if (!base) return result;

            // Resolve leveled list to concrete NPCs (same as SpawnBattleSoldiers)
            std::vector<RE::TESBoundObject*> resolvedBases;
            std::uniform_int_distribution<int> countDist(4, 6);
            int count = countDist(s_rng);

            if (base->GetFormType() == RE::FormType::LeveledNPC) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto playerLevel = player ? player->GetLevel() : static_cast<std::uint16_t>(25);
                RE::BSScrapArray<RE::CALCED_OBJECT> calced;
                static_cast<RE::TESLevCharacter*>(base)->CalculateCurrentFormList(playerLevel, count, calced, 0, false);
                for (auto& c : calced) {
                    auto* form = c.form;
                    for (int depth = 0; depth < 10 && form && form->GetFormType() == RE::FormType::LeveledNPC; ++depth) {
                        RE::BSScrapArray<RE::CALCED_OBJECT> sub;
                        static_cast<RE::TESLevCharacter*>(form)->CalculateCurrentFormList(playerLevel, 1, sub, 0, false);
                        if (!sub.empty() && sub[0].form) form = sub[0].form;
                        else break;
                    }
                    if (form && form->GetFormType() != RE::FormType::LeveledNPC) {
                        resolvedBases.push_back(static_cast<RE::TESBoundObject*>(form));
                    }
                }
            } else {
                for (int i = 0; i < count; ++i) resolvedBases.push_back(base);
            }

            for (auto* resolvedBase : resolvedBases) {
                auto spawned = location->PlaceObjectAtMe(resolvedBase, true);
                if (spawned) {
                    auto* actor = spawned->As<RE::Actor>();
                    if (actor) result.push_back(actor);
                }
            }
            logger::info("[IntelEngine] SpawnQuestEnemies: {} faction '{}' soldiers spawned at {}",
                        result.size(), factionId, location->GetName());
            return result;
        }

        if (type == "bandit") {
            primaryID = "LvlBanditMeleeAny";
            secondaryID = "LvlBanditMissile";
            minCount = 3;
            maxCount = 5;
        } else if (type == "draugr") {
            primaryID = "LvlDraugrMeleeAllMale";
            secondaryID = "LvlDraugrMissileMale";
            minCount = 2;
            maxCount = 4;
        } else if (type == "dragon") {
            primaryID = "EncDragon01Fire";
            minCount = 1;
            maxCount = 1;
        } else {
            logger::error("[IntelEngine] SpawnQuestEnemies: unknown enemy type '{}'", type);
            return result;
        }

        auto* primaryBase = LookupLeveledActor(primaryID);
        if (!primaryBase) {
            logger::error("[IntelEngine] SpawnQuestEnemies: EditorID '{}' not found", primaryID);
            return result;
        }

        RE::TESBoundObject* secondaryBase = nullptr;
        if (secondaryID) {
            secondaryBase = LookupLeveledActor(secondaryID);
            if (!secondaryBase) {
                logger::warn("[IntelEngine] SpawnQuestEnemies: secondary '{}' not found, using primary only", secondaryID);
            }
        }

        std::uniform_int_distribution<int> countDist(minCount, maxCount);
        int count = countDist(s_rng);

        for (int i = 0; i < count; ++i) {
            auto* baseToSpawn = (secondaryBase && i % 2 == 1) ? secondaryBase : primaryBase;

            // PlaceObjectAtMe spawns at the anchor's navmesh position.
            // Don't offset with SetPosition — raw coordinate offsets ignore
            // navmesh/collision and land inside walls in tight interiors.
            // Let AI combat behavior handle natural spreading.
            auto spawned = location->PlaceObjectAtMe(baseToSpawn, true);
            if (spawned) {
                auto* actor = spawned->As<RE::Actor>();
                if (actor) {
                    result.push_back(actor);
                }
                auto pos = spawned->GetPosition();
                logger::info("[IntelEngine] SpawnQuestEnemies: spawned '{}' at ({:.0f}, {:.0f})",
                            baseToSpawn->GetName(), pos.x, pos.y);
            }
        }

        logger::info("[IntelEngine] SpawnQuestEnemies: {} {} spawned at {}",
                    result.size(), type, location->GetName());
        return result;
    }

    // =========================================================================
    // Quest Chest Spawning (find_item sub-type)
    // =========================================================================

    RE::TESObjectREFR* SpawnQuestChest(RE::StaticFunctionTag*, RE::TESObjectREFR* location,
                                        RE::BSFixedString itemName) {
        if (!location) {
            logger::error("[IntelEngine] SpawnQuestChest: location is null");
            return nullptr;
        }

        std::string nameStr(itemName.c_str());
        if (nameStr.empty()) {
            logger::error("[IntelEngine] SpawnQuestChest: itemName is empty");
            return nullptr;
        }

        // Resolve item via ItemIndex
        auto* itemObj = ItemIndex::GetSingleton()->FindByName(nameStr);
        if (!itemObj) {
            logger::error("[IntelEngine] SpawnQuestChest: item '{}' not found in ItemIndex", nameStr);
            return nullptr;
        }

        // Look up a vanilla chest base form to use as the container
        // TreasBossBanditChest is a good choice — large, unlocked, visible
        auto* dh = RE::TESDataHandler::GetSingleton();
        RE::TESBoundObject* chestBase = nullptr;
        if (dh) {
            // Try EditorID lookup first (most reliable)
            static const char* chestEditorIDs[] = {
                "TreasBossBanditChest",
                "TreasBanditChest",
                "TreasChestLarge",
                nullptr
            };
            for (int ci = 0; chestEditorIDs[ci] && !chestBase; ++ci) {
                auto* form = RE::TESForm::LookupByEditorID(chestEditorIDs[ci]);
                if (form && form->GetFormType() == RE::FormType::Container) {
                    chestBase = form->As<RE::TESBoundObject>();
                    logger::info("[IntelEngine] SpawnQuestChest: using chest base '{}'", chestEditorIDs[ci]);
                }
            }
        }

        if (!chestBase) {
            logger::error("[IntelEngine] SpawnQuestChest: could not find chest base form");
            return nullptr;
        }

        // Spawn the chest at the location
        auto spawned = location->PlaceObjectAtMe(chestBase, false);
        if (!spawned) {
            logger::error("[IntelEngine] SpawnQuestChest: PlaceObjectAtMe failed for chest");
            return nullptr;
        }

        auto* chestRef = spawned.get();
        if (!chestRef) {
            logger::error("[IntelEngine] SpawnQuestChest: spawned chest ref is null");
            return nullptr;
        }

        // Add the specific item to the chest
        auto* container = chestRef->As<RE::TESObjectREFR>();
        if (container) {
            auto* invChanges = container->GetInventoryChanges();
            if (!invChanges) {
                // Force inventory creation
                container->InitInventoryIfRequired();
                invChanges = container->GetInventoryChanges();
            }

            // Use AddObjectToContainer to add the item
            container->AddObjectToContainer(itemObj, nullptr, 1, nullptr);
            logger::info("[IntelEngine] SpawnQuestChest: added '{}' (FormID {:08X}) to chest",
                        itemObj->GetName(), itemObj->GetFormID());
        }

        logger::info("[IntelEngine] SpawnQuestChest: chest spawned at {} with '{}'",
                    location->GetName(), nameStr);
        return chestRef;
    }

    bool ValidateQuestItem(RE::StaticFunctionTag*, RE::BSFixedString itemName) {
        std::string nameStr(itemName.c_str());
        if (nameStr.empty()) return false;
        return ItemIndex::GetSingleton()->ValidateName(nameStr);
    }

    RE::BSFixedString GetRandomQuestItemName(RE::StaticFunctionTag*, int minGoldValue) {
        // Exclude recently used quest items for variety
        auto recentItems = NPCIndex::GetSingleton()->GetRecentQuestItemNames();
        auto name = ItemIndex::GetSingleton()->GetRandomValuableName(minGoldValue, recentItems);
        return RE::BSFixedString(name.c_str());
    }

    void NotifyQuestItemUsed(RE::StaticFunctionTag*, RE::BSFixedString itemName) {
        NPCIndex::GetSingleton()->NotifyQuestItemUsed(itemName.c_str());
    }

    void NotifyRescueVictimUsed(RE::StaticFunctionTag*, RE::BSFixedString victimName) {
        NPCIndex::GetSingleton()->NotifyRescueVictimUsed(victimName.c_str());
    }

    void NotifyQuestLocationUsed(RE::StaticFunctionTag*, RE::BSFixedString locationName) {
        auto* str = locationName.c_str();
        if (!str || !*str) return;
        NPCIndex::GetSingleton()->NotifyQuestLocationUsed(str);
    }

    // =========================================================================
    // Quest Boss Spawning (find_item sub-type)
    // =========================================================================

    RE::Actor* SpawnQuestBoss(RE::StaticFunctionTag*, RE::TESObjectREFR* location,
                              RE::BSFixedString enemyType) {
        if (!location) {
            logger::error("[IntelEngine] SpawnQuestBoss: location is null");
            return nullptr;
        }

        std::string type = StringUtils::ToLowerStd(enemyType.c_str());

        RE::TESBoundObject* bossBase = nullptr;

        // Faction boss: use the faction's soldier template (same soldiers, just one extra tough one)
        if (type.substr(0, 8) == "faction:") {
            std::string factionId = std::string(enemyType.c_str()).substr(8);  // original casing
            auto templateId = FactionPolitics::GetSingleton()->GetSoldierTemplate(factionId);
            if (!templateId.empty()) {
                bossBase = LookupLeveledActor(templateId.c_str());
            }
            if (!bossBase) {
                // Fallback: generic bandit boss for faction quests
                bossBase = LookupLeveledActor("LvlBanditBoss");
            }
            logger::info("[IntelEngine] SpawnQuestBoss: faction boss for '{}' (template='{}')",
                factionId, templateId.empty() ? "fallback" : templateId);
        } else {
            const char* bossID = nullptr;
            if (type == "bandit") {
                bossID = "LvlBanditBoss";           // 0x03DF17
            } else if (type == "draugr") {
                bossID = "LvlDraugrWarlockMale";    // 0x01E7AC
            } else if (type == "dragon") {
                bossID = "EncDragon01Fire";          // 0x01CA03
            } else {
                logger::error("[IntelEngine] SpawnQuestBoss: unknown enemy type '{}'", type);
                return nullptr;
            }
            bossBase = LookupLeveledActor(bossID);
        }

        if (!bossBase) {
            logger::error("[IntelEngine] SpawnQuestBoss: boss template not found for '{}'", type);
            return nullptr;
        }

        auto spawned = location->PlaceObjectAtMe(bossBase, true);
        if (!spawned) {
            logger::error("[IntelEngine] SpawnQuestBoss: PlaceObjectAtMe failed");
            return nullptr;
        }

        // Spawn boss close to location (small offset)
        std::uniform_real_distribution<float> spreadDist(-150.0f, 150.0f);
        auto basePos = location->GetPosition();
        RE::NiPoint3 bossPos{basePos.x + spreadDist(s_rng), basePos.y + spreadDist(s_rng), basePos.z};
        spawned->SetPosition(bossPos);

        auto* actor = spawned->As<RE::Actor>();
        if (actor) {
            logger::info("[IntelEngine] SpawnQuestBoss: spawned '{}' boss at ({:.0f}, {:.0f})",
                        bossBase->GetName(), bossPos.x, bossPos.y);
        }
        return actor;
    }

    // =========================================================================
    // Deeper Spawn Point Discovery (find_item sub-type)
    // =========================================================================

    RE::TESObjectREFR* FindDeeperSpawnPoint(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return nullptr;

        auto* cell = actor->GetParentCell();
        if (!cell || !cell->IsInteriorCell()) return nullptr;

        // Scan current cell for deep-dungeon landmark references
        // Priority: word walls > boss chests > regular chests > coffins/sarcophagi > shrines/altars
        RE::TESObjectREFR* wordWall = nullptr;
        RE::TESObjectREFR* bossChest = nullptr;
        RE::TESObjectREFR* chest = nullptr;
        RE::TESObjectREFR* coffin = nullptr;
        RE::TESObjectREFR* shrine = nullptr;

        cell->ForEachReference([&](RE::TESObjectREFR& ref) {
            if (ref.IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;

            auto* baseObj = ref.GetBaseObject();
            if (!baseObj) return RE::BSContainer::ForEachResult::kContinue;

            auto editorId = baseObj->GetFormEditorID();
            std::string editorIdStr = editorId ? StringUtils::ToLowerStd(editorId) : "";
            auto refName = ref.GetName();
            std::string nameStr = refName ? StringUtils::ToLowerStd(refName) : "";

            auto formType = baseObj->GetFormType();

            // Word Walls — TESObjectSTAT with "wordwall" in editor ID
            if (!wordWall && formType == RE::FormType::Static) {
                if (editorIdStr.find("wordwall") != std::string::npos ||
                    editorIdStr.find("word_wall") != std::string::npos) {
                    wordWall = &ref;
                    logger::info("[IntelEngine] FindDeeperSpawnPoint: word wall '{}'", editorIdStr);
                }
            }

            // Boss Chests — TESObjectCONT with "boss" in editor ID
            if (!bossChest && formType == RE::FormType::Container) {
                if (editorIdStr.find("boss") != std::string::npos) {
                    bossChest = &ref;
                    logger::info("[IntelEngine] FindDeeperSpawnPoint: boss chest '{}'", editorIdStr);
                }
            }

            // Regular Chests — TESObjectCONT with "chest" or "treas" (not already matched as boss)
            if (!chest && !bossChest && formType == RE::FormType::Container) {
                if (editorIdStr.find("chest") != std::string::npos ||
                    editorIdStr.find("treas") != std::string::npos) {
                    chest = &ref;
                    logger::info("[IntelEngine] FindDeeperSpawnPoint: chest '{}'", editorIdStr);
                }
            }

            // Coffins/Sarcophagi — Static or Furniture
            if (!coffin && (formType == RE::FormType::Static || formType == RE::FormType::Furniture)) {
                if (StringUtils::ContainsAny(editorIdStr, {"coffin", "sarcophag"}) ||
                    StringUtils::ContainsAny(nameStr, {"coffin", "sarcophag"})) {
                    coffin = &ref;
                    logger::info("[IntelEngine] FindDeeperSpawnPoint: coffin '{}'", editorIdStr);
                }
            }

            // Shrines/Altars — Activator or Static
            if (!shrine && (formType == RE::FormType::Activator || formType == RE::FormType::Static)) {
                if (StringUtils::ContainsAny(editorIdStr, {"shrine", "altar"}) ||
                    StringUtils::ContainsAny(nameStr, {"shrine", "altar"})) {
                    shrine = &ref;
                    logger::info("[IntelEngine] FindDeeperSpawnPoint: shrine/altar '{}'", editorIdStr);
                }
            }

            // Early exit if we found a word wall (highest priority)
            if (wordWall) return RE::BSContainer::ForEachResult::kStop;

            return RE::BSContainer::ForEachResult::kContinue;
        });

        // Return highest-priority landmark
        if (wordWall)  return wordWall;
        if (bossChest) return bossChest;
        if (chest)     return chest;
        if (coffin)    return coffin;
        if (shrine)    return shrine;

        // Fallback: door traversal (find a door leading deeper)
        auto* analyzer = CellAnalyzer::GetSingleton();
        auto doors = analyzer->GetDoors(actor);

        for (auto* door : doors) {
            if (analyzer->IsDoorDownward(door) || analyzer->IsDoorNameDownward(door)) {
                auto* dest = analyzer->GetDoorDestination(door);
                if (dest) {
                    // Only use doors that lead deeper into the dungeon (another interior),
                    // never doors that lead back outside.
                    auto* destCell = dest->GetSaveParentCell();
                    if (!destCell) destCell = dest->GetParentCell();
                    if (destCell && destCell->IsInteriorCell()) {
                        logger::info("[IntelEngine] FindDeeperSpawnPoint: fallback to deeper door");
                        return dest;
                    }
                    logger::info("[IntelEngine] FindDeeperSpawnPoint: skipped door to exterior cell");
                }
            }
        }

        logger::info("[IntelEngine] FindDeeperSpawnPoint: no landmarks or deeper doors in '{}'",
                    cell->GetName());
        return nullptr;
    }

    // =========================================================================
    // Prisoner Furniture Discovery (rescue sub-type)
    // =========================================================================

    // Scan a single cell for prisoner furniture. Returns best match by priority.
    // furnitureOnly=true: only Furniture form type (NPCs can use via Activate — idle markers).
    // furnitureOnly=false: also Statics/Activators (cage meshes, decorative props — for positioning).
    static RE::TESObjectREFR* ScanCellForPrisonerFurnitureImpl(RE::TESObjectCELL* cell, bool furnitureOnly) {
        if (!cell) return nullptr;

        RE::TESObjectREFR* shackle = nullptr;
        RE::TESObjectREFR* cage = nullptr;
        RE::TESObjectREFR* stocks = nullptr;
        RE::TESObjectREFR* prison = nullptr;

        const char* tag = furnitureOnly ? "UsablePrisonerScan" : "PrisonerScan";

        cell->ForEachReference([&](RE::TESObjectREFR& ref) {
            if (ref.IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;

            auto* baseObj = ref.GetBaseObject();
            if (!baseObj) return RE::BSContainer::ForEachResult::kContinue;

            auto formType = baseObj->GetFormType();
            if (furnitureOnly) {
                if (formType != RE::FormType::Furniture) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
            } else {
                if (formType != RE::FormType::Furniture &&
                    formType != RE::FormType::Static &&
                    formType != RE::FormType::Activator &&
                    formType != RE::FormType::MovableStatic) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
            }

            auto editorId = baseObj->GetFormEditorID();
            std::string editorIdStr = editorId ? StringUtils::ToLowerStd(editorId) : "";
            if (editorIdStr.empty()) return RE::BSContainer::ForEachResult::kContinue;

            if (!shackle && StringUtils::ContainsAny(editorIdStr, {"shackle", "manacle"})) {
                shackle = &ref;
                logger::info("[IntelEngine] {}: shackle '{}' in '{}'", tag, editorIdStr, cell->GetName());
            }
            if (!cage && StringUtils::ContainsAny(editorIdStr, {"cage", "gibbet"})) {
                cage = &ref;
                logger::info("[IntelEngine] {}: cage '{}' in '{}'", tag, editorIdStr, cell->GetName());
            }
            if (!stocks && StringUtils::ContainsAny(editorIdStr, {"stock", "pillory"})) {
                if (editorIdStr.find("livestock") == std::string::npos) {
                    stocks = &ref;
                    logger::info("[IntelEngine] {}: stocks '{}' in '{}'", tag, editorIdStr, cell->GetName());
                }
            }
            if (!prison && StringUtils::ContainsAny(editorIdStr, {"prison", "captive", "torture"})) {
                prison = &ref;
                logger::info("[IntelEngine] {}: prison furniture '{}' in '{}'", tag, editorIdStr, cell->GetName());
            }

            if (shackle) return RE::BSContainer::ForEachResult::kStop;
            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (shackle) return shackle;
        if (cage)    return cage;
        if (stocks)  return stocks;
        if (prison)  return prison;
        return nullptr;
    }

    static RE::TESObjectREFR* ScanCellForPrisonerFurniture(RE::TESObjectCELL* cell) {
        return ScanCellForPrisonerFurnitureImpl(cell, false);
    }

    // Scan a single cell for dungeon landmarks (same priority as FindDeeperSpawnPoint).
    static RE::TESObjectREFR* ScanCellForLandmarks(RE::TESObjectCELL* cell) {
        if (!cell) return nullptr;

        RE::TESObjectREFR* wordWall = nullptr;
        RE::TESObjectREFR* bossChest = nullptr;
        RE::TESObjectREFR* chest = nullptr;
        RE::TESObjectREFR* coffin = nullptr;
        RE::TESObjectREFR* shrine = nullptr;

        cell->ForEachReference([&](RE::TESObjectREFR& ref) {
            if (ref.IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;
            auto* baseObj = ref.GetBaseObject();
            if (!baseObj) return RE::BSContainer::ForEachResult::kContinue;

            auto editorId = baseObj->GetFormEditorID();
            std::string editorIdStr = editorId ? StringUtils::ToLowerStd(editorId) : "";
            auto refName = ref.GetName();
            std::string nameStr = refName ? StringUtils::ToLowerStd(refName) : "";
            auto formType = baseObj->GetFormType();

            if (!wordWall && formType == RE::FormType::Static) {
                if (editorIdStr.find("wordwall") != std::string::npos ||
                    editorIdStr.find("word_wall") != std::string::npos) {
                    wordWall = &ref;
                }
            }
            if (!bossChest && formType == RE::FormType::Container) {
                if (editorIdStr.find("boss") != std::string::npos) {
                    bossChest = &ref;
                }
            }
            if (!chest && !bossChest && formType == RE::FormType::Container) {
                if (editorIdStr.find("chest") != std::string::npos ||
                    editorIdStr.find("treas") != std::string::npos) {
                    chest = &ref;
                }
            }
            if (!coffin && (formType == RE::FormType::Static || formType == RE::FormType::Furniture)) {
                if (StringUtils::ContainsAny(editorIdStr, {"coffin", "sarcophag"}) ||
                    StringUtils::ContainsAny(nameStr, {"coffin", "sarcophag"})) {
                    coffin = &ref;
                }
            }
            if (!shrine && (formType == RE::FormType::Activator || formType == RE::FormType::Static)) {
                if (StringUtils::ContainsAny(editorIdStr, {"shrine", "altar"}) ||
                    StringUtils::ContainsAny(nameStr, {"shrine", "altar"})) {
                    shrine = &ref;
                }
            }
            if (wordWall) return RE::BSContainer::ForEachResult::kStop;
            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (wordWall)  return wordWall;
        if (bossChest) return bossChest;
        if (chest)     return chest;
        if (coffin)    return coffin;
        if (shrine)    return shrine;
        return nullptr;
    }

    RE::TESObjectREFR* FindPrisonerFurniture(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return nullptr;
        auto* cell = actor->GetParentCell();
        if (!cell || !cell->IsInteriorCell()) return nullptr;

        auto* result = ScanCellForPrisonerFurniture(cell);
        if (!result) {
            logger::info("[IntelEngine] FindPrisonerFurniture: no prisoner furniture in '{}'",
                        cell->GetName());
        }
        return result;
    }

    static RE::TESObjectREFR* ScanCellForUsablePrisonerFurniture(RE::TESObjectCELL* cell) {
        return ScanCellForPrisonerFurnitureImpl(cell, true);
    }

    RE::TESObjectREFR* FindUsablePrisonerFurniture(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return nullptr;
        auto* cell = actor->GetParentCell();
        if (!cell || !cell->IsInteriorCell()) return nullptr;

        auto* result = ScanCellForUsablePrisonerFurniture(cell);
        if (result) {
            logger::info("[IntelEngine] FindUsablePrisonerFurniture: found usable furniture in '{}'",
                        cell->GetName());
        } else {
            logger::info("[IntelEngine] FindUsablePrisonerFurniture: no usable furniture in '{}'",
                        cell->GetName());
        }
        return result;
    }

    // =========================================================================
    // Rescue Anchor — deep dungeon scan following doors (rescue sub-type)
    // =========================================================================

    RE::TESObjectREFR* FindRescueAnchor(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return nullptr;

        auto* playerCell = actor->GetParentCell();
        if (!playerCell || !playerCell->IsInteriorCell()) return nullptr;

        // Phase 1: Scan player's current cell for prisoner furniture
        auto* result = ScanCellForPrisonerFurniture(playerCell);
        if (result) {
            logger::info("[IntelEngine] FindRescueAnchor: prisoner furniture in current cell");
            return result;
        }

        // Phase 2: Follow doors to adjacent interior cells
        auto* analyzer = CellAnalyzer::GetSingleton();
        auto doors = analyzer->GetDoors(actor);

        // Collect reachable interior cells behind doors
        std::vector<RE::TESObjectCELL*> adjacentCells;
        for (auto* door : doors) {
            auto* dest = analyzer->GetDoorDestination(door);
            if (!dest) continue;
            auto* destCell = dest->GetSaveParentCell();
            if (!destCell) destCell = dest->GetParentCell();
            if (!destCell || !destCell->IsInteriorCell()) continue;
            if (destCell == playerCell) continue;  // skip doors back to same cell
            adjacentCells.push_back(destCell);
        }

        // Phase 2a: Scan adjacent cells for prisoner furniture (priority)
        for (auto* adjCell : adjacentCells) {
            result = ScanCellForPrisonerFurniture(adjCell);
            if (result) {
                logger::info("[IntelEngine] FindRescueAnchor: prisoner furniture behind door in '{}'",
                            adjCell->GetName());
                return result;
            }
        }

        // Phase 2b: Scan adjacent cells for landmarks (fallback)
        for (auto* adjCell : adjacentCells) {
            result = ScanCellForLandmarks(adjCell);
            if (result) {
                logger::info("[IntelEngine] FindRescueAnchor: landmark behind door in '{}'",
                            adjCell->GetName());
                return result;
            }
        }

        // Phase 3: Scan current cell for landmarks (last resort before nullptr)
        result = ScanCellForLandmarks(playerCell);
        if (result) {
            logger::info("[IntelEngine] FindRescueAnchor: landmark in current cell");
            return result;
        }

        logger::info("[IntelEngine] FindRescueAnchor: nothing found in '{}' or adjacent cells",
                    playerCell->GetName());
        return nullptr;
    }

    // =========================================================================
    // Scan Ahead — find anchor in cells BEYOND doors (not current cell)
    // =========================================================================

    RE::TESObjectREFR* ScanAheadForAnchor(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return nullptr;

        auto* playerCell = actor->GetParentCell();
        if (!playerCell || !playerCell->IsInteriorCell()) return nullptr;

        auto* analyzer = CellAnalyzer::GetSingleton();
        auto doors = analyzer->GetDoors(actor);

        // Collect adjacent interior cells (through doors, excluding current cell)
        std::vector<RE::TESObjectCELL*> aheadCells;
        for (auto* door : doors) {
            auto* dest = analyzer->GetDoorDestination(door);
            if (!dest) continue;
            auto* destCell = dest->GetSaveParentCell();
            if (!destCell) destCell = dest->GetParentCell();
            if (!destCell || !destCell->IsInteriorCell()) continue;
            if (destCell == playerCell) continue;
            aheadCells.push_back(destCell);
        }

        if (aheadCells.empty()) {
            logger::info("[IntelEngine] ScanAheadForAnchor: no adjacent interior cells from '{}'",
                        playerCell->GetName());
            return nullptr;
        }

        // Priority 1: prisoner furniture in ahead cells
        for (auto* cell : aheadCells) {
            auto* result = ScanCellForPrisonerFurniture(cell);
            if (result) {
                logger::info("[IntelEngine] ScanAheadForAnchor: prisoner furniture in '{}'",
                            cell->GetName());
                return result;
            }
        }

        // Priority 2: landmarks (boss chests, word walls, etc.) in ahead cells
        for (auto* cell : aheadCells) {
            auto* result = ScanCellForLandmarks(cell);
            if (result) {
                logger::info("[IntelEngine] ScanAheadForAnchor: landmark in '{}'",
                            cell->GetName());
                return result;
            }
        }

        logger::info("[IntelEngine] ScanAheadForAnchor: nothing found ahead from '{}'",
                    playerCell->GetName());
        return nullptr;
    }

    // =========================================================================
    // Dungeon Boss Anchor (pre-placement for rescue/find_item quests)
    // =========================================================================

    RE::TESObjectREFR* GetDungeonBossAnchor(RE::StaticFunctionTag*, RE::BSFixedString locationName) {
        auto* str = locationName.c_str();
        if (!str || !*str) return nullptr;
        return LocationResolver::GetSingleton()->GetDungeonBossAnchor(str);
    }

    // =========================================================================
    // Quest Item Retrieval Check (find_item sub-type)
    // =========================================================================

    bool IsQuestItemInChest(RE::StaticFunctionTag*, RE::TESObjectREFR* container,
                            RE::BSFixedString itemName) {
        if (!container) return false;

        std::string nameStr(itemName.c_str());
        if (nameStr.empty()) return false;

        // Resolve item via ItemIndex
        auto* itemObj = ItemIndex::GetSingleton()->FindByName(nameStr);
        if (!itemObj) return false;

        // Check if the container still has this item
        auto inventory = container->GetInventory();
        for (auto& [form, data] : inventory) {
            if (form && form->GetFormID() == itemObj->GetFormID()) {
                if (data.first > 0) {
                    return true;  // Item still in chest
                }
            }
        }

        return false;  // Item not found — player took it
    }

    // ==========================================================================
    // Faction Politics Functions
    // ==========================================================================

    int GetFactionRelation(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB) {
        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return 0;
        return db->GetRelation(factionA.c_str(), factionB.c_str());
    }

    int AdjustFactionRelation(RE::StaticFunctionTag*, RE::BSFixedString factionA,
                               RE::BSFixedString factionB, int delta) {
        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return 0;
        return db->AdjustRelation(factionA.c_str(), factionB.c_str(), delta);
    }

    int GetPlayerFactionStanding(RE::StaticFunctionTag*, RE::BSFixedString factionId) {
        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return 0;
        return db->GetPlayerStanding(factionId.c_str());
    }

    int AdjustPlayerFactionStanding(RE::StaticFunctionTag*, RE::BSFixedString factionId, int delta) {
        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return 0;
        float gameTime = RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetHoursPassed() : 0.0f;
        return db->AdjustPlayerStanding(factionId.c_str(), delta, gameTime);
    }

    bool IsFactionAtWar(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB) {
        auto* politics = FactionPolitics::GetSingleton();
        if (!politics->IsReady()) return false;
        return politics->IsAtWar(factionA.c_str(), factionB.c_str());
    }

    int GetWarMorale(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB,
                     RE::BSFixedString queryFaction) {
        return FactionPolitics::GetSingleton()->GetWarMorale(factionA.c_str(), factionB.c_str(), queryFaction.c_str());
    }

    RE::BSFixedString GetRelationStatus(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB) {
        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return RE::BSFixedString("Neutral");
        int score = db->GetRelation(factionA.c_str(), factionB.c_str());
        return RE::BSFixedString(FactionPolitics::GetRelationStatus(score).c_str());
    }

    RE::BSFixedString BuildPoliticalContext(RE::StaticFunctionTag*, float currentGameTime) {
        auto* politics = FactionPolitics::GetSingleton();
        if (!politics->IsReady()) return RE::BSFixedString("{}");
        return RE::BSFixedString(politics->BuildPoliticalContext(currentGameTime).c_str());
    }

    RE::BSFixedString BuildPoliticalDashboardJson(RE::StaticFunctionTag*) {
        auto* politics = FactionPolitics::GetSingleton();
        if (!politics->IsReady()) return RE::BSFixedString("{}");
        return RE::BSFixedString(politics->BuildDashboardJson().c_str());
    }

    int RecordPoliticalEvent(RE::StaticFunctionTag*, RE::BSFixedString factionA,
                              RE::BSFixedString factionB, RE::BSFixedString eventType,
                              RE::BSFixedString description, int relationDelta, float gameTime) {
        auto* politics = FactionPolitics::GetSingleton();
        if (!politics->IsReady()) return -1;
        // Single source of truth — FactionPolitics::RecordPoliticalEvent handles
        // validation, clamping, recording, relation adjustment, and state file write.
        return politics->RecordPoliticalEvent(
            factionA.c_str(), factionB.c_str(), eventType.c_str(),
            description.c_str(), relationDelta, gameTime);
    }

    bool IsPoliticsEnabled(RE::StaticFunctionTag*) {
        return FactionPolitics::GetSingleton()->IsEnabled();
    }

    int GetPoliticsTickInterval(RE::StaticFunctionTag*) {
        return FactionPolitics::GetSingleton()->GetTickIntervalHours();
    }

    void ReloadFactionConfig(RE::StaticFunctionTag*) {
        FactionPolitics::GetSingleton()->Reload();
        logger::info("FactionPolitics: Config reloaded via Papyrus");
    }

    std::vector<RE::Actor*> GetFactionLeaderActors(RE::StaticFunctionTag*, RE::BSFixedString factionId) {
        std::vector<RE::Actor*> result;
        auto cfg = FactionPolitics::GetSingleton()->GetFaction(factionId.c_str());
        if (!cfg) return result;

        auto* npcIndex = NPCIndex::GetSingleton();
        for (const auto& leaderName : cfg->leaderNames) {
            auto* actor = npcIndex->FindByName(leaderName);
            if (actor) {
                result.push_back(actor);
            }
        }
        return result;
    }

    std::vector<int> GetFactionLeaderFormIds(RE::StaticFunctionTag*, RE::BSFixedString factionId) {
        std::vector<int> result;
        auto cfg = FactionPolitics::GetSingleton()->GetFaction(factionId.c_str());
        if (!cfg) return result;

        auto* npcIndex = NPCIndex::GetSingleton();
        for (const auto& leaderName : cfg->leaderNames) {
            RE::FormID formId = npcIndex->FindFormIdByName(leaderName);
            if (formId != 0) {
                result.push_back(static_cast<int>(formId));
            } else {
                logger::debug("GetFactionLeaderFormIds: '{}' not found in NPC index", leaderName);
            }
        }
        return result;
    }

    RE::BSFixedString CheckEventManifestation(RE::StaticFunctionTag*,
        RE::BSFixedString factionA, RE::BSFixedString factionB, RE::BSFixedString eventType)
    {
        std::string result = FactionPolitics::GetSingleton()->CheckEventManifestation(
            factionA.c_str(), factionB.c_str(), eventType.c_str());
        return result.c_str();
    }

    void ConfirmManifestationCooldown(RE::StaticFunctionTag*) {
        FactionPolitics::GetSingleton()->ConfirmManifestationCooldown();
    }

    int ApplyPlayerStandingChanges(RE::StaticFunctionTag*, RE::BSFixedString responseJson) {
        // Parse player_standing_changes array from Political DM response.
        // Format: [{"faction":"ImperialFaction","delta":5,"reason":"spoke favorably"}]
        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return 0;

        std::string json = responseJson.c_str();
        if (json.empty()) return 0;

        // Find the player_standing_changes array in the response
        auto pos = json.find("\"player_standing_changes\"");
        if (pos == std::string::npos) return 0;

        // Find the opening bracket
        auto bracketStart = json.find('[', pos);
        if (bracketStart == std::string::npos) return 0;

        // Find matching closing bracket
        int depth = 0;
        size_t bracketEnd = bracketStart;
        for (size_t i = bracketStart; i < json.size(); ++i) {
            if (json[i] == '[') depth++;
            else if (json[i] == ']') { depth--; if (depth == 0) { bracketEnd = i; break; } }
        }
        if (bracketEnd == bracketStart) return 0;

        std::string arrayStr = json.substr(bracketStart, bracketEnd - bracketStart + 1);

        int applied = 0;
        try {
            auto changes = nlohmann::json::parse(arrayStr);
            if (!changes.is_array()) return 0;

            auto* cal = RE::Calendar::GetSingleton();
            float gameTime = cal ? cal->GetCurrentGameTime() : 0.0f;
            auto* politics = FactionPolitics::GetSingleton();
            int maxDelta = politics->GetMaxRelationChangePerTick();

            for (const auto& change : changes) {
                if (!change.contains("faction") || !change.contains("delta")) continue;
                std::string faction = change.value("faction", "");
                int delta = change.value("delta", 0);
                std::string reason = change.value("reason", "");

                // Validate faction ID exists
                if (!politics->GetFaction(faction).has_value()) {
                    logger::warn("Politics: Unknown faction '{}' in player standing change", faction);
                    continue;
                }

                delta = std::clamp(delta, -maxDelta, maxDelta);
                if (delta == 0) continue;

                int newStanding = db->AdjustPlayerStanding(faction, delta, gameTime);
                logger::info("Politics: Player standing with {} changed by {} -> {} ({})",
                             faction, delta, newStanding, reason);
                ++applied;
            }
        } catch (const std::exception& e) {
            logger::warn("Politics: Failed to parse player_standing_changes: {}", e.what());
        }

        return applied;
    }

    int ProcessPlayerConduct(RE::StaticFunctionTag*, RE::Actor* reporter,
                              RE::BSFixedString factionId, RE::BSFixedString sentiment,
                              RE::BSFixedString reason) {
        if (!reporter) return 0;
        return FactionPolitics::GetSingleton()->ProcessPlayerConduct(
            reporter, factionId.c_str(), sentiment.c_str(), reason.c_str());
    }

    int CheckCrimeGoldStandings(RE::StaticFunctionTag*) {
        return FactionPolitics::GetSingleton()->CheckCrimeGoldStandings();
    }

    int DecayPlayerStandings(RE::StaticFunctionTag*, int decayRate) {
        return FactionPolitics::GetSingleton()->DecayPlayerStandings(decayRate);
    }

    void WritePoliticalStateFile(RE::StaticFunctionTag*) {
        FactionPolitics::GetSingleton()->WritePoliticalStateFile();
    }

    void SetPoliticsEnabled(RE::StaticFunctionTag*, bool enabled) {
        FactionPolitics::GetSingleton()->SetEnabled(enabled);
    }

    void SetPoliticsTickInterval(RE::StaticFunctionTag*, int hours) {
        FactionPolitics::GetSingleton()->SetTickIntervalHours(hours);
    }

    // =========================================================================
    // War Lifecycle Natives
    // =========================================================================

    int DeclareWar(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB, float gameTime) {
        return FactionPolitics::GetSingleton()->DeclareWar(factionA.c_str(), factionB.c_str(), gameTime);
    }

    RE::BSFixedString ProcessWarTick(RE::StaticFunctionTag*, float gameTime) {
        std::string result = FactionPolitics::GetSingleton()->ProcessWarTick(gameTime);
        return RE::BSFixedString(result);
    }

    bool EndFactionWar(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB,
                       RE::BSFixedString victor, float gameTime) {
        return FactionPolitics::GetSingleton()->EndWar(factionA.c_str(), factionB.c_str(), victor.c_str(), gameTime);
    }

    int GetActiveWarCount(RE::StaticFunctionTag*) {
        return FactionPolitics::GetSingleton()->GetActiveWarCount();
    }

    int GetActiveWarId(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB) {
        return FactionPolitics::GetSingleton()->GetActiveWarId(factionA.c_str(), factionB.c_str());
    }

    int GetWarStrength(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB,
                       RE::BSFixedString queryFaction) {
        return FactionPolitics::GetSingleton()->GetWarStrength(factionA.c_str(), factionB.c_str(), queryFaction.c_str());
    }

    int RecordOffScreenBattle(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB,
                              RE::BSFixedString location, RE::BSFixedString result, RE::BSFixedString narrative,
                              int attackerLosses, int defenderLosses, RE::BSFixedString victor) {
        return FactionPolitics::GetSingleton()->RecordOffScreenBattle(
            factionA.c_str(), factionB.c_str(), location.c_str(), result.c_str(),
            narrative.c_str(), attackerLosses, defenderLosses, victor.c_str());
    }

    // ==========================================================================
    // Faction Query Functions
    // ==========================================================================

    bool IsHighStatusNPC(RE::StaticFunctionTag*, RE::Actor* actor) {
        // Delegates to NPCIndex::IsHighStatus which checks:
        //   - JobJarlFaction (via editor ID lookup, load-order safe)
        //   - JobCourtWizardFaction, JobStewardFaction, JobHousecarlFaction
        if (!actor) return false;

        if (NPCIndex::IsHighStatus(actor)) return true;

        // NOTE: We do NOT use Unique+Essential as a catch-all here.
        // While it catches generals/faction heads, it also false-positives on
        // followers (Lydia, Serana), quest NPCs (Delphine, Brynjolf), and other
        // essential unique characters who are valid dispatch candidates.
        // The faction-based checks above are sufficient and precise.

        return false;
    }

    RE::BSFixedString ExtractFactionId(RE::StaticFunctionTag*, RE::BSFixedString enemyType) {
        // Extracts faction ID from "faction:FactionId" format. Returns "" if no prefix.
        std::string_view sv = enemyType.c_str();
        constexpr std::string_view prefix = "faction:";
        if (sv.starts_with(prefix)) {
            return RE::BSFixedString(std::string(sv.substr(prefix.size())));
        }
        return RE::BSFixedString("");
    }

    RE::BSFixedString GetFactionDisplayName(RE::StaticFunctionTag*, RE::BSFixedString factionId) {
        auto faction = FactionPolitics::GetSingleton()->GetFaction(factionId.c_str());
        return faction ? RE::BSFixedString(faction->name) : RE::BSFixedString(factionId.c_str());
    }

    RE::BSFixedString GetFactionRival(RE::StaticFunctionTag*, RE::BSFixedString factionId) {
        auto rival = FactionPolitics::GetSingleton()->GetFactionRival(factionId.c_str());
        return RE::BSFixedString(rival);
    }

    RE::BSFixedString GetFactionWarEnemy(RE::StaticFunctionTag*, RE::BSFixedString factionId) {
        return RE::BSFixedString(FactionPolitics::GetSingleton()->GetFactionWarEnemy(factionId.c_str()));
    }

    RE::BSFixedString GetNPCPoliticalFactionId(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return RE::BSFixedString("");
        auto factionId = FactionPolitics::GetSingleton()->GetNPCFactionId(actor);
        return RE::BSFixedString(factionId);
    }

    RE::Actor* FindFactionMember(RE::StaticFunctionTag*, RE::BSFixedString factionId) {
        auto* fp = FactionPolitics::GetSingleton();
        auto cfg = fp->GetFaction(factionId.c_str());
        if (!cfg) return nullptr;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;

        auto playerPos = player->GetPosition();
        RE::Actor* best = nullptr;
        float bestDist = (std::numeric_limits<float>::max)();
        std::string bestName;

        // Prefer generic (non-unique) faction members over named leaders.
        // Uses NPCIndex::IsHighStatus to skip Jarls, court wizards, stewards, housecarls.
        // Generic guards/soldiers (non-unique) are the natural choice for delivering faction messages.
        RE::Actor* bestGeneric = nullptr;
        float bestGenericDist = (std::numeric_limits<float>::max)();
        RE::Actor* bestUnique = nullptr;
        float bestUniqueDist = (std::numeric_limits<float>::max)();

        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) -> bool {
            if (!actor || actor == player) return false;
            if (actor->IsDead() || actor->IsDisabled()) return false;
            if (actor->IsInCombat()) return false;

            // Skip high-status NPCs (Jarls, court wizards, stewards, housecarls)
            if (NPCIndex::IsHighStatus(actor)) return false;

            auto actorFaction = fp->GetNPCFactionId(actor);
            if (actorFaction != cfg->id) return false;

            float dist = actor->GetPosition().GetDistance(playerPos);
            auto* base = actor->GetActorBase();
            bool isUnique = base && base->IsUnique();

            if (!isUnique) {
                if (dist < bestGenericDist) {
                    bestGeneric = actor;
                    bestGenericDist = dist;
                }
            } else {
                if (dist < bestUniqueDist) {
                    bestUnique = actor;
                    bestUniqueDist = dist;
                }
            }
            return false;
        });

        // Prefer generic soldiers/guards, fall back to non-essential unique NPCs
        best = bestGeneric ? bestGeneric : bestUnique;
        bestDist = bestGeneric ? bestGenericDist : bestUniqueDist;
        if (best) {
            bestName = best->GetDisplayFullName();
        }

        if (best) {
            logger::info("[StoryDM] FindFactionMember('{}') -> '{}' (dist={:.0f})",
                         factionId.c_str(), bestName, bestDist);
        } else {
            logger::info("[StoryDM] FindFactionMember('{}') -> no loaded member found",
                         factionId.c_str());
        }
        return best;
    }

    // ==========================================================================
    // Battle System Helper Functions
    // ==========================================================================

    RE::BSFixedString GetFactionSoldierTemplate(RE::StaticFunctionTag*, RE::BSFixedString factionId) {
        return RE::BSFixedString(FactionPolitics::GetSingleton()->GetSoldierTemplate(factionId.c_str()));
    }

    // factionIdWithCount format: "FactionId:Count" (e.g., "StormcloakFaction:7")
    std::vector<RE::Actor*> SpawnBattleSoldiers(RE::StaticFunctionTag*, RE::BSFixedString factionIdWithCount,
                                                RE::TESObjectREFR* spawnAt) {
        std::vector<RE::Actor*> result;
        if (!spawnAt) {
            logger::error("[IntelEngine] SpawnBattleSoldiers: spawnAt is null");
            return result;
        }

        // Parse "FactionId:Count"
        std::string input(factionIdWithCount.c_str());
        std::string factionId;
        int count = 0;
        auto colonPos = input.rfind(':');
        if (colonPos != std::string::npos) {
            factionId = input.substr(0, colonPos);
            try { count = std::stoi(input.substr(colonPos + 1)); } catch (...) { count = 0; }
        } else {
            factionId = input;
        }

        constexpr int MAX_SOLDIERS_PER_SPAWN = 20;
        if (count <= 0 || count > MAX_SOLDIERS_PER_SPAWN) {
            logger::error("[IntelEngine] SpawnBattleSoldiers: invalid count {} from '{}'", count, input);
            return result;
        }

        auto templateId = FactionPolitics::GetSingleton()->GetSoldierTemplate(factionId);

        // Resolve template EditorID to a spawnable base form
        RE::TESBoundObject* base = nullptr;
        if (!templateId.empty()) {
            base = LookupLeveledActor(templateId.c_str());
        }

        // Fallback to generic bandit if faction template not found
        if (!base) {
            logger::warn("[IntelEngine] SpawnBattleSoldiers: template '{}' not found for faction '{}', using bandit fallback",
                        templateId, factionId);
            base = LookupLeveledActor("LvlBanditMeleeAny");
        }

        if (!base) {
            logger::error("[IntelEngine] SpawnBattleSoldiers: no spawnable base found (even fallback failed)");
            return result;
        }

        logger::info("[IntelEngine] SpawnBattleSoldiers: base FormID={:08X} type={} name='{}', template='{}', spawnAt='{}'",
                    base->GetFormID(), static_cast<int>(base->GetFormType()),
                    base->GetName(), templateId, spawnAt->GetName());

        // PlaceObjectAtMe with a LeveledNPC base returns type 61 (REFR) instead of
        // type 62 (Actor), making the refs unusable in Papyrus. Resolve the leveled
        // list to concrete TESNPC forms first so PlaceObjectAtMe gets a real NPC.
        // Nested leveled lists are resolved recursively (max 10 depth).
        std::vector<RE::TESBoundObject*> resolvedBases;
        if (base->GetFormType() == RE::FormType::LeveledNPC) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto playerLevel = player ? player->GetLevel() : static_cast<std::uint16_t>(25);

            RE::BSScrapArray<RE::CALCED_OBJECT> calced;
            static_cast<RE::TESLevCharacter*>(base)->CalculateCurrentFormList(playerLevel, static_cast<std::int16_t>(count), calced, 0, false);

            for (std::uint32_t ci = 0; ci < calced.size(); ++ci) {
                auto* form = calced[ci].form;
                if (!form) continue;

                // Resolve nested leveled lists (e.g., LCharSoldierSons -> LCharSoldierSonsOutfit -> TESNPC)
                constexpr int MAX_DEPTH = 10;
                for (int depth = 0; depth < MAX_DEPTH && form->GetFormType() == RE::FormType::LeveledNPC; ++depth) {
                    RE::BSScrapArray<RE::CALCED_OBJECT> sub;
                    static_cast<RE::TESLevCharacter*>(form)->CalculateCurrentFormList(playerLevel, 1, sub, 0, false);
                    if (!sub.empty() && sub[0].form) {
                        form = sub[0].form;
                    } else {
                        break;
                    }
                }

                if (form->GetFormType() != RE::FormType::LeveledNPC) {
                    resolvedBases.push_back(static_cast<RE::TESBoundObject*>(form));
                } else {
                    logger::warn("[IntelEngine] SpawnBattleSoldiers: [{}] could not resolve nested leveled list, skipping", ci);
                }
            }
        } else {
            for (int i = 0; i < count; ++i) {
                resolvedBases.push_back(base);
            }
        }

        if (resolvedBases.empty()) {
            logger::error("[IntelEngine] SpawnBattleSoldiers: all leveled list resolutions failed for '{}'", templateId);
            return result;
        }

        logger::info("[IntelEngine] SpawnBattleSoldiers: resolved {} bases from leveled list", resolvedBases.size());

        for (size_t i = 0; i < resolvedBases.size(); ++i) {
            auto spawned = spawnAt->PlaceObjectAtMe(resolvedBases[i], true);
            if (spawned) {
                auto* actor = spawned->As<RE::Actor>();
                if (actor) {
                    result.push_back(actor);
                } else {
                    logger::error("[IntelEngine] SpawnBattleSoldiers: [{}] spawned FormID={:08X} but type={} (not Actor!)",
                                i, spawned->GetFormID(), static_cast<int>(spawned->GetFormType()));
                }
            } else {
                logger::error("[IntelEngine] SpawnBattleSoldiers: [{}] PlaceObjectAtMe returned null", i);
            }
        }

        // Remove hold crime factions from ALL spawned soldiers — killing them generates no bounty.
        // This applies to battle soldiers, ambush soldiers, and manifestation soldiers.
        static constexpr RE::FormID crimeFactionIds[] = {
            0x00029DB0, 0x00029DB1, 0x00029DB2, 0x00029DB3,
            0x00029DB4, 0x00029DB5, 0x00029DB6, 0x00029DB7, 0x00029DB8
        };
        for (auto* actor : result) {
            if (!actor) continue;
            for (auto fid : crimeFactionIds) {
                auto* crimeFaction = RE::TESForm::LookupByID<RE::TESFaction>(fid);
                if (crimeFaction) actor->AddToFaction(crimeFaction, -1);
            }
        }

        logger::info("[IntelEngine] SpawnBattleSoldiers: spawned {}/{} for faction '{}' (crime factions removed)",
                    result.size(), count, factionId);
        return result;
    }

    int GetJsonArrayInt(RE::StaticFunctionTag*, RE::BSFixedString jsonStr,
                        RE::BSFixedString arrayKey, int index) {
        try {
            auto j = nlohmann::json::parse(jsonStr.c_str());
            auto key = std::string(arrayKey.c_str());
            if (j.contains(key) && j[key].is_array()) {
                auto& arr = j[key];
                if (index >= 0 && static_cast<size_t>(index) < arr.size()) {
                    // FormIDs can exceed INT_MAX (0xFF prefix = 4278190080+).
                    // Use uint32 to avoid overflow, then reinterpret as signed for Papyrus.
                    auto val = arr[index].get<uint32_t>();
                    return static_cast<int>(val);
                }
            }
        } catch (...) {}
        return 0;
    }

    void SetActorProtected(RE::StaticFunctionTag*, RE::Actor* actor, bool protect) {
        if (!actor) return;
        // Set per-actor Protected flag — does NOT affect other actors sharing the same ActorBase.
        // Protected actors enter bleedout but can only be killed by the player.
        if (protect) {
            actor->GetActorRuntimeData().boolFlags.set(RE::Actor::BOOL_FLAGS::kProtected);
        } else {
            actor->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kProtected);
        }
    }

    RE::BSFixedString ExecuteFullBattleSpawn(RE::StaticFunctionTag*,
                                              RE::BSFixedString questAutoJoinFaction,
                                              RE::Actor* player, float playerAngleZ,
                                              RE::TESObjectREFR* spawnAnchor) {
        auto* bm = BattleManager::GetSingleton();
        if (!bm) return RE::BSFixedString("{}");
        return RE::BSFixedString(bm->ExecuteFullBattleSpawn(
            questAutoJoinFaction.c_str(), player, playerAngleZ, spawnAnchor).c_str());
    }

    RE::BSFixedString SpawnReinforcements(RE::StaticFunctionTag*, int count, RE::Actor* player,
                                          RE::TESObjectREFR* spawnAnchor) {
        auto* bm = BattleManager::GetSingleton();
        if (!bm) return RE::BSFixedString("{}");
        return RE::BSFixedString(bm->SpawnReinforcements(count, player, spawnAnchor).c_str());
    }

    RE::BSFixedString GetBattleSoldierFormIds(RE::StaticFunctionTag*, RE::BSFixedString side) {
        auto* bm = BattleManager::GetSingleton();
        if (!bm) return RE::BSFixedString("{}");
        return RE::BSFixedString(bm->GetBattleSoldierFormIds(side.c_str()).c_str());
    }

    void SetBattleSoldiersAsTeammates(RE::StaticFunctionTag*, RE::BSFixedString side) {
        auto* bm = BattleManager::GetSingleton();
        if (bm) bm->SetBattleSoldiersAsTeammates(side.c_str(), true);
    }

    int CountDeadBattleSoldiers(RE::StaticFunctionTag*, RE::BSFixedString side) {
        auto* bm = BattleManager::GetSingleton();
        if (!bm) return 0;
        return bm->CountDeadSoldiers(side.c_str());
    }

    int CleanupBattleSoldiers(RE::StaticFunctionTag*, float playerX, float playerY,
                               float playerZ, float playerAngleZ, bool forceAll) {
        auto* bm = BattleManager::GetSingleton();
        if (!bm) return 0;
        return bm->CleanupBattleSoldiers(playerX, playerY, playerZ, playerAngleZ, forceAll);
    }

    void ForceCleanupAllSoldiers(RE::StaticFunctionTag*) {
        auto* bm = BattleManager::GetSingleton();
        if (bm) bm->ForceCleanupAllSoldiers();
    }

    void SnapshotBounties(RE::StaticFunctionTag*) {
        auto* bm = BattleManager::GetSingleton();
        if (bm) bm->SnapshotBounties();
    }

    // Snapshot of bounty at battle start — only clear what was added DURING the battle
    static std::unordered_map<RE::FormID, std::int32_t> s_preBattleBounty;
    static constexpr RE::FormID kHoldCrimeFactions[] = {
        0x00029DB0, 0x00029DB1, 0x00029DB2, 0x00029DB3, 0x00029DB4,
        0x00029DB5, 0x00029DB6, 0x00029DB7, 0x00029DB8
    };

    void ClearBattleTeammates(RE::StaticFunctionTag*) {
        // Centralized: delegates to BattleManager::RestoreEnlistedGuards which uses
        // tracked FormIDs (works even if guards unloaded). No more ForEachLoadedActor guesswork.
        BattleManager::GetSingleton()->RestoreEnlistedGuards();
    }

    void ClearAllHoldBounties(RE::StaticFunctionTag*) {
        // Revert bounty to pre-battle snapshot — only clears what accumulated DURING the battle.
        // First call snapshots current bounty; subsequent calls revert to snapshot.
        if (s_preBattleBounty.empty()) {
            // First call — take snapshot
            for (auto fid : kHoldCrimeFactions) {
                auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(fid);
                if (faction) {
                    s_preBattleBounty[fid] = faction->GetCrimeGold();
                }
            }
            return;
        }
        // Subsequent calls — revert to snapshot
        for (auto fid : kHoldCrimeFactions) {
            auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(fid);
            if (!faction) continue;
            auto it = s_preBattleBounty.find(fid);
            std::int32_t preBattle = (it != s_preBattleBounty.end()) ? it->second : 0;
            if (faction->GetCrimeGold() > preBattle) {
                faction->SetCrimeGold(preBattle);
                faction->SetCrimeGoldViolent(0);
            }
        }
    }

    // ClearBattleBounties and RestoreBounties implementations removed.

    // ==========================================================================
    // Phase 2 Migration: FactionPolitics C++ Logic (Papyrus native wrappers)
    // ==========================================================================

    RE::BSFixedString ProcessPoliticalDMResponse(RE::StaticFunctionTag*, RE::BSFixedString response, int success) {
        return RE::BSFixedString(FactionPolitics::GetSingleton()->ProcessPoliticalDMResponse(
            response.c_str(), success));
    }

    RE::BSFixedString RunStandingMechanicsNative(RE::StaticFunctionTag*) {
        return RE::BSFixedString(FactionPolitics::GetSingleton()->RunStandingMechanicsInternal());
    }

    // ==========================================================================
    // Remaining Migration: Text builders, math helpers, display formatters
    // ==========================================================================

    RE::BSFixedString BuildTaskHistoryDesc(RE::StaticFunctionTag*,
                                            RE::BSFixedString taskType, RE::BSFixedString target,
                                            RE::BSFixedString result, RE::BSFixedString msgContent,
                                            RE::BSFixedString meetLocation) {
        std::string type = taskType.c_str();
        std::string tgt = target.c_str();
        std::string res = result.c_str();
        std::string msg = msgContent.c_str();
        std::string loc = meetLocation.c_str();
        std::string desc;

        if (type == "travel") {
            desc = (res == "timeout") ? "Went to " + tgt + " but gave up waiting" : "Traveled to " + tgt;
        } else if (type == "fetch_npc") {
            desc = (res == "success") ? "Found " + tgt + " and brought them back"
                                      : "Looked for " + tgt + " but couldn't bring them";
        } else if (type == "deliver_message") {
            if (res == "delivered") {
                desc = "Delivered a message to " + tgt;
                if (!msg.empty()) {
                    if (msg.size() > 80) msg = msg.substr(0, 80) + "...";
                    desc += ": '" + msg + "'";
                }
                if (!loc.empty()) desc += " (meeting at " + loc + ")";
            } else {
                desc = "Tried to deliver a message to " + tgt;
            }
        } else if (type == "search_for_actor") {
            desc = "Searched for " + tgt;
        } else if (type == "story") {
            desc = "Sought out " + tgt;
        } else if (type == "story_npc") {
            desc = "Went to talk with " + tgt;
        } else {
            desc = "Completed a task involving " + tgt;
        }
        return RE::BSFixedString(desc);
    }

    RE::BSFixedString GetSlotStatusNative(RE::StaticFunctionTag*,
                                           RE::BSFixedString taskType, int taskState,
                                           RE::BSFixedString targetName, RE::BSFixedString cellName) {
        std::string type = taskType.c_str();
        std::string target = targetName.c_str();
        std::string cell = cellName.c_str();
        std::string status;

        if (type == "fetch_npc") {
            if (taskState == 1) status = "Going to fetch " + target;
            else if (taskState == 2) status = "Fetching " + target;
            else if (taskState == 3) status = "Returning with " + target;
            else status = "Fetching " + target;
        } else if (type == "deliver_message") {
            if (taskState == 1) status = "Going to deliver a message to " + target;
            else if (taskState == 2) status = "Delivering message to " + target;
            else if (taskState == 3) status = "Returning after delivering message";
            else status = "Delivering message to " + target;
        } else if (type == "search_for_actor") {
            status = "Searching for " + target;
        } else if (type == "escort_target") {
            if (taskState == 1) status = "Going to escort " + target;
            else if (taskState == 2) status = "Escorting " + target;
            else status = "Escorting " + target;
        } else if (type == "travel") {
            status = "Traveling to " + target;
        } else if (type == "story") {
            status = "Seeking out the player";
        } else if (type == "story_npc") {
            status = "Going to visit " + target;
        } else {
            status = "On a task";
        }

        if (!cell.empty()) status += " (near " + cell + ")";
        return RE::BSFixedString(status);
    }

    RE::BSFixedString GetPreciseTimeDescriptionNative(RE::StaticFunctionTag*, float meetTimeHours, float currentGameTime) {
        float currentHours = std::fmod(currentGameTime * 24.f, 24.f);
        float hoursUntil = meetTimeHours - currentHours;
        if (hoursUntil < 0) hoursUntil += 24.f;

        bool tomorrow = (meetTimeHours < currentHours && hoursUntil > 1.f);
        std::string prefix = tomorrow ? "tomorrow " : "";

        int hour = static_cast<int>(meetTimeHours);
        int minute = static_cast<int>((meetTimeHours - hour) * 60.f);
        std::string ampm = (hour >= 12) ? "PM" : "AM";
        int displayHour = hour % 12;
        if (displayHour == 0) displayHour = 12;

        char buf[64];
        if (minute > 0) {
            snprintf(buf, sizeof(buf), "%s%d:%02d %s", prefix.c_str(), displayHour, minute, ampm.c_str());
        } else {
            snprintf(buf, sizeof(buf), "%s%d %s", prefix.c_str(), displayHour, ampm.c_str());
        }
        return RE::BSFixedString(buf);
    }

    RE::BSFixedString GetTimeDescriptionNative(RE::StaticFunctionTag*, float hours) {
        int h = static_cast<int>(hours);
        if (h < 4) return RE::BSFixedString("late night");
        if (h < 6) return RE::BSFixedString("early morning");
        if (h < 8) return RE::BSFixedString("dawn");
        if (h < 10) return RE::BSFixedString("morning");
        if (h < 12) return RE::BSFixedString("late morning");
        if (h < 14) return RE::BSFixedString("midday");
        if (h < 16) return RE::BSFixedString("afternoon");
        if (h < 18) return RE::BSFixedString("late afternoon");
        if (h < 20) return RE::BSFixedString("evening");
        if (h < 22) return RE::BSFixedString("night");
        return RE::BSFixedString("late night");
    }

    RE::BSFixedString DetermineLatenessOutcomeNative(RE::StaticFunctionTag*, float scheduledTime,
                                                      float arrivalTime, float gracePeriod) {
        float hoursLate = (arrivalTime - scheduledTime) * 24.f;
        if (hoursLate > gracePeriod) return RE::BSFixedString("late");
        if (hoursLate < -gracePeriod) return RE::BSFixedString("early");
        return RE::BSFixedString("on_time");
    }

    RE::BSFixedString BuildStuckNarration(RE::StaticFunctionTag*, RE::BSFixedString taskType) {
        std::string type = taskType.c_str();
        if (type == "fetch_npc") return RE::BSFixedString("tried to carry out the task but couldn't get going and gave up.");
        if (type == "deliver_message") return RE::BSFixedString("tried to deliver the message but was unable to reach the destination and gave up.");
        if (type == "search_for_actor") return RE::BSFixedString("searched but couldn't find who they were looking for and gave up.");
        if (type == "escort_target") return RE::BSFixedString("tried to escort their charge but couldn't make progress and gave up.");
        return RE::BSFixedString("tried to leave but was unable to and gave up on the task.");
    }

    bool IsUrgentMessage(RE::StaticFunctionTag*, RE::BSFixedString msgContent) {
        std::string msg = msgContent.c_str();
        // Lowercase for comparison
        std::transform(msg.begin(), msg.end(), msg.begin(), ::tolower);
        static const std::vector<std::string> urgencyWords = {
            "immediate", "right now", "at once", "right away", "urgently",
            "without delay", "this instant", "as soon as possible"
        };
        for (const auto& word : urgencyWords) {
            if (msg.find(word) != std::string::npos) return true;
        }
        return false;
    }

    // ==========================================================================
    // Phase 3 Migration: StoryEngine helpers (Papyrus native wrappers)
    // ==========================================================================

    RE::BSFixedString BuildExcludeListNative(RE::StaticFunctionTag*, int toggleBitmask, int envFlags) {
        return RE::BSFixedString(FactionPolitics::BuildExcludeList(toggleBitmask, envFlags));
    }

    RE::BSFixedString ValidateStoryResponse(RE::StaticFunctionTag*, RE::BSFixedString responseJson,
                                             int toggleBitmask, int envFlags) {
        return RE::BSFixedString(FactionPolitics::ValidateStoryResponse(
            responseJson.c_str(), toggleBitmask, envFlags));
    }

    RE::BSFixedString BuildFactionBattleDispatchFact(RE::StaticFunctionTag*,
                                                      RE::BSFixedString alliedFaction,
                                                      RE::BSFixedString questLocation,
                                                      RE::BSFixedString playerName) {
        return RE::BSFixedString(FactionPolitics::GetSingleton()->BuildFactionBattleDispatchFact(
            alliedFaction.c_str(), questLocation.c_str(), playerName.c_str()));
    }

    RE::BSFixedString RecordFactionBattleCompletion(RE::StaticFunctionTag*,
                                                     RE::BSFixedString alliedFaction,
                                                     RE::BSFixedString questLocation,
                                                     RE::BSFixedString playerName,
                                                     RE::BSFixedString enemyFaction) {
        std::string enemy = enemyFaction.c_str() ? enemyFaction.c_str() : "";
        return RE::BSFixedString(FactionPolitics::GetSingleton()->RecordFactionBattleCompletion(
            alliedFaction.c_str(), questLocation.c_str(), playerName.c_str(), enemy));
    }

    RE::BSFixedString BuildBattleExpiryFact(RE::StaticFunctionTag*,
                                             RE::BSFixedString alliedFaction,
                                             RE::BSFixedString questLocation,
                                             RE::BSFixedString playerName) {
        return RE::BSFixedString(FactionPolitics::GetSingleton()->BuildBattleExpiryFact(
            alliedFaction.c_str(), questLocation.c_str(), playerName.c_str()));
    }

    // ==========================================================================
    // Phase 1 Migration: BattleManager C++ Logic (Papyrus native wrappers)
    // ==========================================================================

    RE::BSFixedString FinalizeBattle(RE::StaticFunctionTag*, int battleId,
                                     RE::BSFixedString result, RE::BSFixedString victor,
                                     int deadA, int deadB, RE::BSFixedString locationName,
                                     float gameTime) {
        return RE::BSFixedString(BattleManager::GetSingleton()->FinalizeBattle(
            battleId, result.c_str(), victor.c_str(), deadA, deadB,
            locationName.c_str(), gameTime));
    }

    RE::BSFixedString CalculateReinforcementPositions(RE::StaticFunctionTag*,
                                                       float playerX, float playerY, float playerZ,
                                                       float centerX, float centerY, int waveNum) {
        return RE::BSFixedString(BattleManager::GetSingleton()->CalculateReinforcementPositions(
            playerX, playerY, playerZ, centerX, centerY, waveNum));
    }

    RE::BSFixedString EvaluatePlayerJoinBattle(RE::StaticFunctionTag*, RE::BSFixedString questAutoJoin) {
        return RE::BSFixedString(BattleManager::GetSingleton()->EvaluatePlayerJoin(questAutoJoin.c_str()));
    }

    RE::BSFixedString GetBattleNotification(RE::StaticFunctionTag*, RE::BSFixedString type,
                                             RE::BSFixedString locationName, RE::BSFixedString victorName,
                                             bool playerWon) {
        return RE::BSFixedString(BattleManager::GetSingleton()->GetBattleNotification(
            type.c_str(), locationName.c_str(), victorName.c_str(), playerWon));
    }

    RE::BSFixedString ValidateFactionBattleDispatch(RE::StaticFunctionTag*, RE::BSFixedString alliedFaction,
                                                     RE::BSFixedString suggestedEnemy) {
        std::string enemy = suggestedEnemy.c_str() ? suggestedEnemy.c_str() : "";
        return RE::BSFixedString(BattleManager::GetSingleton()->ValidateFactionBattleDispatch(
            alliedFaction.c_str(), enemy));
    }

    RE::BSFixedString CalculateMidBattleState(RE::StaticFunctionTag*, float scheduledTime, float currentTime) {
        return RE::BSFixedString(BattleManager::GetSingleton()->CalculateMidBattleState(scheduledTime, currentTime));
    }

    RE::BSFixedString GetPollAction(RE::StaticFunctionTag*, RE::BSFixedString stateJson) {
        return RE::BSFixedString(BattleManager::GetSingleton()->GetPollAction(stateJson.c_str()));
    }

    void ResetBattleState(RE::StaticFunctionTag*) {
        BattleManager::GetSingleton()->ResetBattleState();
        s_preBattleBounty.clear();  // Reset bounty snapshot for next battle
    }

    RE::BSFixedString CalculateBattleMarkerPosition(RE::StaticFunctionTag*,
                                                     float playerX, float playerY,
                                                     float locX, float locY, float locZ,
                                                     float offsetUnits) {
        return RE::BSFixedString(BattleManager::GetSingleton()->CalculateBattleMarkerPosition(
            playerX, playerY, locX, locY, locZ, offsetUnits));
    }

    // ==========================================================================
    // Battle Witness Functions
    // ==========================================================================

    std::vector<RE::Actor*> GetNearbyWitnessNPCs(RE::StaticFunctionTag*,
                                                  RE::TESObjectREFR* center, float radius) {
        std::vector<RE::Actor*> result;
        if (!center) return result;

        auto pos = center->GetPosition();

        // Get all battle actor FormIDs to exclude them
        std::unordered_set<RE::FormID> battleActors;
        auto* bm = BattleManager::GetSingleton();
        // BattleSnapshot doesn't include actors, so we check via IsBattleActive
        // and skip actors that are in battle factions

        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return result;

        for (auto& handle : processLists->highActorHandles) {
            auto actor = handle.get();
            if (!actor || !actor.get()) continue;
            auto* a = actor.get();

            // Skip dead, player, deleted
            if (a->IsDead() || a->IsPlayerRef() || a->IsDeleted()) continue;

            // Skip unnamed (generic spawns)
            auto name = a->GetName();
            if (!name || name[0] == '\0') continue;

            // Skip if too far
            auto aPos = a->GetPosition();
            float dx = pos.x - aPos.x;
            float dy = pos.y - aPos.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > radius) continue;

            // Skip battle-spawned soldiers (they have temp FormIDs in FF range)
            if ((a->GetFormID() >> 24) == 0xFF) continue;

            result.push_back(a);
        }

        logger::info("[IntelEngine] GetNearbyWitnessNPCs: found {} witnesses within {:.0f} units",
                    result.size(), radius);
        return result;
    }

    // ==========================================================================
    // BattleManager Functions
    // ==========================================================================

    int StartBattle(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB,
                    RE::BSFixedString locationName, int warId) {
        return BattleManager::GetSingleton()->StartBattle(
            factionA.c_str(), factionB.c_str(), locationName.c_str(), warId);
    }

    void EndBattle(RE::StaticFunctionTag*, int battleId, RE::BSFixedString result, RE::BSFixedString victor) {
        BattleManager::GetSingleton()->EndBattle(battleId, result.c_str(), victor.c_str());
    }

    bool RegisterBattleActor(RE::StaticFunctionTag*, RE::Actor* actor, RE::BSFixedString factionId, int tier) {
        if (!actor) return false;
        return BattleManager::GetSingleton()->RegisterActor(actor, factionId.c_str(), tier);
    }

    RE::BSFixedString PollBattleState(RE::StaticFunctionTag*) {
        auto* bm = BattleManager::GetSingleton();
        auto result = bm->PollBattleState();
        // Suppress bounty + stop hostile friendly guards every poll cycle (C++ — stale-bytecode-safe)
        bm->SuppressBountyTick();
        return RE::BSFixedString(result);
    }

    int GetBattleMorale(RE::StaticFunctionTag*, RE::BSFixedString factionId) {
        return BattleManager::GetSingleton()->GetMorale(factionId.c_str());
    }

    void AdjustBattleMorale(RE::StaticFunctionTag*, RE::BSFixedString factionId, int delta) {
        BattleManager::GetSingleton()->AdjustMorale(factionId.c_str(), delta);
    }

    bool IsBattleActive(RE::StaticFunctionTag*) {
        return BattleManager::GetSingleton()->IsBattleActive();
    }

    int GetBattleAliveCount(RE::StaticFunctionTag*, RE::BSFixedString factionId) {
        return BattleManager::GetSingleton()->GetAliveCount(factionId.c_str());
    }

    int GetActiveBattleId(RE::StaticFunctionTag*) {
        return BattleManager::GetSingleton()->GetActiveBattleId();
    }

    int GetBattleCurrentWave(RE::StaticFunctionTag*) {
        return BattleManager::GetSingleton()->GetCurrentWave();
    }

    void AdvanceBattleWave(RE::StaticFunctionTag*) {
        BattleManager::GetSingleton()->AdvanceWave();
    }

    bool SetPlayerBattleSide(RE::StaticFunctionTag*, RE::BSFixedString factionId) {
        return BattleManager::GetSingleton()->SetPlayerSide(factionId.c_str());
    }

    void RemovePlayerCrimeFactions(RE::StaticFunctionTag*) {
        BattleManager::GetSingleton()->RemovePlayerCrimeFactions();
    }

    void RestorePlayerCrimeFactions(RE::StaticFunctionTag*) {
        BattleManager::GetSingleton()->RestorePlayerCrimeFactions();
    }

    RE::BSFixedString GetPlayerBattleSide(RE::StaticFunctionTag*) {
        return BattleManager::GetSingleton()->GetPlayerSide();
    }

    RE::BSFixedString GetFactionBattleSide(RE::StaticFunctionTag*, RE::BSFixedString factionId) {
        return BattleManager::GetSingleton()->GetFactionSide(factionId.c_str());
    }

    bool HasPlayerParticipatedInBattle(RE::StaticFunctionTag*) {
        return BattleManager::GetSingleton()->HasPlayerParticipated();
    }

    bool IsBattleFaction(RE::StaticFunctionTag*, RE::BSFixedString factionId) {
        return BattleManager::GetSingleton()->IsBattleFaction(factionId.c_str());
    }

    // ==========================================================================
    // Pending Battle Functions
    // ==========================================================================

    int AddPendingBattle(RE::StaticFunctionTag*, RE::BSFixedString locationName,
                          RE::BSFixedString factionA, RE::BSFixedString factionB,
                          RE::BSFixedString resultJson) {
        return BattleManager::GetSingleton()->AddPendingBattle(
            locationName.c_str(), factionA.c_str(), factionB.c_str(), resultJson.c_str());
    }

    int PollPendingBattles(RE::StaticFunctionTag*) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return -1;
        auto pos = player->GetPosition();
        return BattleManager::GetSingleton()->PollPendingBattles(pos.x, pos.y, pos.z);
    }

    void RemovePendingBattle(RE::StaticFunctionTag*, int id) {
        BattleManager::GetSingleton()->RemovePendingBattle(id);
    }

    void ClearPendingBattles(RE::StaticFunctionTag*) {
        BattleManager::GetSingleton()->ClearPendingBattles();
    }

    RE::BSFixedString GetPendingBattleInfo(RE::StaticFunctionTag*, int id) {
        return RE::BSFixedString(BattleManager::GetSingleton()->GetPendingBattleInfo(id));
    }

    int GetPendingBattleCount(RE::StaticFunctionTag*) {
        return BattleManager::GetSingleton()->GetPendingBattleCount();
    }

    RE::BSFixedString GetLastExpiredBattleResult(RE::StaticFunctionTag*) {
        return RE::BSFixedString(BattleManager::GetSingleton()->GetLastExpiredBattleResult());
    }

    // ==========================================================================
    // JSON Array Helper Functions
    // ==========================================================================

    int GetJsonArrayLength(RE::StaticFunctionTag*, RE::BSFixedString jsonStr, RE::BSFixedString key) {
        try {
            auto j = nlohmann::json::parse(jsonStr.c_str());
            std::string k = key.c_str();
            if (k.empty()) {
                return j.is_array() ? static_cast<int>(j.size()) : 0;
            }
            if (j.contains(k) && j[k].is_array()) {
                return static_cast<int>(j[k].size());
            }
        } catch (...) {}
        return 0;
    }

    RE::BSFixedString GetJsonArrayItem(RE::StaticFunctionTag*, RE::BSFixedString jsonStr,
                                        RE::BSFixedString key, int index) {
        try {
            auto j = nlohmann::json::parse(jsonStr.c_str());
            std::string k = key.c_str();
            const auto& arr = k.empty() ? j : j[k];
            if (arr.is_array() && index >= 0 && index < static_cast<int>(arr.size())) {
                if (arr[index].is_string()) {
                    return RE::BSFixedString(arr[index].get<std::string>());
                }
                return RE::BSFixedString(arr[index].dump());
            }
        } catch (...) {}
        return RE::BSFixedString("");
    }

}  // namespace IntelEngine::Papyrus
