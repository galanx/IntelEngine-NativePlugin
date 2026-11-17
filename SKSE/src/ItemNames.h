#pragma once

// IntelEngine 4.0: item names as people say them, across modlists and languages. Pure logic without game types,
// checked offline by SKSE/tests/ItemNamesCheck.cpp.
//
// A dialogue model names an item the way it is usually called ("Nord Mead", "Iron Ingot", "Sweetroll"), while the
// modlist may tag or reorder it: sorting mods write "[Drink] Nord Mead", "Nord Mead (Food)", "Drink - Ale",
// "Ingot - Iron" or, in Chinese lists, a tag in full-width brackets. Two names are the same item when their words agree in any order with
// such tags left out; a vaguer word ("Mead") may stand for an item whose name ends in it as whole words ("Nord Mead"),
// never for part of a word, so "Ale" can never become "Vase" or "Pale Ale".
//
// Nothing here knows a word of any language: names are read as UTF-8, a letter of any alphabet is a letter (upper and
// lower case are the same for Latin, Greek and Cyrillic), and punctuation and symbols (stars, guillemets, CJK brackets) separate words.
// Text that is not valid UTF-8 is compared byte for byte.

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace IntelEngine::ItemNames {

    namespace detail {
        // One character of UTF-8 text: its code point and its bytes. An invalid byte is a character of its own.
        struct Char {
            char32_t cp = 0;
            std::string_view bytes;
        };

        inline std::vector<Char> Chars(std::string_view text) {
            std::vector<Char> out;
            for (std::size_t i = 0; i < text.size();) {
                const auto b = static_cast<unsigned char>(text[i]);
                std::size_t n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
                char32_t cp = n == 1 ? b : n == 2 ? (b & 0x1F) : n == 3 ? (b & 0x0F) : n == 4 ? (b & 0x07) : 0;
                bool ok = n > 0 && i + n <= text.size();
                for (std::size_t k = 1; ok && k < n; ++k) {
                    const auto c = static_cast<unsigned char>(text[i + k]);
                    if ((c >> 6) != 0x2) ok = false;
                    cp = (cp << 6) | (c & 0x3F);
                }
                if (!ok) {
                    n = 1;
                    cp = 0xFFFD0000u | b;  // stands for itself: not a code point, never equal to a real one
                }
                out.push_back({cp, text.substr(i, n)});
                i += n;
            }
            return out;
        }

        inline void Put(std::string& out, char32_t cp) {
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
        }

        // Upper case to lower for the scripts that have it in pairs here: Latin, Greek, Cyrillic.
        inline char32_t Lower(char32_t cp) {
            if (cp >= 'A' && cp <= 'Z') return cp + 32;
            if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32;              // Latin-1 capitals
            if (cp >= 0x100 && cp <= 0x17F && cp != 0x130 && cp != 0x138 && cp != 0x149 && cp != 0x178) {
                const bool oddPairs = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
                return oddPairs ? (cp % 2 == 1 ? cp + 1 : cp) : (cp % 2 == 0 ? cp + 1 : cp);   // Latin Extended-A
            }
            if (cp == 0x178) return 0xFF;                                                // Y with diaeresis
            if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) return cp + 32;              // Greek
            if (cp >= 0x410 && cp <= 0x42F) return cp + 32;                              // Cyrillic capitals
            if (cp >= 0x400 && cp <= 0x40F) return cp + 80;                              // Cyrillic capitals with marks (Io)
            return cp;
        }

        // Joins a word without being part of it: the apostrophes, straight or curly (Hunter's).
        inline bool Joiner(char32_t cp) { return cp == '\'' || cp == 0x2019 || cp == 0x2018 || cp == 0x02BC; }

        // A letter or a digit of any alphabet; everything else separates words.
        inline bool Letter(char32_t cp) {
            if (cp >= 0xFFFD0000u) return true;                     // a byte that is not UTF-8: compared as it is
            if (cp < 0x80) return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9');
            if (cp <= 0xBF || cp == 0xD7 || cp == 0xF7) return false;  // Latin-1 punctuation and signs (guillemets, inverted marks, times, divide)
            if (cp >= 0x2000 && cp <= 0x2BFF) return false;         // punctuation, arrows, symbols, dingbats (stars, ticks)
            if (cp >= 0x3000 && cp <= 0x303F) return false;         // CJK punctuation and brackets
            if (cp >= 0xFE30 && cp <= 0xFE4F) return false;         // CJK compatibility forms
            if ((cp >= 0xFF00 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20) || (cp >= 0xFF3B && cp <= 0xFF40) ||
                (cp >= 0xFF5B && cp <= 0xFF65)) {
                return false;                                        // full-width punctuation, brackets and colon
            }
            if (cp >= 0x1F000) return false;                         // emoji and other pictographs
            return true;
        }

        inline bool Opens(char32_t cp) {
            return cp == '(' || cp == '[' || cp == '{' || cp == '<' || cp == 0x3010 || cp == 0x300C || cp == 0x300E ||
                   cp == 0xFF08 || cp == 0xFF3B;
        }
        inline bool Closes(char32_t cp) {
            return cp == ')' || cp == ']' || cp == '}' || cp == '>' || cp == 0x3011 || cp == 0x300D || cp == 0x300F ||
                   cp == 0xFF09 || cp == 0xFF3D;
        }

        // Tags in brackets left out: "(Food) Apple", "[Drink] Ale", "Nord Mead {Drink}", CJK and full-width brackets.
        inline std::string StripTags(std::string_view name) {
            std::string out;
            int depth = 0;
            for (const auto& c : Chars(name)) {
                if (Opens(c.cp)) {
                    ++depth;
                    out += ' ';
                } else if (Closes(c.cp) && depth > 0) {
                    --depth;
                    out += ' ';
                } else if (depth == 0) {
                    out += c.bytes;
                }
            }
            return out;
        }

        // Lower-case words of letters and digits; an apostrophe joins ("Hunter's" = "hunters"), anything else
        // separates.
        inline std::vector<std::string> Split(std::string_view text) {
            std::vector<std::string> words;
            std::string word;
            for (const auto& c : Chars(text)) {
                if (Letter(c.cp)) {
                    if (c.cp >= 0xFFFD0000u) word += c.bytes;
                    else Put(word, Lower(c.cp));
                } else if (Joiner(c.cp)) {
                    continue;
                } else if (!word.empty()) {
                    words.push_back(word);
                    word.clear();
                }
            }
            if (!word.empty()) words.push_back(word);
            return words;
        }

        // The rest after a short tag in front ("Drink - Ale", "Food: Apple", "Misc | Lantern", a full-width colon): one or two
        // words, then a separator. Empty when there is none.
        inline std::string AfterLeadingTag(const std::string& text) {
            static constexpr std::string_view separators[] = {" - ", " -- ", ": ", " | ", " \xE2\x80\x93 ", " \xE2\x80\x94 ",
                                                              "\xEF\xBC\x9A"};  // the last: a full-width colon
            std::size_t best = std::string::npos;
            std::size_t length = 0;
            for (const auto separator : separators) {
                const auto at = text.find(separator);
                if (at != std::string::npos && at < best) {
                    best = at;
                    length = separator.size();
                }
            }
            if (best == std::string::npos) return {};
            const auto head = Split(text.substr(0, best));
            const auto rest = text.substr(best + length);
            if (head.empty() || head.size() > 2 || Split(rest).empty()) return {};
            return rest;
        }

        // The ways of reading a name: all its words (tags left out), and the words after a leading tag if any.
        inline std::vector<std::vector<std::string>> Readings(std::string_view name) {
            std::vector<std::vector<std::string>> out;
            const auto plain = StripTags(name);
            if (auto words = Split(plain); !words.empty()) out.push_back(std::move(words));
            if (const auto rest = AfterLeadingTag(plain); !rest.empty()) out.push_back(Split(rest));
            return out;
        }

        inline std::string Joined(const std::vector<std::string>& words) {
            std::string out;
            for (const auto& w : words) out += w;
            return out;
        }

        inline std::vector<std::string> Sorted(std::vector<std::string> words) {
            std::sort(words.begin(), words.end());
            return words;
        }
    }

    // Two names of the same item: the same words in any order with tags left out ("Ingot - Iron" = "Iron Ingot",
    // "[Drink] Nord Mead" = "Nord Mead"), or the same letters run together ("Sweetroll" = "Sweet Roll").
    inline bool Same(std::string_view a, std::string_view b) {
        const auto ra = detail::Readings(a);
        const auto rb = detail::Readings(b);
        for (const auto& wa : ra) {
            for (const auto& wb : rb) {
                if (detail::Sorted(wa) == detail::Sorted(wb) || detail::Joined(wa) == detail::Joined(wb)) return true;
            }
        }
        return false;
    }

    // How well an item's name answers a vaguer word ("Mead" for "Nord Mead"): the said words end the name as whole
    // words, tags left out, and the name has more words. 0 = it does not; otherwise smaller is plainer (fewer words,
    // then fewer letters), so "Nord Mead" comes before "Honningbrew Mead". Languages written without spaces have no
    // such endings: there only the same item counts.
    inline int Loose(std::string_view wanted, std::string_view name) {
        const auto w = detail::Split(detail::StripTags(wanted));
        if (w.empty() || detail::Joined(w).size() < 3) return 0;
        int best = 0;
        for (const auto& n : detail::Readings(name)) {
            if (n.size() <= w.size() || !std::equal(w.rbegin(), w.rend(), n.rbegin())) continue;
            const int rank = static_cast<int>(n.size()) * 1000 + static_cast<int>(std::min<std::size_t>(detail::Joined(n).size(), 999));
            if (best == 0 || rank < best) best = rank;
        }
        return best;
    }

    // The name to put in what people say: tags in brackets left out ("[Drink] Nord Mead" is "Nord Mead").
    inline std::string Display(std::string_view name) {
        const auto plain = detail::StripTags(name);
        std::string out;
        for (const char c : plain) {
            if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
            out += c;
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return out.empty() ? std::string(name) : out;
    }
}
