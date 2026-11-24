// Standalone C++20 check; no game ABI or test framework. This file is UTF-8 (compile with /utf-8 under MSVC):
// cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /I SKSE/src SKSE/tests/LanguagePackCheck.cpp SKSE/src/NaturalTimeParser.cpp
// LanguagePackCheck.exe <folder of the shipped packs>     (IntelEngine-GamePlugin/SKSE/Plugins/IntelEngine/languages)
//
// The language packs as they ship, against the readers they feed: a time said in each language comes out of the
// real time reader as the hour it means; a word of place is English when it is the whole of what was said and a
// name otherwise; "none" in any language is none; a label is found under names; the game's language is told from
// its setting, its names and the translator's settings.
#include "LanguagePack.h"
#include "NaturalTimeParser.h"
#include "Utf8Fold.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace IntelEngine;
using namespace IntelEngine::Language;

static void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static std::vector<Pack> gPacks;

static const Pack& PackOf(const std::string& file) {
    for (const auto& p : gPacks) {
        if (p.file == file) return p;
    }
    throw std::runtime_error("no pack " + file);
}

// What the game does with a time: fold, into English by the packs in force, then the time reader. At ten in the
// morning, so "in 2 hours" is noon.
static float Hour(const std::string& pack, const std::string& said) {
    const Pack& p = PackOf(pack);
    const std::string english = TimeToEnglish(FoldUtf8(said), {&p.time});
    return NaturalTime::Parse(english, 10.0f);
}
static void Time(const std::string& pack, const std::string& said, float want) {
    const float got = Hour(pack, said);
    if (std::fabs(got - want) > 0.01f) {
        const Pack& p = PackOf(pack);
        std::ostringstream out;
        out << pack << ": \"" << said << "\" read as \"" << TimeToEnglish(FoldUtf8(said), {&p.time}) << "\" gave " << got << ", wanted " << want;
        throw std::runtime_error(out.str());
    }
}
static NameForms::FreeWord FreeOf(const Pack& p) {
    return [&p](std::u32string_view w) {
        for (const auto& f : p.free) {
            if (std::u32string_view(f) == w) return true;
        }
        return false;
    };
}
static std::string Place(const std::string& pack, const std::string& said) {
    const Pack& p = PackOf(pack);
    return PlaceToEnglish(FoldUtf8(said), {&p.place}, FreeOf(p));
}

int main(int argc, char** argv) {
    try {
        const std::filesystem::path folder = argc > 1 ? argv[1] : "../IntelEngine-GamePlugin/SKSE/Plugins/IntelEngine/languages";
        for (const auto& file : std::filesystem::directory_iterator(folder)) {
            if (file.path().extension() != ".tsv" || file.path().stem().string().rfind("_", 0) == 0) continue;
            std::ifstream in(file.path(), std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            gPacks.push_back(ReadPack(text.str(), file.path().stem().string(), FoldUtf8));
            const Pack& p = gPacks.back();
            Require(p.bad == 0, p.file + ": " + std::to_string(p.bad) + " line(s) not understood");
            Require(!p.code.empty() && !p.name.empty() && !p.script.empty(), p.file + ": meta code, name and script are needed");
            Require(!p.time.empty() && !p.place.empty() && !p.none.empty(), p.file + ": time, place and none are needed");
        }
        Require(gPacks.size() >= 10, "ten packs ship");

        // ---- times, each as its language says it (read at ten in the morning; +24 is tomorrow)
        Time("russian", "завтра утром", 32);
        Time("russian", "через 2 часа", 12);
        Time("russian", "через два часа", 12);
        Time("russian", "в 18:00", 18);
        Time("russian", "в шесть вечера", 18);
        Time("russian", "в 6 часов вечера", 18);
        Time("russian", "в полночь", 0);
        Time("russian", "на рассвете", 5);
        Time("russian", "вечером", 18);
        Time("russian", "завтра в 9 утра", 33);
        Time("russian", "послезавтра в полдень", 60);
        Time("russian", "через полчаса", 10.5f);

        Time("german", "morgen früh", 32);
        Time("german", "heute Abend", 18);
        Time("german", "um 18 Uhr", 18);
        Time("german", "in zwei Stunden", 12);
        Time("german", "in einer Stunde", 11);
        Time("german", "morgen um 9 Uhr", 33);
        Time("german", "bei Sonnenuntergang", 19);
        Time("german", "um Mitternacht", 0);
        Time("german", "übermorgen mittags", 60);
        Time("german", "in einer halben Stunde", 10.5f);

        Time("french", "demain matin", 32);
        Time("french", "ce soir", 18);
        Time("french", "à 18 heures", 18);
        Time("french", "dans deux heures", 12);
        Time("french", "à minuit", 0);
        Time("french", "à 6 heures du soir", 18);

        Time("spanish", "mañana por la mañana", 32);
        Time("spanish", "esta noche", 22);
        Time("spanish", "a las 6 de la tarde", 18);
        Time("spanish", "en dos horas", 12);
        Time("spanish", "al amanecer", 5);

        Time("italian", "domani mattina", 32);
        Time("italian", "stasera", 18);
        Time("italian", "alle 18", 18);
        Time("italian", "tra due ore", 12);
        Time("italian", "a mezzanotte", 0);

        Time("polish", "jutro rano", 32);
        Time("polish", "wieczorem", 18);
        Time("polish", "za dwie godziny", 12);
        Time("polish", "o północy", 0);
        Time("polish", "o 18:00", 18);

        Time("japanese", "明日の朝", 32);
        Time("japanese", "午後6時", 18);
        Time("japanese", "2時間後", 12);
        Time("japanese", "今夜", 22);
        Time("japanese", "正午", 12);
        Time("japanese", "明日の午前9時", 33);
        Time("japanese", "夜明け", 5);

        Time("korean", "내일 아침", 32);
        Time("korean", "오후 6시", 18);
        Time("korean", "2시간 후", 12);
        Time("korean", "오늘 밤", 22);
        Time("korean", "자정", 0);
        Time("korean", "내일 오전 9시", 33);
        Time("korean", "저녁 여섯 시", 18);

        Time("chinese", "明天早上", 32);
        Time("chinese", "下午6点", 18);
        Time("chinese", "两小时后", 12);
        Time("chinese", "今晚", 22);
        Time("chinese", "晚上8点", 20);
        Time("chinese", "明天上午9點", 33);
        Time("chinese", "中午", 12);

        Time("albanian", "nesër në mëngjes", 32);
        Time("albanian", "nesër nadje", 32);
        Time("albanian", "sonte", 22);
        Time("albanian", "pas dy orësh", 12);
        Time("albanian", "mbas dy sahatësh", 12);   // Gheg: "sahat" for the hour
        Time("albanian", "në orën 6 të mbrëmjes", 18);
        Time("albanian", "n'mramje", 18);
        Time("albanian", "në mesnatë", 0);
        Time("albanian", "nesër në orën 9", 33);
        Time("albanian", "në agim", 5);

        // English goes through every pack untouched: the packs in force never spoil the language the code speaks.
        for (const auto& p : gPacks) {
            for (const char* english : {"tomorrow morning", "at 6 pm", "in 2 hours", "tonight", "at dawn", "in three days at 13 hours"}) {
                const std::string through = TimeToEnglish(FoldUtf8(english), {&p.time});
                Require(std::fabs(NaturalTime::Parse(through, 10.0f) - NaturalTime::Parse(english, 10.0f)) < 0.01f,
                        p.file + " changes the English \"" + english + "\" into \"" + through + "\"");
            }
        }

        // ---- a word of place is English when it is all that was said; a name stays a name
        Require(Place("russian", "домой") == "home" && Place("russian", "наружу") == "outside" && Place("russian", "наверх") == "upstairs", "Russian: home, outside, upstairs");
        Require(Place("russian", "Дом тёплых ветров").empty() && Place("russian", "Гарцующая кобыла").empty(), "a house's name is a name");
        Require(Place("german", "nach Hause") == "home" && Place("german", "nach draußen") == "outside" && Place("german", "in den Keller") == "cellar", "German");
        Require(Place("german", "Haus der Wärmenden Winde").empty() && Place("german", "Die Beflaggte Mähre").empty(), "German names stay");
        Require(Place("japanese", "家") == "home" && Place("japanese", "外へ") == "outside" && Place("japanese", "二階") == "upstairs", "Japanese");
        Require(Place("japanese", "ブリーズホーム").empty(), "a Japanese name stays");
        Require(Place("korean", "집으로") == "home" && Place("korean", "밖으로") == "outside", "Korean");
        Require(Place("chinese", "回家") == "home" && Place("chinese", "外面") == "outside" && Place("chinese", "樓上") == "upstairs", "Chinese, both writings");
        Require(Place("albanian", "në shtëpi") == "home" && Place("albanian", "n'shpi") == "home" && Place("albanian", "jashtë") == "outside" &&
                    Place("albanian", "te shtëpia jote") == "your home",
                "Albanian, standard and Gheg");
        Require(Place("albanian", "Bannered Mare").empty() && Place("albanian", "te Bannered Mare").empty() && Place("albanian", "Whiterunit").empty(),
                "an English name in Albanian speech stays a name");
        Require(Place("french", "chez moi") == "home" && Place("spanish", "a casa") == "home" && Place("italian", "a casa") == "home" && Place("polish", "do domu") == "home",
                "French, Spanish, Italian, Polish");

        // ---- none
        Require(IsNone("", {}) && IsNone("none", {}) && IsNone(" None ", {}) == false, "empty and none (already folded by the caller)");
        Require(IsNone(FoldUtf8("Нет"), {&PackOf("russian").none}) && IsNone(FoldUtf8("никто"), {&PackOf("russian").none}), "Russian none");
        Require(IsNone(FoldUtf8("keine"), {&PackOf("german").none}) && IsNone(FoldUtf8("Niemand"), {&PackOf("german").none}), "German none");
        Require(IsNone(FoldUtf8("なし"), {&PackOf("japanese").none}) && IsNone(FoldUtf8("없음"), {&PackOf("korean").none}) && IsNone(FoldUtf8("沒有"), {&PackOf("chinese").none}),
                "Japanese, Korean, Chinese none");
        Require(IsNone(FoldUtf8("asnjë"), {&PackOf("albanian").none}) && IsNone(FoldUtf8("Askush"), {&PackOf("albanian").none}), "Albanian none");
        Require(!IsNone(FoldUtf8("нет времени ждать"), {&PackOf("russian").none}) && !IsNone(FoldUtf8("Lydia"), {&PackOf("german").none}),
                "a sentence that begins with no, and a name, are not none");

        // ---- labels: found under the names, in another word order, the most particular first
        {
            const Pack p = ReadPack("meta\tcode\txx\nmeta\tname\tTest\nmeta\tscript\tlatin\n"
                                    "label\t{1} headed off to {2}.\t{2}: dorthin ist {1} aufgebrochen.\n"
                                    "label\t{1} headed off.\t{1} ist aufgebrochen.\n"
                                    "label\tHand it over\tÜbergeben\n"
                                    "label\t{1} wants to speak with you\t{1} möchte mit Euch sprechen\n",
                                    "test", FoldUtf8);
            Require(p.bad == 0 && p.labels.size() == 4, "four labels read");
            Require(Translate(p.labels, "Lydia headed off to The Bannered Mare.").value_or("") == "The Bannered Mare: dorthin ist Lydia aufgebrochen.", "two names, the other way round");
            Require(Translate(p.labels, "Jon Battle-Born headed off.").value_or("") == "Jon Battle-Born ist aufgebrochen.", "the shorter pattern for the shorter sentence");
            Require(Translate(p.labels, "Hand it over").value_or("") == "Übergeben" && !Translate(p.labels, "Hand it over now"), "a fixed text is itself or nothing");
            Require(!Translate(p.labels, "Something nobody translated"), "what no label knows stays English");
            Require(Translate(p.labels, "Лидия wants to speak with you").value_or("") == "Лидия möchte mit Euch sprechen", "a name in another script comes through");
            const Pack bad = ReadPack("label\t{1}{2} touch\tx\nlabel\t{1} asks\t{2} fragt\nnonsense\n", "bad", FoldUtf8);
            Require(bad.bad == 3 && bad.labels.empty(), "holes that touch, a hole the English has not, and a line without tabs are refused");
        }

        // ---- which language is this game in
        {
            Require(TextPack(gPacks, "auto", "RUSSIAN", "cyrillic") == &PackOf("russian"), "the game's own word");
            Require(TextPack(gPacks, "auto", "ENGLISH", "latin") == nullptr, "an English game is English");
            Require(TextPack(gPacks, "auto", "ENGLISH", "hangul") == &PackOf("korean"), "a Korean translation run as English is told by its names");
            Require(TextPack(gPacks, "auto", "ENGLISH", "cyrillic") == &PackOf("russian") && TextPack(gPacks, "auto", "ENGLISH", "kana") == &PackOf("japanese") &&
                        TextPack(gPacks, "auto", "ENGLISH", "han") == &PackOf("chinese"),
                    "... and so are the others");
            Require(TextPack(gPacks, "auto", "GERMAN", "latin") == &PackOf("german"), "German by the setting");
            Require(TextPack(gPacks, "german", "ENGLISH", "latin") == &PackOf("german") && TextPack(gPacks, "de", "RUSSIAN", "cyrillic") == &PackOf("german"), "chosen by hand, by name or code");
            Require(TextPack(gPacks, "english", "RUSSIAN", "cyrillic") == nullptr, "English chosen by hand");
            Require(TextPack(gPacks, "auto", "", "latin") == nullptr && TextPack(gPacks, "klingon", "ENGLISH", "latin") == nullptr, "nothing known: English");

            ScriptCount count;
            for (const char* name : {"Вайтран", "Ривервуд", "Гарцующая кобыла", "Whiterun Stables"}) count.Add(name);
            Require(std::string(count.Script()) == "cyrillic", "mostly Cyrillic names");
            ScriptCount ja;
            for (const char* name : {"ホワイトラン", "竜の橋", "ソリチュード", "白河の見張り台"}) ja.Add(name);
            Require(std::string(ja.Script()) == "kana", "Japanese by its kana, among Han");
            ScriptCount zh;
            for (const char* name : {"白漫城", "河木镇", "雪漫"}) zh.Add(name);
            Require(std::string(zh.Script()) == "han", "Chinese");
            ScriptCount ko;
            for (const char* name : {"화이트런", "리버우드", "Sleeping Giant Inn"}) ko.Add(name);
            Require(std::string(ko.Script()) == "hangul", "Korean among a few untranslated names");
            ScriptCount en;
            for (const char* name : {"Whiterun", "Mähre", "Rivière"}) en.Add(name);
            Require(std::string(en.Script()) == "latin", "Latin with accents is Latin");
        }

        // ---- the languages people speak (SkyrimNet's translator)
        {
            const std::string yaml = "enabled: true\npluginFilterMode: blacklist\nrules:\n  - id: global_rule\n    entityType: global\n    language:\n"
                                     "      spokenLanguage: aln\n      subtitleLanguage: game\n    player:\n      language: aln\n";
            const auto codes = SpokenCodesOfTranslator(yaml);
            Require(codes.size() == 1 && codes[0] == "aln", "the Albanian profile's translator says aln");
            const auto spoken = SpokenPacks(gPacks, codes);
            Require(spoken.size() == 1 && spoken[0] == &PackOf("albanian"), "aln is the Albanian pack");
            Require(SpokenCodesOfTranslator("enabled: false\nrules:\n  - language:\n      spokenLanguage: ru\n").empty(), "a translator switched off says nothing");
            Require(SpokenPacks(gPacks, {"auto", "game", "en", "Russian", "ko"}).size() == 2, "names and codes from the setting; auto, game and English are no pack");
        }
    } catch (const std::exception& e) {
        std::cerr << "LanguagePackCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "LanguagePackCheck passed (" << gPacks.size() << " packs)\n";
    return 0;
}
