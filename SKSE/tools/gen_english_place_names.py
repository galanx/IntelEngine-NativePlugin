#!/usr/bin/env python3
"""Generate a place-name file: names of locations and interior cells in one language, read by the DLL at start-up.

Usage (masters in load order; --strings holds <stem>_<language>.STRINGS files):
    python SKSE/tools/gen_english_place_names.py --strings <dir> --out SKSE/Plugins/IntelEngine/placenames/english.tsv \
        Skyrim.esm Update.esm Dawnguard.esm HearthFires.esm Dragonborn.esm
Another language: add --language german (the STRINGS suffix) and --encoding of those files, and name the output
after the language. Every Data/SKSE/Plugins/IntelEngine/placenames/*.tsv is read; the game's own names win.
"""
import argparse
import collections
import hashlib
import os
import struct
import sys
import zlib

DELETED, COMPRESSED, LOCALIZED = 0x20, 0x00040000, 0x80


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def subrecords(data):
    pos, big = 0, None
    while pos + 6 <= len(data):
        kind, size = data[pos:pos + 4], struct.unpack_from("<H", data, pos + 4)[0]
        pos += 6
        if kind == b"XXXX":  # u32 size for the next subrecord
            big = struct.unpack_from("<I", data, pos)[0]
            pos += size
            continue
        if big is not None:
            size, big = big, None
        yield kind, data[pos:pos + size]
        pos += size


def read_record(f, size, flags):
    data = f.read(size)
    if flags & COMPRESSED:
        data = zlib.decompress(data[4:])
    return data


def load_strings(path):
    with open(path, "rb") as f:
        blob = f.read()
    count = struct.unpack_from("<I", blob, 0)[0]
    base = 8 + count * 8
    table = {}
    for i in range(count):
        sid, offset = struct.unpack_from("<II", blob, 8 + i * 8)
        start = base + offset
        table[sid] = blob[start:blob.index(b"\0", start)]
    return table


def cstr(raw):
    return raw.split(b"\0", 1)[0]


def parse_plugin(path, strings_dir, canonical, language="english", encoding="cp1252"):
    """Yield (defining master, local form id, 'L'|'C', name in that language) for one plugin."""
    name = os.path.basename(path)
    file_size = os.path.getsize(path)
    with open(path, "rb") as f:
        kind, size, flags = struct.unpack("<4sII", f.read(24)[:12])
        if kind != b"TES4":
            sys.exit(f"{path}: not a TES4 plugin")
        masters = [cstr(d).decode("cp1252") for k, d in subrecords(read_record(f, size, flags)) if k == b"MAST"]
        masters = [canonical.get(m.lower(), m) for m in masters] + [name]
        strings = None
        if flags & LOCALIZED:
            stem = os.path.splitext(name)[0]
            strings = load_strings(os.path.join(strings_dir, f"{stem}_{language}.strings"))

        def walk(end):
            while f.tell() < end:
                kind, size, flags, form_id = struct.unpack("<4sIII", f.read(24)[:16])
                if kind == b"GRUP":
                    # flags = label, form_id = group type here. Top LCTN/CELL groups, and the
                    # interior cell block (2) / sub-block (3) groups hold the records we want.
                    group_end = f.tell() - 24 + size
                    if (form_id == 0 and flags in (0x4E54434C, 0x4C4C4543)) or form_id in (2, 3):
                        yield from walk(group_end)
                    f.seek(group_end)
                    continue
                if kind not in (b"LCTN", b"CELL") or flags & DELETED:
                    f.seek(size, 1)
                    continue
                full, interior = None, kind == b"LCTN"
                for sub, data in subrecords(read_record(f, size, flags)):
                    if sub == b"FULL":
                        full = data
                    elif sub == b"DATA" and kind == b"CELL" and data:
                        interior = bool(data[0] & 1)
                if full is None or not interior:
                    continue
                raw = strings.get(struct.unpack("<I", full[:4])[0], b"") if strings is not None else cstr(full)
                text = raw.decode(encoding, "replace").strip()
                index = form_id >> 24
                if text and index < len(masters):
                    yield masters[index], form_id & 0xFFFFFF, "L" if kind == b"LCTN" else "C", text

        yield from walk(file_size)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--strings", required=True, help="directory with <stem>_<language>.STRINGS files")
    parser.add_argument("--out", required=True)
    parser.add_argument("--language", default="english", help="STRINGS file suffix, e.g. german, russian")
    parser.add_argument("--encoding", default="cp1252", help="text encoding of those STRINGS files (written as UTF-8)")
    parser.add_argument("masters", nargs="+", help="master files in load order")
    args = parser.parse_args()

    canonical = {os.path.basename(p).lower(): os.path.basename(p) for p in args.masters}
    entries = {}  # later masters override earlier ones for the same key
    for path in args.masters:
        for master, local_id, kind, text in parse_plugin(path, args.strings, canonical, args.language, args.encoding):
            entries[(master.lower(), local_id)] = (master, kind, " ".join(text.split()))  # no tabs or line breaks

    language = args.language.capitalize()
    lines = [
        "# GENERATED by SKSE/tools/gen_english_place_names.py; regenerate rather than edit by hand.",
        f"# {language} names of locations (L) and interior cells (C): master<TAB>local FormID (hex)<TAB>type<TAB>name (UTF-8).",
        "# Command: python SKSE/tools/gen_english_place_names.py --strings <dir> --out " + args.out.replace("\\", "/")
        + ("" if args.language == "english" else f" --language {args.language} --encoding {args.encoding}")
        + " " + " ".join(os.path.basename(p) for p in args.masters),
        "# Inputs (SHA-256):",
    ]
    for path in args.masters:
        lines.append(f"#   {sha256(path)}  {os.path.basename(path)}")
        strings_path = os.path.join(args.strings, os.path.splitext(os.path.basename(path))[0] + f"_{args.language}.strings")
        if os.path.exists(strings_path):
            lines.append(f"#   {sha256(strings_path)}  {os.path.basename(strings_path)}")
    for (_, local_id), (master, kind, text) in sorted(entries.items()):
        lines.append(f"{master}\t{local_id:06X}\t{kind}\t{text}")
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

    counts = collections.Counter((master, kind) for master, kind, _ in entries.values())
    for (master, kind), count in sorted(counts.items()):
        print(f"{master} {kind} {count}")
    print(f"total {len(entries)}")


if __name__ == "__main__":
    main()
