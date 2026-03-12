import React from 'react';

const STATUS_COLORS = {
  Alliance: 'text-emerald-400',
  Friendly: 'text-green-400',
  Neutral: 'text-gray-400',
  Tense: 'text-yellow-400',
  Hostile: 'text-orange-400',
  Critical: 'text-red-400',
  War: 'text-red-400',
};

const TYPE_LABELS = {
  military: 'Military',
  guild: 'Guild',
  political: 'Political',
};

function MoraleBar({ value, label }) {
  const color = value > 60 ? 'bg-green-500' : value > 30 ? 'bg-yellow-500' : 'bg-red-500';
  return (
    <div className="flex-1">
      <div className="flex justify-between text-[10px] mb-0.5">
        <span className="text-gray-400 truncate">{label}</span>
        <span className={value > 60 ? 'text-green-400' : value > 30 ? 'text-yellow-400' : 'text-red-400'}>{value}%</span>
      </div>
      <div className="h-1.5 bg-white/5 rounded-full overflow-hidden">
        <div className={`h-full ${color} rounded-full transition-all`} style={{ width: `${value}%` }} />
      </div>
    </div>
  );
}

function PoliticsTab({ politics, sendAction }) {
  if (!politics || !politics.enabled) {
    return (
      <div className="p-3">
        <div className="empty-state">
          {politics && !politics.enabled
            ? 'Faction politics is disabled. Enable it in Settings.'
            : 'Politics data not available. Open the dashboard in-game to see live data.'}
        </div>
      </div>
    );
  }

  const factions = politics.factions || [];
  const relations = politics.relations || [];
  const events = politics.events || [];
  const wars = politics.wars || [];
  const standings = politics.player_standings || [];

  // Build faction name lookup
  const factionNames = {};
  for (const f of factions) {
    factionNames[f.id] = f.name;
  }

  // Separate war events for special display
  const warEvents = events.filter(e => e.type === 'war_declaration' || e.type === 'battle_result' || e.type === 'surrender');
  const otherEvents = events.filter(e => e.type !== 'war_declaration' && e.type !== 'battle_result' && e.type !== 'surrender');

  return (
    <div className="p-3 space-y-4">
      {/* Active Wars */}
      {wars.length > 0 && (
        <section>
          <h2 className="section-header mb-2">
            Active Wars
            <span className="ml-2 text-[10px] text-red-400 font-normal">({wars.length})</span>
          </h2>
          <div className="space-y-2">
            {wars.map((w, i) => {
              const nameA = factionNames[w.faction_a] || w.faction_a;
              const nameB = factionNames[w.faction_b] || w.faction_b;
              return (
                <div key={i} className="slot-card rounded px-3 py-2 border-l-2 border-red-500">
                  <div className="flex items-center justify-between mb-2">
                    <span className="text-sm text-red-400 font-medium">
                      {nameA} vs {nameB}
                    </span>
                    <span className="text-[10px] text-gray-500">
                      {w.battles} battle{w.battles !== 1 ? 's' : ''}
                    </span>
                  </div>
                  <div className="space-y-1.5">
                    <div className="flex gap-3">
                      <MoraleBar value={w.morale_a} label={`${nameA} morale`} />
                      <MoraleBar value={w.morale_b} label={`${nameB} morale`} />
                    </div>
                    <div className="flex gap-4 text-[10px]">
                      <div>
                        <span className="text-gray-500">Strength: </span>
                        <span className="text-gray-300">{w.strength_a}%</span>
                        <span className="text-gray-600"> / </span>
                        <span className="text-gray-300">{w.strength_b}%</span>
                      </div>
                    </div>
                    {/* Battle History */}
                    {w.recent_battles && w.recent_battles.length > 0 && (
                      <div className="mt-1.5 pt-1.5 border-t border-white/5 space-y-1">
                        <div className="text-[10px] text-gray-500 uppercase tracking-wider">Battle History</div>
                        {w.recent_battles.map((b, bi) => {
                          const isAttackerVictory = b.result === 'attacker_victory';
                          const isDefenderVictory = b.result === 'defender_victory';
                          const victorName = isAttackerVictory
                            ? (factionNames[b.attacker] || b.attacker)
                            : isDefenderVictory
                              ? (factionNames[b.defender] || b.defender)
                              : null;
                          return (
                            <div key={bi} className="flex items-start gap-1.5 text-[10px]">
                              <span className={isAttackerVictory || isDefenderVictory ? 'text-red-400' : 'text-yellow-400'}>
                                {isAttackerVictory || isDefenderVictory ? '⚔' : '⚖'}
                              </span>
                              <div className="flex-1 min-w-0">
                                <span className="text-gray-300">{b.narrative || b.location}</span>
                                <span className="text-gray-600 ml-1">
                                  {victorName ? `${victorName} wins` : 'Draw'}
                                  {' · '}-{b.attacker_losses}/-{b.defender_losses}
                                </span>
                              </div>
                            </div>
                          );
                        })}
                      </div>
                    )}
                  </div>
                </div>
              );
            })}
          </div>

          {/* War Events Timeline */}
          {warEvents.length > 0 && (
            <div className="mt-2 space-y-1">
              {warEvents.map((e, i) => (
                <div key={i} className="slot-card rounded px-3 py-1.5 border-l-2 border-red-500/30">
                  <div className="flex items-center gap-2">
                    <span className="text-[10px] text-red-400/70 shrink-0">
                      {e.type === 'war_declaration' ? 'WAR' : e.type === 'surrender' ? 'PEACE' : 'BATTLE'}
                    </span>
                    <span className="text-xs text-gray-300 truncate">{e.description}</span>
                  </div>
                </div>
              ))}
            </div>
          )}
        </section>
      )}

      {/* Faction Relations */}
      <section>
        <h2 className="section-header mb-2">Faction Relations</h2>
        {relations.length === 0 ? (
          <div className="text-xs text-gray-500">No faction relations yet. Wait for the first political tick.</div>
        ) : (
          <div className="space-y-1">
            {relations.map((r, i) => (
              <div key={i} className="slot-card rounded px-3 py-1.5 flex items-center justify-between">
                <div className="flex items-center gap-2 flex-1 min-w-0">
                  <span className="text-xs text-gray-300 truncate">
                    {factionNames[r.a] || r.a}
                  </span>
                  <span className="text-gray-600 text-[10px]">&harr;</span>
                  <span className="text-xs text-gray-300 truncate">
                    {factionNames[r.b] || r.b}
                  </span>
                </div>
                <div className="flex items-center gap-2 ml-2 shrink-0">
                  <span className={`text-xs font-medium ${STATUS_COLORS[r.status] || 'text-gray-400'}`}>
                    {r.score > 0 ? '+' : ''}{r.score}
                  </span>
                  <span className={`text-[10px] ${STATUS_COLORS[r.status] || 'text-gray-500'}`}>
                    {r.status}
                  </span>
                  {r.trade && <span className="text-[10px] text-emerald-400" title="Trade Active">$</span>}
                  {r.war && <span className="text-[10px] text-red-400" title="At War">!</span>}
                </div>
              </div>
            ))}
          </div>
        )}
      </section>

      {/* Player Standings */}
      {standings.length > 0 && (
        <section>
          <h2 className="section-header mb-2">Player Standing</h2>
          <div className="space-y-1">
            {standings.map((s, i) => (
              <div key={i} className="slot-card rounded px-3 py-1.5 flex items-center justify-between">
                <div className="flex items-center gap-2">
                  <span className="text-xs text-gray-300">{factionNames[s.faction] || s.faction}</span>
                  {s.title && <span className="text-[10px] text-gray-500 italic">{s.title}</span>}
                </div>
                <span className={`text-xs font-medium ${STATUS_COLORS[s.status] || 'text-gray-400'}`}>
                  {s.standing > 0 ? '+' : ''}{s.standing}
                </span>
              </div>
            ))}
          </div>
        </section>
      )}

      {/* Factions Overview */}
      <section>
        <h2 className="section-header mb-2">Factions</h2>
        <div className="grid grid-cols-2 gap-1">
          {factions.map((f, i) => (
            <div key={i} className="slot-card rounded px-2 py-1.5">
              <div className="text-xs text-gray-200 font-medium">{f.name}</div>
              <div className="text-[10px] text-gray-500">
                {TYPE_LABELS[f.type] || f.type} &middot; {f.hold}
              </div>
            </div>
          ))}
        </div>
      </section>

      {/* Recent Events (non-war) */}
      <section>
        <h2 className="section-header mb-2">Recent Events</h2>
        {otherEvents.length === 0 && warEvents.length === 0 ? (
          <div className="text-xs text-gray-500">No political events yet.</div>
        ) : otherEvents.length === 0 ? (
          <div className="text-xs text-gray-500">Only war events this period.</div>
        ) : (
          <div className="space-y-1.5">
            {otherEvents.map((e, i) => (
              <div key={i} className="slot-card rounded px-3 py-2">
                <div className="flex items-center justify-between mb-0.5">
                  <span className="text-[10px] text-gray-500">
                    {e.type.replace(/_/g, ' ')}
                  </span>
                  <span className={`text-[10px] font-medium ${
                    e.delta > 0 ? 'text-green-400' : e.delta < 0 ? 'text-red-400' : 'text-gray-500'
                  }`}>
                    {e.delta > 0 ? '+' : ''}{e.delta}
                  </span>
                </div>
                <div className="text-xs text-gray-300 leading-relaxed">{e.description}</div>
                <div className="text-[10px] text-gray-600 mt-0.5">
                  {factionNames[e.faction_a] || e.faction_a}
                  {e.faction_b ? ` \u2194 ${factionNames[e.faction_b] || e.faction_b}` : ''}
                </div>
              </div>
            ))}
          </div>
        )}
      </section>
    </div>
  );
}

export default PoliticsTab;
