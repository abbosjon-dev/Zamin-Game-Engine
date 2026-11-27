#!/usr/bin/env python3
"""Real X11/ImGui visual graph edit regression, then a typed function, a graph struct, an inline macro (with a nested struct
field and an enum array variable) and a child graph (a parent set, a curve variable edited key by key, an inherited function
overridden with a call to the parent's, an inherited event overridden) authored in the UI and run in the Player; last
(phase 6) native C++ methods from the node library through a hot reload that changes a signature, and a large graph
played with the heat overlay, the canvas culling and the Visual Script Profiler.
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
  if match and key.startswith(('canvas/','header/','popup/','palette/','status/','profiler/')):return int((match[0][0]+match[0][2])/2),int((match[0][1]+match[0][3])/2)
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
 j=readflow();assert j['Version']==6 and 'Calculate' in j['Functions'] and len(j['Nodes'])==6,j
 print('UI: function created; the format 1 graph saved as format 6',flush=True)
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
 # A format 2 file (no Structs section): the editor writes the current format (5) on save.
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
  j=readstruct();assert j['Version']==6 and [f['Name'] for f in j['Structs']['Stats']['Fields']]==['Power','Field2'],j.get('Structs')
  unclipped();print('UI: struct created, field added and renamed; saved as format 6',flush=True)
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
 # A format 3 file with an enum and a struct: the editor writes format 6 on save.
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
  assert readmacro()['Version']==6 and field['Type']=='Struct' and field['Struct']=='Point' and field['Default']=={'X':5,'Y':0},field
  unclipped();shot('nested-struct.png');print('UI: a struct field of struct type, its nested default edited field by field; saved as format 6',flush=True)
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
# Phase 5: graph inheritance and curves authored in the UI - a parent set (undo/redo) whose members show as inherited, a Curve
# variable edited key by key (add, move by field and by dragging in the preview, interpolation, delete; undo/redo), an
# inherited function overridden (undo/redo) and its body extended with the curve from the node library, an inherited event
# overridden - and the child run in the Player through the parent's Start.
def inherit_flow(project,env,build,out,scene):
 global app
 base=project/'Assets/Scripts/CurveBase.zgraph';flow=project/'Assets/Scripts/CurveChild.zgraph'
 # Format 4 files: the parent (Start: Print(Bonus(Level)), Bonus(X) = X + 1) and the child-to-be (one Create handler).
 base.write_text(json.dumps({'Format':'Zamin.VisualGraph','Version':4,'Nodes':[node(1,'Event.Start',40,60),node(2,'Get.Number',40,240,{'Name':'Level'}),node(3,'Function.Call',300,60,{'Name':'Bonus'}),
  node(4,'FormatNumber',560,240,{'Decimals':2}),node(5,'Concat',820,240,{'A':'INHERIT_UI value='}),node(6,'Print',820,60),node(10,'Function.Entry',40,500,{'Name':'Bonus'}),node(11,'Add',300,560,{'B':1}),node(12,'Function.Return',560,500,{'Name':'Bonus'})],
  'Links':[{'From':1,'Output':'Then','To':3,'Input':'In'},{'From':2,'Output':'Value','To':3,'Input':'X'},{'From':3,'Output':'Then','To':6,'Input':'In'},{'From':3,'Output':'Result','To':4,'Input':'Number'},
   {'From':4,'Output':'Value','To':5,'Input':'B'},{'From':5,'Output':'Value','To':6,'Input':'Text'},{'From':10,'Output':'Then','To':12,'Input':'In'},{'From':10,'Output':'X','To':11,'Input':'A'},{'From':11,'Output':'Value','To':12,'Input':'Result'}],
  'Variables':{'Level':{'Type':'Number','Default':2}},'Functions':{'Bonus':{'Inputs':[{'Name':'X','Type':'Number','Default':0}],'Outputs':[{'Name':'Result','Type':'Number','Default':0}],'Locals':{}}},
  'Enums':{},'Structs':{},'Macros':{}},indent=2))
 flow.write_text(json.dumps({'Format':'Zamin.VisualGraph','Version':4,'Nodes':[node(1,'Event.Create',40,60)],'Links':[],'Variables':{},'Functions':{},'Enums':{},'Structs':{},'Macros':{}},indent=2))
 readchild=lambda:json.loads(flow.read_text())
 shot=lambda name:subprocess.run(['import','-window','root',str(out/name)],check=True)
 def rightclick(key):px,py=find(key);x('mousemove',px,py);time.sleep(.6);x('click',3);time.sleep(1.2)
 def library(text):click(150,233);selectall();time.sleep(.3);x('type','--delay','150',text);time.sleep(1.2);click(155,283)
 log=(work/'editor-inherit.log').open('w')
 app=subprocess.Popen([str(build/'bin/ZaminEditor'),str(project/'VisualWorkshop.zproject')],env=dict(env,ZAMIN_EDITOR_VISUAL_GRAPH='Scripts/CurveChild.zgraph'),stdout=log,stderr=subprocess.STDOUT)
 try:
  for i in range(150):
   if app.poll() is not None:raise RuntimeError('Editor exited early')
   if 'Scene opened:' in (work/'editor-inherit.log').read_text():break
   time.sleep(.2)
  for i in range(60):
   if '#7D2F4C' in pixel(500,240) or '#7D7D2F2F4C4C' in pixel(500,240):break
   time.sleep(.4)
  else:raise RuntimeError('Inheritance graph window did not become ready')
  time.sleep(1)
  # The parent: set by path (one undo step); its variable shows as inherited and is not saved with the child.
  press('Inheritance');press('inheritance/Parent path');selectall();time.sleep(.3);x('type','--delay','120','Scripts/CurveBase.zgraph');time.sleep(.4)
  press('inheritance/Set parent');save()
  j=readchild();assert j['Version']==6 and j.get('Parent')=='Scripts/CurveBase.zgraph' and j['Variables']=={},j
  assert any(k.endswith('variables/Level/inherited') for k in items()['Items']),[k for k in items()['Items'] if 'Level' in k]
  click(531,140);save();assert 'Parent' not in readchild()
  click(585,140);save();assert readchild().get('Parent')=='Scripts/CurveBase.zgraph'
  unclipped();print('UI: parent set (undo/redo); the inherited variable is listed and not saved; saved as format 6',flush=True)
  # A Curve variable: two keys added, the second moved (Time) and raised (Value), the first made cubic, a third added and
  # deleted (undo/redo), then the second dragged in the preview (one undo step).
  press('variables/Variable name');x('type','--delay','150','Ramp');time.sleep(.4)
  press('variables/Type');press('popup/Curve');press('variables/Add variable');save()
  assert readchild()['Variables']['Ramp']=={'Type':'Curve','Default':{'Keys':[]}},readchild()['Variables']
  press('Ramp/Add key');press('Ramp/Add key');press('Ramp/keys')
  enter('Ramp/keys/1/Time','2');enter('Ramp/keys/1/Value','4')
  press('Ramp/keys/0/Mode');press('popup/Cubic');save()
  keys=lambda:readchild()['Variables']['Ramp']['Default']['Keys']
  assert [(k['Time'],k['Value'],k['Mode']) for k in keys()]==[(0,0,'Cubic'),(2,4,'Linear')],keys()
  press('Ramp/Add key');save();assert len(keys())==3 and keys()[2]['Time']==3,keys()
  press('Ramp/keys/2/Delete key');save();assert len(keys())==2,keys()
  click(531,140);save();assert len(keys())==3
  click(585,140);save();assert [(k['Time'],k['Value'],k['Mode']) for k in keys()]==[(0,0,'Cubic'),(2,4,'Linear')],keys()
  unclipped();shot('curve-editor.png')
  # Drag the last key (top right of the preview) halfway left: its time moves to about 1, its value stays.
  r=next(v['Rect'] for k,v in items()['Items'].items() if k.endswith('Ramp/Preview'));x0,y0,x1,y1=r
  px,py=int(x1-8),int(y0+8);tx=int(x0+8+(x1-x0-16)*.5)
  x('mousemove',px,py);time.sleep(.6);x('mousedown',1);time.sleep(.4)
  for i in range(1,11):x('mousemove',px+(tx-px)*i//10,py);time.sleep(.15)
  time.sleep(.4);x('mouseup',1);time.sleep(1.2);save()
  moved=keys()[1];assert .8<moved['Time']<1.2 and abs(moved['Value']-4)<.2,keys()
  click(531,140);save();assert keys()[1]['Time']==2 and keys()[1]['Value']==4,keys()
  print('UI: curve keys added, moved, made cubic, deleted and dragged in the preview; undo/redo restore them',flush=True)
  # The inherited function: listed read-only, then overridden (entry -> Parent.Call -> return, one undo step).
  press('Graph variables');press('Typed functions');press('functions/Bonus')
  assert any(k.endswith('functions/Bonus/inherited') for k in items()['Items'])
  unclipped();shot('inherit-members.png')
  press('Bonus/Override function');save()
  j=readchild();kinds={n['ID']:(n['Kind'],n['Inputs'].get('Name')) for n in j['Nodes']}
  assert 'Bonus' in j['Functions'] and sorted(kinds.values())==sorted([('Event.Create',None),('Function.Entry','Bonus'),('Parent.Call','Bonus'),('Function.Return','Bonus')]),(j['Functions'],kinds)
  entry,call,ret=(next(i for i,v in kinds.items() if v[0]==k) for k in ('Function.Entry','Parent.Call','Function.Return'))
  assert {(entry,'X',call,'X'),(call,'Result',ret,'Result')}<={(l['From'],l['Output'],l['To'],l['Input']) for l in j['Links']}
  click(531,140);save();assert 'Bonus' not in readchild()['Functions'] and len(readchild()['Nodes'])==1
  click(585,140);save();assert 'Bonus' in readchild()['Functions'] and len(readchild()['Nodes'])==4
  assert any(k.endswith('Bonus/override') for k in items()['Items'])
  unclipped();shot('inherit-override.png');print('UI: inherited function overridden with a call to the parent; undo/redo',flush=True)
  # The override adds Ramp(1) to the parent's result: Get.Curve + Curve.Evaluate from the node library, wired into an Add.
  library('get.curve');get=max(n['ID'] for n in readchild()['Nodes'])+1;drag(f'canvas/node/{get}',0,260);enter('node/Name','Ramp')
  library('curve.evaluate');ev=get+1;drag(f'canvas/node/{ev}',260,260);enter('node/Time','1')
  library('add');add=ev+1;drag(f'canvas/node/{add}',520,140)
  rightclick(f'canvas/{ret}/in/Result')
  for a,b in ((f'{get}/out/Value',f'{ev}/in/Curve'),(f'{call}/out/Result',f'{add}/in/A'),(f'{ev}/out/Value',f'{add}/in/B'),(f'{add}/out/Value',f'{ret}/in/Result')):press('canvas/'+a);press('canvas/'+b)
  save();j=readchild();wires={(l['From'],l['Output'],l['To'],l['Input']) for l in j['Links']}
  assert {(get,'Value',ev,'Curve'),(call,'Result',add,'A'),(ev,'Value',add,'B'),(add,'Value',ret,'Result')}<=wires and (call,'Result',ret,'Result') not in wires,wires
  kinds={n['ID']:n for n in j['Nodes']};assert kinds[get]['Kind']=='Get.Curve' and kinds[get]['Inputs']=={'Name':'Ramp'} and kinds[ev]['Kind']=='Curve.Evaluate' and kinds[ev]['Inputs']=={'Time':1} and kinds[add]['Kind']=='Add',kinds
  shot('inherit-curve.png');print('UI: the override evaluates the curve, wired from the node library',flush=True)
  # The inherited Start event: overridden with a Parent.Event that runs the parent's handler first.
  press('Typed functions');press('inheritance/Start/Override event');save()
  j=readchild();starts=[n for n in j['Nodes'] if n['Kind']=='Event.Start'];parents=[n for n in j['Nodes'] if n['Kind']=='Parent.Event']
  assert len(starts)==1 and len(parents)==1 and parents[0]['Inputs']=={'Name':'Start'} and {'From':starts[0]['ID'],'Output':'Then','To':parents[0]['ID'],'Input':'In'} in j['Links'],j['Nodes']
  unclipped();print('UI: inherited event overridden with Parent.Event',flush=True)
 except BaseException:
  shot('editor-inherit-failure.png');raise
 finally:
  app.terminate()
  try:app.wait(timeout=10)
  except subprocess.TimeoutExpired:app.kill();app.wait()
  log.close()
  shutil.copyfile(work/'editor-inherit.log',out/'editor-inherit.log')
 scene.write_text(scene.read_text().replace('Scripts/MacroFlow.zgraph','Scripts/CurveChild.zgraph'))
 run=subprocess.run([str(build/'bin/ZaminPlayer'),str(project/'VisualWorkshop.zproject')],env=dict(env,ZAMIN_MAX_FRAMES='30'),capture_output=True,text=True,timeout=180)
 (out/'player-inherit.log').write_text(run.stdout+run.stderr)
 # The parent's Start (run through the child's Parent.Event) calls Bonus: the child's override adds Ramp(1) = 2 to the
 # parent's Level + 1 = 3.
 assert run.returncode==0 and 'INHERIT_UI value=5.00' in run.stdout+run.stderr and "Visual graph '" not in run.stdout+run.stderr,run.returncode
 print('UI: the child graph runs in the Player through its parent (value=5.00)',flush=True)
def wait_editor(app,logname,ready):
 # Until the editor opened its scene and the layout file shows the ready key (a canvas node, a profiler value ...).
 for i in range(300):
  if app.poll() is not None:raise RuntimeError('Editor exited early')
  text=(work/logname).read_text()
  if 'Scene opened:' in text:
   try:
    data=json.loads(layout.read_text())
    if ready(data):return data
   except (OSError,ValueError):pass
  time.sleep(.4)
 raise RuntimeError('Editor did not become ready: '+logname)
def native_flow(project,env,build,out,scene):
 # Phase 6a: VisualWorkshop's C++ module (Source/WorkshopNative.cpp) registers WorkshopStats.Award/Score/MedalFor and
 # WorkshopMath.Hypot (ZM_METHOD / ZM_FUNCTION). The node library lists them; a node is added with the live signature
 # (undo/redo); a hot reload changes Score's signature: the nodes turn red (the graph still compiles and keeps its
 # wires), Refresh from C++ takes the new signature (undo/redo); the Player runs the graph with the new module.
 graph=project/'Assets/Scripts/NativeMethods.zgraph'
 readnative=lambda:json.loads(graph.read_text())
 shot=lambda name:subprocess.run(['import','-window','root',str(out/name)],check=True)
 log=(work/'editor-native.log').open('w')
 app=subprocess.Popen([str(build/'bin/ZaminEditor'),str(project/'VisualWorkshop.zproject')],env=dict(env,ZAMIN_EDITOR_VISUAL_GRAPH='Scripts/NativeMethods.zgraph'),stdout=log,stderr=subprocess.STDOUT)
 try:
  wait_editor(app,'editor-native.log',lambda d:'canvas/node/1' in d['Items'] and 'palette/WorkshopStats.Score' in d['Items'])
  assert 'Script module loaded: libVisualWorkshop.so' in (work/'editor-native.log').read_text()
  assert not any(k.startswith('canvas/broken/') for k in items()['Items'])
  before=readnative();count=len(before['Nodes'])
  press('palette/search');selectall();time.sleep(.3);x('type','--delay','150','Score');time.sleep(1.2)
  assert not any(k.startswith('palette/WorkshopStats.Award') for k in items()['Items'])
  press('palette/WorkshopStats.Score');save()
  j=readnative();added=max(n['ID'] for n in j['Nodes'])
  node=next(n for n in j['Nodes'] if n['ID']==added)
  assert len(j['Nodes'])==count+1 and node['Kind']=='Native.Pure' and node['Inputs']=={'Method':'WorkshopStats.Score'},node
  assert [p['Name'] for p in j['Natives']['WorkshopStats.Score']['Inputs']]==['Base','Multiplier'] and j['Version']==6
  assert f'canvas/{added}/in/Base' in items()['Items'] and f'canvas/{added}/in/Target' in items()['Items'] and any(k.endswith('native/bound') for k in items()['Items'])
  click(531,140);save();assert len(readnative()['Nodes'])==count
  click(585,140);save();assert len(readnative()['Nodes'])==count+1
  unclipped();shot('native-node-library.png');print('UI: a C++ method node from the node library (live signature); undo/redo',flush=True)
  # The hot reload: Score gets a third parameter; the module is rebuilt outside the editor (as an IDE does).
  source=project/'Source/WorkshopNative.cpp';text=source.read_text()
  text=text.replace('float Score(float base, int multiplier) const { return std::round(base * float(multiplier) * Bonus); }','float Score(float base, int multiplier, float extra) const { return std::round(base * float(multiplier) * Bonus) + extra; }')
  text=text.replace('ZM_PURE_METHOD(WorkshopStats, Score, "Base", "Multiplier")','ZM_PURE_METHOD(WorkshopStats, Score, "Base", "Multiplier", "Extra")')
  assert 'float extra' in text and '"Extra")' in text;source.write_text(text)
  configure=['cmake','-S',str(project),'-B',str(project/'Build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DZamin_DIR='+str(build)]
  if (build/'ZaminModuleCompiler.cmake').is_file():configure+=['-C',str(build/'ZaminModuleCompiler.cmake')]
  with (out/'native-rebuild.log').open('w') as rebuild:
   subprocess.run(configure,check=True,stdout=rebuild,stderr=subprocess.STDOUT,timeout=900)
   subprocess.run(['cmake','--build',str(project/'Build')],check=True,stdout=rebuild,stderr=subprocess.STDOUT,timeout=900)
  for i in range(300):
   if 'Scripts reloaded (hot reload)' in (work/'editor-native.log').read_text() and f'canvas/broken/{added}' in items()['Items']:break
   time.sleep(.5)
  else:raise RuntimeError('The hot reload did not mark the changed method')
  data=items();assert 'canvas/broken/5' in data['Items'] and 'status/native problems' in data['Items'] and data['Values'].get('native/problems')==1,data['Values']
  assert not any(k.startswith('canvas/broken/') and k not in (f'canvas/broken/{added}','canvas/broken/5') for k in data['Items'])
  assert readnative()==j;shot('native-broken-after-hot-reload.png')
  print('UI: after the hot reload the changed method\'s nodes are marked; the graph keeps loading and its wires',flush=True)
  press(f'canvas/node/5');assert any(k.endswith('native/problem') for k in items()['Items'])
  press('native/Refresh from C++');save()
  j=readnative();wires={(l['From'],l['Output'],l['To'],l['Input']) for l in j['Links']}
  assert [p['Name'] for p in j['Natives']['WorkshopStats.Score']['Inputs']]==['Base','Multiplier','Extra'],j['Natives']
  assert {(2,'Value',5,'Target'),(4,'Value',5,'Base'),(5,'Value',9,'Value'),(5,'Value',7,'Score')}<=wires,wires
  for i in range(20):
   if not any(k.startswith('canvas/broken/') for k in items()['Items']):break
   time.sleep(.3)
  else:raise RuntimeError('Nodes still marked after the refresh')
  assert 'canvas/5/in/Extra' in items()['Items']
  click(531,140);save();assert [p['Name'] for p in readnative()['Natives']['WorkshopStats.Score']['Inputs']]==['Base','Multiplier']
  click(585,140);save();assert [p['Name'] for p in readnative()['Natives']['WorkshopStats.Score']['Inputs']]==['Base','Multiplier','Extra']
  unclipped();shot('native-refreshed.png');print('UI: Refresh from C++ takes the new signature, keeps the wires; undo/redo',flush=True)
 except BaseException:
  shot('editor-native-failure.png');raise
 finally:
  app.terminate()
  try:app.wait(timeout=10)
  except subprocess.TimeoutExpired:app.kill();app.wait()
  log.close()
  shutil.copyfile(work/'editor-native.log',out/'editor-native.log')
 run=subprocess.run([str(build/'bin/ZaminPlayer'),str(project/'VisualWorkshop.zproject')],env=dict(env,ZAMIN_MAX_FRAMES='30'),capture_output=True,text=True,timeout=180)
 (out/'player-native.log').write_text(run.stdout+run.stderr)
 assert run.returncode==0 and 'BLUEPRINT_NATIVE score=36 medal=Silver hypot=5.00 awards=1' in run.stdout+run.stderr and "Visual graph '" not in run.stdout+run.stderr,run.returncode
 print('UI: the Player runs the refreshed graph with the rebuilt module (BLUEPRINT_NATIVE score=36 medal=Silver)',flush=True)
def profile_flow(project,env,build,out,scene):
 # Phase 6b: a large graph (about 3000 nodes: an Update handler whose 30-iteration loop is the hot spot, a chain of 300
 # Set nodes, pure filler far to the right) on an entity of the 2D scene, played in the editor: the canvas draws only
 # the visible nodes, the heat overlay marks the loop's nodes hottest, the Visual Script Profiler lists the nodes and
 # the hot path, and a profiler row selects its node in the graph.
 large=project/'Assets/Scripts/LargeGraph.zgraph'
 nodes,links=[],[]
 def add(kind,x,y,inputs=None):
  nodes.append(node(len(nodes)+1,kind,x,y,inputs));return len(nodes)
 def wire(a,o,b,i):links.append({'From':a,'Output':o,'To':b,'Input':i})
 update=add('Event.Update',40,60);loop=add('For',300,60,{'Count':30});hot=add('Set.Number',600,60,{'Name':'Accum'})
 get=add('Get.Number',300,300,{'Name':'Accum'});sin=add('Sin',600,300);plus=add('Add',900,300)
 wire(update,'Then',loop,'In');wire(loop,'Body',hot,'In');wire(loop,'Index',sin,'A');wire(get,'Value',plus,'A');wire(sin,'Value',plus,'B');wire(plus,'Value',hot,'Value')
 last,out_pin=loop,'Completed'
 for i in range(300):
  s=add('Set.Number',1200+(i%10)*260,60+(i//10)*220,{'Name':'X'});g=add('Get.Number',1200+(i%10)*260,160+(i//10)*220,{'Name':'X'});a=add('Add',1300+(i%10)*260,160+(i//10)*220,{'B':1})
  wire(last,out_pin,s,'In');wire(g,'Value',a,'A');wire(a,'Value',s,'Value');last,out_pin=s,'Then'
 while len(nodes)+2<=3000:
  i=len(nodes);g=add('Get.Number',4000+(i%60)*260,60+(i//60)*220,{'Name':'X'});m=add('Multiply',4130+(i%60)*260,60+(i//60)*220,{'B':2});wire(g,'Value',m,'A')
 large.write_text(json.dumps({'Format':'Zamin.VisualGraph','Version':6,'Nodes':nodes,'Links':links,'Variables':{'Accum':{'Type':'Number','Default':0},'X':{'Type':'Number','Default':0}}},indent=1))
 data=json.loads(scene.read_text())
 controller=json.loads(json.dumps(next(e for e in data['Entities'] if e['Tag']=='Native methods controller')))
 controller.update({'Entity':930,'Tag':'Large graph controller'});controller['NativeScript']['Fields']={'Graph':{'Type':'String','Value':'Scripts/LargeGraph.zgraph'}}
 data['Entities'].append(controller);scene.write_text(json.dumps(data,indent=2)+'\n')
 shot=lambda name:subprocess.run(['import','-window','root',str(out/name)],check=True)
 log=(work/'editor-profile.log').open('w')
 app=subprocess.Popen([str(build/'bin/ZaminEditor'),str(project/'VisualWorkshop.zproject')],env=dict(env,ZAMIN_EDITOR_VISUAL_GRAPH='Scripts/LargeGraph.zgraph',ZAMIN_EDITOR_PLAY='1',ZAMIN_EDITOR_VISUAL_PROFILER='1',ZAMIN_EDITOR_SELECT='Large graph controller'),stdout=log,stderr=subprocess.STDOUT)
 try:
  data=wait_editor(app,'editor-profile.log',lambda d:d['Values'].get('profiler/nodes',0)>=5 and f'heat/{hot}' in d['Values'])
  time.sleep(3);v=items()['Values']
  # Culling: thousands of nodes, only the visible ones drawn.
  assert v['canvas/nodes']==len(nodes) and 0<v['canvas/drawnNodes']<v['canvas/nodes']/4,(v['canvas/nodes'],v['canvas/drawnNodes'])
  heats={int(k[5:]):h for k,h in v.items() if k.startswith('heat/')}
  hottest=max(heats,key=heats.get)
  assert heats[hottest]==1 and hottest in (loop,hot,sin,plus,get),heats
  assert v['profiler/instances']==1 and v['profiler/hotPath']>=3 and v['profiler/nodes']>=300,v
  items_=items()['Items'];assert 'profiler/hot/0' in items_ and f'profiler/node/{hottest}' in items_
  print(f'UI: large graph ({len(nodes)} nodes) playing: {v["canvas/drawnNodes"]:.0f} nodes drawn, canvas {v["canvas/ms"]:.1f} ms; hottest node #{hottest} heat 1.0; profiler: {v["profiler/nodes"]:.0f} nodes, hot path {v["profiler/hotPath"]:.0f}',flush=True)
  shot('heat-overlay-large-graph.png')
  press(f'profiler/node/{hot}')
  for i in range(20):
   if items()['Values'].get('graph/selected')==hot:break
   time.sleep(.3)
  else:raise RuntimeError('A profiler row did not select its node')
  shot('visual-script-profiler.png');print('UI: a profiler row selects its node in the graph',flush=True)
  assert "Visual graph '" not in (work/'editor-profile.log').read_text()
 except BaseException:
  shot('editor-profile-failure.png');raise
 finally:
  app.terminate()
  try:app.wait(timeout=10)
  except subprocess.TimeoutExpired:app.kill();app.wait()
  log.close()
  shutil.copyfile(work/'editor-profile.log',out/'editor-profile.log')
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
 inherit_flow(project,env,build,out,scene)
 native_flow(project,env,build,out,scene)
 profile_flow(project,env,build,out,scene)
finally:
 temporary.cleanup()
