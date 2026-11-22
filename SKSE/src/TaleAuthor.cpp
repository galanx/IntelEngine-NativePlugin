// Tales (docs/STORY_ARCS.md section 0): the dungeon master writes a story for the real people of the player's town;
// Tales.h checks it and builds the arc; the arc engine runs it. Here: who may be cast and what is said of them, the
// request and its answer, and the judge who reads a talk and names which prepared outcome it turned into.

#include "ArcBridge.h"
#include "LanguageBridge.h"
#include "FactionPolitics.h"
#include "RumorBridge.h"
#include "Safe.h"

#include "AsyncDispatch.h"
#include "BondBridge.h"
#include "DialogueTracker.h"
#include "FrameSpread.h"
#include "HeartBridge.h"
#include "ItemIndex.h"
#include "ItemNames.h"
#include "LivingWorldBridge.h"
#include "NPCIndex.h"
#include "MemoryDB.h"
#include "QuestBridge.h"
#include "SkyrimNetAPI.h"
#include "SlotTracker.h"
#include "SpeechTracker.h"
#include "Tales.h"

#include <chrono>
#include <map>
#include <optional>
#include <thread>

#include <nlohmann/json.hpp>

namespace IntelEngine {

    namespace {
        namespace A = Arcs;

        constexpr float kQuietSeconds = 6.0f;      // a talk is read once both have been quiet this long
        constexpr float kFarRoadDays = 8.0f / 24.0f;   // someone coming from another hold is on the road this long
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

        // True when the player has begun this quest of the game: one of its objectives has been in their journal.
        // (A quest that merely runs says nothing: the game starts many of its quests with the world, long before
        // the player hears of them: "A Lovely Letter" and "The Golden Claw" both run from the first day.)
        bool Begun(const RE::TESQuest* quest) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || !quest) return false;
            for (auto& instance : player->GetPlayerRuntimeData().objectives) {
                if (instance.Objective && instance.Objective->ownerQuest == quest) return true;
            }
            return false;
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
                std::string goal = objective->displayText.c_str() ? objective->displayText.c_str() : "";
                // "Talk to <Alias=RiverwoodFriend> in Riverwood": the journal's tags name nobody outside the journal.
                for (auto open = goal.find('<'); open != std::string::npos; open = goal.find('<')) {
                    const auto close = goal.find('>', open);
                    if (close == std::string::npos) break;
                    goal.replace(open, close - open + 1, "someone");
                }
                if (!goal.empty()) text += ": " + goal;
                out.push_back(text);
                if (out.size() >= 8) break;
            }
            return out;
        }

        // Things a story may turn on: the town's plain jewellery and a few small valuables and tools. Rings and the
        // like are hidden at home only (an NPC may put them on); the rest may sit in a pocket.
        std::vector<Tales::Item> ItemChoices() {
            // The load order's items do not change in a session: looked up once.
            static std::vector<Tales::Item> cached;
            if (!cached.empty()) return cached;
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
            cached = out;
            return out;
        }

        // What lives in a place, read from its location's keywords (any language, any modlist that keeps them).
        std::uint8_t SiteKindOf(RE::BGSLocation* location) {
            // Only what the game itself treats as a place to fight one's way through. A town's working mine carries
            // the mine keyword and nothing else: Quicksilver Mine in Dawnstar and Left Hand Mine, with their miners
            // at work, were on offer as "bandit hideouts", their heart a miner called Lond (seen 2026-11-19). A
            // story would have put a bandit chief among them.
            if (!location || !(location->HasKeywordString("LocTypeDungeon") || location->HasKeywordString("LocTypeClearable"))) {
                return Tales::SiteNone;
            }
            int depth = 0;
            for (auto* loc = location; loc && depth < 2; loc = loc->parentLoc, ++depth) {
                if (loc->HasKeywordString("LocTypeDragonLair")) return Tales::SiteNone;   // dragons: not yet
                if (loc->HasKeywordString("LocTypeDwarvenAutomatons")) return Tales::SiteDwemer;
                if (loc->HasKeywordString("LocTypeFalmerHive")) return Tales::SiteFalmer;
                if (loc->HasKeywordString("LocTypeDraugrCrypt")) return Tales::SiteCrypt;
                if (loc->HasKeywordString("LocTypeVampireLair")) return Tales::SiteVampires;
                if (loc->HasKeywordString("LocTypeWarlockLair")) return Tales::SiteNecromancers;
                if (loc->HasKeywordString("LocTypeForswornCamp")) return Tales::SiteForsworn;
                if (loc->HasKeywordString("LocTypeHagravenNest")) return Tales::SiteHagraven;
                if (loc->HasKeywordString("LocTypeGiantCamp")) return Tales::SiteGiants;
                if (loc->HasKeywordString("LocTypeAnimalDen")) return Tales::SiteDen;
                if (loc->HasKeywordString("LocTypeBanditCamp") || loc->HasKeywordString("LocTypeMilitaryFort") ||
                    loc->HasKeywordString("LocTypeMine")) {
                    return Tales::SiteBandits;
                }
            }
            return Tales::SiteNone;
        }
        const char* SiteWhat(std::uint8_t kind) {
            switch (kind) {
                case Tales::SiteDwemer: return "a Dwemer ruin";
                case Tales::SiteFalmer: return "a Falmer hive";
                case Tales::SiteCrypt: return "an old Nordic tomb";
                case Tales::SiteBandits: return "a bandit hideout";
                case Tales::SiteDen: return "a cave, a beast's den";
                case Tales::SiteVampires: return "a vampire lair";
                case Tales::SiteNecromancers: return "a necromancer's lair";
                case Tales::SiteForsworn: return "a Forsworn camp";
                case Tales::SiteHagraven: return "a hagraven's nest";
                case Tales::SiteGiants: return "a giant's camp";
                default: return "a place";
            }
        }
        const char* SiteHeart(std::uint8_t kind) {
            switch (kind) {
                case Tales::SiteDwemer: return "a Dwarven centurion";
                case Tales::SiteFalmer: return "a Falmer chief";
                case Tales::SiteCrypt: return "a draugr overlord";
                case Tales::SiteBandits: return "a bandit chief";
                case Tales::SiteDen: return "a giant frostbite spider or a troll";
                case Tales::SiteVampires: return "a vampire";
                case Tales::SiteNecromancers: return "a necromancer";
                case Tales::SiteForsworn: return "a Forsworn shaman";
                case Tales::SiteHagraven: return "a hagraven";
                case Tales::SiteGiants: return "a giant";
                default: return "something";
            }
        }
        // What the place's kind leaves on those it kills: facts for the writer, so that what the player finds on a
        // victim is true of what waits inside (a mystery that can be worked out, never one made up afterwards).
        // Nothing the eye would contradict: the body that lies there is whole and clothed (the game's own dead), so
        // these are wounds under the clothes, colour, smell and what clings to it, never a limb torn off (the first
        // hunt the model wrote said so of a body that lay there in one piece, 2026-11-19).
        const char* SiteSigns(std::uint8_t kind, bool spider) {
            switch (kind) {
                case Tales::SiteDwemer: return "burns and clean deep cuts as from hot blades, a smell of oil and scorched metal, nothing eaten, nothing taken";
                case Tales::SiteFalmer: return "small black arrowheads of chitin broken off in the flesh, wounds gone dark with poison, drag marks toward a hole in the ground, taken at night";
                case Tales::SiteCrypt: return "hewn with old heavy blades, the wounds rimed with frost, grave dust on the clothes, nothing eaten, nothing taken";
                case Tales::SiteBandits: return "cut down with plain steel or shot with common arrows, purse and boots gone, no bite marks";
                case Tales::SiteDen:
                    return spider ? "two great puncture wounds gone black with venom, the flesh shrunken as if sucked dry, shreds of thick web clinging to the clothes"
                                  : "great bite wounds, the ribs stove in, claw marks three fingers wide, a rank animal stink";
                case Tales::SiteVampires: return "two small punctures at the throat, the body pale and bloodless, hardly any blood on the ground, taken at night";
                case Tales::SiteNecromancers: return "no wound to speak of, the skin grey and cold as if long dead, ritual marks cut into the chest, black candle wax nearby";
                case Tales::SiteForsworn: return "hacked with weapons of bone and flint, ritual cuts under the ribs, a charm of briar and feathers left on the chest";
                case Tales::SiteHagraven: return "raked open by long talons, black feathers stuck in the blood, a stink of rot, bones and twigs bound into a fetish nearby";
                case Tales::SiteGiants: return "every bone on one side broken as by a single great blow, thrown a long way from the road, footprints a stride long, nothing eaten";
                default: return "";
            }
        }

        // Where the player's journal points now: no story may send the player into those places.
        std::unordered_set<RE::FormID> ObjectiveLocations() {
            std::unordered_set<RE::FormID> out;
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return out;
            for (auto& instance : player->GetPlayerRuntimeData().objectives) {
                auto* objective = instance.Objective;
                auto* quest = objective ? objective->ownerQuest : nullptr;
                if (!quest || instance.InstanceState != RE::QUEST_OBJECTIVE_STATE::kDisplayed || !objective->targets) continue;
                for (std::uint32_t i = 0; i < objective->numTargets; ++i) {
                    auto* target = objective->targets[i];
                    if (!target) continue;
                    auto ref = quest->GetAliasedRef(target->alias).get();
                    auto* where = ref ? ref->GetCurrentLocation() : nullptr;
                    int depth = 0;
                    for (auto* loc = where; loc && depth < 4; loc = loc->parentLoc, ++depth) {
                        if (loc->HasKeywordString("LocTypeHold")) break;
                        out.insert(loc->GetFormID());
                    }
                }
            }
            return out;
        }

        // What the author is asked for, with the town, its people and what came before.
        struct Request {
            std::string context;           // the JSON the prompt renders
            Tales::Pool pool;
            RE::FormID asker = 0;
            RE::FormID town = 0;
            std::uint32_t seed = 0;        // the story's own number, fixed before it is written (which master a den has)
            bool rumored = false;          // it begins as talk in town: nobody comes to the player with it
            bool afar = false;             // the director's: the asker comes to the player wherever the player is
            bool otherHold = false;        // ... from another hold
            bool heart = false;            // a story of the heart between the asker and the player (Hearts.h)
            std::uint32_t sequelOf = 0;    // the earlier tale whose open end this one takes up, 0 = none
            std::vector<RE::FormID> bios;  // whose bio summaries the worker adds (file reads stay off the game thread)
        };
        std::shared_ptr<Request> g_request;   // the tale being written (one at a time)
        float g_requestAt = -1.0f;

        constexpr std::array<const char*, 16> kGenres = {"crime",     "romance",    "family",  "mystery", "politics", "intrigue", "rivalry",
                                                         "redemption", "debt",      "a secret", "faith",  "adventure", "grief",  "jealousy",
                                                         "the uncanny", "a hunt"};
    }

    // ------------------------------------------------------------------ the request

    bool ArcBridge::RequestTale(RE::Actor* asker, const std::string& written, const TaleSeed* taleSeed) {
        // Whether a tale is on its way is known before the gathering gives its first frame back: the answer is
        // written while this call still stands (GatherTale lets go of it at its first pause).
        bool onItsWay = false;
        GatherTale(asker, written, taleSeed ? std::optional<TaleSeed>(*taleSeed) : std::nullopt, &onItsWay);
        return onItsWay;
    }

// A pause of the gathering: when this frame's share has passed the limit, the rest waits for the next frame. A save
// loaded in between ends the job (the people and places in hand were another game's).
#define IE_TALE_PAUSE(limit)                                                                                          \
    if (spread && share.Ms() > (limit)) {                                                                             \
        share.End();                                                                                                  \
        co_await NextFrame{};                                                                                         \
        if (SlotTracker::GetSingleton()->loadGeneration.load(std::memory_order_relaxed) != generation) {              \
            logger::info("Tales: a save was loaded while a tale's request was gathered: dropped");                    \
            m_taleBusy.store(false);                                                                                  \
            co_return;                                                                                                \
        }                                                                                                             \
        share.Begin();                                                                                                \
    }

    FrameJob ArcBridge::GatherTale(RE::Actor* asker, std::string written, std::optional<TaleSeed> seedOwned, bool* answer) {
        const TaleSeed* taleSeed = seedOwned ? &*seedOwned : nullptr;
        const bool afar = taleSeed && taleSeed->fromAfar;
        // `written`: a tale given as JSON (test hook ArcDebugTaleFile) instead of one asked of the model.
        if (!asker || (written.empty() && !SkyrimNetAPI::SendCustomPromptToLLM)) co_return;
        if (written.empty() && NPCIndex::BlockedForStories(asker)) {
            logger::info("Tales: no tale for {}: kept out of stories by the user's lists", asker->GetDisplayFullName());
            co_return;
        }
        const float real = RealNow();
        const auto started = std::chrono::steady_clock::now();
        if (m_taleBusy.load()) {
            if (g_requestAt >= 0.0f && real - g_requestAt > kWriteTimeout) {
                logger::warn("Tales: the tale asked for {:.0f} s ago never came; asking anew", real - g_requestAt);
                m_taleBusy.store(false);
            } else {
                logger::info("Tales: no tale for {}: another tale is being written", asker->GetDisplayFullName());
                co_return;
            }
        }
        std::string why;
        if (!Usable(asker, 0, why)) {
            logger::info("Tales: no tale for {}: {}", asker->GetDisplayFullName(), why);
            co_return;
        }
        const Folk* me = FolkOf(asker->GetFormID());
        Folk away;   // someone the director chose who has no home in a town (a former follower, a wanderer)
        if (!me && afar) {
            auto* where = TownOfLoc(asker->GetCurrentLocation());
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!where && player) where = TownOfLoc(player->GetCurrentLocation());
            if (where) {
                away.ref = asker->GetFormID();
                away.base = asker->GetActorBase() ? asker->GetActorBase()->GetFormID() : 0;
                away.town = where->GetFormID();
                me = &away;
            }
        }
        if (!me) {
            logger::info("Tales: no tale for {}: not one of the townsfolk with a town", asker->GetDisplayFullName());
            co_return;
        }
        // A town's story comes to a player who is in that town, from someone who has not asked for anything lately. One
        // the director chose for a reason of their own comes to the player wherever the player is.
        if (afar) {
            // The player's own setting for who may cross holds to seek them out holds here too (the offer travels as
            // such a visit): asked before anything is written. Lod of Falkreath had a whole story written and was
            // then turned back on the road by it (2026-11-19).
            {
                auto* index = NPCIndex::GetSingleton();
                auto* pc = RE::PlayerCharacter::GetSingleton();
                const std::string playerHold = pc ? NPCIndex::GetNPCHoldName(pc) : std::string();
                if (index && !NPCIndex::PassesHoldRestriction(asker, playerHold, index->GetHoldRestrictionPolicy("seek_player"))) {
                    logger::info("Tales: no tale for {}: the settings do not let them cross holds to seek the player out", asker->GetDisplayFullName());
                    co_return;
                }
            }
            if (written.empty() && AskedWithin(asker->GetFormID(), 7.0f)) {
                logger::info("Tales: no tale for {}: they asked the player for something lately", asker->GetDisplayFullName());
                co_return;
            }
        } else {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            auto* here = player ? TownOfLoc(cell && cell->GetLocation() ? cell->GetLocation() : player->GetCurrentLocation()) : nullptr;
            if (!here || here->GetFormID() != me->town) {
                logger::info("Tales: no tale for {}: the player is not in their town", asker->GetDisplayFullName());
                co_return;
            }
            if (written.empty() && AskedWithin(asker->GetFormID(), 7.0f)) {
                logger::info("Tales: no tale for {}: they asked the player for something lately", asker->GetDisplayFullName());
                co_return;
            }
        }
        std::vector<std::string> told;
        std::vector<std::string> recentGenres;
        std::vector<std::uint32_t> recentBeats;   // the step kinds (bit per kind) of the stories the player took on, newest first
        bool recentHeart = false;                 // one of the last two stories, or one still open, was of the heart
        std::vector<RE::FormID> heartCast;        // everybody who has been part of a story of the heart, ever
        std::string admirerKey, admirerName;      // somebody close to the asker who is drawn to the player and has not said so
        std::vector<RE::FormID> restedPlaces;     // where the last stories led: those places rest a while
        std::vector<std::string> toldTitles;      // the titles of the last tales: a new one shares no word with them
        std::size_t talesSoFar = 0;               // tales ever planned in this game: which beginning the next one gets
        RE::FormID lastPlace = 0;                 // where the last story that led out into the world went
        int heartLooked = 0;
        // An open end that has come due (4.0): the earlier tale, how it ended, what that left open, who it was about.
        std::uint32_t dueId = 0;
        std::string dueTitle, dueEnded, dueOpen, dueWhen, duePeople;
        std::vector<RE::FormID> dueCast;
        {
            std::scoped_lock lock(m_mutex);
            if (m_book.Count(A::Phase::Planned) > 0 || m_book.Count(A::Phase::Open) >= A::kMaxOpen) {
                logger::info("Tales: no tale for {}: another story is on its way or two are open", asker->GetDisplayFullName());
                co_return;
            }
            if (const float last = m_book.LastBeat(), now = NowDays(); !A::QuietFor(last, now, A::kQuietHours)) {
                logger::info("Tales: no tale for {}: a story came to the player or ended {:.1f} hours ago (quiet for {:.0f})",
                             asker->GetDisplayFullName(), (now - last) * 24.0f, A::kQuietHours);
                co_return;
            }
            // Where the last four stories led out into the world: not there again so soon (the user, 2026-11-20:
            // "what would happen if you clear the broken fang cave but another quest makes you go to the same place
            // later"). The cave just emptied, with a new master in it a day later, is the dungeon master repeating
            // itself; and a master a story left alive there would get a second one beside it.
            {
                int looked = 0;
                for (auto it = m_book.all.rbegin(); it != m_book.all.rend() && looked < 4; ++it) {
                    if (it->ending == A::Ending::Lapsed) continue;
                    ++looked;
                    for (const auto site : it->site) {
                        if (site) restedPlaces.push_back(site);
                        if (site && !lastPlace) lastPlace = site;
                    }
                }
            }
            // Whoever has had a story of the heart already, their own or one told for them, gets no admirer's story.
            for (const auto& arc : m_book.all) {
                if (!(arc.flags & A::Flag::Heart)) continue;
                for (const auto id : arc.cast) {
                    if (id) heartCast.push_back(id);
                }
            }
            for (const auto& arc : m_book.all) {
                if (arc.pattern == A::Pattern::Tale) ++talesSoFar;
            }
            // What was told already (newest first): never again.
            for (auto it = m_book.all.rbegin(); it != m_book.all.rend() && told.size() < 10; ++it) {
                if (it->ending == A::Ending::Lapsed) continue;
                if ((heartLooked++ < 2 || A::IsOpen(*it)) && (it->flags & A::Flag::Heart)) recentHeart = true;
                if (it->phase != A::Phase::Planned && it->ending != A::Ending::Declined) {
                    std::uint32_t beats = 0;
                    for (const auto& n : A::NodesOf(*it)) beats |= 1u << static_cast<unsigned>(n.kind);
                    recentBeats.push_back(beats);
                }
                if (it->pattern == A::Pattern::Tale && toldTitles.size() < 6 && !it->title.empty()) toldTitles.push_back(it->title);
                if (it->pattern == A::Pattern::Tale) {
                    // With how it ended and what that left open (4.0). The writer used to see the title and the
                    // premise only: whether the thief was handed over or let go, whether the innocent was named,
                    // was nowhere, so a later story could not build on it and now and then contradicted it.
                    std::string line = it->genre + ": " + it->title + " - " + it->premise;
                    if (it->phase == A::Phase::Ended) {
                        const int k = A::TaleEndIndex(it->ending);
                        if (k >= 0) {
                            const auto& end = it->ends[static_cast<std::size_t>(k)];
                            if (!end.journal.empty()) line += " HOW IT ENDED (" + PlayerName() + "'s journal): " + Fill(end.journal, *it);
                            if (!end.after.empty()) line += " LEFT OPEN: " + Fill(end.after, *it);
                        } else if (it->ending == A::Ending::Declined) {
                            line += " HOW IT ENDED: " + PlayerName() + " would not take it on.";
                        } else if (it->ending == A::Ending::Ignored) {
                            line += " HOW IT ENDED: " + PlayerName() + " took it on and never saw it through.";
                        }
                    } else if (A::IsOpen(*it)) {
                        line += " (still going on)";
                    }
                    told.push_back(std::move(line));
                    recentGenres.push_back(it->genre);
                } else {
                    told.push_back(A::DefaultTitle(it->pattern) + " (" + (it->pattern == A::Pattern::Thief ? "a theft in town" : it->pattern == A::Pattern::Feud ? "a quarrel settled" : "a murder solved") + ")");
                    recentGenres.push_back(it->pattern == A::Pattern::Thief ? "crime" : it->pattern == A::Pattern::Feud ? "rivalry" : "mystery");
                }
            }
        }

        // ---- an open end that has come due (4.0). The newest tale of this town that ended, two game days ago or
        // more, in a way its writer said leaves something open, and that no later tale has taken up. Not when the
        // newest tale that was told at all was itself such a follow-up: never two running, so a town's stories do
        // not become one endless thread. A story asked for by kind (a hunt, a haunting, a rescue, one of the heart,
        // one whose asker comes from afar with a reason of their own) is that kind and nothing else.
        if (!afar && written.empty() && !(taleSeed && !taleSeed->kind.empty())) {
            std::scoped_lock lock(m_mutex);
            const float now = NowDays();
            bool lastWasSequel = false;
            for (auto it = m_book.all.rbegin(); it != m_book.all.rend(); ++it) {
                if (it->ending == A::Ending::Lapsed || it->pattern != A::Pattern::Tale) continue;
                lastWasSequel = it->sequelOf != 0;
                break;
            }
            for (auto it = m_book.all.rbegin(); it != m_book.all.rend() && !dueId && !lastWasSequel; ++it) {
                if (it->pattern != A::Pattern::Tale || it->phase != A::Phase::Ended || it->followed || it->place != me->town) continue;
                if (it->flags & A::Flag::Heart) continue;   // what stands between two people is theirs, and the heart has its own stories
                const int k = A::TaleEndIndex(it->ending);
                if (k < 0) continue;
                const auto& end = it->ends[static_cast<std::size_t>(k)];
                if (end.after.empty() || now - it->since < 2.0f) continue;
                dueId = it->id;
                dueTitle = it->title;
                dueEnded = Fill(end.journal, *it);
                dueOpen = Fill(end.after, *it);
                dueWhen = now - it->since < 4.0f ? "a few days ago" : now - it->since < 10.0f ? "about a week ago" : "some time ago";
                for (std::size_t r = 1; r < A::kRoles; ++r) {
                    if (!it->cast[r]) continue;
                    dueCast.push_back(it->cast[r]);
                    duePeople += (duePeople.empty() ? "" : ", ") + NameOf(it->cast[r]);
                }
            }
        }

        // ---- the first pause. Everything above is quick and says whether a tale can be asked for at all. What
        // follows (the people, the places, the words for the writer) reads the game and took 0.74 to 0.85 ms of one
        // frame in a small town, 2.4 ms on a large mod list: it is done a part a frame (FrameSpread.h), never in the
        // frame of the look that asked for it. From here a tale is on its way as far as the caller is concerned; a
        // failure further down is logged and frees the writer's desk, as a tale that comes back unusable does. A
        // test's own tale is checked and planned at once, in the one frame, as before.
        const bool spread = written.empty();
        const auto generation = SlotTracker::GetSingleton()->loadGeneration.load(std::memory_order_relaxed);
        FrameShare share(started);
        bool held = false;
        const auto drop = [&]() {
            if (held) m_taleBusy.store(false);
        };
        const double msBefore = share.Ms();
        if (spread) {
            m_taleBusy.store(true);
            g_requestAt = real;
            held = true;
            *answer = true;
            answer = nullptr;   // the caller has its answer and is gone after this pause
        }
        IE_TALE_PAUSE(-1.0)
        auto request = std::make_shared<Request>();
        request->pool.titles = toldTitles;
        request->asker = asker->GetFormID();
        request->town = me->town;
        request->sequelOf = dueId;
        request->seed = A::detail::Mix(request->asker, static_cast<std::uint32_t>(NowDays() * 1440.0f));
        request->rumored = taleSeed && taleSeed->rumored && !afar;
        request->pool.rumored = request->rumored && written.empty();
        request->afar = afar;
        request->otherHold = taleSeed && taleSeed->otherHold;
        const auto atPeople = std::chrono::steady_clock::now();   // choosing among the town is timed from here
        // ---- the people: the asker, then those of the town the game ties to them, then the town's known faces
        struct Candidate {
            const Folk* folk;
            int score;
        };
        std::vector<Candidate> others;
        // (Where the time of this part goes, for the log line: on a big mod list it was 2.35 ms of one frame.)
        double msScore = 0.0, msUsable = 0.0, msWords = 0.0, msHeart = 0.0, msQuests = 0.0;
        const auto lap = [](std::chrono::steady_clock::time_point& t) {
            const auto now = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(now - t).count();
            t = now;
            return ms;
        };
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
            // Those an open end is about come first among the people offered, so the writer can use them.
            if (std::find(dueCast.begin(), dueCast.end(), f.ref) != dueCast.end()) score += 50;
            // Somebody the game still has a quest for comes last among the people offered: used when the town has too
            // few others, and again freely once that quest is done (a story may then grow out of how it ended). The
            // writer was told to keep clear of such a quest and still built a story on Mallus Maccius and the
            // meadery's skeevers while "Dampened Spirits" had not begun (2026-11-22).
            if (const auto bound = m_journalAliases.find(f.base); bound != m_journalAliases.end()) {
                for (const auto questId : bound->second) {
                    auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(questId);
                    if (quest && !quest->IsCompleted()) {
                        score -= 40;
                        break;
                    }
                }
            }
            others.push_back({&f, score});
        }
        std::sort(others.begin(), others.end(), [](const Candidate& x, const Candidate& y) { return x.score > y.score; });
        msScore = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - atPeople).count();
        IE_TALE_PAUSE(0.08)   // choosing among a town of a large mod list is a frame's share by itself (0.37 ms)

        nlohmann::json people = nlohmann::json::array();
        const auto add = [&](const Folk& f, bool isAsker) {
            auto t = std::chrono::steady_clock::now();
            struct Tally {
                double& into;
                std::chrono::steady_clock::time_point& t;
                ~Tally() { into += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); }
            } rest{msQuests, t};          // whatever is left when the function ends, by any way out
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
            // Held at a place when the story begins: nobody the town or a quest needs, and nobody near the player (they
            // are taken out of sight, before the asker has come). The same measure as a murder's victim (3).
            person.canHold = !isAsker && Usable(actor, 3, none);
            // Close to the one who asks, as the game itself has it: a relationship between the two (kin, spouse, lover,
            // friend, ally), or the same roof. A rescue is about such a person or it is not written (the user,
            // 2026-11-19, of a test in which Ysolda asked after Nazeem: "it kinda doesn't make sense").
            person.close = !isAsker && ((askerBase && npc && RE::BGSRelationship::GetRelationship(askerBase, npc)) ||
                                        (f.home != 0 && f.home == me->home));
            msUsable += lap(t);
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
            if (person.canHold) can.push_back("may be held at a place out in the world when the story begins");
            if (person.close) j["close"] = true;
            j["may"] = can;
            j["ties"] = TiesOf(npc);
            msWords += lap(t);
            // What stands between them and the player, as the game and its mods keep it: fact for the author, so no
            // story gives the player's wife another love or reopens what the player turned down.
            if (auto* whoRef = RE::TESForm::LookupByID<RE::Actor>(f.ref)) {
                if (const auto heart = HeartBridge::WordsFor(whoRef); !heart.empty()) {
                    j["heart"] = heart;
                    // Somebody close to the one who asks, drawn to the player, never answered, and not one to come up
                    // and say it: the asker may come in their stead (a shy admirer; decided below). Looked at only for
                    // those who have a line of the heart at all and are close to the asker: next to nobody.
                    if (!isAsker && admirerKey.empty() && person.close) {
                        const bool had = std::find(heartCast.begin(), heartCast.end(), f.ref) != heartCast.end();
                        if (!had && HeartBridge::ShyAdmirer(whoRef)) {
                            admirerKey = person.key;
                            admirerName = person.name;
                        } else {
                            logger::info("Tales: {} is close to the asker and has something with the player, but is no shy admirer ({})", person.name,
                                         had ? "has had a story of the heart already" : "no unanswered courtship, or bold enough to ask themselves");
                        }
                    }
                }
            }
            msHeart += lap(t);
            // Who holds office in the hold (the jarl, the steward) has a part in Skyrim's own story: a tale may bring a
            // matter before them, never make them the guilty one (the first model-written politics tale made the
            // jarl's steward a secret smuggler, 2026-11-19).
            if (InFactionId(actor, 0x50920) || InFactionId(actor, 0x50922)) j["office"] = true;
            // A quest of the game not done yet that has a part for them (Saadia and the Alik'r): their past and fate
            // there are that quest's, never a tale's.
            if (const auto bound = npc ? m_journalAliases.find(npc->GetFormID()) : m_journalAliases.end(); bound != m_journalAliases.end()) {
                std::string quests, done;
                int n = 0, finished = 0;
                for (const auto questId : bound->second) {
                    auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(questId);
                    const char* qname = quest ? quest->GetFullName() : nullptr;
                    if (!quest || !qname || !*qname) continue;
                    if (quest->IsCompleted()) {
                        // Finished with the player: a tale may grow out of how it ended, never repeat it.
                        if (done.find(qname) == std::string::npos && finished++ < 3) done += (done.empty() ? "" : ", ") + std::string(qname);
                        continue;
                    }
                    if (quests.find(qname) != std::string::npos) continue;
                    quests += (quests.empty() ? "" : ", ") + std::string(qname) + (Begun(quest) ? " (begun)" : " (not begun)");
                    if (++n >= 3) break;
                }
                if (!quests.empty()) j["quest"] = quests;
                if (!done.empty()) j["quest_done"] = done;
            }
            // The game's own small quests with them (favours): never retold, whatever their state.
            if (const auto errands = npc ? m_favours.find(npc->GetFormID()) : m_favours.end(); errands != m_favours.end()) {
                std::string favours;
                int n = 0;
                for (const auto questId : errands->second) {
                    auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(questId);
                    const std::string qname = QuestLabel(quest);
                    if (!quest || qname.empty() || favours.find(qname) != std::string::npos) continue;
                    favours += (favours.empty() ? "" : ", ") + qname + (quest->IsCompleted() ? " (finished)" : Begun(quest) ? " (begun)" : " (not begun)");
                    if (++n >= 4) break;
                }
                if (!favours.empty()) j["favours"] = favours;
            }
            if (j.contains("quest") || j.contains("quest_done") || j.contains("favours")) {
                logger::info("Tales: the game's own quests with {}:{}{}{}", person.name,
                             j.contains("quest") ? " unfinished: " + j["quest"].get<std::string>() + ";" : std::string(),
                             j.contains("quest_done") ? " finished: " + j["quest_done"].get<std::string>() + ";" : std::string(),
                             j.contains("favours") ? " errands: " + j["favours"].get<std::string>() : std::string());
            }
            request->pool.people.push_back(person);
            request->bios.push_back(f.ref);
            people.push_back(j);
            return true;
        };
        if (!add(*me, true)) { drop(); co_return; }
        // A test's own tale may cast anyone free in town; the model is shown the few that fit best.
        const std::size_t poolSize = written.empty() ? kPoolSize : 64;
        for (const auto& c : others) {
            if (request->pool.people.size() >= poolSize) break;
            IE_TALE_PAUSE(0.25)
            add(*c.folk, false);
        }
        if (request->pool.people.size() < 3) {
            logger::info("Tales: no tale for {}: too few free people in {}", asker->GetDisplayFullName(), NameOf(me->town));
            { drop(); co_return; }
        }
        IE_TALE_PAUSE(0.08)
        double msCompanions = 0.0;
        const double companionsFrom = share.Ms();
        // ---- old companions: former followers living a life of their own, wherever it has taken them. A story may
        // lead the player to them (a talk with someone out at a ruin is a journey there) or have them seek the player
        // out; they are never jailed, sent away or fought, and nothing is hidden with them (the user, 2026-11-19:
        // "You can meet followers along the way in a dungeon or on the road if you dismissed them. Or they may seek
        // you out if the story permits and it is something important").
        {
            int companions = 0;
            for (const auto& c : LivingWorldBridge::GetSingleton()->OldCompanions()) {
                if (companions >= 2) break;
                if (std::any_of(request->pool.people.begin(), request->pool.people.end(), [&](const Tales::Person& p) { return p.form == c.actor; })) continue;
                auto* companion = RE::TESForm::LookupByID<RE::Actor>(c.actor);
                auto* base = companion ? companion->GetActorBase() : nullptr;
                const char* name = companion ? companion->GetDisplayFullName() : nullptr;
                if (!base || !name || !*name) continue;
                Tales::Person person;
                person.key = "p" + std::to_string(request->pool.people.size());
                person.name = name;
                person.form = c.actor;
                nlohmann::json j;
                j["key"] = person.key;
                j["name"] = person.name;
                j["sex"] = base->IsFemale() ? "woman" : "man";
                auto* race = companion->GetRace();
                j["race"] = race && race->GetFullName() ? race->GetFullName() : "";
                j["work"] = "once travelled with " + PlayerName() + ", now on a road of their own";
                j["temperament"] = Temperament(base);
                const std::string where = c.place ? NameOf(c.place) : std::string();
                j["home"] = where.empty() ? std::string("away, living a life of their own") : "away, at " + where + " now";
                j["may"] = nlohmann::json::array();
                j["ties"] = TiesOf(base);
                if (const auto heart = HeartBridge::WordsFor(companion); !heart.empty()) j["heart"] = heart;
                j["old"] = c.life.empty() ? std::string("They parted from " + PlayerName() + " a while ago.") : c.life;
                request->pool.people.push_back(person);
                request->bios.push_back(c.actor);
                people.push_back(j);
                ++companions;
            }
            if (companions) logger::info("Tales: {} old companion(s) of the player may have a part", companions);
        }
        request->pool.items = ItemChoices();

        msCompanions = share.Ms() - companionsFrom;
        IE_TALE_PAUSE(0.08)
        double msPlaces = 0.0;
        double placesFrom = share.Ms();
        // ---- places out in the world in the town's hold, the nearest first, at most two of a kind
        nlohmann::json places = nlohmann::json::array();
        {
            const auto blocked = ObjectiveLocations();
            auto* townLoc = RE::TESForm::LookupByID<RE::BGSLocation>(me->town);
            auto* townHold = HoldOfLoc(townLoc);
            auto townMarker = townLoc ? std::as_const(townLoc->worldLocMarker).get() : RE::NiPointer<RE::TESObjectREFR>();
            const auto townAt = townMarker ? std::optional<RE::NiPoint3>(townMarker->GetPosition()) : std::nullopt;
            struct Near {
                QuestBridge::PlaceView p;
                std::uint8_t kind;
                float distance;
            };
            std::vector<Near> nearby;
            int refused = 0;
            for (const auto& p : QuestBridge::GetSingleton()->Places()) {
                if (!townHold || p.hold != townHold->GetFormID()) continue;
                msPlaces += share.Ms() - placesFrom;
                IE_TALE_PAUSE(0.25)   // a hold of a large mod list has dozens of places to weigh
                placesFrom = share.Ms();
                auto* location = RE::TESForm::LookupByID<RE::BGSLocation>(p.location);
                const auto kind = SiteKindOf(location);
                if (kind == Tales::SiteNone) continue;
                std::string why;
                if (!SiteFree(location, blocked, why)) {
                    ++refused;
                    continue;
                }
                // The heart of many places is no reference of the game until its part of the place is loaded (every
                // Dwemer ruin looked at; Mzinchaleft's lies in its second hall). Such a place is offered all the
                // same: its master is put there when the player gets that far, and until then the journal leads to
                // the place itself (ArcBridge: PlaceMarker, the watch's signal 14).
                const float distance = townAt && p.hasAt ? townAt->GetDistance(p.at) : 1.0e9f;
                nearby.push_back({p, kind, distance});
            }
            std::sort(nearby.begin(), nearby.end(), [](const Near& x, const Near& y) { return x.distance < y.distance; });
            std::map<std::uint8_t, int> perKind;
            std::vector<std::string> offered;   // a place is offered once, however many records carry its name
            // A place a story led to lately rests, as long as two others are near enough to choose from (a town
            // with one cave by it keeps its cave).
            const auto rests = [&](const Near& n) { return std::find(restedPlaces.begin(), restedPlaces.end(), n.p.location) != restedPlaces.end(); };
            const auto fresh = std::count_if(nearby.begin(), nearby.end(), [&](const Near& n) { return !rests(n); });
            int rested = 0;
            // And another kind of place than last time, as long as two places of other kinds are near: a bandit camp
            // after a bandit camp is the same story's second act again, whoever its chief is called (the play session
            // of 2026-11-22: Halted Stream Camp, then Silent Moons Camp, a chief with a hold over a townsman in both).
            const std::uint8_t lastKind = lastPlace ? SiteKindOf(RE::TESForm::LookupByID<RE::BGSLocation>(lastPlace)) : Tales::SiteNone;
            const auto otherKinds = std::count_if(nearby.begin(), nearby.end(), [&](const Near& n) { return !rests(n) && n.kind != lastKind; });
            int sameKind = 0;
            for (const auto& n : nearby) {
                if (request->pool.sites.size() >= 8) break;
                auto* location = RE::TESForm::LookupByID<RE::BGSLocation>(n.p.location);
                const char* name = location ? location->GetFullName() : nullptr;
                if (!name || !*name) continue;
                if (fresh >= 2 && rests(n)) {
                    ++rested;
                    continue;
                }
                if (lastKind != Tales::SiteNone && n.kind == lastKind && otherKinds >= 2) {
                    ++sameKind;
                    continue;
                }
                if (std::find(offered.begin(), offered.end(), name) != offered.end()) continue;
                if (++perKind[n.kind] > 2) continue;
                offered.emplace_back(name);
                Tales::Site site;
                site.key = "d" + std::to_string(request->pool.sites.size() + 1);
                site.name = name;
                site.location = n.p.location;
                site.anchor = n.p.anchor;
                site.kind = n.kind;
                // Ground under the open sky by its way in (its marker on the map): where what was left of a victim
                // can lie to be found.
                site.outside = location && std::as_const(location->worldLocMarker).get().get() != nullptr;
                request->pool.sites.push_back(site);
                // Which master a den has (the same sum ArcBridge::Form "bossbase" makes): known before a word is written.
                const bool spider = A::detail::Mix(request->seed, site.location) % 2u != 0u;
                nlohmann::json j;
                j["key"] = site.key;
                j["name"] = site.name;
                j["what"] = SiteWhat(n.kind);
                j["heart"] = n.kind == Tales::SiteDen ? (spider ? "a giant frostbite spider" : "a troll") : SiteHeart(n.kind);
                j["signs"] = SiteSigns(n.kind, spider);
                j["outside"] = site.outside;
                j["far"] = n.distance < 15000.0f ? "close by" : n.distance < 40000.0f ? "half a day's walk" : "far across the hold";
                j["cleared"] = location && location->IsCleared();
                j["holds"] = Tales::SiteHolds(n.kind);
                places.push_back(j);
            }
            if (refused) logger::info("Tales: {} place(s) of the hold left out: a quest of the game or of the player's needs them", refused);
            if (rested) logger::info("Tales: {} place(s) rest: a story led there lately", rested);
            if (sameKind) logger::info("Tales: {} place(s) left out: the last story led to one of their kind ({})", sameKind, SiteWhat(lastKind));
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        // The reward: by the player's level, and between four fifths and six fifths of it by the story's own seed, in
        // fives (every tale of a new game paid 155).
        {
            const std::uint32_t base = A::RewardGold(A::Pattern::Thief, player ? player->GetLevel() : 10);
            const std::uint32_t fifths = 16u + A::detail::Mix(request->seed, 0x60D5u) % 9u;   // 16..24 twentieths
            request->pool.gold = std::max<std::uint32_t>(25u, (base * fifths / 20u + 2u) / 5u * 5u);
        }

        msPlaces += share.Ms() - placesFrom;
        IE_TALE_PAUSE(0.08)
        double restFrom = share.Ms();
        double msRest = 0.0, msQuests2 = 0.0, msDump = 0.0;
        // The genre to lean toward: one not told lately, in an order of the day.
        std::vector<std::string> fresh;
        for (const auto* g : kGenres) {
            if (std::find(recentGenres.begin(), recentGenres.begin() + std::min<std::size_t>(recentGenres.size(), 5), g) == recentGenres.end()) fresh.push_back(g);
        }
        std::string lean = fresh.empty() ? std::string(kGenres[seed % kGenres.size()]) : fresh[seed % fresh.size()];
        // And a second flavour to weave in (the user, 2026-11-20: "Rotate in a meaningful way or compose them together
        // ... All flavours can be combined to create beautiful stories"): a hunt that is also about a marriage, a debt
        // with something uncanny under it. Another of those not told lately.
        std::string thread;
        if (fresh.size() >= 2) {
            thread = fresh[(seed / 7u + 1u) % fresh.size()];
            if (thread == lean) thread = fresh[(seed / 7u + 2u) % fresh.size()];
            if (thread == lean) thread.clear();
        }
        // A story of the heart, when that is what stands between the asker and the player (Hearts.h decides who and
        // what, from facts): about the two of them, not a favour for a third. Never two such stories running, and
        // none while one is open.
        std::string heartLean;
        bool secondChance = false;
        if (written.empty() && !recentHeart) {
            const auto wants = HeartBridge::LeanFor(asker);
            switch (wants) {
                case Hearts::Lean::Courtship: heartLean = "courtship"; break;
                case Hearts::Lean::Together: heartLean = "together"; break;
                case Hearts::Lean::Family: heartLean = "family"; break;
                case Hearts::Lean::Affair: heartLean = "affair"; break;
                default: break;
            }
            if (!heartLean.empty()) {
                lean = Hearts::LeanWord(wants);
                request->heart = true;
                request->pool.heart = true;
                secondChance = wants == Hearts::Lean::Courtship && HeartBridge::SecondChanceFor(asker);
                logger::info("Tales: {} and the player: a story of the heart ({}{})", asker->GetDisplayFullName(), lean,
                             secondChance ? ", after a no long ago" : "");
            } else if (!admirerKey.empty() && !taleSeed) {
                // A shy admirer (the user, 2026-11-21: "someone too shy to confess [who] sends someone"): the one who
                // asks is close to somebody who is drawn to the player and has never said so. The asker comes in their
                // stead; the story is the player finding out who, and what to do about it. Once for each admirer.
                heartLean = "admirer";
                lean = "a shy admirer";
                request->heart = true;
                request->pool.heart = true;
                logger::info("Tales: {} comes for {}, who is drawn to the player and has not said so: a shy admirer's story",
                             asker->GetDisplayFullName(), admirerName);
            }
        }

        msRest += share.Ms() - restFrom;
        IE_TALE_PAUSE(0.08)
        restFrom = share.Ms();
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
        // Heavy beats are not told back to back: an ambush at most once in four stories, a brawl once in three, and
        // after two stories that led out into the world the next stays among people. The engine holds to it (the
        // compiler refuses them), whatever the author would like.
        // What the user has switched off in the settings is never in a story, told lately or not: no ambush with
        // "ambush" off, no shadow with "stalker" off, nobody held with "rescue" off. (These switches governed the
        // dungeon master's one-off moments only; a tale could have the very thing in it.) "Clear out enemies" off
        // bars the master of a place to kill, and is kept apart from the bits below: a place may still be gone into.
        const std::uint32_t switchedOff = ArcBridge::GetSingleton()->SwitchesOff();
        const std::uint32_t slayOff = (switchedOff & ArcBridge::kSwitchClearOut) ? 1u << static_cast<unsigned>(A::Kind::Slay) : 0u;
        bool slayTwice = false;   // the last story ended a place's master: this one does not
        std::uint32_t barred = ((switchedOff & ArcBridge::kSwitchAmbush) ? 1u << static_cast<unsigned>(A::Kind::Ambush) : 0u) |
                               ((switchedOff & ArcBridge::kSwitchStalker) ? 1u << static_cast<unsigned>(A::Kind::Stalk) : 0u) |
                               ((switchedOff & ArcBridge::kSwitchRescue) ? 1u << static_cast<unsigned>(A::Kind::Free) : 0u);
        if (written.empty()) {
            const auto bit = [](A::Kind k) { return 1u << static_cast<unsigned>(k); };
            const auto used = [&](std::uint32_t mask, std::size_t last) {
                for (std::size_t i = 0; i < std::min(last, recentBeats.size()); ++i) if (recentBeats[i] & mask) return true;
                return false;
            };
            const std::uint32_t place = bit(A::Kind::Delve) | bit(A::Kind::Slay);
            if (used(bit(A::Kind::Ambush), 3)) barred |= bit(A::Kind::Ambush);
            if (used(bit(A::Kind::Brawl), 2)) barred |= bit(A::Kind::Brawl);
            if (used(bit(A::Kind::Stalk), 3)) barred |= bit(A::Kind::Stalk);
            if (used(bit(A::Kind::Free), 3)) barred |= bit(A::Kind::Free);   // someone held: once in four stories
            if (used(bit(A::Kind::Examine), 1)) barred |= bit(A::Kind::Examine);   // a hunt for what kills: never two running
            if (recentBeats.size() >= 2 && (recentBeats[0] & place) && (recentBeats[1] & place)) barred |= place;
            // A master killed at a place: never in two stories running (a story asked for by kind, a hunt, a haunting,
            // a rescue, is that kind whatever came before). The place itself may be gone into again: for what lies
            // there, for someone who waits there, for a look at what happened.
            const bool byKind = taleSeed && (taleSeed->kind == "mystery" || taleSeed->kind == "haunting" || taleSeed->kind == "rescue");
            if (!byKind && !recentBeats.empty() && (recentBeats[0] & bit(A::Kind::Slay))) slayTwice = true;
            if (barred & place) {
                request->pool.sites.clear();
                places = nlohmann::json::array();
            }
            if (barred) {
                logger::info("Tales: held back this time (told lately):{}{}{}{}", (barred & bit(A::Kind::Ambush)) ? " ambush" : "",
                             (barred & bit(A::Kind::Brawl)) ? " brawl" : "", (barred & bit(A::Kind::Stalk)) ? " stalk" : "",
                             (barred & place) ? " places out in the world" : "");
            }
            ctx["bar_ambush"] = (barred & bit(A::Kind::Ambush)) != 0;
            ctx["bar_brawl"] = (barred & bit(A::Kind::Brawl)) != 0;
            ctx["bar_stalk"] = (barred & bit(A::Kind::Stalk)) != 0;
            ctx["bar_places"] = (barred & place) != 0;
            // Out into the world by the engine's count, not the author's mood: after two stories among people the
            // next one has a place in it (never a story of the heart: that one is between two people).
            const bool must = heartLean.empty() && !(barred & place) && !request->pool.sites.empty() && recentBeats.size() >= 2 &&
                              !used(place | bit(A::Kind::Free), 2);
            request->pool.mustPlace = must;
            ctx["must_place"] = must;
            if (must) logger::info("Tales: this one leads out into the world (the last two stayed among people)");
        } else {
            ctx["bar_ambush"] = (barred & (1u << static_cast<unsigned>(A::Kind::Ambush))) != 0;
            ctx["bar_brawl"] = false;
            ctx["bar_stalk"] = (barred & (1u << static_cast<unsigned>(A::Kind::Stalk))) != 0;
            ctx["bar_places"] = false;
            ctx["must_place"] = false;
        }
        ctx["bar_slay"] = slayOff != 0 || slayTwice;
        request->pool.barred = barred | slayOff | (slayTwice ? 1u << static_cast<unsigned>(A::Kind::Slay) : 0u);
        if (slayTwice) logger::info("Tales: no master of a place is killed in this one: the last story ended on one");
        request->pool.depth = written.empty();
        msRest += share.Ms() - restFrom;
        IE_TALE_PAUSE(0.08)
        restFrom = share.Ms();
        // ---- the build of the day ---------------------------------------------------------------------------------
        // The writer has sixteen kinds of step and, left alone, uses four: ask, ask, go to a place, decide. Ten stories
        // of 2026-11-22 had one search and one body to look at between them: nothing handed over, no fistfight, nobody
        // shadowed, nothing that had to come due, no rescue, no haunting. The engine forbade repeats and asked for
        // nothing (the user: "Isnt the story composer ever being used to make the stories vary and not feel samey?").
        // Now it deals each story its build: of those these people and places allow, the one unused longest, and every
        // third story a heavy one (a hunt, a haunting, a rescue, someone on the player's heels, an ambush). The
        // compiler holds the story to it (Pool::mustKinds, or the flags of a hunt, a haunting, a rescue). Not for a
        // story asked for by kind or by someone with a reason of their own, a story of the heart, one that follows
        // another, or a test's own tale.
        bool buildHunt = false, buildHaunt = false, buildRescue = false;
        std::string buildAsk;
        if (written.empty() && heartLean.empty() && !(taleSeed && !taleSeed->kind.empty()) && !dueId && !afar) {
            const auto bitOf = [](A::Kind k) { return 1u << static_cast<unsigned>(k); };
            const auto age = [&](std::uint32_t mask) {   // stories told since one had it; 1000: never
                for (std::size_t i = 0; i < recentBeats.size(); ++i) {
                    if (recentBeats[i] & mask) return static_cast<int>(i);
                }
                return 1000;
            };
            const auto& pool = request->pool;
            const bool ground = std::any_of(pool.sites.begin(), pool.sites.end(), [](const Tales::Site& s) { return s.outside; });
            const bool holds = std::any_of(pool.sites.begin(), pool.sites.end(), [](const Tales::Site& s) { return Tales::SiteHolds(s.kind); });
            const bool dear = std::any_of(pool.people.begin(), pool.people.end(), [](const Tales::Person& p) { return p.canHold && p.close; });
            const bool fists = std::any_of(pool.people.begin(), pool.people.end(), [](const Tales::Person& p) { return p.canBrawl; });
            const std::uint32_t places = bitOf(A::Kind::Delve) | bitOf(A::Kind::Slay);
            const bool hauntedLately = (!recentBeats.empty() && (recentBeats[0] & bitOf(A::Kind::Haunt))) ||
                                       (recentBeats.size() > 1 && (recentBeats[1] & bitOf(A::Kind::Haunt)));
            struct Build {
                const char* name;
                std::uint32_t bits;
                bool heavy;
                bool can;
                int special;   // 0 a step kind the compiler asks for, 1 a place, 2 a hunt, 3 a haunting, 4 a rescue
                const char* ask;
            };
            const Build builds[] = {
                {"a search", bitOf(A::Kind::Have), false, !pool.items.empty(), 0,
                 "Something has to be found (a \"find\" step): an item of the list hidden in someone's home or carried in someone's pocket, "
                 "and what is found is not what {player} was led to expect."},
                // (An item is never with the one who asks: it lies hidden somewhere, is found, and only then handed on.)
                {"a hand-over", bitOf(A::Kind::Have) | bitOf(A::Kind::Give), false, !pool.items.empty(), 0,
                 "Something has to be found and then handed to someone (a \"find\" step, then a \"give\" step with the same item): an "
                 "item of the list hidden in someone's home or pocket, never with the one who asks. The handing over is where the story "
                 "turns: whoever receives it was not meant to, or it is not what they think."},
                {"a wait and a return", bitOf(A::Kind::Wait) | bitOf(A::Kind::Visit), false, true, 0,
                 "It does not end in a day: something has to come due (\"wait\", some hours to two days), and then someone comes to "
                 "{player} of their own accord (\"visit\") with what the waiting brought."},
                {"a fistfight", bitOf(A::Kind::Brawl), false, fists && !(barred & bitOf(A::Kind::Brawl)), 0,
                 "At one point it comes to blows with bare fists (\"brawl\") with someone who would settle a matter that way, and winning "
                 "and losing lead to different places."},
                {"a place out in the world", bitOf(A::Kind::Delve), false, !pool.sites.empty() && !(barred & places), 1, ""},
                {"someone on the player's heels", bitOf(A::Kind::Stalk), true, !(barred & bitOf(A::Kind::Stalk)), 0,
                 "Someone shadows {player} out of doors (\"stalk\") until they are caught at it or come forward, and what they then say "
                 "changes what the story is about."},
                {"an ambush", bitOf(A::Kind::Ambush), true, !(barred & bitOf(A::Kind::Ambush)), 0,
                 "Once {player} has learned too much, someone hires men to waylay them out in the wilds (\"ambush\"), and who paid those "
                 "men is part of the truth."},
                {"a hunt", bitOf(A::Kind::Examine), true, ground && !(barred & bitOf(A::Kind::Examine)) && !slayTwice && !slayOff, 2, ""},
                {"a haunting", bitOf(A::Kind::Haunt), true, ground && !hauntedLately, 3, ""},
                {"a rescue", bitOf(A::Kind::Free), true, holds && dear && !(barred & bitOf(A::Kind::Free)), 4, ""},
            };
            const std::uint32_t heavy = bitOf(A::Kind::Stalk) | bitOf(A::Kind::Ambush) | bitOf(A::Kind::Free) | bitOf(A::Kind::Examine) |
                                        bitOf(A::Kind::Haunt);
            const bool heavyTurn = recentBeats.size() >= 2 && !(recentBeats[0] & heavy) && !(recentBeats[1] & heavy);
            const Build* chosen = nullptr;
            const auto pick = [&](bool wantHeavy) {
                long best = -1;
                for (std::size_t i = 0; i < std::size(builds); ++i) {
                    const auto& b = builds[i];
                    if (b.heavy != wantHeavy || !b.can) continue;
                    // Longest unused first; among equals the story's own seed decides.
                    const long rank = static_cast<long>(age(b.bits)) * 1000 + static_cast<long>(A::detail::Mix(request->seed, static_cast<std::uint32_t>(i)) % 997u);
                    if (rank > best) {
                        best = rank;
                        chosen = &b;
                    }
                }
            };
            if (heavyTurn) pick(true);
            if (!chosen) pick(false);
            // The engine's older count stands: after two stories among people this one has a place in it.
            if (request->pool.mustPlace && !(chosen && chosen->heavy)) chosen = &builds[4];
            if (chosen) {
                const int since = age(chosen->bits);
                switch (chosen->special) {
                    case 1:
                        request->pool.mustPlace = true;
                        ctx["must_place"] = true;
                        break;
                    case 2: buildHunt = true; break;
                    case 3: buildHaunt = true; break;
                    case 4: buildRescue = true; break;
                    default: {
                        request->pool.mustKinds = chosen->bits;
                        buildAsk = chosen->ask;
                        const std::string name = PlayerName();
                        for (auto at = buildAsk.find("{player}"); at != std::string::npos; at = buildAsk.find("{player}", at + name.size())) {
                            buildAsk.replace(at, 8, name);
                        }
                        break;
                    }
                }
                logger::info("Tales: this one's build: {} ({}; {})", chosen->name,
                             since >= 1000 ? std::string("never told yet") : std::to_string(since + 1) + " stories since the last",
                             heavyTurn && chosen->heavy ? "a heavy turn: the last two were light" : "a light turn");
            }
        }
        ctx["build"] = buildAsk;
        // ---- a different beginning each time ----------------------------------------------------------------------
        // The writer was shown some eighteen premises at once as examples and took the same one again and again:
        // money moving in secret (six of ten titles of 2026-11-22 had the word "Ledger"). Now each story is handed one
        // beginning, the next of the list for this character's game, to make true of these people; the list is about
        // people far more than about coin.
        {
            static constexpr const char* kBeginnings[] = {
                "a promise made in anger that someone now has to keep",
                "two people who each believe the same thing is rightfully theirs",
                "a letter that was written and never sent",
                "someone who has stopped going to a place they went to every day",
                "a friendship that ended, and neither will say why",
                "a thing lent years ago and never given back",
                "someone who saw something and has told it two different ways",
                "an old injury that the wrong person has been blamed for all this time",
                "a gift that was meant as an insult",
                "a wager that went further than either meant",
                "a vow made to a god that has become inconvenient",
                "a courtship one family must not hear of",
                "a helper who knows the work better than the one they serve",
                "someone making ready to leave town without telling anyone",
                "two people of the same craft, one quietly undercutting the other",
                "a rumour someone started in spite and can no longer stop",
                "a kindness done in secret that looks like guilt from outside",
                "someone taken for somebody else, and letting it stand",
                "something buried, in the ground or in a record, that a newcomer has begun asking about",
                "a favour owed to someone on the other side of the war",
                "a debt called in at the worst moment",
                "a tale told to frighten that those who tell it have stopped laughing at",
                "an heirloom two branches of a family remember differently",
                "a death long ago that everyone agreed not to look at closely",
                "someone doing another's work for them, and hiding it",
                "a song, a carving or a dish that someone claims as their own and is not",
            };
            std::string beginning;
            if (written.empty() && heartLean.empty() && !dueId) {
                const auto base = static_cast<std::size_t>(std::hash<std::string>{}(PlayerName()));
                beginning = kBeginnings[(base + talesSoFar) % std::size(kBeginnings)];
                logger::info("Tales: this one begins from: {}", beginning);
            }
            ctx["seed_idea"] = beginning;
        }
        // A hunt for what is killing people: on offer when a place near has ground outside it for what was left of a
        // victim. Asked for (a rumor of deaths, the test hook), it is that or nothing.
        {
            const auto examine = 1u << static_cast<unsigned>(A::Kind::Examine);
            const bool ground = std::any_of(request->pool.sites.begin(), request->pool.sites.end(), [](const Tales::Site& s) { return s.outside; });
            const bool mayHunt = ground && !(barred & examine) && heartLean.empty() && !slayTwice;   // a hunt ends on what did it, killed
            const bool hunt = (taleSeed && taleSeed->kind == "mystery") || buildHunt;
            if (hunt && !mayHunt) {
                logger::info("Tales: no hunt for {}: {}", asker->GetDisplayFullName(),
                             !ground ? "no place near with ground outside it" : "the last story was one, or this one is of the heart");
                { drop(); co_return; }
            }
            ctx["bar_examine"] = !mayHunt;
            if (!mayHunt) request->pool.barred |= examine;
            request->pool.mustExamine = hunt;
            ctx["must_mystery"] = hunt;
            if (hunt) {
                request->pool.mustPlace = false;
                ctx["must_place"] = false;
            }
            // Something that walks by night outside one of those places: on offer on the same ground, never in two
            // stories running, never in a story of the heart. Asked for (the test hook, talk of a haunting), it is
            // that or nothing.
            const auto haunt = 1u << static_cast<unsigned>(A::Kind::Haunt);
            const bool toldLately = std::any_of(recentBeats.begin(), recentBeats.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(2, recentBeats.size())),
                                                [haunt](std::uint32_t beats) { return (beats & haunt) != 0; });
            const bool mayHaunt = ground && !toldLately && heartLean.empty();
            const bool haunting = (taleSeed && taleSeed->kind == "haunting") || buildHaunt;
            if (haunting && !mayHaunt) {
                logger::info("Tales: no haunting for {}: {}", asker->GetDisplayFullName(),
                             !ground ? "no place near with ground outside it" : "one of the last two stories was one, or this one is of the heart");
                { drop(); co_return; }
            }
            ctx["bar_haunt"] = !mayHaunt;
            if (!mayHaunt) request->pool.barred |= haunt;
            request->pool.mustHaunt = haunting;
            ctx["must_haunt"] = haunting;
            if (haunting) {
                request->pool.mustPlace = false;
                ctx["must_place"] = false;
            }
        }
        // Someone held at a place: on offer when a place that keeps prisoners is near and someone of the town may be
        // held. A rescue asked for (the old dungeon master's quest of that name) is a story about exactly that, or
        // nothing: no rescue without a place and a person the game can carry it out with.
        {
            const bool holds = std::any_of(request->pool.sites.begin(), request->pool.sites.end(), [](const Tales::Site& s) { return Tales::SiteHolds(s.kind); });
            const bool someone = std::any_of(request->pool.people.begin(), request->pool.people.end(), [](const Tales::Person& p) { return p.canHold; });
            const bool dear = std::any_of(request->pool.people.begin(), request->pool.people.end(), [](const Tales::Person& p) { return p.canHold && p.close; });
            const bool mayHold = holds && someone && !(barred & (1u << static_cast<unsigned>(A::Kind::Free))) && heartLean.empty();
            const bool rescue = (taleSeed && taleSeed->kind == "rescue") || buildRescue;
            if (rescue && (!mayHold || !dear)) {
                logger::info("Tales: no rescue for {}: {}", asker->GetDisplayFullName(),
                             !holds ? "no place near that keeps prisoners" : !someone ? "nobody of the town may be held"
                             : !dear ? "nobody close to them may be held (a rescue is for someone who is theirs)" : "someone was held in a story told lately");
                { drop(); co_return; }
            }
            ctx["bar_captive"] = !mayHold;
            if (!mayHold) request->pool.barred |= 1u << static_cast<unsigned>(A::Kind::Free);
            request->pool.mustCaptive = rescue;
            ctx["must_captive"] = rescue;
            if (rescue) {
                request->pool.mustPlace = false;
                ctx["must_place"] = false;
            }
        }
        // (The words for the writer are put together a part a frame as well: on a large mod list this last part was
        // 0.7 ms in one.)
        msRest += share.Ms() - restFrom;
        IE_TALE_PAUSE(0.08)
        restFrom = share.Ms();
        ctx["places"] = places;
        {
            const double from = share.Ms();
            ctx["quests"] = PlayerQuests();
            msQuests2 = share.Ms() - from;
        }
        ctx["told"] = told;
        // The language the player reads in ("Russian"), empty for English: the writer is told to write in it what
        // the player reads (LanguageBridge.h). In an English game whose people speak another language it stays
        // English: the journal is the game's, the speech is SkyrimNet's.
        ctx["text_language"] = LanguageBridge::GetSingleton()->TextLanguage();
        ctx["lean"] = lean;
        // The open end that has come due, for the writer (an empty object when there is none; a story of the heart
        // is its own kind).
        nlohmann::json sequel = nlohmann::json::object();
        if (dueId && heartLean.empty()) {
            sequel = {{"title", dueTitle}, {"when", dueWhen}, {"ended", dueEnded}, {"open", dueOpen}, {"people", duePeople}};
            logger::info("Tales: an open end has come due: \"{}\" ({}) left open: {}", dueTitle, dueWhen, dueOpen);
        } else {
            request->sequelOf = 0;
        }
        ctx["sequel"] = sequel;
        ctx["thread"] = heartLean.empty() ? thread : std::string();
        ctx["rumored"] = request->pool.rumored;
        if (!heartLean.empty()) ctx["heart_lean"] = heartLean;
        if (heartLean == "admirer") {
            ctx["admirer"] = admirerKey;
            ctx["admirer_name"] = admirerName;
        }
        if (secondChance) ctx["second_chance"] = true;
        ctx["reward"] = request->pool.gold;
        {
            const int knowsPlayer = KnowsPlayer(asker, me->town);
            ctx["asker_knows"] = knowsPlayer >= 2 ? "met" : knowsPlayer == 1 ? "heard" : "stranger";
            logger::info("Tales: {} {}", asker->GetDisplayFullName(),
                         knowsPlayer >= 2 ? "has had to do with the player before" : knowsPlayer == 1 ? "has heard of the player in town" : "is a stranger to the player");
        }
        if (taleSeed && !taleSeed->why.empty()) {
            // The candidate analyser's view of them: the reason this story begins now must come from it.
            std::string why = taleSeed->why;
            if (why.size() > 3000) {
                std::size_t n = 3000;
                while (n > 0 && (static_cast<unsigned char>(why[n]) & 0xC0) == 0x80) --n;
                why.resize(n);
            }
            ctx["why"] = why;
            ctx["kind"] = taleSeed->kind;
        }
        if (afar && player) {
            auto* cell = player->GetParentCell();
            auto* loc = cell && cell->GetLocation() ? cell->GetLocation() : player->GetCurrentLocation();
            std::string where = loc && loc->GetFullName() ? loc->GetFullName() : "";
            if (loc && loc->HasKeywordString("LocTypePlayerHouse")) where += " (" + PlayerName() + "'s own home)";
            ctx["player_where"] = where;
        }
        msRest += share.Ms() - restFrom;
        IE_TALE_PAUSE(0.08)
        restFrom = share.Ms();
        {
            const double from = share.Ms();
            request->context = ctx.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            msDump = share.Ms() - from;
        }

        m_taleBusy.store(true);
        g_requestAt = real;
        g_request = request;
        msRest += share.Ms() - restFrom;
        share.End();
        logger::info("Tales: asking for a tale of {} in {} ({} people, {} places, {} items, leaning {}, {} told before; at most "
                     "{:.2f} ms of the game's frame, over {} frame(s); {:.2f} ms in all: {:.2f} before the people, {:.2f} the people "
                     "({:.2f} choosing among the town, {:.2f} who may do what, {:.2f} work and ties, {:.2f} the heart, {:.2f} the "
                     "game's quests and the rest), {:.2f} old companions, {:.2f} the places, {:.2f} the rest, of it {:.2f} the player's quests and {:.2f} writing it down)",
                     request->pool.people[0].name, NameOf(me->town), request->pool.people.size(), request->pool.sites.size(),
                     request->pool.items.size(), lean, told.size(), share.worst, share.frames, share.total, msBefore,
                     msScore + msUsable + msWords + msHeart + msQuests, msScore, msUsable, msWords, msHeart, msQuests, msCompanions,
                     msPlaces, msRest, msQuests2, msDump);
        if (!written.empty()) {
            std::string who;
            for (const auto& p : request->pool.people) {
                if (p.canJail || p.canBrawl || p.canHold || p.close) who += " " + p.name + (p.canJail ? " [jail]" : "") + (p.canBrawl ? " [brawl]" : "") + (p.canHold ? " [hold]" : "") + (p.close ? " [close]" : "") + ";";
            }
            for (const auto& s : request->pool.sites) who += " place " + s.key + " " + s.name + " (" + SiteWhat(s.kind) + ");";
            logger::info("Tales: test pool:{}", who);
            TaleWritten(request, 9, written);   // a test's own tale: checked and planned at once, no second asking, no fallback
            if (answer) *answer = true;
            co_return;
        }
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
                // What people of the hold talk about now, and the times they live in (the wars, how the factions take
                // the player): read here, off the game's thread. A story may grow out of them, never against them
                // (the prompt says how). A story of the heart is between two people: it is told neither.
                if (!ctx.contains("heart_lean")) {
                    nlohmann::json talk = nlohmann::json::array();
                    for (auto& line : RumorBridge::GetSingleton()->TalkForTales(3)) talk.push_back(std::move(line));
                    if (!talk.empty()) ctx["talk"] = std::move(talk);
                    std::string times;
                    try {
                        times = FactionPolitics::GetSingleton()->TimesForTales(Safe::Text(ctx, "player_name", "the traveller"));
                    } catch (...) {
                    }
                    if (!times.empty()) ctx["times"] = times;
                    logger::info("Tales: the author is told {} thing(s) going round and {} of the times", ctx.contains("talk") ? ctx["talk"].size() : 0,
                                 times.empty() ? "nothing" : "something");
                }
                request->context = ctx.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
            SendTale(request, 0, {}, {});
        });
        if (answer) *answer = true;
        co_return;
    }
#undef IE_TALE_PAUSE

    void ArcBridge::SendTale(std::shared_ptr<void> handle, int attempt, std::string fix, std::string previous) {
        auto request = std::static_pointer_cast<Request>(handle);
        std::string context = request->context;
        if (attempt > 0) {
            auto ctx = nlohmann::json::parse(context, nullptr, false);
            if (ctx.is_object()) {
                ctx["fix"] = fix;
                ctx["previous"] = previous.size() > 6000 ? previous.substr(0, 6000) : previous;
                context = ctx.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
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
            g_request.reset();
            m_taleBusy.store(false);
            if (attempt >= 9) {
                logger::info("Tales: no tale ({})", why);   // a test's own tale: nothing else instead
                return;
            }
            if (request->rumored) {
                // It would have begun as talk in town: with no tale there is no talk, and nobody comes instead.
                logger::info("Tales: no tale ({}); nothing goes round", why);
                return;
            }
            logger::info("Tales: no tale ({}); the town's story falls back to a pattern", why);
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
        // The tale as written, for the record (one line; the model's own words).
        logger::info("Tales: the tale as written (attempt {}): {}", attempt + 1, json.is_object() ? json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) : text.substr(0, 4000));
        request->pool.titleMayRepeat = attempt >= 1;   // a title is no reason to lose a tale asked for twice
        // Nor is its build: asked for twice and still without it, a story is better than none (a hunt, a haunting and a
        // rescue are held to what they are both times: they are a kind of story, not a step in one).
        if (attempt >= 1 && request->pool.mustKinds) {
            logger::info("Tales: the second asking is not held to its build");
            request->pool.mustKinds = 0;
        }
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
        if (attempt < 9 && !request->afar) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            auto* here = player ? TownOfLoc(cell && cell->GetLocation() ? cell->GetLocation() : player->GetCurrentLocation()) : nullptr;
            if (!here || here->GetFormID() != request->town) {
                logger::info("Tales: \"{}\" is not told: the player left {} while it was written", a.title, NameOf(request->town));
                g_request.reset();
                m_taleBusy.store(false);
                return;
            }
        }
        a.place = request->town;
        if (request->otherHold) a.flags |= A::Flag::CameFar;
        else if (request->afar) a.flags |= A::Flag::Sent;   // the director's rule let them come from elsewhere in the hold
        if (request->heart) a.flags |= A::Flag::Heart;
        a.seed = request->seed;
        a.sequelOf = request->sequelOf;
        if (request->sequelOf) {
            // What the earlier tale left open is taken up now: once.
            std::scoped_lock lock(m_mutex);
            if (auto* earlier = m_book.Find(request->sequelOf)) {
                earlier->followed = 1;
                logger::info("Tales: \"{}\" grows out of what \"{}\" left open", a.title, earlier->title);
            }
        }
        g_request.reset();
        m_taleBusy.store(false);
        const bool onTheRoad = (a.flags & A::Flag::CameFar) != 0;
        std::string hook = result.hook;
        const std::string askWords = a.offerAsk;   // the asker's request, for the town's talk when the writer wrote none
        const auto id = PlanTale(std::move(a));
        auto* asker = RE::TESForm::LookupByID<RE::Actor>(request->asker);
        if (id && asker && request->rumored && !onTheRoad) {
            // Talk in town before anyone comes to the player: what people say of it goes round the hold (the innkeeper
            // brings it up, anyone may mention it), and the story waits until the player and the one who wants help
            // next meet, as a story does whose asker could not reach the player. Then the offer goes out (Lapse).
            {
                std::scoped_lock lock(m_mutex);
                if (auto* arc = m_book.Find(id)) {
                    arc->flags |= A::Flag::Deferred | A::Flag::Rumored;
                    arc->deadline = std::max(arc->deadline, NowDays() + 4.0f);
                }
                auto& pending = m_pending[id];
                pending.offerSent = false;
                pending.deferredAt = RealNow();
            }
            if (hook.empty()) {
                std::string ask = askWords;
                {
                    std::scoped_lock lock(m_mutex);
                    if (const auto* arc = m_book.Find(id)) ask = Fill(askWords, *arc);
                }
                hook = TroubleTalk(NameOf(request->asker), ask);
            }
            // Where to look for them is a fact of the game, not the writer's to leave out: talk that names no place
            // sends nobody anywhere.
            {
                std::scoped_lock lock(m_mutex);
                const Folk* me = FolkOf(request->asker);
                auto* homeCell = me && me->home ? RE::TESForm::LookupByID<RE::TESObjectCELL>(me->home) : nullptr;
                const char* homeName = homeCell ? homeCell->GetFullName() : nullptr;
                // Unless the writer named the place already ("of the Drunken Huntsman" names "The Drunken Huntsman").
                const auto lower = [](std::string s) {
                    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    return s;
                };
                std::string known = homeName ? lower(homeName) : std::string();
                if (known.starts_with("the ")) known.erase(0, 4);
                if (!known.empty() && lower(hook).find(known) == std::string::npos) {
                    hook += " " + NameOf(request->asker) + " lives at " + homeName + ".";
                }
            }
            RumorBridge::GetSingleton()->Note(Rumors::Kind::Trouble, hook, request->town, 0, request->asker, Rumors::Owner::Arc, id, 0.65f, true);
            logger::info("Tales: #{} begins as talk in town: {}", id, hook);
        } else if (id && asker && onTheRoad) {
            // From another hold: they are on the road for a good part of a day before they reach the player (Lapse
            // sends the offer then). Nothing is held meanwhile: no story slot, no visitor on the way.
            std::scoped_lock lock(m_mutex);
            m_pending[id].offerDue = NowDays() + kFarRoadDays;
            logger::info("Tales: #{} {} is on the road from another hold: with the player in about {:.0f} hours", id,
                         asker->GetDisplayFullName(), kFarRoadDays * 24.0f);
        } else if (id && asker) {
            SendOffer(id, asker);
        }
    }

    void ArcBridge::SendOffer(std::uint32_t id, RE::Actor* asker) {
        // Not in front of the people it is about: with one of them beside the player the asker does not even set
        // out; the story waits for the next moment the two are apart (Lapse asks again).
        if (auto* by = Company(id)) {
            const char* name = by->GetDisplayFullName();
            OfferDeferred(id, name && *name ? name : "someone");
            return;
        }
        // Nor does somebody go looking for the player who has no business to: a stranger, somebody with a post to
        // keep, anybody at all once the player is beyond their town's houses (Arcs.h, StaysPut). The story waits for
        // the two to meet, and is talk in town meanwhile.
        bool knowsPlayer = false;
        if (const auto why = WhyStays(id, asker, knowsPlayer); why != A::Stays::No) {
            OfferWaits(id, why, knowsPlayer);
            return;
        }
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
        LogOutline(*planned);
        return id;
    }

    void ArcBridge::LogOutline(const A::Arc& a) {
        static constexpr const char* kKinds[] = {"?", "talk", "find", "give", "choose", "brawl", "go", "visit", "wait", "delve", "slay", "ambush", "stalk", "free", "examine", "haunt"};
        static constexpr const char* kWhens[] = {"?", "done", "option 1", "option 2", "option 3", "picked", "dead", "has", "won", "lost",
                                                 "handed", "persuaded", "said 1", "said 2", "said 3", "waited", "found already", "rested"};
        for (std::size_t i = 0; i < a.graph.size(); ++i) {
            const auto& n = a.graph[i];
            std::string line = "Tales: #" + std::to_string(a.id) + " step " + std::to_string(i) + " " +
                               kKinds[std::min<std::size_t>(static_cast<std::size_t>(n.kind), std::size(kKinds) - 1)] + " " + NameOf(n.who ? a.cast[n.who] : 0) +
                               ": " + n.goal + " |";
            for (const auto& e : n.edges) {
                if (e.when == A::If{}) continue;
                line += " [" + std::string(kWhens[std::min<std::size_t>(static_cast<std::size_t>(e.when), std::size(kWhens) - 1)]) + " -> " +
                        (e.ending != A::Ending::None ? "end " + std::to_string(static_cast<int>(e.ending)) : "step " + std::to_string(static_cast<int>(e.next))) +
                        (e.effect != A::Effect::None ? ", effect " + std::to_string(static_cast<int>(e.effect)) : std::string()) + "]";
            }
            logger::info("{}", line);
        }
    }

    // ------------------------------------------------------------------ the judge of a talk

    void ArcBridge::TaleTalk(std::uint32_t id, const A::Arc& a, Pending& pending, const A::Facts& f) {
        if (a.node >= a.graph.size()) return;
        const auto& n = a.graph[a.node];
        if (n.kind != A::Kind::Talk || !n.who || n.turns[0].empty() || pending.judging || pending.turn >= 0) return;
        auto* speech = SpeechTracker::GetSingleton();
        const RE::FormID who = a.cast[n.who];
        // A new exchange since the last reading: the player said something after it, and the person answered after
        // that. Heimskr, preaching on, was read every two seconds on one greeting (2026-11-19).
        const float now = RealNow();
        const float playerSaid = speech->SecondsSincePlayerSpoke();
        const float theyEnded = speech->SecondsSinceSpeechEnded(who);
        const float playerAt = playerSaid >= 0.0f ? now - playerSaid : -1.0f;
        const float theirAt = theyEnded >= 0.0f ? now - theyEnded : -1.0f;
        // The player's FIRST line since the last reading is what the person's answer is measured against: a player
        // character who goes on talking unprompted (SkyrimNet voices the player's own remarks in some setups) moved
        // "the player's last line" past every answer, and Brannoc's two answers were never read (2026-11-19).
        if (pending.askedAt < 0.0f && playerAt > pending.judgedAt) pending.askedAt = playerAt;
        if (f.with[n.who] && f.spoke[n.who] && pending.askedAt >= 0.0f && theirAt > pending.askedAt) pending.talked = true;
        if (!pending.talked) return;
        if (speech->IsSpeechPending(who)) return;
        const float theirs = speech->SecondsSinceSpeechEnded(who);
        const float players = speech->SecondsSincePlayerSpoke();
        const bool quiet = theirs >= kQuietSeconds && (players < 0.0f || players >= kQuietSeconds);
        if (!quiet && f.with[n.who]) return;   // still talking (walking away ends the talk at once)
        pending.talked = false;
        pending.judging = true;
        pending.judgedAt = RealNow();
        pending.askedAt = -1.0f;
        // Not forced at first: a talk turns only when the conversation really came to one of its outcomes (an ignored
        // line or idle chatter forced into "the closest outcome" would move the story on a matter never spoken of).
        // But a talk that has really taken place does not hang for want of a prepared outcome: after three exchanges
        // with this person on this step that matched none of them, the nearest is named. (Played 2026-11-19: Nazeem,
        // freed, told all he knew, in character and four times over; neither outcome the story had prepared was
        // what he said, and the journal stood still at "Hear how they came to be taken".)
        SendJudge(id, a.node, who, n.goal, n.turns, pending.nones >= 3);
    }

    // ------------------------------------------------------------------ warmed while a save comes in
    //
    // Measured on a list of 1,500 plugins with the hook below: once warm, every part of a tale's request costs a few
    // thousandths of a millisecond. What made a session's first request 2.9 ms (and its pacer 1.4 ms in one frame) were
    // first calls: the user's lists read from SkyrimNet's settings (0.6 ms), the first "has this person had to do with
    // the player" (0.6: a relationship mod's factions looked up by plugin name), the first line of the heart (0.23),
    // the first look at the player's quests, a person's work and the items (0.07 to 0.12 each). They are made here,
    // once, right after a save has loaded, where a millisecond is not felt.
    void ArcBridge::WarmUp() {
        const auto started = std::chrono::steady_clock::now();
        RE::Actor* who = nullptr;
        RE::FormID town = 0;
        for (const auto& f : m_folk) {
            who = RE::TESForm::LookupByID<RE::Actor>(f.ref);
            if (who) {
                town = f.town;
                break;
            }
        }
        if (!who) return;
        std::string why;
        (void)Usable(who, 0, why);
        (void)KnowsPlayer(who, town);
        (void)BondBridge::GetSingleton()->Seen(who);
        (void)HeartBridge::WordsFor(who);
        (void)HeartBridge::LeanFor(who);
        (void)HeartBridge::ShyAdmirer(who);
        (void)PlayerQuests();
        (void)WorkOf(who);
        (void)TiesOf(who->GetActorBase());
        (void)ItemChoices();
        (void)ObjectiveLocations();
        (void)PlayerInTownProper(town);
        (void)QuestBusy(RealNow());
        logger::info("Arcs: first lookups made while the save came in ({:.2f} ms, with {} standing in for the town)",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(),
                     who->GetDisplayFullName() ? who->GetDisplayFullName() : "someone");
    }

    // ------------------------------------------------------------------ what things cost (test hook)
    //
    // The parts of a tale's request, each run several times on the game's thread: the first call (which may fill a
    // table) and the least of the repeats (what it costs once warm, free of the scheduler's interruptions). One
    // reading of a spread job says little on a busy game thread: 0.5 ms "before the people" on a large mod list turned
    // out to be one thing, and guessing at it had been wrong before.
    void ArcBridge::DebugCosts(RE::Actor* who) {
        if (!who) return;
        const auto time = [](const std::function<void()>& fn, int runs) {
            double first = 0.0, least = 1.0e9;
            for (int i = 0; i < runs; ++i) {
                const auto t = std::chrono::steady_clock::now();
                fn();
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
                if (i == 0) first = ms;
                else least = std::min(least, ms);
            }
            return std::pair<double, double>(first, least);
        };
        const Folk* me = FolkOf(who->GetFormID());
        const RE::FormID town = me ? me->town : 0;
        const auto lists = time([&] { (void)NPCIndex::BlockedForStories(who); }, 6);
        const auto usable = time([&] {
            std::string why;
            (void)Usable(who, 0, why);
        }, 6);
        const auto knows = time([&] { (void)KnowsPlayer(who, town); }, 6);
        const auto seen = time([&] { (void)BondBridge::GetSingleton()->Seen(who); }, 6);
        const auto heart = time([&] { (void)HeartBridge::WordsFor(who); }, 6);
        const auto lean = time([&] { (void)HeartBridge::LeanFor(who); }, 6);
        const auto shy = time([&] { (void)HeartBridge::ShyAdmirer(who); }, 6);
        const auto quests = time([&] { (void)PlayerQuests(); }, 4);
        const auto inTown = time([&] { (void)PlayerInTownProper(town); }, 6);
        const auto bad = time([&] { (void)LivingWorldBridge::GetSingleton()->IsBadMoment(); }, 6);
        const auto work = time([&] {
            (void)WorkOf(who);
            (void)TiesOf(who->GetActorBase());
        }, 6);
        const auto items = time([&] { (void)ItemChoices(); }, 4);
        const auto places = time([&] { (void)ObjectiveLocations(); }, 4);
        const auto asked = time([&] { (void)AskedWithin(who->GetFormID(), 7.0f); }, 6);
        const auto talk = time([&] { (void)PlayerTalksWithAnother(who->GetFormID(), 45.0f); }, 6);
        const auto busy = time([&] { (void)QuestBusy(RealNow()); }, 6);
        const auto told = time([&] {
            std::scoped_lock lock(m_mutex);
            float last = -1.0f;
            for (const auto& a : m_book.all) last = std::max(last, a.created);
            (void)last;
        }, 6);
        const auto name = who->GetDisplayFullName();
        logger::info("Arcs: what things cost for {} (ms, the first call / the least of the repeats): the user's lists {:.3f}/{:.3f}, usable "
                     "{:.3f}/{:.3f}, knows the player {:.3f}/{:.3f}, bond {:.3f}/{:.3f}, heart's words {:.3f}/{:.3f}, heart's lean {:.3f}/{:.3f}, "
                     "shy admirer {:.3f}/{:.3f}, the player's quests {:.3f}/{:.3f}, in town proper {:.3f}/{:.3f}, bad moment {:.3f}/{:.3f}, "
                     "work and ties {:.3f}/{:.3f}, items {:.3f}/{:.3f}, places quests need {:.3f}/{:.3f}, asked lately {:.3f}/{:.3f}, in talk "
                     "{:.3f}/{:.3f}, quest gate {:.3f}/{:.3f}, the book {:.3f}/{:.3f}",
                     name ? name : "?", lists.first, lists.second, usable.first, usable.second, knows.first, knows.second, seen.first, seen.second,
                     heart.first, heart.second, lean.first, lean.second, shy.first, shy.second, quests.first, quests.second, inTown.first,
                     inTown.second, bad.first, bad.second, work.first, work.second, items.first, items.second, places.first, places.second,
                     asked.first, asked.second, talk.first, talk.second, busy.first, busy.second, told.first, told.second);
    }

    // ------------------------------------------------------------------ a yes or a no already spoken
    //
    // An offer was answered by a window and by nothing else: a player who said "Yes, I will look into it" to the one who
    // asked got the window all the same, and what the dialogue model does with such a line cannot be relied on (some
    // models agree in words and run no action). The talk is read by the reader of decisions instead, with a yes and a
    // no as its two answers; anything less brings the window, as before.

    namespace {
        constexpr std::uint8_t kOfferNode = 0xFE;   // SendJudge's "step" for the reading of an offer
    }

    void ArcBridge::ReadOffer(std::uint32_t id) {
        std::scoped_lock lock(m_mutex);
        const auto* a = m_book.Find(id);
        if (!a || a->phase != A::Phase::Planned) return;
        auto& pending = m_pending[id];
        if (pending.offerSpoken == -1 && RealNow() - pending.offerReadAt < 25.0f) return;   // being read
        if (!SkyrimNetAPI::SendCustomDecisionToLLM && !SkyrimNetAPI::SendCustomPromptToLLM) {
            pending.offerSpoken = 0;
            return;
        }
        pending.offerSpoken = -1;
        pending.offerReadAt = RealNow();
        const std::string player = PlayerName();
        const std::string ask = Fill(a->offerAsk.empty() ? A::OfferPrompt(*a) : a->offerAsk, *a);
        SendJudge(id, kOfferNode, a->cast[A::RAsker], "whether to take on what " + NameOf(a->cast[A::RAsker]) + " has just asked: \"" + ask + "\"",
                  {player + " says yes: they will do it, look into it, or help.", player + " says no: they will not do it, or not now.", std::string()},
                  false, true, 150.0f);
    }

    int ArcBridge::OfferSpoken(std::uint32_t id) {
        std::scoped_lock lock(m_mutex);
        const auto it = m_pending.find(id);
        if (it == m_pending.end()) return 0;
        auto& pending = it->second;
        // A reading that never came back is nothing decided.
        if (pending.offerSpoken == -1 && RealNow() - pending.offerReadAt > 25.0f) pending.offerSpoken = 0;
        return pending.offerSpoken == -2 ? 0 : pending.offerSpoken;
    }

    // ------------------------------------------------------------------ a decision already spoken
    //
    // A decision step asks the player to decide something in front of someone, by a window with the answers. A player
    // who talks has often just said it: "Ulfberth, your wife is faithful. There is no man. She has been hiding a
    // debt..." was answered by Ulfberth in full, and then the window asked what to tell Ulfberth (the play session of
    // 2026-11-22). So the talk is read first, by the same reader that reads a talk step, with the answers as its
    // outcomes: an answer the player has plainly given is taken as given; anything less brings the window as before.
    // Never for an answer that needs the player's Speech (the window carries that), and only what was said since the
    // step became the story's step.

    bool ArcBridge::ChoiceUnread(const A::Arc& a, const Pending& pending) const {
        if (a.pattern != A::Pattern::Tale || a.node >= a.graph.size()) return false;
        const auto& n = a.graph[a.node];
        if (n.kind != A::Kind::Choose || pending.spoken || pending.answer >= 0) return false;
        for (const auto& e : n.edges) {
            if (e.when == A::If::Persuaded) return false;
        }
        if (!SkyrimNetAPI::SendCustomDecisionToLLM && !SkyrimNetAPI::SendCustomPromptToLLM) return false;   // nobody to read it
        // A reading that never came back does not keep the window shut.
        if (pending.judging && RealNow() - pending.judgedAt > 25.0f) return false;
        return true;
    }

    void ArcBridge::ChoiceTalk(std::uint32_t id, const A::Arc& a, Pending& pending, const A::Facts& f) {
        if (!ChoiceUnread(a, pending) || pending.judging) return;
        const auto& n = a.graph[a.node];
        if (!n.who || !f.with[n.who] || !f.spoke[n.who]) return;
        auto* speech = SpeechTracker::GetSingleton();
        const RE::FormID who = a.cast[n.who];
        // Read once both have gone quiet: the window no longer cuts into what the person is still saying.
        if (speech->IsSpeechPending(who)) return;
        const float theirs = speech->SecondsSinceSpeechEnded(who);
        const float players = speech->SecondsSincePlayerSpoke();
        if ((theirs >= 0.0f && theirs < kQuietSeconds) || (players >= 0.0f && players < kQuietSeconds)) return;
        const float now = RealNow();
        // The player said nothing since this became the step: there is nothing to read, the window asks.
        const float since = pending.nodeAt >= 0.0f ? now - pending.nodeAt : kTalkWindow;
        if (players < 0.0f || players > since) {
            pending.spoken = 1;
            return;
        }
        pending.judging = true;
        pending.judgedAt = now;
        SendJudge(id, a.node, who, n.question.empty() ? n.goal : n.question, n.options, false, true, since + 5.0f);
    }

    bool ArcBridge::Parley(std::uint32_t id, const A::Arc& a, Pending& pending, const A::Node& n, float nearest, std::string& why) {
        auto* speech = SpeechTracker::GetSingleton();
        const RE::FormID who = a.thug[0];
        const float real = RealNow();
        // A menu or the chat box holds everything (typing an answer takes time), and a moment after it closes.
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
            pending.parleyHold = real + 6.0f;
            return false;
        }
        const float since = real - pending.parleyAt;
        const float theirEnded = speech->SecondsSinceSpeechEnded(who);   // < 0: never spoke
        const bool line = speech->IsSpeechPending(who);
        const bool spoke = theirEnded >= 0.0f && real - theirEnded >= pending.parleyAt;
        const float playerSaid = speech->SecondsSincePlayerSpoke();
        const float playerAt = playerSaid >= 0.0f ? real - playerSaid : -1.0f;
        const bool answered = playerAt > pending.parleyAt;
        if (nearest > 2500.0f) return why = "the player walked off", true;
        if (!spoke) {
            // The line waits its turn behind others (eighteen seconds once, behind the player character's own
            // remarks); when it never comes there is no word, then.
            if ((!line && since > 25.0f) || since > 45.0f) return why = "the leader's word never came", true;
            return false;
        }
        if (line || real < pending.parleyHold) return false;
        if (n.turns[0].empty()) {
            // Nothing they would settle for: the word was a threat, and they act on it.
            if (theirEnded >= 2.0f) return why = "the leader has had the word", true;
            return false;
        }
        if (!answered) {
            if (theirEnded >= 12.0f) return why = "the player gave no answer", true;
            return false;
        }
        if (since > 180.0f) return why = "the talk went on too long", true;
        if (pending.judging || pending.turn >= 0) return false;   // being read, or settled (the look's step takes it)
        // A talk is on: read like a talk step's, once per exchange (the player said something since the last
        // reading and the leader answered it), when both have gone quiet.
        const float theirAt = real - theirEnded;
        if (pending.askedAt < 0.0f && playerAt > pending.judgedAt) pending.askedAt = playerAt;
        if (pending.askedAt >= 0.0f && theirAt > pending.askedAt) pending.talked = true;
        if (pending.talked) {
            if (theirEnded < kQuietSeconds || playerSaid < kQuietSeconds) return false;
            pending.talked = false;
            pending.judging = true;
            pending.judgedAt = real;
            pending.askedAt = -1.0f;
            SendJudge(id, a.node, who, "The leader of men hired to waylay " + PlayerName() + " has stopped " + PlayerName() + " on the road. " +
                      Fill("They were paid to: " + n.why, a), n.turns, false);
            return false;
        }
        // Nothing settled yet. After the leader's last line the word is the player's, for a good while (reading it,
        // thinking, opening the chat box: Brannoc attacked four seconds after his own counter-offer, 2026-11-19);
        // after the player's it is the leader's. Three readings that came to nothing, and they have had enough.
        if (pending.nones >= 3) return why = "the talk came to nothing", true;
        if (playerAt > theirAt) {
            if (playerSaid >= 25.0f) return why = "the leader had no answer for the player", true;
            return false;
        }
        if (theirEnded >= 20.0f) return why = "the player had no more to say", true;
        return false;
    }

    void ArcBridge::SendJudge(std::uint32_t id, std::uint8_t node, RE::FormID who, std::string goal, std::array<std::string, 3> turns, bool force,
                              bool decision, float window) {
        const auto lines = DialogueTracker::GetSingleton()->ConversationWith(who, window > 0.0f ? std::min(window, kTalkWindow) : kTalkWindow, kTalkLines);
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
        ctx["decision"] = decision;
        nlohmann::json outcomes = nlohmann::json::array();
        const auto* arc = decision ? m_book.Find(id) : nullptr;   // the answers carry {placeholders} (lock held by the caller)
        for (std::size_t k = 0; k < turns.size(); ++k) {
            if (turns[k].empty()) continue;
            outcomes.push_back({{"key", "o" + std::to_string(k)}, {"text", arc ? Fill(turns[k], *arc) : turns[k]}});
        }
        ctx["outcomes"] = outcomes;
        const std::string context = ctx.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        logger::info("Tales: #{} reading the talk with {} ({} lines{}{})", id, name, lines.size(), force ? ", an outcome must be named" : "",
                     decision ? ", for a decision already spoken" : "");
        // The reading is taken in on the game's own thread, in a task of its own, never inside the call that asked for
        // it: the watch asks with this bridge's lock held (Advance), and a call that fails at once (no API, nothing
        // queued, no route for decisions) used to answer on the spot and take that lock a second time: a hang. The
        // names read for the log are the game's, too.
        const auto answer = [this, id, node](const std::string& choice) {
            int turn = -1;
            if (choice.size() == 2 && choice[0] == 'o' && choice[1] >= '0' && choice[1] <= '2') turn = choice[1] - '0';
            if (auto* task = SKSE::GetTaskInterface()) {
                task->AddTask([this, id, node, turn]() { Judged(id, node, turn); });
            }
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
                            choice = Safe::Text(json, "turn", "none");
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
                    const std::string error = Safe::Text(json, "error");
                    if (error == "no_decisions_route") return byChat();
                    return answer("none");
                }
                std::string choice = "none";
                if (json.is_object() && json.contains("answers") && json["answers"].is_object() && json["answers"].contains("turn") &&
                    json["answers"]["turn"].is_object()) {
                    choice = Safe::Text(json["answers"]["turn"], "choice", "none");
                }
                answer(choice);
            });
        if (!queued) byChat();
    }

    void ArcBridge::Judged(std::uint32_t id, std::uint8_t node, int turn) {
        std::scoped_lock lock(m_mutex);
        const auto* a = m_book.Find(id);
        auto it = m_pending.find(id);
        if (node == kOfferNode) {
            // The player's answer to an offer, as read from the talk.
            if (!a || a->phase != A::Phase::Planned || it == m_pending.end() || it->second.offerSpoken != -1) return;
            it->second.offerSpoken = turn == 0 ? 1 : turn == 1 ? 2 : 0;
            logger::info("Tales: #{} the offer in talk: {}", id,
                         turn == 0 ? "the player said yes" : turn == 1 ? "the player said no" : "nothing was decided: the window asks");
            return;
        }
        if (!a || !A::IsOpen(*a) || it == m_pending.end() || it->second.node != node || a->node != node) return;
        auto& pending = it->second;
        pending.judging = false;
        if (node < a->graph.size() && a->graph[node].kind == A::Kind::Choose) {
            // A decision step: the answer the player has already given in talk, or the window after all.
            const auto& n = a->graph[node];
            if (pending.spoken || RealNow() - pending.judgedAt > 25.0f) {
                pending.spoken = 1;   // the window is up already: its answer is the one that counts
                return;
            }
            if (turn >= 0 && turn < 3 && !n.options[static_cast<std::size_t>(turn)].empty()) {
                pending.answer = turn;
                pending.chooseHold = RealNow() + 30.0f;
                logger::info("Tales: #{} the decision was made in talk: {}", id, Fill(n.options[static_cast<std::size_t>(turn)], *a));
            } else {
                pending.spoken = 1;
                logger::info("Tales: #{} no decision was spoken in the talk: the window asks", id);
            }
            return;
        }
        if (turn >= 0 && turn < 3 && node < a->graph.size() && !a->graph[node].turns[static_cast<std::size_t>(turn)].empty()) {
            pending.turn = turn;
            logger::info("Tales: #{} the talk turned out: {}", id, Fill(a->graph[node].turns[static_cast<std::size_t>(turn)], *a));
        } else {
            ++pending.nones;
            logger::info("Tales: #{} the talk has not turned yet ({} readings)", id, pending.nones);
        }
    }
}
