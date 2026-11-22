"""Scenes, prefabs and Visual Script graphs of Chiroqbon (gen_lantern_keeper.py).

UUID ranges are fixed and identical in every scene so shared graphs target literals: 1000-1099 hero and camera,
1100-1199 controllers and audio, 2000-4999 level geometry and props, 5000-5499 enemies, 5500-5999 interactables,
6000-7999 UI, 8000-8999 lights and effects.
"""
import json
import math
import os

import samplekit as k
import graphkit as gk

HERO, HERO_VISUAL, HERO_LIGHT, CAMERA = 1000, 1001, 1002, 1003
GAME, MUSIC, AMBIENCE, BOSS_MUSIC = 1100, 1101, 1102, 1104
CANVAS = 6000
GS = "LK"
WALL_MASK = 2
ACCENT = (0.98, 0.76, 0.32, 1)
INK = (0.94, 0.92, 0.86, 1)
MUTED = (0.72, 0.68, 0.80, 1)


def scene_uuid(project, name):
    return k.stable_uuid(f"{project}/Scenes/{name}.zscene")


def T(pos=(0, 0, 0), scale=(1, 1, 1), rot=(0, 0, 0)):
    return {"Translation": k.v(*pos), "Rotation": k.v(*rot), "Scale": k.v(*scale)}


class Scene:
    def __init__(self, name, settings):
        self.name = name
        self.settings = settings
        self.entities = {}
        self.children = {}
        self.roots = []

    def add(self, eid, tag, parent=None, pos=(0, 0, 0), scale=(1, 1, 1), active=True, tags=None, **components):
        assert eid not in self.entities, eid
        e = {"Entity": eid, "Tag": tag, "Transform": T(pos, scale)}
        if not active:
            e["Active"] = False
        if parent is not None:
            e["Parent"] = parent
            self.children.setdefault(parent, []).append(eid)
        else:
            self.roots.append(eid)
        if tags:
            e["Tags"] = list(tags)
        e.update(components)
        self.entities[eid] = e
        return eid

    def save(self, assets, project):
        ents = []
        for eid in sorted(self.entities):
            e = dict(self.entities[eid])
            if eid in self.children:
                e["ChildOrder"] = self.children[eid]
            ents.append(e)
        data = {"Scene": self.name, "Version": 5, "Settings": self.settings, "RootOrder": self.roots, "Entities": ents}
        rel = f"Scenes/{self.name}.zscene"
        k.write_text(os.path.join(assets, rel), k.dumps_compact(data) + "\n")
        k.write_zmeta(assets, rel, "Scene", {}, project)


def graph_script(name, **overrides):
    s = {"Class": "Zamin.VisualGraph", "Fields": {"Graph": {"Type": "String", "Value": f"Scripts/{name}.zgraph"}}}
    if overrides:
        s["Fields"]["Overrides"] = {"Type": "String", "Value": json.dumps(overrides, separators=(",", ":"), sort_keys=True)}
    return s


# ------------------------------------------------------------------------------------------------ UI helpers


class UI:
    def __init__(self, scene, target):
        self.s = scene
        self.target = target

    def rect(self, eid, tag, parent, anchor=(0.5, 0.5), pivot=None, pos=(0, 0), size=(100, 40), font=0, order=0, active=True, **c):
        return self.s.add(eid, tag, parent, active=active, UITransform=k.ui_rect(anchor, None, pivot or anchor, pos, size, font, order), **c)

    def stretch(self, eid, tag, parent, order=0, active=True, **c):
        return self.s.add(eid, tag, parent, active=active, UITransform=k.ui_stretch(order=order), **c)

    def text(self, eid, tag, parent, txt, anchor=(0.5, 0.5), pos=(0, 0), size=(400, 40), font=28, col=INK, h="Center", key=None, active=True, hints=False, pivot=None, wrap=False, **c):
        return self.rect(eid, tag, parent, anchor, pivot, pos, size, font, active=active, Text=k.ui_text(txt, col, h, key=key, hints=hints, wrap=wrap), **c)

    def image(self, eid, tag, parent, frame, anchor=(0.5, 0.5), pos=(0, 0), size=(64, 64), col=(1, 1, 1, 1), kind="Simple", order=0, active=True, pivot=None, **c):
        return self.rect(eid, tag, parent, anchor, pivot, pos, size, order=order, active=active,
                         UIImage=k.ui_image(col, "Textures/ui.png", frame, kind, 1.0 if kind == "Sliced" else None, raycast=False), **c)

    def panel(self, eid, tag, parent, pos, size, order=0, active=True, anchor=(0.5, 0.5), pivot=None):
        return self.rect(eid, tag, parent, anchor, pivot, pos, size, order=order, active=active,
                         UIImage=k.ui_image((1, 1, 1, 0.97), "Textures/ui.png", "panel", "Sliced", 1.0, raycast=True))

    def button(self, eid, tag, parent, label, key, message, pos, size=(420, 64), font=30, nav=None, select=False):
        e = self.rect(eid, tag, parent, pos=pos, size=size, font=font,
                      UIButton=k.ui_button((0.36, 0.28, 0.48, 1), (0.62, 0.46, 0.80, 1), (0.98, 0.76, 0.32, 1), message, self.target),
                      UIImage=k.ui_image((1, 1, 1, 1), "Textures/ui.png", "button", "Sliced", 1.0),
                      Text=k.ui_text(label, INK, "Center", key=key), UINavigation=k.ui_nav("Explicit" if nav else "Vertical", select, ACCENT[:3] + (1,)),
                      UIAnimation=k.ui_anim(hover=1.04, press=0.96))
        if nav:
            self.s.entities[e]["UINavigation"].update(Up=nav[0], Down=nav[1], Left=nav[2], Right=nav[3])
        return e

    def bar(self, eid, tag, parent, anchor, pos, size, fill, font=20, txt="", order=0, pivot=None, back=(0.05, 0.04, 0.08, 0.85)):
        extra = {"Text": k.ui_text(txt, INK, "Center")} if font else {}
        return self.rect(eid, tag, parent, anchor, pivot, pos, size, font, order=order, UIProgressBar=k.ui_progress(1.0, back, fill, 2.0, 8.0), **extra)

    def dim(self, eid, tag, order, active=False):
        return self.stretch(eid, tag, CANVAS, order=order, active=active, UIImage=k.ui_image((0.02, 0.01, 0.05, 0.72), raycast=True),
                            UIAnimation=k.ui_anim("Fade", "Fade", 0.2))


def chain_nav(scene, ids, horizontal=False):
    """Explicit wrap-around navigation through `ids` (vertical by default)."""
    n = len(ids)
    for i, eid in enumerate(ids):
        nav = scene.entities[eid]["UINavigation"]
        prev, nxt = ids[(i - 1) % n], ids[(i + 1) % n]
        nav["Mode"] = "Explicit"
        if horizontal:
            nav.update(Left=prev, Right=nxt, Up=0, Down=0)
        else:
            nav.update(Up=prev, Down=nxt, Left=0, Right=0)


# ------------------------------------------------------------------------------------------------ world pieces


def character(scene, eid, style, pos, scale, tags, graph=None, overrides=None, hero=False, active=True, speed=2.4):
    """A top-down actor: physics root (+ graph) and a sprite child (Animator drives the sprite clips)."""
    root = {"Rigidbody2D": k.rigidbody2d("Dynamic", 0.0, True, linear_damping=6.0, layer=2 if hero else 3),
            "CircleCollider2D": k.circle2d(0.3 if hero else 0.32, friction=0.0)}
    if not hero:
        root["NavAgent"] = {"Grid": 2001, "Speed": speed, "Radius": 0.3, "StoppingDistance": 0.1, "RepathInterval": 0.5, "UpdateRotation": False,
                            "Crowd": {"Enabled": True}}
    if graph:
        root["NativeScript"] = graph_script(graph, **(overrides or {}))
    scene.add(eid, style.capitalize() if not hero else "Nilufar", pos=pos, tags=tags, active=active, **root)
    clips = [f"Animations/{style}_{c}.zsanim" for c in ("Idle", "WalkDown", "WalkUp", "WalkLeft", "WalkRight", "Attack", "Death")]
    anim = {"DriveSpriteClips": True, "DefaultState": 0, "DefaultFade": 0.1,
            "Parameters": [{"Name": "MoveX", "Type": 0, "Value": 0}, {"Name": "MoveY", "Type": 0, "Value": 0}, {"Name": "Speed", "Type": 0, "Value": 0},
                           {"Name": "Grounded", "Type": 1, "Value": 1}, {"Name": "Dead", "Type": 1, "Value": 0}, {"Name": "Attack", "Type": 2, "Value": 0}],
            "States": [{"Name": "Move", "Type": "BlendTree2D", "BlendParameter": "MoveX", "BlendParameterY": "MoveY", "Duration": 0.6,
                        "Motions": [{"Clip": f"{style}_Idle", "Position": [0, 0], "Speed": 1}, {"Clip": f"{style}_WalkDown", "Position": [0, -1], "Speed": 1},
                                    {"Clip": f"{style}_WalkUp", "Position": [0, 1], "Speed": 1}, {"Clip": f"{style}_WalkLeft", "Position": [-1, 0], "Speed": 1},
                                    {"Clip": f"{style}_WalkRight", "Position": [1, 0], "Speed": 1}], "Graph": [80, 100]},
                       {"Name": "Attack", "Clip": f"{style}_Attack", "Duration": 0.35, "Loop": False, "Graph": [380, 40]},
                       {"Name": "Death", "Clip": f"{style}_Death", "Duration": 0.6, "Loop": False, "Graph": [380, 200]}],
            "Transitions": [{"From": 0, "To": 2, "Fade": 0, "Priority": 10, "Conditions": [{"Parameter": "Dead", "Op": 2}]},
                            {"From": 1, "To": 2, "Fade": 0, "Priority": 10, "Conditions": [{"Parameter": "Dead", "Op": 2}]},
                            {"From": 0, "To": 1, "Fade": 0.05, "Priority": 5, "Conditions": [{"Parameter": "Attack", "Op": 4}]},
                            {"From": 1, "To": 0, "Fade": 0.1, "HasExitTime": True, "ExitTime": 1, "Priority": 0}]}
    scene.add(eid + 1, "Sprite", eid, pos=(0, 0.32 * scale, 0.1),
              SpriteRenderer=k.sprite(f"Textures/{style}.png", f"{style}_idle_0", lit=True, order=10, size=(scale, scale), draw_mode="Sliced"),
              SpriteAnimation={"Columns": 1, "Rows": 1, "StartFrame": 0, "FrameCount": 2, "FPS": 4, "Loop": True, "Playing": True,
                               "Frames": [f"{style}_idle_0", f"{style}_idle_1"], "Clips": [{"Path": c} for c in clips], "PlayOnStart": True},
              Animator=anim)
    return eid


def light(scene, eid, parent, pos, col, intensity, radius, flicker=0.0, inner=0.3, falloff=1.8):
    return scene.add(eid, "Light", parent, pos=pos, Light2D=k.light2d(col, intensity, radius, inner, falloff, flicker))


def fire_fx(scene, eid, parent, pos, scale=1.0):
    return scene.add(eid, "Olov", parent, pos=pos, ParticleSystem=k.particles(
        EmissionRate=38.0 * scale, MaxParticles=int(120 * scale), Shape=0, ShapeRadius=0.12 * scale, LifetimeMin=0.35, LifetimeMax=0.8,
        SpeedMin=0.6, SpeedMax=1.4 * scale, Direction=(0, 1, 0), Spread=18.0, StartColor=(1.0, 0.75, 0.3, 1.0), EndColor=(0.9, 0.25, 0.08, 0.0),
        StartSize=0.32 * scale, EndSize=0.05, SortingOrder=30))


def fireflies(scene, eid, size, count=60, col=(0.85, 1.0, 0.45, 0.95)):
    w, h = size
    return scene.add(eid, "Yorug' qo'ng'izlar", pos=(w / 2, h / 2, 0), ParticleSystem=k.particles(
        EmissionRate=count / 4.0, MaxParticles=count * 2, Shape=2, ShapeExtents=(w / 2, h / 2, 0), LifetimeMin=3.0, LifetimeMax=6.0,
        SpeedMin=0.05, SpeedMax=0.35, Direction=(0, 1, 0), Spread=180.0, Drag=0.2, StartColor=col, EndColor=(col[0] * 0.7, col[1], col[2] * 0.6, 0.0),
        StartSize=0.12, EndSize=0.05, SortingOrder=40, Billboard=False))


def tilemap(level, g):
    """Ground + wall layers (8 x 2 tileset). Level 1: grass/path/water/ruins; level 2: crypt slabs, runes, walls."""
    w, h = level["size"]
    ground, walls = [], []
    rng = __import__("numpy").random.default_rng(7 + w)
    path_cells = level.get("_path", set())
    for y in range(h):
        for x in range(w):
            c = g[y][x]
            if level["scene"] == "Boglar":
                if c == "#":
                    ground.append(-1)
                    walls.append(6 if (x * 7 + y * 3) % 5 else 9)
                elif c == "~":
                    ground.append(-1)
                    walls.append(7)
                else:
                    walls.append(-1)
                    if (x, y) in path_cells:
                        ground.append(4 + (x + y) % 2)
                    else:
                        ground.append(8 if rng.random() < 0.12 else int(rng.integers(0, 4)))
            else:
                if c == "#":
                    ground.append(-1)
                    walls.append(6)
                else:
                    walls.append(-1)
                    arena = 12 <= x <= 32 and 18 <= y <= 28
                    if arena and (x + y) % 4 == 0:
                        ground.append(8)
                    elif rng.random() < 0.015:
                        ground.append(4)
                    elif rng.random() < 0.08:
                        ground.append(5)
                    else:
                        ground.append(int(rng.integers(0, 4)))
    tex = "Textures/tiles_boglar.png" if level["scene"] == "Boglar" else "Textures/tiles_yertola.png"
    return {"Tileset": tex, "PixelArt": False, "TilesetColumns": 8, "TilesetRows": 2, "Width": w, "Height": h, "TileSize": 1.0,
            "Layers": [{"Name": "Zamin", "Visible": True, "Collision": False, "SortingOrder": 0, "Opacity": 1.0, "Tiles": ground},
                       {"Name": "Devor", "Visible": True, "Collision": True, "SortingOrder": 1, "Opacity": 1.0, "Tiles": walls}],
            "Color": [1, 1, 1, 1], "SortingOrder": -10, "Collision": True, "Friction": 0.0, "CollisionLayer": 1, "Lit": True}


# ------------------------------------------------------------------------------------------------ build


def build(assets, project, C, LEVELS, ENEMIES, routes, oracle):
    os.makedirs(os.path.join(assets, "Scripts"), exist_ok=True)
    projectiles(assets, project)
    for number, level in LEVELS.items():
        route, kinds, g, walkable = routes[number]
        level_scene(assets, project, C, number, level, ENEMIES, route, kinds, g, oracle)
    menu_scene(assets, project, LEVELS)
    hero_graph(assets, project, C)
    game_graph(assets, project, C)
    enemy_graph(assets, project)
    menu_graph(assets, project)


def projectiles(assets, project):
    """Visual-only projectile prefabs (no bodies, colliders or scripts): pooled by CombatWorld."""
    for name, frame, size, col, glow in (("Bolt", "bolt", (0.55, 0.55), (1, 1, 1, 1), (1.0, 0.6, 0.2)), ("Arrow", "arrow", (0.8, 0.2), (1, 1, 1, 1), None),
                                         ("Orb", "orb", (0.6, 0.6), (1, 1, 1, 1), (0.7, 0.3, 1.0)), ("Spit", "orb", (0.35, 0.35), (0.5, 1, 0.9, 1), (0.3, 1.0, 0.8))):
        e = [{"Entity": 1, "Tag": f"{name} visual", "Transform": T((0, 0, 0.2)),
              "SpriteRenderer": k.sprite("Textures/props.png", frame, col, order=35, size=size, draw_mode="Sliced")}]
        if glow:
            e[0]["ChildOrder"] = [2]
            e.append({"Entity": 2, "Tag": "Light", "Parent": 1, "Transform": T(), "Light2D": k.light2d(glow, 1.1, 2.2, 0.2, 1.6)})
        rel = f"Prefabs/{name}.zprefab"
        k.write_json(os.path.join(assets, rel), {"ZaminEntities": 1, "Entities": e})
        k.write_zmeta(assets, rel, "Prefab", {}, project)


def level_scene(assets, project, C, number, level, ENEMIES, route, kinds, g, oracle):
    w, h = level["size"]
    amb_col, amb_int = level["ambient"]
    settings = k.settings_2d(False, amb_col, amb_int, (0.8, 0.55, 0.9) if number == 1 else (0.7, 0.6, 1.1), 0.38 if number == 1 else 0.5,
                             saturation=1.12, contrast=1.06)
    s = Scene(level["scene"], settings)
    s.settings["Post"]["FXAA"] = True
    level["_path"] = set()
    if number == 1:
        # the worn path follows the route (a few cells wide)
        for (x0, y0), (x1, y1) in zip(route, route[1:]):
            steps = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
            for i in range(steps + 1):
                px, py = x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps
                for dx in (0,):
                    level["_path"].add((int(px + dx), int(py)))
    s.add(2000, "Tilemap", Tilemap=tilemap(level, g))
    s.add(2001, "Navigatsiya", NavGrid2D={"AutoBounds": True, "Offset": [0, 0], "Size": [w, h], "CellSize": 0.5, "UseTilemaps": True, "OneWayBlocks": False,
                                          "Tilemap": 0, "UseColliders": True, "LayerMask": WALL_MASK, "AgentRadius": 0.3, "Diagonals": True,
                                          "CutCorners": False, "BakeOnStart": True, "AutoRebuild": False})
    # camera
    s.add(CAMERA, "Kamera", pos=(level["spawn"][0] + 0.5, level["spawn"][1] + 0.5, 10),
          Camera=k.camera_ortho(12.5, (0.03, 0.03, 0.06, 1), shake={"Trauma": 0.0, "MaxOffset": 0.5, "MaxAngle": 0.05}),
          Camera2DFollow=k.camera_follow(HERO, (0, 0.4), (0.15, 0.15), (0.6, 0.45), (1.0, 0.8), ((0, 0), (w, h))))
    # hero
    sx, sy = level["spawn"][0] + 0.5, level["spawn"][1] + 0.5
    character(s, HERO, "hero", (sx, sy, 0), 1.3, ["Hero"], "Hero", {"Level": number, "Slot": f"Run{number}", "Route": [[p[0], p[1], 0] for p in route],
                                                                     "Steps": list(kinds)}, hero=True)
    light(s, HERO_LIGHT, HERO, (0.25, 0.1, 0), (1.0, 0.78, 0.45), 1.25 if number == 1 else 1.6, 4.5 if number == 1 else 6.0, flicker=0.08)
    # controller + audio
    uses = level["uses"]
    pickups = level["pickups"]
    pickup_ids = [5500 + i for i in range(len(pickups))]
    use_ids = [5600 + i for i in range(len(uses))]
    game = {"Level": number, "Slot": f"Run{number}", "Next": "Scenes/Yertola.zscene" if number == 1 else "Scenes/Menu.zscene",
            "Self": f"Scenes/{level['scene']}.zscene", "Title": level["title"],
            "PickupIds": pickup_ids, "PickupKeys": [f"L{number}-{item}-{i:02d}" for i, (item, _) in enumerate(pickups)],
            "PickupItems": [item for item, _ in pickups],
            "UseIds": use_ids, "UseKeys": [u[2] for u in uses], "UsePrompts": [u[3]["Prompt"] for u in uses],
            "UseRequired": [u[3].get("Required", "") for u in uses], "UseCounts": [u[3].get("RequiredCount", 1) for u in uses],
            "UseConsume": [bool(u[3].get("Consume", False)) for u in uses], "UseRewards": [u[3].get("Reward", "") for u in uses],
            "UseHeal": [u[3].get("Heal", 0) for u in uses], "UseMaxUses": [u[3].get("MaxUses", 1) for u in uses],
            "UseCooldown": [u[3].get("Cooldown", 0.2) for u in uses], "UseDeactivate": [u[0] in ("Chest", "Gate") for u in uses],
            "HasExit": level["exit"] is not None, "Exit": [level["exit"][0] + 0.5, level["exit"][1] + 0.5, 0] if level["exit"] else [0, 0, 0]}
    boss_ids, wisp_ids = [], []
    s.add(GAME, "O'yin", NativeScript=graph_script("Game", **game))
    s.add(MUSIC, "Musiqa", GAME, AudioSource=k.audio_source(f"Audio/{level['music']}", 0.8, True, True, "Music", True))
    s.add(AMBIENCE, "Muhit", GAME, AudioSource=k.audio_source(f"Audio/{level['amb']}", 0.6, True, True, "SFX"))
    if number == 2:
        s.add(BOSS_MUSIC, "Boss musiqasi", GAME, AudioSource=k.audio_source("Audio/music_boss.wav", 0.0, True, False, "Music", True))
    # enemies
    for i, (kind, c, key, patrol) in enumerate(level["enemies"]):
        spec = ENEMIES[kind]
        eid = 5000 + i * 10
        lose = spec["View"] + 2
        over = {"Key": key, "HP": spec["HP"], "Armor": spec["Armor"], "Damage": spec["Damage"], "Cooldown": spec["Cooldown"], "Speed": spec["Speed"],
                "View": spec["View"], "Lose": lose, "FOV": spec["FOV"], "Leash": max(spec["Leash"], lose), "Enter": spec["Enter"], "Exit": spec["Exit"],
                "Ranged": spec["Ranged"], "Ability": spec.get("Ability", ""), "Amount": spec.get("Amount", 0), "Visual": eid + 1,
                "Patrol": [[p[0], p[1], 0] for p in patrol], "Loot": spec.get("Loot", ""), "Loot2": spec.get("Loot2", ""),
                "Dormant": not spec.get("active", True), "Boss": kind == "boss", "Kind": kind}
        character(s, eid, kind if kind != "chief" else "chief", (c[0] + 0.5, c[1] + 0.5, 0), spec["scale"], spec["tags"], "Enemy", over, speed=spec["Speed"])
        if kind == "boss":
            boss_ids.append(eid)
            light(s, eid + 2, eid, (0, 0.8, 0), (0.7, 0.35, 1.0), 1.4, 5.0, flicker=0.15)
            s.add(eid + 3, "Halqa", eid, pos=(0, 0, -0.05), active=False, SpriteRenderer=k.sprite("Textures/props.png", "ring", (1, 1, 1, 0.9), order=5, size=(7, 7), draw_mode="Sliced", lit=False))
        if kind == "wisp":
            wisp_ids.append(eid)
            light(s, eid + 2, eid, (0, 0.6, 0), (0.3, 1.0, 0.85), 1.2, 3.0, flicker=0.2)
        if kind == "chief":
            light(s, eid + 2, eid, (0, 0.8, 0), (1.0, 0.4, 0.25), 0.7, 2.5)
    s.entities[GAME]["NativeScript"] = graph_script("Game", **game, Boss=boss_ids[0] if boss_ids else 0, Wisps=wisp_ids)
    # pickups and usable objects
    for eid, (item, c) in zip(pickup_ids, pickups):
        s.add(eid, item, pos=(c[0] + 0.5, c[1] + 0.5, 0), tags=["Pickup", item],
              SpriteRenderer=k.sprite("Textures/props.png", f"item_{item.lower()}", order=8, size=(0.6, 0.6) if item == "Coin" else (0.75, 0.75), draw_mode="Sliced", lit=item != "Lamp"))
        if item == "Lamp":
            light(s, eid + 300, eid, (0, 0.1, 0), (1.0, 0.8, 0.4), 1.4, 3.2, flicker=0.1)
    frames = {"Chest": ("chest", (1.1, 1.1)), "Shrine": ("shrine", (1.3, 1.3)), "Gate": ("gate", (1.0, 3.0)), "Altar": ("altar", (1.8, 1.8))}
    for eid, (kind, c, key, opts) in zip(use_ids, uses):
        frame, size = frames[kind]
        comps = {"SpriteRenderer": k.sprite("Textures/props.png", frame, order=9, size=size, draw_mode="Sliced", lit=True)}
        if kind == "Gate":
            comps.update(Rigidbody2D=k.rigidbody2d("Static", layer=1), BoxCollider2D=k.box2d((1.0, 3.0)))
        s.add(eid, kind, pos=(c[0] + 0.5, c[1] + 0.5, 0), tags=[kind], **comps)
        if kind == "Shrine":
            light(s, eid + 300, eid, (0, 0.2, 0), (0.4, 0.9, 1.0), 1.2, 3.0, flicker=0.05)
        if kind == "Altar":
            light(s, eid + 300, eid, (0, 0.5, 0), (1.0, 0.7, 0.35), 1.0, 3.5, flicker=0.1)
            fire_fx(s, eid + 350, eid, (0, 0.55, 0), 0.6)
    if level["exit"]:
        ex = level["exit"]
        s.add(5990, "Zinapoya", pos=(ex[0] + 0.5, ex[1] + 0.5, 0), SpriteRenderer=k.sprite("Textures/props.png", "stairs", order=2, size=(1.4, 1.4), draw_mode="Sliced", lit=True))
        light(s, 5991, 5990, (0, 0, 0), (0.45, 0.55, 1.0), 1.0, 3.0, flicker=0.05)
    # decoration
    deco = 3000
    if number == 1:
        for i, c in enumerate(level["trees"]):
            s.add(3000 + i, "Daraxt", pos=(c[0] + 0.5, c[1] + 0.5, 0), Rigidbody2D=k.rigidbody2d("Static", layer=1), CircleCollider2D=k.circle2d(0.35),
                  SpriteRenderer=k.sprite("Textures/props.png", f"tree_{i % 3}", order=20, size=(2.6, 2.6), draw_mode="Sliced", lit=True))
        # bushes and tents
        import numpy as np
        rng = np.random.default_rng(3)
        floor = [(x, y) for y in range(h) for x in range(w) if g[y][x] == "." and (x, y) not in level["_path"]]
        blocked = {tuple(c) for c in level["trees"]} | {tuple(e[1]) for e in level["enemies"]} | {tuple(p[1]) for p in level["pickups"]} | {tuple(u[1]) for u in level["uses"]}
        n = 0
        for idx in rng.permutation(len(floor))[:26]:
            c = floor[int(idx)]
            if c in blocked:
                continue
            s.add(3100 + n, "Buta", pos=(c[0] + 0.5, c[1] + 0.5, 0), SpriteRenderer=k.sprite("Textures/props.png", "bush", order=4, size=(0.9, 0.9), draw_mode="Sliced", lit=True))
            n += 1
        for i, c in enumerate([(21, 3), (30, 7), (19, 22)]):
            s.add(3200 + i, "Chodir", pos=(c[0] + 0.5, c[1] + 0.5, 0), SpriteRenderer=k.sprite("Textures/props.png", "tent", order=6, size=(2.0, 2.0), draw_mode="Sliced", lit=True))
        s.add(3300, "Gulxan", pos=(25.5, 4.5, 0), SpriteRenderer=k.sprite("Textures/props.png", "campfire", order=5, size=(1.2, 1.2), draw_mode="Sliced", lit=True))
        fire_fx(s, 3301, 3300, (0, 0.1, 0), 1.0)
        light(s, 3302, 3300, (0, 0.3, 0), (1.0, 0.62, 0.28), 1.6, 6.5, flicker=0.25)
        for i, c in enumerate([(18, 15), (34, 15), (18, 24), (34, 24), (39, 15), (44, 24)]):
            s.add(3400 + i, "Mash'al", pos=(c[0] + 0.5, c[1] + 0.5, 0), SpriteRenderer=k.sprite("Textures/props.png", "torch", order=7, size=(0.8, 0.8), draw_mode="Sliced", lit=False))
            fire_fx(s, 3420 + i, 3400 + i, (0, 0.35, 0), 0.45)
            light(s, 3440 + i, 3400 + i, (0, 0.4, 0), (1.0, 0.65, 0.3), 1.1, 4.5, flicker=0.2)
        fireflies(s, 8000, (w, h), 70)
    else:
        torches = [(2, 9), (12, 2), (14, 9), (30, 2), (30, 9), (32, 12), (41, 2), (2, 20), (10, 12), (14, 16), (30, 16),
                   (12, 18), (32, 18), (12, 28), (32, 28), (18, 28), (26, 28)]
        for i, c in enumerate(torches):
            s.add(3400 + i, "Mash'al", pos=(c[0] + 0.5, c[1] + 0.5, 0), SpriteRenderer=k.sprite("Textures/props.png", "torch", order=7, size=(0.8, 0.8), draw_mode="Sliced", lit=False))
            fire_fx(s, 3430 + i, 3400 + i, (0, 0.35, 0), 0.45)
            light(s, 3460 + i, 3400 + i, (0, 0.4, 0), (1.0, 0.6, 0.28), 1.35, 5.0, flicker=0.25)
        for i, (x0, y0, x1, y1) in enumerate(level["pillars"]):
            s.add(3500 + i, "Ustun", pos=((x0 + x1 + 1) / 2, (y0 + y1 + 1) / 2 + 0.3, 0),
                  SpriteRenderer=k.sprite("Textures/props.png", "pillar", order=12, size=(x1 - x0 + 1.4, y1 - y0 + 2.0), draw_mode="Sliced", lit=True))
        for i, c in enumerate([(5, 7), (17, 3), (27, 8), (37, 3), (4, 17), (25, 14), (14, 20), (30, 27)]):
            s.add(3600 + i, "Suyaklar", pos=(c[0] + 0.5, c[1] + 0.5, 0), SpriteRenderer=k.sprite("Textures/props.png", "bones", order=3, size=(0.9, 0.9), draw_mode="Sliced", lit=True))
        fireflies(s, 8000, (w, h), 40, (0.55, 0.7, 1.0, 0.8))
        s.add(8001, "Chang", pos=(w / 2, h / 2, 0), ParticleSystem=k.particles(
            EmissionRate=8.0, MaxParticles=120, Shape=2, ShapeExtents=(w / 2, h / 2, 0), LifetimeMin=4.0, LifetimeMax=7.0, SpeedMin=0.02, SpeedMax=0.12,
            Spread=180.0, StartColor=(0.7, 0.7, 0.9, 0.18), EndColor=(0.5, 0.5, 0.8, 0.0), StartSize=0.9, EndSize=1.6, Additive=False, SortingOrder=60))
    # effects the graphs trigger (bursts)
    s.add(8100, "Zarba uchquni", pos=(0, 0, 0), ParticleSystem=k.particles(
        Playing=False, Looping=False, EmissionRate=0.0, Burst=0, MaxParticles=80, Shape=0, ShapeRadius=0.2, LifetimeMin=0.25, LifetimeMax=0.5,
        SpeedMin=2.0, SpeedMax=5.0, Spread=180.0, StartColor=(1.0, 0.85, 0.5, 1.0), EndColor=(1.0, 0.3, 0.1, 0.0), StartSize=0.14, EndSize=0.02, SortingOrder=45))
    s.add(8101, "Daraja halqasi", HERO, pos=(0, 0.2, 0), ParticleSystem=k.particles(
        Playing=False, Looping=False, EmissionRate=0.0, MaxParticles=120, Shape=0, ShapeRadius=0.6, LifetimeMin=0.6, LifetimeMax=1.1,
        SpeedMin=1.0, SpeedMax=2.4, Spread=180.0, StartColor=(1.0, 0.9, 0.55, 1.0), EndColor=(0.6, 0.8, 1.0, 0.0), StartSize=0.2, EndSize=0.04, SortingOrder=46))
    hud(s, number, level, C)
    s.save(assets, project)


def hud(s, number, level, C):
    ui = UI(s, GAME)
    s.add(CANVAS, "UI", UICanvas=k.ui_canvas())
    root = ui.stretch(6001, "HUD", CANVAS)
    # top-left vitals
    ui.panel(6010, "Holat", root, (24, -24), (560, 178), anchor=(0, 1), pivot=(0, 1))
    ui.image(6011, "Daraja nishoni", 6010, "badge", (0, 1), (24, -22), (104, 104), pivot=(0, 1))
    ui.text(6012, "Daraja", 6011, "1", (0.5, 0.5), (0, 2), (90, 60), 46, ACCENT)
    ui.bar(6013, "Tajriba", 6010, (0, 1), (24, -136), (104, 14), (0.6, 0.85, 1.0, 1), 0, pivot=(0, 1))
    ui.bar(6014, "Sog'liq", 6010, (0, 1), (148, -30), (380, 38), (0.92, 0.26, 0.24, 1), 22, pivot=(0, 1))
    ui.bar(6015, "Energiya", 6010, (0, 1), (148, -80), (380, 30), (0.38, 0.66, 1.0, 1), 19, pivot=(0, 1))
    ui.text(6016, "Nom", 6010, "NILUFAR", (0, 1), (148, -122), (380, 34), 22, MUTED, "Left", pivot=(0, 1))
    # top-right resources
    ui.panel(6020, "Boyliklar", root, (-24, -24), (430, 96), anchor=(1, 1), pivot=(1, 1))
    for i, (icon, eid, txt) in enumerate((("coin", 6021, "0"), ("lamp", 6023, "0"), ("key", 6026, "0"), ("potion", 6028, "0"))):
        ui.image(eid, icon, 6020, f"icon_{icon}", (0, 0.5), (22 + i * 102, 0), (44, 44), pivot=(0, 0.5))
        ui.text(eid + 1, f"{icon} soni", 6020, txt, (0, 0.5), (70 + i * 102, 0), (56, 40), 28, INK, "Left", pivot=(0, 0.5))
    # quest tracker
    ui.panel(6030, "Vazifalar", root, (-24, -136), (500, 330), anchor=(1, 1), pivot=(1, 1))
    ui.text(6031, "Sarlavha", 6030, "Vazifalar", (0.5, 1), (0, -40), (440, 40), 28, ACCENT, key="hud.quests")
    for i in range(6):
        ui.text(6032 + i, f"Vazifa {i + 1}", 6030, "", (0, 1), (32, -84 - i * 40), (440, 34), 21, INK, "Left", pivot=(0, 1))
    # ability bar
    bar = ui.panel(6040, "Qobiliyatlar", root, (0, 22), (700, 132), anchor=(0.5, 0), pivot=(0.5, 0))
    for i, (icon, hint, label) in enumerate((("melee", "[Attack]", "Zarba"), ("bolt", "[Bolt]", "Bolt"), ("nova", "[Nova]", "Nova"),
                                              ("mend", "[Mend]", "Mend"), ("potion", "[Potion]", "Potion"))):
        x = -260 + i * 130
        ui.image(6041 + i * 4, f"Slot {label}", bar, "slot", (0.5, 0.5), (x, 10), (104, 104), kind="Sliced")
        ui.image(6042 + i * 4, f"Belgi {label}", 6041 + i * 4, f"icon_{icon}", (0.5, 0.5), (0, 8), (68, 68))
        ui.bar(6043 + i * 4, f"Tayyorlik {label}", 6041 + i * 4, (0.5, 0), (0, 4), (84, 12), (0.98, 0.76, 0.32, 1), 0, back=(0.05, 0.04, 0.08, 0.6))
        ui.text(6044 + i * 4, f"Tugma {label}", bar, hint, (0.5, 0.5), (x, -58), (128, 26), 16, MUTED, hints=True)
    ui.text(6090, "Ishora", root, "", (0.5, 0), (0, 178), (900, 40), 26, INK)
    ui.text(6091, "Xabar", root, "", (0.5, 1), (0, -170), (1100, 60), 40, ACCENT)
    ui.text(6092, "Daraja nomi", root, level["title"].upper(), (0.5, 1), (0, -96), (900, 70), 56, INK)
    ui.text(6093, "Boss nomi", root, "Soya Bek", (0.5, 0), (0, 214), (600, 34), 26, (0.85, 0.65, 1.0, 1), active=False, key="hud.boss")
    ui.rect(6094, "Boss sog'lig'i", root, (0.5, 0), None, (0, 186), (760, 22), 0, active=False,
            UIProgressBar=k.ui_progress(1.0, (0.05, 0.04, 0.08, 0.85), (0.62, 0.30, 0.95, 1), 2.0, 6.0))
    for i in range(4):
        ui.text(6070 + i, f"Jang {i + 1}", root, "", (0, 0.5), (32, 60 - i * 34), (360, 30), 22, (1.0, 0.6, 0.5, 1), "Left", pivot=(0, 0.5))
    ui.stretch(6080, "Jarohat", root, order=-1, active=False, UIImage=k.ui_image((1, 1, 1, 0.8), "Textures/ui.png", "vignette", "Sliced", 1.0, raycast=False))
    ui.text(6081, "Boshqaruv", root, "[Inventory] inventar   [Pause] pauza", (1, 0), (-24, 18), (560, 28), 16, MUTED, "Right", hints=True, pivot=(1, 0))
    # pause
    ui.dim(6100, "Pauza", 50)
    ui.panel(6101, "Pauza paneli", 6100, (0, 0), (560, 470))
    ui.text(6102, "Sarlavha", 6101, "Pauza", (0.5, 1), (0, -62), (480, 60), 46, ACCENT, key="pause.title")
    b = [ui.button(6111, "Davom etish", 6101, "Davom etish", "pause.resume", "Resume", (0, 70), select=True),
         ui.button(6112, "Inventar", 6101, "Inventar", "pause.bag", "Bag", (0, -10)),
         ui.button(6113, "Saqlab chiqish", 6101, "Saqlab, bosh menyuga", "pause.savequit", "SaveQuit", (0, -90))]
    chain_nav(s, b)
    # inventory
    ui.dim(6200, "Inventar", 51)
    ui.panel(6201, "Inventar paneli", 6200, (0, 0), (980, 640))
    ui.text(6202, "Sarlavha", 6201, "Inventar", (0.5, 1), (0, -60), (600, 60), 44, ACCENT, key="bag.title")
    rows = []
    for i in range(6):
        rows.append(ui.button(6211 + i, f"Qator {i + 1}", 6201, "", "", "Select", (-200, 150 - i * 68), (460, 58), 26))
    acts = [ui.button(6221, "Kiyish", 6201, "Kiyish", "bag.equip", "Equip", (270, 150), (300, 58), 26),
            ui.button(6222, "Ichish", 6201, "Ichish", "bag.drink", "Drink", (270, 82), (300, 58), 26),
            ui.button(6223, "Keyingi", 6201, "Keyingi", "bag.next", "NextPage", (270, 14), (300, 58), 26),
            ui.button(6224, "Yopish", 6201, "Yopish", "bag.close", "CloseBag", (270, -54), (300, 58), 26)]
    chain_nav(s, rows + acts)
    for i, eid in enumerate(rows):
        s.entities[eid]["UINavigation"].update(Right=acts[min(i, 3)])
    for i, eid in enumerate(acts):
        s.entities[eid]["UINavigation"].update(Left=rows[i])
    for i in range(3):
        ui.text(6231 + i, f"Jihoz {i + 1}", 6201, "", (0.5, 0.5), (270, -130 - i * 38), (380, 34), 22, MUTED)
    # defeat / victory / level card
    ui.dim(6300, "Mag'lubiyat", 52)
    ui.panel(6301, "Mag'lubiyat paneli", 6300, (0, 0), (760, 420))
    ui.text(6302, "Sarlavha", 6301, "CHIROQ SO'NDI", (0.5, 1), (0, -80), (700, 80), 60, (1.0, 0.45, 0.4, 1), key="defeat.title")
    ui.text(6303, "Izoh", 6301, "Nilufar uch marta yiqildi", (0.5, 1), (0, -150), (700, 40), 26, MUTED)
    d = [ui.button(6311, "Qayta", 6301, "Oxirgi saqlashdan", "defeat.retry", "Retry", (0, -30), select=True),
         ui.button(6312, "Menyu", 6301, "Bosh menyu", "end.menu", "Menu", (0, -110))]
    chain_nav(s, d)
    ui.dim(6400, "G'alaba", 53)
    ui.panel(6401, "G'alaba paneli", 6400, (0, 0), (860, 600))
    ui.text(6402, "Sarlavha", 6401, "TONG OTDI", (0.5, 1), (0, -86), (800, 90), 72, ACCENT, key="victory.title")
    ui.text(6403, "Izoh", 6401, "Qo'rg'on yana yorug'", (0.5, 1), (0, -160), (800, 44), 30, INK, key="victory.sub")
    ui.text(6410, "Natijalar", 6401, "", (0.5, 0.5), (0, -10), (720, 220), 26, INK, wrap=True)
    v = [ui.button(6411, "Menyu", 6401, "Bosh menyu", "end.menu", "Menu", (0, -220), select=True)]
    chain_nav(s, v)
    ui.dim(6500, "Daraja yakuni", 54)
    ui.panel(6501, "Daraja paneli", 6500, (0, 0), (860, 420))
    ui.text(6502, "Sarlavha", 6501, "1-DARAJA YAKUNLANDI", (0.5, 1), (0, -86), (800, 80), 58, ACCENT, key="card.title")
    ui.text(6503, "Izoh", 6501, "Bog'lar ozod", (0.5, 1), (0, -156), (800, 44), 30, INK, key="card.sub")
    ui.text(6510, "Natijalar", 6501, "", (0.5, 0.5), (0, -60), (720, 120), 26, INK, wrap=True)


def menu_scene(assets, project, LEVELS):
    level = LEVELS[1]
    settings = k.settings_2d(False, (0.75, 0.62, 0.85), 0.42, (0.75, 0.55, 1.0), 0.45, saturation=1.15, contrast=1.08)
    settings["Post"]["FXAA"] = True
    s = Scene("Menu", settings)
    # a lit diorama: the orchard path winding up to the fortress gate, the keeper by a campfire
    w, h = 30, 18
    import numpy as np
    rng = np.random.default_rng(11)
    ground = [int(rng.integers(0, 4)) if rng.random() > 0.1 else 8 for _ in range(w * h)]
    walls = [-1] * (w * h)
    for x in range(w):
        for y in (12, 13, 14, 15, 16, 17):
            if not (12 <= x <= 14 and y <= 13):
                walls[y * w + x] = 6 if (x * 3 + y) % 5 else 9
    for y in range(0, 14):
        cx = 13 + int(round(2.2 * math.sin(y * 0.45)))
        for x in (cx, cx + 1):
            ground[y * w + x] = 4 + (x + y) % 2
    s.add(2000, "Tilemap", Tilemap={"Tileset": "Textures/tiles_boglar.png", "PixelArt": False, "TilesetColumns": 8, "TilesetRows": 2, "Width": w, "Height": h,
                                    "TileSize": 1.0, "Layers": [{"Name": "Zamin", "Visible": True, "Collision": False, "SortingOrder": 0, "Opacity": 1.0, "Tiles": ground},
                                                                {"Name": "Devor", "Visible": True, "Collision": False, "SortingOrder": 1, "Opacity": 1.0, "Tiles": walls}],
                                    "Color": [1, 1, 1, 1], "SortingOrder": -10, "Collision": False, "Friction": 0.0, "Lit": True})
    s.add(CAMERA, "Kamera", pos=(12.0, 8.6, 10), Camera=k.camera_ortho(12.0, (0.03, 0.03, 0.06, 1)))
    for i, c in enumerate([(2, 3), (5, 8), (22, 3), (25, 9), (8, 11), (20, 11), (1, 10), (27, 5), (9, 4), (17, 1)]):
        s.add(3000 + i, "Daraxt", pos=(c[0] + 0.5, c[1] + 0.5, 0), SpriteRenderer=k.sprite("Textures/props.png", f"tree_{i % 3}", order=20, size=(3.0, 3.0), draw_mode="Sliced", lit=True))
    for i, c in enumerate([(11.4, 12.6), (15.6, 12.6)]):
        s.add(3400 + i, "Mash'al", pos=(c[0], c[1], 0), SpriteRenderer=k.sprite("Textures/props.png", "torch", order=7, size=(0.9, 0.9), draw_mode="Sliced"))
        fire_fx(s, 3420 + i, 3400 + i, (0, 0.38, 0), 0.5)
        light(s, 3440 + i, 3400 + i, (0, 0.4, 0), (1.0, 0.62, 0.28), 1.5, 6.5, flicker=0.2)
    s.add(3300, "Gulxan", pos=(19.5, 6.0, 0), SpriteRenderer=k.sprite("Textures/props.png", "campfire", order=5, size=(1.3, 1.3), draw_mode="Sliced", lit=True))
    fire_fx(s, 3301, 3300, (0, 0.1, 0), 1.1)
    light(s, 3302, 3300, (0, 0.3, 0), (1.0, 0.6, 0.26), 1.8, 7.5, flicker=0.25)
    s.add(3200, "Chodir", pos=(22.5, 7.4, 0), SpriteRenderer=k.sprite("Textures/props.png", "tent", order=6, size=(2.2, 2.2), draw_mode="Sliced", lit=True))
    s.add(1000, "Nilufar", pos=(17.6, 6.3, 0), SpriteRenderer=k.sprite("Textures/hero.png", "hero_idle_0", lit=True, order=10, size=(1.6, 1.6), draw_mode="Sliced"),
          SpriteAnimation={"Columns": 1, "Rows": 1, "StartFrame": 0, "FrameCount": 2, "FPS": 3, "Loop": True, "Playing": True,
                           "Frames": ["hero_idle_0", "hero_idle_1"], "Clips": [{"Path": "Animations/hero_Idle.zsanim"}], "PlayOnStart": True})
    light(s, 1002, 1000, (0.4, 0.2, 0), (1.0, 0.78, 0.45), 1.4, 5.0, flicker=0.08)
    fireflies(s, 8000, (w, h), 90)
    s.add(GAME, "Menyu", NativeScript=graph_script("Menu"))
    s.add(MUSIC, "Musiqa", GAME, AudioSource=k.audio_source("Audio/music_menu.wav", 0.8, True, True, "Music", True))
    ui = UI(s, GAME)
    s.add(CANVAS, "UI", UICanvas=k.ui_canvas())
    ui.text(6010, "Nom", CANVAS, "CHIROQBON", (0, 1), (110, -96), (900, 150), 120, ACCENT, "Left", pivot=(0, 1), UIAnimation=k.ui_anim("Fade", "None", 0.8))
    ui.text(6011, "Shior", CANVAS, "Ko'hna qo'rg'on afsonasi", (0, 1), (118, -236), (900, 50), 36, INK, "Left", key="menu.subtitle", pivot=(0, 1))
    s.add(6020, "Asosiy", CANVAS, UITransform=k.ui_rect((0, 0.5), None, (0, 0.5), (110, -70), (540, 440)),
          UIImage=k.ui_image((1, 1, 1, 0.97), "Textures/ui.png", "panel", "Sliced", 1.0, raycast=True),
          UILayoutGroup=k.ui_layout("Vertical", (50, 50, 46, 46), 14, align=1, fit_height=True))
    main = [ui.button(6021, "Yangi o'yin", 6020, "Yangi o'yin", "menu.new", "New", (0, 0), (440, 64), select=True),
            ui.button(6022, "Davom etish", 6020, "Davom etish", "menu.continue", "Continue", (0, 0), (440, 64)),
            ui.button(6023, "Sozlamalar", 6020, "Sozlamalar", "menu.settings", "Settings", (0, 0), (440, 64)),
            ui.button(6024, "Mualliflar", 6020, "Mualliflar", "menu.credits", "Credits", (0, 0), (440, 64)),
            ui.button(6025, "Chiqish", 6020, "Chiqish", "menu.quit", "Quit", (0, 0), (440, 64))]
    ui.text(6030, "Rekord", CANVAS, "", (0, 0), (118, 92), (900, 40), 26, MUTED, "Left", pivot=(0, 0))
    ui.text(6031, "Boshqaruv", CANVAS, "[Move] yurish   [Attack] zarba   [Bolt] olov o'qi   [Interact] ishlatish   [Pause] pauza",
            (0.5, 0), (0, 40), (1700, 36), 22, MUTED, hints=True)
    ui.text(6032, "Versiya", CANVAS, "v1.0  Zamin Engine", (1, 0), (-24, 12), (400, 28), 18, MUTED, "Right", pivot=(1, 0))
    # settings
    ui.dim(6100, "Sozlamalar", 50)
    ui.panel(6101, "Sozlamalar paneli", 6100, (0, 0), (760, 560))
    ui.text(6102, "Sarlavha", 6101, "Sozlamalar", (0.5, 1), (0, -62), (600, 60), 44, ACCENT, key="menu.settings")
    for i, (eid, label, key) in enumerate(((6111, "Musiqa", "menu.music"), (6112, "Ovoz effektlari", "menu.sfx"))):
        ui.text(eid + 10, label, 6101, label, (0.5, 0.5), (-180, 130 - i * 90), (300, 40), 28, INK, "Left", key=key)
        ui.rect(eid, f"{label} slider", 6101, (0.5, 0.5), None, (150, 130 - i * 90), (320, 44),
                UISlider={"Min": 0.0, "Max": 1.0, "Value": 0.55 if i == 0 else 0.9, "WholeNumbers": False, "Direction": "LeftToRight", "Interactable": True,
                          "TrackThickness": 10.0, "HandleSize": 30.0, "Rounded": True, "TrackColor": [0.12, 0.1, 0.18, 1], "FillColor": list(ACCENT),
                          "HandleColor": [1.0, 0.95, 0.85, 1]}, UINavigation=k.ui_nav("Explicit", i == 0, ACCENT[:3] + (1,)))
    ui.text(6123, "To'liq ekran", 6101, "To'liq ekran", (0.5, 0.5), (-180, -50), (300, 40), 28, INK, "Left", key="menu.fullscreen")
    ui.rect(6113, "To'liq ekran toggle", 6101, (0.5, 0.5), None, (150, -50), (90, 44),
            UIToggle={"IsOn": False, "Interactable": True, "Style": "Switch", "Mark": "Check", "BoxSize": 34.0, "LabelSpacing": 8.0, "Group": "",
                      "AllowSwitchOff": True, "BoxColor": [0.14, 0.12, 0.2, 1], "OnColor": list(ACCENT), "MarkColor": [1, 1, 1, 1]},
            UIButton=k.ui_button((1, 1, 1, 1), (1.2, 1.2, 1.2, 1), (1, 1, 1, 1), "Fullscreen", GAME), UINavigation=k.ui_nav("Explicit", False, ACCENT[:3] + (1,)))
    st = [6111, 6112, 6113, ui.button(6114, "Til", 6101, "Til: O'zbekcha", "menu.language", "Language", (0, -130), (420, 60), 26),
          ui.button(6115, "Orqaga", 6101, "Orqaga", "menu.back", "Back", (0, -210), (420, 60), 28)]
    chain_nav(s, st)
    # credits
    ui.dim(6200, "Mualliflar", 51)
    ui.panel(6201, "Mualliflar paneli", 6200, (0, 0), (1100, 520))
    ui.text(6202, "Sarlavha", 6201, "Mualliflar", (0.5, 1), (0, -62), (600, 60), 44, ACCENT, key="menu.credits")
    ui.text(6210, "Matn", 6201, "", (0.5, 0.5), (0, 10), (980, 260), 26, INK, key="menu.credits.body", wrap=True)
    c = [ui.button(6211, "Orqaga", 6201, "Orqaga", "menu.back", "Back", (0, -190), select=True)]
    chain_nav(s, c)
    s.save(assets, project)




# ------------------------------------------------------------------------------------------------ graphs


def _vars(g, spec):
    for name, kind, default in spec:
        g.var(name, kind, default)


def V(g, name):
    return (g.get(name), "Value")


def hero_graph(assets, project, C):
    """The hero (same graph in every level): the whole campaign configuration, input and the demo pilot writing the
    same intent variables, motor/abilities/interaction/animation, death and respawn, shrine saves, unlocks, claims."""
    g = gk.Graph("Hero")
    _vars(g, (("Level", "Number", 1), ("Slot", "String", "Run1"), ("Pilot", "Bool", False), ("Plan", "Number", 0),
              ("Route", "ArrayVec3", []), ("Steps", "ArrayNumber", []), ("Step", "Number", 0), ("Facing", "Vec3", [0, -1, 0]),
              ("Deaths", "Number", 0), ("Defeated", "Bool", False), ("Hold", "Number", 0), ("Stuck", "Number", 0), ("Best", "Number", 999),
              ("MoveIn", "Vec2", [0, 0]), ("Attack", "Bool", False), ("Bolt", "Bool", False), ("Nova", "Bool", False), ("Mend", "Bool", False),
              ("Potion", "Bool", False), ("Use", "Bool", False), ("Victim", "Entity", 0), ("Lost", "Bool", False), ("Idle", "Bool", False),
              ("Sprint", "Bool", False), ("Reported", "Bool", False)))
    h = C["Hero"]
    # ---- configuration
    g.lane("config")
    start = g.n("Event.Start")
    ops = [("Gameplay.HealthConfigure", dict(Maximum=h["Health"], Shield=0, HitGrace=h["HitGrace"], RespawnDelay=h["RespawnDelay"], Team=h["Team"])),
           ("Gameplay.InventoryConfigure", dict(Slots=h["Slots"], Stack=h["Stack"])),
           ("Gameplay.LoadoutConfigure", dict(Stats=h["Stats"]))]
    for item, (slot, mods) in C["Items"].items():
        ops.append(("Gameplay.ItemDefine", dict(Item=item, Slot=slot, Modifiers=mods)))
    ops += [("Gameplay.EffectDefine", dict(Effect="Burn", Duration=2, Period=0.5, Tick=1, TickAmount=2, Stacking=0, MaxStacks=3)),
            ("Gameplay.EffectDefine", dict(Effect="Shock", Duration=0.8, Stun=True, Stacking=3, MaxStacks=1)),
            ("Gameplay.EffectDefine", dict(Effect="Regen", Duration=3, Period=0.5, Tick=2, TickAmount=4, Hostile=False, Stacking=0, MaxStacks=1)),
            ("Gameplay.EffectDefine", dict(Effect="Poison", Duration=3, Period=1, Tick=1, TickAmount=2, Stacking=2, MaxStacks=3)),
            ("Gameplay.ProjectileDefine", dict(Projectile="Bolt", Kind=0, Speed=14, Lifetime=1.0, Radius=0.1, TargetRadius=0.45, Damage=12, TwoD=True,
                                               BlockMask=WALL_MASK, Effect="Burn", Prefab="Prefabs/Bolt.zprefab", Pool=8)),
            ("Gameplay.ProjectileDefine", dict(Projectile="Arrow", Kind=0, Speed=11, Lifetime=1.2, Radius=0.08, TargetRadius=0.4, Damage=6, TwoD=True,
                                               BlockMask=WALL_MASK, Prefab="Prefabs/Arrow.zprefab", Pool=8)),
            ("Gameplay.ProjectileDefine", dict(Projectile="Orb", Kind=0, Speed=6.5, Lifetime=2.2, Radius=0.15, TargetRadius=0.5, Damage=7, Homing=0.8, TwoD=True,
                                               BlockMask=WALL_MASK, Effect="Poison", Prefab="Prefabs/Orb.zprefab", Pool=8)),
            ("Gameplay.ProjectileDefine", dict(Projectile="Spit", Kind=0, Speed=8, Lifetime=1.2, Radius=0.08, TargetRadius=0.4, Damage=4, TwoD=True,
                                               BlockMask=WALL_MASK, Prefab="Prefabs/Spit.zprefab", Pool=8)),
            ("Gameplay.AreaDefine", dict(Area="Nova", Kind=0, Amount=16, Radius=3, MaxTargets=8, TwoD=True, LineOfSight=True, Mask=WALL_MASK, Effect="Shock")),
            ("Gameplay.AreaDefine", dict(Area="Ring", Kind=0, Amount=12, Radius=3.5, MaxTargets=4, TwoD=True, LineOfSight=True, Mask=WALL_MASK))]
    for key, (kind, amount, cost, cool, rng_, ingredient, unlocked, proj, area, effect) in C["Abilities"].items():
        ops.append(("Gameplay.AbilityDefine", dict(Ability=key, Kind=kind, Amount=amount, Cost=cost, Cooldown=cool, Range=rng_, Ingredient=ingredient, Count=1,
                                                   Unlocked=unlocked, Projectile=proj, Area=area, Effect=effect)))
    for key, title, objectives, requires, rewards, xp in C["Quests"]:
        ops.append(("Gameplay.QuestConfigure", dict(Quest=key, Title=title, Objectives=[o[0] for o in objectives], Labels=[o[1] for o in objectives],
                                                    Kinds=[o[2] for o in objectives], Matches=[o[3] for o in objectives], Goals=[o[4] for o in objectives],
                                                    Requires=requires, Rewards=list(rewards), RewardCounts=list(rewards.values()), XP=xp)))
    ops.append(("Gameplay.SaveBind", dict(Key="Hero")))
    tail = g.steps(start, ops, "SLICE_CONFIG_FAILED hero")
    demo = g.any_of([V(g, "Pilot"), (g.n("App.DemoMode"), "Value"), (g.n("ReadState.Bool", Key=f"{GS}.Run.Demo", Default=False), "Value")])
    t = g.setv(tail, "Pilot", data=demo)
    g.setv(t, "Lost", data=(g.cmp("Greater", (g.n("ReadState.Number", Key=f"{GS}.Run.Lost"), "Value"), 0), "Value"))

    # ---- 1. human input into the intent variables (gameplay input is fenced while a blocking panel is open)
    g.lane("input")
    upd = g.n("Event.Update")
    raw = g.n("Gameplay.Input")
    t = g.setv((upd, "Then"), "MoveIn", data=(raw, "Move"))
    t = g.setv(t, "Sprint", data=(raw, "Sprint"))
    for var, action, held in (("Attack", "Attack", True), ("Bolt", "Bolt", True), ("Nova", "Nova", False), ("Mend", "Mend", False),
                              ("Potion", "Potion", False), ("Use", "Interact", False)):
        pressed = (g.n("ActionPressed" if held else "ActionJustPressed", Name=action), "Value")
        t = g.setv(t, var, data=g.all_of([(raw, "Enabled"), pressed]))
    g.setv(t, "Victim", 0)

    # ---- 2. the demo pilot: follows the generator's A* route; fights what it senses; codes at waypoints
    g.lane("pilot")
    pu = g.n("Event.Update")
    hp = g.n("Gameplay.Health")
    run = g.br((pu, "Then"), g.all_of([V(g, "Pilot"), (g.not_((hp, "Dead")), "Value"), (g.not_(V(g, "Idle")), "Value")]))
    idx = V(g, "Step")
    count = g.n("Array.Length.Vec3")
    g.l(g.get("Route"), "Value", count, "Array")
    has = g.br((run, "True"), (g.cmp("Less", idx, (count, "Value")), "Value"))
    g.setv((has, "False"), "MoveIn", [0, 0])
    goal = g.n("Array.Get.Vec3")
    g.l(g.get("Route"), "Value", goal, "Array")
    g.l(idx[0], idx[1], goal, "Index")
    code = g.n("Array.Get.Number")
    g.l(g.get("Steps"), "Value", code, "Array")
    g.l(idx[0], idx[1], code, "Index")
    sense = g.n("Gameplay.Sense", Tag="Enemy", Range=6.0, MinDot=-1, TwoD=True, LineOfSight=True, Mask=WALL_MASK, Origin=[0, 0, 0])
    may = g.any_of([(g.not_((g.cmp("Equal", V(g, "Plan"), 1), "Value")), "Value"), V(g, "Lost")])
    fight = g.br((has, "True"), g.all_of([(sense, "Visible"), may]))
    me = g.n("GetPosition")
    to = g.n("SubtractVec3")
    g.l(sense, "Position", to, "A")
    g.l(me, "Value", to, "B")
    face = g.n("NormalizeVec3")
    g.l(to, "Value", face, "Vector")
    t = g.setv((fight, "True"), "MoveIn", [0, 0])
    t = g.setv(t, "Facing", data=(face, "Value"))
    t = g.setv(t, "Victim", data=(sense, "Victim"))
    t = g.setv(t, "Attack", data=(g.cmp("Less", (sense, "Distance"), 1.4), "Value"))
    t = g.setv(t, "Bolt", True)
    t = g.setv(t, "Nova", data=(g.cmp("Less", (sense, "Distance"), 2.6), "Value"))
    g.setv(t, "Hold", 0)
    steer = g.n("Gameplay.Steer", Arrive=0.32, Slow=0.5, TwoD=True)
    g.l(goal, "Value", steer, "Goal")
    t = g.setv((fight, "False"), "MoveIn", data=(steer, "Move"))
    better = g.br(t, (g.cmp("Less", (steer, "Distance"), g.num("Subtract", V(g, "Best"), 0.25)), "Value"))
    r = g.setv((better, "True"), "Best", data=(steer, "Distance"))
    r = g.setv(r, "Stuck", 0)
    s_ = g.setv((better, "False"), "Stuck", data=g.num("Add", V(g, "Stuck"), (pu, "Delta")))
    stuck = g.br(s_, g.all_of([(g.cmp("Greater", V(g, "Stuck"), 8), "Value"), (g.not_(V(g, "Reported")), "Value")]))
    rp = g.setv((stuck, "True"), "Reported", True)
    pr = g.ex(rp, "Print")
    g.l(*g.text(["SLICE_BOT_STUCK lvl=", (g.get("Level"), "Value", "number"), " step=", (g.get("Step"), "Value", "number")]), pr, "Text")
    arrived = g.br(g.join(r, (stuck, "False"), (pr, "Then")), (steer, "Arrived"))

    def step_next(src):
        n1 = g.setv(src, "Step", data=g.num("Add", idx, 1))
        n1 = g.setv(n1, "Best", 999)
        n1 = g.setv(n1, "Stuck", 0)
        g.setv(n1, "Hold", 0)
    is_code = lambda v: (g.cmp("Equal", (code, "Value"), v), "Value")
    c0 = g.br((arrived, "True"), is_code(0))
    step_next((c0, "True"))
    c2 = g.br((c0, "False"), is_code(2))
    step_next(g.setv((c2, "True"), "Use", True))
    c3 = g.br((c2, "False"), is_code(3))
    held = g.br(g.setv((c3, "True"), "Hold", data=g.num("Add", V(g, "Hold"), (pu, "Delta"))), (g.cmp("Greater", V(g, "Hold"), 1.2), "Value"))
    step_next((held, "True"))
    c4 = g.br((c3, "False"), is_code(4))
    g.setv((c4, "False"), "Stuck", 0)  # 7: finished, waiting for the level flow
    t = (c4, "True")
    for item in C["Items"]:
        t = (g.ex(t, "Gameplay.EquipmentEquip", Item=item), "Then")
    step_next(t)

    # ---- 3. the pilot drinks and mends when hurt
    g.lane("pilot2")
    ph = g.n("Event.Update")
    hp2 = g.n("Gameplay.Health")
    low = g.br((ph, "Then"), g.all_of([V(g, "Pilot"), (g.not_((hp2, "Dead")), "Value"), (g.cmp("Less", (hp2, "Current"), 45), "Value")]))
    g.setv(g.setv((low, "True"), "Potion", True), "Mend", True)

    # ---- 4. movement, facing, strikes and abilities
    g.lane("act")
    au = g.n("Event.Update")
    motor = g.ex((au, "Then"), "Gameplay.Motor2D", TopDown=True, Speed=h["Speed"], Acceleration=h["Acceleration"], Deceleration=34, SprintMultiplier=1.5)
    g.l(g.get("MoveIn"), "Value", motor, "Move")
    g.l(g.get("Sprint"), "Value", motor, "Sprint")
    g.l(au, "Delta", motor, "Delta")
    length = g.n("LengthVec2")
    g.l(g.get("MoveIn"), "Value", length, "Vector")
    mb = g.br((motor, "Then"), (g.cmp("Greater", (length, "Value"), 0.2), "Value"))
    mv = g.n("BreakVec2")
    g.l(g.get("MoveIn"), "Value", mv, "Vector")
    mk = g.n("MakeVec3", Z=0)
    g.l(mv, "X", mk, "X")
    g.l(mv, "Y", mk, "Y")
    nf = g.n("NormalizeVec3")
    g.l(mk, "Value", nf, "Vector")
    a1, a2, a3 = g.fork(g.join(g.setv((mb, "True"), "Facing", data=(nf, "Value")), (mb, "False")))
    ba = g.br(a1, V(g, "Attack"))
    melee = g.ex((ba, "True"), "Gameplay.Attack", Range=h["MeleeRange"], MinDot=0.3, TwoD=True, LineOfSight=True, Mask=WALL_MASK, Origin=[0, 0, 0],
                 Amount=h["Melee"], Cooldown=h["MeleeCooldown"])
    g.l(g.get("Facing"), "Value", melee, "Facing")
    hit = g.br((melee, "Then"), (melee, "Success"))
    trig = g.ex((hit, "True"), "Animator.SetTrigger", Target=HERO_VISUAL, Name="Attack")
    g.ex((trig, "Then"), "PlaySound", Path="Audio/sfx_swing.wav", Volume=0.7)
    bb = g.br(a2, V(g, "Bolt"))
    cast = g.ex((bb, "True"), "Gameplay.AbilityCast", Ability="Bolt", TwoD=True, LineOfSight=True, Mask=WALL_MASK)
    g.l(g.get("Victim"), "Value", cast, "Victim")
    g.l(g.get("Facing"), "Value", cast, "Direction")
    ok = g.br((cast, "Then"), (cast, "Success"))
    g.ex((g.ex((ok, "True"), "PlaySound", Path="Audio/sfx_bolt.wav", Volume=0.55), "Then"), "Animator.SetTrigger", Target=HERO_VISUAL, Name="Attack")
    for src, (var, ability, sound) in zip(g.fork(a3), (("Nova", "Nova", "sfx_nova"), ("Mend", "Mend", "sfx_levelup"), ("Potion", "Potion", "sfx_coin"))):
        b = g.br(src, V(g, var))
        c_ = g.ex((b, "True"), "Gameplay.AbilityCast", Ability=ability, TwoD=True, LineOfSight=True, Mask=WALL_MASK)
        ok = g.br((c_, "Then"), (c_, "Success"))
        snd = g.ex((ok, "True"), "PlaySound", Path=f"Audio/{sound}.wav", Volume=0.6)
        if ability == "Nova":
            g.ex((snd, "Then"), "ShakeCamera", Intensity=0.3, Duration=0.35)

    # ---- 5. interaction (pickups automatically, everything else on Interact), prompt, animation
    g.lane("interact")
    late = g.n("Event.LateUpdate")
    find = g.n("Gameplay.InteractionFind", Range=1.6, MinDot=-1, TwoD=True, LineOfSight=True, Mask=WALL_MASK, Origin=[0, 0, 0])
    pick = g.n("HasTag", Tag="Pickup")
    g.l(find, "Object", pick, "Target")
    auto = g.all_of([(find, "Found"), (pick, "Value"), (g.cmp("Less", (find, "Distance"), 0.95), "Value")])
    want = g.all_of([(find, "Found"), V(g, "Use")])
    go = g.br((late, "Then"), g.any_of([auto, want]))
    inter = g.ex((go, "True"), "Gameplay.Interact", Range=1.6, MinDot=-1, TwoD=True, LineOfSight=True, Mask=WALL_MASK, Origin=[0, 0, 0])
    g.l(find, "Object", inter, "Target")
    p1, p2, _ = g.fork(g.join((go, "False"), (inter, "Then")))
    show = g.br(p1, g.all_of([(find, "Found"), (g.not_((pick, "Value")), "Value")]))
    prompt = g.ex((show, "True"), "SetUIText", Target=6090)
    g.l(*g.text(["[Interact] ", (find, "Prompt")]), prompt, "Text")
    g.ex((show, "False"), "SetUIText", Target=6090, Text="")
    anim = g.ex(p2, "Gameplay.BindAnimator", Target=HERO_VISUAL, Source=HERO, TwoD=True, TopDown=True, ReferenceSpeed=h["Speed"] * 0.75, Damping=0.06)
    g.l(late, "Delta", anim, "Delta")

    # ---- 6. rule events: deaths (three end the run), shrine saves, collected items, hurt feedback
    g.lane("events")
    ev = g.n("Event.Update")
    for src in g.fork((ev, "Then")):
        poll = g.ex(src, "Gameplay.RuleEvent")
        has_ = g.br((poll, "Then"), (poll, "Has"))
        kind = (poll, "Kind")
        died = g.br((has_, "True"), (g.text_is(kind, "Died"), "Value"))
        t = g.setv((died, "True"), "Deaths", data=g.num("Add", V(g, "Deaths"), 1))
        t = (g.ex(t, "PlaySound", Path="Audio/sfx_death.wav", Volume=0.8), "Then")
        t = (g.ex(t, "Print", Text="SLICE_TRACE ev=death"), "Then")
        three = g.br(t, (g.cmp("Greater", V(g, "Deaths"), 2.5), "Value"))
        g.ex(g.setv((three, "True"), "Defeated", True), "SendMessage", Target=GAME, Name="Defeat", Text="")
        g.ex((three, "False"), "SendMessage", Target=GAME, Name="Banner", Text="Yiqildingiz...")
        used = g.br((died, "False"), (g.text_is(kind, "Interacted"), "Value"))
        valid = g.n("EntityValid")
        g.l(poll, "Subject", valid, "Target")
        tag = g.n("HasTag", Tag="Shrine")
        g.l(poll, "Subject", tag, "Target")
        shrine = g.br((used, "True"), g.all_of([(valid, "Value"), (tag, "Value")]))
        save = g.ex((shrine, "True"), "Gameplay.SaveWrite", Schema=C["Schema"])
        g.l(g.get("Slot"), "Value", save, "Slot")
        saved = g.br((save, "Then"), (save, "Success"))
        fail = g.ex((saved, "False"), "Print")
        g.l(*g.text(["SLICE_IO_FAILED save ", (save, "Reason")]), fail, "Text")
        heal = g.ex((saved, "True"), "Gameplay.Heal", Amount=999)  # a shrine restores the keeper after the save
        cap = g.ex((heal, "Then"), "Gameplay.CheckpointCapture", Slot="Hero", Persistent=False)
        ws = g.ex((cap, "Then"), "WriteState.Number", Key=f"{GS}.Run.Save")
        g.l(g.get("Level"), "Value", ws, "Value")
        wst = g.ex((ws, "Then"), "WriteState.Number", Key=f"{GS}.Run.Step")
        g.l(g.get("Step"), "Value", wst, "Value")
        msg = g.ex((wst, "Then"), "SendMessage", Target=GAME, Name="Saved", Text="shrine")
        stop = g.br((msg, "Then"), g.all_of([(g.cmp("Equal", V(g, "Plan"), 3), "Value"), (g.cmp("Equal", V(g, "Level"), 2), "Value")]))
        g.setv((stop, "True"), "Idle", True)
        col = g.br((used, "False"), (g.text_is(kind, "Collected"), "Value"))
        nxt = (col, "True")
        for item, sound, banner in (("Lamp", "sfx_lamp", "Chiroq topildi!"), ("Coin", "sfx_coin", ""), ("Herb", "sfx_coin", "Shifobaxsh o't topildi"),
                                    ("Qilich", "sfx_chest", "Qilich! [Inventory] - inventarda kiying"), ("Zirh", "sfx_chest", "Zirh topildi!"),
                                    ("Potion", "sfx_coin", "Ichimlik topildi")):
            ib = g.br(nxt, (g.text_is((poll, "Item"), item), "Value"))
            t = (g.ex((ib, "True"), "PlaySound", Path=f"Audio/{sound}.wav", Volume=0.7), "Then")
            t = (g.ex(t, "Print", Text=f"SLICE_TRACE ev=collect a={item}"), "Then")
            if banner:
                g.ex(t, "SendMessage", Target=GAME, Name="Banner", Text=banner)
            nxt = (ib, "False")
        hurt = g.br((col, "False"), (g.text_is(kind, "Damage"), "Value"))
        g.ex((hurt, "True"), "SendMessage", Target=GAME, Name="Hurt", Text="")

    # ---- 7. respawn at the last checkpoint until defeated; death pose
    g.lane("respawn")
    rs = g.n("Event.Update")
    hp3 = g.n("Gameplay.Health")
    r1, r2, _ = g.fork((rs, "Then"))
    due = g.br(r1, g.all_of([(hp3, "Dead"), (g.not_(V(g, "Defeated")), "Value"), (g.cmp("Less", (hp3, "RespawnIn"), 0.001), "Value")]))
    resp = g.ex((due, "True"), "Gameplay.Respawn", Slot="Hero", Persistent=False, Grace=1.5)
    g.ex((resp, "Then"), "Animator.SetBool", Target=HERO_VISUAL, Name="Dead", Value=False)
    dead = g.br(r2, (hp3, "Dead"))
    g.ex((dead, "True"), "Animator.SetBool", Target=HERO_VISUAL, Name="Dead", Value=True)

    # ---- 8. progression: unlocks at XP levels 2 and 3, automatic claims
    g.lane("progress")
    pg = g.n("Event.Update")
    lvl = g.n("Gameplay.QuestProgression")
    src = (pg, "Then")
    for ability, at, banner in (("Nova", 2, "Yangi daraja! Yorug' to'lqin ochildi [Nova]"), ("Mend", 3, "Yangi daraja! Tiklanish ochildi [Mend]")):
        st = g.n("Gameplay.AbilityState", Ability=ability)
        b = g.br(src, g.all_of([(g.cmp("Greater", (lvl, "Level"), at - 0.5), "Value"), (g.not_((st, "Unlocked")), "Value")]))
        u = g.ex((b, "True"), "Gameplay.AbilityUnlock", Ability=ability)
        m = g.ex((u, "Then"), "SendMessage", Target=GAME, Name="LevelUp", Text=banner)
        src = g.join((b, "False"), (m, "Then"))
    for key, *_ in C["Quests"]:
        qs = g.n("Gameplay.QuestState", Quest=key)
        b = g.br(src, (qs, "Ready"))
        cl = g.ex((b, "True"), "Gameplay.QuestClaim", Quest=key)
        m = g.ex((cl, "Then"), "SendMessage", Target=GAME, Name="Claimed", Text=key)
        src = g.join((b, "False"), (m, "Then"))

    # ---- 9. messages: a resumed save restores the pilot's route position
    g.lane("messages")
    msg = g.n("Event.Message")
    b = g.br((msg, "Then"), (g.text_is((msg, "Text"), "Resumed"), "Value"))
    g.setv((b, "True"), "Step", data=(g.n("ReadState.Number", Key=f"{GS}.Run.Step"), "Value"))
    g.save(assets, "Scripts/Hero.zgraph", project)
    return g


QUEST_ROWS = [("Gate", "Chief"), ("Gate", "Gate"), ("Herbs", "Herb"), ("Lamps", "Lamp"), ("Boss", "Boss"), ("Dawn", "Altar")]


def game_graph(assets, project, C):
    """The level controller (same graph in both levels): panels and HUD, interactables, entry (new / carry / continue),
    level completion with the transfer, boss phases and music, victory and defeat, messages, run time."""
    g = gk.Graph("Game")
    _vars(g, (("Level", "Number", 1), ("Slot", "String", "Run1"), ("Next", "String", ""), ("Self", "String", ""), ("Title", "String", ""),
              ("Demo", "Bool", False), ("PickupIds", "ArrayEntity", []), ("PickupKeys", "ArrayString", []), ("PickupItems", "ArrayString", []),
              ("UseIds", "ArrayEntity", []), ("UseKeys", "ArrayString", []), ("UsePrompts", "ArrayString", []), ("UseRequired", "ArrayString", []),
              ("UseCounts", "ArrayNumber", []), ("UseConsume", "ArrayBool", []), ("UseRewards", "ArrayString", []), ("UseHeal", "ArrayNumber", []),
              ("UseMaxUses", "ArrayNumber", []), ("UseCooldown", "ArrayNumber", []), ("UseDeactivate", "ArrayBool", []), ("HasExit", "Bool", False),
              ("Exit", "Vec3", [0, 0, 0]), ("Boss", "Entity", 0), ("Wisps", "ArrayEntity", []), ("Loaded", "Bool", False), ("Time", "Number", 0),
              ("Completing", "Bool", False), ("Ended", "Bool", False), ("Banner", "Number", 0), ("Hurt", "Number", 0), ("TitleTime", "Number", 3.5),
              ("Phase2", "Bool", False), ("BossSeen", "Bool", False), ("BossDown", "Bool", False), ("Auto", "Number", -1), ("Entry", "String", "new")))
    schema = C["Schema"]
    # ---- configuration
    g.lane("config")
    start = g.n("Event.Start")
    ops = [("Gameplay.UIConfigure", dict(Target=6100, Name="Pause", Action="Pause", Focus=6111, Pause=True, Block=True, Back=True)),
           ("Gameplay.UIConfigure", dict(Target=6200, Name="Bag", Action="Inventory", Focus=6211, Pause=False, Block=True, Back=True)),
           ("Gameplay.UIConfigure", dict(Target=6300, Name="Defeat", Action="", Focus=6311, Pause=True, Block=True, Back=False)),
           ("Gameplay.UIConfigure", dict(Target=6400, Name="Victory", Action="", Focus=6411, Pause=True, Block=True, Back=False)),
           ("Gameplay.UIConfigure", dict(Target=6500, Name="Card", Action="", Focus=0, Pause=False, Block=True, Back=False)),
           ("Gameplay.UIBindHUD", dict(Target=6012, Actor=HERO, Kind=9, Item="Coin", Prefix="", Ratio=False)),
           ("Gameplay.UIBindHUD", dict(Target=6013, Actor=HERO, Kind=9, Item="Coin", Prefix="", Ratio=False)),
           ("Gameplay.UIBindHUD", dict(Target=6014, Actor=HERO, Kind=0, Item="Coin", Prefix="", Ratio=True)),
           ("Gameplay.UIBindHUD", dict(Target=6015, Actor=HERO, Kind=7, Item="Coin", Prefix="", Ratio=True))]
    for widget, item in ((6022, "Coin"), (6024, "Lamp"), (6027, "Key"), (6029, "Potion")):
        ops.append(("Gameplay.UIBindHUD", dict(Target=widget, Actor=HERO, Kind=2, Item=item, Prefix="", Ratio=False)))
    for i, ability in enumerate(("Bolt", "Nova", "Mend", "Potion")):
        ops.append(("Gameplay.UIBindHUD", dict(Target=6047 + i * 4, Actor=HERO, Kind=8, Item=ability, Prefix="", Ratio=False)))
    for i, (quest, objective) in enumerate(QUEST_ROWS):
        ops.append(("Gameplay.QuestBindHUD", dict(Target=6032 + i, Actor=HERO, Quest=quest, Objective=objective)))
    ops += [("Gameplay.UICombatFeed", dict(Actor=HERO, Name="Feed", Rows=[6070, 6071, 6072, 6073], Outgoing=True, Incoming=True, Lifetime=2.5)),
            ("Gameplay.UIInventoryConfigure", dict(Actor=HERO, Name="Bag", Rows=[6211 + i for i in range(6)])),
            ("Gameplay.UIRunWhenPaused", dict(Enabled=True))]
    tail = g.steps(start, ops, "SLICE_CONFIG_FAILED ui")
    # pickups: automatic one-shot rewards
    fe = g.ex(tail, "ForEach.Entity")
    g.l(g.get("PickupIds"), "Value", fe, "Array")
    item = g.n("Array.Get.String")
    g.l(g.get("PickupItems"), "Value", item, "Array")
    g.l(fe, "Index", item, "Index")
    key = g.n("Array.Get.String")
    g.l(g.get("PickupKeys"), "Value", key, "Array")
    g.l(fe, "Index", key, "Index")
    ic = g.ex((fe, "Body"), "Gameplay.InteractionConfigure", Prompt="", RequiredItem="", RequiredCount=1, Consume=False, RewardCount=1, Heal=0, MaxUses=1,
              Cooldown=0, Deactivate=True, Enabled=True, Offset=[0, 0, 0])
    g.wire(ic, Target=(fe, "Value"), RewardItem=(item, "Value"))
    sb = g.ex((ic, "Then"), "Gameplay.SaveBind")
    g.wire(sb, Target=(fe, "Value"), Key=(key, "Value"))
    okp = g.br((sb, "Then"), g.all_of([(ic, "Success"), (sb, "Success")]))
    bad = g.ex((okp, "False"), "Print")
    g.l(*g.text(["SLICE_CONFIG_FAILED pickup ", (key, "Value"), " ", (ic, "Reason"), (sb, "Reason")]), bad, "Text")
    # usable objects: chests, shrines, the gate, the altar
    fu = g.ex((fe, "Completed"), "ForEach.Entity")
    g.l(g.get("UseIds"), "Value", fu, "Array")
    pins = {}
    for pin, var, kind in (("Prompt", "UsePrompts", "String"), ("RequiredItem", "UseRequired", "String"), ("RequiredCount", "UseCounts", "Number"),
                           ("Consume", "UseConsume", "Bool"), ("RewardItem", "UseRewards", "String"), ("Heal", "UseHeal", "Number"),
                           ("MaxUses", "UseMaxUses", "Number"), ("Cooldown", "UseCooldown", "Number"), ("Deactivate", "UseDeactivate", "Bool")):
        getter = g.n(f"Array.Get.{kind}")
        g.l(g.get(var), "Value", getter, "Array")
        g.l(fu, "Index", getter, "Index")
        pins[pin] = (getter, "Value")
    uc = g.ex((fu, "Body"), "Gameplay.InteractionConfigure", RewardCount=1, Enabled=True, Offset=[0, 0, 0])
    g.wire(uc, Target=(fu, "Value"), **pins)
    ukey = g.n("Array.Get.String")
    g.l(g.get("UseKeys"), "Value", ukey, "Array")
    g.l(fu, "Index", ukey, "Index")
    usb = g.ex((uc, "Then"), "Gameplay.SaveBind")
    g.wire(usb, Target=(fu, "Value"), Key=(ukey, "Value"))
    oku = g.br((usb, "Then"), g.all_of([(uc, "Success"), (usb, "Success")]))
    badu = g.ex((oku, "False"), "Print")
    g.l(*g.text(["SLICE_CONFIG_FAILED use ", (ukey, "Value"), " ", (uc, "Reason"), (usb, "Reason")]), badu, "Text")
    # settings: volumes and the level title
    t = (fu, "Completed")
    for group, setting, default in (("Music", "Music", 0.55), ("SFX", "Sfx", 0.9)):
        vol = g.ex(t, "Audio.SetGroupVolume", Group=group)
        g.l(g.n("ReadState.Number", Key=f"{GS}.Settings.{setting}", Default=default), "Value", vol, "Volume")
        t = (vol, "Then")
    title = g.ex(t, "SetUIText", Target=6092)
    g.l(g.get("Title"), "Value", title, "Text")

    # ---- entry (first LateUpdate, after every Start): continue from the level's save, carry, or a fresh start
    g.lane("entry")
    lu = g.n("Event.LateUpdate")
    first = g.br((lu, "Then"), (g.not_(V(g, "Loaded")), "Value"))
    t = g.setv((first, "True"), "Loaded", True)
    entry = g.n("ReadState.String", Key=f"{GS}.Run.Entry", Default="new")
    t = g.setv(t, "Entry", data=(entry, "Value"))
    t = g.setv(t, "Time", data=(g.n("ReadState.Number", Key=f"{GS}.Run.Time", Default=0), "Value"))
    saved_here = g.cmp("Equal", (g.n("ReadState.Number", Key=f"{GS}.Run.Save"), "Value"), V(g, "Level"))
    cont = g.br(t, g.all_of([(g.text_is(V(g, "Entry"), "continue"), "Value"), (saved_here, "Value")]))
    load = g.ex((cont, "True"), "Gameplay.SaveLoad", Schema=schema)
    g.l(g.get("Slot"), "Value", load, "Slot")
    loaded = g.br((load, "Then"), (load, "Success"))
    rec = g.br((loaded, "True"), (load, "Recovered"))
    r1 = g.ex((rec, "True"), "Print")
    g.l(*g.text(["SLICE_RESUMED lvl=", (g.get("Level"), "Value", "number"), " source=save recovered=1"]), r1, "Text")
    r1b = g.ex((r1, "Then"), "SendMessage", Target=GAME, Name="Banner", Text="Zaxira nusxadan tiklandi")
    r0 = g.ex((rec, "False"), "Print")
    g.l(*g.text(["SLICE_RESUMED lvl=", (g.get("Level"), "Value", "number"), " source=save recovered=0"]), r0, "Text")
    resumed = g.ex(g.join((r1b, "Then"), (r0, "Then")), "SendMessage", Target=HERO, Name="Resumed", Text="")
    lf = g.ex((loaded, "False"), "Print")
    g.l(*g.text(["SLICE_IO_FAILED load ", (load, "Reason")]), lf, "Text")
    # a level exit (carry) or a Continue without a level 2 save reads the transfer; a direct start of level 2 (Editor Play,
    # StartScene: Entry "new") is a fresh run and never picks up a stale Carry.json
    fresh = g.br(g.join((cont, "False"), (lf, "Then")),
                 g.all_of([(g.cmp("Equal", V(g, "Level"), 2), "Value"), (g.not_((g.text_is(V(g, "Entry"), "new"), "Value")), "Value")]))
    carry = g.ex((fresh, "True"), "Gameplay.TransferLoad", Key="Hero", Slot="Carry", Schema=schema)
    carried = g.br((carry, "Then"), (carry, "Success"))
    heal = g.ex((carried, "True"), "Gameplay.Heal", Target=HERO, Amount=999)
    pc = g.ex((heal, "Then"), "Print", Text="SLICE_RESUMED lvl=2 source=carry recovered=0")
    cf = g.ex((carried, "False"), "Print")
    g.l(*g.text(["SLICE_IO_FAILED transfer ", (carry, "Reason")]), cf, "Text")
    fresh1 = g.setv((fresh, "False"), "Time", 0)
    auto = g.ex(g.join((pc, "Then"), (cf, "Then"), fresh1), "Gameplay.SaveWrite", Schema=schema)
    g.l(g.get("Slot"), "Value", auto, "Slot")
    autook = g.br((auto, "Then"), (auto, "Success"))
    ws = g.ex((autook, "True"), "WriteState.Number", Key=f"{GS}.Run.Save")
    g.l(g.get("Level"), "Value", ws, "Value")
    pa = g.ex((ws, "Then"), "Print")
    g.l(*g.text(["SLICE_SAVED lvl=", (g.get("Level"), "Value", "number"), " at=auto"]), pa, "Text")
    af = g.ex((autook, "False"), "Print")
    g.l(*g.text(["SLICE_IO_FAILED autosave ", (auto, "Reason")]), af, "Text")
    common = g.join((resumed, "Then"), (pa, "Then"), (af, "Then"))
    t = (g.ex(common, "WriteState.String", Key=f"{GS}.Run.Entry", Value="continue"), "Then")
    wl = g.ex(t, "WriteState.Number", Key=f"{GS}.Run.Level")
    g.l(g.get("Level"), "Value", wl, "Value")
    # campaign quests start once (a carried or loaded book keeps its modes): level 1 the orchard quests, level 2 the boss
    t = (wl, "Then")
    for quest, level_only in (("Gate", 0), ("Herbs", 0), ("Lamps", 0), ("Boss", 2)):
        mode = g.n("Gameplay.QuestState", Target=HERO, Quest=quest)
        terms = [(g.text_is((mode, "Mode"), "Locked"), "Value")]
        if level_only:
            terms.insert(0, (g.cmp("Equal", V(g, "Level"), level_only), "Value"))
        b = g.br(t, g.all_of(terms))
        qs = g.ex((b, "True"), "Gameplay.QuestStart", Target=HERO, Quest=quest)
        t = g.join((b, "False"), (qs, "Then"))
    cap = g.ex(t, "Gameplay.CheckpointCapture", Target=HERO, Slot="Hero", Persistent=False)
    st = g.ex((cap, "Then"), "Print")
    g.l(*g.text(["SLICE_LEVEL_START lvl=", (g.get("Level"), "Value", "number"), " entry=", V(g, "Entry")]), st, "Text")
    g.ex((st, "Then"), "SaveGameState")

    # ---- per frame: run time, banner/vignette/title timers
    g.lane("timers")
    up = g.n("Event.Update")
    f1, f2, f3 = g.fork((up, "Then"))
    clock = g.br(f1, g.all_of([V(g, "Loaded"), (g.not_(V(g, "Ended")), "Value")]))
    t = g.setv((clock, "True"), "Time", data=g.num("Add", V(g, "Time"), (up, "Delta")))
    timers = g.fork(g.join(t, (clock, "False")))
    for src, var, action in zip(timers, ("Banner", "Hurt", "TitleTime"), (("SetUIText", dict(Target=6091, Text="")), ("SetActive", dict(Target=6080, Active=False)),
                                                                          ("SetActive", dict(Target=6092, Active=False)))):
        active = g.br(src, (g.cmp("Greater", V(g, var), 0), "Value"))
        dec = g.setv((active, "True"), var, data=g.num("Subtract", V(g, var), (up, "Delta")))
        done = g.br(dec, (g.cmp("Less", V(g, var), 0.0001), "Value"))
        g.ex((done, "True"), action[0], **action[1])

    # ---- level flow: exit (level 1), dawn quest, victory (level 2)
    hero_pos = g.n("GetPosition", Target=HERO)
    dist = g.n("DistanceVec3")
    g.l(hero_pos, "Value", dist, "A")
    g.l(g.get("Exit"), "Value", dist, "B")
    gate = g.n("Gameplay.QuestState", Target=HERO, Quest="Gate")
    ex1, ex2, ex3 = g.fork(f2)
    exit_ = g.br(ex1, g.all_of([V(g, "HasExit"), V(g, "Loaded"), (g.not_(V(g, "Completing")), "Value"), (gate, "Claimed"), (g.cmp("Less", (dist, "Value"), 1.1), "Value")]))
    t = g.setv((exit_, "True"), "Completing", True)
    t = (g.ex(t, "Gameplay.UIOpen", Name="Card"), "Then")
    prog = g.n("Gameplay.QuestProgression", Target=HERO)
    coins = g.n("Gameplay.InventoryCount", Target=HERO, Item="Coin")
    hp = g.n("Gameplay.Health", Target=HERO)
    card = g.ex(t, "SetUIText", Target=6510)
    g.l(*g.text(["Vaqt  ", (g.get("Time"), "Value", "time"), "     Tangalar  ", (coins, "Count", "number"), "     Tajriba  ", (prog, "XP", "number")]), card, "Text")
    pl = g.ex((card, "Then"), "Print")
    g.l(*g.text(["SLICE_LEVEL_COMPLETE lvl=1 time=", (g.get("Time"), "Value", "number", 1), " coins=", (coins, "Count", "number"), " xp=", (prog, "XP", "number"),
                 " deaths=", (hp, "Deaths", "number")]), pl, "Text")
    snd = g.ex((pl, "Then"), "PlaySound", Path="Audio/sfx_levelup.wav", Volume=0.8)
    wait = g.ex((snd, "Then"), "Delay", Seconds=2.5)
    tw = g.ex((wait, "Then"), "Gameplay.TransferWrite", Key="Hero", Slot="Carry", Schema=schema)
    twok = g.br((tw, "Then"), (tw, "Success"))
    twf = g.ex((twok, "False"), "Print")
    g.l(*g.text(["SLICE_IO_FAILED transfer ", (tw, "Reason")]), twf, "Text")
    t = (g.ex((twok, "True"), "WriteState.Number", Key=f"{GS}.Run.Level", Value=2), "Then")
    t = (g.ex(t, "WriteState.Number", Key=f"{GS}.Run.Save", Value=0), "Then")
    t = (g.ex(t, "WriteState.String", Key=f"{GS}.Run.Entry", Value="carry"), "Then")
    wtime = g.ex(t, "WriteState.Number", Key=f"{GS}.Run.Time")
    g.l(g.get("Time"), "Value", wtime, "Value")
    t = (g.ex((wtime, "Then"), "SaveGameState"), "Then")
    nxt = g.ex(t, "LoadScene", FadeOut=0.6, FadeIn=0.6, Color=[0.02, 0.01, 0.04, 1])
    g.l(g.get("Next"), "Value", nxt, "Path")
    dawn = g.n("Gameplay.QuestState", Target=HERO, Quest="Dawn")
    lamps = g.n("Gameplay.QuestState", Target=HERO, Quest="Lamps")
    boss_q = g.n("Gameplay.QuestState", Target=HERO, Quest="Boss")
    ds = g.br(ex2, g.all_of([V(g, "Loaded"), (g.text_is((dawn, "Mode"), "Locked"), "Value"), (lamps, "Claimed")]))
    qs = g.ex((ds, "True"), "Gameplay.QuestStart", Target=HERO, Quest="Dawn")
    g.ex((qs, "Then"), "SendMessage", Target=GAME, Name="Banner", Text="Uch chiroq yig'ildi! Mehrobga boring")
    win = g.br(ex3, g.all_of([(g.cmp("Equal", V(g, "Level"), 2), "Value"), (g.not_(V(g, "Ended")), "Value"), (boss_q, "Claimed"), (dawn, "Claimed")]))
    t = g.setv((win, "True"), "Ended", True)
    t = (g.ex(t, "WriteState.Number", Key=f"{GS}.Run.Finished", Value=1), "Then")
    best = g.n("ReadState.Number", Key=f"{GS}.Best.Time", Default=0)
    record = g.br(t, g.any_of([(g.cmp("Less", (best, "Value"), 0.001), "Value"), (g.cmp("Less", V(g, "Time"), (best, "Value")), "Value")]))
    wb = g.ex((record, "True"), "WriteState.Number", Key=f"{GS}.Best.Time")
    g.l(g.get("Time"), "Value", wb, "Value")
    t = (g.ex(g.join((wb, "Then"), (record, "False")), "SaveGameState"), "Then")
    t = (g.ex(t, "Gameplay.UIOpen", Name="Victory"), "Then")
    stats = g.ex(t, "SetUIText", Target=6410)
    best2 = g.n("ReadState.Number", Key=f"{GS}.Best.Time", Default=0)
    g.l(*g.text(["Vaqt  ", (g.get("Time"), "Value", "time"), "\nO'limlar  ", (hp, "Deaths", "number"), "\nTangalar  ", (coins, "Count", "number"),
                 "\nTajriba  ", (prog, "XP", "number"), "  (", (prog, "Level", "number"), "-daraja)\nEng yaxshi vaqt  ", (best2, "Value", "time")]), stats, "Text")
    allq = g.all_of([(g.n("Gameplay.QuestState", Target=HERO, Quest=q[0]), "Claimed") for q in C["Quests"]])
    pv = g.ex((stats, "Then"), "Print")
    g.l(*g.text(["SLICE_VICTORY game=Chiroqbon time=", (g.get("Time"), "Value", "number", 1), " xp=", (prog, "XP", "number"), " level=", (prog, "Level", "number"),
                 " coins=", (coins, "Count", "number"), " deaths=", (hp, "Deaths", "number"), " quests=", (g.n("Constant.String", Value="5/5"), "Value")]), pv, "Text")
    qb = g.br((pv, "Then"), allq)
    g.ex((qb, "False"), "Print", Text="SLICE_ORACLE_FAILED quests")
    t = (g.ex((qb, "True"), "PlaySound", Path="Audio/sfx_victory.wav", Volume=0.9), "Then")
    t = (g.ex(t, "FadeAudio", Target=MUSIC, Volume=0.25, Seconds=1.0), "Then")
    # the attract loop (ZAMIN_DEMO or the Demo override) returns to the menu; a played game stays on the screen
    attract = g.br(t, g.any_of([V(g, "Demo"), (g.n("App.DemoMode"), "Value")]))
    g.setv((attract, "True"), "Auto", 5.0)

    # ---- boss: music and bar when it engages, phase 2 below 55 % (spirits wake), defeat
    g.lane("boss")
    bu = g.n("Event.Update")
    is2 = g.br((bu, "Then"), g.all_of([(g.cmp("Equal", V(g, "Level"), 2), "Value"), V(g, "Loaded")]))
    b1, b2, b3 = g.fork((is2, "True"))
    bstate = g.n("Gameplay.AIState")
    g.l(g.get("Boss"), "Value", bstate, "Target")
    bhp = g.n("Gameplay.Health")
    g.l(g.get("Boss"), "Value", bhp, "Target")
    engaged = g.any_of([(g.text_is((bstate, "Mode"), "Chase"), "Value"), (g.text_is((bstate, "Mode"), "Attack"), "Value")])
    seen = g.br(b1, g.all_of([(g.not_(V(g, "BossSeen")), "Value"), (g.not_((bhp, "Dead")), "Value"), engaged]))
    t = g.setv((seen, "True"), "BossSeen", True)
    t = (g.ex(t, "SetActive", Target=6093, Active=True), "Then")
    t = (g.ex(t, "SetActive", Target=6094, Active=True), "Then")
    bind = g.ex(t, "Gameplay.UIBindHUD", Target=6094, Kind=0, Item="Coin", Prefix="", Ratio=False)
    g.l(g.get("Boss"), "Value", bind, "Actor")
    t = (g.ex((bind, "Then"), "FadeAudio", Target=MUSIC, Volume=0.0, Seconds=1.5), "Then")
    t = (g.ex(t, "PlayAudio", Target=BOSS_MUSIC), "Then")
    t = (g.ex(t, "FadeAudio", Target=BOSS_MUSIC, Volume=0.9, Seconds=1.5), "Then")
    g.ex(t, "Print", Text="SLICE_TRACE ev=boss a=engaged")
    half = g.num("Multiply", (bhp, "Maximum"), 0.55)
    ph2 = g.br(b2, g.all_of([(g.not_(V(g, "Phase2")), "Value"), (g.not_((bhp, "Dead")), "Value"), (g.cmp("Less", (bhp, "Current"), half), "Value")]))
    t = g.setv((ph2, "True"), "Phase2", True)
    fw = g.ex(t, "ForEach.Entity")
    g.l(g.get("Wisps"), "Value", fw, "Array")
    wake = g.ex((fw, "Body"), "SetActive", Active=True)
    g.l(fw, "Value", wake, "Target")
    t = (g.ex((fw, "Completed"), "SendMessage", Target=GAME, Name="Banner", Text="Soya Bek g'azablandi!"), "Then")
    t = (g.ex(t, "ShakeCamera", Intensity=0.5, Duration=0.6), "Then")
    g.ex(t, "Print", Text="SLICE_TRACE ev=boss a=phase2")
    down = g.br(b3, g.all_of([V(g, "BossSeen"), (g.not_(V(g, "BossDown")), "Value"), (bhp, "Dead")]))
    t = g.setv((down, "True"), "BossDown", True)
    t = (g.ex(t, "SetActive", Target=6093, Active=False), "Then")
    t = (g.ex(t, "SetActive", Target=6094, Active=False), "Then")
    t = (g.ex(t, "FadeAudio", Target=BOSS_MUSIC, Volume=0.0, Seconds=2.0), "Then")
    t = (g.ex(t, "FadeAudio", Target=MUSIC, Volume=0.8, Seconds=2.0), "Then")
    t = (g.ex(t, "SendMessage", Target=GAME, Name="Banner", Text="Soya Bek yengildi!"), "Then")
    g.ex(t, "Print", Text="SLICE_TRACE ev=boss a=defeated")

    # ---- demo: automatic choices on end screens (the bot never clicks)
    g.lane("demo")
    du = g.n("Event.Update")
    on = g.br((du, "Then"), (g.cmp("Greater", V(g, "Auto"), 0), "Value"))
    t = g.setv((on, "True"), "Auto", data=g.num("Subtract", V(g, "Auto"), (g.n("Gameplay.UIState"), "Delta")))
    due = g.br(t, (g.cmp("Less", V(g, "Auto"), 0.0001), "Value"))
    t = g.setv((due, "True"), "Auto", -1)
    which = g.br(t, V(g, "Ended"))
    g.ex((which, "True"), "EmitEvent", Name="Menu")
    g.ex((which, "False"), "EmitEvent", Name="Retry")

    # ---- messages (buttons and graphs)
    g.lane("messages")
    m = g.n("Event.Message")
    nxt = (m, "Then")
    names = ("Resume", "Bag", "CloseBag", "NextPage", "Equip", "Drink", "SaveQuit", "Menu", "Retry", "Defeat", "Saved", "Banner", "LevelUp", "Hurt", "Claimed")
    for name in names:
        b = g.br(nxt, (g.text_is((m, "Text"), name), "Value"))
        e = g.ex((b, "True"), "EmitEvent", Name=name, Text="")
        g.l(m, "Payload", e, "Text")
        nxt = (b, "False")
    payload = lambda event: (event, "Text")

    def on(name):
        return g.n("Event.Custom", Name=name)
    g.ex((on("Resume"), "Then"), "Gameplay.UIBack")
    g.ex((on("Bag"), "Then"), "Gameplay.UIOpen", Name="Bag")
    g.ex((on("CloseBag"), "Then"), "Gameplay.UIClose", Name="Bag")
    g.ex((on("NextPage"), "Then"), "Gameplay.UIInventoryPage", Name="Bag", Delta=1)
    inv = g.n("Gameplay.UIInventoryState", Name="Bag")
    eq = g.ex((on("Equip"), "Then"), "Gameplay.EquipmentEquip", Target=HERO)
    g.l(inv, "Item", eq, "Item")
    eqb = g.br((eq, "Then"), (eq, "Success"))
    msg = g.ex((eqb, "True"), "SendMessage", Target=GAME, Name="Banner")
    g.l(*g.text(["Kiyildi: ", (inv, "Item")]), msg, "Text")
    g.ex((eqb, "False"), "SendMessage", Target=GAME, Name="Banner", Text="Bu buyumni kiyib bo'lmaydi")
    g.ex((on("Drink"), "Then"), "Gameplay.AbilityCast", Target=HERO, Ability="Potion", TwoD=True)
    sq = g.ex((on("SaveQuit"), "Then"), "Gameplay.SaveWrite", Schema=schema)
    g.l(g.get("Slot"), "Value", sq, "Slot")
    sqb = g.br((sq, "Then"), (sq, "Success"))
    ws = g.ex((sqb, "True"), "WriteState.Number", Key=f"{GS}.Run.Save")
    g.l(g.get("Level"), "Value", ws, "Value")
    wt = g.ex((ws, "Then"), "WriteState.Number", Key=f"{GS}.Run.Time")
    g.l(g.get("Time"), "Value", wt, "Value")
    t = (g.ex((wt, "Then"), "SaveGameState"), "Then")
    p = g.ex(t, "Print")
    g.l(*g.text(["SLICE_SAVED lvl=", (g.get("Level"), "Value", "number"), " at=quit"]), p, "Text")
    g.ex((p, "Then"), "LoadScene", Path="Scenes/Menu.zscene", FadeOut=0.5)
    sqf = g.ex((sqb, "False"), "Print")
    g.l(*g.text(["SLICE_IO_FAILED quit ", (sq, "Reason")]), sqf, "Text")
    g.ex((on("Menu"), "Then"), "LoadScene", Path="Scenes/Menu.zscene", FadeOut=0.5)
    t = (g.ex((on("Retry"), "Then"), "WriteState.String", Key=f"{GS}.Run.Entry", Value="continue"), "Then")
    t = (g.ex(t, "SaveGameState"), "Then")
    t = (g.ex(t, "Print", Text="Menyu: Oxirgi saqlashdan"), "Then")
    re = g.ex(t, "LoadScene", FadeOut=0.5)
    g.l(g.get("Self"), "Value", re, "Path")
    t = (g.ex((on("Defeat"), "Then"), "Gameplay.UIOpen", Name="Defeat"), "Then")
    pd = g.ex(t, "Print")
    g.l(*g.text(["SLICE_DEFEAT lvl=", (g.get("Level"), "Value", "number"), " deaths=3"]), pd, "Text")
    t = (g.ex((pd, "Then"), "PlaySound", Path="Audio/sfx_defeat.wav", Volume=0.9), "Then")
    t = (g.ex(t, "WriteState.Number", Key=f"{GS}.Run.Lost", Value=1), "Then")
    t = (g.ex(t, "SaveGameState"), "Then")
    demo = g.br(t, g.any_of([V(g, "Demo"), (g.n("App.DemoMode"), "Value"), (g.n("ReadState.Bool", Key=f"{GS}.Run.Demo", Default=False), "Value")]))
    g.setv((demo, "True"), "Auto", 2.5)
    sv = on("Saved")
    wt = g.ex((sv, "Then"), "WriteState.Number", Key=f"{GS}.Run.Time")
    g.l(g.get("Time"), "Value", wt, "Value")
    t = (g.ex((wt, "Then"), "SaveGameState"), "Then")
    p = g.ex(t, "Print")
    g.l(*g.text(["SLICE_SAVED lvl=", (g.get("Level"), "Value", "number"), " at=shrine"]), p, "Text")
    t = (g.ex((p, "Then"), "SetUIText", Target=6091, Text="Saqlandi"), "Then")
    g.setv(t, "Banner", 2.5)
    bn = on("Banner")
    st = g.ex((bn, "Then"), "SetUIText", Target=6091)
    g.l(bn, "Text", st, "Text")
    g.setv((st, "Then"), "Banner", 2.5)
    lv = on("LevelUp")
    st = g.ex((lv, "Then"), "SetUIText", Target=6091)
    g.l(lv, "Text", st, "Text")
    t = g.setv((st, "Then"), "Banner", 3.5)
    t = (g.ex(t, "EmitParticles", Target=8101, Count=80), "Then")
    t = (g.ex(t, "PlaySound", Path="Audio/sfx_levelup.wav", Volume=0.9), "Then")
    g.ex(t, "Print", Text="SLICE_TRACE ev=levelup")
    hu = on("Hurt")
    t = (g.ex((hu, "Then"), "SetActive", Target=6080, Active=True), "Then")
    t = g.setv(t, "Hurt", 0.22)
    g.ex(t, "PlaySound", Path="Audio/sfx_hurt.wav", Volume=0.5)
    cl = on("Claimed")
    p = g.ex((cl, "Then"), "Print")
    g.l(*g.text(["SLICE_TRACE ev=claim a=", (cl, "Text")]), p, "Text")
    t = (g.ex((p, "Then"), "SetUIText", Target=6091, Text="Vazifa bajarildi!"), "Then")
    g.setv(t, "Banner", 2.5)
    g.save(assets, "Scripts/Game.zgraph", project)
    return g


def enemy_graph(assets, project):
    """Every enemy (instance overrides choose the archetype): health/armour/AI configuration, ranged (delegated)
    attacks, boss strikes and rings, animation from the real velocity, hit sparks, kill loot straight into the bag."""
    g = gk.Graph("Enemy")
    _vars(g, (("Key", "String", "Enemy"), ("Kind", "String", "bandit"), ("HP", "Number", 30), ("Armor", "Number", 0), ("Damage", "Number", 5),
              ("Cooldown", "Number", 1), ("Speed", "Number", 2), ("View", "Number", 6), ("Lose", "Number", 8), ("FOV", "Number", 120),
              ("Leash", "Number", 9), ("Enter", "Number", 1.1), ("Exit", "Number", 1.4), ("Ranged", "Bool", False), ("Ability", "String", ""),
              ("Amount", "Number", 0), ("Visual", "Entity", 0), ("Patrol", "ArrayVec3", []), ("Loot", "String", ""), ("Loot2", "String", ""),
              ("Dormant", "Bool", False), ("Boss", "Bool", False), ("Ring", "Entity", 0), ("DeadTime", "Number", 0), ("RingTime", "Number", 0),
              ("Attacks", "Number", 0)))
    g.lane("config")
    start = g.n("Event.Start")
    stats = g.n("Array.Set.Number", Array=[0, 0, 0, 1, 0], Index=1)
    g.l(g.get("Armor"), "Value", stats, "Value")
    ops = [("Gameplay.HealthConfigure", dict(Shield=0, HitGrace=0, RespawnDelay=60, Team=2), {"Maximum": V(g, "HP")}),
           ("Gameplay.LoadoutConfigure", dict(), {"Stats": (stats, "Value")}),
           ("Gameplay.AIConfigure", dict(TwoD=True, Tag="Hero", Victim=0, Relative=True, PingPong=True, Near=1, Memory=2.5, Wait=0.6, Arrive=0.3, Repath=0.4,
                                         Stall=0.75, RecoveryCooldown=0.5, Retries=3, Eye=[0, 0, 0], Facing=[0, -1, 0], Mask=WALL_MASK, Enabled=True),
            {"Points": V(g, "Patrol"), "View": V(g, "View"), "Lose": V(g, "Lose"), "FieldOfView": V(g, "FOV"), "Leash": V(g, "Leash"),
             "AttackEnter": V(g, "Enter"), "AttackExit": V(g, "Exit"), "Damage": V(g, "Damage"), "AttackCooldown": V(g, "Cooldown"),
             "Strike": (g.not_(V(g, "Ranged")), "Value"), "Speed": V(g, "Speed")}),
           ("Gameplay.SaveBind", dict(), {"Key": V(g, "Key")})]
    tail = g.steps(start, ops, "SLICE_CONFIG_FAILED enemy")
    ranged = g.br(tail, V(g, "Ranged"))
    ad = g.ex((ranged, "True"), "Gameplay.AbilityDefine", Kind=0, Cost=0, Range=9, Count=1, Unlocked=True)
    g.wire(ad, Ability=V(g, "Ability"), Amount=V(g, "Amount"), Cooldown=V(g, "Cooldown"), Projectile=V(g, "Ability"))
    adb = g.br((ad, "Then"), (ad, "Success"))
    bad = g.ex((adb, "False"), "Print")
    g.l(*g.text(["SLICE_CONFIG_FAILED ability ", (ad, "Reason")]), bad, "Text")
    dormant = g.br(g.join((ranged, "False"), (adb, "True"), (bad, "Then")), V(g, "Dormant"))
    g.ex((dormant, "True"), "SetActive", Active=False)
    # ---- per frame
    g.lane("update")
    u = g.n("Event.Update")
    hp = g.n("Gameplay.Health")
    f1, f2, f3 = g.fork((u, "Then"))
    dead = g.br(f1, (hp, "Dead"))
    t = (g.ex((dead, "True"), "Animator.SetBool", Name="Dead", Value=True), "Then")
    g.l(g.get("Visual"), "Value", t[0], "Target")
    t = g.setv(t, "DeadTime", data=g.num("Add", V(g, "DeadTime"), (u, "Delta")))
    gone = g.br(t, (g.cmp("Greater", V(g, "DeadTime"), 1.4), "Value"))
    g.ex((gone, "True"), "SetActive", Active=False)
    ba = g.ex((dead, "False"), "Gameplay.BindAnimator", TwoD=True, TopDown=True, Damping=0.08)
    g.wire(ba, Target=V(g, "Visual"), Source=(g.n("Self"), "Value"), ReferenceSpeed=V(g, "Speed"), Delta=(u, "Delta"))
    # events: hits and the kill loot (a restored death never replays: restore clears the events)
    for src in g.fork(f2)[:2]:
        poll = g.ex(src, "Gameplay.RuleEvent")
        has_ = g.br((poll, "Then"), (poll, "Has"))
        died = g.br((has_, "True"), (g.text_is((poll, "Kind"), "Died"), "Value"))
        t = (g.ex((died, "True"), "PlaySound", Path="Audio/sfx_death.wav", Volume=0.6), "Then")
        p = g.ex(t, "Print")
        g.l(*g.text(["SLICE_TRACE ev=kill a=", V(g, "Kind")]), p, "Text")
        t = (p, "Then")
        for var in ("Loot", "Loot2"):
            lb = g.br(t, (g.not_((g.text_is(V(g, var), ""), "Value")), "Value"))
            add = g.ex((lb, "True"), "Gameplay.InventoryAdd", Target=HERO, Count=1)
            g.l(g.get(var), "Value", add, "Item")
            t = g.join((lb, "False"), (add, "Then"))
        hit = g.br((died, "False"), (g.text_is((poll, "Kind"), "Damage"), "Value"))
        mypos = g.n("GetPosition")
        mv = g.ex((hit, "True"), "SetPosition", Target=8100)
        g.l(mypos, "Value", mv, "Vector")
        t = (g.ex((mv, "Then"), "EmitParticles", Target=8100, Count=16), "Then")
        g.ex(t, "PlaySound", Path="Audio/sfx_hit.wav", Volume=0.45)
    # attacks
    st = g.n("Gameplay.AIState")
    a1, a2, a3 = g.fork(f3)
    alive = (g.not_((hp, "Dead")), "Value")
    attacking = g.all_of([alive, (g.text_is((st, "Mode"), "Attack"), "Value"), (st, "Visible")])
    rb = g.br(a1, g.all_of([V(g, "Ranged"), attacking]))
    cast = g.ex((rb, "True"), "Gameplay.AbilityCast", TwoD=True, LineOfSight=True, Mask=WALL_MASK)
    g.wire(cast, Ability=V(g, "Ability"), Victim=(st, "Victim"))
    cb = g.br((cast, "Then"), (cast, "Success"))
    tr = g.ex((cb, "True"), "Animator.SetTrigger", Name="Attack")
    g.l(g.get("Visual"), "Value", tr, "Target")
    struck = g.br(a2, g.all_of([alive, (g.cmp("Greater", (st, "Attacks"), V(g, "Attacks")), "Value")]))
    t = g.setv((struck, "True"), "Attacks", data=(st, "Attacks"))
    tr2 = g.ex(t, "Animator.SetTrigger", Name="Attack")
    g.l(g.get("Visual"), "Value", tr2, "Target")
    g.ex((tr2, "Then"), "PlaySound", Path="Audio/sfx_swing.wav", Volume=0.5)
    # boss: a mace strike up close and, below 55 %, a telegraphed shadow ring every 5 s
    boss = g.br(a3, g.all_of([V(g, "Boss"), alive]))
    s1, s2, _ = g.fork((boss, "True"))
    near = g.n("DistanceVec3")
    g.l(g.n("GetPosition"), "Value", near, "A")
    g.l(g.n("GetPosition", Target=HERO), "Value", near, "B")
    close = g.br(s1, (g.cmp("Less", (near, "Value"), 1.7), "Value"))
    strike = g.ex((close, "True"), "Gameplay.Strike", Victim=HERO, Range=1.9, MinDot=-1, TwoD=True, LineOfSight=True, Mask=WALL_MASK, Origin=[0, 0, 0],
                  Amount=9, Cooldown=1.3)
    sb = g.br((strike, "Then"), (strike, "Success"))
    tr3 = g.ex((sb, "True"), "Animator.SetTrigger", Name="Attack")
    g.l(g.get("Visual"), "Value", tr3, "Target")
    angry = g.br(s2, (g.cmp("Less", (hp, "Current"), g.num("Multiply", (hp, "Maximum"), 0.55)), "Value"))
    t = g.setv((angry, "True"), "RingTime", data=g.num("Add", V(g, "RingTime"), (u, "Delta")))
    warn = g.br(t, (g.cmp("Greater", V(g, "RingTime"), 4.3), "Value"))
    show = g.ex((warn, "True"), "SetActive", Active=True)
    g.l(g.get("Ring"), "Value", show, "Target")
    fire = g.br((show, "Then"), (g.cmp("Greater", V(g, "RingTime"), 5.1), "Value"))
    area = g.ex((fire, "True"), "Gameplay.AreaApply", Area="Ring", Victim=0, Magnitude=-1)
    g.l(g.n("GetPosition"), "Value", area, "Center")
    t = (g.ex((area, "Then"), "ShakeCamera", Intensity=0.45, Duration=0.4), "Then")
    t = (g.ex(t, "PlaySound", Path="Audio/sfx_nova.wav", Volume=0.8), "Then")
    hide = g.ex(t, "SetActive", Active=False)
    g.l(g.get("Ring"), "Value", hide, "Target")
    g.setv((hide, "Then"), "RingTime", 0)
    g.save(assets, "Scripts/Enemy.zgraph", project)
    return g


UINT_SELF = 18446744073709551615  # an Entity pin's "Self"


def menu_graph(assets, project):
    """The title menu: panels, Continue only for an unfinished run, settings (live volumes, fullscreen, language,
    persisted on Back), credits, demo auto-start, new game / continue with a fade."""
    g = gk.Graph("Menu")
    _vars(g, (("Demo", "Bool", False), ("Started", "Bool", False), ("Time", "Number", 0), ("Can", "Bool", False)))
    g.lane("config")
    start = g.n("Event.Start")
    t = (g.ex((start, "Then"), "LoadGameState"), "Then")
    ops = [("Gameplay.UIConfigure", dict(Target=6020, Name="Main", Action="", Focus=6021, Pause=False, Block=False, Back=False)),
           ("Gameplay.UIConfigure", dict(Target=6100, Name="Settings", Action="", Focus=6111, Pause=False, Block=True, Back=True)),
           ("Gameplay.UIConfigure", dict(Target=6200, Name="Credits", Action="", Focus=6211, Pause=False, Block=True, Back=True)),
           ("Gameplay.UIOpen", dict(Name="Main"))]
    tail = g.steps(t, ops, "SLICE_CONFIG_FAILED menu")
    level = g.n("ReadState.Number", Key=f"{GS}.Run.Level", Default=0)
    finished = g.n("ReadState.Number", Key=f"{GS}.Run.Finished", Default=0)
    can = g.all_of([(g.cmp("Greater", (level, "Value"), 0), "Value"), (g.cmp("Less", (finished, "Value"), 0.5), "Value")])
    t = g.setv(tail, "Can", data=can)
    vis = g.ex(t, "SetActive", Target=6022)
    g.l(g.get("Can"), "Value", vis, "Active")
    focus = g.br((vis, "Then"), V(g, "Can"))
    fo = g.ex((focus, "True"), "Gameplay.UIFocus", Widget=6022)
    t = g.join((focus, "False"), (fo, "Then"))
    for widget, setting, default, group in ((6111, "Music", 0.55, "Music"), (6112, "Sfx", 0.9, "SFX")):
        value = g.n("ReadState.Number", Key=f"{GS}.Settings.{setting}", Default=default)
        sv = g.ex(t, "SetUIValue", Target=widget, Notify=False)
        g.l(value, "Value", sv, "Value")
        vol = g.ex((sv, "Then"), "Audio.SetGroupVolume", Group=group)
        g.l(value, "Value", vol, "Volume")
        t = (vol, "Then")
    best = g.n("ReadState.Number", Key=f"{GS}.Best.Time", Default=0)
    rb = g.br(t, (g.cmp("Greater", (best, "Value"), 0), "Value"))
    rt = g.ex((rb, "True"), "SetUIText", Target=6030)
    g.l(*g.text(["Eng yaxshi vaqt  ", (best, "Value", "time")]), rt, "Text")
    shown = g.br(g.join((rb, "False"), (rt, "Then")), V(g, "Can"))
    g.ex((shown, "True"), "Print", Text="Menyu: Asosiy davom=1")
    g.ex((shown, "False"), "Print", Text="Menyu: Asosiy davom=0")
    # ---- per frame: live volumes while the settings are open; the demo picks Continue or New game after 1 s
    g.lane("update")
    u = g.n("Event.Update")
    t = g.setv((u, "Then"), "Time", data=g.num("Add", V(g, "Time"), (u, "Delta")))
    for widget, group in ((6111, "Music"), (6112, "SFX")):
        vol = g.ex(t, "Audio.SetGroupVolume", Group=group)
        g.l(g.n("UIValue", Target=widget), "Value", vol, "Volume")
        t = (vol, "Then")
    demo = g.any_of([V(g, "Demo"), (g.n("App.DemoMode"), "Value")])
    auto = g.br(t, g.all_of([demo, (g.not_(V(g, "Started")), "Value"), (g.cmp("Greater", V(g, "Time"), 1.0), "Value")]))
    t = g.setv((auto, "True"), "Started", True)
    pick = g.br(t, V(g, "Can"))
    g.ex((pick, "True"), "EmitEvent", Name="Continue")
    g.ex((pick, "False"), "EmitEvent", Name="New")
    # ---- buttons
    g.lane("messages")
    m = g.n("Event.Message")
    nxt = (m, "Then")
    for name in ("New", "Continue", "Settings", "Credits", "Back", "Quit", "Fullscreen", "Language"):
        b = g.br(nxt, (g.text_is((m, "Text"), name), "Value"))
        g.ex((b, "True"), "EmitEvent", Name=name)
        nxt = (b, "False")
    on = lambda name: (g.n("Event.Custom", Name=name), "Then")
    t = on("New")
    for key, value in (("Run.Level", 1), ("Run.Save", 0), ("Run.Time", 0), ("Run.Finished", 0), ("Run.Lost", 0), ("Run.Step", 0)):
        t = (g.ex(t, "WriteState.Number", Key=f"{GS}.{key}", Value=value), "Then")
    t = (g.ex(t, "WriteState.String", Key=f"{GS}.Run.Entry", Value="new"), "Then")
    wd = g.ex(t, "WriteState.Bool", Key=f"{GS}.Run.Demo", Persistent=False)
    g.l(*demo, wd, "Value")
    t = (g.ex((wd, "Then"), "SaveGameState"), "Then")
    t = (g.ex(t, "Print", Text="Menyu: Yangi o'yin"), "Then")
    g.ex(t, "LoadScene", Path="Scenes/Boglar.zscene", FadeOut=0.6, FadeIn=0.6, Color=[0.02, 0.01, 0.04, 1])
    t = (g.ex(on("Continue"), "WriteState.String", Key=f"{GS}.Run.Entry", Value="continue"), "Then")
    wd = g.ex(t, "WriteState.Bool", Key=f"{GS}.Run.Demo", Persistent=False)
    g.l(*demo, wd, "Value")
    t = (g.ex((wd, "Then"), "Print", Text="Menyu: Davom etish"), "Then")
    two = g.br(t, (g.cmp("Greater", (level, "Value"), 1.5), "Value"))
    g.ex((two, "True"), "LoadScene", Path="Scenes/Yertola.zscene", FadeOut=0.6, FadeIn=0.6, Color=[0.02, 0.01, 0.04, 1])
    g.ex((two, "False"), "LoadScene", Path="Scenes/Boglar.zscene", FadeOut=0.6, FadeIn=0.6, Color=[0.02, 0.01, 0.04, 1])
    g.ex((g.ex(on("Settings"), "Gameplay.UIOpen", Name="Settings"), "Then"), "Print", Text="Menyu: Sozlamalar")
    g.ex((g.ex(on("Credits"), "Gameplay.UIOpen", Name="Credits"), "Then"), "Print", Text="Menyu: Mualliflar")
    t = on("Back")
    music, sfx = g.n("UIValue", Target=6111), g.n("UIValue", Target=6112)
    for key, src in (("Music", music), ("Sfx", sfx)):
        w_ = g.ex(t, "WriteState.Number", Key=f"{GS}.Settings.{key}")
        g.l(src, "Value", w_, "Value")
        t = (w_, "Then")
    t = (g.ex(t, "SaveGameState"), "Then")
    p = g.ex(t, "Print")
    g.l(*g.text(["SLICE_SETTINGS music=", (music, "Value", "number", 2), " sfx=", (sfx, "Value", "number", 2), " lang=", (g.n("Localization.Language"), "Value")]), p, "Text")
    t = (g.ex((p, "Then"), "Gameplay.UIBack"), "Then")
    g.ex(t, "Print", Text="Menyu: Asosiy")
    g.ex(on("Quit"), "QuitGame")
    full = g.ex(on("Fullscreen"), "Display.SetFullscreen")
    g.l(g.n("UIValue", Target=6113), "On", full, "Fullscreen")
    lang = g.br(on("Language"), (g.text_is((g.n("Localization.Language"), "Value"), "uz"), "Value"))
    t = (g.ex((lang, "True"), "Localization.SetLanguage", Code="en"), "Then")
    g.ex(t, "SetUIText", Target=6114, Text="Language: English")
    t = (g.ex((lang, "False"), "Localization.SetLanguage", Code="uz"), "Then")
    g.ex(t, "SetUIText", Target=6114, Text="Til: O'zbekcha")
    g.save(assets, "Scripts/Menu.zgraph", project)
    return g
