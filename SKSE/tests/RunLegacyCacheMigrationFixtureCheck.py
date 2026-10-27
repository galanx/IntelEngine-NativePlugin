"""Offline copy-only fixture check. Set IE_CONVERTER_PRIVATE_FIXTURES to opt in."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess


TARGETS = {b"intel_factsrendered", b"intel_gossiprendered", b"intel_taskhistoryrendered"}


def word(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def expected_conversion(data):
    """Independently rebuild expected bytes, changing only caches and two lengths."""
    assert data[:4] == b"SKSE" and word(data, 4) == 1
    offset = 20
    patches = []
    counts = Counter()
    owners = records = 0
    enclosing = None
    for _ in range(word(data, 16)):
        owner, chunks, size = struct.unpack_from("<III", data, offset)
        end = offset + 12 + size
        assert end <= len(data)
        cursor = offset + 12
        owners += owner == 0x424510A2
        for _ in range(chunks):
            kind, version, length = struct.unpack_from("<III", data, cursor)
            start = cursor + 12
            assert start + length <= end
            if owner == 0x424510A2 and kind == 0x53545256:
                records += 1
                assert version == 1
                enclosing = (offset + 8, cursor + 8)
                tokens = data[start:start + length].split(b" ")
                assert tokens and all(tokens)
                declared = int(tokens[0])
                index, position, objects = 1, start + len(tokens[0]) + 1, 0
                while index < len(tokens):
                    identity, entries = tokens[index:index + 2]
                    count = int(entries)
                    assert count > 0
                    position += len(identity) + len(entries) + 2
                    index += 2
                    for _ in range(count):
                        key, raw = tokens[index:index + 2]
                        value_start = position + len(key) + 1
                        if key in TARGETS:
                            # Actor legacy policy: BEL represents a space; the saved
                            # transport restores spaces to BEL after percent encoding.
                            replacement = raw.replace(b"%", b"%25").replace(b"\n", b"%0A")
                            patches.append((value_start, value_start + len(raw), replacement))
                            counts[key.decode("ascii")] += 1
                        position = value_start + len(raw) + 1
                        index += 2
                    objects += 1
                assert objects == declared
            cursor = start + length
        assert cursor == end
        offset = end
    assert offset == len(data) and owners == records == 1 and patches
    expected = bytearray()
    cursor = 0
    for start, end, replacement in patches:
        expected.extend(data[cursor:start])
        expected.extend(replacement)
        cursor = end
    expected.extend(data[cursor:])
    delta = len(expected) - len(data)
    for header in enclosing:
        struct.pack_into("<I", expected, header, word(data, header) + delta)
    return bytes(expected), counts, sum(data[a:b] != replacement for a, b, replacement in patches)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path, help="new retained scratch directory")
    parser.add_argument("--fixtures", type=Path, default=os.environ.get("IE_CONVERTER_PRIVATE_FIXTURES"),
                        help="optional fixture directory or one ESS file")
    parser.add_argument("--expected", type=Path, help="known-good co-save for a single fixture")
    args = parser.parse_args()
    if args.fixtures is None or not args.fixtures.exists():
        print("SKIP: private fixtures unavailable")
        return
    pairs = [args.fixtures] if args.fixtures.is_file() else sorted(args.fixtures.glob("*.ess"))
    assert pairs, "fixture directory contains no ESS pairs"
    assert not args.expected or len(pairs) == 1
    args.work_dir.mkdir(parents=True, exist_ok=False)
    for index, ess in enumerate(pairs):
        skse = ess.with_suffix(".skse")
        source_ess, source_skse = ess.read_bytes(), skse.read_bytes()
        expected, counts, changed = expected_conversion(source_skse)
        area = args.work_dir / str(index)
        area.mkdir()
        copy = area / "fixture.ess"
        shutil.copyfile(ess, copy)
        shutil.copyfile(skse, copy.with_suffix(".skse"))
        result = subprocess.run([str(args.converter.resolve()), "fix", "--save", str(copy.resolve())],
                                capture_output=True, text=True, encoding="utf-8")
        print(result.stdout, end="")
        print(result.stderr, end="")
        assert result.returncode == 0, f"converter exit {result.returncode}"
        actual = (area / "fixture_IEconverted.skse").read_bytes()
        assert actual == expected, "bytes outside exact target rewrite or length patches changed"
        assert (area / "fixture_IEconverted.ess").read_bytes() == source_ess
        assert copy.read_bytes() == ess.read_bytes() == source_ess
        assert copy.with_suffix(".skse").read_bytes() == skse.read_bytes() == source_skse
        assert (area / "fixture_IEbackup/original.ess").read_bytes() == source_ess
        assert (area / "fixture_IEbackup/original.skse").read_bytes() == source_skse
        if args.expected:
            assert actual == args.expected.read_bytes(), "known-good output changed"
        print(json.dumps({"fixture": index, "exit_code": result.returncode, "targets": dict(counts),
                          "rewritten_values": sum(counts.values()), "changed_values": changed,
                          "non_target_bytes_identical": True,
                          "source_sha256": hashlib.sha256(source_skse).hexdigest(),
                          "output_sha256": hashlib.sha256(actual).hexdigest()}))
    print(f"PASS: {len(pairs)} copy-only fixture pair(s); exact cache rewrites, preserved non-target bytes, ESS, backups and sources")


if __name__ == "__main__":
    main()
