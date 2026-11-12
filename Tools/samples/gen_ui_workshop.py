#!/usr/bin/env python3
"""Engine-owned compiler-free HUD, paged inventory, modal focus and pause/save UI course."""
import json,copy,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Projects/UIWorkshop';ASSETS=OUT/'Assets'
for folder in ('Scripts','Scenes','Prefabs'):(ASSETS/folder).mkdir(parents=True,exist_ok=True)
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def meta(p,i,kind):write(Path(str(p)+'.zmeta'),{'UUID':f'c350ba937ef1{i:04x}','Type':kind,'Import':{}})
def script(name):return {'Class':'Zamin.VisualGraph','Fields':{'Graph':{'Type':'String','Value':'Scripts/'+name}}}
class Graph:
 def __init__(self):self.data={'Format':'Zamin.VisualGraph','Version':1,'Nodes':[],'Links':[],'Variables':{}}
 def n(self,kind,**inputs):
  i=len(self.data['Nodes'])+1;self.data['Nodes'].append({'ID':i,'Kind':kind,'X':(i-1)%8*290,'Y':(i-1)//8*320,'Inputs':inputs});return i
 def l(self,a,out,b,pin):self.data['Links'].append({'From':a,'Output':out,'To':b,'Input':pin})
 def chain(self,*nodes):
  for a,b in zip(nodes,nodes[1:]):self.l(a,'Then',b,'In')
 def save(self,name,i):p=ASSETS/'Scripts'/name;write(p,self.data);meta(p,i,'VisualGraph')
 def success(self,source):
  b=self.n('Branch');self.chain(source,b);self.l(source,'Success',b,'Condition');bad=self.n('Print',Text='UI_OPERATION_FAILED');self.l(b,'False',bad,'In');return b
COLORS={'ink':[.035,.045,.07,1],'card':[.065,.08,.12,1],'soft':[.1,.13,.19,1],'text':[.88,.93,1,1],'muted':[.48,.59,.72,1],'teal':[.14,.78,.65,1],'purple':[.5,.36,.9,1]}
def rect(i,name,parent,pos,size,color=None,text=None,font=20,order=0):
 e={'Entity':i,'Tag':name,'Parent':parent,'Transform':{'Translation':[0,0,0],'Rotation':[0,0,0],'Scale':[1,1,1]},'UITransform':{'AnchorMin':[.5,.5],'AnchorMax':[.5,.5],'Pivot':[.5,.5],'Position':list(pos),'Size':list(size),'FontSize':font,'Order':order}}
 if color is not None:e['UIImage']={'Color':color,'RaycastTarget':False}
 if text is not None:e['Text']={'Text':text,'Color':COLORS['text'],'AlignH':'Center','AlignV':'Middle'}
 return e
def button(i,name,parent,pos,size,message):
 e=rect(i,name,parent,pos,size,text=name);e['UIButton']={'Normal':COLORS['soft'],'Hover':[.18,.27,.36,1],'Pressed':[.12,.5,.43,1],'ClickMessage':message,'ClickTarget':900};e['UINavigation']={'Mode':'Explicit','ShowHighlight':True,'HighlightColor':COLORS['teal']};e['UIAnimation']={'Show':'Fade','Hide':'Fade','Duration':.15,'HoverScale':1.03,'PressScale':.97};return e
def links(e,up,down,left,right):e['UINavigation'].update(Up=up,Down=down,Left=left,Right=right)
for two in (True,False):
 suffix='2D' if two else '3D';base=json.loads((ROOT/f'Projects/SaveWorkshop/Assets/Scenes/Save{suffix}.zscene').read_text());base['Scene']='UI '+suffix;base['Entities']=[e for e in base['Entities'] if e['Entity'] not in (903,1000,1001,1002,1003,1004)]
 for e in base['Entities']:
  if e['Entity']==800:e['NativeScript']=script('Player'+suffix+'.zgraph')
  if e['Entity']==900:e['NativeScript']=script('Controller'+suffix+'.zgraph');e['Tag']='UI controller'
 for kind,number in [('Brain',3),('Guard',9)]:
  ext='zgraph' if kind=='Brain' else 'zprefab';folder='Scripts' if kind=='Brain' else 'Prefabs';v=json.loads((ROOT/f'Projects/SaveWorkshop/Assets/{folder}/{kind}{suffix}.{ext}').read_text());p=ASSETS/f'{folder}/{kind}{suffix}.{ext}';write(p,v);meta(p,number if two else number+1,'VisualGraph' if kind=='Brain' else 'Prefab')
 entities=base['Entities'];entities.append({'Entity':2000,'Tag':'Game UI','Transform':{'Translation':[0,0,0],'Rotation':[0,0,0],'Scale':[1,1,1]},'UICanvas':{'ScaleMode':'ScaleWithScreenSize','ReferenceResolution':[1280,720]}})
 entities += [rect(2100,'Vitals card',2000,[-430,255],[360,145],COLORS['card']),rect(2101,'Player health',2100,[0,35],[320,30],text='VITALITY',font=21),rect(2102,'Coin count',2100,[-65,-25],[180,26],text='COINS 8',font=17),rect(2103,'Bag capacity',2100,[95,-25],[145,26],text='BAG 6 / 12',font=17)]
 bar=rect(2110,'Health progress',2100,[0,5],[300,13]);bar['UIProgressBar']={'Value':.75,'Padding':0,'BackgroundColor':[.14,.19,.24,1],'FillColor':COLORS['teal'],'SmoothSpeed':8};entities.append(bar)
 entities += [rect(2120,'Brand',2000,[390,300],[420,36],text='ZAMIN  /  GAMEPLAY UI',font=20),rect(2121,'Controls',2000,[0,-315],[1120,32],text='WASD move     I / Y inventory     Esc / Start pause     Enter / A select     B back',font=18),rect(2122,'Course status',2000,[0,-278],[1120,28],text='Live HUD  |  Stable prefab save slots  |  Keyboard + controller',font=17)]
 for root,name,order in [(2200,'Inventory panel',20),(3100,'Pause panel',30),(4100,'Controls panel',40)]:
  e=rect(root,name,2000,[0,0],[0,0],[.015,.02,.035,.95],order=order);e['UITransform']['AnchorMin']=[0,0];e['UITransform']['AnchorMax']=[1,1];e['UIImage']['RaycastTarget']=True;e['Active']=False;e['UIAnimation']={'Show':'Fade','Hide':'Fade','Duration':.18};entities.append(e)
 entities += [rect(2208,'Inventory surface',2200,[0,0],[1080,570],COLORS['card']),rect(2250,'Inventory heading',2200,[-310,225],[390,45],text='INVENTORY',font=32),rect(2251,'Inventory subtitle',2200,[-300,184],[410,28],text='FIELD KIT  /  YOUR ITEMS',font=15),rect(2252,'Page label',2200,[340,225],[260,35],text='PAGE 1 / 2',font=17),rect(2253,'Details card',2200,[335,20],[285,320],COLORS['soft']),rect(2254,'Details caption',2200,[335,135],[240,30],text='SELECTED ITEM',font=15),rect(2255,'Selected item',2200,[335,55],[240,50],text='Apple',font=28),rect(2256,'Selected count',2200,[335,0],[240,35],text='Quantity 3',font=18),rect(2257,'Persistence note',2200,[335,-90],[240,55],text='Save & load your field kit',font=15)]
 for i,p in [(2201,[-355,70]),(2202,[-55,70]),(2203,[-355,-75]),(2204,[-55,-75])]:entities.append(button(i,'ITEM',2200,p,[270,115],'Select'))
 nav={2201:(2305,2203,2202,2202),2202:(2305,2204,2201,2201),2203:(2201,2301,2204,2204),2204:(2202,2303,2203,2203)}
 for index,(i,label,message) in enumerate([(2301,'PREVIOUS','Previous'),(2302,'NEXT','Next'),(2303,'SAVE','Save'),(2304,'LOAD','Load'),(2305,'CLOSE','CloseBag')]):
  e=button(i,label,2200,[-410+205*index,-220],[175,48],message);links(e,2201 if index<2 else 2202,2201,2301+(index-1)%5,2301+(index+1)%5);entities.append(e)
 for e in entities:
  if e['Entity'] in nav:
   links(e,*nav[e['Entity']]);e['UINavigation']['Mode']='Automatic';e['UIImage']={'Color':COLORS['soft'],'RaycastTarget':False};e['UIButton'].update(Normal=[1,1,1,1],Hover=[2,2.1,2.3,1],Pressed=[1.1,3,3,1])
 entities += [rect(3108,'Pause surface',3100,[0,0],[480,560],COLORS['card']),rect(3150,'Pause heading',3100,[0,220],[420,50],text='PAUSED',font=38),rect(3151,'Pause subtitle',3100,[0,175],[430,34],text='Your world can wait.',font=17)]
 for index,(i,label,msg) in enumerate([(3101,'RESUME','Resume'),(3102,'INVENTORY','OpenBag'),(3103,'CONTROLS','Help'),(3104,'SAVE PROGRESS','Save'),(3105,'LOAD PROGRESS','Load')]):
  e=button(i,label,3100,[0,105-70*index],[350,48],msg);links(e,3101+(index-1)%5,3101+(index+1)%5,0,0);entities.append(e)
 entities += [rect(4108,'Controls surface',4100,[0,0],[620,400],COLORS['card']),rect(4150,'Controls heading',4100,[0,135],[560,50],text='CONTROLS',font=32),rect(4151,'Keyboard guide',4100,[0,65],[560,40],text='Keyboard: WASD  /  I  /  Escape  /  Enter',font=20),rect(4152,'Gamepad guide',4100,[0,5],[560,40],text='Controller: left stick  /  Y  /  Start  /  A  /  B',font=19)]
 e=button(4101,'BACK',4100,[0,-110],[300,48],'Back');links(e,4101,4101,0,0);entities.append(e)
 p=ASSETS/f'Scenes/UI{suffix}.zscene';write(p,base);meta(p,7 if two else 8,'Scene')
 g=Graph();n=g.n;l=g.l;chain=g.chain;g.data['Variables']={'Auto':{'Type':'Bool','Default':False}}
 start=n('Event.Start');hp=n('Gameplay.HealthConfigure',Maximum=100,Shield=20,HitGrace=0,Team=1);bag=n('Gameplay.InventoryConfigure',Slots=12,Stack=99);bind=n('Gameplay.SaveBind',Key='Player');chain(start,hp,bag,bind);last=bind
 for item,count in [('Apple',3),('Coin',8),('Key',2),('Potion',2),('Shield',1),('Sword',1)]:
  node=n('Gameplay.InventoryAdd',Item=item,Count=count);chain(last,node);last=node
 hurt=n('Gameplay.Damage',Amount=45,IgnoreGrace=True);chain(last,hurt)
 update=n('Event.Update');raw=n('Gameplay.Input');motor=n('Gameplay.Motor2D' if two else 'Gameplay.Motor3D',**({'TopDown':True} if two else {}),Speed=4);chain(update,motor);l(raw,'Move',motor,'Move');l(raw,'Enabled',motor,'Enabled');l(update,'Delta',motor,'Delta');g.save('Player'+suffix+'.zgraph',1 if two else 2)
 g=Graph();n=g.n;l=g.l;chain=g.chain;g.data['Variables']={key:{'Type':'Bool','Default':False} for key in ['Auto','Checks','Capture','SawBag','SawPause','SawNested','SawPage','SawSelected','Blocked','Saved','Loaded','Reported']};g.data['Variables']['Step']={'Type':'Number','Default':0}
 start=n('Event.Start');run=n('Gameplay.UIRunWhenPaused');spawn=n('Gameplay.SaveSpawn',Key='Guard',Prefab=f'Prefabs/Guard{suffix}.zprefab',Position=[-5,0,0]);chain(start,run,spawn);last=spawn
 for root,name,action,focus,pause in [(2200,'Bag','Inventory',2201,False),(3100,'Pause','Pause',3101,True),(4100,'Help','',4101,False)]:
  node=n('Gameplay.UIConfigure',Target=root,Name=name,Action=action,Focus=focus,Pause=pause);chain(last,node);last=node
 for widget,kind,prefix,ratio in [(2101,0,'VITALITY  ',True),(2110,0,'',True),(2102,2,'COINS  ',False),(2103,3,'BAG  ',True)]:
  node=n('Gameplay.UIBindHUD',Target=widget,Actor=800,Kind=kind,Item='Coin',Prefix=prefix,Ratio=ratio);chain(last,node);last=node
 inv=n('Gameplay.UIInventoryConfigure',Actor=800,Name='Bag',Rows=[2201,2202,2203,2204]);chain(last,inv)
 for message,kind,args in [('OpenBag','Gameplay.UIOpen',{'Name':'Bag'}),('Resume','Gameplay.UIBack',{}),('Help','Gameplay.UIOpen',{'Name':'Help'}),('Back','Gameplay.UIBack',{}),('CloseBag','Gameplay.UIClose',{'Name':'Bag'}),('Next','Gameplay.UIInventoryPage',{'Name':'Bag','Delta':1}),('Previous','Gameplay.UIInventoryPage',{'Name':'Bag','Delta':-1}),('Save','Gameplay.SaveWrite',{'Slot':'Quick','Schema':'UILabV1'}),('Load','Gameplay.SaveLoad',{'Slot':'Quick','Schema':'UILabV1'})]:
  custom=n('Event.Custom',Name=message);op=n(kind,**args);chain(custom,op)
  if message in ('Save','Load'):
   branch=n('Branch');chain(op,branch);l(op,'Success',branch,'Condition');mark=n('Set.Bool',Name='Saved' if message=='Save' else 'Loaded',Value=True);l(branch,'True',mark,'In');
   if message=='Save':
    damage=n('Gameplay.Damage',Target=800,Amount=35,IgnoreGrace=True);remove=n('Gameplay.InventoryRemove',Target=800,Item='Coin',Count=3);chain(mark,damage,remove)
   bad=n('Print',Text='UI_SAVE_FAILED');l(branch,'False',bad,'In')
  event=n('Event.Message');eq=n('TextEqual',B=message);l(event,'Text',eq,'A');b=n('Branch');chain(event,b);l(eq,'Value',b,'Condition');emit=n('EmitEvent',Name=message);l(b,'True',emit,'In')
 state=n('Gameplay.UIState');inventory=n('Gameplay.UIInventoryState',Name='Bag');raw=n('Gameplay.Input',Target=800)
 # Poll canonical state for both live labels and acceptance telemetry.
 for output,key,kind,constant in [('Top','SawBag','TextEqual','Bag'),('Top','SawPause','TextEqual','Pause'),('Depth','SawNested','Greater',1)]:
  event=n('Event.Update');check=n(kind,B=constant);l(state,output,check,'A');b=n('Branch');chain(event,b);l(check,'Value',b,'Condition');mark=n('Set.Bool',Name=key,Value=True);l(b,'True',mark,'In')
 for source,output,key in [(inventory,'Page','SawPage'),(inventory,'Count','SawSelected')]:
  event=n('Event.Update');check=n('Greater',B=0);l(source,output,check,'A');b=n('Branch');chain(event,b);l(check,'Value',b,'Condition');mark=n('Set.Bool',Name=key,Value=True);l(b,'True',mark,'In')
 event=n('Event.Update');disabled=n('Not');l(raw,'Enabled',disabled,'A');b=n('Branch');chain(event,b);l(disabled,'Value',b,'Condition');mark=n('Set.Bool',Name='Blocked',Value=True);l(b,'True',mark,'In')
 event=n('Event.Update');item=n('Concat',A='');l(inventory,'Item',item,'B');label=n('SetUIText',Target=2255);chain(event,label);l(item,'Value',label,'Text');count=n('NumberToText');l(inventory,'Count',count,'Number');c=n('Concat',A='Quantity  ');l(count,'Value',c,'B');label2=n('SetUIText',Target=2256);chain(label,label2);l(c,'Value',label2,'Text')
 page=n('Add',B=1);l(inventory,'Page',page,'A');num=n('NumberToText');l(page,'Value',num,'Number');pfx=n('Concat',A='PAGE ');l(num,'Value',pfx,'B');sep=n('Concat',B=' / ');l(pfx,'Value',sep,'A');total=n('NumberToText');l(inventory,'Pages',total,'Number');full=n('Concat');l(sep,'Value',full,'A');l(total,'Value',full,'B');label3=n('SetUIText',Target=2252);chain(label2,label3);l(full,'Value',label3,'Text')
 # UI time remains live while the gameplay clock is paused. Auto mode is private QA content.
 stages=[(.6,'Gameplay.UIOpen',{'Name':'Bag'}),(.9,'Gameplay.UIInventoryPage',{'Name':'Bag','Delta':1}),(1.2,'Gameplay.UIClose',{'Name':'Bag'}),(1.6,'Gameplay.UIOpen',{'Name':'Pause'}),(1.9,'Gameplay.UIOpen',{'Name':'Help'}),(2.2,'Gameplay.UIBack',{}),(2.5,'Gameplay.UIBack',{}),(2.8,'Gameplay.UIOpen',{'Name':'Bag'}),(3.1,'Gameplay.UIInventoryPage',{'Name':'Bag','Delta':-1}),(3.4,'EmitEvent',{'Name':'Save'}),(3.7,'EmitEvent',{'Name':'Load'}),(4.3,'Gameplay.UIClose',{'Name':'Bag'}),(6.7,'Gameplay.UIOpen',{'Name':'Pause'})]
 auto=n('Get.Bool',Name='Auto');step=n('Get.Number',Name='Step')
 for index,(seconds,kind,args) in enumerate(stages):
  event=n('Event.Update');due=n('Greater',B=seconds);l(state,'Time',due,'A');current=n('Equal',B=index);l(step,'Value',current,'A');both=n('And');l(due,'Value',both,'A');l(current,'Value',both,'B');enabled=n('And');l(auto,'Value',enabled,'A');l(both,'Value',enabled,'B');b=n('Branch');chain(event,b);l(enabled,'Value',b,'Condition');advance=n('Set.Number',Name='Step',Value=index+1);l(b,'True',advance,'In');op=n(kind,**args);chain(advance,op)
 event=n('Event.Update');due=n('Greater',B=7.05);l(state,'Time',due,'A');checks=n('Get.Bool',Name='Checks');reported=n('Get.Bool',Name='Reported');invrep=n('Not');l(reported,'Value',invrep,'A');a=n('And');l(due,'Value',a,'A');l(checks,'Value',a,'B');once=n('And');l(a,'Value',once,'A');l(invrep,'Value',once,'B');b=n('Branch');chain(event,b);l(once,'Value',b,'Condition');mark=n('Set.Bool',Name='Reported',Value=True);l(b,'True',mark,'In')
 terms=[n('Get.Bool',Name=key) for key in ['SawBag','SawPause','SawNested','SawPage','SawSelected','Blocked','Saved','Loaded']]
 for source,output,expected in [(n('Gameplay.Health',Target=800),'Current',75),(n('Gameplay.InventoryCount',Target=800,Item='Coin'),'Count',8)]:
  eq=n('Equal',B=expected);l(source,output,eq,'A');terms.append(eq)
 terms.append(n('Get.Bool',Name='SawPause'));condition=terms[0]
 for term in terms[1:]:
  both=n('And');l(condition,'Value',both,'A');l(term,'Value',both,'B');condition=both
 passed=n('Branch');chain(mark,passed);l(condition,'Value',passed,'Condition');ok=n('Print',Text='UI_ACCEPTANCE hud=1 inventory=1 page=1 focus=1 modal=1 pause=1 save=1 load=1');bad=n('Print',Text='UI_ORACLE_FAILED');l(passed,'True',ok,'In');l(passed,'False',bad,'In');
 # A shorter screenshot run proves that the second inventory page is actually open.
 event=n('Event.Update');capture=n('Get.Bool',Name='Capture');due=n('Greater',B=3.55);l(state,'Time',due,'A');once=n('And');l(capture,'Value',once,'A');l(due,'Value',once,'B');unique=n('And');l(once,'Value',unique,'A');l(invrep,'Value',unique,'B');b=n('Branch');chain(event,b);l(unique,'Value',b,'Condition');mark=n('Set.Bool',Name='Reported',Value=True);l(b,'True',mark,'In');top=n('TextEqual',B='Bag');l(state,'Top',top,'A');page=n('Equal',B=1);l(inventory,'Page',page,'A');native=n('And');l(top,'Value',native,'A');l(page,'Value',native,'B');b=n('Branch');chain(mark,b);l(native,'Value',b,'Condition');ok=n('Print',Text='UI_INVENTORY_SHOWN page=1 slots=6');bad=n('Print',Text='UI_ORACLE_FAILED');l(b,'True',ok,'In');l(b,'False',bad,'In');g.save('Controller'+suffix+'.zgraph',5 if two else 6)
manifest=json.loads((ROOT/'Projects/SaveWorkshop/SaveWorkshop.zproject').read_text());manifest['Project'].update(Name='UIWorkshop',StartScene='Scenes/UI2D.zscene',Description='Live HUD, paged inventory, modal focus/pause and compiler-free save/load UI.');manifest['Player']['Product']='Zamin UI Workshop'
inputmap=copy.deepcopy(json.loads((ROOT/'Projects/Platformer2D/Platformer2D.zproject').read_text())['InputActions']);inputmap['Actions'][0]['Bindings']=['Key:Space','Gamepad:A'];inputmap['Actions'].append({'Name':'Inventory','Bindings':['Key:I','Gamepad:Y']});manifest['InputActions']=inputmap;write(OUT/'UIWorkshop.zproject',manifest)
if not (OUT/'Thumbnail.png').exists():shutil.copyfile(ROOT/'Projects/SaveWorkshop/Thumbnail.png',OUT/'Thumbnail.png')
