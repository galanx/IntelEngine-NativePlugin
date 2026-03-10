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
  if (!config) {
    return (
      <div className="p-3">
        <div className="empty-state">Settings not available. Open the dashboard in-game to see live data.</div>
      </div>
    );
  }

  return (
    <div className="p-3 space-y-4">
      {/* Story Engine */}
      <section>
        <h2 className="section-header mb-2">Story Engine</h2>
        <div className="space-y-2">
          <ToggleRow
            label="Story Engine"
            value={config.storyEnabled}
            onChange={v => onSettingChange('storyEnabled', v)}
            hint="NPCs with unfinished business autonomously seek you out based on shared history."
          />
          <SliderRow
            label="Check Interval"
            value={config.storyInterval}
            min={0.5} max={12} step={0.5}
            unit="hrs"
            onCommit={v => onSettingChange('storyInterval', v)}
            hint="How often (game hours) the Story Engine checks for NPCs with reasons to find you."
          />
          <SliderRow
            label="NPC Cooldown"
            value={config.storyCooldown}
            min={6} max={72} step={6}
            unit="hrs"
            onCommit={v => onSettingChange('storyCooldown', v)}
            hint="How long a picked NPC's priority stays reduced. Higher = more variety, lower = favorites return sooner."
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
            hint="Max game days an NPC will travel before being teleported. Lower = faster, higher = more realistic."
          />
          <ToggleRow
            label="Teleport When Stuck"
            value={config.allowStuckTeleport}
            onChange={v => onSettingChange('allowStuckTeleport', v)}
            hint="When enabled, stuck NPCs are teleported to the target. When disabled, the NPC gives up instead."
          />
        </div>
      </section>

      {/* Policies */}
      <section>
        <h2 className="section-header mb-2">NPC Policies</h2>
        <div className="space-y-2">
          <PolicyRow
            label="Danger Zones"
            value={config.dangerZonePolicy}
            options={DANGER_POLICIES}
            hints={DANGER_HINTS}
            onChange={v => onSettingChange('dangerZonePolicy', v)}
          />
          <PolicyRow
            label="Player Home"
            value={config.playerHomePolicy}
            options={HOME_POLICIES}
            hints={HOME_HINTS}
            onChange={v => onSettingChange('playerHomePolicy', v)}
          />
        </div>
      </section>

      {/* NPC Social */}
      <section>
        <h2 className="section-header mb-2">NPC Social</h2>
        <div className="space-y-2">
          <ToggleRow
            label="NPC Interactions"
            value={config.npcTickEnabled}
            onChange={v => onSettingChange('npcTickEnabled', v)}
            hint="NPCs gossip, argue, trade, and socialize independently."
          />
          <SliderRow
            label="Interaction Interval"
            value={config.npcTickInterval}
            min={0.5} max={6} step={0.5}
            unit="hrs"
            onCommit={v => onSettingChange('npcTickInterval', v)}
            hint="How often (game hours) the NPC social system checks for NPC-to-NPC interactions."
          />
          <SliderRow
            label="Social Cooldown"
            value={config.npcSocialCooldown}
            min={6} max={72} step={6}
            unit="hrs"
            onCommit={v => onSettingChange('npcSocialCooldown', v)}
            hint="How long before an NPC can be picked for another social interaction. Separate from story cooldown."
          />
        </div>
      </section>

      {/* Task Settings */}
      <section>
        <h2 className="section-header mb-2">Task Settings</h2>
        <div className="space-y-2">
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
            hint="How long NPCs wait at travel destinations before returning home. Does not affect scheduled meetings."
          />
          <SliderRow
            label="Release Distance"
            value={config.releaseDistance}
            min={200} max={2000} step={100}
            unit="u"
            onCommit={v => onSettingChange('releaseDistance', v)}
            hint="How far you must walk before an NPC stops lingering and returns to normal. Affects all systems."
          />
          <SliderRow
            label="Meeting Timeout"
            value={config.meetingTimeoutHours}
            min={1} max={12} step={0.5}
            unit="hrs"
            onCommit={v => onSettingChange('meetingTimeoutHours', v)}
            hint="How long an NPC waits at the meeting spot after the scheduled time before giving up."
          />
          <SliderRow
            label="Meeting Grace Period"
            value={config.meetingGracePeriod}
            min={0} max={2} step={0.1}
            unit="hrs"
            onCommit={v => onSettingChange('meetingGracePeriod', v)}
            hint="Arrival tolerance for meetings. Handles Dynamic Time Scaling mods. Set higher if using variable timescales."
          />
          <ToggleRow
            label="Report Back (Delivery)"
            value={config.reportBack}
            onChange={v => onSettingChange('reportBack', v)}
            hint="When enabled, messengers return to you after delivering a message off-screen and report back."
          />
          <ToggleRow
            label="Task Confirmation Prompt"
            value={config.taskConfirmPrompt}
            onChange={v => onSettingChange('taskConfirmPrompt', v)}
            hint="When enabled, a prompt appears before an NPC starts a task. You can Allow, Deny, or Deny Silently."
          />
        </div>
      </section>

      {/* Quest Sub-Types */}
      <section>
        <h2 className="section-header mb-2">Quest Types</h2>
        <div className="space-y-2">
          <ToggleRow label="Combat (Clear Enemies)" value={config.questCombat} onChange={v => onSettingChange('questCombat', v)}
            hint="NPC asks you to kill bandits, draugr, or dragons at a location." />
          <ToggleRow label="Rescue (Save Captive)" value={config.questRescue} onChange={v => onSettingChange('questRescue', v)}
            hint="A real NPC is teleported to the location and held captive by enemies." />
          <ToggleRow label="Find Item" value={config.questFindItem} onChange={v => onSettingChange('questFindItem', v)}
            hint="A valuable item spawns in a chest guarded by enemies." />
          <ToggleRow
            label="Allow NPC Death (Rescue)"
            value={config.questAllowVictimDeath}
            onChange={v => onSettingChange('questAllowVictimDeath', v)}
            hint="WARNING: Rescued NPCs can die during combat. This can break main quests if essential NPCs are killed!"
          />
          <SliderRow
            label="Quest Timeout"
            value={config.questTimeoutDays}
            min={1} max={30} step={1}
            unit="days"
            onCommit={v => onSettingChange('questTimeoutDays', v)}
            hint="Days before an unfinished quest auto-expires. The quest giver remembers you never showed up."
          />
        </div>
      </section>

      {/* Debug */}
      <section>
        <h2 className="section-header mb-2">Debug</h2>
        <div className="space-y-2">
          <ToggleRow
            label="Debug Mode"
            value={config.debugMode}
            onChange={v => onSettingChange('debugMode', v)}
            hint="Enable debug notifications and logging."
          />
        </div>
      </section>

      {/* ── Plugin Configuration (from settings.yaml) ── */}
      {pluginConfig && onPluginConfigChange && (
        <>
          <div className="border-t border-white/10 pt-3 mt-3">
            <span className="text-[10px] text-gray-600 uppercase tracking-wider">Plugin Configuration</span>
          </div>

          {/* Blocklists */}
          <section>
            <h2 className="section-header mb-2">Blocklists</h2>
            <div className="space-y-2">
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
            </div>
          </section>

          {/* LLM Overrides */}
          <section>
            <h2 className="section-header mb-2">LLM Overrides</h2>
            <p className="text-[10px] text-gray-600 mb-2">Leave empty to use base SkyrimNet config</p>
            <div className="space-y-2">
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
            </div>
          </section>

          {/* Dashboard UI */}
          <section>
            <h2 className="section-header mb-2">Dashboard</h2>
            <div className="space-y-2">
              <SliderRow
                label="UI Scale"
                value={pluginConfig['ui.scale'] || 1}
                min={0.8} max={2.0} step={0.1}
                onCommit={v => onPluginConfigChange('ui.scale', v)}
                hint="Scale the dashboard for high-DPI / 4K monitors. 1.0 = default, 1.5-2.0 recommended for 4K."
              />
              <HotkeyRow
                value={pluginConfig['ui.dashboard_hotkey'] ?? 118}
                modifiers={pluginConfig['ui.dashboard_modifiers'] ?? 2}
                onChangeKey={v => onPluginConfigChange('ui.dashboard_hotkey', v)}
                onChangeMods={v => onPluginConfigChange('ui.dashboard_modifiers', v)}
              />
            </div>
          </section>
        </>
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
      <span className="text-xs text-gray-300 block mb-1">{label}</span>
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
