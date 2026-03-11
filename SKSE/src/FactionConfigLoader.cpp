/**
 * FactionConfigLoader Implementation
 *
 * Line-based YAML parser for factions.yaml. Extracted from FactionPolitics.cpp
 * to keep that file under the 600 LOC limit.
 */

#include "FactionConfigLoader.h"
#include "Plugin.h"

#include <fstream>
#include <algorithm>
#include <sstream>

namespace IntelEngine {

    // =========================================================================
    // YAML Parsing Helpers
    // =========================================================================

    namespace {

        std::string Trim(const std::string& s) {
            size_t start = s.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return "";
            size_t end = s.find_last_not_of(" \t\r\n");
            return s.substr(start, end - start + 1);
        }

        std::string StripQuotes(const std::string& s) {
            if (s.size() >= 2 &&
                ((s.front() == '"' && s.back() == '"') ||
                 (s.front() == '\'' && s.back() == '\''))) {
                return s.substr(1, s.size() - 2);
            }
            return s;
        }

        std::string StripComment(const std::string& s) {
            bool inQuote = false;
            char quoteChar = 0;
            for (size_t i = 0; i < s.size(); ++i) {
                if (!inQuote && (s[i] == '"' || s[i] == '\'')) {
                    inQuote = true;
                    quoteChar = s[i];
                } else if (inQuote && s[i] == quoteChar) {
                    inQuote = false;
                } else if (!inQuote && s[i] == '#') {
                    return s.substr(0, i);
                }
            }
            return s;
        }

        std::vector<std::string> ParseInlineArray(const std::string& val) {
            std::vector<std::string> result;
            std::string v = Trim(val);
            if (v.size() < 2 || v.front() != '[' || v.back() != ']') return result;
            v = v.substr(1, v.size() - 2);

            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                item = Trim(item);
                item = StripQuotes(item);
                if (!item.empty()) result.push_back(item);
            }
            return result;
        }

        int ParseInt(const std::string& val, int def) {
            try { return std::stoi(Trim(val)); }
            catch (...) { return def; }
        }

        size_t GetIndent(const std::string& line) {
            return line.find_first_not_of(" \t");
        }

        bool IsArrayItem(const std::string& trimmed) {
            return trimmed.size() >= 2 && trimmed[0] == '-' && trimmed[1] == ' ';
        }

        bool ParseKeyValue(const std::string& trimmed, std::string& key, std::string& value) {
            size_t colonPos = trimmed.find(':');
            if (colonPos == std::string::npos || colonPos == 0) return false;

            size_t bracketDepth = 0;
            for (size_t i = 0; i < colonPos; ++i) {
                if (trimmed[i] == '[') bracketDepth++;
                if (trimmed[i] == ']') bracketDepth--;
            }
            if (bracketDepth > 0) return false;

            key = Trim(trimmed.substr(0, colonPos));
            value = Trim(StripComment(trimmed.substr(colonPos + 1)));
            value = StripQuotes(value);
            return true;
        }

    }  // anonymous namespace

    // =========================================================================
    // Config Loading
    // =========================================================================

    FactionConfigLoadResult LoadFactionConfigFromFile(const std::string& path) {
        FactionConfigLoadResult result;

        std::ifstream file(path);
        if (!file.is_open()) {
            logger::warn("FactionConfigLoader: factions.yaml not found at {}", path);
            return result;
        }

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        file.close();

        enum class Section { None, Factions, DefaultRelations };
        Section currentSection = Section::None;

        FactionConfig currentFaction;
        DefaultRelation currentRelation;
        bool hasCurrentItem = false;

        auto flushItem = [&]() {
            if (!hasCurrentItem) return;
            switch (currentSection) {
                case Section::Factions:
                    if (!currentFaction.id.empty()) {
                        result.factions.push_back(std::move(currentFaction));
                    }
                    currentFaction = FactionConfig{};
                    break;
                case Section::DefaultRelations:
                    if (!currentRelation.factionA.empty()) {
                        result.defaultRelations.push_back(std::move(currentRelation));
                    }
                    currentRelation = DefaultRelation{};
                    break;
                default: break;
            }
            hasCurrentItem = false;
        };

        for (size_t i = 0; i < lines.size(); ++i) {
            size_t indent = GetIndent(lines[i]);
            if (indent == std::string::npos) continue;

            std::string trimmed = Trim(lines[i]);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (indent == 0) {
                flushItem();
                if (trimmed == "factions:") currentSection = Section::Factions;
                else if (trimmed == "default_relations:") currentSection = Section::DefaultRelations;
                else currentSection = Section::None;
                continue;
            }

            if (IsArrayItem(trimmed)) {
                flushItem();
                hasCurrentItem = true;

                std::string remainder = Trim(trimmed.substr(2));
                if (!remainder.empty()) {
                    std::string key, value;
                    if (ParseKeyValue(remainder, key, value)) {
                        switch (currentSection) {
                            case Section::Factions:
                                if (key == "id") currentFaction.id = value;
                                else if (key == "name") currentFaction.name = value;
                                break;
                            case Section::DefaultRelations:
                                if (key == "faction_a") currentRelation.factionA = value;
                                break;
                            default: break;
                        }
                    }
                }
                continue;
            }

            if (hasCurrentItem) {
                std::string key, value;
                if (!ParseKeyValue(trimmed, key, value)) continue;

                switch (currentSection) {
                    case Section::Factions:
                        if (key == "id") currentFaction.id = value;
                        else if (key == "name") currentFaction.name = value;
                        else if (key == "type") currentFaction.type = value;
                        else if (key == "hold") currentFaction.hold = value;
                        else if (key == "skyrim_faction_id") currentFaction.skyrimFactionId = value;
                        else if (key == "leader_names") currentFaction.leaderNames = ParseInlineArray(StripComment(lines[i].substr(lines[i].find(':')+1)));
                        else if (key == "rivals") currentFaction.rivals = ParseInlineArray(StripComment(lines[i].substr(lines[i].find(':')+1)));
                        else if (key == "allies") currentFaction.allies = ParseInlineArray(StripComment(lines[i].substr(lines[i].find(':')+1)));
                        else if (key == "base_army_strength") currentFaction.baseArmyStrength = ParseInt(value, 0);
                        else if (key == "war_threshold") currentFaction.warThreshold = ParseInt(value, -50);
                        else if (key == "conflict_style") currentFaction.conflictStyle = value;
                        break;

                    case Section::DefaultRelations:
                        if (key == "faction_a") currentRelation.factionA = value;
                        else if (key == "faction_b") currentRelation.factionB = value;
                        else if (key == "relation") currentRelation.relation = ParseInt(value, 0);
                        break;

                    default: break;
                }
            }
        }
        flushItem();

        result.success = true;
        logger::info("FactionConfigLoader: Loaded {} factions, {} default relations",
                     result.factions.size(), result.defaultRelations.size());
        return result;
    }

}  // namespace IntelEngine
