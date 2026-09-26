# IntelEngine SkyrimNet Beta 25 Migration Plan

## Status

Implemented and statically verified on branch `migration/beta25-plugin-content`. A complete GamePlugin test package has been assembled; in-game discovery, action, prompt, dashboard, persistence, and log verification remain pending.

## Objective

Migrate IntelEngine's distributable SkyrimNet actions and prompts from the retired loose roots into one SkyrimNet Beta 25 external bundle while preserving action identities, Papyrus entry points, prompt lookup names, plugin settings, and runtime behavior.

The migration is complete when SkyrimNet discovers `galanx.intelengine`, registers all 11 executable actions exactly once under their existing names, exposes the three existing categories, resolves all 11 prompts, and the IntelEngine dashboard reads and edits the external action files instead of the retired loose directory.

## Decisions and assumptions

1. **External bundle id:** use `galanx.intelengine`.
   - SkyrimNet's converter test fixture already uses `galanx.intelengine` for IntelEngine.
   - The repository copyright/trademark owner is Galanx.
   - The external directory name must exactly equal `manifest.json.id`.
2. **Compatibility target:** SkyrimNet `0.25.0` or newer.
3. **Action identities remain stable in this migration.** Preserve `GoToLocation`, `FetchPerson`, `EscortTarget`, `SearchForActor`, `DeliverMessage`, `CancelCurrentTask`, `ChangeSpeed`, `ScheduleMeeting`, `ScheduleFetch`, `ScheduleDelivery`, and `ReportPlayerConduct`.
   - This avoids orphaning per-action settings and avoids an unrelated Papyrus/dashboard identifier cutover.
   - The supported converter derives canonical lowercase filenames from these names; it does not require snake_case action identities.
   - A later snake_case rename should be a separate atomic migration with its own settings warning and full consumer inventory.
4. **Category identities remain stable:** preserve `intel_travel`, `intel_scheduling`, and `intel_communication` in `customCategory` fields.
5. **Keep `config/plugins/IntelEngine/` outside the external bundle.** Beta 25 still reads per-plugin configuration from `config/plugins/*/manifest.yaml` and `settings.yaml`. IntelEngine's DLL also reads that path directly and calls `GetPluginConfigValue("IntelEngine", ...)`.
6. **Do not move global SkyrimNet runtime/user content.** The character bio paths read by `MemoryDB.cpp`, save-specific dynamic bios, registry, library, overlay, and save layers are owned by SkyrimNet or the player and are not IntelEngine package content.
7. **No Papyrus or ESP behavior change is intended.** A DLL rebuild is required because the dashboard's native action-file paths change. Papyrus recompilation is not required unless implementation changes `.psc` files.
8. **Release version:** if implementation begins from `3.5.0`, publish the external bundle and corresponding mod release as `3.5.1`. If the project version has advanced, increment the then-current patch version once instead of hard-coding a stale version.

## Current state

### Distributable loose content

IntelEngine currently ships 14 action YAML files under `SKSE/Plugins/SkyrimNet/config/actions/`:

- 11 executable actions:
  - `intel_travel.yaml` → `GoToLocation`
  - `intel_fetchnpc.yaml` → `FetchPerson`
  - `intel_escorttarget.yaml` → `EscortTarget`
  - `intel_searchforactor.yaml` → `SearchForActor`
  - `intel_delivermessage.yaml` → `DeliverMessage`
  - `intel_canceltask.yaml` → `CancelCurrentTask`
  - `intel_changespeed.yaml` → `ChangeSpeed`
  - `intel_schedulemeeting.yaml` → `ScheduleMeeting`
  - `intel_schedulefetch.yaml` → `ScheduleFetch`
  - `intel_scheduledelivery.yaml` → `ScheduleDelivery`
  - `intel_report_player_conduct.yaml` → `ReportPlayerConduct`
- 3 category containers:
  - `cat_travel.yaml` → `Travel` / `intel_travel`
  - `cat_scheduling.yaml` → `Scheduling` / `intel_scheduling`
  - `cat_communication.yaml` → `Communication` / `intel_communication`

IntelEngine currently ships 11 prompt files under `SKSE/Plugins/SkyrimNet/prompts/`:

- Custom LLM prompts:
  - `intel_story_dm.prompt`
  - `intel_story_npc_dm.prompt`
  - `intel_political_dm.prompt`
  - `intel_schedule_safety_net.prompt`
- Character-bio submodules:
  - `0197_intel_received_messages.prompt`
  - `0198_intel_schedule_awareness.prompt`
  - `0199_intel_meeting_outcome.prompt`
  - `0200_intel_gossip.prompt`
  - `0800_intel_facts.prompt`
  - `0801_intel_task_awareness.prompt`
  - `0810_intel_political_awareness.prompt`

There are no IntelEngine trigger YAML files in the repository.

### Configuration that must remain in place

`SKSE/Plugins/SkyrimNet/config/plugins/IntelEngine/` is not a retired content root. Preserve:

- `manifest.yaml`
- `settings.sample.yaml`
- `factions.sample.yaml`

At runtime, generated/user-edited `settings.yaml` and `factions.yaml` remain in this directory and must not be overwritten during upgrades.

### Live consumers that must migrate

- `build.ps1` separately copies legacy `prompts/` and recursively copies `config/`.
- `verify.ps1` verifies action YAMLs and prompts only at legacy paths.
- `SKSE/src/DashboardUIManager.cpp` enumerates and toggles YAML files under `Data/SKSE/Plugins/SkyrimNet/config/actions`.
- `web/dashboard/src/components/ActionsTab.jsx` and `DirectorTab.jsx` key behavior by the existing action names. These remain unchanged because action identities remain stable.
- `Source/Scripts/IntelEngine_Core.psc`, `IntelEngine_NPCTasks.psc`, `IntelEngine_Schedule.psc`, and `IntelEngine_Travel.psc` use the existing action-name strings for confirmation and follower-skip behavior. These remain unchanged for the same reason.
- `Source/Scripts/IntelEngine_StoryEngine.psc` and `IntelEngine_Politics.psc` invoke custom prompts by their existing logical names. Prompt names and paths must remain stable.

## Target layout

```text
SKSE/Plugins/SkyrimNet/
├── config/
│   └── plugins/
│       └── IntelEngine/
│           ├── manifest.yaml
│           ├── settings.sample.yaml
│           └── factions.sample.yaml
└── external/
    └── galanx.intelengine/
        ├── manifest.json
        ├── actions/
        │   ├── gotolocation.yaml
        │   ├── fetchperson.yaml
        │   ├── escorttarget.yaml
        │   ├── searchforactor.yaml
        │   ├── delivermessage.yaml
        │   ├── cancelcurrenttask.yaml
        │   ├── changespeed.yaml
        │   ├── schedulemeeting.yaml
        │   ├── schedulefetch.yaml
        │   ├── scheduledelivery.yaml
        │   ├── reportplayerconduct.yaml
        │   ├── travel.yaml
        │   ├── scheduling.yaml
        │   └── communication.yaml
        └── prompts/
            ├── intel_story_dm.prompt
            ├── intel_story_npc_dm.prompt
            ├── intel_political_dm.prompt
            ├── intel_schedule_safety_net.prompt
            └── submodules/character_bio/
                ├── 0197_intel_received_messages.prompt
                ├── 0198_intel_schedule_awareness.prompt
                ├── 0199_intel_meeting_outcome.prompt
                ├── 0200_intel_gossip.prompt
                ├── 0800_intel_facts.prompt
                ├── 0801_intel_task_awareness.prompt
                └── 0810_intel_political_awareness.prompt
```

The action stems above follow the current converter rule: sanitize and case-fold the in-file `name`. Treat the converter output as authoritative if it differs.

Normal mod assets remain outside the external subtree, including `IntelEngine.esp`, `IntelEngine.dll`, PEX files, source scripts, MCM assets, translations, and PrismaUI files.

## Manifest contract

Generate `manifest.json` through the supported `content-convert` CLI. Do not hand-convert the content tree.

Required fields:

- `id`: `galanx.intelengine`
- `type`: `bundle`
- `title`: `IntelEngine`
- `author`: `galanx`
- release `version`: `3.5.1` if the current release remains `3.5.0`
- `min_skyrimnet_version`: stamped by the offline converter from `--target-version 0.25.0`
- a concise tagline and complete description, including the SkyrimNet Beta 25 requirement and non-mod requirements
- `tags`, `nsfw`, and a supported icon key
- `mods[]` with `IntelEngine.esp` marked required

For an external-only mod package, omit legacy `files[]`. Before any Hub submission, generate and review the action-plugin `invocation` metadata required by the Hub validator; do not invent it merely to satisfy local external discovery.

## Delegation plan for Luna subagents

Use Luna for bounded, mechanically verifiable slices. The coordinator retains decisions, integrates the slices, and runs validation once. Subagents must not commit, push, deploy, launch Skyrim, or run project-wide builds/tests; those shared operations happen after all edits are integrated.

### Shared contract for every delegated slice

- External identity is `galanx.intelengine`.
- SkyrimNet compatibility target is `0.25.0`.
- Preserve all 11 existing in-file action names and all three `customCategory` values.
- Preserve `config/plugins/IntelEngine/**` and every runtime consumer of `Plugin_IntelEngine`.
- Do not edit player-managed registry, library, overlay, save, `settings.yaml`, or `factions.yaml` state.
- Do not add compatibility copies or deprecated-path fallbacks.
- Each Luna reports exact files changed, commands it ran, converter warnings, and unresolved assumptions.

### Wave A — parallel independent slices

| Luna assignment | Exclusive ownership | Deliverable |
|---|---|---|
| Content conversion | `SKSE/Plugins/SkyrimNet/external/galanx.intelengine/**` and removal of the 25 corresponding loose action/prompt files | Reproducible converter input description, successful converter report, checked-in external tree, and an exact old→new path map |
| Build and verification migration | `build.ps1`, `verify.ps1` | Repository-relative source handling, explicit Data/deploy paths, external-tree staging and verification, known-file legacy cleanup, and no automatic Git operation during validation |
| Dashboard native path migration | `SKSE/src/DashboardUIManager.cpp` | Both action enumeration and toggle-write paths use the external bundle; plugin-config and global character-bio paths remain untouched |

These slices may run concurrently because their file ownership does not overlap. The coordinator provides the shared contract in each assignment rather than asking subagents to rediscover scope.

### Coordinator integration gate

After Wave A, the coordinator:

1. Reviews the converter report and exact path map.
2. Searches for remaining IntelEngine-owned loose action/prompt paths and accidental action-name changes.
3. Confirms `config/plugins/IntelEngine/**` and unrelated global SkyrimNet prompt paths were not moved.
4. Resolves cross-slice path constants and build-script assumptions.
5. Runs the converter-parity comparison, DLL build, dashboard build, staging, and static/package verification once.

No Luna should validate mid-flight: project-wide commands would race sibling edits and waste quota by repeating the same work.

### Wave B — delegate only after integration is stable

| Luna assignment | Exclusive ownership | Deliverable |
|---|---|---|
| Current documentation update | `README.md` and any explicitly identified current release-facing document, excluding historical plans | Beta 25 requirement, external bundle path/id, preserved action identities, configuration-path distinction, and upgrade guidance |
| Archive inspection | Read-only inspection of the final staged package/archive | File inventory proving one external bundle, 14 action YAMLs, 11 prompts, no loose duplicates, and no user/generated state |
| Log triage | Read-only review of SkyrimNet and IntelEngine logs after the coordinator runs the game | Evidence table for discovery, registrations, prompt failures, old-path errors, duplicate actions, and native exceptions |

The coordinator then performs the actual in-game actions, dashboard toggle, settings persistence check, final diff review, commit, and any push requested by the user. These are integration-sensitive or user-session operations and should not be delegated.

### Quota discipline

- Prefer one Luna per Wave A ownership slice; do not create agents for single searches or one-line edits.
- Pass file paths, invariants, and acceptance criteria in the initial assignment so Luna does not spend quota reconstructing the migration.
- Reuse a Luna only for corrections within its owned slice. Do not ask every Luna to review the entire repository.
- Keep build, converter parity, package inspection, and runtime verification centralized unless a read-only archive/log review is explicitly delegated.

## Implementation plan

### Phase 1 — Freeze and record the baseline

1. Confirm the branch is `migration/beta25-plugin-content` and the worktree is clean except for approved migration work.
2. Record the current versions in `SKSE/CMakeLists.txt`, the legacy plugin manifest, and release documentation.
3. Enumerate the 14 action YAMLs, 11 prompts, three plugin-config files, and every live legacy-path consumer.
4. Search for all 11 action names, all 11 prompt names, `config/actions`, `SkyrimNet/prompts`, and `config/plugins/IntelEngine`; classify every occurrence as distributable content, a consumer that must change, or a runtime/global path that must stay.
5. Record current converter and SkyrimNet revisions used for the migration so the generated output is reproducible.

**Checkpoint:** no content has moved; the exact migration closure and deliberate non-moves are documented.

### Phase 2 — Build a converter input and manifest

1. Create a temporary legacy source tree outside the checked-in content destination.
2. Copy only IntelEngine's current `prompts/**` and `config/actions/**` into that temporary tree.
3. Do not copy `config/plugins/IntelEngine`, user settings, generated files, save content, dashboard output, DLLs, scripts, or ESP files into converter input.
4. Create temporary manifest-fields JSON using the contract above.
5. Build or locate SkyrimNet's `content-convert` executable from `D:\git\SkyrimNet`.

**Checkpoint:** converter input contains exactly 25 source content files and no configuration or runtime assets.

### Phase 3 — Convert and install the external layer

1. Run:

   ```powershell
   content-convert <temporary-source> <temporary-output> --target-version 0.25.0 --manifest <manifest-fields.json>
   ```

2. Require a successful report with 25 converted files, zero rejected files, and no unexplained skips.
3. Review all reference-normalization and unresolved-reference reports. Resolve every literal prompt reference before proceeding; manually review non-literal reference sites.
4. Verify the converter preserved all in-file action names, Papyrus mappings, parameter mappings, category join keys, priorities, eligibility rules, event strings, and prompt bytes except documented normalization.
5. Move the validated output into `SKSE/Plugins/SkyrimNet/external/galanx.intelengine/`.
6. Remove IntelEngine's checked-in loose `config/actions/**` and `prompts/**` files. Do not remove `config/plugins/IntelEngine/**`.
7. Run the converter again into a fresh temporary output and compare it recursively with the checked-in external layer.

**Checkpoint:** the repository contains one distributable copy of each IntelEngine action and prompt, all under the external bundle.

### Phase 4 — Migrate build, deployment, and dashboard consumers

1. Update `build.ps1` to copy `SKSE/Plugins/SkyrimNet/external/galanx.intelengine/**` recursively into the matching Data path.
2. Remove the legacy prompt-copy block and ensure the generic config copy continues to deploy `config/plugins/IntelEngine/manifest.yaml` plus sample files while excluding user-owned `settings.yaml` and `factions.yaml`.
3. Update `verify.ps1` to compare every file in the external bundle from source → Data → each deployment target. Keep separate verification for `config/plugins/IntelEngine` where useful.
4. Add targeted cleanup for IntelEngine's known old loose action and prompt files in the Data staging tree and IntelEngine mod deployment directories. Do not delete shared SkyrimNet roots or use a mirror operation that can erase user files.
5. Change `DashboardUIManager.cpp` action enumeration and toggle paths to `Data/SKSE/Plugins/SkyrimNet/external/galanx.intelengine/actions`.
6. Keep `DashboardConfig.cpp`, `FactionPolitics.cpp`, `NPCIndex.cpp`, and their `GetPluginConfigValue("IntelEngine", ...)` calls on `config/plugins/IntelEngine`; that configuration system is separate from content distribution.
7. Keep `MemoryDB.cpp` character-bio and save paths unchanged; they refer to SkyrimNet-owned runtime content, not IntelEngine's packaged prompts.
8. Make the build scripts operate intentionally on this repository rather than silently sourcing `E:\Tools\spookys-automod-toolkit\Mods\IntelEngine`. Prefer `$PSScriptRoot` for repository assets and explicit parameters/configuration for the separate Data repository and deploy targets.
9. During migration work, invoke deployment with `-SkipGit`; the build script must not commit or push either repository as a side effect of validation.

**Checkpoint:** no executable code opens IntelEngine action YAMLs from the retired directory, and the build can stage the external bundle from this branch.

### Phase 5 — Rebuild affected artifacts

1. Build the native plugin with:

   ```powershell
   .\SKSE\BuildDLL.ps1
   ```

2. Build the dashboard with:

   ```powershell
   npm --prefix .\web\dashboard run build
   ```

3. Stage/deploy with the revised build workflow using `-DeployOnly -SkipGit` or the equivalent explicit non-publishing command.
4. Recompile Papyrus only if implementation changed `.psc` files. If so, compile every changed script and verify the corresponding PEX output is newer than its source.
5. Align release version references only when preparing the release: external manifest, CMake project version, and current release documentation must agree.

**Checkpoint:** DLL and dashboard builds succeed, and staged output contains current artifacts plus the external content layer.

### Phase 6 — Static and package verification

1. Validate `manifest.json` structure, strict semver fields, author/id/directory agreement, required mod metadata, and absence of retired fields.
2. Confirm each action filename stem equals its in-file `name` case-insensitively.
3. Confirm all 11 executable action definitions retain their original `questEditorId`, `scriptName`, `executionFunctionName`, parameter order, category, eligibility, event, and priority semantics.
4. Confirm the three containers retain their category join keys and contain eligible descendants.
5. Confirm all 11 prompt logical paths exist and all converter-reported references resolve.
6. Confirm no IntelEngine content remains under loose `config/actions/` or loose `prompts/` in source, staging, deployed mod directories, or the release archive.
7. Confirm `config/plugins/IntelEngine/manifest.yaml` and sample configs remain packaged, while user-created `settings.yaml` and `factions.yaml` are neither packaged nor overwritten.
8. Inspect the final release archive/file manifest. It must contain one `galanx.intelengine` external bundle, no duplicate actions/prompts, and no registry, library, overlay, save, temporary converter, or generated user content.

### Phase 7 — Runtime verification in SkyrimNet Beta 25+

1. Install the complete IntelEngine build through the normal mod-manager route without editing SkyrimNet's player-managed `content-registry.json`.
2. Start Skyrim and confirm the logs discover and enable `galanx.intelengine` with no rejected or skipped content.
3. Confirm all 11 executable actions register exactly once under their existing names.
4. Confirm `Travel`, `Scheduling`, and `Communication` appear only when they have eligible children and route to the correct actions.
5. Exercise one immediate action, one scheduled action, and `ReportPlayerConduct`; verify the expected Papyrus functions execute.
6. Exercise the Story DM, NPC social DM, Political DM, and schedule safety-net prompt lookups by their existing names.
7. Open dialogue that renders at least one IntelEngine character-bio submodule and verify its live context appears.
8. Open the IntelEngine dashboard:
   - verify the Actions tab lists 11 IntelEngine actions;
   - toggle one action, confirm the external YAML changes, restart, and confirm the state persists;
   - execute a representative action from the Director tab.
9. Verify plugin settings still load through `Plugin_IntelEngine`, dashboard hotkey/settings writes still persist, and faction configuration still loads without being reset.
10. Review SkyrimNet and IntelEngine logs for duplicate action registrations, missing prompt/template errors, old-path file errors, and native exceptions.

**Checkpoint:** content discovery, action execution, prompt rendering, dashboard editing, and plugin configuration all work from the migrated layout.

### Phase 8 — Documentation and release cleanup

1. Update current README architecture, install requirements, action authoring paths, dashboard behavior, and migration notes for SkyrimNet Beta 25+.
2. Warn users that the external plugin id is `galanx.intelengine` and may require enablement/priority review on first upgrade.
3. State that action names were intentionally preserved, so existing per-action settings should remain keyed correctly; still ask users to review them after migration.
4. Document that plugin configuration remains under `config/plugins/IntelEngine` and is not part of the external content directory.
5. Remove temporary converter inputs/outputs and throwaway verification scripts.
6. Commit the migration as a clean cutover. Do not retain duplicate loose files, aliases, compatibility copies, or deprecated path fallbacks.

## Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Wrong external id or author | Bundle is rejected or discovered under the wrong ownership | Use `galanx.intelengine`; validate directory/id/author agreement before runtime testing |
| Treating plugin configuration as retired content | Settings, LLM variant, dashboard hotkey, and faction configuration break | Preserve `config/plugins/IntelEngine` and every `GetPluginConfigValue("IntelEngine", ...)` consumer |
| Dashboard keeps legacy paths | Actions tab is empty and toggles silently fail | Update both enumeration and write paths; rebuild DLL; verify a real toggle survives restart |
| Build script operates on the E: clone instead of this branch | Validation reports success against unrelated files | Make source paths repo-relative and deployment paths explicit before using the build script |
| Old loose files remain in Data/deploy targets | Duplicate/stale content complicates diagnosis | Remove only the known IntelEngine legacy files; verify absence in each output and archive |
| Converter renames files unexpectedly | Hand-authored target layout drifts from supported rules | Treat converter output/report as authoritative and compare a second fresh conversion |
| Action identity changes accidentally | Existing settings and dashboard/Papyrus dispatch break | Preserve all 11 in-file names and exact-search them before and after conversion |
| Custom prompt lookup changes | Story, social, political, or safety-net requests fail | Preserve logical prompt paths/names and exercise each request type in game |
| Package includes generated/user state | Saves, secrets, or personal settings leak into release | Allowlist source content; explicitly reject registry, library, overlay, saves, settings, and temporary files |
| `build.ps1` commits or pushes during validation | Partial migration is published | Use `-SkipGit`; commit and push only after all checkpoints pass |

## Definition of done

- The migration lives on `migration/beta25-plugin-content`.
- `SKSE/Plugins/SkyrimNet/external/galanx.intelengine/manifest.json` is valid for SkyrimNet Beta 25.
- Exactly 14 action YAMLs and 11 prompts are packaged under that external bundle.
- No IntelEngine action or prompt is packaged under retired loose roots.
- `config/plugins/IntelEngine` remains functional and user settings are preserved.
- All 11 action identities and Papyrus mappings are unchanged.
- Build, verification, DLL, dashboard, staging, and archive checks pass.
- Runtime verification covers discovery, categories, immediate and scheduled actions, all custom prompt families, a character-bio submodule, dashboard toggling/execution, and plugin settings.
- Current documentation describes the new path, Beta 25 requirement, plugin identity, and upgrade behavior.
- No temporary converter data, player-managed state, compatibility duplicate, or unintended generated file is committed.
