import React from 'react';
import SlotCard from './SlotCard';

const SCHED_TASK_ICONS = {
  travel: '\u{1F5FA}',
  fetch_npc: '\u{1F3C3}',
  deliver_message: '\u{1F4DC}',
};

function TasksTab({ tasks, scheduled, sendAction }) {
  const activeTasks = tasks.filter(s => s.state !== 0);
  const emptyTasks = tasks.filter(s => s.state === 0 && s.cooldownRemaining <= 0);
  const cooldownTasks = tasks.filter(s => s.state === 0 && s.cooldownRemaining > 0);

  const activeScheduled = scheduled.filter(s => s.agent);

  return (
    <div className="p-3 space-y-4">
      {/* Active Tasks */}
      <section>
        <div className="flex items-center justify-between mb-2">
          <h2 className="section-header">Active Tasks</h2>
          <span className="text-xs text-gray-500">{activeTasks.length}/5</span>
        </div>
        {activeTasks.length === 0 ? (
          <div className="empty-state">No active tasks</div>
        ) : (
          <div className="space-y-1.5">
            {activeTasks.map(slot => (
              <SlotCard
                key={slot.index}
                slot={slot}
                onCancel={() => sendAction('cancelTask', { slot: slot.index })}
              />
            ))}
          </div>
        )}
      </section>

      {/* Cooldowns */}
      {cooldownTasks.length > 0 && (
        <section>
          <h2 className="section-header mb-2">On Cooldown</h2>
          <div className="space-y-1">
            {cooldownTasks.map(slot => (
              <div key={slot.index} className="slot-card rounded px-3 py-1.5 opacity-60">
                <div className="flex items-center justify-between">
                  <span className="text-xs text-gray-400">
                    Slot {slot.index + 1} &mdash; {slot.agentName || 'Unknown'}
                  </span>
                  <span className="text-xs text-gray-500">
                    {Math.ceil(slot.cooldownRemaining)}s
                  </span>
                </div>
              </div>
            ))}
          </div>
        </section>
      )}

      {/* Scheduled Tasks */}
      <section>
        <div className="flex items-center justify-between mb-2">
          <h2 className="section-header">Scheduled</h2>
          <span className="text-xs text-gray-500">{activeScheduled.length}/10</span>
        </div>
        {activeScheduled.length === 0 ? (
          <div className="empty-state">No scheduled tasks</div>
        ) : (
          <div className="space-y-1.5">
            {activeScheduled.map((sched, i) => (
              <div key={i} className="slot-card rounded px-3 py-2">
                <div className="flex items-center justify-between mb-1">
                  <div className="flex items-center gap-2">
                    <span className="text-sm">
                      {SCHED_TASK_ICONS[sched.taskType] || '\u{1F4CB}'}
                    </span>
                    <span className="text-sm text-gray-200 font-medium truncate">
                      {sched.agent}
                    </span>
                  </div>
                  <div className="flex items-center gap-2">
                    <span className={`text-xs font-medium ${
                      sched.schedState === 1 ? 'text-yellow-400' :
                      sched.schedState === 2 ? 'text-emerald-400' :
                      'text-blue-400'
                    }`}>
                      {sched.schedState === 1 ? 'Dispatched' :
                       sched.schedState === 2 ? 'Meeting' : 'Pending'}
                    </span>
                    <button
                      onClick={() => sendAction('cancelSchedule', { slot: i })}
                      className="btn-cancel"
                      title="Cancel scheduled task"
                    >
                      &#x2715;
                    </button>
                  </div>
                </div>
                <div className="text-xs text-gray-400 truncate">
                  {sched.taskType && sched.taskType.replace(/_/g, ' ')}
                  {sched.destination ? ` \u2192 ${sched.destination}` : ''}
                  {sched.targetName ? ` (${sched.targetName})` : ''}
                </div>
                {(sched.timeDesc || sched.schedStatus) && (
                  <div className="text-xs text-gray-500 mt-0.5">
                    {sched.timeDesc}
                    {sched.schedStatus && (
                      <span className={`ml-2 font-medium ${
                        sched.schedStatus.includes('overdue') ? 'text-red-400' :
                        sched.schedStatus.includes('soon') ? 'text-yellow-400' :
                        'text-gray-400'
                      }`}>
                        {sched.schedStatus}
                      </span>
                    )}
                  </div>
                )}
              </div>
            ))}
          </div>
        )}
      </section>
    </div>
  );
}

export default TasksTab;
