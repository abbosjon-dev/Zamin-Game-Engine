"""Projects/TopDownShooter: "Neon Maydon" - its textures, sounds, scene and project file.

    python3 Tools/samples/gen_topdownshooter.py

Everything under Projects/TopDownShooter/Assets (except Localization, written by hand) and the .zproject
are made here; the gameplay is C++ (Projects/TopDownShooter/Source).
"""

import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import samplekit as k  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NAME = "TopDownShooter"
PROJECT = k.project_dir(NAME)  # Projects/<NAME> (or $ZAMIN_SAMPLES_OUT/<NAME>: Tests/samples_assets.py --regen)
ASSETS = os.path.join(PROJECT, "Assets")

ARENA_W, ARENA_H = 19.0, 11.0  # half extents of the playable floor (units)
ATLAS = "Textures/shooter_atlas.png"

# ------------------------------------------------------------------------------------------- textures


def shape_sprite(size, sd_fn, fill_top, fill_bottom, line, glow_color, glow_radius=7.0, line_width=3.0, extra=None):
    """A neon shape: a vertical gradient fill, a bright outline and a soft halo."""
    img = k.image(size, size)
    x, y = k.coords(size, size)
    sd = sd_fn(x, y)
    k.glow(img, sd, glow_color, glow_radius, 0.9)
    t = np.clip((y / size) + 0.5, 0, 1)
    cov = k.smooth(0.5, -0.5, sd)
    grad = k.mix_color(fill_top, fill_bottom, t)
    for i in range(3):
        img[:, :, i] = img[:, :, i] * (1 - cov) + grad[:, :, i] * cov
    img[:, :, 3] = np.maximum(img[:, :, 3], cov)
    k.stroke(img, sd + line_width * 0.5, line, line_width)
    if extra:
        extra(img, x, y)
    return img


def make_atlas():
    atlas = k.Atlas(1024, 512)

    # player ship (points up)
    hull = [(0, -54), (38, 42), (13, 30), (0, 42), (-13, 30), (-38, 42)]

    def cockpit(img, x, y):
        k.fill(img, k.sd_polygon(x, y, [(0, -26), (9, 6), (0, 12), (-9, 6)]), (0.85, 1.0, 1.0, 1.0))
        k.glow(img, k.sd_circle(x, y - 34, 6), (0.5, 0.95, 1.0, 1.0), 6, 0.8)

    atlas.add("ship", shape_sprite(128, lambda x, y: k.sd_polygon(x, y, hull), (0.25, 0.85, 1.0), (0.05, 0.25, 0.6),
                                   (0.75, 1.0, 1.0, 1.0), (0.2, 0.8, 1.0, 1.0), 9, 3.5, cockpit))
    # chaser: a dart
    atlas.add("chaser", shape_sprite(96, lambda x, y: k.sd_polygon(x, y, [(0, -40), (30, 36), (0, 20), (-30, 36)]),
                                     (1.0, 0.45, 0.55), (0.6, 0.05, 0.2), (1.0, 0.8, 0.85, 1.0), (1.0, 0.2, 0.4, 1.0), 7))
    # swarmer: a small 4-point star

    def star(x, y):
        pts = []
        for i in range(8):
            r = 27 if i % 2 == 0 else 10
            a = -math.pi / 2 + i * math.pi / 4
            pts.append((r * math.cos(a), r * math.sin(a)))
        return k.sd_polygon(x, y, pts)

    atlas.add("swarmer", shape_sprite(64, star, (0.6, 1.0, 0.5), (0.1, 0.55, 0.2), (0.85, 1.0, 0.8, 1.0), (0.3, 1.0, 0.4, 1.0), 5, 2.5))

    # shooter: a diamond with an eye
    def eye(img, x, y):
        k.fill(img, k.sd_circle(x, y, 14), (0.1, 0.0, 0.2, 1.0))
        k.fill(img, k.sd_circle(x, y - 3, 7), (1.0, 0.85, 1.0, 1.0))

    atlas.add("shooter", shape_sprite(112, lambda x, y: k.sd_polygon(x, y, [(0, -48), (44, 0), (0, 48), (-44, 0)]),
                                      (0.8, 0.5, 1.0), (0.35, 0.1, 0.65), (0.95, 0.8, 1.0, 1.0), (0.7, 0.3, 1.0, 1.0), 8, 3.5, eye))
    # dasher: a double chevron

    def chevron(x, y):
        a = k.sd_polygon(x, y, [(0, -42), (36, 30), (18, 38), (0, 6), (-18, 38), (-36, 30)])
        return a

    atlas.add("dasher", shape_sprite(96, chevron, (1.0, 0.95, 0.45), (0.85, 0.5, 0.05), (1.0, 1.0, 0.8, 1.0), (1.0, 0.8, 0.2, 1.0), 7))

    # tank: a hexagon with armour rings
    def armour(img, x, y):
        k.stroke(img, k.sd_polygon(x, y, k.regular_polygon(6, 44, math.pi / 6)), (1.0, 0.85, 0.6, 1.0), 3)
        k.fill(img, k.sd_circle(x, y, 22), (0.25, 0.08, 0.02, 1.0))
        k.fill(img, k.sd_circle(x, y, 13), (1.0, 0.75, 0.35, 1.0))
        k.glow(img, k.sd_circle(x, y, 13), (1.0, 0.6, 0.2, 1.0), 6, 0.9)

    atlas.add("tank", shape_sprite(160, lambda x, y: k.sd_polygon(x, y, k.regular_polygon(6, 68, math.pi / 6)), (1.0, 0.6, 0.25),
                                   (0.6, 0.2, 0.03), (1.0, 0.9, 0.7, 1.0), (1.0, 0.45, 0.1, 1.0), 10, 4, armour))

    # projectiles: white, the sprite's colour tints them (HDR colours glow through the bloom)
    img = k.image(32, 64)
    x, y = k.coords(32, 64)
    sd = k.sd_round_box(x, y, 6, 24, 6)
    k.glow(img, sd, (1, 1, 1, 1), 5, 0.9)
    k.fill(img, sd, (1, 1, 1, 1))
    atlas.add("bullet", img)
    img = k.image(48, 48)
    x, y = k.coords(48, 48)
    sd = k.sd_circle(x, y, 9)
    k.glow(img, sd, (1, 1, 1, 1), 6, 1.0)
    k.fill(img, sd, (1, 1, 1, 1))
    atlas.add("orb", img)

    # pickups / hud icons
    def heart_sd(x, y, s=1.0):
        x, y = x / s, y / s
        left = k.sd_circle(x + 11, y + 6, 13)
        right = k.sd_circle(x - 11, y + 6, 13)
        tip = k.sd_polygon(x, y, [(-23, 0), (23, 0), (0, 26)])
        return np.minimum(np.minimum(left, right), tip) * s

    img = k.image(64, 64)
    x, y = k.coords(64, 64, (32, 30))
    sd = heart_sd(x, y)
    k.glow(img, sd, (1.0, 0.3, 0.4, 1), 5, 0.8)
    k.fill(img, sd, (1.0, 0.3, 0.42, 1))
    k.fill(img, k.sd_circle(x + 10, y + 10, 4), (1, 0.85, 0.9, 0.9))
    atlas.add("heart", img)
    img = k.image(64, 64)
    k.stroke(img, heart_sd(x, y), (0.6, 0.65, 0.8, 0.55), 3)
    atlas.add("heart_empty", img)

    img = k.image(64, 64)
    x, y = k.coords(64, 64)
    bolt = [(6, -28), (-14, 4), (-2, 4), (-8, 28), (14, -6), (2, -6)]
    sd = k.sd_polygon(x, y, bolt)
    k.glow(img, sd, (1, 1, 1, 1), 5, 0.8)
    k.fill(img, sd, (1, 1, 1, 1))
    atlas.add("bolt", img)
    img = k.image(64, 64)
    for ang in (-0.45, 0.0, 0.45):
        cx, cy = 26 * math.sin(ang) * 0.9, -26 * math.cos(ang) * 0.4 + 6
        sd = k.sd_round_box((x - cx) * math.cos(ang) + (y - cy) * math.sin(ang), -(x - cx) * math.sin(ang) + (y - cy) * math.cos(ang), 4, 14, 4)
        k.glow(img, sd, (1, 1, 1, 1), 4, 0.7)
        k.fill(img, sd, (1, 1, 1, 1))
    atlas.add("spread", img)

    # rings and glows (tinted at runtime)
    img = k.image(128, 128)
    x, y = k.coords(128, 128)
    sd = np.abs(k.sd_circle(x, y, 54)) - 3
    k.glow(img, sd, (1, 1, 1, 1), 6, 0.9)
    k.fill(img, sd, (1, 1, 1, 1))
    atlas.add("ring", img)
    img = k.image(64, 64)
    x, y = k.coords(64, 64)
    r = np.sqrt(x * x + y * y) / 32.0
    img[:, :, :3] = 1.0
    img[:, :, 3] = np.clip(1.0 - r, 0, 1) ** 2
    atlas.add("glow", img)
    img = k.image(64, 64)
    sd = np.abs(k.sd_circle(x, y, 16)) - 1.5
    for (dx, dy) in ((0, -1), (0, 1), (1, 0), (-1, 0)):
        tick = k.sd_round_box(x - dx * 24, y - dy * 24, 2 if dx == 0 else 6, 6 if dx == 0 else 2, 1)
        sd = np.minimum(sd, tick)
    k.glow(img, sd, (1, 1, 1, 1), 3, 0.6)
    k.fill(img, sd, (1, 1, 1, 1))
    k.fill(img, k.sd_circle(x, y, 2.5), (1, 1, 1, 1))
    atlas.add("reticle", img)

    # obstacle block (9-slice): a dark plate with a bright rim (tinted)
    img = k.image(64, 64)
    x, y = k.coords(64, 64)
    sd = k.sd_round_box(x, y, 28, 28, 10)
    k.glow(img, sd, (1, 1, 1, 1), 3, 0.5)
    inner = k.mix_color((0.2, 0.22, 0.32), (0.1, 0.11, 0.18), np.clip(y / 64 + 0.5, 0, 1))
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = img[:, :, i] * (1 - cov) + inner[:, :, i] * cov
    img[:, :, 3] = np.maximum(img[:, :, 3], cov)
    k.stroke(img, sd + 2, (1, 1, 1, 1), 3)
    atlas.add("block", img, border=(18, 18, 18, 18))

    # UI: a panel and a button (9-slice)
    img = k.image(96, 96)
    x, y = k.coords(96, 96)
    sd = k.sd_round_box(x, y, 44, 44, 22)
    k.glow(img, sd, (0.3, 0.85, 1.0, 1.0), 3, 0.5)
    fill_col = k.mix_color((0.05, 0.08, 0.16), (0.02, 0.03, 0.08), np.clip(y / 96 + 0.5, 0, 1))
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = img[:, :, i] * (1 - cov) + fill_col[:, :, i] * cov
    img[:, :, 3] = np.maximum(img[:, :, 3], cov * 0.93)
    k.stroke(img, sd + 1.5, (0.4, 0.9, 1.0, 1.0), 2.5)
    atlas.add("panel", img, border=(30, 30, 30, 30))
    img = k.image(96, 64)
    x, y = k.coords(96, 64)
    sd = k.sd_round_box(x, y, 44, 28, 16)
    shade = k.mix_color((1.0, 1.0, 1.0), (0.78, 0.8, 0.86), np.clip(y / 64 + 0.5, 0, 1))
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = shade[:, :, i]
    img[:, :, 3] = cov
    k.stroke(img, sd + 1.5, (1, 1, 1, 0.9), 2)
    atlas.add("button", img, border=(22, 22, 22, 22))

    return atlas.save(ASSETS, ATLAS, NAME)


def make_floor():
    size = 256
    n = k.value_noise(size, size, 4, seed=7, octaves=4)
    img = k.image(size, size, (0, 0, 0, 1))
    base = np.array([0.045, 0.05, 0.09])
    for i in range(3):
        img[:, :, i] = base[i] * (0.75 + 0.5 * n)
    ys, xs = np.mgrid[0:size, 0:size]
    cell = 64
    dx = np.minimum(xs % cell, cell - xs % cell)
    dy = np.minimum(ys % cell, cell - ys % cell)
    line = np.exp(-np.minimum(dx, dy) ** 2 / 2.0)
    fine = np.exp(-np.minimum(xs % 16, 16 - xs % 16) ** 2 / 0.6) * 0.15 + np.exp(-np.minimum(ys % 16, 16 - ys % 16) ** 2 / 0.6) * 0.15
    dots = np.exp(-(dx ** 2 + dy ** 2) / 8.0)
    grid_col = np.array([0.16, 0.32, 0.5])
    for i in range(3):
        img[:, :, i] += grid_col[i] * (line * 0.55 + fine * 0.35) + np.array([0.5, 0.85, 1.0])[i] * dots * 0.5
    k.write_png(os.path.join(ASSETS, "Textures/floor.png"), img)
    k.write_zmeta(ASSETS, "Textures/floor.png", "Texture", {}, NAME)


def make_space():
    size = 512
    rng = np.random.default_rng(11)
    a = k.value_noise(size, size, 3, seed=3, octaves=5)
    b = k.value_noise(size, size, 5, seed=5, octaves=4)
    img = k.image(size, size, (0, 0, 0, 1))
    neb1 = np.array([0.35, 0.08, 0.55])
    neb2 = np.array([0.05, 0.3, 0.55])
    base = np.array([0.01, 0.012, 0.03])
    m1 = np.clip((a - 0.45) * 2.2, 0, 1) ** 1.6
    m2 = np.clip((b - 0.5) * 2.4, 0, 1) ** 1.8
    for i in range(3):
        img[:, :, i] = base[i] + neb1[i] * m1 * 0.55 + neb2[i] * m2 * 0.5
    x, y = k.coords(size, size, (0, 0))
    for _ in range(420):
        sx, sy = rng.uniform(0, size, 2)
        r = rng.choice([0.6, 0.8, 1.1, 1.6], p=[0.5, 0.3, 0.15, 0.05])
        bright = rng.uniform(0.5, 1.0)
        # wrap so the texture tiles
        for ox in (-size, 0, size):
            for oy in (-size, 0, size):
                if -8 < sx + ox < size + 8 and -8 < sy + oy < size + 8:
                    x0, x1 = int(max(0, sx + ox - 6)), int(min(size, sx + ox + 6))
                    y0, y1 = int(max(0, sy + oy - 6)), int(min(size, sy + oy + 6))
                    if x0 >= x1 or y0 >= y1:
                        continue
                    px, py = x[y0:y1, x0:x1] + 0.5 - (sx + ox), y[y0:y1, x0:x1] + 0.5 - (sy + oy)
                    d2 = px * px + py * py
                    s = np.exp(-d2 / (r * r)) * bright
                    tint = np.array([0.8, 0.9, 1.0]) if rng.random() < 0.7 else np.array([1.0, 0.85, 0.7])
                    for i in range(3):
                        img[y0:y1, x0:x1, i] += s * tint[i]
    k.write_png(os.path.join(ASSETS, "Textures/space.png"), np.clip(img, 0, 1))
    k.write_zmeta(ASSETS, "Textures/space.png", "Texture", {}, NAME)


def make_icon():
    size = 256
    img = k.image(size, size)
    x, y = k.coords(size, size)
    sd = k.sd_round_box(x, y, 120, 120, 44)
    bg = k.mix_color((0.06, 0.05, 0.18), (0.01, 0.02, 0.06), np.clip(y / size + 0.5, 0, 1))
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = bg[:, :, i]
    img[:, :, 3] = cov
    k.stroke(img, sd + 3, (0.3, 0.9, 1.0, 1.0), 5)
    k.glow(img, k.sd_circle(x, y, 40), (0.2, 0.7, 1.0, 1.0), 40, 0.5)
    hull = [(0, -92), (64, 72), (22, 50), (0, 72), (-22, 50), (-64, 72)]
    sd = k.sd_polygon(x, y + 6, hull)
    k.glow(img, sd, (0.2, 0.85, 1.0, 1.0), 10, 0.9)
    k.fill(img, sd, (0.2, 0.75, 1.0, 1.0))
    k.stroke(img, sd + 2, (0.85, 1.0, 1.0, 1.0), 5)
    k.fill(img, k.sd_polygon(x, y + 6, [(0, -44), (15, 10), (0, 20), (-15, 10)]), (0.9, 1.0, 1.0, 1.0))
    img[:, :, 3] = np.minimum(img[:, :, 3], cov)
    k.write_png(os.path.join(ASSETS, "Textures/GameIcon.png"), img)
    k.write_zmeta(ASSETS, "Textures/GameIcon.png", "Texture", {}, NAME)


# --------------------------------------------------------------------------------------------- sounds


def make_sounds():
    R = k.RATE
    out = {}
    # player shot: a quick square sweep
    n = int(0.11 * R)
    f = np.linspace(1500, 420, n)
    s = k.osc(f, 0.11, "square") * 0.5 + k.osc(f * 0.5, 0.11, "saw") * 0.3
    s = k.lowpass(s, 5000) * k.exp_decay(n, 0.035)
    out["shoot"] = s
    # enemy shot: a wobbly lower tone
    n = int(0.28 * R)
    f = np.linspace(700, 240, n) * (1 + 0.06 * np.sin(np.arange(n) / R * 2 * np.pi * 30))
    out["enemy_shoot"] = k.osc(f, 0.28, "tri") * k.exp_decay(n, 0.09)
    # hit: a tick
    n = int(0.07 * R)
    out["hit"] = k.highpass(k.noise(0.07, 3), 1800) * k.exp_decay(n, 0.015) + k.osc(320, 0.07, "square") * k.exp_decay(n, 0.02) * 0.4
    # explosions
    for name, length, seed, thump in (("explode", 0.75, 4, 1.0), ("big_explode", 1.6, 9, 1.4)):
        n = int(length * R)
        cutoff = np.linspace(5000, 180, n)
        s = k.lowpass(k.noise(length, seed), cutoff) * k.exp_decay(n, length * 0.3)
        body = k.osc(np.linspace(95, 35, n), length, "sine") * k.exp_decay(n, length * 0.22) * thump
        out[name] = s * 1.4 + body
    # player hurt: a falling buzz
    n = int(0.4 * R)
    s = k.osc(np.linspace(520, 110, n), 0.4, "saw")
    out["player_hurt"] = np.tanh(s * 2.5) * k.exp_decay(n, 0.14) + k.lowpass(k.noise(0.4, 5), 900) * k.exp_decay(n, 0.08)
    # pickup / power-up: arpeggios
    buf = np.zeros(int(0.36 * R))
    for i, note in enumerate(("C6", "E6", "G6", "C7")):
        tone = k.osc(k.note_freq(note), 0.12, "square") * k.exp_decay(int(0.12 * R), 0.05)
        k.place(buf, k.lowpass(tone, 7000), i * 0.06, 0.6)
    out["pickup"] = buf
    buf = np.zeros(int(0.7 * R))
    for i, note in enumerate(("A5", "C#6", "E6", "A6", "C#7", "E7")):
        tone = k.osc(k.note_freq(note), 0.2, "saw") * k.exp_decay(int(0.2 * R), 0.08)
        k.place(buf, k.lowpass(tone, 6000), i * 0.07, 0.5)
    out["powerup"] = buf
    # dash: a whoosh
    n = int(0.3 * R)
    cut = np.concatenate([np.linspace(300, 4000, n // 2), np.linspace(4000, 600, n - n // 2)])
    out["dash"] = k.lowpass(k.noise(0.3, 6), cut) * np.sin(np.linspace(0, np.pi, n)) ** 1.5
    # spawn portal: a rising shimmer
    n = int(0.6 * R)
    f = np.linspace(180, 900, n)
    out["spawn"] = k.osc(f, 0.6, "sine") * (0.6 + 0.4 * np.sin(np.arange(n) / R * 2 * np.pi * 22)) * np.sin(np.linspace(0, np.pi, n)) * 0.6
    # wave start / clear / game over
    buf = np.zeros(int(1.2 * R))
    for note in ("A3", "E4", "A4"):
        k.place(buf, k.lowpass(k.osc(k.note_freq(note), 1.0, "saw") * k.exp_decay(int(1.0 * R), 0.35), 2500), 0.0, 0.35)
    for i, note in enumerate(("A4", "C5", "E5", "A5")):
        k.place(buf, k.osc(k.note_freq(note), 0.25, "square") * k.exp_decay(int(0.25 * R), 0.1), 0.15 + i * 0.09, 0.3)
    out["wave"] = buf
    buf = np.zeros(int(1.0 * R))
    for i, note in enumerate(("C5", "E5", "G5", "C6", "E6")):
        k.place(buf, k.osc(k.note_freq(note), 0.4, "tri") * k.exp_decay(int(0.4 * R), 0.18), i * 0.08, 0.6)
    out["wave_clear"] = buf
    buf = np.zeros(int(1.9 * R))
    for i, note in enumerate(("E5", "C5", "A4", "E4", "A3")):
        tone = k.osc(k.note_freq(note), 0.6, "tri") + 0.3 * k.osc(k.note_freq(note) * 1.005, 0.6, "saw")
        k.place(buf, k.lowpass(tone, 3000) * k.exp_decay(int(0.6 * R), 0.3), i * 0.22, 0.6)
    out["gameover"] = buf
    # ui
    n = int(0.06 * R)
    out["ui_click"] = k.osc(1250, 0.06, "square") * k.exp_decay(n, 0.012) * 0.6 + k.osc(2500, 0.06, "sine") * k.exp_decay(n, 0.006) * 0.4
    for name, s in out.items():
        rel = f"Audio/{name}.wav"
        k.write_wav(os.path.join(ASSETS, rel), s)
        k.write_zmeta(ASSETS, rel, "Audio", {"LoadMode": "Decompress", "Preload": True, "Volume": 1.0}, NAME)
    make_music()


def make_music():
    """A synthwave loop: 112 BPM, 8 bars, Am - F - C - G (two bars each), bass, arpeggio, pad and drums."""
    R = k.RATE
    bpm = 112.0
    beat = 60.0 / bpm
    bars = 8
    length = bars * 4 * beat
    total = int(length * R)
    buf = np.zeros(total + R)
    chords = [("A2", ("A3", "C4", "E4")), ("F2", ("F3", "A3", "C4")), ("C3", ("C4", "E4", "G4")), ("G2", ("G3", "B3", "D4"))]
    rng = np.random.default_rng(21)
    kick = k.osc(np.linspace(140, 40, int(0.25 * R)), 0.25, "sine") * k.exp_decay(int(0.25 * R), 0.08)
    snare = k.highpass(k.noise(0.22, 2), 1200) * k.exp_decay(int(0.22 * R), 0.06) + k.osc(190, 0.22, "tri") * k.exp_decay(int(0.22 * R), 0.04) * 0.5
    hat = k.highpass(k.noise(0.05, 8), 6000) * k.exp_decay(int(0.05 * R), 0.012)
    for bar in range(bars):
        root, triad = chords[(bar // 2) % 4]
        t0 = bar * 4 * beat
        # bass: eighth notes, octave jumps
        for i in range(8):
            f = k.note_freq(root) * (2.0 if i % 4 == 3 else 1.0)
            tone = k.lowpass(k.osc(f, beat / 2, "saw"), 700) * k.env(int(beat / 2 * R), 0.004, 0.18, 0.4, 0.05, 0.04)
            k.place(buf, tone, t0 + i * beat / 2, 0.55)
        # arpeggio: sixteenths through the chord, an octave up every other beat
        for i in range(16):
            note = triad[i % 3]
            f = k.note_freq(note) * (2.0 if (i // 4) % 2 else 1.0) * 2.0
            tone = k.osc(f, beat / 4, "square") * k.exp_decay(int(beat / 4 * R), 0.05)
            k.place(buf, k.lowpass(tone, 3200), t0 + i * beat / 4, 0.16)
        # pad: detuned saws over the bar
        pad = np.zeros(int(4 * beat * R))
        for note in triad:
            for det in (0.997, 1.003):
                pad += k.osc(k.note_freq(note) * det, 4 * beat, "saw")
        pad = k.lowpass(pad, 900) * k.env(len(pad), 0.4, 0.5, 0.8, 4 * beat - 1.2, 0.3)
        k.place(buf, pad, t0, 0.07)
        # drums
        for b in range(4):
            k.place(buf, kick, t0 + b * beat, 0.9)
            if b % 2 == 1:
                k.place(buf, snare, t0 + b * beat, 0.45)
            for h in range(2):
                k.place(buf, hat, t0 + b * beat + h * beat / 2 + beat / 4, 0.18 * rng.uniform(0.7, 1.0))
    # the tail (reverb-ish echoes past the loop's end) folds back onto its start: a seamless loop
    buf[:R] += buf[total:total + R]
    music = buf[:total]
    rel = "Audio/music.wav"
    k.write_wav(os.path.join(ASSETS, rel), music, peak=0.8)
    k.write_zmeta(ASSETS, rel, "Audio", {"LoadMode": "Stream", "Preload": False, "Volume": 1.0}, NAME)


# ---------------------------------------------------------------------------------------------- scene

# collision layers (Project settings > Physics layers)
LAYERS = ["Default", "Wall", "Player", "Enemy", "PlayerShot", "EnemyShot", "Pickup"]
L = {n: i for i, n in enumerate(LAYERS)}
PAIRS = [("Default", n) for n in LAYERS] + [
    ("Wall", "Player"), ("Wall", "Enemy"), ("Wall", "PlayerShot"), ("Wall", "EnemyShot"),
    ("Player", "Enemy"), ("Player", "EnemyShot"), ("Player", "Pickup"),
    ("Enemy", "Enemy"), ("Enemy", "PlayerShot"),
]

CYAN = (0.3, 0.9, 1.0)
UI_BUTTON = dict(normal=(0.16, 0.42, 0.62, 1.0), hover=(0.26, 0.72, 0.95, 1.0), pressed=(0.1, 0.3, 0.45, 1.0))


def build_scene():
    s = k.SceneBuilder("Arena", 8810000000000000)
    s.settings = k.settings_2d(ambient2d=(0.45, 0.5, 0.85), ambient2d_intensity=0.55, bloom=(0.8, 0.5, 0.85), vignette=0.38,
                               saturation=1.12, contrast=1.08, chroma=0.12, grain=0.04)
    s.metadata = {"Description": "Neon Maydon: top-down shooter - to'lqinlar, dushman turlari, hisob va kombo", "Author": "Zamin",
                  "Tags": ["namuna", "2D", "shooter"]}

    camera = s.entity("Main Camera", pos=(0, 0, 10), Camera=k.camera_ortho(17.0, (0.01, 0.012, 0.03, 1.0)))
    game = s.entity("Game")  # filled in below (its fields point at everything)

    # ---- background and arena
    s.entity("Kosmos", pos=(0, 0, -1), scale=(70, 70, 1), SpriteRenderer=k.sprite("Textures/space.png", order=-100, tiling=2.0),
             Parallax={"Factor": [0.75, 0.75], "RepeatX": False, "RepeatY": False, "Spacing": [0, 0]})
    arena = s.entity("Maydon")
    s.entity("Pol", parent=arena, SpriteRenderer=k.sprite("Textures/floor.png", order=-50, lit=True, draw_mode="Tiled",
                                                           size=(ARENA_W * 2, ARENA_H * 2), ppu=64))
    s.entity("Chang", parent=arena, ParticleSystem=k.particles(
        EmissionRate=7.0, MaxParticles=200, Shape=3, ShapeExtents=(ARENA_W, ARENA_H, 0), LifetimeMin=5.0, LifetimeMax=9.0,
        SpeedMin=0.05, SpeedMax=0.35, Direction=(0, 1, 0), Spread=180.0, StartColor=(0.45, 0.75, 1.0, 0.45), EndColor=(0.6, 0.4, 1.0, 0.0),
        StartSize=0.12, EndSize=0.04, Prewarm=True, SortingOrder=-40))
    # corner lights colour the lit floor
    for i, (cx, cy, col) in enumerate(((-15, 8, (1.0, 0.25, 0.85)), (15, 8, (0.2, 0.75, 1.0)), (-15, -8, (0.2, 0.75, 1.0)),
                                       (15, -8, (1.0, 0.25, 0.85)), (0, 0, (0.45, 0.4, 1.0)))):
        s.entity(f"Chiroq {i + 1}", parent=arena, pos=(cx, cy, 0), Light2D=k.light2d(col, 0.9 if i < 4 else 0.6, 15 if i < 4 else 20,
                                                                                    falloff=1.6))
    # walls: a dark body with a neon tube along the inside edge
    t = 1.0
    for name, pos, size, tube_pos, tube_size in (
            ("Devor (yuqori)", (0, ARENA_H + t / 2), (ARENA_W * 2 + 2 * t, t), (0, -t / 2 + 0.06), (ARENA_W * 2 + 0.12, 0.12)),
            ("Devor (past)", (0, -ARENA_H - t / 2), (ARENA_W * 2 + 2 * t, t), (0, t / 2 - 0.06), (ARENA_W * 2 + 0.12, 0.12)),
            ("Devor (chap)", (-ARENA_W - t / 2, 0), (t, ARENA_H * 2), (t / 2 - 0.06, 0), (0.12, ARENA_H * 2)),
            ("Devor (o'ng)", (ARENA_W + t / 2, 0), (t, ARENA_H * 2), (-t / 2 + 0.06, 0), (0.12, ARENA_H * 2))):
        w = s.entity(name, parent=arena, pos=(pos[0], pos[1], 0), scale=(size[0], size[1], 1),
                     SpriteRenderer=k.sprite(col=(0.05, 0.06, 0.11, 1), order=-10),
                     Rigidbody2D=k.rigidbody2d("Static", layer=L["Wall"]), BoxCollider2D=k.box2d((1, 1)))
        s.entity("Neon", parent=w, pos=(tube_pos[0] / size[0], tube_pos[1] / size[1], 0), scale=(tube_size[0] / size[0], tube_size[1] / size[1], 1),
                 SpriteRenderer=k.sprite(col=(0.35, 1.25, 1.7, 1), order=-9))
    # obstacles: neon blocks
    for i, (bx, by, bw, bh, col) in enumerate(((-9, 4.5, 2.2, 2.2, (1.0, 0.35, 0.9)), (9, -4.5, 2.2, 2.2, (1.0, 0.35, 0.9)),
                                                (9, 4.5, 1.4, 3.6, (0.35, 0.85, 1.0)), (-9, -4.5, 1.4, 3.6, (0.35, 0.85, 1.0)),
                                                (0, 7.0, 4.0, 1.0, (0.6, 0.5, 1.0)), (0, -7.0, 4.0, 1.0, (0.6, 0.5, 1.0)))):
        s.entity(f"To'siq {i + 1}", parent=arena, pos=(bx, by, 0),
                 SpriteRenderer=k.sprite(ATLAS, "block", col=(*[c * 1.6 for c in col], 1), order=0, draw_mode="Sliced", size=(bw, bh), ppu=48),
                 Rigidbody2D=k.rigidbody2d("Static", layer=L["Wall"]), BoxCollider2D=k.box2d((bw, bh)))

    # ---- the player
    player = s.entity("O'yinchi", pos=(0, -2, 0), Tags=["Player"],
                      Rigidbody2D=k.rigidbody2d("Dynamic", layer=L["Player"], linear_damping=0.0),
                      CircleCollider2D=k.circle2d(0.45, density=1.0), Light2D=k.light2d((0.35, 0.85, 1.0), 1.5, 8.5, 0.5, 1.8))
    visual = s.entity("Visual", parent=player, scale=(1.35, 1.35, 1), SpriteRenderer=k.sprite(ATLAS, "ship", col=(1.15, 1.25, 1.35, 1), order=20))
    thruster = s.entity("Dvigatel", parent=visual, pos=(0, -0.42, 0), ParticleSystem=k.particles(
        EmissionRate=70.0, MaxParticles=200, LifetimeMin=0.18, LifetimeMax=0.38, SpeedMin=1.0, SpeedMax=2.6, Direction=(0, -1, 0),
        Spread=14.0, StartColor=(0.5, 1.6, 2.4, 1.0), EndColor=(0.3, 0.2, 1.0, 0.0), StartSize=0.26, EndSize=0.02, SortingOrder=18))
    shield = s.entity("Qalqon", parent=player, scale=(2.3, 2.3, 1), SpriteRenderer=k.sprite(ATLAS, "ring", col=(0.4, 1.4, 2.0, 0.0), order=21))
    reticle = s.entity("Nishon", pos=(0, 2, 0), scale=(0.9, 0.9, 1), SpriteRenderer=k.sprite(ATLAS, "reticle", col=(1.0, 1.6, 1.8, 0.85), order=50))

    s.add(camera, "Camera2DFollow", k.camera_follow(player, damping=(0.14, 0.14), dead_zone=(0.3, 0.25), look_ahead=(1.6, 1.2),
                                                     bounds=((-ARENA_W - 1.6, -ARENA_H - 1.6), (ARENA_W + 1.6, ARENA_H + 1.6))))

    # ---- effects shared by everything (the game moves them and emits bursts)
    fx = s.entity("Effektlar")
    explosion = s.entity("Portlash", parent=fx, ParticleSystem=k.particles(
        Playing=True, EmissionRate=0.0, MaxParticles=2500, Shape=1, ShapeRadius=0.25, LifetimeMin=0.3, LifetimeMax=0.85,
        SpeedMin=2.5, SpeedMax=11.0, Direction=(0, 1, 0), Spread=180.0, Drag=3.2, StartColor=(1.6, 0.8, 0.3, 1.0),
        EndColor=(1.0, 0.2, 0.1, 0.0), StartSize=0.38, EndSize=0.0, SortingOrder=30))
    sparks = s.entity("Uchqunlar", parent=fx, ParticleSystem=k.particles(
        Playing=True, EmissionRate=0.0, MaxParticles=1500, LifetimeMin=0.08, LifetimeMax=0.24, SpeedMin=4.0, SpeedMax=12.0,
        Direction=(0, 1, 0), Spread=180.0, Drag=5.0, StartColor=(2.0, 2.0, 2.0, 1.0), EndColor=(1.0, 0.8, 0.4, 0.0), StartSize=0.16,
        EndSize=0.0, SortingOrder=31))
    flashes = []
    for i in range(6):
        flashes.append(s.entity(f"Chaqnash {i + 1}", parent=fx, pos=(0, -100, 0), Light2D=k.light2d((1.0, 0.6, 0.3), 0.0, 6.0, 0.5, 1.5)))

    # ---- templates (a switched-off group: Instantiate makes active root copies)
    tpl = s.entity("Shablonlar", active=False)

    def bullet(name, frame, col, radius, scale, layer, damage, lifetime, from_player, speed):
        b = s.entity(name, parent=tpl, Rigidbody2D=k.rigidbody2d("Dynamic", layer=L[layer], bullet=True),
                     CircleCollider2D=k.circle2d(radius, density=0.1, sensor=True),
                     NativeScript=k.script("Projectile", Damage=damage, Lifetime=lifetime, FromPlayer=from_player, Speed=speed))
        s.entity("Visual", parent=b, scale=scale, SpriteRenderer=k.sprite(ATLAS, frame, col=col, order=15))
        return b

    t_bullet = bullet("O'q", "bullet", (0.9, 2.2, 3.0, 1.0), 0.16, (0.32, 0.62, 1), "PlayerShot", 1.0, 1.1, True, 26.0)
    t_ebullet = bullet("Dushman o'qi", "orb", (3.0, 1.1, 0.4, 1.0), 0.2, (0.65, 0.65, 1), "EnemyShot", 1.0, 4.0, False, 8.0)

    enemy_specs = [
        # kind index (Enemy::Kind), name, frame, scale, radius, density, hp, speed, score, colour (for effects)
        (0, "Quvg'in", "chaser", 1.05, 0.45, 1.0, 2.0, 5.2, 100, (1.0, 0.3, 0.45)),
        (1, "To'da", "swarmer", 0.72, 0.3, 0.5, 1.0, 6.8, 50, (0.4, 1.0, 0.45)),
        (2, "O'qchi", "shooter", 1.15, 0.55, 2.0, 4.0, 3.2, 250, (0.75, 0.4, 1.0)),
        (3, "Hujumchi", "dasher", 1.05, 0.45, 1.5, 3.0, 4.0, 200, (1.0, 0.82, 0.25)),
        (4, "Tank", "tank", 1.75, 0.95, 6.0, 14.0, 1.9, 600, (1.0, 0.5, 0.15)),
    ]
    t_enemies = []
    for kind, name, frame, scale, radius, density, hp, speed, score, col in enemy_specs:
        e = s.entity(name, parent=tpl, Tags=["Enemy"], Rigidbody2D=k.rigidbody2d("Dynamic", layer=L["Enemy"], linear_damping=0.5),
                     CircleCollider2D=k.circle2d(radius, density=density),
                     NativeScript=k.script("Enemy", Kind=("Enum", kind), Health=hp, Speed=speed, Score=score,
                                           Color=("Vec3", col)))
        s.entity("Visual", parent=e, scale=(scale, scale, 1), SpriteRenderer=k.sprite(ATLAS, frame, col=(1.25, 1.25, 1.25, 1), order=10))
        t_enemies.append(e)

    t_pickups = []
    for kind, name, frame, col in ((0, "Jon", "heart", (2.2, 0.7, 0.9, 1)), (1, "Tezotar", "bolt", (2.4, 2.0, 0.5, 1)),
                                   (2, "Uch o'q", "spread", (0.6, 2.2, 1.0, 1))):
        p = s.entity(f"Bonus: {name}", parent=tpl, Tags=["Pickup"], Rigidbody2D=k.rigidbody2d("Kinematic", layer=L["Pickup"]),
                     CircleCollider2D=k.circle2d(0.6, sensor=True), Light2D=k.light2d(tuple(c / 2.4 for c in col[:3]), 0.9, 3.0),
                     NativeScript=k.script("Pickup", Kind=("Enum", kind), Lifetime=9.0))
        s.entity("Halqa", parent=p, scale=(1.5, 1.5, 1), SpriteRenderer=k.sprite(ATLAS, "ring", col=(col[0] * 0.6, col[1] * 0.6, col[2] * 0.6, 0.8), order=5))
        s.entity("Visual", parent=p, scale=(0.95, 0.95, 1), SpriteRenderer=k.sprite(ATLAS, frame, col=col, order=6))
        t_pickups.append(p)

    t_portal = s.entity("Portal", parent=tpl, scale=(0.2, 0.2, 1), SpriteRenderer=k.sprite(ATLAS, "ring", col=(1.6, 0.5, 1.8, 1), order=4),
                        NativeScript=k.script("FxAnimator", Lifetime=0.75, StartScale=0.3, EndScale=2.4, Spin=4.0, FadeIn=0.25, FadeOut=0.3))
    t_wave = s.entity("Zarba to'lqini", parent=tpl, scale=(0.5, 0.5, 1), SpriteRenderer=k.sprite(ATLAS, "ring", col=(2, 2, 2, 1), order=29),
                      NativeScript=k.script("FxAnimator", Lifetime=0.45, StartScale=0.5, EndScale=4.5, FadeOut=0.45))
    t_popup = s.entity("Ochko", parent=tpl, scale=(0.75, 0.75, 1), Text=k.world_text("+100", (1.0, 1.0, 0.6, 1.0), order=1100),
                       NativeScript=k.script("FxAnimator", Lifetime=0.9, StartScale=0.6, EndScale=1.0, Rise=1.6, FadeOut=0.35, Pop=True))

    # ---- UI
    canvas = s.entity("UI", UITransform=k.ui_stretch(), UICanvas=k.ui_canvas())
    hud = s.entity("HUD", parent=canvas, UITransform=k.ui_stretch())

    def text(name, parent, txt, anchor, pivot, pos, size, font, col=(1, 1, 1, 1), h="Left", key=None, active=True, **extra):
        return s.entity(name, parent=parent, active=active, UITransform=k.ui_rect(anchor, None, pivot, pos, size, font),
                        Text=k.ui_text(txt, col, h, key=key), **extra)

    text("Hisob yorlig'i", hud, "HISOB", (0, 1), (0, 1), (48, -30), (400, 30), 26, (0.55, 0.9, 1.0, 0.85), key="hud.score_label")
    score_text = text("Hisob", hud, "0", (0, 1), (0, 1), (46, -60), (700, 76), 68)
    combo_text = text("Kombo", hud, "x1", (0, 1), (0, 1), (48, -142), (500, 44), 36, (1.0, 0.85, 0.3, 1.0))
    combo_bar = s.entity("Kombo vaqti", parent=hud, UITransform=k.ui_rect((0, 1), None, (0, 1), (50, -192), (260, 10)),
                         UIProgressBar=k.ui_progress(0.0, fill=(1.0, 0.8, 0.25, 1.0), padding=2.0, smooth=0.0))
    wave_text = text("To'lqin", hud, "1-TO'LQIN", (0.5, 1), (0.5, 1), (0, -28), (800, 56), 46, h="Center")
    enemies_text = text("Dushmanlar", hud, "", (0.5, 1), (0.5, 1), (0, -88), (800, 32), 26, (0.8, 0.85, 1.0, 0.85), h="Center")
    hearts = s.entity("Jonlar", parent=hud, UITransform=k.ui_rect((1, 1), None, (1, 1), (-44, -34), (5 * 54, 48)),
                      UILayoutGroup=k.ui_layout("Horizontal", spacing=6, align=5, control_width=False, expand_width=False))
    heart_ids = [s.entity(f"Jon {i + 1}", parent=hearts, UITransform=k.ui_rect(size=(48, 48)),
                          UIImage=k.ui_image(texture=ATLAS, frame="heart", preserve_aspect=True, raycast=False)) for i in range(5)]
    best_text = text("Rekord", hud, "", (1, 1), (1, 1), (-48, -94), (600, 32), 26, (0.75, 0.8, 1.0, 0.8), h="Right")
    power_text = text("Bonus", hud, "", (0, 0), (0, 0), (48, 40), (900, 40), 30, (0.6, 1.0, 0.6, 1.0))
    text("Siltanish yorlig'i", hud, "SILTANISH", (0, 0), (0, 0), (48, 92), (300, 26), 22, (0.55, 0.9, 1.0, 0.8), key="hud.dash")
    dash_bar = s.entity("Siltanish", parent=hud, UITransform=k.ui_rect((0, 0), None, (0, 0), (50, 84), (200, 8)),
                        UIProgressBar=k.ui_progress(1.0, fill=(0.35, 0.9, 1.0, 1.0), padding=2.0, smooth=0.0))
    banner = text("E'lon", hud, "", (0.5, 0.5), (0.5, 0.5), (0, 170), (1600, 120), 104, (1.0, 1.0, 1.0, 1.0), h="Center",
                  UIAnimation=k.ui_anim("Pop", "Fade", 0.35))
    banner_sub = text("E'lon izohi", banner, "", (0.5, 0.0), (0.5, 1.0), (0, -6), (1400, 50), 36, (0.75, 0.9, 1.0, 1.0), h="Center")
    demo_badge = text("Demo belgisi", hud, "DEMO - avtomatik o'yin", (0.5, 0), (0.5, 0), (0, 36), (1000, 36), 28, (1.0, 0.8, 0.35, 0.9),
                      h="Center", key="hud.demo", active=False)

    def dim(name, active):
        return s.entity(name, parent=canvas, active=active, UITransform=k.ui_stretch(order=10),
                        UIImage=k.ui_image((0.0, 0.01, 0.04, 0.6)), UIAnimation=k.ui_anim("Fade", "Fade", 0.25))

    def panel(name, parent, pos, size, padding=(44, 44, 40, 44), spacing=16):
        return s.entity(name, parent=parent, UITransform=k.ui_rect(pos=pos, size=size),
                        UIImage=k.ui_image(texture=ATLAS, frame="panel", kind="Sliced", border_scale=1.0),
                        UILayoutGroup=k.ui_layout("Vertical", padding, spacing, align=1, fit_height=True),
                        UIAnimation=k.ui_anim("Pop", "None", 0.3))

    def button(name, parent, label, key, message, select=False):
        return s.entity(name, parent=parent, UITransform=k.ui_rect(size=(460, 72), font=32),
                        UIButton=k.ui_button(UI_BUTTON["normal"], UI_BUTTON["hover"], UI_BUTTON["pressed"], message, game),
                        UIImage=k.ui_image(texture=ATLAS, frame="button", kind="Sliced", border_scale=1.0),
                        Text=k.ui_text(label, (1, 1, 1, 1), "Center", key=key), UINavigation=k.ui_nav("Vertical", select),
                        UIAnimation=k.ui_anim(hover=1.05, press=0.95))

    def label(name, parent, txt, size, font, col=(1, 1, 1, 1), key=None):
        return s.entity(name, parent=parent, UITransform=k.ui_rect(size=size, font=font), Text=k.ui_text(txt, col, "Center", key=key))

    # main menu
    main_menu = dim("Bosh menyu", True)
    title_root = s.entity("Sarlavha", parent=main_menu, UITransform=k.ui_rect(pos=(0, 268), size=(1700, 180), font=156),
                          UIAnimation=k.ui_anim("SlideDown", "None", 0.6, slide=120))
    s.entity("Sarlavha soyasi", parent=title_root, UITransform=k.ui_rect((0, 0), (1, 1), pos=(7, -7), size=(0, 0), font=156),
             Text=k.ui_text("NEON MAYDON", (0.9, 0.1, 0.7, 0.8), "Center", key="menu.title"))
    s.entity("Sarlavha matni", parent=title_root, UITransform=k.ui_rect((0, 0), (1, 1), size=(0, 0), font=156),
             Text=k.ui_text("NEON MAYDON", (0.6, 1.0, 1.0, 1.0), "Center", key="menu.title"))
    s.entity("Taglavha", parent=main_menu, UITransform=k.ui_rect(pos=(0, 152), size=(1400, 50), font=38),
             Text=k.ui_text("Top-down shooter  ·  Zamin Engine namunasi", (0.75, 0.85, 1.0, 0.9), "Center", key="menu.subtitle"))
    menu_panel = panel("Menyu paneli", main_menu, (0, -110), (560, 330))
    play_button = button("O'ynash", menu_panel, "O'ynash", "menu.play", "Play", True)
    language_button = button("Til", menu_panel, "Til: O'zbekcha", None, "Language")
    button("Chiqish", menu_panel, "Chiqish", "menu.quit", "Quit")
    menu_best = s.entity("Bosh menyu rekordi", parent=main_menu, UITransform=k.ui_rect(pos=(0, -330), size=(1200, 44), font=32),
                         Text=k.ui_text("", (1.0, 0.85, 0.35, 1.0), "Center"))
    s.entity("Boshqaruv", parent=main_menu, UITransform=k.ui_rect((0.5, 0), None, (0.5, 0), (0, 56), (1800, 70), font=25),
             Text=k.ui_text("WASD - yurish   ·   Sichqoncha - nishon   ·   LKM - otish   ·   Space - siltanish   ·   Esc - pauza\n"
                            "Gamepad: chap stik - yurish, o'ng stik - nishon va otish, A - siltanish",
                            (0.7, 0.8, 0.95, 0.85), "Center", key="menu.controls"))

    # pause
    pause_menu = dim("Pauza menyusi", False)
    pause_panel = panel("Pauza paneli", pause_menu, (0, 0), (560, 420))
    label("Pauza sarlavhasi", pause_panel, "PAUZA", (460, 80), 60, (0.6, 1.0, 1.0, 1.0), "pause.title")
    resume_button = button("Davom etish", pause_panel, "Davom etish", "pause.resume", "Resume", True)
    button("Qaytadan", pause_panel, "Qaytadan", "pause.restart", "Restart")
    button("Bosh menyuga", pause_panel, "Bosh menyu", "pause.menu", "Menu")

    # game over
    over_menu = dim("O'yin tugadi", False)
    over_panel = panel("Natija paneli", over_menu, (0, 0), (640, 560))
    label("Natija sarlavhasi", over_panel, "O'YIN TUGADI", (560, 80), 64, (1.0, 0.4, 0.5, 1.0), "over.title")
    over_score = label("Natija hisobi", over_panel, "0", (560, 90), 84)
    over_wave = label("Natija to'lqini", over_panel, "", (560, 40), 30, (0.75, 0.85, 1.0, 1.0))
    over_record = label("Natija rekordi", over_panel, "", (560, 44), 34, (1.0, 0.85, 0.35, 1.0))
    retry_button = button("Qayta o'ynash", over_panel, "Qayta o'ynash", "over.retry", "Restart", True)
    button("Natija: bosh menyu", over_panel, "Bosh menyu", "pause.menu", "Menu")

    # ---- the game script
    s.add(game, "NativeScript", k.script(
        "ShooterGame",
        Player=k.ref(player), Reticle=k.ref(reticle), Shield=k.ref(shield), Thruster=k.ref(thruster),
        BulletTemplate=k.ref(t_bullet), EnemyBulletTemplate=k.ref(t_ebullet),
        ChaserTemplate=k.ref(t_enemies[0]), SwarmerTemplate=k.ref(t_enemies[1]), ShooterTemplate=k.ref(t_enemies[2]),
        DasherTemplate=k.ref(t_enemies[3]), TankTemplate=k.ref(t_enemies[4]),
        HealthPickup=k.ref(t_pickups[0]), RapidPickup=k.ref(t_pickups[1]), SpreadPickup=k.ref(t_pickups[2]),
        PortalTemplate=k.ref(t_portal), ShockwaveTemplate=k.ref(t_wave), PopupTemplate=k.ref(t_popup),
        Explosion=k.ref(explosion), Sparks=k.ref(sparks), FlashLights=k.ref(fx),
        ScoreText=k.ref(score_text), ComboText=k.ref(combo_text), ComboBar=k.ref(combo_bar), WaveText=k.ref(wave_text),
        EnemiesText=k.ref(enemies_text), Hearts=k.ref(hearts), BestText=k.ref(best_text), PowerText=k.ref(power_text),
        DashBar=k.ref(dash_bar), Banner=k.ref(banner), BannerSub=k.ref(banner_sub), DemoBadge=k.ref(demo_badge), Hud=k.ref(hud),
        MainMenu=k.ref(main_menu), PauseMenu=k.ref(pause_menu), GameOverMenu=k.ref(over_menu), MenuBest=k.ref(menu_best),
        PlayButton=k.ref(play_button), LanguageButton=k.ref(language_button), ResumeButton=k.ref(resume_button),
        RetryButton=k.ref(retry_button), OverScore=k.ref(over_score), OverWave=k.ref(over_wave), OverRecord=k.ref(over_record),
        ArenaHalfSize=("Vec2", (ARENA_W, ARENA_H))))
    s.add(game, "AudioSource", k.audio_source("Audio/music.wav", 0.7, loop=True, play=True, output="Music", stream=True))
    s.add(player, "NativeScript", k.script("PlayerShip", MoveSpeed=9.0, Acceleration=11.0, FireRate=11.0, DashSpeed=24.0,
                                           DashTime=0.16, DashCooldown=0.9, MaxHealth=5))
    assert len(heart_ids) == 5
    return s


def write_localization():
    uz = {
        "$name": "O'zbekcha",
        "menu": {"title": "NEON MAYDON", "subtitle": "Top-down shooter  ·  Zamin Engine namunasi", "play": "O'ynash", "quit": "Chiqish",
                 "language": "Til: {0}", "best": "Rekord: {0}   ·   eng uzoq to'lqin: {1}",
                 "controls": "WASD - yurish   ·   Sichqoncha - nishon   ·   LKM - otish   ·   Space - siltanish   ·   Esc - pauza\n"
                             "Gamepad: chap stik - yurish, o'ng stik - nishon va otish, A - siltanish"},
        "hud": {"score_label": "HISOB", "wave": "{0}-TO'LQIN", "enemies": "Dushmanlar: {0}", "best": "Rekord: {0}", "dash": "SILTANISH",
                "demo": "DEMO - avtomatik o'yin", "combo": "x{0}  kombo {1}", "rapid": "TEZOTAR  {0}", "spread": "UCH O'Q  {0}",
                "wave_sub": "{0} ta dushman", "boss_sub": "Tank to'lqini!", "cleared": "TO'LQIN TOZALANDI", "bonus": "+{0} bonus",
                "streak": "{0} TA KETMA-KET!", "get_ready": "TAYYORLANING"},
        "pause": {"title": "PAUZA", "resume": "Davom etish", "restart": "Qaytadan", "menu": "Bosh menyu"},
        "over": {"title": "O'YIN TUGADI", "retry": "Qayta o'ynash", "wave": "{0}-to'lqingacha yetdingiz  ·  {1} ta dushman",
                 "record": "YANGI REKORD!", "best": "Rekord: {0}"},
        "pickup": {"health": "+1 JON", "rapid": "TEZOTAR!", "spread": "UCH O'Q!"},
    }
    en = {
        "$name": "English",
        "menu": {"title": "NEON ARENA", "subtitle": "Top-down shooter  ·  a Zamin Engine sample", "play": "Play", "quit": "Quit",
                 "language": "Language: {0}", "best": "Best: {0}   ·   furthest wave: {1}",
                 "controls": "WASD - move   ·   Mouse - aim   ·   LMB - fire   ·   Space - dash   ·   Esc - pause\n"
                             "Gamepad: left stick - move, right stick - aim and fire, A - dash"},
        "hud": {"score_label": "SCORE", "wave": "WAVE {0}", "enemies": "Enemies: {0}", "best": "Best: {0}", "dash": "DASH",
                "demo": "DEMO - autoplay", "combo": "x{0}  combo {1}", "rapid": "RAPID FIRE  {0}", "spread": "TRIPLE SHOT  {0}",
                "wave_sub": "{0} enemies", "boss_sub": "Tank wave!", "cleared": "WAVE CLEARED", "bonus": "+{0} bonus",
                "streak": "{0} IN A ROW!", "get_ready": "GET READY"},
        "pause": {"title": "PAUSED", "resume": "Resume", "restart": "Restart", "menu": "Main menu"},
        "over": {"title": "GAME OVER", "retry": "Play again", "wave": "Reached wave {0}  ·  {1} enemies down", "record": "NEW RECORD!",
                 "best": "Best: {0}"},
        "pickup": {"health": "+1 LIFE", "rapid": "RAPID FIRE!", "spread": "TRIPLE SHOT!"},
    }
    k.write_json(os.path.join(ASSETS, "Localization/uz.json"), uz)
    k.write_json(os.path.join(ASSETS, "Localization/en.json"), en)


def main():
    make_atlas()
    make_floor()
    make_space()
    make_icon()
    make_sounds()
    write_localization()
    scene = build_scene()
    uuid = scene.save(ASSETS, "Scenes/Arena.zscene", NAME)
    actions = [
        ("Fire", ["Mouse:Left", "Key:J", "GamepadAxis:RightTrigger+", "Gamepad:RightBumper"]),
        ("Dash", ["Key:Space", "Key:LeftShift", "Gamepad:A", "Gamepad:LeftBumper"]),
        ("Pause", ["Key:Escape", "Key:P", "Gamepad:Start"]),
    ]
    axes = [
        k.axis("MoveX", ["Key:A", "Gamepad:DPadLeft"], ["Key:D", "Gamepad:DPadRight"], "LeftX"),
        k.axis("MoveY", ["Key:S", "Gamepad:DPadDown"], ["Key:W", "Gamepad:DPadUp"], "LeftY", invert=True),
        k.axis("AimX", ["Key:Left"], ["Key:Right"], "RightX"),
        k.axis("AimY", ["Key:Down"], ["Key:Up"], "RightY", invert=True),
    ]
    vectors = [{"Name": "Move", "X": "MoveX", "Y": "MoveY", "Mode": "Clamp", "Deadzone": 0.0},
               {"Name": "Aim", "X": "AimX", "Y": "AimY", "Mode": "Clamp", "Deadzone": 0.25}]
    k.write_project(PROJECT, NAME, "Neon Maydon", "Scenes/Arena.zscene", uuid, "1.0.0", [("Scenes/Arena.zscene", uuid)], actions, axes,
                    vectors, ["Player", "Enemy", "Pickup"], {"names": LAYERS, "matrix": k.collision_matrix(LAYERS, PAIRS)},
                    description="2D twin-stick shooter: neon maydon, 5 xil dushman, to'lqinlar, kombo, bonuslar, 2D yorug'lik va zarralar")
    k.write_module_cmake(PROJECT, NAME, "Zamin namuna o'yini: Neon Maydon (top-down shooter) - C++ skriptlar moduli (libTopDownShooter.so)")
    print("TopDownShooter: tayyor")


if __name__ == "__main__":
    main()
