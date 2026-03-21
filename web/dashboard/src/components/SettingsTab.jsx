import { useState, useCallback, useRef } from 'react';

const DANGER_POLICIES = ['Allow All', 'Block Civilians', 'Followers Only', 'Block All'];
const DANGER_HINTS = [
  'No filtering. All NPC types can visit you in dungeons and caves.',
  'Farmers, merchants and civilians stay away. Warriors and mages can still visit.',
  'Only NPCs who can be followers visit you in dangerous locations.',
  'Nobody visits you in dangerous locations.',
];
const HOME_POLICIES = ['Allow All', 'Block Civilians', 'Followers Only', 'Block All'];
const HOME_HINTS = [
  'No filtering. All NPC types can visit you at home.',
  'Civilians won\'t bother you at home. Warriors and mages can still visit.',
  'Only NPCs who can be followers visit you at home.',
  'Nobody visits you at home.',
];

const HOLD_POLICIES = ['Any Hold', 'Same Hold (Civ)', 'Same Hold (-Fol)', 'Same Hold (All)', 'Same Town (Civ)', 'Same Town (-Fol)', 'Same Town (All)'];
const HOLD_HINTS = [
  'No restriction. NPCs from any hold can be dispatched.',
  'Civilians must be in the same hold as you. Warriors and followers can cross holds.',
  'Everyone except followers must be in the same hold.',
  'All NPCs must be in the same hold as you.',
  'Civilians must be in the same town/city. Warriors can cross within the hold. Followers go anywhere.',
  'Everyone except followers must be in the same town/city.',
  'All NPCs must be in the same town/city as you.',
];
const HOLD_TYPES = [
  { key: 'holdPolicySeekPlayer', label: 'Seek Player' },
  { key: 'holdPolicyInformant', label: 'Informant' },
  { key: 'holdPolicyRoadEncounter', label: 'Road Encounter' },
  { key: 'holdPolicyAmbush', label: 'Ambush' },
  { key: 'holdPolicyStalker', label: 'Stalker' },
  { key: 'holdPolicyMessage', label: 'Message' },
  { key: 'holdPolicyQuest', label: 'Quest' },
];

const CONFIRM_MODES = ['Disabled', 'Followers Only', 'Everyone'];
const CONFIRM_HINTS = [
  'No confirmation prompt. Action executes immediately.',
  'Prompt only when active followers perform this action.',
  'Prompt for all NPCs performing this action.',
];
const CONFIRM_ACTIONS = [
  { key: 'confirmGoToLocation', label: 'Go To Location' },
  { key: 'confirmDeliverMessage', label: 'Deliver Message' },
  { key: 'confirmFetchPerson', label: 'Fetch Person' },
  { key: 'confirmEscortTarget', label: 'Escort Target' },
  { key: 'confirmSearchForActor', label: 'Search For Actor' },
  { key: 'confirmScheduleMeeting', label: 'Schedule Meeting' },
  { key: 'confirmScheduleFetch', label: 'Schedule Fetch' },
  { key: 'confirmScheduleDelivery', label: 'Schedule Delivery' },
];

const VK_NAMES = {
  '-1': 'Disabled',
  '48': '0', '49': '1', '50': '2', '51': '3', '52': '4',
  '53': '5', '54': '6', '55': '7', '56': '8', '57': '9',
  '112': 'F1', '113': 'F2', '114': 'F3', '115': 'F4',
  '116': 'F5', '117': 'F6', '118': 'F7', '119': 'F8', '120': 'F9',
  '121': 'F10', '122': 'F11', '123': 'F12',
};

const MOD_FLAGS = [
  { label: 'Ctrl', value: 1 },
  { label: 'Shift', value: 2 },
  { label: 'Alt', value: 4 },
];

function SettingsTab({ config, onSettingChange, pluginConfig, onPluginConfigChange }) {
  // Track which accordion sections are open (by key)
  const [openSections, setOpenSections] = useState({ storyEngine: true, tasks: true });

  const toggle = useCallback((key) => {
    setOpenSections(prev => ({ ...prev, [key]: !prev[key] }));
  }, []);

  if (!config) {
    return (
      <div className="p-3">
        <div className="empty-state">Settings not available. Open the dashboard in-game to see live data.</div>
      </div>
    );
  }

  return (
    <div className="p-3 space-y-1">
      {/* Story Engine */}
      <Accordion title="Story Engine" isOpen={openSections.storyEngine} onToggle={() => toggle('storyEngine')}>
        <ToggleRow
          label="Story Engine"
          value={config.storyEnabled}
          onChange={v => onSettingChange('storyEnabled', v)}
          hint="NPCs with unfinished business autonomously seek you out based on shared history."
        />
        <SliderRow
          label="Check Interval"
          value={config.storyInterval}
          min={0.5} max={168} step={0.5}
          unit="hrs"
          onCommit={v => onSettingChange('storyInterval', v)}
          hint="How often (game hours) the Story Engine checks for NPCs with reasons to find you. Max 168 = 1 week."
        />
        <SliderRow
          label="NPC Cooldown"
          value={config.storyCooldown}
          min={6} max={72} step={6}
          unit="hrs"
          onCommit={v => onSettingChange('storyCooldown', v)}
          hint="How long a picked NPC's priority stays reduced. Higher = more variety."
        />
        <SliderRow
          label="Long Absence"
          value={config.longAbsenceDays}
          min={1} max={14} step={1}
          unit="days"
          onCommit={v => onSettingChange('longAbsenceDays', v)}
          hint="Minimum game days since last interaction before an NPC becomes a Story Engine candidate."
        />
        <SliderRow
          label="Max Travel Time"
          value={config.maxTravelDays}
          min={0.25} max={3} step={0.25}
          unit="days"
          onCommit={v => onSettingChange('maxTravelDays', v)}
          hint="Max game days an NPC will travel before being teleported."
        />
        <ToggleRow
          label="Teleport When Stuck"
          value={config.allowStuckTeleport}
          onChange={v => onSettingChange('allowStuckTeleport', v)}
          hint="When disabled, stuck NPCs give up instead of being teleported."
        />
      </Accordion>

      {/* NPC Behavior */}
      <Accordion title="NPC Behavior" isOpen={openSections.npcBehavior} onToggle={() => toggle('npcBehavior')}>
        <span className="text-[10px] text-gray-500 uppercase tracking-wider block mb-1">Danger Zones</span>
        <PolicyRow
          label=""
          value={config.dangerZonePolicy}
          options={DANGER_POLICIES}
          hints={DANGER_HINTS}
          onChange={v => onSettingChange('dangerZonePolicy', v)}
        />
        <span className="text-[10px] text-gray-500 uppercase tracking-wider block mb-1 mt-2">Player Home</span>
        <PolicyRow
          label=""
          value={config.playerHomePolicy}
          options={HOME_POLICIES}
          hints={HOME_HINTS}
          onChange={v => onSettingChange('playerHomePolicy', v)}
        />
        <div className="border-t border-white/5 mt-2 pt-2">
          <ToggleRow
            label="NPC Interactions"
            value={config.npcTickEnabled}
            onChange={v => onSettingChange('npcTickEnabled', v)}
            hint="NPCs gossip, argue, trade, and socialize independently."
          />
          <SliderRow
            label="Interaction Interval"
            value={config.npcTickInterval}
            min={0.5} max={168} step={0.5}
            unit="hrs"
            onCommit={v => onSettingChange('npcTickInterval', v)}
            hint="How often the NPC social system checks for NPC-to-NPC interactions. Max 168 = 1 week."
          />
          <SliderRow
            label="Social Cooldown"
            value={config.npcSocialCooldown}
            min={6} max={72} step={6}
            unit="hrs"
            onCommit={v => onSettingChange('npcSocialCooldown', v)}
            hint="How long before an NPC can be picked for another social interaction."
          />
          <ToggleRow
            label="NPC-to-NPC Interactions"
            value={config.npc_interaction !== false}
            onChange={v => onSettingChange('npc_interaction', v)}
            hint="NPCs interact with each other autonomously (arguments, trades, conversations)."
          />
          <ToggleRow
            label="NPC Gossip"
            value={config.npc_gossip !== false}
            onChange={v => onSettingChange('npc_gossip', v)}
            hint="NPCs spread rumors and gossip among themselves."
          />
        </div>
      </Accordion>

      {/* Hold Restrictions */}
      <Accordion title="Hold Restrictions" isOpen={openSections.holdRestrictions} onToggle={() => toggle('holdRestrictions')}>
        <p className="text-[9px] text-gray-600 mb-2">
          Restrict NPCs from traveling across holds per story type. Default: same hold for civilians.
        </p>
        {HOLD_TYPES.map(type => (
          <div key={type.key} className="mb-1.5">
            <span className="text-[10px] text-gray-500 uppercase tracking-wider block mb-0.5">{type.label}</span>
            <PolicyRow
              label=""
              value={config[type.key] ?? 1}
              options={HOLD_POLICIES}
              hints={HOLD_HINTS}
              onChange={v => onSettingChange(type.key, v)}
            />
          </div>
        ))}
      </Accordion>

      {/* Tasks & Meetings */}
      <Accordion title="Tasks & Meetings" isOpen={openSections.tasks} onToggle={() => toggle('tasks')}>
        <SliderRow
          label="Max Concurrent"
          value={config.maxTasks}
          min={1} max={5} step={1}
          onCommit={v => onSettingChange('maxTasks', v)}
          hint="Maximum number of NPCs that can be on tasks at once."
        />
        <SliderRow
          label="Default Wait Hours"
          value={config.defaultWaitHours}
          min={6} max={168} step={6}
          unit="hrs"
          onCommit={v => onSettingChange('defaultWaitHours', v)}
          hint="How long NPCs wait at destinations before returning home."
        />
        <SliderRow
          label="Release Distance"
          value={config.releaseDistance}
          min={200} max={2000} step={100}
          unit="u"
          onCommit={v => onSettingChange('releaseDistance', v)}
          hint="How far you must walk before an NPC stops lingering."
        />
        <SliderRow
          label="Meeting Timeout"
          value={config.meetingTimeoutHours}
          min={1} max={12} step={0.5}
          unit="hrs"
          onCommit={v => onSettingChange('meetingTimeoutHours', v)}
          hint="How long an NPC waits at the meeting spot before giving up."
        />
        <SliderRow
          label="Meeting Grace Period"
          value={config.meetingGracePeriod}
          min={0} max={2} step={0.1}
          unit="hrs"
          onCommit={v => onSettingChange('meetingGracePeriod', v)}
          hint="Arrival tolerance for meetings. Set higher if using variable timescales."
        />
        <ToggleRow
          label="Report Back (Delivery)"
          value={config.reportBack}
          onChange={v => onSettingChange('reportBack', v)}
          hint="Messengers return to you after delivering a message off-screen."
        />
      </Accordion>

      {/* Action Confirmation Prompts */}
      <Accordion title="Action Confirmations" isOpen={openSections.confirmations} onToggle={() => toggle('confirmations')}>
        <p className="text-[10px] text-gray-500 mb-2">Per-action confirmation prompts (Disabled / Followers Only / Everyone)</p>
        {CONFIRM_ACTIONS.map(({ key, label }) => {
          const val = parseInt(config[key]) || 0;
          return (
            <div key={key} className="flex items-center justify-between py-1 group">
              <span className="text-xs text-gray-300">{label}</span>
              <button
                className="text-xs px-2 py-0.5 rounded bg-white/5 hover:bg-white/10 text-gray-300 min-w-[100px] text-center"
                title={CONFIRM_HINTS[val]}
                onClick={() => onSettingChange(key, (val + 1) % 3)}
              >
                {CONFIRM_MODES[val]}
              </button>
            </div>
          );
        })}
      </Accordion>

      {/* Quest Types */}
      <Accordion title="Quest Types" isOpen={openSections.quests} onToggle={() => toggle('quests')}>
        <ToggleRow label="Combat (Clear Enemies)" value={config.questCombat} onChange={v => onSettingChange('questCombat', v)}
          hint="NPC asks you to kill bandits, draugr, or dragons at a location." />
        <ToggleRow label="Rescue (Save Captive)" value={config.questRescue} onChange={v => onSettingChange('questRescue', v)}
          hint="A real NPC is teleported to the location and held captive by enemies." />
        <ToggleRow label="Find Item" value={config.questFindItem} onChange={v => onSettingChange('questFindItem', v)}
          hint="A valuable item spawns in a chest guarded by enemies." />
        <ToggleRow label="Faction Combat" value={config.questFactionCombat} onChange={v => onSettingChange('questFactionCombat', v)}
          hint="Clear out hostile faction soldiers at a location. Rewards faction standing." />
        <ToggleRow label="Faction Rescue" value={config.questFactionRescue} onChange={v => onSettingChange('questFactionRescue', v)}
          hint="Rescue a captive from hostile faction soldiers. Rewards faction standing." />
        <ToggleRow label="Faction Battle" value={config.questFactionBattle} onChange={v => onSettingChange('questFactionBattle', v)}
          hint="A friendly faction invites you to join an upcoming battle. Requires high standing (40+)." />
        <ToggleRow
          label="Allow NPC Death (Rescue)"
          value={config.questAllowVictimDeath}
          onChange={v => onSettingChange('questAllowVictimDeath', v)}
          hint="WARNING: Rescued NPCs can die during combat. Can break main quests!"
        />
        <SliderRow
          label="Quest Timeout"
          value={config.questTimeoutDays}
          min={1} max={30} step={1}
          unit="days"
          onCommit={v => onSettingChange('questTimeoutDays', v)}
          hint="Days before an unfinished quest auto-expires."
        />
      </Accordion>

      {/* Faction Politics */}
      <Accordion title="Faction Politics" isOpen={openSections.politics} onToggle={() => toggle('politics')}>
        {pluginConfig && onPluginConfigChange ? (
          <>
            <ToggleRow
              label="Politics Enabled"
              value={pluginConfig['politics.enabled'] !== false && pluginConfig['politics.enabled'] !== 'false'}
              onChange={v => onPluginConfigChange('politics.enabled', v)}
              hint="Factions autonomously trade, negotiate, scheme, and go to war."
            />
            <SliderRow
              label="Tick Interval"
              value={parseInt(pluginConfig['politics.tick_interval_hours']) || 6}
              min={1} max={168} step={1}
              unit="hrs"
              onCommit={v => onPluginConfigChange('politics.tick_interval_hours', v)}
              hint="How often the Political DM evaluates faction relations. Max 168 = 1 week."
            />
            <SliderRow
              label="Max Relation Change"
              value={parseInt(pluginConfig['politics.max_relation_change_per_tick']) || 15}
              min={5} max={30} step={1}
              onCommit={v => onPluginConfigChange('politics.max_relation_change_per_tick', v)}
              hint="Maximum relation score change per political tick."
            />
            <SliderRow
              label="Max Active Wars"
              value={parseInt(pluginConfig['politics.max_active_wars']) || 2}
              min={1} max={5} step={1}
              onCommit={v => onPluginConfigChange('politics.max_active_wars', v)}
              hint="Maximum number of simultaneous faction wars."
            />
          </>
        ) : (
          <span className="text-[10px] text-gray-600">Plugin config not available.</span>
        )}
      </Accordion>

      {/* Debug */}
      <Accordion title="Debug" isOpen={openSections.debug} onToggle={() => toggle('debug')}>
        <ToggleRow
          label="Debug Mode"
          value={config.debugMode}
          onChange={v => onSettingChange('debugMode', v)}
          hint="Enable debug notifications and logging."
        />
      </Accordion>

      {/* ── Plugin Configuration ── */}
      {pluginConfig && onPluginConfigChange && (
        <>
          <div className="border-t border-white/10 pt-2 mt-2">
            <span className="text-[10px] text-gray-600 uppercase tracking-wider">Plugin Configuration</span>
          </div>

          <Accordion title="Blocklists & Whitelists" isOpen={openSections.blocklists} onToggle={() => toggle('blocklists')}>
            <TextInputRow
              label="Faction Blocklist"
              hint="Comma-separated EditorIDs (e.g. PrisonerFaction,BanditFaction:1)"
              value={pluginConfig['story.faction_blocklist'] || ''}
              onCommit={v => onPluginConfigChange('story.faction_blocklist', v)}
            />
            <TextInputRow
              label="Location Blocklist"
              hint="Comma-separated location names"
              value={pluginConfig['story.location_blocklist'] || ''}
              onCommit={v => onPluginConfigChange('story.location_blocklist', v)}
            />
            <TextInputRow
              label="NPC Blocklist"
              hint="Comma-separated NPC display names"
              value={pluginConfig['story.npc_blocklist'] || ''}
              onCommit={v => onPluginConfigChange('story.npc_blocklist', v)}
            />
            <div className="mt-2 pt-2 border-t border-white/5">
              <p className="text-[10px] text-gray-500 mb-1">Whitelists (empty = all allowed)</p>
            </div>
            <TextInputRow
              label="Faction Whitelist"
              hint="Comma-separated faction EditorIDs. Only these factions' NPCs can be dispatched."
              value={pluginConfig['story.faction_whitelist'] || ''}
              onCommit={v => onPluginConfigChange('story.faction_whitelist', v)}
            />
            <TextInputRow
              label="Location Whitelist"
              hint="Comma-separated locations. NPCs only visit you at these locations."
              value={pluginConfig['story.location_whitelist'] || ''}
              onCommit={v => onPluginConfigChange('story.location_whitelist', v)}
            />
            <TextInputRow
              label="NPC Whitelist"
              hint="Comma-separated NPC names. Only these NPCs can be dispatched."
              value={pluginConfig['story.npc_whitelist'] || ''}
              onCommit={v => onPluginConfigChange('story.npc_whitelist', v)}
            />
          </Accordion>

          <Accordion title="LLM Overrides" isOpen={openSections.llm} onToggle={() => toggle('llm')}>
            <p className="text-[10px] text-gray-600 mb-2">Leave empty to use base SkyrimNet config</p>
            <TextInputRow
              label="API Endpoint"
              hint="e.g. http://localhost:5000/v1"
              value={pluginConfig['llm.endpoint'] || ''}
              onCommit={v => onPluginConfigChange('llm.endpoint', v)}
            />
            <TextInputRow
              label="API Key"
              value={pluginConfig['llm.api_key'] || ''}
              onCommit={v => onPluginConfigChange('llm.api_key', v)}
              secret
            />
            <TextInputRow
              label="Model"
              value={pluginConfig['llm.model_name'] || ''}
              onCommit={v => onPluginConfigChange('llm.model_name', v)}
            />
            <NumberInputRow
              label="Temperature"
              value={pluginConfig['llm.temperature'] || 0}
              min={0} max={2} step={0.1}
              onCommit={v => onPluginConfigChange('llm.temperature', v)}
              hint="0 = use base config"
            />
            <NumberInputRow
              label="Max Tokens"
              value={pluginConfig['llm.max_tokens'] || 0}
              min={0} max={4096} step={1}
              onCommit={v => onPluginConfigChange('llm.max_tokens', v)}
              hint="0 = use base config"
            />
            <NumberInputRow
              label="Timeout"
              value={pluginConfig['llm.timeout'] || 0}
              min={0} max={120} step={1}
              unit="sec"
              onCommit={v => onPluginConfigChange('llm.timeout', v)}
              hint="0 = use base config"
            />
          </Accordion>

          <Accordion title="Dashboard" isOpen={openSections.dashboard} onToggle={() => toggle('dashboard')}>
            <SliderRow
              label="UI Scale"
              value={pluginConfig['ui.scale'] || 1}
              min={0.8} max={2.0} step={0.1}
              onCommit={v => onPluginConfigChange('ui.scale', v)}
              hint="Scale for high-DPI / 4K monitors. 1.5-2.0 recommended for 4K."
            />
            <HotkeyRow
              value={pluginConfig['ui.dashboard_hotkey'] ?? 55}
              modifiers={pluginConfig['ui.dashboard_modifiers'] ?? 2}
              onChangeKey={v => onPluginConfigChange('ui.dashboard_hotkey', v)}
              onChangeMods={v => onPluginConfigChange('ui.dashboard_modifiers', v)}
            />
          </Accordion>
        </>
      )}
    </div>
  );
}

// ── Accordion ──

function Accordion({ title, isOpen, onToggle, children }) {
  return (
    <div className="border border-white/5 rounded overflow-hidden">
      <button
        onClick={onToggle}
        className="w-full flex items-center justify-between px-3 py-1.5 bg-white/[0.02] hover:bg-white/[0.04] transition-colors"
      >
        <span className="text-xs font-medium text-gray-300">{title}</span>
        <span className={`text-[10px] text-gray-500 transition-transform ${isOpen ? 'rotate-180' : ''}`}>▼</span>
      </button>
      {isOpen && (
        <div className="px-3 py-2 space-y-1 border-t border-white/5">
          {children}
        </div>
      )}
    </div>
  );
}

// ── Shared Components ──

function ToggleRow({ label, value, onChange, hint }) {
  return (
    <div className="py-1">
      <div className="flex items-center justify-between">
        <span className="text-xs text-gray-300">{label}</span>
        <button
          onClick={() => onChange(!value)}
          className={`w-8 h-4 rounded-full transition-colors relative ${
            value ? 'bg-blue-500' : 'bg-gray-600'
          }`}
        >
          <span className={`absolute top-0.5 w-3 h-3 rounded-full bg-white transition-transform ${
            value ? 'left-4' : 'left-0.5'
          }`} />
        </button>
      </div>
      {hint && <span className="text-[9px] text-gray-600 leading-tight block mt-0.5">{hint}</span>}
    </div>
  );
}

function SliderRow({ label, value, min, max, step, unit, onCommit, hint }) {
  const [localVal, setLocalVal] = useState(null);
  const dragging = useRef(false);

  const current = localVal !== null ? localVal : (value || min);
  const displayVal = Number.isInteger(step) ? current : current?.toFixed?.(1) || current;

  const handleInput = useCallback((e) => {
    dragging.current = true;
    setLocalVal(parseFloat(e.target.value));
  }, []);

  const handleCommit = useCallback(() => {
    if (dragging.current && localVal !== null) {
      dragging.current = false;
      onCommit(localVal);
      setLocalVal(null);
    }
  }, [localVal, onCommit]);

  return (
    <div className="py-1">
      <div className="flex items-center justify-between mb-1">
        <span className="text-xs text-gray-300">{label}</span>
        <span className="text-xs text-gray-400">{displayVal}{unit ? ` ${unit}` : ''}</span>
      </div>
      <input
        type="range"
        min={min} max={max} step={step}
        value={current}
        onInput={handleInput}
        onPointerUp={handleCommit}
        onKeyUp={handleCommit}
        className="w-full h-1 rounded-full appearance-none bg-gray-700 slider"
      />
      {hint && <span className="text-[9px] text-gray-600 leading-tight block mt-0.5">{hint}</span>}
    </div>
  );
}

function PolicyRow({ label, value, options, hints, onChange }) {
  return (
    <div className="py-1">
      {label && <span className="text-xs text-gray-300 block mb-1">{label}</span>}
      <div className="flex gap-1">
        {options.map((opt, i) => (
          <button
            key={i}
            onClick={() => onChange(i)}
            className={`flex-1 text-[10px] py-1 rounded transition-colors ${
              value === i
                ? 'bg-blue-500/30 text-blue-300 border border-blue-500/50'
                : 'bg-white/5 text-gray-500 border border-white/5 hover:text-gray-300'
            }`}
          >
            {opt}
          </button>
        ))}
      </div>
      {hints && hints[value] && <span className="text-[9px] text-gray-600 leading-tight block mt-0.5">{hints[value]}</span>}
    </div>
  );
}

// ── Plugin Config Components ──

function TextInputRow({ label, value, onCommit, hint, secret }) {
  const [localVal, setLocalVal] = useState(null);
  const editing = localVal !== null;
  const display = editing ? localVal : value;

  return (
    <div className="py-1">
      <span className="text-xs text-gray-300 block mb-1">{label}</span>
      <input
        type={secret ? 'password' : 'text'}
        value={display}
        onChange={e => setLocalVal(e.target.value)}
        onBlur={() => {
          if (localVal !== null && localVal !== value) onCommit(localVal);
          setLocalVal(null);
        }}
        onKeyDown={e => {
          if (e.key === 'Enter') e.target.blur();
          if (e.key === 'Escape') { setLocalVal(null); e.target.blur(); }
        }}
        placeholder={hint || ''}
        className="w-full px-2 py-1 text-xs bg-white/5 border border-white/10 rounded text-gray-300 placeholder-gray-600 focus:border-blue-500/50 focus:outline-none"
      />
    </div>
  );
}

function NumberInputRow({ label, value, min, max, step, unit, onCommit, hint }) {
  const [localVal, setLocalVal] = useState(null);
  const editing = localVal !== null;
  const display = editing ? localVal : value;

  return (
    <div className="py-1">
      <div className="flex items-center justify-between mb-1">
        <span className="text-xs text-gray-300">{label}</span>
        {hint && <span className="text-[10px] text-gray-600">{hint}</span>}
      </div>
      <div className="flex items-center gap-2">
        <input
          type="number"
          value={display}
          min={min} max={max} step={step}
          onChange={e => setLocalVal(parseFloat(e.target.value) || 0)}
          onBlur={() => {
            if (localVal !== null && localVal !== value) {
              const clamped = Math.max(min, Math.min(max, localVal));
              onCommit(Number.isInteger(step) ? Math.round(clamped) : clamped);
            }
            setLocalVal(null);
          }}
          onKeyDown={e => {
            if (e.key === 'Enter') e.target.blur();
            if (e.key === 'Escape') { setLocalVal(null); e.target.blur(); }
          }}
          className="w-20 px-2 py-1 text-xs bg-white/5 border border-white/10 rounded text-gray-300 focus:border-blue-500/50 focus:outline-none"
        />
        {unit && <span className="text-xs text-gray-500">{unit}</span>}
      </div>
    </div>
  );
}

function HotkeyRow({ value, modifiers, onChangeKey, onChangeMods }) {
  const keyLabel = VK_NAMES[String(value)] || `VK ${value}`;
  const modBits = modifiers || 0;

  return (
    <div className="py-1 space-y-2">
      <div className="flex items-center justify-between">
        <span className="text-xs text-gray-300">Key</span>
        <select
          value={value}
          onChange={e => onChangeKey(parseInt(e.target.value))}
          className="px-2 py-1 text-xs bg-white/5 border border-white/10 rounded text-gray-300 focus:border-blue-500/50 focus:outline-none"
        >
          <option value={-1}>Disabled</option>
          {[0,1,2,3,4,5,6,7,8,9].map(n => (
            <option key={`k${n}`} value={48 + n}>{n}</option>
          ))}
          {[1,2,3,4,5,6,7,8,9,10,11,12].map(n => (
            <option key={`f${n}`} value={111 + n}>F{n}</option>
          ))}
        </select>
      </div>
      <div className="flex items-center justify-between">
        <span className="text-xs text-gray-300">Modifiers</span>
        <div className="flex gap-1">
          {MOD_FLAGS.map(m => (
            <button
              key={m.label}
              onClick={() => onChangeMods(modBits ^ m.value)}
              className={`px-2 py-0.5 text-[10px] rounded transition-colors ${
                (modBits & m.value)
                  ? 'bg-blue-500/30 text-blue-300 border border-blue-500/50'
                  : 'bg-white/5 text-gray-500 border border-white/5'
              }`}
            >
              {m.label}
            </button>
          ))}
        </div>
      </div>
      <div className="text-[10px] text-gray-600">
        Current: {MOD_FLAGS.filter(m => modBits & m.value).map(m => m.label).join('+')}
        {MOD_FLAGS.some(m => modBits & m.value) ? '+' : ''}{keyLabel}
      </div>
    </div>
  );
}

export default SettingsTab;
