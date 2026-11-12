#!/usr/bin/env python3
"""Rebuild the compiler-free 2D/3D combat courses from engine-owned sample assets."""
import copy
import json
from pathlib import Path
import shutil

REPO = Path(__file__).resolve().parents[2]
ROOT = REPO/'Projects/RulesWorkshop'
ASSETS = ROOT/'Assets'
for folder in ('Scripts', 'Scenes', 'Models'):
    (ASSETS/folder).mkdir(parents=True, exist_ok=True)


def write(path, data):
    path.write_text(json.dumps(data, indent=2)+'\n')


def meta(path, number, kind):
    write(Path(str(path)+'.zmeta'), {'UUID': f'a920ba937ef1{number:04x}', 'Type': kind, 'Import': {}})


class Graph:
    def __init__(self):
        self.data = {'Format': 'Zamin.VisualGraph', 'Version': 1, 'Nodes': [], 'Links': [], 'Variables': {}}
    def n(self, kind, **inputs):
        i = len(self.data['Nodes'])+1
        self.data['Nodes'].append({'ID': i, 'Kind': kind, 'X': (i-1)%7*290, 'Y': (i-1)//7*340, 'Inputs': inputs})
        return i
    def l(self, a, output, b, pin):
        if self.data['Nodes'][b-1]['Kind']=='Not' and pin=='Value':
            pin='A'
        self.data['Links'].append({'From': a, 'Output': output, 'To': b, 'Input': pin})
    def chain(self, *nodes):
        for a, b in zip(nodes, nodes[1:]):
            self.l(a, 'Then', b, 'In')
    def flag(self, name, value=True):
        return self.n('WriteState.Bool', Key='Rules.'+name, Value=value, Persistent=False)
    def read(self, name):
        return self.n('ReadState.Bool', Key='Rules.'+name)
    def save(self, name, number):
        path = ASSETS/'Scripts'/name
        write(path, self.data)
        meta(path, number, 'VisualGraph')


FLAGS = ('Died', 'Respawned', 'Key', 'Killed', 'Healed', 'Unlocked', 'Coin', 'Finished')


def player(two):
    g = Graph(); n = g.n; l = g.l; chain = g.chain
    g.data['Variables']['Auto'] = {'Type': 'Bool', 'Default': False}
    g.data['Variables']['Move'] = {'Type': 'Vec2', 'Default': [0, 0]}
    start = n('Event.Start'); configure = n('Gameplay.HealthConfigure', Maximum=100, Shield=25, Team=1, RespawnDelay=.3)
    bag = n('Gameplay.InventoryConfigure', Slots=8, Stack=10)
    capture = n('Gameplay.CheckpointCapture', Slot='RulesSpawn', Persistent=False)
    chain(start, configure, bag, capture)
    reset = [g.flag(key, False) for key in FLAGS]; chain(capture, *reset)
    update = n('Event.Update'); choose = n('Branch'); auto = n('Get.Bool', Name='Auto'); chain(update, choose); l(auto, 'Value', choose, 'Condition')
    raw = n('Gameplay.Input'); normal = n('Set.Vec2', Name='Move'); l(choose, 'False', normal, 'In'); l(raw, 'Move', normal, 'Value')
    replay = n('ReadState.Number', Key='Rules.AutoMove'); vector = n('MakeVec2'); l(replay, 'Value', vector, 'X')
    automated = n('Set.Vec2', Name='Move'); l(choose, 'True', automated, 'In'); l(vector, 'Value', automated, 'Value')
    motor = n('Gameplay.Motor2D' if two else 'Gameplay.Motor3D', Speed=4)
    move = n('Get.Vec2', Name='Move'); l(move, 'Value', motor, 'Move'); chain(normal, motor); chain(automated, motor)
    l(update, 'Delta', motor, 'Delta'); l(raw, 'Jump', motor, 'Jump'); l(raw, 'JumpHeld', motor, 'JumpHeld'); l(raw, 'Sprint', motor, 'Sprint')
    finished = g.read('Finished'); running = n('Not'); l(finished, 'Value', running, 'Value'); enabled = n('And'); l(raw, 'Enabled', enabled, 'A'); l(running, 'Value', enabled, 'B'); l(enabled, 'Value', motor, 'Enabled')
    bind = n('Gameplay.BindAnimator', Target=810, Source=800, TwoD=two); chain(motor, bind); l(update, 'Delta', bind, 'Delta')
    # A dead actor's motor is gated by the engine; respawn commits only after checkpoint restore succeeds.
    deadupdate = n('Event.Update'); dead = n('Gameplay.Health'); isdead = n('Branch'); chain(deadupdate, isdead); l(dead, 'Dead', isdead, 'Condition')
    respawn = n('Gameplay.Respawn', Slot='RulesSpawn', Persistent=False, Grace=.7); l(isdead, 'True', respawn, 'In')
    # Real input and deterministic editor messages share the same custom-event handlers.
    actions = n('Event.Update'); manual = n('Branch'); chain(actions, manual); inverse = n('Not'); l(auto, 'Value', inverse, 'Value'); l(inverse, 'Value', manual, 'Condition'); seq = n('Sequence'); l(manual, 'True', seq, 'In')
    for action, event, pin in [('Interact', 'Use', 'First'), ('Fire', 'Attack', 'Second')]:
        key = n('ActionJustPressed', Name=action); both = n('And'); l(key, 'Value', both, 'A'); l(raw, 'Enabled', both, 'B'); branch = n('Branch'); l(seq, pin, branch, 'In'); l(both, 'Value', branch, 'Condition'); emit = n('EmitEvent', Name=event); l(branch, 'True', emit, 'In')
    for event in ('Use', 'Attack'):
        message = n('Event.Message'); equal = n('TextEqual', B=event); l(message, 'Text', equal, 'A'); branch = n('Branch'); chain(message, branch); l(equal, 'Value', branch, 'Condition'); emit = n('EmitEvent', Name=event); l(branch, 'True', emit, 'In')
    use = n('Event.Custom', Name='Use'); find = n('Gameplay.InteractionFind', TwoD=two, Range=1.8); branch = n('Branch'); chain(use, branch); l(find, 'Found', branch, 'Condition')
    interact = n('Gameplay.Interact', TwoD=two, Range=1.8); l(branch, 'True', interact, 'In'); l(find, 'Object', interact, 'Target')
    success = n('Branch'); chain(interact, success); l(interact, 'Success', success, 'Condition'); seq = n('Sequence'); l(success, 'True', seq, 'In')
    for tag, flag, pin in [('Key', 'Key', 'First'), ('Door', 'Unlocked', 'Second'), ('Medkit', 'Healed', 'Third')]:
        tagged = n('HasTag', Tag=tag); l(interact, 'Object', tagged, 'Target'); branch = n('Branch'); l(seq, pin, branch, 'In'); l(tagged, 'Value', branch, 'Condition'); mark = g.flag(flag); l(branch, 'True', mark, 'In')
    attack = n('Event.Custom', Name='Attack'); hit = n('Gameplay.Attack', TwoD=two, Range=2.6, MinDot=.2, Amount=30, Cooldown=.25); chain(attack, hit)
    killed = n('Branch'); chain(hit, killed); l(hit, 'Killed', killed, 'Condition'); flag = g.flag('Killed'); l(killed, 'True', flag, 'In'); hide = n('SetActive', Active=False); chain(flag, hide); l(hit, 'Victim', hide, 'Target')
    # Drain one bounded rule event each update; canonical health state remains authoritative if telemetry overflows.
    eventupdate = n('Event.Update'); poll = n('Gameplay.RuleEvent'); chain(eventupdate, poll); seq = n('Sequence'); chain(poll, seq)
    for kind, flag, pin in [('Died', 'Died', 'First'), ('Respawned', 'Respawned', 'Second')]:
        equal = n('TextEqual', B=kind); l(poll, 'Kind', equal, 'A'); b = n('Branch'); l(seq, pin, b, 'In'); l(equal, 'Value', b, 'Condition'); mark = g.flag(flag); l(b, 'True', mark, 'In')
    # HUD reads actual health, shield and inventory every frame.
    hud = n('Event.LateUpdate'); current = n('NumberToText'); shield = n('NumberToText'); coins = n('Gameplay.InventoryCount', Item='Coin'); cointext = n('NumberToText')
    l(dead, 'Current', current, 'Number'); l(dead, 'Shield', shield, 'Number'); l(coins, 'Count', cointext, 'Number')
    last = n('Concat', A='HP '); l(current, 'Value', last, 'B')
    for text, source in [('  |  Shield ', shield), ('  |  Coins ', cointext)]:
        sep = n('Concat', B=text); l(last, 'Value', sep, 'A'); value = n('Concat'); l(sep, 'Value', value, 'A'); l(source, 'Value', value, 'B'); last = value
    sethud = n('SetUIText', Target=1003); chain(hud, sethud); l(last, 'Value', sethud, 'Text')
    promptevent = n('Event.Update'); has = n('Branch'); chain(promptevent, has); l(find, 'Found', has, 'Condition'); prompt = n('Concat', A='E - '); l(find, 'Prompt', prompt, 'B'); setprompt = n('SetUIText', Target=1002); l(has, 'True', setprompt, 'In'); l(prompt, 'Value', setprompt, 'Text')
    normalprompt = n('SetUIText', Target=1002, Text='Red pad: death/respawn | Key > guard > medkit > locked door > coin > finish'); l(has, 'False', normalprompt, 'In')
    g.save('Player2D.zgraph' if two else 'Player3D.zgraph', 1 if two else 2)


def enemy(two):
    g = Graph(); start = g.n('Event.Start'); health = g.n('Gameplay.HealthConfigure', Maximum=75, Team=2, HitGrace=.1); g.chain(start, health)
    update = g.n('Event.Update'); attack = g.n('Gameplay.Attack', Amount=35, Cooldown=.8, Range=2.1, TwoD=two, Origin=[0, 0, 0]); g.chain(update, attack)
    g.save('Enemy2D.zgraph' if two else 'Enemy3D.zgraph', 3 if two else 4)


def objects(two):
    for name, settings, number in [('Key', {'Prompt': 'Take golden key', 'RewardItem': 'Key'}, 5), ('Medkit', {'Prompt': 'Use medkit', 'Heal': 100}, 7), ('Door', {'Prompt': 'Unlock door (1 Key)', 'RequiredItem': 'Key', 'Consume': True}, 9), ('Coin', {'Prompt': 'Coin', 'RewardItem': 'Coin'}, 11)]:
        g = Graph(); start = g.n('Event.Start'); configure = g.n('Gameplay.InteractionConfigure', Deactivate=True, **settings); g.chain(start, configure)
        if name == 'Coin':
            trigger = g.n('Event.TriggerBegin'); tag = g.n('HasTag', Tag='Player'); g.l(trigger, 'Other', tag, 'Target'); branch = g.n('Branch'); g.chain(trigger, branch); g.l(tag, 'Value', branch, 'Condition'); use = g.n('Gameplay.Interact', TwoD=two, Range=2, LineOfSight=False); g.l(branch, 'True', use, 'In'); g.l(trigger, 'Other', use, 'Actor'); success = g.n('Branch'); g.chain(use, success); g.l(use, 'Success', success, 'Condition'); mark = g.flag('Coin'); g.l(success, 'True', mark, 'In')
        g.save(f'{name}{"2D" if two else "3D"}.zgraph', number+(0 if two else 1))
    g = Graph(); event = g.n('Event.TriggerBegin'); tag = g.n('HasTag', Tag='Player'); g.l(event, 'Other', tag, 'Target'); branch = g.n('Branch'); g.chain(event, branch); g.l(tag, 'Value', branch, 'Condition'); damage = g.n('Gameplay.Damage', Amount=500); g.l(branch, 'True', damage, 'In'); g.l(event, 'Other', damage, 'Target'); g.save('Hazard2D.zgraph' if two else 'Hazard3D.zgraph', 13 if two else 14)
    g = Graph(); event = g.n('Event.TriggerBegin'); tag = g.n('HasTag', Tag='Player'); g.l(event, 'Other', tag, 'Target'); hascoin = g.n('Gameplay.InventoryCount', Target=800, Item='Coin'); enough = g.n('Greater', B=0); g.l(hascoin, 'Count', enough, 'A'); both = g.n('And'); g.l(tag, 'Value', both, 'A'); g.l(enough, 'Value', both, 'B'); branch = g.n('Branch'); g.chain(event, branch); g.l(both, 'Value', branch, 'Condition'); mark = g.flag('Finished'); g.l(branch, 'True', mark, 'In'); text = g.n('SetUIText', Target=1004, Text='COURSE COMPLETE - combat, inventory, locked door and respawn verified'); g.chain(mark, text); g.save('Goal2D.zgraph' if two else 'Goal3D.zgraph', 15 if two else 16)


def director():
    g = Graph(); n = g.n; l = g.l
    g.data['Variables'] = {'Auto': {'Type': 'Bool', 'Default': False}, 'Checks': {'Type': 'Bool', 'Default': False}}
    start = n('Event.Start'); auto = n('Get.Bool', Name='Auto'); gate = n('Branch'); g.chain(start, gate); l(auto, 'Value', gate, 'Condition'); wait = n('Delay', Seconds=.5); l(gate, 'True', wait, 'In'); last = wait
    stages = [('Move', -1, .5), ('Move', 0, .5), ('Move', 1, .55), ('Use', 0, .23), ('Move', 0, .13), ('Attack', 0, .4), ('Attack', 0, .4), ('Attack', 0, .2), ('Use', 0, .25), ('Move', 1, .95), ('Use', 0, 1.3), ('Move', 0, .1)]
    for action, value, delay in stages:
        node = n('WriteState.Number', Key='Rules.AutoMove', Value=value, Persistent=False) if action == 'Move' else n('SendMessage', Target=800, Name=action)
        wait = n('Delay', Seconds=delay); g.chain(last, node, wait); last = wait
    observe = n('Event.Start'); checks = n('Get.Bool', Name='Checks'); gate = n('Branch'); g.chain(observe, gate); l(checks, 'Value', gate, 'Condition'); wait = n('Delay', Seconds=6.8); l(gate, 'True', wait, 'In')
    conditions = [g.read(key) for key in FLAGS]
    hero = n('Gameplay.Health', Target=800); alive = n('Not'); l(hero, 'Dead', alive, 'Value'); conditions.append(alive)
    guard = n('Gameplay.Health', Target=880); isdead = n('Not'); # Double Not adapts the named Dead output into a Value.
    l(guard, 'Dead', isdead, 'Value'); deadvalue = n('Not'); l(isdead, 'Value', deadvalue, 'Value'); conditions.append(deadvalue)
    for item, count in [('Key', 0), ('Coin', 1)]:
        bag = n('Gameplay.InventoryCount', Target=800, Item=item); equal = n('Equal', B=count); l(bag, 'Count', equal, 'A'); conditions.append(equal)
    condition = conditions[0]
    for node in conditions[1:]:
        both = n('And'); l(condition, 'Value', both, 'A'); l(node, 'Value', both, 'B'); condition = both
    b = n('Branch'); g.chain(wait, b); l(condition, 'Value', b, 'Condition'); ok = n('Print', Text='RULES_ACCEPTANCE death=1 respawn=1 key=1 kill=1 heal=1 unlock=1 coin=1 finish=1'); fail = n('Print', Text='RULES_ORACLE_FAILED'); l(b, 'True', ok, 'In'); l(b, 'False', fail, 'In')
    # Independent observer tasks explain missing flags without blocking on replay delays.
    for key in FLAGS:
        event = n('Event.Start'); delay = n('Delay', Seconds=6.85); g.chain(event, delay); check = n('Branch'); g.chain(delay, check); value = g.read(key); missing = n('Print', Text='RULES_MISSING_'+key.upper()); enabled = n('And'); inverse = n('Not'); l(value, 'Value', inverse, 'Value'); l(inverse, 'Value', enabled, 'A'); l(checks, 'Value', enabled, 'B'); l(enabled, 'Value', check, 'Condition'); l(check, 'True', missing, 'In')
    g.save('RulesDirector.zgraph', 17)


def script(name):
    return {'Class': 'Zamin.VisualGraph', 'Fields': {'Graph': {'Type': 'String', 'Value': 'Scripts/'+name}}}


for two in (True, False):
    player(two); enemy(two); objects(two)
    suffix = '2D' if two else '3D'
    base = json.loads((REPO/f'Projects/GameplayWorkshop/Assets/Scenes/Course{suffix}.zscene').read_text())
    base['Scene'] = 'Rules '+suffix
    # Keep the existing player, avatar, camera, stage, sun and HUD infrastructure.
    base['Entities'] = [e for e in base['Entities'] if e['Entity'] in (800, 810, 801, 802, 807, 1000, 1001, 1002)]
    actor = next(e for e in base['Entities'] if e['Entity']==800); actor['NativeScript'] = script(f'Player{suffix}.zgraph')
    camera = next(e for e in base['Entities'] if e['Entity']==801); camera['Camera']['OrthographicSize'] = 9
    for i, text in [(1001, 'WASD move / Space jump / Ctrl or click attack / E interact'), (1002, 'First touch the red pad to test death and checkpoint respawn')]:
        next(e for e in base['Entities'] if e['Entity']==i)['Text']['Text'] = text
    for i, y, text in [(1003, -100, 'HP 100 | Shield 25 | Coins 0'), (1004, -136, 'Key > guard > medkit > locked door > coin > finish')]:
        e = copy.deepcopy(next(e for e in base['Entities'] if e['Entity']==1002)); e['Entity'] = i; e['Tag'] = 'Vitals' if i==1003 else 'Objective'; e['UITransform']['Position'][1] = y; e['Text']['Text'] = text; base['Entities'].append(e)
    original = json.loads((REPO/f'Projects/GameplayWorkshop/Assets/Scenes/Course{suffix}.zscene').read_text())
    template = next(e for e in original['Entities'] if e['Entity']==804)
    for i, name, pos, scale, color, sensor in [
        (804, 'Key', [1.8,1,0], [.35,1,1], [1,.75,.18,1], True),
        (805, 'Door', [6,1.5,0], [.5,3,6 if not two else 1], [.25,.65,1,1], False),
        (881, 'Medkit', [3.2,.5,1.2 if not two else 0], [.3,.6,.3], [.8,1,.75,1], True),
        (882, 'Coin', [7.5,1,0], [.35,1,1], [1,.8,.15,1], True),
        (883, 'Hazard', [-1.3,1,0], [.5,2,4 if not two else 1], [1,.12,.2,1], True),
        (884, 'Goal', [9,1.5,0], [.5,3,6 if not two else 1], [.15,.9,.5,1], True),
        (880, 'Enemy', [4,.9,0], [.8,1.8,.8], [.95,.25,.22,1], False)]:
        e = copy.deepcopy(template);e['Entity']=i;e['Tag']=name;e['Tags']=[name];e['Transform']['Translation']=pos;e['Transform']['Scale']=scale
        e['NativeScript']=script(f'{name}{suffix}.zgraph')
        e['BoxCollider2D' if two else 'BoxCollider3D']['IsSensor']=sensor
        if two:e['SpriteRenderer']={'Color':color}
        else:e['MeshRenderer']={'Mesh':'Cube','Shader':'Lit','Albedo':color,'Roughness':.5}
        base['Entities'].append(e)
    base['Entities'].append({'Entity':900,'Tag':'Rules director','Transform':{'Translation':[0,0,0],'Rotation':[0,0,0],'Scale':[1,1,1]},'NativeScript':script('RulesDirector.zgraph')})
    scene = ASSETS/f'Scenes/Rules{suffix}.zscene';write(scene,base);meta(scene,18 if two else 19,'Scene')
    camera = ASSETS/f'Scripts/Camera{suffix}.zgraph';data = json.loads((REPO/f'Projects/GameplayWorkshop/Assets/Scripts/Camera{suffix}.zgraph').read_text());write(camera,data);meta(camera,20 if two else 21,'VisualGraph')

director()
model=ASSETS/'Models/Runner.gltf';shutil.copyfile(REPO/'Projects/GameplayWorkshop/Assets/Models/Runner.gltf',model);meta(model,22,'Model')
if not (ROOT/'Thumbnail.png').exists():
    shutil.copyfile(REPO/'Projects/GameplayWorkshop/Thumbnail.png',ROOT/'Thumbnail.png')
manifest=json.loads((REPO/'Projects/GameplayWorkshop/GameplayWorkshop.zproject').read_text());manifest['Project'].update(Name='RulesWorkshop',StartScene='Scenes/Rules2D.zscene',Description='Compiler-free health, shield, death/respawn, melee, inventory and locked-door courses.');manifest['Player']['Product']='Zamin Rules Workshop';write(ROOT/'RulesWorkshop.zproject',manifest)
