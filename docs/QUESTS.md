# IntelEngine 4.0 quests: multi-step, decided by the world

Status: design of 2026-11-18, being built on `feat/4.0-living-world`. The pure logic is `SKSE/src/Quests.h`
(checked offline by `SKSE/tests/QuestsCheck.cpp`), the game side `QuestBridge`, the scripts
`IntelEngine_Quests.psc`, the journal records `esp/EspBuilder`.

## What it must do

The user's brief (2026-11-18): a multi-step quest system that is as reliable as it is immersive, fun and
satisfying, and that works whatever language the player talks in and whatever model they run. NPCs agreeing and
then doing nothing, or a quest that ends because someone *narrated* "hands over the necklace", must not happen.

## The rule

**Words never move a quest. Facts of the game do.** Every step of a quest is a fixed state with a check the engine
makes on the world: who is where, who is dead, what is in whose inventory, who spoke face to face, what time it
is. The dialogue model only ever writes words, from what the record says is true; it is never asked whether a
step happened. So the same quest runs the same way in English, Gheg or Japanese, on a large model or a small one.

Two consequences:
- **State first, words after.** The engine decides the whole quest before anyone speaks of it: who asks, what
  for, where it really is, what really happened (the hidden truth). The giver's prompt then says what they know,
  and they put it in their own words. Nothing they say can make it false, because nothing they say changes it.
- **Choices are actions, not sentences.** The player chooses by doing: whom they hand the item to, whether they
  keep it, whether they go at all. Where a choice has to be explicit (taking the quest on, handing something
  over) it is a choice popup that looks like a dialogue choice, which IntelEngine already uses for quest offers.

## What other games taught (research of 2026-11-18)

- Fill every role from the live world before the quest exists, and start nothing when a role cannot be filled
  (Skyrim's Story Manager, Shadows of Doubt, Wildermyth). Here: no place, no item, no quest, and the 3.8 path runs.
- Repetition comes from the same shape pushed on the player, not from the wording (Fallout 4's radiant quests;
  Bethesda's Bruce Nesmith: tighter conditions, fewer events). Here: the kind changes from one quest to the next, at
  most three open, a giver rests a week, and quests are offered, never forced.
- Check outcomes, not methods (BG3, Witcher 3's condition blocks): the foe dead by anyone's hand counts; a step
  whose fact is already true completes on the spot; out of order is free (Emily Short's storylets).
- Never block the player in view; when a plan breaks, end with a consequence the world remembers (Riedl's narrative
  mediation, Disco Elysium, RimWorld's two timers). Here: every ending writes a memory and what the hold heard.
- Twists must be true from the start and found through something checkable (Shadows of Doubt; the LLM detective
  paper that gates what a suspect may reveal). Here: the truth is rolled when the quest is made; the dead thief is
  really there.
- The reward players remember is the world remembering (Nemesis): gold that really moves, plus callbacks quoting
  the engine's record, never the model's recollection.
- The model writes words; the engine owns truth and actions (generative agents' embellishment, Convai's "Sure, I'll
  follow you" that never moves). Prompts carry facts and explicit not-yet-true claims ("I have paid nothing").

## The fingerprint: stamped items

An item a quest is about is a *stamped* item: a real item of the game whose reference carries its own name
("Hulda's Silver Necklace", `ExtraTextDisplayData`), made by the engine when the quest places it. Every check
looks for that name on that base form, so any other silver necklace does not count, a narrated handover moves
nothing, and the item cannot be faked. The stamp is visible to the player, which also makes it the giver's own
thing. Name format: `{owner}'s {item}`, from the esp (`IntelEngine_QuestName*` messages) so a translation of
IntelEngine.esp translates it too.

## Facts the engine checks (step kinds)

| Step | Done when | Read from |
|---|---|---|
| Reach | the player is in the place (location, or near its anchor) | player location / distance |
| Recover | the stamped item is in the player's inventory | inventory, base form + stamp |
| HandOver | the player gave the stamped item to someone (choice popup, the engine moves it) | the move itself |
| Defeat | the foe is dead, by anyone's hand | `IsDead` |
| Meet | the player is with someone, a few steps away | distance, same space |
| Clear | the foe of the place and their gang are dead | `IsDead` over the spawned ones |
| Talk | the player is with someone while they speak (SkyrimNet's speech events) | `SpeechTracker` |

Endings come from facts too: the giver died, the time ran out (with or without the item in the player's hands),
the item went to someone else, the player said no in the offer popup.

**Out of order is fine.** When a later step's fact is already true, every step before it is done too (the item
recovered before the place was "reached" counts; a foe killed by guards counts). The journal never asks for
something that is already so.

## A quest

A quest is a template plus a cast plus a truth, chosen by code:
- **Template**: a small graph of steps (3 to 5), each with an objective phrasing, a marker, its fact, what comes
  after it, and whether it ends the quest (and how).
- **Cast**: real forms only, from lists the engine builds: the giver (a candidate the director chose), the place
  (a dungeon of the giver's hold that has a boss room in IntelEngine's dungeon index), the item (a real item of
  the load order, by keyword and value), the foe (spawned at the boss room, given a name), a companion.
- **Truth**: what really happened, rolled when the quest is made and never changed (the thief is waiting at the
  place / was killed there by what lives in it). The giver knows only what they would know; the rest is found
  by going there.

Nothing is spawned until the player has taken the quest on; the foe and the item are placed at the boss room then
(the place's cell need not be loaded), the gang when the cell loads (as the 3.8 quests do).

## Templates of the first round

**Keepsake** ("Hulda's Silver Necklace"). Something dear to the giver was stolen; the thief ran toward a place.
1. Search the place (Reach) - marker on the place.
2. Recover the item (Recover) - marker on whoever or whatever holds it once the place is reached.
3. Bring it back (HandOver to the giver) - ends Done: gold, the giver's memory, the deed heard in the hold.
Truths: the thief and a few bandits wait there with it; or the thief lies dead there, killed by what lives in
the place, the necklace on the body. Other endings: kept (time ran out with the item in the player's hands: the
giver knows the player found it and never brought it back), ignored, refused, the giver died.

**Bounty** ("Trouble at Fellglow Keep"). A named foe and their gang have settled in a place near the giver.
1. Deal with the foe (Defeat) - marker on the foe at the boss room.
2. Tell the giver (Talk, with the giver) - ends Done.
Truth: now and then the foe carries something stamped of another person of the hold (taken on the road); finding
it opens an errand of its own ("Bring Lars's Hunting Bow to Lars", the existing storyline errand), so one quest
leads to another person.

**Venture** (the user's question of 2026-11-18: meeting a former follower on their own adventure). A follower
left behind who is living their own life sets out for a dungeon, sometimes with another former follower. The
player meets them on the road (placed ahead out of sight, walking there, as road encounters are) or at the place.
1. Go with them to the place (Reach, together) - marker on the follower.
2. Clear the place together (Clear) - they follow the player inside.
3. Share the spoils (Talk, with them) - ends Done: a share of gold, their memory of it, the deed in the hold.
If the player does not come, they go alone, and their memory says so.

The meeting itself (the user, 2026-11-18: "meeting them in the road and they recognizing me and stopping to talk,
either alone or both with whomever they are travelling ... They may have met other follower types that I haven't
travelled with"): two in three ventures have a companion, another former follower who is free or a follower type of
the load order (the potential follower faction, mods' followers too, catalogued at the main menu) who is free to
leave unseen (no quest of the journal, no shop, no scene, not near the player).

Where they are (the user: "placing them somewhere along the direction the player is walking towards and then they
are there, or the player meets them in some dungeon or in the woods"): ahead of the player, the way the trail of the
last outdoor steps points (or the way the player faces just after a fast travel), 2500 to 7000 units out, within 40
degrees and near the player's height, out of sight, resting. A bench, bedroll, idle marker, map marker or door that
way comes first; otherwise a point of the navmesh, road triangles first (the game marks roads as preferred for
pathing). Both scans read only the loaded cells that way (about 0.1 and 0.4 ms of one frame). Or they are met at the
place itself, when their own life has them adventuring there. They recognise the player in sight within 2500 units
(or anywhere within 1000) and walk over. The ally speaks first: if another conversation holds the scene the greeting
is given again every 20 seconds, nobody sets off before it is said, and the companion has their say only after it
(glad to see an old friend, or meeting a stranger). When they are met, each is given the memory of falling in with
the other.

Coming along happens in the conversation (the user: "if I ask to join, they just join, without popup ... if they
ask me, then the popup appears"): the player asks and the one who agrees chooses TakePlayerAlong, and they join; one
of them asks and chooses InviteAlong, and the popup comes once everyone has finished speaking ("Jenassa and Ugor are
on their way to clear out White River Watch and ask you to come along. Say yes and they follow you there and share
what you find; say no and they go alone."). A player who walks along with them for 40 seconds is asked. Another
mod's follow chosen in the talk counts as joining. Joined, they become the player's followers through the follower
system the player has (SeverActions when installed, else SkyrimNet's follow, else IntelEngine's own follow
package), at the player's pace; when the venture ends they are dismissed through the same system, without
SeverActions' send-home, and walk home at an ordinary pace. Turned down, they answer, then set off for the place
(its boss room, by the reference the quest holds) and are let go once out of sight; when their answer is a task of
their own (GoToLocation), that task leads them. Nobody asked and the player went on: the venture lapses, and
nobody remembers it. A task given to the companion before the venture is taken on (the NPC social engine, another
mod) drops the companion and the ally goes on alone; a task given to the ally ends the venture. The NPC social
engine and the hourly package sweep leave the people of a meeting alone.

## The journal

Each quest is a real side quest with its own title (four slot quests `IntelEngine_Quest0..3`, the title set from
the record when it opens and again after every load), objectives that show one after another with markers, and a
log that reads like a vanilla journal ("Hulda asked me to find her necklace. The thief fled toward Bleak Falls
Barrow."), written in the esp with alias names so it translates. Done quests go to Completed Quests. The errands
and meetings of "Unfinished Business" stay as they are.

## Who knows what

- The giver: what they asked, why, what they offer, what they know of the truth, what the player has done so
  far (the facts), how it ended.
- The hold: a rumor while it is open ("Hulda's necklace was stolen"), the deed when it ends.
- The companion of a venture: where they are going, with whom, what they found.
All of it from the record, in the third or first person, never instructions about what to say.

## How a quest comes about

The staged director (one decision per candidate) picks a person and a kind of moment. When the kind is `quest`
at the Rumors or Lives level, the engine plans a quest for that person first (a template that fits, a place, an
item); only when none fits does the 3.8 writer path run (rescue and faction quests stay there for now). The giver
then comes to the player as story visitors do and makes the offer; once their first line is spoken the offer
popup asks the player. A former follower's venture comes about when the director picks them for a road encounter
while their own life is an adventure.

## What it costs

No call to a model is needed for any step. The watch runs every two seconds over at most four quests, a few
distance and inventory reads each, inside the half-millisecond-a-frame rule (measured in the log like the
other watches). Planning happens on the director's worker; spawning happens once, when the quest is taken on.

## Save data

Co-save record `IEQS` (its own, version 1): the quests with template, step states, cast, truth, stamps, times,
endings. A record that does not load starts empty; forms that left the load order drop the quest.

## Tested in game (test profile, 2026-11-18, by typed talk)

- Keepsake, end to end: Uthgerd came over and asked in her own words, the offer popup, the journal quest titled
  "Uthgerd's Silver Ruby Ring" with its description, the thief "Rorlund the Gaunt" and his gang at Halted Stream
  Camp, the stamped ring on his body ("Uthgerd's Silver Ruby Ring" in the loot list), the step done once it was in
  the pack, the handover popup once she had spoken, 160 gold, her memory, the deed in the hold, Completed Quests.
- Bounty, end to end, and a false claim before the deed: with the first wording Hulda "paid" in words on the
  player's say-so (nothing moved); with "I have paid nothing" and "word of a death like that would reach me" she
  refused: "Bring me proof, or news the guards can swear to, and then we'll talk of coin."
- Venture, end to end: Jenassa met on the road out of Riverwood ("Days of silence, and I find you standing in the
  rain... Come along, and we split whatever we find. You owe me that much."), together to White River Watch, the
  master of the place dead, the split ("Two hundred and ten for you, and I keep my share").
- Road meetings (2026-11-18 afternoon): Jenassa with Illia, Lob and Vilkas in turn, caught up from 2000-2500 units
  behind within 16 seconds; Illia, never met: "She's not wrong — bandits rarely care who walks up. I'm Illia."; Vilkas:
  "A stranger in iron, and a Nord I've never laid eyes on ... Do you fight as well as you walk?"; both follow when
  taken up; turned down, both walk on when the player leaves.
- Road meetings, second round (2026-11-18 evening), on the road west of Riverwood: 6 of 6 points ahead on the road;
  Jenassa walked up the road in the rain; Jenassa greeted first, then Ugor or Vilkas; asked by Jenassa ("Come along,
  share the spoils...") or by Vilkas ("Will you walk with us?"), the popup came 4 to 6 seconds after the last line;
  accepted, both followed through SeverActions; dismissed, Jenassa walked off toward Whiterun (wading the river);
  turned down, "Fine. Ugor and I will clear the Watch without you" and her own GoToLocation to the fort's door; asked
  by the player, TakePlayerAlong and no popup; silent and walked away from, lapsed.
- Declined (memory "refused"), time ran out (memory "never did", the hold hears the gang still holds the place),
  saved and loaded with a quest open (record, title and steps came back).

## Limits found

- The journal's alias names read a spawned actor's template ("Bandit Chief") and an item's base name, not names
  given at runtime: the brigand's name is in the quest title, and objectives say "the brigand", "the Silver Ruby
  Ring".
- A log entry needs its QSDT header: Mutagen leaves it out when the entry's flags are null, and the game then shows
  no log text. Every entry has explicit flags.
- An item placed with PlaceAtMe at an unloaded boss room never reached the thief: the stamped item is made once
  the cell is loaded, at the thief, disabled, and put straight into their inventory.
- Placed a fixed distance ahead, Jenassa stood inside a hillside by the river; a plain XMarker ahead was on a roof;
  a navmesh point 750 units above the road was a cliff top. Points now come from the navmesh's road triangles near
  the player's height.
- "White River Watch" as a destination sent Jenassa to the river by Riverwood: every name with "river", "lake" or
  "bath" in it was read as "go to the water". A real place name is now matched before such words, and the venture
  goes by the place's reference, not its name.
- A travel package put back on with a new target keeps its old one (Jenassa walked on); coming over is another
  package (a jog).
- The engine's lip-sync test misses lines SkyrimNet plays through its own sound player and counts the game's idle
  chatter (Jenassa's hireling lines): the waits before a popup use SkyrimNet's own account of an NPC's speech
  (queued, voiced, audible) and four quiet seconds counted while watching (a narrated gesture is followed by two
  silent seconds while the first spoken line is made).
- The action comes with the reply's text, before a word of it is spoken, so the popup waits for the reply to begin.
- SkyrimNet can drop a narration queued behind another conversation (Olava's, started by its GameMaster): the
  greeting is given again.
- SeverActions may make a venture's ally a follower of its own from their "come along" line; that counts as joining,
  and they stay with the player after the venture, as SeverActions keeps them.

## Not in the first round

Rescues of real people in the new system (they stay in the 3.8 quest path), the bounty's "spoils" truth (the foe
carrying a stamped item of someone else of the hold, which opens an errand of its own: the truth exists, nothing
picks it yet), letters, rivals bidding for an item, quests that hand off between several givers, LLM-written quest
titles.
