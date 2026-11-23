// The dashboard's words in the language the player reads.
//
// Every text the page shows goes through t(english, params). The English is the key, so the page reads exactly as
// before in English, and a text no table has stays English. Each language is one JSON file beside this one
// (de.json, fr.json ...): English -> translation. en.json holds only the plural forms of English.
//
//   t('Saved.')                              a plain text
//   t('Now: {line}', { line })               {name} is filled from params; a value may be a React element
//   t('{n} actions', { n: 3 })               a value that is an object in a table is a plural: its forms are
//                                            one / few / many / other, picked for params.n by the language's rule
//   tk('Walk')                               no translation: marks an English text kept in a list (labels of options,
//                                            words the DLL sends) as one the page translates where it shows it
//   tx(<span>Dashboard Hotkey</span>)        the element with its English text children translated
//
// Which language: the one the DLL reads labels in (LanguageBridge::Decide), from the settings the page already gets
// (languageOf). Words the DLL or a model wrote (names, story titles, rumors) are shown as they come.
//
// `npm run check:strings` (tools/i18n-check.js) lists every key in the sources and checks each table against it.

import { Children, cloneElement, isValidElement } from 'react';
import en from './en.json';
import de from './de.json';
import fr from './fr.json';
import es from './es.json';
import it from './it.json';
import ru from './ru.json';
import pl from './pl.json';
import ja from './ja.json';
import zh from './zh.json';
import ko from './ko.json';
import sq from './sq.json';

export const TABLES = { en, de, fr, es, it, ru, pl, ja, zh, ko, sq };

// The language packs the DLL ships (SKSE/Plugins/IntelEngine/languages/*.tsv), as their meta rows describe them:
// the pack's English name, the names and codes "Language of journal and messages" may give (meta code, codes), the
// word the game's own setting uses for it (meta game; none for a language the game does not ship) and the script
// the game's own place names are written in (meta script).
export const PACKS = [
  { code: 'de', name: 'german', names: ['de', 'deu', 'ger', 'german', 'deutsch'], game: 'german', script: 'latin' },
  { code: 'fr', name: 'french', names: ['fr', 'fra', 'fre', 'french'], game: 'french', script: 'latin' },
  { code: 'es', name: 'spanish', names: ['es', 'spa', 'spanish'], game: 'spanish', script: 'latin' },
  { code: 'it', name: 'italian', names: ['it', 'ita', 'italian'], game: 'italian', script: 'latin' },
  { code: 'ru', name: 'russian', names: ['ru', 'rus', 'russian'], game: 'russian', script: 'cyrillic' },
  { code: 'pl', name: 'polish', names: ['pl', 'pol', 'polish'], game: 'polish', script: 'latin' },
  { code: 'ja', name: 'japanese', names: ['ja', 'jpn', 'japanese'], game: 'japanese', script: 'kana' },
  { code: 'zh', name: 'chinese', names: ['zh', 'zho', 'chi', 'zh-hans', 'zh-hant', 'zh-cn', 'zh-tw', 'chinese'], game: 'chinese', script: 'han' },
  { code: 'ko', name: 'korean', names: ['ko', 'kor', 'korean'], game: '', script: 'hangul' },
  { code: 'sq', name: 'albanian', names: ['sq', 'sqi', 'alb', 'aln', 'als', 'albanian', 'gheg', 'shqip'], game: '', script: 'latin' },
];

const packNamed = word => PACKS.find(pack => pack.names.includes(word));

// The language the DLL reads labels in, from what the page gets in pluginConfig:
//   language.text     the setting: auto, english, or a pack by name or code (LanguagePack.h, TextPack)
//   language.reading  the pack the DLL decided on, by its English name, empty for English (sent by a DLL that has
//                     it; see the report of 2026-11-23: not sent yet)
//   language.now      LanguageBridge::Describe, the DLL's own line: "the game says german, its place names are written
//                     in latin; the player reads German; ..." (" (chosen)" after the name when the setting named it)
// A setting that names a pack wins, as in the DLL; at auto the DLL's decision counts. The line can be older than a
// setting changed on this page (the page keeps what it changed), so at auto a line that says "(chosen)" is read by
// the DLL's own rule for auto instead: the game's language word, else the script of the game's own place names.
export function languageOf(config) {
  const settings = config || {};
  const want = String(settings['language.text'] ?? 'auto').trim().toLowerCase();
  if (want && want !== 'auto') {
    if (want === 'english' || want === 'en') return 'en';
    return packNamed(want)?.code || 'en';
  }
  if (typeof settings['language.reading'] === 'string') {
    const reading = settings['language.reading'].trim().toLowerCase();
    return (reading && packNamed(reading)?.code) || 'en';
  }
  const line = String(settings['language.now'] || '');
  const reads = /the player reads ([^;]+?)(\s*\(chosen\))?;/i.exec(line);
  if (reads && !reads[2]) return packNamed(reads[1].trim().toLowerCase())?.code || 'en';
  const said = /the game says ([^,;]+), its place names are written in ([a-z]+)/i.exec(line);
  if (!said) return 'en';
  const game = said[1].trim().toLowerCase();
  const byGame = game !== 'english' && game !== 'nothing' && PACKS.find(pack => pack.game && pack.game === game);
  if (byGame) return byGame.code;
  const script = said[2].toLowerCase();
  if (script === 'latin') return 'en';
  const inScript = PACKS.filter(pack => pack.script === script);
  return (inScript.find(pack => pack.game) || inScript[0])?.code || 'en';
}

// Which plural form a count takes (one / few / many / other).
const PLURAL = {
  fr: n => (n === 0 || n === 1 ? 'one' : 'other'),
  ru: n => {
    const last = n % 10, lastTwo = n % 100;
    if (!Number.isInteger(n)) return 'other';
    return last === 1 && lastTwo !== 11 ? 'one' : last >= 2 && last <= 4 && (lastTwo < 12 || lastTwo > 14) ? 'few' : 'many';
  },
  pl: n => {
    const last = n % 10, lastTwo = n % 100;
    if (!Number.isInteger(n)) return 'other';
    return n === 1 ? 'one' : last >= 2 && last <= 4 && (lastTwo < 12 || lastTwo > 14) ? 'few' : 'many';
  },
  ja: () => 'other',
  zh: () => 'other',
  ko: () => 'other',
};
const pluralOf = (code, n) => (PLURAL[code] || (count => (count === 1 ? 'one' : 'other')))(n);

let current = 'en';
let table = TABLES.en;
let backwards = null;

export function setLanguage(code) {
  const next = TABLES[code] ? code : 'en';
  if (next === current) return;
  current = next;
  table = TABLES[next];
  backwards = null;
}

export const language = () => current;

function fill(text, params) {
  if (!params || typeof text !== 'string' || !text.includes('{')) return text;
  const parts = [];
  let plain = true;
  let at = 0;
  text.replace(/\{(\w+)\}/g, (whole, name, offset) => {
    if (!(name in params)) return whole;
    const value = params[name];
    parts.push(text.slice(at, offset), value);
    if (value !== null && typeof value === 'object') plain = false;
    at = offset + whole.length;
    return whole;
  });
  parts.push(text.slice(at));
  if (plain) return parts.map(part => (part === null || part === undefined ? '' : String(part))).join('');
  // A React element among the values: the pieces, each with a key of its own.
  return parts.filter(part => part !== '' && part !== null && part !== undefined)
    .map((part, i) => (isValidElement(part) && part.key === null ? cloneElement(part, { key: `t${i}` }) : part));
}

export function t(key, params) {
  let text = table[key];
  let code = current;
  if (text === undefined || text === '') {
    text = TABLES.en[key];
    code = 'en';
  }
  if (text === undefined || text === '') return fill(key, params);
  if (text !== null && typeof text === 'object') {
    const n = Number(params?.n);
    const form = pluralOf(code, n);
    text = text[form] ?? text.other ?? text.many ?? text.one ?? key;
  }
  return fill(text, params);
}

// An English text the page translates where it shows it (see the top). Gives the text back as it is.
export const tk = english => english;

// An element whose text children are English, with those children translated: tx(<span className="...">UI Scale</span>).
// For the few rows a check finds by their English markup in the source (RunSettingNamesCheck.py).
export const tx = element => cloneElement(element, {}, ...Children.toArray(element.props.children)
  .map(child => (typeof child === 'string' ? t(child) : child)));

// The English a translated text was made from (the page keys a few things, such as which card is folded, by their
// English heading): the text itself when it is English or not in the table.
export function englishOf(text) {
  if (current === 'en' || typeof text !== 'string') return text;
  if (!backwards) {
    backwards = new Map();
    for (const [english, translated] of Object.entries(table)) {
      if (typeof translated === 'string' && translated) backwards.set(translated, english);
    }
  }
  return backwards.get(text) ?? text;
}
