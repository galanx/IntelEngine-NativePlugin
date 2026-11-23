// Standalone C++20 check; no game ABI or test framework. This file is UTF-8 (compile with /utf-8 under MSVC):
// cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /I SKSE/src SKSE/tests/GameTextCheck.cpp
//
// The two doors of game text (GameText.h) and the plain letters names are compared by (LetterFold.h):
//   1. What is UTF-8 already (every string table of Special Edition, in every language) passes untouched, the same
//      pointer; a letter cut off at the end is dropped; text an old plugin saved in a Windows code page (1252, 1250,
//      1251) is read in that page, whole or byte by byte; Cyrillic is told by the text itself.
//   2. Names go game -> model -> game in German, French, Polish and Russian: an NPC, a place and an item with
//      letters beyond ASCII, as the string tables give them and as an old plugin saved them; the model writes them
//      in other case, without their marks, with an ending of its language; the lookup finds the record it came from,
//      and what goes back into the game is the game's own text (UTF-8, or the old page where a setup shows that).
#include "GameText.h"
#include "ItemNames.h"
#include "LetterFold.h"
#include "NameForms.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace IntelEngine;

static void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static std::string Hex(const std::string& s) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : s) {
        if (c >= 0x20 && c < 0x7F) {
            out += static_cast<char>(c);
        } else {
            out += "\\x";
            out += digits[c >> 4];
            out += digits[c & 15];
        }
    }
    return out;
}

static void Equal(const std::string& got, const std::string& want, const std::string& what) {
    Require(got == want, what + ": got '" + Hex(got) + "', wanted '" + Hex(want) + "'");
}

// Folded the way the lookups fold (StringUtils::ToLowerStd / LanguageBridge::Folded): lower case, plain letters.
static std::string Fold(const std::string& s) { return LetterFold::Plain(s); }

static const NameForms::FreeWord kFree = [](std::u32string_view w) {
    // the little words of the four packs that matter here (articles, prepositions): what a pack's `free` row gives
    for (const char32_t* f : {U"the", U"a", U"an", U"der", U"die", U"das", U"dem", U"den", U"in", U"nach", U"la", U"le", U"l", U"de", U"a", U"w",
                              U"we", U"do", U"z", U"в", U"во", U"к", U"из"}) {
        if (w == f) return true;
    }
    return false;
};

// One name of the game, as each source gives it.
struct GameName {
    const char* language;
    std::string table;    // the string tables of Special Edition: UTF-8
    std::string legacy;   // an old plugin of that language: its Windows code page
    int page;             // that page
};

// What the DLL indexes: the name through the door in, folded.
static std::string Indexed(const std::string& bytes) { return Fold(GameText::In(bytes)); }

static void RoundTripName(const std::vector<GameName>& names, const GameName& meant, const std::vector<std::string>& said, const char* kind) {
    for (const bool old : {false, true}) {
        GameText::Use(meant.page, true);
        std::vector<std::string> index;
        for (const auto& n : names) index.push_back(Indexed(old ? n.legacy : n.table));
        const std::string mine = GameText::In(old ? meant.legacy : meant.table);
        Equal(mine, meant.table, std::string(meant.language) + " " + kind + (old ? " from an old plugin" : " from the tables") + " arrives as UTF-8");
        // people as NPCIndex finds them (a first name alone will do when it is nobody else's), places as
        // LocationResolver does (the whole name, in any of its forms)
        const int atLeast = std::string(kind) == "NPC" ? NameForms::PartForms : NameForms::Forms;
        for (const auto& s : said) {
            const auto best = NameForms::Find(Fold(s), index, atLeast, kFree);
            Require(best && index[static_cast<std::size_t>(best.index)] == Indexed(old ? meant.legacy : meant.table),
                    std::string(meant.language) + " " + kind + ": the model wrote '" + s + "' and the lookup " +
                        (best ? "found '" + index[static_cast<std::size_t>(best.index)] + "'" : std::string("found nobody")));
        }
        // back into the game: the record's own name, as the game shows it
        Equal(GameText::Out(mine), meant.table, std::string(meant.language) + " " + kind + " goes back as the game shows it (UTF-8)");
        GameText::Use(meant.page, true, meant.page);
        Equal(GameText::Out(mine), meant.legacy, std::string(meant.language) + " " + kind + " goes back in the old page where a setup shows that");
        GameText::Use(meant.page, true);
    }
}

static void RoundTripItem(const GameName& meant, const std::vector<GameName>& others, const std::vector<std::string>& said) {
    for (const bool old : {false, true}) {
        GameText::Use(meant.page, true);
        ItemNames::Book book;
        std::vector<std::string> shown;
        for (const auto* n : {&others[0], &meant, &others[1]}) {
            shown.push_back(GameText::In(old ? n->legacy : n->table));
            book.Add(shown.back());
        }
        for (const auto& s : said) {
            const auto i = book.FindSame(s);
            Require(i == 1, std::string(meant.language) + " item: the model wrote '" + s + "' and the item book " +
                                (i < 0 ? std::string("found nothing") : "found '" + shown[static_cast<std::size_t>(i)] + "'"));
        }
        Equal(GameText::Out(shown[1]), meant.table, std::string(meant.language) + " item goes back as the game shows it");
    }
}

int main() {
    try {
        // ---- 1. is it UTF-8 --------------------------------------------------------------------------------
        Require(GameText::WellFormed("Whiterun") && GameText::WellFormed("Weißlauf") && GameText::WellFormed("Вайтран") &&
                    GameText::WellFormed("ホワイトラン") && GameText::WellFormed("\xF0\x9F\x98\x80"),
                "UTF-8 of any script is well-formed");
        Require(!GameText::WellFormed("Wei\xDF" "lauf") && !GameText::WellFormed("\xC0\xAF") && !GameText::WellFormed("\xED\xA0\x80") &&
                    !GameText::WellFormed("\xF4\x90\x80\x80") && !GameText::WellFormed("\xE2\x82"),
                "a byte of an old page, an overlong form, a surrogate, beyond U+10FFFF, a cut sequence are not");

        // ---- the door in: nothing to do for what is right -------------------------------------------------------
        GameText::Use(1252, true);
        {
            const char* english = "Bannered Mare";
            const char* german = "Weißlauf";
            const char* russian = "Вайтран";
            Require(GameText::In(english) == english && GameText::In(german) == german && GameText::In(russian) == russian,
                    "ASCII and UTF-8 come back as the same pointer");
            Require(GameText::In(static_cast<const char*>(nullptr)) == nullptr, "no name stays no name");
            const std::string legacy = "Wei\xDF" "lauf";
            const char* once = GameText::In(legacy.c_str());
            Require(once != legacy.c_str() && std::string(once) == "Weißlauf" && GameText::In(legacy.c_str()) == once,
                    "a name in an old page is made UTF-8 once and handed out as one pointer");
        }
        Equal(GameText::In(std::string("Вайтр") + "\xD0"), "Вайтр", "a letter cut off at the end is dropped");
        Equal(GameText::In(std::string("I\x92m like you\x85")), "I’m like you…", "Windows-1252 punctuation (a plugin of 3,966 had it)");
        Equal(GameText::In(std::string("Weißlauf’s \x92" "best\x92")), "Weißlauf’s ’best’", "stray bytes in UTF-8 are read in the page, the rest stays");
        Equal(GameText::In(std::string("Sn\xE6rving")), "Snærving", "a whole name in Windows-1252");

        // Cyrillic is told by the text, whatever the game's language (a German game with a Russian mod left unconverted)
        Equal(GameText::In(std::string("\xD0\xE0\xE7\xF0\xF3\xF8\xE8\xF2\xE5\xEB\xFC \xC7\xE0\xEA\xEB\xE8\xED\xE0\xED\xE8\xE9")), "Разрушитель Заклинаний",
              "Windows-1251 in a game of Western page (11Debaser.esl's name)");
        GameText::Use(1252, false);
        Equal(GameText::In(std::string("\xC2\xE0\xE9\xF2\xF0\xE0\xED")), "Âàéòðàí", "a page the user named is taken at its word");
        GameText::Use(1250, true);
        Equal(GameText::In(std::string("Bia\xB3" "a Gra\xF1")), "Biała Grań", "Windows-1250 in a Polish game");
        Equal(GameText::In(std::string("\x8C" "wiat \xBF" "ycia")), "Świat życia", "the 1250 letters at 0x8C and 0xBF");
        GameText::Use(1251, true);
        Equal(GameText::In(std::string("\xC2\xE0\xE9\xF2\xF0\xE0\xED \xB8\xA8")), "Вайтран ёЁ", "Windows-1251 in a Russian game, yo included");

        // ---- which page ---------------------------------------------------------------------------------------
        Require(GameText::PageOfLanguage("GERMAN") == 1252 && GameText::PageOfLanguage("french") == 1252 && GameText::PageOfLanguage("POLISH") == 1250 &&
                    GameText::PageOfLanguage("RUSSIAN") == 1251 && GameText::PageOfLanguage("japanese") == 932 && GameText::PageOfLanguage("CHINESE") == 950 &&
                    GameText::PageOfLanguage("de") == 1252 && GameText::PageOfLanguage("pl") == 1250 && GameText::PageOfLanguage("") == 0 &&
                    GameText::PageOfLanguage("klingon") == 0,
                "the page of each language the game ships (sLanguage), and of codes");
        Require(GameText::PageFromSetting("auto") == 0 && GameText::PageFromSetting("") == 0 && GameText::PageFromSetting("UTF-8") == GameText::kUtf8 &&
                    GameText::PageFromSetting("1250") == 1250 && GameText::PageFromSetting("cp1251") == 1251 &&
                    GameText::PageFromSetting("windows-1252") == 1252 && GameText::PageFromSetting("Polish") == 1250,
                "the setting language.codepage, however it is written");

        // ---- the system's pages: a double-byte page is read through the DLL's reader -----------------------------
        {
            static int asked = 0;
            GameText::OtherPages().store([](std::string_view text, int page, bool toUtf8) -> std::string {
                ++asked;
                return page == 932 && toUtf8 ? std::string("[932:") + std::string(text) + "]" : std::string(text);
            });
            GameText::Use(932, true);
            Equal(GameText::In(std::string("\x83z\x83\x8F")), "[932:\x83z\x83\x8F]", "Shift-JIS goes to the system's reader");
            Require(asked == 1, "the reader was asked once");
            GameText::OtherPages().store(nullptr);
        }

        // ---- the door out -------------------------------------------------------------------------------------
        GameText::Use(1252, true);
        Equal(GameText::Out("Weißlauf"), "Weißlauf", "the game shows UTF-8: text goes as it is");
        Equal(GameText::Out(std::string("Wei\xDF" "lauf")), "Weißlauf", "text that is no UTF-8 is made so on the way out");
        GameText::Use(1252, true, 1252);
        Equal(GameText::Out("Weißlauf"), "Wei\xDF" "lauf", "a setup that shows Windows-1252");
        Equal(GameText::Out("Biała Grań"), "Biala Gran", "a letter the page has not goes as its plain letter");
        Equal(GameText::Out("Вайтран"), "???????", "and a letter without one as a question mark");
        GameText::Use(1250, true, 1250);
        Equal(GameText::Out("Biała Grań"), "Bia\xB3" "a Gra\xF1", "Windows-1250");
        GameText::Use(1252, true);

        // ---- plain letters ------------------------------------------------------------------------------------
        Equal(Fold("Weißlauf"), "weisslauf", "a sharp s is two s");
        Equal(Fold("ÉPÉE en Acier"), "epee en acier", "capitals with accents");
        Equal(Fold("Biała Grań"), "biala gran", "a stroke and an acute");
        Equal(Fold("Чёрный Вереск"), "черныи вереск", "yo and short i");
        Equal(Fold("Ærø Þórr"), "aero thorr", "ligature and thorn");
        Equal(Fold("Belethor’s"), "belethor's", "the curly apostrophe");
        Equal(Fold("Ｗｈｉｔｅｒｕｎ"), "whiterun", "full-width letters");
        Equal(Fold("e\xCC\x81" "glise"), "eglise", "a mark standing by itself after its letter");
        Equal(Fold("ホワイトラン"), "ホワイトラン", "kana as they are");

        // ---- 2. game -> model -> game ---------------------------------------------------------------------------
        // German (Windows-1252 before Special Edition)
        {
            const GameName balgruuf{"German", "Balgruuf der Große", "Balgruuf der Gro\xDF" "e", 1252};
            const GameName other{"German", "Balgruuf", "Balgruuf", 1252};
            const GameName brenuin{"German", "Brenuin", "Brenuin", 1252};
            RoundTripName({other, balgruuf, brenuin}, balgruuf, {"Balgruuf der Grosse", "balgruuf dem Großen", "BALGRUUF DER GROSSE"}, "NPC");
            const GameName whiterun{"German", "Weißlauf", "Wei\xDF" "lauf", 1252};
            const GameName dawnstar{"German", "Dämmerstern", "D\xE4mmerstern", 1252};
            const GameName riverwood{"German", "Flusswald", "Flusswald", 1252};
            RoundTripName({riverwood, whiterun, dawnstar}, whiterun, {"Weisslauf", "nach Weißlauf", "in Weisslauf"}, "place");
            RoundTripName({riverwood, whiterun, dawnstar}, dawnstar, {"Dammerstern", "DÄMMERSTERN", "nach Dämmerstern"}, "place");
            const GameName sweetroll{"German", "Süßrolle", "S\xFC\xDFrolle", 1252};
            RoundTripItem(sweetroll, {{"German", "Süßkartoffel", "S\xFC\xDFkartoffel", 1252}, {"German", "Rolle", "Rolle", 1252}},
                          {"Süssrolle", "Sussrolle", "süßrolle", "SÜSSROLLE"});
        }
        // French
        {
            const GameName elisif{"French", "Élisif la Belle", "\xC9lisif la Belle", 1252};
            const GameName other{"French", "Elenwen", "Elenwen", 1252};
            RoundTripName({other, elisif}, elisif, {"Elisif la Belle", "élisif", "ELISIF LA BELLE"}, "NPC");
            const GameName dawnstar{"French", "Aubétoile", "Aub\xE9toile", 1252};
            const GameName whiterun{"French", "Blancherive", "Blancherive", 1252};
            RoundTripName({whiterun, dawnstar}, dawnstar, {"Aubetoile", "à Aubétoile", "AUBÉTOILE"}, "place");
            const GameName sword{"French", "Épée en acier", "\xC9p\xE9" "e en acier", 1252};
            RoundTripItem(sword, {{"French", "Épée en fer", "\xC9p\xE9" "e en fer", 1252}, {"French", "Acier", "Acier", 1252}},
                          {"épée en acier", "Epee en acier", "ÉPÉE EN ACIER", "acier en épée"});
        }
        // Polish (Windows-1250 before Special Edition)
        {
            const GameName ulfric{"Polish", "Ulfryk Gromowładny", "Ulfryk Gromow\xB3" "adny", 1250};
            const GameName other{"Polish", "Ulfryk", "Ulfryk", 1250};
            RoundTripName({other, ulfric}, ulfric, {"Ulfryk Gromowladny", "Ulfryka Gromowładnego", "ulfryk gromowładny"}, "NPC");
            const GameName whiterun{"Polish", "Biała Grań", "Bia\xB3" "a Gra\xF1", 1250};
            const GameName solitude{"Polish", "Samotnia", "Samotnia", 1250};
            RoundTripName({solitude, whiterun}, whiterun, {"Biala Gran", "w Białej Grani", "do Białej Grani", "BIAŁA GRAŃ"}, "place");
            const GameName sweetroll{"Polish", "Słodka bułka", "S\xB3" "odka bu\xB3" "ka", 1250};
            RoundTripItem(sweetroll, {{"Polish", "Słodka woda", "S\xB3" "odka woda", 1250}, {"Polish", "Bułka", "Bu\xB3" "ka", 1250}},
                          {"slodka bulka", "Słodka Bułka", "SŁODKA BUŁKA"});
        }
        // Russian (Windows-1251 before Special Edition)
        {
            const GameName ulfric{"Russian", "Ульфрик Буревестник", "\xD3\xEB\xFC\xF4\xF0\xE8\xEA \xC1\xF3\xF0\xE5\xE2\xE5\xF1\xF2\xED\xE8\xEA", 1251};
            const GameName other{"Russian", "Ульфрик", "\xD3\xEB\xFC\xF4\xF0\xE8\xEA", 1251};
            RoundTripName({other, ulfric}, ulfric, {"ульфрик буревестник", "Ульфрика Буревестника", "УЛЬФРИК БУРЕВЕСТНИК"}, "NPC");
            const GameName whiterun{"Russian", "Вайтран", "\xC2\xE0\xE9\xF2\xF0\xE0\xED", 1251};
            const GameName riverwood{"Russian", "Ривервуд", "\xD0\xE8\xE2\xE5\xF0\xE2\xF3\xE4", 1251};
            RoundTripName({riverwood, whiterun}, whiterun, {"в Вайтране", "Ваитран", "из Вайтрана", "ВАЙТРАН"}, "place");
            const GameName mead{"Russian", "Медовуха Чёрного Вереска", "\xCC\xE5\xE4\xEE\xE2\xF3\xF5\xE0 \xD7\xB8\xF0\xED\xEE\xE3\xEE \xC2\xE5\xF0\xE5\xF1\xEA\xE0", 1251};
            RoundTripItem(mead, {{"Russian", "Медовуха", "\xCC\xE5\xE4\xEE\xE2\xF3\xF5\xE0", 1251},
                                 {"Russian", "Вино Чёрного Вереска", "\xC2\xE8\xED\xEE \xD7\xB8\xF0\xED\xEE\xE3\xEE \xC2\xE5\xF0\xE5\xF1\xEA\xE0", 1251}},
                          {"медовуха черного вереска", "Медовуха Чёрного Вереска", "МЕДОВУХА ЧЁРНОГО ВЕРЕСКА"});
            // and the same Russian names in a German game: the text itself says it is Cyrillic
            GameText::Use(1252, true);
            Equal(GameText::In(ulfric.legacy), ulfric.table, "a Russian name saved in 1251, met in a German game");
        }

        // ---- never another name ------------------------------------------------------------------------------
        {
            GameText::Use(1252, true);
            const std::vector<std::string> index{Indexed("Weißlauf"), Indexed("Weißlaufs Wache"), Indexed("Weiler")};
            const auto one = NameForms::Find(Fold("Weisslauf"), index, NameForms::Forms, kFree);
            Require(one && one.index == 0, "Weisslauf is Weißlauf, not its guard");
            Require(!NameForms::Find(Fold("Weissdorn"), index, NameForms::Forms, kFree), "Weissdorn is nobody here");
            Require(!ItemNames::Same("Süßrolle", "Süßkartoffel") && !ItemNames::Same("Słodka bułka", "Słodka woda"), "two items stay two");
        }
    } catch (const std::exception& e) {
        std::cerr << "GameTextCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "GameTextCheck passed\n";
    return 0;
}
