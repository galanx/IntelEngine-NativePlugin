/**
 * Location Resolver Implementation
 *
 * Resolves named locations and semantic terms to travel destinations.
 * No external database dependencies - builds index from game data.
 */

#include "LocationResolver.h"
#include "CellAnalyzer.h"
#include "NPCIndex.h"
#include "StringUtils.h"
#include "ProcessUtils.h"
#include "Settings.h"

#include <cfloat>
#include <nlohmann/json.hpp>

namespace IntelEngine {

    void LocationResolver::BuildLocationIndex() {
        std::unique_lock lock(m_mutex);

        logger::info("Building location index from game data...");

        m_cellIndex.clear();
        m_locationIndex.clear();
        m_allCellNames.clear();
        m_allLocationNames.clear();

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            logger::error("Failed to get TESDataHandler");
            return;
        }

        // Index interior cells by name
        // NOTE: Interior cells are stored in dataHandler->interiorCells (NiTPrimitiveArray),
        // NOT in GetFormArray<TESObjectCELL>() which may return an empty array.
        std::uint32_t totalCells = 0;
        std::uint32_t namedCells = 0;

        for (auto* cell : dataHandler->interiorCells) {
            if (!cell) continue;
            totalCells++;

            // Get cell display name
            auto name = cell->GetName();
            if (name && strlen(name) > 0) {
                namedCells++;
                std::string lowerName = StringUtils::ToLowerStd(name);

                // Only add if not already present (first wins)
                if (m_cellIndex.find(lowerName) == m_cellIndex.end()) {
                    m_cellIndex[lowerName] = cell->GetFormID();
                    m_allCellNames.push_back(lowerName);
                }
            }

            // Also index by editor ID for modders
            auto editorId = cell->GetFormEditorID();
            if (editorId && strlen(editorId) > 0) {
                std::string lowerEditorId = StringUtils::ToLowerStd(editorId);

                if (m_cellIndex.find(lowerEditorId) == m_cellIndex.end()) {
                    m_cellIndex[lowerEditorId] = cell->GetFormID();
                    // Don't add editor IDs to allCellNames to avoid cluttering fuzzy search
                }
            }
        }

        logger::info("Interior cells scanned: {} total, {} with names, {} unique indexed",
                     totalCells, namedCells, m_allCellNames.size());

        // Index all BGSLocations by name (Whiterun, Dragonsreach, etc.)
        for (auto* loc : dataHandler->GetFormArray<RE::BGSLocation>()) {
            if (!loc) continue;

            auto name = loc->GetFullName();
            if (name && strlen(name) > 0) {
                std::string lowerName = StringUtils::ToLowerStd(name);

                if (m_locationIndex.find(lowerName) == m_locationIndex.end()) {
                    m_locationIndex[lowerName] = loc->GetFormID();
                    m_allLocationNames.push_back(lowerName);
                }
            }

            // Also index by editor ID
            auto editorId = loc->GetFormEditorID();
            if (editorId && strlen(editorId) > 0) {
                std::string lowerEditorId = StringUtils::ToLowerStd(editorId);

                if (m_locationIndex.find(lowerEditorId) == m_locationIndex.end()) {
                    m_locationIndex[lowerEditorId] = loc->GetFormID();
                }
            }
        }

        // Cache LocTypePlayerHouse keyword for player home resolution
        m_locTypePlayerHouse = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypePlayerHouse");
        if (m_locTypePlayerHouse) {
            logger::info("Cached LocTypePlayerHouse keyword (FormID {:08X})", m_locTypePlayerHouse->GetFormID());
        } else {
            logger::warn("LocTypePlayerHouse keyword not found — player home resolution will be unavailable");
        }

        // Build NPC home index from bed ownership data
        BuildHomeIndex();

        m_indexBuilt = true;
        logger::info("Location index built: {} cells, {} locations, {} NPC homes",
                     m_allCellNames.size(), m_allLocationNames.size(), m_npcHomeIndex.size());
    }

    RE::TESObjectCELL* LocationResolver::ResolveCell(const std::string& locationName) {
        std::shared_lock lock(m_mutex);

        if (locationName.empty()) return nullptr;

        std::string lowerName = StringUtils::ToLowerStd(locationName);

        // 1. Exact match
        auto it = m_cellIndex.find(lowerName);
        if (it != m_cellIndex.end()) {
            auto* form = RE::TESForm::LookupByID(it->second);
            if (form) {
                logger::debug("ResolveCell('{}') -> Exact match", locationName);
                return form->As<RE::TESObjectCELL>();
            }
        }

        // 2. Token + fuzzy match (article-stripped exact → word overlap → Levenshtein)
        auto fuzzy = StringUtils::TokenFuzzyFind(lowerName, m_allCellNames,
                                                  Settings::GetSingleton()->fuzzyMatchThreshold);
        if (fuzzy) {
            auto cellIt = m_cellIndex.find(fuzzy.match);
            if (cellIt != m_cellIndex.end()) {
                auto* form = RE::TESForm::LookupByID(cellIt->second);
                if (form) {
                    logger::debug("ResolveCell('{}') -> Fuzzy match '{}' (distance={})",
                                 locationName, fuzzy.match, fuzzy.distance);
                    return form->As<RE::TESObjectCELL>();
                }
            }
        }

        logger::debug("ResolveCell('{}') -> Not found", locationName);
        return nullptr;
    }

    RE::BGSLocation* LocationResolver::ResolveLocation(const std::string& locationName) {
        std::shared_lock lock(m_mutex);

        if (locationName.empty()) return nullptr;

        std::string lowerName = StringUtils::ToLowerStd(locationName);

        // 1. Exact match
        auto it = m_locationIndex.find(lowerName);
        if (it != m_locationIndex.end()) {
            auto* form = RE::TESForm::LookupByID(it->second);
            if (form) {
                logger::info("ResolveLocation('{}') -> Exact match (FormID {:08X})",
                            locationName, it->second);
                return form->As<RE::BGSLocation>();
            }
        }

        // 2. Token + fuzzy match (article-stripped exact → word overlap → Levenshtein)
        auto fuzzy = StringUtils::TokenFuzzyFind(lowerName, m_allLocationNames,
                                                  Settings::GetSingleton()->fuzzyMatchThreshold);
        if (fuzzy) {
            auto locIt = m_locationIndex.find(fuzzy.match);
            if (locIt != m_locationIndex.end()) {
                auto* form = RE::TESForm::LookupByID(locIt->second);
                if (form) {
                    logger::info("ResolveLocation('{}') -> Fuzzy match '{}' (distance={}, FormID {:08X})",
                                locationName, fuzzy.match, fuzzy.distance, locIt->second);
                    return form->As<RE::BGSLocation>();
                }
            }
        }

        logger::info("ResolveLocation('{}') -> Not found (no exact or fuzzy match)", locationName);
        return nullptr;
    }

    RE::TESObjectREFR* LocationResolver::FindDoorInCellTo(RE::TESObjectCELL* sourceCell, RE::TESObjectCELL* targetCell) {
        if (!sourceCell || !targetCell) return nullptr;

        RE::TESObjectREFR* foundDoor = nullptr;

        sourceCell->ForEachReference([&](RE::TESObjectREFR& ref) -> RE::BSContainer::ForEachResult {
            auto* baseObj = ref.GetBaseObject();
            if (!baseObj || !baseObj->Is(RE::FormType::Door)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            if (ref.IsDisabled()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Check where this door leads
            auto* extraTeleport = ref.extraList.GetByType<RE::ExtraTeleport>();
            if (!extraTeleport || !extraTeleport->teleportData) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            auto linkedDoor = extraTeleport->teleportData->linkedDoor.get();
            if (!linkedDoor) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            auto* destCell = linkedDoor->GetParentCell();
            if (destCell && destCell->GetFormID() == targetCell->GetFormID()) {
                foundDoor = &ref;
                return RE::BSContainer::ForEachResult::kStop;
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        return foundDoor;
    }

    RE::TESObjectREFR* LocationResolver::FindDoorTo(const std::string& locationName) {
        // Find the target cell
        auto* targetCell = ResolveCell(locationName);
        if (!targetCell) {
            return nullptr;
        }

        // Get player's current cell to start searching
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;

        auto* currentCell = player->GetParentCell();
        if (!currentCell) return nullptr;

        // Search current cell for a door to the target
        auto* door = FindDoorInCellTo(currentCell, targetCell);
        if (door) {
            logger::debug("FindDoorTo('{}') -> Found door in current cell", locationName);
            return door;
        }

        // For exterior cells, we might need to search attached cells
        // But for now, if not found in current cell, return nullptr
        // and let Papyrus use AI package-based travel

        logger::debug("FindDoorTo('{}') -> No door found in current cell", locationName);
        return nullptr;
    }

    RE::TESObjectREFR* LocationResolver::FindTravelTarget(const std::string& locationName) {
        // Named destination resolution — prioritize world markers (exterior persistent refs)
        // over interior cell refs. Interior cell refs cause pathfinding failures and
        // false arrival detection when the NPC is in a different worldspace.
        //
        // Semantic destinations (outside, upstairs, etc.) are handled separately by
        // ResolveSemantic() via door scanning — they never reach this function.

        logger::info("FindTravelTarget('{}') — starting resolution", locationName);

        // Strategy 1: World location marker (highest priority for named destinations)
        // These are exterior persistent references at the map marker position.
        // Skyrim's AI can always pathfind to these.
        auto* bgsLocation = ResolveLocation(locationName);
        if (bgsLocation) {
            auto locName = bgsLocation->GetFullName();
            logger::info("  BGSLocation matched: '{}' (FormID {:08X})",
                        locName ? locName : "unnamed", bgsLocation->GetFormID());

            auto worldMarkerPtr = bgsLocation->worldLocMarker.get();
            if (worldMarkerPtr) {
                auto* markerRef = worldMarkerPtr.get();
                if (markerRef) {
                    auto pos = markerRef->GetPosition();
                    logger::info("  Strategy 1 SUCCESS: worldLocMarker at ({:.0f}, {:.0f}, {:.0f})",
                                pos.x, pos.y, pos.z);
                    return markerRef;
                }
                logger::info("  Strategy 1: worldLocMarker pointer valid but ref is null");
            } else {
                logger::info("  Strategy 1: worldLocMarker is NULL");
            }

            // Strategy 1b: Exterior door of destination cell.
            // For named interior locations like "Breezehome" that have no worldLocMarker,
            // find the door leading into the building and return the exterior-side door.
            // This sends NPCs to the building entrance, not the parent city gate.
            {
                auto* destCell = ResolveCell(locationName);
                if (destCell && destCell->IsInteriorCell()) {
                    auto* interiorDoor = FindExteriorDoorInCell(destCell);
                    if (interiorDoor) {
                        auto* teleport = interiorDoor->extraList.GetByType<RE::ExtraTeleport>();
                        if (teleport && teleport->teleportData) {
                            auto linkedDoor = teleport->teleportData->linkedDoor.get();
                            if (linkedDoor) {
                                auto* extDoor = linkedDoor.get();
                                if (extDoor) {
                                    auto pos = extDoor->GetPosition();
                                    logger::info("  Strategy 1b SUCCESS: exterior door of '{}' at ({:.0f}, {:.0f}, {:.0f})",
                                                destCell->GetName() ? destCell->GetName() : "unnamed",
                                                pos.x, pos.y, pos.z);
                                    return extDoor;
                                }
                            }
                        }
                    }
                    logger::info("  Strategy 1b: no exterior door found for cell '{}'",
                                destCell->GetName() ? destCell->GetName() : "unnamed");
                }
            }

            // Strategy 2: Parent location's world marker (for interior locations like inns/shops
            // whose parent is an exterior settlement with a map marker)
            auto* parentLoc = bgsLocation->parentLoc;
            if (parentLoc) {
                auto parentName = parentLoc->GetFullName();
                logger::info("  Parent location: '{}' (FormID {:08X})",
                            parentName ? parentName : "unnamed", parentLoc->GetFormID());

                auto parentMarkerPtr = parentLoc->worldLocMarker.get();
                if (parentMarkerPtr) {
                    auto* parentRef = parentMarkerPtr.get();
                    if (parentRef) {
                        auto pos = parentRef->GetPosition();
                        logger::info("  Strategy 2 SUCCESS: parent worldLocMarker at ({:.0f}, {:.0f}, {:.0f})",
                                    pos.x, pos.y, pos.z);
                        return parentRef;
                    }
                }
                logger::info("  Strategy 2: parent worldLocMarker is NULL");

                // Strategy 2b: Grandparent location marker
                // e.g., "Sleeping Giant Inn" → "Riverwood" → "Whiterun Hold"
                auto* grandparentLoc = parentLoc->parentLoc;
                if (grandparentLoc) {
                    auto gpName = grandparentLoc->GetFullName();
                    logger::info("  Grandparent location: '{}' (FormID {:08X})",
                                gpName ? gpName : "unnamed", grandparentLoc->GetFormID());

                    auto gpMarkerPtr = grandparentLoc->worldLocMarker.get();
                    if (gpMarkerPtr) {
                        auto* gpRef = gpMarkerPtr.get();
                        if (gpRef) {
                            auto pos = gpRef->GetPosition();
                            logger::info("  Strategy 2b SUCCESS: grandparent worldLocMarker at ({:.0f}, {:.0f}, {:.0f})",
                                        pos.x, pos.y, pos.z);
                            return gpRef;
                        }
                    }
                    logger::info("  Strategy 2b: grandparent worldLocMarker is NULL");
                } else {
                    logger::info("  No grandparent location");
                }
            } else {
                logger::info("  No parent location");
            }

            // Strategy 3: Find a loaded actor at the BGSLocation (all 4 process tiers)
            logger::info("  Strategy 3: searching loaded actors at BGSLocation...");
            RE::TESObjectREFR* strategy3Result = nullptr;
            ProcessUtils::ForEachLoadedActor([&](RE::Actor* loadedActor) -> bool {
                if (loadedActor->GetCurrentLocation() == bgsLocation) {
                    auto actorName = loadedActor->GetDisplayFullName();
                    auto pos = loadedActor->GetPosition();
                    logger::info("  Strategy 3 SUCCESS: found '{}' at ({:.0f}, {:.0f}, {:.0f})",
                                actorName ? actorName : "unnamed",
                                pos.x, pos.y, pos.z);
                    strategy3Result = loadedActor;
                    return true;  // stop iteration
                }
                return false;
            });
            if (strategy3Result) return strategy3Result;
            logger::info("  Strategy 3: no loaded actors at BGSLocation");
        } else {
            logger::info("  No BGSLocation match for '{}'", locationName);
        }

        // Strategy 4: Door in current cell leading to the target
        // (nearby destinations — "go to the Bannered Mare" when already in Whiterun)
        auto* doorRef = FindDoorTo(locationName);
        if (doorRef) {
            logger::info("  Strategy 4 SUCCESS: door in current cell");
            return doorRef;
        }
        logger::info("  Strategy 4: no door in current cell");

        // Strategy 5: Interior cell reference (last resort — for modded interiors
        // without BGSLocation or world markers)
        auto* targetCell = ResolveCell(locationName);
        if (targetCell) {
            auto cellName = targetCell->GetName();
            logger::info("  Strategy 5: matched interior cell '{}' (FormID {:08X})",
                        cellName ? cellName : "unnamed", targetCell->GetFormID());

            RE::TESObjectREFR* bestRef = nullptr;

            targetCell->ForEachReference([&](RE::TESObjectREFR& ref) -> RE::BSContainer::ForEachResult {
                if (ref.IsDisabled()) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                auto* baseObj = ref.GetBaseObject();
                if (baseObj && baseObj->Is(RE::FormType::Door)) {
                    bestRef = &ref;
                    return RE::BSContainer::ForEachResult::kStop;
                }

                if (!bestRef) {
                    bestRef = &ref;
                }

                return RE::BSContainer::ForEachResult::kContinue;
            });

            if (bestRef) {
                auto pos = bestRef->GetPosition();
                logger::info("  Strategy 5 SUCCESS: interior cell ref at ({:.0f}, {:.0f}, {:.0f})",
                            pos.x, pos.y, pos.z);
                return bestRef;
            }
            logger::info("  Strategy 5: cell found but no valid references");
        } else {
            logger::info("  Strategy 5: no interior cell match");
        }

        logger::info("FindTravelTarget('{}') -> FAILED — no travel target found", locationName);
        return nullptr;
    }

    RE::TESObjectREFR* LocationResolver::Resolve(const std::string& locationName) {
        return FindTravelTarget(locationName);
    }

    RE::TESObjectREFR* LocationResolver::ResolveSemantic(RE::Actor* actor, const std::string& semanticTerm) {
        if (!actor) return nullptr;

        std::string lowerTerm = StringUtils::ToLowerStd(semanticTerm);

        // Log all doors for diagnostics
        LogAllDoors(actor);

        logger::debug("ResolveSemantic: Resolving '{}'", semanticTerm);

        // Route to specific resolver
        if (lowerTerm == "upstairs" || lowerTerm == "up" || lowerTerm == "above") {
            return ResolveUpstairs(actor);
        }
        if (lowerTerm == "downstairs" || lowerTerm == "down" || lowerTerm == "below") {
            return ResolveDownstairs(actor);
        }
        if (lowerTerm == "outside" || lowerTerm == "out" || lowerTerm == "exterior") {
            return ResolveOutside(actor);
        }
        if (lowerTerm == "inside" || lowerTerm == "in" || lowerTerm == "interior") {
            return ResolveInside(actor);
        }
        if (lowerTerm == "the back" || lowerTerm == "back room" || lowerTerm == "back") {
            return ResolveBack(actor);
        }
        if (lowerTerm == "cellar" || lowerTerm == "basement" || lowerTerm == "below stairs") {
            return ResolveCellar(actor);
        }
        if (lowerTerm == "bedroom" || lowerTerm == "the bedroom" || lowerTerm == "my room" ||
            lowerTerm == "my bed" || lowerTerm == "bed") {
            return ResolveBedroom(actor);
        }
        if (lowerTerm == "kitchen" || lowerTerm == "the kitchen") {
            return ResolveKitchen(actor);
        }

        logger::debug("ResolveSemantic: '{}' not matched to any resolver", semanticTerm);
        return nullptr;
    }

    void LocationResolver::LogAllDoors(RE::Actor* actor) {
        // Skip expensive door iteration when debug logging is off
        if (!spdlog::default_logger()->should_log(spdlog::level::debug)) return;

        auto* analyzer = CellAnalyzer::GetSingleton();
        auto doors = analyzer->GetDoors(actor);

        auto* cell = actor->GetParentCell();
        logger::debug("ResolveSemantic: Actor '{}' in cell '{}' ({} doors)",
                     actor->GetDisplayFullName(),
                     cell ? cell->GetName() : "null",
                     doors.size());

        for (size_t i = 0; i < doors.size(); i++) {
            auto* door = doors[i];
            auto destName = analyzer->GetDoorDestinationName(door);
            bool isExterior = analyzer->IsDoorExterior(door);
            bool isUp = analyzer->IsDoorUpward(door);
            bool isDown = analyzer->IsDoorDownward(door);
            bool nameUp = analyzer->IsDoorNameUpward(door);
            bool nameDown = analyzer->IsDoorNameDownward(door);

            auto doorRefName = door->GetName();
            logger::debug("  Door[{}]: refName='{}' dest='{}' exterior={} zUp={} zDown={} nameUp={} nameDown={}",
                         i, doorRefName ? doorRefName : "(null)", destName.c_str(),
                         isExterior, isUp, isDown, nameUp, nameDown);
        }
    }

    RE::TESObjectREFR* LocationResolver::ResolveUpstairs(RE::Actor* actor, bool preferBeds) {
        auto* analyzer = CellAnalyzer::GetSingleton();
        auto doors = analyzer->GetDoors(actor);

        logger::debug("ResolveUpstairs: Scanning {} doors, preferBeds={}", doors.size(), preferBeds);

        // Pass 1: Door name match (strongest signal — "The Resting Pilgrim Upstairs")
        for (auto* door : doors) {
            if (analyzer->IsDoorNameUpward(door)) {
                logger::debug("ResolveUpstairs: Door name match -> '{}'",
                             analyzer->GetDoorDestinationName(door).c_str());
                return door;
            }
        }

        // Pass 2: Furniture on upper floor (same-cell fallback — Bannered Mare)
        auto furniture = analyzer->FindFurnitureAbove(actor);
        if (!furniture.empty()) {
            // If preferBeds, try beds first
            if (preferBeds) {
                for (auto* ref : furniture) {
                    if (analyzer->IsBedFurniture(ref)) {
                        auto* name = ref->GetBaseObject() ? ref->GetBaseObject()->GetName() : "furniture";
                        logger::debug("ResolveUpstairs: Bed above -> '{}'", name ? name : "unnamed");
                        return ref;
                    }
                }
            }
            // Return first furniture above (any type)
            auto* first = furniture[0];
            auto* name = first->GetBaseObject() ? first->GetBaseObject()->GetName() : "furniture";
            logger::debug("ResolveUpstairs: Furniture above -> '{}'", name ? name : "unnamed");
            return first;
        }

        // Pass 3: Door Z-coordinate (interior doors only — skip exterior to prevent teleporting outside)
        for (auto* door : doors) {
            if (analyzer->IsDoorExterior(door)) continue;
            if (analyzer->IsDoorUpward(door)) {
                logger::debug("ResolveUpstairs: Door Z match -> '{}'",
                             analyzer->GetDoorDestinationName(door).c_str());
                return door;
            }
        }

        // Pass 4: Downstairs-named cell heuristic (inverse logic)
        // If current cell is "cellar"/"basement"/"lower", any door leading to a cell
        // WITHOUT those keywords is effectively "upstairs"
        auto* cell = actor->GetParentCell();
        if (cell) {
            auto cellName = cell->GetName();
            if (cellName && strlen(cellName) > 0) {
                std::string lowerCellName = StringUtils::ToLowerStd(cellName);
                if (lowerCellName.find("cellar") != std::string::npos ||
                    lowerCellName.find("basement") != std::string::npos ||
                    lowerCellName.find("lower") != std::string::npos ||
                    lowerCellName.find("dungeon") != std::string::npos ||
                    lowerCellName.find("undercroft") != std::string::npos) {

                    logger::debug("ResolveUpstairs: Cell '{}' is downstairs-named, trying inverse heuristic", cellName);

                    for (auto* door : doors) {
                        auto destName = analyzer->GetDoorDestinationName(door);
                        std::string lowerDest = StringUtils::ToLowerStd(destName.c_str());
                        if (lowerDest.find("cellar") == std::string::npos &&
                            lowerDest.find("basement") == std::string::npos &&
                            lowerDest.find("lower") == std::string::npos &&
                            lowerDest.find("dungeon") == std::string::npos &&
                            lowerDest.find("undercroft") == std::string::npos) {
                            logger::debug("ResolveUpstairs: Inverse heuristic -> '{}'", destName.c_str());
                            return door;
                        }
                    }
                }
            }
        }

        logger::debug("ResolveUpstairs: No matching target found");
        return nullptr;
    }

    RE::TESObjectREFR* LocationResolver::ResolveDownstairs(RE::Actor* actor, bool preferBeds) {
        auto* analyzer = CellAnalyzer::GetSingleton();
        auto doors = analyzer->GetDoors(actor);

        logger::debug("ResolveDownstairs: Scanning {} doors, preferBeds={}", doors.size(), preferBeds);

        // Pass 1: Door name match (strongest signal — "Honeyside Cellar")
        for (auto* door : doors) {
            if (analyzer->IsDoorNameDownward(door)) {
                logger::debug("ResolveDownstairs: Door name match -> '{}'",
                             analyzer->GetDoorDestinationName(door).c_str());
                return door;
            }
        }

        // Pass 2: Furniture on lower floor (same-cell fallback)
        auto furniture = analyzer->FindFurnitureBelow(actor);
        if (!furniture.empty()) {
            if (preferBeds) {
                for (auto* ref : furniture) {
                    if (analyzer->IsBedFurniture(ref)) {
                        auto* name = ref->GetBaseObject() ? ref->GetBaseObject()->GetName() : "furniture";
                        logger::debug("ResolveDownstairs: Bed below -> '{}'", name ? name : "unnamed");
                        return ref;
                    }
                }
            }
            auto* first = furniture[0];
            auto* name = first->GetBaseObject() ? first->GetBaseObject()->GetName() : "furniture";
            logger::debug("ResolveDownstairs: Furniture below -> '{}'", name ? name : "unnamed");
            return first;
        }

        // Pass 3: Door Z-coordinate (interior doors only — skip exterior to prevent teleporting outside)
        for (auto* door : doors) {
            if (analyzer->IsDoorExterior(door)) continue;
            if (analyzer->IsDoorDownward(door)) {
                logger::debug("ResolveDownstairs: Door Z match -> '{}'",
                             analyzer->GetDoorDestinationName(door).c_str());
                return door;
            }
        }

        // Pass 4: Upstairs-named cell heuristic
        // If current cell name contains "upstairs"/"upper", any door leading to a cell
        // WITHOUT those keywords is effectively "downstairs" (e.g., "The Resting Pilgrim Upstairs"
        // has a door to "The Resting Pilgrim" — that's downstairs)
        auto* cell = actor->GetParentCell();
        if (cell) {
            auto cellName = cell->GetName();
            if (cellName && strlen(cellName) > 0) {
                std::string lowerCellName = StringUtils::ToLowerStd(cellName);
                if (lowerCellName.find("upstairs") != std::string::npos ||
                    lowerCellName.find("upper") != std::string::npos ||
                    lowerCellName.find("second floor") != std::string::npos ||
                    lowerCellName.find("top floor") != std::string::npos) {

                    logger::debug("ResolveDownstairs: Cell '{}' is upstairs-named, trying inverse heuristic", cellName);

                    for (auto* door : doors) {
                        auto destName = analyzer->GetDoorDestinationName(door);
                        std::string lowerDest = StringUtils::ToLowerStd(destName.c_str());
                        // Any door NOT leading to an upstairs destination is "downstairs"
                        if (lowerDest.find("upstairs") == std::string::npos &&
                            lowerDest.find("upper") == std::string::npos &&
                            lowerDest.find("second floor") == std::string::npos &&
                            lowerDest.find("top floor") == std::string::npos) {
                            logger::debug("ResolveDownstairs: Inverse heuristic -> '{}'", destName.c_str());
                            return door;
                        }
                    }
                }
            }
        }

        logger::debug("ResolveDownstairs: No matching target found");
        return nullptr;
    }

    RE::TESObjectREFR* LocationResolver::ResolveOutside(RE::Actor* actor) {
        auto* analyzer = CellAnalyzer::GetSingleton();
        auto doors = analyzer->GetDoors(actor);

        logger::debug("ResolveOutside: Scanning {} doors", doors.size());

        // Pass 1: Door leading to exterior cell (strongest signal)
        // Return the INTERIOR-side door so the NPC can pathfind to it.
        // Papyrus handles teleporting the NPC through the door on arrival.
        for (auto* door : doors) {
            if (analyzer->IsDoorExterior(door)) {
                logger::debug("ResolveOutside: Exterior door -> '{}'",
                             analyzer->GetDoorDestinationName(door).c_str());
                return door;
            }
        }

        // Pass 2: Door whose destination name matches a known BGSLocation
        // (handles modded interiors where doors lack ExtraTeleport)
        for (auto* door : doors) {
            auto destName = analyzer->GetDoorDestinationName(door);
            if (destName.length() > 0) {
                auto* destLoc = ResolveLocation(destName.c_str());
                if (destLoc) {
                    logger::debug("ResolveOutside: Named location match -> '{}'",
                                 destName.c_str());
                    return door;
                }
            }
        }

        // Pass 3: Multi-hop — check if any door leads to a cell that HAS an exterior door
        // (e.g., "The Resting Pilgrim Upstairs" → "The Resting Pilgrim" → Skyrim exterior)
        // Uses FindExteriorDoorInCell to avoid duplicating the door-scanning loop.
        for (auto* door : doors) {
            auto* destDoor = analyzer->GetDoorDestination(door);
            if (!destDoor) continue;

            auto* destCell = destDoor->GetParentCell();
            if (!destCell) continue;

            if (FindExteriorDoorInCell(destCell)) {
                // Return the interior-side door so NPC can pathfind to it.
                // Papyrus handles teleporting through doors and multi-hop to exterior.
                logger::debug("ResolveOutside: Multi-hop via '{}' (destination cell has exterior door)",
                             analyzer->GetDoorDestinationName(door).c_str());
                return door;
            }
        }

        logger::debug("ResolveOutside: No exterior door found");
        return nullptr;
    }

    RE::TESObjectREFR* LocationResolver::ResolveInside(RE::Actor* actor) {
        auto* cell = actor->GetParentCell();
        if (!cell) return nullptr;

        // Already inside
        if (cell->IsInteriorCell()) {
            logger::debug("ResolveInside: Already in interior cell");
            return nullptr;
        }

        // Outside - find nearest interior door
        auto* analyzer = CellAnalyzer::GetSingleton();
        auto doors = analyzer->GetDoors(actor);

        logger::debug("ResolveInside: Scanning {} doors", doors.size());

        for (auto* door : doors) {
            if (!analyzer->IsDoorExterior(door)) {
                logger::debug("ResolveInside: Interior door -> '{}'",
                             analyzer->GetDoorDestinationName(door).c_str());
                return door;
            }
        }

        logger::debug("ResolveInside: No interior door found");
        return nullptr;
    }

    RE::TESObjectREFR* LocationResolver::ResolveBack(RE::Actor* actor) {
        auto* analyzer = CellAnalyzer::GetSingleton();
        auto doors = analyzer->GetDoors(actor);

        logger::debug("ResolveBack: Scanning {} doors", doors.size());

        if (doors.size() <= 1) {
            logger::debug("ResolveBack: Not enough doors ({})", doors.size());
            return nullptr;
        }

        // Find main entrance (exterior door or first door)
        RE::TESObjectREFR* mainEntrance = nullptr;
        for (auto* door : doors) {
            if (analyzer->IsDoorExterior(door)) {
                mainEntrance = door;
                break;
            }
        }

        if (!mainEntrance && !doors.empty()) {
            mainEntrance = doors[0];
        }

        if (!mainEntrance) return nullptr;

        // Find door furthest from main entrance
        RE::TESObjectREFR* backDoor = nullptr;
        float maxDistance = 0.0f;

        for (auto* door : doors) {
            if (door == mainEntrance) continue;

            float dist = door->GetPosition().GetDistance(mainEntrance->GetPosition());
            if (dist > maxDistance) {
                maxDistance = dist;
                backDoor = door;
            }
        }

        if (backDoor) {
            logger::debug("ResolveBack: Found back door -> '{}'",
                         analyzer->GetDoorDestinationName(backDoor).c_str());
        } else {
            logger::debug("ResolveBack: No back door found");
        }

        return backDoor;
    }

    RE::TESObjectREFR* LocationResolver::ResolveCellar(RE::Actor* actor) {
        // Cellar is a subset of downstairs — same passes (door name, furniture below,
        // door Z) without preferBeds. Delegate to avoid duplicating 3 passes.
        logger::debug("ResolveCellar: Delegating to ResolveDownstairs");
        return ResolveDownstairs(actor, false);
    }

    RE::TESObjectREFR* LocationResolver::ResolveBedroom(RE::Actor* actor) {
        if (!actor) return nullptr;

        auto* analyzer = CellAnalyzer::GetSingleton();
        auto* cell = actor->GetParentCell();
        if (!cell) return nullptr;

        logger::debug("ResolveBedroom: Scanning cell for beds");

        RE::TESObjectREFR* nearestBed = nullptr;
        float nearestDist = FLT_MAX;
        auto actorPos = actor->GetPosition();

        cell->ForEachReference([&](RE::TESObjectREFR& ref) -> RE::BSContainer::ForEachResult {
            if (ref.IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;

            if (analyzer->IsBedFurniture(&ref)) {
                float dist = ref.GetPosition().GetDistance(actorPos);
                if (dist < nearestDist) {
                    nearestDist = dist;
                    nearestBed = &ref;
                }
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (nearestBed) {
            auto* name = nearestBed->GetBaseObject() ? nearestBed->GetBaseObject()->GetName() : "bed";
            logger::debug("ResolveBedroom: Found bed -> '{}' (dist={:.0f})", name ? name : "unnamed", nearestDist);
        } else {
            logger::debug("ResolveBedroom: No bed found in cell");
        }

        return nearestBed;
    }

    RE::TESObjectREFR* LocationResolver::ResolveKitchen(RE::Actor* actor) {
        if (!actor) return nullptr;

        auto* analyzer = CellAnalyzer::GetSingleton();
        auto stations = analyzer->FindCookingStations(actor);

        if (stations.empty()) {
            logger::debug("ResolveKitchen: No cooking stations found in cell");
            return nullptr;
        }

        // Return nearest cooking station
        RE::TESObjectREFR* nearest = nullptr;
        float nearestDist = FLT_MAX;
        auto actorPos = actor->GetPosition();

        for (auto* station : stations) {
            float dist = station->GetPosition().GetDistance(actorPos);
            if (dist < nearestDist) {
                nearestDist = dist;
                nearest = station;
            }
        }

        if (nearest) {
            auto* name = nearest->GetBaseObject() ? nearest->GetBaseObject()->GetName() : "cooking station";
            logger::debug("ResolveKitchen: Found '{}' (dist={:.0f})", name ? name : "unnamed", nearestDist);
        }

        return nearest;
    }

    // =========================================================================
    // Water / River Resolution
    // =========================================================================

    RE::TESObjectREFR* LocationResolver::ResolveWater(RE::Actor* actor, bool preferInterior) {
        if (!actor) return nullptr;

        auto* currentCell = actor->GetParentCell();
        if (!currentCell) return nullptr;

        logger::debug("ResolveWater: preferInterior={}, cell='{}', interior={}",
                     preferInterior, currentCell->GetName() ? currentCell->GetName() : "unnamed",
                     currentCell->IsInteriorCell());

        // Keywords for interior water features (modded baths, hot springs)
        static const std::vector<std::string> interiorKeywords = {
            "bath", "tub", "pool", "jacuzzi", "hot spring", "basin", "wash"
        };

        // Keywords for exterior water landmarks
        static const std::vector<std::string> exteriorKeywords = {
            "dock", "pier", "boat", "canoe", "ship",
            "fish", "fishing",
            "bridge",
            "mill", "lumber",
            "waterfall",
            "river", "lake", "stream", "pond", "harbor", "harbour",
            "well", "fountain"
        };

        struct WaterCandidate {
            RE::TESObjectREFR* ref;
            float distSq;
        };

        auto actorPos = actor->GetPosition();

        // Helper: check if reference base name matches any keyword in a list
        auto matchesKeywords = [](RE::TESObjectREFR& ref,
                                  const std::vector<std::string>& keywords) -> bool {
            auto* baseObj = ref.GetBaseObject();
            if (!baseObj) return false;
            auto* name = baseObj->GetName();
            if (!name || strlen(name) == 0) return false;
            std::string lowerName = StringUtils::ToLowerStd(name);
            for (const auto& kw : keywords) {
                if (lowerName.find(kw) != std::string::npos) return true;
            }
            return false;
        };

        // Helper: scan a cell for references matching keywords, return nearest
        auto scanCell = [&](RE::TESObjectCELL* cell,
                           const std::vector<std::string>& keywords) -> RE::TESObjectREFR* {
            std::vector<WaterCandidate> candidates;
            cell->ForEachReference([&](RE::TESObjectREFR& ref) -> RE::BSContainer::ForEachResult {
                if (ref.IsDisabled() || ref.IsDeleted())
                    return RE::BSContainer::ForEachResult::kContinue;
                if (matchesKeywords(ref, keywords)) {
                    auto pos = ref.GetPosition();
                    float dx = pos.x - actorPos.x;
                    float dy = pos.y - actorPos.y;
                    candidates.push_back({&ref, dx * dx + dy * dy});
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });

            if (candidates.empty()) return nullptr;

            auto* best = &candidates[0];
            for (size_t i = 1; i < candidates.size(); i++) {
                if (candidates[i].distSq < best->distSq) best = &candidates[i];
            }

            auto* baseName = best->ref->GetBaseObject() ? best->ref->GetBaseObject()->GetName() : "unknown";
            logger::debug("ResolveWater: Found '{}' at distance {:.0f} ({} candidates)",
                         baseName, std::sqrt(best->distSq), candidates.size());
            return best->ref;
        };

        bool isInterior = currentCell->IsInteriorCell();

        // Strategy 1: Interior bath scan (if preferInterior and indoors)
        if (preferInterior && isInterior) {
            auto* result = scanCell(currentCell, interiorKeywords);
            if (result) return result;
            logger::debug("ResolveWater: No interior water found, no exterior fallback from indoors");
            return nullptr;
        }

        // Strategy 2: Exterior landmark scan
        if (!isInterior) {
            return scanCell(currentCell, exteriorKeywords);
        }

        // Interior + exterior-only request (e.g., "go swim" while indoors) — can't resolve
        logger::debug("ResolveWater: Indoor with exterior intent — cannot resolve");
        return nullptr;
    }

    // =========================================================================
    // NPC Home Index
    // =========================================================================

    void LocationResolver::BuildHomeIndex() {
        m_npcHomeIndex.clear();

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        auto* analyzer = CellAnalyzer::GetSingleton();
        if (!dataHandler || !analyzer) return;

        logger::info("Building NPC home index from bed ownership...");

        std::uint32_t bedsScanned = 0;
        std::uint32_t actorOwned = 0;
        std::uint32_t factionOwned = 0;

        for (auto* cell : dataHandler->interiorCells) {
            if (!cell) continue;

            cell->ForEachReference([&](RE::TESObjectREFR& ref) -> RE::BSContainer::ForEachResult {
                if (ref.IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;
                if (!analyzer->IsBedFurniture(&ref)) return RE::BSContainer::ForEachResult::kContinue;

                bedsScanned++;
                auto bedFormId = ref.GetFormID();
                auto cellFormId = cell->GetFormID();

                // Check direct actor ownership (e.g., a bed owned by Alvor specifically)
                auto* ownerBase = ref.GetActorOwner();
                if (ownerBase) {
                    auto ownerFormId = ownerBase->GetFormID();
                    // Only set if not already mapped (first bed wins — primary sleeping bed)
                    if (m_npcHomeIndex.find(ownerFormId) == m_npcHomeIndex.end()) {
                        m_npcHomeIndex[ownerFormId] = {cellFormId, bedFormId};
                        actorOwned++;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                // Check faction ownership (shared homes — Alvor + Sigrid via faction)
                auto* factionOwner = ref.GetFactionOwner();
                if (factionOwner) {
                    // Map all unique NPC members of this faction to this cell
                    for (auto* npcForm : dataHandler->GetFormArray<RE::TESNPC>()) {
                        if (!npcForm || !npcForm->IsUnique()) continue;
                        if (!npcForm->IsInFaction(factionOwner)) continue;

                        auto npcFormId = npcForm->GetFormID();
                        if (m_npcHomeIndex.find(npcFormId) == m_npcHomeIndex.end()) {
                            m_npcHomeIndex[npcFormId] = {cellFormId, bedFormId};
                            factionOwned++;
                        }
                    }
                }

                return RE::BSContainer::ForEachResult::kContinue;
            });
        }

        logger::info("NPC home index built: {} beds scanned, {} actor-owned, {} faction-mapped, {} total NPCs with homes",
                     bedsScanned, actorOwned, factionOwned, m_npcHomeIndex.size());
    }

    RE::TESObjectREFR* LocationResolver::FindExteriorDoorInCell(RE::TESObjectCELL* cell) {
        if (!cell) return nullptr;

        RE::TESObjectREFR* found = nullptr;
        cell->ForEachReference([&](RE::TESObjectREFR& ref) -> RE::BSContainer::ForEachResult {
            auto* baseObj = ref.GetBaseObject();
            if (!baseObj || !baseObj->Is(RE::FormType::Door) || ref.IsDisabled())
                return RE::BSContainer::ForEachResult::kContinue;

            auto* teleport = ref.extraList.GetByType<RE::ExtraTeleport>();
            if (!teleport || !teleport->teleportData)
                return RE::BSContainer::ForEachResult::kContinue;

            auto linkedDoor = teleport->teleportData->linkedDoor.get();
            if (!linkedDoor)
                return RE::BSContainer::ForEachResult::kContinue;

            auto* destCell = linkedDoor->GetParentCell();
            if (destCell && !destCell->IsInteriorCell()) {
                found = &ref;
                return RE::BSContainer::ForEachResult::kStop;
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        return found;
    }

    RE::TESObjectREFR* LocationResolver::GetExteriorLinkedDoor(RE::TESObjectREFR* interiorDoor) {
        if (!interiorDoor) return nullptr;
        auto* teleport = interiorDoor->extraList.GetByType<RE::ExtraTeleport>();
        if (!teleport || !teleport->teleportData) return nullptr;
        auto linked = teleport->teleportData->linkedDoor.get();
        return linked ? linked.get() : nullptr;
    }

    RE::TESObjectREFR* LocationResolver::GetPlayerHomeExteriorDoorRef() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;
        auto* cell = player->GetParentCell();
        if (!cell || !cell->IsInteriorCell()) return nullptr;
        return GetExteriorLinkedDoor(FindExteriorDoorInCell(cell));
    }

    RE::TESObjectREFR* LocationResolver::GetPlayerHomeInteriorDoorRef() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;
        auto* cell = player->GetParentCell();
        if (!cell || !cell->IsInteriorCell()) return nullptr;
        return FindExteriorDoorInCell(cell);
    }

    RE::TESObjectREFR* LocationResolver::GetLocationTravelTarget(RE::TESObjectCELL* homeCell) {
        if (!homeCell) return nullptr;

        // Prefer BGSLocation worldLocMarker — gives an exterior marker at the front door
        // that Skyrim's AI can always pathfind to.
        auto* location = homeCell->GetLocation();
        if (location) {
            // Try this location's marker
            auto markerPtr = location->worldLocMarker.get();
            if (markerPtr) {
                auto* marker = markerPtr.get();
                if (marker) return marker;
            }

            // Try parent location's marker (e.g., house → settlement)
            if (location->parentLoc) {
                auto parentPtr = location->parentLoc->worldLocMarker.get();
                if (parentPtr) {
                    auto* parentMarker = parentPtr.get();
                    if (parentMarker) return parentMarker;
                }
            }
        }

        // Fallback: find a door in the home cell that leads to an exterior
        // and return the exterior-side linked door as a travel target
        auto* exteriorDoor = GetExteriorLinkedDoor(FindExteriorDoorInCell(homeCell));
        if (exteriorDoor) return exteriorDoor;

        return nullptr;
    }

    // =========================================================================
    // Home Resolution
    // =========================================================================

    RE::TESObjectREFR* LocationResolver::ResolveNPCHome(RE::TESNPC* actorBase) {
        if (!actorBase) return nullptr;

        auto it = m_npcHomeIndex.find(actorBase->GetFormID());
        if (it == m_npcHomeIndex.end()) {
            logger::debug("ResolveNPCHome: No home indexed for '{}'",
                         actorBase->GetFullName() ? actorBase->GetFullName() : "unknown");
            return nullptr;
        }

        auto* homeCell = RE::TESForm::LookupByID<RE::TESObjectCELL>(it->second.cellFormId);
        if (!homeCell) {
            logger::warn("ResolveNPCHome: Home cell {:08X} not found for '{}'",
                        it->second.cellFormId,
                        actorBase->GetFullName() ? actorBase->GetFullName() : "unknown");
            return nullptr;
        }

        auto cellName = homeCell->GetName();
        logger::info("ResolveNPCHome: '{}' -> cell '{}' ({:08X})",
                    actorBase->GetFullName() ? actorBase->GetFullName() : "unknown",
                    cellName ? cellName : "unnamed", it->second.cellFormId);

        return GetLocationTravelTarget(homeCell);
    }

    RE::TESObjectREFR* LocationResolver::ResolvePlayerHome() {
        if (!m_locTypePlayerHouse) {
            logger::debug("ResolvePlayerHome: LocTypePlayerHouse keyword not cached");
            return nullptr;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!player || !dataHandler) return nullptr;

        auto playerPos = player->GetPosition();
        RE::TESObjectREFR* nearestMarker = nullptr;
        float nearestDistSq = FLT_MAX;

        for (auto* loc : dataHandler->GetFormArray<RE::BGSLocation>()) {
            if (!loc || !loc->HasKeyword(m_locTypePlayerHouse)) continue;

            auto markerPtr = loc->worldLocMarker.get();
            if (!markerPtr) continue;
            auto* marker = markerPtr.get();
            if (!marker) continue;

            // Skip interior-only markers
            auto* parentCell = marker->GetParentCell();
            if (parentCell && parentCell->IsInteriorCell()) continue;

            float dx = marker->GetPosition().x - playerPos.x;
            float dy = marker->GetPosition().y - playerPos.y;
            float distSq = dx * dx + dy * dy;

            if (distSq < nearestDistSq) {
                nearestDistSq = distSq;
                nearestMarker = marker;

                // If player is very close (within city), this is almost certainly the right one
                if (distSq < 10000.0f * 10000.0f) break;
            }
        }

        if (nearestMarker) {
            auto pos = nearestMarker->GetPosition();
            logger::info("ResolvePlayerHome: nearest player home at ({:.0f}, {:.0f}, {:.0f}), dist={:.0f}",
                        pos.x, pos.y, pos.z, std::sqrt(nearestDistSq));
        } else {
            logger::debug("ResolvePlayerHome: no player homes found with LocTypePlayerHouse keyword");
        }

        return nearestMarker;
    }

    RE::TESObjectREFR* LocationResolver::ResolveHome(RE::Actor* actor, const SemanticIntent& intent) {
        if (!actor) return nullptr;

        // Helper: store home cell ID for anti-trespass door unlock
        auto storeHomeCellId = [this](RE::TESNPC* actorBase) {
            if (!actorBase) return;
            auto it = m_npcHomeIndex.find(actorBase->GetFormID());
            if (it != m_npcHomeIndex.end()) {
                m_lastResolvedHomeCellId = it->second.cellFormId;
                logger::debug("ResolveHome: stored home cell {:08X} for anti-trespass",
                             m_lastResolvedHomeCellId);
            }
        };

        switch (intent.homeOwner) {
            case HomeOwner::NPC: {
                logger::debug("ResolveHome: NPC's own home");
                storeHomeCellId(actor->GetActorBase());
                auto* result = ResolveNPCHome(actor->GetActorBase());
                if (!result) {
                    // Fallback: use editor location
                    auto* editorLoc = actor->GetEditorLocation();
                    if (editorLoc) {
                        logger::debug("ResolveHome: falling back to editor location '{}'",
                                     editorLoc->GetFullName() ? editorLoc->GetFullName() : "unnamed");
                        return FindTravelTarget(editorLoc->GetFullName() ? editorLoc->GetFullName() : "");
                    }
                }
                return result;
            }

            case HomeOwner::PLAYER: {
                logger::debug("ResolveHome: player's home");
                // Player homes use LocTypePlayerHouse keyword — no cell ID to store
                return ResolvePlayerHome();
            }

            case HomeOwner::NAMED: {
                logger::debug("ResolveHome: named NPC's home -> '{}'", intent.homeOwnerHint);

                // Check if the name matches the player character.
                // The action LLM uses the player's name (e.g., "Dragonborn's home")
                // since template variables aren't available in dynamic params.
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (player) {
                    auto playerName = player->GetDisplayFullName();
                    if (playerName && strlen(playerName) > 0) {
                        std::string lowerPlayer = StringUtils::ToLowerStd(playerName);
                        if (lowerPlayer == intent.homeOwnerHint) {
                            logger::debug("ResolveHome: '{}' is the player — routing to player home",
                                         intent.homeOwnerHint);
                            return ResolvePlayerHome();
                        }
                    }
                }

                // Not the player — look up the NPC
                auto* npcIndex = NPCIndex::GetSingleton();
                auto* targetActor = npcIndex->FindByNameNear(intent.homeOwnerHint, actor, true);
                if (targetActor) {
                    storeHomeCellId(targetActor->GetActorBase());
                    auto* result = ResolveNPCHome(targetActor->GetActorBase());
                    if (result) return result;

                    // Fallback: editor location of the named NPC
                    auto* editorLoc = targetActor->GetEditorLocation();
                    if (editorLoc) {
                        logger::debug("ResolveHome: falling back to '{}' editor location",
                                     intent.homeOwnerHint);
                        return FindTravelTarget(editorLoc->GetFullName() ? editorLoc->GetFullName() : "");
                    }
                }
                logger::debug("ResolveHome: NPC '{}' not found", intent.homeOwnerHint);
                return nullptr;
            }

            default:
                return nullptr;
        }
    }

    // =========================================================================
    // Unified Destination Resolution
    // =========================================================================

    SemanticIntent LocationResolver::DetectSemanticIntent(const std::string& destination) {
        std::string lower = StringUtils::ToLowerStd(destination);

        // Helper: check if string mentions bed/bedroom
        auto hasBedContext = [&lower]() {
            return lower.find("bedroom") != std::string::npos ||
                   lower.find("my room") != std::string::npos ||
                   lower.find("my bed") != std::string::npos;
        };

        // === Exact semantic terms (fast path) ===
        if (lower == "upstairs" || lower == "up" || lower == "above")
            return {SemanticIntent::UPSTAIRS, "", false};
        if (lower == "downstairs" || lower == "down" || lower == "below")
            return {SemanticIntent::DOWNSTAIRS, "", false};
        if (lower == "outside" || lower == "out" || lower == "exterior")
            return {SemanticIntent::OUTSIDE, "", false};
        if (lower == "inside" || lower == "in" || lower == "interior")
            return {SemanticIntent::INSIDE, "", false};
        if (lower == "the back" || lower == "back room" || lower == "back")
            return {SemanticIntent::BACK, "", false};
        if (lower == "cellar" || lower == "basement" || lower == "below stairs")
            return {SemanticIntent::CELLAR, "", false};

        // === Stairs terms (exact) — map to DOWNSTAIRS (NPC goes toward stairwell area) ===
        if (lower == "stairs" || lower == "stairwell" || lower == "staircase")
            return {SemanticIntent::DOWNSTAIRS, "", false};

        // === Bedroom terms (exact) ===
        if (lower == "bedroom" || lower == "the bedroom" || lower == "my room" ||
            lower == "my bed" || lower == "bed" || lower == "room")
            return {SemanticIntent::BEDROOM, "", true};

        // === Kitchen terms (exact) ===
        if (lower == "kitchen" || lower == "the kitchen")
            return {SemanticIntent::KITCHEN, "", false};

        // === Water/bath terms (exact) — interior preferred ===
        if (lower == "bath" || lower == "the bath" || lower == "bathe" ||
            lower == "hot spring" || lower == "hot springs" || lower == "pool" ||
            lower == "the pool")
            return {SemanticIntent::WATER, "", false, HomeOwner::NONE, "", true};

        // === Water terms (exact) — exterior ===
        if (lower == "the river" || lower == "river" || lower == "the lake" ||
            lower == "lake" || lower == "stream" || lower == "the stream" ||
            lower == "the water" || lower == "water" || lower == "the shore")
            return {SemanticIntent::WATER, "", false};

        // === Water action phrases ===
        if (lower == "wash" || lower == "wash up" || lower == "go wash" ||
            lower == "clean up" || lower == "take a bath" ||
            lower.find("wash yourself") != std::string::npos ||
            lower.find("wash myself") != std::string::npos ||
            lower.find("clean yourself") != std::string::npos)
            return {SemanticIntent::WATER, "", false, HomeOwner::NONE, "", true};

        if (lower == "swim" || lower == "go swim" || lower == "go swimming" ||
            lower == "go fishing" || lower == "fishing" ||
            lower.find("go to the river") != std::string::npos ||
            lower.find("go to the lake") != std::string::npos)
            return {SemanticIntent::WATER, "", false};

        // === Home terms (exact) — NPC's own home ===
        if (lower == "home" || lower == "my home" || lower == "my house" || lower == "my place" ||
            lower == "go home" || lower == "head home" || lower == "back home")
            return {SemanticIntent::HOME, "", false, HomeOwner::NPC, ""};

        // === Home terms (exact) — player's home ===
        if (lower == "your home" || lower == "your house" || lower == "your place" ||
            lower == "the player's home" || lower == "player's home")
            return {SemanticIntent::HOME, "", false, HomeOwner::PLAYER, ""};

        // === Possessive and bare-name home patterns ===
        // Matches: "Alvor's home", "Camilla Valerius's house", "Camilla Valerius home"
        {
            std::vector<std::string> homeSuffixes = {
                "'s home", "'s house", "'s place",   // possessive: "Alvor's home"
                " home", " house", " place"          // bare: "Camilla Valerius home"
            };

            // Words that precede "home" but are NOT names — prevents
            // "go home", "back home", "head home" from being misdetected.
            std::unordered_set<std::string> nonNameWords = {
                "go", "my", "your", "head", "back", "the", "get",
                "at", "to", "a", "our", "their", "its", "this"
            };

            for (const auto& suffix : homeSuffixes) {
                auto pos = lower.find(suffix);
                if (pos == std::string::npos || pos == 0) continue;

                std::string ownerName = lower.substr(0, pos);

                // Strip leading travel prefixes
                std::vector<std::string> travelPrefixes = {"go to ", "head to ", "travel to ", "get to "};
                for (const auto& tp : travelPrefixes) {
                    if (ownerName.length() > tp.length() && ownerName.substr(0, tp.length()) == tp) {
                        ownerName = ownerName.substr(tp.length());
                        break;
                    }
                }
                ownerName = StringUtils::TrimStd(ownerName);
                if (ownerName.empty()) continue;

                // For bare suffixes (no apostrophe), reject single non-name words
                // to avoid "go home" → NAMED("go") or "my home" → NAMED("my").
                // Multi-word names like "camilla valerius" are always accepted.
                bool isBare = (suffix.front() == ' ');
                if (isBare && ownerName.find(' ') == std::string::npos) {
                    if (nonNameWords.count(ownerName)) continue;
                }

                return {SemanticIntent::HOME, "", false, HomeOwner::NAMED, ownerName};
            }
        }

        // === Compound phrase patterns ===

        // "out of X" / "outside of X" / "outside X" → OUTSIDE + context
        std::vector<std::string> outsidePrefixes = {"out of ", "outside of ", "outside "};
        for (const auto& prefix : outsidePrefixes) {
            if (lower.length() > prefix.length() && lower.substr(0, prefix.length()) == prefix) {
                std::string context = lower.substr(prefix.length());
                if (context == "here" || context == "this place" || context == "this building")
                    return {SemanticIntent::OUTSIDE, "", false};
                return {SemanticIntent::OUTSIDE, context, false};
            }
        }

        // "leave" / "get out" / "away" → OUTSIDE (no context)
        if (lower == "leave" || lower == "get out" || lower == "get out of here" || lower == "away")
            return {SemanticIntent::OUTSIDE, "", false};

        // "leave X" → OUTSIDE + context
        if (lower.length() > 6 && lower.substr(0, 6) == "leave ") {
            std::string context = lower.substr(6);
            if (context == "here" || context == "this place")
                return {SemanticIntent::OUTSIDE, "", false};
            return {SemanticIntent::OUTSIDE, context, false};
        }

        // === Embedded keywords (catches "go upstairs", "head downstairs", etc.) ===
        // Also detects bed context for compound phrases like "upstairs bedroom"
        if (lower.find("upstairs") != std::string::npos || lower.find("upper floor") != std::string::npos)
            return {SemanticIntent::UPSTAIRS, "", hasBedContext()};
        if (lower.find("downstairs") != std::string::npos || lower.find("basement") != std::string::npos ||
            lower.find("cellar") != std::string::npos || lower.find("stairwell") != std::string::npos ||
            lower.find("staircase") != std::string::npos || lower.find("stairs") != std::string::npos)
            return {SemanticIntent::DOWNSTAIRS, "", hasBedContext()};
        if (lower.find("outside") != std::string::npos || lower.find("exterior") != std::string::npos)
            return {SemanticIntent::OUTSIDE, "", false};

        // === Bedroom embedded (catches "the bedroom", "go to bed")
        if (lower.find("bedroom") != std::string::npos || lower.find("my bed") != std::string::npos)
            return {SemanticIntent::BEDROOM, "", true};

        // === Kitchen embedded (catches "the kitchen", "go to the kitchen")
        if (lower.find("kitchen") != std::string::npos)
            return {SemanticIntent::KITCHEN, "", false};

        // === Water embedded (catches "head to the river", "find a river") ===
        if (lower.find("river") != std::string::npos || lower.find("lake") != std::string::npos ||
            lower.find("swim") != std::string::npos || lower.find("fishing") != std::string::npos)
            return {SemanticIntent::WATER, "", false};
        if (lower.find("bath") != std::string::npos || lower.find("hot spring") != std::string::npos)
            return {SemanticIntent::WATER, "", false, HomeOwner::NONE, "", true};

        // === Home embedded — catch phrases like "go to my home", "head to your place" ===
        if (lower.find("my home") != std::string::npos || lower.find("my house") != std::string::npos ||
            lower.find("my place") != std::string::npos)
            return {SemanticIntent::HOME, "", false, HomeOwner::NPC, ""};
        if (lower.find("your home") != std::string::npos || lower.find("your house") != std::string::npos ||
            lower.find("your place") != std::string::npos)
            return {SemanticIntent::HOME, "", false, HomeOwner::PLAYER, ""};
        // Bare "home" as embedded keyword (e.g., "head home now") — NPC's own home
        // Must be a standalone word — not part of a compound like "breezehome" or "homebrew"
        {
            auto homePos = lower.find("home");
            if (homePos != std::string::npos) {
                bool wordStart = (homePos == 0 || !std::isalpha(static_cast<unsigned char>(lower[homePos - 1])));
                bool wordEnd = (homePos + 4 >= lower.length() || !std::isalpha(static_cast<unsigned char>(lower[homePos + 4])));
                if (wordStart && wordEnd)
                    return {SemanticIntent::HOME, "", false, HomeOwner::NPC, ""};
            }
        }

        return {SemanticIntent::NONE, "", false};
    }

    RE::TESObjectREFR* LocationResolver::ResolveSemantic(RE::Actor* actor, const SemanticIntent& intent) {
        switch (intent.type) {
            case SemanticIntent::UPSTAIRS:   return ResolveUpstairs(actor, intent.preferBeds);
            case SemanticIntent::DOWNSTAIRS: return ResolveDownstairs(actor, intent.preferBeds);
            case SemanticIntent::OUTSIDE:    return ResolveOutside(actor);
            case SemanticIntent::INSIDE:     return ResolveInside(actor);
            case SemanticIntent::BACK:       return ResolveBack(actor);
            case SemanticIntent::CELLAR:     return ResolveCellar(actor);
            case SemanticIntent::BEDROOM:    return ResolveBedroom(actor);
            case SemanticIntent::KITCHEN:    return ResolveKitchen(actor);
            case SemanticIntent::HOME:       return ResolveHome(actor, intent);
            case SemanticIntent::WATER:      return ResolveWater(actor, intent.preferInterior);
            default: return nullptr;
        }
    }

    RE::TESObjectREFR* LocationResolver::ResolveViaParentLocation(RE::Actor* actor) {
        if (!actor) return nullptr;

        auto* loc = actor->GetCurrentLocation();
        if (!loc) return nullptr;

        auto* parent = loc->parentLoc;
        if (!parent) return nullptr;

        auto name = parent->GetFullName();
        if (!name || strlen(name) == 0) return nullptr;

        logger::debug("  ResolveViaParentLocation: Trying parent '{}'", name);
        return FindTravelTarget(name);
    }

    std::string LocationResolver::ExtractLocationName(const std::string& text) {
        std::string lower = StringUtils::ToLowerStd(text);

        std::vector<std::string> prefixes = {
            "out of ", "outside of ", "outside ", "leave ",
            "go to ", "head to ", "get to ", "travel to "
        };

        for (const auto& prefix : prefixes) {
            if (lower.length() > prefix.length() && lower.substr(0, prefix.length()) == prefix) {
                std::string remainder = lower.substr(prefix.length());
                if (remainder.empty() || remainder == "here" || remainder == "this place")
                    continue;
                return remainder;
            }
        }

        return "";
    }

    RE::TESObjectREFR* LocationResolver::ResolveAnyDestination(RE::Actor* actor, const std::string& destination) {
        if (!actor || destination.empty()) return nullptr;

        // Reset home cell tracking — only set if this destination resolves to a home
        m_lastResolvedHomeCellId = 0;

        logger::debug("ResolveAnyDestination: '{}' for '{}'", destination, actor->GetDisplayFullName());

        // Phase 1: Detect semantic intent
        auto intent = DetectSemanticIntent(destination);

        if (intent.type != SemanticIntent::NONE) {
            logger::debug("  Semantic intent: type={}, context='{}', preferBeds={}, homeOwner={}",
                         static_cast<int>(intent.type), intent.locationContext, intent.preferBeds,
                         static_cast<int>(intent.homeOwner));

            // Phase 2-HOME: Home resolution uses the home index, not door scanning.
            // Resolve immediately and return — no fallthrough to fuzzy search.
            if (intent.type == SemanticIntent::HOME) {
                auto* homeResult = ResolveHome(actor, intent);
                if (homeResult) {
                    logger::debug("  Resolved via home index");
                    return homeResult;
                }
                logger::debug("  Home resolution failed — no home found");
                return nullptr;
            }

            // Phase 2a: Parent BGSLocation (outside only — preferred)
            // "Outside" resolves to the parent city/town name (e.g., "Whiterun" for Bannered Mare)
            // so the NPC gets an exterior world marker and pathfinds there normally.
            if (intent.type == SemanticIntent::OUTSIDE) {
                auto* parentResult = ResolveViaParentLocation(actor);
                if (parentResult) {
                    logger::debug("  Resolved 'outside' via parent BGSLocation");
                    return parentResult;
                }
            }

            // Log all doors for diagnostics
            LogAllDoors(actor);

            // Phase 2b: Semantic resolution (door scanning + furniture fallback)
            // Primary path for upstairs/downstairs/back room, fallback for outside
            auto* doorResult = ResolveSemantic(actor, intent);
            if (doorResult) {
                logger::debug("  Resolved via door scanning");
                return doorResult;
            }

            // Phase 2c: Location context from compound phrase
            if (!intent.locationContext.empty()) {
                logger::debug("  Trying location context: '{}'", intent.locationContext);
                auto* contextResult = FindTravelTarget(intent.locationContext);
                if (contextResult) {
                    logger::debug("  Resolved via location context");
                    return contextResult;
                }
            }

            logger::debug("  Semantic resolution exhausted");
        }

        // Phase 2.5: NPC name as destination → resolve to their home
        // Catches "Camilla", "Camilla Valerius", "Alvor" used as a bare destination.
        // Only when semantic intent was NONE (not already handled as home).
        if (intent.type == SemanticIntent::NONE) {
            auto* npcIndex = NPCIndex::GetSingleton();
            auto* targetActor = npcIndex->FindByNameNear(destination, actor, true);
            if (targetActor) {
                auto* targetBase = targetActor->GetActorBase();
                if (targetBase) {
                    auto it = m_npcHomeIndex.find(targetBase->GetFormID());
                    if (it != m_npcHomeIndex.end()) {
                        m_lastResolvedHomeCellId = it->second.cellFormId;
                        logger::debug("  NPC name '{}' → home cell {:08X}", destination,
                                     m_lastResolvedHomeCellId);
                        auto* homeResult = ResolveNPCHome(targetBase);
                        if (homeResult) {
                            logger::debug("  Resolved NPC name as home destination");
                            return homeResult;
                        }
                    }
                    // NPC found but no home — fall through to named location
                    // (destination might coincidentally be a location name too)
                    logger::debug("  NPC '{}' found but no home indexed, falling through", destination);
                }
            }
        }

        // Phase 3: Named location resolution
        logger::debug("  Trying named location: '{}'", destination);
        auto* namedResult = FindTravelTarget(destination);
        if (namedResult) {
            logger::debug("  Resolved via named location");
            return namedResult;
        }

        // Phase 4: Extract location name from string (last resort)
        auto extracted = ExtractLocationName(destination);
        if (!extracted.empty()) {
            logger::debug("  Trying extracted location: '{}'", extracted);
            auto* extractedResult = FindTravelTarget(extracted);
            if (extractedResult) {
                logger::debug("  Resolved via extracted location name");
                return extractedResult;
            }
        }

        logger::debug("  ResolveAnyDestination: FAILED for '{}'", destination);
        return nullptr;
    }

    // =========================================================================
    // Legacy Functions
    // =========================================================================

    bool LocationResolver::IsSemanticTerm(const std::string& term) {
        std::string lowerTerm = StringUtils::ToLowerStd(term);
        return m_semanticTerms.find(lowerTerm) != m_semanticTerms.end();
    }

    std::vector<RE::BSFixedString> LocationResolver::GetAvailableDirections(RE::Actor* actor) {
        std::vector<RE::BSFixedString> result;
        if (!actor) return result;

        auto* analyzer = CellAnalyzer::GetSingleton();
        auto doors = analyzer->GetDoors(actor);

        bool hasUpstairs = false;
        bool hasDownstairs = false;
        bool hasOutside = false;
        bool hasInside = false;

        for (auto* door : doors) {
            if (analyzer->IsDoorUpward(door)) hasUpstairs = true;
            if (analyzer->IsDoorDownward(door)) hasDownstairs = true;
            if (analyzer->IsDoorExterior(door)) hasOutside = true;
            if (!analyzer->IsDoorExterior(door)) hasInside = true;
        }

        if (hasUpstairs) result.push_back("upstairs");
        if (hasDownstairs) result.push_back("downstairs");

        auto* cell = actor->GetParentCell();
        if (cell && cell->IsInteriorCell() && hasOutside) {
            result.push_back("outside");
        }
        if (cell && !cell->IsInteriorCell() && hasInside) {
            result.push_back("inside");
        }

        return result;
    }

    RE::BSFixedString LocationResolver::GetActorLocationName(RE::Actor* actor) {
        if (!actor) return "";

        auto* cell = actor->GetParentCell();
        if (!cell) return "";

        // 1. Try cell display name
        auto cellName = cell->GetName();
        if (cellName && strlen(cellName) > 0) {
            return cellName;
        }

        // 2. Fallback: BGSLocation name (works for modded interiors in exterior cells)
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

        return "Unknown Location";
    }

    RE::BSFixedString LocationResolver::GetLocationSuggestion(const std::string& searchTerm) {
        std::shared_lock lock(m_mutex);

        if (searchTerm.empty()) return "";

        std::string lowerSearch = StringUtils::ToLowerStd(searchTerm);

        // Find closest match across both cells and locations
        auto cellFuzzy = StringUtils::FuzzyFind(lowerSearch, m_allCellNames, 5);
        auto locFuzzy = StringUtils::FuzzyFind(lowerSearch, m_allLocationNames, 5);

        // Pick the closer match
        if (cellFuzzy && (!locFuzzy || cellFuzzy.distance <= locFuzzy.distance)) {
            return RE::BSFixedString(cellFuzzy.match);
        }
        if (locFuzzy) {
            return RE::BSFixedString(locFuzzy.match);
        }

        return "";
    }

    RE::BSFixedString LocationResolver::GetStatsJSON() {
        std::shared_lock lock(m_mutex);

        nlohmann::json stats;
        stats["cellCount"] = m_allCellNames.size();
        stats["locationCount"] = m_allLocationNames.size();
        stats["semanticTermCount"] = m_semanticTerms.size();
        stats["npcHomesIndexed"] = m_npcHomeIndex.size();
        stats["indexBuilt"] = m_indexBuilt;

        return RE::BSFixedString(stats.dump());
    }

    RE::TESObjectREFR* LocationResolver::FindNearestWaypointToward(
            RE::Actor* actor, RE::TESObjectREFR* destination, float maxRadius) {
        if (!actor || !destination) return nullptr;

        auto actorPos = actor->GetPosition();
        auto destPos = destination->GetPosition();

        // Actor's distance to destination (2D — Z irrelevant for route planning)
        float actorToDestX = destPos.x - actorPos.x;
        float actorToDestY = destPos.y - actorPos.y;
        float actorToDestSq = actorToDestX * actorToDestX + actorToDestY * actorToDestY;

        float maxRadiusSq = maxRadius * maxRadius;
        float bestDistSq = FLT_MAX;
        RE::TESObjectREFR* bestMarker = nullptr;

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return nullptr;

        for (auto* loc : dataHandler->GetFormArray<RE::BGSLocation>()) {
            if (!loc) continue;

            auto markerPtr = loc->worldLocMarker.get();
            if (!markerPtr) continue;

            auto* marker = markerPtr.get();
            if (!marker) continue;

            // Skip interior-only markers
            auto* parentCell = marker->GetParentCell();
            if (parentCell && parentCell->IsInteriorCell()) continue;

            auto markerPos = marker->GetPosition();

            // Distance from actor to marker (2D)
            float dx = markerPos.x - actorPos.x;
            float dy = markerPos.y - actorPos.y;
            float distSq = dx * dx + dy * dy;

            // Must be within search radius
            if (distSq > maxRadiusSq) continue;

            // Must be closer to destination than the actor is
            float markerToDestX = destPos.x - markerPos.x;
            float markerToDestY = destPos.y - markerPos.y;
            float markerToDestSq = markerToDestX * markerToDestX + markerToDestY * markerToDestY;
            if (markerToDestSq >= actorToDestSq) continue;

            // Track closest qualifying marker
            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                bestMarker = marker;
            }
        }

        if (bestMarker) {
            auto pos = bestMarker->GetPosition();
            logger::debug("FindNearestWaypointToward: found marker at ({:.0f}, {:.0f}, {:.0f}), "
                          "dist={:.0f}", pos.x, pos.y, pos.z, std::sqrt(bestDistSq));
        }

        return bestMarker;
    }

    // =========================================================================
    // Home Door Access (anti-trespass + pathfinding)
    // =========================================================================

    RE::TESObjectREFR* LocationResolver::SetHomeDoorAccess(RE::Actor* npc, bool unlock) {
        if (!npc) return nullptr;

        auto* actorBase = npc->GetActorBase();
        if (!actorBase) return nullptr;

        auto it = m_npcHomeIndex.find(actorBase->GetFormID());
        if (it == m_npcHomeIndex.end()) {
            logger::debug("SetHomeDoorAccess: No home indexed for '{}'",
                         actorBase->GetFullName() ? actorBase->GetFullName() : "unknown");
            return nullptr;
        }

        // Store the cell ID so Papyrus can query it via GetLastResolvedHomeCellId()
        m_lastResolvedHomeCellId = it->second.cellFormId;

        return SetHomeDoorAccessForCell(it->second.cellFormId, unlock);
    }

    RE::TESObjectREFR* LocationResolver::SetHomeDoorAccessForCell(RE::FormID cellFormId, bool unlock) {
        if (cellFormId == 0) return nullptr;

        auto* homeCell = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellFormId);
        if (!homeCell) {
            logger::warn("SetHomeDoorAccessForCell: Cell {:08X} not found", cellFormId);
            return nullptr;
        }

        auto cellName = homeCell->GetName();
        auto* interiorDoor = FindExteriorDoorInCell(homeCell);

        if (!interiorDoor) {
            logger::debug("SetHomeDoorAccessForCell: No exterior door in cell '{}' ({:08X})",
                         cellName ? cellName : "unnamed", cellFormId);
            return nullptr;
        }

        if (unlock) {
            // Fast path: if door is already unlocked AND cell is already public,
            // check if WE made it public (refCount > 0) or if it's naturally public.
            // If we made it public, bump refCount so restore doesn't fire too early.
            // If naturally public, skip entirely.
            bool doorAlreadyOpen = !interiorDoor->IsLocked();
            bool cellAlreadyPublic = homeCell->cellFlags.any(RE::TESObjectCELL::Flag::kPublicArea);
            if (doorAlreadyOpen && cellAlreadyPublic) {
                auto existingIt = m_cellOriginalStates.find(cellFormId);
                if (existingIt != m_cellOriginalStates.end()) {
                    // We previously unlocked this cell — another slot also needs it.
                    // Bump refCount so restore waits for all slots to release.
                    existingIt->second.refCount++;
                    logger::debug("SetHomeDoorAccess: Cell '{}' ({:08X}) already open by us, refCount -> {}",
                                 cellName ? cellName : "unnamed", cellFormId, existingIt->second.refCount);
                } else {
                    // Naturally public (inn, shop) — nothing to track
                    logger::debug("SetHomeDoorAccess: Cell '{}' ({:08X}) already open & public, skipping",
                                 cellName ? cellName : "unnamed", cellFormId);
                }
                return interiorDoor;
            }

            // Save original state BEFORE modifying — so we only restore what we changed.
            // Without this, public locations (inns, shops) get set PRIVATE on cleanup,
            // triggering the vanilla trespass system on buildings that were never restricted.
            //
            // Reference-counted: multiple slots can share the same cell. Only save state
            // on first unlock; increment refCount on subsequent unlocks. Restore only
            // happens when refCount drops to zero (last slot releases the cell).
            auto stateIt = m_cellOriginalStates.find(cellFormId);
            bool firstUnlock = (stateIt == m_cellOriginalStates.end());

            if (firstUnlock) {
                CellOriginalState origState;
                origState.cellWasPublic = homeCell->cellFlags.any(RE::TESObjectCELL::Flag::kPublicArea);
                origState.interiorDoorWasLocked = interiorDoor->IsLocked();
                origState.refCount = 1;

                // Check exterior door lock state before modifying
                auto* teleport = interiorDoor->extraList.GetByType<RE::ExtraTeleport>();
                if (teleport && teleport->teleportData) {
                    auto linkedDoor = teleport->teleportData->linkedDoor.get();
                    if (linkedDoor) {
                        auto* extDoor = linkedDoor.get();
                        if (extDoor) {
                            origState.exteriorDoorWasLocked = extDoor->IsLocked();
                        }
                    }
                }

                m_cellOriginalStates[cellFormId] = origState;
            } else {
                // Another slot is also using this cell — just bump the ref count
                stateIt->second.refCount++;
                logger::debug("SetHomeDoorAccess: Cell '{}' ({:08X}) refCount incremented to {}",
                             cellName ? cellName : "unnamed", cellFormId, stateIt->second.refCount);
            }

            // Unlock interior door for pathfinding
            if (interiorDoor->IsLocked()) {
                auto* lock = interiorDoor->GetLock();
                if (lock) {
                    lock->SetLocked(false);
                    logger::info("SetHomeDoorAccess: Unlocked door in '{}'", cellName ? cellName : "unnamed");
                }
            }

            // Also unlock the exterior-side linked door (NPCs approach from outside)
            auto* teleport = interiorDoor->extraList.GetByType<RE::ExtraTeleport>();
            if (teleport && teleport->teleportData) {
                auto linkedDoor = teleport->teleportData->linkedDoor.get();
                if (linkedDoor) {
                    auto* extDoor = linkedDoor.get();
                    if (extDoor && extDoor->IsLocked()) {
                        auto* extLock = extDoor->GetLock();
                        if (extLock) {
                            extLock->SetLocked(false);
                            logger::info("SetHomeDoorAccess: Unlocked exterior door");
                        }
                    }
                }
            }

            // Make cell public (no trespass) — always set, idempotent
            if (!homeCell->cellFlags.any(RE::TESObjectCELL::Flag::kPublicArea)) {
                homeCell->SetPublic(true);
                logger::info("SetHomeDoorAccess: Set cell '{}' ({:08X}) PUBLIC for anti-trespass",
                            cellName ? cellName : "unnamed", cellFormId);
            } else {
                logger::info("SetHomeDoorAccess: Cell '{}' ({:08X}) already public, no change needed",
                            cellName ? cellName : "unnamed", cellFormId);
            }
        } else {
            // Restore to original state — only when last user releases the cell
            auto stateIt = m_cellOriginalStates.find(cellFormId);
            if (stateIt == m_cellOriginalStates.end()) {
                // No saved state — we never unlocked this cell, so don't touch it
                logger::warn("SetHomeDoorAccess: No saved state for cell {:08X}, skipping restore", cellFormId);
                return interiorDoor;
            }

            // Decrement ref count — only restore when last slot releases
            stateIt->second.refCount--;
            if (stateIt->second.refCount > 0) {
                logger::debug("SetHomeDoorAccess: Cell '{}' ({:08X}) refCount decremented to {}, keeping public",
                             cellName ? cellName : "unnamed", cellFormId, stateIt->second.refCount);
                return interiorDoor;
            }

            const auto& origState = stateIt->second;

            // Only re-lock interior door if it was originally locked
            if (origState.interiorDoorWasLocked) {
                auto* lock = interiorDoor->GetLock();
                if (lock) {
                    lock->SetLocked(true);
                    logger::info("SetHomeDoorAccess: Re-locked door in '{}'", cellName ? cellName : "unnamed");
                }
            }

            // Only re-lock exterior door if it was originally locked
            if (origState.exteriorDoorWasLocked) {
                auto* teleport = interiorDoor->extraList.GetByType<RE::ExtraTeleport>();
                if (teleport && teleport->teleportData) {
                    auto linkedDoor = teleport->teleportData->linkedDoor.get();
                    if (linkedDoor) {
                        auto* extDoor = linkedDoor.get();
                        if (extDoor) {
                            auto* extLock = extDoor->GetLock();
                            if (extLock) {
                                extLock->SetLocked(true);
                                logger::info("SetHomeDoorAccess: Re-locked exterior door");
                            }
                        }
                    }
                }
            }

            // Only restore private if cell was originally private
            if (!origState.cellWasPublic) {
                homeCell->SetPublic(false);
                logger::info("SetHomeDoorAccess: Set cell '{}' ({:08X}) PRIVATE (restored original)",
                            cellName ? cellName : "unnamed", cellFormId);
            } else {
                logger::info("SetHomeDoorAccess: Cell '{}' ({:08X}) was originally public, leaving as-is",
                            cellName ? cellName : "unnamed", cellFormId);
            }

            m_cellOriginalStates.erase(stateIt);
        }

        return interiorDoor;
    }

}  // namespace IntelEngine
