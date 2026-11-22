#include "StoryDirector.h"
#include "Safe.h"

#include "AsyncDispatch.h"
#include "LivingWorldBridge.h"
#include "MemoryDB.h"
#include "QuestBridge.h"
#include "ArcBridge.h"
#include "SkyrimNetAPI.h"
#include "StoryDirectorPick.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace IntelEngine::StoryDirector {
    namespace {
        constexpr std::uint32_t kTaleWriting = 0xFFFFFFFFu;   // a tale was asked for: its offer comes when it is written
    }

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

        // Someone the director chose whose moment never went out (the writer turned it down, a check at dispatch
        // refused it) rests for half a game day. Without it the same person was chosen again on every tick: Uthgerd
        // five times in ten seconds at a fast clock, each time a whole round of decision calls (2026-11-17).
        constexpr float kRestDays = 0.5f;
        std::mutex g_restMutex;
        std::unordered_map<RE::FormID, float> g_restUntil;
        RE::FormID g_lastChosen = 0;
        float g_lastChosenDay = 0.0f;

        float GameDays() {
            auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? calendar->GetCurrentGameTime() : 0.0f;
        }

        // Called at the start of a round: was the last choice ever dispatched? If not, it rests.
        void NoteLastChoice() {
            std::scoped_lock lock(g_restMutex);
            if (!g_lastChosen) return;
            const float now = GameDays();
            const float since = LivingWorldBridge::GetSingleton()->DaysSincePicked(g_lastChosen);
            if (since > now - g_lastChosenDay + 0.001f) {
                g_restUntil[g_lastChosen] = now + kRestDays;
                logger::info("[Director] {:08X} was chosen but no moment went out: rests for {:.1f} days", g_lastChosen, kRestDays);
            }
            g_lastChosen = 0;
        }

        bool Resting(RE::FormID actor) {
            std::scoped_lock lock(g_restMutex);
            auto it = g_restUntil.find(actor);
            if (it == g_restUntil.end()) return false;
            if (GameDays() >= it->second) {
                g_restUntil.erase(it);
                return false;
            }
            return true;
        }

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
                                level = Safe::Text(json, "reason", "none");
                                kind = Safe::Text(json, "kind");
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
                        const std::string error = Safe::Text(json, "error");
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
                            level = Safe::Text(reason, "choice", "none");
                            if (reason.contains("probabilities") && reason["probabilities"].is_object()) {
                                for (const auto& [key, p] : reason["probabilities"].items()) {
                                    if (p.is_number()) probabilities.emplace_back(key, p.get<float>());
                                }
                            }
                        }
                        if (answers.contains("kind") && answers["kind"].is_object()) kind = Safe::Text(answers["kind"], "choice");
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
                logger::info("[Director] {}: {} (level {}, expected {:.2f}), kind '{}', rank {:.1f}, {}{}, may be: {}", v.name,
                             v.answered ? std::string(kLevels[std::clamp(v.level, 0, 5)]) : std::string("no answer"), v.level,
                             v.expected, v.kind, v.rank, v.reach <= 0 ? "here" : v.reach == 1 ? "in the hold" : "in another hold",
                             v.bonded ? ", close to the player" : "", v.allowed);
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
            const float quietDays = LivingWorldBridge::GetSingleton()->QuietDays();
            const bool quiet = quietDays >= kQuietDays;
            const int index = Pick(
                verdicts, [](const std::string& kind) { return NPCIndex::GetSingleton()->StoryTypePickCount(kind); }, quiet);
            if (index < 0) {
                // Said plainly when distance was what held someone back.
                for (const auto& v : verdicts) {
                    if (!v.answered || !Allowed(v) || v.level < kRealLevel || MayCome(v)) continue;
                    logger::info("[Director] {} has a {} reason but is {}{}: not worth that road; nothing comes of it now", v.name,
                                 kLevels[std::clamp(v.level, 0, 5)], v.reach == 1 ? "elsewhere in the hold" : "in another hold",
                                 v.reach >= 2 && !v.farRested ? " (and someone came from afar lately)" : v.bonded ? "" : " and no one close to the player");
                }
                logger::info("[Director] nobody has a reason enough to act now: no moment ({:.1f} days quiet)", quietDays);
                session->done("");
                return;
            }
            const auto& chosen = verdicts[index];
            if (chosen.level < kRealLevel) {
                logger::info("[Director] {:.1f} quiet days: a gentle moment on a mild reason", quietDays);
            }
            {
                std::scoped_lock lock(g_restMutex);
                g_lastChosen = chosen.formId;
                g_lastChosenDay = GameDays();
            }
            // A person with a real reason of their own to come, ask or send word: their tale, written around that very
            // reason (their memories, unfinished business, their life, as the analyser read them), and they come to the
            // player wherever the player is, at home too. The user, 2026-11-19: no one-off visit decided by one big
            // prompt; everything a story. Hostile moments keep their own path until tales have them.
            if (chosen.kind == "quest" || chosen.kind == "seek_player" || chosen.kind == "message" || chosen.kind == "informant") {
                std::string why;
                {
                    const auto ctx = nlohmann::json::parse(session->contexts[index], nullptr, false);
                    why = Safe::Text(ctx, "candidate");
                }
                auto asked = std::make_shared<std::promise<bool>>();
                auto future = asked->get_future();
                const RE::FormID giverId = chosen.formId;
                const std::string kind = chosen.kind;
                const bool otherHold = chosen.reach >= 2;
                // Word from someone who is not here is a letter, as it was before 4.0: nobody crosses the hold to say
                // what a courier can carry, and nobody stays silent because the road is long. (Before, such a moment
                // became a tale whose asker travels in person, or nothing at all.)
                const bool letter = chosen.kind == "message" && chosen.reach >= 1;
                if (letter) logger::info("[Director] {} is not here and sends word: a letter by courier", chosen.name);
                if (auto* task = letter ? nullptr : SKSE::GetTaskInterface()) {
                    task->AddTask([asked, giverId, why, kind, otherHold]() {
                        auto* giver = RE::TESForm::LookupByID<RE::Actor>(giverId);
                        ArcBridge::TaleSeed seed{why, kind, true, otherHold};
                        asked->set_value(giver && ArcBridge::GetSingleton()->RequestTale(giver, {}, &seed));
                    });
                    if (future.wait_for(std::chrono::seconds(5)) == std::future_status::ready && future.get()) {
                        logger::info("[Director] {} acts: a tale grows from their {} reason ({}); its offer follows", chosen.name,
                                     kLevels[std::clamp(chosen.level, 0, 5)], chosen.kind);
                        session->done("");
                        return;
                    }
                }
                // Someone who is not here comes with a story of their own or not at all: never the one-off visit of the
                // old dungeon master (the user: "people were visiting for the smallest most absurd reasons across the
                // world").
                if (chosen.reach >= 1 && !letter) {
                    logger::info("[Director] no tale for {} now, and they are not here: nothing comes of it", chosen.name);
                    session->done("");
                    return;
                }
                if (!letter) logger::info("[Director] no tale for {} now: the moment goes the old way", chosen.name);
            }
            // 4.0 quests (docs/QUESTS.md): a quest moment is planned by the engine first, every step a fact of the game;
            // the 3.8 writer path runs only when no quest fits this person now (rescues, faction quests).
            const bool venture = chosen.kind == "road_encounter" && LivingWorldBridge::GetSingleton()->HasOwnLife(chosen.formId);
            bool noEngineQuest = false;
            if (chosen.kind == "quest" || venture) {
                auto planned = std::make_shared<std::promise<std::uint32_t>>();
                auto future = planned->get_future();
                const RE::FormID giverId = chosen.formId;
                auto arc = std::make_shared<std::atomic<bool>>(false);
                if (auto* task = SKSE::GetTaskInterface()) {
                    task->AddTask([planned, giverId, venture, arc]() {
                        auto* giver = RE::TESForm::LookupByID<RE::Actor>(giverId);
                        // Story arcs first (docs/STORY_ARCS.md): a thief, a feud or a murder among the people of the
                        // town the player is in; else a quest as before.
                        if (!venture) {
                            // A tale the dungeon master writes for this person's town comes first; its answer brings
                            // the offer by itself (kTaleWriting tells the wait here that one is on its way).
                            if (ArcBridge::GetSingleton()->RequestTale(giver)) {
                                arc->store(true);
                                planned->set_value(kTaleWriting);
                                return;
                            }
                            if (const auto id = ArcBridge::GetSingleton()->PlanFor(giver, 0, true); id != 0) {
                                arc->store(true);
                                planned->set_value(id);
                                return;
                            }
                        }
                        planned->set_value(venture ? QuestBridge::GetSingleton()->PlanVenture(giver)
                                                   : QuestBridge::GetSingleton()->PlanFor(giver));
                    });
                    if (future.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
                        if (const auto id = future.get(); id == kTaleWriting) {
                            logger::info("[Director] {} acts: a tale of their town is being written; its offer follows", chosen.name);
                            session->done("");
                            return;
                        } else if (id != 0) {
                            const bool isArc = arc->load();
                            logger::info("[Director] {} acts: {} #{} planned by the engine; the offer goes out", chosen.name,
                                         isArc ? "story arc" : "quest", id);
                            task->AddTask([id, giverId, isArc]() {
                                // A story of the town goes out by the door every offer goes out of: who comes to the
                                // player and who waits is decided there (a stranger, somebody with a post to keep,
                                // anybody once the player is beyond the town's houses).
                                if (isArc) {
                                    if (auto* giver = RE::TESForm::LookupByID<RE::Actor>(giverId)) ArcBridge::GetSingleton()->Offer(id, giver);
                                    return;
                                }
                                auto* source = SKSE::GetModCallbackEventSource();
                                if (!source) return;
                                SKSE::ModCallbackEvent event{};
                                event.eventName = isArc ? "IntelEngine_ArcOffer" : "IntelEngine_QuestOffer";
                                event.strArg = "";
                                event.numArg = static_cast<float>(id);
                                event.sender = RE::TESForm::LookupByID(giverId);
                                source->SendEvent(&event);
                            });
                            session->done("");
                            return;
                        }
                    }
                }
                logger::info("[Director] no quest of the engine fits {} now: the dungeon master may ask for a rescue or a faction's quest", chosen.name);
                noEngineQuest = chosen.kind == "quest";
            }
            // The dungeon master sees that one person and that kind of moment only (a quest keeps its sub-types).
            std::unordered_set<std::string> writerExcluded = session->excluded;
            for (const char* type : kAllTypes) {
                const std::string_view t = type;
                const bool same = t == chosen.kind || (t.starts_with(chosen.kind + "_")) ||
                                  (chosen.kind == "ambush" && t == "faction_ambush");
                if (!same) writerExcluded.insert(type);
            }
            if (noEngineQuest) {
                // The engine's own quests (a bounty, a keepsake, a venture, a tale) stand in for the old "clear out" and
                // "find an item": where none of them fits, the old ones are not written in their place (they were the
                // quests that would not complete, had no text, or put a dragon inside a fort). What the dungeon master
                // may still ask for: a rescue, which becomes a story of the town with someone held at a place
                // (StoryEngine.HandleQuestDispatch -> ArcRequestRescue), and the factions' own quests.
                writerExcluded.insert("quest_combat");
                writerExcluded.insert("quest_find_item");
                bool left = false;
                for (const char* type : {"quest_rescue", "quest_faction_combat", "quest_faction_rescue", "quest_faction_battle"}) {
                    left = left || !writerExcluded.contains(type);
                }
                if (!left) {
                    logger::info("[Director] nothing left to ask for {}: no quest comes of it", chosen.name);
                    session->done("");
                    return;
                }
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
        NoteLastChoice();
        session->pool = NPCIndex::GetSingleton()->RankStoryDMCandidates(session->snap);   // points into session->snap
        // Those who rest after a moment that never went out are not asked again yet (no decision call is spent).
        std::erase_if(session->pool, [](const NPCIndex::RankedCandidate& c) { return c.a && Resting(c.a->formId); });
        if (session->pool.empty()) {
            logger::info("[Director] nobody to consider yet: no candidates");
            session->done("");
            return;
        }
        const std::string playerName = session->snap.playerName.empty() ? "the traveller" : session->snap.playerName;
        const bool farRested = !ArcBridge::GetSingleton()->FarComerLately(kFarRestDays);
        std::vector<std::size_t> toAsk;
        for (const auto& candidate : session->pool) {
            Verdict v;
            v.formId = candidate.a->formId;
            v.name = candidate.a->nameDisplay;
            v.rank = candidate.score;
            v.allowed = candidate.a->eligibleTypes;
            v.sincePicked = LivingWorldBridge::GetSingleton()->DaysSincePicked(v.formId);
            v.reach = candidate.a->reach;
            v.bonded = candidate.a->bonded;
            v.known = candidate.a->fromMemoryDB || candidate.a->bonded;
            v.farRested = farRested;
            v.barred = LivingWorldBridge::PlaceBars() != Patrols::Barred::No;
            const auto kinds = KindsFor(*candidate.a, session->excluded);
            nlohmann::json context;
            context["name"] = v.name;
            context["player_name"] = playerName;
            // How far they are: the reason has to be worth the road.
            context["distance"] = v.reach <= 0 ? "is where " + playerName + " is now"
                                  : v.reach == 1 ? "is elsewhere in the same hold as " + playerName + ", some hours away on the road"
                                                 : "is in another hold, days of travel away from " + playerName;
            auto list = nlohmann::json::array();
            for (const auto& kind : kinds) {
                list.push_back({{"key", kind}, {"description", Replace(KindMeaning(kind), "{player}", playerName)}});
            }
            context["kinds"] = list;
            std::string json = context.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
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
