import React from 'react';

const STATE_LABELS = {
  0: 'Empty',
  1: 'Traveling',
  2: 'At Destination',
  3: 'Returning',
  5: 'Searching',
  8: 'At Target',
};

const STATE_COLORS = {
  1: 'text-yellow-400',
  2: 'text-emerald-400',
  3: 'text-blue-400',
  5: 'text-purple-400',
  8: 'text-pink-400',
};

const DOT_COLORS = {
  1: 'bg-yellow-400',
  2: 'bg-emerald-400',
  3: 'bg-blue-400',
  5: 'bg-purple-400',
  8: 'bg-pink-400',
};

const TASK_TYPE_ICONS = {
  travel: '\u{1F5FA}',
  fetch_npc: '\u{1F3C3}',
  deliver_message: '\u{1F4DC}',
  search_for_actor: '\u{1F50D}',
  escort: '\u{1F6E1}',
  story: '\u{1F4D6}',
  story_npc: '\u{1F4AC}',
  scheduled: '\u{23F0}',
};

function SlotCard({ slot, onCancel }) {
  const stateLabel = STATE_LABELS[slot.state] || `State ${slot.state}`;
  const stateColor = STATE_COLORS[slot.state] || 'text-gray-400';
  const dotColor = DOT_COLORS[slot.state] || 'bg-gray-400';
  const icon = TASK_TYPE_ICONS[slot.taskType] || '\u{1F4CB}';
  const taskLabel = slot.taskType
    ? slot.taskType.replace(/_/g, ' ').replace(/\b\w/g, c => c.toUpperCase())
    : 'Unknown';

  return (
    <div className="slot-card rounded px-3 py-2">
      {/* Row 1: State + Cancel */}
      <div className="flex items-center justify-between mb-1">
        <div className="flex items-center gap-1.5">
          <span className={`inline-block w-1.5 h-1.5 rounded-full pulse-dot ${dotColor}`} />
          <span className={`text-xs font-medium ${stateColor}`}>{stateLabel}</span>
        </div>
        <div className="flex items-center gap-2">
          <span className="text-xs text-gray-600 font-mono">#{slot.index + 1}</span>
          <button onClick={onCancel} className="btn-cancel" title="Cancel task">
            &#x2715;
          </button>
        </div>
      </div>

      {/* Row 2: Agent + task info */}
      <div className="flex items-center gap-2">
        <span className="text-sm">{icon}</span>
        <div className="flex-1 min-w-0">
          <div className="text-sm text-gray-200 font-medium truncate">
            {slot.agentName || 'Unknown NPC'}
          </div>
          <div className="text-xs text-gray-400 truncate">
            {taskLabel}{slot.targetName ? ` \u2192 ${slot.targetName}` : ''}
          </div>
        </div>
      </div>
    </div>
  );
}

export default SlotCard;
