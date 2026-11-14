// What a faction's soldier_template names: stock names by form id, form ids as text, anything else a name.
// Build: cl /std:c++20 /EHsc /I SKSE\src SKSE\tests\SoldierTemplatesCheck.cpp
#include "SoldierTemplates.h"

#include <cassert>
#include <iostream>

int main() {
    using namespace IntelEngine::SoldierTemplates;

    // The stock templates of the default faction file, whatever their case or quotes.
    assert(StockFormId("LCharSoldierImperial") == 0x0001FC5B);
    assert(StockFormId("lcharsoldiersons") == 0x0001FC5C);
    assert(StockFormId("\"LCharThalmorMelee1H\"") == 0x0002B129);
    assert(StockFormId(" LCharBanditMeleeAny ") == kFallbackFormId);
    assert(StockFormId("LCharBanditWizard") == 0x0001E771);
    assert(StockFormId("LCharForswornMelee1H") == 0x0001E792);
    assert(StockFormId("WEThiefSubChar") == 0x00104DC9);
    assert(StockFormId("WEAssassinSubChar") == 0x001051F4);
    assert(StockFormId("LvlBanditMeleeAny") == 0);          // the old fallback's name: not a stock template
    assert(StockFormId("") == 0);

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

    std::cout << "PASS: stock templates by form id, form ids as text, names left to the editor-id lookup\n";
    return 0;
}
