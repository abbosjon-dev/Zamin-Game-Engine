#!/usr/bin/env python3
"""Real 2D/3D quest actions, v2 fresh-process resume, reward replay rejection and relocated export."""
import argparse,json,os,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MARKERS=['QUEST_ACCEPTANCE collect=1 kill=1 interact=1 trigger=1 claim=1 save=2','QUEST_RESUMED claims=2 reward=1 xp=200','QUEST_NO_REPLAY xp=200 coin=5 badge=1']
TAPES=['15:hold=D;30:E;45:E;60:E;65:LeftControl;80:E;92:release=D;95:R;100:F5','20:F9;30:hold=A;110:release=A;115:R;130:F5','20:F9;40:R']
def validate(log,phase):
 text=log.read_text(errors='replace')
 for marker in ('GL_INVALID_','Assertion failed',"Visual graph '",'Visual graph error','QUEST_ORACLE_FAILED','QUEST_IO_FAILED','AI_CONFIG_FAILED'):
  if marker in text:raise RuntimeError(f'{marker}: {log}')
 if MARKERS[phase] not in text:raise RuntimeError(f'Missing quest acceptance: {log}')
def overrides(scene,auto,phase):
 data=json.loads(scene.read_text())
 for e in data['Entities']:
  if e['Entity'] in (800,900):e['NativeScript']['Fields']['Overrides']={'Type':'String','Value':json.dumps({'Auto':auto,**({'Checks':True,'Phase':phase} if e['Entity']==900 else {})})}
 scene.write_text(json.dumps(data,indent=2)+'\n')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,default=ROOT/'build');p.add_argument('--out',type=Path);p.add_argument('--no-editor',action='store_true');p.add_argument('--exports',action='store_true');p.add_argument('--scene',choices=('Quest2D','Quest3D'));args=p.parse_args();build=args.build.resolve();out=(args.out or build/'GameplayQuest/Workflow').resolve();out.mkdir(parents=True,exist_ok=True);runner=[] if os.environ.get('DISPLAY') else ['xvfb-run','-a','-s','-screen 0 1600x1000x24 -nolisten tcp'];report=[]
 with tempfile.TemporaryDirectory(prefix='zamin-quest-workflow-') as temporary:
  work=Path(temporary)
  for course in ((args.scene,) if args.scene else ('Quest2D','Quest3D')):
   project=work/course;shutil.copytree(ROOT/'Projects/QuestWorkshop',project,ignore=shutil.ignore_patterns('.zamin','Build'));manifest=project/'QuestWorkshop.zproject';data=json.loads(manifest.read_text());assert not data['Project']['ScriptModulePath'];data['Project']['StartScene']=f'Scenes/{course}.zscene';manifest.write_text(json.dumps(data,indent=2)+'\n');scene=project/f'Assets/Scenes/{course}.zscene';env=dict(os.environ,LIBGL_ALWAYS_SOFTWARE='1',LP_NUM_THREADS='2',ZAMIN_GL_DEBUG='1',ZAMIN_CAPTURE_FPS='30')
   for mode in (('player',) if args.no_editor else ('player','editor')):
    home=work/(course+'-'+mode+'-home');home.mkdir()
    for phase in (0,1,2):
     overrides(scene,mode=='editor',phase);log=out/f'{course}-{mode}-{phase}.log';png=out/f'{course}-{mode}-{phase}.png';runenv=dict(env,HOME=str(home),ZAMIN_MAX_FRAMES='160' if phase<2 else '75',ZAMIN_SCREENSHOT=str(png),ZAMIN_PLAYER_INPUT=TAPES[phase] if mode=='player' else '')
     if mode=='editor':runenv.update(ZAMIN_EDITOR_PLAY='1',ZAMIN_EDITOR_SELECT='Player')
     cmd=[str(build/'bin'/('ZaminEditor' if mode=='editor' else 'ZaminPlayer')),str(manifest)]+([] if mode=='editor' else ['--no-splash'])
     with log.open('w') as stream:subprocess.run(runner+cmd,env=runenv,stdout=stream,stderr=subprocess.STDOUT,timeout=360,check=True)
     validate(log,phase);subprocess.run(['python3',str(ROOT/'Tests/png_check.py'),str(png)],check=True)
     files=list(home.rglob('gameplay-saves/Quest.json'));assert len(files)==1;save=json.loads(files[0].read_text());assert save['Version']==2 and len(save['Payload']['Entities'])==6;hero=next(e for e in save['Payload']['Entities'] if e['Key']=='Player');assert hero['Quests']['XP']==(150 if phase==0 else 200);assert hero['Items']['Coin']==5
     if phase>0:assert hero['Items']['Badge']==1 and hero['Quests']['Entries']['Home']['Mode']==3
     report.append({'Scene':course,'Mode':mode,'Phase':phase,'Passed':True});print(f'PASS {course}/{mode}/{phase}: real objectives, claims, v2 saves, XP and replay rejection',flush=True)
   if args.exports:
    overrides(scene,True,0);exportout=out/(course+'-export');exportwork=work/(course+'-export-work');exportenv=dict(env,ZAMIN_BUILD_DIR=str(build))
    subprocess.run(['bash',str(ROOT/'Tests/export_test.sh'),'--project',str(project),'--frames','160','--shot-frames','160','--work',str(exportwork),'--out',str(exportout)],env=exportenv,timeout=900,check=True)
    validate(exportout/'player.log',0);validate(exportout/'player-shot.log',1);relocated=exportwork/'relocated-quest';(exportwork/'export').rename(relocated);log=exportout/'relocated-no-replay.log';png=exportout/'relocated-no-replay.png';runenv=dict(exportenv,HOME=str(exportwork/'home'),ZAMIN_MAX_FRAMES='75',ZAMIN_SCREENSHOT=str(png))
    with log.open('w') as stream:subprocess.run(runner+[str(relocated/'QuestWorkshop.sh'),'--no-splash'],env=runenv,stdout=stream,stderr=subprocess.STDOUT,timeout=300,check=True)
    validate(log,2);subprocess.run(['python3',str(ROOT/'Tests/png_check.py'),str(png)],check=True);report.append({'Scene':course,'Mode':'export-write-resume-and-real-relocated-no-replay','Passed':True});print(f'PASS {course}/export: native quest progression survives relocation without duplicate rewards',flush=True)
 (out/'Report.json').write_text(json.dumps({'Passed':True,'Checks':report},indent=2)+'\n')
if __name__=='__main__':main()
