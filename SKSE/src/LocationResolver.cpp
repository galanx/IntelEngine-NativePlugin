/**
 * Location Resolver Implementation
 *
 * Resolves named locations and semantic terms to travel destinations.
 * No external database dependencies - builds index from game data.
 */

#include "LocationResolver.h"
#include "GameText.h"
#include "LanguageBridge.h"
#include "NameForms.h"
#include "PlaceWords.h"
#include "PlaceNameFile.h"
#include "CellAnalyzer.h"
#include "NPCIndex.h"
#include "StringUtils.h"
#include "ProcessUtils.h"
#include "Settings.h"
#include "SkyrimNetAPI.h"
#include "NamedPlaceMatch.h"

#include <cfloat>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <nlohmann/json.hpp>

namespace IntelEngine {

    namespace {
        // An exact name with and without the little word in front of it: "The Drunken Huntsman" is said without its
        // "the", "die Beflaggte Maehre" without its "die", and "in Weisslauf" names Weisslauf. The little words are
        // English's articles and those of the language packs in force (LanguageBridge::Free); only English's "the"
        // is put in front of a name, since another language's article depends on the word it stands before.
        std::vector<std::string> ExactNames(const std::string& lowerName) {
            std::vector<std::string> names{lowerName};
            const auto space = lowerName.find(' ');
            if (space != std::string::npos && space > 0 && space + 1 < lowerName.size()) {
                const NameForms::Words first = NameForms::Read(std::string_view(lowerName).substr(0, space));
                const auto free = LanguageBridge::GetSingleton()->Free();
                if (first.at.size() == 1 && free && free(first.Word(0))) names.push_back(lowerName.substr(space + 1));
            }
            if (lowerName.rfind("the ", 0) != 0) names.push_back("the " + lowerName);
            return names;
        }
    }

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
            auto name = GameText::In(cell->GetName());
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

            auto name = GameText::In(loc->GetFullName());
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

        // What script are the game's own place names written in? Asked before any names from files are added:
        // it tells a translation the game's language setting does not know of (LanguageBridge.h).
        LanguageBridge::GetSingleton()->NoteNames(m_allLocationNames);

        // Add place names from Data/SKSE/Plugins/IntelEngine/placenames/*.tsv (english.tsv ships), so names
        // in another language resolve: English input on a German install, for one. The game's own names were
        // indexed above and stay first-wins.
        // (4.0: each master is looked for once a file, not once a row. Asked for by a plugin's name the game walks
        // its whole list of plugin files, which on a list of two thousand plugins is slow enough to matter fifteen
        // hundred times over; and the time the files take is logged, since this runs again whenever the index is
        // rebuilt.)
        std::error_code dirError;
        try {
            for (const auto& file : std::filesystem::directory_iterator("Data/SKSE/Plugins/IntelEngine/placenames", dirError)) {
                if (file.path().extension() != ".tsv") continue;
                const auto began = std::chrono::steady_clock::now();
                std::ifstream in(file.path(), std::ios::binary);
                std::uint32_t rows = 0, found = 0, added = 0;
                // master's name -> the first form id of that plugin in this load order; nothing when it is not loaded
                std::unordered_map<std::string, std::optional<RE::FormID>> masters;
                for (std::string line; std::getline(in, line);) {
                    const auto row = ParsePlaceNameLine(line);
                    if (!row) continue;
                    rows++;
                    auto known = masters.find(row->master);
                    if (known == masters.end()) {
                        std::optional<RE::FormID> first;
                        if (const auto* plugin = dataHandler->LookupModByName(row->master); plugin && plugin->compileIndex != 0xFF) {
                            first = (static_cast<RE::FormID>(plugin->compileIndex) << 24) +
                                    (static_cast<RE::FormID>(plugin->smallFileCompileIndex) << 12);
                        }
                        known = masters.emplace(row->master, first).first;
                    }
                    if (!known->second) continue;  // master not loaded
                    auto* form = RE::TESForm::LookupByID(*known->second + row->localId);
                    if (!form) continue;
                    auto* cell = row->type == 'C' ? form->As<RE::TESObjectCELL>() : nullptr;
                    const bool isLocation = row->type == 'L' && form->Is(RE::FormType::Location);
                    if (!isLocation && !(cell && cell->IsInteriorCell())) continue;
                    found++;

                    auto& index = isLocation ? m_locationIndex : m_cellIndex;
                    std::string lowerName = StringUtils::ToLowerStd(row->name);
                    if (index.try_emplace(lowerName, form->GetFormID()).second) {
                        (isLocation ? m_allLocationNames : m_allCellNames).push_back(std::move(lowerName));
                        added++;
                    }
                }
                logger::info("Place names {}: {} rows, {} forms found, {} names added ({:.1f} ms)",
                             file.path().filename().string(), rows, found, added,
                             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count());
            }
        } catch (const std::exception& e) {
            logger::warn("Place-name files: {}", e.what());
        }
        if (dirError) logger::info("No place-name files ({})", dirError.message());

        // Every location's name in plain letters as well (ExactLocation): two spellings of one name, one key.
        m_locationPlain.clear();
        for (const auto& name : m_allLocationNames) {
            if (const auto it = m_locationIndex.find(name); it != m_locationIndex.end()) m_locationPlain.try_emplace(NameForms::Key(name), it->second);
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

        // Build dungeon boss anchor index from BGSLocation::specialRefs
        BuildDungeonIndex();

        m_indexBuilt = true;
        logger::info("Location index built: {} cells, {} locations, {} NPC homes, {} dungeon endpoints",
                     m_allCellNames.size(), m_allLocationNames.size(), m_npcHomeIndex.size(), m_dungeonIndex.size());
    }

    RE::TESObjectCELL* LocationResolver::ResolveCell(const std::string& locationName) {
        std::shared_lock lock(m_mutex);

        if (locationName.empty()) return nullptr;

        std::string lowerName = StringUtils::ToLowerStd(GameText::In(locationName));

        // 1. Exact match
        auto it = m_cellIndex.find(lowerName);
        if (it != m_cellIndex.end()) {
            auto* form = RE::TESForm::LookupByID(it->second);
            if (form) {
                logger::debug("ResolveCell('{}') -> Exact match", locationName);
                return form->As<RE::TESObjectCELL>();
            }
        }

        // 1b. The name as people say it in their language (NameForms.h): an ending, an article, a particle.
        if (const auto forms = NameForms::Find(lowerName, m_allCellNames, NameForms::Forms, LanguageBridge::GetSingleton()->Free())) {
            auto cellIt = m_cellIndex.find(m_allCellNames[static_cast<std::size_t>(forms.index)]);
            if (auto* form = cellIt != m_cellIndex.end() ? RE::TESForm::LookupByID(cellIt->second) : nullptr) {
                logger::info("ResolveCell('{}') -> '{}' (the name in another form)", locationName, m_allCellNames[static_cast<std::size_t>(forms.index)]);
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

        std::string lowerName = StringUtils::ToLowerStd(GameText::In(locationName));

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

        // 1b. The name as people say it in their language (NameForms.h): an ending, an article, a particle.
        if (const auto forms = NameForms::Find(lowerName, m_allLocationNames, NameForms::Forms, LanguageBridge::GetSingleton()->Free())) {
            auto locIt = m_locationIndex.find(m_allLocationNames[static_cast<std::size_t>(forms.index)]);
            if (auto* form = locIt != m_locationIndex.end() ? RE::TESForm::LookupByID(locIt->second) : nullptr) {
                logger::info("ResolveLocation('{}') -> '{}' (the name in another form)", locationName, m_allLocationNames[static_cast<std::size_t>(forms.index)]);
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

    RE::BGSLocation* LocationResolver::ExactLocation(const std::string& locationName) const {
        if (locationName.empty()) return nullptr;
        const std::string lowerName = StringUtils::ToLowerStd(GameText::In(locationName));
        std::shared_lock lock(m_mutex);
        if (const auto it = m_locationIndex.find(lowerName); it != m_locationIndex.end()) {
            return RE::TESForm::LookupByID<RE::BGSLocation>(it->second);
        }
        // the same name in other letters ("Weisslauf" for the name with a sharp s): plain letters, compared whole
        if (const auto it = m_locationPlain.find(NameForms::Key(lowerName)); it != m_locationPlain.end()) {
            return RE::TESForm::LookupByID<RE::BGSLocation>(it->second);
        }
        return nullptr;
    }

    RE::TESObjectREFR* LocationResolver::FindDoorInCellTo(RE::TESObjectCELL* sourceCell, RE::TESObjectCELL* targetCell) {
        if (!sourceCell || !targetCell) return nullptr;

        RE::TESObjectREFR* foundDoor = nullptr;

        sourceCell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
            if (!ref) return RE::BSContainer::ForEachResult::kContinue;

            auto* baseObj = ref->GetBaseObject();
            if (!baseObj || !baseObj->Is(RE::FormType::Door)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            if (ref->IsDisabled()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Check where this door leads
            auto* extraTeleport = ref->extraList.GetByType<RE::ExtraTeleport>();
            if (!extraTeleport || !extraTeleport->teleportData) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            auto linkedDoor = std::as_const(extraTeleport->teleportData->linkedDoor).get();
            if (!linkedDoor) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            auto* destCell = linkedDoor->GetSaveParentCell();
            if (!destCell) destCell = linkedDoor->GetParentCell();
            if (destCell && destCell->GetFormID() == targetCell->GetFormID()) {
                foundDoor = ref;
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

        // Strategy 0: Exact interior cell match (highest priority for named interiors)
        // Catches shop/inn/house names like "Riverwood Trader" or "Bannered Mare" before
        // BGSLocation fuzzy matching can resolve to a parent settlement ("Riverwood").
        // Uses EXACT match only — fuzzy would let "Riften" match "Riften Jail", hijacking
        // settlement lookups that should resolve via BGSLocation world markers.
        {
            std::string lowerName = StringUtils::ToLowerStd(GameText::In(locationName));
            RE::TESObjectCELL* destCell = nullptr;
            {
                // "Drunken Huntsman" names the cell "The Drunken Huntsman" (and "The Bannered Mare" is often said
                // without "the"): an exact name with or without a leading article is still exact (ExactNames).
                const std::vector<std::string> names = ExactNames(lowerName);
                std::shared_lock lock(m_mutex);
                for (const auto& name : names) {
                    auto cellIt = m_cellIndex.find(name);
                    if (cellIt != m_cellIndex.end()) {
                        destCell = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellIt->second);
                        if (destCell) break;
                    }
                }
            }
            if (destCell && destCell->IsInteriorCell()) {
                auto* interiorDoor = FindExteriorDoorInCell(destCell);
                if (interiorDoor) {
                    auto* extDoor = GetExteriorLinkedDoor(interiorDoor);
                    if (extDoor) {
                        auto pos = extDoor->GetPosition();
                        logger::info("  Strategy 0 SUCCESS: interior cell '{}' exterior door at ({:.0f}, {:.0f}, {:.0f})",
                                    GameText::In(destCell->GetName()) ? GameText::In(destCell->GetName()) : "unnamed",
                                    pos.x, pos.y, pos.z);
                        return extDoor;
                    }
                }
                logger::info("  Strategy 0: interior cell '{}' found but no exterior door",
                            GameText::In(destCell->GetName()) ? GameText::In(destCell->GetName()) : "unnamed");
            }
        }

        // Strategy 1: World location marker (highest priority for named destinations)
        // These are exterior persistent references at the map marker position.
        // Skyrim's AI can always pathfind to these.
        auto* bgsLocation = ResolveLocation(locationName);
        if (bgsLocation) {
            auto locName = GameText::In(bgsLocation->GetFullName());
            logger::info("  BGSLocation matched: '{}' (FormID {:08X})",
                        locName ? locName : "unnamed", bgsLocation->GetFormID());

            auto worldMarkerPtr = std::as_const(bgsLocation->worldLocMarker).get();
            if (worldMarkerPtr) {
                auto* markerRef = worldMarkerPtr.get();
                // A marker standing inside another building is the game data's mistake, not a destination: the
                // Drunken Huntsman's own marker is placed in Belethor's General Goods next door, so everyone sent
                // to the inn waited in the shop (seen 2026-11-17). Used only when its interior belongs to this place.
                if (markerRef) {
                    auto* markerCell = markerRef->GetParentCell();
                    auto* markerLoc = markerCell ? markerCell->GetLocation() : nullptr;
                    if (markerCell && markerCell->IsInteriorCell() && markerLoc && markerLoc != bgsLocation &&
                        !markerLoc->IsParent(bgsLocation)) {
                        logger::info("  Strategy 1: worldLocMarker lies in '{}', another place: not used",
                                     GameText::In(markerCell->GetName()) ? GameText::In(markerCell->GetName()) : "unnamed");
                        markerRef = nullptr;
                    }
                }
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
                        auto* extDoor = GetExteriorLinkedDoor(interiorDoor);
                        if (extDoor) {
                            auto pos = extDoor->GetPosition();
                            logger::info("  Strategy 1b SUCCESS: exterior door of '{}' at ({:.0f}, {:.0f}, {:.0f})",
                                        GameText::In(destCell->GetName()) ? GameText::In(destCell->GetName()) : "unnamed",
                                        pos.x, pos.y, pos.z);
                            return extDoor;
                        }
                    }
                    logger::info("  Strategy 1b: no exterior door found for cell '{}'",
                                GameText::In(destCell->GetName()) ? GameText::In(destCell->GetName()) : "unnamed");
                }
            }

            // Strategy 2: Parent location's world marker (for interior locations like inns/shops
            // whose parent is an exterior settlement with a map marker)
            auto* parentLoc = bgsLocation->parentLoc;
            if (parentLoc) {
                auto parentName = GameText::In(parentLoc->GetFullName());
                logger::info("  Parent location: '{}' (FormID {:08X})",
                            parentName ? parentName : "unnamed", parentLoc->GetFormID());

                auto parentMarkerPtr = std::as_const(parentLoc->worldLocMarker).get();
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
                    auto gpName = GameText::In(grandparentLoc->GetFullName());
                    logger::info("  Grandparent location: '{}' (FormID {:08X})",
                                gpName ? gpName : "unnamed", grandparentLoc->GetFormID());

                    auto gpMarkerPtr = std::as_const(grandparentLoc->worldLocMarker).get();
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

            // Strategy 2c: Scan ALL locations for one whose parent is this BGSLocation
            // and that HAS a worldLocMarker. Handles cases like Kynesgrove where the
            // BGSLocation itself has no marker but a child sub-location might.
            {
                auto* dataHandler = RE::TESDataHandler::GetSingleton();
                if (dataHandler) {
                    RE::TESObjectREFR* childMarkerRef = nullptr;
                    float bestChildDist = (std::numeric_limits<float>::max)();
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    auto playerPos = player ? player->GetPosition() : RE::NiPoint3{0, 0, 0};

                    for (auto* loc : dataHandler->GetFormArray<RE::BGSLocation>()) {
                        if (!loc || loc == bgsLocation) continue;
                        // Check if this location is a child of our target
                        bool isChild = false;
                        auto* p = loc->parentLoc;
                        for (int depth = 0; p && depth < 3; ++depth) {
                            if (p == bgsLocation) { isChild = true; break; }
                            p = p->parentLoc;
                        }
                        if (!isChild) continue;

                        auto markerPtr = std::as_const(loc->worldLocMarker).get();
                        if (markerPtr) {
                            auto* ref = markerPtr.get();
                            // Under the open sky only: a marker inside a building is that building, not the town it
                            // stands in ("Whiterun" became Dragonsreach, and a horse was sent up the palace steps).
                            if (auto* markerCell = ref ? ref->GetParentCell() : nullptr; markerCell && markerCell->IsInteriorCell()) continue;
                            if (ref) {
                                float dist = ref->GetPosition().GetDistance(playerPos);
                                if (dist < bestChildDist) {
                                    bestChildDist = dist;
                                    childMarkerRef = ref;
                                }
                            }
                        }
                    }
                    if (childMarkerRef) {
                        auto pos = childMarkerRef->GetPosition();
                        logger::info("  Strategy 2c SUCCESS: child location worldLocMarker at ({:.0f}, {:.0f}, {:.0f})",
                                    pos.x, pos.y, pos.z);
                        return childMarkerRef;
                    }
                    logger::info("  Strategy 2c: no child locations with worldLocMarker");
                }
            }

            // Strategy 3: Find a loaded actor at the BGSLocation (all 4 process tiers)
            logger::info("  Strategy 3: searching loaded actors at BGSLocation...");
            RE::TESObjectREFR* strategy3Result = nullptr;
            ProcessUtils::ForEachLoadedActor([&](RE::Actor* loadedActor) -> bool {
                if (loadedActor->GetCurrentLocation() == bgsLocation) {
                    auto actorName = GameText::In(loadedActor->GetDisplayFullName());
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
            auto cellName = GameText::In(targetCell->GetName());
            logger::info("  Strategy 5: matched interior cell '{}' (FormID {:08X})",
                        cellName ? cellName : "unnamed", targetCell->GetFormID());

            RE::TESObjectREFR* bestRef = nullptr;

            targetCell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
                if (!ref || ref->IsDisabled()) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                auto* baseObj = ref->GetBaseObject();
                if (baseObj && baseObj->Is(RE::FormType::Door)) {
                    bestRef = ref;
                    return RE::BSContainer::ForEachResult::kStop;
                }

                if (!bestRef) {
                    bestRef = ref;
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

        // Log all doors for diagnostics
        LogAllDoors(actor);

        logger::debug("ResolveSemantic: Resolving '{}'", semanticTerm);

        // The word is read by the same rows as every destination (PlaceWords.h), in whatever language it was said.
        const SemanticIntent intent = DetectSemanticIntent(semanticTerm);
        if (intent.type == SemanticIntent::HOME) return ResolveHome(actor, intent);
        if (intent.type != SemanticIntent::NONE) return ResolveSemantic(actor, intent);

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
                     GameText::In(actor->GetDisplayFullName()),
                     cell ? GameText::In(cell->GetName()) : "null",
                     doors.size());

        for (size_t i = 0; i < doors.size(); i++) {
            auto* door = doors[i];
            auto destName = analyzer->GetDoorDestinationName(door);
            bool isExterior = analyzer->IsDoorExterior(door);
            bool isUp = analyzer->IsDoorUpward(door);
            bool isDown = analyzer->IsDoorDownward(door);
            bool nameUp = analyzer->IsDoorNameUpward(door);
            bool nameDown = analyzer->IsDoorNameDownward(door);

            auto doorRefName = GameText::In(door->GetName());
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
                        auto* name = ref->GetBaseObject() ? GameText::In(ref->GetBaseObject()->GetName()) : "furniture";
                        logger::debug("ResolveUpstairs: Bed above -> '{}'", name ? name : "unnamed");
                        return ref;
                    }
                }
            }
            // Return first furniture above (any type)
            auto* first = furniture[0];
            auto* name = first->GetBaseObject() ? GameText::In(first->GetBaseObject()->GetName()) : "furniture";
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
            auto cellName = GameText::In(cell->GetName());
            if (cellName && strlen(cellName) > 0) {
                // (the words are data: PlaceWords.h, `name downstairs` of English and of the language in force)
                auto* language = LanguageBridge::GetSingleton();
                if (language->NameSays(cellName, "downstairs")) {

                    logger::debug("ResolveUpstairs: Cell '{}' is downstairs-named, trying inverse heuristic", cellName);

                    for (auto* door : doors) {
                        auto destName = analyzer->GetDoorDestinationName(door);
                        if (!language->NameSays(destName.c_str(), "downstairs")) {
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
                        auto* name = ref->GetBaseObject() ? GameText::In(ref->GetBaseObject()->GetName()) : "furniture";
                        logger::debug("ResolveDownstairs: Bed below -> '{}'", name ? name : "unnamed");
                        return ref;
                    }
                }
            }
            auto* first = furniture[0];
            auto* name = first->GetBaseObject() ? GameText::In(first->GetBaseObject()->GetName()) : "furniture";
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
            auto cellName = GameText::In(cell->GetName());
            if (cellName && strlen(cellName) > 0) {
                auto* language = LanguageBridge::GetSingleton();
                if (language->NameSays(cellName, "upstairs")) {

                    logger::debug("ResolveDownstairs: Cell '{}' is upstairs-named, trying inverse heuristic", cellName);

                    for (auto* door : doors) {
                        auto destName = analyzer->GetDoorDestinationName(door);
                        // Any door NOT leading to an upstairs destination is "downstairs"
                        if (!language->NameSays(destName.c_str(), "upstairs")) {
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

            auto* destCell = destDoor->GetSaveParentCell();
            if (!destCell) destCell = destDoor->GetParentCell();
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

        cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
            if (!ref || ref->IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;

            if (analyzer->IsBedFurniture(ref)) {
                float dist = ref->GetPosition().GetDistance(actorPos);
                if (dist < nearestDist) {
                    nearestDist = dist;
                    nearestBed = ref;
                }
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (nearestBed) {
            auto* name = nearestBed->GetBaseObject() ? GameText::In(nearestBed->GetBaseObject()->GetName()) : "bed";
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
            auto* name = nearest->GetBaseObject() ? GameText::In(nearest->GetBaseObject()->GetName()) : "cooking station";
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
                     preferInterior, GameText::In(currentCell->GetName()) ? GameText::In(currentCell->GetName()) : "unnamed",
                     currentCell->IsInteriorCell());

        // What a thing by the water is called is data (PlaceWords.h: `name bath` for baths and hot springs indoors,
        // `name water` for docks, bridges, mills and the like outdoors), in English and in the language in force.
        static const std::string interiorKeywords = "bath";
        static const std::string exteriorKeywords = "water";

        struct WaterCandidate {
            RE::TESObjectREFR* ref;
            float distSq;
        };

        auto actorPos = actor->GetPosition();

        // Helper: check if reference base name matches any keyword in a list
        auto matchesKeywords = [](RE::TESObjectREFR& ref, const std::string& what) -> bool {
            auto* baseObj = ref.GetBaseObject();
            if (!baseObj) return false;
            auto* language = LanguageBridge::GetSingleton();
            auto* name = GameText::In(baseObj->GetName());
            if (name && name[0] && language->NameSays(name, what)) return true;
            // The editor id is English whatever the game is in (where a load order keeps editor ids at all).
            auto* editorId = baseObj->GetFormEditorID();
            return editorId && editorId[0] && language->NameSays(editorId, what, true);
        };

        // Helper: scan a cell for references matching keywords, return nearest
        auto scanCell = [&](RE::TESObjectCELL* cell, const std::string& keywords) -> RE::TESObjectREFR* {
            std::vector<WaterCandidate> candidates;
            cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
                if (!ref || ref->IsDisabled() || ref->IsDeleted())
                    return RE::BSContainer::ForEachResult::kContinue;
                if (matchesKeywords(*ref, keywords)) {
                    auto pos = ref->GetPosition();
                    float dx = pos.x - actorPos.x;
                    float dy = pos.y - actorPos.y;
                    candidates.push_back({ref, dx * dx + dy * dy});
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });

            if (candidates.empty()) return nullptr;

            auto* best = &candidates[0];
            for (size_t i = 1; i < candidates.size(); i++) {
                if (candidates[i].distSq < best->distSq) best = &candidates[i];
            }

            auto* baseName = best->ref->GetBaseObject() ? GameText::In(best->ref->GetBaseObject()->GetName()) : "unknown";
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

            cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
                if (!ref || ref->IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;
                if (!analyzer->IsBedFurniture(ref)) return RE::BSContainer::ForEachResult::kContinue;

                bedsScanned++;
                auto bedFormId = ref->GetFormID();
                auto cellFormId = cell->GetFormID();

                // Check direct actor ownership (e.g., a bed owned by Alvor specifically)
                auto* ownerBase = ref->GetActorOwner();
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
                auto* factionOwner = ref->GetFactionOwner();
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

    // =========================================================================
    // Dungeon Boss Anchor Index
    // =========================================================================

    void LocationResolver::BuildDungeonIndex() {
        m_dungeonIndex.clear();

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return;

        logger::info("Building dungeon index...");

        // Step 1: Enumerate ALL BGSLocationRefType forms and log them.
        // This tells us exactly what LocRefTypes exist in the load order.
        std::unordered_set<RE::FormID> priorityRefTypes;  // Boss, BossContainer, etc.
        {
            auto& locRefTypes = dataHandler->GetFormArray<RE::BGSLocationRefType>();
            logger::info("DungeonIndex: {} LocRefType forms in load order:", locRefTypes.size());
            for (auto* lrt : locRefTypes) {
                if (!lrt) continue;
                auto edId = lrt->GetFormEditorID();
                std::string name = edId ? edId : "(null)";
                logger::info("  LocRefType {:08X} '{}'", lrt->GetFormID(), name);

                // Flag priority types — these are most likely to be interior anchors
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower.find("boss") != std::string::npos ||
                    lower.find("captive") != std::string::npos ||
                    lower.find("center") != std::string::npos) {
                    priorityRefTypes.insert(lrt->GetFormID());
                }
            }
        }

        // Step 2: Cache dungeon-type keywords
        std::vector<RE::BGSKeyword*> dungeonKeywords;
        static const char* dungeonKeywordEditorIDs[] = {
            "LocTypeDungeon", "LocTypeCrypt", "LocTypeRuin",
            "LocTypeCave", "LocTypeMine", "LocTypeMilitaryFort"
        };
        for (auto* editorID : dungeonKeywordEditorIDs) {
            auto* kw = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(editorID);
            if (kw) dungeonKeywords.push_back(kw);
        }
        if (dungeonKeywords.empty()) {
            logger::warn("DungeonIndex: no dungeon keywords found");
            return;
        }

        // Step 3: Build parent → children map
        std::unordered_map<RE::FormID, std::vector<RE::BGSLocation*>> childMap;
        for (auto* loc : dataHandler->GetFormArray<RE::BGSLocation>()) {
            if (loc && loc->parentLoc) {
                childMap[loc->parentLoc->GetFormID()].push_back(loc);
            }
        }

        // Helper: check if a specialRef resolves to an interior cell.
        // Tries parentSpaceID first, then resolves the actual ref and checks GetParentCell().
        auto isInteriorRef = [](const RE::SpecialRefData& sref) -> RE::TESObjectCELL* {
            // Method 1: parentSpaceID is an interior cell FormID
            if (sref.refData.parentSpaceID != 0) {
                auto* parentSpace = RE::TESForm::LookupByID(sref.refData.parentSpaceID);
                if (parentSpace) {
                    auto* cell = parentSpace->As<RE::TESObjectCELL>();
                    if (cell && cell->IsInteriorCell()) return cell;
                }
            }
            // Method 2: resolve the ref itself and check its actual cell.
            // Some persistent refs have worldspace as parentSpaceID in ESP data
            // but are actually placed in an interior cell at runtime.
            if (sref.refData.refID != 0) {
                auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(sref.refData.refID);
                if (ref) {
                    auto* cell = ref->GetSaveParentCell();
                    if (!cell) cell = ref->GetParentCell();
                    if (cell && cell->IsInteriorCell()) return cell;
                }
            }
            return nullptr;
        };

        // Helper: scan a location's specialRefs for an interior anchor.
        // Priority: priority LocRefType (Boss/BossContainer/etc.) > any interior ref.
        auto scanLocationRefs = [&](RE::BGSLocation* scanLoc, DungeonEndpoint& outEndpoint,
                                     bool& outFoundPriority, std::uint32_t& extSkipped) {
            for (auto& sref : scanLoc->specialRefs) {
                if (!sref.type) continue;
                if (sref.refData.refID == 0) continue;

                auto* interiorCell = isInteriorRef(sref);
                if (!interiorCell) {
                    extSkipped++;
                    continue;
                }

                bool isPriority = priorityRefTypes.count(sref.type->GetFormID()) > 0;
                if (isPriority) {
                    outEndpoint.anchorRefId = sref.refData.refID;
                    outEndpoint.cellFormId = interiorCell->GetFormID();
                    outFoundPriority = true;
                    return;  // priority type in interior = best possible
                }

                // Store first interior ref as fallback
                if (outEndpoint.anchorRefId == 0) {
                    outEndpoint.anchorRefId = sref.refData.refID;
                    outEndpoint.cellFormId = interiorCell->GetFormID();
                }
            }
        };

        std::uint32_t locationsChecked = 0;
        std::uint32_t priorityMatches = 0;
        std::uint32_t fallbackMatches = 0;
        std::uint32_t exteriorSkipped = 0;
        std::uint32_t childResolved = 0;
        std::uint32_t refResolvedInterior = 0;  // refs where Method 2 found interior

        for (auto* loc : dataHandler->GetFormArray<RE::BGSLocation>()) {
            if (!loc) continue;

            bool isDungeon = false;
            for (auto* kw : dungeonKeywords) {
                if (loc->HasKeyword(kw)) { isDungeon = true; break; }
            }
            if (!isDungeon) continue;
            locationsChecked++;

            // Phase 1: scan this location's own specialRefs
            DungeonEndpoint bestEndpoint;
            bool foundPriority = false;
            scanLocationRefs(loc, bestEndpoint, foundPriority, exteriorSkipped);

            // Phase 2: scan child sub-locations if no interior ref found
            if (bestEndpoint.anchorRefId == 0) {
                auto childIt = childMap.find(loc->GetFormID());
                if (childIt != childMap.end()) {
                    for (auto* child : childIt->second) {
                        scanLocationRefs(child, bestEndpoint, foundPriority, exteriorSkipped);
                        if (foundPriority) break;
                    }
                    if (bestEndpoint.anchorRefId != 0) childResolved++;
                }
            }

            if (bestEndpoint.anchorRefId != 0) {
                m_dungeonIndex[loc->GetFormID()] = bestEndpoint;
                if (foundPriority) priorityMatches++;
                else fallbackMatches++;
            } else {
                // Log dungeons with NO interior anchor for diagnostics
                auto locName = GameText::In(loc->GetFullName());
                if (locName && locName[0]) {
                    logger::info("DungeonIndex: NO interior anchor for '{}' ({:08X}), {} specialRefs",
                                 locName, loc->GetFormID(), loc->specialRefs.size());
                    for (auto& sref : loc->specialRefs) {
                        auto typeEd = sref.type ? (sref.type->GetFormEditorID() ? sref.type->GetFormEditorID() : "?") : "null";
                        logger::info("  specialRef: type='{}' refID={:08X} parentSpace={:08X}",
                                     typeEd, sref.refData.refID, sref.refData.parentSpaceID);
                    }
                }
            }
        }

        logger::info("DungeonIndex: {} dungeons checked, {} priority, {} fallback, {} children, {} ext skipped, {} total indexed",
                     locationsChecked, priorityMatches, fallbackMatches, childResolved, exteriorSkipped, m_dungeonIndex.size());
    }

    RE::TESObjectREFR* LocationResolver::GetDungeonBossAnchor(RE::BGSLocation* loc) {
        if (!loc) return nullptr;

        std::shared_lock lock(m_mutex);
        auto it = m_dungeonIndex.find(loc->GetFormID());
        if (it == m_dungeonIndex.end()) return nullptr;

        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(it->second.anchorRefId);
        if (ref) {
            auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(it->second.cellFormId);
            auto pos = ref->GetPosition();
            logger::info("[IntelEngine] GetDungeonBossAnchor: '{}' → ref {:08X} '{}' in cell {:08X} '{}' pos({:.0f},{:.0f},{:.0f})",
                         GameText::In(loc->GetFullName()) ? GameText::In(loc->GetFullName()) : "?",
                         it->second.anchorRefId,
                         GameText::In(ref->GetName()) ? GameText::In(ref->GetName()) : "unnamed",
                         it->second.cellFormId,
                         cell ? (GameText::In(cell->GetName()) ? GameText::In(cell->GetName()) : "unnamed") : "null",
                         pos.x, pos.y, pos.z);
        }
        return ref;
    }

    std::vector<std::pair<RE::FormID, RE::FormID>> LocationResolver::DungeonAnchors() const {
        std::shared_lock lock(m_mutex);
        std::vector<std::pair<RE::FormID, RE::FormID>> out;
        out.reserve(m_dungeonIndex.size());
        for (const auto& [location, endpoint] : m_dungeonIndex) out.emplace_back(location, endpoint.anchorRefId);
        return out;
    }

    std::vector<std::pair<RE::FormID, RE::FormID>> LocationResolver::HomeCells() const {
        std::shared_lock lock(m_mutex);
        std::vector<std::pair<RE::FormID, RE::FormID>> out;
        out.reserve(m_npcHomeIndex.size());
        for (const auto& [base, home] : m_npcHomeIndex) out.emplace_back(base, home.cellFormId);
        return out;
    }

    RE::TESObjectREFR* LocationResolver::GetDungeonBossAnchor(const std::string& locationName) {
        if (locationName.empty()) return nullptr;

        // Resolve location name to BGSLocation
        auto* loc = ResolveLocation(locationName);
        if (!loc) {
            logger::info("[IntelEngine] GetDungeonBossAnchor: location '{}' not resolved", locationName);
            return nullptr;
        }

        // Check this location
        auto* result = GetDungeonBossAnchor(loc);
        if (result) return result;

        // Also check parent location (dungeon sub-locations may nest under the main one)
        if (loc->parentLoc) {
            result = GetDungeonBossAnchor(loc->parentLoc);
            if (result) return result;
        }

        logger::info("[IntelEngine] GetDungeonBossAnchor: '{}' not in dungeon index", locationName);
        return nullptr;
    }

    RE::TESObjectREFR* LocationResolver::FindExteriorDoorInCell(RE::TESObjectCELL* cell) {
        if (!cell) return nullptr;

        std::vector<RE::TESObjectREFR*> candidates;
        int doorCount = 0;
        int noTeleport = 0;
        int noLinked = 0;
        int interiorDest = 0;
        int noDest = 0;
        cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
            if (!ref) return RE::BSContainer::ForEachResult::kContinue;

            auto* baseObj = ref->GetBaseObject();
            if (!baseObj || !baseObj->Is(RE::FormType::Door) || ref->IsDisabled())
                return RE::BSContainer::ForEachResult::kContinue;

            doorCount++;
            auto* teleport = ref->extraList.GetByType<RE::ExtraTeleport>();
            if (!teleport || !teleport->teleportData) {
                noTeleport++;
                return RE::BSContainer::ForEachResult::kContinue;
            }

            auto linkedDoor = std::as_const(teleport->teleportData->linkedDoor).get();
            if (!linkedDoor) {
                noLinked++;
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Use GetSaveParentCell — GetParentCell returns null when the
            // destination cell isn't loaded (e.g., loading a save inside an interior)
            auto* destCell = linkedDoor->GetSaveParentCell();
            if (!destCell) destCell = linkedDoor->GetParentCell();
            if (!destCell) {
                noDest++;
                return RE::BSContainer::ForEachResult::kContinue;
            }

            if (destCell->IsInteriorCell()) {
                interiorDest++;
            } else {
                candidates.push_back(ref);
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (candidates.empty()) {
            logger::debug("FindExteriorDoorInCell '{}': {} doors, {} noTeleport, {} noLinked, {} noDest, {} interiorDest, 0 exterior",
                         GameText::In(cell->GetName()) ? GameText::In(cell->GetName()) : "unnamed",
                         doorCount, noTeleport, noLinked, noDest, interiorDest);
            return nullptr;
        }
        if (candidates.size() == 1) return candidates[0];

        // Multiple exterior doors — prefer lowest z-coordinate (ground-level main entrance)
        RE::TESObjectREFR* best = candidates[0];
        for (size_t i = 1; i < candidates.size(); ++i) {
            if (candidates[i]->GetPositionZ() < best->GetPositionZ()) {
                best = candidates[i];
            }
        }
        logger::debug("FindExteriorDoorInCell: {} exterior doors found, selected lowest-z at ({:.0f}, {:.0f}, {:.0f})",
            candidates.size(), best->GetPositionX(), best->GetPositionY(), best->GetPositionZ());
        return best;
    }

    RE::TESObjectREFR* LocationResolver::GetExteriorLinkedDoor(RE::TESObjectREFR* interiorDoor) {
        if (!interiorDoor) return nullptr;
        auto* teleport = interiorDoor->extraList.GetByType<RE::ExtraTeleport>();
        if (!teleport || !teleport->teleportData) return nullptr;
        auto linked = std::as_const(teleport->teleportData->linkedDoor).get();
        return linked ? linked.get() : nullptr;
    }

    RE::TESObjectREFR* LocationResolver::GetPlayerHomeExteriorDoorRef() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;
        auto* cell = player->GetParentCell();
        if (!cell || !cell->IsInteriorCell()) return nullptr;

        logger::debug("GetPlayerHomeExteriorDoorRef: scanning cell '{}' ({:08X})",
                     GameText::In(cell->GetName()) ? GameText::In(cell->GetName()) : "unnamed", cell->GetFormID());

        // Direct: door in player's cell leading to exterior
        auto* directDoor = FindExteriorDoorInCell(cell);
        if (directDoor) {
            auto* extDoor = GetExteriorLinkedDoor(directDoor);
            logger::debug("GetPlayerHomeExteriorDoorRef: direct exterior door found (extDoor={})",
                         extDoor ? "valid" : "null");
            return extDoor;
        }
        logger::debug("GetPlayerHomeExteriorDoorRef: no direct exterior door, trying multi-hop");

        // Multi-hop: door leads to another interior that HAS an exterior door
        // (modded homes with porches, entryways, or multi-room layouts)
        RE::TESObjectREFR* bestHopDoor = nullptr;
        cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
            if (!ref) return RE::BSContainer::ForEachResult::kContinue;

            auto* baseObj = ref->GetBaseObject();
            if (!baseObj || !baseObj->Is(RE::FormType::Door) || ref->IsDisabled())
                return RE::BSContainer::ForEachResult::kContinue;

            auto* teleport = ref->extraList.GetByType<RE::ExtraTeleport>();
            if (!teleport || !teleport->teleportData)
                return RE::BSContainer::ForEachResult::kContinue;

            auto linkedDoor = std::as_const(teleport->teleportData->linkedDoor).get();
            if (!linkedDoor)
                return RE::BSContainer::ForEachResult::kContinue;

            auto* destCell = linkedDoor->GetSaveParentCell();
            if (!destCell) destCell = linkedDoor->GetParentCell();
            if (destCell && destCell->IsInteriorCell()) {
                auto* hopDoor = FindExteriorDoorInCell(destCell);
                if (hopDoor) {
                    bestHopDoor = GetExteriorLinkedDoor(hopDoor);
                    return RE::BSContainer::ForEachResult::kStop;
                }
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (bestHopDoor) {
            logger::debug("GetPlayerHomeExteriorDoorRef: multi-hop exterior door found");
        }
        return bestHopDoor;
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
            auto markerPtr = std::as_const(location->worldLocMarker).get();
            if (markerPtr) {
                auto* marker = markerPtr.get();
                if (marker) return marker;
            }

            // Try parent location's marker (e.g., house → settlement)
            if (location->parentLoc) {
                auto parentPtr = std::as_const(location->parentLoc->worldLocMarker).get();
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
                         GameText::In(actorBase->GetFullName()) ? GameText::In(actorBase->GetFullName()) : "unknown");
            return nullptr;
        }

        auto* homeCell = RE::TESForm::LookupByID<RE::TESObjectCELL>(it->second.cellFormId);
        if (!homeCell) {
            logger::warn("ResolveNPCHome: Home cell {:08X} not found for '{}'",
                        it->second.cellFormId,
                        GameText::In(actorBase->GetFullName()) ? GameText::In(actorBase->GetFullName()) : "unknown");
            return nullptr;
        }

        auto cellName = GameText::In(homeCell->GetName());
        logger::info("ResolveNPCHome: '{}' -> cell '{}' ({:08X})",
                    GameText::In(actorBase->GetFullName()) ? GameText::In(actorBase->GetFullName()) : "unknown",
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

            auto markerPtr = std::as_const(loc->worldLocMarker).get();
            if (!markerPtr) continue;
            auto* marker = markerPtr.get();
            if (!marker) continue;

            // Skip interior-only markers
            auto* parentCell = marker->GetSaveParentCell();
            if (!parentCell) parentCell = marker->GetParentCell();
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
                                     GameText::In(editorLoc->GetFullName()) ? GameText::In(editorLoc->GetFullName()) : "unnamed");
                        return FindTravelTarget(GameText::In(editorLoc->GetFullName()) ? GameText::In(editorLoc->GetFullName()) : "");
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
                    auto playerName = GameText::In(player->GetDisplayFullName());
                    if (playerName && strlen(playerName) > 0) {
                        std::string lowerPlayer = StringUtils::ToLowerStd(playerName);
                        // (their name with an ending or an article is still their name: NameForms.h)
                        if (lowerPlayer == intent.homeOwnerHint ||
                            NameForms::Stands(NameForms::Read(lowerPlayer), NameForms::Read(intent.homeOwnerHint),
                                              LanguageBridge::GetSingleton()->Free()).stand >= NameForms::Forms) {
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
                        return FindTravelTarget(GameText::In(editorLoc->GetFullName()) ? GameText::In(editorLoc->GetFullName()) : "");
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

    bool LocationResolver::IsExactPlaceName(const std::string& destination) {
        std::string lower = StringUtils::ToLowerStd(GameText::In(destination));
        // The plain words of a direction keep their meaning even if some cell's editor ID happens to be one of them
        // (the words are data: PlaceWords.h, English's rows and those of the languages in force).
        if (LanguageBridge::GetSingleton()->IsPlaceWord(destination)) return false;
        const std::vector<std::string> names = ExactNames(lower);
        std::shared_lock lock(m_mutex);
        for (const auto& name : names) {
            if (m_cellIndex.contains(name) || m_locationIndex.contains(name)) return true;
        }
        return false;
    }

    SemanticIntent LocationResolver::DetectSemanticIntent(const std::string& said) {
        // Words of place are data, not code (PlaceWords.h): English's rows and the rows of the language packs in
        // force are read by one rule, so "upstairs", "nach oben" and "naverkh" are the same word, and so are "Alvor's
        // house" and the same in a language whose pack names its word for a house. (Until 4.0 this was a ladder of
        // English lists; PlaceWordsCheck holds that ladder and compares.)
        const PlaceWords::Intent read = LanguageBridge::GetSingleton()->ReadPlace(said);
        SemanticIntent intent;
        switch (read.kind) {
            case PlaceWords::Kind::Upstairs:   intent.type = SemanticIntent::UPSTAIRS; break;
            case PlaceWords::Kind::Downstairs: intent.type = SemanticIntent::DOWNSTAIRS; break;
            case PlaceWords::Kind::Outside:    intent.type = SemanticIntent::OUTSIDE; break;
            case PlaceWords::Kind::Inside:     intent.type = SemanticIntent::INSIDE; break;
            case PlaceWords::Kind::Back:       intent.type = SemanticIntent::BACK; break;
            case PlaceWords::Kind::Cellar:     intent.type = SemanticIntent::CELLAR; break;
            case PlaceWords::Kind::Bedroom:    intent.type = SemanticIntent::BEDROOM; break;
            case PlaceWords::Kind::Kitchen:    intent.type = SemanticIntent::KITCHEN; break;
            case PlaceWords::Kind::Home:       intent.type = SemanticIntent::HOME; break;
            case PlaceWords::Kind::Water:      intent.type = SemanticIntent::WATER; break;
            case PlaceWords::Kind::None:       return intent;
        }
        intent.locationContext = read.context;
        intent.preferBeds = read.preferBeds;
        intent.preferInterior = read.preferInterior;
        intent.homeOwnerHint = read.ownerName;
        switch (read.owner) {
            case PlaceWords::Owner::Self:   intent.homeOwner = HomeOwner::NPC; break;
            case PlaceWords::Owner::Player: intent.homeOwner = HomeOwner::PLAYER; break;
            case PlaceWords::Owner::Named:  intent.homeOwner = HomeOwner::NAMED; break;
            case PlaceWords::Owner::None:   break;
        }
        return intent;
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

        auto name = GameText::In(parent->GetFullName());
        if (!name || strlen(name) == 0) return nullptr;

        logger::debug("  ResolveViaParentLocation: Trying parent '{}'", name);
        return FindTravelTarget(name);
    }

    std::string LocationResolver::ExtractLocationName(const std::string& text) {
        // "out of X", "leave X", "go to X" in English and in the languages in force (PlaceWords.h): X.
        return LanguageBridge::GetSingleton()->PlaceBehind(text);
    }

    RE::TESObjectREFR* LocationResolver::ResolveNamedPlace(RE::Actor* actor, const std::string& destination) {
        if (!SkyrimNetAPI::ListPlaces) return nullptr;  // SkyrimNet older than v1.0.0-rc1

        const auto places = ParseNamedPlaces(SkyrimNetAPI::ListPlaces(""));
        RE::FormID currentSpace = 0;
        if (auto* cell = actor->GetParentCell(); cell && cell->IsInteriorCell()) {
            currentSpace = cell->GetFormID();
        } else if (auto* worldspace = actor->GetWorldspace()) {
            currentSpace = worldspace->GetFormID();
        }

        const auto* place = MatchNamedPlace(places, destination, currentSpace);
        if (!place) {
            logger::debug("  Named Place: no match for '{}' among {} places", destination, places.size());
            return nullptr;
        }

        auto* interiorCell = RE::TESForm::LookupByID<RE::TESObjectCELL>(place->space);
        // SkyrimNet's defaults for a place without a radius (PublicAPI.h "Named places")
        const float radius = place->radius > 0.0f ? place->radius : (interiorCell ? 350.0f : 1500.0f);
        const RE::NiPoint3 center{place->x, place->y, place->z};

        // SkyrimNet's containment rule: a place reaches 200 units up or down (PublicAPI.h "Named places")
        constexpr float kPlaceHalfHeight = 200.0f;
        RE::TESObjectREFR* bestRef = nullptr;
        float bestDistance = radius;
        // Persistent refs only: travel holds the target as a linked ref, and a temporary ref is freed when its
        // cell unloads (2026-11-17-run1: CTD in the travel package after the player left the cell).
        const auto consider = [&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
            if (ref && ref->IsPersistent() && !ref->IsDisabled() && !ref->IsDeleted() && !ref->As<RE::Actor>()) {
                const auto pos = ref->GetPosition();
                const float dz = pos.z - center.z;
                const float distance = pos.GetDistance(center);
                if (dz >= -kPlaceHalfHeight && dz <= kPlaceHalfHeight && distance <= bestDistance) {
                    bestDistance = distance;
                    bestRef = ref;
                }
            }
            return RE::BSContainer::ForEachResult::kContinue;
        };

        if (interiorCell) {
            interiorCell->ForEachReference(consider);
        } else if (auto* worldspace = RE::TESForm::LookupByID<RE::TESWorldSpace>(place->space)) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* tes = RE::TES::GetSingleton();
            auto* playerCell = player ? player->GetParentCell() : nullptr;
            if (tes && playerCell && !playerCell->IsInteriorCell() && player->GetWorldspace() == worldspace) {
                tes->ForEachReferenceInRange(player, player->GetPosition().GetDistance(center) + radius, consider);
            } else if (worldspace->persistentCell) {
                // ponytail: a place in an unloaded exterior only sees persistent refs; add a cell-grid
                // lookup at (x, y) if such places fail to resolve in play.
                worldspace->persistentCell->ForEachReference(consider);
            }
        }

        if (!bestRef) {
            logger::info("  Named Place '{}' (space {:08X}) matched, but no reference within {:.0f} units",
                         place->name, place->space, radius);
            return nullptr;
        }
        logger::info("  Named Place '{}' (space {:08X}) -> ref {:08X}, {:.0f} units from its position",
                     place->name, place->space, bestRef->GetFormID(), bestDistance);
        return bestRef;
    }

    RE::TESObjectREFR* LocationResolver::ResolveAnyDestination(RE::Actor* actor, const std::string& destination) {
        if (!actor || destination.empty()) return nullptr;

        // Reset home cell tracking — only set if this destination resolves to a home
        m_lastResolvedHomeCellId = 0;

        logger::debug("ResolveAnyDestination: '{}' for '{}'", destination, GameText::In(actor->GetDisplayFullName()));

        // SkyrimNet's Named Places are not asked for here (3.8.3's ResolveNamedPlace, above, stays for whoever wants
        // the nearest reference of a place by its exact name): in 4.0 every trip asks them first, before this
        // resolver (Core.ResolveTravelTarget -> PlaceTargets: the name in any spelling, the place's own spot, and a
        // room word such as "the kitchen" only inside the building one stands in). Asking again here would list
        // SkyrimNet's places a second time on every resolution, and a room word would mean any room so named anywhere.

        // Phase 1: Detect semantic intent. Not in a place's own name: "White River Watch" has "river" in it and sent
        // Jenassa to the river by Riverwood instead of the fort (2026-11-18); "Riverwood" and "Lakeview Manor" alike.
        auto intent = IsExactPlaceName(destination) ? SemanticIntent{} : DetectSemanticIntent(destination);

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

        // Phase 3: Named location resolution (try this BEFORE NPC name)
        // Location names take priority over NPC names because fuzzy NPC matching
        // can produce false positives (e.g., "Nilheim" → "Wilhelm" at distance 4,
        // resolving a dungeon quest to the innkeeper's home instead).
        logger::debug("  Trying named location: '{}'", destination);
        auto* namedResult = FindTravelTarget(destination);
        if (namedResult) {
            logger::debug("  Resolved via named location");
            return namedResult;
        }

        // Phase 3.5: NPC name as destination → resolve to their home
        // Catches "Camilla", "Camilla Valerius", "Alvor" used as a bare destination.
        // Only when semantic intent was NONE (not already handled as home).
        // Runs AFTER named location so location names always win over NPC names.
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
                    // NPC found but no home — fall through to extraction
                    logger::debug("  NPC '{}' found but no home indexed, falling through", destination);
                }
            }
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

    bool LocationResolver::IsSemanticTerm(const std::string& said) {
        // A plain word of place or of a spot in a room, in English or a language in force (PlaceWords.h).
        return LanguageBridge::GetSingleton()->IsPlaceWord(said);
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
        auto cellName = GameText::In(cell->GetName());
        if (cellName && strlen(cellName) > 0) {
            return cellName;
        }

        // 2. Fallback: BGSLocation name (works for modded interiors in exterior cells)
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

        return "Unknown Location";
    }

    RE::BSFixedString LocationResolver::GetLocationSuggestion(const std::string& searchTerm) {
        std::shared_lock lock(m_mutex);

        if (searchTerm.empty()) return "";

        std::string lowerSearch = StringUtils::ToLowerStd(GameText::In(searchTerm));

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
        stats["semanticTermCount"] = PlaceWords::English().place.size();
        stats["npcHomesIndexed"] = m_npcHomeIndex.size();
        stats["indexBuilt"] = m_indexBuilt;

        return RE::BSFixedString(stats.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    std::vector<RE::FormID> LocationResolver::GetHouseholdMembers(RE::Actor* actor) {
        std::vector<RE::FormID> result;
        if (!actor) return result;

        auto* base = actor->GetActorBase();
        if (!base) return result;

        std::shared_lock lock(m_mutex);

        auto it = m_npcHomeIndex.find(base->GetFormID());
        if (it == m_npcHomeIndex.end()) return result;

        RE::FormID homeCellId = it->second.cellFormId;

        for (const auto& [npcFormId, info] : m_npcHomeIndex) {
            if (npcFormId != base->GetFormID() && info.cellFormId == homeCellId) {
                result.push_back(npcFormId);
            }
        }
        return result;
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

            auto markerPtr = std::as_const(loc->worldLocMarker).get();
            if (!markerPtr) continue;

            auto* marker = markerPtr.get();
            if (!marker) continue;

            // Skip interior-only markers
            auto* parentCell = marker->GetSaveParentCell();
            if (!parentCell) parentCell = marker->GetParentCell();
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
                         GameText::In(actorBase->GetFullName()) ? GameText::In(actorBase->GetFullName()) : "unknown");
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

        auto cellName = GameText::In(homeCell->GetName());
        auto* interiorDoor = FindExteriorDoorInCell(homeCell);

        if (!interiorDoor) {
            logger::debug("SetHomeDoorAccessForCell: No exterior door in cell '{}' ({:08X})",
                         cellName ? cellName : "unnamed", cellFormId);
            return nullptr;
        }

        if (unlock) {
            // Already unlocked and public? Skip — idempotent.
            bool doorAlreadyOpen = !interiorDoor->IsLocked();
            bool cellAlreadyPublic = homeCell->cellFlags.any(RE::TESObjectCELL::Flag::kPublicArea);
            if (doorAlreadyOpen && cellAlreadyPublic) {
                logger::debug("SetHomeDoorAccess: Cell '{}' ({:08X}) already open & public, skipping",
                             cellName ? cellName : "unnamed", cellFormId);
                return interiorDoor;
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

            // Make cell public (no trespass) — idempotent
            if (!cellAlreadyPublic) {
                homeCell->SetPublic(true);
                logger::info("SetHomeDoorAccess: Set cell '{}' ({:08X}) PUBLIC for anti-trespass",
                            cellName ? cellName : "unnamed", cellFormId);
            }
        } else {
            // Never re-lock doors — let vanilla handle door locking/unlocking.
            // IntelEngine only unlocks to enable NPC pathfinding.
            logger::debug("SetHomeDoorAccess: Lock request for '{}' ({:08X}) ignored — vanilla handles re-locking",
                         cellName ? cellName : "unnamed", cellFormId);
        }

        return interiorDoor;
    }

}  // namespace IntelEngine
