#!/usr/bin/env python3
"""Real 2D/3D combat slice: projectiles, areas, status effects, v4 fresh-process resume, no replay and relocated export."""
import argparse,json,os,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MARKERS=['COMBAT_ACCEPTANCE sentry=45 burn=2 pierce=3 nova=2 hidden=30 poison=1 energy=25 save=4','COMBAT_RESUMED sentry=0 player=94 xp=150 coin=5 energy=15','COMBAT_NO_REPLAY bolts=3 xp=150 coin=5 effects=0']
# Keyboard in phases 0-1 (R Rally, Space Bolt, L Lance, N Nova, F5/F9), the gamepad in phase 2 (Back Load, A Bolt, Y Nova).
TAPES=['8:R;11:Space;14:L;20:N;32:Space;34:F5','8:F9;80:Space;116:Space;122:F5','8:pad=Back;26:pad=A;32:pad=Y']
FRAMES=['90','160','75']
def validate(log,phase):
 text=log.read_text(errors='replace')
 for marker in ('GL_INVALID_','Assertion failed',"Visual graph '",'Visual graph error','COMBAT_ORACLE_FAILED','COMBAT_IO_FAILED','COMBAT_CONFIG_FAILED','Snaryad vizual prefabi rad etildi','is not a known action'):
  if marker in text:raise RuntimeError(f'{marker}: {log}')
 if MARKERS[phase] not in text:raise RuntimeError(f'Missing combat acceptance: {log}')
 if phase==1 and 'Jang holati tiklandi: 8 effekt, 1 snaryad' not in text:raise RuntimeError(f'Missing combat restore: {log}')
def check_save(home,phase):
 files=list(home.rglob('gameplay-saves/Combat.json'));assert len(files)==1,files;save=json.loads(files[0].read_text());assert save['Version']==4,save['Version'];payload=save['Payload']
 entities={e['Key']:e for e in payload['Entities']};assert sorted(entities)==['Dummy1','Dummy2','Dummy3','Hidden','Player','Sentry'],sorted(entities)
 hero,sentry=entities['Player'],entities['Sentry'];bolt=hero['Loadout']['Abilities']['Bolt']
 assert hero['Items']['Potion']==1 and entities['Hidden']['Health']==30 and entities['Dummy3']['Health']==35
 if phase==0:
  assert len(payload['Combat']['Projectiles'])==1 and payload['Combat']['Projectiles'][0]['Key']=='Bolt' and payload['Combat']['Projectiles'][0]['Owner']=='Player'
  poison=[e for e in hero['Effects'] if e['Key']=='Poison'];assert len(poison)==1 and poison[0]['Remaining']>0 and poison[0]['Source']=='Sentry'
  assert [e['Key'] for e in sentry['Effects']]==['Burn'] and sentry['Health']==90 and not sentry['Dead']
  assert bolt['Uses']==2 and bolt['Remaining']>0 and hero['Loadout']['Energy']==25
 else:
  assert payload['Combat']['Projectiles']==[] and sentry['Dead'] and sentry['Effects']==[] and sentry['Health']==0
  assert bolt['Uses']==3 and hero['Loadout']['Energy']==15 and hero['Items']['Coin']==5 and hero['Quests']['XP']==150 and hero['Health']==94
def overrides(scene,auto,phase):
 data=json.loads(scene.read_text())
 for e in data['Entities']:
  if e['Entity']==800:e['NativeScript']['Fields']['Overrides']={'Type':'String','Value':json.dumps({'Auto':auto,'Checks':True,'Phase':phase})}
 scene.write_text(json.dumps(data,indent=2)+'\n')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,default=ROOT/'build');p.add_argument('--out',type=Path);p.add_argument('--no-editor',action='store_true');p.add_argument('--exports',action='store_true');p.add_argument('--scene',choices=('Combat2D','Combat3D'));args=p.parse_args();build=args.build.resolve();out=(args.out or build/'GameplayCombat/Workflow').resolve();out.mkdir(parents=True,exist_ok=True);runner=[] if os.environ.get('DISPLAY') else ['xvfb-run','-a','-s','-screen 0 1600x1000x24 -nolisten tcp'];report=[]
 with tempfile.TemporaryDirectory(prefix='zamin-combat-workflow-') as temporary:
  work=Path(temporary)
  for course in ((args.scene,) if args.scene else ('Combat2D','Combat3D')):
   project=work/course;shutil.copytree(ROOT/'Projects/CombatWorkshop',project,ignore=shutil.ignore_patterns('.zamin','Build'));manifest=project/'CombatWorkshop.zproject';data=json.loads(manifest.read_text());assert not data['Project']['ScriptModulePath'];data['Project']['StartScene']=f'Scenes/{course}.zscene';manifest.write_text(json.dumps(data,indent=2)+'\n');scene=project/f'Assets/Scenes/{course}.zscene';env=dict(os.environ,LIBGL_ALWAYS_SOFTWARE='1',LP_NUM_THREADS='2',ZAMIN_GL_DEBUG='1',ZAMIN_CAPTURE_FPS='30')
   for mode in (('player',) if args.no_editor else ('player','editor')):
    home=work/(course+'-'+mode+'-home');home.mkdir()
    for phase in (0,1,2):
     overrides(scene,mode=='editor',phase);log=out/f'{course}-{mode}-{phase}.log';png=out/f'{course}-{mode}-{phase}.png';runenv=dict(env,HOME=str(home),ZAMIN_MAX_FRAMES=FRAMES[phase],ZAMIN_SCREENSHOT=str(png),ZAMIN_PLAYER_INPUT=TAPES[phase] if mode=='player' else '')
     if mode=='editor':runenv.update(ZAMIN_EDITOR_PLAY='1',ZAMIN_EDITOR_SELECT='Player')
     cmd=[str(build/'bin'/('ZaminEditor' if mode=='editor' else 'ZaminPlayer')),str(manifest)]+([] if mode=='editor' else ['--no-splash'])
     with log.open('w') as stream:subprocess.run(runner+cmd,env=runenv,stdout=stream,stderr=subprocess.STDOUT,timeout=420,check=True)
     validate(log,phase);subprocess.run(['python3',str(ROOT/'Tests/png_check.py'),str(png)],check=True);check_save(home,phase)
     report.append({'Scene':course,'Mode':mode,'Phase':phase,'Passed':True});print(f'PASS {course}/{mode}/{phase}: projectiles, pierce, area LoS, stacking effects, v4 save and replay rejection',flush=True)
   if args.exports:
    overrides(scene,True,0);exportout=out/(course+'-export');exportwork=work/(course+'-export-work');exportenv=dict(env,ZAMIN_BUILD_DIR=str(build))
    subprocess.run(['bash',str(ROOT/'Tests/export_test.sh'),'--project',str(project),'--frames','90','--shot-frames','160','--work',str(exportwork),'--out',str(exportout)],env=exportenv,timeout=900,check=True)
    validate(exportout/'player.log',0);validate(exportout/'player-shot.log',1);relocated=exportwork/'relocated-combat';(exportwork/'export').rename(relocated);log=exportout/'relocated-no-replay.log';png=exportout/'relocated-no-replay.png';runenv=dict(exportenv,HOME=str(exportwork/'home'),ZAMIN_MAX_FRAMES='75',ZAMIN_SCREENSHOT=str(png))
    with log.open('w') as stream:subprocess.run(runner+[str(relocated/'CombatWorkshop.sh'),'--no-splash'],env=runenv,stdout=stream,stderr=subprocess.STDOUT,timeout=300,check=True)
    validate(log,2);subprocess.run(['python3',str(ROOT/'Tests/png_check.py'),str(png)],check=True);check_save(exportwork/'home',1);report.append({'Scene':course,'Mode':'export-write-resume-and-real-relocated-no-replay','Passed':True});print(f'PASS {course}/export: combat state survives relocation without duplicate rewards',flush=True)
 (out/'Report.json').write_text(json.dumps({'Passed':True,'Checks':report},indent=2)+'\n')
if __name__=='__main__':main()
