#!/usr/bin/env python3
"""Every sentence the player reads, as a template for translators.

What the player reads is put together in the scripts and in the DLL from fixed English and names:
    NotifyPlayer(npc.GetDisplayName() + " headed off to " + destination)
    if (what == "flank") return "A second force of " + theA + " breaks from cover on the flank!";
At run time the finished sentence goes through the language pack of the game's language (LanguagePack.h, Translate):
a pack's line   label <TAB> {1} headed off to {2} <TAB> ...   is found under the names and its translation filled
with them. This tool reads the sources and writes each such sentence once, with {1} {2} where a name or a number
goes, into languages/_labels_template.tsv (a file the game does not load: its name begins with "_"). A translator
copies the lines into a pack and replaces the third field.

    python SKSE/tools/gen_label_template.py [--check] [--game <IntelEngine-GamePlugin checkout>]

--check compares with the file as shipped and fails when it is out of date (RunLabelTemplateCheck in the checks).
"""
import argparse
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.normpath(os.path.join(HERE, "..", ".."))


def read(path):
    return io.open(path, encoding="utf-8").read()


def split_top(expr, sep):
    """Split at `sep` outside strings and brackets."""
    parts, depth, quote, start, i = [], 0, False, 0, 0
    while i < len(expr):
        ch = expr[i]
        if quote:
            if ch == "\\":
                i += 1
            elif ch == '"':
                quote = False
        elif ch == '"':
            quote = True
        elif ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == sep and depth == 0:
            parts.append(expr[start:i])
            start = i + 1
        i += 1
    parts.append(expr[start:])
    return [p.strip() for p in parts]


def call_args(text, at):
    """The text between the bracket at `at` (an opening one) and its partner."""
    depth, quote, i = 0, False, at
    while i < len(text):
        ch = text[i]
        if quote:
            if ch == "\\":
                i += 1
            elif ch == '"':
                quote = False
        elif ch == '"':
            quote = True
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return text[at + 1:i]
        i += 1
    return None


LITERAL = re.compile(r'^"((?:[^"\\]|\\.)*)"$')


def pattern_of(expr):
    """A concatenation into "{1} headed off to {2}"; None when it holds no fixed words worth translating."""
    expr = expr.strip()
    # std::string("...") and the like around a literal
    out, holes, last_hole = "", 0, False
    for operand in split_top(expr, "+"):
        operand = re.sub(r'^std::string\((".*")\)$', r"\1", operand)
        m = LITERAL.match(operand)
        if m:
            literal = m.group(1).replace('\\"', '"').replace("\\\\", "\\")
            if "\\n" in literal or "\\t" in literal:
                return None
            out += literal
            last_hole = False
        else:
            if operand == "":
                return None
            if not last_hole:   # two names side by side are one hole: nothing tells where one ends
                holes += 1
                if holes > 9:
                    return None
                out += "{%d}" % holes
            last_hole = True
    fixed = re.sub(r"\{\d\}", "", out)
    if len(re.findall(r"[A-Za-z]", fixed)) < 3 or "\t" in out:
        return None
    return out


def from_scripts(folder):
    found = []
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".psc") or name == "IntelEngine.psc":
            continue
        text = "\n".join(line for line in read(os.path.join(folder, name)).splitlines() if not line.lstrip().startswith(";"))
        text = text.replace("\\\n", " ")
        for key, which in (("NotifyPlayer(", [0]), ("IntelEngine.L(", [0]), ("ShowChoice(", [1, 2, 3, 4, 5]), ("TellFailureToPlayer(", [0])):
            for m in re.finditer(re.escape(key), text):
                before = text[max(0, m.start() - 9):m.start()]
                if "Function " in before:
                    continue
                args = call_args(text, m.end() - 1)
                if args is None:
                    continue
                parts = split_top(args, ",")
                for index in which:
                    if index < len(parts):
                        p = pattern_of(parts[index])
                        if p:
                            found.append((p, name))
    return found


def from_dll(folder):
    found = []
    # the sentences of a battle: if (what == "...") return <concatenation>;
    text = read(os.path.join(folder, "BattleManager.cpp"))
    for m in re.finditer(r'if \(what == "[a-z_]+"\) return (.*?);\s*(?://.*)?$', text, re.M):
        p = pattern_of(m.group(1))
        if p:
            found.append((p, "BattleManager.cpp"))
    return found


def build():
    rows = from_scripts(os.path.join(NATIVE, "Source", "Scripts")) + from_dll(os.path.join(NATIVE, "SKSE", "src"))
    seen, lines = {}, []
    for pattern, where in rows:
        seen.setdefault(pattern, where)
    out = [
        "# IntelEngine: every sentence the player reads, for translators. GENERATED by SKSE/tools/gen_label_template.py",
        "# from the scripts and the DLL; the game does not load this file (its name begins with \"_\").",
        "# To translate: copy a line into your language's pack (russian.tsv, ...) and replace the THIRD field.",
        "# {1} {2} stand for a name or a number and may change places in the translation; none may be added.",
        "# A sentence no pack has stays English. Longer, more particular sentences are tried first.",
        "",
    ]
    for pattern in sorted(seen, key=lambda p: (seen[p], p.lower())):
        lines.append("label\t%s\t%s" % (pattern, pattern))
    return "\n".join(out + lines) + "\n", len(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--game", default=os.path.normpath(os.path.join(NATIVE, "..", "IntelEngine-GamePlugin")))
    args = ap.parse_args()
    target = os.path.join(args.game, "SKSE", "Plugins", "IntelEngine", "languages", "_labels_template.tsv")
    text, count = build()
    if args.check:
        if not os.path.exists(target):
            sys.exit("FAIL: %s is missing; run gen_label_template.py" % target)
        if read(target) != text:
            sys.exit("FAIL: _labels_template.tsv is out of date (a sentence the player reads was added or changed); run SKSE/tools/gen_label_template.py")
        print("PASS: _labels_template.tsv has all %d sentences the player reads" % count)
        return
    io.open(target, "w", encoding="utf-8", newline="\n").write(text)
    print("wrote %s (%d sentences)" % (target, count))


if __name__ == "__main__":
    main()
