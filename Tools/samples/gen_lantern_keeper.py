#!/usr/bin/env python3
"""Chiroqbon (Projects/LanternKeeper): the compiler-free 2D vertical slice - a title menu, two levels (Boglar, the dusk
orchards outside the old fortress; Yertola, the crypt beneath it with the boss Soya Bek), a hero motor with melee,
projectile and area abilities, AI enemies (melee, archers, a chief, spirits and a boss), a campaign quest book with XP
levels and unlocks, equipment, inventory, HUD, pause, level card, victory and defeat screens, music and sound.

Everything is generated deterministically (fixed seeds, UUIDs from fixed ranges and asset paths): running it twice
gives byte-identical files. The numbers in CONSTANTS are the single source of truth for the balance and for the
headless oracle (Tests/LanternKeeperTests.cpp reads Tests/oracle.json next to the project - test data outside Assets/,
so no export ships it - written here).
Graphs are built in lantern_graphs.py with graphkit; shared art/audio/route helpers live in slicekit.py.
"""
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import samplekit as k  # noqa: E402
import slicekit as sk  # noqa: E402
import lantern_graphs as lg  # noqa: E402

NAME = "LanternKeeper"
OUT = k.project_dir(NAME)
ASSETS = os.path.join(OUT, "Assets")
PROJECT = NAME

# ------------------------------------------------------------------------------------------------ constants (oracle)
C = {
    "Schema": "Chiroqbon-1",
    "Hero": {"Health": 100, "Team": 1, "HitGrace": 0.35, "RespawnDelay": 1.2, "Slots": 16, "Stack": 99,
             "Stats": [0, 0, 0, 60, 4], "Speed": 4.2, "Acceleration": 28, "Melee": 10, "MeleeCooldown": 0.4, "MeleeRange": 1.3},
    "Items": {"Qilich": [0, [8, 0, 0, 0, 0]], "Tumor": [3, [0, 0, 3, 20, 0]], "Zirh": [2, [0, 25, 0, 0, 0]]},
    # Ability: Kind (0 damage, 1 heal, 3 effect), Amount, Cost, Cooldown, Range, Ingredient, Unlocked, Projectile, Area, Effect
    "Abilities": {"Bolt": [0, 12, 8, 0.55, 9, "", True, "Bolt", "", "Burn"],
                  "Nova": [0, 16, 25, 6, 4, "", False, "", "Nova", "Shock"],
                  "Mend": [3, 1, 20, 10, 5, "", False, "", "", "Regen"],
                  "Potion": [1, 35, 0, 1, 5, "Potion", True, "", "", ""]},
    "Quests": [
        # Key, Title, objectives [(Key, Label, Kind 1 collect/2 kill/3 interact, Match, Goal)], Requires, Rewards, XP
        ("Gate", "Bog' darvozasi", [("Chief", "Qaroqchi boshlig'ini yeng", 2, "Chief", 1), ("Gate", "Darvozani och", 3, "Gate", 1)], [], {"Coin": 5}, 100),
        ("Herbs", "Shifobaxsh o'tlar", [("Herb", "Shifobaxsh o't", 1, "Herb", 3)], [], {"Potion": 2}, 100),
        ("Lamps", "Uch chiroq", [("Lamp", "Tong chiroqlari", 1, "Lamp", 3)], [], {}, 200),
        ("Boss", "Soya Bek", [("Boss", "Soya Bekni yeng", 2, "Boss", 1)], ["Gate"], {}, 250),
        ("Dawn", "Tong chirog'i", [("Altar", "Mehrobda chiroqni yoq", 3, "Altar", 1)], ["Lamps"], {}, 200),
    ],
    "XP": 850, "FinalLevel": 4,
}
WALL_LAYER, WALL_MASK = 1, 2
GS = "LK"  # GameState prefix
UUID_HERO, UUID_CAMERA, UUID_GAME = 1000, 1003, 1100

# ------------------------------------------------------------------------------------------------ level layouts
# Grids are built from carved rectangles (inclusive cell ranges, y up); markers sit at cell centres.


def grid(w, h, rooms, water=(), pillars=()):
    g = [["#"] * w for _ in range(h)]
    for x0, y0, x1, y1 in rooms:
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                g[y][x] = "."
    for x0, y0, x1, y1 in water:
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                g[y][x] = "~"
    for x0, y0, x1, y1 in pillars:
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                g[y][x] = "#"
    return g


LEVELS = {
    1: dict(
        scene="Boglar", title="Bog'lar", title_en="THE ORCHARDS", size=(46, 26),
        rooms=[(1, 1, 14, 24), (15, 4, 18, 7), (19, 1, 31, 11), (23, 12, 26, 14), (17, 15, 34, 24), (35, 18, 38, 20), (39, 15, 44, 24)],
        water=[(5, 15, 9, 18), (26, 6, 29, 8)],
        pillars=[],
        spawn=(3, 3),
        trees=[(2, 8), (11, 4), (12, 12), (2, 19), (9, 23), (13, 22), (20, 2), (30, 2), (18, 22), (33, 16), (21, 18)],
        enemies=[
            # kind, cell, save key, patrol (relative cells)
            ("bandit", (23, 6), "L1-Bandit-01", [(0, 0), (2, 2)]),
            ("bandit", (27, 3), "L1-Bandit-02", [(0, 0), (-3, 0)]),
            ("archer", (29, 10), "L1-Archer-01", []),
            ("bandit", (22, 19), "L1-Bandit-03", [(0, 0), (0, 3)]),
            ("bandit", (28, 21), "L1-Bandit-04", [(0, 0), (-3, 0)]),
            ("archer", (32, 23), "L1-Archer-02", []),
            ("chief", (30, 18), "L1-Chief", [(0, 0), (2, 0)]),
        ],
        pickups=[("Herb", (11, 6)), ("Herb", (3, 13)), ("Herb", (12, 20)), ("Coin", (6, 4)), ("Coin", (8, 10)), ("Coin", (5, 22)),
                 ("Coin", (16, 5)), ("Coin", (24, 9)), ("Coin", (20, 16)), ("Coin", (33, 20)), ("Coin", (41, 19)), ("Lamp", (42, 22))],
        uses=[("Chest", (3, 22), "L1-Chest", dict(Prompt="Sandiqni ochish", Reward="Qilich")),
              ("Shrine", (21, 9), "L1-Shrine", dict(Prompt="Saqlash ziyoratgohi", MaxUses=0, Cooldown=2)),
              ("Gate", (36, 19), "L1-Gate", dict(Prompt="Darvozani ochish", Required="Key", Consume=True))],
        exit=(42, 16),
        # the bot's points of interest: (cell, code, argument). 0 go, 2 interact, 3 hold until clear, 4 equip, 7 finish
        plan=[((3, 3), 0), ((11, 6), 0), ((6, 4), 0), ((8, 10), 0), ((3, 13), 0), ((12, 20), 0), ((5, 22), 0), ((3, 21), 2), ((3, 20), 4),
              ((12, 13), 0), ((16, 5), 0), ((19, 6), 3), ((24, 9), 0), ((21, 8), 2), ((25, 12), 0), ((25, 14), 3), ((20, 16), 0), ((25, 17), 3),
              ((30, 19), 3), ((33, 20), 0), ((34, 19), 4), ((35, 19), 2), ((39, 19), 0), ((41, 19), 0), ((42, 22), 0), ((42, 16), 7)],
        ambient=((1.0, 0.78, 0.60), 0.5), music="music_boglar.wav", amb="amb_orchard.wav",
    ),
    2: dict(
        scene="Yertola", title="Yertola", title_en="THE CRYPT", size=(44, 30),
        rooms=[(2, 2, 12, 9), (13, 4, 13, 6), (14, 2, 30, 9), (31, 4, 31, 6), (32, 2, 41, 12), (21, 10, 23, 10), (14, 11, 30, 16),
               (11, 13, 13, 15), (2, 12, 10, 20), (21, 17, 23, 17), (12, 18, 32, 28)],
        water=[],
        pillars=[(15, 21, 16, 22), (28, 21, 29, 22), (15, 25, 16, 26), (28, 25, 29, 26), (19, 4, 19, 4), (25, 7, 25, 7)],
        spawn=(4, 5),
        trees=[],
        enemies=[
            ("skeleton", (18, 6), "L2-Skeleton-01", [(0, 0), (0, 2)]),
            ("skeleton", (26, 4), "L2-Skeleton-02", [(0, 0), (2, 0)]),
            ("skeleton", (36, 9), "L2-Skeleton-03", [(0, 0), (-2, 0)]),
            ("archer", (39, 4), "L2-Archer-01", []),
            ("skeleton", (6, 18), "L2-Skeleton-04", [(0, 0), (2, 0)]),
            ("archer", (3, 13), "L2-Archer-02", []),
            ("boss", (22, 25), "L2-Boss", []),
            ("wisp", (17, 27), "L2-Wisp-1", []),
            ("wisp", (27, 27), "L2-Wisp-2", []),
        ],
        pickups=[("Coin", (10, 3)), ("Coin", (22, 8)), ("Coin", (34, 3)), ("Coin", (40, 11)), ("Coin", (4, 15)), ("Coin", (28, 13)),
                 ("Lamp", (39, 11)), ("Lamp", (3, 19)), ("Potion", (9, 19))],
        uses=[("Chest", (33, 11), "L2-Chest", dict(Prompt="Sandiqni ochish", Reward="Zirh")),
              ("Shrine", (22, 13), "L2-Shrine", dict(Prompt="Saqlash ziyoratgohi", MaxUses=0, Cooldown=2)),
              ("Altar", (22, 27), "L2-Altar", dict(Prompt="Tong chirog'ini yoqish", Required="Lamp", RequiredCount=3, Consume=True))],
        exit=None,
        plan=[((4, 5), 0), ((10, 3), 0), ((12, 5), 3), ((15, 5), 3), ((22, 8), 0), ((24, 5), 3), ((30, 5), 0), ((32, 5), 3),
              ((34, 3), 0), ((38, 6), 3), ((39, 11), 0), ((40, 11), 0), ((34, 11), 2), ((34, 10), 4), ((30, 5), 0), ((22, 9), 0),
              ((22, 12), 0), ((28, 13), 0), ((15, 14), 0), ((12, 14), 3), ((8, 16), 3), ((4, 15), 0), ((3, 19), 0), ((9, 19), 0),
              ((12, 14), 0), ((22, 13), 2), ((22, 15), 0), ((22, 18), 3), ((22, 21), 3), ((22, 26), 2), ((22, 25), 7)],
        ambient=((0.48, 0.52, 0.9), 0.2), music="music_yertola.wav", amb="amb_crypt.wav",
    ),
}

# Enemy archetypes: graph overrides (lantern_graphs.Enemy) and look.
ENEMIES = {
    "bandit": dict(HP=36, Armor=0, Damage=7, Cooldown=1.0, Speed=2.4, View=6.5, FOV=110, Leash=9, Enter=1.1, Exit=1.4, Ranged=False, tags=["Enemy", "Bandit"], scale=1.25),
    "chief": dict(HP=90, Armor=15, Damage=12, Cooldown=1.2, Speed=2.2, View=6.5, FOV=140, Leash=7, Enter=1.2, Exit=1.5, Ranged=False, tags=["Enemy", "Chief"], scale=1.45,
                  Loot="Key", Loot2="Tumor"),
    "archer": dict(HP=26, Armor=0, Damage=0, Cooldown=1.8, Speed=2.0, View=7, FOV=160, Leash=4, Enter=6, Exit=7.5, Ranged=True, Ability="Arrow", Amount=6,
                   tags=["Enemy", "Archer"], scale=1.25),
    "skeleton": dict(HP=48, Armor=20, Damage=9, Cooldown=1.1, Speed=2.2, View=6, FOV=120, Leash=8, Enter=1.1, Exit=1.4, Ranged=False, tags=["Enemy", "Skeleton"], scale=1.25),
    "wisp": dict(HP=18, Armor=0, Damage=0, Cooldown=1.6, Speed=3.0, View=9, FOV=360, Leash=12, Enter=5, Exit=6.5, Ranged=True, Ability="Spit", Amount=4,
                 tags=["Enemy", "Wisp"], scale=1.0, active=False),
    "boss": dict(HP=240, Armor=10, Damage=0, Cooldown=2.4, Speed=1.8, View=10, FOV=360, Leash=8, Enter=5.5, Exit=7, Ranged=True, Ability="Orb", Amount=7,
                 tags=["Enemy", "Boss"], scale=1.9),
}

ACTIONS = [("Attack", ["Key:J", "Mouse:Left", "Gamepad:X"]), ("Bolt", ["Key:K", "Mouse:Right", "Gamepad:RightBumper"]),
           ("Nova", ["Key:L", "Gamepad:Y"]), ("Mend", ["Key:H", "Gamepad:LeftBumper"]), ("Potion", ["Key:Q", "Gamepad:DPadDown"]),
           ("Interact", ["Key:E", "Gamepad:A"]), ("Sprint", ["Key:LeftShift", "GamepadAxis:LeftTrigger+"]),
           ("Inventory", ["Key:I", "Key:Tab", "Gamepad:Back"]), ("Pause", ["Key:Escape", "Key:P", "Gamepad:Start"])]
# (no Jump: the hero's Gameplay.Input reads none - every action here has a key and a gamepad control)


def cell(c):
    return (c[0] + 0.5, c[1] + 0.5)


# ------------------------------------------------------------------------------------------------ art


def make_art():
    os.makedirs(os.path.join(ASSETS, "Textures"), exist_ok=True)
    # characters + their clips
    for style in ("hero", "bandit", "archer", "chief", "skeleton", "wisp", "boss"):
        sk.character_atlas(style, ASSETS, f"Textures/{style}.png", PROJECT)
        tex = f"Textures/{style}.png"
        clips = {"Idle": ([f"{style}_idle_{i}" for i in range(2)], 2.5, True, [])}
        for d in ("down", "left", "right", "up"):
            clips[f"Walk{d.capitalize()}"] = ([f"{style}_walk_{d}_{i}" for i in range(sk.WALK_FRAMES)], 10, True, [(1, "Step"), (4, "Step")])
        clips["Attack"] = ([f"{style}_attack_{i}" for i in range(4)], 12, False, [(2, "Hit")])
        clips["Death"] = ([f"{style}_death_{i}" for i in range(4)], 8, False, [])
        for name, (frames, fps, loop, events) in clips.items():
            rel = f"Animations/{style}_{name}.zsanim"
            k.write_json(os.path.join(ASSETS, rel), sk.clip_json(tex, frames, fps, loop, events))
            k.write_zmeta(ASSETS, rel, "SpriteAnimation", {}, PROJECT)
    # tilesets: 8 x 2 tiles of 64 px
    def tileset(rel, tiles, cols, rows):
        assert len(tiles) <= cols * rows, rel
        img = k.image(cols * sk.TILE, rows * sk.TILE)
        for i, t in enumerate(tiles):
            k.blit(img, t, (i % cols) * sk.TILE, (i // cols) * sk.TILE)
        k.write_png(os.path.join(ASSETS, rel), img)
        k.write_zmeta(ASSETS, rel, "Texture", {"Filter": "Linear"}, PROJECT)
    # Boglar: 16 x 6 tiles - seamless grass (0-3), flowers (4), walls (5, 6); the path's 47-tile blob autotile (16 + its
    # index in slicekit.blob_masks), the water autotile (64 + mask); decals (80-89). Every grass variant shares the base
    # noise at its border: no seams.
    GRASS, GRASS_DARK, GRASS_LIGHT = (0.46, 0.55, 0.27), (0.24, 0.31, 0.13), (0.62, 0.70, 0.34)
    grass = [sk.seamless_ground(GRASS, GRASS_DARK, 10, 11 + i, ((0.70, 0.78, 0.40), 5), light=GRASS_LIGHT) for i in range(4)]
    for i, tile in enumerate(grass):
        sk._blades(tile, 200 + i, (0.30, 0.42, 0.16), 10)
    flowers = sk.seamless_ground(GRASS, GRASS_DARK, 10, 20, ((1.0, 0.70, 0.50), 12), light=GRASS_LIGHT)
    base_grass = sk.seamless_ground(GRASS, GRASS_DARK, 10, 10, light=GRASS_LIGHT, detail=0.0)
    path = sk.seamless_ground((0.64, 0.53, 0.40), (0.42, 0.33, 0.24), 30, 30, ((0.80, 0.70, 0.56), 0), cells=5, detail=0.0)
    wall = sk.paint_wall((0.74, 0.62, 0.46), (0.47, 0.36, 0.26), 40)
    wall2 = sk.paint_wall((0.66, 0.56, 0.42), (0.40, 0.31, 0.22), 41)
    tiles = grass + [flowers, wall, wall2, k.image(sk.TILE, sk.TILE)] + [k.image(sk.TILE, sk.TILE)] * 8
    tiles += [sk.path_autotile(m, base_grass, path, seed=i) for i, m in enumerate(sk.blob_masks())] + [k.image(sk.TILE, sk.TILE)]
    tiles += [sk.water_autotile(m, base_grass, seed=m) for m in range(16)]
    tiles += [sk.decal_petals(), sk.decal_clover(), sk.decal_pebbles(), sk.decal_leaves(), sk.decal_mushrooms(), sk.decal_tuft(), sk.decal_moss(),
              sk.decal_wildflowers(), sk.decal_wall_shadow(), sk.decal_pebbles(13, (0.50, 0.44, 0.36))]
    tileset("Textures/tiles_boglar.png", tiles, 16, 6)
    slabs = [sk.paint_ground((0.40, 0.39, 0.50), (0.20, 0.19, 0.28), 60 + i, ((0.55, 0.55, 0.7), 3), cells=2) for i in range(4)]
    for slab in slabs:
        xs, ys = k.coords(sk.TILE, sk.TILE, (0, 0))
        seam = (np.abs(xs - 1) < 1.2) | (np.abs(ys - 1) < 1.2)
        for c in range(3):
            slab[:, :, c] = np.where(seam, slab[:, :, c] * 0.55, slab[:, :, c])
    rune = sk.paint_ground((0.30, 0.30, 0.42), (0.14, 0.14, 0.22), 70, cells=2)
    x, y = k.coords(sk.TILE, sk.TILE)
    k.glow(rune, np.abs(k.sd_circle(x, y, 20)) - 1.5, (0.55, 0.75, 1.0, 1), 3, 0.35)
    k.stroke(rune, k.sd_polygon(x, y, k.regular_polygon(8, 14, math.pi / 8)), (0.6, 0.8, 1.0, 0.5), 1.4)
    dark = sk.paint_ground((0.32, 0.31, 0.40), (0.16, 0.15, 0.22), 71, cells=2)
    cwall = sk.paint_wall((0.38, 0.36, 0.50), (0.20, 0.18, 0.28), 72)
    inlay = sk.paint_ground((0.34, 0.30, 0.40), (0.16, 0.13, 0.20), 73, cells=2)
    k.stroke(inlay, k.sd_polygon(x, y, k.regular_polygon(6, 24, 0)), (0.85, 0.65, 1.0, 0.5), 2)
    # Yertola: slabs (0-3), rune (4), dark slab (5), wall (6, 7), inlay (8); decals (9-15): cracks, rubble, moss, a
    # puddle, a cobweb, the wall's shadow
    decals = [sk.decal_crack(9), sk.decal_crack(19), sk.decal_rubble(), sk.decal_moss(17, (0.20, 0.34, 0.30)), sk.decal_puddle(), sk.decal_cobweb(),
              sk.decal_wall_shadow(0.65)]
    tileset("Textures/tiles_yertola.png", slabs + [rune, dark, cwall, cwall, inlay] + decals, 8, 2)
    # props atlas
    props = k.Atlas(1024, 512)
    for i in range(3):
        props.add(f"tree_{i}", sk.paint_tree(80 + i), border=(0, 0, 0, 0))
    def prop(name, painter, size=64):
        img = k.image(size, size)
        painter(img, *k.coords(size, size))
        props.add(name, sk.outline(img, (0.06, 0.04, 0.08), 2), border=(0, 0, 0, 0))
    def chest(img, x, y):
        sk._shade(img, k.sd_round_box(x, y + 4, 24, 16, 4), (0.62, 0.40, 0.20), (0.36, 0.22, 0.10), y, -12, 20)
        sk._shade(img, k.sd_round_box(x, y - 10, 24, 8, 6), (0.75, 0.50, 0.26), (0.50, 0.32, 0.16), y, -18, -2)
        k.fill(img, k.sd_round_box(x, y - 2, 25, 2, 1), (0.95, 0.78, 0.35, 1))
        k.fill(img, k.sd_round_box(x, y + 1, 4, 5, 1.5), (0.95, 0.78, 0.35, 1))
    prop("chest", chest)
    def shrine(img, x, y):
        k.fill(img, k.sd_round_box(x, y + 18, 24, 7, 3), (0.5, 0.48, 0.55, 1))
        sk._shade(img, k.sd_round_box(x, y, 12, 20, 5), (0.80, 0.78, 0.86), (0.48, 0.46, 0.56), y, -20, 20)
        gem = k.sd_polygon(x, y, k.regular_polygon(4, 8, 0, 0, -4))
        k.glow(img, gem, (0.4, 0.9, 1.0, 1), 8, 1.0)
        k.fill(img, gem, (0.7, 1.0, 1.0, 1))
    prop("shrine", shrine)
    def gate(img, x, y):
        for bx in (-18, -6, 6, 18):
            sk._shade(img, k.sd_round_box(x - bx, y, 3, 30, 1.5), (0.45, 0.42, 0.40), (0.22, 0.20, 0.20), y, -30, 30)
        for by in (-16, 0, 16):
            k.fill(img, k.sd_round_box(x, y - by, 24, 2.5, 1), (0.30, 0.28, 0.28, 1))
        k.fill(img, k.sd_round_box(x, y, 5, 6, 2), (0.95, 0.75, 0.30, 1))
    prop("gate", gate)
    def altar(img, x, y):
        sk._shade(img, k.sd_round_box(x, y + 10, 28, 12, 4), (0.55, 0.45, 0.65), (0.25, 0.18, 0.32), y, -2, 22)
        sk._shade(img, k.sd_round_box(x, y - 4, 18, 8, 3), (0.70, 0.60, 0.80), (0.40, 0.32, 0.50), y, -12, 4)
        bowl = k.sd_circle(x, y - 16, 9)
        k.glow(img, bowl, (1.0, 0.7, 0.3, 1), 7, 0.5)
        sk._shade(img, bowl, (0.95, 0.80, 0.45), (0.60, 0.40, 0.15), y, -25, -7)
    prop("altar", altar)
    def exit_(img, x, y):
        for i in range(5):
            k.fill(img, k.sd_round_box(x, y - 18 + i * 9, 24 - i * 3, 4, 1.5), (0.40 - i * 0.06, 0.34 - i * 0.05, 0.30 - i * 0.05, 1))
        k.glow(img, k.sd_round_box(x, y + 18, 12, 3, 1), (0.4, 0.5, 1.0, 1), 6, 0.7)
    prop("stairs", exit_)
    def tent(img, x, y):
        sk._shade(img, k.sd_polygon(x, y, [(-28, 22), (28, 22), (0, -26)]), (0.80, 0.30, 0.22), (0.45, 0.14, 0.10), y, -26, 22)
        k.fill(img, k.sd_polygon(x, y, [(-6, 22), (6, 22), (0, 2)]), (0.18, 0.08, 0.06, 1))
    prop("tent", tent)
    def fire(img, x, y):
        for a in range(5):
            ang = a * 2 * math.pi / 5
            k.fill(img, sk._capsule(x, y, math.cos(ang) * 14, math.sin(ang) * 8 + 8, -math.cos(ang) * 4, -math.sin(ang) * 2 + 8, 3), (0.40, 0.25, 0.14, 1))
        for r in range(7):
            k.fill(img, k.sd_circle(x - math.cos(r) * 18, y - 10 - math.sin(r) * 6, 3), (0.45, 0.45, 0.48, 1))
    prop("campfire", fire)
    def torch(img, x, y):
        k.fill(img, k.sd_round_box(x, y + 6, 3, 18, 1), (0.36, 0.24, 0.14, 1))
        k.fill(img, k.sd_round_box(x, y - 12, 7, 4, 2), (0.30, 0.28, 0.30, 1))
    prop("torch", torch)
    def pillar(img, x, y):
        sk._shade(img, k.sd_round_box(x, y, 22, 28, 6), (0.52, 0.48, 0.64), (0.22, 0.20, 0.30), y, -28, 28)
        k.stroke(img, k.sd_round_box(x, y, 16, 22, 4), (0.75, 0.6, 1.0, 0.45), 2)
    prop("pillar", pillar)
    def bones(img, x, y):
        for a in (0.3, 1.9, 3.1):
            k.fill(img, sk._capsule(x, y, math.cos(a) * 14, math.sin(a) * 8, -math.cos(a) * 14, -math.sin(a) * 8, 2.5), (0.85, 0.82, 0.74, 1))
        k.fill(img, k.sd_circle(x + 8, y - 6, 7), (0.88, 0.86, 0.78, 1))
    prop("bones", bones)
    def bush(img, x, y):
        sk._shade(img, np.minimum(k.sd_circle(x - 8, y, 14), k.sd_circle(x + 9, y + 2, 12)), (0.40, 0.58, 0.28), (0.18, 0.30, 0.12), y, -14, 14)
    prop("bush", bush)
    for kind in ("coin", "lamp", "herb", "key", "potion"):
        props.add(f"item_{kind}", sk.paint_icon(kind), border=(0, 0, 0, 0))
    props.add("glow", sk.glow_disc(64, (1.0, 0.8, 0.45)), border=(0, 0, 0, 0))
    props.add("ring", _ring(128), border=(0, 0, 0, 0))
    props.add("bolt", sk.paint_icon("bolt"), border=(0, 0, 0, 0))
    props.add("orb", _orb(), border=(0, 0, 0, 0))
    props.add("arrow", _arrow(), border=(0, 0, 0, 0))
    props.save(ASSETS, "Textures/props.png", PROJECT)
    # UI atlas: frames + icons
    ui = k.Atlas(1024, 512)
    sk.ui_frames(ui)
    for kind in ("coin", "lamp", "herb", "key", "potion", "bolt", "nova", "mend", "sword", "charm", "armor", "melee"):
        ui.add(f"icon_{kind}", sk.paint_icon(kind), border=(0, 0, 0, 0))
    ui.add("vignette", _vignette(128), border=(40, 40, 40, 40))
    ui.save(ASSETS, "Textures/ui.png", PROJECT)
    # the game icon
    icon = k.image(128, 128)
    xi, yi = k.coords(128, 128)
    sk._shade(icon, k.sd_round_box(xi, yi, 60, 60, 24), (0.20, 0.16, 0.32), (0.06, 0.05, 0.10), yi, -60, 60)
    lamp = sk.paint_icon("lamp", 96)
    region = icon[16:112, 16:112]
    a = lamp[:, :, 3:4]
    region[:, :, :3] = lamp[:, :, :3] * a + region[:, :, :3] * (1 - a)
    region[:, :, 3:4] = np.maximum(region[:, :, 3:4], a)
    k.write_png(os.path.join(ASSETS, "Textures/GameIcon.png"), icon)
    k.write_zmeta(ASSETS, "Textures/GameIcon.png", "Texture", {}, PROJECT)


def _ring(size):
    img = k.image(size, size)
    x, y = k.coords(size, size)
    d = np.abs(k.sd_circle(x, y, size * 0.42)) - 3
    k.glow(img, d, (0.75, 0.45, 1.0, 1), 6, 1.0)
    k.fill(img, d, (0.95, 0.80, 1.0, 1))
    return img


def _orb():
    img = k.image(48, 48)
    x, y = k.coords(48, 48)
    d = k.sd_circle(x, y, 9)
    k.glow(img, d, (0.6, 0.25, 0.95, 1), 9, 1.0)
    k.fill(img, d, (0.85, 0.65, 1.0, 1))
    k.fill(img, k.sd_circle(x - 2, y - 2, 3), (1, 1, 1, 0.8))
    return img


def _arrow():
    img = k.image(64, 16)
    x, y = k.coords(64, 16)
    k.fill(img, sk._capsule(x, y, -24, 0, 20, 0, 1.2), (0.55, 0.40, 0.22, 1))
    k.fill(img, k.sd_polygon(x, y, [(18, -5), (30, 0), (18, 5)]), (0.85, 0.86, 0.9, 1))
    k.fill(img, k.sd_polygon(x, y, [(-28, -5), (-20, 0), (-28, 5), (-24, 0)]), (0.9, 0.3, 0.25, 1))
    return img


def _vignette(size):
    img = k.image(size, size)
    x, y = k.coords(size, size)
    r = np.maximum(np.abs(x), np.abs(y)) / (size / 2)
    img[:, :, :3] = (0.55, 0.0, 0.02)
    img[:, :, 3] = np.clip((r - 0.55) / 0.45, 0, 1) ** 1.5
    return img


# ------------------------------------------------------------------------------------------------ audio


def make_audio():
    rel = lambda name: os.path.join(ASSETS, "Audio", name)
    k.write_wav(rel("music_menu.wav"), sk.music_loop(16.0, k.note_freq("D3"), "hijaz", 84, 11, 0.45, 0.3, drums=False))
    k.write_wav(rel("music_boglar.wav"), sk.music_loop(16.0, k.note_freq("A2"), "dorian", 100, 12, 0.55, 0.22))
    k.write_wav(rel("music_yertola.wav"), sk.music_loop(16.0, k.note_freq("D2"), "minor", 76, 13, 0.35, 0.35, dark=True))
    k.write_wav(rel("music_boss.wav"), sk.music_loop(12.0, k.note_freq("C2"), "hijaz", 124, 14, 0.6, 0.3, dark=True))
    t = np.arange(int(8 * k.RATE)) / k.RATE
    wind = k.lowpass(k.noise(8, 21), 500) * (0.6 + 0.4 * np.sin(2 * np.pi * 0.11 * t))
    crickets = np.zeros_like(t)
    for i in range(40):
        at = (i * 0.21 + (i * 37 % 11) * 0.013) % 8
        k.place(crickets, k.osc(4200, 0.05, "sine") * np.exp(-np.arange(int(0.05 * k.RATE)) / k.RATE * 60), at, 0.15)
    k.write_wav(rel("amb_orchard.wav"), wind * 0.6 + crickets, peak=0.5)
    drips = np.zeros_like(t)
    for i in range(9):
        k.place(drips, sk.pluck(1800 + (i * 337) % 900, 0.4, 0.2, i), (i * 0.93) % 8, 0.5)
    k.write_wav(rel("amb_crypt.wav"), sk.drone(55, 8, 3) * 0.3 + drips + k.lowpass(k.noise(8, 22), 200) * 0.3, peak=0.5)
    for kind in ("swing", "hit", "bolt", "coin", "lamp", "chest", "death", "hurt", "levelup", "victory", "defeat", "nova"):
        k.write_wav(rel(f"sfx_{kind}.wav"), sk.sfx(kind, 5))
    for name in os.listdir(os.path.join(ASSETS, "Audio")):
        if name.endswith(".wav"):
            k.write_zmeta(ASSETS, f"Audio/{name}", "Audio", {}, PROJECT)


# ------------------------------------------------------------------------------------------------ localization

TEXT = {
    "menu.new": ("Yangi o'yin", "New game"), "menu.continue": ("Davom etish", "Continue"), "menu.settings": ("Sozlamalar", "Settings"),
    "menu.credits": ("Mualliflar", "Credits"), "menu.quit": ("Chiqish", "Quit"), "menu.back": ("Orqaga", "Back"),
    "menu.subtitle": ("Ko'hna qo'rg'on afsonasi", "A legend of the old fortress"),
    "menu.music": ("Musiqa", "Music"), "menu.sfx": ("Ovoz effektlari", "Sound effects"), "menu.fullscreen": ("To'liq ekran", "Fullscreen"), "menu.uiscale": ("Interfeys o'lchami", "Interface size"),
    "menu.language": ("Til: O'zbekcha", "Language: English"),
    "menu.credits.body": ("Zamin Engine vertical slice\nBarcha grafika, musiqa va ovozlar protsedurali generatorlardan:\nTools/samples/gen_lantern_keeper.py, slicekit.py, graphkit.py\n\nRahmat, chiroqbon!",
                          "Zamin Engine vertical slice\nAll art, music and sound come from procedural generators:\nTools/samples/gen_lantern_keeper.py, slicekit.py, graphkit.py\n\nThank you, lantern keeper!"),
    "pause.title": ("Pauza", "Paused"), "pause.resume": ("Davom etish", "Resume"), "pause.bag": ("Inventar", "Inventory"),
    "pause.savequit": ("Saqlab, bosh menyuga", "Save and quit to menu"),
    "bag.title": ("Inventar", "Inventory"), "bag.equip": ("Kiyish", "Equip"), "bag.drink": ("Ichish", "Drink"), "bag.close": ("Yopish", "Close"),
    "bag.next": ("Keyingi", "Next"),
    "defeat.title": ("CHIROQ SO'NDI", "THE LANTERN WENT OUT"), "defeat.retry": ("Oxirgi saqlashdan", "From the last save"),
    "victory.title": ("TONG OTDI", "DAWN HAS COME"), "victory.sub": ("Qo'rg'on yana yorug'", "The fortress shines again"),
    "end.menu": ("Bosh menyu", "Main menu"), "card.title": ("1-DARAJA YAKUNLANDI", "LEVEL 1 COMPLETE"), "card.sub": ("Bog'lar ozod", "The orchards are free"),
    "hud.quests": ("Vazifalar", "Quests"), "hud.boss": ("Soya Bek", "Soya Bek"),
    "controls": ("[Move] yurish   [Attack] zarba   [Bolt] olov o'qi   [Nova] to'lqin   [Mend] tiklash   [Potion] ichimlik   [Interact] ishlatish   [Inventory] inventar   [Pause] pauza",
                 "[Move] move   [Attack] strike   [Bolt] fire bolt   [Nova] wave   [Mend] mend   [Potion] potion   [Interact] use   [Inventory] bag   [Pause] pause"),
}


# What the HUD names by key (the engine looks up "item.<Key>", "combat.<Key>" and "quest.<Quest>[.<Objective>]"; a key
# without a string shows as it is): the bag's items, the combat feed's payloads, the quest book.
ITEM_NAMES = {"Coin": ("Tanga", "Coin"), "Lamp": ("Chiroq", "Lamp"), "Key": ("Kalit", "Key"), "Herb": ("Shifobaxsh o't", "Healing herb"),
              "Potion": ("Ichimlik", "Potion"), "Qilich": ("Qilich", "Sword"), "Tumor": ("Tumor", "Charm"), "Zirh": ("Zirh", "Armour")}
COMBAT_NAMES = {"Bolt": ("Olov o'qi", "Fire bolt"), "Burn": ("Kuyish", "Burn"), "Arrow": ("O'q", "Arrow"), "Poison": ("Zahar", "Poison"),
                "Orb": ("Soya shari", "Shadow orb"), "Spit": ("Ruh zahri", "Spirit venom"), "Nova": ("Yorug' to'lqin", "Light wave"),
                "Ring": ("Soya halqasi", "Shadow ring"), "Shock": ("Karaxt", "Stun"), "Regen": ("Tiklanish", "Mend"), "Mend": ("Tiklanish", "Mend"),
                "Potion": ("Ichimlik", "Potion")}
QUEST_EN = {"Gate": ("The orchard gate", {"Chief": "Defeat the bandit chief", "Gate": "Open the gate"}),
            "Herbs": ("Healing herbs", {"Herb": "Healing herb"}), "Lamps": ("Three lamps", {"Lamp": "Lamps of Dawn"}),
            "Boss": ("Soya Bek", {"Boss": "Defeat Soya Bek"}), "Dawn": ("The Dawn Lamp", {"Altar": "Light the lamp on the altar"})}


def make_localization():
    """uz.json / en.json: the scene texts (TEXT), the strings the graphs build (lantern_graphs.TEXT, filled by build), the
    HUD's item / combat / quest names and the control names. An empty text is left out (the fallback is "")."""
    pairs = dict(TEXT)
    pairs.update(lg.TEXT)
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


# ------------------------------------------------------------------------------------------------ scenes


def main():
    os.makedirs(ASSETS, exist_ok=True)
    make_art()
    make_audio()
    oracle = {"Schema": C["Schema"], "XP": C["XP"], "FinalLevel": C["FinalLevel"], "Quests": [q[0] for q in C["Quests"]], "Levels": {}}
    routes = {}
    for number, level in LEVELS.items():
        w, h = level["size"]
        g = grid(w, h, level["rooms"], level["water"], level["pillars"])
        walkable = [[g[y][x] == "." for x in range(w)] for y in range(h)]
        for c in level["trees"]:
            walkable[c[1]][c[0]] = False
        for kind, c, *_ in level["uses"]:
            if kind == "Gate":
                walkable[c[1]][c[0]] = True
        points = [p[0] for p in level["plan"]]
        bad = [p for p in points if not walkable[p[1]][p[0]]]
        assert not bad, f"level {number}: plan points on walls/trees {bad}"
        codes = {p[0]: p[1] for p in level["plan"]}
        steps = sk.route(walkable, points)
        route = [cell(points[0])] + [cell(c) for c, _ in steps]
        kinds = [0] + [codes.get(points[i], 0) if i >= 0 else 0 for _, i in steps]
        routes[number] = (route, kinds, g, walkable)
        coins = sum(1 for item, _ in level["pickups"] if item == "Coin")
        oracle["Levels"][str(number)] = {"Scene": level["scene"], "Coins": coins, "Enemies": len(level["enemies"]), "Waypoints": len(route)}
    oracle["Coins"] = sum(v["Coins"] for v in oracle["Levels"].values()) + C["Quests"][0][4]["Coin"]
    lg.build(ASSETS, PROJECT, C, LEVELS, ENEMIES, routes, oracle)
    make_localization()  # after the graphs: they register the strings they build
    k.write_json(os.path.join(OUT, "Tests", "oracle.json"), oracle)
    actions = list(ACTIONS)
    k.write_project(OUT, NAME, "Chiroqbon", "Scenes/Menu.zscene", lg.scene_uuid(PROJECT, "Menu"), version="1.0.0",
                    build_scenes=[(f"Scenes/{s}.zscene", lg.scene_uuid(PROJECT, s)) for s in ("Menu", "Boglar", "Yertola")],
                    actions=actions,
                    axes=[{"Deadzone": 0.15, "GamepadAxis": "LeftX", "Invert": False, "Name": "Horizontal", "Negative": ["Key:A", "Key:Left", "Gamepad:DPadLeft"],
                           "Positive": ["Key:D", "Key:Right", "Gamepad:DPadRight"]},
                          {"Deadzone": 0.15, "GamepadAxis": "LeftY", "Invert": True, "Name": "Vertical", "Negative": ["Key:S", "Key:Down"],
                           "Positive": ["Key:W", "Key:Up", "Gamepad:DPadUp"]}],
                    vectors=[{"Deadzone": 0.0, "Mode": "Clamp", "Name": "Move", "X": "Horizontal", "Y": "Vertical"}],
                    tags_=("Hero", "Enemy", "Bandit", "Chief", "Archer", "Skeleton", "Wisp", "Boss", "Pickup", "Shrine", "Gate", "Altar", "Chest"),
                    layers={"names": ["Default", "Wall", "Hero", "Enemy"],
                            "matrix": k.collision_matrix(["Default", "Wall", "Hero", "Enemy"], [("Default", "Default"), ("Default", "Wall"), ("Default", "Hero"), ("Default", "Enemy"),
                                                                                             ("Wall", "Hero"), ("Wall", "Enemy"), ("Hero", "Enemy"), ("Enemy", "Enemy")])},
                    extra=k.export_always_include(["Audio/*.wav", "Localization/*.json"]),
                    description="Chiroqbon: a 2D action-adventure vertical slice - two levels, melee and magic, AI enemies, quests, equipment, saves and a boss.")
    manifest = json.load(open(os.path.join(OUT, f"{NAME}.zproject")))
    manifest["Project"]["ScriptModulePath"] = ""
    k.write_json(os.path.join(OUT, f"{NAME}.zproject"), manifest)


if __name__ == "__main__":
    main()
