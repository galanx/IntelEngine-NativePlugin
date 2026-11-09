"""Check informant fact formatting against current compiled Papyrus bytecode.

Usage: python RunInformantFactBytecodeCheck.py <papyrus.exe> <Game Scripts>
"""
import importlib.util
import pathlib
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
        def invoke(self, script, name, args=()):
            if name == "DispatchToTarget":
                return True
            return super().invoke(script, name, args)
        def external(self, method, args, script):
            if method == "Substring":
                return args[0][args[1]:args[1] + args[2]]
            return super().external(method, args, script)

    failures = []
    for automatic in (False, True):
        for sender, lead in (("Mira", "heard from Mira that "), ("", "witnessed that ")):
            for subject, gossip in (("Snilf", "Snilf was seen near the western gate."),
                                    ("Snilf", "was seen near the western gate."),
                                    ("Snilf", "At the western gate, Snilf was seen."),
                                    ("Snilf", "snilf was seen near the western gate."),
                                    ("Snilf [the alchemist]", "Snilf was seen near the western gate."),
                                    ("Snilf [the alchemist]", "was seen near the western gate.")):
                vm = FactVM(({}, core, story), pathlib.Path("unused"))
                vm.target.name = "Snilf"
                response = {"should_act": True, "type": "informant", "npc": vm.target.name,
                            "narration": "Gossip from town", "subject": subject,
                            "gossip": gossip, "sender": sender}
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
                untagged_subject = subject.split(" [", 1)[0]
                expected = lead + (gossip if untagged_subject in gossip else subject + " " + gossip)
                facts = [e[1][1] for e in vm.effects if e[0] == "InjectFact" and e[1][0] is recipient]
                subject_fact = [gossip] if subject == "Snilf" else []
                if facts != (subject_fact + [expected] if automatic else [expected]):
                    failures.append((automatic, sender, subject, gossip, facts, expected))
    require(not failures, f"informant entrypoint fact mismatch: {failures!r}")
    print("actual PEX informant facts: both dispatch entrypoints, name/no-name/case, sender/no-sender PASS")


if __name__ == "__main__":
    main()
