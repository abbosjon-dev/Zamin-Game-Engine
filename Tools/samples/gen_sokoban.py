"""Projects/Sokoban: "Omborchi" (Sokoban) - its textures, sounds, scene and project file.

    python3 Tools/samples/gen_sokoban.py

Everything under Projects/Sokoban/Assets and the .zproject are made here; the rules, the levels, the solver and
the game flow are C++ (Projects/Sokoban/Source). The levels are checked here too: each must be solvable (a small
breadth-first search over the box pushes - the same idea as the game's own solver in Board.cpp).
"""

import math
import os
import re
import sys
from collections import deque

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import samplekit as k  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NAME = "Sokoban"
PROJECT = os.path.join(ROOT, "Projects", NAME)
ASSETS = os.path.join(PROJECT, "Assets")
ATLAS = "Textures/ui_atlas.png"
LEVEL_COUNT = 10

# ------------------------------------------------------------------------------------------ levels check


def read_levels():
    """The level maps of Source/Levels.cpp (raw string literals R"(...)")."""
    with open(os.path.join(PROJECT, "Source", "Levels.cpp"), encoding="utf-8") as f:
        text = f.read()
    return [m.strip("\n") for m in re.findall(r'R"\((.*?)\)"', text, re.S)]


def check_level(text):
    """The fewest pushes that solve the level (None: unsolvable)."""
    rows = text.split("\n")
    walls, goals, boxes, player = set(), set(), set(), None
    for r, row in enumerate(rows):
        for c, ch in enumerate(row):
            if ch == '#':
                walls.add((r, c))
            if ch in '.*+':
                goals.add((r, c))
            if ch in '$*':
                boxes.add((r, c))
            if ch in '@+':
                player = (r, c)
    dirs = ((-1, 0), (1, 0), (0, -1), (0, 1))
    floor, stack = {player}, [player]
    while stack:
        p = stack.pop()
        for dr, dc in dirs:
            n = (p[0] + dr, p[1] + dc)
            if n not in walls and n not in floor and 0 <= n[0] < len(rows):
                floor.add(n)
                stack.append(n)

    def reach(pl, bx):
        seen, q = {pl}, deque([pl])
        while q:
            p = q.popleft()
            for dr, dc in dirs:
                n = (p[0] + dr, p[1] + dc)
                if n in floor and n not in bx and n not in seen:
                    seen.add(n)
                    q.append(n)
        return seen

    start = frozenset(boxes)
    seen = {(min(reach(player, start)), start): 0}
    q = deque([(player, start, 0)])
    while q:
        pl, bx, n = q.popleft()
        if bx == goals:
            return n
        rc = reach(pl, bx)
        for b in bx:
            for dr, dc in dirs:
                behind, to = (b[0] - dr, b[1] - dc), (b[0] + dr, b[1] + dc)
                if behind in rc and to in floor and to not in bx:
                    nb = frozenset((bx - {b}) | {to})
                    key = (min(reach(b, nb)), nb)
                    if key not in seen:
                        seen[key] = n + 1
                        q.append((b, nb, n + 1))
    return None

# ------------------------------------------------------------------------------------------- textures


def save_texture(rel, img):
    k.write_png(os.path.join(ASSETS, rel), np.clip(img, 0, 1))
    k.write_zmeta(ASSETS, rel, "Texture", {}, NAME)


def make_textures():
    s = 256
    ys, xs = np.mgrid[0:s, 0:s].astype(np.float64)

    # wooden floor: four planks with grain, gaps and nails
    n = k.value_noise(s, s, 6, seed=11, octaves=4)
    grain = k.value_noise(s, s, 24, seed=12, octaves=2)
    plank = (ys // 64).astype(int)
    stretch = np.sin((xs + plank * 37) / 9.0 + grain * 6.0) * 0.5 + 0.5
    gap = k.smooth(2.5, 0.5, np.minimum(ys % 64, 64 - ys % 64))
    shade = 0.82 + 0.18 * ((plank * 0.37) % 1.0)
    height = 0.7 + 0.15 * stretch + 0.1 * n - 0.6 * gap
    img = k.image(s, s, (0, 0, 0, 1))
    base = np.array([0.62, 0.42, 0.25])
    for i in range(3):
        img[:, :, i] = base[i] * shade * (0.75 + 0.2 * stretch + 0.2 * n) * (1.0 - 0.7 * gap)
    for px in (16, 240):
        for py in range(32, 256, 64):
            d = np.hypot(xs - px, ys - py)
            nail = k.smooth(3.0, 1.5, d)
            for i in range(3):
                img[:, :, i] = img[:, :, i] * (1 - nail) + nail * 0.25
    save_texture("Textures/floor.png", img)
    save_texture("Textures/floor_normal.png", k.normal_map_from_height(height, 2.5))

    # brick wall: running bond, mortar, weathering
    n = k.value_noise(s, s, 8, seed=21, octaves=4)
    rows = (ys // 32).astype(int)
    bx = (xs + (rows % 2) * 32) % 64
    by = ys % 32
    mortar = np.maximum(k.smooth(3.0, 1.0, np.minimum(bx, 64 - bx)), k.smooth(3.0, 1.0, np.minimum(by, 32 - by)))
    tint = ((rows * 7 + ((xs + (rows % 2) * 32) // 64).astype(int) * 13) % 5) / 5.0
    height = 0.8 + 0.12 * n - 0.7 * mortar
    img = k.image(s, s, (0, 0, 0, 1))
    brick = np.array([0.62, 0.27, 0.18])
    mort = np.array([0.55, 0.52, 0.48])
    for i in range(3):
        b = brick[i] * (0.75 + 0.25 * tint + 0.25 * n)
        img[:, :, i] = b * (1 - mortar) + mort[i] * (0.8 + 0.2 * n) * mortar
    save_texture("Textures/brick.png", img)
    save_texture("Textures/brick_normal.png", k.normal_map_from_height(height, 4.0))

    # crate: planks in a frame with a diagonal brace
    n = k.value_noise(s, s, 6, seed=31, octaves=4)
    grain = k.value_noise(s, s, 20, seed=32, octaves=2)
    x, y = k.coords(s, s)
    frame = np.maximum(np.abs(x), np.abs(y)) > 100
    brace = (np.abs(x + y) < 16) & ~frame
    slats = k.smooth(2.0, 0.5, np.abs(((y + 128) % 50) - 25) - 23) * ~frame
    height = np.where(frame | brace, 0.95, 0.55) - 0.3 * slats + 0.06 * n
    img = k.image(s, s, (0, 0, 0, 1))
    wood = np.array([0.72, 0.5, 0.28])
    dark = np.array([0.5, 0.33, 0.18])
    for i in range(3):
        col = np.where(frame | brace, dark[i], wood[i]) * (0.75 + 0.2 * n + 0.15 * np.sin(grain * 9.0 + x / 6.0))
        img[:, :, i] = col * (1 - 0.5 * slats)
    edge = k.smooth(2.5, 0.0, np.abs(np.maximum(np.abs(x), np.abs(y)) - 100))
    for i in range(3):
        img[:, :, i] *= 1 - 0.55 * edge
    for cx, cy in ((-114, -114), (114, -114), (-114, 114), (114, 114)):
        d = np.hypot(x - cx, y - cy)
        bolt = k.smooth(6, 4, d)
        for i in range(3):
            img[:, :, i] = img[:, :, i] * (1 - bolt) + bolt * 0.35
    save_texture("Textures/crate.png", img)
    save_texture("Textures/crate_normal.png", k.normal_map_from_height(height, 3.0))

    # goal pad: a glowing ring with an inner dot (cylinder caps map radius 128 px to the rim)
    r = np.hypot(x + 0.5, y + 0.5) / 128.0
    ring = k.smooth(0.07, 0.0, np.abs(r - 0.78)) + 0.6 * k.smooth(0.05, 0.0, np.abs(r - 0.55)) + k.smooth(0.2, 0.12, r)
    ticks = (np.cos(np.arctan2(y, x) * 8.0) > 0.7) * k.smooth(0.05, 0.0, np.abs(r - 0.92))
    v = np.clip(ring + ticks, 0, 1)
    img = k.image(s, s, (0, 0, 0, 1))
    for i in range(3):
        img[:, :, i] = 0.06 + 0.94 * v
    save_texture("Textures/goal.png", img)

    # warehouse ground: worn concrete with painted grid lines
    n = k.value_noise(s, s, 8, seed=41, octaves=5)
    line = k.smooth(2.0, 0.5, np.minimum(np.minimum(xs % 128, 128 - xs % 128), np.minimum(ys % 128, 128 - ys % 128)))
    img = k.image(s, s, (0, 0, 0, 1))
    for i, c in enumerate((0.2, 0.22, 0.27)):
        img[:, :, i] = c * (0.75 + 0.45 * n) * (1 - 0.35 * line) + line * (0.08, 0.07, 0.03)[i]
    save_texture("Textures/ground.png", img)


def make_ui_atlas():
    atlas = k.Atlas(512, 256)
    k.ui_panel_sprites(atlas, (0.12, 0.09, 0.07), (0.05, 0.04, 0.035), (1.0, 0.7, 0.3), (1.0, 0.6, 0.25))
    return atlas.save(ASSETS, ATLAS, NAME)


def make_icon():
    size = 256
    img = k.image(size, size)
    x, y = k.coords(size, size)
    sd = k.sd_round_box(x, y, 120, 120, 44)
    bg = k.mix_color((0.25, 0.55, 0.95), (0.08, 0.12, 0.35), np.clip(y / size + 0.5, 0, 1))
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = bg[:, :, i]
    img[:, :, 3] = cov
    # a crate pushed onto a glowing target
    k.glow(img, np.abs(k.sd_circle(x, y - 40, 70)) - 6, (0.4, 1.0, 0.6, 1.0), 14, 0.9)
    k.fill(img, np.abs(k.sd_circle(x, y - 40, 70)) - 6, (0.5, 1.0, 0.7, 1.0))
    box = k.sd_round_box(x, y - 10, 62, 62, 8)
    k.fill(img, box, (0.85, 0.58, 0.3, 1.0))
    k.fill(img, np.abs(box + 12) - 5, (0.55, 0.35, 0.18, 1.0))
    k.fill(img, k.sd_round_box((x + y - 10) * 0.7071, (x - y + 10) * 0.7071, 70, 6, 3) + 0 * box, (0.55, 0.35, 0.18, 1.0))
    img[:, :, 3] = np.minimum(img[:, :, 3], cov)
    save_texture("Textures/GameIcon.png", img)

# --------------------------------------------------------------------------------------------- sounds


def make_sounds():
    R = k.RATE
    out = {}
    n = int(0.09 * R)
    out["step"] = k.osc(np.linspace(520, 380, n), 0.09, "square") * k.exp_decay(n, 0.02) * 0.35 + \
        k.highpass(k.noise(0.09, 1), 2500) * k.exp_decay(n, 0.008) * 0.3
    n = int(0.28 * R)
    scrape = k.lowpass(k.noise(0.28, 2), 900) * np.sin(np.linspace(0, np.pi, n)) ** 0.5
    out["push"] = scrape * 0.9 + k.osc(np.linspace(110, 80, n), 0.28, "tri") * k.exp_decay(n, 0.08) * 0.5
    n = int(0.16 * R)
    out["bump"] = k.osc(np.linspace(160, 90, n), 0.16, "sine") * k.exp_decay(n, 0.05) + k.lowpass(k.noise(0.16, 3), 600) * k.exp_decay(n, 0.03) * 0.5
    buf = np.zeros(int(0.7 * R))
    for i, note in enumerate(("G5", "D6")):
        f = k.note_freq(note)
        k.place(buf, (k.osc(f, 0.5, "sine") + 0.35 * k.osc(f * 2, 0.5, "sine")) * k.exp_decay(int(0.5 * R), 0.16), i * 0.07, 0.6)
    out["goal"] = buf
    n = int(0.22 * R)
    out["undo"] = k.osc(np.linspace(300, 900, n), 0.22, "tri") * np.sin(np.linspace(0, np.pi, n)) * 0.6
    n = int(0.45 * R)
    out["restart"] = k.lowpass(k.noise(0.45, 4), np.linspace(400, 3000, n)) * np.sin(np.linspace(0, np.pi, n)) * 0.6 + \
        k.osc(np.linspace(200, 600, n), 0.45, "sine") * np.sin(np.linspace(0, np.pi, n)) * 0.3
    buf = np.zeros(int(0.9 * R))
    for i, note in enumerate(("C5", "G5")):
        k.place(buf, k.osc(k.note_freq(note), 0.6, "tri") * k.exp_decay(int(0.6 * R), 0.2), i * 0.12, 0.5)
    out["level_start"] = buf
    buf = np.zeros(int(2.0 * R))
    for i, note in enumerate(("C5", "E5", "G5", "C6", "G5", "C6", "E6")):
        tone = k.osc(k.note_freq(note), 0.5, "square") * 0.4 + k.osc(k.note_freq(note) * 2, 0.5, "sine") * 0.3
        k.place(buf, tone * k.exp_decay(int(0.5 * R), 0.18), [0, 0.1, 0.2, 0.3, 0.5, 0.62, 0.74][i], 0.5)
    out["level_complete"] = k.lowpass(buf, 6000)
    n = int(0.06 * R)
    out["ui_click"] = k.osc(1100, 0.06, "square") * k.exp_decay(n, 0.012) * 0.6 + k.osc(2200, 0.06, "sine") * k.exp_decay(n, 0.006) * 0.4
    for name, snd in out.items():
        rel = f"Audio/{name}.wav"
        k.write_wav(os.path.join(ASSETS, rel), snd)
        k.write_zmeta(ASSETS, rel, "Audio", {"LoadMode": "Decompress", "Preload": True, "Volume": 1.0}, NAME)
    # music: a calm loop - a soft pad (Fmaj7 - Am7 - Dm7 - Cmaj7) with a plucked pentatonic arpeggio, 16 s
    seconds, bars = 16.0, 4
    total = int(seconds * R)
    buf = np.zeros(total + R)
    chords = (("F3", "A3", "C4", "E4"), ("A3", "C4", "E4", "G4"), ("D3", "F3", "A3", "C4"), ("C3", "E3", "G3", "B3"))
    bar = seconds / bars
    for b, chord in enumerate(chords):
        seg = np.zeros(int(bar * R))
        for note in chord:
            for det in (0.997, 1.003):
                seg += k.osc(k.note_freq(note) * det, bar, "saw")
        seg = k.lowpass(seg, 700) * k.env(len(seg), 0.8, 0.5, 0.85, bar - 1.6, 0.8)
        k.place(buf, seg, b * bar, 0.16)
        for step in range(8):
            note = chord[(step * 3) % 4]
            f = k.note_freq(note) * 2
            pluck = (k.osc(f, 0.6, "tri") + 0.3 * k.osc(f * 2, 0.6, "sine")) * k.exp_decay(int(0.6 * R), 0.15)
            k.place(buf, pluck, b * bar + step * bar / 8, 0.22 if step % 2 == 0 else 0.14)
    buf[:R] += buf[total:total + R]
    rel = "Audio/music.wav"
    k.write_wav(os.path.join(ASSETS, rel), buf[:total], peak=0.6)
    k.write_zmeta(ASSETS, rel, "Audio", {"LoadMode": "Stream", "Preload": False, "Volume": 1.0}, NAME)

# ---------------------------------------------------------------------------------------------- scene


def build_scene():
    s = k.SceneBuilder("Warehouse", 8840000000000000)
    s.settings = {
        "DrawSky": False, "Ambient": 0.42, "Exposure": 1.05, "ShadowDistance": 40,
        "Fog": {"Enabled": True, "Color": [0.05, 0.06, 0.1], "Density": 0.022, "HeightFalloff": 0.0},
        "Post": {"Tonemap": 1, "Bloom": True, "BloomThreshold": 1.0, "BloomKnee": 0.5, "BloomIntensity": 0.55, "BloomRadius": 1.0,
                 "Vignette": 0.38, "Saturation": 1.12, "Contrast": 1.05, "Brightness": 0.0, "Tint": [1, 1, 1], "SSAO": True,
                 "SSAORadius": 0.5, "SSAOIntensity": 1.3, "SSAOBias": 0.03, "FXAA": True},
        "Gravity3D": [0, -9.81, 0],
    }
    s.metadata = {"Description": "Omborchi (Sokoban): 10 ta daraja, orqaga qaytarish (undo), darajalar menyusi, rekordlar va "
                                 "yechuvchi bot", "Author": "Zamin", "Tags": ["namuna", "3D", "puzzle"]}

    s.entity("Quyosh", rot=(math.radians(-52), math.radians(28), 0), DirectionalLight=k.dir_light((1.0, 0.86, 0.7), 2.8, True))
    s.entity("To'ldiruvchi yorug'lik", pos=(0, 6, 8), PointLight=k.point_light((0.45, 0.6, 1.0), 2.5, 22.0))
    camera = s.entity("Kamera", pos=(0, 10, 7), rot=(math.radians(-56), 0, 0),
                      Camera=k.camera_persp(math.radians(40), 0.1, 200.0, (0.05, 0.06, 0.1, 1.0)),
                      AudioListener={"Enabled": True, "Priority": 0, "TrackVelocity": False})

    # ---- the warehouse around the level
    env = s.entity("Ombor")
    s.entity("Pol", parent=env, pos=(0, -0.13, 0), scale=(70, 1, 70),
             MeshRenderer=k.mesh("Plane", (1, 1, 1, 1), 0.85, 0.0, albedo_map="Textures/ground.png", tiling=(35, 35)))
    crate = dict(albedo_map="Textures/crate.png", normal_map="Textures/crate_normal.png")
    for i, (x, z, stack, rot) in enumerate(((-8.5, -5.5, 3, 0.2), (8.8, -6.0, 2, -0.3), (-9.5, 4.0, 2, 0.5), (9.2, 3.5, 1, 0.1),
                                           (-3.5, -8.5, 2, 0.0), (4.0, -9.0, 3, 0.4))):
        for j in range(stack):
            s.entity(f"Quti uyumi {i + 1}.{j + 1}", parent=env, pos=(x + 0.08 * j, 0.45 + 0.9 * j, z - 0.05 * j), rot=(0, rot + 0.3 * j, 0),
                     scale=(0.9, 0.9, 0.9), MeshRenderer=k.mesh("Cube", (0.85, 0.85, 0.85, 1), 0.7, 0.0, **crate))
    for i, (x, z, col) in enumerate(((-7.5, -7.0, (0.7, 0.15, 0.12)), (-7.0, -7.9, (0.15, 0.3, 0.65)), (7.5, 6.5, (0.7, 0.15, 0.12)),
                                     (-10.5, 0.5, (0.2, 0.5, 0.25)), (10.5, -1.5, (0.15, 0.3, 0.65)), (8.2, 7.4, (0.85, 0.6, 0.12)))):
        b = s.entity(f"Bochka {i + 1}", parent=env, pos=(x, 0.55, z), scale=(0.7, 1.1, 0.7),
                     MeshRenderer=k.mesh("Cylinder", (*col, 1), 0.45, 0.6))
        s.entity("Halqa", parent=b, pos=(0, 0.25, 0), scale=(1.04, 0.04, 1.04), MeshRenderer=k.mesh("Cylinder", (0.2, 0.2, 0.22, 1), 0.4, 0.8))
    # shelves along the back wall
    for i in range(5):
        x = -10 + i * 5
        shelf = s.entity(f"Javon {i + 1}", parent=env, pos=(x, 0, -12))
        for lx in (-2, 2):
            s.entity("Ustun", parent=shelf, pos=(lx, 2, 0), scale=(0.15, 4, 0.15), MeshRenderer=k.mesh("Cube", (0.9, 0.45, 0.12, 1), 0.5, 0.5))
        for ly in (1.0, 2.2, 3.4):
            s.entity("Tokcha", parent=shelf, pos=(0, ly, 0), scale=(4.2, 0.1, 1.2), MeshRenderer=k.mesh("Cube", (0.3, 0.32, 0.36, 1), 0.5, 0.6))
            s.entity("Yuk", parent=shelf, pos=((i % 3) - 1.0, ly + 0.45, 0), scale=(0.8, 0.8, 0.8), MeshRenderer=k.mesh("Cube", (0.85, 0.85, 0.85, 1), 0.7, 0.0, **crate))
    # hanging lamps
    for i, (x, z) in enumerate(((-7, -4), (7, -4), (0, -8))):
        lamp = s.entity(f"Chiroq {i + 1}", parent=env, pos=(x, 6.5, z), PointLight=k.point_light((1.0, 0.75, 0.45), 6.0, 13.0))
        s.entity("Abajur", parent=lamp, pos=(0, 0.25, 0), scale=(0.9, 0.35, 0.9), MeshRenderer=k.mesh("Cone", (0.15, 0.25, 0.2, 1), 0.4, 0.5, cast_shadows=False))
        s.entity("Lampochka", parent=lamp, scale=(0.3, 0.3, 0.3),
                 MeshRenderer=k.mesh("Sphere", (1, 0.85, 0.6, 1), emission=(1.0, 0.75, 0.45), emission_strength=10.0, cast_shadows=False))
    s.entity("Chang", pos=(0, 2, 0), ParticleSystem=k.particles(
        EmissionRate=6.0, MaxParticles=150, Shape=3, ShapeExtents=(10, 3, 8), LifetimeMin=6.0, LifetimeMax=10.0, SpeedMin=0.05,
        SpeedMax=0.2, Direction=(0, 1, 0), Spread=180.0, StartColor=(1.0, 0.85, 0.6, 0.35), EndColor=(1.0, 0.8, 0.6, 0.0),
        StartSize=0.05, EndSize=0.05, Prewarm=True))

    # ---- the level's blocks: templates (switched off), the game copies them under "Daraja"
    templates = s.entity("Shablonlar", active=False)
    s.entity("Devor", parent=templates, pos=(0, 0.42, 0), scale=(1, 0.84, 1),
             MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.85, 0.0, albedo_map="Textures/brick.png", normal_map="Textures/brick_normal.png"))
    s.entity("Pol plitasi", parent=templates, pos=(0, -0.06, 0), scale=(0.98, 0.12, 0.98),
             MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.7, 0.0, albedo_map="Textures/floor.png", normal_map="Textures/floor_normal.png"))
    goal = s.entity("Maqsad", parent=templates, pos=(0, 0.012, 0), scale=(0.78, 0.02, 0.78),
                    MeshRenderer=k.mesh("Cylinder", (0.4, 0.85, 1.0, 1), 0.3, 0.0, emission=(0.25, 0.8, 1.0), emission_strength=3.0,
                                        albedo_map="Textures/goal.png", cast_shadows=False))
    s.get(goal)["MeshRenderer"]["EmissiveMap"] = "Textures/goal.png"
    s.entity("Maqsad chirog'i", parent=goal, pos=(0, 30, 0), PointLight=k.point_light((0.25, 0.8, 1.0), 1.4, 2.2))
    s.entity("Quti", parent=templates, pos=(0, 0.42, 0), scale=(0.84, 0.84, 0.84),
             MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.65, 0.0, **crate))
    board = s.entity("Daraja")

    # ---- the robot (its root on the floor, facing +Z; the body bobs and leans)
    robot = s.entity("Robot")
    body = s.entity("Robot tanasi", parent=robot)
    orange = (1.0, 0.5, 0.12, 1)
    white = (0.92, 0.93, 0.95, 1)
    s.entity("Tana", parent=body, pos=(0, 0.36, 0), scale=(0.5, 0.42, 0.46), MeshRenderer=k.mesh("Cube", orange, 0.4, 0.3))
    s.entity("Ko'krak chirog'i", parent=body, pos=(0, 0.4, 0.235), scale=(0.2, 0.08, 0.02),
             MeshRenderer=k.mesh("Cube", (0.4, 1.0, 0.6, 1), emission=(0.3, 1.0, 0.5), emission_strength=3.0, cast_shadows=False))
    s.entity("Bosh", parent=body, pos=(0, 0.74, 0), scale=(0.42, 0.34, 0.4), MeshRenderer=k.mesh("Sphere", white, 0.3, 0.2))
    eye = s.entity("Ko'zlar", parent=body, pos=(0, 0.76, 0.18), scale=(0.3, 0.09, 0.06),
                   MeshRenderer=k.mesh("Cube", (0.4, 0.9, 1.0, 1), emission=(0.3, 0.85, 1.0), emission_strength=6.0, cast_shadows=False))
    s.entity("Antenna", parent=body, pos=(0, 0.98, 0), scale=(0.03, 0.18, 0.03), MeshRenderer=k.mesh("Cylinder", (0.3, 0.3, 0.32, 1), 0.4, 0.8))
    s.entity("Antenna uchi", parent=body, pos=(0, 1.09, 0), scale=(0.08, 0.08, 0.08),
             MeshRenderer=k.mesh("Sphere", (1, 0.3, 0.2, 1), emission=(1.0, 0.25, 0.15), emission_strength=6.0, cast_shadows=False))
    for side in (-1, 1):
        s.entity("G'ildirak", parent=body, pos=(0.27 * side, 0.14, 0), rot=(0, 0, math.radians(90)), scale=(0.26, 0.08, 0.26),
                 MeshRenderer=k.mesh("Cylinder", (0.12, 0.12, 0.14, 1), 0.6, 0.2))
        s.entity("Qo'l", parent=body, pos=(0.29 * side, 0.4, 0.08), scale=(0.08, 0.08, 0.3), MeshRenderer=k.mesh("Cube", white, 0.4, 0.2))
    s.entity("Robot chirog'i", parent=robot, pos=(0, 1.4, 0.4), PointLight=k.point_light((1.0, 0.7, 0.4), 1.2, 3.0))

    # ---- effects
    fx = s.entity("Effektlar")
    dust = s.entity("Chang bulut", parent=fx, pos=(0, -20, 0), ParticleSystem=k.particles(
        Playing=True, EmissionRate=0.0, MaxParticles=300, Shape=2, ShapeRadius=0.25, LifetimeMin=0.4, LifetimeMax=0.8, SpeedMin=0.4,
        SpeedMax=1.2, Direction=(0, 1, 0), Spread=70.0, Drag=2.0, StartColor=(0.75, 0.65, 0.55, 0.5), EndColor=(0.6, 0.55, 0.5, 0.0),
        StartSize=0.18, EndSize=0.4, Additive=False))
    burst = s.entity("Maqsad uchqunlari", parent=fx, pos=(0, -20, 0), ParticleSystem=k.particles(
        Playing=True, EmissionRate=0.0, MaxParticles=400, Shape=2, ShapeRadius=0.3, LifetimeMin=0.5, LifetimeMax=1.0, SpeedMin=1.0,
        SpeedMax=3.0, Direction=(0, 1, 0), Spread=40.0, Gravity=(0, -2, 0), StartColor=(0.6, 2.4, 1.2, 1.0), EndColor=(0.3, 1.0, 0.5, 0.0),
        StartSize=0.1, EndSize=0.0))
    confetti = s.entity("Konfetti", parent=fx, pos=(0, -20, 0), ParticleSystem=k.particles(
        Playing=True, EmissionRate=0.0, MaxParticles=600, Shape=2, ShapeRadius=0.3, LifetimeMin=1.2, LifetimeMax=2.2, SpeedMin=3.0,
        SpeedMax=6.0, Direction=(0, 1, 0), Spread=45.0, Gravity=(0, -6, 0), Drag=0.8, StartColor=(2.0, 1.4, 0.4, 1.0),
        EndColor=(1.0, 0.3, 0.8, 0.0), StartSize=0.12, EndSize=0.06, AngularVelocityMax=6.0))

    # ---- UI
    game = s.entity("Game")
    canvas = s.entity("UI", UITransform=k.ui_stretch(), UICanvas=k.ui_canvas())
    ui = k.UIKit(s, canvas, ATLAS, game, (0.85, 0.5, 0.15, 1.0), (1.0, 0.65, 0.25, 1.0), (0.6, 0.32, 0.08, 1.0))
    hud = s.entity("HUD", parent=canvas, UITransform=k.ui_stretch())
    s.entity("Yuqori fon", parent=hud, UITransform=k.ui_rect((0, 1), (1, 1), (0.5, 1), (0, 0), (0, 140)),
             UIImage=k.ui_image((0.0, 0.0, 0.0, 0.35), raycast=False))
    level_text = ui.text("Daraja nomi", hud, "", (0, 1), (0, 1), (50, -34), (900, 52), 44, (1.0, 0.85, 0.55, 1.0))
    best = ui.text("Rekord", hud, "", (0, 1), (0, 1), (50, -88), (900, 34), 28, (0.85, 0.85, 0.9, 0.9))
    boxes = ui.text("Qutilar", hud, "", (0.5, 1), (0.5, 1), (0, -36), (700, 60), 50, h="Center")
    moves = ui.text("Qadamlar", hud, "", (1, 1), (1, 1), (-50, -34), (600, 46), 40, h="Right")
    pushes = ui.text("Surishlar", hud, "", (1, 1), (1, 1), (-50, -84), (600, 36), 30, (0.85, 0.85, 0.9, 0.9), h="Right")
    s.entity("Pastki fon", parent=hud, UITransform=k.ui_rect((0, 0), (1, 0), (0.5, 0), (0, 0), (0, 80)),
             UIImage=k.ui_image((0.0, 0.0, 0.0, 0.3), raycast=False))
    hint = ui.text("Maslahat", hud, "", (0.5, 0), (0.5, 0), (0, 40), (1700, 36), 26, (0.9, 0.9, 1.0, 0.85), h="Center")
    banner = ui.text("E'lon", hud, "", (0.5, 0.5), (0.5, 0.5), (0, 250), (1400, 110), 92, (1.0, 0.92, 0.7, 1.0), h="Center",
                     UIAnimation=k.ui_anim("Pop", "Fade", 0.35))
    banner_sub = ui.text("E'lon izohi", banner, "", (0.5, 0.0), (0.5, 1.0), (0, -4), (1200, 46), 36, (1.0, 0.8, 0.5, 1.0), h="Center")
    demo_badge = ui.text("Demo belgisi", hud, "DEMO - avtomatik o'yin", (1, 0), (1, 0), (-50, 110), (600, 32), 26, (1.0, 0.8, 0.35, 0.9),
                         h="Right", key="hud.demo", active=False)

    main_menu = ui.dim("Bosh menyu", True, 0.35, (0.02, 0.02, 0.05))
    title = s.entity("Sarlavha", parent=main_menu, UITransform=k.ui_rect(pos=(0, 300), size=(1700, 180), font=156),
                     UIAnimation=k.ui_anim("SlideDown", "None", 0.6, slide=120))
    s.entity("Sarlavha soyasi", parent=title, UITransform=k.ui_rect((0, 0), (1, 1), pos=(6, -7), size=(0, 0), font=156),
             Text=k.ui_text("OMBORCHI", (0.4, 0.18, 0.04, 0.9), "Center", key="menu.title"))
    s.entity("Sarlavha matni", parent=title, UITransform=k.ui_rect((0, 0), (1, 1), size=(0, 0), font=156),
             Text=k.ui_text("OMBORCHI", (1.0, 0.75, 0.35, 1.0), "Center", key="menu.title"))
    s.entity("Taglavha", parent=main_menu, UITransform=k.ui_rect(pos=(0, 186), size=(1500, 50), font=38),
             Text=k.ui_text("Sokoban - qutilarni joyiga itaring  ·  Zamin Engine namunasi", (1.0, 0.92, 0.85, 0.95), "Center", key="menu.subtitle"))
    menu_panel = ui.panel("Menyu paneli", main_menu, (0, -90), (560, 420))
    play_button = ui.button("O'ynash", menu_panel, "O'ynash", "menu.play", "Play", True)
    ui.button("Darajalar", menu_panel, "Darajalar", "menu.levels", "Levels")
    language_button = ui.button("Til", menu_panel, "Til: O'zbekcha", None, "Language")
    ui.button("Chiqish", menu_panel, "Chiqish", "menu.quit", "Quit")
    progress = s.entity("Yechilganlar", parent=main_menu, UITransform=k.ui_rect(pos=(0, -340), size=(1200, 44), font=32),
                        Text=k.ui_text("", (1.0, 0.85, 0.4, 1.0), "Center"))
    s.entity("Boshqaruv", parent=main_menu, UITransform=k.ui_rect((0.5, 0), None, (0.5, 0), (0, 50), (1800, 70), font=25),
             Text=k.ui_text(UZ["menu"]["controls"], (0.95, 0.9, 0.85, 0.9), "Center", key="menu.controls"))

    levels_menu = ui.dim("Darajalar menyusi", False, 0.55)
    levels_panel = ui.panel("Darajalar paneli", levels_menu, (0, 0), (900, 600))
    ui.label("Darajalar sarlavhasi", levels_panel, "DARAJALAR", (800, 80), 60, (1.0, 0.8, 0.45, 1.0), "levels.title")
    grid = s.entity("Daraja tugmalari", parent=levels_panel, UITransform=k.ui_rect(size=(800, 270)),
                    UILayoutGroup={"Type": "Grid", "Padding": [10, 10, 10, 10], "Alignment": 4, "FitWidth": False, "FitHeight": False,
                                   "CellSize": [140, 110], "GridSpacing": [16, 16], "StartCorner": 0, "StartAxisVertical": False,
                                   "Constraint": "FixedColumnCount", "ConstraintCount": 5})
    n, h, p = (0.3, 0.45, 0.85, 1.0), (0.45, 0.6, 1.0, 1.0), (0.2, 0.3, 0.6, 1.0)
    for i in range(LEVEL_COUNT):
        s.entity(f"Daraja {i + 1}", parent=grid, UITransform=k.ui_rect(size=(140, 110), font=34),
                 UIButton=k.ui_button(n, h, p, f"Level{i + 1}", game),
                 UIImage=k.ui_image(texture=ATLAS, frame="button", kind="Sliced", border_scale=1.0),
                 Text=k.ui_text(str(i + 1), (1, 1, 1, 1), "Center"), UINavigation=k.ui_nav("Automatic"),
                 UIAnimation=k.ui_anim(hover=1.06, press=0.94))
    back_button = ui.button("Orqaga", levels_panel, "Orqaga", "levels.back", "LevelsBack")

    pause_menu = ui.dim("Pauza menyusi", False)
    pause_panel = ui.panel("Pauza paneli", pause_menu, (0, 0), (620, 460))
    ui.label("Pauza sarlavhasi", pause_panel, "PAUZA", (520, 80), 60, (1.0, 0.8, 0.45, 1.0), "pause.title")
    resume_button = ui.button("Davom etish", pause_panel, "Davom etish", "pause.resume", "Resume", True)
    ui.button("Qaytadan", pause_panel, "Qaytadan", "pause.restart", "Restart")
    ui.button("Bosh menyuga", pause_panel, "Bosh menyu", "pause.menu", "Menu")

    complete_menu = ui.dim("Natija oynasi", False, 0.45)
    complete_panel = ui.panel("Natija paneli", complete_menu, (0, -40), (760, 560))
    complete_title = ui.label("Natija sarlavhasi", complete_panel, "", (680, 84), 56, (0.6, 1.0, 0.7, 1.0))
    complete_stats = ui.label("Natija statistikasi", complete_panel, "", (680, 44), 32, (1.0, 0.95, 0.85, 1.0))
    complete_record = ui.label("Natija rekordi", complete_panel, "", (680, 40), 30, (1.0, 0.7, 0.3, 1.0))
    next_button = ui.button("Keyingi", complete_panel, "Keyingi daraja", "complete.next", "Next", True)
    ui.button("Yana bir bor", complete_panel, "Qaytadan", "pause.restart", "Restart")
    ui.button("Natijadan menyuga", complete_panel, "Bosh menyu", "pause.menu", "Menu")

    s.add(game, "NativeScript", k.script(
        "SokobanGame", BoardRoot=k.ref(board), WallTemplate=k.ref(s.children[templates][0]), FloorTemplate=k.ref(s.children[templates][1]),
        GoalTemplate=k.ref(goal), BoxTemplate=k.ref(s.children[templates][3]), Robot=k.ref(robot), RobotBody=k.ref(body), RobotEye=k.ref(eye),
        CameraRig=k.ref(camera), Dust=k.ref(dust), GoalBurst=k.ref(burst), Confetti=k.ref(confetti), Hud=k.ref(hud),
        LevelText=k.ref(level_text), MovesText=k.ref(moves), PushesText=k.ref(pushes), BoxesText=k.ref(boxes), BestText=k.ref(best),
        HintText=k.ref(hint), Banner=k.ref(banner), BannerSub=k.ref(banner_sub), DemoBadge=k.ref(demo_badge), MainMenu=k.ref(main_menu),
        MenuProgress=k.ref(progress), PlayButton=k.ref(play_button), LanguageButton=k.ref(language_button), LevelsMenu=k.ref(levels_menu),
        LevelGrid=k.ref(grid), LevelsBackButton=k.ref(back_button), PauseMenu=k.ref(pause_menu), ResumeButton=k.ref(resume_button),
        CompleteMenu=k.ref(complete_menu), CompleteTitle=k.ref(complete_title), CompleteStats=k.ref(complete_stats),
        CompleteRecord=k.ref(complete_record), NextButton=k.ref(next_button)))
    s.add(game, "AudioSource", k.audio_source("Audio/music.wav", 0.55, loop=True, play=True, output="Music", stream=True))
    return s


UZ = {
    "$name": "O'zbekcha",
    "menu": {"title": "OMBORCHI", "subtitle": "Sokoban - qutilarni joyiga itaring  ·  Zamin Engine namunasi", "play": "O'ynash",
             "levels": "Darajalar", "quit": "Chiqish", "language": "Til: {0}", "progress": "Yechilgan darajalar: {0} / {1}",
             "controls": "WASD / strelkalar - yurish (qutini itarish)   ·   Z / Backspace - orqaga   ·   R - qaytadan   ·   Esc - pauza\n"
                         "Gamepad: D-pad / chap stik - yurish, X - orqaga, Y - qaytadan, Start - pauza"},
    "hud": {"demo": "DEMO - avtomatik o'yin", "level": "{0}-DARAJA", "level_name": "{0}-daraja  ·  {1}", "moves": "Qadamlar: {0}",
            "pushes": "Surishlar: {0}", "boxes": "Qutilar: {0} / {1}", "best": "Rekord: {0} qadam", "no_best": "Rekord: hali yo'q",
            "hint": "Hamma qutilarni yorqin doiralarga itaring  ·  Z - orqaga  ·  R - qaytadan  ·  Esc - pauza", "solved": "YECHILDI!",
            "solved_sub": "{0} qadam  ·  {1} surish", "all_done": "HAMMASI YECHILDI!"},
    "levels": {"title": "DARAJALAR", "back": "Orqaga", "moves": "{0} qadam"},
    "pause": {"title": "PAUZA", "resume": "Davom etish", "restart": "Qaytadan", "menu": "Bosh menyu"},
    "complete": {"title": "{0}-DARAJA YECHILDI!", "all": "BARCHA DARAJALAR YECHILDI!", "stats": "{0} qadam  ·  {1} surish",
                 "record": "Yangi rekord!", "best": "Rekord: {0} qadam", "next": "Keyingi daraja"},
}
EN = {
    "$name": "English",
    "menu": {"title": "WAREHOUSE KEEPER", "subtitle": "Sokoban - push every crate into place  ·  a Zamin Engine sample", "play": "Play",
             "levels": "Levels", "quit": "Quit", "language": "Language: {0}", "progress": "Levels solved: {0} / {1}",
             "controls": "WASD / arrows - move (push a crate)   ·   Z / Backspace - undo   ·   R - restart   ·   Esc - pause\n"
                         "Gamepad: D-pad / left stick - move, X - undo, Y - restart, Start - pause"},
    "hud": {"demo": "DEMO - autoplay", "level": "LEVEL {0}", "level_name": "Level {0}  ·  {1}", "moves": "Moves: {0}", "pushes": "Pushes: {0}",
            "boxes": "Crates: {0} / {1}", "best": "Record: {0} moves", "no_best": "Record: none yet",
            "hint": "Push every crate onto a glowing ring  ·  Z - undo  ·  R - restart  ·  Esc - pause", "solved": "SOLVED!",
            "solved_sub": "{0} moves  ·  {1} pushes", "all_done": "ALL SOLVED!"},
    "levels": {"title": "LEVELS", "back": "Back", "moves": "{0} moves"},
    "pause": {"title": "PAUSED", "resume": "Resume", "restart": "Restart", "menu": "Main menu"},
    "complete": {"title": "LEVEL {0} SOLVED!", "all": "EVERY LEVEL SOLVED!", "stats": "{0} moves  ·  {1} pushes", "record": "New record!",
                 "best": "Record: {0} moves", "next": "Next level"},
}


def main():
    levels = read_levels()
    assert len(levels) == LEVEL_COUNT, f"{len(levels)} levels in Levels.cpp, expected {LEVEL_COUNT}"
    for i, lv in enumerate(levels):
        pushes = check_level(lv)
        assert pushes, f"level {i + 1} has no solution"
        print(f"  {i + 1}-daraja: {pushes} surishda yechiladi")
    make_textures()
    make_ui_atlas()
    make_icon()
    make_sounds()
    k.write_json(os.path.join(ASSETS, "Localization/uz.json"), UZ)
    k.write_json(os.path.join(ASSETS, "Localization/en.json"), EN)
    scene = build_scene()
    uuid = scene.save(ASSETS, "Scenes/Warehouse.zscene", NAME)
    actions = [
        ("Undo", ["Key:Z", "Key:Backspace", "Gamepad:X"]),
        ("Restart", ["Key:R", "Gamepad:Y"]),
        ("Pause", ["Key:Escape", "Key:P", "Gamepad:Start"]),
    ]
    axes = [
        k.axis("MoveX", ["Key:A", "Key:Left", "Gamepad:DPadLeft"], ["Key:D", "Key:Right", "Gamepad:DPadRight"], "LeftX", deadzone=0.4),
        k.axis("MoveY", ["Key:S", "Key:Down", "Gamepad:DPadDown"], ["Key:W", "Key:Up", "Gamepad:DPadUp"], "LeftY", invert=True, deadzone=0.4),
    ]
    vectors = [{"Name": "Move", "X": "MoveX", "Y": "MoveY", "Mode": "Clamp", "Deadzone": 0.0}]
    k.write_project(PROJECT, NAME, "Omborchi", "Scenes/Warehouse.zscene", uuid, "1.0.0", [("Scenes/Warehouse.zscene", uuid)], actions, axes,
                    vectors, description="3D Sokoban: qutilarni joyiga itaring - 10 ta daraja, orqaga qaytarish (undo), "
                                         "darajalar menyusi va yechuvchi bot")
    k.write_module_cmake(PROJECT, NAME, "Zamin namuna o'yini: Omborchi (Sokoban) - C++ skriptlar moduli (libSokoban.so)")
    print("Sokoban: tayyor")


if __name__ == "__main__":
    main()
