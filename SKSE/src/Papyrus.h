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
    // Debug Functions
    // ==========================================================================

    void TestNPCSearch(RE::StaticFunctionTag*, RE::BSFixedString searchTerm);
    void TestLocationResolve(RE::StaticFunctionTag*, RE::BSFixedString locationName);
    void TestSemanticResolve(RE::StaticFunctionTag*, RE::Actor* akNPC, RE::BSFixedString term);
    void TestValidation(RE::StaticFunctionTag*, RE::BSFixedString actionType, RE::BSFixedString target);
    void SetDebugLevel(RE::StaticFunctionTag*, int level);
    RE::BSFixedString GetVersion(RE::StaticFunctionTag*);

}  // namespace IntelEngine::Papyrus
