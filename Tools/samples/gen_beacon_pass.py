#!/usr/bin/env python3
"""Dovon mash'ali (Projects/BeaconPass): the compiler-free 3D vertical slice - a title menu over the mountain pass, one
level (Dovon: the valley camp, the caravanserai and the watchtower plateau), a third-person character controller with
an orbit camera, a skinned humanoid with Animator locomotion (idle/walk/run blend, air/land, an upper-body attack/cast
layer, an additive hit layer, death), melee timed by animation events, Bolt and the Zilzila ground slam, AI guards with
animation-timed (delegated) strikes, archers, the Black Warden boss, a four-quest campaign with XP levels, equipment,
shrine saves (v4) and the beacon finale, victory and defeat screens, music and sound.

Everything is generated deterministically (fixed seeds, UUIDs from fixed ranges and asset paths): running it twice
gives byte-identical files. The numbers in C are the single source of truth for the balance and for the headless
oracle (Tests/BeaconPassTests.cpp reads Tests/oracle.json next to the project - test data outside Assets/, so no export
ships it - written here). Graphs and scenes are built in
beacon_graphs.py (graphkit); models in gltfkit.py; shared art/audio/route helpers in slicekit.py.
"""
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import samplekit as k  # noqa: E402
import slicekit as sk  # noqa: E402
import gltfkit as gk  # noqa: E402
import beacon_graphs as bg  # noqa: E402

NAME = "BeaconPass"
OUT = k.project_dir(NAME)
ASSETS = os.path.join(OUT, "Assets")
PROJECT = NAME

# ------------------------------------------------------------------------------------------------ constants (oracle)
WALK, RUN = 1.8, 5.4                     # m/s: the motor speeds and the locomotion blend thresholds
WALK_STRIDE, RUN_STRIDE = 2.0, 3.9       # m per gait cycle (two steps): clip lengths = stride / speed
C = {
    "Schema": "Dovon-1",
    "Hero": {"Health": 120, "Team": 1, "HitGrace": 0.3, "RespawnDelay": 1.6, "Slots": 16, "Stack": 99, "Stats": [0, 0, 0, 80, 5],
             "Walk": WALK, "Run": RUN, "Jump": 6.5, "Acceleration": 26, "Melee": 14, "MeleeCooldown": 0.45, "MeleeRange": 2.2},
    "Items": {"Shamshir": [0, [10, 0, 0, 0, 0]], "Qalqon": [1, [0, 30, 0, 0, 0]]},
    # Ability: Kind (0 damage, 1 heal), Amount, Cost, Cooldown, Range, Ingredient, Unlocked, Projectile, Area, Effect
    "Abilities": {"Bolt": [0, 14, 10, 0.6, 14, "", True, "Bolt", "", "Burn"],
                  "Zilzila": [0, 20, 30, 7, 4, "", False, "", "Zilzila", "Shock"],
                  "Potion": [1, 40, 0, 1, 5, "Potion", True, "", "", ""]},
    "Quests": [
        # Key, Title, objectives [(Key, Label, Kind 1 collect/2 kill/3 interact, Match, Goal)], Requires, Rewards, XP
        ("Pass", "Dovon yo'li", [("Guards", "Darvoza qorovullarini yeng", 2, "GateGuard", 2), ("Gate", "Karvonsaroy darvozasini och", 3, "Gate", 1)], [], {}, 100),
        ("Stones", "Olov toshlari", [("Stone", "Olov toshlarini yig'", 1, "FireStone", 3)], [], {}, 200),
        ("Warden", "Qora qorovul", [("Warden", "Qora qorovulni yeng", 2, "Warden", 1)], ["Pass"], {}, 300),
        ("Beacon", "Mash'al", [("Beacon", "Mash'alni yoq", 3, "Beacon", 1)], ["Stones", "Warden"], {}, 200),
    ],
    "XP": 800, "FinalLevel": 4,
}
GS = "BP"
WORLD_LAYER, WALL_LAYER, HERO_LAYER, ENEMY_LAYER = 1, 2, 3, 4
WALL_MASK = (1 << WALL_LAYER)                        # projectiles, line of sight of casts
VIEW_MASK = (1 << WORLD_LAYER) | (1 << WALL_LAYER)   # the camera's sphere cast

# ------------------------------------------------------------------------------------------------ the level (metres; x east, z south, y up)
TERRAIN_ORIGIN, TERRAIN_SIZE, TERRAIN_RES = (-60.0, -60.0), 120.0, 81


def smooth(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3 - 2 * t)


def floor_y(z):
    """The valley floor: the camp (y 0), the first ramp, the caravanserai terrace (y 3), the second ramp, the plateau (6)."""
    if z >= 30:
        return 0.0
    if z >= 16:
        return 3.0 * smooth((30 - z) / 14)
    if z >= -18:
        return 3.0
    if z >= -27:
        return 3.0 + 3.0 * smooth((-18 - z) / 9)
    return 6.0


def half_width(z):
    return 19.0 + 2.5 * math.sin(z * 0.07) + (2.0 if z > 30 else 0.0)


def height(x, z):
    base = floor_y(z)
    out = max(0.0, abs(x) - half_width(z))
    rise = out ** 1.25 * 0.85 + out * (0.9 + 0.5 * math.sin(z * 0.21 + x * 0.13)) * smooth(out / 6)
    rise += max(0.0, z - 50) * 1.3 + max(0.0, -48 - z) * 2.2
    ridge = 2.4 * smooth((abs(x) - half_width(z) - 2) / 10) * (math.sin(x * 0.31 + z * 0.17) * 0.6 + math.sin(z * 0.43 - x * 0.11) * 0.4 + 0.5)
    return base + rise + max(0.0, ridge)


def terrain_heights():
    n = TERRAIN_RES
    step = TERRAIN_SIZE / (n - 1)
    hs, ws = [], []
    for j in range(n):
        for i in range(n):
            x, z = TERRAIN_ORIGIN[0] + i * step, TERRAIN_ORIGIN[1] + j * step
            h = height(x, z)
            hs.append(round(h, 3))
    for j in range(n):
        for i in range(n):
            x, z = TERRAIN_ORIGIN[0] + i * step, TERRAIN_ORIGIN[1] + j * step
            h = hs[j * n + i]
            hx = hs[j * n + min(n - 1, i + 1)] - hs[j * n + max(0, i - 1)]
            hz = hs[min(n - 1, j + 1) * n + i] - hs[max(0, j - 1) * n + i]
            slope = math.hypot(hx, hz) / (2 * step)
            rock = smooth((slope - 0.45) / 0.35)
            high = smooth((h - floor_y(z) - 9) / 8)
            path = smooth(1 - abs(x - path_x(z)) / 3.2) if -46 < z < 50 else 0.0
            path *= 1 - rock
            grass = max(0.0, 1 - rock - high - path)
            w = np.array([rock, grass, path, high * (1 - rock)])  # layer 0 (unpainted default): rock
            w = w / max(1e-6, w.sum())
            q = [round(float(v), 1) for v in w]
            q[0] = round(1.0 - sum(q[1:]), 1)
            ws.append([max(0.0, v) for v in q])
    return hs, ws


def path_x(z):
    """The worn track's centre line (decoration only: the dirt layer)."""
    if z > 30:
        return 0.0 + 3.0 * math.sin((z - 30) * 0.12)
    if z > 10:
        return 1.5 * math.sin(z * 0.2)
    return 0.0


# Points of interest (world positions; y follows the floor).
SPAWN = (0.0, 42.0)
P = dict(
    fire=(-3.5, 37.5), tent1=(-9.0, 43.0), tent2=(8.5, 45.5), shrine1=(4.5, 38.0), chest_sword=(-5.5, 33.5), potion1=(3.0, 31.0),
    ledge=(10.5, 29.0), shrine2=(3.4, 0.6), gate=(0.0, 10.0), well=(0.0, -3.5), stone2=(-9.5, -7.5), potion2=(-3.0, -12.0),
    chest_shield=(11.6, -6.0), stone3=(11.0, -11.0), shrine3=(-7.4, -27.8), beacon=(0.0, -42.0), tower=(10.5, -43.5),
)
LEDGE_HALF, LEDGE_TOP = (1.6, 1.6), 1.0
WALLS = dict(x0=-14.0, x1=14.0, z0=-16.0, z1=10.0, thick=1.2, height=4.2, gate=2.2)
STABLE = dict(x0=7.6, x1=13.4, z0=-13.4, z1=-2.8)
PLATFORM = dict(cx=0.0, cz=-42.0, half=3.0, height=3.0, stair_z0=-34.6)

ENEMIES = [
    # kind, position (x, z), save key, patrol offsets (x, z)
    ("guard", (-3.0, 14.0), "Dovon-Guard-01", [(0, 0), (5, 0)], ["Enemy", "GateGuard"]),
    ("guard", (4.0, 12.6), "Dovon-Guard-02", [], ["Enemy", "GateGuard"]),
    ("guard", (-6.5, -2.5), "Dovon-Guard-03", [(0, 0), (0, -6)], ["Enemy", "Guard"]),
    ("archer", (-11.0, -13.0), "Dovon-Archer-01", [], ["Enemy", "Archer"]),
    ("guard", (8.0, -6.0), "Dovon-Guard-04", [(0, 0), (0, 4)], ["Enemy", "Guard"]),
    ("archer", (8.0, -30.5), "Dovon-Archer-02", [], ["Enemy", "Archer"]),
    ("warden", (2.5, -32.5), "Dovon-Warden", [], ["Enemy", "Warden"]),
]
ARCHETYPES = {
    "guard": dict(HP=50, Armor=10, Damage=9, Cooldown=1.5, Speed=2.6, View=8, FOV=140, Leash=12, Enter=1.7, Exit=2.2, Ranged=False, Reach=2.3,
                  model="Guard", weapon="Spear"),
    "archer": dict(HP=32, Armor=0, Damage=8, Cooldown=2.2, Speed=1.2, View=12, FOV=200, Leash=13, Enter=9, Exit=11, Ranged=True, Reach=0,
                   Ability="Arrow", model="Guard", weapon="Bow"),
    "warden": dict(HP=280, Armor=25, Damage=16, Cooldown=1.6, Speed=2.4, View=7.5, FOV=200, Leash=9, Enter=2.0, Exit=2.6, Ranged=False, Reach=2.8,
                   model="Warden", weapon="Mace", Boss=True),
}

# The pilot's plan: (point (x, z), code, direct). Codes: 0 go, 1 walk, 2 interact, 3 hold (1.2 s once arrived, fights first), 4 equip,
# 5 jump, 7 finish. A shrine is a rest stop: the courier comes to a standstill before the rest, so the save holds a body at
# rest (a restore stops the body) and a resumed run continues exactly as the original one.
# `direct` legs skip the route search (the jump onto the ledge, the drop from it, the beacon stairs).
PLAN = [
    ((0.0, 42.0), 0, False), ((-4.2, 34.6), 1, False), ((-4.2, 34.6), 2, False), ((-4.2, 34.6), 4, False), ((3.0, 31.0), 1, False),
    ((10.5, 32.6), 5, False), ((10.5, 29.4), 0, True), ((10.5, 26.2), 0, True), ((4.0, 22.0), 0, False), ((1.5, 17.0), 3, False),
    ((0.0, 11.6), 2, False), ((0.0, 6.0), 0, False), ((-4.0, 0.5), 3, False), ((-9.5, -7.5), 0, False), ((-6.0, -10.5), 3, False),
    ((-3.0, -12.0), 0, False), ((2.6, 1.6), 3, False), ((2.6, 1.6), 2, False), ((6.0, -3.0), 3, False), ((10.3, -6.0), 2, False), ((10.3, -6.0), 4, False),
    ((11.0, -11.0), 0, False), ((0.0, -14.0), 0, False), ((0.0, -19.0), 0, False), ((-5.9, -26.5), 3, False), ((-5.9, -26.5), 2, False), ((0.0, -29.5), 3, False),
    ((0.0, -34.3), 0, False), ((0.0, -39.4), 0, True), ((0.0, -40.4), 2, True), ((0.0, -40.4), 7, True),
]


def floor_at(x, z):
    """Where the hero stands at (x, z): the ledge top, the beacon platform/stairs, else the terrain."""
    lx, lz = P["ledge"]
    if abs(x - lx) <= LEDGE_HALF[0] and abs(z - lz) <= LEDGE_HALF[1]:
        return LEDGE_TOP
    pf = PLATFORM
    if abs(x - pf["cx"]) <= pf["half"] and abs(z - pf["cz"]) <= pf["half"]:
        return 6.0 + pf["height"]
    if abs(x) <= 1.2 and pf["cz"] + pf["half"] < z <= pf["stair_z0"]:
        return 6.0 + pf["height"] * (pf["stair_z0"] - z) / (pf["stair_z0"] - (pf["cz"] + pf["half"]))
    return height(x, z)


# ---- the pilot's route: A* on a 0.5 m grid of the valley (walls, props and steep ground blocked)
GRID = dict(x0=-20.0, z0=-46.0, cell=0.5, w=80, h=192)


def to_cell(x, z):
    return (int((x - GRID["x0"]) / GRID["cell"]), int((z - GRID["z0"]) / GRID["cell"]))


def from_cell(c):
    return (GRID["x0"] + (c[0] + 0.5) * GRID["cell"], GRID["z0"] + (c[1] + 0.5) * GRID["cell"])


def blockers():
    """Axis-aligned footprints (x0, z0, x1, z1) of everything solid the pilot must walk around."""
    w = WALLS
    t = w["thick"] / 2
    out = [(w["x0"] - t, w["z1"] - t, -w["gate"], w["z1"] + t), (w["gate"], w["z1"] - t, w["x1"] + t, w["z1"] + t),
           (w["x0"] - t, w["z0"] - t, -w["gate"], w["z0"] + t), (w["gate"], w["z0"] - t, w["x1"] + t, w["z0"] + t),
           (w["x0"] - t, w["z0"], w["x0"] + t, w["z1"]), (w["x1"] - t, w["z0"], w["x1"] + t, w["z1"])]
    for sx in (-1, 1):
        out.append((sx * 3.4 - 1.3, w["z1"] - 1.3, sx * 3.4 + 1.3, w["z1"] + 1.3))  # gatehouse towers
    for cx in (w["x0"], w["x1"]):
        for cz in (w["z0"], w["z1"]):
            out.append((cx - 1.7, cz - 1.7, cx + 1.7, cz + 1.7))
    lx, lz = P["ledge"]
    out.append((lx - LEDGE_HALF[0], lz - LEDGE_HALF[1], lx + LEDGE_HALF[0], lz + LEDGE_HALF[1]))
    pf = PLATFORM
    out.append((pf["cx"] - pf["half"], pf["cz"] - pf["half"], pf["cx"] + pf["half"], pf["cz"] + pf["half"]))
    out.append((-1.6, pf["cz"] + pf["half"], -1.25, pf["stair_z0"]))
    out.append((1.25, pf["cz"] + pf["half"], 1.6, pf["stair_z0"]))
    for name, r in (("fire", 0.9), ("tent1", 2.6), ("tent2", 2.6), ("shrine1", 0.7), ("chest_sword", 0.6), ("well", 1.1), ("shrine2", 0.7),
                    ("chest_shield", 0.6), ("shrine3", 0.7), ("tower", 2.6)):
        x, z = P[name]
        out.append((x - r, z - r, x + r, z + r))
    s = STABLE
    for px in (s["x0"], s["x1"]):
        for pz in (s["z0"], (s["z0"] + s["z1"]) / 2, s["z1"]):
            out.append((px - 0.25, pz - 0.25, px + 0.25, pz + 0.25))
    for c in CRATES:
        out.append((c[0] - 0.55, c[1] - 0.55, c[0] + 0.55, c[1] + 0.55))
    return out


CRATES = [(-11.5, -2.0), (-11.5, -3.2), (-10.3, -2.0), (12.3, 7.0), (-12.0, 7.5), (6.5, 40.5), (-7.5, 38.0), (2.5, -14.0)]


def walk_grid():
    g = GRID
    walkable = [[True] * g["w"] for _ in range(g["h"])]
    blocks = blockers()
    clearance = 0.45
    for j in range(g["h"]):
        for i in range(g["w"]):
            x, z = from_cell((i, j))
            h = height(x, z)
            if h - floor_y(z) > 0.5 or abs(x) > half_width(z) - 1.0:
                walkable[j][i] = False
                continue
            for x0, z0, x1, z1 in blocks:
                if x0 - clearance <= x <= x1 + clearance and z0 - clearance <= z <= z1 + clearance:
                    walkable[j][i] = False
                    break
    return walkable


def compile_route():
    walkable = walk_grid()
    route, codes = [], []
    first = PLAN[0][0]
    route.append((first[0], floor_at(*first), first[1]))
    codes.append(PLAN[0][1])
    for (point, code, direct), (prev, _, _) in zip(PLAN[1:], PLAN[:-1]):
        if direct or point == prev:
            route.append((point[0], floor_at(*point), point[1]))
            codes.append(code)
            continue
        a, b = to_cell(*prev), to_cell(*point)
        for c in (a, b):
            assert walkable[c[1]][c[0]], f"plan point {from_cell(c)} is blocked"
        for cell, index in sk.route(walkable, [a, b], max_leg=10):
            x, z = (point if index >= 0 else from_cell(cell))
            route.append((round(x, 3), round(floor_at(x, z), 3), round(z, 3)))
            codes.append(code if index >= 0 else (1 if code == 1 else 0))  # a walk leg walks all the way
    return route, codes


# ------------------------------------------------------------------------------------------------ art


def save_tex(rel, img, srgb=True):
    k.write_png(os.path.join(ASSETS, rel), np.clip(img, 0, 1))
    k.write_zmeta(ASSETS, rel, "Texture", {} if srgb else {"sRGB": False}, PROJECT)


def pbr_set(name, albedo, height_map, rough, strength=3.0):
    save_tex(f"Textures/{name}.png", albedo)
    save_tex(f"Textures/{name}_n.png", k.normal_map_from_height(height_map, strength), False)
    orm = k.image(albedo.shape[1], albedo.shape[0], (1, 1, 0, 1))
    orm[:, :, 0] = np.clip(0.55 + 0.45 * height_map, 0, 1)
    orm[:, :, 1] = rough
    orm[:, :, 2] = 0
    save_tex(f"Textures/{name}_orm.png", orm, False)


def colourise(base, dark, t):
    img = k.image(t.shape[1], t.shape[0], (0, 0, 0, 1))
    for c in range(3):
        img[:, :, c] = dark[c] + (base[c] - dark[c]) * t
    return img


def make_textures():
    s = 256
    ys, xs = np.mgrid[0:s, 0:s].astype(np.float64)
    # ashlar stone: staggered blocks, chipped edges
    n = k.value_noise(s, s, 8, seed=101, octaves=5)
    n2 = k.value_noise(s, s, 3, seed=102, octaves=3)
    row = (ys // 64).astype(int)
    bx = (xs + (row % 2) * 48) % 96
    by = ys % 64
    edge = np.minimum(np.minimum(bx, 96 - bx), np.minimum(by, 64 - by))
    mortar = k.smooth(3.5, 1.0, edge + 2.5 * (n - 0.5))
    block_tone = (np.sin(row * 12.9898 + ((xs + (row % 2) * 48) // 96) * 78.233) * 43758.5453) % 1.0
    h = 0.55 + 0.25 * n + 0.1 * n2 - 0.6 * mortar + 0.08 * block_tone
    t = np.clip(0.62 + 0.28 * n + 0.12 * block_tone - 0.35 * mortar, 0, 1)
    pbr_set("stone", colourise((0.86, 0.74, 0.58), (0.36, 0.29, 0.22), t), h, 0.82 + 0.1 * mortar, 4.0)
    # lime plaster with faint cracks
    n = k.value_noise(s, s, 6, seed=111, octaves=5)
    crack = k.smooth(0.012, 0.0, np.abs(k.value_noise(s, s, 5, seed=112, octaves=2) - 0.5))
    h = 0.5 + 0.2 * n - 0.4 * crack
    pbr_set("plaster", colourise((0.92, 0.84, 0.70), (0.62, 0.52, 0.40), np.clip(0.7 + 0.3 * n - 0.4 * crack, 0, 1)), h, np.full_like(h, 0.9), 2.0)
    # cedar planks
    n = k.value_noise(s, s, 4, seed=121, octaves=4)
    grain = 0.5 + 0.5 * np.sin(xs * 0.9 + 6 * k.value_noise(s, s, 3, seed=122, octaves=3))
    seam = k.smooth(1.6, 0.4, np.minimum(ys % 42, 42 - ys % 42))
    h = 0.5 + 0.15 * grain + 0.15 * n - 0.5 * seam
    pbr_set("wood", colourise((0.66, 0.44, 0.26), (0.30, 0.18, 0.10), np.clip(0.55 + 0.25 * grain + 0.2 * n - 0.5 * seam, 0, 1)), h, np.full_like(h, 0.78), 3.0)
    # terrain: grass, dirt path, rock, scree
    n = k.value_noise(s, s, 10, seed=131, octaves=5)
    blades = k.value_noise(s, s, 40, seed=132, octaves=2)
    pbr_set("t_grass", colourise((0.52, 0.56, 0.26), (0.22, 0.27, 0.10), np.clip(0.4 + 0.4 * n + 0.3 * blades, 0, 1)), 0.5 + 0.3 * blades, np.full((s, s), 0.95), 2.0)
    n = k.value_noise(s, s, 12, seed=141, octaves=5)
    pebbles = k.smooth(0.08, 0.0, k.value_noise(s, s, 28, seed=142, octaves=1) - 0.62)
    pbr_set("t_dirt", colourise((0.70, 0.56, 0.40), (0.36, 0.26, 0.18), np.clip(0.5 + 0.35 * n + 0.3 * pebbles, 0, 1)), 0.4 + 0.3 * n + 0.4 * pebbles, np.full((s, s), 0.9), 3.0)
    n = k.value_noise(s, s, 6, seed=151, octaves=6)
    strata = 0.5 + 0.5 * np.sin(ys * 0.12 + 4 * n)
    pbr_set("t_rock", colourise((0.62, 0.56, 0.52), (0.26, 0.23, 0.22), np.clip(0.35 + 0.45 * n + 0.2 * strata, 0, 1)), 0.3 + 0.5 * n + 0.2 * strata, np.full((s, s), 0.85), 5.0)
    n = k.value_noise(s, s, 18, seed=161, octaves=4)
    pbr_set("t_scree", colourise((0.82, 0.80, 0.78), (0.42, 0.40, 0.40), np.clip(0.45 + 0.5 * n, 0, 1)), 0.3 + 0.6 * n, np.full((s, s), 0.9), 4.0)
    # cloth with an ikat band (banners and awnings use the model colours; this one is for the stall awnings)
    x, y = xs / s, ys / s
    ikat = 0.5 + 0.5 * np.sin(2 * math.pi * (x * 4 + 0.15 * np.sin(2 * math.pi * y * 3)))
    cloth = k.image(s, s, (0, 0, 0, 1))
    for c, (a, b) in enumerate(((0.12, 0.85), (0.22, 0.55), (0.55, 0.25))):
        cloth[:, :, c] = a + (b - a) * ikat
    save_tex("Textures/ikat.png", cloth)


def make_ui():
    ui = k.Atlas(1024, 512)
    sk.ui_frames(ui, accent=(0.98, 0.72, 0.32), deep=(0.05, 0.07, 0.12))
    for kind in ("potion", "bolt", "nova", "sword", "armor", "melee"):
        ui.add(f"icon_{kind}", sk.paint_icon(kind), border=(0, 0, 0, 0))
    ui.add("icon_stone", _stone_icon(), border=(0, 0, 0, 0))
    ui.add("vignette", _vignette(128), border=(40, 40, 40, 40))
    ui.save(ASSETS, "Textures/ui.png", PROJECT)
    icon = k.image(128, 128)
    xi, yi = k.coords(128, 128)
    sk._shade(icon, k.sd_round_box(xi, yi, 60, 60, 24), (0.10, 0.16, 0.30), (0.03, 0.04, 0.08), yi, -60, 60)
    flame = _stone_icon(96)
    region = icon[16:112, 16:112]
    a = flame[:, :, 3:4]
    region[:, :, :3] = flame[:, :, :3] * a + region[:, :, :3] * (1 - a)
    region[:, :, 3:4] = np.maximum(region[:, :, 3:4], a)
    k.write_png(os.path.join(ASSETS, "Textures/GameIcon.png"), icon)
    k.write_zmeta(ASSETS, "Textures/GameIcon.png", "Texture", {}, PROJECT)


def _stone_icon(size=64):
    img = k.image(size, size)
    x, y = k.coords(size, size)
    gem = k.sd_polygon(x, y, [(0, -size * 0.36), (size * 0.24, -size * 0.04), (size * 0.12, size * 0.32), (-size * 0.14, size * 0.32), (-size * 0.25, -size * 0.02)])
    k.glow(img, gem, (1.0, 0.5, 0.12, 1), size * 0.14, 1.0)
    sk._shade(img, gem, (1.0, 0.86, 0.45), (0.85, 0.22, 0.05), y, -size * 0.36, size * 0.32)
    k.stroke(img, gem, (1.0, 0.95, 0.75, 0.8), 1.5)
    return sk.outline(img, (0.05, 0.04, 0.07), 2)


def _vignette(size):
    img = k.image(size, size)
    x, y = k.coords(size, size)
    r = np.maximum(np.abs(x), np.abs(y)) / (size / 2)
    img[:, :, :3] = (0.55, 0.0, 0.02)
    img[:, :, 3] = np.clip((r - 0.55) / 0.45, 0, 1) ** 1.5
    return img


LOOKS = {
    "Hero": dict(name="Hero", beard=True, hat="turban", colours={
        "skin": ((0.78, 0.56, 0.42), 0.6, 0.0), "cloth": ((0.10, 0.30, 0.44), 0.8, 0.0), "trim": ((0.93, 0.89, 0.80), 0.85, 0.0),
        "leather": ((0.40, 0.24, 0.13), 0.65, 0.0), "metal": ((0.75, 0.72, 0.68), 0.35, 1.0), "dark": ((0.17, 0.13, 0.12), 0.85, 0.0)}),
    "Guard": dict(name="Guard", beard=True, hat="helmet", armour=True, wide=1.08, bulk=1.06, colours={
        "skin": ((0.72, 0.50, 0.38), 0.6, 0.0), "cloth": ((0.55, 0.12, 0.10), 0.8, 0.0), "trim": ((0.30, 0.08, 0.07), 0.85, 0.0),
        "leather": ((0.30, 0.20, 0.12), 0.7, 0.0), "metal": ((0.55, 0.56, 0.60), 0.35, 1.0), "dark": ((0.12, 0.11, 0.12), 0.85, 0.0)}),
    "Warden": dict(name="Warden", scale=1.25, beard=False, hat="horned", armour=True, wide=1.15, bulk=1.15, glow=(3.0, 0.6, 0.15), skirt=0.42, colours={
        "skin": ((0.50, 0.42, 0.40), 0.6, 0.0), "cloth": ((0.10, 0.09, 0.12), 0.8, 0.0), "trim": ((0.75, 0.70, 0.60), 0.7, 0.0),
        "leather": ((0.18, 0.12, 0.10), 0.7, 0.0), "metal": ((0.22, 0.21, 0.25), 0.3, 1.0), "dark": ((0.06, 0.05, 0.07), 0.85, 0.0),
        "glow": ((1.0, 0.30, 0.10), 0.3, 0.0)}),
}


def make_models():
    clips = gk.clip_set(WALK, RUN, WALK_STRIDE, RUN_STRIDE)
    for look in LOOKS.values():
        rel = f"Models/{look['name']}.glb"
        gk.write_humanoid(os.path.join(ASSETS, rel), look, clips, fps=15)
        k.write_zmeta(ASSETS, rel, "Model", {}, PROJECT)

    def static(name, parts):
        rel = f"Models/{name}.glb"
        gk.write_static(os.path.join(ASSETS, rel), parts, name)
        k.write_zmeta(ASSETS, rel, "Model", {}, PROJECT)
    static("Sword", gk.sword())
    static("Spear", gk.spear())
    static("Mace", gk.mace())
    static("Shield", gk.shield())
    static("Bow", _bow())
    static("Brazier", gk.brazier(1.0))
    static("Beacon", gk.brazier(2.2, 4))
    static("Tent", gk.tent())
    static("TentRed", gk.tent(2.0, 2.4, (0.62, 0.22, 0.16)))
    static("Well", gk.well())
    static("Shrine", gk.shrine())
    static("Chest", gk.chest())
    static("Crate", gk.crate())
    static("BannerIndigo", gk.banner((0.16, 0.18, 0.52), (0.95, 0.75, 0.3)))
    static("BannerTeal", gk.banner((0.08, 0.48, 0.50), (0.95, 0.75, 0.3)))
    static("FireStone", gk.crystal((1.0, 0.55, 0.18), (2.4, 0.9, 0.2), 0.28))
    static("Potion", _potion())
    static("Bolt", gk.bolt_visual((1.0, 0.75, 0.3), (3.0, 1.4, 0.4)))
    static("Arrow", gk.arrow_visual())
    static("Ring", gk.ring(4.0, (1.0, 0.35, 0.15), (2.5, 0.6, 0.2)))
    for i in range(3):
        static(f"Pine{i}", gk.pine(300 + i, 6.0 + i))
    static("PineFar", gk.pine(310, 7.0, far=True))
    for i in range(3):
        static(f"Rock{i}", gk.rock(400 + i, 1.0))
    for i in range(3):
        static(f"Peak{i}", gk.peak(500 + i, 60.0, 70.0 + 18 * i))


def _potion():
    glass, cork = gk.Part(), gk.Part()
    gk.rigid(glass, gk.revolve([(0.001, 0.0), (0.12, 0.02), (0.16, 0.1), (0.15, 0.2), (0.06, 0.28), (0.05, 0.36)], (0, 0, 0), (0, 1, 0), 12)[:4])
    gk.rigid(cork, gk.revolve([(0.055, 0.34), (0.06, 0.42), (0.001, 0.43)], (0, 0, 0), (0, 1, 0), 8)[:4])
    return [("Glass", (0.85, 0.12, 0.22), 0.15, 0.0, (0.9, 0.1, 0.18), glass), ("Cork", (0.55, 0.38, 0.22), 0.9, 0.0, (0, 0, 0), cork)]


def _bow():
    wood, string = gk.Part(), gk.Part()
    pts = []
    for i in range(13):
        t = -1 + 2 * i / 12
        pts.append((0.0, t * 0.62, -0.16 * (1 - t * t)))
    for a, b in zip(pts, pts[1:]):
        gk.tube(wood, a, b, 0.018, 0.018, (0, 0), seg=6, cap=2)
    gk.tube(string, pts[0], pts[-1], 0.004, 0.004, (0, 0), seg=4, cap=1)
    return [("Wood", (0.42, 0.26, 0.14), 0.7, 0.0, (0, 0, 0), wood), ("String", (0.9, 0.86, 0.75), 0.9, 0.0, (0, 0, 0), string)]


# ------------------------------------------------------------------------------------------------ audio


def make_audio():
    rel = lambda name: os.path.join(ASSETS, "Audio", name)
    k.write_wav(rel("music_menu.wav"), sk.music_loop(12.0, k.note_freq("D3"), "hijaz", 72, 21, 0.4, 0.35, drums=False))
    k.write_wav(rel("music_dovon.wav"), sk.music_loop(12.0, k.note_freq("A2"), "dorian", 92, 22, 0.5, 0.28))
    k.write_wav(rel("music_warden.wav"), sk.music_loop(10.0, k.note_freq("C2"), "hijaz", 120, 23, 0.6, 0.3, dark=True))
    t = np.arange(int(6 * k.RATE)) / k.RATE
    wind = k.lowpass(k.noise(6, 31), 420) * (0.55 + 0.45 * np.sin(2 * np.pi * 0.17 * t) ** 2)
    birds = np.zeros_like(t)
    for i in range(7):
        at = (i * 0.83 + (i * 31 % 7) * 0.11) % 5.5
        n = int(0.09 * k.RATE)
        tt = np.arange(n) / k.RATE
        chirp = np.sin(2 * np.pi * (3200 + 1800 * tt / 0.09 + 300 * np.sin(tt * 90)) * tt) * np.exp(-tt * 30)
        k.place(birds, chirp, at, 0.12)
    k.write_wav(rel("amb_pass.wav"), wind * 0.7 + birds, peak=0.5)
    for kind in ("swing", "hit", "bolt", "chest", "death", "hurt", "levelup", "ui", "victory", "defeat", "nova", "lamp"):
        k.write_wav(rel(f"sfx_{kind}.wav"), sk.sfx(kind, 7))
    # footsteps on dirt/stone: short filtered noise thumps
    for i in range(4):
        n = int(0.12 * k.RATE)
        tt = np.arange(n) / k.RATE
        thump = k.lowpass(k.noise(0.12, 40 + i)[:n], 900 + 250 * i) * np.exp(-tt * 38) + np.sin(2 * np.pi * (85 + 9 * i) * tt) * np.exp(-tt * 45) * 0.6
        k.write_wav(rel(f"step_{i}.wav"), thump, peak=0.6)
    # the arrow's whoosh and the beacon's roar
    n = int(0.35 * k.RATE)
    tt = np.arange(n) / k.RATE
    k.write_wav(rel("sfx_arrow.wav"), k.highpass(k.noise(0.35, 50)[:n], 1500) * np.sin(np.pi * tt / 0.35) ** 2, peak=0.5)
    n = int(2.2 * k.RATE)
    tt = np.arange(n) / k.RATE
    roar = k.lowpass(k.noise(2.2, 51)[:n], 700) * np.clip(tt / 0.3, 0, 1) * np.exp(-tt * 1.1) + np.sin(2 * np.pi * 48 * tt) * np.exp(-tt * 1.6) * 0.8
    k.write_wav(rel("sfx_beacon.wav"), roar, peak=0.8)
    for name in sorted(os.listdir(os.path.join(ASSETS, "Audio"))):
        if name.endswith(".wav"):
            k.write_zmeta(ASSETS, f"Audio/{name}", "Audio", {}, PROJECT)


# ------------------------------------------------------------------------------------------------ localization

TEXT = {
    "menu.new": ("Yangi o'yin", "New game"), "menu.continue": ("Davom etish", "Continue"), "menu.settings": ("Sozlamalar", "Settings"),
    "menu.credits": ("Mualliflar", "Credits"), "menu.quit": ("Chiqish", "Quit"), "menu.back": ("Orqaga", "Back"),
    "menu.subtitle": ("Karvon yo'li afsonasi", "A legend of the caravan road"),
    "menu.music": ("Musiqa", "Music"), "menu.sfx": ("Ovoz effektlari", "Sound effects"), "menu.fullscreen": ("To'liq ekran", "Fullscreen"), "menu.uiscale": ("Interfeys o'lchami", "Interface size"),
    "menu.language": ("Til: O'zbekcha", "Language: English"),
    "menu.credits.body": ("Zamin Engine 3D vertical slice\nModellar, animatsiyalar, teksturalar, musiqa va ovozlar protsedurali generatorlardan:\n"
                          "Tools/samples/gen_beacon_pass.py, gltfkit.py, beacon_graphs.py\n\nRahmat, chopar!",
                          "Zamin Engine 3D vertical slice\nModels, animations, textures, music and sound come from procedural generators:\n"
                          "Tools/samples/gen_beacon_pass.py, gltfkit.py, beacon_graphs.py\n\nThank you, courier!"),
    "pause.title": ("Pauza", "Paused"), "pause.resume": ("Davom etish", "Resume"), "pause.bag": ("Inventar", "Inventory"),
    "pause.savequit": ("Saqlab, bosh menyuga", "Save and quit to menu"),
    "bag.title": ("Inventar", "Inventory"), "bag.equip": ("Kiyish", "Equip"), "bag.drink": ("Ichish", "Drink"), "bag.close": ("Yopish", "Close"),
    "bag.next": ("Keyingi", "Next"),
    "defeat.title": ("YO'L YOPILDI", "THE ROAD IS CLOSED"), "defeat.retry": ("Oxirgi saqlashdan", "From the last save"),
    "victory.title": ("MASH'AL YONDI", "THE BEACON BURNS"), "victory.sub": ("Karvon yo'lni topdi", "The caravan found its road"),
    "end.menu": ("Bosh menyu", "Main menu"), "hud.quests": ("Vazifalar", "Quests"), "hud.boss": ("Qora qorovul", "The Black Warden"),
    "controls": ("[Move] yurish   [LookX|Sichqoncha] qarash   [Jump] sakrash   [Walk] sekin   [Attack] zarba   [Bolt] olov o'qi   [Slam] zilzila   [Potion] ichimlik   "
                 "[Interact] ishlatish   [Pause] pauza",
                 "[Move] move   [LookX|Mouse] look   [Jump] jump   [Walk] walk   [Attack] strike   [Bolt] fire bolt   [Slam] ground slam   [Potion] potion   "
                 "[Interact] use   [Pause] pause"),
}


# What the HUD names by key (the engine looks up "item.<Key>", "combat.<Key>" and "quest.<Quest>[.<Objective>]"; a key
# without a string shows as it is): the bag's items, the combat feed's payloads, the quest book.
ITEM_NAMES = {"FireStone": ("Olov toshi", "Fire stone"), "Potion": ("Ichimlik", "Potion"), "Shamshir": ("Shamshir", "Sabre"),
              "Qalqon": ("Qalqon", "Shield"), "Key": ("Kalit", "Key")}
COMBAT_NAMES = {"Bolt": ("Olov o'qi", "Fire bolt"), "Burn": ("Kuyish", "Burn"), "Arrow": ("O'q", "Arrow"), "Zilzila": ("Zilzila", "Ground slam"),
                "Shockwave": ("Zarba to'lqini", "Shockwave"), "Shock": ("Karaxt", "Stun"), "Potion": ("Ichimlik", "Potion")}
QUEST_EN = {"Pass": ("The mountain pass", {"Guards": "Defeat the gate guards", "Gate": "Open the caravanserai gate"}),
            "Stones": ("Fire stones", {"Stone": "Gather the fire stones"}), "Warden": ("The Black Warden", {"Warden": "Defeat the Black Warden"}),
            "Beacon": ("The beacon", {"Beacon": "Light the beacon"})}


def make_localization():
    """uz.json / en.json: the scene texts (TEXT), the strings the graphs build (beacon_graphs.TEXT, filled by build), the
    HUD's item / combat / quest names and the control names. An empty text is left out (the fallback is "")."""
    pairs = dict(TEXT)
    pairs.update(bg.TEXT)
    for key, pair in ITEM_NAMES.items():
        pairs[f"item.{key}"] = pair
    for key, pair in COMBAT_NAMES.items():
        pairs[f"combat.{key}"] = pair
    for key, title, objectives, *_ in C["Quests"]:
        en_title, en_labels = QUEST_EN[key]
        pairs[f"quest.{key}"] = (title, en_title)
        for o in objectives:
            pairs[f"quest.{key}.{o[0]}"] = (o[1], en_labels[o[0]])
    for lang, index in (("uz", 0), ("en", 1)):
        table = {"$name": "O'zbekcha" if lang == "uz" else "English"}
        for key, pair in sorted(pairs.items()):
            if pair[index]:
                table[key] = pair[index]
        for kind, names in k.INPUT_NAMES[lang].items():
            for control, label in names.items():
                table[f"input.{kind}.{control}"] = label
        k.write_json(os.path.join(ASSETS, "Localization", f"{lang}.json"), table)
        k.write_zmeta(ASSETS, f"Localization/{lang}.json", "Localization", {}, PROJECT)


# ------------------------------------------------------------------------------------------------ manifest

# (no Sprint: the courier runs by default and walks on Walk - Gameplay.Input reads none; every action has a key and a
# gamepad control)
ACTIONS = [("Jump", ["Key:Space", "Gamepad:A"]), ("Walk", ["Key:LeftShift", "Gamepad:LeftThumb"]),
           ("Attack", ["Mouse:Left", "Key:J", "Gamepad:X"]), ("Bolt", ["Mouse:Right", "Key:K", "Gamepad:RightBumper"]),
           ("Slam", ["Key:Q", "Gamepad:Y"]), ("Potion", ["Key:R", "Gamepad:DPadDown"]), ("Interact", ["Key:E", "Gamepad:B"]),
           ("Inventory", ["Key:I", "Key:Tab", "Gamepad:Back"]), ("Pause", ["Key:Escape", "Key:P", "Gamepad:Start"])]


def main():
    os.makedirs(ASSETS, exist_ok=True)
    make_textures()
    make_ui()
    make_models()
    make_audio()
    route, codes = compile_route()
    heights, weights = terrain_heights()
    walk_cycle = WALK_STRIDE / WALK
    run_cycle = RUN_STRIDE / RUN
    oracle = {"Schema": C["Schema"], "XP": C["XP"], "FinalLevel": C["FinalLevel"], "Quests": [q[0] for q in C["Quests"]], "Stones": 3,
              "Enemies": len(ENEMIES), "Waypoints": len(route), "Walk": WALK, "Run": RUN, "WalkStride": WALK_STRIDE, "RunStride": RUN_STRIDE,
              "WalkCycle": round(walk_cycle, 4), "RunCycle": round(run_cycle, 4), "Terrain": {"Origin": list(TERRAIN_ORIGIN), "Size": TERRAIN_SIZE,
                                                                                               "Resolution": TERRAIN_RES}}
    world = dict(heights=heights, weights=weights, origin=TERRAIN_ORIGIN, size=TERRAIN_SIZE, res=TERRAIN_RES, height=height, floor_at=floor_at,
                 floor_y=floor_y, P=P, WALLS=WALLS, STABLE=STABLE, PLATFORM=PLATFORM, LEDGE=(P["ledge"], LEDGE_HALF, LEDGE_TOP), CRATES=CRATES,
                 half_width=half_width)
    bg.build(ASSETS, PROJECT, C, world, ENEMIES, ARCHETYPES, route, codes, oracle, dict(WALL_MASK=WALL_MASK, VIEW_MASK=VIEW_MASK, GS=GS,
             WORLD_LAYER=WORLD_LAYER, WALL_LAYER=WALL_LAYER, HERO_LAYER=HERO_LAYER, ENEMY_LAYER=ENEMY_LAYER))
    make_localization()  # after the graphs: they register the strings they build
    k.write_json(os.path.join(OUT, "Tests", "oracle.json"), oracle)
    names = ["Default", "World", "Wall", "Hero", "Enemy"]
    k.write_project(OUT, NAME, "Dovon mash'ali", "Scenes/Menu.zscene", bg.scene_uuid(PROJECT, "Menu"), version="1.0.0",
                    build_scenes=[(f"Scenes/{s}.zscene", bg.scene_uuid(PROJECT, s)) for s in ("Menu", "Dovon")],
                    actions=ACTIONS,
                    axes=[{"Deadzone": 0.15, "GamepadAxis": "LeftX", "Invert": False, "Name": "Horizontal", "Negative": ["Key:A", "Key:Left", "Gamepad:DPadLeft"],
                           "Positive": ["Key:D", "Key:Right", "Gamepad:DPadRight"]},
                          {"Deadzone": 0.15, "GamepadAxis": "LeftY", "Invert": True, "Name": "Vertical", "Negative": ["Key:S", "Key:Down"],
                           "Positive": ["Key:W", "Key:Up", "Gamepad:DPadUp"]},
                          {"Deadzone": 0.18, "GamepadAxis": "RightX", "Invert": False, "Name": "LookX", "Negative": [], "Positive": []},
                          {"Deadzone": 0.18, "GamepadAxis": "RightY", "Invert": False, "Name": "LookY", "Negative": [], "Positive": []}],
                    vectors=[{"Deadzone": 0.0, "Mode": "Clamp", "Name": "Move", "X": "Horizontal", "Y": "Vertical"}],
                    tags_=("Hero", "Enemy", "Guard", "GateGuard", "Archer", "Warden", "Pickup", "Shrine", "Gate", "Beacon", "Chest", "FireStone"),
                    layers={"names": names, "matrix": k.collision_matrix(names, [("Default", "Default"), ("Default", "World"), ("Default", "Wall"),
                                                                                    ("Default", "Hero"), ("Default", "Enemy"), ("World", "Hero"), ("World", "Enemy"),
                                                                                    ("Wall", "Hero"), ("Wall", "Enemy"), ("Hero", "Enemy"), ("Enemy", "Enemy")])},
                    extra=k.export_always_include(["Audio/*.wav", "Localization/*.json"]),
                    description="Dovon mash'ali: a 3D action-adventure vertical slice - a mountain pass, a caravanserai, AI guards and a boss, "
                                "Animator locomotion, quests, equipment, shrine saves and the beacon finale.")
    manifest = json.load(open(os.path.join(OUT, f"{NAME}.zproject")))
    manifest["Project"]["ScriptModulePath"] = ""
    k.write_json(os.path.join(OUT, f"{NAME}.zproject"), manifest)


if __name__ == "__main__":
    main()
