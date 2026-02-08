/**
 * Cell Analyzer Implementation
 *
 * Analyzes cell geometry and door connections.
 */

#include "CellAnalyzer.h"
#include "StringUtils.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace IntelEngine {

    std::vector<RE::TESObjectREFR*> CellAnalyzer::GetDoors(RE::Actor* actor) {
        std::vector<RE::TESObjectREFR*> doors;

        if (!actor) return doors;

        auto* cell = actor->GetParentCell();
        if (!cell) return doors;

        // Iterate through all references in the cell
        // Note: CommonLibSSE-NG ForEachReference takes TESObjectREFR& (reference), not pointer
        cell->ForEachReference([&doors](RE::TESObjectREFR& ref) {
            // Check if it's a door
            auto* baseObj = ref.GetBaseObject();
            if (!baseObj) return RE::BSContainer::ForEachResult::kContinue;

            if (baseObj->Is(RE::FormType::Door)) {
                // Skip if disabled
                if (!ref.IsDisabled()) {
                    doors.push_back(&ref);
                }
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        return doors;
    }

    RE::TESObjectREFR* CellAnalyzer::GetDoorDestination(RE::TESObjectREFR* door) {
        if (!door) return nullptr;

        // Get the teleport link
        auto* extraTeleport = door->extraList.GetByType<RE::ExtraTeleport>();
        if (!extraTeleport || !extraTeleport->teleportData) {
            return nullptr;
        }

        return extraTeleport->teleportData->linkedDoor.get().get();
    }

    RE::BSFixedString CellAnalyzer::GetDoorDestinationName(RE::TESObjectREFR* door) {
        if (!door) return "";

        auto* destDoor = GetDoorDestination(door);
        if (!destDoor) {
            // No teleport data, check if it has a name directly
            auto doorName = door->GetName();
            if (doorName && strlen(doorName) > 0) {
                return doorName;
            }
            return "Unknown";
        }

        auto* destCell = destDoor->GetParentCell();
        if (!destCell) return "Unknown";

        auto cellName = destCell->GetName();
        if (cellName && strlen(cellName) > 0) {
            return cellName;
        }

        auto editorId = destCell->GetFormEditorID();
        if (editorId && strlen(editorId) > 0) {
            return editorId;
        }

        return "Unknown";
    }

    bool CellAnalyzer::IsDoorExterior(RE::TESObjectREFR* door) {
        if (!door) return false;

        auto* destDoor = GetDoorDestination(door);
        if (!destDoor) return false;

        auto* destCell = destDoor->GetParentCell();
        if (!destCell) return false;

        return !destCell->IsInteriorCell();
    }

    bool CellAnalyzer::IsDoorUpward(RE::TESObjectREFR* door) {
        if (!door) return false;

        auto* destDoor = GetDoorDestination(door);
        if (!destDoor) return false;

        float currentZ = door->GetPositionZ();
        float destZ = destDoor->GetPositionZ();

        // Minimum 100-unit Z delta to count as a floor change.
        // Prevents false positives from ramps, balcony doors, and slight elevation changes.
        return (destZ - currentZ) > 100.0f;
    }

    bool CellAnalyzer::IsDoorDownward(RE::TESObjectREFR* door) {
        if (!door) return false;

        auto* destDoor = GetDoorDestination(door);
        if (!destDoor) return false;

        float currentZ = door->GetPositionZ();
        float destZ = destDoor->GetPositionZ();

        // Minimum 100-unit Z delta to count as a floor change.
        return (currentZ - destZ) > 100.0f;
    }

    bool CellAnalyzer::IsDoorNameUpward(RE::TESObjectREFR* door) {
        if (!door) return false;

        auto destName = GetDoorDestinationName(door);
        std::string lowerDest = StringUtils::ToLowerStd(destName.c_str());

        auto doorName = door->GetName();
        std::string lowerDoor = doorName ? StringUtils::ToLowerStd(doorName) : "";

        return StringUtils::ContainsAny(lowerDest, {"upstairs", "upper", "second floor", "top floor", "upper level", "attic"}) ||
               StringUtils::ContainsAny(lowerDoor, {"upstairs", "upper", "second floor", "top floor", "upper level", "attic"});
    }

    bool CellAnalyzer::IsDoorNameDownward(RE::TESObjectREFR* door) {
        if (!door) return false;

        auto destName = GetDoorDestinationName(door);
        std::string lowerDest = StringUtils::ToLowerStd(destName.c_str());

        auto doorName = door->GetName();
        std::string lowerDoor = doorName ? StringUtils::ToLowerStd(doorName) : "";

        return StringUtils::ContainsAny(lowerDest, {"downstairs", "lower", "cellar", "basement", "dungeon", "below", "crypt", "undercroft"}) ||
               StringUtils::ContainsAny(lowerDoor, {"downstairs", "lower", "cellar", "basement", "dungeon", "below", "crypt", "undercroft"});
    }

    std::string CellAnalyzer::GetDirection(RE::Actor* actor, RE::TESObjectREFR* target) {
        if (!actor || !target) return "unknown";

        auto actorPos = actor->GetPosition();
        auto targetPos = target->GetPosition();

        float dx = targetPos.x - actorPos.x;
        float dy = targetPos.y - actorPos.y;

        // Calculate angle
        float angle = std::atan2(dy, dx) * 180.0f / 3.14159f;

        // Convert to compass direction
        // North is typically +Y in Skyrim
        if (angle >= -22.5f && angle < 22.5f) return "east";
        if (angle >= 22.5f && angle < 67.5f) return "northeast";
        if (angle >= 67.5f && angle < 112.5f) return "north";
        if (angle >= 112.5f && angle < 157.5f) return "northwest";
        if (angle >= 157.5f || angle < -157.5f) return "west";
        if (angle >= -157.5f && angle < -112.5f) return "southwest";
        if (angle >= -112.5f && angle < -67.5f) return "south";
        if (angle >= -67.5f && angle < -22.5f) return "southeast";

        return "unknown";
    }

    std::string CellAnalyzer::ResolveCellName(RE::Actor* actor, RE::TESObjectCELL* cell) {
        // 1. Try cell display name
        auto cellName = cell->GetName();
        if (cellName && strlen(cellName) > 0) {
            return cellName;
        }

        // 2. Fallback: actor's BGSLocation name (works for modded interiors in exterior cells)
        auto* location = actor->GetCurrentLocation();
        if (location) {
            auto locName = location->GetFullName();
            if (locName && strlen(locName) > 0) {
                return locName;
            }
        }

        // 3. Fallback: cell editor ID
        auto editorId = cell->GetFormEditorID();
        if (editorId && strlen(editorId) > 0) {
            return editorId;
        }

        // 4. Fallback: worldspace name (for exterior cells)
        if (!cell->IsInteriorCell()) {
            auto* worldspace = cell->GetRuntimeData().worldSpace;
            if (worldspace) {
                auto wsName = worldspace->GetFullName();
                if (wsName && strlen(wsName) > 0) {
                    return wsName;
                }
            }
        }

        return "Unknown";
    }

    void CellAnalyzer::EnsureKeywordsCached() {
        if (m_keywordsCached) return;
        m_keywordsCached = true;

        static const char* editorIDs[] = {
            "LocTypeInn", "LocTypeDwelling", "LocTypeStore",
            "LocTypeHouse", "LocTypeTemple"
        };

        for (auto* editorID : editorIDs) {
            auto* form = RE::TESForm::LookupByEditorID(editorID);
            if (form) {
                auto* keyword = form->As<RE::BGSKeyword>();
                if (keyword) {
                    m_interiorKeywords.push_back(keyword);
                }
            }
        }

        logger::debug("CellAnalyzer: Cached {} interior keywords", m_interiorKeywords.size());
    }

    bool CellAnalyzer::IsEffectivelyInterior(RE::Actor* actor, RE::TESObjectCELL* cell) {
        // 1. Actual interior cell
        if (cell->IsInteriorCell()) {
            return true;
        }

        // 2. Check if actor is in a BGSLocation with indoor keywords
        //    (for mods that place building interiors in exterior worldspace cells)
        auto* location = actor->GetCurrentLocation();
        if (location) {
            EnsureKeywordsCached();
            for (auto* keyword : m_interiorKeywords) {
                if (location->HasKeyword(keyword)) {
                    return true;
                }
            }
        }

        return false;
    }

    bool CellAnalyzer::IsBedFurniture(RE::TESObjectREFR* ref) {
        if (!ref) return false;

        auto* baseObj = ref->GetBaseObject();
        if (!baseObj || !baseObj->Is(RE::FormType::Furniture)) return false;

        auto containsBedKeyword = [](const std::string& str) {
            return str.find("bed") != std::string::npos ||
                   str.find("hammock") != std::string::npos ||
                   str.find("sleeping") != std::string::npos;
        };

        // Check display name
        auto* name = baseObj->GetName();
        if (name && strlen(name) > 0) {
            std::string lower = StringUtils::ToLowerStd(name);
            if (containsBedKeyword(lower)) return true;
        }

        // Check editor ID (more reliable for generic/unnamed furniture)
        auto editorId = baseObj->GetFormEditorID();
        if (editorId && strlen(editorId) > 0) {
            std::string lower = StringUtils::ToLowerStd(editorId);
            if (containsBedKeyword(lower)) return true;
        }

        return false;
    }

    bool CellAnalyzer::IsCookingStation(RE::TESObjectREFR* ref) {
        if (!ref) return false;

        auto* baseObj = ref->GetBaseObject();
        if (!baseObj) return false;

        // Cooking stations can be Furniture or Activator types
        if (!baseObj->Is(RE::FormType::Furniture) && !baseObj->Is(RE::FormType::Activator))
            return false;

        auto containsCookingKeyword = [](const std::string& str) {
            return str.find("cooking") != std::string::npos ||
                   str.find("cook") != std::string::npos ||
                   str.find("oven") != std::string::npos ||
                   str.find("spit") != std::string::npos ||
                   str.find("cauldron") != std::string::npos;
        };

        // Check display name
        auto* name = baseObj->GetName();
        if (name && strlen(name) > 0) {
            std::string lower = StringUtils::ToLowerStd(name);
            if (containsCookingKeyword(lower)) return true;
        }

        // Check editor ID
        auto editorId = baseObj->GetFormEditorID();
        if (editorId && strlen(editorId) > 0) {
            std::string lower = StringUtils::ToLowerStd(editorId);
            if (containsCookingKeyword(lower)) return true;
        }

        return false;
    }

    std::vector<RE::TESObjectREFR*> CellAnalyzer::FindCookingStations(RE::Actor* actor) {
        std::vector<RE::TESObjectREFR*> results;
        if (!actor) return results;

        auto* cell = actor->GetParentCell();
        if (!cell) return results;

        cell->ForEachReference([&results, this](RE::TESObjectREFR& ref) {
            if (ref.IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;

            if (IsCookingStation(&ref)) {
                results.push_back(&ref);
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        logger::debug("FindCookingStations: Found {} cooking stations in cell", results.size());
        return results;
    }

    std::vector<RE::TESObjectREFR*> CellAnalyzer::FindFurnitureAbove(RE::Actor* actor, float minZDiff) {
        std::vector<RE::TESObjectREFR*> results;
        if (!actor) return results;

        auto* cell = actor->GetParentCell();
        if (!cell) return results;

        float actorX = actor->GetPositionX();
        float actorY = actor->GetPositionY();
        float actorZ = actor->GetPositionZ();

        cell->ForEachReference([&results, actorZ, minZDiff](RE::TESObjectREFR& ref) {
            if (ref.IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;

            auto* baseObj = ref.GetBaseObject();
            if (!baseObj || !baseObj->Is(RE::FormType::Furniture))
                return RE::BSContainer::ForEachResult::kContinue;

            float refZ = ref.GetPositionZ();
            if ((refZ - actorZ) >= minZDiff) {
                results.push_back(&ref);
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        // Sort by horizontal (XY) distance to actor — nearest directly above first
        std::sort(results.begin(), results.end(),
            [actorX, actorY](RE::TESObjectREFR* a, RE::TESObjectREFR* b) {
                float dxA = a->GetPositionX() - actorX;
                float dyA = a->GetPositionY() - actorY;
                float dxB = b->GetPositionX() - actorX;
                float dyB = b->GetPositionY() - actorY;
                return (dxA * dxA + dyA * dyA) < (dxB * dxB + dyB * dyB);
            });

        logger::debug("FindFurnitureAbove: Found {} furniture refs {}+ units above actor Z={:.0f}",
                      results.size(), minZDiff, actorZ);

        return results;
    }

    std::vector<RE::TESObjectREFR*> CellAnalyzer::FindFurnitureBelow(RE::Actor* actor, float minZDiff) {
        std::vector<RE::TESObjectREFR*> results;
        if (!actor) return results;

        auto* cell = actor->GetParentCell();
        if (!cell) return results;

        float actorX = actor->GetPositionX();
        float actorY = actor->GetPositionY();
        float actorZ = actor->GetPositionZ();

        cell->ForEachReference([&results, actorZ, minZDiff](RE::TESObjectREFR& ref) {
            if (ref.IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;

            auto* baseObj = ref.GetBaseObject();
            if (!baseObj || !baseObj->Is(RE::FormType::Furniture))
                return RE::BSContainer::ForEachResult::kContinue;

            float refZ = ref.GetPositionZ();
            if ((actorZ - refZ) >= minZDiff) {
                results.push_back(&ref);
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        // Sort by horizontal (XY) distance to actor — nearest directly below first
        std::sort(results.begin(), results.end(),
            [actorX, actorY](RE::TESObjectREFR* a, RE::TESObjectREFR* b) {
                float dxA = a->GetPositionX() - actorX;
                float dyA = a->GetPositionY() - actorY;
                float dxB = b->GetPositionX() - actorX;
                float dyB = b->GetPositionY() - actorY;
                return (dxA * dxA + dyA * dyA) < (dxB * dxB + dyB * dyB);
            });

        logger::debug("FindFurnitureBelow: Found {} furniture refs {}+ units below actor Z={:.0f}",
                      results.size(), minZDiff, actorZ);

        return results;
    }

    RE::BSFixedString CellAnalyzer::GetSpatialInfoJSON(RE::Actor* actor) {
        if (!actor) return "{}";

        try {
            nlohmann::json result;

            auto* cell = actor->GetParentCell();
            if (!cell) {
                logger::warn("GetSpatialInfoJSON: Actor '{}' has no parent cell",
                             actor->GetDisplayFullName());
                return "{}";
            }

            // Cell info - use robust name resolution
            std::string cellNameStr = ResolveCellName(actor, cell);
            result["cellName"] = cellNameStr;

            // Interior detection - check both cell flag and location keywords
            bool isInterior = IsEffectivelyInterior(actor, cell);
            result["cellType"] = isInterior ? "interior" : "exterior";

            logger::debug("GetSpatialInfoJSON: Actor '{}' -> cell='{}', type={}, isInterior={}",
                         actor->GetDisplayFullName(), cellNameStr,
                         cell->IsInteriorCell() ? "interior-cell" : "exterior-cell",
                         isInterior);

            // Doors
            nlohmann::json doorsJson = nlohmann::json::array();
            auto doors = GetDoors(actor);

            for (auto* door : doors) {
                if (!door) continue;

                try {
                    nlohmann::json doorInfo;

                    doorInfo["direction"] = GetDirection(actor, door);
                    doorInfo["leadsTo"] = std::string(GetDoorDestinationName(door).c_str());
                    doorInfo["isExterior"] = IsDoorExterior(door);

                    // Z direction
                    if (IsDoorUpward(door)) {
                        doorInfo["direction"] = "up";
                    } else if (IsDoorDownward(door)) {
                        doorInfo["direction"] = "down";
                    }

                    // Check if locked
                    bool isLocked = door->IsLocked();
                    doorInfo["isLocked"] = isLocked;

                    doorsJson.push_back(doorInfo);
                } catch (const std::exception& e) {
                    logger::warn("GetSpatialInfoJSON: Error processing door: {}", e.what());
                }
            }

            result["doors"] = doorsJson;

            // Directional availability
            bool hasUp = false, hasDown = false;
            for (auto* door : doors) {
                if (!door) continue;
                if (IsDoorUpward(door)) hasUp = true;
                if (IsDoorDownward(door)) hasDown = true;
            }
            result["hasStairsUp"] = hasUp;
            result["hasStairsDown"] = hasDown;

            // Notable areas (furniture, etc.) - basic implementation
            nlohmann::json areasJson = nlohmann::json::array();
            // TODO: Scan for notable furniture (bars, fireplaces, beds)
            result["notableAreas"] = areasJson;

            auto jsonStr = result.dump();
            logger::debug("GetSpatialInfoJSON: Returning {} bytes of JSON", jsonStr.size());
            return RE::BSFixedString(jsonStr);

        } catch (const std::exception& e) {
            logger::error("GetSpatialInfoJSON: Exception: {}", e.what());
            return "{}";
        } catch (...) {
            logger::error("GetSpatialInfoJSON: Unknown exception");
            return "{}";
        }
    }

}  // namespace IntelEngine
