# Battles, the ladder and the call to the line (4.0)

How a battle comes about, how it is fought, what it leaves behind, and what the player gets out of standing in one.
The rules are in two pure headers with their own checks (`Battlefield.h` / `BattlefieldCheck`, `Ranks.h` /
`RanksCheck`); `BattleManager` applies them to the game, `IntelEngine_Battle.psc` runs the scene.

3.8 put both armies down round the spot the player stood on, a tavern floor included, with the outcome already
written; a battle was over in a minute and the player was told the result wherever they were. None of that is left.

## 1. How a battle comes about

- **Armies gather.** When the political dungeon master has two factions at war fight (`battle_result`,
  `battle_scheduled`), nothing is fought yet: `AddPendingBattle` records a gathering near a named place (its
  worldspace and hold with it) that stands for ten game hours. It is recorded and told as `battle_gathering`: the
  holds hear that armies are drawing up before the day is out.
- **The player comes near** (within 6,500 units, same worldspace, outdoors): the gathering becomes a battle on the
  ground there, if there is ground for it.
- **Nobody comes:** when the ten hours are up it is decided unseen, as the dungeon master wrote it (or by the
  strength of the two sides), and told then. A player in that hold is told; everyone else hears it as talk.
- Gatherings come back with the save (co-save record `IEPB`).
- A brawl or a border skirmish between two factions is the same thing in small: `StartSkirmish`, two to six a side,
  nobody held back, recorded as men of the two coming to blows and not as a battle.

## 2. The field

`BattleManager::PickField` looks for ground round the player: `Battlefield::Candidates` gives places up to 5,600
units out, no nearer than 4,500 where the player is looking and 3,200 where they are not; each is tested on the game's own data: land height and water, a navmesh triangle under each
end of both lines, no door within 1,500, nobody of the loaded people within 1,900, not inside a walled town. The two
lines are drawn across the player's line of sight, so both are seen from the side.

Nobody is drawn up in water: any water more than 12 units over the ground counts (`Battlefield::Wet`; it was 60, and
a ford is shallower than that), and it is asked of every soldier's spot in both lines, not of the middles alone.
Played 2026-11-21: the Legion stood knee-deep under the bridge at Honningbrew; the same gathering now draws up on
the dry ground beside the meadery.

Soldiers are made at their own spot in the line, disabled, and faded in by the script, one per call with a frame
between: nobody appears where the player is looking from close by, and no frame makes more than one.
The search goes over as many frames as it needs: one call of `PickField` looks for a tenth of a millisecond, finishes
the question it is on (every question about the ground walks a cell's navmesh), answers "looking", and the script
asks again a frame later (`FindingField` keeps the poll off the battle meanwhile: it would take a battle without
lines for one left over, and close it). On the full Isekai list, 2026-11-21: six looks, 0.77 ms in all, the longest
0.22 ms. In one go the search had taken 0.8 to 1.1 ms on the small test list and 1.5 to 1.7 ms on the full one.

### Making a soldier: three frames (2026-11-22)

Until then a soldier was made in one call, one frame at a time: 0.3 to 0.85 ms of its frame on the test list, 1.0 to
1.4 ms on the full one, of which the game's own making of an actor was 0.8 to 1.0 there. The user, told that this was
the one cost still over: "But we dont want to cause stutters either."

Timed apart on the test list, one soldier made whole (0.51 to 0.57 ms): the game's own making 0.40 to 0.48 by who is
made, setting them down 0.07, their values, sides and place in the roll 0.04, choosing who 0.02, the rest (the two
sides looked up by plugin name, the answer written as JSON) 0.03. The game's part cannot be cut up from outside, so it
has a frame to itself and everything else is done in the frames around it:

1. **Chosen** (`PrepareGroup`, native `BattlePrepareGroup(side, kind, player)`; kind 0 a wave, 1 the hidden force, 2 a
   champion): where the group comes from out of the player's sight (`ComingPlace`: the looks at the ground) and who
   they are (the roll). 1 chosen, 0 nowhere or nobody, -1 no battle. What it chose holds for five seconds
   (`preparedKind`, `preparedAt`); `ReinforceLine` called without it does both itself, as before. The lines of the
   muster have no such step: their place is the field's, and their roll costs 0.02 ms with the first soldier.
2. **Made** (`MakeSoldiers`, from `MusterLine`, `ReinforceLine`, `RaiseLoner`): `PlaceSwitchedOff`, the game's own
   PlaceAtMe (`RELOCATION_ID(55672, 56203)`, the function behind Papyrus' `PlaceAtMe`) asked for one, kept, switched
   off from the start, beside the player. Before, `CreateReferenceAtLocation` made them whole on their spot and
   `Disable()` followed in the next line. Each is noted in `unsettled_` by handle (not by id: an id given to somebody
   else since would be taken for them) with their spot, side and orders. The script waits a frame before it even
   reads what the call answered.
3. **Settled** (`SettleSoldiers`, native `BattleSettle()`): everybody in `unsettled_` is set down on their spot
   (`SetDown`, the engine's move, `RELOCATION_ID(56227, 56626)`; the height of the ground is looked up here), gets
   their values, loses the factions they came with, gets their side and their place in the battle's roll
   (`RegisterActor`, which is why `RememberBattleSoldiers` must come after it). It answers "" or, as JSON `{count,
   formIds}`, those it could not settle: the battle ended between the halves, or their ground is no longer loaded.
   They were never switched on, and the script deletes them (`DropOrphans`). Nobody is switched on before this step.

In the script (`IntelEngine_Battle`): `SettleSoldiers()` (the call, the orphans, a frame's wait) after every making in
`DrawUpLine`, `BringUp`, `SpringFlank`, `BringChampion`, and the same two steps for the killer of
`ExecuteAssassination`. `ResolveBattleFactions` keeps what it found (it went through every plugin's name for every
soldier). `Revert` empties `unsettled_`.

**Measured** (test list, five battles; the last three on the final form, some 150 soldiers; the log line "at most ...
in the last 16 runs" of each clock):

| frame | what the log calls it | ms |
|---|---|---|
| chosen | "choosing where more soldiers come from, and who" | under 0.5 (no notice; a group's eight runs were not reached) |
| made | "drawing up soldiers" / "bringing up soldiers" | 0.42 to 0.47 at most; three notices: 0.50, 0.54, 0.77 |
| of which the game | "the game making one soldier" | 0.39 to 0.48 at most; one notice: 0.72 |
| settled | "setting soldiers down and giving them their side" | 0.09 to 0.13 |

So the half millisecond is kept except where the game's own call breaks it (0.48 of the 0.54, 0.72 of the 0.77): a
soldier with their arms and armour costs the game that much to bring into being, and IntelEngine has 0.04 ms beside
it. A muster of sixteen takes 2.7 s (it took 1.6), out of the player's sight. The battles ran to their end as before
(lines, charge, hidden force, champion, waves, turncoats, the end, the march off, the removals).

**On the full Isekai list** (the user's word for the run, 2026-11-22 night: "Yes if you feel we are making progress
without negative side effects that cost us features"; four battles, the profile put back and checked afterwards):

| frame | ms on the full list |
|---|---|
| made, the whole call | 0.65 to 1.07 |
| of which the game's own making | 0.59 to 1.00, every soldier over half a millisecond |
| settled | 0.12 to 0.31 |
| a fight called off (`CeaseFire`), two soldiers a frame | at most 0.39 (six a frame took 0.87) |

So there the game's own call is over the half millisecond for every soldier, with 0.05 to 0.07 ms of IntelEngine's
beside it: made whole it had been 1.0 to 1.4 ms. Two costs of IntelEngine's own showed only there and are gone:

- The first soldier of a session's first battle: 1.45 ms, 0.58 of it lookups (every faction's soldier template by
  name, which is a search through every leveled list and NPC of the load order when the game's own name table does
  not know it, and the two battle sides). `BattleManager::WarmUp()` makes them while the save comes in (kPostLoadGame
  and kNewGame, beside `ArcBridge::WarmUp()`; "9 soldier templates and the two sides looked up while the save came in
  (0.49 ms)").
- The roll of a line (who its soldiers are) in the same frame as its first soldier's making: 0.23 ms.
  `PrepareGroup` kind 3, called by `DrawUpLine` a frame before the first `BattleMusterLine`; it also takes the player
  out of the two battle sides. After both: the first soldier's call 1.07 ms with the game's 1.00.

**Not done.** What is left over the half millisecond on a big list is the game's own making of a person. The one lever
for it is making soldiers ahead of the battle in frames nobody watches (a loading screen, an open menu) and keeping
them switched off until the muster: not built, because it puts people into the save who belong to no battle yet. A
save in the frame between a soldier's making and their settling leaves that soldier switched off and unknown to the
script after the load; the muster had that gap before (soldiers are known to the script only after
`RememberBattleSoldiers`).

## 3. Muster, the hail, the charge

- The lines stand (`SetDontMove`, no aggression) while the player can still choose.
- A captain of the nearer line calls to a player who comes within 1,500 units, in his own words (a SkyrimNet
  narration), and a choice opens: stand with one side, the other, or stay out of it. Turning a side down costs a
  little standing with it. `JoinBattle` in conversation does the same by talk.
- A captain does not call over somebody his army knows as an officer of the other one (the third rank and up with
  the enemy, and higher than with his own: `KnownOfficerOf`). He shouts at them instead, once (`warn_off`), and
  nothing begins by it. Going over to the other side stays the player's own choice in their own captain's window.
  (Played 2026-11-21: a Captain of the Legion walking to their line was greeted by the Stormcloak captain as a
  stranger whose blade was welcome.)
- The charge comes 45 seconds after the player has watched the lines (150 at most; 15 and 60 for a skirmish).
  From the rank of Captain the line waits for the player's word.

## 4. The fight

- A soldier strikes at 0.35 of the game's damage and stands 1.75 times as much: a line holds for two to three
  minutes instead of one.
- Each army has reserves of its own (waves of 8, 5, 4, 3, 2). A wave comes up from behind its own line, from a
  walkable point the player cannot see (`ReservePoints`); when there is no such point nobody comes.
- Nobody stands about while the battle is on: at every look the DLL names up to six soldiers who are not fighting,
  each with the enemy nearest to them, and the script sets them on that enemy (`BattleIdleHands`,
  `RouseIdleHands`). Everyone who came up after the lines had met was sent to the middle of the field, one spot for
  all: with the fighting moved on they stood there in a knot, whole and idle, while their side lost (the user,
  2026-11-21: "herded in one spot like chicken"). The soldiers who keep by the player are left to the player.
- Those soldiers follow on the mod's follow package (`IntelEngine_FollowPlayerTaskPackage`), a few steps off. The
  travel packages lead to the very spot their target stands on (radius 0): on those they walked into the player
  for as long as the battle lasted.
- Morale: a soldier lost costs 5, a captain 15, a champion 20; a side breaks at 25. The player's coming is worth
  10 to the side they join, over and above a full heart (`kHeartMost` 110; capped at 100 it was worth nothing to
  a side joined before the fight), and costs the other 5.
- Both armies breaking between one look and the next (the looks are three seconds apart) is a battle fought out:
  the side with more men standing keeps the field, with as many each the stouter heart, and only with both the
  same is it drawn (`Battlefield::BrokeTogether`). Seven against five used to be called a standstill.
- A battle that has gone on for ten minutes, or that the player has left behind (9,000 units for 25 seconds), is
  decided as it stands (`Battlefield::Verdict`, which wants a clear lead).
- The log says how it stood when it was decided: "Battle 1 is decided (ImperialFaction_victory): ImperialFaction
  heart 45, 6 standing, wave 3; StormcloakFaction heart 25, 7 standing, wave 2".
- The two battle sides are allied to themselves in the plugin file, and the soldiers of the player's side ignore
  friendly hits (`TrustOwnSide`): a stray blow in the press is no assault. Before, one such blow turned the
  soldier struck and his line on the player (335 times in three minutes, played with a heavy hand).
- What happens in a battle is not the hold's business: what the player owes a hold for violent crime is noted when
  they take a side, and what comes on top during the fight and for four minutes after is struck out again; guards
  who came for it stand down (`StrikeBattleBounty`). Older debts, theft and trespass are untouched. Cutting down
  one's own still costs renown and standing, and is told.
- Kills are credited by the game's own record of who killed whom (`myKiller`; CommonLib's `GetKiller()` returns
  nothing for someone dead).

### What a battle may have in store (`Battlefield::Plan`, never in a skirmish)

| | chance | what happens |
|---|---|---|
| A champion | 28% one side, 28% the other, 12% both | After 40 to 75 seconds of fighting a champion comes up from behind the line: three times a soldier's hardiness, nearly twice a soldier's force (0.6 of the game's damage; at full damage one champion cut down twelve in a minute and the battle was over in 69 seconds), named "Champion of the ...". Against the player's side, he goes for the player. Cutting one down is worth 200 gold and 12 renown; his fall costs his side 20 morale. |
| A hidden force | 30% | After 30 to 70 seconds four more of one side break cover out to one side of the other's line. |
| Turncoats | 16% | When a side's morale is down to 70 a third of its living (three at most) change sides, and it loses 10 more. They are called "Turncoat" from then on: they still wear their old side's armour. |

A hidden force and turncoats in one battle leave the champions out. What is in store is never shown anywhere
(not on the dashboard either): it is seen when it happens. `DebugTwists` forces it for a test.

## 5. Afterwards

- The beaten run, the winners walk off, the bodies lie for six game hours. Each army goes toward a town, by the
  roads (`Battlefield::MarchTo`, `BattleManager::WayHome`): its home (the faction's `hold`) if that is at least
  16,000 units off, else the nearest town that is; the beaten never through the army that beat them. The old
  point 14,000 units off in a straight line is kept for where there is no town (another world): near Honningbrew
  it lay in the mountain and nobody took a step.
- Their travel orders are the battle's own. Core's sweep of stale packages asks `IntelEngine_Battle.HoldsActor`;
  it used to release every soldier it saw on two rounds, who then stood where they were
  (`RunPackageOwnershipCheck.py` holds every script that hands out travel packages to this).
- Nobody is given the travel package they are already on (the game does not begin it anew): whoever comes up,
  comes at a jog; whoever leaves, at a walk or a run (`PaceFor`).
- Someone of the player's side who is still standing comes over (from anywhere on the field), says what he
  thinks of it in his own words, and hands over the pay: `Battlefield::Pay`, raised by rank. Nothing is paid before
  he has spoken. He stops a few steps off, turns to the player and stands while he speaks. (His order to come to
  the player used to stay on him until he had spoken, and the words take their time: he walked into the player
  again and again. The user, 2026-11-21.)
- He comes the way a companion follows: on `IntelEngine_FollowPlayerTaskPackage`, which has no place to get to. On
  the travel order, with the player as its place, the order did not take for somebody fresh out of the fight: on
  the full Isekai list the Legion's captain walked on with the march, past the player and off toward Riverwood,
  twice in three battles, and a battle won paid nothing. If he does not turn within four seconds he is told again
  another way (the travel order at a run; then with his AI switched off and on and told to follow; then the other
  pace), and the log says each step (`Battle: ... is to come over`, `comes over`, `has not taken`). The time he is
  given is by the clock and as long as the way: 25 s and a second for every 60 units, at most 110.
- The pay of a battle won is never lost: when nobody of the player's side is left, none is near, or the one who set
  out did not get there, it is sent on and the player is told (`PaySentOn`, `BattleSay("pay_sent")`).
- Somebody who was sent off and stands (`StandingAbout`, every look of the aftermath): told again after two looks
  without a step, again another way after four, with a step aside if nobody is looking (one caught on a fence's
  corner walked off as soon as he was brushed against), and after five he rests where he is on the sandbox
  package. Seen on the full list: a beaten Stormcloak in a farm's fenced cabbage patch, on his order to run and not
  moving; three soldiers standing 3,700 to 4,700 units off at two and at five minutes.
- Only what has left the loaded area is taken out of the game. A soldier or a body the game still holds, however
  far off and unseen, is left alone: switching a loaded body off and deleting it takes its skeleton apart while
  other code works on it. On the full list that ended in a crash inside the hair-and-cloth physics
  (`hdt::ActorManager::setSkeletonsActive`, walking the skeleton of a male soldier) eighteen seconds after a soldier
  2,500 units off had been removed. It had been done from 7,000 units since 3.8.1. The walkers leave the loaded
  area on their own way to a town; one who cannot get away stays, resting, until the player has gone.
- Standing: the side fought for, the side fought against, soldiers of a side the player was not at war with,
  a side turned down, walking away after taking a side.
- The battle is recorded once, as it was fought, and word of it starts in the hold it was fought in. The player's
  part is in the telling ("...was seen fighting for the Imperial Legion, and reportedly killed 7 of the other
  side, their champion among them").

### The verdict holds (2026-11-22)

The user, watching the battles of that night on the full list: "some stormcloak soldiers still spawned and kept
fighting and actually killed lots of imperials after imperials won, is that in purpose or was something off?" It was
off. `DisperseSoldiers` calmed each soldier in turn (`CalmSoldier`: aggression 0, `StopCombat`, weapon away) while the
two battle sides stayed enemies until the battle was wound up three minutes later. On the full list the round takes
five to eight seconds for fifteen soldiers; whoever had not been reached yet went on striking those who had, who then
defended themselves, and `StandingAbout` leaves anybody in a fight alone. In the battle watched the winners' man was
"fighting TRUE" for 37 s after the verdict and never reached the player; the beaten side had seven or eight standing,
five of them a wave that had come up half a minute before.

- `CeaseFire(amongThemselves)` (script): `Intel_BattleSideA.SetAlly(Intel_BattleSideB)`, then native
  `BattleCeaseFire(first, 2, amongThemselves)` round the battle's soldiers, two a frame (`BattleManager::CeaseFire`:
  aggression 0, `SetBeenAttacked(false)`, `StopCombat()`; of the running battle, or of the one just closed by its
  cleanup list). `amongThemselves`: only somebody whose fight is with another soldier of the battle.
- Where: `SetAlly` in the first line of `HandleBattleEnd`; `CeaseFire(false)` at the top of `DisperseSoldiers`
  (every way a battle ends goes through it), `CeaseFire(true)` after its round of orders and before every look of
  the aftermath. `StartBattle` makes the sides enemies again (`SetEnemy`, as before).
- `SendToTheFight`: soldiers whose group was still being made when the battle closed stay switched off (the
  aftermath's next look takes them out: `OutOfSight` answers 1 for somebody without a body). They used to fade in and
  jog to the middle of the field after the verdict.

Played twice on the full list: the fights called off 1.0 to 1.7 s after the verdict (most of that is the script
reaching `DisperseSoldiers`: the count of the dead, the books, the journal), no line "they were still at each other",
the winners' man "fighting False", his word after 4.5 and 10.7 s, the pay handed over, nobody killed afterwards.
Three times on the test profile (0.3 to 0.6 s). Not played: a battle the player fights on in after the verdict (the
sides are friends now: blows at the beaten are blows at friends).

**The guards.** On the test profile the winners' man still showed "fighting TRUE" for twenty seconds after the
verdict, and once never reached the player. A look of the aftermath that finds a soldier fighting somebody who is not
of the battle now says whom ("... is still fighting Whiterun Guard (0001B5C2), who is not of the battle: left to
it"), and that showed it: two Whiterun guards by the road, six soldiers of both armies. `GuardsStandDown()` (in
`SuppressBountyTick`, which runs every round of a battle and of its aftermath): for every soldier of the battle whose
fight is with a guard who is in neither battle side, both forget the blow and their fight ends (`SetBeenAttacked(false)`,
`StopCombat()`). Looked for from the soldiers' side, so it costs a lookup a soldier. Seen by setting a guard and the
Imperial captain on each other in the aftermath (console `startcombat` both ways): "Whiterun Guard (0001B5E3) and
Imperial Legion Captain (FF000ECB) let each other be", both out of the fight within the round. Not seen: a guard
drawn in by himself with the rule in place (none came into the one battle played after it). A fight with a wolf or a
bandit is left alone, and a fight with nobody left in it is ended.

## 6. The ladder (`Ranks.h`, co-save record `IERK`)

What the player does for a faction earns renown with it (`Ranks::Worth`): a battle fought 6, won 6 more, each enemy
cut down 1, a captain 6, a champion 12, a leader's life saved from an assassin 10, a service done 8. Walking away
from a battle after taking a side costs 15, striking one's own side 4. Renown gives a rank; what the ranks are called
depends on what kind of body the faction is.

| renown | an army | a guild | a house |
|---|---|---|---|
| 10 | Auxiliary | Associate | Hired Blade |
| 30 | Sworn Sword | Trusted Hand | Retainer |
| 70 | Veteran | Veteran | Trusted Retainer |
| 130 | Captain | Lieutenant | Sworn Captain |
| 220 | Champion | Right Hand | Champion of the House |

What a rank gives:

- **From the first rank: the call to the line** (section 7).
- Pay after a battle won: a quarter more with each rank.
- From the second rank two soldiers keep by the player in a battle, from the fourth four.
- From the fourth rank the line waits for the player's word to charge.
- From the third rank the name is known beyond the faction: `political_state.json` carries `player_known`, and
  every NPC's political awareness (prompt 0810) says what the player is talked of as. The faction's own people
  know the whole record (`known_as`: "a Sworn Sword of the Imperial Legion: fought in 3 of their battles (2 won),
  cut down 1 champion of their enemies").
- A faction that counts the player an enemy honours no rank; the renown stays.

## 7. The call to the line

When armies gather and the player holds a rank with one of them, that faction calls its own: a journal entry
"Called to the Line" with the objective "Stand with the Imperial Legion near Rorikstead before the battle is
decided" and the place marked on the map (`BattleManager::Call`, `IntelEngine_Battle.psc` `TendCall`, the quest
`IntelEngine_Call` built by `esp/EspBuilder`). With a rank on both sides the one the player stands higher with
calls; with equal ranks neither does, and the choice is theirs.

The journal says how it went: stood in the line, stood with the other side, came and took no side, walked away
before it was over, the battle was decided without them, or the armies parted with nothing decided (their war
ended first). Not coming costs nothing but the entry.

Somebody who is already close by when the armies gather is called when they are drawn up (`CallAtMuster`), and the
journal points at their side's commander.

## 8. On the dashboard

- **Politics, "Your Name":** each faction's view of the player: standing, rank on its ladder, renown and how
  much is missing to the next rank, what the rank gives and what the next one adds, the record, and the sentence
  its people know them by.
- **Politics, "In the Field":** a battle being fought (drawn up or fighting, each side's morale and how many
  stand, the player's side) and armies gathering (where, how long before it is decided without the player,
  whether the player is called). Nothing of what a battle has in store.
- **Politics, "Chronicle":** everything that happened between the factions in one list, newest first (the last 24).
- **World:** the talk of the day the player has heard, the stories and quests they have taken on, what people
  have asked of them, and those who follow them or once did. What has not reached the player stays hidden unless
  "Show everything (spoilers)" is switched on.

## 9. Test hooks (`IntelEngine_Battle.psc`)

`StartBattleImmediate(a, b, 0)`, `DebugSkirmish(a, b, size)`, `DebugTwists(champion, flank, turncoat)`,
`DebugDidFor(faction, times)` (a service done: 8 renown each), `DebugGathering(place, a, b)`,
`DebugAssassination(targetFaction, attackerFaction)`.
