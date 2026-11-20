# Rumors: the talk of the day

Written 2026-11-20. Code: `SKSE/src/Rumors.h` (the rules, pure logic), `SKSE/src/RumorBridge.h/.cpp` (the game's
side), `SKSE/tests/RumorsCheck.cpp`. Scripts: `IntelEngine_Arcs.psc` (`BringUpRumor`, `OnArrived`). Prompt block:
`submodules/character_bio/0204_intel_rumors.prompt`.

## What was wrong before

The user (2026-11-20): "rumors overhaul to make rumors much more immersive and maybe utilize SkyrimNet memories".

Until now a rumor was a line of world knowledge in SkyrimNet for one hold, written when a quest, a story or a
follower's life produced one. It stayed there, it never travelled, nothing was ever forgotten, nobody was the one who
tells such things, and the player heard of it only when a conversation happened to turn to it. A stranger with trouble
who was not standing beside the player was never heard of at all: the story was simply not told.

## Two things with one name

- **The hold's long memory** is unchanged: the entries quests, stories and lives write into SkyrimNet's world
  knowledge for their hold, found when talk turns to them, withdrawn after a load when they belong to another timeline.
- **The talk of the day** is new: a small ledger kept with the save (co-save record `IERU`, at most 40 rumors). Everything
  the long memory gets is noted there too, and it is the ledger that makes it *talked of*: it travels, it is brought up,
  it fades.

## A rumor

`Rumors::Rumor`: its words (hearsay, past tense, one or two sentences), the hold it began in, who it is about, who to
go to (`lead`), what owns it (a story, a quest, a life, a storyline), when it began, its weight (0 to 1), and for each
of the nine holds whether it has reached it and when.

Kinds, and how long they are talked of before nobody mentions them (`LifeDays`: base days times 0.6 + weight):

| Kind | What | Base |
|---|---|---|
| Trouble | someone wants help; the rumor that starts a story | 5 |
| Deed | something that was done (a story's or a quest's ending) | 9 |
| Life | where a former follower's life has taken them | 7 |
| News | what the world did (politics, a war) | 11 |

## Travelling

Once a game hour (`Ledger::Tick`, called from the look that runs anyway) each rumor may cross into one neighbouring
hold per game day, along the roads (`kBorders`, checked symmetric by the test). How far it goes is its weight
(`Carries`): under 0.55 it never leaves its hold, 0.55 crosses one border, 0.75 three, 0.9 every hold. Which border it
takes is decided by a hash of its id and the day, so a save always plays out the same way.

In a hold it only reached by travelling, people say so: the entry written for that hold ends "(So say travellers come
from Whiterun; nobody here saw it.)". Each hold's entry is a SkyrimNet world knowledge line with the condition
`get_intelengine_hold(actorUUID) == "<hold>"` and never for the people it is about. When the rumor is forgotten, or its
owner takes it back, every entry is removed again. After a load, entries of rumors the loaded save does not know are
withdrawn (`Reconcile`, by their label "IntelEngine rumor #id@hold"): seen in the test game after a reload, "1 rumor(s)
of other timelines withdrawn".

## Who tells it

`RumorBridge::Lines` (the decorator `get_intelengine_rumors`, in everyone's prompt):

- **Those who hear everything** get up to four things: innkeepers, their servers, bards and merchants, by the game's own
  job factions (`JobInnkeeperFaction`, `JobInnServer`, `JobBardFaction`, `JobMerchantFaction`), so any mod's innkeeper
  counts and no name list is kept. "Asked for news or rumors, Hulda passes on what fits, one thing at a time and in their
  own words; unasked, only when the talk comes near it. It is hearsay: Hulda saw none of it."
- **Anyone else** knows the one thing everybody is talking about: trouble, or something with weight that is not yet old.
- The one a rumor is about, and the one it points to, do not get it as hearsay: they know their own part from their
  story's lines.

## Brought up when you walk in

When the player arrives in an inn (the director's arrival seam 2), whoever keeps the place (`RumorBridge::Publican`: the
innkeeper, else a server or the bard, awake, at peace, in the same room) brings up the one thing most worth a word
(`Ledger::ToBringUp`): trouble first, else something new with weight. Once in six game hours at most, after the player
has had seven seconds to come in, never in a fight or a menu, never the same thing twice. They say it themselves, as a
narration SkyrimNet turns into their own words. Played 2026-11-20, Hulda: "Since you're loitering, you may as well hear
this. Anoriath the hunter's been asking about for someone not afraid to go look at whatever was found by the old cave in
the hills. He's at the Drunken Huntsman. Might be work in it for a man with an iron axe and no coin for ale."

The teller remembers having told it (a SkyrimNet memory of their own, "I passed on to <name> what was going round: ...",
tagged `intelengine_rumor`), so they can come back to it later.

## The rumor that starts a story

A stranger with trouble does not go looking for someone they have never heard of (the user's rule). Until now that
meant a stranger elsewhere in town had no story at all. Now their trouble becomes talk:

1. The director's pacing picks someone of the town. If they do not know the player and are not right there, the tale is
   asked for as *rumored*: the writer also writes `offer.rumor`, what the town says of it ("Anoriath the hunter has been
   asking for someone who is not afraid to go and look at what was found out by Guldun Rock.").
2. The engine adds where the asker lives when the writer did not name it (a fact of the game, from where their bed is),
   and the rumor goes round the hold as Trouble with the asker as its lead.
3. Nobody comes to the player. The story waits (the same state as a story whose asker could not reach the player).
4. The player hears it (the innkeeper brings it up; anyone may mention it) and goes there. When the player is near the
   asker the offer goes out with the usual manners: not at a bad moment, not within the hour of another story's beat,
   not while someone else of the story stands in earshot (the log says which: "Anoriath is near the player and waits
   with the offer: Olava the Feeble of the story stands within earshot").
5. If the player walks up and **speaks to them**, it is the player who began it: the asker tells their trouble there and
   then, whoever stands near, and the offer follows ("the player came to Anoriath and spoke: the offer goes out").
   They already know their own trouble, so even the first answer is right: "Aye, it's Hjorlund, my old guide. Found
   dead by Graywinter Watch six days past, and nobody in Whiterun will say what killed him."
6. Taken on, turned down, dropped or lapsed, the Trouble rumor is taken back ("#1 is taken back"). The ending of the
   story is noted as a Deed, as before, and travels by its weight.

If no model can write the tale, nothing goes round and nobody comes instead.

## What feeds the ledger

| From | Kind, weight | Notes |
|---|---|---|
| A rumored tale's `offer.rumor` | Trouble 0.65 | the only one whose SkyrimNet entry the ledger writes itself |
| A story's ending (`ArcBridge::Tell`) | Deed 0.6 | replaces the story's earlier word |
| A quest taken on (`QuestBridge::Spread`) | Deed 0.45 | under the bring-up line: the player is on it already |
| A quest's ending (`QuestBridge::Ended`) | Deed 0.6 | replaces the open one |
| A storyline's deed (`StorylineBridge::Apply`) | Deed 0.45 | |
| A former follower's life (`LivingWorldLives`) | Life 0.4 | each step replaces the last |

Politics feeds it too (News, by the kind of event: a war declared or ended 0.95, a battle 0.8, an alliance or an
attempt on someone's life 0.75, trade and quarrels 0.5, spying 0.45): `FactionPolitics::News`, see
`docs/POLITICS_AND_FACTIONS.md`.

## Cost

Nothing here runs per frame. The hourly move of at most 40 rumors is inside the two-second look and is timed where it
runs ("Rumors: a look at the talk of the day": the slowest of every 150 runs is logged, any run of half a millisecond
or more at once). Writing to SkyrimNet happens on a thread of its own. The prompt block is rendered on SkyrimNet's
threads. The search for the publican runs once, when the player has walked into an inn.

## Not yet

- A page to see and delete what is going round (the dashboard).
- Rumors that are wrong on purpose (a Trouble whose talk in town blames the wrong thing is already possible inside a
  hunt: the naming step).
