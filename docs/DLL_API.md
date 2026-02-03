# IntelEngine Native DLL API Specification

## Overview

The `IntelEngine.dll` SKSE plugin provides high-performance functions that would be too slow in Papyrus:

- **Fuzzy string matching** - Find NPCs/locations even with typos
- **Game data indexing** - O(1) hash-based searches built from game data at startup
- **Semantic resolution** - Translate "upstairs" to actual door references
- **Action validation** - Pre-flight checks for feasibility

**No external databases required!** All indexes are built from game data at startup.

## Build Requirements

- Visual Studio 2022
- CommonLibSSE-NG (for AE/SE/VR support)
- CMake 3.21+
- vcpkg for dependencies

## Papyrus API

All functions are exposed to Papyrus via the `IntelEngine` script namespace.

---

## NPC Search Functions

### FindNPCByName
```papyrus
Actor Function FindNPCByName(String searchTerm) Global Native
```

Find an NPC by name using fuzzy matching.

**Parameters:**
- `searchTerm` - Name to search for (e.g., "Nazeem", "nazim", "the annoying guy")

**Returns:**
- Actor reference if found, None if not found

**Algorithm:**
1. Exact match check (case-insensitive)
2. Levenshtein distance fuzzy match (threshold: 3)
3. Partial match (contains substring)

**Index:** Built from all unique NPCs in game data at startup

**Performance:** O(1) average via pre-built hash index

**Example:**
```papyrus
Actor nazeem = IntelEngine.FindNPCByName("Nazeem")
Actor jarl = IntelEngine.FindNPCByName("jarl balgruuf")
Actor fuzzy = IntelEngine.FindNPCByName("nazim")  ; typo still works
```

---

### FindNPCsNearLocation
```papyrus
Actor[] Function FindNPCsNearLocation(String locationName, Float radius) Global Native
```

Find all NPCs near a named location.

**Parameters:**
- `locationName` - Location to search near
- `radius` - Search radius in game units

**Returns:**
- Array of Actor references

---

### GetNPCCurrentLocation
```papyrus
String Function GetNPCCurrentLocation(Actor akNPC) Global Native
```

Get human-readable location name for an NPC.

**Parameters:**
- `akNPC` - The NPC to query

**Returns:**
- Location name (e.g., "Whiterun Marketplace", "Dragonsreach")

---

### IsNPCAccessible
```papyrus
Bool Function IsNPCAccessible(Actor akNPC) Global Native
```

Check if an NPC can be interacted with (not in inaccessible cell, not disabled).

**Parameters:**
- `akNPC` - The NPC to check

**Returns:**
- True if NPC can be reached

---

## Location Functions

### ResolveLocationToCell
```papyrus
Cell Function ResolveLocationToCell(String locationName) Global Native
```

Resolve a location name to a Cell using fuzzy matching.

**Parameters:**
- `locationName` - Named location (e.g., "Bannered Mare", "Dragonsreach")

**Returns:**
- Cell if found, None otherwise

**Use Case:** Give the Cell to Skyrim's AI pathfinding to handle travel.

---

### ResolveLocationToBGSLocation
```papyrus
Location Function ResolveLocationToBGSLocation(String locationName) Global Native
```

Resolve a location name to a BGSLocation (for broader areas).

**Parameters:**
- `locationName` - Named location (e.g., "Whiterun", "Solitude")

**Returns:**
- Location if found, None otherwise

---

### FindDoorToLocation
```papyrus
ObjectReference Function FindDoorToLocation(String locationName) Global Native
```

Find a door in loaded cells that leads to the target location.

**Parameters:**
- `locationName` - Named location

**Returns:**
- Door reference if found in current/loaded cells, None otherwise

**Use Case:** If door is found, NPC can walk to it directly. If None, fall back to AI package travel.

---

### ResolveLocation (Legacy)
```papyrus
ObjectReference Function ResolveLocation(String locationName) Global Native
```

Legacy function - tries FindDoorToLocation first, returns None if not found.

**Returns:**
- Door reference or None

**Supports Fuzzy Matching:**
- City names: "Whiterun", "Solitude", "Riften"
- Specific places: "Dragonsreach", "Blue Palace"
- Inns: "Bannered Mare", "Sleeping Giant"
- Typo tolerance via Levenshtein distance

---

### GetLocationSuggestion
```papyrus
String Function GetLocationSuggestion(String searchTerm) Global Native
```

Get suggested location name for a failed search (for "did you mean?" prompts).

**Returns:**
- Closest matching location name, or empty string

---

### ResolveSemanticLocation
```papyrus
ObjectReference Function ResolveSemanticLocation(Actor akNPC, String semanticTerm) Global Native
```

Resolve a semantic/relative location term to an actual marker.

**Parameters:**
- `akNPC` - The NPC (context for relative directions)
- `semanticTerm` - Relative term ("upstairs", "outside", "the back")

**Returns:**
- ObjectReference marker if resolvable, None if not possible

**Supported Terms:**

| Term | Resolution Logic |
|------|------------------|
| "upstairs" | Find door/stairs where destination Z > current Z |
| "downstairs" | Find door/stairs where destination Z < current Z |
| "outside" | Find nearest door marked as exterior transition |
| "inside" | If outside, find nearest interior door |
| "the back room" | Find door furthest from main entrance |
| "the cellar" / "basement" | Find door to cell with lower Z or matching name |
| "my room" | If NPC has owned bed, find that cell |
| "the bar" / "counter" | Find furniture of type bar/counter |
| "near the fire" | Find fireplace/campfire furniture |

**Example:**
```papyrus
; NPC is in Bannered Mare ground floor
ObjectReference upstairs = IntelEngine.ResolveSemanticLocation(npc, "upstairs")
; Returns marker for Bannered Mare upper floor

ObjectReference outside = IntelEngine.ResolveSemanticLocation(npc, "outside")
; Returns marker outside the inn door
```

---

### GetCellSpatialInfo
```papyrus
String Function GetCellSpatialInfo(Actor akNPC) Global Native
```

Get JSON-formatted spatial information about NPC's current cell.

**Returns:** JSON string with structure:
```json
{
  "cellName": "WhiterunBanneredMare",
  "cellType": "interior",
  "doors": [
    {
      "direction": "north",
      "leadsTo": "Whiterun",
      "isExterior": true,
      "isLocked": false
    },
    {
      "direction": "up",
      "leadsTo": "Bannered Mare (Upper Floor)",
      "isExterior": false,
      "isLocked": false
    }
  ],
  "hasStairsUp": true,
  "hasStairsDown": false,
  "notableAreas": [
    {"name": "bar counter", "direction": "west", "distance": 5.2},
    {"name": "fireplace", "direction": "center", "distance": 3.1}
  ]
}
```

---

### IsSemanticTerm
```papyrus
Bool Function IsSemanticTerm(String term) Global Native
```

Check if a term is a semantic/relative location reference.

**Parameters:**
- `term` - The term to check

**Returns:**
- True if term is semantic ("upstairs", "outside", etc.)

---

## Action Validation Functions

### ValidateAction
```papyrus
Bool Function ValidateAction(Actor akNPC, String actionType, String targetParam) Global Native
```

Pre-validate if an action is possible before attempting.

**Parameters:**
- `akNPC` - NPC who would perform action
- `actionType` - Type of action ("travel", "fetch_npc", "fetch_item", "lockpick")
- `targetParam` - Action-specific target

**Returns:**
- True if action is feasible

**Example:**
```papyrus
; Can this NPC travel to Solitude?
Bool canTravel = IntelEngine.ValidateAction(npc, "travel", "Solitude")

; Can this NPC fetch Nazeem?
Bool canFetch = IntelEngine.ValidateAction(npc, "fetch_npc", "Nazeem")

; Can this NPC lockpick this chest?
Bool canPick = IntelEngine.ValidateAction(npc, "lockpick", refID)
```

---

### GetActionFailureReason
```papyrus
String Function GetActionFailureReason(Actor akNPC, String actionType, String targetParam) Global Native
```

Get human-readable reason why an action would fail.

**Returns:**
- Failure reason string, empty if action is valid

**Example reasons:**
- "Cannot find anyone named 'Nazim' - did you mean 'Nazeem'?"
- "Upstairs is not accessible from here - no stairs found"
- "Lockpicking requires lockpicks (you have none)"

---

## String Utility Functions

### StringToLower
```papyrus
String Function StringToLower(String text) Global Native
```

Convert string to lowercase. ~2000x faster than Papyrus loop.

---

### StringContains
```papyrus
Bool Function StringContains(String haystack, String needle) Global Native
```

Check if string contains substring. ~2000x faster than Papyrus.

---

### LevenshteinDistance
```papyrus
Int Function LevenshteinDistance(String a, String b) Global Native
```

Calculate edit distance between two strings for fuzzy matching.

---

## Index Management

Indexes are built automatically from game data on startup. No external JSON files needed.

### IsIndexLoaded
```papyrus
Bool Function IsIndexLoaded() Global Native
```

Check if NPC and location indexes are built (should be true after game load).

---

### GetIndexStats
```papyrus
String Function GetIndexStats() Global Native
```

Get JSON with index statistics (cell count, location count, NPC count).

---

### RebuildNPCIndex
```papyrus
Function RebuildNPCIndex() Global Native
```

Rebuild NPC index from game data (call after major NPC changes).

---

### RebuildLocationIndex
```papyrus
Function RebuildLocationIndex() Global Native
```

Rebuild location index from game data.

---

## Decorator Registration

The DLL automatically registers these decorators with SkyrimNet on load:

| Decorator | Arguments | Returns | Description |
|-----------|-----------|---------|-------------|
| `can_resolve_location` | `actor`, `locationName` | bool | Can travel to this location? |
| `can_resolve_semantic` | `actor`, `term` | bool | Can resolve "upstairs" etc? |
| `can_find_npc` | `npcName` | bool | Does this NPC exist? |
| `get_npc_location` | `npcName` | string | Where is this NPC? |
| `validate_task` | `actor`, `taskType`, `target` | bool | Is task feasible? |
| `get_task_failure_reason` | `actor`, `taskType`, `target` | string | Why would task fail? |

---

## Performance Characteristics

| Operation | DLL Time | Papyrus Time | Speedup |
|-----------|----------|--------------|---------|
| Find NPC by name | <1ms | 500-2000ms | 500-2000x |
| Resolve location | <0.5ms | 50-200ms | 100-400x |
| Semantic resolution | <2ms | N/A (not possible) | - |
| String lowercase | <0.01ms | 20-50ms | 2000-5000x |
| Levenshtein distance | <0.1ms | 100-500ms | 1000-5000x |

---

## Error Handling

All functions return sensible defaults on error:
- Actor functions return `None`
- String functions return `""`
- Bool functions return `false`
- Numeric functions return `0` or `-1`

Errors are logged to `IntelEngine.log` in the SKSE logs folder.

---

## Thread Safety

All functions are thread-safe and can be called from multiple Papyrus threads simultaneously. Internal data structures use read-write locks for concurrent access.

---

## Memory Management

- NPC index: Built from all unique NPCs in game data (~5-10MB for vanilla + mods)
- Location index: Built from all Cells and BGSLocations (~2-5MB)
- Both indexes are built on game data load event
- Caches are cleared on cell change to prevent stale data
- Total memory footprint: ~10-15MB typical

---

## Building the DLL

```bash
# Clone CommonLibSSE-NG
git clone https://github.com/CharmedBaryon/CommonLibSSE-NG

# Configure
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release

# Output: build/Release/IntelEngine.dll
```

## Testing

Test functions are exposed for validation:

```papyrus
; Test NPC search
IntelEngine.TestNPCSearch("Nazeem")  ; Should find and log

; Test location resolution
IntelEngine.TestLocationResolve("upstairs")  ; Context-dependent

; Test validation
IntelEngine.TestValidation("fetch_npc", "Nazeem")
```
