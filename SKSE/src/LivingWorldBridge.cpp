#include "LivingWorldBridge.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string_view>

using namespace std::string_view_literals;

namespace IntelEngine {

    namespace {
        bool LocationHas(const RE::BGSLocation* location, std::initializer_list<std::string_view> keywords) {
            for (auto* loc = location; loc; loc = loc->parentLoc) {
                for (const auto editorID : keywords) {
                    if (loc->HasKeywordString(editorID)) return true;
                }
            }
            return false;
        }

        LivingWorld::PlaceKind KindOf(const RE::BGSLocation* location) {
            LivingWorld::PlaceKind kind;
            if (!location) return kind;
            // Only the place itself counts as a dungeon or an inn; a town is found through the parents too, so the
            // Bannered Mare is in a town, and a dungeon inside a hold is not a town.
            kind.dungeon = location->HasKeywordString("LocTypeDungeon") || location->HasKeywordString("LocTypeCave") ||
                           location->HasKeywordString("LocTypeDraugrCrypt") || location->HasKeywordString("LocTypeDwarvenAutomatons") ||
                           location->HasKeywordString("LocTypeBanditCamp") || location->HasKeywordString("LocTypeVampireLair") ||
                           location->HasKeywordString("LocTypeWarlockLair") || location->HasKeywordString("LocTypeFalmerHive") ||
                           location->HasKeywordString("LocTypeAnimalDen") || location->HasKeywordString("LocTypeHagravenNest") ||
                           location->HasKeywordString("LocTypeForswornCamp") || location->HasKeywordString("LocTypeDragonLair");
            kind.inn = location->HasKeywordString("LocTypeInn");
            kind.playerHouse = location->HasKeywordString("LocTypePlayerHouse");
            kind.town = !kind.dungeon && LocationHas(location, {"LocTypeCity", "LocTypeTown", "LocTypeSettlement"});
            return kind;
        }

        // A bad moment for an ordinary visit: a fight, a conversation, a scene, a menu over the game.
        bool BadMoment() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && player->IsInCombat()) return true;
            if (auto* ui = RE::UI::GetSingleton()) {
                if (ui->GameIsPaused()) return true;
                for (const auto menu : {"Dialogue Menu"sv, "OstimSceneMenu"sv, "Sleep/Wait Menu"sv, "BarterMenu"sv}) {
                    if (ui->IsMenuOpen(menu)) return true;
                }
            }
            return false;
        }

        std::uint8_t KindId(const std::string& storyType) {
            std::uint32_t h = 2166136261u;
            for (const char c : storyType) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
            return static_cast<std::uint8_t>(h % 250u + 1u);
        }

        bool IsBig(const std::string& storyType) {
            return storyType == "quest" || storyType == "ambush" || storyType.starts_with("faction_");
        }
    }

    void LivingWorldBridge::Register() {
        if (m_registered) return;
        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            holder->AddEventSink<RE::TESActorLocationChangeEvent>(this);
            holder->AddEventSink<RE::TESSleepStopEvent>(this);
            m_registered = true;
            logger::info("LivingWorld: listening for arrivals and waking up");
        }
    }

    LivingWorld::Settings LivingWorldBridge::GetSettings() const {
        std::scoped_lock lock(m_mutex);
        return m_settings;
    }

    void LivingWorldBridge::SetSettings(const LivingWorld::Settings& settings) {
        std::scoped_lock lock(m_mutex);
        m_settings = settings;
        logger::info("LivingWorld: level {}, interruptions {}, quest NPCs {}, pacing {}, wait {:.0f} days, own life after {:.0f} days",
                     static_cast<int>(settings.level), static_cast<int>(settings.interruptions),
                     static_cast<int>(settings.questNpcMoves), static_cast<int>(settings.pacing), settings.waitOrderDays,
                     settings.lifeStartDays);
    }

    float LivingWorldBridge::NowDays() {
        auto* calendar = RE::Calendar::GetSingleton();
        return calendar ? calendar->GetCurrentGameTime() : 0.0f;
    }

    LivingWorld::Life& LivingWorldBridge::Touch(RE::FormID actor) {
        for (auto& life : m_lives) {
            if (life.actor == actor) return life;
        }
        if (m_lives.size() >= LivingWorld::kMaxLives) LivingWorld::Trim(m_lives, LivingWorld::kMaxLives - 1);
        LivingWorld::Life life;
        life.actor = actor;
        life.lastMet = NowDays();
        m_lives.push_back(life);
        return m_lives.back();
    }

    bool LivingWorldBridge::MayDeliver(int seam, bool urgent) {
        const auto settings = GetSettings();
        if (settings.level == LivingWorld::WorldLevel::Classic) return true;  // today's behaviour, untouched
        if (seam < 0 || seam > static_cast<int>(LivingWorld::Seam::InCombat)) seam = 0;
        LivingWorld::Situation at;
        at.seam = static_cast<LivingWorld::Seam>(seam);
        at.now = NowDays();
        at.badMoment = BadMoment();
        std::scoped_lock lock(m_mutex);
        LivingWorld::Refill(m_state, settings, at.now);
        const auto seed = static_cast<std::uint32_t>(at.now * 1440.0f);
        const bool may = LivingWorld::MayDeliverNow(m_state, settings, at, urgent, false, seed);
        logger::info("LivingWorld: seam {} at day {:.2f}, budget {:.2f}, bad moment {}: {}", seam, at.now, m_state.budget,
                     at.badMoment, may ? "a moment may land" : "held back");
        return may;
    }

    float LivingWorldBridge::ScoreStoryCandidate(RE::FormID actor, int interactionCount, float daysSinceLastMet, bool sameHold,
                                                 bool friendOfFriend, float nowDays, std::uint32_t seed) {
        std::scoped_lock lock(m_mutex);
        auto& life = Touch(actor);
        // Bond from how much they and the player have shared (SkyrimNet's history); the relationship mod adds to
        // it in a later step. Somebody never met keeps lastMet at now, so they never count as a forgotten face.
        life.bond = std::clamp(std::sqrt(static_cast<float>(std::max(0, interactionCount))) / 6.0f, 0.0f, 1.0f);
        if (interactionCount > 0 && daysSinceLastMet >= 0.0f) life.lastMet = nowDays - daysSinceLastMet;

        LivingWorld::Candidate c;
        c.life = &life;
        c.onRoute = sameHold ? 0.7f : 0.0f;
        c.unresolved = false;  // open business arrives with the storylines of phase 1
        LivingWorld::Situation at;
        at.now = nowDays;
        float score = LivingWorld::Score(c, m_state, at, seed);
        if (friendOfFriend) score += 0.3f;
        const bool rotationTurn =
            m_state.picks % LivingWorld::kRotationEvery == LivingWorld::kRotationEvery - 1;
        if (rotationTurn && LivingWorld::IsForgottenAcquaintance(life, nowDays)) score += 5.0f;
        return score;
    }

    void LivingWorldBridge::OnStoryDispatched(RE::FormID actor, const std::string& storyType) {
        if (actor == 0 || storyType.empty() || IsClassic()) return;
        std::scoped_lock lock(m_mutex);
        auto& life = Touch(actor);
        LivingWorld::Candidate c;
        c.life = &life;
        c.kind = KindId(storyType);
        c.big = IsBig(storyType);
        LivingWorld::Situation at;
        at.now = NowDays();
        LivingWorld::Commit(m_state, life, c, at);
        logger::info("LivingWorld: moment {} with {:08X} counted (moments so far {}, budget {:.2f})", storyType, actor,
                     m_state.picks, m_state.budget);
    }

    void LivingWorldBridge::SendSeam(LivingWorld::Seam seam) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        task->AddTask([seam]() {
            auto* source = SKSE::GetModCallbackEventSource();
            if (!source) return;
            SKSE::ModCallbackEvent event{};
            event.eventName = "IntelEngine_Seam";
            event.strArg = "";
            event.numArg = static_cast<float>(seam);
            event.sender = nullptr;
            source->SendEvent(&event);
        });
    }

    RE::BSEventNotifyControl LivingWorldBridge::ProcessEvent(const RE::TESActorLocationChangeEvent* event,
                                                             RE::BSTEventSource<RE::TESActorLocationChangeEvent>*) {
        if (!event || !event->actor || !event->actor->IsPlayerRef() || IsClassic()) return RE::BSEventNotifyControl::kContinue;
        const auto seam = LivingWorld::ClassifySeam(KindOf(event->oldLoc), KindOf(event->newLoc));
        if (seam == LivingWorld::Seam::InDungeon) return RE::BSEventNotifyControl::kContinue;
        if (seam == LivingWorld::Seam::OnRoad) {
            // Wandering between named wilderness places: a look at most every two game hours.
            const float now = NowDays();
            std::scoped_lock lock(m_mutex);
            if (now - m_lastRoadSeamDay < 2.0f / 24.0f) return RE::BSEventNotifyControl::kContinue;
            m_lastRoadSeamDay = now;
        }
        SendSeam(seam);
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl LivingWorldBridge::ProcessEvent(const RE::TESSleepStopEvent* event,
                                                             RE::BSTEventSource<RE::TESSleepStopEvent>*) {
        if (!event || event->interrupted || IsClassic()) return RE::BSEventNotifyControl::kContinue;
        SendSeam(LivingWorld::Seam::Woke);
        return RE::BSEventNotifyControl::kContinue;
    }

    void LivingWorldBridge::Save(SKSE::SerializationInterface* intfc) {
        std::vector<std::uint8_t> bytes;
        {
            std::scoped_lock lock(m_mutex);
            bytes = LivingWorld::Serialize(m_lives, m_state);
        }
        if (!intfc->OpenRecord(LivingWorld::kRecordType, LivingWorld::kRecordVersion)) {
            logger::error("LivingWorld: could not open the co-save record");
            return;
        }
        intfc->WriteRecordData(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    }

    void LivingWorldBridge::Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length) {
        if (version != LivingWorld::kRecordVersion || length > (1u << 20)) {
            logger::warn("LivingWorld: skipping a co-save record of version {} ({} bytes)", version, length);
            return;
        }
        std::vector<std::uint8_t> bytes(length);
        if (length > 0 && intfc->ReadRecordData(bytes.data(), length) != length) {
            logger::warn("LivingWorld: the co-save record is short; starting with an empty record");
            return;
        }
        std::vector<LivingWorld::Life> lives;
        LivingWorld::DirectorState state;
        if (!LivingWorld::Deserialize(bytes, lives, state)) {
            logger::warn("LivingWorld: the co-save record is damaged; starting with an empty record");
            return;
        }
        // Form IDs of NPCs from light or regular plugins can shift between loads; keep only those still there.
        const auto resolve = [intfc](std::uint32_t& formID) {
            if (formID == 0) return true;
            RE::FormID resolved = 0;
            if (!intfc->ResolveFormID(formID, resolved)) {
                formID = 0;
                return false;
            }
            formID = resolved;
            return true;
        };
        std::erase_if(lives, [&](LivingWorld::Life& life) {
            if (!resolve(life.actor) || life.actor == 0) return true;
            resolve(life.parkedAt);
            resolve(life.place);
            resolve(life.companion);
            return false;
        });
        resolve(state.lastActor);
        std::scoped_lock lock(m_mutex);
        m_lives = std::move(lives);
        m_state = state;
        logger::info("LivingWorld: loaded {} lives, {} moments so far", m_lives.size(), m_state.picks);
    }

    void LivingWorldBridge::Revert() {
        std::scoped_lock lock(m_mutex);
        m_lives.clear();
        m_state = LivingWorld::DirectorState{};
        m_lastRoadSeamDay = -1.0e6f;
    }
}
