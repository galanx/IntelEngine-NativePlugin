/**
 * FactionPolitics Implementation
 *
 * YAML parsing is delegated to FactionConfigLoader.cpp.
 * This file handles: initialization, context building, dashboard JSON,
 * political summary, and state file output.
 */

#include "FactionPolitics.h"
#include "FactionConfigLoader.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include "BattleManager.h"
#include "DashboardConfig.h"
#include "MemoryDB.h"
#include "NPCIndex.h"
#include "SkyrimNetAPI.h"
#include "ProcessUtils.h"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <random>
#include <unordered_set>

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

    std::string FactionPolitics::GetDefaultFactionConfig() {
        return R"YAML(# Faction Politics Configuration — auto-generated defaults
# Modders can edit this file to add/modify factions. It will NOT be overwritten by updates.
factions:
  - id: "ImperialFaction"
    name: "Imperial Legion"
    type: "military"
    hold: "Solitude"
    skyrim_faction_id: "CWImperialFaction"
    leader_names: ["General Tullius", "Legate Rikke"]
    rivals: ["StormcloakFaction"]
    allies: ["ThalmorFaction"]
    base_army_strength: 30
    war_threshold: -50
    soldier_template: "LCharSoldierImperial"
    prison_location: "SolitudeCastleDour"
    prison_marker: [0, 0, 0]
  - id: "StormcloakFaction"
    name: "Stormcloaks"
    type: "military"
    hold: "Windhelm"
    skyrim_faction_id: "CWSonsFaction"
    leader_names: ["Ulfric Stormcloak", "Galmar Stone-Fist"]
    rivals: ["ImperialFaction", "ThalmorFaction"]
    allies: []
    base_army_strength: 25
    war_threshold: -50
    soldier_template: "LCharSoldierSons"
    prison_location: "WindhelmPalaceOfTheKings"
    prison_marker: [0, 0, 0]
  - id: "ThalmorFaction"
    name: "Thalmor"
    type: "military"
    hold: "Solitude"
    skyrim_faction_id: "ThalmorFaction"
    leader_names: ["Elenwen", "Ondolemar"]
    rivals: ["StormcloakFaction"]
    allies: ["ImperialFaction"]
    base_army_strength: 20
    war_threshold: -50
    soldier_template: "LCharThalmorMelee1H"
    prison_location: "ThalmorEmbassy"
    prison_marker: [0, 0, 0]
  - id: "CompanionsFaction"
    name: "The Companions"
    type: "guild"
    hold: "Whiterun"
    skyrim_faction_id: "CompanionsFaction"
    leader_names: ["Kodlak Whitemane", "Vilkas"]
    rivals: []
    allies: []
    war_threshold: -60
    conflict_style: "brawl"
    soldier_template: "LCharBanditMeleeAny"
    prison_location: "WhiterunJorrvaskr"
    prison_marker: [0, 0, 0]
  - id: "ThievesGuildFaction"
    name: "Thieves Guild"
    type: "guild"
    hold: "Riften"
    skyrim_faction_id: "ThievesGuildFaction"
    leader_names: ["Mercer Frey", "Brynjolf"]
    rivals: []
    allies: ["BlackBriarFamily"]
    war_threshold: -55
    conflict_style: "sabotage"
    soldier_template: "WEThiefSubChar"
    prison_location: "RiftenRatway"
    prison_marker: [0, 0, 0]
  - id: "CollegeOfWinterholdFaction"
    name: "College of Winterhold"
    type: "guild"
    hold: "Winterhold"
    skyrim_faction_id: "CollegeofWinterholdFaction"
    leader_names: ["Savos Aren", "Mirabelle Ervine"]
    rivals: []
    allies: []
    war_threshold: -65
    conflict_style: "sabotage"
    soldier_template: "LCharBanditWizard"
    prison_location: "CollegeOfWinterholdHallOfTheElements"
    prison_marker: [0, 0, 0]
  - id: "DarkBrotherhoodFaction"
    name: "Dark Brotherhood"
    type: "guild"
    hold: "Falkreath"
    skyrim_faction_id: "DarkBrotherhoodFaction"
    leader_names: ["Astrid", "Nazir"]
    rivals: []
    allies: []
    war_threshold: -50
    conflict_style: "assassination"
    soldier_template: "WEAssassinSubChar"
    prison_location: "DarkBrotherhoodSanctuary"
    prison_marker: [0, 0, 0]
  - id: "SilverBloodFamily"
    name: "Silver-Blood Family"
    type: "political"
    hold: "Markarth"
    skyrim_faction_id: "CrimeFactionReach"
    leader_names: ["Thongvor Silver-Blood", "Thonar Silver-Blood"]
    rivals: []
    allies: []
    war_threshold: -55
    conflict_style: "proxy"
    soldier_template: "LCharForswornMelee1H"
    prison_location: "MarkarthCidhnaMine"
    prison_marker: [0, 0, 0]
  - id: "BlackBriarFamily"
    name: "Black-Briar Family"
    type: "political"
    hold: "Riften"
    skyrim_faction_id: "CrimeFactionRift"
    leader_names: ["Maven Black-Briar"]
    rivals: []
    allies: ["ThievesGuildFaction"]
    war_threshold: -55
    conflict_style: "assassination"
    soldier_template: "WEThiefSubChar"
    prison_location: "RiftenJail"
    prison_marker: [0, 0, 0]
default_relations:
  - faction_a: "ImperialFaction"
    faction_b: "ThalmorFaction"
    relation: 50
  - faction_a: "ImperialFaction"
    faction_b: "StormcloakFaction"
    relation: -30
  - faction_a: "StormcloakFaction"
    faction_b: "ThalmorFaction"
    relation: -40
  - faction_a: "BlackBriarFamily"
    faction_b: "ThievesGuildFaction"
    relation: 60
)YAML";
    }

    bool FactionPolitics::LoadConfig() {
        auto configPath = GetConfigPath();

        // Auto-create factions.yaml with defaults if missing.
        // std::call_once ensures only one thread creates the file even if LoadConfig races.
        static std::once_flag s_createFlag;
        std::call_once(s_createFlag, [&]() {
        if (!std::filesystem::exists(configPath)) {
            logger::info("FactionPolitics: factions.yaml not found, creating defaults at {}", configPath);
            std::filesystem::create_directories(std::filesystem::path(configPath).parent_path());
            std::ofstream out(configPath);
            if (out.is_open()) {
                out << GetDefaultFactionConfig();
                out.close();
                logger::info("FactionPolitics: Default factions.yaml created");
            } else {
                logger::error("FactionPolitics: Failed to create default factions.yaml at {}", configPath);
            }
        }
        });  // end call_once

        auto result = LoadFactionConfigFromFile(configPath);
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
        // Copy under lock, release, then call DB — prevents lock ordering inversion
        // (configMutex_ -> db mutex_ vs db mutex_ -> configMutex_ in RecordPoliticalEvent)
        std::vector<DefaultRelation> relsCopy;
        {
            std::lock_guard<std::mutex> lock(configMutex_);
            relsCopy = defaultRelations_;
        }
        auto* db = PoliticalDB::GetSingleton();
        for (const auto& rel : relsCopy) {
            db->SeedDefaultRelation(rel.factionA, rel.factionB, rel.relation);
        }
        logger::info("FactionPolitics: Seeded {} default relations", relsCopy.size());
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
        warDeclarationCooldownDays_.store(readInt("politics.war_declaration_cooldown_days", "war_declaration_cooldown_days", 7));
        moraleDecayPerTick_.store(readInt("politics.morale_decay_per_tick", "morale_decay_per_tick", 2));

        logger::info("FactionPolitics: Settings loaded - enabled={}, tick={}h, maxDelta={}, maxWars={}, warCooldown={}d, moraleDecay={}",
                     enabled_.load(), tickIntervalHours_.load(),
                     maxRelationChangePerTick_.load(), maxActiveWars_.load(),
                     warDeclarationCooldownDays_.load(), moraleDecayPerTick_.load());
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

    std::string FactionPolitics::GetSoldierTemplate(const std::string& factionId) const {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto it = factionIndex_.find(factionId);
        if (it == factionIndex_.end()) return "";
        return factions_[it->second].soldierTemplate;
    }

    std::vector<std::string> FactionPolitics::GetAllFactionIds() const {
        std::lock_guard<std::mutex> lock(configMutex_);
        std::vector<std::string> ids;
        ids.reserve(factionIndex_.size());
        for (const auto& [id, _] : factionIndex_) {
            ids.push_back(id);
        }
        return ids;
    }

    std::string FactionPolitics::GetFactionRival(const std::string& factionId) const {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto it = factionIndex_.find(factionId);
        if (it == factionIndex_.end()) return "";
        const auto& rivals = factions_[it->second].rivals;
        if (rivals.empty()) return "";
        return rivals[0];
    }

    std::string FactionPolitics::GetFactionWarEnemy(const std::string& factionId) {
        // Check active wars first — if the faction is at war, that's the enemy
        auto* db = PoliticalDB::GetSingleton();
        if (db && db->IsReady()) {
            auto wars = db->GetActiveWars();
            for (const auto& war : wars) {
                if (war.factionA == factionId) return war.factionB;
                if (war.factionB == factionId) return war.factionA;
            }
        }
        // No active war — fall back to configured rival
        return GetFactionRival(factionId);
    }

    // =========================================================================
    // Convenience wrappers for BattleManager
    // =========================================================================

    std::string FactionPolitics::GetFactionDisplayName(const std::string& factionId) const {
        auto faction = GetFaction(factionId);
        return faction ? faction->name : factionId;
    }

    int FactionPolitics::GetPlayerStanding(const std::string& factionId) const {
        auto* db = PoliticalDB::GetSingleton();
        if (!db || !db->IsReady()) return 0;
        return db->GetPlayerStanding(factionId);
    }

    int FactionPolitics::AdjustPlayerStanding(const std::string& factionId, int delta) {
        auto* db = PoliticalDB::GetSingleton();
        if (!db || !db->IsReady()) return 0;
        // MUST use GetCurrentGameTime (days) — same scale as CleanupFutureEvents.
        // GetHoursPassed() returns hours which is always > GetCurrentGameTime() days,
        // causing all standing entries to appear "in the future" and get wiped on load.
        float gameTime = RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetCurrentGameTime() : 0.f;
        return db->AdjustPlayerStanding(factionId, delta, gameTime);
    }

    int FactionPolitics::RecordPoliticalEvent(const std::string& factionA, const std::string& factionB,
                                               const std::string& eventType, const std::string& description,
                                               int delta, float gameTime) {
        auto* db = PoliticalDB::GetSingleton();
        if (!db || !db->IsReady()) return -1;
        if (!ValidateEvent(factionA, factionB, eventType, delta)) return -1;

        int maxDelta = GetMaxRelationChangePerTick();
        delta = std::clamp(delta, -maxDelta, maxDelta);

        int eventId = db->RecordEvent(factionA, factionB, eventType, description, delta, gameTime);

        if (!factionB.empty() && delta != 0) {
            db->AdjustRelation(factionA, factionB, delta);
        }
        if (eventId >= 0) {
            WritePoliticalStateFile();
        }
        return eventId;
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
        return "Critical";
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

        // Recent events (last 15, oldest first so most recent is last = stronger LLM signal)
        auto recentEvents = db->GetRecentEvents(15);
        std::reverse(recentEvents.begin(), recentEvents.end());
        nlohmann::json eventsJson = nlohmann::json::array();
        for (const auto& e : recentEvents) {
            nlohmann::json ej;
            ej["type"] = e.eventType;
            ej["faction_a"] = e.factionA;
            if (!e.factionB.empty()) ej["faction_b"] = e.factionB;
            ej["description"] = e.description;
            ej["delta"] = e.relationDelta;
            float daysAgoF = currentGameTime - e.gameTime;
            int daysAgo = static_cast<int>(daysAgoF);
            ej["days_ago"] = daysAgo;
            // Human-readable recency for LLM context
            if (daysAgoF < 0.25f) ej["when"] = "just now";
            else if (daysAgoF < 1.0f) ej["when"] = "earlier today";
            else if (daysAgo == 1) ej["when"] = "yesterday";
            else ej["when"] = std::to_string(daysAgo) + " days ago";
            eventsJson.push_back(ej);
        }
        state["recent_events"] = eventsJson;

        // Active wars (with duration and recent battle history for DM context)
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
            float warDays = currentGameTime - w.startTime;
            wj["war_days"] = static_cast<int>(warDays);

            // Embed recent battle history (oldest first, most recent last for LLM recency)
            auto battles = db->GetBattlesForWar(w.id, 5);
            if (!battles.empty()) {
                std::reverse(battles.begin(), battles.end());
                nlohmann::json battlesJson = nlohmann::json::array();
                for (const auto& b : battles) {
                    nlohmann::json bj;
                    bj["location"] = b.locationName;
                    bj["attacker"] = b.attacker;
                    bj["result"] = b.result;
                    bj["victor"] = (b.result == "attacker_victory") ? b.attacker : (b.result == "defender_victory") ? b.defender : "draw";
                    bj["attacker_losses"] = b.attackerLosses;
                    bj["defender_losses"] = b.defenderLosses;
                    float daysAgoF = currentGameTime - b.gameTime;
                    int daysAgo = static_cast<int>(daysAgoF);
                    bj["days_ago"] = daysAgo;
                    if (daysAgoF < 0.25f) bj["when"] = "just now";
                    else if (daysAgoF < 1.0f) bj["when"] = "earlier today";
                    else if (daysAgo == 1) bj["when"] = "yesterday";
                    else bj["when"] = std::to_string(daysAgo) + " days ago";
                    if (!b.narrative.empty()) bj["narrative"] = b.narrative;
                    battlesJson.push_back(bj);
                }
                wj["recent_battles"] = battlesJson;
            }

            warsJson.push_back(wj);
        }
        state["active_wars"] = warsJson;

        // War cooldowns — faction pairs that recently ended a war and can't redeclare yet
        int cooldownDays = warDeclarationCooldownDays_.load();
        if (cooldownDays > 0) {
            nlohmann::json cooldownsJson = nlohmann::json::array();
            // Check all relations for Critical pairs with recent war history
            for (const auto& r : allRelations) {
                if (!r.warActive) {  // Only check pairs not currently at war
                    auto recentWar = db->GetMostRecentWar(r.factionA, r.factionB);
                    if (recentWar && recentWar->endTime > 0.0f) {
                        float daysSinceEnd = currentGameTime - recentWar->endTime;
                        if (daysSinceEnd < static_cast<float>(cooldownDays)) {
                            nlohmann::json cj;
                            cj["faction_a"] = r.factionA;
                            cj["faction_b"] = r.factionB;
                            cj["days_remaining"] = cooldownDays - static_cast<int>(daysSinceEnd);
                            cooldownsJson.push_back(cj);
                        }
                    }
                }
            }
            if (!cooldownsJson.empty()) {
                state["war_cooldowns"] = cooldownsJson;
            }
        }

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

        // Faction leaders — all leaders get bio/relationships, those the player
        // interacted with also get memories/dialogue/events
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* npcIndex = NPCIndex::GetSingleton();
            if (player && npcIndex) {
                // Snapshot faction leader data under lock, then release
                struct LeaderInfo { std::string name; std::string factionId; std::string hold; };
                std::vector<LeaderInfo> leaderSnapshot;
                {
                    std::lock_guard<std::mutex> lock(configMutex_);
                    for (const auto& f : factions_) {
                        for (const auto& leaderName : f.leaderNames) {
                            leaderSnapshot.push_back({leaderName, f.id, f.hold});
                        }
                    }
                }

                auto* memDB = MemoryDB::GetSingleton();
                nlohmann::json leadersJson = nlohmann::json::array();

                // Check which leaders are physically nearby (exterior, within range)
                std::unordered_set<std::string> nearbyLeaders;
                auto* playerWorld = player->GetWorldspace();
                if (playerWorld) {
                    for (const auto& info : leaderSnapshot) {
                        auto* leader = npcIndex->FindByName(info.name);
                        if (!leader || !leader->Is3DLoaded()) continue;
                        if (leader->GetWorldspace() != playerWorld) continue;

                        float dx = leader->GetPositionX() - player->GetPositionX();
                        float dy = leader->GetPositionY() - player->GetPositionY();
                        float dz = leader->GetPositionZ() - player->GetPositionZ();
                        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                        if (dist < NEARBY_LEADER_DISTANCE) {
                            nearbyLeaders.insert(info.name);
                        }
                    }
                }

                // Build context for ALL leaders
                for (const auto& info : leaderSnapshot) {
                    nlohmann::json leaderEntry;
                    leaderEntry["name"] = info.name;
                    leaderEntry["faction"] = info.factionId;
                    leaderEntry["hold"] = info.hold;
                    leaderEntry["nearby"] = nearbyLeaders.count(info.name) > 0;

                    // Try to resolve actor for MemoryDB enrichment
                    auto* leader = npcIndex->FindByName(info.name);
                    if (leader && memDB) {
                        RE::FormID leaderId = leader->GetFormID();

                        // Bio + relationships always included (cached, cheap)
                        auto bio = memDB->GetNPCBioSummary(leaderId);
                        if (!bio.empty()) leaderEntry["bio"] = bio;

                        auto relationships = memDB->GetNPCBioRelationships(leaderId);
                        if (!relationships.empty()) leaderEntry["relationships"] = relationships;

                        // Memories, dialogue, events — included when available
                        auto memories = memDB->GetFormattedMemories(leaderId, 3);
                        if (!memories.empty()) leaderEntry["memories"] = memories;

                        auto dialogue = memDB->GetRecentDialogueForActor(leaderId, 3);
                        if (!dialogue.empty()) leaderEntry["recent_dialogue"] = dialogue;

                        auto events = memDB->GetRecentEventsForActor(leaderId, 3);
                        if (!events.empty()) leaderEntry["recent_events"] = events;
                    }

                    leadersJson.push_back(leaderEntry);
                }

                if (!leadersJson.empty()) {
                    state["faction_leaders"] = leadersJson;
                    std::string playerHold = NPCIndex::GetNPCHoldName(player);
                    if (!playerHold.empty()) {
                        state["player_hold"] = playerHold;
                    }
                }
            }
        }

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

        auto recentEvents = db->GetRecentEvents(10);
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

            // Battle history for dashboard display
            auto battles = db->GetBattlesForWar(w.id, 10);
            if (!battles.empty()) {
                nlohmann::json battlesJson = nlohmann::json::array();
                for (const auto& b : battles) {
                    nlohmann::json bj;
                    bj["location"] = b.locationName;
                    bj["attacker"] = b.attacker;
                    bj["defender"] = b.defender;
                    bj["result"] = b.result;
                    bj["attacker_losses"] = b.attackerLosses;
                    bj["defender_losses"] = b.defenderLosses;
                    if (!b.narrative.empty()) bj["narrative"] = b.narrative;
                    battlesJson.push_back(bj);
                }
                wj["recent_battles"] = battlesJson;
            }

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

        // Get active wars FIRST so we can cross-reference with relations
        auto activeWars = db->GetActiveWars();
        // Build a set of active war pairs for quick lookup
        std::set<std::pair<std::string, std::string>> warPairs;
        for (const auto& w : activeWars) {
            warPairs.insert({w.factionA, w.factionB});
            warPairs.insert({w.factionB, w.factionA});  // both directions
        }

        auto allRelations = db->GetAllRelations();
        for (const auto& r : allRelations) {
            // Use actual war records, not stale warActive flag from relation table
            bool isAtWar = warPairs.count({r.factionA, r.factionB}) > 0;
            if (r.relationScore == 0 && !r.tradeActive && !isAtWar) continue;
            md += "- ";
            md += getName(r.factionA);
            md += " [" + r.factionA + "]";
            md += " / ";
            md += getName(r.factionB);
            md += " [" + r.factionB + "]";
            md += ": ";
            md += std::to_string(r.relationScore);
            md += " (";
            md += GetRelationStatus(r.relationScore);
            md += ")";
            if (r.tradeActive) md += " [trade]";
            if (isAtWar) md += " [WAR]";
            md += "\n";
        }
        if (!activeWars.empty()) {
            md += "Active wars:\n";
            for (const auto& w : activeWars) {
                md += "- ";
                md += getName(w.factionA);
                md += " [" + w.factionA + "]";
                md += " vs ";
                md += getName(w.factionB);
                md += " [" + w.factionB + "]";
                md += " (";
                md += std::to_string(w.battlesFought);
                md += " battles, morale ";
                md += std::to_string(w.factionAMorale);
                md += "% / ";
                md += std::to_string(w.factionBMorale);
                md += "%)\n";
            }
        }

        // Last 10 events with timestamps (oldest first, most recent last for LLM recency)
        auto recentEvents = db->GetRecentEvents(10);
        if (!recentEvents.empty()) {
            std::reverse(recentEvents.begin(), recentEvents.end());
            float currentGameTime = RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetCurrentGameTime() : 0.0f;
            md += "Recent political events (oldest first, most recent last):\n";
            for (const auto& e : recentEvents) {
                md += "- (";
                float daysAgoF = currentGameTime - e.gameTime;
                int daysAgo = static_cast<int>(daysAgoF);
                if (daysAgoF < 0.25f) md += "just now";
                else if (daysAgoF < 1.0f) md += "earlier today";
                else if (daysAgo == 1) md += "yesterday";
                else { md += std::to_string(daysAgo); md += " days ago"; }
                md += ") ";
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
    // Shared Helpers
    // =========================================================================

    bool FactionPolitics::IsPlayerAtInn() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;
        auto* loc = player->GetCurrentLocation();
        if (!loc) return false;
        auto* innKeyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeInn");
        return innKeyword && loc->HasKeyword(innKeyword);
    }

    std::optional<FactionConfig> FactionPolitics::GetNPCFaction(RE::Actor* actor) const {
        if (!actor) return std::nullopt;

        std::string actorName = actor->GetDisplayFullName();

        std::lock_guard<std::mutex> lock(configMutex_);

        // Tier 1: Leader name match
        for (const auto& fac : factions_) {
            for (const auto& leaderName : fac.leaderNames) {
                if (leaderName == actorName) return fac;
            }
        }

        // Tier 2: Skyrim engine faction membership
        for (const auto& fac : factions_) {
            if (fac.skyrimFactionId.empty()) continue;
            auto* skyrimFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>(fac.skyrimFactionId);
            if (skyrimFaction && actor->IsInFaction(skyrimFaction)) {
                return fac;
            }
        }

        return std::nullopt;
    }

    // =========================================================================
    // Witnessable Event Detection
    // =========================================================================

    std::string FactionPolitics::GetLatestWitnessableEvent() {
        if (!initialized_.load() || !enabled_.load()) return "";

        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return "";

        auto* cal = RE::Calendar::GetSingleton();
        if (!cal) return "";

        float currentTime = cal->GetCurrentGameTime();
        float tickWindow = static_cast<float>(tickIntervalHours_.load()) / 24.0f;

        auto events = db->GetRecentEvents(1);
        if (events.empty()) return "";

        const auto& evt = events[0];
        if ((currentTime - evt.gameTime) > tickWindow) return "";

        static const std::unordered_map<std::string, std::string> witnessableTypes = {
            {"assassination_attempt", "An assassination attempt"},
            {"brawl", "A brawl"},
            {"sabotage", "A sabotage operation"},
            {"espionage", "An espionage operation"},
            {"border_skirmish", "A border skirmish"}
        };
        auto typeIt = witnessableTypes.find(evt.eventType);
        if (typeIt == witnessableTypes.end()) return "";

        auto idToName = BuildIdToNameMap();
        auto getName = [&](const std::string& id) -> std::string {
            auto it = idToName.find(id);
            return (it != idToName.end()) ? it->second : id;
        };

        std::string result = typeIt->second + ": " + evt.description;
        result += " (" + getName(evt.factionA);
        if (!evt.factionB.empty()) {
            result += " / " + getName(evt.factionB);
        }
        result += ")";

        logger::info("FactionPolitics: Witnessable event: {}", result);
        return result;
    }

    // =========================================================================
    // Event Manifestation (physical spawning near player)
    // =========================================================================

    std::string FactionPolitics::CheckEventManifestation(
        const std::string& factionA, const std::string& factionB,
        const std::string& eventType)
    {
        if (!initialized_.load() || !enabled_.load()) return "";

        // Skip espionage — too subtle for physical manifestation
        if (eventType == "espionage") return "";

        // Only manifest types with clear physical combat (skip sabotage — no visible target)
        if (eventType != "assassination_attempt" && eventType != "brawl" &&
            eventType != "border_skirmish") {
            return "";
        }

        // Guard: no active battle (shared soldier factions would conflict)
        if (BattleManager::GetSingleton()->IsBattleActive()) {
            logger::info("FactionPolitics: Skipping manifestation — active battle");
            return "";
        }

        // Guard: cooldown
        auto* cal = RE::Calendar::GetSingleton();
        if (!cal) return "";
        float currentTime = cal->GetCurrentGameTime();
        float lastTime = lastManifestationTime_.load();
        if (lastTime > 0.0f && (currentTime - lastTime) < (MANIFESTATION_COOLDOWN_HOURS / 24.0f)) {
            logger::info("FactionPolitics: Skipping manifestation — cooldown active");
            return "";
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return "";
        auto* playerCell = player->GetParentCell();
        if (!playerCell) return "";
        bool isInterior = playerCell->IsInteriorCell();

        // Interior: only assassination_attempt manifests (spawn attacker near target leader)
        if (isInterior && eventType != "assassination_attempt") {
            logger::info("FactionPolitics: Skipping manifestation — player is indoors ({})", eventType);
            return "";
        }

        // Get player's hold name
        std::string playerHold = NPCIndex::GetNPCHoldName(player);
        if (playerHold.empty()) return "";

        // Look up faction configs
        auto cfgA = GetFaction(factionA);
        auto cfgB = GetFaction(factionB);
        if (!cfgA) return "";

        // Determine attacker/target based on which faction's hold the player is in
        std::string attackerFaction, targetFaction;
        bool matchedHold = false;

        // For assassination: factionA attacks factionB
        if (eventType == "assassination_attempt") {
            if (cfgB && !cfgB->hold.empty() && playerHold == cfgB->hold) {
                attackerFaction = factionA;
                targetFaction = factionB;
                matchedHold = true;
            }
        }
        // For brawl/border_skirmish: either side's hold works
        else if (eventType == "brawl" || eventType == "border_skirmish") {
            if (cfgB && !cfgB->hold.empty() && playerHold == cfgB->hold) {
                attackerFaction = factionA;
                targetFaction = factionB;
                matchedHold = true;
            } else if (cfgA && !cfgA->hold.empty() && playerHold == cfgA->hold) {
                attackerFaction = factionB;
                targetFaction = factionA;
                matchedHold = true;
            }
        }

        // Also check if any faction leader is loaded near the player (within 5000 units)
        if (!matchedHold) {
            float px = player->GetPositionX();
            float py = player->GetPositionY();
            auto* npcIndex = NPCIndex::GetSingleton();

            auto checkLeadersNearby = [&](const FactionConfig& cfg) -> bool {
                for (const auto& name : cfg.leaderNames) {
                    auto* actor = npcIndex->FindByName(name);
                    if (!actor) continue;
                    float dx = px - actor->GetPositionX();
                    float dy = py - actor->GetPositionY();
                    if ((dx * dx + dy * dy) <= NEARBY_LEADER_DISTANCE * NEARBY_LEADER_DISTANCE) return true;
                }
                return false;
            };

            if (eventType == "assassination_attempt") {
                if (cfgB && checkLeadersNearby(*cfgB)) {
                    attackerFaction = factionA;
                    targetFaction = factionB;
                }
            } else {
                if (cfgB && checkLeadersNearby(*cfgB)) {
                    attackerFaction = factionA;
                    targetFaction = factionB;
                } else if (cfgA && checkLeadersNearby(*cfgA)) {
                    attackerFaction = factionB;
                    targetFaction = factionA;
                }
            }
        }

        if (attackerFaction.empty()) return "";

        // Determine spawn counts by event type (with variance for immersion)
        std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
        int spawnCount = 2;
        bool spawnDefenders = false;
        int defenderCount = 0;

        if (eventType == "assassination_attempt") {
            spawnCount = 2 + (rng() % 2);  // 2-3
        } else if (eventType == "brawl") {
            spawnCount = 2 + (rng() % 3);  // 2-4
            spawnDefenders = true;
            defenderCount = 2 + (rng() % 3);  // 2-4
        } else if (eventType == "border_skirmish") {
            spawnCount = 3 + (rng() % 3);  // 3-5
            spawnDefenders = true;
            defenderCount = 3 + (rng() % 3);  // 3-5
        }

        // Note: cooldown is NOT set here — Papyrus calls ConfirmManifestationCooldown()
        // after verifying that actors actually spawned. This prevents consuming the
        // cooldown when all SpawnBattleSoldiers calls fail.

        // Build JSON response
        nlohmann::json result;
        result["manifest"] = true;
        result["attacker_faction"] = attackerFaction;
        result["target_faction"] = targetFaction;
        result["event_type"] = eventType;
        result["spawn_count"] = spawnCount;
        result["spawn_defenders"] = spawnDefenders;
        result["defender_count"] = defenderCount;

        logger::info("FactionPolitics: Event manifestation — {} attacks {} ({}, {} attackers{})",
                     attackerFaction, targetFaction, eventType, spawnCount,
                     spawnDefenders ? ", " + std::to_string(defenderCount) + " defenders" : "");

        return result.dump();
    }

    void FactionPolitics::ConfirmManifestationCooldown() {
        auto* cal = RE::Calendar::GetSingleton();
        if (cal) {
            lastManifestationTime_.store(cal->GetCurrentGameTime());
            logger::info("FactionPolitics: Manifestation cooldown confirmed by Papyrus");
        }
    }

    void FactionPolitics::ResetManifestationCooldown() {
        lastManifestationTime_.store(0.0f);
        logger::info("FactionPolitics: Manifestation cooldown reset (director override)");
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

        // Snapshot battle state once (single lock) — exempt battle factions from crime gold
        // penalties. Uses playerParticipated (sticky) so exemption survives RemovePlayerFromBattle
        // clearing the side before EndBattle clears the battle entirely.
        auto battleSnap = BattleManager::GetSingleton()->GetBattleSnapshot();

        int changes = 0;

        std::lock_guard<std::mutex> lock(configMutex_);
        for (const auto& fac : factions_) {
            if (fac.skyrimFactionId.empty()) continue;

            // Skip crime gold checks for factions in the active battle when player participated
            if (battleSnap.active && battleSnap.playerParticipated &&
                (fac.id == battleSnap.factionA || fac.id == battleSnap.factionB)) continue;

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
    // War Lifecycle
    // =========================================================================

    int FactionPolitics::DeclareWar(const std::string& factionA, const std::string& factionB, float gameTime) {
        if (!initialized_.load()) return -1;

        auto cfgA = GetFaction(factionA);
        auto cfgB = GetFaction(factionB);
        if (!cfgA || !cfgB) {
            logger::warn("FactionPolitics::DeclareWar: Invalid faction(s): {} / {}", factionA, factionB);
            return -1;
        }

        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return -1;

        // Check max active wars
        auto activeWars = db->GetActiveWars();
        if (static_cast<int>(activeWars.size()) >= maxActiveWars_.load()) {
            logger::info("FactionPolitics::DeclareWar: Max active wars ({}) reached", maxActiveWars_.load());
            return -1;
        }

        // Check cooldown — no war between these factions within cooldown period
        int cooldownDays = warDeclarationCooldownDays_.load();
        auto recentWar = db->GetMostRecentWar(factionA, factionB);
        if (recentWar && recentWar->endTime > 0.0f) {
            float daysSinceEnd = gameTime - recentWar->endTime;
            if (daysSinceEnd < static_cast<float>(cooldownDays)) {
                logger::info("FactionPolitics::DeclareWar: Cooldown active ({:.1f} days since last war, need {})",
                             daysSinceEnd, cooldownDays);
                return -1;
            }
        }

        // Use base army strength from config
        int strengthA = cfgA->baseArmyStrength > 0 ? cfgA->baseArmyStrength : 50;
        int strengthB = cfgB->baseArmyStrength > 0 ? cfgB->baseArmyStrength : 50;
        // Normalize to percentage scale (0-100) relative to the stronger side
        int maxStrength = (std::max)(strengthA, strengthB);
        if (maxStrength > 0) {
            strengthA = (strengthA * 100) / maxStrength;
            strengthB = (strengthB * 100) / maxStrength;
        }

        int warId = db->StartWar(factionA, factionB, gameTime, strengthA, strengthB);
        if (warId < 0) return -1;

        logger::info("FactionPolitics: WAR #{} declared: {} vs {} (strength {}/{})",
                     warId, factionA, factionB, strengthA, strengthB);

        WritePoliticalStateFile();
        return warId;
    }

    std::string FactionPolitics::ProcessWarTick(float gameTime) {
        if (!initialized_.load()) return "[]";

        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return "[]";

        auto activeWars = db->GetActiveWars();
        if (activeWars.empty()) return "[]";

        int decayRate = moraleDecayPerTick_.load();
        nlohmann::json updates = nlohmann::json::array();

        for (auto& war : activeWars) {
            // Apply morale decay to both sides
            int newMoraleA = (std::max)(0, war.factionAMorale - decayRate);
            int newMoraleB = (std::max)(0, war.factionBMorale - decayRate);

            nlohmann::json update;
            update["war_id"] = war.id;
            update["faction_a"] = war.factionA;
            update["faction_b"] = war.factionB;
            update["morale_a"] = newMoraleA;
            update["morale_b"] = newMoraleB;
            update["strength_a"] = war.factionAStrength;
            update["strength_b"] = war.factionBStrength;
            update["battles"] = war.battlesFought;

            // Check surrender conditions: morale < 20
            bool aCollapsed = newMoraleA < 20;
            bool bCollapsed = newMoraleB < 20;

            if (aCollapsed && bCollapsed) {
                // Both collapsed — the side with higher morale wins (or draw)
                std::string victor = (newMoraleA >= newMoraleB) ? war.factionA : war.factionB;
                update["ended"] = true;
                update["victor"] = victor;
                update["end_reason"] = "mutual_exhaustion";
                db->EndWar(war.id, victor, gameTime);
                logger::info("FactionPolitics: War #{} ended — mutual exhaustion, victor: {}", war.id, victor);
            } else if (aCollapsed) {
                update["ended"] = true;
                update["victor"] = war.factionB;
                update["end_reason"] = "surrender";
                db->EndWar(war.id, war.factionB, gameTime);
                logger::info("FactionPolitics: War #{} ended — {} surrendered to {}", war.id, war.factionA, war.factionB);
            } else if (bCollapsed) {
                update["ended"] = true;
                update["victor"] = war.factionA;
                update["end_reason"] = "surrender";
                db->EndWar(war.id, war.factionA, gameTime);
                logger::info("FactionPolitics: War #{} ended — {} surrendered to {}", war.id, war.factionB, war.factionA);
            } else {
                // Update morale in DB
                db->UpdateWarState(war.id, newMoraleA, newMoraleB,
                                   war.factionAStrength, war.factionBStrength, war.battlesFought);
            }

            updates.push_back(update);
        }

        if (!updates.empty()) {
            WritePoliticalStateFile();
        }

        return updates.dump();
    }

    bool FactionPolitics::EndWar(const std::string& factionA, const std::string& factionB,
                                  const std::string& victor, float gameTime) {
        if (!initialized_.load()) return false;

        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return false;

        auto war = db->GetActiveWar(factionA, factionB);
        if (!war) {
            logger::warn("FactionPolitics::EndWar: No active war between {} and {}", factionA, factionB);
            return false;
        }

        bool ok = db->EndWar(war->id, victor, gameTime);
        if (ok) {
            // Improve relations slightly on peace
            db->AdjustRelation(factionA, factionB, 10);
            WritePoliticalStateFile();
        }
        return ok;
    }

    int FactionPolitics::GetActiveWarCount() {
        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return 0;
        return static_cast<int>(db->GetActiveWars().size());
    }

    int FactionPolitics::GetActiveWarId(const std::string& factionA, const std::string& factionB) {
        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return -1;
        auto war = db->GetActiveWar(factionA, factionB);
        return war ? war->id : -1;
    }

    int FactionPolitics::GetWarStrength(const std::string& factionA, const std::string& factionB,
                                         const std::string& queryFaction) {
        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return 0;

        auto war = db->GetActiveWar(factionA, factionB);
        if (!war) return 0;

        if (queryFaction == war->factionA) return war->factionAStrength;
        if (queryFaction == war->factionB) return war->factionBStrength;
        return 0;
    }

    int FactionPolitics::GetWarMorale(const std::string& factionA, const std::string& factionB,
                                       const std::string& queryFaction) {
        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return -1;

        auto war = db->GetActiveWar(factionA, factionB);
        if (!war) return -1;

        if (queryFaction == war->factionA) return war->factionAMorale;
        if (queryFaction == war->factionB) return war->factionBMorale;
        return -1;
    }

    int FactionPolitics::RecordOffScreenBattle(const std::string& factionA, const std::string& factionB,
                                                const std::string& location, const std::string& result,
                                                const std::string& narrative, int attackerLosses,
                                                int defenderLosses, const std::string& victor) {
        auto* db = PoliticalDB::GetSingleton();
        if (!db->IsReady()) return -1;

        auto war = db->GetActiveWar(factionA, factionB);
        if (!war) {
            logger::warn("FactionPolitics::RecordOffScreenBattle: No active war between {} and {}", factionA, factionB);
            return -1;
        }

        auto* cal = RE::Calendar::GetSingleton();
        float gameTime = cal ? cal->GetCurrentGameTime() : 0.0f;

        int battleId = db->RecordBattle(war->id, location, gameTime,
                                         factionA, factionB, result,
                                         attackerLosses, defenderLosses, narrative);

        // Map caller's attacker/defender losses to alphabetical war factions.
        // Caller's factionA = attacker, factionB = defender.
        // War struct stores factions in alphabetical order.
        bool callerAIsWarA = (factionA == war->factionA);
        int lossForWarA = callerAIsWarA ? attackerLosses : defenderLosses;
        int lossForWarB = callerAIsWarA ? defenderLosses : attackerLosses;

        int moraleA = war->factionAMorale;
        int moraleB = war->factionBMorale;
        int strengthA = war->factionAStrength;
        int strengthB = war->factionBStrength;

        if (victor == war->factionA) {
            moraleA = (std::min)(100, moraleA + 10);
            moraleB = (std::max)(0, moraleB - 15);
        } else if (victor == war->factionB) {
            moraleB = (std::min)(100, moraleB + 10);
            moraleA = (std::max)(0, moraleA - 15);
        } else {
            // Draw
            moraleA = (std::max)(0, moraleA - 5);
            moraleB = (std::max)(0, moraleB - 5);
        }

        strengthA = (std::max)(0, strengthA - lossForWarA);
        strengthB = (std::max)(0, strengthB - lossForWarB);

        db->UpdateWarState(war->id, moraleA, moraleB, strengthA, strengthB, war->battlesFought + 1);

        // Worsen inter-faction relations after each battle
        db->AdjustRelation(factionA, factionB, -10);

        WritePoliticalStateFile();
        return battleId;
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

        // Recent events (last 10, oldest first — most recent last for LLM recency)
        std::reverse(recentEvents.begin(), recentEvents.end());
        float stateGameTime = RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetCurrentGameTime() : 0.0f;
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
            float daysAgoF = stateGameTime - e.gameTime;
            int daysAgo = static_cast<int>(daysAgoF);
            if (daysAgoF < 0.25f) ej["when"] = "just now";
            else if (daysAgoF < 1.0f) ej["when"] = "earlier today";
            else if (daysAgo == 1) ej["when"] = "yesterday";
            else ej["when"] = std::to_string(daysAgo) + " days ago";
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

    // =========================================================================
    // Story Engine helpers (non-faction-specific)
    // =========================================================================

    std::string FactionPolitics::BuildExcludeList(int toggleBitmask, int envFlags) {
        // Toggle bits: 0=seekPlayer, 1=informant, 2=roadEncounter, 3=ambush,
        // 4=stalker, 5=message, 6=quest, 7=factionAmbush,
        // 8=questCombat, 9=questRescue, 10=questFindItem,
        // 11=questFactionCombat, 12=questFactionRescue, 13=questFactionBattle
        // Env bits: 0=isInterior, 1=isDangerous

        std::vector<std::string> excludes;
        auto addIfDisabled = [&](int bit, const char* name) {
            if (!(toggleBitmask & (1 << bit))) excludes.push_back(name);
        };

        addIfDisabled(0, "seek_player");
        addIfDisabled(1, "informant");
        addIfDisabled(2, "road_encounter");
        addIfDisabled(3, "ambush");
        addIfDisabled(4, "stalker");
        addIfDisabled(5, "message");
        addIfDisabled(6, "quest");
        addIfDisabled(7, "faction_ambush");

        // Quest sub-types (only if quest itself is enabled)
        if (toggleBitmask & (1 << 6)) {
            addIfDisabled(8, "quest_combat");
            addIfDisabled(9, "quest_rescue");
            addIfDisabled(10, "quest_find_item");
            addIfDisabled(11, "quest_faction_combat");
            addIfDisabled(12, "quest_faction_rescue");
            addIfDisabled(13, "quest_faction_battle");
        }

        // Environment auto-excludes (exact match, no substring issues)
        bool isInterior = (envFlags & 1) != 0;
        bool isDangerous = (envFlags & 2) != 0;

        auto addUnique = [&](const char* name) {
            for (const auto& e : excludes) {
                if (e == name) return;  // exact match, not substring
            }
            excludes.push_back(name);
        };

        if (isInterior) {
            addUnique("stalker");
            addUnique("ambush");
            addUnique("road_encounter");
            addUnique("faction_ambush");
        }
        if (isDangerous) {
            addUnique("informant");
        }

        // Join with ", "
        std::string result;
        for (size_t i = 0; i < excludes.size(); ++i) {
            if (i > 0) result += ", ";
            result += excludes[i];
        }
        return result;
    }

    std::string FactionPolitics::ValidateStoryResponse(const std::string& responseJson,
                                                        int toggleBitmask, int envFlags) {
        nlohmann::json out;
        out["valid"] = false;

        nlohmann::json resp;
        try {
            resp = nlohmann::json::parse(responseJson);
        } catch (...) {
            out["reason"] = "JSON parse error";
            return out.dump();
        }

        bool shouldAct = resp.value("should_act", false);
        if (!shouldAct) {
            out["reason"] = "should_act is false";
            return out.dump();
        }

        std::string storyType = resp.value("type", "");
        if (storyType.empty()) {
            out["reason"] = "missing type field";
            return out.dump();
        }

        // Check if story type is excluded
        std::string excludeList = BuildExcludeList(toggleBitmask, envFlags);
        // Exact match check (not substring)
        std::istringstream stream(excludeList);
        std::string token;
        while (std::getline(stream, token, ',')) {
            // Trim whitespace
            size_t start = token.find_first_not_of(' ');
            if (start != std::string::npos) {
                token = token.substr(start);
            }
            if (token == storyType) {
                out["reason"] = "type '" + storyType + "' is disabled";
                return out.dump();
            }
        }

        // Quest subtype validation
        if (storyType == "quest") {
            std::string subType = resp.value("questSubType", "");
            if (subType.empty()) {
                out["reason"] = "quest type missing questSubType";
                return out.dump();
            }

            // Whitelist of known subtypes
            static const std::unordered_set<std::string> validSubTypes = {
                "combat", "rescue", "find_item",
                "faction_combat", "faction_rescue", "faction_battle"
            };
            if (validSubTypes.find(subType) == validSubTypes.end()) {
                out["reason"] = "unknown questSubType: " + subType;
                return out.dump();
            }

            // Check if subtype is excluded
            std::string questExcludeKey = "quest_" + subType;
            std::istringstream stream2(excludeList);
            while (std::getline(stream2, token, ',')) {
                size_t start = token.find_first_not_of(' ');
                if (start != std::string::npos) token = token.substr(start);
                if (token == questExcludeKey) {
                    out["reason"] = "questSubType '" + subType + "' is disabled";
                    return out.dump();
                }
            }

            // Per-subtype field presence validation
            std::string questLoc = resp.value("questLocation", "");
            std::string enemyType = resp.value("enemyType", "");
            std::string victimName = resp.value("victimName", "");
            std::string alliedFaction = resp.value("alliedFaction", "");

            if (subType == "faction_rescue") {
                if (questLoc.empty() || enemyType.empty() || victimName.empty() || alliedFaction.empty()) {
                    out["reason"] = "faction_rescue missing questLocation, enemyType, victimName, or alliedFaction";
                    return out.dump();
                }
            } else if (subType == "rescue") {
                if (questLoc.empty() || enemyType.empty() || victimName.empty()) {
                    out["reason"] = "rescue missing questLocation, enemyType, or victimName";
                    return out.dump();
                }
            } else if (subType == "faction_combat") {
                if (questLoc.empty() || enemyType.empty() || alliedFaction.empty()) {
                    out["reason"] = "faction_combat missing questLocation, enemyType, or alliedFaction";
                    return out.dump();
                }
            } else if (subType == "faction_battle") {
                if (questLoc.empty() || alliedFaction.empty()) {
                    out["reason"] = "faction_battle missing questLocation or alliedFaction";
                    return out.dump();
                }
            } else {
                // combat, find_item, etc.
                if (questLoc.empty() || enemyType.empty()) {
                    out["reason"] = "quest missing questLocation or enemyType";
                    return out.dump();
                }
            }
        }

        out["valid"] = true;
        out["type"] = storyType;
        return out.dump();
    }

    // =========================================================================
    // Phase 3 Migration: StoryEngine helpers
    // =========================================================================

    std::string FactionPolitics::BuildFactionBattleDispatchFact(const std::string& alliedFaction,
                                                                 const std::string& questLocation,
                                                                 const std::string& playerName) {
        std::string allyName = GetFactionDisplayName(alliedFaction);
        return "delivered word that the " + allyName + " needed reinforcements near " +
               questLocation + " and asked " + playerName + " to join the fight";
    }

    std::string FactionPolitics::RecordFactionBattleCompletion(const std::string& alliedFaction,
                                                                const std::string& questLocation,
                                                                const std::string& playerName,
                                                                const std::string& enemyFaction) {
        nlohmann::json out;
        std::string allyName = GetFactionDisplayName(alliedFaction);

        // Notification
        out["notification"] = "The " + allyName + " will remember your valor at " + questLocation + ".";

        // Quest giver fact
        out["questGiverFact"] = "learned that " + playerName + " answered the call and fought alongside the " + allyName + " at " + questLocation;

        // Leader fact (different wording — they received word, weren't there)
        out["leaderFact"] = "received word that " + playerName + " fought alongside the " + allyName + " at " + questLocation;

        // Political event — use actual enemy faction (not re-derived)
        std::string enemyId = enemyFaction.empty() ? GetFactionWarEnemy(alliedFaction) : enemyFaction;
        std::string battleDesc = "someone believed to be " + playerName +
            " fought alongside " + allyName + " forces at " + questLocation;
        if (!enemyId.empty()) {
            std::string enemyName = GetFactionDisplayName(enemyId);
            if (!enemyName.empty()) {
                battleDesc += ", helping drive back the " + enemyName;
            }
        }

        auto* cal = RE::Calendar::GetSingleton();
        float gameTime = cal ? cal->GetCurrentGameTime() : 0.f;
        RecordPoliticalEvent(alliedFaction, "", "player_combat", battleDesc, 0, gameTime);

        out["allyName"] = allyName;
        return out.dump();
    }

    std::string FactionPolitics::BuildBattleExpiryFact(const std::string& alliedFaction,
                                                        const std::string& questLocation,
                                                        const std::string& playerName) {
        std::string allyName = GetFactionDisplayName(alliedFaction);
        return "learned that " + playerName + " never arrived to help the " + allyName +
               " at " + questLocation + " despite promising to join the fight";
    }

    // =========================================================================
    // Phase 2 Migration: Politics logic moved from Papyrus to C++
    // =========================================================================

    std::string FactionPolitics::ProcessPoliticalDMResponse(const std::string& response, int success) {
        nlohmann::json out;
        out["acted"] = false;

        logger::info("Politics DM response: success={}, len={}, first100='{}'",
            success, response.size(), response.substr(0, 100));

        if (success != 1 || response.empty()) {
            out["error"] = "DM response failed or empty";
            return out.dump();
        }

        // Strip non-JSON content (markdown fences, reasoning preamble, trailing text)
        std::string cleanResponse = response;
        // Markdown fences
        auto fenceStart = cleanResponse.find("```");
        if (fenceStart != std::string::npos) {
            auto contentStart = cleanResponse.find('\n', fenceStart);
            if (contentStart != std::string::npos) {
                contentStart++;
                auto fenceEnd = cleanResponse.rfind("```");
                if (fenceEnd != std::string::npos && fenceEnd > fenceStart) {
                    cleanResponse = cleanResponse.substr(contentStart, fenceEnd - contentStart);
                }
            }
        }
        // Strip text before first '{' and after last '}'
        auto jsonStart = cleanResponse.find('{');
        if (jsonStart != std::string::npos && jsonStart > 0) {
            cleanResponse = cleanResponse.substr(jsonStart);
        }
        auto jsonEnd = cleanResponse.rfind('}');
        if (jsonEnd != std::string::npos && jsonEnd < cleanResponse.size() - 1) {
            cleanResponse = cleanResponse.substr(0, jsonEnd + 1);
        }
        while (!cleanResponse.empty() && (cleanResponse.back() == '\n' || cleanResponse.back() == '\r' || cleanResponse.back() == ' '))
            cleanResponse.pop_back();
        if (cleanResponse.size() != response.size()) {
            logger::info("Politics DM: stripped non-JSON content, clean len={}", cleanResponse.size());
        }

        nlohmann::json resp;
        try {
            resp = nlohmann::json::parse(cleanResponse);
        } catch (const std::exception& e) {
            logger::error("Politics: JSON parse error: {} — first 200 chars: '{}'", e.what(), cleanResponse.substr(0, 200));
            out["error"] = "JSON parse error";
            return out.dump();
        }

        bool shouldAct = resp.value("should_act", false);
        logger::info("Politics DM: should_act={}", shouldAct);
        if (!shouldAct) {
            out["noAction"] = true;
            return out.dump();
        }

        std::string factionA = resp.value("faction_a", "");
        std::string factionB = resp.value("faction_b", "");
        std::string eventType = resp.value("event_type", "");
        std::string description = resp.value("description", "");
        int delta = resp.value("relation_delta", 0);

        logger::info("Politics DM: factionA={}, factionB={}, type={}, delta={}, desc={}",
            factionA, factionB, eventType, delta, description.substr(0, 80));

        if (factionA.empty() || eventType.empty()) {
            logger::warn("Politics DM: rejected — missing faction_a or event_type");
            out["error"] = "missing faction_a or event_type";
            return out.dump();
        }

        auto* cal = RE::Calendar::GetSingleton();
        float gameTime = cal ? cal->GetCurrentGameTime() : 0.f;

        // Record event (handles validation, clamping, relation adjustment, state file)
        int eventId = RecordPoliticalEvent(factionA, factionB, eventType, description, delta, gameTime);
        if (eventId < 0) {
            logger::error("Politics DM: RecordPoliticalEvent FAILED for {} vs {} (type={})",
                factionA, factionB, eventType);
            out["error"] = "event recording failed";
            return out.dump();
        }
        logger::info("Politics DM: Event #{} recorded — {} vs {} ({}), delta={}",
            eventId, factionA, factionB, eventType, delta);

        out["acted"] = true;
        out["eventId"] = eventId;
        out["eventType"] = eventType;
        out["factionA"] = factionA;
        out["factionB"] = factionB;
        out["description"] = description;

        // --- Handle war declaration ---
        if (eventType == "war_declaration") {
            int warId = DeclareWar(factionA, factionB, gameTime);
            out["warDeclared"] = (warId >= 0);
            out["warId"] = warId;
            logger::info("Politics: WAR #{} DECLARED — {} vs {}", warId, factionA, factionB);
        }

        // --- Handle surrender ---
        if (eventType == "surrender") {
            bool ended = EndWar(factionA, factionB, factionB, gameTime);
            out["warEnded"] = ended;
            logger::info("Politics: WAR ENDED — {} surrendered to {}", factionA, factionB);
        }

        // --- Handle off-screen battle result (ONLY during active war) ---
        // NOTE: GetActiveWar acquires PoliticalDB::mutex_. Safe here because
        // ProcessPoliticalDMResponse is called from Papyrus thread, not from a DB write context.
        if (eventType == "battle_result") {
            auto war = PoliticalDB::GetSingleton()->GetActiveWar(factionA, factionB);
            if (!war) {
                logger::info("Politics: battle_result ignored — no active war between {} and {}", factionA, factionB);
                out["acted"] = false;
                return out.dump();
            }

            std::string battleLoc = resp.value("battle_location", "the field");
            if (battleLoc.empty()) battleLoc = "the field";

            int pendingId = BattleManager::GetSingleton()->AddPendingBattle(
                battleLoc, factionA, factionB, response);

            if (pendingId >= 0) {
                out["pendingBattleId"] = pendingId;
                out["battleLocation"] = battleLoc;
                out["startPendingPoll"] = true;
                // Notification text for Papyrus
                std::string nameA = GetFactionDisplayName(factionA);
                std::string nameB = GetFactionDisplayName(factionB);
                out["notification"] = nameA + " forces engage " + nameB + " at " + battleLoc + "!";
                logger::info("Politics: Pending battle #{} at {} — {} vs {}",
                    pendingId, battleLoc, factionA, factionB);
            } else {
                // Location unresolvable — fall back to off-screen
                std::string battleResult = resp.value("battle_result", "draw");
                std::string victor = resp.value("battle_victor", "");
                int lossesA = std::clamp(resp.value("attacker_losses", 0), 0, 30);
                int lossesB = std::clamp(resp.value("defender_losses", 0), 0, 30);
                RecordOffScreenBattle(factionA, factionB, battleLoc, battleResult,
                    description, lossesA, lossesB, victor);
                out["offScreenResolved"] = true;
                logger::info("Politics: Off-screen battle at {} (location unresolvable)", battleLoc);
            }
        }

        // --- Handle scheduled battle ---
        if (eventType == "battle_scheduled") {
            std::uniform_real_distribution<float> delayDist(6.f, 12.f);
            thread_local std::mt19937 rng(std::random_device{}());
            float delayHours = delayDist(rng);
            float battleTime = gameTime + delayHours / 24.f;
            int warId = GetActiveWarId(factionA, factionB);

            out["scheduleBattle"] = true;
            out["scheduleFactionA"] = factionA;
            out["scheduleFactionB"] = factionB;
            out["scheduleWarId"] = warId;
            out["scheduleBattleTime"] = battleTime;
            logger::info("Politics: Battle scheduled — {} vs {} in {:.1f}h", factionA, factionB, delayHours);
        }

        // --- Check manifestation ---
        if (eventType == "assassination_attempt" || eventType == "brawl" ||
            eventType == "border_skirmish") {
            std::string manifestJson = CheckEventManifestation(factionA, factionB, eventType);
            if (!manifestJson.empty()) {
                out["manifestJson"] = manifestJson;
            }
        }

        // --- Apply player standing changes from DM response ---
        if (resp.contains("player_standing_changes") && resp["player_standing_changes"].is_array()) {
            auto* db = PoliticalDB::GetSingleton();
            int maxDelta = GetMaxRelationChangePerTick();
            int applied = 0;
            for (const auto& change : resp["player_standing_changes"]) {
                std::string faction = change.value("faction", "");
                int changeDelta = change.value("delta", 0);
                if (faction.empty() || changeDelta == 0) continue;
                if (!GetFaction(faction).has_value()) continue;
                changeDelta = std::clamp(changeDelta, -maxDelta, maxDelta);
                db->AdjustPlayerStanding(faction, changeDelta, gameTime);
                ++applied;
            }
            out["standingsApplied"] = applied;
        }

        // --- Run standing mechanics (decay + crime) ---
        int decayed = DecayPlayerStandings(1);
        int crimeChanges = CheckCrimeGoldStandings();
        if (decayed > 0 || crimeChanges > 0) {
            WritePoliticalStateFile();
        }
        out["decayed"] = decayed;
        out["crimeChanges"] = crimeChanges;

        return out.dump();
    }

    std::string FactionPolitics::RunStandingMechanicsInternal() {
        nlohmann::json out;
        int decayed = DecayPlayerStandings(1);
        int crimeChanges = CheckCrimeGoldStandings();
        if (decayed > 0 || crimeChanges > 0) {
            WritePoliticalStateFile();
        }
        out["decayed"] = decayed;
        out["crimeChanges"] = crimeChanges;
        out["updated"] = (decayed > 0 || crimeChanges > 0);
        return out.dump();
    }

}  // namespace IntelEngine
