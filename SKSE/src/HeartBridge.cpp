// IntelEngine: what stands between the player and someone, read from the game (HeartBridge.h, Hearts.h).

#include "HeartBridge.h"
#include "Safe.h"

#include "LivingWorldBridge.h"

#include <algorithm>
#include <cctype>
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
        };
        std::mutex g_mutex;
        std::unordered_map<RE::FormID, Script> g_told;

        // The rank in a faction, -1 when not a member.
        int RankIn(RE::Actor* actor, RE::TESFaction* faction) {
            if (!actor || !faction) return -1;
            // The game's own record is looked at first, then what has changed since (joined, left, promoted): the
            // last word counts, and a rank below nothing means they have left.
            int rank = -1;
            actor->VisitFactions([&](RE::TESFaction* f, std::int8_t r) {
                if (f == faction) rank = r;
                return false;
            });
            return rank < 0 ? -1 : rank;
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
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* changes = player ? player->GetInventoryChanges() : nullptr;
            if (!changes || !changes->entryList) return false;
            for (auto* entry : *changes->entryList) {
                if (entry && entry->object && entry->object->GetFormID() == kAmuletOfMara && entry->IsWorn()) return true;
            }
            return false;
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
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* base = actor ? actor->GetActorBase() : nullptr;
        if (!actor || !base || !player || !data || actor == player) return h;
        auto* pc = player->GetActorBase();
        const bool wedToPlayer = RankIn(actor, data->LookupForm<RE::TESFaction>(kPlayerMarriedFaction, "Skyrim.esm")) >= 0;
        const bool wedElsewhere = WedToSomeoneElse(base, pc);
        // Without a relationship mod the game's own rule stands in: someone it lets the player marry, on friendly
        // terms, while the player wears an Amulet of Mara.
        const bool candidate = RankIn(actor, data->LookupForm<RE::TESFaction>(kPotentialMarriageFaction, "Skyrim.esm")) >= 0 &&
                               PlayerWearsAmuletOfMara();
        h = Hearts::FromTheGame(GameRank(base, pc), candidate, wedToPlayer, wedElsewhere);

        // A relationship mod keeps it: its word counts, the game's rank was only the fallback.
        if (const int tier = RankIn(actor, data->LookupForm<RE::TESFaction>(kBondFaction, kRelationships)); tier >= 0) {
            h.tracked = true;
            h.tier = static_cast<std::uint8_t>(std::min(tier, 5));
            h.sparked = false;   // its own flag decides (told by the script below)
        }
        switch (RankIn(actor, data->LookupForm<RE::TESFaction>(kMarriageFaction, kRelationships))) {
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
        auto* data = RE::TESDataHandler::GetSingleton();
        rules.affairs = data && (data->LookupModByName("OStim.esp") != nullptr || data->LookupModByName("OStimNet.esp") != nullptr);
        return rules;
    }

    Hearts::Lean LeanFor(RE::Actor* actor) { return Hearts::LeanOf(WhoOf(actor), HeartOf(actor), RulesNow()); }

    std::string WordsFor(RE::Actor* actor) {
        if (!actor || !Hearts::MayHaveAHeartStory(WhoOf(actor))) return {};
        auto* player = RE::PlayerCharacter::GetSingleton();
        const char* name = player ? player->GetDisplayFullName() : nullptr;
        return Hearts::Words(HeartOf(actor), name && *name ? name : "the traveller");
    }

    void Told(std::uint32_t actor, int sparked, int stance, int withChild, bool childIsPlayers, int children, bool playersChild) {
        if (!actor) return;
        Script s;
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
