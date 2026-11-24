#!/usr/bin/env python3
"""The MCM's translation files, written from SKSE/tools/mcm/strings_<lang>.txt.

IntelEngine_MCM.psc shows every text by key ("$IntelEngine_ActiveTasks"); SkyUI looks the key up in
Interface/Translations/IntelEngine_<LANGUAGE>.txt of the game's own language. A language without a file shows the raw
keys, so a file is written for every language the game ships in: its own strings where there are any, the English
ones otherwise.

The strings files: "key = text" under a "[language]" line; {} takes a value, in order. Keys beginning with _ are parts
this script puts together into the keys the script asks for (the questions before clearing a slot or cancelling a
scheduled task, over the slot's own text):
  Clear<Task>      = _ClearQ + line break + Task<Task>            ({slot}{who}[{name}])
  Clear<Task>Near  = _ClearQ + line break + Task<Task> + " " + SlotNear
  ClearSlot        = _ClearQ + line break + "{}"                  ({slot}{the slot's text})
  CancelSchedFetch = _CancelQ + line break + SchedFetch, CancelSchedMessage likewise, CancelSched = _CancelQ + line break + "{}"
In the files a key that takes values carries one {} per value ("$IntelEngine_Slot{}"), as SkyUI looks it up.

    python SKSE/tools/gen_mcm_translations.py [game repo]      (default: ../IntelEngine-GamePlugin next to this repo)
    python SKSE/tools/gen_mcm_translations.py --check [game repo]  (exit 1 when a key the MCM uses is missing anywhere)
"""
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
MCM = REPO / "Source" / "Scripts" / "IntelEngine_MCM.psc"
PREFIX = "$IntelEngine_"

# The game's languages and their file names; the strings file of each (None: English text).
LANGUAGES = {
    "ENGLISH": "en", "FRENCH": "fr", "GERMAN": "de", "ITALIAN": None, "SPANISH": None, "POLISH": None,
    "RUSSIAN": None, "JAPANESE": None, "CZECH": None, "CHINESE": None,
}


def read(code):
    out = {}
    for line in (HERE / "mcm" / f"strings_{code}.txt").read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#") or re.fullmatch(r"\[\w+\]", line.strip()):
            continue
        key, text = line.split(" = ", 1)
        out[key.strip()] = text.strip()
    return out


def compose(strings):
    out = {k: v for k, v in strings.items() if not k.startswith("_")}
    # The question, then on a line of its own what it is about (as 3.8's message box had it). "\n" is written as these
    # two characters: SkyUI makes the line break of them.
    clear, cancel = strings["_ClearQ"] + "\\n", strings["_CancelQ"] + "\\n"
    for key, text in strings.items():
        if key.startswith("Task") and key != "TaskKey":
            task = key[len("Task"):]
            out["Clear" + task] = clear + text
            out["Clear" + task + "Near"] = clear + text + " " + strings["SlotNear"]
    out["ClearSlot"] = clear + "{}"
    out["CancelSchedFetch"] = cancel + strings["SchedFetch"]
    out["CancelSchedMessage"] = cancel + strings["SchedMessage"]
    out["CancelSched"] = cancel + "{}"
    return out


def used_keys():
    script = MCM.read_text(encoding="utf-8")
    return set(re.findall(r'"\$IntelEngine_([A-Za-z0-9_]+?)(?:\{|")', script)) | set(re.findall(r'\{\$IntelEngine_([A-Za-z0-9_]+)\}', script))


def main(argv):
    check = "--check" in argv
    rest = [a for a in argv if a != "--check"]
    game = pathlib.Path(rest[0]) if rest else REPO.parent / "IntelEngine-GamePlugin"
    english = compose(read("en"))
    used = used_keys()
    missing_en = sorted(used - set(english))
    if missing_en:
        print("keys the MCM uses that the English strings lack:", ", ".join(missing_en))
        return 1
    bad = 0
    for language, code in LANGUAGES.items():
        own = compose(read(code)) if code else {}
        table = dict(english)
        table.update({k: v for k, v in own.items() if k in english})
        if code and code != "en":
            lacking = sorted(set(english) - set(own))
            if lacking:
                print(f"{language}: {len(lacking)} key(s) in English only: {', '.join(lacking[:8])}")
            for key, text in own.items():
                if key in english and text.count("{}") != english[key].count("{}"):
                    print(f"{language}: {key} takes {text.count('{}')} values, English {english[key].count('{}')}")
                    bad += 1
        if check:
            continue
        # A text that takes values is looked up by SkyUI under its key with one {} for each value ("$IntelEngine_Slot{}"
        # for "$IntelEngine_Slot{2}"), as SkyUI's own file writes them. Without them the key showed raw in the menu.
        lines = [f"{PREFIX}{key}{'{}' * english[key].count('{}')}\t{table[key]}" for key in sorted(table)]
        path = game / "Interface" / "Translations" / f"IntelEngine_{language}.txt"
        path.parent.mkdir(parents=True, exist_ok=True)
        # SkyUI reads these as UTF-16 LE with a byte order mark, one "key<TAB>text" a line.
        path.write_bytes(b"\xff\xfe" + ("\r\n".join(lines) + "\r\n").encode("utf-16-le"))
    if not check:
        print(f"{len(LANGUAGES)} translation files of {len(english)} keys each written to {game / 'Interface' / 'Translations'}")
    print(f"{len(used)} keys used by the MCM, all present in English")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
