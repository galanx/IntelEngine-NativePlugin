using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;

namespace IntelEngine.EspBuilder;

// The records IntelEngine 4.0 adds to IntelEngine.esp, built from code so they have a source (the rest of the esp
// is edited by hand in xEdit). Running "build" again replaces what an earlier run added and leaves everything else
// as it is.
//
//   dotnet run -c Release -- info  <esp>
//   dotnet run -c Release -- build <esp in> <output folder>   (writes <output folder>/IntelEngine.esp)
public static class Program
{
    public const string ThreadQuest = "IntelEngine_Threads";
    public const string LivesQuest = "IntelEngine_Lives";
    public const int LifeKeepSlots = 12;
    public const string LifeSandbox = "IntelEngine_LifeSandbox";
    public const int Slots = 6;

    // One phrasing per objective of a slot; slot i uses objectives i*10 + k. The script shows the one that fits.
    // <Alias=...> is replaced by the name of what the alias holds (an NPC, an item, a location).
    public static readonly string[] Phrasings =
    {
        "Meet <Alias=ThreadNpc{0}> at <Alias=ThreadPlace{0}>",      // 0 a meeting
        "Find <Alias=ThreadNpc{0}>",                                 // 1 someone to look for
        "Speak with <Alias=ThreadNpc{0}>",                           // 2 someone to talk to
        "Rescue <Alias=ThreadNpc{0}>",                               // 3 a captive
        "Go to <Alias=ThreadPlace{0}>",                              // 4 a place
        "Return to <Alias=ThreadNpc{0}>",                            // 5 back to whoever asked
        "Confront <Alias=ThreadNpc{0}>",                             // 6 a foe
        "Bring <Alias=ThreadItem{0}> to <Alias=ThreadNpc{0}>",       // 7 an item
        "Wait for <Alias=ThreadNpc{0}> at <Alias=ThreadPlace{0}>",   // 8 someone coming to you
        "Escort <Alias=ThreadNpc{0}> to <Alias=ThreadPlace{0}>",     // 9 bringing someone somewhere
    };

    public static int Main(string[] args)
    {
        if (args.Length >= 2 && args[0] == "info") return Info(args[1]);
        if (args.Length >= 3 && args[0] == "build") return Build(args[1], args[2]);
        if (args.Length >= 3 && args[0] == "copy") return Copy(args[1], args[2]);
        if (args.Length >= 3 && args[0] == "package") return DumpPackage(args[1], args[2]);
        if (args.Length >= 2 && args[0] == "reflect") return Reflect(args[1]);
        if (args.Length >= 3 && args[0] == "findref") return FindRef(args[1], args[2]);
        if (args.Length >= 3 && args[0] == "findcell") return FindCell(args[1], args[2]);
        if (args.Length >= 3 && args[0] == "findloc") return FindLoc(args[1], args[2]);
        if (args.Length >= 3 && args[0] == "questdump") return QuestDump(args[1], args[2]);
        if (args.Length >= 3 && args[0] == "finditem") return FindItem(args[1], args[2]);
        if (args.Length >= 3 && args[0] == "findnpc") return FindNpc(args[1], args[2]);
        if (args.Length >= 3 && args[0] == "npc") return NpcInfo(args[1], args.Skip(2).ToArray());
        Console.Error.WriteLine("usage: info <esp> | build <esp in> <output folder>");
        return 2;
    }

    static int Build(string input, string output)
    {
        // The output keeps the plugin's file name (Mutagen ties a mod to its name): "output" is a folder.
        Directory.CreateDirectory(output);
        output = Path.Combine(output, Path.GetFileName(input));
        var mod = SkyrimMod.CreateFromBinary(input, SkyrimRelease.SkyrimSE);
        var quest = mod.Quests.FirstOrDefault(q => q.EditorID == ThreadQuest);
        if (quest == null)
        {
            quest = mod.Quests.AddNew();
            quest.EditorID = ThreadQuest;
        }
        // The journal shows each objective of a miscellaneous quest on its own line, like vanilla favours.
        quest.Name = "Unfinished Business";
        quest.Type = Quest.TypeEnum.Misc;
        quest.Priority = 40;
        quest.Flags = 0;
        quest.Aliases.Clear();
        quest.Objectives.Clear();
        quest.Stages.Clear();

        uint nextAlias = 0;
        for (int slot = 0; slot < Slots; ++slot)
        {
            var npc = AddAlias(quest, ref nextAlias, $"ThreadNpc{slot}", QuestAlias.TypeEnum.Reference);
            var target = AddAlias(quest, ref nextAlias, $"ThreadTarget{slot}", QuestAlias.TypeEnum.Reference);
            AddAlias(quest, ref nextAlias, $"ThreadPlace{slot}", QuestAlias.TypeEnum.Location);
            AddAlias(quest, ref nextAlias, $"ThreadItem{slot}", QuestAlias.TypeEnum.Reference);
            for (int k = 0; k < Phrasings.Length; ++k)
            {
                var objective = new QuestObjective
                {
                    Index = (ushort)(slot * 10 + k),
                    DisplayText = string.Format(Phrasings[k], slot),
                };
                // The marker always points at the slot's target (the NPC, a marker at the place, the captive...).
                objective.Targets.Add(new QuestObjectiveTarget { AliasID = (int)target.ID });
                quest.Objectives.Add(objective);
            }
        }
        quest.VirtualMachineAdapter = new QuestAdapter
        {
            Version = 5,
            ObjectFormat = 2,
            Scripts = { new ScriptEntry { Name = ThreadQuest } },
        };
        var lifeSandbox = AddLifeSandbox(mod);
        var lives = AddLivesQuest(mod, lifeSandbox);
        var keeper = AddQuestKeeper(mod);
        var journal = new List<Quest>();
        for (int slot = 0; slot < QuestSlots; ++slot) journal.Add(AddJournalQuest(mod, slot));
        var arcKeeper = AddArcKeeper(mod);
        var arcs = new List<Quest>();
        for (int slot = 0; slot < ArcSlots; ++slot) arcs.Add(AddArcJournalQuest(mod, slot));
        var holders = AddTaleHolders(mod);
        var cutLoose = AddCutLoose(mod);
        mod.WriteToBinary(output);
        Console.WriteLine($"{ThreadQuest} [{quest.FormKey.ID:X6}]: {quest.Aliases.Count} aliases, {quest.Objectives.Count} objectives -> {output}");
        Console.WriteLine($"{LifeSandbox} [{lifeSandbox.FormKey.ID:X6}], {LivesQuest} [{lives.FormKey.ID:X6}]");
        Console.WriteLine($"{QuestKeeper} [{keeper.FormKey.ID:X6}], journal quests " +
                          string.Join(", ", journal.Select(q => $"{q.EditorID} [{q.FormKey.ID:X6}]")));
        Console.WriteLine($"{ArcKeeper} [{arcKeeper.FormKey.ID:X6}], arc journal quests " +
                          string.Join(", ", arcs.Select(q => $"{q.EditorID} [{q.FormKey.ID:X6}]")));
        Console.WriteLine("tale holders " + string.Join(", ", holders.Select(m => $"{m.EditorID} [{m.FormKey.ID:X6}]")));
        Console.WriteLine($"{CutLoose} [{cutLoose.FormKey.ID:X6}]");
        return 0;
    }

    // What the player's key does to someone a story holds bound: the crosshair says "Cut loose" where the game would
    // say "Talk" (IntelEngine_Arcs.psc sets it on them with SetActivateTextOverride while they are bound, and takes
    // it off again). The game shows a message's title there; the text is the same in case a game version reads that.
    public const string CutLoose = "IntelEngine_CutLoose";

    static Message AddCutLoose(SkyrimMod mod)
    {
        var message = mod.Messages.FirstOrDefault(m => m.EditorID == CutLoose);
        if (message == null)
        {
            message = mod.Messages.AddNew();
            message.EditorID = CutLoose;
        }
        message.Name = "Cut loose";
        message.Description = "Cut loose";
        return message;
    }

    // ------------------------------------------------------------------ story arcs (docs/STORY_ARCS.md)

    public const string ArcKeeper = "IntelEngine_ArcKeeper";
    public const string JournalArc = "IntelEngine_Arc";
    public const int ArcSlots = 4;   // Arcs.h kSlots

    // The aliases of an arc's journal quest, in the order IntelEngine_Arcs.psc names them (ALIAS_*). Actors may be dead
    // (a victim, a culprit the player killed): the journal still names them.
    static readonly string[] ArcActorAliases = { "Asker", "Culprit", "Witness", "Rival", "Victim", "Suspect2", "Suspect3" };

    // The objective phrasings, in the order of Arcs.h Objective. Every marker points at the Marker alias.
    public static readonly string[] ArcPhrasings =
    {
        "Ask <Alias=Witness> about the theft",                   // 0 AskWitness
        "Find <Alias.ShortName=Asker>'s <Alias=Item0>",          // 1 FindStolen
        "Confront <Alias=Culprit>",                              // 2 Confront
        "Return the <Alias=Item0> to <Alias=Asker>",             // 3 Return
        "Hear <Alias=Rival>'s side",                             // 4 HearRival
        "Tell <Alias=Asker> how to settle it with <Alias=Rival>",// 5 DecideFeud
        "Beat <Alias=Rival> in a brawl",                         // 6 BeatRival
        "Make peace with <Alias=Rival>",                         // 7 MakePeace
        "See where <Alias=Victim> died",                         // 8 SeeScene
        "Question <Alias=Witness>",                              // 9 Question
        "Find proof of who killed <Alias=Victim>",               // 10 FindProof
        "Name the killer to <Alias=Asker>",                      // 11 NameKiller
    };

    // The journal's log (Arcs.h: OpenStage = pattern * 1000, a node's or a branch's log stage, EndStage = open + 90 +
    // (ending - 10)). An ending's entry completes or fails the quest.
    static readonly (ushort Stage, string Text, QuestLogEntry.Flag? Flag)[] ArcLog =
    {
        (1000, "<Alias=Asker> came to me: someone broke into <Alias.ShortName=Asker>'s home in <Alias=Place> and stole the <Alias=Item0>. <Alias.ShortName=Asker> wants it back.", null),
        (1010, "<Alias=Witness> saw <Alias=Culprit> slip out of <Alias.ShortName=Asker>'s home that night with something under one arm.", null),
        (1020, "I found <Alias.ShortName=Asker>'s <Alias=Item0> among <Alias.ShortName=Culprit>'s things.", null),
        (1031, "I handed <Alias=Culprit> over to the guard.", null),
        (1032, "I let <Alias=Culprit> go.", null),
        (1033, "<Alias=Culprit> is dead.", null),
        (1090, "I brought the <Alias=Item0> back to <Alias.ShortName=Asker>.", QuestLogEntry.Flag.CompleteQuest),
        (1093, "I never brought the <Alias=Item0> back, and <Alias.ShortName=Asker> has given up on it.", QuestLogEntry.Flag.FailQuest),
        (1094, "<Alias=Asker> is dead.", QuestLogEntry.Flag.FailQuest),
        (1096, "I kept <Alias.ShortName=Asker>'s <Alias=Item0> for myself.", QuestLogEntry.Flag.FailQuest),
        (2000, "<Alias=Asker> and <Alias=Rival> are at odds, and <Alias.ShortName=Asker> asked me to settle it, one way or another.", null),
        (2010, "I heard <Alias.ShortName=Rival>'s side of it.", null),
        (2021, "I told <Alias.ShortName=Asker> I would settle it with my fists against <Alias.ShortName=Rival>.", null),
        (2022, "I told <Alias.ShortName=Asker> I would try to make peace between them.", null),
        (2023, "<Alias.ShortName=Rival> would not be talked round.", null),
        (2090, "I beat <Alias.ShortName=Rival> in a brawl, and the quarrel is settled.", QuestLogEntry.Flag.CompleteQuest),
        (2091, "<Alias.ShortName=Rival> beat me in the brawl.", QuestLogEntry.Flag.FailQuest),
        (2093, "I never settled it, and <Alias.ShortName=Asker> stopped waiting.", QuestLogEntry.Flag.FailQuest),
        (2094, "<Alias=Asker> is dead.", QuestLogEntry.Flag.FailQuest),
        (2098, "<Alias.ShortName=Asker> and <Alias.ShortName=Rival> have made peace, and <Alias.ShortName=Asker> paid me.", QuestLogEntry.Flag.CompleteQuest),
        (2099, "I chose to stay out of their quarrel.", QuestLogEntry.Flag.CompleteQuest),
        (3000, "<Alias=Victim> was found dead at home in <Alias=Place>. <Alias=Asker> asked me to find the killer and name them with proof.", null),
        (3010, "I have seen where <Alias.ShortName=Victim> died.", null),
        (3020, "<Alias=Witness> saw <Alias=Culprit> near <Alias.ShortName=Victim>'s house that night, hurrying away.", null),
        (3030, "I found something that points to the killer.", null),
        (3031, "<Alias.ShortName=Asker> will not take anyone's guilt on my word alone. I need proof.", null),
        (3090, "I proved that <Alias=Culprit> killed <Alias.ShortName=Victim>, and the guard took <Alias.ShortName=Culprit>.", QuestLogEntry.Flag.CompleteQuest),
        (3093, "<Alias.ShortName=Victim>'s killer was never found.", QuestLogEntry.Flag.FailQuest),
        (3094, "<Alias=Asker> is dead.", QuestLogEntry.Flag.FailQuest),
        (3095, "I named the wrong person. <Alias.ShortName=Victim>'s killer is still free.", QuestLogEntry.Flag.FailQuest),
        // A tale's (Arcs.h kTale*Stage): the story so far and its ending, written by the DLL as the names of the
        // slot's two holder items (IntelEngine_TaleText<slot>, IntelEngine_TaleClose<slot>).
        (10, "<Alias=Tale>", null),
        (90, "<Alias=Close>", QuestLogEntry.Flag.CompleteQuest),
        (91, "<Alias=Close>", QuestLogEntry.Flag.FailQuest),
    };

    // A tale's steps are objectives kTaleObjective.. (Arcs.h) whose text the DLL sets at runtime.
    public const int TaleObjective = 20;
    public const int TaleSteps = 12;   // Arcs.h kMaxTaleNodes

    // A journal quest per arc slot: a side quest whose title the DLL sets (ArcBridge), with no script of its own;
    // IntelEngine_Arcs.psc on the keeper drives its aliases, objectives and stages.
    static Quest AddArcJournalQuest(SkyrimMod mod, int slot)
    {
        var editorId = $"{JournalArc}{slot}";
        var quest = mod.Quests.FirstOrDefault(q => q.EditorID == editorId);
        if (quest == null)
        {
            quest = mod.Quests.AddNew();
            quest.EditorID = editorId;
        }
        quest.Name = "A Story";
        quest.Type = Quest.TypeEnum.SideQuest;
        quest.Priority = 50;
        quest.Flags = Quest.Flag.AllowRepeatedStages;
        quest.Aliases.Clear();
        quest.Objectives.Clear();
        quest.Stages.Clear();
        uint nextAlias = 0;
        foreach (var name in ArcActorAliases)
        {
            var actor = AddAlias(quest, ref nextAlias, name, QuestAlias.TypeEnum.Reference);
            actor.Flags |= QuestAlias.Flag.AllowDead;
        }
        AddAlias(quest, ref nextAlias, "Item0", QuestAlias.TypeEnum.Reference);
        AddAlias(quest, ref nextAlias, "Item1", QuestAlias.TypeEnum.Reference);
        var marker = AddAlias(quest, ref nextAlias, "Marker", QuestAlias.TypeEnum.Reference);
        marker.Flags |= QuestAlias.Flag.AllowDead;
        AddAlias(quest, ref nextAlias, "Place", QuestAlias.TypeEnum.Location);
        AddAlias(quest, ref nextAlias, "Tale", QuestAlias.TypeEnum.Reference);    // 11: the story so far
        AddAlias(quest, ref nextAlias, "Close", QuestAlias.TypeEnum.Reference);   // 12: how it ended
        for (int k = 0; k < ArcPhrasings.Length; ++k)
        {
            var objective = new QuestObjective { Index = (ushort)k, DisplayText = ArcPhrasings[k] };
            objective.Targets.Add(new QuestObjectiveTarget { AliasID = (int)marker.ID });
            quest.Objectives.Add(objective);
        }
        for (int k = 0; k < TaleSteps; ++k)
        {
            var objective = new QuestObjective { Index = (ushort)(TaleObjective + k), DisplayText = "A step of the story" };
            objective.Targets.Add(new QuestObjectiveTarget { AliasID = (int)marker.ID });
            quest.Objectives.Add(objective);
        }
        foreach (var (stage, text, flag) in ArcLog)
        {
            // Flags always set: without them the entry has no QSDT header and the game shows no log text.
            var entry = new QuestLogEntry { Entry = text, Flags = flag ?? (QuestLogEntry.Flag)0 };
            quest.Stages.Add(new QuestStage { Index = stage, LogEntries = { entry } });
        }
        quest.VirtualMachineAdapter = null;
        return quest;
    }

    // The items whose names a tale's journal log shows (the DLL renames them): per slot the story so far, then the
    // ending. Never placed where anyone could see them: the script makes disabled references of them under the world.
    static List<MiscItem> AddTaleHolders(SkyrimMod mod)
    {
        var list = new List<MiscItem>();
        foreach (var kind in new[] { "TaleText", "TaleClose" })
        {
            for (int slot = 0; slot < ArcSlots; ++slot)
            {
                var editorId = $"IntelEngine_{kind}{slot}";
                var item = mod.MiscItems.FirstOrDefault(m => m.EditorID == editorId);
                if (item == null)
                {
                    item = mod.MiscItems.AddNew();
                    item.EditorID = editorId;
                }
                item.Name = kind == "TaleText" ? "The story so far" : "How it ended";
                item.Value = 0;
                item.Weight = 0;
                list.Add(item);
            }
        }
        return list;
    }

    // The keeper of the arcs: a quest of its own for IntelEngine_Arcs.psc (its watch and update loop), nothing in the
    // journal.
    static Quest AddArcKeeper(SkyrimMod mod)
    {
        var quest = mod.Quests.FirstOrDefault(q => q.EditorID == ArcKeeper);
        if (quest == null)
        {
            quest = mod.Quests.AddNew();
            quest.EditorID = ArcKeeper;
        }
        quest.Name = null;
        quest.Type = Quest.TypeEnum.None;
        quest.Priority = 10;
        quest.Flags = Quest.Flag.StartGameEnabled;
        quest.Aliases.Clear();
        quest.Objectives.Clear();
        quest.Stages.Clear();
        quest.VirtualMachineAdapter = new QuestAdapter
        {
            Version = 5,
            ObjectFormat = 2,
            Scripts = { new ScriptEntry { Name = "IntelEngine_Arcs" } },
        };
        return quest;
    }

    // ------------------------------------------------------------------ quests (docs/QUESTS.md)

    public const string QuestKeeper = "IntelEngine_QuestKeeper";
    public const string JournalQuest = "IntelEngine_Quest";
    public const int QuestSlots = 6;   // Quests.h kSlots

    // The objective phrasings of every journal quest, in the order of Quests.h Objective. The marker of each points at
    // the Marker alias, which the script moves to what the step is about.
    public static readonly string[] QuestPhrasings =
    {
        "Search <Alias=Place> for the <Alias=Item>",      // 0 Search
        "Recover the <Alias=Item>",                      // 1 Recover
        "Bring the <Alias=Item> to <Alias=Giver>",       // 2 Bring
        "Deal with the brigand at <Alias=Place>",        // 3 DealWith (the name is in the title)
        "Tell <Alias=Giver> it is done",                 // 4 Report
        "Go with <Alias=Ally> to <Alias=Place>",         // 5 GoWith
        "Clear <Alias=Place> with <Alias=Ally>",         // 6 Clear
        "Share the spoils with <Alias=Ally>",            // 7 Share
    };

    // The journal's log, stage by stage (Quests.h OpenStage / StepDef.logStage / EndStage). An ending's entry completes
    // or fails the quest, which moves it to Completed Quests with the usual notice.
    static readonly (ushort Stage, string Text, QuestLogEntry.Flag? Flag)[] QuestLog =
    {
        (100, "<Alias=Giver> came to me for help: someone stole a <Alias=Item> from <Alias.PronounObj=Giver> and was seen running toward <Alias=Place>. <Alias.ShortName=Giver> wants it back.", null),
        (110, "I have reached <Alias=Place>, where the thief was headed.", null),
        (120, "I have found the <Alias=Item>.", null),
        (190, "I brought the <Alias=Item> back to <Alias.ShortName=Giver>.", QuestLogEntry.Flag.CompleteQuest),
        (191, "I never brought the <Alias=Item> back to <Alias.ShortName=Giver>.", QuestLogEntry.Flag.FailQuest),
        (192, "I never went after the <Alias=Item>, and <Alias.ShortName=Giver> has given up on it.", QuestLogEntry.Flag.FailQuest),
        (193, "<Alias=Giver> is dead. There is no one left to return the <Alias=Item> to.", QuestLogEntry.Flag.FailQuest),
        (200, "<Alias=Giver> asked me to deal with a brigand whose gang has settled at <Alias=Place> and preys on everyone near it.", null),
        (210, "The brigand is dead.", null),
        (290, "I told <Alias.ShortName=Giver> that the brigand is dead.", QuestLogEntry.Flag.CompleteQuest),
        (292, "I never dealt with the brigand, and <Alias.ShortName=Giver> stopped waiting.", QuestLogEntry.Flag.FailQuest),
        (293, "<Alias=Giver> is dead.", QuestLogEntry.Flag.FailQuest),
        (300, "I ran into <Alias=Ally>, who is on the way to clear out <Alias=Place> and asked me to come along.", null),
        (310, "We reached <Alias=Place> together.", null),
        (320, "We cleared <Alias=Place>.", null),
        (390, "<Alias=Ally> and I shared what we found in <Alias=Place>.", QuestLogEntry.Flag.CompleteQuest),
        (394, "I never joined <Alias=Ally>, who went to <Alias=Place> alone.", QuestLogEntry.Flag.FailQuest),
        (395, "<Alias=Ally> is dead.", QuestLogEntry.Flag.FailQuest),
    };

    // A journal quest of its own per slot: a side quest whose title the DLL sets from the quest record (QuestBridge),
    // with no script; IntelEngine_Quests.psc on the keeper drives its aliases, objectives and stages.
    static Quest AddJournalQuest(SkyrimMod mod, int slot)
    {
        var editorId = $"{JournalQuest}{slot}";
        var quest = mod.Quests.FirstOrDefault(q => q.EditorID == editorId);
        if (quest == null)
        {
            quest = mod.Quests.AddNew();
            quest.EditorID = editorId;
        }
        quest.Name = "A Request";
        quest.Type = Quest.TypeEnum.SideQuest;
        quest.Priority = 50;
        quest.Flags = Quest.Flag.AllowRepeatedStages;
        quest.Aliases.Clear();
        quest.Objectives.Clear();
        quest.Stages.Clear();
        uint nextAlias = 0;
        AddAlias(quest, ref nextAlias, "Giver", QuestAlias.TypeEnum.Reference);
        AddAlias(quest, ref nextAlias, "Foe", QuestAlias.TypeEnum.Reference);
        AddAlias(quest, ref nextAlias, "Ally", QuestAlias.TypeEnum.Reference);
        AddAlias(quest, ref nextAlias, "Item", QuestAlias.TypeEnum.Reference);
        var marker = AddAlias(quest, ref nextAlias, "Marker", QuestAlias.TypeEnum.Reference);
        AddAlias(quest, ref nextAlias, "Place", QuestAlias.TypeEnum.Location);
        AddAlias(quest, ref nextAlias, "Companion", QuestAlias.TypeEnum.Reference);
        for (int k = 0; k < QuestPhrasings.Length; ++k)
        {
            var objective = new QuestObjective { Index = (ushort)k, DisplayText = QuestPhrasings[k] };
            objective.Targets.Add(new QuestObjectiveTarget { AliasID = (int)marker.ID });
            quest.Objectives.Add(objective);
        }
        foreach (var (stage, text, flag) in QuestLog)
        {
            // Flags always set: without them the entry has no QSDT header and the game shows no log text.
            var entry = new QuestLogEntry { Entry = text, Flags = flag ?? (QuestLogEntry.Flag)0 };
            quest.Stages.Add(new QuestStage { Index = stage, LogEntries = { entry } });
        }
        quest.VirtualMachineAdapter = null;
        return quest;
    }

    // The keeper: a quest of its own for IntelEngine_Quests.psc (its watch and its update loop), nothing in the journal.
    static Quest AddQuestKeeper(SkyrimMod mod)
    {
        var quest = mod.Quests.FirstOrDefault(q => q.EditorID == QuestKeeper);
        if (quest == null)
        {
            quest = mod.Quests.AddNew();
            quest.EditorID = QuestKeeper;
        }
        quest.Name = null;
        quest.Type = Quest.TypeEnum.None;
        quest.Priority = 10;
        quest.Flags = Quest.Flag.StartGameEnabled;
        quest.Aliases.Clear();
        quest.Objectives.Clear();
        quest.Stages.Clear();
        quest.VirtualMachineAdapter = new QuestAdapter
        {
            Version = 5,
            ObjectFormat = 2,
            Scripts = { new ScriptEntry { Name = "IntelEngine_Quests" } },
        };
        return quest;
    }

    // The package of a former follower's own life: a sandbox around where they are (an inn, a dungeon's entrance, a
    // town) at a walk. It holds only while they are not the player's teammate, so the moment they rejoin, the follower
    // framework's packages run again, before the scripts have even taken this one off. "Near self" needs no linked
    // reference, so it is safe while they are in a cell that is not loaded (a sandbox around a reference in an
    // unloaded cell crashes the engine).
    static Package AddLifeSandbox(SkyrimMod mod)
    {
        var source = mod.Packages.First(p => p.EditorID == "IntelEngine_SandboxPackage");
        var package = mod.Packages.FirstOrDefault(p => p.EditorID == LifeSandbox);
        if (package == null) package = mod.Packages.DuplicateInAsNewRecord(source);
        else package.DeepCopyIn(source);
        package.EditorID = LifeSandbox;
        package.PreferredSpeed = Package.Speed.Walk;
        var location = package.Data.Values.OfType<PackageDataLocation>().First();
        location.Location = new LocationTargetRadius
        {
            Target = new LocationFallback { Type = LocationTargetRadius.LocationType.NearSelf },
            Radius = 1000,
        };
        package.Conditions.Clear();
        package.Conditions.Add(new ConditionFloat
        {
            CompareOperator = CompareOperator.EqualTo,
            ComparisonValue = 0,
            Data = new GetPlayerTeammateConditionData(),
        });
        return package;
    }

    // A quest of its own for the lives script (IntelEngine_Lives.psc), so its update loop is not shared with another
    // script's: nothing in the journal.
    static Quest AddLivesQuest(SkyrimMod mod, Package lifeSandbox)
    {
        var quest = mod.Quests.FirstOrDefault(q => q.EditorID == LivesQuest);
        if (quest == null)
        {
            quest = mod.Quests.AddNew();
            quest.EditorID = LivesQuest;
        }
        quest.Name = null;
        quest.Type = Quest.TypeEnum.None;
        quest.Priority = 10;
        quest.Flags = Quest.Flag.StartGameEnabled;
        quest.Aliases.Clear();
        quest.Objectives.Clear();
        quest.Stages.Clear();
        // Followers the lives record keeps are held here, so the game keeps them in memory (IntelEngine_Lives.psc
        // KeepInMemory): a hireling's reference is not persistent, and once Jenassa walked into a cell that was not
        // loaded the game let go of her and her own life never began (2026-11-17).
        uint nextAlias = 0;
        for (int i = 0; i < LifeKeepSlots; ++i)
        {
            quest.Aliases.Add(new QuestAlias
            {
                ID = nextAlias++,
                Name = $"LifeKeep{i:00}",
                Type = QuestAlias.TypeEnum.Reference,
                Flags = QuestAlias.Flag.Optional | QuestAlias.Flag.AllowReuseInQuest | QuestAlias.Flag.AllowDisabled,
            });
        }
        quest.VirtualMachineAdapter = new QuestAdapter
        {
            Version = 5,
            ObjectFormat = 2,
            Scripts =
            {
                new ScriptEntry
                {
                    Name = LivesQuest,
                    Properties =
                    {
                        new ScriptObjectProperty
                        {
                            Name = "LifeSandbox",
                            Flags = ScriptProperty.Flag.Edited,
                            Object = new FormLink<ISkyrimMajorRecordGetter>(lifeSandbox.FormKey),
                        },
                    },
                },
            },
        };
        return quest;
    }

    // Read and write the plugin unchanged: shows that rewriting through Mutagen keeps the existing records as they are.
    static int Copy(string input, string output)
    {
        Directory.CreateDirectory(output);
        output = Path.Combine(output, Path.GetFileName(input));
        SkyrimMod.CreateFromBinary(input, SkyrimRelease.SkyrimSE).WriteToBinary(output);
        return 0;
    }

    // The structure of one package, to copy it right: template, flags, data inputs and their targets.
    static int DumpPackage(string path, string editorId)
    {
        using var mod = SkyrimMod.CreateFromBinaryOverlay(path, SkyrimRelease.SkyrimSE);
        var package = mod.Packages.First(p => p.EditorID == editorId);
        Console.WriteLine($"{package.EditorID}: flags={package.Flags} type={package.Type} template={package.PackageTemplate.FormKey} " +
                          $"interrupt={package.InterruptOverride} preferredSpeed={package.PreferredSpeed}");
        foreach (var kv in package.Data)
        {
            var value = kv.Value;
            Console.WriteLine($"   data {kv.Key}: {value.GetType().Name} {DescribeData(value)}");
        }
        Console.WriteLine($"   procedure tree: {package.ProcedureTree.Count} branches");
        foreach (var branch in package.ProcedureTree)
            Console.WriteLine($"     branch {branch.BranchType} procedure={branch.ProcedureType} flags={branch.Flags} inputs=" +
                              string.Join(",", branch.DataInputIndices));
        return 0;
    }

    static string DescribeData(IAPackageDataGetter value) => value switch
    {
        IPackageDataLocationGetter l => $"{l.Name} target={Target(l.Location.Target)} radius={l.Location.Radius}",
        IPackageDataBoolGetter b => $"{b.Name}={b.Data}",
        IPackageDataFloatGetter f => $"{f.Name}={f.Data}",
        _ => Flatten(value),
    };

    static string Target(IALocationTargetGetter target) => target switch
    {
        ILocationFallbackGetter f => $"fallback {f.Type}",
        ILocationKeywordGetter k => $"keyword {k.Link.FormKey}",
        ILocationTargetGetter t => $"reference {t.Link.FormKey}",
        _ => target.GetType().Name,
    };

    static string Flatten(IAPackageDataGetter value) =>
        string.Join(" ", (value.ToString() ?? "").Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries).Select(l => l.Trim()));

    static QuestAlias AddAlias(Quest quest, ref uint id, string name, QuestAlias.TypeEnum type)
    {
        var alias = new QuestAlias
        {
            ID = id++,
            Name = name,
            Type = type,
            // Stores Text: without it the journal shows "[...]" in place of the alias's name.
            Flags = type == QuestAlias.TypeEnum.Reference
                ? QuestAlias.Flag.Optional | QuestAlias.Flag.AllowReuseInQuest | QuestAlias.Flag.AllowDisabled | QuestAlias.Flag.StoresText
                : QuestAlias.Flag.Optional | QuestAlias.Flag.AllowReuseInQuest | QuestAlias.Flag.StoresText,
        };
        quest.Aliases.Add(alias);
        return alias;
    }

    // The members of a Mutagen type (and the values of its enums), to write the builder against the real API.
    static int Reflect(string typeName)
    {
        if (typeName.StartsWith("~"))
        {
            foreach (var t in typeof(Quest).Assembly.GetTypes().Where(t => t.Name.Contains(typeName[1..])).Take(20)) Console.WriteLine(t.FullName);
            return 0;
        }
        var type = typeof(Quest).Assembly.GetTypes().FirstOrDefault(t => t.Name == typeName || t.FullName == typeName);
        if (type == null) { Console.Error.WriteLine("no such type"); return 1; }
        if (type.IsEnum) { Console.WriteLine(string.Join(", ", Enum.GetNames(type))); return 0; }
        foreach (var p in type.GetProperties()) Console.WriteLine($"{p.PropertyType.FullName} {p.Name}");
        foreach (var n in type.GetNestedTypes()) Console.WriteLine($"nested {n.FullName}" + (n.IsEnum ? ": " + string.Join(", ", Enum.GetNames(n)) : ""));
        return 0;
    }

    // Placed references of an NPC base by editor ID (test help: the reference id the console wants).
    static int FindRef(string path, string editorId)
    {
        using var mod = SkyrimMod.CreateFromBinaryOverlay(path, SkyrimRelease.SkyrimSE);
        var bases = mod.Npcs.Where(n => (n.EditorID ?? "").Contains(editorId, StringComparison.OrdinalIgnoreCase)).Select(n => n.FormKey).ToHashSet();
        Console.WriteLine($"{bases.Count} bases: " + string.Join(", ", bases.Select(b => b.ID.ToString("X8"))));
        foreach (var cell in mod.EnumerateMajorRecords<IPlacedNpcGetter>())
            if (bases.Contains(cell.Base.FormKey)) Console.WriteLine($"{cell.FormKey.ID:X8} base {cell.Base.FormKey.ID:X8}");
        return 0;
    }

    // Interior cells by editor ID part (test help: the cell the console's coc wants, and its persistent references).
    static int FindCell(string path, string part)
    {
        using var mod = SkyrimMod.CreateFromBinaryOverlay(path, SkyrimRelease.SkyrimSE);
        foreach (var cell in mod.EnumerateMajorRecords<ICellGetter>())
            if ((cell.EditorID ?? "").Contains(part, StringComparison.OrdinalIgnoreCase))
                Console.WriteLine($"{cell.FormKey.ID:X8} {cell.EditorID} '{cell.Name?.String}' persistent={cell.Persistent.Count}" +
                                  string.Concat(cell.Persistent.Take(6).Select(r => $" {r.FormKey.ID:X8}")));
        return 0;
    }

    // Leveled and plain actors by editor ID part (test help: which of the game's creatures a story may spawn).
    static int FindNpc(string path, string part)
    {
        using var mod = SkyrimMod.CreateFromBinaryOverlay(path, SkyrimRelease.SkyrimSE);
        foreach (var l in mod.LeveledNpcs)
            if ((l.EditorID ?? "").Contains(part, StringComparison.OrdinalIgnoreCase)) Console.WriteLine($"LVLN {l.FormKey.ID:X8} {l.EditorID}");
        foreach (var n in mod.Npcs)
            if ((n.EditorID ?? "").Contains(part, StringComparison.OrdinalIgnoreCase) && (n.EditorID ?? "").StartsWith("Enc"))
                Console.WriteLine($"NPC_ {n.FormKey.ID:X8} {n.EditorID} '{n.Name?.String}'");
        return 0;
    }

    // NPC records by form id: template, flags, factions, AI data (test help: what a base is before the script places it).
    static int NpcInfo(string path, string[] ids)
    {
        using var mod = SkyrimMod.CreateFromBinaryOverlay(path, SkyrimRelease.SkyrimSE);
        foreach (var id in ids)
        {
            var want = Convert.ToUInt32(id, 16);
            var n = mod.Npcs.FirstOrDefault(x => x.FormKey.ID == want);
            if (n == null) { Console.WriteLine($"{id}: none"); continue; }
            Console.WriteLine($"{want:X8} {n.EditorID} '{n.Name?.String}' template={n.Template.FormKey.ID:X8} flags={n.Configuration.TemplateFlags} cfg={n.Configuration.Flags} " +
                              $"aggr={n.AIData.Aggression} conf={n.AIData.Confidence} class={n.Class.FormKey.ID:X8} race={n.Race.FormKey.ID:X8}");
            foreach (var f in n.Factions)
            {
                var fac = mod.Factions.FirstOrDefault(x => x.FormKey == f.Faction.FormKey);
                Console.WriteLine($"   faction {f.Faction.FormKey.ID:X8} {fac?.EditorID} rank {f.Rank} flags={fac?.Flags}");
                foreach (var r in fac?.Relations ?? new List<IRelationGetter>()) Console.WriteLine($"      {r.Reaction} {r.Target.FormKey.ID:X8}");
            }
            foreach (var i in n.Items ?? new List<IContainerEntryGetter>()) Console.WriteLine($"   item {i.Item.Item.FormKey.ID:X8} x{i.Item.Count}");
        }
        return 0;
    }

    // Locations by editor ID part, with their keywords' form ids and parent (test help).
    static int FindLoc(string path, string part)
    {
        using var mod = SkyrimMod.CreateFromBinaryOverlay(path, SkyrimRelease.SkyrimSE);
        foreach (var loc in mod.Locations)
            if ((loc.EditorID ?? "").Contains(part, StringComparison.OrdinalIgnoreCase))
                Console.WriteLine($"{loc.FormKey.ID:X8} {loc.EditorID} parent={loc.ParentLocation.FormKey.ID:X8} keywords=" +
                                  string.Join(",", (loc.Keywords ?? new List<IFormLinkGetter<IKeywordGetter>>()).Select(k => k.FormKey.ID.ToString("X8"))));
        foreach (var kw in mod.Keywords)
            if ((kw.EditorID ?? "").StartsWith("LocType")) Console.WriteLine($"kw {kw.FormKey.ID:X8} {kw.EditorID}");
        return 0;
    }

    // One quest's aliases, stages and the story manager nodes and keywords that start it (test help: vanilla brawls).
    static int QuestDump(string path, string editorId)
    {
        using var mod = SkyrimMod.CreateFromBinaryOverlay(path, SkyrimRelease.SkyrimSE);
        var quest = mod.Quests.FirstOrDefault(q => q.EditorID == editorId);
        if (quest == null) { Console.WriteLine("no such quest"); return 1; }
        Console.WriteLine($"{quest.FormKey.ID:X8} {quest.EditorID} flags={quest.Flags} type={quest.Type} event={quest.Event}");
        foreach (var alias in quest.Aliases)
            Console.WriteLine($"   alias {alias.ID} {alias.Name} flags={alias.Flags} forced={alias.ForcedReference.FormKey.ID:X8} unique={alias.UniqueActor.FormKey.ID:X8} " +
                              $"fromEvent={alias.FindMatchingRefFromEvent?.FromEvent} data={alias.FindMatchingRefFromEvent?.EventData?.Length}");
        foreach (var stage in quest.Stages)
            Console.WriteLine($"   stage {stage.Index} " + string.Concat(stage.LogEntries.Select(l => $"'{l.Entry?.String}'")));
        foreach (var node in mod.StoryManagerQuestNodes)
            if (node.Quests.Any(q => q.Quest.FormKey == quest.FormKey))
                Console.WriteLine($"   SM quest node {node.FormKey.ID:X8} {node.EditorID} parent={node.Parent.FormKey.ID:X8}");
        foreach (var ev in mod.StoryManagerEventNodes)
            Console.WriteLine($"   SM event node {ev.FormKey.ID:X8} {ev.EditorID} type={ev.Type}");
        foreach (var kw in mod.Keywords)
            if ((kw.EditorID ?? "").Contains("Brawl", StringComparison.OrdinalIgnoreCase) || (kw.EditorID ?? "").Contains("Intimidate", StringComparison.OrdinalIgnoreCase))
                Console.WriteLine($"   kw {kw.FormKey.ID:X8} {kw.EditorID}");
        return 0;
    }

    // Weapons and misc items by editor ID part (test help: the props of the arcs).
    static int FindItem(string path, string part)
    {
        using var mod = SkyrimMod.CreateFromBinaryOverlay(path, SkyrimRelease.SkyrimSE);
        foreach (var w in mod.Weapons)
            if ((w.EditorID ?? "").Contains(part, StringComparison.OrdinalIgnoreCase)) Console.WriteLine($"WEAP {w.FormKey.ID:X8} {w.EditorID} '{w.Name?.String}'");
        foreach (var m in mod.MiscItems)
            if ((m.EditorID ?? "").Contains(part, StringComparison.OrdinalIgnoreCase)) Console.WriteLine($"MISC {m.FormKey.ID:X8} {m.EditorID} '{m.Name?.String}' value={m.Value}");
        return 0;
    }

    static int Info(string path)
    {
        using var mod = SkyrimMod.CreateFromBinaryOverlay(path, SkyrimRelease.SkyrimSE);
        Console.WriteLine($"{mod.ModKey}: light={mod.ModHeader.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Small)} " +
                          $"next object id={mod.ModHeader.Stats.NextFormID:X} records={mod.ModHeader.Stats.NumRecords}");
        Console.WriteLine("masters: " + string.Join(", ", mod.ModHeader.MasterReferences.Select(m => m.Master.FileName)));
        foreach (var group in new (string Name, IEnumerable<IMajorRecordGetter> Records)[]
                 {
                     ("QUST", mod.Quests), ("PACK", mod.Packages), ("KYWD", mod.Keywords), ("GLOB", mod.Globals),
                     ("FLST", mod.FormLists), ("STAT", mod.Statics), ("ACTI", mod.Activators), ("MESG", mod.Messages),
                     ("FACT", mod.Factions), ("NPC_", mod.Npcs), ("SPEL", mod.Spells), ("MGEF", mod.MagicEffects),
                 })
        {
            var list = group.Records.ToList();
            if (list.Count == 0) continue;
            Console.WriteLine($"{group.Name} ({list.Count}): " +
                              string.Join(", ", list.Select(r => $"{r.EditorID}[{r.FormKey.ID:X6}]")));
        }
        foreach (var quest in mod.Quests)
        {
            Console.WriteLine($"quest {quest.EditorID} '{quest.Name?.String}' flags={quest.Flags} type={quest.Type} " +
                              $"priority={quest.Priority} stages={quest.Stages.Count} objectives={quest.Objectives.Count}");
            foreach (var alias in quest.Aliases)
                Console.WriteLine($"   alias {alias.ID} {alias.Name} type={alias.Type} flags={alias.Flags}");
            foreach (var objective in quest.Objectives)
                Console.WriteLine($"   objective {objective.Index} '{objective.DisplayText?.String}' targets=" +
                                  string.Join(",", objective.Targets.Select(t => t.AliasID)));
            foreach (var stage in quest.Stages)
                Console.WriteLine($"   stage {stage.Index} flags={stage.Flags} log={stage.LogEntries.Count}" +
                                  string.Concat(stage.LogEntries.Select(l => $" [{l.Flags}] '{l.Entry?.String}'")));
            if (quest.VirtualMachineAdapter is { } vm)
                Console.WriteLine("   scripts: " + string.Join(", ", vm.Scripts.Select(s => s.Name)));
        }
        return 0;
    }
}
