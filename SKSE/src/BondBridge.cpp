// IntelEngine: how somebody stands with the player, read from their memories of the player (BondBridge.h, Bonds.h).

#include "BondBridge.h"

#include "AsyncDispatch.h"
#include "FrameCost.h"
#include "HeartBridge.h"
#include "MemoryDB.h"
#include "Safe.h"
#include "SkyrimNetAPI.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <map>
#include <vector>

namespace IntelEngine {

    namespace {
        constexpr const char* kRelationships = "SNRom_Integration.esp";
        constexpr double kCountEverySeconds = 300.0;   // how often the counts are read again when nothing else reads them
        constexpr float kWith = 3000.0f;

        double RealSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        float NowDays() {
            auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? calendar->GetCurrentGameTime() : 0.0f;
        }

        std::string PlayerName() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const char* name = player ? player->GetDisplayFullName() : nullptr;
            return name && *name ? name : "the traveller";
        }

        // Somebody whose standing with the player is worth reading: a named, unique, grown person of the world. A
        // child is never read (nothing of the heart is ever asked about one), nor a creature or a nameless guard.
        bool Person(RE::Actor* actor) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!actor || actor == player || actor->IsDead() || actor->IsDeleted()) return false;
            auto* base = actor->GetActorBase();
            if (!base || !base->IsUnique()) return false;
            const char* name = actor->GetDisplayFullName();
            if (!name || !*name) return false;
            return !HeartBridge::Childlike(actor);
        }

        bool WithPlayer(RE::Actor* who) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!who || !player || !who->Is3DLoaded()) return false;
            auto* theirs = who->GetParentCell();
            auto* cell = player->GetParentCell();
            if (!theirs || !cell || theirs->IsInteriorCell() != cell->IsInteriorCell()) return false;
            if (cell->IsInteriorCell() && cell != theirs) return false;
            if (who->GetWorldspace() != player->GetWorldspace()) return false;
            return who->GetPosition().GetDistance(player->GetPosition()) <= kWith;
        }

        // Is a relationship mod installed that keeps bonds itself? Looked up once the game's data is there (by a
        // plugin's name the game walks its load order).
        bool RelationshipMod() {
            static std::atomic<int> known{-1};
            if (const int k = known.load(); k >= 0) return k == 1;
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data || !data->LookupForm<RE::TESForm>(0x0C6472, "Skyrim.esm")) return false;   // too early to say
            const bool there = data->LookupModByName(kRelationships) != nullptr;
            known.store(there ? 1 : 0);
            logger::info("Bonds: {}", there ? "SkyrimNet Relationships keeps how people stand with the player; nothing is read here"
                                           : "no relationship mod is installed: how people stand with the player is read from what they remember");
            return there;
        }
    }

    BondBridge* BondBridge::GetSingleton() {
        static BondBridge instance;
        return &instance;
    }

    bool BondBridge::Used() const { return m_pretend.load() || !RelationshipMod(); }

    void BondBridge::Pretend(bool noRelationshipMod) {
        m_pretend.store(noRelationshipMod);
        logger::info("Bonds: test: {}", noRelationshipMod ? "going on as if no relationship mod were installed" : "the installed mods count again");
    }

    Bonds::Bond BondBridge::Of(std::uint32_t actor) const {
        std::scoped_lock lock(m_mutex);
        const auto it = m_book.find(actor);
        return it != m_book.end() ? it->second : Bonds::Bond{};
    }

    void BondBridge::Heard(std::uint32_t speaker) {
        if (!speaker || speaker == 0x14) return;
        std::scoped_lock lock(m_mutex);
        if (m_book.size() >= Bonds::kMost && !m_book.contains(speaker)) return;
        ++m_book[speaker].times;
    }

    int BondBridge::Times(std::uint32_t actor) const {
        int own = 0;
        {
            std::scoped_lock lock(m_mutex);
            if (const auto it = m_book.find(actor); it != m_book.end()) own = it->second.times;
        }
        return (std::max)(own, MemoryDB::GetSingleton()->TimesWithPlayer(actor));
    }

    std::string BondBridge::Describe(std::uint32_t actor) const {
        const auto b = Of(actor);
        if (!Bonds::Judged(b)) return "not read";
        std::string out = Bonds::Word(b);
        if (b.drawn) {
            out += ", drawn to the player";
            out += b.answer > 0 ? " and welcome" : b.answer < 0 ? ", turned down" : ", unanswered";
        }
        return out + std::format(" (read at day {:.1f}, {} times together)", b.judgedAt, b.eventsAt);
    }

    void BondBridge::Tick() {
        if (!Used()) return;
        static FrameStats stats;
        FrameClock clock("Bonds: a look at who is to be read", stats, FrameClock::Now(), 60);
        const double real = RealSeconds();
        // How often the two have had to do with each other is SkyrimNet's to count; the director reads it on its
        // rounds, and when nothing has read it for a while it is read here, on the worker.
        if (real - m_lastCount >= kCountEverySeconds && !m_counting.exchange(true)) {
            m_lastCount = real;
            AsyncDispatch::Submit([this]() {
                const auto rows = MemoryDB::GetSingleton()->GetPlayerRelationshipData();
                const auto often = std::count_if(rows.begin(), rows.end(), [](const auto& row) { return row.interactionCount >= Bonds::kFirstAt; });
                logger::info("Bonds: SkyrimNet has seen {} people with the player, {} of them {} times or more", rows.size(), often, Bonds::kFirstAt);
                m_counting.store(false);
            });
        }
        if (m_busy.load() || real - m_lastRead < Bonds::kPauseSeconds) return;
        // SkyrimNet's count and IntelEngine's own: the larger for each of them.
        std::map<std::uint32_t, int> counts;
        for (const auto& [id, times] : MemoryDB::GetSingleton()->TimesWithPlayerAll(Bonds::kFirstAt)) counts[id] = times;
        const float now = NowDays();
        std::vector<Bonds::Candidate> due;
        {
            std::scoped_lock lock(m_mutex);
            for (const auto& [id, bond] : m_book) {
                if (bond.times >= Bonds::kFirstAt) counts[id] = (std::max)(counts[id], bond.times);
            }
            for (const auto& [id, events] : counts) {
                const auto it = m_book.find(id);
                const Bonds::Bond bond = it != m_book.end() ? it->second : Bonds::Bond{};
                if (!Bonds::Due(bond, events, now)) continue;
                Bonds::Candidate c;
                c.id = id;
                c.events = events;
                c.since = Bonds::Judged(bond) ? events - bond.eventsAt : events;
                due.push_back(c);
            }
        }
        if (due.empty()) return;
        // The game is asked about a few of them only: whoever has had most to do with the player first.
        std::sort(due.begin(), due.end(), [](const Bonds::Candidate& a, const Bonds::Candidate& b) { return a.since > b.since; });
        if (due.size() > 24) due.resize(24);
        RE::Actor* best = nullptr;
        Bonds::Candidate chosen;
        for (auto& c : due) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(c.id);
            if (!Person(actor)) {
                // Nobody to read (a creature, a child, somebody dead): noted, so that they are not looked at every time.
                std::scoped_lock lock(m_mutex);
                auto& bond = m_book[c.id];
                bond = Bonds::NothingToRead(bond, c.events, now);
                bond.standing = Bonds::Stranger;
                continue;
            }
            c.with = WithPlayer(actor);
            c.here = actor->Is3DLoaded();
            if (!best || Bonds::Before(c, chosen)) {
                best = actor;
                chosen = c;
            }
        }
        if (best) Start(best, chosen.events, false);
    }

    bool BondBridge::JudgeNow(RE::Actor* who) {
        if (!Person(who) || m_busy.load()) return false;
        return Start(who, Times(who->GetFormID()), true);
    }

    bool BondBridge::Start(RE::Actor* who, int events, bool forced) {
        if (m_busy.exchange(true)) return false;
        m_lastRead = RealSeconds();
        const std::uint32_t id = who->GetFormID();
        const char* raw = who->GetDisplayFullName();
        const std::string name = raw ? raw : "";
        const std::string player = PlayerName();
        const float now = NowDays();
        logger::info("Bonds: reading what {} ({:08X}) remembers of the player ({} times together{})", name, id, events, forced ? ", asked for by a test" : "");
        AsyncDispatch::Submit([this, id, name, player, events, now, forced]() mutable {
            // The worker: SkyrimNet's database is read here, never inside a frame.
            int count = 0;
            auto* db = MemoryDB::GetSingleton();
            if (forced) {
                // A test asks before the count has come round: count first.
                db->GetPlayerRelationshipData();
                events = (std::max)(events, db->TimesWithPlayer(id));
            }
            const std::string memories = db->GetMemoriesAbout(id, player, Bonds::kMemories, count);
            const std::string talk = db->GetRecentDialogueForActor(id, Bonds::kExchanges);
            const auto done = [this, id, events, now, count, hasTalk = !talk.empty()](const std::string& standing, const std::string& heart,
                                                                                     const std::string& answer, float sure, const std::string& failure) {
                if (auto* task = SKSE::GetTaskInterface()) {
                    task->AddTask([=, this]() { Finish(id, standing, heart, answer, sure, events, now, count, hasTalk, failure); });
                } else {
                    m_busy.store(false);
                }
            };
            if (memories.empty() && talk.empty()) return done({}, {}, {}, 0.0f, "nothing on record");
            nlohmann::json ctx;
            ctx["npc_name"] = name;
            ctx["player_name"] = player;
            ctx["memories"] = memories.empty() ? std::string("(none on record)") : memories;
            ctx["talk"] = talk.empty() ? std::string("(none on record)") : talk;
            const std::string context = ctx.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            // (The chat form gives its answers no weight: sure 0, so it is believed when it says the same twice.)
            const auto byChat = [context, done]() {
                if (!SkyrimNetAPI::SendCustomPromptToLLM) return done({}, {}, {}, 0.0f, "no model to ask");
                const bool queued = SkyrimNetAPI::SendCustomPromptToLLM("intel_bond_chat", "intel_story_dm", context.c_str(),
                    [done](const char* response, int success) {
                        if (!success || !response) return done({}, {}, {}, 0.0f, "the model did not answer");
                        const std::string text = response;
                        const auto open = text.find('{');
                        const auto close = text.rfind('}');
                        if (open == std::string::npos || close == std::string::npos || close <= open) return done({}, {}, {}, 0.0f, "an answer that is no answer");
                        const auto json = nlohmann::json::parse(text.substr(open, close - open + 1), nullptr, false);
                        done(Safe::Text(json, "standing"), Safe::Text(json, "heart", "none"), Safe::Text(json, "answer", "unanswered"), 0.0f, {});
                    });
                if (!queued) done({}, {}, {}, 0.0f, "the model could not be asked");
            };
            if (!SkyrimNetAPI::SendCustomDecisionToLLM) return byChat();
            const bool queued = SkyrimNetAPI::SendCustomDecisionToLLM("intel_bond", context.c_str(), 4,
                [done, byChat](const char*, const char* resultJson, int success) {
                    const auto json = nlohmann::json::parse(resultJson ? resultJson : "", nullptr, false);
                    if (!success) {
                        if (Safe::Text(json, "error") == "no_decisions_route") return byChat();   // no decision provider: the chat model reads it
                        return done({}, {}, {}, 0.0f, "no decision (" + Safe::Text(json, "error", "unknown") + ")");
                    }
                    // Each answer with the weight the reader gave it (0 when it gave none).
                    const auto choice = [&json](const char* question, const char* fallback, float& weight) {
                        weight = 0.0f;
                        if (!json.is_object() || !json.contains("answers") || !json["answers"].is_object()) return std::string(fallback);
                        const auto& answers = json["answers"];
                        if (!answers.contains(question) || !answers[question].is_object()) return std::string(fallback);
                        const auto& one = answers[question];
                        const std::string chosen = Safe::Text(one, "choice", fallback);
                        if (one.contains("probabilities") && one["probabilities"].is_object()) {
                            for (const auto& [key, p] : one["probabilities"].items()) {
                                if (key == chosen && p.is_number()) weight = p.get<float>();
                            }
                        }
                        return chosen;
                    };
                    float sureOfStanding = 0.0f, sureOfHeart = 0.0f, unused = 0.0f;
                    const std::string standing = choice("standing", "", sureOfStanding);
                    const std::string heart = choice("heart", "none", sureOfHeart);
                    const std::string answer = choice("answer", "unanswered", unused);
                    done(standing, heart, answer, (std::min)(sureOfStanding, sureOfHeart), {});
                });
            if (!queued) byChat();
        });
        return true;
    }

    void BondBridge::Finish(std::uint32_t actor, const std::string& standing, const std::string& heart, const std::string& answer, float sure,
                            int events, float nowDays, int memories, bool talk, const std::string& failure) {
        std::string was, is;
        Bonds::Reading read = Bonds::Reading::Unreadable;
        {
            std::scoped_lock lock(m_mutex);
            auto& bond = m_book[actor];
            was = Bonds::Word(bond);
            // (The names come through the game's strings on some paths: compared whatever their case.)
            const auto lower = [](std::string s) {
                for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return s;
            };
            if (failure.empty()) read = Bonds::Read(lower(standing), lower(heart), lower(answer), sure, bond, events, nowDays);
            if (read == Bonds::Reading::Unreadable) bond = Bonds::NothingToRead(bond, events, nowDays);
            is = Bonds::Word(bond);
            if (bond.drawn) is += bond.answer > 0 ? ", drawn to the player and welcome" : bond.answer < 0 ? ", drawn to the player and turned down" : ", drawn to the player, unanswered";
        }
        m_busy.store(false);
        auto* form = RE::TESForm::LookupByID<RE::Actor>(actor);
        const char* raw = form ? form->GetDisplayFullName() : nullptr;
        const std::string name = raw && *raw ? raw : "someone";
        if (read == Bonds::Reading::Taken) {
            logger::info("Bonds: {} stands to the player as {} (was {}; {} memories{} read, {} times together; the reader said {} / {} / {}, sure {:.2f})",
                         name, is, was, memories, talk ? " and their last talk" : "", events, standing, heart, answer, sure);
        } else if (read == Bonds::Reading::Noted) {
            logger::info("Bonds: {} still stands to the player as {}: the reader said {} / {} / {} and was not sure ({:.2f}); believed if the next "
                         "reading says the same ({} memories{} read, {} times together)",
                         name, is, standing, heart, answer, sure, memories, talk ? " and their last talk" : "", events);
        } else {
            logger::info("Bonds: nothing read of {} ({}): {} by the count, looked at again when more has passed between them", name,
                         failure.empty() ? "the reader named no standing: '" + standing + "'" : failure, is);
        }
    }

    // ------------------------------------------------------------------ kept with the save

    void BondBridge::Save(SKSE::SerializationInterface* intfc) const {
        std::vector<std::uint8_t> bytes;
        {
            std::scoped_lock lock(m_mutex);
            bytes = Bonds::Serialize(m_book);
        }
        if (!intfc->OpenRecord(Bonds::kRecordType, Bonds::kRecordVersion)) {
            logger::error("Bonds: could not open the co-save record");
            return;
        }
        intfc->WriteRecordData(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    }

    void BondBridge::Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length) {
        if (version != Bonds::kRecordVersion || length > (1u << 17)) {
            logger::warn("Bonds: skipping a co-save record of version {} ({} bytes)", version, length);
            return;
        }
        std::vector<std::uint8_t> bytes(length);
        if (length > 0 && intfc->ReadRecordData(bytes.data(), length) != length) {
            logger::warn("Bonds: the co-save record is short: everybody is read anew");
            return;
        }
        Bonds::Book saved;
        if (!Bonds::Deserialize(bytes, saved)) {
            logger::warn("Bonds: the co-save record cannot be read: everybody is read anew");
            return;
        }
        // The load order may have changed since: every id is asked for again, and whoever is gone is dropped.
        Bonds::Book book;
        for (const auto& [id, bond] : saved) {
            RE::FormID resolved = 0;
            if (intfc->ResolveFormID(id, resolved) && resolved) book.emplace(resolved, bond);
        }
        std::scoped_lock lock(m_mutex);
        m_book = std::move(book);
        logger::info("Bonds: what was read of {} people comes back with the save", m_book.size());
    }

    void BondBridge::Revert() {
        std::scoped_lock lock(m_mutex);
        m_book.clear();
        m_lastRead = -1.0e9;
        m_lastCount = -1.0e9;
    }

}
