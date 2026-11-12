import withSections from './Section';
import { Children, cloneElement, useState, useMemo, useCallback, useEffect, useRef } from 'react';
import Select, { useDropdown, dropdownClass } from './Select';

function resizeTextarea(event) {
  if (event.button !== 0) return;
  event.preventDefault();
  const handle = event.currentTarget, textarea = handle.parentElement.querySelector('textarea');
  const startY = event.clientY, height = textarea.offsetHeight, scale = textarea.getBoundingClientRect().height / height;
  const style = getComputedStyle(textarea), min = Math.max(parseFloat(style.minHeight) || 0, textarea.rows * parseFloat(style.lineHeight) + parseFloat(style.paddingTop) + parseFloat(style.paddingBottom) + parseFloat(style.borderTopWidth) + parseFloat(style.borderBottomWidth));
  handle.setPointerCapture(event.pointerId);
  handle.onpointermove = move => { textarea.style.height = `${Math.min(400, Math.max(min, height + (move.clientY - startY) / scale))}px`; };
  handle.onlostpointercapture = () => { handle.onpointermove = null; };
  handle.onpointerup = up => handle.releasePointerCapture(up.pointerId);
}

function Field({ label, required, hint, children, className = 'flex-1' }) {
  return (
    <div className={`flex ${className} min-w-0 flex-col gap-0.5`}>
      <label className="text-[10px] text-gray-500">{label} {required && <span className="text-red-400 ml-0.5">*</span>}</label>
      {Children.map(children, child => ['input', 'textarea'].includes(child?.type) && child.props.type !== 'number' ? (
        <div className="director-text-field relative min-w-0">
          {cloneElement(child, { 'aria-label': child.props['aria-label'] || label })}
          {child.props.value && <button type="button" aria-label={`Clear ${label}`}
            onMouseDown={e => e.preventDefault()} onClick={() => child.props.onChange({ target: { value: '' } })}
            className={`absolute right-1 ${child.type === 'textarea' ? 'top-1' : 'top-1/2 -translate-y-1/2'} px-1 text-xs text-gray-500 hover:text-gray-200`}>×</button>}
          {child.type === 'textarea' && <span className="director-resize-handle" aria-label={`Resize ${label}`} onPointerDown={resizeTextarea} />}
        </div>
      ) : child)}
      {hint && <span className="director-hint">{hint}</span>}
    </div>
  );
}

/** Text input with dropdown suggestions. Works in Ultralight (no <datalist> needed). */
function FactionInput({ value, onChange: change, onInput, factions = [], options: entries = factions, npc = entries !== factions, selectionOnly = false, query = null, setQuery = () => {}, ...inputProps }) {
  const [open, setOpen] = useState(false);
  const [showAll, setShowAll] = useState(false);
  const [active, setActive] = useState(0);
  const text = query ?? (selectionOnly ? entries.find(f => f.id === String(value))?.name || '' : value);
  const openDropdown = () => setOpen(true);
  const onChange = value => { if (npc) { onInput?.(); change({ target: { value } }); } else change(value); };

  const filtered = useMemo(() => {
    const lc = showAll ? '' : text.toLowerCase();
    const matches = entries.filter(f => f.id.toLowerCase().includes(lc) || f.name.toLowerCase().includes(lc));
    matches.sort((a, b) => (a.name.toLowerCase() === lc ? -2 : a.name.toLowerCase().startsWith(lc) ? -1 : 0) - (b.name.toLowerCase() === lc ? -2 : b.name.toLowerCase().startsWith(lc) ? -1 : 0));
    const factionMatches = matches.filter(f => f.kind === 'faction').slice(0, 10);
    return [...matches.filter(f => f.kind !== 'faction').slice(0, 50 - factionMatches.length), ...factionMatches];
  }, [entries, text, showAll]);
  const expanded = open && (npc || filtered.length > 0);
  const { ref, menuRef, openUp, maxHeight } = useDropdown(expanded, setOpen, filtered.length);

  return (
    <div ref={ref} data-npc-picker={npc || undefined} className="min-w-0 relative">
      <input
        {...inputProps} role={npc ? 'combobox' : undefined} aria-expanded={npc ? expanded : undefined} aria-autocomplete={npc ? 'list' : undefined}
        type="text"
        value={text}
        onChange={e => { if (selectionOnly) setQuery(e.target.value); onChange(selectionOnly ? '' : e.target.value); setActive(0); setShowAll(false); openDropdown(); }}
        onFocus={() => { setShowAll(true); openDropdown(); }}
        onClick={() => { setShowAll(true); openDropdown(); }}
        onKeyDown={e => { if (e.key === 'Escape' && expanded) { e.stopPropagation(); setOpen(false); } else if (npc && ['ArrowDown','ArrowUp','Enter'].includes(e.key)) { e.preventDefault(); if (!expanded) { setActive(0); openDropdown(); } else if (e.key === 'Enter' && filtered[active]) { onChange(filtered[active].id); setQuery(null); setOpen(false); } else setActive(i => (i + (e.key === 'ArrowUp' ? -1 : 1) + filtered.length) % (filtered.length || 1)); } }}
        className="w-full bg-white/5 border border-white/10 rounded px-2 py-1.5 pr-6 text-xs text-gray-200 outline-none placeholder:text-gray-600"
      />
      {text && (
        <button
          type="button"
          aria-label={npc ? `Clear ${inputProps['aria-label'] || 'NPC'}` : 'Clear faction'}
          onMouseDown={e => e.preventDefault()}
          onClick={() => { onChange(''); setQuery(null); setShowAll(true); openDropdown(); }}
          className="absolute right-1 top-1/2 -translate-y-1/2 px-1 text-xs text-gray-500 hover:text-gray-200"
        >
          ×
        </button>
      )}
      {expanded && (
        <div role={npc ? 'listbox' : undefined} ref={menuRef} style={{ maxHeight }} className={`${dropdownClass} ${openUp ? 'bottom-full mb-0.5' : 'top-full mt-0.5'}`}>
          {filtered.map((f, index) => (
            <div
              key={f.id}
              role={npc ? 'option' : undefined} aria-selected={npc ? f.id === String(value) : undefined} onMouseDown={e => e.preventDefault()}
              onClick={() => { onChange(f.id); setQuery(null); setOpen(false); }}
              className={`px-2 py-1 text-xs text-gray-300 hover:bg-white/10 cursor-pointer truncate ${active === index ? 'bg-white/10' : ''}`}
            >
              {f.name} {(!npc || f.kind) && <span className="text-gray-600">({npc ? f.kind : f.id})</span>}
            </div>
          ))}
          {!filtered.length && <div className="px-2 py-1 text-xs text-gray-500">{selectionOnly ? 'no nearby NPC matches' : 'No matches'}</div>}
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
  seek_player: { example: 'e.g. set out to repay an old debt', hint: 'Travels to find you for a personal reason.', narration: 'Why they sought the named character (past tense, under 120 characters); do not claim the character’s location.' },
  informant: { hint: 'Comes to tell you something they saw or heard.', fields: [
    { key: 'subject', label: 'Subject', type: 'text', required: true,
      placeholder: 'e.g. Camilla Valerius',
      hint: 'Required. The NPC the rumour is about; their name must appear in the Rumor.', half: true },
    { key: 'sender', label: 'Source', type: 'text',
      placeholder: 'e.g. Sven',
      hint: 'Who told the informant (may be the Subject); empty = witnessed firsthand.', half: true },
    { key: 'rumor', label: 'Rumor', type: 'text', required: true,
      placeholder: 'e.g. Camilla Valerius was seen arguing with Adrianne over an unpaid debt',
      hint: 'Required. One full past-tense sentence that names the Subject. Must start with a name, not a verb or an -ing word.' },
  ] },
  road_encounter: { example: 'e.g. set out to sell pelts in Helgen', hint: 'Appears on the road ahead of you, travelling to Destination on their own errand (outdoors only).', narration: 'Why they are travelling, e.g. set out to sell pelts in Helgen.', fields: [
    { key: 'destination', label: 'Destination', type: 'text', required: true,
      placeholder: 'e.g. Falkreath, Solitude, Old Hroldan',
      hint: 'Where they are heading. Required: a real Skyrim location.' },
  ] },
  ambush: { example: 'e.g. was hired by the Thieves Guild to collect a debt', hint: 'The NPC above turns hostile and attacks you (outdoors only). Pick someone who can fight.', narration: 'Why they wanted to attack the named character, and who hired them if anyone.', fields: [
    { key: 'sender', label: 'Hired by', type: 'text',
      placeholder: 'e.g. Maven Black-Briar',
      hint: 'Optional. An NPC or a faction (its leaders learn they ordered it).' },
  ] },
  stalker: { example: 'e.g. confessed to obsessive pursuit out of jealousy', hint: 'Secretly follows you out of romance, jealousy or obsession; flees when caught (outdoors only).', narration: 'The confrontation when caught, not their hiding; include their motive.', fields: [
    { key: 'sender', label: 'Encouraged by', type: 'text',
      placeholder: 'e.g. Faendal',
      hint: 'Optional. An NPC or a faction (its leaders learn they ordered it).' },
  ] },
  message: { example: 'e.g. wanted to arrange a private meeting about a debt', hint: 'Sends a nearby courier to deliver a message from the selected NPC.', narration: 'Why the sender wanted contact.', fields: [
    { key: 'msgContent', label: 'Message content', type: 'text', required: true,
      placeholder: 'e.g. I need to speak with you about a private matter',
      hint: 'Required. The message text. The NPC you pick is the sender; IE picks a nearby courier.' },
    { key: 'destination', label: 'Meeting place', type: 'text',
      placeholder: 'e.g. The Bee and Barb, Dragonsreach',
      hint: 'Optional. If set with meetTime, creates a meeting invitation.' },
    { key: 'meetTime', label: 'Meeting time', type: 'select', options: MEET_TIMES,
      hint: 'Optional. Must match msgContent. Avoid night/midnight for palaces/shops.' },
  ] },
  quest: { example: 'e.g. needed help rescuing a friend from Fort Greymoor', hint: 'Sends an NPC to ask for help with combat, a rescue or finding an item; leaders use a courier.', narration: 'Why help is needed, consistent with the quest location and sub-type.', fields: [
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
  ] },
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
      placeholder: 'e.g. "I\'m on duty."',
      hint: 'Optional. Leave empty and the person comes along. If filled, they refuse to come and give this reason.' },
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

const IDENTITY_KEYS = ['npcName', 'npc1Name', 'npc2Name', 'targetName', 'subject', 'sender', 'victimName', 'factionA', 'factionB'];

const stripExamples = value => typeof value === 'string' ? value.replace(/\s*e\.g\.[^.;]*(?:[.;]|$)/gi, '').replace(/"[^"]*"/g, '').trim() : value;
function DirectorTab({ loadedNpcs, knownNpcs = [], tasks = [], actions, factions = [], story, social, sendAction }) {
  // Story dispatch state
  const [storyNpc, setStoryNpc] = useState('');
  const [storyType, setStoryType] = useState('seek_player');
  const [storyNarration, setStoryNarration] = useState('');
  const [storyFields, setStoryFields] = useState({});
  const [replacementConfirm, setReplacementConfirm] = useState('');


  // NPC Social dispatch state
  const [socialNpc1, setSocialNpc1] = useState('');
  const [socialNpc2, setSocialNpc2] = useState('');
  const [socialType, setSocialType] = useState('npc_interaction');
  const [socialNarration, setSocialNarration] = useState('');
  const [socialFields, setSocialFields] = useState({});

  // Political dispatch state
  const [polFactionA, setPolFactionA] = useState('');
  const [polFactionB, setPolFactionB] = useState('');
  const [polEventType, setPolEventType] = useState('');
  const [polDescription, setPolDescription] = useState('');
  const [polDelta, setPolDelta] = useState(8);
  const [touched, setTouched] = useState({});
  const [pending, setPending] = useState(null);
  const [generateError, setGenerateError] = useState('');
  const pendingRef = useRef(null);
  const requestId = useRef(0);
  const reportGenerate = (request, outcome) => send('generateDirector', {requestId:request.id,form:request.form,outcome:{filled:Object.keys(request.fields).filter(k=>request.fields[k] !== ''),blank:Object.keys(request.fields).filter(k=>request.fields[k] === ''),...outcome}});
  const stopGenerate = () => { if (pendingRef.current) reportGenerate(pendingRef.current, {ok:false,applied:[],rejected:{$response:'Stopped'}}); requestId.current++; pendingRef.current = null; setPending(null); setGenerateError(''); };
  const submitGenerate = request => {
    request.touched = Object.fromEntries(Object.keys(request.fields).map(k => [k, request.fields[k] !== '' && (request.followUp || touched[k] === true)]));
    setTouched(t => ({...t, ...Object.fromEntries(Object.keys(request.draft).filter(k => request.draft[k] === '').map(k => [k, false]))}));
    pendingRef.current = request; setPending(request); setGenerateError('');
    send('generateDirector', { requestId:request.id, form:request.form, fields:Object.fromEntries(Object.entries(request.fields).filter(([, v]) => v !== '')), blank:Object.keys(request.fields).filter(k=>request.fields[k] === ''), touched:request.touched, schema:request.schema, variants:request.variants, options:request.options, followUp:!!request.followUp });
  };
  useEffect(() => {
    window.directorGenerateResult = json => {
      const request = pendingRef.current;
      if (!request) return;
      let result;
      try { result = JSON.parse(json); } catch { pendingRef.current = null; setPending(null); setGenerateError('Invalid response; try again.'); return; }
      if (result.requestId !== request.id || result.form !== request.form) return;
      const audit = {ok:false,applied:[],rejected:{$response:result.error || 'Invalid result'}}; try {
      Object.assign(audit.rejected, result.rejected || {});
      pendingRef.current = null; setPending(null);
      if (!result.ok || !result.fields || typeof result.fields !== 'object' || Array.isArray(result.fields)) {
        setGenerateError(String(result.error || 'Generation failed; try again.').slice(0,120)); return;
      }
      const { form, draft: old, schema, variants, options } = request;
      const typeKey = form === 'action' ? 'actionName' : form === 'story' ? 'storyType' : form === 'social' ? 'socialType' : 'eventType';
      const typeOptions = schema[typeKey]?.options || [];
      const nextType = typeOptions.includes(old[typeKey]) ? old[typeKey] : typeOptions.includes(result.fields[typeKey]) ? result.fields[typeKey] : old[typeKey];
      const allowed = { ...schema, ...(variants[nextType] || {}) };
      const next = { ...old }, skipped = new Set(), npcKeys = ['npcName','npc1Name','npc2Name','targetName','subject','sender','victimName'];
      for (const [key, rawValue] of Object.entries(result.fields)) {
        let value = rawValue;
        if (form === 'action' && key === 'waitForPlayer') value = '';
        if (form === 'action' && key === 'destination' && typeof value === 'string' && options.targets.some(name => name.toLowerCase() === value.trim().toLowerCase())) { audit.rejected[key] = 'Destination must be a place'; continue; }
        if (value === null && allowed[key] && !allowed[key].required) { skipped.add(key); continue; }
        if (npcKeys.includes(key) && typeof value === 'string' && (!value.trim() || /^(?:none|placeholder|e\.g\..*|.*\(e\.g\..*\))$/i.test(value.trim()))) { skipped.add(key); continue; }
        if (!allowed[key]) { audit.rejected[key] = 'Unknown field'; continue; }
        if (IDENTITY_KEYS.includes(key) && typeof value === 'string') {
          const names = key === 'targetName' ? options.targets : [...options.npcs, ...options.factions.map(f => f.name)];
          const matches = names.filter(name => name.replace(/ \[[^\]]*\]$/, '').toLowerCase() === value.toLowerCase());
          if (!names.includes(value) && matches.length === 1) value = matches[0];
        }
        if (['factionA','factionB','factionId'].includes(key) && typeof value === 'string') value = options.factions.find(f=>f.id===value || f.name===value)?.id || '';
        if (allowed[key].type === 'number' ? typeof value !== 'number' : typeof value !== 'string') { audit.rejected[key] = 'Invalid type'; continue; }
        if (npcKeys.includes(key) && typeof value === 'string' && value.trim() && npcKeys.some(other => other !== key && value.trim().toLowerCase() === String(next[other] || '').trim().toLowerCase())) { audit.rejected[key] = 'Duplicate NPC'; continue; }
        if (((touched[key] === true || request.followUp || IDENTITY_KEYS.includes(key)) && old[key])) { audit.rejected[key] = 'Preserved or unchanged'; continue; }
        if (key === 'narration' && typeof value === 'string' && [...IDENTITY_KEYS.map(k => next[k]), ...IDENTITY_KEYS.map(k => result.fields[k]), ...options.npcs].some(name => { if (typeof name !== 'string' || !name.trim() || name.trim().toLowerCase() === String(next.npcName || '').trim().toLowerCase()) return false; const text = value.trim(), prefix = name.trim(); return text.slice(0, prefix.length).toLowerCase() === prefix.toLowerCase() && /^\s+(?:approach(?:es|ed)?|comes\s+to|came\s+to|finds|found|walks\s+up\s+to|walked\s+up\s+to|seeks\s+out|sought\s+out)\b/i.test(text.slice(prefix.length)); })) { audit.rejected[key] = 'Wrong actor'; continue; }
        const placeholderText = [...IDENTITY_KEYS.map(k => next[k]), ...IDENTITY_KEYS.map(k => result.fields[k]), ...options.npcs].filter(name => typeof name === 'string' && name).reduce((text, name) => text.split(name).join(''), String(value));
        if (allowed[key].type === 'text' && !IDENTITY_KEYS.includes(key) && /\[[^\]]+\]/.test(placeholderText)) { audit.rejected[key] = 'Placeholder text'; continue; }
        if (form === 'action' && key === 'targetName' && !options.targets.includes(value)) { audit.rejected[key] = 'Invalid target NPC'; continue; }
        if (allowed[key].type === 'select' && !allowed[key].options.includes(value)) { audit.rejected[key] = 'Invalid option'; continue; }
        if (['factionA','factionB','factionId'].includes(key) && !value) { audit.rejected[key] = 'Invalid identity'; continue; }
        if (form === 'action' && key === 'npcName' && !options.npcs.includes(value)) { audit.rejected[key] = 'Invalid nearby NPC'; continue; }
        if (key === 'relationDelta' && (!Number.isInteger(value) || value < -15 || value > 15)) { audit.rejected[key] = 'Invalid relation change'; continue; }
        const other = {factionA:'factionB',factionB:'factionA'}[key]; if (other && value && value.trim().toLowerCase() === (next[other] || '').trim().toLowerCase()) { audit.rejected[key] = 'Identities must be distinct'; continue; }
        next[key] = value;
      }
      next[typeKey] = nextType;
      if (form === 'story') { setStoryNpc(next.npcName); setStoryType(next.storyType); setStoryNarration(next.narration ?? ''); setStoryFields(Object.fromEntries(Object.keys(allowed).filter(k => !['npcName','storyType','narration'].includes(k)).map(k => [k,next[k]]))); }
      if (form === 'social') { setSocialNpc1(next.npc1Name); setSocialNpc2(next.npc2Name); setSocialType(next.socialType); setSocialNarration(next.narration); setSocialFields(Object.fromEntries(Object.keys(allowed).filter(k => !['npc1Name','npc2Name','socialType','narration'].includes(k)).map(k => [k,next[k]]))); }
      setTouched(t=>({...t,[typeKey]:t[typeKey] || 'generated'}));
      if (form === 'political') { setPolFactionA(next.factionA); setPolFactionB(next.factionB); setPolEventType(next.eventType); setPolDescription(next.description); setPolDelta(next.relationDelta); }
      if (form === 'action') { const matches = nearby.filter(n => n.name === next.npcName); setActionNpc(nearby.find(n => n.id === actionNpc)?.name === next.npcName ? actionNpc : matches.length === 1 ? matches[0].id : ''); setSelectedAction(next.actionName); setActionParams(prev => (request.fields.actionName === 'CancelCurrentTask' || request.fields.actionName === 'ChangeSpeed') ? { ...prev, reason:next.reason || '' } : Object.fromEntries(Object.keys(allowed).filter(k => !['npcName','actionName'].includes(k)).map(k => [k,next[k] ?? '']))); }
      audit.ok = true; delete audit.rejected.$response; audit.applied = Object.keys(result.fields).filter(k=>next[k] !== old[k]); for (const k of Object.keys(result.fields)) if (!skipped.has(k) && !audit.applied.includes(k)) audit.rejected[k] ??= 'Preserved or unchanged';
      setGenerateError('');
      if (!Object.keys(result.rejected || {}).some(k => k === 'narration' || k === 'gossip' || k === 'rumor') &&!request.followUp && Object.entries(allowed).some(([k, f]) => f.required && !String(next[k] ?? '').trim())) {
        const fields = Object.fromEntries(Object.keys(allowed).map(k => [k, typeof next[k] === 'string' && !next[k].trim() ? '' : next[k] ?? '']));
        submitGenerate({ ...request, id:String(++requestId.current), fields, draft:{...fields}, followUp:true });
      }
      } finally { reportGenerate(request, audit); }
    };
    return () => { delete window.directorGenerateResult; };
  }, [pending, touched]);
  const generate = form => {
    const story = form === 'story', social = form === 'social', action = form === 'action';
    const enumTypes = story ? DIRECTOR_STORY_TYPES : social ? SOCIAL_TYPES : action ? executableActions.filter(a => !['CancelCurrentTask','ChangeSpeed'].includes(a.name)).map(a => ({key:a.name})) : POLITICAL_EVENT_TYPES;
    const typeKey = action ? 'actionName' : story ? 'storyType' : social ? 'socialType' : 'eventType';
    let actionNpcName = nearby.find(n => n.id === actionNpc)?.name || '';
    if (action && (actionQuery || (actionNpc && !actionNpcName))) { setGenerateError('Pick a nearby NPC or clear the Action NPC field.'); return; }
    let actionName = selectedAction;
    if (action && !actionNpcName) { const choices = nearby.filter(n => n.name.toLowerCase() !== String(actionParams.targetName || '').trim().toLowerCase()); if (choices.length) { const npc = choices[Math.floor(Math.random() * choices.length)]; actionNpcName = npc.name; setActionNpc(npc.id); } }
    if (action && !actionName && enumTypes.length) { actionName = enumTypes[Math.floor(Math.random() * enumTypes.length)].key; setSelectedAction(actionName); }
    const reasonOnly = action && ['CancelCurrentTask','ChangeSpeed'].includes(actionName);
    const defs = story ? currentFields : social ? currentSocialFields : action ? (actionFieldDefs[actionName] || []) : [];
    let params = actionParams;
    if (action && defs.some(f => f.key === 'targetName') && !String(params.targetName || '').trim()) {
      const choices = [...new Set(nearby.map(n => n.name))].filter(name => name.toLowerCase() !== actionNpcName.toLowerCase());
      if (choices.length) { params = { ...params, targetName:choices[Math.floor(Math.random() * choices.length)] }; setActionParams(params); }
    }
    let fields = story ? { npcName:storyNpc, storyType:touched.storyType ? storyType : '', narration:storyNarration, ...storyFields } :
      social ? { npc1Name:socialNpc1, npc2Name:socialNpc2, socialType:touched.socialType ? socialType : '', narration:socialNarration, ...socialFields } :
        action ? { npcName:actionNpcName, actionName, ...params } :
          { factionA:polFactionA, factionB:polFactionB, eventType:polEventType, description:polDescription, relationDelta:polDelta };
    if (reasonOnly) fields = { npcName:fields.npcName, actionName, reason:actionParams.reason || '' };
    if (story && storyType === 'informant') delete fields.narration; // IE writes the informant line itself
    if (action && !reasonOnly) fields.reason ??= ''; if (!reasonOnly) for (const f of defs) fields[f.key] ??= '';
    const npcPool = [...new Set(nearby.map(n => n.name))];
    const requiredNpcKeys = [...(story ? ['npcName'] : social ? ['npc1Name','npc2Name'] : []), ...defs.filter(f => f.required && ['npcName','npc1Name','npc2Name','subject'].includes(f.key)).map(f => f.key)];
    for (const key of requiredNpcKeys) if (!String(fields[key] || '').trim()) { const choices = npcPool.filter(name => !Object.values(fields).some(value => typeof value === 'string' && value.trim().toLowerCase() === name.toLowerCase())); if (choices.length) fields[key] = choices[Math.floor(Math.random() * choices.length)]; }
    if (story) { setStoryNpc(fields.npcName); if (fields.subject) setStoryFields(prev => ({...prev,subject:fields.subject})); }
    if (social) { setSocialNpc1(fields.npc1Name); setSocialNpc2(fields.npc2Name); }
    if (story || social) setTouched(t => ({...t,...Object.fromEntries(requiredNpcKeys.filter(k=>fields[k]).map(k=>[k,'generated']))}));
    for (const key of Object.keys(fields)) if (typeof fields[key] === 'string' && !fields[key].trim()) fields[key] = '';
    const draft = { ...fields };
    // Re-roll keeps who (filled identities and type, from any source) and rewrites generated text.
    for (const key of Object.keys(fields)) if (touched[key] !== true && !([...IDENTITY_KEYS, typeKey].includes(key) && fields[key])) fields[key] = '';
    const schema = Object.fromEntries(Object.entries(draft).filter(([k])=>!defs.some(f=>f.key===k)).map(([k,v]) => [k, { type: typeof v === 'number' ? 'number' : k === typeKey ? 'select' : 'text',
      required: (!reasonOnly || (action && k === 'reason')) && k !== 'relationDelta' && (k !== 'reason' || action),
      ...(k === typeKey ? { options:reasonOnly ? [actionName] : enumTypes.map(x=>x.key) } : {}) }]));
    if (story && storyGuidance && schema.narration) schema.narration.hint = `${storyGuidance.hint} ${storyGuidance.narration}`;
    const variants = Object.fromEntries(enumTypes.map(t => [t.key, Object.fromEntries((story ? (STORY_TYPE_FIELDS[t.key]?.fields||[]) : social ? (SOCIAL_TYPE_FIELDS[t.key]||[]) : action ? actionFieldDefs[t.key] : []).map(f=>[f.key,Object.fromEntries(Object.entries(f).filter(([key])=>key!=='placeholder').map(([key,value])=>[key,key === 'options' ? value.map(o => typeof o === 'string' ? o : o.value) : key === 'type' && value === 'faction' ? 'text' : value]))]))]));
    for (const definition of [...Object.values(schema), ...Object.values(variants).flatMap(fields => Object.values(fields))]) for (const key of ['hint','example']) if (definition[key]) definition[key] = stripExamples(definition[key]);
    const id = String(++requestId.current);
    const options = { npcs:action ? nearby.map(n => n.name) : [...new Set([...npcs.map(n=>n.name), ...knownNpcs, ...['npcName','npc1Name','npc2Name','subject','sender','victimName'].map(k=>draft[k]).filter(Boolean)])],
      ...(action ? { targets:[...new Set([...npcs.map(n=>n.name), ...knownNpcs])] } : {}), factions:factions.map(f=>({id:f.id,name:f.name})) };
    const request = { id, form, fields, draft, schema, variants, options };
    submitGenerate(request);
  };
  const clear = form => {
    const keys = form === 'story' ? ['storyType', ...Object.keys(STORY_TYPE_FIELDS).flatMap(t=>(STORY_TYPE_FIELDS[t]?.fields||[]).map(f=>f.key))] :
      form === 'social' ? ['socialType', ...Object.keys(SOCIAL_TYPE_FIELDS).flatMap(t=>(SOCIAL_TYPE_FIELDS[t]||[]).map(f=>f.key))] : ['eventType'];
    keys.push(...(form === 'story' ? ['npcName','narration'] : form === 'social' ? ['npc1Name','npc2Name','narration'] : ['factionA','factionB','description','relationDelta']));
    if (form === 'action') keys.splice(0, keys.length, 'npcName', 'actionName', ...Object.values(actionFieldDefs).flatMap(defs => defs.map(f => f.key)));
    if (pending?.form === form) stopGenerate();
    setTouched(t=>Object.fromEntries(Object.entries(t).filter(([key])=>!keys.includes(key))));
    setGenerateError('');
    if (form === 'story') { setStoryNpc(''); setStoryType('seek_player'); setStoryNarration(''); setStoryFields({}); }
    if (form === 'social') { setSocialNpc1(''); setSocialNpc2(''); setSocialType('npc_interaction'); setSocialNarration(''); setSocialFields({}); }
    if (form === 'political') { setPolFactionA(''); setPolFactionB(''); setPolEventType(''); setPolDescription(''); setPolDelta(8); }
    if (form === 'action') { setActionNpc(''); setActionQuery(null); setSelectedAction(''); setActionParams({}); }
  };

  // Action execution state
  const [actionNpc, setActionNpc] = useState('');
  const [actionQuery, setActionQuery] = useState(null);
  const [selectedAction, setSelectedAction] = useState('');
  const [actionParams, setActionParams] = useState({});
  const draftBindings = {
    story: [[storyNpc, setStoryNpc, ''], [storyType, setStoryType, 'seek_player'], [storyNarration, setStoryNarration, ''], [storyFields, setStoryFields, {}]],
    social: [[socialNpc1, setSocialNpc1, ''], [socialNpc2, setSocialNpc2, ''], [socialType, setSocialType, 'npc_interaction'], [socialNarration, setSocialNarration, ''], [socialFields, setSocialFields, {}]],
    political: [[polFactionA, setPolFactionA, ''], [polFactionB, setPolFactionB, ''], [polEventType, setPolEventType, ''], [polDescription, setPolDescription, ''], [polDelta, setPolDelta, 8]],
    action: [[actionNpc, setActionNpc, ''], [selectedAction, setSelectedAction, ''], [actionParams, setActionParams, {}]],
  };
  const draftSnapshot = JSON.stringify({ ...Object.fromEntries(Object.entries(draftBindings).filter(([, bindings]) => bindings.some(([value,, initial]) => JSON.stringify(value) !== JSON.stringify(initial))).map(([form, bindings]) => [form, bindings.map(([value]) => value)])), touched });
  const restoredDrafts = useRef(false);
  useEffect(() => {
    try {
      const saved = JSON.parse(localStorage.getItem('intelengine.director.drafts') || '{}');
      for (const [form, bindings] of Object.entries(draftBindings)) if (Array.isArray(saved[form]) && saved[form].length === bindings.length) bindings.forEach(([, set, initial], i) => {
        const value = saved[form][i];
        if (typeof initial === 'object' ? value && !Array.isArray(value) && typeof value === 'object' && Object.values(value).every(v => typeof v === 'string') : typeof value === typeof initial) set(value);
      });
      if (saved.touched && typeof saved.touched === 'object' && !Array.isArray(saved.touched)) setTouched(Object.fromEntries(Object.entries(saved.touched).filter(([, value]) => typeof value === 'boolean' || value === 'generated')));
    } catch {}
  }, []);
  useEffect(() => { if (!restoredDrafts.current) { restoredDrafts.current = true; return; } try { localStorage.setItem('intelengine.director.drafts', draftSnapshot); } catch {} }, [draftSnapshot]);

  const [busyConfirm, setBusyConfirm] = useState('');
  const send = sendAction;
  sendAction = (action, payload) => { send(action, payload); if (action === 'cancelTask') return; if (action === 'dispatchStory' || action === 'dispatchNpcSocial') { window.onRequestRefresh?.('{}'); return; } const sent = `sent:${action}:${Date.now()}`; setBusyConfirm(sent); setTimeout(() => setBusyConfirm(current => current === sent ? '' : current), 2000); };
  const npcs = loadedNpcs || [];
  const nearby = useMemo(() => npcs.map(n => ({ id: String(n.formId), name: n.name, kind: 'nearby' })), [loadedNpcs]);
  const npcOptions = useMemo(() => { const names = new Set(npcs.map(n => n.name)); return [...new Set([...names, ...knownNpcs])].map(name => ({ id: name, name, kind: names.has(name) ? 'nearby' : '' })); }, [loadedNpcs, knownNpcs]);
  const hirerOptions = useMemo(() => [...npcOptions, ...factions.map(f => ({ ...f, kind: 'faction' }))], [npcOptions, factions]);
  // Informant text needs the faction's display name, not its ID (the rumor must contain the Subject).
  const informantOptions = useMemo(() => [...npcOptions, ...factions.map(f => ({ ...f, id: f.name, kind: 'faction' }))], [npcOptions, factions]);
  const busySlots = names => tasks.filter(slot => slot.state && names.some(name => slot.agentName?.toLowerCase() === name.trim().toLowerCase()));
  const storyBusy = busySlots([storyNpc]);
  const socialBusy = busySlots([socialNpc1, socialNpc2]);
  const actionBusy = tasks.filter(slot => slot.state && String(slot.agentFormId >>> 0) === actionNpc);
  const confirmBusy = (kind, slots) => {
    if (!slots.length) { setBusyConfirm(''); return true; }
    if (busyConfirm !== kind) { setBusyConfirm(kind); return false; }
    slots.forEach(slot => sendAction('cancelTask', { slot: slot.index }));
    setBusyConfirm('');
    return true;
  };
  const confirmReplacement = (kind, engine, request) => {
    if (!engine?.isActive) { setReplacementConfirm(''); return '-1'; }
    const generation = Number.isInteger(engine.generation) ? String(engine.generation) : '-1';
    const identity = kind === 'story' ? [engine.activeNPC, engine.activeType] : [engine.traveler, engine.target, engine.type];
    const confirmation = JSON.stringify([kind, generation, identity, request]);
    if (replacementConfirm !== confirmation) { setReplacementConfirm(confirmation); return null; }
    setReplacementConfirm(''); return generation;
  };

  // Only show IntelEngine actions in the Director tab
  const INTEL_ACTIONS = new Set([
    'GoToLocation', 'FetchPerson', 'DeliverMessage', 'EscortTarget', 'SearchForActor',
    'ScheduleFetch', 'ScheduleDelivery', 'ScheduleMeeting', 'ReportPlayerConduct',
    'CancelCurrentTask', 'ChangeSpeed',
  ]);
  const executableActions = useMemo(
    () => (actions || []).filter(a => INTEL_ACTIONS.has(a.name)),
    [actions],
  );

  const currentAction = useMemo(
    () => executableActions.find(a => a.name === selectedAction),
    [executableActions, selectedAction],
  );

  // Use ACTION_FIELDS if defined, otherwise fall back to YAML params (minus hidden ones)
  const actionFieldDefs = useMemo(() => Object.fromEntries(executableActions.map(action => [action.name,
    ACTION_FIELDS[action.name] || (action.params || [])
        .filter(p => !HIDDEN_PARAMS.has(p.name))
        .map(p => ({
          key: p.name,
          label: p.name,
          type: p.name === 'factionId' ? 'faction' : Array.isArray(p.enum) && p.enum.length ? 'select' : 'text',
          required: action.name === 'ReportPlayerConduct' || p.required,
          options: p.enum || [],
          placeholder: p.description,
          hint: p.description,
        }))
  ])), [executableActions]);
  const currentActionFields = actionFieldDefs[selectedAction] || [];

  const storyGuidance = touched.storyType ? STORY_TYPE_FIELDS[storyType] : null, currentFields = STORY_TYPE_FIELDS[storyType]?.fields || [];
  const currentSocialFields = SOCIAL_TYPE_FIELDS[socialType] || [];
  const variantRequired = (defs, values) => defs.filter(f => f.required).map(f => [f.label, !!(values[f.key] || '').trim()]);
  // An unpicked type shows "(auto)", which only Generate honours, so Dispatch needs a picked (or generated) type.
  const storyTypeValid = !!touched.storyType && DIRECTOR_STORY_TYPES.some(type => type.key === storyType);
  const socialTypeValid = !!touched.socialType && SOCIAL_TYPES.some(type => type.key === socialType);
  const polEventTypeValid = !!touched.eventType && POLITICAL_EVENT_TYPES.some(type => type.key === polEventType);
  const storyRequired = [['NPC', !!storyNpc.trim()], ['Story type', storyTypeValid], ...(storyType === 'informant' ? [] : [['Narration', !!storyNarration.trim()]]), ...variantRequired(currentFields, storyFields)];
  const socialRequired = [['NPC 1', !!socialNpc1.trim()], ['NPC 2', !!socialNpc2.trim()], ['Social type', socialTypeValid], ['Narration', !!socialNarration.trim()], ...variantRequired(currentSocialFields, socialFields)];
  const politicalRequired = [['Faction A', !!polFactionA.trim()], ['Faction B', !!polFactionB.trim() && polFactionA.trim() !== polFactionB.trim()], ['Political event type', polEventTypeValid], ['Description', !!polDescription.trim()]];
  const missingFields = fields => fields.some(([, valid]) => !valid) && <p className="text-[10px] text-gray-400">Fill required fields: {fields.filter(([, valid]) => !valid).map(([label]) => label).join(', ')}</p>;
  const busyHeader = form => {
    const engine = form === 'story' ? story : form === 'social' ? social : null;
    if (!engine?.isActive) return null;
    const who = form === 'story' ? engine.activeNPC : `${engine.traveler} → ${engine.target}`;
    const type = (form === 'story' ? engine.activeType : engine.type) || '';
    return <span className="text-amber-400 italic normal-case"> — busy: {who} ({type.replace(/_/g, ' ').replace(/\b\w/g, c => c.toUpperCase())})</span>;
  };
  const generateControls = form => { return <div className="flex items-center justify-between mb-2"><h2 className="section-header">{{story:'Story Dispatch',social:'NPC Social Dispatch',political:'Political Dispatch',action:'Action Execution'}[form]}{busyHeader(form)}</h2><div className="flex gap-2">
    <button data-expand-section onClick={() => generate(form)} disabled={!!pending}
      className="rounded px-2 py-1 text-xs text-gray-300 bg-white/10 disabled:opacity-40">{pending?.form === form ? 'Generating…' : 'Generate'}</button>
    <button onClick={() => pending?.form === form ? stopGenerate() : clear(form)} className="rounded px-2 py-1 text-xs text-gray-400">{pending?.form === form ? 'Stop' : 'Clear'}</button>
  </div></div>; };

  const handleStoryTypeChange = useCallback((type) => {
    setStoryType(type);
    setStoryFields({});
  }, []);

  const handleDispatchStory = useCallback(() => {
    if (storyRequired.some(([, valid]) => !valid)) return;
    const request = {
      npcName: storyNpc.trim(), storyType, narration: storyNarration.trim(),
      ...storyFields, ...(['ambush','stalker'].includes(storyType) && storyFields.sender ? {sender:factions.find(f=>f.id===storyFields.sender || f.name===storyFields.sender)?.id || storyFields.sender} : {}),
    };
    const replaceGeneration = confirmReplacement('story', story, request);
    if (replaceGeneration === null) return;
    sendAction('dispatchStory', { ...request, replaceGeneration });
    setStoryNarration('');
    setStoryFields({});
  }, [story, storyNpc, storyType, storyRequired, storyNarration, storyFields, factions, sendAction, confirmReplacement]);


  const handleSocialTypeChange = useCallback((type) => {
    setSocialType(type);
    setSocialFields({});
  }, []);

  const handleDispatchSocial = useCallback(() => {
    if (socialRequired.some(([, valid]) => !valid)) return;
    const request = {
      npc1Name: socialNpc1.trim(), npc2Name: socialNpc2.trim(), socialType,
      narration: socialNarration.trim(), ...socialFields,
    };
    const replaceGeneration = confirmReplacement('social', social, request);
    if (replaceGeneration === null) return;
    sendAction('dispatchNpcSocial', { ...request, replaceGeneration });
    setSocialNarration('');
    setSocialFields({});
  }, [social, socialNpc1, socialNpc2, socialType, socialRequired, socialNarration, socialFields, sendAction, confirmReplacement]);

  const handlePolEventTypeChange = useCallback((type) => {
    setPolEventType(type);
    const evt = POLITICAL_EVENT_TYPES.find(e => e.key === type);
    if (evt) setPolDelta(evt.delta);
  }, []);

  const handleDispatchPolitics = useCallback(() => {
    if (!polEventTypeValid || !polFactionA.trim() || !polFactionB.trim() || !polDescription.trim()) return;
    sendAction('dispatchPolitics', {
      factionA: polFactionA.trim(),
      factionB: polFactionB.trim(),
      factionALabel: factions.find(f => f.id === polFactionA.trim())?.name || polFactionA.trim(), factionBLabel: factions.find(f => f.id === polFactionB.trim())?.name || polFactionB.trim(),
      eventType: polEventType,
      eventLabel: POLITICAL_EVENT_TYPES.find(e => e.key === polEventType)?.label || polEventType,
      description: polDescription.trim(),
      relationDelta: polDelta,
    });
    setPolDescription('');
  }, [polFactionA, polFactionB, polEventType, polEventTypeValid, polDescription, polDelta, sendAction, factions]);

  const handleActionChange = useCallback((name) => {
    setTouched(t => ({...t, actionName:!!name, ...Object.fromEntries(Object.keys(actionParams).map(k => [k,false]))}));
    setSelectedAction(name);
    const action = executableActions.find(a => a.name === name);
    setActionParams(Object.fromEntries((action?.params || [])
      .filter(p => !HIDDEN_PARAMS.has(p.name) && Array.isArray(p.enum) && p.enum.length)
      .map(p => [p.name, p.enum[0]])));
  }, [executableActions, actionParams]);
  const validActionFields = currentActionFields.filter(f => {
    const value = actionParams[f.key] || '';
    if (f.required && !value.trim()) return false;
    if (f.type === 'faction' && value && !factions.some(faction => faction.id === value)) return false;
    if (f.type === 'select' && value &&
      !f.options.some(option => (typeof option === 'string' ? option : option.value) === value)) return false;
    return true;
  });
  const actionNpcValid = npcs.some(npc => String(npc.formId) === actionNpc);
  const requiredFieldsValid = validActionFields.length === currentActionFields.length;
  const actionRequired = [['NPC', actionNpcValid], ['Action', !!selectedAction], ...currentActionFields.filter(f => !validActionFields.includes(f)).map(f => [f.label, false])];
  const handleExecuteAction = useCallback(() => {
    if (!actionNpcValid || !selectedAction || !requiredFieldsValid) return;
    // Cancel/ChangeSpeed act on the current task, so never pre-cancel it.
    if (!['CancelCurrentTask','ChangeSpeed'].includes(selectedAction) && !confirmBusy('action', actionBusy)) return;
    sendAction('executeAction', {
      npcFormId: Number(actionNpc),
      actionName: selectedAction,
      params: Object.fromEntries(Object.entries(actionParams).filter(([key, value]) => key !== 'reason' || value.trim())),
    });
    handleActionChange(selectedAction);
  }, [actionNpc, actionNpcValid, selectedAction, actionParams, sendAction, requiredFieldsValid, handleActionChange, confirmBusy, actionBusy]);

  const setParam = useCallback((key, value) => {
    setTouched(t => ({...t, [key]:true}));
    setActionParams(prev => ({ ...prev, [key]: value }));
  }, []);


  return (
    <div className="director-tab p-3 space-y-4">
      {/* Story Dispatch */}
      <section>
        {generateControls('story')}{pending?.form === 'story' && <small className="text-gray-400">Generating…</small>}{generateError && !pending && <small className="text-red-400">{generateError}</small>}
        <fieldset disabled={pending?.form === 'story'} className="space-y-2 border-0 p-0 m-0 min-w-0" inert={pending?.form === 'story' ? '' : undefined}>
          <div className="flex gap-2">
            <Field label="NPC" required hint="Exact display name.">
            <FactionInput options={npcOptions}
              type="text"
              value={storyNpc}
              onInput={() => setTouched(t=>({...t,npcName:true}))}
              onChange={e => { setStoryNpc(e.target.value); setBusyConfirm(''); }}
              placeholder="e.g. Saadia"
              className="flex-1 bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
            />
            </Field>
            <Field label="Story type" required hint={storyGuidance?.hint || 'What they will do.'}>
            <Select aria-label="Story type" containerClassName="w-[130px]"
              value={touched.storyType ? storyType : ''}
              onChange={e => { setTouched(t => ({...t,storyType:true})); handleStoryTypeChange(e.target.value); }}
              className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none"
            >
              <option value="" className="text-gray-500">(auto)</option>
              {DIRECTOR_STORY_TYPES.map(t => (
                <option key={t.key} value={t.key}>{t.label}</option>
              ))}
            </Select>
            </Field>
          </div>
          {storyType !== 'informant' && <Field label="Narration" required hint={storyGuidance?.narration || 'Why the NPC acts; used as story context.'}>
          <textarea
            value={storyNarration}
            onInput={() => setTouched(t=>({...t,narration:true}))}
            onChange={e => setStoryNarration(e.target.value)}
            placeholder={storyGuidance?.example || 'e.g. set out to buy supplies'}
            rows={2}
            className="w-full bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
          />
          </Field>}

          {/* Type-specific fields; `half` fields share a row */}
          <div className="grid grid-cols-2 gap-x-2">
          {currentFields.map(f => (
            <div key={f.key} className={f.half ? '' : 'col-span-2'}>
            <Field label={f.label} required={f.required}>
              {f.type === 'select' ? (
                <Select aria-label={f.label}
                  value={storyFields[f.key] || ''}
                  onChange={e => { setTouched(t=>({...t,[f.key]:true})); setStoryFields(prev => ({ ...prev, [f.key]: e.target.value })); }}
                  className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none"
                >
                  <option value="" className={!touched[f.key] ? 'text-gray-500' : ''}>{touched[f.key] ? '—' : '(auto)'}</option>
                  {f.options.map(o => (
                    <option key={o} value={o}>{o}</option>
                  ))}
                </Select>
              ) : ['subject','sender','victimName'].includes(f.key) ? <FactionInput options={storyType === 'informant' ? informantOptions : f.key === 'sender' && ['ambush','stalker'].includes(storyType) ? hirerOptions : npcOptions} aria-label={f.label} value={storyFields[f.key] || ''} onChange={e => { setTouched(t=>({...t,[f.key]:true})); setStoryFields(prev => ({...prev,[f.key]:e.target.value})); }} placeholder={f.placeholder} /> : (
                <input
                  type="text"
                  value={storyFields[f.key] || ''}
                  onInput={() => setTouched(t=>({...t,[f.key]:true}))}
                  onChange={e => setStoryFields(prev => ({ ...prev, [f.key]: e.target.value }))}
                  placeholder={f.placeholder}
                  className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
                />
              )}
              {f.hint && (
                <span className="director-hint">{f.hint}</span>
              )}
            </Field>
            </div>
          ))}
          </div>

          <button
            onClick={handleDispatchStory}
            disabled={storyRequired.some(([, valid]) => !valid)}
            className="w-full px-3 py-1.5 text-xs font-medium rounded transition-colors bg-blue-500/20 text-blue-400 hover:bg-blue-500/30 disabled:opacity-30 disabled:cursor-not-allowed"
          >
            {story?.isActive && replacementConfirm.startsWith('["story"') ? 'Busy — confirm replacement' : busyConfirm.startsWith('sent:dispatchStory:') ? 'Sent' : story?.isActive ? 'Replace Story' : 'Dispatch Story'}
          </button>
          {missingFields(storyRequired)}
        </fieldset>
      </section>

      {/* NPC Social Dispatch */}
      <section>
        {generateControls('social')}{pending?.form === 'social' && <small className="text-gray-400">Generating…</small>}{generateError && !pending && <small className="text-red-400">{generateError}</small>}
        <fieldset disabled={pending?.form === 'social'} className="space-y-2 border-0 p-0 m-0 min-w-0" inert={pending?.form === 'social' ? '' : undefined}>
          <div className="flex gap-2">
            <Field label="NPC 1" required hint="The traveler/teller.">
            <FactionInput options={npcOptions}
              type="text"
              value={socialNpc1}
              onInput={() => setTouched(t=>({...t,npc1Name:true}))}
              onChange={e => { setSocialNpc1(e.target.value); setBusyConfirm(''); }}
              placeholder="e.g. Mikael"
              className="flex-1 bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
            />
            </Field>
            <Field label="NPC 2" required hint="The counterpart/listener.">
            <FactionInput options={npcOptions}
              type="text"
              value={socialNpc2}
              onInput={() => setTouched(t=>({...t,npc2Name:true}))}
              onChange={e => { setSocialNpc2(e.target.value); setBusyConfirm(''); }}
              placeholder="e.g. Sven"
              className="flex-1 bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
            />
            </Field>
            <Field label="Social type" required hint="Interaction or gossip." className="w-[130px] shrink-0">
            <Select aria-label="Social type" containerClassName="w-[130px]"
              value={touched.socialType ? socialType : ''}
              onChange={e => { setTouched(t => ({...t,socialType:true})); handleSocialTypeChange(e.target.value); }}
              className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none"
            >
              <option value="" className="text-gray-500">(auto)</option>
              {SOCIAL_TYPES.map(t => (
                <option key={t.key} value={t.key}>{t.label}</option>
              ))}
            </Select>
            </Field>
          </div>
            <Field label="Narration" required hint="What happens between them.">
            <textarea
              value={socialNarration}
              onInput={() => setTouched(t=>({...t,narration:true}))}
              onChange={e => setSocialNarration(e.target.value)}
              placeholder="e.g. confronted him about the stolen shipment"
              rows={1}
              className="flex-1 bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
            />
            </Field>

          {currentSocialFields.map(f => (
            <Field key={f.key} label={f.label} required={f.required}>
              <input
                type="text"
                value={socialFields[f.key] || ''}
                onChange={e => { setTouched(t=>({...t,[f.key]:true})); setSocialFields(prev => ({ ...prev, [f.key]: e.target.value })); }}
                placeholder={f.placeholder}
                className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
              />
              {f.hint && (
                <span className="director-hint">{f.hint}</span>
              )}
            </Field>
          ))}

          <button
            onClick={handleDispatchSocial}
            disabled={socialRequired.some(([, valid]) => !valid)}
            className="w-full px-3 py-1.5 text-xs font-medium rounded transition-colors bg-purple-500/20 text-purple-400 hover:bg-purple-500/30 disabled:opacity-30 disabled:cursor-not-allowed"
          >
            {social?.isActive && replacementConfirm.startsWith('["social"') ? 'Busy — confirm replacement' : busyConfirm.startsWith('sent:dispatchNpcSocial:') ? 'Sent' : social?.isActive ? 'Replace Social' : 'Dispatch Social'}
          </button>
          {missingFields(socialRequired)}
        </fieldset>
      </section>

      {/* Political Dispatch */}
      <section>
        {generateControls('political')}{pending?.form === 'political' && <small className="text-gray-400">Generating…</small>}{generateError && !pending && <small className="text-red-400">{generateError}</small>}
        <fieldset disabled={pending?.form === 'political'} className="space-y-2 border-0 p-0 m-0 min-w-0" inert={pending?.form === 'political' ? '' : undefined}>
          <div className="flex gap-2">
            <Field label={polEventType === 'surrender' ? 'Surrendering faction' : 'Faction A'} required hint={polEventType === 'surrender' ? 'The side that gives up. Needs an active IE war with the victor.' : 'First side.'}>
            <FactionInput
              value={polFactionA}
              onChange={value => { setTouched(t=>({...t,factionA:true})); setPolFactionA(value); }}
              factions={factions}
              placeholder="e.g. StormcloakFaction"
            />
            </Field>
            <Field label={polEventType === 'surrender' ? 'Victor' : 'Faction B'} required hint={polEventType === 'surrender' ? 'The side that wins.' : 'Other side. Pick two different faction IDs.'}>
            <FactionInput
              value={polFactionB}
              onChange={value => { setTouched(t=>({...t,factionB:true})); setPolFactionB(value); }}
              factions={factions}
              placeholder="e.g. ThalmorFaction"
            />
            </Field>
          </div>
          <div className="flex gap-2">
            <Field label="Event type" required hint="What happened.">
            <Select aria-label="Political event type" containerClassName="flex-1"
              value={touched.eventType ? polEventType : ''}
              onChange={e => { setTouched(t => ({...t,eventType:true})); handlePolEventTypeChange(e.target.value); }}
              className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none"
            >
              <option value="" className="text-gray-500">(auto)</option>
              {POLITICAL_EVENT_TYPES.map(t => (
                <option key={t.key} value={t.key}>{t.label}</option>
              ))}
            </Select>
            </Field>
            <Field label="Relation delta" hint="Change, -15 to 15.">
              <input
                type="number"
                value={polDelta}
                onInput={() => setTouched(t=>({...t,relationDelta:true}))}
                onChange={e => setPolDelta(Number(e.target.value))}
                className="w-14 bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none text-center"
                min={-15}
                max={15}
              />
            </Field>
          </div>
          <Field label="Description" required hint="A short account of the political event.">
          <textarea
            value={polDescription}
            onInput={() => setTouched(t=>({...t,description:true}))}
            onChange={e => setPolDescription(e.target.value)}
            placeholder="e.g. Imperial spies intercepted dispatches near Windhelm"
            rows={2}
            className="w-full bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
          />
          </Field>
          <button
            onClick={handleDispatchPolitics}
            disabled={politicalRequired.some(([, valid]) => !valid)}
            className="w-full px-3 py-1.5 text-xs font-medium rounded transition-colors bg-amber-500/20 text-amber-400 hover:bg-amber-500/30 disabled:opacity-30 disabled:cursor-not-allowed"
          >
            {busyConfirm.startsWith('sent:dispatchPolitics:') ? 'Sent' : 'Dispatch Political Event'}
          </button>
          {missingFields(politicalRequired)}
        </fieldset>
      </section>

      {/* Action Execution */}
      <section>
        {generateControls('action')}{pending?.form === 'action' && <small className="text-gray-400">Generating…</small>}{generateError && !pending && <small className="text-red-400">{generateError}</small>}
        <fieldset disabled={pending?.form === 'action'} className="space-y-2 border-0 p-0 m-0 min-w-0" inert={pending?.form === 'action' ? '' : undefined}>
          <div className="flex gap-2">
            <Field label="NPC (nearby only)" required hint="Nearby actor who performs the action.">
            <FactionInput options={nearby} selectionOnly aria-label="Action NPC" placeholder="e.g. Guard 1"
              value={actionNpc} query={actionQuery} setQuery={setActionQuery}
              onChange={e => { setActionNpc(e.target.value); setBusyConfirm(''); }}
              className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none"
            />
            </Field>
            <Field label="Action" required hint="Command to run.">
            <Select aria-label="Action" containerClassName="w-[150px]"
              value={selectedAction}
              onChange={e => handleActionChange(e.target.value)}
              className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none"
            >
              <option value="">Select action...</option>
              {executableActions.map(a => (
                <option key={a.name} value={a.name}>{a.name}</option>
              ))}
            </Select>
            </Field>
          </div>

          {/* Dynamic param fields for selected action */}
          {currentActionFields.map(f => (
            <Field key={f.key} label={f.label} required={f.required} hint={f.hint}>
              {f.type === 'faction' ? (
                <FactionInput
                  value={actionParams[f.key] || ''}
                  onChange={value => setParam(f.key, value)}
                  factions={factions}
                  placeholder="Select faction..."
                />
              ) : f.type === 'select' ? (
                <Select aria-label={f.label}
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
                </Select>
              ) : f.key === 'reason' ? null : f.key === 'targetName' ? <FactionInput options={npcOptions} aria-label={f.label} value={actionParams[f.key] || ''} onChange={e => setParam(f.key, e.target.value)} placeholder={f.placeholder} /> : (
                <input
                  type="text"
                  value={actionParams[f.key] || ''}
                  onChange={e => setParam(f.key, e.target.value)}
                  placeholder={f.placeholder}
                  className="bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none placeholder:text-gray-600"
                />
              )}
            </Field>
          ))}

          <Field label="Reason" hint="Optional. Why the NPC does this; they remember it and can explain it."><textarea value={actionParams.reason || ''} onChange={e => setParam('reason', e.target.value)} rows={2} className="w-full bg-white/5 border border-white/10 rounded px-2 py-1.5 text-xs text-gray-200 outline-none" /></Field>
          <button
            onClick={handleExecuteAction}
            disabled={actionRequired.some(([, valid]) => !valid)}
            className="w-full px-3 py-1.5 text-xs font-medium rounded transition-colors bg-emerald-500/20 text-emerald-400 hover:bg-emerald-500/30 disabled:opacity-30 disabled:cursor-not-allowed"
          >
            {busyConfirm.startsWith('sent:executeAction:') ? 'Sent' : actionBusy.length && busyConfirm === 'action' ? 'Busy — you sure?' : 'Execute Action'}
          </button>
          {missingFields(actionRequired)}
        </fieldset>
      </section>
    </div>
  );
}

export default withSections(DirectorTab);
