IntelEngine story director: rules

Tags name the source; "(inferred)" marks my own derivation or rescaling. "min" = real minutes played outside menus (inferred: waits and fast travel skip game hours, so pace on play time; use game hours only for plausibility).

The ten that matter most

1. Tension meter, four states. Tension 0-100 rises with what the player feels: damage, being downed, a nearby killing, a major beat. It decays, but not while combat or a scene is live. Build (beats allowed) -> Peak (one more beat after crossing 70) -> Fade (nothing new until a natural break) -> Relax (no arc starts for 10-15 min, or until the player changes hold). Change frequency, not amplitude. [L4D, Booth GDC 2009: peak held 3-5 s, relax 30-45 s or until progress; rescaling inferred]

2. On/off cycle for slow and eventful stretches. Default "Cassandra": 40 min on, 50 off (her 4.6 : 6.0 days); 1-2 majors per on-phase (50/50); majors at least 15 min apart (her 1.9 days, 18% of the cycle). Minor beats on their own mean time, 25-40 min (her misc 4.8 days scales to 40). Jitter each phase x0.75-1.25. [RimWorld wiki; L4D "structured unpredictability", mobs every 90-180 s; scaling inferred]

3. Presets. Phoebe: 8 : 8 days (~70 min each), exactly one major per on-phase. Randy: no cycle, any beat every ~12 min, weights misc 3.5, arrival 2.4, big threat 1.4, small threat 0.6, stakes x0.5-1.5, a major forced after 13 days without one (1.8x his mean): here 2 h. [RimWorld wiki]

4. Grace and ramp. Minors only for the first 90 min of a new game, about one cycle (Cassandra's majors start day 11, after one weak raider on day 5). Stakes 0.7 until 1.5 h, rising to 1.0 by 6 h (raid starting factor 0.7 at 10 days, 1.0 at 40). [RimWorld wiki]

5. Attention gate. Each beat is a request {priority, expiry, attention cost}; send the highest only when the player is ready: not fighting, talking, in menus or on a main-quest stage, nor within 10 min of a cliffhanger (inferred). Expired beats resolve off-screen and return as rumor. [Watch Dogs Legion Player Attention System; off-screen part from a Nemesis design blog]

6. Weighted pick with an explicit "nothing". Tick every 2 min: fire with p = 1 - e^(-2/MTB) (inferred), then weight x trait fit (x2 typical) x novelty; the most specific matching storylet wins, broad ones are fallbacks. [CK3 forum: 900 nothing vs 100 event = 10%, x2 = 18%; Ruskin L4D2 dialog and Short "Beyond Branching", via summaries]

7. Repetition ledger. Record (type, lead NPC, place, climax verb). Novelty = 0.5^(starts of this type in the last 10). No type twice in a row or within 2 game days; an NPC leads one open arc at most and rests 3 game days after; no new major in a town within a game day of the last. A new arc differs from each of the last five in 2 of the 4 fields: players notice faces and verbs, not wording. [Compton's "10,000 bowls of oatmeal", secondary; numbers inferred]

8. Idle player. Same town, no combat, no quest change, tension under 15 for 40 min and no beat for 25 min: one cheap, ignorable local hook (rumor, visitor, letter) cast from people nearby. Ignored: wait twice as long; after two ignored, stop until the player travels or sleeps. Crafting or talking is not idle. [inferred from L4D relax and Randy's cap]

9. Five fact-gated stages. Setup (ignorable hook) -> complication -> reveal -> climax (player present, one major beat) -> aftermath (1-3 echo beats over 1-7 game days). Each stage unlocks only on game facts; store every beat's causes so the model can cite them; pick the beat whose tension fits the stage's target. One real choice per arc is enough. [Wildermyth wiki: hook quests intro/arrive/resolve, most stories one choice; Ryan via Short: contingent unlocking, causal bookkeeping; Facade beat selection; day counts inferred]

10. Concurrency. At most 3 open arcs, 1 in climax, 1 new start per 30 min. Others advance by quiet beats (clues, gossip) in Relax. The player's own consequences (revenge, bounty) skip the cycle, not the gate. [L4D: bosses not paced; numbers inferred]

Mysteries

11. Build the crime from real ties: motive (rivalry, debt, a lover's spouse), means (a weapon the killer owns), opportunity (the killer follows the victim until alone); the weapon goes home with the killer. [Shadows of Doubt DevBlog 10]
12. The body is always found in reasonable time (smell, noise, missed visit; about a game day, inferred); an examiner gives time of death after ~2 game hours. Witnesses recall people they know or who acted out of place. [Shadows of Doubt]
13. Fairness: two independent routes to culprit, motive and means; each innocent gets one elimination clue (no access, busy then); every placed item is a clue or a deliberate misdirect; at most one strong red herring, with its own innocent explanation. Code proves the culprit is reachable from findable facts before starting (inferred). [Golden Idol interview]
14. Accusing names the culprit plus one evidence item; feedback comes in batches ("two of three are right") so guessing fails. [Obra Dinn sets of three; Golden Idol "two or fewer incorrect"; Shadows of Doubt resolution form]
15. Wrong accusation: the innocent is punished (fined, in Shadows of Doubt), the killer stays free and may kill again; never a dead end. Evidence taken by burglary convinces the player, not the court. [Shadows of Doubt wiki, DevBlog 10]
16. Blind-test each template 5-7 times. [Golden Idol]

Romance, drama, politics

17. Cast from history (ties, traits, past deeds); every arc cites at least one earlier fact about the player or the NPC. [Wildermyth: hooks set by history; CK3 trait weights]
18. Affinity and memory spread along family and friend links: hurting one sours the kin, helping warms them. [Watch Dogs Legion Census]
19. At most 4 feelings per pair; a new one replaces the weakest; they fade unless renewed. [Sims 4 sentiments, community sources]
20. Arc survivors (escaped killer, spurned rival) return remembering how the player beat them, carry a mark or title, act off-screen, stay rare (~10% of arcs) and can finally die. [Nemesis tabletop adaptation, secondary]
21. Sift real events before inventing: from the most interesting one, walk back along causes to one needing no explanation; let NPCs retell arcs later. [Ryan via Short; Dwarf Fortress engravings]
22. Adapt to loss and party size: a follower's death lowers stakes (RimWorld adaptation 0.4-1.47, a death costs 20-30 adapt days); companion-joining arcs weigh less as the party grows (population curve 0 -> 8.0, 1 -> 2.0, 5 -> 1.0, 9 -> 0.4, 12 -> 0). [RimWorld wiki]
