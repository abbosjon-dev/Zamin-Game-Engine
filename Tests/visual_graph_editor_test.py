#!/usr/bin/env python3
"""Real X11/ImGui visual graph edit regression, then a typed function, a graph struct and an inline macro (with a nested struct
field and an enum array variable) authored in the UI and run in the Player.
Run under xvfb-run with a 1600x1000x24 screen. Requires xdotool and ImageMagick.
ZAMIN_BUILD_DIR chooses the build (default build); the sample is copied to a private temp directory.
"""
import os,subprocess,time,json,shutil,tempfile
from pathlib import Path

repo=Path(__file__).resolve().parent.parent;build=Path(os.environ.get('ZAMIN_BUILD_DIR',str(repo/'build'))).resolve();out=build/'VisualEditorTest';out.mkdir(parents=True,exist_ok=True);temporary=tempfile.TemporaryDirectory(prefix='zamin-visual-ui-');work=Path(temporary.name);project=work/'Project'
if project.exists():shutil.rmtree(project)
shutil.copytree(repo/'Projects/VisualWorkshop',project,ignore=shutil.ignore_patterns('.zamin','Build'))
(work/'home').mkdir(exist_ok=True)
layout=work/'layout.json'
# The editor writes the screen rectangles of the properties column's widgets (ZAMIN_EDITOR_VISUAL_LAYOUT); the test clicks
# them by key ("<scope>/<id>") instead of fixed coordinates and checks that none of them is clipped at the right edge.
env=dict(os.environ,HOME=str(work/'home'),ZAMIN_EDITOR_VISUAL_LAYOUT=str(layout),ZAMIN_EDITOR_VISUAL_GRAPH='Scripts/Move2D.zgraph',ZAMIN_EDITOR_SELECT='Player',ZAMIN_MAX_FRAMES='10000')
log=(work/'editor.log').open('w');app=subprocess.Popen([str(build/'bin/ZaminEditor'),str(project/'VisualWorkshop.zproject')],env=env,stdout=log,stderr=subprocess.STDOUT)
def x(*args):subprocess.run(['xdotool',*map(str,args)],check=True,stdout=subprocess.DEVNULL)
def click(px,py):
 # Give GLFW a frame to observe the pointer before the button event on slow software renders.
 x('mousemove',px,py);time.sleep(.6)
 x('mousedown',1);time.sleep(1);x('mouseup',1);time.sleep(1.2)
def items():
 for i in range(20):
  try:return json.loads(layout.read_text())
  except (OSError,ValueError):time.sleep(.3)
 raise RuntimeError('No layout file')
def find(key):
 # The rectangle of a recorded widget, scrolled into the properties column first (canvas nodes/pins: as drawn).
 for attempt in range(40):
  data=items();region=data['Regions'].get('properties');match=[v['Rect'] for k,v in data['Items'].items() if k==key or k.endswith('/'+key)]
  # Canvas nodes/pins, the header's buttons and an open combo's entries are clicked where they are drawn.
  if match and key.startswith(('canvas/','header/','popup/')):return int((match[0][0]+match[0][2])/2),int((match[0][1]+match[0][3])/2)
  if match and region:
   r=match[0];cx,cy=int((region[0]+region[2])/2),int((region[1]+region[3])/2)
   if r[3]>region[3]-4:x('mousemove',cx,cy);time.sleep(.3);x('click',5);time.sleep(.8);continue
   if r[1]<region[1]+4:x('mousemove',cx,cy);time.sleep(.3);x('click',4);time.sleep(.8);continue
   return int((r[0]+r[2])/2),int((r[1]+r[3])/2)
  time.sleep(.4)
 raise RuntimeError('Widget not found in the properties column: '+key)
def press(key):click(*find(key))
def drag(key,dx,dy):
 # Drags a canvas node by its header by (dx, dy) graph units (the header is 220 units wide at any zoom).
 r=next(v['Rect'] for k,v in items()['Items'].items() if k==key);zoom=(r[2]-r[0])/220;dx,dy=int(dx*zoom),int(dy*zoom)
 px,py=find(key);x('mousemove',px,py);time.sleep(.6);x('mousedown',1);time.sleep(.4)
 # Small steps: the header must stay under the pointer from one (slow, software-rendered) frame to the next.
 steps=max(abs(dx),abs(dy))//8+1
 for i in range(1,steps+1):x('mousemove',px+dx*i//steps,py+dy*i//steps);time.sleep(.12)
 time.sleep(.4);x('mouseup',1);time.sleep(1.2)
def selectall():
 # Ctrl held over a few frames: a slow software-rendered frame can miss a quick ctrl+a chord.
 x('keydown','ctrl');time.sleep(.3);x('key','a');time.sleep(.3);x('keyup','ctrl')
def enter(key,text):
 # Replaces the text of an input field and commits it with Enter.
 press(key);selectall();time.sleep(.4);x('type','--delay','150',text);time.sleep(.4);x('key','Return');time.sleep(1.5)
def unclipped():
 clipped=[k for k,v in items()['Items'].items() if v['Clipped']]
 assert not clipped,clipped
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
 press('Typed functions');press('functions/Create function');save();unclipped()
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
# A format 1 file: the editor upgrades it to the current format on save.
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
 press('Typed functions');press('functions/Create function');save()
 j=readflow();assert j['Version']==4 and 'Calculate' in j['Functions'] and len(j['Nodes'])==6,j
 print('UI: function created; the format 1 graph saved as format 4',flush=True)
 click(160,282);save()
 j=readflow();calls=[n for n in j['Nodes'] if n['Kind']=='Function.Call'];assert len(calls)==1 and calls[0]['Inputs']['Name']=='Calculate',j['Nodes']
 call=calls[0]['ID'];print('UI: call node from the node library passed',flush=True)
 click(507,265);click(393,340);click(582,340);click(937,265);click(582,360);click(541,617);save()
 j=readflow();assert len(j['Links'])==7,j['Links']
 assert {(l['From'],l['Output'],l['To'],l['Input']) for l in j['Links']}>={(1,'Then',call,'In'),(call,'Then',2,'In'),(call,'Result',4,'Number')}
 print('UI: call wired into Start -> Print',flush=True)
 press('functions/Calculate');press('Calculate/Parameters');press('Calculate/Parameters/Add pin');save();unclipped()
 j=readflow();assert [p['Name'] for p in j['Functions']['Calculate']['Inputs']]==['X','Value2'],j['Functions']
 print('UI: parameter added to the signature',flush=True)
 subprocess.run(['import','-window','root',str(out/'function-signature.png')],check=True)
 press('node/X');selectall();time.sleep(.4);x('type','--delay','150','21');time.sleep(.4);x('key','Return');time.sleep(1.5);save()
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
# Phase 3: a graph struct authored in the UI - create it, add and rename fields, Make it from the node library with a
# literal, read a field with GetField, wire it, rename the wired field (the wires stay), undo/redo - and run it in the Player.
def struct_flow(project,env,build,out,scene):
 global app
 flow=project/'Assets/Scripts/StructFlow.zgraph'
 # A format 2 file (no Structs section): the editor writes the current format (4) on save.
 flow.write_text(json.dumps({'Format':'Zamin.VisualGraph','Version':2,'Nodes':[node(1,'Event.Start',40,60),node(2,'Print',760,60),node(3,'Concat',560,420,{'A':'STRUCT_UI value='}),node(4,'FormatNumber',300,470,{'Decimals':0})],
  'Links':[{'From':1,'Output':'Then','To':2,'Input':'In'},{'From':3,'Output':'Value','To':2,'Input':'Text'},{'From':4,'Output':'Value','To':3,'Input':'B'}],'Variables':{},'Functions':{},'Enums':{}},indent=2))
 readstruct=lambda:json.loads(flow.read_text())
 log=(work/'editor-structs.log').open('w')
 app=subprocess.Popen([str(build/'bin/ZaminEditor'),str(project/'VisualWorkshop.zproject')],env=dict(env,ZAMIN_EDITOR_VISUAL_GRAPH='Scripts/StructFlow.zgraph'),stdout=log,stderr=subprocess.STDOUT)
 try:
  for i in range(150):
   if app.poll() is not None:raise RuntimeError('Editor exited early')
   if 'Scene opened:' in (work/'editor-structs.log').read_text():break
   time.sleep(.2)
  for i in range(60):
   if '#7D2F4C' in pixel(400,235) or '#7D7D2F2F4C4C' in pixel(400,235):break
   time.sleep(.4)
  else:raise RuntimeError('Struct graph window did not become ready')
  time.sleep(1)
  press('Graph variables');press('Graph structs');press('structs/Create struct');press('structs/Stats');press('Stats/Add field')
  enter('Stats/0/Field name','Power');save()
  j=readstruct();assert j['Version']==4 and [f['Name'] for f in j['Structs']['Stats']['Fields']]==['Power','Field2'],j.get('Structs')
  unclipped();print('UI: struct created, field added and renamed; saved as format 4',flush=True)
  subprocess.run(['import','-window','root',str(out/'struct-editor.png')],check=True)
  # Make Stats from the node library (Structs / Make Stats), Power = 7; then a GetField node (struct and field preset).
  click(150,233);selectall();time.sleep(.3);x('type','--delay','150','make stats');time.sleep(1.2);click(155,283)
  drag('canvas/node/5',-75,115);enter('node/Power','7')
  click(150,233);selectall();time.sleep(.3);x('type','--delay','150','struct.getfield');time.sleep(1.2);click(155,283)
  drag('canvas/node/6',215,115);save()
  j=readstruct();kinds={n['ID']:(n['Kind'],n['Inputs']) for n in j['Nodes']}
  assert kinds[5]==('Struct.Make',{'Struct':'Stats','Power':7}) and kinds[6]==('Struct.GetField',{'Struct':'Stats','Field':'Power'}),kinds
  print('UI: Make/GetField from the node library with the struct, field and literal',flush=True)
  press('canvas/5/out/Value');press('canvas/6/in/Value');press('canvas/6/out/Value');press('canvas/4/in/Number');save()
  wires=lambda:{(l['From'],l['Output'],l['To'],l['Input']) for l in readstruct()['Links']}
  assert {(5,'Value',6,'Value'),(6,'Value',4,'Number')}<=wires(),wires()
  print('UI: struct wires typed and connected',flush=True)
  # Renaming the wired field keeps both wires and moves the Make literal and the GetField field with it.
  enter('Stats/0/Field name','Strength');save()
  j=readstruct();kinds={n['ID']:n['Inputs'] for n in j['Nodes']}
  assert {(5,'Value',6,'Value'),(6,'Value',4,'Number')}<=wires() and kinds[5].get('Strength')==7 and kinds[6]['Field']=='Strength',(wires(),kinds)
  click(531,140);save();assert readstruct()['Structs']['Stats']['Fields'][0]['Name']=='Power' and next(n for n in readstruct()['Nodes'] if n['ID']==6)['Inputs']['Field']=='Power'
  click(585,140);save();assert readstruct()['Structs']['Stats']['Fields'][0]['Name']=='Strength' and {(5,'Value',6,'Value'),(6,'Value',4,'Number')}<=wires()
  unclipped();print('UI: field rename keeps wires; undo/redo restore it',flush=True)
  subprocess.run(['import','-window','root',str(out/'struct-graph.png')],check=True)
 except BaseException:
  subprocess.run(['import','-window','root',str(out/'editor-structs-failure.png')],check=True);raise
 finally:
  app.terminate()
  try:app.wait(timeout=10)
  except subprocess.TimeoutExpired:app.kill();app.wait()
  log.close()
  shutil.copyfile(work/'editor-structs.log',out/'editor-structs.log')
 scene.write_text(scene.read_text().replace('Scripts/FunctionFlow.zgraph','Scripts/StructFlow.zgraph'))
 run=subprocess.run([str(build/'bin/ZaminPlayer'),str(project/'VisualWorkshop.zproject')],env=dict(env,ZAMIN_MAX_FRAMES='30'),capture_output=True,text=True,timeout=180)
 (out/'player-structs.log').write_text(run.stdout+run.stderr)
 assert run.returncode==0 and 'STRUCT_UI value=7' in run.stdout+run.stderr and "Visual graph '" not in run.stdout+run.stderr,run.returncode
 print('UI: the authored struct graph runs in the Player (value=7)',flush=True)
# Phase 4: a nested struct field, an enum array variable and an inline macro authored in the UI - the macro is created, given
# an output, its pin renamed (undo/redo), opened, its body edited, used from the node library, wired, renamed, opened by a
# double-click - and the graph run in the Player.
def macro_flow(project,env,build,out,scene):
 global app
 flow=project/'Assets/Scripts/MacroFlow.zgraph'
 # A format 3 file with an enum and a struct: the editor writes format 4 on save.
 flow.write_text(json.dumps({'Format':'Zamin.VisualGraph','Version':3,'Nodes':[node(1,'Event.Start',40,60),node(2,'Print',760,60),node(3,'Concat',560,420,{'A':'MACRO_UI value='}),node(4,'FormatNumber',300,470,{'Decimals':0})],
  'Links':[{'From':3,'Output':'Value','To':2,'Input':'Text'},{'From':4,'Output':'Value','To':3,'Input':'B'}],'Variables':{},'Functions':{},
  'Enums':{'Rank':{'Entries':[{'Name':'Bronze','Value':0},{'Name':'Gold','Value':1}],'Source':''}},
  'Structs':{'Point':{'Fields':[{'Name':'X','Type':'Number','Default':0},{'Name':'Y','Type':'Number','Default':0}]}}},indent=2))
 readmacro=lambda:json.loads(flow.read_text())
 shot=lambda name:subprocess.run(['import','-window','root',str(out/name)],check=True)
 log=(work/'editor-macros.log').open('w')
 app=subprocess.Popen([str(build/'bin/ZaminEditor'),str(project/'VisualWorkshop.zproject')],env=dict(env,ZAMIN_EDITOR_VISUAL_GRAPH='Scripts/MacroFlow.zgraph'),stdout=log,stderr=subprocess.STDOUT)
 try:
  for i in range(150):
   if app.poll() is not None:raise RuntimeError('Editor exited early')
   if 'Scene opened:' in (work/'editor-macros.log').read_text():break
   time.sleep(.2)
  for i in range(60):
   if '#7D2F4C' in pixel(400,235) or '#7D7D2F2F4C4C' in pixel(400,235):break
   time.sleep(.4)
  else:raise RuntimeError('Macro graph window did not become ready')
  time.sleep(1)
  # A struct field of struct type: Stats.Value becomes a Point, whose default is edited field by field.
  press('Graph variables');press('Graph structs');press('structs/Create struct');press('structs/Stats')
  press('Stats/0/Field type');press('popup/Struct');press('Stats/0/Default/fields');enter('Stats/0/Default/X','5');save()
  field=readmacro()['Structs']['Stats']['Fields'][0]
  assert readmacro()['Version']==4 and field['Type']=='Struct' and field['Struct']=='Point' and field['Default']=={'X':5,'Y':0},field
  unclipped();shot('nested-struct.png');print('UI: a struct field of struct type, its nested default edited field by field; saved as format 4',flush=True)
  press('Graph structs')
  # An enum array variable.
  press('Graph variables');press('variables/Variable name');x('type','--delay','150','Ranks');time.sleep(.4)
  press('variables/Type');press('popup/ArrayEnum');press('variables/Add variable');save()
  assert readmacro()['Variables']['Ranks']=={'Type':'ArrayEnum','Default':[],'Enum':'Rank'},readmacro()['Variables']
  unclipped();print('UI: an ArrayEnum variable of the graph enum',flush=True)
  press('Graph variables')
  # The macro: created with In -> Then, an output added and renamed (one undo step each).
  press('Macros');press('macros/Create macro');press('macros/Step');press('Step/Outputs');press('Step/Outputs/Add pin')
  enter('Step/Outputs/1/Pin name','Result');save()
  j=readmacro();body={n['ID']:n for n in j['Nodes'] if n.get('Macro')=='Step'}
  assert [p['Name'] for p in j['Macros']['Step']['Outputs']]==['Then','Result'] and {n['Kind'] for n in body.values()}=={'Macro.Entry','Macro.Exit'},(j['Macros'],body)
  entry=next(i for i,n in body.items() if n['Kind']=='Macro.Entry');exit_=next(i for i,n in body.items() if n['Kind']=='Macro.Exit')
  click(531,140);save();assert [p['Name'] for p in readmacro()['Macros']['Step']['Outputs']]==['Then','Value2']
  click(585,140);save();assert [p['Name'] for p in readmacro()['Macros']['Step']['Outputs']]==['Then','Result']
  unclipped();print('UI: macro created, output added and renamed; undo/redo restore the rename',flush=True)
  # Open it: the canvas shows the body only; a Constant from the node library goes into the body, wired to Result.
  press('Step/Open macro');time.sleep(1)
  keys=items()['Items'];assert f'canvas/node/{entry}' in keys and 'canvas/node/1' not in keys,[k for k in keys if k.startswith('canvas/node')]
  click(150,233);selectall();time.sleep(.3);x('type','--delay','150','Constant.Number');time.sleep(1.2);click(155,283)
  const=max(n['ID'] for n in readmacro()['Nodes'])+1
  drag(f'canvas/node/{const}',0,160);enter('node/Value','7')
  press(f'canvas/{const}/out/Value');press(f'canvas/{exit_}/in/Result');save()
  j=readmacro();inner=next(n for n in j['Nodes'] if n['ID']==const)
  assert inner['Macro']=='Step' and inner['Inputs']['Value']==7 and {'From':const,'Output':'Value','To':exit_,'Input':'Result'} in j['Links'],(inner,j['Links'])
  shot('macro-body.png');print('UI: the macro opens; its body is edited from the node library',flush=True)
  # Back to the graph: the macro from the node library, wired between Start and Print, its Result into the text.
  press('header/Whole graph');time.sleep(1);assert 'canvas/node/1' in items()['Items']
  click(150,233);selectall();time.sleep(.3);x('type','--delay','150','step');time.sleep(1.2);click(155,283)
  instance=const+1;drag(f'canvas/node/{instance}',300,140)
  for a,b in (('1/out/Then',f'{instance}/in/In'),(f'{instance}/out/Then','2/in/In'),(f'{instance}/out/Result','4/in/Number')):press('canvas/'+a);press('canvas/'+b)
  save();j=readmacro();wires={(l['From'],l['Output'],l['To'],l['Input']) for l in j['Links']}
  assert next(n for n in j['Nodes'] if n['ID']==instance)['Inputs']=={'Name':'Step'} and {(1,'Then',instance,'In'),(instance,'Then',2,'In'),(instance,'Result',4,'Number')}<=wires,(j['Nodes'],wires)
  print('UI: the macro from the node library, wired into Start -> Print',flush=True)
  # Renaming the macro renames its instance and its body; a double-click on the instance opens the body.
  enter('Step/Rename macro','Seven');save();j=readmacro()
  assert 'Seven' in j['Macros'] and next(n for n in j['Nodes'] if n['ID']==instance)['Inputs']['Name']=='Seven' and all(n.get('Macro')=='Seven' for n in j['Nodes'] if n['ID'] in (entry,exit_,const))
  unclipped();shot('macro-graph.png')
  px,py=find(f'canvas/node/{instance}');x('mousemove',px,py);time.sleep(.6);x('click','--repeat','2','--delay','80',1);time.sleep(2)
  keys=items()['Items'];assert f'canvas/node/{const}' in keys and f'canvas/node/{instance}' not in keys,[k for k in keys if k.startswith('canvas/node')]
  shot('macro-open.png');print('UI: macro renamed with its instance; a double-click opens it',flush=True)
 except BaseException:
  shot('editor-macros-failure.png');raise
 finally:
  app.terminate()
  try:app.wait(timeout=10)
  except subprocess.TimeoutExpired:app.kill();app.wait()
  log.close()
  shutil.copyfile(work/'editor-macros.log',out/'editor-macros.log')
 scene.write_text(scene.read_text().replace('Scripts/StructFlow.zgraph','Scripts/MacroFlow.zgraph'))
 run=subprocess.run([str(build/'bin/ZaminPlayer'),str(project/'VisualWorkshop.zproject')],env=dict(env,ZAMIN_MAX_FRAMES='30'),capture_output=True,text=True,timeout=180)
 (out/'player-macros.log').write_text(run.stdout+run.stderr)
 assert run.returncode==0 and 'MACRO_UI value=7' in run.stdout+run.stderr and "Visual graph '" not in run.stdout+run.stderr,run.returncode
 print('UI: the authored macro graph runs in the Player (value=7)',flush=True)
try:
 # Run: the Player starts the 2D scene with the authored graph on the Player entity.
 scene=project/'Assets/Scenes/Visual2D.zscene'
 scene.write_text(scene.read_text().replace('Scripts/Move2D.zgraph','Scripts/FunctionFlow.zgraph'))
 run=subprocess.run([str(build/'bin/ZaminPlayer'),str(project/'VisualWorkshop.zproject')],env=dict(env,ZAMIN_MAX_FRAMES='30'),capture_output=True,text=True,timeout=180)
 (out/'player-functions.log').write_text(run.stdout+run.stderr)
 assert run.returncode==0 and 'FUNCTION_UI result=21' in run.stdout+run.stderr,run.returncode
 assert "Visual graph '" not in run.stdout+run.stderr
 print('UI: the authored function runs in the Player (result=21)',flush=True)
 struct_flow(project,env,build,out,scene)
 macro_flow(project,env,build,out,scene)
finally:
 temporary.cleanup()
