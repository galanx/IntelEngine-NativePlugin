#pragma once

/**
 * Location Resolver
 *
 * Resolves location names and semantic terms to travel destinations.
 *
 * Two types of resolution:
 * 1. Named locations ("The Bannered Mare") - Fuzzy match against indexed cells/locations,
 *    return the Cell for Skyrim's AI to handle pathfinding
 * 2. Semantic terms ("outside", "upstairs") - Scan current cell doors to find
 *    the appropriate exit
 *
 * No external database dependencies - builds index from game data at startup.
 */

#include "Plugin.h"

#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>

namespace IntelEngine {

    // Identifies who "home" refers to when a HOME intent is detected.
    enum class HomeOwner {
        NONE,       // not a home intent
        NPC,        // "my home" / "home" — the traveling NPC's own home
        PLAYER,     // "your home" / "your place" — the player's home
        NAMED       // "Alvor's home" — a third-party NPC (name in homeOwnerHint)
    };

    struct SemanticIntent {
        enum Type { NONE, UPSTAIRS, DOWNSTAIRS, OUTSIDE, INSIDE, BACK, CELLAR, BEDROOM, KITCHEN, HOME, WATER };
        Type type = NONE;
        std::string locationContext;  // extracted from compound phrases (e.g., "Helgen" from "out of Helgen")
        bool preferBeds = false;      // true when destination mentions bedroom/bed alongside a direction
        HomeOwner homeOwner = HomeOwner::NONE;  // who "home" belongs to
        std::string homeOwnerHint;              // NPC name for HomeOwner::NAMED (e.g., "Alvor")
        bool preferInterior = false;  // WATER: true = bath/wash (try interior first), false = swim/river (exterior)
    };

    class LocationResolver {
    public:
        static LocationResolver* GetSingleton() {
            static LocationResolver instance;
            return &instance;
        }

        /**
         * Build location index from game data.
         * Called on data loaded event.
         */
        void BuildLocationIndex();

        /**
         * Check if index is built.
         */
        bool IsIndexBuilt() const { return m_indexBuilt; }

        /**
         * Resolve a named location to a Cell.
         * Uses fuzzy matching against all indexed cell/location names.
         * Returns the Cell pointer for Skyrim's AI to handle travel.
         *
         * @param locationName Named location (e.g., "The Bannered Mare")
         * @return Cell pointer, or nullptr if not found
         */
        RE::TESObjectCELL* ResolveCell(const std::string& locationName);

        /**
         * Resolve a named location to a BGSLocation.
         * Useful for broader locations like "Whiterun" or "Dragonsreach".
         *
         * @param locationName Named location
         * @return Location pointer, or nullptr if not found
         */
        RE::BGSLocation* ResolveLocation(const std::string& locationName);

        /**
         * Find a door in loaded cells that leads to the target location.
         * If found, NPC can walk to this door and use it.
         * If not found (target not in loaded cells), returns nullptr
         * and Papyrus should use AI package-based travel.
         *
         * @param locationName Named location
         * @return Door reference, or nullptr
         */
        RE::TESObjectREFR* FindDoorTo(const std::string& locationName);

        /**
         * Find a travel target at the named destination.
         * Searches target cell refs, loaded actors at BGSLocation,
         * and loaded cells belonging to the location.
         * Returns any valid ObjectReference the NPC's AI can pathfind to.
         */
        RE::TESObjectREFR* FindTravelTarget(const std::string& locationName);

        /**
         * Resolve a location name to a travel target.
         * Uses FindTravelTarget internally.
         */
        RE::TESObjectREFR* Resolve(const std::string& locationName);

        /**
         * Resolve a semantic/relative location term.
         * These require door scanning because they're relative to current position.
         *
         * @param actor NPC for context (current cell)
         * @param semanticTerm Relative term (upstairs, outside, etc.)
         * @return Door reference, or nullptr
         */
        RE::TESObjectREFR* ResolveSemantic(RE::Actor* actor, const std::string& semanticTerm);

        /**
         * Resolve semantic intent (avoids re-parsing the string).
         * Takes full intent so resolvers can access preferBeds flag.
         */
        RE::TESObjectREFR* ResolveSemantic(RE::Actor* actor, const SemanticIntent& intent);

        /**
         * Unified destination resolver — handles semantic terms, compound phrases,
         * and named locations in a single pipeline. All intelligence is native C++.
         *
         * Resolution order:
         * 1. Detect semantic intent (exact terms, compound phrases, imperative phrases)
         * 2. Door scanning (name match first, Z second)
         * 3. Parent BGSLocation fallback (outside only)
         * 4. Location context from compound phrase
         * 5. Named location resolution (fuzzy match)
         * 6. Extract location name from string (last resort)
         */
        RE::TESObjectREFR* ResolveAnyDestination(RE::Actor* actor, const std::string& destination);

        /**
         * Check if a term is semantic (relative direction).
         */
        bool IsSemanticTerm(const std::string& term);

        /**
         * Get available semantic directions from actor's location.
         */
        std::vector<RE::BSFixedString> GetAvailableDirections(RE::Actor* actor);

        /**
         * Get human-readable location name for an actor.
         */
        RE::BSFixedString GetActorLocationName(RE::Actor* actor);

        /**
         * Get suggestion for a failed location search.
         */
        RE::BSFixedString GetLocationSuggestion(const std::string& searchTerm);

        /**
         * Get index statistics as JSON.
         */
        RE::BSFixedString GetStatsJSON();

        /**
         * Find the nearest BGSLocation worldLocMarker toward a destination.
         *
         * Iterates all BGSLocations, checks their worldLocMarker:
         * 1. Must be a valid exterior reference (not interior-only)
         * 2. Must be within maxRadius of the actor
         * 3. Must be closer to destination than the actor is
         * Returns the closest qualifying marker, or nullptr.
         *
         * Used by stuck recovery to redirect NPCs to known-good positions
         * at settlement entrances instead of blind vector teleports.
         */
        RE::TESObjectREFR* FindNearestWaypointToward(
            RE::Actor* actor, RE::TESObjectREFR* destination, float maxRadius);

        // ==========================================================================
        // Home Door Access (anti-trespass + pathfinding)
        // ==========================================================================

        /**
         * Unlock/lock an NPC's home front door and set cell public/private.
         * Call with unlock=true before travel, unlock=false on task completion.
         * Uses the NPC home index to find the home cell, then finds the exterior door.
         *
         * @param npc The NPC whose home to access (looks up via ActorBase)
         * @param unlock true=unlock+public, false=lock+private
         * @return Door ref if found, nullptr if NPC has no indexed home or no exterior door
         */
        RE::TESObjectREFR* SetHomeDoorAccess(RE::Actor* npc, bool unlock);

        /**
         * Same as SetHomeDoorAccess but takes a cell FormID directly.
         * Used when the home cell ID was stored by Papyrus (e.g., for re-locking on task completion).
         */
        RE::TESObjectREFR* SetHomeDoorAccessForCell(RE::FormID cellFormId, bool unlock);

        /**
         * Get the home cell FormID from the last successful home resolution.
         * Returns 0 if the last ResolveAnyDestination was not a home destination.
         * Used by Papyrus to store the cell ID for later re-locking.
         */
        RE::FormID GetLastResolvedHomeCellId() const { return m_lastResolvedHomeCellId; }

        /**
         * Get cached LocTypePlayerHouse keyword.
         * Used by CellAnalyzer::IsPlayerInOwnHome().
         */
        RE::BGSKeyword* GetPlayerHouseKeyword() const { return m_locTypePlayerHouse; }

        /**
         * Get the exterior-side door reference for the player's current home.
         * Finds the front door in the player's interior cell, then returns
         * the linked door on the exterior side (for MoveTo teleport targets).
         * @return Exterior door ref, or nullptr if player is not in an interior cell
         */
        RE::TESObjectREFR* GetPlayerHomeExteriorDoorRef();

        /**
         * Get the interior-side door of the player's current home cell.
         * Used for placing NPCs at the doorway so they walk in naturally.
         */
        RE::TESObjectREFR* GetPlayerHomeInteriorDoorRef();

        /**
         * Find NPCs sharing the same home cell as the given actor (household members).
         * Thread-safe: acquires shared_lock on m_mutex.
         * @return Vector of ActorBase FormIDs sharing the same home cell (excluding actor)
         */
        std::vector<RE::FormID> GetHouseholdMembers(RE::Actor* actor);

        // ==========================================================================
        // Dungeon Boss Anchor (pre-placement for rescue/find_item quests)
        // ==========================================================================

        /**
         * Get the boss room anchor for a dungeon location.
         * Uses DungeonIndex built from BGSLocation::specialRefs at startup.
         * Returns a persistent ref in the boss room (accessible even unloaded).
         * @return Boss anchor ref, or nullptr if dungeon not indexed
         */
        RE::TESObjectREFR* GetDungeonBossAnchor(RE::BGSLocation* loc);
        RE::TESObjectREFR* GetDungeonBossAnchor(const std::string& locationName);

    private:
        LocationResolver() = default;
        ~LocationResolver() = default;
        LocationResolver(const LocationResolver&) = delete;
        LocationResolver& operator=(const LocationResolver&) = delete;

        // Semantic term resolution (door scanning + furniture fallback)
        RE::TESObjectREFR* ResolveUpstairs(RE::Actor* actor, bool preferBeds = false);
        RE::TESObjectREFR* ResolveDownstairs(RE::Actor* actor, bool preferBeds = false);
        RE::TESObjectREFR* ResolveOutside(RE::Actor* actor);
        RE::TESObjectREFR* ResolveInside(RE::Actor* actor);
        RE::TESObjectREFR* ResolveBack(RE::Actor* actor);
        RE::TESObjectREFR* ResolveCellar(RE::Actor* actor);
        RE::TESObjectREFR* ResolveBedroom(RE::Actor* actor);
        RE::TESObjectREFR* ResolveKitchen(RE::Actor* actor);
        RE::TESObjectREFR* ResolveWater(RE::Actor* actor, bool preferInterior);

        // Home resolution
        RE::TESObjectREFR* ResolveHome(RE::Actor* actor, const SemanticIntent& intent);
        RE::TESObjectREFR* ResolveNPCHome(RE::TESNPC* actorBase);
        RE::TESObjectREFR* ResolvePlayerHome();

        // Home index: build bed-ownership map at startup
        void BuildHomeIndex();
        RE::TESObjectREFR* GetLocationTravelTarget(RE::TESObjectCELL* homeCell);

        // Find the interior-side door in a cell that leads to an exterior cell.
        RE::TESObjectREFR* FindExteriorDoorInCell(RE::TESObjectCELL* cell);

        // Follow an interior door's teleport link to get the exterior-side door ref.
        RE::TESObjectREFR* GetExteriorLinkedDoor(RE::TESObjectREFR* interiorDoor);

        // Helper: Find door in a cell that leads to target
        RE::TESObjectREFR* FindDoorInCellTo(RE::TESObjectCELL* sourceCell, RE::TESObjectCELL* targetCell);

        // Debug: Log all doors in actor's cell with direction signals
        void LogAllDoors(RE::Actor* actor);

        // Detect semantic intent from any destination string
        SemanticIntent DetectSemanticIntent(const std::string& destination);

        // Resolve via parent BGSLocation (for "outside" fallback)
        RE::TESObjectREFR* ResolveViaParentLocation(RE::Actor* actor);

        // Extract a location name from a compound phrase
        std::string ExtractLocationName(const std::string& text);

        // Thread safety
        mutable std::shared_mutex m_mutex;

        // Cell index: lowercase name -> Cell FormID
        std::unordered_map<std::string, RE::FormID> m_cellIndex;

        // Location index: lowercase name -> BGSLocation FormID
        std::unordered_map<std::string, RE::FormID> m_locationIndex;

        // All indexed names (for fuzzy search)
        std::vector<std::string> m_allCellNames;
        std::vector<std::string> m_allLocationNames;

        // Semantic terms set
        std::unordered_set<std::string> m_semanticTerms = {
            "upstairs", "up", "above",
            "downstairs", "down", "below",
            "outside", "out", "exterior",
            "inside", "in", "interior",
            "the back", "back room", "back",
            "cellar", "basement", "below stairs",
            "my room", "bedroom", "the bedroom", "my bed", "bed", "room",
            "kitchen", "the kitchen",
            "the bar", "counter", "bar counter",
            "near the fire", "fireplace", "hearth",
            "stairs", "stairwell", "staircase",
            "home", "my home", "my house", "my place",
            "your home", "your house", "your place",
            "the river", "river", "lake", "stream", "water", "swim", "wash",
            "bath", "bathe", "the bath", "hot spring"
        };

        // NPC home index: ActorBase FormID -> home cell FormID
        // Built at startup from bed ownership (actor + faction).
        struct HomeInfo {
            RE::FormID cellFormId = 0;   // interior cell containing the owned bed
            RE::FormID bedFormId = 0;    // the bed reference itself
        };
        std::unordered_map<RE::FormID, HomeInfo> m_npcHomeIndex;

        // LocTypePlayerHouse keyword (cached at index build time)
        RE::BGSKeyword* m_locTypePlayerHouse = nullptr;

        // Last home cell resolved by ResolveAnyDestination (for Papyrus to query)
        RE::FormID m_lastResolvedHomeCellId = 0;

        // Dungeon index: BGSLocation FormID → boss room endpoint
        struct DungeonEndpoint {
            RE::FormID anchorRefId = 0;   // boss ref FormID (persistent, works unloaded)
            RE::FormID cellFormId = 0;    // cell containing the anchor
        };
        std::unordered_map<RE::FormID, DungeonEndpoint> m_dungeonIndex;

        // Build dungeon endpoint index from BGSLocation::specialRefs
        void BuildDungeonIndex();

        bool m_indexBuilt = false;
    };

}  // namespace IntelEngine
