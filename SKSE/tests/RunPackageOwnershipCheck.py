"""Whoever hands an NPC one of IntelEngine's packages without a task slot is known to the sweep of stale packages.

Core's SweepStalePackages looks, once a game hour, at everyone who runs one of six packages (the travel packages at
a walk, a jog and a run, the stalking one, the two sandboxes) and releases whoever nothing in the mod accounts for
(OwnsPackagesOf) on two rounds in a row. A script that gives such a package away and is not asked there loses it
again three to six minutes of play later, and the NPC stops where they stand:

- the people of a road meeting, on the road (2026-11-18),
- the soldiers of a battle: reserves on their way up and everyone leaving the field afterwards. The beaten, who were
  to run, stood about among the winners and the dead (2026-11-21).

So: every script that hands out one of the six is listed here with the words OwnsPackagesOf knows it by, and those
words have to be in OwnsPackagesOf. A new script that hands one out fails this check until it is accounted for.

Usage: python RunPackageOwnershipCheck.py <Source/Scripts>
"""
import pathlib
import re
import sys

WATCHED = ("TravelPackage_Walk", "TravelPackage_Jog", "TravelPackage_Run", "TravelPackage_Stalk", "SandboxPackage", "SandboxNearPlayerPackage")

# packages of ours the sweep does not look for (a life's own sandbox, the follow-the-player package of a thread)
NOT_WATCHED = ("LifeSandbox", "followPackage")

# script -> what in Core.OwnsPackagesOf accounts for the packages it hands out
ACCOUNTED = {
    "IntelEngine_Core": ("FindSlotByActor(npc)", "Intel_WalkHomeActors"),
    "IntelEngine_Travel": ("FindSlotByActor(npc)",),
    "IntelEngine_NPCTasks": ("NPCTasks.HoldsActor(npc)",),
    "IntelEngine_StoryEngine": ("StoryEngine.HoldsActor(npc)",),
    "IntelEngine_Quests": ("Intel_RoadVenture", "Intel_WalkHomeActors"),
    "IntelEngine_Arcs": ("Intel_ArcThug",),
    "IntelEngine_Battle": ("Battle.HoldsActor(npc)",),
}

# a script asked through HoldsActor has to have one, and it has to look at these lists of its own
HOLDS = {
    "IntelEngine_Battle": ("SpawnedSoldiers", "ManifestActors"),
    "IntelEngine_StoryEngine": (),
    "IntelEngine_NPCTasks": (),
}


def function_body(text, name):
    found = re.search(r"^[ \t]*(?:\w+(?:\[\])?[ \t]+)?Function[ \t]+" + re.escape(name) + r"\b.*?^[ \t]*EndFunction", text, re.S | re.M | re.I)
    return found.group(0) if found else ""


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    src = pathlib.Path(sys.argv[1])
    if not src.is_dir():
        raise SystemExit("expected the Source/Scripts directory")
    core = (src / "IntelEngine_Core.psc").read_text(encoding="utf-8", errors="replace")
    owns = function_body(core, "OwnsPackagesOf")
    if not owns:
        raise SystemExit("IntelEngine_Core.psc has no OwnsPackagesOf")
    sweep = function_body(core, "SweepStalePackages")
    for package in WATCHED:
        if package not in sweep:
            raise SystemExit(f"SweepStalePackages no longer looks for {package}: this check's list is out of date")

    problems = []
    givers = {}
    for path in sorted(src.glob("IntelEngine*.psc")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for number, line in enumerate(text.splitlines(), 1):
            code = line.split(";", 1)[0]
            call = re.search(r"ActorUtil\.AddPackageOverride\(([^,]+),([^,]+),", code)
            if not call:
                continue
            package = call.group(2).strip().split(".")[-1]
            # Anything that is not plainly one of the packages the sweep leaves alone counts: a variable or a call
            # (travelPkg, pace, GetTravelPackage(speed)) chooses between the watched ones.
            if package not in NOT_WATCHED:
                givers.setdefault(path.stem, []).append(number)

    if len(givers) < 5:
        raise SystemExit(f"only {len(givers)} scripts hand out packages: are these the plugin's scripts?")
    for script, lines in givers.items():
        if script not in ACCOUNTED:
            problems.append(f"{script}.psc hands out a watched package (line {lines[0]}) and OwnsPackagesOf is not known to account for it: "
                            "add it to OwnsPackagesOf and to this check's list")
            continue
        for words in ACCOUNTED[script]:
            if words not in owns:
                problems.append(f"OwnsPackagesOf no longer asks for {script}'s packages ({words})")
    for script, lists in HOLDS.items():
        if script not in givers:
            continue
        body = function_body((src / f"{script}.psc").read_text(encoding="utf-8", errors="replace"), "HoldsActor")
        if not body:
            problems.append(f"{script}.psc has no HoldsActor for the sweep to ask")
            continue
        for name in lists:
            if name not in body:
                problems.append(f"{script}.HoldsActor does not look at {name}")
    if problems:
        raise SystemExit("packages the sweep of stale packages would take away from those they were given to:\n  " + "\n  ".join(problems))
    print(f"PackageOwnership check passed ({len(givers)} scripts hand out watched packages, all accounted for)")


if __name__ == "__main__":
    main()
