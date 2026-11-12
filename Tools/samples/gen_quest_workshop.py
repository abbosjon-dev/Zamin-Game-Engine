#!/usr/bin/env python3
"""Compiler-free 2D/3D quest course: real collectibles, combat, beacon, exit and saved reward progression."""
import json,copy,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Projects/QuestWorkshop';ASSETS=OUT/'Assets'
for folder in ('Scripts','Scenes','Prefabs'):(ASSETS/folder).mkdir(parents=True,exist_ok=True)
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def meta(p,i,kind):write(Path(str(p)+'.zmeta'),{'UUID':f'd650ba937ef1{i:04x}','Type':kind,'Import':{}})
def script(name):return {'Class':'Zamin.VisualGraph','Fields':{'Graph':{'Type':'String','Value':'Scripts/'+name}}}
class Graph:
 def __init__(self):self.data={'Format':'Zamin.VisualGraph','Version':1,'Nodes':[],'Links':[],'Variables':{}}
 def n(self,kind,**inputs):
  i=len(self.data['Nodes'])+1;self.data['Nodes'].append({'ID':i,'Kind':kind,'X':(i-1)%8*290,'Y':(i-1)//8*320,'Inputs':inputs});return i
 def l(self,a,out,b,pin):self.data['Links'].append({'From':a,'Output':out,'To':b,'Input':pin})
 def chain(self,*nodes):
  for a,b in zip(nodes,nodes[1:]):self.l(a,'Then',b,'In')
 def save(self,name,i):p=ASSETS/'Scripts'/name;write(p,self.data);meta(p,i,'VisualGraph')
 def read(self,key):return self.n('ReadState.Bool',Key='Quest.'+key)
 def mark(self,key):return self.n('WriteState.Bool',Key='Quest.'+key,Value=True,Persistent=False)
def transform(p,s=(1,1,1)):return {'Translation':list(p),'Rotation':[0,0,0],'Scale':list(s)}
def visual(i,name,pos,scale,color,two):
 e={'Entity':i,'Tag':name,'Transform':transform(pos,scale)};e['SpriteRenderer' if two else 'MeshRenderer']={'Color':color} if two else {'Mesh':'Cube','Shader':'Unlit','Albedo':color,'CastShadows':False};return e
def text(i,value,y):return {'Entity':i,'Parent':1200,'Tag':'Quest HUD','Transform':transform([0,0,0]),'UITransform':{'AnchorMin':[0,1],'AnchorMax':[0,1],'Pivot':[0,1],'Position':[28,y],'Size':[1170,34],'FontSize':19},'Text':{'Text':value,'Color':[.86,.94,1,1],'AlignH':'Left','AlignV':'Middle','Overflow':'Ellipsis'}}
for two in (True,False):
 suffix='2D' if two else '3D';base=json.loads((ROOT/f'Projects/SaveWorkshop/Assets/Scenes/Save{suffix}.zscene').read_text());base['Scene']='Quest '+suffix;base['Entities']=[e for e in base['Entities'] if e['Entity'] not in (903,960,961,1000,1001,1002,1003,1004)]
 for e in base['Entities']:
  if e['Entity']==800:e['NativeScript']=script('Player'+suffix+'.zgraph')
  if e['Entity']==900:e['NativeScript']=script('Director'+suffix+'.zgraph');e['Tag']='Quest director'
 for i,x in [(601,2),(602,4),(603,6)]:base['Entities'].append(visual(i,'Rune '+str(i),[x,0,.1] if two else [x,.4,0],[.4,.4,1] if two else [.4,.8,.4],[.55,.35,1,1],two))
 beacon=visual(604,'Beacon',[8.5,0,.1] if two else [8.5,.5,0],[.6,.8,1] if two else [.6,1,.6],[.1,.8,.75,1],two);beacon['Tags']=['Beacon'];base['Entities'].append(beacon);base['Entities'].append(visual(605,'Exit',[9.7,0,-.02] if two else [9.7,.03,0],[.25,4,1] if two else [.25,.06,4],[.95,.65,.2,1],two))
 base['Entities'].append({'Entity':1200,'Tag':'Quest HUD','Transform':transform([0,0,0]),'UICanvas':{'ScaleMode':'ScaleWithScreenSize','ReferenceResolution':[1280,720]}})
 for i,y,label in [(1201,-22,'QUEST LAB | WASD move | E use | Ctrl attack | R claim | F5 save | F9 load'),(1202,-65,'Survey'),(1203,-103,'Recover three runes'),(1204,-137,'Defeat sentry'),(1205,-171,'Activate beacon'),(1206,-205,'Reach exit'),(1207,-251,'Return home'),(1209,-292,'Level 1 | XP 0 | Coins 0 | Badge 0'),(1210,-329,'Collect > fight > beacon > exit > claim > save > restart > return > claim')]:base['Entities'].append(text(i,label,y))
 bar={'Entity':1208,'Parent':1200,'Tag':'Quest progress','Transform':transform([0,0,0]),'UITransform':{'AnchorMin':[0,1],'AnchorMax':[0,1],'Pivot':[0,1],'Position':[28,-238],'Size':[520,7]},'UIProgressBar':{'Value':0,'Padding':0,'BackgroundColor':[.12,.16,.23,1],'FillColor':[.15,.8,.65,1]}};base['Entities'].append(bar)
 p=ASSETS/f'Scenes/Quest{suffix}.zscene';write(p,base);meta(p,7 if two else 8,'Scene')
 brain=json.loads((ROOT/f'Projects/SaveWorkshop/Assets/Scripts/Brain{suffix}.zgraph').read_text());g=Graph();g.data=brain;event=g.n('Event.Update');hp=g.n('Gameplay.Health');b=g.n('Branch');g.chain(event,b);g.l(hp,'Dead',b,'Condition');off=g.n('SetActive',Active=False);g.l(b,'True',off,'In');g.save('Brain'+suffix+'.zgraph',3 if two else 4)
 prefab=json.loads((ROOT/f'Projects/SaveWorkshop/Assets/Prefabs/Guard{suffix}.zprefab').read_text());prefab['Entities'][0]['NativeScript']=script('Brain'+suffix+'.zgraph');p=ASSETS/f'Prefabs/Guard{suffix}.zprefab';write(p,prefab);meta(p,9 if two else 10,'Prefab')
 g=Graph();n=g.n;l=g.l;chain=g.chain;g.data['Variables']={'Auto':{'Type':'Bool','Default':False},'Move':{'Type':'Vec2','Default':[0,0]}}
 start=n('Event.Start');hp=n('Gameplay.HealthConfigure',Maximum=100,HitGrace=0,Team=1);bag=n('Gameplay.InventoryConfigure',Slots=12,Stack=99);bind=n('Gameplay.SaveBind',Key='Player');quest=n('Gameplay.QuestConfigure',Quest='Survey',Title='FIELD SURVEY',Objectives=['Runes','Sentry','Beacon','Exit'],Labels=['Recover runes','Defeat sentry','Activate beacon','Reach exit'],Kinds=[1,2,3,0],Matches=['Rune','Enemy','Beacon',''],Goals=[3,1,1,1],Rewards=['Coin'],RewardCounts=[5],XP=150);second=n('Gameplay.QuestConfigure',Quest='Home',Title='RETURN HOME',Objectives=['Home'],Labels=['Return to starting area'],Kinds=[0],Matches=[''],Goals=[1],Requires=['Survey'],Rewards=['Badge'],RewardCounts=[1],XP=50);begin=n('Gameplay.QuestStart',Quest='Survey');chain(start,hp,bag,bind,quest,second,begin);last=begin
 for i in (601,602,603,604):
  args={'RewardItem':'Rune','Deactivate':True} if i!=604 else {'Prompt':'Activate beacon'};use=n('Gameplay.InteractionConfigure',Target=i,**args);save=n('Gameplay.SaveBind',Target=i,Key='Rune'+str(i) if i!=604 else 'Beacon');chain(last,use,save);last=save
 for widget,key,objective in [(1202,'Survey',''),(1203,'Survey','Runes'),(1204,'Survey','Sentry'),(1205,'Survey','Beacon'),(1206,'Survey','Exit'),(1207,'Home',''),(1208,'Survey','')]:
  node=n('Gameplay.QuestBindHUD',Target=widget,Actor=800,Quest=key,Objective=objective);chain(last,node);last=node
 update=n('Event.Update');raw=n('Gameplay.Input');auto=n('Get.Bool',Name='Auto');b=n('Branch');chain(update,b);l(auto,'Value',b,'Condition');normal=n('Set.Vec2',Name='Move');l(b,'False',normal,'In');l(raw,'Move',normal,'Value');value=n('ReadState.Number',Key='Quest.Move');vec=n('MakeVec2');l(value,'Value',vec,'X');automatic=n('Set.Vec2',Name='Move');l(b,'True',automatic,'In');l(vec,'Value',automatic,'Value');move=n('Get.Vec2',Name='Move');motor=n('Gameplay.Motor2D' if two else 'Gameplay.Motor3D',**({'TopDown':True} if two else {}),Speed=4);chain(normal,motor);chain(automatic,motor);l(move,'Value',motor,'Move');l(raw,'Enabled',motor,'Enabled');l(update,'Delta',motor,'Delta')
 for action,eventname in [('Interact','Use'),('Fire','Attack'),('Claim','Claim'),('QuickSave','Save'),('QuickLoad','Load')]:
  event=n('Event.Update');pressed=n('ActionJustPressed',Name=action);gate=n('And');l(pressed,'Value',gate,'A');l(raw,'Enabled',gate,'B');b=n('Branch');chain(event,b);l(gate,'Value',b,'Condition');emit=n('EmitEvent',Name=eventname);l(b,'True',emit,'In')
  event=n('Event.Message');equal=n('TextEqual',B=eventname);l(event,'Text',equal,'A');b=n('Branch');chain(event,b);l(equal,'Value',b,'Condition');emit=n('EmitEvent',Name=eventname);l(b,'True',emit,'In')
 event=n('Event.Custom',Name='Use');find=n('Gameplay.InteractionFind',Range=1.6,TwoD=two,LineOfSight=False,Origin=[0,0,0]);b=n('Branch');chain(event,b);l(find,'Found',b,'Condition');use=n('Gameplay.Interact',Actor=800,Range=1.6,TwoD=two,LineOfSight=False,Origin=[0,0,0]);l(b,'True',use,'In');l(find,'Object',use,'Target')
 event=n('Event.Custom',Name='Attack');guard=n('Gameplay.SaveFind',Key='Guard');strike=n('Gameplay.Strike',Amount=1000,Range=2.5,TwoD=two,LineOfSight=False,Origin=[0,0,0]);chain(event,strike);l(guard,'Entity',strike,'Victim')
 event=n('Event.Custom',Name='Claim');first=n('Gameplay.QuestClaim',Quest='Survey');second=n('Gameplay.QuestClaim',Quest='Home');chain(event,first,second)
 for name,kind,flag in [('Save','Gameplay.SaveWrite','Saved'),('Load','Gameplay.SaveLoad','Loaded')]:
  event=n('Event.Custom',Name=name);op=n(kind,Slot='Quest',Schema='QuestLabV1');chain(event,op);b=n('Branch');chain(op,b);l(op,'Success',b,'Condition');mark=g.mark(flag);l(b,'True',mark,'In')
  if name=='Save':
   state=n('Gameplay.QuestState',Quest='Home');choose=n('Branch');chain(mark,choose);l(state,'Claimed',choose,'Condition')
   for pin,value in [('True',2),('False',1)]:
    phaseflag=n('WriteState.Number',Key='Quest.Phase',Value=value,Persistent=True);flush=n('SaveGameState');l(choose,pin,phaseflag,'In');chain(phaseflag,flush)
  bad=n('Print',Text='QUEST_IO_FAILED');l(b,'False',bad,'In')
 position=n('GetPosition');x=n('BreakVec3');l(position,'Value',x,'Vector');survey=n('Gameplay.QuestState',Quest='Survey');home=n('Gameplay.QuestState',Quest='Home')
 event=n('Event.Update');ready=n('And');l(survey,'Claimed',ready,'A');locked=n('TextEqual',B='Locked');l(home,'Mode',locked,'A');l(locked,'Value',ready,'B');b=n('Branch');chain(event,b);l(ready,'Value',b,'Condition');begin=n('Gameplay.QuestStart',Quest='Home');l(b,'True',begin,'In')
 for key,objective,compare,threshold,receipt,st in [('Survey','Exit','Greater',9.2,'Exit.Zone',survey),('Home','Home','Less',.8,'Return.Home',home)]:
  event=n('Event.Update');inside=n(compare,B=threshold);l(x,'X',inside,'A');active=n('TextEqual',B='Active');l(st,'Mode',active,'A');goal=n('Gameplay.QuestObjective',Quest=key,Objective=objective);notdone=n('Not');l(goal,'Complete',notdone,'A');a=n('And');l(inside,'Value',a,'A');l(active,'Value',a,'B');condition=n('And');l(a,'Value',condition,'A');l(notdone,'Value',condition,'B');b=n('Branch');chain(event,b);l(condition,'Value',b,'Condition');advance=n('Gameplay.QuestAdvance',Quest=key,Objective=objective,Receipt=receipt);l(b,'True',advance,'In')
 event=n('Event.LateUpdate');xp=n('Gameplay.QuestProgression');coins=n('Gameplay.InventoryCount',Item='Coin');badges=n('Gameplay.InventoryCount',Item='Badge');parts=[]
 for source,pin,prefix in [(xp,'Level','LEVEL '),(xp,'XP','  |  XP '),(coins,'Count','  |  COINS '),(badges,'Count','  |  BADGES ')]:
  number=n('NumberToText');l(source,pin,number,'Number');part=n('Concat',A=prefix);l(number,'Value',part,'B');parts.append(part)
 current=parts[0]
 for part in parts[1:]:
  join=n('Concat');l(current,'Value',join,'A');l(part,'Value',join,'B');current=join
 label=n('SetUIText',Target=1209);chain(event,label);l(current,'Value',label,'Text');g.save('Player'+suffix+'.zgraph',1 if two else 2)
 g=Graph();n=g.n;l=g.l;chain=g.chain;g.data['Variables']={'Auto':{'Type':'Bool','Default':False},'Checks':{'Type':'Bool','Default':False},'Phase':{'Type':'Number','Default':0}}
 event=n('Event.Start');spawn=n('Gameplay.SaveSpawn',Key='Guard',Prefab=f'Prefabs/Guard{suffix}.zprefab',Position=[7.2,0,0]);chain(event,spawn)
 auto=n('Get.Bool',Name='Auto');phase=n('Get.Number',Name='Phase')
 start=n('Event.Start');loadprefs=n('LoadGameState');chain(start,loadprefs);gate=n('Branch');chain(loadprefs,gate);l(auto,'Value',gate,'Condition');readphase=n('ReadState.Number',Key='Quest.Phase');setphase=n('Set.Number',Name='Phase');l(gate,'True',setphase,'In');l(readphase,'Value',setphase,'Value')
 for p in (0,1,2):
  start=n('Event.Start');same=n('Equal',B=p);l(phase,'Value',same,'A');condition=n('And');l(auto,'Value',condition,'A');l(same,'Value',condition,'B');ready=n('Delay',Seconds=.05);b=n('Branch');chain(start,ready,b);l(condition,'Value',b,'Condition');last=n('Delay',Seconds=.5 if p==0 else .66);l(b,'True',last,'In')
  plan=[('Move',1,.5),('Use',0,.5),('Use',0,.5),('Use',0,.15),('Attack',0,.52),('Use',0,.4),('Move',0,.1),('Claim',0,.2),('Save',0,.1)] if p==0 else [('Load',0,.34),('Move',-1,2.67),('Move',0,.17),('Claim',0,.5),('Save',0,.1)] if p==1 else [('Load',0,.67),('Claim',0,.1)]
  for action,value,delay in plan:
   command=n('WriteState.Number',Key='Quest.Move',Value=value,Persistent=False) if action=='Move' else n('SendMessage',Target=800,Name=action);wait=n('Delay',Seconds=delay);chain(last,command,wait);last=wait
  event=n('Event.Start');checks=n('Get.Bool',Name='Checks');both=n('And');l(checks,'Value',both,'A');l(same,'Value',both,'B');ready=n('Delay',Seconds=.05);b=n('Branch');chain(event,ready,b);l(both,'Value',b,'Condition');wait=n('Delay',Seconds=4.2 if p==0 else 4.85 if p==1 else 2);l(b,'True',wait,'In')
  survey=n('Gameplay.QuestState',Target=800,Quest='Survey');home=n('Gameplay.QuestState',Target=800,Quest='Home');xp=n('Gameplay.QuestProgression',Target=800);coins=n('Gameplay.InventoryCount',Target=800,Item='Coin');badge=n('Gameplay.InventoryCount',Target=800,Item='Badge');terms=[]
  for source,pin,wanted in [(xp,'XP',150 if p==0 else 200),(coins,'Count',5),(badge,'Count',0 if p==0 else 1)]:
   check=n('Equal',B=wanted);l(source,pin,check,'A');terms.append(check)
  claimed=n('Not');l(survey,'Claimed',claimed,'A');invert=n('Not');l(claimed,'Value',invert,'A');terms.append(invert)
  check=n('TextEqual',B='Active' if p==0 else 'Claimed');l(home,'Mode',check,'A');terms.append(check);terms.append(g.read('Saved' if p==0 else 'Loaded'))
  condition=terms[0]
  for term in terms[1:]:
   both=n('And');l(condition,'Value',both,'A');l(term,'Value',both,'B');condition=both
  passed=n('Branch');chain(wait,passed);l(condition,'Value',passed,'Condition');ok=n('Print',Text='QUEST_ACCEPTANCE collect=1 kill=1 interact=1 trigger=1 claim=1 save=2' if p==0 else 'QUEST_RESUMED claims=2 reward=1 xp=200' if p==1 else 'QUEST_NO_REPLAY xp=200 coin=5 badge=1');bad=n('Print',Text='QUEST_ORACLE_FAILED');l(passed,'True',ok,'In');l(passed,'False',bad,'In')
 g.save('Director'+suffix+'.zgraph',5 if two else 6)
manifest=json.loads((ROOT/'Projects/SaveWorkshop/SaveWorkshop.zproject').read_text());manifest['Project'].update(Name='QuestWorkshop',StartScene='Scenes/Quest2D.zscene',Description='Real collect/kill/interact/trigger quest course with atomic rewards, XP and v2 fresh-process saves.');manifest['Player']['Product']='Zamin Quest Workshop';inputmap=copy.deepcopy(json.loads((ROOT/'Projects/UIWorkshop/UIWorkshop.zproject').read_text())['InputActions']);inputmap['Actions'] += [{'Name':'Claim','Bindings':['Key:R']},{'Name':'QuickSave','Bindings':['Key:F5']},{'Name':'QuickLoad','Bindings':['Key:F9']}];manifest['InputActions']=inputmap;write(OUT/'QuestWorkshop.zproject',manifest)
if not (OUT/'Thumbnail.png').exists():shutil.copyfile(ROOT/'Projects/SaveWorkshop/Thumbnail.png',OUT/'Thumbnail.png')
