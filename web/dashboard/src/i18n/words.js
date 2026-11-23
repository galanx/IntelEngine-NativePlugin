// Words the DLL and the scripts send in English that the page translates where it shows them (anything else they
// send, such as names, story titles and rumors, is shown as it comes). They are listed here for
// tools/i18n-check.js, which gathers every text marked with tk() or passed to t(); the page itself does not import
// this file. A word added on the DLL's side is added here and to the tables.
import { tk } from './index';

export const WORDS = [
  // How two factions, or a faction and the player, stand (FactionPolitics.cpp, GetRelationStatus)
  tk('Alliance'), tk('Friendly'), tk('Neutral'), tk('Tense'), tk('Hostile'), tk('Critical'), tk('War'),
  // Rank titles (Ranks.h, Title): an army's, a guild's, a house's
  tk('Auxiliary'), tk('Sworn Sword'), tk('Veteran'), tk('Captain'), tk('Champion'),
  tk('Associate'), tk('Trusted Hand'), tk('Lieutenant'), tk('Right Hand'),
  tk('Hired Blade'), tk('Retainer'), tk('Trusted Retainer'), tk('Sworn Captain'), tk('Champion of the House'),
  // How someone stands toward the player, and where that was read (BondBridge.cpp, DashboardJson)
  tk('A foe'), tk('Cold toward you'), tk('Lovers'), tk('A close friend'), tk('A friend'), tk('An acquaintance'),
  tk('Read from what passed between you'),
  tk('Read from what passed between you, until SkyrimNet Relationships has caught up'),
  tk('Read from what passed between you (SkyrimNet Relationships does not watch them)'),
  tk('drawn to you, and welcome'), tk('drawn to you; you turned them down'), tk('drawn to you; you have not answered'),
  // An NPC's IntelEngine package (the Tasks page)
  tk('Travel (Walk)'), tk('Travel (Jog)'), tk('Travel (Run)'), tk('Travel (Stalk)'), tk('Sandbox'), tk('Sandbox (Near Player)'),
  // A task's kind, as a task card writes it (SlotCard) and as a scheduled task's line does (TasksTab)
  tk('Travel'), tk('Fetch Npc'), tk('Deliver Message'), tk('Search For Actor'), tk('Escort'), tk('Story'), tk('Story Npc'),
  tk('Scheduled'), tk('travel'), tk('fetch npc'), tk('deliver message'),
  // How a scheduled task stands (IntelEngine_Schedule.psc, GetScheduleStatus; "in ~3h" is a text of its own)
  tk('overdue'), tk('very soon'), tk('en route, overdue'), tk('en route, soon'), tk('meeting'),
  // A story's kind, as the Story page writes it, and the Director's busy line
  tk('seek player'), tk('informant'), tk('road encounter'), tk('ambush'), tk('faction ambush'), tk('stalker'), tk('message'),
  tk('quest'), tk('npc interaction'), tk('npc gossip'), tk('Faction Ambush'), tk('Npc Interaction'), tk('Npc Gossip'),
  // The dungeon master's quest: its kind (sub-type) and enemies
  tk('find item'), tk('faction combat'), tk('faction rescue'), tk('faction battle'),
  // Beside a name in the Director's pickers
  tk('nearby'), tk('faction'),
  // Political events the chronicle has no name of its own for (their type, as written)
  tk('trade deal'), tk('diplomatic gift'), tk('insult'), tk('trade dispute'), tk('embargo'), tk('territory dispute'),
  tk('betrayal'), tk('peace offer'), tk('event'),
];
