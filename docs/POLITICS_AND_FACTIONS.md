# Politics in 4.0: held to the game's own stories, heard as news, edited without a text editor

Written 2026-11-20. Code: `SKSE/src/VanillaWar.h` (the rules, pure logic), `FactionPolitics.cpp` (reading the game,
the gates, the news), `FactionEditor.h/.cpp` and `FactionEditorText.h` (the editor's DLL side), 
`web/dashboard/src/components/FactionEditor.jsx` (its page). Tests: `VanillaWarCheck.cpp`, `FactionEditorCheck.cpp`.

The user (2026-11-20): "politics work in harmony with rumors and dungeon master as well and involve the player more.
Remember, nothing should contradict vanilla quests. And freedom with politics should be taken only after the player
has completed all vanilla quests that would otherwise contradict new wars of our politics system. [...] Also the new
faction editor should be user friendly where a user can easily add new factions and assign new templates that are
available in the modlist they play in."

## 1. A faction is bound by its own story

The civil war was already respected (`VanillaWar.h`, first half): the Empire and the Stormcloaks campaign while the
game's war is undecided and never after, the Concordat holds the Empire and the Thalmor together until then.

New: **every other faction that stands for one of the game's own is bound while the player has not finished that
faction's questline.** Until then its leaders, its hall and its enemies are the game's to decide.

| Faction file's `skyrim_faction_id` | Bound until this quest is finished |
|---|---|
| CompanionsFaction | C06, Glory of the Dead |
| ThievesGuildFaction | TG09, Darkness Returns |
| CollegeofWinterholdFaction | MG08, The Eye of Magnus |
| DarkBrotherhoodFaction | DB11, Hail Sithis!, or DBDestroy (either) |
| ThalmorFaction | MQ305, Dragonslayer |
| CrimeFactionReach (the Silver-Bloods) | MS02, No One Escapes Cidhna Mine |
| CrimeFactionRift (the Black-Briars) | TG09 |
| DLC1HunterFaction, DLC1VampireFaction | DLC1VQ08, Kindred Judgment |

A quest the load order does not have binds nobody. A faction a mod adds has no questline of the game and is free
unless its file entry says otherwise.

**What bound means**, in code and not only in the prompt:

- `WarPossible` is false for any pair with a bound faction, so no war is declared by the dungeon master, by the rule
  that sends Critical pairs to war, or by the dashboard.
- Its relation score with anyone is held just short of what the simulation calls war (`BoundFloor`).
- A war it is in already (an older save) is called off with no winner.
- `HarmsTheBound`: a `war_declaration`, `assassination_attempt`, `surrender`, `battle_result`, `battle_scheduled` or
  `border_skirmish` that involves it is refused when the dungeon master proposes one.
- The dungeon master is told, per faction (`"bound": "Its own story in Skyrim is not finished (Glory of the Dead):
  ..."`) and in a rule of its own ("Factions bound by their own story").
- It still lives among the others: trade, gifts, insults, brawls, friendships and grudges.

Read on the game's thread once a political tick (`BuildPoliticsTickSnapshot`), logged when it changes:
`Politics: CompanionsFaction is bound by its own story in the game ("Glory of the Dead")`, and later
`... is free: its story in the game ("Glory of the Dead") is finished`.

**Saying otherwise** (factions.yaml, or the editor's "Free once these quests of the game are finished"):

```yaml
    free_after: ["C06"]          # this quest instead of the game's own
    free_after: ["DB11|DBDestroy"]   # either of two
    free_after: []               # never bound
```

## 2. Politics is heard

Every political event that is recorded becomes news in the talk of the day (`FactionPolitics::News` ->
`RumorBridge::NoteNews`, see `docs/RUMORS.md`). It begins in the hold of the faction's seat and travels by its kind:

| Event | Weight | How far |
|---|---|---|
| a war declared, ended, a surrender, a peace | 0.95 | every hold, a hold a day |
| a battle fought or about to be fought | 0.8 | three holds beyond |
| an alliance, a pact, a marriage, an attempt on someone's life | 0.75 | three holds beyond |
| trade, gifts, insults, brawls | 0.5 | its own hold; the innkeeper brings it up while it is new |
| spying, sabotage | 0.45 | its own hold, told when asked |

So an innkeeper in Riften says that Maven's agents clashed with Silver-Blood merchants, a battle that is coming is
talked of before it is fought (and the player can be there: battles already begin when the player comes near and can
be joined), and what the player did in one is in the telling ("... was seen fighting for the Stormcloaks").
Played 2026-11-20: three events of one tick each went round their hold (`Rumors: #2 goes round The Rift (news, ...)`).

## 3. The faction editor

Dashboard, Politics tab, "Edit factions" -> "Add or change factions...".

- **Everything it offers comes from the user's own load order.** Opened, it asks the DLL once a session for the
  catalogue (`FactionEditor::Catalog`, on a thread of its own): every faction of the game and of the mods, with name,
  editor id and plugin; every leveled list of people soldiers can be raised from that is not empty; the names of the
  people the game knows. On the test profile: 1,508 factions and 706 lists in 11 ms.
- **Search, do not remember.** "Faction of the game" and "Soldiers" are search fields: type part of a name, an editor
  id or a mod's file name. Leaders are added by name the same way. Kind, seat, how it fights, rivals and allies are
  buttons. A new faction's id is made from its name.
- **A record with no editor id** is written as plugin and form id (`MyMod.esp|0x000801`), which both
  `soldier_template` and now `skyrim_faction_id` accept (`FactionEditor::GameFaction`, used wherever a faction
  file's game faction is looked up).
- **Saving is careful.** What is sent is checked (`Refusal`: ids of letters, digits and `_`; no quote, backslash or
  comma where the file's reader could not read it back; one of the three kinds). The old file is kept as
  `factions.yaml.bak`. Only the `factions:` section is written anew (`Splice`); `default_relations`, `war_allowed` and
  every comment outside the section stay as they were. The new file is read back before it replaces the old one, and
  refused if it does not hold the same number of factions. Then the file is read in again (`ReloadConfig`): new pairs
  get their default relations, and the next political tick uses it.
- What the load order does not have (a game faction of a mod that is switched off) is a warning, not a refusal.
- Comments inside the `factions:` section are not kept by a save (the backup has them).

Test hook `IntelEngine_Arcs.DebugFactionEditor`: reads the catalogue, resolves every faction it offers, and writes
what a save would write to a file in the temp folder and reads it back, without touching the user's file (which mod
managers share between profiles).

## 4. What the audit of 3.8 against 4.0 found, and what was done

Done:

- The engine's own quests (bounty, keepsake, venture) could send the player to a place a quest of the game that is not
  finished has something at. They now keep clear of those and of a story's places (`ArcBridge::GameNeedsPlace`,
  `PlaceInStory`), as the tales did already.
- The user's block lists and white lists (names, factions) were only applied to the old dungeon master's candidates.
  They now hold for the town's stories too: their askers and their cast (`NPCIndex::BlockedForStories`).
- The "Stories" switch did not stop the town's own stories. Off, none begins now; those open go on to their end.
- Word from someone far away had become "a tale whose asker travels in person, or nothing". It is a letter by courier
  again (the old dungeon master's message), which also fits the rule that nobody crosses a hold for a small thing.

Still open (in the order the audit judged them):

1. A faction's quests (a fight for it, a battle, a rescue for it) can barely be reached at "Lives": the director has no
   faction slot that needs no candidate while a war is on, and "rescue for a faction" returns before it looks for a
   member to ask. News of a coming battle is the first step toward the player; a summons from a faction the player
   stands well with is the next.
2. The old "clear out" and "find an item" toggles do not switch the engine's bounty and keepsake quests on and off.
3. An informant who is here still becomes a tale that asks for something, where 3.8 let them bring news and leave.
4. The old dungeon master's fallback (when no candidate is judged at all) can still write a quest from a person's bio
   with only the prompt's rule against retelling the game's quests.
5. NPC gossip does not feed the talk of the day (it is the model's own invention about people, and would spread as
   fact).
