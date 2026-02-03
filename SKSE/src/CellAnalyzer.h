#pragma once

/**
 * Cell Analyzer
 *
 * Analyzes cell structure to find doors, determine directions,
 * and generate spatial information for prompts.
 */

#include "Plugin.h"

namespace IntelEngine {

    class CellAnalyzer {
    public:
        static CellAnalyzer* GetSingleton() {
            static CellAnalyzer instance;
            return &instance;
        }

        /**
         * Get all doors in the actor's current cell.
         */
        std::vector<RE::TESObjectREFR*> GetDoors(RE::Actor* actor);

        /**
         * Get the destination cell name for a door.
         */
        RE::BSFixedString GetDoorDestinationName(RE::TESObjectREFR* door);

        /**
         * Check if door leads to exterior cell.
         */
        bool IsDoorExterior(RE::TESObjectREFR* door);

        /**
         * Check if door leads to higher elevation (destination Z > current Z).
         */
        bool IsDoorUpward(RE::TESObjectREFR* door);

        /**
         * Check if door leads to lower elevation (destination Z < current Z).
         */
        bool IsDoorDownward(RE::TESObjectREFR* door);

        /**
         * Check if door name or destination cell name suggests upward direction.
         * Checks for keywords: upstairs, upper, second floor, top floor, attic, etc.
         */
        bool IsDoorNameUpward(RE::TESObjectREFR* door);

        /**
         * Check if door name or destination cell name suggests downward direction.
         * Checks for keywords: downstairs, lower, cellar, basement, dungeon, etc.
         */
        bool IsDoorNameDownward(RE::TESObjectREFR* door);

        /**
         * Find furniture references above the actor's Z position.
         * Used for same-cell "upstairs" resolution when no door transition exists.
         * @param minZDiff Minimum Z difference to qualify (default 150 units = different floor)
         */
        std::vector<RE::TESObjectREFR*> FindFurnitureAbove(RE::Actor* actor, float minZDiff = 150.0f);

        /**
         * Find furniture references below the actor's Z position.
         * Used for same-cell "downstairs" resolution when no door transition exists.
         * @param minZDiff Minimum Z difference to qualify (default 150 units = different floor)
         */
        std::vector<RE::TESObjectREFR*> FindFurnitureBelow(RE::Actor* actor, float minZDiff = 150.0f);

        /**
         * Check if a furniture reference is a bed (bed, bedroll, hammock, sleeping bag).
         * Uses base object name and editor ID matching.
         */
        bool IsBedFurniture(RE::TESObjectREFR* ref);

        /**
         * Check if a reference is a cooking station (cooking pot, cooking spit, oven).
         * Checks Furniture and Activator form types.
         */
        bool IsCookingStation(RE::TESObjectREFR* ref);

        /**
         * Find cooking station references in the actor's current cell.
         * Returns all cooking pots, spits, and ovens.
         */
        std::vector<RE::TESObjectREFR*> FindCookingStations(RE::Actor* actor);

        /**
         * Get JSON-formatted spatial information for prompt context.
         */
        RE::BSFixedString GetSpatialInfoJSON(RE::Actor* actor);

        /**
         * Get the teleport destination reference for a door.
         */
        RE::TESObjectREFR* GetDoorDestination(RE::TESObjectREFR* door);

    private:
        CellAnalyzer() = default;
        ~CellAnalyzer() = default;
        CellAnalyzer(const CellAnalyzer&) = delete;
        CellAnalyzer& operator=(const CellAnalyzer&) = delete;

        // Calculate direction from actor to target
        std::string GetDirection(RE::Actor* actor, RE::TESObjectREFR* target);

        // Resolve cell name with fallbacks (BGSLocation, editor ID, worldspace)
        std::string ResolveCellName(RE::Actor* actor, RE::TESObjectCELL* cell);

        // Check if actor is effectively indoors (interior cell OR exterior cell with indoor location keywords)
        bool IsEffectivelyInterior(RE::Actor* actor, RE::TESObjectCELL* cell);

        // Lazily cache interior location keywords (game data doesn't change after load)
        void EnsureKeywordsCached();
        bool m_keywordsCached = false;
        std::vector<RE::BGSKeyword*> m_interiorKeywords;
    };

}  // namespace IntelEngine
