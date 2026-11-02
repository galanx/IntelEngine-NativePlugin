"""Render activity dates, provenance and leaders in the production bundle (headless Chrome).
Usage: python SKSE/tests/RunDashboardMetadataCheck.py [chrome.exe] [dashboard-tree]
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
 const require = (ok, message) => { if (!ok) throw Error(message); };
 const click = async label => { [...document.querySelectorAll('button')].find(b => b.textContent.trim() === label).click(); await pause(); };
 const card = text => [...document.querySelectorAll('.slot-card')].find(e => e.textContent.includes(text));
 const state = {
  calendar:{time:2.5,year:205,month:0,day:2,hour:12}, config:{}, pluginConfig:{'ui.scale':1},
  tasks:[{index:0,state:1,agentName:'Nazeem',taskType:'story',targetName:'Player',origin:'Director',startTime:1.25,reason:'A personal request'}],
  scheduled:[{agent:'Saadia',taskType:'travel',destination:'Riverwood',origin:'Schedule · Director',startTime:2.5}],
  story:{enabled:true,isActive:false,types:{}},
  npcSocialLog:[{type:'npc_interaction',npc1:'Mikael',npc2:'Sven',text:'MIDNIGHT',time:2},
   {type:'npc_gossip',npc1:'A',npc2:'B',text:'LEGACY_SOCIAL'},
   {type:'npc_gossip',npc1:'C',npc2:'D',text:'ZERO_SOCIAL',time:0}],
  politics:{enabled:true,factions:[{id:'A',name:'Thalmor',type:'political',hold:'Haafingar',leaders:['Elenwen','Ondolemar']},
   {id:'B',name:'Empty leaders',type:'guild',hold:'Whiterun',leaders:[]}],relations:[],
   events:[{type:'insult',description:'RECENT_EVENT',faction_a:'A',faction_b:'B',delta:-5,time:1.75},
    {type:'insult',description:'LEGACY_EVENT',faction_a:'A',delta:0},
    {type:'war_declaration',description:'WAR_EVENT',faction_a:'A',faction_b:'B',delta:-10,time:0.5}],
   wars:[{faction_a:'A',faction_b:'B',battles:1,morale_a:50,morale_b:50,strength_a:50,strength_b:50,
    recent_battles:[{narrative:'BATTLE_ROW',result:'draw',attacker_losses:1,defender_losses:1,time:0.25}]}]}
 };
 try {
  await pause(); window.updateFullState(JSON.stringify(state)); await pause();
  require(card('Nazeem').textContent.includes('Director') && card('Nazeem').textContent.includes('1 Morning Star, 4E 205 06:00') && card('Nazeem').textContent.includes('A personal request'), 'active task provenance/start/reason missing');
  require(card('Saadia').textContent.includes('Schedule · Director') && card('Saadia').textContent.includes('2 Morning Star, 4E 205 12:00'), 'schedule provenance/start missing');
  // Incremental slot pushes must carry metadata too.
  window.updateSlots(JSON.stringify([{...state.tasks[0],state:2}])); await pause();
  require(card('Nazeem').textContent.includes('A personal request'), 'slot update dropped metadata');
  await click('Story');
  require(card('MIDNIGHT').textContent.includes('2 Morning Star, 4E 205 00:00'), 'midnight date wrong');
  require(!card('LEGACY_SOCIAL').textContent.includes('4E'), 'legacy social date fabricated');
  require(card('ZERO_SOCIAL').textContent.includes('31 Evening Star, 4E 204 00:00'), 'zero timestamp or year rollover wrong');
  await click('Politics');
  require(card('RECENT_EVENT').textContent.includes('1 Morning Star, 4E 205 18:00'), 'political date missing');
  require(!card('LEGACY_EVENT').textContent.includes('4E'), 'legacy political date fabricated');
  require(card('WAR_EVENT').textContent.includes('31 Evening Star, 4E 204 12:00'), 'war event date missing');
  require(card('BATTLE_ROW').textContent.includes('31 Evening Star, 4E 204 06:00'), 'battle date missing');
  require([...document.querySelectorAll('.slot-card')].some(e=>e.textContent.includes('Leaders: Elenwen, Ondolemar')), 'configured leaders missing');
  require(!card('Empty leaders').textContent.includes('Leaders:'), 'empty leaders heading rendered');
  // No Gregorian leap day: custom anchor March 1 crosses to February 28.
  state.calendar={time:2.5,year:204,month:2,day:1,hour:12};
  window.updateFullState(JSON.stringify(state)); await pause();
  require(card('RECENT_EVENT').textContent.includes("28 Sun's Dawn, 4E 204 18:00"), 'Skyrim month boundary treated as Gregorian leap year');
  state.calendar={time:2.25,year:204,month:2,day:1,hour:12};
  window.updateFullState(JSON.stringify(state)); await pause();
  require(card('RECENT_EVENT').textContent.includes("1 First Seed, 4E 204 00:00"), 'custom clock offset ignored');
  state.calendar={time:50.20000076293945,year:201,month:7,day:20,hour:4.800000190734863};
  state.politics.events[0].time=49;
  window.updateFullState(JSON.stringify(state)); await pause();
  require(card('RECENT_EVENT').textContent.includes('19 Last Seed, 4E 201 00:00'), 'native float rounding crossed midnight');
  document.getElementById('result').textContent=JSON.stringify({status:'PASS',checks:'activity dates, absent/zero timestamps, custom calendar/year/month boundaries, task metadata and faction leaders'});
 } catch(error) { document.getElementById('result').textContent=JSON.stringify({status:'FAIL',error:String(error)}); }
})();
"""

if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='ie-metadata-') as directory:
        folder = Path(directory)
        for source in DIST.iterdir():
            if source.is_file():
                shutil.copy2(source, folder / source.name)
        page = (folder / 'index.html').read_text(encoding='utf-8')
        page = page.replace('</body>', '<pre id="result">RUNNING</pre><script>' + SCRIPT + '</script></body>')
        (folder / 'index.html').write_text(page, encoding='utf-8')
        completed = subprocess.run([str(CHROME), '--headless=new', '--no-sandbox', '--disable-gpu',
            '--allow-file-access-from-files', '--no-first-run', '--window-size=1600,1000',
            '--user-data-dir=' + str(folder / 'profile'), '--virtual-time-budget=10000', '--dump-dom',
            (folder / 'index.html').as_uri()], capture_output=True, text=True, encoding='utf-8', timeout=60)
        marker = '<pre id="result">'
        result = html.unescape(completed.stdout.split(marker, 1)[1].split('</pre>', 1)[0]) if marker in completed.stdout else ''
        if not result.startswith('{'):
            raise SystemExit(result or completed.stderr or 'Chrome emitted no result')
        data = json.loads(result)
        print(json.dumps(data, indent=2))
        assert data['status'] == 'PASS'
