#!/usr/bin/env python3
"""Real X11/ImGui visual graph edit regression, then a typed function authored in the UI and run in the Player.
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
  if 'Scene opened:' in (work/'editor.log').read_text():break
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
 # Canvas shortcuts mutate a whole selection and remain undoable.
 x('keydown','ctrl');x('key','d');x('keyup','ctrl');time.sleep(1.5);save()
 assert len(read()['Nodes'])==13;print('UI: duplicate selection passed',flush=True)
 click(531,140);save();assert len(read()['Nodes'])==12
 # Select the new Constant again after Undo clears the selection.
 click(420,301);x('keydown','ctrl');x('key','c');x('keyup','ctrl');time.sleep(.5)
 x('keydown','ctrl');x('key','v');x('keyup','ctrl');time.sleep(1.5);save()
 assert len(read()['Nodes'])==13;print('UI: copy/paste + undo passed',flush=True)
 x('key','Delete');time.sleep(1.5);save()
 assert len(read()['Nodes'])==12;print('UI: delete pasted selection passed',flush=True)
 assert {n['ID'] for n in read()['Nodes']}==set(range(1,13))
 assert len(read()['Links'])==10
 original=read()
 x('keydown','ctrl');x('key','a');x('key','d');x('keyup','ctrl');time.sleep(1.5);save()
 assert len(read()['Nodes'])==24 and len(read()['Links'])==20
 x('key','Delete');time.sleep(1.5);save()
 assert read()['Nodes']==original['Nodes'] and read()['Links']==original['Links']
 print('UI: whole-group duplicate/delete preserves original nodes and wires',flush=True)
 click(1270,421);click(1270,477);save()
 assert 'Calculate' in read()['Functions'] and len(read()['Nodes'])==14
 assert read()['Functions']['Calculate']['Inputs'][0]['Name']=='X'
 assert read()['Functions']['Calculate']['Outputs'][0]['Name']=='Result'
 print('UI: create typed function signature + entry/return passed',flush=True)
 click(531,140);save();assert 'Calculate' not in read()['Functions'] and len(read()['Nodes'])==12
 print('UI: typed function creation undo passed',flush=True)

 subprocess.run(['import','-window','root',str(out/'editor-ui.png')],check=True)
except BaseException:
 subprocess.run(['import','-window','root',str(out/'editor-ui-failure.png')],check=True);raise
finally:
 app.terminate()
 try:app.wait(timeout=10)
 except subprocess.TimeoutExpired:app.kill();app.wait()
 log.close()
 shutil.copyfile(work/'editor.log',out/'editor.log')

# Phase 2: a typed function authored in the UI - create it, add a parameter, call it from the node library,
# wire it, set an argument, undo/redo, open it with a double-click - and run the saved graph in the Player.
flow=project/'Assets/Scripts/FunctionFlow.zgraph'
def node(i,kind,x,y,inputs=None):return {'ID':i,'Kind':kind,'X':x,'Y':y,'Inputs':inputs or {},'Breakpoint':False}
# A format 1 file: the editor upgrades it to format 2 on save.
flow.write_text(json.dumps({'Format':'Zamin.VisualGraph','Version':1,'Nodes':[node(1,'Event.Start',40,60),node(2,'Print',760,60),node(3,'Concat',560,420,{'A':'FUNCTION_UI result='}),node(4,'NumberToText',300,470)],
 'Links':[{'From':3,'Output':'Value','To':2,'Input':'Text'},{'From':4,'Output':'Value','To':3,'Input':'B'}],'Variables':{}},indent=2))
def readflow():return json.loads(flow.read_text())
def pixel(px,py):return subprocess.check_output(['import','-window','root','-crop',f'1x1+{px}+{py}','txt:-'],text=True).upper()
log=(work/'editor-functions.log').open('w')
app=subprocess.Popen([str(build/'bin/ZaminEditor'),str(project/'VisualWorkshop.zproject')],env=dict(env,ZAMIN_EDITOR_VISUAL_GRAPH='Scripts/FunctionFlow.zgraph'),stdout=log,stderr=subprocess.STDOUT)
try:
 for i in range(150):
  if app.poll() is not None:raise RuntimeError('Editor exited early')
  if 'Scene opened:' in (work/'editor-functions.log').read_text():break
  time.sleep(.2)
 for i in range(60):
  if '#7D2F4C' in pixel(400,235) or '#7D7D2F2F4C4C' in pixel(400,235):break
  time.sleep(.4)
 else:raise RuntimeError('Function graph window did not become ready')
 time.sleep(1)
 click(1284,357);click(1249,421);save()
 j=readflow();assert j['Version']==2 and 'Calculate' in j['Functions'] and len(j['Nodes'])==6,j
 print('UI: function created; the format 1 graph saved as format 2',flush=True)
 click(160,282);save()
 j=readflow();calls=[n for n in j['Nodes'] if n['Kind']=='Function.Call'];assert len(calls)==1 and calls[0]['Inputs']['Name']=='Calculate',j['Nodes']
 call=calls[0]['ID'];print('UI: call node from the node library passed',flush=True)
 click(507,265);click(393,340);click(582,340);click(937,265);click(582,360);click(541,617);save()
 j=readflow();assert len(j['Links'])==7,j['Links']
 assert {(l['From'],l['Output'],l['To'],l['Input']) for l in j['Links']}>={(1,'Then',call,'In'),(call,'Then',2,'In'),(call,'Result',4,'Number')}
 print('UI: call wired into Start -> Print',flush=True)
 click(1260,598);click(1278,684);click(1260,829);save()
 j=readflow();assert [p['Name'] for p in j['Functions']['Calculate']['Inputs']]==['X','Value2'],j['Functions']
 print('UI: parameter added to the signature',flush=True)
 subprocess.run(['import','-window','root',str(out/'function-signature.png')],check=True)
 click(1385,319);x('key','ctrl+a');time.sleep(.4);x('type','--delay','150','21');time.sleep(.4);x('key','Return');time.sleep(1.5);save()
 arg=lambda:next(n for n in readflow()['Nodes'] if n['ID']==call)['Inputs'].get('X')
 assert arg()==21,arg()
 click(531,140);save();assert arg()!=21,arg()
 click(585,140);save();assert arg()==21,arg()
 print('UI: call argument + undo/redo passed',flush=True)
 # Double-click the call: the function body is framed (its purple entry where Event.Start was) and selected.
 x('mousemove',480,312);time.sleep(.6);x('click','--repeat','2','--delay','80',1);time.sleep(2)
 assert '#533989' in pixel(420,237) or '#535339398989' in pixel(420,237),pixel(420,237)
 subprocess.run(['import','-window','root',str(out/'function-open.png')],check=True)
 print('UI: double-click opens the function',flush=True)
except BaseException:
 subprocess.run(['import','-window','root',str(out/'editor-functions-failure.png')],check=True);raise
finally:
 app.terminate()
 try:app.wait(timeout=10)
 except subprocess.TimeoutExpired:app.kill();app.wait()
 log.close()
 shutil.copyfile(work/'editor-functions.log',out/'editor-functions.log')
try:
 # Run: the Player starts the 2D scene with the authored graph on the Player entity.
 scene=project/'Assets/Scenes/Visual2D.zscene'
 scene.write_text(scene.read_text().replace('Scripts/Move2D.zgraph','Scripts/FunctionFlow.zgraph'))
 run=subprocess.run([str(build/'bin/ZaminPlayer'),str(project/'VisualWorkshop.zproject')],env=dict(env,ZAMIN_MAX_FRAMES='30'),capture_output=True,text=True,timeout=180)
 (out/'player-functions.log').write_text(run.stdout+run.stderr)
 assert run.returncode==0 and 'FUNCTION_UI result=21' in run.stdout+run.stderr,run.returncode
 assert "Visual graph '" not in run.stdout+run.stderr
 print('UI: the authored function runs in the Player (result=21)',flush=True)
finally:
 temporary.cleanup()
