#pragma once

// The game's text and ours: the one place where text of the game becomes UTF-8 (In) and the one where UTF-8 becomes
// text for the game (Out).
//
// Everything this mod exchanges with SkyrimNet and the dialogue model is UTF-8. What the game hands out is bytes, and
// CommonLib passes them on as they are (TESFullName::GetFullName and the rest return the game's own pointer).
//
// What those bytes are, looked up on a Special Edition 1.5.97 install (2026-11-22):
//   - The game's own string tables are UTF-8 in every language it ships: Skyrim - Interface.bsa holds the tables of
//     English, French, German, Italian, Spanish, Polish, Russian, Japanese and Chinese (about 90,000 strings each), and
//     not one string of them is anything else; the same for the Creation Club archives. Windows-1252 for German and
//     French, 1250 for Polish and 1251 for Russian were the old game (Legendary Edition), not this one. A name with an
//     umlaut from a German game therefore arrives right, and what the game shows is UTF-8.
//   - Names a plugin carries in itself are whatever its author's tool saved: on a list of 3,966 plugins, 31 of
//     197,798 names were no UTF-8: Windows-1252 (a curly apostrophe, an accent) and Windows-1251 (Russian names left
//     in three mods). Translations made for the old game and never converted are the same, in bulk.
//   - SkyrimNet reads such text the same way: what is well-formed UTF-8 stays, a byte that is not is read as
//     Windows-1252.
//
// So the rule, for every text that comes from the game (a name, a title, what a script read from the game and handed
// to a native):
//   1. Nothing above ASCII, or well-formed UTF-8: it is what it should be, and comes back as the same bytes (for the
//      pointer form: the same pointer). This is every text of an English game and every text of the game's own tables
//      in any language: the cost is one pass over the bytes.
//   2. Well-formed but for a letter cut off at the very end (a text shortened by bytes): the cut letter is dropped.
//   3. More stray bytes than well-formed letters: the text was saved in an old code page, and all of it is read in
//      that page.
//   4. Otherwise (UTF-8 with a stray byte or two, or a sentence put together from both): only the stray bytes are read
//      in the page, as SkyrimNet does.
// Which page: the setting "language.codepage" (a number: taken at its word), or by itself: the page the language of
// the game was written in before UTF-8 (PageOfLanguage), with one thing read from the text: words made of high bytes
// from end to end are Cyrillic (Windows-1251) whatever the game's language, since no Western or Central European
// word is written without a single plain letter (Guess).
//
// Out: the game shows UTF-8, so text for the game is the text itself, made well-formed if it was not. A setup that
// shows another page can say so ("language.display"); then text for the screen is written in that page.
//
// Single-byte pages are tables in this file's care (CodePageTables.h, taken from Windows); a page without a table
// (the double-byte ones: 932, 936, 949, 950) is read through a function the DLL hands in (OtherPages). Pure, std
// only, ASCII; no windows.h. Checked by GameTextCheck.

#include "CodePageTables.h"
#include "LetterFold.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace IntelEngine::GameText {

    inline constexpr int kUtf8 = 65001;

    // ---- is it UTF-8 ----------------------------------------------------------------------------------------------

    // The length of the well-formed letter that begins at `at` (1 for ASCII); 0 when none does. Strictly: no overlong
    // form, no surrogate, nothing above U+10FFFF.
    inline std::size_t Sequence(std::string_view s, std::size_t at) {
        const auto b0 = static_cast<unsigned char>(s[at]);
        if (b0 < 0x80) return 1;
        const auto next = [&](std::size_t k) -> unsigned { return at + k < s.size() ? static_cast<unsigned char>(s[at + k]) : 0u; };
        const auto cont = [](unsigned b) { return (b & 0xC0u) == 0x80u; };
        if (b0 >= 0xC2 && b0 <= 0xDF) return cont(next(1)) ? 2 : 0;
        if (b0 >= 0xE0 && b0 <= 0xEF) {
            const unsigned b1 = next(1);
            if (!cont(b1) || !cont(next(2))) return 0;
            if ((b0 == 0xE0 && b1 < 0xA0) || (b0 == 0xED && b1 >= 0xA0)) return 0;
            return 3;
        }
        if (b0 >= 0xF0 && b0 <= 0xF4) {
            const unsigned b1 = next(1);
            if (!cont(b1) || !cont(next(2)) || !cont(next(3))) return 0;
            if ((b0 == 0xF0 && b1 < 0x90) || (b0 == 0xF4 && b1 >= 0x90)) return 0;
            return 4;
        }
        return 0;
    }

    inline bool Ascii(std::string_view s) {
        for (const char c : s) {
            if (static_cast<unsigned char>(c) >= 0x80) return false;
        }
        return true;
    }

    inline bool WellFormed(std::string_view s) {
        for (std::size_t i = 0; i < s.size();) {
            const std::size_t n = Sequence(s, i);
            if (n == 0) return false;
            i += n;
        }
        return true;
    }

    // What a text is made of beyond ASCII.
    struct Shape {
        std::size_t good = 0;   // well-formed letters
        std::size_t bad = 0;    // bytes that belong to none
        std::size_t cut = 0;    // > 0: the only bad bytes are the beginning of one letter at the very end (so many)
    };
    inline Shape Look(std::string_view s) {
        Shape shape;
        std::size_t firstBad = 0;
        for (std::size_t i = 0; i < s.size();) {
            const std::size_t n = Sequence(s, i);
            if (n == 0) {
                if (shape.bad++ == 0) firstBad = i;
                ++i;
            } else {
                if (n > 1) ++shape.good;
                i += n;
            }
        }
        if (shape.bad > 0 && shape.bad <= 3 && shape.good > 0 && firstBad + shape.bad == s.size()) {
            const auto b0 = static_cast<unsigned char>(s[firstBad]);
            const std::size_t need = (b0 >= 0xC2 && b0 <= 0xDF) ? 1 : (b0 >= 0xE0 && b0 <= 0xEF) ? 2 : (b0 >= 0xF0 && b0 <= 0xF4) ? 3 : 0;
            bool follows = need > 0 && shape.bad - 1 < need;
            for (std::size_t k = 1; follows && k < shape.bad; ++k) {
                follows = (static_cast<unsigned char>(s[firstBad + k]) & 0xC0) == 0x80;
            }
            if (follows) shape.cut = shape.bad;
        }
        return shape;
    }

    // ---- code pages -----------------------------------------------------------------------------------------------

    // A page without a table here, read or written by the system: bytes of `page` to UTF-8 (toUtf8) or back.
    using OtherPage = std::string (*)(std::string_view text, int page, bool toUtf8);
    inline std::atomic<OtherPage>& OtherPages() {
        static std::atomic<OtherPage> other{nullptr};
        return other;
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

    // Bytes of a page as UTF-8, every byte. A page that neither has a table nor a reader is read as Windows-1252.
    inline void Decode(std::string& out, std::string_view bytes, int page) {
        const std::uint16_t* table = CodePageTables::Of(page);
        if (!table) {
            if (const OtherPage other = OtherPages().load()) {
                out += other(bytes, page, true);
                return;
            }
            table = CodePageTables::k1252;
        }
        for (const char c : bytes) {
            const auto b = static_cast<unsigned char>(c);
            Put(out, b < 0x80 ? b : table[b - 0x80]);
        }
    }
    inline std::string Decode(std::string_view bytes, int page) {
        std::string out;
        out.reserve(bytes.size() * 2);
        Decode(out, bytes, page);
        return out;
    }

    // Well-formed UTF-8 as bytes of a page. A letter the page does not have is written as its plain letter where it
    // has one ("l" for an l with a stroke in a Western page), else as a question mark.
    inline std::string Encode(std::string_view utf8, int page) {
        const std::uint16_t* table = CodePageTables::Of(page);
        if (!table) {
            if (const OtherPage other = OtherPages().load()) return other(utf8, page, false);
            return std::string(utf8);
        }
        const auto byteOf = [table](char32_t cp) -> int {
            if (cp < 0x80) return static_cast<int>(cp);
            for (int i = 0; i < 128; ++i) {
                if (table[i] == cp) return 0x80 + i;
            }
            return -1;
        };
        std::string out;
        out.reserve(utf8.size());
        for (std::size_t i = 0; i < utf8.size();) {
            const std::size_t n = Sequence(utf8, i);
            if (n <= 1) {
                out += n == 1 ? utf8[i] : '?';
                ++i;
                continue;
            }
            const auto b0 = static_cast<unsigned char>(utf8[i]);
            char32_t cp = b0 & (n == 2 ? 0x1Fu : n == 3 ? 0x0Fu : 0x07u);
            for (std::size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3Fu);
            i += n;
            if (const int b = byteOf(cp); b >= 0) {
                out += static_cast<char>(b);
                continue;
            }
            char32_t plain[2];
            const int count = LetterFold::Fold(cp, plain);
            bool written = count > 0;
            std::string letters;
            for (int k = 0; k < count; ++k) {
                const int b = byteOf(plain[k]);
                if (b < 0) written = false;
                else letters += static_cast<char>(b);
            }
            if (written) out += letters;
            else if (count > 0) out += '?';
        }
        return out;
    }

    // ---- which page -----------------------------------------------------------------------------------------------

    namespace detail {
        inline std::string Lower(std::string_view s) {
            std::string out;
            for (const char c : s) {
                if (c == ' ' || c == '\t' || c == '"') continue;
                out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            }
            return out;
        }
    }

    // The page a language was written in before UTF-8: by the game's word for it (Skyrim.ini's sLanguage), its
    // English name or its code. 0: a language this does not know.
    inline int PageOfLanguage(std::string_view language) {
        struct Row {
            int page;
            const char* names;   // with a space before and after each
        };
        static constexpr Row kRows[] = {
            {1252, " english french german italian spanish portuguese danish dutch swedish norwegian finnish albanian icelandic"
                   " en fr de it es pt da nl sv no nb fi sq aln is "},
            {1250, " polish czech hungarian slovak romanian croatian slovenian pl cs hu sk ro hr sl "},
            {1251, " russian ukrainian bulgarian belarusian serbian macedonian ru uk bg be sr mk "},
            {1253, " greek el "},
            {1254, " turkish tr "},
            {1257, " lithuanian latvian estonian lt lv et "},
            {932, " japanese ja "},
            {950, " chinese zh "},   // the game's CHINESE is Traditional Chinese
            {949, " korean ko "},
        };
        const std::string word = " " + detail::Lower(language) + " ";
        if (word.size() <= 2) return 0;
        for (const auto& row : kRows) {
            if (std::string_view(row.names).find(word) != std::string_view::npos) return row.page;
        }
        return 0;
    }

    // What the setting says: 0 for "auto" or nothing (decide by the language); kUtf8 for "utf8" / "utf-8"; a page for
    // a number however it is dressed ("1250", "cp1250", "windows-1250"); the page of a language named ("polish").
    inline int PageFromSetting(std::string_view setting) {
        const std::string word = detail::Lower(setting);
        if (word.empty() || word == "auto") return 0;
        if (word == "utf8" || word == "utf-8" || word == "unicode") return kUtf8;
        int number = 0;
        std::size_t digits = 0;
        for (std::size_t i = word.size(); i > 0 && word[i - 1] >= '0' && word[i - 1] <= '9'; --i) ++digits;
        if (digits >= 3 && digits <= 5) {
            for (std::size_t i = word.size() - digits; i < word.size(); ++i) number = number * 10 + (word[i] - '0');
            return number;
        }
        return PageOfLanguage(word);
    }

    // Is this text Cyrillic, whatever page the language says? `mine[i]` != 0 marks the bytes that are to be read in
    // the page. Words of four letters or more made of the letters of Windows-1251 from end to end (0xC0 and up, and
    // the two yo), with no plain letter in the word: Russian, never a Western or Central European word (which keeps
    // plain letters between its marked ones). Greek, written the same way, is told by the language (base 1253).
    inline int Guess(std::string_view s, std::string_view mine, int base) {
        if (base == 1251 || base == 1253 || !CodePageTables::Of(base)) return base;
        const auto plainLetter = [](unsigned char b) { return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z'); };
        std::size_t run = 0;
        bool mixed = false;   // the word so far has a plain letter in it
        for (std::size_t i = 0; i <= s.size(); ++i) {
            const unsigned char b = i < s.size() ? static_cast<unsigned char>(s[i]) : 0;
            const bool high = i < s.size() && mine[i] && (b >= 0xC0 || b == 0xA8 || b == 0xB8);
            if (high) {
                ++run;
                continue;
            }
            if (i < s.size() && plainLetter(b)) {
                mixed = true;
                run = 0;
                continue;
            }
            if (run >= 4 && !mixed) return 1251;
            run = 0;
            mixed = false;
        }
        return base;
    }

    // ---- the rule -------------------------------------------------------------------------------------------------

    // Text of the game as UTF-8 (the four cases at the top). `page`: what a stray byte is read in; `guess`: let the
    // text itself say it is Cyrillic.
    inline std::string ToUtf8(std::string_view text, int page, bool guess = true) {
        if (Ascii(text)) return std::string(text);
        const Shape shape = Look(text);
        if (shape.bad == 0) return std::string(text);
        if (shape.cut > 0) return std::string(text.substr(0, text.size() - shape.cut));
        if (page == kUtf8 || page <= 0) page = 1252;
        const bool whole = shape.bad > shape.good;
        // A page of two bytes to a letter (932, 936, 949, 950) has letters whose second byte is ASCII ("\x83z"):
        // such text is read whole, never cut where a byte is ASCII.
        if (whole && !CodePageTables::Of(page)) return Decode(text, page);
        // Which bytes are the page's: every byte above ASCII (the whole text is old), or the stray ones alone.
        std::string mine(text.size(), '\0');
        for (std::size_t i = 0; i < text.size();) {
            const std::size_t n = whole ? 0 : Sequence(text, i);
            if (n == 0) {
                mine[i] = static_cast<unsigned char>(text[i]) >= 0x80 ? '\1' : '\0';
                ++i;
            } else {
                i += n;
            }
        }
        if (guess) page = Guess(text, mine, page);
        std::string out;
        out.reserve(text.size() * 2);
        for (std::size_t i = 0; i < text.size();) {
            std::size_t end = i;
            while (end < text.size() && mine[end]) ++end;
            if (end > i) {
                Decode(out, text.substr(i, end - i), page);
                i = end;
                continue;
            }
            while (end < text.size() && !mine[end]) ++end;
            out.append(text.substr(i, end - i));
            i = end;
        }
        return out;
    }

    // ---- what is in force: decided once a game (LanguageBridge) ----------------------------------------------------

    struct InForce {
        std::atomic<int> read{1252};      // the page of text that is no UTF-8
        std::atomic<bool> guess{true};    // the page was not named by the user: the text may say it is Cyrillic
        std::atomic<int> write{kUtf8};    // what the game shows
    };
    inline InForce& Now() {
        static InForce now;
        return now;
    }
    inline void Use(int readPage, bool guess, int writePage = kUtf8) {
        auto& now = Now();
        now.read.store(readPage > 0 && readPage != kUtf8 ? readPage : 1252);
        now.guess.store(guess);
        now.write.store(writePage > 0 ? writePage : kUtf8);
    }

    namespace detail {
        // Old text made UTF-8 once and kept for good, so that a name can be handed on as a pointer the way the game
        // hands it out. Only names come this way (a load order has a few dozen such): it does not grow in play.
        inline const char* Kept(std::string_view raw) {
            struct Store {
                std::shared_mutex mutex;
                std::unordered_map<std::string, std::string> made;
            };
            static Store store;
            const int page = Now().read.load();
            const bool guess = Now().guess.load();
            std::string key = std::to_string(page);
            key += guess ? '?' : '!';
            key += raw;
            {
                std::shared_lock lock(store.mutex);
                if (const auto it = store.made.find(key); it != store.made.end()) return it->second.c_str();
            }
            std::string utf8 = ToUtf8(raw, page, guess);
            std::unique_lock lock(store.mutex);
            return store.made.try_emplace(std::move(key), std::move(utf8)).first->second.c_str();
        }
    }

    // ---- the two doors --------------------------------------------------------------------------------------------

    // A name as the game hands it out (a pointer, perhaps null), as UTF-8. The same pointer whenever the text is
    // already what it should be.
    inline const char* In(const char* gameText) {
        if (!gameText) return gameText;
        const char* p = gameText;
        while (*p && static_cast<unsigned char>(*p) < 0x80) ++p;
        if (!*p) return gameText;
        const std::string_view text(gameText);
        if (WellFormed(text)) return gameText;
        return detail::Kept(text);
    }

    // Any text that came from the game (what a script read there and passed on), as UTF-8.
    inline std::string In(const std::string& gameText) {
        if (Ascii(gameText) || WellFormed(gameText)) return gameText;
        return ToUtf8(gameText, Now().read.load(), Now().guess.load());
    }

    // UTF-8 as text for the game to show.
    inline std::string Out(std::string_view text) {
        const int page = Now().write.load();
        if (Ascii(text)) return std::string(text);
        std::string utf8 = WellFormed(text) ? std::string(text) : ToUtf8(text, Now().read.load(), Now().guess.load());
        if (page == kUtf8) return utf8;
        return Encode(utf8, page);
    }
}
