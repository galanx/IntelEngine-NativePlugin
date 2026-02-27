/**
 * Papyrus Native Function Implementation
 *
 * Implements all native functions exposed to Papyrus scripts.
 */

#include "Papyrus.h"
#include "NPCIndex.h"
#include <random>
#include <algorithm>
#include <chrono>
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

namespace IntelEngine::Papyrus {

    // Forward declarations for functions defined after Register()
    RE::Actor* ResolveStoryCandidate(RE::StaticFunctionTag*, RE::BSFixedString);
    RE::Actor* FindMessengerForSender(RE::StaticFunctionTag*, RE::Actor*);
    void NotifyStoryCooldown(RE::StaticFunctionTag*, RE::Actor*, float);
    void NotifyStoryTypePicked(RE::StaticFunctionTag*, RE::BSFixedString);
    std::vector<int> GetDMCandidatePoolFormIDs(RE::StaticFunctionTag*);
    RE::BSFixedString RenderFactsSection(RE::StaticFunctionTag*, std::vector<RE::BSFixedString>, std::vector<float>, float);
    RE::BSFixedString RenderGossipHeardSection(RE::StaticFunctionTag*, std::vector<RE::BSFixedString>, std::vector<RE::BSFixedString>, std::vector<float>, float);
    RE::BSFixedString RenderGossipToldSection(RE::StaticFunctionTag*, std::vector<RE::BSFixedString>, std::vector<RE::BSFixedString>, std::vector<float>, float);
    RE::BSFixedString RenderTaskHistorySection(RE::StaticFunctionTag*, std::vector<RE::BSFixedString>, std::vector<float>, float);
    void SetDangerZonePolicy(RE::StaticFunctionTag*, bool, bool);
    bool IsPlayerInBlockedLocation(RE::StaticFunctionTag*);

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
        a_vm->RegisterFunction("IsPlayerInOwnHome", SCRIPT_NAME, IsPlayerInOwnHome); ++count;
        a_vm->RegisterFunction("GetPlayerHomeExteriorDoor", SCRIPT_NAME, GetPlayerHomeExteriorDoor); ++count;
        a_vm->RegisterFunction("GetPlayerHomeInteriorDoor", SCRIPT_NAME, GetPlayerHomeInteriorDoor); ++count;
        a_vm->RegisterFunction("IsCivilianClass", SCRIPT_NAME, IsCivilianClass); ++count;
        a_vm->RegisterFunction("IsJarl", SCRIPT_NAME, IsJarl); ++count;
        a_vm->RegisterFunction("SetDangerZonePolicy", SCRIPT_NAME, SetDangerZonePolicy); ++count;
        a_vm->RegisterFunction("IsPlayerInBlockedLocation", SCRIPT_NAME, IsPlayerInBlockedLocation); ++count;
        a_vm->RegisterFunction("StoryResponseShouldAct", SCRIPT_NAME, StoryResponseShouldAct); ++count;
        a_vm->RegisterFunction("StoryResponseGetField", SCRIPT_NAME, StoryResponseGetField); ++count;
        a_vm->RegisterFunction("BuildActorContextJson", SCRIPT_NAME, BuildActorContextJson); ++count;
        a_vm->RegisterFunction("BuildDungeonMasterContext", SCRIPT_NAME, BuildDungeonMasterContext); ++count;
        a_vm->RegisterFunction("BuildNPCInteractionContext", SCRIPT_NAME, BuildNPCInteractionContext); ++count;
        a_vm->RegisterFunction("BuildNPCInteractionRequestJson", SCRIPT_NAME, BuildNPCInteractionRequestJson); ++count;
        a_vm->RegisterFunction("NotifyStoryCooldown", SCRIPT_NAME, NotifyStoryCooldown); ++count;
        a_vm->RegisterFunction("NotifyStoryTypePicked", SCRIPT_NAME, NotifyStoryTypePicked); ++count;
        a_vm->RegisterFunction("GetDMCandidatePoolFormIDs", SCRIPT_NAME, GetDMCandidatePoolFormIDs); ++count;
        a_vm->RegisterFunction("SpawnQuestEnemies", SCRIPT_NAME, SpawnQuestEnemies); ++count;

        // MemoryDB Functions (SkyrimNet SQLite reader)
        a_vm->RegisterFunction("GetNPCMemories", SCRIPT_NAME, GetNPCMemories); ++count;
        a_vm->RegisterFunction("GetRecentWorldEvents", SCRIPT_NAME, GetRecentWorldEvents); ++count;
        a_vm->RegisterFunction("GetActiveStoryNPCs", SCRIPT_NAME, GetActiveStoryNPCs); ++count;
        a_vm->RegisterFunction("GetNPCRelationshipSummary", SCRIPT_NAME, GetNPCRelationshipSummary); ++count;
        a_vm->RegisterFunction("IsMemoryDBConnected", SCRIPT_NAME, IsMemoryDBConnected); ++count;

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
        return NPCIndex::GetSingleton()->FindByName(searchTerm.c_str());
    }

    RE::Actor* FindNPCByNameNear(RE::StaticFunctionTag*, RE::BSFixedString searchTerm, RE::Actor* nearActor) {
        return NPCIndex::GetSingleton()->FindByNameNear(searchTerm.c_str(), nearActor);
    }

    RE::Actor* ResolveStoryCandidate(RE::StaticFunctionTag*, RE::BSFixedString name) {
        return NPCIndex::GetSingleton()->ResolveStoryCandidate(name.c_str());
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
        return NPCIndex::GetSingleton()->GetSuggestion(searchTerm.c_str());
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

    void SetDangerZonePolicy(RE::StaticFunctionTag*, bool blockCivilians, bool blockAll) {
        NPCIndex::GetSingleton()->SetDangerZonePolicy(blockCivilians, blockAll);
    }

    bool IsPlayerInBlockedLocation(RE::StaticFunctionTag*) {
        return NPCIndex::IsPlayerInBlockedLocation();
    }

    bool StoryResponseShouldAct(RE::StaticFunctionTag*, RE::BSFixedString response) {
        std::string_view sv(response.c_str());
        bool result = sv.find("\"should_act\":true") != std::string_view::npos ||
                      sv.find("\"should_act\": true") != std::string_view::npos;
        logger::info("[StoryDM] ShouldAct={} — input len={}", result, sv.size());
        return result;
    }

    RE::BSFixedString StoryResponseGetField(RE::StaticFunctionTag*, RE::BSFixedString json,
                                            RE::BSFixedString fieldName) {
        std::string_view sv(json.c_str());
        // BSFixedString is case-insensitive: "npc" may become "NPC" if the engine
        // already stored that casing.  We must also handle camelCase keys like
        // "msgContent" that get lowercased.  Solution: lowercase BOTH the JSON
        // (for searching) and the field name, but extract values from the original.
        std::string jsonLower(sv);
        std::transform(jsonLower.begin(), jsonLower.end(), jsonLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        std::string field(fieldName.c_str());
        std::transform(field.begin(), field.end(), field.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::string needle = "\"" + field + "\":";
        auto pos = jsonLower.find(needle);
        if (pos == std::string::npos) {
            // Try with space after colon
            needle = "\"" + field + "\": ";
            pos = jsonLower.find(needle);
        }
        if (pos == std::string::npos) {
            logger::warn("[StoryDM] GetField('{}') NOT FOUND — input len={}, first80='{}'",
                         fieldName.c_str(), sv.size(),
                         sv.size() > 80 ? sv.substr(0, 80) : sv);
            return "";
        }

        // Find opening quote of value (use original sv for value extraction)
        auto quoteStart = sv.find('"', pos + needle.size());
        if (quoteStart == std::string_view::npos) return "";

        // Find closing quote (handle escaped quotes)
        auto i = quoteStart + 1;
        while (i < sv.size()) {
            if (sv[i] == '"' && (i == 0 || sv[i - 1] != '\\')) break;
            ++i;
        }
        if (i >= sv.size()) return "";

        auto value = sv.substr(quoteStart + 1, i - quoteStart - 1);
        return RE::BSFixedString(std::string(value).c_str());
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
        std::string json = "{";
        json += "\"npcPairPool\":\"" + std::string(npcContext.c_str()) + "\"}";
        return RE::BSFixedString(json);
    }

    void NotifyStoryCooldown(RE::StaticFunctionTag*, RE::Actor* akActor, float gameTime) {
        if (!akActor) return;
        NPCIndex::GetSingleton()->NotifyStoryCooldown(akActor->GetFormID(), gameTime);
    }

    void NotifyStoryTypePicked(RE::StaticFunctionTag*, RE::BSFixedString storyType) {
        std::string type(storyType.c_str());
        if (!type.empty()) {
            NPCIndex::GetSingleton()->NotifyStoryTypePicked(type);
        }
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
            "ambush", "stalker", "message", "quest"
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

        // Reverse order (most recent first), matching template behavior
        for (int i = static_cast<int>(facts.size()) - 1; i >= 0; --i) {
            auto idx = static_cast<size_t>(i);
            std::string timeLabel = "some time ago";
            if (idx < factTimes.size()) {
                timeLabel = FormatRelativeTimeFromDays(currentGameDays, factTimes[idx]);
            }
            result += "- I ";
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

        // Reverse order (most recent first)
        for (int i = static_cast<int>(descs.size()) - 1; i >= 0; --i) {
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

    static std::mt19937 s_rng{std::random_device{}()};

    // Exact-match cache (EditorID → single form)
    static std::unordered_map<std::string, RE::TESBoundObject*> s_leveledActorCache;
    // Prefix-match cache (EditorID → list of matching forms for random selection)
    static std::unordered_map<std::string, std::vector<RE::TESBoundObject*>> s_prefixMatchCache;

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
    };

    static RE::TESBoundObject* LookupLeveledActor(const char* editorID) {
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
            size_t prefixLen = strlen(editorID);
            std::vector<RE::TESBoundObject*> matches;
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
        std::uniform_real_distribution<float> spreadDist(-300.0f, 300.0f);

        int count = countDist(s_rng);
        auto basePos = location->GetPosition();

        for (int i = 0; i < count; ++i) {
            auto* baseToSpawn = (secondaryBase && i % 2 == 1) ? secondaryBase : primaryBase;

            auto spawned = location->PlaceObjectAtMe(baseToSpawn, true);
            if (spawned) {
                float sx = spreadDist(s_rng);
                float sy = spreadDist(s_rng);
                RE::NiPoint3 newPos{basePos.x + sx, basePos.y + sy, basePos.z};
                spawned->SetPosition(newPos);

                auto* actor = spawned->As<RE::Actor>();
                if (actor) {
                    result.push_back(actor);
                }
                logger::info("[IntelEngine] SpawnQuestEnemies: spawned '{}' at ({:.0f}, {:.0f})",
                            baseToSpawn->GetName(), newPos.x, newPos.y);
            }
        }

        logger::info("[IntelEngine] SpawnQuestEnemies: {} {} spawned at {}",
                    result.size(), type, location->GetName());
        return result;
    }

}  // namespace IntelEngine::Papyrus
