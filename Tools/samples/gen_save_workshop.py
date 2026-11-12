#!/usr/bin/env python3
"""Generate compiler-free stable-slot gameplay save/resume courses from engine-owned AI geometry."""
import copy
import json
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Projects/SaveWorkshop';ASSETS=OUT/'Assets'
for folder in ('Scripts','Scenes','Prefabs'):(ASSETS/folder).mkdir(parents=True,exist_ok=True)
def write(path,data):path.write_text(json.dumps(data,indent=2)+'\n')
def meta(path,i,kind):write(Path(str(path)+'.zmeta'),{'UUID':f'b450ba937ef1{i:04x}','Type':kind,'Import':{}})
def script(name):return {'Class':'Zamin.VisualGraph','Fields':{'Graph':{'Type':'String','Value':'Scripts/'+name}}}
class Graph:
 def __init__(self):self.data={'Format':'Zamin.VisualGraph','Version':1,'Nodes':[],'Links':[],'Variables':{}}
 def n(self,kind,**inputs):
  i=len(self.data['Nodes'])+1;self.data['Nodes'].append({'ID':i,'Kind':kind,'X':(i-1)%7*290,'Y':(i-1)//7*340,'Inputs':inputs});return i
 def l(self,a,out,b,pin):self.data['Links'].append({'From':a,'Output':out,'To':b,'Input':pin})
 def chain(self,*nodes):
  for a,b in zip(nodes,nodes[1:]):self.l(a,'Then',b,'In')
 def save(self,name,i):p=ASSETS/'Scripts'/name;write(p,self.data);meta(p,i,'VisualGraph')
 def check(self,source,out,text):
  b=self.n('Branch');self.chain(source,b);self.l(source,out,b,'Condition');bad=self.n('Print',Text=text);self.l(b,'False',bad,'In');return b
for two in (True,False):
 suffix='2D' if two else '3D'
 base=json.loads((ROOT/f'Projects/AIWorkshop/Assets/Scenes/AI{suffix}.zscene').read_text())
 base['Scene']='Save '+suffix
 base['Entities']=[e for e in base['Entities'] if e['Entity'] not in (880,881,901,902)]
 for e in base['Entities']:
  if e['Entity']==800:e['NativeScript']=script('Player'+suffix+'.zgraph')
  if e['Entity']==900:e['NativeScript']=script('Director'+suffix+'.zgraph');e['Tag']='Save director'
  if e['Entity']==1001:e['Text']['Text']='SAVE LAB | WASD move | E progress | Ctrl save | Space load'
  if e['Entity']==1002:e['Text']['Text']='Stable prefab slot: Guard | Versioned snapshot + validated backup'
  if e['Entity']==1003:e['Text']['Text']='HP 1000 | Coins 0 | Guard HP 60'
  if e['Entity']==1004:e['Text']['Text']='Press E, move, save. Restart the Player, press Space to resume.'
 door={'Entity':903,'Tag':'Progress door','Transform':{'Translation':[6,0,0] if two else [6,1,0],'Rotation':[0,0,0],'Scale':[.6,2,1] if two else [.6,2,.6]}}
 door['SpriteRenderer' if two else 'MeshRenderer']={'Color':[.9,.65,.2,1]} if two else {'Mesh':'Cube','Shader':'Unlit','Albedo':[.9,.65,.2,1],'CastShadows':False}
 base['Entities'].append(door)
 p=ASSETS/f'Scenes/Save{suffix}.zscene';write(p,base);meta(p,7 if two else 8,'Scene')
 prefab=json.loads((ROOT/f'Projects/AIWorkshop/Assets/Prefabs/Reinforcement{suffix}.zprefab').read_text());prefab['Entities'][0]['NativeScript']=script('Brain'+suffix+'.zgraph');prefab['Entities'][0]['Tag']='Persistent guard'
 p=ASSETS/f'Prefabs/Guard{suffix}.zprefab';write(p,prefab);meta(p,9 if two else 10,'Prefab')
 brain=json.loads((ROOT/f'Projects/AIWorkshop/Assets/Scripts/Brain{suffix}.zgraph').read_text())
 for node in brain['Nodes']:
  if node['Kind']=='Gameplay.AIConfigure':node['Inputs']['Tag']='Nobody'
 p=ASSETS/f'Scripts/Brain{suffix}.zgraph';write(p,brain);meta(p,3 if two else 4,'VisualGraph')
 g=Graph();n=g.n;l=g.l;chain=g.chain;g.data['Variables']={'Auto':{'Type':'Bool','Default':False},'Move':{'Type':'Vec2','Default':[0,0]}}
 start=n('Event.Start');hp=n('Gameplay.HealthConfigure',Maximum=1000,HitGrace=0,Team=1);bag=n('Gameplay.InventoryConfigure',Slots=8,Stack=99);bind=n('Gameplay.SaveBind',Key='Player');chain(start,hp,bag,bind)
 update=n('Event.Update');raw=n('Gameplay.Input');auto=n('Get.Bool',Name='Auto');branch=n('Branch');chain(update,branch);l(auto,'Value',branch,'Condition')
 manual=n('Set.Vec2',Name='Move');l(branch,'False',manual,'In');l(raw,'Move',manual,'Value');value=n('ReadState.Number',Key='Save.Move');vec=n('MakeVec2');l(value,'Value',vec,'X');automatic=n('Set.Vec2',Name='Move');l(branch,'True',automatic,'In');l(vec,'Value',automatic,'Value')
 move=n('Get.Vec2',Name='Move');motor=n('Gameplay.Motor2D' if two else 'Gameplay.Motor3D',**({'TopDown':True} if two else {}),Speed=4);chain(manual,motor);chain(automatic,motor);l(move,'Value',motor,'Move');l(update,'Delta',motor,'Delta');l(raw,'Enabled',motor,'Enabled')
 for action,message in [('Interact','Progress'),('Fire','Save'),('Jump','Load')]:
  event=n('Event.Update');pressed=n('ActionJustPressed',Name=action);b=n('Branch');chain(event,b);l(pressed,'Value',b,'Condition');send=n('SendMessage',Target=900,Name=message);l(b,'True',send,'In')
 late=n('Event.LateUpdate');hp=n('Gameplay.Health');count=n('Gameplay.InventoryCount',Item='Coin');hpt=n('NumberToText');ct=n('NumberToText');l(hp,'Current',hpt,'Number');l(count,'Count',ct,'Number');a=n('Concat',A='HP ');l(hpt,'Value',a,'B');b=n('Concat',B=' | Coins ');l(a,'Value',b,'A');c=n('Concat');l(b,'Value',c,'A');l(ct,'Value',c,'B');text=n('SetUIText',Target=1003);chain(late,text);l(c,'Value',text,'Text');g.save('Player'+suffix+'.zgraph',1 if two else 2)
 g=Graph();n=g.n;l=g.l;chain=g.chain
 g.data['Variables']={'Auto':{'Type':'Bool','Default':False},'Checks':{'Type':'Bool','Default':False},'Resume':{'Type':'Bool','Default':False},'Wrote':{'Type':'Bool','Default':False},'Loaded':{'Type':'Bool','Default':False},'Guard':{'Type':'Entity','Default':0}}
 start=n('Event.Start');spawn=n('Gameplay.SaveSpawn',Key='Guard',Prefab=f'Prefabs/Guard{suffix}.zprefab',Position=[-5,0,0]);stored=n('Set.Entity',Name='Guard');l(spawn,'Entity',stored,'Value');door=n('Gameplay.InteractionConfigure',Target=903,RequiredItem='Key',Consume=True,RewardItem='Coin',Deactivate=True);bind=n('Gameplay.SaveBind',Target=903,Key='Door');chain(start,spawn,stored,door,bind)
 guard=n('Get.Entity',Name='Guard');npc=n('Gameplay.Health');l(guard,'Value',npc,'Target');ai=n('Gameplay.AIState');l(guard,'Value',ai,'Target')
 # A completed interaction is restored as used, so loading cannot award the coin again.
 event=n('Event.Custom',Name='Progress');key=n('Gameplay.InventoryAdd',Target=800,Item='Key',Count=2);coin=n('Gameplay.InventoryAdd',Target=800,Item='Coin',Count=7);use=n('Gameplay.Interact',Target=903,Actor=800,Range=20,TwoD=two,LineOfSight=False,Origin=[0,0,0]);hurt=n('Gameplay.Damage',Target=800,Amount=125,IgnoreGrace=True);hurtguard=n('Gameplay.Damage',Amount=20,IgnoreGrace=True);l(guard,'Value',hurtguard,'Target');label=n('SetUIText',Target=1004,Text='Progress applied: HP 875 | Coins 8 | Guard HP 40 | Door consumed');chain(event,key,coin,use,hurt,hurtguard,label)
 for name,kind,variable in [('Save','Gameplay.SaveWrite','Wrote'),('Load','Gameplay.SaveLoad','Loaded')]:
  event=n('Event.Custom',Name=name);op=n(kind,Slot='Quick',Schema='SaveLabV1');chain(event,op);b=n('Branch');chain(op,b);l(op,'Success',b,'Condition');mark=n('Set.Bool',Name=variable,Value=True);l(b,'True',mark,'In');label=n('SetUIText',Target=1004,Text='Saved to Quick slot' if name=='Save' else 'RESUMED: health / inventory / interaction / AI / pose / prefab');chain(mark,label);
  if name=='Save':
   flag=n('WriteState.Bool',Key='Save.AutoResume',Value=True,Persistent=True);flush=n('SaveGameState');chain(label,flag,flush)
  bad=n('SetUIText',Target=1004);l(b,'False',bad,'In');l(op,'Reason',bad,'Text')
 for message in ('Progress','Save','Load'):
  event=n('Event.Message');equal=n('TextEqual',B=message);l(event,'Text',equal,'A');b=n('Branch');chain(event,b);l(equal,'Value',b,'Condition');emit=n('EmitEvent',Name=message);l(b,'True',emit,'In')
 # Auto mode resumes only after a prior automatic write; manual startup remains explicit.
 start=n('Event.Start');loadstate=n('LoadGameState');auto=n('Get.Bool',Name='Auto');persist=n('ReadState.Bool',Key='Save.AutoResume');both=n('And');l(auto,'Value',both,'A');l(persist,'Value',both,'B');b=n('Branch');chain(start,loadstate,b);l(both,'Value',b,'Condition');mark=n('Set.Bool',Name='Resume',Value=True);l(b,'True',mark,'In')

 start=n('Event.Start');ready=n('Delay',Seconds=.3);resume=n('Get.Bool',Name='Resume');b=n('Branch');chain(start,ready,b);l(resume,'Value',b,'Condition');emit=n('EmitEvent',Name='Load');l(b,'True',emit,'In')
 auto=n('Get.Bool',Name='Auto');start=n('Event.Start');ready=n('Delay',Seconds=.35);b=n('Branch');chain(start,ready,b);l(auto,'Value',b,'Condition');notresume=n('Not');l(resume,'Value',notresume,'A');b2=n('Branch');l(b,'True',b2,'In');l(notresume,'Value',b2,'Condition');last=n('Delay',Seconds=1);l(b2,'True',last,'In')
 for action,delay in [('Progress',5/30),('Move',20/30),('Stop',45/30),('Save',.1)]:
  node=n('WriteState.Number',Key='Save.Move',Value=1 if action=='Move' else 0,Persistent=False) if action in ('Move','Stop') else n('EmitEvent',Name=action)
  wait=n('Delay',Seconds=delay);chain(last,node,wait);last=wait
 # Observe native state after restoration; no acceptance flag stands in for the bag, HP or AI.
 start=n('Event.Start');checks=n('Get.Bool',Name='Checks');b=n('Branch');chain(start,b);l(checks,'Value',b,'Condition');wait=n('Delay',Seconds=4.3);l(b,'True',wait,'In')
 terms=[]
 for source,output,wanted in [(n('Gameplay.Health',Target=800),'Current',875),(n('Gameplay.InventoryCount',Target=800,Item='Coin'),'Count',8),(npc,'Current',40)]:
  equal=n('Equal',B=wanted);l(source,output,equal,'A');terms.append(equal)
 active=n('Gameplay.SaveFind',Key='Door');inactive=n('Not');l(active,'Active',inactive,'A');terms.append(inactive);arrived=n('Greater',B=0);l(ai,'Arrivals',arrived,'A');terms.append(arrived)
 pose=n('GetPosition',Target=800);x=n('BreakVec3');l(pose,'Value',x,'Vector');moved=n('Greater',B=1);l(x,'X',moved,'A');terms.append(moved)
 mode=n('Branch');chain(wait,mode);l(resume,'Value',mode,'Condition')
 for resumed,pin,variable in [(True,'True','Loaded'),(False,'False','Wrote')]:
  terms2=terms+[n('Get.Bool',Name=variable)];condition=terms2[0]
  for term in terms2[1:]:
   both=n('And');l(condition,'Value',both,'A');l(term,'Value',both,'B');condition=both
  passed=n('Branch');l(mode,pin,passed,'In');l(condition,'Value',passed,'Condition');ok=n('Print',Text='SAVE_RESUMED health=1 bag=1 interaction=1 ai=1 pose=1 prefab=1' if resumed else 'SAVE_ACCEPTANCE wrote=1 hp=875 coin=8 npc=40 door=0');bad=n('Print',Text='SAVE_ORACLE_FAILED');l(passed,'True',ok,'In');l(passed,'False',bad,'In')
 g.save('Director'+suffix+'.zgraph',5 if two else 6)
manifest=json.loads((ROOT/'Projects/AIWorkshop/AIWorkshop.zproject').read_text());manifest['Project'].update(Name='SaveWorkshop',StartScene='Scenes/Save2D.zscene',Description='Compiler-free stable-key gameplay snapshots: health, bags, used doors, AI and prefab slots.');manifest['Player']['Product']='Zamin Save Workshop';write(OUT/'SaveWorkshop.zproject',manifest)
if not (OUT/'Thumbnail.png').exists():shutil.copyfile(ROOT/'Projects/AIWorkshop/Thumbnail.png',OUT/'Thumbnail.png')
