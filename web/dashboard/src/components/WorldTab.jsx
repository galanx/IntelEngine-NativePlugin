import withSections from './Section';
import React from 'react';

// The World page: what the player has heard, what they have taken on, and their people. It shows what the player
// knows. What has not reached them yet (talk in other holds, what a parted companion has made of their life, stories
// on their way) stays hidden unless they ask for it: finding those out is the game.

const RUMOR_KINDS = {
  deed: ['What was done', 'text-emerald-400 border-emerald-500/60'],
  trouble: ['Someone needs help', 'text-amber-400 border-amber-500/60'],
  life: ['An old companion', 'text-sky-400 border-sky-500/60'],
  news: ['News of the land', 'text-red-400 border-red-500/60'],
};

const STORY_KINDS = {
  keepsake: 'something lost',
  bounty: 'a bounty',
  venture: 'a venture',
  request: 'a request',
  theft: 'a theft',
  feud: 'bad blood',
  murder: 'a death',
  tale: 'a story',
  story: 'a story',
};

const plural = (n, one, many) => `${n} ${n === 1 ? one : many}`;

function age(days) {
  if (!Number.isFinite(days)) return '';
  if (days < 1 / 24) return 'just now';
  if (days < 1) return `${plural(Math.max(1, Math.round(days * 24)), 'hour', 'hours')} ago`;
  return `${plural(Math.round(days), 'day', 'days')} ago`;
}

function left(days) {
  if (!Number.isFinite(days)) return null;
  if (days <= 0) return 'time is up';
  if (days < 1) return `${plural(Math.max(1, Math.round(days * 24)), 'hour', 'hours')} left`;
  return `${plural(Math.round(days), 'day', 'days')} left`;
}

// "asked X to bring them ...; still open (2 days ago)": what was asked, as a sentence. How it stands is said below it.
function askLine(line) {
  const text = String(line).replace(/; still open/, '').replace(/; .+ has done it but not come back yet/, '');
  return text.charAt(0).toUpperCase() + text.slice(1);
}

function list(names) {
  if (names.length <= 1) return names.join('');
  return `${names.slice(0, -1).join(', ')} and ${names[names.length - 1]}`;
}

function WorldTab({ world }) {
  const [everything, setEverything] = React.useState(() => { try { return localStorage.getItem('intelengine.world.everything') === 'yes'; } catch { return false; } });
  const toggle = () => setEverything((was) => { try { localStorage.setItem('intelengine.world.everything', was ? 'no' : 'yes'); } catch {} return !was; });

  if (!world) {
    return (
      <div className="p-3">
        <div className="empty-state">Nothing to show yet. Open the dashboard in-game to see live data.</div>
      </div>
    );
  }

  // The engine's lines name the player; the page speaks to them.
  const you = (text) => (world.player ? String(text).split(world.player).join('you') : String(text));
  const rumors = (world.rumors || []).slice().sort((a, b) => a.days - b.days);
  const heard = rumors.filter((r) => r.told > 0);
  const unheardHere = rumors.filter((r) => !(r.told > 0) && r.here);
  const elsewhere = rumors.filter((r) => !(r.told > 0) && !r.here);
  const shownRumors = everything ? rumors : heard;

  const stories = world.stories?.open || [];
  const onTheirWay = world.stories?.planned || 0;
  const quests = world.quests || [];
  const asks = world.asks || [];
  const taken = [
    ...stories.map((s) => ({ ...s, key: `s${s.id}` })),
    ...quests.map((q) => ({ ...q, key: `q${q.id}` })),
  ];
  const people = (world.people || []).slice().sort((a, b) => Number(b.with_you) - Number(a.with_you) || a.name.localeCompare(b.name));

  return (
    <div className="ie-page p-3 space-y-4">
      <div className="flex items-center justify-between gap-3 px-1" style={{ gridColumn: '1 / -1' }}>
        <span className="text-[11px] text-gray-500 leading-snug">
          {everything
            ? 'Showing everything the world holds, heard or not.'
            : 'Showing what you have heard and taken on. The rest is for you to find out.'}
        </span>
        <button type="button" className="ie-btn" aria-pressed={everything} onClick={toggle}>
          {everything ? 'Hide what I have not heard' : 'Show everything (spoilers)'}
        </button>
      </div>

      {/* Talk of the day */}
      <section>
        <h2 className="section-header mb-2">
          Talk of the Day
          {heard.length > 0 && <span className="ml-2 text-[10px] text-gray-500 font-normal">({heard.length} heard)</span>}
        </h2>
        <div className="space-y-1.5">
          {shownRumors.length === 0 && (
            <div className="text-xs text-gray-500 leading-relaxed">
              Nobody has told you anything yet. Innkeepers hear everything; so do those who serve at their tables, bards and merchants.
            </div>
          )}
          {shownRumors.map((r) => {
            const [label, tone] = RUMOR_KINDS[r.kind] || RUMOR_KINDS.deed;
            const told = r.told > 0;
            return (
              <div key={r.id} className={`slot-card rounded px-3 py-2 border-l-2 ${tone.split(' ')[1]} ${told ? '' : 'opacity-70'}`}>
                <div className="flex items-baseline justify-between gap-2 mb-0.5">
                  <span className={`text-[10px] uppercase tracking-wider ${tone.split(' ')[0]}`}>{label}</span>
                  <span className="text-[10px] text-gray-500 shrink-0">{age(r.days)}</span>
                </div>
                <div className="text-xs text-gray-300 leading-relaxed">{r.text}</div>
                <div className="text-[10px] text-gray-500 mt-1">
                  {r.heard_in?.length > 0 ? `Talked of in ${list(r.heard_in)}` : `From ${r.from}`}
                  {r.lead ? ` · ${r.lead} is the one to see` : ''}
                  {told ? '' : r.here ? ' · going round here, not told to you yet' : ' · has not reached you'}
                </div>
              </div>
            );
          })}
          {!everything && unheardHere.length > 0 && (
            <div className="text-[11px] text-amber-400/90 leading-snug">
              {unheardHere.length === 1 ? 'There is more being talked of' : `${unheardHere.length} more things are being talked of`} where you are. Ask at the inn.
            </div>
          )}
          {!everything && elsewhere.length > 0 && (
            <div className="text-[11px] text-gray-500 leading-snug">
              Other holds have their own talk: {plural(elsewhere.length, 'thing', 'things')} that nobody here has heard.
            </div>
          )}
        </div>
      </section>

      {/* What the player has taken on */}
      <section>
        <h2 className="section-header mb-2">
          Your Stories
          {taken.length + asks.length > 0 && <span className="ml-2 text-[10px] text-amber-400 font-normal">({taken.length + asks.length} open)</span>}
        </h2>
        <div className="space-y-1.5">
          {taken.length === 0 && asks.length === 0 && (
            <div className="text-xs text-gray-500 leading-relaxed">
              You have taken nothing on. People with a trouble of their own come to those they know, and the talk of the day says who needs help.
            </div>
          )}
          {taken.map((s) => (
            <div key={s.key} className="slot-card rounded px-3 py-2 border-l-2 border-amber-400">
              <div className="flex items-baseline justify-between gap-2">
                <span className="text-sm text-gray-100 font-medium truncate">{s.title || 'A story'}</span>
                {left(s.days_left) && (
                  <span className={`text-[10px] shrink-0 ${s.days_left < 1 ? 'text-red-400' : 'text-gray-500'}`}>{left(s.days_left)}</span>
                )}
              </div>
              <div className="text-[10px] text-gray-500 mt-0.5">
                {STORY_KINDS[s.kind] || s.kind}
                {s.who ? ` · for ${s.who}` : ''}
                {s.where ? ` · ${s.where}` : ''}
                {s.gold > 0 ? ` · ${s.gold} gold offered` : ''}
              </div>
              {s.log && <div className="text-xs text-gray-300 leading-relaxed italic mt-1.5 whitespace-pre-line">{s.log}</div>}
            </div>
          ))}
          {asks.map((a) => (
            <div key={`a${a.id}`} className="slot-card rounded px-3 py-2 border-l-2 border-sky-400">
              <div className="flex items-baseline justify-between gap-2">
                <span className="text-sm text-gray-100 font-medium truncate">{a.who || 'Someone'}</span>
                {left(a.days_left) && (
                  <span className={`text-[10px] shrink-0 ${a.days_left < 1 ? 'text-red-400' : 'text-gray-500'}`}>{left(a.days_left)}</span>
                )}
              </div>
              {a.line && <div className="text-xs text-gray-300 leading-relaxed mt-0.5">{askLine(you(a.line))}</div>}
              <div className="text-[10px] text-gray-500 mt-0.5">
                {a.done ? 'Done: go back and tell them' : 'Still open'}
                {a.gold > 0 ? ` · ${a.gold} gold offered` : ''}
              </div>
            </div>
          ))}
          {everything && onTheirWay > 0 && (
            <div className="text-[11px] text-gray-500 leading-snug">
              {onTheirWay === 1 ? 'One more story is on its way to you.' : `${onTheirWay} more stories are on their way to you.`}
            </div>
          )}
        </div>
      </section>

      {/* Those who follow the player, or once did */}
      <section>
        <h2 className="section-header mb-2">Your People</h2>
        <div className="space-y-1">
          {people.length === 0 && (
            <div className="text-xs text-gray-500 leading-relaxed">
              Nobody has travelled with you yet. Those who do are remembered here, also after you part ways: they go on living.
            </div>
          )}
          {people.map((p, i) => {
            // "parted from X at Y three days ago; has found work at Z": the parting the player was there for, what
            // came after they were not.
            const [parting, since] = you(p.line || '').split('; ');
            const state = p.dead ? 'Dead' : p.with_you ? 'With you' : p.waiting ? 'Waiting for you' : 'Living their own life';
            const tone = p.dead ? 'text-gray-500' : p.with_you ? 'text-emerald-400' : p.waiting ? 'text-amber-400' : 'text-sky-400';
            return (
              <div key={i} className={`slot-card rounded px-3 py-1.5 ${p.dead ? 'opacity-60' : ''}`}>
                <div className="flex items-baseline justify-between gap-2">
                  <span className="text-xs text-gray-200 font-medium truncate">{p.name}</span>
                  <span className={`text-[10px] shrink-0 ${tone}`}>{state}</span>
                </div>
                {parting && <div className="text-[10px] text-gray-400 mt-0.5">{parting.charAt(0).toUpperCase() + parting.slice(1)}</div>}
                {since && (everything
                  ? <div className="text-[10px] text-gray-400">{since.charAt(0).toUpperCase() + since.slice(1)}</div>
                  : <div className="text-[10px] text-gray-600">What they have done since is theirs to tell, or the talk of the day's.</div>)}
              </div>
            );
          })}
        </div>
      </section>
    </div>
  );
}

export default withSections(WorldTab);
