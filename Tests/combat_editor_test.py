#!/usr/bin/env python3
"""Real X11/ImGui visual graph edit regression for the combat nodes (ProjectileDefine, EffectDefine, ProjectileFire):
each is added, saved and undone; on ProjectileDefine (Kind) and EffectDefine (Tick) an enumerated pin is changed
through its named-choice combo, saved, undone, redone and undone again, checking the saved graph each time.
Run under xvfb-run with a 1600x1000x24 screen. Requires xdotool and ImageMagick.
ZAMIN_BUILD_DIR chooses the build (default build); the sample is copied to a private temp directory.
"""
import os,subprocess,time,json,shutil,tempfile
from pathlib import Path

repo=Path(__file__).resolve().parent.parent;build=Path(os.environ.get('ZAMIN_BUILD_DIR',str(repo/'build'))).resolve();out=build/'GameplayCombat/Authoring';out.mkdir(parents=True,exist_ok=True);temporary=tempfile.TemporaryDirectory(prefix='zamin-combat-ui-');work=Path(temporary.name);project=work/'Project'
if project.exists():shutil.rmtree(project)
shutil.copytree(repo/'Projects/VisualWorkshop',project,ignore=shutil.ignore_patterns('.zamin','Build'))
(work/'home').mkdir(exist_ok=True)
env=dict(os.environ,HOME=str(work/'home'),ZAMIN_EDITOR_VISUAL_GRAPH='Scripts/Move2D.zgraph',ZAMIN_EDITOR_SELECT='Player',ZAMIN_MAX_FRAMES='10000')
log=(work/'editor.log').open('w');app=subprocess.Popen([str(build/'bin/ZaminEditor'),str(project/'VisualWorkshop.zproject')],env=env,stdout=log,stderr=subprocess.STDOUT)
def x(*args):subprocess.run(['xdotool',*map(str,args)],check=True,stdout=subprocess.DEVNULL)
def click(px,py):
 # Give GLFW a frame to observe the pointer before the button event on slow software renders.
 x('mousemove',px,py);time.sleep(1)
 x('mousedown',1);time.sleep(2);x('mouseup',1);time.sleep(2)
def read():return json.loads((project/'Assets/Scripts/Move2D.zgraph').read_text())
def save():click(498,140);time.sleep(1.2)
# Selected-node panel rows (an added node is selected): the combo of an enumerated pin, then its popup rows.
CHOICES={'Gameplay.ProjectileDefine':('Kind',373,1,'Davolash'),'Gameplay.EffectDefine':('Tick',437,1,'Zarar')}
def saved(expect,check):
 # Save until the file shows the expected state (only Save is retried: it is idempotent).
 for attempt in range(4):
  save()
  if check(read()):return
  time.sleep(1)
 raise AssertionError(f'{expect}: {read()["Nodes"][-1]}')
def choose(kind):
 pin,y,index,label=CHOICES[kind]
 click(1385,y);click(1332,y+31+22*index)
 saved(f'{pin}={label}',lambda g:len(g['Nodes'])==12 and g['Nodes'][-1]['Inputs'].get(pin)==index)
 subprocess.run(['import','-window','root',str(out/f'combat-choice-{pin}.png')],check=True)
 click(607,140) # Undo: the pin returns to its default (not stored)
 saved(f'{pin} undone',lambda g:len(g['Nodes'])==12 and g['Nodes'][-1]['Kind']==kind and g['Nodes'][-1]['Inputs'].get(pin,0)==0)
 click(744,140) # Redo
 saved(f'{pin} redone',lambda g:len(g['Nodes'])==12 and g['Nodes'][-1]['Inputs'].get(pin)==index)
 click(607,140)
 saved(f'{pin} undone again',lambda g:len(g['Nodes'])==12 and g['Nodes'][-1]['Inputs'].get(pin,0)==0)
 print(f'UI: {kind} {pin} combo -> {label}, save, Undo/Redo passed',flush=True)
try:
 for i in range(100):
  if app.poll() is not None:raise RuntimeError('Editor exited early')
  if 'Sahna ochildi:' in (work/'editor.log').read_text():break
  time.sleep(.2)
 for i in range(60):
  pixel=subprocess.check_output(['import','-window','root','-crop','1x1+450+230','txt:-'],text=True)
  if '#7D2F4C' in pixel.upper() or '#7D7D2F2F4C4C' in pixel.upper():break
  time.sleep(.4)
 else:raise RuntimeError('Graph window did not become ready')
 time.sleep(1)
 for kind in ('Gameplay.ProjectileDefine', 'Gameplay.EffectDefine', 'Gameplay.ProjectileFire'):
  click(150,233);x('keydown','ctrl');x('key','a');x('keyup','ctrl');x('type','--clearmodifiers','--delay','120',kind);time.sleep(1.5)
  click(155,283)
  # Retry only the idempotent Save action; repeating Add can duplicate a delayed click.
  for attempt in range(4):
   save()
   if len(read()['Nodes'])==12:break
   assert len(read()['Nodes'])==11,read()['Nodes']
   time.sleep(1)
  assert len(read()['Nodes'])==12 and read()['Nodes'][-1]['Kind']==kind
  if kind=='Gameplay.ProjectileDefine':subprocess.run(['import','-window','root',str(out/'combat-authoring.png')],check=True)
  if kind in CHOICES:choose(kind)
  click(607,140)
  for attempt in range(4):
   save()
   if len(read()['Nodes'])==11:break
   time.sleep(1)
  assert len(read()['Nodes'])==11
  print('UI: '+kind+' add/save/Undo passed',flush=True)
 subprocess.run(['import','-window','root',str(out/'editor-ui.png')],check=True)
except BaseException:
 subprocess.run(['import','-window','root',str(out/'editor-ui-failure.png')],check=True);raise
finally:
 app.terminate()
 try:app.wait(timeout=10)
 except subprocess.TimeoutExpired:app.kill();app.wait()
 log.close()
 shutil.copyfile(work/'editor.log',out/'editor.log')
 temporary.cleanup()
