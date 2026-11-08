"""Projects/FPSArena: "FPS Arena" - its textures, sounds, scene and project file.

    python3 Tools/samples/gen_fpsarena.py

Everything under Projects/FPSArena/Assets and the .zproject are made here; the first-person controller and
the parkour course are C++ (Projects/FPSArena/Source).
"""

import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import samplekit as k  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NAME = "FPSArena"
PROJECT = os.path.join(ROOT, "Projects", NAME)
ASSETS = os.path.join(PROJECT, "Assets")
ATLAS = "Textures/ui_atlas.png"

# ------------------------------------------------------------------------------------------- textures


def save_texture(rel, img):
    k.write_png(os.path.join(ASSETS, rel), np.clip(img, 0, 1))
    k.write_zmeta(ASSETS, rel, "Texture", {}, NAME)


def make_textures():
    s = 256
    ys, xs = np.mgrid[0:s, 0:s].astype(np.float64)

    # concrete floor tiles: 2 x 2 slabs with seams
    n = k.value_noise(s, s, 8, seed=21, octaves=5)
    n2 = k.value_noise(s, s, 3, seed=22, octaves=3)
    px, py = (xs % 128) - 63.5, (ys % 128) - 63.5
    seam = k.smooth(-61.5, -63.5, -np.maximum(np.abs(px), np.abs(py)))  # 1 near the slab edge
    height = 0.6 + 0.25 * n + 0.15 * n2 - 0.55 * seam
    img = k.image(s, s, (0, 0, 0, 1))
    base = np.array([0.52, 0.54, 0.58])
    for i in range(3):
        img[:, :, i] = base[i] * (0.72 + 0.4 * n + 0.15 * n2) * (1.0 - 0.45 * seam)
    save_texture("Textures/floor.png", img)
    save_texture("Textures/floor_normal.png", k.normal_map_from_height(height, 3.0))

    # wall panels: 4 vertical panels with bevels, rivets and a dark stripe
    n = k.value_noise(s, s, 6, seed=31, octaves=4)
    qx = (xs % 64) - 31.5
    qy = ys - 127.5
    panel = k.sd_round_box(qx, qy, 29, 124, 4)
    bevel = np.clip(-panel / 4.0, 0, 1)
    rivets = np.zeros_like(xs)
    for ry in (-112, -40, 40, 112):
        for rx in (-22, 22):
            d = np.hypot(qx - rx, qy - ry)
            rivets = np.maximum(rivets, k.smooth(3.0, 1.5, d))
    stripe = k.smooth(2, 0, np.abs(ys - 200) - 10)
    height = 0.3 + 0.5 * bevel + 0.2 * rivets + 0.05 * n
    img = k.image(s, s, (0, 0, 0, 1))
    base = np.array([0.36, 0.4, 0.47])
    for i in range(3):
        img[:, :, i] = base[i] * (0.65 + 0.35 * bevel + 0.2 * n) + rivets * 0.15
        img[:, :, i] = img[:, :, i] * (1 - stripe) + stripe * np.array([0.95, 0.6, 0.15])[i] * (0.8 + 0.2 * n)
    save_texture("Textures/wall.png", img)
    save_texture("Textures/wall_normal.png", k.normal_map_from_height(height, 4.0))

    # crate: a frame and a cross over panels
    n = k.value_noise(s, s, 6, seed=41, octaves=4)
    x, y = k.coords(s, s)
    frame = np.maximum(np.abs(x), np.abs(y)) > 104
    cross = (np.abs(np.abs(x) - np.abs(y)) < 14) & ~frame
    height = np.where(frame | cross, 0.9, 0.4) + 0.08 * n
    img = k.image(s, s, (0, 0, 0, 1))
    body = np.array([0.3, 0.34, 0.4])
    trim = np.array([0.95, 0.55, 0.15])
    for i in range(3):
        img[:, :, i] = np.where(frame | cross, trim[i] * (0.75 + 0.25 * n), body[i] * (0.7 + 0.3 * n))
    edge = k.smooth(2.5, 0.0, np.abs(np.maximum(np.abs(x), np.abs(y)) - 104))
    for i in range(3):
        img[:, :, i] *= 1 - 0.5 * edge
    save_texture("Textures/crate.png", img)
    save_texture("Textures/crate_normal.png", k.normal_map_from_height(height, 3.0))

    # jump pad: concentric emissive rings
    img = k.image(s, s, (0, 0, 0, 1))
    r = np.hypot(x, y)
    rings = (np.sin(r / 9.0) * 0.5 + 0.5) ** 6 * (r < 120)
    for i, c in enumerate((0.25, 0.9, 1.0)):
        img[:, :, i] = 0.08 + rings * c
    save_texture("Textures/pad.png", img)


def make_ui_atlas():
    atlas = k.Atlas(512, 256)
    k.ui_panel_sprites(atlas, (0.06, 0.09, 0.14), (0.02, 0.03, 0.06), (0.95, 0.65, 0.2), (1.0, 0.6, 0.2))
    # crosshair: a dot and four ticks
    img = k.image(64, 64)
    x, y = k.coords(64, 64)
    sd = k.sd_circle(x, y, 2.5)
    for dx, dy in ((0, -1), (0, 1), (1, 0), (-1, 0)):
        sd = np.minimum(sd, k.sd_round_box(x - dx * 13, y - dy * 13, 1.5 if dx == 0 else 5, 5 if dx == 0 else 1.5, 1))
    k.glow(img, sd, (0, 0, 0, 1), 2.5, 0.7)
    k.fill(img, sd, (1, 1, 1, 1))
    atlas.add("crosshair", img)
    return atlas.save(ASSETS, ATLAS, NAME)


def make_icon():
    size = 256
    img = k.image(size, size)
    x, y = k.coords(size, size)
    sd = k.sd_round_box(x, y, 120, 120, 44)
    bg = k.mix_color((0.95, 0.55, 0.2), (0.35, 0.12, 0.05), np.clip(y / size + 0.5, 0, 1))
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = bg[:, :, i]
    img[:, :, 3] = cov
    ring = np.abs(k.sd_circle(x, y, 62)) - 8
    k.fill(img, ring, (1, 1, 1, 1))
    for dx, dy in ((0, -1), (0, 1), (1, 0), (-1, 0)):
        k.fill(img, k.sd_round_box(x - dx * 70, y - dy * 70, 8 if dx == 0 else 26, 26 if dx == 0 else 8, 4), (1, 1, 1, 1))
    k.fill(img, k.sd_circle(x, y, 12), (1, 1, 1, 1))
    img[:, :, 3] = np.minimum(img[:, :, 3], cov)
    save_texture("Textures/GameIcon.png", img)


# --------------------------------------------------------------------------------------------- sounds


def make_sounds():
    R = k.RATE
    out = {}
    for i in range(4):
        n = int(0.16 * R)
        thump = k.lowpass(k.noise(0.16, 50 + i), 700 + 150 * i) * k.exp_decay(n, 0.035)
        click = k.highpass(k.noise(0.16, 60 + i), 3000) * k.exp_decay(n, 0.008) * 0.25
        out[f"step{i + 1}"] = thump + click
    n = int(0.3 * R)
    out["jump"] = k.lowpass(k.noise(0.3, 7), np.linspace(400, 2500, n)) * np.sin(np.linspace(0, np.pi, n)) * 0.7 + \
        k.osc(np.linspace(180, 320, n), 0.3, "sine") * k.exp_decay(n, 0.08) * 0.4
    n = int(0.35 * R)
    out["land"] = k.lowpass(k.noise(0.35, 8), 500) * k.exp_decay(n, 0.06) + k.osc(np.linspace(90, 50, n), 0.35, "sine") * k.exp_decay(n, 0.07)
    n = int(0.7 * R)
    f = np.linspace(160, 900, n)
    out["jumppad"] = k.osc(f, 0.7, "sine") * k.exp_decay(n, 0.3) + 0.4 * k.osc(f * 1.5, 0.7, "tri") * k.exp_decay(n, 0.15)
    buf = np.zeros(int(0.8 * R))
    for i, note in enumerate(("E6", "B6")):
        k.place(buf, (k.osc(k.note_freq(note), 0.5, "sine") + 0.3 * k.osc(k.note_freq(note) * 2, 0.5, "sine")) * k.exp_decay(int(0.5 * R), 0.18), i * 0.09, 0.6)
    out["checkpoint"] = buf
    buf = np.zeros(int(1.6 * R))
    for i, note in enumerate(("C5", "E5", "G5", "C6", "E6", "G6")):
        k.place(buf, k.osc(k.note_freq(note), 0.6, "square") * k.exp_decay(int(0.6 * R), 0.25), i * 0.08, 0.3)
    out["lap"] = k.lowpass(buf, 5000)
    n = int(0.06 * R)
    out["ui_click"] = k.osc(1000, 0.06, "square") * k.exp_decay(n, 0.012) * 0.6 + k.osc(2000, 0.06, "sine") * k.exp_decay(n, 0.006) * 0.4
    for name, snd in out.items():
        rel = f"Audio/{name}.wav"
        k.write_wav(os.path.join(ASSETS, rel), snd)
        k.write_zmeta(ASSETS, rel, "Audio", {"LoadMode": "Decompress", "Preload": True, "Volume": 1.0}, NAME)
    # ambience: a slow synth pad (Dm9 - Bbmaj7), 16 s loop
    seconds = 16.0
    total = int(seconds * R)
    buf = np.zeros(total + R)
    for half, chord in enumerate((("D3", "F3", "A3", "E4"), ("A#2", "D3", "F3", "A3"))):
        seg = np.zeros(int(8.0 * R))
        for note in chord:
            for det in (0.996, 1.004):
                seg += k.osc(k.note_freq(note) * det, 8.0, "saw")
        lfo = 900 + 500 * np.sin(np.linspace(0, 2 * np.pi, len(seg)))
        seg = k.lowpass(seg, lfo) * k.env(len(seg), 1.5, 1.0, 0.85, 4.5, 1.0)
        k.place(buf, seg, half * 8.0, 0.25)
    buf[:R] += buf[total:total + R]
    rel = "Audio/ambience.wav"
    k.write_wav(os.path.join(ASSETS, rel), buf[:total], peak=0.6)
    k.write_zmeta(ASSETS, rel, "Audio", {"LoadMode": "Stream", "Preload": False, "Volume": 1.0}, NAME)


# ---------------------------------------------------------------------------------------------- scene

FLOOR = "Textures/floor.png"
WALL = "Textures/wall.png"
CRATE = "Textures/crate.png"

# The parkour course: checkpoints in order (x, y, z) - the floating markers.
CHECKPOINTS = [(0, 1.3, -17), (0, 3.3, -1.5), (11, 1.3, 0), (13, 1.3, 15), (-12, 8.0, 12), (-17, 1.3, -8)]
# The bot's route (it walks straight between these): (x, y, z) on the floor / platform.
ROUTE = [(0, 0, -17), (0, 0, -12), (0, 2, -5), (0, 2, 0), (4.4, 2, 0), (9.5, 0, 0), (11, 0, 0), (13, 0, 8), (13, 0, 15),
         (2, 0, 17), (-12, 0, 12), (-17, 0, 2), (-17, 0, -8), (-8, 0, -17)]


def build_scene():
    s = k.SceneBuilder("Arena", 8830000000000000)
    s.settings = {
        "DrawSky": True, "SkyTop": [0.1, 0.2, 0.46], "SkyHorizon": [0.92, 0.62, 0.42], "Ground": [0.22, 0.2, 0.2],
        "Ambient": 0.5, "Exposure": 1.0, "ShadowDistance": 60,
        "Sky": {"Curve": 1.0, "GroundFade": 1.0, "SunDisc": True, "SunSize": 1.0, "SunIntensity": 1.0, "SunGlow": 1.0},
        "Fog": {"Enabled": True, "Color": [0.62, 0.52, 0.5], "Density": 0.0055, "HeightFalloff": 0.1},
        "Post": {"Tonemap": 1, "Bloom": True, "BloomThreshold": 1.0, "BloomKnee": 0.5, "BloomIntensity": 0.6, "BloomRadius": 1.0,
                 "Vignette": 0.28, "Saturation": 1.1, "Contrast": 1.06, "Brightness": 0.0, "Tint": [1, 1, 1], "SSAO": True,
                 "SSAORadius": 0.6, "SSAOIntensity": 1.2, "SSAOBias": 0.03, "FXAA": True},
        "Gravity3D": [0, -9.81, 0],
    }
    s.metadata = {"Description": "FPS Arena: birinchi shaxs boshqaruvi - sichqoncha bilan qarash, yugurish, sakrash, cho'kkalash, "
                                 "sakratgich va parkur yo'lagi", "Author": "Zamin", "Tags": ["namuna", "3D", "fps"]}

    def solid(name, parent, pos, scale, tex, tiling=1.0, rot=(0, 0, 0), albedo=(1, 1, 1, 1), normal=None, rough=0.75, metal=0.0,
              collider=True, emission=(0, 0, 0), emission_strength=0.0, mesh_kind="Cube"):
        comps = {"MeshRenderer": k.mesh(mesh_kind, albedo, rough, metal, emission, emission_strength, tex, normal or "",
                                        tiling, normal_strength=1.0)}
        if collider:
            comps["Rigidbody3D"] = k.rigidbody3d("Static")
            comps["BoxCollider3D"] = k.box3d((0.5, 0.5, 0.5), friction=0.8)
        return s.entity(name, parent=parent, pos=pos, rot=rot, scale=scale, **comps)

    # ---- light
    s.entity("Quyosh", rot=(math.radians(-32), math.radians(35), 0), DirectionalLight=k.dir_light((1.0, 0.8, 0.6), 3.4, True))

    arena = s.entity("Arena")
    solid("Pol", arena, (0, -0.5, 0), (50, 1, 50), FLOOR, (12, 12), normal="Textures/floor_normal.png", rough=0.85)
    for name, pos, scale in (("Devor (shimol)", (0, 3, -25), (50, 6, 1)), ("Devor (janub)", (0, 3, 25), (50, 6, 1)),
                             ("Devor (g'arb)", (-25, 3, 0), (1, 6, 50)), ("Devor (sharq)", (25, 3, 0), (1, 6, 50))):
        tiling = (scale[0] / 3 if scale[0] > 1 else scale[2] / 3, 2)
        solid(name, arena, pos, scale, WALL, tiling, normal="Textures/wall_normal.png", rough=0.55, metal=0.3)
    # emissive light strips along the walls
    for name, pos, scale in (("Chiroq tasmasi 1", (0, 5.6, -24.45), (46, 0.12, 0.1)), ("Chiroq tasmasi 2", (0, 5.6, 24.45), (46, 0.12, 0.1)),
                             ("Chiroq tasmasi 3", (-24.45, 5.6, 0), (0.1, 0.12, 46)), ("Chiroq tasmasi 4", (24.45, 5.6, 0), (0.1, 0.12, 46))):
        solid(name, arena, pos, scale, "", collider=False, albedo=(1, 0.6, 0.2, 1), emission=(1.0, 0.55, 0.15), emission_strength=6.0)

    # central platform with a ramp (north) and stairs (east)
    plat = s.entity("Platforma", parent=arena)
    solid("Platforma tanasi", plat, (0, 1, 0), (10, 2, 10), WALL, (3, 1), normal="Textures/wall_normal.png", rough=0.5, metal=0.25)
    solid("Platforma usti", plat, (0, 2.02, 0), (10.2, 0.06, 10.2), FLOOR, (3, 3), normal="Textures/floor_normal.png", collider=False,
          albedo=(0.85, 0.88, 0.95, 1))
    angle = math.atan2(2.0, 7.0)
    length = math.hypot(2.0, 7.0)
    solid("Qiya yo'lak", plat, (0, 1.0 - 0.2 / math.cos(angle), -8.5), (4, 0.4, length), FLOOR, (1.5, 3), rot=(-angle, 0, 0),
          normal="Textures/floor_normal.png", albedo=(0.95, 0.85, 0.7, 1))
    for i in range(7):
        h = 2.0 - i * (2.0 / 7.0)
        solid(f"Zina {i + 1}", plat, (5.25 + i * 0.5, h / 2, 0), (0.5, h, 4), FLOOR, (0.3, 1), normal="Textures/floor_normal.png",
              albedo=(0.9, 0.9, 0.95, 1))
    for cx, cz in ((-4.6, -4.6), (4.6, -4.6), (-4.6, 4.6), (4.6, 4.6)):
        s.entity("Platforma chirog'i", parent=plat, pos=(cx, 2.6, cz), PointLight=k.point_light((1.0, 0.6, 0.25), 3.0, 6.0),
                 MeshRenderer=k.mesh("Sphere", (1, 0.7, 0.4, 1), emission=(1.0, 0.6, 0.25), emission_strength=8.0, cast_shadows=False),
                 scale=(0.25, 0.25, 0.25))

    # pillars with glowing bands
    for i, (px, pz) in enumerate(((-8, -10), (8, -10), (-18, 0), (18, 8), (-8, 18), (6, 10))):
        p = solid(f"Ustun {i + 1}", arena, (px, 3, pz), (1.6, 6, 1.6), WALL, (0.6, 2), normal="Textures/wall_normal.png", rough=0.45,
                  metal=0.4, mesh_kind="Cylinder")
        s.entity("Halqa", parent=p, pos=(0, 0.15, 0), scale=(1.06, 0.03, 1.06),
                 MeshRenderer=k.mesh("Cylinder", (0.2, 0.8, 1.0, 1), emission=(0.2, 0.75, 1.0), emission_strength=7.0, cast_shadows=False))
    # low cover walls
    for i, (cx, cz, sx, sz) in enumerate(((-14, -16, 6, 0.8), (16, -15, 0.8, 6), (-4, 12, 5, 0.8), (18, 18, 4, 0.8))):
        solid(f"Pana {i + 1}", arena, (cx, 0.7, cz), (sx, 1.4, sz), WALL, (sx / 2 + sz / 2, 0.6), normal="Textures/wall_normal.png",
              rough=0.6, metal=0.2)
    # crates (they can be pushed)
    for i, (cx, cy, cz) in enumerate(((-19, 0.6, -18), (-17.7, 0.6, -18.2), (-18.4, 1.8, -18.1), (19, 0.6, -3), (19.2, 0.6, -1.7),
                                      (-20, 0.6, 15), (9, 0.6, 19), (10.3, 0.6, 19.4))):
        s.entity(f"Quti {i + 1}", parent=arena, pos=(cx, cy, cz), rot=(0, 0.2 * i, 0), scale=(1.2, 1.2, 1.2),
                 MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.6, 0.1, albedo_map=CRATE, normal_map="Textures/crate_normal.png"),
                 Rigidbody3D=k.rigidbody3d("Dynamic", mass=25.0), BoxCollider3D=k.box3d((0.5, 0.5, 0.5), friction=0.7))
    # coloured lights around the arena
    for i, (lx, ly, lz, col) in enumerate(((-18, 4, -18, (0.3, 0.6, 1.0)), (18, 4, -18, (1.0, 0.4, 0.3)), (-18, 4, 18, (1.0, 0.5, 0.9)),
                                           (18, 4, 18, (0.4, 1.0, 0.6)))):
        s.entity(f"Rangli chiroq {i + 1}", parent=arena, pos=(lx, ly, lz), PointLight=k.point_light(col, 5.0, 14.0))

    # jump pad
    pad = s.entity("Sakratgich", parent=arena, pos=(-12, 0.06, 12), scale=(2.6, 0.12, 2.6), Tags=["JumpPad"],
                   MeshRenderer=k.mesh("Cylinder", (1, 1, 1, 1), 0.3, 0.2, emission=(0.3, 0.9, 1.0), emission_strength=3.0,
                                       albedo_map="Textures/pad.png"))
    s.entity("Sakratgich chirog'i", parent=pad, pos=(0, 8, 0), PointLight=k.point_light((0.3, 0.85, 1.0), 4.0, 7.0))
    s.entity("Sakratgich zarralari", parent=arena, pos=(-12, 0.2, 12), ParticleSystem=k.particles(
        EmissionRate=22.0, MaxParticles=200, Shape=1, ShapeRadius=1.1, LifetimeMin=0.8, LifetimeMax=1.4, SpeedMin=2.0, SpeedMax=4.5,
        Direction=(0, 1, 0), Spread=6.0, StartColor=(0.4, 1.6, 2.4, 1.0), EndColor=(0.2, 0.5, 1.0, 0.0), StartSize=0.14, EndSize=0.02))

    # checkpoints: floating diamonds (the game colours the current one)
    course = s.entity("Parkur")
    cps = []
    for i, (cx, cy, cz) in enumerate(CHECKPOINTS):
        c = s.entity(f"Nuqta {i + 1}", parent=course, pos=(cx, cy, cz), rot=(math.radians(45), 0, math.radians(45)), scale=(0.7, 0.7, 0.7),
                     MeshRenderer=k.mesh("Cube", (0.3, 0.8, 1.0, 1), 0.2, 0.1, emission=(0.3, 0.8, 1.0), emission_strength=4.0,
                                         cast_shadows=False),
                     PointLight=k.point_light((0.3, 0.8, 1.0), 2.5, 5.0))
        cps.append(c)

    # ---- player: the capsule (feet at its origin) with the head / camera
    player = s.entity("O'yinchi", pos=(0, 0.05, -20), Tags=["Player"], CharacterController3D=k.character3d(0.38, 1.8, 50.0, 0.4, 1.7),
                      AudioListener={"Enabled": True, "Priority": 0, "TrackVelocity": True})
    head = s.entity("Bosh", parent=player, pos=(0, 1.62, 0),
                    Camera=k.camera_persp(math.radians(68), 0.05, 400.0, (0.5, 0.6, 0.75, 1.0), shake={"MaxOffset": [0.08, 0.08], "MaxAngle": 2.5, "Exponent": 2.0}))

    # ---- UI
    game = s.entity("Game")
    canvas = s.entity("UI", UITransform=k.ui_stretch(), UICanvas=k.ui_canvas())
    ui = k.UIKit(s, canvas, ATLAS, game, (0.85, 0.45, 0.12, 1.0), (1.0, 0.62, 0.25, 1.0), (0.6, 0.3, 0.08, 1.0))
    hud = s.entity("HUD", parent=canvas, UITransform=k.ui_stretch())
    s.entity("Yuqori fon", parent=hud, UITransform=k.ui_rect((0, 1), (1, 1), (0.5, 1), (0, 0), (0, 150)),
             UIImage=k.ui_image((0.0, 0.0, 0.0, 0.32), raycast=False))
    s.entity("Pastki fon", parent=hud, UITransform=k.ui_rect((0, 0), (1, 0), (0.5, 0), (0, 0), (0, 140)),
             UIImage=k.ui_image((0.0, 0.0, 0.0, 0.28), raycast=False))
    s.entity("Nishon", parent=hud, UITransform=k.ui_rect(size=(44, 44)),
             UIImage=k.ui_image((1, 1, 1, 0.85), ATLAS, "crosshair", raycast=False, preserve_aspect=True))
    timer = ui.text("Vaqt", hud, "00:00.00", (0.5, 1), (0.5, 1), (0, -30), (600, 70), 60, h="Center")
    best = ui.text("Eng yaxshi vaqt", hud, "", (0.5, 1), (0.5, 1), (0, -100), (700, 34), 28, (1.0, 0.8, 0.4, 0.95), h="Center")
    progress = ui.text("Nuqtalar", hud, "", (0, 1), (0, 1), (50, -40), (600, 44), 36, (0.6, 0.9, 1.0, 1.0))
    laps = ui.text("Aylanalar", hud, "", (0, 1), (0, 1), (50, -88), (600, 34), 28, (0.85, 0.85, 0.9, 0.85))
    speed = ui.text("Tezlik", hud, "", (0, 0), (0, 0), (50, 86), (600, 40), 32)
    state = ui.text("Holat", hud, "", (0, 0), (0, 0), (50, 44), (600, 34), 26, (0.75, 0.85, 1.0, 0.9))
    ui.text("Chidamlilik yorlig'i", hud, "CHIDAM", (1, 0), (1, 0), (-50, 84), (300, 28), 22, (1.0, 0.75, 0.4, 0.85), h="Right",
            key="hud.stamina")
    stamina = s.entity("Chidamlilik", parent=hud, UITransform=k.ui_rect((1, 0), None, (1, 0), (-50, 60), (300, 14)),
                       UIProgressBar=k.ui_progress(1.0, fill=(1.0, 0.65, 0.2, 1.0), padding=2.0, smooth=8.0))
    hint = ui.text("Maslahat", hud, "", (0.5, 0), (0.5, 0), (0, 40), (1400, 36), 26, (0.9, 0.9, 1.0, 0.85), h="Center")
    banner = ui.text("E'lon", hud, "", (0.5, 0.5), (0.5, 0.5), (0, 180), (1400, 110), 92, h="Center", UIAnimation=k.ui_anim("Pop", "Fade", 0.35))
    banner_sub = ui.text("E'lon izohi", banner, "", (0.5, 0.0), (0.5, 1.0), (0, -4), (1200, 46), 36, (1.0, 0.85, 0.6, 1.0), h="Center")
    demo_badge = ui.text("Demo belgisi", hud, "DEMO - avtomatik o'yin", (1, 1), (1, 1), (-50, -40), (600, 32), 26, (1.0, 0.8, 0.35, 0.9),
                         h="Right", key="hud.demo", active=False)

    main_menu = ui.dim("Bosh menyu", True, 0.45, (0.02, 0.02, 0.04))
    title = s.entity("Sarlavha", parent=main_menu, UITransform=k.ui_rect(pos=(0, 270), size=(1700, 180), font=156),
                     UIAnimation=k.ui_anim("SlideDown", "None", 0.6, slide=120))
    s.entity("Sarlavha soyasi", parent=title, UITransform=k.ui_rect((0, 0), (1, 1), pos=(6, -7), size=(0, 0), font=156),
             Text=k.ui_text("FPS ARENA", (0.45, 0.15, 0.02, 0.9), "Center", key="menu.title"))
    s.entity("Sarlavha matni", parent=title, UITransform=k.ui_rect((0, 0), (1, 1), size=(0, 0), font=156),
             Text=k.ui_text("FPS ARENA", (1.0, 0.8, 0.5, 1.0), "Center", key="menu.title"))
    s.entity("Taglavha", parent=main_menu, UITransform=k.ui_rect(pos=(0, 156), size=(1500, 50), font=38),
             Text=k.ui_text("Birinchi shaxs boshqaruvi  ·  Zamin Engine namunasi", (1.0, 0.92, 0.85, 0.95), "Center", key="menu.subtitle"))
    menu_panel = ui.panel("Menyu paneli", main_menu, (0, -110), (560, 330))
    play_button = ui.button("O'ynash", menu_panel, "O'ynash", "menu.play", "Play", True)
    language_button = ui.button("Til", menu_panel, "Til: O'zbekcha", None, "Language")
    ui.button("Chiqish", menu_panel, "Chiqish", "menu.quit", "Quit")
    menu_best = s.entity("Bosh menyu rekordi", parent=main_menu, UITransform=k.ui_rect(pos=(0, -330), size=(1200, 44), font=32),
                         Text=k.ui_text("", (1.0, 0.85, 0.4, 1.0), "Center"))
    s.entity("Boshqaruv", parent=main_menu, UITransform=k.ui_rect((0.5, 0), None, (0.5, 0), (0, 56), (1800, 70), font=25),
             Text=k.ui_text("Sichqoncha - qarash   ·   WASD - yurish   ·   Shift - yugurish   ·   Space - sakrash   ·   C / Ctrl - cho'kkalash   ·   Esc - pauza\n"
                            "Gamepad: chap stik - yurish, o'ng stik - qarash, A - sakrash, B - cho'kkalash, chap stik bosish - yugurish",
                            (0.95, 0.9, 0.85, 0.9), "Center", key="menu.controls"))
    pause_menu = ui.dim("Pauza menyusi", False)
    pause_panel = ui.panel("Pauza paneli", pause_menu, (0, 0), (620, 520))
    ui.label("Pauza sarlavhasi", pause_panel, "PAUZA", (520, 80), 60, (1.0, 0.8, 0.5, 1.0), "pause.title")
    resume_button = ui.button("Davom etish", pause_panel, "Davom etish", "pause.resume", "Resume", True)
    sens_label = ui.label("Sezgirlik yorlig'i", pause_panel, "Sichqoncha sezgirligi: 1.0", (520, 40), 28, (0.9, 0.9, 1.0, 1.0))
    sens_slider = s.entity("Sezgirlik", parent=pause_panel, UITransform=k.ui_rect(size=(520, 44)),
                           UISlider={"Min": 0.25, "Max": 3.0, "Value": 1.0, "WholeNumbers": False, "Direction": "LeftToRight",
                                     "Interactable": True, "TrackThickness": 10.0, "HandleSize": 30.0, "Rounded": True,
                                     "TrackColor": [0.1, 0.12, 0.18, 1], "FillColor": [1.0, 0.65, 0.25, 1], "HandleColor": [1.0, 0.95, 0.9, 1]},
                           UINavigation=k.ui_nav("Vertical"))
    ui.button("Qaytadan", pause_panel, "Qaytadan", "pause.restart", "Restart")
    ui.button("Bosh menyuga", pause_panel, "Bosh menyu", "pause.menu", "Menu")

    s.add(game, "NativeScript", k.script(
        "ArenaGame", Player=k.ref(player), Checkpoints=k.ref(course), JumpPad=k.ref(pad),
        Hud=k.ref(hud), TimerText=k.ref(timer), BestText=k.ref(best), ProgressText=k.ref(progress), LapsText=k.ref(laps),
        SpeedText=k.ref(speed), StateText=k.ref(state), StaminaBar=k.ref(stamina), HintText=k.ref(hint), Banner=k.ref(banner),
        BannerSub=k.ref(banner_sub), DemoBadge=k.ref(demo_badge), MainMenu=k.ref(main_menu), PauseMenu=k.ref(pause_menu),
        MenuBest=k.ref(menu_best), PlayButton=k.ref(play_button), LanguageButton=k.ref(language_button), ResumeButton=k.ref(resume_button),
        SensitivitySlider=k.ref(sens_slider), SensitivityText=k.ref(sens_label)))
    s.add(game, "AudioSource", k.audio_source("Audio/ambience.wav", 0.5, loop=True, play=True, output="Music", stream=True))
    route = ";".join(f"{x:g},{y:g},{z:g}" for x, y, z in ROUTE)
    s.add(player, "NativeScript", k.script("FirstPersonController", Head=k.ref(head), BotRoute=route))
    return s


def write_localization():
    uz = {
        "$name": "O'zbekcha",
        "menu": {"title": "FPS ARENA", "subtitle": "Birinchi shaxs boshqaruvi  ·  Zamin Engine namunasi", "play": "O'ynash", "quit": "Chiqish",
                 "language": "Til: {0}", "best": "Eng yaxshi aylana: {0}",
                 "controls": "Sichqoncha - qarash   ·   WASD - yurish   ·   Shift - yugurish   ·   Space - sakrash   ·   C / Ctrl - cho'kkalash   ·   Esc - pauza\n"
                             "Gamepad: chap stik - yurish, o'ng stik - qarash, A - sakrash, B - cho'kkalash, chap stik bosish - yugurish"},
        "hud": {"demo": "DEMO - avtomatik o'yin", "stamina": "CHIDAM", "progress": "Nuqta {0} / {1}", "laps": "Aylanalar: {0}",
                "best": "Eng yaxshi: {0}", "speed": "{0:.1f} m/s", "walk": "Yurish", "sprint": "Yugurish", "crouch": "Cho'kkalash",
                "air": "Havoda", "idle": "Turish", "hint": "Yorqin nuqtalardan ketma-ket o'ting  ·  ko'k doira - sakratgich",
                "start": "PARKUR", "start_sub": "Yorqin nuqtalardan o'ting", "lap": "AYLANA TUGADI!", "record": "Yangi rekord: {0}",
                "time": "Vaqt: {0}"},
        "pause": {"title": "PAUZA", "resume": "Davom etish", "restart": "Qaytadan", "menu": "Bosh menyu", "sensitivity": "Sichqoncha sezgirligi: {0:.2f}"},
    }
    en = {
        "$name": "English",
        "menu": {"title": "FPS ARENA", "subtitle": "First-person controller  ·  a Zamin Engine sample", "play": "Play", "quit": "Quit",
                 "language": "Language: {0}", "best": "Best lap: {0}",
                 "controls": "Mouse - look   ·   WASD - move   ·   Shift - sprint   ·   Space - jump   ·   C / Ctrl - crouch   ·   Esc - pause\n"
                             "Gamepad: left stick - move, right stick - look, A - jump, B - crouch, left stick click - sprint"},
        "hud": {"demo": "DEMO - autoplay", "stamina": "STAMINA", "progress": "Checkpoint {0} / {1}", "laps": "Laps: {0}", "best": "Best: {0}",
                "speed": "{0:.1f} m/s", "walk": "Walking", "sprint": "Sprinting", "crouch": "Crouching", "air": "Airborne", "idle": "Standing",
                "hint": "Run through the glowing markers in order  ·  blue disc - jump pad", "start": "PARKOUR",
                "start_sub": "Run through the glowing markers", "lap": "LAP COMPLETE!", "record": "New record: {0}", "time": "Time: {0}"},
        "pause": {"title": "PAUSED", "resume": "Resume", "restart": "Restart", "menu": "Main menu", "sensitivity": "Mouse sensitivity: {0:.2f}"},
    }
    k.write_json(os.path.join(ASSETS, "Localization/uz.json"), uz)
    k.write_json(os.path.join(ASSETS, "Localization/en.json"), en)


def main():
    make_textures()
    make_ui_atlas()
    make_icon()
    make_sounds()
    write_localization()
    scene = build_scene()
    uuid = scene.save(ASSETS, "Scenes/Arena.zscene", NAME)
    actions = [
        ("Jump", ["Key:Space", "Gamepad:A"]),
        ("Sprint", ["Key:LeftShift", "Gamepad:LeftThumb"]),
        ("Crouch", ["Key:C", "Key:LeftControl", "Gamepad:B"]),
        ("Fire", ["Mouse:Left", "GamepadAxis:RightTrigger+"]),
        ("Pause", ["Key:Escape", "Key:P", "Gamepad:Start"]),
    ]
    axes = [
        k.axis("MoveX", ["Key:A", "Key:Left"], ["Key:D", "Key:Right"], "LeftX"),
        k.axis("MoveY", ["Key:S", "Key:Down"], ["Key:W", "Key:Up"], "LeftY", invert=True),
        k.axis("LookX", [], [], "RightX", deadzone=0.12),
        k.axis("LookY", [], [], "RightY", invert=True, deadzone=0.12),
    ]
    vectors = [{"Name": "Move", "X": "MoveX", "Y": "MoveY", "Mode": "Clamp", "Deadzone": 0.0},
               {"Name": "Look", "X": "LookX", "Y": "LookY", "Mode": "Clamp", "Deadzone": 0.0}]
    k.write_project(PROJECT, NAME, "FPS Arena", "Scenes/Arena.zscene", uuid, "1.0.0", [("Scenes/Arena.zscene", uuid)], actions, axes,
                    vectors, ["Player", "JumpPad"])
    k.write_module_cmake(PROJECT, NAME, "Zamin namuna o'yini: FPS Arena - C++ skriptlar moduli (libFPSArena.so)")
    print("FPSArena: tayyor")


if __name__ == "__main__":
    main()
