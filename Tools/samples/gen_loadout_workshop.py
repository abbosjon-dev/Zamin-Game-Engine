#!/usr/bin/env python3
"""Deterministic compiler-free equipment, abilities, combat, quests and v3-save workshop."""
import copy,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Projects/LoadoutWorkshop';ASSETS=OUT/'Assets'
for f in ('Scenes','Scripts'):(ASSETS/f).mkdir(parents=True,exist_ok=True)
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def meta(p,i,t):write(Path(str(p)+'.zmeta'),{'UUID':f'ea715c034ab2{i:04x}','Type':t,'Import':{}})
def script(name):return {'Class':'Zamin.VisualGraph','Fields':{'Graph':{'Type':'String','Value':'Scripts/'+name}}}
class Graph:
 def __init__(self):self.data={'Format':'Zamin.VisualGraph','Version':1,'Nodes':[],'Links':[],'Variables':{}}
 def n(self,kind,**inputs):
  i=len(self.data['Nodes'])+1;self.data['Nodes'].append({'ID':i,'Kind':kind,'X':(i-1)%8*290,'Y':(i-1)//8*320,'Inputs':inputs});return i
 def l(self,a,out,b,pin):self.data['Links'].append({'From':a,'Output':out,'To':b,'Input':pin})
 def chain(self,*nodes):
  for a,b in zip(nodes,nodes[1:]):self.l(a,'Then',b,'In')
 def save(self,name,i):p=ASSETS/'Scripts'/name;write(p,self.data);meta(p,i,'VisualGraph')
for two in (True,False):
 suffix='2D' if two else '3D';g=Graph();n=g.n;l=g.l;chain=g.chain;g.data['Variables']={'Auto':{'Type':'Bool','Default':False},'Checks':{'Type':'Bool','Default':False},'Phase':{'Type':'Number','Default':0}}
 start=n('Event.Start');last=start
 # All configuration executes on one host, so definitions and save bindings are complete before input.
 operations=[('Gameplay.HealthConfigure',dict(Maximum=100,Shield=50,HitGrace=0,Team=1)),('Gameplay.HealthConfigure',dict(Target=850,Maximum=100,HitGrace=0,Team=2)),('Gameplay.InventoryConfigure',dict(Slots=8,Stack=99)),('Gameplay.LoadoutConfigure',dict(Stats=[0,0,0,100,0]))]
 for item,slot,mods in [('Sword',0,[20,0,10,0,0]),('Plate',2,[0,100,0,50,0]),('Charm',3,[0,0,0,0,5])]:operations.extend([('Gameplay.ItemDefine',dict(Item=item,Slot=slot,Modifiers=mods)),('Gameplay.InventoryAdd',dict(Item=item,Count=1))])
 operations.append(('Gameplay.InventoryAdd',dict(Item='Potion',Count=2)))
 for key,kind,amount,cost,cool,ingredient in [('Bolt',0,30,25,1,''),('Heal',1,25,10,1,'Potion'),('Shield',2,20,10,2,'')]:operations.append(('Gameplay.AbilityDefine',dict(Ability=key,Kind=kind,Amount=amount,Cost=cost,Cooldown=cool,Range=5,Ingredient=ingredient)))
 operations.extend([('Gameplay.QuestConfigure',dict(Quest='Sentry',Title='Defeat the sentry',Objectives=['Kill'],Labels=['Sentry defeated'],Kinds=[2],Matches=['Enemy'],Goals=[1],Rewards=['Coin'],RewardCounts=[5],XP=150)),('Gameplay.QuestStart',dict(Quest='Sentry')),('Gameplay.SaveBind',dict(Key='Player')),('Gameplay.SaveBind',dict(Target=850,Key='Sentry')),('Gameplay.Damage',dict(Amount=30,Source=0,PierceShield=True,IgnoreGrace=True))])
 for kind,args in operations:
  op=n(kind,**args);chain(last,op);last=op
 # Keyboard inputs are the same commands used by automation; no test-only state setters.
 for key,name in [('1','Sword'),('2','Plate'),('3','Charm'),('U','Unequip'),('Space','Bolt'),('H','Heal'),('J','Shield'),('R','Claim'),('F5','Save'),('F9','Load')]:
  event=n('Event.Update');pressed=n('ActionJustPressed',Name=name);gate=n('Branch');chain(event,gate);l(pressed,'Value',gate,'Condition');emit=n('EmitEvent',Name=name);l(gate,'True',emit,'In')
 for item in ('Sword','Plate','Charm'):
  event=n('Event.Custom',Name=item);op=n('Gameplay.EquipmentEquip',Item=item);chain(event,op)
 event=n('Event.Custom',Name='Unequip');op=n('Gameplay.EquipmentUnequip',Slot=0);chain(event,op)
 for key in ('Bolt','Heal','Shield'):
  event=n('Event.Custom',Name=key);op=n('Gameplay.AbilityCast',Ability=key,Victim=850 if key=='Bolt' else 0,TwoD=two,LineOfSight=False);chain(event,op)
 event=n('Event.Custom',Name='Claim');op=n('Gameplay.QuestClaim',Quest='Sentry');chain(event,op)
 for name,kind in [('Save','Gameplay.SaveWrite'),('Load','Gameplay.SaveLoad')]:
  event=n('Event.Custom',Name=name);op=n(kind,Slot='Loadout',Schema='LoadoutLabV1');chain(event,op);b=n('Branch');chain(op,b);l(op,'Success',b,'Condition');bad=n('Print',Text='LOADOUT_IO_FAILED');l(b,'False',bad,'In')
  if name=='Save':
   state=n('Gameplay.QuestState',Quest='Sentry');choose=n('Branch');l(b,'True',choose,'In');l(state,'Claimed',choose,'Condition')
   for pin,phase in [('True',2),('False',1)]:
    flag=n('WriteState.Number',Key='Loadout.Phase',Value=phase,Persistent=True);flush=n('SaveGameState');l(choose,pin,flag,'In');chain(flag,flush)
 # HUD: canonical native state, not mirrored Blueprint variables.
 stats=n('Gameplay.LoadoutStats');bolt=n('Gameplay.AbilityState',Ability='Bolt');health=n('Gameplay.Health');enemy=n('Gameplay.Health',Target=850);potion=n('Gameplay.InventoryCount',Item='Potion');coins=n('Gameplay.InventoryCount',Item='Coin');quest=n('Gameplay.QuestProgression')
 def line(target,parts):
  event=n('Event.LateUpdate');last=None
  for source,pin,prefix in parts:
   text=n('NumberToText');l(source,pin,text,'Number');part=n('Concat',A=prefix);l(text,'Value',part,'B')
   if last:
    join=n('Concat');l(last,'Value',join,'A');l(part,'Value',join,'B');last=join
   else:last=part
  label=n('SetUIText',Target=target);chain(event,label);l(last,'Value',label,'Text')
 line(1203,[(stats,'Attack','ATTACK  '),(stats,'Armor','     ARMOR  '),(stats,'Power','     POWER  ')])
 line(1204,[(stats,'Energy','ENERGY  '),(stats,'Maximum',' / '),(stats,'Regen','     REGEN / s  ')])
 line(1205,[(bolt,'Remaining','BOLT COOLDOWN  '),(bolt,'Uses',' s     CASTS  '),(enemy,'Current','     SENTRY HP  ')])
 line(1206,[(health,'Current','HEALTH  '),(health,'Shield','     SHIELD  '),(potion,'Count','     POTIONS  ')])
 line(1207,[(quest,'XP','QUEST XP  '),(coins,'Count','     COINS  ')])
 for slot,target in [(0,1210),(2,1211),(3,1212)]:
  event=n('Event.LateUpdate');s=n('Gameplay.EquipmentState',Slot=slot);prefix=n('Concat',A=['WEAPON  ','','BODY  ','TRINKET  '][slot]);l(s,'Item',prefix,'B');label=n('SetUIText',Target=target);chain(event,label);l(prefix,'Value',label,'Text')
 auto=n('Get.Bool',Name='Auto');phase=n('Get.Number',Name='Phase');event=n('Event.Start');load=n('LoadGameState');chain(event,load);b=n('Branch');chain(load,b);l(auto,'Value',b,'Condition');pref=n('ReadState.Number',Key='Loadout.Phase');setphase=n('Set.Number',Name='Phase');l(b,'True',setphase,'In');l(pref,'Value',setphase,'Value')
 for phaseval in (0,1,2):
  same=n('Equal',B=phaseval);l(phase,'Value',same,'A');both=n('And');l(auto,'Value',both,'A');l(same,'Value',both,'B');event=n('Event.Start');delay=n('Delay',Seconds=.05);b=n('Branch');chain(event,delay,b);l(both,'Value',b,'Condition');last=n('Delay',Seconds=.2);l(b,'True',last,'In')
  plan=[('Sword',.05),('Plate',.05),('Bolt',.1),('Heal',.1),('Save',.1)] if phaseval==0 else [('Load',1.1),('Bolt',.1),('Claim',.1),('Save',.1)] if phaseval==1 else [('Load',1.1),('Bolt',.05),('Claim',.05),('Sword',.05)]
  for name,wait in plan:
   op=n('EmitEvent',Name=name);delay=n('Delay',Seconds=wait);chain(last,op,delay);last=delay
  checks=n('Get.Bool',Name='Checks');both=n('And');l(checks,'Value',both,'A');l(same,'Value',both,'B');event=n('Event.Start');delay=n('Delay',Seconds=.05);b=n('Branch');chain(event,delay,b);l(both,'Value',b,'Condition');wait=n('Delay',Seconds=3);l(b,'True',wait,'In');terms=[]
  for source,pin,wanted in [(stats,'Attack',20),(stats,'Armor',100),(stats,'Energy',65 if phaseval==0 else 40),(bolt,'Uses',1 if phaseval==0 else 2),(enemy,'Current',40 if phaseval==0 else 0),(potion,'Count',1),(coins,'Count',0 if phaseval==0 else 5),(quest,'XP',0 if phaseval==0 else 150)]:
   equal=n('Equal',B=wanted);l(source,pin,equal,'A');terms.append(equal)
  condition=terms[0]
  for term in terms[1:]:
   a=n('And');l(condition,'Value',a,'A');l(term,'Value',a,'B');condition=a
  b=n('Branch');chain(wait,b);l(condition,'Value',b,'Condition');good=n('Print',Text=['LOADOUT_ACCEPTANCE equip=2 casts=1 potion=1 save=3','LOADOUT_RESUMED casts=2 xp=150 coin=5','LOADOUT_NO_REPLAY casts=2 energy=40'][phaseval]);bad=n('Print',Text='LOADOUT_ORACLE_FAILED');l(b,'True',good,'In');l(b,'False',bad,'In')
 g.save('Loadout'+suffix+'.zgraph',1 if two else 2)
 base=json.loads((ROOT/f'Projects/QuestWorkshop/Assets/Scenes/Quest{suffix}.zscene').read_text());base['Scene']='Loadout '+suffix
 keep={800,810,801,940,1200,1201};base['Entities']=[e for e in base['Entities'] if e['Entity'] in keep]
 hero=next(e for e in base['Entities'] if e['Entity']==800);hero['NativeScript']=script('Loadout'+suffix+'.zgraph');hero['Transform']['Translation'][0]=-1
 visual=copy.deepcopy(next(e for e in base['Entities'] if e['Entity']==810));visual['Entity']=851;visual['Parent']=850;visual['Tag']='Sentry visual';visual['SpriteRenderer' if two else 'MeshRenderer']['Color' if two else 'Albedo']=[.95,.3,.32,1]
 base['Entities'].extend([{'Entity':850,'Tag':'Sentry','Tags':['Enemy'],'Transform':{'Translation':[2,0,0],'Rotation':[0,0,0],'Scale':[1,1,1]}},visual])
 template=next(e for e in base['Entities'] if e['Entity']==1201);template['Text']['Text']='LOADOUT LAB  /  equipment + abilities + persistent progression';template['Text']['Color']=[.28,.9,.78,1]
 labels=[(1202,'1 Sword | 2 Plate | 3 Charm | U Unequip | Space Bolt | H Heal | J Shield | R Claim | F5/F9 Save/Load',67),(1203,'Stats',120),(1204,'Energy',163),(1205,'Ability',206),(1206,'Health',249),(1207,'Rewards',292),(1210,'Weapon',520),(1211,'Body',563),(1212,'Trinket',606)]
 for id,text,y in labels:
  e=copy.deepcopy(template);e['Entity']=id;e['Tag']='Loadout HUD';e['UITransform']['Position'][1]=-y;e['UITransform']['FontSize']=17 if id==1202 else 20;e['Text']['Text']=text;e['Text']['Color']=[.85,.91,1,1];base['Entities'].append(e)
 p=ASSETS/f'Scenes/Loadout{suffix}.zscene';write(p,base);meta(p,3 if two else 4,'Scene')
manifest=json.loads((ROOT/'Projects/QuestWorkshop/QuestWorkshop.zproject').read_text());manifest['Project'].update(Name='LoadoutWorkshop',StartScene='Scenes/Loadout3D.zscene',ScriptModulePath='',Description='Equipment, stat modifiers, abilities and v3 persistent combat progression.');manifest['Player']['Product']='Zamin Loadout Workshop';manifest['InputActions']['Actions']=[{'Name':name,'Bindings':['Key:'+key]} for key,name in [('1','Sword'),('2','Plate'),('3','Charm'),('U','Unequip'),('Space','Bolt'),('H','Heal'),('J','Shield'),('R','Claim'),('F5','Save'),('F9','Load')]];write(OUT/'LoadoutWorkshop.zproject',manifest)
