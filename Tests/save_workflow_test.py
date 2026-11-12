#!/usr/bin/env python3
"""Real 2D/3D stable-slot save, fresh-process resume, backup recovery and relocated export acceptance."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
WRITE='SAVE_ACCEPTANCE wrote=1 hp=875 coin=8 npc=40 door=0'
RESUME='SAVE_RESUMED health=1 bag=1 interaction=1 ai=1 pose=1 prefab=1'
TAPE='30:E;35:hold=D;55:release=D;100:LeftControl'
def validate(log,resume):
 text=log.read_text(errors='replace')
 for marker in ('GL_INVALID_','Assertion failed',"Visual graph '",'Visual graph error','SAVE_ORACLE_FAILED','AI_CONFIG_FAILED'):
  if marker in text:raise RuntimeError(f'{marker}: {log}')
 if (RESUME if resume else WRITE) not in text:raise RuntimeError(f'Missing save acceptance: {log}')
def overrides(scene,auto,resume):
 data=json.loads(scene.read_text())
 for e in data['Entities']:
  if e['Entity'] in (800,900):e['NativeScript']['Fields']['Overrides']={'Type':'String','Value':json.dumps({'Auto':auto,**({'Checks':True,'Resume':resume} if e['Entity']==900 else {})})}
 scene.write_text(json.dumps(data,indent=2)+'\n')
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',type=Path,default=ROOT/'build');parser.add_argument('--out',type=Path);parser.add_argument('--no-editor',action='store_true');parser.add_argument('--exports',action='store_true');parser.add_argument('--scene',choices=('Save2D','Save3D'));args=parser.parse_args();build=args.build.resolve();out=(args.out or build/'GameplaySave/Workflow').resolve();out.mkdir(parents=True,exist_ok=True)
 runner=[] if os.environ.get('DISPLAY') else ['xvfb-run','-a','-s','-screen 0 1600x1000x24 -nolisten tcp'];report=[]
 with tempfile.TemporaryDirectory(prefix='zamin-save-workflow-') as temporary:
  work=Path(temporary)
  for course in ((args.scene,) if args.scene else ('Save2D','Save3D')):
   project=work/course;shutil.copytree(ROOT/'Projects/SaveWorkshop',project,ignore=shutil.ignore_patterns('.zamin','Build'));manifest=project/'SaveWorkshop.zproject';data=json.loads(manifest.read_text());assert not data['Project']['ScriptModulePath'];data['Project']['StartScene']=f'Scenes/{course}.zscene';manifest.write_text(json.dumps(data,indent=2)+'\n');scene=project/f'Assets/Scenes/{course}.zscene';home=work/(course+'-home');home.mkdir()
   env=dict(os.environ,HOME=str(home),LIBGL_ALWAYS_SOFTWARE='1',LP_NUM_THREADS='2',ZAMIN_GL_DEBUG='1',ZAMIN_CAPTURE_FPS='30')
   for mode in ('write','resume','backup')+(() if args.no_editor else ('editor-write','editor-resume')):
    auto=mode.startswith('editor');resume=mode in ('resume','backup','editor-resume')
    if mode=='editor-write':shutil.rmtree(home/'.local',ignore_errors=True)
    if mode=='backup':
     files=list(home.rglob('gameplay-saves/Quick.json'));assert len(files)==1;file=files[0];shutil.copyfile(file,str(file)+'.bak');file.write_text('{corrupted')
    overrides(scene,auto,resume);log=out/f'{course}-{mode}.log';png=out/f'{course}-{mode}.png';runenv=dict(env,ZAMIN_MAX_FRAMES='150',ZAMIN_SCREENSHOT=str(png));runenv['ZAMIN_PLAYER_INPUT']='' if resume or auto else TAPE
    if auto:runenv.update(ZAMIN_EDITOR_PLAY='1',ZAMIN_EDITOR_SELECT='Player')
    cmd=[str(build/'bin'/('ZaminEditor' if auto else 'ZaminPlayer')),str(manifest)]+([] if auto else ['--no-splash'])
    with log.open('w') as stream:subprocess.run(runner+cmd,env=runenv,stdout=stream,stderr=subprocess.STDOUT,timeout=300,check=True)
    validate(log,resume);subprocess.run(['python3',str(ROOT/'Tests/png_check.py'),str(png)],check=True)
    files=list(home.rglob('gameplay-saves/Quick.json'));assert len(files)==1
    if mode!='backup':
     save=json.loads(files[0].read_text());assert save['Format']=='Zamin.GameSave' and save['Version']==1 and len(save['Payload']['Entities'])==3;assert files[0].stat().st_size<=2*1024*1024
    report.append({'Scene':course,'Mode':mode,'Passed':True});print(f'PASS {course}/{mode}: health, bag, consumed interaction, AI, pose, prefab',flush=True)
   if args.exports:
    overrides(scene,True,False);exportout=out/(course+'-export');exportwork=work/(course+'-export-work');exportenv=dict(env,ZAMIN_BUILD_DIR=str(build));exportenv.pop('ZAMIN_PLAYER_INPUT',None)
    subprocess.run(['bash',str(ROOT/'Tests/export_test.sh'),'--project',str(project),'--frames','150','--shot-frames','150','--work',str(exportwork),'--out',str(exportout)],env=exportenv,timeout=900,check=True)
    validate(exportout/'player.log',False);validate(exportout/'player-shot.log',True)
    # The moved export actually resumes the save, beyond the launcher's version smoke test.
    relocated=exportwork/'relocated-game';(exportwork/'export').rename(relocated)
    log=exportout/'relocated-resume.log';png=exportout/'relocated-resume.png';runenv=dict(exportenv,HOME=str(exportwork/'home'),ZAMIN_MAX_FRAMES='150',ZAMIN_SCREENSHOT=str(png))
    with log.open('w') as stream:subprocess.run(runner+[str(relocated/'SaveWorkshop.sh'),'--no-splash'],env=runenv,stdout=stream,stderr=subprocess.STDOUT,timeout=300,check=True)
    validate(log,True);subprocess.run(['python3',str(ROOT/'Tests/png_check.py'),str(png)],check=True);report.append({'Scene':course,'Mode':'export-write-fresh-resume-and-relocated-resume','Passed':True});print(f'PASS {course}/export: fresh and relocated save resume',flush=True)
 (out/'Report.json').write_text(json.dumps({'Passed':True,'Checks':report},indent=2)+'\n')
if __name__=='__main__':main()
