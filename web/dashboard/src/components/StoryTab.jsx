import React from 'react';

const STORY_TYPES = [
  { key: 'seek_player', label: 'Seek Player', icon: '\u{1F50D}' },
  { key: 'informant', label: 'Informant', icon: '\u{1F4E2}' },
  { key: 'road_encounter', label: 'Road Encounter', icon: '\u{1F6E4}' },
  { key: 'ambush', label: 'Ambush', icon: '\u{2694}' },
  { key: 'stalker', label: 'Stalker', icon: '\u{1F441}' },
  { key: 'message', label: 'Message', icon: '\u{1F4E8}' },
  { key: 'quest', label: 'Quest', icon: '\u{2728}' },
  { key: 'npc_interaction', label: 'NPC Interaction', icon: '\u{1F91D}' },
  { key: 'npc_gossip', label: 'NPC Gossip', icon: '\u{1F5E3}' },
];

const TYPE_ICON_MAP = Object.fromEntries(STORY_TYPES.map(t => [t.key, t.icon]));

function StoryTab({ story, quest, social, npcSocialLog, sendAction }) {
  if (!story) {
    return (
      <div className="p-3">
        <div className="empty-state">Story engine data not available. Open the dashboard in-game to see live data.</div>
      </div>
    );
  }

  return (
    <div className="p-3 space-y-4">
      {/* Story Engine Status */}
      <section>
        <div className="flex items-center justify-between mb-2">
          <h2 className="section-header">Story Engine</h2>
          <span className={`text-xs font-medium ${story.enabled ? 'text-emerald-400' : 'text-red-400'}`}>
            {story.enabled ? 'Active' : 'Disabled'}
          </span>
        </div>

        {story.isActive && (
          <div className="slot-card rounded px-3 py-2 mb-2 border-l-2 border-blue-400">
            <div className="flex items-center justify-between mb-0.5">
              <div className="flex items-center gap-2">
                <span className="text-sm">{TYPE_ICON_MAP[story.activeType] || '\u{1F4CC}'}</span>
                <span className="text-sm text-gray-200 font-medium">
                  {story.activeNPC || 'Unknown NPC'}
                </span>
              </div>
              <span className="text-[10px] text-blue-400">
                {story.activeType && story.activeType.replace(/_/g, ' ')}
              </span>
            </div>
            {story.activeNarration && (
              <div className="text-xs text-gray-400 leading-relaxed mt-1 pl-6 italic">
                {story.activeNarration}
              </div>
            )}
          </div>
        )}

        {!story.isActive && (
          <div className="slot-card rounded px-3 py-2 mb-2 opacity-60">
            <div className="text-xs text-gray-500">No active story dispatch</div>
            {story.nextCheckIn != null && story.nextCheckIn > 0 && (
              <div className="text-xs text-gray-500 mt-0.5">
                Next check in {story.nextCheckIn.toFixed(1)} hours
              </div>
            )}
          </div>
        )}
      </section>

      {/* Story Type Toggles */}
      <section>
        <h2 className="section-header mb-2">Story Types</h2>
        <div className="grid grid-cols-1 gap-1">
          {STORY_TYPES.map(type => {
            const enabled = story.types ? story.types[type.key] : true;
            return (
              <button
                key={type.key}
                onClick={() => sendAction('toggleStoryType', { type: type.key, enabled: !enabled })}
                className={`flex items-center gap-2 px-3 py-1.5 rounded text-left transition-colors ${
                  enabled
                    ? 'slot-card text-gray-200'
                    : 'bg-transparent text-gray-600 border border-white/5'
                }`}
              >
                <span className="text-sm w-5 text-center">{type.icon}</span>
                <span className="text-xs flex-1">{type.label}</span>
                <span className={`text-xs ${enabled ? 'text-emerald-400' : 'text-gray-600'}`}>
                  {enabled ? 'ON' : 'OFF'}
                </span>
              </button>
            );
          })}
        </div>
      </section>

      {/* Active Quest */}
      {quest && quest.active && (
        <section>
          <h2 className="section-header mb-2">Active Quest</h2>
          <div className="slot-card rounded px-3 py-2 border-l-2 border-amber-400">
            <div className="flex items-center justify-between mb-1">
              <span className="text-sm text-gray-200 font-medium">
                {quest.subType ? quest.subType.replace(/_/g, ' ') : 'Quest'}
              </span>
              <span className="text-xs text-amber-400">In Progress</span>
            </div>
            {quest.giver && (
              <div className="text-xs text-gray-400">Quest giver: {quest.giver}</div>
            )}
            {quest.location && (
              <div className="text-xs text-gray-400">Location: {quest.location}</div>
            )}
            {quest.enemyType && (
              <div className="text-xs text-gray-400">
                Enemies: {quest.enemyType} ({quest.enemiesSpawned ? 'spawned' : 'pending'})
              </div>
            )}
            {quest.victimName && (
              <div className="text-xs text-gray-400">
                Victim: {quest.victimName} ({quest.victimFreed ? 'freed' : 'captive'})
              </div>
            )}
            {quest.itemName && (
              <div className="text-xs text-gray-400">
                Item: {quest.itemName}
              </div>
            )}
          </div>
        </section>
      )}

      {/* NPC Social */}
      {social && (
        <section>
          <div className="flex items-center justify-between mb-2">
            <h2 className="section-header">NPC Social</h2>
            <span className={`text-xs ${social.enabled ? 'text-emerald-400' : 'text-gray-600'}`}>
              {social.enabled ? 'Active' : 'Disabled'}
            </span>
          </div>
          {social.isActive ? (
            <div className="slot-card rounded px-3 py-2 border-l-2 border-purple-400">
              <div className="flex items-center gap-2">
                <span className="text-sm">{TYPE_ICON_MAP[social.type] || '\u{1F91D}'}</span>
                <span className="text-sm text-gray-200">
                  {social.traveler} &rarr; {social.target}
                </span>
                <span className="text-[10px] text-gray-500 ml-auto">
                  {social.type && social.type.replace(/_/g, ' ')}
                </span>
              </div>
              {social.narration && (
                <div className="text-xs text-gray-400 leading-relaxed mt-1 pl-6 italic">
                  {social.narration}
                </div>
              )}
            </div>
          ) : (
            <div className="slot-card rounded px-3 py-1.5 opacity-60">
              <div className="text-xs text-gray-500">No active NPC interactions</div>
            </div>
          )}
        </section>
      )}

      {/* Recent NPC Activity */}
      {npcSocialLog && npcSocialLog.length > 0 && (
        <section>
          <h2 className="section-header mb-2">Recent NPC Activity</h2>
          <div className="space-y-1.5">
            {npcSocialLog.map((entry, i) => (
              <div key={i} className="slot-card rounded px-3 py-2">
                <div className="flex items-center gap-2 mb-0.5">
                  <span className="text-sm">
                    {TYPE_ICON_MAP[entry.type] || '\u{1F91D}'}
                  </span>
                  <span className="text-xs text-gray-200 font-medium">
                    {entry.npc1} & {entry.npc2}
                  </span>
                  <span className="text-[10px] text-gray-500 ml-auto">
                    {entry.type === 'npc_gossip' ? 'gossip' : 'interaction'}
                  </span>
                </div>
                <div className="text-xs text-gray-400 leading-relaxed pl-6">
                  {entry.text}
                </div>
              </div>
            ))}
          </div>
        </section>
      )}
    </div>
  );
}

export default StoryTab;
