import { useState, useEffect, useCallback } from 'react';
import TasksTab from './components/TasksTab';
import StoryTab from './components/StoryTab';
import PackagesTab from './components/PackagesTab';
import SettingsTab from './components/SettingsTab';
import DirectorTab from './components/DirectorTab';
import ActionsTab from './components/ActionsTab';
import PoliticsTab from './components/PoliticsTab';

const TABS = [
  { id: 'tasks', label: 'Tasks' },
  { id: 'story', label: 'Story' },
  { id: 'politics', label: 'Politics' },
  { id: 'director', label: 'Director' },
  { id: 'actions', label: 'Actions' },
  { id: 'packages', label: 'Packages' },
  { id: 'settings', label: 'Settings' },
];

function App() {
  const [activeTab, setActiveTab] = useState('tasks');
  const [state, setState] = useState({
    tasks: [],
    scheduled: [],
    story: null,
    quest: null,
    social: null,
    config: null,
  });
  const [removingPkgIds, setRemovingPkgIds] = useState(new Set());

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

      setState(prev => ({
        ...data,
        // Preserve local pluginConfig after initial load — prevents stale
        // server pushes from overwriting values the user just edited
        pluginConfig: prev.pluginConfig || data.pluginConfig,
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

  return (
    <div className="fixed top-4 right-4 w-[540px] max-h-[calc(100vh-2rem)] flex flex-col origin-top-right" style={{ zoom: uiScale }}>
      <div className="dashboard-panel rounded-lg overflow-hidden flex flex-col max-h-[calc(100vh-2rem)]">
        {/* Header */}
        <div className="flex items-center justify-between px-4 py-2.5 border-b border-white/10">
          <div className="flex items-center gap-3">
            <h1 className="text-sm font-semibold text-gray-200 uppercase tracking-wider">
              IntelEngine
            </h1>
            <span className="text-xs text-gray-500">
              {activeCount} active{scheduledCount > 0 ? ` / ${scheduledCount} scheduled` : ''}
            </span>
          </div>
          <div className="flex items-center gap-1">
            <button onClick={handleRefresh} className="btn-icon" title="Refresh">&#x21BB;</button>
            <button onClick={handleClose} className="btn-icon" title="Close (Shift+7)">&#x2715;</button>
          </div>
        </div>
        {/* Tabs */}
        <div className="flex border-b border-white/10">
          {TABS.map(tab => (
            <button
              key={tab.id}
              onClick={() => setActiveTab(tab.id)}
              className={`flex-1 px-3 py-2 text-xs font-medium transition-colors ${
                activeTab === tab.id
                  ? 'text-blue-400 border-b-2 border-blue-400 bg-white/5'
                  : 'text-gray-500 hover:text-gray-300 hover:bg-white/5'
              }`}
            >
              {tab.label}
            </button>
          ))}
        </div>

        {/* Content */}
        <div className="flex-1 overflow-y-auto">
          {activeTab === 'tasks' && (
            <TasksTab
              tasks={state.tasks || []}
              scheduled={state.scheduled || []}
              sendAction={sendAction}
            />
          )}
          {activeTab === 'story' && (
            <StoryTab
              story={state.story}
              quest={state.quest}
              social={state.social}
              npcSocialLog={state.npcSocialLog}
              sendAction={sendAction}
            />
          )}
          {activeTab === 'politics' && (
            <PoliticsTab
              politics={state.politics}
              sendAction={sendAction}
            />
          )}
          {activeTab === 'director' && (
            <DirectorTab
              loadedNpcs={state.loadedNpcs || []}
              actions={state.actions || []}
              sendAction={sendAction}
            />
          )}
          {activeTab === 'actions' && (
            <ActionsTab
              actions={state.actions || []}
              sendAction={sendAction}
            />
          )}
          {activeTab === 'packages' && (
            <PackagesTab
              packages={state.packages || []}
              sendAction={sendAction}
              removingIds={removingPkgIds}
              onMarkRemoving={(formId) => setRemovingPkgIds(prev => new Set([...prev, formId]))}
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
  );
}

export default App;
