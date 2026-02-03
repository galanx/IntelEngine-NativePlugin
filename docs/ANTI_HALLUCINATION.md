# IntelEngine Anti-Hallucination System

## The Problem

AI-powered NPCs often "hallucinate" - they claim to perform actions that are impossible in the game:

- "I'll go upstairs" (when there are no stairs)
- "I'll fetch Nazim" (misspelled/non-existent NPC)
- "I'll catch some fish for you" (NPCs can't fish)
- "I'll meet you at the Cloud District" (vague, not a specific marker)

This breaks immersion because the NPC says they'll do something, but nothing happens.

## The Solution

IntelEngine implements a multi-layer validation system:

### Layer 1: Pre-Action Validation (Decorators)

Before the AI even considers an action, decorators check feasibility:

```yaml
eligibilityRules:
  - conditions:
      - decoratorName: intel_can_accept_task
        arguments: ["currentActor"]
        comparisonOperator: "=="
        expectedValue: true
    required: true
```

**Decorators implemented:**

| Decorator | Purpose |
|-----------|---------|
| `intel_can_accept_task` | Not already on a task, not dead, not in combat |
| `intel_can_travel_to` | Location exists and is reachable |
| `intel_can_find_npc` | NPC exists in the game |
| `intel_can_resolve_semantic` | "Upstairs" etc. actually resolves |
| `intel_validate_task` | General task validation |
| `intel_get_failure_reason` | Why a task would fail (for AI explanation) |

### Layer 2: Spatial Awareness Prompts

NPCs are given detailed information about their surroundings:

```
## Spatial Awareness

### Current Location
I am currently in **Bannered Mare**.
This is an interior space.

### Exits & Pathways
- **North**: leads to Whiterun (outside)
- **Up**: leads to Bannered Mare (Upper Floor)

### Vertical Navigation
- I can go **upstairs** from here
```

This allows the AI to make informed decisions based on actual game state.

### Layer 3: System Prompt Rules

The `0500_intel_anti_hallucination.prompt` injects critical rules:

```
### The Golden Rule
NEVER claim to do something you cannot verify is possible.

It is better to say "I'm not sure I can do that" than to claim
you'll do something and then fail silently.
```

### Layer 4: Failure Narration

When validation fails, NPCs explain WHY instead of silently failing:

```papyrus
; Instead of just returning false:
If target == None
    Core.SendTaskNarration(akAgent, akAgent.GetDisplayName() +
        " doesn't know anyone named '" + targetName + "'.")
    Return false
EndIf
```

The player sees: `Lydia doesn't know anyone named 'Nazim'.`

### Layer 5: Fuzzy Matching with Suggestions

The DLL provides fuzzy matching AND suggestions for typos:

```cpp
// User says "fetch nazim"
Actor* result = FindNPCByName("nazim");
// Returns Nazeem due to Levenshtein distance match

// If truly unknown:
String suggestion = GetNPCSuggestion("gerdor");
// Might return "Did you mean 'Gerda' or 'Gerdur'?"
```

## Validation Flow

```
Player: "Go fetch Nazeem from Whiterun"

1. AI considers action: IntelFetchNPC

2. Eligibility check (decorators):
   ├─ intel_can_accept_task(Lydia) → true (not busy)
   ├─ intel_can_find_npc("Nazeem") → true (NPC exists)
   └─ is_in_combat(Lydia) → false (not fighting)

3. Action selected by AI

4. Papyrus execution:
   ├─ FindNPCByName("Nazeem") → Actor found
   ├─ GetNPCLocation(Nazeem) → "Whiterun"
   └─ StartFetchTask() → success

5. Narration: "Lydia set off to find Nazeem."
```

Contrast with validation failure:

```
Player: "Go catch some fish"

1. AI considers available actions

2. No fishing action exists

3. AI (with anti-hallucination prompt) responds:
   "I don't know how to fish. But I could fetch you
   some food if there's any nearby?"
```

## What NPCs CAN'T Do

IntelEngine explicitly documents impossible actions:

| Action | Why Impossible | NPC Response |
|--------|---------------|--------------|
| Fish | No NPC fishing mechanics | "I don't know how to fish" |
| Mine | Player-only animations | "I can't mine ore" |
| Lockpick | No lockpick animation (yet) | "I don't have the skill/tools" |
| Teleport | By design - natural travel | Will walk there instead |
| Enter restricted areas | Game engine limitation | "I can't get in there" |

## Testing the System

### Test Case 1: Valid Task
```
Player: "Lydia, go to Whiterun and fetch Nazeem"
Expected: Lydia travels to Whiterun, finds Nazeem, brings him back
Result: SUCCESS
```

### Test Case 2: Invalid NPC
```
Player: "Lydia, fetch me Bob the Builder"
Expected: Lydia says she doesn't know anyone named that
Result: "I don't know anyone named 'Bob the Builder'."
```

### Test Case 3: Invalid Location
```
Player: "Go to Narnia"
Expected: NPC expresses confusion
Result: "I don't know where 'Narnia' is."
```

### Test Case 4: Semantic Resolution
```
Player: "Go upstairs and wait for me"
Expected: NPC checks if stairs exist, travels if yes
Result (if stairs): Travels upstairs
Result (if no stairs): "I don't see a way upstairs from here."
```

### Test Case 5: Impossible Action
```
Player: "Go catch fish at the river"
Expected: NPC admits inability
Result: "I don't know how to fish. Is there something else I can help with?"
```

## Configuration

Debug mode can be enabled to see validation in action:

```
IntelEngine_DebugMode = 1
```

This shows notifications for:
- Decorator evaluations
- Location resolutions
- Validation failures with reasons

## Future Improvements

1. **Lockpicking support** - Simulate lock picking when NPCs have skill + lockpicks
2. **Crafting validation** - Check for workstations and materials
3. **Combat awareness** - Better handling of dangerous travel routes
4. **Memory of failures** - NPCs remember what they couldn't do

## Summary

The anti-hallucination system ensures NPCs:

1. **Know their limits** - Via decorators and prompts
2. **Explain failures** - Via narration when tasks can't complete
3. **Suggest alternatives** - Via AI reasoning with context
4. **Never silently fail** - Always communicate with the player

This creates believable, intelligent NPCs that feel alive because they respond realistically to requests rather than making impossible promises.
