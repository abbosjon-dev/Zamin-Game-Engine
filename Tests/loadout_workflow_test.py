#!/usr/bin/env python3
"""Real 2D/3D loadout actions, v3 fresh-process resume, reward replay rejection and relocated export."""
import argparse,json,os,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MARKERS=['LOADOUT_ACCEPTANCE equip=2 casts=1 potion=1 save=3','LOADOUT_RESUMED casts=2 xp=150 coin=5','LOADOUT_NO_REPLAY casts=2 energy=40']
TAPES=['8:1;10:2;12:Space;20:H;24:F5','10:F9;55:Space;60:R;65:F5','10:F9;55:Space;60:R;65:1']
def validate(log,phase):
 text=log.read_text(errors='replace')
 for marker in ('GL_INVALID_','Assertion failed',"Visual graph '",'Visual graph error','LOADOUT_ORACLE_FAILED','LOADOUT_IO_FAILED','AI_CONFIG_FAILED'):
  if marker in text:raise RuntimeError(f'{marker}: {log}')
 if MARKERS[phase] not in text:raise RuntimeError(f'Missing loadout acceptance: {log}')
def overrides(scene,auto,phase):
 data=json.loads(scene.read_text())
 for e in data['Entities']:
  if e['Entity']==800:e['NativeScript']['Fields']['Overrides']={'Type':'String','Value':json.dumps({'Auto':auto,'Checks':True,'Phase':phase})}
 scene.write_text(json.dumps(data,indent=2)+'\n')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,default=ROOT/'build');p.add_argument('--out',type=Path);p.add_argument('--no-editor',action='store_true');p.add_argument('--exports',action='store_true');p.add_argument('--scene',choices=('Loadout2D','Loadout3D'));args=p.parse_args();build=args.build.resolve();out=(args.out or build/'GameplayLoadout/Workflow').resolve();out.mkdir(parents=True,exist_ok=True);runner=[] if os.environ.get('DISPLAY') else ['xvfb-run','-a','-s','-screen 0 1600x1000x24 -nolisten tcp'];report=[]
 with tempfile.TemporaryDirectory(prefix='zamin-loadout-workflow-') as temporary:
  work=Path(temporary)
  for course in ((args.scene,) if args.scene else ('Loadout2D','Loadout3D')):
   project=work/course;shutil.copytree(ROOT/'Projects/LoadoutWorkshop',project,ignore=shutil.ignore_patterns('.zamin','Build'));manifest=project/'LoadoutWorkshop.zproject';data=json.loads(manifest.read_text());assert not data['Project']['ScriptModulePath'];data['Project']['StartScene']=f'Scenes/{course}.zscene';manifest.write_text(json.dumps(data,indent=2)+'\n');scene=project/f'Assets/Scenes/{course}.zscene';env=dict(os.environ,LIBGL_ALWAYS_SOFTWARE='1',LP_NUM_THREADS='2',ZAMIN_GL_DEBUG='1',ZAMIN_CAPTURE_FPS='30')
   for mode in (('player',) if args.no_editor else ('player','editor')):
    home=work/(course+'-'+mode+'-home');home.mkdir()
    for phase in (0,1,2):
     overrides(scene,mode=='editor',phase);log=out/f'{course}-{mode}-{phase}.log';png=out/f'{course}-{mode}-{phase}.png';runenv=dict(env,HOME=str(home),ZAMIN_MAX_FRAMES='110',ZAMIN_SCREENSHOT=str(png),ZAMIN_PLAYER_INPUT=TAPES[phase] if mode=='player' else '')
     if mode=='editor':runenv.update(ZAMIN_EDITOR_PLAY='1',ZAMIN_EDITOR_SELECT='Player')
     cmd=[str(build/'bin'/('ZaminEditor' if mode=='editor' else 'ZaminPlayer')),str(manifest)]+([] if mode=='editor' else ['--no-splash'])
     with log.open('w') as stream:subprocess.run(runner+cmd,env=runenv,stdout=stream,stderr=subprocess.STDOUT,timeout=360,check=True)
     validate(log,phase);subprocess.run(['python3',str(ROOT/'Tests/png_check.py'),str(png)],check=True)
     files=list(home.rglob('gameplay-saves/Loadout.json'));assert len(files)==1;save=json.loads(files[0].read_text());assert save['Version']==3 and len(save['Payload']['Entities'])==2;hero=next(e for e in save['Payload']['Entities'] if e['Key']=='Player');assert hero['Quests']['XP']==(0 if phase==0 else 150);assert hero['Loadout']['Equipped']==['Sword','','Plate',''];assert hero['Loadout']['Energy']==(65 if phase==0 else 40);assert hero['Loadout']['Abilities']['Bolt']['Uses']==(1 if phase==0 else 2);assert hero['Items']['Potion']==1
     if phase==0:assert hero['Loadout']['Abilities']['Bolt']['Remaining']>0
     if phase>0:assert hero['Items']['Coin']==5
     report.append({'Scene':course,'Mode':mode,'Phase':phase,'Passed':True});print(f'PASS {course}/{mode}/{phase}: real equipment, abilities, v3 saves, quest XP and replay rejection',flush=True)
   if args.exports:
    overrides(scene,True,0);exportout=out/(course+'-export');exportwork=work/(course+'-export-work');exportenv=dict(env,ZAMIN_BUILD_DIR=str(build))
    subprocess.run(['bash',str(ROOT/'Tests/export_test.sh'),'--project',str(project),'--frames','110','--shot-frames','110','--work',str(exportwork),'--out',str(exportout)],env=exportenv,timeout=900,check=True)
    validate(exportout/'player.log',0);validate(exportout/'player-shot.log',1);relocated=exportwork/'relocated-loadout';(exportwork/'export').rename(relocated);log=exportout/'relocated-no-replay.log';png=exportout/'relocated-no-replay.png';runenv=dict(exportenv,HOME=str(exportwork/'home'),ZAMIN_MAX_FRAMES='110',ZAMIN_SCREENSHOT=str(png))
    with log.open('w') as stream:subprocess.run(runner+[str(relocated/'LoadoutWorkshop.sh'),'--no-splash'],env=runenv,stdout=stream,stderr=subprocess.STDOUT,timeout=300,check=True)
    validate(log,2);subprocess.run(['python3',str(ROOT/'Tests/png_check.py'),str(png)],check=True);report.append({'Scene':course,'Mode':'export-write-resume-and-real-relocated-no-replay','Passed':True});print(f'PASS {course}/export: native equipment/ability progression survives relocation without duplicate rewards',flush=True)
 (out/'Report.json').write_text(json.dumps({'Passed':True,'Checks':report},indent=2)+'\n')
if __name__=='__main__':main()
