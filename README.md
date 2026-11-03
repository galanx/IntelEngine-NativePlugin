# IntelEngine
### NPC Autonomy, Faction Politics & Player-Driven Task Framework for SkyrimNet

> **Compatibility:** IntelEngine 3.5.1 and later declares **SkyrimNet Beta 25** as its minimum; 3.6.0 was tested with official **Beta 28**. Beta 24 and earlier are incompatible.
> The 3.6.0 native build includes SE/AE/VR. Tested runtimes: Skyrim **1.6.1170.0** and **1.7.104.0**. Skyrim VR **1.4.15** should work and was partly tested. Skyrim SE **1.5.x**: untested; **1.6.640.0** and **1.7.99.0** are also untested targets.
>
> This repository carries a compatibility update for current SkyrimNet releases. IntelEngine was originally created by [Galanx](https://github.com/galanx).
> Players: report bugs at [IntelEngine GamePlugin issues](https://github.com/galanx/IntelEngine-GamePlugin/issues). This repository holds the SKSE DLL source.

*"Meet me at the Western Watchtower at sunset."*
*She agrees. Hours pass. The sun dips. You arrive — and she's already there.*

---

IntelEngine has two parts that work together to make Skyrim's NPCs feel alive.

**Part 1 — Player-Driven Actions:** You tell NPCs what to do through natural conversation and they follow through physically across the entire game world. Scheduling is the core — meetings, fetches, and deliveries all support future time expressions.

**Part 2 — Dungeon Master (AI-Driven Stories):** An LLM Dungeon Master observes the world state and decides when NPCs should act on their own — seeking you out, ambushing you, spreading gossip, delivering messages, offering quests, and interacting with each other.

---

## The Day Everything Changed

You step out of Breezehome at dawn with more to do than one Dragonborn can handle.

Farengar needs to warn the Jarl. You turn to him: *"Tell Jarl Balgruuf there's been a dragon sighting near the Western Watchtower. Tell him to meet me at the Bannered Mare tonight."* He nods and departs — **delivering your message** on foot through the keep. Balgruuf will hear it, remember who sent it, and the meeting request **schedules him to travel** to the inn that evening.

You need backup at the watchtower, so you find Jenassa at the Drunken Huntsman. *"Meet me at the Western Watchtower at sunset."* She agrees. The **meeting is scheduled**. She doesn't leave now — she knows when to depart, calculates the distance, and **leaves early enough to arrive on time**. You go about your day.

Before heading out, you need Adrianne. *"Lydia, go get Adrianne. The blacksmith. Bring her here."* Lydia walks to Warmaiden's, finds Adrianne at the forge, and **escorts her back** on foot. Adrianne **lingers nearby** — leaning on a post, sitting on a bench — until you've spoken and walk away. She drifts back to the forge on her own.

You tell Lydia to **go to the Bannered Mare** and wait. She descends the steps, crosses the market, pushes through the inn door, and settles into a seat by the fire. But you change your mind halfway — *"Actually, stop. Come back."* She **cancels the task** and turns around. *"Forget that. **Run** to the stables instead."* She takes off at full sprint.

Hours later, the sun touches the mountains. You reach the Western Watchtower. Jenassa is already there — sitting by the wall, calm. She arrived early. **The meeting is live.** You discuss the plan, scout the area, and when you're done, you walk away. **The meeting ends naturally** — no timer, no script, just proximity. She heads off on her own.

That evening, you push through the door of the Bannered Mare. Balgruuf is there — the message reached him, and he **kept his scheduled appointment**. He remembers the dragon warning. He knows Farengar carried it.

But something else happened while you were at the watchtower. **On the road back**, you crossed paths with Adrianne — she was heading to Falkreath to sell swords, minding her own business. You stopped and chatted. She had no idea you'd be there. Neither did you. The **Dungeon Master** placed that encounter because it fit the moment.

And later that night, sitting in the inn, **a warrior you'd wronged three days ago tracked you down**. He snuck through the door, drew his blade, and attacked. You beat him — he yielded, dropped to his knees, and begged for mercy. He told you someone sent him. Someone you thought was a friend. That fact is now in his bio. And in yours.

Tomorrow, things get more complex. You tell a guard: *"Go get Ysolda after sunrise."* The **fetch is scheduled** for morning — the guard stays at his post until dawn, then departs. Meanwhile, **two NPCs at the market start arguing** about something that happened yesterday — the Dungeon Master decided it was time. You overhear it. Both of them remember it differently.

Five NPCs are acting simultaneously. Ten more tasks are queued for the future. Stories are unfolding that you didn't start. The world doesn't wait for you to do everything yourself.

This is **IntelEngine**.

---

## What This Mod Does

IntelEngine is an NPC autonomy framework for [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin). Where SkyrimNet gives NPCs the ability to think and speak through AI, IntelEngine gives them the ability to **act** — physically, across the game world, on their own two feet. Both on your command and on their own initiative.

### Part 1 — Player-Driven Actions

Through natural conversation, any NPC can:

- **Schedule meetings, fetches, and deliveries** for any future time — *"at sunset"*, *"tomorrow morning"*, *"in three hours"*
- **Travel** to named locations, relative directions, or anywhere the game world has a door
- **Fetch** people and escort them back to you on foot
- **Deliver messages** to anyone — with optional meeting requests that schedule the recipient to travel
- **Search** for someone alongside you, traveling together

### Part 2 — Dungeon Master (AI-Driven Stories)

Without any player input, NPCs autonomously:

- **Seek you out** over unfinished business, old friendships, or something they overheard
- **Share gossip** traced to real events, spreading through chains of up to 10 people
- **Ambush you** for real grudges — with stealth approach, combat, and a yield system
- **Secretly follow you** out of obsession until caught
- **Deliver messages** from NPCs who can't come themselves
- **Offer quests** — bounty hunts, rescue missions, item retrieval, and **faction war battles**
- **Interact with each other** independently — arguments, deals, whispered conspiracies

### Part 3 — Faction Politics (NEW in v3.0)

A living political system where 9 factions (configurable) scheme, trade, and wage war:

- **Political DM** generates events every 6 game hours — trade deals, espionage, border skirmishes, assassinations, war declarations, and surrenders
- **Player standing** with each faction rises and falls based on your actions — fight for a faction, betray them, commit crimes in their territory
- **Faction wars** with morale, army strength, off-screen battles, and **player-present battles** with 5 waves of spawned soldiers (22 per side)
- **Faction quests** — when your standing is 20+ with a faction, they call on you for combat missions, rescue operations, and full-scale battles that affect war outcomes
- **Political awareness** — NPCs know about recent political events and react to them in conversation
- **Prisma UI dashboard** with a Politics tab showing all faction relations, active wars, and player standings

**One political timeline per playthrough:** `IntelEngine-<id>.db` is one database shared by all saves in that playthrough, not a separate database per save. Loading an earlier save permanently removes later political events, wars, battles and standing history, and re-opens wars that ended after that save (`CleanupFutureEvents`). To keep a later timeline, back up the database and its `-wal`/`-shm` companions with the game closed before loading an earlier save; the `.ess`/`.skse` pair alone does not preserve it.

Relation changes from battles and war endings survive a reload. Changes already lost in saves made before this fix are not recovered.

No teleportation. No console commands. No hardcoded location lists. IntelEngine dynamically indexes every actor and location across every cell in the game — every inn, every home, every NPC is discoverable because the index is built from your actual load order.

---

## Scheduling — The Core Feature

The ability to coordinate NPCs across time is what sets IntelEngine apart. You don't just dispatch NPCs on errands right now — you plan ahead, and the world follows through.

### Schedule Meeting

> *"Meet me at the Bannered Mare at sunset."*
> *"See you at Dragonsreach tomorrow morning."*
> *"Let's meet at the Western Watchtower in three hours."*

NPCs understand natural time expressions:

| Expression | Resolves To |
|---|---|
| dawn, sunrise | 5-6 AM |
| morning | 8 AM |
| noon, midday | 12 PM |
| afternoon | 2 PM |
| evening | 6 PM |
| sunset, dusk | 7-8 PM |
| night | 10 PM |
| midnight | 12 AM |
| "in 2 hours", "in half an hour" | Relative to current time |
| "tonight" | This evening |
| "tomorrow" | Tomorrow morning |

**Early departure** — NPCs calculate travel time based on distance and depart early enough to arrive before the scheduled hour. A meeting at the Western Watchtower means leaving Whiterun well before sunset — the system estimates the walk and builds in a buffer.

**Natural arrival** — The NPC waits at the meeting point, idling naturally — sitting, leaning, looking around. When you arrive, the meeting is live. Walk away, and it ends. No arbitrary timer. No "press E to end meeting." Just proximity.

**No-shows are remembered.** If you never arrive, the NPC waits until their patience runs out (configurable, default 3 game hours), then leaves. They remember you didn't show — and it colors future interactions differently than a meeting that went well.

**Lateness is tracked.** The NPC knows if they arrived late (pathfinding trouble, long distance). You know if the player was late. The meeting outcome — success, late arrival, or no-show — persists in the NPC's memory.

### Schedule Fetch

> *"Go get Ysolda after sunset."*
> *"Bring me Nazeem tomorrow morning."*

The NPC stays where they are now. When the departure window arrives, they travel to the target, escort them back to you on foot, and the fetched person lingers nearby until you walk away.

### Schedule Delivery

> *"Tell Adrianne after sunset that the shipment is ready."*
> *"Send word to Jarl Balgruuf tomorrow morning."*

A message delivered at the right time. If the message includes a meeting request — *"tell him to meet me at Dragonsreach tonight"* — the recipient is also scheduled to travel there.

**Up to 10 tasks** can be scheduled simultaneously across all NPCs.

---

## Immediate Tasks

### Go To Location

> *"Go to the Bannered Mare."*
> *"Head upstairs."*
> *"Wait outside."*
> *"Leave."*

Send any NPC to a destination right now. The location resolver understands:

- **Named places** — Whiterun, Dragonsreach, Bannered Mare, the Drunken Huntsman
- **Relative directions** — upstairs, downstairs, outside, inside, the back room
- **Fuzzy matching** — typos and partial names still resolve (*"Bannred Mare"* finds the Bannered Mare)
- **Contextual resolution** — *"go upstairs"* scans doors by Z-axis, *"go outside"* identifies exterior doors

Three travel speeds — **walk**, **jog**, or **run** — chosen based on urgency. New tasks automatically replace the current one — you don't need to cancel before redirecting, just speak and the NPC adapts.

### Fetch Person

> *"Go get Adrianne and bring her here."*
> *"Fetch Nazeem."*

The NPC walks to the target — **anywhere in Skyrim** — has a brief exchange, and escorts them back on foot. Ask someone in Whiterun to fetch an NPC living in Riverwood, and they'll walk the road, find them, and walk back together. The target doesn't need to be nearby, loaded, or even in the same hold. IntelEngine's C++ NPC index locates actors across the entire game world, including unloaded cells.

When they arrive, the fetched person **lingers naturally** — sitting on furniture, leaning against walls, idling in place. They behave like someone who was asked to come over, not a mannequin planted in front of you. They stay as long as you're nearby. Walk away, and they return to their routine.

NPC names are matched with **typo tolerance** — Levenshtein distance matching means *"Adiranne"* still finds Adrianne. Failed searches return suggestions.

### Deliver Message

> *"Tell Jarl Balgruuf about the dragon sighting."*
> *"Warn Adrianne that trouble is coming."*
> *"Go tell Alvor to meet me at the Bannered Mare in two hours."*

The NPC carries your words to the recipient face-to-face — **across any distance**. Send a messenger from Solitude to Riften, and they'll make the journey. The recipient **remembers** what was said, who sent it, and who carried it — information that persists in their character context for future conversations.

If the message includes a meeting request, the recipient is automatically **scheduled to travel** there at the specified time. One command chains a delivery into a meeting.

Messengers optionally **report back** to you after delivery (configurable via MCM).

### Search For Actor

> *"Take me to Ysolda."*
> *"Help me find Nazeem."*

Unlike Fetch — where the NPC goes alone — Search means you travel **together**. The NPC leads, you follow. If you fall behind, they **pause and wait** for you to catch up before continuing.

---

## Mid-Task Control

### Change Speed

> *"Hurry up." / "Run!" / "Pick up the pace."*
> *"Slow down." / "Walk." / "No rush."*

Adjust an NPC's travel pace on the fly — walk, jog, or run — without interrupting their current task.

### Cancel Task

> *"Stop." / "Wait." / "Hold on." / "Come back."*

Immediately halts the NPC's current active task. Scheduled tasks are unaffected — those still fire at their appointed time.

---

## Navigation Intelligence

### Dynamic World Indexing

IntelEngine does not use static location lists or hardcoded cell names. On game load, the native C++ SKSE plugin **dynamically indexes every actor and every location** across all loaded cells in your game — including mod-added content. Every inn, home, dungeon, shop, and NPC is discoverable because the index is built from your actual load order at runtime. If a mod adds a new tavern to Whiterun, IntelEngine can dispatch NPCs there.

### Cross-Cell Travel

**Every task in IntelEngine works across the entire game world.** Send an NPC from Riverwood to Riften. Schedule a meeting in Solitude while you're standing in Windhelm. Dispatch a messenger from Whiterun to deliver words to someone in Markarth. NPCs navigate using Skyrim's native AI pathfinding — they walk through doors, cross cell boundaries, traverse the open world between holds, and arrive at their destination on foot. The game engine handles the routing. IntelEngine provides the destination and the intent. There is no range limit.

### Semantic Location Resolution

The destination resolver processes spatial language beyond named locations:

| You say | What happens |
|---|---|
| "Go upstairs" | Scans doors by Z-axis, finds upper floors |
| "Go outside" | Identifies exterior doors in current cell |
| "Go to the back room" | Resolves interior spaces behind the main area |
| "Leave" | Finds the nearest exit |
| "Go to the inn" | Fuzzy-matches against dynamically indexed locations |

> **Note:** Upstairs/downstairs resolution relies on door Z-axis positions, which Skyrim's cell design doesn't always make reliable — some interiors place doors at unexpected heights or use single-level layouts with misleading geometry. Named locations and exterior/interior directions are consistently accurate.

### Stuck Recovery

Skyrim's pathfinding sometimes fails — doors won't open, navmesh gaps block the path, or geometry traps an NPC. IntelEngine monitors positions continuously through native C++ polling and responds with escalating recovery:

1. **Soft recovery** — re-evaluates AI packages, giving the engine another chance
2. **Progressive teleport** — nudges the NPC incrementally toward their destination, with decreasing distances on each attempt
3. **Safety timeout** — force-completes the task after extended time to prevent permanent soft-locks in unloaded cells

**Locked doors** are a common edge case — an NPC dispatched to fetch someone at night may find their target behind a locked home door. When the NPC can't reach the target and gets stuck at the entrance, the stuck detection system catches the stall, escalates through recovery, and if all else fails, narrates the failure so the NPC doesn't silently hang forever.

### Departure Verification

When an NPC is supposed to leave a location, IntelEngine verifies they actually moved from their starting position. If they're immobilized — locked door, blocked path, obstructed AI — recovery escalates before the task is abandoned.

---

## NPC Memory — Awareness Prompts

IntelEngine injects six **SkyrimNet awareness prompts** into every NPC's character bio. These render live game state as natural-language context the AI sees on every dialogue cycle. NPCs don't just execute tasks — they **know** what they've done, what they're doing, and what they've committed to.

### Task Awareness
NPCs know their current task — *"traveling to Dragonsreach"*, *"delivering a message to Nazeem"*, *"returning with Adrianne"* — and maintain a timestamped history of completed tasks. *"I fetched Ysolda earlier today. Delivered a message to the Jarl yesterday."*

### Schedule Awareness
NPCs know their commitments — *"I'm meeting someone at the Bannered Mare this evening"* — and how long until departure. Active meetings, pending schedules, and en-route status are all visible to the NPC's AI.

### Meeting Outcomes
NPCs remember how meetings went. Did the player show up on time? Late? Not at all? Was the NPC themselves delayed? These outcomes persist — a no-show is a different memory than a successful rendezvous, and NPCs can reference either in future conversations.

### Received Messages
When a message is delivered, the recipient retains who sent it, who carried it, what it said, and when it arrived. This information stays in their character context so they can react naturally — *"Farengar told me about the dragon sighting. That was your warning?"*

### Known Facts
Everything the NPC learned through story events — gossip heard, ambush outcomes, stalker catches, quest results. Displayed with natural time references: *"just now"*, *"earlier today"*, *"a few days ago."* Facts expire over time, so recent events feel vivid while old news fades.

### Gossip Network
Both rumors the NPC has heard (and from whom) and rumors they've shared (and to whom). Creates a visible web of social information — *"Ysolda told me that Nazeem was seen lurking near the warehouse."*

---

## Concurrency & Persistence

- **5 simultaneous active tasks** — five NPCs traveling, fetching, or delivering at the same time, each tracked independently
- **10 scheduled tasks** — queued for future execution across all NPCs
- **Save/load safe** — active and scheduled tasks survive saving and reloading; AI packages are reconstructed on game load
- **Independent recovery** — each task slot has its own stuck detection, departure verification, and deadline tracking
- **Follower-aware** — follower NPCs are temporarily released for task execution and restored to your service when they return

---

## MCM Configuration

Three pages in the SkyUI Mod Configuration Menu:

### Active Tasks
Real-time view of all 5 task slots — what each NPC is doing, where, and current status. Clear individual tasks or reset everything.

### Scheduled Tasks
View all pending scheduled tasks with times and targets. Cancel individually or clear all.

### Settings

| Setting | Default | Range | Description |
|---|---|---|---|
| Debug Mode | Off | On/Off | Shows IE debug messages as in-game notifications (always logged to Papyrus.0.log). |
| Max Concurrent Tasks | 5 | 1-5 | How many NPCs can be active at once |
| Default Wait Hours | 48 | 6-168 | How long NPCs wait at destinations |
| Task Confirmation | On | On/Off | Prompt before executing tasks |
| Report Back After Delivery | On | On/Off | Messengers narrate delivery completion |
| Meeting Timeout | 3 hrs | 1-12 | How long NPCs wait at meeting spots |

### Story Tuning

| Setting | Default | Description |
|---|---|---|
| Danger Zone Policy | Allow All | Who can be dispatched to dangerous areas (Allow All / Block Civilians / Followers Only / Block All) |
| Player Home Visit Policy | Allow All | Who can visit when you're at home (same 4 levels) |
| Story Tick Interval | 3 hrs | How often the Dungeon Master evaluates the world |
| Travel Timeout | 7 hrs | Max travel time before a story NPC gives up |
| Meeting Timeout | 5 hrs | Max wait time for scheduled meetings |
| Quest Sub-Types | All On | Toggle rescue, find item, and combat quests individually |

---

## In-Game Dashboard

IntelEngine provides an optional real-time React dashboard via [Magelight UI](https://github.com/Severause/MagelightUI) **1.7.1 or newer** (recommended), with [PrismaUI](https://www.prismaui.dev/) as a frozen fallback. Install a host separately; neither runtime is bundled. With neither usable host, the dashboard is unavailable but IE gameplay and the MCM remain enabled.

Open with the configured hotkey (default: Shift+7). Skyrim must have foreground focus and exactly the configured Ctrl/Shift/Alt modifiers; release a held combo after returning focus. Another host's input ownership blocks opening without stealing focus or showing a notice. Magelight preserves the dashboard's game pause and supports Escape close; PrismaUI retains its existing X/hotkey close behavior without new Escape support.

Host selection is fixed until restart. In `Data/SKSE/Plugins/SkyrimNet/config/plugins/IntelEngine/settings.yaml`, set `ui.host` to `auto` (default, prefer Magelight), `magelight` (prefer Magelight), or `prismaui` (prefer PrismaUI). Each preference falls back to the other usable host; unknown values warn and use auto. Restart Skyrim after changing this setting; Reload UI does not switch hosts. A missing, too-old or unusable Magelight host logs the reason for PrismaUI fallback.

Change the dashboard hotkey in the dashboard's Settings tab or on the SkyrimNet settings page (click Save); both apply immediately. The MCM has no hotkey option.

Section headers in Tasks, Story, Politics, Director, Actions and Packages toggle their bodies with ▾/▸ indicators. All sections start open; each section's choice is remembered in browser localStorage across reloads. Header controls remain usable without toggling. Offline headless regression: `python SKSE/tests/RunDashboardDropdownCheck.py "<chrome.exe>" web/dashboard/dist --scenario=dashboard_sections_collapse`; game-host behavior is not live-verified.

### Tabs

**Tasks** — Live view of all active and scheduled tasks. Cancel individual tasks, see agent names, destinations, task types, speeds, and scheduled meeting timing ("in ~3h", "very soon", "overdue"). A successful task-card cancellation shows `<Name>'s task cancelled.`. Cancelling the active story NPC's task also ends the story and its tracking, clearing the Story tab.
Slot allocation and clearing refresh the open dashboard immediately; a closed dashboard skips snapshot building.

Cancelled stories with narration add a `stopped before finishing: ...` NPC fact before cleanup. Generic load abandonment and the six-hour watchdog add `never finished: ...` facts; this is source/compiled-bytecode behavior, not live-verified.

Task cards show recorded origin, in-game allocation time and existing dispatch reason. Scheduled cards retain their creation provenance; legacy missing times stay blank. SkyrimNet dialogue actions currently provide origin only: upstream action mappings do not forward the triggering dialogue response.

**Story Engine** — Toggle the Story Engine and individual story types on/off. See the active dispatch, next DM check-in timer, NPC social activity, active quests, and a log of recent NPC-to-NPC interactions.

Activity dates use the current save's in-game calendar and display to the nearest minute, including political events, war timelines, battle history and new NPC activity entries. Older entries without a timestamp remain undated; a recorded zero is a valid timestamp.

**Politics** — Faction cards list configured leaders from the existing political data, retaining unloaded names and omitting confirmed permanent deaths.

**Director** — Manual Dungeon Master controls. Dispatch any story type to any NPC with full parameter fields (informant gossip, quest locations, message content, etc.). Execute any of the 11 player-driven actions, including ReportPlayerConduct, with guided field hints and YAML enum choices. Action Execution's NPC combobox filters the existing loaded-NPC list (within 4000 units in the same exterior worldspace or interior cell) while typing; Enter/click picks the actor's unchanged FormID. Unpicked/free text cannot execute, and × clears the pick. Manual CancelCurrentTask stops the current task even beside the player; SkyrimNet-driven cancellation retains its proximity guard.

Every Director NPC-name field uses the shared suggestion picker: nearby names first (marked `nearby`), then sorted known NPC display names, with free text still allowed. This includes informant subjects/sources, quest givers/rescue victims and action target names. Hired by/Encouraged by append marked factions and retain faction IDs. Lists render at most 50 matches, reserving up to 10 rows for matching factions. Action Execution alone requires a nearby list pick and retains its actor FormID. `knownNpcs` is sent on dashboard DOM-ready or index-count changes, retained across periodic snapshots, and kept in coalesced pending snapshots until delivery. Offline `npc_picker_consistent` covers a 5,007-name population-scale synthetic fixture; this is not an installed-modlist count or live-game verification.

Story, NPC Social and Political forms have Generate and Clear controls. Generate drafts empty fields and re-rolls its own generated fields, preserving nonempty user-edited values and picked dropdowns; untouched base dropdowns show examples and let the model choose valid options. Political event type starts empty and must be picked or generated before Dispatch. Review the result, then Dispatch separately. While generating, that form is disabled and Clear becomes Stop: Stop ignores the eventual result without changing the draft. Clear resets all fields and touched flags. Failures retain the draft for retry; older SkyrimNet builds without the custom-prompt export show `Generate unavailable`. Native completion is delivered through the UI host thread and discarded after dashboard close/reopen. One shared `intel_director_generate` prompt serves all three forms; Action Execution and Papyrus dispatch routes are unchanged. Built-bundle and native lifecycle checks are offline evidence, not live model/game acceptance.

Director section titles remain left-aligned with small Generate/Clear/Stop controls on the right. All base fields and Action variants share `Field`: label above, control, hint beneath, with red required markers in labels. Base placeholders are examples; untouched variant dropdowns retain `(auto)`. Missing-field lists remain beneath Dispatch. FetchPerson's optional Refuse reason leaves the person willing to come when empty; a filled reason makes them refuse.
Director drafts stay mounted but hidden when another dashboard tab is selected, so switching tabs preserves fields without storing drafts. Active Story and NPC Social engines show italic amber busy details in their section titles, including when collapsed. Story Dispatch is disabled as `Story engine busy`; Social shows `NPC social busy` but remains enabled because Director Papyrus does not refuse it solely for an active social engine. NPC slot confirmation still takes precedence on the enabled Social button.
Director Story, NPC Social and busy notices format raw keys as friendly words (`seek_player` → `Seek Player`). Political notices retain the dashboard's event label, use a friendly event key when absent, and name both faction IDs as friendly words.
Each completed Generate reports its request's filled/blank field names and the dashboard's applied/rejected outcome (with reasons) in one IntelEngine info log line. Native replies are logged at info before parsing (first 600 characters, including field text); malformed errors include the first 120 characters. The shared parser extracts the outermost JSON object to accept code fences or leading prose.
Generate includes all visible blank variant fields in its request and asks for every base/variant field, including optional text and relation delta. The dashboard is the single field-validation boundary: valid returned fields apply even if another field has an invalid key, type, option, identity or relation delta. Invalid fields are skipped with logged reasons; chosen identities and touched values remain unchanged. Model omissions remain blank for review rather than being invented locally. Political variants are empty because no political type-specific controls exist; an empty touched map does not mean selected identities can be replaced.
Generate passes an explicit blank-field list to the shared prompt, which requires non-empty suggestions for every applicable blank, especially narration and description. This is prompt guidance, not a live model-compliance guarantee; no retries or local filler are added.
Generate opens its collapsed section, omits placeholder examples from variant metadata and asks for nearby NPC names and faction IDs. Returned free-text NPC names remain dispatch-resolved; empty/`None` names are silently skipped, and faction display names normalize to option IDs. Existing identities remain protected. Director Story/Social NPC lookup failures retain their debug log and also notify the player that no unique match was found.
Shared comboboxes discard their old search draft when a nonempty controlled selection changes, so generated choices remain visible after clearing/filtering a dropdown.

Director Story, NPC Social and Action Execution require a second click when a selected NPC owns an active slot: the first click changes the button to `Busy — you sure?`; the second sends the existing slot cancellation(s), then dispatches. Changing an NPC resets confirmation. Papyrus still refuses busy task-creating requests if cancellation has not completed; automatic requests retain their existing behavior.

Director Story also refuses a new story while the story engine is active, even for a different NPC. Finish or cancel the active story before dispatching another; the notification identifies its NPC and story type.

Director Action Execution shows `<Name>: <action> started.` only when one of its nine Bool-returning actions succeeds; cancellation and conduct reports retain their existing feedback. Story shows `<ActiveStoryNPC name> sets out (<type>).` only while active after its dispatch generation advances, including message couriers rather than senders; road encounters do not advance that generation and are skipped. NPC Social notifies after queued travel or an applied off-screen outcome; Politics notifies only when its existing `acted` signal is true. Automatic paths receive no new success notices. Director submit buttons show `Sent` for about two seconds after sending; this is submission feedback, not proof the game accepted the request.

Pick-only dashboard dropdowns use the shared type-to-filter combobox in both hosts; no plain-select variant remains. They filter existing choices while typing, pick with Enter/click and clear with ×; unlike the free-text NPC suggestions above, unpicked search text is never submitted. Director dispatch requires picked story/social/political types and Action Execution requires a picked nearby actor and valid action parameters. Clearing/filtering the Settings hotkey only edits its search draft; only a list pick commits a numeric key. Comboboxes and Director suggestions measure their displayed list height (up to 128 CSS pixels, including UI scale) within the viewport and clipping containers: down when it fits, otherwise up when it fits, otherwise the larger side with a clamped scrollable height. Pick-only dropdowns support arrow keys, Escape, outside-click/focus dismissal, current-value highlighting and disabled state. Containing-pane scroll/resize dismisses open lists; list scrolling stays open. Magelight Escape capture is held only while a list is open, including when focus is on its clear button.

ReportPlayerConduct uses the faction suggestion picker (submitting its ID), initializes YAML enums to their first choice, and requires a valid faction, sentiment and nonempty reason before Execute is enabled. Conduct reports notify their standing-change count or the failure reason; native processing also accepts configured display names case-insensitively.

**Active Packages** — See which NPCs are currently running IntelEngine AI packages (travel, sandbox, stalk). Remove packages from stuck NPCs directly.

**Settings** — Shared settings behavior with the MCM. Politics settings are read from `settings.yaml` and saved there by both interfaces; plugin configuration includes blocklists and LLM overrides.

### How It Works

The dashboard communicates through a shared C++ host bridge and existing Papyrus ModEvents. All JSON is built with `nlohmann::json` for proper escaping. Papyrus reads live quest properties and pushes comprehensive JSON snapshots. Remove Packages and Execute Action retain exact decimal-string actor FormIDs, including high-bit IDs.

Director submission does not close the dashboard. Manual cancellation notifies `<Name>'s task was cancelled.` on success or `<Name> has no task to cancel.` on a repeated/no-task request; SkyrimNet-driven cancellation feedback is unchanged. Delivery-recipient rejection also notifies the player. SkyMessage confirmations are unchanged. Magelight lifecycle, Escape, contention and game dispatch require separate live acceptance; offline checks/builds are not runtime proof.

DeliverMessage targets the resolved recipient actor, including across cells. An unresolved or invalid recipient is rejected through a player notification/log without replacing an existing task or triggering NPC dialogue that could select an unrelated action. Missing delivery, fetch, search and escort targets use target-first wording: `<Target> could not be found.`

Offline host checks (Clang and existing nlohmann_json headers required): `python SKSE/tests/RunDashboardHostCheck.py <vcpkg-triplet-dir>`. Build and stage only the dashboard with `pwsh -NoProfile -File build.ps1 -DashboardOnly -DataDir <GamePlugin-worktree>`, then compare both trees with `pwsh -NoProfile -File SKSE/tests/RunDashboardAssetCheck.ps1 -DataDir <GamePlugin-worktree>`. Dashboard-only mode does not install or publish.

Built-dashboard dropdown regression (Chrome, Python stdlib only): `python SKSE/tests/RunDashboardDropdownCheck.py [chrome.exe] [dashboard-tree]`. Exercises all eight combobox sites for filtering/pick/clear/no-match behavior, and every site plus the faction picker for geometry at scales 1 and 2. Covers down-when-fit, bottom-edge flipping/clamping, same-value field preservation, keyboard/focus/Escape/scroll dismissal and byte-equal Director payloads. The Magelight Escape bridge is modeled; this is offline Chromium evidence, not Magelight/PrismaUI game-host acceptance.
Named checks use `--scenario=<name>`: `director_required_fields` covers unpicked Political types, distinct factions and explicit event selection; `director_generate_merge_and_stop` covers partial valid-field application, generated-narration re-rolls and Stop; `generate_reroll` separately verifies that user-edited narration stays unchanged.

Compiled arrival/history smoke: `python -X utf8 SKSE/tests/RunArrivalDeliveryBytecodeCheck.py <papyrus.exe> <GamePlugin-worktree>/Scripts <native-build>/TaskHistoryDescriptionCheck.exe`. This executes the staged PEX delivery/cleanup and common story-arrival paths through a bounded interpreter with modeled engine/storage boundaries and the real native formatter. It checks no-meeting delivery, case-equivalent boundary outcomes, player-absent return, guarded/forced Travel cancellation, forced arrival reentry, dispatch latch reset, and callback cleanup while an arrival poll is suspended. `RunDeliverMessageRejectionCheck.py` takes the same arguments and checks unresolved-recipient rejection preserves an existing Travel task while distant resolved recipients remain actor targets. Neither check reproduces Skyrim scheduling or model behavior.

Conduct processing smoke (VS developer shell): `python SKSE/tests/RunPlayerConductCheck.py <vcpkg-triplet-dir>`. It executes production conduct/name-lookup methods with real isolated SQLite and modeled game notification/reporter/clock boundaries, covering primary/rival/ally changes and success/error messages.

---

## Architecture

IntelEngine operates as two tightly integrated layers, with a third connecting it to SkyrimNet's AI:

**SKSE Native Plugin (C++)** — high-performance backend that runs outside Papyrus:
- **Dynamic world indexing** — scans all cells and actors on game load, building searchable indexes from your actual load order rather than static lists
- **Location resolution** — unified resolver handling named locations, semantic terms, fuzzy matching, and spatial analysis (door scanning, Z-axis direction, furniture detection)
- **NPC search** — name matching with Levenshtein distance tolerance, partial matching, and suggestion fallback
- **Time parsing** — natural language to game-hour conversion for scheduling
- **Stuck & departure detection** — native position polling at engine speed, independent per task slot
- **Cell analysis** — door enumeration, exterior/interior classification, directional scanning
- **Dashboard bridge** — PrismaUI integration with thread-safe C++/Papyrus event routing and JSON state serialization

**Papyrus Scripts** — game engine integration:
- AI package management (travel, sandbox, and escort packages at three speed tiers)
- Slot-based task state machine with dual persistence (runtime arrays + StorageUtil)
- Schedule monitoring with distance-based early departure calculation
- Save/load recovery with full package and linked-ref reconstruction
- MCM management interface

**SkyrimNet External Bundle** — IntelEngine's 11 AI-selectable actions, three category containers, four custom LLM prompts, and seven character-bio submodules ship under `SKSE/Plugins/SkyrimNet/external/galanx.intelengine/`. Action identities and Papyrus entry points remain unchanged.

**SkyrimNet Plugin Configuration** — IntelEngine's LLM variant, settings schema, generated `settings.yaml`, and user-editable `factions.yaml` remain under `SKSE/Plugins/SkyrimNet/config/plugins/IntelEngine/`. This configuration path is separate from the Beta 25 external content bundle.

---

## Story Engine — The Dungeon Master

The Story Engine is Part 2 of IntelEngine. Every few in-game hours, an LLM Dungeon Master observes the full world state — your location, time of day, NPC memories and relationships — and decides if something should happen. World facts shared by every candidate are shown once, while each NPC's additional knowledge remains attached to them for silent fact-checking. No random dice rolls. Every event is grounded in the actual history of your playthrough.

Nine story types, each individually toggleable via MCM:

- **Seek Player** — NPCs travel to find you because of unfinished business, old friendships, or something they overheard. Forgotten friends don't stay forgotten — NPCs you haven't seen in a while are more likely to come looking.
- **Informant** — NPCs approach with real gossip traced to actual game events. The chain of who told whom is tracked.
- **Road Encounter** — You cross paths with NPCs traveling on their own business. Exterior only. They have their own destination.
- **Ambush** — Hostile NPCs with real grudges stalk and attack you. Stealth or charge variants. Beat them and they yield — talk, kill, or walk away.
- **Stalker** — Romantically obsessed NPCs secretly follow you until caught. No combat — the emotional confrontation is the payoff.
- **Message** — Couriers deliver verbal messages from NPCs who can't come themselves, with optional meeting invitations that schedule the sender.
- **Quest: Combat** — NPCs ask you to clear enemies from a location. Guide option (they jog with you) or go alone with a map marker. Bandits, draugr, or dragons.
- **Quest: Rescue** — NPCs plead for help rescuing someone who's been taken. The victim is pre-placed deep inside the dungeon before you enter — restrained in prisoner furniture when available, or found injured in the boss room. The compass guides you through the dungeon to them.
- **Quest: Find Item** — NPCs send you to retrieve a specific item from a dungeon. A quest chest is placed at the boss room with a guardian enemy. The compass points directly at the chest.
- **NPC Interaction** — Two NPCs interact independently of you. Arguments, deals, training, whispered conspiracies. Happens whether you're watching or not.
- **NPC Gossip** — Rumors spread through chains of up to 10 people. Information travels realistically through the world.

Every event injects facts into NPC bios that persist and influence all future dialogue. Message and quest courier facts/memories are recorded only after arrival passes the travel rejection checks; rejected pending quests do not publish quest facts or repetition history. The Dungeon Master prompt lists only enabled story types and guards long-absence seek-player guidance accordingly. Anti-repetition, type balancing, and per-NPC cooldowns keep stories varied and meaningful.

---

## Planned Features

IntelEngine is under active development. The following features are planned for future releases:

- **Eliminate Target** — dispatch an NPC to kill a specific person. Assassinations, bounty hunting, or settling grudges — carried out autonomously across the world.
- **Lockpick Door** — send an NPC to pick a locked door, gaining access to restricted areas without doing the dirty work yourself.
- **Steal Item** — task an NPC with stealing something from someone's inventory or home. Risk and reward — their skill determines success.
- **Background events in SkyrimNet History — implemented, pending live acceptance:** persistent `intel_social` and `intel_story` schemas categorize executed NPC interactions, gossip exchanges and story/seek/informant outcomes. Matching generic memories are replaced rather than duplicated; travel proposals, rejections and cooldown blocks do not emit these types. SkyrimNet's existing automatic-audience distance/configuration filters still apply, so off-screen registration does not guarantee History retention. Politics remains in IntelEngine's Politics tab: SkyrimNet currently has no History-only sink excluded from NPC prompts/memory.
- **Timeline-rewind notification — backlog, not implemented:** when loading an earlier save removes later political history (`CleanupFutureEvents` > 0), show a top-left `RE::DebugNotification` hint with the count, deferred until the load completes. Separate native feature, not part of the migration release.

---

## Compatibility

> **Warning: Overlapping SkyrimNet actions from other mods will cause conflicts.**
>
> SkyrimNet presents ALL registered actions to its AI when choosing what an NPC should do. If another mod provides its own travel, fetch, or cancel actions, the AI will see **duplicate options** — for example, two different "go to location" actions. It may pick IntelEngine's travel but the other mod's cancel, which won't work because each mod's cancel only affects its own tasks.
>
> **If you use another SkyrimNet mod that provides similar actions** (travel, fetch, deliver, cancel, etc.), you must **disable the overlapping actions** in either IntelEngine or the other mod. Each action YAML has an `enabled: true/false` flag — set conflicting actions to `enabled: false` in whichever mod you want to defer.
>
> **SeverActions:** its travel overlaps IntelEngine's `GoToLocation` (and related travel actions). Keep travel enabled in only one of the two — turn off IntelEngine's travel actions in the dashboard **Actions** tab, or turn off SeverActions' travel — so NPCs never get two competing travel orders.

> **SkyrimNet Beta 25 / IntelEngine 3.5.1 migration:** IntelEngine content now loads from external plugin id `galanx.intelengine`. After upgrading, review its enablement and priority in SkyrimNet. Existing action names are unchanged, so per-action settings retain their keys, but should still be reviewed after the content-layer migration. Do not move or delete `config/plugins/IntelEngine`; it continues to own IntelEngine settings and faction configuration.

> **3.6.0 rendered-cache repair:** rendered facts, gossip and task-history caches are now stored encoded, so saved text can no longer corrupt later co-save values such as stored NPC names. Fresh-new-game repair verified on Skyrim 1.6.1170 and 1.7.104, including warm and independent cold reloads (on-screen names can take a moment to reappear). A bounded offline converter now exists for eligible saves **from IntelEngine 3.5.2 or earlier**: one real 3.5.2 save on each runtime passed bounded converted-copy acceptance. The 1.7.104 check retained **11 independently sampled caches plus two explicit policy-derived fields and 24 qualified live names** through warm and independent cold reloads, including natural UTF-8 and production prompt checks. Four other name holders remain unverified and were omitted from the new checkpoint; their original/converted bytes were preserved. An initial attempt crashed with cause unresolved; successful recovery is not a general stability claim. The converter refuses ambiguous/truncated streams, unknown provenance or sentinel meanings, unsupported identities and unqualified controls; it cannot restore already-lost values. Use the complete repaired DLL, both affected PEX files and all three prompts; never mix components. **Never load a converted save with 3.5.2 or earlier.** See [converter eligibility, procedure, tests and limits](docs/DLL_API.md#offline-legacy-save-conversion).
>
### How to convert an old save

For eligible SE/AE saves from IntelEngine 3.5.2 or earlier, or VR 1.4.15 saves from VR-capable IntelEngine 3.5.1 or earlier. Tested on one real 3.5.2 save per qualified SE/AE runtime and one real 3.5.1 VR fixture; older versions are format-gated, not separately tested. Fix mode does not read the IntelEngine version.

Rendered caches were first stored in IntelEngine 2.0.0; this history does not establish conversion testing on older versions.

**Convert before loading an old save with 3.6.0.** The Game installer includes the converter and launcher for everyone; fresh games do not need conversion. Close Skyrim and keep the matching same-directory `.ess` / `.skse` pair together. Run `LegacyCacheMigration.exe fix --save "<path-to-save.ess>"`, or drag one ESS onto the adjacent `Convert-IE-Save.bat`. Fix mode accepts SKSE co-save format 1 with complete framing, exactly one PapyrusUtil owner and, when present, exactly one STRV v1 record. Tested conversion runtimes: **1.6.1170**, **1.7.104**, or **VR 1.4.15 / SKSEVR 2.0.12**. Skyrim 1.5.97 / SKSE64 2.0.20 conversion: untested. VR also requires a bounded `TESV_SAVEGAME` version-12 ESS header. Eligibility does not guarantee every legacy save qualifies.

One real VR fixture passed bounded converted-copy acceptance: six repaired facts/task-history caches, recovered task progression, a roughly 39-minute sampled soak and one cold reload. It contained no gossip caches or stored names, so those migration cases remain untested on a real VR fixture. Cold comparisons matched all 66 cache comparisons and 76/77 state fields; a small scalar deadline difference and SkyrimNet timeline differences remain unexplained, three actor mappings were absent both before and after, and the original SkyrimNet database baseline and native name index were not established. This is not full VR, historical database restoration or general old-save qualification. Expert VR `convert` still refuses because no historical VR PapyrusUtil provider hash is qualified; `inspect` alone grants no eligibility.

Only rendered-cache values are rewritten. Non-cache fields, including BEL-bearing task destinations and stored names, pass through byte-for-byte without normalization. If STRV is absent and the PapyrusUtil owner contains only recognized record types/versions, `fix` can publish a verified byte-identical cache-free pair; this is a copy, not a repair. Unknown records in that path or a present STRV with no eligible targets refuse.

Player `fix` also re-encodes save-local `0xFF` rendered-cache holders in place when their serialized type is actor (`62`), and orphaned global (object `0`) cache entries; no form resolution or remapping is needed. Both use the same legacy policy: restore BEL to space, then encode. Target values are byte strings: malformed UTF-8 (including historical byte-truncated messages) is preserved without transcoding or replacement. TAB, LF, VT, FF, CR and embedded ESC are encoded; a lone ESC (ambiguous empty sentinel), NUL, DEL and unsupported C0 controls still refuse, as do restored-original mismatches. Native target assignments and reverse-patch byte equality must still verify. Human output lists converted fields, temporary identities (decimal object ID plus hexadecimal form ID), orphaned global keys, and each output path on separate lines. Expert `inspect`/`convert` JSON, strict UTF-8 and qualification rules are unchanged.

Validation focuses on IntelEngine's rendered caches. Other plugins' blocks, including duplicate serialization IDs, and non-cache keys and values pass through byte-for-byte; their content does not disqualify an otherwise eligible save. This does not repair other mods' data. Ambiguous IntelEngine targets, duplicate PapyrusUtil owners or STRV records, and incomplete framing still refuse. The converter verifies that every byte outside the cache values and their enclosing length fields stays unchanged.

For Skyrim **1.6.1170**, the converter accepts **SKSE64 2.2.6, 2.2.7 and 2.2.8**; the other runtime gates are unchanged. The [SKSE release history](https://github.com/ianpatt/skse64/blob/v2.2.8/skse64_whatsnew.txt) and [2.2.8 runtime definition](https://github.com/ianpatt/skse64/blob/v2.2.8/skse64_common/skse_version.h) identify these as builds for the same runtime. Header eligibility is not a separate in-game acceptance test.

Offline fixture check: `python SKSE/tests/RunLegacyCacheMigrationFixtureCheck.py --converter <converter.exe> --fixtures <fixture-folder-or-ESS> --work-dir <new-scratch-folder> [--expected <known-good.skse>]`. Omit `--fixtures` to use `IE_CONVERTER_PRIVATE_FIXTURES`; unavailable private fixtures are skipped. The check converts copies and compares the complete output against an independent cache-only rewrite, optionally against a known-good co-save too. Keep private saves outside this repository.

Converter build candidate **3.6.2** embeds IntelEngine Windows version metadata. Build the standalone tool with MSVC Release (static C++ runtime, no debug information): `cmake -S SKSE/migration -B <build-dir> -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=<existing-vcpkg-prefix>`, then `cmake --build <build-dir> --config Release --target LegacyCacheMigration LegacyCacheMigrationCheck`. Run `<build-dir>/Release/LegacyCacheMigrationCheck.exe <new-scratch-dir>` for the synthetic conversion/publication checks. The closed-game process check remains unchanged; window-only detection can miss a starting or headless game. Build metadata is not a digital signature or an antivirus clearance; this candidate does not replace the published 3.6.0 package.

Linked/cloud-managed components and extended/device paths remain unsupported. Copy **both** `.ess` and `.skse` to a short writable normal physical folder, e.g. `C:\IE-convert`, preserving the originals. Desktop/Documents may themselves be cloud-managed. Ordinary `.\My Save.ess` spelling and case-insensitive `.ess` extensions are accepted without renaming the input. Access failures identify paths and OS errors; runtime refusals list the detected and supported runtime/SKSE tuples. Existing backup/output/staging collisions must be inspected, never overwritten: retry the original pair in a fresh physical folder, and do not load an incomplete output pair.

It preserves the originals in `<stem>_IEbackup/` and creates sibling `<stem>_IEconverted.ess` / `<stem>_IEconverted.skse` without replacing existing files. Exit **0** means both outputs were published; **2** is a usage, eligibility or safety refusal; **3** is an I/O, verification or publication failure. If the second rename fails, the first converted file may remain; do not load an incomplete pair. Install the complete repaired DLL, both affected PEX files and all three prompts before using the converted pair. Never load a converted save with 3.5.2 or earlier.

Do not repeat conversion or mix original and converted companions. `fix` refuses input stems ending in `_IEconverted` (case-insensitive), but renaming bypasses this filename guard. There is no reliable save marker distinguishing legacy values from already-encoded 3.6.x values; percent-looking text is legitimate legacy text too. **Do not convert a save written by 3.6.x or a previously converted save**, even after renaming: it can double-encode values. Writer provenance cannot be proved from the save; BEL-to-space is owner policy. Rollback requires the original pair, matching old components and corresponding IntelEngine/SkyrimNet external-data backups; the converter backs up only the pair.

**If you skip conversion:** fixed templates interpret recognized literal percent escapes as controls (for example, `%0A` becomes a newline); `%250A` becomes literal `%0A` without recursion. This does not make native restoration of raw legacy saves safe. See the [API conversion record](docs/DLL_API.md#player-fix-mode-how-to-convert-an-old-save) for exact decoder results and offline byte comparisons.

In the unconverted VR observation, the Travel task restored and progressed, but three of four original populated facts caches read back empty, the fourth only as a header, and both original rendered histories read back empty despite some raw history remaining. One earlier process crashed; its cause is unproven, and the evidence does not establish that it read the unconverted pair. A restored task is not evidence that skipping conversion is safe.

Both preserved legacy pairs reproduced their verified converted bytes through the unmodified shipped batch and production CLI with Skyrim closed, on ASCII and Unicode/space scratch paths. The production CLI also correctly refused while Skyrim was running. These offline results do not establish general existing-save safety.

> **Papyrus API and array guards:** seven existing native functions are now declared in `IntelEngine.psc`, and three array-null checks use boolean guards. Maintenance, recovery and schedule retention were exercised across save/reload.

> **3.6.0 Story DM actor resolution:** model-selected NPC names now resolve only within the latest Story DM/social candidate pools, using exact display names or a unique case/whitespace-normalized name with a trailing bracketed role removed. Unknown and ambiguous names reject instead of falling back to worldwide fuzzy search. Duplicate display names remain ambiguous. Overlapping requests are not request-isolated; the existing Courier/Messenger substitution path is unchanged. Live selection and unknown-name rejection were verified; duplicate-name rejection remains unverified live.

> **Unreleased — localized place-name prompts (source/offline only):** action and DM/scheduling prompts require exact in-game/context place names in the game's language, never translations; semantic keywords stay English. GoToLocation lists nearby names through SkyrimNet's `get_place_names("nearby")` decorator. This is prompt guidance only, not Unicode/alias resolver support or custom named-Place routing; localized-install acceptance remains pending.

> **Unreleased — same-faction dispatch (source/offline only):** reject identical faction IDs in political event validation, direct war declaration, relation adjustment and database recording/seeding. Hide legacy self-pair relations on read without changing the save format. Conduct reports never apply cross-faction effects to the same faction; one-sided political events remain supported.

> **Unreleased — actor arrival (source/offline only):** actor targets use a 200-unit radial base plus current speed times a 0.50-second response allowance (150ms polling + estimated 350ms VM/package delay), with a 120-unit Z gate. A claimed native threshold survives a speed drop before the Papyrus recheck; disarm/re-arm clears it. Markers retain 300-unit horizontal/200-unit Z bounds. Story sandbox evaluation no longer waits an extra 100ms; player-targeted sandbox remains self-linked, not player-following. Run3 confirmed running overshoot at the old fixed threshold. Physical stopping and the latency allowance still need live verification.

> **Unreleased — absent meeting location (source/offline only):** the shared meeting description omits the location clause for the `none` sentinel, including declined-meeting facts.

> **Unreleased — log cleanup (source/offline only):** optional response fields use a separate native accessor that logs missing keys at debug; required fields and malformed JSON still warn. Dashboard lifecycle and input-contention events log at info once per event. Page-close and hotkey causes are explicit; the host does not supply an Escape-specific exit reason. `DispatchToTarget` already returns Bool on every source/compiled exit.

> **Unreleased — Action Execution actors (source/offline only):** the loaded same-cell actor list includes non-unique guards and mod-added citizens, retaining exact reference FormIDs and existing unavailable-actor checks. Automatic DM/social selection is unchanged.

> **Unreleased — #44 task-history outcomes (source/offline only):** failed/aborted/unknown-result travel and story tasks use attempted wording; successful story journeys and proven escort arrivals record their outcomes. Native success-token comparisons now preserve Papyrus's case-insensitive semantics, including `delivered`/`Delivered`, without modifying message or target text. The run3 delivery path writes the result before player-present cleanup reads the same actor/key and then unsets it; no intervening overwrite exists in that path. The raw run3 result spelling was not logged, so the casing defect is fixed offline without claiming that it proves the live inverse's cause.

> **Unreleased — single story arrival (source/offline only):** a shared entry latch prevents callback/poll re-entry for seek-player and other story types, while retaining ambush/quest monitoring and resetting for new dispatches. Queued or suspended arrival polls stop after cleanup or dispatch replacement; stable actor/target references prevent cleared-global dereferences. The compiled-PEX cleanup interleaving regression fails on the prior artifact and passes on the rebuilt script; live acceptance remains pending.

> **Unreleased — Director actions and faction picker (source/offline/browser only):** Action Execution excludes unsupported `ReportPlayerConduct` and includes parameter-less `CancelCurrentTask`. Clicking/focusing a filled faction field opens the full list; typing filters it, free text remains valid, and the clear button empties it in one click.

> **Unreleased — #41 automatic rejection diagnostics (source/offline only):** automatic candidate selection remains bounded to the DM/social pools, including rejection of duplicate entries for the same actor. Rejection warnings distinguish `empty-pools`, `no-match`, `ambiguous`, `zero-formid`, `lookup-null`, `deleted`, and `id-mismatch`, with both pool sizes. Ambiguity includes all matching pool/name/FormID entries and whether they identify the same actor. Live verification remains pending.

> **Unreleased — #43 leader identity (source/offline only):** political leader lookups reject empty names and use case-insensitive, forward word-boundary matching: `Jarl Ulfric Stormcloak` matches configured `Ulfric Stormcloak`, but `Ulfric` alone and unrelated longer names do not. Loaded-actor selection still prefers the player's cell. Indexed leader/liveness lookups share that contract without fuzzy fallback; matched loaded leaders log their FormIDs at info level. Live bio-fact injection remains unverified.

> **Unreleased — #42 dashboard FormIDs (source/offline only):** Remove Packages and Execute Action carry decimal FormID strings, not ModEvent floats. `IntelEngine.ParseFormID` parses unsigned 32-bit values into Papyrus Int bit patterns, preserving FE/FF IDs; malformed/overflow values and zero are rejected before actor lookup. Dashboard JSON IDs must be positive integers within `uint32_t`. The DLL and rebuilt `IntelEngine.pex`/`IntelEngine_Core.pex` must be staged together; live dispatch remains unverified.

> **Unreleased — #41 manual Director identity (source/offline only):** Story and NPC Social Director selections use a separate case-insensitive exact full-display-name lookup across indexed actor references, including persistent/interior references without loaded 3D and current process-list actors. Unavailable, mismatched, deleted, dead, and disabled references are filtered before ambiguity: repeated entries for one eligible reference are allowed; distinct eligible references reject with both FormIDs logged. If none are eligible, the most specific rejection state is logged. There is no fuzzy, whitespace, or role-suffix fallback. Automatic DM/social candidate selection is unchanged. The existing story dispatcher has an off-screen arrival path, but unloaded-NPC dispatch has not been verified live.

> **3.6.0 NPC Social DM threading repair:** the async tick now queues game-state snapshotting through SKSE's main-thread task interface, including callback-quest inspection. Results and candidate pools from before a save load/revert are discarded. SE/AE soaks passed; stale-result drop and Phase B thread identity remain unverified. The 1.5.97 crash report on the original IntelEngine repo is not confirmed fixed. The faction-name expression and legacy synchronous APIs are unchanged.

### Historical 3.5.2 compatibility results

The **v3.5.2 compatibility release** uses pinned MinLL/CommonLibVR with the audited non-VR header correction. A user run established Skyrim **1.6.1170** startup, save loading, dashboard use, and action dispatch; Epicrob also confirmed success on his affected **1.7.x** setup. Use matching SKSE and Address Library. VR was disabled in 3.5.2, and these observations do not establish every supported runtime or task scenario. The historical `v3.5.2-rc1` DLL has a demonstrated ABI defect and must not be used. See [native build and verification evidence](docs/DLL_API.md#verification-evidence).

The native MSVC Release profile disables optimization with `/Od /Ob0`, retains C++23 through `/std:c++23preview`, and removes `/d2ReducedOptimizeHugeFunctions`. The full package includes the rebuilt DLL, all 11 rebuilt Papyrus scripts, and the rebuilt dashboard alongside the existing ESP and configuration assets. **3.5.2 is the release/package version; the byte-identical tested DLL retains native version 3.5.1.** The compatibility fix does not claim to resolve the 1.5.97 crash report on the original IntelEngine repo or the deferred Papyrus/translation bugs.

---

## Requirements

- The 3.6.0 native build includes Skyrim SE, AE and VR. Tested: Skyrim **1.6.1170.0** and **1.7.104.0**; Skyrim VR **1.4.15** should work and was partly tested. Skyrim SE **1.5.x**: untested. Other listed targets are untested.
- [SKSE](https://skse.silverlock.org/)
- [SkyrimNet Beta 25 or newer](https://github.com/MinLL/SkyrimNet-GamePlugin) (declared minimum; 3.6.0 tested with official Beta 28)
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) (MCM)
- [PapyrusUtil](https://www.nexusmods.com/skyrimspecialedition/mods/13048) (persistent storage)
- [powerofthree's Papyrus Extender](https://www.nexusmods.com/skyrimspecialedition/mods/22854) (package management, linked refs)
- Papyrus MessageBox (confirmation prompts): [Papyrus MessageBox - SKSE NG](https://www.nexusmods.com/skyrimspecialedition/mods/83578) for Skyrim 1.6 and earlier, or [PapyrusMessageBox-1.7.104](https://www.nexusmods.com/skyrimspecialedition/mods/193932) for Skyrim 1.7

### Optional

- [Magelight UI](https://github.com/Severause/MagelightUI) **1.7.1+** — optional, recommended dashboard host. Install separately.
- [PrismaUI](https://www.prismaui.dev/) — optional frozen dashboard fallback; no new features or fixes on this path. Neither host is required for IE gameplay.

---

*IntelEngine turns Skyrim's people from scenery into agents — NPCs who keep appointments, carry your words, fetch who you need, and remember what happened. And with the Dungeon Master, they also act on their own — seeking you out, settling grudges, spreading gossip, sending you on rescue missions deep into dungeons, and living their own lives whether you're watching or not. The world doesn't wait for you anymore.*
