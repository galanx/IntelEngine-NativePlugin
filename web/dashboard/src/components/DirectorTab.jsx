import { useState, useMemo, useCallback } from 'react';

const DIRECTOR_STORY_TYPES = [
  { key: 'seek_player', label: 'Seek Player' },
  { key: 'informant', label: 'Informant' },
  { key: 'road_encounter', label: 'Road Encounter' },
  { key: 'ambush', label: 'Ambush' },
  { key: 'stalker', label: 'Stalker' },
  { key: 'message', label: 'Message' },
  { key: 'quest', label: 'Quest' },
];

const MEET_TIMES = ['dawn', 'morning', 'afternoon', 'evening', 'sunset', 'night', 'midnight'];
const ENEMY_TYPES = ['bandit', 'draugr', 'dragon'];
const QUEST_SUB_TYPES = ['combat', 'rescue', 'find_item'];

const SPEED_OPTIONS = [
  { value: '0', label: 'Walk' },
  { value: '1', label: 'Jog' },
  { value: '2', label: 'Run' },
];
const WAIT_OPTIONS = [
  { value: '0', label: 'No' },
  { value: '1', label: 'Yes' },
];

// Type-specific fields per story type
// required: true = dispatch will fail without it, hint: shown below the field
const STORY_TYPE_FIELDS = {
  informant: [
    { key: 'subject', label: 'Subject NPC', type: 'text', required: true,
      placeholder: 'e.g. Camilla Valerius',
      hint: 'Required. The NPC the gossip is about. Gets a fact injected.' },
    { key: 'gossip', label: 'Gossip', type: 'text', required: true,
      placeholder: 'e.g. was seen arguing with Adrianne over an unpaid debt',
      hint: 'Required. Past-tense verb phrase without subject name prefix.' },
    { key: 'sender', label: 'Source', type: 'text',
      placeholder: 'e.g. Sven',
      hint: 'Optional. Who the informant heard it from. Empty = witnessed firsthand.' },
  ],
  road_encounter: [
    { key: 'destination', label: 'Destination', type: 'text', required: true,
      placeholder: 'e.g. Falkreath, Solitude, Old Hroldan',
      hint: 'Required. A real Skyrim location. NPC is placed on the road near player.' },
  ],
  ambush: [
    { key: 'sender', label: 'Hired by', type: 'text',
      placeholder: 'e.g. Maven Black-Briar',
      hint: 'Optional. NPC who ordered the ambush. Gets a fact about hiring.' },
  ],
  stalker: [
    { key: 'sender', label: 'Encouraged by', type: 'text',
      placeholder: 'e.g. Faendal',
      hint: 'Optional. NPC who encouraged the stalking. Gets a fact.' },
  ],
  message: [
    { key: 'msgContent', label: 'Message content', type: 'text', required: true,
      placeholder: 'e.g. I need to speak with you about a private matter',
      hint: 'Required. The actual message text. A messenger NPC is auto-selected.' },
    { key: 'destination', label: 'Meeting place', type: 'text',
      placeholder: 'e.g. The Bee and Barb, Dragonsreach',
      hint: 'Optional. If set with meetTime, creates a meeting invitation.' },
    { key: 'meetTime', label: 'Meeting time', type: 'select', options: MEET_TIMES,
      hint: 'Optional. Must match msgContent. Avoid night/midnight for palaces/shops.' },
  ],
  quest: [
    { key: 'questSubType', label: 'Sub-type', type: 'select', options: QUEST_SUB_TYPES, required: true,
      hint: 'Required. combat = kill enemies, rescue = save captive NPC, find_item = retrieve item.' },
    { key: 'questLocation', label: 'Quest location', type: 'text', required: true,
      placeholder: 'e.g. Bleak Falls Barrow, Fort Greymoor, Valtheim Towers',
      hint: 'Required. A real Skyrim dungeon/fort/ruin. Enemies spawn here.' },
    { key: 'enemyType', label: 'Enemy type', type: 'select', options: ENEMY_TYPES, required: true,
      hint: 'Required. bandit = forts/camps, draugr = Nordic ruins, dragon = open areas.' },
    { key: 'sender', label: 'Quest giver', type: 'text',
      placeholder: 'e.g. Jarl Balgruuf',
      hint: 'Optional. If set, NPC becomes a courier. Required for Jarls/leaders (they never travel).' },
    { key: 'msgContent', label: 'Plea for help', type: 'text',
      placeholder: 'e.g. Draugr have been crawling out of the barrow at night',
      hint: 'Optional. Why they need help. Shown in NPC dialogue on arrival.' },
    { key: 'victimName', label: 'Victim (rescue)', type: 'text',
      placeholder: 'e.g. Camilla Valerius',
      hint: 'Rescue only. Exact NPC name. Must exist in-game or quest is rejected.' },
    { key: 'itemName', label: 'Item (find_item)', type: 'text',
      placeholder: 'e.g. Ebony Blade, Glass Sword, Spell Tome: Fireball',
      hint: 'Find_item only. Exact in-game item name. Fallback to random if not found.' },
    { key: 'itemDesc', label: 'Item description', type: 'text',
      placeholder: 'e.g. a legendary elven blade, a powerful spell tome',
      hint: 'Find_item only. Brief narrative description for dialogue context.' },
  ],
};

// Params auto-filled from NPC dropdown or hardcoded — never shown to user
const HIDDEN_PARAMS = new Set(['akNPC', 'akAgent', 'isScheduled']);

// Per-action field definitions with hints (mirrors YAML param specs)
// key must match the pendingParam name that Papyrus reads
const ACTION_FIELDS = {
  CancelCurrentTask: [],
  ChangeSpeed: [
    { key: 'newSpeed', label: 'New speed', type: 'select', required: true,
      options: SPEED_OPTIONS,
      hint: 'Required. Changes speed mid-task without canceling it.' },
  ],
  GoToLocation: [
    { key: 'destination', label: 'Destination', type: 'text', required: true,
      placeholder: 'e.g. Whiterun, Bannered Mare, home',
      hint: 'Required. Named location, "home", "<Name>\'s home", or relative ("upstairs", "outside").' },
    { key: 'speed', label: 'Speed', type: 'select',
      options: SPEED_OPTIONS,
      hint: 'Travel speed. Empty = Walk.' },
    { key: 'waitForPlayer', label: 'Wait for player', type: 'select',
      options: WAIT_OPTIONS,
      hint: 'Yes = player told NPC to go (lingers at destination). No = NPC\'s own decision.' },
  ],
  FetchPerson: [
    { key: 'targetName', label: 'Person to fetch', type: 'text', required: true,
      placeholder: 'e.g. Camilla Valerius, Alvor',
      hint: 'Required. Exact NPC name. Agent goes alone, returns with them.' },
    { key: 'failReason', label: 'Refuse reason', type: 'text',
      placeholder: 'none',
      hint: 'Optional. Empty = comes willingly. If set, target refuses with this reason.' },
  ],
  SearchForActor: [
    { key: 'targetName', label: 'Person to find', type: 'text', required: true,
      placeholder: 'e.g. Lydia, Faendal',
      hint: 'Required. Exact NPC name. Agent leads the player TO the target (travel together).' },
    { key: 'speed', label: 'Speed', type: 'select',
      options: SPEED_OPTIONS,
      hint: 'Travel speed. Empty = Walk.' },
  ],
  DeliverMessage: [
    { key: 'targetName', label: 'Deliver to', type: 'text', required: true,
      placeholder: 'e.g. Jarl Balgruuf, Camilla',
      hint: 'Required. Exact NPC name. Agent goes alone to deliver the message.' },
    { key: 'msgContent', label: 'Message', type: 'text', required: true,
      placeholder: 'e.g. Your presence is requested at Dragonsreach',
      hint: 'Required. The words to relay. Shown in NPC dialogue on arrival.' },
    { key: 'meetLocation', label: 'Meeting place', type: 'text',
      placeholder: 'none',
      hint: 'Optional. If message is a meeting invitation, the location. Empty = not a meeting.' },
    { key: 'meetTime', label: 'Meeting time', type: 'select', options: MEET_TIMES,
      hint: 'Optional. If message is a meeting invitation, the time. Empty = not a meeting.' },
  ],
  EscortTarget: [
    { key: 'targetName', label: 'Person to escort', type: 'text', required: true,
      placeholder: 'e.g. Camilla Valerius, Sven',
      hint: 'Required. Exact NPC name (never the player). Agent walks WITH them.' },
    { key: 'destination', label: 'Destination', type: 'text',
      placeholder: 'home',
      hint: 'Optional. Where to escort them. Empty = "home".' },
    { key: 'shouldWait', label: 'Wait at destination', type: 'select',
      options: WAIT_OPTIONS,
      hint: 'Yes = escorted person waits for player at destination. No = resumes normal life.' },
  ],
  ScheduleMeeting: [
    { key: 'destination', label: 'Destination', type: 'text', required: true,
      placeholder: 'e.g. Bannered Mare, home',
      hint: 'Required. Where to go when the time comes. Location name or NPC name (resolves to their home).' },
    { key: 'timeCondition', label: 'When', type: 'text', required: true,
      placeholder: 'e.g. at dawn, in 2 hours, tomorrow morning',
      hint: 'Required. "at dawn", "after sunset", "in 2 hours", "tonight", "tomorrow morning".' },
  ],
  ScheduleFetch: [
    { key: 'targetName', label: 'Person to fetch', type: 'text', required: true,
      placeholder: 'e.g. Camilla Valerius',
      hint: 'Required. Exact NPC name. Agent stays put now, fetches them later.' },
    { key: 'timeCondition', label: 'When', type: 'text', required: true,
      placeholder: 'e.g. at dawn, tonight, in 3 hours',
      hint: 'Required. When to go fetch them.' },
  ],
  ScheduleDelivery: [
    { key: 'targetName', label: 'Deliver to', type: 'text', required: true,
      placeholder: 'e.g. Jarl Balgruuf',
      hint: 'Required. Exact NPC name. Agent stays put now, delivers message later.' },
    { key: 'msgContent', label: 'Message', type: 'text', required: true,
      placeholder: 'e.g. The Jarl requests your presence',
      hint: 'Required. What to tell them when the time comes.' },
    { key: 'timeCondition', label: 'When', type: 'text', required: true,
      placeholder: 'e.g. at dawn, after sunset',
      hint: 'Required. When to deliver the message.' },
    { key: 'meetLocation', label: 'Meeting place', type: 'text',
      placeholder: 'none',
      hint: 'Optional. If message includes a meeting request. Empty = not a meeting.' },
    { key: 'meetTime', label: 'Meeting time', type: 'select', options: MEET_TIMES,
      hint: 'Optional. If message includes a meeting time. Empty = not a meeting.' },
  ],
};

function DirectorTab({ loadedNpcs, actions, sendAction }) {
  // Story dispatch state
  const [storyNpc, setStoryNpc] = useState('');
  const [storyType, setStoryType] = useState('seek_player');
  const [storyNarration, setStoryNarration] = useState('');
  const [storyFields, setStoryFields] = useState({});

  // Action execution state
  const [actionNpc, setActionNpc] = useState('');
  const [selectedAction, setSelectedAction] = useState('');
  const [actionParams, setActionParams] = useState({});

  const npcs = loadedNpcs || [];

  // Only show IntelEngine actions in the Director tab
  const INTEL_ACTIONS = new Set([
    'GoToLocation', 'FetchPerson', 'DeliverMessage', 'EscortTarget', 'SearchForActor',
    'ScheduleFetch', 'ScheduleDelivery', 'ScheduleMeeting',
    'CancelCurrentTask', 'ChangeSpeed', 'ReportPlayerConduct',
  ]);
  const executableActions = useMemo(
    () => (actions || []).filter(a => a.params && a.params.length > 0 && INTEL_ACTIONS.has(a.name)),
    [actions],
  );

  const currentAction = useMemo(
    () => executableActions.find(a => a.name === selectedAction),
    [executableActions, selectedAction],
  );

  // Use ACTION_FIELDS if defined, otherwise fall back to YAML params (minus hidden ones)
  const currentActionFields = useMemo(() => {
    if (!selectedAction) return [];
    if (ACTION_FIELDS[selectedAction]) return ACTION_FIELDS[selectedAction];
    if (currentAction) {
      return currentAction.params
        .filter(p => !HIDDEN_PARAMS.has(p.name))
        .map(p => ({ key: p.name, label: p.name, type: 'text', placeholder: p.description }));
    }
    return [];
  }, [selectedAction, currentAction]);

  const currentFields = STORY_TYPE_FIELDS[storyType] || [];

  const handleStoryTypeChange = useCallback((type) => {
    setStoryType(type);
    setStoryFields({});
  }, []);

  const handleDispatchStory = useCallback(() => {
    if (!storyNpc.trim() || !storyNarration.trim()) return;
    sendAction('dispatchStory', {
      npcName: storyNpc.trim(),
      storyType,
      narration: storyNarration.trim(),
      ...storyFields,
    });
    setStoryNarration('');
    setStoryFields({});
  }, [storyNpc, storyType, storyNarration, storyFields, sendAction]);

  const handleExecuteAction = useCallback(() => {
    if (!actionNpc || !selectedAction) return;
    sendAction('executeAction', {
      npcFormId: Number(actionNpc),
      actionName: selectedAction,
      params: actionParams,
    });
    setActionParams({});
  }, [actionNpc, selectedAction, actionParams, sendAction]);

  const setParam = useCallback((key, value) => {
    setActionParams(prev => ({ ...prev, [key]: value }));
  }, []);

  const handleActionChange = useCallback((name) => {
    setSelectedAction(name);
    setActionParams({});
  }, []);

  return (
    <div className="p-3 space-y-4">
      {/* Story Dispatch */}
      <section>
        <h2 className="section-header mb-2">Story Dispatch</h2>
        <div className="space-y-2">
          <div className="flex gap-2">
            <input
              type="text"
              value={storyNpc}
              onChange={e => setStoryNpc(e.target.value)}
              placeholder="NPC name (e.g. Saadia)"
              className="flex-1 bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
            />
            <select
              value={storyType}
              onChange={e => handleStoryTypeChange(e.target.value)}
              className="w-[130px] bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none"
            >
              {DIRECTOR_STORY_TYPES.map(t => (
                <option key={t.key} value={t.key}>{t.label}</option>
              ))}
            </select>
          </div>
          <textarea
            value={storyNarration}
            onChange={e => setStoryNarration(e.target.value)}
            placeholder="Narration (e.g. rushes out to find the player because...)"
            rows={2}
            className="w-full bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none resize-none placeholder:text-gray-600"
          />

          {/* Type-specific fields */}
          {currentFields.map(f => (
            <div key={f.key} className="flex flex-col gap-0.5">
              <label className="text-[10px] text-gray-500">
                {f.label}
                {f.required && <span className="text-red-400 ml-0.5">*</span>}
              </label>
              {f.type === 'select' ? (
                <select
                  value={storyFields[f.key] || ''}
                  onChange={e => setStoryFields(prev => ({ ...prev, [f.key]: e.target.value }))}
                  className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none"
                >
                  <option value="">—</option>
                  {f.options.map(o => (
                    <option key={o} value={o}>{o}</option>
                  ))}
                </select>
              ) : (
                <input
                  type="text"
                  value={storyFields[f.key] || ''}
                  onChange={e => setStoryFields(prev => ({ ...prev, [f.key]: e.target.value }))}
                  placeholder={f.placeholder}
                  className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
                />
              )}
              {f.hint && (
                <span className="text-[9px] text-gray-600 leading-tight">{f.hint}</span>
              )}
            </div>
          ))}

          <button
            onClick={handleDispatchStory}
            disabled={!storyNpc.trim() || !storyNarration.trim()}
            className="w-full px-3 py-1.5 text-xs font-medium rounded transition-colors bg-blue-500/20 text-blue-400 hover:bg-blue-500/30 disabled:opacity-30 disabled:cursor-not-allowed"
          >
            Dispatch Story
          </button>
        </div>
      </section>

      {/* Action Execution */}
      <section>
        <h2 className="section-header mb-2">Action Execution</h2>
        <div className="space-y-2">
          <div className="flex gap-2">
            <select
              value={actionNpc}
              onChange={e => setActionNpc(e.target.value)}
              className="flex-1 bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none"
            >
              <option value="">Select NPC...</option>
              {npcs.map(n => (
                <option key={n.formId} value={n.formId}>{n.name}</option>
              ))}
            </select>
            <select
              value={selectedAction}
              onChange={e => handleActionChange(e.target.value)}
              className="w-[150px] bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none"
            >
              <option value="">Select Action...</option>
              {executableActions.map(a => (
                <option key={a.name} value={a.name}>{a.name}</option>
              ))}
            </select>
          </div>

          {/* Dynamic param fields for selected action */}
          {currentActionFields.map(f => (
            <div key={f.key} className="flex flex-col gap-0.5">
              <label className="text-[10px] text-gray-500">
                {f.label}
                {f.required && <span className="text-red-400 ml-0.5">*</span>}
              </label>
              {f.type === 'select' ? (
                <select
                  value={actionParams[f.key] || ''}
                  onChange={e => setParam(f.key, e.target.value)}
                  className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none"
                >
                  <option value="">—</option>
                  {f.options.map(o => typeof o === 'string' ? (
                    <option key={o} value={o}>{o}</option>
                  ) : (
                    <option key={o.value} value={o.value}>{o.label}</option>
                  ))}
                </select>
              ) : (
                <input
                  type="text"
                  value={actionParams[f.key] || ''}
                  onChange={e => setParam(f.key, e.target.value)}
                  placeholder={f.placeholder}
                  className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
                />
              )}
              {f.hint && (
                <span className="text-[9px] text-gray-600 leading-tight">{f.hint}</span>
              )}
            </div>
          ))}

          <button
            onClick={handleExecuteAction}
            disabled={!actionNpc || !selectedAction || !currentActionFields.every(f => !f.required || (actionParams[f.key] || '').trim())}
            className="w-full px-3 py-1.5 text-xs font-medium rounded transition-colors bg-emerald-500/20 text-emerald-400 hover:bg-emerald-500/30 disabled:opacity-30 disabled:cursor-not-allowed"
          >
            Execute Action
          </button>
        </div>
      </section>
    </div>
  );
}

export default DirectorTab;
