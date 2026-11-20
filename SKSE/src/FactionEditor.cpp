#include "FactionEditor.h"

#include "FactionConfigLoader.h"
#include "FactionEditorText.h"
#include "FactionPolitics.h"
#include "SoldierTemplates.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>

namespace IntelEngine::FactionEditor {

    namespace {
        std::mutex g_mutex;
        std::string g_catalog;

        nlohmann::json ToJson(const FactionConfig& f) {
            nlohmann::json j;
            j["id"] = f.id;
            j["name"] = f.name;
            j["type"] = f.type;
            j["hold"] = f.hold;
            j["skyrim_faction_id"] = f.skyrimFactionId;
            j["leader_names"] = f.leaderNames;
            j["rivals"] = f.rivals;
            j["allies"] = f.allies;
            j["base_army_strength"] = f.baseArmyStrength;
            j["war_threshold"] = f.warThreshold;
            j["conflict_style"] = f.conflictStyle;
            j["soldier_template"] = f.soldierTemplate;
            j["prison_location"] = f.prisonLocation;
            j["prison_marker"] = {f.prisonMarker[0], f.prisonMarker[1], f.prisonMarker[2]};
            j["pe_side"] = f.peSide;
            if (f.freeAfterSet) j["free_after"] = f.freeAfter;
            return j;
        }

        std::string PluginOf(const RE::TESForm* form) {
            auto* file = form ? form->GetFile(0) : nullptr;
            return file ? std::string(file->GetFilename()) : std::string();
        }

        // How a record is written into the file: by the name the game keeps for it, else by plugin and form id.
        std::string RefOf(const RE::TESForm* form, const std::string& editorId) {
            if (!editorId.empty()) return editorId;
            char hex[16];
            const auto plugin = PluginOf(form);
            if (plugin.empty()) {
                std::snprintf(hex, sizeof hex, "0x%08X", form->GetFormID());
                return hex;
            }
            std::snprintf(hex, sizeof hex, "0x%06X", form->GetLocalFormID());
            return plugin + "|" + hex;
        }
    }

    RE::TESFaction* GameFaction(const std::string& id) {
        if (id.empty()) return nullptr;
        SoldierTemplates::Ref ref;
        if (!SoldierTemplates::ParseRef(id, ref)) return RE::TESForm::LookupByEditorID<RE::TESFaction>(id);
        RE::TESForm* form = nullptr;
        if (ref.plugin.empty()) {
            form = RE::TESForm::LookupByID(ref.formId);
        } else if (auto* data = RE::TESDataHandler::GetSingleton()) {
            form = data->LookupForm(ref.formId, ref.plugin);
        }
        return form ? form->As<RE::TESFaction>() : nullptr;
    }

    std::string Catalog() {
        std::scoped_lock lock(g_mutex);
        if (!g_catalog.empty()) return g_catalog;
        const auto began = std::chrono::steady_clock::now();
        nlohmann::json out;
        out["gameFactions"] = nlohmann::json::array();
        out["templates"] = nlohmann::json::array();
        out["holds"] = kHolds;
        out["types"] = kTypes;
        out["styles"] = kStyles;
        if (auto* data = RE::TESDataHandler::GetSingleton()) {
            // The names the game itself keeps (its editor-id map), for records whose own name it does not hand out.
            std::unordered_map<RE::FormID, std::string> names;
            {
                const auto& [map, mapLock] = RE::TESForm::GetAllFormsByEditorID();
                const RE::BSReadLockGuard guard{mapLock};
                if (map) {
                    for (const auto& [id, form] : *map) {
                        if (form && (form->Is(RE::FormType::Faction) || form->Is(RE::FormType::LeveledNPC))) names.emplace(form->GetFormID(), id.c_str());
                    }
                }
            }
            const auto editorId = [&](const RE::TESForm* form) {
                const char* own = form->GetFormEditorID();
                if (own && *own) return std::string(own);
                const auto it = names.find(form->GetFormID());
                return it == names.end() ? std::string() : it->second;
            };
            for (auto* faction : data->GetFormArray<RE::TESFaction>()) {
                if (!faction) continue;
                const auto id = editorId(faction);
                const char* name = faction->GetFullName();
                if (id.empty() && (!name || !*name)) continue;   // nothing a person could tell it by
                out["gameFactions"].push_back({{"id", RefOf(faction, id)}, {"name", name ? name : ""}, {"plugin", PluginOf(faction)}});
            }
            for (auto* list : data->GetFormArray<RE::TESLevCharacter>()) {
                if (!list || list->numEntries == 0) continue;    // a list an overhaul emptied raises nobody
                const auto id = editorId(list);
                out["templates"].push_back({{"id", RefOf(list, id)}, {"plugin", PluginOf(list)}});
            }
        }
        g_catalog = out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        logger::info("FactionEditor: what the load order offers: {} factions, {} lists of people to raise soldiers from ({:.0f} ms, once a session, off the game's thread)",
                     out["gameFactions"].size(), out["templates"].size(),
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count());
        return g_catalog;
    }

    nlohmann::json Current() {
        auto out = nlohmann::json::array();
        const auto loaded = LoadFactionConfigFromFile(FactionPolitics::GetSingleton()->ConfigPath());
        if (!loaded.success) return out;
        for (const auto& f : loaded.factions) out.push_back(ToJson(f));
        return out;
    }

    nlohmann::json Save(const nlohmann::json& factions) {
        using namespace text;
        nlohmann::json result{{"ok", false}, {"error", ""}, {"warnings", nlohmann::json::array()}};
        const auto fail = [&](const std::string& why) {
            result["error"] = why;
            logger::warn("FactionEditor: not saved: {}", why);
            return result;
        };
        if (!factions.is_array() || factions.empty()) return fail("there is no faction to save");
        if (factions.size() > 64) return fail("more than 64 factions");
        std::set<std::string> ids;
        for (const auto& f : factions) {
            if (const auto why = Refusal(f); !why.empty()) return fail(why);
            if (!ids.insert(Text(f, "id")).second) return fail(Text(f, "id") + ": two factions have this id");
        }
        std::string section = "factions:\n";
        for (const auto& f : factions) {
            section += FactionYaml(f);
            const auto id = Text(f, "id");
            // What the load order does not have is said, not refused: a mod may be switched on later.
            if (const auto game = Text(f, "skyrim_faction_id"); !game.empty() && !GameFaction(game)) {
                result["warnings"].push_back(id + ": the load order has no faction \"" + game + "\" (membership by it will find nobody)");
            }
            for (const char* key : {"rivals", "allies"}) {
                for (const auto& other : List(f, key)) {
                    if (!ids.contains(other)) result["warnings"].push_back(id + ": \"" + other + "\" in " + key + " is no faction of this file");
                }
            }
        }
        auto* politics = FactionPolitics::GetSingleton();
        const std::filesystem::path path = politics->ConfigPath();
        std::string before;
        {
            std::ifstream in(path, std::ios::binary);
            std::stringstream buffer;
            buffer << in.rdbuf();
            before = buffer.str();
        }
        const auto after = Splice(before, section);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (!before.empty()) {
            auto backup = path;
            backup += ".bak";
            std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) return fail("the old file could not be kept as factions.yaml.bak: " + ec.message());
        }
        {
            auto fresh = path;
            fresh += ".new";
            {
                std::ofstream out(fresh, std::ios::binary | std::ios::trunc);
                if (!out.is_open()) return fail("the file could not be written");
                out << after;
            }
            // Read it back before it replaces anything: what the game would load must be what was meant.
            const auto check = LoadFactionConfigFromFile(fresh.string());
            if (!check.success || check.factions.size() != factions.size()) {
                std::filesystem::remove(fresh, ec);
                return fail("what was written did not read back as " + std::to_string(factions.size()) + " factions; nothing was changed");
            }
            std::filesystem::rename(fresh, path, ec);
            if (ec) {
                // Some file systems do not rename onto an existing file: the old one is safe beside it as .bak.
                std::filesystem::remove(path, ec);
                std::filesystem::rename(fresh, path, ec);
                if (ec) return fail("the new file could not take the old one's place: " + ec.message());
            }
        }
        if (!politics->ReloadConfig()) return fail("the file was written, but could not be read in again; the old one is beside it as factions.yaml.bak");
        result["ok"] = true;
        result["factions"] = Current();
        logger::info("FactionEditor: {} factions saved ({} warning(s)); the old file is kept as factions.yaml.bak", factions.size(), result["warnings"].size());
        return result;
    }

    namespace {
        // Test hook: what the editor does when it is opened and when it saves, without the dashboard: the catalogue of
        // the load order, the file as it stands, and that file saved as it is (so nothing changes). Off the game's
        // thread, as the dashboard does it; the log tells.
        void FactionEditorSelfTest(RE::StaticFunctionTag*) {
            std::thread([]() {
                const auto catalog = nlohmann::json::parse(Catalog(), nullptr, false);
                const auto current = Current();
                std::string first, list;
                if (catalog.is_object() && !catalog["gameFactions"].empty()) first = catalog["gameFactions"][0].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                if (catalog.is_object() && !catalog["templates"].empty()) list = catalog["templates"][0].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                logger::info("FactionEditor: test: {} factions in the file; the first the load order offers: {}; the first list: {}", current.size(), first, list);
                int named = 0, byRef = 0;
                if (catalog.is_object()) {
                    for (const auto& g : catalog["gameFactions"]) {
                        const auto id = g.value("id", std::string());
                        (id.find('|') == std::string::npos && id.rfind("0x", 0) != 0 ? named : byRef)++;
                        if (!GameFaction(id)) logger::warn("FactionEditor: test: \"{}\" is offered but does not resolve", id);
                    }
                }
                logger::info("FactionEditor: test: {} factions offered by name, {} by plugin and form id; every one resolves unless warned above", named, byRef);
                // The user's file is not touched by a test (it is shared by every profile of a mod manager): what a
                // save would write goes to a file of its own in the temp folder and is read back from there.
                std::string before;
                {
                    std::ifstream in(FactionPolitics::GetSingleton()->ConfigPath(), std::ios::binary);
                    std::stringstream buffer;
                    buffer << in.rdbuf();
                    before = buffer.str();
                }
                std::string section = "factions:\n";
                std::string refused;
                for (const auto& f : current) {
                    if (const auto why = Refusal(f); !why.empty()) refused += why + "; ";
                    section += FactionYaml(f);
                }
                std::error_code ec;
                const auto trial = std::filesystem::temp_directory_path(ec) / "intelengine_factions_trial.yaml";
                {
                    std::ofstream out(trial, std::ios::binary | std::ios::trunc);
                    out << Splice(before, section);
                }
                const auto was = LoadFactionConfigFromFile(FactionPolitics::GetSingleton()->ConfigPath());
                const auto now = LoadFactionConfigFromFile(trial.string());
                bool same = was.success && now.success && was.factions.size() == now.factions.size() &&
                            was.defaultRelations.size() == now.defaultRelations.size() && was.warAllowed.size() == now.warAllowed.size();
                for (std::size_t i = 0; same && i < was.factions.size(); ++i) {
                    const auto& a = was.factions[i];
                    const auto& b = now.factions[i];
                    same = a.id == b.id && a.name == b.name && a.type == b.type && a.hold == b.hold && a.skyrimFactionId == b.skyrimFactionId &&
                           a.leaderNames == b.leaderNames && a.rivals == b.rivals && a.allies == b.allies && a.baseArmyStrength == b.baseArmyStrength &&
                           a.warThreshold == b.warThreshold && a.conflictStyle == b.conflictStyle && a.soldierTemplate == b.soldierTemplate &&
                           a.prisonLocation == b.prisonLocation && a.peSide == b.peSide && a.freeAfter == b.freeAfter;
                    if (!same) logger::warn("FactionEditor: test: {} does not read back the same", a.id);
                }
                std::filesystem::remove(trial, ec);
                logger::info("FactionEditor: test: what a save would write reads back {} ({} factions, {} relations; refused: '{}'); the user's file was not touched",
                             same ? "the same as the file" : "DIFFERENT", now.factions.size(), now.defaultRelations.size(), refused);
            }).detach();
        }
    }

    void Register(RE::BSScript::IVirtualMachine* a_vm, int& count) {
        a_vm->RegisterFunction("FactionEditorSelfTest", "IntelEngine", FactionEditorSelfTest);
        ++count;
    }
}
