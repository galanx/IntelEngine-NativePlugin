"""Check typed event schemas and executed outcomes from current Papyrus bytecode.

Usage: python RunStoryTypedEventBytecodeCheck.py <papyrus.exe> <Game Scripts> <history-check.exe>
"""
import importlib
import json
import pathlib
import subprocess
import sys


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    exe, scripts, history = map(pathlib.Path, sys.argv[1:])
    require(exe.is_file() and scripts.is_dir() and history.is_file(),
            "expected papyrus.exe, Game Scripts directory, and history executable")
    runner = importlib.import_module("RunArrivalDeliveryBytecodeCheck")

    def disassemble(filename):
        pex = scripts / filename
        require(pex.is_file(), f"missing current PEX: {pex}")
        return runner.parse_functions(subprocess.run([str(exe), "read", str(pex)], check=True,
            capture_output=True, text=True).stdout)

    files = (disassemble("IntelEngine_NPCTasks.pex"), disassemble("IntelEngine_Core.pex"),
             disassemble("IntelEngine_StoryEngine.pex"))
    class MaintenanceVM(runner.VM):
        def external(self, method, args, script):
            if method == "RegisterForModEvent":
                self.effects.append((method, tuple(args)))
                return None
            return super().external(method, args, script)

    maintenance = MaintenanceVM(files, history)
    maintenance.properties["LastMaintenanceRealTime"] = 0.0
    maintenance.halt_before = "ClearAllPrefix"
    maintenance.invoke("IntelEngine_Core", "Maintenance", (True,))
    require(maintenance.halt_reached, "initial Maintenance did not reach first-load cleanup")
    maintenance.halt_before = None
    maintenance.invoke("IntelEngine_Core", "Maintenance", (False,))
    schemas = [args for method, args in maintenance.effects if method == "RegisterEventSchema"]
    require([s[0] for s in schemas] == ["intel_social", "intel_story"],
            f"Maintenance did not register schemas exactly once after duplicate bootstrap: {schemas!r}")

    vm = runner.VM(files, history)
    for schema in schemas:
        fields = json.loads(schema[3])
        templates = json.loads(schema[4])
        require(fields == [{"name": "line", "type": 0, "required": True,
                            "description": "Event content"}], f"unexpected fields: {fields!r}")
        require(schema[5:] == (False, 120000, True, False),
                f"schema persistence/processing mismatch: {schema!r}")
        require(templates["recent_events"] == "*{{ line }}* ({{ time_desc }})" and
                all(templates[key] == "*{{ line }}*" for key in ("raw", "compact", "verbose")),
                f"schema templates differ from persistent_generic: {templates!r}")

    escaped = 'She said "hello"\\there'
    vm.invoke("IntelEngine_Core", "SendStoryEvent",
              ("intel_social", vm.agent, vm.target, escaped))
    events = [args for method, args in vm.effects if method == "RegisterEvent"]
    require(len(events) == 1 and events[0][0] == "intel_social" and
            json.loads(events[0][1]) == {"line": escaped},
            f"typed event JSON was not escaped/registered correctly: {events!r}")

    class SocialVM(runner.VM):
        def __init__(self, functions, history):
            super().__init__(functions, history)
            self.agent.cell = "far-cell"
            self.target.cell = "player-cell"
            self.extra = runner.Actor("Eorlund", "far-cell")
            self.actors = {"Courier": self.agent, "Recipient": self.target, "Eorlund": self.extra}
            self.related = [self.extra]
            self.reenter_social = False
            self.cancel_on_wait = False
            self.should_act = True
            self.slot_owner = None
            self.properties.update({"Core": "IntelEngine_Core", "StoryEngine": "IntelEngine_StoryEngine",
                "TypeNPCInteractionEnabled": True, "TypeNPCGossipEnabled": True,
                "NPCSocialCooldownHours": 24.0, "NPCSocialArrivalHandling": False})
            self.halt_after_story_event = False

        def invoke(self, script, name, args=()):
            if script == "IntelEngine_Core" and name in (
                    "AllocateSlot", "ClearLinkedRefs", "DebugMsg", "FindFreeAgentSlot", "FindSlotByAgent",
                    "InitializeStuckTrackingForSlot", "InjectFact", "InitOffScreenTracking",
                    "PushDashboardState", "RemoveAllPackages"):
                if name == "InjectFact":
                    self.effects.append(("InjectFact", tuple(args)))
                if name == "FindFreeAgentSlot":
                    return 0
                if name == "FindSlotByAgent":
                    return 0 if self.slot_owner is args[0] else -1
                if name == "AllocateSlot":
                    slot, actor, task_type, target_name, speed, *_ = args
                    self.slot_owner = actor
                    self.properties["SlotStates"][slot] = 1
                    self.properties["SlotTaskTypes"][slot] = task_type
                    self.properties["SlotTargetNames"][slot] = target_name
                    self.properties[f"AgentAlias{slot:02d}"].actor = actor
                return None
            if script == "IntelEngine_StoryEngine" and name in ("AddNPCSocialLog", "AddRecentStoryEvent"):
                self.effects.append((name, tuple(args)))
                return None
            return super().invoke(script, name, args)

        def external(self, method, args, script):
            if method == "RegisterEvent" and self.halt_after_story_event:
                self.halt_after_story_event = False
                self.halt_before = "InjectFact"
            if method == "Wait" and args == [0.5] and self.reenter_social:
                self.reenter_social = False
                self.invoke("IntelEngine_StoryEngine", "OnNPCSocialArrived")
            if method == "Wait" and args == [0.5] and self.cancel_on_wait:
                self.cancel_on_wait = False
                # Slot-index conversion is modeled; cancellation/cleanup bytecode executes.
                self.invoke("IntelEngine_Core", "OnDashboardCancelTask", ("", "", 0, None))
            if method == "StoryResponseShouldAct":
                return self.should_act
            if method == "StoryResponseGetField":
                return json.loads(args[0]).get(args[1], "")
            if method in ("ResolveStoryCandidate", "ResolveDirectorActor"):
                return self.actors.get(args[0])
            if method == "GetConfigFloat":
                return 800.0 if args[1] == "interaction.maxDistance" else 2.0
            if method == "Is3DLoaded":
                return True  # fix66: scene is in SkyrimNet range (1000 <= 0.85*800*2)
            if method == "GetDistance":
                return 1000.0
            if method == "IsPlayerTeammate":
                return False
            if method in ("FormListHas",):
                return False
            if method in ("FormListAdd", "NotifySocialCooldown", "NotifyStoryTypePicked"):
                return None
            if method in ("RenderGossipHeardSection", "RenderGossipToldSection"):
                return ""
            if method == "RandomInt":
                return 1
            if method == "GetRelatedCandidate":
                return self.related.pop(0) if self.related else None
            if method == "GetFormID":
                return {"Courier": 100, "Recipient": 200, "Eorlund": 300}.get(args[0].name, 400)
            if method in ("IntListClear", "IntListAdd"):
                return None
            if method == "ClearSlotState":
                self.slot_owner = None
                self.properties["SlotStates"][args[0]] = 0
            return super().external(method, args, script)

    def social_response(vm, story_type):
        return json.dumps({"type": story_type, "npc": "Courier", "npc2": "Recipient",
            "narration": "They discussed the road", "fact1": "found a map",
            "fact2": "likes ale", "gossip": "heard the Jarl raised taxes"})

    def events_of(vm):
        return [effect[1] for effect in vm.effects if effect[0] == "RegisterEvent"]

    def free_traveler(vm):
        # 3.8 stuck fix: DispatchNPCSocial no longer sends someone who already holds a task slot or a story
        # role (they got a second slot and a jog override on top of what they were doing); for them the
        # meeting counts as having happened off-screen. The base VM starts with the agent in slot 0 and as
        # the active story NPC, so a scenario about a walk-over has to start with a free traveler.
        vm.properties["SlotStates"] = [0] * 5
        vm.properties["AgentAlias00"].actor = None
        vm.properties.update({"IsActive": False, "ActiveStoryNPC": None})
        return vm

    rejected = SocialVM(files, history)
    rejected.should_act = False
    rejected.invoke("IntelEngine_StoryEngine", "OnNPCInteractionResponse",
                    (social_response(rejected, "npc_interaction"), 1))
    require(not events_of(rejected) and not any(e[0] == "InjectFact" for e in rejected.effects),
            "non-action response emitted/applied a social outcome")

    cooldown = SocialVM(files, history)
    cooldown.storage[(cooldown.agent, "Intel_NPCSocialLastPicked")] = 123.0
    cooldown.invoke("IntelEngine_StoryEngine", "OnNPCInteractionResponse",
                    (social_response(cooldown, "npc_interaction"), 1))
    require(not events_of(cooldown) and not any(e[0] == "InjectFact" for e in cooldown.effects),
            "cooldown-blocked response emitted/applied a social outcome")

    social = free_traveler(SocialVM(files, history))
    social.invoke("IntelEngine_StoryEngine", "OnNPCInteractionResponse",
                  (social_response(social, "npc_interaction"), 1))
    require(not events_of(social) and not any(e[0] == "InjectFact" for e in social.effects) and
            social.call_counts.get(("IntelEngine_Core", "InjectGossip"), 0) == 0,
            "social proposal/travel start emitted or stored a completed interaction")
    require(social.properties["NPCSocialTraveler"] is social.agent and
            social.properties["NPCSocialNPC1"] is social.agent,
            "automatic social response did not defer the exchange to arrival")
    social.reenter_social = True
    social.invoke("IntelEngine_StoryEngine", "OnNPCSocialArrived")
    social_events = events_of(social)
    require(len(social_events) == 1 and social_events[0][0] == "intel_social" and
            json.loads(social_events[0][1]) == {"line": "Courier met with Recipient"},
            f"social arrival did not emit exactly once: {social_events!r}")
    require(sum(e[0] == "InjectFact" for e in social.effects) == 2 and
            social.properties["NPCSocialTraveler"] is None,
            "social facts were not deferred/applied once at arrival and cleaned up")
    require(any(effect[0] == "SendTaskNarration" for effect in social.effects),
            "social arrival lost dialogue-driving DirectNarration")

    cancelled = free_traveler(SocialVM(files, history))
    cancelled.properties.update({"IsActive": False, "ActiveStoryNPC": None,
        "ActiveStoryType": "", "ActiveNarration": ""})
    cancelled.invoke("IntelEngine_StoryEngine", "OnNPCInteractionResponse",
                     (social_response(cancelled, "npc_interaction"), 1))
    cancelled.cancel_on_wait = True
    cancelled.invoke("IntelEngine_StoryEngine", "OnNPCSocialArrived")
    require(cancelled.properties["SlotStates"][0] == 0 and
            cancelled.properties["AgentAlias00"].actor is None,
            "Core cancellation did not clear the active social slot")
    require(not events_of(cancelled) and not any(e[0] == "InjectFact" for e in cancelled.effects),
            "cancelled in-flight social interaction emitted an event or applied deferred facts")
    require(not cancelled.properties["NPCSocialArrivalHandling"] and
            not cancelled.properties["IsNPCStoryActive"] and
            cancelled.properties["NPCSocialTraveler"] is None,
            "cancelled social arrival left monitoring latched or blocked the next dispatch")

    gossip = free_traveler(SocialVM(files, history))
    gossip.invoke("IntelEngine_StoryEngine", "OnNPCInteractionResponse",
                  (social_response(gossip, "npc_gossip"), 1))
    require(not events_of(gossip) and
            gossip.call_counts.get(("IntelEngine_Core", "InjectGossip"), 0) == 0 and
            (gossip.target, "Intel_GossipHeard") not in gossip.storage,
            "gossip was stored/emitted before its dispatched meeting")
    gossip.invoke("IntelEngine_StoryEngine", "OnNPCSocialArrived")
    gossip_events = events_of(gossip)
    require(len(gossip_events) == 1 and gossip_events[0][0] == "intel_social" and
            gossip_events[0][2:] == (gossip.agent, gossip.target) and
            json.loads(gossip_events[0][1]) == {"line": "Courier told Recipient a rumor: heard the Jarl raised taxes"},
            f"gossip arrival did not emit with the actual teller/listener: {gossip_events!r}")
    require(gossip.storage[(gossip.target, "Intel_GossipHeard")] == ["heard the Jarl raised taxes"] and
            gossip.storage[(gossip.agent, "Intel_GossipTold")] == ["heard the Jarl raised taxes"],
            "gossip exchange did not update both StorageUtil records")

    # fix 84/96: with a "subject" the gossip is stored and told as "<Subject> <gossip>", unless the gossip
    # names the subject itself (whole word). The case above has no subject and must not gain a leading blank
    # (3.8 review fix in Core.InformantRumor).
    for subject, phrase, expected in (
            ("Balgruuf", "raised the taxes again", "Balgruuf raised the taxes again"),
            ("Balgruuf", "saw Balgruuf raise the taxes", "saw Balgruuf raise the taxes"),
            ("Ria", "was seen leaving Riften at night", "Ria was seen leaving Riften at night")):
        named = free_traveler(SocialVM(files, history))
        named_response = json.loads(social_response(named, "npc_gossip"))
        named_response.update({"subject": subject, "gossip": phrase})
        named.invoke("IntelEngine_StoryEngine", "OnNPCInteractionResponse", (json.dumps(named_response), 1))
        named.invoke("IntelEngine_StoryEngine", "OnNPCSocialArrived")
        named_events = events_of(named)
        require(len(named_events) == 1 and
                json.loads(named_events[0][1]) == {"line": "Courier told Recipient a rumor: " + expected} and
                named.storage[(named.target, "Intel_GossipHeard")] == [expected] and
                named.storage[(named.agent, "Intel_GossipTold")] == [expected],
                f"gossip with subject {subject!r} was not stored and told as {expected!r}: {named_events!r}")

    chain = SocialVM(files, history)
    chain.agent.cell = chain.target.cell = chain.extra.cell = "far-cell"
    chain.invoke("IntelEngine_Core", "InjectGossip",
                 (chain.agent, chain.target, "heard the Jarl raised taxes"))
    chain.invoke("IntelEngine_StoryEngine", "SpreadGossipOffScreen",
                 (chain.agent, chain.target, "heard the Jarl raised taxes"))
    chain_events = events_of(chain)
    require(len(chain_events) == 2 and chain_events[0][2:] == (chain.agent, chain.target) and
            chain_events[1][2:] == (chain.target, chain.extra),
            f"gossip chain did not emit one typed event per stored exchange: {chain_events!r}")

    director = free_traveler(SocialVM(files, history))
    director.properties["SlotStates"] = [0] * 5
    director.properties.update({f"AgentAlias{i:02d}": runner.ReferenceAlias() for i in range(5)})
    director_response = json.dumps({"fact1": "found a map", "fact2": "likes ale",
                                    "gossip": "", "shouldAct": True})
    director_payload = json.dumps({"socialType": "npc_interaction", "npc1Name": "Courier",
        "npc2Name": "Recipient", "narration": "They discussed the road",
        "response": director_response})
    director.invoke("IntelEngine_Core", "OnDashboardDispatchNpcSocial",
                    ("", director_payload, 0.0, None))
    require(not events_of(director) and not any(e[0] == "InjectFact" for e in director.effects) and
            director.properties["NPCSocialTraveler"] is director.agent,
            "Director social travel did not defer facts/event until arrival")
    require([e[1][5] for e in director.effects if e[0] == "AddNPCSocialLog"] == ["Courier: found a map | Recipient: likes ale"],
            "Director npc_interaction log lost its detail")
    director.invoke("IntelEngine_StoryEngine", "OnNPCSocialArrived")
    require(len(events_of(director)) == 1 and
            json.loads(events_of(director)[0][1]) == {"line": "Courier met with Recipient"},
            "Director social arrival did not emit exactly one intel_social event")
    arrival = runner.VM(files, history)
    arrival.properties.update({"Core": "IntelEngine_Core", "IsActive": True, "ArrivalHandling": False,
                               "ActiveStoryNPC": arrival.agent, "ActiveStoryType": "seek_player",
                               "ActiveNarration": "A personal message", "ActiveSecondNPC": None})
    arrival.halt_before = "FinishArrivalWithLinger"
    arrival.invoke("IntelEngine_StoryEngine", "OnStoryNPCArrived")
    story_events = [args for method, args in arrival.effects if method == "RegisterEvent"]
    require(arrival.halt_reached and len(story_events) == 1 and story_events[0][0] == "intel_story" and
            json.loads(story_events[0][1]) == {"line": "Courier arrived to speak with Player"},
            f"executed seek arrival did not emit intel_story before cleanup: {story_events!r}")
    require(any(effect[0] == "SendTaskNarration" for effect in arrival.effects),
            "seek arrival lost dialogue-driving DirectNarration")
    require(not any(effect[0] == "SendPersistentMemory" for effect in arrival.effects),
            "seek arrival retained duplicate persistent_generic memory")

    for sender_name in ("", "Amren"):
        message = SocialVM(files, history)
        message.properties["ActiveStoryType"] = "message"
        message.storage[(message.agent, "Intel_MessageSender")] = sender_name
        message.storage[(message.agent, "Intel_MessageContent")] = "Meet at the bridge"
        message.invoke("IntelEngine_StoryEngine", "OnMessageArrived")
        delivered = events_of(message)
        require(len(delivered) == 1 and delivered[0][0] == "intel_story" and
                delivered[0][2:] == (message.agent, message.player) and
                json.loads(delivered[0][1])["line"] ==
                f"Courier completed delivery of a message from {sender_name or 'Courier'} to Player",
                "message outcome lost the actual courier/sender or emitted twice")
        require(not any(effect[0] == "SendPersistentMemory" for effect in message.effects),
                "message outcome retained its duplicate generic memory")

    quest = SocialVM(files, history)
    quest.slot_owner = quest.agent
    quest.halt_after_story_event = True
    # A story dispatch holds a "story" slot; 3.8: with any other type in that slot the NPC has been given
    # another task during the wait, and the quest offer is not made on top of it.
    quest.properties["SlotTaskTypes"] = ["story"] + [""] * 4
    quest.properties.update({"DispatchGeneration": 1, "QuestActive": True,
        "ActiveStoryType": "quest", "ActiveStoryNPC": quest.agent, "ActiveNarration": "A request for help",
        "QuestDepartureNPC": quest.target, "QuestDepartureFact": "", "QuestGiver": quest.agent,
        "QuestSubType": "combat"})
    quest.invoke("IntelEngine_StoryEngine", "OnQuestNPCArrived")
    quest_events = events_of(quest)
    require(quest.halt_reached and len(quest_events) == 1 and quest_events[0][0] == "intel_story" and
            quest_events[0][2:] == (quest.agent, quest.player) and
            json.loads(quest_events[0][1]) == {"line": "Courier delivered a quest request to Player"},
            f"executed quest courier arrival did not emit one intel_story handoff: {quest_events!r}")
    require(any(effect[0] == "SendTaskNarration" for effect in quest.effects) and
            not any(effect[0] == "SendPersistentMemory" for effect in quest.effects),
            "quest courier handoff lost dialogue narration or retained duplicate start memory")
    print("compiled typed schemas, response deferral, Core cancellation, gossip storage/chain, Director route, seek and quest outcomes passed")


if __name__ == "__main__":
    main()
