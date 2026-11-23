import withSections from './Section';
import React from 'react';
import { t, tk } from '../i18n';

// The World page: what the player has heard, what they have taken on, and their people. It shows what the player
// knows. What has not reached them yet (talk in other holds, what a parted companion has made of their life, stories
// on their way) stays hidden unless they ask for it: finding those out is the game.

const RUMOR_KINDS = {
  deed: [tk('What was done'), 'text-emerald-400 border-emerald-500/60'],
  trouble: [tk('Someone needs help'), 'text-amber-400 border-amber-500/60'],
  life: [tk('An old companion'), 'text-sky-400 border-sky-500/60'],
  news: [tk('News of the land'), 'text-red-400 border-red-500/60'],
};

const STORY_KINDS = {
  keepsake: tk('something lost'),
  bounty: tk('a bounty'),
  venture: tk('a venture'),
  request: tk('a request'),
  theft: tk('a theft'),
  feud: tk('bad blood'),
  murder: tk('a death'),
  tale: tk('a story'),
  story: tk('a story'),
};

function age(days) {
  if (!Number.isFinite(days)) return '';
  if (days < 1 / 24) return t('just now');
  if (days < 1) return t('{n} hours ago', { n: Math.max(1, Math.round(days * 24)) });
  return t('{n} days ago', { n: Math.round(days) });
}

function left(days) {
  if (!Number.isFinite(days)) return null;
  if (days <= 0) return t('time is up');
  if (days < 1) return t('{n} hours left', { n: Math.max(1, Math.round(days * 24)) });
  return t('{n} days left', { n: Math.round(days) });
}

// "asked X to bring them ...; still open (2 days ago)": what was asked, as a sentence. How it stands is said below it.
function askLine(line) {
  const text = String(line).replace(/; still open/, '').replace(/; .+ has done it but not come back yet/, '');
  return text.charAt(0).toUpperCase() + text.slice(1);
}

function list(names) {
  if (names.length <= 1) return names.join('');
  return t('{list} and {last}', { list: names.slice(0, -1).join(', '), last: names[names.length - 1] });
}

function WorldTab({ world }) {
  const [everything, setEverything] = React.useState(() => { try { return localStorage.getItem('intelengine.world.everything') === 'yes'; } catch { return false; } });
  const toggle = () => setEverything((was) => { try { localStorage.setItem('intelengine.world.everything', was ? 'no' : 'yes'); } catch {} return !was; });

  if (!world) {
    return (
      <div className="p-3">
        <div className="empty-state">{t('Nothing to show yet. Open the dashboard in-game to see live data.')}</div>
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
  // Stories planned and not offered yet: `planned` counts all of them, those whose trouble already goes round as talk in
  // town among them. A DLL that also sends `talk` (how many of the planned wait as talk) lets the two be told apart.
  const planned = world.stories?.planned || 0;
  const asTalk = Number.isInteger(world.stories?.talk) ? Math.min(world.stories.talk, planned) : null;
  const quests = world.quests || [];
  const asks = world.asks || [];
  const taken = [
    ...stories.map((s) => ({ ...s, key: `s${s.id}` })),
    ...quests.map((q) => ({ ...q, key: `q${q.id}` })),
  ];
  const people = (world.people || []).slice().sort((a, b) => Number(b.with_you) - Number(a.with_you) || a.name.localeCompare(b.name));
  // How people stand toward the player where IntelEngine reads it from talk (the DLL sends them closest first).
  const bonds = world.bonds || [];

  return (
    <div className="ie-page p-3 space-y-4">
      <div className="flex items-center justify-between gap-3 px-1" style={{ gridColumn: '1 / -1' }}>
        <span className="text-[11px] text-gray-500 leading-snug">
          {t(everything
            ? 'Showing everything the world holds, heard or not.'
            : 'Showing what you have heard and taken on. The rest is for you to find out.')}
        </span>
        <button type="button" className="ie-btn" aria-pressed={everything} onClick={toggle}>
          {t(everything ? 'Hide what I have not heard' : 'Show everything (spoilers)')}
        </button>
      </div>

      {/* Talk of the day */}
      <section>
        <h2 className="section-header mb-2">
          {t('Talk of the Day')}
          {heard.length > 0 && <span className="ml-2 text-[10px] text-gray-500 font-normal">{t('({n} heard)', { n: heard.length })}</span>}
        </h2>
        <div className="space-y-1.5">
          {shownRumors.length === 0 && (
            <div className="text-xs text-gray-500 leading-relaxed">
              {t('Nobody has told you anything yet. Innkeepers hear everything; so do those who serve at their tables, bards and merchants.')}
            </div>
          )}
          {shownRumors.map((r) => {
            const [label, tone] = RUMOR_KINDS[r.kind] || RUMOR_KINDS.deed;
            const told = r.told > 0;
            return (
              <div key={r.id} className={`slot-card rounded px-3 py-2 border-l-2 ${tone.split(' ')[1]} ${told ? '' : 'opacity-70'}`}>
                <div className="flex items-baseline justify-between gap-2 mb-0.5">
                  <span className={`text-[10px] uppercase tracking-wider ${tone.split(' ')[0]}`}>{t(label)}</span>
                  <span className="text-[10px] text-gray-500 shrink-0">{age(r.days)}</span>
                </div>
                <div className="text-xs text-gray-300 leading-relaxed">{r.text}</div>
                <div className="text-[10px] text-gray-500 mt-1">
                  {r.heard_in?.length > 0 ? t('Talked of in {places}', { places: list(r.heard_in) }) : t('From {place}', { place: r.from })}
                  {r.lead ? t(' · {name} is the one to see', { name: r.lead }) : ''}
                  {told ? '' : t(r.here ? ' · going round here, not told to you yet' : ' · has not reached you')}
                </div>
              </div>
            );
          })}
          {!everything && unheardHere.length > 0 && (
            <div className="text-[11px] text-amber-400/90 leading-snug">
              {unheardHere.length === 1 ? t('There is more being talked of where you are. Ask at the inn.') : t('{n} more things are being talked of where you are. Ask at the inn.', { n: unheardHere.length })}
            </div>
          )}
          {!everything && elsewhere.length > 0 && (
            <div className="text-[11px] text-gray-500 leading-snug">
              {t('Other holds have their own talk: {n} things that nobody here has heard.', { n: elsewhere.length })}
            </div>
          )}
        </div>
      </section>

      {/* What the player has taken on */}
      <section>
        <h2 className="section-header mb-2">
          {t('Your Stories')}
          {taken.length + asks.length > 0 && <span className="ml-2 text-[10px] text-amber-400 font-normal">{t('({n} open)', { n: taken.length + asks.length })}</span>}
        </h2>
        <div className="space-y-1.5">
          {taken.length === 0 && asks.length === 0 && (
            <div className="text-xs text-gray-500 leading-relaxed">
              {t('You have taken nothing on. People with a trouble of their own come to those they know, and the talk of the day says who needs help.')}
            </div>
          )}
          {taken.map((s) => (
            <div key={s.key} className="slot-card rounded px-3 py-2 border-l-2 border-amber-400">
              <div className="flex items-baseline justify-between gap-2">
                <span className="text-sm text-gray-100 font-medium truncate">{s.title || t('A story')}</span>
                {left(s.days_left) && (
                  <span className={`text-[10px] shrink-0 ${s.days_left < 1 ? 'text-red-400' : 'text-gray-500'}`}>{left(s.days_left)}</span>
                )}
              </div>
              <div className="text-[10px] text-gray-500 mt-0.5">
                {STORY_KINDS[s.kind] ? t(STORY_KINDS[s.kind]) : s.kind}
                {s.who ? t(' · for {name}', { name: s.who }) : ''}
                {s.where ? ` · ${s.where}` : ''}
                {s.gold > 0 ? t(' · {n} gold offered', { n: s.gold }) : ''}
              </div>
              {s.log && <div className="text-xs text-gray-300 leading-relaxed italic mt-1.5 whitespace-pre-line">{s.log}</div>}
            </div>
          ))}
          {asks.map((a) => (
            <div key={`a${a.id}`} className="slot-card rounded px-3 py-2 border-l-2 border-sky-400">
              <div className="flex items-baseline justify-between gap-2">
                <span className="text-sm text-gray-100 font-medium truncate">{a.who || t('Someone')}</span>
                {left(a.days_left) && (
                  <span className={`text-[10px] shrink-0 ${a.days_left < 1 ? 'text-red-400' : 'text-gray-500'}`}>{left(a.days_left)}</span>
                )}
              </div>
              {a.line && <div className="text-xs text-gray-300 leading-relaxed mt-0.5">{askLine(you(a.line))}</div>}
              <div className="text-[10px] text-gray-500 mt-0.5">
                {t(a.done ? 'Done: go back and tell them' : 'Still open')}
                {a.gold > 0 ? t(' · {n} gold offered', { n: a.gold }) : ''}
              </div>
            </div>
          ))}
          {everything && planned > 0 && asTalk === null && (
            <div className="text-[11px] text-gray-500 leading-snug">
              {planned === 1
                ? t('One more story is in the making: talked of in its town, or on its way to you.')
                : t('{n} more stories are in the making: talked of in their towns, or on their way to you.', { n: planned })}
            </div>
          )}
          {everything && asTalk > 0 && (
            <div className="text-[11px] text-gray-500 leading-snug">
              {asTalk === 1
                ? t('One more story is talked of in its town, waiting for you.')
                : t('{n} more stories are talked of in their towns, waiting for you.', { n: asTalk })}
            </div>
          )}
          {everything && asTalk !== null && planned - asTalk > 0 && (
            <div className="text-[11px] text-gray-500 leading-snug">
              {planned - asTalk === 1 ? t('One more story is on its way to you.') : t('{n} more stories are on their way to you.', { n: planned - asTalk })}
            </div>
          )}
        </div>
      </section>

      {/* Those who follow the player, or once did */}
      <section>
        <h2 className="section-header mb-2">{t('Your People')}</h2>
        <div className="space-y-1">
          {people.length === 0 && (
            <div className="text-xs text-gray-500 leading-relaxed">
              {t('Nobody has travelled with you yet. Those who do are remembered here, also after you part ways: they go on living.')}
            </div>
          )}
          {people.map((p, i) => {
            // "parted from X at Y three days ago; has found work at Z": the parting the player was there for, what
            // came after they were not.
            const [parting, since] = you(p.line || '').split('; ');
            const state = t(p.dead ? 'Dead' : p.with_you ? 'With you' : p.waiting ? 'Waiting for you' : 'Living their own life');
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
                  : <div className="text-[10px] text-gray-600">{t("What they have done since is theirs to tell, or the talk of the day's.")}</div>)}
              </div>
            );
          })}
        </div>
      </section>

      {/* How the people the player has talked with stand toward them, as read from what passed between the two */}
      <section>
        <h2 className="section-header mb-2">{t('How People Stand With You')}</h2>
        <div className="space-y-1">
          {bonds.length === 0 && (
            <div className="text-xs text-gray-500 leading-relaxed">
              {t('Nobody yet. Once someone has spoken with you a few times, how they stand toward you is read from what passed between you: an acquaintance, a friend, a close friend, or someone who has turned cold.')}
            </div>
          )}
          {bonds.map((b, i) => (
            <div key={i} className="slot-card rounded px-3 py-1.5">
              <div className="flex items-baseline justify-between gap-2">
                <span className="text-xs text-gray-200 font-medium truncate">{b.name}</span>
                <span className={`text-[10px] shrink-0 ${b.sour ? 'text-red-400' : b.standing >= 2 ? 'text-emerald-400' : 'text-gray-400'}`}>{t(b.word)}</span>
              </div>
              <div className="text-[10px] text-gray-500 mt-0.5">{t(b.source)}</div>
              {b.heart && (everything
                ? <div className="text-[10px] text-gray-400">{t(b.heart).charAt(0).toUpperCase() + t(b.heart).slice(1)}</div>
                : <div className="text-[10px] text-gray-600">{t('What is in their heart is theirs to show.')}</div>)}
            </div>
          ))}
        </div>
      </section>
    </div>
  );
}

export default withSections(WorldTab);
