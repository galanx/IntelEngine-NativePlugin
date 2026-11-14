// What a faction's soldier_template names: a form id as text, anything else a name for the shared lookup.
// Build: cl /std:c++20 /EHsc /I SKSE\src SKSE\tests\SoldierTemplatesCheck.cpp
#include "SoldierTemplates.h"

#include <cassert>
#include <iostream>

int main() {
    using namespace IntelEngine::SoldierTemplates;

    assert(kFallbackFormId == 0x0003DECD);

    // A template written as a form id.
    Ref ref;
    assert(ParseRef("0x0001FC5B", ref) && ref.plugin.empty() && ref.formId == 0x0001FC5B);
    assert(ParseRef("MyMod.esp|0x000801", ref) && ref.plugin == "MyMod.esp" && ref.formId == 0x801);
    assert(ParseRef(" My Mod.esp | 801 ", ref) && ref.plugin == "My Mod.esp" && ref.formId == 0x801);
    assert(ParseRef("\"0X1fc5b\"", ref) && ref.formId == 0x1FC5B);

    // Names stay names: they are looked up as editor ids.
    assert(!ParseRef("LCharSoldierImperial", ref));
    assert(!ParseRef("DEAD", ref));                         // hex letters only, but no 0x and no plugin
    assert(!ParseRef("0x", ref) && !ParseRef("0xZZ", ref) && !ParseRef("0x123456789", ref));
    assert(!ParseRef("|0x801", ref) && !ParseRef("MyMod.esp|", ref) && !ParseRef("MyMod.esp|0", ref));
    assert(!ParseRef("", ref));

    std::cout << "PASS: form ids as text are told from names, names are left to the shared lookup\n";
    return 0;
}
