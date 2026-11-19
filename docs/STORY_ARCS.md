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
(no line of theirs pending, their last one 6 s ago, the player silent for 12 s; by SkyrimNet's account of speech, not
by the dialogue target, which many people carry for minutes), and `Core.WalkBack` sends them at a walk to the spot
they set out from (`RememberWhereFrom` moves their own marker there at dispatch), or home when that spot is beside the
player; tracked like every walk home (`CheckWalkHomeActors`). An asker the first step is with stays. A tale's visitor
leaves the same way when their step is done.

**A quiet stretch.** `Arcs.h`: `Book::LastBeat` (the last offer made or answered, or ending; a plan dropped unseen is
no beat) and `QuietFor`. No tale is requested and no pattern planned within `kQuietHours` (6) of it, whoever asks and
why (the director's reasons and the test hooks too; before, only the pacer kept a day between its own stories). An
offer already owed (deferred, or on the road from afar) waits out `kQuietHoursOwed` (1).

**Not in front of the people it is about.** `ArcBridge::Company(id)`: someone of the story other than its asker who
stands within 500 units of the player (followers are not counted). With such a person there the offer is not sent
(`SendOffer`, the deferred and far-comer loops); a visitor already on the way turns back when within 1400 units
(`StoryEngine.HeldBackByCompany`, also asked at the moment of arrival), walks back, and the story waits until the two
are apart (`ArcOfferHeldBack`; a far-comer tries again within the hour).

Long texts are cut where they read whole (`Tales.h Words`): at the last full sentence, else the last clause, else a
word with "...". An asker's plea in the offer box read "...because I cannot go near him... Reward: 155 gold".

Played on foot: Faendal comes at 77 units a second (a walk) at the first approach and again at the next meeting; lets
it be when the player leaves for Whiterun; Sven's model-written tale offered as from a stranger, Embry questioned by
text and the talk read. Offline: `ArcsCheck` (the quiet stretch), `RunArrivalDeliveryBytecodeCheck` ("visit over":
stays while anyone speaks, then walks back; nowhere to go = stays), `TalesCheck LongWords`. **Not yet seen in game:**
taking leave, the quiet stretch and holding back for company (the PC's active Windows session was not the test's when
they were built).

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
