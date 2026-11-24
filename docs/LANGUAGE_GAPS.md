# Language gaps

What IntelEngine 4.0 shows the player in English and no language pack can change yet: file, line, the English. Line
numbers are those of the working tree of 2026-11-23 (branch `feat/4.0-living-world`); they move as the files are edited.

**Routed** (not listed here): every sentence that goes through `IntelEngine.L` (the native `LabelText`), which is
`Core.NotifyPlayer`, the body, kind and buttons of `Core.ShowChoice`, the battle notices `Debug.Notification(IntelEngine.L(...))`,
`IntelEngine.BattleSay` (names of champions, turncoats, captains, strangers) and `LanguageBridge::Label` in the DLL
(journal titles and tale objectives in `ArcBridge::ApplyTitles` and `QuestBridge::ApplyTitles`, the battle call's
objective in `BattleManager::SetCallText`, the trail's way in `ArcBridge::Text`). Their sentences are rows of the packs
(`IntelEngine-GamePlugin/SKSE/Plugins/IntelEngine/languages/*.tsv`): on 2026-11-23 every pack has the same 301 rows,
the 160 of `_labels_template.tsv` (all but `{1} will {2} {3}`, left out on purpose, its particular forms are rows) and
the sentences the scripts build in pieces or the DLL hands back (scheduling notices, quest and story offers, battle
results, ranks, the trail). A pack is only read for labels when it is the language the player reads
(`language.text`): Albanian, which the game does not ship, needs that setting.

**Not routed**, below (567 rows): notices that bypass `IntelEngine.L`, names the code builds in English, English words that sit
inside routed sentences, the text of the MCM, of the journal records in `IntelEngine.esp`, of SkyrimNet's settings
page, and of the dashboard (its own list: `docs/DASHBOARD_STRINGS.md`). Text a model writes is not listed: the tale
writer is told the reading language (`TaleAuthor.cpp`, `text_language`, `intel_tale_author.prompt`); no other prompt
is (the story writer's quest message and item description, the political writer's event label: rows in section 4).

## 1. Notices and message boxes that bypass the packs (scripts)

| where | English | note |
|---|---|---|
| IntelEngine_Core.psc:3520 | Intel: {1} | shown only while "Debug messages" is on |

## 2. Names the scripts put on people and things

The name a person or a thing carries in the game (`SetDisplayName`). The battle's names go through `BattleSay` and are rows; these do not.

| where | the name | where it comes from |
|---|---|---|
| IntelEngine_Arcs.psc:655 | `IntelEngine.ArcText(id, "prop" + p)` | a story's thing: the writer's name for a tale (told the reading language); for Thief and Murder the DLL's English "{1}'s {2}" / "Bloodied {1}" (ArcBridge.cpp) |
| IntelEngine_Arcs.psc:671 | `IntelEngine.ArcText(id, "prop" + p)` | a story's thing: the writer's name for a tale (told the reading language); for Thief and Murder the DLL's English "{1}'s {2}" / "Bloodied {1}" (ArcBridge.cpp) |
| IntelEngine_Arcs.psc:1889 | `IntelEngine.ArcText(id, "prop" + p)` | a story's thing: the writer's name for a tale (told the reading language); for Thief and Murder the DLL's English "{1}'s {2}" / "Bloodied {1}" (ArcBridge.cpp) |
| IntelEngine_Arcs.psc:1978 | `IntelEngine.ArcText(id, "prop" + p)` | a story's thing: the writer's name for a tale (told the reading language); for Thief and Murder the DLL's English "{1}'s {2}" / "Bloodied {1}" (ArcBridge.cpp) |
| IntelEngine_Quests.psc:1213 | `IntelEngine.QuestText(id, "stamp")` | a quest's thing: the DLL's English "{1}'s {2}" (QuestBridge.cpp) |
| IntelEngine_Quests.psc:1314 | `IntelEngine.QuestSetFoe(id, foe)` | a bounty's brigand: Quests.h FoeName, English by-names ("Half-Hand", "the Crow") |
| IntelEngine_Quests.psc:1333 | `IntelEngine.QuestText(id, "stamp")` | a quest's thing: the DLL's English "{1}'s {2}" (QuestBridge.cpp) |

Not counted, because the tale writer gives them in the reading language: IntelEngine_Arcs.psc:740 (`IntelEngine.ArcText(id, "bossname" + s)`); IntelEngine_Arcs.psc:877 (`IntelEngine.ArcText(id, "thugname")`); IntelEngine_Arcs.psc:2097 (`IntelEngine.ArcText(id, "cluename" + k)`); IntelEngine_Arcs.psc:2159 (`IntelEngine.ArcText(id, "spiritname")`).

## 3. Popup titles

`ShowChoice` translates the body, the kind and the buttons, never the title; mostly a person's name, which is right.

| where | the title | note |
|---|---|---|
| IntelEngine_Battle.psc:1592 | `IntelEngine.PlayerRankTitle(PlayerBattleSide) / captain.GetDisplayName()` | the window's title: the rank title (Ranks.h, English) when no captain is named |

## 4. English inside routed sentences

The sentence is a row; what fills its hole is English all the same.

| where | sentence | the English in it |
|---|---|---|
| IntelEngine_Core.psc:4266 | {1} is already busy ({2}). Cancel it first. | the task or story kind is FriendlyKey's English ("Seek Player", "Fetch Npc"; FriendlyKey.h) |
| IntelEngine_Core.psc:4279 | Story engine is busy with {1} ({2}). Cancel it first. | the task or story kind is FriendlyKey's English ("Seek Player", "Fetch Npc"; FriendlyKey.h) |
| IntelEngine_Core.psc:4550 | {1} sets out ({2}). | the task or story kind is FriendlyKey's English ("Seek Player", "Fetch Npc"; FriendlyKey.h) |
| IntelEngine_Core.psc:4658 | {1} and {2}: {3} started. | the task or story kind is FriendlyKey's English ("Seek Player", "Fetch Npc"; FriendlyKey.h) |
| IntelEngine_Core.psc:4684 | Political event refused: {1} between {2} and {3}. ... | the task or story kind is FriendlyKey's English ("Seek Player", "Fetch Npc"; FriendlyKey.h) |
| IntelEngine_Core.psc:4680 | Political event: {1} between {2} and {3}. | {1}: the political writer's event_label (its prompt is not told the reading language) or FriendlyKey; {2} {3}: faction names |
| IntelEngine_Core.psc:4815 | {1}: {2} started. / {1}: {2} failed. | {2}: the action's own name ("GoToLocation", "ScheduleMeeting"), never meant to be read |
| IntelEngine_NPCTasks.psc:2243 | {1}: {2} refused - {3} | {3}: the reason as the code or the model gave it, English |
| IntelEngine_StoryEngine.psc:4984 | Quest: {1} [{2}] | {1}: the story writer's message (its prompt is not told the reading language) |
| IntelEngine_StoryEngine.psc:4928 | {1} tells you about {2} near {3}. | {2}: the story writer's item description, not told the reading language |
| FactionPolitics.cpp (GetFactionDisplayName) | {faction} | faction names in every battle and politics sentence come from factions.yaml and the faction editor, in English ("Stormcloaks", "Imperial Legion"); the packs leave them as they are |
| Ranks.h:112 | {rank} | rank titles: translated inside the promotion and "waits for your word" rows only; elsewhere (the popup title, the dashboard) English |

## 5. Text the MCM pages show that is built outside the MCM

| where | English | note |
|---|---|---|
| IntelEngine_Core.psc:3530 | Invalid | GetSlotStatus (MCM) |
| IntelEngine_Core.psc:3534 | Empty | GetSlotStatus (MCM) |
| IntelEngine_Core.psc:3557 | {1}: {2} | GetSlotStatus (MCM) |
| IntelEngine_Core.psc:3561 | Unknown | GetSlotStatus (MCM) |
| IntelEngine_Core.psc:3549 | [IN COMBAT] | GetSlotStatus (MCM) |
| IntelEngine_Schedule.psc:383 | Empty | GetScheduleDisplay (MCM) |
| IntelEngine_Schedule.psc:390 | {1} @ {2} | GetScheduleDisplay (MCM) |
| IntelEngine_Schedule.psc:392 | {1}: fetch {2} | GetScheduleDisplay (MCM) |
| IntelEngine_Schedule.psc:394 | {1}: msg to {2} | GetScheduleDisplay (MCM) |
| IntelEngine_Schedule.psc:396 | {1}: {2} | GetScheduleDisplay (MCM) |
| IntelEngine_Schedule.psc:411 | overdue | GetScheduleStatus (MCM) |
| IntelEngine_Schedule.psc:413 | very soon | GetScheduleStatus (MCM) |
| IntelEngine_Schedule.psc:415 | in ~{1}h | GetScheduleStatus (MCM) |
| IntelEngine_Schedule.psc:419 | en route, overdue | GetScheduleStatus (MCM) |
| IntelEngine_Schedule.psc:421 | en route, soon | GetScheduleStatus (MCM) |
| IntelEngine_Schedule.psc:423 | en route, ~{1}h | GetScheduleStatus (MCM) |
| IntelEngine_Schedule.psc:426 | meeting | GetScheduleStatus (MCM) |

## 6. The MCM (`IntelEngine_MCM.psc`)

The MCM writes its labels, headings, values and help texts as plain English strings; it uses no `$` keys, so
SkyUI's translation files cannot reach it. `Interface/Translations/IntelEngine_ENGLISH.txt` (in both repos, UTF-16 LE)
holds eight `$IntelEngine_...` keys from 3.x that no script uses any more. Nothing was converted.

What a conversion would need:

- every string below that the player reads replaced by a `$IntelEngine_<Key>` token, and the same key in
  `Interface/Translations/IntelEngine_<LANGUAGE>.txt` for ENGLISH and each language (UTF-16 LE with BOM, `$KEY<TAB>text`
  per line, CRLF), in both repos;
- labels built from pieces (`"Slot " + slot + ":"`, the busy slot's text, the schedule rows, slider formats such as
  `"{0}"`) written as SkyUI's nested tokens (`"$IntelEngine_Slot{" + slot + "}"`, translation `Slot {}:`) or left as
  data;
- the values the scripts compare: `ConfirmModeLabel`, `LivingWorldLabel` and the menu choices are compared and stored
  as their English words (`settings.yaml` stores a choice as its word): the stored value and the shown label must be
  split first;
- `RunSettingNamesCheck.py` reads the MCM's labels from the script: it would have to read them from
  `IntelEngine_ENGLISH.txt`, and keep the rule that a label is the name on SkyrimNet's page (section 8) and on the
  dashboard;
- the string-pool trap ([intelengine-setting-names]): a `$` key spelled like any other string of the mod's scripts
  takes that string's capitals; keys must be unique words;
- the column width: a translated label and its value must still fit about 38 letters together;
- the text the MCM gets from section 5 (slot and schedule texts) routed too, through `IntelEngine.L` and pack rows.

| where | English | call |
|---|---|---|
| IntelEngine_MCM.psc:203 | Settings |  |
| IntelEngine_MCM.psc:242 | Scheduled Meetings | a value compared (and shown) |
| IntelEngine_MCM.psc:270 | Active Tasks | AddHeaderOption |
| IntelEngine_MCM.psc:275 | Clear | AddTextOption |
| IntelEngine_MCM.psc:290 | Actions | AddHeaderOption |
| IntelEngine_MCM.psc:291 | Reset All Tasks | AddTextOption |
| IntelEngine_MCM.psc:291 | Click | AddTextOption |
| IntelEngine_MCM.psc:299 | Empty | a value compared (and shown) |
| IntelEngine_MCM.psc:299 | Invalid | a value compared (and shown) |
| IntelEngine_MCM.psc:299 | Unknown | a value compared (and shown) |
| IntelEngine_MCM.psc:299 | Error: No Core | a value compared (and shown) |
| IntelEngine_MCM.psc:300 | Slot {1}: | AddTextOption |
| IntelEngine_MCM.psc:356 | Scheduled Tasks | AddHeaderOption |
| IntelEngine_MCM.psc:357 | Error: | AddTextOption |
| IntelEngine_MCM.psc:357 | Schedule not found | AddTextOption |
| IntelEngine_MCM.psc:379 | Cancel | AddTextOption |
| IntelEngine_MCM.psc:387 | No scheduled meetings | AddTextOption |
| IntelEngine_MCM.psc:397 | Cancel All Scheduled | AddTextOption |
| IntelEngine_MCM.psc:408 | General Settings | AddHeaderOption |
| IntelEngine_MCM.psc:409 | Mod by | AddTextOption |
| IntelEngine_MCM.psc:409 | Galanx | AddTextOption |
| IntelEngine_MCM.psc:412 | Debug messages | AddToggleOption |
| IntelEngine_MCM.psc:415 | NPCs on a task at once | AddSliderOption |
| IntelEngine_MCM.psc:418 | Wait at a destination (game hours) | AddSliderOption |
| IntelEngine_MCM.psc:421 | Ask before starting | AddHeaderOption |
| IntelEngine_MCM.psc:424 | Go somewhere | AddTextOption |
| IntelEngine_MCM.psc:425 | Fetch someone | AddTextOption |
| IntelEngine_MCM.psc:426 | Deliver a message | AddTextOption |
| IntelEngine_MCM.psc:427 | Escort someone | AddTextOption |
| IntelEngine_MCM.psc:428 | Search for someone | AddTextOption |
| IntelEngine_MCM.psc:429 | Arrange a meeting | AddTextOption |
| IntelEngine_MCM.psc:430 | Fetch someone later | AddTextOption |
| IntelEngine_MCM.psc:431 | Deliver a message later | AddTextOption |
| IntelEngine_MCM.psc:435 | Report back after a delivery | AddToggleOption |
| IntelEngine_MCM.psc:438 | Wait for you at a meeting (game hours) | AddSliderOption |
| IntelEngine_MCM.psc:444 | On time until (game hours late) | AddSliderOption |
| IntelEngine_MCM.psc:450 | Visit ends when you walk away (units) | AddSliderOption |
| IntelEngine_MCM.psc:455 | Stories | AddHeaderOption |
| IntelEngine_MCM.psc:458 | Stories | AddToggleOption |
| IntelEngine_MCM.psc:459 | Force Restart | AddTextOption |
| IntelEngine_MCM.psc:459 | GO | AddTextOption |
| IntelEngine_MCM.psc:462 | Check interval (game hours) | AddSliderOption |
| IntelEngine_MCM.psc:471 | Living world | AddHeaderOption |
| IntelEngine_MCM.psc:473 | How alive the world is | AddTextOption |
| IntelEngine_MCM.psc:474 | Interruptions at bad moments | AddTextOption |
| IntelEngine_MCM.psc:475 | Quest NPCs may move | AddTextOption |
| IntelEngine_MCM.psc:476 | How often something happens | AddTextOption |
| IntelEngine_MCM.psc:477 | Skipped time counts toward the pace | AddToggleOption |
| IntelEngine_MCM.psc:478 | A follower told to wait waits (days) | AddSliderOption |
| IntelEngine_MCM.psc:479 | A follower left behind moves on (days) | AddSliderOption |
| IntelEngine_MCM.psc:483 | NPCs sent into dangerous places | AddHeaderOption |
| IntelEngine_MCM.psc:489 | Allow all NPCs | AddToggleOption |
| IntelEngine_MCM.psc:490 | Block civilians only | AddToggleOption |
| IntelEngine_MCM.psc:491 | Allow followers only | AddToggleOption |
| IntelEngine_MCM.psc:492 | Block all NPCs | AddToggleOption |
| IntelEngine_MCM.psc:495 | NPCs sent into your home | AddHeaderOption |
| IntelEngine_MCM.psc:511 | What followers can be asked to do | AddHeaderOption |
| IntelEngine_MCM.psc:513 | Go somewhere | AddToggleOption |
| IntelEngine_MCM.psc:514 | Fetch someone | AddToggleOption |
| IntelEngine_MCM.psc:515 | Deliver a message | AddToggleOption |
| IntelEngine_MCM.psc:516 | Escort someone | AddToggleOption |
| IntelEngine_MCM.psc:517 | Search for someone | AddToggleOption |
| IntelEngine_MCM.psc:518 | Arrange a meeting | AddToggleOption |
| IntelEngine_MCM.psc:519 | Fetch someone later | AddToggleOption |
| IntelEngine_MCM.psc:520 | Deliver a message later | AddToggleOption |
| IntelEngine_MCM.psc:524 | Auto Dynamic Bio Updates | AddHeaderOption |
| IntelEngine_MCM.psc:526 | Refresh an NPC's profile after much talk | AddToggleOption |
| IntelEngine_MCM.psc:527 | Lines before a profile refresh | AddSliderOption |
| IntelEngine_MCM.psc:531 | Story types | AddHeaderOption |
| IntelEngine_MCM.psc:551 | Someone comes to see you | AddToggleOption |
| IntelEngine_MCM.psc:552 | Someone brings a rumor | AddToggleOption |
| IntelEngine_MCM.psc:553 | A meeting on the road | AddToggleOption |
| IntelEngine_MCM.psc:554 | An ambush | AddToggleOption |
| IntelEngine_MCM.psc:555 | A faction's patrol hunts you | AddToggleOption |
| IntelEngine_MCM.psc:556 | Someone follows you unseen | AddToggleOption |
| IntelEngine_MCM.psc:557 | A message is delivered | AddToggleOption |
| IntelEngine_MCM.psc:558 | Someone has work for you | AddToggleOption |
| IntelEngine_MCM.psc:561 | NPCs among themselves | AddHeaderOption |
| IntelEngine_MCM.psc:569 | NPCs visit and talk to each other | AddToggleOption |
| IntelEngine_MCM.psc:577 | Kind: one NPC goes to see another | AddToggleOption |
| IntelEngine_MCM.psc:578 | Kind: gossip passed on | AddToggleOption |
| IntelEngine_MCM.psc:586 | Rest per NPC (game hours) | AddSliderOption |
| IntelEngine_MCM.psc:589 | Story Tuning | AddHeaderOption |
| IntelEngine_MCM.psc:597 | Long absence (days) | AddSliderOption |
| IntelEngine_MCM.psc:598 | Longest journey (days) | AddSliderOption |
| IntelEngine_MCM.psc:604 | Move a stuck traveller along | AddToggleOption |
| IntelEngine_MCM.psc:610 | Quest expires after (days) | AddSliderOption |
| IntelEngine_MCM.psc:613 | Quest kinds | AddHeaderOption |
| IntelEngine_MCM.psc:630 | Clear out enemies | AddToggleOption |
| IntelEngine_MCM.psc:631 | Rescue someone | AddToggleOption |
| IntelEngine_MCM.psc:632 | Find an item | AddToggleOption |
| IntelEngine_MCM.psc:633 | Fight a faction's enemies | AddToggleOption |
| IntelEngine_MCM.psc:634 | Rescue for a faction | AddToggleOption |
| IntelEngine_MCM.psc:635 | Join a faction's battle | AddToggleOption |
| IntelEngine_MCM.psc:644 | Faction politics | AddHeaderOption |
| IntelEngine_MCM.psc:647 | Faction politics | AddToggleOption |
| IntelEngine_MCM.psc:651 | Political Engine integration (experimental) | AddToggleOption |
| IntelEngine_MCM.psc:652 | PE reactions move relations | AddToggleOption |
| IntelEngine_MCM.psc:664 | Hold Restrictions | AddHeaderOption |
| IntelEngine_MCM.psc:665 | How far each kind of story may | AddTextOption |
| IntelEngine_MCM.psc:666 | reach for its NPC. | AddTextOption |
| IntelEngine_MCM.psc:667 | Defaults differ: see the help text | AddTextOption |
| IntelEngine_MCM.psc:688 | Someone comes to see you | AddHeaderOption |
| IntelEngine_MCM.psc:689 | No restriction | AddToggleOption |
| IntelEngine_MCM.psc:690 | Same hold (civilians) | AddToggleOption |
| IntelEngine_MCM.psc:691 | Same hold (except followers) | AddToggleOption |
| IntelEngine_MCM.psc:692 | Same hold (everyone) | AddToggleOption |
| IntelEngine_MCM.psc:693 | Same town (civilians) | AddToggleOption |
| IntelEngine_MCM.psc:694 | Same town (except followers) | AddToggleOption |
| IntelEngine_MCM.psc:695 | Same town (everyone) | AddToggleOption |
| IntelEngine_MCM.psc:696 | Someone brings a rumor | AddHeaderOption |
| IntelEngine_MCM.psc:704 | A meeting on the road | AddHeaderOption |
| IntelEngine_MCM.psc:712 | An ambush | AddHeaderOption |
| IntelEngine_MCM.psc:723 | Someone follows you unseen | AddHeaderOption |
| IntelEngine_MCM.psc:731 | A message is delivered | AddHeaderOption |
| IntelEngine_MCM.psc:739 | Someone has work for you | AddHeaderOption |
| IntelEngine_MCM.psc:753 | Classic | a value shown |
| IntelEngine_MCM.psc:755 | Rumors | a value shown |
| IntelEngine_MCM.psc:757 | Lives | a value shown |
| IntelEngine_MCM.psc:760 | Never | a value shown |
| IntelEngine_MCM.psc:762 | If urgent | a value shown |
| IntelEngine_MCM.psc:764 | Realistic | a value shown |
| IntelEngine_MCM.psc:769 | If important | a value shown |
| IntelEngine_MCM.psc:771 | Freely | a value shown |
| IntelEngine_MCM.psc:774 | Calm | a value shown |
| IntelEngine_MCM.psc:776 | Normal | a value shown |
| IntelEngine_MCM.psc:778 | Lively | a value shown |
| IntelEngine_MCM.psc:809 | Disabled |  |
| IntelEngine_MCM.psc:811 | Followers only |  |
| IntelEngine_MCM.psc:813 | Everyone |  |
| IntelEngine_MCM.psc:931 | Reset all active tasks? NPCs will return to their normal routines. | ShowMessage |
| IntelEngine_MCM.psc:931 | Yes | ShowMessage |
| IntelEngine_MCM.psc:931 | No | ShowMessage |
| IntelEngine_MCM.psc:938 | All tasks have been reset. | ShowMessage |
| IntelEngine_MCM.psc:994 | Cancel all scheduled meetings? | ShowMessage |
| IntelEngine_MCM.psc:1101 | Story engine restarted. All monitoring loops re-initialized. | ShowMessage |
| IntelEngine_MCM.psc:1406 | Click to cycle. Classic: as before 4.0. Rumors: the world moves on while you are away and you hear about it, but followers you left wait where you left them. Lives: followers you left behind live lives of their own (work at an inn, an adventure, a trip) and you can come upon them later; people close to you miss you while you are away. | SetInfoText |
| IntelEngine_MCM.psc:1408 | Click to cycle. Whether someone may turn up during a fight, a conversation, a scene or a menu: never, only for something urgent, or now and then, as in real life. | SetInfoText |
| IntelEngine_MCM.psc:1410 | Click to cycle. Whether NPCs who take part in a running quest may travel or move for a story. The target of an objective you have open never moves. | SetInfoText |
| IntelEngine_MCM.psc:1412 | Click to cycle. About one notable moment every two game days, every game day, or twice a game day. Moments wait for a good time: arriving in a town or an inn, coming home, leaving a dungeon, waking up. | SetInfoText |
| IntelEngine_MCM.psc:1414 | Off: waiting, sleeping and fast travel do not bring the next moment sooner. It also takes time actually played: eight minutes between two moments, four minutes in a town before a story of its own begins there, twenty minutes between two such stories. On: the game's clock alone counts: skip a day and a day's worth of moments is due. | SetInfoText |
| IntelEngine_MCM.psc:1416 | How long after you told a follower to wait they start a life of their own, once their follower mod lets them go. While it still holds them, they keep waiting. | SetInfoText |
| IntelEngine_MCM.psc:1418 | How long a follower you dismissed stays where you left them before starting a life of their own. | SetInfoText |
| IntelEngine_MCM.psc:1420 | Shows what IntelEngine does as notices on screen. The Papyrus log has it either way. For more in the DLL's own log (IntelEngine.log), set DebugLevel=4 in IntelEngine.ini. | SetInfoText |
| IntelEngine_MCM.psc:1422 | Maximum number of NPCs that can be on tasks at once. | SetInfoText |
| IntelEngine_MCM.psc:1424 | How many game hours NPCs wait at travel destinations before returning home. Does not affect scheduled meetings (those use "Wait for you at a meeting"). | SetInfoText |
| IntelEngine_MCM.psc:1426 | Force reset all active tasks. NPCs will return to their normal routines. | SetInfoText |
| IntelEngine_MCM.psc:1431 | Click to cycle: Disabled / Followers only / Everyone. Controls whether a confirmation prompt appears before this action. Followers only = only prompts when the acting NPC is your active follower. | SetInfoText |
| IntelEngine_MCM.psc:1436 | On (default): your active followers can be given this action. Off: it is silently refused for them, before any confirmation prompt. | SetInfoText |
| IntelEngine_MCM.psc:1438 | When enabled, messengers return to you after delivering a message off-screen and report back. | SetInfoText |
| IntelEngine_MCM.psc:1440 | How long an NPC waits at the meeting spot after the scheduled time before giving up. If you're later than this, the meeting is cancelled. | SetInfoText |
| IntelEngine_MCM.psc:1442 | Arriving within this time after the agreed hour still counts as on time. Default 0.5 (30 minutes). Set it higher with a mod that changes the timescale as you play. | SetInfoText |
| IntelEngine_MCM.psc:1444 | How far (game units) you must walk away before an NPC stops lingering and returns to normal. Affects all systems: travel arrivals, task completions, story encounters. Default 800. | SetInfoText |
| IntelEngine_MCM.psc:1446 | Cancel all scheduled meetings and tasks. | SetInfoText |
| IntelEngine_MCM.psc:1448 | The Story Engine (the dungeon master). NPCs with unfinished business will autonomously seek you out based on your shared history. | SetInfoText |
| IntelEngine_MCM.psc:1450 | At Classic: how often the dungeon master looks for somebody with a reason to come to you. Above Classic it is not used: the pace is "How often something happens", and the dungeon master looks when you arrive somewhere, when you wake, and every three game hours besides. | SetInfoText |
| IntelEngine_MCM.psc:1452 | How long (game hours) a picked NPC's priority stays reduced. Higher = more variety in NPC picks, lower = favorites return sooner. | SetInfoText |
| IntelEngine_MCM.psc:1454 | Minimum game days since your last interaction with an NPC before the Story Engine considers them as a candidate. An NPC who has just come to you is left alone for at least as long. | SetInfoText |
| IntelEngine_MCM.psc:1456 | Maximum game days an NPC will travel before being teleported to the target. Lower = faster delivery, higher = more realistic. | SetInfoText |
| IntelEngine_MCM.psc:1458 | When enabled, NPCs stuck during travel or on the way for longer than "Longest journey" are teleported to the target. When disabled, the NPC gives up instead. | SetInfoText |
| IntelEngine_MCM.psc:1460 | No danger zone filtering. All NPC types can visit you in dungeons and caves. Informants always turn back. | SetInfoText |
| IntelEngine_MCM.psc:1462 | Farmers, merchants and other civilians are not sent out into the wilds after you; warriors and mages may be. Above Classic nobody is sent into a dungeon after you whatever this says (they wait until you are out), and nobody is ever sent to another plane. | SetInfoText |
| IntelEngine_MCM.psc:1464 | Only people who can be followers are sent out into the wilds after you. Above Classic nobody is sent into a dungeon after you whatever this says (they wait until you are out), and nobody is ever sent to another plane. | SetInfoText |
| IntelEngine_MCM.psc:1466 | Nobody is sent to you anywhere but in a settlement. | SetInfoText |
| IntelEngine_MCM.psc:1468 | No home visit filtering. All NPC types can visit you when you're at home. | SetInfoText |
| IntelEngine_MCM.psc:1470 | Farmers, merchants and other civilians won't bother you at home. Warriors and mages can still visit. | SetInfoText |
| IntelEngine_MCM.psc:1472 | Only NPCs who can be recruited as followers (PotentialFollowerFaction) can visit you at home. | SetInfoText |
| IntelEngine_MCM.psc:1474 | Nobody visits you when you're at home. | SetInfoText |
| IntelEngine_MCM.psc:1476 | How many in-game days before an unfinished dynamic quest auto-expires. The quest giver remembers you never showed up. Default 1. | SetInfoText |
| IntelEngine_MCM.psc:1478 | NPC travels to find you based on shared history or unfinished business. The Director and the log call this type "Seek Player". | SetInfoText |
| IntelEngine_MCM.psc:1480 | NPC approaches you to relay gossip about a third party. The Director and the log call this type "Informant". | SetInfoText |
| IntelEngine_MCM.psc:1482 | NPC appears on the road nearby, coincidentally traveling. Exterior only. The Director and the log call this type "Road Encounter". | SetInfoText |
| IntelEngine_MCM.psc:1484 | Someone with a grudge lies in wait on the road. Off: no ambush inside a town's story either. The Director and the log call this type "Ambush". | SetInfoText |
| IntelEngine_MCM.psc:1486 | A faction that holds you for an enemy (standing -40 or worse) sends men after you on the road, now and then. Never while you are with somebody, resting or settled somewhere. The Director and the log call this type "Faction Ambush". | SetInfoText |
| IntelEngine_MCM.psc:1488 | Someone shadows you from a distance, out of doors. Off: nobody does inside a town's story either. The Director and the log call this type "Stalker". | SetInfoText |
| IntelEngine_MCM.psc:1490 | NPC delivers a verbal message, optionally with a meeting invitation. The Director and the log call this type "Message". | SetInfoText |
| IntelEngine_MCM.psc:1492 | People who need your help. Off: nobody brings you their trouble, and no story of the town begins. The Director and the log call this type "Quest". | SetInfoText |
| IntelEngine_MCM.psc:1494 | Two NPCs interact: argue, trade, train, bond. Visible or off-screen. The Director and the log call this type "NPC Interaction". | SetInfoText |
| IntelEngine_MCM.psc:1496 | One NPC shares a rumor about a third party with another NPC. The Director and the log call this type "NPC Gossip". | SetInfoText |
| IntelEngine_MCM.psc:1498 | Force restart all monitoring loops. Use if story ticks seem frozen or NPCs stop acting. | SetInfoText |
| IntelEngine_MCM.psc:1500 | Enable autonomous NPC-to-NPC interactions. NPCs gossip, argue, trade, and socialize independently. | SetInfoText |
| IntelEngine_MCM.psc:1502 | How often (game hours) the NPC social system checks for NPC-to-NPC interactions. | SetInfoText |
| IntelEngine_MCM.psc:1504 | How long (game hours) before an NPC can be picked for another social interaction. Separate from the stories' "Long absence". | SetInfoText |
| IntelEngine_MCM.psc:1506 | A bounty on what lives at a place. Off: no bounty, and no master of a place to kill inside a town's story. | SetInfoText |
| IntelEngine_MCM.psc:1508 | Someone of the town held at a place out in the world. Off: nobody is held in a story. | SetInfoText |
| IntelEngine_MCM.psc:1510 | A keepsake lost at a place. Off: no keepsake hunt. | SetInfoText |
| IntelEngine_MCM.psc:1512 | Enable faction combat quests. Clear out hostile faction soldiers at a location. Rewards faction standing with the opposing side. | SetInfoText |
| IntelEngine_MCM.psc:1514 | Enable faction rescue quests. Rescue a captive from hostile faction soldiers. Rewards faction standing with the opposing side. | SetInfoText |
| IntelEngine_MCM.psc:1516 | Enable faction battle quests. A friendly faction invites you to join an upcoming battle. Requires high standing (40+). | SetInfoText |
| IntelEngine_MCM.psc:1518 | WARNING: If enabled, rescued NPCs can die during combat. This can break main quests if essential NPCs are killed! Keep disabled unless you want maximum realism. | SetInfoText |
| IntelEngine_MCM.psc:1520 | Enable the autonomous faction politics system. Factions trade, negotiate, scheme, and go to war based on LLM-driven decisions. | SetInfoText |
| IntelEngine_MCM.psc:1522 | How often (in game hours) the Political DM evaluates faction relations and generates events. Default 6. | SetInfoText |
| IntelEngine_MCM.psc:1524 | Experimental, off by default. Needs the mod Political Engine and does nothing without it. When on, its realms, rulers and campaign news feed IntelEngine's politics. Load a save after turning it on: the realms are added at game start and on load. | SetInfoText |
| IntelEngine_MCM.psc:1526 | Only matters with Political Engine integration on. When off, reactions to Political Engine's news are recorded without changing faction relations or your standing. | SetInfoText |
| IntelEngine_MCM.psc:1538 | Which NPCs may come from further away for this kind of story.  |  |
| IntelEngine_MCM.psc:1540 | Default for "Someone comes to see you": Same hold (civilians). | a value shown |
| IntelEngine_MCM.psc:1542 | Default for "Someone brings a rumor": Same town (everyone). | a value shown |
| IntelEngine_MCM.psc:1544 | Default for "A meeting on the road": No restriction. | a value shown |
| IntelEngine_MCM.psc:1546 | Default for "An ambush": Same hold (civilians). | a value shown |
| IntelEngine_MCM.psc:1548 | Default for "Someone follows you unseen": Same hold (civilians). | a value shown |
| IntelEngine_MCM.psc:1550 | Default for "A message is delivered": Same hold (except followers). | a value shown |
| IntelEngine_MCM.psc:1552 | Default for "Someone has work for you": No restriction. | a value shown |
| IntelEngine_MCM.psc:1574 | Cancel scheduled task?\n{1} | ShowMessage |
| IntelEngine_MCM.psc:1600 | Clear task in slot {1}?\n{2} | ShowMessage |
| IntelEngine_MCM.psc:1614 | IntelEngine: Slot {1} cleared | Debug.Notification |

## 7. The DLL

| where | English | note |
|---|---|---|
| FactionPolitics.cpp:2392 | Conduct report failed: politics is not initialized. | RE::SendHUDMessage: a notice that bypasses NotifyPlayer |
| FactionPolitics.cpp:2411 | Conduct report failed: invalid sentiment '{1}'. | RE::SendHUDMessage: a notice that bypasses NotifyPlayer |
| FactionPolitics.cpp:2419 | Conduct report failed: unknown faction '{1}'. | RE::SendHUDMessage: a notice that bypasses NotifyPlayer |
| FactionPolitics.cpp:2425 | Conduct report failed: political database is unavailable. | RE::SendHUDMessage: a notice that bypasses NotifyPlayer |
| FactionPolitics.cpp:2482 | Reported to {1}: {2} standings changed. | RE::SendHUDMessage: a notice that bypasses NotifyPlayer |
| Arcs.h:955 | {asker} is dead. | TaleClosing: the journal's closing line of a tale when its writer left none; set as an item name by ArcBridge::ApplyTitles, not through LanguageBridge::Label |
| Arcs.h:956 | I never saw it through, and {asker} has stopped waiting. | TaleClosing: the journal's closing line of a tale when its writer left none; set as an item name by ArcBridge::ApplyTitles, not through LanguageBridge::Label |
| Arcs.h:957 | There is nothing more to be done about it now. | TaleClosing: the journal's closing line of a tale when its writer left none; set as an item name by ArcBridge::ApplyTitles, not through LanguageBridge::Label |
| Arcs.h:958 | It is over. | TaleClosing: the journal's closing line of a tale when its writer left none; set as an item name by ArcBridge::ApplyTitles, not through LanguageBridge::Label |
| ArcBridge.cpp:573 | {1}'s {2} | the thief's loot: the asker's name + English possessive + the item |
| ArcBridge.cpp:601 | Bloodied {1} | the murder's proof |
| ArcBridge.cpp:603 | {1}'s {2} | the victim's belonging |
| ArcBridge.cpp:2236 | Cut loose | the crosshair's word; read at the main menu from IntelEngine.esp's messages (0x58E5-0x58E7): an .esp translation reaches it, a pack does not |
| ArcBridge.cpp:2237 | Examine | the crosshair's word; read at the main menu from IntelEngine.esp's messages (0x58E5-0x58E7): an .esp translation reaches it, a pack does not |
| ArcBridge.cpp:2238 | Lay to rest | the crosshair's word; read at the main menu from IntelEngine.esp's messages (0x58E5-0x58E7): an .esp translation reaches it, a pack does not |
| QuestBridge.cpp:367 | {1}'s {2} | a keepsake's name, on the item and as the journal title ({item}): the giver + English possessive + the item |
| Quests.h:508 | Half-Hand, the Crow, Bone-Eye, Blackwater, the Unlucky, Ash-Tongue, Red-Knife, the Gaunt, Cold-Heart, Iron-Jaw, the Quiet, Wolfsbane, Grey-Cloak, Coin-Biter | FoeName: a brigand's by-name, on the actor and in the title "Bounty: {1}" |
| Papyrus.cpp:6020 | in {1} days | time words inside the scheduling notices; the packs read them in their own rows ("... in {3} days {4}") |
| Papyrus.cpp:6022 | tomorrow | the same: the packs have rows with "tomorrow" |
| Papyrus.cpp:6027 | AM / PM | the hour as "10 AM": stays as written inside every translated scheduling notice |

Not counted: `GetDirectorFailureMessage` (Papyrus.cpp:901) is English but goes through `NotifyPlayer`, and both its sentences are pack rows; `BattleManager::GetBattleNotification` (BattleManager.cpp:1552, 18 sentences) is called by no script; a tale's log (ArcBridge.cpp:3649, the story so far and its ending) is the writer's text, written in the reading language, and never passes through `LanguageBridge::Label`.


## 8. SkyrimNet's settings page (`manifest.yaml`, made from `SettingsBridge.cpp`)

The names, help texts, sections and choices of IntelEngine's page in SkyrimNet's settings. The 59 shared settings
come from the table in `SKSE/src/SettingsBridge.cpp` (the manifest is written from it), the rest are written by hand in
the manifest (in both repos). A choice's word is also the value `settings.yaml` stores, so choices cannot be
translated without a separate label. Whether SkyrimNet can show a manifest in another language is not known here.

| where | English | field |
|---|---|---|
| manifest.yaml:4 | NPC task & story automation engine | help text |
| manifest.yaml:9 | Story Dungeon Master decisions — uses base OpenRouter config unless overridden below | help text |
| manifest.yaml:14 | Dashboard Hotkey | setting name |
| manifest.yaml:17 | Main key for dashboard toggle (VK code, e.g., 55 = 7). Set to -1 to disable. Click Save to apply; the dashboard's Settings tab also changes it. | help text |
| manifest.yaml:21 | UI | section |
| manifest.yaml:23 | Dashboard Modifiers | setting name |
| manifest.yaml:26 | Modifier key bitmask: 1=Ctrl, 2=Shift, 4=Alt. Combine with +. Default 2 = Shift. | help text |
| manifest.yaml:32 | UI Scale | setting name |
| manifest.yaml:35 | Dashboard zoom level. 1.3 = default, 1.5-2.0 for 4K monitors. | help text |
| manifest.yaml:42 | Faction Blocklist | setting name |
| manifest.yaml:45 | Comma-separated faction EditorIDs to exclude from story candidates. Append :rank to filter by max rank (e.g., PrisonerFaction,ImperialFaction:1 blocks all prisoners and Imperials rank 0-1) | help text |
| manifest.yaml:47 | Story Engine | section |
| manifest.yaml:49 | Location Blocklist | setting name |
| manifest.yaml:52 | Comma-separated location names where NPC dispatch is blocked (e.g., 'Bleak Falls Barrow,Dustman's Cairn'). Case-insensitive. Blocks new dispatches and cancels in-progress ones. | help text |
| manifest.yaml:56 | NPC Blocklist | setting name |
| manifest.yaml:59 | Comma-separated NPC display names to exclude from story candidates (e.g., 'Nazeem,Heimskr'). Case-insensitive. Blocked NPCs will never be picked by the Dungeon Master. | help text |
| manifest.yaml:64 | Faction Whitelist | setting name |
| manifest.yaml:67 | Comma-separated faction EditorIDs. If non-empty, ONLY NPCs in these factions can be dispatched. Empty = all factions allowed. | help text |
| manifest.yaml:71 | Location Whitelist | setting name |
| manifest.yaml:74 | Comma-separated location names. If non-empty, NPCs can only visit you when you are in one of these locations. Empty = all locations allowed. | help text |
| manifest.yaml:78 | NPC Whitelist | setting name |
| manifest.yaml:81 | Comma-separated NPC display names. If non-empty, ONLY these NPCs can be dispatched. Empty = all NPCs allowed. | help text |
| manifest.yaml:85 | Language of journal and messages | setting name |
| manifest.yaml:88 | What you read (notices, the journal of a story, the words on buttons). auto: the language your game is in, told by the game's own setting and, where that says English, by the script its place names are written in (a translation the game never shipped). Or a language pack by name (russian, german, japanese, korean, chinese, french, spanish, italian, polish), or english. Packs are files in Data/SKSE/Plugins/IntelEngine/languages; a language without translated sentences reads English. | help text |
| manifest.yaml:90 | Language | section |
| manifest.yaml:92 | Languages people speak | setting name |
| manifest.yaml:95 | The languages people speak besides the game's own, so that a time, a place or a name they say in it is understood. auto: read from SkyrimNet's translator settings (an English game whose people speak Albanian is told that way). Or names with commas between them (albanian, russian). | help text |
| manifest.yaml:101 | Stories | setting name |
| manifest.yaml:104 | The dungeon master: NPCs who know you come to you, send word, lie in wait or bring work. | help text |
| manifest.yaml:106 | Stories | section |
| manifest.yaml:108 | Check interval (game hours) | setting name |
| manifest.yaml:111 | At Classic: how often the dungeon master looks for somebody with a reason to come to you. Above Classic it is not used: the pace is 'How often something happens', and the dungeon master looks when you arrive somewhere, when you wake, and every three game hours besides. | help text |
| manifest.yaml:117 | Long absence (days) | setting name |
| manifest.yaml:120 | After this many days without meeting you, an NPC counts as not having seen you for a long time. | help text |
| manifest.yaml:126 | Longest journey (days) | setting name |
| manifest.yaml:129 | A story NPC who has been on the way for this long is brought to the end of the journey. | help text |
| manifest.yaml:135 | Move a stuck traveller along | setting name |
| manifest.yaml:138 | A story NPC who cannot get on is placed further along the way, out of your sight. | help text |
| manifest.yaml:142 | Story type: someone comes to see you | setting name |
| manifest.yaml:148 | Story type: someone brings a rumor | setting name |
| manifest.yaml:154 | Story type: a meeting on the road | setting name |
| manifest.yaml:160 | Story type: a message is delivered | setting name |
| manifest.yaml:166 | Story type: someone has work for you | setting name |
| manifest.yaml:169 | People who need your help. Off: nobody brings you their trouble, and no story of the town begins. | help text |
| manifest.yaml:173 | Story type: an ambush | setting name |
| manifest.yaml:176 | Someone with a grudge lies in wait on the road. Off: no ambush inside a town's story either. | help text |
| manifest.yaml:180 | Story type: a faction's patrol hunts you | setting name |
| manifest.yaml:183 | A faction that holds you for an enemy (standing -40 or worse) sends men after you on the road, now and then. Never while you are with somebody, resting or settled somewhere. | help text |
| manifest.yaml:187 | Story type: someone follows you unseen | setting name |
| manifest.yaml:190 | Someone shadows you from a distance, out of doors. Off: nobody does inside a town's story either. | help text |
| manifest.yaml:194 | NPCs sent into dangerous places | setting name |
| manifest.yaml:197 | Who may be sent to you while you are out in the wilds, away from any settlement. Above Classic nobody is sent into a dungeon after you whatever this says (they wait until you are out), and nobody is ever sent to another plane. | help text |
| manifest.yaml:199 | Allow all NPCs | choice (also the stored value; listed once) |
| manifest.yaml:200 | Block civilians only | choice (also the stored value; listed once) |
| manifest.yaml:201 | Allow followers only | choice (also the stored value; listed once) |
| manifest.yaml:202 | Block all NPCs | choice (also the stored value; listed once) |
| manifest.yaml:206 | NPCs sent into your home | setting name |
| manifest.yaml:209 | Who may come to you while you are in one of your homes. | help text |
| manifest.yaml:219 | How alive the world is | setting name |
| manifest.yaml:222 | Classic: as before 4.0. Rumors: the world moves on while you are away and you hear about it, but followers you left wait where you left them. Lives: followers you left behind live lives of their own (work at an inn, an adventure, a trip) and you can come upon them later; people close to you miss you while you are away. | help text |
| manifest.yaml:224 | Classic | choice (also the stored value; listed once) |
| manifest.yaml:225 | Rumors | choice (also the stored value; listed once) |
| manifest.yaml:226 | Lives | choice (also the stored value; listed once) |
| manifest.yaml:228 | Living world | section |
| manifest.yaml:230 | Interruptions at bad moments | setting name |
| manifest.yaml:233 | Whether someone may turn up during a fight, a conversation, a scene or a menu: never, only for something urgent, or now and then, as in real life. | help text |
| manifest.yaml:235 | Never | choice (also the stored value; listed once) |
| manifest.yaml:236 | If urgent | choice (also the stored value; listed once) |
| manifest.yaml:237 | Realistic | choice (also the stored value; listed once) |
| manifest.yaml:241 | Quest NPCs may move | setting name |
| manifest.yaml:244 | Whether NPCs who take part in a running quest may travel or move for a story. The target of an objective you have open never moves. | help text |
| manifest.yaml:247 | If important | choice (also the stored value; listed once) |
| manifest.yaml:248 | Freely | choice (also the stored value; listed once) |
| manifest.yaml:252 | How often something happens | setting name |
| manifest.yaml:255 | About one notable moment every two game days, every game day, or twice a game day. Moments wait for a good time: arriving in a town or an inn, coming home, leaving a dungeon, waking up. | help text |
| manifest.yaml:257 | Calm | choice (also the stored value; listed once) |
| manifest.yaml:258 | Normal | choice (also the stored value; listed once) |
| manifest.yaml:259 | Lively | choice (also the stored value; listed once) |
| manifest.yaml:263 | Skipped time counts toward the pace | setting name |
| manifest.yaml:266 | Off: waiting, sleeping and fast travel do not bring the next moment sooner. It also takes time actually played: eight minutes between two moments, four minutes in a town before a story of its own begins there, twenty minutes between two such stories. On: the game's clock alone counts: skip a day and a day's worth of moments is due. | help text |
| manifest.yaml:270 | A follower told to wait waits (days) | setting name |
| manifest.yaml:273 | How long after you told a follower to wait they start a life of their own, once their follower mod lets them go. While it still holds them, they keep waiting. | help text |
| manifest.yaml:279 | A follower left behind moves on (days) | setting name |
| manifest.yaml:282 | How long a follower you dismissed stays where you left them before starting a life of their own. | help text |
| manifest.yaml:289 | Someone comes to see you | setting name |
| manifest.yaml:292 | How far this kind of story may reach for its NPC. Same hold or same town keeps visitors local; the groups in brackets are the ones the limit applies to. | help text |
| manifest.yaml:294 | No restriction | choice (also the stored value; listed once) |
| manifest.yaml:295 | Same hold (civilians) | choice (also the stored value; listed once) |
| manifest.yaml:296 | Same hold (except followers) | choice (also the stored value; listed once) |
| manifest.yaml:297 | Same hold (everyone) | choice (also the stored value; listed once) |
| manifest.yaml:298 | Same town (civilians) | choice (also the stored value; listed once) |
| manifest.yaml:299 | Same town (except followers) | choice (also the stored value; listed once) |
| manifest.yaml:300 | Same town (everyone) | choice (also the stored value; listed once) |
| manifest.yaml:302 | Stories: how far NPCs travel | section |
| manifest.yaml:304 | Someone brings a rumor | setting name |
| manifest.yaml:307 | How far this kind of story may reach for its NPC. Above Classic the director keeps people of other holds at home all the same, unless they are close to you and the matter presses. | help text |
| manifest.yaml:319 | A meeting on the road | setting name |
| manifest.yaml:322 | How far this kind of story may reach for its NPC. Same hold or same town keeps visitors local; the groups in brackets are the ones the limit applies to. | help text |
| manifest.yaml:334 | A message is delivered | setting name |
| manifest.yaml:337 | How far this kind of story may reach for its NPC. Above Classic the director keeps people of other holds at home all the same, unless they are close to you and the matter presses. | help text |
| manifest.yaml:349 | Someone has work for you | setting name |
| manifest.yaml:352 | How far this kind of story may reach for its NPC. Above Classic the director keeps people of other holds at home all the same, unless they are close to you and the matter presses. | help text |
| manifest.yaml:364 | An ambush | setting name |
| manifest.yaml:367 | How far this kind of story may reach for its NPC. Above Classic the director keeps people of other holds at home all the same, unless they are close to you and the matter presses. | help text |
| manifest.yaml:379 | Someone follows you unseen | setting name |
| manifest.yaml:382 | How far this kind of story may reach for its NPC. Above Classic the director keeps people of other holds at home all the same, unless they are close to you and the matter presses. | help text |
| manifest.yaml:395 | Quest kind: clear out enemies | setting name |
| manifest.yaml:398 | A bounty on what lives at a place. Off: no bounty, and no master of a place to kill inside a town's story. | help text |
| manifest.yaml:400 | Stories: quests | section |
| manifest.yaml:402 | Quest kind: rescue someone | setting name |
| manifest.yaml:405 | Someone of the town held at a place out in the world. Off: nobody is held in a story. | help text |
| manifest.yaml:409 | Quest kind: find an item | setting name |
| manifest.yaml:412 | A keepsake lost at a place. Off: no keepsake hunt. | help text |
| manifest.yaml:416 | Quest kind: fight a faction's enemies | setting name |
| manifest.yaml:422 | Quest kind: rescue for a faction | setting name |
| manifest.yaml:428 | Quest kind: join a faction's battle | setting name |
| manifest.yaml:434 | Quest expires after (days) | setting name |
| manifest.yaml:437 | A quest of the dungeon master's own that you were offered and did not finish ends after this many days. A story of the town says in its offer how long it waits. | help text |
| manifest.yaml:444 | NPCs visit and talk to each other | setting name |
| manifest.yaml:447 | NPCs who know each other meet without you, and what comes of it spreads as gossip. | help text |
| manifest.yaml:449 | NPCs among themselves | section |
| manifest.yaml:451 | Kind: one NPC goes to see another | setting name |
| manifest.yaml:457 | Kind: gossip passed on | setting name |
| manifest.yaml:463 | Check interval (game hours) | setting name |
| manifest.yaml:471 | Rest per NPC (game hours) | setting name |
| manifest.yaml:480 | NPCs on a task at once | setting name |
| manifest.yaml:483 | Travel, errands, meetings and stories all take one of these places. | help text |
| manifest.yaml:487 | Tasks | section |
| manifest.yaml:489 | Wait at a destination (game hours) | setting name |
| manifest.yaml:492 | How long an NPC you sent somewhere waits there for you before going home. | help text |
| manifest.yaml:498 | Visit ends when you walk away (units) | setting name |
| manifest.yaml:501 | An NPC who stayed with you after a task goes back to their own day once you are this far away. | help text |
| manifest.yaml:507 | Report back after a delivery | setting name |
| manifest.yaml:510 | An NPC who delivered a message for you comes back to tell you how it went. | help text |
| manifest.yaml:515 | Go somewhere | setting name |
| manifest.yaml:518 | Ask you before an NPC starts this task: never, only for your followers, or for everyone. | help text |
| manifest.yaml:520 | Disabled | choice (also the stored value; listed once) |
| manifest.yaml:521 | Followers only | choice (also the stored value; listed once) |
| manifest.yaml:522 | Everyone | choice (also the stored value; listed once) |
| manifest.yaml:524 | Tasks: ask before starting | section |
| manifest.yaml:526 | Deliver a message | setting name |
| manifest.yaml:529 | Ask you before an NPC starts this task: never, only for your followers, or for everyone. | help text |
| manifest.yaml:537 | Fetch someone | setting name |
| manifest.yaml:540 | Ask you before an NPC starts this task: never, only for your followers, or for everyone. | help text |
| manifest.yaml:548 | Escort someone | setting name |
| manifest.yaml:551 | Ask you before an NPC starts this task: never, only for your followers, or for everyone. | help text |
| manifest.yaml:559 | Search for someone | setting name |
| manifest.yaml:562 | Ask you before an NPC starts this task: never, only for your followers, or for everyone. | help text |
| manifest.yaml:570 | Arrange a meeting | setting name |
| manifest.yaml:573 | Ask you before an NPC starts this task: never, only for your followers, or for everyone. | help text |
| manifest.yaml:581 | Fetch someone later | setting name |
| manifest.yaml:584 | Ask you before an NPC starts this task: never, only for your followers, or for everyone. | help text |
| manifest.yaml:592 | Deliver a message later | setting name |
| manifest.yaml:595 | Ask you before an NPC starts this task: never, only for your followers, or for everyone. | help text |
| manifest.yaml:604 | Go somewhere | setting name |
| manifest.yaml:607 | On: your followers can be given this action like anyone else. Off: it is refused for your active followers, without a prompt. | help text |
| manifest.yaml:609 | Tasks: what followers can be asked to do | section |
| manifest.yaml:611 | Deliver a message | setting name |
| manifest.yaml:614 | On: your followers can be given this action like anyone else. Off: it is refused for your active followers, without a prompt. | help text |
| manifest.yaml:618 | Fetch someone | setting name |
| manifest.yaml:621 | On: your followers can be given this action like anyone else. Off: it is refused for your active followers, without a prompt. | help text |
| manifest.yaml:625 | Escort someone | setting name |
| manifest.yaml:628 | On: your followers can be given this action like anyone else. Off: it is refused for your active followers, without a prompt. | help text |
| manifest.yaml:632 | Search for someone | setting name |
| manifest.yaml:635 | On: your followers can be given this action like anyone else. Off: it is refused for your active followers, without a prompt. | help text |
| manifest.yaml:639 | Arrange a meeting | setting name |
| manifest.yaml:642 | On: your followers can be given this action like anyone else. Off: it is refused for your active followers, without a prompt. | help text |
| manifest.yaml:646 | Fetch someone later | setting name |
| manifest.yaml:649 | On: your followers can be given this action like anyone else. Off: it is refused for your active followers, without a prompt. | help text |
| manifest.yaml:653 | Deliver a message later | setting name |
| manifest.yaml:656 | On: your followers can be given this action like anyone else. Off: it is refused for your active followers, without a prompt. | help text |
| manifest.yaml:661 | Wait for you at a meeting (game hours) | setting name |
| manifest.yaml:664 | How long an NPC waits at the agreed place before giving up. | help text |
| manifest.yaml:668 | Meetings | section |
| manifest.yaml:670 | On time until (game hours late) | setting name |
| manifest.yaml:673 | Arriving within this time after the agreed hour still counts as on time. | help text |
| manifest.yaml:680 | Refresh an NPC's profile after much talk | setting name |
| manifest.yaml:683 | Asks SkyrimNet to update an NPC's dynamic profile once you have exchanged enough lines with them. | help text |
| manifest.yaml:685 | Other | section |
| manifest.yaml:687 | Lines before a profile refresh | setting name |
| manifest.yaml:695 | Debug messages | setting name |
| manifest.yaml:698 | Shows what IntelEngine does as notices on screen. The Papyrus log has it either way; for more in the DLL's own log set DebugLevel=4 in IntelEngine.ini. | help text |
| manifest.yaml:705 | Faction politics | setting name |
| manifest.yaml:708 | Factions change their relations, quarrel and go to war while you play. | help text |
| manifest.yaml:710 | Politics | section |
| manifest.yaml:712 | Check interval (game hours) | setting name |
| manifest.yaml:715 | How often the political game master considers a new event. | help text |
| manifest.yaml:721 | Largest change of a relation per check | setting name |
| manifest.yaml:724 | How far two factions' relation can move in one check. | help text |
| manifest.yaml:730 | Wars at the same time | setting name |
| manifest.yaml:733 | No new war is declared while this many are being fought. | help text |
| manifest.yaml:739 | Political Engine integration (experimental) | setting name |
| manifest.yaml:742 | Off by default. When on, and the mod Political Engine is installed, its realms, rulers and campaign news feed IntelEngine's politics. Realms are added on the next load of a save. | help text |
| manifest.yaml:747 | API Endpoint | setting name |
| manifest.yaml:750 | Custom API endpoint URL (e.g., http://localhost:5000/v1). Leave empty to use the base OpenRouter endpoint. | help text |
| manifest.yaml:752 | LLM Overrides | section |
| manifest.yaml:754 | API Key | setting name |
| manifest.yaml:757 | API key for the custom endpoint. Leave empty to use the base OpenRouter API key. | help text |
| manifest.yaml:761 | Model Override | setting name |
| manifest.yaml:764 | Override model name for story DM. Leave empty to use the same model as dialogue. | help text |
| manifest.yaml:768 | Temperature Override | setting name |
| manifest.yaml:771 | Override sampling temperature. Set to 0 to use the base config value. | help text |
| manifest.yaml:777 | Max Tokens Override | setting name |
| manifest.yaml:780 | Override max response tokens. Set to 0 to use the base config value. | help text |
| manifest.yaml:786 | Timeout Override | setting name |
| manifest.yaml:789 | Override request timeout in seconds. Set to 0 to use the base config value. | help text |

## 9. `IntelEngine.esp` (built by `esp/EspBuilder/Program.cs`)

Quest names, objectives and log entries of the journal records, the tale log items, the crosshair words. They are
translated the way any plugin is (an .esp translation, xTranslator); a pack cannot reach them unless the DLL sets them
at run time through `LanguageBridge::Label`, as it does for the battle call's objective and a tale's steps. Each
objective and log phrasing exists once per journal slot in the plugin (4 story slots, 6 thread slots...); it is listed
once, at its line in the builder.

| where | English | kind |
|---|---|---|
| Program.cs:26 | Meet <Alias=ThreadNpc{0}> at <Alias=ThreadPlace{0}> | objective |
| Program.cs:27 | Find <Alias=ThreadNpc{0}> | objective |
| Program.cs:28 | Speak with <Alias=ThreadNpc{0}> | objective |
| Program.cs:29 | Rescue <Alias=ThreadNpc{0}> | objective |
| Program.cs:30 | Go to <Alias=ThreadPlace{0}> | objective |
| Program.cs:31 | Return to <Alias=ThreadNpc{0}> | objective |
| Program.cs:32 | Confront <Alias=ThreadNpc{0}> | objective |
| Program.cs:33 | Bring <Alias=ThreadItem{0}> to <Alias=ThreadNpc{0}> | objective |
| Program.cs:34 | Wait for <Alias=ThreadNpc{0}> at <Alias=ThreadPlace{0}> | objective |
| Program.cs:35 | Escort <Alias=ThreadNpc{0}> to <Alias=ThreadPlace{0}> | objective |
| Program.cs:69 | Unfinished Business | name |
| Program.cs:112 | Examine | crosshair word (a message's name) |
| Program.cs:113 | Lay to rest | crosshair word (a message's name) |
| Program.cs:197 | Cut loose | crosshair word (a message's name) |
| Program.cs (Papers) | Letter, Ledger, Journal, Note | names of the four papers; the DLL gives a tale's writer the pack's label, a story stamps its own name on its copy |
| Program.cs (Papers) | what a copy of each reads | shown only if the DLL could not put a page in; the DLL shows the pack's label of `ArcBridge::kPapers[].blank` |
| Program.cs:215 | Ask <Alias=Witness> about the theft | objective |
| Program.cs:216 | Find <Alias.ShortName=Asker>'s <Alias=Item0> | objective |
| Program.cs:217 | Confront <Alias=Culprit> | objective |
| Program.cs:218 | Return the <Alias=Item0> to <Alias=Asker> | objective |
| Program.cs:219 | Hear <Alias=Rival>'s side | objective |
| Program.cs:220 | Tell <Alias=Asker> how to settle it with <Alias=Rival> | objective |
| Program.cs:221 | Beat <Alias=Rival> in a brawl | objective |
| Program.cs:222 | Make peace with <Alias=Rival> | objective |
| Program.cs:223 | See where <Alias=Victim> died | objective |
| Program.cs:224 | Question <Alias=Witness> | objective |
| Program.cs:225 | Find proof of who killed <Alias=Victim> | objective |
| Program.cs:226 | Name the killer to <Alias=Asker> | objective |
| Program.cs:233 | <Alias=Asker> came to me: someone broke into <Alias.ShortName=Asker>'s home in <Alias=Place> and stole the <Alias=Item0>. <Alias.ShortName=Asker> wants it back. | log entry |
| Program.cs:234 | <Alias=Witness> saw <Alias=Culprit> slip out of <Alias.ShortName=Asker>'s home that night with something under one arm. | log entry |
| Program.cs:235 | I found <Alias.ShortName=Asker>'s <Alias=Item0> among <Alias.ShortName=Culprit>'s things. | log entry |
| Program.cs:236 | I handed <Alias=Culprit> over to the guard. | log entry |
| Program.cs:237 | I let <Alias=Culprit> go. | log entry |
| Program.cs:238 | <Alias=Culprit> is dead. | log entry |
| Program.cs:239 | I brought the <Alias=Item0> back to <Alias.ShortName=Asker>. | log entry |
| Program.cs:240 | I never brought the <Alias=Item0> back, and <Alias.ShortName=Asker> has given up on it. | log entry |
| Program.cs:241 | <Alias=Asker> is dead. | log entry |
| Program.cs:242 | I kept <Alias.ShortName=Asker>'s <Alias=Item0> for myself. | log entry |
| Program.cs:243 | <Alias=Asker> and <Alias=Rival> are at odds, and <Alias.ShortName=Asker> asked me to settle it, one way or another. | log entry |
| Program.cs:244 | I heard <Alias.ShortName=Rival>'s side of it. | log entry |
| Program.cs:245 | I told <Alias.ShortName=Asker> I would settle it with my fists against <Alias.ShortName=Rival>. | log entry |
| Program.cs:246 | I told <Alias.ShortName=Asker> I would try to make peace between them. | log entry |
| Program.cs:247 | <Alias.ShortName=Rival> would not be talked round. | log entry |
| Program.cs:248 | I beat <Alias.ShortName=Rival> in a brawl, and the quarrel is settled. | log entry |
| Program.cs:249 | <Alias.ShortName=Rival> beat me in the brawl. | log entry |
| Program.cs:250 | I never settled it, and <Alias.ShortName=Asker> stopped waiting. | log entry |
| Program.cs:252 | <Alias.ShortName=Asker> and <Alias.ShortName=Rival> have made peace, and <Alias.ShortName=Asker> paid me. | log entry |
| Program.cs:253 | I chose to stay out of their quarrel. | log entry |
| Program.cs:254 | <Alias=Victim> was found dead at home in <Alias=Place>. <Alias=Asker> asked me to find the killer and name them with proof. | log entry |
| Program.cs:255 | I have seen where <Alias.ShortName=Victim> died. | log entry |
| Program.cs:256 | <Alias=Witness> saw <Alias=Culprit> near <Alias.ShortName=Victim>'s house that night, hurrying away. | log entry |
| Program.cs:257 | I found something that points to the killer. | log entry |
| Program.cs:258 | <Alias.ShortName=Asker> will not take anyone's guilt on my word alone. I need proof. | log entry |
| Program.cs:259 | I proved that <Alias=Culprit> killed <Alias.ShortName=Victim>, and the guard took <Alias.ShortName=Culprit>. | log entry |
| Program.cs:260 | <Alias.ShortName=Victim>'s killer was never found. | log entry |
| Program.cs:262 | I named the wrong person. <Alias.ShortName=Victim>'s killer is still free. | log entry |
| Program.cs:285 | A Story | journal title until the DLL sets one (through Label) |
| Program.cs:313 | A step of the story | objective until the DLL sets the tale's step (through Label) |
| Program.cs:343 | The story so far | name of the item that carries a tale's log; the DLL replaces it with the log's text |
| Program.cs:343 | How it ended | name of the item that carries a tale's log; the DLL replaces it with the log's text |
| Program.cs:388 | Search <Alias=Place> for the <Alias=Item> | objective |
| Program.cs:389 | Recover the <Alias=Item> | objective |
| Program.cs:390 | Bring the <Alias=Item> to <Alias=Giver> | objective |
| Program.cs:391 | Deal with the brigand at <Alias=Place> | objective |
| Program.cs:392 | Tell <Alias=Giver> it is done | objective |
| Program.cs:393 | Go with <Alias=Ally> to <Alias=Place> | objective |
| Program.cs:394 | Clear <Alias=Place> with <Alias=Ally> | objective |
| Program.cs:395 | Share the spoils with <Alias=Ally> | objective |
| Program.cs:402 | <Alias=Giver> came to me for help: someone stole a <Alias=Item> from <Alias.PronounObj=Giver> and was seen running toward <Alias=Place>. <Alias.ShortName=Giver> wants it back. | log entry |
| Program.cs:403 | I have reached <Alias=Place>, where the thief was headed. | log entry |
| Program.cs:404 | I have found the <Alias=Item>. | log entry |
| Program.cs:405 | I brought the <Alias=Item> back to <Alias.ShortName=Giver>. | log entry |
| Program.cs:406 | I never brought the <Alias=Item> back to <Alias.ShortName=Giver>. | log entry |
| Program.cs:407 | I never went after the <Alias=Item>, and <Alias.ShortName=Giver> has given up on it. | log entry |
| Program.cs:408 | <Alias=Giver> is dead. There is no one left to return the <Alias=Item> to. | log entry |
| Program.cs:409 | <Alias=Giver> asked me to deal with a brigand whose gang has settled at <Alias=Place> and preys on everyone near it. | log entry |
| Program.cs:410 | The brigand is dead. | log entry |
| Program.cs:411 | I told <Alias.ShortName=Giver> that the brigand is dead. | log entry |
| Program.cs:412 | I never dealt with the brigand, and <Alias.ShortName=Giver> stopped waiting. | log entry |
| Program.cs:413 | <Alias=Giver> is dead. | log entry |
| Program.cs:414 | I ran into <Alias=Ally>, who is on the way to clear out <Alias=Place> and asked me to come along. | log entry |
| Program.cs:415 | We reached <Alias=Place> together. | log entry |
| Program.cs:416 | We cleared <Alias=Place>. | log entry |
| Program.cs:417 | <Alias=Ally> and I shared what we found in <Alias=Place>. | log entry |
| Program.cs:418 | I never joined <Alias=Ally>, who went to <Alias=Place> alone. | log entry |
| Program.cs:419 | <Alias=Ally> is dead. | log entry |
| Program.cs:433 | A Request | journal title until the DLL sets one (through Label) |
| Program.cs:470 | Armies are drawing up near <Alias=Place>, and those I serve have called everyone of theirs to the line. I am expected there before the battle is decided. | log entry |
| Program.cs:471 | I answered the call and stood in the line near <Alias=Place>. | log entry |
| Program.cs:472 | I was called to the line near <Alias=Place>, and stood with the other side. | log entry |
| Program.cs:473 | I came to the field near <Alias=Place> and took no side. | log entry |
| Program.cs:474 | I stood in the line near <Alias=Place>, and walked away before it was over. | log entry |
| Program.cs:475 | The battle near <Alias=Place> was decided without me. | log entry |
| Program.cs:476 | The armies near <Alias=Place> parted with nothing decided. | log entry |
| Program.cs:487 | Called to the Line | name |
| Program.cs:497 | Stand in the line near <Alias=Place> before the battle is decided | objective until BattleManager::SetCallText sets it (through Label) |
| IntelEngine.esp 0x000D61 IntelEngine | Investigate the threat near <Alias=QuestTarget> | objective (made by hand, not by the builder) |

## 10. The choice popup (`web/choice/index.html`)

The popup's own words around the translated body and buttons. The dashboard's are in `docs/DASHBOARD_STRINGS.md`.

| where | English | note |
|---|---|---|
| choice/index.html:128 | 1-{1} or arrows + Enter · Esc = {2} | the popup's foot line |
| choice/index.html:146 | No answer = {1} | the popup's timer |
| choice/index.html:57 | IntelEngine | the popup's tag |
