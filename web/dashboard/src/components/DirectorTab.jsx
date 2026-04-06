import { useState, useMemo, useCallback, useRef, useEffect } from 'react';

/** Text input with dropdown suggestions. Works in Ultralight (no <datalist> needed). */
function FactionInput({ value, onChange, factions, placeholder }) {
  const [open, setOpen] = useState(false);
  const ref = useRef(null);

  // Close dropdown on outside click
  useEffect(() => {
    const handler = (e) => { if (ref.current && !ref.current.contains(e.target)) setOpen(false); };
    document.addEventListener('mousedown', handler);
    return () => document.removeEventListener('mousedown', handler);
  }, []);

  // Filter suggestions based on current value (single source of truth)
  const filtered = useMemo(() => {
    if (!value) return factions;
    const lc = value.toLowerCase();
    // Hide suggestions if value exactly matches a faction ID (already selected)
    if (factions.some(f => f.id === value)) return [];
    return factions.filter(f => f.id.toLowerCase().includes(lc) || f.name.toLowerCase().includes(lc));
  }, [factions, value]);

  return (
    <div ref={ref} className="flex-1 relative">
      <input
        type="text"
        value={value}
        onChange={e => { onChange(e.target.value); setOpen(true); }}
        onFocus={() => setOpen(true)}
        placeholder={placeholder}
        className="w-full bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
      />
      {open && filtered.length > 0 && (
        <div className="absolute z-50 top-full left-0 right-0 mt-0.5 max-h-32 overflow-y-auto bg-[#1a1a2e] border border-white/10 rounded shadow-lg">
          {filtered.map(f => (
            <div
              key={f.id}
              onClick={() => { onChange(f.id); setOpen(false); }}
              className="px-2 py-1 text-xs text-gray-300 hover:bg-white/10 cursor-pointer truncate"
            >
              {f.name} <span className="text-gray-600">({f.id})</span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

const DIRECTOR_STORY_TYPES = [
  { key: 'seek_player', label: 'Seek Player' },
  { key: 'informant', label: 'Informant' },
  { key: 'road_encounter', label: 'Road Encounter' },
  { key: 'ambush', label: 'Ambush' },
  { key: 'stalker', label: 'Stalker' },
  { key: 'message', label: 'Message' },
  { key: 'quest', label: 'Quest' },
];

const SOCIAL_TYPES = [
  { key: 'npc_interaction', label: 'Interaction' },
  { key: 'npc_gossip', label: 'Gossip' },
];

const POLITICAL_EVENT_TYPES = [
  { key: 'trade_deal', label: 'Trade Deal', delta: 8 },
  { key: 'diplomatic_gift', label: 'Diplomatic Gift', delta: 10 },
  { key: 'alliance_proposal', label: 'Alliance Proposal', delta: 12 },
  { key: 'peace_offer', label: 'Peace Offer', delta: 8 },
  { key: 'insult', label: 'Insult', delta: -5 },
  { key: 'trade_dispute', label: 'Trade Dispute', delta: -4 },
  { key: 'embargo', label: 'Embargo', delta: -10 },
  { key: 'espionage', label: 'Espionage', delta: -8 },
  { key: 'territory_dispute', label: 'Territory Dispute', delta: -8 },
  { key: 'betrayal', label: 'Betrayal', delta: -12 },
  { key: 'border_skirmish', label: 'Border Skirmish', delta: -10 },
  { key: 'assassination_attempt', label: 'Assassination', delta: -12 },
  { key: 'sabotage', label: 'Sabotage', delta: -10 },
  { key: 'brawl', label: 'Brawl', delta: -6 },
  { key: 'war_declaration', label: 'War Declaration', delta: -15 },
  { key: 'surrender', label: 'Surrender', delta: 8 },
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

const SOCIAL_TYPE_FIELDS = {
  npc_interaction: [
    { key: 'fact1', label: 'NPC 1 memory', type: 'text', required: true,
      placeholder: 'e.g. confronted Sven about stolen goods',
      hint: 'Required. What NPC 1 remembers. Past-tense verb phrase, no subject prefix.' },
    { key: 'fact2', label: 'NPC 2 memory', type: 'text', required: true,
      placeholder: 'e.g. was accused by Mikael of stealing goods',
      hint: 'Required. What NPC 2 remembers. Past-tense verb phrase, no subject prefix.' },
  ],
  npc_gossip: [
    { key: 'gossip', label: 'Gossip', type: 'text', required: true,
      placeholder: 'e.g. was overheard arguing with the steward about missing tribute',
      hint: 'Required. What was said. Past-tense verb phrase, no subject prefix.' },
  ],
};

function DirectorTab({ loadedNpcs, actions, factions = [], sendAction }) {
  // Story dispatch state
  const [storyNpc, setStoryNpc] = useState('');
  const [storyType, setStoryType] = useState('seek_player');
  const [storyNarration, setStoryNarration] = useState('');
  const [storyFields, setStoryFields] = useState({});

  // NPC Social dispatch state
  const [socialNpc1, setSocialNpc1] = useState('');
  const [socialNpc2, setSocialNpc2] = useState('');
  const [socialType, setSocialType] = useState('npc_interaction');
  const [socialNarration, setSocialNarration] = useState('');
  const [socialFields, setSocialFields] = useState({});

  // Political dispatch state
  const [polFactionA, setPolFactionA] = useState('');
  const [polFactionB, setPolFactionB] = useState('');
  const [polEventType, setPolEventType] = useState('trade_deal');
  const [polDescription, setPolDescription] = useState('');
  const [polDelta, setPolDelta] = useState(8);

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

  const currentSocialFields = SOCIAL_TYPE_FIELDS[socialType] || [];

  const handleSocialTypeChange = useCallback((type) => {
    setSocialType(type);
    setSocialFields({});
  }, []);

  const handleDispatchSocial = useCallback(() => {
    if (!socialNpc1.trim() || !socialNpc2.trim() || !socialNarration.trim()) return;
    sendAction('dispatchNpcSocial', {
      npc1Name: socialNpc1.trim(),
      npc2Name: socialNpc2.trim(),
      socialType,
      narration: socialNarration.trim(),
      ...socialFields,
    });
    setSocialNarration('');
    setSocialFields({});
  }, [socialNpc1, socialNpc2, socialType, socialNarration, socialFields, sendAction]);

  const handlePolEventTypeChange = useCallback((type) => {
    setPolEventType(type);
    const evt = POLITICAL_EVENT_TYPES.find(e => e.key === type);
    if (evt) setPolDelta(evt.delta);
  }, []);

  const handleDispatchPolitics = useCallback(() => {
    if (!polFactionA.trim() || !polFactionB.trim() || !polDescription.trim()) return;
    sendAction('dispatchPolitics', {
      factionA: polFactionA.trim(),
      factionB: polFactionB.trim(),
      eventType: polEventType,
      description: polDescription.trim(),
      relationDelta: polDelta,
    });
    setPolDescription('');
  }, [polFactionA, polFactionB, polEventType, polDescription, polDelta, sendAction]);

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

      {/* NPC Social Dispatch */}
      <section>
        <h2 className="section-header mb-2">NPC Social</h2>
        <div className="space-y-2">
          <div className="flex gap-2">
            <input
              type="text"
              value={socialNpc1}
              onChange={e => setSocialNpc1(e.target.value)}
              placeholder="NPC 1 (e.g. Mikael)"
              className="flex-1 bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
            />
            <input
              type="text"
              value={socialNpc2}
              onChange={e => setSocialNpc2(e.target.value)}
              placeholder="NPC 2 (e.g. Sven)"
              className="flex-1 bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
            />
          </div>
          <div className="flex gap-2">
            <select
              value={socialType}
              onChange={e => handleSocialTypeChange(e.target.value)}
              className="w-[130px] bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none"
            >
              {SOCIAL_TYPES.map(t => (
                <option key={t.key} value={t.key}>{t.label}</option>
              ))}
            </select>
            <textarea
              value={socialNarration}
              onChange={e => setSocialNarration(e.target.value)}
              placeholder="Narration (e.g. confronted him about the stolen shipment)"
              rows={1}
              className="flex-1 bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none resize-none placeholder:text-gray-600"
            />
          </div>

          {currentSocialFields.map(f => (
            <div key={f.key} className="flex flex-col gap-0.5">
              <label className="text-[10px] text-gray-500">
                {f.label}
                {f.required && <span className="text-red-400 ml-0.5">*</span>}
              </label>
              <input
                type="text"
                value={socialFields[f.key] || ''}
                onChange={e => setSocialFields(prev => ({ ...prev, [f.key]: e.target.value }))}
                placeholder={f.placeholder}
                className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
              />
              {f.hint && (
                <span className="text-[9px] text-gray-600 leading-tight">{f.hint}</span>
              )}
            </div>
          ))}

          <button
            onClick={handleDispatchSocial}
            disabled={!socialNpc1.trim() || !socialNpc2.trim() || !socialNarration.trim()}
            className="w-full px-3 py-1.5 text-xs font-medium rounded transition-colors bg-purple-500/20 text-purple-400 hover:bg-purple-500/30 disabled:opacity-30 disabled:cursor-not-allowed"
          >
            Dispatch Social
          </button>
        </div>
      </section>

      {/* Political Dispatch */}
      <section>
        <h2 className="section-header mb-2">Political Event</h2>
        <div className="space-y-2">
          <div className="flex gap-2">
            <FactionInput
              value={polFactionA}
              onChange={setPolFactionA}
              factions={factions}
              placeholder="Faction A (e.g. StormcloakFaction)"
            />
            <FactionInput
              value={polFactionB}
              onChange={setPolFactionB}
              factions={factions}
              placeholder="Faction B (e.g. ThalmorFaction)"
            />
          </div>
          <div className="flex gap-2">
            <select
              value={polEventType}
              onChange={e => handlePolEventTypeChange(e.target.value)}
              className="flex-1 bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none"
            >
              {POLITICAL_EVENT_TYPES.map(t => (
                <option key={t.key} value={t.key}>{t.label}</option>
              ))}
            </select>
            <div className="flex items-center gap-1">
              <label className="text-[10px] text-gray-500">Δ</label>
              <input
                type="number"
                value={polDelta}
                onChange={e => setPolDelta(Number(e.target.value))}
                className="w-14 bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none text-center"
                min={-15}
                max={15}
              />
            </div>
          </div>
          <textarea
            value={polDescription}
            onChange={e => setPolDescription(e.target.value)}
            placeholder="Event description (e.g. Imperial spies intercepted Stormcloak dispatches near Windhelm)"
            rows={2}
            className="w-full bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none resize-none placeholder:text-gray-600"
          />
          <button
            onClick={handleDispatchPolitics}
            disabled={!polFactionA.trim() || !polFactionB.trim() || !polDescription.trim()}
            className="w-full px-3 py-1.5 text-xs font-medium rounded transition-colors bg-amber-500/20 text-amber-400 hover:bg-amber-500/30 disabled:opacity-30 disabled:cursor-not-allowed"
          >
            Dispatch Political Event
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
