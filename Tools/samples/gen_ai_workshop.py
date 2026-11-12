#!/usr/bin/env python3
"""Generate the engine-owned, compiler-free AI patrol/combat courses and NPC prefabs."""
import copy
import json
import math
from pathlib import Path
import shutil

REPO = Path(__file__).resolve().parents[2]
ROOT = REPO/'Projects/AIWorkshop'
ASSETS = ROOT/'Assets'
for folder in ('Scripts', 'Scenes', 'Prefabs'):
    (ASSETS/folder).mkdir(parents=True, exist_ok=True)


def write(path, data):
    path.write_text(json.dumps(data, indent=2)+'\n')


def meta(path, number, kind):
    write(Path(str(path)+'.zmeta'), {'UUID': f'a930ba937ef1{number:04x}', 'Type': kind, 'Import': {}})


class Graph:
    def __init__(self):
        self.data = {'Format': 'Zamin.VisualGraph', 'Version': 1, 'Nodes': [], 'Links': [], 'Variables': {}}
    def n(self, kind, **inputs):
        i = len(self.data['Nodes'])+1
        self.data['Nodes'].append({'ID': i, 'Kind': kind, 'X': (i-1)%7*290, 'Y': (i-1)//7*340, 'Inputs': inputs})
        return i
    def l(self, a, output, b, pin):
        self.data['Links'].append({'From': a, 'Output': output, 'To': b, 'Input': pin})
    def chain(self, *nodes):
        for a, b in zip(nodes, nodes[1:]):
            self.l(a, 'Then', b, 'In')
    def flag(self, key, value=True):
        return self.n('WriteState.Bool', Key='AI.'+key, Value=value, Persistent=False)
    def read(self, key):
        return self.n('ReadState.Bool', Key='AI.'+key)
    def save(self, name, number):
        path = ASSETS/'Scripts'/name
        write(path, self.data); meta(path, number, 'VisualGraph')


def script(name):
    return {'Class': 'Zamin.VisualGraph', 'Fields': {'Graph': {'Type': 'String', 'Value': 'Scripts/'+name}}}


def transform(pos, scale=(1, 1, 1), rotation=(0, 0, 0)):
    return {'Translation': list(pos), 'Rotation': list(rotation), 'Scale': list(scale)}


FLAGS = ('Patrol', 'Chase', 'Attack', 'Investigate', 'Return', 'Dead', 'Spawn')


def player(two):
    g = Graph(); n = g.n; l = g.l; chain = g.chain
    g.data['Variables'] = {'Auto': {'Type': 'Bool', 'Default': False}, 'Move': {'Type': 'Vec2', 'Default': [0, 0]}}
    start = n('Event.Start'); health = n('Gameplay.HealthConfigure', Maximum=1000, HitGrace=0, Team=1); chain(start, health)
    reset = [g.flag(key, False) for key in FLAGS+('Open',)]; chain(health, *reset)
    update = n('Event.Update'); auto = n('Get.Bool', Name='Auto'); branch = n('Branch'); chain(update, branch); l(auto, 'Value', branch, 'Condition')
    raw = n('Gameplay.Input'); normal = n('Set.Vec2', Name='Move'); l(branch, 'False', normal, 'In'); l(raw, 'Move', normal, 'Value')
    command = n('ReadState.Number', Key='AI.AutoMove'); vec = n('MakeVec2'); l(command, 'Value', vec, 'X'); replay = n('Set.Vec2', Name='Move'); l(branch, 'True', replay, 'In'); l(vec, 'Value', replay, 'Value')
    motor = n('Gameplay.Motor2D' if two else 'Gameplay.Motor3D', **({'TopDown': True} if two else {}), Speed=4)
    move = n('Get.Vec2', Name='Move'); l(move, 'Value', motor, 'Move'); l(update, 'Delta', motor, 'Delta'); l(raw, 'Enabled', motor, 'Enabled'); chain(normal, motor); chain(replay, motor)
    actions = n('Event.Update'); manual = n('Not'); l(auto, 'Value', manual, 'A'); b = n('Branch'); chain(actions, b); l(manual, 'Value', b, 'Condition'); seq = n('Sequence'); l(b, 'True', seq, 'In')
    for action, event, pin in [('Interact', 'Use', 'First'), ('Fire', 'Shoot', 'Second')]:
        pressed = n('ActionJustPressed', Name=action); enabled = n('And'); l(pressed, 'Value', enabled, 'A'); l(raw, 'Enabled', enabled, 'B'); check = n('Branch'); l(seq, pin, check, 'In'); l(enabled, 'Value', check, 'Condition'); emit = n('EmitEvent', Name=event); l(check, 'True', emit, 'In')
    for name in ('Use', 'Shoot'):
        message = n('Event.Message'); equal = n('TextEqual', B=name); l(message, 'Text', equal, 'A'); b = n('Branch'); chain(message, b); l(equal, 'Value', b, 'Condition'); emit = n('EmitEvent', Name=name); l(b, 'True', emit, 'In')
    use = n('Event.Custom', Name='Use'); current = g.read('Open'); inverse = n('Not'); l(current, 'Value', inverse, 'A'); setopen = g.flag('Open'); l(inverse, 'Value', setopen, 'Value'); chain(use, setopen)
    active = n('Not'); l(current, 'Value', active, 'A'); first = n('SetActive', Target=901); second = n('SetActive', Target=902); chain(setopen, first, second); l(active, 'Value', first, 'Active'); l(active, 'Value', second, 'Active')
    shoot = n('Event.Custom', Name='Shoot'); strike = n('Gameplay.Strike', Victim=880, Range=20, TwoD=two, Origin=[0, 0, 0] if two else [0, 1, 0], Facing=[-1, 0, 0], Amount=1000); chain(shoot, strike)
    # The UI reads native state, not the acceptance flags.
    late = n('Event.LateUpdate'); state = n('Gameplay.AIState', Target=880); hp = n('Gameplay.Health'); number = n('NumberToText'); l(hp, 'Current', number, 'Number')
    label = n('Concat', A='Sentry: '); l(state, 'Mode', label, 'B'); sep = n('Concat', B='  |  '); l(label, 'Value', sep, 'A'); status = n('Concat'); l(sep, 'Value', status, 'A'); l(state, 'Status', status, 'B'); text = n('SetUIText', Target=1002); chain(late, text); l(status, 'Value', text, 'Text')
    hptext = n('Concat', A='Player HP '); l(number, 'Value', hptext, 'B'); vitals = n('SetUIText', Target=1003); chain(text, vitals); l(hptext, 'Value', vitals, 'Text')
    g.save('Player2D.zgraph' if two else 'Player3D.zgraph', 1 if two else 2)


def brain(two):
    g = Graph(); start = g.n('Event.Start'); hp = g.n('Gameplay.HealthConfigure', Maximum=60, Team=2, HitGrace=.05); ai = g.n('Gameplay.AIConfigure', TwoD=two, Points=[[0, -1.5, 0], [0, 1.5, 0]] if two else [[0, 0, -1.5], [0, 0, 1.5]], FieldOfView=360, View=6, Lose=7, Near=1, Memory=.6, Leash=9, Speed=3, Wait=.15, AttackEnter=1.1, AttackExit=1.3, Damage=5, AttackCooldown=.5, Stall=.5, RecoveryCooldown=.2, Retries=4, Eye=[0, 0, 0] if two else [0, 1, 0]); g.chain(start, hp, ai)
    check = g.n('Branch'); g.chain(ai, check); g.l(ai, 'Success', check, 'Condition'); fail = g.n('Print', Text='AI_CONFIG_FAILED'); g.l(check, 'False', fail, 'In')
    g.save('Brain2D.zgraph' if two else 'Brain3D.zgraph', 3 if two else 4)


def director(two):
    g = Graph(); n = g.n; l = g.l; chain = g.chain
    g.data['Variables'] = {'Auto': {'Type': 'Bool', 'Default': False}, 'Checks': {'Type': 'Bool', 'Default': False}, 'SpawnIssued': {'Type': 'Bool', 'Default': False}, 'Reinforcement': {'Type': 'Entity', 'Default': 0}}
    start = n('Event.Start'); auto = n('Get.Bool', Name='Auto'); b = n('Branch'); chain(start, b); l(auto, 'Value', b, 'Condition'); wait = n('Delay', Seconds=65/30); l(b, 'True', wait, 'In'); last = wait
    for action, value, delay in [('Use', 0, 65/30), ('Move', 1, 25/30), ('Use', 0, 35/30), ('Move', 0, 10/30), ('Use', 0, 100/30), ('Shoot', 0, .1)]:
        command = n('WriteState.Number', Key='AI.AutoMove', Value=value, Persistent=False) if action == 'Move' else n('SendMessage', Target=800, Name=action)
        wait = n('Delay', Seconds=delay); chain(last, command, wait); last = wait
    state = n('Gameplay.AIState', Target=880)
    for mode, flag in [('Patrol', 'Patrol'), ('Chase', 'Chase'), ('Investigate', 'Investigate'), ('Dead', 'Dead')]:
        update = n('Event.Update'); equal = n('TextEqual', B=mode); l(state, 'Mode', equal, 'A'); check = n('Branch'); chain(update, check); l(equal, 'Value', check, 'Condition'); mark = g.flag(flag); l(check, 'True', mark, 'In')
    for output, flag in [('Attacks', 'Attack'), ('Returns', 'Return')]:
        update = n('Event.Update'); greater = n('Greater', B=0); l(state, output, greater, 'A'); check = n('Branch'); chain(update, check); l(greater, 'Value', check, 'Condition'); mark = g.flag(flag); l(check, 'True', mark, 'In')
    # Spawn an authored NPC prefab after the first guard dies. Its own Event.Start configures health and AI.
    update = n('Event.Update'); dead = g.read('Dead'); issued = n('Get.Bool', Name='SpawnIssued'); inverse = n('Not'); l(issued, 'Value', inverse, 'A'); both = n('And'); l(dead, 'Value', both, 'A'); l(inverse, 'Value', both, 'B'); check = n('Branch'); chain(update, check); l(both, 'Value', check, 'Condition'); mark = n('Set.Bool', Name='SpawnIssued', Value=True); l(check, 'True', mark, 'In'); delay = n('Delay', Seconds=.4); spawn = n('SpawnPrefab', Path='Prefabs/Reinforcement2D.zprefab' if two else 'Prefabs/Reinforcement3D.zprefab', Position=[-5, -3, 0] if two else [-5, 0, -3]); stored = n('Set.Entity', Name='Reinforcement'); chain(mark, delay, spawn, stored); l(spawn, 'Value', stored, 'Value')
    entity = n('Get.Entity', Name='Reinforcement'); spawned = n('Gameplay.AIState'); l(entity, 'Value', spawned, 'Target'); arrived = n('Greater', B=0); l(spawned, 'Arrivals', arrived, 'A'); configured = n('And'); l(spawned, 'Configured', configured, 'A'); l(arrived, 'Value', configured, 'B'); valid = n('EntityValid'); l(entity, 'Value', valid, 'Target'); safe = n('And'); l(valid, 'Value', safe, 'A'); l(configured, 'Value', safe, 'B'); configured = safe; update = n('Event.Update'); b = n('Branch'); chain(update, b); l(configured, 'Value', b, 'Condition'); done = g.flag('Spawn'); l(b, 'True', done, 'In')
    # A separate observer task cannot be delayed by the replay sequence.
    event = n('Event.Start'); checks = n('Get.Bool', Name='Checks'); b = n('Branch'); chain(event, b); l(checks, 'Value', b, 'Condition'); wait = n('Delay', Seconds=13); l(b, 'True', wait, 'In')
    terms = [g.read(flag) for flag in FLAGS]
    hero = n('Gameplay.Health', Target=800); alive = n('Greater', B=0); l(hero, 'Current', alive, 'A'); terms.append(alive); hurt = n('Less', B=1000); l(hero, 'Current', hurt, 'A'); terms.append(hurt)
    guard = n('Gameplay.Health', Target=880); isdead = n('Not'); l(guard, 'Dead', isdead, 'A'); deadvalue = n('Not'); l(isdead, 'Value', deadvalue, 'A'); terms.append(deadvalue); terms.append(configured)
    condition = terms[0]
    for term in terms[1:]:
        both = n('And'); l(condition, 'Value', both, 'A'); l(term, 'Value', both, 'B'); condition = both
    passed = n('Branch'); chain(wait, passed); l(condition, 'Value', passed, 'Condition'); ok = n('Print', Text='AI_ACCEPTANCE patrol=1 chase=1 attack=1 investigate=1 return=1 dead=1 spawn=1'); fail = n('Print', Text='AI_ORACLE_FAILED'); l(passed, 'True', ok, 'In'); l(passed, 'False', fail, 'In'); text = n('SetUIText', Target=1004, Text='AI COURSE COMPLETE - patrol / LOS / combat / memory / return / prefab spawn'); chain(ok, text)
    for flag in FLAGS:
        event = n('Event.Start'); wait = n('Delay', Seconds=13.05); chain(event, wait); value = g.read(flag); inverse = n('Not'); l(value, 'Value', inverse, 'A'); condition = n('And'); l(inverse, 'Value', condition, 'A'); l(checks, 'Value', condition, 'B'); b = n('Branch'); chain(wait, b); l(condition, 'Value', b, 'Condition'); log = n('Print', Text='AI_MISSING_'+flag.upper()); l(b, 'True', log, 'In')
    g.save('Director2D.zgraph' if two else 'Director3D.zgraph', 5 if two else 6)


def visual(i, name, pos, scale, color, two, parent=0):
    e = {'Entity': i, 'Tag': name, 'Transform': transform(pos, scale)}
    if parent:
        e['Parent'] = parent
    e['SpriteRenderer' if two else 'MeshRenderer'] = {'Color': color} if two else {'Mesh': 'Cube', 'Shader': 'Unlit', 'Albedo': color, 'CastShadows': False}
    return e


for two in (True, False):
    player(two); brain(two); director(two)
    suffix = '2D' if two else '3D'
    hero = {'Entity': 800, 'Tag': 'Player', 'Tags': ['Player'], 'Transform': transform([0, 0, 0]), 'NativeScript': script(f'Player{suffix}.zgraph')}
    sentry = {'Entity': 880, 'Tag': 'Sentry', 'Tags': ['Enemy'], 'Transform': transform([-5, 0, 0]), 'NativeScript': script(f'Brain{suffix}.zgraph'), 'NavAgent': {'Grid': 950, 'Speed': 3, 'Radius': .3, 'StoppingDistance': .1, 'RepathInterval': .5, 'UpdateRotation': True, 'Crowd': {'Enabled': True}}}
    for actor in (hero, sentry):
        if two:
            actor.update(Rigidbody2D={'BodyType': 'Dynamic', 'GravityScale': 0, 'FixedRotation': True}, BoxCollider2D={'Size': [.35, .35], 'Friction': 0})
        else:
            actor['CharacterController3D'] = {'Radius': .35, 'Height': 1.8, 'StepHeight': .25}
    entities = [hero, sentry, visual(810, 'Player visual', [0, 0, .05] if two else [0, .9, 0], [.7, .7, 1] if two else [.7, 1.8, .7], [.22, .9, .65, 1], two, 800), visual(881, 'Sentry visual', [0, 0, .05] if two else [0, .9, 0], [.7, .7, 1] if two else [.7, 1.8, .7], [.68, .35, 1, 1], two, 880)]
    camera = {'Entity': 801, 'Tag': 'Overview camera', 'Transform': transform([0, 0, 15] if two else [0, 14, 13], rotation=[0, 0, 0] if two else [-math.atan2(14, 13), 0, 0]), 'Camera': {'Primary': True, 'ProjectionType': 1, 'OrthographicSize': 14 if two else 18, 'OrthographicNear': -100, 'OrthographicFar': 100, 'ClearColor': [.035, .045, .075, 1]}}
    entities.append(camera)
    if two:
        entities.append(visual(940, 'Arena', [0, 0, -.1], [20, 12, 1], [.075, .105, .16, 1], True))
        entities.append({'Entity': 950, 'Tag': 'Navigation grid', 'Transform': transform([0, 0, 0]), 'NavGrid2D': {'AutoBounds': False, 'Size': [20, 12], 'CellSize': .4, 'UseTilemaps': False, 'UseColliders': True, 'AgentRadius': .35}})
    else:
        floor = visual(940, 'Arena', [0, -.5, 0], [24, 1, 14], [.075, .105, .16, 1], False); floor.update(Rigidbody3D={'BodyType': 'Static'}, BoxCollider3D={'HalfExtents': [.5, .5, .5]}); entities.append(floor)
        entities.append({'Entity': 950, 'Tag': 'Navigation grid', 'Transform': transform([0, 1, 0]), 'NavGrid3D': {'Size': [20, 4, 12], 'CellSize': .4, 'AgentRadius': .35, 'AgentHeight': 1.8}})
    for i, x in [(901, -2.5), (902, 1.5)]:
        e = visual(i, 'LOS shutter', [x, 0, .02] if two else [x, 1.5, 0], [.4, 12, 1] if two else [.4, 3, 12], [.25, .55, .85, 1], two)
        e['Rigidbody2D' if two else 'Rigidbody3D'] = {'BodyType': 'Kinematic'}
        e['BoxCollider2D' if two else 'BoxCollider3D'] = {'Size': [.5, .5]} if two else {'HalfExtents': [.5, .5, .5]}
        e['NavObstacle'] = {'Size': [1, 1, 1], 'Grid': 950, 'CarveOnlyStationary': False}
        entities.append(e)
    # Patrol markers are visual aids, not colliders or navigation obstacles.
    for i, side in enumerate((-1.5, 1.5)):
        entities.append(visual(960+i, 'Patrol waypoint', [-5, side, .01] if two else [-5, .03, side], [.3, .3, 1] if two else [.3, .06, .3], [.2, .8, 1, 1], two))
    entities.append({'Entity': 900, 'Tag': 'AI director', 'Transform': transform([0, 0, 0]), 'NativeScript': script(f'Director{suffix}.zgraph')})
    entities.append({'Entity': 1000, 'Tag': 'HUD', 'Transform': transform([0, 0, 0]), 'UICanvas': {'ScaleMode': 'ScaleWithScreenSize', 'ReferenceResolution': [1280, 720]}})
    for i, y, text in [(1001, -24, 'AI LAB  |  WASD move  |  E shutters  |  Ctrl / click shoot guard'), (1002, -62, 'Sentry: Patrol - shutters block vision and navigation'), (1003, -100, 'Player HP 1000'), (1004, -138, 'Open > approach > escape > close > investigate/return > reopen > shoot > reinforcement')]:
        entities.append({'Entity': i, 'Parent': 1000, 'Tag': 'HUD text', 'Transform': transform([0, 0, 0]), 'UITransform': {'AnchorMin': [0, 1], 'AnchorMax': [0, 1], 'Pivot': [0, 1], 'Position': [24, y], 'Size': [1230, 40], 'FontSize': 20}, 'Text': {'Text': text, 'Color': [.85, .92, 1, 1], 'AlignH': 'Left', 'AlignV': 'Top'}})
    path = ASSETS/f'Scenes/AI{suffix}.zscene'; write(path, {'Version': 5, 'Scene': 'AI '+suffix, 'Settings': {'DrawSky': False, 'Ambient': .7}, 'Entities': entities}); meta(path, 7 if two else 8, 'Scene')
    root = copy.deepcopy(sentry); root['Entity'] = 1; root['Tag'] = 'Reinforcement'; root['Transform'] = transform([0, 0, 0]); root['NavAgent']['Grid'] = 0
    child = copy.deepcopy(entities[3]); child['Entity'] = 2; child['Parent'] = 1; child['Tag'] = 'Reinforcement visual'
    prefab = ASSETS/f'Prefabs/Reinforcement{suffix}.zprefab'; write(prefab, {'ZaminEntities': 1, 'Entities': [root, child]}); meta(prefab, 9 if two else 10, 'Prefab')
manifest = json.loads((REPO/'Projects/RulesWorkshop/RulesWorkshop.zproject').read_text()); manifest['Project'].update(Name='AIWorkshop', StartScene='Scenes/AI2D.zscene', Description='Compiler-free AI patrol, LOS, chase/attack, memory, return and prefab spawn lab.'); manifest['Player']['Product'] = 'Zamin AI Workshop'; write(ROOT/'AIWorkshop.zproject', manifest)
if not (ROOT/'Thumbnail.png').exists():
    shutil.copyfile(REPO/'Projects/RulesWorkshop/Thumbnail.png', ROOT/'Thumbnail.png')
