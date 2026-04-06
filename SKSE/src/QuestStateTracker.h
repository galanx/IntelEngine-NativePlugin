#pragma once

/**
 * QuestStateTracker — Holds active IntelEngine quest state for SkyrimNet decorator.
 *
 * Papyrus pushes quest details into this singleton at dispatch/completion.
 * The registered SkyrimNet decorator reads from it to provide rich quest context
 * instead of the generic "Investigate the threat near [...]" objective text.
 */

#include <mutex>
#include <string>

namespace IntelEngine {

    class QuestStateTracker {
    public:
        static QuestStateTracker* GetSingleton() {
            static QuestStateTracker instance;
            return &instance;
        }

        void SetActive(const std::string& locationName, const std::string& subType,
                       const std::string& enemyType, const std::string& giverName,
                       const std::string& briefing, const std::string& victimName,
                       const std::string& itemName, const std::string& alliedFaction) {
            std::lock_guard lock(m_mutex);
            m_active = true;
            m_locationName = locationName;
            m_subType = subType;
            m_enemyType = enemyType;
            m_giverName = giverName;
            m_briefing = briefing;
            m_victimName = victimName;
            m_itemName = itemName;
            m_alliedFaction = alliedFaction;
        }

        void Clear() {
            std::lock_guard lock(m_mutex);
            m_active = false;
            m_locationName.clear();
            m_subType.clear();
            m_enemyType.clear();
            m_giverName.clear();
            m_briefing.clear();
            m_victimName.clear();
            m_itemName.clear();
            m_alliedFaction.clear();
        }

        /** Build formatted quest info string for the SkyrimNet decorator.
         *  Returns empty string when no quest is active.
         *  Output matches SkyrimNet's quest list format: **Name** (Type): description */
        std::string GetFormattedQuestInfo() const {
            std::lock_guard lock(m_mutex);
            if (!m_active) return "";

            // Map sub-type to display label and quest name prefix
            std::string typeLabel = "Quest";
            std::string namePrefix = "The Trouble";
            if (m_subType == "combat" || m_subType == "faction_combat") {
                typeLabel = "Combat";
                namePrefix = "The Threat";
            } else if (m_subType == "rescue" || m_subType == "faction_rescue") {
                typeLabel = "Rescue";
                namePrefix = "The Rescue";
            } else if (m_subType == "find_item") {
                typeLabel = "Retrieval";
                namePrefix = "The Search";
            } else if (m_subType == "faction_battle") {
                typeLabel = "Faction Battle";
                namePrefix = "The Battle";
            }

            // Build semantic quest name
            std::string questName = namePrefix;
            if (!m_locationName.empty())
                questName += " at " + m_locationName;

            // Build natural objective with quest giver's original words
            std::string objective;
            std::string giver = m_giverName.empty() ? "Someone" : m_giverName;
            std::string enemy = m_enemyType;

            // Strip "faction:" prefix for readability
            if (enemy.size() > 8 && enemy.substr(0, 8) == "faction:")
                enemy = enemy.substr(8);

            // Sanitize briefing — strip double quotes to prevent formatting issues
            std::string briefing = m_briefing;
            for (auto& c : briefing) {
                if (c == '"') c = '\'';
            }

            if (m_subType == "rescue" || m_subType == "faction_rescue") {
                objective = giver + " asked for help rescuing " + (m_victimName.empty() ? "a captive" : m_victimName);
                objective += " from " + (enemy.empty() ? "captors" : enemy);
                if (!m_locationName.empty()) objective += " at " + m_locationName;
            } else if (m_subType == "find_item") {
                objective = giver + " asked for help retrieving " + (m_itemName.empty() ? "an item" : m_itemName);
                if (!m_locationName.empty()) objective += " from " + m_locationName;
            } else if (m_subType == "faction_battle") {
                objective = giver + " asked for help fighting " + (enemy.empty() ? "the enemy" : enemy);
                if (!m_locationName.empty()) objective += " at " + m_locationName;
            } else {
                objective = giver + " asked for help against " + (enemy.empty() ? "a threat" : enemy);
                if (!m_locationName.empty()) objective += " at " + m_locationName;
            }

            // Add faction allegiance for faction quests
            if (!m_alliedFaction.empty())
                objective += " on behalf of the " + m_alliedFaction;

            // Append the quest giver's original words for full context
            if (!briefing.empty())
                objective += " and said: \"" + briefing + "\"";

            // Match SkyrimNet format: **Quest Name** (Type): objective
            return "**" + questName + "** (" + typeLabel + "): " + objective;
        }

    private:
        QuestStateTracker() = default;

        mutable std::mutex m_mutex;
        bool m_active = false;
        std::string m_locationName;
        std::string m_subType;
        std::string m_enemyType;
        std::string m_giverName;
        std::string m_briefing;
        std::string m_victimName;
        std::string m_itemName;
        std::string m_alliedFaction;
    };

}  // namespace IntelEngine
