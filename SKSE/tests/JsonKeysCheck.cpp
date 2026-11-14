// A member name that came from a script is matched whatever its case (the game keeps one spelling per string).
// Build: cl /std:c++20 /EHsc /I SKSE\src /I <vcpkg triplet>\include SKSE\tests\JsonKeysCheck.cpp
#include "JsonKeys.h"

#include <cassert>
#include <iostream>

int main() {
    using namespace IntelEngine::JsonKeys;
    using nlohmann::json;

    // What GetBattleSoldierFormIds answers: two created references (ids starting with FF) and their count.
    const json side = json::parse(R"({"formIds":[4278196001,4278196002],"count":2,"alive":2})");

    // The spelling the battle script wrote, and the one the game handed over instead (another script's variable).
    assert(ArrayInt(side, "formIds", 0) == static_cast<int>(0xFF001721u));
    assert(ArrayInt(side, "formIDs", 0) == static_cast<int>(0xFF001721u));
    assert(ArrayInt(side, "FORMIDS", 1) == static_cast<int>(0xFF001722u));
    assert(ArrayInt(side, "formIDs", 0) < 0);                 // the same 32 bits, as a script's signed Int

    // Nothing there: 0, never a throw.
    assert(ArrayInt(side, "formIds", 2) == 0);                // past the end
    assert(ArrayInt(side, "formIds", -1) == 0);
    assert(ArrayInt(side, "formId", 0) == 0);                 // another name, not a prefix match
    assert(ArrayInt(side, "count", 0) == 0);                  // a member, but no array
    assert(ArrayInt(json::parse("[1,2]"), "formIds", 0) == 0);     // no object at all
    assert(ArrayInt(json::parse(R"({"formIds":["x",1.5,null]})"), "formIds", 0) == 0);
    assert(ArrayInt(json::parse(R"({"formIds":["x",1.5,null]})"), "formIds", 1) == 0);
    assert(ArrayInt(json::parse(R"({"formIds":[-7,20]})"), "FormIds", 0) == -7);
    assert(ArrayInt(json::parse(R"({"formIds":[-7,20]})"), "FormIds", 1) == 20);

    // Both spellings present: the exact one wins, whichever comes first in the object.
    const json both = json::parse(R"({"FormIDs":[1],"formIds":[2]})");
    assert(ArrayInt(both, "formIds", 0) == 2);
    assert(ArrayInt(both, "FormIDs", 0) == 1);

    assert(Find(side, "Alive") != nullptr && Find(side, "Alive")->get<int>() == 2);
    assert(Find(side, "dead") == nullptr);
    assert(EqualsIgnoreCase("leaderFormId", "LEADERFORMID") && !EqualsIgnoreCase("formIds", "formId"));

    std::cout << "PASS: a script's member name is found in any case, form ids above INT_MAX keep their bits\n";
    return 0;
}
