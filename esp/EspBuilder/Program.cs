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
        mod.WriteToBinary(output);
        Console.WriteLine($"{ThreadQuest} [{quest.FormKey.ID:X6}]: {quest.Aliases.Count} aliases, {quest.Objectives.Count} objectives -> {output}");
        Console.WriteLine($"{LifeSandbox} [{lifeSandbox.FormKey.ID:X6}], {LivesQuest} [{lives.FormKey.ID:X6}]");
        return 0;
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
                Console.WriteLine($"   stage {stage.Index} flags={stage.Flags} log={stage.LogEntries.Count}");
            if (quest.VirtualMachineAdapter is { } vm)
                Console.WriteLine("   scripts: " + string.Join(", ", vm.Scripts.Select(s => s.Name)));
        }
        return 0;
    }
}
