"""One name per setting: the MCM and the dashboard use the names of SkyrimNet's plugin settings page.

The page's names are the labels of the table in SKSE/src/SettingsBridge.cpp (59 settings shared with the game) and
the hand-written fields of manifest.yaml (politics, LLM overrides, the dashboard's own). This check reads those,
the MCM script and the dashboard's sources, and fails when a label or an option name differs.

Two rules turn a page name into a menu label:
  * A unit the page carries in brackets stands beside the value on the dashboard:
    "Check interval (game hours)" is the row "Check interval" with the unit "game hours". The MCM keeps the brackets.
  * Under a heading that says what its switches are, the prefix is left out: "Story type: an ambush" is "An ambush"
    under "Story types", "Quest kind: find an item" is "Find an item" under "Quest kinds".

Usage: python RunSettingNamesCheck.py        (no compiler needed)
"""
import pathlib
import re
import sys

repo = pathlib.Path(__file__).resolve().parents[2]
bridge_src = (repo / 'SKSE' / 'src' / 'SettingsBridge.cpp').read_text(encoding='utf-8')
manifest_src = (repo / 'SKSE' / 'Plugins' / 'SkyrimNet' / 'config' / 'plugins' / 'IntelEngine' / 'manifest.yaml').read_text(encoding='utf-8')
mcm_src = (repo / 'Source' / 'Scripts' / 'IntelEngine_MCM.psc').read_text(encoding='utf-8')
settings_src = (repo / 'web' / 'dashboard' / 'src' / 'components' / 'SettingsTab.jsx').read_text(encoding='utf-8')
story_src = (repo / 'web' / 'dashboard' / 'src' / 'components' / 'StoryTab.jsx').read_text(encoding='utf-8')

problems = []


def differ(where, key, found, expected):
    if found != expected:
        problems.append(f'{where}: {key} reads {found!r}, SkyrimNet\'s page has {expected!r}')


# ── SkyrimNet's page: the bridge table ──
STRING = r'"((?:[^"\\]|\\.)*)"'


def calls(text):
    """(kind, argument text) of every table entry; the argument text ends at the call's closing bracket."""
    for match in re.finditer(r'\b(Bool|Num|Choice|Allowed)\(', text):
        depth, i, quoted = 1, match.end(), False
        while depth:
            c = text[i]
            if quoted:
                if c == '\\': i += 1
                elif c == '"': quoted = False
            elif c == '"': quoted = True
            elif c == '(': depth += 1
            elif c == ')': depth -= 1
            i += 1
        yield match.group(1), text[match.end():i - 1]


table = bridge_src[bridge_src.index('std::vector<Entry> BuildTable()'):]
table = table[:table.index('\n        }\n')]
page = {}                # setting key -> name on SkyrimNet's page
for kind, args in calls(table):
    strings = [s.replace('\\"', '"') for s in re.findall(STRING, args)]
    page[strings[0]] = strings[3]          # key, section, name in the file, label
if len(page) < 50:
    sys.exit(f'only {len(page)} settings read from SettingsBridge.cpp: the table has changed shape')


def options(constant):
    """The option names of a select: one literal, or several the compiler joins."""
    literals = re.search(r'constexpr const char\* %s\s*=(.*?);' % constant, bridge_src, re.S).group(1)
    return ''.join(re.findall(STRING, literals)).split('|')


NPC_POLICY, HOLD_POLICY, CONFIRM = options('kNpcPolicy'), options('kHoldPolicy'), options('kConfirm')

# every field of the page, the hand-written ones (politics, LLM, the dashboard's own) among them: path -> name
written = dict((path, name) for name, path in re.findall(r'- name: "([^"]*)"\s*\n\s*path: "([^"]*)"', manifest_src))


def bare(name):
    """A page name without its unit: ("Check interval", "game hours")."""
    match = re.fullmatch(r'(.*) \(((?:game hours|days|units)[^()]*)\)', name)
    return (match.group(1), match.group(2)) if match else (name, None)


def under_heading(name, prefix):
    assert name.startswith(prefix), (name, prefix)
    rest = name[len(prefix):]
    return rest[0].upper() + rest[1:]


STORY_TYPES = {'seek_player': 'typeSeekPlayer', 'informant': 'typeInformant', 'road_encounter': 'typeRoadEncounter',
               'message': 'typeMessage', 'quest': 'typeQuest', 'ambush': 'typeAmbush',
               'faction_ambush': 'typeFactionAmbush', 'stalker': 'typeStalker'}
QUEST_KINDS = ['questCombat', 'questRescue', 'questFindItem', 'questFactionCombat', 'questFactionRescue', 'questFactionBattle']
HOLD_KEYS = ['holdPolicySeekPlayer', 'holdPolicyInformant', 'holdPolicyRoadEncounter', 'holdPolicyMessage',
             'holdPolicyQuest', 'holdPolicyAmbush', 'holdPolicyStalker']

# ── The MCM ──
mcm = dict((oid, label.replace('\\"', '"')) for oid, label in
           re.findall(r'(OID_\w+)\s*=\s*Add(?:Toggle|Slider|Text)Option\(%s' % STRING, mcm_src))
headers = [h.replace('\\"', '"') for h in re.findall(r'AddHeaderOption\(%s\)' % STRING, mcm_src)]
toggles = [t.replace('\\"', '"') for t in re.findall(r'AddToggleOption\(%s' % STRING, mcm_src)]

ACTIONS = ['GoToLocation', 'DeliverMessage', 'FetchPerson', 'EscortTarget', 'SearchForActor', 'ScheduleMeeting',
           'ScheduleFetch', 'ScheduleDelivery']
MCM_KEYS = {'OID_DebugMode': 'debugMode', 'OID_MaxTasks': 'maxTasks', 'OID_DefaultWaitHours': 'defaultWaitHours',
            'OID_DeliveryReportBack': 'reportBack', 'OID_MeetingTimeoutHours': 'meetingTimeoutHours',
            'OID_MeetingGracePeriod': 'meetingGracePeriod', 'OID_LingerReleaseDistance': 'releaseDistance',
            'OID_StoryEngineEnabled': 'storyEnabled', 'OID_StoryEngineInterval': 'storyInterval',
            'OID_AutoBioEnabled': 'autoBioEnabled', 'OID_AutoBioThreshold': 'autoBioThreshold',
            'OID_NPCTickEnabled': 'npcTickEnabled', 'OID_TypeNPCInteraction': 'typeNpcInteraction',
            'OID_TypeNPCGossip': 'typeNpcGossip', 'OID_NPCTickInterval': 'npcTickInterval',
            'OID_NPCSocialCooldown': 'npcSocialCooldown', 'OID_StoryLongAbsence': 'longAbsenceDays',
            'OID_StoryMaxTravel': 'maxTravelDays', 'OID_AllowStuckTeleport': 'allowStuckTeleport',
            'OID_QuestExpiryDays': 'questTimeoutDays'}
for action in ACTIONS:
    MCM_KEYS['OID_Confirm' + action] = 'confirm' + action
    MCM_KEYS['OID_SkipFollower' + action] = 'skipFollower' + action
for oid, key in MCM_KEYS.items():
    differ('MCM', oid, mcm.get(oid), page[key])
for key in STORY_TYPES.values():
    oid = 'OID_T' + key[1:]
    differ('MCM (under "Story types")', oid, mcm.get(oid), under_heading(page[key], 'Story type: '))
for key in QUEST_KINDS:
    oid = 'OID_Q' + key[1:]
    differ('MCM (under "Quest kinds")', oid, mcm.get(oid), under_heading(page[key], 'Quest kind: '))
for heading in ('Story types', 'Quest kinds', page['dangerZonePolicy'], page['playerHomePolicy']) + tuple(page[k] for k in HOLD_KEYS):
    if heading not in headers:
        problems.append(f'MCM: no heading {heading!r}')
for name in NPC_POLICY:
    if toggles.count(name) != 2:
        problems.append(f'MCM: the option {name!r} is there {toggles.count(name)} times, expected 2 (dangerous places, your home)')
for name in HOLD_POLICY:
    if toggles.count(name) != len(HOLD_KEYS):
        problems.append(f'MCM: the option {name!r} is there {toggles.count(name)} times, expected {len(HOLD_KEYS)}')
confirm_labels = re.findall(r'return %s' % STRING, mcm_src[mcm_src.index('String Function ConfirmModeLabel'):][:400])
if sorted(set(confirm_labels)) != sorted(CONFIRM):
    problems.append(f'MCM: ConfirmModeLabel answers {sorted(set(confirm_labels))!r}, the page has {CONFIRM!r}')
differ('MCM', 'OID_PoliticsEnabled', mcm.get('OID_PoliticsEnabled'), written['politics.enabled'])
differ('MCM', 'OID_PoliticsTickInterval', mcm.get('OID_PoliticsTickInterval'), written['politics.tick_interval_hours'])
differ('MCM', 'OID_PEIntegration', mcm.get('OID_PEIntegration'), written['politics.pe_integration'])

# ── One spelling per string ──
# Papyrus keeps a single spelling of every string, whichever it meets first, and compares without regard to case. A
# label that differs only in case from another string of these scripts shows in that other spelling ("Faction
# politics" under the heading "Faction Politics" showed with a capital P), and it can do worse: "Politics" as a
# heading could reach the DLL in place of the key "politics" the politics script hands it.
SAME_WORD_ELSEWHERE = {'Disabled'}       # also "disabled" in a debug message of the story script: harmless both ways
spellings = {}
for script in sorted((repo / 'Source' / 'Scripts').glob('*.psc')):
    for number, line in enumerate(script.read_text(encoding='utf-8', errors='replace').splitlines(), 1):
        for literal in re.findall(STRING, line.split(';', 1)[0]):
            spellings.setdefault(literal.lower(), {}).setdefault(literal, f'{script.name}:{number}')
shown = set(mcm.values()) | set(headers) | set(toggles) | set(confirm_labels) | set(re.findall(r'Pages\[\d\] = %s' % STRING, mcm_src))
for label in sorted(shown - SAME_WORD_ELSEWHERE):
    others = {spelling: where for spelling, where in spellings.get(label.lower(), {}).items() if spelling != label}
    if others:
        problems.append(f'MCM: {label!r} is also spelled {others!r}; Papyrus keeps one spelling per string, so one of the two shows or works wrongly')

# ── The dashboard's Settings tab ──
rows = {}                # setting key or file path -> (label, unit)
for element in re.findall(r'<(?:ToggleRow|SliderRow|PolicyRow|TextInputRow|NumberInputRow)\b.*?/>', settings_src, re.S):
    label = re.search(r'\blabel="([^"]*)"', element)
    handler = re.search(r"on(?:Setting|PluginConfig)Change\('([^']+)'", element)
    unit = re.search(r'\bunit="([^"]*)"', element)
    if label and handler:
        rows[handler.group(1)] = (label.group(1), unit.group(1) if unit else None)
listed = dict(re.findall(r"\{ key: '(\w+)', label: ['\"]([^'\"]*)['\"]", settings_src))


def dashboard(key):
    return rows.get({'typeNpcInteraction': 'npc_interaction', 'typeNpcGossip': 'npc_gossip'}.get(key, key))


for key, name in page.items():
    if key in STORY_TYPES.values():
        continue                                    # switched on the Story tab, below
    if key in listed:
        differ('dashboard', key, listed[key], name)
    elif key in QUEST_KINDS:
        differ('dashboard (under "Quest kinds")', key, (dashboard(key) or (None,))[0], under_heading(name, 'Quest kind: '))
    else:
        differ('dashboard', key, dashboard(key), bare(name))
for path in ('politics.enabled', 'politics.tick_interval_hours', 'politics.max_relation_change_per_tick',
             'politics.max_active_wars', 'politics.pe_integration', 'story.faction_blocklist',
             'story.location_blocklist', 'story.npc_blocklist', 'story.faction_whitelist', 'story.location_whitelist',
             'story.npc_whitelist', 'llm.endpoint', 'llm.api_key', 'llm.model_name', 'llm.temperature',
             'llm.max_tokens', 'llm.timeout', 'ui.scale'):
    found = rows.get(path)
    expected = bare(written[path])
    if path.startswith(('llm.', 'ui.')) and found:
        found = (found[0], None)                    # these rows show their own unit and range hints
        expected = (written[path], None)
    differ('dashboard', path, found, expected)
if 'Quest kinds' not in re.findall(r'<Accordion title="([^"]*)"', settings_src):
    problems.append('dashboard: no section "Quest kinds"')
for label in (written['ui.dashboard_hotkey'], written['ui.dashboard_modifiers']):
    if f'>{label}</span>' not in settings_src:
        problems.append(f'dashboard: no row {label!r}')


def array(name, source=settings_src):
    body = re.search(r'const %s = \[(.*?)\];' % name, source, re.S).group(1)
    return re.findall(r"'([^']*)'", body)


for name, expected in (('NPC_POLICIES', NPC_POLICY), ('HOLD_POLICIES', HOLD_POLICY), ('CONFIRM_MODES', CONFIRM)):
    if array(name) != expected:
        problems.append(f'dashboard: {name} is {array(name)!r}, the page has {expected!r}')

# ── The dashboard's Story tab: the story type switches ──
story = dict(re.findall(r"\{ key: '(\w+)', label: ['\"]([^\"]*?)['\"], type:", story_src))
for raw, key in STORY_TYPES.items():
    differ('dashboard, Story tab (under "Story types")', raw, story.get(raw), under_heading(page[key], 'Story type: '))

# ── A setting only the MCM and the dashboard have ──
differ('dashboard against the MCM', 'politics.pe_reactions_move_relations',
       (rows.get('politics.pe_reactions_move_relations') or (None,))[0], mcm.get('OID_PEReactions'))

if problems:
    print('\n'.join(problems))
    sys.exit(f'{len(problems)} name(s) differ')
print(f'One name per setting: {len(page)} shared settings and {len(written) - len(page)} hand-written fields of SkyrimNet\'s page '
      f'match the MCM ({len(MCM_KEYS) + len(STORY_TYPES) + len(QUEST_KINDS) + 3} options, {len(HOLD_KEYS) + 4} headings) '
      f'and the dashboard ({len(rows) + len(listed)} rows)')
