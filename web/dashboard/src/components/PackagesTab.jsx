import { useCallback } from 'react';

const PKG_ICONS = {
  'Travel (Walk)': '\u{1F6B6}',
  'Travel (Jog)': '\u{1F3C3}',
  'Travel (Run)': '\u{1F4A8}',
  'Travel (Stalk)': '\u{1F441}',
  'Sandbox': '\u{1F3D6}',
  'Sandbox (Near Player)': '\u{1F4CD}',
};

function PackagesTab({ packages, sendAction, removingIds, onMarkRemoving }) {
  const handleRemove = useCallback((formId) => {
    onMarkRemoving(formId);
    sendAction('removePackages', { formId });
  }, [sendAction, onMarkRemoving]);

  if (!packages || packages.length === 0) {
    return (
      <div className="p-3">
        <div className="empty-state">No NPCs currently running IntelEngine packages.</div>
      </div>
    );
  }

  return (
    <div className="p-3 space-y-4">
      <section>
        <div className="flex items-center justify-between mb-2">
          <h2 className="section-header">Active Packages</h2>
          <span className="text-xs text-gray-500">{packages.length} NPC{packages.length !== 1 ? 's' : ''}</span>
        </div>
        <div className="space-y-1.5">
          {packages.map((entry, i) => {
            const isRemoving = removingIds.has(entry.formId);
            return (
              <div key={entry.formId || i} className={`slot-card rounded px-3 py-2 flex items-center gap-2 transition-opacity ${isRemoving ? 'opacity-40' : ''}`}>
                <span className="text-sm">{PKG_ICONS[entry.pkgType] || '\u{1F4E6}'}</span>
                <div className="flex-1 min-w-0">
                  <div className="text-sm text-gray-200 font-medium truncate">{entry.name}</div>
                  <div className="text-xs text-gray-500">{entry.pkgType || 'Unknown'}</div>
                </div>
                <button
                  onClick={() => handleRemove(entry.formId)}
                  disabled={isRemoving}
                  className={`px-2 py-1 text-[10px] rounded transition-colors whitespace-nowrap ${
                    isRemoving
                      ? 'bg-gray-500/20 text-gray-500 cursor-not-allowed'
                      : 'bg-red-500/20 text-red-400 hover:bg-red-500/30'
                  }`}
                >
                  {isRemoving ? 'Removing\u2026' : 'Remove'}
                </button>
              </div>
            );
          })}
        </div>
      </section>
    </div>
  );
}

export default PackagesTab;
