#pragma once

/**
 * BattleManager - Player-Present Battle System
 *
 * Tracks a single active battle: spawned actors, wave progress, morale,
 * and narration events. Papyrus polls PollBattleState() every 3 seconds
 * to get events and drive the battle forward.
 *
 * Only ONE battle at a time — Skyrim's engine can't handle multiple
 * mass combats simultaneously.
 */

#include "Plugin.h"
#include "Battlefield.h"

#include <nlohmann/json.hpp>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace SKSE {
    class SerializationInterface;
}

namespace IntelEngine {

    // Co-save record of the armies gathered somewhere (the pending battles). They used to live in memory alone and
    // were thrown away on every load: word of a battle "before the day is out" had gone round, and it never came.
    constexpr std::uint32_t kPendingBattleRecord = 'IEPB';

    /** A battle waiting to materialize — exists as metadata until the player approaches. */
    struct PendingBattle {
        int id = 0;
        std::string factionA;
        std::string factionB;
        std::string locationName;
        float x = 0.f, y = 0.f, z = 0.f;  // world coordinates from BGSLocation worldLocMarker
        float deadline = 0.f;               // game time when battle auto-resolves
        std::string resultJson;             // full DM response JSON for post-resolution
        RE::FormID worldspace = 0;          // the world the place lies in (0: indoors, never fought in view)
        std::string hold;                   // the hold it lies in, by its name in the game: where word of it starts
        // Where it is, for the journal of someone called to it (found again from the name when asked; not saved).
        RE::FormID location = 0, marker = 0;
    };

    class BattleManager {
    public:
        static BattleManager* GetSingleton() {
            static BattleManager instance;
            return &instance;
        }

        // =================================================================
        // Pending Battles (location-based, spawn on player proximity)
        // =================================================================

        /** Create a pending battle at a named location's world coordinates.
         *  Returns pending battle ID (>= 0) or -1 on failure (location not found). */
        int AddPendingBattle(const std::string& locationName,
                             const std::string& factionA, const std::string& factionB,
                             const std::string& resultJson);

        /** Check all pending battles against where the player is: the same world, out of doors, within
         *  Battlefield::kTriggerDistance of the place. Returns the ID of the first such battle, or -1.
         *  Also decides the battles whose time is up, where nobody saw them (RecordOffScreenBattle), and tells of them. */
        int PollPendingBattles(float playerX, float playerY, RE::FormID worldspace, bool indoors);

        /** Are these two already gathering for a battle somewhere? */
        bool HasPendingBattle(const std::string& factionA, const std::string& factionB) const;

        /** The place and the hold of a pending battle, as the game names them ({"", ""} when there is none). */
        std::pair<std::string, std::string> PendingBattlePlace(int id) const;

        /** Is the player in this hold (by the name the game gives it) right now? Word of a battle reaches them at once
         *  only there. */
        bool PlayerIsInHold(const std::string& hold) const;

        /** Remove a pending battle by ID (after it has been triggered or expired). */
        void RemovePendingBattle(int id);

        /** Get info about a pending battle as JSON string. Returns "{}" if not found. */
        std::string GetPendingBattleInfo(int id) const;

        /** Get count of active pending battles. */
        int GetPendingBattleCount() const;

        /** Clear all pending battles and expired results. */
        void ClearPendingBattles();

        // The pending battles are kept with the save.
        void Save(SKSE::SerializationInterface* intfc) const;
        void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
        void Revert();

        /** Get and consume one expired battle result JSON from the queue.
         *  Returns "" (empty string) if no expired battles queued.
         *  Called by Papyrus each poll cycle; drains one result per call. */
        std::string GetLastExpiredBattleResult();

        // =================================================================
        // Battle Lifecycle
        // =================================================================

        /** Start a new battle. Returns battle ID or -1 if a battle is already active. */
        int StartBattle(const std::string& factionA, const std::string& factionB,
                        const std::string& locationName, int warId);

        /** End the active battle. Records result. */
        void EndBattle(int battleId, const std::string& result, const std::string& victor);

        /** Check if a battle is currently active. */
        bool IsBattleActive() const;

        /** Armies are drawn up or fighting, or a battle has only just ended (its dead on the field, its survivors
         *  walking off): nobody comes up to the player with a story of their own. */
        bool IsFieldBusy() const;

        /** Get the active battle ID, or -1 if none. */
        int GetActiveBattleId() const;

        // =================================================================
        // Actor Tracking
        // =================================================================

        /** Register a spawned actor in the battle.
         *  tier: 0=generic soldier, 1=recruited NPC, 2=faction leader */
        bool RegisterActor(RE::Actor* actor, const std::string& factionId, int tier);

        /** Get count of alive actors for a faction. */
        int GetAliveCount(const std::string& factionId) const;

        // =================================================================
        // Morale
        // =================================================================

        /** Get current morale for a faction in the active battle (0-100). */
        int GetMorale(const std::string& factionId) const;

        /** Adjust morale by delta (clamped 0-100). */
        void AdjustMorale(const std::string& factionId, int delta);

        // =================================================================
        // Combat Polling (called every 3s from Papyrus)
        // =================================================================

        /** Poll current battle state. Returns JSON with events since last poll.
         *  Checks: actor deaths, morale thresholds, wave timing, battle end.
         *  Returns "{}" if no battle active. */
        std::string PollBattleState();

        // =================================================================
        // Wave Management
        // =================================================================

        /** Get current wave number (0=pre-battle, 1=vanguard, 2=reinforcements, 3=reserves). */
        int GetCurrentWave() const;

        /** Advance to next wave. Called by Papyrus after spawning wave actors. */
        void AdvanceWave();

        /** Each army brings up its own reserves, when it is the one that needs them ("A" or "B"). An army had its
         *  next wave whenever either side was thinned: the side that was winning grew as fast as the one that was
         *  losing, and whoever won the first clash won the battle (three of three, watched 2026-11-20). */
        int GetWave(const std::string& side) const;
        void AdvanceWaveOf(const std::string& side);

        // =================================================================
        // Player Participation
        // =================================================================

        /** Set the player's battle side. Applies morale boost/penalty.
         *  factionId must match factionA or factionB. Empty string = leave/spectator. */
        bool SetPlayerSide(const std::string& factionId);

        /** Get the player's current battle side (empty = spectator). */
        std::string GetPlayerSide() const;

        /** Check if the player participated in the current battle at any point. */
        bool HasPlayerParticipated() const;

        /** Check if the given faction is involved in the active battle.
         *  Used by CheckCrimeGoldStandings to exempt battle kills. */
        bool IsBattleFaction(const std::string& factionId) const;

        // =================================================================
        // Phase 1 Migration: Logic moved from Papyrus to C++
        // =================================================================

        /** Finalize a battle: apply standings, record political events, build narrative.
         *  Returns JSON with all results for Papyrus to display notifications and run cleanup.
         *  Consolidates: HandleBattleEnd, ApplyPlayerKillStanding, ApplyPostBattleStanding,
         *  ApplySpectatorConsequences, InjectBattleWitnessMemories, narrative building. */
        std::string FinalizeBattle(int battleId, const std::string& result,
                                   const std::string& victor, int deadA, int deadB,
                                   const std::string& locationName, float gameTime);

        /** Calculate reinforcement spawn positions behind player relative to battle center.
         *  Returns JSON: {soldierCount, spawnAX/AY, spawnBX/BY, spawnZ} */
        std::string CalculateReinforcementPositions(float playerX, float playerY, float playerZ,
                                                     float centerX, float centerY, int waveNum);

        /** Evaluate whether player should auto-join a battle side.
         *  Returns JSON: {shouldJoin, joinFaction, displayName, isQuestJoin} */
        std::string EvaluatePlayerJoin(const std::string& questAutoJoinFaction);

        /** Get notification text for a battle event.
         *  type: "wave1", "wave2", "wave3", "no_survivor_win", "no_survivor_loss",
         *        "soldier_victory", "soldier_defeat" */
        std::string GetBattleNotification(const std::string& type,
                                          const std::string& locationName = "",
                                          const std::string& victorName = "",
                                          bool playerWon = false);

        /** Validate that a faction_battle quest can be dispatched.
         *  Returns JSON: {canStart, enemyFaction, failReason} */
        std::string ValidateFactionBattleDispatch(const std::string& alliedFaction,
                                                   const std::string& suggestedEnemy = "");

        /** `count` more soldiers for one side of the active battle ("A" or "B"), created behind their own line where
         *  the player is least likely to see it, switched off: the script lets them fade in and sends them on.
         *  first: their number in the group that comes up together (0 picks where the group comes from).
         *  Returns JSON: {success, count, formIds, error} */
        std::string ReinforceLine(const std::string& side, int first, int count, RE::Actor* player, int kind = 0);
        // kind: 0 those who come up behind their own line; 1 a hidden force, out to one side of the other's line; 2 the
        // side's champion, alone, from behind their own line.

        /** Some of one side's living turn their coats (Battlefield::Turncoats): from now on they count, live and die
         *  for the other side. The script changes their battle side and sets them on their old comrades.
         *  Returns JSON: {count, formIds} */
        std::string TurnCoats(const std::string& side);

        /** Test hook: what the active battle has in store, said outright (champion: 0 none, 1 side A, 2 side B, 3
         *  both; flank and turncoat: 0, 1 or 2). */
        void SetTwists(int champion, int flank, int turncoat);

        /** Calculate mid-battle state for late-arriving player.
         *  Returns JSON: {soldiersPerSide, moraleLossA, moraleLossB} */
        std::string CalculateMidBattleState(float scheduledTime, float currentTime);

        /** Get action to take from poll result. Parses stateJson internally.
         *  Returns JSON: {action: "none"|"spawn_wave"|"battle_end", waveNum, result, victor} */
        std::string GetPollAction(const std::string& stateJson);

        /** Reset all battle state. Called after cleanup completes. */
        void ResetBattleState();

        /** Called every Papyrus poll — clears bounty and stops hostile friendly guards. */
        void SuppressBountyTick();

        /** Restore all enlisted guards to pre-battle state (remove battle faction, restore crime factions).
         *  Uses tracked FormIDs — works even if guards unloaded. */
        void RestoreEnlistedGuards();

        /** Clean up ALL stale battle state — called on game load as safety net. */
        void CleanupStaleBattleState();

        /** Both do nothing any more (kept for the scripts that call them). 3.8 took the player "out of the nine hold
         *  crime factions" for a faction quest and "put them back" afterwards. The player is in none of them to begin
         *  with, and of the nine ids only one was a crime faction (see RepairPlayerFactions). */
        void RemovePlayerCrimeFactions();
        void RestorePlayerCrimeFactions();

        /** Takes the player out of the three factions 3.8 put them into at the end of every battle joined and every
         *  faction quest: CrimeFactionHaafingar, TownIrontreeMillFaction and TownHalfMoonMillFaction (the ids it took
         *  for "the nine hold crime factions": 00029DB0 to 00029DB8, of which only the first is one, the next two are
         *  two mills' people, and the rest are not factions). Run on every load. */
        void RepairPlayerFactions();

        /** Where the two armies of the active battle can be drawn up near the player: not where the player stands,
         *  not where people live or stand about, on ground that is there, dry, and not across a cliff. `place`:
         *  what the battle is named after (may be null). The field found is remembered for the battle.
         *  Returns JSON: {ok, x, y, z, axis, reason} */
        std::string PickField(RE::Actor* player, RE::TESObjectREFR* place);

        /** Part of one army of the active battle drawn up on its field ("A" or "B"): soldiers `first` to
         *  `first + count - 1` of its first wave, in ranks, the commander (number 0) in front, facing the other line,
         *  standing down (they start nothing). Created where they stand, switched off: the script lets them fade in.
         *  A few at a time: making one soldier takes about a quarter of a millisecond of the game's frame.
         *  Returns JSON: {success, count, formIds, commander: [id] (with number 0), error} */
        std::string MusterLine(const std::string& side, int first, int count, RE::Actor* player);

        /** The active battle is a skirmish: `size` a side (2 to 6), nobody held back, told and recorded as a clash of a
         *  few and not as a battle. To be said before the lines are drawn up. (A brawl or a border skirmish used to
         *  be made around the player: one band 800 units in front of their eyes, the other behind their back, both
         *  in the same battle side, so that nothing but the first order to fight made them enemies.) */
        void SetSkirmish(int size);

        /** One person of a faction's soldiers made at a spot of the world around the player, switched off, in no
         *  faction at all and starting nothing: somebody who comes for one man (an assassin). */
        RE::Actor* RaiseLoner(const std::string& factionId, Battlefield::Vec at, float facingDeg, RE::Actor* player);

        /** A spot to come from for somebody who is to walk up to `target` unseen: on ground that can be walked, out of
         *  the player's sight or far off. False: there is none. */
        bool PickApproach(RE::Actor* player, RE::Actor* target, Battlefield::Vec& at) const;

        /** Takes an actor out of every faction they came with (their record's and those given since). */
        static void StripFactions(RE::Actor* actor);

        /** Where a band that comes for the player themselves can come from: out of doors, not in or by a town or a
         *  house, from a spot the player does not see and that can be walked from.
         *  Returns JSON: {ok, x, y, z, reason} */
        std::string PickAmbush(RE::Actor* player) const;

        /** The side enemies of the player are put into (Intel_BattleSideB): one band, hostile to nobody by itself. */
        RE::TESFaction* EnemySide() const;

        /** The lines have met: from now on the battle can be won and lost. */
        void BeginFight();
        bool IsMustering() const;

        /** Who has the better of a battle that is not fought out (the player left, it went on too long).
         *  Returns JSON: {result, victor} (victor "" and result "draw" when neither). */
        std::string Verdict() const;

        /** A side called the player to its line and was turned down or not answered. */
        void NoteTurnedDown(const std::string& factionId);
        /** The player has gone from the field: what follows is decided with nobody watching. Whoever had taken a
         *  side walked away from it. */
        void NotePlayerLeft();

        /** A sentence of a battle, whole, for the script to show or to give somebody to say. Whole because of how the
         *  game keeps text: one spelling for all strings that differ only in capitals, whichever came first. "the
         *  Imperial Legion", put together in a script, came out as "The Imperial Legion" in the middle of a sentence
         *  (2026-11-20), and would again for any name the game already holds with a capital.
         *  what: see the function. a, b: faction ids. name: someone's name. number, other: counts. */
        std::string Say(const std::string& asked, const std::string& a, const std::string& b, const std::string& name, int number,
                        int other) const;

        /** For SkyrimNet (decorator get_intelengine_battle_side): the name of the army this soldier stands in, when
         *  the player may still join it by saying so; empty for everyone else. */
        std::string SideOpenTo(RE::Actor* actor) const;
        /** For SkyrimNet (decorator get_intelengine_battle): what a soldier of the battle knows about where they
         *  stand right now; empty for everyone else. */
        std::string SoldierBrief(RE::Actor* actor) const;

        /** The field of the active battle.
         *  Returns JSON: {set, x, y, z, axis, mustering, commanderA: [id], commanderB: [id]} */
        std::string FieldInfo() const;
        /** For the dashboard: the battle on the field now (who, where, drawn up or fighting, how each side stands)
         *  and the armies drawing up somewhere (who, where, how long before it is decided without the player).
         *  Nothing of what a battle has in store. Any thread. */
        nlohmann::json DashboardJson(float nowGameDays) const;
        /** The gathering one of whose armies calls the player (they hold a rank with it: Ranks::Caller), for the
         *  journal: {id, faction, place, location: [id], marker: [id]}; "{}" when nobody calls. */
        std::string Call();
        /** Where an army goes when the battle is over: a town's marker to march toward (Battlefield::MarchTo: home if
         *  it is far enough, else the nearest town; the beaten never through the army that beat them). field: where
         *  it was fought; away: from the other army toward this one. Null: no town to march to (another world). */
        RE::TESObjectREFR* WayHome(const std::string& factionId, Battlefield::Vec field, Battlefield::Vec away, bool beaten, RE::FormID notTo);
        /** The words of the call's objective in the journal, set on the quest (not kept by the game: said again after
         *  a load). */
        void SetCallText(const std::string& factionId, const std::string& place);
        /** Which of two factions calls the player to its line, "" when neither. */
        static std::string CallerOf(const std::string& factionA, const std::string& factionB);

        /** The side ("A" or "B") a political faction stands on in the active battle: the battle's first faction is
         *  side A (Intel_BattleSideA), the other side B. (3.8 put "the player's side" on A, but only on one of the two
         *  ways a battle could begin: on the other this answered "A" for both factions, and a player who joined the
         *  second faction fought their own side.) */
        std::string GetFactionSide(const std::string& factionId) const;

        /** Get FormIDs of soldiers on a given side ("A" or "B").
         *  Returns JSON: {"formIds": [...], "count": N, "alive": M} */
        std::string GetBattleSoldierFormIds(const std::string& side) const;

        /** Set all alive soldiers on a side as player teammates (or clear).
         *  Used when player manually joins mid-battle. */
        void SetBattleSoldiersAsTeammates(const std::string& side, bool isTeammate = true);

        /** Count dead soldiers on a given side ("A" or "B"). */
        int CountDeadSoldiers(const std::string& side) const;

        /** Cleanup battle soldiers by proximity to player.
         *  Disables+deletes actors behind the player and far enough away.
         *  Returns count of remaining (not yet cleaned) actors. */
        int CleanupBattleSoldiers(float playerX, float playerY, float playerZ,
                                   float playerAngleZ, bool forceAll);

        /** Force cleanup ALL battle soldiers (hard timeout). Disables+deletes all tracked actors. */
        void ForceCleanupAllSoldiers();
        /** Drop the list of soldiers left to clean up, without touching them: the script removes them itself
         *  at the end of a battle, where it can tell which ones the player is looking at. */
        void ForgetBattleSoldiers();

        /** Remove crime factions from battle soldiers (no bounty for kills). Called at spawn. */
        void SnapshotBounties();

        /** Calculate battle marker offset position for exterior placement.
         *  Returns JSON: {x, y, z} */
        std::string CalculateBattleMarkerPosition(float playerX, float playerY,
                                                   float locX, float locY, float locZ,
                                                   float offsetUnits);

        /** Snapshot of battle state for cross-system queries (single lock acquisition). */
        struct BattleSnapshot {
            bool active = false;
            bool playerParticipated = false;
            std::string factionA;
            std::string factionB;
        };

        /** Get an atomic snapshot of battle state. Used by FactionPolitics to avoid
         *  multiple mutex acquisitions and lock-order issues with configMutex_. */
        BattleSnapshot GetBattleSnapshot() const;

    private:
        BattleManager() = default;
        ~BattleManager() = default;
        BattleManager(const BattleManager&) = delete;
        BattleManager& operator=(const BattleManager&) = delete;

        struct BattleActor {
            RE::FormID formId = 0;
            std::string factionId;
            int tier = 0;            // 0=generic, 1=recruited, 2=leader
            bool alive = true;
        };

        struct BattleState {
            int id = 0;
            int warId = 0;
            std::string factionA;
            std::string factionB;
            std::string locationName;
            int moraleA = 100;
            int moraleB = 100;
            int currentWave = 0;         // 0=pre-battle, 1/2/3 = wave number (the further of the two sides)
            int waveA = 0, waveB = 0;    // how many of its waves each side has on the field
            int waveStartAliveA = 0;     // alive count at start of current wave (casualty baseline)
            int waveStartAliveB = 0;
            // Narration throttling — track which events have been narrated
            bool firstBloodNarrated = false;
            std::unordered_set<std::string> moraleThresholdsNarrated; // "factionId_threshold" keys

            std::vector<BattleActor> actors;

            // ESP faction mapping — which faction got Intel_BattleSideA.
            // Normalized so player's allied faction is always SideA when player joins.
            // Reinforcements and wave spawns must use this mapping, not factionA/B directly.
            std::string sideAFaction;  // faction ID assigned to Intel_BattleSideA

            // Player participation
            std::string playerSide;          // empty = spectator, factionId = participating
            bool playerParticipated = false;  // true if player joined at any point (survives side changes)
            int playerKillsA = 0;            // soldiers from factionA killed by player
            int playerKillsB = 0;            // soldiers from factionB killed by player
            nlohmann::json physicalPosition = nullptr;

            // The field (PickField), and how the battle stands on it.
            bool fieldSet = false;
            float fieldX = 0.f, fieldY = 0.f, fieldZ = 0.f, fieldAxis = 0.f;
            bool mustering = true;           // drawn up, not yet fighting: nothing is decided
            RE::FormID commanderA = 0, commanderB = 0;
            std::unordered_set<std::string> turnedDown;   // sides that called the player to their line in vain
            bool playerLeft = false;         // took a side and walked away
            bool unseen = false;             // the player has gone: nobody is watching
            std::string commanderFell;       // a side whose commander has just fallen, until the poll has said so
            float reserveAX = 0.f, reserveAY = 0.f, reserveBX = 0.f, reserveBY = 0.f;   // where the group now coming up comes from
            // Who is still to be made of the group now being drawn up or brought up: rolled once for the whole
            // group, made one a call (asking the list for one at a time cost more than making them: 0.5 to 0.85 ms).
            std::vector<RE::TESBoundObject*> rolledA, rolledB;
            // What it has in store (Battlefield::Plan), and what of that has happened.
            Battlefield::Twists twists;
            double fightBegan = 0.0;                    // real seconds
            bool flankDone = false, turnDone = false, championCalledA = false, championCalledB = false;
            RE::FormID championA = 0, championB = 0;
            float flankX = 0.f, flankY = 0.f;           // where the hidden force breaks cover
            std::string championFell, sprung, turning;  // said by the poll, once each
            // Captains and champions of each side cut down by the player's own hand.
            int playerSlewCaptainsA = 0, playerSlewCaptainsB = 0, playerSlewChampionsA = 0, playerSlewChampionsB = 0;
            int lineSize = Battlefield::WaveSize(1);    // how many of a side are drawn up
            int waves = Battlefield::kWaves;            // how many waves a side has in all
            int lossPerSoldier = Battlefield::kLossSoldier;
            bool skirmish = false;
        };
        /** The people a faction raises: `count` of its soldier template (its first ally's, the bandits', when its own
         *  yields nobody). */
        std::vector<RE::TESBoundObject*> RollFor(const std::string& factionId, int count, RE::Actor* player) const;
        /** The record a soldier template names, looked up once a game session (by name it is a search). */
        RE::TESBoundObject* TemplateOf(const std::string& templateId) const;
        mutable std::unordered_map<std::string, RE::TESBoundObject*> templates_;
        mutable std::mutex templatesMutex_;

        /** Soldiers of one side created where they are to stand (never at the player), facing `facingDeg`, switched
         *  off, without the factions they came with, in their battle side. standDown: they start no fight (the lines
         *  at the muster). commander: the first of them leads the side. */
        std::vector<RE::Actor*> RaiseSoldiers(const std::string& factionId, RE::TESFaction* battleFaction,
                                              const std::vector<Battlefield::Vec>& spots, float facingDeg, float fallbackZ,
                                              RE::Actor* player, bool standDown, bool commander, bool ofABattle = true,
                                              const std::vector<RE::TESBoundObject*>* who = nullptr, bool champion = false);
        /** The next `count` of the group rolled for a side of the active battle (rolled now, `whole` of them, when
         *  `first` is 0 or nothing is left). */
        std::vector<RE::TESBoundObject*> NextOf(bool sideA, const std::string& factionId, int first, int count, int whole, RE::Actor* player);

        /** Resolve Intel_BattleSideA and Intel_BattleSideB factions from IntelEngine.esp.
         *  Returns {nullptr, nullptr} if ESP not found. */
        std::pair<RE::TESFaction*, RE::TESFaction*> ResolveBattleFactions() const;

        /** Thread-safe random float in [min, max]. */
        static float RandomFloat(float min, float max);

        /** Check if an actor is still alive in the game world. */
        bool IsActorAlive(RE::FormID formId) const;

        std::optional<BattleState> activeBattle_;
        mutable std::mutex mutex_;
        int nextBattleId_ = 1;
        std::atomic<bool> suppressBattleBounty_{false};  // set during active battle with player
        // What the player owed each crime faction for violent crime when they took a side, and until when what has
        // come on top is struck out again (StrikeBattleBounty). The game's thread only.
        std::unordered_map<RE::FormID, float> bountyBefore_;
        std::vector<RE::TESFaction*> crimeFactions_;     // every faction that keeps a bounty (read from the game's data once)
        bool bountyWatched_ = false;
        double bountyWatchUntil_ = 0.0;          // real seconds; 0: while the battle is fought
        void SnapshotBounty();
        void StrikeBattleBounty();
        std::atomic<double> quietUntil_{0.0};            // real seconds (steady clock): the aftermath of the last battle
        std::string lastPlace_;                          // where the last battle was fought (spoken of after it is over)
        mutable std::string lastPromotion_;              // the rank the last battle lifted the player to, until it has been said

        // Non-battle actors modified during battle (guards added to battle faction + teammate).
        // Tracked for reliable cleanup — ForEachLoadedActor misses unloaded actors.
        std::vector<RE::FormID> modifiedGuardFormIds_;
        bool playerCrimeFactionsRemoved_ = false;  // track if player needs crime faction restore

        // Pending battles (location-based, waiting for player proximity)
        std::vector<PendingBattle> pendingBattles_;
        mutable std::mutex pendingMutex_;  // separate mutex — pending ops don't touch active battle
        int nextPendingId_ = 1;
        // How long two armies face each other before it is decided without the player: long enough for word of
        // it to reach a tavern and for someone who hears it there to walk out to the field. (3.8: three game hours,
        // nine minutes of play, with the outcome already told.)
        static constexpr float PENDING_BATTLE_DURATION = 10.0f / 24.0f;  // game days

        // Standing constants (MUST match Papyrus properties in IntelEngine_Battle.psc lines 40-47)
        static constexpr int KILL_STANDING_PENALTY_PER_SOLDIER = -5;
        static constexpr int VICTORY_ALLY_BONUS = 15;
        static constexpr int VICTORY_ENEMY_PENALTY = -10;
        static constexpr int DEFEAT_ALLY_BONUS = 5;
        static constexpr int DEFEAT_ENEMY_PENALTY = -5;
        static constexpr int SPECTATOR_PENALTY = -5;
        static constexpr int SPECTATOR_PENALTY_THRESHOLD = 10;
        static constexpr int AUTO_JOIN_STANDING_THRESHOLD = Battlefield::kFriendStanding;
        static constexpr int HOSTILE_STANDING_THRESHOLD = Battlefield::kHostileStanding;  // faction attacks player on sight
        static constexpr int NEUTRAL_KILL_PENALTY = -3;     // each soldier cut down by someone who took no side
        static constexpr int NEUTRAL_KILL_PENALTY_MOST = -15;
        static constexpr int DESERTION_PENALTY = -8;        // took a side and walked away from it

        // Wave soldier counts: Battlefield::WaveSize (the script asks for the same numbers).
        static constexpr int WAVE1_SOLDIERS = Battlefield::WaveSize(1);
        static constexpr int WAVE2_SOLDIERS = Battlefield::WaveSize(2);
        static constexpr int WAVE3_SOLDIERS = Battlefield::WaveSize(3);
        static constexpr int WAVE4_SOLDIERS = Battlefield::WaveSize(4);
        static constexpr int WAVE5_SOLDIERS = Battlefield::WaveSize(5);
        static constexpr int MAX_SOLDIERS_PER_SIDE = Battlefield::kMaxPerSide;

        // Spawn distances
        static constexpr float SPAWN_DISTANCE = 400.f;
        static constexpr float REINFORCEMENT_BEHIND_DISTANCE = 600.f;
        static constexpr float BATTLE_MARKER_OFFSET = 3000.f;

        // Expired battle results queue — consumed one-at-a-time by Papyrus for RESULT notifications
        std::vector<std::string> expiredResults_;  // JSON strings, popped front per read

        // Persistent cleanup list — survives EndBattle/ResetBattleState.
        // Populated when battle ends, cleared when physical cleanup completes.
        std::vector<RE::FormID> cleanupFormIds_;
        mutable std::mutex cleanupMutex_;
    };

}  // namespace IntelEngine
