"""Compile and exercise the actual field lookup without the Skyrim ABI (VS shell)."""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/Papyrus.cpp").read_text(encoding="utf-8")
strip_start = source.index("    static bool StripToJson(")
strip_end = source.index("    bool StoryResponseShouldAct(", strip_start)
cache_start = source.index("    constexpr std::size_t kLogTruncLen")
cache_end = source.index("    RE::BSFixedString BuildActorContextJson(", cache_start)
with tempfile.TemporaryDirectory(prefix="story-field-check-") as directory:
    work = pathlib.Path(directory)
    (work / "StoryResponseProduction.inc").write_text(
        source[strip_start:strip_end] + source[cache_start:cache_end], encoding="utf-8")
    subprocess.run(["cl", "/nologo", "/std:c++20", "/EHsc", "/utf-8", "/W4", "/WX",
                    "/I" + str(work), "/I" + str(pathlib.Path(sys.argv[1]) / "include"),
                    str(root / "tests/StoryResponseFieldCheck.cpp"), "/Fe:" + str(work / "check.exe"),
                    "/Fo:" + str(work / "check.obj")], check=True, cwd=work)
    subprocess.run([str(work / "check.exe")], check=True)
