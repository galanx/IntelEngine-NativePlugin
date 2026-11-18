"""Fail if a compiled script interns an engine name in the wrong case.

The game keeps one case-insensitive string pool, and the first spelling loaded wins. A .pex whose string table
holds "generation" makes the lightning controller's lookup of "Generation" miss, so Sparks bolts go invisible
(galanx/IntelEngine-GamePlugin#35). Quoted Papyrus strings are safe; bare identifiers (variables, parameters,
properties) are what land here.

Usage: python -X utf8 SKSE/tests/RunPexStringCaseCheck.py <dir>

<dir> is searched recursively: a Scripts folder, or a whole MO2 mods folder to find which mod is at fault.
"""
import struct
import sys
from pathlib import Path

# Names the engine looks up case-sensitively (meshes\Magic\LightningBolt01.nif controller).
ENGINE_NAMES = ["Generation", "Mutation", "ArcOffset"]


def strings(pex: bytes) -> list[str]:
    if pex[:4] != b"\xfa\x57\xc0\xde":
        raise ValueError("not a Skyrim .pex")
    pos = 4 + 1 + 1 + 2 + 8  # magic, major, minor, game id, compile time

    def wstr() -> str:
        nonlocal pos
        (n,) = struct.unpack_from(">H", pex, pos)
        s = pex[pos + 2 : pos + 2 + n].decode("utf-8", "replace")
        pos += 2 + n
        return s

    for _ in range(3):  # source file, user, machine
        wstr()
    (count,) = struct.unpack_from(">H", pex, pos)
    pos += 2
    return [wstr() for _ in range(count)]


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    wanted = {n.lower(): n for n in ENGINE_NAMES}
    bad = 0
    root = Path(sys.argv[1])
    files = sorted(root.rglob("*.pex"))
    for f in files:
        try:
            table = strings(f.read_bytes())
        except (ValueError, struct.error) as e:
            print(f"SKIP {f.relative_to(root)}: {e}")
            continue
        for s in table:
            right = wanted.get(s.lower())
            if right and s != right:
                print(f"FAIL {f.relative_to(root)}: '{s}' (the engine looks up '{right}')")
                bad += 1
    print(f"{len(files)} .pex checked, {bad} wrong-case engine names")
    return 1 if bad or not files else 0


if __name__ == "__main__":
    sys.exit(main())
