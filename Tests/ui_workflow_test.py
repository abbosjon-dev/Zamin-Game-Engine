#!/usr/bin/env python3
"""Actual keyboard/gamepad modal UI, paged inventory, native HUD and pause/save/load acceptance."""
import argparse,json,os,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
ACCEPT='UI_ACCEPTANCE hud=1 inventory=1 page=1 focus=1 modal=1 pause=1 save=1 load=1'
CAPTURE='UI_INVENTORY_SHOWN page=1 slots=6'
TAPE='15:hold=D;28:release=D;35:I;45:Right;55:Down;65:Down;70:Enter;80:Right;85:Enter;95:Left;100:Left;105:Enter;115:Escape;125:pad=Start;135:pad=DPadDown;145:pad=DPadDown;150:pad=A;160:pad=B;170:pad=B;185:I;190:Escape;200:Escape'
def validate(log,capture=False):
 text=log.read_text(errors='replace')
 for marker in ('GL_INVALID_','Assertion failed',"Visual graph '",'Visual graph error','UI_ORACLE_FAILED','UI_OPERATION_FAILED','UI_SAVE_FAILED','AI_CONFIG_FAILED'):
  if marker in text:raise RuntimeError(f'{marker}: {log}')
 if (CAPTURE if capture else ACCEPT) not in text:raise RuntimeError(f'Missing UI acceptance: {log}')
def overrides(scene,auto,capture=False):
 data=json.loads(scene.read_text())
 for e in data['Entities']:
  if e['Entity']==900:e['NativeScript']['Fields']['Overrides']={'Type':'String','Value':json.dumps({'Auto':auto,'Checks':not capture,'Capture':capture})}
 scene.write_text(json.dumps(data,indent=2)+'\n')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,default=ROOT/'build');p.add_argument('--out',type=Path);p.add_argument('--no-editor',action='store_true');p.add_argument('--exports',action='store_true');p.add_argument('--scene',choices=('UI2D','UI3D'));args=p.parse_args();build=args.build.resolve();out=(args.out or build/'GameplayUI/Workflow').resolve();out.mkdir(parents=True,exist_ok=True);runner=[] if os.environ.get('DISPLAY') else ['xvfb-run','-a','-s','-screen 0 1600x1000x24 -nolisten tcp'];report=[]
 with tempfile.TemporaryDirectory(prefix='zamin-ui-workflow-') as temporary:
  work=Path(temporary)
  for course in ((args.scene,) if args.scene else ('UI2D','UI3D')):
   project=work/course;shutil.copytree(ROOT/'Projects/UIWorkshop',project,ignore=shutil.ignore_patterns('.zamin','Build'));manifest=project/'UIWorkshop.zproject';data=json.loads(manifest.read_text());assert not data['Project']['ScriptModulePath'];data['Project']['StartScene']=f'Scenes/{course}.zscene';manifest.write_text(json.dumps(data,indent=2)+'\n');scene=project/f'Assets/Scenes/{course}.zscene'
   env=dict(os.environ,LIBGL_ALWAYS_SOFTWARE='1',LP_NUM_THREADS='2',ZAMIN_GL_DEBUG='1',ZAMIN_CAPTURE_FPS='30')
   for mode in ('player','inventory','scaled-inventory')+(() if args.no_editor else ('editor',)):
    auto=mode=='editor';capture='inventory' in mode;overrides(scene,auto,capture);home=work/(course+'-'+mode+'-home');home.mkdir();log=out/f'{course}-{mode}.log';png=out/f'{course}-{mode}.png';runenv=dict(env,HOME=str(home),ZAMIN_MAX_FRAMES='110' if capture else '225',ZAMIN_SCREENSHOT=str(png),ZAMIN_PLAYER_INPUT='' if auto else TAPE)
    if auto:runenv.update(ZAMIN_EDITOR_PLAY='1',ZAMIN_EDITOR_SELECT='Player')
    cmd=[str(build/'bin'/('ZaminEditor' if auto else 'ZaminPlayer')),str(manifest)]+([] if auto else ['--no-splash'])
    if mode=='scaled-inventory':cmd+=['--resolution','1024x768']
    with log.open('w') as stream:subprocess.run(runner+cmd,env=runenv,stdout=stream,stderr=subprocess.STDOUT,timeout=360,check=True)
    validate(log,capture);subprocess.run(['python3',str(ROOT/'Tests/png_check.py'),str(png)],check=True)
    files=list(home.rglob('gameplay-saves/Quick.json'));assert len(files)==1;save=json.loads(files[0].read_text());assert save['Format']=='Zamin.GameSave' and len(save['Payload']['Entities'])==2
    player=next(e for e in save['Payload']['Entities'] if e['Key']=='Player');assert player['Health']==75 and player['Items']['Coin']==8
    report.append({'Scene':course,'Mode':mode,'Passed':True});print(f'PASS {course}/{mode}: modal scope, focus, live HUD, inventory pages, pause and save/load',flush=True)
   if args.exports:
    overrides(scene,True);exportout=out/(course+'-export');exportenv=dict(env,ZAMIN_BUILD_DIR=str(build))
    subprocess.run(['bash',str(ROOT/'Tests/export_test.sh'),'--project',str(project),'--frames','225','--shot-frames','225','--out',str(exportout)],env=exportenv,timeout=900,check=True)
    validate(exportout/'player.log');validate(exportout/'player-shot.log');report.append({'Scene':course,'Mode':'compiler-free-export-and-relocated-version-check','Passed':True});print(f'PASS {course}/export',flush=True)
 (out/'Report.json').write_text(json.dumps({'Passed':True,'Checks':report},indent=2)+'\n')
if __name__=='__main__':main()
