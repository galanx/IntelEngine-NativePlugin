// Tales (docs/STORY_ARCS.md section 0): the dungeon master writes a story for the real people of the player's town;
// Tales.h checks it and builds the arc; the arc engine runs it. Here: who may be cast and what is said of them, the
// request and its answer, and the judge who reads a talk and names which prepared outcome it turned into.

#include "ArcBridge.h"

#include "AsyncDispatch.h"
#include "DialogueTracker.h"
#include "ItemIndex.h"
#include "ItemNames.h"
#include "MemoryDB.h"
#include "QuestBridge.h"
#include "SkyrimNetAPI.h"
#include "SpeechTracker.h"
#include "Tales.h"

#include <chrono>
#include <map>
#include <thread>

#include <nlohmann/json.hpp>

namespace IntelEngine {

    namespace {
        namespace A = Arcs;

        constexpr float kQuietSeconds = 6.0f;      // a talk is read once both have been quiet this long
        constexpr float kTalkWindow = 300.0f;      // the conversation read: its lines of the last five minutes
        constexpr std::size_t kTalkLines = 16;
        constexpr std::size_t kPoolSize = 10;      // the asker and up to nine others
        constexpr float kWriteTimeout = 150.0f;    // real seconds a tale may take to be written

        std::string NameOf(RE::FormID id) {
            auto* form = id ? RE::TESForm::LookupByID(id) : nullptr;
            if (!form) return {};
            if (auto* ref = form->As<RE::TESObjectREFR>()) {
                const char* name = ref->GetDisplayFullName();
                return name ? name : "";
            }
            const char* name = form->GetName();
            return name ? name : "";
        }

        std::string PlayerName() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const char* name = player ? player->GetDisplayFullName() : nullptr;
            return name && *name ? name : "the traveller";
        }

        RE::BGSLocation* TownOfLoc(RE::BGSLocation* location) {
            int depth = 0;
            for (auto* loc = location; loc && depth < 12; loc = loc->parentLoc, ++depth) {
                if ((loc->HasKeywordString("LocTypeCity") || loc->HasKeywordString("LocTypeTown")) && !loc->HasKeywordString("LocTypeDwelling")) {
                    return loc;
                }
            }
            return nullptr;
        }

        RE::BGSLocation* HoldOfLoc(RE::BGSLocation* location) {
            int depth = 0;
            for (auto* loc = location; loc && depth < 12; loc = loc->parentLoc, ++depth) {
                if (loc->HasKeywordString("LocTypeHold")) return loc;
            }
            return nullptr;
        }

        float RealNow() {
            return static_cast<float>(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        bool InFactionId(RE::Actor* actor, RE::FormID id) {
            auto* data = RE::TESDataHandler::GetSingleton();
            auto* faction = data ? data->LookupForm<RE::TESFaction>(id, "Skyrim.esm") : nullptr;
            return faction && actor->IsInFaction(faction);
        }

        // The work the town knows them by (the game's job factions), else their class.
        std::string WorkOf(RE::Actor* actor) {
            if (InFactionId(actor, 0x50920)) return "the jarl";
            if (InFactionId(actor, 0x50922)) return "the jarl's steward";
            if (actor->IsGuard()) return "a guard";
            if (InFactionId(actor, 0x5091B)) return "keeps the inn";
            if (InFactionId(actor, 0x53514)) return "a bard";
            if (InFactionId(actor, 0x51596) || actor->GetVendorFaction()) return "keeps a shop or trades";
            auto* npc = actor->GetActorBase();
            auto* cls = npc ? npc->npcClass : nullptr;
            const char* name = cls ? cls->GetFullName() : nullptr;
            return name && *name ? std::string(name) : std::string("townsfolk");
        }

        std::string Temperament(RE::TESNPC* npc) {
            if (!npc) return {};
            std::string out;
            switch (npc->GetMoralityLevel()) {
                case RE::ACTOR_MORALITY::kAnyCrime: out = "would do anything, even kill, if it paid"; break;
                case RE::ACTOR_MORALITY::kViolenceAgainstEnemy: out = "would hurt an enemy, never steal from a friend"; break;
                case RE::ACTOR_MORALITY::kPropertyCrimeOnly: out = "might steal or cheat, never hurt anyone"; break;
                default: out = "law-abiding, never commits a crime"; break;
            }
            switch (npc->GetConfidenceLevel()) {
                case RE::ACTOR_CONFIDENCE::kCowardly: out += "; cowardly"; break;
                case RE::ACTOR_CONFIDENCE::kCautious: out += "; cautious"; break;
                case RE::ACTOR_CONFIDENCE::kBrave: out += "; brave"; break;
                case RE::ACTOR_CONFIDENCE::kFoolhardy: out += "; reckless"; break;
                default: break;
            }
            switch (npc->GetAggressionLevel()) {
                case RE::ACTOR_AGGRESSION::kAggressive: out += "; hot-tempered"; break;
                case RE::ACTOR_AGGRESSION::kVeryAggressive:
                case RE::ACTOR_AGGRESSION::kFrenzied: out += "; violent"; break;
                default: out += "; peaceable"; break;
            }
            return out;
        }

        std::string Level(RE::BGSRelationship::RELATIONSHIP_LEVEL level) {
            using L = RE::BGSRelationship::RELATIONSHIP_LEVEL;
            switch (level) {
                case L::kLover: return "lover";
                case L::kAlly: return "close ally";
                case L::kConfidant: return "confidant";
                case L::kFriend: return "friend";
                case L::kAcquaintance: return "acquaintance";
                case L::kRival: return "rival";
                case L::kFoe: return "foe";
                case L::kEnemy: return "enemy";
                case L::kArchnemesis: return "sworn enemy";
                default: return "known";
            }
        }

        // What the game says ties this person to others (family, friends, rivals, enemies), at most a few.
        std::vector<std::string> TiesOf(RE::TESNPC* npc) {
            std::vector<std::string> out;
            if (!npc || !npc->relationships) return out;
            for (auto* rel : *npc->relationships) {
                if (!rel || out.size() >= 6) continue;
                auto* other = rel->npc1 == npc ? rel->npc2 : rel->npc1;
                if (!other || other == npc) continue;
                const char* otherName = other->GetFullName();
                if (!otherName || !*otherName) continue;
                std::string tie = std::string(otherName) + ": " + Level(rel->level.get());
                if (auto* assoc = rel->assocType) {
                    // npc1 holds the association's first part (the parent), npc2 the second (the child).
                    const int part = rel->npc1 == other ? RE::BGSAssociationType::Members::kParent : RE::BGSAssociationType::Members::kChild;
                    const int sex = other->IsFemale() ? RE::BGSAssociationType::Sexes::kFemale : RE::BGSAssociationType::Sexes::kMale;
                    const char* label = assoc->associationLabels[part][sex].c_str();
                    if (label && *label) tie = std::string(otherName) + ": their " + label + " (" + Level(rel->level.get()) + ")";
                }
                out.push_back(tie);
            }
            return out;
        }

        // The quests the player is on now (their journal's shown objectives): never to be contradicted or touched.
        std::vector<std::string> PlayerQuests() {
            std::vector<std::string> out;
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return out;
            std::vector<RE::TESQuest*> seen;
            for (auto& instance : player->GetPlayerRuntimeData().objectives) {
                auto* objective = instance.Objective;
                auto* quest = objective ? objective->ownerQuest : nullptr;
                if (!quest || std::find(seen.begin(), seen.end(), quest) != seen.end()) continue;
                if (instance.InstanceState != RE::QUEST_OBJECTIVE_STATE::kDisplayed) continue;
                seen.push_back(quest);
                const char* id = quest->GetFormEditorID();
                if (id && std::string_view(id).starts_with("IntelEngine")) continue;
                const char* name = quest->GetFullName();
                if (!name || !*name) continue;
                std::string kind;
                switch (quest->GetType()) {
                    case RE::QUEST_DATA::Type::kMainQuest: kind = "the main quest"; break;
                    case RE::QUEST_DATA::Type::kCivilWar: kind = "the civil war"; break;
                    case RE::QUEST_DATA::Type::kCompanionsQuest: kind = "the Companions"; break;
                    case RE::QUEST_DATA::Type::kMagesGuild: kind = "the College of Winterhold"; break;
                    case RE::QUEST_DATA::Type::kThievesGuild: kind = "the Thieves Guild"; break;
                    case RE::QUEST_DATA::Type::kDarkBrotherhood: kind = "the Dark Brotherhood"; break;
                    case RE::QUEST_DATA::Type::kDaedric: kind = "a Daedric prince"; break;
                    case RE::QUEST_DATA::Type::kDLC01_Vampire: kind = "Dawnguard"; break;
                    case RE::QUEST_DATA::Type::kDLC02_Dragonborn: kind = "Dragonborn"; break;
                    case RE::QUEST_DATA::Type::kSideQuest: kind = "a side quest"; break;
                    default: kind = "a task"; break;
                }
                std::string text = std::string(name) + " (" + kind + ")";
                if (objective->displayText.c_str() && *objective->displayText.c_str()) text += ": " + std::string(objective->displayText.c_str());
                out.push_back(text);
                if (out.size() >= 8) break;
            }
            return out;
        }

        // Things a story may turn on: the town's plain jewellery and a few small valuables and tools. Rings and the
        // like are hidden at home only (an NPC may put them on); the rest may sit in a pocket.
        std::vector<Tales::Item> ItemChoices() {
            std::vector<Tales::Item> out;
            std::vector<std::string> names;
            for (const auto id : QuestBridge::GetSingleton()->Keepsakes()) {
                const auto name = NameOf(id);
                if (name.empty() || std::find(names.begin(), names.end(), name) != names.end()) continue;
                names.push_back(name);
                out.push_back({name, id, false});
            }
            auto* index = ItemIndex::GetSingleton();
            for (const char* want : {"Ruby", "Garnet", "Amethyst", "Sapphire", "Emerald", "Silver Goblet", "Gold Ingot", "Silver Ingot",
                                     "Lute", "Flute", "Iron Dagger", "Steel Dagger"}) {
                auto* object = index ? index->FindByName(want) : nullptr;
                const char* name = object ? object->GetName() : nullptr;
                if (!object || !name || _stricmp(name, want) != 0) continue;   // only an exact match (the index is fuzzy)
                if (std::find(names.begin(), names.end(), name) != names.end()) continue;
                names.push_back(name);
                const bool wield = object->GetFormType() == RE::FormType::Weapon || object->GetFormType() == RE::FormType::Armor;
                out.push_back({name, object->GetFormID(), !wield});
            }
            return out;
        }

        // What the author is asked for, with the town, its people and what came before.
        struct Request {
            std::string context;           // the JSON the prompt renders
            Tales::Pool pool;
            RE::FormID asker = 0;
            RE::FormID town = 0;
            std::vector<RE::FormID> bios;  // whose bio summaries the worker adds (file reads stay off the game thread)
        };
        std::shared_ptr<Request> g_request;   // the tale being written (one at a time)
        float g_requestAt = -1.0f;

        constexpr std::array<const char*, 14> kGenres = {"crime",     "romance",    "family",  "mystery", "politics", "intrigue", "rivalry",
                                                         "redemption", "debt",      "a secret", "faith",  "adventure", "grief",  "jealousy"};
    }

    // ------------------------------------------------------------------ the request

    bool ArcBridge::RequestTale(RE::Actor* asker) {
        if (!asker || !SkyrimNetAPI::SendCustomPromptToLLM) return false;
        const float real = RealNow();
        if (m_taleBusy.load()) {
            if (g_requestAt >= 0.0f && real - g_requestAt > kWriteTimeout) {
                logger::warn("Tales: the tale asked for {:.0f} s ago never came; asking anew", real - g_requestAt);
                m_taleBusy.store(false);
            } else {
                return false;
            }
        }
        std::string why;
        if (!Usable(asker, 0, why)) {
            logger::info("Tales: no tale for {}: {}", asker->GetDisplayFullName(), why);
            return false;
        }
        const Folk* me = FolkOf(asker->GetFormID());
        if (!me) return false;
        std::vector<std::string> told;
        std::vector<std::string> recentGenres;
        {
            std::scoped_lock lock(m_mutex);
            if (m_book.Count(A::Phase::Planned) > 0 || m_book.Count(A::Phase::Open) >= A::kMaxOpen) return false;
            // What was told already (newest first): never again.
            for (auto it = m_book.all.rbegin(); it != m_book.all.rend() && told.size() < 10; ++it) {
                if (it->ending == A::Ending::Lapsed) continue;
                if (it->pattern == A::Pattern::Tale) {
                    told.push_back(it->genre + ": " + it->title + " - " + it->premise);
                    recentGenres.push_back(it->genre);
                } else {
                    told.push_back(A::DefaultTitle(it->pattern) + " (" + (it->pattern == A::Pattern::Thief ? "a theft in town" : it->pattern == A::Pattern::Feud ? "a quarrel settled" : "a murder solved") + ")");
                    recentGenres.push_back(it->pattern == A::Pattern::Thief ? "crime" : it->pattern == A::Pattern::Feud ? "rivalry" : "mystery");
                }
            }
        }

        auto request = std::make_shared<Request>();
        request->asker = asker->GetFormID();
        request->town = me->town;
        // ---- the people: the asker, then those of the town the game ties to them, then the town's known faces
        struct Candidate {
            const Folk* folk;
            int score;
        };
        std::vector<Candidate> others;
        auto* askerBase = asker->GetActorBase();
        const auto seed = A::detail::Mix(asker->GetFormID(), static_cast<std::uint32_t>(NowDays() * 1440.0f));
        for (const auto& f : m_folk) {
            if (f.town != me->town || f.ref == me->ref) continue;
            auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(f.base);
            if (!npc) continue;
            int score = static_cast<int>(A::detail::Mix(seed, f.ref) % 5);
            if (npc->relationships && !npc->relationships->empty()) score += 4;
            if (askerBase && RE::BGSRelationship::GetRelationship(askerBase, npc)) score += 6;
            if (f.home) score += 2;
            others.push_back({&f, score});
        }
        std::sort(others.begin(), others.end(), [](const Candidate& x, const Candidate& y) { return x.score > y.score; });

        nlohmann::json people = nlohmann::json::array();
        const auto add = [&](const Folk& f, bool isAsker) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(f.ref);
            if (!actor) return false;
            std::string reason;
            if (!isAsker && !Usable(actor, 0, reason)) return false;
            auto* npc = actor->GetActorBase();
            Tales::Person person;
            person.key = "p" + std::to_string(request->pool.people.size());
            person.name = actor->GetDisplayFullName() ? actor->GetDisplayFullName() : "";
            person.form = f.ref;
            person.hasHome = f.home != 0;
            std::string none;
            person.canJail = !isAsker && Usable(actor, 1, none);
            const auto confidence = npc ? npc->GetConfidenceLevel() : RE::ACTOR_CONFIDENCE::kAverage;
            const bool tradesOrServes = InFactionId(actor, 0x5091B) || InFactionId(actor, 0x51596) || InFactionId(actor, 0x53514) ||
                                        actor->GetVendorFaction() != nullptr;
            person.canBrawl = !isAsker && Usable(actor, 2, none) && !tradesOrServes && static_cast<int>(confidence) >= 2;
            if (person.name.empty()) return false;
            nlohmann::json j;
            j["key"] = person.key;
            j["name"] = person.name;
            j["sex"] = npc && npc->IsFemale() ? "woman" : "man";
            auto* race = actor->GetRace();
            j["race"] = race && race->GetFullName() ? race->GetFullName() : "";
            j["work"] = WorkOf(actor);
            j["temperament"] = Temperament(npc);
            auto* homeCell = f.home ? RE::TESForm::LookupByID<RE::TESObjectCELL>(f.home) : nullptr;
            j["home"] = homeCell && homeCell->GetFullName() && *homeCell->GetFullName() ? std::string(homeCell->GetFullName())
                        : f.home                                                         ? std::string("a home of their own")
                                                                                         : std::string("no home of their own");
            nlohmann::json can = nlohmann::json::array();
            if (person.canJail) can.push_back("may be taken by the guard or leave town for a while");
            if (person.canBrawl) can.push_back("would settle a matter with fists");
            j["may"] = can;
            j["ties"] = TiesOf(npc);
            request->pool.people.push_back(person);
            request->bios.push_back(f.ref);
            people.push_back(j);
            return true;
        };
        if (!add(*me, true)) return false;
        for (const auto& c : others) {
            if (request->pool.people.size() >= kPoolSize) break;
            add(*c.folk, false);
        }
        if (request->pool.people.size() < 3) {
            logger::info("Tales: no tale for {}: too few free people in {}", asker->GetDisplayFullName(), NameOf(me->town));
            return false;
        }
        request->pool.items = ItemChoices();
        auto* player = RE::PlayerCharacter::GetSingleton();
        request->pool.gold = A::RewardGold(A::Pattern::Thief, player ? player->GetLevel() : 10);

        // The genre to lean toward: one not told lately, in an order of the day.
        std::vector<std::string> fresh;
        for (const auto* g : kGenres) {
            if (std::find(recentGenres.begin(), recentGenres.begin() + std::min<std::size_t>(recentGenres.size(), 5), g) == recentGenres.end()) fresh.push_back(g);
        }
        const std::string lean = fresh.empty() ? std::string(kGenres[seed % kGenres.size()]) : fresh[seed % fresh.size()];

        nlohmann::json ctx;
        ctx["player_name"] = PlayerName();
        if (player) {
            auto* base = player->GetActorBase();
            ctx["player_sex"] = base && base->IsFemale() ? "woman" : "man";
            auto* race = player->GetRace();
            ctx["player_race"] = race && race->GetFullName() ? race->GetFullName() : "";
            ctx["player_level"] = player->GetLevel();
        }
        auto* town = RE::TESForm::LookupByID<RE::BGSLocation>(me->town);
        auto* hold = HoldOfLoc(town);
        ctx["town"] = town && town->GetFullName() ? town->GetFullName() : "the town";
        ctx["hold"] = hold && hold->GetFullName() ? hold->GetFullName() : "";
        ctx["asker"] = request->pool.people[0].key;
        ctx["asker_name"] = request->pool.people[0].name;
        ctx["people"] = people;
        nlohmann::json items = nlohmann::json::array();
        for (const auto& i : request->pool.items) items.push_back(i.pocket ? i.name : i.name + " (home only)");
        ctx["items"] = items;
        ctx["quests"] = PlayerQuests();
        ctx["told"] = told;
        ctx["lean"] = lean;
        ctx["reward"] = request->pool.gold;
        request->context = ctx.dump();

        m_taleBusy.store(true);
        g_requestAt = real;
        g_request = request;
        logger::info("Tales: asking for a tale of {} in {} ({} people, {} items, leaning {}, {} told before)", request->pool.people[0].name,
                     NameOf(me->town), request->pool.people.size(), request->pool.items.size(), lean, told.size());
        // Bio summaries are files: read on the worker, then sent from there.
        AsyncDispatch::Submit([this, request]() {
            auto ctx = nlohmann::json::parse(request->context, nullptr, false);
            if (ctx.is_object() && ctx["people"].is_array()) {
                for (std::size_t i = 0; i < ctx["people"].size() && i < request->bios.size(); ++i) {
                    std::string bio;
                    try {
                        bio = MemoryDB::GetSingleton()->GetNPCBioSummary(request->bios[i]);
                    } catch (...) {
                    }
                    if (bio.size() > 500) {
                        std::size_t n = 500;
                        while (n > 0 && (static_cast<unsigned char>(bio[n]) & 0xC0) == 0x80) --n;
                        bio = bio.substr(0, n) + "...";
                    }
                    if (!bio.empty()) ctx["people"][i]["about"] = bio;
                }
                request->context = ctx.dump();
            }
            SendTale(request, 0, {}, {});
        });
        return true;
    }

    void ArcBridge::SendTale(std::shared_ptr<void> handle, int attempt, std::string fix, std::string previous) {
        auto request = std::static_pointer_cast<Request>(handle);
        std::string context = request->context;
        if (attempt > 0) {
            auto ctx = nlohmann::json::parse(context, nullptr, false);
            if (ctx.is_object()) {
                ctx["fix"] = fix;
                ctx["previous"] = previous.size() > 6000 ? previous.substr(0, 6000) : previous;
                context = ctx.dump();
            }
        }
        const bool queued = SkyrimNetAPI::SendCustomPromptToLLM(
            "intel_tale_author", "intel_story_dm", context.c_str(), [this, request, attempt](const char* response, int success) {
                std::string text = success && response ? response : "";
                if (auto* task = SKSE::GetTaskInterface()) {
                    task->AddTask([this, request, attempt, text]() { TaleWritten(request, attempt, text); });
                }
            });
        if (!queued) {
            logger::warn("Tales: the request could not be sent");
            if (auto* task = SKSE::GetTaskInterface()) task->AddTask([this, request]() { TaleWritten(request, 9, {}); });
        }
    }

    void ArcBridge::TaleWritten(std::shared_ptr<void> handle, int attempt, const std::string& text) {
        auto request = std::static_pointer_cast<Request>(handle);
        if (g_request != request) return;   // a newer request replaced it (timed out)
        const auto fallback = [&](const char* why) {
            logger::info("Tales: no tale ({}); the town's story falls back to a pattern", why);
            g_request.reset();
            m_taleBusy.store(false);
            auto* asker = RE::TESForm::LookupByID<RE::Actor>(request->asker);
            if (!asker) return;
            const auto id = PlanFor(asker, 0, true);
            if (id) SendOffer(id, asker);
        };
        if (text.empty()) return fallback("no answer from the model");
        const auto open = text.find('{');
        const auto close = text.rfind('}');
        nlohmann::json json;
        if (open != std::string::npos && close != std::string::npos && close > open) json = nlohmann::json::parse(text.substr(open, close - open + 1), nullptr, false);
        auto result = Tales::Compile(json, request->pool);
        if (!result.ok) {
            logger::info("Tales: the tale (attempt {}) does not hold: {}", attempt + 1, result.errors);
            if (attempt == 0) {
                AsyncDispatch::Submit([this, request, errors = result.errors, text]() { SendTale(request, 1, errors, text); });
                return;
            }
            return fallback("the tale did not hold twice");
        }
        if (!result.notes.empty()) logger::info("Tales: mended: {}", result.notes);
        // Time passed while it was written: everyone must still be free.
        auto& a = result.arc;
        for (std::size_t r = 1; r < A::kRoles; ++r) {
            if (!a.cast[r]) continue;
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(a.cast[r]);
            std::string why;
            if (!actor || !Usable(actor, 0, why)) {
                logger::info("Tales: {} is no longer free ({})", NameOf(a.cast[r]), why);
                return fallback("the cast is no longer free");
            }
        }
        a.place = request->town;
        a.seed = A::detail::Mix(request->asker, static_cast<std::uint32_t>(NowDays() * 1440.0f));
        g_request.reset();
        m_taleBusy.store(false);
        const auto id = PlanTale(std::move(a));
        auto* asker = RE::TESForm::LookupByID<RE::Actor>(request->asker);
        if (id && asker) SendOffer(id, asker);
    }

    void ArcBridge::SendOffer(std::uint32_t id, RE::Actor* asker) {
        if (auto* source = SKSE::GetModCallbackEventSource()) {
            SKSE::ModCallbackEvent event{};
            event.eventName = "IntelEngine_ArcOffer";
            event.strArg = "";
            event.numArg = static_cast<float>(id);
            event.sender = asker;
            source->SendEvent(&event);
        }
    }

    std::uint32_t ArcBridge::PlanTale(A::Arc a) {
        const float now = NowDays();
        std::scoped_lock lock(m_mutex);
        if (const auto refusal = m_book.Refusal(a); !refusal.empty()) {
            logger::info("Tales: \"{}\" not planned: {}", a.title, refusal);
            return 0;
        }
        const auto id = m_book.Plan(a, now);
        if (!id) return 0;
        m_pending.erase(id);
        const auto* planned = m_book.Find(id);
        std::string cast;
        for (std::size_t r = 1; r < A::kRoles; ++r) {
            if (planned->cast[r]) cast += " " + std::to_string(r) + "=" + NameOf(planned->cast[r]) + " (" + planned->roleName[r] + ")";
        }
        logger::info("Tales: #{} planned: \"{}\" ({}), {} steps, {} gold;{}", id, planned->title, planned->genre, planned->graph.size(), planned->gold, cast);
        logger::info("Tales: #{} premise: {}", id, planned->premise);
        return id;
    }

    // ------------------------------------------------------------------ the judge of a talk

    void ArcBridge::TaleTalk(std::uint32_t id, const A::Arc& a, Pending& pending, const A::Facts& f) {
        if (a.node >= a.graph.size()) return;
        const auto& n = a.graph[a.node];
        if (n.kind != A::Kind::Talk || !n.who || n.turns[0].empty() || pending.judging || pending.turn >= 0) return;
        if (f.with[n.who] && f.spoke[n.who]) pending.talked = true;
        if (!pending.talked) return;
        auto* speech = SpeechTracker::GetSingleton();
        const RE::FormID who = a.cast[n.who];
        if (speech->IsSpeechPending(who)) return;
        const float theirs = speech->SecondsSinceSpeechEnded(who);
        const float players = speech->SecondsSincePlayerSpoke();
        const bool quiet = theirs >= kQuietSeconds && (players < 0.0f || players >= kQuietSeconds);
        if (!quiet && f.with[n.who]) return;   // still talking (walking away ends the talk at once)
        pending.talked = false;
        pending.judging = true;
        pending.judgedAt = RealNow();
        // After two readings that found nothing yet, the next one must pick the outcome closest to what was said.
        SendJudge(id, a.node, who, n.goal, n.turns, pending.nones >= 2);
    }

    void ArcBridge::SendJudge(std::uint32_t id, std::uint8_t node, RE::FormID who, std::string goal, std::array<std::string, 3> turns, bool force) {
        const auto lines = DialogueTracker::GetSingleton()->ConversationWith(who, kTalkWindow, kTalkLines);
        const std::string name = NameOf(who);
        const std::string player = PlayerName();
        std::string transcript;
        for (const auto& line : lines) transcript += (line.speaker == 0x14 ? player : NameOf(line.speaker)) + ": " + line.text + "\n";
        nlohmann::json ctx;
        ctx["npc_name"] = name;
        ctx["player_name"] = player;
        ctx["goal"] = goal;
        ctx["transcript"] = transcript.empty() ? std::string("(nothing was heard)") : transcript;
        ctx["allow_none"] = !force;
        nlohmann::json outcomes = nlohmann::json::array();
        for (std::size_t k = 0; k < turns.size(); ++k) {
            if (turns[k].empty()) continue;
            outcomes.push_back({{"key", "o" + std::to_string(k)}, {"text", turns[k]}});
        }
        ctx["outcomes"] = outcomes;
        const std::string context = ctx.dump();
        logger::info("Tales: #{} reading the talk with {} ({} lines{})", id, name, lines.size(), force ? ", an outcome must be named" : "");
        const auto answer = [this, id, node](const std::string& choice) {
            int turn = -1;
            if (choice.size() == 2 && choice[0] == 'o' && choice[1] >= '0' && choice[1] <= '2') turn = choice[1] - '0';
            Judged(id, node, turn);
        };
        const auto byChat = [context, answer]() {
            if (!SkyrimNetAPI::SendCustomPromptToLLM) return answer("none");
            const bool queued = SkyrimNetAPI::SendCustomPromptToLLM("intel_tale_turn_chat", "intel_story_dm", context.c_str(),
                [answer](const char* response, int success) {
                    std::string choice = "none";
                    if (success && response) {
                        const std::string text = response;
                        const auto open = text.find('{');
                        const auto close = text.rfind('}');
                        if (open != std::string::npos && close != std::string::npos && close > open) {
                            const auto json = nlohmann::json::parse(text.substr(open, close - open + 1), nullptr, false);
                            if (json.is_object()) choice = json.value("turn", std::string("none"));
                        }
                    }
                    answer(choice);
                });
            if (!queued) answer("none");
        };
        if (!SkyrimNetAPI::SendCustomDecisionToLLM) return byChat();
        const bool queued = SkyrimNetAPI::SendCustomDecisionToLLM("intel_tale_turn", context.c_str(), 0,
            [answer, byChat](const char*, const char* resultJson, int success) {
                const auto json = nlohmann::json::parse(resultJson ? resultJson : "", nullptr, false);
                if (!success) {
                    const std::string error = json.is_object() ? json.value("error", "") : "";
                    if (error == "no_decisions_route") return byChat();
                    return answer("none");
                }
                std::string choice = "none";
                if (json.is_object() && json.contains("answers") && json["answers"].is_object() && json["answers"].contains("turn") &&
                    json["answers"]["turn"].is_object()) {
                    choice = json["answers"]["turn"].value("choice", std::string("none"));
                }
                answer(choice);
            });
        if (!queued) byChat();
    }

    void ArcBridge::Judged(std::uint32_t id, std::uint8_t node, int turn) {
        std::scoped_lock lock(m_mutex);
        const auto* a = m_book.Find(id);
        auto it = m_pending.find(id);
        if (!a || !A::IsOpen(*a) || it == m_pending.end() || it->second.node != node || a->node != node) return;
        auto& pending = it->second;
        pending.judging = false;
        if (turn >= 0 && turn < 3 && node < a->graph.size() && !a->graph[node].turns[static_cast<std::size_t>(turn)].empty()) {
            pending.turn = turn;
            logger::info("Tales: #{} the talk turned out: {}", id, a->graph[node].turns[static_cast<std::size_t>(turn)]);
        } else {
            ++pending.nones;
            logger::info("Tales: #{} the talk has not turned yet ({} readings)", id, pending.nones);
        }
    }
}
