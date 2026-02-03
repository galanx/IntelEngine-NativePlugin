# IntelEngine Package Priority System

## The Challenge

Skyrim AI packages use priorities to determine which package "wins" when multiple are applied. Higher priority = takes precedence.

IntelEngine needs careful priority management for:
1. Active task execution (travel, fetch, escort)
2. Interrupts ("stop and listen to me")
3. Following player mid-task
4. Resuming paused tasks
5. Returning to default behavior after cancellation

## Priority Hierarchy

```
Priority | State              | Package              | When Used
---------|--------------------|-----------------------|----------------------------------
  110    | Interrupted        | ListenPackage        | Player said "stop" - NPC listens
  105    | Urgent Follow      | FollowPlayerUrgent   | "Follow me NOW" during combat
  100    | Active Travel      | TravelPackage_*      | Traveling to destination
   95    | Task Follow        | FollowPlayerTask     | Following player mid-task
   90    | At Destination     | SandboxPackage       | Waiting at destination
   85    | Target Following   | FollowAgentPackage   | Target NPC following messenger
   80    | Paused Sandbox     | PausedSandboxPackage | Paused mid-task, sandboxing nearby
   75    | Cancelled Sandbox  | DefaultSandboxPackage| Task cancelled, return to normal
    0    | Default            | (none from us)       | NPC's original AI takes over
```

## State Machine

```
                              ┌─────────────────────┐
                              │    IDLE (state=0)   │
                              │  No IntelEngine pkg │
                              └──────────┬──────────┘
                                         │
                              "Go to Whiterun"
                                         │
                                         ▼
┌─────────────────────────────────────────────────────────────────┐
│                     ACTIVE TASK (state=1,2,3)                    │
│                    TravelPackage (priority 100)                  │
└─────────────────────────────────────────────────────────────────┘
         │                    │                    │
    "Stop"/"Wait"      "Follow me"           "Cancel"/"Nevermind"
         │                    │                    │
         ▼                    ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│ PAUSED (state=5)│  │FOLLOWING(state=6)│  │CANCELLED(state=0)│
│ ListenPkg (110) │  │FollowPkg (95)   │  │ SandboxPkg (75) │
│ then Sandbox(80)│  │ Task preserved  │  │ Slot cleared    │
└────────┬────────┘  └────────┬────────┘  └─────────────────┘
         │                    │
    "Continue"/               │
    "Resume"/                 "Resume task"/
    Conv. ends                "Go back to what you were doing"
         │                    │
         ▼                    ▼
┌─────────────────────────────────────────────────────────────────┐
│                     ACTIVE TASK (state=1,2,3)                    │
│                  TravelPackage (priority 100) restored           │
└─────────────────────────────────────────────────────────────────┘
```

## Interrupt Flow Details

### "Stop" / "Wait" / "Hold on"

1. NPC is traveling (priority 100 travel package)
2. Player says "stop"
3. System:
   - Stores current state in StorageUtil (`Intel_PausedState`, `Intel_PausedDestination`)
   - Removes travel package
   - Applies ListenPackage (priority 110) briefly for face-to-face
   - After 1 second, switches to PausedSandboxPackage (priority 80)
   - Sets state to PAUSED (5)
4. NPC responds: "Yes? What is it?"
5. NPC sandboxes nearby while listening

### "Continue" / "Resume" / "Go on" / Conversation Ends

1. NPC is paused (state 5)
2. Player says "continue" or conversation naturally ends
3. System:
   - Restores state from `Intel_PausedState`
   - Removes PausedSandboxPackage
   - Re-applies TravelPackage (priority 100)
   - Sets linked ref back to destination
   - Sets state back to active
4. NPC responds: "Alright, I'll continue on my way"

### "Cancel" / "Nevermind" / "Forget it"

1. NPC is on task (any state 1-5)
2. Player says "cancel" or "nevermind"
3. System:
   - Clears all IntelEngine packages
   - Clears slot completely
   - Applies DefaultSandboxPackage (priority 75) temporarily
   - After short delay, removes it so NPC's natural AI takes over
4. NPC responds: "Alright, I'll forget about it"

### "Follow me" (during task)

1. NPC is on task (state 1-3)
2. Player says "follow me"
3. System:
   - Preserves task state in StorageUtil (destination, task type, etc.)
   - Removes travel package
   - Applies FollowPlayerTask (priority 95)
   - Sets state to FOLLOWING_PLAYER (6)
4. NPC responds: "Lead the way. I'll get back to my task after"

### "Resume your task" (while following)

1. NPC is following (state 6)
2. Player says "go back to what you were doing"
3. System:
   - Restores task from StorageUtil
   - Removes FollowPlayerTask
   - Re-applies TravelPackage (priority 100)
   - Sets state back to active
4. NPC responds: "Right, I should finish what I started"

## Package Removal Safety

**CRITICAL**: Always remove packages in the correct order to avoid state conflicts.

```papyrus
; When transitioning states, FIRST remove old, THEN add new

Function TransitionToState(Actor npc, Int newState, Package newPkg, Int newPriority)
    ; Remove ALL IntelEngine packages first
    RemoveAllIntelPackages(npc)

    ; Small delay for engine to process
    Utility.Wait(0.1)

    ; Apply new package
    ActorUtil.AddPackageOverride(npc, newPkg, newPriority, 1)

    ; Force evaluation
    npc.EvaluatePackage()
EndFunction
```

## Conflict Resolution

### What if player spams "stop" repeatedly?

The system checks current state before processing:
```papyrus
If currentState == STATE_PAUSED
    ; Already paused, acknowledge but don't re-pause
    Core.SendTaskNarration(npc, npc.GetDisplayName() + " is already waiting.")
    Return
EndIf
```

### What if NPC gets stuck between states?

MCM provides manual reset:
- "Clear Task" - Removes all packages, clears slot
- "Force Reset All" - Nuclear option, clears everything

### What if packages conflict with other mods?

Priority 100+ is intentionally high. Most mods use 50-80.
If conflicts occur, user can adjust via MCM globals.

## MCM Configuration

```
IntelEngine MCM:
├── Active Tasks
│   ├── Slot 0: [NPC Name] - [Task] → [Clear] button
│   ├── Slot 1: Empty
│   └── ...
├── Settings
│   ├── Travel Package Priority: [100] (slider 50-120)
│   ├── Follow Package Priority: [95] (slider 50-120)
│   ├── Debug Mode: [Off/On]
│   └── Auto-Resume on Conv End: [On/Off]
└── Maintenance
    ├── Force Reset All Tasks
    └── Reload Databases
```

## Testing Checklist

- [ ] Interrupt during travel works
- [ ] Resume after interrupt continues to correct destination
- [ ] Cancel clears all state properly
- [ ] Follow during task preserves task
- [ ] Resume task after following works
- [ ] Multiple interrupts don't stack incorrectly
- [ ] MCM clear works for stuck NPCs
- [ ] Save/load preserves paused state correctly
