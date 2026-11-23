#pragma once

// The plain letter of a letter: how two spellings of one name are told to be the same name.
//
// The game writes "Weisslauf" with a sharp s, a model that speaks Swiss German writes it with two s; a name comes
// from the game with an accent on its capital and from the model without ("Eglise" for the church with an acute E);
// Polish "Biala Gran" is typed without its stroke and its acute; Russian drops the two dots of yo and a model that
// writes for a voice puts stress marks on vowels; one side writes an apostrophe straight and the other curly; one
// writes a letter and its mark as one sign and the other as two. Compared letter for letter, every one of these was
// another name. (This header is kept ASCII; the real letters are in the checks.)
//
// One rule for every language, with no word of any of them: a letter is compared as its plain letter. Lower case;
// marks taken off (umlaut, acute, cedilla, stroke, breve: "a" for a-umlaut, "l" for l-stroke, Cyrillic "e" for yo and
// "i" for short i); the letters that are two in plain writing written as two (sharp s: ss, ae and oe ligatures, thorn:
// th); a mark standing by itself after its letter left out; every apostrophe the plain one; full-width Latin letters
// and digits the plain ones. Latin, Greek, Cyrillic and Armenian: the scripts that have case or marks. Kana, Han and
// Hangul are compared as they are.
//
// What it does not do: the German habit of writing "ue" for u-umlaut (a spelling, not a mark: "Mueller" stays another
// word than "Muller"; the lookups' allowance for a slip of the pen covers one such letter).
//
// The tables come from Unicode's own data (LetterFoldTable.h, generated). Pure, std only; used by NameForms.h and
// ItemNames.h and checked by GameTextCheck.

#include "LetterFoldTable.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace IntelEngine::LetterFold {

    // The plain letters of `c` into `out` (room for two); how many: 0 (left out), 1 or 2.
    inline int Fold(char32_t c, char32_t* out) {
        namespace T = LetterFoldTable;
        if (c < 0x80) {
            out[0] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
            return 1;
        }
        if (c < T::kFirstA) {
            if (c == 0xAD) return 0;                      // a soft hyphen: where a word may break, not a letter
            out[0] = c == 0xB4 ? U'\'' : c;               // an acute accent typed for an apostrophe
            return 1;
        }
        std::uint16_t first = 0;
        if (c < T::kEndA) {
            first = T::kA[c - T::kFirstA];
        } else if (c >= T::kFirstB && c < T::kEndB) {
            first = T::kB[c - T::kFirstB];
        } else {
            if (c >= 0x0531 && c <= 0x0556) {             // Armenian capitals
                out[0] = c + 0x30;
            } else if (c == 0x2019 || c == 0x2032 || c == 0xFF07) {
                out[0] = U'\'';                           // the curly apostrophe, a prime, the full-width one
            } else if (c == 0x200B || c == 0x200C || c == 0x200D || c == 0x2060 || c == 0xFEFF) {
                return 0;                                 // signs of no width
            } else if (c >= 0xFF21 && c <= 0xFF3A) {
                out[0] = c - 0xFF21 + U'a';               // full-width capitals
            } else if (c >= 0xFF41 && c <= 0xFF5A) {
                out[0] = c - 0xFF41 + U'a';
            } else if (c >= 0xFF10 && c <= 0xFF19) {
                out[0] = c - 0xFF10 + U'0';
            } else if (c == 0x212A) {
                out[0] = U'k';                            // the Kelvin sign
            } else if (c == 0x212B) {
                out[0] = U'a';                            // the Angstrom sign
            } else {
                out[0] = c;
            }
            return 1;
        }
        if (first == 0) return 0;
        out[0] = first;
        // The few that become two letters: a short sorted list.
        std::size_t lo = 0, hi = sizeof(T::kMore) / sizeof(T::kMore[0]);
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            if (T::kMore[mid].letter < c) lo = mid + 1;
            else hi = mid;
        }
        if (lo < sizeof(T::kMore) / sizeof(T::kMore[0]) && T::kMore[lo].letter == c) {
            out[1] = T::kMore[lo].second;
            return 2;
        }
        return 1;
    }

    // A whole text in plain letters, as UTF-8: for the keys of a table and for comparing byte by byte. Everything
    // that is no letter (spaces, signs) stays as it is; a byte that is no UTF-8 stays as it is.
    inline std::string Plain(std::string_view text) {
        std::string out;
        out.reserve(text.size());
        const auto put = [&out](char32_t cp) {
            if (cp < 0x80) {
                out += static_cast<char>(cp);
            } else if (cp < 0x800) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        };
        for (std::size_t i = 0; i < text.size();) {
            const auto b0 = static_cast<unsigned char>(text[i]);
            if (b0 < 0x80) {
                out += static_cast<char>((b0 >= 'A' && b0 <= 'Z') ? b0 + 32 : b0);
                ++i;
                continue;
            }
            const int more = (b0 & 0xE0) == 0xC0 ? 1 : (b0 & 0xF0) == 0xE0 ? 2 : (b0 & 0xF8) == 0xF0 ? 3 : 0;
            bool good = more > 0 && i + static_cast<std::size_t>(more) < text.size();
            char32_t cp = b0 & (more == 1 ? 0x1Fu : more == 2 ? 0x0Fu : 0x07u);
            for (int k = 1; good && k <= more; ++k) {
                const auto b = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
                if ((b & 0xC0) != 0x80) good = false;
                cp = (cp << 6) | (b & 0x3Fu);
            }
            if (!good) {
                out += text[i++];
                continue;
            }
            char32_t plain[2];
            const int n = Fold(cp, plain);
            for (int k = 0; k < n; ++k) put(plain[k]);
            i += static_cast<std::size_t>(more) + 1;
        }
        return out;
    }
}
