import withSections from './Section';
import React from 'react';
import GameTime from './GameTime';
import FactionEditor, { hasUnsavedFactions } from './FactionEditor';
import { t, tk } from '../i18n';

const STATUS_COLORS = {
  Alliance: 'text-emerald-400',
  Friendly: 'text-green-400',
  Neutral: 'text-gray-400',
  Tense: 'text-yellow-400',
  Hostile: 'text-orange-400',
  Critical: 'text-red-400',
  War: 'text-red-400',
};

// Political Engine side colours (political_sides080.h); the dashboard feed sends pe_side only while PE is active.
const PE_SIDES = [[tk('Independent'), '#9aaba7'], [tk('Empire'), '#c45454'], [tk('Stormcloaks'), '#6095d0'], [tk('Aldmeri Dominion'), '#d3bc61']];
const peSideProps = (side) => ({ style: PE_SIDES[side] && { color: PE_SIDES[side][1] },
  title: PE_SIDES[side] ? t('{side} (Political Engine side)', { side: t(PE_SIDES[side][0]) }) : undefined });
const TYPE_LABELS = {
  military: tk('Military'),
  guild: tk('Guild'),
  political: tk('Political'),
};

// What each kind of event is called in the chronicle, and its colour (anything the list does not know is shown
// under its own name).
const EVENT_KINDS = {
  war_declaration: [tk('War declared'), 'text-red-400 border-red-500/60'],
  battle_result: [tk('Battle'), 'text-red-400 border-red-500/60'],
  player_battle: [tk('Your part in it'), 'text-amber-300 border-amber-400/60'],
  friendly_fire: [tk('Your own side struck'), 'text-orange-400 border-orange-500/60'],
  battle_gathering: [tk('Armies gather'), 'text-amber-400 border-amber-500/60'],
  battle_scheduled: [tk('Armies gather'), 'text-amber-400 border-amber-500/60'],
  border_skirmish: [tk('Skirmish'), 'text-orange-400 border-orange-500/60'],
  brawl: [tk('Brawl'), 'text-orange-400 border-orange-500/60'],
  surrender: [tk('Surrender'), 'text-emerald-400 border-emerald-500/60'],
  war_ended: [tk('War over'), 'text-emerald-400 border-emerald-500/60'],
  peace_treaty: [tk('Peace'), 'text-emerald-400 border-emerald-500/60'],
  alliance_proposal: [tk('Alliance'), 'text-emerald-400 border-emerald-500/60'],
  mutual_defense_pact: [tk('Pact'), 'text-emerald-400 border-emerald-500/60'],
  political_marriage: [tk('Marriage'), 'text-emerald-400 border-emerald-500/60'],
  assassination_attempt: [tk('Attempt on a life'), 'text-purple-400 border-purple-500/60'],
  assassination: [tk('Assassination'), 'text-purple-400 border-purple-500/60'],
  leader_death: [tk('A leader falls'), 'text-gray-300 border-gray-500/60'],
  espionage: [tk('Spying'), 'text-sky-400 border-sky-500/60'],
  sabotage: [tk('Sabotage'), 'text-sky-400 border-sky-500/60'],
  player_kill: [tk('Your doing'), 'text-amber-300 border-amber-400/60'],
};
const eventKind = (type) => EVENT_KINDS[type] || [String(type || 'event').replace(/_/g, ' '), 'text-gray-400 border-white/10'];

const CIVIL_WAR = [
  tk('The civil war is undecided: both sides are in the field.'),
  tk('A truce holds between the Empire and the Stormcloaks.'),
  tk('The civil war is over: the Empire holds Skyrim.'),
  tk('The civil war is over: the Stormcloaks hold Skyrim.'),
];

const hours = (h) => (h < 1 ? t('within the hour') : h < 1.5 ? t('about an hour') : t('about {n} hours', { n: Math.round(h) }));

function Meter({ value, max = 100, tone }) {
  const share = Math.max(0, Math.min(1, max > 0 ? value / max : 0));
  return (
    <div className="h-1.5 bg-white/5 rounded-full overflow-hidden">
      <div className={`h-full ${tone} rounded-full transition-all`} style={{ width: `${share * 100}%` }} />
    </div>
  );
}

function MoraleBar({ value, label }) {
  const color = value > 60 ? 'bg-green-500' : value > 30 ? 'bg-yellow-500' : 'bg-red-500';
  return (
    <div className="flex-1 min-w-0">
      <div className="flex justify-between text-[10px] mb-0.5">
        <span className="text-gray-400 truncate">{label}</span>
        <span className={value > 60 ? 'text-green-400' : value > 30 ? 'text-yellow-400' : 'text-red-400'}>{value}%</span>
      </div>
      <Meter value={value} tone={color} />
    </div>
  );
}

// What a rank gives, from the feed's own numbers (Ranks.h): said the same way wherever it is shown.
function perkWords(perk) {
  if (!perk) return [];
  const words = [];
  if (perk.called) words.push(t('you are called to the line when their army gathers'));
  if (perk.pay > 0) words.push(t('{n}% more pay after a battle won', { n: perk.pay }));
  if (perk.file > 0) words.push(t('{n} soldiers keep by you in battle', { n: perk.file }));
  if (perk.word) words.push(t('the line waits for your word to charge'));
  if (perk.famous) words.push(t('your name is talked of beyond their own'));
  return words;
}

// One faction's view of the player: the standing, the rank on its ladder and what was done to earn it.
function NameCard({ row, needed, perks }) {
  const { name, standing, status, rank = 0, earnedRank = 0, renown = 0, titles = [], record, knownAs } = row;
  const top = titles.length;                    // ranks above none
  const floor = earnedRank > 0 ? needed[earnedRank - 1] ?? 0 : 0;
  const ceiling = earnedRank < top ? needed[earnedRank] : null;
  const nextTitle = earnedRank < top ? titles[earnedRank] : null;
  const unhonoured = earnedRank > rank;         // earned, but they count the player an enemy
  const deeds = [];
  if (record) {
    if (record.battles > 0) deeds.push(t('{n} battles', { n: record.battles }) + (record.won > 0 ? t(', {n} won', { n: record.won }) : ''));
    if (record.kills > 0) deeds.push(t('{n} cut down', { n: record.kills }));
    if (record.champions > 0) deeds.push(t('{n} champions slain', { n: record.champions }));
    if (record.captains > 0) deeds.push(t('{n} captains slain', { n: record.captains }));
    if (record.saved > 0) deeds.push(t('{n} lives saved', { n: record.saved }));
    if (record.quests > 0) deeds.push(t('{n} errands', { n: record.quests }));
    if (record.deserted > 0) deeds.push(t('{n} battles walked away from', { n: record.deserted }));
  }
  const has = perkWords(perks[rank - 1]);
  const gains = perkWords(perks[earnedRank]).filter((word) => !perkWords(perks[earnedRank - 1]).includes(word));
  return (
    <div className={`slot-card rounded px-3 py-2 border-l-2 ${rank > 0 ? 'border-amber-400' : 'border-white/10'}`}>
      <div className="flex items-baseline justify-between gap-2">
        <span className="text-sm text-gray-100 font-medium truncate">{name}</span>
        {standing !== undefined && (
          <span className={`text-xs shrink-0 ${STATUS_COLORS[status] || 'text-gray-400'}`}>
            {t(status)} {standing > 0 ? '+' : ''}{standing}
          </span>
        )}
      </div>

      <div className="flex items-baseline gap-2 mt-0.5">
        <span className={`text-xs font-medium ${rank > 0 ? 'text-amber-300' : 'text-gray-500'}`}>
          {t(rank > 0 ? titles[rank - 1] : earnedRank > 0 ? 'No rank honoured' : 'No rank yet')}
        </span>
        {top > 0 && (
          <span className="text-[11px] tracking-widest" aria-hidden="true">
            {titles.map((title, i) => (
              <span key={i} title={t(title)} className={i < rank ? 'text-amber-400' : i < earnedRank ? 'text-amber-400/40' : 'text-gray-600'}>{i < earnedRank ? '◆' : '◇'}</span>
            ))}
          </span>
        )}
      </div>

      {top > 0 && (
        <div className="mt-1.5">
          <Meter value={ceiling === null ? 1 : renown - floor} max={ceiling === null ? 1 : ceiling - floor} tone="bg-amber-400" />
          <div className="flex justify-between text-[10px] mt-0.5">
            <span className="text-gray-400">{t('{n} renown', { n: renown })}</span>
            <span className="text-gray-500">
              {nextTitle ? t('{n} more to {title}', { n: ceiling - renown, title: t(nextTitle) }) : t('the top of their ladder')}
            </span>
          </div>
        </div>
      )}

      {unhonoured && (
        <div className="text-[10px] text-orange-400 mt-1">
          {t('You earned the rank of {title}, but they honour no rank while they count you an enemy.', { title: t(titles[earnedRank - 1]) })}
        </div>
      )}
      {knownAs && <div className="text-[11px] text-gray-300 italic leading-snug mt-1.5">{t('Known among them as {name}.', { name: knownAs })}</div>}
      {deeds.length > 0 && <div className="text-[10px] text-gray-500 mt-1">{deeds.join(' · ')}</div>}
      {has.length > 0 && <div className="text-[10px] text-emerald-400/90 mt-1">{t('Your rank: {perks}.', { perks: has.join('; ') })}</div>}
      {nextTitle && gains.length > 0 && (
        <div className="text-[10px] text-gray-500 mt-0.5">{t('As {title}: {perks}.', { title: t(nextTitle), perks: gains.join('; ') })}</div>
      )}
      {top > 0 && renown === 0 && (
        <div className="text-[10px] text-gray-500 mt-1">
          {t("Fight at their side, cut down their enemies' champions, save one of their leaders or do them a service: that is how a name is made.")}
        </div>
      )}
    </div>
  );
}

function PoliticsTab({ politics, calendar, sendAction, knownNpcs = [] }) {
  const [editing, setEditing] = React.useState(() => hasUnsavedFactions());
  const [allRelations, setAllRelations] = React.useState(false);
  const [allEvents, setAllEvents] = React.useState(false);
  if (!politics || !politics.enabled) {
    return (
      <div className="p-3">
        <div className="empty-state">
          {t(politics && !politics.enabled
            ? 'Faction politics is disabled. Enable it in Settings.'
            : 'Politics data not available. Open the dashboard in-game to see live data.')}
        </div>
      </div>
    );
  }

  const factions = politics.factions || [];
  const relations = politics.relations || [];
  const events = politics.events || [];
  const wars = politics.wars || [];
  const standings = politics.player_standings || [];
  const ranks = politics.player_ranks || [];
  const needed = politics.rank_needed || [];
  const perks = politics.rank_perks || [];
  const field = politics.field || {};
  const battle = field.battle;
  const gatherings = field.gatherings || [];

  // Build faction name lookup
  const factionNames = {};
  const factionSides = {};
  const factionById = {};
  for (const f of factions) {
    factionNames[f.id] = f.name;
    factionSides[f.id] = f.pe_side;
    factionById[f.id] = f;
  }
  const nameOf = (id) => factionNames[id] || id;

  // The player's name with each faction: everyone who has a view of them or a deed of theirs on record, the
  // highest rank first.
  const nameRows = [];
  const seen = new Set();
  for (const r of ranks) {
    const s = standings.find((x) => x.faction === r.faction);
    seen.add(r.faction);
    nameRows.push({
      id: r.faction, name: r.name || nameOf(r.faction), standing: s?.standing, status: s?.status,
      rank: r.rank, earnedRank: r.earned_rank, renown: r.renown, record: r, knownAs: r.known_as,
      titles: factionById[r.faction]?.rank_titles || [],
    });
  }
  for (const s of standings) {
    if (seen.has(s.faction) || !factionById[s.faction] || factionById[s.faction].disbanded) continue;
    nameRows.push({ id: s.faction, name: nameOf(s.faction), standing: s.standing, status: s.status, titles: factionById[s.faction].rank_titles || [] });
  }
  nameRows.sort((a, b) => (b.rank || 0) - (a.rank || 0) || (b.renown || 0) - (a.renown || 0) || Math.abs(b.standing || 0) - Math.abs(a.standing || 0));
  // Those who have neither a view of the player nor a deed of theirs on record are one line, not a card each.
  const known = nameRows.filter((row) => row.record || row.standing);
  const strangers = nameRows.filter((row) => !row.record && !row.standing);

  // Relations worth a look: anything but plain neutrality. The rest is a long list of nothing.
  const notable = relations.filter((r) => r.war || r.trade || (r.status && r.status !== 'Neutral'));
  const shownRelations = (allRelations ? relations : notable).slice().sort((a, b) => Math.abs(b.score) - Math.abs(a.score));

  const shownEvents = allEvents ? events : events.slice(0, 8);
  const civilWar = CIVIL_WAR[politics.civil_war];

  return (
    <div className="ie-page p-3 space-y-4">
      {/* The player's own name with the factions: what the ladder is for */}
      <section>
        <h2 className="section-header mb-2">{t('Your Name')}</h2>
        {known.length === 0 ? (
          <div className="text-xs text-gray-500 leading-relaxed">
            {t('Nobody has a view of you yet. Take a side when two armies meet, do a faction a service or save one of its leaders, and it will remember.')}
          </div>
        ) : (
          <div className="space-y-1.5">
            {known.map((row) => <NameCard key={row.id} row={row} needed={needed} perks={perks} />)}
          </div>
        )}
        {known.length > 0 && strangers.length > 0 && (
          <div className="text-[10px] text-gray-500 leading-snug mt-2">
            {t('No view of you yet: {names}.', { names: strangers.map((row) => row.name).join(', ') })}
          </div>
        )}
      </section>

      {/* A battle being fought, armies drawing up, the game's own war */}
      {(battle || gatherings.length > 0 || civilWar) && (
        <section>
          <h2 className="section-header mb-2">
            {t('In the Field')}
            {(battle || gatherings.length > 0) && <span className="ml-2 text-[10px] text-amber-400 font-normal">{t('(now)')}</span>}
          </h2>
          <div className="space-y-2">
            {battle && (
              <div className="slot-card rounded px-3 py-2 border-l-2 border-red-500">
                <div className="flex items-baseline justify-between gap-2 mb-1.5">
                  <span className="text-sm text-red-400 font-medium truncate">
                    {t('{a} against {b}', { a: nameOf(battle.a), b: nameOf(battle.b) })}
                  </span>
                  <span className="text-[10px] text-amber-400 shrink-0 pulse-dot">
                    {t(battle.phase === 'muster' ? 'drawn up, not yet fighting' : battle.skirmish ? 'a skirmish, now' : 'fighting now')}
                  </span>
                </div>
                {battle.place && <div className="text-[10px] text-gray-500 mb-1.5">{battle.place}</div>}
                <div className="flex gap-3">
                  <MoraleBar value={battle.morale_a} label={t('{side} · {n} standing', { side: nameOf(battle.a), n: battle.standing_a })} />
                  <MoraleBar value={battle.morale_b} label={t('{side} · {n} standing', { side: nameOf(battle.b), n: battle.standing_b })} />
                </div>
                <div className="text-[10px] text-gray-400 mt-1.5">
                  {battle.player_side
                    ? t('Your side: {side}', { side: nameOf(battle.player_side) }) + (battle.player_kills > 0 ? t(' · {n} cut down by your hand', { n: battle.player_kills }) : '')
                    : t(battle.phase === 'muster'
                      ? 'You have taken no side. Walk up to a line and its captain will call to you.'
                      : 'You have taken no side.')}
                </div>
              </div>
            )}
            {gatherings.map((g, i) => (
              <div key={i} className="slot-card rounded px-3 py-2 border-l-2 border-amber-500">
                <div className="text-xs text-gray-200">
                  {t(g.place ? 'Armies are drawing up near {place}: {a} against {b}.' : 'Armies are drawing up: {a} against {b}.', { place: g.place, a: nameOf(g.a), b: nameOf(g.b) })}
                </div>
                {g.calls && (
                  <div className="text-[11px] text-amber-300 mt-0.5">
                    {t('You are called to the line: {side}. The place is marked in your journal.', { side: nameOf(g.calls) })}
                  </div>
                )}
                <div className="text-[10px] text-gray-500 mt-0.5">
                  {t('It is decided without you in {time}. Be there before, and you may choose a side.', { time: hours(g.hours_left) })}
                </div>
              </div>
            ))}
            {civilWar && <div className="text-[11px] text-gray-400 leading-snug">{t(civilWar)}</div>}
          </div>
        </section>
      )}

      {/* Active Wars */}
      {wars.length > 0 && (
        <section>
          <h2 className="section-header mb-2">
            {t('Active Wars')}
            <span className="ml-2 text-[10px] text-red-400 font-normal">({wars.length})</span>
          </h2>
          <div className="space-y-2">
            {wars.map((w, i) => {
              const nameA = nameOf(w.faction_a);
              const nameB = nameOf(w.faction_b);
              return (
                <div key={i} className="slot-card rounded px-3 py-2 border-l-2 border-red-500">
                  <div className="flex items-center justify-between mb-2">
                    <span className="text-sm text-red-400 font-medium">
                      {t('{a} vs {b}', { a: nameA, b: nameB })}
                    </span>
                    <span className="text-[10px] text-gray-500">
                      {t('{n} battles', { n: w.battles })}
                    </span>
                  </div>
                  <div className="space-y-1.5">
                    <div className="flex gap-3">
                      <MoraleBar value={w.morale_a} label={t('{side} morale', { side: nameA })} />
                      <MoraleBar value={w.morale_b} label={t('{side} morale', { side: nameB })} />
                    </div>
                    <div className="flex gap-4 text-[10px]">
                      <div>
                        <span className="text-gray-500">{t('Strength:')}{' '}</span>
                        <span className="text-gray-300">{w.strength_a}%</span>
                        <span className="text-gray-600"> / </span>
                        <span className="text-gray-300">{w.strength_b}%</span>
                      </div>
                    </div>
                    {/* Battle History */}
                    {w.recent_battles && w.recent_battles.length > 0 && (
                      <div className="mt-1.5 pt-1.5 border-t border-white/5 space-y-1">
                        <div className="text-[10px] text-gray-500 uppercase tracking-wider">{t('Battle History')}</div>
                        {w.recent_battles.map((b, bi) => {
                          const isAttackerVictory = b.result === 'attacker_victory';
                          const isDefenderVictory = b.result === 'defender_victory';
                          const victorName = isAttackerVictory
                            ? nameOf(b.attacker)
                            : isDefenderVictory
                              ? nameOf(b.defender)
                              : null;
                          return (
                            <div key={bi} className="flex items-start gap-1.5 text-[10px]">
                              <span className={isAttackerVictory || isDefenderVictory ? 'text-red-400' : 'text-yellow-400'}>
                                {isAttackerVictory || isDefenderVictory ? '⚔' : '⚖'}
                              </span>
                              <div className="flex-1 min-w-0">
                                <span className="text-gray-300">{b.narrative || b.location}</span>
                                <span className="text-gray-600 ml-1">
                                  {victorName ? t('{side} wins', { side: victorName }) : t('Draw')}
                                  {' · '}-{b.attacker_losses}/-{b.defender_losses}
                                </span>
                                <GameTime time={b.time} calendar={calendar} />
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
        </section>
      )}

      {/* Everything that happened, newest first, in one list */}
      <section>
        <h2 className="section-header mb-2">{t('Chronicle')}</h2>
        {events.length === 0 ? (
          <div className="text-xs text-gray-500">{t('Nothing has happened between the factions yet.')}</div>
        ) : (
          <div className="space-y-1.5">
            {shownEvents.map((e, i) => {
              const [label, tone] = eventKind(e.type);
              return (
                <div key={e.id ?? i} className={`slot-card rounded px-3 py-2 border-l-2 ${tone.split(' ')[1]}`}>
                  <div className="flex items-baseline justify-between gap-2 mb-0.5">
                    <span className={`text-[10px] uppercase tracking-wider ${tone.split(' ')[0]}`}>{t(label)}</span>
                    <span className="text-[10px] text-gray-500 truncate">
                      {nameOf(e.faction_a)}
                      {e.faction_b ? ` ↔ ${nameOf(e.faction_b)}` : ''}
                      {e.delta ? <span className={`ml-1.5 font-medium ${e.delta > 0 ? 'text-green-400' : 'text-red-400'}`}>{e.delta > 0 ? '+' : ''}{e.delta}</span> : null}
                    </span>
                  </div>
                  <div className="text-xs text-gray-300 leading-relaxed">{e.description}</div>
                  <GameTime time={e.time} calendar={calendar} />
                </div>
              );
            })}
            {events.length > 8 && (
              <button type="button" className="btn-icon" onClick={() => setAllEvents(!allEvents)}>
                {allEvents ? t('Show the latest only') : t('Show all {n}', { n: events.length })}
              </button>
            )}
          </div>
        )}
      </section>

      {/* Faction Relations */}
      <section>
        <h2 className="section-header mb-2">{t('Faction Relations')}</h2>
        {relations.length === 0 ? (
          <div className="text-xs text-gray-500">{t('No faction relations yet. Wait for the first political tick.')}</div>
        ) : (
          <div className="space-y-1">
            {shownRelations.length === 0 && (
              <div className="text-xs text-gray-500">{t('Nobody is at odds and nobody is close: every faction keeps to itself for now.')}</div>
            )}
            {shownRelations.map((r, i) => (
              <div key={i} className="slot-card rounded px-3 py-1.5 flex items-center justify-between">
                <div className="flex items-center gap-2 flex-1 min-w-0">
                  <span className="text-xs text-gray-300 truncate" {...peSideProps(factionSides[r.a])}>
                    {nameOf(r.a)}
                  </span>
                  <span className="text-gray-600 text-[10px]">&harr;</span>
                  <span className="text-xs text-gray-300 truncate" {...peSideProps(factionSides[r.b])}>
                    {nameOf(r.b)}
                  </span>
                </div>
                <div className="flex items-center gap-2 ml-2 shrink-0">
                  {r.war && <span className="text-[10px] text-red-400 uppercase tracking-wider">{t('at war')}</span>}
                  {r.trade && <span className="text-[10px] text-emerald-400 uppercase tracking-wider">{t('trading')}</span>}
                  <span className={`text-[10px] ${STATUS_COLORS[r.status] || 'text-gray-500'}`}>
                    {t(r.status)}
                  </span>
                  <span className={`text-xs font-medium w-8 text-right ${STATUS_COLORS[r.status] || 'text-gray-400'}`}>
                    {r.score > 0 ? '+' : ''}{r.score}
                  </span>
                </div>
              </div>
            ))}
            {relations.length > notable.length && (
              <button type="button" className="btn-icon" onClick={() => setAllRelations(!allRelations)}>
                {allRelations ? t('Show only those that matter') : t('Show all {n} ({m} neutral)', { n: relations.length, m: relations.length - notable.length })}
              </button>
            )}
          </div>
        )}
      </section>

      {/* Factions Overview */}
      <section>
        <h2 className="section-header mb-2">{t('Factions')}</h2>
        <div className="grid grid-cols-2 gap-1">
          {factions.map((f, i) => (
            <div key={i} className={`slot-card rounded px-2 py-1.5 ${f.disbanded ? 'opacity-60' : ''}`}>
              <div className="text-xs text-gray-200 font-medium" {...peSideProps(f.pe_side)}>{f.name}</div>
              <div className="text-[10px] text-gray-500">
                {TYPE_LABELS[f.type] ? t(TYPE_LABELS[f.type]) : f.type} &middot; {f.hold}
              </div>
              {f.disbanded && <div className="text-[10px] text-red-400 mt-0.5">{t('Broken: its leaders are dead')}</div>}
              {f.leaders?.length > 0 && (
                <div className="text-[10px] text-gray-400 mt-0.5">{t('Leaders: {names}', { names: f.leaders.join(', ') })}</div>
              )}
              {f.fallen_leaders?.length > 0 && (
                <div className="text-[10px] text-gray-500 mt-0.5">{t('Fallen: {names}', { names: f.fallen_leaders.join(', ') })}</div>
              )}
              {f.bound_by && (
                <div className="text-[10px] text-sky-400/80 mt-0.5" title={t('No war with it and no attempt on its people until that story of the game is over')}>
                  {t('Held by its own story: {quest}', { quest: f.bound_by })}
                </div>
              )}
              {f.pe_owned && (
                <div className="text-[10px] text-amber-400 mt-0.5" title={t("Private: only you see this; NPCs and the AI don't")}>
                  {'\u{1F512}'} {t(f.pe_owned === 'puppet' ? 'Governor (your puppet)' : 'Vassal jarl (your vassal)')}
                </div>
              )}
            </div>
          ))}
        </div>
      </section>

      {/* The faction editor: asked for, not loaded with the tab (it reads the whole load order once) */}
      <section style={editing ? { gridColumn: '1 / -1' } : undefined}>
        <h2 className="section-header mb-2">{t('Edit factions')}</h2>
        {editing
          ? <FactionEditor sendAction={sendAction} knownNpcs={knownNpcs} />
          : <button type="button" className="btn-icon" onClick={() => setEditing(true)}>{t('Add or change factions…')}</button>}
      </section>
    </div>
  );
}

export default withSections(PoliticsTab);
