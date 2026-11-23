#pragma once

// Rumors: what is going round right now.
//
// The holds of Skyrim remember what happened in them (SkyrimNet's world knowledge, written by the quests, the stories
// and the lives of former followers: found when a conversation turns to it, and kept). That is a hold's long memory.
// This is the other thing: the talk of the day. What people have just heard, which they would bring up unasked at an
// inn, which travels to the next hold in a day or two and is forgotten in a week or two; and the word that someone
// is looking for help, which is how a player hears of trouble before anyone has come to them with it.
//
// Pure rules and a record, no game types: who knows a rumor, when it spreads, when it fades, what someone would tell
// the player first. RumorBridge.cpp puts it into the game (SkyrimNet entries for the holds it has reached, the
// prompt block of people who hear everything, an innkeeper bringing it up).

#include "Arcs.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace IntelEngine::Rumors {

    namespace A = IntelEngine::Arcs;

    // The holds, by their location records in Skyrim.esm, in a fixed order: the index is a rumor's bit for that hold.
    constexpr std::size_t kHolds = 9;
    enum Hold : std::uint8_t { Whiterun = 0, Falkreath, Reach, Haafingar, Hjaalmarch, Pale, Winterhold, Eastmarch, Rift, NoHold = 0xFF };
    constexpr std::array<std::uint32_t, kHolds> kHoldLocation = {0x00016772, 0x0001676F, 0x00016769, 0x00016770, 0x0001676E,
                                                                 0x0001676D, 0x0001676B, 0x0001676A, 0x0001676C};
    constexpr std::uint16_t Bit(std::uint8_t hold) { return static_cast<std::uint16_t>(1u << hold); }
    // Which holds border which: the roads a rumor travels.
    constexpr std::array<std::uint16_t, kHolds> kBorders = {
        static_cast<std::uint16_t>(Bit(Falkreath) | Bit(Reach) | Bit(Hjaalmarch) | Bit(Pale) | Bit(Eastmarch) | Bit(Rift)),   // Whiterun
        static_cast<std::uint16_t>(Bit(Whiterun) | Bit(Reach) | Bit(Rift)),                                                  // Falkreath
        static_cast<std::uint16_t>(Bit(Haafingar) | Bit(Hjaalmarch) | Bit(Whiterun) | Bit(Falkreath)),                       // the Reach
        static_cast<std::uint16_t>(Bit(Reach) | Bit(Hjaalmarch)),                                                            // Haafingar
        static_cast<std::uint16_t>(Bit(Haafingar) | Bit(Reach) | Bit(Whiterun) | Bit(Pale)),                                 // Hjaalmarch
        static_cast<std::uint16_t>(Bit(Hjaalmarch) | Bit(Whiterun) | Bit(Winterhold) | Bit(Eastmarch)),                      // the Pale
        static_cast<std::uint16_t>(Bit(Pale) | Bit(Eastmarch)),                                                              // Winterhold
        static_cast<std::uint16_t>(Bit(Winterhold) | Bit(Pale) | Bit(Whiterun) | Bit(Rift)),                                 // Eastmarch
        static_cast<std::uint16_t>(Bit(Eastmarch) | Bit(Whiterun) | Bit(Falkreath)),                                         // the Rift
    };
    inline std::uint8_t HoldIndex(std::uint32_t location) {
        for (std::size_t i = 0; i < kHolds; ++i) {
            if (kHoldLocation[i] == location) return static_cast<std::uint8_t>(i);
        }
        return NoHold;
    }

    enum class Kind : std::uint8_t {
        Deed = 1,     // how something ended (a quest, a story): what was done, and by whom
        Trouble = 2,  // something is wrong and someone wants it looked into: go to `lead`
        Life = 3,     // what a former follower of the player is doing with their life
        News = 4,     // what the powers of the land have done to each other
    };

    // What a rumor belongs to, so that it goes when its owner does (a story given up, a quest of another timeline).
    enum class Owner : std::uint8_t { None = 0, Arc = 1, Quest = 2, Life = 3, Storyline = 4, Politics = 5 };

    struct Rumor {
        std::uint32_t id = 0;
        Kind kind = Kind::Deed;
        std::string text;                 // as told where it began: third person, a sentence or two
        std::uint8_t home = NoHold;       // the hold it began in
        std::uint32_t about = 0;          // someone it is about: they do not pass it on
        std::uint32_t lead = 0;           // Trouble: who wants help (the one to go to); they do not gossip about it either
        Owner owner = Owner::None;
        std::uint32_t ownerId = 0;
        float born = 0.0f;                // game days
        float weight = 0.5f;              // 0..1: how much there is to it (how far it carries, how long it lasts)
        std::uint16_t heardIn = 0;        // bit per hold: where it is going round
        std::array<float, kHolds> heardAt{};          // the day each hold heard it
        std::array<std::int32_t, kHolds> entry{};     // the hold's entry with SkyrimNet, written by the bridge (0 = none)
        std::uint8_t told = 0;            // how often someone has brought it up to the player
        float toldAt = -1.0f;
        std::int32_t spreadDay = -1;      // the last day its spreading was looked at (once a day)
    };

    struct Event {
        enum Type : std::uint8_t { Reached, Forgotten } type = Reached;
        Rumor rumor;                      // as it stood (a forgotten one is gone from the ledger)
        std::uint8_t hold = NoHold;       // Reached: the hold that has now heard it
    };

    constexpr std::size_t kMaxRumors = 40;
    constexpr std::size_t kMaxRumorText = 320;
    constexpr float kTravelDays = 1.0f;   // a hold passes a rumor on once it has had it this long

    inline float LifeDays(const Rumor& r) {
        const float base = r.kind == Kind::Deed ? 9.0f : r.kind == Kind::Trouble ? 5.0f : r.kind == Kind::Life ? 7.0f : 11.0f;
        return base * (0.6f + std::clamp(r.weight, 0.0f, 1.0f));
    }
    // How many holds beyond its own a rumor reaches: small things stay at home.
    inline int Carries(const Rumor& r) { return r.weight >= 0.9f ? 8 : r.weight >= 0.75f ? 3 : r.weight >= 0.55f ? 1 : 0; }

    class Ledger {
    public:
        std::vector<Rumor> all;
        std::uint32_t nextId = 1;

        Rumor* Find(std::uint32_t id) {
            for (auto& r : all) if (r.id == id) return &r;
            return nullptr;
        }

        // A new rumor begins in its home hold. When the ledger is full the one least worth keeping goes (returned in
        // `dropped` for the bridge to take out of the game).
        std::uint32_t Add(Rumor r, float now, std::vector<Rumor>* dropped = nullptr) {
            if (r.home >= kHolds || r.text.empty()) return 0;
            if (r.text.size() > kMaxRumorText) r.text.resize(A::detail::Fit(r.text, kMaxRumorText));
            r.id = nextId++;
            r.born = now;
            r.weight = std::clamp(r.weight, 0.0f, 1.0f);
            r.heardIn = Bit(r.home);
            r.heardAt.fill(0.0f);
            r.heardAt[r.home] = now;
            r.told = 0;
            r.toldAt = -1.0f;
            r.spreadDay = -1;
            if (all.size() >= kMaxRumors) {
                auto worst = std::min_element(all.begin(), all.end(), [&](const Rumor& x, const Rumor& y) { return Worth(x, now) < Worth(y, now); });
                if (dropped) dropped->push_back(*worst);
                all.erase(worst);
            }
            all.push_back(std::move(r));
            return all.back().id;
        }

        // Everything of one owner goes (the story was given up, ended, or belongs to another timeline).
        std::vector<Rumor> Withdraw(Owner owner, std::uint32_t ownerId, Kind only = Kind{}) {
            std::vector<Rumor> gone;
            for (auto it = all.begin(); it != all.end();) {
                if (it->owner == owner && it->ownerId == ownerId && (only == Kind{} || it->kind == only)) {
                    gone.push_back(std::move(*it));
                    it = all.erase(it);
                } else {
                    ++it;
                }
            }
            return gone;
        }

        // Time passes: what is too old is forgotten, and what carries reaches a hold next to one that has had it for a
        // day. At most one new hold per rumor and day, chosen by the rumor's own number, so the same save always
        // tells it the same way.
        std::vector<Event> Tick(float now) {
            std::vector<Event> events;
            for (auto it = all.begin(); it != all.end();) {
                if (now - it->born > LifeDays(*it)) {
                    events.push_back({Event::Forgotten, *it, NoHold});
                    it = all.erase(it);
                } else {
                    ++it;
                }
            }
            const auto day = static_cast<std::int32_t>(now);
            for (auto& r : all) {
                if (r.spreadDay == day) continue;
                r.spreadDay = day;
                int beyond = -1;
                for (std::size_t h = 0; h < kHolds; ++h) if (r.heardIn & Bit(static_cast<std::uint8_t>(h))) ++beyond;
                if (beyond >= Carries(r)) continue;
                std::uint16_t next = 0;
                for (std::size_t h = 0; h < kHolds; ++h) {
                    if ((r.heardIn & Bit(static_cast<std::uint8_t>(h))) && now - r.heardAt[h] >= kTravelDays) next |= kBorders[h];
                }
                next = static_cast<std::uint16_t>(next & ~r.heardIn);
                if (!next) continue;
                // Not every day brings a traveller: the more there is to it, the sooner.
                if (A::detail::Mix(r.id, static_cast<std::uint32_t>(day)) % 100u >= static_cast<std::uint32_t>(40.0f + r.weight * 60.0f)) continue;
                std::array<std::uint8_t, kHolds> choices{};
                std::size_t n = 0;
                for (std::size_t h = 0; h < kHolds; ++h) if (next & Bit(static_cast<std::uint8_t>(h))) choices[n++] = static_cast<std::uint8_t>(h);
                const auto hold = choices[A::detail::Mix(r.id * 31u, static_cast<std::uint32_t>(day)) % n];
                r.heardIn |= Bit(hold);
                r.heardAt[hold] = now;
                events.push_back({Event::Reached, r, hold});
            }
            return events;
        }

        // What someone in a hold could pass on to the player now, the most worth telling first: trouble before deeds,
        // what nobody has brought up yet before what was told, the newer before the older. Never by the one it is
        // about, nor by the one who wants the help (they say it themselves when the player comes).
        std::vector<const Rumor*> ForHold(std::uint8_t hold, std::uint32_t teller, float now, std::size_t most) const {
            std::vector<const Rumor*> out;
            if (hold >= kHolds) return out;
            for (const auto& r : all) {
                if (!(r.heardIn & Bit(hold))) continue;
                if (teller && (r.about == teller || r.lead == teller)) continue;
                out.push_back(&r);
            }
            std::stable_sort(out.begin(), out.end(), [&](const Rumor* x, const Rumor* y) { return Telling(*x, now) > Telling(*y, now); });
            if (out.size() > most) out.resize(most);
            return out;
        }

        // The one a publican would bring up unasked, or null: something nobody has brought up to the player yet, that
        // is still fresh and worth a word (trouble always is). Nothing about someone who stands within hearing
        // (`present`): talk of a former follower is not told across the room they are working in.
        const Rumor* ToBringUp(std::uint8_t hold, std::uint32_t teller, float now, std::span<const std::uint32_t> present = {}) const {
            for (const Rumor* r : ForHold(hold, teller, now, kMaxRumors)) {
                if (r->told > 0) continue;
                if (r->about && std::find(present.begin(), present.end(), r->about) != present.end()) continue;
                if (r->kind != Kind::Trouble && (r->weight < 0.5f || now - r->born > 3.0f)) continue;
                return r;
            }
            return nullptr;
        }

        void Told(std::uint32_t id, float now) {
            if (auto* r = Find(id)) {
                if (r->told < 255) ++r->told;
                r->toldAt = now;
            }
        }

    private:
        // How much a rumor is worth keeping when the ledger is full.
        static float Worth(const Rumor& r, float now) {
            const float left = 1.0f - std::clamp((now - r.born) / std::max(LifeDays(r), 0.1f), 0.0f, 1.0f);
            return left * (0.3f + r.weight) + (r.kind == Kind::Trouble ? 1.0f : 0.0f);
        }
        // How readily it is told to the player.
        static float Telling(const Rumor& r, float now) {
            const float fresh = 1.0f - std::clamp((now - r.born) / std::max(LifeDays(r), 0.1f), 0.0f, 1.0f);
            return (r.kind == Kind::Trouble ? 2.0f : 0.0f) + (r.told == 0 ? 1.0f : 0.0f) + fresh * 0.6f + r.weight * 0.4f;
        }
    };

    // ------------------------------------------------------------------ the record (co-save)

    constexpr std::uint32_t kRecordVersion = 1;

    inline std::vector<std::uint8_t> Serialize(const Ledger& l) {
        using namespace A::detail;
        std::vector<std::uint8_t> out;
        Put(out, l.nextId);
        Put(out, static_cast<std::uint16_t>(std::min(l.all.size(), kMaxRumors)));
        std::size_t written = 0;
        for (const auto& r : l.all) {
            if (written++ >= kMaxRumors) break;
            Put(out, r.id);
            Put(out, static_cast<std::uint8_t>(r.kind));
            PutText(out, r.text, kMaxRumorText);
            Put(out, r.home);
            Put(out, r.about);
            Put(out, r.lead);
            Put(out, static_cast<std::uint8_t>(r.owner));
            Put(out, r.ownerId);
            Put(out, r.born);
            Put(out, r.weight);
            Put(out, r.heardIn);
            for (const auto at : r.heardAt) Put(out, at);
            for (const auto e : r.entry) Put(out, e);
            Put(out, r.told);
            Put(out, r.toldAt);
            Put(out, r.spreadDay);
        }
        return out;
    }

    inline bool Deserialize(std::span<const std::uint8_t> in, Ledger& l, std::uint32_t version = kRecordVersion) {
        using namespace A::detail;
        if (version != 1) return false;
        Ledger read;
        std::size_t at = 0;
        std::uint16_t count = 0;
        if (!Get(in, at, read.nextId) || !Get(in, at, count) || count > kMaxRumors) return false;
        for (std::uint16_t i = 0; i < count; ++i) {
            Rumor r;
            std::uint8_t kind = 0, owner = 0;
            if (!Get(in, at, r.id) || !Get(in, at, kind) || !GetText(in, at, r.text, kMaxRumorText) || !Get(in, at, r.home) || !Get(in, at, r.about) ||
                !Get(in, at, r.lead) || !Get(in, at, owner) || !Get(in, at, r.ownerId) || !Get(in, at, r.born) || !Get(in, at, r.weight) ||
                !Get(in, at, r.heardIn)) {
                return false;
            }
            for (auto& when : r.heardAt) if (!Get(in, at, when)) return false;
            for (auto& e : r.entry) if (!Get(in, at, e)) return false;
            if (!Get(in, at, r.told) || !Get(in, at, r.toldAt) || !Get(in, at, r.spreadDay)) return false;
            if (kind < 1 || kind > static_cast<std::uint8_t>(Kind::News) || owner > static_cast<std::uint8_t>(Owner::Politics) || r.home >= kHolds ||
                r.text.empty() || !std::isfinite(r.born) || !std::isfinite(r.weight) || !std::isfinite(r.toldAt) || (r.heardIn >> kHolds) != 0 ||
                !(r.heardIn & Bit(r.home)) || r.id == 0 || r.id >= read.nextId) {
                return false;
            }
            for (const auto when : r.heardAt) if (!std::isfinite(when)) return false;
            r.kind = static_cast<Kind>(kind);
            r.owner = static_cast<Owner>(owner);
            read.all.push_back(std::move(r));
        }
        if (at != in.size()) return false;
        l = std::move(read);
        return true;
    }
}
