#include "SettingsBridge.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace IntelEngine::SettingsBridge {

    namespace {

        constexpr const char* kSettingEvent = "IntelEngine_DashboardSetting";
        constexpr const char* kToggleStoryEvent = "IntelEngine_DashboardToggleStory";

        constexpr const char* kNpcPolicy = "Allow all NPCs|Block civilians only|Allow followers only|Block all NPCs";
        constexpr const char* kHoldPolicy =
            "No restriction|Same hold (civilians)|Same hold (except followers)|Same hold (everyone)|"
            "Same town (civilians)|Same town (except followers)|Same town (everyone)";
        constexpr const char* kConfirm = "Disabled|Followers only|Everyone";

        constexpr const char* kHoldText =
            "How far this kind of story may reach for its NPC. Same hold or same town keeps visitors local; "
            "the groups in brackets are the ones the limit applies to.";
        constexpr const char* kConfirmText =
            "Ask you before an NPC starts this task: never, only for your followers, or for everyone.";
        constexpr const char* kAllowText =
            "On: your followers can be given this action like anyone else. Off: it is refused for your active followers, without a prompt.";
        constexpr const char* kHoldShort = "How far this kind of story may reach for its NPC.";

        Entry Bool(const char* key, const char* section, const char* name, bool def, const char* category,
                   const char* label, const char* description, const char* event = nullptr, const char* arg = nullptr) {
            return {key, section, name, Kind::Bool, 0, 1, nullptr, def ? 1.0 : 0.0, category, label, description, event, arg};
        }
        // A switch the game stores as "skip for followers", shown as "followers can do this" (on by default).
        Entry Allowed(const char* key, const char* section, const char* name, const char* category, const char* label) {
            Entry entry = Bool(key, section, name, true, category, label, kAllowText);
            entry.inverted = true;
            return entry;
        }
        Entry Num(Kind kind, const char* key, const char* section, const char* name, double min, double max, double def,
                  const char* category, const char* label, const char* description) {
            return {key, section, name, kind, min, max, nullptr, def, category, label, description, nullptr, nullptr};
        }
        Entry Choice(const char* key, const char* section, const char* name, const char* options, int def,
                     const char* category, const char* label, const char* description) {
            return {key, section, name, Kind::Select, 0, 0, options, static_cast<double>(def), category, label, description,
                    nullptr, nullptr};
        }

        std::vector<Entry> BuildTable() {
            const char* S = "Stories";
            const char* SH = "Stories: how far NPCs travel";
            const char* SQ = "Stories: quests";
            const char* N = "NPCs among themselves";
            const char* T = "Tasks";
            const char* TC = "Tasks: ask before starting";
            const char* TF = "Tasks: what followers can be asked to do";
            const char* M = "Meetings";
            const char* O = "Other";
            return {
                // ── Stories ──
                Bool("storyEnabled", "stories", "enabled", true, S, "Stories",
                     "The dungeon master: NPCs who know you come to you, send word, lie in wait or bring work."),
                Num(Kind::Float, "storyInterval", "stories", "check_interval_hours", 0.5, 168, 3, S, "Check interval (game hours)",
                    "How often the dungeon master considers starting something."),
                Num(Kind::Float, "longAbsenceDays", "stories", "long_absence_days", 1, 14, 3, S, "Long absence (days)",
                    "After this many days without meeting you, an NPC counts as not having seen you for a long time."),
                Num(Kind::Float, "maxTravelDays", "stories", "max_travel_days", 0.25, 3, 1, S, "Longest journey (days)",
                    "A story NPC who has been on the way for this long is brought to the end of the journey."),
                Bool("allowStuckTeleport", "stories", "teleport_when_stuck", true, S, "Move a stuck traveller along",
                     "A story NPC who cannot get on is placed further along the way, out of your sight."),
                Bool("typeSeekPlayer", "stories", "type_seek_player", true, S, "Story type: someone comes to see you", "",
                     kToggleStoryEvent, "seek_player"),
                Bool("typeInformant", "stories", "type_informant", true, S, "Story type: someone brings a rumor", "",
                     kToggleStoryEvent, "informant"),
                Bool("typeRoadEncounter", "stories", "type_road_encounter", true, S, "Story type: a meeting on the road", "",
                     kToggleStoryEvent, "road_encounter"),
                Bool("typeMessage", "stories", "type_message", true, S, "Story type: a message is delivered", "",
                     kToggleStoryEvent, "message"),
                Bool("typeQuest", "stories", "type_quest", true, S, "Story type: someone has work for you", "",
                     kToggleStoryEvent, "quest"),
                Bool("typeAmbush", "stories", "type_ambush", true, S, "Story type: an ambush", "", kToggleStoryEvent, "ambush"),
                Bool("typeFactionAmbush", "stories", "type_faction_ambush", true, S, "Story type: a faction's patrol hunts you",
                     "", kToggleStoryEvent, "faction_ambush"),
                Bool("typeStalker", "stories", "type_stalker", true, S, "Story type: someone follows you unseen", "",
                     kToggleStoryEvent, "stalker"),
                Choice("dangerZonePolicy", "stories", "danger_zone_policy", kNpcPolicy, 1, S, "NPCs sent into dangerous places",
                       "Who may be sent to you while you are in a dungeon or another dangerous place."),
                Choice("playerHomePolicy", "stories", "player_home_policy", kNpcPolicy, 0, S, "NPCs sent into your home",
                       "Who may come to you while you are in one of your homes."),

                // ── Stories: reach ──
                Choice("holdPolicySeekPlayer", "stories", "reach_seek_player", kHoldPolicy, 1, SH, "Someone comes to see you",
                       kHoldText),
                Choice("holdPolicyInformant", "stories", "reach_informant", kHoldPolicy, 6, SH, "Someone brings a rumor", kHoldShort),
                Choice("holdPolicyRoadEncounter", "stories", "reach_road_encounter", kHoldPolicy, 0, SH, "A meeting on the road",
                       kHoldText),
                Choice("holdPolicyMessage", "stories", "reach_message", kHoldPolicy, 2, SH, "A message is delivered", kHoldShort),
                Choice("holdPolicyQuest", "stories", "reach_quest", kHoldPolicy, 0, SH, "Someone has work for you", kHoldShort),
                Choice("holdPolicyAmbush", "stories", "reach_ambush", kHoldPolicy, 1, SH, "An ambush", kHoldShort),
                Choice("holdPolicyStalker", "stories", "reach_stalker", kHoldPolicy, 1, SH, "Someone follows you unseen", kHoldShort),

                // ── Stories: quests ──
                Bool("questCombat", "stories", "quest_combat", true, SQ, "Quest kind: clear out enemies", ""),
                Bool("questRescue", "stories", "quest_rescue", true, SQ, "Quest kind: rescue someone", ""),
                Bool("questFindItem", "stories", "quest_find_item", true, SQ, "Quest kind: find an item", ""),
                Bool("questFactionCombat", "stories", "quest_faction_combat", true, SQ, "Quest kind: fight a faction's enemies", ""),
                Bool("questFactionRescue", "stories", "quest_faction_rescue", true, SQ, "Quest kind: rescue for a faction", ""),
                Bool("questFactionBattle", "stories", "quest_faction_battle", true, SQ, "Quest kind: join a faction's battle", ""),
                Num(Kind::Float, "questTimeoutDays", "stories", "quest_timeout_days", 1, 30, 1, SQ, "Quest expires after (days)",
                    "A quest you were offered and did not finish ends after this many days."),

                // ── NPCs among themselves ──
                Bool("npcTickEnabled", "social", "enabled", true, N, "NPCs visit and talk to each other",
                     "NPCs who know each other meet without you, and what comes of it spreads as gossip."),
                Bool("typeNpcInteraction", "social", "type_interaction", true, N, "Kind: one NPC goes to see another", "",
                     kToggleStoryEvent, "npc_interaction"),
                Bool("typeNpcGossip", "social", "type_gossip", true, N, "Kind: gossip passed on", "", kToggleStoryEvent,
                     "npc_gossip"),
                Num(Kind::Float, "npcTickInterval", "social", "interval_hours", 0.5, 168, 1.5, N, "Check interval (game hours)", ""),
                Num(Kind::Float, "npcSocialCooldown", "social", "cooldown_hours", 6, 72, 24, N, "Rest per NPC (game hours)", ""),

                // ── Tasks ──
                Num(Kind::Int, "maxTasks", "tasks", "max_concurrent", 1, 5, 5, T, "NPCs on a task at once",
                    "Travel, errands, meetings and stories all take one of these places."),
                Num(Kind::Float, "defaultWaitHours", "tasks", "default_wait_hours", 6, 168, 48, T, "Wait at a destination (game hours)",
                    "How long an NPC you sent somewhere waits there for you before going home."),
                Num(Kind::Float, "releaseDistance", "tasks", "release_distance", 300, 2000, 800, T, "Visit ends when you walk away (units)",
                    "An NPC who stayed with you after a task goes back to their own day once you are this far away."),
                Bool("reportBack", "tasks", "report_back_after_delivery", true, T, "Report back after a delivery",
                     "An NPC who delivered a message for you comes back to tell you how it went."),

                Choice("confirmGoToLocation", "tasks", "confirm_go_to_location", kConfirm, 1, TC, "Go somewhere", kConfirmText),
                Choice("confirmDeliverMessage", "tasks", "confirm_deliver_message", kConfirm, 1, TC, "Deliver a message", kConfirmText),
                Choice("confirmFetchPerson", "tasks", "confirm_fetch_person", kConfirm, 1, TC, "Fetch someone", kConfirmText),
                Choice("confirmEscortTarget", "tasks", "confirm_escort_target", kConfirm, 1, TC, "Escort someone", kConfirmText),
                Choice("confirmSearchForActor", "tasks", "confirm_search_for_actor", kConfirm, 1, TC, "Search for someone", kConfirmText),
                Choice("confirmScheduleMeeting", "tasks", "confirm_schedule_meeting", kConfirm, 2, TC, "Arrange a meeting", kConfirmText),
                Choice("confirmScheduleFetch", "tasks", "confirm_schedule_fetch", kConfirm, 1, TC, "Fetch someone later", kConfirmText),
                Choice("confirmScheduleDelivery", "tasks", "confirm_schedule_delivery", kConfirm, 1, TC, "Deliver a message later",
                       kConfirmText),

                Allowed("skipFollowerGoToLocation", "tasks", "followers_go_to_location", TF, "Go somewhere"),
                Allowed("skipFollowerDeliverMessage", "tasks", "followers_deliver_message", TF, "Deliver a message"),
                Allowed("skipFollowerFetchPerson", "tasks", "followers_fetch_person", TF, "Fetch someone"),
                Allowed("skipFollowerEscortTarget", "tasks", "followers_escort_target", TF, "Escort someone"),
                Allowed("skipFollowerSearchForActor", "tasks", "followers_search_for_actor", TF, "Search for someone"),
                Allowed("skipFollowerScheduleMeeting", "tasks", "followers_schedule_meeting", TF, "Arrange a meeting"),
                Allowed("skipFollowerScheduleFetch", "tasks", "followers_schedule_fetch", TF, "Fetch someone later"),
                Allowed("skipFollowerScheduleDelivery", "tasks", "followers_schedule_delivery", TF, "Deliver a message later"),

                // ── Meetings ──
                Num(Kind::Float, "meetingTimeoutHours", "meetings", "timeout_hours", 1, 12, 5, M, "Wait for you at a meeting (game hours)",
                    "How long an NPC waits at the agreed place before giving up."),
                Num(Kind::Float, "meetingGracePeriod", "meetings", "grace_period_hours", 0, 2, 0.5, M, "On time until (game hours late)",
                    "Arriving within this time after the agreed hour still counts as on time."),

                // ── Other ──
                Bool("autoBioEnabled", "other", "auto_bio_enabled", false, O, "Refresh an NPC's profile after much talk",
                     "Asks SkyrimNet to update an NPC's dynamic profile once you have exchanged enough lines with them."),
                Num(Kind::Int, "autoBioThreshold", "other", "auto_bio_threshold", 5, 1000, 20, O, "Lines before a profile refresh", ""),
                Bool("debugMode", "other", "debug_messages", false, O, "Debug messages",
                     "Shows what IntelEngine does as notices on screen. The Papyrus log has it either way; for more in the DLL's own log set DebugLevel=4 in IntelEngine.ini."),
            };
        }

        std::string Lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        std::string Trim(const std::string& text) {
            const auto first = text.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return {};
            return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
        }

        /** A number as YAML and JSON write it: no trailing zeros, at most three decimals. */
        std::string Number(double value) {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.3f", value);
            std::string text(buffer);
            text.erase(text.find_last_not_of('0') + 1);
            if (!text.empty() && text.back() == '.') text.pop_back();
            return text;
        }

        /** The game's value of a setting from the config object; false when it is not there or not a value. */
        bool GameValue(const nlohmann::json& config, const Entry& entry, double& value) {
            auto it = config.find(entry.key);
            if (it == config.end()) it = config.find(Lower(entry.key));     // as ParseConfig stores the keys
            if (it == config.end()) return false;
            if (it->is_boolean()) {
                value = it->get<bool>() ? 1.0 : 0.0;
                if (entry.inverted) value = 1.0 - value;        // from here on: the file's side
                return true;
            }
            if (it->is_number()) {
                value = it->get<double>();
                return true;
            }
            return false;
        }

        bool Same(const Entry& entry, double a, double b) {
            if (entry.kind == Kind::Float) return std::fabs(a - b) < 0.0005;
            return std::llround(a) == std::llround(b);
        }

    }  // namespace

    const std::vector<Entry>& Table() {
        static const std::vector<Entry> table = BuildTable();
        return table;
    }

    std::vector<std::string> OptionsOf(const Entry& entry) {
        std::vector<std::string> options;
        if (!entry.options) return options;
        std::string all(entry.options);
        size_t start = 0;
        while (start <= all.size()) {
            const size_t bar = all.find('|', start);
            options.push_back(all.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
        return options;
    }

    std::string ToYaml(const Entry& entry, double value) {
        switch (entry.kind) {
            case Kind::Bool:
                return value > 0.5 ? "true" : "false";
            case Kind::Int:
                return std::to_string(std::llround(value));
            case Kind::Float:
                return Number(value);
            case Kind::Select: {
                const auto options = OptionsOf(entry);
                const auto index = static_cast<size_t>(std::clamp<long long>(std::llround(value), 0, static_cast<long long>(options.size()) - 1));
                return "\"" + options[index] + "\"";
            }
        }
        return {};
    }

    bool FromYaml(const Entry& entry, const std::string& raw, double& value) {
        const std::string text = Trim(raw);
        if (text.empty()) return false;
        if (entry.kind == Kind::Bool) {
            const std::string low = Lower(text);
            if (low == "true" || low == "1" || low == "yes" || low == "on") {
                value = 1.0;
                return true;
            }
            if (low == "false" || low == "0" || low == "no" || low == "off") {
                value = 0.0;
                return true;
            }
            return false;
        }
        if (entry.kind == Kind::Select) {
            const auto options = OptionsOf(entry);
            const std::string low = Lower(text);
            for (size_t i = 0; i < options.size(); ++i) {
                if (Lower(options[i]) == low) {
                    value = static_cast<double>(i);
                    return true;
                }
            }
            // A number is accepted too (a file written by hand): it is the choice's position.
            char* end = nullptr;
            const long index = std::strtol(text.c_str(), &end, 10);
            if (end != text.c_str() && *end == '\0' && index >= 0 && index < static_cast<long>(options.size())) {
                value = static_cast<double>(index);
                return true;
            }
            return false;
        }
        char* end = nullptr;
        const double number = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0' || !std::isfinite(number)) return false;
        value = std::clamp(number, entry.min, entry.max);
        if (entry.kind == Kind::Int) value = static_cast<double>(std::llround(value));
        return true;
    }

    nlohmann::json ParseConfig(const std::string& scriptJson) {
        // Outside quoted text the words true, false and null are put in lower case, whatever case they came in.
        std::string fixed;
        fixed.reserve(scriptJson.size());
        bool inText = false;
        bool escaped = false;
        for (size_t i = 0; i < scriptJson.size();) {
            const char c = scriptJson[i];
            if (inText) {
                fixed += c;
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    inText = false;
                }
                ++i;
                continue;
            }
            if (c == '"') {
                inText = true;
                fixed += c;
                ++i;
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c))) {
                size_t end = i;
                while (end < scriptJson.size() && std::isalpha(static_cast<unsigned char>(scriptJson[end]))) ++end;
                const std::string word = scriptJson.substr(i, end - i);
                const std::string low = Lower(word);
                fixed += (low == "true" || low == "false" || low == "null") ? low : word;
                i = end;
                continue;
            }
            fixed += c;
            ++i;
        }
        const auto parsed = nlohmann::json::parse(fixed, nullptr, false);
        if (!parsed.is_object()) return nlohmann::json();
        nlohmann::json config = nlohmann::json::object();
        for (auto it = parsed.begin(); it != parsed.end(); ++it) config[Lower(it.key())] = it.value();
        return config;
    }

    int SyncToFile(const nlohmann::json& config, const Store& store) {
        if (!config.is_object() || !store.read || !store.write) return 0;
        int written = 0;
        for (const Entry& entry : Table()) {
            double game = 0.0;
            if (!GameValue(config, entry, game)) continue;
            std::string text;
            double file = 0.0;
            if (store.read(entry.section, entry.name, text) && FromYaml(entry, text, file) && Same(entry, game, file)) continue;
            if (store.write(entry.section, entry.name, ToYaml(entry, game))) ++written;
        }
        return written;
    }

    std::vector<Change> ApplyFromFile(const nlohmann::json& config, const Store& store) {
        std::vector<Change> changes;
        if (!config.is_object() || !store.read) return changes;
        for (const Entry& entry : Table()) {
            double game = 0.0;
            const bool known = GameValue(config, entry, game);
            std::string text;
            if (!store.read(entry.section, entry.name, text)) {
                // Not in the file yet: it gets the game's value, so the page shows what is in force.
                if (known && store.write) store.write(entry.section, entry.name, ToYaml(entry, game));
                continue;
            }
            double file = 0.0;
            if (!FromYaml(entry, text, file)) continue;         // unreadable: the game keeps its value
            if (known && Same(entry, game, file)) continue;
            Change change;
            change.event = entry.event ? entry.event : kSettingEvent;
            change.arg = entry.event ? entry.eventArg : entry.key;
            change.value = static_cast<float>(entry.inverted ? 1.0 - file : file);    // back to the game's side
            changes.push_back(std::move(change));
        }
        return changes;
    }

    std::string ManifestFields() {
        std::string out;
        const char* category = "";
        for (const Entry& entry : Table()) {
            if (std::string(category) != entry.category) {
                category = entry.category;
                out += std::string("    # ── ") + category + " ──\n";
            }
            out += std::string("    - name: \"") + entry.label + "\"\n";
            out += std::string("      path: \"") + entry.section + "." + entry.name + "\"\n";
            switch (entry.kind) {
                case Kind::Bool:
                    out += "      type: \"bool\"\n";
                    break;
                case Kind::Int:
                    out += "      type: \"int\"\n";
                    break;
                case Kind::Float:
                    out += "      type: \"float\"\n";
                    break;
                case Kind::Select:
                    out += "      type: \"select\"\n";
                    break;
            }
            if (entry.description && *entry.description) out += std::string("      description: \"") + entry.description + "\"\n";
            if (entry.kind == Kind::Select) {
                out += "      options:";
                for (const auto& option : OptionsOf(entry)) out += "\n        - \"" + option + "\"";
                out += "\n";
            }
            out += "      defaultValue: " + ToYaml(entry, entry.def) + "\n";
            if (entry.kind == Kind::Int || entry.kind == Kind::Float) {
                out += "      min: " + Number(entry.min) + "\n";
                out += "      max: " + Number(entry.max) + "\n";
            }
            out += std::string("      category: \"") + entry.category + "\"\n\n";
        }
        return out;
    }

}  // namespace IntelEngine::SettingsBridge
