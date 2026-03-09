import { useCallback } from 'react';

function ActionsTab({ actions, sendAction }) {
  const handleToggle = useCallback((name, currentEnabled) => {
    sendAction('toggleAction', { name, enabled: !currentEnabled });
  }, [sendAction]);

  if (!actions || actions.length === 0) {
    return (
      <div className="p-3">
        <div className="empty-state">No actions loaded.</div>
      </div>
    );
  }

  return (
    <div className="p-3 space-y-3">
      <div className="flex items-center justify-between mb-1">
        <h2 className="section-header">SkyrimNet Actions</h2>
        <span className="text-xs text-gray-500">{actions.length} actions</span>
      </div>

      <div className="px-2 py-1.5 rounded bg-amber-500/10 border border-amber-500/20">
        <p className="text-[10px] text-amber-400/80 leading-relaxed">
          Toggles modify the YAML files on disk. Changes take effect on next game launch.
        </p>
      </div>

      <div className="space-y-1.5">
        {actions.map(action => (
          <button
            key={action.name}
            onClick={() => handleToggle(action.name, action.enabled)}
            className={`w-full flex items-center gap-2 px-3 py-2 rounded text-left transition-colors ${
              action.enabled
                ? 'slot-card text-gray-200'
                : 'bg-transparent text-gray-600 border border-white/5'
            }`}
          >
            <div className="flex-1 min-w-0">
              <div className="text-xs font-medium truncate">{action.name}</div>
              {action.description && (
                <div className="text-[10px] text-gray-500 truncate mt-0.5">{action.description}</div>
              )}
            </div>
            <span className={`text-xs shrink-0 ${action.enabled ? 'text-emerald-400' : 'text-gray-600'}`}>
              {action.enabled ? 'ON' : 'OFF'}
            </span>
          </button>
        ))}
      </div>
    </div>
  );
}

export default ActionsTab;
