# Dashboard strings

Every text the dashboard (`web/dashboard/src`, built to `Magelight/IntelEngine/dashboard` and `PrismaUI/views/IntelEngine/dashboard` of the game repo) and the choice popup (`web/choice/index.html`) show, as written in the sources on 2026-11-23. The dashboard has no string table: every text is written into its components in English, and none of it goes through a language pack. This list is drawn from the sources by a script (string literals that read as words, and the text between tags), so it holds a few false hits (values compared in code) and may miss text built from pieces; it is a translator's starting point, not a table the page reads. The words the page gets from the DLL (story types, states, names) are not here.

Settings and their choices carry the names of SkyrimNet's plugin settings page (`SettingsBridge.cpp`, `manifest.yaml`; `RunSettingNamesCheck.py` compares them): a translation has to keep the three in step.


## web/dashboard/src/App.jsx

| line | text |
|---:|---|
| 11 | Tasks |
| 11 | What people are doing for you |
| 12 | Story |
| 12 | The dungeon master and its stories |
| 13 | World |
| 13 | Talk of the day, your stories, your people |
| 14 | Politics |
| 14 | Factions, wars and alliances |
| 15 | Director |
| 15 | Stage an event yourself |
| 16 | Actions |
| 16 | What NPCs are allowed to do |
| 17 | Settings |
| 17 | Tune IntelEngine |
| 293 | IntelEngine |
| 294 |  · ${scheduledCount} scheduled |
| 317 | LB / RB pages · A select · B back |
| 318 | Arrow keys move · Page Up / Down pages · Esc closes |
| 329 | Refresh |
| 329 | &#x21BB; Refresh |
| 330 | Close |
| 330 | &#x2715; Close |

## web/dashboard/src/index.jsx

| line | text |
|---:|---|
| 8 | ${msg} at ${src}:${line}:${col} |

## web/dashboard/src/components/ActionsTab.jsx

| line | text |
|---:|---|
| 22 | No actions loaded. |
| 31 | IntelEngine Actions |
| 46 | !border-l-4 !border-l-amber-500 [&>div>div:first-child]:!text-sm [&>div>div:first-child]:!font-bold |
| 46 | ml-4 !w-[calc(100%-1rem)] |
| 55 | {{ npc.name }} |
| 55 | the NPC |
| 59 | OFF (category) |
| 69 | Other Mod Actions |

## web/dashboard/src/components/DirectorTab.jsx

| line | text |
|---:|---|
| 21 | Needed for one of the sub-types |
| 25 | Clear ${label} |
| 28 | Resize ${label} |
| 70 | Clear faction |
| 90 | no nearby NPC matches |
| 90 | No matches |
| 98 | Seek Player |
| 99 | Informant |
| 100 | Road Encounter |
| 101 | Ambush |
| 102 | Stalker |
| 103 | Message |
| 104 | Quest |
| 108 | Interaction |
| 109 | Gossip |
| 113 | Trade Deal |
| 114 | Diplomatic Gift |
| 115 | Alliance Proposal |
| 116 | Peace Offer |
| 117 | Insult |
| 118 | Trade Dispute |
| 119 | Embargo |
| 120 | Espionage |
| 121 | Territory Dispute |
| 122 | Betrayal |
| 123 | Border Skirmish |
| 124 | Assassination |
| 125 | Sabotage |
| 126 | Brawl |
| 127 | War Declaration |
| 128 | Surrender |
| 136 | Walk |
| 137 | Jog |
| 138 | Run |
| 141 | No |
| 142 | Yes |
| 148 | e.g. set out to repay an old debt |
| 148 | Travels to find you for a personal reason. |
| 148 | Why they sought the named character (past tense, under 120 characters); do not claim the character’s location. |
| 149 | Comes to tell you something they saw or heard. |
| 150 | Subject |
| 151 | e.g. Camilla Valerius |
| 152 | Required. The NPC the rumour is about; their name must appear in the Rumor. |
| 153 | Source |
| 154 | e.g. Sven |
| 155 | Who told the informant (may be the Subject); empty = witnessed firsthand. |
| 156 | Rumor |
| 157 | e.g. Camilla Valerius was seen arguing with Adrianne over an unpaid debt |
| 158 | Required. One full past-tense sentence that names the Subject. Must start with a name, not a verb or an -ing word. |
| 160 | e.g. set out to sell pelts in Helgen |
| 160 | Appears on the road ahead of you, travelling to Destination on their own errand (outdoors only). |
| 160 | Why they are travelling, e.g. set out to sell pelts in Helgen. |
| 161 | Destination |
| 162 | e.g. Falkreath, Solitude, Old Hroldan |
| 163 | Where they are heading. Required: a real Skyrim location. |
| 165 | e.g. was hired by the Thieves Guild to collect a debt |
| 165 | The NPC above turns hostile and attacks you (outdoors only). Pick someone who can fight. |
| 165 | Why they wanted to attack the named character, and who hired them if anyone. |
| 166 | Hired by |
| 167 | e.g. Maven Black-Briar |
| 168 | Optional. An NPC or a faction (its leaders learn they ordered it). |
| 170 | e.g. confessed to obsessive pursuit out of jealousy |
| 170 | Secretly follows you out of romance, jealousy or obsession; flees when caught (outdoors only). |
| 170 | The confrontation when caught, not their hiding; include their motive. |
| 171 | Encouraged by |
| 172 | e.g. Faendal |
| 175 | e.g. wanted to arrange a private meeting about a debt |
| 175 | Sends a nearby courier to deliver a message from the selected NPC. |
| 175 | Why the sender wanted contact. |
| 176 | Message content |
| 177 | e.g. I need to speak with you about a private matter |
| 178 | Required. The message text. The NPC you pick is the sender; IE picks a nearby courier. |
| 179 | Meeting place |
| 180 | e.g. The Bee and Barb, Dragonsreach |
| 181 | Optional. With a Meeting time, creates a meeting invitation. |
| 182 | Meeting time |
| 183 | Optional. Must match the Message content. Avoid night/midnight for palaces/shops. |
| 185 | e.g. needed help rescuing a friend from Fort Greymoor |
| 185 | Sends an NPC to ask for help with combat, a rescue or finding an item; leaders use a courier. |
| 185 | Why help is needed, consistent with the quest location and sub-type. |
| 186 | Sub-type |
| 187 | Required. combat = kill enemies, rescue = save captive NPC, find_item = retrieve item. |
| 188 | Quest location |
| 189 | e.g. Bleak Falls Barrow, Fort Greymoor, Valtheim Towers |
| 190 | Required. A real Skyrim dungeon, fort, ruin or camp, not where you are now or a player home. Enemies spawn here. |
| 191 | Enemy type |
| 192 | Required. bandit = forts/camps, draugr = Nordic ruins, dragon = open areas. |
| 193 | Quest giver |
| 194 | e.g. Jarl Balgruuf |
| 195 | Optional. If set, NPC becomes a courier. Required for Jarls/leaders (they never travel). |
| 196 | Plea for help |
| 197 | e.g. Draugr have been crawling out of the barrow at night |
| 198 | Required. Why they need help. Shown in NPC dialogue on arrival. |
| 199 | Victim (rescue) |
| 201 | Required for rescue. Exact NPC name. Must exist in-game or quest is rejected. |
| 202 | Item (find_item) |
| 203 | e.g. Ebony Blade, Glass Sword, Spell Tome: Fireball |
| 204 | Find_item only. Exact in-game item name. Fallback to random if not found. |
| 205 | Item description (find_item) |
| 206 | e.g. a legendary elven blade, a powerful spell tome |
| 207 | Required for find_item. Brief narrative description for dialogue context. |
| 219 | New speed |
| 221 | Required. Changes speed mid-task without canceling it. |
| 225 | e.g. Whiterun, Bannered Mare, home |
| 226 | Required. Named location, "home", "<Name>\'s home", or relative ("upstairs", "outside"). |
| 227 | Speed |
| 229 | Travel speed. Empty = Walk. |
| 230 | Wait for player |
| 232 | Yes = player told NPC to go (lingers at destination). No = NPC\'s own decision. |
| 235 | Person to fetch |
| 236 | e.g. Camilla Valerius, Alvor |
| 237 | Required. Exact NPC name. Agent goes alone, returns with them. |
| 238 | Refuse reason |
| 239 | e.g. "I\'m on duty." |
| 240 | Optional. Leave empty and the person comes along. If filled, they refuse to come and give this reason. |
| 243 | Person to find |
| 244 | e.g. Lydia, Faendal |
| 245 | Required. Exact NPC name. Agent leads the player TO the target (travel together). |
| 251 | Deliver to |
| 252 | e.g. Jarl Balgruuf, Camilla |
| 253 | Required. Exact NPC name. Agent goes alone to deliver the message. |
| 255 | e.g. Your presence is requested at Dragonsreach |
| 256 | Required. The words to relay. Shown in NPC dialogue on arrival. |
| 259 | Optional. If message is a meeting invitation, the location. Empty = not a meeting. |
| 261 | Optional. If message is a meeting invitation, the time. Empty = not a meeting. |
| 264 | Person to escort |
| 265 | e.g. Camilla Valerius, Sven |
| 266 | Required. Exact NPC name (never the player). Agent walks WITH them. |
| 269 | Optional. Where to escort them. Empty = "home". |
| 270 | Wait at destination |
| 272 | Yes = escorted person waits for player at destination. No = resumes normal life. |
| 276 | e.g. Bannered Mare, home |
| 277 | Required. Where to go when the time comes. Location name or NPC name (resolves to their home). |
| 278 | When |
| 279 | e.g. at dawn, in 2 hours, tomorrow morning |
| 280 | Required. "at dawn", "after sunset", "in 2 hours", "tonight", "tomorrow morning". |
| 285 | Required. Exact NPC name. Agent stays put now, fetches them later. |
| 287 | e.g. at dawn, tonight, in 3 hours |
| 288 | Required. When to go fetch them. |
| 293 | Required. Exact NPC name. Agent stays put now, delivers message later. |
| 295 | e.g. The Jarl requests your presence |
| 296 | Required. What to tell them when the time comes. |
| 298 | e.g. at dawn, after sunset |
| 299 | Required. When to deliver the message. |
| 302 | Optional. If message includes a meeting request. Empty = not a meeting. |
| 304 | Optional. If message includes a meeting time. Empty = not a meeting. |
| 310 | NPC 1 memory |
| 311 | e.g. confronted Sven about stolen goods |
| 312 | Required. What NPC 1 remembers. Past-tense verb phrase, no subject prefix. |
| 313 | NPC 2 memory |
| 314 | e.g. was accused by Mikael of stealing goods |
| 315 | Required. What NPC 2 remembers. Past-tense verb phrase, no subject prefix. |
| 320 | Required. Who or what the rumour is about (an NPC, a faction or you). |
| 322 | e.g. was overheard arguing with the steward about missing tribute |
| 323 | Required. What the Subject did. Past-tense verb phrase; their name is added automatically. |
| 358 | Stopped |
| 370 | Invalid response; try again. |
| 372 | Invalid result |
| 376 | Generation failed; try again. |
| 387 | Destination must be a place |
| 390 | Unknown field |
| 397 | Invalid type |
| 398 | Duplicate NPC |
| 399 | Preserved or unchanged |
| 400 | Wrong actor |
| 402 | Placeholder text |
| 403 | Invalid target NPC |
| 404 | Invalid option |
| 405 | Invalid identity |
| 406 | Invalid nearby NPC |
| 407 | Invalid relation change |
| 408 | Identities must be distinct |
| 422 | Does not fit the event type |
| 427 | CancelCurrentTask |
| 427 | ChangeSpeed |
| 451 | Pick a nearby NPC or clear the Action NPC field. |
| 482 | ${g.hint} ${g.narration} |
| 561 | GoToLocation |
| 561 | FetchPerson |
| 561 | DeliverMessage |
| 561 | EscortTarget |
| 561 | SearchForActor |
| 562 | ScheduleFetch |
| 562 | ScheduleDelivery |
| 562 | ScheduleMeeting |
| 562 | ReportPlayerConduct |
| 601 | Informant NPC |
| 601 | Story type |
| 601 | Narration |
| 602 | NPC 1 |
| 602 | NPC 2 |
| 602 | Social type |
| 607 | Faction A |
| 607 | Faction B |
| 607 | Political event type |
| 607 | Description |
| 608 | Fill required fields: {...} |
| 612 | ${engine.traveler} → ${engine.target} |
| 614 | — busy: {...} |
| 616 | Story Dispatch |
| 616 | NPC Social Dispatch |
| 616 | Political Dispatch |
| 616 | Action Execution |
| 618 | Generating… |
| 618 | Generate |
| 619 | Stop |
| 619 | Clear |
| 697 | Action |
| 723 | Exact display name. |
| 729 | e.g. Saadia |
| 733 | What they will do. |
| 746 | Why the NPC acts; used as story context. |
| 751 | e.g. set out to buy supplies |
| 796 | Busy — confirm replacement |
| 796 | sent:dispatchStory: |
| 796 | Sent |
| 796 | Replace Story |
| 796 | Dispatch Story |
| 807 | The traveler/teller. |
| 813 | e.g. Mikael |
| 817 | The counterpart/listener. |
| 827 | Interaction or gossip. |
| 840 | What happens between them. |
| 845 | e.g. confronted him about the stolen shipment |
| 871 | sent:dispatchNpcSocial: |
| 871 | Replace Social |
| 871 | Dispatch Social |
| 882 | Surrendering faction |
| 882 | The side that gives up. Needs an active IE war with the victor. |
| 882 | First side. |
| 887 | e.g. StormcloakFaction |
| 890 | Victor |
| 890 | The side that wins. |
| 890 | Other side. Pick two different faction IDs. |
| 895 | e.g. ThalmorFaction |
| 900 | Event type |
| 900 | What happened. |
| 912 | Relation delta |
| 912 | Change, -15 to 15. |
| 924 | A short account of the political event. |
| 929 | e.g. Imperial spies intercepted dispatches near Windhelm |
| 939 | sent:dispatchPolitics: |
| 939 | Dispatch Political Event |
| 942 | These two can't go to war. Only pairs allowed by war_allowed in factions.yaml fight wars (by default, military factions only). |
| 951 | NPC (nearby only) |
| 951 | Nearby actor who performs the action. |
| 952 | Action NPC |
| 952 | e.g. Guard 1 |
| 958 | Command to run. |
| 964 | Select action... |
| 980 | Select faction... |
| 1007 | Reason |
| 1007 | Optional. Why the NPC does this; they remember it and can explain it. |
| 1013 | sent:executeAction: |
| 1013 | Busy — you sure? |
| 1013 | Execute Action |

## web/dashboard/src/components/FactionEditor.jsx

| line | text |
|---:|---|
| 7 | Military |
| 7 | Political |
| 8 | None |
| 8 | Brawls |
| 8 | Sabotage |
| 8 | Assassination |
| 8 | Through others |
| 18 | Faction |
| 63 | Remove |
| 96 | Reading your load order {...} |
| 115 | Working… |
| 115 | Save |
| 116 | Discard changes |
| 117 | Not saved yet. Your changes are kept while you look at other pages. |
| 117 | Saved factions are in use at once. The old file is kept as factions.yaml.bak. |
| 119 | Not saved: {...} |
| 120 | Saved. |
| 128 | New faction |
| 131 | + Add a faction |
| 135 | Name |
| 136 | The Silver Hand |
| 138 | Id |
| 138 | What the file and other factions call it. Made from the name; change it only if you need to. |
| 138 | Fixed once saved: relations and history are kept under it. |
| 142 | Kind |
| 145 | Seat |
| 145 | The city it is at home in. |
| 148 | Faction of the game |
| 149 | Type part of a name or a mod: every faction of your load order is offered. Its members count as this faction’s people. May stay empty. |
| 150 | Search your load order… |
| 152 | Leaders |
| 152 | People of the game, by name. A faction whose leaders are all dead is treated as gone. |
| 153 | Add a leader… |
| 155 | Soldiers |
| 155 | The list its soldiers are raised from in a battle: every leveled list of people in your load order is offered. Empty: its first ally's soldiers. |
| 159 | Army strength |
| 159 | 0 to 500, as the dungeon master weighs it. |
| 161 | War below |
| 161 | Relation at which it is at war (-100 to 0). |
| 164 | How it fights when it is not at war |
| 168 | Rivals |
| 169 | Allies |
| 171 | Free once these quests of the game are finished |
| 171 | Quest editor ids (C06, TG09…). Until then it goes to no war and nobody of it is harmed. Left alone, the game’s own guilds wait for their own questlines; add a line here to say otherwise for this faction. |
| 172 | Add a quest id and press Enter… |
| 173 | Use the game's own rule again |
| 175 | Remove this faction |

## web/dashboard/src/components/GameTime.jsx

| line | text |
|---:|---|
| 3 | Morning Star |
| 3 | Sun's Dawn |
| 3 | First Seed |
| 3 | Rain's Hand |
| 3 | Second Seed |
| 3 | Midyear |
| 3 | Sun's Height |
| 3 | Last Seed |
| 3 | Hearthfire |
| 3 | Frostfall |
| 3 | Sun's Dusk |
| 3 | Evening Star |
| 19 | ${date + 1} ${MONTHS[month]}, 4E ${year} ${String(Math.floor(minutes / 60)).padStart(2, '0')}:${String(minutes % 60).padStart(2, '0')} |

## web/dashboard/src/components/PackagesTab.jsx

| line | text |
|---:|---|
| 5 | Travel (Walk) |
| 6 | Travel (Jog) |
| 7 | Travel (Run) |
| 8 | Travel (Stalk) |
| 9 | Sandbox |
| 10 | Sandbox (Near Player) |
| 23 | IE packages |
| 26 | No NPCs currently running IntelEngine packages. |
| 35 | Unknown |
| 46 | Removing\u2026 |
| 46 | Remove |

## web/dashboard/src/components/PoliticsTab.jsx

| line | text |
|---:|---|
| 17 | Independent #9aaba7 |
| 17 | Empire #c45454 |
| 17 | Stormcloaks #6095d0 |
| 17 | Aldmeri Dominion #d3bc61 |
| 17 |  (Political Engine side) |
| 20 | Military |
| 22 | Political |
| 28 | War declared |
| 29 | Battle |
| 30 | Your part in it |
| 31 | Your own side struck |
| 32 | Armies gather |
| 34 | Skirmish |
| 35 | Brawl |
| 36 | Surrender |
| 37 | War over |
| 38 | Peace |
| 39 | Alliance |
| 40 | Pact |
| 41 | Marriage |
| 42 | Attempt on a life |
| 43 | Assassination |
| 44 | A leader falls |
| 45 | Spying |
| 46 | Sabotage |
| 47 | Your doing |
| 52 | The civil war is undecided: both sides are in the field. |
| 53 | A truce holds between the Empire and the Stormcloaks. |
| 54 | The civil war is over: the Empire holds Skyrim. |
| 55 | The civil war is over: the Stormcloaks hold Skyrim. |
| 59 | within the hour |
| 59 | about an hour |
| 59 | about ${Math.round(h)} hours |
| 87 | you are called to the line when their army gathers |
| 88 | ${perk.pay}% more pay after a battle won |
| 89 | ${perk.file} soldiers keep by you in battle |
| 90 | the line waits for your word to charge |
| 91 | your name is talked of beyond their own |
| 105 | ${plural(record.battles, 'battle', 'battles')}${record.won > 0 ?  |
| 106 | ${record.kills} cut down |
| 107 | champion slain |
| 107 | champions slain |
| 108 | captain slain |
| 108 | captains slain |
| 109 | life saved |
| 109 | lives saved |
| 111 | battle walked away from |
| 111 | battles walked away from |
| 128 | No rank honoured |
| 128 | No rank yet |
| 145 | ${ceiling - renown} more to ${nextTitle} |
| 145 | the top of their ladder |
| 156 | Known among them as {...} |
| 158 | Your rank: {...} |
| 160 | As {...} |
| 164 | Fight at their side, cut down their enemies' champions, save one of their leaders or do them a service: that is how a name is made. |
| 180 | Faction politics is disabled. Enable it in Settings. |
| 181 | Politics data not available. Open the dashboard in-game to see live data. |
| 233 | Neutral |
| 243 | Your Name |
| 246 | Nobody has a view of you yet. Take a side when two armies meet, do a faction a service or save one of its |
| 247 | leaders, and it will remember. |
| 265 | In the Field |
| 276 | drawn up, not yet fighting |
| 276 | a skirmish, now |
| 276 | fighting now |
| 281 | ${nameOf(battle.a)} · ${battle.standing_a} standing |
| 282 | ${nameOf(battle.b)} · ${battle.standing_b} standing |
| 286 | Your side: ${nameOf(battle.player_side)}${battle.player_kills > 0 ?  |
| 288 | You have taken no side. Walk up to a line and its captain will call to you. |
| 289 | You have taken no side. |
| 296 |  near ${g.place} |
| 317 | Active Wars |
| 336 | ${nameA} morale |
| 337 | ${nameB} morale |
| 341 | Strength: |
| 350 | Battle History |
| 356 | : isDefenderVictory |
| 367 | ${victorName} wins |
| 367 | Draw |
| 387 | Chronicle |
| 389 | Nothing has happened between the factions yet. |
| 400 |  ↔ ${nameOf(e.faction_b)} |
| 411 | Show the latest only |
| 411 | Show all ${events.length} |
| 420 | Faction Relations |
| 422 | No faction relations yet. Wait for the first political tick. |
| 426 | Nobody is at odds and nobody is close: every faction keeps to itself for now. |
| 440 | at war |
| 453 | Show only those that matter |
| 453 | Show all ${relations.length} (${relations.length - notable.length} neutral) |
| 462 | Factions |
| 470 | Broken: its leaders are dead |
| 472 | Leaders: {...} |
| 475 | Fallen: {...} |
| 478 | No war with it and no attempt on its people until that story of the game is over |
| 483 | Private: only you see this; NPCs and the AI don't |
| 484 | Governor (your puppet) |
| 484 | Vassal jarl (your vassal) |
| 494 | Edit factions |
| 497 | Add or change factions {...} |

## web/dashboard/src/components/Section.jsx

| line | text |
|---:|---|
| 4 | button, a, input, select, textarea |

## web/dashboard/src/components/Select.jsx

| line | text |
|---:|---|
| 118 | ${label}... |
| 126 | Clear ${label} |
| 146 | No matches |

## web/dashboard/src/components/SettingsTab.jsx

| line | text |
|---:|---|
| 8 | Allow all NPCs |
| 8 | Block civilians only |
| 8 | Allow followers only |
| 8 | Block all NPCs |
| 10 | Classic |
| 10 | Rumors |
| 10 | Lives |
| 12 | As before 4.0. |
| 13 | The world moves on while you are away and you hear about it, but followers you left wait where you left them. |
| 14 | Followers you left behind live lives of their own (work at an inn, an adventure, a trip) and you can come upon them later; people close to you miss you while you are away. |
| 16 | Never |
| 16 | If urgent |
| 16 | Realistic |
| 18 | Nobody turns up during a fight, a conversation, a scene or a menu. |
| 19 | Only something urgent may interrupt a fight, a conversation, a scene or a menu. |
| 20 | Now and then someone turns up at a bad moment, as in real life. |
| 22 | If important |
| 22 | Freely |
| 24 | NPCs who take part in a running quest stay where the quest needs them. |
| 25 | They may travel or move for an important story. The target of an objective you have open never moves. |
| 26 | They may travel or move for any story. The target of an objective you have open never moves. |
| 28 | Calm |
| 28 | Normal |
| 28 | Lively |
| 30 | About one notable moment every two game days. |
| 31 | About one notable moment every game day. |
| 32 | About two notable moments every game day. |
| 35 | No filtering: anyone may be sent to you out in the wilds. Above Classic nobody is sent into a dungeon after you whatever this says (they wait until you are out), and nobody is ever sent to another plane. |
| 36 | Farmers, merchants and other civilians are not sent out into the wilds after you; warriors and mages may be. Above Classic nobody is sent into a dungeon after you whatever this says (they wait until you are out), and nobody is ever sent to another plane. |
| 37 | Only people who can be followers are sent out into the wilds after you. Above Classic nobody is sent into a dungeon after you whatever this says (they wait until you are out), and nobody is ever sent to another plane. |
| 38 | Nobody is sent to you anywhere but in a settlement. |
| 41 | No filtering. All NPC types can visit you at home. |
| 42 | Civilians won\'t bother you at home. Warriors and mages can still visit. |
| 43 | Only NPCs who can be followers visit you at home. |
| 44 | Nobody visits you at home. |
| 47 | No restriction |
| 47 | Same hold (civilians) |
| 47 | Same hold (except followers) |
| 47 | Same hold (everyone) |
| 48 | Same town (civilians) |
| 48 | Same town (except followers) |
| 48 | Same town (everyone) |
| 50 | No restriction from this setting. Above Classic the director still keeps people of other holds at home unless they are close to you and the matter presses. |
| 51 | Civilians must be in the same hold as you. Warriors and followers can cross holds. |
| 52 | Everyone except followers must be in the same hold. |
| 53 | All NPCs must be in the same hold as you. |
| 54 | Civilians must be in the same town/city. Warriors can cross within the hold. Followers go anywhere. |
| 55 | Everyone except followers must be in the same town/city. |
| 56 | All NPCs must be in the same town/city as you. |
| 60 | Someone comes to see you |
| 61 | Someone brings a rumor |
| 62 | A meeting on the road |
| 63 | An ambush |
| 64 | Someone follows you unseen |
| 65 | A message is delivered |
| 66 | Someone has work for you |
| 69 | Disabled |
| 69 | Followers only |
| 69 | Everyone |
| 71 | No confirmation prompt. Action executes immediately. |
| 72 | Prompt only when active followers perform this action. |
| 73 | Prompt for all NPCs performing this action. |
| 76 | Go somewhere |
| 77 | Deliver a message |
| 78 | Fetch someone |
| 79 | Escort someone |
| 80 | Search for someone |
| 81 | Arrange a meeting |
| 82 | Fetch someone later |
| 83 | Deliver a message later |
| 107 | Ctrl |
| 108 | Shift |
| 109 | Alt |
| 123 | Settings not available. Open the dashboard in-game to see live data. |
| 131 | Stories |
| 136 | The Story Engine (the dungeon master): NPCs with unfinished business autonomously seek you out based on shared history. |
| 142 | Check interval |
| 145 | game hours |
| 148 | Not used at this level of the living world: the pace is "How often something happens" (Living world), and the dungeon master looks when you arrive somewhere, when you wake, and every three game hours besides. It is the pace at Classic. |
| 149 | How often (game hours) the dungeon master looks for somebody with a reason to come to you. At Classic this is the pace. Max 168 = 1 week. |
| 155 | Long absence |
| 160 | Minimum game days since last interaction before an NPC becomes a Story Engine candidate. An NPC who has just come to you is left alone for at least as long. |
| 163 | Longest journey |
| 168 | Max game days an NPC will travel before being teleported. |
| 171 | Move a stuck traveller along |
| 174 | A story NPC who cannot get on, or has been on the way for longer than Longest journey, is placed further along, out of your sight. When disabled, they give up instead. |
| 179 | Living world |
| 181 | How alive the world is |
| 188 | Interruptions at bad moments |
| 195 | Quest NPCs may move |
| 202 | How often something happens |
| 208 | Skipped time counts toward the pace |
| 209 | Off: waiting, sleeping and fast travel do not bring the next moment sooner. It also takes time actually played: eight minutes between two moments, four minutes in a town before a story of its own begins there, twenty minutes between two such stories. On: the game&apos;s clock alone counts: skip a day and a day&apos;s worth of moments is due. |
| 211 | A follower told to wait waits |
| 216 | How long after you told a follower to wait they start a life of their own, once their follower mod lets them go. While it still holds them, they keep waiting. |
| 219 | A follower left behind moves on |
| 224 | How long a follower you dismissed stays where you left them before starting a life of their own. |
| 229 | NPC Behavior |
| 231 | NPCs sent into dangerous places |
| 238 | NPCs sent into your home |
| 245 | NPCs among themselves |
| 247 | NPCs visit and talk to each other |
| 250 | NPCs who know each other meet without you: they gossip, argue, trade, and socialize independently. |
| 253 | Kind: one NPC goes to see another |
| 256 | Arguments, trades, conversations. The Director and the log call this type NPC Interaction. |
| 259 | Kind: gossip passed on |
| 262 | NPCs spread rumors among themselves. The Director and the log call this type NPC Gossip. |
| 270 | How often the NPC social system checks for NPC-to-NPC interactions. Max 168 = 1 week. |
| 273 | Rest per NPC |
| 278 | How long before an NPC can be picked for another social interaction. |
| 284 | Hold Restrictions |
| 286 | How far each kind of story may reach for its NPC. The defaults differ and are shown with each kind. |
| 292 | default: ${HOLD_POLICIES[type.def]} |
| 303 | Tasks & Meetings |
| 305 | NPCs on a task at once |
| 309 | Maximum number of NPCs that can be on tasks at once. |
| 312 | Wait at a destination |
| 317 | How long NPCs wait at destinations before returning home. |
| 320 | Visit ends when you walk away |
| 325 | How far you must walk before an NPC stops lingering. |
| 328 | Wait for you at a meeting |
| 333 | How long an NPC waits at the meeting spot before giving up. |
| 336 | On time until |
| 339 | game hours late |
| 341 | Arrival tolerance for meetings. Set higher if using variable timescales. |
| 344 | Report back after a delivery |
| 347 | Messengers return to you after delivering a message off-screen. |
| 352 | Ask before starting |
| 353 | Ask you before an NPC starts this task: never (Disabled), only for your followers, or for everyone. |
| 373 | What followers can be asked to do |
| 374 | All on by default. Switch one off to keep your active followers out of that action (it is refused without a prompt). |
| 386 | Auto Dynamic Bio |
| 387 | Automatically update NPC dynamic bios after X dialogue lines (per-NPC tracking) |
| 389 | Refresh an NPC's profile after much talk |
| 392 | When enabled, NPCs get their dynamic bio refreshed after a set number of dialogue exchanges. |
| 395 | Lines before a profile refresh |
| 399 | Number of dialogue lines per NPC before triggering a bio update. Counter resets after each update. |
| 404 | Quest kinds |
| 405 | Clear out enemies |
| 406 | A bounty on what lives at a place. Off: no bounty, and no master of a place to kill inside a town&apos;s story. |
| 407 | Rescue someone |
| 408 | Someone of the town held at a place out in the world. Off: nobody is held in a story. |
| 409 | Find an item |
| 410 | A keepsake lost at a place. Off: no keepsake hunt. |
| 411 | Fight a faction's enemies |
| 412 | Clear out hostile faction soldiers at a location. Rewards faction standing. |
| 413 | Rescue for a faction |
| 414 | Rescue a captive from hostile faction soldiers. Rewards faction standing. |
| 415 | Join a faction's battle |
| 416 | A friendly faction invites you to join an upcoming battle. Requires high standing (40+). |
| 419 | Quest expires after |
| 424 | Days before an unfinished quest auto-expires. |
| 429 | Faction politics |
| 436 | Factions autonomously trade, negotiate, scheme, and go to war. |
| 438 | Yes |
| 438 | No |
| 438 | Political Engine detected: {...} |
| 440 | Political Engine integration (experimental) |
| 443 | Off by default. Needs the Political Engine mod and does nothing without it. Load a save after turning it on: realms are added at game start and on load. |
| 444 | PE reactions move relations |
| 447 | When off, PE reactions are recorded without changing relations or player standing. |
| 454 | How often the Political DM evaluates faction relations. Max 168 = 1 week. |
| 457 | Largest change of a relation per check |
| 461 | Maximum relation score change per political tick. |
| 464 | Wars at the same time |
| 468 | Maximum number of simultaneous faction wars. |
| 472 | Plugin config not available. |
| 477 | Debug |
| 479 | Debug messages |
| 482 | Shows what IntelEngine does as notices on screen. The Papyrus log has it either way; for more in the DLL's own log set DebugLevel=4 in IntelEngine.ini. |
| 490 | Plugin Configuration |
| 493 | Blocklists & Whitelists |
| 495 | Faction Blocklist |
| 496 | Comma-separated EditorIDs (e.g. PrisonerFaction,BanditFaction:1) |
| 501 | Location Blocklist |
| 502 | Comma-separated location names |
| 507 | NPC Blocklist |
| 508 | Comma-separated NPC display names |
| 513 | Whitelists (empty = all allowed) |
| 516 | Faction Whitelist |
| 517 | Comma-separated faction EditorIDs. Only these factions' NPCs can be dispatched. |
| 522 | Location Whitelist |
| 523 | Comma-separated locations. NPCs only visit you at these locations. |
| 528 | NPC Whitelist |
| 529 | Comma-separated NPC names. Only these NPCs can be dispatched. |
| 535 | Language |
| 536 | not known yet |
| 536 | Now: {...} |
| 538 | Language of journal and messages |
| 539 | auto, or a language pack by name (russian, german, japanese, korean, chinese, french, spanish, italian, polish), or english |
| 544 | Languages people speak |
| 545 | auto (from SkyrimNet's translator settings), or names with commas between them (albanian, russian) |
| 551 | LLM Overrides |
| 552 | Leave empty to use base SkyrimNet config |
| 554 | API Endpoint |
| 555 | e.g. http://localhost:5000/v1 |
| 560 | API Key |
| 566 | Model Override |
| 571 | Temperature Override |
| 575 | 0 = use base config |
| 578 | Max Tokens Override |
| 585 | Timeout Override |
| 594 | Dashboard |
| 596 | UI Scale |
| 600 | Scale for high-DPI / 4K monitors. 1.5-2.0 recommended for 4K. |
| 648 | value ? "" : "" |
| 797 | VK ${value} |
| 803 | Dashboard Hotkey |
| 804 | Dashboard hotkey |
| 819 | Dashboard Modifiers |

## web/dashboard/src/components/SlotCard.jsx

| line | text |
|---:|---|
| 5 | Empty |
| 6 | Traveling |
| 7 | At Destination |
| 8 | Returning |
| 9 | Searching |
| 10 | At Target |
| 41 | State ${slot.state} |
| 47 | Unknown |
| 59 | Cancel task |
| 70 | Unknown NPC |
| 73 |  \u2192 ${slot.targetName} |

## web/dashboard/src/components/StoryTab.jsx

| line | text |
|---:|---|
| 8 | Someone comes to see you |
| 9 | Someone brings a rumor |
| 10 | A meeting on the road |
| 11 | An ambush |
| 12 | A faction's patrol hunts you |
| 13 | Someone follows you unseen |
| 14 | A message is delivered |
| 15 | Someone has work for you |
| 16 | NPC Interaction |
| 17 | NPC Gossip |
| 27 | Story engine data not available. Open the dashboard in-game to see live data. |
| 37 | Story Engine |
| 39 | Active |
| 39 | Disabled |
| 49 | Unknown NPC |
| 66 | No active story dispatch |
| 78 | Story types |
| 79 | What the dungeon master may start by itself. A type switched off is not in a story of the town either: no ambush or shadow inside one, and with &quot;Someone has work for you&quot; off none begins. Director dispatch can use any type. What NPCs do among themselves is under Settings, NPC Behavior. |
| 86 | The Director and the log call this type "${type.type}". |
| 108 | The Dungeon Master&apos;s Quest |
| 109 | A faction&apos;s quest written by the dungeon master. Stories of the town, bounties, keepsakes and ventures are on the World page. |
| 113 | Quest |
| 115 | In Progress |
| 118 | Quest giver: {...} |
| 121 | Location: {...} |
| 142 | Cancel Quest |
| 152 | NPC Social |
| 176 | No active NPC interactions |
| 185 | Recent NPC Activity |
| 215 | hide details |
| 215 | show details |

## web/dashboard/src/components/TasksTab.jsx

| line | text |
|---:|---|
| 25 | Active Tasks |
| 29 | No active tasks |
| 49 | Scheduled |
| 53 | No scheduled tasks |
| 73 | Dispatched |
| 74 | Meeting |
| 74 | Pending |
| 79 | Cancel scheduled task |
| 87 |  \u2192 ${sched.destination} |
| 90 | Schedule |

## web/dashboard/src/components/WorldTab.jsx

| line | text |
|---:|---|
| 9 | What was done |
| 10 | Someone needs help |
| 11 | An old companion |
| 12 | News of the land |
| 16 | something lost |
| 17 | a bounty |
| 18 | a venture |
| 19 | a request |
| 20 | a theft |
| 22 | a death |
| 23 | a story |
| 31 | just now |
| 32 | ${plural(Math.max(1, Math.round(days * 24)), 'hour', 'hours')} ago |
| 33 | ${plural(Math.round(days), 'day', 'days')} ago |
| 38 | time is up |
| 39 | ${plural(Math.max(1, Math.round(days * 24)), 'hour', 'hours')} left |
| 40 | ${plural(Math.round(days), 'day', 'days')} left |
| 51 | ${names.slice(0, -1).join(', ')} and ${names[names.length - 1]} |
| 61 | Nothing to show yet. Open the dashboard in-game to see live data. |
| 91 | Showing everything the world holds, heard or not. |
| 92 | Showing what you have heard and taken on. The rest is for you to find out. |
| 95 | Hide what I have not heard |
| 95 | Show everything (spoilers) |
| 102 | Talk of the Day |
| 122 | Talked of in ${list(r.heard_in)} |
| 122 | From ${r.from} |
| 123 |  · ${r.lead} is the one to see |
| 124 |  · going round here, not told to you yet |
| 124 |  · has not reached you |
| 131 | There is more being talked of |
| 131 | ${unheardHere.length} more things are being talked of |
| 145 | Your Stories |
| 151 | You have taken nothing on. People with a trouble of their own come to those they know, and the talk of the day says who needs help. |
| 157 | A story |
| 164 |  · for ${s.who} |
| 165 |  · ${s.where} |
| 166 |  · ${s.gold} gold offered |
| 174 | Someone |
| 181 | Done: go back and tell them |
| 181 | Still open |
| 182 |  · ${a.gold} gold offered |
| 188 | One more story is on its way to you. |
| 188 | ${onTheirWay} more stories are on their way to you. |
| 196 | Your People |
| 200 | Nobody has travelled with you yet. Those who do are remembered here, also after you part ways: they go on living. |
| 207 | Dead |
| 207 | With you |
| 207 | Waiting for you |
| 207 | Living their own life |
| 218 | What they have done since is theirs to tell, or the talk of the day's. |
| 227 | How People Stand With You |
| 231 | Nobody yet. Once someone has spoken with you a few times, how they stand toward you is read from what passed between you: an acquaintance, a friend, a close friend, or someone who has turned cold. |
| 243 | What is in their heart is theirs to show. |

## web/choice/index.html

| line | text |
|---:|---|
| 6 | IntelEngine choice |
| 57 | IntelEngine |
| 128 |  or arrows + Enter · Esc =  |
| 146 | No answer =  |

782 strings.
