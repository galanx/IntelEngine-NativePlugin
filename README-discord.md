# 🧠 IntelEngine
### Autonomous NPC Dispatch & Scheduling for SkyrimNet

*"Meet me at the Western Watchtower at sunset."*
*She agrees. Hours pass. The sun dips. You arrive — and she's already there.*

---

## 🌍 What This Mod Does

IntelEngine is a task execution framework for [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin). Where SkyrimNet gives NPCs the ability to think and speak through AI, IntelEngine gives them the ability to **act** — physically, across the game world, on their own two feet.

Through natural conversation, any NPC can:
- 📍 **Travel** to named locations, relative directions, or semantic destinations
- 🏃 **Fetch** people and escort them back to you on foot
- 💬 **Deliver messages** to anyone — with optional meeting requests
- 🔍 **Search** for someone alongside you, traveling together
- ⏰ **Schedule** any of the above for a future time

No teleportation. No console commands. No hardcoded location lists. Everything is dynamically indexed from your actual load order at runtime.

---

## ⏰ Scheduling — The Core Feature

You don't just dispatch NPCs right now — you plan ahead, and the world follows through.

### 📅 Schedule Meeting
> *"Meet me at the Bannered Mare at sunset."*
> *"See you at Dragonsreach tomorrow morning."*
> *"Let's meet at the Western Watchtower in three hours."*

NPCs understand natural time expressions:
- **Named times** — dawn, sunrise, morning, noon, afternoon, evening, sunset, dusk, night, midnight
- **Relative** — "in 2 hours", "in half an hour", "soon"
- **Descriptive** — "tonight", "tomorrow", "tomorrow morning"

✨ **Early departure** — NPCs calculate travel time and leave early enough to arrive on time
✨ **Natural arrival** — NPCs idle naturally at the meeting point (sitting, leaning). Walk up and the meeting is live. Walk away and it ends.
✨ **No-shows remembered** — If you never show, the NPC waits (configurable timeout), then leaves. They remember.
✨ **Lateness tracked** — Both NPC and player lateness are recorded and persist in memory

### 📅 Schedule Fetch
> *"Go get Ysolda after sunset."*
> *"Bring me Nazeem tomorrow morning."*

NPC stays put until departure time, then travels to the target and escorts them back.

### 📅 Schedule Delivery
> *"Tell Adrianne after sunset that the shipment is ready."*

Message delivered at the right time. If it includes a meeting request, the recipient gets scheduled too.

**Up to 10 tasks** can be scheduled simultaneously.

---

## ⚡ Immediate Tasks

### 📍 Go To Location
> *"Go to the Bannered Mare." / "Head upstairs." / "Wait outside." / "Leave."*

The location resolver understands:
- **Named places** — Whiterun, Dragonsreach, Bannered Mare
- **Relative directions** — upstairs, downstairs, outside, inside, the back room, kitchen, bedroom
- **Fuzzy matching** — typos still resolve (*"Bannred Mare"* → Bannered Mare)
- Three speeds — **walk**, **jog**, or **run**

### 🏃 Fetch Person
> *"Go get Adrianne and bring her here." / "Fetch Nazeem."*

NPC walks to the target **anywhere in Skyrim**, has a brief exchange, and escorts them back on foot. The fetched person **lingers naturally** — sitting, leaning, idling — until you walk away.

NPC names matched with **typo tolerance** via Levenshtein distance.

### 💬 Deliver Message
> *"Tell Jarl Balgruuf about the dragon sighting."*
> *"Go tell Alvor to meet me at the Bannered Mare in two hours."*

NPC carries your words face-to-face **across any distance**. The recipient **remembers** who sent it, who carried it, and what was said. If the message includes a meeting request, the recipient gets **automatically scheduled**.

### 🔍 Search For Actor
> *"Take me to Ysolda." / "Help me find Nazeem."*

Unlike Fetch, you travel **together**. The NPC leads, you follow. If you fall behind, they **pause and wait**.

---

## 🎮 Mid-Task Control

### ⚡ Change Speed
> *"Hurry up!" / "Run!" / "Slow down." / "Walk."*

Adjust pace on the fly without interrupting the task.

### ✋ Cancel Task
> *"Stop." / "Wait." / "Hold on." / "Come back."*

Immediately halts the current active task. Scheduled tasks are unaffected.

---

## 🧭 Navigation Intelligence

### 🗺️ Dynamic World Indexing
On game load, the C++ SKSE plugin **indexes every actor and location** across all loaded cells — including mod-added content. If a mod adds a new tavern to Whiterun, IntelEngine can dispatch NPCs there.

### 🚪 Cross-Cell Travel
**Every task works across the entire game world.** Send an NPC from Riverwood to Riften. NPCs walk through doors, cross cell boundaries, traverse the open world. No range limit.

### 🏠 Semantic Location Resolution
- *"Go upstairs"* → scans doors and furniture by Z-axis
- *"Go outside"* → identifies exterior doors
- *"Go to the back room"* → resolves interior spaces
- *"Go to my room"* → finds nearest bed
- *"Leave"* → finds nearest exit
- *"Go to the kitchen"* → finds cooking stations

### 🔧 Stuck Recovery (3-Layer)
Skyrim's pathfinding sometimes fails. IntelEngine monitors positions continuously and responds:

1. **Soft recovery** — re-evaluate AI packages
2. **Waypoint navigation** — redirects NPC to nearby settlement markers on known-good navmesh
3. **Progressive teleport** — nudges NPC toward destination with multi-angle attempts

### ✅ Departure Verification
Verifies NPCs actually leave their starting position. If immobilized, recovery escalates before the task is abandoned.

---

## 🧠 NPC Memory

IntelEngine injects **four awareness prompts** into every NPC's character bio via SkyrimNet:

- **📋 Task Awareness** — NPCs know what they're doing and maintain a history of completed tasks
- **⏰ Schedule Awareness** — NPCs know their commitments and how long until departure
- **🤝 Meeting Outcomes** — NPCs remember if you showed up on time, late, or not at all
- **💌 Received Messages** — Recipients retain who sent it, who carried it, and what was said

---

## ⚙️ MCM Configuration

Three pages in SkyUI Mod Configuration Menu:

**📋 Active Tasks** — Real-time view of all 5 slots. Clear individual tasks or reset all.
**📅 Scheduled Tasks** — View pending tasks with times. Cancel individually or clear all.

**🔧 Settings:**
- `Debug Mode` — Verbose logging (default: off)
- `Max Concurrent Tasks` — 1-5 simultaneous NPCs (default: 5)
- `Default Wait Hours` — How long NPCs wait at destinations, 6-168h (default: 48)
- `Task Confirmation` — Prompt before executing (default: on)
- `Report Back After Delivery` — Messengers narrate completion (default: on)
- `Meeting Timeout` — Wait time at meeting spots, 1-12h (default: 3)

---

## 🏗️ Architecture

**SKSE Native Plugin (C++):**
- Dynamic world indexing from actual load order
- Unified location resolver (named, semantic, fuzzy)
- NPC search with Levenshtein distance tolerance
- Natural language time parsing
- Native stuck & departure detection per slot
- Cell analysis (doors, furniture, spatial scanning)

**Papyrus Scripts:**
- AI package management (walk/jog/run)
- Slot-based state machine with dual persistence
- Schedule monitoring with distance-based early departure
- Save/load recovery with full package reconstruction
- MCM interface

**SkyrimNet Action YAMLs:**
- 9 AI-selectable actions with eligibility rules and typed parameters
- Event strings feed context back into NPC awareness

---

## 🔮 Planned Features

- **Eliminate Target** — Dispatch an NPC to kill someone. Assassinations, bounty hunting, or settling grudges.
- **Lockpick Door** — Send an NPC to pick a locked door.
- **Steal Item** — Task an NPC with stealing from someone's inventory or home.
- **NPC-Initiated Visits** — NPCs seek *you* out based on accumulated memories and relationship. A companion tracks you down because they haven't seen you. A friend proposes traveling somewhere together. The world comes looking for you.

---

## ⚠️ Compatibility

> **Warning:** SkyrimNet presents ALL registered actions to its AI. If another mod provides its own travel, fetch, or cancel actions, the AI sees **duplicate options** and may mix-and-match between mods (which breaks things).
>
> **If using another SkyrimNet mod with similar actions**, disable the overlapping ones in either mod. Each action YAML has an `enabled: true/false` flag.

---

## 📦 Requirements

- Skyrim Special Edition / Skyrim VR
- [SKSE](https://skse.silverlock.org/)
- [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin)
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) (MCM)
- [PapyrusUtil](https://www.nexusmods.com/skyrimspecialedition/mods/13048) (persistent storage)
- [powerofthree's Papyrus Extender](https://www.nexusmods.com/skyrimspecialedition/mods/22854) (package management)

---

*IntelEngine doesn't add quests, dialogue, or story. It adds capability. Combined with SkyrimNet's AI, it turns Skyrim's people from scenery into agents you can dispatch across the world — NPCs who walk where you point, carry what you say, fetch who you need, keep appointments, and remember what happened.*
