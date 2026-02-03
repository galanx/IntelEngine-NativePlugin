# IntelEngine ESP Structure - Creation Kit Setup

## Overview

This document describes the Creation Kit setup required for `IntelEngine.esp`. The plugin contains:

- 1 Quest (IntelEngine)
- 10 Reference Aliases (for concurrent tasks)
- 6 AI Packages
- 4 Keywords
- 3 Globals
- 1 Faction

---

## Quest: IntelEngine

**Editor ID:** `IntelEngine`
**Type:** Miscellaneous
**Start Game Enabled:** Yes
**Run Once:** No

### Quest Scripts

Attach the following scripts to the quest:

1. `IntelEngine_Core` - Main initialization and decorator registration
2. `IntelEngine_Travel` - Travel and navigation system
3. `IntelEngine_NPCTasks` - NPC fetch/message/escort tasks
4. `IntelEngine_Schedule` - Time-based scheduling
5. `IntelEngine_SimpleTasks` - Simple errand tasks

---

## Reference Aliases

Create 10 Reference Aliases for concurrent task handling:

### Task Agent Aliases (5)

NPCs actively performing tasks (traveling, fetching, etc.)

| Alias Name | Flags | Purpose |
|------------|-------|---------|
| `AgentAlias00` | Optional, Allow Reuse, Initially Cleared | Task agent slot 0 |
| `AgentAlias01` | Optional, Allow Reuse, Initially Cleared | Task agent slot 1 |
| `AgentAlias02` | Optional, Allow Reuse, Initially Cleared | Task agent slot 2 |
| `AgentAlias03` | Optional, Allow Reuse, Initially Cleared | Task agent slot 3 |
| `AgentAlias04` | Optional, Allow Reuse, Initially Cleared | Task agent slot 4 |

**Packages to attach to each Agent Alias:**
- `IntelEngine_TravelPackage` (conditions: GetLinkedRef with `IntelEngine_TravelTarget`)
- `IntelEngine_EscortPackage` (conditions: GetLinkedRef with `IntelEngine_EscortTarget`)
- `IntelEngine_SandboxPackage` (conditions: GetLinkedRef with `IntelEngine_SandboxLocation`)

### Target Aliases (5)

NPCs being fetched/escorted back to player.

| Alias Name | Flags | Purpose |
|------------|-------|---------|
| `TargetAlias00` | Optional, Allow Reuse, Initially Cleared | Target NPC slot 0 |
| `TargetAlias01` | Optional, Allow Reuse, Initially Cleared | Target NPC slot 1 |
| `TargetAlias02` | Optional, Allow Reuse, Initially Cleared | Target NPC slot 2 |
| `TargetAlias03` | Optional, Allow Reuse, Initially Cleared | Target NPC slot 3 |
| `TargetAlias04` | Optional, Allow Reuse, Initially Cleared | Target NPC slot 4 |

**Packages to attach to each Target Alias:**
- `IntelEngine_FollowAgentPackage` (conditions: follow linked ref)

---

## AI Packages

### IntelEngine_TravelPackage

**Type:** Travel
**Purpose:** Make NPC travel to linked reference marker

**Settings:**
- Location: Near linked reference (keyword: `IntelEngine_TravelTarget`)
- Radius: 256 units
- Walk/Run: Based on package template variant (see below)
- Schedule: Always

**Variants needed:**
- `IntelEngine_TravelPackage_Walk` - Walk speed
- `IntelEngine_TravelPackage_Jog` - Jog speed
- `IntelEngine_TravelPackage_Run` - Run speed

### IntelEngine_EscortPackage

**Type:** Escort
**Purpose:** Lead target NPC back to player

**Settings:**
- Escort Target: Linked reference (keyword: `IntelEngine_EscortTarget`)
- Follow Distance: 200 units
- Wait at Location: No (keep moving)

### IntelEngine_FollowAgentPackage

**Type:** Follow
**Purpose:** Make target NPC follow the agent

**Settings:**
- Follow Target: Linked reference (keyword: `IntelEngine_AgentLink`)
- Follow Distance: 150 units
- Stay close: Yes

### IntelEngine_SandboxPackage

**Type:** Sandbox
**Purpose:** Idle at destination while waiting

**Settings:**
- Location: Current location OR linked reference
- Radius: 512 units (small area)
- Allow Eating: Yes
- Allow Sleeping: No
- Allow Sitting: Yes

### IntelEngine_WaitPackage

**Type:** Sandbox/Guard
**Purpose:** Wait at specific spot for player

**Settings:**
- Location: Linked reference (keyword: `IntelEngine_WaitLocation`)
- Radius: 64 units (stay put)
- Facing: Toward linked marker if set

### IntelEngine_ApproachPackage

**Type:** Travel
**Purpose:** Walk to a nearby object/NPC

**Settings:**
- Location: Near linked reference
- Radius: 128 units
- Walk only: Yes
- Single use: Yes (complete when arrived)

---

## Keywords

| Editor ID | Purpose |
|-----------|---------|
| `IntelEngine_TravelTarget` | Linked ref keyword for travel destination |
| `IntelEngine_EscortTarget` | Linked ref keyword for NPC being escorted |
| `IntelEngine_AgentLink` | Linked ref keyword for agent→target follow |
| `IntelEngine_WaitLocation` | Linked ref keyword for wait/meeting spot |
| `IntelEngine_SandboxLocation` | Linked ref keyword for sandbox area |
| `IntelEngine_TaskAssigned` | Keyword applied to NPCs on active tasks |

---

## Globals

| Editor ID | Type | Default | Purpose |
|-----------|------|---------|---------|
| `IntelEngine_DebugMode` | Short | 0 | Enable debug messages (1=on) |
| `IntelEngine_MaxConcurrentTasks` | Short | 5 | Max simultaneous tasks |
| `IntelEngine_DefaultWaitHours` | Float | 24.0 | Default wait time at destination |

---

## Faction

### IntelEngine_TaskFaction

**Editor ID:** `IntelEngine_TaskFaction`
**Purpose:** Track NPCs currently assigned to tasks

NPCs are added to this faction when given a task and removed on completion. This allows:
- Quick check if NPC is busy
- Prompt templates to query task status
- Prevention of double-assignment

---

## Package Priority Guidelines

| Priority | Package Type | When Used |
|----------|--------------|-----------|
| 100 | Travel/Escort | Active task execution |
| 90 | Wait/Sandbox at destination | Arrived, waiting for player |
| 80 | Follow agent | Target following messenger |
| 70 | Approach | Walking to nearby object |

Priority 100 ensures IntelEngine tasks override:
- Default sandbox packages
- Schedule packages (eat, sleep, work)
- Other mod packages (typically 50-70)

---

## Script Property Setup

### IntelEngine_Core.psc Properties

```
Quest Property IntelEngine Auto
; Self-reference for quest access

ReferenceAlias Property AgentAlias00 Auto
ReferenceAlias Property AgentAlias01 Auto
ReferenceAlias Property AgentAlias02 Auto
ReferenceAlias Property AgentAlias03 Auto
ReferenceAlias Property AgentAlias04 Auto

ReferenceAlias Property TargetAlias00 Auto
ReferenceAlias Property TargetAlias01 Auto
ReferenceAlias Property TargetAlias02 Auto
ReferenceAlias Property TargetAlias03 Auto
ReferenceAlias Property TargetAlias04 Auto

Keyword Property IntelEngine_TravelTarget Auto
Keyword Property IntelEngine_EscortTarget Auto
Keyword Property IntelEngine_AgentLink Auto
Keyword Property IntelEngine_WaitLocation Auto

Package Property IntelEngine_TravelPackage_Walk Auto
Package Property IntelEngine_TravelPackage_Jog Auto
Package Property IntelEngine_TravelPackage_Run Auto
Package Property IntelEngine_EscortPackage Auto
Package Property IntelEngine_FollowAgentPackage Auto
Package Property IntelEngine_SandboxPackage Auto
Package Property IntelEngine_WaitPackage Auto

Faction Property IntelEngine_TaskFaction Auto

GlobalVariable Property IntelEngine_DebugMode Auto
GlobalVariable Property IntelEngine_MaxConcurrentTasks Auto
GlobalVariable Property IntelEngine_DefaultWaitHours Auto
```

---

## XMarker Placement

For semantic location resolution, the DLL needs reference markers. These are dynamically found at runtime by scanning cells for:

1. **Doors** - Teleport markers indicate where doors lead
2. **Stairs** - Furniture markers at top/bottom of stairs
3. **Notable furniture** - Bars, fireplaces, beds with ownership

No manual XMarker placement required - the DLL scans existing game data.

---

## Testing Checklist

After setting up in Creation Kit:

- [ ] Quest starts on game load
- [ ] Scripts compile without errors
- [ ] All properties are filled
- [ ] Packages have correct conditions
- [ ] Keywords are properly named
- [ ] Faction is created

**In-game testing:**
```
; Console commands for testing
help IntelEngine
sqv IntelEngine
; Should show all aliases as "None" initially
```

---

## Compatibility Notes

### Load Order
- Load after SkyrimNet.esp
- Load after any mods that add locations/NPCs you want to reference

### Conflicts
- Mods that heavily modify AI packages may interfere
- Priority 100 should override most conflicts
- If issues occur, raise priority to 110

### Safe to Merge
This ESP can be merged with other patches using zMerge or similar tools.
