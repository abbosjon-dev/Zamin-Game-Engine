#!/usr/bin/env python3
"""Real X11/ImGui visual graph edit regression.
Run under xvfb-run with a 1600x1000x24 screen. Requires xdotool and ImageMagick.
ZAMIN_BUILD_DIR chooses the build (default build); the sample is copied to a private temp directory.
"""
import os,subprocess,time,json,shutil,tempfile
from pathlib import Path

repo=Path(__file__).resolve().parent.parent;build=Path(os.environ.get('ZAMIN_BUILD_DIR',str(repo/'build'))).resolve();out=build/'VisualEditorTest';out.mkdir(parents=True,exist_ok=True);temporary=tempfile.TemporaryDirectory(prefix='zamin-visual-ui-');work=Path(temporary.name);project=work/'Project'
if project.exists():shutil.rmtree(project)
shutil.copytree(repo/'Projects/VisualWorkshop',project,ignore=shutil.ignore_patterns('.zamin','Build'))
(work/'home').mkdir(exist_ok=True)
env=dict(os.environ,HOME=str(work/'home'),ZAMIN_EDITOR_VISUAL_GRAPH='Scripts/Move2D.zgraph',ZAMIN_EDITOR_SELECT='Player',ZAMIN_MAX_FRAMES='10000')
log=(work/'editor.log').open('w');app=subprocess.Popen([str(build/'bin/ZaminEditor'),str(project/'VisualWorkshop.zproject')],env=env,stdout=log,stderr=subprocess.STDOUT)
def x(*args):subprocess.run(['xdotool',*map(str,args)],check=True,stdout=subprocess.DEVNULL)
def click(px,py):
 # Give GLFW a frame to observe the pointer before the button event on slow software renders.
 x('mousemove',px,py);time.sleep(.6)
 x('mousedown',1);time.sleep(1);x('mouseup',1);time.sleep(1.2)
def read():return json.loads((project/'Assets/Scripts/Move2D.zgraph').read_text())
def save():click(473,140);time.sleep(1.2)
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
 click(150,233);x('keydown','ctrl');time.sleep(.3);x('key','a');time.sleep(.3);x('keyup','ctrl');time.sleep(.3);x('type','--clearmodifiers','--delay','180','Constant.Number');time.sleep(1.2);click(155,283);save()
 j=read();assert len(j['Nodes'])==12,len(j['Nodes']);assert j['Nodes'][-1]['Kind']=='Constant.Number';print('UI: add node + save passed',flush=True)
 click(531,140);save();assert len(read()['Nodes'])==11;print('UI: undo passed',flush=True)
 click(585,140);save();assert len(read()['Nodes'])==12;print('UI: redo passed',flush=True)
 # Link the event's Number output to the new Constant.Number input.
 click(497,281);click(394,338);save();j=read();assert len(j['Links'])==10,j['Links'];assert any(l['From']==1 and l['Output']=='Delta' and l['To']==12 and l['Input']=='Value' for l in j['Links']);print('UI: typed wire + save passed',flush=True)
 click(497,281);click(960,301);save();assert len(read()['Links'])==10;print('UI: incompatible wire rejected',flush=True)
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
