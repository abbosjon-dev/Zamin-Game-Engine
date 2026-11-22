"""Scenes, prefabs and Visual Script graphs of Dovon mash'ali (gen_beacon_pass.py).

UUID ranges are fixed and identical in both scenes so shared graphs target literals: 1000-1099 hero and camera,
1100-1199 controllers and audio, 2000-4999 level geometry and props, 5000-5499 enemies, 5500-5999 interactables,
6000-7999 UI, 8000-8999 lights and effects. The hero carries its model, Animator and Footsteps on the same entity as its
Character Controller 3D, so the clips' events (Hit, Release, Footstep) reach the hero's own graph.
"""
import json
import math
import os

import samplekit as k
import graphkit as gk
from lantern_graphs import Scene, UI, chain_nav, graph_script, _vars, V, CANVAS, INK, MUTED

HERO, SWORD, SHIELD, CAMERA = 1000, 1001, 1002, 1003
GAME, MUSIC, AMBIENCE, BOSS_MUSIC, SUN = 1100, 1101, 1102, 1104, 1110
ACCENT = (0.98, 0.74, 0.34, 1)
GS = "BP"
# the banner of a save whose primary and backup are both unreadable (the level starts again from the camp)
UNREADABLE = "Saqlash o'qilmadi - daraja boshidan"
M = {}  # masks and layers (set by build)


# The title menu's camera: above the valley camp, looking north up the pass (yaw 0 looks along -z).
MENU_EYE, MENU_PITCH, MENU_YAW = (-7.0, 6.5, 47.5), math.radians(-5.0), math.radians(-3.0)

def save_scene(scene, assets, project):
    """Scene.save with the terrain paint and foliage instances on single lines (they are thousands of small objects)."""
    ents = []
    blobs = {}
    for eid in sorted(scene.entities):
        e = json.loads(json.dumps(scene.entities[eid]))
        if eid in scene.children:
            e["ChildOrder"] = scene.children[eid]
        for comp, key in (("Terrain", "Paint"), ("Foliage", "Instances")):
            if comp in e:
                marker = f"@@{comp}{eid}@@"
                blobs[marker] = json.dumps(e[comp][key], separators=(",", ":"))
                e[comp][key] = marker
        ents.append(e)
    data = {"Scene": scene.name, "Version": 5, "Settings": scene.settings, "RootOrder": scene.roots, "Entities": ents}
    text = k.dumps_compact(data)
    for marker, blob in blobs.items():
        text = text.replace(json.dumps(marker), blob)
    rel = f"Scenes/{scene.name}.zscene"
    k.write_text(os.path.join(assets, rel), text + "\n")
    k.write_zmeta(assets, rel, "Scene", {}, project)


def scene_uuid(project, name):
    return k.stable_uuid(f"{project}/Scenes/{name}.zscene")


def model(path, cast=True, albedo=(1, 1, 1, 1)):
    return k.mesh("Model", albedo, 0.6, model=path, cast_shadows=cast)


def textured(kind, tex, scale, tiling_per_m=0.5, rough=0.85, cast=True, albedo=(1, 1, 1, 1)):
    """A primitive with a tiling PBR set: the tiling follows the entity's scale so texels stay square."""
    tx = max(0.25, scale[0] * tiling_per_m)
    ty = max(0.25, scale[1] * tiling_per_m)
    m = k.mesh(kind, albedo, rough, 0.0, albedo_map=f"Textures/{tex}.png", normal_map=f"Textures/{tex}_n.png", tiling=(round(tx, 3), round(ty, 3)),
               normal_strength=1.0, cast_shadows=cast)
    m["MetalRoughMap"] = f"Textures/{tex}_orm.png"
    return m


def solid(s, eid, tag, pos, scale, tex, layer=None, collider=True, rot=(0, 0, 0), kind="Cube", tiling=0.5, cast=True, albedo=(1, 1, 1, 1), parent=None):
    comps = {"MeshRenderer": textured(kind, tex, scale, tiling, cast=cast, albedo=albedo)}
    if collider:
        comps["Rigidbody3D"] = k.rigidbody3d("Static", layer=layer if layer is not None else M["WALL_LAYER"])
        comps["BoxCollider3D"] = k.box3d((0.5, 0.5, 0.5), friction=0.8)
    e = s.add(eid, tag, parent, pos=pos, scale=scale, **comps)
    s.entities[e]["Transform"]["Rotation"] = k.v(*rot)
    return e


def settings_3d():
    return {"DrawSky": True, "SkyTop": [0.20, 0.32, 0.58], "SkyHorizon": [1.0, 0.66, 0.42], "Ground": [0.42, 0.33, 0.26], "Ambient": 0.42,
            "Exposure": 1.05, "ShadowDistance": 55,
            "Sky": {"Curve": 0.8, "GroundFade": 1.5, "SunDisc": True, "SunSize": 1.6, "SunIntensity": 2.4, "SunGlow": 0.6},
            "Fog": {"Enabled": True, "Color": [0.86, 0.64, 0.50], "Density": 0.0065, "HeightFalloff": 0.06},
            "Post": {"Tonemap": 1, "Bloom": True, "BloomThreshold": 1.0, "BloomKnee": 0.5, "BloomIntensity": 0.55, "BloomRadius": 1.0, "Vignette": 0.26,
                     "Saturation": 1.1, "Contrast": 1.06, "Brightness": 0.0, "Tint": [1.0, 0.98, 0.95], "SSAO": True, "SSAORadius": 0.6,
                     "SSAOIntensity": 1.1, "SSAOBias": 0.03, "FXAA": True},
            "Gravity3D": [0, -9.81, 0]}


def point(s, eid, parent, pos, col, intensity, radius):
    return s.add(eid, "Chiroq", parent, pos=pos, PointLight=k.point_light(col, intensity, radius))


def fire(s, eid, parent, pos, scale=1.0, active=True):
    return s.add(eid, "Olov", parent, pos=pos, active=active, ParticleSystem=k.particles(
        EmissionRate=34.0 * scale, MaxParticles=int(110 * scale), Shape=2, ShapeRadius=0.16 * scale, LifetimeMin=0.35, LifetimeMax=0.85,
        SpeedMin=0.7 * scale, SpeedMax=1.8 * scale, Direction=(0, 1, 0), Spread=16.0, StartColor=(1.0, 0.72, 0.28, 1.0), EndColor=(0.85, 0.22, 0.06, 0.0),
        StartSize=0.34 * scale, EndSize=0.06 * scale, Gravity=(0, 0.6, 0)))


def smoke(s, eid, parent, pos, scale=1.0):
    return s.add(eid, "Tutun", parent, pos=pos, ParticleSystem=k.particles(
        EmissionRate=5.0 * scale, MaxParticles=int(60 * scale), Shape=2, ShapeRadius=0.25 * scale, LifetimeMin=2.5, LifetimeMax=4.0, SpeedMin=0.4,
        SpeedMax=0.9, Direction=(0.2, 1, 0), Spread=12.0, StartColor=(0.35, 0.32, 0.30, 0.35), EndColor=(0.55, 0.5, 0.48, 0.0), StartSize=0.5 * scale,
        EndSize=2.0 * scale, Additive=False))


# ------------------------------------------------------------------------------------------------ the world (both scenes)


def world(s, W, level):
    """Terrain, sun, caravanserai, camp, plateau, beacon platform, watchtower, foliage and distant peaks."""
    P = W["P"]
    fy = W["floor_at"]
    ox, oz = W["origin"]
    s.add(2000, "Tog' vodiysi", pos=(ox, 0, oz), Terrain=terrain(W), Rigidbody3D=k.rigidbody3d("Static", layer=M["WORLD_LAYER"]),
          MeshCollider3D={"Friction": 0.8, "IsSensor": False, "Model": "", "Offset": [0, 0, 0], "Restitution": 0.0})
    s.add(SUN, "Quyosh", rot=(math.radians(-17), math.radians(-62), 0), DirectionalLight=k.dir_light((1.0, 0.76, 0.52), 3.6, True))
    s.entities[SUN]["Transform"]["Rotation"] = k.v(math.radians(-17), math.radians(-62), 0)
    # ---- caravanserai (terrace y 3)
    w = W["WALLS"]
    y0, hgt, t = 3.0, w["height"], w["thick"]
    eid = 2100
    segs = [((w["x0"] + -w["gate"]) / 2, w["z1"], -w["gate"] - w["x0"], t), ((w["gate"] + w["x1"]) / 2, w["z1"], w["x1"] - w["gate"], t),
            ((w["x0"] + -w["gate"]) / 2, w["z0"], -w["gate"] - w["x0"], t), ((w["gate"] + w["x1"]) / 2, w["z0"], w["x1"] - w["gate"], t),
            (w["x0"], (w["z0"] + w["z1"]) / 2, t, w["z1"] - w["z0"]), (w["x1"], (w["z0"] + w["z1"]) / 2, t, w["z1"] - w["z0"])]
    for cx, cz, sx, sz in segs:
        solid(s, eid, "Devor", (cx, y0 + hgt / 2 - 0.3, cz), (sx, hgt + 0.6, sz), "stone")
        eid += 1
        # merlons along the top
        length = max(sx, sz)
        n = int(length / 2.0)
        for i in range(n):
            f = (i + 0.5) / n - 0.5
            mx, mz = (cx + f * sx, cz) if sx > sz else (cx, cz + f * sz)
            solid(s, eid, "Tishli devor", (mx, y0 + hgt + 0.55, mz), (0.8 if sx > sz else t, 0.8, t if sx > sz else 0.8), "stone", collider=False)
            eid += 1
    for cx in (w["x0"], w["x1"]):
        for cz in (w["z0"], w["z1"]):
            solid(s, eid, "Burj", (cx, y0 + 3.0, cz), (3.4, 7.0, 3.4), "stone", kind="Cylinder", tiling=0.45)
            eid += 1
            for i in range(8):
                a = 2 * math.pi * i / 8
                solid(s, eid, "Burj tishi", (cx + math.cos(a) * 1.5, y0 + 6.9, cz + math.sin(a) * 1.5), (0.6, 0.7, 0.6), "stone", collider=False)
                eid += 1
    # gatehouse: two towers, the lintel, banners and braziers
    for sx in (-1, 1):
        solid(s, eid, "Darvoza minorasi", (sx * 3.4, y0 + 3.1, w["z1"]), (2.6, 7.4, 2.6), "stone")
        eid += 1
        for i, (dx, dz) in enumerate(((-0.9, -0.9), (0.9, -0.9), (-0.9, 0.9), (0.9, 0.9))):
            solid(s, eid, "Minora tishi", (sx * 3.4 + dx, y0 + 7.1, w["z1"] + dz), (0.6, 0.7, 0.6), "stone", collider=False)
            eid += 1
        s.add(eid, "Bayroq", pos=(sx * 3.4, y0 + 2.2, w["z1"] + 1.32), MeshRenderer=model("Models/BannerIndigo.glb" if sx < 0 else "Models/BannerTeal.glb"))
        s.entities[eid]["Transform"]["Rotation"] = k.v(0, math.pi, 0)
        eid += 1
        s.add(eid, "Mangal", pos=(sx * 2.9, y0, w["z1"] + 2.6), MeshRenderer=model("Models/Brazier.glb"))
        fire(s, eid + 1, eid, (0, 1.05, 0), 0.8)
        point(s, eid + 2, eid, (0, 1.5, 0), (1.0, 0.6, 0.28), 5.0, 8.0)
        eid += 3
    solid(s, eid, "Ravoq", (0, y0 + hgt + 0.15, w["z1"]), (2 * w["gate"] + 0.2, 1.4, t + 0.2), "stone")
    eid += 1
    solid(s, eid, "Ravoq bezagi", (0, y0 + hgt - 0.65, w["z1"] + 0.72), (2 * w["gate"], 0.25, 0.25), "plaster", collider=False)
    eid += 1
    # well, stable, stalls, crates, courtyard torches
    wx, wz = P["well"]
    s.add(eid, "Quduq", pos=(wx, y0, wz), MeshRenderer=model("Models/Well.glb"), Rigidbody3D=k.rigidbody3d("Static", layer=M["WALL_LAYER"]),
          BoxCollider3D=k.box3d((0.9, 0.5, 0.9), (0, 0.5, 0)))
    eid += 1
    st = W["STABLE"]
    for px in (st["x0"], st["x1"]):
        for pz in (st["z0"], (st["z0"] + st["z1"]) / 2, st["z1"]):
            solid(s, eid, "Ustun", (px, y0 + 1.5, pz), (0.35, 3.0, 0.35), "wood", kind="Cylinder", tiling=1.0)
            eid += 1
    solid(s, eid, "Otxona tomi", ((st["x0"] + st["x1"]) / 2, y0 + 3.15, (st["z0"] + st["z1"]) / 2), (st["x1"] - st["x0"] + 1.0, 0.3, st["z1"] - st["z0"] + 1.0),
          "wood", collider=False, rot=(0, 0, math.radians(-6)))
    eid += 1
    for i, (ax, az) in enumerate(((-11.6, 3.5), (-11.6, -8.5))):
        s.add(eid, "Soyabon", pos=(ax + 0.6, y0 + 2.4, az), scale=(2.6, 0.06, 3.6), MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.9, albedo_map="Textures/ikat.png",
                                                                                                         tiling=(1, 1)))
        s.entities[eid]["Transform"]["Rotation"] = k.v(0, 0, math.radians(14))
        eid += 1
        for dz in (-1.6, 1.6):
            solid(s, eid, "Soyabon ustuni", (ax + 1.8, y0 + 1.1, az + dz), (0.15, 2.2, 0.15), "wood", collider=False, kind="Cylinder", tiling=1.0)
            eid += 1
    for i, (cx, cz) in enumerate(W["CRATES"]):
        s.add(eid, "Yashik", pos=(cx, fy(cx, cz), cz), MeshRenderer=model("Models/Crate.glb"), Rigidbody3D=k.rigidbody3d("Static"),
              BoxCollider3D=k.box3d((0.4, 0.4, 0.4), (0, 0.4, 0)))
        s.entities[eid]["Transform"]["Rotation"] = k.v(0, 0.3 * i, 0)
        eid += 1
    for tx, tz in ((-12.6, -14.6), (12.6, -14.6), (-12.6, 8.6), (12.6, 8.6)):
        s.add(eid, "Mangal", pos=(tx, y0, tz), MeshRenderer=model("Models/Brazier.glb"))
        fire(s, eid + 1, eid, (0, 1.05, 0), 0.7)
        point(s, eid + 2, eid, (0, 1.5, 0), (1.0, 0.58, 0.26), 3.5, 7.0)
        eid += 3
    # ---- camp (y 0)
    fx, fz = P["fire"]
    s.add(eid, "Gulxan", pos=(fx, 0, fz), MeshRenderer=model("Models/Rock0.glb"), scale=(0.35, 0.25, 0.35))
    fire(s, eid + 1, eid, (0, 0.4, 0), 1.3)
    smoke(s, eid + 2, eid, (0, 1.6, 0), 1.0)
    point(s, eid + 3, eid, (0, 1.0, 0), (1.0, 0.55, 0.24), 6.0, 10.0)
    eid += 4
    for i in range(7):
        a = 2 * math.pi * i / 7
        s.add(eid, "Tosh", pos=(fx + math.cos(a) * 0.75, 0, fz + math.sin(a) * 0.75), scale=(0.22, 0.2, 0.22), MeshRenderer=model(f"Models/Rock{i % 3}.glb"))
        eid += 1
    for name, mdl in (("tent1", "Tent"), ("tent2", "TentRed")):
        x, z = P[name]
        s.add(eid, "Chodir", pos=(x, fy(x, z), z), MeshRenderer=model(f"Models/{mdl}.glb"), Rigidbody3D=k.rigidbody3d("Static", layer=M["WALL_LAYER"]),
              BoxCollider3D=k.box3d((1.6, 1.2, 1.6), (0, 1.2, 0)))
        eid += 1
    for x, z, mdl in ((-1.8, 46.0, "BannerTeal"), (2.2, 46.4, "BannerIndigo"), (-3.0, 12.4, "BannerTeal")):
        s.add(eid, "Bayroq", pos=(x, fy(x, z), z), MeshRenderer=model(f"Models/{mdl}.glb"))
        eid += 1
    # the ledge: a rock shelf near the camp (the first fire stone waits on top)
    (lx, lz), (hx, hz), top = W["LEDGE"]
    solid(s, eid, "Qoya supasi", (lx, top / 2 - 0.4, lz), (hx * 2, top + 0.8, hz * 2), "t_rock", tiling=0.6)
    eid += 1
    for i, (dx, dz, sc) in enumerate(((-2.1, 0.8, 0.8), (1.9, -1.4, 0.9), (0.6, 2.0, 0.6))):
        s.add(eid, "Qoya", pos=(lx + dx, 0.0, lz + dz), scale=(sc, sc, sc), MeshRenderer=model(f"Models/Rock{i}.glb"))
        eid += 1
    # ---- plateau: the beacon platform with its stairs, the watchtower
    pf = W["PLATFORM"]
    py = 6.0
    solid(s, eid, "Mash'al supasi", (pf["cx"], py + pf["height"] / 2 - 0.5, pf["cz"]), (pf["half"] * 2, pf["height"] + 1.0, pf["half"] * 2), "stone")
    eid += 1
    for i in range(4):
        a = i * math.pi / 2
        solid(s, eid, "Supa tishi", (pf["cx"] + math.cos(a) * 2.6, py + pf["height"] + 0.35, pf["cz"] + math.sin(a) * 2.6), (0.6, 0.7, 0.6), "stone", collider=False)
        eid += 1
    steps = 12
    z_top, z_bottom = pf["cz"] + pf["half"], pf["stair_z0"]
    run = (z_bottom - z_top) / steps
    for i in range(steps):
        h = pf["height"] * (i + 1) / steps
        zc = z_bottom - run * (i + 0.5)
        solid(s, eid, "Zina", (0, py + h / 2 - 0.1, zc), (2.4, h + 0.2, run + 0.02), "stone", collider=False)
        eid += 1
    # one smooth ramp collider under the steps (the character walks it like stairs)
    length = math.hypot(z_bottom - z_top, pf["height"])
    angle = math.atan2(pf["height"], z_bottom - z_top)
    s.add(eid, "Zina rampasi", pos=(0, py + pf["height"] / 2 - 0.12, (z_top + z_bottom) / 2), scale=(2.4, 0.2, length),
          Rigidbody3D=k.rigidbody3d("Static", layer=M["WALL_LAYER"]), BoxCollider3D=k.box3d((0.5, 0.5, 0.5), friction=0.9))
    s.entities[eid]["Transform"]["Rotation"] = k.v(angle, 0, 0)
    eid += 1
    for sx in (-1, 1):
        solid(s, eid, "Zina yoni", (sx * 1.42, py + pf["height"] / 2 - 0.1, (z_top + z_bottom) / 2), (0.36, pf["height"] + 0.2, z_bottom - z_top), "stone")
        eid += 1
    tx, tz = P["tower"]
    solid(s, eid, "Kuzatuv minorasi", (tx, py + 7.0, tz), (4.4, 14.0, 4.4), "stone", kind="Cylinder", tiling=0.4)
    eid += 1
    solid(s, eid, "Minora ayvoni", (tx, py + 14.2, tz), (5.4, 0.4, 5.4), "wood", kind="Cylinder", collider=False)
    eid += 1
    for i in range(6):
        a = 2 * math.pi * i / 6
        solid(s, eid, "Ayvon ustuni", (tx + math.cos(a) * 2.3, py + 15.3, tz + math.sin(a) * 2.3), (0.22, 2.0, 0.22), "wood", kind="Cylinder", collider=False)
        eid += 1
    solid(s, eid, "Minora tomi", (tx, py + 17.3, tz), (6.2, 2.4, 6.2), "wood", kind="Cone", collider=False, albedo=(0.55, 0.32, 0.22, 1))
    eid += 1
    # ---- nature: packed pines on the slopes, a few close pines and rocks, distant snow peaks
    pines = []
    rocks = []
    import numpy as np
    rng = np.random.default_rng(17)
    h = W["height"]
    for _ in range(900):
        x = float(rng.uniform(-56, 56))
        z = float(rng.uniform(-56, 56))
        out = abs(x) - W["half_width"](z)
        if out < 1.5 or len(pines) >= 220:
            continue
        y = h(x, z)
        if y - W["floor_y"](z) > 26:
            continue
        pines.append({"Position": [round(x - ox, 3), round(y - 0.1, 3), round(z - oz, 3)], "Rotation": [0, round(float(rng.uniform(0, 6.28)), 3), 0],
                      "Scale": round(float(rng.uniform(0.75, 1.35)), 3)})
    s.add(2900, "Qarag'ayzor", pos=(ox, 0, oz), Foliage={"NearType": "Model", "FarType": "Model", "NearModel": "Models/Pine0.glb", "FarModel": "Models/PineFar.glb",
                                                         "Tint": [0.26, 0.42, 0.24, 1.0], "LODDistance": 45.0, "CullDistance": 140.0, "Hysteresis": 0.1,
                                                         "Backend": 0, "CastShadows": False, "Instances": pines})
    near = [(-13.0, 30.0), (14.5, 36.0), (-16.0, 47.0), (16.0, 22.0), (-17.0, 18.0), (17.5, -22.0), (-16.5, -24.0), (-14.5, -35.0), (15.0, -44.0)]
    for i, (x, z) in enumerate(near):
        s.add(eid, "Qarag'ay", pos=(x, h(x, z) - 0.1, z), scale=(1.0 + 0.1 * (i % 3),) * 3, MeshRenderer=model(f"Models/Pine{i % 3}.glb"))
        s.entities[eid]["Transform"]["Rotation"] = k.v(0, i * 1.7, 0)
        eid += 1
    for _ in range(300):
        x = float(rng.uniform(-30, 30))
        z = float(rng.uniform(-50, 54))
        out = abs(x) - W["half_width"](z)
        if not (-1.0 < out < 7.0) or len(rocks) >= 26:
            continue
        rocks.append((x, z, float(rng.uniform(0.6, 1.8)), float(rng.uniform(0, 6.28))))
    for i, (x, z, sc, yaw) in enumerate(rocks):
        s.add(eid, "Qoya", pos=(x, h(x, z) - 0.2 * sc, z), scale=(sc, sc, sc), MeshRenderer=model(f"Models/Rock{i % 3}.glb"))
        s.entities[eid]["Transform"]["Rotation"] = k.v(0, yaw, 0)
        eid += 1
    for i in range(11):
        a = 2 * math.pi * i / 11 + 0.3
        r = 150 + 25 * (i % 3)
        s.add(eid, "Qorli cho'qqi", pos=(math.cos(a) * r, -6.0, math.sin(a) * r - 10), scale=(1.0 + 0.15 * (i % 2),) * 3,
              MeshRenderer=model(f"Models/Peak{i % 3}.glb", cast=False))
        s.entities[eid]["Transform"]["Rotation"] = k.v(0, a, 0)
        eid += 1
    assert eid < 3000, eid
    # dust motes in the evening light over the courtyard
    s.add(8000, "Chang zarralari", pos=(0, 5.0, 0), ParticleSystem=k.particles(
        EmissionRate=14.0, MaxParticles=140, Shape=3, ShapeExtents=(14, 2.5, 16), LifetimeMin=5.0, LifetimeMax=9.0, SpeedMin=0.03, SpeedMax=0.15,
        Spread=180.0, StartColor=(1.0, 0.86, 0.6, 0.55), EndColor=(1.0, 0.8, 0.55, 0.0), StartSize=0.06, EndSize=0.03))


def terrain(W):
    layers = []
    for tex, col, tiling in (("t_rock", (1, 1, 1), 0.14), ("t_grass", (1, 1, 1), 0.22), ("t_dirt", (1, 1, 1), 0.3), ("t_scree", (1, 1, 1), 0.2)):
        layers.append({"Color": list(col), "Texture": f"Textures/{tex}.png", "Tiling": tiling, "NormalMap": f"Textures/{tex}_n.png",
                       "MaskMap": f"Textures/{tex}_orm.png", "Roughness": 0.92, "Metallic": 0.0, "Occlusion": 1.0, "NormalStrength": 1.0})
    paint = [{"Index": i, "Weight": w} for i, w in enumerate(W["weights"]) if w != [1.0, 0.0, 0.0, 0.0]]
    return {"Resolution": W["res"], "Size": [W["size"], W["size"]], "HeightLimit": 64.0, "Collision": True, "Heights": W["heights"],
            "LODEnabled": True, "LockBorderLOD": False, "LODDistanceScale": 2.0, "Layers": layers, "Paint": paint}


# ------------------------------------------------------------------------------------------------ actors


def animator(walk, run):
    """The humanoid controller: Locomotion (1D blend by Speed: Idle 0, Walk = walk speed, Run = run speed), Air, Land,
    Dead; layers UpperBody (override from Spine: Attack/Cast) and Hit (additive Hurt); clip events Hit, Release and
    Footstep (both feet of Walk and Run)."""
    cond = lambda p, op, t=0.0: {"Parameter": p, "Op": op, "Threshold": t}
    motion = lambda clip, th: {"Clip": clip, "Threshold": th, "Speed": 1.0, "Position": [0, 0]}
    state = lambda name, clip, loop, graph: {"Name": name, "Clip": clip, "Speed": 1.0, "Loop": loop, "SpeedParameter": "", "Duration": 0.0, "Graph": graph}
    loco = state("Locomotion", "", True, [80, 120])
    loco.update(Type="BlendTree1D", BlendParameter="Speed", BlendParameterY="", Motions=[motion("Idle", 0.0), motion("Walk", walk), motion("Run", run)])
    return {"Clip": "", "Speed": 1.0, "Loop": True, "Playing": True, "PlayInEditor": True, "DefaultFade": 0.2, "DefaultState": 0,
            "Parameters": [{"Name": "Speed", "Type": 0, "Value": 0}, {"Name": "Grounded", "Type": 1, "Value": 1},
                           {"Name": "VerticalSpeed", "Type": 0, "Value": 0}, {"Name": "Dead", "Type": 1, "Value": 0}],
            "States": [loco, state("Air", "Fall", True, [360, 40]), state("Land", "Land", False, [360, 200]), state("Dead", "Death", False, [620, 120])],
            "Transitions": [
                {"From": -1, "To": 3, "Fade": 0.15, "HasExitTime": False, "ExitTime": 1.0, "Priority": 10, "InterruptDuringFade": True, "Conditions": [cond("Dead", 2)]},
                {"From": 0, "To": 1, "Fade": 0.12, "HasExitTime": False, "ExitTime": 1.0, "Priority": 5, "InterruptDuringFade": True,
                 "Conditions": [cond("Grounded", 3), cond("VerticalSpeed", 0, 1.5)]},
                {"From": 0, "To": 1, "Fade": 0.2, "HasExitTime": False, "ExitTime": 1.0, "Priority": 4, "InterruptDuringFade": True,
                 "Conditions": [cond("Grounded", 3), cond("VerticalSpeed", 1, -3.0)]},
                {"From": 1, "To": 2, "Fade": 0.06, "HasExitTime": False, "ExitTime": 1.0, "Priority": 5, "InterruptDuringFade": True, "Conditions": [cond("Grounded", 2)]},
                {"From": 2, "To": 0, "Fade": 0.15, "HasExitTime": True, "ExitTime": 0.85, "Priority": 0, "InterruptDuringFade": True, "Conditions": []},
                {"From": 3, "To": 0, "Fade": 0.25, "HasExitTime": False, "ExitTime": 1.0, "Priority": 5, "InterruptDuringFade": True, "Conditions": [cond("Dead", 3)]}],
            "Events": [{"Clip": "Attack", "Time": 0.42, "Name": "Hit"}, {"Clip": "Cast", "Time": 0.45, "Name": "Release"},
                       {"Clip": "Walk", "Time": 0.0, "Name": "Footstep"}, {"Clip": "Walk", "Time": 0.5, "Name": "Footstep"},
                       {"Clip": "Run", "Time": 0.0, "Name": "Footstep"}, {"Clip": "Run", "Time": 0.5, "Name": "Footstep"}],
            "AnyStateGraph": [80, -60],
            "Layers": [{"Name": "UpperBody", "Clip": "", "Speed": 1.0, "Loop": False, "Weight": 0.0, "Blend": "Override", "Mask": ["Spine"]},
                       {"Name": "Hit", "Clip": "", "Speed": 1.0, "Loop": False, "Weight": 1.0, "Blend": "Additive", "Mask": []}]}


def footsteps(volume, reference):
    return {"Sounds": [{"Path": f"Audio/step_{i}.wav"} for i in range(4)], "Event": "Footstep", "Volume": volume, "VolumeRandom": [0.8, 1.0],
            "PitchRandom": [0.9, 1.1], "SpeedParameter": "Speed", "ReferenceSpeed": reference, "SpeedPitch": 0.12, "SpeedVolume": 0.3, "MinSpeed": 0.4,
            "MinInterval": 0.12, "Spatial": True, "MinDistance": 1.0, "MaxDistance": 22.0, "Output": "SFX"}


def humanoid(s, eid, tag, pos, yaw, look, graph, overrides, tags, hero=False, walk=1.8, run=5.4, speed=2.6, active=True):
    comps = {"CharacterController3D": k.character3d(0.38 if look != "Warden" else 0.5, 1.8 if look != "Warden" else 2.25, 50.0, 0.42,
                                                    layer=M["HERO_LAYER"] if hero else M["ENEMY_LAYER"]),
             "MeshRenderer": model(f"Models/{look}.glb"), "Animator": animator(walk, run), "Footsteps": footsteps(0.75 if hero else 0.35, walk),
             "NativeScript": graph_script(graph, **overrides)}
    if not hero:
        comps["NavAgent"] = {"Grid": 2001, "Speed": speed, "Acceleration": 14.0, "AngularSpeed": 540.0, "Radius": 0.4, "StoppingDistance": 0.2,
                             "RepathInterval": 0.5, "UpdateRotation": True, "Crowd": {"Enabled": True}}
    s.add(eid, tag, pos=pos, tags=tags, active=active, **comps)
    s.entities[eid]["Transform"]["Rotation"] = k.v(0, yaw, 0)
    return eid


def attach(s, eid, parent, tag, path, bone, pos=(0, 0, 0), rot=(0, 0, 0), active=True):
    s.add(eid, tag, parent, pos=pos, active=active, MeshRenderer=model(path), BoneAttachment={"Target": parent, "Bone": bone, "InheritBoneScale": False})
    s.entities[eid]["Transform"]["Rotation"] = k.v(*rot)
    return eid


# ------------------------------------------------------------------------------------------------ build


def build(assets, project, C, W, ENEMIES, ARCHETYPES, route, codes, oracle, masks):
    M.update(masks)
    os.makedirs(os.path.join(assets, "Scripts"), exist_ok=True)
    projectiles(assets, project)
    level_scene(assets, project, C, W, ENEMIES, ARCHETYPES, route, codes, oracle)
    menu_scene(assets, project, W)
    hero_graph(assets, project, C)
    camera_graph(assets, project)
    game_graph(assets, project, C)
    enemy_graph(assets, project)
    menu_graph(assets, project)


def projectiles(assets, project):
    """Visual-only projectile prefabs (no bodies, colliders or scripts): pooled by CombatWorld."""
    for name, path, glow in (("Bolt", "Models/Bolt.glb", (1.0, 0.55, 0.2)), ("Arrow", "Models/Arrow.glb", None)):
        e = [{"Entity": 1, "Tag": f"{name} visual", "Transform": {"Translation": [0, 0, 0], "Rotation": [0, 0, 0], "Scale": [1, 1, 1]},
              "MeshRenderer": model(path, cast=False)}]
        if glow:
            e[0]["ChildOrder"] = [2]
            e.append({"Entity": 2, "Tag": "Light", "Parent": 1, "Transform": {"Translation": [0, 0, 0], "Rotation": [0, 0, 0], "Scale": [1, 1, 1]},
                      "PointLight": k.point_light(glow, 4.0, 5.0)})
        rel = f"Prefabs/{name}.zprefab"
        k.write_json(os.path.join(assets, rel), {"ZaminEntities": 1, "Entities": e})
        k.write_zmeta(assets, rel, "Prefab", {}, project)


def level_scene(assets, project, C, W, ENEMIES, ARCHETYPES, route, codes, oracle):
    s = Scene("Dovon", settings_3d())
    s.settings["Post"]["FXAA"] = True
    world(s, W, True)
    P = W["P"]
    fy = W["floor_at"]
    # navigation over the valley path, the terrace and the plateau
    s.add(2001, "Navigatsiya", pos=(0, 5.0, 2.0), NavGrid3D={"Offset": [0, 0, 0], "Size": [40, 18, 100], "CellSize": 0.5, "AgentRadius": 0.4, "AgentHeight": 1.8,
                                                             "MaxSlope": 42.0, "StepHeight": 0.42, "LayerMask": (1 << M["WORLD_LAYER"]) | (1 << M["WALL_LAYER"]),
                                                             "Diagonals": True, "CutCorners": False, "BakeOnStart": True})
    # hero, sword and shield (shown once equipped), camera
    hero = {"Slot": "Run1", "Route": [[round(p[0], 3), round(p[1], 3), round(p[2], 3)] for p in route], "Steps": list(codes)}
    humanoid(s, HERO, "Temur", (0.0, 0.0, 42.0), 0.0, "Hero", "Hero", hero, ["Hero"], hero=True, walk=C["Hero"]["Walk"], run=C["Hero"]["Run"])
    attach(s, SWORD, HERO, "Shamshir", "Models/Sword.glb", "Hand.R", (0, -0.06, -0.02), (math.radians(-70), 0, 0), active=False)
    attach(s, SHIELD, HERO, "Qalqon", "Models/Shield.glb", "LowerArm.L", (-0.07, -0.12, 0.0), (0, math.radians(-90), 0), active=False)
    s.add(CAMERA, "Kamera", pos=(0.0, 2.4 + 1.5, 47.6), Camera=k.camera_persp(1.0, 0.1, 400.0, (0.6, 0.5, 0.45, 1), shake={"Trauma": 0.0, "MaxOffset": 0.4, "MaxAngle": 0.05}),
          NativeScript=graph_script("Camera3D"))
    s.entities[CAMERA]["Transform"]["Rotation"] = k.v(math.radians(-12), 0, 0)
    # interactables (pickups, chests, the gate, shrines, the beacon)
    pickups = [("FireStone", (P["ledge"][0], P["ledge"][1]), W["LEDGE"][2]), ("FireStone", P["stone2"], None), ("FireStone", P["stone3"], None),
               ("Potion", P["potion1"], None), ("Potion", P["potion2"], None)]
    pickup_ids = [5500 + i * 4 for i in range(len(pickups))]
    for eid, (item, (x, z), y) in zip(pickup_ids, pickups):
        y = fy(x, z) if y is None else y
        if item == "FireStone":
            s.add(eid, "Olov toshi", pos=(x, y + 0.45, z), tags=["Pickup", "FireStone"], MeshRenderer=model("Models/FireStone.glb", cast=False))
            point(s, eid + 1, eid, (0, 0.2, 0), (1.0, 0.5, 0.15), 3.0, 4.5)
            s.add(eid + 2, "Uchqun", eid, ParticleSystem=k.particles(EmissionRate=6.0, MaxParticles=24, Shape=2, ShapeRadius=0.3, LifetimeMin=0.8,
                                                                      LifetimeMax=1.6, SpeedMin=0.2, SpeedMax=0.5, StartColor=(1.0, 0.7, 0.3, 1.0),
                                                                      EndColor=(1.0, 0.3, 0.1, 0.0), StartSize=0.07, EndSize=0.02))
        else:
            s.add(eid, "Ichimlik", pos=(x, y + 0.05, z), tags=["Pickup", "Potion"], MeshRenderer=model("Models/Potion.glb", cast=False))
    uses = [("Chest", P["chest_sword"], "Dovon-Chest-1", dict(Prompt="Sandiqni ochish", Reward="Shamshir"), "Models/Chest.glb"),
            ("Chest", P["chest_shield"], "Dovon-Chest-2", dict(Prompt="Sandiqni ochish", Reward="Qalqon"), "Models/Chest.glb"),
            ("Gate", P["gate"], "Dovon-Gate", dict(Prompt="Darvozani ochish"), None),
            ("Shrine", P["shrine1"], "Dovon-Shrine-1", dict(Prompt="Saqlash ziyoratgohi", MaxUses=0, Cooldown=2), "Models/Shrine.glb"),
            ("Shrine", P["shrine2"], "Dovon-Shrine-2", dict(Prompt="Saqlash ziyoratgohi", MaxUses=0, Cooldown=2), "Models/Shrine.glb"),
            ("Shrine", P["shrine3"], "Dovon-Shrine-3", dict(Prompt="Saqlash ziyoratgohi", MaxUses=0, Cooldown=2), "Models/Shrine.glb"),
            ("Beacon", P["beacon"], "Dovon-Beacon", dict(Prompt="Mash'alni yoqish", Required="FireStone", RequiredCount=3, Consume=True), "Models/Beacon.glb")]
    use_ids = [5600 + i * 4 for i in range(len(uses))]
    for eid, (kind, (x, z), key, opts, path) in zip(use_ids, uses):
        if kind == "Gate":
            solid(s, eid, "Darvoza", (x, 3.0 + 1.95, z), (2 * W["WALLS"]["gate"] + 0.1, 3.9, 0.45), "wood", tiling=0.6)
            s.entities[eid]["Tags"] = ["Gate"]
            continue
        y = fy(x, z) if kind != "Beacon" else 9.0
        s.add(eid, kind, pos=(x, y, z), tags=[kind], MeshRenderer=model(path), Rigidbody3D=k.rigidbody3d("Static", layer=M["WALL_LAYER"]),
              BoxCollider3D=k.box3d((0.5, 0.45, 0.45) if kind == "Chest" else (0.45, 0.8, 0.45) if kind == "Shrine" else (1.2, 0.6, 1.2),
                                    (0, 0.45, 0) if kind == "Chest" else (0, 0.8, 0) if kind == "Shrine" else (0, 0.6, 0)))
        if kind == "Chest":
            s.entities[eid]["Transform"]["Rotation"] = k.v(0, math.pi if z > 0 else -math.pi / 2, 0)
        if kind == "Shrine":
            point(s, eid + 1, eid, (0, 1.8, 0), (0.4, 0.85, 1.0), 3.0, 5.0)
    # the beacon's fire (lit at the end), the Zilzila dust ring, hit sparks, the level-up ring
    bx, bz = P["beacon"]
    fire(s, 8102, None, (bx, 11.4, bz), 4.0, active=False)
    s.add(8103, "Mash'al nuri", pos=(bx, 13.0, bz), active=False, PointLight=k.point_light((1.0, 0.62, 0.25), 14.0, 40.0))
    s.add(8106, "Mash'al tutuni", pos=(bx, 14.0, bz), active=False, ParticleSystem=k.particles(
        EmissionRate=10.0, MaxParticles=120, Shape=2, ShapeRadius=0.8, LifetimeMin=3.0, LifetimeMax=5.0, SpeedMin=1.0, SpeedMax=2.0, Direction=(0.3, 1, 0),
        Spread=10.0, StartColor=(0.35, 0.3, 0.28, 0.45), EndColor=(0.5, 0.45, 0.42, 0.0), StartSize=1.0, EndSize=4.0, Additive=False))
    s.add(8100, "Zarba uchquni", pos=(0, -50, 0), ParticleSystem=k.particles(
        Playing=False, Looping=False, EmissionRate=0.0, MaxParticles=80, Shape=2, ShapeRadius=0.15, LifetimeMin=0.2, LifetimeMax=0.45, SpeedMin=2.0,
        SpeedMax=5.0, Spread=180.0, Gravity=(0, -6, 0), StartColor=(1.0, 0.85, 0.5, 1.0), EndColor=(1.0, 0.3, 0.1, 0.0), StartSize=0.1, EndSize=0.02))
    s.add(8101, "Daraja halqasi", HERO, pos=(0, 0.2, 0), ParticleSystem=k.particles(
        Playing=False, Looping=False, EmissionRate=0.0, MaxParticles=120, Shape=1, ShapeRadius=0.7, LifetimeMin=0.6, LifetimeMax=1.2, SpeedMin=0.8,
        SpeedMax=2.0, Direction=(0, 1, 0), Spread=30.0, StartColor=(1.0, 0.9, 0.55, 1.0), EndColor=(0.6, 0.8, 1.0, 0.0), StartSize=0.12, EndSize=0.03))
    s.add(8104, "Zilzila changi", HERO, pos=(0, 0.1, 0), ParticleSystem=k.particles(
        Playing=False, Looping=False, EmissionRate=0.0, MaxParticles=160, Shape=1, ShapeRadius=0.6, LifetimeMin=0.5, LifetimeMax=1.0, SpeedMin=3.0,
        SpeedMax=6.0, Direction=(0, 0.2, 0), Spread=90.0, Drag=2.0, StartColor=(0.85, 0.72, 0.55, 0.8), EndColor=(0.7, 0.6, 0.5, 0.0), StartSize=0.4,
        EndSize=1.2, Additive=False))
    s.add(8105, "To'lqin changi", pos=(0, -50, 0), ParticleSystem=k.particles(
        Playing=False, Looping=False, EmissionRate=0.0, MaxParticles=160, Shape=1, ShapeRadius=1.0, LifetimeMin=0.5, LifetimeMax=1.0, SpeedMin=4.0,
        SpeedMax=7.0, Direction=(0, 0.2, 0), Spread=90.0, Drag=2.0, StartColor=(1.0, 0.45, 0.2, 0.9), EndColor=(0.5, 0.2, 0.1, 0.0), StartSize=0.4,
        EndSize=1.4))
    # controller and audio
    use_kinds = [u[0] for u in uses]
    game = {"Slot": "Run1", "Self": "Scenes/Dovon.zscene",
            "PickupIds": pickup_ids, "PickupKeys": [f"Dovon-{item}-{i:02d}" for i, (item, _, _) in enumerate(pickups)], "PickupItems": [p[0] for p in pickups],
            "UseIds": use_ids, "UseKeys": [u[2] for u in uses], "UsePrompts": [u[3]["Prompt"] for u in uses],
            "UseRequired": [u[3].get("Required", "") for u in uses], "UseCounts": [u[3].get("RequiredCount", 1) for u in uses],
            "UseConsume": [bool(u[3].get("Consume", False)) for u in uses], "UseRewards": [u[3].get("Reward", "") for u in uses],
            "UseMaxUses": [u[3].get("MaxUses", 1) for u in uses], "UseCooldown": [u[3].get("Cooldown", 0.2) for u in uses],
            "UseDeactivate": [k_ in ("Gate",) for k_ in use_kinds], "Boss": 5060}
    s.add(GAME, "O'yin", NativeScript=graph_script("Game", **game))
    s.add(MUSIC, "Musiqa", GAME, AudioSource=k.audio_source("Audio/music_dovon.wav", 0.75, True, True, "Music", True))
    s.add(AMBIENCE, "Muhit", GAME, AudioSource=k.audio_source("Audio/amb_pass.wav", 0.55, True, True, "SFX"))
    s.add(BOSS_MUSIC, "Qorovul musiqasi", GAME, AudioSource=k.audio_source("Audio/music_warden.wav", 0.0, True, False, "Music", True))
    # enemies
    oracle["Keys"] = []
    for i, (kind, (x, z), key, patrol, tags) in enumerate(ENEMIES):
        spec = ARCHETYPES[kind]
        eid = 5000 + i * 10
        lose = spec["View"] + 2
        over = {"Key": key, "Kind": kind, "HP": spec["HP"], "Armor": spec["Armor"], "Damage": spec["Damage"], "Cooldown": spec["Cooldown"], "Speed": spec["Speed"],
                "View": spec["View"], "Lose": lose, "FOV": spec["FOV"], "Leash": max(spec["Leash"], lose), "Enter": spec["Enter"], "Exit": spec["Exit"],
                "Ranged": spec["Ranged"], "Ability": spec.get("Ability", ""), "Reach": spec["Reach"], "Patrol": [[p[0], 0, p[1]] for p in patrol],
                "Boss": bool(spec.get("Boss")), "Ring": eid + 3 if spec.get("Boss") else 0}
        yaw = math.pi  # facing south, down the pass
        humanoid(s, eid, {"guard": "Qorovul", "archer": "Kamonchi", "warden": "Qora qorovul"}[kind], (x, fy(x, z), z), yaw, spec["model"], "Enemy", over, tags,
                 speed=spec["Speed"])
        bone, rot, off = ("Hand.R", (math.radians(-80), 0, 0), (0, -0.05, -0.02)) if kind != "archer" else ("Hand.L", (0, 0, 0), (0, -0.06, 0))
        if kind == "guard":
            rot, off = (0, 0, 0), (0, -0.05, 0)
        attach(s, eid + 1, eid, spec["weapon"], f"Models/{spec['weapon']}.glb", bone, off, rot)
        if spec.get("Boss"):
            point(s, eid + 2, eid, (0, 2.6, -0.4), (1.0, 0.35, 0.15), 2.5, 5.0)
            s.add(eid + 3, "Zarba halqasi", eid, pos=(0, 0.08, 0), active=False, MeshRenderer=model("Models/Ring.glb", cast=False))
        oracle["Keys"].append(key)
    hud(s, C)
    save_scene(s, assets, project)
    oracle["Pickups"] = len(pickups)


def hud(s, C):
    ui = UI(s, GAME)
    s.add(CANVAS, "UI", UICanvas=k.ui_canvas())
    root = ui.stretch(6001, "HUD", CANVAS)
    # top-left vitals
    ui.panel(6010, "Holat", root, (24, -24), (560, 178), anchor=(0, 1), pivot=(0, 1))
    ui.image(6011, "Daraja nishoni", 6010, "badge", (0, 1), (24, -22), (104, 104), pivot=(0, 1))
    ui.text(6012, "Daraja", 6011, "1", (0.5, 0.5), (0, 2), (90, 60), 46, ACCENT)
    ui.bar(6013, "Tajriba", 6010, (0, 1), (24, -136), (104, 14), (0.6, 0.85, 1.0, 1), 0, pivot=(0, 1))
    ui.bar(6014, "Sog'liq", 6010, (0, 1), (148, -30), (380, 38), (0.92, 0.26, 0.24, 1), 26, pivot=(0, 1))
    ui.bar(6015, "Energiya", 6010, (0, 1), (148, -78), (380, 32), (0.38, 0.66, 1.0, 1), 25, pivot=(0, 1))
    ui.text(6016, "Nom", 6010, "TEMUR  ·  CHOPAR", (0, 1), (148, -122), (380, 34), 25, MUTED, "Left", pivot=(0, 1))
    # top-right: fire stones and potions
    ui.panel(6020, "Boyliklar", root, (-24, -24), (260, 96), anchor=(1, 1), pivot=(1, 1))
    for i, (icon, eid) in enumerate((("stone", 6021), ("potion", 6023))):
        ui.image(eid, icon, 6020, f"icon_{icon}", (0, 0.5), (22 + i * 120, 0), (48, 48), pivot=(0, 0.5))
        ui.text(eid + 1, f"{icon} soni", 6020, "0", (0, 0.5), (74 + i * 120, 0), (60, 40), 30, INK, "Left", pivot=(0, 0.5))
    # quest tracker
    ui.panel(6030, "Vazifalar", root, (-24, -136), (520, 300), anchor=(1, 1), pivot=(1, 1))
    ui.text(6031, "Sarlavha", 6030, "Vazifalar", (0.5, 1), (0, -40), (460, 40), 28, ACCENT, key="hud.quests")
    for i in range(5):
        ui.text(6032 + i, f"Vazifa {i + 1}", 6030, "", (0, 1), (32, -84 - i * 40), (460, 34), 25, INK, "Left", pivot=(0, 1))
    # ability bar: strike, Bolt, Zilzila, potion
    bar = ui.panel(6040, "Qobiliyatlar", root, (0, 22), (580, 132), anchor=(0.5, 0), pivot=(0.5, 0))
    for i, (icon, hint, label) in enumerate((("melee", "[Attack]", "Zarba"), ("bolt", "[Bolt]", "Bolt"), ("nova", "[Slam]", "Zilzila"), ("potion", "[Potion]", "Potion"))):
        x = -195 + i * 130
        ui.image(6041 + i * 4, f"Slot {label}", bar, "slot", (0.5, 0.5), (x, 10), (104, 104), kind="Sliced")
        ui.image(6042 + i * 4, f"Belgi {label}", 6041 + i * 4, f"icon_{icon}", (0.5, 0.5), (0, 8), (68, 68))
        ui.bar(6043 + i * 4, f"Tayyorlik {label}", 6041 + i * 4, (0.5, 0), (0, 4), (84, 12), (0.98, 0.76, 0.32, 1), 0, back=(0.05, 0.04, 0.08, 0.6))
        ui.text(6044 + i * 4, f"Tugma {label}", bar, hint, (0.5, 0.5), (x, -58), (140, 32), 25, MUTED, hints=True)
    ui.text(6090, "Ishora", root, "", (0.5, 0), (0, 178), (900, 40), 26, INK)
    ui.text(6091, "Xabar", root, "", (0.5, 1), (0, -170), (1100, 60), 40, ACCENT)
    ui.text(6092, "Daraja nomi", root, "DOVON", (0.5, 1), (0, -96), (900, 70), 56, INK)
    ui.text(6093, "Boss nomi", root, "Qora qorovul", (0.5, 0), (0, 214), (600, 34), 26, (1.0, 0.55, 0.4, 1), active=False, key="hud.boss")
    ui.rect(6094, "Boss sog'lig'i", root, (0.5, 0), None, (0, 186), (760, 22), 0, active=False,
            UIProgressBar=k.ui_progress(1.0, (0.05, 0.04, 0.08, 0.85), (0.9, 0.3, 0.2, 1), 2.0, 6.0))
    for i in range(4):
        ui.text(6070 + i, f"Jang {i + 1}", root, "", (0, 0.5), (32, 60 - i * 36), (380, 32), 25, (1.0, 0.6, 0.5, 1), "Left", pivot=(0, 0.5))
    ui.stretch(6080, "Jarohat", root, order=-1, active=False, UIImage=k.ui_image((1, 1, 1, 0.8), "Textures/ui.png", "vignette", "Sliced", 1.0, raycast=False))
    ui.text(6081, "Boshqaruv", root, "[Inventory] inventar   [Pause] pauza", (1, 0), (-24, 18), (640, 32), 25, MUTED, "Right", hints=True, pivot=(1, 0))
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
    rows = [ui.button(6211 + i, f"Qator {i + 1}", 6201, "", "", "Select", (-200, 150 - i * 68), (460, 58), 26) for i in range(6)]
    acts = [ui.button(6221, "Kiyish", 6201, "Kiyish", "bag.equip", "Equip", (270, 150), (300, 58), 26),
            ui.button(6222, "Ichish", 6201, "Ichish", "bag.drink", "Drink", (270, 82), (300, 58), 26),
            ui.button(6223, "Keyingi", 6201, "Keyingi", "bag.next", "NextPage", (270, 14), (300, 58), 26),
            ui.button(6224, "Yopish", 6201, "Yopish", "bag.close", "CloseBag", (270, -54), (300, 58), 26)]
    chain_nav(s, rows + acts)
    for i, eid in enumerate(rows):
        s.entities[eid]["UINavigation"].update(Right=acts[min(i, 3)])
    for i, eid in enumerate(acts):
        s.entities[eid]["UINavigation"].update(Left=rows[i])
    # defeat / victory
    ui.dim(6300, "Mag'lubiyat", 52)
    ui.panel(6301, "Mag'lubiyat paneli", 6300, (0, 0), (760, 420))
    ui.text(6302, "Sarlavha", 6301, "YO'L YOPILDI", (0.5, 1), (0, -80), (700, 80), 60, (1.0, 0.45, 0.4, 1), key="defeat.title")
    ui.text(6303, "Izoh", 6301, "Temur uch marta yiqildi", (0.5, 1), (0, -150), (700, 40), 26, MUTED)
    d = [ui.button(6311, "Qayta", 6301, "Oxirgi saqlashdan", "defeat.retry", "Retry", (0, -30), select=True),
         ui.button(6312, "Menyu", 6301, "Bosh menyu", "end.menu", "Menu", (0, -110))]
    chain_nav(s, d)
    ui.dim(6400, "G'alaba", 53)
    ui.panel(6401, "G'alaba paneli", 6400, (0, 0), (860, 600))
    ui.text(6402, "Sarlavha", 6401, "MASH'AL YONDI", (0.5, 1), (0, -86), (800, 90), 72, ACCENT, key="victory.title")
    ui.text(6403, "Izoh", 6401, "Karvon yo'lni topdi", (0.5, 1), (0, -160), (800, 44), 30, INK, key="victory.sub")
    ui.text(6410, "Natijalar", 6401, "", (0.5, 0.5), (0, -10), (720, 220), 26, INK, wrap=True)
    v = [ui.button(6411, "Menyu", 6401, "Bosh menyu", "end.menu", "Menu", (0, -220), select=True)]
    chain_nav(s, v)


def menu_scene(assets, project, W):
    s = Scene("Menu", settings_3d())
    world(s, W, False)
    # above the valley camp, looking north up the pass at the caravanserai and the watchtower in the evening sun (the
    # Menu graph sways it slowly); the view keeps the slopes' pines at the sides and the subject right of the menu panel
    s.add(CAMERA, "Kamera", pos=MENU_EYE, Camera=k.camera_persp(0.9, 0.1, 400.0, (0.6, 0.5, 0.45, 1)))
    s.entities[CAMERA]["Transform"]["Rotation"] = k.v(MENU_PITCH, MENU_YAW, 0)
    # the courier resting at the camp fire; a guard on the gate; the unlit beacon
    s.add(HERO, "Temur", pos=(-2.6, 0.0, 39.6), MeshRenderer=model("Models/Hero.glb"),
          Animator={"Clip": "Idle", "Speed": 1.0, "Loop": True, "Playing": True, "PlayInEditor": True, "DefaultFade": 0.2, "DefaultState": -1, "Parameters": [],
                    "States": [], "Transitions": []})
    s.entities[HERO]["Transform"]["Rotation"] = k.v(0, math.radians(150), 0)
    s.add(5000, "Qorovul", pos=(1.6, 3.0, 12.4), MeshRenderer=model("Models/Guard.glb"),
          Animator={"Clip": "Idle", "Speed": 0.8, "Loop": True, "Playing": True, "PlayInEditor": True, "DefaultFade": 0.2, "DefaultState": -1, "Parameters": [],
                    "States": [], "Transitions": []})
    s.entities[5000]["Transform"]["Rotation"] = k.v(0, math.pi, 0)
    attach(s, 5001, 5000, "Nayza", "Models/Spear.glb", "Hand.R", (0, -0.05, 0))
    s.add(GAME, "Menyu", NativeScript=graph_script("Menu"))
    s.add(MUSIC, "Musiqa", GAME, AudioSource=k.audio_source("Audio/music_menu.wav", 0.8, True, True, "Music", True))
    s.add(AMBIENCE, "Muhit", GAME, AudioSource=k.audio_source("Audio/amb_pass.wav", 0.5, True, True, "SFX"))
    ui = UI(s, GAME)
    s.add(CANVAS, "UI", UICanvas=k.ui_canvas())
    ui.text(6010, "Nom", CANVAS, "DOVON MASH'ALI", (0, 1), (110, -96), (1200, 150), 112, ACCENT, "Left", pivot=(0, 1), UIAnimation=k.ui_anim("Fade", "None", 0.8))
    ui.text(6011, "Shior", CANVAS, "Karvon yo'li afsonasi", (0, 1), (118, -236), (900, 50), 36, INK, "Left", key="menu.subtitle", pivot=(0, 1))
    s.add(6020, "Asosiy", CANVAS, UITransform=k.ui_rect((0, 0.5), None, (0, 0.5), (110, -70), (540, 440)),
          UIImage=k.ui_image((1, 1, 1, 0.97), "Textures/ui.png", "panel", "Sliced", 1.0, raycast=True),
          UILayoutGroup=k.ui_layout("Vertical", (50, 50, 46, 46), 14, align=1, fit_height=True))
    [ui.button(6021, "Yangi o'yin", 6020, "Yangi o'yin", "menu.new", "New", (0, 0), (440, 64), select=True),
     ui.button(6022, "Davom etish", 6020, "Davom etish", "menu.continue", "Continue", (0, 0), (440, 64)),
     ui.button(6023, "Sozlamalar", 6020, "Sozlamalar", "menu.settings", "Settings", (0, 0), (440, 64)),
     ui.button(6024, "Mualliflar", 6020, "Mualliflar", "menu.credits", "Credits", (0, 0), (440, 64)),
     ui.button(6025, "Chiqish", 6020, "Chiqish", "menu.quit", "Quit", (0, 0), (440, 64))]
    ui.text(6030, "Rekord", CANVAS, "", (0, 0), (118, 124), (900, 40), 26, MUTED, "Left", pivot=(0, 0))
    # the controls line: 78 % of the view's width (it wraps on narrow views and stays out of a 21:9 view's corners)
    s.add(6031, "Boshqaruv", CANVAS, UITransform=k.ui_rect((0.11, 0), (0.89, 0), (0.5, 0), (0, 30), (0, 76), 25),
          Text=k.ui_text("[Move] yurish   [LookX|Sichqoncha] qarash   [Jump] sakrash   [Attack] zarba   [Bolt] olov o'qi   [Interact] ishlatish   [Pause] pauza",
                         MUTED, "Center", hints=True, wrap=True))
    ui.text(6032, "Versiya", CANVAS, "v1.0  Zamin Engine", (1, 0), (-24, 12), (420, 32), 25, MUTED, "Right", pivot=(1, 0))
    # settings
    ui.dim(6100, "Sozlamalar", 50)
    ui.panel(6101, "Sozlamalar paneli", 6100, (0, 0), (760, 650))
    ui.text(6102, "Sarlavha", 6101, "Sozlamalar", (0.5, 1), (0, -62), (600, 60), 44, ACCENT, key="menu.settings")
    # music, effects and the UI scale (80..120 % in 10 % steps: the slider's 8..12)
    for i, (eid, label, key) in enumerate(((6111, "Musiqa", "menu.music"), (6112, "Ovoz effektlari", "menu.sfx"), (6116, "Interfeys o'lchami", "menu.uiscale"))):
        ui.text(eid + 10, label, 6101, label, (0.5, 0.5), (-180, 175 - i * 80), (300, 40), 28, INK, "Left", key=key)
        ui.rect(eid, f"{label} slider", 6101, (0.5, 0.5), None, (150, 175 - i * 80), (320, 44),
                UISlider={"Min": 8.0 if i == 2 else 0.0, "Max": 12.0 if i == 2 else 1.0, "Value": (0.55, 0.9, 10.0)[i], "WholeNumbers": i == 2,
                          "Direction": "LeftToRight", "Interactable": True,
                          "TrackThickness": 10.0, "HandleSize": 30.0, "Rounded": True, "TrackColor": [0.1, 0.12, 0.2, 1], "FillColor": list(ACCENT),
                          "HandleColor": [1.0, 0.95, 0.85, 1]}, UINavigation=k.ui_nav("Explicit", i == 0, ACCENT[:3] + (1,)))
    ui.text(6123, "To'liq ekran", 6101, "To'liq ekran", (0.5, 0.5), (-180, -65), (300, 40), 28, INK, "Left", key="menu.fullscreen")
    ui.rect(6113, "To'liq ekran toggle", 6101, (0.5, 0.5), None, (150, -65), (90, 44),
            UIToggle={"IsOn": False, "Interactable": True, "Style": "Switch", "Mark": "Check", "BoxSize": 34.0, "LabelSpacing": 8.0, "Group": "",
                      "AllowSwitchOff": True, "BoxColor": [0.12, 0.14, 0.22, 1], "OnColor": list(ACCENT), "MarkColor": [1, 1, 1, 1]},
            UIButton=k.ui_button((1, 1, 1, 1), (1.2, 1.2, 1.2, 1), (1, 1, 1, 1), "Fullscreen", GAME), UINavigation=k.ui_nav("Explicit", False, ACCENT[:3] + (1,)))
    st = [6111, 6112, 6116, 6113, ui.button(6114, "Til", 6101, "Til: O'zbekcha", "menu.language", "Language", (0, -145), (420, 60), 26),
          ui.button(6115, "Orqaga", 6101, "Orqaga", "menu.back", "Back", (0, -225), (420, 60), 28)]
    chain_nav(s, st)
    ui.dim(6200, "Mualliflar", 51)
    ui.panel(6201, "Mualliflar paneli", 6200, (0, 0), (1100, 520))
    ui.text(6202, "Sarlavha", 6201, "Mualliflar", (0.5, 1), (0, -62), (600, 60), 44, ACCENT, key="menu.credits")
    ui.text(6210, "Matn", 6201, "", (0.5, 0.5), (0, 10), (980, 260), 26, INK, key="menu.credits.body", wrap=True)
    c = [ui.button(6211, "Orqaga", 6201, "Orqaga", "menu.back", "Back", (0, -190), select=True)]
    chain_nav(s, c)
    save_scene(s, assets, project)


# ------------------------------------------------------------------------------------------------ graphs


def hero_graph(assets, project, C):
    """The hero: the whole campaign configuration, input and the demo pilot writing the same intent variables, the
    camera-relative motor with a turning body, animation-timed strikes (the Attack clip's Hit event) and casts (the Cast
    clip's Release), interaction, death and respawn, shrine saves, unlocks and automatic claims."""
    g = gk.Graph("Hero")
    WALL = M["WALL_MASK"]
    _vars(g, (("Slot", "String", "Run1"), ("Pilot", "Bool", False), ("Plan", "Number", 0), ("Route", "ArrayVec3", []), ("Steps", "ArrayNumber", []),
              ("Step", "Number", 0), ("Facing", "Vec3", [0, 0, -1]), ("Yaw", "Number", 0), ("Deaths", "Number", 0), ("Defeated", "Bool", False),
              ("Hold", "Number", 0), ("Stuck", "Number", 0), ("Best", "Number", 999), ("MoveIn", "Vec2", [0, 0]), ("Jump", "Bool", False),
              ("JumpHeld", "Bool", True), ("Walk", "Bool", False), ("Attack", "Bool", False), ("Bolt", "Bool", False), ("Slam", "Bool", False),
              ("Potion", "Bool", False), ("Use", "Bool", False), ("Victim", "Entity", 0), ("Lost", "Bool", False), ("Idle", "Bool", False),
              ("Reported", "Bool", False), ("Busy", "Number", 0), ("Layered", "Bool", False), ("Pending", "String", ""), ("MoveSpeed", "Number", 5.4),
              ("Shrines", "Number", 0), ("CheckStep", "Number", 0)))
    h = C["Hero"]
    # ---- configuration
    g.lane("config")
    start = g.n("Event.Start")
    ops = [("Gameplay.HealthConfigure", dict(Maximum=h["Health"], Shield=0, HitGrace=h["HitGrace"], RespawnDelay=h["RespawnDelay"], Team=h["Team"])),
           ("Gameplay.InventoryConfigure", dict(Slots=h["Slots"], Stack=h["Stack"])),
           ("Gameplay.LoadoutConfigure", dict(Stats=h["Stats"]))]
    for item, (slot, mods) in C["Items"].items():
        ops.append(("Gameplay.ItemDefine", dict(Item=item, Slot=slot, Modifiers=mods)))
    chest = [0, 1.1, 0]
    ops += [("Gameplay.EffectDefine", dict(Effect="Burn", Duration=2, Period=0.5, Tick=1, TickAmount=2, Stacking=0, MaxStacks=3)),
            ("Gameplay.EffectDefine", dict(Effect="Shock", Duration=1.0, Stun=True, Stacking=3, MaxStacks=1)),
            ("Gameplay.ProjectileDefine", dict(Projectile="Bolt", Kind=0, Speed=18, Lifetime=1.0, Radius=0.12, TargetRadius=0.55, TargetOffset=chest, Damage=14,
                                               TwoD=False, BlockMask=WALL, Effect="Burn", Prefab="Prefabs/Bolt.zprefab", Pool=8)),
            ("Gameplay.ProjectileDefine", dict(Projectile="Arrow", Kind=0, Speed=16, Lifetime=1.2, Radius=0.08, TargetRadius=0.5, TargetOffset=chest, Damage=8,
                                               TwoD=False, BlockMask=WALL, Prefab="Prefabs/Arrow.zprefab", Pool=8)),
            ("Gameplay.AreaDefine", dict(Area="Zilzila", Kind=0, Amount=20, Radius=3.5, MaxTargets=8, TargetOffset=[0, 0.9, 0], TwoD=False, LineOfSight=True,
                                         Mask=WALL, Effect="Shock")),
            ("Gameplay.AreaDefine", dict(Area="Shockwave", Kind=0, Amount=18, Radius=4.0, MaxTargets=4, TargetOffset=[0, 0.9, 0], TwoD=False, LineOfSight=True,
                                         Mask=WALL, Effect="Shock"))]
    for key, (kind, amount, cost, cool, rng_, ingredient, unlocked, proj, area, effect) in C["Abilities"].items():
        ops.append(("Gameplay.AbilityDefine", dict(Ability=key, Kind=kind, Amount=amount, Cost=cost, Cooldown=cool, Range=rng_, Ingredient=ingredient, Count=1,
                                                   Unlocked=unlocked, Projectile=proj, Area=area, Effect=effect)))
    for key, title, objectives, requires, rewards, xp in C["Quests"]:
        ops.append(("Gameplay.QuestConfigure", dict(Quest=key, Title=title, Objectives=[o[0] for o in objectives], Labels=[o[1] for o in objectives],
                                                    Kinds=[o[2] for o in objectives], Matches=[o[3] for o in objectives], Goals=[o[4] for o in objectives],
                                                    Requires=requires, Rewards=list(rewards), RewardCounts=list(rewards.values()), XP=xp)))
    ops.append(("Gameplay.SaveBind", dict(Key="Hero")))
    # the respawn point (this graph's checkpoint cache): the level start, then every shrine; a resumed save recaptures
    ops.append(("Gameplay.CheckpointCapture", dict(Slot="Hero", Persistent=False)))
    tail = g.steps(start, ops, "SLICE_CONFIG_FAILED hero")
    demo = g.any_of([V(g, "Pilot"), (g.n("App.DemoMode"), "Value"), (g.n("ReadState.Bool", Key=f"{GS}.Run.Demo", Default=False), "Value")])
    t = g.setv(tail, "Pilot", data=demo)
    wp = g.ex(t, "WriteState.Bool", Key=f"{GS}.Pilot", Persistent=False)  # the camera's auto-follow reads it
    g.l(g.get("Pilot"), "Value", wp, "Value")
    g.setv((wp, "Then"), "Lost", data=(g.cmp("Greater", (g.n("ReadState.Number", Key=f"{GS}.Run.Lost"), "Value"), 0), "Value"))

    # ---- 1. human input into the intent variables (Gameplay.Input is fenced while a blocking panel is open)
    g.lane("input")
    upd = g.n("Event.Update")
    raw = g.n("Gameplay.Input")
    t = g.setv((upd, "Then"), "MoveIn", data=(raw, "Move"))
    t = g.setv(t, "Jump", data=(raw, "Jump"))
    t = g.setv(t, "JumpHeld", data=(raw, "JumpHeld"))
    for var, action, held in (("Walk", "Walk", True), ("Attack", "Attack", True), ("Bolt", "Bolt", False), ("Slam", "Slam", False),
                              ("Potion", "Potion", False), ("Use", "Interact", False)):
        pressed = (g.n("ActionPressed" if held else "ActionJustPressed", Name=action), "Value")
        t = g.setv(t, var, data=g.all_of([(raw, "Enabled"), pressed]))
    g.setv(t, "Victim", 0)

    # ---- 2. the demo pilot: the generator's A* route, steering through the camera like a stick; fights what it senses
    g.lane("pilot")
    pu = g.n("Event.Update")
    hp = g.n("Gameplay.Health")
    run = g.br((pu, "Then"), g.all_of([V(g, "Pilot"), (g.not_((hp, "Dead")), "Value"), (g.not_(V(g, "Idle")), "Value")]))
    idx = V(g, "Step")
    count = g.n("Array.Length.Vec3")
    g.l(g.get("Route"), "Value", count, "Array")
    has = g.br(g.setv(g.setv((run, "True"), "JumpHeld", True), "Walk", False), (g.cmp("Less", idx, (count, "Value")), "Value"))
    g.setv((has, "False"), "MoveIn", [0, 0])
    goal = g.n("Array.Get.Vec3")
    g.l(g.get("Route"), "Value", goal, "Array")
    g.l(idx[0], idx[1], goal, "Index")
    code = g.n("Array.Get.Number")
    g.l(g.get("Steps"), "Value", code, "Array")
    g.l(idx[0], idx[1], code, "Index")
    sense = g.n("Gameplay.Sense", Tag="Enemy", Range=9.0, MinDot=-1, TwoD=False, LineOfSight=True, Mask=WALL, Origin=[0, 1.2, 0])
    may = g.any_of([(g.not_((g.cmp("Equal", V(g, "Plan"), 1), "Value")), "Value"), V(g, "Lost")])
    fight = g.br((has, "True"), g.all_of([(sense, "Visible"), may]))
    me = g.n("GetPosition")
    to = g.n("SubtractVec3")
    g.l(sense, "Position", to, "A")
    g.l(me, "Value", to, "B")
    flat = g.n("BreakVec3")
    g.l(to, "Value", flat, "Vector")
    mk = g.n("MakeVec3", Y=0)
    g.l(flat, "X", mk, "X")
    g.l(flat, "Z", mk, "Z")
    face = g.n("NormalizeVec3")
    g.l(mk, "Value", face, "Vector")
    near_ = (sense, "Distance")
    # close in on what it fights (stops 3.2 m short: Bolt range; melee once the enemy comes)
    chase = g.n("Gameplay.Steer", Arrive=3.2, Slow=0.5, TwoD=False, Camera=CAMERA)
    g.l(sense, "Position", chase, "Goal")
    t = g.setv((fight, "True"), "MoveIn", data=(chase, "Move"))
    t = g.setv(t, "Facing", data=(face, "Value"))
    t = g.setv(t, "Victim", data=(sense, "Victim"))
    t = g.setv(t, "Attack", data=(g.cmp("Less", near_, 2.1), "Value"))
    t = g.setv(t, "Bolt", data=(g.cmp("Greater", near_, 2.6), "Value"))
    t = g.setv(t, "Slam", data=(g.cmp("Less", near_, 2.4), "Value"))
    g.setv(t, "Hold", 0)
    # plan 1 before its defeat: it walks up to what it sees without striking (and is struck down there)
    afraid = g.br((fight, "False"), (sense, "Visible"))
    meet = g.n("Gameplay.Steer", Arrive=1.0, Slow=0.5, TwoD=False, Camera=CAMERA)
    g.l(sense, "Position", meet, "Goal")
    g.setv(g.setv((afraid, "True"), "MoveIn", data=(meet, "Move")), "Facing", data=(face, "Value"))
    # route points (codes 0/1) are passed at full pace, turning 0.7 m early; action points are reached precisely
    steer = g.n("Gameplay.Steer", Arrive=0.35, Slow=0.6, TwoD=False, Camera=CAMERA)
    g.l(goal, "Value", steer, "Goal")
    through = g.n("Gameplay.Steer", Arrive=0.7, Slow=0, TwoD=False, Camera=CAMERA)
    g.l(goal, "Value", through, "Goal")
    is_code = lambda v: (g.cmp("Equal", (code, "Value"), v), "Value")
    passing = g.any_of([is_code(0), is_code(1)])
    kind_ = g.br((afraid, "False"), passing)
    t = g.join(g.setv((kind_, "True"), "MoveIn", data=(through, "Move")), g.setv((kind_, "False"), "MoveIn", data=(steer, "Move")))
    t = g.setv(t, "Walk", data=is_code(1))
    better = g.br(t, (g.cmp("Less", (steer, "Distance"), g.num("Subtract", V(g, "Best"), 0.25)), "Value"))
    r = g.setv((better, "True"), "Best", data=(steer, "Distance"))
    r = g.setv(r, "Stuck", 0)
    s_ = g.setv((better, "False"), "Stuck", data=g.num("Add", V(g, "Stuck"), (pu, "Delta")))
    stuck = g.br(s_, g.all_of([(g.cmp("Greater", V(g, "Stuck"), 8), "Value"), (g.not_(V(g, "Reported")), "Value")]))
    rp = g.setv((stuck, "True"), "Reported", True)
    pr = g.ex(rp, "Print")
    g.l(*g.text(["SLICE_BOT_STUCK lvl=1 step=", (g.get("Step"), "Value", "number")]), pr, "Text")
    arrived = g.br(g.join(r, (stuck, "False"), (pr, "Then")), g.any_of([(steer, "Arrived"), g.all_of([passing, (through, "Arrived")])]))

    def step_next(src):
        n1 = g.setv(src, "Step", data=g.num("Add", idx, 1))
        n1 = g.setv(n1, "Best", 999)
        n1 = g.setv(n1, "Stuck", 0)
        g.setv(n1, "Hold", 0)
    c0 = g.br((arrived, "True"), g.any_of([is_code(0), is_code(1)]))
    step_next((c0, "True"))
    c2 = g.br((c0, "False"), is_code(2))
    step_next(g.setv((c2, "True"), "Use", True))
    c3 = g.br((c2, "False"), is_code(3))
    held = g.br(g.setv((c3, "True"), "Hold", data=g.num("Add", V(g, "Hold"), (pu, "Delta"))), (g.cmp("Greater", V(g, "Hold"), 1.2), "Value"))
    step_next((held, "True"))
    c4 = g.br((c3, "False"), is_code(4))
    t = (c4, "True")
    for item in C["Items"]:
        t = (g.ex(t, "Gameplay.EquipmentEquip", Item=item), "Then")
    step_next(t)
    c5 = g.br((c4, "False"), is_code(5))
    step_next(g.setv((c5, "True"), "Jump", True))
    g.setv((c5, "False"), "Stuck", 0)  # 7: finished, waiting for the level flow

    # ---- 3. the pilot drinks when hurt
    g.lane("pilot2")
    ph = g.n("Event.Update")
    hp2 = g.n("Gameplay.Health")
    drinks = g.any_of([(g.not_((g.cmp("Equal", V(g, "Plan"), 1), "Value")), "Value"), V(g, "Lost")])
    low = g.br((ph, "Then"), g.all_of([V(g, "Pilot"), (g.not_((hp2, "Dead")), "Value"), (g.cmp("Less", (hp2, "Current"), 50), "Value"), drinks]))
    g.setv((low, "True"), "Potion", True)

    # ---- 4. the motor (camera-relative), the body turning to its heading
    g.lane("motor")
    au = g.n("Event.Update")
    sp = g.br((au, "Then"), V(g, "Walk"))
    t = g.join(g.setv((sp, "True"), "MoveSpeed", h["Walk"]), g.setv((sp, "False"), "MoveSpeed", h["Run"]))
    motor = g.ex(t, "Gameplay.Motor3D", Acceleration=h["Acceleration"], Deceleration=34, AirControl=0.45, JumpSpeed=h["Jump"], Camera=CAMERA, Sprint=False)
    g.wire(motor, Move=V(g, "MoveIn"), Jump=V(g, "Jump"), JumpHeld=V(g, "JumpHeld"), Speed=V(g, "MoveSpeed"), Delta=(au, "Delta"))
    vel = g.n("BreakVec3")
    g.l(motor, "Velocity", vel, "Vector")
    hv = g.n("MakeVec3", Y=0)
    g.l(vel, "X", hv, "X")
    g.l(vel, "Z", hv, "Z")
    hl = g.n("LengthVec3")
    g.l(hv, "Value", hl, "Vector")
    moving = g.br((motor, "Then"), (g.cmp("Greater", (hl, "Value"), 0.6), "Value"))
    hn = g.n("NormalizeVec3")
    g.l(hv, "Value", hn, "Vector")
    t = g.join(g.setv((moving, "True"), "Facing", data=(hn, "Value")), (moving, "False"))
    fb = g.n("BreakVec3")
    g.l(g.get("Facing"), "Value", fb, "Vector")
    target = g.n("Atan2")
    g.l(*g.num("Negate", (fb, "X")), target, "Y")
    g.l(*g.num("Negate", (fb, "Z")), target, "X")
    lerp = g.n("LerpAngle")
    g.l(g.get("Yaw"), "Value", lerp, "A")
    g.l(target, "Value", lerp, "B")
    g.l(*g.num("Min", g.num("Multiply", (au, "Delta"), 12), 1), lerp, "T")
    t = g.setv(t, "Yaw", data=(lerp, "Value"))
    rot = g.n("MakeVec3", X=0, Z=0)
    g.l(g.get("Yaw"), "Value", rot, "Y")
    sr = g.ex(t, "SetRotation")
    g.l(rot, "Value", sr, "Vector")
    # equipment shows on the body
    e0, e1 = g.n("Gameplay.EquipmentState", Slot=0), g.n("Gameplay.EquipmentState", Slot=1)
    a1 = g.ex((sr, "Then"), "SetActive", Target=SWORD)
    g.l(e0, "Equipped", a1, "Active")
    a2 = g.ex((a1, "Then"), "SetActive", Target=SHIELD)
    g.l(e1, "Equipped", a2, "Active")

    # ---- 5. combat intents: melee and casts start upper-body clips; their Hit/Release events apply them
    g.lane("combat")
    cu = g.n("Event.Update")
    hp3 = g.n("Gameplay.Health")
    free = g.all_of([(g.not_((hp3, "Dead")), "Value"), (g.cmp("Less", V(g, "Busy"), 0.001), "Value")])

    def upper(src, clip, speed, busy, pending, sound):
        t = (g.ex(src, "Animator.PlayLayer", Name="UpperBody", Clip=clip, Speed=speed, Loop=False, Fade=0.08, Restart=True), "Then")
        t = (g.ex(t, "Animator.SetLayerWeight", Name="UpperBody", Weight=1, Fade=0.08), "Then")
        t = g.setv(t, "Busy", busy)
        t = g.setv(t, "Layered", True)
        t = g.setv(t, "Pending", pending)
        return (g.ex(t, "PlaySound", Path=f"Audio/{sound}.wav", Volume=0.6), "Then")
    f1, f2, f3 = g.fork((cu, "Then"))
    ma = g.br(f1, g.all_of([free, V(g, "Attack")]))
    upper((ma, "True"), "Attack", 1.25, 0.6, "Attack", "sfx_swing")
    bolt_ready = (g.n("Gameplay.AbilityState", Ability="Bolt"), "Ready")
    slam_ready = (g.n("Gameplay.AbilityState", Ability="Zilzila"), "Ready")
    mb = g.br((ma, "False"), g.all_of([free, V(g, "Slam"), slam_ready]))
    upper((mb, "True"), "Cast", 1.3, 0.62, "Zilzila", "sfx_ui")
    mc = g.br((mb, "False"), g.all_of([free, V(g, "Bolt"), bolt_ready]))
    upper((mc, "True"), "Cast", 1.5, 0.53, "Bolt", "sfx_ui")
    # the busy timer releases the upper body
    t = g.setv(f2, "Busy", data=g.num("Max", g.num("Subtract", V(g, "Busy"), (cu, "Delta")), 0))
    rel = g.br(t, g.all_of([V(g, "Layered"), (g.cmp("Less", V(g, "Busy"), 0.001), "Value")]))
    g.setv((g.ex((rel, "True"), "Animator.SetLayerWeight", Name="UpperBody", Weight=0, Fade=0.2), "Then"), "Layered", False)
    pb = g.br(f3, V(g, "Potion"))
    pc = g.ex((pb, "True"), "Gameplay.AbilityCast", Ability="Potion", TwoD=False)
    pok = g.br((pc, "Then"), (pc, "Success"))
    g.ex((pok, "True"), "PlaySound", Path="Audio/sfx_levelup.wav", Volume=0.5)
    # clip events: each applies the action that started its clip once (a shrine rest cancels the one in flight)
    ev = g.n("Event.AnimationEvent")
    hit = g.br((ev, "Then"), (g.text_is((ev, "Text"), "Hit"), "Value"))
    swing = g.br((hit, "True"), (g.text_is(V(g, "Pending"), "Attack"), "Value"))
    melee = g.ex(g.setv((swing, "True"), "Pending", ""), "Gameplay.Attack", Range=h["MeleeRange"], MinDot=0.45, TwoD=False, LineOfSight=True, Mask=WALL, Origin=[0, 1.0, 0],
                 Amount=h["Melee"], Cooldown=h["MeleeCooldown"])
    g.l(g.get("Facing"), "Value", melee, "Facing")
    landed = g.br((melee, "Then"), (melee, "Success"))
    vp = g.n("GetPosition")
    g.l(melee, "Victim", vp, "Target")
    spark = g.ex((landed, "True"), "SetPosition", Target=8100)
    lift = g.n("AddVec3", B=[0, 1.1, 0])
    g.l(vp, "Value", lift, "A")
    g.l(lift, "Value", spark, "Vector")
    t = (g.ex((spark, "Then"), "EmitParticles", Target=8100, Count=22), "Then")
    g.ex(t, "PlaySound", Path="Audio/sfx_hit.wav", Volume=0.7)
    release = g.br((hit, "False"), (g.text_is((ev, "Text"), "Release"), "Value"))
    isbolt = g.br((release, "True"), (g.text_is(V(g, "Pending"), "Bolt"), "Value"))
    cast = g.ex(g.setv((isbolt, "True"), "Pending", ""), "Gameplay.AbilityCast", Ability="Bolt", TwoD=False, LineOfSight=True, Mask=WALL)
    g.wire(cast, Victim=V(g, "Victim"), Direction=V(g, "Facing"))
    ok = g.br((cast, "Then"), (cast, "Success"))
    g.ex((ok, "True"), "PlaySound", Path="Audio/sfx_bolt.wav", Volume=0.6)
    isslam = g.br((isbolt, "False"), (g.text_is(V(g, "Pending"), "Zilzila"), "Value"))
    slam = g.ex(g.setv((isslam, "True"), "Pending", ""), "Gameplay.AbilityCast", Ability="Zilzila", TwoD=False, LineOfSight=True, Mask=WALL)
    sok = g.br((slam, "Then"), (slam, "Success"))
    t = (g.ex((sok, "True"), "EmitParticles", Target=8104, Count=120), "Then")
    t = (g.ex(t, "ShakeCamera", Intensity=0.45, Duration=0.45), "Then")
    t = (g.ex(t, "PlaySound", Path="Audio/sfx_nova.wav", Volume=0.8), "Then")
    g.ex(t, "Print", Text="SLICE_TRACE ev=cast a=Zilzila")

    # ---- 6. interaction (pickups automatically, everything else on Interact), prompt, animation parameters
    g.lane("interact")
    late = g.n("Event.LateUpdate")
    find = g.n("Gameplay.InteractionFind", Range=2.6, MinDot=-1, TwoD=False, LineOfSight=True, Mask=WALL, Origin=[0, 1.0, 0])
    pick = g.n("HasTag", Tag="Pickup")
    g.l(find, "Object", pick, "Target")
    auto = g.all_of([(find, "Found"), (pick, "Value"), (g.cmp("Less", (find, "Distance"), 1.4), "Value")])
    want = g.all_of([(find, "Found"), V(g, "Use")])
    go = g.br((late, "Then"), g.any_of([auto, want]))
    inter = g.ex((go, "True"), "Gameplay.Interact", Range=2.6, MinDot=-1, TwoD=False, LineOfSight=True, Mask=WALL, Origin=[0, 1.0, 0])
    g.l(find, "Object", inter, "Target")
    p1, p2, _ = g.fork(g.join((go, "False"), (inter, "Then")))
    show = g.br(p1, g.all_of([(find, "Found"), (g.not_((pick, "Value")), "Value")]))
    prompt = g.ex((show, "True"), "SetUIText", Target=6090)
    g.l(*g.text(["[Interact] ", (find, "Prompt")]), prompt, "Text")
    g.ex((show, "False"), "SetUIText", Target=6090, Text="")
    anim = g.ex(p2, "Gameplay.BindAnimator", TwoD=False, HorizontalParameter="", ForwardParameter="", ReferenceSpeed=h["Run"], Damping=0.1)
    g.l(late, "Delta", anim, "Delta")

    # ---- 7. rule events: deaths (three end the run), shrine saves, collected items, hurt feedback. Polled late, after
    # the interaction lane: a shrine save is written at the end of the frame of the rest (the queue keeps events until
    # polled), the phase in which the level controller restores a save, so a resumed run goes on exactly as the
    # original one.
    g.lane("events")
    evu = g.n("Event.LateUpdate")
    for src in g.fork((evu, "Then")):
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
        # a shrine rest cancels the strike or cast in flight and restores the courier; the save keeps the rested state
        rest = g.setv(g.setv((shrine, "True"), "Pending", ""), "Busy", 0)
        heal = g.ex(rest, "Gameplay.Heal", Amount=999)
        save = g.ex((heal, "Then"), "Gameplay.SaveWrite", Schema=C["Schema"])
        g.l(g.get("Slot"), "Value", save, "Slot")
        saved = g.br((save, "Then"), (save, "Success"))
        fail = g.ex((saved, "False"), "Print")
        g.l(*g.text(["SLICE_IO_FAILED save ", (save, "Reason")]), fail, "Text")
        cap = g.ex((saved, "True"), "Gameplay.CheckpointCapture", Slot="Hero", Persistent=False)
        cs = g.setv((cap, "Then"), "CheckStep", data=V(g, "Step"))
        ws = g.ex(cs, "WriteState.Number", Key=f"{GS}.Run.Save", Value=1)
        wst = g.ex((ws, "Then"), "WriteState.Number", Key=f"{GS}.Run.Step")
        g.l(g.get("Step"), "Value", wst, "Value")
        msg = g.ex((wst, "Then"), "SendMessage", Target=GAME, Name="Saved", Text="shrine")
        t = g.setv((msg, "Then"), "Shrines", data=g.num("Add", V(g, "Shrines"), 1))
        stop = g.br(t, (g.cmp("Equal", V(g, "Plan"), 3), "Value"))
        g.setv((stop, "True"), "Idle", True)
        col = g.br((used, "False"), (g.text_is(kind, "Collected"), "Value"))
        nxt = (col, "True")
        for item, sound, banner in (("FireStone", "sfx_lamp", "Olov toshi topildi!"), ("Shamshir", "sfx_chest", "Shamshir! Endi zarbangiz kuchliroq"),
                                    ("Qalqon", "sfx_chest", "Qalqon topildi!"), ("Potion", "sfx_lamp", "Ichimlik topildi")):
            ib = g.br(nxt, (g.text_is((poll, "Item"), item), "Value"))
            t = (g.ex((ib, "True"), "PlaySound", Path=f"Audio/{sound}.wav", Volume=0.7), "Then")
            t = (g.ex(t, "Print", Text=f"SLICE_TRACE ev=collect a={item}"), "Then")
            g.ex(t, "SendMessage", Target=GAME, Name="Banner", Text=banner)
            nxt = (ib, "False")
        hurt = g.br((col, "False"), (g.text_is(kind, "Damage"), "Value"))
        t = (g.ex((hurt, "True"), "Animator.PlayLayer", Name="Hit", Clip="Hurt", Loop=False, Fade=0.04, Restart=True), "Then")
        g.ex(t, "SendMessage", Target=GAME, Name="Hurt", Text="")

    # ---- 8. respawn at the last checkpoint until defeated; the death state
    g.lane("respawn")
    rs = g.n("Event.Update")
    hp4 = g.n("Gameplay.Health")
    r1, r2, _ = g.fork((rs, "Then"))
    due = g.br(r1, g.all_of([(hp4, "Dead"), (g.not_(V(g, "Defeated")), "Value"), (g.cmp("Less", (hp4, "RespawnIn"), 0.001), "Value")]))
    resp = g.ex((due, "True"), "Gameplay.Respawn", Slot="Hero", Persistent=False, Grace=1.5)
    rok = g.br((resp, "Then"), (resp, "Success"))
    rf = g.ex((rok, "False"), "Print")
    g.l(*g.text(["SLICE_IO_FAILED respawn ", (resp, "Reason")]), rf, "Text")
    t = (g.ex((rok, "True"), "Animator.SetBool", Name="Dead", Value=False), "Then")
    # the pilot takes up its route again from the checkpoint's step
    t = g.setv(t, "Step", data=V(g, "CheckStep"))
    t = g.setv(g.setv(g.setv(t, "Best", 999), "Stuck", 0), "Hold", 0)
    g.setv(t, "Reported", False)
    dead = g.br(r2, (hp4, "Dead"))
    g.ex((dead, "True"), "Animator.SetBool", Name="Dead", Value=True)

    # ---- 9. progression: Zilzila at XP level 2, automatic claims
    g.lane("progress")
    pg = g.n("Event.Update")
    lvl = g.n("Gameplay.QuestProgression")
    st = g.n("Gameplay.AbilityState", Ability="Zilzila")
    b = g.br((pg, "Then"), g.all_of([(g.cmp("Greater", (lvl, "Level"), 1.5), "Value"), (g.not_((st, "Unlocked")), "Value")]))
    u = g.ex((b, "True"), "Gameplay.AbilityUnlock", Ability="Zilzila")
    m = g.ex((u, "Then"), "SendMessage", Target=GAME, Name="LevelUp", Text="Yangi daraja! Zilzila ochildi [Slam]")
    src = g.join((b, "False"), (m, "Then"))
    for key, *_ in C["Quests"]:
        qs = g.n("Gameplay.QuestState", Quest=key)
        b = g.br(src, (qs, "Ready"))
        cl = g.ex((b, "True"), "Gameplay.QuestClaim", Quest=key)
        m = g.ex((cl, "Then"), "SendMessage", Target=GAME, Name="Claimed", Text=key)
        src = g.join((b, "False"), (m, "Then"))

    # ---- 10. messages: a resumed save restores the pilot's route position and the body's heading as saved (the
    # restored rotation; otherwise the turn lerp would start from yaw 0)
    g.lane("messages")
    msg = g.n("Event.Message")
    b = g.br((msg, "Then"), (g.text_is((msg, "Text"), "Resumed"), "Value"))
    t = g.setv(g.setv((b, "True"), "Step", data=(g.n("ReadState.Number", Key=f"{GS}.Run.Step"), "Value")), "CheckStep", data=V(g, "Step"))
    rb = g.n("BreakVec3")
    g.l(g.n("GetRotation"), "Value", rb, "Vector")
    t = g.setv(t, "Yaw", data=(rb, "Y"))
    heading = g.n("MakeVec3", Y=0)
    g.l(*g.num("Negate", g.num("Sin", (rb, "Y"))), heading, "X")
    g.l(*g.num("Negate", g.num("Cos", (rb, "Y"))), heading, "Z")
    t = g.setv(t, "Facing", data=(heading, "Value"))
    g.ex(t, "Gameplay.CheckpointCapture", Slot="Hero", Persistent=False)
    g.save(assets, "Scripts/Hero.zgraph", project)
    return g


def camera_graph(assets, project):
    """The orbit camera: mouse / right stick look, a sphere cast keeps it out of walls; the demo pilot's camera turns
    slowly behind the courier's heading; after the beacon is lit it pulls back for the reveal."""
    g = gk.Graph("Camera3D")
    _vars(g, (("Offset", "Vec3", [0, 2.4, 5.6]),))
    late = g.n("Event.LateUpdate")
    raw = g.n("Gameplay.Input", Target=HERO)
    stick = g.n("MakeVec2")
    g.l(*g.num("Multiply", (g.n("Axis", Name="LookX"), "Value"), 14), stick, "X")
    g.l(*g.num("Multiply", (g.n("Axis", Name="LookY"), "Value"), 10), stick, "Y")
    look = g.n("AddVec2")
    g.l(raw, "Look", look, "A")
    g.l(stick, "Value", look, "B")
    # the pilot's follow: turn the orbit towards behind the velocity (a stick-sized look input)
    motion = g.n("Gameplay.Motion", Target=HERO, TwoD=False)
    v = g.n("BreakVec3")
    g.l(motion, "Velocity", v, "Vector")
    want = g.n("Atan2")
    g.l(*g.num("Negate", (v, "X")), want, "Y")
    g.l(*g.num("Negate", (v, "Z")), want, "X")
    cam, hero = g.n("GetPosition"), g.n("GetPosition", Target=HERO)
    d = g.n("SubtractVec3")
    g.l(cam, "Value", d, "A")
    g.l(hero, "Value", d, "B")
    db = g.n("BreakVec3")
    g.l(d, "Value", db, "Vector")
    now = g.n("Atan2")
    g.l(db, "X", now, "Y")
    g.l(db, "Z", now, "X")
    turn = g.n("LerpAngle")
    g.l(now, "Value", turn, "A")
    g.l(want, "Value", turn, "B")
    g.l(*g.num("Min", g.num("Multiply", (late, "Delta"), 1.8), 1), turn, "T")
    step = g.num("Subtract", (turn, "Value"), (now, "Value"))
    auto = g.n("MakeVec2", Y=0)
    g.l(*g.num("Divide", g.num("Negate", step), 0.003), auto, "X")
    follow = g.all_of([(g.n("ReadState.Bool", Key=f"{GS}.Pilot", Default=False), "Value"), (g.cmp("Greater", (motion, "Speed"), 1.0), "Value")])
    lit = g.n("Gameplay.QuestState", Target=HERO, Quest="Beacon")
    b = g.br((late, "Then"), (lit, "Claimed"))
    t = g.join(g.setv((b, "True"), "Offset", [0, 6.5, 13.0]), (b, "False"))
    fb = g.br(t, follow)
    cams = []
    for out, extra in ((("True"), True), (("False"), False)):
        c = g.ex((fb, out), "Gameplay.Camera", Anchor=HERO, Eye=[0, 1.5, 0], Orbit=True, Damping=0.1, Sensitivity=0.003, SnapDistance=5, Radius=0.25,
                 Mask=M["VIEW_MASK"])
        g.wire(c, Offset=V(g, "Offset"), Delta=(late, "Delta"))
        if extra:
            sum_ = g.n("AddVec2")
            g.l(look, "Value", sum_, "A")
            g.l(auto, "Value", sum_, "B")
            g.l(sum_, "Value", c, "Look")
        else:
            g.l(look, "Value", c, "Look")
        cams.append(c)
    g.save(assets, "Scripts/Camera3D.zgraph", project)
    return g


QUEST_ROWS = [("Pass", "Guards"), ("Pass", "Gate"), ("Stones", "Stone"), ("Warden", "Warden"), ("Beacon", "Beacon")]


def game_graph(assets, project, C):
    """The level controller: panels and HUD, interactables, entry (new / continue), the campaign's quest starts, the
    Warden's music and bar, the beacon finale and victory, defeat, messages, run time."""
    g = gk.Graph("Game")
    _vars(g, (("Slot", "String", "Run1"), ("Self", "String", ""), ("Demo", "Bool", False), ("PickupIds", "ArrayEntity", []), ("PickupKeys", "ArrayString", []),
              ("PickupItems", "ArrayString", []), ("UseIds", "ArrayEntity", []), ("UseKeys", "ArrayString", []), ("UsePrompts", "ArrayString", []),
              ("UseRequired", "ArrayString", []), ("UseCounts", "ArrayNumber", []), ("UseConsume", "ArrayBool", []), ("UseRewards", "ArrayString", []),
              ("UseMaxUses", "ArrayNumber", []), ("UseCooldown", "ArrayNumber", []), ("UseDeactivate", "ArrayBool", []), ("Boss", "Entity", 0),
              ("Loaded", "Bool", False), ("Time", "Number", 0), ("Ended", "Bool", False), ("Banner", "Number", 0), ("Hurt", "Number", 0),
              ("TitleTime", "Number", 3.5), ("BossSeen", "Bool", False), ("BossDown", "Bool", False), ("Enraged", "Bool", False), ("Auto", "Number", -1),
              ("Entry", "String", "new"), ("Lit", "Bool", False)))
    schema = C["Schema"]
    # ---- configuration
    g.lane("config")
    start = g.n("Event.Start")
    ops = [("Gameplay.UIConfigure", dict(Target=6100, Name="Pause", Action="Pause", Focus=6111, Pause=True, Block=True, Back=True)),
           ("Gameplay.UIConfigure", dict(Target=6200, Name="Bag", Action="Inventory", Focus=6211, Pause=False, Block=True, Back=True)),
           ("Gameplay.UIConfigure", dict(Target=6300, Name="Defeat", Action="", Focus=6311, Pause=True, Block=True, Back=False)),
           ("Gameplay.UIConfigure", dict(Target=6400, Name="Victory", Action="", Focus=6411, Pause=True, Block=True, Back=False)),
           ("Gameplay.UIBindHUD", dict(Target=6012, Actor=HERO, Kind=9, Item="Coin", Prefix="", Ratio=False)),
           ("Gameplay.UIBindHUD", dict(Target=6013, Actor=HERO, Kind=9, Item="Coin", Prefix="", Ratio=False)),
           ("Gameplay.UIBindHUD", dict(Target=6014, Actor=HERO, Kind=0, Item="Coin", Prefix="", Ratio=True)),
           ("Gameplay.UIBindHUD", dict(Target=6015, Actor=HERO, Kind=7, Item="Coin", Prefix="", Ratio=True)),
           ("Gameplay.UIBindHUD", dict(Target=6022, Actor=HERO, Kind=2, Item="FireStone", Prefix="", Ratio=False)),
           ("Gameplay.UIBindHUD", dict(Target=6024, Actor=HERO, Kind=2, Item="Potion", Prefix="", Ratio=False))]
    for i, ability in enumerate(("Bolt", "Zilzila", "Potion")):
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
    # usable objects: chests, the gate, shrines, the beacon
    fu = g.ex((fe, "Completed"), "ForEach.Entity")
    g.l(g.get("UseIds"), "Value", fu, "Array")
    pins = {}
    for pin, var, kind in (("Prompt", "UsePrompts", "String"), ("RequiredItem", "UseRequired", "String"), ("RequiredCount", "UseCounts", "Number"),
                           ("Consume", "UseConsume", "Bool"), ("RewardItem", "UseRewards", "String"), ("MaxUses", "UseMaxUses", "Number"),
                           ("Cooldown", "UseCooldown", "Number"), ("Deactivate", "UseDeactivate", "Bool")):
        getter = g.n(f"Array.Get.{kind}")
        g.l(g.get(var), "Value", getter, "Array")
        g.l(fu, "Index", getter, "Index")
        pins[pin] = (getter, "Value")
    uc = g.ex((fu, "Body"), "Gameplay.InteractionConfigure", RewardCount=1, Heal=0, Enabled=True, Offset=[0, 0, 0])
    g.wire(uc, Target=(fu, "Value"), **pins)
    ukey = g.n("Array.Get.String")
    g.l(g.get("UseKeys"), "Value", ukey, "Array")
    g.l(fu, "Index", ukey, "Index")
    usb = g.ex((uc, "Then"), "Gameplay.SaveBind")
    g.wire(usb, Target=(fu, "Value"), Key=(ukey, "Value"))
    oku = g.br((usb, "Then"), g.all_of([(uc, "Success"), (usb, "Success")]))
    badu = g.ex((oku, "False"), "Print")
    g.l(*g.text(["SLICE_CONFIG_FAILED use ", (ukey, "Value"), " ", (uc, "Reason"), (usb, "Reason")]), badu, "Text")
    t = (fu, "Completed")
    for group, setting, default in (("Music", "Music", 0.55), ("SFX", "Sfx", 0.9)):
        vol = g.ex(t, "Audio.SetGroupVolume", Group=group)
        g.l(g.n("ReadState.Number", Key=f"{GS}.Settings.{setting}", Default=default), "Value", vol, "Volume")
        t = (vol, "Then")
    # the menu's UI scale setting (a level started on its own - Editor Play - too)
    us = g.ex(t, "Display.SetUIScale")
    g.l(g.n("ReadState.Number", Key=f"{GS}.Settings.UIScale", Default=1.0), "Value", us, "Scale")
    t = (us, "Then")

    # ---- entry (first LateUpdate, after every Start): continue from the save, or a fresh start
    g.lane("entry")
    lu = g.n("Event.LateUpdate")
    first = g.br((lu, "Then"), (g.not_(V(g, "Loaded")), "Value"))
    t = g.setv((first, "True"), "Loaded", True)
    entry = g.n("ReadState.String", Key=f"{GS}.Run.Entry", Default="new")
    t = g.setv(t, "Entry", data=(entry, "Value"))
    t = g.setv(t, "Time", data=(g.n("ReadState.Number", Key=f"{GS}.Run.Time", Default=0), "Value"))
    saved = g.cmp("Equal", (g.n("ReadState.Number", Key=f"{GS}.Run.Save"), "Value"), 1)
    cont = g.br(t, g.all_of([(g.text_is(V(g, "Entry"), "continue"), "Value"), (saved, "Value")]))
    load = g.ex((cont, "True"), "Gameplay.SaveLoad", Schema=schema)
    g.l(g.get("Slot"), "Value", load, "Slot")
    loaded = g.br((load, "Then"), (load, "Success"))
    rec = g.br((loaded, "True"), (load, "Recovered"))
    r1 = g.ex((rec, "True"), "Print", Text="SLICE_RESUMED lvl=1 source=save recovered=1")
    r1b = g.ex((r1, "Then"), "SendMessage", Target=GAME, Name="Banner", Text="Zaxira nusxadan tiklandi")
    r0 = g.ex((rec, "False"), "Print", Text="SLICE_RESUMED lvl=1 source=save recovered=0")
    resumed = g.ex(g.join((r1b, "Then"), (r0, "Then")), "SendMessage", Target=HERO, Name="Resumed", Text="")
    lf = g.ex((loaded, "False"), "Print")
    g.l(*g.text(["SLICE_IO_FAILED load ", (load, "Reason")]), lf, "Text")
    # an unreadable save (primary and backup) is said on screen: the level starts again from the camp
    lfb = g.ex((lf, "Then"), "SendMessage", Target=GAME, Name="Banner", Text=UNREADABLE)
    fresh = g.setv(g.join((cont, "False"), (lfb, "Then")), "Time", 0)
    auto = g.ex(fresh, "Gameplay.SaveWrite", Schema=schema)
    g.l(g.get("Slot"), "Value", auto, "Slot")
    autook = g.br((auto, "Then"), (auto, "Success"))
    ws = g.ex((autook, "True"), "WriteState.Number", Key=f"{GS}.Run.Save", Value=1)
    pa = g.ex((ws, "Then"), "Print", Text="SLICE_SAVED lvl=1 at=auto")
    af = g.ex((autook, "False"), "Print")
    g.l(*g.text(["SLICE_IO_FAILED autosave ", (auto, "Reason")]), af, "Text")
    common = g.join((resumed, "Then"), (pa, "Then"), (af, "Then"))
    t = (g.ex(common, "WriteState.String", Key=f"{GS}.Run.Entry", Value="continue"), "Then")
    t = (g.ex(t, "WriteState.Number", Key=f"{GS}.Run.Level", Value=1), "Then")
    for quest in ("Pass", "Stones"):
        mode = g.n("Gameplay.QuestState", Target=HERO, Quest=quest)
        b = g.br(t, (g.text_is((mode, "Mode"), "Locked"), "Value"))
        qs = g.ex((b, "True"), "Gameplay.QuestStart", Target=HERO, Quest=quest)
        t = g.join((b, "False"), (qs, "Then"))
    st = g.ex(t, "Print")
    g.l(*g.text(["SLICE_LEVEL_START lvl=1 entry=", V(g, "Entry")]), st, "Text")
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

    # ---- campaign: the Warden quest after the pass, the beacon quest after the stones and the Warden, the finale
    ex1, ex2, ex3 = g.fork(f2)
    qpass, qstones = g.n("Gameplay.QuestState", Target=HERO, Quest="Pass"), g.n("Gameplay.QuestState", Target=HERO, Quest="Stones")
    qward, qbeacon = g.n("Gameplay.QuestState", Target=HERO, Quest="Warden"), g.n("Gameplay.QuestState", Target=HERO, Quest="Beacon")
    ws_ = g.br(ex1, g.all_of([V(g, "Loaded"), (g.text_is((qward, "Mode"), "Locked"), "Value"), (qpass, "Claimed")]))
    t = (g.ex((ws_, "True"), "Gameplay.QuestStart", Target=HERO, Quest="Warden"), "Then")
    g.ex(t, "SendMessage", Target=GAME, Name="Banner", Text="Darvoza ochiq! Qora qorovul minorada kutmoqda")
    bs = g.br(ex2, g.all_of([V(g, "Loaded"), (g.text_is((qbeacon, "Mode"), "Locked"), "Value"), (qstones, "Claimed"), (qward, "Claimed")]))
    t = (g.ex((bs, "True"), "Gameplay.QuestStart", Target=HERO, Quest="Beacon"), "Then")
    g.ex(t, "SendMessage", Target=GAME, Name="Banner", Text="Uch olov toshi! Mash'alni yoqing")
    win = g.br(ex3, g.all_of([(g.not_(V(g, "Ended")), "Value"), (qward, "Claimed"), (qbeacon, "Claimed")]))
    t = g.setv((win, "True"), "Ended", True)
    for target in (8102, 8103, 8106):
        t = (g.ex(t, "SetActive", Target=target, Active=True), "Then")
    t = (g.ex(t, "PlaySound", Path="Audio/sfx_beacon.wav", Volume=1.0), "Then")
    t = (g.ex(t, "ShakeCamera", Intensity=0.35, Duration=1.2, Frequency=12), "Then")
    t = (g.ex(t, "Print", Text="SLICE_TRACE ev=beacon a=lit"), "Then")
    t = (g.ex(t, "WriteState.Number", Key=f"{GS}.Run.Finished", Value=1), "Then")
    best = g.n("ReadState.Number", Key=f"{GS}.Best.Time", Default=0)
    record = g.br(t, g.any_of([(g.cmp("Less", (best, "Value"), 0.001), "Value"), (g.cmp("Less", V(g, "Time"), (best, "Value")), "Value")]))
    wb = g.ex((record, "True"), "WriteState.Number", Key=f"{GS}.Best.Time")
    g.l(g.get("Time"), "Value", wb, "Value")
    t = (g.ex(g.join((wb, "Then"), (record, "False")), "SaveGameState"), "Then")
    prog = g.n("Gameplay.QuestProgression", Target=HERO)
    hp = g.n("Gameplay.Health", Target=HERO)
    stones = g.n("Gameplay.InventoryCount", Target=HERO, Item="FireStone")
    allq = g.all_of([(g.n("Gameplay.QuestState", Target=HERO, Quest=q[0]), "Claimed") for q in C["Quests"]])
    pv = g.ex(t, "Print")
    g.l(*g.text(["SLICE_VICTORY game=Dovon time=", (g.get("Time"), "Value", "number", 1), " xp=", (prog, "XP", "number"), " level=", (prog, "Level", "number"),
                 " stones=", (stones, "Count", "number"), " deaths=", (hp, "Deaths", "number"), " quests=", (g.n("Constant.String", Value="4/4"), "Value")]), pv, "Text")
    qb = g.br((pv, "Then"), allq)
    g.ex((qb, "False"), "Print", Text="SLICE_ORACLE_FAILED quests")
    t = (g.ex((qb, "True"), "FadeAudio", Target=MUSIC, Volume=0.25, Seconds=1.0), "Then")
    t = (g.ex(t, "Delay", Seconds=2.5), "Then")
    t = (g.ex(t, "Gameplay.UIOpen", Name="Victory"), "Then")
    t = (g.ex(t, "PlaySound", Path="Audio/sfx_victory.wav", Volume=0.9), "Then")
    stats = g.ex(t, "SetUIText", Target=6410)
    best2 = g.n("ReadState.Number", Key=f"{GS}.Best.Time", Default=0)
    g.l(*g.text(["Vaqt  ", (g.get("Time"), "Value", "time"), "\nO'limlar  ", (hp, "Deaths", "number"), "\nTajriba  ", (prog, "XP", "number"), "  (",
                 (prog, "Level", "number"), "-daraja)\nEng yaxshi vaqt  ", (best2, "Value", "time")]), stats, "Text")
    t = (g.ex((stats, "Then"), "Print", Text="Menyu: G'alaba"), "Then")
    attract = g.br(t, g.any_of([V(g, "Demo"), (g.n("App.DemoMode"), "Value")]))
    g.setv((attract, "True"), "Auto", 5.0)

    # ---- the Warden: music and bar when it engages, enrage below 40 %, defeat
    g.lane("boss")
    bu = g.n("Event.Update")
    on = g.br((bu, "Then"), V(g, "Loaded"))
    b1, b2, b3 = g.fork((on, "True"))
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
    low = g.num("Multiply", (bhp, "Maximum"), 0.4)
    rage = g.br(b2, g.all_of([(g.not_(V(g, "Enraged")), "Value"), (g.not_((bhp, "Dead")), "Value"), (g.cmp("Less", (bhp, "Current"), low), "Value")]))
    t = g.setv((rage, "True"), "Enraged", True)
    t = (g.ex(t, "SendMessage", Target=GAME, Name="Banner", Text="Qora qorovul g'azablandi!"), "Then")
    t = (g.ex(t, "ShakeCamera", Intensity=0.5, Duration=0.6), "Then")
    g.ex(t, "Print", Text="SLICE_TRACE ev=boss a=enraged")
    down = g.br(b3, g.all_of([V(g, "BossSeen"), (g.not_(V(g, "BossDown")), "Value"), (bhp, "Dead")]))
    t = g.setv((down, "True"), "BossDown", True)
    t = (g.ex(t, "SetActive", Target=6093, Active=False), "Then")
    t = (g.ex(t, "SetActive", Target=6094, Active=False), "Then")
    t = (g.ex(t, "FadeAudio", Target=BOSS_MUSIC, Volume=0.0, Seconds=2.0), "Then")
    t = (g.ex(t, "FadeAudio", Target=MUSIC, Volume=0.75, Seconds=2.0), "Then")
    t = (g.ex(t, "SendMessage", Target=GAME, Name="Banner", Text="Qora qorovul yengildi!"), "Then")
    g.ex(t, "Print", Text="SLICE_TRACE ev=boss a=defeated")

    # ---- demo: automatic choices on end screens (the pilot never clicks)
    g.lane("demo")
    du = g.n("Event.Update")
    ok_ = g.br((du, "Then"), (g.cmp("Greater", V(g, "Auto"), 0), "Value"))
    t = g.setv((ok_, "True"), "Auto", data=g.num("Subtract", V(g, "Auto"), (g.n("Gameplay.UIState"), "Delta")))
    due = g.br(t, (g.cmp("Less", V(g, "Auto"), 0.0001), "Value"))
    t = g.setv((due, "True"), "Auto", -1)
    which = g.br(t, V(g, "Ended"))
    g.ex((which, "True"), "EmitEvent", Name="Menu")
    g.ex((which, "False"), "EmitEvent", Name="Retry")

    # ---- messages (buttons and graphs)
    g.lane("messages")
    m = g.n("Event.Message")
    nxt = (m, "Then")
    for name in ("Resume", "Bag", "CloseBag", "NextPage", "Equip", "Drink", "SaveQuit", "Menu", "Retry", "Defeat", "Saved", "Banner", "LevelUp", "Hurt", "Claimed"):
        b = g.br(nxt, (g.text_is((m, "Text"), name), "Value"))
        e = g.ex((b, "True"), "EmitEvent", Name=name, Text="")
        g.l(m, "Payload", e, "Text")
        nxt = (b, "False")

    def on(name):
        return g.n("Event.Custom", Name=name)
    g.ex((on("Resume"), "Then"), "Gameplay.UIBack")
    g.ex((g.ex((on("Bag"), "Then"), "Gameplay.UIOpen", Name="Bag"), "Then"), "Print", Text="Menyu: Inventar")
    g.ex((on("CloseBag"), "Then"), "Gameplay.UIClose", Name="Bag")
    g.ex((on("NextPage"), "Then"), "Gameplay.UIInventoryPage", Name="Bag", Delta=1)
    inv = g.n("Gameplay.UIInventoryState", Name="Bag")
    eq = g.ex((on("Equip"), "Then"), "Gameplay.EquipmentEquip", Target=HERO)
    g.l(inv, "Item", eq, "Item")
    eqb = g.br((eq, "Then"), (eq, "Success"))
    msg = g.ex((eqb, "True"), "SendMessage", Target=GAME, Name="Banner")
    g.l(*g.text(["Kiyildi: ", (inv, "Item")]), msg, "Text")
    g.ex((eqb, "False"), "SendMessage", Target=GAME, Name="Banner", Text="Bu buyumni kiyib bo'lmaydi")
    g.ex((on("Drink"), "Then"), "Gameplay.AbilityCast", Target=HERO, Ability="Potion", TwoD=False)
    sq = g.ex((on("SaveQuit"), "Then"), "Gameplay.SaveWrite", Schema=schema)
    g.l(g.get("Slot"), "Value", sq, "Slot")
    sqb = g.br((sq, "Then"), (sq, "Success"))
    ws = g.ex((sqb, "True"), "WriteState.Number", Key=f"{GS}.Run.Save", Value=1)
    wt = g.ex((ws, "Then"), "WriteState.Number", Key=f"{GS}.Run.Time")
    g.l(g.get("Time"), "Value", wt, "Value")
    t = (g.ex((wt, "Then"), "SaveGameState"), "Then")
    t = (g.ex(t, "Print", Text="SLICE_SAVED lvl=1 at=quit"), "Then")
    g.ex(t, "LoadScene", Path="Scenes/Menu.zscene", FadeOut=0.5)
    sqf = g.ex((sqb, "False"), "Print")
    g.l(*g.text(["SLICE_IO_FAILED quit ", (sq, "Reason")]), sqf, "Text")
    g.ex((on("Menu"), "Then"), "LoadScene", Path="Scenes/Menu.zscene", FadeOut=0.5)
    t = (g.ex((on("Retry"), "Then"), "WriteState.String", Key=f"{GS}.Run.Entry", Value="continue"), "Then")
    t = (g.ex(t, "SaveGameState"), "Then")
    t = (g.ex(t, "Print", Text="Menyu: Oxirgi saqlashdan"), "Then")
    re = g.ex(t, "LoadScene", FadeOut=0.5)
    g.l(g.get("Self"), "Value", re, "Path")
    t = (g.ex((on("Defeat"), "Then"), "Gameplay.UIOpen", Name="Defeat"), "Then")
    t = (g.ex(t, "Print", Text="SLICE_DEFEAT lvl=1 deaths=3"), "Then")
    t = (g.ex(t, "PlaySound", Path="Audio/sfx_defeat.wav", Volume=0.9), "Then")
    t = (g.ex(t, "WriteState.Number", Key=f"{GS}.Run.Lost", Value=1), "Then")
    t = (g.ex(t, "SaveGameState"), "Then")
    demo = g.br(t, g.any_of([V(g, "Demo"), (g.n("App.DemoMode"), "Value"), (g.n("ReadState.Bool", Key=f"{GS}.Run.Demo", Default=False), "Value")]))
    g.setv((demo, "True"), "Auto", 2.5)
    sv = on("Saved")
    wt = g.ex((sv, "Then"), "WriteState.Number", Key=f"{GS}.Run.Time")
    g.l(g.get("Time"), "Value", wt, "Value")
    t = (g.ex((wt, "Then"), "SaveGameState"), "Then")
    t = (g.ex(t, "Print", Text="SLICE_SAVED lvl=1 at=shrine"), "Then")
    t = (g.ex(t, "SetUIText", Target=6091, Text="Saqlandi"), "Then")
    g.setv(t, "Banner", 2.5)
    bn = on("Banner")
    st = g.ex((bn, "Then"), "SetUIText", Target=6091)
    g.l(bn, "Text", st, "Text")
    g.setv((st, "Then"), "Banner", 2.5)
    lv = on("LevelUp")
    st = g.ex((lv, "Then"), "SetUIText", Target=6091)
    g.l(lv, "Text", st, "Text")
    t = g.setv((st, "Then"), "Banner", 3.5)
    t = (g.ex(t, "EmitParticles", Target=8101, Count=90), "Then")
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
    """Every enemy (instance overrides choose the archetype): health/armour/AI configuration with delegated attacks - the
    graph plays the upper-body Attack/Cast clip and the clip's Hit/Release event strikes or shoots - locomotion from
    the real velocity, hit feedback, the Warden's telegraphed shockwave."""
    g = gk.Graph("Enemy")
    WALL = M["WALL_MASK"]
    _vars(g, (("Key", "String", "Enemy"), ("Kind", "String", "guard"), ("HP", "Number", 30), ("Armor", "Number", 0), ("Damage", "Number", 5),
              ("Cooldown", "Number", 1.5), ("Speed", "Number", 2), ("View", "Number", 8), ("Lose", "Number", 10), ("FOV", "Number", 140),
              ("Leash", "Number", 12), ("Enter", "Number", 1.7), ("Exit", "Number", 2.2), ("Ranged", "Bool", False), ("Ability", "String", ""),
              ("Reach", "Number", 2.3), ("Patrol", "ArrayVec3", []), ("Boss", "Bool", False), ("Ring", "Entity", 0), ("DeadTime", "Number", 0),
              ("Swing", "Number", 0), ("Wave", "Number", 0), ("Layered", "Bool", False)))
    g.lane("config")
    start = g.n("Event.Start")
    stats = g.n("Array.Set.Number", Array=[0, 0, 0, 1, 0], Index=1)
    g.l(g.get("Armor"), "Value", stats, "Value")
    ops = [("Gameplay.HealthConfigure", dict(Shield=0, HitGrace=0, RespawnDelay=60, Team=2), {"Maximum": V(g, "HP")}),
           ("Gameplay.LoadoutConfigure", dict(), {"Stats": (stats, "Value")}),
           ("Gameplay.AIConfigure", dict(TwoD=False, Tag="Hero", Victim=0, Relative=True, PingPong=True, Near=1.5, Memory=3, Wait=0.8, Arrive=0.4, Repath=0.4,
                                         Stall=0.75, RecoveryCooldown=0.5, Retries=3, Eye=[0, 1.5, 0], Facing=[0, 0, 1], Mask=WALL, Enabled=True, Strike=False),
            {"Points": V(g, "Patrol"), "View": V(g, "View"), "Lose": V(g, "Lose"), "FieldOfView": V(g, "FOV"), "Leash": V(g, "Leash"),
             "AttackEnter": V(g, "Enter"), "AttackExit": V(g, "Exit"), "Damage": V(g, "Damage"), "AttackCooldown": V(g, "Cooldown"), "Speed": V(g, "Speed")}),
           ("Gameplay.SaveBind", dict(), {"Key": V(g, "Key")})]
    tail = g.steps(start, ops, "SLICE_CONFIG_FAILED enemy")
    ranged = g.br(tail, V(g, "Ranged"))
    ad = g.ex((ranged, "True"), "Gameplay.AbilityDefine", Kind=0, Cost=0, Range=16, Count=1, Unlocked=True)
    g.wire(ad, Ability=V(g, "Ability"), Amount=V(g, "Damage"), Cooldown=V(g, "Cooldown"), Projectile=V(g, "Ability"))
    adb = g.br((ad, "Then"), (ad, "Success"))
    bad = g.ex((adb, "False"), "Print")
    g.l(*g.text(["SLICE_CONFIG_FAILED ability ", (ad, "Reason")]), bad, "Text")
    # ---- per frame
    g.lane("update")
    u = g.n("Event.Update")
    hp = g.n("Gameplay.Health")
    f1, f2, f3 = g.fork((u, "Then"))
    dead = g.br(f1, (hp, "Dead"))
    t = (g.ex((dead, "True"), "Animator.SetBool", Name="Dead", Value=True), "Then")
    t = g.setv(t, "DeadTime", data=g.num("Add", V(g, "DeadTime"), (u, "Delta")))
    gone = g.br(t, (g.cmp("Greater", V(g, "DeadTime"), 4.0), "Value"))
    g.ex((gone, "True"), "SetActive", Active=False)
    ba = g.ex((dead, "False"), "Gameplay.BindAnimator", TwoD=False, HorizontalParameter="", ForwardParameter="", Damping=0.12)
    g.wire(ba, ReferenceSpeed=V(g, "Speed"), Delta=(u, "Delta"))
    # rule events: hit feedback and the kill trace (a restored death never replays: a restore clears the events)
    for src in g.fork(f2)[:2]:
        poll = g.ex(src, "Gameplay.RuleEvent")
        has_ = g.br((poll, "Then"), (poll, "Has"))
        died = g.br((has_, "True"), (g.text_is((poll, "Kind"), "Died"), "Value"))
        t = (g.ex((died, "True"), "PlaySound", Path="Audio/sfx_death.wav", Volume=0.6), "Then")
        p = g.ex(t, "Print")
        g.l(*g.text(["SLICE_TRACE ev=kill a=", V(g, "Kind")]), p, "Text")
        hit = g.br((died, "False"), (g.text_is((poll, "Kind"), "Damage"), "Value"))
        t = (g.ex((hit, "True"), "Animator.PlayLayer", Name="Hit", Clip="Hurt", Loop=False, Fade=0.04, Restart=True), "Then")
        g.ex(t, "PlaySound", Path="Audio/sfx_hit.wav", Volume=0.45)
    # attacks: in Attack mode with the victim in sight the brain's cadence starts a clip; the clip's event does the rest
    st = g.n("Gameplay.AIState")
    a1, a2, a3 = g.fork(f3)
    alive = (g.not_((hp, "Dead")), "Value")
    attacking = g.all_of([alive, (g.text_is((st, "Mode"), "Attack"), "Value"), (st, "Visible")])
    t = g.setv(a1, "Swing", data=g.num("Max", g.num("Subtract", V(g, "Swing"), (u, "Delta")), 0))
    go = g.br(t, g.all_of([attacking, (g.cmp("Less", V(g, "Swing"), 0.001), "Value")]))
    # face the victim while striking
    me, vic = g.n("GetPosition"), g.n("GetPosition")
    g.l(st, "Victim", vic, "Target")
    dd = g.n("SubtractVec3")
    g.l(vic, "Value", dd, "A")
    g.l(me, "Value", dd, "B")
    db = g.n("BreakVec3")
    g.l(dd, "Value", db, "Vector")
    yaw = g.n("Atan2")
    g.l(*g.num("Negate", (db, "X")), yaw, "Y")
    g.l(*g.num("Negate", (db, "Z")), yaw, "X")
    rot = g.n("MakeVec3", X=0, Z=0)
    g.l(yaw, "Value", rot, "Y")
    sr = g.ex((go, "True"), "SetRotation")
    g.l(rot, "Value", sr, "Vector")
    t = g.setv((sr, "Then"), "Swing", data=V(g, "Cooldown"))
    melee = g.br(t, V(g, "Ranged"))
    for out, clip, speed in (("False", "Attack", 1.0), ("True", "Cast", 0.9)):
        t = (g.ex((melee, out), "Animator.PlayLayer", Name="UpperBody", Clip=clip, Speed=speed, Loop=False, Fade=0.1, Restart=True), "Then")
        t = (g.ex(t, "Animator.SetLayerWeight", Name="UpperBody", Weight=1, Fade=0.1), "Then")
        t = g.setv(t, "Layered", True)
        g.ex(t, "PlaySound", Path="Audio/sfx_swing.wav", Volume=0.5)
    rel = g.br(a2, g.all_of([V(g, "Layered"), (g.cmp("Less", V(g, "Swing"), g.num("Subtract", V(g, "Cooldown"), 0.85)), "Value")]))
    g.setv((g.ex((rel, "True"), "Animator.SetLayerWeight", Name="UpperBody", Weight=0, Fade=0.25), "Then"), "Layered", False)
    # clip events
    ev = g.n("Event.AnimationEvent")
    dead_ = g.n("Gameplay.Health")
    hitev = g.br((ev, "Then"), g.all_of([(g.text_is((ev, "Text"), "Hit"), "Value"), (g.not_((dead_, "Dead")), "Value")]))
    strike = g.ex((hitev, "True"), "Gameplay.Strike", MinDot=0.2, TwoD=False, LineOfSight=True, Mask=WALL, Origin=[0, 1.0, 0], Cooldown=0.3)
    g.wire(strike, Victim=(st, "Victim"), Range=V(g, "Reach"), Amount=V(g, "Damage"))
    sok = g.br((strike, "Then"), (strike, "Success"))
    g.ex((sok, "True"), "PlaySound", Path="Audio/sfx_hit.wav", Volume=0.6)
    relev = g.br((hitev, "False"), g.all_of([(g.text_is((ev, "Text"), "Release"), "Value"), (g.not_((dead_, "Dead")), "Value")]))
    cast = g.ex((relev, "True"), "Gameplay.AbilityCast", TwoD=False, LineOfSight=True, Mask=WALL)
    g.wire(cast, Ability=V(g, "Ability"), Victim=(st, "Victim"))
    cok = g.br((cast, "Then"), (cast, "Success"))
    g.ex((cok, "True"), "PlaySound", Path="Audio/sfx_arrow.wav", Volume=0.6)
    # the Warden: a telegraphed shockwave every 6 s (4 s below 40 %) while it fights
    boss = g.br(a3, g.all_of([V(g, "Boss"), alive, g.any_of([(g.text_is((st, "Mode"), "Chase"), "Value"), (g.text_is((st, "Mode"), "Attack"), "Value")])]))
    rage = g.cmp("Less", (hp, "Current"), g.num("Multiply", (hp, "Maximum"), 0.4))
    t = g.setv((boss, "True"), "Wave", data=g.num("Add", V(g, "Wave"), (u, "Delta")))
    warn_at = g.br(t, g.any_of([g.all_of([(rage, "Value"), (g.cmp("Greater", V(g, "Wave"), 3.2), "Value")]),
                                    (g.cmp("Greater", V(g, "Wave"), 5.2), "Value")]))
    show = g.ex((warn_at, "True"), "SetActive", Active=True)
    g.l(g.get("Ring"), "Value", show, "Target")
    fire_at = g.br((show, "Then"), g.any_of([g.all_of([(rage, "Value"), (g.cmp("Greater", V(g, "Wave"), 4.0), "Value")]),
                                             (g.cmp("Greater", V(g, "Wave"), 6.0), "Value")]))
    area = g.ex((fire_at, "True"), "Gameplay.AreaApply", Area="Shockwave", Victim=0, Magnitude=-1)
    g.l(g.n("GetPosition"), "Value", area, "Center")
    wpos = g.n("AddVec3", B=[0, 0.2, 0])
    g.l(g.n("GetPosition"), "Value", wpos, "A")
    mv = g.ex((area, "Then"), "SetPosition", Target=8105)
    g.l(wpos, "Value", mv, "Vector")
    t = (g.ex((mv, "Then"), "EmitParticles", Target=8105, Count=140), "Then")
    t = (g.ex(t, "ShakeCamera", Intensity=0.5, Duration=0.45), "Then")
    t = (g.ex(t, "PlaySound", Path="Audio/sfx_nova.wav", Volume=0.9), "Then")
    t = (g.ex(t, "Print", Text="SLICE_TRACE ev=boss a=shockwave"), "Then")
    hide = g.ex(t, "SetActive", Active=False)
    g.l(g.get("Ring"), "Value", hide, "Target")
    g.setv((hide, "Then"), "Wave", 0)
    g.save(assets, "Scripts/Enemy.zgraph", project)
    return g


def menu_graph(assets, project):
    """The title menu over the pass: an orbiting camera, Continue only for an unfinished run, settings (live volumes,
    fullscreen, language, persisted on Back), credits, demo auto-start, new game / continue with a fade."""
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
    # the UI scale setting (the slider's 8..12 = 80..120 %)
    scale = g.n("ReadState.Number", Key=f"{GS}.Settings.UIScale", Default=1.0)
    sv = g.ex(t, "SetUIValue", Target=6116, Notify=False)
    g.l(*g.num("Multiply", (scale, "Value"), 10), sv, "Value")
    us = g.ex((sv, "Then"), "Display.SetUIScale")
    g.l(scale, "Value", us, "Scale")
    t = (us, "Then")
    best = g.n("ReadState.Number", Key=f"{GS}.Best.Time", Default=0)
    rb = g.br(t, (g.cmp("Greater", (best, "Value"), 0), "Value"))
    rt = g.ex((rb, "True"), "SetUIText", Target=6030)
    g.l(*g.text(["Eng yaxshi vaqt  ", (best, "Value", "time")]), rt, "Text")
    shown = g.br(g.join((rb, "False"), (rt, "Then")), V(g, "Can"))
    g.ex((shown, "True"), "Print", Text="Menyu: Asosiy davom=1")
    g.ex((shown, "False"), "Print", Text="Menyu: Asosiy davom=0")
    # ---- per frame: the camera's slow orbit over the pass, live volumes, the demo's auto-start
    g.lane("update")
    u = g.n("Event.Update")
    t = g.setv((u, "Then"), "Time", data=g.num("Add", V(g, "Time"), (u, "Delta")))
    # a slow sway above the camp: sideways 3 m and a few degrees of yaw (sin of the menu time), the pass always in view
    sway = g.n("Sin")
    g.l(*g.num("Multiply", V(g, "Time"), 0.07), sway, "A")
    pos = g.n("MakeVec3", Y=MENU_EYE[1], Z=MENU_EYE[2])
    g.l(*g.num("Add", g.num("Multiply", (sway, "Value"), 3.0), MENU_EYE[0]), pos, "X")
    mvc = g.ex(t, "SetPosition", Target=CAMERA)
    g.l(pos, "Value", mvc, "Vector")
    rot = g.n("MakeVec3", X=MENU_PITCH, Z=0)
    g.l(*g.num("Add", g.num("Multiply", (sway, "Value"), 0.05), MENU_YAW), rot, "Y")
    rc = g.ex((mvc, "Then"), "SetRotation", Target=CAMERA)
    g.l(rot, "Value", rc, "Vector")
    t = (rc, "Then")
    for widget, group in ((6111, "Music"), (6112, "SFX")):
        vol = g.ex(t, "Audio.SetGroupVolume", Group=group)
        g.l(g.n("UIValue", Target=widget), "Value", vol, "Volume")
        t = (vol, "Then")
    us = g.ex(t, "Display.SetUIScale")
    g.l(*g.num("Multiply", (g.n("UIValue", Target=6116), "Value"), 0.1), us, "Scale")
    t = (us, "Then")
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
    g.ex(t, "LoadScene", Path="Scenes/Dovon.zscene", FadeOut=0.6, FadeIn=0.8, Color=[0.05, 0.03, 0.02, 1])
    t = (g.ex(on("Continue"), "WriteState.String", Key=f"{GS}.Run.Entry", Value="continue"), "Then")
    wd = g.ex(t, "WriteState.Bool", Key=f"{GS}.Run.Demo", Persistent=False)
    g.l(*demo, wd, "Value")
    t = (g.ex((wd, "Then"), "Print", Text="Menyu: Davom etish"), "Then")
    g.ex(t, "LoadScene", Path="Scenes/Dovon.zscene", FadeOut=0.6, FadeIn=0.8, Color=[0.05, 0.03, 0.02, 1])
    g.ex((g.ex(on("Settings"), "Gameplay.UIOpen", Name="Settings"), "Then"), "Print", Text="Menyu: Sozlamalar")
    g.ex((g.ex(on("Credits"), "Gameplay.UIOpen", Name="Credits"), "Then"), "Print", Text="Menyu: Mualliflar")
    t = on("Back")
    music, sfx = g.n("UIValue", Target=6111), g.n("UIValue", Target=6112)
    for key, src in (("Music", music), ("Sfx", sfx)):
        w_ = g.ex(t, "WriteState.Number", Key=f"{GS}.Settings.{key}")
        g.l(src, "Value", w_, "Value")
        t = (w_, "Then")
    ui_scale = g.num("Multiply", (g.n("UIValue", Target=6116), "Value"), 0.1)
    w_ = g.ex(t, "WriteState.Number", Key=f"{GS}.Settings.UIScale")
    g.l(*ui_scale, w_, "Value")
    t = (g.ex((w_, "Then"), "SaveGameState"), "Then")
    p = g.ex(t, "Print")
    g.l(*g.text(["SLICE_SETTINGS music=", (music, "Value", "number", 2), " sfx=", (sfx, "Value", "number", 2), " ui=", (ui_scale[0], "Value", "number", 2),
                 " lang=", (g.n("Localization.Language"), "Value")]), p, "Text")
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
