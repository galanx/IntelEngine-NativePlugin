import React from 'react';

const STATUS_COLORS = {
  Alliance: 'text-emerald-400',
  Friendly: 'text-green-400',
  Neutral: 'text-gray-400',
  Tense: 'text-yellow-400',
  Hostile: 'text-orange-400',
  War: 'text-red-400',
};

const TYPE_LABELS = {
  military: 'Military',
  guild: 'Guild',
  political: 'Political',
};

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

  return (
    <div className="p-3 space-y-4">
      {/* Active Wars */}
      {wars.length > 0 && (
        <section>
          <h2 className="section-header mb-2">Active Wars</h2>
          <div className="space-y-2">
            {wars.map((w, i) => (
              <div key={i} className="slot-card rounded px-3 py-2 border-l-2 border-red-500">
                <div className="flex items-center justify-between mb-1">
                  <span className="text-sm text-red-400 font-medium">
                    {factionNames[w.faction_a] || w.faction_a} vs {factionNames[w.faction_b] || w.faction_b}
                  </span>
                  <span className="text-[10px] text-gray-500">{w.battles} battles</span>
                </div>
                <div className="flex gap-4 text-xs">
                  <div>
                    <span className="text-gray-500">Morale: </span>
                    <span className={w.morale_a > 50 ? 'text-green-400' : 'text-red-400'}>{w.morale_a}%</span>
                    <span className="text-gray-600"> / </span>
                    <span className={w.morale_b > 50 ? 'text-green-400' : 'text-red-400'}>{w.morale_b}%</span>
                  </div>
                  <div>
                    <span className="text-gray-500">Strength: </span>
                    <span className="text-gray-300">{w.strength_a}%</span>
                    <span className="text-gray-600"> / </span>
                    <span className="text-gray-300">{w.strength_b}%</span>
                  </div>
                </div>
              </div>
            ))}
          </div>
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

      {/* Recent Events */}
      <section>
        <h2 className="section-header mb-2">Recent Events</h2>
        {events.length === 0 ? (
          <div className="text-xs text-gray-500">No political events yet.</div>
        ) : (
          <div className="space-y-1.5">
            {events.map((e, i) => (
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
