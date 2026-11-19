# Story arcs

The user, 2026-11-18: "make the player get lost in a living world and offer full entertainment, good story arcs
involving crime, romance, drama, action, brawls, battles, politics, secrets, murder-mysteries (for generic npcs)
where you can find evidence in someone's pocket or home", "like in a great movie with very good pacing, good amounts
of entertainment without making it too spammy or uncoordinated", "more boring days where things happen slower,
while there can be days with more interesting events", "if the dungeon master notices the player has been idling
around town for hours, then it can think of a story", "build enough tools for the dungeon master", "there should
still be events where followers or npcs you have a good bond with might seek the player out and visit him at home",
"accepting the followers to join you after meeting them might turn out to be a longer quest than expected with a
very deep story", "be careful of using game thread and causing stuttering", "test as many edge cases as possible
that might come up from a player's perspective", "no mistakes".

This document is the plan of record. Research behind it: `docs/research/PACING.md` (story directors, mysteries,
procedural drama; sources cited there) and the crime and engine findings in section 9.

## Status (2026-11-18)

Built and played in game (Whiterun, test profile): the engine (`SKSE/src/Arcs.h`, `ArcBridge.*`,
`IntelEngine_Arcs.psc`, journal quests IntelEngine_Arc0..3), the three first patterns (thief, feud, murder) through
their branches, the engine's own pacing (a town's story when the player lingers, a day apart, rest for askers),
the director's quest moments trying an arc first, and rumor reconciliation after loads (arcs, quests, lives).
Not yet: more patterns (romance, politics, debts, longer ventures), visits at the player's home, the tension meter
beyond the simple cadence, the battle bounty fix, a run on the full profile and the Albanian profiles.

## 0. Tales: stories the dungeon master composes (2026-11-19)

The user, 2026-11-18 night, after playing the three patterns: "the stories are too shallow and risk getting too
repetitive", "Hulda doesnt fit to a brawl", "we use different types to combine a full story", "it should never
contradict main quests. If someone is an enemy with someone else due to the main story, that should also reflect",
"pace events right, so that the player doesn't get interrupted while doing main quests", "Everything should feel ...
like it belongs to the role of the chosen story candidates", "based on how the dialogue flows, they should adapt to
different branching so the dungeon master can predict possible outcomes and prepare quest nodes in advance", "I don't
want a 'bad blood' quest, or a murder mystery quest repeated dozens of times", "procedurally engineering stories
logic, where the possibilities are almost infinite".

So a story is no longer one of three fixed patterns. It is a **tale**: a graph of steps the dungeon master writes for
the real people of a town, from a library of step kinds the engine can carry out and check. The three patterns stay
as the fallback when no model answers.

1. **Who may be in it** (code). The townsfolk of the player's town, each with what the game says about them: job and
   class, temperament (the game's morality, confidence and aggression), whether they can fight with fists, be taken
   by the guard, have a home of their own, and their ties to each other and to the player (the game's relationship
   ranks, so canon rivalries and families show). Nobody a quest of the journal still needs, no children, no
   followers, nobody in another story.
2. **What it may use** (code). Step kinds: talk (the conversation decides which way it turns), find (a stamped item
   hidden in someone's home or pocket), give (hand it to someone), choose (a decision popup), brawl (only with
   someone who would), go (to someone's home), visit (someone comes to the player), wait (time passes, a day to pay
   up). Effects: pay, the player pays, taken by the guard, leaves town for a while, two people become friends or
   rivals. Endings: good, bad, and a third the author names.
3. **The author** (the model, one call). Given the town, its people, the player's current quests (never to be
   contradicted or touched), the stories already told (never repeated), and a genre to lean toward, it writes the
   tale as data: title, the hidden truth, each person's own truth (first person: what they know, want, hide, say),
   the props, the offer, the steps with every branch, and the endings (journal line, what the town hears, what each
   person remembers). Several branches per step: the outcomes are prepared before the player gets there.
4. **The check** (code). Every name, step, branch, item and effect is checked against what the engine can do and
   against each person's fit (no brawl with an innkeeper, nobody jailed who cannot be); every path must reach an
   ending; texts have limits. A tale that fails is sent back once with the reasons; then the fallback patterns.
5. **Talk that turns the story** (the model, small calls). When the player has talked with the step's person and the
   talk goes quiet, a small decision call reads that conversation and names which prepared outcome it was (or none
   yet). Popups stay for the player's own decisions; the dialogue never has to pick an action.
6. **The journal** shows the tale's own words: the title, each step's goal as its objective (texts set by the DLL),
   the story so far and the ending as log text (two holder aliases whose names the DLL writes).
7. **Pacing**: a tale is offered only to a player idling in a town, never within a while of a main, faction or DLC
   quest moving on, never in a bad moment; a day between tales; the ledger keeps the last tales so nothing repeats.

### Status of tales (2026-11-19)

Built: `SKSE/src/Tales.h` (the check and the build of a tale, `SKSE/tests/TalesCheck.cpp`), `SKSE/src/TaleAuthor.cpp`
(who may be cast and what is said of them, the request, the judge of a talk), the arc engine's graphs, kinds Visit and
Wait, conditions Said0..2 and Waited, effects Leave, Befriend, Estrange and GiveItem, ending Lost, co-save IEAR v3
(v1 and v2 load), the journal's runtime objective texts (objectives 20..31) and the Tale/Close holders, the prompts
`intel_tale_author`, `intel_tale_turn` (decision) and `intel_tale_turn_chat`, the quest gate (TESQuestStageEvent), the
linger of a step's person while the player talks with them (all arcs).

Played in game (Whiterun, test profile): written tales "The Goblet Saadia Never Owned" (mystery, 6 steps: three talks
turned by the judge from what was really said, the hand-over, the decision, the friendship of Saadia and Hulda, both
remembering it) and "The Ring in the Wrong Stall" (9 steps, first draft refused for a ring in a pocket and rewritten);
hand-written tests through `IntelEngine_Arcs.DebugTaleFile` (wait, visit, brawl won, a hand-over by the loser, the
return; a death mid-tale ending as lost); a v2 save loaded by v3; the pattern thief still running on the same engine;
the director's own pacing bringing a tale after 2.3 game hours (0.5 ms to cast); the quest gate after a main-quest
stage. Later the same night: leaving town and coming back only out of sight (Amren stayed away while the player stood
in his house), a 24-hour wait and a visitor coming from across town (retried after the story engine's watchdog
cleared the first trip), a "go" step and a prop planted in the home on entering, a talk read once per exchange (a
preacher's sermon no longer triggers a reading every two seconds), the step's person lingering while the player
talks and leaving with a line after a minute of silence, a Riverwood tale with no item at all ("The Rounds He Never
Bought") whose author was told Delphine and Gerdur belong to quests of their own. Not seen yet: the judge's chat form
(no decision provider), the Albanian profiles, the full Isekai profile, home visits (not built).

Test hooks: `IntelEngine_Arcs.DebugTale(asker)` (asks the model), `DebugTaleFile(asker, absolute path)` (a tale
written by hand; people by name; no fallback); the log has each tale as written and its outline.

### The ambush (2026-11-19)

Step `"ambush"`: someone of the tale (`"by"`, optional) has paid men to waylay the player. `Kind::Ambush`, the men in
`Arc::thug` (the game's own Hired Thugs, one below level 6, two below 12, else three), the leader's name and what he
carries (`"hidden": "ambush"`) in the arc; co-save IEAR v5.

Three ways on, all the author's: `"then"` (the men are dead), `"lost"` (required: the player is beaten down, never
killed; the men take what they came for and leave), and up to two `"outcomes"` of talk with the leader (what the men
would settle for; `"player pays N"` goes to the leader, `"give iN"` has him hand over what he carries).

How it plays, each rule found by playing it:
- Only out in the wilds (outdoors, no town, farm, inn or home), twenty seconds into the wilds and a quarter of a game
  hour into the step, never during another fight or within the quest gate.
- The men wait on the road itself (navmesh preferred triangles), on a stretch the player cannot see; they are made
  behind the player and moved there unseen. No such stretch twice running: they come after the player from behind.
  First version put them on any hidden ground, and they stood in the woods by the river.
- They always come to the player: when the player nears, after a minute of the player not coming their way, and
  whenever they are not on the player any more (a wolf drew two of them into the river once). The fight, the bodies
  and what the leader carries are where the player stands.
- The leader has the word first (a narration; SkyrimNet gives the line from what the men know: who paid, for what,
  what they would settle for). `ArcBridge::Parley` then waits: no line within 25 s (others queue ahead), no answer
  within 10 s of it, a talk that came to nothing (read by the judge once per exchange, like a talk step), the player
  walking off, or an offer the player's purse cannot cover: they fall on the player. A menu or the chat box holds the
  clock.
- While they are on the player, the player is essential (only if the tale has `"lost"`, only if nothing else already
  keeps the player alive): going down is the `lost` branch. The player comes round six seconds after the men left
  and is mortal again. Two level 6 men killed a level 1 character in twenty seconds before this.
- "Fighting" means a man's combat target is the player or a teammate; anything else is a distraction.
- The player far away, indoors or in a town: the men are taken out of the world unseen and come again six game
  hours later. Men who stood down walk back toward whoever sent them and are removed once unseen.
- The leader's body is held by the script until the tale ends and is the marker of the step that searches it.

Variety (`Tales::Pool::barred`): an ambush in any of the last three stories the player took on, a brawl in either of
the last two, or places out in the world in both of the last two, and that beat is not on offer: the prompt does not
show it and `Compile` refuses it.

### The stalker, old companions, office (2026-11-19)

Step `"stalk"` (`Kind::Stalk`, co-save v6): `"by"` (never the asker) shadows the player out of doors; `"why"` is what
makes them follow and what they dare not say. The DLL sends them (the visit's signal) only when the player is
outdoors, in no fight, and they are behind the player and not beside them; the script uses the story engine's stalker
(`DispatchArcStalk`: the stalk package, holding at 800 units). Seen by the player, or after 75 s unnoticed, they stand
up, walk over (a walk package to the player) and speak within 350 units; the step is done when they and the player
have talked. First version let them "come forward" where they crouched: Sven whispered his confession from a thousand
units off and the step never completed.

Old companions (`LivingWorldBridge::OldCompanions`): up to two former followers living a life of their own (alive,
not with the player, in no quest or story) join a tale's pool with where they are now and their life in a line;
never jailed, sent away, fought, or hiding anything. A talk with them is a journey to where they are (the marker
follows them); a visit brings them to the player. While they have a part in an open story their own life does not
move them on (`LivingWorldLives.cpp`).

Office: a jarl or steward in the pool is marked for the author (may hear, judge, advise; never guilty, never
changed).

### How far a reason carries (2026-11-19)

The user: "The cases where someone should come from very far should be too rare and only for good reason. This was
exactly my complaint, that people from afar were visiting for absurd reasons. Everything should connect to an
entertaining story arc and make sense." Before this, a "quest" moment had no distance rule at all and its asker came
in person from anywhere.

`StoryDirectorPick.h MayCome` (pure, `StoryDirectorCheck.cpp Distance`), fed per candidate with `reach` (0 loaded or
of the player's settlement, 1 the same hold, 2 another hold or unknown: `NPCIndex::ReachOf`), `bonded`
(`LivingWorldBridge::CloseToPlayer`: once a follower, a confidant or more in the game's relationships, wed to the
player, a Confidant in SkyrimNet Relationships read softly) and `farRested` (no arc flagged `Flag::CameFar` in the
last ten days):
- here: as before;
- the same hold: pressing, or real when bonded; a mild reason's gentle moment only from someone here;
- another hold: bonded and pressing, or urgent with the probabilities agreeing (4.5); never ambush or stalker; a road
  meeting only with someone bonded; never when someone came from afar within ten days.
A far candidate who fails is passed over, so someone here with a real reason acts instead. The decision prompt is
told the distance. Kinds quest, seek_player, message and informant become a tale seeded with the candidate's reason;
when no tale can be written for someone who is not here, nothing happens (never the old one-off visit).

A far-comer's tale is flagged `CameFar`; its offer goes out eight game hours after planning (`ArcBridge::Lapse`,
nothing reserved meanwhile; after a load the road counts from the planning), retried hourly when the moment is wrong.
The seek_player hold policy of the settings is asked before the tale is requested (Lod of Falkreath had a story
written and was then turned back on the road by it). Inside a tale, a visit or stalk step by someone of the tale's
town waits while the player is outside the tale's hold ("does not come after the player across Skyrim"); an old
companion, who is of no town, may come from wherever their life has them.

Test hook: `IntelEngine_Arcs.DebugReach(actor)` logs how the rule sees someone. Played: Hulda here, Faendal in the
hold, Faendal from Falkreath in another hold; Sven's visit waiting while the player stood in Falkreath and going out on
the return to Riverwood; Lod refused before writing; Bolund (not a civilian) on the road eight hours, then arriving
with his offer in the Bannered Mare; the ten-day flag set.

Tests are walked, not teleported: `IntelEngine_Quests.DebugWalkTo(ref, run)` moves the player by the game's own
pathfinding (`Scripts/intelengine/ambush_setup.sh`, `ambush_meet.sh`).

### How someone comes, and how they go (2026-11-19)

The user: "it wouldn't make sense if a faendal or sven who neither have heard anything about the player, nor have met
him, to just run to him for some errand. Maybe the running should be reduced to walking so it looks less targeted and
more natural. If the player disappears before they can reach him, then they defer until next chance to meet him.
Running into the player is only by people who have genuinely been looking for him, missed him or who want to kill
him." Later the same day, watching a test: "When declining Faendal's quest, he doesn't walk away. Sven came just
moments after Faendal ... while Faendal was standing beside me all the time. The pacing isn't quite right."

**At a walk.** `StoryEngine.StoryComesAtAWalk` (seek_player, informant, message, quest, road_encounter): the visitor
gets `TravelPackage_Walk` unless `ActiveStoryHurries`, which is `IntelEngine.ArcHurries(arc, npc)`: someone close to
the player (`LivingWorldBridge::CloseToPlayer`) or an asker who came from another hold (`Flag::CameFar`). Having missed
the player once is no reason to run the next time. Attackers keep their own packages.

**Letting it be.** A walking visitor does not follow the player across the world: `DeferStory` when the player is in
another place (`ArcReachOf >= 1`) or walked off faster than they follow (2500 units beyond the nearest they got, and
more than 3500 away). `Core.HandleOffScreenTravel` asks `StoryEngine.LetsItBe` before it would set a visitor down near
the player (Faendal of Riverwood stood in the Bannered Mare a minute and a half after the player had left). The arc is
flagged `Deferred`, keeps three more days, and `ArcBridge::Lapse` sends the offer again when the asker is loaded, in the
same space, within 3000 units, at least a minute later.

**Strangers.** `ArcBridge::KnowsPlayer`: 2 met (SkyrimNet's memory of a meeting, a talk counted by the dialogue tracker,
close to the player, a friend in the game's relationships), 1 the town heard of the player (an ended story with a
rumour there), 0 stranger. The pacer asks those who know the player first; a stranger only when they are loaded within
2500 units of the player in the same space ("a stranger with trouble takes a chance on a stranger who is right
there"). The author is told `asker_knows` and writes the offer accordingly.

**Never the game's own quests again.** The user: "Dungeon master written quests should never copy original game quests
but should branch of them or even create new stories based on the knowledge, but never give the same letter via
vanilla quest and then dungeon master quest to camilla." The catalogue keeps each person's named small quests
(`m_favours`: miscellaneous quests that name at most six people). The author gets per person `quest` (not finished,
"(begun)" only when one of its objectives has been in the player's journal: the game starts many quests with the
world), `quest_done` (finished: a story may grow out of how it ended) and `favours` (never retold), with the rule
"Never retell a quest of the game". Seen: Sven, Faendal and Camilla listed with "A Lovely Letter", Camilla and Lucan
with "The Golden Claw"; the tale written was "The Bard Who Sold the Wrong Song".

**Taking their leave.** A story visitor used to stand beside the player for the rest of the visit (three minutes, more
while anyone talked) whatever the answer had been. Now `IntelEngine_Arcs.OnAskerArrived`, once the answer is given
and the asker has said their word to it, calls `StoryEngine.VisitOver`: the linger ends as soon as `TalkOver` holds
(no line of theirs pending, their last one 8 s ago, the player silent for 16 s; by SkyrimNet's account of speech, not
by the dialogue target, which many people carry for minutes), and `Core.WalkBack` sends them at a walk to the spot
they set out from (`RememberWhereFrom` moves their own marker there at dispatch), or home when that spot is beside the
player; tracked like every walk home (`CheckWalkHomeActors`). An asker the first step is with stays. A tale's visitor
leaves the same way when their step is done. The walk's package comes off the moment they are within 400 units of
that spot: the story engine keeps its 3 s tick while anyone is on such a walk (`Core.WalkHomePending`). Before, the
check ran with the hourly sweep only, and Faendal stood rooted at his spot for two and a half minutes.

**A quiet stretch.** `Arcs.h`: `Book::LastBeat` (the last offer made or answered, or ending; a plan dropped unseen is
no beat) and `QuietFor`. No tale is requested and no pattern planned within `kQuietHours` (6) of it, whoever asks and
why (the director's reasons and the test hooks too; before, only the pacer kept a day between its own stories). An
offer already owed (deferred, or on the road from afar) waits out `kQuietHoursOwed` (1).

**Not in front of the people it is about.** `ArcBridge::Company(id)`: someone of the story other than its asker who
stands within earshot of the player (followers are not counted). Earshot is SkyrimNet's own interaction distance
(`interaction.maxDistance`, 1600 units by default; `IntelEngine_Arcs.Watch` reads it about once a minute and tells the
DLL, `ArcEarshot`, never less than 500 for whisper mode): whoever is within it hears a line and may answer. A first
guess of 500 units was far too small: Alvor, 900 units off at his forge, answered Faendal's talk about his arrowheads
with a full confession before the player had asked him anything. With such a person there the offer is not sent
(`SendOffer`, the deferred and far-comer loops); a visitor already on the way turns back when within 1400 units
(`StoryEngine.HeldBackByCompany`, also asked at the moment of arrival), walks back, and the story waits until the two
are apart (`ArcOfferHeldBack`; a far-comer tries again within the hour).

Long texts are cut where they read whole (`Tales.h Words`): at the last full sentence, else the last clause, else a
word with "...". An asker's plea in the offer box read "...because I cannot go near him... Reward: 155 gold".

**The reward changes hands in person.** A tale whose paying ending is reached away from its asker used to end there,
and "Faendal gave you 60 gold" came up with Faendal four thousand units away. The compiler (`Tales.h`, after the
reachability pass) now writes one step more for each such ending: back to the asker, journal objective "Tell <asker>
how it ended", marker on them (`A::IsWayBack`: a talk with the asker that prepares no outcomes). Its edges: told
(with the asker and a recent exchange) = the ending the story had reached, paid; the asker dead = that ending, unpaid
(not "the asker died": the story was over); 72 game hours without going back (`If::Waited`) = that ending, unpaid.
Entering it gives the arc four more days. A step with the asker (talk, give, visit, choose) pays on the spot as
before. `"effect": "pay"` on a branch that goes on to another step away from the asker is refused with the reason
(the author gets its one retry); twelve steps and a way back do not fit, and the error says how many do.

**Truths say the thing itself.** A hand-written test tale gave Sven the truth "I will tell {player} what I did when I
find the nerve"; the dialogue model filled "what I did" with the forged letter of the game's own quest. The author's
prompt now asks for the deed itself in every truth ("what was done, with what, to whom, when"), because whoever
speaks for a person fills a gap with the nearest thing known of them.

Played on foot, 2026-11-19 (three runs in Riverwood, hand-written tales, the test character walked by the game's
pathfinding): Faendal comes at 77 units a second at every approach; refused, he says his word, leaves 31 to 33 s
after the answer and walks back to the spot he set out from, where his day goes on at once; accepted, the same after
29 s; a second story asked for right after is refused four ways (model-written, with a reason, from a file, a
pattern); with Sven beside the player in the inn the offer about Sven is not sent and stays unsent while Faendal is
at home, and goes out the next morning when the two meet out of Sven's hearing; with Alvor in the cast, Faendal sets
out, the player goes to stand with Alvor, and Faendal turns round 1300 units out and walks back, then comes again
once the player stands apart; Sven comes for his visit step and leaves 23 s after the tale ends; "Tell Faendal how it
ended" with the marker on him, and the gold handed over at two steps' distance after being told. Offline:
`ArcsCheck` (the quiet stretch), `TalesCheck` (`PaidInPerson`, `LongWords`), `RunArrivalDeliveryBytecodeCheck`
("visit over": stays while anyone speaks, then walks back; nowhere to go = stays). Not seen in game: the 72 hours
without going back, the asker dying on the way back (both offline only), whisper mode's shorter earshot.

### Hearts: stories between someone and the player (2026-11-19, built, not yet played)

The user's vision: stories that lead to "pursuing romance, marriage and having kids (if fertility mode installed,
preferably with ostim), affairs (if ostim is installed)". Until now "romance" was only a genre among townsfolk.

**IntelEngine owns none of that state and writes none of it.** A relationship mod keeps it, or the game does.
`HeartBridge.cpp` reads it softly (a mod that is not installed reads as nothing):

| What | Where it is read |
|---|---|
| How close (tier 0 to 5) | SkyrimNet Relationships' bond faction rank when they are tracked; else the game's own relationship rank |
| Something of the heart on their side ("sparked"), the player's answer to it | SkyrimNet Relationships' script storage, read by `IntelEngine_Arcs.TellHearts` (four people a look) and told to the DLL (`ArcHeart`). Without that mod: someone the game itself lets the player marry (PotentialMarriageFaction) who is at least a friend, while the player wears an Amulet of Mara (the game's own sign of looking for someone: nobody is courted who did not ask to be) |
| Betrothed, wed, ended, widowed | SkyrimNet Relationships' marriage faction; the game's PlayerMarriedFaction |
| Married to someone else | the game's Spouse association |
| With child, whose | Fertility Mode as SeverActions mirrors it into script storage |
| Children with the player | Kinship's script storage |

**Who may be part of one** is decided by `Hearts.h` from those facts, never by the model. Hard rules, whatever is
installed: nobody who is a child or looks or sounds like one (the game's flag, the race, a "Child" voice, under 0.86
of a grown person's height: a follower mod's child of 0.79 with a child's voice is one); nobody of the player's own
family; nobody the player turned down; nothing that ended is reopened. `LeanOf` then says what a story between the
two would be about:

- **courtship**: something has begun (sparked) with a friend or more, answered or not;
- **a life shared**: lovers, betrothed or wed;
- **the household**: wed, with a child of the player's on the way or children already;
- **an affair**: they are married to another, the player has welcomed it, and they are already close. Only where an
  intimacy mod (OStim) is installed: the user's own condition. Never as a first step.

**When.** In `RequestTale`, when the asker's lean is not None and neither of the last two stories nor an open one was
of the heart (`Flag::Heart`), the story is about the two of them: `heart_lean` goes to the prompt, which has a section
for each of the four. Nobody is chosen as asker because of it; the pacer and the director pick as before (someone wed
to the player or close to them already ranks high there). Switched off with the living world at "Classic".

**What the author is told.** Every person of the pool with something true between them and the player carries a line
("With <player>: Married to <player>. With child (halfway); the child is <player>'s."), `Hearts::Words`, whether or
not the story is of the heart, with the rule that it is fact: the player's wife is never given another love, and
nothing of the heart is written with someone who was turned down or has no line. Someone who must not be part of any
such story has no line at all.

**What a story changes.** Two new effects, `closer pX` and `apart pX`: the game's own relationship rank between that
person and the player moves one step (`Hearts::RankAfter`, done by the script), never beyond friendship either way.
What lies beyond is the player's own doing and the relationship mod's to keep; such a mod hears of the story through
what both remember of its ending. The prompt requires a kind way to say no at more than one point, words and
gestures only, and that the story ends at the door.

Offline: `HeartsCheck` (who, what, the hard rules, the words, the rank), `TalesCheck CloserAndApart`,
`RunTaleAuthorPromptRenderCheck` (the prompt rendered with SkyrimNet's own Inja for nine kinds of request: each of
the four leans, a spouse in the cast of an ordinary story, a stranger, a reason of their own, a second attempt).
**Played once (2026-11-19 evening, Whiterun, by talk):** Ysolda as a friend with a spark (test hook `DebugHeart`,
which tells the DLL without touching any mod's records). The log: "Ysolda and the player: a story of the heart
(courtship)". The model wrote "The Price of a Stall" in one try: a fortune-teller's omen that being near the player
would cost Ysolda what she loves, bought by Anoriath for twelve septims and a haunch of venison because the brothers
of the Drunken Huntsman fear her plans for an inn. Olava and Anoriath each owned up when asked (read from the talk),
Anoriath said it to Ysolda's face in the market, the last choice offered a warm answer, time, or a no; the warm one
moved the game's rank 2 to 3 and wrote three memories and a rumour. What that run changed:
- **No gold for it.** Ysolda had promised "155 gold the moment we're done". `Tales::Pool::heart`: the compiler sets
  the reward to nothing and no ending pays, whatever the author wrote; the offer names no reward.
- **A kind no cools nothing.** The author had put `apart` on "You are a friend, nothing more". The prompt now says a
  kind no carries no effect at all; `apart` is for cruelty or carelessness only.
- Not played: the other three leans, a refusal, someone a relationship mod really tracks, Fifi or a child (who must
  get no line), a story held back because one of the last two was of the heart.

### Places out in the world, played on foot (2026-11-19 evening)

Two hand-written tales, the test character walking by the game's pathfinding and fighting with real swings
(`Scripts\intelengine\go.py`, `fight.py`): White River Watch from Riverwood (the whole way there on foot, three wolf
fights, the guards outside, the cave, the chief, the search, the hand-over), and Mzinchaleft from Dawnstar (entered
by console, then on foot; one bar gate of the ruin's own lever puzzle passed by console). Both ran from offer to paid
ending. What they showed, all fixed:
- **The marker for "take it from him" pointed at the heart of the place once the master was dead**, not at the body.
  It is on the master, alive or dead, while what is to be found is on them (`PlaceMarker`).
- **Working mines were offered as bandit hideouts.** `SiteKindOf` took the mine keyword alone: Quicksilver Mine and
  Iron-Breaker Mine in Dawnstar, Left Hand Mine, Kolskeggr, Gloombound, their "heart" a miner at work. A place now
  needs the game's own LocTypeDungeon or LocTypeClearable.
- **Many places have a heart that is no reference of the game until one is inside** (every Dwemer ruin looked at:
  Mzinchaleft, Nchuand-Zel, Kagrenzel; also Hag Rock Redoubt, Druadach Redoubt, Mara's Eye Pond, Hillgrund's Tomb and
  more). For those no master was placed at the offer and the journal had no marker: "kill what waits there" could
  never be done. Now: until the heart is in the game the journal leads to the place by its marker on the map, and
  shows none while the player is inside (a marker out of doors would point back at the way in); when the heart's
  part of the place is loaded the watch says so (signal 14), the script places the master there (not in plain view
  where that can be helped) and the marker moves onto it. Seen at Mzinchaleft: none in the first hall; on entering
  the second, "the Warden of Mzinchaleft waits at the heart of Mzinchaleft" 0.2 s after the load, the item in its
  works, the marker on the heart, then on the wreck.
- A place whose name two records carry (Gloombound Mine) was offered twice.
- Not played: a model-written story with a place in it; the walk overland to Mzinchaleft; a place the player had
  cleared before (its company coming back); a Falmer hive, a tomb, a giants' camp.

## 1. Principles

1. **Truth first.** Code decides what happens and makes it true in the game (people, items, bodies, places) before
   anyone speaks of it. The language model only writes words, from a record that says what is true and what is not
   yet true. Nothing a character says moves an arc: facts of the game do.
2. **Any language, any model.** Every branch is decided by a game fact or a choice popup. Conversation actions (the
   dialogue model choosing an action) may speed a branch up but are never the only way through: one model in the
   user's rotation rarely writes action lines.
3. **Real people, real places.** Arcs are cast from people the world already has (their relationships, factions,
   homes, what the player did with them), never from invented names, except the strangers a pattern spawns on
   purpose (a killer for hire, a gang).
4. **Movie pacing.** A director paces beats over real play time with quiet and eventful stretches, never the same
   kind of thing twice running, never a beat in a bad moment, and reacts to an idle player.
5. **No stutter.** Nothing of IntelEngine takes more than about 0.5 ms of one frame. Arc ticks gather their facts in
   one native call; scans are spread, cached at the main menu, or done when a cell loads.
6. **Never break the game.** No vanilla quest is broken: no NPC a quest needs is moved, harmed or killed; nothing
   happens in the player's sight that should not; every arc can end cleanly, also after a load, a death or a
   missing mod.
7. **Optional and gentle.** Everything heavy is behind a setting (murders, kidnappings), and the defaults are the
   immersive middle.

## 2. The arc engine

An arc is a quest of the quest book (`Quests.h`) whose steps form a graph:

- **Pattern**: authored data (C++ tables) for one kind of story: roles, props, places, nodes, edges, endings,
  journal texts (in the esp), the words each role knows at each node.
- **Cast**: up to 6 roles bound to real actors (asker, subject, culprit, suspect, witness, rival, lover, victim,
  ally...). A role may be filled by a spawned stranger where the pattern allows it.
- **Props**: up to 4 stamped items (the fingerprint already used for keepsakes: base form plus a unique display
  name), each with where it is (on someone, in someone's home container, at a place, in the player's pack).
- **Places**: up to 3 (a dungeon of the catalogue, someone's home, an inn).
- **Flags**: 32 story bits (what the player learned, chose or did) that edges may test.
- **Node**: what the player is to do now. Kind (the fact that completes it), journal objective, marker role, who
  must be present, a deadline, and outgoing edges.
- **Edge**: a condition (a fact, a choice, a flag, a time) and its effects (next node, flags, moves, spawns, a
  memory, a rumor, a relationship change, an ending, a follow-up arc).

The three existing quests (keepsake, bounty, venture) become small graphs of the same engine; `QuestsCheck` keeps
their behaviour exactly.

### Facts (all read in one native call per arc tick, 2 s)

| Fact | Read from |
|------|-----------|
| The player is at a place / with a person (450 units, same space) | positions, locations |
| A person spoke while the player was with them | SkyrimNet speech events (SpeechTracker) |
| A prop is in the player's pack / in a person's pack / still where it was put | the stamped name, by inventory count, throttled; container-changed events |
| A person is dead, and who killed them | `IsDead`, `GetKiller` |
| A place is cleared / its master is dead | location cleared flag, spawned boss |
| A brawl was won or lost | vanilla brawl quest stages, or SeverActions' brawl events |
| The player's bounty in a hold changed | crime factions |
| A choice was made | the choice popup's answer |
| An action was chosen in talk | the arc's own actions (accelerators only) |
| Time passed | game days |

### Choices

A decision point is a popup with up to 3 answers (Magelight `ShowChoice`), shown only once everyone has finished
speaking, and only when the player is with the person it concerns. Accusations name a suspect and need one piece of
evidence in the pack (section 5).

### What the characters know

Every role gets first-person lines in their bio (as the quest giver lines do now): what they know, what they want,
what is true and what is not yet true ("I have paid nothing"). A culprit's lines hold the truth and the lie they
tell; a witness holds one clue; a suspect holds an alibi. This is how the dialogue model plays an investigation
honestly without being told the plot.

## 3. Pacing: the director

Numbers are play time (real minutes outside menus) unless they say game days. Research: L4D's director, RimWorld's
storytellers, CK3, Watch Dogs Legion (see `docs/research/PACING.md`).

1. **Tension**, 0 to 100: rises with what the player lives through (a fight, a death nearby, a major beat), decays
   at rest. States Build, Peak, Fade, Relax. After a peak no new arc starts for 10 to 15 minutes or until the
   player changes hold.
2. **Rhythm**: an on/off cycle with jitter (Normal: about 40 minutes eventful, 50 quiet). One or two major beats
   per eventful stretch, at least 15 minutes apart; minor beats every 25 to 40 minutes. Calm and Lively scale it.
   This is what makes slow days and eventful days.
3. **Attention gate**: a beat waits while the player fights, talks, sleeps, is in a menu or in a vanilla quest's
   scene, and for a while after a cliffhanger. A beat whose moment passes resolves off-screen and is heard of later.
4. **Variety ledger**: each arc start is recorded (kind, lead person, place, climax verb). No kind twice running or
   within 2 game days; a person leads one arc at a time and rests 3 game days after; no new major arc in a town
   within a game day of the last one there; a new arc differs from each of the last five in 2 of the 4 fields.
5. **Idle player**: in the same town, no combat, no quest change, low tension for 40 minutes and no beat for 25:
   one ignorable local hook from people nearby (a plea, a quarrel, a rumor, a visitor). Ignored, the next comes
   twice as late; after two ignored, none until the player travels or sleeps.
6. **Concurrency**: at most 3 open arcs, 1 in its climax, 1 new start per 30 minutes; the others move by quiet
   beats.
7. **Home visits**: a friend, lover or former follower with a good bond may come to the player's home, with a
   reason from the record (missing them, a favour, news of an arc), at most one a few game days, never at night
   unless urgent.
8. **Grace**: the first 90 minutes of a new game get minor beats only.

The director already in 4.0 (seams, budget, scoring, rotation of low-bond acquaintances, staged LLM judging) is
extended, not replaced: the rhythm and tension above decide when; the existing scoring decides who; patterns decide
what.

## 4. Patterns (first library)

Each pattern has at least one real choice, a reveal, a climax the player is present for, and an aftermath that
echoes later (memories, rumors, a person who comes back). Every branch ends somewhere: done, failed, refused,
ignored, someone died.

1. **Venture, longer** (the user's ask): after a venture with a former follower, their own past may catch up: an
   old debt, a rival from their mercenary days, a family matter. Rarely, not every time.
2. **A debt gone bad** (crime, drama): someone the asker loves owes a dangerous person. Pay it, beat the collector
   in a brawl, expose the lender to the guard, or find out the debt is a lie.
3. **The thief among us** (crime): a keepsake stolen by someone in town, not a bandit. Evidence in the thief's pack
   or home; confront them (return it, they flee, they fight), or report them.
4. **Murder in town** (mystery; generic victims only, behind a setting): a victim found dead in their home; three
   suspects with motive, means and opportunity from real ties; clues in pockets and homes, a witness, an alibi for
   each innocent; accuse with evidence before the steward or the captain. Wrong accusation: the innocent is fined or
   shamed, the killer stays free and may strike again.
5. **A feud** (drama, brawls): two people of a town at odds over a real grievance; take a side, settle it in a brawl,
   or reconcile them with a gift or a word carried. The loser remembers.
6. **A letter unsent** (romance): someone loves someone; carry a letter, arrange a meeting, find out about a rival
   or a secret marriage. With the relationship mod installed, affection moves through its own API.
7. **The informer** (politics, secrets): someone passes word to the other side of the war; find the proof, then
   expose them, warn them off, or use it. The political engine records the outcome.
8. **Raid on the farm** (action, battles): a gang threatens a homestead the player knows; defend it with whoever
   comes (the battle system), or pay them off.

## 5. Mysteries, fairly

From Shadows of Doubt, Golden Idol and Obra Dinn:

- The crime is built from real ties: motive (rivalry, debt, a lover's spouse), means (a weapon the culprit owns,
  which goes home with them), opportunity (when the victim was alone).
- The body is found within about a game day; a witness remembers people who acted out of place.
- Two independent routes lead to the culprit; each innocent has one clue that clears them; at most one strong red
  herring, with its own innocent explanation; every placed item is a clue or a deliberate misdirect. Code checks
  before the arc starts that the culprit can be reached from findable facts.
- An accusation names the culprit and shows one piece of evidence. Wrong: the innocent suffers, the culprit stays
  free, never a dead end.

## 6. Safety

- **Never killed, moved for a story, or arrested**: the player, children, essential or protected actors, anyone in a
  quest alias (filled now, or named by a quest that has not started yet: a scan at data load of every alias with a
  unique or forced actor), jarls, stewards, merchants, innkeepers, bards, followers and former followers, the
  player's spouse and household, anyone SeverActions is holding, anyone in a scene.
- Murder victims: unique, non-essential townsfolk with no role above, or a spawned stranger; only out of sight;
  `MoveTo` then `Kill`, never `KillEssential` (it rewrites the base), never `PlaceAtMe` a unique base. The vanilla
  death reactions then run (mourning, the inheritance letter, a relative hiring thugs): welcome, they make it real.
  The dead-body cleanup script moves a body and its pack after 12 game hours unloaded: evidence is never left on a
  victim who may be cleaned up.
- Evidence in a home is put there when the home's cell loads, in a container that does not respawn, owned by the
  person. Stamped items are books, notes, keys or misc items, never something the NPC would equip.
- A battle no longer wipes the player's bounty in every hold (fix needed in BattleManager).
- Everything an arc placed is taken back or explained when it ends; arcs survive save and load (co-save), and an
  older save without arcs loads unchanged.

## 7. Performance

- One native call per arc tick returns every fact the arc needs; the Papyrus watch makes at most 3 native calls per
  open arc per tick (each native call waits for a frame).
- Inventory scans by stamped name are throttled (only while the prop may have moved: the player near it, a
  container-changed event).
- Catalogues (relationships between NPCs, homes and owned containers, quest-alias protection) are built at the main
  menu or data load, in budgeted slices, and logged with their cost.
- Every native that walks the world logs its cost; anything near 0.5 ms is split.

## 8. Edge cases to play (player's point of view)

Each pattern is played through every branch. Beyond that:

- Save and load in the middle of every node; load an older save; start a new game.
- The asker, the culprit, a witness or the victim's body is killed by the player, by a dragon, or disappears
  (disabled by another mod); a prop is sold, dropped, stored in a chest, or given to a follower.
- The player ignores a hook; walks away mid-talk; fast travels; sleeps or waits through a deadline; enters a city
  worldspace or an interior mid-node.
- The player accuses the wrong person; accuses without evidence; attacks the culprit before the reveal; pickpockets
  evidence and is caught; has a bounty; is arrested.
- The player is in combat, in a vanilla quest scene, on a horse, a werewolf, a vampire feeding, in a menu, in
  dialogue, when a beat wants to come.
- Two arcs want the same person; an arc's person is a follower now; a former follower rejoins mid-arc.
- The player has no home; the home is in another hold; the visitor arrives while the player is asleep or away.
- Languages: the Albanian profiles, where every name and word comes back in Gheg.
- Long play: a day of idling in a town, a week of travel, ten arcs in a row (variety, no repeats).

## 9. Engine facts the plan rests on (researched 2026-11-18)

- `Kill(killer)` blames the killer; with none loaded nobody witnesses. `KillEssential` permanently edits the base.
- Vanilla death reactions start from the story manager's kill event: WIKill01 (mourning), WIKill03 (inheritance
  letter), WIKill04 (an enemy thanks the player), WIKill06 (a relative hires thugs); `OnStoryDiscoverDeadBody`
  starts WIDeadBody01.
- `WIDeadBodyCleanupScript` on town uniques moves the body and `RemoveAllItems` into the grave container after 12
  game hours unloaded.
- Home cells: `TESObjectCELL::GetOwner`; refs `GetActorOwner`/`GetFactionOwner`; non-persistent refs of an unloaded
  interior are not in memory, so evidence is planted on cell attach. Respawning containers (`TESObjectCONT` flag)
  reset.
- Pickup: `TESContainerChangedEvent` (new container 0x14) then the stamped name.
- No vanilla NPC arrest; SeverActions offers arrest, off-screen jail and release through its mod events (internal,
  version-dependent) and frozen `SAAPI_*` exports; a fallback is our own jail sandbox.
- Brawls: vanilla `DGIntimidateQuest` (0x47AE6) started by the brawl story event, player only, one at a time,
  stages for won/lost/assault; SeverActions' brawl works for any two actors and reports `SeverBrawl_Ended`.
- Relationship mod: write only through `SNRom_Bridge.ApplyDepth` on `SNRom_Quest`; `SNRom_AssessSoon` mod event.
- Natives are not tasklet-callable: each Papyrus call waits for the game thread.

## 10. Build order

1. Engine: graph nodes, cast, props, places, flags in `Quests.h`; the three quests as graphs; co-save v2; checks.
2. Facts and tools: one batched fact call; props in packs and homes (cell attach); deaths and killers; brawls;
   choices; arc actions; journal texts in the esp.
3. Patterns, one at a time, each played through every branch before the next.
4. Director: tension, rhythm, variety ledger, idle hooks, home visits; arcs as the director's major beats.
5. Safety catalogue (quest aliases at data load), the battle bounty fix, memory writes that survive SkyrimNet's
   post-load check, rumor reconciliation.
6. Long play tests and the edge cases above; Albanian profile; the full Isekai profile.

## Someone held at a place, and stories that are more than an errand (2026-11-19 night)

The user, of the tales played that day: "are these quests this flat only because you are testing ... I thought the
composer would tie a story to multiple types where going deep into the ruins and killing a boss can be part of a
larger story, not a flat fetch quest. And do we still have kidnapping and rescue quests?" The flat ones were test
tales written by hand to exercise single steps. Two things followed.

### Depth is held to by the compiler (Tales.h, `Pool::depth`: stories asked of the model only)
- Ways that end differently: a story whose every way leads to the same ending is refused.
- A place out in the world is one act among people: at least two steps among people (talk, choose, visit, stalk,
  brawl) besides it, and after every step out there (delve, slay, free) a talk or a choice must still be reachable.
  "Go there, kill it, take it, hand it over" is refused with the reason, and the author writes again.
- `Pool::mustPlace`: after two stories that stayed in town (and none barred), the next has a place in it.
- The prompt says the same in the author's terms ("Never an errand", "one act of a story"), and that each outcome of
  a talk is ONE thing (see "what the play showed" below).

### Someone held (`"held": "dX"` on a person, step `"free"`; Arcs.h `Kind::Free`, `CaptiveState`, co-save 7)
- Who: `Person::canHold` (the measure of a murder's victim: nobody the town or a quest needs, nobody near the
  player). Where: `SiteHolds` (bandits, necromancers, vampires, Forsworn, hagravens, Falmer; never a Dwemer ruin, a
  tomb or a den). One person a story, never the asker; nothing is done with them before the step that frees them
  (checked on the graph); once in four stories (`barred`).
- The game side (IntelEngine_Arcs, "someone held at a place"): taken (disabled) once unseen, when the offer goes
  out; if they still stand in sight when the asker arrives, the offer is dropped. Away until the heart of the place
  is in the game (the same signal 14 that places a master), then put beside it: FIRST made a prisoner of the place
  (Skyrim.esm `dunPrisonerFaction` 000AA784, a friend of every such place's people, so nobody there harms them) and
  unable to die, THEN moved in, restrained, on their knees (`IdleBoundKneesStart`). Freed by the player's own hand
  (they activate them; see "Cut loose by hand" further down; until 2026-11-19 late night, by standing within 300
  units with no fight going on). They follow the player (IntelEngine's own follow
  package), stay while a step of the story is with them, and part once the player is out of the place under the
  open sky, or after three game hours: they walk home, or are simply home when the player is far ahead. Everything
  put on them comes off at the parting. A story declined, dropped or ended with them still held brings them home
  unseen; no memory is written of anything that did not happen.
- A rescue asked for (`ArcRequestRescue`; `TaleSeed::kind == "rescue"`, `Pool::mustCaptive`): the old dungeon
  master's rescue quest (IntelEngine_StoryEngine.HandleQuestDispatch) hands over to this at the new levels, and the
  one held must be close to the asker as the game has it (`Person::close`: a relationship record between the two,
  or the same roof), or nothing is written. The user, of a hand-written test in which Ysolda asked after Nazeem:
  "it kinda doesn't make sense".
- The 3.8 rescue (teleported in view, beaten into bleedout, a hostage nobody could find, memories of a captivity
  that never was) runs at the Classic level only, with its two worst faults mended.

### Played (test profile, by talk; the camp and the cave entered by console, fights by real swings in god mode)
- A hand-written tale at White River Watch: taken unseen, placed 0.2 s after the cave loaded, freed on reaching him,
  came out through the passage with the player, parted after three hours and set off home, flags restored.
- **A model-written rescue, offer to ending** ("The Quiet Wife", second attempt): Nazeem asks after his wife Ahlam;
  Anoriath saw her leave of her own will; she is held at Halted Stream Camp beside the bandit chief; freed, she will
  not say why until she faces Nazeem; Nazeem owns up to the crooked bargain that had him blackmailed; the choice
  (Speech too low to press him) ended "other", 160 gold paid in person, a memory for each of them.
- What the play showed, all mended:
  - Placed first and made a prisoner a moment later, the bandits beside him had already drawn and went on striking
    (half his health in twenty seconds). Now a prisoner before they stand there; seen at Halted Stream: untouched.
  - A talk that really took place could hang: Nazeem told all he knew four times, and neither prepared outcome was
    what he said. After three exchanges that match none, the judge names the nearest (`pending.nones >= 3`). Seen:
    "an outcome must be named", and the story moved on. The prompt now asks for outcomes that are one thing each.
  - The log called the step "stalk".
- Not played: a model-written story with `must_place` (needs two stories told first), the Falmer/vampire/Forsworn
  kinds of place, a captive left behind by fast travel, a declined rescue (the release path was read, not seen).

## The game's own civil war (VanillaWar.h), and the reports from Discord

See `Scripts\intelengine\reports\bug-ledger-vs-4.0-2026-11-19.md` on the user's PC for every report with where it
stood in this branch and what was done. In short: the simulation reads the civil war from the game (CWObj stage 255,
CWPlayerAllegiance, MQ302, MQ305) each political tick and is held to it; ids in any case; soldiers belong to their
side only; nothing political with politics off; drivers and ferrymen never sent; removal that removes; failures told
once; names as whole words (NameMatch.h); nobody walks into a scene; JSON dumps that cannot throw. The maintainer's
3.8.3 is merged in.

## The journal's marker, and a player who does not keep to the order (2026-11-19 late night)

The user: "Based on player unpredictability, we may need a more reliable way to resolve the quest markers." The
marker of a step used to be decided when the step was shown, and for most kinds of step never again; and a fact
counted only while the story stood on the step that asked for it.

### One answer for the marker, asked on every look (`ArcBridge::MarkerOf`)
- `ArcForm(id, "marker")` and the watch (`Advance`, every 2 s) ask the same function. When the answer differs from
  the one before, the script is told (signal 14, the same one a place's heart uses) and sets the alias anew
  (`ShowStep`). A new step sets its own. What signal 14 also calls (`PlaceBosses`, `PlaceCaptive`) does nothing
  unless there is something to place: a master is placed once (`siteState` bit 0), someone held only while they
  are away and their place is in the game.
- The order: what the step is about, where it is right now (the ambush leader who carries the thing, standing or
  fallen; for a place its master, the one held, its heart, or the place on the map (`PlaceMarker`); the spot
  something was hidden at); else where it is to be looked for (the home's door for something hidden in a home that
  is not put there yet, and for "go to their home"); else the step's person; else, for a person who is not in the
  game just now, the door of their home.
- Never a reference that is switched off or deleted (`Points`): such a marker shows nothing.
- `HomeDoorOf`: a teleport door inside the home cell, the one to the street where there is one. Such doors are
  persistent, so it is found while the home is not loaded; found once per home and kept until the next load.
- Before: "search their house" and "go to their house" put the marker on the owner, wherever in town they were.

### Done early is done
- **The depths of a place**: reached once is reached (`siteState` bit 2, in the co-save with the rest of that
  byte), whichever step the story was on. Measured from where the game put the place's master when the heart of
  the place is that master (Halted Stream Camp's chief), not from where a fight has taken him.
- **Someone held**: free once the player has cut them loose (below), whichever step is current. The step that
  frees them is done the moment the story comes to it.
- **Cut loose by hand** (the user, on seeing the first version free her the moment the player arrived: "Wasnt
  Ahlam freed a bit prematurely?", then "Yes freeing should be activate"). Coming near no longer frees anyone: it
  did so for a player sneaking past, and with the captors still standing around. The player walks up and activates
  the one held. `ArcBridge::Activated` hears it (a sink on the game's `TESActivateEvent`; everything but the
  player's hand on an actor falls out at once), sets `captiveState` to Freed there and then, and sends the mod
  event `IntelEngine_ArcUntie`, on which the script runs `WatchCaptives` at once, so the bonds come off without
  waiting for the next look. While bound, `BlockActivation(true)` keeps the game's own answer to the key (its
  dialogue menu) away; the event still comes. The crosshair goes on saying "Talk", so the first time the player
  stands within 500 units of them a notification says "<name> is bound. Activate to cut the bonds." In a fight
  too, at the player's own risk: the one freed still cannot die and is still left alone by the place's people
  until they part from the player.
- **The steps that led there are left** (`If::Freed` = 16, Tales.h `leadsUp`): every step that can only come
  before the one step that frees them gets a shortcut to that step. It fires when they are free already: the
  step's objective is taken away (the engine's own `skip`: not completed, no line in the journal, nothing for the
  step's person to remember), and the story stands at the step that frees, which is done on the next look. It
  comes before the death of someone the step needed (nobody needs the witness any more) and after anything else
  that holds at the same moment (a way down that was gone all the same is done, not left). Only where exactly one
  step frees them: with two ways of freeing (a ransom, a door broken) each writes its own line, and which of them
  happened cannot be said for the player. A step the story can come back to after the rescue gets no shortcut.
- Talking with the one freed before the story asks is not judged then: the step that asks for it needs an
  exchange with them while the story stands on it (`TaleTalk`).

### The engine's own quests (keepsake, bounty, venture)
- Their steps already counted whenever their fact held (several steps a look). The marker was set only when a
  step was shown, on whatever the step's role named. `QuestForm(id, "marker")` now takes the first of the step's
  candidates that is in the game (the holder, the foe, the place's heart). `QuestBridge::Advance`, the look that
  is made anyway, compares it with the one before and returns `kMarkerMoved` (1 << 30, alone: nothing was done)
  when it has changed on the same step; the script's `Settle` sets the alias anew. No call of its own.

### What it costs
- No model call anywhere in this. A step left by the shortcut is a talk the judge never has to read.
- Inside the game's frame (the looks are Papyrus natives, every 2 s for each open story or quest): the marker is a
  handful of form lookups and flag reads inside the look that ran before; nothing walks the loaded actors. The one
  walk is `HomeDoorOf`'s, over the references of one home cell, once for each home in a session's load, and it
  says in the log what it took ("the door of home ... ms of the game's frame, once").
- `FrameCost.h` (`FrameClock`): both looks are now timed where they run, the wait for the bridge's lock included.
  A look of 0.5 ms or more is said at once; the slowest of every 150 is logged ("Arcs: a look at a story: at most
  ... ms of the game's frame in the last 150 runs", "Quests: a look at a quest: ..."). Neither look was timed
  before.
- **Measured (test profile, 2026-11-19 late night), and what the timing found that was there before:**
  - The first look ever timed took 0.64 ms: 0.52 of it was `HomeOfCast` in the Bannered Mare, asking of every
    reference in the room who owns it, and for each whether each person of the story belongs to the faction that
    does. Only a bed counts, so only furniture is asked now (and only containers and furniture in `HidingSpot`).
    After: **at most 0.08 ms in 150 looks**, twice, each over five minutes with the story taken on, the camp
    entered, the one held cut loose, and Whiterun's street. The door of a home: 0.01 ms, once.
  - The look at who follows the player (`LivingWorldBridge::Track`, every 10 s) showed 0.94 ms at worst in one
    session and 0.38 in the next, 0.21 of it going through all 2129 actors of the game's process lists to find the
    teammates. That grows with the load order. It is now a slice a look, at most 0.12 ms (`kScanMs`), the answer
    being the last pass finished: **at most 0.26 ms in 30 looks**, the slice done by 0.10. Its log line says where
    the slowest run's time went. A new follower is known a look or two later; one who left is seen at once.
  - Not measured: the full Isekai profile (many more actors and references), and a look at an engine quest (none
    was open).
- The co-save: one bit of a byte that was there (`siteState`), and the shortcut edges of a story with someone held.

### Checked
- Offline: `TalesCheck` (the shortcut is compiled where it belongs and nowhere else; found early with the witness
  dead; the master dead and they free in the same look; two ways of freeing), the whole suite 46 of 46.
- **Played the same night** (test profile; a hand-written tale: Nazeem asks after Ahlam, Anoriath saw her go, Halted
  Stream Camp, "go to Nazeem's house", a choice; the camp entered by console with the bandits blind):
  - Before asking the witness: standing on top of her does nothing; the key on her, and the log says "cut loose by
    the player (before the story asked for it)"; she stands and speaks within the moment; two seconds later the
    talk with the witness is gone from the journal (objective never completed, hidden), "Find Ahlam and set her
    free" is completed, and "Hear from Ahlam why she came here" is shown with the marker on her. No dialogue menu.
  - The talk with her read by the judge; "Go to Nazeem's house": the marker on the door of the Drunken Huntsman,
    where he lodges (found in 0.01 ms), done on walking in; the choice, the ending, the reward.
  - Left behind at the camp by a player gone ahead, she went her own way; come out with the player, she parted at
    Whiterun's gate with a word of her own.
- Not played: a search of a home before and after the thing is put there; someone the current step is about
  switched off (their door); an engine quest whose marker moves; cutting someone loose in the middle of a fight.
