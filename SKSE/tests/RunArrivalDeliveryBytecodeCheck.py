"""Execute bounded arrival paths from the current compiled Papyrus bytecode.

Usage: python RunArrivalDeliveryBytecodeCheck.py <papyrus.exe> <Game Scripts> <history-check.exe>
"""
import ast
import pathlib
import json
import re
import subprocess
import sys


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def split_args(text):
    out, start, depth, quote, i = [], 0, 0, None, 0
    while i < len(text):
        ch = text[i]
        if quote:
            if ch == "\\":
                i += 2
                continue
            if ch == quote and (quote != "'" or i + 1 < len(text) and text[i + 1] == ")"):
                quote = None
        elif ch in "'\"":
            quote = ch
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "," and depth == 0:
            out.append(text[start:i].strip())
            start = i + 1
        i += 1
    if text[start:].strip(): out.append(text[start:].strip())
    return out


def atom(text):
    text = text.strip()
    m = re.fullmatch(r"(ident|string|integer|float|boolean)\((.*)\)", text, re.S)
    if m:
        kind, value = m.groups()
        if kind == "ident":
            return ("var", value)
        if kind == "string":
            try:
                return ast.literal_eval(value)
            except (SyntaxError, ValueError):
                return value[1:-1]
        if kind == "integer":
            return int(value)
        if kind == "float":
            return float(value)
        return value == "01"
    if text == "none":
        return None
    raise ValueError("unsupported bytecode operand: " + text)


def parse_functions(text):
    starts = list(re.finditer(r"\n\s*name: '([^']+)'\n", text))
    funcs = {}
    for idx, m in enumerate(starts):
        end = starts[idx + 1].start() if idx + 1 < len(starts) else len(text)
        section = text[m.end():end]
        head = re.search(r"instructions count: '?(\d+)'?\s*\n", section)
        if not head:
            continue
        params = re.search(r"params count: '(\d+)'\s*\n(.*?)locals count:", section, re.S)
        names = []
        if params:
            names = [line.strip().split()[-1] for line in params.group(2).splitlines() if line.strip()]
            require(len(names) == int(params.group(1)), f"parameter parse mismatch in {m.group(1)}")
        instructions = []
        for op in re.finditer(r"^\s*opcode: '([^']+)', args: \[(.*?)\]\s*$",
                              section[head.end():], re.M | re.S):
            instructions.append((op.group(1), [atom(x) for x in split_args(op.group(2))]))
        require(len(instructions) == int(head.group(1)), f"disassembly parse mismatch in {m.group(1)}")
        funcs[m.group(1)] = (names, instructions)
    return funcs


class ReferenceAlias:
    def __init__(self, actor=None):
        self.actor = actor


class Actor:
    def __init__(self, name, cell):
        self.name, self.cell = name, cell


class VM:
    """Small interpreter: production methods execute; engine/native edges are explicit stubs."""
    def __init__(self, functions, history_exe, result_spelling=None):
        self.scripts = {"IntelEngine_NPCTasks": functions[0], "IntelEngine_Core": functions[1]}
        if len(functions) > 2: self.scripts["IntelEngine_StoryEngine"] = functions[2]
        self.storage, self.effects = {}, []
        self.history_exe, self.result_spelling = history_exe, result_spelling
        self.properties = {"MAX_SLOTS": 5, "SlotTaskTypes": ["deliver_message"] + [""] * 4,
                           "SlotTargetNames": ["Recipient"] + [""] * 4,
                           "SlotStates": [8] + [0] * 4, "SlotSpeeds": [0] * 5,
                           "SlotDeadlines": [0.0] * 5}
        self.player, self.agent, self.target = Actor("Player", "player-cell"), Actor("Courier", "player-cell"), Actor("Recipient", "player-cell")
        aliases = [ReferenceAlias(self.agent if i == 0 else None) for i in range(5)]
        self.properties.update({f"AgentAlias{i:02d}": aliases[i] for i in range(5)})
        self.properties.update({f"TargetAlias{i:02d}": ReferenceAlias() for i in range(5)})
        self.properties.update({"NPCTickIntervalHours": 1.5, "MONITOR_INTERVAL": 5.0, "IDLE_POLL_INTERVAL": 30.0})
        self.properties.update({"IsActive": True, "ArrivalHandling": False, "DispatchGeneration": 0,
            "ActiveStoryNPC": self.agent, "ActiveStoryType": "seek_player",
            "ActiveSecondNPC": None, "ActiveNarration": "A message about the road"})
        self.call_counts, self.reenter_story = {}, False
        self.storage.update({(self.agent, "Intel_Message"): "Meet me at the bridge",
            (self.agent, "Intel_Result"): "", (self.agent, "Intel_DeliveryMeetLocation"): "none",
            (self.agent, "Intel_DeliveryMeetTime"): "none", (self.agent, "Intel_WasFollower"): 0,
            (self.agent, "Intel_TaskType"): "deliver_message", (self.agent, "Intel_Target"): "Recipient",
            (self.agent, "Intel_Slot"): 0, (self.agent, "Intel_State"): 8, (self.agent, "Intel_Speed"): 0,
            (self.agent, "Intel_TargetNPC"): self.target, (self.agent, "Intel_TaskHistory"): [],
            (self.agent, "Intel_TaskHistoryTime"): [], (self.agent, "Intel_TaskCooldown"): 0.0})
        self.stop_on_dispatch_reset = self.dispatch_reset_seen = False
        self.halt_before, self.halt_reached = None, False
        self.arrival_interleave_method = self.arrival_interleave_action = None
        self.replacement = None

    def val(self, operand, env):
        if not isinstance(operand, tuple): return operand
        name = operand[1]
        if name.startswith("::") and name.endswith("_var"):
            return self.properties.get(name[2:-4], env.get(name))
        if name in env: return env[name]
        return self.properties.get(name.removeprefix("::").removesuffix("_var"))

    def invoke(self, script, name, args=()):
        params, code = self.scripts[script][name]
        require(len(params) == len(args), f"call arity mismatch for {script}.{name}")
        self.call_counts[(script, name)] = self.call_counts.get((script, name), 0) + 1
        env = {"self": script, "::Core_var": "IntelEngine_Core", "::Schedule_var": None,
               "::NoneVar": None, "Core": "IntelEngine_Core", "player": self.player}
        env.update(zip(params, args))
        return self.run(script, name, code, env)

    def storage_call(self, method, args):
        pair = (args[0], args[1])
        if method.startswith("Has"): return pair in self.storage
        if method.startswith("Get"):
            value = self.storage.get(pair, args[2] if len(args) > 2 else
                                     {"GetStringValue": "", "GetIntValue": 0, "GetFloatValue": 0.0}.get(method))
            if method == "GetStringValue" and pair[1] == "Intel_Result" and value == "delivered" and self.result_spelling:
                value = self.result_spelling
            if method == "GetStringValue" and pair[1] in ("Intel_Result", "Intel_Message", "Intel_DeliveryMeetLocation"):
                self.effects.append(("history-read", pair[1], value))
            return value
        if method.startswith("Set"):
            self.storage[pair] = args[-1]
            if pair[1] == "Intel_Result": self.effects.append(("result-write", args[-1]))
            return None
        if method.startswith("Unset"):
            self.storage.pop(pair, None)
            self.effects.append(("unset", pair[1]))
            return None
        if method in ("StringListAdd", "FloatListAdd"): self.storage.setdefault(pair, []).append(args[2]); return None
        if method in ("StringListCount", "FloatListCount"): return len(self.storage.get(pair, []))
        if method in ("StringListRemoveAt", "FloatListRemoveAt"): self.storage.get(pair, []).pop(args[2]); return None
        if method in ("StringListToArray", "FloatListToArray"): return list(self.storage.get(pair, []))
        raise ValueError("unsupported StorageUtil native " + method)

    def external(self, method, args, script):
        if method == "IsDashboardOpen": return False
        if method == self.arrival_interleave_method:
            self.arrival_interleave_method = None
            if self.arrival_interleave_action == "replace":
                self.replacement = Actor("Replacement", "replacement-cell")
                self.properties.update({"IsActive": True, "ArrivalHandling": False,
                    "DispatchGeneration": self.properties["DispatchGeneration"] + 1,
                    "ActiveStoryNPC": self.replacement, "ActiveStoryType": "seek_player"})
            elif self.arrival_interleave_action == "phase":
                self.properties["ActiveStoryType"] = "quest_guide"
            else:
                self.invoke("IntelEngine_StoryEngine", "CleanupStoryDispatch")
            self.arrival_interleave_action = None
        if method == "GetDistance":
            self.effects.append(("GetDistance", tuple(args)))
            require(args[0] is not None and args[1] is not None,
                    "arrival check dereferenced a cleared NPC or target")
            return 100.0
        if method in ("RegisterEvent", "RegisterEventSchema"):
            self.effects.append((method, tuple(args)))
            return 0
        if method == "StringEscapeJson":
            return json.dumps(args[0], ensure_ascii=False)[1:-1]
        if method == "IsPlayerInBlockedLocation": return False
        if method == "IsPlayerInWhitelistedLocation": return True
        if method in ("HasStringValue", "HasIntValue", "HasFloatValue", "GetStringValue", "GetIntValue",
                      "GetFloatValue", "GetFormValue", "SetStringValue", "SetIntValue", "SetFloatValue",
                      "SetFormValue", "UnsetStringValue", "UnsetIntValue", "UnsetFloatValue",
                      "UnsetFormValue", "StringListAdd", "FloatListAdd", "StringListCount", "FloatListCount",
                      "StringListRemoveAt", "FloatListRemoveAt", "StringListToArray", "FloatListToArray"):
            if method == "SetStringValue" and self.reenter_story:
                self.reenter_story = False
                self.invoke("IntelEngine_StoryEngine", "OnStoryNPCArrived")
            return self.storage_call(method, args)
        if method == "GetPlayer": return self.player
        if method == "GetActorReference": return args[0].actor
        if method == "ForceRefTo": args[0].actor = args[1]; return None
        if method == "Clear": args[0].actor = None; return None
        if method == "GetDisplayName": return args[0].name
        if method == "GetParentCell": return args[0].cell
        if method == "IsPlayerInDangerousLocation": return False
        if method == "CheckHoldRestriction": return True
        if method == "GetCurrentGameTime": return 123.0
        if method == "GetArrivalThreshold":
            self.effects.append(("GetArrivalThreshold", tuple(args)))
            return 300.0
        if method == "GetCurrentRealTime": return 456.0
        if method == "FriendlyKey":
            return subprocess.run([str(self.history_exe), "--friendly", args[0]],
                check=True, capture_output=True, text=True).stdout.rstrip("\r\n")
        if method == "BuildTaskHistoryDesc":
            self.effects.append(("formatter-args", tuple(args)))
            return subprocess.run([str(self.history_exe), "--describe", *map(str, args)],
                check=True, capture_output=True, text=True).stdout.rstrip("\r\n")
        if method in ("IsDead", "IsDisabled", "Is3DLoaded", "IsInCombat"): return False
        if method == "GetFormID": return 1234
        if method == "GetPoliticsTickInterval": return 6.0
        if method == "IntListFind": return -1
        if method in ("IntListAdd", "GetForm"): return None
        if method in ("FormListCount", "IntListCount"):
            return len(self.storage.get((args[0], args[1]), []))
        if method == "EncodeRenderedCache": return args[0]
        if method == "RenderTaskHistorySection": return ""
        if method == "IsIndexLoaded": return True
        if method == "ClearSlotState":
            self.effects.append((method, tuple(args)))
            return None
        if method in ("ResetDepartureSlot", "ResetOffScreenSlot", "DisarmProximityArrival",
                      "ResetStuckSlot", "ClearScheduleSlotByAgent", "ClearHomeDoorAccessForCell",
                      "SetHomeDoorAccessForCell", "ClearPackageOverride", "SetActorValue"):
            self.effects.append((method, tuple(args)))
            return None
        if method in ("Trace", "Notification"):
            self.effects.append(("Trace", args[0] if args else ""))
            return None
        if method == "DirectNarration":
            self.effects.append(("SendTaskNarration", tuple(args)))
            return 0
        if method == "RegisterPersistentEvent":
            self.effects.append(("SendPersistentMemory", tuple(args)))
            return 0
        if method in ("NotifyPlayer", "DebugMsg", "RemoveAllPackages", "RemoveIntelPackages",
                      "ClearLinkedRefs", "RemoveFromFaction", "SetPlayerTeammate", "EvaluatePackage",
                      "SetLookAt", "MarkSlotProcessing", "RegisterForSingleUpdate", "RegisterForSingleUpdateGameTime", "Wait",
                      "SetLinkedRef", "AddPackageOverride", "InitializeStuckTrackingForSlot",
                      "ClearScheduleSlot", "EnsureBuildingAccess"):
            self.effects.append((method, tuple(args)))
            return None
        if method == "IsPlayerInOwnHome": return False
        if method in ("AddToFaction", "StopScheduler", "UpdateSlotState", "SetSlotSpeed",
                      "InitOffScreenTracking", "InitOffScreenTravel", "MarkSystemPending",
                      "ClearSystemPending", "ArmProximityArrival", "RemovePackageOverride"):
            self.effects.append((method, tuple(args)))
            return None
        raise ValueError(f"unmodeled external call {script}.{method}({args!r})")

    def run(self, script, name, code, env):
        pc, steps = 0, 0
        while pc < len(code):
            steps += 1
            require(steps < 20000, f"instruction limit exceeded in {script}.{name}")
            op, a = code[pc]
            v = lambda x: self.val(x, env)
            nextpc = pc + 1
            if op in ("assign", "cast"):
                env[a[0][1]] = v(a[1])
                if a[0][1].startswith("::") and a[0][1].endswith("_var"):
                    self.properties[a[0][1][2:-4]] = env[a[0][1]]
                elif a[0][1] in self.properties:
                    self.properties[a[0][1]] = env[a[0][1]]
                if self.stop_on_dispatch_reset and script == "IntelEngine_StoryEngine" and name == "DispatchToTarget" and a[0][1] == "::ArrivalHandling_var" and not env[a[0][1]]:
                    self.dispatch_reset_seen = True
                    return None
            elif op in ("iadd", "fadd", "fsub", "fmul", "fdiv"):
                x, y = v(a[1]), v(a[2])
                env[a[0][1]] = {"iadd": lambda: x + y, "fadd": lambda: x + y,
                    "fsub": lambda: x - y, "fmul": lambda: x * y, "fdiv": lambda: x / y}[op]()
            elif op == "fneg": env[a[0][1]] = -v(a[1])
            elif op == "strcat":
                env[a[0][1]] = str(v(a[1])) + str(v(a[2]))
            elif op.startswith("cmp_"):
                x, y = v(a[1]), v(a[2])
                if isinstance(x, str) and isinstance(y, str): x, y = x.casefold(), y.casefold()
                env[a[0][1]] = {"cmp_eq": lambda: x == y, "cmp_ne": lambda: x != y,
                    "cmp_lt": lambda: x < y, "cmp_le": lambda: x <= y,
                    "cmp_gt": lambda: x > y, "cmp_ge": lambda: x >= y}[op]()
            elif op == "not": env[a[0][1]] = not bool(v(a[1]))
            elif op in ("jmp", "jmpf", "jmpt"):
                take = op == "jmp" or (bool(v(a[0])) if op == "jmpt" else not bool(v(a[0])))
                if take: nextpc = pc + v(a[-1])
            elif op == "ret": return v(a[0]) if a else None
            elif op == "propget":
                prop = a[0][1].removeprefix("::").removesuffix("_var")
                env[a[2][1]] = self.properties.get(prop)
            elif op == "propset":
                prop = a[0][1].removeprefix("::").removesuffix("_var")
                self.properties[prop] = v(a[2])
            elif op == "array_length": env[a[0][1]] = len(v(a[1]))
            elif op == "array_getelement": env[a[0][1]] = v(a[1])[v(a[2])]
            elif op == "array_setelement":
                prop = a[0][1].removeprefix("::").removesuffix("_var")
                v(a[0])[v(a[1])] = v(a[2])
            elif op in ("callmethod", "callstatic"):
                method, result = a[0 if op == "callmethod" else 1][1], a[2]
                argc = v(a[3]); call_args = [v(x) for x in a[4:4 + argc]]
                receiver = v(a[1]) if op == "callmethod" else None
                target = receiver if isinstance(receiver, str) and receiver in self.scripts else None
                if method == self.halt_before:
                    self.halt_reached = True
                    return None
                if method in self.scripts.get(target, {}):
                    value = self.invoke(target, method, call_args)
                else:
                    native_args = ([receiver] if isinstance(receiver, (Actor, ReferenceAlias)) or method == "GetDistance" else []) + call_args
                    value = self.external(method, native_args, script)
                if isinstance(result, tuple): env[result[1]] = value
            else:
                raise ValueError(f"unsupported opcode {script}.{name} pc={pc}: {op} {a!r}")
            pc = nextpc


class DirectorVM(VM):
    def __init__(self, functions, history):
        super().__init__(functions, history)
        self.agent.name = "Nazeem"
        self.player.cell = None
        self.storage.clear()
        self.properties.update({"StoryEngine": "IntelEngine_StoryEngine", "NPCTasks": "IntelEngine_NPCTasks",
            "SlotStates": [0] * 5, "SlotTaskTypes": [""] * 5, "SlotTargetNames": [""] * 5,
            "IsActive": False, "ActiveStoryNPC": None, "ActiveStoryType": "", "ActiveNarration": ""})
        self.properties.update({f"AgentAlias{i:02d}": ReferenceAlias() for i in range(5)})

    def invoke(self, script, name, args=()):
        if script == "IntelEngine_Core" and name == "PushDashboardState":
            self.effects.append(("PushDashboardState", ()))
            return None
        if script == "IntelEngine_Core" and name == "InjectFact":
            if args[1] != "": self.effects.append((name, tuple(args)))
            return None
        if (script == "IntelEngine_Core" and name in
                ("InitializeStuckTrackingForSlot", "InitOffScreenTracking")) or (
                script == "IntelEngine_StoryEngine" and name == "ReapplyTravelPackage"):
            self.effects.append((name, tuple(args)))
            return None
        return super().invoke(script, name, args)

    def external(self, method, args, script):
        if method == "StoryResponseGetField": return json.loads(args[0]).get(args[1], "")
        if method == "ResolveDirectorActor": return self.agent if args[0] == self.agent.name else self.target
        if method == "ParseFormID": return int(args[0])
        if method == "GetForm": return self.agent
        if method in ("UnregisterForUpdate", "UnregisterForUpdateGameTime"):
            self.effects.append((method, tuple(args)))
            return None
        if method == "Notification":
            self.effects.append(("Notification", args[0]))
            return None
        return super().external(method, args, script)

    def seek(self, narration):
        payload = json.dumps({"storyType": "seek_player", "npcName": self.agent.name,
                              "narration": narration, "response": "{}"})
        self.invoke("IntelEngine_Core", "OnDashboardDispatchStory", ("", payload, 0.0, None))

    def task_snapshot(self):
        return (dict(self.storage), tuple((self.properties[f"AgentAlias{i:02d}"].actor,
            self.properties["SlotStates"][i], self.properties["SlotTaskTypes"][i],
            self.properties["SlotTargetNames"][i]) for i in range(5)),
            tuple(self.properties[k] for k in ("ActiveStoryNPC", "ActiveStoryType",
                "ActiveSecondNPC", "ActiveNarration", "DispatchGeneration", "IsActive")))


def run_director_lite_checks(files, history):
    def first_seek():
        task = DirectorVM(files, history)
        task.seek("First seek")
        require(("PushDashboardState", ()) in task.effects,
                "Director story dispatch did not push dashboard state")
        require(("Notification", "Nazeem sets out (Seek Player).") in task.effects,
                "Director story notice must use friendly story type: " + repr(task.effects))
        require(sum(task.properties[f"AgentAlias{i:02d}"].actor is task.agent for i in range(5)) == 1,
                "first Director seek did not allocate exactly one slot")
        return task

    def require_refused(task, snapshot, effects_at):
        require(task.task_snapshot() == snapshot, "refused Director request changed the original task")
        extra = task.effects[effects_at:]
        require([e for e in extra if e[0] == "Notification"] ==
                [("Notification", "Nazeem is already busy (Story). Cancel it first.")],
                "busy Director request did not notify exactly once")
        require(all(e[0] in ("Notification", "Trace") for e in extra),
                "refused Director request wrote facts, narrated, or changed travel")

    def director_seek_then_seek_is_refused():
        task = first_seek()
        snapshot, effects_at = task.task_snapshot(), len(task.effects)
        task.seek("Second seek")
        require_refused(task, snapshot, effects_at)

    def run6_seek_seek_fetch_arrival():
        task = first_seek()
        snapshot, effects_at = task.task_snapshot(), len(task.effects)
        task.seek("Second seek")
        require_refused(task, snapshot, effects_at)
        effects_at = len(task.effects)
        payload = json.dumps({"actionName": "FetchPerson", "npcFormId": "1234", "targetName": "Alva"})
        task.invoke("IntelEngine_Core", "OnDashboardExecuteAction", ("", payload, 0.0, None))
        require_refused(task, snapshot, effects_at)
        task.halt_before = "EnsureBuildingAccess"
        task.invoke("IntelEngine_StoryEngine", "OnProximityArrived", ("0",))
        require(task.call_counts.get(("IntelEngine_Core", "ClearSlot"), 0) == 1 and
                task.properties["SlotStates"] == [0] * 5 and
                all(task.properties[f"AgentAlias{i:02d}"].actor is None for i in range(5)),
                "run6 arrival failed to clear only the original seek slot")

    def director_story_while_story_active():
        class CourierVM(DirectorVM):
            def __init__(self):
                super().__init__(files, history)
                self.agent.name, self.target.name = "Gundarsting [Servant]", "Amren"
                self.saadia = Actor("Saadia", "player-cell")

            def external(self, method, args, script):
                if method == "FindMessengerForSender": return self.agent
                if method == "ResolveDirectorActor" and args[0] == "Saadia": return self.saadia
                return super().external(method, args, script)

        task = CourierVM()
        message = json.dumps({"msgContent": "Please come see me", "destination": "", "meetTime": ""})
        payload = json.dumps({"storyType": "message", "npcName": "Amren",
                              "narration": "Deliver Amren's message", "response": message})
        task.invoke("IntelEngine_Core", "OnDashboardDispatchStory", ("", payload, 0.0, None))
        require(task.properties["ActiveStoryNPC"] is task.agent and
                task.properties["ActiveStoryType"] == "message" and
                task.storage[(task.agent, "Intel_MessageContent")] == "Please come see me",
                "first Director message did not establish the courier task")
        snapshot, effects_at = task.task_snapshot(), len(task.effects)
        payload = json.dumps({"storyType": "seek_player", "npcName": "Saadia",
                              "narration": "A second story", "response": "{}"})
        task.invoke("IntelEngine_Core", "OnDashboardDispatchStory", ("", payload, 0.0, None))
        require(task.task_snapshot() == snapshot, "second Director story displaced the active courier")
        extra = task.effects[effects_at:]
        require([e for e in extra if e[0] == "Notification"] == [("Notification",
                "Story engine is busy with Gundarsting [Servant] (Message). Cancel it first.")],
                "active Story engine did not notify exactly once")
        require(all(e[0] in ("Notification", "Trace") for e in extra),
                "refused story wrote facts, narrated, or changed travel")
        task.halt_before = "EnsureBuildingAccess"
        task.invoke("IntelEngine_StoryEngine", "OnProximityArrived", ("0",))
        require(task.call_counts.get(("IntelEngine_Core", "ClearSlot"), 0) == 1 and
                task.properties["SlotStates"] == [0] * 5 and
                all(task.properties[f"AgentAlias{i:02d}"].actor is None for i in range(5)),
                "preserved courier arrival did not release its original slot")

    def cancel_story_task_ends_story():
        class CancelStoryVM(DirectorVM):
            def __init__(self, route, during_wait):
                super().__init__(files, history)
                self.agent.name, self.target.name = "Jon Battle-Born", "Amren"
                self.route, self.cancel_at_wait = route, during_wait
                self.resumed_snapshot = self.resumed_effects_at = None

            def invoke(self, script, name, args=()):
                if (script == "IntelEngine_StoryEngine" and name == "ReapplyTravelPackage") or (
                        script == "IntelEngine_Core" and name in
                        ("InitializeStuckTrackingForSlot", "InitOffScreenTracking")):
                    return VM.invoke(self, script, name, args)
                return super().invoke(script, name, args)

            def message(self, content):
                message = json.dumps({"msgContent": content, "destination": "", "meetTime": ""})
                payload = json.dumps({"storyType": "message", "npcName": "Amren",
                                      "narration": content, "response": message})
                self.invoke("IntelEngine_Core", "OnDashboardDispatchStory", ("", payload, 0.0, None))

            def cancel_and_replace(self):
                if self.route == "dashboard":
                    self.invoke("IntelEngine_Core", "OnDashboardCancelTask", ("", "", 0, None))
                else:
                    payload = json.dumps({"actionName": "CancelCurrentTask", "npcFormId": "1234"})
                    self.invoke("IntelEngine_Core", "OnDashboardExecuteAction", ("", payload, 0.0, None))
                require(not self.properties["IsActive"] and self.properties["ActiveStoryNPC"] is None and
                        self.properties["ActiveStoryType"] == "" and self.properties["ActiveNarration"] == "",
                        "message courier cancellation left shared story active")
                require(self.properties["SlotStates"] == [0] * 5 and
                        all(self.properties[f"AgentAlias{i:02d}"].actor is None for i in range(5)) and
                        all((self.agent, key) not in self.storage for key in
                            ("Intel_IsStoryDispatch", "Intel_OffscreenArrival", "Intel_MessageContent")),
                        "message courier cancellation retained its slot or tracking payload")
                snapshot, effects_at = self.task_snapshot(), len(self.effects)
                self.invoke("IntelEngine_StoryEngine", "CheckStoryNPCArrival")
                self.invoke("IntelEngine_StoryEngine", "OnProximityArrived", ("0",))
                require(self.task_snapshot() == snapshot and self.effects[effects_at:] == [],
                        "queued courier poll or arrival re-armed cancelled work")
                self.message("Replacement message")
                require(self.properties["IsActive"] and self.properties["ActiveStoryNPC"] is self.agent and
                        self.properties["ActiveStoryType"] == "message" and
                        self.storage[(self.agent, "Intel_MessageContent")] == "Replacement message" and
                        self.properties["SlotStates"][0] == 1,
                        "new Director Story was not admitted after courier cancellation")
                self.resumed_snapshot, self.resumed_effects_at = self.task_snapshot(), len(self.effects)

            def external(self, method, args, script):
                if method == "FindMessengerForSender": return self.agent
                if method == "CalculateDeadlineFromDistance": return 124.0
                if method == "SetSlotOffscreenArrival":
                    self.effects.append((method, tuple(args)))
                    return None
                if method == "Wait" and self.cancel_at_wait:
                    self.cancel_at_wait = False
                    self.cancel_and_replace()
                return super().external(method, args, script)

        for route in ("dashboard", "director"):
            for during_wait in (False, True):
                task = CancelStoryVM(route, during_wait)
                task.message("Initial message")
                if not during_wait:
                    task.cancel_and_replace()
                require(task.task_snapshot() == task.resumed_snapshot,
                        "resumed cancelled dispatch changed its replacement's tracking")
                require(not any(e[0] in ("InitializeStuckTrackingForSlot", "InitOffScreenTracking",
                        "ResetStuckSlot", "InitOffScreenTravel", "ArmProximityArrival",
                        "UnregisterForUpdate", "UnregisterForUpdateGameTime")
                        for e in task.effects[task.resumed_effects_at:]),
                        "resumed cancelled dispatch re-armed tracking or stopped the new scheduler")

    def cancel_story_injects_closing_fact():
        for narration in ("set out to find Prisoner -- to say hi", ""):
            task = DirectorVM(files, history)
            task.seek(narration or "Initial seek")
            task.properties["ActiveNarration"] = narration
            effects_at = len(task.effects)
            require(task.invoke("IntelEngine_Core", "CancelCurrentTask", (task.agent, True)),
                    "active story cancellation failed")
            require([e for e in task.effects[effects_at:] if e[0] == "InjectFact"] ==
                    ([("InjectFact", (task.agent, "stopped before finishing: " + narration))]
                     if narration else []),
                    "story cancellation did not inject exactly one closing fact with the original narration")
            require(not task.properties["IsActive"] and task.properties["ActiveNarration"] == "",
                    "closing fact prevented story cleanup")
            effects_at = len(task.effects)
            require(not task.invoke("IntelEngine_Core", "CancelCurrentTask", (task.agent, True)) and
                    not any(e[0] == "InjectFact" for e in task.effects[effects_at:]),
                    "repeated cancellation injected a duplicate closing fact")

    def director_action_success_notice():
        for state, expected in ((1, [("Notification", "Nazeem: ChangeSpeed started.")]),
                                (0, [("Notification", "Nazeem: ChangeSpeed failed.")])):
            task = DirectorVM(files, history)
            task.properties["AgentAlias00"].actor = task.agent
            task.properties["SlotStates"][0] = state
            payload = json.dumps({"actionName": "ChangeSpeed", "npcFormId": "1234", "newSpeed": 0, "reason":"wanted to help Player"})
            task.invoke("IntelEngine_Core", "OnDashboardExecuteAction", ("", payload, 0.0, None))
            require((("InjectFact", (task.agent, "wanted to help Player")) in task.effects) == (state == 1),
                    "Director action reason injection did not match action success")
            require([e for e in task.effects if e[0] == "Notification"] == expected,
                    "director_action_success_notice: execution outcome notice mismatch")
        task = DirectorVM(files, history)
        payload = json.dumps({"actionName": "ChangeSpeed", "npcFormId": "1234", "newSpeed": 0})
        task.invoke("IntelEngine_Core", "OnDashboardExecuteAction", ("", payload, 0.0, None))
        require(not any(e[0] == "InjectFact" for e in task.effects),
                "Director action without a reason injected a fact")

    def director_conduct_report_success_notice():
        class ConductVM(DirectorVM):
            def __init__(self):
                super().__init__(files, history)
                self.scripts["IntelEngine_Politics"] = files[3]
                self.properties["Politics"] = "IntelEngine_Politics"

            def external(self, method, args, script):
                if method == "StoryResponseGetField":
                    return json.loads(args[0]).get(args[1], "")
                if method == "ProcessPlayerConduct":
                    self.effects.append(("conduct-record", tuple(args)))
                    return 1
                if method == "WritePoliticalStateFile":
                    self.effects.append(("political-state-write", ()))
                    return None
                return super().external(method, args, script)

        task = ConductVM()
        payload = json.dumps({"actionName": "ReportPlayerConduct", "npcFormId": "1234",
            "factionId": "Stormcloaks", "sentiment": "positive", "reason": "defended the hold"})
        task.invoke("IntelEngine_Core", "OnDashboardExecuteAction", ("", payload, 0.0, None))
        require(("conduct-record", (task.agent, "Stormcloaks", "positive", "defended the hold")) in task.effects,
                "Director did not execute the Politics conduct report")
        require([e for e in task.effects if e[0] == "InjectFact"] ==
                [("InjectFact", (task.agent, "defended the hold"))],
                "Director conduct report did not inject exactly the reason fact")
        require([e for e in task.effects if e[0] == "Notification"] ==
                [("Notification", "Nazeem: conduct reported.")],
                "Director conduct report did not emit only the reported notice")

    def director_social_and_political_success_notice():
        class NoticeVM(DirectorVM):
            def __init__(self):
                super().__init__(files, history)
                self.scripts["IntelEngine_Politics"] = files[3]
                self.properties["Politics"] = "IntelEngine_Politics"
                self.distance, self.acted = 1000.0, True

            def external(self, method, args, script):
                if method == "GetDistance": return self.distance
                if method == "GetActorHoldName": return "Whiterun"
                if method == "GetLength": return len(args[0])
                if method in ("StoryResponseGetOptionalField", "ExtractJsonField"):
                    return json.loads(args[0]).get(args[1], "")
                if method == "ProcessPoliticalDMResponse":
                    self.effects.append(("political-record", self.acted))
                    return json.dumps({"acted": str(self.acted).lower(), "event_type": "trade_deal"})
                if method == "ResetManifestationCooldown": return None
                return super().external(method, args, script)

        for mode in ("travel", "no-slot", "off-screen", "refused"):
            task = NoticeVM()
            if mode == "no-slot": task.properties["SlotStates"] = [1] * 5
            if mode == "off-screen": task.distance = 100.0
            if mode == "refused":
                task.properties["AgentAlias00"].actor = task.agent
                task.properties["SlotStates"][0] = 1
            payload = json.dumps({"npc1Name": task.agent.name, "npc2Name": task.target.name,
                "socialType": "npc_interaction", "narration": "A meeting", "response": "{}"})
            task.invoke("IntelEngine_Core", "OnDashboardDispatchNpcSocial", ("", payload, 0.0, None))
            if mode != "refused":
                require(("PushDashboardState", ()) in task.effects,
                        "Director social dispatch did not push dashboard state: " + mode)
            notices = [e for e in task.effects if e[0] == "Notification" and e[1].endswith("started.")]
            require(notices == ([] if mode == "refused" else
                [("Notification", "Nazeem and Recipient: Npc Interaction started.")]),
                "director_social_and_political_success_notice: Social " + mode)
            if mode == "travel":
                require(task.properties["IsNPCStoryActive"] and task.properties["SlotTaskTypes"][0] == "npc_social",
                        "Social notice without queued travel")
        for acted in (True, False):
          for event in ({"event_type":"war_declaration"}, {"event_type":"war_declaration","event_label":"War declared"}):
            task = NoticeVM()
            task.acted = acted
            event.update({"faction_a": "ImperialFaction", "faction_b": "StormcloakFaction",
                          "faction_a_label": "Imperial Legion", "faction_b_label": "Stormcloaks"})
            task.invoke("IntelEngine_Core", "OnDashboardDispatchPolitics",
                        ("", json.dumps(event), 0.0, None))
            require([e for e in task.effects if e[0] == "Notification"] ==
                    ([("Notification", "Political event: " + event.get("event_label", "War Declaration") +
                       " between Imperial Legion and Stormcloaks.")] if acted else []),
                    "director_social_and_political_success_notice: Political acted=" + str(acted))
            require(("political-record", acted) in task.effects, "Political native boundary not reached")
    def director_story_not_found_notifies():
        class MissingActorVM(DirectorVM):
            def __init__(self):
                super().__init__(files, history)

            def external(self, method, args, script):
                if method == "ResolveDirectorActor":
                    return self.agent if args[0] == "Nazeem" else None
                return super().external(method, args, script)

        for event, payload, names in (
            ("OnDashboardDispatchStory", {"storyType": "seek_player", "npcName": "Arvel the Swift"},
             ("Arvel the Swift",)),
            ("OnDashboardDispatchNpcSocial", {"npc1Name": "Missing One", "npc2Name": "Missing Two"},
             ("Missing One",)),
            ("OnDashboardDispatchNpcSocial", {"npc1Name": "Nazeem", "npc2Name": "Missing Two"},
             ("Missing Two",))):
            task = MissingActorVM()
            task.invoke("IntelEngine_Core", event, ("", json.dumps(payload), 0.0, None))
            notices = [e for e in task.effects if e[0] == "Notification"]
            require(notices ==
                    [("Notification", "Couldn't find NPC '" + names[0] + "' (no unique match).")],
                    "director_story_not_found_notifies: missing NPC notice " + repr(task.effects))
            require(any(e[0] == "Trace" for e in task.effects),
                    "director_story_not_found_notifies: DebugMsg trace was removed")


    def director_message_generation_success_notice():
        class MessageVM(DirectorVM):
            def external(self, method, args, script):
                if method == "FindMessengerForSender": return self.target
                if method == "CalculateDeadlineFromDistance": return 124.0
                if method == "SetSlotOffscreenArrival": return None
                return super().external(method, args, script)
        for full in (False, True):
            task = MessageVM(files, history)
            if full: task.properties["SlotStates"] = [1] * 5
            generation = task.properties["DispatchGeneration"]
            payload = json.dumps({"storyType": "message", "npcName": task.agent.name,
                "narration": "A message", "response": '{"msgContent":"Hello"}'})
            task.invoke("IntelEngine_Core", "OnDashboardDispatchStory", ("", payload, 0.0, None))
            require([e for e in task.effects if e[0] == "Notification"] ==
                    ([] if full else [("Notification", "Recipient sets out (Message).")]),
                    "director_message_generation_success_notice: accepted courier once, refused none")
            require(task.properties["DispatchGeneration"] == generation + (0 if full else 1),
                    "message generation did not match dispatch admission")

    def ambush_faction_hirer_injects_leader_fact():
        class HirerVM(DirectorVM):
            def external(self, method, args, script):
                if method == "FindNPCByName":
                    return self.target if args[0] == self.target.name else None
                if method == "GetFactionLeaderFormIds":
                    require(len(args) == 1, "faction lookup did not receive one sender")
                    self.effects.append((method, tuple(args)))
                    return [13276, 0] if args[0] in ("ImperialFaction", "UndisplayedFaction") else []
                if method == "GetFactionDisplayName":
                    require(args[0] in ("ImperialFaction", "UndisplayedFaction"),
                            "display-name lookup did not receive the faction ID: " + repr(args))
                    self.effects.append((method, tuple(args)))
                    return "Imperials" if args[0] == "ImperialFaction" else ""
                if method == "FriendlyKey":
                    raise AssertionError("leader faction fact used FriendlyKey instead of display name")
                if method == "GetForm": return self.target if args[0] == 13276 else None
                return super().external(method, args, script)

        for story_type, verb in (("ambush", "sent Nazeem to ambush Player"),
                                 ("stalker", "encouraged Nazeem to follow Player")):
            for sender in ("Recipient", "ImperialFaction", "UndisplayedFaction", ""):
                task = HirerVM(files, history)
                task.halt_before = "RandomInt" if story_type == "ambush" else "DispatchToTarget"
                task.invoke("IntelEngine_StoryEngine", "HandleAmbushStalkerDispatch",
                            (task.agent, "had a grudge", json.dumps({"sender": sender}), story_type, "Director"))
                display_name = {"ImperialFaction": "Imperials", "UndisplayedFaction": "UndisplayedFaction"}.get(sender, "")
                expected = (display_name + " " if display_name else "") + verb
                require([e for e in task.effects if e[0] == "InjectFact"] ==
                        [("InjectFact", (task.agent, "had a grudge"))] +
                        ([("InjectFact", (task.target, expected))] if sender else []),
                        "hirer facts differ for " + story_type + "/" + sender + ": " + repr(task.effects))
                require(any(e[0] == "GetFactionLeaderFormIds" for e in task.effects) ==
                        bool(sender), "non-empty sender did not check faction leaders")
                require(any(e[0] == "GetFactionDisplayName" for e in task.effects) ==
                        (sender in ("ImperialFaction", "UndisplayedFaction")),
                        "faction display name was not used only for faction-leader facts")
                require(task.halt_reached, "hirer check did not reach the dispatch boundary")


    failures = []
    for case in (director_seek_then_seek_is_refused, run6_seek_seek_fetch_arrival,
                 director_story_while_story_active, cancel_story_task_ends_story,
                 cancel_story_injects_closing_fact, director_action_success_notice,
                 director_conduct_report_success_notice, director_social_and_political_success_notice,
                 director_message_generation_success_notice,
                 director_story_not_found_notifies, ambush_faction_hirer_injects_leader_fact):
        try:
            case()
            print("actual PEX " + case.__name__ + " PASS")
        except AssertionError as error:
            failures.append(case.__name__)
            print("actual PEX " + case.__name__ + " FAIL: " + str(error))
    require(not failures, "Failed Director cases: " + ", ".join(failures))


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    exe, scripts = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    history = pathlib.Path(sys.argv[3])
    require(exe.is_file() and scripts.is_dir() and history.is_file(),
            "expected papyrus.exe, Game Scripts directory, and history executable")

    def disassemble(filename):
        pex = scripts / filename
        require(pex.is_file(), f"missing current PEX: {pex}")
        return parse_functions(subprocess.run([str(exe), "read", str(pex)], check=True,
            capture_output=True, text=True).stdout)

    files = (disassemble("IntelEngine_NPCTasks.pex"), disassemble("IntelEngine_Core.pex"),
             disassemble("IntelEngine_StoryEngine.pex"), disassemble("IntelEngine_Politics.pex"))
    vm = VM(files, history)
    vm.invoke("IntelEngine_NPCTasks", "OnArrivedToDeliver", (0, vm.agent, vm.target))
    events = vm.effects
    fmt_at = next((i for i, e in enumerate(events) if e[0] == "formatter-args"), -1)
    wait_at = next((i for i, e in enumerate(events) if e[0] == "Wait" and e[1] == (3.0,)), -1)
    result_read_at = next((i for i, e in enumerate(events)
                           if e[:2] == ("history-read", "Intel_Result") and e[2] == "delivered"), -1)
    message_unset_at = next((i for i, e in enumerate(events) if e == ("unset", "Intel_Message")), -1)
    result_unset_at = next((i for i, e in enumerate(events) if e == ("unset", "Intel_Result")), -1)
    result_write_at = next((i for i, e in enumerate(events) if e == ("result-write", "delivered")), -1)
    require(0 <= result_write_at < wait_at, "delivery result write did not precede wait(3.0)")
    require(wait_at >= 0 and result_read_at > wait_at, "delivery history did not read result after wait(3.0)")
    require(fmt_at > result_read_at and result_unset_at > fmt_at and message_unset_at > fmt_at,
            "history read/formatter did not precede result and task-storage cleanup")
    fmt = events[fmt_at][1]
    require(fmt == ("deliver_message", "Recipient", "delivered", "Meet me at the bridge", ""),
            f"unexpected native formatter arguments: {fmt!r}")
    require(any(e == ("result-write", "delivered") for e in events), "delivery result write missing")
    require(any(e[0] == "SendTaskNarration" for e in events), "player-present narration missing")
    require(vm.call_counts.get(("IntelEngine_Core", "ClearSlot"), 0) == 1, "player-present path did not clear exactly once")
    require(not any(e[0] == "SetPlayerTeammate" for e in events),
            "non-follower delivery unexpectedly restored teammate status")
    require(vm.call_counts.get(("IntelEngine_NPCTasks", "BeginDeliveryReturn"), 0) == 0, "player-present path entered return")
    require(not any(e[0] == "ScheduleMeeting" for e in events), "no-meeting path scheduled a meeting")
    print("delivery formatter args:", repr(fmt))
    expected = "Delivered a message to Recipient: 'Meet me at the bridge'"
    require(vm.storage[(vm.agent, "Intel_TaskHistory")] == [expected],
            "actual bytecode/native formatter did not save successful delivery history")
    # Model possible pooled-string spelling at the native boundary, not an observed run3 value.
    for spelling in ("Delivered", "DELIVERED"):
        pooled = VM(files, history, spelling)
        pooled.invoke("IntelEngine_NPCTasks", "OnArrivedToDeliver", (0, pooled.agent, pooled.target))
        require(pooled.storage[(pooled.agent, "Intel_TaskHistory")] == [expected],
                "mixed-case boundary outcome lost successful delivery history")

    absent = VM(files, history)
    absent.agent.cell = "recipient-cell"
    absent.invoke("IntelEngine_NPCTasks", "OnArrivedToDeliver", (0, absent.agent, absent.target))
    require(absent.storage[(absent.agent, "Intel_Result")] == "delivered", "player-absent result missing")
    require(absent.properties["SlotStates"][0] == 3, "player-absent delivery did not begin return")
    require(absent.call_counts.get(("IntelEngine_NPCTasks", "BeginDeliveryReturn"), 0) == 1,
            "player-absent path skipped actual return handler")
    require(absent.call_counts.get(("IntelEngine_Core", "ClearSlot"), 0) == 0,
            "player-absent delivery cleared slot before return")
    require(not any(e[0] == "SendTaskNarration" for e in absent.effects),
            "player-absent path emitted player-present narration")

    # Execute the shared cancel and full cleanup on an actively traveling NPC.
    class CancelVM(VM):
        def __init__(self, distance):
            super().__init__(files, history)
            self.distance = distance
            self.properties.update({"LINGER_RELEASE_DISTANCE": 1500.0,
                "SlotTaskTypes": ["Travel"] + [""] * 4, "SlotStates": [1] + [0] * 4,
                "IntelEngine_TravelTarget": "travel-link"})
            self.storage[(self.agent, "Intel_TaskType")] = "Travel"
            self.packages = {"travel"}
            self.link = self.target

        def invoke(self, script, name, args=()):
            if script == "IntelEngine_Core" and name == "InjectFact":
                self.effects.append((name, tuple(args)))
                return None
            return super().invoke(script, name, args)

        def external(self, method, args, script):
            if method == "Is3DLoaded": return True
            if method == "GetDistance": return self.distance
            if method == "ClearPackageOverride": self.packages.clear()
            if method == "SetLinkedRef": self.link = args[1]
            if method == "StoryResponseGetField": return json.loads(args[0]).get(args[1], "")
            if method == "ParseFormID": return int(args[0])
            if method == "GetForm": return self.agent
            if method == "PushDashboardState": return None
            return super().external(method, args, script)

    # SkyrimNet requires one YAML mapping per reflected parameter; it supplies no defaults.
    mapping_path = pathlib.Path(__file__).resolve().parents[2] / (
        "SKSE/Plugins/SkyrimNet/external/galanx.intelengine/actions/cancelcurrenttask.yaml")
    mapping_text = mapping_path.read_text(encoding="utf-8").split("parameterMapping:", 1)[1].split("eligibilityRules:", 1)[0]

    def automatic_cancel_args(actor):
        args = []
        for entry in mapping_text.split("  - ")[1:]:
            fields = dict(re.findall(r"^\s*(type|value):\s*(\S+)\s*$", entry, re.M))
            if fields.get("type") == "speaker":
                args.append(actor)
            else:
                require(fields.get("type") == "static" and fields.get("value") in ("true", "false"),
                        "unsupported cancellation mapping")
                args.append(fields["value"] == "true")
        return args

    blocked = CancelVM(100.0)
    require(blocked.invoke("IntelEngine_Core", "CancelCurrentTask", automatic_cancel_args(blocked.agent)) is False,
            "ordinary nearby cancellation bypassed the linger guard")
    require(blocked.properties["SlotStates"][0] == 1 and blocked.packages == {"travel"},
            "blocked cancellation mutated the traveling task")
    for force, distance in ((True, 100.0), (False, 2000.0)):
        cancelled = CancelVM(distance)
        args = (cancelled.agent, True) if force else automatic_cancel_args(cancelled.agent)
        require(cancelled.invoke("IntelEngine_Core", "CancelCurrentTask", args) is True,
                "forced nearby or ordinary distant cancel was rejected")
        require(cancelled.properties["SlotStates"][0] == 0 and
                cancelled.properties["SlotTaskTypes"][0] == "" and
                cancelled.properties["AgentAlias00"].actor is None,
                "Travel cancellation did not release slot and alias")
        require(not cancelled.packages and cancelled.link is None and
                (cancelled.agent, "Intel_TaskType") not in cancelled.storage,
                "Travel cancellation left packages, link, or persisted task")
        require(("ClearSlotState", (0,)) in cancelled.effects and
                ("DisarmProximityArrival", (0,)) in cancelled.effects and
                ("EvaluatePackage", (cancelled.agent,)) in cancelled.effects,
                "Travel cancellation did not reset native tracking and resume AI")
    print("actual PEX Travel cancel: YAML-mapped nearby guarded; forced nearby and mapped distant cleanup passed")

    director = CancelVM(100.0)
    director.halt_before = "PushDashboardState"
    payload = json.dumps({"actionName": "CancelCurrentTask", "npcFormId": "1234",
                          "reason": "helped the courier"})
    director.invoke("IntelEngine_Core", "OnDashboardExecuteAction", ("", payload, 0.0, None))
    require(director.properties["SlotStates"][0] == 0,
            "Director cancel did not clear nearby Travel task")
    notices = [e for e in director.effects if e[0] == "Trace" and e[1] in
               ("Courier's task was cancelled.", "Courier has no task to cancel.",
                "Courier: CancelCurrentTask failed.")]
    require(notices == [("Trace", "Courier's task was cancelled.")],
            "Director successful cancel emitted incorrect notices: " + repr(notices))
    require([e for e in director.effects if e[0] == "InjectFact"] ==
            [("InjectFact", (director.agent, "helped the courier"))],
            "Director successful cancel did not inject only its reason fact")
    director.effects.clear()
    director.invoke("IntelEngine_Core", "OnDashboardExecuteAction", ("", payload, 0.0, None))
    notices = [e for e in director.effects if e[0] == "Trace" and e[1] in
               ("Courier's task was cancelled.", "Courier has no task to cancel.",
                "Courier: CancelCurrentTask failed.")]
    require(notices == [("Trace", "Courier has no task to cancel.")],
            "Director no-task cancel emitted incorrect notices: " + repr(notices))
    require(not any(e[0] == "InjectFact" for e in director.effects),
            "Director no-task cancel injected a reason fact")
    require(not any(e[0] == "Trace" and ("task was cancelled" in e[1] or "no task to cancel" in e[1])
                    for e in blocked.effects + cancelled.effects),
            "automatic cancellation emitted Director-only feedback")
    print("actual PEX Director cancel: success and repeated no-task feedback passed; automatic feedback unchanged")

    story = VM(files, history)
    story.properties["SlotTaskTypes"][0] = "story"
    story.properties["SlotTargetNames"][0] = "Player"
    story.reenter_story = True
    story.halt_before = "EnsureBuildingAccess"
    story.invoke("IntelEngine_StoryEngine", "OnStoryNPCArrived")
    require(story.halt_reached, "story arrival did not reach post-clear boundary")
    require(story.call_counts.get(("IntelEngine_StoryEngine", "OnStoryNPCArrived"), 0) == 2,
            "forced recursive story arrival was not exercised")
    require(story.call_counts.get(("IntelEngine_Core", "ClearSlot"), 0) == 1,
            "reentrant story arrival cleared other than once")
    trace_count = sum(e[0] == "Trace" and "Story: Courier arrived!" in e[1] for e in story.effects)
    narration_count = sum(e[0] == "SendTaskNarration" for e in story.effects)
    require(trace_count == 1 and narration_count == 1,
            f"reentrant story arrival duplicated trace/narration ({trace_count}/{narration_count})")
    # An ordinary active poll with the latch already set must be a no-op.
    story.properties.update({"IsActive": True, "ArrivalHandling": True, "ActiveStoryNPC": story.agent})
    story.invoke("IntelEngine_StoryEngine", "OnStoryNPCArrived")
    require(story.call_counts.get(("IntelEngine_Core", "ClearSlot"), 0) == 1 and
            sum(e[0] == "SendTaskNarration" for e in story.effects) == 1,
            "ordinary latched arrival poll repeated handling")

    # A real dispatch with an available slot must reset the latch in its PEX.
    story.properties.update({"ArrivalHandling": True, "IsActive": True, "ActiveStoryType": "seek_player",
        "SlotStates": [0] * 5, "SlotTaskTypes": [""] * 5, "SlotTargetNames": [""] * 5})
    story.properties.update({f"AgentAlias{i:02d}": ReferenceAlias() for i in range(5)})
    story.stop_on_dispatch_reset = True
    story.invoke("IntelEngine_StoryEngine", "DispatchToTarget",
                 (story.agent, story.player, "A fresh arrival", "story", "Story engine"))
    require(story.dispatch_reset_seen and story.properties["ArrivalHandling"] is False,
            "actual new dispatch did not reset arrival latch")
    print("actual PEX delivery, absent-player return, and story arrival latch paths passed")
    # Cleanup can run while the poll stack is suspended inside its location check.
    interleaved = VM(files, history)
    interleaved.properties.update({"SlotTaskTypes": ["story"] + [""] * 4,
        "SlotTargetNames": ["Player"] + [""] * 4})
    interleaved.player.cell = None
    interleaved.arrival_interleave_method = "IsPlayerInBlockedLocation"
    interleaved.invoke("IntelEngine_StoryEngine", "CheckStoryNPCArrival")
    require(interleaved.call_counts.get(("IntelEngine_StoryEngine", "OnStoryNPCArrived"), 0) == 0,
            "stale arrival poll handled arrival after callback cleanup")
    require(interleaved.properties["ActiveStoryNPC"] is None and
            not interleaved.properties["IsActive"],
            "callback cleanup did not clear the dispatch")

    cleared = VM(files, history)
    cleared.properties.update({"IsActive": False, "ActiveStoryNPC": None})
    cleared.invoke("IntelEngine_StoryEngine", "CheckStoryNPCArrival")
    require(cleared.call_counts.get(("IntelEngine_StoryEngine", "CleanupStoryDispatch"), 0) == 0,
            "cleared arrival entry performed redundant cleanup")

    latched = VM(files, history)
    latched.properties["ArrivalHandling"] = True
    latched.player.cell = None
    latched.invoke("IntelEngine_StoryEngine", "CheckStoryNPCArrival")
    require(not any(e[0] == "GetDistance" for e in latched.effects) and
            latched.call_counts.get(("IntelEngine_StoryEngine", "CleanupStoryDispatch"), 0) == 0,
            "latched arrival entry ran a second poll")
    print("actual PEX story arrival poll: callback cleanup, cleared entry, and latched entry passed")
    missing_target = VM(files, history)
    missing_target.properties.update({"ActiveStoryType": "npc_interaction",
        "ActiveSecondNPC": None, "SlotTaskTypes": ["story"] + [""] * 4,
        "SlotTargetNames": ["Recipient"] + [""] * 4})
    missing_target.invoke("IntelEngine_StoryEngine", "CheckStoryNPCArrival")
    require(not any(e[0] == "GetDistance" for e in missing_target.effects),
            "NPC-to-NPC arrival fell back to the player when its target was cleared")

    for interleave_at in ("GetDistance", "GetArrivalThreshold"):
        for action in ("cleanup", "replace", "phase"):
            raced = VM(files, history)
            raced.properties.update({"SlotTaskTypes": ["story"] + [""] * 4,
                "SlotTargetNames": ["Player"] + [""] * 4})
            raced.player.cell = None
            raced.arrival_interleave_method = interleave_at
            raced.arrival_interleave_action = action
            raced.invoke("IntelEngine_StoryEngine", "CheckStoryNPCArrival")
            require(raced.call_counts.get(("IntelEngine_StoryEngine", "OnStoryNPCArrived"), 0) == 0,
                    f"stale poll handled arrival after {action} at {interleave_at}")
            if action == "cleanup":
                require(not raced.properties["IsActive"] and raced.properties["ActiveStoryNPC"] is None,
                        f"{interleave_at} callback cleanup was overwritten")
            elif action == "replace":
                require(raced.properties["ActiveStoryNPC"] is raced.replacement and
                        raced.properties["DispatchGeneration"] == 1 and raced.properties["IsActive"],
                        f"stale {interleave_at} stack operated on a replacement dispatch")
            else:
                require(raced.properties["ActiveStoryNPC"] is raced.agent and
                        raced.properties["ActiveStoryType"] == "quest_guide" and
                        raced.properties["DispatchGeneration"] == 0,
                        f"stale {interleave_at} stack operated on a changed dispatch phase")

    # These native "checks" mutate slot tracking; an old poll must not touch a reused slot.
    class TrackingRaceVM(VM):
        loaded = False

        def external(self, method, args, script):
            if method in ("CheckOffScreenProgress", "CheckStuckStatus"):
                self.effects.append((method, tuple(args)))
                return 0
            result = super().external(method, args, script)
            if method == "GetDistance":
                return 1000.0
            if method == "Is3DLoaded":
                return self.loaded
            return result

    for loaded in (False, True):
        tracking = TrackingRaceVM(files, history)
        tracking.loaded = loaded
        tracking.player.cell = None
        tracking.properties.update({"SlotTaskTypes": ["story"] + [""] * 4,
            "SlotTargetNames": ["Player"] + [""] * 4})
        tracking.arrival_interleave_method = "Is3DLoaded"
        tracking.arrival_interleave_action = "replace"
        tracking.invoke("IntelEngine_StoryEngine", "CheckStoryNPCArrival")
        require(not any(e[0] in ("CheckOffScreenProgress", "CheckStuckStatus") for e in tracking.effects),
                "stale arrival poll mutated replacement slot tracking")

    combat = VM(files, history)
    combat.properties.update({"ArrivalHandling": True, "ActiveStoryType": "ambush_combat"})
    combat.halt_before = "CheckAmbushCombat"
    combat.invoke("IntelEngine_StoryEngine", "CheckStoryNPCArrival")
    require(combat.halt_reached,
            "latched ambush-combat dispatch skipped ongoing combat monitoring")
    print("actual PEX arrival poll: cleanup/replacement interleavings and latched combat monitor passed")
    run_director_lite_checks(files, history)


if __name__ == "__main__":
    main()
