#pragma once

// Words of place: "upstairs", "outside", "home", "Alvor's house", "the kitchen". A dialogue model fills a destination
// with them as readily as with a name, and it does so in the language it speaks.
//
// The resolver knew these words in English only, as lists written into its code: one list for the whole of what was
// said, others for words found among others, a pattern for somebody's home and one for leaving a place; and it told a
// cellar door, a bed or a cooking pot by English words in their names. In a German game no door was a cellar door.
//
// Here the words are data and the rules have none. Every language, English among them, gives the same rows (a pack's
// file, LanguagePack.h; English's rows are the text below, in that very form, so English is one language of many and
// always in force):
//
//   place   <kind>   forms     the whole of what was said, little words aside ("the kitchen", "nach oben")
//   phrase  <kind>   forms     found anywhere in it, before the patterns below are tried ("wash yourself")
//   part    <kind>   forms     found anywhere in it, after them, in the order of the rows ("go upstairs now")
//   word    <kind>   forms     the same, as a whole word only ("head home now", never "Breezehome")
//   owner   house    forms     the word for somebody's home behind their name ("Alvor's house", "Camilla home")
//   owner   first    forms     ... or before it, in languages that say it so ("dom Alvora", "la maison de")
//   owner   of       forms     what hangs on the owner's name in front of it ("'s")
//   owner   go       forms     words of going in front of the owner, let go ("go to")
//   owner   not      forms     single words that are nobody's name ("my", "back": "back home" is not Back's home)
//   leave   from     forms     leaving a place: the form, then the place ("out of Helgen", "leave the inn")
//   leave   here     forms     ... unless the place is only "here"
//   bed     *        forms     words that ask for a bed along with a direction ("upstairs bedroom")
//   room    *        forms     words of a spot in a room that are no direction ("the bar", "hearth")
//   name    <what>   forms     words in the NAMES of things in the game, for what the game's own data does not tell:
//                              upstairs / downstairs (a door or a cell), bed, cooking, bath, water, coffin, shrine
//                              (an object)
//
// Kinds: upstairs downstairs outside inside back cellar bedroom kitchen bath river lake water home, and "your home".
// A pack that has only `place` rows (all that ship today) works as before; its single words also stand in for `name`
// rows it does not have ("keller" in a cell's name is a cellar).
//
// What the game itself knows comes first and needs no word: a bed is furniture one can sleep in, a cooking pot carries
// the game's keyword, up and down are heights of doors (CellAnalyzer). Words in names are the last resort.
//
// Pure logic (no game types), ASCII; checked by PlaceWordsCheck, which also holds the old English rules beside these
// and compares them.

#include "LanguagePack.h"
#include "NameForms.h"

#include <string>
#include <string_view>
#include <vector>

namespace IntelEngine::PlaceWords {

    // English, in the rows of a pack (tabs between the three fields).
    inline constexpr const char* kEnglish =
        "meta\tcode\ten\n"
        "meta\tname\tEnglish\n"
        "place\tupstairs\tupstairs|up|above\n"
        "place\tdownstairs\tdownstairs|down|below|stairs|stairwell|staircase\n"
        "place\toutside\toutside|out|exterior|leave|get out|get out of here|away\n"
        "place\tinside\tinside|in|interior\n"
        "place\tback\tthe back|back room|back\n"
        "place\tcellar\tcellar|basement|below stairs\n"
        "place\tbedroom\tbedroom|the bedroom|my room|my bed|bed|room\n"
        "place\tkitchen\tkitchen|the kitchen\n"
        "place\tbath\tbath|the bath|bathe|hot spring|hot springs|pool|the pool|wash|wash up|go wash|clean up|take a bath\n"
        "place\triver\tthe river|river|the lake|lake|stream|the stream|the water|water|the shore|swim|go swim|go swimming|go fishing|fishing\n"
        "place\thome\thome|my home|my house|my place|go home|head home|back home\n"
        "place\tyour home\tyour home|your house|your place|the player's home|player's home\n"
        "phrase\tbath\twash yourself|wash myself|clean yourself\n"
        "phrase\triver\tgo to the river|go to the lake\n"
        "part\tupstairs\tupstairs|upper floor\n"
        "part\tdownstairs\tdownstairs|basement|cellar|stairwell|staircase|stairs\n"
        "part\toutside\toutside|exterior\n"
        "part\tbedroom\tbedroom|my bed\n"
        "part\tkitchen\tkitchen\n"
        "part\triver\triver|lake|swim|fishing\n"
        "part\tbath\tbath|hot spring\n"
        "part\thome\tmy home|my house|my place\n"
        "part\tyour home\tyour home|your house|your place\n"
        "word\thome\thome\n"
        "owner\thouse\thome|house|place\n"
        "owner\tof\t's\n"
        "owner\tgo\tgo to|head to|travel to|get to\n"
        "owner\tnot\tgo|my|your|head|back|the|get|at|to|a|our|their|its|this\n"
        "leave\tfrom\tout of|outside of|outside|leave\n"
        "leave\there\there|this place|this building\n"
        "bed\t*\tbedroom|my room|my bed\n"
        "room\t*\tthe bar|counter|bar counter|near the fire|fireplace|hearth\n"
        "name\tupstairs\tupstairs|upper|second floor|top floor|upper level|attic\n"
        "name\tdownstairs\tdownstairs|lower|cellar|basement|dungeon|below|crypt|undercroft\n"
        "name\tbed\tbed|hammock|sleeping\n"
        "name\tcooking\tcooking|cook|oven|spit|cauldron\n"
        "name\tbath\tbath|tub|pool|jacuzzi|hot spring|basin|wash\n"
        "name\twater\tdock|pier|boat|canoe|ship|fish|fishing|bridge|mill|lumber|waterfall|river|lake|stream|pond|harbor|harbour|well|fountain\n"
        "name\tcoffin\tcoffin|sarcophag\n"
        "name\tshrine\tshrine|altar\n";

    inline const Language::Pack& English() {
        static const Language::Pack pack = Language::ReadPack(kEnglish, "english", {});
        return pack;
    }

    // The packs in force for a reading, English among them (the order they are tried in).
    using Packs = std::vector<const Language::Pack*>;

    enum class Kind { None, Upstairs, Downstairs, Outside, Inside, Back, Cellar, Bedroom, Kitchen, Home, Water };
    enum class Owner { None, Self, Player, Named };

    struct Intent {
        Kind kind = Kind::None;
        std::string context;          // the place that is left ("helgen" of "out of Helgen")
        bool preferBeds = false;
        Owner owner = Owner::None;
        std::string ownerName;        // whose home, as it was written
        bool preferInterior = false;  // water: a bath before a river

        bool operator==(const Intent&) const = default;
    };

    namespace detail {
        inline Intent OfKey(std::string_view key, bool bed) {
            Intent i;
            if (key == "upstairs") {
                i.kind = Kind::Upstairs;
                i.preferBeds = bed;
            } else if (key == "downstairs" || key == "stairs") {
                i.kind = Kind::Downstairs;
                i.preferBeds = bed;
            } else if (key == "outside") {
                i.kind = Kind::Outside;
            } else if (key == "inside") {
                i.kind = Kind::Inside;
            } else if (key == "back") {
                i.kind = Kind::Back;
            } else if (key == "cellar") {
                i.kind = Kind::Cellar;
            } else if (key == "bedroom") {
                i.kind = Kind::Bedroom;
                i.preferBeds = true;
            } else if (key == "kitchen") {
                i.kind = Kind::Kitchen;
            } else if (key == "bath") {
                i.kind = Kind::Water;
                i.preferInterior = true;
            } else if (key == "river" || key == "lake" || key == "water") {
                i.kind = Kind::Water;
            } else if (key == "home") {
                i.kind = Kind::Home;
                i.owner = Owner::Self;
            } else if (key == "your home") {
                i.kind = Kind::Home;
                i.owner = Owner::Player;
            }
            return i;
        }

        inline std::string Trim(std::string_view s) { return Language::detail::Trim(s); }

        // Every word of a section and key, over the packs.
        template <class F>
        void Each(const Packs& packs, std::string_view section, std::string_view key, F&& f) {
            for (const auto* pack : packs) {
                const auto* list = pack ? pack->More(section) : nullptr;
                if (!list) continue;
                for (const auto& w : *list) {
                    if (key.empty() || w.key == key) f(w);
                }
            }
        }

        inline bool WholeWordAt(std::string_view text, std::size_t at, std::size_t size) {
            return Language::detail::BoundaryBefore(text, at) && Language::detail::BoundaryAt(text, at + size);
        }

        // Does `text` hold one of the forms of this section and key, anywhere in it?
        inline bool Holds(std::string_view text, const Packs& packs, std::string_view section, std::string_view key) {
            bool held = false;
            Each(packs, section, key, [&](const Language::Word& w) { held = held || text.find(w.form) != std::string_view::npos; });
            return held;
        }

        // Every word of what is left is one a pack lets go (or nothing is left).
        inline bool OnlyFree(std::string_view left, const NameForms::FreeWord& free) {
            const NameForms::Words rest = NameForms::Read(left);
            for (std::size_t i = 0; i < rest.at.size(); ++i) {
                if (!(free && free(rest.Word(i)))) return false;
            }
            return true;
        }

        inline std::vector<const std::vector<Language::Word>*> PlaceLists(const Packs& packs) {
            std::vector<const std::vector<Language::Word>*> lists;
            for (const auto* pack : packs) {
                if (pack) lists.push_back(&pack->place);
            }
            return lists;
        }
    }

    // The whole of what was said is words of place (little words aside): the hits, or none when anything else is in
    // it. "the kitchen" yes; "the kitchen knife" no.
    inline std::vector<const Language::Word*> Whole(std::string_view folded, const Packs& packs, const NameForms::FreeWord& free) {
        Language::Read r = Language::Reading(folded, detail::PlaceLists(packs));
        if (r.hits.empty() || !detail::OnlyFree(r.left, free)) return {};
        return std::move(r.hits);
    }

    // What was said, read as a word of place. Kind::None: it is a name, or nothing this knows. `folded`: lower case.
    inline Intent Read(std::string_view said, const Packs& packs, const NameForms::FreeWord& free = {}) {
        const std::string folded = detail::Trim(said);
        if (folded.empty()) return {};
        const bool bed = detail::Holds(folded, packs, "bed", {});

        // 1. The whole of it is one word of place.
        const auto whole = Whole(folded, packs, free);
        if (!whole.empty()) {
            bool one = true;
            for (const auto* w : whole) one = one && w->key == whole.front()->key;
            if (one) return detail::OfKey(whole.front()->key, bed);
        }

        // 2. A phrase that settles it wherever it stands.
        {
            Intent found;
            detail::Each(packs, "phrase", {}, [&](const Language::Word& w) {
                if (found.kind == Kind::None && folded.find(w.form) != std::string::npos) found = detail::OfKey(w.key, bed);
            });
            if (found.kind != Kind::None) return found;
        }

        // 3. Somebody's home: their name, then the word for a home ("alvor's house", "camilla valerius home"), or the
        //    word first where a language says it so.
        {
            std::vector<const Language::Word*> houses, firsts, ofs, gos, nots;
            detail::Each(packs, "owner", {}, [&](const Language::Word& w) {
                (w.key == "house" ? houses : w.key == "first" ? firsts : w.key == "of" ? ofs : w.key == "go" ? gos : nots).push_back(&w);
            });
            const auto owner = [&](std::string name, bool bare) -> std::string {
                for (const auto* go : gos) {
                    if (name.size() > go->form.size() + 1 && name.compare(0, go->form.size(), go->form) == 0 && name[go->form.size()] == ' ') {
                        name = name.substr(go->form.size() + 1);
                        break;
                    }
                }
                name = detail::Trim(name);
                if (name.empty()) return {};
                if (bare && name.find(' ') == std::string::npos) {
                    for (const auto* no : nots) {
                        if (no->form == name) return {};
                    }
                    if (free && free(Language::detail::U32(name))) return {};
                }
                return name;
            };
            // with what hangs on the name ("'s"), then bare
            for (const bool bare : {false, true}) {
                for (const auto* of : bare ? std::vector<const Language::Word*>{nullptr} : ofs) {
                    for (const auto* house : houses) {
                        const std::string tail = (of ? of->form : std::string{}) + " " + house->form;
                        const auto at = folded.find(tail);
                        if (at == std::string::npos || at == 0) continue;
                        const std::string name = owner(folded.substr(0, at), bare);
                        if (name.empty()) continue;
                        Intent i;
                        i.kind = Kind::Home;
                        i.owner = Owner::Named;
                        i.ownerName = name;
                        return i;
                    }
                }
            }
            for (const auto* first : firsts) {
                if (folded.size() <= first->form.size() + 1 || folded.compare(0, first->form.size(), first->form) != 0 ||
                    folded[first->form.size()] != ' ') {
                    continue;
                }
                const std::string name = owner(folded.substr(first->form.size() + 1), true);
                if (name.empty()) continue;
                Intent i;
                i.kind = Kind::Home;
                i.owner = Owner::Named;
                i.ownerName = name;
                return i;
            }
        }

        // 4. Leaving a place: the word, then the place.
        {
            Intent found;
            detail::Each(packs, "leave", "from", [&](const Language::Word& w) {
                if (found.kind != Kind::None) return;
                if (folded.size() <= w.form.size() + 1 || folded.compare(0, w.form.size(), w.form) != 0 || folded[w.form.size()] != ' ') return;
                const std::string where = detail::Trim(std::string_view(folded).substr(w.form.size() + 1));
                if (where.empty()) return;
                found.kind = Kind::Outside;
                bool here = false;
                detail::Each(packs, "leave", "here", [&](const Language::Word& h) { here = here || h.form == where; });
                if (!here) found.context = where;
            });
            if (found.kind != Kind::None) return found;
        }

        // 5. Several words of place and nothing else ("upstairs bedroom", a pack's "nach oben ins schlafzimmer"), and
        //    words of place among other words: the first row that is found, in the order of the rows. Several words
        //    of place are read by the `part` rows of the languages they are in, where those languages have them
        //    ("get out of here bedroom" is the bedroom, as English always read it), else by their meanings.
        if (!whole.empty()) {
            for (const auto* pack : packs) {
                if (!pack || pack->place.empty()) continue;
                bool mine = false;
                for (const auto* w : whole) mine = mine || (w >= pack->place.data() && w < pack->place.data() + pack->place.size());
                if (!mine) continue;
                if (const auto* parts = pack->More("part")) {
                    for (const auto& w : *parts) {
                        if (folded.find(w.form) != std::string::npos) return detail::OfKey(w.key, bed);
                    }
                }
            }
            bool wantsBed = bed;
            for (const auto* w : whole) wantsBed = wantsBed || w->key == "bedroom";
            for (const std::string_view key : {"upstairs", "downstairs", "cellar", "stairs", "outside", "bedroom", "kitchen", "river", "lake",
                                               "water", "bath", "home", "your home"}) {
                for (const auto* w : whole) {
                    if (w->key == key) return detail::OfKey(key == "cellar" ? std::string_view("downstairs") : key, wantsBed);
                }
            }
            return {};
        }
        for (const auto* pack : packs) {
            if (!pack) continue;
            if (const auto* parts = pack->More("part")) {
                for (const auto& w : *parts) {
                    if (folded.find(w.form) != std::string::npos) return detail::OfKey(w.key, bed);
                }
            }
            if (const auto* words = pack->More("word")) {
                for (const auto& w : *words) {
                    for (auto at = folded.find(w.form); at != std::string::npos; at = folded.find(w.form, at + 1)) {
                        if (detail::WholeWordAt(folded, at, w.form.size())) return detail::OfKey(w.key, bed);
                    }
                }
            }
        }
        return {};
    }

    // The whole of what was said is one plain word of place or of a spot in a room ("upstairs", "the kitchen", "the
    // bar"): such a word keeps its meaning even where something in the game is called the same.
    inline bool Plain(std::string_view said, const Packs& packs, const NameForms::FreeWord& free = {}) {
        const std::string folded = detail::Trim(said);
        if (folded.empty()) return false;
        if (!Whole(folded, packs, free).empty()) return true;
        bool room = false;
        detail::Each(packs, "room", {}, [&](const Language::Word& w) { room = room || w.form == folded; });
        return room;
    }

    // The place behind a word of leaving or of going at the front ("out of helgen", "go to riverwood"): the rest, or
    // empty when there is none or it is only "here".
    inline std::string Behind(std::string_view said, const Packs& packs) {
        const std::string folded = detail::Trim(said);
        std::string found;
        const auto tryForm = [&](const Language::Word& w) {
            if (!found.empty()) return;
            if (folded.size() <= w.form.size() + 1 || folded.compare(0, w.form.size(), w.form) != 0 || folded[w.form.size()] != ' ') return;
            const std::string rest = detail::Trim(std::string_view(folded).substr(w.form.size() + 1));
            bool here = rest.empty();
            detail::Each(packs, "leave", "here", [&](const Language::Word& h) { here = here || h.form == rest; });
            if (!here) found = rest;
        };
        detail::Each(packs, "leave", "from", tryForm);
        detail::Each(packs, "owner", "go", tryForm);
        return found;
    }

    // Does the name of something in the game say what it is: "upstairs" / "downstairs" for a door or a cell, "bed",
    // "cooking", "bath", "water" for an object? By the `name` rows; a pack without them lends the single words of its
    // `place` rows of that meaning, from four letters on ("keller", "podval" in their own letters).
    inline bool NameSays(std::string_view foldedName, std::string_view what, const Packs& packs) {
        if (foldedName.empty()) return false;
        for (const auto* pack : packs) {
            if (!pack) continue;
            if (const auto* names = pack->More("name")) {
                bool has = false;
                for (const auto& w : *names) {
                    if (w.key != what) continue;
                    has = true;
                    if (foldedName.find(w.form) != std::string_view::npos) return true;
                }
                if (has) continue;
            }
            for (const auto& w : pack->place) {
                const bool means = what == "upstairs" ? w.key == "upstairs"
                                 : what == "downstairs" ? (w.key == "downstairs" || w.key == "cellar")
                                 : what == "bath" ? w.key == "bath"
                                 : what == "water" ? (w.key == "river" || w.key == "lake")
                                 : false;
                if (!means || w.letters < 4 || w.form.find(' ') != std::string::npos) continue;
                if (foldedName.find(w.form) != std::string_view::npos) return true;
            }
        }
        return false;
    }
}
