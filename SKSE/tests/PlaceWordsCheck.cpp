// Standalone C++20 check; no game ABI or test framework. This file is UTF-8 (compile with /utf-8 under MSVC):
// cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /I SKSE/src SKSE/tests/PlaceWordsCheck.cpp
//
// Words of place as data (PlaceWords.h). Two things are held to:
//   1. English lost nothing: the ladder of English lists the resolver had (PlaceWordsOldLadder.h, lifted from the
//      code as it was) and the rows of data are asked the same few thousand things; whatever the ladder understood,
//      the rows understand the same way, and the names it left alone are still left alone.
//   2. The same rule reads German, French, Polish and Russian from rows like a language pack's, with and without
//      the marks on their letters; and tells a cellar door by its name in those languages.
#include "PlaceWords.h"
#include "PlaceWordsOldLadder.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace IntelEngine;
using PlaceWords::Intent;
using PlaceWords::Kind;
using PlaceWords::Owner;

static void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static const NameForms::FreeWord kEnglishFree = [](std::u32string_view w) { return w == U"the" || w == U"a" || w == U"an"; };

// The ladder's answer in the shape of the rows' answer.
static Intent Old(const std::string& said) {
    const OldLadder::SemanticIntent o = OldLadder::DetectSemanticIntent(said);
    Intent i;
    using T = OldLadder::SemanticIntent;
    switch (o.type) {
        case T::NONE: return i;
        case T::UPSTAIRS: i.kind = Kind::Upstairs; break;
        case T::DOWNSTAIRS: i.kind = Kind::Downstairs; break;
        case T::OUTSIDE: i.kind = Kind::Outside; break;
        case T::INSIDE: i.kind = Kind::Inside; break;
        case T::BACK: i.kind = Kind::Back; break;
        case T::CELLAR: i.kind = Kind::Cellar; break;
        case T::BEDROOM: i.kind = Kind::Bedroom; break;
        case T::KITCHEN: i.kind = Kind::Kitchen; break;
        case T::HOME: i.kind = Kind::Home; break;
        case T::WATER: i.kind = Kind::Water; break;
    }
    i.context = o.locationContext;
    i.preferBeds = o.preferBeds;
    i.preferInterior = o.preferInterior;
    i.ownerName = o.homeOwnerHint;
    i.owner = o.homeOwner == OldLadder::HomeOwner::NPC ? Owner::Self
            : o.homeOwner == OldLadder::HomeOwner::PLAYER ? Owner::Player
            : o.homeOwner == OldLadder::HomeOwner::NAMED ? Owner::Named
            : Owner::None;
    return i;
}

static std::string Show(const Intent& i) {
    return "kind " + std::to_string(static_cast<int>(i.kind)) + " context '" + i.context + "' beds " + (i.preferBeds ? "1" : "0") + " owner " +
           std::to_string(static_cast<int>(i.owner)) + " '" + i.ownerName + "' interior " + (i.preferInterior ? "1" : "0");
}

// A pack from rows written here, folded the way the game folds them (plain letters).
static Language::Pack Pack(const char* name, const std::string& rows) {
    return Language::ReadPack(rows, name, [](const std::string& s) { return LetterFold::Plain(s); });
}
static Intent Say(const std::string& said, const PlaceWords::Packs& packs, const NameForms::FreeWord& free) {
    return PlaceWords::Read(LetterFold::Plain(said), packs, free);
}
static NameForms::FreeWord FreeOf(const Language::Pack& pack) {
    return [&pack](std::u32string_view w) {
        if (w == U"the" || w == U"a" || w == U"an") return true;
        for (const auto& f : pack.free) {
            if (std::u32string_view(f) == w) return true;
        }
        return false;
    };
}

int main() {
    try {
        const PlaceWords::Packs english{&PlaceWords::English()};
        Require(PlaceWords::English().bad == 0 && PlaceWords::English().place.size() > 60, "English's own rows are all understood");

        // ---- 1. English against the ladder it replaces --------------------------------------------------------
        {
            // every form English knows, alone and among the words people put round it
            std::vector<std::string> forms;
            for (const auto& w : PlaceWords::English().place) forms.push_back(w.form);
            for (const auto& entry : PlaceWords::English().more) {
                if (entry.first == "phrase" || entry.first == "part" || entry.first == "word" || entry.first == "bed" || entry.first == "room") {
                    for (const auto& w : entry.second) forms.push_back(w.form);
                }
            }
            const std::vector<std::string> before = {"", "go ", "go to ", "go to the ", "head ", "the ", "to the ", "let's go ", "run "};
            const std::vector<std::string> after = {"", " now", " please", " bedroom", " and wait", " of the inn"};
            std::vector<std::string> asked;
            for (const auto& form : forms) {
                for (const auto& b : before) {
                    // "the the player's home" is said by nobody (the ladder made "the the player" an owner of it)
                    if (b.size() >= 4 && b.compare(b.size() - 4, 4, "the ") == 0 && form.rfind("the ", 0) == 0) continue;
                    for (const auto& a : after) asked.push_back(b + form + a);
                }
            }
            for (const char* more :
                 {"alvor's home", "alvor's house", "camilla valerius's place", "camilla valerius home", "camilla valerius house", "go to alvor's place",
                  "head to the jarl's house", "travel to sigrid's home", "get to lucan's place", "belethor house", "back home", "go home", "my home",
                  "head home", "head home now", "your house", "the player's home", "player's home", "out of helgen", "outside of whiterun",
                  "outside the inn", "outside here", "out of here", "out of this place", "outside this building", "leave riverwood", "leave here",
                  "leave this place", "leave", "get out", "get out of here", "away", "upstairs bedroom", "my room upstairs", "the upstairs room",
                  "downstairs to my bed", "take a bath", "wash yourself in the river", "go to the river and swim", "go swimming", "go fishing",
                  "find a river", "head to the lake", "the hot springs", "upper floor", "the stairs", "below stairs", "the back room", "the shore"}) {
                asked.push_back(more);
            }
            const std::vector<std::string> names = {"white river watch", "riverwood", "breezehome", "the bannered mare", "dragonsreach", "honeyside",
                                                    "whiterun", "the drunken huntsman", "bleak falls barrow", "jorrvaskr", "warmaiden's", "solitude",
                                                    "high hrothgar", "the winking skeever", "candlehearth hall", "belethor's general goods",
                                                    "arcadia's cauldron", "the ragged flagon", "sleeping giant inn", "dead man's drink"};
            std::size_t same = 0, more = 0;
            for (const auto& said : asked) {
                const Intent was = Old(said);
                const Intent now = PlaceWords::Read(said, english, kEnglishFree);
                if (was.kind == Kind::None) {
                    if (now.kind != Kind::None) ++more;   // the rows may understand more, never less
                    continue;
                }
                // The ladder made an owner of words that are nobody's name ("the your house": "the your"); the rows
                // read the home that was meant. Counted as understood better, not as a difference.
                if (was.owner == Owner::Named && now.kind == Kind::Home && now.owner != Owner::Named) {
                    bool nobody = true;
                    std::size_t at = 0;
                    while (at <= was.ownerName.size()) {
                        const std::size_t end = std::min(was.ownerName.find(' ', at), was.ownerName.size());
                        const std::string word = was.ownerName.substr(at, end - at);
                        bool little = false;
                        for (const auto& entry : PlaceWords::English().more) {
                            if (entry.first != "owner") continue;
                            for (const auto& w : entry.second) little = little || (w.key == "not" && w.form == word);
                        }
                        nobody = nobody && little;
                        at = end + 1;
                    }
                    if (nobody) {
                        ++more;
                        continue;
                    }
                }
                // A cellar is the way down without a bed (LocationResolver::ResolveCellar): "the below stairs" was
                // read as down by the ladder and is the cellar now, which is the same trip.
                const auto down = [](Intent i) {
                    if (i.kind == Kind::Cellar) i.kind = Kind::Downstairs;
                    return i;
                };
                Require(down(was) == down(now), "English: '" + said + "' was " + Show(was) + " and is " + Show(now));
                ++same;
            }
            // Names are read as the ladder read them: left alone, or (a name with "river" in it) read as a word of
            // place, which the resolver guards against by asking first whether it is a place's own name
            // (LocationResolver::IsExactPlaceName).
            std::size_t alone = 0;
            for (const auto& name : names) {
                const Intent was = Old(name), now = PlaceWords::Read(name, english, kEnglishFree);
                Require(was == now, "the name '" + name + "' was " + Show(was) + " and is " + Show(now));
                if (now.kind == Kind::None) ++alone;
            }
            Require(alone + 2 >= names.size(), "names are left alone but for those with a word of place in them");
            Require(same > 2000, "the ladder understood a few thousand of what was asked: " + std::to_string(same));
            std::cout << "English: " << asked.size() << " asked, " << same << " understood as before, " << more << " understood now that were not\n";
        }

        // ---- plain words, and what stands behind a word of leaving ---------------------------------------------
        Require(PlaceWords::Plain("the kitchen", english, kEnglishFree) && PlaceWords::Plain("upstairs", english, kEnglishFree) &&
                    PlaceWords::Plain("the bar", english, kEnglishFree) && PlaceWords::Plain("home", english, kEnglishFree),
                "plain words of place and of a spot in a room");
        Require(!PlaceWords::Plain("kitchen knife", english, kEnglishFree) && !PlaceWords::Plain("white river watch", english, kEnglishFree) &&
                    !PlaceWords::Plain("go upstairs", english, kEnglishFree),
                "a word among others is not a plain word");
        Require(PlaceWords::Behind("out of helgen", english) == "helgen" && PlaceWords::Behind("go to riverwood", english) == "riverwood" &&
                    PlaceWords::Behind("leave here", english).empty() && PlaceWords::Behind("riverwood", english).empty(),
                "the place behind a word of leaving or going");

        // ---- names of things, English ---------------------------------------------------------------------------
        Require(PlaceWords::NameSays("honeyside cellar", "downstairs", english) && PlaceWords::NameSays("the resting pilgrim upstairs", "upstairs", english) &&
                    !PlaceWords::NameSays("the bannered mare", "downstairs", english) && !PlaceWords::NameSays("the bannered mare", "upstairs", english),
                "a cellar and an upper floor by their names");
        Require(PlaceWords::NameSays("commonbed01", "bed", english) && PlaceWords::NameSays("craftingcookingpot", "cooking", english) &&
                    PlaceWords::NameSays("riften docks", "water", english) && PlaceWords::NameSays("bathing pool", "bath", english),
                "a bed, a cooking pot, a dock and a bath by their names");

        // ---- 2. German ------------------------------------------------------------------------------------------
        {
            // the rows the German pack ships, and what a translator may add (owner, name)
            const Language::Pack german = Pack("german",
                "meta\tcode\tde\nmeta\tname\tGerman\n"
                "place\thome\tnach hause|nachhause|zuhause|zu hause|heim|mein zuhause|mein haus\n"
                "place\tyour home\tzu dir nach hause|dein zuhause|dein haus\n"
                "place\toutside\tnach draußen|draußen|hinaus|raus|ins freie\n"
                "place\tupstairs\tnach oben|oben|ins obergeschoss\n"
                "place\tdownstairs\tnach unten|unten\n"
                "place\tcellar\tin den keller|keller\n"
                "place\tbedroom\tins schlafzimmer|schlafzimmer|mein zimmer\n"
                "place\tkitchen\tin die küche|küche\n"
                "place\tbath\tins bad|bad|badehaus\n"
                "place\triver\tzum fluss|fluss\n"
                "free\t*\tder|die|das|den|dem|zu|zur|zum|in|im|ins|nach|von|vom|aus\n"
                "owner\thouse\thaus|zuhause|heim\n"
                "leave\tfrom\traus aus|hinaus aus|verlasse\n");
            Require(german.bad == 0, "the German rows are understood");
            const PlaceWords::Packs packs{&german, &PlaceWords::English()};
            const auto free = FreeOf(german);
            Require(Say("nach oben", packs, free).kind == Kind::Upstairs && Say("Nach Hause", packs, free).owner == Owner::Self, "nach oben, nach Hause");
            Require(Say("nach draußen", packs, free).kind == Kind::Outside && Say("nach draussen", packs, free).kind == Kind::Outside &&
                        Say("NACH DRAUSSEN", packs, free).kind == Kind::Outside,
                    "draußen with a sharp s, with two s and in capitals is one word");
            Require(Say("in die Küche", packs, free).kind == Kind::Kitchen && Say("in die Kuche", packs, free).kind == Kind::Kitchen, "Küche, with and without its umlaut");
            Require(Say("in den Keller", packs, free).kind == Kind::Cellar && Say("zum Fluss", packs, free).kind == Kind::Water, "Keller, Fluss");
            const Intent up = Say("nach oben ins Schlafzimmer", packs, free);
            Require(up.kind == Kind::Upstairs && up.preferBeds, "nach oben ins Schlafzimmer: upstairs, to a bed");
            const Intent alvor = Say("Alvors Haus", packs, free);
            Require(alvor.kind == Kind::Home && alvor.owner == Owner::Named && alvor.ownerName == "alvors", "Alvors Haus is somebody's home");
            Require(NameForms::Stands(NameForms::Read("alvor"), NameForms::Read(alvor.ownerName), free).stand == NameForms::Forms, "... Alvor's, by his name with its ending");
            const Intent leave = Say("raus aus Weißlauf", packs, free);
            Require(leave.kind == Kind::Outside && leave.context == "weisslauf", "raus aus Weißlauf: out of there (in plain letters, as names are compared)");
            Require(Say("Weißlauf", packs, free).kind == Kind::None && Say("Haus der Kriegerjungfer", packs, free).kind == Kind::None &&
                        Say("Brisenheim", packs, free).kind == Kind::None,
                    "German names stay names (Brisenheim ends in the word for home)");
            Require(Say("upstairs", packs, free).kind == Kind::Upstairs && Say("Alvor's house", packs, free).ownerName == "alvor", "English is still read beside it");
            Require(PlaceWords::NameSays(LetterFold::Plain("Brisenheim Keller"), "downstairs", packs) &&
                        PlaceWords::NameSays(LetterFold::Plain("Weinkeller"), "downstairs", packs) &&
                        !PlaceWords::NameSays(LetterFold::Plain("Die Beflaggte Mähre"), "downstairs", packs),
                    "a German cellar by its name, from the pack's own word");
            Require(PlaceWords::Plain(LetterFold::Plain("die Küche"), packs, free) && !PlaceWords::Plain(LetterFold::Plain("Küchenmesser"), packs, free), "plain German words");
        }

        // ---- French ---------------------------------------------------------------------------------------------
        {
            const Language::Pack french = Pack("french",
                "meta\tcode\tfr\nmeta\tname\tFrench\n"
                "place\thome\tà la maison|chez moi|ma maison\n"
                "place\toutside\tdehors|à l'extérieur|l'extérieur\n"
                "place\tupstairs\ten haut|à l'étage|l'étage\n"
                "place\tcellar\tà la cave|la cave|cave|au sous-sol\n"
                "place\tkitchen\tla cuisine|à la cuisine|cuisine\n"
                "free\t*\tle|la|les|l|un|une|des|du|de|d|à|au|aux|en|chez|vers|dans\n"
                "owner\tfirst\tla maison de|la maison d'|maison de|chez\n");
            Require(french.bad == 0, "the French rows are understood");
            const PlaceWords::Packs packs{&french, &PlaceWords::English()};
            const auto free = FreeOf(french);
            Require(Say("à l'étage", packs, free).kind == Kind::Upstairs && Say("a l'etage", packs, free).kind == Kind::Upstairs &&
                        Say("À L'ÉTAGE", packs, free).kind == Kind::Upstairs,
                    "à l'étage with its accents, without them, in capitals");
            Require(Say("dehors", packs, free).kind == Kind::Outside && Say("chez moi", packs, free).owner == Owner::Self &&
                        Say("à la cave", packs, free).kind == Kind::Cellar,
                    "dehors, chez moi, à la cave");
            const Intent home = Say("la maison de Gerdur", packs, free);
            Require(home.kind == Kind::Home && home.owner == Owner::Named && home.ownerName == "gerdur", "la maison de Gerdur: the word for the house stands first");
            Require(Say("Épervine", packs, free).kind == Kind::None && Say("Blancherive", packs, free).kind == Kind::None, "French names stay names");
            Require(PlaceWords::NameSays(LetterFold::Plain("Cave de la Jument pavoisée"), "downstairs", packs), "a French cellar by its name");
        }

        // ---- Polish ---------------------------------------------------------------------------------------------
        {
            const Language::Pack polish = Pack("polish",
                "meta\tcode\tpl\nmeta\tname\tPolish\nmeta\tcodepage\t1250\n"
                "place\thome\tdo domu|w domu|mój dom|do siebie\n"
                "place\toutside\tna zewnątrz|na dwór|zewnątrz\n"
                "place\tupstairs\tna górę|na górze|piętro wyżej\n"
                "place\tcellar\tdo piwnicy|piwnica|piwnicy\n"
                "place\tbath\tdo łaźni|łaźnia|kąpiel\n"
                "free\t*\tdo|w|we|na|u|z|ze|od|po|za|przy|nad|pod\n"
                "owner\tfirst\tdom|do domu\n");
            Require(polish.bad == 0 && polish.codepage == 1250, "the Polish rows are understood, the code page among them");
            const PlaceWords::Packs packs{&polish, &PlaceWords::English()};
            const auto free = FreeOf(polish);
            Require(Say("na górę", packs, free).kind == Kind::Upstairs && Say("na gore", packs, free).kind == Kind::Upstairs, "na górę, typed with and without its marks");
            Require(Say("do domu", packs, free).owner == Owner::Self && Say("na zewnątrz", packs, free).kind == Kind::Outside &&
                        Say("Do Łaźni", packs, free).preferInterior,
                    "do domu, na zewnątrz, do łaźni");
            const Intent home = Say("dom Alvora", packs, free);
            Require(home.kind == Kind::Home && home.owner == Owner::Named && home.ownerName == "alvora", "dom Alvora");
            Require(NameForms::Stands(NameForms::Read("alvor"), NameForms::Read(home.ownerName), free).stand == NameForms::Forms, "... Alvor's, in the genitive");
            Require(Say("Biała Grań", packs, free).kind == Kind::None && Say("Samotnia", packs, free).kind == Kind::None, "Polish names stay names");
            Require(PlaceWords::NameSays(LetterFold::Plain("Piwnica Wietrznego Domu"), "downstairs", packs), "a Polish cellar by its name");
        }

        // ---- Russian --------------------------------------------------------------------------------------------
        {
            const Language::Pack russian = Pack("russian",
                "meta\tcode\tru\nmeta\tname\tRussian\n"
                "place\thome\tдомой|к себе домой|к себе|мой дом\n"
                "place\toutside\tнаружу|снаружи|на улицу\n"
                "place\tupstairs\tнаверх|наверху|на второй этаж\n"
                "place\tdownstairs\tвниз|внизу\n"
                "place\tcellar\tв подвал|подвал|погреб\n"
                "place\tbedroom\tв спальню|спальня|спальню\n"
                "free\t*\tк|ко|в|во|на|у|из|до|от|с|со|за|по\n"
                "owner\tfirst\tдом|в дом|к дому\n"
                "leave\tfrom\tпрочь из|вон из\n");
            Require(russian.bad == 0, "the Russian rows are understood");
            const PlaceWords::Packs packs{&russian, &PlaceWords::English()};
            const auto free = FreeOf(russian);
            Require(Say("домой", packs, free).owner == Owner::Self && Say("Домой", packs, free).kind == Kind::Home && Say("НАВЕРХ", packs, free).kind == Kind::Upstairs,
                    "домой, наверх, in any case of letters");
            Require(Say("в подвал", packs, free).kind == Kind::Cellar && Say("на улицу", packs, free).kind == Kind::Outside, "в подвал, на улицу");
            const Intent up = Say("наверх в спальню", packs, free);
            Require(up.kind == Kind::Upstairs && up.preferBeds, "наверх в спальню: upstairs, to a bed");
            const Intent home = Say("дом Алвора", packs, free);
            Require(home.kind == Kind::Home && home.owner == Owner::Named && home.ownerName == "алвора", "дом Алвора");
            Require(NameForms::Stands(NameForms::Read("алвор"), NameForms::Read(home.ownerName), free).stand == NameForms::Forms, "... Alvor's, in the genitive");
            const Intent leave = Say("прочь из Вайтрана", packs, free);
            // (in plain letters, as names are compared: the short i without its breve)
            Require(leave.kind == Kind::Outside && leave.context == LetterFold::Plain("вайтрана") && leave.context == "ваитрана", "прочь из Вайтрана");
            Require(Say("Вайтран", packs, free).kind == Kind::None && Say("Ветреный пик", packs, free).kind == Kind::None, "Russian names stay names");
            Require(PlaceWords::NameSays(LetterFold::Plain("Подвал Йоррваскра"), "downstairs", packs) &&
                        !PlaceWords::NameSays(LetterFold::Plain("Гарцующая кобыла"), "downstairs", packs),
                    "a Russian cellar by its name");
        }
    } catch (const std::exception& e) {
        std::cerr << "PlaceWordsCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "PlaceWordsCheck passed\n";
    return 0;
}
