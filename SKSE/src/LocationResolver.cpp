/**
 * Location Resolver Implementation
 *
 * Resolves named locations and semantic terms to travel destinations.
 * No external database dependencies - builds index from game data.
 */

#include "LocationResolver.h"
#include "CellAnalyzer.h"
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

        m_indexBuilt = true;
        logger::info("Location index built: {} cells, {} locations",
                     m_allCellNames.size(), m_allLocationNames.size());
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

        // 2. Fuzzy match
        auto fuzzy = StringUtils::FuzzyFind(lowerName, m_allCellNames,
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

        // 2. Fuzzy match
        auto fuzzy = StringUtils::FuzzyFind(lowerName, m_allLocationNames,
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

        // Pass 3: Door Z-coordinate (last resort — can misfire with balcony doors)
        for (auto* door : doors) {
            if (analyzer->IsDoorUpward(door)) {
                logger::debug("ResolveUpstairs: Door Z match -> '{}'",
                             analyzer->GetDoorDestinationName(door).c_str());
                return door;
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

        // Pass 3: Door Z-coordinate (last resort)
        for (auto* door : doors) {
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
        for (auto* door : doors) {
            auto* destDoor = analyzer->GetDoorDestination(door);
            if (!destDoor) continue;

            auto* destCell = destDoor->GetParentCell();
            if (!destCell) continue;

            // Scan destination cell for exterior doors
            bool destHasExterior = false;
            destCell->ForEachReference([&](RE::TESObjectREFR& ref) -> RE::BSContainer::ForEachResult {
                auto* baseObj = ref.GetBaseObject();
                if (!baseObj || !baseObj->Is(RE::FormType::Door) || ref.IsDisabled()) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                auto* refTeleport = ref.extraList.GetByType<RE::ExtraTeleport>();
                if (!refTeleport || !refTeleport->teleportData) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                auto linkedDoor = refTeleport->teleportData->linkedDoor.get();
                if (!linkedDoor) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                auto* linkedCell = linkedDoor->GetParentCell();
                if (linkedCell && !linkedCell->IsInteriorCell()) {
                    destHasExterior = true;
                    return RE::BSContainer::ForEachResult::kStop;
                }

                return RE::BSContainer::ForEachResult::kContinue;
            });

            if (destHasExterior) {
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
        auto* analyzer = CellAnalyzer::GetSingleton();
        auto doors = analyzer->GetDoors(actor);

        logger::debug("ResolveCellar: Scanning {} doors", doors.size());

        // Pass 1: Door name match (strongest signal — "Honeyside Cellar")
        for (auto* door : doors) {
            if (analyzer->IsDoorNameDownward(door)) {
                logger::debug("ResolveCellar: Door name match -> '{}'",
                             analyzer->GetDoorDestinationName(door).c_str());
                return door;
            }
        }

        // Pass 2: Furniture on lower floor (same-cell fallback)
        auto furniture = analyzer->FindFurnitureBelow(actor);
        if (!furniture.empty()) {
            auto* first = furniture[0];
            auto* name = first->GetBaseObject() ? first->GetBaseObject()->GetName() : "furniture";
            logger::debug("ResolveCellar: Furniture below -> '{}'", name ? name : "unnamed");
            return first;
        }

        // Pass 3: Door Z-coordinate (door going down — last resort)
        for (auto* door : doors) {
            if (analyzer->IsDoorDownward(door)) {
                logger::debug("ResolveCellar: Door Z match -> '{}'",
                             analyzer->GetDoorDestinationName(door).c_str());
                return door;
            }
        }

        logger::debug("ResolveCellar: No matching target found");
        return nullptr;
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

        // === Bedroom terms (exact) ===
        if (lower == "bedroom" || lower == "the bedroom" || lower == "my room" ||
            lower == "my bed" || lower == "bed")
            return {SemanticIntent::BEDROOM, "", true};

        // === Kitchen terms (exact) ===
        if (lower == "kitchen" || lower == "the kitchen")
            return {SemanticIntent::KITCHEN, "", false};

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
            lower.find("cellar") != std::string::npos)
            return {SemanticIntent::DOWNSTAIRS, "", hasBedContext()};
        if (lower.find("outside") != std::string::npos || lower.find("exterior") != std::string::npos)
            return {SemanticIntent::OUTSIDE, "", false};

        // === Bedroom embedded (catches "the bedroom", "go to bed")
        if (lower.find("bedroom") != std::string::npos || lower.find("my bed") != std::string::npos)
            return {SemanticIntent::BEDROOM, "", true};

        // === Kitchen embedded (catches "the kitchen", "go to the kitchen")
        if (lower.find("kitchen") != std::string::npos)
            return {SemanticIntent::KITCHEN, "", false};

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

        logger::debug("ResolveAnyDestination: '{}' for '{}'", destination, actor->GetDisplayFullName());

        // Phase 1: Detect semantic intent
        auto intent = DetectSemanticIntent(destination);

        if (intent.type != SemanticIntent::NONE) {
            logger::debug("  Semantic intent: type={}, context='{}', preferBeds={}",
                         static_cast<int>(intent.type), intent.locationContext, intent.preferBeds);

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

}  // namespace IntelEngine
