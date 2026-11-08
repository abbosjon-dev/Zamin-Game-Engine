"""Projects/EndlessRunner: "Ipak Yo'li" (an endless runner on the Silk Road) - its textures, sounds, scene and project.

    python3 Tools/samples/gen_endlessrunner.py

Everything under Projects/EndlessRunner/Assets and the .zproject are made here; the runner, the procedural world
and the game flow are C++ (Projects/EndlessRunner/Source).
"""

import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import samplekit as k  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NAME = "EndlessRunner"
PROJECT = os.path.join(ROOT, "Projects", NAME)
ASSETS = os.path.join(PROJECT, "Assets")
ATLAS = "Textures/ui_atlas.png"
ROAD_W, ROAD_L, ROAD_TILE = 8.4, 300.0, 8.0
SAND_W, SAND_L, SAND_TILE = 400.0, 300.0, 10.0

# ------------------------------------------------------------------------------------------- textures


def save_texture(rel, img):
    k.write_png(os.path.join(ASSETS, rel), np.clip(img, 0, 1))
    k.write_zmeta(ASSETS, rel, "Texture", {}, NAME)


def make_textures():
    s = 256
    ys, xs = np.mgrid[0:s, 0:s].astype(np.float64)
    u, vv = (xs + 0.5) / s, (ys + 0.5) / s

    # road: sandstone pavers, the lane lines (dashed, golden) at the lane borders; v runs along the road (8 m)
    n = k.value_noise(s, s, 8, seed=3, octaves=5)
    rows = (ys // 32).astype(int)
    bx = (xs + (rows % 2) * 24) % 48
    by = ys % 32
    joint = np.maximum(k.smooth(2.5, 0.5, np.minimum(bx, 48 - bx)), k.smooth(2.5, 0.5, np.minimum(by, 32 - by)))
    stone = ((rows * 5 + ((xs + (rows % 2) * 24) // 48).astype(int) * 3) % 4) / 4.0
    img = k.image(s, s, (0, 0, 0, 1))
    base = np.array([0.78, 0.62, 0.44])
    for i in range(3):
        img[:, :, i] = base[i] * (0.78 + 0.16 * stone + 0.2 * n) * (1 - 0.45 * joint)
    lanes = 1.15 / ROAD_W
    for cu in (0.5 - lanes, 0.5 + lanes):
        dash = k.smooth(0.006, 0.002, np.abs(u - cu)) * (vv % 1.0 < 0.55)
        for i, c in enumerate((1.0, 0.9, 0.6)):
            img[:, :, i] = img[:, :, i] * (1 - dash) + c * dash
    edge = k.smooth(0.012, 0.004, np.minimum(u, 1 - u) - 0.012)
    for i, c in enumerate((0.45, 0.2, 0.12)):
        img[:, :, i] = img[:, :, i] * (1 - edge) + c * edge
    save_texture("Textures/road.png", img)
    save_texture("Textures/road_normal.png", k.normal_map_from_height(0.8 - 0.6 * joint + 0.1 * n, 2.5))

    # sand: soft ripples and grain
    n = k.value_noise(s, s, 6, seed=5, octaves=5)
    n2 = k.value_noise(s, s, 3, seed=6, octaves=2)
    ripple = np.sin((xs * 0.35 + ys * 0.9) / 6.0 + n2 * 9.0) * 0.5 + 0.5
    img = k.image(s, s, (0, 0, 0, 1))
    for i, c in enumerate((0.93, 0.74, 0.5)):
        img[:, :, i] = c * (0.82 + 0.1 * ripple + 0.12 * n)
    save_texture("Textures/sand.png", img)

    # mud brick (walls, houses, towers)
    n = k.value_noise(s, s, 8, seed=7, octaves=4)
    rows = (ys // 21).astype(int)
    bx = (xs + (rows % 2) * 32) % 64
    by = ys % 21
    mortar = np.maximum(k.smooth(2.5, 0.8, np.minimum(bx, 64 - bx)), k.smooth(2.5, 0.8, np.minimum(by, 21 - by)))
    img = k.image(s, s, (0, 0, 0, 1))
    for i, (c, m) in enumerate(((0.8, 0.62), (0.6, 0.5), (0.4, 0.38))):
        img[:, :, i] = c * (0.8 + 0.25 * n) * (1 - mortar) + m * mortar
    save_texture("Textures/brick.png", img)
    save_texture("Textures/brick_normal.png", k.normal_map_from_height(0.8 - 0.6 * mortar + 0.1 * n, 3.0))

    # blue majolica tiles: an eight-pointed star in each tile
    x, y = ((xs % 64) - 31.5), ((ys % 64) - 31.5)
    r = np.hypot(x, y)
    ang = np.arctan2(y, x)
    star = r < 18 + 7 * np.cos(ang * 8)
    ring = np.abs(r - 26) < 2.0
    grout = (np.minimum(np.abs(x), np.abs(y)) > 30.5)
    img = k.image(s, s, (0, 0, 0, 1))
    deep = np.array([0.08, 0.3, 0.7])
    turq = np.array([0.15, 0.65, 0.8])
    white = np.array([0.92, 0.92, 0.88])
    for i in range(3):
        img[:, :, i] = np.where(star, turq[i], deep[i])
        img[:, :, i] = np.where(ring, white[i], img[:, :, i])
        img[:, :, i] = np.where(grout, 0.85, img[:, :, i])
    save_texture("Textures/tiles.png", img)

    # ikat carpet (the gate's hanging carpet): wavy bands of red, gold, indigo and white
    n = k.value_noise(s, s, 12, seed=9, octaves=2)
    wave = (ys + 18 * np.sin(xs / 20.0) + 6 * n) % 64
    cols = [(0.75, 0.1, 0.12), (0.95, 0.75, 0.2), (0.15, 0.12, 0.45), (0.95, 0.92, 0.85)]
    band = (wave // 16).astype(int)
    img = k.image(s, s, (0, 0, 0, 1))
    for b, c in enumerate(cols):
        m = band == b
        for i in range(3):
            img[:, :, i] = np.where(m, c[i] * (0.85 + 0.15 * n), img[:, :, i])
    diamond = (np.abs(((xs % 32) - 16)) + np.abs(((ys % 32) - 16))) < 6
    for i in range(3):
        img[:, :, i] = np.where(diamond, (0.98, 0.95, 0.85)[i], img[:, :, i])
    border = (np.minimum(xs, s - 1 - xs) < 10) | (np.minimum(ys, s - 1 - ys) < 10)
    for i in range(3):
        img[:, :, i] = np.where(border, (0.55, 0.08, 0.1)[i], img[:, :, i])
    save_texture("Textures/carpet.png", img)

    # wood (carts, fences)
    grain = k.value_noise(s, s, 24, seed=11, octaves=2)
    n = k.value_noise(s, s, 6, seed=12, octaves=4)
    plank = (xs // 32).astype(int)
    stripes = np.sin(ys / 5.0 + grain * 7 + plank) * 0.5 + 0.5
    gap = k.smooth(2.0, 0.5, np.minimum(xs % 32, 32 - xs % 32))
    img = k.image(s, s, (0, 0, 0, 1))
    for i, c in enumerate((0.55, 0.36, 0.2)):
        img[:, :, i] = c * (0.75 + 0.2 * stripes + 0.2 * n) * (1 - 0.6 * gap)
    save_texture("Textures/wood.png", img)

    # canvas (the caravan's cover): off-white with red stripes
    n = k.value_noise(s, s, 16, seed=13, octaves=3)
    stripe = (xs % 64) < 12
    img = k.image(s, s, (0, 0, 0, 1))
    for i in range(3):
        img[:, :, i] = np.where(stripe, (0.7, 0.18, 0.12)[i], (0.9, 0.85, 0.74)[i]) * (0.85 + 0.15 * n)
    save_texture("Textures/canvas.png", img)


def make_ui_atlas():
    atlas = k.Atlas(512, 256)
    k.ui_panel_sprites(atlas, (0.16, 0.1, 0.07), (0.07, 0.04, 0.03), (1.0, 0.75, 0.3), (1.0, 0.65, 0.25))
    return atlas.save(ASSETS, ATLAS, NAME)


def make_icon():
    size = 256
    img = k.image(size, size)
    x, y = k.coords(size, size)
    sd = k.sd_round_box(x, y, 120, 120, 44)
    bg = k.mix_color((1.0, 0.72, 0.35), (0.85, 0.35, 0.15), np.clip(y / size + 0.5, 0, 1))
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = bg[:, :, i]
    img[:, :, 3] = cov
    # a road to a dome on the horizon
    road = k.sd_polygon(x, y, [(-20, -10), (20, -10), (90, 120), (-90, 120)])
    k.fill(img, road, (0.95, 0.85, 0.65, 1.0))
    k.fill(img, k.sd_round_box(x, y - 60, 3, 50, 2), (1.0, 0.75, 0.3, 1.0))
    k.fill(img, k.sd_circle(x, y + 30, 46), (0.12, 0.45, 0.8, 1.0))
    k.fill(img, k.sd_round_box(x, y + 2, 52, 16, 2), (0.85, 0.65, 0.45, 1.0))
    k.fill(img, k.sd_circle(x - 60, y + 70, 18), (1.0, 0.95, 0.7, 1.0))
    img[:, :, 3] = np.minimum(img[:, :, 3], cov)
    save_texture("Textures/GameIcon.png", img)

# --------------------------------------------------------------------------------------------- sounds


def make_sounds():
    R = k.RATE
    out = {}
    buf = np.zeros(int(0.35 * R))
    for i, note in enumerate(("B5", "E6")):
        f = k.note_freq(note)
        k.place(buf, (k.osc(f, 0.3, "square") * 0.35 + k.osc(f * 2, 0.3, "sine") * 0.3) * k.exp_decay(int(0.3 * R), 0.07), i * 0.05, 0.6)
    out["coin"] = k.lowpass(buf, 7000)
    n = int(0.3 * R)
    out["jump"] = k.osc(np.linspace(260, 640, n), 0.3, "tri") * np.sin(np.linspace(0, np.pi, n)) * 0.6 + \
        k.lowpass(k.noise(0.3, 1), 2000) * k.exp_decay(n, 0.05) * 0.3
    n = int(0.4 * R)
    out["slide"] = k.highpass(k.lowpass(k.noise(0.4, 2), np.linspace(3500, 800, n)), 300) * np.sin(np.linspace(0, np.pi, n)) * 0.8
    n = int(0.18 * R)
    out["land"] = k.lowpass(k.noise(0.18, 3), 500) * k.exp_decay(n, 0.04) + k.osc(np.linspace(110, 60, n), 0.18, "sine") * k.exp_decay(n, 0.05) * 0.6
    n = int(0.25 * R)
    out["whoosh"] = k.lowpass(k.noise(0.25, 4), np.linspace(800, 4000, n)) * np.sin(np.linspace(0, np.pi, n)) ** 2 * 0.7
    n = int(1.2 * R)
    crash = k.lowpass(k.noise(1.2, 5), np.linspace(4000, 200, n)) * k.exp_decay(n, 0.3) * 1.3
    out["crash"] = crash + k.osc(np.linspace(120, 35, n), 1.2, "sine") * k.exp_decay(n, 0.25) + \
        k.highpass(k.noise(1.2, 6), 2500) * k.exp_decay(n, 0.08) * 0.6
    buf = np.zeros(int(0.8 * R))
    for i, note in enumerate(("C6", "E6", "G6", "C7")):
        k.place(buf, k.osc(k.note_freq(note), 0.3, "square") * k.exp_decay(int(0.3 * R), 0.1), i * 0.07, 0.35)
    out["powerup"] = k.lowpass(buf, 6000)
    n = int(0.7 * R)
    out["shield_break"] = k.highpass(k.noise(0.7, 7), 3000) * k.exp_decay(n, 0.12) + \
        sum(k.osc(f, 0.7, "sine") * k.exp_decay(n, 0.2) * 0.25 for f in (1800, 2750, 3900))
    buf = np.zeros(int(1.1 * R))
    for i, note in enumerate(("G5", "C6", "E6", "G6")):
        tone = k.osc(k.note_freq(note), 0.5, "tri") + 0.3 * k.osc(k.note_freq(note) * 2, 0.5, "sine")
        k.place(buf, tone * k.exp_decay(int(0.5 * R), 0.15), i * 0.09, 0.4)
    out["milestone"] = buf
    n = int(0.06 * R)
    out["ui_click"] = k.osc(1100, 0.06, "square") * k.exp_decay(n, 0.012) * 0.6 + k.osc(2200, 0.06, "sine") * k.exp_decay(n, 0.006) * 0.4
    for name, snd in out.items():
        rel = f"Audio/{name}.wav"
        k.write_wav(os.path.join(ASSETS, rel), snd)
        k.write_zmeta(ASSETS, rel, "Audio", {"LoadMode": "Decompress", "Preload": True, "Volume": 1.0}, NAME)

    # music: 120 bpm, 16 s - a frame drum (doira: low "dum", high "tak") and a plucked pentatonic tune over a drone
    seconds = 16.0
    total = int(seconds * R)
    buf = np.zeros(total + R)
    beat = 0.5
    dum = k.osc(np.linspace(95, 55, int(0.3 * R)), 0.3, "sine") * k.exp_decay(int(0.3 * R), 0.09)
    tak = k.highpass(k.noise(0.12, 21), 1800) * k.exp_decay(int(0.12 * R), 0.02) * 0.7
    pattern = "D.tDt.t."  # eighth notes: D dum, t tak
    for bar in range(int(seconds / (beat * 4))):
        for i, ch in enumerate(pattern):
            at = bar * beat * 4 + i * beat / 2
            if ch == "D":
                k.place(buf, dum, at, 0.8)
            elif ch == "t":
                k.place(buf, tak, at, 0.5)
    tune = ["A4", "C5", "D5", "E5", "D5", "C5", "A4", "G4", "A4", "C5", "E5", "G5", "E5", "D5", "C5", "A4"]
    for i, note in enumerate(tune * 2):
        f = k.note_freq(note)
        pluck = (k.osc(f, 0.45, "saw") * 0.4 + k.osc(f * 2, 0.45, "tri") * 0.3) * k.exp_decay(int(0.45 * R), 0.12)
        k.place(buf, k.lowpass(pluck, 3500), i * beat, 0.32)
    drone = (k.osc(k.note_freq("A2"), seconds, "saw") + k.osc(k.note_freq("E3"), seconds, "saw") * 0.6)
    k.place(buf, k.lowpass(drone, 400) * 0.1, 0.0)
    buf[:R] += buf[total:total + R]
    rel = "Audio/music.wav"
    k.write_wav(os.path.join(ASSETS, rel), buf[:total], peak=0.6)
    k.write_zmeta(ASSETS, rel, "Audio", {"LoadMode": "Stream", "Preload": False, "Volume": 1.0}, NAME)

# ---------------------------------------------------------------------------------------------- scene

WOOD = dict(albedo_map="Textures/wood.png")
BRICK = dict(albedo_map="Textures/brick.png", normal_map="Textures/brick_normal.png")
TILES = dict(albedo_map="Textures/tiles.png")


def build_scene():
    s = k.SceneBuilder("SilkRoad", 8850000000000000)
    s.settings = {
        "DrawSky": True, "SkyTop": [0.22, 0.42, 0.78], "SkyHorizon": [1.0, 0.76, 0.5], "Ground": [0.75, 0.58, 0.4],
        "Ambient": 0.5, "Exposure": 0.92, "ShadowDistance": 70,
        "Sky": {"Curve": 1.0, "GroundFade": 1.0, "SunDisc": True, "SunSize": 1.4, "SunIntensity": 1.2, "SunGlow": 1.2},
        "Fog": {"Enabled": True, "Color": [0.96, 0.76, 0.56], "Density": 0.0075, "HeightFalloff": 0.05},
        "Post": {"Tonemap": 1, "Bloom": True, "BloomThreshold": 1.0, "BloomKnee": 0.5, "BloomIntensity": 0.6, "BloomRadius": 1.0,
                 "Vignette": 0.25, "Saturation": 1.12, "Contrast": 1.06, "Brightness": 0.0, "Tint": [1, 1, 1], "SSAO": True,
                 "SSAORadius": 0.5, "SSAOIntensity": 1.0, "SSAOBias": 0.03, "FXAA": True},
        "Gravity3D": [0, -9.81, 0],
    }
    s.metadata = {"Description": "Ipak Yo'li: 3D cheksiz yuguruvchi - uch yo'lak, sakrash, sirpanish, protsedurali to'siqlar, tangalar, "
                                 "bonuslar, oshib boruvchi tezlik", "Author": "Zamin", "Tags": ["namuna", "3D", "runner"]}

    # the sun low ahead: long shadows towards the camera, a golden sky
    s.entity("Quyosh", rot=(math.radians(-34), math.radians(35), 0), DirectionalLight=k.dir_light((1.0, 0.82, 0.6), 3.0, True))
    camera = s.entity("Kamera", pos=(0, 4.4, 7.8), rot=(math.radians(-18), 0, 0),
                      Camera=k.camera_persp(math.radians(62), 0.1, 400.0, (0.9, 0.75, 0.55, 1.0),
                                            shake={"MaxOffset": [0.25, 0.25], "MaxAngle": 4.0, "Exponent": 2.0}),
                      AudioListener={"Enabled": True, "Priority": 0, "TrackVelocity": False})

    # ---- the endless ground: planes that follow the runner (their textures stay put)
    road = s.entity("Yo'l", pos=(0, 0, -100), scale=(ROAD_W, 1, ROAD_L),
                    MeshRenderer=k.mesh("Plane", (1, 1, 1, 1), 0.8, 0.0, albedo_map="Textures/road.png", normal_map="Textures/road_normal.png",
                                        tiling=(1, ROAD_L / ROAD_TILE)))
    sand = s.entity("Qum", pos=(0, -0.05, -100), scale=(SAND_W, 1, SAND_L),
                    MeshRenderer=k.mesh("Plane", (1, 1, 1, 1), 0.95, 0.0, albedo_map="Textures/sand.png",
                                        tiling=(SAND_W / SAND_TILE, SAND_L / SAND_TILE)))
    curbs = []
    for side in (-1, 1):
        curbs.append(s.entity("Yo'l cheti" + (" (chap)" if side < 0 else " (o'ng)"), pos=(side * (ROAD_W / 2 + 0.15), 0.12, -100),
                              scale=(0.3, 0.24, ROAD_L), MeshRenderer=k.mesh("Cube", (0.62, 0.4, 0.26, 1), 0.7, 0.0)))

    # ---- templates (switched off): obstacles, pickups, scenery
    tpl = s.entity("Shablonlar", active=False)
    # cart: a wooden box on two wheels, melons on top
    cart = s.entity("Arava", parent=tpl)
    s.entity("Kuzov", parent=cart, pos=(0, 0.55, 0), scale=(1.5, 0.55, 1.7), MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.8, 0.0, **WOOD))
    for side in (-1, 1):
        s.entity("G'ildirak", parent=cart, pos=(side * 0.8, 0.42, 0), rot=(0, 0, math.radians(90)), scale=(0.84, 0.12, 0.84),
                 MeshRenderer=k.mesh("Cylinder", (0.35, 0.22, 0.12, 1), 0.7, 0.0))
    for i, (mx, mz) in enumerate(((-0.35, -0.4), (0.35, -0.35), (0.0, 0.3), (-0.4, 0.45), (0.4, 0.4))):
        s.entity("Qovun", parent=cart, pos=(mx, 0.98, mz), scale=(0.42, 0.34, 0.5),
                 MeshRenderer=k.mesh("Sphere", (0.85, 0.75, 0.25, 1) if i % 2 else (0.25, 0.5, 0.15, 1), 0.5, 0.0))
    # fence across the road
    fence = s.entity("To'siq", parent=tpl)
    for x in (-3.7, -1.2, 1.2, 3.7):
        s.entity("Ustun", parent=fence, pos=(x, 0.45, 0), scale=(0.16, 0.9, 0.16), MeshRenderer=k.mesh("Cube", (0.5, 0.3, 0.15, 1), 0.8, 0.0))
    for y, col in ((0.4, (0.8, 0.15, 0.1)), (0.75, (0.95, 0.9, 0.8))):
        s.entity("Xoda", parent=fence, pos=(0, y, 0), scale=(7.8, 0.14, 0.1), MeshRenderer=k.mesh("Cube", (*col, 1), 0.6, 0.0))
    # gate with a hanging carpet: slide under it
    gate = s.entity("Darvoza", parent=tpl)
    for side in (-1, 1):
        s.entity("Ustun", parent=gate, pos=(side * 4.45, 2.2, 0), scale=(0.6, 4.4, 0.6), MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.8, 0.0, **BRICK))
    s.entity("To'sin", parent=gate, pos=(0, 4.35, 0), scale=(9.6, 0.5, 0.7), MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.4, 0.1, **TILES, tiling=(6, 0.4)))
    s.entity("Gilam", parent=gate, pos=(0, 2.65, 0), scale=(7.9, 2.9, 0.06), MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.9, 0.0,
                                                                                                 albedo_map="Textures/carpet.png", tiling=(3, 1)))
    s.entity("Popuk", parent=gate, pos=(0, 1.16, 0), scale=(7.9, 0.06, 0.08),
             MeshRenderer=k.mesh("Cube", (1.0, 0.8, 0.3, 1), emission=(1.0, 0.7, 0.2), emission_strength=1.5))
    # wall: mud brick with a tile band
    wall = s.entity("Devor", parent=tpl)
    s.entity("G'isht", parent=wall, pos=(0, 1.5, 0), scale=(2.0, 3.0, 1.2), MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.85, 0.0, **BRICK, tiling=(1.5, 2)))
    s.entity("Koshin", parent=wall, pos=(0, 2.6, 0), scale=(2.04, 0.35, 1.24), MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.35, 0.1, **TILES, tiling=(2, 0.35)))
    # caravan: a long covered wagon
    caravan = s.entity("Karvon", parent=tpl)
    s.entity("Asos", parent=caravan, pos=(0, 0.95, 0), scale=(2.0, 0.9, 9.0), MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.8, 0.0, **WOOD, tiling=(1, 3)))
    s.entity("Chodir", parent=caravan, pos=(0, 1.45, 0), rot=(math.radians(90), 0, 0), scale=(2.0, 8.6, 2.4),
             MeshRenderer=k.mesh("Cylinder", (1, 1, 1, 1), 0.9, 0.0, albedo_map="Textures/canvas.png", tiling=(4, 1)))
    for z in (-3.4, 3.4):
        for side in (-1, 1):
            s.entity("G'ildirak", parent=caravan, pos=(side * 1.05, 0.5, z), rot=(0, 0, math.radians(90)), scale=(1.0, 0.14, 1.0),
                     MeshRenderer=k.mesh("Cylinder", (0.35, 0.22, 0.12, 1), 0.7, 0.0))
    # pickups (their root turns)
    coin = s.entity("Tanga", parent=tpl)
    s.entity("Tanga yuzi", parent=coin, rot=(math.radians(90), 0, 0), scale=(0.62, 0.08, 0.62),
             MeshRenderer=k.mesh("Cylinder", (1.0, 0.78, 0.25, 1), 0.25, 1.0, emission=(1.0, 0.65, 0.15), emission_strength=1.6, cast_shadows=False))
    magnet = s.entity("Magnit", parent=tpl)
    s.entity("Shar", parent=magnet, scale=(0.7, 0.7, 0.7), MeshRenderer=k.mesh("Sphere", (0.8, 0.3, 1.0, 1), 0.2, 0.3, emission=(0.7, 0.25, 1.0),
                                                                                emission_strength=3.0, cast_shadows=False))
    s.entity("Halqa", parent=magnet, rot=(math.radians(90), 0, 0), scale=(1.2, 0.05, 1.2),
             MeshRenderer=k.mesh("Cylinder", (1, 0.8, 1, 1), emission=(0.9, 0.6, 1.0), emission_strength=5.0, cast_shadows=False))
    s.entity("Nur", parent=magnet, PointLight=k.point_light((0.7, 0.3, 1.0), 3.0, 4.0))
    shield = s.entity("Qalqon", parent=tpl)
    s.entity("Shar", parent=shield, scale=(0.7, 0.7, 0.7), MeshRenderer=k.mesh("Sphere", (0.3, 0.9, 1.0, 1), 0.2, 0.3, emission=(0.2, 0.85, 1.0),
                                                                                emission_strength=3.0, cast_shadows=False))
    s.entity("Halqa", parent=shield, rot=(0, 0, math.radians(90)), scale=(1.2, 0.05, 1.2),
             MeshRenderer=k.mesh("Cylinder", (0.8, 1, 1, 1), emission=(0.5, 1.0, 1.0), emission_strength=5.0, cast_shadows=False))
    s.entity("Nur", parent=shield, PointLight=k.point_light((0.3, 0.85, 1.0), 3.0, 4.0))
    # scenery
    tower = s.entity("Minora", parent=tpl)
    s.entity("Tana", parent=tower, pos=(0, 3.5, 0), scale=(1.7, 7.0, 1.7), MeshRenderer=k.mesh("Cylinder", (1, 1, 1, 1), 0.85, 0.0, **BRICK, tiling=(3, 5)))
    s.entity("Belbog'", parent=tower, pos=(0, 6.2, 0), scale=(1.78, 0.6, 1.78), MeshRenderer=k.mesh("Cylinder", (1, 1, 1, 1), 0.35, 0.1, **TILES, tiling=(4, 0.5)))
    s.entity("Gumbaz", parent=tower, pos=(0, 7.2, 0), scale=(1.9, 1.6, 1.9), MeshRenderer=k.mesh("Sphere", (1, 1, 1, 1), 0.3, 0.1, **TILES, tiling=(3, 2)))
    s.entity("Uchi", parent=tower, pos=(0, 8.3, 0), scale=(0.12, 0.8, 0.12),
             MeshRenderer=k.mesh("Cylinder", (1.0, 0.8, 0.3, 1), 0.3, 1.0, emission=(1.0, 0.7, 0.2), emission_strength=1.0))
    palm = s.entity("Palma", parent=tpl)
    s.entity("Tana", parent=palm, pos=(0, 2.5, 0), rot=(0.08, 0, 0.06), scale=(0.32, 5.0, 0.32), MeshRenderer=k.mesh("Cylinder", (0.45, 0.32, 0.2, 1), 0.9, 0.0))
    for i in range(7):
        a = i * 2 * math.pi / 7
        s.entity("Barg", parent=palm, pos=(math.sin(a) * 1.1, 4.85, math.cos(a) * 1.1), rot=(0, a + math.pi / 2, math.radians(-25)),
                 scale=(2.4, 0.06, 0.55), MeshRenderer=k.mesh("Cube", (0.25, 0.55, 0.18, 1), 0.7, 0.0))
    house = s.entity("Uy", parent=tpl)
    s.entity("Devor", parent=house, pos=(0, 1.6, 0), scale=(4.2, 3.2, 4.2), MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.85, 0.0, **BRICK, tiling=(3, 2)))
    s.entity("Tom", parent=house, pos=(0, 3.25, 0), scale=(4.5, 0.25, 4.5), MeshRenderer=k.mesh("Cube", (0.7, 0.55, 0.38, 1), 0.85, 0.0))
    s.entity("Gumbaz", parent=house, pos=(0, 3.4, 0), scale=(2.4, 1.8, 2.4), MeshRenderer=k.mesh("Sphere", (1, 1, 1, 1), 0.3, 0.1, **TILES, tiling=(3, 2)))
    for side in (-1, 1):
        s.entity("Eshik", parent=house, pos=(side * 2.11, 1.0, 0), scale=(0.05, 2.0, 1.1), MeshRenderer=k.mesh("Cube", (0.3, 0.17, 0.08, 1), 0.7, 0.0))
    dune = s.entity("Qum tepa", parent=tpl, pos=(0, -0.6, 0), scale=(8, 2.4, 6),
                    MeshRenderer=k.mesh("Sphere", (1, 1, 1, 1), 0.95, 0.0, albedo_map="Textures/sand.png", tiling=(3, 2)))
    arch = s.entity("Ravoq", parent=tpl)
    for side in (-1, 1):
        s.entity("Ustun", parent=arch, pos=(side * 5.6, 3.6, 0), scale=(1.3, 7.2, 1.3), MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.85, 0.0, **BRICK, tiling=(1, 4)))
        s.entity("Gumbazcha", parent=arch, pos=(side * 5.6, 7.6, 0), scale=(1.5, 1.2, 1.5), MeshRenderer=k.mesh("Sphere", (1, 1, 1, 1), 0.3, 0.1, **TILES))
    s.entity("Peshtoq", parent=arch, pos=(0, 6.9, 0), scale=(12.6, 1.6, 1.4), MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.35, 0.1, **TILES, tiling=(8, 1)))
    s.entity("Oltin hoshiya", parent=arch, pos=(0, 6.05, 0.71), scale=(12.6, 0.12, 0.05),
             MeshRenderer=k.mesh("Cube", (1.0, 0.8, 0.3, 1), 0.3, 1.0, emission=(1.0, 0.7, 0.25), emission_strength=2.0, cast_shadows=False))

    # ---- the runner (faces -Z; the body, arms and legs are animated by the script)
    player = s.entity("Yuguruvchi")
    body = s.entity("Tana", parent=player)
    chapan = (0.12, 0.25, 0.65, 1)
    gold = (1.0, 0.75, 0.25, 1)
    skin = (0.85, 0.62, 0.45, 1)
    s.entity("Chopon", parent=body, pos=(0, 1.15, 0), scale=(0.56, 0.66, 0.34), MeshRenderer=k.mesh("Cube", chapan, 0.7, 0.0))
    s.entity("Belbog'", parent=body, pos=(0, 0.88, 0), scale=(0.58, 0.09, 0.36), MeshRenderer=k.mesh("Cube", gold, 0.4, 0.6))
    s.entity("Etak", parent=body, pos=(0, 0.72, 0), scale=(0.6, 0.3, 0.38), MeshRenderer=k.mesh("Cube", chapan, 0.7, 0.0))
    s.entity("Hoshiya", parent=body, pos=(0, 1.15, -0.172), scale=(0.08, 0.64, 0.01), MeshRenderer=k.mesh("Cube", gold, 0.4, 0.6))
    head = s.entity("Bosh", parent=body, pos=(0, 1.66, 0), scale=(0.34, 0.36, 0.34), MeshRenderer=k.mesh("Sphere", skin, 0.6, 0.0))
    s.entity("Do'ppi", parent=body, pos=(0, 1.83, 0), scale=(0.33, 0.12, 0.33), MeshRenderer=k.mesh("Cylinder", (0.1, 0.1, 0.12, 1), 0.6, 0.0))
    s.entity("Do'ppi naqshi", parent=body, pos=(0, 1.83, 0), scale=(0.335, 0.04, 0.335),
             MeshRenderer=k.mesh("Cylinder", (0.95, 0.95, 0.9, 1), emission=(1, 1, 1), emission_strength=0.3))
    limbs = {}
    for name, side in (("ArmL", -1), ("ArmR", 1)):
        pivot = s.entity("Qo'l" + (" (chap)" if side < 0 else " (o'ng)"), parent=body, pos=(side * 0.36, 1.42, 0))
        s.entity("Yeng", parent=pivot, pos=(0, -0.28, 0), scale=(0.15, 0.56, 0.15), MeshRenderer=k.mesh("Cube", chapan, 0.7, 0.0))
        s.entity("Kaft", parent=pivot, pos=(0, -0.6, 0), scale=(0.13, 0.13, 0.13), MeshRenderer=k.mesh("Sphere", skin, 0.6, 0.0))
        limbs[name] = pivot
    for name, side in (("LegL", -1), ("LegR", 1)):
        pivot = s.entity("Oyoq" + (" (chap)" if side < 0 else " (o'ng)"), parent=body, pos=(side * 0.15, 0.62, 0))
        s.entity("Shim", parent=pivot, pos=(0, -0.28, 0), scale=(0.19, 0.56, 0.19), MeshRenderer=k.mesh("Cube", (0.2, 0.15, 0.12, 1), 0.8, 0.0))
        s.entity("Etik", parent=pivot, pos=(0, -0.58, -0.06), scale=(0.2, 0.12, 0.32), MeshRenderer=k.mesh("Cube", (0.35, 0.2, 0.1, 1), 0.6, 0.0))
        limbs[name] = pivot
    dust = s.entity("Chang", parent=player, pos=(0, 0.05, 0.3), ParticleSystem=k.particles(
        EmissionRate=20.0, MaxParticles=300, Shape=1, ShapeRadius=0.25, LifetimeMin=0.4, LifetimeMax=0.8, SpeedMin=0.8, SpeedMax=2.0,
        Direction=(0, 0.6, 1), Spread=35.0, Drag=1.5, StartColor=(0.95, 0.8, 0.6, 0.5), EndColor=(0.9, 0.75, 0.55, 0.0), StartSize=0.2,
        EndSize=0.55, Additive=False))
    bubble = s.entity("Qalqon pufagi", parent=player, pos=(0, 0.95, 0), scale=(2.2, 2.2, 2.2), active=False,
                      MeshRenderer=k.mesh("Sphere", (0.3, 0.85, 1.0, 0.22), 0.1, 0.0, emission=(0.2, 0.7, 1.0), emission_strength=0.8, cast_shadows=False))
    s.get(bubble)["MeshRenderer"]["BlendMode"] = "Transparent"
    s.entity("Yuguruvchi chirog'i", parent=player, pos=(0, 2.5, 1.5), PointLight=k.point_light((1.0, 0.8, 0.55), 1.0, 5.0))

    # ---- effects
    fx = s.entity("Effektlar")
    coin_burst = s.entity("Tanga uchqunlari", parent=fx, pos=(0, -20, 0), ParticleSystem=k.particles(
        Playing=True, EmissionRate=0.0, MaxParticles=500, Shape=2, ShapeRadius=0.2, LifetimeMin=0.3, LifetimeMax=0.6, SpeedMin=2.0,
        SpeedMax=4.5, Direction=(0, 1, 0), Spread=180.0, Drag=2.0, StartColor=(3.0, 2.2, 0.6, 1.0), EndColor=(1.0, 0.5, 0.1, 0.0),
        StartSize=0.12, EndSize=0.0))
    crash_burst = s.entity("To'qnashuv", parent=fx, pos=(0, -20, 0), ParticleSystem=k.particles(
        Playing=True, EmissionRate=0.0, MaxParticles=600, Shape=2, ShapeRadius=0.5, LifetimeMin=0.5, LifetimeMax=1.2, SpeedMin=2.0,
        SpeedMax=7.0, Direction=(0, 1, 0), Spread=80.0, Gravity=(0, -9, 0), Drag=0.8, StartColor=(0.95, 0.8, 0.55, 0.9),
        EndColor=(0.8, 0.6, 0.4, 0.0), StartSize=0.35, EndSize=0.8, Additive=False))

    # ---- UI
    game = s.entity("Game")
    canvas = s.entity("UI", UITransform=k.ui_stretch(), UICanvas=k.ui_canvas())
    ui = k.UIKit(s, canvas, ATLAS, game, (0.9, 0.45, 0.12, 1.0), (1.0, 0.62, 0.25, 1.0), (0.62, 0.28, 0.06, 1.0))
    hud = s.entity("HUD", parent=canvas, UITransform=k.ui_stretch())
    s.entity("Yuqori fon", parent=hud, UITransform=k.ui_rect((0, 1), (1, 1), (0.5, 1), (0, 0), (0, 150)),
             UIImage=k.ui_image((0.08, 0.03, 0.0, 0.62), raycast=False))
    score = ui.text("Ochko", hud, "0", (0.5, 1), (0.5, 1), (0, -24), (700, 90), 80, (1.0, 0.95, 0.85, 1.0), h="Center")
    distance = ui.text("Masofa", hud, "", (0, 1), (0, 1), (50, -36), (700, 52), 44, (1.0, 0.9, 0.7, 1.0))
    best = ui.text("Rekord", hud, "", (0, 1), (0, 1), (50, -90), (700, 34), 28, (1.0, 0.85, 0.6, 0.85))
    coins = ui.text("Tangalar", hud, "", (1, 1), (1, 1), (-50, -36), (600, 52), 44, (1.0, 0.8, 0.3, 1.0), h="Right")
    speed = ui.text("Tezlik", hud, "", (0, 0), (0, 0), (50, 44), (600, 40), 32, (1.0, 0.95, 0.9, 0.9))
    power = ui.text("Bonuslar", hud, "", (0.5, 0), (0.5, 0), (0, 50), (1200, 44), 34, (0.75, 0.9, 1.0, 1.0), h="Center")
    banner = ui.text("E'lon", hud, "", (0.5, 0.5), (0.5, 0.5), (0, 220), (1400, 110), 92, (1.0, 0.95, 0.8, 1.0), h="Center",
                     UIAnimation=k.ui_anim("Pop", "Fade", 0.35))
    banner_sub = ui.text("E'lon izohi", banner, "", (0.5, 0.0), (0.5, 1.0), (0, -4), (1200, 50), 40, (1.0, 0.85, 0.5, 1.0), h="Center")
    demo_badge = ui.text("Demo belgisi", hud, "DEMO - avtomatik o'yin", (1, 1), (1, 1), (-50, -90), (600, 32), 26, (1.0, 0.9, 0.6, 0.9),
                         h="Right", key="hud.demo", active=False)

    main_menu = ui.dim("Bosh menyu", True, 0.25, (0.08, 0.03, 0.0))
    title = s.entity("Sarlavha", parent=main_menu, UITransform=k.ui_rect(pos=(0, 290), size=(1700, 180), font=156),
                     UIAnimation=k.ui_anim("SlideDown", "None", 0.6, slide=120))
    s.entity("Sarlavha soyasi", parent=title, UITransform=k.ui_rect((0, 0), (1, 1), pos=(6, -7), size=(0, 0), font=156),
             Text=k.ui_text("IPAK YO'LI", (0.35, 0.12, 0.02, 0.9), "Center", key="menu.title"))
    s.entity("Sarlavha matni", parent=title, UITransform=k.ui_rect((0, 0), (1, 1), size=(0, 0), font=156),
             Text=k.ui_text("IPAK YO'LI", (1.0, 0.85, 0.45, 1.0), "Center", key="menu.title"))
    s.entity("Taglavha", parent=main_menu, UITransform=k.ui_rect(pos=(0, 176), size=(1500, 50), font=38),
             Text=k.ui_text(UZ["menu"]["subtitle"], (1.0, 0.95, 0.85, 0.95), "Center", key="menu.subtitle"))
    menu_panel = ui.panel("Menyu paneli", main_menu, (0, -90), (560, 330))
    play_button = ui.button("O'ynash", menu_panel, "O'ynash", "menu.play", "Play", True)
    language_button = ui.button("Til", menu_panel, "Til: O'zbekcha", None, "Language")
    ui.button("Chiqish", menu_panel, "Chiqish", "menu.quit", "Quit")
    menu_best = s.entity("Bosh menyu rekordi", parent=main_menu, UITransform=k.ui_rect(pos=(0, -300), size=(1200, 44), font=32),
                         Text=k.ui_text("", (1.0, 0.9, 0.5, 1.0), "Center"))
    s.entity("Boshqaruv", parent=main_menu, UITransform=k.ui_rect((0.5, 0), None, (0.5, 0), (0, 50), (1800, 70), font=25),
             Text=k.ui_text(UZ["menu"]["controls"], (1.0, 0.95, 0.9, 0.9), "Center", key="menu.controls"))

    pause_menu = ui.dim("Pauza menyusi", False)
    pause_panel = ui.panel("Pauza paneli", pause_menu, (0, 0), (620, 460))
    ui.label("Pauza sarlavhasi", pause_panel, "PAUZA", (520, 80), 60, (1.0, 0.85, 0.5, 1.0), "pause.title")
    resume_button = ui.button("Davom etish", pause_panel, "Davom etish", "pause.resume", "Resume", True)
    ui.button("Qaytadan", pause_panel, "Qaytadan", "pause.restart", "Restart")
    ui.button("Bosh menyuga", pause_panel, "Bosh menyu", "pause.menu", "Menu")

    over_menu = ui.dim("O'yin tugadi", False, 0.5)
    over_panel = ui.panel("Natija paneli", over_menu, (0, 0), (760, 540))
    ui.label("Natija sarlavhasi", over_panel, "YIQILDINGIZ!", (680, 84), 64, (1.0, 0.55, 0.35, 1.0), "over.title")
    over_score = ui.label("Natija ochkosi", over_panel, "", (680, 60), 48, (1.0, 0.92, 0.6, 1.0))
    over_stats = ui.label("Natija statistikasi", over_panel, "", (680, 40), 30, (1.0, 0.95, 0.9, 1.0))
    over_record = ui.label("Natija rekordi", over_panel, "", (680, 40), 30, (1.0, 0.7, 0.3, 1.0))
    retry_button = ui.button("Qayta yugurish", over_panel, "Qaytadan", "pause.restart", "Restart", True)
    ui.button("Natijadan menyuga", over_panel, "Bosh menyu", "pause.menu", "Menu")

    s.add(player, "NativeScript", k.script("Runner", Body=k.ref(body), Head=k.ref(head), ArmL=k.ref(limbs["ArmL"]), ArmR=k.ref(limbs["ArmR"]),
                                           LegL=k.ref(limbs["LegL"]), LegR=k.ref(limbs["LegR"]), Dust=k.ref(dust), ShieldBubble=k.ref(bubble)))
    s.add(game, "NativeScript", k.script(
        "RunnerGame", Player=k.ref(player), CameraRig=k.ref(camera), Road=k.ref(road), Sand=k.ref(sand), CurbL=k.ref(curbs[0]),
        CurbR=k.ref(curbs[1]), CartTemplate=k.ref(cart), FenceTemplate=k.ref(fence), GateTemplate=k.ref(gate), WallTemplate=k.ref(wall),
        CaravanTemplate=k.ref(caravan), CoinTemplate=k.ref(coin), MagnetTemplate=k.ref(magnet), ShieldTemplate=k.ref(shield),
        TowerTemplate=k.ref(tower), PalmTemplate=k.ref(palm), DuneTemplate=k.ref(dune), HouseTemplate=k.ref(house), ArchTemplate=k.ref(arch),
        CoinBurst=k.ref(coin_burst), CrashBurst=k.ref(crash_burst), Hud=k.ref(hud), ScoreText=k.ref(score), DistanceText=k.ref(distance),
        CoinsText=k.ref(coins), SpeedText=k.ref(speed), PowerText=k.ref(power), BestText=k.ref(best), Banner=k.ref(banner),
        BannerSub=k.ref(banner_sub), DemoBadge=k.ref(demo_badge), MainMenu=k.ref(main_menu), MenuBest=k.ref(menu_best),
        PlayButton=k.ref(play_button), LanguageButton=k.ref(language_button), PauseMenu=k.ref(pause_menu), ResumeButton=k.ref(resume_button),
        GameOverMenu=k.ref(over_menu), OverScore=k.ref(over_score), OverStats=k.ref(over_stats), OverRecord=k.ref(over_record),
        RetryButton=k.ref(retry_button)))
    s.add(game, "AudioSource", k.audio_source("Audio/music.wav", 0.5, loop=True, play=True, output="Music", stream=True))
    return s


UZ = {
    "$name": "O'zbekcha",
    "menu": {"title": "IPAK YO'LI", "subtitle": "Cheksiz yugurish  ·  Zamin Engine namunasi", "play": "O'ynash", "quit": "Chiqish",
             "language": "Til: {0}", "best": "Rekord: {0} ochko  ·  eng uzoq: {1} m",
             "controls": "A / D yoki strelkalar - yo'lak almashtirish   ·   Space / W - sakrash   ·   S - sirpanish   ·   Esc - pauza\n"
                         "Gamepad: D-pad / LB / RB / chap stik - yo'lak, A - sakrash, B - sirpanish, Start - pauza"},
    "hud": {"demo": "DEMO - avtomatik o'yin", "distance": "{0} m", "coins": "Tanga: {0}", "speed": "{0} km/soat", "best": "Rekord: {0}",
            "go": "YUGUR!", "go_sub": "Aravalardan sakrang, gilam ostidan sirpaning", "milestone": "{0} m!", "faster": "Tezlik oshdi",
            "magnet": "Magnit: tangalar o'zi keladi!", "shield": "Qalqon: bitta zarbadan saqlaydi", "magnet_time": "Magnit {0} s",
            "shield_time": "Qalqon {0} s"},
    "pause": {"title": "PAUZA", "resume": "Davom etish", "restart": "Qaytadan", "menu": "Bosh menyu"},
    "over": {"title": "YIQILDINGIZ!", "score": "Ochko: {0}", "stats": "{0} m  ·  {1} tanga", "record": "Yangi rekord!", "best": "Rekord: {0}"},
}
EN = {
    "$name": "English",
    "menu": {"title": "SILK ROAD", "subtitle": "An endless run  ·  a Zamin Engine sample", "play": "Play", "quit": "Quit",
             "language": "Language: {0}", "best": "Record: {0} points  ·  longest: {1} m",
             "controls": "A / D or arrows - change lanes   ·   Space / W - jump   ·   S - slide   ·   Esc - pause\n"
                         "Gamepad: D-pad / LB / RB / left stick - lanes, A - jump, B - slide, Start - pause"},
    "hud": {"demo": "DEMO - autoplay", "distance": "{0} m", "coins": "Coins: {0}", "speed": "{0} km/h", "best": "Record: {0}", "go": "RUN!",
            "go_sub": "Jump the carts, slide under the carpets", "milestone": "{0} m!", "faster": "Faster now",
            "magnet": "Magnet: the coins come to you!", "shield": "Shield: one hit forgiven", "magnet_time": "Magnet {0} s",
            "shield_time": "Shield {0} s"},
    "pause": {"title": "PAUSED", "resume": "Resume", "restart": "Restart", "menu": "Main menu"},
    "over": {"title": "YOU FELL!", "score": "Score: {0}", "stats": "{0} m  ·  {1} coins", "record": "New record!", "best": "Record: {0}"},
}


def main():
    make_textures()
    make_ui_atlas()
    make_icon()
    make_sounds()
    k.write_json(os.path.join(ASSETS, "Localization/uz.json"), UZ)
    k.write_json(os.path.join(ASSETS, "Localization/en.json"), EN)
    scene = build_scene()
    uuid = scene.save(ASSETS, "Scenes/SilkRoad.zscene", NAME)
    actions = [
        ("Left", ["Key:A", "Key:Left", "Gamepad:DPadLeft", "Gamepad:LeftBumper"]),
        ("Right", ["Key:D", "Key:Right", "Gamepad:DPadRight", "Gamepad:RightBumper"]),
        ("Jump", ["Key:Space", "Key:W", "Key:Up", "Gamepad:A"]),
        ("Slide", ["Key:S", "Key:Down", "Gamepad:B"]),
        ("Pause", ["Key:Escape", "Key:P", "Gamepad:Start"]),
    ]
    axes = [k.axis("Steer", [], [], "LeftX", deadzone=0.25)]
    k.write_project(PROJECT, NAME, "Ipak Yo'li", "Scenes/SilkRoad.zscene", uuid, "1.0.0", [("Scenes/SilkRoad.zscene", uuid)], actions, axes,
                    description="3D cheksiz yugurish: uch yo'lak, sakrash va sirpanish, protsedurali to'siqlar, tangalar, oshib boruvchi tezlik")
    k.write_module_cmake(PROJECT, NAME, "Zamin namuna o'yini: Ipak Yo'li (endless runner) - C++ skriptlar moduli (libEndlessRunner.so)")
    print("EndlessRunner: tayyor")


if __name__ == "__main__":
    main()
