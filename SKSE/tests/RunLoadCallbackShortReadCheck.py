"""Compile and exercise the production LoadCallback without the Skyrim ABI (VS shell)."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/Plugin.cpp").read_text(encoding="utf-8")
start = source.index("    void LoadCallback(")
end = source.index("    void RevertCallback(", start)
with tempfile.TemporaryDirectory(prefix="load-callback-check-") as directory:
    work = pathlib.Path(directory)
    (work / "LoadCallback.inc").write_text(source[start:end], encoding="utf-8")
    subprocess.run(["cl", "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", "/I" + str(work),
                    str(root / "tests/LoadCallbackShortReadCheck.cpp"), "/Fe:" + str(work / "check.exe")], check=True, cwd=work)
    subprocess.run([str(work / "check.exe")], check=True)
