#pragma once

// The ladder: what the player has done for a faction, the rank that earns them with it, and what a rank gives.
// Pure: no game types (tests/RanksCheck.cpp).
//
// Until 4.0 there was one number between the player and a faction, the standing: how well they are liked. It rose
// by fifteen for a battle won and sank by itself over time, and nothing came of it but who attacked on sight. Nobody
// knew what the player had done, nothing was to be gained by fighting a faction's battles but that number, and a
// hundredth battle was the same as the first.
//
// Renown is what they have done; it does not fade. A rank is renown the faction still honours (not while it takes
// the player for an enemy). A rank is known: to the faction's own, to its enemies, to those who talk. It pays
// better, puts soldiers at the player's side, and from Captain on the line waits for the player's word.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace IntelEngine::Ranks {

    struct Record {
        std::int32_t battles = 0;       // fought in its line to the end
        std::int32_t won = 0;           // of those, won
        std::int32_t kills = 0;         // of its enemies, in its battles
        std::int32_t champions = 0;     // champions of its enemies cut down
        std::int32_t captains = 0;      // captains of its enemies cut down
        std::int32_t saved = 0;         // attempts on its leaders' lives ended by the player's hand
        std::int32_t quests = 0;        // things done at its asking
        std::int32_t deserted = 0;      // battles walked away from
        std::int32_t renown = 0;
    };

    enum class Deed { Fought, Won, Kill, Champion, Captain, Saved, Quest, Deserted, StruckOwn };

    inline constexpr int kKillsCountedABattle = 8;      // a battle is not a tally sheet

    inline constexpr int Worth(Deed deed) {
        switch (deed) {
            case Deed::Fought: return 6;
            case Deed::Won: return 6;           // on top of having fought
            case Deed::Kill: return 1;
            case Deed::Champion: return 12;
            case Deed::Captain: return 6;
            case Deed::Saved: return 10;
            case Deed::Quest: return 8;
            case Deed::Deserted: return -15;
            case Deed::StruckOwn: return -4;
        }
        return 0;
    }

    inline void Earn(Record& r, Deed deed, int times = 1) {
        if (times <= 0) return;
        int counted = times;
        switch (deed) {
            case Deed::Fought: r.battles += times; break;
            case Deed::Won: r.won += times; break;
            case Deed::Kill:
                r.kills += times;
                counted = (std::min)(times, kKillsCountedABattle);
                break;
            case Deed::Champion: r.champions += times; break;
            case Deed::Captain: r.captains += times; break;
            case Deed::Saved: r.saved += times; break;
            case Deed::Quest: r.quests += times; break;
            case Deed::Deserted: r.deserted += times; break;
            case Deed::StruckOwn: break;
        }
        r.renown = (std::max)(0, r.renown + Worth(deed) * counted);
    }

    // ---- the ranks

    inline constexpr int kRanks = 6;
    inline constexpr int kNeeded[kRanks] = {0, 10, 30, 70, 130, 220};

    inline constexpr int RankOf(int renown) {
        int rank = 0;
        for (int i = 1; i < kRanks; ++i) {
            if (renown >= kNeeded[i]) rank = i;
        }
        return rank;
    }
    // Renown still to earn for the next rank; 0 at the top.
    inline constexpr int ToNext(int renown) {
        const int rank = RankOf(renown);
        return rank + 1 < kRanks ? kNeeded[rank + 1] - renown : 0;
    }
    // A rank the faction honours: none while it takes the player for an enemy.
    inline constexpr int Honoured(int renown, int standing, int hostileAt) { return standing <= hostileAt ? 0 : RankOf(renown); }

    // What kind of body a faction is decides what its ranks are called.
    enum class Kind { Army, Guild, House };
    inline Kind KindOf(std::string_view type) {
        std::string lower(type);
        for (auto& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (lower.find("guild") != std::string::npos || lower.find("order") != std::string::npos || lower.find("college") != std::string::npos ||
            lower.find("crim") != std::string::npos) {
            return Kind::Guild;
        }
        if (lower.find("famil") != std::string::npos || lower.find("house") != std::string::npos || lower.find("clan") != std::string::npos ||
            lower.find("noble") != std::string::npos || lower.find("polit") != std::string::npos) {
            return Kind::House;
        }
        return Kind::Army;
    }
    inline constexpr const char* Title(Kind kind, int rank) {
        constexpr const char* kArmy[kRanks] = {"", "Auxiliary", "Sworn Sword", "Veteran", "Captain", "Champion"};
        constexpr const char* kGuild[kRanks] = {"", "Associate", "Trusted Hand", "Veteran", "Lieutenant", "Right Hand"};
        constexpr const char* kHouse[kRanks] = {"", "Hired Blade", "Retainer", "Trusted Retainer", "Sworn Captain", "Champion of the House"};
        if (rank <= 0 || rank >= kRanks) return "";
        return kind == Kind::Guild ? kGuild[rank] : kind == Kind::House ? kHouse[rank] : kArmy[rank];
    }

    // ---- what a rank gives

    // The winning side's pay for one who fought in its line grows with their rank.
    inline constexpr int Pay(int basePay, int rank) { return basePay + basePay * (rank < 0 ? 0 : rank) / 4; }
    // A faction calls those who hold a rank with it when its army gathers (a journal entry, the place marked).
    inline constexpr bool Called(int rank) { return rank >= 1; }
    // When both armies would call the player, the one they stand higher with does; equal ranks, neither: they are
    // left to choose for themselves. 0 nobody, 1 the first, 2 the second.
    inline constexpr int Caller(int rankFirst, int rankSecond) {
        if (rankFirst > rankSecond && Called(rankFirst)) return 1;
        if (rankSecond > rankFirst && Called(rankSecond)) return 2;
        return 0;
    }
    // Soldiers who keep by the player in a battle: a file of their own.
    inline constexpr int FileSize(int rank) { return rank >= 4 ? 4 : rank >= 2 ? 2 : 0; }
    // From Captain on the line waits for the player's word to charge.
    inline constexpr bool GivesTheWord(int rank) { return rank >= 4; }
    // Known beyond the faction itself: people who are none of its own have heard the name.
    inline constexpr bool Famous(int rank) { return rank >= 3; }

    // What the faction's own know of the player, in a sentence for whoever speaks of them. `the`: "the Imperial Legion".
    inline std::string KnownAs(const Record& r, int rank, Kind kind, const std::string& the) {
        if (rank <= 0 && r.battles == 0 && r.saved == 0 && r.quests == 0) return {};
        std::string out;
        if (rank > 0) {
            const std::string title = Title(kind, rank);
            const char first = title.empty() ? 'x' : static_cast<char>(title[0] | 0x20);
            out = std::string(first == 'a' || first == 'e' || first == 'i' || first == 'o' || first == 'u' ? "an " : "a ") + title + " of " + the;
        } else {
            out = "someone who has stood with " + the;
        }
        std::vector<std::string> deeds;
        if (r.battles > 0) {
            deeds.push_back("fought in " + std::to_string(r.battles) + (r.battles == 1 ? " battle of theirs" : " of their battles") +
                            (r.won > 0 ? " (" + std::to_string(r.won) + " won)" : std::string()));
        }
        if (r.champions > 0) deeds.push_back("cut down " + std::to_string(r.champions) + (r.champions == 1 ? " champion" : " champions") + " of their enemies");
        if (r.captains > 0) deeds.push_back("killed " + std::to_string(r.captains) + (r.captains == 1 ? " enemy captain" : " enemy captains"));
        if (r.saved > 0) deeds.push_back("saved the life of " + std::string(r.saved == 1 ? "one of their leaders" : "their leaders more than once"));
        if (r.deserted > 0) deeds.push_back("walked away from " + std::to_string(r.deserted) + (r.deserted == 1 ? " battle" : " battles"));
        for (std::size_t i = 0; i < deeds.size(); ++i) out += (i == 0 ? ": " : i + 1 == deeds.size() ? ", and " : ", ") + deeds[i];
        return out;
    }

    // ---- kept with the save

    inline constexpr std::uint32_t kRecordType = 'IERK';
    inline constexpr std::uint32_t kRecordVersion = 1;

    using Book = std::map<std::string, Record>;

    inline std::vector<std::uint8_t> Serialize(const Book& book) {
        std::vector<std::uint8_t> out;
        const auto put32 = [&out](std::uint32_t v) {
            for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        };
        put32(static_cast<std::uint32_t>(book.size()));
        for (const auto& [faction, r] : book) {
            put32(static_cast<std::uint32_t>(faction.size()));
            out.insert(out.end(), faction.begin(), faction.end());
            for (const std::int32_t v : {r.battles, r.won, r.kills, r.champions, r.captains, r.saved, r.quests, r.deserted, r.renown}) {
                put32(static_cast<std::uint32_t>(v));
            }
        }
        return out;
    }

    inline bool Deserialize(const std::vector<std::uint8_t>& bytes, Book& book) {
        std::size_t at = 0;
        const auto get32 = [&bytes, &at](std::uint32_t& v) {
            if (at + 4 > bytes.size()) return false;
            v = 0;
            for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(bytes[at + i]) << (8 * i);
            at += 4;
            return true;
        };
        Book loaded;
        std::uint32_t count = 0;
        if (!get32(count) || count > 512) return false;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t size = 0;
            if (!get32(size) || size > 256 || size > bytes.size() - at) return false;
            std::string faction(reinterpret_cast<const char*>(bytes.data() + at), size);
            at += size;
            Record r;
            for (std::int32_t* field : {&r.battles, &r.won, &r.kills, &r.champions, &r.captains, &r.saved, &r.quests, &r.deserted, &r.renown}) {
                std::uint32_t v = 0;
                if (!get32(v)) return false;
                *field = static_cast<std::int32_t>(v);
            }
            loaded.emplace(std::move(faction), r);
        }
        book = std::move(loaded);
        return true;
    }
}
