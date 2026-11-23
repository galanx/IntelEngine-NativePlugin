import withSections from './Section';
import React, { useState } from 'react';
import GameTime from './GameTime';
import { t } from '../i18n';

// label: the setting's name as on SkyrimNet's plugin settings page ("Story type: ..."), shown translated; type: what
// the log calls it, English in every language (the Director shows its own translated names).
const STORY_TYPES = [
  { key: 'seek_player', label: "Someone comes to see you", type: 'Seek Player', icon: '\u{1F50D}' },
  { key: 'informant', label: "Someone brings a rumor", type: 'Informant', icon: '\u{1F4E2}' },
  { key: 'road_encounter', label: "A meeting on the road", type: 'Road Encounter', icon: '\u{1F6E4}' },
  { key: 'ambush', label: "An ambush", type: 'Ambush', icon: '\u{2694}' },
  { key: 'faction_ambush', label: "A faction's patrol hunts you", type: 'Faction Ambush', icon: '\u{1F6E1}' },
  { key: 'stalker', label: "Someone follows you unseen", type: 'Stalker', icon: '\u{1F441}' },
  { key: 'message', label: "A message is delivered", type: 'Message', icon: '\u{1F4E8}' },
  { key: 'quest', label: "Someone has work for you", type: 'Quest', icon: '\u{2728}' },
  { key: 'npc_interaction', label: 'NPC Interaction', icon: '\u{1F91D}' },
  { key: 'npc_gossip', label: 'NPC Gossip', icon: '\u{1F5E3}' },
];

const TYPE_ICON_MAP = Object.fromEntries(STORY_TYPES.map(t => [t.key, t.icon]));

function StoryTab({ story, quest, social, npcSocialLog, calendar, sendAction }) {
  const [expandedLog, setExpandedLog] = useState(null);
  if (!story) {
    return (
      <div className="p-3">
        <div className="empty-state">{t('Story engine data not available. Open the dashboard in-game to see live data.')}</div>
      </div>
    );
  }

  return (
    <div className="ie-page p-3 space-y-4">
      {/* Story Engine Status */}
      <section>
        <div className="flex items-center justify-between mb-2">
          <h2 className="section-header">{t('Story Engine')}</h2>
          <span className={`text-xs font-medium ${story.enabled ? 'text-emerald-400' : 'text-red-400'}`}>
            {t(story.enabled ? 'Active' : 'Disabled')}
          </span>
        </div>

        {story.isActive && (
          <div className="slot-card rounded px-3 py-2 mb-2 border-l-2 border-blue-400">
            <div className="flex items-center justify-between mb-0.5">
              <div className="flex items-center gap-2">
                <span className="text-sm">{TYPE_ICON_MAP[story.activeType] || '\u{1F4CC}'}</span>
                <span className="text-sm text-gray-200 font-medium">
                  {story.activeNPC || t('Unknown NPC')}
                </span>
              </div>
              <span className="text-[10px] text-blue-400">
                {story.activeType && t(story.activeType.replace(/_/g, ' '))}
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
            <div className="text-xs text-gray-500">{t('No active story dispatch')}</div>
            {story.nextCheckIn != null && story.nextCheckIn > 0 && (
              <div className="text-xs text-gray-500 mt-0.5">
                {t('Next look by the clock in {hours} hours (arriving somewhere or waking up can bring one sooner)', { hours: story.nextCheckIn.toFixed(1) })}
              </div>
            )}
          </div>
        )}
      </section>

      {/* Story Type Toggles */}
      <section>
        <h2 className="section-header mb-2">{t('Story types')}</h2>
        <p className="text-[10px] text-gray-400 mb-1">{t('What the dungeon master may start by itself. A type switched off is not in a story of the town either: no ambush or shadow inside one, and with "Someone has work for you" off none begins. Director dispatch can use any type. What NPCs do among themselves is under Settings, NPC Behavior.')}</p>
        <div className="grid grid-cols-1 gap-1">
          {STORY_TYPES.filter(type => !type.key.startsWith('npc_')).map(type => {
            const enabled = story.types ? story.types[type.key] : true;
            return (
              <button
                key={type.key}
                title={t('The Director and the log call this type "{type}".', { type: type.type })}
                onClick={() => sendAction('toggleStoryType', { type: type.key, enabled: !enabled })}
                className={`flex items-center gap-2 px-3 py-1.5 rounded text-left transition-colors ${
                  enabled
                    ? 'slot-card text-gray-200'
                    : 'bg-transparent text-gray-600 border border-white/5'
                }`}
              >
                <span className="text-sm w-5 text-center">{type.icon}</span>
                <span className="text-xs flex-1">{t(type.label)}</span>
                <span className={`text-xs ${enabled ? 'text-emerald-400' : 'text-gray-600'}`}>
                  {t(enabled ? 'ON' : 'OFF')}
                </span>
              </button>
            );
          })}
        </div>
      </section>

      {/* Active Quest */}
      {quest && quest.active && (
        <section>
          <h2 className="section-header mb-2">{t("The Dungeon Master's Quest")}</h2>
          <p className="text-[10px] text-gray-400 mb-1">{t("A faction's quest written by the dungeon master. Stories of the town, bounties, keepsakes and ventures are on the World page.")}</p>
          <div className="slot-card rounded px-3 py-2 border-l-2 border-amber-400">
            <div className="flex items-center justify-between mb-1">
              <span className="text-sm text-gray-200 font-medium">
                {t(quest.subType ? quest.subType.replace(/_/g, ' ') : 'Quest')}
              </span>
              <span className="text-xs text-amber-400">{t('In Progress')}</span>
            </div>
            {quest.giver && (
              <div className="text-xs text-gray-400">{t('Quest giver: {name}', { name: quest.giver })}</div>
            )}
            {quest.location && (
              <div className="text-xs text-gray-400">{t('Location: {place}', { place: quest.location })}</div>
            )}
            {quest.enemyType && (
              <div className="text-xs text-gray-400">
                {t('Enemies: {enemies} ({state})', { enemies: t(quest.enemyType), state: t(quest.enemiesSpawned ? 'spawned' : 'pending') })}
              </div>
            )}
            {quest.victimName && (
              <div className="text-xs text-gray-400">
                {t('Victim: {name} ({state})', { name: quest.victimName, state: t(quest.victimFreed ? 'freed' : 'captive') })}
              </div>
            )}
            {quest.itemName && (
              <div className="text-xs text-gray-400">
                {t('Item: {item}', { item: quest.itemName })}
              </div>
            )}
            <button
              className="mt-2 px-3 py-1 text-xs rounded bg-red-900/40 hover:bg-red-800/60 text-red-300 border border-red-700/30 transition-colors"
              onClick={() => sendAction('cancelQuest')}
            >
              {t('Cancel Quest')}
            </button>
          </div>
        </section>
      )}

      {/* NPC Social */}
      {social && (
        <section>
          <div className="flex items-center justify-between mb-2">
            <h2 className="section-header">{t('NPC Social')}</h2>
            <span className={`text-xs ${social.enabled ? 'text-emerald-400' : 'text-gray-600'}`}>
              {t(social.enabled ? 'Active' : 'Disabled')}
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
                  {social.type && t(social.type.replace(/_/g, ' '))}
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
              <div className="text-xs text-gray-500">{t('No active NPC interactions')}</div>
            </div>
          )}
        </section>
      )}

      {/* Recent NPC Activity */}
      {npcSocialLog && npcSocialLog.length > 0 && (
        <section>
          <h2 className="section-header mb-2">{t('Recent NPC Activity')}</h2>
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
                  {entry.location && (
                    <span className="text-[10px] text-gray-600">
                      {entry.location}
                    </span>
                  )}
                  <span className="text-[10px] text-gray-500 ml-auto">
                    {t(entry.type === 'npc_gossip' ? 'gossip' : 'interaction')}
                  </span>
                </div>
                <div className="text-xs text-gray-400 leading-relaxed pl-6">
                  {entry.text}
                  <GameTime time={entry.time} calendar={calendar} />
                </div>
                {entry.detail && entry.type !== 'npc_gossip' && (
                  <div className="pl-6 mt-1">
                    <button
                      className="text-[10px] text-blue-400 hover:text-blue-300"
                      onClick={() => setExpandedLog(expandedLog === i ? null : i)}
                    >
                      {t(expandedLog === i ? 'hide details' : 'show details')}
                    </button>
                    {expandedLog === i && (
                      <div className="text-[11px] text-gray-300 mt-1 p-2 rounded bg-black/30 leading-relaxed">
                        {entry.detail}
                      </div>
                    )}
                  </div>
                )}
              </div>
            ))}
          </div>
        </section>
      )}
    </div>
  );
}

export default withSections(StoryTab);
