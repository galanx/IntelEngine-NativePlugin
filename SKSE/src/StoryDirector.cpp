#include "StoryDirector.h"

#include "AsyncDispatch.h"
#include "LivingWorldBridge.h"
#include "MemoryDB.h"
#include "SkyrimNetAPI.h"
#include "StoryDirectorPick.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

namespace IntelEngine::StoryDirector {

    namespace {
        // Every story type the dungeon master knows (BuildStoryDMRequestJsonCore's list).
        const char* const kAllTypes[] = {"seek_player", "informant", "road_encounter", "ambush", "stalker", "message", "quest",
                                         "quest_combat", "quest_rescue", "quest_find_item", "faction_ambush",
                                         "quest_faction_combat", "quest_faction_rescue", "quest_faction_battle"};

        // What each kind of moment means, for the decision's options ({player} = the player's name).
        const char* KindMeaning(std::string_view kind) {
            if (kind == "seek_player") return "they set out to find {player} themselves and talk in person";
            if (kind == "informant") return "they come to tell {player} something they learned that matters to {player}";
            if (kind == "message") return "they send {player} word by someone else, or a letter";
            if (kind == "quest") return "they need {player}'s help with real trouble: a threat, a captive, something lost";
            if (kind == "road_encounter") return "{player} meets them on the road while they are about their own business";
            if (kind == "ambush") return "they lie in wait on the road to attack {player}";
            if (kind == "stalker") return "they secretly follow {player}";
            return nullptr;
        }

        constexpr auto kWaitForAnswers = std::chrono::seconds(25);
        std::atomic<bool> g_noDecisionRoute{false};   // the player has no decision provider: ask the chat model

        struct Session {
            NPCIndex::StoryDMTickSnapshot snap;
            std::unordered_set<std::string> excluded;
            BuildRequest build;
            std::function<void(std::string)> done;
            std::vector<NPCIndex::RankedCandidate> pool;
            std::vector<Verdict> verdicts;
            std::vector<std::string> contexts;       // the decision context JSON of each candidate
            std::mutex mutex;
            int left = 0;
            bool finished = false;
        };

        std::string Replace(std::string text, std::string_view key, const std::string& value) {
            for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size())) {
                text.replace(at, key.size(), value);
            }
            return text;
        }

        // The kinds this candidate may be part of now: their Eligible line without the excluded types.
        std::vector<std::string> KindsFor(const NPCIndex::StoryDMActorSnapshot& a, const std::unordered_set<std::string>& excluded) {
            std::vector<std::string> kinds;
            std::size_t at = 0;
            const auto& line = a.eligibleTypes;
            while (at <= line.size()) {
                auto end = line.find(',', at);
                if (end == std::string::npos) end = line.size();
                std::string item = line.substr(at, end - at);
                while (!item.empty() && item.front() == ' ') item.erase(item.begin());
                while (!item.empty() && item.back() == ' ') item.pop_back();
                if (!item.empty() && !excluded.count(item) && KindMeaning(item)) kinds.push_back(item);
                at = end + 1;
            }
            return kinds;
        }

        void Finish(const std::shared_ptr<Session>& session);

        void Record(const std::shared_ptr<Session>& session, std::size_t index, bool answered, const std::string& level,
                    const std::vector<std::pair<std::string, float>>& probabilities, const std::string& kind) {
            bool last = false;
            {
                std::scoped_lock lock(session->mutex);
                if (session->finished || index >= session->verdicts.size()) return;
                auto& v = session->verdicts[index];
                v.answered = answered;
                v.level = std::max(0, LevelOf(level));
                v.expected = ExpectedLevel(probabilities, level);
                v.kind = kind;
                last = --session->left == 0;
            }
            if (last) {
                AsyncDispatch::Submit([session] { Finish(session); });   // off SkyrimNet's thread pool
            }
        }

        // The chat model's answer: {"reason":"real","kind":"seek_player"}, maybe inside other text.
        void AskByChat(const std::shared_ptr<Session>& session, std::size_t index) {
            if (!SkyrimNetAPI::SendCustomPromptToLLM) {
                Record(session, index, false, "none", {}, "");
                return;
            }
            const bool queued = SkyrimNetAPI::SendCustomPromptToLLM(
                "intel_story_reason_chat", "intel_story_dm", session->contexts[index].c_str(),
                [session, index](const char* response, int success) {
                    std::string level = "none";
                    std::string kind;
                    bool answered = false;
                    if (success && response) {
                        const std::string text = response;
                        const auto open = text.find('{');
                        const auto close = text.rfind('}');
                        if (open != std::string::npos && close != std::string::npos && close > open) {
                            const auto json = nlohmann::json::parse(text.substr(open, close - open + 1), nullptr, false);
                            if (json.is_object()) {
                                level = json.value("reason", "none");
                                kind = json.value("kind", "");
                                answered = LevelOf(level) >= 0;
                            }
                        }
                    }
                    Record(session, index, answered, level, {}, kind);
                });
            if (!queued) Record(session, index, false, "none", {}, "");
        }

        void Ask(const std::shared_ptr<Session>& session, std::size_t index) {
            if (g_noDecisionRoute.load() || !SkyrimNetAPI::SendCustomDecisionToLLM) {
                AskByChat(session, index);
                return;
            }
            const bool queued = SkyrimNetAPI::SendCustomDecisionToLLM(
                "intel_story_reason", session->contexts[index].c_str(), 0,
                [session, index](const char*, const char* resultJson, int success) {
                    const auto json = nlohmann::json::parse(resultJson ? resultJson : "", nullptr, false);
                    if (!success) {
                        const std::string error = json.is_object() ? json.value("error", "") : "";
                        if (error == "no_decisions_route") {
                            // No decision provider (the player is not on OpenRouter): the chat model answers instead.
                            g_noDecisionRoute.store(true);
                            AskByChat(session, index);
                            return;
                        }
                        logger::info("[Director] no decision for candidate {} ({})", index, error);
                        Record(session, index, false, "none", {}, "");
                        return;
                    }
                    std::string level = "none";
                    std::string kind;
                    std::vector<std::pair<std::string, float>> probabilities;
                    if (json.is_object() && json.contains("answers") && json["answers"].is_object()) {
                        const auto& answers = json["answers"];
                        if (answers.contains("reason") && answers["reason"].is_object()) {
                            const auto& reason = answers["reason"];
                            level = reason.value("choice", "none");
                            if (reason.contains("probabilities") && reason["probabilities"].is_object()) {
                                for (const auto& [key, p] : reason["probabilities"].items()) {
                                    if (p.is_number()) probabilities.emplace_back(key, p.get<float>());
                                }
                            }
                        }
                        if (answers.contains("kind") && answers["kind"].is_object()) kind = answers["kind"].value("choice", "");
                    }
                    Record(session, index, LevelOf(level) >= 0, level, probabilities, kind);
                });
            if (!queued) AskByChat(session, index);
        }

        void Finish(const std::shared_ptr<Session>& session) {
            std::vector<Verdict> verdicts;
            {
                std::scoped_lock lock(session->mutex);
                if (session->finished) return;
                session->finished = true;
                verdicts = session->verdicts;
            }
            for (const auto& v : verdicts) {
                logger::info("[Director] {}: {} (level {}, expected {:.2f}), kind '{}', rank {:.1f}, may be: {}", v.name,
                             v.answered ? std::string(kLevels[std::clamp(v.level, 0, 5)]) : std::string("no answer"), v.level,
                             v.expected, v.kind, v.rank, v.allowed);
            }
            const bool anyAnswer = std::any_of(verdicts.begin(), verdicts.end(), [](const Verdict& v) { return v.answered; });
            if (!anyAnswer) {
                // Not one answer (the template failed, the model is down, every call timed out): the single
                // dungeon-master prompt of 3.8 with every candidate, so the world never goes silent because of this.
                logger::warn("[Director] no candidate was judged; the dungeon master chooses from all of them as before");
                const auto all = NPCIndex::GetSingleton()->RenderDungeonMasterContext(session->snap, session->pool, session->excluded);
                session->done(all.empty() ? std::string() : session->build(all, session->excluded, "", ""));
                return;
            }
            const int index = Pick(verdicts, [](const std::string& kind) { return NPCIndex::GetSingleton()->StoryTypePickCount(kind); });
            if (index < 0) {
                logger::info("[Director] nobody has a real reason to act now: no moment");
                session->done("");
                return;
            }
            const auto& chosen = verdicts[index];
            // The dungeon master sees that one person and that kind of moment only (a quest keeps its sub-types).
            std::unordered_set<std::string> writerExcluded = session->excluded;
            for (const char* type : kAllTypes) {
                const std::string_view t = type;
                const bool same = t == chosen.kind || (t.starts_with(chosen.kind + "_")) ||
                                  (chosen.kind == "ambush" && t == "faction_ambush");
                if (!same) writerExcluded.insert(type);
            }
            const auto md = NPCIndex::GetSingleton()->RenderDungeonMasterContext(session->snap, {session->pool[index]}, writerExcluded);
            logger::info("[Director] {} acts: a {} moment ({} reason)", chosen.name, chosen.kind, kLevels[std::clamp(chosen.level, 0, 5)]);
            session->done(md.empty() ? std::string() : session->build(md, writerExcluded, chosen.name, chosen.kind));
        }
    }

    bool ShouldRun() {
        return !LivingWorldBridge::GetSingleton()->IsClassic() &&
               (SkyrimNetAPI::SendCustomDecisionToLLM != nullptr || SkyrimNetAPI::SendCustomPromptToLLM != nullptr);
    }

    void Run(NPCIndex::StoryDMTickSnapshot snap, std::unordered_set<std::string> excluded, BuildRequest build,
             std::function<void(std::string)> done) {
        auto session = std::make_shared<Session>();
        session->snap = std::move(snap);
        session->excluded = std::move(excluded);
        session->build = std::move(build);
        session->done = std::move(done);
        session->pool = NPCIndex::GetSingleton()->RankStoryDMCandidates(session->snap);   // points into session->snap
        if (session->pool.empty()) {
            session->done("");
            return;
        }
        const std::string playerName = session->snap.playerName.empty() ? "the traveller" : session->snap.playerName;
        std::vector<std::size_t> toAsk;
        for (const auto& candidate : session->pool) {
            Verdict v;
            v.formId = candidate.a->formId;
            v.name = candidate.a->nameDisplay;
            v.rank = candidate.score;
            v.allowed = candidate.a->eligibleTypes;
            const auto kinds = KindsFor(*candidate.a, session->excluded);
            nlohmann::json context;
            context["name"] = v.name;
            context["player_name"] = playerName;
            auto list = nlohmann::json::array();
            for (const auto& kind : kinds) {
                list.push_back({{"key", kind}, {"description", Replace(KindMeaning(kind), "{player}", playerName)}});
            }
            context["kinds"] = list;
            std::string json = context.dump();
            // Their block, already JSON-escaped by the renderer, goes in as the first field.
            const auto md = kinds.empty() ? std::string()
                                          : NPCIndex::GetSingleton()->RenderDungeonMasterContext(session->snap, {candidate}, session->excluded);
            json.insert(1, "\"candidate\":\"" + md + "\",");
            if (!kinds.empty() && !md.empty()) toAsk.push_back(session->verdicts.size());
            session->verdicts.push_back(std::move(v));
            session->contexts.push_back(std::move(json));
        }
        if (toAsk.empty()) {
            logger::info("[Director] no candidate may be part of any allowed moment now");
            session->done("");
            return;
        }
        {
            std::scoped_lock lock(session->mutex);
            session->left = static_cast<int>(toAsk.size());
        }
        logger::info("[Director] asking about {} candidates, each on their own ({})", toAsk.size(),
                     g_noDecisionRoute.load() || !SkyrimNetAPI::SendCustomDecisionToLLM ? "chat model" : "decision model");
        for (const auto index : toAsk) Ask(session, index);
        // Whatever has come back by then decides; a candidate who never answered has no reason.
        std::thread([session] {
            std::this_thread::sleep_for(kWaitForAnswers);
            AsyncDispatch::Submit([session] { Finish(session); });
        }).detach();
    }
}
