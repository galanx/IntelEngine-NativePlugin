// IntelEngine: what stands between the player and someone, read from the game (HeartBridge.h, Hearts.h).

#include "HeartBridge.h"
#include "Safe.h"

#include "LivingWorldBridge.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <mutex>
#include <unordered_map>

namespace IntelEngine::HeartBridge {

    namespace {
        // Skyrim.esm
        constexpr RE::FormID kPlayerMarriedFaction = 0x0C6472;      // wed to the player (relationship mods add theirs too)
        constexpr RE::FormID kPotentialMarriageFaction = 0x019809;  // whom the game itself lets the player marry
        constexpr RE::FormID kSpouseAssociation = 0x0142CA;
        constexpr RE::FormID kAmuletOfMara = 0x0C891B;
        // SkyrimNet Relationships (SNRom_Integration.esp); its README invites other mods to read these.
        constexpr const char* kRelationships = "SNRom_Integration.esp";
        constexpr RE::FormID kBondFaction = 0xD63;       // member = tracked, rank = tier 0..5
        constexpr RE::FormID kMarriageFaction = 0xD70;   // rank 1 engaged, 2 married, 3 broken off, 4 divorced, 5 widowed, 6 separated

        struct Script {
            bool sparked = false;
            std::int8_t stance = 0;
            std::uint8_t withChild = 0;
            bool childIsPlayers = false;
            std::uint8_t children = 0;
            bool playersChild = false;
            float refusedAt = -1.0f;   // the game day the player was first seen to have turned them down
        };
        std::mutex g_mutex;
        std::unordered_map<RE::FormID, Script> g_told;

        // The factions that say how it stands, and whether an intimacy mod is installed. Looked up once: asked for by
        // a plugin's name, the game walks its whole load order, and on a big mod list four such walks a person were
        // most of the time a story's request took in its frame (1.7 of 4.2 ms for ten people, 2026-11-21). The load
        // order does not change while the game runs.
        struct Known {
            bool looked = false;
            RE::TESFaction* playerMarried = nullptr;
            RE::TESFaction* potentialMarriage = nullptr;
            RE::TESFaction* bond = nullptr;
            RE::TESFaction* marriage = nullptr;
            bool intimacyMod = false;
        };
        Known TheKnown() {
            static Known known;
            static std::mutex guard;
            std::scoped_lock lock(guard);
            if (!known.looked) {
                if (auto* data = RE::TESDataHandler::GetSingleton()) {
                    known.playerMarried = data->LookupForm<RE::TESFaction>(kPlayerMarriedFaction, "Skyrim.esm");
                    known.potentialMarriage = data->LookupForm<RE::TESFaction>(kPotentialMarriageFaction, "Skyrim.esm");
                    known.bond = data->LookupForm<RE::TESFaction>(kBondFaction, kRelationships);
                    known.marriage = data->LookupForm<RE::TESFaction>(kMarriageFaction, kRelationships);
                    known.intimacyMod = data->LookupModByName("OStim.esp") != nullptr || data->LookupModByName("OStimNet.esp") != nullptr;
                    // Kept only once the game's own records are there: before that there is nothing to find.
                    known.looked = known.playerMarried != nullptr;
                }
            }
            return known;
        }

        // The ranks in those factions, -1 when not a member: one pass over what somebody belongs to. The game's own
        // record is looked at first, then what has changed since (joined, left, promoted): the last word counts, and a
        // rank below nothing means they have left.
        struct Ranks {
            int playerMarried = -1, potentialMarriage = -1, bond = -1, marriage = -1;
        };
        Ranks RanksOf(RE::Actor* actor, const Known& known) {
            Ranks ranks;
            if (!actor) return ranks;
            actor->VisitFactions([&](RE::TESFaction* f, std::int8_t r) {
                if (!f) return false;
                const int rank = r < 0 ? -1 : r;
                if (f == known.playerMarried) ranks.playerMarried = rank;
                else if (f == known.potentialMarriage) ranks.potentialMarriage = rank;
                else if (f == known.bond) ranks.bond = rank;
                else if (f == known.marriage) ranks.marriage = rank;
                return false;
            });
            return ranks;
        }

        // The game's own rank between the two: 4 lover .. 0 acquaintance .. -4 archnemesis.
        int GameRank(RE::TESNPC* npc, RE::TESNPC* pc) {
            if (!npc || !pc) return 0;
            auto* tie = RE::BGSRelationship::GetRelationship(npc, pc);
            return tie ? 4 - static_cast<int>(tie->level.get()) : 0;
        }

        // The game's own sign that the player is looking for someone: without it the people one can marry say nothing
        // of the kind, and neither does a story (nobody is courted who did not ask to be).
        bool PlayerWearsAmuletOfMara() {
            // (Asked for every person of a story's request in one frame: the player's things are gone through once a
            // quarter of a second, not once a person.)
            static std::atomic<long long> lookedAt{0};
            static std::atomic<bool> worn{false};
            const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (lookedAt.load() != 0 && now - lookedAt.load() < 250) return worn.load();
            bool found = false;
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* changes = player ? player->GetInventoryChanges() : nullptr;
            if (changes && changes->entryList) {
                for (auto* entry : *changes->entryList) {
                    if (entry && entry->object && entry->object->GetFormID() == kAmuletOfMara && entry->IsWorn()) {
                        found = true;
                        break;
                    }
                }
            }
            worn.store(found);
            lookedAt.store(now == 0 ? 1 : now);
            return found;
        }

        bool WedToSomeoneElse(RE::TESNPC* npc, RE::TESNPC* pc) {
            if (!npc || !npc->relationships) return false;
            for (auto* rel : *npc->relationships) {
                if (!rel || !rel->assocType || rel->assocType->GetFormID() != kSpouseAssociation) continue;
                auto* other = rel->npc1 == npc ? rel->npc2 : rel->npc1;
                if (other && other != npc && other != pc) return true;
            }
            return false;
        }

        // Family of the player as the game records it (a parent, a child, a sibling): any association but a spouse or
        // a courtship.
        bool KinOfPlayer(RE::TESNPC* npc, RE::TESNPC* pc) {
            if (!npc || !pc || !npc->relationships) return false;
            for (auto* rel : *npc->relationships) {
                if (!rel || !rel->assocType) continue;
                auto* other = rel->npc1 == npc ? rel->npc2 : rel->npc1;
                if (other != pc) continue;
                const char* label = rel->assocType->associationLabels[0][0].c_str();
                std::string word = label ? label : "";
                std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (rel->assocType->GetFormID() == kSpouseAssociation || word.find("spouse") != std::string::npos ||
                    word.find("court") != std::string::npos || word.find("lover") != std::string::npos) {
                    continue;
                }
                return true;
            }
            return false;
        }
    }

    bool Childlike(RE::Actor* actor) {
        if (!actor) return true;   // unknown is treated as a child: nothing of the heart is ever told on a guess
        if (actor->IsChild()) return true;
        auto* base = actor->GetActorBase();
        if (!base) return true;
        if (auto* race = actor->GetRace(); race && race->IsChildRace()) return true;
        if (auto* voice = base->voiceType) {
            std::string id = voice->GetFormEditorID() ? voice->GetFormEditorID() : "";
            std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (id.find("child") != std::string::npos) return true;
        }
        // Follower mods ship children as small adults of a custom race (a "FemaleChild" voice and 0.79 of a grown
        // person's height in one the user plays with): size counts too.
        return base->GetHeight() < 0.86f;
    }

    Hearts::Who WhoOf(RE::Actor* actor) {
        Hearts::Who who;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* base = actor ? actor->GetActorBase() : nullptr;
        if (!actor || !base || !player || actor == player) return who;
        who.childlike = Childlike(actor);
        const char* name = actor->GetDisplayFullName();
        who.person = base->IsUnique() && name && *name && !actor->IsDead();
        who.kin = KinOfPlayer(base, player->GetActorBase());
        std::scoped_lock lock(g_mutex);
        if (const auto it = g_told.find(actor->GetFormID()); it != g_told.end() && it->second.playersChild) who.kin = true;
        return who;
    }

    Hearts::Heart HeartOf(RE::Actor* actor) {
        Hearts::Heart h;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* base = actor ? actor->GetActorBase() : nullptr;
        if (!actor || !base || !player || actor == player) return h;
        auto* pc = player->GetActorBase();
        const Known known = TheKnown();
        const Ranks ranks = RanksOf(actor, known);
        const bool wedToPlayer = ranks.playerMarried >= 0;
        const bool wedElsewhere = WedToSomeoneElse(base, pc);
        // Without a relationship mod the game's own rule stands in: someone it lets the player marry, on friendly
        // terms, while the player wears an Amulet of Mara.
        const bool candidate = ranks.potentialMarriage >= 0 && PlayerWearsAmuletOfMara();
        h = Hearts::FromTheGame(GameRank(base, pc), candidate, wedToPlayer, wedElsewhere);

        // A relationship mod keeps it: its word counts, the game's rank was only the fallback.
        if (const int tier = ranks.bond; tier >= 0) {
            h.tracked = true;
            h.tier = static_cast<std::uint8_t>(std::min(tier, 5));
            h.sparked = false;   // its own flag decides (told by the script below)
        }
        switch (ranks.marriage) {
            case 1: h.wed = Hearts::Wed::Engaged; break;
            case 2: h.wed = Hearts::Wed::Married; break;
            case 3: case 4: case 6: h.wed = wedToPlayer ? Hearts::Wed::Married : Hearts::Wed::Ended; break;
            case 5: h.wed = Hearts::Wed::Widowed; break;
            default: break;
        }
        if (h.wed == Hearts::Wed::Married || h.wed == Hearts::Wed::Engaged) h.wedElsewhere = false;

        std::scoped_lock lock(g_mutex);
        if (const auto it = g_told.find(actor->GetFormID()); it != g_told.end()) {
            const auto& s = it->second;
            if (h.tracked) h.sparked = s.sparked;
            else h.sparked = h.sparked || s.sparked;
            h.stance = s.stance;
            if (s.stance < 0 && s.refusedAt >= 0.0f) {
                auto* calendar = RE::Calendar::GetSingleton();
                const float now = calendar ? calendar->GetCurrentGameTime() : 0.0f;
                h.refusedDays = std::max(0.0f, now - s.refusedAt);
            }
            h.withChild = s.withChild;
            h.childIsPlayers = s.childIsPlayers;
            h.children = s.children;
        }
        return h;
    }

    Hearts::Rules RulesNow() {
        Hearts::Rules rules;
        rules.stories = !LivingWorldBridge::GetSingleton()->IsClassic();
        // The user's own condition for affairs: only where an intimacy mod is installed.
        rules.affairs = TheKnown().intimacyMod;
        return rules;
    }

    Hearts::Lean LeanFor(RE::Actor* actor) { return Hearts::LeanOf(WhoOf(actor), HeartOf(actor), RulesNow()); }

    std::string WordsFor(RE::Actor* actor) {
        if (!actor || !Hearts::MayHaveAHeartStory(WhoOf(actor))) return {};
        auto* player = RE::PlayerCharacter::GetSingleton();
        const char* name = player ? player->GetDisplayFullName() : nullptr;
        return Hearts::Words(HeartOf(actor), name && *name ? name : "the traveller");
    }

    bool SecondChanceFor(RE::Actor* actor) { return actor && Hearts::SecondChance(HeartOf(actor)); }

    void Told(std::uint32_t actor, int sparked, int stance, int withChild, bool childIsPlayers, int children, bool playersChild, float refusedAt) {
        if (!actor) return;
        Script s;
        s.refusedAt = refusedAt;
        s.sparked = sparked != 0;
        s.stance = static_cast<std::int8_t>(std::clamp(stance, -1, 1));
        s.withChild = static_cast<std::uint8_t>(std::clamp(withChild, 0, 3));
        s.childIsPlayers = childIsPlayers;
        s.children = static_cast<std::uint8_t>(std::clamp(children, 0, 20));
        s.playersChild = playersChild;
        std::scoped_lock lock(g_mutex);
        g_told[actor] = s;
    }

    void Forget() {
        std::scoped_lock lock(g_mutex);
        g_told.clear();
    }

}
