"""No JSON of the plugin is written out with a dump that throws on bytes that are no UTF-8.

Names of the game need not be UTF-8: a mod's plugin saved in a Windows code page (Russian, Chinese, accented Latin)
gives an NPC or a place such a name. nlohmann's dump() throws type_error 316 on it; in a Papyrus native or a task of
the game's thread that is a crash, elsewhere a request that silently never goes out. 3.7.1 mended the dashboard
("Failed to push full state: invalid UTF-8 byte"); this check holds every other dump to the same rule.

Usage: python RunJsonDumpCheck.py <SKSE/src>
"""
import pathlib
import re
import sys


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    src = pathlib.Path(sys.argv[1])
    if not src.is_dir():
        raise SystemExit("expected the SKSE/src directory")
    bad = []
    seen = 0
    for path in sorted(list(src.glob("*.cpp")) + list(src.glob("*.h"))):
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            for call in re.finditer(r"\.dump\(([^)]*)\)", line):
                seen += 1
                if "error_handler_t::replace" not in call.group(1):
                    bad.append(f"{path.name}:{number}: {line.strip()[:120]}")
    if seen < 50:
        raise SystemExit(f"only {seen} dumps found: is this the plugin's source?")
    if bad:
        raise SystemExit("a dump that throws on a name that is no UTF-8 (use error_handler_t::replace):\n  " + "\n  ".join(bad))
    print(f"JsonDump check passed ({seen} dumps, all replacing)")


if __name__ == "__main__":
    main()
