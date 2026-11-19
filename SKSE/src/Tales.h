#pragma once

// IntelEngine tales (docs/STORY_ARCS.md section 0): a story the dungeon master writes for the real people of a town,
// as data, from the step kinds the arc engine can carry out and check. Compile() checks every name, step, branch,
// item and effect against what the engine can do and against each person's fit, and builds the arc (Arcs.h) the
// engine then runs like any other. Pure logic with nlohmann::json, checked offline by SKSE/tests/TalesCheck.cpp.
//
// The author's JSON (the prompt intel_tale_author.prompt asks for exactly this):
// {
//   "title": "...", "genre": "crime", "premise": "the hidden truth",
//   "people": [ {"who": "p0", "part": "the one who asks", "truth": "first person: what I know, want, hide, say"}, ... ],
//               one of them may carry "held": "d1": taken to that place before the story begins, and freed in a step
//   "items":  [ {"id": "i1", "item": "<a name of the item list>", "name": "Ysolda's Silver Ring", "hidden": "home"|"pocket"|"at"|"ambush", "of": "p3"} ],
//   "offer":  {"words": "what drives the asker to come", "ask": "what the player is asked", "journal": "first line", "reward": 150, "days": 3},
//   "steps":  [ {"id": "s1", "do": "talk|find|give|choose|visit|wait|brawl|go|delve|slay|free|ambush|stalk", ...}, ... ],
//   "endings": {"good": {...}, "bad": {...}, "other": {...}}   each {"journal", "rumor", "pay", "memories": {"p0": "..."}}
// }
// A branch ("then") names a step id or "end:good", "end:bad", "end:other"; an "effect" is one of "jail pX", "leave pX",
// "befriend pX pY", "estrange pX pY", "pay", "player pays pX N", "give iN" (the step's person hands the item over).

#include "Arcs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace IntelEngine::Tales {

    namespace A = Arcs;

    // One of the town's people the author may cast (the bridge decides who may be what).
    struct Person {
        std::string key;          // "p0" is the one who asks
        std::string name;
        std::uint32_t form = 0;
        bool hasHome = false;     // a home of their own (something can be hidden there, the player can go there)
        bool canJail = false;     // may be taken by the guard or leave town for a while
        bool canBrawl = false;    // would settle something with fists
        bool canHold = false;     // may be held at a place when the story begins (the town can do without them for a
                                  // while, and they are nowhere the player could see them go)
        bool close = false;       // close to the one who asks, as the game has it (a relationship, or the same roof)
    };

    // What a prop may be: an item of the game, by its name.
    struct Item {
        std::string name;
        std::uint32_t base = 0;
        bool pocket = true;       // may be carried in someone's pack (nothing they might put on or wield)
    };

    // What lives in a place, and so who waits at its heart: the boss is always the place's own kind (a centurion in a
    // Dwemer ruin, an overlord in a Nordic tomb), never the author's choice.
    enum SiteKind : std::uint8_t {
        SiteNone = 0,
        SiteDwemer = 1,       // a Dwemer ruin: a Dwarven centurion
        SiteFalmer = 2,       // a Falmer hive: a Falmer chief
        SiteCrypt = 3,        // a Nordic tomb: a draugr overlord
        SiteBandits = 4,      // a bandit camp, a fort, a mine held by bandits: a bandit chief
        SiteDen = 5,          // a beast's den, a cave: a giant frostbite spider or a troll
        SiteVampires = 6,     // a vampire lair: a vampire
        SiteNecromancers = 7, // a necromancer's lair: a necromancer
        SiteForsworn = 8,     // a Forsworn camp: a Forsworn shaman
        SiteHagraven = 9,     // a hagraven nest: a hagraven
        SiteGiants = 10,      // a giant's camp: a giant
    };
    constexpr std::uint8_t kSiteKinds = 10;

    // Who keeps prisoners. The game's own faction for a dungeon's prisoners is a friend of each of these, so someone
    // held is left alone by the people of the place. A ruin's machines, a tomb's dead and a beast hold nobody.
    inline bool SiteHolds(std::uint8_t kind) {
        return kind == SiteBandits || kind == SiteNecromancers || kind == SiteVampires || kind == SiteForsworn ||
               kind == SiteHagraven || kind == SiteFalmer;
    }

    // A place out in the world the story may lead to.
    struct Site {
        std::string key;              // "d1"
        std::string name;
        std::uint32_t location = 0;
        std::uint32_t anchor = 0;     // its boss room
        std::uint8_t kind = SiteNone;
        bool outside = true;          // it has a marker under the open sky, where something can be left to be found
    };

    // What was left of a victim, as the author names it ("a hunter", "a cow"): the game's own people and beasts.
    inline std::uint8_t RemainsOf(const std::string& what) {
        static constexpr std::pair<const char*, std::uint8_t> kNames[] = {
            {"hunter", A::RemainsHunter}, {"cow", A::RemainsCow}, {"horse", A::RemainsHorse}, {"goat", A::RemainsGoat}, {"deer", A::RemainsDeer}};
        for (const auto& [word, kind] : kNames) {
            if (what.find(word) != std::string::npos) return kind;
        }
        return A::RemainsNone;
    }

    struct Pool {
        std::vector<Person> people;   // people[0] is the asker
        std::vector<Site> sites;      // places near the town (never one a quest of the game still needs)
        std::vector<Item> items;
        std::uint32_t gold = 150;     // the reward the engine would offer (the author may move it within limits)
        // Step kinds (bit per Arcs::Kind) not on offer for this tale: a story told lately had one, and the next ones
        // are told another way (the user, 2026-11-19: never "here we go again... again someone ambushing").
        std::uint32_t barred = 0;
        // A story between the asker and the player themselves (Hearts.h): nothing is paid for it, whatever the author
        // wrote. (Played 2026-11-19: Ysolda promised "155 gold the moment we're done" for being helped over a fear
        // of her own heart.)
        bool heart = false;
        // A story asked of the model is held to what makes one worth living (a test's own tale is not): ways that end
        // differently, and a place out in the world as one act among people, never the whole of it (the user,
        // 2026-11-19: "going deep into the ruins and killing a boss can be part of a larger story, not a flat fetch
        // quest").
        bool depth = false;
        bool mustPlace = false;     // this one leads out into the world (the last ones stayed in town)
        bool mustCaptive = false;   // this one is about someone held at a place (a rescue was asked for)
        bool mustExamine = false;   // this one is a hunt for what is killing people: remains to look at, then what did it
    };

    struct Result {
        bool ok = false;
        std::string errors;           // what was wrong, one per line (sent back to the author once)
        std::string notes;            // what was mended without asking (unreachable steps dropped)
        A::Arc arc;
    };

    constexpr std::size_t kMaxGoal = 140;
    constexpr std::size_t kMaxLine = 320;
    constexpr std::size_t kMaxTruth = 900;
    constexpr std::size_t kMaxMemory = 400;

    namespace detail {
        inline std::string Lower(std::string s) {
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
        inline std::string Trim(std::string s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
            return s;
        }
        inline std::string Str(const nlohmann::json& j, const char* key) {
            if (!j.is_object() || !j.contains(key)) return {};
            const auto& v = j[key];
            if (v.is_string()) return Trim(v.get<std::string>());
            if (v.is_number_integer()) return std::to_string(v.get<long long>());
            return {};
        }
        inline int Int(const nlohmann::json& j, const char* key, int fallback) {
            if (!j.is_object() || !j.contains(key)) return fallback;
            const auto& v = j[key];
            if (v.is_number()) return static_cast<int>(v.get<double>());
            if (v.is_string()) {
                try { return std::stoi(v.get<std::string>()); } catch (...) { return fallback; }
            }
            return fallback;
        }
        inline bool Bool(const nlohmann::json& j, const char* key) {
            if (!j.is_object() || !j.contains(key)) return false;
            const auto& v = j[key];
            if (v.is_boolean()) return v.get<bool>();
            if (v.is_string()) {
                const auto s = Lower(v.get<std::string>());
                return s == "true" || s == "yes";
            }
            return false;
        }
        // The player's character is "{player}" in every text the engine fills in; the author was asked to write it so,
        // and "the player" (which no character would say) becomes it too.
        inline std::string Words(std::string s, std::size_t limit) {
            s = Trim(std::move(s));
            for (const char* word : {"the player", "The player"}) {
                for (auto at = s.find(word); at != std::string::npos; at = s.find(word, at + 8)) s.replace(at, std::strlen(word), "{player}");
            }
            if (s.size() > limit) {
                // Too long for where it is shown. Keep what reads whole: up to the last full sentence, or else up
                // to the last clause (a plea in the offer box read "...because I cannot go near him... Reward: 155
                // gold", 2026-11-19); by punctuation only, so it holds in any language.
                const auto before = [&](std::size_t i) { return i + 1 < s.size() && s[i + 1] == ' '; };
                for (std::size_t i = std::min(limit, s.size()); i-- > limit / 2;) {
                    if ((s[i] == '.' || s[i] == '!' || s[i] == '?') && before(i)) {
                        s.resize(i + 1);
                        return s;
                    }
                }
                for (std::size_t i = std::min(limit, s.size()); i-- > limit * 3 / 5;) {
                    if ((s[i] == ',' || s[i] == ';' || s[i] == ':') && before(i)) {
                        s.resize(i);
                        s += ".";
                        return s;
                    }
                }
                // No such place: cut at the end of a word, and say that it was cut (an answer in a popup read
                // "...swindled by Quickhand, and th", 2026-11-19).
                std::size_t n = limit > 3 ? limit - 3 : limit;
                while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
                if (const auto space = s.rfind(' ', n); space != std::string::npos && space + 30 >= n && space > 0) n = space;
                s.resize(n);
                while (!s.empty() && (s.back() == ',' || s.back() == ';' || s.back() == ':' || s.back() == ' ')) s.pop_back();
                s += "...";
            }
            return s;
        }
        inline std::vector<std::string> Split(const std::string& s) {
            std::vector<std::string> out;
            std::string word;
            for (const char c : s) {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    if (!word.empty()) out.push_back(word), word.clear();
                } else {
                    word += c;
                }
            }
            if (!word.empty()) out.push_back(word);
            return out;
        }
    }

    // Builds the arc of a tale, or says why not.
    inline Result Compile(const nlohmann::json& tale, const Pool& pool) {
        using namespace detail;
        Result r;
        std::string& err = r.errors;
        const auto fail = [&](const std::string& why) { err += why + "\n"; };
        if (!tale.is_object()) {
            fail("the answer is not one JSON object");
            return r;
        }
        if (pool.people.empty()) {
            fail("nobody to ask");
            return r;
        }
        A::Arc& a = r.arc;
        a.pattern = A::Pattern::Tale;
        a.title = Words(Str(tale, "title"), 60);
        a.genre = Lower(Words(Str(tale, "genre"), 60));
        // One word or two for the ledger ("mystery of the heart, with a family secret" -> "mystery of the heart").
        if (const auto cut = a.genre.find_first_of(",;(/"); cut != std::string::npos) a.genre = Trim(a.genre.substr(0, cut));
        if (a.genre.size() > 32) a.genre.resize(32);
        a.premise = Words(Str(tale, "premise"), 600);
        if (a.title.size() < 3) fail("title: missing");
        if (a.premise.empty()) fail("premise: missing");

        // ---- the people: p0 is the asker (role 1), the others take roles 2..7 in the order written
        std::map<std::string, const Person*> byKey;
        for (const auto& p : pool.people) byKey[Lower(p.key)] = &p;
        // A person may be named by key ("p3") or, as models sometimes do, by name ("Brenuin").
        const auto canon = [&](const std::string& raw) {
            const auto key = Lower(Trim(raw));
            if (byKey.contains(key)) return key;
            for (const auto& p : pool.people) {
                if (Lower(p.name) == key) return Lower(p.key);
            }
            return key;
        };
        std::map<std::string, A::Role> roleOf;
        std::vector<std::pair<A::Role, std::string>> pendingHeld;   // who is held, and where (resolved with the places)
        const auto& people = tale.contains("people") ? tale["people"] : nlohmann::json();
        if (!people.is_array() || people.size() < 2) fail("people: at least the asker and one other person");
        std::size_t nextRole = 2;
        if (people.is_array()) {
            // The asker first, whatever order they were written in.
            std::vector<const nlohmann::json*> order;
            for (const auto& p : people) if (canon(Str(p, "who")) == Lower(pool.people[0].key)) order.push_back(&p);
            for (const auto& p : people) if (canon(Str(p, "who")) != Lower(pool.people[0].key)) order.push_back(&p);
            if (order.empty() || canon(Str(*order[0], "who")) != Lower(pool.people[0].key)) fail("people: " + pool.people[0].key + " (who asks) must be one of them");
            for (const auto* p : order) {
                const auto key = canon(Str(*p, "who"));
                const auto found = byKey.find(key);
                if (found == byKey.end()) {
                    fail("people: \"" + key + "\" is nobody of the list");
                    continue;
                }
                if (roleOf.contains(key)) {
                    fail("people: " + key + " is listed twice");
                    continue;
                }
                A::Role role = A::RAsker;
                if (key != Lower(pool.people[0].key)) {
                    if (nextRole >= A::kRoles) {
                        fail("people: at most " + std::to_string(A::kRoles - 1) + " people");
                        continue;
                    }
                    role = static_cast<A::Role>(nextRole++);
                }
                roleOf[key] = role;
                a.cast[role] = found->second->form;
                a.roleName[role] = Words(Str(*p, "part"), 60);
                a.truth[role] = Words(Str(*p, "truth"), kMaxTruth);
                if (a.truth[role].empty()) fail("people: " + key + " has no truth");
                if (const auto held = Str(*p, "held"); !held.empty() && Lower(held) != "no" && Lower(held) != "none") pendingHeld.emplace_back(role, held);
            }
        }
        const auto person = [&](const std::string& raw, const char* where) -> A::Role {
            const auto key = canon(raw);
            const auto it = roleOf.find(key);
            if (it == roleOf.end()) {
                fail(std::string(where) + ": \"" + raw + "\" is not one of the tale's people");
                return A::RNone;
            }
            return it->second;
        };
        const auto personOf = [&](A::Role role) -> const Person* {
            for (const auto& [key, rr] : roleOf) if (rr == role) return byKey[key];
            return nullptr;
        };

        // ---- the props
        std::map<std::string, std::uint8_t> propOf;
        std::vector<std::pair<std::uint8_t, std::string>> pendingSites;   // props carried by a place's boss
        if (tale.contains("items") && tale["items"].is_array()) {
            for (const auto& it : tale["items"]) {
                if (propOf.size() >= A::kProps) {
                    fail("items: at most " + std::to_string(A::kProps));
                    break;
                }
                const auto id = Lower(Str(it, "id"));
                const auto itemName = Lower(Str(it, "item"));
                const Item* base = nullptr;
                for (const auto& i : pool.items) if (Lower(i.name) == itemName) base = &i;
                if (id.empty()) fail("items: one has no id");
                if (!base) {
                    fail("items: \"" + Str(it, "item") + "\" is not in the item list");
                    continue;
                }
                const auto p = static_cast<std::uint8_t>(propOf.size());
                a.propBase[p] = base->base;
                a.propName[p] = Words(Str(it, "name"), 60);
                if (a.propName[p].empty()) a.propName[p] = base->name;
                const auto hidden = Lower(Str(it, "hidden"));
                if (hidden == "ambush" || hidden == "thugs" || hidden == "leader") {
                    // Carried by the leader of the story's ambush (found on them once they are down).
                    a.propAmbush = static_cast<std::uint8_t>(a.propAmbush | (1u << p));
                    propOf[id] = p;
                    continue;
                }
                if (hidden == "at" || hidden == "place" || hidden == "dungeon") {
                    // Carried by the boss of a place out in the world (resolved once the places are known).
                    pendingSites.emplace_back(p, Str(it, "of"));
                    propOf[id] = p;
                    continue;
                }
                const A::Role holder = person(Str(it, "of"), "items");
                if (holder == A::RAsker) fail("items: nothing is hidden with " + pool.people[0].key + " (who asks)");
                if (hidden == "home") {
                    const auto* who = personOf(holder);
                    if (who && !who->hasHome) fail("items: " + who->key + " has no home of their own to hide it in");
                    a.propHome[p] = holder;
                } else if (hidden == "pocket") {
                    if (!base->pocket) fail("items: a " + base->name + " is never carried hidden in a pocket; hide it at home");
                    a.propPocket[p] = holder;
                } else {
                    fail("items: hidden must be \"home\", \"pocket\", \"at\" (a place) or \"ambush\" (the ambush's leader)");
                }
                propOf[id] = p;
            }
        }

        // ---- the places out in the world: given a slot when first named (a step, an item)
        std::map<std::string, std::uint8_t> siteSlot;   // site key -> index + 1 in the arc
        const auto siteOf = [&](const std::string& raw, const std::string& where) -> std::uint8_t {
            auto key = Lower(Trim(raw));
            if (key.starts_with("in ")) key = Trim(key.substr(3));
            const Site* found = nullptr;
            for (const auto& s : pool.sites) {
                if (Lower(s.key) == key || Lower(s.name) == key) found = &s;
            }
            if (!found) {
                fail(where + ": \"" + raw + "\" is no place of the list");
                return 0;
            }
            const auto k = Lower(found->key);
            if (const auto it = siteSlot.find(k); it != siteSlot.end()) return it->second;
            if (siteSlot.size() >= A::kSites) {
                fail(where + ": at most " + std::to_string(A::kSites) + " places out in the world");
                return 0;
            }
            const auto slot = static_cast<std::uint8_t>(siteSlot.size() + 1);
            siteSlot[k] = slot;
            a.site[slot - 1u] = found->location;
            a.siteAnchor[slot - 1u] = found->anchor;
            a.siteKind[slot - 1u] = found->kind;
            return slot;
        };

        for (const auto& [p, where] : pendingSites) a.propSite[p] = siteOf(where, "items");

        // ---- someone held at a place when the story begins (taken there unseen before the asker comes)
        for (const auto& [role, where] : pendingHeld) {
            const auto* who = personOf(role);
            const std::string name = who ? who->key : std::string("someone");
            if (a.captive) {
                fail("people: only one person is held in a story");
                break;
            }
            if (role == A::RAsker) {
                fail("people: " + name + " (who asks) is not the one held: they come to {player}");
                continue;
            }
            if (who && !who->canHold) {
                fail("people: " + name + " cannot be held anywhere (the town cannot do without them, or they stand where {player} sees them): choose someone whose line says they may be held");
                continue;
            }
            const auto slot = siteOf(where, "people: held");
            if (!slot) continue;
            if (!SiteHolds(a.siteKind[slot - 1u])) {
                fail("people: nobody is held at " + where + ": what lives there keeps no prisoners; choose a place whose line says someone may be held there");
                continue;
            }
            a.captive = static_cast<std::uint8_t>(role);
            a.captiveSite = slot;
        }

        // ---- the offer
        const auto& offer = tale.contains("offer") ? tale["offer"] : nlohmann::json::object();
        a.offerWords = Words(Str(offer, "words"), 600);
        a.offerAsk = Words(Str(offer, "ask"), 200);
        a.opening = Words(Str(offer, "journal"), kMaxLine);
        a.gold = pool.heart ? 0u : static_cast<std::uint32_t>(std::clamp(Int(offer, "reward", static_cast<int>(pool.gold)), 25, 1000));
        a.days = static_cast<float>(std::clamp(Int(offer, "days", 3), 1, 7));
        if (a.offerWords.empty() || a.offerAsk.empty()) fail("offer: words and ask are needed");
        if (a.opening.empty()) a.opening = a.offerAsk;
        a.story = a.opening;

        // ---- the endings
        const auto& endings = tale.contains("endings") ? tale["endings"] : nlohmann::json::object();
        static constexpr const char* kEndNames[3] = {"good", "bad", "other"};
        std::array<bool, 3> endWritten{};
        for (std::size_t k = 0; k < 3; ++k) {
            if (!endings.is_object() || !endings.contains(kEndNames[k])) continue;
            const auto& e = endings[kEndNames[k]];
            auto& end = a.ends[k];
            end.journal = Words(Str(e, "journal"), kMaxLine);
            end.rumor = Words(Str(e, "rumor"), kMaxLine);
            end.pay = !pool.heart && (k == 0 ? !e.contains("pay") || Bool(e, "pay") : Bool(e, "pay"));
            if (e.contains("memories") && e["memories"].is_object()) {
                for (const auto& [key, text] : e["memories"].items()) {
                    const auto it = roleOf.find(Lower(key));
                    if (it == roleOf.end() || !text.is_string()) continue;
                    end.memory[it->second] = Words(text.get<std::string>(), kMaxMemory);
                }
            }
            endWritten[k] = !end.journal.empty();
        }
        if (!endWritten[0]) fail("endings: \"good\" needs a journal line");
        if (!endWritten[1]) fail("endings: \"bad\" needs a journal line");

        // ---- the steps
        const auto& steps = tale.contains("steps") ? tale["steps"] : nlohmann::json();
        if (!steps.is_array() || steps.empty()) {
            fail("steps: at least one");
            return r;
        }
        std::map<std::string, std::size_t> stepIndex;
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const auto id = Lower(Str(steps[i], "id"));
            if (id.empty() || stepIndex.contains(id)) fail("steps: every step needs its own id (step " + std::to_string(i + 1) + ")");
            else stepIndex[id] = i;
        }
        // Which steps each step leads to (indices; -1..-3 the endings), to drop the unreachable and find dead ends.
        std::vector<std::vector<int>> leads(steps.size());
        const auto target = [&](const std::string& raw, std::size_t from, const char* where) -> int {
            const auto t = Lower(Trim(raw));
            if (t == "end:good" || t == "good") return -1;
            if (t == "end:bad" || t == "bad") return -2;
            if (t == "end:other" || t == "other") {
                if (!endWritten[2]) fail(std::string(where) + ": end:other is used but endings.other has no journal line");
                return -3;
            }
            if (t == "stay") return static_cast<int>(from);
            const auto it = stepIndex.find(t);
            if (it == stepIndex.end()) {
                fail(std::string(where) + ": \"" + raw + "\" is no step and no ending");
                return -2;
            }
            return static_cast<int>(it->second);
        };
        bool payLeader = false;   // while an ambush's outcomes of talk are read: gold may go to the men's leader
        struct Branch {
            int to = -2;
            int prop = -1;   // GiveItem: which prop
            std::string line;
            A::Effect effect = A::Effect::None;
            A::Role effectRole = A::RNone;
            A::Role bondWith = A::RNone;
            std::uint32_t price = 0;
        };
        const auto effectOf = [&](const nlohmann::json& j, Branch& b, const std::string& where) {
            const auto raw = Lower(Str(j, "effect"));
            const auto words = Split(raw);
            if (words.empty() || words[0] == "none") return;
            // What follows the verb: one person ("p3", or a name of several words), or two joined by "and"/"with".
            const auto rest = [&](std::size_t from) {
                std::string s;
                for (std::size_t i = from; i < words.size(); ++i) s += (s.empty() ? "" : " ") + words[i];
                return s;
            };
            const auto two = [&](const std::string& s, std::string& x, std::string& y) {
                for (const char* sep : {" and ", " with ", " & "}) {
                    if (const auto at = s.find(sep); at != std::string::npos) {
                        x = s.substr(0, at);
                        y = s.substr(at + std::strlen(sep));
                        return true;
                    }
                }
                const auto parts = Split(s);
                if (parts.size() != 2) return false;
                x = parts[0];
                y = parts[1];
                return true;
            };
            const auto missing = [&]() { fail(where + ": effect \"" + Str(j, "effect") + "\" is missing a person"); };
            if (words[0] == "pay") {
                b.effect = A::Effect::PayGold;
                b.effectRole = A::RAsker;
            } else if (words[0] == "jail" || words[0] == "leave") {
                if (words.size() < 2) return missing();
                b.effectRole = person(rest(1), where.c_str());
                const auto* who = personOf(b.effectRole);
                if (b.effectRole == A::RAsker || (who && !who->canJail)) {
                    fail(where + ": " + rest(1) + " cannot be " + (words[0] == "jail" ? "taken by the guard" : "sent away"));
                    return;
                }
                b.effect = words[0] == "jail" ? A::Effect::Jail : A::Effect::Leave;
            } else if (words[0] == "befriend" || words[0] == "estrange") {
                std::string x, y;
                if (!two(rest(1), x, y)) return missing();
                b.effectRole = person(x, where.c_str());
                b.bondWith = person(y, where.c_str());
                if (b.effectRole == b.bondWith) fail(where + ": " + words[0] + " needs two different people");
                b.effect = words[0] == "befriend" ? A::Effect::Befriend : A::Effect::Estrange;
            } else if (words[0] == "closer" || words[0] == "apart") {
                // "closer p2": that person and {player} grow closer by a step; "apart p2": they cool toward {player}.
                if (words.size() < 2) return missing();
                b.effectRole = person(rest(1), where.c_str());
                b.effect = words[0] == "closer" ? A::Effect::Closer : A::Effect::Apart;
            } else if (words[0] == "give") {
                // "give i1": the step's person hands the item over (a confession, a bargain): it reaches the player.
                if (words.size() < 2) return missing();
                const auto it = propOf.find(words[1]);
                if (it == propOf.end()) {
                    fail(where + ": effect \"" + Str(j, "effect") + "\" names no item of the tale");
                    return;
                }
                b.effect = A::Effect::GiveItem;
                b.prop = it->second;
            } else if (words[0] == "player" && words.size() == 3 && words[1] == "pays" && payLeader) {
                // "player pays 150" in an ambush's outcome of talk: to the leader of the men.
                const int amount = std::atoi(words.back().c_str());
                if (amount < 1 || amount > 2000) fail(where + ": the player pays between 1 and 2000 gold");
                b.price = static_cast<std::uint32_t>(std::clamp(amount, 1, 2000));
                b.effect = A::Effect::PlayerPays;
            } else if (words[0] == "player" && words.size() >= 4 && words[1] == "pays") {
                // "player pays p3 50": the last word is the gold, what lies between is the person.
                b.effectRole = person(rest(2).substr(0, rest(2).size() - words.back().size() - 1), where.c_str());
                const int amount = std::atoi(words.back().c_str());
                if (amount < 1 || amount > 2000) fail(where + ": the player pays between 1 and 2000 gold");
                b.price = static_cast<std::uint32_t>(std::clamp(amount, 1, 2000));
                b.effect = A::Effect::PlayerPays;
            } else {
                fail(where + ": unknown effect \"" + Str(j, "effect") + "\"");
            }
        };
        const auto branch = [&](const nlohmann::json& j, std::size_t from, const std::string& where) {
            Branch b;
            const auto then = Str(j, "then");
            if (then.empty()) fail(where + ": \"then\" is missing");
            b.to = then.empty() ? -2 : target(then, from, where.c_str());
            b.line = Words(Str(j, "journal"), kMaxLine);
            effectOf(j, b, where);
            if (b.to >= 0) leads[from].push_back(b.to);
            return b;
        };

        struct Step {
            A::Node node;
            std::vector<std::pair<A::If, Branch>> edges;   // in the order they are looked at
            int persuadeOption = -1;
            bool naming = false;   // a choice that names what killed someone (the step "name"): its place is node.site
        };
        std::vector<Step> built(steps.size());
        int ambushes = 0;   // one ambush a story (its men are the arc's own)
        std::size_t clues = 0;   // things to examine, each with its own slot of the arc
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const auto& s = steps[i];
            auto& st = built[i];
            auto& n = st.node;
            const auto where = "step " + Str(s, "id");
            const auto kind = Lower(Str(s, "do"));
            n.goal = Words(Str(s, "goal"), kMaxGoal);
            if (n.goal.empty()) fail(where + ": goal is missing");
            const auto with = [&](const char* key) {
                const auto raw = Str(s, key);
                if (raw.empty()) {
                    fail(where + ": \"" + key + "\" is missing");
                    return A::RNone;
                }
                return person(raw, where.c_str());
            };
            const auto item = [&]() -> int {
                const auto it = propOf.find(Lower(Str(s, "item")));
                if (it == propOf.end()) {
                    fail(where + ": \"item\" names no item of the tale");
                    return -1;
                }
                return it->second;
            };
            if (kind == "talk") {
                n.kind = A::Kind::Talk;
                n.who = with("with");
                const auto& outcomes = s.contains("outcomes") ? s["outcomes"] : nlohmann::json();
                if (!outcomes.is_array() || outcomes.empty() || outcomes.size() > 3) {
                    fail(where + ": a talk has 1 to 3 outcomes");
                } else {
                    for (std::size_t k = 0; k < outcomes.size(); ++k) {
                        n.turns[k] = Words(Str(outcomes[k], "if"), 200);
                        if (n.turns[k].empty()) fail(where + ": outcome " + std::to_string(k + 1) + " has no \"if\"");
                        st.edges.push_back({static_cast<A::If>(static_cast<int>(A::If::Said0) + static_cast<int>(k)),
                                            branch(outcomes[k], i, where + " outcome " + std::to_string(k + 1))});
                    }
                }
            } else if (kind == "visit") {
                n.kind = A::Kind::Visit;
                n.who = with("with");
                n.why = Words(Str(s, "why"), 200);
                st.edges.push_back({A::If::Done, branch(s, i, where)});
            } else if (kind == "find") {
                n.kind = A::Kind::Have;
                const int p = item();
                n.prop = static_cast<std::uint8_t>(std::max(0, p));
                if (p >= 0) n.marker = static_cast<A::Role>(a.propHome[p] ? a.propHome[p] : a.propPocket[p]);
                st.edges.push_back({A::If::Done, branch(s, i, where)});
            } else if (kind == "give") {
                n.kind = A::Kind::Give;
                n.who = with("to");
                const int p = item();
                n.prop = static_cast<std::uint8_t>(std::max(0, p));
                st.edges.push_back({A::If::Done, branch(s, i, where)});
            } else if (kind == "choose") {
                n.kind = A::Kind::Choose;
                n.who = with("with");
                n.question = Words(Str(s, "question"), 160);
                if (n.question.empty()) n.question = "What will you do?";
                const auto& options = s.contains("options") ? s["options"] : nlohmann::json();
                if (!options.is_array() || options.size() < 2 || options.size() > 3) {
                    fail(where + ": a choice has 2 or 3 options");
                } else {
                    for (std::size_t k = 0; k < options.size(); ++k) {
                        const auto& o = options[k];
                        const auto oWhere = where + " option " + std::to_string(k + 1);
                        n.options[k] = Words(Str(o, "say"), 140);
                        if (n.options[k].empty()) fail(oWhere + ": \"say\" is missing");
                        const auto when = static_cast<A::If>(static_cast<int>(A::If::Option0) + static_cast<int>(k));
                        if (Bool(o, "persuade")) {
                            if (st.persuadeOption >= 0) fail(where + ": only one option may need Speech");
                            if (Lower(Str(o, "effect")).starts_with("give")) fail(oWhere + ": an option that needs Speech cannot hand an item over");
                            st.persuadeOption = static_cast<int>(k);
                            if (n.options[k].find("(Speech)") == std::string::npos) n.options[k] += " (Speech)";
                            st.edges.push_back({A::If::Persuaded, branch(o, i, oWhere)});
                            // Not persuasive enough: where "else" says, or the same step again.
                            nlohmann::json otherwise = nlohmann::json::object();
                            otherwise["then"] = Str(o, "else").empty() ? std::string("stay") : Str(o, "else");
                            otherwise["journal"] = Str(o, "else_journal");
                            st.edges.push_back({when, branch(otherwise, i, oWhere + " else")});
                        } else {
                            st.edges.push_back({when, branch(o, i, oWhere)});
                        }
                    }
                }
            } else if (kind == "brawl") {
                n.kind = A::Kind::Brawl;
                n.who = with("with");
                if (const auto* who = personOf(n.who); who && !who->canBrawl) fail(where + ": " + who->key + " is no one to brawl with");
                if (n.who == A::RAsker) fail(where + ": the one who asks is not the one to brawl with");
                nlohmann::json won = s.contains("won") ? s["won"] : nlohmann::json::object();
                nlohmann::json lost = s.contains("lost") ? s["lost"] : nlohmann::json::object();
                st.edges.push_back({A::If::Won, branch(won, i, where + " won")});
                st.edges.push_back({A::If::Lost, branch(lost, i, where + " lost")});
            } else if (kind == "go") {
                n.kind = A::Kind::Reach;
                // "home of p3", or just "p3"
                auto words = Split(Str(s, "to"));
                std::string key = words.empty() ? std::string() : words.back();
                n.who = key.empty() ? A::RNone : person(key, where.c_str());
                if (const auto* who = personOf(n.who); who && !who->hasHome) fail(where + ": " + who->key + " has no home to go to");
                st.edges.push_back({A::If::Done, branch(s, i, where)});
            } else if (kind == "delve" || kind == "slay") {
                // Into a place out in the world: to its depths, or to kill what waits there (the place's own kind of
                // boss; the author only names it).
                n.kind = kind == "delve" ? A::Kind::Delve : A::Kind::Slay;
                n.site = siteOf(Str(s, "place"), where);
                if (kind == "slay" && n.site) {
                    const auto name = Words(Str(s, "boss"), 50);
                    if (!name.empty()) a.bossName[n.site - 1u] = name;
                }
                st.edges.push_back({A::If::Done, branch(s, i, where)});
            } else if (kind == "name") {
                // The player tells someone what did it, having looked at what was left: the true answer and two that
                // people of the story believe or would rather believe, in an order nobody can learn by heart. The
                // truth is the engine's (what lives in that place); the one who hears it gives what they have against
                // it. Being wrong costs that help and nothing else: the story goes on either way. To the engine it is
                // a choice like any other.
                n.kind = A::Kind::Choose;
                n.who = with("with");
                n.site = siteOf(Str(s, "place"), where);
                st.naming = true;
                n.question = Words(Str(s, "question"), 160);
                if (n.question.empty()) n.question = "What did this?";
                const auto truth = Words(Str(s, "truth"), 140);
                const auto& others = s.contains("others") ? s["others"] : nlohmann::json();
                std::array<std::string, 2> wrong;
                if (others.is_array() && others.size() == 2) {
                    for (std::size_t k = 0; k < 2; ++k) wrong[k] = others[k].is_string() ? Words(others[k].get<std::string>(), 140) : std::string();
                }
                if (truth.empty()) fail(where + ": \"truth\": the answer that is true (what lives in that place)");
                if (wrong[0].empty() || wrong[1].empty()) fail(where + ": \"others\": two answers that are false, which people of the story believe");
                if (!s.contains("right") || !s["right"].is_object() || !s.contains("wrong") || !s["wrong"].is_object()) {
                    fail(where + ": \"right\" and \"wrong\", each with \"then\" and \"journal\"");
                } else {
                    // Where the truth stands among the three follows from the words themselves.
                    std::size_t sum = 0;
                    for (const unsigned char c : truth + wrong[0] + Str(s, "id")) sum = sum * 31u + c;
                    const std::size_t at = sum % 3u;
                    std::size_t w = 0;
                    for (std::size_t k = 0; k < 3; ++k) {
                        const bool isTruth = k == at;
                        n.options[k] = isTruth ? truth : wrong[w++];
                        const auto when = static_cast<A::If>(static_cast<int>(A::If::Option0) + static_cast<int>(k));
                        auto b = branch(isTruth ? s["right"] : s["wrong"], i, where + (isTruth ? " right" : " wrong"));
                        if (isTruth && b.effect == A::Effect::None && n.who != A::RNone && n.site) {
                            b.effect = A::Effect::Remedy;
                            b.effectRole = n.who;
                            b.prop = static_cast<int>(n.site) - 1;
                        }
                        st.edges.push_back({when, b});
                    }
                }
            } else if (kind == "examine") {
                // What was left of a victim lies outside a place: looked at closely it tells what did it, and what did
                // it is what lives in that place (the engine knows its kind; the writer was given its signs). So the
                // mystery is fair by construction, and the same place has a "slay" further on (checked below).
                n.kind = A::Kind::Examine;
                n.site = siteOf(Str(s, "place"), where);
                const auto what = RemainsOf(Lower(Str(s, "what")));
                if (!what) fail(where + ": \"what\" lies there: a hunter, a cow, a horse, a goat or a deer");
                n.question = Words(Str(s, "clue"), A::kMaxText);
                n.why = Words(Str(s, "name"), 40);   // the dead one's name in the story, when it gives one (the body carries it)
                if (n.question.empty()) fail(where + ": \"clue\": what is seen on the remains, in a sentence or two, from that place's signs");
                if (n.site) {
                    for (const auto& ps : pool.sites) {
                        if (ps.location == a.site[n.site - 1u] && !ps.outside) fail(where + ": nothing can be left outside " + ps.name + " (it has no way in under the open sky)");
                    }
                }
                if (clues >= A::kClues) {
                    fail(where + ": at most " + std::to_string(A::kClues) + " things to examine in a story");
                } else if (what && n.site) {
                    n.prop = static_cast<std::uint8_t>(clues);
                    a.clueWhat[clues] = what;
                    a.clueSite[clues] = n.site;
                    ++clues;
                }
                st.edges.push_back({A::If::Done, branch(s, i, where)});
            } else if (kind == "free") {
                // Someone held at a place: found there and cut loose by the player. (The engine keeps them alive and in their bonds
                // until then, and the people of the place leave them be.)
                n.kind = A::Kind::Free;
                n.who = with(Str(s, "who").empty() && !Str(s, "with").empty() ? "with" : "who");
                if (!a.captive) fail(where + ": nobody of the story is held anywhere (\"held\": \"dX\" on one of the people)");
                else if (n.who != A::RNone && n.who != static_cast<A::Role>(a.captive)) fail(where + ": the one to free is the one who is held");
                n.site = a.captiveSite;
                st.edges.push_back({A::If::Done, branch(s, i, where)});
            } else if (kind == "ambush") {
                // Hired men on the player's road, sent by someone of the story ("by"; left out when nobody knows yet).
                if (ambushes++ > 0) fail(where + ": one ambush in a story");
                n.kind = A::Kind::Ambush;
                const auto by = Str(s, "by");
                n.who = by.empty() ? A::RNone : person(by, where.c_str());
                n.why = Words(Str(s, "why"), 200);
                if (n.why.empty()) fail(where + ": an ambush needs \"why\": what the men were paid to do");
                a.thugName = Words(Str(s, "leader"), 50);
                // Beaten is no reload: the men leave with what they came for, and the story goes on from there.
                if (!s.contains("lost") || !s["lost"].is_object()) {
                    fail(where + ": an ambush needs \"lost\": {\"then\", \"journal\"}, where the story goes when the men beat {player} down and leave");
                } else {
                    st.edges.push_back({A::If::Lost, branch(s["lost"], i, where + " lost")});
                }
                st.edges.push_back({A::If::Done, branch(s, i, where)});
                // Talked out of it: what the men would settle for, read from the real talk with their leader.
                if (s.contains("outcomes")) {
                    const auto& outcomes = s["outcomes"];
                    if (!outcomes.is_array() || outcomes.size() > 2) {
                        fail(where + ": an ambush has at most two outcomes of talk");
                    } else {
                        payLeader = true;
                        for (std::size_t k = 0; k < outcomes.size(); ++k) {
                            n.turns[k] = Words(Str(outcomes[k], "if"), 200);
                            if (n.turns[k].empty()) fail(where + ": outcome " + std::to_string(k + 1) + " has no \"if\"");
                            st.edges.push_back({static_cast<A::If>(static_cast<int>(A::If::Said0) + static_cast<int>(k)),
                                                branch(outcomes[k], i, where + " outcome " + std::to_string(k + 1))});
                        }
                        payLeader = false;
                    }
                }
            } else if (kind == "stalk") {
                // Someone of the story shadows the player out of doors, and talks once caught or come forward.
                n.kind = A::Kind::Stalk;
                n.who = with("by");
                if (n.who == A::RAsker) fail(where + ": the one who asks does not shadow {player}; they came openly");
                n.why = Words(Str(s, "why"), 200);
                if (n.why.empty()) fail(where + ": a stalk needs \"why\": what makes them follow and what they dare not say");
                st.edges.push_back({A::If::Done, branch(s, i, where)});
            } else if (kind == "wait") {
                n.kind = A::Kind::Wait;
                n.waitHours = static_cast<float>(std::clamp(Int(s, "hours", 12), 1, 72));
                st.edges.push_back({A::If::Done, branch(s, i, where)});
            } else {
                fail(where + ": \"do\" must be talk, find, give, choose, visit, wait, brawl, go, delve, slay, free, examine, name, ambush or stalk");
            }
            if (static_cast<unsigned>(n.kind) != 0 && (pool.barred & (1u << static_cast<unsigned>(n.kind)))) {
                fail(where + ": no \"" + kind + "\" in this story (a story told lately had one): tell this one another way");
            }
            // No marker on the one who sent an ambush: the men are not where they are.
            // Nor on someone shadowing the player: where they are is theirs to keep.
            if (n.kind != A::Kind::Have && n.kind != A::Kind::Wait && n.kind != A::Kind::Ambush && n.kind != A::Kind::Stalk && n.marker == A::RNone) {
                n.marker = n.who;
            }
        }
        if (!err.empty()) return r;

        // ---- keep what can be reached from the first step; every kept step must reach an ending
        std::vector<bool> reach(steps.size(), false);
        std::vector<std::size_t> todo{0};
        reach[0] = true;
        while (!todo.empty()) {
            const auto at = todo.back();
            todo.pop_back();
            for (const int to : leads[at]) {
                if (to >= 0 && !reach[static_cast<std::size_t>(to)]) {
                    reach[static_cast<std::size_t>(to)] = true;
                    todo.push_back(static_cast<std::size_t>(to));
                }
            }
        }
        std::vector<bool> ends(steps.size(), false);
        for (bool changed = true; changed;) {
            changed = false;
            for (std::size_t i = 0; i < steps.size(); ++i) {
                if (ends[i]) continue;
                for (const auto& [when, b] : built[i].edges) {
                    if (b.to < 0 || (b.to != static_cast<int>(i) && ends[static_cast<std::size_t>(b.to)])) {
                        ends[i] = true;
                        changed = true;
                        break;
                    }
                }
            }
        }
        std::vector<int> newIndex(steps.size(), -1);
        std::size_t kept = 0;
        for (std::size_t i = 0; i < steps.size(); ++i) {
            if (!reach[i]) {
                r.notes += "step " + Str(steps[i], "id") + " is never reached: left out\n";
                continue;
            }
            if (!ends[i]) fail("step " + Str(steps[i], "id") + ": no way from it leads to an ending");
            newIndex[i] = static_cast<int>(kept++);
        }
        if (kept > A::kMaxTaleNodes) fail("steps: at most " + std::to_string(A::kMaxTaleNodes));
        if (!err.empty()) return r;

        // ---- someone held: a step of the story frees them, and nothing is done with them before it
        const auto kindOf = [&](std::size_t i) { return built[i].node.kind; };
        // Steps that only lead up to the one step that frees them (reached before it, never after it), and that step.
        // A player goes where they like: found and freed before the story came to it, those steps are left, as things
        // that never happened (FoundEarly below), and the story goes on from the step that frees.
        std::vector<bool> leadsUp(steps.size(), false);
        int freeStep = -1;
        if (a.captive) {
            const auto captive = static_cast<A::Role>(a.captive);
            const auto* held = personOf(captive);
            const std::string name = held ? held->key : std::string("someone");
            bool freed = false;
            for (std::size_t i = 0; i < steps.size(); ++i) freed = freed || (reach[i] && kindOf(i) == A::Kind::Free);
            if (!freed) fail("steps: " + name + " is held and no step frees them ({\"do\": \"free\", \"who\": \"" + name + "\"})");
            // What can be reached without passing the step that frees them.
            std::vector<bool> early(steps.size(), false);
            std::vector<std::size_t> open;
            if (kindOf(0) != A::Kind::Free) {
                early[0] = true;
                open.push_back(0);
            }
            while (!open.empty()) {
                const auto at = open.back();
                open.pop_back();
                for (const int to : leads[at]) {
                    const auto next = static_cast<std::size_t>(to);
                    if (to >= 0 && !early[next] && kindOf(next) != A::Kind::Free) {
                        early[next] = true;
                        open.push_back(next);
                    }
                }
            }
            for (std::size_t i = 0; i < steps.size(); ++i) {
                if (!early[i]) continue;
                const auto& n = built[i].node;
                const bool theirs = n.who == captive || (n.kind == A::Kind::Have && n.prop < A::kProps && a.propPocket[n.prop] == a.captive);
                if (theirs) fail("step " + Str(steps[i], "id") + ": " + name + " is held until a step frees them: nothing is done with them before that");
            }
            // One step frees them: the shortcut to it is plain. (Two ways of freeing them, the ransom paid or the door
            // broken, each write their own line: which of them happened cannot be said for the player, so none is taken.)
            int freeing = 0;
            for (std::size_t i = 0; i < steps.size(); ++i) {
                if (reach[i] && kindOf(i) == A::Kind::Free) {
                    ++freeing;
                    freeStep = static_cast<int>(i);
                }
            }
            if (freeing != 1) freeStep = -1;
            if (freeStep >= 0) {
                // What comes after it: a step the story can come back to once they are free is no step before.
                std::vector<bool> after(steps.size(), false);
                open.assign(1, static_cast<std::size_t>(freeStep));
                while (!open.empty()) {
                    const auto at = open.back();
                    open.pop_back();
                    for (const int to : leads[at]) {
                        const auto next = static_cast<std::size_t>(to);
                        if (to >= 0 && !after[next]) {
                            after[next] = true;
                            open.push_back(next);
                        }
                    }
                }
                for (std::size_t i = 0; i < steps.size(); ++i) leadsUp[i] = reach[i] && early[i] && !after[i];
            }
        } else if (std::any_of(built.begin(), built.end(), [](const Step& st) { return st.node.kind == A::Kind::Free; })) {
            fail("steps: a step frees someone, and nobody of the story is held");
        }
        if (pool.mustCaptive && !a.captive) {
            fail("people: this story is about someone held at one of the places: give one of the people \"held\": \"dX\", and free them in a step");
        }
        // A rescue is asked for by someone to whom the one taken belongs: never a stranger for no reason.
        // ---- what was left of a victim: what did it waits at that same place, and the story comes to it
        std::size_t examines = 0;
        for (std::size_t i = 0; i < steps.size(); ++i) {
            if (kindOf(i) != A::Kind::Examine) continue;
            const auto& clue = built[i].node;
            if (!reach[i]) {
                // A step that is never reached leaves nothing lying out there.
                if (clue.prop < A::kClues && a.clueSite[clue.prop] == clue.site) a.clueWhat[clue.prop] = a.clueSite[clue.prop] = 0;
                continue;
            }
            ++examines;
            std::vector<bool> seen(steps.size(), false);
            std::vector<std::size_t> ahead{i};
            bool met = false;
            while (!ahead.empty() && !met) {
                const auto at = ahead.back();
                ahead.pop_back();
                for (const int to : leads[at]) {
                    if (to < 0) continue;
                    const auto next = static_cast<std::size_t>(to);
                    if (seen[next]) continue;
                    seen[next] = true;
                    if (kindOf(next) == A::Kind::Slay && built[next].node.site == clue.site) {
                        met = true;
                        break;
                    }
                    ahead.push_back(next);
                }
            }
            if (!met && clue.site) {
                fail("step " + Str(steps[i], "id") + ": what did this is what lives in that place: a later step is a \"slay\" there");
            }
        }
        // ---- naming what did it: after having looked at what was left by that place, and before facing it there
        for (std::size_t i = 0; i < steps.size(); ++i) {
            if (!reach[i] || !built[i].naming || !built[i].node.site) continue;
            const auto site = built[i].node.site;
            const auto reaches = [&](std::size_t from, const std::function<bool(std::size_t)>& wanted) {
                std::vector<bool> seen(steps.size(), false);
                std::vector<std::size_t> ahead{from};
                while (!ahead.empty()) {
                    const auto at = ahead.back();
                    ahead.pop_back();
                    for (const int to : leads[at]) {
                        if (to < 0) continue;
                        const auto next = static_cast<std::size_t>(to);
                        if (seen[next]) continue;
                        seen[next] = true;
                        if (wanted(next)) return true;
                        ahead.push_back(next);
                    }
                }
                return false;
            };
            bool looked = false;
            for (std::size_t j = 0; j < steps.size(); ++j) {
                if (reach[j] && kindOf(j) == A::Kind::Examine && built[j].node.site == site && reaches(j, [&](std::size_t x) { return x == i; })) looked = true;
            }
            if (!looked) fail("step " + Str(steps[i], "id") + ": what did it can be named once its victim has been looked at: an \"examine\" by that place comes before");
            if (!reaches(i, [&](std::size_t x) { return kindOf(x) == A::Kind::Slay && built[x].node.site == site; })) {
                fail("step " + Str(steps[i], "id") + ": it is named before it is faced: a later step is the \"slay\" there");
            }
        }
        if (pool.mustExamine && !examines) {
            fail("steps: this story is a hunt for what is killing people: a step \"examine\" (what was left of a victim, outside one of the places), then the \"slay\" there");
        }
        if (pool.mustCaptive && a.captive) {
            const auto* held = personOf(static_cast<A::Role>(a.captive));
            if (held && !held->close) {
                fail("people: " + held->key + " is nothing to " + pool.people[0].key + ": the one held must be close to the one who asks (a line that says \"Close to\")");
            }
        }

        // ---- what makes a story worth living, held to by the engine (stories asked of the model only)
        if (pool.depth) {
            const auto among = [&](std::size_t i) {
                const auto k = kindOf(i);
                return k == A::Kind::Talk || k == A::Kind::Choose || k == A::Kind::Visit || k == A::Kind::Stalk || k == A::Kind::Brawl;
            };
            const auto outThere = [&](std::size_t i) {
                const auto k = kindOf(i);
                return k == A::Kind::Delve || k == A::Kind::Slay || k == A::Kind::Free || k == A::Kind::Examine;
            };
            std::set<int> endingsReached;
            bool anyPlace = false;
            int amongPeople = 0;
            for (std::size_t i = 0; i < steps.size(); ++i) {
                if (!reach[i]) continue;
                for (const auto& [when, b] : built[i].edges) if (b.to < 0) endingsReached.insert(b.to);
                anyPlace = anyPlace || outThere(i);
                amongPeople += among(i) ? 1 : 0;
            }
            if (endingsReached.size() < 2) {
                fail("steps: every way through this story leads to the same ending: give {player} a real choice, or a talk that can go two ways, and let those ways end differently (end:good, end:bad, end:other)");
            }
            if (pool.mustPlace && !anyPlace) {
                fail("steps: this story leads out into the world: one of the places listed must be part of it (\"delve\", \"slay\", or someone \"held\" there and a step \"free\")");
            }
            if (anyPlace) {
                if (amongPeople < 2) {
                    fail("steps: a place out in the world is one act of a story, never the whole of it: at least two steps among people (talk, choose, visit) besides it");
                }
                // What is found or done out there must change something among the people: no place step from which
                // only fetching and handing over follow.
                for (std::size_t i = 0; i < steps.size(); ++i) {
                    if (!reach[i] || !outThere(i)) continue;
                    std::vector<bool> seen(steps.size(), false);
                    std::vector<std::size_t> open{i};
                    seen[i] = true;
                    bool leadsOn = false;
                    while (!open.empty() && !leadsOn) {
                        const auto at = open.back();
                        open.pop_back();
                        for (const int to : leads[at]) {
                            const auto next = static_cast<std::size_t>(to);
                            if (to < 0 || seen[next]) continue;
                            seen[next] = true;
                            if (among(next)) leadsOn = true;
                            open.push_back(next);
                        }
                    }
                    if (!leadsOn) {
                        fail("step " + Str(steps[i], "id") + ": the story ends at the place, with nothing but fetching and handing over after it: what {player} finds or does there must lead on to a talk or a choice among the people (who is faced with it, what is decided)");
                        break;
                    }
                }
            }
        }
        if (!err.empty()) return r;

        // ---- the reward changes hands in person: an ending that pays, reached on a step that is not with the asker,
        // goes by one step more, back to the asker (A::IsWayBack). One such step for each ending that needs it.
        const auto atHand = [](const A::Node& n) {
            return n.who == A::RAsker &&
                   (n.kind == A::Kind::Talk || n.kind == A::Kind::Give || n.kind == A::Kind::Visit || n.kind == A::Kind::Choose);
        };
        const auto paysAway = [&](std::size_t i, const Branch& b) {
            return b.to < 0 && !atHand(built[i].node) &&
                   (b.effect == A::Effect::PayGold || a.ends[static_cast<std::size_t>(-b.to - 1)].pay);
        };
        std::array<int, 3> wayBack{-1, -1, -1};
        const std::size_t ownSteps = kept;
        for (std::size_t i = 0; i < steps.size(); ++i) {
            if (newIndex[i] < 0) continue;
            for (const auto& [when, b] : built[i].edges) {
                if (b.to >= 0) {
                    if (b.effect == A::Effect::PayGold && !atHand(built[i].node)) {
                        fail("step " + Str(steps[i], "id") + ": effect \"pay\" away from the one who asks: they pay in person; put it on a step with them, or let the ending pay");
                    }
                    continue;
                }
                const auto k = static_cast<std::size_t>(-b.to - 1);
                if (paysAway(i, b) && wayBack[k] < 0) wayBack[k] = static_cast<int>(kept++);
            }
        }
        if (kept > A::kMaxTaleNodes) {
            fail("steps: at most " + std::to_string(A::kMaxTaleNodes - (kept - ownSteps)) +
                 " here: the reward is paid by the one who asks in person, and the way back to them takes a step; end the story with them, or tell it in fewer steps");
        }
        if (!err.empty()) return r;

        // ---- the graph
        for (std::size_t i = 0; i < steps.size(); ++i) {
            if (newIndex[i] < 0) continue;
            auto& st = built[i];
            A::Node n = st.node;
            n.objective = static_cast<std::uint8_t>(A::kTaleObjective + newIndex[i]);
            std::size_t e = 0;
            // Found and freed already: this step only led there. Its objective goes (it never happened) and the story
            // stands at the step that frees them, which is done at once. Before the death of someone this step needed
            // (nobody needs the witness any more), after anything else that holds at the same moment (a way down that
            // was gone all the same is done, not left).
            A::Edge foundEarly;
            foundEarly.when = A::If::Freed;
            foundEarly.skip = true;
            const bool needsSomeone = n.who != A::RNone && n.who != A::RAsker && n.kind != A::Kind::Have && n.kind != A::Kind::Wait && n.kind != A::Kind::Ambush;
            bool shortcut = freeStep >= 0 && leadsUp[i] && newIndex[static_cast<std::size_t>(freeStep)] >= 0 &&
                            st.edges.size() + (needsSomeone ? 2u : 1u) <= n.edges.size();
            if (shortcut) foundEarly.next = static_cast<std::uint8_t>(newIndex[static_cast<std::size_t>(freeStep)]);
            if (shortcut && needsSomeone) {
                n.edges[e++] = foundEarly;
                shortcut = false;
            }
            // Someone the step needs is dead: the tale ends badly (the asker's death is the engine's own ending).
            // The men of an ambush were paid already: who sent them dying changes nothing for them.
            if (n.who != A::RNone && n.who != A::RAsker && n.kind != A::Kind::Have && n.kind != A::Kind::Wait && n.kind != A::Kind::Ambush) {
                A::Edge dead;
                dead.when = A::If::Dead;
                dead.role = n.who;
                dead.ending = A::Ending::Lost;
                dead.line = "{" + std::to_string(static_cast<int>(n.who)) + "} is dead.";
                n.edges[e++] = dead;
            }
            for (const auto& [when, b] : st.edges) {
                if (e >= n.edges.size()) break;
                A::Edge edge;
                edge.when = when;
                if (when == A::If::Persuaded) edge.prop = static_cast<std::uint8_t>(st.persuadeOption);
                else if ((b.effect == A::Effect::GiveItem || b.effect == A::Effect::Remedy) && b.prop >= 0) edge.prop = static_cast<std::uint8_t>(b.prop);
                const bool goesBack = paysAway(i, b);
                if (b.to >= 0) {
                    edge.next = static_cast<std::uint8_t>(newIndex[static_cast<std::size_t>(b.to)]);
                } else if (goesBack) {
                    edge.next = static_cast<std::uint8_t>(wayBack[static_cast<std::size_t>(-b.to - 1)]);
                } else {
                    edge.ending = b.to == -1 ? A::Ending::Done : b.to == -2 ? A::Ending::Failed : A::Ending::StayedOut;
                }
                edge.line = b.line;
                edge.effect = b.effect;
                edge.effectRole = b.effectRole;
                if (goesBack && edge.effect == A::Effect::PayGold) {   // paid on the way back, not here
                    edge.effect = A::Effect::None;
                    edge.effectRole = A::RNone;
                }
                if (edge.effect == A::Effect::GiveItem && edge.effectRole == A::RNone) edge.effectRole = n.who;   // who hands it over
                edge.bondWith = b.bondWith;
                if (b.price) a.price = std::max(a.price, b.price);
                // A good or other ending the author marked as paid: the asker pays the reward as the tale closes.
                if (b.to < 0 && !goesBack) {
                    const auto k = static_cast<std::size_t>(-b.to - 1);
                    if (a.ends[k].pay) {
                        if (edge.effect == A::Effect::None) {
                            edge.effect = A::Effect::PayGold;
                            edge.effectRole = A::RAsker;
                        } else if (edge.effect != A::Effect::PayGold) {
                            edge.effect2 = A::Effect::PayGold;
                            edge.effect2Role = A::RAsker;
                        }
                    }
                }
                n.edges[e++] = edge;
            }
            if (shortcut && e < n.edges.size()) n.edges[e++] = foundEarly;
            a.graph.push_back(std::move(n));
        }
        for (std::size_t index = ownSteps; index < kept; ++index) {
            std::size_t k = 0;
            while (k < wayBack.size() && wayBack[k] != static_cast<int>(index)) ++k;
            if (k >= wayBack.size()) continue;
            const auto ending = k == 0 ? A::Ending::Done : k == 1 ? A::Ending::Failed : A::Ending::StayedOut;
            A::Node n;
            n.kind = A::Kind::Talk;
            n.who = n.marker = A::RAsker;
            n.objective = static_cast<std::uint8_t>(A::kTaleObjective + index);
            n.goal = "Tell {1} how it ended";
            A::Edge dead;
            dead.when = A::If::Dead;
            dead.role = A::RAsker;
            dead.ending = ending;
            dead.line = "{1} died before I could tell them how it ended.";
            A::Edge told;
            told.when = A::If::Done;
            told.ending = ending;
            told.effect = A::Effect::PayGold;
            told.effectRole = A::RAsker;
            A::Edge never;
            never.when = A::If::Waited;
            never.hours = A::kWayBackHours;
            never.ending = ending;
            never.line = "I never went back to {1} about it, and the reward stayed where it was.";
            n.edges[0] = dead;
            n.edges[1] = told;
            n.edges[2] = never;
            a.graph.push_back(std::move(n));
            r.notes += "the reward is paid in person: a step back to the one who asks was added\n";
        }
        if (a.propAmbush && !std::any_of(a.graph.begin(), a.graph.end(), [](const A::Node& n) { return n.kind == A::Kind::Ambush; })) {
            fail("items: an item the ambush's leader carries needs a step that is an ambush");
        }
        // Every prop hidden must be found somewhere, and found before it is given.
        for (std::size_t p = 0; p < propOf.size(); ++p) {
            const bool found = std::any_of(a.graph.begin(), a.graph.end(), [p](const A::Node& n) {
                if (n.kind == A::Kind::Have && n.prop == p) return true;
                return std::any_of(n.edges.begin(), n.edges.end(), [p](const A::Edge& e) { return e.effect == A::Effect::GiveItem && e.prop == p; });
            });
            if (!found) r.notes += "item " + std::to_string(p) + " is never searched for: it can only turn up by chance\n";
        }
        r.ok = err.empty();
        return r;
    }

}
