/**
 * FactionPolitics Implementation
 *
 * YAML parsing is delegated to FactionConfigLoader.cpp.
 * This file handles: initialization, context building, dashboard JSON,
 * political summary, and state file output.
 */

#include "FactionPolitics.h"
#include "FactionConfigLoader.h"
#include "DashboardConfig.h"
#include "SkyrimNetAPI.h"

#include <fstream>
#include <filesystem>
#include <algorithm>

namespace IntelEngine {

    namespace {

        std::string ToLower(const std::string& s) {
            std::string result = s;
            std::transform(result.begin(), result.end(), result.begin(), ::tolower);
            return result;
        }

    }  // anonymous namespace

    // =========================================================================
    // Config Loading (delegates to FactionConfigLoader)
    // =========================================================================

    std::string FactionPolitics::GetConfigPath() const {
        return "Data/SKSE/Plugins/SkyrimNet/config/plugins/IntelEngine/factions.yaml";
    }

    bool FactionPolitics::LoadConfig() {
        auto result = LoadFactionConfigFromFile(GetConfigPath());
        if (!result.success) {
            logger::error("FactionPolitics: Failed to load factions.yaml");
            return false;
        }

        std::lock_guard<std::mutex> lock(configMutex_);
        factions_ = std::move(result.factions);
        defaultRelations_ = std::move(result.defaultRelations);

        factionIndex_.clear();
        nameToId_.clear();
        for (size_t i = 0; i < factions_.size(); ++i) {
            factionIndex_[factions_[i].id] = i;
            nameToId_[ToLower(factions_[i].name)] = factions_[i].id;
        }

        return true;
    }

    // =========================================================================
    // Initialization
    // =========================================================================

    void FactionPolitics::Initialize() {
        LoadSettings();

        if (!enabled_.load()) {
            logger::info("FactionPolitics: Disabled in settings");
            return;
        }

        if (!LoadConfig()) return;

        if (!PoliticalDB::GetSingleton()->IsReady()) {
            logger::error("FactionPolitics: PoliticalDB not initialized");
            return;
        }

        SeedDefaultRelations();
        RecalculateRelationScores();
        initialized_.store(true);

        SnapshotCrimeGoldBaseline();
        WritePoliticalStateFile();
        logger::info("FactionPolitics: Initialized successfully");
    }

    void FactionPolitics::ClearCaches() {
        initialized_.store(false);
        {
            std::lock_guard<std::mutex> lock(configMutex_);
            crimeGoldBaseline_.clear();
        }
        logger::info("FactionPolitics: Caches cleared");
    }

    void FactionPolitics::SeedDefaultRelations() {
        auto* db = PoliticalDB::GetSingleton();
        std::lock_guard<std::mutex> lock(configMutex_);
        for (const auto& rel : defaultRelations_) {
            db->SeedDefaultRelation(rel.factionA, rel.factionB, rel.relation);
        }
        logger::info("FactionPolitics: Seeded {} default relations", defaultRelations_.size());
    }

    void FactionPolitics::RecalculateRelationScores() {
        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return;

        db->ResetAllRelationScores();

        {
            std::lock_guard<std::mutex> lock(configMutex_);
            for (const auto& rel : defaultRelations_) {
                db->SetRelation(rel.factionA, rel.factionB, rel.relation);
            }
        }

        auto events = db->GetAllEventsChronological();
        int replayed = 0;
        for (const auto& evt : events) {
            if (!evt.factionB.empty() && evt.relationDelta != 0) {
                db->AdjustRelation(evt.factionA, evt.factionB, evt.relationDelta);
                replayed++;
            }
        }

        logger::info("FactionPolitics: Recalculated relation scores (replayed {} event deltas)", replayed);
    }

    void FactionPolitics::LoadSettings() {
        auto* config = DashboardConfig::GetSingleton();
        std::string path = "Data/SKSE/Plugins/SkyrimNet/config/plugins/IntelEngine/settings.yaml";

        auto readBool = [&](const std::string& key, bool def) -> bool {
            if (SkyrimNetAPI::GetPluginConfigValue) {
                try {
                    std::string val = SkyrimNetAPI::GetPluginConfigValue("IntelEngine", key.c_str(), def ? "true" : "false");
                    return val == "true" || val == "1";
                } catch (...) {}
            }
            std::string val = config->ReadYamlString(path, "politics", key.substr(key.find('.') + 1), def ? "true" : "false");
            return val == "true" || val == "1";
        };

        auto readInt = [&](const std::string& key, const std::string& yamlKey, int def) -> int {
            if (SkyrimNetAPI::GetPluginConfigValue) {
                try {
                    std::string val = SkyrimNetAPI::GetPluginConfigValue("IntelEngine", key.c_str(), std::to_string(def).c_str());
                    return std::stoi(val);
                } catch (...) {}
            }
            std::string val = config->ReadYamlString(path, "politics", yamlKey, std::to_string(def));
            try { return std::stoi(val); } catch (...) { return def; }
        };

        enabled_.store(readBool("politics.enabled", true));
        tickIntervalHours_.store(readInt("politics.tick_interval_hours", "tick_interval_hours", 6));
        maxRelationChangePerTick_.store(readInt("politics.max_relation_change_per_tick", "max_relation_change_per_tick", 15));
        maxActiveWars_.store(readInt("politics.max_active_wars", "max_active_wars", 2));

        logger::info("FactionPolitics: Settings loaded - enabled={}, tick={}h, maxDelta={}, maxWars={}",
                     enabled_.load(), tickIntervalHours_.load(),
                     maxRelationChangePerTick_.load(), maxActiveWars_.load());
    }

    // =========================================================================
    // Config Access
    // =========================================================================

    std::optional<FactionConfig> FactionPolitics::GetFaction(const std::string& factionId) const {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto it = factionIndex_.find(factionId);
        if (it == factionIndex_.end()) return std::nullopt;
        return factions_[it->second];
    }

    std::string FactionPolitics::GetFactionIdByName(const std::string& displayName) const {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto it = nameToId_.find(ToLower(displayName));
        return (it != nameToId_.end()) ? it->second : "";
    }

    // =========================================================================
    // Relation Helpers
    // =========================================================================

    std::string FactionPolitics::GetRelationStatus(int score) {
        if (score >= RELATION_ALLIANCE) return "Alliance";
        if (score >= RELATION_FRIENDLY) return "Friendly";
        if (score >= RELATION_NEUTRAL)  return "Neutral";
        if (score >= RELATION_TENSE)    return "Tense";
        if (score >= RELATION_HOSTILE)  return "Hostile";
        return "War";
    }

    bool FactionPolitics::IsAtWar(const std::string& factionA, const std::string& factionB) {
        auto* db = PoliticalDB::GetSingleton();
        int score = db->GetRelation(factionA, factionB);

        int threshold = RELATION_HOSTILE - 1;  // Default: below -50
        auto cfgA = GetFaction(factionA);
        auto cfgB = GetFaction(factionB);
        if (cfgA) threshold = (std::max)(threshold, cfgA->warThreshold);
        if (cfgB) threshold = (std::max)(threshold, cfgB->warThreshold);

        return score <= threshold;
    }

    // =========================================================================
    // Event Validation
    // =========================================================================

    bool FactionPolitics::ValidateEvent(const std::string& factionA, const std::string& factionB,
                                         const std::string& eventType, int relationDelta) {
        if (!initialized_.load()) return false;

        if (!GetFaction(factionA)) {
            logger::warn("FactionPolitics: Invalid faction_a: {}", factionA);
            return false;
        }
        if (!factionB.empty() && !GetFaction(factionB)) {
            logger::warn("FactionPolitics: Invalid faction_b: {}", factionB);
            return false;
        }

        int maxDelta = maxRelationChangePerTick_.load();
        if (std::abs(relationDelta) > maxDelta) {
            logger::warn("FactionPolitics: Delta {} exceeds max {} — will be clamped", relationDelta, maxDelta);
        }

        return true;
    }

    // =========================================================================
    // Shared Helpers
    // =========================================================================

    nlohmann::json FactionPolitics::FactionToJson(const FactionConfig& f) {
        nlohmann::json fj;
        fj["id"] = f.id;
        fj["name"] = f.name;
        fj["type"] = f.type;
        fj["hold"] = f.hold;
        if (!f.skyrimFactionId.empty()) fj["skyrim_faction_id"] = f.skyrimFactionId;
        fj["leaders"] = f.leaderNames;
        return fj;
    }

    std::unordered_map<std::string, std::string> FactionPolitics::BuildIdToNameMap() const {
        std::unordered_map<std::string, std::string> idToName;
        std::lock_guard<std::mutex> lock(configMutex_);
        for (const auto& f : factions_) {
            idToName[f.id] = f.name;
        }
        return idToName;
    }

    // =========================================================================
    // Political DM Context
    // =========================================================================

    std::string FactionPolitics::BuildPoliticalContext(float currentGameTime) {
        if (!initialized_.load()) return "{}";

        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return "{}";

        nlohmann::json state;

        // Factions (extended version with combat/political metadata for the DM)
        {
            std::lock_guard<std::mutex> lock(configMutex_);
            nlohmann::json factionsJson = nlohmann::json::array();
            for (const auto& f : factions_) {
                auto fj = FactionToJson(f);
                if (!f.conflictStyle.empty()) fj["conflict_style"] = f.conflictStyle;
                if (f.baseArmyStrength > 0) fj["army_strength"] = f.baseArmyStrength;
                fj["war_threshold"] = f.warThreshold;
                factionsJson.push_back(fj);
            }
            state["factions"] = factionsJson;
        }

        // Current relations
        auto allRelations = db->GetAllRelations();
        nlohmann::json relationsJson = nlohmann::json::array();
        for (const auto& r : allRelations) {
            nlohmann::json rj;
            rj["faction_a"] = r.factionA;
            rj["faction_b"] = r.factionB;
            rj["score"] = r.relationScore;
            rj["status"] = GetRelationStatus(r.relationScore);
            rj["trade_active"] = r.tradeActive;
            rj["war_active"] = r.warActive;
            relationsJson.push_back(rj);
        }
        state["relations"] = relationsJson;

        // Recent events (last 10)
        auto recentEvents = db->GetRecentEvents(10);
        nlohmann::json eventsJson = nlohmann::json::array();
        for (const auto& e : recentEvents) {
            nlohmann::json ej;
            ej["type"] = e.eventType;
            ej["faction_a"] = e.factionA;
            if (!e.factionB.empty()) ej["faction_b"] = e.factionB;
            ej["description"] = e.description;
            ej["delta"] = e.relationDelta;
            eventsJson.push_back(ej);
        }
        state["recent_events"] = eventsJson;

        // Active wars
        auto activeWars = db->GetActiveWars();
        nlohmann::json warsJson = nlohmann::json::array();
        for (const auto& w : activeWars) {
            nlohmann::json wj;
            wj["faction_a"] = w.factionA;
            wj["faction_b"] = w.factionB;
            wj["battles_fought"] = w.battlesFought;
            wj["morale_a"] = w.factionAMorale;
            wj["morale_b"] = w.factionBMorale;
            wj["strength_a"] = w.factionAStrength;
            wj["strength_b"] = w.factionBStrength;
            warsJson.push_back(wj);
        }
        state["active_wars"] = warsJson;

        // Player standings
        auto standings = db->GetAllPlayerStandings();
        nlohmann::json standingsJson = nlohmann::json::object();
        for (const auto& ps : standings) {
            nlohmann::json sj;
            sj["standing"] = ps.standing;
            sj["status"] = GetRelationStatus(ps.standing);
            if (!ps.title.empty()) sj["title"] = ps.title;
            standingsJson[ps.factionId] = sj;
        }
        state["player_standings"] = standingsJson;

        state["current_game_time"] = currentGameTime;

        // Recent player dialogues (for player standing analysis)
        if (SkyrimNetAPI::GetRecentDialogue) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                std::string dialogueJson = SkyrimNetAPI::GetRecentDialogue(player->GetFormID(), 5);
                if (!dialogueJson.empty() && dialogueJson != "[]") {
                    try {
                        state["recent_player_dialogues"] = nlohmann::json::parse(dialogueJson);
                    } catch (...) {
                        state["recent_player_dialogues"] = dialogueJson;
                    }
                }
            }
        }

        nlohmann::json wrapper;
        wrapper["politicalContext"] = state.dump(2);
        wrapper["tickHours"] = std::to_string(tickIntervalHours_.load());
        wrapper["maxActiveWars"] = std::to_string(maxActiveWars_.load());
        wrapper["maxRelationChange"] = std::to_string(maxRelationChangePerTick_.load());

        return wrapper.dump();
    }

    // =========================================================================
    // Dashboard JSON
    // =========================================================================

    std::string FactionPolitics::BuildDashboardJson() {
        if (!initialized_.load()) return "{}";

        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return "{}";

        nlohmann::json dashboard;

        {
            std::lock_guard<std::mutex> lock(configMutex_);
            nlohmann::json factionsJson = nlohmann::json::array();
            for (const auto& f : factions_) {
                factionsJson.push_back(FactionToJson(f));
            }
            dashboard["factions"] = factionsJson;
        }

        auto allRelations = db->GetAllRelations();
        nlohmann::json relationsJson = nlohmann::json::array();
        for (const auto& r : allRelations) {
            nlohmann::json rj;
            rj["a"] = r.factionA;
            rj["b"] = r.factionB;
            rj["score"] = r.relationScore;
            rj["status"] = GetRelationStatus(r.relationScore);
            rj["trade"] = r.tradeActive;
            rj["war"] = r.warActive;
            relationsJson.push_back(rj);
        }
        dashboard["relations"] = relationsJson;

        auto recentEvents = db->GetRecentEvents(20);
        nlohmann::json eventsJson = nlohmann::json::array();
        for (const auto& e : recentEvents) {
            nlohmann::json ej;
            ej["id"] = e.id;
            ej["type"] = e.eventType;
            ej["faction_a"] = e.factionA;
            ej["faction_b"] = e.factionB;
            ej["description"] = e.description;
            ej["delta"] = e.relationDelta;
            ej["time"] = e.gameTime;
            eventsJson.push_back(ej);
        }
        dashboard["events"] = eventsJson;

        auto activeWars = db->GetActiveWars();
        nlohmann::json warsJson = nlohmann::json::array();
        for (const auto& w : activeWars) {
            nlohmann::json wj;
            wj["id"] = w.id;
            wj["faction_a"] = w.factionA;
            wj["faction_b"] = w.factionB;
            wj["battles"] = w.battlesFought;
            wj["morale_a"] = w.factionAMorale;
            wj["morale_b"] = w.factionBMorale;
            wj["strength_a"] = w.factionAStrength;
            wj["strength_b"] = w.factionBStrength;
            warsJson.push_back(wj);
        }
        dashboard["wars"] = warsJson;

        auto standings = db->GetAllPlayerStandings();
        nlohmann::json standingsJson = nlohmann::json::array();
        for (const auto& ps : standings) {
            nlohmann::json sj;
            sj["faction"] = ps.factionId;
            sj["standing"] = ps.standing;
            sj["status"] = GetRelationStatus(ps.standing);
            sj["title"] = ps.title;
            standingsJson.push_back(sj);
        }
        dashboard["player_standings"] = standingsJson;

        dashboard["enabled"] = enabled_.load();
        dashboard["tick_hours"] = tickIntervalHours_.load();

        return dashboard.dump();
    }

    // =========================================================================
    // Political Summary (for Story DM / NPC DM context enrichment)
    // =========================================================================

    std::string FactionPolitics::BuildPoliticalSummary() {
        if (!initialized_.load() || !enabled_.load()) return "";

        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return "";

        std::string md;
        md.reserve(1024);
        md += "## Political Climate\n";

        auto idToName = BuildIdToNameMap();
        auto getName = [&](const std::string& id) -> std::string {
            auto it = idToName.find(id);
            return (it != idToName.end()) ? it->second : id;
        };

        auto allRelations = db->GetAllRelations();
        for (const auto& r : allRelations) {
            if (r.relationScore == 0 && !r.tradeActive && !r.warActive) continue;
            md += "- ";
            md += getName(r.factionA);
            md += " / ";
            md += getName(r.factionB);
            md += ": ";
            md += std::to_string(r.relationScore);
            md += " (";
            md += GetRelationStatus(r.relationScore);
            md += ")";
            if (r.tradeActive) md += " [trade]";
            if (r.warActive) md += " [WAR]";
            md += "\n";
        }

        auto activeWars = db->GetActiveWars();
        if (!activeWars.empty()) {
            md += "Active wars:\n";
            for (const auto& w : activeWars) {
                md += "- ";
                md += getName(w.factionA);
                md += " vs ";
                md += getName(w.factionB);
                md += " (";
                md += std::to_string(w.battlesFought);
                md += " battles, morale ";
                md += std::to_string(w.factionAMorale);
                md += "% / ";
                md += std::to_string(w.factionBMorale);
                md += "%)\n";
            }
        }

        // Last 5 events (compact — DMs don't need full history)
        auto recentEvents = db->GetRecentEvents(5);
        if (!recentEvents.empty()) {
            md += "Recent political events:\n";
            for (const auto& e : recentEvents) {
                md += "- ";
                md += e.description;
                md += " (";
                md += getName(e.factionA);
                if (!e.factionB.empty()) {
                    md += " / ";
                    md += getName(e.factionB);
                }
                md += ", ";
                if (e.relationDelta > 0) md += "+";
                md += std::to_string(e.relationDelta);
                md += ")\n";
            }
        }

        return md;
    }

    // =========================================================================
    // Player Standing Mechanics
    // =========================================================================

    void FactionPolitics::SnapshotCrimeGoldBaseline() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        std::lock_guard<std::mutex> lock(configMutex_);
        crimeGoldBaseline_.clear();

        for (const auto& fac : factions_) {
            if (fac.skyrimFactionId.empty()) continue;

            auto* skyrimFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>(fac.skyrimFactionId);
            if (!skyrimFaction) continue;

            int violent = player->GetViolentCrimeGoldValue(skyrimFaction);
            int nonViolent = player->GetNonViolentCrimeGoldValue(skyrimFaction);
            crimeGoldBaseline_[fac.id] = violent + nonViolent;
        }

        logger::info("FactionPolitics: Snapshotted crime gold baseline for {} factions", crimeGoldBaseline_.size());
    }

    std::string FactionPolitics::GetNPCFactionId(RE::Actor* actor) const {
        if (!actor) return "";

        std::string actorName = actor->GetDisplayFullName();

        std::lock_guard<std::mutex> lock(configMutex_);

        // Tier 1: Leader name match
        for (const auto& fac : factions_) {
            for (const auto& leaderName : fac.leaderNames) {
                if (leaderName == actorName) return fac.id;
            }
        }

        // Tier 2: Skyrim engine faction membership
        for (const auto& fac : factions_) {
            if (fac.skyrimFactionId.empty()) continue;
            auto* skyrimFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>(fac.skyrimFactionId);
            if (skyrimFaction && actor->IsInFaction(skyrimFaction)) {
                return fac.id;
            }
        }

        // No tier 3 (location) — too broad for standing consequences
        return "";
    }

    int FactionPolitics::ProcessPlayerConduct(RE::Actor* reporter, const std::string& factionId,
                                               const std::string& sentiment, const std::string& reason) {
        if (!initialized_.load()) return 0;

        // Map sentiment to delta
        int delta = 0;
        if (sentiment == "strongly_positive") delta = 10;
        else if (sentiment == "positive") delta = 5;
        else if (sentiment == "negative") delta = -5;
        else if (sentiment == "strongly_negative") delta = -10;
        else {
            logger::warn("FactionPolitics: Invalid sentiment '{}'", sentiment);
            return 0;
        }

        // Validate target faction
        if (!GetFaction(factionId)) {
            logger::warn("FactionPolitics: Unknown faction '{}' in conduct report", factionId);
            return 0;
        }

        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return 0;

        auto* cal = RE::Calendar::GetSingleton();
        float gameTime = cal ? cal->GetCurrentGameTime() : 0.0f;

        int changes = 0;

        // Primary: apply delta to the faction the player mentioned
        int newStanding = db->AdjustPlayerStanding(factionId, delta, gameTime);
        ++changes;
        logger::info("FactionPolitics: Player standing with {} changed by {} -> {} ({})",
                     factionId, delta, newStanding, reason);

        // Cross-faction: check if the reporting NPC belongs to a different faction
        std::string reporterFactionId = GetNPCFactionId(reporter);
        if (!reporterFactionId.empty() && reporterFactionId != factionId) {
            auto reporterFaction = GetFaction(reporterFactionId);
            if (reporterFaction) {
                bool isRival = false;
                bool isAlly = false;
                for (const auto& r : reporterFaction->rivals) {
                    if (r == factionId) { isRival = true; break; }
                }
                if (!isRival) {
                    for (const auto& a : reporterFaction->allies) {
                        if (a == factionId) { isAlly = true; break; }
                    }
                }

                if (isRival) {
                    // Praising a rival hurts you with the reporter's faction (and vice versa)
                    int crossDelta = -delta / 2;
                    if (crossDelta != 0) {
                        int crossStanding = db->AdjustPlayerStanding(reporterFactionId, crossDelta, gameTime);
                        ++changes;
                        logger::info("FactionPolitics: Cross-faction: {} standing changed by {} -> {} "
                                     "(player {} rival {})",
                                     reporterFactionId, crossDelta, crossStanding,
                                     delta > 0 ? "praised" : "insulted", factionId);
                    }
                } else if (isAlly) {
                    // Praising an ally helps you with the reporter's faction too (halved)
                    int crossDelta = delta / 2;
                    if (crossDelta != 0) {
                        int crossStanding = db->AdjustPlayerStanding(reporterFactionId, crossDelta, gameTime);
                        ++changes;
                        logger::info("FactionPolitics: Cross-faction: {} standing changed by {} -> {} "
                                     "(player {} ally {})",
                                     reporterFactionId, crossDelta, crossStanding,
                                     delta > 0 ? "praised" : "insulted", factionId);
                    }
                }
            }
        }

        return changes;
    }

    int FactionPolitics::CheckCrimeGoldStandings() {
        if (!initialized_.load()) return 0;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return 0;

        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return 0;

        auto* cal = RE::Calendar::GetSingleton();
        float gameTime = cal ? cal->GetCurrentGameTime() : 0.0f;

        int changes = 0;

        std::lock_guard<std::mutex> lock(configMutex_);
        for (const auto& fac : factions_) {
            if (fac.skyrimFactionId.empty()) continue;

            auto* skyrimFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>(fac.skyrimFactionId);
            if (!skyrimFaction) continue;

            int violent = player->GetViolentCrimeGoldValue(skyrimFaction);
            int nonViolent = player->GetNonViolentCrimeGoldValue(skyrimFaction);
            int totalCrime = violent + nonViolent;

            int baseline = 0;
            auto it = crimeGoldBaseline_.find(fac.id);
            if (it != crimeGoldBaseline_.end()) baseline = it->second;

            int increase = totalCrime - baseline;
            if (increase <= 0) {
                // Update baseline if crime went down (bounty paid off)
                crimeGoldBaseline_[fac.id] = totalCrime;
                continue;
            }

            // Scale: -1 standing per 100 gold of new crime
            int penalty = -(increase / 100);
            if (penalty == 0) continue;

            // Apply and update baseline
            int newStanding = db->AdjustPlayerStanding(fac.id, penalty, gameTime);
            crimeGoldBaseline_[fac.id] = totalCrime;
            ++changes;

            logger::info("FactionPolitics: Crime gold penalty for {} -> {} standing (crime increase: {}g)",
                         fac.id, newStanding, increase);
        }

        return changes;
    }

    int FactionPolitics::DecayPlayerStandings(int decayRate) {
        if (!initialized_.load()) return 0;

        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return 0;

        auto* cal = RE::Calendar::GetSingleton();
        float gameTime = cal ? cal->GetCurrentGameTime() : 0.0f;

        auto standings = db->GetAllPlayerStandings();
        int decayed = 0;

        for (const auto& ps : standings) {
            if (ps.standing == 0) continue;

            int delta = (ps.standing > 0) ? -decayRate : decayRate;
            // Don't overshoot past 0
            if (std::abs(delta) > std::abs(ps.standing)) {
                delta = -ps.standing;
            }

            db->AdjustPlayerStanding(ps.factionId, delta, gameTime);
            ++decayed;
        }

        return decayed;
    }

    // =========================================================================
    // Political State File (pull-based NPC awareness)
    // =========================================================================

    void FactionPolitics::WritePoliticalStateFile() {
        if (!initialized_.load() || !enabled_.load()) return;

        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return;

        // Gather all data before building JSON.
        // Each DB call is independently locked. Since WritePoliticalStateFile is called
        // from Papyrus (game thread, sequential), concurrent mutation is unlikely,
        // but gathering all reads together minimizes the window.
        auto recentEvents = db->GetRecentEvents(10);
        auto allRelations = db->GetAllRelations();
        auto activeWars = db->GetActiveWars();
        auto playerStandings = db->GetAllPlayerStandings();

        nlohmann::json state;

        {
            std::lock_guard<std::mutex> lock(configMutex_);
            nlohmann::json factionsJson = nlohmann::json::array();
            for (const auto& f : factions_) {
                factionsJson.push_back(FactionToJson(f));
            }
            state["factions"] = factionsJson;
        }

        auto idToName = BuildIdToNameMap();
        auto getName = [&](const std::string& id) -> std::string {
            auto it = idToName.find(id);
            return (it != idToName.end()) ? it->second : id;
        };

        // Recent events (last 10, newest first) — include display names
        nlohmann::json eventsJson = nlohmann::json::array();
        for (const auto& e : recentEvents) {
            nlohmann::json ej;
            ej["type"] = e.eventType;
            ej["faction_a"] = e.factionA;
            ej["faction_a_name"] = getName(e.factionA);
            if (!e.factionB.empty()) {
                ej["faction_b"] = e.factionB;
                ej["faction_b_name"] = getName(e.factionB);
            }
            ej["description"] = e.description;
            ej["delta"] = e.relationDelta;
            ej["time"] = e.gameTime;
            eventsJson.push_back(ej);
        }
        state["recent_events"] = eventsJson;

        // Non-neutral relations only — include display names
        nlohmann::json relationsJson = nlohmann::json::array();
        for (const auto& r : allRelations) {
            if (r.relationScore == 0 && !r.tradeActive && !r.warActive) continue;
            nlohmann::json rj;
            rj["a"] = r.factionA;
            rj["b"] = r.factionB;
            rj["a_name"] = getName(r.factionA);
            rj["b_name"] = getName(r.factionB);
            rj["score"] = r.relationScore;
            rj["status"] = GetRelationStatus(r.relationScore);
            if (r.warActive) rj["war"] = true;
            if (r.tradeActive) rj["trade"] = true;
            relationsJson.push_back(rj);
        }
        state["relations"] = relationsJson;

        // Active wars
        if (!activeWars.empty()) {
            nlohmann::json warsJson = nlohmann::json::array();
            for (const auto& w : activeWars) {
                nlohmann::json wj;
                wj["a"] = w.factionA;
                wj["b"] = w.factionB;
                wj["battles"] = w.battlesFought;
                warsJson.push_back(wj);
            }
            state["wars"] = warsJson;
        }

        // Player standings (for NPC awareness of how factions view the player)
        // Array format for Inja template iteration (Inja doesn't support dynamic key access)
        if (!playerStandings.empty()) {
            nlohmann::json standingsJson = nlohmann::json::array();
            for (const auto& ps : playerStandings) {
                if (ps.standing == 0) continue;  // Skip neutral — no useful info for NPCs
                nlohmann::json sj;
                sj["faction_id"] = ps.factionId;
                sj["faction_name"] = getName(ps.factionId);
                sj["standing"] = ps.standing;
                sj["status"] = GetRelationStatus(ps.standing);
                standingsJson.push_back(sj);
            }
            if (!standingsJson.empty()) {
                state["player_standings"] = standingsJson;
            }
        }

        // Write to Data/SKSE/Plugins/IntelEngine/political_state.json
        std::filesystem::path outPath = "Data/SKSE/Plugins/IntelEngine/political_state.json";
        try {
            std::filesystem::create_directories(outPath.parent_path());
            std::ofstream file(outPath, std::ios::trunc);
            if (file.is_open()) {
                file << state.dump(2);
                file.close();
                logger::debug("FactionPolitics: Wrote political_state.json ({} events, {} relations)",
                             recentEvents.size(), relationsJson.size());
            } else {
                logger::warn("FactionPolitics: Failed to open political_state.json for writing");
            }
        } catch (const std::exception& e) {
            logger::warn("FactionPolitics: Exception writing political_state.json: {}", e.what());
        }
    }

}  // namespace IntelEngine
