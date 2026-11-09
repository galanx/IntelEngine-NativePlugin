"""Exercise the built dashboard in isolated headless Chrome; stdlib only.
Usage: python SKSE/tests/RunDashboardDropdownCheck.py [chrome.exe] [dashboard-tree] [--busy-only]
"""
import base64
import os
import socket
import struct
import time
import urllib.request
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
  const optionText = option => option.textContent.trim().replace(/ \((?:nearby|faction)\)$/, '');
  const clickText = text => [...document.querySelectorAll('button')].find(e => e.textContent.trim() === text).click();
  const outcomeFor = id => payloads.filter(([name])=>name==='generateDirector').map(([,json])=>JSON.parse(json)).find(m=>m.requestId===id && m.outcome)?.outcome;
  const choose = async (label, text) => {
    const control = button(label);
    control.scrollIntoView({ block: 'nearest' }); await pause();
    control.dispatchEvent(new MouseEvent('mousedown', { bubbles: true })); control.focus({ preventScroll: true });
    await pause(); // Focus/scroll dismissal must settle before opening the next control.
    if (control.getAttribute('aria-expanded') !== 'true') control.click();
    await pause();
    const option = [...document.querySelectorAll('[role=option]')].find(e => optionText(e) === text);
    check(option, `missing option ${label}: ${text}`); option.click(); await pause();
  };
  const fill = async (selector, value, root=document) => {
    const element = root.querySelector(selector);
    check(element, `missing input ${selector}`);
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
    const dispatchSection = { 'Story type':1, 'Social type':2, 'Political event type':3 }[label];
    if (dispatchSection) check(document.querySelectorAll('section')[dispatchSection-1].querySelector('fieldset > button')?.disabled, `${label}: unpicked type can dispatch`);
    check(!document.querySelector('[role=option]') && payloads.length === count, `${label}: free text submitted`);
    await fill(`[aria-label="${label}"]`, text.toLowerCase());
    const options = [...document.querySelectorAll('[role=option]')];
    check(options.length && options.every(option => option.textContent.toLowerCase().includes(text.toLowerCase())), `${label}: filter failed`);
    options.find(option => optionText(option) === text).click(); await pause();
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
    for (const name of ['dispatchStory','dispatchNpcSocial','dispatchPolitics','executeAction','cancelTask','changePluginConfig','toggleAction','generateDirector'])
      window['onDashboard_' + name] = json => payloads.push([name, json]);
    window.refreshRequests = 0; window.refreshState = null;
    window.onRequestRefresh = () => { window.refreshRequests++; if (window.refreshState) window.updateFullState(window.refreshState); };
    window.updateFullState(JSON.stringify({config:{}, pluginConfig:{'ui.scale':1},
      tasks:[{index:0,state:0,cooldownRemaining:30}], scheduled:[],
      story:{enabled:true}, quest:{active:true}, social:{enabled:true},
      npcSocialLog:[{type:'npc_interaction',text:'A conversation'}], packages:[{formId:1,name:'Nazeem',pkgType:'Sandbox'}],
      loadedNpcs:Array.from({length:30},(_,i)=>({formId:108196+i,name:i?'Guard '+i:'Nazeem'})),
      actions:[{name:'GoToLocation',enabled:true},{name:'FetchPerson',enabled:true},{name:'ReportPlayerConduct',params:[{name:'sentiment',enum:['strongly_positive','positive','negative','strongly_negative']},{name:'factionId'},{name:'reason'}]},{name:'ChangeSpeed',enabled:true}],
      politics:{enabled:true,factions:[{id:'ImperialFaction',name:'Imperial Legion'},{id:'StormcloakFaction',name:'Stormcloaks'},...Array.from({length:3},(_,i)=>({id:'ThalmorFaction'+i,name:'Thalmor '+i}))],relations:[],wars:[],player_standings:[],events:[]}}));
    await pause();
    const reloadReady = location.hash === '#director_live_polish2' && localStorage.getItem('__directorLivePolish2Reload') === 'ready';
    if (!reloadReady) { clickText('Director'); await pause(); }
    if (location.hash === '#picker_rank') {
      await window.updateFullState(JSON.stringify({config:{},pluginConfig:{},tasks:[],scheduled:[],story:{enabled:true},social:{enabled:true},knownNpcs:['Snow-Shod','Asgeir Snow-Shod','Hod'],loadedNpcs:[{formId:1,name:'Asgeir Snow-Shod'}],actions:[]})); await pause();
      const npc = document.querySelector('input[placeholder="e.g. Saadia"]');
      await fill('input[placeholder="e.g. Saadia"]','Hod');
      npc.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true})); await pause();
      check(npc.value==='Hod','typing Hod and pressing Enter selected the wrong NPC');
      await fill('input[placeholder="e.g. Saadia"]','Ho'); await fill('input[placeholder="e.g. Saadia"]','Hod');
      const options = [...document.querySelectorAll('[role=option]')];
      npc.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowDown',bubbles:true})); await pause();
      check(getComputedStyle(options[1]).backgroundColor!==getComputedStyle(options[0]).backgroundColor,'ArrowDown did not visibly highlight the next NPC');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['picker_rank','picker_keyboard_highlight']}); return;
    }
    if (location.hash === '#status_visuals') {
      const generate = [...document.querySelectorAll('section')][0].querySelector('button[data-expand-section]');
      generate.click(); await pause();
      check([...document.querySelectorAll('small')].some(e=>e.textContent==='Generating…'&&e.className.includes('text-gray-400')),'Generate status is not gray');
      const request=JSON.parse(payloads.filter(([name])=>name==='generateDirector').at(-1)[1]);
      window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form:request.form,ok:false,error:'test error'})); await pause();
      check([...document.querySelectorAll('small')].some(e=>e.textContent==='test error'&&e.className.includes('text-red-400')),'Generate error is not red');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['status_visuals']}); return;
    }
    if (location.hash === '#replacement_confirm') {
      const fixture = (storyGeneration, socialGeneration) => ({config:{},pluginConfig:{},tasks:[{index:7,state:1,agentName:'Unrelated'}],scheduled:[],story:{enabled:true,isActive:true,generation:storyGeneration,activeNPC:'Old Story',activeType:'seek_player'},social:{enabled:true,isActive:true,generation:socialGeneration,traveler:'Old Traveler',target:'Old Target',type:'npc_interaction'},loadedNpcs:[{formId:1,name:'New Story'},{formId:2,name:'New Traveler'},{formId:3,name:'New Target'}],actions:[]});
      const section = i => document.querySelectorAll('section')[i-1], button = i => [...section(i).querySelectorAll('button')].find(e=>/Replace|Dispatch|confirm replacement/.test(e.textContent));
      const sent = name => payloads.filter(([n])=>n===name), click = async i => { check(button(i) && !button(i).disabled,'replacement submit unavailable'); button(i).click(); await pause(); };
      const idle = {...fixture(0,0),story:{enabled:true,isActive:false},social:{enabled:true,isActive:false}};
      await window.updateFullState(JSON.stringify(idle)); await pause();
      await fill('input[placeholder="e.g. Saadia"]','New Story',section(1)); await fill('textarea','Initial story',section(1));
      window.refreshState=JSON.stringify(fixture(10,11)); const storyRefreshes=window.refreshRequests; await click(1);
      check(window.refreshRequests===storyRefreshes+1 && button(1).textContent.includes('Replace Story'),'Story dispatch did not request or receive refreshed active state');
      const storyDispatches=sent('dispatchStory').length; await fill('textarea','Replace refreshed story',section(1)); await click(1);
      check(sent('dispatchStory').length===storyDispatches,'first refreshed click dispatched instead of arming confirmation');
      await click(1);
      check(JSON.parse(sent('dispatchStory').at(-1)[1]).replaceGeneration==='10','refreshed Story replacement did not dispatch on confirmation');
      window.refreshState=JSON.stringify(fixture(12,13)); await window.updateFullState(JSON.stringify({...idle,story:fixture(12,13).story})); await pause();
      await fill('input[placeholder="e.g. Mikael"]','New Traveler',section(2)); await fill('input[placeholder="e.g. Sven"]','New Target',section(2)); await fill('textarea','Initial social trip',section(2)); await fill('input[placeholder="e.g. confronted Sven about stolen goods"]','discussed the road',section(2)); await fill('input[placeholder="e.g. was accused by Mikael of stealing goods"]','heard about the road',section(2));
      const socialRefreshes=window.refreshRequests; await click(2);
      check(window.refreshRequests===socialRefreshes+1 && button(2).textContent.includes('Replace Social'),'Social dispatch did not request or receive refreshed active state');
      await fill('textarea','Replace refreshed social',section(2)); await fill('input[placeholder="e.g. confronted Sven about stolen goods"]','discussed the updated road'); await fill('input[placeholder="e.g. was accused by Mikael of stealing goods"]','heard the updated road'); await click(2); await click(2);
      check(JSON.parse(sent('dispatchNpcSocial').at(-1)[1]).replaceGeneration==='13','refreshed Social replacement did not dispatch on confirmation');
      payloads.length=0; window.refreshState=null;
      await window.updateFullState(JSON.stringify(fixture(4,8))); await pause();
      await fill('input[placeholder="e.g. Saadia"]','New Story',section(1)); await fill('textarea','A replacement story',section(1)); await click(1);
      check(!sent('dispatchStory').length && !sent('cancelTask').length,'Story first click dispatched or cancelled a task');
      await fill('textarea','Changed story request',section(1)); await click(1); check(!sent('dispatchStory').length,'changed Story request retained confirmation');
      await click(1); check(JSON.parse(sent('dispatchStory').at(-1)[1]).replaceGeneration==='4' && !sent('cancelTask').length,'Story replacement payload or no-cancel contract failed');
      await fill('input[placeholder="e.g. Mikael"]','New Traveler',section(2)); await fill('input[placeholder="e.g. Sven"]','New Target',section(2)); await fill('textarea','A replacement social trip',section(2)); await fill('input[placeholder="e.g. confronted Sven about stolen goods"]','discussed the road',section(2)); await fill('input[placeholder="e.g. was accused by Mikael of stealing goods"]','heard about the road',section(2)); check(!button(2).disabled,'valid Social replacement disabled'); await click(2);
      check(!sent('dispatchNpcSocial').length,'Social first click dispatched');
      await window.updateFullState(JSON.stringify(fixture(4,9))); await pause(); await click(2);
      check(!sent('dispatchNpcSocial').length,'changed active Social dispatch retained confirmation');
      await click(2); check(JSON.parse(sent('dispatchNpcSocial').at(-1)[1]).replaceGeneration==='9' && !sent('cancelTask').length,'Social replacement payload or no-cancel contract failed');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['replacement_confirm','dispatch_refresh_replacement']}); return;
    }
    if (location.hash === '#nearby_picks') {
      Math.random=()=>0.999;
      const fixture = {config:{},pluginConfig:{'ui.scale':1},tasks:[],scheduled:[],story:{enabled:true},social:{enabled:true},
        loadedNpcs:[{formId:1,name:'Natch [Merchant]'},{formId:2,name:'Nearby Guard'}],knownNpcs:['Distant Person'],actions:[{name:'FetchPerson'}]};
      const host = index => document.querySelector(`section:nth-of-type(${index})`);
      const control = (index,label) => [...host(index).querySelectorAll('button')].find(e=>e.textContent.trim()===label);
      const requests = () => payloads.filter(([name,json])=>name==='generateDirector' && !JSON.parse(json).outcome).map(([,json])=>JSON.parse(json));
      const reply = async fields => { const r=requests().at(-1); window.directorGenerateResult(JSON.stringify({requestId:r.requestId,form:r.form,ok:true,fields})); await pause(); return outcomeFor(r.requestId); };
      const stopClear = async index => { control(index,'Stop')?.click(); await pause(); control(index,'Clear').click(); await pause(); };
      window.updateFullState(JSON.stringify(fixture)); await pause();
      await choose('Story type','Informant');
      for (const [index,keys] of [[1,['npcName','subject']],[2,['npc1Name','npc2Name']],[4,['npcName','targetName']]]) {
        control(index,'Generate').click(); await pause(); const r=requests().at(-1), names=keys.map(k=>r.fields[k]);
        check(names.every(n=>fixture.loadedNpcs.some(npc=>npc.name===n)) && new Set(names).size===2, `${r.form}: pre-picks escaped nearby distinct pool: ${JSON.stringify(names)}`);
        await stopClear(index);
      }
      await choose('Action','FetchPerson'); await fill('[aria-label="Person to fetch"]','Nearby Guard',host(4));
      control(4,'Generate').click(); await pause();
      check(requests().at(-1).fields.npcName==='Natch [Merchant]' && requests().at(-1).fields.targetName==='Nearby Guard','actor pre-pick duplicated a typed target');
      await stopClear(4);
      window.updateFullState(JSON.stringify({...fixture,loadedNpcs:[fixture.loadedNpcs[0]]})); await pause();
      await choose('Story type','Informant'); control(1,'Generate').click(); await pause();
      check(requests().at(-1).fields.npcName==='Natch [Merchant]' && requests().at(-1).blank.includes('subject'),'exhausted nearby pool fell back to known NPCs');
      let audit=await reply({subject:'nAtCh',narration:'An individual known as [Subject NPC] was seen',gossip:'[Subject NPC] argued'});
      check(audit.rejected.subject==='Duplicate NPC' && audit.rejected.narration==='Placeholder text' && audit.rejected.gossip==='Placeholder text','duplicate or placeholder rejection missing: '+JSON.stringify(audit));
      check(host(1).querySelector('textarea').value==='' && requests().at(-1).followUp && requests().at(-1).blank.includes('gossip'),'rejected text was applied or follow-up missing');
      await reply({subject:'Distant Person',narration:'Wanted to share news',gossip:'argued over a debt'});
      await stopClear(1); await choose('Story type','Informant');
      control(1,'Generate').click(); await pause(); const reroll=requests().at(-1), countBeforeReject=requests().length;
      window.directorGenerateResult(JSON.stringify({requestId:reroll.requestId,form:'story',ok:true,
        fields:{narration:'Jon discussed a debt',subject:'Carlotta Valentia'},rejected:{gossip:'Placeholder NPC reference'}})); await pause();
      audit=outcomeFor(reroll.requestId);
      check(host(1).querySelector('textarea').value==='Jon discussed a debt' &&
        audit.rejected.gossip==='Placeholder NPC reference' && !audit.applied.includes('gossip') &&
        requests().length===countBeforeReject, 'native rejection retried, discarded a valid sibling, or applied rejected gossip');
      await stopClear(1);
      control(2,'Generate').click(); await pause();
      check(requests().at(-1).blank.includes('npc2Name'),'Social exhausted pool did not stay blank');
      audit=await reply({npc2Name:'nAtCh',socialType:'npc_interaction',narration:'Met over a debt',fact1:'asked about a debt',fact2:'discussed repayment'});
      check(audit.rejected.npc2Name==='Duplicate NPC','Social duplicate was accepted');
      await stopClear(2);
      control(4,'Generate').click(); await pause();
      check(requests().at(-1).blank.includes('targetName'),'Action exhausted pool did not stay blank');
      audit=await reply({targetName:'Natch [Merchant]',reason:'Wanted help'});
      check(audit.rejected.targetName==='Duplicate NPC','Action duplicate was accepted');
      await stopClear(4);
      window.updateFullState(JSON.stringify({...fixture,loadedNpcs:[]})); await pause();
      control(2,'Generate').click(); await pause();
      check(requests().at(-1).blank.includes('npc1Name') && requests().at(-1).blank.includes('npc2Name'),'empty nearby pool used known names');
      audit=await reply({npc2Name:'Natch [Merchant]',npc1Name:'natch [merchant]',socialType:'npc_interaction',narration:'Discussed trade',fact1:'offered wares',fact2:'considered buying'});
      check(audit.applied.includes('npc2Name') && audit.rejected.npc1Name==='Duplicate NPC' && host(2).querySelector('input[placeholder="e.g. Sven"]').value==='Natch [Merchant]','same-result duplicate or tagged NPC handling failed');
      await stopClear(2);
      await fill('input[placeholder="e.g. Saadia"]','Distant Person',host(1)); control(1,'Generate').click(); await pause();
      check(requests().at(-1).fields.npcName==='Distant Person' && requests().at(-1).options.npcs.includes('Distant Person'),'typed known NPC was lost');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['nearby_picks','duplicate_npc','placeholder_text','tagged_npc','single_follow_up']}); return;
    }
    if (location.hash === '#story_social_identity') {
      Math.random=()=>0;
      const story=()=>document.querySelector('section:nth-of-type(1)'), social=()=>document.querySelector('section:nth-of-type(2)');
      const control=(root,label)=>[...root.querySelectorAll('button')].find(e=>e.textContent.trim()===label);
      const request=()=>JSON.parse(payloads.filter(([name,json])=>name==='generateDirector'&&!JSON.parse(json).outcome).at(-1)[1]);
      const stop=root=>control(root,'Stop').click();
      await choose('Story type','Informant'); control(story(),'Generate').click(); await pause();
      let result=request();
      check(result.fields.subject && result.fields.subject!==result.fields.npcName && story().querySelector('input[placeholder="e.g. Camilla Valerius"]').value===result.fields.subject,
        'Informant did not preselect and show a distinct Subject NPC');
      check(!JSON.stringify([result.schema,result.variants]).includes('e.g.') &&
        !JSON.stringify([result.schema,result.variants]).includes('"was seen'),
        'Generate metadata retained examples');
      check(story().textContent.includes('e.g. "was seen arguing'),
        'UI Informant gossip hint was altered');
      stop(story()); await pause();
      await fill('input[placeholder="e.g. Camilla Valerius"]','Mikael',story()); control(story(),'Generate').click(); await pause();
      result=request(); check(result.fields.subject==='Mikael','Generate replaced a user-set Subject NPC'); stop(story()); await pause();
      await choose('Story type','Road Encounter'); control(story(),'Generate').click(); await pause(); result=request();
      check(!JSON.stringify([result.schema,result.variants]).includes('e.g.') &&
        !result.schema.narration.hint.includes('sell pelts in Helgen'), 'Generate schema retained narration examples');
      check(story().querySelector('textarea').placeholder==='e.g. set out to sell pelts in Helgen' &&
        story().textContent.includes('Why they are travelling, e.g. set out to sell pelts in Helgen.'), 'UI Road Encounter hints were altered');
      stop(story()); await pause();
      control(social(),'Generate').click(); await pause(); result=request();
      check(result.fields.npc1Name && result.fields.npc2Name && result.fields.npc1Name!==result.fields.npc2Name &&
        social().querySelector('input[placeholder="e.g. Mikael"]').value===result.fields.npc1Name &&
        social().querySelector('input[placeholder="e.g. Sven"]').value===result.fields.npc2Name,
        'Social Generate did not preselect and show distinct NPCs');
      stop(social()); await pause(); control(social(),'Clear').click(); await pause();
      await fill('input[placeholder="e.g. Mikael"]','Mikael',social()); control(social(),'Generate').click(); await pause(); result=request();
      check(result.fields.npc1Name==='Mikael' && result.fields.npc2Name && result.fields.npc2Name!=='Mikael' &&
        social().querySelector('input[placeholder="e.g. Sven"]').value===result.fields.npc2Name,
        'Social Generate replaced NPC 1 or failed to preselect a distinct empty NPC 2');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['story_social_identity','hint_stripping']}); return;
    }
    if (location.hash === '#action_generate') {
      Math.random=()=>0;
      window.updateFullState(JSON.stringify({config:{},pluginConfig:{'ui.scale':1},tasks:[],scheduled:[],
        story:{enabled:true},social:{enabled:true},loadedNpcs:[
          {formId:1,name:'Natch [Merchant]'},{formId:2,name:'Twin [Guard]'},{formId:3,name:'Twin [Merchant]'},
          {formId:4,name:'Twin [Guard]'}], knownNpcs:['Distant Person'],actions:[
          {name:'GoToLocation'},{name:'FetchPerson'},{name:'ChangeSpeed'},{name:'CancelCurrentTask'},
          {name:'ReportPlayerConduct',params:[{name:'factionId'},{name:'reason'},{name:'sentiment',enum:['positive','negative']}]}],
        politics:{factions:[{id:'GuildFaction',name:'Guild [Faction]'}]}})); await pause();
      const host = index => document.querySelector(`section:nth-of-type(${index})`);
      const control = (index,label) => [...host(index).querySelectorAll('button')].find(e=>e.textContent.trim()===label);
      const requests = () => payloads.filter(([name,json])=>name==='generateDirector' && !JSON.parse(json).outcome).map(([,json])=>JSON.parse(json));
      const reply = async (form, fields) => { const request=requests().at(-1); window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form,ok:true,fields})); await pause(); return request; };
      check(control(4,'Generate') && control(4,'Clear') && host(4).textContent.includes('NPC (nearby only)'),'Action Generate/Clear controls or nearby-only label missing');
      control(1,'Generate').click(); await pause();
      await reply('story',{npcName:'nAtCh',storyType:'seek_player',narration:'Wanted to discuss trade'});
      check(document.querySelector('input[placeholder="e.g. Saadia"]').value==='Natch [Merchant]','unique trailing-tag name was not snapped');
      control(1,'Clear').click(); await pause(); control(1,'Generate').click(); await pause();
      await reply('story',{npcName:'Twin',storyType:'seek_player',narration:'Wanted to discuss trade'});
      check(document.querySelector('input[placeholder="e.g. Saadia"]').value==='Natch [Merchant]','Story Generate replaced its preselected NPC with an ambiguous example match');
      control(1,'Clear').click(); await pause(); control(1,'Generate').click(); await pause();
      await reply('story',{npcName:'Twin [Guard]',storyType:'seek_player',narration:'Wanted to discuss trade'});
      check(document.querySelector('input[placeholder="e.g. Saadia"]').value==='Natch [Merchant]','Story Generate replaced its preselected NPC with a tagged name');
      control(4,'Generate').click(); await pause();
      let request=requests().at(-1);
      check(request.form==='action' && !request.schema.actionName.options.includes('CancelCurrentTask') &&
        !request.schema.actionName.options.includes('ChangeSpeed') && request.options.npcs.includes('Natch [Merchant]') &&
        !request.options.npcs.includes('Distant Person') && request.options.targets.includes('Distant Person'),'Action options were not restricted');
      check(request.fields.npcName && request.options.npcs.includes(request.fields.npcName) &&
        request.fields.actionName && request.schema.actionName.options.includes(request.fields.actionName) &&
        !request.blank.includes('npcName') && !request.blank.includes('actionName') &&
        button('Action NPC').value===request.fields.npcName && button('Action').value===request.fields.actionName,
        'Empty Action Generate did not preselect and show nearby NPC/action');
      check(button('Action NPC').matches(':disabled'),'Action was editable while generating');
      check(Object.values(request.fields).every(value=>value!=='') && request.blank.includes('reason'),
        'Action Generate sent empty-string fields or lost blank-key metadata');
      await reply('action',{destination:'Whiterun',reason:'expected the player to come'});
      check(button('Action NPC').value===request.fields.npcName && button('Action').value===request.fields.actionName,
        'generated Action changed the preselected NPC or action');
      control(4,'Generate').click(); await pause();
      const rejected=requests().at(-1);
      await reply('action',{destination:'Natch [Merchant]'});
      check(outcomeFor(rejected.requestId).rejected.destination==='Destination must be a place',
        'Action accepted an NPC name as a destination');
      control(4,'Generate').click(); await pause(); request=requests().at(-1);
      check(request.fields.npcName==='Natch [Merchant]' && request.fields.actionName==='GoToLocation' &&
        (request.fields.destination ?? '')==='','Action re-roll lost identities/type or retained generated text');
      await reply('action',{npcName:'Twin [Guard]',actionName:'FetchPerson',destination:'Riften',reason:'Wanted a new opportunity'});
      check(button('Action NPC').value==='Natch [Merchant]' && button('Action').value==='GoToLocation' &&
        host(4).querySelector('input[placeholder="e.g. Whiterun, Bannered Mare, home"]').value==='Riften',
        'Action re-roll changed identity/type or failed to rewrite text');
      await fill('input[placeholder="e.g. Whiterun, Bannered Mare, home"]','Solitude',host(4));
      check(control(4,'Generate'),`Generate missing after manual-param reply; controls=${host(4).querySelector('div.flex.items-center.justify-between')?.textContent}, pending=${JSON.stringify(requests().at(-1)?.fields)}`);
      control(4,'Generate').click(); await pause();
      check(requests().at(-1).touched.destination===true,'Action manual param ownership missing');
      await reply('action',{destination:'Whiterun',reason:'Wanted to visit Whiterun'});
      check(host(4).querySelector('input[placeholder="e.g. Whiterun, Bannered Mare, home"]').value==='Solitude',
        'Action generated result replaced manual param');
      check(JSON.parse(localStorage.getItem('intelengine.director.drafts')).action[2].destination==='Solitude',
        'Action generated draft was not persisted');
      control(4,'Generate').click(); await pause(); const stopped=requests().at(-1);
      control(4,'Stop').click(); await pause();
      window.directorGenerateResult(JSON.stringify({requestId:stopped.requestId,form:'action',ok:true,fields:{destination:'late'}})); await pause();
      check(host(4).querySelector('input[placeholder="e.g. Whiterun, Bannered Mare, home"]').value==='Solitude','Action Stop applied late result');
      control(4,'Clear').click(); await pause();
      check(button('Action NPC').value==='' && button('Action').value==='' &&
        !JSON.parse(localStorage.getItem('intelengine.director.drafts')).action,'Action Clear left fields or persisted draft');
      control(4,'Generate').click(); await pause(); request=requests().at(-1);
      check(request.fields.npcName && request.options.npcs.includes(request.fields.npcName) &&
        request.fields.actionName && request.schema.actionName.options.includes(request.fields.actionName) &&
        button('Action NPC').value===request.fields.npcName && button('Action').value===request.fields.actionName,
        'Clear then Generate did not pick and show a nearby NPC/action');
      control(4,'Stop').click(); await pause(); control(4,'Clear').click(); await pause();
      await choose('Action NPC','Natch [Merchant]'); await choose('Action','FetchPerson');
      control(4,'Generate').click(); await pause(); request=requests().at(-1);
      check(request.fields.targetName==='Twin [Guard]' && request.fields.npcName==='Natch [Merchant]' &&
        button('Person to fetch').value==='Twin [Guard]',`Action Generate did not preselect a distinct nearby target: ${JSON.stringify({target:request.fields.targetName,blank:request.blank,action:request.fields.actionName,actor:request.fields.npcName,targets:request.options.targets,shown:button('Person to fetch').value})}`);
      await reply('action',{targetName:'Natch [Merchant]',reason:'Found a lead'});
      check(outcomeFor(request.requestId).rejected.targetName==='Duplicate NPC' &&
        button('Person to fetch').value==='Twin [Guard]','Action accepted its actor as target or lost preselection');
      await fill('section:nth-of-type(4) input[aria-label="Person to fetch"]','Twin [Guard]',host(4));
      control(4,'Generate').click(); await pause(); request=requests().at(-1);
      check(request.fields.targetName==='Twin [Guard]','Action Generate replaced a user-set target');
      await reply('action',{targetName:'Distant Person'});
      check(button('Person to fetch').value==='Twin [Guard]','Generated Action replaced a user-set target');
      await choose('Action NPC','Natch [Merchant]');
      for (const action of ['CancelCurrentTask','ChangeSpeed']) {
        await choose('Action',action);
        if (action==='ChangeSpeed') await choose('New speed','Jog');
        const before=requests().length; control(4,'Generate').click(); await pause(); request=requests().at(-1);
        check(requests().length===before+1 && request.fields.actionName===action && request.schema.reason &&
          Object.keys(request.fields).every(key=>['npcName','actionName','reason'].includes(key)),
          `${action}: Generate did not request only the action reason: ${JSON.stringify(request)}`);
        await reply('action',{reason:'Wanted to help the player'});
        check(button('Action').value===action && (action!=='ChangeSpeed' || button('New speed').value==='Jog') &&
          host(4).querySelector('textarea').value==='Wanted to help the player',
          `${action}: Generate changed action/params or failed to fill Reason: action=${button('Action').value}, speed=${button('New speed')?.textContent}, reason=${host(4).querySelector('textarea').value}`);
      }
      control(4,'Generate').click(); await pause(); request=requests().at(-1);
      check((request.fields.reason ?? '')==='' && request.touched.reason===false,'generated Reason did not re-roll as blank text');
      const generatedReasonRequest=requests().at(-1), requestCount=requests().length;
      await reply('action',{reason:''});
      const reasonFollowUp=requests().at(-1), followUpCount=requests().length;
      check(followUpCount===requestCount+1 && reasonFollowUp.followUp && reasonFollowUp.schema.reason.required,'empty generated Action reason did not trigger exactly one required-field follow-up');
      await reply('action',{reason:'Rerolled motive'});
      check(requests().length===followUpCount && generatedReasonRequest!==reasonFollowUp,'empty generated Action reason triggered more than one follow-up');
      await fill('section:nth-of-type(4) textarea','Typed motive');
      control(4,'Generate').click(); await pause(); request=requests().at(-1);
      check(request.fields.reason==='Typed motive' && request.touched.reason===true,'typed Reason was not preserved as user-owned');
      await reply('action',{reason:'Generated replacement'});
      check(host(4).querySelector('textarea').value==='Typed motive','Generate replaced a typed Reason');
      check(host(4).querySelector('textarea') && host(4).textContent.includes('Why the NPC does this; they remember it and can explain it.'),
        'Action Reason field/hint missing');
      control(4,'Clear').click(); await pause();
      check(host(4).querySelector('textarea').value==='', 'Action Clear did not wipe Reason');
      await choose('Action NPC','Natch [Merchant]'); await choose('Action','CancelCurrentTask');
      control(4,'Execute Action').click(); await pause();
      check(!Object.hasOwn(JSON.parse(payloads.at(-1)[1]).params,'reason'),'empty Action Reason was sent');
      control(4,'Clear').click(); await pause();
      await choose('Action NPC','Natch [Merchant]');
      control(4,'Clear').click(); await pause();
      await choose('Action NPC','Natch [Merchant]'); await choose('Action','ReportPlayerConduct');
      control(4,'Generate').click(); await pause(); request=requests().at(-1);
      check(request.variants.ReportPlayerConduct.reason.required && request.variants.ReportPlayerConduct.factionId,
        'Action YAML fallback omitted params');
      await reply('action',{factionId:'GuildFaction',reason:'Helped local merchants',sentiment:'positive'});
      check(!control(4,'Execute Action').disabled,'Action YAML params did not satisfy required fields');
      check(payloads.filter(([name])=>name==='executeAction').length===1,'Generate dispatched gameplay');
      control(4,'Execute Action').click(); await pause();
      check(payloads.at(-1)[0]==='executeAction' && JSON.parse(payloads.at(-1)[1]).npcFormId===1 &&
        JSON.parse(payloads.at(-1)[1]).params.reason==='Helped local merchants','Action generated draft dispatched wrong actor/params');
      control(4,'Clear').click(); await pause();
      await fill('[aria-label="Action NPC"]','Twin [Guard]',host(4));
      const sameNameOptions=[...host(4).querySelectorAll('[role=option]')].filter(option=>optionText(option)==='Twin [Guard]');
      check(sameNameOptions.length===2,'duplicate nearby display names were not both selectable');
      sameNameOptions[1].click(); await pause();
      await choose('Action','GoToLocation');
      control(4,'Generate').click(); await pause(); request=requests().at(-1);
      await reply('action',{npcName:'Twin [Guard]',actionName:'GoToLocation',destination:'Riften',reason:'Wanted to travel'});
      check(button('Action NPC').value==='Twin [Guard]','Generate changed the selected duplicate actor display name');
      control(4,'Execute Action').click(); await pause();
      check(payloads.at(-1)[0]==='executeAction' && JSON.parse(payloads.at(-1)[1]).npcFormId===4,
        'Generate changed the selected duplicate actor FormID');
      for (const generatedNpc of ['Natch [Merchant]', 'Twin [Guard]']) {
        control(4,'Generate').click(); await pause();
        check(requests().at(-1).fields.npcName==='Twin [Guard]','Generate lost selection changed after mount');
        await reply('action',{npcName:generatedNpc,destination:'Whiterun',reason:'Wanted another trip'});
        const execute=host(4).querySelector('button.w-full'); check(execute && !execute.disabled,'Generated Action not executable'); execute.click(); await pause();
        check(payloads.at(-1)[0]==='executeAction' && JSON.parse(payloads.at(-1)[1]).npcFormId===4,'Generate replaced selected FormID with generated actor');
      }
      control(4,'Clear').click(); await pause(); await choose('Action','GoToLocation');
      await fill('[aria-label="Action NPC"]','Distant Person',host(4));
      const beforeUnmatched=requests().length, executions=payloads.filter(([name])=>name==='executeAction').length;
      control(4,'Generate').click(); await pause();
      if (requests().length>beforeUnmatched) await reply('action',{npcName:'Natch [Merchant]',destination:'Riften',reason:'Wanted to travel'});
      check(requests().length===beforeUnmatched,'Unpicked typed Action actor was random-filled by Generate');
      check(button('Action NPC').value==='Distant Person' && host(4).querySelector('button.w-full').disabled,'Typed Action query lost or became executable');
      await fill('[aria-label="Action NPC"]','Distant',host(4)); await fill('[aria-label="Action NPC"]','Distant Person',host(4));
      check(host(4).querySelector('[role=listbox]')?.textContent.includes('no nearby NPC matches'),'Action empty-match feedback missing');
      host(4).querySelector('button.w-full').click(); await pause();
      check(payloads.filter(([name])=>name==='executeAction').length===executions,'Unpicked far-away performer was submitted');
      document.querySelector('[aria-label="Clear Action NPC"]').click(); await pause(); control(4,'Generate').click(); await pause();
      check(requests().length===beforeUnmatched+1 && button('Action NPC').value==='Natch [Merchant]','Cleared Action query did not restore nearby auto-pick');
      control(4,'Stop').click(); await pause(); control(4,'Clear').click(); await pause();
      await choose('Action NPC','Natch [Merchant]'); await choose('Action','GoToLocation');
      window.updateFullState(JSON.stringify({config:{},pluginConfig:{},tasks:[],scheduled:[],loadedNpcs:[{formId:2,name:'Twin [Guard]'}],actions:[{name:'GoToLocation'}]})); await pause();
      const beforeMissing=requests().length; control(4,'Generate').click(); await pause();
      check(requests().length===beforeMissing && JSON.parse(localStorage.getItem('intelengine.director.drafts')).action[0]==='1' && host(4).querySelector('button.w-full').disabled,'Missing selected FormID was replaced');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['action_generate','name_snapping'],requests:requests().length});
      return;
    }
    if (location.hash === '#actions_grouped') {
      const actions = [
        {name:'FetchPerson',fileName:'fetch.yaml',description:'Find {{ npc.name }}',enabled:true,params:[],category:'intel_travel',isCategory:false},
        {name:'OtherFlat',fileName:'flat.yaml',description:'Flat mod action',enabled:true,params:[],category:'',isCategory:false},
        {name:'Scheduling',fileName:'scheduling.yaml',description:'Scheduling actions',enabled:false,params:[],category:'intel_scheduling',isCategory:true},
        {name:'ModAction',fileName:'mod.yaml',description:'Other mod action',enabled:true,params:[],category:'other_mod',isCategory:false},
        {name:'Communication',fileName:'communication.yaml',description:'Communication actions',enabled:true,params:[],category:'intel_communication',isCategory:true},
        {name:'CommunicationAction',fileName:'communication-action.yaml',description:'Communication action',enabled:false,params:[],category:'intel_communication',isCategory:false},
        {name:'GoToLocation',fileName:'travel.yaml',description:'Travel action',enabled:false,params:[],category:'intel_travel',isCategory:false},
        {name:'Travel',fileName:'travel-category.yaml',description:'Travel actions',enabled:true,params:[],category:'intel_travel',isCategory:true},
        {name:'UnknownCategory',fileName:'unknown-category.yaml',description:'Unknown category',enabled:true,params:[],category:'intel_custom',isCategory:true},
        {name:'CustomAction',fileName:'custom.yaml',description:'Custom IntelEngine action',enabled:true,params:[],category:'intel_custom',isCategory:false},
        {name:'ScheduledAction',fileName:'scheduled.yaml',description:'Scheduled action',enabled:true,params:[],category:'intel_scheduling',isCategory:false},
        {name:'ModCategory',fileName:'mod-category.yaml',description:'Mod category',enabled:true,params:[],category:'other_mod',isCategory:true}
      ];
      window.updateFullState(JSON.stringify({config:{},pluginConfig:{'ui.scale':1},tasks:[],scheduled:[],
        story:{enabled:true},quest:{active:true},social:{enabled:true},npcSocialLog:[],packages:[],
        loadedNpcs:[],actions}));
      await pause(); clickText('Actions'); await pause();
      const cards = [...document.querySelectorAll('button')].filter(e => e.querySelector('.text-xs.font-medium'));
      const names = cards.map(e => e.querySelector('.text-xs.font-medium').textContent.trim());
      const order = ['Travel (category)',...names.slice(1,3),'Communication (category)','CommunicationAction',
        'Scheduling (category)','ScheduledAction','UnknownCategory (category)','CustomAction'];
      check(names.slice(1,3).sort().join('|')==='FetchPerson|GoToLocation' &&
        JSON.stringify(names.slice(0,order.length))===JSON.stringify(order),
        'IntelEngine categories/actions not ordered and grouped: '+JSON.stringify(names));
      for (const parent of ['Travel','Communication','Scheduling','UnknownCategory']) {
        const card=cards.find(e=>e.querySelector('.text-xs.font-medium').textContent.trim()===parent+' (category)');
        check(card && !card.classList.contains('ml-4'), `category label/indent missing: ${parent}`);
      }
      for (const child of ['GoToLocation','FetchPerson','CommunicationAction','ScheduledAction','CustomAction']) {
        const card=cards.find(e=>e.querySelector('.text-xs.font-medium').textContent.trim()===child);
        const parent=cards.find(e=>e.querySelector('.text-xs.font-medium').textContent.trim()===(child==='GoToLocation'||child==='FetchPerson'?'Travel':child==='CommunicationAction'?'Communication':child==='ScheduledAction'?'Scheduling':'UnknownCategory')+' (category)');
        check(card && card.getBoundingClientRect().left > parent.getBoundingClientRect().left,
          `categorized action not visibly indented: ${child}`);
      }
      check(cards.find(e=>names.includes('FetchPerson') && e.textContent.includes('Find the NPC')),
        'NPC description placeholder was not substituted');
      const intelSection=[...document.querySelectorAll('h2')].find(e=>e.textContent.trim()==='IntelEngine Actions')?.closest('section');
      check(intelSection?.querySelector('.text-xs.text-gray-500')?.textContent.trim()==='9 actions',
        'IntelEngine category/action count is incorrect');
      const otherHeading=[...document.querySelectorAll('h2')].find(e=>e.textContent.trim()==='Other Mod Actions');
      const otherSection=otherHeading?.closest('section');
      check(otherSection && otherSection.querySelector('.text-xs.text-gray-600')?.textContent.trim()==='3 actions',
        'Other Mod Actions count is incorrect');
      check(names.slice(order.length).join('|')==='ModCategory (category)|ModAction|OtherFlat',
        'Other mod category/action and flat ordering is incorrect: '+JSON.stringify(names.slice(order.length)));
      check(cards.at(-2).getBoundingClientRect().left > cards.at(-3).getBoundingClientRect().left &&
        cards.at(-1).getBoundingClientRect().left === cards.at(-3).getBoundingClientRect().left,
        'Other mod grouped action or flat action indentation is incorrect');
      check(!otherSection.textContent.includes('Travel') && !otherSection.textContent.includes('CustomAction') &&
        !otherSection.textContent.includes('UnknownCategory'), 'IntelEngine action leaked into Other Mod Actions');
      const cardFor = name => [...document.querySelectorAll('button')].find(e=>e.querySelector('.text-xs.font-medium')?.textContent.trim()===name);
      for (const parent of actions.filter(a=>a.isCategory)) {
        const card = cardFor(parent.name+' (category)');
        check(card.classList.contains('!border-l-4') && getComputedStyle(card).borderLeftWidth==='4px',
          `category header lacks distinct accent border: ${parent.name}`);
        check(card.getBoundingClientRect().width > cardFor(actions.find(a=>!a.isCategory && a.category===parent.category).name).getBoundingClientRect().width,
          `category header is not wider than children: ${parent.name}`);
      }
      const updateActions = async () => { window.updateFullState(JSON.stringify({actions})); await pause(); };
      const checkChild = (action, categoryOff) => {
        const card = cardFor(action.name), status = card.querySelector('span');
        check(status.textContent.trim()===(categoryOff?'OFF (category)':action.enabled?'ON':'OFF'),
          `incorrect effective status: ${action.name}: ${status.textContent}`);
        const dimmed = categoryOff || !action.enabled;
        check(card.classList.contains('text-gray-600')===dimmed && !card.classList.contains('slot-card')===dimmed &&
          status.classList.contains('text-gray-600')===dimmed, `incorrect dimmed style: ${action.name}`);
      };
      for (const category of actions.filter(a=>a.isCategory)) {
        const children = actions.filter(a=>!a.isCategory && a.category===category.category);
        const original = category.enabled;
        category.enabled = false; await updateActions();
        for (const child of children) {
          checkChild(child, true);
          cardFor(child.name).click(); await pause();
          const [event,json] = payloads.pop();
          check(event==='toggleAction' && JSON.stringify(JSON.parse(json))===JSON.stringify({name:child.name,enabled:!child.enabled}),
            `category-off child toggle changed own enabled payload: ${child.name}`);
        }
        category.enabled = true; await updateActions();
        for (const child of children) checkChild(child, false);
        category.enabled = original;
      }
      await updateActions();
      cards.find(e=>e.querySelector('.text-xs.font-medium').textContent.trim()==='Travel (category)').click(); await pause();
      cards.find(e=>e.querySelector('.text-xs.font-medium').textContent.trim()==='GoToLocation').click(); await pause();
      check(JSON.stringify(payloads.map(([name,json])=>[name,JSON.parse(json)]))===JSON.stringify([
        ['toggleAction',{name:'Travel',enabled:false}],
        ['toggleAction',{name:'GoToLocation',enabled:true}]
      ]), 'category/action toggles emitted incorrect toggleAction payloads: '+JSON.stringify(payloads));
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['actions_grouped'],names,otherCount:3});
      return;
    }
    if (location.hash === '#director_draft_persistence') {
      await fill('input[placeholder="e.g. Saadia"]', 'Saadia draft');
      clickText('Tasks'); await pause();
      check(!document.querySelector('.director-tab')?.checkVisibility(), 'inactive Director remains visible');
      clickText('Director'); await pause();
      check(document.querySelector('input[placeholder="e.g. Saadia"]').value === 'Saadia draft',
        'Story field was lost switching to Tasks and back');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['director_draft_persistence']});
      return;
    }
    if (location.hash === '#director_busy_line') {
      const director = () => [...document.querySelectorAll('.director-tab')].find(e => e.checkVisibility());
      const section = index => director().querySelectorAll('section')[index-1];
      const heading = index => section(index).querySelector('h2');
      const suffix = index => [...heading(index).querySelectorAll('span')].find(e => e.textContent.includes('— busy:'));
      const oldLine = index => [...section(index).querySelectorAll('p')].find(e=>e.textContent.includes('engine busy with'));
      const dispatch = index => section(index).querySelector('fieldset > button');
      const update = async (storyActive, socialActive) => {
        window.updateFullState(JSON.stringify({config:{},pluginConfig:{'ui.scale':1},tasks:[],
          story:{enabled:true,isActive:storyActive,activeNPC:'Delphine',activeType:'seek_player'},
          social:{enabled:true,isActive:socialActive,traveler:'Mikael',target:'Sven',type:'npc_interaction'}}));
        await pause();
      };
      check(!oldLine(1) && !oldLine(2), 'busy paragraphs shown without active engine state');
      await update(true,true);
      const expected = [' — busy: Delphine (Seek Player)', ' — busy: Mikael → Sven (Npc Interaction)'];
      for (const index of [1,2]) {
        check(heading(index).textContent.includes(index===1?'Story Dispatch':'NPC Social Dispatch'),
          `busy suffix attached to wrong heading ${index}`);
        check(!oldLine(index), 'busy paragraph remains below Director heading');
        const marker = suffix(index), style = marker && getComputedStyle(marker);
        check(marker && style.fontStyle === 'italic' && style.color === 'rgb(251, 191, 36)',
          `missing italic amber busy suffix in heading ${index}`);
        check(marker.textContent === expected[index-1], `incorrect busy identity/type in heading ${index}`);
        const details = heading(index).closest('details');
        details.open = false; await pause();
        check(details.open === false && suffix(index)?.checkVisibility(), `busy suffix hidden when section ${index} collapsed`);
        details.open = true; await pause();
      }
      check(payloads.length===0, 'busy advisory dispatched an action');
      await fill('section:nth-of-type(1) input[placeholder="e.g. Saadia"]','Saadia',director());
      await fill('section:nth-of-type(1) textarea','came to repay a debt',director());
      await fill('section:nth-of-type(2) input[placeholder="e.g. Mikael"]','Mikael',director());
      await choose('Social type','Interaction');
      await fill('section:nth-of-type(2) input[placeholder="e.g. Sven"]','Sven',director());
      await fill('section:nth-of-type(2) textarea','discussed a shipment',director());
      const socialLabel = name => [...section(2).querySelectorAll('label')].find(e=>e.textContent.trim().startsWith(name));
      for (const [name,value] of [['NPC 1 memory','discussed the shipment'],['NPC 2 memory','agreed to help']]) {
        const control=socialLabel(name)?.parentElement.querySelector('input,textarea');
        Object.getOwnPropertyDescriptor(control.tagName==='TEXTAREA'?HTMLTextAreaElement.prototype:HTMLInputElement.prototype,'value').set.call(control,value);
        control.dispatchEvent(new Event('input',{bubbles:true})); await pause();
      }
      check(dispatch(1)?.disabled && dispatch(1).textContent.trim()==='Story engine busy',
        'active Story did not disable dispatch with busy label');
      check(!dispatch(2)?.disabled && dispatch(2).textContent.trim()==='NPC social busy',
        'active Social should show busy label while remaining dispatchable');
      const storyInput = () => section(1).querySelector('input[placeholder="e.g. Saadia"]');
      clickText('Tasks'); await pause(); clickText('Director'); await pause();
      check(storyInput()?.value==='Saadia', 'Story field was lost switching to Tasks and back');
      check(dispatch(1)?.disabled && dispatch(1).textContent.trim()==='Story engine busy' &&
        !dispatch(2)?.disabled && dispatch(2).textContent.trim()==='NPC social busy',
        'busy dispatch states changed after tab switch');
      dispatch(2).click(); await pause();
      check(payloads.length===1 && payloads[0][0]==='dispatchNpcSocial',
        'active Social busy button did not dispatch');
      await update(false,true);
      check(!suffix(1) && suffix(2) && !oldLine(1) && !oldLine(2), 'Story inactivity did not independently remove its heading suffix');
      await update(true,false);
      check(suffix(1) && !suffix(2) && !oldLine(1) && !oldLine(2), 'Social inactivity did not independently remove its heading suffix');
      await update(false,false);
      check(!suffix(1) && !suffix(2) && !oldLine(1) && !oldLine(2), 'inactive engines retained busy suffixes or paragraphs');
      const nativeTimeout=window.setTimeout; let queuedTimeout;
      window.setTimeout=(fn,delay,...args)=>delay===30000?(queuedTimeout=fn,0):nativeTimeout(fn,delay,...args);
      dispatch(1).click(); await pause();
      check(dispatch(1).textContent.trim()==='Queued' && heading(1).textContent.includes(' — queued: starts on dashboard close'),
        'Story dispatch did not show queued state');
      check(typeof queuedTimeout==='function', 'queued timeout was not armed');
      await update(true,false);
      check(dispatch(1).textContent.trim()==='Story engine busy' && !heading(1).textContent.includes(' — queued:'),
        'fresh Story state did not clear queued state and show busy status');
      await update(false,false); dispatch(1).click(); await pause();
      queuedTimeout(); await pause();
      check(dispatch(1).textContent.trim()==='Dispatch Story' && !heading(1).textContent.includes(' — queued:'),
        '30 second timeout did not clear queued state');
      window.setTimeout=nativeTimeout;
      const political=section(3);
      await fill('input[placeholder="e.g. StormcloakFaction"]','ImperialFaction',political);
      await fill('input[placeholder="e.g. ThalmorFaction"]','StormcloakFaction',political);
      await fill('[aria-label="Political event type"]','Trade Deal',political);
      const tradeDeal=[...document.querySelectorAll('[role=option]')].find(e=>optionText(e)==='Trade Deal');
      check(tradeDeal,'missing Political event type: Trade Deal'); tradeDeal.click(); await pause();
      await fill('section:nth-of-type(1) textarea','came to repay a debt',director());
      dispatch(1).click(); await pause();
      document.dispatchEvent(new Event('visibilitychange')); await pause();
      check(dispatch(1).textContent.trim()==='Dispatch Story' && !heading(1).textContent.includes(' — queued:'),
        'dashboard reopen did not clear queued state');
      await fill('section:nth-of-type(3) textarea','A diplomatic exchange',director());
      dispatch(3).click(); await pause();
      check(dispatch(3).textContent.trim()==='Sent','Political dispatch stopped showing its Sent flash');
      window.setTimeout=nativeTimeout;
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['director_busy_line'],headers:expected,storyDispatchDisabledWhileBusy:true,socialDispatchEnabledWhileBusy:true,inactiveSuffixesAbsent:true,storyDraftPreservedAcrossTabs:true,queuedClearsOnStateAndTimeout:true,politicalSentFlash:true});
      return;
    }
    if (location.hash === '#generate_reroll') {
      const story = () => document.querySelector('section');
      const narration = () => story().querySelector('textarea');
      const requests = [];
      const roll = async fields => {
        story().querySelector('summary button').click(); await pause();
        const request = JSON.parse(payloads.at(-1)[1]); requests.push(request);
        window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form:'story',ok:true,fields})); await pause();
        const stop=[...document.querySelectorAll('button')].find(e=>e.textContent.trim()==='Stop');
        if (stop) { stop.click(); await pause(); }
        return request;
      };
      const a = await roll({npcName:'Nazeem',storyType:'ambush',narration:'was hired to collect an unpaid debt',sender:'Guard 1'});
      check((a.fields.npcName ?? '') !== '' && (a.fields.storyType ?? '') === '' && button('Story type').value === 'Ambush', 'auto Generate did not preselect its NPC or pick its type');
      check(narration().value === 'was hired to collect an unpaid debt', 'first Generate did not fill narration A');
      const b = await roll({npcName:'Guard 2',storyType:'road_encounter',narration:'set out to sell pelts in Helgen',sender:'Guard 2',destination:'Helgen'});
      check(narration().value === 'set out to sell pelts in Helgen', 'second Generate did not replace narration A with B');
      check((b.fields.narration ?? '') === '' && b.fields.storyType === 'ambush' && b.fields.sender === 'Guard 1', 'reroll did not blank generated text and retain generated type/sender');
      check(b.fields.npcName === a.fields.npcName && story().querySelector('input').value === a.fields.npcName && button('Story type').value === 'Ambush' &&
        story().querySelector('input[aria-label="Hired by"]').value === 'Guard 1', 'reroll replaced generated identities or type');
      story().querySelector('[aria-label="Clear NPC"]').click(); await pause();
      const repicked = await roll({npcName:'Guard 2',storyType:'road_encounter',narration:'came to collect a debt',sender:'Guard 2'});
      check(repicked.fields.npcName && repicked.fields.storyType === 'ambush' && story().querySelector('input').value === repicked.fields.npcName &&
        button('Story type').value === 'Ambush', 'clearing NPC did not re-pick only that identity');
      await fill('input[placeholder="e.g. Saadia"]', 'Saadia');
      await fill('section textarea', 'set out to buy salt in Riften');
      const c = await roll({npcName:'Nazeem',storyType:'ambush',narration:'replacement must not win',sender:'Guard 2'});
      check(c.fields.narration === 'set out to buy salt in Riften' && narration().value === c.fields.narration, 'typing into generated narration did not preserve it');
      check(c.fields.npcName === 'Saadia' && story().querySelector('input').value === 'Saadia', 'user-typed identity did not survive Generate');
      for (const [type, example] of [['Ambush','e.g. was hired by the Thieves Guild to collect a debt'],['Road Encounter','e.g. set out to sell pelts in Helgen']]) {
        await choose('Story type', type);
        check(narration().placeholder === example, type + ' placeholder did not change');
      }
      const label = [...story().querySelectorAll('label')].find(e=>e.textContent.startsWith('Narration'));
      const hint = label.parentElement.querySelector('.director-hint');
      check(getComputedStyle(hint).fontSize === '11px' && getComputedStyle(hint).color === 'rgb(156, 163, 175)' && label.querySelector('.text-red-400'), 'hint is not readable or required label star lost');
      [...story().querySelectorAll('button')].find(e=>e.textContent.trim()==='Clear').click(); await pause();
      const cleared = await roll({npcName:'Guard 1',storyType:'seek_player',narration:'came to repay a debt'});
      check(cleared.fields.npcName && (cleared.fields.narration ?? '') === '' && story().querySelector('input').value === cleared.fields.npcName, 'Clear retained user ownership');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['generate_reroll'],requests});
      return;
    }
    if (location.hash === '#story_type_hints') {
      const story = () => document.querySelector('section');
      const text = () => [...story().querySelectorAll('p, span')].map(e=>e.textContent).join('\n');
      check(text().includes('What they will do.') && text().includes('Why the NPC acts; used as story context.'), 'auto hints changed');
      await choose('Story type', 'Road Encounter');
      check(text().includes('Appears on the road ahead of you, travelling to Destination on their own errand (outdoors only).'), 'road encounter type hint missing');
      check(text().includes('Why they are travelling, e.g. set out to sell pelts in Helgen.') && text().includes('Where they are heading. Required: a real Skyrim location.'), 'road narration or Destination hint missing');
      story().querySelector('summary button').click(); await pause();
      const road = JSON.parse(payloads.at(-1)[1]);
      check(payloads.at(-1)[0] === 'generateDirector' && road.fields.storyType === 'road_encounter' && road.schema.narration.hint.includes('Why they are travelling') && road.schema.narration.hint.includes('outdoors only') && road.variants.road_encounter.destination.hint.includes('Where they are heading.'), 'Generate missing road guidance');
      clickText('Stop'); await pause();
      await choose('Story type', 'Message');
      check(text().includes('Sends a nearby courier to deliver a message from the selected NPC.') && text().includes('Why the sender wanted contact.'), 'message hints did not replace road hints');
      check(!text().includes('Where they are heading.') && !text().includes('Why they are travelling'), 'road hints survived type switch');
      check(text().includes('Optional. If set with meetTime, creates a meeting invitation.'), 'message destination hint missing');
      story().querySelector('summary button').click(); await pause();
      const message = JSON.parse(payloads.at(-1)[1]);
      check(message.schema.narration.hint.includes('Why the sender wanted contact.') && !message.schema.narration.hint.includes('travelling'), 'Generate retained previous type guidance');
      clickText('Stop'); await pause();
      document.querySelector('[aria-label="Clear Story type"]').click(); await pause();
      check(text().includes('What they will do.') && text().includes('Why the NPC acts; used as story context.'), 'auto generic hints not restored');
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
          const headers = [...document.querySelectorAll('.section-header')].filter(e => e.checkVisibility());
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
    if (location.hash === '#npc_picker_consistent') {
      const nearby = Array.from({length:30},(_,i)=>({formId:108196+i,name:i?'Guard '+i:'Nazeem'}));
      const known = ['Aela the Huntress','Balgruuf the Greater','Camilla Valerius','Farengar Secret-Fire',
        'Maven Black-Briar','Sven','Whiterun citizen', ...Array.from({length:5000},(_,i)=>`Indexed citizen ${String(i).padStart(4,'0')}`)].sort();
      const fixture = {config:{},pluginConfig:{'ui.scale':1},tasks:[],scheduled:[],
        loadedNpcs:nearby,knownNpcs:known,
        actions:['FetchPerson','SearchForActor','DeliverMessage','EscortTarget','ScheduleFetch','ScheduleDelivery',
          'ChangeSpeed'].map(name=>({name,enabled:true,params:name==='ChangeSpeed'?[{name:'newSpeed',enum:['0','1','2']}]:[]})),
        politics:{enabled:true,factions:[{id:'ImperialFaction',name:'Imperial Legion'},{id:'StormcloakFaction',name:'Stormcloaks'}],relations:[],wars:[],player_standings:[],events:[]}};
      window.updateFullState(JSON.stringify(fixture));
      await pause();
      const pickers = () => [...document.querySelectorAll('[data-npc-picker]')];
      const input = placeholder => document.querySelector(`[data-npc-picker] input[placeholder="${placeholder}"]`);
      const options = () => [...document.querySelectorAll('[role=option]')];
      const chooseOption = async label => {
        const option=options().find(e=>e.textContent.trim()===label);
        check(option,`missing NPC picker option: ${label}`); option.click(); await pause();
      };
      const open = async control => { control.focus(); control.click(); await pause(); };
      const storyInput = () => input('e.g. Saadia');
      check(pickers().length===4, 'Story, both Social identities, and Action do not share NPC picker');
      check(pickers().every(w=>w.querySelector('[role=combobox]')), 'a Director NPC picker lacks combobox role');
      const story=storyInput();
      await open(story);
      check(options().length===50 && options()[0].textContent.trim()==='Nazeem (nearby)' &&
        options()[29].textContent.trim()==='Guard 29 (nearby)' &&
        options()[30].textContent.trim()==='Aela the Huntress',
        'nearby-first ordering, nearby markers, known names, or 50-row cap failed');
      await fill('[placeholder="e.g. Saadia"]','Camilla');
      check(options().length===1 && options()[0].textContent.trim()==='Camilla Valerius','typing did not filter known NPCs');
      await chooseOption('Camilla Valerius');
      check(story.value==='Camilla Valerius','known non-nearby NPC was not selected');
      const {knownNpcs: omitted, ...periodic} = fixture;
      window.updateFullState(JSON.stringify(periodic)); await pause();
      await choose('Story type','Message');
      check(story.value==='Camilla Valerius','Message sender did not use the Story NPC picker');
      clickText('Clear'); await pause();
      check(story.value==='', 'Director Clear did not clear the picked NPC');
      await fill('[placeholder="e.g. Saadia"]','Saadia');
      const social1=input('e.g. Mikael'), social2=input('e.g. Sven');
      await fill('[placeholder="e.g. Mikael"]','Aela');
      check(options().length===1 && options()[0].textContent.trim()==='Aela the Huntress',
        'Social NPC picker did not filter known names');
      await chooseOption('Aela the Huntress');
      await fill('[placeholder="e.g. Sven"]','Maven');
      await chooseOption('Maven Black-Briar');
      check(social1.value==='Aela the Huntress' && social2.value==='Maven Black-Briar',
        'Social NPC identities did not retain selected names');
      await choose('Story type','Informant');
      check(pickers().length===6 && input('e.g. Camilla Valerius') && input('e.g. Sven'),
        'Informant subject/source fields do not use NPC picker');
      await choose('Story type','Ambush');
      const hirer=input('e.g. Maven Black-Briar');
      check(hirer && pickers().length===5,'Ambush hired-by field does not use NPC picker');
      await open(hirer);
      check(options().some(e=>e.textContent.trim()==='Imperial Legion (faction)'),
        'Hired by picker omitted labeled faction suggestion');
      await chooseOption('Imperial Legion (faction)');
      await fill('section:nth-of-type(1) textarea','was hired by the Imperial Legion');
      clickText('Dispatch Story'); await pause();
      check(JSON.parse(payloads.at(-1)[1]).sender==='ImperialFaction','faction suggestion lost faction-ID mapping');
      await choose('Story type','Stalker');
      check(input('e.g. Faendal'),'Encouraged by field does not use NPC picker');
      await choose('Story type','Quest');
      await choose('Sub-type','rescue');
      check(input('e.g. Jarl Balgruuf') && input('e.g. Camilla Valerius'),
        'quest giver or rescue victim does not use NPC picker');
      const action=document.querySelector('[aria-label="Action NPC"]');
      await open(action);
      check(action.closest('[data-npc-picker]') && options().length===30 &&
        options().every(e=>e.textContent.trim().endsWith('(nearby)')),
        'Action NPC is not the shared nearby-only picker');
      await chooseOption('Guard 2 (nearby)');
      check(action.value==='Guard 2','Action NPC selection displayed a marker');
      await choose('Action','ChangeSpeed'); await choose('New speed','Jog');
      clickText('Execute Action'); await pause();
      check(JSON.parse(payloads.at(-1)[1]).npcFormId===108198,'Action picker did not preserve selected formId');
      for (const [name,placeholder] of [
        ['FetchPerson','e.g. Camilla Valerius, Alvor'],['SearchForActor','e.g. Lydia, Faendal'],
        ['DeliverMessage','e.g. Jarl Balgruuf, Camilla'],['EscortTarget','e.g. Camilla Valerius, Sven'],
        ['ScheduleFetch','e.g. Camilla Valerius'],['ScheduleDelivery','e.g. Jarl Balgruuf']
      ]) {
        await choose('Action',name);
        check(input(placeholder),`${name} targetName does not use NPC picker`);
      }
      check(pickers().every(w=>w.querySelector('input[role=combobox]')),
        'Director NPC text controls are not all combobox inputs');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',
        checks:['npc_picker_consistent'],directorPickers:pickers().length,knownFixtureCount:known.length,knownFixtureBytes:new TextEncoder().encode(JSON.stringify(known)).length});
      return;
    }
    if (location.hash === '#director_live_polish2') {
      document.querySelector('.director-tab').closest('.fixed').style.zoom=1.3;
      if (reloadReady) {
        check(localStorage.getItem('intelengine.activeTab')==='actions' &&
          [...document.querySelectorAll('button')].find(e=>e.textContent.trim()==='Actions')?.classList.contains('text-blue-400') &&
          document.querySelector('.director-tab').closest('[hidden]'),'active Actions tab did not survive reload');
        clickText('Director'); await pause();
      }
      const story=document.querySelector('section:nth-of-type(1)'), social=document.querySelector('section:nth-of-type(2)');
      const political=document.querySelector('section:nth-of-type(3)'), action=document.querySelector('section:nth-of-type(4)');
      const checkRow=(host,labels,name)=>{
        const fields=labels.map(label=>[...host.querySelectorAll('label')].find(e=>e.textContent.trim().startsWith(label))?.parentElement);
        check(fields.every(Boolean),`${name}: row fields missing`);
        check(fields.every(field=>Math.abs(field.getBoundingClientRect().top-fields[0].getBoundingClientRect().top)<3),
          `${name}: fields do not share one row`);
      };
      if (!reloadReady) await fill('section:nth-of-type(1) textarea','Textarea clear alignment');
      const clear=story.querySelector('[aria-label="Clear Narration"]');
      const textarea=story.querySelector('textarea'), clearRect=clear.getBoundingClientRect(), areaRect=textarea.getBoundingClientRect();
      check(clear && Math.abs(clearRect.right-areaRect.right)<7 && Math.abs(clearRect.top-areaRect.top)<7,
        'textarea clear is not in the top-right corner');
      const npcInput=story.querySelector('input[placeholder="e.g. Saadia"]');
      if (!reloadReady) await fill('input[placeholder="e.g. Saadia"]','Saadia');
      const npc=story.querySelector('[aria-label="Clear NPC"]');
      check(Math.abs((npc.getBoundingClientRect().top+npc.getBoundingClientRect().bottom-
        npcInput.getBoundingClientRect().top-npcInput.getBoundingClientRect().bottom)/2)<2,
        'input clear is not vertically centered');
      const handle=textarea.parentElement.querySelector('.director-resize-handle');
      check(handle?.getAttribute('aria-label')==='Resize Narration','shared resize handle is missing or inaccessible');
      const drag = async delta => {
        handle.scrollIntoView({block:'center'}); await pause();
        const rect=handle.getBoundingClientRect();
        window.__dragDone=false;
        document.getElementById('result').textContent='DRAG:'+JSON.stringify({x:rect.left+4,y:rect.top+4,delta});
        while (!window.__dragDone) await pause();
      };
      const beforeHeight=textarea.getBoundingClientRect().height;
      await drag(100);
      check(textarea.getBoundingClientRect().height>beforeHeight+40,'real pointer drag did not increase textarea height');
      await drag(1000);
      check(textarea.offsetHeight===400,'resize drag did not clamp at 400px');
      await drag(-1000);
      check(Math.abs(textarea.getBoundingClientRect().height-beforeHeight)<2,'resize drag did not clamp at current rows height');
      checkRow(social,['NPC 1','NPC 2','Social type'],'Social');
      const socialNarration=[...social.querySelectorAll('label')].find(e=>e.textContent.trim().startsWith('Narration')).parentElement;
      check(socialNarration.getBoundingClientRect().width>social.getBoundingClientRect().width*.8,
        'Social Narration is not full width');
      const storeKey='intelengine.director.drafts';
      if (reloadReady) {
        const drafts=JSON.parse(localStorage.getItem(storeKey)||'{}');
        check(story.querySelector('textarea').value==='Saadia reload draft' &&
          social.querySelector('textarea').value==='Mikael reload draft' &&
          political.querySelector('textarea').value==='Imperial reload draft',
          'Director textarea drafts did not survive page reload');
        check(drafts.story?.[2]==='Saadia reload draft' && drafts.social?.[3]==='Mikael reload draft' &&
          drafts.political?.[3]==='Imperial reload draft' && drafts.action?.[1]==='ChangeSpeed' &&
          drafts.action?.[2]?.newSpeed==='1',
          'one or more Director form values were not saved');
        check(drafts.touched?.npcName===true && drafts.touched?.storyType===true &&
          drafts.touched?.narration===true && drafts.touched?.npc1Name===true &&
          drafts.touched?.socialType===true && drafts.touched?.factionA===true &&
          drafts.touched?.eventType===true,
          'Director touched state did not survive page reload');
        check(npcInput.value==='Saadia' && button('Story type').value==='Message' &&
          social.querySelector('input[placeholder="e.g. Mikael"]').value==='Mikael' &&
          button('Social type').value==='Gossip' && button('Political event type').value==='Insult' &&
          political.querySelector('input[placeholder="e.g. StormcloakFaction"]').value==='ImperialFaction' &&
          button('Action NPC').value==='Nazeem' && button('Action').value==='ChangeSpeed' &&
          button('New speed').value==='Jog',
          'Director identity/type/Action form values did not survive page reload');
        story.querySelector('summary button').click(); await pause();
        const request=payloads.filter(([name,json])=>name==='generateDirector' && !JSON.parse(json).outcome).map(([,json])=>JSON.parse(json)).at(-1);
        check(request?.touched.narration===true && request.fields.narration==='Saadia reload draft' &&
          request.touched.npcName===true && request.fields.npcName==='Saadia',
          'restored user ownership was not retained in Generate');
        [...story.querySelectorAll('button')].find(e=>e.textContent.trim()==='Stop').click(); await pause();
        for (const [form, host] of [['story',story],['social',social],['political',political]]) {
          [...host.querySelectorAll('summary button')].find(e=>e.textContent.trim()==='Clear').click(); await pause();
          check(!JSON.parse(localStorage.getItem(storeKey)||'{}')[form],`Clear retained ${form} saved draft`);
        }
        localStorage.removeItem('__directorLivePolish2Reload');
        document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['director_live_polish2'],reload:'CDP Page.reload'});
        return;
      }
      await fill('section:nth-of-type(1) textarea','Saadia reload draft');
      await fill('section:nth-of-type(2) textarea','Mikael reload draft');
      await fill('section:nth-of-type(3) textarea','Imperial reload draft');
      await fill('input[placeholder="e.g. Saadia"]','Saadia');
      await fill('input[placeholder="e.g. Mikael"]','Mikael',social);
      await fill('input[placeholder="e.g. StormcloakFaction"]','ImperialFaction',political);
      const types=[['Story type','Message'],['Social type','Gossip'],['Political event type','Insult']];
      for(const [label,value] of types) await choose(label,value);
      await choose('Action NPC','Nazeem');
      await choose('Action','ChangeSpeed');
      await choose('New speed','Jog');
      const drafts=JSON.parse(localStorage.getItem(storeKey)||'{}');
      check(['story','social','political','action'].every(form=>Array.isArray(drafts[form])) &&
        drafts.touched && typeof drafts.touched==='object',
        'Director form drafts/touched were not saved to localStorage');
      localStorage.setItem('__directorLivePolish2Reload','ready');
      clickText('Actions'); await pause();
      check(localStorage.getItem('intelengine.activeTab')==='actions','active tab was not saved as Actions');
      document.getElementById('result').textContent='RELOAD_READY';
      return;
    }
    if (location.hash === '#director_live_polish') {
      const story=document.querySelector('section:nth-of-type(1)'), social=document.querySelector('section:nth-of-type(2)');
      const political=document.querySelector('section:nth-of-type(3)');
      const requests=()=>payloads.filter(([name,json])=>name==='generateDirector' && !JSON.parse(json).outcome).map(([,json])=>JSON.parse(json));
      const clearButton=(host,label)=>host.querySelector(`[aria-label="Clear ${label}"]`);
      await choose('Social type','Gossip');
      const gossipLabel=[...social.querySelectorAll('label')].find(e=>e.textContent.trim().startsWith('Gossip'));
      const gossipInput=gossipLabel?.parentElement.querySelector('input');
      check(gossipInput,'Social Gossip text input is missing');
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(gossipInput,'saw an argument');
      gossipInput.dispatchEvent(new Event('input',{bubbles:true})); await pause();
      check(gossipInput?.closest('.director-text-field') && clearButton(social,'Gossip')?.checkVisibility(),
        'direct text input lacks conditional clear wrapper');
      clearButton(social,'Gossip').click(); await pause();
      check(gossipInput.value==='' && !clearButton(social,'Gossip')?.checkVisibility(),
        'text input clear did not clear/hide');
      const socialTextarea=social.querySelector('textarea');
      await fill('section:nth-of-type(2) textarea','A typed social draft');
      check(socialTextarea.closest('.director-text-field')?.contains(clearButton(social,'Narration')) &&
        clearButton(social,'Narration')?.checkVisibility(),'textarea clear button/wrapper did not appear');
      clearButton(social,'Narration').click(); await pause();
      check(socialTextarea.value==='' && !clearButton(social,'Narration')?.checkVisibility(),
        'textarea clear did not clear/hide');
      social.querySelector('summary button').click(); await pause();
      let request=requests().at(-1);
      check(!Object.hasOwn(request.fields,'gossip') && request.blank.includes('gossip') && !request.touched.gossip &&
        !Object.hasOwn(request.fields,'narration') && request.blank.includes('narration') && !request.touched.narration,
        'cleared text controls were submitted as touched');
      clickText('Stop'); await pause();
      const factionA=political.querySelector('input[placeholder="e.g. StormcloakFaction"]');
      await fill('input[placeholder="e.g. StormcloakFaction"]','Stormcloaks',political);
      const pickerClear=political.querySelector('[aria-label="Clear faction"]');
      check(pickerClear?.checkVisibility(),'Faction A picker lacks clear button');
      const pickerRect=factionA.getBoundingClientRect(), clearRect=pickerClear.getBoundingClientRect();
      check(Math.abs((pickerRect.top+pickerRect.bottom-clearRect.top-clearRect.bottom)/2)<2,
        'faction picker clear is not vertically centered');
      pickerClear.click(); await pause();
      check(factionA.value==='' && !political.querySelector('[aria-label="Clear faction"]')?.checkVisibility(),
        'faction picker clear did not clear/hide');
      political.querySelector('summary button').click(); await pause();
      request=requests().at(-1);
      check(!Object.hasOwn(request.fields,'factionA') && request.blank.includes('factionA') && !request.touched.factionA,
        'cleared faction picker value was submitted as touched');
      clickText('Stop'); await pause();
      await fill('input[placeholder="e.g. StormcloakFaction"]','Stormcloaks',political);
      await fill('input[placeholder="e.g. StormcloakFaction"]','',political);
      political.querySelector('summary button').click(); await pause();
      request=requests().at(-1);
      check(!Object.hasOwn(request.fields,'factionA') && request.blank.includes('factionA') && !request.touched.factionA,
        'manually deleted faction was submitted as touched');
      clickText('Stop'); await pause();
      const storyNpc=story.querySelector('input[placeholder="e.g. Saadia"]');
      await fill('input[placeholder="e.g. Saadia"]','Saadia');
      const npcRect=storyNpc.getBoundingClientRect(), npcClearRect=clearButton(story,'NPC').getBoundingClientRect();
      check(Math.abs((npcRect.top+npcRect.bottom-npcClearRect.top-npcClearRect.bottom)/2)<2,
        'NPC clear is not vertically centered');
      await choose('Story type','Message');
      story.querySelector('summary button').click(); await pause();
      request=requests().at(-1);
      const beforeMessage=requests().length;
      window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form:'story',ok:true,
        fields:{npcName:'Other',storyType:'message',narration:'Generated message reason'}})); await pause();
      let followup=requests().at(-1);
      check(followup.followUp===true && followup.fields.npcName==='Saadia' &&
        followup.fields.narration==='Generated message reason',
        'missing required Message variant field did not follow up with merged text/identity');
      check(requests().length===beforeMessage+1,'Message follow-up was not exactly once');
      window.directorGenerateResult(JSON.stringify({requestId:followup.requestId,form:'story',ok:true,
        fields:{msgContent:'Meet at the Bannered Mare',narration:'Must not replace merged narration',npcName:'Other'}})); await pause();
      check(requests().length===beforeMessage+1 && storyNpc.value==='Saadia' &&
        story.querySelector('textarea').value==='Generated message reason',
        'complete Message result triggered another follow-up or replaced preserved fields');
      await choose('Story type','Informant');
      const gossipField=[...story.querySelectorAll('label')].find(e=>e.textContent.trim().startsWith('Gossip'));
      check(gossipField?.parentElement.querySelector('.director-hint')?.textContent.trim()==='What the Subject NPC did, told as a rumour, e.g. "was seen arguing with Adrianne over an unpaid debt". Their name is added automatically.',
        'Informant gossip hint is not exact');
      story.querySelector('summary button').click(); await pause();
      request=requests().at(-1);
      const beforeInformant=requests().length;
      window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form:'story',ok:true,
        fields:{storyType:'informant',narration:'Generated informant account'}})); await pause();
      followup=requests().at(-1);
      check(followup.followUp===true && followup.fields.narration==='Generated informant account',
        'missing required Informant fields did not follow up with merged narration');
      check(requests().length===beforeInformant+1,'Informant follow-up was not exactly once');
      window.directorGenerateResult(JSON.stringify({requestId:followup.requestId,form:'story',ok:true,
        fields:{gossip:'was seen arguing over an unpaid debt'}})); await pause();
      check(requests().length===beforeInformant+1 && story.querySelector('[aria-label="Subject NPC"]').value!=='',
        'still-incomplete follow-up looped or lost the preselected subject');
      await fill('section:nth-of-type(1) textarea','');
      story.querySelector('summary button').click(); await pause();
      request=requests().at(-1);
      const beforeBase=requests().length;
      window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form:'story',ok:true,
        fields:{storyType:'informant',subject:'Mikael',gossip:'was seen arguing over a debt'}})); await pause();
      followup=requests().at(-1);
      check(followup.followUp===true && (followup.fields.narration ?? '')==='',
        'missing required base narration did not trigger one follow-up');
      check(requests().length===beforeBase+1,'base narration follow-up was not exactly once');
      window.directorGenerateResult(JSON.stringify({requestId:followup.requestId,form:'story',ok:true,
        fields:{narration:'Completed base narrative'}})); await pause();
      check(requests().length===beforeBase+1,'complete base narration triggered another follow-up');
      story.querySelector('summary button').click(); await pause();
      request=requests().at(-1);
      const beforeComplete=requests().length;
      window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form:'story',ok:true,
        fields:{narration:'A complete reroll',gossip:'was seen settling an old debt'}})); await pause();
      check(requests().length===beforeComplete && story.querySelector('textarea').value==='A complete reroll',
        'complete initial result triggered a follow-up or did not reroll generated text');
      await choose('Social type','Gossip');
      const socialNarration=social.querySelector('textarea'), type=button('Social type');
      check(socialNarration.getBoundingClientRect().width>social.getBoundingClientRect().width*.8 &&
        type.getBoundingClientRect().width<socialNarration.getBoundingClientRect().width,
        'Social narration is not full width or type control is not narrow');
      check(getComputedStyle(socialNarration).resize==='vertical' &&
        getComputedStyle(socialNarration).backgroundImage.includes('gradient'),'textarea resize grip is missing');
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['director_live_polish']});
      return;
    }
    if (['#director_polish', '#director_field_layout'].includes(location.hash)) {
      const failures = [];
      const expect = (condition, message, details) => {
        if (!condition) failures.push({message, ...(details || {})});
      };
      const section = index => document.querySelector(`section:nth-of-type(${index})`);
      const hint = (host, prefix) => [...host.querySelectorAll('p, span')].find(p=>p.textContent.trim().startsWith(prefix));
      const field = (host, labelText, controlSelector, hintText, required) => {
        const label = [...host.querySelectorAll('label')].find(e=>e.textContent.trim().startsWith(labelText));
        const control = label?.parentElement.querySelector(controlSelector);
        const help = label && hint(label.parentElement, hintText);
        const rect = node => {
          const r=node?.getBoundingClientRect();
          return r&&{x:r.x,y:r.y,width:r.width,bottom:r.bottom};
        };
        const details = {label:label?.textContent.trim(),labelClass:label?.className,control:control?.outerHTML?.slice(0,100),
          hint:help?.textContent.trim(),labelRect:rect(label),controlRect:rect(control),hintRect:rect(help)};
        const labelBeforeControl = !!label && !!control &&
          !!(label.compareDocumentPosition(control) & Node.DOCUMENT_POSITION_FOLLOWING);
        const controlBeforeHint = !!control && !!help &&
          !!(control.compareDocumentPosition(help) & Node.DOCUMENT_POSITION_FOLLOWING);
        expect(!!label && !!control && !!help && labelBeforeControl && controlBeforeHint,
          `${labelText}: label/control/hint DOM order is wrong`, details);
        expect(label?.classList.contains('text-[10px]') && label?.classList.contains('text-gray-500') &&
          (label?.querySelector('.text-red-400') !== null) === required &&
          !help?.querySelector('.text-red-400'),
          `${labelText}: label variant or required star is wrong`, details);
        expect(!!label && !!control && !!help && (() => {
          const l=label.getBoundingClientRect(), c=control.getBoundingClientRect(), h=help.getBoundingClientRect();
          return l.y<=c.y && h.y>=c.bottom-1 && Math.abs(l.x-c.x)<=2 && Math.abs(c.x-h.x)<=2;
        })(), `${labelText}: label/control/hint geometry is not vertically stacked and aligned`, details);
        return help;
      };
      const story = section(1), social = section(2), politics = section(3), action = section(4);
      const baseHints = [
        field(story,'NPC','input[placeholder="e.g. Saadia"]','Exact display name.',true),
        field(story,'Story type','[aria-label="Story type"]','What they will do.',true),
        field(story,'Narration','textarea','Why the NPC acts; used as story context.',true),
        field(social,'NPC 1','input[placeholder="e.g. Mikael"]','The traveler/teller.',true),
        field(social,'NPC 2','input[placeholder="e.g. Sven"]','The counterpart/listener.',true),
        field(social,'Social type','[aria-label="Social type"]','Interaction or gossip.',true),
        field(social,'Narration','textarea','What happens between them.',true),
        field(politics,'Faction A','input[placeholder="e.g. StormcloakFaction"]','First side.',true),
        field(politics,'Faction B','input[placeholder="e.g. ThalmorFaction"]','Other side.',true),
        field(politics,'Event','[aria-label="Political event type"]','What happened.',true),
        field(politics,'Relation delta','input[type=number]','Change, -15 to 15.',false),
        field(politics,'Description','textarea','A short account of the political event.',true),
        field(action,'NPC','[aria-label="Action NPC"]','Nearby actor who performs the action.',true),
        field(action,'Action','[aria-label="Action"]','Command to run.',true)
      ];
      const starSpans = [...document.querySelectorAll('section .text-red-400')];
      const badStars = starSpans.filter(s=>getComputedStyle(s).color!=='rgb(248, 113, 113)');
      const rules = badStars.map(star=>{
        const matched=[];
        for (const sheet of document.styleSheets) {
          try { for (const rule of sheet.cssRules || []) if (rule.selectorText && star.matches(rule.selectorText)) matched.push({selector:rule.selectorText,style:rule.style?.cssText}); }
          catch (_) {}
        }
        return {text:star.parentElement?.textContent,computed:getComputedStyle(star).color,matches:matched};
      });
      expect(starSpans.length>0 && !badStars.length, 'required stars do not compute to red-400', {starCount:starSpans.length,badStars:rules});
      const textareas = [...document.querySelectorAll('section textarea')];
      const badTextareas=textareas.filter(t=>getComputedStyle(t).resize!=='vertical' || parseFloat(getComputedStyle(t).minHeight)<48);
      expect(textareas.length>0 && !badTextareas.length, 'Director textareas lack vertical resize or sensible minimum height',
        {textareas:textareas.map(t=>({placeholder:t.placeholder,resize:getComputedStyle(t).resize,minHeight:getComputedStyle(t).minHeight}))});
      const storyHint = baseHints[2];
      const storyStyle = storyHint && getComputedStyle(storyHint);
      const storyContract = element => element?.classList.contains('director-hint') &&
        getComputedStyle(element).fontSize===storyStyle?.fontSize &&
        getComputedStyle(element).color===storyStyle?.color &&
        storyStyle?.fontSize==='11px' && storyStyle?.color==='rgb(156, 163, 175)';
      expect(baseHints.length===14 && baseHints.every(storyContract),
        'base fields do not retain shared director-hint style',
        {hints:baseHints.map(e=>e&&{text:e.textContent.trim(),className:e.className,fontSize:getComputedStyle(e).fontSize,color:getComputedStyle(e).color})});
      await choose('Action','FetchPerson');
      const refusalHint=[...action.querySelectorAll('.director-hint')].find(e=>e.textContent.trim()==='Optional. Leave empty and the person comes along. If filled, they refuse to come and give this reason.');
      expect(!!refusalHint,'FetchPerson visible refusal hint is missing or changed',
        {hints:[...action.querySelectorAll('.director-hint')].map(e=>e.textContent.trim())});
      await choose('Action','GoToLocation');
      const dynamicHint=[...action.querySelectorAll('.director-hint')].find(e=>e.textContent.includes('Named location'));
      expect(storyContract(dynamicHint), 'Action Execution dynamic hint does not share the readable director-hint style with Story',
        {dynamic:dynamicHint&&{tag:dynamicHint.tagName,className:dynamicHint.className,fontSize:getComputedStyle(dynamicHint).fontSize,color:getComputedStyle(dynamicHint).color},
          story:storyHint&&{className:storyHint.className,fontSize:storyStyle.fontSize,color:storyStyle.color}});
      document.getElementById('result').textContent=JSON.stringify({status:failures.length?'FAIL':'PASS',
        checks:[location.hash.slice(1)],starColors:starSpans.map(s=>({text:s.parentElement?.textContent.trim(),className:s.className,color:getComputedStyle(s).color})),failures});
      return;
    }
    if (location.hash === '#director_required_fields') {
      const story = () => document.querySelector('section:nth-of-type(1)');
      const dispatch = () => [...story().querySelectorAll('button')].find(e => e.textContent.trim() === 'Dispatch Story');
      const labels = () => [...story().querySelectorAll('label')].map(e => e.textContent);
      const missing = () => [...story().querySelectorAll('p')].find(e => e.textContent.startsWith('Fill required fields:'));
      check(['NPC *', 'Story type *', 'Narration *'].every(text => labels().includes(text)), 'Story required field labels are not marked');
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
      await fill('input[placeholder="e.g. Saadia"]', 'Saadia');
      check(dispatch().disabled && missing()?.textContent === 'Fill required fields: Narration', 'filled NPC remained missing');
      await choose('Story type', 'Message');
      await fill('section:nth-of-type(1) textarea', 'Requests a private meeting');
      await fill('input[placeholder="e.g. I need to speak with you about a private matter"]', 'Meet me at the inn');
      check(!dispatch().disabled && !missing(), 'valid Story remains disabled or shows missing fields');
      const section = index => document.querySelector(`section:nth-of-type(${index})`);
      const submit = index => [...section(index).querySelectorAll('button')].at(-1);
      const missingIn = index => [...section(index).querySelectorAll('p')].find(e => e.textContent.startsWith('Fill required fields:'))?.textContent;
      const labeled = (container, name) => [...container.querySelectorAll('label')].find(e => e.textContent.trim().startsWith(name));
      const controlFor = label => label?.parentElement?.querySelector('input, textarea');
      const redStar = label => [...(label?.parentElement?.querySelectorAll('span') || [])].some(e => e.textContent === '*' && e.classList.contains('text-red-400'));
      await choose('Story type', 'Informant');
      check(['Subject NPC', 'Gossip'].every(name => redStar(labeled(story(), name))), 'Informant required fields are not marked with red stars');
      check(submit(1).disabled && missingIn(1) === 'Fill required fields: Subject NPC, Gossip', 'Informant missing list does not name Subject NPC and Gossip');
      for (const name of ['Subject NPC', 'Gossip']) {
        const control = controlFor(labeled(story(), name));
        check(control, `Informant ${name} control missing`);
        const value = name === 'Subject NPC' ? 'Mikael' : 'saw a suspicious meeting';
        Object.getOwnPropertyDescriptor(control.tagName === 'TEXTAREA' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype, 'value').set.call(control, value);
        control.dispatchEvent(new Event('input', { bubbles: true })); await pause();
      }
      clickText('Generate'); await pause();
      const informantRequest = JSON.parse(payloads.at(-1)[1]);
      check(informantRequest.variants?.informant?.subject?.required === true &&
        informantRequest.variants?.informant?.gossip?.required === true, 'Generate omitted required Informant metadata');
      clickText('Stop'); await pause();
      check(!submit(1).disabled && !missingIn(1), 'completed Informant remains blocked');
      await fill('input[placeholder="e.g. Mikael"]', 'Mikael', section(2));
      await fill('input[placeholder="e.g. Sven"]', 'Sven', section(2));
      await fill('section:nth-of-type(2) textarea', 'Discussed a shipment');
      await choose('Social type', 'Interaction');
      const social = section(2);
      check(['NPC 1 memory', 'NPC 2 memory'].every(name => redStar(labeled(social, name))), 'Interaction memories are not marked with red stars');
      check(submit(2).disabled && missingIn(2) === 'Fill required fields: NPC 1 memory, NPC 2 memory', 'Interaction missing list does not name both memories');
      for (const [name, value] of [['NPC 1 memory', 'settled the argument'], ['NPC 2 memory', 'accepted the settlement']]) {
        const control = controlFor(labeled(social, name));
        check(control, `${name} control missing`);
        Object.getOwnPropertyDescriptor(control.tagName === 'TEXTAREA' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype, 'value').set.call(control, value);
        control.dispatchEvent(new Event('input', { bubbles: true })); await pause();
      }
      check(!submit(2).disabled && !missingIn(2), 'completed Interaction remains blocked');
      await choose('Social type', 'Gossip');
      check(submit(2).disabled && missingIn(2) === 'Fill required fields: Gossip' && redStar(labeled(section(2), 'Gossip')), 'Gossip requirements retained inactive Interaction memories');
      await fill('input[placeholder="e.g. StormcloakFaction"]', 'StormcloakFaction');
      await fill('input[placeholder="e.g. ThalmorFaction"]', 'StormcloakFaction');
      await fill('section:nth-of-type(3) textarea', 'A diplomatic exchange');
      check(submit(3).disabled && missingIn(3) === 'Fill required fields: Faction B, Political event type', 'unpicked Political event type did not block dispatch');
      await choose('Political event type', 'Trade Deal');
      check(submit(3).disabled && missingIn(3) === 'Fill required fields: Faction B', 'same-faction validation lost its label');
      await fill('input[placeholder="e.g. ThalmorFaction"]', 'ThalmorFaction');
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
    if (location.hash === '#ambush_faction_hirer') {
      const story = document.querySelector('section:nth-of-type(1)');
      const button = text => [...story.querySelectorAll('button')].find(e=>e.textContent.trim()===text || text==='Dispatch Story' && e.textContent.trim()==='Queued');
      await fill('input[placeholder="e.g. Saadia"]','Saadia');
      await choose('Story type','Ambush');
      check([...story.querySelectorAll('.director-hint')].some(e=>e.textContent.trim()==='The NPC above turns hostile and attacks you (outdoors only). Pick someone who can fight.'), 'ambush hint does not identify the selected NPC as the attacker');
      await fill('section:nth-of-type(1) textarea','was hired by the Imperial Legion');
      await fill('input[placeholder="e.g. Maven Black-Briar"]','Imperial Legion');
      button('Dispatch Story').click(); await pause();
      check(JSON.parse(payloads.at(-1)[1]).sender==='ImperialFaction','typed faction name was not dispatched as producer faction ID');
      button('Clear').click(); await pause();
      await fill('input[placeholder="e.g. Saadia"]','Saadia');
      await choose('Story type','Stalker');
      button('Generate').click(); await pause();
      const request=JSON.parse(payloads.at(-1)[1]);
      window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form:'story',ok:true,
        fields:{npcName:'Nazeem',storyType:'stalker',narration:'confessed to following you for the Imperial Legion',sender:'Imperial Legion'}})); await pause();
      check(story.querySelector('input[placeholder="e.g. Saadia"]').value==='Saadia','generation replaced the typed NPC identity');
      button('Dispatch Story').click(); await pause();
      const generated=JSON.parse(payloads.at(-1)[1]);
      check(generated.sender==='ImperialFaction' && generated.npcName==='Saadia','generated faction sender or preserved NPC was dispatched incorrectly');
      for (const sender of ['ImperialFaction', 'Maven Black-Briar']) {
        button('Clear').click(); await pause();
        await fill('input[placeholder="e.g. Saadia"]','Saadia');
        await choose('Story type','Ambush');
        await fill('section:nth-of-type(1) textarea','had an unpaid debt');
        await fill('input[placeholder="e.g. Maven Black-Briar"]',sender);
        button('Dispatch Story').click(); await pause();
        check(JSON.parse(payloads.at(-1)[1]).sender===sender,'faction ID or NPC sender changed: '+sender);
      }
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['ambush_faction_hirer'],typed:JSON.parse(payloads.find(([name])=>name==='dispatchStory')[1]),generated});
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
        const identityKeys=Object.keys(fields).filter(key=>!key.endsWith('Type'));
        const selected=identityKeys.map((key,i)=>request.fields[key] || expected[i]);
        if (form==='social') check(selected.every(Boolean) && selected[0]!==selected[1], 'Social required NPC preselection was empty or duplicated');
        check(!request.options.npcs.includes('Sven') && !request.options.npcs.includes('Mikael'), 'fixture unexpectedly contains producer NPC names');
        window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form,ok:true,fields})); await pause(); clickText('Stop'); await pause();
        const inputs=[...document.querySelectorAll(`section:nth-of-type(${index}) input[type=text]`)].filter(e=>e.getAttribute('role')!=='combobox' || e.closest('[data-npc-picker]'));
        const identityInputs=form==='story' ? [inputs[0],inputs[1],inputs[3]] : inputs.slice(0,2);
        const values=identityInputs.map(e=>e.value);
        const audit=outcomeFor(request.requestId);
        const typeLabel={story:'Story type',social:'Social type',political:'Political event type'}[form], selectedType=button(typeLabel).value;
        if (JSON.stringify(values)!==JSON.stringify(selected) ||
            identityKeys.some(key=>audit.rejected[key] && !(request.fields[key] && audit.rejected[key]==='Preserved or unchanged'))) failures.push({form,fields,values,expected:selected,rejected:audit.rejected});
        control(index,'Generate').click(); await pause();
        const reroll=JSON.parse(payloads.at(-1)[1]);
        const replacements=Object.fromEntries(Object.entries(fields).map(([key,value])=>[key,key.endsWith('Type') ? (form==='story' ? 'ambush' : 'npc_gossip') : value && value!=='None' ? (key.startsWith('faction') ? 'ThalmorFaction0' : 'Guard 2') : value]));
        window.directorGenerateResult(JSON.stringify({requestId:reroll.requestId,form,ok:true,fields:replacements})); await pause(); clickText('Stop'); await pause();
        check(values.every((value,i)=>!value || reroll.fields[identityKeys[i]]===value), 'reroll request blanked filled identities');
        check(identityInputs.every((e,i)=>e.isConnected && e.value===(reroll.fields[identityKeys[i]] || values[i])), 'reroll replaced filled identities');
        check(button(typeLabel).value===selectedType, 'reroll replaced a filled type');
      }
      check(!failures.length, 'producer identity regressions: '+JSON.stringify(failures));
      document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:['director_generate_identities']});
      return;
    }
    if (location.hash === '#director_generate_merge_and_stop') {
      payloads.length = 0;
      check(button('Story type').placeholder==='(auto)' && button('Story type').value==='', 'untouched type is not auto');
      await fill('input[placeholder="e.g. Saadia"]','Saadia');
      await choose('Story type','Quest'); await filterPick('Story type','Quest');
      await choose('Sub-type','rescue');
      clickText('Generate');
      await pause();
      check(document.querySelector('section:nth-of-type(1) textarea').matches(':disabled') &&
        [...document.querySelectorAll('section:nth-of-type(1) button')].find(e=>e.textContent.trim()==='Dispatch Story').matches(':disabled'),
        'generating form remained editable');
      let request = JSON.parse(payloads.at(-1)[1]);
      check(payloads.at(-1)[0] === 'generateDirector' && request.fields.storyType === 'quest' &&
        request.fields.questSubType === 'rescue' && (request.fields.enemyType ?? '') === '' &&
        request.options.npcs.includes('Saadia'), 'generate omitted touched/auto/options contract');
      window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form:'story',ok:true,
        fields:{narration:'Partial plan',enemyType:'not_an_enemy',unexpected:'ignore'}})); await pause(); clickText('Stop'); await pause();
      check(document.querySelector('section:nth-of-type(1) textarea').value==='Partial plan' &&
        button('Enemy type').value==='', 'one invalid field discarded valid narrative');
      const partial = outcomeFor(request.requestId);
      check(partial.applied.includes('narration') && partial.rejected.enemyType==='Invalid option' &&
        partial.rejected.unexpected==='Unknown field', 'partial validation reasons missing');
      clickText('Generate'); await pause(); request = JSON.parse(payloads.at(-1)[1]);
      check(Object.keys(request.variants.quest).every(key => key in request.fields || request.blank.includes(key)), 'visible quest blanks omitted from request');
      check(Object.values(request.variants.quest).every(field=>!('placeholder' in field)), 'example placeholders leaked in generated variants');
      window.directorGenerateResult(JSON.stringify({requestId:request.requestId,form:'story',ok:true,
        fields:{storyType:'quest',npcName:'Other',narration:'Wants to discuss a careful plan',
          questSubType:'rescue',enemyType:'bandit',questLocation:'Fort Greymoor',sender:'None',
          msgContent:'Rescue my friend',victimName:'Guard 1',itemName:'Iron sword',itemDesc:'An old family blade'}}));
      await pause();
      check(document.querySelector('input[placeholder="e.g. Saadia"]').value==='Saadia' &&
        document.querySelector('section:nth-of-type(1) textarea').value==='Wants to discuss a careful plan' &&
        button('Story type').value==='Quest' && button('Sub-type').value==='rescue' &&
        button('Enemy type').value==='bandit', 'generated merge changed identity/touched dropdown or lost text');
      check([...document.querySelectorAll('section:nth-of-type(1) input, section:nth-of-type(1) textarea')].filter(e=>e.placeholder!=='e.g. Jarl Balgruuf').every(e=>e.value.trim()) &&
        document.querySelector('input[placeholder="e.g. Jarl Balgruuf"]').value==='', 'Generate lost quest content or applied None sender');
      const audit = JSON.parse(payloads.at(-1)[1]).outcome;
      check(audit?.ok && audit.filled.includes('npcName') && audit.blank.includes('narration') &&
        audit.applied.includes('questLocation') && audit.rejected.npcName === 'Preserved or unchanged', 'Generate outcome missing applied/preserved request audit');
      check(!audit.rejected.sender, 'empty/None identity was reported as rejected');
      clickText('Generate'); await pause(); const nulls=JSON.parse(payloads.at(-1)[1]);
      window.directorGenerateResult(JSON.stringify({requestId:nulls.requestId,form:'story',ok:true,fields:{narration:null,sender:null}})); await pause();
      const nullAudit=outcomeFor(nulls.requestId);
      check(nullAudit.rejected.narration && !nullAudit.rejected.sender, 'Generate did not reject required null or skip optional null');
      clickText('Generate'); await pause(); const stopped=JSON.parse(payloads.at(-1)[1]); clickText('Stop'); await pause();
      window.directorGenerateResult(JSON.stringify({requestId:stopped.requestId,form:'story',ok:true,fields:{narration:'late'}}));
      await pause();
      check(document.querySelector('section:nth-of-type(1) textarea').value==='Wants to discuss a careful plan','Stop applied late result');
      clickText('Clear'); await pause();
      check(document.querySelector('input[placeholder="e.g. Saadia"]').value==='' &&
        button('Story type').value==='' && !payloads.some(([name])=>name.startsWith('dispatch')),'Clear failed or Generate dispatched');
      const formButton = (index, text) => [...document.querySelectorAll(`section:nth-of-type(${index}) button`)].find(e=>e.textContent.trim()===text);
      for (const [index, form, fields, label, expected] of [
        [2, 'social', {npc1Name:'Sven',npc2Name:'Mikael',socialType:'npc_interaction',narration:'Resolved a dispute',fact1:'settled the argument',fact2:'accepted the settlement'}, 'Social type', 'Interaction'],
        [2, 'social', {npc1Name:'Nazeem',npc2Name:'Guard 1',socialType:'npc_gossip',narration:'Discussed the missing shipment',gossip:'saw a suspicious shipment'}, 'Social type', 'Gossip'],
        [3, 'political', {factionA:'Imperial Legion',factionB:'Stormcloaks',eventType:'insult',description:'A public diplomatic insult',relationDelta:-5}, 'Political event type', 'Insult']
      ]) {
        formButton(index,'Generate').click(); await pause();
        const generated=JSON.parse(payloads.at(-1)[1]);
        if (form==='social' && fields.socialType==='npc_interaction')
          check(generated.variants?.npc_interaction?.fact1?.required===true &&
            generated.variants?.npc_interaction?.fact2?.required===true,
            'Generate omitted required Social memory metadata');
        window.directorGenerateResult(JSON.stringify({requestId:generated.requestId,form,ok:true,fields}));
        await pause();
        check(button(label).value===expected && document.querySelector(`section:nth-of-type(${index}) textarea`).value===
          (fields.narration || fields.description), `${form}: auto type and dependent draft did not merge`);
        if (form==='social' && fields.npc1Name==='Sven') check(
          document.querySelector('input[placeholder="e.g. Mikael"]').value===generated.fields.npc1Name &&
          document.querySelector('input[placeholder="e.g. Sven"]').value===generated.fields.npc2Name &&
          generated.fields.npc1Name && generated.fields.npc2Name && generated.fields.npc1Name!==generated.fields.npc2Name,
          'dashboard did not preselect distinct social NPCs');
        if (form==='political') check(
          document.querySelectorAll('section:nth-of-type(3) input[type=text]')[0].value==='ImperialFaction' &&
          document.querySelectorAll('section:nth-of-type(3) input[type=text]')[1].value==='StormcloakFaction',
          'faction display names were not normalized to IDs');
        check([...document.querySelectorAll(`section:nth-of-type(${index}) input, section:nth-of-type(${index}) textarea`)].every(e=>e.value.trim()),
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
        fields:{npc1Name:'Nazeem',npc2Name:'Nazeem',socialType:'npc_interaction',narration:'A valid scene',fact1:42,fact2:'considered the proposal'}})); await pause(); clickText('Stop'); await pause();
      const duplicateAudit=outcomeFor(duplicate.requestId);
      check(duplicate.fields.npc1Name && duplicate.fields.npc2Name && duplicate.fields.npc1Name!==duplicate.fields.npc2Name &&
        document.querySelector('input[placeholder="e.g. Mikael"]').value===duplicate.fields.npc1Name &&
        document.querySelector('input[placeholder="e.g. Sven"]').value===duplicate.fields.npc2Name &&
        document.querySelector('section:nth-of-type(2) textarea').value==='A valid scene' &&
        duplicateAudit.rejected.npc1Name===(duplicate.fields.npc2Name==='Nazeem' ? 'Duplicate NPC' : 'Preserved or unchanged') && duplicateAudit.rejected.npc2Name===(duplicate.fields.npc1Name==='Nazeem' ? 'Duplicate NPC' : 'Preserved or unchanged') && duplicateAudit.rejected.fact1==='Invalid type',
        'duplicate identities or wrong-type field discarded valid fields');
      await choose('Political event type','Insult');
      document.querySelector('[aria-label="Clear Political event type"]').click(); await pause();
      formButton(3,'Generate').click(); await pause(); const clearedType=JSON.parse(payloads.at(-1)[1]);
      check(!clearedType.touched.eventType && clearedType.blank.includes('eventType') && !Object.hasOwn(clearedType.fields,'eventType'), 'cleared type was not handed back to Generate');
      window.directorGenerateResult(JSON.stringify({requestId:clearedType.requestId,form:'political',ok:true,
        fields:{eventType:'insult',factionA:'StormcloakFaction',factionB:'ThalmorFaction0',description:'An insult',relationDelta:-5}})); await pause();
      check(button('Political event type').value==='Insult' && document.querySelector('section:nth-of-type(3) textarea').value==='An insult',
        'cleared touched type prevented a complete generated draft: '+JSON.stringify({display:button('Political event type').value,description:document.querySelector('section:nth-of-type(3) textarea').value,outcome:JSON.parse(payloads.at(-1)[1]).outcome}));
      check(payloads.every(([name])=>name==='generateDirector'), 'generation dispatched an action');
      const messages=payloads.map(([,json])=>JSON.parse(json));
      check(messages.filter(m=>!m.outcome).every(request=>messages.filter(m=>m.requestId===request.requestId && m.outcome).length===1),
        'Generate emitted missing/duplicate outcome info records: '+JSON.stringify(messages.filter(m=>!m.outcome).map(r=>({id:r.requestId,form:r.form,followUp:r.followUp,outcomes:messages.filter(m=>m.requestId===r.requestId && m.outcome).length}))));
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
    await fill('input[placeholder="e.g. Saadia"]','Saadia');
    await fill('section:nth-of-type(1) textarea','Story proof');
    await filterPick('Story type','Message');
    await filterPick('Meeting time','dawn');
    await choose('Story type','Message'); // Same value must not clear type-specific fields.
    clickText('Dispatch Story'); await pause();
    await fill('input[placeholder="e.g. Mikael"]','Mikael');
    await fill('input[placeholder="e.g. Sven"]','Sven');
    await fill('section:nth-of-type(2) textarea','Social proof');
    const gossipLabel=[...document.querySelectorAll('section:nth-of-type(2) label')].find(e=>e.textContent.trim().startsWith('Gossip'));
    const gossipControl=gossipLabel?.parentElement?.querySelector('input,textarea');
    Object.getOwnPropertyDescriptor(gossipControl.tagName==='TEXTAREA'?HTMLTextAreaElement.prototype:HTMLInputElement.prototype,'value').set.call(gossipControl,'heard a rumor about a shipment');
    gossipControl.dispatchEvent(new Event('input',{bubbles:true})); await pause();
    await filterPick('Social type','Gossip'); clickText('Dispatch Social'); await pause();
    await fill('input[placeholder="e.g. StormcloakFaction"]','StormcloakFaction');
    await fill('input[placeholder="e.g. ThalmorFaction"]','ThalmorFaction');
    await fill('section:nth-of-type(3) textarea','Politics proof');
    await filterPick('Political event type','Insult'); clickText('Dispatch Political Event'); await pause();
    await filterPick('Action NPC','Nazeem'); await filterPick('Action','ReportPlayerConduct');
    await fill('input[placeholder="Select faction..."]','ThalmorFaction');
    await fill('section:nth-of-type(4) textarea','praised Legion');
    await filterPick('sentiment','positive');
    await choose('Action','ReportPlayerConduct'); // Same action must preserve all parameters.
    clickText('Execute Action'); await pause();
    const political = JSON.parse(payloads.find(([name]) => name === 'dispatchPolitics')[1]);
    check(political.factionA === 'StormcloakFaction' && political.factionALabel === 'Stormcloaks' &&
      political.factionB === 'ThalmorFaction' && political.factionBLabel === 'ThalmorFaction',
      'political faction labels must resolve known IDs and preserve unknown IDs');
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
    await geometry(document.querySelector('input[placeholder="e.g. StormcloakFaction"]'),'FactionInput');
    const npc=button('Action NPC'); npc.click(); await pause();
    check(optionText(document.querySelector('[role=option][aria-selected=true]'))==='Nazeem','current value not highlighted');
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
    const factionInput=document.querySelector('input[placeholder="e.g. StormcloakFaction"]');
    factionInput.focus(); await pause(); button('Action').focus(); await pause();
    check(!factionInput.parentElement.querySelector('.overflow-y-auto') && escapeCapture.at(-1)?.[1]==='0','faction focus exit left capture armed');
    await fill('input[placeholder="e.g. StormcloakFaction"]','__no_faction__');
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
      const confirm = async (section, expected) => {
        const count = payloads.length;
        submit(section).click(); await pause();
        check(JSON.stringify(payloads.slice(count).map(([name,json])=>[name,JSON.parse(json)])) === JSON.stringify(expected),
          'busy_confirm_cancel_dispatch: cancel/dispatch order or payload changed: '+JSON.stringify(payloads.slice(count)));
      };
      await choose('Action NPC','Nazeem'); await choose('Action','ChangeSpeed');
      await choose('New speed','Jog');
      // ChangeSpeed/CancelCurrentTask act on the busy task: one click, no pre-cancel.
      await confirm(4,[['executeAction',{npcFormId:108196,actionName:'ChangeSpeed',params:{newSpeed:'1'}}]]);
      window.updateSlots('[]'); await pause();
      document.dispatchEvent(new Event('visibilitychange')); await pause();
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
    if scenario == 'director_live_polish2':
        profile = folder / 'profile'
        chrome = subprocess.Popen([str(CHROME), '--headless=new', '--no-sandbox', '--disable-gpu',
            '--allow-file-access-from-files', '--no-first-run', '--remote-debugging-port=0',
            '--remote-allow-origins=*', '--window-size=1600,3000', '--user-data-dir=' + str(profile),
            (folder / 'index.html').as_uri() + '#' + scenario], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        sock = None
        try:
            port_file = profile / 'DevToolsActivePort'
            deadline = time.time() + 20
            while time.time() < deadline and not port_file.exists():
                if chrome.poll() is not None:
                    raise SystemExit('Chrome exited before opening the DevTools port')
                time.sleep(.1)
            port = int(port_file.read_text(encoding='utf-8').splitlines()[0])
            targets = json.load(urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list'))
            target = next(item for item in targets if item.get('type') == 'page')
            ws_url = target['webSocketDebuggerUrl']
            host, ws_port = ws_url.split('/')[2].split(':')
            sock = socket.create_connection((host, int(ws_port)), timeout=10)
            key = base64.b64encode(os.urandom(16)).decode()
            path = '/' + '/'.join(ws_url.split('/')[3:])
            sock.sendall((f'GET {path} HTTP/1.1\r\nHost: {host}:{ws_port}\r\n'
                f'Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n'
                'Sec-WebSocket-Version: 13\r\nOrigin: http://localhost\r\n\r\n').encode())
            response = b''
            while b'\r\n\r\n' not in response:
                response += sock.recv(4096)
            if b'101 ' not in response:
                raise SystemExit('Chrome DevTools websocket handshake failed: ' + response.decode(errors='replace'))
            sock.settimeout(10)
            next_id = [0]
            def cdp(method, params=None):
                next_id[0] += 1
                command_id = next_id[0]
                payload = json.dumps({'id':command_id,'method':method,'params':params or {}}).encode()
                mask = os.urandom(4)
                size = len(payload)
                header = bytes([0x81, 0x80 | (size if size < 126 else 126 if size < 65536 else 127)])
                if size >= 126:
                    header += struct.pack('!H' if size < 65536 else '!Q', size)
                sock.sendall(header + mask + bytes(byte ^ mask[index % 4] for index,byte in enumerate(payload)))
                while True:
                    frame = sock.recv(2)
                    if len(frame) != 2: raise SystemExit('Chrome DevTools websocket closed')
                    length = frame[1] & 127
                    if length == 126: length = struct.unpack('!H', sock.recv(2))[0]
                    elif length == 127: length = struct.unpack('!Q', sock.recv(8))[0]
                    body = b''
                    while len(body) < length: body += sock.recv(length-len(body))
                    message = json.loads(body.decode())
                    if message.get('id') == command_id: return message
            cdp('Runtime.enable'); cdp('Page.enable')
            def read_result():
                value = cdp('Runtime.evaluate', {'expression':"document.getElementById('result')?.textContent || ''",'returnByValue':True})
                result = value.get('result',{}).get('result',{}).get('value','')
                if result.startswith('DRAG:'):
                    drag = json.loads(result[5:])
                    cdp('Input.dispatchMouseEvent', {'type':'mouseMoved','x':drag['x'],'y':drag['y']})
                    cdp('Input.dispatchMouseEvent', {'type':'mousePressed','x':drag['x'],'y':drag['y'],'button':'left','buttons':1,'clickCount':1})
                    cdp('Input.dispatchMouseEvent', {'type':'mouseMoved','x':drag['x'],'y':drag['y']+drag['delta'],'button':'left','buttons':1})
                    cdp('Input.dispatchMouseEvent', {'type':'mouseReleased','x':drag['x'],'y':drag['y']+drag['delta'],'button':'left','buttons':0,'clickCount':1})
                    cdp('Runtime.evaluate', {'expression':"document.getElementById('result').textContent='RUNNING'; window.__dragDone=true;"})
                    return 'RUNNING'
                return result
            deadline = time.time() + 45
            result = ''
            while time.time() < deadline:
                result = read_result()
                if result == 'RELOAD_READY' or result.startswith('{') or result.startswith('FAIL:'): break
                time.sleep(.2)
            if result != 'RELOAD_READY':
                raise SystemExit('Reload scenario failed before CDP reload: ' + result)
            cdp('Page.reload', {'ignoreCache':True})
            deadline = time.time() + 45
            result = ''
            while time.time() < deadline:
                result = read_result()
                if result.startswith('{') or result.startswith('FAIL:'): break
                time.sleep(.2)
            if not result.startswith('{'):
                raise SystemExit(result or 'Reload scenario emitted no result')
            data = json.loads(result)
            print(json.dumps(data, indent=2))
            assert data['status'] == 'PASS'
        finally:
            if sock: sock.close()
            chrome.terminate()
            chrome.wait(timeout=10)
        raise SystemExit(0)
    completed = subprocess.run([str(CHROME), '--headless=new', '--no-sandbox', '--disable-gpu',
        '--allow-file-access-from-files', '--no-first-run', '--window-size=' + ('1600,3000' if not scenario or '--busy-only' in sys.argv or scenario in ['busy','nearby_picks','director_required_fields','npc_picker_consistent','director_polish','director_field_layout','director_live_polish','action_generate'] else '1600,1000'),
        '--user-data-dir=' + str(folder / 'profile'), '--virtual-time-budget=90000', '--dump-dom',
        (folder / 'index.html').as_uri() + ('#busy' if '--busy-only' in sys.argv else '#' + scenario if scenario else '')], capture_output=True, text=True, encoding='utf-8', timeout=60)
    start = completed.stdout.find('<pre id="result">')
    result = html.unescape(completed.stdout[start + len('<pre id="result">'):].split('</pre>', 1)[0]) if start >= 0 else ''
    if not result.startswith('{'):
        raise SystemExit(result or completed.stderr or 'Chrome emitted no result')
    data = json.loads(result)
    print(json.dumps(data, indent=2))
    assert data['status'] == 'PASS'
