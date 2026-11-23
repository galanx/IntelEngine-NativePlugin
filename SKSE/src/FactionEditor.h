#pragma once

// IntelEngine: the faction editor's side in the DLL (the dashboard's Politics tab, "Edit factions").
//
// The user (2026-11-20): "the new faction editor should be user friendly where a user can easily add new factions and
// assign new templates that are available in the modlist they play in." Until now a faction was added by editing
// factions.yaml by hand, with the game faction and the soldiers named by editor id from memory. The editor offers what
// the user's own load order has: every faction of the game and of their mods, every leveled list of people to raise
// soldiers from, the names of the people the game knows; and it writes the file for them.
//
// Nothing here runs unless the editor is opened. The catalogue is read once a session (the records of a load order do
// not change while the game runs). The words written into the file are FactionEditorText.h's (pure, tested).

#include "Plugin.h"

#include <nlohmann/json.hpp>
#include <string>

namespace IntelEngine::FactionEditor {

    /** The game faction a faction file's skyrim_faction_id names: its editor id ("CWSonsFaction"), or a record of a
     *  plugin by form id ("MyMod.esp|0x000801", "0x0001C21C"). Null when the load order has no such faction. */
    RE::TESFaction* GameFaction(const std::string& id);

    /** What the load order offers, as JSON: {"gameFactions": [{"id", "name", "plugin"}], "templates": [{"id",
     *  "plugin"}], "holds": [...], "types": [...], "styles": [...]}. Built on first use, then kept. Any thread. */
    std::string Catalog();

    /** The user's own faction file as it stands (not the fragments of factions.d): a JSON array of factions with every
     *  key the file knows. Any thread. */
    nlohmann::json Current();

    /** Check what the editor sends, keep the old file beside the new one (factions.yaml.bak), write the "factions:"
     *  section anew (everything else in the file stays as it is), read the new file back before it replaces the old
     *  one, and read it in. Returns {"ok", "error", "warnings": [...], "factions": [...]} ("factions": the file as it
     *  then stands). */
    nlohmann::json Save(const nlohmann::json& factions);

    /** The natives (a test hook). */
    void Register(RE::BSScript::IVirtualMachine* a_vm, int& count);
}
