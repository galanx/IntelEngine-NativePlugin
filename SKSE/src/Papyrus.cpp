/**
 * Papyrus Native Function Implementation
 *
 * Implements all native functions exposed to Papyrus scripts.
 */

#include "Papyrus.h"
#include "NPCIndex.h"
#include "LocationResolver.h"
#include "StringUtils.h"
#include "CellAnalyzer.h"
#include "ActionValidator.h"
#include "DepartureDetector.h"
#include "StuckDetector.h"
#include "OffScreenTracker.h"
#include "Settings.h"

namespace IntelEngine::Papyrus {

    bool Register(RE::BSScript::IVirtualMachine* a_vm) {
        if (!a_vm) {
            return false;
        }

        int count = 0;

        // NPC Search Functions
        a_vm->RegisterFunction("FindNPCByName", SCRIPT_NAME, FindNPCByName); ++count;
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

        // Helper: compute relative hour offset from current time, wrapped at 24
        auto relativeHour = [](float offset) -> float {
            auto* cal = RE::Calendar::GetSingleton();
            if (!cal) return -1.0f;
            float target = cal->GetHour() + offset;
            while (target >= 24.0f) target -= 24.0f;
            return target;
        };

        // --- Relative time patterns ---
        if (input.find("soon") != std::string::npos ||
            input.find("shortly") != std::string::npos ||
            input.find("a moment") != std::string::npos ||
            input.find("right away") != std::string::npos ||
            input.find("immediately") != std::string::npos) {
            return relativeHour(0.25f);
        }

        if (input.find("half an hour") != std::string::npos ||
            input.find("30 minute") != std::string::npos) {
            return relativeHour(0.5f);
        }

        if (input.find("few hour") != std::string::npos ||
            input.find("couple hour") != std::string::npos ||
            input.find("couple of hour") != std::string::npos) {
            return relativeHour(2.0f);
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
                return relativeHour(hp.hours);
            }
        }

        // "1 hour" / "an hour" / "one hour" — after multi-digit patterns
        if (input.find("1 hour") != std::string::npos ||
            input.find("an hour") != std::string::npos ||
            input.find("one hour") != std::string::npos) {
            return relativeHour(1.0f);
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
                return nt.hour;
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
                return static_cast<float>(cp.hour);
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
                return static_cast<float>(cp.hour);
            }
        }

        // Nothing matched
        logger::debug("ParseTimeCondition: could not parse '{}'", condition.c_str());
        return -1.0f;
    }

    float CalculateTargetGameTime(RE::StaticFunctionTag*, float targetHour, float currentHour) {
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar) return 0.0f;

        float currentGameTime = calendar->GetCurrentGameTime();
        float dayPart = std::floor(currentGameTime);
        float targetDayFraction = targetHour / 24.0f;
        float targetTime = dayPart + targetDayFraction;

        // If target hour is before or at current hour, it means tomorrow
        if (targetHour <= currentHour) {
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

}  // namespace IntelEngine::Papyrus
