// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/ItemNamesCheck.cpp
#include "ItemNames.h"

#include <cstdint>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace IntelEngine::ItemNames;

static void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static void Same_(const char* a, const char* b) {
    Require(Same(a, b) && Same(b, a), std::string("the same item: '") + a + "' and '" + b + "'");
}

static void Different(const char* a, const char* b) {
    Require(!Same(a, b) && !Same(b, a), std::string("different items: '") + a + "' and '" + b + "'");
}

static void Names() {
    // Plain, any case.
    Same_("Nord Mead", "nord mead");
    Same_("Ale", "ALE");
    // Tags of sorting mods, in front or behind, in any bracket.
    Same_("Nord Mead", "[Drink] Nord Mead");
    Same_("Nord Mead", "Nord Mead (Drink)");
    Same_("Apple", "(Fd) Apple");
    Same_("Ale", "{Drink} Ale");
    Same_("Bread", "<Food> Bread");
    Same_("Ale", "Drink - Ale");
    Same_("Apple", "Food: Apple");
    Same_("Lantern", "Misc | Lantern");
    // Words turned round.
    Same_("Iron Ingot", "Ingot - Iron");
    Same_("Iron Ingot", "Ingot: Iron");
    Same_("Wolf Pelt", "Pelt - Wolf");
    Same_("Iron Ingot", "[Ingot] Iron Ingot");
    // Spelling of the same words.
    Same_("Sweetroll", "Sweet Roll");
    Same_("Black-Briar Mead", "Black Briar Mead");
    Same_("Hunter's Bow", "Hunters Bow");
    // Never another item.
    Different("Ale", "Pale Ale");
    Different("Ale", "Vase");
    Different("Iron Sword", "Steel Sword");
    Different("Iron Ingot", "Iron Ore");
    Different("Ale", "[Drink] Pale Ale");
    Different("Potion of Healing", "Potion of Minor Healing");
    // A long phrase with a separator is not a tag.
    Different("Ale", "The Lusty Argonian Maid - Ale");
    // Names with no letters never match anything.
    Different("", "");
    Different("()", "Ale");
}

static void Vague() {
    Require(Loose("Mead", "Nord Mead") > 0, "Mead may be Nord Mead");
    Require(Loose("Mead", "Nord Mead") < Loose("Mead", "Honningbrew Mead"), "the plainer name first");
    Require(Loose("Mead", "[Drink] Nord Mead") == Loose("Mead", "Nord Mead"), "tags do not count");
    Require(Loose("Mead", "Drink - Nord Mead") == Loose("Mead", "Nord Mead"), "nor a tag in front");
    Require(Loose("Sword", "Iron Sword") > 0, "Sword may be Iron Sword");
    Require(Loose("Ale", "Vase") == 0, "never part of a word");
    Require(Loose("Mead", "Mead Barrel") == 0, "only the end of a name");
    Require(Loose("Ale", "Ale") == 0, "the same name is not a vaguer one");
    Require(Loose("Al", "Pale Al") == 0, "too short to stand for anything");
    Require(Loose("Healing", "Potion of Healing") > 0, "the last words of a name");
}

static void Shown() {
    Require(Display("[Drink] Nord Mead") == "Nord Mead", "tags left out of what is said");
    Require(Display("Nord Mead (Drink)") == "Nord Mead", "behind too");
    Require(Display("  (Fd)  Apple ") == "Apple", "and spaces tidied");
    Require(Display("Nord Mead") == "Nord Mead", "a plain name stays");
    Require(Display("Ingot - Iron") == "Ingot - Iron", "a name turned round stays as the game shows it");
    Require(Display("[]") == "[]", "nothing left: the name as it was");
}

static void Languages() {
    // Written as bytes so this file stays ASCII; the readable names are in the messages.
    Require(Same("Nordischer Met", "[Getr\xC3\xA4nk] Nordischer Met") && Same("[Getr\xC3\xA4nk] Nordischer Met", "Nordischer Met"), "the same item: German, a sorting tag");
    Require(Same("\xC3\x84pfel", "\xC3\xA4pfel") && Same("\xC3\xA4pfel", "\xC3\x84pfel"), "the same item: German capitals");
    Require(Same("Mjalt\xC3\xAB Nordik", "[Pije] Mjalt\xC3\xAB Nordik") && Same("[Pije] Mjalt\xC3\xAB Nordik", "Mjalt\xC3\xAB Nordik"), "the same item: Albanian, a tag");
    Require(Same("MJALT\xC3\x8B", "mjalt\xC3\xAB") && Same("mjalt\xC3\xAB", "MJALT\xC3\x8B"), "the same item: Albanian capitals");
    Require(Same("\xD0\x9D\xD0\xBE\xD1\x80\xD0\xB4\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 \xD0\xBC\xD1\x91\xD0\xB4", "[\xD0\x9D\xD0\xB0\xD0\xBF\xD0\xB8\xD1\x82\xD0\xBE\xD0\xBA] \xD0\x9D\xD0\xBE\xD1\x80\xD0\xB4\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 \xD0\xBC\xD1\x91\xD0\xB4") && Same("[\xD0\x9D\xD0\xB0\xD0\xBF\xD0\xB8\xD1\x82\xD0\xBE\xD0\xBA] \xD0\x9D\xD0\xBE\xD1\x80\xD0\xB4\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 \xD0\xBC\xD1\x91\xD0\xB4", "\xD0\x9D\xD0\xBE\xD1\x80\xD0\xB4\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 \xD0\xBC\xD1\x91\xD0\xB4"), "the same item: Russian, a tag");
    Require(Same("\xD0\x9D\xD0\x9E\xD0\xA0\xD0\x94\xD0\xA1\xD0\x9A\xD0\x98\xD0\x99 \xD0\x9C\xD0\x81\xD0\x94", "\xD0\xBD\xD0\xBE\xD1\x80\xD0\xB4\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 \xD0\xBC\xD1\x91\xD0\xB4") && Same("\xD0\xBD\xD0\xBE\xD1\x80\xD0\xB4\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 \xD0\xBC\xD1\x91\xD0\xB4", "\xD0\x9D\xD0\x9E\xD0\xA0\xD0\x94\xD0\xA1\xD0\x9A\xD0\x98\xD0\x99 \xD0\x9C\xD0\x81\xD0\x94"), "the same item: Russian capitals with Io");
    Require(Same("\xE8\xAF\xBA\xE5\xBE\xB7\xE8\x9C\x82\xE8\x9C\x9C\xE9\x85\x92", "\xE3\x80\x90\xE9\xA5\xAE\xE5\x93\x81\xE3\x80\x91\xE8\xAF\xBA\xE5\xBE\xB7\xE8\x9C\x82\xE8\x9C\x9C\xE9\x85\x92") && Same("\xE3\x80\x90\xE9\xA5\xAE\xE5\x93\x81\xE3\x80\x91\xE8\xAF\xBA\xE5\xBE\xB7\xE8\x9C\x82\xE8\x9C\x9C\xE9\x85\x92", "\xE8\xAF\xBA\xE5\xBE\xB7\xE8\x9C\x82\xE8\x9C\x9C\xE9\x85\x92"), "the same item: Chinese, a tag in CJK brackets");
    Require(Same("\xE8\x8B\xB9\xE6\x9E\x9C", "\xE9\xA3\x9F\xE7\x89\xA9\xEF\xBC\x9A\xE8\x8B\xB9\xE6\x9E\x9C") && Same("\xE9\xA3\x9F\xE7\x89\xA9\xEF\xBC\x9A\xE8\x8B\xB9\xE6\x9E\x9C", "\xE8\x8B\xB9\xE6\x9E\x9C"), "the same item: Chinese, a tag before a full-width colon");
    Require(Same("Nord Mead", "\xE2\x98\x85 Nord Mead") && Same("\xE2\x98\x85 Nord Mead", "Nord Mead"), "the same item: a star in front");
    Require(Same("Hunter\xE2\x80\x99s Bow", "Hunters Bow") && Same("Hunters Bow", "Hunter\xE2\x80\x99s Bow"), "the same item: a curly apostrophe");
    Require(Same("\xCE\x9C\xCE\x91\xCE\x9D\xCE\x99\xCE\xA4\xCE\x91\xCE\xA1\xCE\x99", "\xCE\xBC\xCE\xB1\xCE\xBD\xCE\xB9\xCF\x84\xCE\xB1\xCF\x81\xCE\xB9") && Same("\xCE\xBC\xCE\xB1\xCE\xBD\xCE\xB9\xCF\x84\xCE\xB1\xCF\x81\xCE\xB9", "\xCE\x9C\xCE\x91\xCE\x9D\xCE\x99\xCE\xA4\xCE\x91\xCE\xA1\xCE\x99"), "the same item: Greek capitals");
    Require(!Same("\xD0\x9C\xD1\x91\xD0\xB4", "\xD0\x9D\xD0\xBE\xD1\x80\xD0\xB4\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 \xD0\xBC\xD1\x91\xD0\xB4"), "different items: Russian: one word is not the whole name");
    Require(!Same("\xE9\x85\x92", "\xE8\xAF\xBA\xE5\xBE\xB7\xE8\x9C\x82\xE8\x9C\x9C\xE9\x85\x92"), "different items: Chinese: a character is not the whole name");
    Require(!Same("Mjalt\xC3\xAB", "Mjalt\xC3\xAB e Zez\xC3\xAB"), "different items: Albanian: not another item");
    Require(Loose("\xD0\xBC\xD1\x91\xD0\xB4", "\xD0\x9D\xD0\xBE\xD1\x80\xD0\xB4\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 \xD0\xBC\xD1\x91\xD0\xB4") > 0, "Russian: mead may be Nord mead");
    Require(Loose("\xE9\x85\x92", "\xE8\xAF\xBA\xE5\xBE\xB7\xE8\x9C\x82\xE8\x9C\x9C\xE9\x85\x92") == 0, "Chinese has no word endings to go by");
    Require(Display("\xE3\x80\x90\xE9\xA5\xAE\xE5\x93\x81\xE3\x80\x91\xE8\xAF\xBA\xE5\xBE\xB7\xE8\x9C\x82\xE8\x9C\x9C\xE9\x85\x92") == "\xE8\xAF\xBA\xE5\xBE\xB7\xE8\x9C\x82\xE8\x9C\x9C\xE9\x85\x92", "CJK tags left out of what is said");
    Require(Same("Ale\xC3\xBF", "Ale\xC3\xBF"), "letters outside ASCII compare");
    Require(Same("Ale\xFF", "Ale\xFF") && !Same("Ale\xFF", "Ale\xFE"), "bytes that are not UTF-8 compare as they are");
}

// The book built once must answer exactly as the scan over every item did (the first Same, then the plainest Loose,
// the first of equals), for names that match, names that do not, and names that only a vaguer word reaches.
static void Booked() {
    const char* items[] = {
        "Vase", "Pale Ale", "[Drink] Nord Mead", "Honningbrew Mead", "Nord Mead", "Ingot - Iron", "Iron Ingot", "Iron Ore",
        "Sweetroll", "Sweet Roll", "Drink - Ale", "Ale", "Black-Briar Mead", "Mead Barrel", "Iron Sword", "Steel Sword",
        "Potion of Healing", "Potion of Minor Healing", "()", "", "Hunter\xE2\x80\x99s Bow", "\xE2\x98\x85 Nord Mead",
        "Mjalt\xC3\xAB Nordik", "[Pije] Mjalt\xC3\xAB Nordik", "Ale\xFF", "Stale Ale", "Ale of Riften",
    };
    Book book;
    for (const char* name : items) book.Add(name);
    Require(book.Size() == std::size(items), "every item has a number");
    const char* asked[] = {"Ale", "ale", "Nord Mead", "nord mead", "Mead", "Iron Ingot", "Ingot", "Sweet Roll", "Sweetroll",
                           "Healing", "Hunters Bow", "Mjalt\xC3\xAB", "MJALT\xC3\x8B NORDIK", "Ale\xFF", "Al", "Vase",
                           "Steel", "Sword", "Potion", "Black Briar Mead", "nothing like it", "", "()"};
    for (const char* said : asked) {
        std::int64_t same = -1;
        std::vector<std::uint32_t> all;
        for (std::size_t i = 0; i < std::size(items); ++i) {
            if (!Same(said, items[i])) continue;
            if (same < 0) same = static_cast<std::int64_t>(i);
            all.push_back(static_cast<std::uint32_t>(i));
        }
        Require(book.FindSame(said) == same, std::string("the book finds the same item as the scan for '") + said + "'");
        Require(book.SameAll(said) == all, std::string("the book lists every same item for '") + said + "'");
        std::int64_t loose = -1;
        int best = 0;
        for (std::size_t i = 0; i < std::size(items); ++i) {
            const int rank = Loose(said, items[i]);
            if (rank > 0 && (best == 0 || rank < best)) {
                best = rank;
                loose = static_cast<std::int64_t>(i);
            }
        }
        Require(book.FindLoose(said) == loose, std::string("the book finds the plainest item as the scan for '") + said + "'");
    }
}

int main() {
    try {
        Names();
        Vague();
        Shown();
        Languages();
        Booked();
    } catch (const std::exception& e) {
        std::cerr << "ItemNamesCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "ItemNamesCheck passed\n";
    return 0;
}
