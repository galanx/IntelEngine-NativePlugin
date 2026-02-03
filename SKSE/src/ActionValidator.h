#pragma once

/**
 * Action Validator
 *
 * Pre-validates actions before NPC attempts them.
 * Returns machine-readable status codes - all narration text lives in Papyrus.
 *
 * Status codes:
 *   "" (empty)           = valid / success
 *   "no_destination"     = no destination specified
 *   "no_path"            = semantic direction has no path from here
 *   "location_not_found" = named location could not be resolved
 *   "npc_not_found"      = NPC name could not be matched
 *   "npc_dead"           = matched NPC is dead
 *   "npc_not_accessible" = matched NPC is not reachable
 *   "npc_is_self"        = target NPC is the actor themselves
 *   "npc_is_player"      = target NPC is the player
 *   "item_not_found"     = item not in inventory
 *   "no_actor"           = no actor context provided
 *   "unknown_action"     = unrecognized action type
 */

#include "Plugin.h"

namespace IntelEngine {

    class ActionValidator {
    public:
        static ActionValidator* GetSingleton() {
            static ActionValidator instance;
            return &instance;
        }

        /**
         * Validate if an action can be performed.
         *
         * @param actor NPC who would perform the action (can be nullptr for general validation)
         * @param actionType Type of action (travel, fetch_npc, fetch_item, etc.)
         * @param target Action target parameter
         * @return True if action is valid
         */
        bool Validate(RE::Actor* actor, const std::string& actionType, const std::string& target);

        /**
         * Get reason why action would fail.
         *
         * @return Human-readable failure reason, empty string if valid
         */
        RE::BSFixedString GetFailureReason(RE::Actor* actor,
                                            const std::string& actionType,
                                            const std::string& target);

    private:
        ActionValidator() = default;
        ~ActionValidator() = default;
        ActionValidator(const ActionValidator&) = delete;
        ActionValidator& operator=(const ActionValidator&) = delete;

        // Specific validators
        bool ValidateTravel(RE::Actor* actor, const std::string& destination, std::string& outReason);
        bool ValidateFetchNPC(RE::Actor* actor, const std::string& npcName, std::string& outReason);
        bool ValidateFetchItem(RE::Actor* actor, const std::string& itemName, std::string& outReason);
        bool ValidateDeliverMessage(RE::Actor* actor, const std::string& recipient, std::string& outReason);
    };

}  // namespace IntelEngine
