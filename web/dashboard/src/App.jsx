import { useState, useEffect, useCallback, useRef } from 'react';
import TasksTab from './components/TasksTab';
import StoryTab from './components/StoryTab';
import SettingsTab from './components/SettingsTab';
import DirectorTab from './components/DirectorTab';
import ActionsTab from './components/ActionsTab';
import PoliticsTab from './components/PoliticsTab';

const TABS = [
  { id: 'tasks', label: 'Tasks', hint: 'What people are doing for you' },
  { id: 'story', label: 'Story', hint: 'The dungeon master and its stories' },
  { id: 'politics', label: 'Politics', hint: 'Factions, wars and alliances' },
  { id: 'director', label: 'Director', hint: 'Stage an event yourself' },
  { id: 'actions', label: 'Actions', hint: 'What NPCs are allowed to do' },
  { id: 'settings', label: 'Settings', hint: 'Tune IntelEngine' },
];

// The panel is a centred workspace sized from the screen, so it reads the same on 1080p, 1440p, 4K, ultrawide
// and in a headset. The height sets the size on a wide screen; on a square or upright surface (a VR panel, a
// rotated monitor) the width does, so the layout never ends up magnified into a narrow strip.
// `ui.scale` stays the player's multiplier on top (1.3 is its neutral value).
function shellMetrics(uiScale) {
  const vw = window.innerWidth || 1920;
  const vh = window.innerHeight || 1080;
  const fit = Math.min(vh / 1080, vw / 1920);
  const zoom = Math.min(Math.max(fit, 0.6), 2.6) * 1.15 * ((uiScale || 1.3) / 1.3);
  const height = vh * 0.88;
  const width = Math.min(vw * 0.94, height * 1.72);
  return { zoom, width: Math.round(width / zoom), height: Math.round(height / zoom) };
}

const FOCUSABLE = 'button:not(:disabled), input:not(:disabled), select:not(:disabled), textarea:not(:disabled), summary, a[href], [tabindex]:not([tabindex="-1"])';

// A field that uses the arrow keys itself: leave them alone there.
function isTyping(el) {
  if (!el) return false;
  if (el.tagName === 'TEXTAREA' || el.tagName === 'SELECT' || el.isContentEditable) return true;
  return el.tagName === 'INPUT' && !['checkbox', 'radio', 'button', 'range'].includes(el.type);
}

// Arrow-key focus: the nearest control in that direction. Used by keyboards and by hosts that type the pad as keys;
// Magelight's own pad navigation (attached below) does the same job when it is available.
function moveFocus(root, dir) {
  const items = Array.from(root.querySelectorAll(FOCUSABLE))
    .filter(el => el.getClientRects().length > 0 && !el.closest('[hidden]'));
  const current = root.contains(document.activeElement) ? document.activeElement : null;
  if (!current || current === document.body) {
    (root.querySelector('[role=tab][aria-selected=true]') || items[0])?.focus();
    return;
  }
  const a = current.getBoundingClientRect();
  const ax = a.left + a.width / 2;
  const ay = a.top + a.height / 2;
  let best = null;
  let bestScore = Infinity;
  for (const el of items) {
    if (el === current) continue;
    const b = el.getBoundingClientRect();
    const dx = b.left + b.width / 2 - ax;
    const dy = b.top + b.height / 2 - ay;
    const along = dir === 'left' ? -dx : dir === 'right' ? dx : dir === 'up' ? -dy : dy;
    const across = dir === 'left' || dir === 'right' ? Math.abs(dy) : Math.abs(dx);
    if (along <= 1) continue;
    const score = along + across * 2.5;
    if (score < bestScore) { bestScore = score; best = el; }
  }
  if (best) {
    best.focus();
    best.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  }
}

function App() {
  const [activeTab, setActiveTab] = useState(() => { try { const saved = localStorage.getItem('intelengine.activeTab'); return TABS.some(tab => tab.id === saved) ? saved : 'tasks'; } catch { return 'tasks'; } });
  useEffect(() => { try { localStorage.setItem('intelengine.activeTab', activeTab); } catch {} }, [activeTab]);
  const [state, setState] = useState({
    tasks: [],
    scheduled: [],
    story: null,
    quest: null,
    social: null,
    config: null,
  });
  const [removingPkgIds, setRemovingPkgIds] = useState(new Set());
  const shellRef = useRef(null);
  const [padInUse, setPadInUse] = useState(false);

  const handleFullState = useCallback((jsonStr) => {
    try {
      const data = typeof jsonStr === 'string' ? JSON.parse(jsonStr) : jsonStr;

      // Auto-clear removingPkgIds for packages the server actually removed
      const serverPkgIds = new Set((data.packages || []).map(p => p.formId));
      setRemovingPkgIds(prev => {
        const next = new Set();
        for (const id of prev) {
          if (serverPkgIds.has(id)) next.add(id);
        }
        return next.size !== prev.size ? next : prev;
      });

      // Ensure all known config keys have defaults so they always render in the UI,
      // even if SkyrimNet's state push doesn't include them from settings.yaml
      const pluginDefaults = {
        'story.faction_blocklist': '',
        'story.location_blocklist': '',
        'story.npc_blocklist': '',
        'story.faction_whitelist': '',
        'story.location_whitelist': '',
        'story.npc_whitelist': '',
        'politics.enabled': true,
        'politics.tick_interval_hours': 6,
        'politics.max_relation_change_per_tick': 15,
        'politics.max_active_wars': 2,
        'ui.scale': 1.3,
        'llm.endpoint': '',
        'llm.api_key': '',
        'llm.model_name': '',
        'llm.temperature': 0,
        'llm.max_tokens': 0,
        'llm.timeout': 0,
      };

      setState(prev => ({
        ...data,
        knownNpcs: data.knownNpcs ?? prev.knownNpcs,
        // Merge: defaults → server values → locally edited values
        pluginConfig: {
          ...pluginDefaults,
          ...(data.pluginConfig || {}),
          ...(prev.pluginConfig || {}),
        },
      }));
    } catch (e) {
      console.error('[Dashboard] Failed to parse full state:', e);
    }
  }, []);

  // Legacy handler for slot-only updates
  const handleUpdateSlots = useCallback((jsonStr) => {
    try {
      const data = typeof jsonStr === 'string' ? JSON.parse(jsonStr) : jsonStr;
      setState(prev => ({ ...prev, tasks: data }));
    } catch (e) {
      console.error('[Dashboard] Failed to parse slot data:', e);
    }
  }, []);

  useEffect(() => {
    window.updateFullState = (json) => handleFullState(json);
    window.updateSlots = (json) => handleUpdateSlots(json);

    const pending = window.__pendingInteropCalls || [];
    for (const call of pending) {
      if (call.name === 'updateFullState') handleFullState(call.json);
      else if (call.name === 'updateSlots') handleUpdateSlots(call.json);
    }
    window.__pendingInteropCalls = [];

    return () => {
      window.updateFullState = () => {};
      window.updateSlots = () => {};
    };
  }, [handleFullState, handleUpdateSlots]);

  const sendAction = useCallback((action, payload = {}) => {
    const fn = window[`onDashboard_${action}`];
    if (fn) fn(JSON.stringify(payload));
  }, []);
  useEffect(() => {
    const editable = el => !!el && (el.matches('input:not([type=checkbox]):not([type=radio]):not([type=range]):not([type=button]):not([type=submit]):not([type=reset]):not([type=file]):not([type=color]), textarea') || el.isContentEditable);
    const focus = event => sendAction('textFocus', { focused: editable(event.type === 'focusout' ? event.relatedTarget : event.target) });
    document.addEventListener('focusin', focus); document.addEventListener('focusout', focus);
    return () => { document.removeEventListener('focusin', focus); document.removeEventListener('focusout', focus); }; }, [sendAction]);

  const handleSettingChange = useCallback((key, value) => {
    // Optimistic local update — no round-trip needed
    setState(prev => ({
      ...prev,
      config: prev.config ? { ...prev.config, [key]: value } : prev.config,
    }));
    // Send to game
    sendAction('changeSetting', { key, value });
  }, [sendAction]);

  const handleClose = useCallback(() => {
    if (window.onCloseDashboard) window.onCloseDashboard('{}');
  }, []);

  const handleRefresh = useCallback(() => {
    if (window.onRequestRefresh) window.onRequestRefresh('{}');
  }, []);

  const activeCount = (state.tasks || []).filter(s => s.state !== 0).length;
  const scheduledCount = (state.scheduled || []).filter(s => s.agent).length;
  const uiScale = state.pluginConfig?.['ui.scale'] || 1.3;

  const [metrics, setMetrics] = useState(() => shellMetrics(uiScale));
  useEffect(() => {
    const update = () => setMetrics(shellMetrics(uiScale));
    update();
    window.addEventListener('resize', update);
    return () => window.removeEventListener('resize', update);
  }, [uiScale]);

  const stepTab = useCallback((delta) => {
    setActiveTab(current => {
      const at = TABS.findIndex(tab => tab.id === current);
      const next = TABS[Math.min(Math.max(at + delta, 0), TABS.length - 1)].id;
      requestAnimationFrame(() => shellRef.current?.querySelector(`[data-tab="${next}"]`)?.focus());
      return next;
    });
  }, []);

  // Controller: Magelight moves the selection between the panel's controls itself (A selects, B goes back,
  // LB / RB turn the pages through role=tab, the right stick scrolls). Hosts without that layer get arrow keys below.
  useEffect(() => {
    const pad = window.magelight?.gamepad;
    const root = shellRef.current;
    if (!pad?.attachNavigation || !root) return undefined;
    let detach = null;
    let unwatch = null;
    try {
      detach = pad.attachNavigation({
        root,
        initialFocus: () => root.querySelector('[role=tab][aria-selected=true]'),
        wrap: false,
      });
      if (pad.getState && pad.onStateChange) {
        const seen = s => setPadInUse(!!(s && s.connected && s.capturing));
        seen(pad.getState());
        unwatch = pad.onStateChange(seen);
      }
    } catch (e) {
      console.error('[Dashboard] gamepad navigation unavailable:', e);
    }
    return () => {
      try { if (unwatch) unwatch(); } catch {}
      try { if (detach) detach(); } catch {}
    };
  }, []);

  useEffect(() => {
    const onKey = (event) => {
      if (event.defaultPrevented || event.ctrlKey || event.altKey || event.metaKey) return;
      const root = shellRef.current;
      if (!root) return;
      const focused = document.activeElement;
      const typing = isTyping(focused);
      const dir = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right' }[event.key];
      if (dir) {
        if (typing) return;
        if (focused?.type === 'range' && (dir === 'left' || dir === 'right')) return;   // a slider keeps left / right
        // A dropdown uses up / down itself (to open, then to walk its list) and everything while it is open.
        // Moving the focus away would close it, so go by where the key was pressed as well as by the focus.
        const origin = event.target instanceof Element ? event.target : focused;
        const combo = origin?.closest?.('[role=combobox]') || focused?.closest?.('[role=combobox]');
        if (combo && (combo.getAttribute('aria-expanded') === 'true' || dir === 'up' || dir === 'down')) return;
        if (origin?.closest?.('[role=listbox]')) return;
        event.preventDefault();
        moveFocus(root, dir);
      } else if (!typing && (event.key === 'PageDown' || event.key === 'PageUp')) {
        event.preventDefault();
        stepTab(event.key === 'PageDown' ? 1 : -1);
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [stepTab]);

  const active = TABS.find(tab => tab.id === activeTab) || TABS[0];
  const badges = { tasks: activeCount + scheduledCount };

  return (
    <div className="ie-backdrop">
      <div
        ref={shellRef}
        className="ie-shell"
        style={{ zoom: metrics.zoom, width: metrics.width, height: metrics.height }}
      >
        {/* Pages */}
        <nav className="ie-nav">
          <div className="ie-brand">
            <h1>IntelEngine</h1>
            <span>{activeCount} active{scheduledCount > 0 ? ` · ${scheduledCount} scheduled` : ''}</span>
          </div>
          <div className="ie-nav-list" role="tablist" aria-orientation="vertical">
            {TABS.map(tab => (
              <button
                key={tab.id}
                role="tab"
                data-tab={tab.id}
                aria-selected={activeTab === tab.id ? 'true' : 'false'}
                onClick={() => setActiveTab(tab.id)}
                className="ie-nav-item"
                data-hint={tab.hint}
                data-badge={badges[tab.id] > 0 ? badges[tab.id] : undefined}
                aria-description={tab.hint}
              >
                {/* The button's text is the page name alone (tests and screen readers go by it); the hint
                    and the count are drawn from the attributes above by the stylesheet. */}
                <span className="ie-nav-label">{tab.label}</span>
              </button>
            ))}
          </div>
          <div className="ie-nav-foot">
            {padInUse
              ? <>LB / RB pages · A select · B back</>
              : <>Arrow keys move · Page Up / Down pages · Esc closes</>}
          </div>
        </nav>

        <div className="ie-main">
          <div className="ie-header">
            <div className="min-w-0">
              <h2 className="ie-title">{active.label}</h2>
              <div className="ie-subtitle">{active.hint}</div>
            </div>
            <div className="flex items-center gap-2">
              <button onClick={handleRefresh} className="ie-btn" title="Refresh">&#x21BB; Refresh</button>
              <button onClick={handleClose} className="ie-btn" title="Close">&#x2715; Close</button>
            </div>
          </div>

          {/* Content */}
          <div className="ie-content flex-1 overflow-y-auto" tabIndex={-1}>
            {activeTab === 'tasks' && (
              <TasksTab
                tasks={state.tasks || []}
                scheduled={state.scheduled || []}
                calendar={state.calendar}
                sendAction={sendAction}
                packages={state.packages || []}
                removingIds={removingPkgIds}
                onMarkRemoving={(formId) => setRemovingPkgIds(prev => new Set([...prev, formId]))}
              />
            )}
            {activeTab === 'story' && (
              <StoryTab
                story={state.story}
                quest={state.quest}
                social={state.social}
                npcSocialLog={state.npcSocialLog}
                calendar={state.calendar}
                sendAction={sendAction}
              />
            )}
            {activeTab === 'politics' && (
              <PoliticsTab
                politics={state.politics}
                calendar={state.calendar}
                sendAction={sendAction}
              />
            )}
            <div hidden={activeTab !== 'director'}>
              <DirectorTab
                loadedNpcs={state.loadedNpcs || []}
                knownNpcs={state.knownNpcs || []}
                tasks={state.tasks || []}
                actions={state.actions || []}
                factions={state.politics?.factions || []}
                story={state.story}
                social={state.social}
                sendAction={sendAction}
              />
            </div>
            {activeTab === 'actions' && (
              <ActionsTab
                actions={state.actions || []}
                sendAction={sendAction}
              />
            )}
            {activeTab === 'settings' && (
              <SettingsTab
                config={state.config}
                onSettingChange={handleSettingChange}
                pluginConfig={state.pluginConfig}
                onPluginConfigChange={(path, value) => {
                  setState(prev => ({
                    ...prev,
                    pluginConfig: { ...(prev.pluginConfig || {}), [path]: value },
                  }));
                  sendAction('changePluginConfig', { path, value });
                }}
              />
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

export default App;
