#pragma once

// Language packs: what IntelEngine has to know about a language, kept in files and not in code.
//
// The mod is played in games of many languages, and in games of one language where the people speak another (an
// English game whose people speak Gheg Albanian). Three things then go wrong with words that were only ever
// expected in English:
//   - the dialogue model fills an action's time, or a place like "home", in the language it speaks ("zavtra utrom",
//     "neser ne mengjes", written in their own letters), and the readers knew English only;
//   - "none", the word for an empty field, comes back translated and was taken for a value;
//   - what the player reads (notices, the words on buttons) is English whatever the game is in.
//
// A pack is one file, Data/SKSE/Plugins/IntelEngine/languages/<name>.tsv, UTF-8, three fields a line with tabs
// between them: section, key, value. Lines beginning with # and empty lines are skipped.
//
//   meta   code     ru                 the language's code
//   meta   codes    ru|rus             every code that means it (SkyrimNet's spoken-language codes among them)
//   meta   name     Russian            its English name: what a prompt is told to write in
//   meta   game     russian            the game's own word for it (Skyrim.ini, sLanguage); left out when the game
//                                      does not ship the language
//   meta   script   cyrillic           what its names are written in: latin, cyrillic, hangul, kana, han
//   time   tomorrow <forms>            words of time: the key is the English word the time reader knows, the value
//   place  home     <forms>            every form of it, | between them. Likewise words of place ("home",
//   none   none     <forms>            "outside", "upstairs") and the words for "none".
//   free   *        <forms>            little words that are no part of a name beside it (articles, prepositions)
//   label  <English, with {1} {2}>  <the same in this language>     what the player reads
//   meta   codepage 1250               the Windows code page old plugins of this language were saved in (GameText.h);
//                                      left out: told by the language's code
//   phrase / part / word / owner / leave / bed / room / name   <key>  <forms>
//                                      more words of place, read by PlaceWords.h (which says what each is); English
//                                      has them built in, in this same form
//
// Which packs are in force is decided once a game (LanguageBridge): the pack of the language the game is in, for
// what the player reads; that pack and the packs of the languages people speak, for what the model writes. English
// needs no pack: it is what the code speaks. Adding a language is adding a file.
//
// Pure logic (no game types), checked by LanguagePackCheck. This header is kept ASCII.

#include "NameForms.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace IntelEngine::Language {

    // Lower case for any script (FoldUtf8 in the game; the check passes the same).
    using Fold = std::function<std::string(const std::string&)>;

    // One form of a word and the English it stands for.
    struct Word {
        std::string form;         // folded
        std::string key;          // English, as the readers know it; may be several words ("in the evening")
        std::size_t letters = 0;
        bool unspaced = false;    // written in a script without spaces: found anywhere, not only as a whole word
    };

    // A sentence the player reads, with holes for names: "{1} headed off to {2}".
    struct Label {
        std::vector<std::string> fixed;   // the pieces around the holes: n holes, n + 1 pieces
        std::vector<int> hole;            // the number each hole carries
        std::string to;                   // the translation, with the same numbers
        std::size_t weight = 0;           // letters that are fixed: the more, the more particular the pattern
    };

    struct Pack {
        std::string file;                 // "russian"
        std::string code;                 // "ru"
        std::vector<std::string> codes;   // lower case
        std::string name;                 // "Russian"
        std::string game;                 // "russian", lower case; empty: the game does not ship it
        std::string script;               // latin, cyrillic, hangul, kana, han
        std::vector<Word> time, place, none;
        std::vector<std::u32string> free;
        std::vector<Label> labels;
        int codepage = 0;                 // meta codepage; 0: not said
        // The other sections of place words (PlaceWords.h), each in the order of its rows: section name and words.
        std::vector<std::pair<std::string, std::vector<Word>>> more;
        std::size_t rows = 0, bad = 0;

        const std::vector<Word>* More(std::string_view section) const {
            for (const auto& entry : more) {
                if (entry.first == section) return &entry.second;
            }
            return nullptr;
        }

        bool Is(std::string_view what) const {   // by file name, code or English name, lower case
            if (what.empty()) return false;
            if (what == file || what == code) return true;
            std::string lowerName = name;
            for (auto& c : lowerName) c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            if (what == lowerName) return true;
            return std::find(codes.begin(), codes.end(), std::string(what)) != codes.end();
        }
    };

    namespace detail {
        inline std::string Trim(std::string_view s) {
            const auto b = s.find_first_not_of(" \t\r\n");
            if (b == std::string_view::npos) return {};
            const auto e = s.find_last_not_of(" \t\r\n");
            return std::string(s.substr(b, e - b + 1));
        }
        inline std::string AsciiLower(std::string s) {
            for (auto& c : s) c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            return s;
        }
        inline std::vector<std::string> Bars(std::string_view s) {
            std::vector<std::string> out;
            std::size_t at = 0;
            while (at <= s.size()) {
                const auto bar = s.find('|', at);
                const auto piece = Trim(s.substr(at, bar == std::string_view::npos ? std::string_view::npos : bar - at));
                if (!piece.empty()) out.push_back(piece);
                if (bar == std::string_view::npos) break;
                at = bar + 1;
            }
            return out;
        }
        // The letters of a word as NameForms compares them: plain letters (LetterFold.h), so that a pack's little
        // word written with its accent is the word NameForms reads.
        inline std::u32string U32(std::string_view s) {
            std::u32string out;
            char32_t c = 0;
            char32_t plain[2];
            for (std::size_t i = 0; i < s.size();) {
                i += NameForms::Decode(s, i, c);
                const int n = LetterFold::Fold(c, plain);
                for (int k = 0; k < n; ++k) out.push_back(plain[k]);
            }
            return out;
        }
        inline bool PlaceSection(std::string_view section) {
            for (const std::string_view known : {"phrase", "part", "word", "owner", "leave", "bed", "room", "name"}) {
                if (section == known) return true;
            }
            return false;
        }
    }

    // "{1} headed off to {2}" into its pieces. False when a hole is written wrongly or two holes touch (nothing
    // would tell where one name ends and the next begins).
    inline bool ReadPattern(std::string_view pattern, Label& out) {
        out.fixed.assign(1, std::string{});
        out.hole.clear();
        out.weight = 0;
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 1] >= '1' && pattern[i + 1] <= '9' && pattern[i + 2] == '}') {
                if (!out.hole.empty() && out.fixed.back().empty()) return false;
                out.hole.push_back(pattern[i + 1] - '0');
                out.fixed.emplace_back();
                i += 2;
                continue;
            }
            out.fixed.back().push_back(pattern[i]);
            ++out.weight;
        }
        return true;
    }

    // One line of a pack into it. Returns false for a line that is neither empty, a comment nor understood.
    inline bool AddLine(Pack& pack, std::string_view line, const Fold& fold) {
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF)
            line.remove_prefix(3);   // a byte-order mark at the top of the file
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos || line[first] == '#') return true;
        const auto t1 = line.find('\t');
        const auto t2 = t1 == std::string_view::npos ? t1 : line.find('\t', t1 + 1);
        if (t2 == std::string_view::npos) return false;
        const std::string section = detail::AsciiLower(detail::Trim(line.substr(0, t1)));
        const std::string key = detail::Trim(line.substr(t1 + 1, t2 - t1 - 1));
        const std::string value = detail::Trim(line.substr(t2 + 1));
        if (section.empty() || value.empty()) return false;
        ++pack.rows;

        if (section == "meta") {
            const std::string k = detail::AsciiLower(key);
            if (k == "code") pack.code = detail::AsciiLower(value);
            else if (k == "codes") for (auto& c : detail::Bars(value)) pack.codes.push_back(detail::AsciiLower(c));
            else if (k == "name") pack.name = value;
            else if (k == "game") pack.game = detail::AsciiLower(value);
            else if (k == "script") pack.script = detail::AsciiLower(value);
            else if (k == "codepage") {
                int page = 0;
                for (const char c : value) {
                    if (c < '0' || c > '9' || page > 99999) return false;
                    page = page * 10 + (c - '0');
                }
                pack.codepage = page;
            }
            else return false;
            return true;
        }
        if (detail::PlaceSection(section)) {
            if (key.empty()) return false;
            std::vector<Word>* list = nullptr;
            for (auto& entry : pack.more) {
                if (entry.first == section) list = &entry.second;
            }
            if (!list) {
                pack.more.emplace_back(section, std::vector<Word>{});
                list = &pack.more.back().second;
            }
            for (const auto& raw : detail::Bars(value)) {
                Word w;
                w.form = fold ? fold(raw) : raw;
                w.key = detail::AsciiLower(key);
                const auto letters = detail::U32(w.form);
                w.letters = letters.size();
                w.unspaced = !letters.empty() && NameForms::Unspaced(letters.front());
                if (w.letters > 0) list->push_back(std::move(w));
            }
            return true;
        }
        if (section == "time" || section == "place" || section == "none") {
            if (key.empty()) return false;
            auto& list = section == "time" ? pack.time : section == "place" ? pack.place : pack.none;
            for (const auto& raw : detail::Bars(value)) {
                Word w;
                w.form = fold ? fold(raw) : raw;
                w.key = detail::AsciiLower(key);
                const auto letters = detail::U32(w.form);
                w.letters = letters.size();
                w.unspaced = !letters.empty() && NameForms::Unspaced(letters.front());
                if (w.letters > 0) list.push_back(std::move(w));
            }
            return true;
        }
        if (section == "free") {
            for (const auto& raw : detail::Bars(value)) pack.free.push_back(detail::U32(fold ? fold(raw) : raw));
            return true;
        }
        if (section == "label") {
            Label label;
            if (key.empty() || !ReadPattern(key, label)) return false;
            // The translation may put the holes in another order, never ask for one the English has not.
            Label there;
            if (!ReadPattern(value, there)) return false;
            for (const int h : there.hole) {
                if (std::find(label.hole.begin(), label.hole.end(), h) == label.hole.end()) return false;
            }
            label.to = value;
            pack.labels.push_back(std::move(label));
            return true;
        }
        return false;
    }

    // A whole file's text into a pack.
    inline Pack ReadPack(std::string_view text, std::string file, const Fold& fold) {
        Pack pack;
        pack.file = detail::AsciiLower(std::move(file));
        std::size_t at = 0;
        while (at < text.size()) {
            const auto nl = text.find('\n', at);
            const auto line = text.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at);
            if (!AddLine(pack, line, fold)) ++pack.bad;
            if (nl == std::string_view::npos) break;
            at = nl + 1;
        }
        // Longer forms first, so "tomorrow morning" in one word of a language is tried before its "tomorrow"; and
        // the more particular label before the more general.
        for (auto* list : {&pack.time, &pack.place, &pack.none}) {
            std::stable_sort(list->begin(), list->end(), [](const Word& a, const Word& b) { return a.letters > b.letters; });
        }
        std::stable_sort(pack.labels.begin(), pack.labels.end(), [](const Label& a, const Label& b) { return a.weight > b.weight; });
        return pack;
    }

    // ---- words of other languages into the English the readers know ----------------------------------------------

    namespace detail {
        // Is there a word boundary in `text` before byte `at` / at byte `at`? (The end of the text, or a separator
        // of any script.)
        inline bool BoundaryBefore(std::string_view text, std::size_t at) {
            if (at == 0) return true;
            std::size_t b = at - 1;
            while (b > 0 && (static_cast<unsigned char>(text[b]) & 0xC0) == 0x80) --b;   // back to the letter's first byte
            char32_t c = 0;
            NameForms::Decode(text, b, c);
            return NameForms::Separator(c);
        }
        inline bool BoundaryAt(std::string_view text, std::size_t at) {
            if (at >= text.size()) return true;
            char32_t c = 0;
            NameForms::Decode(text, at, c);
            return NameForms::Separator(c);
        }
    }

    // Every form found in `folded` (lower case) is replaced by its English, with spaces round it. A form of a
    // spaced script counts as a whole word only ("in" is not found inside "morning"); a form of an unspaced script
    // counts wherever it stands. The lists are tried in the order given, each with its longer forms first; what was
    // replaced is not looked at again. Text no list knows stays as it is.
    struct Read {
        std::string english;   // the text with every known form in English, one space between words
        std::string left;      // what no list knew, one space between its words
        std::string keys;      // the English of the known forms alone, in their order
        std::vector<const Word*> hits;   // the known forms themselves, in their order
    };
    inline Read Reading(std::string_view folded, const std::vector<const std::vector<Word>*>& lists) {
        std::string out, left, keys;
        std::vector<const Word*> hits;
        out.reserve(folded.size() + 16);
        std::size_t i = 0;
        while (i < folded.size()) {
            const Word* hit = nullptr;
            for (const auto* list : lists) {
                if (!list) continue;
                for (const auto& w : *list) {
                    if (w.form.size() > folded.size() - i || folded.compare(i, w.form.size(), w.form) != 0) continue;
                    if (!w.unspaced && !(detail::BoundaryBefore(folded, i) && detail::BoundaryAt(folded, i + w.form.size()))) continue;
                    if (!hit || w.letters > hit->letters) hit = &w;
                }
            }
            if (hit) {
                out += ' ';
                out += hit->key;
                out += ' ';
                left += ' ';
                if (!keys.empty()) keys += ' ';
                keys += hit->key;
                hits.push_back(hit);
                i += hit->form.size();
                continue;
            }
            char32_t c = 0;
            const auto n = NameForms::Decode(folded, i, c);
            out.append(folded.substr(i, n));
            if (NameForms::Separator(c)) left += ' ';
            else left.append(folded.substr(i, n));
            i += n;
        }
        // one space between words, none at the ends
        const auto tidy = [](const std::string& raw) {
            std::string t;
            t.reserve(raw.size());
            bool space = false;
            for (const char c : raw) {
                if (c == ' ' || c == '\t') {
                    space = !t.empty();
                    continue;
                }
                if (space) t += ' ';
                space = false;
                t += c;
            }
            return t;
        };
        return {tidy(out), tidy(left), keys, std::move(hits)};
    }
    inline std::string ToEnglish(std::string_view folded, const std::vector<const std::vector<Word>*>& lists) {
        return Reading(folded, lists).english;
    }

    // A word of place, when the WHOLE of what was written is one: "domoi" (home) yes, a house's name that happens
    // to hold the word for house no. What the lists do not know must be little words a pack lets go (`free`), or
    // nothing. Empty when it is no word of place: then it is a name, and stays as it was written.
    inline std::string PlaceToEnglish(std::string_view folded, const std::vector<const std::vector<Word>*>& lists,
                                      const NameForms::FreeWord& free = {}) {
        const Read r = Reading(folded, lists);
        if (r.keys.empty()) return {};
        if (!r.left.empty()) {
            const NameForms::Words rest = NameForms::Read(r.left);
            for (std::size_t i = 0; i < rest.at.size(); ++i) {
                if (!(free && free(rest.Word(i)))) return {};
            }
        }
        return r.keys;
    }

    // Times: after the words, the order. Many languages put the half of the day before the hour ("gogo 6-ji",
    // "xiawu 6 dian": pm 6 o'clock) and a count before its "in" ("2 jikan go": 2 hours later); the time reader
    // wants "6 pm". "am"/"pm" standing right before a number changes places with it.
    //
    // And the half of the day right beside an hour, on either side, is that hour's am or pm ("gogo 6-ji", "wanshang
    // 8 dian", "3 pasdite": afternoon 6, evening 8, 3 afternoon), where English says "6 in the evening": morning
    // and dawn are am, noon, afternoon and evening pm, night pm from five on and am before ("2 at night").
    inline std::string TimeToEnglish(std::string_view folded, const std::vector<const std::vector<Word>*>& lists) {
        const std::string s = ToEnglish(folded, lists);
        std::vector<std::string> words;
        std::size_t at = 0;
        while (at < s.size()) {
            const auto sp = s.find(' ', at);
            words.push_back(s.substr(at, sp == std::string::npos ? std::string::npos : sp - at));
            if (sp == std::string::npos) break;
            at = sp + 1;
        }
        static constexpr const char* kNumbers[] = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve"};
        // the hour a word names, or -1: digits up to 24 (with or without ":mm"), or a number word
        const auto hourOf = [](const std::string& w) {
            if (w.empty()) return -1;
            if (w[0] >= '0' && w[0] <= '9') {
                int h = 0;
                std::size_t i = 0;
                while (i < w.size() && w[i] >= '0' && w[i] <= '9' && i < 2) h = h * 10 + (w[i++] - '0');
                if (i < w.size() && w[i] != ':') return -1;
                return h <= 24 ? h : -1;
            }
            for (int n = 0; n < 12; ++n) {
                if (w == kNumbers[n]) return n + 1;
            }
            return -1;
        };
        const auto half = [](const std::string& w, int hour) -> const char* {
            if (w == "am" || w == "pm") return w == "am" ? "am" : "pm";
            if (w == "morning" || w == "dawn") return "am";
            if (w == "noon" || w == "afternoon" || w == "evening") return "pm";
            if (w == "night") return hour >= 5 && hour <= 12 ? "pm" : "am";
            return nullptr;
        };
        for (std::size_t k = 0; k + 1 < words.size(); ++k) {
            // the half of the day, then the hour: they change places
            const int after = hourOf(words[k + 1]);
            if (after >= 0 && after <= 12) {
                if (const char* h = half(words[k], after)) {
                    words[k] = words[k + 1];
                    words[k + 1] = h;
                    ++k;
                    continue;
                }
            }
            // the hour, then the half of the day
            const int before = hourOf(words[k]);
            if (before >= 0 && before <= 12) {
                if (const char* h = half(words[k + 1], before)) {
                    words[k + 1] = h;
                    ++k;
                    continue;
                }
                // "6 o'clock evening", "6 hours pm": the hour's own word stands between them
                if (k + 2 < words.size() && (words[k + 1] == "o'clock" || words[k + 1] == "hours" || words[k + 1] == "hour")) {
                    if (const char* h2 = half(words[k + 2], before)) {
                        words[k + 1] = h2;
                        words[k + 2].clear();
                        k += 2;
                    }
                }
            }
        }
        std::string out;
        for (const auto& w : words) {
            if (w.empty()) continue;
            if (!out.empty()) out += ' ';
            out += w;
        }
        return out;
    }

    // Is this the word for an empty field: nothing at all, "none", or what a pack in force says for it? Only as the
    // whole of what was written ("no one" yes, "no one but Lydia" no).
    inline bool IsNone(std::string_view folded, const std::vector<const std::vector<Word>*>& lists) {
        const std::string text = detail::Trim(folded);
        if (text.empty() || text == "none" || text == "null" || text == "n/a" || text == "-") return true;
        const std::string english = ToEnglish(text, lists);
        return english == "none";
    }

    // ---- what the player reads ------------------------------------------------------------------------------------

    // The message as this pack words it, when one of its labels is this message's pattern. The holes take whatever
    // stands between the fixed pieces (the shortest that fits), so names come through untouched.
    inline std::optional<std::string> Translate(const std::vector<Label>& labels, std::string_view message) {
        for (const auto& label : labels) {
            const auto& fixed = label.fixed;
            if (message.size() < label.weight) continue;
            if (message.compare(0, fixed.front().size(), fixed.front()) != 0) continue;
            if (label.hole.empty()) {
                if (message.size() != fixed.front().size()) continue;
                return label.to;
            }
            std::vector<std::string_view> caught;
            std::size_t at = fixed.front().size();
            bool fits = true;
            for (std::size_t h = 0; h < label.hole.size() && fits; ++h) {
                const std::string& next = fixed[h + 1];
                const bool last = h + 1 == label.hole.size();
                std::size_t end = 0;
                if (last) {
                    // the last piece must end the message
                    if (message.size() < at + next.size() || message.compare(message.size() - next.size(), next.size(), next) != 0) {
                        fits = false;
                        break;
                    }
                    end = message.size() - next.size();
                } else {
                    end = message.find(next, at + 1);
                    if (end == std::string_view::npos) {
                        fits = false;
                        break;
                    }
                }
                if (end <= at) {   // a hole holds at least one letter
                    fits = false;
                    break;
                }
                caught.push_back(message.substr(at, end - at));
                at = end + next.size();
            }
            if (!fits) continue;
            std::string out;
            for (std::size_t i = 0; i < label.to.size(); ++i) {
                if (label.to[i] == '{' && i + 2 < label.to.size() && label.to[i + 1] >= '1' && label.to[i + 1] <= '9' && label.to[i + 2] == '}') {
                    const int number = label.to[i + 1] - '0';
                    for (std::size_t h = 0; h < label.hole.size(); ++h) {
                        if (label.hole[h] == number) {
                            out.append(caught[h]);
                            break;
                        }
                    }
                    i += 2;
                    continue;
                }
                out.push_back(label.to[i]);
            }
            return out;
        }
        return std::nullopt;
    }

    // ---- which language is this game in ---------------------------------------------------------------------------

    // The script most of the game's own names are written in. The game's language setting is not enough: Korean and
    // other languages the game never shipped are played as fan translations with the setting left at English.
    struct ScriptCount {
        std::size_t latin = 0, cyrillic = 0, hangul = 0, kana = 0, han = 0;

        void Add(std::string_view name) {
            char32_t c = 0;
            for (std::size_t i = 0; i < name.size();) {
                i += NameForms::Decode(name, i, c);
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0x24F)) ++latin;
                else if (c >= 0x400 && c <= 0x52F) ++cyrillic;
                else if ((c >= 0xAC00 && c <= 0xD7AF) || (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F)) ++hangul;
                else if ((c >= 0x3040 && c <= 0x30FF && c != 0x30FB) || (c >= 0xFF66 && c <= 0xFF9F)) ++kana;
                else if ((c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF)) ++han;
            }
        }
        // Japanese is told by its kana, however much of a name is written in Han; then whatever is a fifth of the
        // letters or more, the least Latin of them first.
        const char* Script() const {
            const std::size_t all = latin + cyrillic + hangul + kana + han;
            if (all == 0) return "latin";
            if (kana * 20 >= all) return "kana";
            if (hangul * 5 >= all) return "hangul";
            if (han * 5 >= all) return "han";
            if (cyrillic * 5 >= all) return "cyrillic";
            return "latin";
        }
    };

    // The pack of what the player reads; none means English. `setting`: "auto", or a pack by file name, code or
    // English name ("english" and "en" choose English). `gameWord`: Skyrim.ini's sLanguage. `namesScript`: what the
    // game's own names are written in.
    inline const Pack* TextPack(const std::vector<Pack>& packs, std::string_view setting, std::string_view gameWord, std::string_view namesScript) {
        const std::string want = detail::AsciiLower(detail::Trim(setting));
        if (!want.empty() && want != "auto") {
            if (want == "english" || want == "en") return nullptr;
            for (const auto& p : packs) {
                if (p.Is(want)) return &p;
            }
            return nullptr;
        }
        const std::string game = detail::AsciiLower(detail::Trim(gameWord));
        if (!game.empty() && game != "english") {
            for (const auto& p : packs) {
                if (!p.game.empty() && p.game == game) return &p;
            }
        }
        // The setting says English (or something no pack knows): then the names tell. Latin letters are taken for
        // the language the setting names; anything else is a translation the setting does not know of.
        if (namesScript.empty() || namesScript == "latin") return nullptr;
        const Pack* found = nullptr;
        for (const auto& p : packs) {
            if (p.script != namesScript) continue;
            if (!found || (found->game.empty() && !p.game.empty())) found = &p;   // one the game ships before one it does not
        }
        return found;
    }

    // The packs of the languages people speak: by SkyrimNet's codes ("aln", "ru") or the names in the setting.
    inline std::vector<const Pack*> SpokenPacks(const std::vector<Pack>& packs, const std::vector<std::string>& codes) {
        std::vector<const Pack*> out;
        for (const auto& raw : codes) {
            const std::string code = detail::AsciiLower(detail::Trim(raw));
            if (code.empty() || code == "auto" || code == "game" || code == "english" || code == "en") continue;
            for (const auto& p : packs) {
                if (p.Is(code) && std::find(out.begin(), out.end(), &p) == out.end()) out.push_back(&p);
            }
        }
        return out;
    }

    // The spoken-language codes in SkyrimNet's UniversalTranslator.yaml (its text): every "spokenLanguage:" and the
    // player's "language:" under a rule, when the translator is switched on. Read as lines, not as YAML: two keys
    // of a file that is not ours.
    inline std::vector<std::string> SpokenCodesOfTranslator(std::string_view yaml) {
        std::vector<std::string> out;
        bool enabled = false;
        std::size_t at = 0;
        while (at < yaml.size()) {
            const auto nl = yaml.find('\n', at);
            const std::string line = detail::Trim(yaml.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at));
            at = nl == std::string_view::npos ? yaml.size() : nl + 1;
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            const std::string key = detail::Trim(line.substr(0, colon));
            std::string value = detail::Trim(line.substr(colon + 1));
            if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'')) value = value.substr(1, value.size() - 2);
            if (key == "enabled") enabled = detail::AsciiLower(value) == "true";
            if ((key == "spokenLanguage" || key == "language") && !value.empty() && value != "~" && value != "null") {
                const std::string code = detail::AsciiLower(value);
                if (code != "game" && code != "auto" && std::find(out.begin(), out.end(), code) == out.end()) out.push_back(code);
            }
        }
        if (!enabled) out.clear();
        return out;
    }
}
