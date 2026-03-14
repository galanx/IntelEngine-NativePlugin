#pragma once

/**
 * Papyrus Native Function Registration
 *
 * Binds C++ implementations to Papyrus native function declarations.
 */

#include "Plugin.h"

namespace IntelEngine::Papyrus {

    constexpr std::string_view SCRIPT_NAME = "IntelEngine";

    /**
     * Register all Papyrus native functions with the VM.
     */
    bool Register(RE::BSScript::IVirtualMachine* a_vm);

    // ==========================================================================
    // NPC Search Functions
    // ==========================================================================

    RE::Actor* FindNPCByName(RE::StaticFunctionTag*, RE::BSFixedString searchTerm);

    RE::Actor* FindNPCByNameNear(RE::StaticFunctionTag*, RE::BSFixedString searchTerm, RE::Actor* nearActor);

    RE::BSFixedString GetNPCCurrentLocation(RE::StaticFunctionTag*, RE::Actor* akNPC);

    bool IsNPCAccessible(RE::StaticFunctionTag*, RE::Actor* akNPC);

    RE::BSFixedString GetNPCNameSuggestion(RE::StaticFunctionTag*, RE::BSFixedString searchTerm);

    // ==========================================================================
    // Actor Location Functions
    // ==========================================================================

    RE::BSFixedString GetActorParentLocationName(RE::StaticFunctionTag*, RE::Actor* actor);

    // ==========================================================================
    // Location Resolution Functions
    // ==========================================================================

    // Named location resolution - returns Cell for Skyrim AI to handle travel
    RE::TESObjectCELL* ResolveLocationToCell(RE::StaticFunctionTag*, RE::BSFixedString locationName);

    // Named location resolution - returns BGSLocation for broader areas
    RE::BGSLocation* ResolveLocationToBGSLocation(RE::StaticFunctionTag*, RE::BSFixedString locationName);

    // Find a door leading to the target (if in current cell)
    RE::TESObjectREFR* FindDoorToLocation(RE::StaticFunctionTag*, RE::BSFixedString locationName);

    // Semantic/relative direction resolution (requires door scanning)
    RE::TESObjectREFR* ResolveSemanticLocation(RE::StaticFunctionTag*,
                                                RE::Actor* akNPC,
                                                RE::BSFixedString semanticTerm);

    // Unified destination resolver — handles semantic terms, compound phrases,
    // and named locations in a single pipeline
    RE::TESObjectREFR* ResolveAnyDestination(RE::StaticFunctionTag*,
                                              RE::Actor* akNPC,
                                              RE::BSFixedString destination);

    RE::BSFixedString GetCellSpatialInfo(RE::StaticFunctionTag*, RE::Actor* akNPC);

    bool IsSemanticTerm(RE::StaticFunctionTag*, RE::BSFixedString term);

    std::vector<RE::BSFixedString> GetAvailableSemanticDirections(RE::StaticFunctionTag*,
                                                                   RE::Actor* akNPC);

    // Get suggestion for failed location search
    RE::BSFixedString GetLocationSuggestion(RE::StaticFunctionTag*, RE::BSFixedString searchTerm);

    // ==========================================================================
    // Action Validation Functions
    // ==========================================================================

    bool ValidateAction(RE::StaticFunctionTag*,
                        RE::Actor* akNPC,
                        RE::BSFixedString actionType,
                        RE::BSFixedString targetParam);

    RE::BSFixedString GetActionFailureReason(RE::StaticFunctionTag*,
                                              RE::Actor* akNPC,
                                              RE::BSFixedString actionType,
                                              RE::BSFixedString targetParam);

    // ==========================================================================
    // String Utility Functions
    // ==========================================================================

    RE::BSFixedString StringToLower(RE::StaticFunctionTag*, RE::BSFixedString text);
    RE::BSFixedString StringToUpper(RE::StaticFunctionTag*, RE::BSFixedString text);
    bool StringContains(RE::StaticFunctionTag*, RE::BSFixedString haystack, RE::BSFixedString needle);
    bool StringStartsWith(RE::StaticFunctionTag*, RE::BSFixedString text, RE::BSFixedString prefix);
    bool StringEndsWith(RE::StaticFunctionTag*, RE::BSFixedString text, RE::BSFixedString suffix);
    int LevenshteinDistance(RE::StaticFunctionTag*, RE::BSFixedString a, RE::BSFixedString b);
    RE::BSFixedString StringTrim(RE::StaticFunctionTag*, RE::BSFixedString text);
    RE::BSFixedString StringEscapeJson(RE::StaticFunctionTag*, RE::BSFixedString text);
    std::vector<RE::BSFixedString> StringSplit(RE::StaticFunctionTag*,
                                                RE::BSFixedString text,
                                                RE::BSFixedString delimiter);

    // ==========================================================================
    // Index Management Functions
    // ==========================================================================

    bool IsIndexLoaded(RE::StaticFunctionTag*);
    RE::BSFixedString GetIndexStats(RE::StaticFunctionTag*);
    void RebuildNPCIndex(RE::StaticFunctionTag*);
    void RebuildLocationIndex(RE::StaticFunctionTag*);

    // ==========================================================================
    // Cell Analysis Functions
    // ==========================================================================

    std::vector<RE::TESObjectREFR*> GetCellDoors(RE::StaticFunctionTag*, RE::Actor* akNPC);
    RE::BSFixedString GetDoorDestination(RE::StaticFunctionTag*, RE::TESObjectREFR* akDoor);
    RE::TESObjectREFR* GetDoorDestinationRef(RE::StaticFunctionTag*, RE::TESObjectREFR* akDoor);
    bool IsDoorExterior(RE::StaticFunctionTag*, RE::TESObjectREFR* akDoor);
    bool IsDoorUpward(RE::StaticFunctionTag*, RE::TESObjectREFR* akDoor);
    bool IsDoorDownward(RE::StaticFunctionTag*, RE::TESObjectREFR* akDoor);

    // ==========================================================================
    // Item Search Functions
    // ==========================================================================

    RE::TESForm* FindItemInInventory(RE::StaticFunctionTag*, RE::Actor* akActor, RE::BSFixedString itemName);
    RE::TESObjectREFR* FindNearbyItemByName(RE::StaticFunctionTag*,
                                             RE::Actor* akActor,
                                             RE::BSFixedString itemName,
                                             float radius);

    // ==========================================================================
    // Distance / Math Utility Functions
    // ==========================================================================

    float GetDistance3D(RE::StaticFunctionTag*, RE::TESObjectREFR* ref1, RE::TESObjectREFR* ref2);
    float GetDistance2D(RE::StaticFunctionTag*, RE::TESObjectREFR* ref1, RE::TESObjectREFR* ref2);
    float CalculateDeadlineFromDistance(RE::StaticFunctionTag*,
                                        RE::TESObjectREFR* source, RE::TESObjectREFR* target,
                                        bool isRoundTrip, float minHours, float maxHours);
    std::vector<float> GetOffsetBehind(RE::StaticFunctionTag*, RE::TESObjectREFR* akRef, float distance);

    // ==========================================================================
    // Time Parsing Functions
    // ==========================================================================

    float ParseTimeCondition(RE::StaticFunctionTag*, RE::BSFixedString condition);
    float CalculateTargetGameTime(RE::StaticFunctionTag*, float targetHour, float currentHour);

    // ==========================================================================
    // Departure Detection Functions
    // ==========================================================================

    int CheckDepartureStatus(RE::StaticFunctionTag*, RE::Actor* akActor, int slot, float threshold);
    void ResetDepartureSlot(RE::StaticFunctionTag*, int slot, RE::Actor* akActor);
    int GetDepartureRetries(RE::StaticFunctionTag*, int slot);

    // ==========================================================================
    // Stuck Detection Functions
    // ==========================================================================

    int CheckStuckStatus(RE::StaticFunctionTag*, RE::Actor* akActor, int slot, float threshold);
    void ResetStuckSlot(RE::StaticFunctionTag*, int slot, RE::Actor* akActor);
    float GetTeleportDistance(RE::StaticFunctionTag*, int slot);
    int GetStuckRecoveryAttempts(RE::StaticFunctionTag*, int slot);

    // ==========================================================================
    // Off-Screen Travel Detection Functions
    // ==========================================================================

    void InitOffScreenTravel(RE::StaticFunctionTag*, int slot,
                             float estimatedArrivalGameTime, RE::Actor* actor);
    int CheckOffScreenProgress(RE::StaticFunctionTag*, int slot,
                               RE::Actor* actor, float currentGameTime);
    void ResetOffScreenSlot(RE::StaticFunctionTag*, int slot);

    // ==========================================================================
    // Waypoint Navigation Functions
    // ==========================================================================

    RE::TESObjectREFR* FindNearestWaypointToward(RE::StaticFunctionTag*,
        RE::Actor* actor, RE::TESObjectREFR* destination, float maxRadius);

    // ==========================================================================
    // Home Door Access Functions (anti-trespass)
    // ==========================================================================

    // Unlock/lock NPC's home door + set cell public/private. Returns door ref.
    RE::TESObjectREFR* SetHomeDoorAccess(RE::StaticFunctionTag*, RE::Actor* akNPC, bool unlock);

    // Same for a specific cell (target NPC's home in fetch/deliver tasks).
    RE::TESObjectREFR* SetHomeDoorAccessForCell(RE::StaticFunctionTag*, int cellFormId, bool unlock);

    // Get the home cell ID that was resolved in the last ResolveAnyDestination call.
    int GetLastResolvedHomeCellId(RE::StaticFunctionTag*);

    // ==========================================================================
    // Slot Tracker Functions (C++ state mirror for SkyrimNet decorators)
    // ==========================================================================

    // Push slot state from Papyrus to C++ SlotTracker.
    // Called by Core.AllocateSlot, Core.SetSlotState.
    void UpdateSlotState(RE::StaticFunctionTag*, int slot, RE::Actor* agent, int newState,
                         RE::BSFixedString taskType, RE::BSFixedString targetName);

    // Clear a slot in C++ SlotTracker.
    // Called by Core.ClearSlot.
    void ClearSlotState(RE::StaticFunctionTag*, int slot);

    // Check if actor is available for new tasks (no active task + no cooldown).
    // Used as SkyrimNet tag via Papyrus wrapper.
    bool IsActorAvailable(RE::StaticFunctionTag*, RE::Actor* akActor);
    bool HasBaseAIPackages(RE::StaticFunctionTag*, RE::Actor* akActor);
    bool HasNonSandboxAI(RE::StaticFunctionTag*, RE::Actor* akActor);
    RE::TESObjectREFR* GetEditorLocationRef(RE::StaticFunctionTag*, RE::Actor* akActor);

    // ==========================================================================
    // Story Engine Functions
    // ==========================================================================

    RE::Actor* GetRandomStoryCandidate(RE::StaticFunctionTag*);
    RE::Actor* GetMemoryDrivenCandidate(RE::StaticFunctionTag*);
    RE::Actor* GetRelatedCandidate(RE::StaticFunctionTag*, RE::Actor* relatedTo);
    RE::BSFixedString GetActorUUID(RE::StaticFunctionTag*, RE::Actor* actor);
    bool IsPlayerInDangerousLocation(RE::StaticFunctionTag*);
    bool HasNearbyDungeonEntrance(RE::StaticFunctionTag*, RE::TESObjectREFR* questLocation);
    bool IsPlayerInOwnHome(RE::StaticFunctionTag*);
    RE::TESObjectREFR* GetPlayerHomeExteriorDoor(RE::StaticFunctionTag*);
    RE::TESObjectREFR* GetPlayerHomeInteriorDoor(RE::StaticFunctionTag*);
    bool IsCivilianClass(RE::StaticFunctionTag*, RE::Actor* actor);
    bool IsJarl(RE::StaticFunctionTag*, RE::Actor* actor);
    bool StoryResponseShouldAct(RE::StaticFunctionTag*, RE::BSFixedString response);
    RE::BSFixedString StoryResponseGetField(RE::StaticFunctionTag*, RE::BSFixedString json,
                                            RE::BSFixedString fieldName);
    RE::BSFixedString BuildActorContextJson(RE::StaticFunctionTag*, RE::Actor* actor,
                                            int slot);
    RE::BSFixedString BuildDungeonMasterContext(RE::StaticFunctionTag*, int maxCandidates,
                                                float absenceDays);

    // NPC-to-NPC interaction context (location-grouped pairs for NPC Social tick)
    RE::BSFixedString BuildNPCInteractionContext(RE::StaticFunctionTag*, int maxPairs);
    RE::BSFixedString BuildNPCInteractionRequestJson(RE::StaticFunctionTag*,
                                                      RE::BSFixedString npcContext);

    // Quest enemy spawning — looks up vanilla leveled ActorBases by EditorID,
    // spawns at location with spread. Returns Actor* array for direct Papyrus use.
    std::vector<RE::Actor*> SpawnQuestEnemies(RE::StaticFunctionTag*, RE::TESObjectREFR* location,
                                        RE::BSFixedString enemyType);

    // Quest chest spawning — creates a container with a specific named item inside.
    // Used by find_item quest sub-type.
    RE::TESObjectREFR* SpawnQuestChest(RE::StaticFunctionTag*, RE::TESObjectREFR* location,
                                        RE::BSFixedString itemName);

    // Validate that an item name exists in the ItemIndex.
    bool ValidateQuestItem(RE::StaticFunctionTag*, RE::BSFixedString itemName);

    // Get a random valuable item name from the ItemIndex (fallback for LLM misses).
    // Excludes recently used items for rotation variety.
    RE::BSFixedString GetRandomQuestItemName(RE::StaticFunctionTag*, int minGoldValue);

    // Notify that a quest item was used (for rotation tracking).
    void NotifyQuestItemUsed(RE::StaticFunctionTag*, RE::BSFixedString itemName);

    // Notify that a rescue victim was used (for rotation tracking).
    void NotifyRescueVictimUsed(RE::StaticFunctionTag*, RE::BSFixedString victimName);

    // Notify that a quest location was used (for rotation tracking).
    void NotifyQuestLocationUsed(RE::StaticFunctionTag*, RE::BSFixedString locationName);

    // Quest boss spawning — spawns a boss-tier leveled actor near a location.
    RE::Actor* SpawnQuestBoss(RE::StaticFunctionTag*, RE::TESObjectREFR* location,
                              RE::BSFixedString enemyType);

    // Find a deeper spawn point in an interior cell by scanning for dungeon landmarks
    // (word walls, boss chests, coffins, shrines) then falling back to door traversal.
    RE::TESObjectREFR* FindDeeperSpawnPoint(RE::StaticFunctionTag*, RE::Actor* actor);

    // Scan current interior cell for prisoner furniture (shackles, cages, stocks).
    // Returns the highest-priority prisoner furniture ref, or nullptr if none found.
    // Includes Statics/Activators (cage meshes) — use for positioning near props.
    RE::TESObjectREFR* FindPrisonerFurniture(RE::StaticFunctionTag*, RE::Actor* actor);

    // Scan current interior cell for USABLE prisoner furniture (FormType::Furniture only).
    // Only returns objects NPCs can actually sit in (shackles, stocks with idle markers).
    // Excludes Statics/Activators (cage meshes, doors). Use for Activate() path.
    RE::TESObjectREFR* FindUsablePrisonerFurniture(RE::StaticFunctionTag*, RE::Actor* actor);

    // Deep rescue anchor — scans current cell + cells behind doors for prisoner
    // furniture or landmarks. Used for rescue sub-type victim placement.
    RE::TESObjectREFR* FindRescueAnchor(RE::StaticFunctionTag*, RE::Actor* actor);

    // Scan cells AHEAD of the player (through doors, not current cell) for prisoner
    // furniture or landmarks. Returns an anchor in the next cell — invisible to player.
    // Used by the dungeon depth-tracking fallback to find placement as player explores.
    RE::TESObjectREFR* ScanAheadForAnchor(RE::StaticFunctionTag*, RE::Actor* actor);

    // Get the boss room anchor for a dungeon location (from DungeonIndex).
    // Returns a persistent ref deep inside the dungeon, accessible even unloaded.
    RE::TESObjectREFR* GetDungeonBossAnchor(RE::StaticFunctionTag*, RE::BSFixedString locationName);

    // Check if the specific quest item is still inside a container.
    bool IsQuestItemInChest(RE::StaticFunctionTag*, RE::TESObjectREFR* container,
                            RE::BSFixedString itemName);

    // ==========================================================================
    // MemoryDB Functions (SkyrimNet SQLite reader)
    // ==========================================================================

    RE::BSFixedString GetNPCMemories(RE::StaticFunctionTag*, RE::Actor* akActor, int maxCount);
    RE::BSFixedString GetRecentWorldEvents(RE::StaticFunctionTag*, int maxCount, RE::BSFixedString eventTypeFilter);
    RE::BSFixedString GetActiveStoryNPCs(RE::StaticFunctionTag*, int maxCount);
    RE::BSFixedString GetNPCRelationshipSummary(RE::StaticFunctionTag*, RE::Actor* akActor1, RE::Actor* akActor2);
    RE::BSFixedString IsMemoryDBConnected(RE::StaticFunctionTag*);
    int GetPlayerInteractionCount(RE::StaticFunctionTag*, RE::Actor* actor);

    // ==========================================================================
    // Dialogue Safety Net Functions
    // ==========================================================================

    // Tick-based check: queries MemoryDB for new dialogue, runs keyword matching.
    // Returns keyword hint (0=nothing, 1=meeting, 2=fetch, 3=delivery).
    // Stores the NPC internally — retrieve with GetSafetyNetNPC().
    int RunSafetyNetCheck(RE::StaticFunctionTag*);

    // Signal that new dialogue occurred — re-enables safety net polling.
    void NotifyNewDialogue(RE::StaticFunctionTag*);

    // Returns the NPC from the last positive RunSafetyNetCheck() call.
    RE::Actor* GetSafetyNetNPC(RE::StaticFunctionTag*);

    RE::Actor* GetLastConversationPartner(RE::StaticFunctionTag*);
    RE::BSFixedString GetRecentDialogue(RE::StaticFunctionTag*, RE::Actor* npc, int maxExchanges);
    int HasScheduleKeywords(RE::StaticFunctionTag*, RE::Actor* npc);

    // Build JSON context for safety net LLM prompt (all values properly escaped).
    RE::BSFixedString BuildSafetyNetContextJson(RE::StaticFunctionTag*,
                                                 RE::Actor* npc, int keywordHint);

    // Build JSON request for Story DM prompt. dmContext is pre-escaped from BuildDungeonMasterContext;
    // excludedTypes is escaped here.
    RE::BSFixedString BuildStoryDMRequestJson(RE::StaticFunctionTag*,
                                               RE::BSFixedString dmContext,
                                               RE::BSFixedString excludedTypes);

    // ==========================================================================
    // Dashboard Config Functions
    // ==========================================================================

    // Notify the dashboard UI that slot data changed (triggers JS refresh)
    void NotifyDashboardSlotChanged(RE::StaticFunctionTag*);

    // Get the current dashboard hotkey VK code (-1 = disabled)
    int GetDashboardHotkey(RE::StaticFunctionTag*);

    // Set the dashboard hotkey VK code and persist to settings.yaml
    bool SetDashboardHotkey(RE::StaticFunctionTag*, int vkCode);

    // Hot-reload the dashboard UI from disk (destroy + recreate PrismaUI view)
    void ReloadDashboardUI(RE::StaticFunctionTag*);

    // Re-read hotkey config from settings.yaml without touching the view
    void ReloadDashboardConfig(RE::StaticFunctionTag*);

    // Push comprehensive dashboard state JSON to the PrismaUI frontend
    void PushDashboardFullState(RE::StaticFunctionTag*, RE::BSFixedString json);

    // Check if dashboard is currently visible
    bool IsDashboardOpen(RE::StaticFunctionTag*);

    // ==========================================================================
    // Faction Politics Functions
    // ==========================================================================

    /** Get relation score (-100 to +100) between two factions. */
    int GetFactionRelation(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB);

    /** Adjust relation between factions by delta. Returns new score. */
    int AdjustFactionRelation(RE::StaticFunctionTag*, RE::BSFixedString factionA,
                               RE::BSFixedString factionB, int delta);

    /** Get player standing with a faction (-100 to +100). */
    int GetPlayerFactionStanding(RE::StaticFunctionTag*, RE::BSFixedString factionId);

    /** Adjust player standing with a faction by delta. Returns new standing. */
    int AdjustPlayerFactionStanding(RE::StaticFunctionTag*, RE::BSFixedString factionId, int delta);

    /** Check if two factions are at war (score below war threshold). */
    bool IsFactionAtWar(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB);

    /** Get queryFaction's morale in an active war between factionA and factionB. Returns -1 if no war. */
    int GetWarMorale(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB, RE::BSFixedString queryFaction);

    /** Get human-readable relation status string (Alliance/Friendly/Neutral/Tense/Hostile/War). */
    RE::BSFixedString GetRelationStatus(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB);

    /** Build full political context JSON for the Political DM prompt. */
    RE::BSFixedString BuildPoliticalContext(RE::StaticFunctionTag*, float currentGameTime);

    /** Build compact political dashboard JSON for PrismaUI. */
    RE::BSFixedString BuildPoliticalDashboardJson(RE::StaticFunctionTag*);

    /** Record a political event. Returns event ID (-1 on failure). */
    int RecordPoliticalEvent(RE::StaticFunctionTag*, RE::BSFixedString factionA,
                              RE::BSFixedString factionB, RE::BSFixedString eventType,
                              RE::BSFixedString description, int relationDelta, float gameTime);

    /** Check if the faction politics system is enabled. */
    bool IsPoliticsEnabled(RE::StaticFunctionTag*);

    /** Get the politics tick interval in game hours. */
    int GetPoliticsTickInterval(RE::StaticFunctionTag*);

    /** Hot-reload factions.yaml config. */
    void ReloadFactionConfig(RE::StaticFunctionTag*);

    /** Get loaded Actor references for faction leaders (for fact injection). */
    std::vector<RE::Actor*> GetFactionLeaderActors(RE::StaticFunctionTag*, RE::BSFixedString factionId);

    /** Get FormIDs of faction leaders (works even if actors aren't loaded). */
    std::vector<int> GetFactionLeaderFormIds(RE::StaticFunctionTag*, RE::BSFixedString factionId);

    /** Check if a political event should physically manifest near the player.
     *  Returns JSON spawn instructions or empty string. */
    RE::BSFixedString CheckEventManifestation(RE::StaticFunctionTag*,
        RE::BSFixedString factionA, RE::BSFixedString factionB, RE::BSFixedString eventType);

    /** Confirm manifestation cooldown after Papyrus verified actors spawned. */
    void ConfirmManifestationCooldown(RE::StaticFunctionTag*);

    /** Parse LLM player standing response JSON and apply standing changes. Returns count applied. */
    int ApplyPlayerStandingChanges(RE::StaticFunctionTag*, RE::BSFixedString responseJson);

    /** Process player conduct with cross-faction consequences. Returns standings changed (1-2). */
    int ProcessPlayerConduct(RE::StaticFunctionTag*, RE::Actor* reporter,
                              RE::BSFixedString factionId, RE::BSFixedString sentiment,
                              RE::BSFixedString reason);

    /** Check crime gold against political factions, apply standing penalties for increases. Returns count changed. */
    int CheckCrimeGoldStandings(RE::StaticFunctionTag*);

    /** Decay all non-zero player standings by decayRate toward 0. Returns count decayed. */
    int DecayPlayerStandings(RE::StaticFunctionTag*, int decayRate);

    /** Write political_state.json for pull-based NPC awareness. Call after standing changes. */
    void WritePoliticalStateFile(RE::StaticFunctionTag*);

    /** Set politics enabled/disabled at runtime. */
    void SetPoliticsEnabled(RE::StaticFunctionTag*, bool enabled);

    /** Set politics tick interval (hours) at runtime. */
    void SetPoliticsTickInterval(RE::StaticFunctionTag*, int hours);

    /** Declare war between two factions. Returns war ID or -1. */
    int DeclareWar(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB, float gameTime);

    /** Process war tick: morale decay, surrender checks. Returns JSON of updates. */
    RE::BSFixedString ProcessWarTick(RE::StaticFunctionTag*, float gameTime);

    /** End a specific war with a victor. */
    bool EndFactionWar(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB,
                       RE::BSFixedString victor, float gameTime);

    /** Get number of active wars. */
    int GetActiveWarCount(RE::StaticFunctionTag*);

    /** Get war strength for a faction in an active war. */
    int GetWarStrength(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB,
                       RE::BSFixedString queryFaction);

    /** Record off-screen battle result. Returns battle ID or -1. */
    int RecordOffScreenBattle(RE::StaticFunctionTag*, RE::BSFixedString factionA, RE::BSFixedString factionB,
                              RE::BSFixedString location, RE::BSFixedString result, RE::BSFixedString narrative,
                              int attackerLosses, int defenderLosses, RE::BSFixedString victor);

    // ==========================================================================
    // Debug Functions
    // ==========================================================================

    void TestNPCSearch(RE::StaticFunctionTag*, RE::BSFixedString searchTerm);
    void TestLocationResolve(RE::StaticFunctionTag*, RE::BSFixedString locationName);
    void TestSemanticResolve(RE::StaticFunctionTag*, RE::Actor* akNPC, RE::BSFixedString term);
    void TestValidation(RE::StaticFunctionTag*, RE::BSFixedString actionType, RE::BSFixedString target);
    void SetDebugLevel(RE::StaticFunctionTag*, int level);
    RE::BSFixedString GetVersion(RE::StaticFunctionTag*);

}  // namespace IntelEngine::Papyrus
