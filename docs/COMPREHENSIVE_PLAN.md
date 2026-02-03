# IntelEngine Comprehensive Implementation Plan

## Requirements Analysis & Feasibility Mapping

This document maps all requirements to SkyrimNet capabilities and determines implementation approach.

---

## 1. INTERRUPT SYSTEM

### Requirements
- NPCs can be stopped mid-task
- NPCs listen, then can resume, cancel, or follow player
- Smart package priority management
- MCM for manual clearing

### Feasibility: **FULLY POSSIBLE**

### Implementation

**State Machine:**
```
IDLE → ACTIVE_TASK → PAUSED → RESUMED
                  ↘ FOLLOWING → RESUMED
                  ↘ CANCELLED → IDLE
```

**Package Priorities:**
| Priority | State | Purpose |
|----------|-------|---------|
| 110 | Listen | Brief face-to-face when interrupted |
| 100 | Active Task | Travel/escort execution |
| 95 | Following Player | Mid-task following |
| 80 | Paused Sandbox | Sandboxing while paused |
| 0 | Default | NPC's normal AI |

**New Scripts Needed:**
- `IntelEngine_Interrupt.psc` - Pause/resume/cancel logic

**New Actions:**
- `StopTask` - Pause and listen
- `ResumeTask` - Continue previous task
- `CancelTask` - Forget and sandbox
- `FollowDuringTask` - Follow but preserve task

---

## 2. ACTION VALIDATION IN PAPYRUS (Not LLM)

### Requirements
- Don't rely on LLM to "say so" when things fail
- Validate in Papyrus logic
- Use DirectNarration to make NPC speak failure reason

### Feasibility: **FULLY POSSIBLE**

### Implementation

**Current (Wrong) Approach:**
```yaml
description: >
  If you don't know the NPC, say so...  # LLM might ignore this
```

**Correct Approach:**
```papyrus
Bool Function FetchNPC(Actor akAgent, String targetName)
    Actor target = IntelEngine.FindNPCByName(targetName)

    If target == None
        ; Use DirectNarration to make NPC speak
        TriggerDirectNarration(akAgent, akAgent.GetDisplayName() + " looked puzzled. \"I don't know anyone by that name.\"")
        Return false
    EndIf

    ; Continue with task...
EndFunction

Function TriggerDirectNarration(Actor akActor, String content)
    Int handle = ModEvent.Create("DirectNarration")
    If handle
        ModEvent.PushForm(handle, akActor)
        ModEvent.PushString(handle, content)
        ModEvent.Send(handle)
    EndIf
EndFunction
```

**Action YAML (Simplified):**
```yaml
description: >
  Fetch an NPC by name and bring them back.
  The system will validate if the NPC exists.
# No need for "say so if you don't know" - Papyrus handles it
```

---

## 3. ANTI-HALLUCINATION VIA CONTEXT

### Requirements
- Don't give vague instructions like "verify it exists"
- Use existing prompts (spatial awareness, inventory, time, weather)
- Instruct AI not to use actions when context doesn't support them

### Feasibility: **FULLY POSSIBLE**

### Implementation

**Context Already Available in SkyrimNet:**
- Spatial awareness (doors, stairs, exits)
- Inventory contents
- Time of day
- Weather
- Current location
- Nearby objects
- NPC's capabilities

**Better Anti-Hallucination Prompt:**
```jinja2
## Action Guidelines

When considering actions, CHECK YOUR CONTEXT FIRST:

### Travel Actions
- Check "Spatial Awareness" section for available exits
- If no "upstairs" shown → DO NOT say you'll go upstairs
- If no exit to outside shown → DO NOT say you'll go outside

### Fetch/Give Actions
- Check "Inventory Awareness" for items you have
- If item not listed → DO NOT offer to give it

### NPC Actions
- If you don't recognize the name from Skyrim lore → DO NOT claim to know them
- The system will tell you if someone doesn't exist

### What Happens on Failure
If you attempt an impossible action, you will automatically express confusion.
The system handles validation - focus on roleplay.
```

**This approach:**
- Tells AI what to CHECK (concrete)
- Tells AI what NOT to do based on checks
- Reassures AI that system handles validation

---

## 4. ADVANCED GAMEMASTER SYSTEM

### Requirements
- Periodically analyze NPCs and their memories
- Assign dynamic tasks to make world lively
- Generate dynamic quests (e.g., NPC's sister in danger)
- Have NPCs visit player at home
- Update dynamic bios during important events

### Feasibility: **PARTIALLY POSSIBLE - Requires Creative Implementation**

### What SkyrimNet Already Has
- Gamemaster model for initiating NPC interactions
- Memory system for storing/retrieving memories
- Dynamic bio update capability
- DirectNarration for creating events

### What We Need to Build
- Periodic trigger for Gamemaster evaluation
- Story generator that creates events
- Task assignment system (IntelEngine)
- NPC visiting system

### Implementation Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    GAMEMASTER TRIGGER                           │
│        (Fires periodically or on significant events)            │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    STORY EVALUATION                             │
│  - Pull random NPCs with relationships to player/each other     │
│  - Query their memories and bios                                │
│  - Evaluate potential story hooks                               │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    EVENT GENERATION                             │
│  - Create DirectNarration events                                │
│  - Trigger dynamic bio updates                                  │
│  - Assign IntelEngine tasks to NPCs                             │
└─────────────────────────────────────────────────────────────────┘
```

**Periodic Trigger (YAML):**
```yaml
# 0100_intel_gamemaster_periodic.trigger.yaml
name: IntelGamemasterPeriodic
description: Periodically evaluate world state for dynamic events
eventTypes:
  - sleep_stop        # Player wakes up - check for events
  - location_change   # Player enters new area
enabled: true
probability: 0.3      # 30% chance to evaluate
cooldownSeconds: 1800 # 30 minutes between evaluations

response:
  type: mod_event
  content: "IntelEngine_GamemasterEvaluate"
```

**Papyrus Handler:**
```papyrus
; IntelEngine_Gamemaster.psc

Event OnModEvent_IntelEngine_GamemasterEvaluate()
    ; Select random NPCs to evaluate
    Actor[] candidates = GetGamemasterCandidates()

    ; For each candidate, check for story potential
    Int i = 0
    While i < candidates.Length
        EvaluateNPCForStory(candidates[i])
        i += 1
    EndWhile
EndEvent

Function EvaluateNPCForStory(Actor npc)
    ; Get NPC's relationships and memories
    ; Use SkyrimNet API to query

    ; Check for story hooks:
    ; - Family member in danger (based on relationships)
    ; - Important news to share
    ; - Desire to visit player (based on relationship rank)

    ; If story hook found, create event
EndFunction
```

**Dynamic Quest Generation Example:**

Scenario: "Sister in Bleakfalls Barrow"

```papyrus
Function GenerateFamilyInDangerQuest(Actor npc)
    ; Find a relative
    Actor relative = FindRelativeOf(npc)
    If relative == None
        Return
    EndIf

    ; Choose a dungeon
    String dungeon = GetRandomDungeon()

    ; Create the story event via DirectNarration
    String story = "A messenger arrives with urgent news for " + npc.GetDisplayName() + ". "
    story += "\"Your " + GetRelationshipType(npc, relative) + ", " + relative.GetDisplayName()
    story += ", has gone missing in " + dungeon + "! You must help them!\""

    ; Trigger the narration
    TriggerDirectNarration(npc, story)

    ; Update NPC's bio to reflect this
    TriggerDynamicBioUpdate(npc, "urgent_concern",
        npc.GetDisplayName() + " learned that " + relative.GetDisplayName() +
        " is in danger in " + dungeon + ". They are deeply worried.")

    ; Assign travel task to NPC (they go to rescue)
    IntelEngine_Travel.TravelTo(npc, dungeon, 0, true, 2)  ; Run there
EndFunction
```

**NPC Visits Player:**

```papyrus
Function CheckForPlayerVisitors()
    ; Only check if player is at home
    If !IsPlayerAtHome()
        Return
    EndIf

    ; Find NPCs with high relationship
    Actor[] friends = GetNPCsWithRelationshipAbove(2)

    ; Pick one randomly
    Actor visitor = friends[Utility.RandomInt(0, friends.Length - 1)]

    ; Have them travel to player's home
    ObjectReference playerHome = GetPlayerCurrentHome()

    ; Assign task
    IntelEngine_Travel.TravelTo(visitor, playerHome, 24.0, true, 0)

    ; Create approach narration
    TriggerDirectNarration(visitor, visitor.GetDisplayName() +
        " decided to visit " + Game.GetPlayer().GetDisplayName() + ".")
EndFunction
```

### Limitations & Considerations

1. **Performance**: Can't evaluate all NPCs every frame
   - Solution: Periodic evaluation with probability

2. **Memory Access**: Can't easily query arbitrary NPC memories
   - Solution: Use StorageUtil for important story state

3. **Relationship Data**: Limited relationship info in vanilla
   - Solution: Use faction membership + custom relationship tracking

---

## 5. MESSAGE DELIVERY PERSISTENCE

### Requirements
- When NPC A delivers message to NPC B
- NPC B remembers the message
- NPC B knows when player asks them about it

### Feasibility: **FULLY POSSIBLE**

### Implementation

**On Message Delivery:**
```papyrus
Function OnArrivedToDeliver(Int slot, Actor agent, Actor target)
    String message = StorageUtil.GetStringValue(agent, "Intel_Message")
    String senderDesc = StorageUtil.GetStringValue(agent, "Intel_MessageSender")

    ; Store message on target for persistence
    StorageUtil.SetStringValue(target, "Intel_ReceivedMessage", message)
    StorageUtil.SetStringValue(target, "Intel_MessageFrom", agent.GetDisplayName())
    StorageUtil.SetFloatValue(target, "Intel_MessageTime", Utility.GetCurrentGameTime())

    ; Create DirectNarration so target processes it
    String narration = agent.GetDisplayName() + " approached " + target.GetDisplayName()
    narration += " and said: \"" + message + "\""
    TriggerDirectNarration(target, narration)

    ; Trigger memory/bio update for target
    TriggerDynamicBioUpdate(target, "received_message",
        target.GetDisplayName() + " received a message from " +
        agent.GetDisplayName() + ": \"" + message + "\"")
EndFunction
```

**Prompt to Show Received Messages:**
```jinja2
{% if render_mode == "full" or render_mode == "static" %}
{% set received_msg = get_storage_string(actorUUID, "Intel_ReceivedMessage") %}
{% if received_msg and received_msg != "" %}
{% set msg_from = get_storage_string(actorUUID, "Intel_MessageFrom") %}
## Recent Message Received
I recently received a message from **{{ msg_from }}**:
> "{{ received_msg }}"
{% endif %}
{% endif %}
```

---

## 6. SNEAK & ASSASSINATION ACTIONS

### Requirements
- NPCs can sneak into homes
- NPCs can assassinate (sneak behind and attack)
- Works across cities

### Feasibility: **PARTIALLY POSSIBLE**

### What Works
- NPCs can sneak (package with sneak flag)
- NPCs can attack (StartCombat)
- NPCs can travel across cities
- NPCs can enter unlocked buildings

### What's Challenging
- Locked doors (no NPC lockpick animations)
- Stealth kill animations (player-only)
- True "assassination" feel

### Implementation

**Sneak Package:**
```
IntelEngine_SneakTravelPackage
- Type: Travel
- Sneak: Always
- Location: Linked ref
- Combat Override: Don't engage unless attacked
```

**Sneak Into Home Action:**
```papyrus
Bool Function SneakToLocation(Actor akNPC, String destination, Bool stealthMode)
    ; Resolve destination
    ObjectReference destMarker = ResolveDestination(akNPC, destination)
    If destMarker == None
        TriggerDirectNarration(akNPC, akNPC.GetDisplayName() + " doesn't know where that is.")
        Return false
    EndIf

    ; Set stealth mode
    StorageUtil.SetIntValue(akNPC, "Intel_StealthMode", stealthMode as Int)

    ; Apply sneak travel package
    If stealthMode
        ActorUtil.AddPackageOverride(akNPC, SneakTravelPackage, PRIORITY_TRAVEL, 1)
    Else
        ActorUtil.AddPackageOverride(akNPC, TravelPackage_Walk, PRIORITY_TRAVEL, 1)
    EndIf

    ; ... rest of travel setup
EndFunction
```

**Assassination Action:**
```papyrus
Bool Function AssassinateTarget(Actor akNPC, String targetName)
    ; Find target
    Actor target = IntelEngine.FindNPCByName(targetName)
    If target == None
        TriggerDirectNarration(akNPC, akNPC.GetDisplayName() + " doesn't know who " + targetName + " is.")
        Return false
    EndIf

    If target.IsDead()
        TriggerDirectNarration(akNPC, akNPC.GetDisplayName() + " learned that " + targetName + " is already dead.")
        Return false
    EndIf

    ; Travel to target in stealth
    StorageUtil.SetFormValue(akNPC, "Intel_AssassinTarget", target)
    StorageUtil.SetStringValue(akNPC, "Intel_TaskType", "assassinate")

    ; Get target location
    ObjectReference destMarker = GetNPCLocationMarker(target)

    ; Apply sneak travel
    ActorUtil.AddPackageOverride(akNPC, SneakTravelPackage, PRIORITY_TRAVEL, 1)

    ; ... travel setup

    TriggerDirectNarration(akNPC, akNPC.GetDisplayName() + " slipped into the shadows, intent on their dark purpose.")
    Return true
EndFunction

Function OnArrivedAtAssassinTarget(Actor assassin, Actor target)
    ; Approach from behind if possible
    ; Position assassin behind target
    Float targetAngle = target.GetAngleZ()
    Float behindAngle = targetAngle + 180.0

    ; Move to position behind (within reason)
    ; ... positioning logic

    ; Remove stealth package, initiate combat
    Core.RemoveAllPackages(assassin)

    ; Sneak attack bonus will apply if target unaware
    assassin.StartCombat(target)

    TriggerDirectNarration(assassin, assassin.GetDisplayName() + " struck from the shadows!")
EndFunction
```

**Limitations:**
- No true stealth kill animation (would need custom animations)
- NPCs fight to the death (normal combat)
- Locked doors cannot be bypassed

---

## 7. MCM MENU SUPPORT

### Requirements
- View active tasks
- Manually clear tasks
- Configure settings
- Reset stuck NPCs

### Feasibility: **FULLY POSSIBLE**

### Implementation

**MCM Configuration Script:**
```papyrus
Scriptname IntelEngine_MCM extends SKI_ConfigBase

; Pages
String[] Pages

Event OnConfigInit()
    Pages = new String[3]
    Pages[0] = "Active Tasks"
    Pages[1] = "Settings"
    Pages[2] = "Maintenance"

    ; ... MCM setup
EndEvent

Event OnPageReset(String page)
    If page == "Active Tasks"
        ShowActiveTasksPage()
    ElseIf page == "Settings"
        ShowSettingsPage()
    ElseIf page == "Maintenance"
        ShowMaintenancePage()
    EndIf
EndEvent

Function ShowActiveTasksPage()
    SetCursorFillMode(TOP_TO_BOTTOM)

    Int i = 0
    While i < Core.MAX_SLOTS
        String status = Core.GetSlotStatus(i)
        AddTextOption("Slot " + i, status)

        If Core.SlotStates[i] != 0
            AddTextOption("", "[Clear Task]", OPTION_FLAG_NONE)
        EndIf

        i += 1
    EndWhile
EndFunction

Function ShowSettingsPage()
    AddToggleOption("Debug Mode", Core.IntelEngine_DebugMode.GetValue() as Bool)
    AddSliderOption("Travel Priority", Core.PRIORITY_TRAVEL as Float, "{0}")
    AddToggleOption("Confirm Travel", true)
    AddToggleOption("Auto-Resume on Conv End", true)
    AddToggleOption("Enable Gamemaster", true)
EndFunction

Function ShowMaintenancePage()
    AddTextOption("", "[Force Reset All Tasks]")
    AddTextOption("", "[Reload Databases]")
    AddTextOption("", "[Clear All Scheduled Tasks]")
EndFunction
```

---

## 8. COMPLETE FILE STRUCTURE

```
Mods/IntelEngine/
├── IntelEngine.esp
├── IntelEngine.dll
├── README.md
├── docs/
│   ├── DLL_API.md
│   ├── ESP_STRUCTURE.md
│   ├── PACKAGE_PRIORITIES.md
│   ├── ANTI_HALLUCINATION.md
│   └── COMPREHENSIVE_PLAN.md (this file)
├── Source/Scripts/
│   ├── IntelEngine_Core.psc         # Core systems, slots, decorators
│   ├── IntelEngine_Travel.psc       # Travel & navigation
│   ├── IntelEngine_NPCTasks.psc     # Fetch, message, escort
│   ├── IntelEngine_SimpleTasks.psc  # Simple errands
│   ├── IntelEngine_Schedule.psc     # Time-based scheduling
│   ├── IntelEngine_Interrupt.psc    # Pause/resume/cancel
│   ├── IntelEngine_Stealth.psc      # Sneak & assassination
│   ├── IntelEngine_Gamemaster.psc   # Dynamic world events
│   └── IntelEngine_MCM.psc          # MCM configuration
├── SKSE/Plugins/
│   ├── IntelEngine/
│   │   ├── LocationDB.json
│   │   └── NPCAliases.json
│   └── SkyrimNet/
│       ├── config/
│       │   ├── actions/
│       │   │   ├── intel_travel.yaml
│       │   │   ├── intel_fetchnpc.yaml
│       │   │   ├── intel_delivermessage.yaml
│       │   │   ├── intel_schedulemeeting.yaml
│       │   │   ├── intel_fetchdrink.yaml
│       │   │   ├── intel_fetchfood.yaml
│       │   │   ├── intel_stoptask.yaml
│       │   │   ├── intel_resumetask.yaml
│       │   │   ├── intel_canceltask.yaml
│       │   │   ├── intel_followduringtask.yaml
│       │   │   ├── intel_sneakto.yaml
│       │   │   └── intel_assassinate.yaml
│       │   └── triggers/
│       │       └── intel_gamemaster_periodic.yaml
│       └── prompts/submodules/
│           ├── character_bio/
│           │   ├── 0195_intel_spatial_awareness.prompt
│           │   ├── 0196_intel_task_awareness.prompt
│           │   └── 0197_intel_received_messages.prompt
│           └── system/
│               └── 0500_intel_action_guidelines.prompt
└── Interface/
    └── IntelEngine/
        └── IntelEngine_MCM.txt      # MCM translation
```

---

## 9. IMPLEMENTATION PRIORITY

### Phase 1: Core (Must Have)
1. ✅ Travel system (named + semantic)
2. ✅ Fetch NPC
3. ✅ Deliver message
4. ⏳ Interrupt system (pause/resume/cancel)
5. ⏳ Validation in Papyrus (not LLM)

### Phase 2: Extended Features
1. Message persistence (target remembers)
2. Scheduled tasks
3. MCM menu
4. Improved anti-hallucination prompts

### Phase 3: Advanced
1. Stealth/assassination
2. Gamemaster periodic evaluation
3. Dynamic quest generation
4. NPC visits player

### Phase 4: Polish
1. Performance optimization
2. Edge case handling
3. Documentation
4. Testing suite

---

## 10. TECHNICAL CONSIDERATIONS

### Performance
- Gamemaster evaluation: Max 3 NPCs per trigger
- Cooldowns: 30 minutes between major evaluations
- Lazy loading: Only evaluate when needed

### Persistence
- All task state in StorageUtil (survives save/load)
- Aliases for cross-cell persistence
- Recovery on game load

### Compatibility
- Priority 100+ packages override most mods
- MCM allows priority adjustment
- Graceful degradation if SkyrimNet missing

### Code Quality
- Consistent naming conventions
- Comprehensive documentation
- Clear separation of concerns
- Error handling with DirectNarration feedback
