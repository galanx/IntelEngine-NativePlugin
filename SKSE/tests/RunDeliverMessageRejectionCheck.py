"""Check unresolved delivery rejection and actor-target delivery via compiled Papyrus bytecode.

Usage: python RunDeliverMessageRejectionCheck.py <papyrus.exe> <Game Scripts> <history-check.exe>
"""
import pathlib
import subprocess
import sys

from RunArrivalDeliveryBytecodeCheck import Actor, ReferenceAlias, VM, parse_functions, require


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    exe, scripts, history = map(pathlib.Path, sys.argv[1:])
    require(exe.is_file() and scripts.is_dir() and history.is_file(),
            "expected papyrus.exe, Game Scripts directory, and history executable")
    files = tuple(parse_functions(subprocess.run([str(exe), "read", str(scripts / filename)],
        check=True, capture_output=True, text=True).stdout) for filename in
        ("IntelEngine_NPCTasks.pex", "IntelEngine_Core.pex", "IntelEngine_StoryEngine.pex"))

    class DeliveryVM(VM):
        def __init__(self, recipient, suggestion=""):
            super().__init__(files, history)
            self.recipient = recipient
            self.suggestion = suggestion
            self.properties.update({"SlotStates": [0] * 5, "SlotTaskTypes": [""] * 5,
                "SlotTargetNames": [""] * 5, "SlotSpeeds": [0] * 5,
                "SlotDeadlines": [0.0] * 5})
            self.active_task = False
            self.properties.update({f"AgentAlias{i:02d}": ReferenceAlias(self.agent if i == 0 else None)
                                   for i in range(5)})
            self.properties.update({f"TargetAlias{i:02d}": ReferenceAlias() for i in range(5)})
            self.agent.cell = "courier-cell"
            if recipient is not None:
                recipient.cell = "distant-recipient-cell"

        def invoke(self, script, name, args=()):
            if script == "IntelEngine_Core" and name == "ShowTaskConfirmationForAction":
                return 0
            return super().invoke(script, name, args)

        def external(self, method, args, script):
            if method == "FindNPCByNameNear": return self.recipient
            if method == "GetNPCNameSuggestion": return self.suggestion
            if method == "IsPlayerTeammate": return False
            if method == "IsInFaction": return bool(args[0] is self.agent and self.active_task)
            if method == "SetHomeDoorAccess": return None
            if method == "ResolveAnyDestination":
                self.effects.append(("ResolveAnyDestination", tuple(args)))
                return None
            if method == "RegisterEvent":
                self.effects.append(("RegisterEvent", tuple(args)))
                return None
            return super().external(method, args, script)

    unresolved = DeliveryVM(None)
    unresolved.active_task = True
    unresolved.properties.update({"SlotStates": [1, 0, 0, 0, 0],
        "SlotTaskTypes": ["travel", "", "", "", ""],
        "SlotTargetNames": ["Old destination", "", "", "", ""]})
    unresolved.storage.update({(unresolved.agent, "Intel_TaskType"): "travel",
        (unresolved.agent, "Intel_Target"): "Old destination", (unresolved.agent, "Intel_Slot"): 0})
    result = unresolved.invoke("IntelEngine_NPCTasks", "DeliverMessage",
                               (unresolved.agent, "Jarl Balgruff", "A message", "none", "none"))
    require(result is False, "unresolved recipient was accepted")
    require(not any(event[0] == "SendTaskNarration" for event in unresolved.effects),
            "unresolved-recipient rejection emitted NPC DirectNarration")
    require(not any(event[0] == "RegisterEvent" for event in unresolved.effects),
            "unresolved-recipient rejection registered a task-start event")
    require(unresolved.properties["SlotStates"][0] == 1 and
            unresolved.properties["SlotTaskTypes"][0] == "travel" and
            unresolved.storage[(unresolved.agent, "Intel_TaskType")] == "travel" and
            unresolved.call_counts.get(("IntelEngine_Core", "ClearSlot"), 0) == 0,
            "unresolved delivery modified the agent's existing Travel task")
    require(any(event[0] == "Trace" and event[1].endswith("Jarl Balgruff could not be found.")
                for event in unresolved.effects),
            "unresolved recipient did not report target-first rejection to the player")
    suggested = DeliveryVM(None, "Jarl Balgruuf")
    require(suggested.invoke("IntelEngine_NPCTasks", "DeliverMessage",
                             (suggested.agent, "Jarl Balgruff", "A message", "none", "none")) is False,
            "delivery with a name suggestion was accepted")
    require(any(event[0] == "Trace" and
                event[1].endswith("Jarl Balgruff could not be found. The closest known name is Jarl Balgruuf.")
                for event in suggested.effects),
            "delivery suggestion did not lead with the requested target name")

    for method, args in (
        ("FetchNPC", ("Jarl Balgruff", "none")),
        ("SearchForActor", ("Jarl Balgruff", 0)),
        ("EscortTarget", ("Jarl Balgruff", "home", 0)),
    ):
        sibling = DeliveryVM(None)
        require(sibling.invoke("IntelEngine_NPCTasks", method, (sibling.agent, *args)) is False,
                f"unresolved {method} target was accepted")
        require(any(event[0] == "SendTaskNarration" and
                    event[1][0] == "Jarl Balgruff could not be found."
                    for event in sibling.effects),
                f"unresolved {method} did not narrate target-first rejection")


    recipient = Actor("Jarl Balgruuf", "far-cell")
    resolved = DeliveryVM(recipient)
    resolved.halt_before = "SetLinkedRef"
    resolved.invoke("IntelEngine_NPCTasks", "DeliverMessage",
                    (resolved.agent, "Jarl Balgruuf", "A message", "none", "none"))
    require(resolved.halt_reached, "resolved delivery did not reach actor travel setup")
    require(resolved.storage[(resolved.agent, "Intel_TargetNPC")] is recipient and
            resolved.storage[(resolved.agent, "Intel_DestMarker")] is recipient,
            "resolved distant recipient was not retained as the delivery actor target")
    require(not any(event[0] == "ResolveAnyDestination" for event in resolved.effects),
            "actor-target delivery unexpectedly resolved a named location")
    print("compiled DeliverMessage rejection and distant actor-target paths passed")


if __name__ == "__main__":
    main()
