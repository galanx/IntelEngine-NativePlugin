// Standalone C++20 check; no game ABI or test framework. This file is UTF-8 (compile with /utf-8 under MSVC):
// cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /I SKSE/src SKSE/tests/NameFormsCheck.cpp
//
// A name as people say it, in the languages the mod is played in: Russian and German endings, Albanian endings in an
// English game, Korean and Japanese particles and word breaks, Chinese; and that none of it lets one name answer for
// another. Everything is given in lower case, as the lookups give it (FoldUtf8).
#include "NameForms.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace IntelEngine::NameForms;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static Verdict V(const char* name, const char* said, const FreeWord& free = {}) { return Stands(Read(name), Read(said), free); }
static bool Is(const char* name, const char* said, int stand) { return V(name, said).stand == stand; }

int main() {
    try {
        // ---- letters, not bytes
        Require(Letters("lydia") == 5 && Letters("лидия") == 5 && Letters("리디아") == 3 && Letters("リディア") == 4 && Letters("白漫城") == 3,
                "a letter is a letter in every script");
        Require(Letters("m\xE4hre") == 5, "a byte of an old code page counts as one letter");
        Require(Slips(std::string_view("лидия"), std::string_view("лидия ветреный пик"), 3) == 1 && Slips(std::string_view("리디아"), std::string_view("리사"), 3) == 0,
                "a slip of the pen is allowed by letters: a Korean name of three letters gets none, not the allowance of nine bytes");
        Require(Slips(4, 3) == 0 && Slips(5, 3) == 1 && Slips(8, 3) == 2 && Slips(12, 3) == 3 && Slips(20, 1) == 1, "the same scale as before, in letters");

        // ---- words in any script
        {
            const Words w = Read("jon battle-born");
            Require(w.at.size() == 3 && w.text == U"jonbattleborn", "a hyphen and a space separate");
            const Words j = Read("バナード・メア");
            Require(j.at.size() == 2 && j.Word(0) == U"バナード" && j.Word(1) == U"メア", "the Japanese dot between the words of a name separates");
            const Words c = Read("「白漫城」、雪漫");
            Require(c.at.size() == 2, "CJK brackets and the ideographic comma separate");
            const Words e = Read("edan's shadow");
            Require(e.at.size() == 2 && e.Word(0) == U"edan's", "an apostrophe belongs to its word");
        }

        // ---- Russian: case endings on the whole name and on a first name
        Require(Is("лидия", "лидии", Forms) && Is("лидия", "к лидии", Forms) && Is("лидия", "лидию", Forms), "Лидия: Лидии, к Лидии, Лидию");
        Require(Is("вайтран", "в вайтране", Forms) && Is("вайтран", "вайтраном", Forms) && Is("вайтран", "из вайтрана", Forms), "Вайтран: в Вайтране, Вайтраном");
        Require(Is("камилла валерия", "камилле валерии", Forms), "both words of a name declined");
        Require(Is("йон сын битвы", "к йону", PartForms) && Is("лидия ветреный пик", "лидии", PartForms), "a first name, declined, is part of the name");
        Require(Is("гарцующая кобыла", "в гарцующей кобыле", Forms), "an adjective and a noun, both in another case");
        Require(Is("ветреный пик", "ветреного пика", Forms), "a long word may lose its last two letters");
        Require(V("лидия", "лидия").stand == Same && V("лидия", "лидии").cost == 2, "the same word costs nothing, an ending costs its letters");

        // ---- German: article and ending
        Require(Is("die beflaggte mähre", "zur beflaggten mähre", Forms) && Is("die beflaggte mähre", "beflaggte mähre", Forms), "zur Beflaggten Mähre");
        Require(Is("sven", "svens", Forms) && Is("jarl balgruuf der ältere", "jarl balgruufs", PartForms), "a genitive s");
        Require(Is("weißlauf", "nach weißlauf", No), "a preposition of four letters is a word like any other, until a language pack names it");
        {
            const FreeWord german = [](std::u32string_view w) { return w == U"nach" || w == U"beim"; };
            Require(V("weißlauf", "nach weißlauf", german).stand == Forms, "... then it is let go");
        }

        // ---- Albanian speech in an English game: the article and the case hang behind an English name
        Require(Is("whiterun", "whiterunit", Forms) && Is("whiterun", "në whiterun", Forms) && Is("whiterun", "whiterunin", Forms), "Whiterunit, në Whiterun");
        Require(Is("lydia", "lydian", Forms) && Is("lydia", "lydies", Forms) && Is("camilla valerius", "camillën", PartForms), "Lydian, Lydies, Camillën");
        Require(Is("the bannered mare", "te bannered mare", Forms) && Is("the bannered mare", "bannered mare-ja", Forms), "te Bannered Mare, Bannered Mare-ja");
        Require(Is("jon battle-born", "jonit", PartForms) && Is("hulda", "huldës", Forms), "Jonit, Huldës");

        // ---- Korean: a particle behind, without a space
        Require(Is("화이트런", "화이트런으로", Forms) && Is("리디아", "리디아에게", Forms) && Is("화이트런", "화이트런", Same), "화이트런으로, 리디아에게");
        Require(Is("리디아", "리사", No) && Is("리디아", "리디", No), "another name, and half a name, are not it");
        Require(Is("깃발 달린 암말", "깃발 달린 암말에서", Forms), "the particle on the last word of several");

        // ---- Japanese: a particle behind; the dot between the words of a name left out
        Require(Is("ホワイトラン", "ホワイトランへ", Forms) && Is("リディア", "リディアに", Forms), "ホワイトランへ, リディアに");
        Require(Is("バナード・メア", "バナードメア", Same) && Is("バナード・メア", "バナードメアへ", Forms), "written without its dot, with a particle");
        Require(Is("リディア", "リディアのところへ行く", No), "a whole clause is not a name");

        // ---- Chinese: no endings; the name as it is, or with a word of place behind
        Require(Is("白漫城", "白漫城", Same) && Is("白漫城", "白漫城里", Forms) && Is("白漫城", "白河镇", No), "白漫城, 白漫城里; another town is another town");
        Require(Is("龙", "龙霄宫", No), "a name of one letter stands only for itself");

        // ---- never one name for another (the reports NameMatch was written for, and their kin)
        Require(Is("gollgarth", "garth", No) && Is("kaitsun the bold", "tsun", No) && Is("edan's shadow", "edan", No), "Garth, Tsun, Edan");
        Require(Is("anna", "annekke", No) && Is("annekke crag-jumper", "anna", No) && Is("maven black-briar", "mavis", No), "Anna is not Annekke, Mavis is not Maven");
        Require(Is("jon battle-born", "jonas the bard", No) && Is("jon battle-born", "olfrid battle-born", No), "a real word that is no part of the name makes it another name");
        Require(Is("lydia", "lynly", No) && Is("aela the huntress", "aerin", No) && Is("riverwood", "riften", No), "two letters alike are no name");
        Require(Is("victoria vici", "ria", No) && Is("hod", "hodit", Forms) && Is("hod", "hodges", No), "part of a word never; a short name takes two letters at most");
        {
            // "Jon" is a name of three letters and "the" is not: by length they are alike, so the articles come from
            // the language pack (English ones are always in force).
            const FreeWord english = [](std::u32string_view w) { return w == U"the" || w == U"a" || w == U"an"; };
            Require(V("the pale", "the", english).stand == No && V("the reach", "a the", english).stand == No, "an article alone stands for nothing");
            Require(V("the pale", "pale", english).stand == Forms && V("the bannered mare", "bannered mare", english).stand == Forms, "the name without its article is the whole name");
            Require(V("jon battle-born", "jon", english).stand == PartForms, "a first name of three letters is still a name");
        }
        Require(Is("riverwood trader", "the trader", PartForms) && Is("riverwood trader", "trader", PartForms), "part of a name, as whole words");

        // ---- the best of many
        {
            const std::vector<std::string> names = {"лидия", "лилия", "йон сын битвы", "вайтран", "вайтранская конюшня"};
            auto b = Find("к лидии", names);
            Require(b && b.index == 0 && b.stand == Forms, "Лидии is Лидия, not Лилия");
            b = Find("в вайтране", names);
            Require(b && b.index == 3, "the whole name before a part of a longer one");
            b = Find("йону", names, PartForms);
            Require(b && b.index == 2 && b.stand == PartForms, "a first name when part names are asked for");
            b = Find("йону", names);
            Require(!b, "... and nobody when they are not");
            const std::vector<std::string> twins = {"анна", "анни"};
            b = Find("анну", twins);
            Require(b.index >= 0 && b.tied && !b, "two names that stand equally well: nobody's");
            const std::vector<std::string> koreans = {"화이트런", "리버우드", "리디아"};
            b = Find("리버우드로", koreans);
            Require(b && b.index == 1, "리버우드로");
        }

        // ---- does a sentence speak of someone
        Require(Mentions("лидию видели у ворот вайтрана", "лидия") && Mentions("svens laute wurde gestohlen", "sven"), "Лидию, Svens");
        Require(Mentions("camillën e panë te lumi", "camilla valerius") == false && Mentions("camillën valerius e panë te lumi", "camilla valerius"),
                "every real word of the name must be there");
        Require(Mentions("thonë se lydian e panë me një të huaj", "lydia") && !Mentions("victoria was seen at the docks", "ria") &&
                    !Mentions("they met in riften", "ria"),
                "Lydian; never Ria inside Victoria or Riften");
        Require(Mentions("リディアが門で目撃された", "リディア") && Mentions("리디아가 성문에서 목격되었다", "리디아") && !Mentions("リサが来た", "リディア"),
                "in a script without spaces the name is found where it stands");
        Require(Mentions("jon battle-born was seen leaving", "jon battle-born") && !Mentions("olfrid was seen leaving", "jon battle-born"), "English as before");
        Require(Mentions("mikael mocked sinmir's views on the city's rot", "sinmir") && Mentions("they laughed at jon battle-born’s song", "jon battle-born") &&
                    Mentions("it was jonas' doing", "jonas") && !Mentions("the city's rot", "cit"),
                "a possessive speaks of its owner, with either apostrophe");
        Require(V("edan's shadow", "edan").stand == No, "... while in a lookup Edan is still not Edan's Shadow");
    } catch (const std::exception& e) {
        std::cerr << "NameFormsCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "NameFormsCheck passed\n";
    return 0;
}
