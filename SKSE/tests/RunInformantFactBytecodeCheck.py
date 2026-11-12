"""Check informant fact formatting against current compiled Papyrus bytecode.

Usage: python RunInformantFactBytecodeCheck.py <papyrus.exe> <Game Scripts>
"""
import importlib.util
import pathlib
import re
import subprocess
import sys


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    papyrus, scripts = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    require(papyrus.is_file() and scripts.is_dir(), "expected papyrus.exe and Game Scripts directory")
    runner = pathlib.Path(__file__).with_name("RunArrivalDeliveryBytecodeCheck.py")
    spec = importlib.util.spec_from_file_location("arrival_bytecode", runner)
    arrival = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(arrival)

    def disassemble(name):
        pex = scripts / (name + ".pex")
        require(pex.is_file(), f"missing current PEX: {pex}")
        return arrival.parse_functions(subprocess.run([str(papyrus), "read", str(pex)],
            check=True, capture_output=True, text=True).stdout)

    core, story = disassemble("IntelEngine_Core"), disassemble("IntelEngine_StoryEngine")

    class FactVM(arrival.DirectorVM):
        dispatched = None
        def invoke(self, script, name, args=()):
            if name == "DispatchToTarget":
                self.dispatched = args[2]
                return True
            return super().invoke(script, name, args)
        def external(self, method, args, script):
            if method == "Find":  # Papyrus Find is case-insensitive
                return args[0].lower().find(args[1].lower(), args[2] if len(args) > 2 else 0)
            return super().external(method, args, script)

    def contains(text, name):
        return name.lower() in text.lower()

    def expected_fact(subject, rumor, gossip, sender, informant):
        untagged = subject.split(" [", 1)[0]
        text = rumor or (gossip if contains(gossip, untagged) else subject + " " + gossip)
        word = lambda n: r"(?<![A-Za-z])" + re.escape(n) + r"(?![A-Za-z])"  # whole word, as Papyrus ReplaceText
        text = re.sub(word(informant + "'s"), "my", text, flags=re.I)
        text = re.sub(word(informant), "me", text, flags=re.I)
        if not sender:
            return "witnessed that " + text
        if sender == untagged:
            return sender + " told me that " + text
        return "heard from " + sender + " that " + text

    cases = [  # (subject, rumor, gossip) -- rumor-only, legacy gossip-only
        ("Sinmir", "Mikael mocked Sinmir's views on the city's rot", ""),
        ("Sinmir", "Sinmir sold Ria's ale in Riften, Victoria's mead and RIA's wine to Ria", ""),
        ("Sinmir [the skald]", "Sinmir argued with Hulda about Hulda's town", ""),
        ("Snilf", "", "Snilf was seen near the western gate."),
        ("Snilf", "", "was seen near the western gate."),
        ("Snilf", "", "At the western gate, Snilf was seen."),
        ("Snilf [the alchemist]", "", "was seen near the western gate."),
    ]
    failures = []
    for automatic in (False, True):
        for sender, informant in (("Mira", "Hulda"), ("", "Hulda"), ("Sinmir", "Hulda"), ("Snilf", "Hulda"), ("Mira", "Ria")):
            for subject, rumor, gossip in cases:
                vm = FactVM(({}, core, story), pathlib.Path("unused"))
                vm.target.name = vm.agent.name = informant
                response = {"should_act": True, "type": "informant", "npc": informant,
                            "narration": "Gossip from town", "subject": subject,
                            "rumor": rumor, "gossip": gossip, "sender": sender}
                if automatic:
                    vm.invoke("IntelEngine_StoryEngine", "OnDungeonMasterResponse",
                              (arrival.json.dumps(response), 1))
                    recipient = vm.target
                else:
                    payload = {"storyType": "informant", "npcName": vm.agent.name,
                               "narration": response["narration"],
                               "response": arrival.json.dumps(response)}
                    vm.invoke("IntelEngine_Core", "OnDashboardDispatchStory",
                              ("", arrival.json.dumps(payload), 0.0, None))
                    recipient = vm.agent
                expected = expected_fact(subject, rumor, gossip, sender, informant)
                text = rumor or (gossip if contains(gossip, subject.split(" [", 1)[0]) else subject + " " + gossip)
                line = (informant + " came to tell Player what " + sender + " told them: " + text) if sender                     else informant + " came to tell Player what they witnessed: " + text
                facts = [e[1][1] for e in vm.effects if e[0] == "InjectFact"]  # nothing may land on the subject
                owners = {id(e[1][0]) for e in vm.effects if e[0] == "InjectFact"}
                if facts != [expected] or owners != {id(recipient)} or vm.dispatched != line:
                    failures.append((automatic, sender, subject, rumor, gossip, facts, expected, vm.dispatched, line))
    require(not failures, f"informant entrypoint mismatch: {failures!r}")

    # Arrival: IE-built line is narrated as-is and stored for the "Why I came to find" bio section.
    vm = FactVM(({}, core, story), pathlib.Path("unused"))
    line = "Hulda came to tell Player what Mira told them: Sinmir mocked Mikael"
    vm.properties.update({"IsActive": True, "ArrivalHandling": False, "ActiveStoryNPC": vm.target,
                          "ActiveStoryType": "informant", "ActiveNarration": line, "DispatchGeneration": 1})
    vm.invoke("IntelEngine_StoryEngine", "OnStoryNPCArrived")
    narrated = [e[1][0] for e in vm.effects if e[0] == "SendTaskNarration"]
    require(narrated == [line], f"arrival narration mismatch: {narrated!r}")
    require(vm.storage.get((vm.target, "Intel_ArrivalPurpose")) == line, "arrival purpose not stored for the bio section")
    print("actual PEX informant: both dispatch entrypoints, rumor/legacy gossip, tagged subject, sender/none/==subject, "
          "'me'/'my' substitution, IE-built arrival line, no subject injection, arrival purpose stored PASS")


if __name__ == "__main__":
    main()
