"""Exercise the built dashboard in isolated headless Chrome; stdlib only.
Usage: python SKSE/tests/RunDashboardDropdownCheck.py [chrome.exe] [dashboard-tree] [--busy-only]
"""
import html
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHROME = Path(sys.argv[1] if len(sys.argv) > 1 else r'C:\Program Files\Google\Chrome\Application\chrome.exe')
DIST = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / 'web/dashboard/dist'

SCRIPT = r"""
(async () => {
  const pause = () => new Promise(resolve => setTimeout(resolve, 100));
  const check = (ok, message) => { if (!ok) throw Error(message); };
  const button = label => document.querySelector(`[role=combobox][aria-label="${label}"]`);
  const clickText = text => [...document.querySelectorAll('button')].find(e => e.textContent.trim() === text).click();
  const choose = async (label, text) => {
    const control = button(label);
    control.scrollIntoView({ block: 'nearest' }); await pause();
    control.dispatchEvent(new MouseEvent('mousedown', { bubbles: true })); control.focus({ preventScroll: true });
    await pause(); // Focus/scroll dismissal must settle before opening the next control.
    if (control.getAttribute('aria-expanded') !== 'true') control.click();
    await pause();
    const option = [...document.querySelectorAll('[role=option]')].find(e => e.textContent.trim() === text);
    check(option, `missing option ${label}: ${text}`); option.click(); await pause();
  };
  const fill = async (selector, value) => {
    const element = document.querySelector(selector);
    element.scrollIntoView({ block: 'nearest' }); await pause();
    element.dispatchEvent(new MouseEvent('mousedown', { bubbles: true }));
    element.focus({ preventScroll: true });
    await pause();
    Object.getOwnPropertyDescriptor(element.tagName === 'TEXTAREA' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype, 'value').set.call(element, value);
    element.dispatchEvent(new Event('input', { bubbles: true })); await pause();
  };
  const filterPick = async (label, text) => {
    const count = payloads.length;
    await fill(`[aria-label="${label}"]`, '__no_match__');
    const dispatch = { 'Story type':'Dispatch Story', 'Social type':'Dispatch Social', 'Political event type':'Dispatch Political Event' }[label];
    if (dispatch) check([...document.querySelectorAll('button')].find(e=>e.textContent.trim()===dispatch).disabled, `${label}: unpicked type can dispatch`);
    check(!document.querySelector('[role=option]') && payloads.length === count, `${label}: free text submitted`);
    await fill(`[aria-label="${label}"]`, text.toLowerCase());
    const options = [...document.querySelectorAll('[role=option]')];
    check(options.length && options.every(option => option.textContent.toLowerCase().includes(text.toLowerCase())), `${label}: filter failed`);
    options.find(option => option.textContent.trim() === text).click(); await pause();
    check(button(label).value === text, `${label}: pick failed`);
    const beforeClear = payloads.length;
    document.querySelector(`[aria-label="Clear ${label}"]`).click(); await pause();
    check(button(label).value === '', `${label}: clear left text behind`);
    if (label === 'Dashboard hotkey') check(payloads.length === beforeClear, 'clearing hotkey committed an invalid key');
    await choose(label, text);
  };
  try {
    await pause();
    window.payloads = [];
    window.escapeCapture = [];
    window.magelight = { send: (name, value) => escapeCapture.push([name, value]) };
    for (const name of ['dispatchStory','dispatchNpcSocial','dispatchPolitics','executeAction','cancelTask','changePluginConfig','generateDirector'])
      window['onDashboard_' + name] = json => payloads.push([name, json]);
    window.updateFullState(JSON.stringify({config:{}, pluginConfig:{'ui.scale':1},
      tasks:[{index:0,state:0,cooldownRemaining:30}], scheduled:[],
      story:{enabled:true}, quest:{active:true}, social:{enabled:true},
      npcSocialLog:[{type:'npc_interaction',text:'A conversation'}], packages:[{formId:1,name:'Nazeem',pkgType:'Sandbox'}],
      loadedNpcs:Array.from({length:30},(_,i)=>({formId:108196+i,name:i?'Guard '+i:'Nazeem'})),
      actions:[{name:'GoToLocation',enabled:true},{name:'ReportPlayerConduct',params:[{name:'sentiment',enum:['strongly_positive','positive','negative','strongly_negative']},{name:'factionId'},{name:'reason'}]},{name:'ChangeSpeed',enabled:true}],
      politics:{enabled:true,factions:[{id:'ImperialFaction',name:'Imperial Legion'},{id:'StormcloakFaction',name:'Stormcloaks'},...Array.from({length:3},(_,i)=>({id:'ThalmorFaction'+i,name:'Thalmor '+i}))],relations:[],wars:[],player_standings:[],events:[]}}));
    await pause(); clickText('Director'); await pause();
    if (location.hash === '#story_type_hints') {
      const story = () => document.querySelector('section');
      const text = () => [...story().querySelectorAll('p, span')].map(e=>e.textContent).join('\n');
      check(text().includes('what they will do.') && text().includes('why the NPC acts; used as story context.'), 'auto hints changed');
      await choose('Story type', 'Road Encounter');
      check(text().includes('Appears on the road ahead of you, travelling to Destination on their own errand (outdoors only).'), 'road encounter type hint missing');
      check(text().includes('Why they are travelling, e.g. set out to sell pelts in Helgen.') && text().includes('Where they are heading. Required: a real Skyrim location.'), 'road narration or Destination hint missing');
      story().querySelector('summary button').click(); await pause();
      const road = JSON.parse(payloads.at(-1)[1]);
      check(payloads.at(-1)[0] === 'generateDirector' && road.fields.storyType === 'road_encounter' && road.schema.narration.hint.includes('Why they are travelling') && road.schema.narration.hint.includes('outdoors only') && road.variants.road_encounter.destination.hint.includes('Where they are heading.'), 'Generate missing road guidance');
      clickText('Stop'); await pause();
      await choose('Story type', 'Message');
      check(text().includes('Sends a nearby courier to deliver a message from the selected NPC.') && text().includes('Why the sender wants to contact you.'), 'message hints did not replace road hints');
      check(!text().includes('Where they are heading.') && !text().includes('Why they are travelling'), 'road hints survived type switch');
      check(text().includes('Optional. If set with meetTime, creates a meeting invitation.'), 'message destination hint missing');
      story().querySelector('summary button').click(); await pause();
      const message = JSON.parse(payloads.at(-1)[1]);
      check(message.schema.narration.hint.includes('Why the sender wants to contact you.') && !message.schema.narration.hint.includes('travelling'), 'Generate retained previous type guidance');
      clickText('Stop'); await pause();
      document.querySelector('[aria-label="Clear Story type"]').click(); await pause();
      check(text().includes('what they will do.') && text().includes('why the NPC acts; used as story context.'), 'auto generic hints not restored');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['story_type_hints'],guidance:[road.schema.narration.hint,message.schema.narration.hint]});
      return;
    }
    if (location.hash === '#tasks_packages_folded') {
      // Core.psc emits agentFormId; NPCIndex::ScanActorsWithPackages emits numeric formId/name/pkgType.
      const tasks = [
        {index:0,state:1,agentName:'Courier',agentFormId:108196,taskType:'travel'},
        {index:1,state:1,agentName:'No package',agentFormId:108198,taskType:'travel'},
        {index:2,state:0,agentName:'',agentFormId:0,cooldownRemaining:30}
      ];
      const packages = [
        {formId:108196,name:'Courier',pkgType:'Travel (Jog)'},
        {formId:108197,name:'Courier',pkgType:'Sandbox'}
      ];
      const push = entries => window.updateFullState(JSON.stringify({config:{},tasks,scheduled:[],packages:entries}));
      window.onDashboard_removePackages = json => payloads.push(['removePackages',json]);
      push(packages); await pause(); clickText('Tasks'); await pause();
      check(![...document.querySelectorAll('button')].some(e=>e.textContent.trim()==='Packages'), 'Packages tab still present');
      const cards = [...document.querySelectorAll('.slot-card')];
      check(cards.find(e=>e.textContent.includes('Traveling') && e.textContent.includes('Courier'))?.textContent.includes('Travel (Jog)'), 'task card missing matched package type');
      check(!cards.find(e=>e.textContent.includes('No package'))?.textContent.includes('Travel (Jog)'), 'unmatched task inherited package type');
      const section = [...document.querySelectorAll('details')].find(e=>e.querySelector('summary')?.textContent.includes('Other IE packages'));
      check(section && section.open, 'orphan packages missing collapsible section');
      check(section.querySelectorAll('.slot-card').length === 1 && section.textContent.includes('Sandbox') && !section.textContent.includes('Travel (Jog)'), 'package/task identity partition failed');
      section.querySelector('summary').click(); await pause();
      check(!section.open && !section.querySelector('.slot-card').checkVisibility(), 'orphan section did not collapse');
      section.querySelector('summary').click(); await pause();
      section.querySelector('button').click(); await pause();
      check(payloads.length === 1 && payloads[0][0] === 'removePackages' && JSON.parse(payloads[0][1]).formId === 108197, 'Remove sent wrong action or identity');
      check(section.querySelector('button').disabled && section.querySelector('button').textContent === 'Removing\u2026', 'pending removal state missing');
      push([packages[0]]); await pause();
      check(![...document.querySelectorAll('h2')].some(e=>e.textContent === 'Other IE packages'), 'empty orphan section remains visible');
      push(packages); await pause();
      const remove = [...document.querySelectorAll('button')].find(e=>e.textContent === 'Remove');
      check(remove && !remove.disabled, 'server-confirmed removal did not clear pending state');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['tasks_packages_folded'],payloads});
      return;
    }
    if (location.hash === '#dashboard_sections_collapse') {
      const reloaded = sessionStorage.getItem('dashboard_sections_collapse') === 'closed';
      if (!reloaded) {
        window.updateFullState(JSON.stringify({config:{}, pluginConfig:{'ui.scale':1},
          tasks:[{index:0,state:0,cooldownRemaining:30}], scheduled:[],
          story:{enabled:true}, quest:{active:true}, social:{enabled:true},
          npcSocialLog:[{type:'npc_interaction',text:'A conversation'}],
          packages:[{formId:1,name:'Nazeem',pkgType:'Sandbox'}],
          actions:[{name:'GoToLocation',enabled:true},{name:'OtherModAction',enabled:true}],
          politics:{enabled:true,factions:[{id:'A',name:'Faction A'}],relations:[],
            wars:[{faction_a:'A',faction_b:'B'}],player_standings:[{faction:'A',standing:5}],events:[]}}));
        await pause();
        for (const [tab, expected] of [['Director',4],['Actions',2],['Politics',5],['Story',5],['Tasks',4]]) {
          clickText(tab); await pause();
          const headers = [...document.querySelectorAll('.section-header')];
          check(headers.length === expected, `${tab}: expected ${expected} section headings, found ${headers.length}`);
          for (const heading of headers) {
            const summary = heading.closest('summary'), section = summary?.parentElement;
            check(section?.tagName === 'DETAILS' && section.open, `${tab}: section header is not open details/summary`);
            check(getComputedStyle(heading,'::before').content.includes('▾'), `${tab}: open arrow missing`);
            summary.click(); await pause();
            check(!section.open && [...section.children].filter(e => e !== summary).every(e => !e.checkVisibility()),
              `${tab}: collapsed body remains visible (open=${section.open}, body=${[...section.children].filter(e=>e!==summary).map(e=>`${e.tagName}:${e.getClientRects().length}:${e.checkVisibility()}`)})`);
            check(getComputedStyle(heading,'::before').content.includes('▸'), `${tab}: collapsed arrow missing`);
            summary.click(); await pause();
          }
        }
        clickText('Director'); await pause();
        const generateSection = [...document.querySelectorAll('.dashboard-section')].find(d=>[...d.querySelector('summary').querySelectorAll('button')].some(b=>b.textContent.trim()==='Generate'));
        check(generateSection, 'Director Generate section missing');
        generateSection.querySelector('summary').click(); await pause();
        generateSection.querySelector('summary button').click(); await pause();
        check(generateSection.open && payloads.some(([name])=>name==='generateDirector'), 'collapsed Generate did not open section or fire');
        clickText('Stop'); await pause();
      }
      const details = document.querySelector('section details'), summary = details.querySelector('summary');
      if (reloaded) {
        check(!details.open, 'closed section did not persist across page reload');
        check([...details.children].filter(e => e !== summary).every(e => !e.checkVisibility()), 'reloaded collapsed body remains visible');
        summary.click(); await pause();
        payloads.length = 0;
        [...summary.querySelectorAll('button')].find(e => e.textContent.trim() === 'Generate').click();
        await pause();
        check(details.open && payloads.some(([name]) => name === 'generateDirector'), 'header Generate toggled section or callback did not fire');
        clickText('Stop');
        sessionStorage.removeItem('dashboard_sections_collapse');
        document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['dashboard_sections_collapse'],sections:20,reloadPersisted:true,headerGenerateFired:true});
        return;
      }
      check(details.open, 'section did not default open');
      summary.click(); await pause();
      check(!details.open && !details.querySelector(':scope > :not(summary)').checkVisibility(),
        'summary click did not close/hide section body');
      sessionStorage.setItem('dashboard_sections_collapse', 'closed');
      location.reload();
      return;
    }
    if (location.hash === '#director_required_fields') {
      const story = () => document.querySelector('section:nth-of-type(1)');
      const dispatch = () => [...story().querySelectorAll('button')].find(e => e.textContent.trim() === 'Dispatch Story');
      const hints = () => [...story().querySelectorAll('p')].map(e => e.textContent);
      const missing = () => [...story().querySelectorAll('p')].find(e => e.textContent.startsWith('Fill required fields:'));
      check(['NPC *:', 'Story type *:', 'Narration *:'].every(text => hints().some(hint => hint.includes(text))), 'Story required field hints are not marked');
      for (const [index, title] of [[1,'Story Dispatch'],[2,'NPC Social Dispatch'],[3,'Political Dispatch']]) {
        const section = document.querySelector(`section:nth-of-type(${index})`);
        const heading = section.querySelector('h2'), generate = [...section.querySelectorAll('button')].find(e=>e.textContent==='Generate');
        check(heading?.textContent === title, 'missing form title: '+title);
        const markers = [...section.querySelectorAll('span')].filter(e=>e.textContent==='*');
        check(markers.length > 0 && markers.every(e=>e.classList.contains('text-red-400')), 'required marker missing red class: '+title);
        const h=heading.getBoundingClientRect(), b=generate.getBoundingClientRect();
        check(b.left >= h.right && Math.abs(b.top-h.top)<10 && b.width<100, 'Generate is not a small right-hand header button');
      }
      check(dispatch().disabled && missing()?.textContent === 'Fill required fields: NPC, Narration', 'Story missing list does not match initial validation');
      await fill('input[placeholder="NPC name (e.g. Saadia)"]', 'Saadia');
      check(dispatch().disabled && missing()?.textContent === 'Fill required fields: Narration', 'filled NPC remained missing');
      await choose('Story type', 'Message');
      check(![...story().querySelectorAll('label')].some(e => e.textContent.includes('Message content') && e.textContent.includes('*')), 'optional Dispatch field marked required');
      await fill('section:nth-of-type(1) textarea', 'Requests a private meeting');
      check(!dispatch().disabled && !missing(), 'valid Story remains disabled or shows missing fields');
      const section = index => document.querySelector(`section:nth-of-type(${index})`);
      const submit = index => [...section(index).querySelectorAll('button')].at(-1);
      const missingIn = index => [...section(index).querySelectorAll('p')].find(e => e.textContent.startsWith('Fill required fields:'))?.textContent;
      await fill('input[placeholder="NPC 1 (e.g. Mikael)"]', 'Mikael');
      await fill('input[placeholder="NPC 2 (e.g. Sven)"]', 'Sven');
      await fill('section:nth-of-type(2) textarea', 'Discussed a shipment');
      await choose('Social type', 'Gossip');
      check(!submit(2).disabled && !missingIn(2) && ![...section(2).querySelectorAll('label')].some(e => e.textContent.includes('Gossip') && e.textContent.includes('*')), 'Social optional variant marked required');
      await fill('input[placeholder="Faction A (e.g. StormcloakFaction)"]', 'StormcloakFaction');
      await fill('input[placeholder="Faction B (e.g. ThalmorFaction)"]', 'StormcloakFaction');
      await fill('section:nth-of-type(3) textarea', 'A diplomatic exchange');
      check(submit(3).disabled && missingIn(3) === 'Fill required fields: Faction B', 'same-faction validation lost its label');
      await fill('input[placeholder="Faction B (e.g. ThalmorFaction)"]', 'ThalmorFaction');
      check(!submit(3).disabled && !missingIn(3), 'valid Political form remains blocked');
      [...section(3).querySelectorAll('button')].find(e=>e.textContent.trim()==='Dispatch Political Event').click(); await pause();
      const politicalPayload = JSON.parse(payloads.find(([name])=>name==='dispatchPolitics')[1]);
      check(politicalPayload.eventType==='trade_deal' && politicalPayload.eventLabel==='Trade Deal', 'political producer omitted option label or key');
      await choose('Action NPC', 'Nazeem'); await choose('Action', 'GoToLocation');
      check(submit(4).disabled && missingIn(4) === 'Fill required fields: Destination', 'Action missing destination not listed');
      check([...section(4).querySelectorAll('label')].some(e => e.textContent.replace(/\s/g, '') === 'Destination*'), 'required Action variant not marked');
      await fill('input[placeholder="e.g. Whiterun, Bannered Mare, home"]', 'Whiterun');
      check(!submit(4).disabled && !missingIn(4), 'valid Action remains blocked');
      await choose('Action', 'ChangeSpeed');
      check(submit(4).disabled && missingIn(4) === 'Fill required fields: New speed', 'Action requirements did not change with variant');
      await choose('New speed', 'Jog');
      check(!submit(4).disabled && !missingIn(4), 'picked speed remains blocked');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['director_required_fields']});
      return;
    }
    if (location.hash === '#director_generate_identities') {
      const failures = [];
      const control = (index, text) => [...document.querySelectorAll(`section:nth-of-type(${index}) button`)].find(e=>e.textContent.trim()===text);
      for (const [index, form, fields, expected] of [
        [2,'social',{npc1Name:'Sven',npc2Name:'Mikael',socialType:'npc_interaction'},['Sven','Mikael']],
        [3,'political',{factionA:'Imperial Legion',factionB:'Stormcloaks'},['ImperialFaction','StormcloakFaction']],
        [3,'political',{factionA:'ImperialFaction',factionB:'StormcloakFaction'},['ImperialFaction','StormcloakFaction']],
        [1,'story',{npcName:'Sven',storyType:'informant',subject:'Mikael',sender:'Sven'},['Sven','Mikael','Sven']],
        [1,'story',{npcName:'None',storyType:'informant',subject:'',sender:'None'},['','','']],
      ]) {
        control(index,'Clear').click(); await pause();
        control(index,'Generate').click(); await pause();
        const request=JSON.parse(payloads.at(-1)[1]);
        check(!request.options.npcs.includes('Sven') && !request.options.npcs.includes('Mikael'), 'fixture unexpectedly contains producer NPC names');
        window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form,ok:true,fields})); await pause();
        const inputs=[...document.querySelectorAll(`section:nth-of-type(${index}) input[type=text]:not([role=combobox])`)];
        const values=(form==='story' ? [inputs[0],inputs[1],inputs[3]] : inputs.slice(0,2)).map(e=>e.value);
        const audit=JSON.parse(payloads.at(-1)[1]).outcome;
        if (JSON.stringify(values)!==JSON.stringify(expected) ||
            Object.keys(fields).filter(key=>!key.endsWith('Type')).some(key=>audit.rejected[key])) failures.push({form,fields,values,expected,rejected:audit.rejected});
      }
      check(!failures.length, 'producer identity regressions: '+JSON.stringify(failures));
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['director_generate_identities']});
      return;
    }
    if (location.hash === '#director_generate_merge_and_stop') {
      payloads.length = 0;
      check(button('Story type').placeholder==='(auto)' && button('Story type').value==='', 'untouched type is not auto');
      await fill('input[placeholder="NPC name (e.g. Saadia)"]','Saadia');
      await fill('section:nth-of-type(1) textarea','Wants to discuss a plan');
      await choose('Story type','Quest'); await filterPick('Story type','Quest');
      await choose('Sub-type','rescue');
      clickText('Generate');
      await pause();
      check(document.querySelector('section:nth-of-type(1) textarea').matches(':disabled') &&
        [...document.querySelectorAll('section:nth-of-type(1) button')].find(e=>e.textContent.trim()==='Dispatch Story').matches(':disabled'),
        'generating form remained editable');
      let request = JSON.parse(payloads.at(-1)[1]);
      check(payloads.at(-1)[0] === 'generateDirector' && request.fields.storyType === 'quest' &&
        request.fields.questSubType === 'rescue' && request.fields.enemyType === '' &&
        request.options.npcs.includes('Saadia'), 'generate omitted touched/auto/options contract');
      window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form:'story',ok:true,
        fields:{narration:'Partial plan',enemyType:'not_an_enemy',unexpected:'ignore'}})); await pause();
      check(document.querySelector('section:nth-of-type(1) textarea').value==='Partial plan' &&
        button('Enemy type').value==='', 'one invalid field discarded valid narrative');
      const partial = JSON.parse(payloads.at(-1)[1]).outcome;
      check(partial.applied.includes('narration') && partial.rejected.enemyType==='Invalid option' &&
        partial.rejected.unexpected==='Unknown field', 'partial validation reasons missing');
      clickText('Generate'); await pause(); request = JSON.parse(payloads.at(-1)[1]);
      check(Object.keys(request.variants.quest).every(key => key in request.fields), 'visible quest blanks omitted from request');
      check(Object.values(request.variants.quest).every(field=>!('placeholder' in field)), 'example placeholders leaked in generated variants');
      window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form:'story',ok:true,
        fields:{storyType:'quest',npcName:'Other',narration:'Wants to discuss a careful plan',
          questSubType:'rescue',enemyType:'bandit',questLocation:'Fort Greymoor',sender:'None',
          msgContent:'Rescue my friend',victimName:'Guard 1',itemName:'Iron sword',itemDesc:'An old family blade'}}));
      await pause();
      check(document.querySelector('input[placeholder="NPC name (e.g. Saadia)"]').value==='Saadia' &&
        document.querySelector('section:nth-of-type(1) textarea').value==='Wants to discuss a careful plan' &&
        button('Story type').value==='Quest' && button('Sub-type').value==='rescue' &&
        button('Enemy type').value==='bandit', 'generated merge changed identity/touched dropdown or lost text');
      check([...document.querySelectorAll('section:nth-of-type(1) input:not([role=combobox]), section:nth-of-type(1) textarea')].filter(e=>e.placeholder!=='e.g. Jarl Balgruuf').every(e=>e.value.trim()) &&
        document.querySelector('input[placeholder="e.g. Jarl Balgruuf"]').value==='', 'Generate lost quest content or applied None sender');
      const audit = JSON.parse(payloads.at(-1)[1]).outcome;
      check(audit?.ok && audit.filled.includes('npcName') && audit.blank.includes('narration') === false &&
        audit.applied.includes('questLocation') && audit.rejected.npcName === 'Preserved or unchanged', 'Generate outcome missing applied/preserved request audit');
      check(!audit.rejected.sender, 'empty/None identity was reported as rejected');
      clickText('Generate'); await pause(); const stopped=JSON.parse(payloads.at(-1)[1]); clickText('Stop'); await pause();
      window.directorGenerateResult(JSON.stringify({requestId:stopped.requestId,form:'story',ok:true,fields:{narration:'late'}}));
      await pause();
      check(document.querySelector('section:nth-of-type(1) textarea').value==='Wants to discuss a careful plan','Stop applied late result');
      clickText('Clear'); await pause();
      check(document.querySelector('input[placeholder="NPC name (e.g. Saadia)"]').value==='' &&
        button('Story type').value==='' && !payloads.some(([name])=>name.startsWith('dispatch')),'Clear failed or Generate dispatched');
      const formButton = (index, text) => [...document.querySelectorAll(`section:nth-of-type(${index}) button`)].find(e=>e.textContent.trim()===text);
      for (const [index, form, fields, label, expected] of [
        [2, 'social', {npc1Name:'Sven',npc2Name:'Mikael',socialType:'npc_interaction',narration:'Resolved a dispute',fact1:'settled the argument',fact2:'accepted the settlement'}, 'Social type', 'Interaction'],
        [2, 'social', {npc1Name:'Nazeem',npc2Name:'Guard 1',socialType:'npc_gossip',narration:'Discussed the missing shipment',gossip:'saw a suspicious shipment'}, 'Social type', 'Gossip'],
        [3, 'political', {factionA:'Imperial Legion',factionB:'Stormcloaks',eventType:'insult',description:'A public diplomatic insult',relationDelta:-5}, 'Political event type', 'Insult']
      ]) {
        formButton(index,'Generate').click(); await pause();
        const generated=JSON.parse(payloads.at(-1)[1]);
        window.directorGenerateResult(JSON.stringify({requestId:generated.requestId,form,ok:true,fields}));
        await pause();
        check(button(label).value===expected && document.querySelector(`section:nth-of-type(${index}) textarea`).value===
          (fields.narration || fields.description), `${form}: auto type and dependent draft did not merge`);
        if (form==='social' && fields.npc1Name==='Sven') check(
          document.querySelector('input[placeholder="NPC 1 (e.g. Mikael)"]').value==='Sven' &&
          document.querySelector('input[placeholder="NPC 2 (e.g. Sven)"]').value==='Mikael',
          'producer NPC names outside nearby options were rejected');
        if (form==='political') check(
          document.querySelectorAll('section:nth-of-type(3) input[type=text]')[0].value==='ImperialFaction' &&
          document.querySelectorAll('section:nth-of-type(3) input[type=text]')[1].value==='StormcloakFaction',
          'faction display names were not normalized to IDs');
        check([...document.querySelectorAll(`section:nth-of-type(${index}) input:not([role=combobox]), section:nth-of-type(${index}) textarea`)].every(e=>e.value.trim()),
          `${form}: Generate left visible fields blank`);
        if (form==='political') {
          formButton(index,'Generate').click(); await pause(); const partial=JSON.parse(payloads.at(-1)[1]);
          window.directorGenerateResult(JSON.stringify({requestId:partial.requestId,form,ok:true,
            fields:{description:'A revised diplomatic account',relationDelta:99,factionA:'ThalmorFaction'}})); await pause();
          check(document.querySelector('section:nth-of-type(3) textarea').value==='A revised diplomatic account' &&
            document.querySelector('section:nth-of-type(3) input[type=number]').value==='-5' &&
            JSON.parse(payloads.at(-1)[1]).outcome.rejected.relationDelta==='Invalid relation change',
            'invalid relation delta discarded valid description or selected faction');
        }
        formButton(index,'Generate').click(); await pause();
        const failed=JSON.parse(payloads.at(-1)[1]);
        window.directorGenerateResult(JSON.stringify({requestId:failed.requestId,form,ok:false,fields:{},error:'Generation failed'}));
        await pause();
        check(button(label).value===expected && formButton(index,'Generate'), `${form}: failure damaged draft or blocked retry`);
        formButton(index,'Clear').click(); await pause();
        check(button(label).value==='' && document.querySelector(`section:nth-of-type(${index}) textarea`).value==='', `${form}: Clear did not reset`);
      }
      formButton(2,'Generate').click(); await pause(); const duplicate=JSON.parse(payloads.at(-1)[1]);
      window.directorGenerateResult(JSON.stringify({requestId:duplicate.requestId,form:'social',ok:true,
        fields:{npc1Name:'Nazeem',npc2Name:'Nazeem',socialType:'npc_interaction',narration:'A valid scene',fact1:42,fact2:'considered the proposal'}})); await pause();
      const duplicateAudit=JSON.parse(payloads.at(-1)[1]).outcome;
      check(document.querySelector('input[placeholder="NPC 1 (e.g. Mikael)"]').value==='Nazeem' &&
        document.querySelector('input[placeholder="NPC 2 (e.g. Sven)"]').value==='' &&
        document.querySelector('section:nth-of-type(2) textarea').value==='A valid scene' &&
        duplicateAudit.rejected.npc2Name==='Identities must be distinct' && duplicateAudit.rejected.fact1==='Invalid type',
        'duplicate identities or wrong-type field discarded valid fields');
      await choose('Political event type','Insult');
      document.querySelector('[aria-label="Clear Political event type"]').click(); await pause();
      formButton(3,'Generate').click(); await pause(); const clearedType=JSON.parse(payloads.at(-1)[1]);
      check(clearedType.touched.eventType && clearedType.fields.eventType==='', 'cleared type fixture is not touched and empty');
      window.directorGenerateResult(JSON.stringify({requestId:clearedType.requestId,form:'political',ok:true,
        fields:{eventType:'insult',factionA:'StormcloakFaction',factionB:'ThalmorFaction',description:'An insult',relationDelta:-5}})); await pause();
      check(button('Political event type').value==='Insult' && document.querySelector('section:nth-of-type(3) textarea').value==='An insult',
        'cleared touched type prevented a complete generated draft: '+JSON.stringify({display:button('Political event type').value,description:document.querySelector('section:nth-of-type(3) textarea').value,outcome:JSON.parse(payloads.at(-1)[1]).outcome}));
      check(payloads.every(([name])=>name==='generateDirector'), 'generation dispatched an action');
      const messages=payloads.map(([,json])=>JSON.parse(json));
      check(messages.filter(m=>!m.outcome).every(request=>messages.filter(m=>m.requestId===request.requestId && m.outcome).length===1),
        'Generate emitted missing/duplicate outcome info records');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['director_generate_merge_and_stop'],payloads});
      return;
    }
    await busy_confirm_cancel_dispatch();
    if (location.hash === '#busy') {
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['busy_confirm_cancel_dispatch'],payloads});
      return;
    }
    payloads.length = 0;
    check(!document.querySelector('select'), 'native selects remain');
    await fill('input[placeholder="NPC name (e.g. Saadia)"]','Saadia');
    await fill('section:nth-of-type(1) textarea','Story proof');
    await filterPick('Story type','Message');
    await filterPick('Meeting time','dawn');
    await choose('Story type','Message'); // Same value must not clear type-specific fields.
    clickText('Dispatch Story'); await pause();
    await fill('input[placeholder="NPC 1 (e.g. Mikael)"]','Mikael');
    await fill('input[placeholder="NPC 2 (e.g. Sven)"]','Sven');
    await fill('section:nth-of-type(2) textarea','Social proof');
    await filterPick('Social type','Gossip'); clickText('Dispatch Social'); await pause();
    await fill('input[placeholder="Faction A (e.g. StormcloakFaction)"]','StormcloakFaction');
    await fill('input[placeholder="Faction B (e.g. ThalmorFaction)"]','ThalmorFaction');
    await fill('section:nth-of-type(3) textarea','Politics proof');
    await filterPick('Political event type','Insult'); clickText('Dispatch Political Event'); await pause();
    await filterPick('Action NPC','Nazeem'); await filterPick('Action','ReportPlayerConduct');
    await fill('input[placeholder="Select faction..."]','ThalmorFaction');
    await fill('section:nth-of-type(4) input:not([role=combobox]):not([placeholder="Select faction..."])','praised Legion');
    await filterPick('sentiment','positive');
    await choose('Action','ReportPlayerConduct'); // Same action must preserve all parameters.
    clickText('Execute Action'); await pause();
    const expected = [
      ['dispatchStory','{"npcName":"Saadia","storyType":"message","narration":"Story proof","meetTime":"dawn"}'],
      ['dispatchNpcSocial','{"npc1Name":"Mikael","npc2Name":"Sven","socialType":"npc_gossip","narration":"Social proof"}'],
      ['dispatchPolitics','{"factionA":"StormcloakFaction","factionB":"ThalmorFaction","eventType":"insult","description":"Politics proof","relationDelta":-5}'],
      ['executeAction','{"npcFormId":108196,"actionName":"ReportPlayerConduct","params":{"sentiment":"positive","factionId":"ThalmorFaction","reason":"praised Legion"}}']
    ];
    check(JSON.stringify(payloads) === JSON.stringify(expected), 'prechange payload bytes differ: '+JSON.stringify(payloads));
    const measurements = [];
    const geometry = async (control, label) => {
      document.activeElement?.blur(); await pause();
      const wrapper = control.parentElement, parent = wrapper.parentElement, next = wrapper.nextSibling;
      const originalStyle = wrapper.getAttribute('style');
      const placeholder = document.createElement('div');
      placeholder.className = wrapper.className;
      placeholder.style.cssText = `height:${wrapper.offsetHeight}px;width:${wrapper.offsetWidth}px`;
      parent.insertBefore(placeholder, wrapper);
      const host = document.createElement('div');
      document.getElementById('dashboard-root').appendChild(host);
      for (const scale of [1,2]) {
        host.style.cssText = `position:fixed;left:20px;top:20px;width:500px;height:${680/scale}px;overflow:hidden;zoom:${scale};z-index:999`;
        host.appendChild(wrapper);
        const modes = [['top',10],['bottom',680/scale-45],['fits-below',680/scale-170]];
        if (label === 'FactionInput' || label === 'Social type') modes.push(['short-fits-below',680/scale-156]);
        if (label === 'Action NPC') modes.push(['clamped',60/scale]);
        for (const [mode, y] of modes) {
          host.style.height = `${(mode === 'clamped' ? 180 : 680)/scale}px`;
          wrapper.style.cssText = `position:absolute;left:10px;top:${y}px;width:300px`;
          await pause(); // Reparenting can scroll the original pane; settle before opening.
          control.click(); await pause();
          const menu = wrapper.querySelector('[role=listbox]') || wrapper.querySelector('.overflow-y-auto');
          check(menu, `${label} ${scale} ${mode}: menu missing`);
          const a=wrapper.getBoundingClientRect(), m=menu.getBoundingClientRect(), h=host.getBoundingClientRect();
          const up=m.bottom<=a.top+0.5;
          check(up === (mode==='bottom'), `${label} ${scale} ${mode}: wrong direction ${JSON.stringify({anchor:[a.top,a.bottom],menu:[m.top,m.bottom],clip:[h.top,h.bottom]})}`);
          check(m.top>=h.top-0.5 && m.bottom<=Math.min(h.bottom,innerHeight)+0.5, `${label}: clipped`);
          measurements.push({label,scale,mode,anchor:[a.top,a.bottom],menu:[m.top,m.bottom],clip:[h.top,h.bottom]});
          control.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true})); await pause();
        }
      }
      parent.insertBefore(wrapper,next); if(originalStyle===null)wrapper.removeAttribute('style');else wrapper.setAttribute('style',originalStyle); host.remove();
      placeholder.remove();
      await pause();
    };
    for (const control of [...document.querySelectorAll('[role=combobox]')]) await geometry(control,control.getAttribute('aria-label'));
    // The five-row faction list must also prefer down when it fits below.
    await geometry(document.querySelector('input[placeholder="Faction A (e.g. StormcloakFaction)"]'),'FactionInput');
    const npc=button('Action NPC'); npc.click(); await pause();
    check(document.querySelector('[role=option][aria-selected=true]').textContent.trim()==='Nazeem','current value not highlighted');
    npc.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowDown',bubbles:true})); await pause();
    npc.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true})); await pause();
    check(npc.value==='Guard 1','keyboard selection failed');
    npc.click(); await pause(); document.body.dispatchEvent(new MouseEvent('mousedown',{bubbles:true})); await pause();
    check(!document.querySelector('[role=listbox]'),'outside click did not close');
    await choose('Action','ChangeSpeed'); await choose('New speed','Jog');
    const execute = () => [...document.querySelectorAll('button')].find(e=>e.textContent.trim()==='Execute Action');
    check(!execute().disabled,'picked NPC did not enable valid action');
    await fill('[aria-label="Action NPC"]','gUaRd 2');
    check(document.querySelectorAll('[role=option]').length===11,'case-insensitive actor filter failed');
    check(execute().disabled,'typed text retained previous picked FormID');
    npc.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true})); await pause();
    check(npc.value==='Guard 2' && !execute().disabled,'filtered Enter pick failed');
    clickText('Execute Action'); await pause();
    check(payloads.at(-1)[1]==='{\"npcFormId\":108198,\"actionName\":\"ChangeSpeed\",\"params\":{\"newSpeed\":\"1\"}}','filtered pick changed FormID/value transport');
    await choose('New speed','Jog');
    await fill('[aria-label="Action NPC"]','No such actor');
    check(!document.querySelector('[role=option]') && execute().disabled,'no-match text can submit');
    npc.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true})); await pause();
    check(execute().disabled,'no-match Enter selected an actor');
    document.querySelector('[aria-label="Clear Action NPC"]').click(); await pause();
    check(npc.value==='' && execute().disabled && document.querySelectorAll('[role=option]').length===30,'clear did not restore nearby list without a selection');
    await fill('[aria-label="Action NPC"]','Guard 29');
    document.querySelector('[role=option]').click(); await pause();
    check(npc.value==='Guard 29' && !execute().disabled,'filtered click pick failed');
    npc.click(); await pause();
    document.querySelector('.flex-1.overflow-y-auto').dispatchEvent(new Event('scroll'));
    await pause(); check(!document.querySelector('[role=listbox]'),'container scroll did not dismiss');
    npc.click(); await pause(); window.dispatchEvent(new Event('resize'));
    await pause(); check(!document.querySelector('[role=listbox]'),'resize did not dismiss');
    npc.click(); await pause();
    check(escapeCapture.at(-1)?.[1]==='1','open menu did not acquire Magelight Escape capture');
    npc.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true})); await pause();
    check(escapeCapture.at(-1)?.[1]==='0','Escape close did not release Magelight capture');
    npc.click(); await pause();
    const clearNpc = document.querySelector('[aria-label="Clear Action NPC"]');
    clearNpc.focus(); await pause();
    clearNpc.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true})); await pause();
    check(!document.querySelector('[role=listbox]') && escapeCapture.at(-1)?.[1]==='0','Escape on clear button left capture armed');
    npc.click(); await pause(); clearNpc.focus(); button('Action').focus(); await pause();
    check(!document.querySelector('[role=listbox]') && escapeCapture.at(-1)?.[1]==='0','focus leaving clear button left capture armed');
    const factionInput=document.querySelector('input[placeholder="Faction A (e.g. StormcloakFaction)"]');
    factionInput.focus(); await pause(); button('Action').focus(); await pause();
    check(!factionInput.parentElement.querySelector('.overflow-y-auto') && escapeCapture.at(-1)?.[1]==='0','faction focus exit left capture armed');
    await fill('input[placeholder="Faction A (e.g. StormcloakFaction)"]','__no_faction__');
    check(!factionInput.parentElement.querySelector('.overflow-y-auto') && escapeCapture.at(-1)?.[1]==='0','invisible faction list retained Escape capture');
    let escaped = false;
    const uncapturedEscape = () => { escaped = true; };
    document.addEventListener('keydown', uncapturedEscape);
    factionInput.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}));
    document.removeEventListener('keydown', uncapturedEscape);
    check(escaped,'invisible faction list swallowed Escape');
    async function busy_confirm_cancel_dispatch() {
      window.updateSlots(JSON.stringify([
        {index:2,state:1,agentName:'Nazeem',agentFormId:108196},
        {index:4,state:1,agentName:'Guard 1',agentFormId:108197}
      ])); await pause();
      const submit = section => document.querySelector(`section:nth-of-type(${section}) button.w-full`);
      const arm = async (section, expectedLabel) => {
        const count = payloads.length;
        submit(section).click(); await pause();
        check(payloads.length === count && submit(section).textContent.trim() === 'Busy — you sure?',
          `busy_confirm_cancel_dispatch: first ${expectedLabel} click dispatched instead of confirming`);
      };
      const confirm = async (section, expected) => {
        const count = payloads.length;
        submit(section).click(); await pause();
        check(JSON.stringify(payloads.slice(count).map(([name,json])=>[name,JSON.parse(json)])) === JSON.stringify(expected),
          'busy_confirm_cancel_dispatch: cancel/dispatch order or payload changed: '+JSON.stringify(payloads.slice(count)));
      };
      await choose('Story type','Seek Player');
      await fill('input[placeholder="NPC name (e.g. Saadia)"]',' nazeem ');
      await fill('section:nth-of-type(1) textarea','Busy story');
      await arm(1,'Story');
      await fill('input[placeholder="NPC name (e.g. Saadia)"]','Saadia');
      check(submit(1).textContent.trim()==='Dispatch Story','busy_confirm_cancel_dispatch: Story NPC change retained confirmation');
      await fill('input[placeholder="NPC name (e.g. Saadia)"]',' nazeem ');
      await arm(1,'Story');
      await confirm(1,[['cancelTask',{slot:2}],['dispatchStory',{npcName:'nazeem',storyType:'seek_player',narration:'Busy story'}]]);
      await choose('Social type','Gossip');
      for (const [first,second,slots] of [['Nazeem','Sven',[2]],['Mikael','Nazeem',[2]],['Nazeem','Guard 1',[2,4]]]) {
        await fill('input[placeholder="NPC 1 (e.g. Mikael)"]',first);
        await fill('input[placeholder="NPC 2 (e.g. Sven)"]',second);
        await fill('section:nth-of-type(2) textarea','Busy social');
        await arm(2,'Social');
        await fill('input[placeholder="NPC 2 (e.g. Sven)"]','Saadia');
        check(submit(2).textContent.trim()==='Dispatch Social','busy_confirm_cancel_dispatch: Social NPC change retained confirmation');
        await fill('input[placeholder="NPC 2 (e.g. Sven)"]',second);
        await arm(2,'Social');
        await confirm(2,[...slots.map(slot=>['cancelTask',{slot}]),['dispatchNpcSocial',{npc1Name:first,npc2Name:second,socialType:'npc_gossip',narration:'Busy social'}]]);
      }
      await choose('Action NPC','Nazeem'); await choose('Action','ChangeSpeed');
      await choose('New speed','Jog');
      await arm(4,'Action');
      await choose('Action NPC','Guard 2');
      check(submit(4).textContent.trim()==='Execute Action','busy_confirm_cancel_dispatch: Action NPC change retained confirmation');
      await choose('Action NPC','Nazeem');
      await arm(4,'Action');
      await confirm(4,[['cancelTask',{slot:2}],['executeAction',{npcFormId:108196,actionName:'ChangeSpeed',params:{newSpeed:'1'}}]]);
      window.updateSlots('[]'); await pause();
    }
    clickText('Settings'); await pause();
    clickText('Dashboard▼'); await pause();
    await geometry(button('Dashboard hotkey'),'Dashboard hotkey');
    await filterPick('Dashboard hotkey','Disabled');
    check(payloads.at(-1)[1]==='{\"path\":\"ui.dashboard_hotkey\",\"value\":-1}','numeric hotkey value changed');
    await filterPick('Dashboard hotkey','0');
    check(payloads.at(-1)[1]==='{\"path\":\"ui.dashboard_hotkey\",\"value\":48}','zero-label hotkey lost numeric key or clear support');
    await filterPick('Dashboard hotkey','F1');
    check(payloads.at(-1)[1]==='{\"path\":\"ui.dashboard_hotkey\",\"value\":112}','function-key label did not filter/display/submit correctly');
    document.getElementById('result').textContent=JSON.stringify({status:'PASS',payloads,measurements});
  } catch(error) { document.getElementById('result').textContent='FAIL: '+error.stack; }
})();
"""

with tempfile.TemporaryDirectory(prefix='ie-dropdowns-') as directory:
    folder = Path(directory)
    for source in DIST.iterdir():
        if source.is_file():
            shutil.copy2(source, folder / source.name)
    page = (folder / 'index.html').read_text(encoding='utf-8')
    page = page.replace('</body>', '<pre id="result">RUNNING</pre><script>' + SCRIPT + '</script></body>')
    (folder / 'index.html').write_text(page, encoding='utf-8')
    scenario = next((arg.split('=',1)[1] for arg in sys.argv if arg.startswith('--scenario=')), '')
    completed = subprocess.run([str(CHROME), '--headless=new', '--no-sandbox', '--disable-gpu',
        '--allow-file-access-from-files', '--no-first-run', '--window-size=' + ('1600,3000' if '--busy-only' in sys.argv or scenario == 'director_required_fields' else '1600,1000'),
        '--user-data-dir=' + str(folder / 'profile'), '--virtual-time-budget=90000', '--dump-dom',
        (folder / 'index.html').as_uri() + ('#busy' if '--busy-only' in sys.argv else '#' + scenario if scenario else '')], capture_output=True, text=True, encoding='utf-8', timeout=60)
    start = completed.stdout.find('<pre id="result">')
    result = html.unescape(completed.stdout[start + len('<pre id="result">'):].split('</pre>', 1)[0]) if start >= 0 else ''
    if not result.startswith('{'):
        raise SystemExit(result or completed.stderr or 'Chrome emitted no result')
    data = json.loads(result)
    print(json.dumps(data, indent=2))
    assert data['status'] == 'PASS'
