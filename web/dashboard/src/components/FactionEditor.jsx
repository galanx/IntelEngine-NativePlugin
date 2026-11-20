import React, { useEffect, useMemo, useRef, useState } from 'react';

// The faction editor: factions.yaml without a text editor. Everything it offers comes from the user's own load order
// (the DLL's FactionEditor::Catalog): the game's and the mods' factions, the leveled lists soldiers can be raised from,
// the people the game knows by name. Saving writes the file (the old one is kept beside it) and reads it in again.

const TYPE_LABELS = { military: 'Military', guild: 'Guild', political: 'Political' };
const STYLE_LABELS = { '': 'None', brawl: 'Brawls', sabotage: 'Sabotage', assassination: 'Assassination', proxy: 'Through others' };
const input = 'w-full bg-black/30 border border-white/10 rounded px-2 py-1 text-xs text-gray-200 placeholder-gray-600 focus:outline-none focus:border-white/30';
const chip = 'inline-flex items-center gap-1 rounded bg-white/10 px-1.5 py-0.5 text-[11px] text-gray-200';
const pill = on => `rounded px-2 py-1 text-[11px] border ${on ? 'border-amber-400/60 bg-amber-400/10 text-amber-200' : 'border-white/10 text-gray-400 hover:text-gray-200'}`;

const blank = () => ({ id: '', name: '', type: 'military', hold: 'Whiterun', skyrim_faction_id: '', leader_names: [], rivals: [], allies: [],
  base_army_strength: 20, war_threshold: -50, conflict_style: '', soldier_template: '', prison_location: '', prison_marker: [0, 0, 0], pe_side: -1, isNew: true });

// "The Silver Hand" -> "TheSilverHandFaction": what the file calls a faction, made from its name until the user says otherwise.
const idFromName = name => { const words = name.replace(/[^A-Za-z0-9 ]/g, ' ').split(' ').filter(Boolean);
  const joined = words.map(w => w[0].toUpperCase() + w.slice(1)).join(''); return joined ? (/faction$/i.test(joined) ? joined : joined + 'Faction') : ''; };

function Field({ label, hint, children }) {
  return (<div className="mb-2"><div className="text-[11px] text-gray-300 mb-0.5">{label}</div>{children}
    {hint && <div className="text-[10px] text-gray-500 leading-tight mt-0.5">{hint}</div>}</div>);
}

// A text field that offers what the load order has: type any part of a name, an id or a plugin's name.
function Suggest({ value, onChange, options, placeholder, onPick, clearOnPick }) {
  const [text, setText] = useState(value || '');
  const [open, setOpen] = useState(false);
  const [at, setAt] = useState(0);
  const box = useRef(null);
  useEffect(() => { setText(value || ''); }, [value]);
  useEffect(() => { if (!open) return; const away = e => { if (!box.current?.contains(e.target)) setOpen(false); };
    document.addEventListener('mousedown', away); return () => document.removeEventListener('mousedown', away); }, [open]);
  const found = useMemo(() => { const q = text.trim().toLowerCase(); const out = [];
    for (const o of options) { if (out.length >= 40) break;
      if (!q || o.id.toLowerCase().includes(q) || (o.name || '').toLowerCase().includes(q) || (o.plugin || '').toLowerCase().includes(q)) out.push(o); }
    return out; }, [text, options]);
  const pick = o => { if (onPick) onPick(o.id); else onChange(o.id); setText(clearOnPick ? '' : o.id); setOpen(false); };
  return (<div ref={box} className="relative">
    <input className={input} value={text} placeholder={placeholder} spellCheck={false}
      onFocus={() => setOpen(true)}
      onChange={e => { setText(e.target.value); setAt(0); setOpen(true); if (!onPick) onChange(e.target.value); }}
      onKeyDown={e => { if (e.key === 'ArrowDown') { e.preventDefault(); setAt(a => Math.min(a + 1, found.length - 1)); }
        else if (e.key === 'ArrowUp') { e.preventDefault(); setAt(a => Math.max(a - 1, 0)); }
        else if (e.key === 'Enter') { e.preventDefault(); if (open && found[at]) pick(found[at]); else if (onPick && text.trim()) { onPick(text.trim()); setText(''); } }
        else if (e.key === 'Escape' && open) { e.stopPropagation(); setOpen(false); } }} />
    {open && found.length > 0 && (<div className="absolute z-20 left-0 right-0 mt-0.5 max-h-40 overflow-y-auto rounded border border-white/10 bg-[#15171c] shadow-lg">
      {found.map((o, i) => (<div key={o.id + i} onMouseDown={e => { e.preventDefault(); pick(o); }} onMouseEnter={() => setAt(i)}
        className={`px-2 py-1 text-[11px] cursor-pointer ${i === at ? 'bg-white/10 text-gray-100' : 'text-gray-300'}`}>
        <span>{o.name || o.id}</span>{o.name && <span className="text-gray-500"> {'·'} {o.id}</span>}{o.plugin && <span className="text-gray-600"> {'·'} {o.plugin}</span>}
      </div>))}
    </div>)}
  </div>);
}

function Chips({ values, onChange, options, placeholder }) {
  return (<div>
    {values.length > 0 && (<div className="flex flex-wrap gap-1 mb-1">{values.map(v => (<span key={v} className={chip}>{v}
      <button type="button" className="text-gray-400 hover:text-red-400" title="Remove" onClick={() => onChange(values.filter(x => x !== v))}>{'×'}</button></span>))}</div>)}
    <Suggest value="" options={options} placeholder={placeholder} clearOnPick onChange={() => {}} onPick={v => { if (v && !values.includes(v)) onChange([...values, v]); }} />
  </div>);
}

export default function FactionEditor({ sendAction, knownNpcs = [] }) {
  const [catalog, setCatalog] = useState(null);
  const [factions, setFactions] = useState(null);
  const [selected, setSelected] = useState(0);
  const [status, setStatus] = useState(null);
  const [dirty, setDirty] = useState(false);
  const [busy, setBusy] = useState(true);

  useEffect(() => {
    window.factionEditorResult = json => {
      let data; try { data = typeof json === 'string' ? JSON.parse(json) : json; } catch { return; }
      setBusy(false);
      if (data.do === 'open') { setCatalog(data.catalog || {}); setFactions(data.factions || []); setDirty(false); return; }
      setStatus({ ok: !!data.ok, error: data.error || '', warnings: data.warnings || [] });
      if (data.ok) { setFactions(data.factions || []); setDirty(false); }
    };
    sendAction('factionEditor', { do: 'open' });
    return () => { delete window.factionEditorResult; };
  }, [sendAction]);

  const npcOptions = useMemo(() => knownNpcs.map(name => ({ id: name })), [knownNpcs]);
  if (!factions || !catalog) return <div className="text-xs text-gray-500 py-2">Reading your load order{'…'}</div>;

  const current = factions[selected];
  const change = patch => { setFactions(list => list.map((f, i) => i === selected ? { ...f, ...patch } : f)); setDirty(true); setStatus(null); };
  const rename = name => change(current.isNew && !current.idTouched ? { name, id: idFromName(name) } : { name });
  const add = () => { setFactions(list => [...list, blank()]); setSelected(factions.length); setDirty(true); setStatus(null); };
  const remove = () => { const gone = current.id; setFactions(list => list.filter((_, i) => i !== selected)
      .map(f => ({ ...f, rivals: f.rivals.filter(x => x !== gone), allies: f.allies.filter(x => x !== gone) })));
    setSelected(s => Math.max(0, s - 1)); setDirty(true); setStatus(null); };
  const save = () => { setBusy(true); setStatus(null);
    sendAction('factionEditor', { do: 'save', factions: factions.map(({ isNew, idTouched, ...f }) => f) }); };
  const discard = () => { setBusy(true); setStatus(null); sendAction('factionEditor', { do: 'open' }); };
  const others = factions.filter((_, i) => i !== selected && factions[i].id);
  const toggle = (key, id) => change({ [key]: current[key].includes(id) ? current[key].filter(x => x !== id) : [...current[key], id],
    ...(key === 'rivals' ? { allies: current.allies.filter(x => x !== id) } : { rivals: current.rivals.filter(x => x !== id) }) });
  const gameFaction = current && catalog.gameFactions?.find(g => g.id === current.skyrim_faction_id);

  return (<div className="faction-editor">
    <div className="flex items-center gap-2 mb-2">
      <button type="button" className={pill(dirty)} disabled={!dirty || busy} onClick={save}>{busy ? 'Working…' : 'Save'}</button>
      <button type="button" className={pill(false)} disabled={!dirty || busy} onClick={discard}>Discard changes</button>
      <span className="text-[10px] text-gray-500">{dirty ? 'Not saved yet.' : 'Saved factions are in use at once. The old file is kept as factions.yaml.bak.'}</span>
    </div>
    {status && !status.ok && <div className="text-[11px] text-red-400 mb-2">Not saved: {status.error}</div>}
    {status && status.ok && <div className="text-[11px] text-green-400 mb-2">Saved.</div>}
    {status && status.warnings.map((w, i) => <div key={i} className="text-[11px] text-yellow-400 mb-1">{w}</div>)}

    <div className="flex gap-2">
      <div className="w-56 shrink-0 space-y-1">
        {factions.map((f, i) => (<div key={i} onClick={() => setSelected(i)}
          className={`slot-card rounded px-2 py-1 cursor-pointer ${i === selected ? 'border-amber-400/50' : ''}`}>
          <div className="text-xs text-gray-200 truncate">{f.name || 'New faction'}</div>
          <div className="text-[10px] text-gray-500 truncate">{TYPE_LABELS[f.type] || f.type} {'·'} {f.hold}</div>
        </div>))}
        <button type="button" className={pill(false) + ' w-full'} onClick={add}>+ Add a faction</button>
      </div>

      {current && (<div className="flex-1 min-w-0 grid grid-cols-1 lg:grid-cols-2 gap-x-4 content-start">
        <Field label="Name">
          <input className={input} value={current.name} placeholder="The Silver Hand" onChange={e => rename(e.target.value)} />
        </Field>
        <Field label="Id" hint={current.isNew ? 'What the file and other factions call it. Made from the name; change it only if you need to.' : 'Fixed once saved: relations and history are kept under it.'}>
          <input className={input} value={current.id} disabled={!current.isNew} spellCheck={false}
            onChange={e => change({ id: e.target.value.replace(/[^A-Za-z0-9_]/g, ''), idTouched: true })} />
        </Field>
        <Field label="Kind">
          <div className="flex gap-1">{(catalog.types || []).map(t => <button type="button" key={t} className={pill(current.type === t)} onClick={() => change({ type: t })}>{TYPE_LABELS[t] || t}</button>)}</div>
        </Field>
        <Field label="Seat" hint="The city it is at home in.">
          <div className="flex flex-wrap gap-1">{(catalog.holds || []).map(h => <button type="button" key={h} className={pill(current.hold === h)} onClick={() => change({ hold: h })}>{h}</button>)}</div>
        </Field>
        <Field label="Faction of the game" hint={gameFaction ? `${gameFaction.name || gameFaction.id} (${gameFaction.plugin}). Its members count as this faction's people.`
            : 'Type part of a name or a mod: every faction of your load order is offered. Its members count as this faction’s people. May stay empty.'}>
          <Suggest value={current.skyrim_faction_id} options={catalog.gameFactions || []} placeholder="Search your load order…" onChange={v => change({ skyrim_faction_id: v })} />
        </Field>
        <Field label="Leaders" hint="People of the game, by name. A faction whose leaders are all dead is treated as gone.">
          <Chips values={current.leader_names} options={npcOptions} placeholder="Add a leader…" onChange={v => change({ leader_names: v })} />
        </Field>
        <Field label="Soldiers" hint="The list its soldiers are raised from in a battle: every leveled list of people in your load order is offered. Empty: its first ally's soldiers.">
          <Suggest value={current.soldier_template} options={catalog.templates || []} placeholder="Search your load order…" onChange={v => change({ soldier_template: v })} />
        </Field>
        <div className="flex gap-2">
          <div className="flex-1"><Field label="Army strength" hint="0 to 500, as the dungeon master weighs it.">
            <input className={input} type="number" min="0" max="500" value={current.base_army_strength} onChange={e => change({ base_army_strength: Number(e.target.value) || 0 })} /></Field></div>
          <div className="flex-1"><Field label="War below" hint="Relation at which it is at war (-100 to 0).">
            <input className={input} type="number" min="-100" max="0" value={current.war_threshold} onChange={e => change({ war_threshold: Number(e.target.value) || 0 })} /></Field></div>
        </div>
        <Field label="How it fights when it is not at war">
          <div className="flex flex-wrap gap-1">{(catalog.styles || []).map(s => <button type="button" key={s} className={pill((current.conflict_style || '') === s)} onClick={() => change({ conflict_style: s })}>{STYLE_LABELS[s] ?? s}</button>)}</div>
        </Field>
        {others.length > 0 && (<>
          <Field label="Rivals"><div className="flex flex-wrap gap-1">{others.map(o => <button type="button" key={o.id} className={pill(current.rivals.includes(o.id))} onClick={() => toggle('rivals', o.id)}>{o.name || o.id}</button>)}</div></Field>
          <Field label="Allies"><div className="flex flex-wrap gap-1">{others.map(o => <button type="button" key={o.id} className={pill(current.allies.includes(o.id))} onClick={() => toggle('allies', o.id)}>{o.name || o.id}</button>)}</div></Field>
        </>)}
        <Field label="Free once these quests of the game are finished" hint={'Quest editor ids (C06, TG09…). Until then it goes to no war and nobody of it is harmed. Left alone, the game’s own guilds wait for their own questlines; add a line here to say otherwise for this faction.'}>
          <Chips values={current.free_after || []} options={[]} placeholder="Add a quest id and press Enter…" onChange={v => change({ free_after: v })} />
          {current.free_after && <button type="button" className="text-[10px] text-gray-500 hover:text-gray-300 mt-0.5" onClick={() => { const { free_after, ...rest } = current; setFactions(list => list.map((f, i) => i === selected ? rest : f)); setDirty(true); }}>Use the game's own rule again</button>}
        </Field>
        <div><button type="button" className="text-[11px] text-gray-500 hover:text-red-400 mt-1" onClick={remove}>Remove this faction</button></div>
      </div>)}
    </div>
  </div>);
}
