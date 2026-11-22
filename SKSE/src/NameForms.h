#pragma once

// A name as people say it. The dialogue model fills an action's "who" or "where" in the language it speaks, and
// most languages do not leave a name alone: Russian declines it ("k Lidii" for Lidiya, "v Vaitrane" for Vaitran,
// written in Cyrillic), German puts an article and a case ending on it ("zur Beflaggten Maehre" for "Die Beflaggte
// Maehre"), Albanian hangs the article behind ("Whiterunit", "Lydian", "Camillen"), Korean and Japanese hang a
// particle behind without a space ("Hwaiteureon-euro", "Howaitoran-e" in their own scripts) and write the words of a
// name with a dot or with nothing between them. Looked up as written, every one of these was "not found", or found
// the wrong person by a slip of the pen counted in bytes (a Korean name of three letters is nine bytes long).
// (This header is kept ASCII; the real letters are in NameFormsCheck.cpp.)
//
// By structure alone, the same for every language: words are told apart by what separates them in any script; two
// words are the same name-word when one begins with the other and only a short ending differs; little words that
// are no part of the name (an article, a preposition) are let go. Nothing here knows a single word of any language;
// a language pack may name further words to let go (LanguagePack.h). Pure logic, checked by NameFormsCheck.
//
// Everything takes text already folded to lower case (StringUtils::ToLowerStd / FoldUtf8).

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace IntelEngine::NameForms {

    // ---- reading UTF-8. Leniently: a byte that begins no valid letter is a letter of its own (in the private-use
    // block), so text that came in an old code page still compares letter for letter with itself.
    inline std::size_t Decode(std::string_view s, std::size_t at, char32_t& out) {
        const auto b0 = static_cast<unsigned char>(s[at]);
        if (b0 < 0x80) {
            out = b0;
            return 1;
        }
        const int more = (b0 & 0xE0) == 0xC0 ? 1 : (b0 & 0xF0) == 0xE0 ? 2 : (b0 & 0xF8) == 0xF0 ? 3 : 0;
        if (more == 0 || at + static_cast<std::size_t>(more) >= s.size()) {
            out = 0xE000u + b0;
            return 1;
        }
        char32_t cp = b0 & (more == 1 ? 0x1Fu : more == 2 ? 0x0Fu : 0x07u);
        for (int k = 1; k <= more; ++k) {
            const auto b = static_cast<unsigned char>(s[at + static_cast<std::size_t>(k)]);
            if ((b & 0xC0) != 0x80) {
                out = 0xE000u + b0;
                return 1;
            }
            cp = (cp << 6) | (b & 0x3Fu);
        }
        out = cp;
        return static_cast<std::size_t>(more) + 1;
    }

    // How many letters a text has (not bytes: Lidiya in Cyrillic has five, Lydia in Hangul three).
    inline std::size_t Letters(std::string_view s) {
        std::size_t n = 0;
        char32_t c = 0;
        for (std::size_t i = 0; i < s.size(); i += Decode(s, i, c)) ++n;
        return n;
    }

    // Written without spaces between words: kana, Han, Hangul. (U+30FB, the dot between the words of a foreign
    // name in Japanese, is a separator; U+30FC, the long vowel mark, is a letter.)
    inline bool Unspaced(char32_t c) {
        return (c >= 0x3040 && c <= 0x30FF && c != 0x30FB) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) ||
               (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F) ||
               (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF66 && c <= 0xFF9F);
    }

    // What stands between two words, in any script. In ASCII everything but letters, digits and the apostrophe of
    // a name ("Edan's"); beyond it the spaces, dashes, quotes and stops of general, CJK and full-width punctuation.
    // U+2019, the typographic apostrophe, is part of a name like the plain one.
    inline bool Separator(char32_t c) {
        if (c < 0x80) return !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'');
        return c == 0x00A0 || c == 0x00AB || c == 0x00B7 || c == 0x00BB || (c >= 0x2000 && c <= 0x206F && c != 0x2019) ||
               (c >= 0x3000 && c <= 0x303F) || c == 0x30FB || (c >= 0xFF01 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) ||
               (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65);
    }
    inline bool Apostrophe(char32_t c) { return c == '\'' || c == 0x2019; }

    // A text read into its words: every letter in a row with the separators left out, and where each word lies.
    struct Words {
        std::u32string text;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> at;   // begin, length (in letters)

        std::u32string_view Word(std::size_t i) const { return std::u32string_view(text).substr(at[i].first, at[i].second); }
        void Clear() {
            text.clear();
            at.clear();
        }
    };

    // Into `w`, which keeps its memory from one name to the next (a lookup reads thousands of names).
    inline void ReadInto(Words& w, std::string_view folded) {
        w.Clear();
        std::uint32_t begun = 0;
        bool in = false;
        char32_t c = 0;
        for (std::size_t i = 0; i < folded.size();) {
            i += Decode(folded, i, c);
            if (Separator(c)) {
                if (in) w.at.push_back({begun, static_cast<std::uint32_t>(w.text.size()) - begun});
                in = false;
                continue;
            }
            if (!in) {
                begun = static_cast<std::uint32_t>(w.text.size());
                in = true;
            }
            w.text.push_back(c);
        }
        if (in) w.at.push_back({begun, static_cast<std::uint32_t>(w.text.size()) - begun});
    }
    inline Words Read(std::string_view folded) {
        Words w;
        ReadInto(w, folded);
        return w;
    }

    // Is `said` the word `name` with another ending? The cost (letters that differ at the end; 0 = the same word),
    // or -1.
    //  - Scripts without spaces do not change the word itself: the whole name-word, then at most three letters of a
    //    particle (Whiterun + "euro", Lydia + "ni"). A name-word of one letter stands only for itself.
    //  - Alphabets: the two begin alike for at least three letters; of the name-word at most its last letter may be
    //    replaced (Lidiya: Lidii, Kamilla: Kamille, Lydia: Lydies; its last two in a long word, Vetrenyi:
    //    Vetrenogo), and what was said may go on for at most three letters behind the common part (Whiterun:
    //    Whiterunit, Vaitran: Vaitranom), two behind a name-word of three ("Jon": "Jonit", never "Jonas").
    //  - Never across an apostrophe: "Edan" is not "Edan's" (the creature "Edan's Shadow" answered for people).
    inline int SameWord(std::u32string_view name, std::u32string_view said) {
        if (name.empty() || said.empty()) return -1;
        if (name == said) return 0;
        const std::size_t ln = name.size(), ls = said.size();
        std::size_t p = 0;
        while (p < ln && p < ls && name[p] == said[p]) ++p;
        const std::size_t nameTail = ln - p, saidTail = ls - p;
        if (Unspaced(name.front())) {
            return (ln >= 2 && nameTail == 0 && saidTail <= 3) ? static_cast<int>(saidTail) : -1;
        }
        if (ln < 3 || p < 3) return -1;
        const std::size_t mayLose = ln >= 7 ? 2 : ln >= 4 ? 1 : 0;
        const std::size_t mayAdd = ln >= 4 ? 3 : 2;
        if (nameTail > mayLose || saidTail > mayAdd) return -1;
        for (std::size_t i = p; i < ln; ++i) {
            if (Apostrophe(name[i])) return -1;
        }
        for (std::size_t i = p; i < ls; ++i) {
            if (Apostrophe(said[i])) return -1;
        }
        return static_cast<int>(nameTail + saidTail);
    }

    // A word that is let go when it matches nothing: an article or a preposition. By length (up to three letters in
    // an alphabet: "the", "die", "zur", "k", "ne"; nothing in the scripts without spaces, where a short word is a
    // word), or because a language pack names it ("towards", "nach", "beim").
    using FreeWord = std::function<bool(std::u32string_view)>;
    inline bool Small(std::u32string_view word, const FreeWord& free) {
        if (word.empty()) return true;
        if (free && free(word)) return true;
        return !Unspaced(word.front()) && word.size() <= 3;
    }

    // How well what was said stands for a name.
    enum Stand : int {
        No = 0,
        PartForms = 1,   // part of the name (a first name), with other endings: "k Ionu" for Jon Battle-Born
        Forms = 3,       // the whole name with other endings, articles or particles
        Same = 4,        // the same letters, written with other separators ("Battle Born", a name without its dot)
    };
    struct Verdict {
        int stand = No;
        int cost = 0;   // letters that differ, all words together: the lower the nearer
    };

    inline Verdict Stands(const Words& name, const Words& said, const FreeWord& free = {}) {
        Verdict v;
        if (name.text.empty() || said.text.empty()) return v;
        if (name.text == said.text) {
            v.stand = Same;
            return v;
        }
        // Without spaces there may be no words to go by: the whole name, then a particle.
        if (Unspaced(name.text.front()) && name.text.size() >= 2 && said.text.size() > name.text.size() &&
            said.text.size() - name.text.size() <= 3 && said.text.compare(0, name.text.size(), name.text) == 0) {
            v.stand = Forms;
            v.cost = static_cast<int>(said.text.size() - name.text.size());
            return v;
        }
        // Word by word. Each word said goes with the name-word it fits best; a name-word is taken once.
        constexpr std::size_t kMost = 12;
        if (name.at.size() > kMost || said.at.size() > kMost) return v;
        bool taken[kMost] = {};
        int cost = 0;
        std::size_t matched = 0;
        bool matchedReal = false;   // one of them more than a little word
        for (std::size_t i = 0; i < said.at.size(); ++i) {
            const auto word = said.Word(i);
            int best = -1;
            std::size_t bestAt = 0;
            for (std::size_t j = 0; j < name.at.size(); ++j) {
                if (taken[j]) continue;
                const int c = SameWord(name.Word(j), word);
                if (c >= 0 && (best < 0 || c < best)) {
                    best = c;
                    bestAt = j;
                }
            }
            if (best >= 0) {
                taken[bestAt] = true;
                cost += best;
                ++matched;
                // "Jon" is a name of three letters; "the" is not a name, and only a language pack can tell.
                if (!(free && free(word)) && (Unspaced(word.front()) ? word.size() >= 2 : word.size() >= 3)) matchedReal = true;
            } else if (!Small(word, free)) {
                return v;   // a real word that is no part of this name: another name
            }
        }
        if (matched == 0 || !matchedReal) return v;
        bool nameLeft = false;
        for (std::size_t j = 0; j < name.at.size(); ++j) {
            if (!taken[j] && !Small(name.Word(j), free)) nameLeft = true;
        }
        v.stand = nameLeft ? PartForms : Forms;
        v.cost = cost;
        return v;
    }

    // The best among many names (each folded). `tied` when two different names stand equally well: then it is
    // nobody's, and the caller goes on to its other ways (the nearest, the one in the room).
    struct Best {
        int index = -1;
        int stand = No;
        int cost = 0;
        bool tied = false;
        explicit operator bool() const { return index >= 0 && !tied; }
    };
    template <class Names>
    Best Find(std::string_view saidFolded, const Names& names, int atLeast = Forms, const FreeWord& free = {}) {
        Best best;
        const Words said = Read(saidFolded);
        if (said.text.empty()) return best;
        Words name;
        int i = -1;
        for (const auto& candidate : names) {
            ++i;
            ReadInto(name, candidate);
            const Verdict v = Stands(name, said, free);
            if (v.stand < atLeast) continue;
            if (best.index < 0 || v.stand > best.stand || (v.stand == best.stand && v.cost < best.cost)) {
                best.index = i;
                best.stand = v.stand;
                best.cost = v.cost;
                best.tied = false;
            } else if (v.stand == best.stand && v.cost == best.cost) {
                best.tied = true;
            }
        }
        return best;
    }

    // Does a sentence speak of this name, in whatever form its language puts it ("Lidiyu videli u vorot" speaks of
    // Lidiya; "Svens Laute" of Sven; "Victoria" does not speak of Ria)? Every real word of the name is found among
    // the words of the sentence; a name of little words only is looked for as it is written.
    inline bool Mentions(std::string_view sentenceFolded, std::string_view nameFolded, const FreeWord& free = {}) {
        const Words name = Read(nameFolded);
        const Words text = Read(sentenceFolded);
        if (name.text.empty() || text.text.empty()) return false;
        if (Unspaced(name.text.front())) return text.text.find(name.text) != std::u32string::npos;
        // In a sentence a possessive speaks of its owner ("Sinmir's views" is about Sinmir): the apostrophe and what
        // follows it are let go. (In a lookup "Edan" is not "Edan's Shadow": that is another thing's name.)
        const auto bare = [](std::u32string_view word) {
            for (std::size_t i = 1; i < word.size(); ++i) {
                if (Apostrophe(word[i]) && word.size() - i <= 2) return word.substr(0, i);
            }
            return word;
        };
        std::size_t real = 0;
        for (std::size_t j = 0; j < name.at.size(); ++j) {
            const auto word = name.Word(j);
            const bool little = Small(word, free);
            if (little && name.at.size() > 1) continue;
            ++real;
            bool found = false;
            for (std::size_t i = 0; i < text.at.size() && !found; ++i) {
                const auto said = text.Word(i);
                found = little ? (said == word || bare(said) == word) : (SameWord(word, said) >= 0 || SameWord(word, bare(said)) >= 0);
            }
            if (!found) return false;
        }
        return real > 0;
    }

    // A slip of the pen is measured by how long the name is, in letters: none in a name of up to four letters, one
    // up to seven, two up to eleven, then three; never more than the setting allows. (It was measured in bytes: a
    // Cyrillic name of four letters got the allowance of eight, a Korean one of three the allowance of nine.)
    inline int Slips(std::size_t letters, int most) {
        const int byLength = letters <= 4 ? 0 : letters <= 7 ? 1 : letters <= 11 ? 2 : 3;
        return std::min(most, byLength);
    }
    inline int Slips(std::string_view a, std::string_view b, int most) { return Slips(std::min(Letters(a), Letters(b)), most); }
}
