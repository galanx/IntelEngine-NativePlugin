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

        // Is a relationship mod installed that keeps bonds itself, and by which factions does it say so? SkyrimNet
        // Relationships: member of its bond faction = watched, the rank its tier (0 to 5); its enmity faction's rank 1
        // disliked, 2 resentful, 3 enemy (its README invites other mods to read both). Looked up once the game's data
        // is there: asked for by a plugin's name the game walks its whole load order.
        constexpr RE::FormID kBondFaction = 0xD63;
        constexpr RE::FormID kEnmityFaction = 0xD64;
        constexpr double kTheirsAgainSeconds = 300.0;   // whoever that mod watches is asked about again this much later

        struct Mod {
            bool looked = false;
            bool there = false;
            RE::TESFaction* bond = nullptr;
            RE::TESFaction* enmity = nullptr;
        };
        Mod TheMod() {
            static Mod mod;
            static std::mutex guard;
            std::scoped_lock lock(guard);
            if (!mod.looked) {
                auto* data = RE::TESDataHandler::GetSingleton();
                // (Too early to say until the game's own records are there. Asked for as a faction: asked for as a
                // plain form the lookup answers nothing for any record, and this was never known: until 2026-11-21
                // everybody was read here, the heart too, in a game with SkyrimNet Relationships installed; a test
                // switch had stood in for the real thing.)
                if (!data || !data->LookupForm<RE::TESFaction>(0x0C6472, "Skyrim.esm")) return mod;
                // In the load order, not merely in the Data folder: a plugin that is there and unticked keeps nothing.
                mod.there = data->LookupLoadedModByName(kRelationships) != nullptr || data->LookupLoadedLightModByName(kRelationships) != nullptr;
                if (mod.there) {
                    mod.bond = data->LookupForm<RE::TESFaction>(kBondFaction, kRelationships);
                    mod.enmity = data->LookupForm<RE::TESFaction>(kEnmityFaction, kRelationships);
                }
                mod.looked = true;
                if (!mod.there) {
                    logger::info("Bonds: no relationship mod is installed: how people stand with the player is read from what they remember, the heart too");
                } else if (mod.bond) {
                    logger::info("Bonds: SkyrimNet Relationships speaks for whoever it watches (its factions {:08X}{}); everybody else is read here for "
                                 "friendship and ill will, never for anything of the heart",
                                 mod.bond->GetFormID(), mod.enmity ? std::format(" and {:08X}", mod.enmity->GetFormID()) : std::string(", none for ill will"));
                } else {
                    logger::warn("Bonds: SkyrimNet Relationships is installed but its bond faction was not found: nobody counts as watched by it; "
                                 "everybody is read here for friendship and ill will, never for anything of the heart");
                }
            }
            return mod;
        }
    }

    BondBridge* BondBridge::GetSingleton() {
        static BondBridge instance;
        return &instance;
    }

    void BondBridge::Pretend(bool noRelationshipMod) {
        m_pretend.store(noRelationshipMod);
        logger::info("Bonds: test: {}", noRelationshipMod ? "going on as if no relationship mod were installed" : "the installed mods count again");
    }

    Bonds::Theirs BondBridge::TheirsOf(RE::Actor* who) const {
        Bonds::Theirs theirs;
        if (m_pretend.load()) return theirs;
        const Mod mod = TheMod();
        if (!mod.there) return theirs;
        theirs.installed = true;
        if (!who || !mod.bond) return theirs;
        // One pass over what they belong to: the game's own record first, then what has changed since (joined, left,
        // promoted). The last word counts, and a rank below nothing means they have left.
        int bond = -1, enmity = -1;
        who->VisitFactions([&](RE::TESFaction* faction, std::int8_t rank) {
            if (faction == mod.bond) bond = rank < 0 ? -1 : rank;
            else if (mod.enmity && faction == mod.enmity) enmity = rank < 0 ? -1 : rank;
            return false;
        });
        if (bond < 0) return theirs;
        theirs.tracked = true;
        theirs.tier = static_cast<std::uint8_t>((std::min)(bond, 5));
        theirs.ill = static_cast<std::uint8_t>(std::clamp(enmity, 0, 3));
        return theirs;
    }

    Bonds::View BondBridge::Seen(RE::Actor* who) {
        if (!who) return {};
        const Bonds::Theirs theirs = TheirsOf(who);
        Bonds::View view;
        Bonds::Bond after;
        bool handedNow = false;
        {
            std::scoped_lock lock(m_mutex);
            const auto it = m_book.find(who->GetFormID());
            if (it == m_book.end()) {
                Bonds::Bond nothing;   // nobody is put in the book for being asked about
                return Bonds::See(nothing, theirs);
            }
            const bool was = it->second.handed;
            view = Bonds::See(it->second, theirs);
            handedNow = it->second.handed && !was;
            after = it->second;
        }
        if (handedNow) {
            const char* raw = who->GetDisplayFullName();
            logger::info("Bonds: {} is SkyrimNet Relationships' to speak for from now (its tier {}{}); what was read here before ({}) no longer "
                         "stands in for it",
                         raw && *raw ? raw : "someone", theirs.tier, theirs.ill ? std::format(", ill will {}", theirs.ill) : std::string(), Bonds::Word(after));
        }
        return view;
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
        std::string out = "not read";
        if (Bonds::Judged(b)) {
            out = Bonds::Word(b);
            if (b.drawn) {
                out += ", drawn to the player";
                out += b.answer > 0 ? " and welcome" : b.answer < 0 ? ", turned down" : ", unanswered";
            }
            out += std::format(" (read at day {:.1f}, {} times together)", b.judgedAt, b.eventsAt);
        }
        // And whose word counts for them (the rule of Bonds::See, without touching its notes).
        const Bonds::Theirs theirs = TheirsOf(RE::TESForm::LookupByID<RE::Actor>(actor));
        if (!theirs.installed) return out + "; no relationship mod: all of it counts";
        if (!theirs.tracked) return out + "; SkyrimNet Relationships does not watch them: friendship and ill will count as read, nothing of the heart";
        out += std::format("; SkyrimNet Relationships watches them (tier {}, ill will {}): ", theirs.tier, theirs.ill);
        Bonds::Bond copy = b;
        const Bonds::View view = Bonds::See(copy, theirs);
        if (!Bonds::Judged(b)) return out + "its word alone";
        if (copy.handed) return out + "its word alone, it has caught up with what was read here";
        return out + std::format("what was read here stays as a floor until it has caught up (counts as {}{})",
                                 view.sour == Bonds::Foe ? "a foe" : view.sour == Bonds::Cold ? "cold" : view.standing >= Bonds::Confidant ? "a confidant"
                                 : view.standing == Bonds::Friend ? "a friend" : view.standing == Bonds::Acquaintance ? "an acquaintance" : "a stranger",
                                 theirs.tier > 0 ? ", its own tier where that is higher" : "");
    }

    void BondBridge::Tick() {
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
                // Whoever a relationship mod watches is its to speak for: found so a while ago, not asked about yet.
                if (const auto theirs = m_theirsUntil.find(id); theirs != m_theirsUntil.end() && real < theirs->second) continue;
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
            if (TheirsOf(actor).tracked) {
                // A relationship mod watches them: not read here. (Seen keeps the notes of the seam for whoever was
                // read before that mod took them on.)
                m_theirsUntil[c.id] = real + kTheirsAgainSeconds;
                Seen(actor);
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
        // Beside a relationship mod nothing of the heart is asked: that is its to begin and to keep.
        const bool heartAsked = !TheirsOf(who).installed;
        logger::info("Bonds: reading what {} ({:08X}) remembers of the player ({} times together{}{})", name, id, events,
                     heartAsked ? "" : "; friendship and ill will only, the heart is SkyrimNet Relationships' to keep", forced ? "; asked for by a test" : "");
        AsyncDispatch::Submit([this, id, name, player, events, now, forced, heartAsked]() mutable {
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
            const auto done = [this, id, events, now, count, heartAsked, hasTalk = !talk.empty()](const std::string& standing, const std::string& heart,
                                                                                                 const std::string& answer, float sure,
                                                                                                 const std::string& failure) {
                if (auto* task = SKSE::GetTaskInterface()) {
                    task->AddTask([=, this]() { Finish(id, standing, heart, answer, sure, events, now, count, hasTalk, heartAsked, failure); });
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
            ctx["ask_heart"] = heartAsked;
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
                [done, byChat, heartAsked](const char*, const char* resultJson, int success) {
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
                    // Sure of it: of the standing and of the heart, or of the standing alone where the heart was not asked.
                    done(standing, heart, answer, heartAsked ? (std::min)(sureOfStanding, sureOfHeart) : sureOfStanding, {});
                });
            if (!queued) byChat();
        });
        return true;
    }

    void BondBridge::Finish(std::uint32_t actor, const std::string& standing, const std::string& heart, const std::string& answer, float sure,
                            int events, float nowDays, int memories, bool talk, bool heartAsked, const std::string& failure) {
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
            if (failure.empty()) read = Bonds::Read(lower(standing), lower(heart), lower(answer), sure, bond, events, nowDays, heartAsked);
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
        if (version < 1 || version > Bonds::kRecordVersion || length > (1u << 17)) {
            logger::warn("Bonds: skipping a co-save record of version {} ({} bytes)", version, length);
            return;
        }
        std::vector<std::uint8_t> bytes(length);
        if (length > 0 && intfc->ReadRecordData(bytes.data(), length) != length) {
            logger::warn("Bonds: the co-save record is short: everybody is read anew");
            return;
        }
        Bonds::Book saved;
        if (!Bonds::Deserialize(bytes, saved, version)) {
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
        m_theirsUntil.clear();
        m_lastRead = -1.0e9;
        m_lastCount = -1.0e9;
    }

}
