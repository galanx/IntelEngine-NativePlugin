"""Compile real political DB/feed/config code without the game ABI (VS developer shell).
Usage: python SKSE/tests/RunPoliticalFeedCheck.py <vcpkg-triplet-dir>
Only engine objects, clock, logging and state-file output are modeled. Production
methods including shared actor registration are extracted verbatim.
"""
import pathlib
import subprocess
import sys
import tempfile

src = pathlib.Path(__file__).resolve().parents[1] / "src"
deps = pathlib.Path(sys.argv[1]).resolve()


def method(text, signature):
    start = text.rfind("\n", 0, text.index("    " + signature)) + 1
    indent = text[start:start + len(text[start:]) - len(text[start:].lstrip(" "))]
    closing = "\n" + indent + "}"
    end = text.index(closing, start) + len(closing)
    return text[start:end]


with tempfile.TemporaryDirectory(prefix="political-feed-") as directory:
    work = pathlib.Path(directory)
    (work / "Plugin.h").write_text(r'''
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
namespace RE {
using FormID = std::uint32_t;
class TESFaction;
class TESBoundObject;
class TESObjectREFR;
struct TESFile { const char* fileName; };
inline TESFile fixtureFile{"Fixture.esm"};
struct TESForm {
    FormID id, local;
    TESFile* file;
    TESFile* GetFile(int) const { return file; }
    FormID GetFormID() const { return id; }
    FormID GetLocalFormID() const { return local; }
    bool IsDynamicForm() const { return id >= 0xFF000000; }
    template<class T> static T* LookupByID(FormID id) { static T value{}; return id == 0x2A456 ? &value : nullptr; }
};
struct BGSKeyword : TESForm {};
struct BGSLocation : TESForm {
    struct KeywordData { BGSKeyword* keyword; float data; };
    std::string name;
    std::vector<KeywordData> keywordData;
    const char* GetName() const { return name.c_str(); }
};
struct TESDataHandler {
    std::vector<BGSLocation*> locations;
    int reads = 0;
    static TESDataHandler* GetSingleton() { static TESDataHandler value; return &value; }
    template<class T> const auto& GetFormArray() { ++reads; return locations; }
};
struct World : TESForm {
    World(FormID value) : TESForm{value, value, &fixtureFile} {}
};
struct Cell : TESForm {
    bool interior; World* world;
    Cell(FormID value, bool inside, World* parent) : TESForm{value, value, &fixtureFile}, interior(inside), world(parent) {}
    bool IsInteriorCell() const { return interior; }
};
struct Actor {
    bool dead = false, essential = false;
    Cell* cell = nullptr;
    float x = 0, y = 0, z = 0;
    FormID GetFormID() const { return 0xFF000001; }
    bool IsDead() const { return dead; }
    bool IsEssential() const { return essential; }
    Cell* GetParentCell() const { return cell; }
    World* GetWorldspace() const { return cell ? cell->world : nullptr; }
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
};
struct Calendar {
    static Calendar* GetSingleton() { static Calendar value; return &value; }
    float GetCurrentGameTime() const { return 2.f; }
};
}
namespace logger {
inline int warnings = 0;
template<class... T> void info(T&&...) {}
template<class... T> void warn(T&&...) { ++warnings; }
template<class... T> void error(T&&...) {}
}
''', encoding="utf-8")
    for name in ("PoliticalDB.h", "PoliticalDB.cpp", "FactionPolitics.h",
                 "FactionConfigLoader.h", "FactionConfigLoader.cpp", "BattleManager.h"):
        text = (src / name).read_text(encoding="utf-8")
        if name in ("FactionPolitics.h", "BattleManager.h"):
            text = text.replace("    private:", "    public:")
        (work / name).write_text(text, encoding="utf-8")
    header = (work / "FactionPolitics.h").read_text()
    (work / "FactionPolitics.h").write_text(header.replace("    public:", "    public:\n        nlohmann::json TestPEContext();", 1))
    source = (src / "FactionPolitics.cpp").read_text(encoding="utf-8")
    signatures = (
        "std::string ToLower(",
        "const char* BattleResultBlockedReason(",
        "std::string FactionPolitics::GetConfigPath(",
        "std::string FactionPolitics::GetDefaultFactionConfig(",
        "bool FactionPolitics::LoadConfig(",
        "std::optional<FactionConfig> FactionPolitics::GetFaction(",
        "bool FactionPolitics::ValidateEvent(",
        "int FactionPolitics::RecordPoliticalEvent(",
        "template <typename ResolveFn>",
        "nlohmann::json FactionPolitics::FactionToJson(",
        "std::string FactionPolitics::GetFactionsJson(",
        "int FactionPolitics::RecordOffScreenBattle(",
        "void FactionPolitics::RecalculateRelationScores(",
        "std::string FactionPolitics::ProcessPoliticalDMResponse(",
        "int FactionPolitics::DecayPlayerStandings(",
        "std::string FactionPolitics::GetFactionDisplayName(",
        "int FactionPolitics::EnforceWarRule(",
        "bool FactionPolitics::WarPossible(",
        "std::string FactionPolitics::GetRelationStatus(",
        "nlohmann::json FactionPolitics::FactionToJsonFromSnapshot(",
    )
    (work / "PoliticalFeedMethods.inc").write_text(
        "namespace IntelEngine {\n" + "\n".join(method(source, s) for s in signatures) +
        "\nvoid FactionPolitics::WritePoliticalStateFile() {}\n}\n", encoding="utf-8")
    context_start = source.index("        auto pe = PoliticalEngineBridge::GetSingleton()->Context();")
    context_end = source.index("        // Active wars", context_start)
    (work / "PEContext.inc").write_text(source[context_start:context_end])
    vanilla_start = source.index("        if (PoliticalEngineBridge::GetSingleton()->IsActive() && !PoliticalEngineBridge::GetSingleton()->HasSidesExport())")
    vanilla_end = source.index("        auto* npcIndex", vanilla_start)
    (work / "PEVanillaSides.inc").write_text(source[vanilla_start:vanilla_end])
    battle = (src / "BattleManager.cpp").read_text(encoding="utf-8")
    (work / "PhysicalPosition.inc").write_text(
        "namespace IntelEngine {\n" +
        "\n".join(method(battle, signature) for signature in
                  ("int BattleManager::StartBattle(", "bool BattleManager::RegisterActor(")) +
        "\n}\n", encoding="utf-8")
    exe = work / "check.exe"
    subprocess.run([
        "clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-D_CRT_SECURE_NO_WARNINGS",
        "-I" + str(work), "-I" + str(src), "-isystem", str(deps / "include"),
        str(pathlib.Path(__file__).with_name("PoliticalFeedCheck.cpp")),
        str(work / "PoliticalDB.cpp"), str(work / "FactionConfigLoader.cpp"),
        str(deps / "lib/sqlite3.lib"), "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], cwd=work, check=True)
