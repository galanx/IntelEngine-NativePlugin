/**
 * Cell Analyzer Implementation
 *
 * Analyzes cell geometry and door connections.
 */

#include "CellAnalyzer.h"
#include "GameText.h"
#include "LanguageBridge.h"
#include "LocationResolver.h"
#include "StringUtils.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <nlohmann/json.hpp>

namespace IntelEngine {

    std::vector<RE::TESObjectREFR*> CellAnalyzer::GetDoors(RE::Actor* actor) {
        std::vector<RE::TESObjectREFR*> doors;

        if (!actor) return doors;

        auto* cell = actor->GetParentCell();
        if (!cell) return doors;

        // Iterate through all references in the cell
        cell->ForEachReference([&doors](RE::TESObjectREFR* ref) {
            if (!ref) return RE::BSContainer::ForEachResult::kContinue;

            // Check if it's a door
            auto* baseObj = ref->GetBaseObject();
            if (!baseObj) return RE::BSContainer::ForEachResult::kContinue;

            if (baseObj->Is(RE::FormType::Door)) {
                // Skip if disabled
                if (!ref->IsDisabled()) {
                    doors.push_back(ref);
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

        return std::as_const(extraTeleport->teleportData->linkedDoor).get().get();
    }

    RE::BSFixedString CellAnalyzer::GetDoorDestinationName(RE::TESObjectREFR* door) {
        if (!door) return "";

        auto* destDoor = GetDoorDestination(door);
        if (!destDoor) {
            // No teleport data, check if it has a name directly
            auto doorName = GameText::In(door->GetName());
            if (doorName && strlen(doorName) > 0) {
                return doorName;
            }
            return "Unknown";
        }

        auto* destCell = destDoor->GetSaveParentCell();
        if (!destCell) destCell = destDoor->GetParentCell();
        if (!destCell) return "Unknown";

        auto cellName = GameText::In(destCell->GetName());
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

        auto* destCell = destDoor->GetSaveParentCell();
        if (!destCell) destCell = destDoor->GetParentCell();
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

        // What a name calls "up" is data (PlaceWords.h, `name upstairs`): English's words and those of the language
        // the game is in. (Heights of doors need no word: IsDoorUpward.)
        auto destName = GetDoorDestinationName(door);
        auto doorName = GameText::In(door->GetName());
        auto* language = LanguageBridge::GetSingleton();
        return language->NameSays(destName.c_str(), "upstairs") || (doorName && language->NameSays(doorName, "upstairs"));
    }

    bool CellAnalyzer::IsDoorNameDownward(RE::TESObjectREFR* door) {
        if (!door) return false;

        auto destName = GetDoorDestinationName(door);
        auto doorName = GameText::In(door->GetName());
        auto* language = LanguageBridge::GetSingleton();
        return language->NameSays(destName.c_str(), "downstairs") || (doorName && language->NameSays(doorName, "downstairs"));
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
        auto cellName = GameText::In(cell->GetName());
        if (cellName && strlen(cellName) > 0) {
            return cellName;
        }

        // 2. Fallback: actor's BGSLocation name (works for modded interiors in exterior cells)
        auto* location = actor->GetCurrentLocation();
        if (location) {
            auto locName = GameText::In(location->GetFullName());
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
                auto wsName = GameText::In(worldspace->GetFullName());
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

        // The game's own word first, which is no word: furniture one can sleep in. The same in every language.
        if (auto* furniture = baseObj->As<RE::TESFurniture>(); furniture && furniture->furnFlags.any(RE::TESFurniture::ActiveMarker::kCanSleep)) {
            return true;
        }

        // Then its editor id (English in every game; more reliable for generic/unnamed furniture), then what it is
        // called: the words are data (PlaceWords.h, `name bed`), English's and those of the language in force.
        auto* language = LanguageBridge::GetSingleton();
        auto editorId = baseObj->GetFormEditorID();
        if (editorId && editorId[0] && language->NameSays(editorId, "bed", true)) return true;
        auto* name = GameText::In(baseObj->GetName());
        return name && name[0] && language->NameSays(name, "bed");
    }

    bool CellAnalyzer::IsCookingStation(RE::TESObjectREFR* ref) {
        if (!ref) return false;

        auto* baseObj = ref->GetBaseObject();
        if (!baseObj) return false;

        // Cooking stations can be Furniture or Activator types
        if (!baseObj->Is(RE::FormType::Furniture) && !baseObj->Is(RE::FormType::Activator))
            return false;

        // The game's own keyword first: what the cooking menu itself goes by, in every language.
        RE::BGSKeywordForm* keywords = nullptr;
        if (auto* furniture = baseObj->As<RE::TESFurniture>()) keywords = furniture;
        else if (auto* activator = baseObj->As<RE::TESObjectACTI>()) keywords = activator;
        if (keywords && (keywords->HasKeywordString("CraftingCookpot") || keywords->HasKeywordString("BYOHCraftingOven"))) return true;

        // Then its editor id (English in every game), then what it is called: the words are data (PlaceWords.h,
        // `name cooking`).
        auto* language = LanguageBridge::GetSingleton();
        auto editorId = baseObj->GetFormEditorID();
        if (editorId && editorId[0] && language->NameSays(editorId, "cooking", true)) return true;
        auto* name = GameText::In(baseObj->GetName());
        return name && name[0] && language->NameSays(name, "cooking");
    }

    std::vector<RE::TESObjectREFR*> CellAnalyzer::FindCookingStations(RE::Actor* actor) {
        std::vector<RE::TESObjectREFR*> results;
        if (!actor) return results;

        auto* cell = actor->GetParentCell();
        if (!cell) return results;

        cell->ForEachReference([&results, this](RE::TESObjectREFR* ref) {
            if (!ref || ref->IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;

            if (IsCookingStation(ref)) {
                results.push_back(ref);
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

        cell->ForEachReference([&results, actorZ, minZDiff](RE::TESObjectREFR* ref) {
            if (!ref || ref->IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;

            auto* baseObj = ref->GetBaseObject();
            if (!baseObj || !baseObj->Is(RE::FormType::Furniture))
                return RE::BSContainer::ForEachResult::kContinue;

            float refZ = ref->GetPositionZ();
            if ((refZ - actorZ) >= minZDiff) {
                results.push_back(ref);
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

        cell->ForEachReference([&results, actorZ, minZDiff](RE::TESObjectREFR* ref) {
            if (!ref || ref->IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;

            auto* baseObj = ref->GetBaseObject();
            if (!baseObj || !baseObj->Is(RE::FormType::Furniture))
                return RE::BSContainer::ForEachResult::kContinue;

            float refZ = ref->GetPositionZ();
            if ((actorZ - refZ) >= minZDiff) {
                results.push_back(ref);
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
                             GameText::In(actor->GetDisplayFullName()));
                return "{}";
            }

            // Cell info - use robust name resolution
            std::string cellNameStr = ResolveCellName(actor, cell);
            result["cellName"] = cellNameStr;

            // Interior detection - check both cell flag and location keywords
            bool isInterior = IsEffectivelyInterior(actor, cell);
            result["cellType"] = isInterior ? "interior" : "exterior";

            logger::debug("GetSpatialInfoJSON: Actor '{}' -> cell='{}', type={}, isInterior={}",
                         GameText::In(actor->GetDisplayFullName()), cellNameStr,
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

            auto jsonStr = result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
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

    void CellAnalyzer::EnsureDangerousKeywordsCached() {
        if (m_dangerousKeywordsCached) return;
        m_dangerousKeywordsCached = true;

        // Inverted logic: enumerate SAFE location types (small, stable set).
        // Any interior NOT matching these is considered dangerous.
        // This catches dungeons, dragon lairs, vampire lairs, warlock lairs,
        // hagraven nests, animal dens, giant camps, and modded dungeon types
        // without needing an exhaustive dangerous-type list.
        static const char* safeEditorIDs[] = {
            "LocTypeInn", "LocTypeStore", "LocTypeHouse",
            "LocTypePlayerHouse", "LocTypeTemple", "LocTypeGuild",
            "LocTypeFarm", "LocTypeLumberMill", "LocTypeCastle",
            "LocTypeJail",
            // City/town/settlement keywords — Whiterun streets, Riverwood square, etc.
            // These exterior hubs don't carry the interior-building keywords above,
            // so without them the city itself falls through as "dangerous" and the
            // civilian-block policy aborts quest couriers walking into town.
            "LocTypeHabitation", "LocTypeHabitationHasInn",
            "LocTypeCity", "LocTypeTown", "LocTypeSettlement",
            "LocTypeHoldCapital", "LocTypeHoldMajor", "LocTypeHold"
        };

        for (auto* editorID : safeEditorIDs) {
            auto* form = RE::TESForm::LookupByEditorID(editorID);
            if (form) {
                auto* keyword = form->As<RE::BGSKeyword>();
                if (keyword) {
                    m_safeKeywords.push_back(keyword);
                }
            }
        }

        logger::debug("CellAnalyzer: Cached {} safe location keywords", m_safeKeywords.size());
    }

    bool CellAnalyzer::IsLocationDangerous(RE::BGSLocation* location) {
        if (location) EnsureDangerousKeywordsCached();
        RE::BGSKeyword* matched = nullptr;
        if (location) for (auto* keyword : m_safeKeywords) if (location->HasKeyword(keyword)) { matched = keyword; break; }
        const bool dangerous = !matched;
        if (spdlog::default_logger()->should_log(spdlog::level::debug)) {
            static std::mutex diagnosticMutex;
            std::lock_guard diagnosticLock(diagnosticMutex);
            static RE::BGSLocation* previousLocation = nullptr;
            static bool previousDangerous = false, initialized = false;
            if (!initialized || location != previousLocation || dangerous != previousDangerous) {
                const char* name = location ? GameText::In(location->GetFullName()) : nullptr;
                const char* keyword = matched ? matched->GetFormEditorID() : nullptr;
                logger::debug("[diag:danger] location='{}' FormID={} safeKeyword={}",
                    name ? name : "null", location ? location->GetFormID() : 0,
                    location ? (keyword ? keyword : "none") : "null location");
                previousLocation = location; previousDangerous = dangerous; initialized = true;
            }
        }
        return dangerous;
    }

    bool CellAnalyzer::IsLocationNonCombative(RE::BGSLocation* location) {
        if (!location) return false;

        // Keyword-based: only temples are universally non-combative.
        // Castles, guild halls, and inns are intentionally excluded —
        // sieges, guild defense, and faction battles at these locations are lore-appropriate.
        static auto* s_templeKW = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeTemple");
        if (s_templeKW && location->HasKeyword(s_templeKW)) {
            return true;
        }

        // Name-based: specific sacred/peaceful locations that should never have combat quests.
        // Does NOT walk parent chain — sub-dungeons inside castles (Nchuand-Zel, dragon trap)
        // are valid combat zones even if their parent is a protected location.
        auto locName = GameText::In(location->GetFullName());
        if (locName && locName[0]) {
            static const std::vector<std::string> kNonCombative = {
                "High Hrothgar", "Sky Haven Temple",
                "Eldergleam Sanctuary", "Ancestor Glade",
                "Blue Palace", "Palace of the Kings",
                "Mistveil Keep", "Understone Keep", "Dragonsreach"
            };
            std::string name(locName);
            for (const auto& sacred : kNonCombative) {
                if (name.find(sacred) != std::string::npos) return true;
            }
        }

        return false;
    }

    bool CellAnalyzer::IsPlayerInDangerousLocation() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;
        return IsLocationDangerous(player->GetCurrentLocation());
    }

    bool CellAnalyzer::IsPlayerInOwnHome() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        auto* cell = player->GetParentCell();
        auto* location = player->GetCurrentLocation();

        // Also check cell's own location if player location is null or lacks keyword
        auto* keyword = LocationResolver::GetSingleton()->GetPlayerHouseKeyword();

        if (location && keyword && location->HasKeyword(keyword)) {
            logger::debug("IsPlayerInOwnHome: true (player location '{}' has LocTypePlayerHouse)",
                         GameText::In(location->GetFullName()) ? GameText::In(location->GetFullName()) : "unnamed");
            return true;
        }

        // Fallback: check cell's location (player's GetCurrentLocation can lag behind cell transitions)
        if (cell) {
            auto* cellLoc = cell->GetLocation();
            if (cellLoc && keyword && cellLoc->HasKeyword(keyword)) {
                logger::debug("IsPlayerInOwnHome: true (cell location '{}' has LocTypePlayerHouse)",
                             GameText::In(cellLoc->GetFullName()) ? GameText::In(cellLoc->GetFullName()) : "unnamed");
                return true;
            }
        }

        logger::debug("IsPlayerInOwnHome: false (location={}, cell={})",
                     location ? (GameText::In(location->GetFullName()) ? GameText::In(location->GetFullName()) : "unnamed") : "null",
                     cell ? (GameText::In(cell->GetName()) ? GameText::In(cell->GetName()) : "unnamed") : "null");
        return false;
    }

}  // namespace IntelEngine
