#!/usr/bin/env python3
"""Deterministic compiler-free 2D/3D combat slice: projectiles, areas, status effects, HUD feedback and v4 saves.

Writes Projects/CombatWorkshop (scenes, graphs, visual-only projectile prefabs, manifest). Running it twice gives
byte-identical files. The oracle numbers below are derived from the shared rules and this geometry; the headless
test CombatWorkshopOracleHeadless and Tests/combat_workflow_test.py check them against canonical native state.
"""
import copy,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Projects/CombatWorkshop';ASSETS=OUT/'Assets'
for f in ('Scenes','Scripts','Prefabs'):(ASSETS/f).mkdir(parents=True,exist_ok=True)
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def meta(p,i,t):write(Path(str(p)+'.zmeta'),{'UUID':f'c0ba7a5e11d0{i:04x}','Type':t,'Import':{}})
def script(name):return {'Class':'Zamin.VisualGraph','Fields':{'Graph':{'Type':'String','Value':'Scripts/'+name}}}
def transform(p,s=(1,1,1)):return {'Translation':list(p),'Rotation':[0,0,0],'Scale':list(s)}
class Graph:
 def __init__(self):self.data={'Format':'Zamin.VisualGraph','Version':1,'Nodes':[],'Links':[],'Variables':{}}
 def n(self,kind,**inputs):
  i=len(self.data['Nodes'])+1;self.data['Nodes'].append({'ID':i,'Kind':kind,'X':(i-1)%8*290,'Y':(i-1)//8*320,'Inputs':inputs});return i
 def l(self,a,out,b,pin):self.data['Links'].append({'From':a,'Output':out,'To':b,'Input':pin})
 def chain(self,*nodes):
  for a,b in zip(nodes,nodes[1:]):self.l(a,'Then',b,'In')
 def save(self,name,i):p=ASSETS/'Scripts'/name;write(p,self.data);meta(p,i,'VisualGraph')

# ---- Layout (2D: top-down XY; 3D: the same plan on XZ with y up, x3 = x2, z3 = -y2). Units: m.
PLAYER,SENTRY,WALL,HIDDEN=(0,0),(0,5),(0,-1.5),(0,-3)
DUMMIES=[(861,(2,0)),(862,(3,0)),(863,(4,0))]
WALL_LAYER,WALL_MASK=1,2                     # project collision layer 1 "Wall"; masks are 1 << layer
def at(xy,two,y3=0.0):return [xy[0],xy[1],0] if two else [xy[0],y3,-xy[1]]
def aim(two):return [0,0,0] if two else [0,.9,0]  # target/area offset: chest height in 3D

# ---- Canonical oracle (Damage = (Amount + Power + Attack) x 100/(100 + Armor); shield absorbs first; a DoT
# ticks floor(Duration/Period) times; Burn/Poison ticks keep their phase). All armor/power are 0 here.
# Phase 0, checked 2.45 s after Start (Rally .25 s -> Haste +10 Attack for 5 s):
#   Bolt 1 (.35 s, 20+10 = 30, flight (5-.55)/15 = .30 s) -> Sentry 90; Burn tick (3) at hit+.5 -> 87;
#   Bolt 2 (1.05 s) hits after that tick -> 57 and refreshes Burn to 2 stacks (6/tick, phase kept): ticks at
#   hit1+1.0 and +1.5 -> 45 by 2.45 s.  SENTRY_P0 = 120-30-3-30-6-6
SENTRY_P0,BURN_P0=45,2
#   Lance (.45 s, 15+10 = 25, pierce 2 = three hits on the line) and Nova (.65 s, 12+10 = 22, radius 3.5: Dummy1
#   at 2 m and Dummy2 at 3 m; Dummy3 at 4 m is outside; Hidden at 3 m is behind the wall: no line of sight).
DUMMY_HP,DUMMY_NOVA,DUMMY_LANCE,HIDDEN_HP=60,60-25-22,60-25,30
#   Sentry Spit at the first frame (6 = Venom, absorbed by the shield: 30 -> 24; flight (5-.55)/8 = .56 s) gives
#   Poison (Independent, pierces the shield, 2 per 1 s, 3 ticks): one tick by 2.45 s -> 98; Spit 2 (2.0 s) is still
#   flying. Energy 100 - Rally 20 - Bolt 10 - Lance 15 - Nova 20 - Bolt 10 = 25.
PLAYER_P0,SHIELD_P0,ENERGY_P0=98,24,25
# Saved by F5 at ~1.1 s: Bolt 2 in flight, Poison (3 ticks left), Haste (~4.15 s left), Burn (1 stack) and Frost.
# Phase 1 (fresh process, F9 at .25 s): the restored Bolt hits (57 after the first Burn tick), Burn ends at 33,
# Bolt 3 (2.65 s, Haste still active) -> 3, its new Burn tick kills the Sentry (kill credit -> quest reward Coin 5,
# 150 XP, claimed automatically); Bolt 4 (3.85 s) is rejected (dead target) and spends nothing. The 3 remaining
# Poison ticks -> 94. Checked at 4.8 s after Haste ended: Attack 0, Energy 25 - 10 = 15, Bolt uses 3.
PLAYER_P1,ENERGY_P1,XP,COINS=94,15,150,5
# Phase 2 (fresh process): Bolt at the dead Sentry and Nova (15 < 20 energy) are both rejected: nothing changes,
# no reward is granted again and every effect has expired.

for two in (True,False):
 suffix='2D' if two else '3D';g=Graph();n=g.n;l=g.l;chain=g.chain
 g.data['Variables']={'Auto':{'Type':'Bool','Default':False},'Checks':{'Type':'Bool','Default':False},'Phase':{'Type':'Number','Default':0}}
 start=n('Event.Start');last=start
 # All configuration runs on one host before input: health, bags, loadouts, definitions, abilities, quest, binds.
 ops=[('Gameplay.HealthConfigure',dict(Maximum=100,Shield=30,HitGrace=0,Team=1)),('Gameplay.HealthConfigure',dict(Target=850,Maximum=120,HitGrace=0,Team=2))]
 ops+=[('Gameplay.HealthConfigure',dict(Target=i,Maximum=DUMMY_HP,HitGrace=0,Team=2)) for i,_ in DUMMIES]
 ops+=[('Gameplay.HealthConfigure',dict(Target=871,Maximum=HIDDEN_HP,HitGrace=0,Team=2)),('Gameplay.InventoryConfigure',dict(Slots=8,Stack=99)),('Gameplay.InventoryAdd',dict(Item='Potion',Count=1)),('Gameplay.LoadoutConfigure',dict(Stats=[0,0,0,100,0])),('Gameplay.LoadoutConfigure',dict(Target=850,Stats=[0,0,0,1,0]))]
 # Effects: Tick 1 Damage / 2 Heal; Stacking 0 Refresh, 1 Extend, 2 Independent, 3 HighestWins.
 ops+=[('Gameplay.EffectDefine',dict(Effect='Burn',Duration=2,Period=.5,Tick=1,TickAmount=3,Stacking=0,MaxStacks=3)),
       ('Gameplay.EffectDefine',dict(Effect='Poison',Duration=3,Period=1,Tick=1,TickAmount=2,PierceShield=True,Stacking=2,MaxStacks=3)),
       ('Gameplay.EffectDefine',dict(Effect='Shock',Duration=1,Stun=True,Stacking=3,MaxStacks=1)),
       ('Gameplay.EffectDefine',dict(Effect='Frost',Duration=3,Speed=.5,Stacking=1,MaxStacks=2)),
       ('Gameplay.EffectDefine',dict(Effect='Regen',Duration=3,Period=.5,Tick=2,TickAmount=4,Hostile=False,Stacking=0,MaxStacks=1)),
       ('Gameplay.EffectDefine',dict(Effect='Haste',Duration=5,Speed=1.5,Modifiers=[10,0,0,0,0],Hostile=False,Stacking=0,MaxStacks=1)),
       ('Gameplay.AreaDefine',dict(Area='Nova',Kind=0,Amount=12,Radius=3.5,Falloff=0,MaxTargets=8,TargetOffset=aim(two),TwoD=two,LineOfSight=True,Mask=WALL_MASK,Effect='Frost'))]
 for key,speed,life,pierce,effect in [('Bolt',15,1.2,0,'Burn'),('Lance',20,1,2,'Shock'),('Venom',8,2,0,'Poison')]:
  ops.append(('Gameplay.ProjectileDefine',dict(Projectile=key,Kind=0,Speed=speed,Lifetime=life,Radius=.1,TargetRadius=.45,TargetOffset=aim(two),Damage=10,Pierce=pierce,TwoD=two,BlockMask=WALL_MASK,Effect=effect,Prefab=f'Prefabs/{key}{suffix}.zprefab',Pool=8)))
 for key,kind,amount,cost,cool,rng,link in [('Bolt',0,20,10,.5,12,dict(Projectile='Bolt')),('Lance',0,15,15,1,12,dict(Projectile='Lance')),('Nova',0,12,20,3,5,dict(Area='Nova')),('Mend',3,1,10,4,5,dict(Effect='Regen')),('Rally',3,1,20,6,5,dict(Effect='Haste'))]:
  ops.append(('Gameplay.AbilityDefine',dict(Ability=key,Kind=kind,Amount=amount,Cost=cost,Cooldown=cool,Range=rng,**link)))
 ops+=[('Gameplay.AbilityDefine',dict(Target=850,Ability='Spit',Kind=0,Amount=6,Cost=0,Cooldown=2,Range=12,Projectile='Venom')),
       ('Gameplay.QuestConfigure',dict(Quest='Sentry',Title="Sentry'ni yeng",Objectives=['Kill'],Labels=['Sentry yengildi'],Kinds=[2],Matches=['Enemy'],Goals=[1],Rewards=['Coin'],RewardCounts=[COINS],XP=XP)),('Gameplay.QuestStart',dict(Quest='Sentry')),
       ('Gameplay.SaveBind',dict(Key='Player')),('Gameplay.SaveBind',dict(Target=850,Key='Sentry'))]
 ops+=[('Gameplay.SaveBind',dict(Target=i,Key=f'Dummy{k+1}')) for k,(i,_) in enumerate(DUMMIES)]
 ops+=[('Gameplay.SaveBind',dict(Target=871,Key='Hidden'))]
 # HUD: Kind 0 Health bar, 5 Effect rows (stacks and time left), 6 hit marker; a 4-row incoming/outgoing feed.
 ops+=[('Gameplay.UIBindHUD',dict(Target=1206,Actor=850,Kind=0,Prefix='',Ratio=True))]
 for widget,actor,key,prefix,ratio in [(1210,800,'Poison','ZAHAR x',True),(1211,850,'Burn','YONISH x',True),(1212,861,'Frost','MUZLASH  ',False),(1213,861,'Shock','KARAXT  ',False),(1214,800,'Regen','TIKLANISH  ',False),(1215,800,'Haste','SHIJOAT  ',False)]:
  ops.append(('Gameplay.UIBindHUD',dict(Target=widget,Actor=actor,Kind=5,Item=key,Prefix=prefix,Ratio=ratio)))
 ops+=[('Gameplay.UIBindHUD',dict(Target=1220,Actor=800,Kind=6,Item='',Prefix='X',Ratio=False)),('Gameplay.UICombatFeed',dict(Actor=800,Name='Zarbalar',Rows=[1231,1232,1233,1234],Outgoing=True,Incoming=True,Lifetime=2.5))]
 # Kind 3 (status effect) abilities ignore Amount; it only has to be valid. Every configuration step must succeed.
 success=None
 for kind,args in ops:
  op=n(kind,**args);chain(last,op);last=op
  if success:
   a=n('And');l(success,'Value',a,'A');l(op,'Success',a,'B');success=a
  else:
   success=n('And',B=True);l(op,'Success',success,'A')
 b=n('Branch');chain(last,b);l(success,'Value',b,'Condition');failed=n('Print',Text='COMBAT_CONFIG_FAILED');l(b,'False',failed,'In')
 # Keyboard and gamepad actions emit the same custom events that automation uses; no test-only state setters.
 actions=[('Bolt','Space','A'),('Lance','L','X'),('Nova','N','Y'),('Mend','M','B'),('Rally','R','RightBumper'),('Save','F5','Start'),('Load','F9','Back')]
 for name,_,_ in actions:
  event=n('Event.Update');pressed=n('ActionJustPressed',Name=name);gate=n('Branch');chain(event,gate);l(pressed,'Value',gate,'Condition');emit=n('EmitEvent',Name=name);l(gate,'True',emit,'In')
 cast=dict(Bolt=dict(Victim=850),Lance=dict(Direction=[1,0,0]),Nova={},Mend={},Rally={})
 for key,extra in cast.items():
  event=n('Event.Custom',Name=key);op=n('Gameplay.AbilityCast',Ability=key,TwoD=two,LineOfSight=False,**extra);chain(event,op)
 # The Sentry spits Venom at the Player whenever its cooldown allows, in the phases that allow it (QA phases 1-2
 # resume a save, so a live Sentry would change the resumed oracle). A human player always plays phase 0.
 phase=n('Get.Number',Name='Phase');quiet=n('Equal',B=0);l(phase,'Value',quiet,'A');event=n('Event.Update');b=n('Branch');chain(event,b);l(quiet,'Value',b,'Condition');spit=n('Gameplay.AbilityCast',Target=850,Victim=800,Ability='Spit',TwoD=two,LineOfSight=False);l(b,'True',spit,'In')
 # The quest reward is claimed as soon as the Sentry kill completes it.
 quest=n('Gameplay.QuestState',Quest='Sentry');event=n('Event.Update');b=n('Branch');chain(event,b);l(quest,'Ready',b,'Condition');claim=n('Gameplay.QuestClaim',Quest='Sentry');l(b,'True',claim,'In')
 # Movement: WASD / left stick through the shared motor (effects scale its speed, Shock stops it).
 raw=n('Gameplay.Input');event=n('Event.Update');motor=n('Gameplay.Motor2D' if two else 'Gameplay.Motor3D',Speed=4,**({'TopDown':True} if two else {}));chain(event,motor);l(raw,'Move',motor,'Move');l(raw,'Enabled',motor,'Enabled');l(event,'Delta',motor,'Delta')
 sentry=n('Gameplay.Health',Target=850)
 for name,kind in [('Save','Gameplay.SaveWrite'),('Load','Gameplay.SaveLoad')]:
  event=n('Event.Custom',Name=name);op=n(kind,Slot='Combat',Schema='CombatLabV1');chain(event,op);b=n('Branch');chain(op,b);l(op,'Success',b,'Condition');fail=n('Print',Text='COMBAT_IO_FAILED');l(b,'False',fail,'In')
  if name=='Save':
   choose=n('Branch');l(b,'True',choose,'In');l(sentry,'Dead',choose,'Condition')
   for pin,value in [('True',2),('False',1)]:
    flag=n('WriteState.Number',Key='Combat.Phase',Value=value,Persistent=True);flush=n('SaveGameState');l(choose,pin,flag,'In');chain(flag,flush)
 # HUD lines show canonical native state (pure nodes), never mirrored Blueprint variables.
 hp=n('Gameplay.Health');stats=n('Gameplay.LoadoutStats');bolt=n('Gameplay.AbilityState',Ability='Bolt');lance=n('Gameplay.AbilityState',Ability='Lance');nova=n('Gameplay.AbilityState',Ability='Nova')
 dummies=[n('Gameplay.Health',Target=i) for i,_ in DUMMIES];hidden=n('Gameplay.Health',Target=871);coins=n('Gameplay.InventoryCount',Item='Coin');progress=n('Gameplay.QuestProgression');shots=n('Gameplay.ProjectileState',Projectile=0)
 cooldown=n('Ceil');l(bolt,'Remaining',cooldown,'A')
 def line(target,parts):
  event=n('Event.LateUpdate');joined=None
  for source,pin,prefix in parts:
   text=n('NumberToText');l(source,pin,text,'Number');part=n('Concat',A=prefix);l(text,'Value',part,'B')
   if joined:
    join=n('Concat');l(joined,'Value',join,'A');l(part,'Value',join,'B');joined=join
   else:joined=part
  label=n('SetUIText',Target=target);chain(event,label);l(joined,'Value',label,'Text')
 line(1203,[(hp,'Current',"SOG'LIQ  "),(hp,'Shield','     QALQON  '),(stats,'Energy','     ENERGIYA  '),(stats,'Maximum',' / ')])
 line(1204,[(stats,'Attack','HUJUM  '),(cooldown,'Value','     BOLT QAYTA  '),(bolt,'Uses',' s     BOLTLAR  '),(shots,'Live','     UCHMOQDA  ')])
 line(1205,[(sentry,'Current',"SENTRY SOG'LIQ  "),(dummies[0],'Current','     NISHONLAR  '),(dummies[1],'Current',' / '),(dummies[2],'Current',' / '),(hidden,'Current','     YASHIRIN  ')])
 line(1207,[(progress,'XP','XP  '),(coins,'Count','     TANGA  ')])
 # QA phases: Auto replays the command plan in Editor/export runs; Checks compares native state with the oracle.
 auto=n('Get.Bool',Name='Auto');event=n('Event.Start');load=n('LoadGameState');chain(event,load);b=n('Branch');chain(load,b);l(auto,'Value',b,'Condition');pref=n('ReadState.Number',Key='Combat.Phase');setphase=n('Set.Number',Name='Phase');l(b,'True',setphase,'In');l(pref,'Value',setphase,'Value')
 effect=lambda target,key:n('Gameplay.EffectState',Target=target,Effect=key)
 mods=n('Gameplay.CombatModifiers')
 for p in (0,1,2):
  same=n('Equal',B=p);l(phase,'Value',same,'A');both=n('And');l(auto,'Value',both,'A');l(same,'Value',both,'B');event=n('Event.Start');delay=n('Delay',Seconds=.05);b=n('Branch');chain(event,delay,b);l(both,'Value',b,'Condition');last=n('Delay',Seconds=.2);l(b,'True',last,'In')
  plan=[('Rally',.1),('Bolt',.1),('Lance',.2),('Nova',.4),('Bolt',.05),('Save',.1)] if p==0 else [('Load',2.4),('Bolt',1.2),('Bolt',.2),('Save',.1)] if p==1 else [('Load',.6),('Bolt',.2),('Nova',.1)]
  for name,wait in plan:
   op=n('EmitEvent',Name=name);delay=n('Delay',Seconds=wait);chain(last,op,delay);last=delay
  checks=n('Get.Bool',Name='Checks');both=n('And');l(checks,'Value',both,'A');l(same,'Value',both,'B');event=n('Event.Start');delay=n('Delay',Seconds=.05);b=n('Branch');chain(event,delay,b);l(both,'Value',b,'Condition');wait=n('Delay',Seconds=[2.4,4.75,1.95][p]);l(b,'True',wait,'In')
  if p==0:
   terms=[(sentry,'Current',SENTRY_P0),(effect(850,'Burn'),'Stacks',BURN_P0),(hp,'Current',PLAYER_P0),(hp,'Shield',SHIELD_P0),(effect(800,'Poison'),'Instances',1),(dummies[0],'Current',DUMMY_NOVA),(dummies[1],'Current',DUMMY_NOVA),(dummies[2],'Current',DUMMY_LANCE),(hidden,'Current',HIDDEN_HP),(effect(861,'Frost'),'Instances',1),(effect(863,'Frost'),'Instances',0),(stats,'Attack',10),(stats,'Energy',ENERGY_P0),(bolt,'Uses',2),(lance,'Uses',1),(nova,'Uses',1)]
  elif p==1:
   terms=[(sentry,'Current',0),(sentry,'Deaths',1),(effect(850,'Burn'),'Instances',0),(hp,'Current',PLAYER_P1),(hp,'Shield',SHIELD_P0),(effect(800,'Poison'),'Instances',0),(stats,'Attack',0),(stats,'Energy',ENERGY_P1),(bolt,'Uses',3),(progress,'XP',XP),(coins,'Count',COINS),(dummies[0],'Current',DUMMY_NOVA),(hidden,'Current',HIDDEN_HP)]
  else:
   terms=[(sentry,'Current',0),(sentry,'Deaths',1),(progress,'XP',XP),(coins,'Count',COINS),(stats,'Energy',ENERGY_P1),(bolt,'Uses',3),(nova,'Uses',1),(hp,'Current',PLAYER_P1),(mods,'Effects',0),(dummies[0],'Current',DUMMY_NOVA),(dummies[2],'Current',DUMMY_LANCE)]
  condition=None
  for source,pin,wanted in terms:
   equal=n('Equal',B=wanted);l(source,pin,equal,'A')
   if condition:
    a=n('And');l(condition,'Value',a,'A');l(equal,'Value',a,'B');condition=a
   else:condition=equal
  b=n('Branch');chain(wait,b);l(condition,'Value',b,'Condition')
  good=n('Print',Text=[f'COMBAT_ACCEPTANCE sentry={SENTRY_P0} burn={BURN_P0} pierce=3 nova=2 hidden={HIDDEN_HP} poison=1 energy={ENERGY_P0} save=4',f'COMBAT_RESUMED sentry=0 player={PLAYER_P1} xp={XP} coin={COINS} energy={ENERGY_P1}',f'COMBAT_NO_REPLAY bolts=3 xp={XP} coin={COINS} effects=0'][p]);fail=n('Print',Text='COMBAT_ORACLE_FAILED');l(b,'True',good,'In');l(b,'False',fail,'In')
 g.save('Combat'+suffix+'.zgraph',1 if two else 2)

 # ---- Scene: the LoadoutWorkshop base (player, camera, arena, HUD canvas and title) plus the combat course.
 base=json.loads((ROOT/f'Projects/LoadoutWorkshop/Assets/Scenes/Loadout{suffix}.zscene').read_text());base['Scene']='Combat '+suffix
 keep={800,810,801,940,1200,1201};base['Entities']=[e for e in base['Entities'] if e['Entity'] in keep]
 hero=next(e for e in base['Entities'] if e['Entity']==800);hero['NativeScript']=script('Combat'+suffix+'.zgraph');hero['Transform']['Translation']=at(PLAYER,two)
 camera=next(e for e in base['Entities'] if e['Entity']==801)
 arena=next(e for e in base['Entities'] if e['Entity']==940)
 if two:camera['Transform']['Translation']=[-2.4,1,15];camera['Camera']['OrthographicSize']=10;arena['Transform'].update(Translation=[2,1,-.1],Scale=[12,11,1])
 else:camera['Transform']['Translation']=[-4,14,12];camera['Camera']['OrthographicSize']=13;arena['Transform'].update(Translation=[2,-.5,-1],Scale=[13,1,11])
 def visual(i,parent,tag,color,scale2=(.8,.8),scale3=(.8,1.8,.8)):
  e={'Entity':i,'Parent':parent,'Tag':tag,'Transform':transform([0,0,.05] if two else [0,scale3[1]/2,0],(scale2[0],scale2[1],1) if two else scale3)}
  e['SpriteRenderer' if two else 'MeshRenderer']={'Color':color} if two else {'Mesh':'Cube','Shader':'Unlit','Albedo':color,'CastShadows':False};return e
 def actor(i,tag,tags,xy):
  e={'Entity':i,'Tag':tag,'Tags':tags,'Transform':transform(at(xy,two))}
  if two:e.update({'Rigidbody2D':{'BodyType':'Static'},'BoxCollider2D':{'Size':[.4,.4]}})
  else:e.update({'Rigidbody3D':{'BodyType':'Static'},'BoxCollider3D':{'HalfExtents':[.4,.9,.4],'Offset':[0,.9,0]}})
  return e
 base['Entities']+=[actor(850,'Sentry',['Enemy'],SENTRY),visual(851,850,'Sentry visual',[.95,.3,.32,1],(.9,.9),(.9,1.8,.9))]
 for k,(i,xy) in enumerate(DUMMIES):base['Entities']+=[actor(i,f'Dummy{k+1}',['Dummy'],xy),visual(880+k+1,i,f'Dummy{k+1} visual',[.95,.66,.25,1],(.6,.6),(.6,1.4,.6))]
 base['Entities']+=[actor(871,'Hidden',['Dummy'],HIDDEN),visual(872,871,'Hidden visual',[.7,.5,.95,1],(.6,.6),(.6,1.4,.6))]
 wall={'Entity':870,'Tag':'Wall','Tags':['Wall']}
 if two:wall.update({'Transform':transform([WALL[0],WALL[1],0],(2.4,.3,1)),'SpriteRenderer':{'Color':[.45,.5,.6,1]},'Rigidbody2D':{'BodyType':'Static','Layer':WALL_LAYER},'BoxCollider2D':{'Size':[.5,.5]}})
 else:wall.update({'Transform':transform(at(WALL,False,.7),(2.4,1.4,.3)),'MeshRenderer':{'Mesh':'Cube','Shader':'Unlit','Albedo':[.45,.5,.6,1],'CastShadows':False},'Rigidbody3D':{'BodyType':'Static','Layer':WALL_LAYER},'BoxCollider3D':{'HalfExtents':[.5,.5,.5]}})
 base['Entities'].append(wall)
 template=next(e for e in base['Entities'] if e['Entity']==1201);template['Text']['Text']='JANG LABORATORIYASI  /  snaryadlar + effektlar + saqlash v4';template['Text']['Color']=[.28,.9,.78,1]
 def label(i,text,y,size=19,width=760,color=(.85,.91,1,1)):
  e=copy.deepcopy(template);e['Entity']=i;e['Tag']='Combat HUD';e['UITransform']['Position']=[28,-y];e['UITransform']['Size']=[width,30];e['UITransform']['FontSize']=size;e['Text']['Text']=text;e['Text']['Color']=list(color);return e
 hud=[label(1202,'Space Bolt | L Nayza | N Nova | M Davolash | R Shijoat | F5/F9 Saqlash/Yuklash | WASD',60,16,1170),label(1203,"SOG'LIQ",98),label(1204,'HUJUM',130),label(1205,"SENTRY SOG'LIQ",162),label(1207,'XP',214)]
 bar={'Entity':1206,'Parent':1200,'Tag':'Combat HUD','Transform':transform([0,0,0]),'UITransform':{'AnchorMin':[0,1],'AnchorMax':[0,1],'Pivot':[0,1],'Position':[28,-194],'Size':[360,7]},'UIProgressBar':{'Value':1,'Padding':0,'BackgroundColor':[.12,.16,.23,1],'FillColor':[.95,.3,.32,1]}}
 hud.append(bar)
 for k,(i,text) in enumerate([(1210,'ZAHAR x'),(1211,'YONISH x'),(1212,'MUZLASH'),(1213,'KARAXT'),(1214,'TIKLANISH'),(1215,'SHIJOAT')]):hud.append(label(i,text,258+k*30,18,360,(.98,.84,.45,1)))
 marker=label(1220,'',0,44,80,(1,.35,.3,1));marker['UITransform'].update({'AnchorMin':[.5,.5],'AnchorMax':[.5,.5],'Pivot':[.5,.5],'Position':[0,0],'Size':[80,60]});marker['Text']['AlignH']='Center';hud.append(marker)
 hud.append(label(1230,'Zarbalar',446,18,360,(.28,.9,.78,1)))
 for k in range(4):hud.append(label(1231+k,'',476+k*28,18,360,(1,.55,.5,1)))
 base['Entities']+=hud
 p=ASSETS/f'Scenes/Combat{suffix}.zscene';write(p,base);meta(p,3 if two else 4,'Scene')

 # ---- Visual-only projectile prefabs: no bodies, colliders, scripts or navigation (validated at define time).
 for k,(key,color,size2,size3) in enumerate([('Bolt',[1,.78,.25,1],(.42,.16),(.16,.16,.5)),('Lance',[.4,.92,1,1],(.7,.1),(.1,.1,.8)),('Venom',[.45,1,.35,1],(.3,.3),(.28,.28,.28))]):
  e={'Entity':1,'Tag':key+' visual','Transform':transform([0,0,0],(size2[0],size2[1],1) if two else size3)}
  e['SpriteRenderer' if two else 'MeshRenderer']={'Color':color} if two else {'Mesh':'Cube','Shader':'Unlit','Albedo':color,'CastShadows':False}
  p=ASSETS/f'Prefabs/{key}{suffix}.zprefab';write(p,{'ZaminEntities':1,'Entities':[e]});meta(p,(5 if two else 8)+k,'Prefab')

manifest=json.loads((ROOT/'Projects/LoadoutWorkshop/LoadoutWorkshop.zproject').read_text())
manifest['Project'].update(Name='CombatWorkshop',StartScene='Scenes/Combat2D.zscene',ScriptModulePath='',Description='Projectiles, area damage, timed status effects, combat HUD feedback and v4 persistent combat state.')
manifest['Player']['Product']='Zamin Combat Workshop'
manifest['InputActions']['Actions']=[{'Name':name,'Bindings':['Key:'+key,'Gamepad:'+pad]} for name,key,pad in actions+[('Jump','J','LeftBumper'),('Sprint','LeftShift','LeftThumb')]]
manifest['CollisionLayers2D']={'Names':['Default','Wall']+['']*14,'Collisions':[65535]*16}
write(OUT/'CombatWorkshop.zproject',manifest)
if not (OUT/'Thumbnail.png').exists():shutil.copyfile(ROOT/'Projects/LoadoutWorkshop/Thumbnail.png',OUT/'Thumbnail.png')
