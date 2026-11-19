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
//   "items":  [ {"id": "i1", "item": "<a name of the item list>", "name": "Ysolda's Silver Ring", "hidden": "home"|"pocket"|"at"|"ambush", "of": "p3"} ],
//   "offer":  {"words": "what drives the asker to come", "ask": "what the player is asked", "journal": "first line", "reward": 150, "days": 3},
//   "steps":  [ {"id": "s1", "do": "talk|find|give|choose|visit|wait|brawl|go|delve|slay|ambush", ...}, ... ],
//   "endings": {"good": {...}, "bad": {...}, "other": {...}}   each {"journal", "rumor", "pay", "memories": {"p0": "..."}}
// }
// A branch ("then") names a step id or "end:good", "end:bad", "end:other"; an "effect" is one of "jail pX", "leave pX",
// "befriend pX pY", "estrange pX pY", "pay", "player pays pX N", "give iN" (the step's person hands the item over).

#include "Arcs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
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

    // A place out in the world the story may lead to.
    struct Site {
        std::string key;              // "d1"
        std::string name;
        std::uint32_t location = 0;
        std::uint32_t anchor = 0;     // its boss room
        std::uint8_t kind = SiteNone;
    };

    struct Pool {
        std::vector<Person> people;   // people[0] is the asker
        std::vector<Site> sites;      // places near the town (never one a quest of the game still needs)
        std::vector<Item> items;
        std::uint32_t gold = 150;     // the reward the engine would offer (the author may move it within limits)
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
                std::size_t n = limit;
                while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
                s.resize(n);
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

        // ---- the offer
        const auto& offer = tale.contains("offer") ? tale["offer"] : nlohmann::json::object();
        a.offerWords = Words(Str(offer, "words"), 600);
        a.offerAsk = Words(Str(offer, "ask"), 200);
        a.opening = Words(Str(offer, "journal"), kMaxLine);
        a.gold = static_cast<std::uint32_t>(std::clamp(Int(offer, "reward", static_cast<int>(pool.gold)), 25, 1000));
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
            end.pay = k == 0 ? !e.contains("pay") || Bool(e, "pay") : Bool(e, "pay");
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
        };
        std::vector<Step> built(steps.size());
        int ambushes = 0;   // one ambush a story (its men are the arc's own)
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
                        n.options[k] = Words(Str(o, "say"), 90);
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
            } else if (kind == "ambush") {
                // Hired men on the player's road, sent by someone of the story ("by"; left out when nobody knows yet).
                if (ambushes++ > 0) fail(where + ": one ambush in a story");
                n.kind = A::Kind::Ambush;
                const auto by = Str(s, "by");
                n.who = by.empty() ? A::RNone : person(by, where.c_str());
                n.why = Words(Str(s, "why"), 200);
                if (n.why.empty()) fail(where + ": an ambush needs \"why\": what the men were paid to do");
                a.thugName = Words(Str(s, "leader"), 50);
                st.edges.push_back({A::If::Done, branch(s, i, where)});
            } else if (kind == "wait") {
                n.kind = A::Kind::Wait;
                n.waitHours = static_cast<float>(std::clamp(Int(s, "hours", 12), 1, 72));
                st.edges.push_back({A::If::Done, branch(s, i, where)});
            } else {
                fail(where + ": \"do\" must be talk, find, give, choose, visit, wait, brawl, go, delve, slay or ambush");
            }
            // No marker on the one who sent an ambush: the men are not where they are.
            if (n.kind != A::Kind::Have && n.kind != A::Kind::Wait && n.kind != A::Kind::Ambush && n.marker == A::RNone) n.marker = n.who;
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

        // ---- the graph
        for (std::size_t i = 0; i < steps.size(); ++i) {
            if (newIndex[i] < 0) continue;
            auto& st = built[i];
            A::Node n = st.node;
            n.objective = static_cast<std::uint8_t>(A::kTaleObjective + newIndex[i]);
            std::size_t e = 0;
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
                else if (b.effect == A::Effect::GiveItem && b.prop >= 0) edge.prop = static_cast<std::uint8_t>(b.prop);
                if (b.to >= 0) {
                    edge.next = static_cast<std::uint8_t>(newIndex[static_cast<std::size_t>(b.to)]);
                } else {
                    edge.ending = b.to == -1 ? A::Ending::Done : b.to == -2 ? A::Ending::Failed : A::Ending::StayedOut;
                }
                edge.line = b.line;
                edge.effect = b.effect;
                edge.effectRole = b.effectRole;
                if (edge.effect == A::Effect::GiveItem && edge.effectRole == A::RNone) edge.effectRole = n.who;   // who hands it over
                edge.bondWith = b.bondWith;
                if (b.price) a.price = std::max(a.price, b.price);
                // A good or other ending the author marked as paid: the asker pays the reward as the tale closes.
                if (b.to < 0) {
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
            a.graph.push_back(std::move(n));
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
