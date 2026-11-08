"""Projects/Breakout: "Zamin Breakout" - its textures, sounds, scene and project file.

    python3 Tools/samples/gen_breakout.py

Everything under Projects/Breakout/Assets and the .zproject are made here; the gameplay and the six levels'
layouts are C++ (Projects/Breakout/Source).
"""

import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import samplekit as k  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NAME = "Breakout"
PROJECT = os.path.join(ROOT, "Projects", NAME)
ASSETS = os.path.join(PROJECT, "Assets")
ATLAS = "Textures/breakout_atlas.png"

FIELD_HALF_W = 10.5   # inner half width of the playfield (units)
FIELD_TOP = 11.0      # inner top edge
FIELD_BOTTOM = -12.5  # where a ball is lost

# ------------------------------------------------------------------------------------------- textures


def bevel_block(w, h, radius, base_top, base_bottom, rim=(1, 1, 1, 0.55), gloss=0.35):
    """A glossy rounded block: a vertical gradient, a light top edge, a dark bottom edge and a gloss band."""
    img = k.image(w, h)
    x, y = k.coords(w, h)
    sd = k.sd_round_box(x, y, w / 2 - 2, h / 2 - 2, radius)
    t = np.clip(y / h + 0.5, 0, 1)
    col = k.mix_color(base_top, base_bottom, t)
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = col[:, :, i]
    img[:, :, 3] = cov
    # inner bevel: light at the top, shadow at the bottom
    inner = k.smooth(-1.0, -5.0, sd)
    top_light = np.clip(-y / h * 2, 0, 1) * (1 - inner) * 0.6
    bottom_dark = np.clip(y / h * 2, 0, 1) * (1 - inner) * 0.45
    for i in range(3):
        img[:, :, i] = np.clip(img[:, :, i] + top_light - bottom_dark, 0, 1)
    # gloss band across the upper half
    band = k.smooth(0.0, 0.18, -(y / h) - 0.05) * k.smooth(0.42, 0.25, -(y / h)) * inner * gloss
    for i in range(3):
        img[:, :, i] = np.clip(img[:, :, i] + band, 0, 1)
    k.stroke(img, sd + 0.8, rim, 1.6)
    img[:, :, 3] = np.minimum(img[:, :, 3], cov)
    return img


def crack(img, seed):
    """Dark crack lines over a block (a brick that took a hit)."""
    h, w = img.shape[:2]
    rng = np.random.default_rng(seed)
    x, y = k.coords(w, h)
    for _ in range(3):
        cx, cy = rng.uniform(-w * 0.2, w * 0.2), rng.uniform(-h * 0.15, h * 0.15)
        pts = [(cx, cy)]
        ang = rng.uniform(0, 2 * math.pi)
        for _ in range(4):
            ang += rng.uniform(-0.8, 0.8)
            step = rng.uniform(6, 14)
            pts.append((pts[-1][0] + math.cos(ang) * step, pts[-1][1] + math.sin(ang) * step))
        for a, b in zip(pts, pts[1:]):
            ex, ey = b[0] - a[0], b[1] - a[1]
            tt = np.clip(((x - a[0]) * ex + (y - a[1]) * ey) / (ex * ex + ey * ey), 0, 1)
            d = np.sqrt((x - a[0] - ex * tt) ** 2 + (y - a[1] - ey * tt) ** 2) - 0.9
            cov = k.smooth(0.6, -0.6, d) * (img[:, :, 3] > 0.5)
            for i in range(3):
                img[:, :, i] = img[:, :, i] * (1 - cov * 0.75)
    return img


def make_atlas():
    atlas = k.Atlas(1024, 512)
    # bricks: white (tinted per row / level), steel, gold, bomb
    atlas.add("brick", bevel_block(96, 40, 8, (1.0, 1.0, 1.0), (0.72, 0.72, 0.78)))
    atlas.add("brick_cracked", crack(bevel_block(96, 40, 8, (1.0, 1.0, 1.0), (0.72, 0.72, 0.78)), 3))
    steel = bevel_block(96, 40, 6, (0.85, 0.88, 0.95), (0.45, 0.48, 0.56), gloss=0.5)
    x, y = k.coords(96, 40)
    for rx in (-38, 38):
        for ry in (-10, 10):
            k.fill(steel, k.sd_circle(x - rx, y - ry, 2.6), (0.35, 0.37, 0.45, 1.0))
            k.fill(steel, k.sd_circle(x - rx + 0.8, y - ry - 0.8, 1.0), (0.95, 0.97, 1.0, 0.9))
    atlas.add("steel", steel)
    atlas.add("steel_cracked", crack(steel.copy(), 9))
    gold = bevel_block(96, 40, 6, (1.0, 0.88, 0.45), (0.7, 0.45, 0.08), rim=(1.0, 0.95, 0.7, 0.8), gloss=0.55)
    k.stroke(gold, k.sd_round_box(x, y, 36, 11, 4), (1.0, 0.95, 0.7, 0.55), 1.5)
    atlas.add("gold", gold)
    bomb = bevel_block(96, 40, 8, (1.0, 0.35, 0.3), (0.55, 0.05, 0.08))
    k.fill(bomb, k.sd_circle(x, y + 2, 10), (0.08, 0.05, 0.06, 1.0))
    k.fill(bomb, k.sd_round_box(x - 6, y - 10, 2, 4, 1), (0.08, 0.05, 0.06, 1.0))
    k.glow(bomb, k.sd_circle(x - 8, y - 15, 2.5), (1.0, 0.9, 0.4, 1.0), 3, 1.0)
    k.fill(bomb, k.sd_circle(x - 8, y - 15, 2.5), (1.0, 0.95, 0.6, 1.0))
    k.fill(bomb, k.sd_circle(x - 3, y - 2, 3), (1, 1, 1, 0.5))
    atlas.add("bomb", bomb)

    # paddle: a metallic capsule with a neon rim and a light strip
    w, h = 256, 56
    img = k.image(w, h)
    x, y = k.coords(w, h)
    sd = k.sd_round_box(x, y, w / 2 - 6, h / 2 - 6, h / 2 - 6)
    k.glow(img, sd, (0.3, 0.9, 1.0, 1.0), 4, 0.8)
    col = k.mix_color((0.85, 0.92, 1.0), (0.25, 0.3, 0.42), np.clip(y / h + 0.5, 0, 1))
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = img[:, :, i] * (1 - cov) + col[:, :, i] * cov
    img[:, :, 3] = np.maximum(img[:, :, 3], cov)
    k.stroke(img, sd + 1.5, (0.5, 1.0, 1.0, 1.0), 3)
    strip = k.sd_round_box(x, y + 2, w / 2 - 40, 3, 3)
    k.glow(img, strip, (0.4, 1.0, 1.0, 1.0), 4, 0.9)
    k.fill(img, strip, (0.85, 1.0, 1.0, 1.0))
    atlas.add("paddle", img)
    # laser turret
    img = k.image(32, 40)
    x, y = k.coords(32, 40)
    sd = k.sd_round_box(x, y + 4, 9, 14, 4)
    k.fill(img, sd, (0.85, 0.3, 0.35, 1.0))
    k.stroke(img, sd + 1, (1.0, 0.7, 0.7, 1.0), 2)
    k.glow(img, k.sd_circle(x, y - 10, 3), (1.0, 0.4, 0.4, 1.0), 4, 1.0)
    atlas.add("turret", img)

    # ball: a glossy white sphere with a halo
    img = k.image(64, 64)
    x, y = k.coords(64, 64)
    sd = k.sd_circle(x, y, 22)
    k.glow(img, sd, (0.7, 0.9, 1.0, 1.0), 4, 0.6)
    shade = np.clip(1.0 - np.sqrt((x + 6) ** 2 + (y + 6) ** 2) / 34.0, 0.25, 1.0)
    cov = k.smooth(0.5, -0.5, sd)
    for i, c in enumerate((0.92, 0.97, 1.0)):
        img[:, :, i] = img[:, :, i] * (1 - cov) + shade * c * cov
    img[:, :, 3] = np.maximum(img[:, :, 3], cov)
    k.fill(img, k.sd_circle(x + 7, y + 7, 5), (1, 1, 1, 0.9))
    atlas.add("ball", img)

    # power-up capsule (tinted) and icons
    img = k.image(112, 48)
    x, y = k.coords(112, 48)
    sd = k.sd_round_box(x, y, 50, 18, 18)
    k.glow(img, sd, (1, 1, 1, 1), 5, 0.7)
    col = k.mix_color((1.0, 1.0, 1.0), (0.55, 0.55, 0.62), np.clip(y / 48 + 0.5, 0, 1))
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = img[:, :, i] * (1 - cov) + col[:, :, i] * cov
    img[:, :, 3] = np.maximum(img[:, :, 3], cov)
    k.fill(img, k.sd_round_box(x, y - 8, 40, 3, 3), (1, 1, 1, 0.55))
    atlas.add("capsule", img)

    # small things: a soft glow, a ring, a laser beam, a heart, a ball icon
    img = k.image(64, 64)
    x, y = k.coords(64, 64)
    r = np.sqrt(x * x + y * y) / 32.0
    img[:, :, :3] = 1.0
    img[:, :, 3] = np.clip(1.0 - r, 0, 1) ** 2
    atlas.add("glow", img)
    img = k.image(128, 128)
    x, y = k.coords(128, 128)
    sd = np.abs(k.sd_circle(x, y, 54)) - 3
    k.glow(img, sd, (1, 1, 1, 1), 6, 0.9)
    k.fill(img, sd, (1, 1, 1, 1))
    atlas.add("ring", img)
    img = k.image(24, 128)
    x, y = k.coords(24, 128)
    sd = k.sd_round_box(x, y, 3, 60, 3)
    k.glow(img, sd, (1, 1, 1, 1), 5, 1.0)
    k.fill(img, sd, (1, 1, 1, 1))
    atlas.add("beam", img)
    img = k.image(48, 48)
    x, y = k.coords(48, 48, (24, 22))
    hs = 0.75
    left = k.sd_circle(x / hs + 11, y / hs + 6, 13) * hs
    right = k.sd_circle(x / hs - 11, y / hs + 6, 13) * hs
    tip = k.sd_polygon(x / hs, y / hs, [(-23, 0), (23, 0), (0, 26)]) * hs
    sd = np.minimum(np.minimum(left, right), tip)
    k.fill(img, sd, (1.0, 0.35, 0.45, 1))
    atlas.add("heart", img)
    img = k.image(48, 48)
    x, y = k.coords(48, 48)
    sd = k.sd_circle(x, y, 15)
    shade = np.clip(1.0 - np.sqrt((x + 5) ** 2 + (y + 5) ** 2) / 26.0, 0.3, 1.0)
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = shade
    img[:, :, 3] = cov
    atlas.add("ball_icon", img)

    # frame pieces of the playfield (9-slice): a dark metal bar with a neon line
    img = k.image(64, 64)
    x, y = k.coords(64, 64)
    sd = k.sd_round_box(x, y, 30, 30, 8)
    col = k.mix_color((0.22, 0.24, 0.34), (0.08, 0.09, 0.14), np.clip(y / 64 + 0.5, 0, 1))
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = col[:, :, i]
    img[:, :, 3] = cov
    k.stroke(img, sd + 3, (0.55, 0.85, 1.0, 1.0), 2)
    atlas.add("frame", img, border=(14, 14, 14, 14))

    k.ui_panel_sprites(atlas, (0.09, 0.06, 0.2), (0.03, 0.02, 0.09), (0.85, 0.55, 1.0), (0.7, 0.4, 1.0))
    return atlas.save(ASSETS, ATLAS, NAME)


def make_backgrounds():
    # backdrop: a night gradient with soft bokeh lights
    w, h = 512, 288
    img = k.image(w, h, (0, 0, 0, 1))
    x, y = k.coords(w, h)
    t = np.clip(y / h + 0.5, 0, 1)
    top = np.array([0.1, 0.04, 0.22])
    bottom = np.array([0.02, 0.05, 0.14])
    for i in range(3):
        img[:, :, i] = top[i] * (1 - t) + bottom[i] * t
    n = k.value_noise(w, h, 3, seed=12, octaves=4)
    for i, c in enumerate((0.25, 0.1, 0.4)):
        img[:, :, i] += np.clip(n - 0.5, 0, 1) * c
    rng = np.random.default_rng(5)
    palette = [(1.0, 0.4, 0.8), (0.4, 0.7, 1.0), (0.7, 0.5, 1.0), (1.0, 0.75, 0.4)]
    for _ in range(46):
        cx, cy = rng.uniform(-w / 2, w / 2), rng.uniform(-h / 2, h / 2)
        r = rng.uniform(4, 22)
        col = palette[rng.integers(0, len(palette))]
        a = rng.uniform(0.05, 0.22)
        d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2)
        disc = k.smooth(r, r - 2.5, d) * a
        for i in range(3):
            img[:, :, i] += col[i] * disc
    k.write_png(os.path.join(ASSETS, "Textures/backdrop.png"), np.clip(img, 0, 1))
    k.write_zmeta(ASSETS, "Textures/backdrop.png", "Texture", {}, NAME)

    # the field's floor: dark glass panels (64 px, 2 x 2 per tile) with lighter seams and a soft top light
    s = 128
    img = k.image(s, s, (0, 0, 0, 1))
    ys, xs = np.mgrid[0:s, 0:s].astype(np.float64)
    px, py = (xs % 64) - 31.5, (ys % 64) - 31.5
    sd = k.sd_round_box(px, py, 29, 29, 6)
    seam = k.smooth(1.5, -0.5, np.abs(sd + 1.0))
    inside = k.smooth(0.5, -0.5, sd)
    shade = k.value_noise(s, s, 2, seed=4, octaves=3)
    light = np.clip(0.5 - py / 64.0, 0, 1)
    base = np.array([0.075, 0.08, 0.16])
    for i in range(3):
        img[:, :, i] = base[i] * (0.75 + 0.35 * shade) * (0.8 + 0.4 * light * inside) + seam * np.array([0.2, 0.24, 0.48])[i]
    k.write_png(os.path.join(ASSETS, "Textures/field.png"), np.clip(img, 0, 1))
    k.write_zmeta(ASSETS, "Textures/field.png", "Texture", {}, NAME)


def make_icon():
    size = 256
    img = k.image(size, size)
    x, y = k.coords(size, size)
    sd = k.sd_round_box(x, y, 120, 120, 44)
    bg = k.mix_color((0.16, 0.06, 0.3), (0.03, 0.03, 0.1), np.clip(y / size + 0.5, 0, 1))
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = bg[:, :, i]
    img[:, :, 3] = cov
    k.stroke(img, sd + 3, (0.85, 0.55, 1.0, 1.0), 5)
    colors = [(1.0, 0.35, 0.45), (1.0, 0.65, 0.25), (1.0, 0.9, 0.3), (0.4, 0.95, 0.5), (0.35, 0.8, 1.0)]
    for row, c in enumerate(colors):
        for col in range(4):
            bx = -66 + col * 44
            by = -78 + row * 24
            b = k.sd_round_box(x - bx, y - by, 19, 9, 4)
            k.fill(img, b, (*c, 1.0))
            k.fill(img, k.sd_round_box(x - bx, y - by - 4, 15, 2, 2), (1, 1, 1, 0.45))
    k.glow(img, k.sd_circle(x - 20, y - 52, 13), (0.7, 0.9, 1.0, 1.0), 12, 0.9)
    k.fill(img, k.sd_circle(x - 20, y - 52, 13), (0.95, 0.98, 1.0, 1.0))
    pad = k.sd_round_box(x, y - 88, 56, 10, 10)
    k.glow(img, pad, (0.3, 0.9, 1.0, 1.0), 8, 0.9)
    k.fill(img, pad, (0.75, 0.95, 1.0, 1.0))
    img[:, :, 3] = np.minimum(img[:, :, 3], cov)
    k.write_png(os.path.join(ASSETS, "Textures/GameIcon.png"), img)
    k.write_zmeta(ASSETS, "Textures/GameIcon.png", "Texture", {}, NAME)


# --------------------------------------------------------------------------------------------- sounds


def make_sounds():
    R = k.RATE
    out = {}
    n = int(0.12 * R)
    out["paddle"] = k.osc(np.linspace(520, 380, n), 0.12, "sine") * k.exp_decay(n, 0.05) + \
        k.osc(1040, 0.12, "tri") * k.exp_decay(n, 0.02) * 0.4
    # brick ping: a glassy bell (pitch is varied by the game)
    n = int(0.35 * R)
    f0 = 880.0
    out["brick"] = (k.osc(f0, 0.35, "sine") + 0.5 * k.osc(f0 * 2.76, 0.35, "sine") * k.exp_decay(n, 0.05) +
                    0.3 * k.osc(f0 * 5.4, 0.35, "sine") * k.exp_decay(n, 0.02)) * k.exp_decay(n, 0.12)
    n = int(0.45 * R)
    shatter = k.highpass(k.noise(0.45, 4), 2500) * k.exp_decay(n, 0.07)
    out["break"] = shatter * 0.8 + (k.osc(1320, 0.45, "sine") + 0.4 * k.osc(1980, 0.45, "sine")) * k.exp_decay(n, 0.1) * 0.6
    n = int(0.25 * R)
    out["steel"] = (k.osc(1700, 0.25, "square") * 0.3 + k.osc(2550, 0.25, "sine")) * k.exp_decay(n, 0.04) + \
        k.highpass(k.noise(0.25, 5), 4000) * k.exp_decay(n, 0.01) * 0.6
    n = int(0.4 * R)
    out["gold"] = (k.osc(620, 0.4, "sine") + 0.6 * k.osc(1240 * 1.01, 0.4, "sine") + 0.3 * k.osc(1860, 0.4, "tri")) * k.exp_decay(n, 0.12)
    n = int(0.9 * R)
    out["bomb"] = k.lowpass(k.noise(0.9, 6), np.linspace(4000, 150, n)) * k.exp_decay(n, 0.25) * 1.4 + \
        k.osc(np.linspace(110, 35, n), 0.9, "sine") * k.exp_decay(n, 0.2)
    # power-ups
    n = int(0.5 * R)
    buf = np.zeros(n)
    for i, note in enumerate(("E5", "G#5", "B5", "E6")):
        k.place(buf, k.osc(k.note_freq(note), 0.15, "square") * k.exp_decay(int(0.15 * R), 0.06), i * 0.06, 0.5)
    out["powerup"] = k.lowpass(buf, 6000)
    n = int(0.3 * R)
    out["powerup_drop"] = k.osc(np.linspace(300, 700, n), 0.3, "tri") * np.sin(np.linspace(0, np.pi, n)) * 0.6
    n = int(0.2 * R)
    out["laser"] = k.osc(np.linspace(2200, 500, n), 0.2, "saw") * k.exp_decay(n, 0.06) * 0.6
    n = int(0.25 * R)
    out["launch"] = k.lowpass(k.noise(0.25, 7), np.linspace(500, 5000, n)) * np.sin(np.linspace(0, np.pi, n)) + \
        k.osc(np.linspace(300, 900, n), 0.25, "sine") * k.exp_decay(n, 0.1) * 0.5
    n = int(1.0 * R)
    out["ball_lost"] = k.osc(np.linspace(600, 90, n), 1.0, "tri") * k.exp_decay(n, 0.35) + \
        k.lowpass(k.noise(1.0, 8), 600) * k.exp_decay(n, 0.15) * 0.4
    buf = np.zeros(int(1.6 * R))
    for i, note in enumerate(("C5", "E5", "G5", "C6")):
        k.place(buf, k.osc(k.note_freq(note), 0.5, "square") * k.exp_decay(int(0.5 * R), 0.2), i * 0.11, 0.35)
    for note in ("C5", "E5", "G5", "C6"):
        k.place(buf, k.lowpass(k.osc(k.note_freq(note), 1.0, "saw"), 3000) * k.exp_decay(int(1.0 * R), 0.4), 0.5, 0.2)
    out["level_clear"] = buf
    buf = np.zeros(int(2.0 * R))
    for i, note in enumerate(("G4", "E4", "C4", "G3")):
        tone = k.osc(k.note_freq(note), 0.7, "tri") + 0.3 * k.osc(k.note_freq(note) * 1.006, 0.7, "saw")
        k.place(buf, k.lowpass(tone, 2500) * k.exp_decay(int(0.7 * R), 0.35), i * 0.28, 0.6)
    out["gameover"] = buf
    n = int(0.06 * R)
    out["ui_click"] = k.osc(1150, 0.06, "square") * k.exp_decay(n, 0.012) * 0.6 + k.osc(2300, 0.06, "sine") * k.exp_decay(n, 0.006) * 0.4
    for name, s in out.items():
        rel = f"Audio/{name}.wav"
        k.write_wav(os.path.join(ASSETS, rel), s)
        k.write_zmeta(ASSETS, rel, "Audio", {"LoadMode": "Decompress", "Preload": True, "Volume": 1.0}, NAME)
    make_music()


def make_music():
    """A bright electro loop: 124 BPM, 8 bars, F - Dm - Bb - C."""
    R = k.RATE
    beat = 60.0 / 124.0
    bars = 8
    total = int(bars * 4 * beat * R)
    buf = np.zeros(total + R)
    chords = [("F2", ("F4", "A4", "C5")), ("D2", ("D4", "F4", "A4")), ("A#1", ("A#3", "D4", "F4")), ("C2", ("C4", "E4", "G4"))]
    kick = k.osc(np.linspace(150, 45, int(0.22 * R)), 0.22, "sine") * k.exp_decay(int(0.22 * R), 0.07)
    clap = k.highpass(k.noise(0.2, 3), 900) * k.exp_decay(int(0.2 * R), 0.05)
    hat = k.highpass(k.noise(0.04, 9), 7000) * k.exp_decay(int(0.04 * R), 0.01)
    for bar in range(bars):
        root, triad = chords[(bar // 2) % 4]
        t0 = bar * 4 * beat
        for i in range(8):  # off-beat bass
            f = k.note_freq(root) * 2.0
            tone = k.lowpass(k.osc(f, beat / 2 * 0.9, "square"), 900) * k.env(int(beat / 2 * 0.9 * R), 0.003, 0.15, 0.3, 0.05, 0.03)
            k.place(buf, tone, t0 + i * beat / 2 + beat / 4, 0.4)
        pattern = [0, 1, 2, 1, 0, 2, 1, 2]
        for i in range(16):  # plucks
            note = triad[pattern[i % 8]]
            f = k.note_freq(note) * (2.0 if i % 8 >= 6 else 1.0)
            tone = (k.osc(f, beat / 4, "tri") + 0.3 * k.osc(f * 2, beat / 4, "sine")) * k.exp_decay(int(beat / 4 * R), 0.06)
            k.place(buf, tone, t0 + i * beat / 4, 0.2)
        for b in range(4):
            k.place(buf, kick, t0 + b * beat, 0.85)
            if b % 2 == 1:
                k.place(buf, clap, t0 + b * beat, 0.4)
            for hh in range(4):
                k.place(buf, hat, t0 + b * beat + hh * beat / 4, 0.12 if hh % 2 else 0.06)
    buf[:R] += buf[total:total + R]
    rel = "Audio/music.wav"
    k.write_wav(os.path.join(ASSETS, rel), buf[:total], peak=0.8)
    k.write_zmeta(ASSETS, rel, "Audio", {"LoadMode": "Stream", "Preload": False, "Volume": 1.0}, NAME)


# ---------------------------------------------------------------------------------------------- scene

LAYERS = ["Default", "Wall", "Ball", "Brick"]
L = {n: i for i, n in enumerate(LAYERS)}
PAIRS = [("Default", n) for n in LAYERS] + [("Ball", "Wall"), ("Ball", "Brick")]


def build_scene():
    s = k.SceneBuilder("Breakout", 8820000000000000)
    s.settings = k.settings_2d(ambient2d=(0.75, 0.75, 0.95), ambient2d_intensity=0.7, bloom=(0.85, 0.55, 0.75), vignette=0.32,
                               saturation=1.12, contrast=1.06, chroma=0.08, grain=0.03)
    s.metadata = {"Description": "Zamin Breakout: raketka, to'p fizikasi, g'ishtlar, bonuslar va 6 ta daraja", "Author": "Zamin",
                  "Tags": ["namuna", "2D", "arkada"]}
    s.entity("Main Camera", pos=(0, -0.6, 10), Camera=k.camera_ortho(25.0, (0.02, 0.02, 0.05, 1.0)))
    game = s.entity("Game")

    # ---- background, field, frame
    s.entity("Fon", pos=(0, 0, -2), scale=(48, 27, 1), SpriteRenderer=k.sprite("Textures/backdrop.png", order=-100))
    s.entity("Fon zarralari", pos=(0, 0, -1), ParticleSystem=k.particles(
        EmissionRate=3.0, MaxParticles=80, Shape=3, ShapeExtents=(24, 14, 0), LifetimeMin=8.0, LifetimeMax=14.0, SpeedMin=0.05,
        SpeedMax=0.25, Spread=180.0, StartColor=(0.8, 0.6, 1.0, 0.25), EndColor=(0.4, 0.7, 1.0, 0.0), StartSize=0.5, EndSize=0.2,
        Prewarm=True, SortingOrder=-90))
    field = s.entity("Maydon")
    s.entity("Maydon foni", parent=field, pos=(0, (FIELD_TOP + FIELD_BOTTOM) / 2, 0),
             SpriteRenderer=k.sprite("Textures/field.png", col=(1, 1, 1, 0.92), order=-60, lit=True, draw_mode="Tiled",
                                     size=(FIELD_HALF_W * 2, FIELD_TOP - FIELD_BOTTOM), ppu=48))
    t = 1.0
    for name, pos, size in (("Chap devor", (-FIELD_HALF_W - t / 2, -0.75), (t, 24.5)),
                            ("O'ng devor", (FIELD_HALF_W + t / 2, -0.75), (t, 24.5)),
                            ("Yuqori devor", (0, FIELD_TOP + t / 2), (FIELD_HALF_W * 2 + 2 * t, t))):
        s.entity(name, parent=field, pos=(pos[0], pos[1], 0),
                 SpriteRenderer=k.sprite(ATLAS, "frame", col=(1, 1, 1, 1), order=-5, draw_mode="Sliced", size=size, ppu=40),
                 Rigidbody2D=k.rigidbody2d("Static", layer=L["Wall"]), BoxCollider2D=k.box2d(size, restitution=1.0))
    # a soft glow line where balls are lost
    s.entity("Chegara", parent=field, pos=(0, FIELD_BOTTOM + 0.6, 0), scale=(FIELD_HALF_W * 2, 0.06, 1),
             SpriteRenderer=k.sprite(col=(1.4, 0.3, 0.6, 0.55), order=-50))
    bricks = s.entity("G'ishtlar", parent=field)  # the level's bricks are made here by the game

    # ---- paddle
    paddle = s.entity("Raketka", pos=(0, FIELD_BOTTOM + 2.0, 0), Light2D=k.light2d((0.4, 0.9, 1.0), 0.8, 5.5, 0.5, 1.8))
    s.entity("Visual", parent=paddle, scale=(3.2, 0.7, 1), SpriteRenderer=k.sprite(ATLAS, "paddle", col=(1.1, 1.15, 1.25, 1), order=20))
    for side, x in (("chap", -1.25), ("o'ng", 1.25)):
        s.entity(f"Lazer ({side})", parent=paddle, pos=(x, 0.38, 0), scale=(0.36, 0.45, 1), active=False,
                 SpriteRenderer=k.sprite(ATLAS, "turret", col=(1.2, 1.0, 1.0, 1), order=19))

    # ---- effects
    fx = s.entity("Effektlar")
    shards = s.entity("Parchalar", parent=fx, ParticleSystem=k.particles(
        EmissionRate=0.0, MaxParticles=1500, Shape=3, ShapeExtents=(0.7, 0.25, 0), LifetimeMin=0.5, LifetimeMax=1.1, SpeedMin=2.0,
        SpeedMax=7.0, Direction=(0, 1, 0), Spread=75.0, Gravity=(0, -18, 0), Drag=0.6, AngularVelocityMax=8.0,
        StartColor=(1, 1, 1, 1), EndColor=(1, 1, 1, 0), StartSize=0.24, EndSize=0.06, RenderAsCircle=False, SortingOrder=30))
    sparks = s.entity("Uchqunlar", parent=fx, ParticleSystem=k.particles(
        EmissionRate=0.0, MaxParticles=800, LifetimeMin=0.1, LifetimeMax=0.3, SpeedMin=3.0, SpeedMax=9.0, Spread=180.0, Drag=4.0,
        StartColor=(2, 2, 2, 1), EndColor=(1, 0.8, 0.5, 0), StartSize=0.14, EndSize=0.0, SortingOrder=31))

    # ---- templates
    tpl = s.entity("Shablonlar", active=False)
    t_brick = s.entity("G'isht", parent=tpl, Rigidbody2D=k.rigidbody2d("Static", layer=L["Brick"]),
                       BoxCollider2D=k.box2d((1.5, 0.6), restitution=1.0), NativeScript=k.script("Brick"))
    s.entity("Visual", parent=t_brick, scale=(1.5, 0.6, 1), SpriteRenderer=k.sprite(ATLAS, "brick", order=10))
    t_ball = s.entity("To'p", parent=tpl, Rigidbody2D=k.rigidbody2d("Dynamic", layer=L["Ball"], bullet=True),
                      CircleCollider2D=k.circle2d(0.28, density=1.0, friction=0.0, restitution=1.0),
                      Light2D=k.light2d((0.6, 0.85, 1.0), 1.1, 5.0, 0.4, 1.8), NativeScript=k.script("Ball", Speed=13.0, MaxSpeed=22.0))
    s.entity("Visual", parent=t_ball, scale=(0.75, 0.75, 1), SpriteRenderer=k.sprite(ATLAS, "ball", col=(1.3, 1.35, 1.45, 1), order=25))
    s.entity("Iz", parent=t_ball, ParticleSystem=k.particles(
        EmissionRate=55.0, MaxParticles=120, LifetimeMin=0.22, LifetimeMax=0.35, SpeedMin=0.0, SpeedMax=0.3, Spread=180.0,
        StartColor=(0.5, 1.1, 1.8, 0.8), EndColor=(0.3, 0.2, 1.0, 0.0), StartSize=0.42, EndSize=0.05, SortingOrder=24))
    powerups = []
    for kind, name, letter, col in ((0, "Keng", "K", (0.35, 0.8, 1.0)), (1, "Ko'p to'p", "x3", (0.45, 1.0, 0.5)),
                                    (2, "Sekin", "S", (1.0, 0.85, 0.3)), (3, "Olov", "O", (1.0, 0.45, 0.2)),
                                    (4, "Lazer", "L", (1.0, 0.3, 0.45)), (5, "Jon", "+1", (1.0, 0.45, 0.85))):
        p = s.entity(f"Bonus: {name}", parent=tpl, Light2D=k.light2d(col, 0.7, 2.5),
                     NativeScript=k.script("PowerUp", Kind=("Enum", kind), FallSpeed=4.5))
        s.entity("Visual", parent=p, scale=(1.5, 0.64, 1), SpriteRenderer=k.sprite(ATLAS, "capsule", col=(*[c * 1.35 for c in col], 1), order=22))
        s.entity("Harf", parent=p, pos=(0, -0.02, 0), scale=(0.62, 0.62, 1), Text=k.world_text(letter, (0.06, 0.05, 0.12, 1.0), order=1023))
        powerups.append(p)
    t_beam = s.entity("Lazer nuri", parent=tpl, SpriteRenderer=k.sprite(ATLAS, "beam", col=(2.6, 0.6, 0.8, 1), order=21),
                      NativeScript=k.script("FxAnimator", Lifetime=0.18, StartScale=1.0, EndScale=1.0, FadeOut=0.18, KeepScale=True))
    t_ring = s.entity("Halqa", parent=tpl, SpriteRenderer=k.sprite(ATLAS, "ring", col=(2, 2, 2, 1), order=29),
                      NativeScript=k.script("FxAnimator", Lifetime=0.4, StartScale=0.4, EndScale=3.2, FadeOut=0.4))
    t_popup = s.entity("Ochko", parent=tpl, scale=(0.6, 0.6, 1), Text=k.world_text("+10", (1, 1, 0.7, 1), order=1100),
                       NativeScript=k.script("FxAnimator", Lifetime=0.8, StartScale=0.6, EndScale=1.0, Rise=1.4, FadeOut=0.3, Pop=True))

    # ---- UI
    canvas = s.entity("UI", UITransform=k.ui_stretch(), UICanvas=k.ui_canvas())
    ui = k.UIKit(s, canvas, ATLAS, game, (0.45, 0.22, 0.75, 1.0), (0.65, 0.38, 1.0, 1.0), (0.3, 0.14, 0.5, 1.0))
    hud = s.entity("HUD", parent=canvas, UITransform=k.ui_stretch())
    label_col = (0.75, 0.6, 1.0, 0.9)
    ui.text("Hisob yorlig'i", hud, "HISOB", (0, 1), (0, 1), (60, -70), (420, 32), 28, label_col, key="hud.score_label")
    score = ui.text("Hisob", hud, "0", (0, 1), (0, 1), (58, -104), (460, 80), 70)
    ui.text("Rekord yorlig'i", hud, "REKORD", (0, 1), (0, 1), (60, -210), (420, 30), 26, label_col, key="hud.best_label")
    best = ui.text("Rekord", hud, "0", (0, 1), (0, 1), (60, -242), (420, 50), 42, (1.0, 0.85, 0.4, 1.0))
    ui.text("Jonlar yorlig'i", hud, "JONLAR", (0, 1), (0, 1), (60, -330), (420, 30), 26, label_col, key="hud.lives_label")
    lives = s.entity("Jonlar", parent=hud, UITransform=k.ui_rect((0, 1), None, (0, 1), (60, -368), (420, 48)),
                     UILayoutGroup=k.ui_layout("Horizontal", spacing=8, align=3, control_width=False, expand_width=False))
    for i in range(6):
        s.entity(f"Jon {i + 1}", parent=lives, UITransform=k.ui_rect(size=(40, 40)),
                 UIImage=k.ui_image(texture=ATLAS, frame="ball_icon", preserve_aspect=True, raycast=False))
    combo = ui.text("Kombo", hud, "", (0, 1), (0, 1), (60, -450), (440, 46), 38, (1.0, 0.75, 0.35, 1.0))
    ui.text("Daraja yorlig'i", hud, "DARAJA", (1, 1), (1, 1), (-60, -70), (420, 32), 28, label_col, h="Right", key="hud.level_label")
    level = ui.text("Daraja", hud, "1 / 6", (1, 1), (1, 1), (-58, -104), (420, 80), 70, h="Right")
    level_name = ui.text("Daraja nomi", hud, "", (1, 1), (1, 1), (-60, -186), (440, 40), 32, (0.8, 0.9, 1.0, 0.9), h="Right")
    bricks_left = ui.text("G'ishtlar", hud, "", (1, 1), (1, 1), (-60, -232), (440, 34), 26, (0.75, 0.75, 0.9, 0.8), h="Right")
    ui.text("Bonuslar yorlig'i", hud, "BONUSLAR", (1, 1), (1, 1), (-60, -330), (420, 30), 26, label_col, h="Right", key="hud.powerups_label")
    power = ui.text("Bonuslar", hud, "", (1, 1), (1, 1), (-60, -368), (460, 200), 30, (0.7, 1.0, 0.8, 1.0), h="Right")
    s.get(power)["Text"]["AlignV"] = "Top"
    hint = ui.text("Maslahat", hud, "", (0.5, 0), (0.5, 0), (0, 40), (1000, 40), 30, (0.85, 0.85, 1.0, 0.9), h="Center")
    banner = ui.text("E'lon", hud, "", (0.5, 0.5), (0.5, 0.5), (0, 80), (1200, 120), 100, h="Center", UIAnimation=k.ui_anim("Pop", "Fade", 0.35))
    banner_sub = ui.text("E'lon izohi", banner, "", (0.5, 0.0), (0.5, 1.0), (0, -6), (1100, 50), 38, (0.85, 0.8, 1.0, 1.0), h="Center")
    demo_badge = ui.text("Demo belgisi", hud, "DEMO - avtomatik o'yin", (0.5, 1), (0.5, 1), (0, -14), (900, 34), 26, (1.0, 0.8, 0.35, 0.9),
                         h="Center", key="hud.demo", active=False)

    # main menu
    main_menu = ui.dim("Bosh menyu", True, 0.55, (0.03, 0.0, 0.06))
    title = s.entity("Sarlavha", parent=main_menu, UITransform=k.ui_rect(pos=(0, 270), size=(1700, 180), font=160),
                     UIAnimation=k.ui_anim("SlideDown", "None", 0.6, slide=120))
    s.entity("Sarlavha soyasi", parent=title, UITransform=k.ui_rect((0, 0), (1, 1), pos=(6, -7), size=(0, 0), font=160),
             Text=k.ui_text("BREAKOUT", (0.4, 0.1, 0.8, 0.85), "Center", key="menu.title"))
    s.entity("Sarlavha matni", parent=title, UITransform=k.ui_rect((0, 0), (1, 1), size=(0, 0), font=160),
             Text=k.ui_text("BREAKOUT", (1.0, 0.85, 1.0, 1.0), "Center", key="menu.title"))
    s.entity("Taglavha", parent=main_menu, UITransform=k.ui_rect(pos=(0, 158), size=(1400, 50), font=38),
             Text=k.ui_text("G'ishtlarni sindiring  ·  Zamin Engine namunasi", (0.85, 0.8, 1.0, 0.95), "Center", key="menu.subtitle"))
    menu_panel = ui.panel("Menyu paneli", main_menu, (0, -110), (560, 330))
    play_button = ui.button("O'ynash", menu_panel, "O'ynash", "menu.play", "Play", True)
    language_button = ui.button("Til", menu_panel, "Til: O'zbekcha", None, "Language")
    ui.button("Chiqish", menu_panel, "Chiqish", "menu.quit", "Quit")
    menu_best = s.entity("Bosh menyu rekordi", parent=main_menu, UITransform=k.ui_rect(pos=(0, -330), size=(1200, 44), font=32),
                         Text=k.ui_text("", (1.0, 0.85, 0.4, 1.0), "Center"))
    s.entity("Boshqaruv", parent=main_menu, UITransform=k.ui_rect((0.5, 0), None, (0.5, 0), (0, 56), (1800, 70), font=25),
             Text=k.ui_text("Sichqoncha / A,D / strelkalar - raketka   ·   Space / LKM - to'pni otish, lazer   ·   Esc - pauza\n"
                            "Gamepad: chap stik - raketka, A - otish, Start - pauza", (0.8, 0.75, 0.95, 0.85), "Center", key="menu.controls"))
    # pause
    pause_menu = ui.dim("Pauza menyusi", False)
    pause_panel = ui.panel("Pauza paneli", pause_menu, (0, 0), (560, 420))
    ui.label("Pauza sarlavhasi", pause_panel, "PAUZA", (460, 80), 60, (0.9, 0.8, 1.0, 1.0), "pause.title")
    resume_button = ui.button("Davom etish", pause_panel, "Davom etish", "pause.resume", "Resume", True)
    ui.button("Qaytadan", pause_panel, "Qaytadan", "pause.restart", "Restart")
    ui.button("Bosh menyuga", pause_panel, "Bosh menyu", "pause.menu", "Menu")
    # game over / victory
    over_menu = ui.dim("O'yin tugadi", False)
    over_panel = ui.panel("Natija paneli", over_menu, (0, 0), (660, 560))
    over_title = ui.label("Natija sarlavhasi", over_panel, "O'YIN TUGADI", (580, 80), 64, (1.0, 0.45, 0.55, 1.0))
    over_score = ui.label("Natija hisobi", over_panel, "0", (580, 90), 84)
    over_level = ui.label("Natija darajasi", over_panel, "", (580, 40), 30, (0.8, 0.85, 1.0, 1.0))
    over_record = ui.label("Natija rekordi", over_panel, "", (580, 44), 34, (1.0, 0.85, 0.4, 1.0))
    retry_button = ui.button("Qayta o'ynash", over_panel, "Qayta o'ynash", "over.retry", "Restart", True)
    ui.button("Natija: bosh menyu", over_panel, "Bosh menyu", "pause.menu", "Menu")

    s.add(game, "NativeScript", k.script(
        "BreakoutGame",
        Paddle=k.ref(paddle), Bricks=k.ref(bricks),
        BrickTemplate=k.ref(t_brick), BallTemplate=k.ref(t_ball),
        WidePowerUp=k.ref(powerups[0]), MultiPowerUp=k.ref(powerups[1]), SlowPowerUp=k.ref(powerups[2]), FirePowerUp=k.ref(powerups[3]),
        LaserPowerUp=k.ref(powerups[4]), LifePowerUp=k.ref(powerups[5]),
        BeamTemplate=k.ref(t_beam), RingTemplate=k.ref(t_ring), PopupTemplate=k.ref(t_popup), Shards=k.ref(shards), Sparks=k.ref(sparks),
        Hud=k.ref(hud), ScoreText=k.ref(score), BestText=k.ref(best), Lives=k.ref(lives), ComboText=k.ref(combo), LevelText=k.ref(level),
        LevelNameText=k.ref(level_name), BricksText=k.ref(bricks_left), PowerText=k.ref(power), HintText=k.ref(hint), Banner=k.ref(banner),
        BannerSub=k.ref(banner_sub), DemoBadge=k.ref(demo_badge),
        MainMenu=k.ref(main_menu), PauseMenu=k.ref(pause_menu), GameOverMenu=k.ref(over_menu), MenuBest=k.ref(menu_best),
        PlayButton=k.ref(play_button), LanguageButton=k.ref(language_button), ResumeButton=k.ref(resume_button),
        RetryButton=k.ref(retry_button), OverTitle=k.ref(over_title), OverScore=k.ref(over_score), OverLevel=k.ref(over_level),
        OverRecord=k.ref(over_record),
        FieldHalfWidth=FIELD_HALF_W, FieldTop=FIELD_TOP, FieldBottom=FIELD_BOTTOM))
    s.add(game, "AudioSource", k.audio_source("Audio/music.wav", 0.6, loop=True, play=True, output="Music", stream=True))
    s.add(paddle, "NativeScript", k.script("PaddleController", Speed=26.0, Width=3.2))
    return s


def write_localization():
    uz = {
        "$name": "O'zbekcha",
        "menu": {"title": "BREAKOUT", "subtitle": "G'ishtlarni sindiring  ·  Zamin Engine namunasi", "play": "O'ynash", "quit": "Chiqish",
                 "language": "Til: {0}", "best": "Rekord: {0}   ·   eng uzoq daraja: {1}",
                 "controls": "Sichqoncha / A,D / strelkalar - raketka   ·   Space / LKM - to'pni otish, lazer   ·   Esc - pauza\n"
                             "Gamepad: chap stik - raketka, A - otish, Start - pauza"},
        "hud": {"score_label": "HISOB", "best_label": "REKORD", "lives_label": "JONLAR", "level_label": "DARAJA",
                "powerups_label": "BONUSLAR", "demo": "DEMO - avtomatik o'yin", "level": "{0} / {1}", "bricks": "G'ishtlar: {0}",
                "combo": "KOMBO x{0}", "serve": "Space / sichqoncha - to'pni otish", "level_banner": "{0}-DARAJA",
                "level_clear": "DARAJA TUGADI!", "level_bonus": "+{0} bonus", "ball_lost": "TO'P YO'QOLDI", "lives_left": "Jonlar: {0}",
                "wide": "KENG  {0}", "slow": "SEKIN  {0}", "fire": "OLOV  {0}", "laser": "LAZER  {0}", "loop": "{0}-aylana: tezroq!"},
        "level": {"1": "Kamalak", "2": "Piramida", "3": "Qal'a", "4": "Shaxmat", "5": "Yurak", "6": "Labirint"},
        "pause": {"title": "PAUZA", "resume": "Davom etish", "restart": "Qaytadan", "menu": "Bosh menyu"},
        "over": {"title": "O'YIN TUGADI", "victory": "G'ALABA!", "retry": "Qayta o'ynash", "level": "{0}-daraja  ·  {1} ta g'isht",
                 "record": "YANGI REKORD!", "best": "Rekord: {0}"},
        "powerup": {"wide": "KENG RAKETKA", "multi": "UCHTA TO'P!", "slow": "SEKIN TO'P", "fire": "OLOV TO'PI!", "laser": "LAZER!",
                    "life": "+1 JON"},
    }
    en = {
        "$name": "English",
        "menu": {"title": "BREAKOUT", "subtitle": "Smash the bricks  ·  a Zamin Engine sample", "play": "Play", "quit": "Quit",
                 "language": "Language: {0}", "best": "Best: {0}   ·   furthest level: {1}",
                 "controls": "Mouse / A,D / arrows - paddle   ·   Space / LMB - launch, laser   ·   Esc - pause\n"
                             "Gamepad: left stick - paddle, A - launch, Start - pause"},
        "hud": {"score_label": "SCORE", "best_label": "BEST", "lives_label": "LIVES", "level_label": "LEVEL", "powerups_label": "POWER-UPS",
                "demo": "DEMO - autoplay", "level": "{0} / {1}", "bricks": "Bricks: {0}", "combo": "COMBO x{0}",
                "serve": "Space / mouse - launch the ball", "level_banner": "LEVEL {0}", "level_clear": "LEVEL CLEAR!",
                "level_bonus": "+{0} bonus", "ball_lost": "BALL LOST", "lives_left": "Lives: {0}", "wide": "WIDE  {0}", "slow": "SLOW  {0}",
                "fire": "FIRE  {0}", "laser": "LASER  {0}", "loop": "Loop {0}: faster!"},
        "level": {"1": "Rainbow", "2": "Pyramid", "3": "Castle", "4": "Chessboard", "5": "Heart", "6": "Labyrinth"},
        "pause": {"title": "PAUSED", "resume": "Resume", "restart": "Restart", "menu": "Main menu"},
        "over": {"title": "GAME OVER", "victory": "VICTORY!", "retry": "Play again", "level": "Level {0}  ·  {1} bricks",
                 "record": "NEW RECORD!", "best": "Best: {0}"},
        "powerup": {"wide": "WIDE PADDLE", "multi": "THREE BALLS!", "slow": "SLOW BALL", "fire": "FIREBALL!", "laser": "LASER!",
                    "life": "+1 LIFE"},
    }
    k.write_json(os.path.join(ASSETS, "Localization/uz.json"), uz)
    k.write_json(os.path.join(ASSETS, "Localization/en.json"), en)


def main():
    make_atlas()
    make_backgrounds()
    make_icon()
    make_sounds()
    write_localization()
    scene = build_scene()
    uuid = scene.save(ASSETS, "Scenes/Breakout.zscene", NAME)
    actions = [
        ("Action", ["Key:Space", "Mouse:Left", "Key:Up", "Key:W", "Gamepad:A", "GamepadAxis:RightTrigger+"]),
        ("Pause", ["Key:Escape", "Key:P", "Gamepad:Start"]),
    ]
    axes = [k.axis("MoveX", ["Key:A", "Key:Left", "Gamepad:DPadLeft"], ["Key:D", "Key:Right", "Gamepad:DPadRight"], "LeftX", deadzone=0.15)]
    k.write_project(PROJECT, NAME, "Zamin Breakout", "Scenes/Breakout.zscene", uuid, "1.0.0", [("Scenes/Breakout.zscene", uuid)], actions,
                    axes, [], [], {"names": LAYERS, "matrix": k.collision_matrix(LAYERS, PAIRS)})
    k.write_module_cmake(PROJECT, NAME, "Zamin namuna o'yini: Breakout - C++ skriptlar moduli (libBreakout.so)")
    print("Breakout: tayyor")


if __name__ == "__main__":
    main()
