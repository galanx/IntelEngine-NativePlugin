// IntelEngine: the faction editor's words (SKSE/src/FactionEditorText.h): what it refuses, what it writes for a
// faction, and that everything else in the user's file stays where it was. No game, no files.

#include "../src/FactionEditorText.h"

#include <iostream>
#include <stdexcept>

using namespace IntelEngine::FactionEditor;
using json = nlohmann::json;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static json SilverHand() {
    return json::parse(R"({"id": "TheSilverHandFaction", "name": "The Silver Hand", "type": "military", "hold": "Whiterun",
        "skyrim_faction_id": "SilverHandFaction", "leader_names": ["Krev the Skinner", " Hillara "], "rivals": ["CompanionsFaction"],
        "allies": [], "base_army_strength": 20, "war_threshold": -50, "conflict_style": "", "soldier_template": "MyMod.esp|0x000A02",
        "prison_location": "", "prison_marker": [0, 0, 0], "pe_side": -1})");
}

int main() {
    try {
        // ---- what is refused
        Require(Refusal(SilverHand()).empty(), "a plain faction is fine");
        json f = SilverHand();
        f["id"] = "";
        Require(Refusal(f).find("no id") != std::string::npos, "an id is needed");
        f["id"] = "Silver Hand";
        Require(Refusal(f).find("letters, digits and _") != std::string::npos, "no spaces in an id");
        f["id"] = "PE_Whiterun";
        Require(Refusal(f).find("Political Engine") != std::string::npos, "PE_ ids are not the user's");
        f = SilverHand();
        f["name"] = "";
        Require(Refusal(f).find("no name") != std::string::npos, "a name is needed");
        f = SilverHand();
        f["name"] = "The \"Silver\" Hand";
        Require(Refusal(f).find("quote") != std::string::npos, "a quote cannot be written: the file's reader knows no escapes");
        f = SilverHand();
        f["type"] = "army";
        Require(Refusal(f).find("military, guild or political") != std::string::npos, "one of the three kinds");
        f = SilverHand();
        f["leader_names"] = {"Krev, the Skinner"};
        Require(Refusal(f).find("comma") != std::string::npos, "a comma would split a name in two");
        Require(Refusal(json::array()).find("not a faction") != std::string::npos, "not an object");

        // ---- what is written
        const std::string yaml = FactionYaml(SilverHand());
        Require(yaml ==
                    "  - id: \"TheSilverHandFaction\"\n"
                    "    name: \"The Silver Hand\"\n"
                    "    type: \"military\"\n"
                    "    hold: \"Whiterun\"\n"
                    "    skyrim_faction_id: \"SilverHandFaction\"\n"
                    "    leader_names: [\"Krev the Skinner\", \"Hillara\"]\n"
                    "    rivals: [\"CompanionsFaction\"]\n"
                    "    allies: []\n"
                    "    base_army_strength: 20\n"
                    "    war_threshold: -50\n"
                    "    soldier_template: \"MyMod.esp|0x000A02\"\n",
                "a faction as the file has it: empty keys left out, names trimmed");
        f = SilverHand();
        f["free_after"] = json::array();
        f["conflict_style"] = "brawl";
        f["prison_location"] = "WhiterunJorrvaskr";
        f["prison_marker"] = {1.5, 2, 3};
        f["pe_side"] = 2;
        f["war_threshold"] = 40;
        f["base_army_strength"] = 9000;
        const std::string full = FactionYaml(f);
        Require(full.find("    free_after: []\n") != std::string::npos, "an empty free_after is written: it says \"never bound\"");
        Require(FactionYaml(SilverHand()).find("free_after") == std::string::npos, "not given: not written, and the game's own rule holds");
        Require(full.find("    conflict_style: \"brawl\"\n") != std::string::npos && full.find("    prison_location: \"WhiterunJorrvaskr\"\n") != std::string::npos &&
                    full.find("    prison_marker: [1.5, 2, 3]\n") != std::string::npos && full.find("    pe_side: 2\n") != std::string::npos,
                "the rarer keys when they are set");
        Require(full.find("    war_threshold: 0\n") != std::string::npos && full.find("    base_army_strength: 500\n") != std::string::npos, "numbers held within their range");

        // ---- everything else in the file stays where it was
        const std::string file =
            "# Faction Politics Configuration\n"
            "# My own notes.\n"
            "factions:\n"
            "  - id: \"Old\"\n"
            "    name: \"Old\"   # gone after the edit\n"
            "\n"
            "# Which pairs start how.\n"
            "default_relations:\n"
            "  - faction_a: \"A\"\n"
            "    faction_b: \"B\"\n"
            "    relation: 15\n"
            "\n"
            "war_allowed:\n"
            "  military: [\"military\"]\n";
        const std::string section = "factions:\n" + FactionYaml(SilverHand());
        const std::string spliced = Splice(file, section);
        Require(spliced.rfind("# Faction Politics Configuration\n# My own notes.\nfactions:\n  - id: \"TheSilverHandFaction\"\n", 0) == 0, "the head of the file, then the new section");
        Require(spliced.find("\"Old\"") == std::string::npos, "the old section is gone");
        Require(spliced.find("\n\n# Which pairs start how.\ndefault_relations:\n  - faction_a: \"A\"\n    faction_b: \"B\"\n    relation: 15\n\nwar_allowed:\n  military: [\"military\"]\n") != std::string::npos,
                "what follows is untouched, its heading comment with it");
        Require(Splice(spliced, section) == spliced, "saving the same again changes nothing");
        const std::string crlf = "factions:\r\n  - id: \"Old\"\r\ndefault_relations:\r\n  - faction_a: \"A\"\r\n";
        Require(Splice(crlf, section).find("default_relations:\n  - faction_a: \"A\"\n") != std::string::npos && Splice(crlf, section).find("\"Old\"") == std::string::npos,
                "a file with Windows line ends");
        Require(Splice("", section) == section, "an empty file gets the section");
        Require(Splice("war_allowed:\n  military: [\"military\"]\n", section) == section + "\nwar_allowed:\n  military: [\"military\"]\n", "a file without the section gets it first");
        const std::string last = "# head\nfactions:\n  - id: \"Old\"\n";
        Require(Splice(last, section) == "# head\n" + section, "the section as the last of the file");
    } catch (const std::exception& e) {
        std::cerr << "FactionEditorCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "FactionEditorCheck passed\n";
    return 0;
}
