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

**Strangers.** `ArcBridge::KnowsPlayer`: 2 has had to do with the player, 1 the town heard of the player (an ended
story with a rumour there), 0 stranger. "Has had to do" is: SkyrimNet has seen the two have to do with each other at
least six times (`Arcs::kAcquaintedEvents`, `MemoryDB::TimesWithPlayer`: a talk, not a greeting), or they once
travelled with the player or are close in the game's own relationships (`LivingWorldBridge::CloseToPlayer`), or the
game ties the two with a relationship record, or the player took on a story of theirs. The pacer asks those who know
the player first. The author is told `asker_knows` and writes the offer accordingly.

**Who goes to the player, and who waits (2026-11-21).** The user, of an innkeeper who walked out of Whiterun to a farm
to ask a favour of a character who had stood in her inn for a minute: "he never met her before and she left her inn to
track the player down as if she knows he is some kind of hero. Is this supposed to be immersive". Three holes let it
happen. "Met" was true for anybody who had said one line to anyone since the game started (the counter kept for
refreshing bios was asked) and for anybody SkyrimNet had seen with the player once. "In their town" was the name of
the place the player stood in, and a town's name covers its farms and its meadery. And nothing kept somebody with a
post at it. The rule now, one function (`Arcs::StaysPut`, cases in `ArcsCheck WhoGoes`), asked at the one door every
offer goes out of (`ArcBridge::SendOffer`: the pacer's, a tale's, the director's picks of the town and the test hook
`DebugArc` all pass it):
- whoever the player is with (the same room, or out of doors within 3,000 units) may speak up there and then;
- who was let come from elsewhere by the director's own distance rule (`Flag::CameFar`, `Flag::Sent`) comes;
- somebody with a post to keep does not leave it to look for anyone (`KeepsPost`: who sells, who guards, the jarl,
  the steward, innkeepers and merchants by their job factions);
- nobody goes out of their own town after the player: `PlayerInTownProper` is inside a walled town's own world, or
  within 4,500 units of an open town's map marker; indoors, wherever the player last stood under the sky;
- a stranger does not go looking for the player, except as someone who has set out to ask round their town for anyone
  (`SetsOut`, one in three of the stories that began as talk, after six hours, by day, and now only among the
  town's houses and never from a post).
Whoever stays waits (`OfferWaits`: `Flag::Deferred`, four more days), their trouble goes round as talk in town with
where to find them, and the offer goes out when the two meet. Somebody who knows the player and held back only because
the player was out of town comes across it once the player is back (`Pending::awaits`; not kept across a load: then
they wait to meet). The director's "of the player's own settlement" (`NPCIndex::ReachOf` 0) holds only with the player
among its houses; out at its farms they count as the rest of the hold, and need the reason and the acquaintance.
Played on the test profile: the player at Chillfurrow Farm, Hulda given a story: "does not go looking for the player
(they have a post to keep)", talk in town, nobody sent; the player back in the Bannered Mare: she came over inside her
inn. Olava the same way ("the player is not in their town"), and she spoke up when the player walked into her part
of town. Not played: somebody who knows the player coming across town once the player is back.

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

### How somebody stands with the player without a relationship mod (2026-11-21)

The user: "we have no reliable fallback for bonds if the relationship mod is missing like how many memory records you
have with a specific npc and judging those memories if they ever hinted to friendship or a flirt". Without SkyrimNet
Relationships the mod knew the game's own relationship rank, a marriage, and whether somebody once followed the
player: nothing of a friendship that grew in talk, of a flirt, of a no. Now (`Bonds.h` the rules, `BondsCheck`;
`BondBridge.cpp` the game side; as first built it was meant to count only while `SNRom_Integration.esp` is not
loaded, which the section after this one replaces with a rule for each person):

- **Counted.** How often somebody has spoken to the player: SkyrimNet's count (`MemoryDB::TimesWithPlayer`) or
  IntelEngine's own (`DialogueTracker` tells `BondBridge::Heard` of every line whose target is the player; kept in
  the bond record), whichever is larger. In the test game SkyrimNet's `GetPlayerContext` answered
  `"relationships":[]` with the lines in its database, so a trigger that waited for it alone would never have come.
  A count says "acquainted" and no more (six lines: `kFirstAt`, the same number as `Arcs::kAcquaintedEvents`).
- **Read.** From the sixth line on, and again after eight more (and two game hours), the dialogue model is given what
  that person remembers that has to do with the player (`GetMemoriesForActor` with the player's name as the query,
  24 at most) and the last twelve exchanges between the two, and answers three questions by choosing a name
  (`decisions/intel_bond.prompt`, chat form `intel_bond_chat.prompt` without a decision provider): how they stand
  (stranger, acquaintance, friend, confidant, lover, cold, foe), whether anything of the heart is in it on their
  side (none, drawn), and how the player answered (unanswered, welcomed, declined). No words are searched for: it
  works in whatever language is played. The database is read on the worker; one reading at a time, 45 real seconds
  apart; whoever is with the player first. Children are never read.
- **Not believed on one word.** One and the same talk, read three times, came out friend, acquaintance, friend. A
  reading is taken when the reader is sure (the weight it gives its own answers, the smaller of standing and heart,
  at least 0.75) or when the reading before said the same; otherwise it is noted, how they stand stays as it was,
  and they are read again after three more lines. Played: unsure "acquaintance" noted, the same again taken, two
  more readings left it as it was.
- **A no stands.** Once the player is read to have turned somebody down, later readings that find nothing of the
  heart, or find it unanswered, do not wipe that out; only the player plainly taking it back does. The day of the no
  is kept for `Hearts::SecondChance`.
- **Where it counts.** `LivingWorldBridge::CloseToPlayer` (a confidant or more: who may come from afar, who hurries);
  `ArcBridge::KnowsPlayer` (acquainted); the pacer (somebody cold or a foe does not bring their trouble to the
  player); `HeartBridge::HeartOf`, which raises the tier above the game's own rank and sets the spark and the
  player's answer, so a courtship can begin from a friendship that grew in talk. The game's rank and marriage stand;
  who may be part of a story of the heart is still `Hearts.h`'s to say (never a child, never kin, never against a no).
- **Kept** with the save (record `IEBN`), ids resolved again on load.
- Test hooks (`IntelEngine_Arcs`): `DebugBond(actor)` reads somebody now, `DebugBondOf(actor)` says what was read and
  what comes of it (close or not, the line a story's author is told), `DebugBondNoMod(true)` goes on as if no
  relationship mod were installed (the test profile has one).
- Played on the test profile (2026-11-21): Hulda after three friendly exchanges read "friend", "To Prisoner: a
  friend." told to an author; Saadia, who holds an old refusal against the test character, answered a courting line
  coldly and was read "cold / none", nothing of the heart told; after Hulda's sixth line a reading started by
  itself; four readings came back after a save and a load. Not played: a reading of "drawn" in the game (nobody
  courted the test character), a no and its taking back, a follower, memories on record (the test timeline had only
  talk), the chat form, a game without SkyrimNet Relationships installed (the test switch stood in for that).
- What it costs the player: one small decision call per reading (answered in about 0.3 s in the test), per person at
  most once every eight lines they say to the player.

### Time skipped is not time played (2026-11-22)

The user: "What about player constantly skipping time. Will that ruin the pacing?" It would have. Every pacing rule
counted the game's clock: the director's budget (so many moments a game day, three game hours apart), the town
pacer (two game hours in town, a game day between two stories), the quiet after a story (six game hours). The
budget is capped at two moments, so nothing piled up over a long sleep; but whoever waits a day, and then another,
was owed a moment after each wait and a new town story after each (each a call to the tale author, most of them
lapsing unseen).

`LivingWorldBridge::PlayedSeconds()`: seconds the game actually ran this session, counted by the look of the living
world (`NotePlayerWhere`, every ten seconds or so), which is not taken while the game is paused, and never more
than twenty seconds a look: a menu, a wait, a sleep or a fast travel adds one look's worth. Kept for the session
only. Rules (pure, in `LivingWorldCheck` and `ArcsCheck`):

- the director: `kMinGapPlayedSeconds` (480) of play between two ordinary moments (`Situation::playedSince`,
  `MayDeliverNow`); what is urgent still comes; the stamp is set where a moment is counted (`OnStoryDispatched`,
  `OnPatrolSent`);
- the town pacer (`ArcBridge::Pace`): `kTownLingerPlayed` (240) of play in the town, and `kStoryGapPlayed` (1200)
  since the last story that began by itself. The log says once why it holds back.

At a timescale of 20 these are what three game hours (nine minutes), two game hours (six) and a game day
(seventy-two) already are, so they bind only when time is skipped or runs fast. Not changed, on purpose: what the
world does by itself while time passes (rumors travel and fade, an offer lapses after its days, followers left
behind get on with their lives, a gathering nobody came to is decided unseen, a patrol's two game days of rest).
Test hooks set by hand (`DebugTale`, `DebugArc`) do not go through the pacer and are not held.

Played on the test profile, skipping time with `set timescale to 2500`: in town "22 s played here (240 needed,
whatever the clock says)" after four and a half game hours; "a tale is being written in Whiterun" at 240 s; after
a further game day "the last one began 11 s of play ago (1200 needed)". On the road, after a patrol: a game day
later "budget 2.00, bad moment false, 76 s of play since the last moment: held back", then 362 s held back, then
"531 s of play since the last moment: a moment may land". Not played: the game's own wait and sleep menus and a
fast travel (the fast clock stood in for them); twenty minutes of play to see the second town story begin.

**The switch.** The user, right after: "Make it a setting that the user can choose if time skipping should count
toward the story time". `livingWorldSkippedTime` (`living_world.skipped_time_counts`), named "Skipped time counts
toward the pace" on all three pages, off by default. `LivingWorld::Settings::skippedTimeCounts` reaches the DLL as
the seventh argument of `SetLivingWorldSettings`. On, `MayDeliverNow` leaves out the gap in time played and
`ArcBridge::Pace` leaves out both of its conditions in time played, so the game's clock alone counts, as it did
before the rules above. The line the DLL logs when its settings change says which way it stands ("skipped time
counts toward the pace" / "does not count: moments take time played").

Played: off, after a skipped day "budget 2.00, bad moment false, 44 s of play since the last moment: held back";
switched on, "66 s of play since the last moment: a moment may land"; thrown on and off again in a fresh start of
the game, the log line and `settings.yaml` (`skipped_time_counts`) followed each time. The dashboard's row was
looked at in the preview (it sends `livingWorldSkippedTime` true and false, which the DLL hands to the scripts as
1.0 and 0.0, the values played). Then the row was opened in the game on both other pages: on SkyrimNet's page it
was switched on and saved ("1 setting(s) taken from settings.yaml", "skipped time counts toward the pace"); in the
MCM its info text shows in full and a click switched it off again ("does not count"). With the switch on, the town
pacer was not watched beginning a story after a skip (only the director's verdict was).

### Any language (2026-11-22)

The user: "Create the necessary dictionaries and language detection logic that properly handles place names in the
appropriate language, even handling special characters like cyrillic or japanese/korean or even chinese, would be
great if we can later on add more dictionary files that translate the labels that are used and the logic only uses
those files based on the games language, and also handle Albanian where the game language is still originally
english (which is a special case)". An audit the same day had found what depended on English (memory
`intelengine-language-audit`).

- `NameForms.h` (pure, `NameFormsCheck`): a name as people say it. Words by the separators of any script; `SameWord`
  (one begins with the other, a short ending differs; never across an apostrophe); little words let go (`Small`,
  and a pack's `free` words); `Stands` gives Same / Forms / PartForms; `Find` the best of many, tied = nobody's;
  `Mentions` for a sentence (a possessive speaks of its owner); `Slips` in letters. Used by `NPCIndex::FindByName`,
  `FindByNameNear`, the form-id and location lookups, `LocationResolver::ResolveCell/ResolveLocation`,
  `StoryCandidateResolution` (which also folds case for every script now), `InformantRumorValidation`.
- `LanguagePack.h` (pure, `LanguagePackCheck` runs the shipped packs through the real `NaturalTimeParser`):
  a pack file's sections (meta, time, place, none, free, label); `TimeToEnglish` (words, then the half of the day
  beside an hour becomes its am / pm); `PlaceToEnglish` (only when the whole of what was said is a word of place);
  `IsNone`; `Translate` (a label found under names); `ScriptCount`, `TextPack`, `SpokenPacks`,
  `SpokenCodesOfTranslator`.
- `LanguageBridge` (the game): reads the packs once, is told the script of the game's own place names by the place
  index, reads Skyrim.ini's sLanguage and SkyrimNet's `UniversalTranslator.yaml`, decides, answers from a snapshot.
  Natives: `PlaceInEnglish`, `IsNoneWord`, `L`, `TextMentionsName`, `TextLanguage`, `ReplaceWholeWord`,
  `CommaList`; `ParseTimeCondition` reads through the packs and logs what it took a time for.
- What the player reads: `Core.NotifyPlayer`, every `Debug.Notification`, `ShowChoice` (what is shown is
  translated, what is returned stays the English the callers compare), `BattleSay`, the journal's title and
  objectives. `SKSE/tools/gen_label_template.py` writes `_labels_template.tsv` from the sources (`--check` in the
  suite). The tale author's context has `text_language`.
- `Tales::Words` counts letters and cuts at the sentence ends of any script; the save record's text limits are
  bytes of their own (`kMaxTextBytes`, `kMaxLongBytes`).
- Politics: `RumorBridge::HoldOfPlayer` (by form id), `HoldByName` as whole words, `HoldShown`;
  `FactionPolitics::ShownAsInGame` adds `leaders_shown` and `hold_shown`, read by the awareness prompt.
- The actions tell the model to write a time in English ("tomorrow 08:00", "in 2 hours"), a name in its plain
  form, and "none" in English: the first line of defence; the packs are the second.

Played (English game, "Languages people speak" set on SkyrimNet's page to six languages; `lang_ingame.py`): the
log reads "the game says ENGLISH, its place names are written in latin; the player reads English; people speak
Albanian, Russian, German, Japanese, Korean, Chinese"; "завтра утром", "nesër në mëngjes", "morgen früh", "明日の朝",
"내일 아침" all 32 (tomorrow at eight), "në orën 6 të mbrëmjes", "um 18 Uhr", "下午6点" all 18, "pas dy orësh" two
hours on; "домой", "n'shpi", "nach Hause", "家", "回家" home, "jashtë", "밖으로" outside, "Whiterunit" left a name,
"asnjë" none; "Huldës", "Huldas" Hulda, "Lydian" Lydia, "Jonit" Jon Battle-Born, "zu Ysolda" Ysolda, "Belethorit"
Belethor. Not played: a translated game; a label actually translated on screen (no pack ships sentences yet);
the dashboard's Language section in the game. Not built: asking the model "which of these did you mean" when a
name still fits nobody; converting names that are not UTF-8 (German and other Western game files may be
Windows-1252: unverified without such an install); block and white lists and pending schedules still go by name.

### A trail to follow (2026-11-22)

The user's bar is a contract with a scene, tracks and a lair; a hunt had one body by the door. `kClues` is 3;
`Arc::clueFar` says how far out each lies (the last looked at by a place lies by it, those before farther out, set
in `Tales::Compile`); a hunt asked for must have at least two. `ArcBridge::TrailSpot` finds the ground: the
farthest sign 4,400 units out (2,200 for a trail of two), the others evenly on the way in, on the line from the
place to the player, every one 3,000 units from the player (less room, shorter trail; only a player at the door
gets it short and on the far side); candidates from the navmesh of the loaded cells, the best under the open sky
(three rays of the game's collision per spot, at most six spots). The script places a marker at the spot and the
dead one at the marker: placed at the cave and moved, the bodies stayed invisible. `MarkerOf` shows the way to the
first sign only for a game hour; `cluetext` adds one of eight fixed sentences for the way on.

Played on the test profile (`tales/test_r.json`, `trail_setup.sh`), on foot: offer taken at Anoriath's stall, one
honest answer from Elrindir, then toward Broken Fang Cave from two cells east: "a sign of the trail, 4400 units
out ... open ground found (9100 navmesh triangles looked at, 716 spots fit, 1 tried for open sky, 0.37 ms)", the
other 0.22 ms. Goat (marked), window "...nothing has fed on the meat at all. The trail leads on to the west.";
journal "Follow the trail on toward the cave", no marker; horse, "The trail leads on to the west."; Torvald by
the cave, no way on; then Anoriath's question, the cave, Elrindir's choice, the end. What the first walk found and
mended: both far signs invisible (moved before loaded), one on the far side of the cave out of reach (each sign had
chosen its side by itself). Not played: a trail laid by the model's own story; the marker appearing after the
hour; a player arriving at the door (the far-side branch).

### What a story leaves to the next (2026-11-22)

`TaleEnd::after` (the writer's sentence on what an ending leaves unsettled), `Arc::sequelOf`, `Arc::followed`,
record version 10. `RequestTale` hands the writer each earlier tale with "HOW IT ENDED" (the closing journal line of
the ending reached) and "LEFT OPEN"; and looks for an open end that has come due: the newest tale of this town
that ended two game days ago or more with something left open, not of the heart, not taken up yet, and not when
the last tale told was itself a follow-up. Its people come first among those offered, the prompt gets a section of
its own, and on acceptance the new tale carries `sequelOf` and the old one `followed`.

Played with the model: the hunt above ended with Elrindir's part kept from Anoriath; a tale for Amren in between
was written with an "after" on every ending and declined; three game days on: "an open end has come due: \"The
Trail to Broken Fang\" (a few days ago) left open: Elrindir owes his silence to Prisoner, and Anoriath does not
know yet that his own brother sent Torvald to that cave", and "\"The Hunter's Widow\" grows out of what \"The
Trail to Broken Fang\" left open" (notes with a goat's head; Torvald's widow in Rorikstead; three endings, each
leaving its own open end). Not played: the follow-up itself to its end; a chain of three.

### Maintenance skipped on a load (2026-11-22)

`Core.Maintenance` guarded against a second call by `Utility.GetCurrentRealTime()` against a time kept in the
save; that clock restarts with the game. A kept time later than now was mended long ago; a kept time a few seconds
earlier than now is the common case (start, load at once: the same second every session) and skipped the whole
Maintenance: "IntelEngine Maintenance skipped (duplicate call within 5s)" as the first line after a load, and no
story visitor's arrival was looked at again. `IntelEngine.LoadStamp()` (the second the session began and a count of
its loads) says which load a kept time belongs to. Seen before and after in the test profile's log.

### What a long play session found (2026-11-22)

Two and a half hours on the test profile from a new character: Riverwood, then Whiterun, three game days, two stories
written by the model (one played to its good ending by talk), a death and a reload, the game's own wait menu, a save
and a load in the middle of a story. No crash, no script error, no stack dump. Seven things came out of it; all are
built, and each was seen again in the game afterwards unless it says otherwise.

**A quest moved on behind the player's back held the town's stories back** (`ArcBridge::QuestMovedOn`, `QuestBusy`).
Every stage change of a quest with a journal type set a quarter of an hour of "the player is on a quest". The game
moves such quests all day without the player: "Animal Extermination" twice, a nameless one, and after a load "Rescue
Mission", "Family Heirloom", "Hired Muscle" and "Take Up Arms" within three minutes. The stage event is now only noted
(the newest six quests, each with the second its hold would end); whether it holds anything back is asked when a
story is about to begin, and then only a quest the journal shows counts (an objective of it shown, done or failed).
The stage is set before the quest's own script shows its first objective, which is why the question cannot be asked
at the event. Test hook `IntelEngine_Arcs.DebugQuestBusy` (seen: False with the four above, TRUE after `setstage MS13
10`).

**Asking for a tale took 0.74 to 0.85 ms of one frame** (2.4 ms on the large list). `RequestTale` is now a thin
caller of `GatherTale`, a coroutine (`FrameSpread.h`: `FrameJob`, `co_await NextFrame{}`, `FrameShare`) that gives the
frame back before the people, every few people, before the places and before the words for the writer. The answer
"a tale is on its way" is given before the first pause; a failure further down is logged and frees the writer's desk,
as a tale that comes back unusable does. A save loaded between two parts ends the job. A test's own tale is still
checked and planned in the one frame. Seen: five frames, at most 0.32 ms of any one.

**Three stories running were the same story.** A bandit chief with a hold over a townsperson, a camp, a kill, and
titles "The Quiet Order", "The Quiet Husband", "The Quiet Ledger", 155 gold each. Held by the engine now:
- the master of a place is not killed in two stories running (`slayTwice`: the step is not offered, the compiler
  refuses it, and no hunt is on offer either since a hunt ends on one); a story asked for by kind (a hunt, a
  haunting, a rescue) is that kind whatever came before;
- the next story that leads out goes to another kind of place than the last, as long as two places of other kinds
  are near (`lastPlace`, `SiteKindOf`);
- a title shares no word of five letters or more with the last six titles (`Pool::titles`, `detail::SharedTitleWord`
  in Tales.h; in any alphabet, names of the story's own people and places are free, scripts without spaces are not
  held to it; on the second asking a title is no longer a reason to lose the tale: `Pool::titleMayRepeat`);
- the reward is the level's sum times 0.8 to 1.2 by the story's seed, in fives.
The writer's prompt says what the town's first word is (what was seen or heard of the asker, never what sort of
helper is wanted), what a title is, and that trouble mostly comes from among a story's own people. Seen: the next
story was "The Mare's Second Ledger", wholly among people, 160 gold.

**An asker spoke into somebody else's talk.** The acolyte's request was sent while the smith was still speaking, and
SkyrimNet gave the line to the smith. `ArcBridge::PlayerTalksWithAnother(except, within)`: somebody other than this
one said a line to the player within `within` seconds, or has one on its way or being said, or one that ended within
`within` seconds, or one the player answered within `within` seconds (`DialogueTracker::SecondsSinceOtherSpokeToPlayer`
with `SpeechTracker`). Nobody sets out toward the player with a story while that is so (45 s; all four doors in
`Lapse`), and one who arrives during a talk waits (`IntelEngine_StoryEngine.WaitsTheirTurn`, 20 s; asked again every
look; after a minute and a half a story's asker lets it be, a one-off visitor speaks after all). One who arrives after
another story came to the player or ended within the hour lets it be (`ArcQuietNow`). Seen both: "waits their turn",
then the request in the acolyte's own voice with a glance at the smith; and "lets it be for now" when the smith's
story had just ended.

**A decision already spoken was asked again by window.** `ArcBridge::ChoiceTalk` / `ChoiceUnread`: a tale's decision
step is first looked for in what the player has said to the step's person since the step became the story's step,
once both are quiet, by the same reader that reads a talk step (`SendJudge(..., decision = true, window)`; the two
reader prompts ask whether the player has plainly said one of the answers). Named: taken as the answer
(`pending.answer`). None: the window, as before (`pending.spoken`). Never for a step with an answer that needs Speech;
a reading that does not come back within 25 s does not keep the window shut, and comes too late to count. Seen both
ways ("the decision was made in talk", "no decision was spoken in the talk: the window asks").

**Smaller.** The reader of a talk goes by the person's own lines (a talk was read as a confession on the strength of
what the player had just said). A choice window waits while a menu or a text box holds the keyboard
(`Core.ShowChoice`; the wait itself was not seen, only that windows still open). When a story's asker stays put after
all and no talk was written for the town, the town passes on the asker's own request (`ArcBridge::TroubleTalk`, seen).
In open country without a named place the player is in the hold they last walked in (`NPCIndex::GetNPCHoldName`,
`HoldNameAt`; built, not seen in the game).

**Not built.** A new game's inns have nothing of IntelEngine's to tell for the first six game hours (the talk of the
day fills with the first political news and the first story). The walker of the test tools stalls on long ways to a
person inside a far interior; the last stretch to Halted Stream Camp and the way back were made by console, so nothing
of the road beyond eight minutes on foot was seen.

### MCM rows that fit their column (2026-11-22)

Found while the new switch was looked at in the MCM itself (which had not been opened for the 4.0 settings before).
SkyUI sets a text option's value in capitals from the right edge of its column, over whatever the label holds
there. In the picture taken at a third of the screen's size a column is about 207 px wide, a label takes about
4.5 px a letter and a value about 6.5: label and value together fit up to roughly 38 letters, a slider's or a
switch's label up to roughly 40.

- "Interruptions at bad moments" lay under "ONLY WHEN URGENT", "NPCs of running quests may move" under "FOR
  IMPORTANT REASONS", and "A follower left behind moves on after (days)" touched its number. One name per setting
  (`RunSettingNamesCheck`), so the table, the manifest, the dashboard and the MCM changed together: the choices are
  now "If urgent" and "If important", the names "Quest NPCs may move" and "A follower left behind moves on (days)".
  Keys and stored positions are unchanged.
- `settings.yaml` stores a choice as its word. `SettingsBridge::ApplyFromFile` now writes the game's value over a
  value it cannot read (a word of an older build, a typo), as it already did for a key the file lacks; before, the
  old word stayed in the file until some setting was changed in the game. Both renamed choices are their setting's
  default, and the game keeps its own value in any case. `RunSettingsBridgeCheck` has the case.
- Active Tasks page: a busy slot's text was passed as the value of "Slot N:" and lay over it. `AddSlotRow` makes it
  the row's own text; the bracketed end ("(near The Bannered Mare)") goes to the label of the Clear row below
  (`SlotWhere`), where the first attempt had been cut at the column's edge.

Seen in the game on the test profile: the Living world section with every row clear of its value, the Active Tasks
page with two travellers, SkyrimNet's page with the same names, and a `settings.yaml` holding "Only when urgent" /
"For important reasons" rewritten at the start ("2 setting(s) the file lacked or held unreadably were written").
The MCM offers "R Default" on every row and no row answers it (no `OnOptionDefault` in the script): as before.
The Scheduled Tasks page was not seen with an entry on it.

### The story clock: one pace setting above Classic (2026-11-22)

The user: "What about the story checks time which is every 3h by default, do we still need that?" Above Classic two
settings paced the same thing. "How often something happens" is the director's budget (half a moment, one or two a
game day, three game hours apart at least: `LivingWorld::MomentsPerDay`, `kMinGapDays`). "Check interval" gated the
clock look in `StoryEngine.TickScheduler` (seams, arriving somewhere or waking, bypass it). Set low it changed
nothing (the budget holds the look back before any model is asked); set to a day or a week it starved the director
while the player stayed in one place. Now `Core.GetStoryClockHours()` is what the story engine's three uses of the
interval and the dashboard's "next look" ask: at Classic the setting, above it `IntelEngine.LivingWorldClockHours()`
(`kMinGapDays` in hours: 3). The setting keeps its place and its name; its descriptions on the three surfaces say
it is Classic's, and the dashboard dims the row above Classic. The two other "Check interval" settings (NPCs among
themselves, politics) are clocks of their own systems and are untouched.

Played: with the setting at 24 hours, `StartScheduler` logged "story=3.000000" at Lives and Rumors and
"story=24.000000" at Classic.

### Places nobody is sent to (2026-11-22)

A user's report the user passed on: "npcs spawn in dangerous places to talk to me and they die / im in the soul
carion / how did they even manage to get here", with the question whether 4.0 solves it and whether the hold and
danger settings are still needed. Read in code: partly. `NPCIndex::ReachOf` put everybody "in another hold" for a
player in the Soul Cairn, and `StoryDirectorPick::MayCome` lets somebody close come from another hold on a pressing
reason (anybody on an urgent one); the old setting "NPCs sent into dangerous places" blocks civilians only by
default; and an unseen traveller whose destination is the player is set down at the player's cell
(`Core.HandleOffScreenTravel`). Nothing said a place cannot be reached.

The rule (`Patrols::PlaceBars`, pure, in `PatrolsCheck`; `LivingWorldBridge::PlaceBars`, taken with the look of the
living world and read from any thread):

- **Beyond reach**: the place has no hold of the game above it (`LocTypeHold` up the location's parents) and, out
  of doors, its land is neither Skyrim's nor Solstheim's; indoors, the last land seen out of doors was not. The land
  of a worldspace is its parent's only while it takes its *ground* from the parent (`kUseLandData`): Whiterun's
  streets take everything from Tamriel (0x7f), the Soul Cairn takes only its map (0x4), and read as "Skyrim" the
  first version of this rule let people through (seen in the test game). At every level, whatever the settings.
- **A dungeon** (`LivingWorldBridge::IsDungeon`: the game's own location kinds) above Classic. At Classic the setting
  decides as before.
- Whoever is with the player already (`ReachOf` 0) is not being sent and may speak.

Where it is asked: `MayCome` (`Verdict::barred`), `LivingWorldBridge::MayMoveForStory` (every dispatch of the story
engine), `Arcs::StaysPut` (`Going::barred`, before even somebody the director lets come from afar; the offer waits
and becomes talk in the asker's town), a tale's visit and stalk steps, `QuestBridge::PlanVenture`,
`FactionPatrolDue`, `ArcBridge::AmbushLook`, and `Core.HandleOffScreenTravel` through the native `PlaceBarsArrival`:
a story's visitor is called off (`StoryEngine.CallOffWhereNobodyComes`), somebody on the player's own errand waits
when the place is beyond reach. Test hook: `IntelEngine_Arcs.DebugPlace` (it also logs the worldspace, its parent
and what it takes from it).

The settings: the seven "how far NPCs travel" choices and "NPCs sent into dangerous places" still work and can only
tighten what the director decides; at the default nobody needs to touch them. They are what Classic has instead of
the director's distance rule, so they stay. Their descriptions say what is automatic now.

Played on the test profile: the inn and Whiterun's streets "people may come", the Soul Cairn "nobody could have
followed", Bleak Falls Barrow "a dungeon", and back; in the Soul Cairn Hulda's story "does not go looking for the
player (the player is where nobody is sent)" and became talk in town, a forced patrol "nobody comes now", Ysolda
dispatched from the Director tab set out and let it be. Not played: somebody already on the way when the player
steps through the portal, a hurried visitor, a follower on the player's own errand returning, Apocrypha, Sovngarde,
a mod's land, Solstheim.

### Private moments: nobody in arms comes upon the player (2026-11-22)

The user, about the faction patrols restored the same night: "I am also not sure about faction patrols either
because users were complaining of soldiers spawning during their private moments". What stood in the way of each
door before, read in code:

- the patrol: the director's `MayDeliver` (a running scene: hard; combat, a busy battlefield, the game paused, the
  game's dialogue, barter, sleep and OStim menus at the default "only when urgent") and its handler (out of doors,
  not in or by a town or house, from a spot unseen);
- a skirmish or brawl near the player, and the killer after a leader (`FactionPolitics::CheckEventManifestation`):
  out of doors for the first two, a cooldown, no battle running. Nothing about scenes, menus or talk;
- the hired men of a tale (`ArcBridge::AmbushLook`): the wilds, combat, `IsBadMoment`.

None of them knew a SkyrimNet conversation (no menu of the game), the minutes after a scene, a rest, a bath, or a
player who has simply stopped somewhere with somebody. Now all three ask `LivingWorldBridge::PrivateMoment()`
(`Patrols::PrivateMoment`, pure, in `PatrolsCheck`), and the skirmish asks `IsBadMoment` as well:

- a scene with the player is running, or was last known of within ten minutes (the script's OStim thread events,
  and the game's own scenes as the look saw them);
- a line to or by the player within two minutes (`DialogueTracker::SecondsSincePlayerTalk`);
- sitting, lying, asleep (`GetSitSleepState`); nothing worn on the body slot;
- settled: less than 1,000 units from where they were about a minute ago, unless they went through a door or to
  another land in that minute. Where the player is, how they sit and what they wear is noted by the look of the
  living world (every ten seconds or so, `NotePlayerWhere`); `PrivateMoment` itself reads only that and a few
  clocks, so any thread may ask. Right after a load, before a minute of looks, nobody comes. Where the look is not
  being taken, standing still is not held against the player and the other reasons hold.

The test switch for patrols does not lift it. Test hook: `IntelEngine_Arcs.DebugPrivate` (logs the reason, or that
the player may be come upon). Played on the test profile: "in talk" (a wandering thief had really spoken to the test
character), "wears nothing", "has stayed where they are", then "on the road and may be come upon" after a minute's
walk; with Stormcloak standing at -78 and every roll forced, "Patrols: nobody comes now: the player has stayed where
they are for a while" twice, and walking, "StormcloakFaction sends 4 after the player". Not played: an intimacy
mod's scene and the ten minutes after it (the test profile has none), sitting and sleeping, the skirmish's and the
tale ambush's use of the same call.

### A shy admirer, and stories that know the talk and the times (2026-11-22)

The user: "if someone is too shy to confess and sends someone to request meeting the player, can the player have
secret admirers, how do rumors tie to stories, how does politics tie back to stories". None of it existed: a
courtship had one shape (the asker comes openly), the tale author knew nothing of the rumor ledger nor of the
political state ("politics" was a genre word).

- **A shy admirer** (`Hearts::ShyAdmirer`, `HeartsCheck`; `HeartBridge::ShyAdmirer`; `TaleAuthor.cpp`). While the
  people of a town's story are gathered, anybody who is close to the asker (a relationship of the game, or the same
  roof: `Person::close`) and has a line of the heart at all is looked at: a courtship that could begin
  (`Hearts::LeanOf`), not answered by the player either way, the game's own confidence at "average" or below (the
  bold ask for themselves), and never part of a story of the heart before. If there is one, the asker has no story
  of the heart of their own and came with no reason of their own (`taleSeed`), the tale is that: `heart_lean`
  "admirer", `admirer` (their key), `admirer_name`; the prompt's section "Someone who has not dared to say it". It is
  a story of the heart for pacing and pay (`Flag::Heart`, no gold); its effects name the admirer ("closer pX",
  "apart pX"), which the compiler always took by person. The offer follows the rules every offer follows: the
  go-between is held back while the admirer stands with the player or within earshot.
- **The talk and the times** (`RumorBridge::TalkForTales`, `FactionPolitics::TimesForTales`, added to the request on
  the worker, after the bios): up to three things going round the hold the player is in (never somebody's trouble:
  that is its own story's to offer) and a few lines of the wars and of how the factions take the player. Both
  sections of the prompt are optional (`exists("talk")`, `exists("times")`) and say what a story may and may not do
  with them. Not for stories of the heart.
- Played (test profile, 2026-11-22): `DebugHeart Saadia 1 0` with the game's rank at friend, `DebugTale Hulda`:
  "Hulda comes for Saadia, who is drawn to the player and has not said so"; the first writing broke a formal rule
  (a choice with one option), the second held: talk with Hulda, talk with Olfina ("what she has noticed at the
  Mare"), a stalk step by Saadia, a choice and a talk with Saadia; the offer waited while Saadia was in earshot,
  went out, was taken on. Then, with a war declared and a test rumor, `DebugTale Ysolda`: "the author is told 1
  thing(s) going round and something of the times"; the tale's hidden truth was a sale to the Legion's
  quartermaster. Not played: the admirer story to its ends; an admirer found by a real reading or by SkyrimNet
  Relationships' own spark (the test hook stood in); a tale that takes up the rumor.
- Cost in the frame: the tale's request was 0.81 to 0.88 ms in the test game with these in (the talk and the times
  are read on the worker; the admirer is one more look at the heart of people who are close to the asker and have a
  line of the heart, next to nobody).

Not built, and why: a letter as an object and anonymous gifts (the engine has no item-with-text path of its own;
the "find" and "give" steps with listed items stand in); a battle enemy who yields and can be spared, befriended or
recruited (battle soldiers are made for the battle and leave the world with it; one that stayed would need a
keeper, a home and a follower mod's consent, and removing loaded actors is what crashed the full profile); faction
missions as stories of their own with new companions (politics colours a town's story now; the old faction quests
remain the dungeon master's).

### What 3.8 had and 4.0 had lost: found by an audit, 2026-11-21/22

The user asked whether 4.0 covers everything 3.8 had. 3.8 (`main`) was held against the branch feature by feature.
Mended:

- **The faction patrol** (`Patrols.h`, `PatrolsCheck`; natives `FactionPatrolDue` / `FactionPatrolSent`; the hook in
  `IntelEngine_StoryEngine.TickScheduler`). The director judges people, each on their own reason; a faction is
  nobody, so "faction_ambush" could only happen at Classic or when no candidate was judged at all. Now, at a moment
  the director lets land (budget, rest after a big moment, no bad moment), the story engine first asks whether the
  faction that stands worst with the player, at -40 or below, sends men: one such moment in five above -60, 35 in a
  hundred above -80, every other one below; three to seven soldiers; not within two game days of the last (kept for
  the session). The old handler does the rest with its own checks, and when it cannot (no spot to come from unseen,
  a battle running) the dungeon master looks as usual and the budget is not charged. A patrol that went out is a
  big moment (`LivingWorldBridge::OnPatrolSent`). The standings are read from the political database inside the
  native, as `GetPlayerFactionStanding` always did; the frame cost is clocked ("Patrols: is a faction after the
  player"). Test hook: `IntelEngine_Arcs.DebugPatrolAlways`.
- **The story-type switches inside stories** (`ArcBridge::StorySwitches`, pushed by `IntelEngine_Arcs.Watch` about
  once a minute): bits for ambush, stalker, rescue, quest, clear-out, find-an-item. `TaleAuthor` bars the steps
  (`bar_ambush`, `bar_stalk`, `bar_captive`, new `bar_slay`), `QuestBridge::PlanFor` leaves out the bounty or the
  keepsake, `ArcBridge::Pace` begins no story with "quest" off.
- **Upgrade.** A 3.7.1 save loaded under this build holds level 2, interruptions 1, quest NPCs 1, pacing 1 (read by
  pushing the script's values to the DLL again after setting the DLL's to zero).

Left as designed, not mended: a battle no longer comes to the spot the player stands on at its hour, and the player
is not joined to a side automatically at standing 20 (a battle is a gathering at a place; a commander asks). 3.8's
"arriving in the middle of a battle" has no twin: the armies face each other for the hours of the gathering and the
battle begins when the player comes, so within those hours it is always seen from its start; after them it is
decided unseen. The 3.8 quests "clear out" and "find an item" are the bounty and the keepsake.

### Beside SkyrimNet Relationships: whose word counts, person by person (2026-11-21, night)

Asked whether the reading should be the standard everywhere, I said no: SkyrimNet Relationships weighs every exchange
as it happens, by that person's own character, with points that move slowly, and nothing of the heart begins there
unless the person raises it and the player answers. A reading of what was said afterwards is the poorer witness. But
that mod watches only followers and whom the player enrols; an innkeeper the player drinks with for a month was
nobody to it, and so nobody to IntelEngine. The user left the choice to me ("decide whatever would make the gameplay
feel more immersive and at more natural pacing"). The rule (`Bonds::See`, tested in `BondsCheck`):

- **Whoever that mod watches** (a member of its bond faction `SNRom_Bond`, 0xD63): its word, and nothing is read
  here. Its tier is read where tiers are read (`HeartBridge::HeartOf`, `CloseToPlayer`); new here is its **ill will**
  (`SNRom_Enmity`, 0xD64: 1 disliked and 2 resentful count as cold, 3 enemy as a foe): somebody that mod holds in
  ill will does not bring their trouble to the player, is no friend in a story's picture of the two, and no story of
  the heart is told about them.
- **Whoever it does not watch**: read here, for friendship and ill will only. The two questions of the heart are not
  put (`ask_heart` false: the prompts drop them and the choice "lover"; a "lover" answered anyway is a confidant), and
  nothing read of the heart earlier counts while that mod is installed. A courtship is that mod's to begin, by its
  own rules; it is not begun here behind its back.
- **No such mod**: as built the same day, the heart too.
- **The seam.** That mod starts everybody it takes on as a stranger: a friend of a month would be a stranger
  overnight, and stop being someone who may walk over to the player. What was read before stays, as a floor for
  friendship (a confidant at most) and for ill will, until that mod's own tier has reached it (for ill will: until
  it finds them on speaking terms), or its tier goes down from the highest it has shown (something happened that it
  saw), or it reports ill will itself. From then its word alone (`Bond::handed`, kept with the save; record version
  2, version 1 still read). Taken off its roster, they are read here again.
- A reading that leaves the heart out leaves alone what was read of it before: remove the mod and it counts again.
- Whoever is found watched is not asked about again for five minutes (`m_theirsUntil`), so a roster of a hundred
  costs a hundred lookups in five minutes, not one a look each.

**A defect of the first build, found only by playing it without the test switch:** whether that mod is installed was
never known. The check waited for the game's records with `LookupForm<RE::TESForm>`, which answers nothing for any
record (the typed lookup compares the form's type with "none"), so it stayed "too early to say" for ever, and in a
game with SkyrimNet Relationships everybody would have been read, the heart too. The test switch
(`DebugBondNoMod`) had stood in for the real thing in every run. Asked for as a faction now. The same pass found
that "installed" was judged by a file being in the Data folder (`LookupModByName`): now by being in the load order,
here and for the intimacy mods that open stories about an affair.

Played on the test profile, which has SkyrimNet Relationships, without the switch: the mod found (its factions
FE059D63 and FE059D64); Hulda, not on its roster, read by herself at her sixth line and again with the short
prompt ("friend / none / unanswered, sure 0.82": friendship and ill will only); put on its roster by console at tier
0 she still counted as a friend and a story's author was told so; at its tier 1 the same; at its tier 2 the log said
she is that mod's to speak for from now; at its tier 1 again she was an acquaintance by its word, nothing held her
up; its ill will 2 was read; taken off the roster she was a friend again as read. Saadia on the roster said six
lines and was not read for as long as she was on it; off it she was read at the next look ("acquaintance", unsure,
noted). Not played: a real enrolment through that mod's own hotkey and its points climbing by talk (the console set
its faction ranks), a follower, the full Isekai profile.

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
  dialogue menu) away; the event still comes. In a fight too, at the player's own risk: the one freed still cannot
  die and is still left alone by the place's people until they part from the player.
- **The crosshair says "Cut loose"** on someone held bound, where the game would say "Talk" (the user: "Yes,
  relabel"). Skyrim's scripts cannot change that word on a reference (`SetActivateTextOverride` is Fallout 4's), so
  the DLL does: `NpcActivateText` in ArcBridge.cpp replaces `TESNPC::GetActivateText` (virtual 0x4C, installed at
  the main menu by `Listen`; the plugin's first and only change to a function of the game), calls the game's own,
  and for the one held swaps the first line of what it wrote. Who is held is four numbers (`g_bound`, kept by
  `RefreshBound` whenever that changes and on every `Lapse`), compared without a lock, so looking at people costs
  nothing. The word is the name of a message of IntelEngine.esp (`IntelEngine_CutLoose` [0058E5], esp/EspBuilder),
  so a translated plugin file translates it; without the record it is "Cut loose". The notification that said
  what the key does is gone with it.
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
  - Before asking the witness: standing on top of her does nothing; the crosshair reads "Cut loose / Ahlam" (and
    "Talk / Ahlam" again once she is free); the key on her, and the log says "cut loose by
    the player (before the story asked for it)"; she stands and speaks within the moment; two seconds later the
    talk with the witness is gone from the journal (objective never completed, hidden), "Find Ahlam and set her
    free" is completed, and "Hear from Ahlam why she came here" is shown with the marker on her. No dialogue menu.
  - The talk with her read by the judge; "Go to Nazeem's house": the marker on the door of the Drunken Huntsman,
    where he lodges (found in 0.01 ms), done on walking in; the choice, the ending, the reward.
  - Left behind at the camp by a player gone ahead, she went her own way; come out with the player, she parted at
    Whiterun's gate with a word of her own.
- Not played: a search of a home before and after the thing is put there; someone the current step is about
  switched off (their door); an engine quest whose marker moves; cutting someone loose in the middle of a fight.

## A hunt for what is killing people: remains to examine (2026-11-19, late)

The user's measure for the dungeon master's stories: "If our story arc rivals witcher 3 quests where an npc hears
about some kind of monster or ghost and the player explores to find the cause of deaths and mysteries like in the
witcher, then we have a perfect system". The shape of such a contract is a genre (someone who wants it found out,
witnesses who saw part of it and may be wrong, what was left of a victim, working out what did it, facing it, and
what that overturns among people). Nothing of the Witcher's own is used, and no quest of the game is retold.

What the engine had: the asker, talks the judge reads, a place out in the world with a master of the place's own
kind, a thing on the master's body, people-steps after the place, a choice, endings that last. What it lacked: a
clue in the world. That is this piece.

### The step `"examine"` (Arcs.h `Kind::Examine` 14, `Remains`, `ClueState`, `kClues` 2, co-save 8)
- `{"do": "examine", "place": "dX", "what": "a hunter" | "a cow" | "a horse" | "a goat" | "a deer", "name": "Torvald",
  "clue": "what is seen on it"}`. What was left of a victim lies outside the place; the player looks at it closely.
- **Fair by construction.** The writer is given, for every place, the signs its kind leaves on those it kills
  (`SiteSigns`, TaleAuthor.cpp: a vampire's two punctures and bloodless pallor, a troll's bites and stove-in ribs,
  a bandit's plain steel and the missing purse...), and writes the clue from them. The compiler requires a `slay`
  at that same place after the `examine`, so what did it is what lives there. Which master a den has (a spider or
  a troll) is fixed before a word is written: the story's number is drawn when the story is asked for
  (`Request::seed`), and `Form "bossbase"` picks by `Mix(seed, place)`, the same sum the writer was told by.
- The signs never say what the eye would contradict: the body that lies there is whole and clothed, so they speak
  of wounds, colour, smell and what clings to it, never of anything torn off (the first hunt the model wrote said
  "limbs torn off" of a body in one piece).
- A place qualifies when it has ground under the open sky by its way in: its marker on the map
  (`Site::outside`, `OutsideOf`).

### In the game
- **Laid** by the script (`PlaceClues`, on the same signal 14 that places a master) once the marker's cell is in the
  game and the story has not been past the step (`ClueWanted`): the game's own hunter (a man: `EncHunterNordM`) or a
  beast, put right at the marker (where a traveller arrives, so it is ground one can stand on), killed only once
  it is really there (`Is3DLoaded`, then `KillSilent`), named as the story names the dead one. While the player
  stands within 2500 units it waits (eight looks at most). One that the game has cleared away is laid anew.
- **Examined** by the player's own hand: activation is blocked, the crosshair says "Examine" (the same hook as "Cut
  loose"; the word is the message `IntelEngine_Examine` [0058E6]), `ArcBridge::Activated` makes it Seen and sends
  `IntelEngine_ArcClue`, and the script shows what is seen in IntelEngine's own window ("What you see"; "Search
  the body" / "Stand up", Esc stands up). The game's plain message box showed no words at all in the test profile.
  After that the body is searched like any other.
- **Done early is done**: looked at before the story asked (the player came on it first), the step is done the
  moment the story comes to it.
- **The marker**: on the remains once they lie there, until then on the place on the map (`MarkerOf`).
- **When it is told**: on offer in any story whose places have ground outside; never two hunts running; asked for
  outright by `ArcRequestHunt` (`TaleSeed::kind == "mystery"`, `Pool::mustExamine`, the prompt's "Something is
  killing near ..."), which is what a rumor of deaths will call. Test hook: `IntelEngine_Arcs.DebugHunt(asker)`.

### Two things the play showed about every story
- **The journal said the same thing again and again.** Each time a marker moved or something was laid in the
  world, the step was shown anew, and the objective came up on the screen once more (the user: "The quest title
  appeared multiple times"). The marker now moves without a word (`MoveMarker`; the compass follows the alias by
  itself: seen, the arrow went from the cave's marker onto the body with nothing announced). The engine's quests
  the same.
- **A master killed before its step** (the user: "What would have happened if the vampire got killed before
  talking to Elrindir"): the kill counts the moment the story comes to that step, and the steps before it are
  still asked for, because who sent the dead man there and what his body shows are still worth knowing. "Seen
  alive" is now kept with the story (`siteState` bit 3): killed away from the heart of its place, then a save and
  a load, the kill had counted for nothing until the player went down to the heart.

### Played (test profile)
- A hand-written hunt, offer to ending: Anoriath does not believe wolves killed a hunter; the body by Broken Fang
  Cave ("Examine / Hunter"; the window: two small wounds at the throat, white as tallow); looked at before anyone
  was asked, and the step done at once when the story came to it; the cave fought through on foot to its master;
  the choice; the reward handed over; the rumor in the hold.
- **A hunt the dungeon master wrote itself**, first attempt ("The Hunter Who Knew the Wood"): a seer who saw "a
  great shape, bent low and dragging something", an innkeeper who heard a boast, the remains outside Graywinter
  Watch with a troll's signs, the troll, a brother who had sent the hunters there for the troll's fat to clear a
  debt, and three ways to tell it. Played through both witnesses and the examination; the asker held back while a
  witness stood by the player, and brought it when they next met.
- Not played: the model-written hunt's kill and ending; a beast as the remains; remains cleared away and laid
  anew; the body's name (added after the play).

### Naming what did it (the step `"name"`, 2026-11-20)
- `{"do": "name", "place": "dX", "with": "pX", "question": "...", "truth": "...", "others": ["...", "..."],
  "right": {"then", "journal"}, "wrong": {"then", "journal"}}`. Having looked at what was left, the player tells
  someone of the story what did it: the true answer, and two that people of the story believe or would rather
  believe (what the guard says, what the one with something to hide puts about). The compiler makes it a choice
  like any other (`Kind::Choose`, `Step::naming`), lays the three answers out in an order that follows from their
  own words, and wants an `examine` by that place before it and the `slay` there after it.
- **Worth something**: the branch of the true answer carries `Effect::Remedy` (12): whoever was told gives the player
  two of what helps against what lives in that place (`ArcBridge::RemedyFor`, the game's own: Cure Disease where
  vampires den, Cure Poison for spiders and Falmer, Resist Frost for a tomb's overlord, a scroll of Firebolt for a
  troll, ...; `Form "remedy<s>"`, the script's `GiveRemedy`). A wrong answer costs that help and nothing else.
- The truth itself is the engine's (the place's kind); its wording is the writer's, in the story's own language.
- Shown by the story's ordinary choice window: with three answers the third stands behind "Something else" (Escape
  must never choose an answer). A window with all three side by side would serve this step better.
- Played (a hand-written hunt): the body named ("Examine / Torvald"; the window headed TORVALD), the question at
  Anoriath's stall with the vampire as the second of three, answered rightly: two Potions of Cure Disease, and
  the hunt as the next objective. Not played: a wrong answer; a hunt with this step written by the model.

### A place that was just emptied (2026-11-20)
The user: "what would happen if you clear the broken fang cave but another quest makes you go to the same place
later. Will intelengine spawn new monsters and boss? Or will it stay empty".
- A later story that leads there puts its own master at the heart, of the place's kind and with the story's name,
  as for any place; where the game marks the place as cleared, two of its kind come with it (`PlaceGang`), and the
  writer is told "Cleared once already; something new has moved in". The rest of the place is the game's: empty
  until the game's own respawn. A quest of the game that goes there gets nothing from IntelEngine, and a place the
  journal still needs is never used.
- New: where the last four stories led rests (`restedPlaces`, TaleAuthor.cpp), as long as two other places are
  near enough to choose from. Nothing held the dungeon master back from sending the player into the cave they had
  just emptied; and a master that a lapsed story left alive there would have got a second one beside it.
- Not built: taking a story's master out of the world when its story lapses without the kill. He stays where he
  was put, with the story's name, until the game clears the place.

### Still to come for a hunt to stand beside the best of them
- Things that only show themselves at night, and a ghost with a grievance.
- A scene by the town (where the dead were found), tracks leading from it, more than bodies to look at.
- The hook: a rumor of deaths heard at an inn, leading to the one who will pay to have it found out.

## Something that walks by night: hauntings (2026-11-20)

The user (2026-11-20): "Night and ghosts and such mystery quests that are well written and immersive with witcher 3
vibes and a bit spooky so we have more variety."

### What it is

A step of a tale, "haunt" (`Kind::Haunt`): outside one of the story's places the shade of someone who died there walks
between nightfall (21:00) and first light (05:00). By day there is nothing to see. The story is about why it does not
rest, and that is always something the living did.

- **It is spoken with like anyone.** The shade is an actor of the game's own ghosts (a woman of Yngvild's dead, a man
  of the cairns' dead; Skyrim.esm, leveled), named by the story. What it knows is the "truth" the writer gave it, first
  person, put into its prompt by `ArcBridge::LinesFor` together with what it is: not among the living, bound to the
  spot, gone at first light, and what would let it rest. Played: "I was Ingerd... I lay on these rocks last winter with
  a broken ankle, and the snow came. An elf took my mother's necklace and left me. I want it back."
- **It harms nobody who leaves it be.** The script takes it out of its factions and its anger before anyone sees it.
- **Two ways to end it**, both prepared by the writer:
  - `banished` (always there, so a story can never be stuck): the player strikes it down. The game's own "Ghostly
    Remains" are left. Never the good ending by itself: the wrong stays unrighted.
  - `rested` (when "wants" names an item of the tale): with that thing in their pack the crosshair on the shade reads
    **Lay to rest** (MESG `IntelEngine_LayToRest` [0058E7], the same hook as "Cut loose" and "Examine"). The player's
    hand on it takes the thing out of their pack for good, the shade says its last words, and fades. Without the
    thing their hand passes through it, and it answers that in its own way.
- **Fair by construction** (Tales.h): one haunting a story; the place must have open ground outside; the shade has a
  name, a sex and a truth of its own; "rested" needs "wants", and what it wants must be in the player's hands on some
  way before the shade is reached (a "find" for it, or someone handing it over). Not in two stories running, never in
  a story of the heart.
- **Out of order counts**, as for remains and captives: the shade walks from the moment the story is taken on, while
  the story has not been past it. A player who goes there on the first night, before asking anyone, meets it, hears
  what it wants ("An elf with a bow left me on these rocks. He took my mother's necklace. Find it, and I may rest."),
  and may strike it down there and then; the step is done when the story comes to it.

### How it runs

- Arc fields (co-save record version 9): `spiritSite`, `spiritWoman`, `spiritWants`, `spiritState`
  (`SpiritAway`, `SpiritOut`, `SpiritBanished`, `SpiritRested`), `spiritRef`, `spiritName`, `spiritTruth`.
  `Facts::spirit` carries the state; `If::Done` on a haunt is "banished", `If::Rested` is "laid to rest".
- The look every two seconds (`ArcBridge::Advance`) sees a shade that died (banished, whoever struck it) or that the
  game lost (it walks again), and asks the script (signal bit 14, the same as for masters, captives and remains) to
  put it out when it is night and the ground by the place is in the game, or to take it away when the sky is light.
- `IntelEngine_Arcs.TendSpirit`: placed by the place's map marker, or 900 units behind the player when that spot is in
  plain view close by, fading in either way (it is a shade: that is how it should arrive). At first light it fades
  unless it is fighting. `OnArcSpirit`: the player's hand on it ("rest" or "touch"). `ReleaseSpirit` when the story ends.
- The journal's marker stands on the shade while it is out and on the place by day (`MarkerOf`).
- Cost: a lookup and two compares a look; nothing per frame. In the session that played it the slowest look at a
  story in 150 was 0.36 ms (the budget is 0.5).

### Asked for

- The dungeon master may write one whenever a place near has open ground (`bar_haunt` false); the prompt's step list
  then offers "haunt".
- `ArcRequestHaunt(giver, rumored)` asks for exactly that or nothing (`must_haunt`, the section "Something walks by
  night near <town>" of the prompt): people who will not speak of the dead one, the one thing the dead one wants in a
  living person's keeping, the shade by night, and an ending among the living. Test hook `DebugHaunt`.

### Played (2026-11-20, test profile)

"The Woman at Graywinter" (hand-written, `Scripts/intelengine/tales/test_q.json`): Olava told what she had seen
("pale as frost against the rocks, one hand forever at her own throat"), Elrindir gave up the necklace when faced with
it, the shade stood by the cave mouth after dark with the journal's marker over her, the crosshair read "Lay to
rest", and her last words were her own ("I can let go now. Be well, warrior. Don't forget me."). In a second run,
without the necklace: reached for, she said what she wanted; at 05:12 she faded and at 21:30 she walked again with the
player standing there; struck down, she left Ghostly Remains and the state was Banished. Model-written on the second
attempt: "The Hunter Who Was Never Buried" (compiled, not played through).

### Not yet

- A haunting inside a home in town, and one that only shows itself to someone who has first looked at the remains.
- The shade of a victim of a hunt as an optional witness at night.
- Other things of the night that are not the dead (what only comes out after dark at a place).
