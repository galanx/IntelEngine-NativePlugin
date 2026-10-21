"""Compile the real persistence methods without the game ABI; requires clang++ and vcpkg.

Usage (VS developer shell): python SKSE/tests/RunBattleRelationPersistenceCheck.py <vcpkg-triplet-dir>
No production logic is copied: selected complete methods are compiled verbatim, together
with the full PoliticalDB.cpp and production headers. Temporary shims replace only the
clock, logging and political-state file output; private access is test-local.
"""
import pathlib
import subprocess
import sys
import tempfile

src = pathlib.Path(__file__).resolve().parents[1] / "src"
deps = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="battle-relation-") as directory:
    work = pathlib.Path(directory)
    (work / "Plugin.h").write_text("""
#pragma once
#include <atomic>
#include <cstdint>
namespace RE {
using FormID = std::uint32_t;
class Actor;
struct Calendar {
    static Calendar* GetSingleton() { static Calendar clock; return &clock; }
    float GetCurrentGameTime() const { return 2.f; }
};
}
namespace logger {
template<class... T> void info(T&&...) {}
template<class... T> void warn(T&&...) {}
template<class... T> void error(T&&...) {}
}
""", encoding="utf-8")
    for name in ("PoliticalDB.h", "PoliticalDB.cpp", "FactionPolitics.h"):
        text = (src / name).read_text(encoding="utf-8")
        if name == "FactionPolitics.h":
            text = text.replace("    private:", "    public:")
        (work / name).write_text(text, encoding="utf-8")
    source = (src / "FactionPolitics.cpp").read_text(encoding="utf-8")
    methods = []
    for signature in ("void FactionPolitics::RecalculateRelationScores(",
                      "bool FactionPolitics::EndWar(",
                      "int FactionPolitics::RecordOffScreenBattle("):
        start = source.index("    " + signature)
        end = source.index("\n    }", start) + len("\n    }")
        methods.append(source[start:end])
    (work / "FactionPoliticsMethods.inc").write_text(
        "namespace IntelEngine {\n" + "\n".join(methods) +
        "\nvoid FactionPolitics::WritePoliticalStateFile() {}\n}\n", encoding="utf-8")
    executable = work / "check.exe"
    subprocess.run([
        "clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-D_CRT_SECURE_NO_WARNINGS",
        "-I" + str(work), "-isystem", str(deps / "include"),
        str(pathlib.Path(__file__).with_name("BattleRelationPersistenceCheck.cpp")),
        str(work / "PoliticalDB.cpp"), str(deps / "lib/sqlite3.lib"),
        "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], cwd=work, check=True)
