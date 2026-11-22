"""Shared content kit of the vertical slices (Tools/samples/gen_lantern_keeper.py, later gen_beacon_pass.py).

Deterministic (fixed seeds, no clock): painted top-down characters with 4-direction walk cycles, idle, attack and
death frames; autotiled ground/wall tilesets; props and icons; UI frames; music and sound recipes on samplekit's
oscillators; A* route compilation on a level grid (bot routes never cross walls).
"""
import heapq
import math

import numpy as np

import samplekit as k

FRAME = 64

# ------------------------------------------------------------------------------------------------ painting helpers


def _shade(img, sd, top, bottom, y, y0, y1):
    """A vertical gradient fill (top colour at y0, bottom at y1) inside the shape."""
    t = np.clip((y - y0) / max(1e-3, y1 - y0), 0, 1)
    col = k.mix_color(top, bottom, t)
    cov = k.smooth(0.5, -0.5, sd)
    a0 = img[:, :, 3]
    out = cov + a0 * (1 - cov)
    safe = np.where(out > 1e-6, out, 1)
    for i in range(3):
        img[:, :, i] = (col[:, :, i] * cov + img[:, :, i] * a0 * (1 - cov)) / safe
    img[:, :, 3] = out


def _capsule(x, y, ax, ay, bx, by, r):
    px, py = x - ax, y - ay
    ex, ey = bx - ax, by - ay
    t = np.clip((px * ex + py * ey) / max(1e-6, ex * ex + ey * ey), 0, 1)
    return np.sqrt((px - ex * t) ** 2 + (py - ey * t) ** 2) - r


def outline(img, color=(0.06, 0.04, 0.08), width=2):
    """A dark outline behind the painted shape (alpha dilated by `width` px)."""
    a = img[:, :, 3]
    grown = a.copy()
    for dx in range(-width, width + 1):
        for dy in range(-width, width + 1):
            if dx * dx + dy * dy <= width * width + 1:
                grown = np.maximum(grown, np.roll(np.roll(a, dy, axis=0), dx, axis=1))
    out = k.image(img.shape[1], img.shape[0])
    out[:, :, :3] = color
    out[:, :, 3] = np.clip(grown, 0, 1) * 0.95
    # the shape over its outline
    a0 = out[:, :, 3]
    total = a + a0 * (1 - a)
    safe = np.where(total > 1e-6, total, 1)
    for i in range(3):
        out[:, :, i] = (img[:, :, i] * a + out[:, :, i] * a0 * (1 - a)) / safe
    out[:, :, 3] = total
    return out


def rim(img, color=(1.0, 0.85, 0.6), strength=0.35, dx=-1, dy=-1):
    """Light on the edges facing the key light (top-left): where the alpha drops in that direction."""
    a = img[:, :, 3]
    edge = np.clip(a - np.roll(np.roll(a, -dy * 2, axis=0), -dx * 2, axis=1), 0, 1) * strength
    for i in range(3):
        img[:, :, i] = img[:, :, i] * (1 - edge) + color[i] * edge
    return img


# ------------------------------------------------------------------------------------------------ characters

STYLES = {
    # body (robe top, bottom), trim, skin, hair/hood, accent, scale, accessory
    "hero": dict(body=((0.20, 0.42, 0.62), (0.10, 0.22, 0.38)), trim=(0.95, 0.72, 0.30), skin=(0.94, 0.76, 0.60),
                 hood=(0.16, 0.32, 0.50), accent=(1.0, 0.82, 0.35), scale=1.0, item="lantern"),
    "bandit": dict(body=((0.55, 0.36, 0.22), (0.32, 0.20, 0.12)), trim=(0.80, 0.16, 0.14), skin=(0.82, 0.62, 0.46),
                   hood=(0.78, 0.14, 0.12), accent=(0.85, 0.85, 0.88), scale=1.0, item="sword"),
    "archer": dict(body=((0.30, 0.42, 0.24), (0.17, 0.25, 0.13)), trim=(0.62, 0.45, 0.24), skin=(0.82, 0.62, 0.46),
                   hood=(0.22, 0.30, 0.17), accent=(0.70, 0.52, 0.30), scale=1.0, item="bow"),
    "chief": dict(body=((0.48, 0.12, 0.12), (0.26, 0.06, 0.07)), trim=(0.95, 0.78, 0.30), skin=(0.76, 0.56, 0.40),
                  hood=(0.12, 0.10, 0.10), accent=(0.92, 0.92, 0.95), scale=1.18, item="sword"),
    "skeleton": dict(body=((0.78, 0.76, 0.68), (0.52, 0.50, 0.44)), trim=(0.30, 0.28, 0.34), skin=(0.90, 0.88, 0.80),
                     hood=(0.86, 0.84, 0.76), accent=(0.60, 0.62, 0.70), scale=1.0, item="sword", bones=True),
    "wisp": dict(body=((0.40, 0.95, 0.85), (0.10, 0.45, 0.55)), trim=(0.70, 1.0, 0.95), skin=(0.80, 1.0, 0.95),
                 hood=(0.30, 0.85, 0.80), accent=(0.8, 1.0, 0.95), scale=0.8, item="none", spirit=True),
    "boss": dict(body=((0.30, 0.16, 0.42), (0.10, 0.04, 0.16)), trim=(0.85, 0.55, 1.0), skin=(0.50, 0.40, 0.58),
                 hood=(0.16, 0.08, 0.24), accent=(0.75, 0.45, 1.0), scale=1.2, item="staff", crown=True),
}


def paint_character(style, direction, phase, pose="walk", t=0.0):
    """One 64x64 frame. direction: down/up/left/right; phase 0..1 of the walk cycle; pose walk/idle/attack/death
    (t 0..1 of attack/death)."""
    s = STYLES[style]
    if direction == "right":
        return paint_character(style, "left", phase, pose, t)[:, ::-1].copy()
    img = k.image(FRAME, FRAME)
    sc = s["scale"]
    cx, base = 32.0, 54.0
    x, y = k.coords(FRAME, FRAME, (0, 0))
    swing = math.sin(phase * 2 * math.pi)
    bob = abs(math.cos(phase * 2 * math.pi)) * 1.5 if pose == "walk" else (math.sin(phase * 2 * math.pi) * 0.6 if pose == "idle" else 0)
    squash = 1.0
    tilt = 0.0
    alpha = 1.0
    if pose == "death":
        squash = 1.0 - 0.55 * t
        tilt = 0.9 * t
        alpha = 1.0 - 0.35 * t
    # shadow
    k.fill(img, k.sd_circle((x - cx) / (13 * sc), (y - base) / (4.5 * sc), 1.0) * 4, (0, 0, 0, 0.35))
    if s.get("spirit"):
        # a glowing flame-like spirit
        cy = base - 20 * sc - bob * 2
        core = k.sd_circle(x - cx, (y - cy) * 0.85, 10 * sc)
        k.glow(img, core, (*s["body"][0], 1), 7, 0.9)
        tail = k.sd_polygon(x, y, [(cx - 8 * sc, cy), (cx + 8 * sc, cy), (cx + swing * 4, cy + 18 * sc)])
        _shade(img, np.minimum(core, tail), s["body"][0], s["body"][1], y, cy - 10, cy + 18)
        k.fill(img, k.sd_circle(x - cx, y - cy + 1, 5 * sc), (*s["trim"], 0.9))
        if direction != "up":
            for ex in (-3.5, 3.5):
                k.fill(img, k.sd_circle(x - cx - ex * (0.4 if direction == "left" else 1), y - cy + 1, 1.6), (0.05, 0.12, 0.16, 1))
        img[:, :, 3] *= alpha
        return rim(outline(img, (0.03, 0.10, 0.12), 1), (0.9, 1, 1), 0.3)
    h = lambda v: v * sc * squash
    body_top, body_bot = base - h(26) - bob, base - h(8) - bob
    width = 11 * sc
    side = direction == "left"
    # legs
    leg = 3.2 * sc
    if pose != "death":
        if side:
            for k_, sgn in ((0.0, 1), (0.6, -1)):
                fx = cx + sgn * swing * 5 * sc
                d = _capsule(x, y, cx + sgn * 0.5, body_bot - 2, fx, base - 1, leg)
                _shade(img, d, (0.22, 0.17, 0.14), (0.12, 0.09, 0.08), y, body_bot, base)
        else:
            for sgn in (-1, 1):
                lift = swing * sgn * 3 * sc
                d = _capsule(x, y, cx + sgn * 4 * sc, body_bot - 2, cx + sgn * 4.5 * sc, base - 1 - max(0, lift), leg)
                _shade(img, d, (0.22, 0.17, 0.14), (0.12, 0.09, 0.08), y, body_bot, base)
    # back arm (side view) / arms
    def arm(sgn, front):
        reach = -swing * sgn * 5 * sc if pose == "walk" else 0
        if pose == "attack" and front:
            ang = -1.2 + 2.6 * t
            ex, ey = cx + math.sin(ang) * 14 * sc, body_top + 8 - math.cos(ang) * 10 * sc
        elif side:
            ex, ey = cx + reach, body_top + h(15)
        else:
            ex, ey = cx + sgn * (width + 2), body_top + h(14) + reach * 0.4
        sx, sy = (cx, body_top + 5) if side else (cx + sgn * (width - 2), body_top + 5)
        d = _capsule(x, y, sx, sy, ex, ey, 2.8 * sc)
        _shade(img, d, s["body"][0], s["body"][1], y, sy, ey + 4)
        k.fill(img, k.sd_circle(x - ex, y - ey, 2.6 * sc), (*s["skin"], 1))
        return ex, ey
    if side:
        arm(1, False)
    # robe / body: a trapezoid, wider at the hem
    hem = width * 1.25
    poly = [(cx - width * (0.7 if side else 0.9), body_top), (cx + width * (0.7 if side else 0.9), body_top),
            (cx + hem * (0.8 if side else 1), body_bot + 2), (cx - hem * (0.8 if side else 1), body_bot + 2)]
    body = k.sd_polygon(x, y, poly) - 2.5
    _shade(img, body, s["body"][0], s["body"][1], y, body_top, body_bot)
    if s.get("bones"):
        for r_ in range(3):
            k.fill(img, k.sd_round_box(x - cx, y - (body_top + 6 + r_ * 4.5), width * 0.7, 0.9, 0.9), (0.45, 0.43, 0.38, 0.9))
    # belt / trim
    k.fill(img, k.sd_round_box(x - cx, y - (body_top + h(12)), width * (0.75 if side else 0.95), 1.6 * sc, 1.0), (*s["trim"], 1))
    k.fill(img, k.sd_round_box(x - cx, y - (body_bot + 1), hem * (0.8 if side else 1) - 1, 1.3, 1.0), (*s["trim"], 0.8))
    # head
    hr = 9.0 * sc
    hx, hy = cx + (-1.5 if side else 0), body_top - hr + 3
    head = k.sd_circle(x - hx, y - hy, hr)
    _shade(img, head, s["skin"], tuple(c * 0.78 for c in s["skin"]), y, hy - hr, hy + hr)
    # hood / hair / helmet
    if direction == "up":
        k.fill(img, k.sd_circle(x - hx, y - hy + 0.5, hr + 0.6), (*s["hood"], 1))
    else:
        cap = np.maximum(k.sd_circle(x - hx, y - hy - 0.5, hr + 1.2), (y - (hy - (1 if side else 2))) * 1.0)
        if side:
            cap = np.minimum(cap, np.maximum(k.sd_circle(x - hx - 3, y - hy, hr + 0.8), -(x - hx - 1)))
        k.fill(img, cap, (*s["hood"], 1))
        # face
        if side:
            k.fill(img, k.sd_circle(x - (hx - 5 * sc), y - (hy + 1.5), 1.3 * sc), (0.08, 0.05, 0.06, 1))
        else:
            for ex in (-3.2, 3.2):
                k.fill(img, k.sd_circle(x - (hx + ex * sc), y - (hy + 1.5), 1.35 * sc), (0.08, 0.05, 0.06, 1))
            if s.get("bones"):
                k.fill(img, k.sd_round_box(x - hx, y - (hy + 5.5), 3 * sc, 0.8, 0.5), (0.25, 0.22, 0.2, 1))
    if s.get("crown"):
        spikes = [(hx - 9 * sc, hy - hr + 2), (hx - 6 * sc, hy - hr - 9), (hx - 2 * sc, hy - hr + 1), (hx, hy - hr - 12),
                  (hx + 2 * sc, hy - hr + 1), (hx + 6 * sc, hy - hr - 9), (hx + 9 * sc, hy - hr + 2)]
        crown = k.sd_polygon(x, y, spikes)
        k.glow(img, crown, (*s["accent"], 1), 4, 0.6)
        _shade(img, crown, s["accent"], s["trim"], y, hy - hr - 12, hy - hr + 2)
    # front arm and the held item
    hand = arm(-1, True) if side else None
    if not side:
        arm(-1, False)
        hand = arm(1, True)
    item = s["item"]
    if item != "none" and direction != "up" and pose != "death":
        ex, ey = hand
        if item == "lantern":
            k.fill(img, _capsule(x, y, ex, ey, ex, ey + 4, 0.8), (0.3, 0.22, 0.12, 1))
            lamp = k.sd_round_box(x - ex, y - (ey + 7), 3.2, 4.0, 1.4)
            k.glow(img, lamp, (1.0, 0.78, 0.3, 1), 6, 0.9)
            k.fill(img, lamp, (1.0, 0.86, 0.45, 1))
            k.fill(img, k.sd_round_box(x - ex, y - (ey + 7), 3.6, 4.4, 1.4) + 0, (0.45, 0.30, 0.12, 0.0))
        elif item == "sword":
            tip = (ex + (-10 if side else 3), ey - (4 if side else 14))
            k.fill(img, _capsule(x, y, ex, ey, *tip, 1.1), (*s["accent"], 1))
            k.fill(img, _capsule(x, y, ex - 2.5, ey, ex + 2.5, ey, 1.0), (0.4, 0.3, 0.15, 1))
        elif item == "bow":
            bx = ex - (3 if side else 0)
            bow = np.maximum(np.abs(k.sd_circle(x - bx, y - ey, 9 * sc)) - 1.1, -(x - bx) if not side else (x - bx))
            k.fill(img, bow, (*s["accent"], 1))
            k.fill(img, np.abs(x - bx) - 0.4 + np.maximum(np.abs(y - ey) - 9 * sc, 0) * 9, (0.9, 0.9, 0.85, 0.7))
        elif item == "staff":
            k.fill(img, _capsule(x, y, ex, ey + 8, ex, ey - 20 * sc, 1.3), (0.25, 0.15, 0.30, 1))
            orb = k.sd_circle(x - ex, y - (ey - 22 * sc), 3.5)
            k.glow(img, orb, (*s["accent"], 1), 6, 0.9)
            k.fill(img, orb, (0.95, 0.80, 1.0, 1))
    if tilt:
        # fall over: rotate the painted figure about its feet
        img = rotate(img, tilt, (cx, base))
    img[:, :, 3] *= alpha
    return rim(outline(img), (1.0, 0.86, 0.62), 0.3)


def rotate(img, angle, center):
    h, w = img.shape[:2]
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    c, s_ = math.cos(-angle), math.sin(-angle)
    sx = (xs - center[0]) * c - (ys - center[1]) * s_ + center[0]
    sy = (xs - center[0]) * s_ + (ys - center[1]) * c + center[1]
    xi, yi = np.clip(np.round(sx).astype(int), 0, w - 1), np.clip(np.round(sy).astype(int), 0, h - 1)
    out = img[yi, xi].copy()
    out[(sx < 0) | (sx >= w) | (sy < 0) | (sy >= h)] = 0
    return out


WALK_FRAMES = 6


def character_atlas(style, assets, rel, project):
    """The character's sprite sheet (640 x 320): walk_<dir>_<i> (6 each), idle_<i> (2), attack_<i> (4), death_<i> (4)."""
    atlas = k.Atlas(640, 320)
    atlas.pad = 2
    names = []
    for d in ("down", "left", "right", "up"):
        for i in range(WALK_FRAMES):
            name = f"{style}_walk_{d}_{i}"
            atlas.add(name, paint_character(style, d, i / WALK_FRAMES), border=(0, 0, 0, 0))
            names.append(name)
    for i in range(2):
        atlas.add(f"{style}_idle_{i}", paint_character(style, "down", i / 2, "idle"), border=(0, 0, 0, 0))
    for i in range(4):
        atlas.add(f"{style}_attack_{i}", paint_character(style, "down", 0.25, "attack", i / 3), border=(0, 0, 0, 0))
    for i in range(4):
        atlas.add(f"{style}_death_{i}", paint_character(style, "down", 0.0, "death", (i + 1) / 4), border=(0, 0, 0, 0))
    atlas.save(assets, rel, project)
    return atlas


def clip_json(texture, frames, fps, loop=True, events=()):
    c = {"Version": 1, "Texture": texture, "FPS": float(fps), "Loop": loop, "Frames": list(frames)}
    if events:
        c["Events"] = [{"Frame": f, "Name": n} for f, n in events]
    return c


# ------------------------------------------------------------------------------------------------ tiles

TILE = 64


def tile_noise(seed, cells=4):
    return k.value_noise(TILE, TILE, cells, seed=seed, octaves=3)


def paint_ground(base, dark, seed, speckle=None, cells=4):
    n = tile_noise(seed, cells)
    img = k.image(TILE, TILE, (*base, 1))
    for i in range(3):
        img[:, :, i] = base[i] * (0.82 + 0.36 * n) * 0.6 + dark[i] * 0.4 * (1 - n)
    if speckle:
        rng = np.random.default_rng(seed + 100)
        x, y = k.coords(TILE, TILE, (0, 0))
        for _ in range(speckle[1]):
            px, py = rng.uniform(4, TILE - 4, 2)
            k.fill(img, k.sd_circle(x - px, y - py, rng.uniform(0.8, 1.8)), (*speckle[0], 0.55))
    img[:, :, 3] = 1
    return img


def paint_wall(top, side, seed, bricks=True):
    """A wall tile seen from above: a lit cap with a darker front face at the bottom (3/4 view) and mortar lines."""
    img = paint_ground(top, tuple(c * 0.6 for c in top), seed, cells=3)
    x, y = k.coords(TILE, TILE, (0, 0))
    face = y > TILE * 0.62
    n = tile_noise(seed + 7, 3)
    for i in range(3):
        img[:, :, i] = np.where(face, side[i] * (0.75 + 0.35 * n), img[:, :, i])
    if bricks:
        for row in range(0, TILE, 16):
            line = np.abs(y - row) < 1.0
            for i in range(3):
                img[:, :, i] = np.where(line, img[:, :, i] * 0.65, img[:, :, i])
            offset = 0 if (row // 16) % 2 else 16
            for col in range(offset, TILE + 1, 32):
                vline = (np.abs(x - col) < 1.0) & (y >= row) & (y < row + 16)
                for i in range(3):
                    img[:, :, i] = np.where(vline, img[:, :, i] * 0.7, img[:, :, i])
    # top edge highlight and face shadow
    hl = np.clip(1 - y / 3.0, 0, 1) * 0.25
    sh = np.clip((y - TILE * 0.62) / 3.0, 0, 1) * np.clip(1 - (y - TILE * 0.62) / 6.0, 0, 1) * 0.3
    for i in range(3):
        img[:, :, i] = img[:, :, i] * (1 - sh) + hl
    return img


def paint_water(seed):
    n = tile_noise(seed, 3)
    img = k.image(TILE, TILE, (0.12, 0.36, 0.48, 1))
    x, y = k.coords(TILE, TILE, (0, 0))
    ripple = 0.5 + 0.5 * np.sin((x * 0.25 + y * 0.12) + n * 6)
    for i, (a, b) in enumerate(zip((0.10, 0.30, 0.42), (0.30, 0.62, 0.70))):
        img[:, :, i] = a + (b - a) * ripple * 0.55 + n * 0.08
    return img


# ------------------------------------------------------------------------------------------------ props and icons


def paint_tree(seed, foliage=((0.94, 0.62, 0.30), (0.55, 0.32, 0.18)), size=128):
    """An apricot tree from above: trunk, a round sunlit canopy with fruit."""
    img = k.image(size, size)
    x, y = k.coords(size, size, (0, 0))
    c = size / 2
    k.fill(img, k.sd_circle((x - c) / 1.0, (y - (size * 0.86)) * 2.6, size * 0.28), (0, 0, 0, 0.35))
    k.fill(img, _capsule(x, y, c, size * 0.86, c, size * 0.55, size * 0.06), (0.36, 0.22, 0.12, 1))
    rng = np.random.default_rng(seed)
    blobs = None
    for _ in range(7):
        bx, by, br = c + rng.uniform(-0.2, 0.2) * size, size * (0.42 + rng.uniform(-0.12, 0.08)), size * rng.uniform(0.16, 0.22)
        d = k.sd_circle(x - bx, y - by, br)
        blobs = d if blobs is None else np.minimum(blobs, d)
    n = k.value_noise(size, size, 4, seed=seed, octaves=3)
    _shade(img, blobs - n * 3, (0.46, 0.62, 0.30), (0.20, 0.34, 0.16), y, size * 0.2, size * 0.66)
    for _ in range(9):
        fx, fy = c + rng.uniform(-0.25, 0.25) * size, size * rng.uniform(0.28, 0.6)
        k.fill(img, k.sd_circle(x - fx, y - fy, size * 0.025), (*foliage[0], 1))
    return rim(outline(img, (0.08, 0.06, 0.05), 2), (1.0, 0.8, 0.5), 0.4)


def paint_icon(kind, size=64):
    img = k.image(size, size)
    x, y = k.coords(size, size)
    if kind == "coin":
        d = k.sd_circle(x, y, size * 0.32)
        k.glow(img, d, (1, 0.8, 0.3, 1), 5, 0.6)
        _shade(img, d, (1.0, 0.86, 0.38), (0.78, 0.52, 0.12), y, -size * 0.3, size * 0.3)
        k.stroke(img, k.sd_circle(x, y, size * 0.22), (0.7, 0.45, 0.1, 0.9), 2)
    elif kind == "lamp":
        body = k.sd_round_box(x, y + 2, size * 0.18, size * 0.24, 5)
        k.glow(img, body, (1, 0.75, 0.3, 1), 9, 1.0)
        _shade(img, body, (1.0, 0.92, 0.6), (0.95, 0.55, 0.15), y, -size * 0.2, size * 0.25)
        k.fill(img, k.sd_round_box(x, y - size * 0.27, size * 0.12, size * 0.05, 2), (0.55, 0.36, 0.14, 1))
        k.stroke(img, k.sd_circle(x, y - size * 0.36, size * 0.07), (0.55, 0.36, 0.14, 1), 2)
    elif kind == "herb":
        for ang in (-0.7, 0.0, 0.7):
            leaf = k.sd_round_box((x * math.cos(ang) - y * math.sin(ang)), (x * math.sin(ang) + y * math.cos(ang)) + size * 0.12, size * 0.08, size * 0.2, size * 0.07)
            _shade(img, leaf, (0.55, 0.88, 0.40), (0.18, 0.50, 0.20), y, -size * 0.3, size * 0.3)
        k.fill(img, k.sd_circle(x, y - size * 0.2, size * 0.06), (0.95, 0.85, 1.0, 1))
    elif kind == "key":
        k.stroke(img, k.sd_circle(x + size * 0.14, y, size * 0.13), (1.0, 0.82, 0.35, 1), 4)
        k.fill(img, _capsule(x, y, -size * 0.32, 0, size * 0.02, 0, 2.5), (1.0, 0.82, 0.35, 1))
        k.fill(img, _capsule(x, y, -size * 0.28, 0, -size * 0.28, size * 0.12, 2.0), (1.0, 0.82, 0.35, 1))
    elif kind == "potion":
        flask = np.minimum(k.sd_circle(x, y + size * 0.08, size * 0.22), k.sd_round_box(x, y - size * 0.18, size * 0.07, size * 0.1, 2))
        k.glow(img, flask, (1, 0.3, 0.4, 1), 6, 0.6)
        _shade(img, flask, (1.0, 0.45, 0.55), (0.62, 0.08, 0.2), y, -size * 0.3, size * 0.3)
        k.fill(img, k.sd_round_box(x, y - size * 0.3, size * 0.09, size * 0.04, 2), (0.55, 0.36, 0.2, 1))
    elif kind == "bolt":
        d = k.sd_circle(x, y, size * 0.16)
        k.glow(img, d, (1, 0.55, 0.15, 1), 12, 1.0)
        k.fill(img, d, (1.0, 0.92, 0.65, 1))
    elif kind == "nova":
        k.glow(img, np.abs(k.sd_circle(x, y, size * 0.28)) - 2, (0.55, 0.8, 1.0, 1), 6, 1.0)
        k.stroke(img, k.sd_circle(x, y, size * 0.28), (0.85, 0.95, 1.0, 1), 4)
        k.fill(img, k.sd_circle(x, y, size * 0.08), (0.85, 0.95, 1.0, 1))
    elif kind == "mend":
        cross = np.minimum(k.sd_round_box(x, y, size * 0.08, size * 0.26, 3), k.sd_round_box(x, y, size * 0.26, size * 0.08, 3))
        k.glow(img, cross, (0.4, 1.0, 0.55, 1), 7, 0.8)
        _shade(img, cross, (0.7, 1.0, 0.7), (0.2, 0.75, 0.35), y, -size * 0.26, size * 0.26)
    elif kind == "sword":
        k.fill(img, _capsule(x, y, -size * 0.25, size * 0.25, size * 0.25, -size * 0.25, 3.5), (0.88, 0.9, 0.95, 1))
        k.fill(img, _capsule(x, y, -size * 0.28, size * 0.08, -size * 0.08, size * 0.28, 3.0), (0.85, 0.62, 0.2, 1))
    elif kind == "charm":
        gem = k.sd_polygon(x, y, k.regular_polygon(6, size * 0.26, math.pi / 6))
        k.glow(img, gem, (0.5, 0.75, 1.0, 1), 8, 0.8)
        _shade(img, gem, (0.6, 0.85, 1.0), (0.2, 0.35, 0.8), y, -size * 0.26, size * 0.26)
    elif kind == "armor":
        shell = k.sd_round_box(x, y, size * 0.24, size * 0.28, size * 0.12)
        _shade(img, shell, (0.75, 0.78, 0.85), (0.38, 0.42, 0.5), y, -size * 0.28, size * 0.28)
        k.fill(img, k.sd_round_box(x, y, size * 0.05, size * 0.24, 2), (0.9, 0.75, 0.35, 1))
    elif kind == "melee":
        k.fill(img, _capsule(x, y, -size * 0.22, size * 0.22, size * 0.22, -size * 0.22, 4), (0.95, 0.85, 0.65, 1))
        k.stroke(img, np.abs(k.sd_circle(x - size * 0.05, y + size * 0.05, size * 0.3)) - 1, (1, 1, 1, 0.5), 2)
    return outline(img, (0.05, 0.04, 0.07), 2) if kind not in ("bolt", "nova") else img


def glow_disc(size, color, inner=0.15):
    img = k.image(size, size)
    x, y = k.coords(size, size)
    r = np.sqrt(x * x + y * y) / (size / 2)
    a = np.clip(1 - r, 0, 1) ** 1.6
    img[:, :, :3] = color
    img[:, :, 3] = a
    core = np.clip(1 - r / inner, 0, 1)
    for i in range(3):
        img[:, :, i] = img[:, :, i] * (1 - core) + core
    return img


# ------------------------------------------------------------------------------------------------ UI frames


def ui_frames(atlas, accent=(0.95, 0.72, 0.30), deep=(0.07, 0.06, 0.10)):
    """9-slice panel (girih-like corner ornaments), button, slider track/knob, bar frame, badge."""
    s = 128
    img = k.image(s, s)
    x, y = k.coords(s, s)
    box = k.sd_round_box(x, y, 60, 60, 18)
    _shade(img, box, (0.14, 0.12, 0.20), deep, y, -60, 60)
    img[:, :, 3] *= 0.94
    k.stroke(img, box + 3, (*accent, 0.95), 2.5)
    k.stroke(img, box + 9, (*accent, 0.35), 1.2)
    for cx_, cy_ in ((-46, -46), (46, -46), (-46, 46), (46, 46)):
        star = k.sd_polygon(x, y, k.regular_polygon(8, 7, math.pi / 8, cx_, cy_))
        k.fill(img, star, (*accent, 0.9))
        k.fill(img, k.sd_circle(x - cx_, y - cy_, 2.2), (*deep, 1))
    atlas.add("panel", img, border=(40, 40, 40, 40))
    img = k.image(96, 48)
    x, y = k.coords(96, 48)
    box = k.sd_round_box(x, y, 46, 22, 12)
    _shade(img, box, (1, 1, 1), (0.76, 0.78, 0.84), y, -22, 22)
    k.stroke(img, box + 1.5, (1, 1, 1, 0.9), 2)
    atlas.add("button", img, border=(20, 20, 20, 20))
    img = k.image(64, 24)
    x, y = k.coords(64, 24)
    box = k.sd_round_box(x, y, 30, 8, 8)
    _shade(img, box, (0.25, 0.22, 0.32), (0.10, 0.09, 0.14), y, -8, 8)
    k.stroke(img, box + 1, (*accent, 0.7), 1.5)
    atlas.add("track", img, border=(12, 10, 12, 10))
    img = k.image(48, 48)
    x, y = k.coords(48, 48)
    knob = k.sd_circle(x, y, 16)
    k.glow(img, knob, (*accent, 1), 4, 0.6)
    _shade(img, knob, (1.0, 0.9, 0.62), accent, y, -16, 16)
    atlas.add("knob", img)
    img = k.image(96, 32)
    x, y = k.coords(96, 32)
    box = k.sd_round_box(x, y, 46, 14, 10)
    k.fill(img, box, (0.04, 0.035, 0.06, 0.85))
    k.stroke(img, box + 1, (*accent, 0.9), 2)
    atlas.add("frame", img, border=(14, 14, 14, 14))
    img = k.image(72, 72)
    x, y = k.coords(72, 72)
    badge = k.sd_polygon(x, y, k.regular_polygon(8, 32, math.pi / 8))
    _shade(img, badge, (0.30, 0.22, 0.45), (0.10, 0.07, 0.16), y, -32, 32)
    k.stroke(img, badge + 1.5, (*accent, 1), 2.5)
    atlas.add("badge", img)
    img = k.image(96, 96)
    x, y = k.coords(96, 96)
    slot = k.sd_round_box(x, y, 44, 44, 16)
    _shade(img, slot, (0.16, 0.14, 0.22), (0.06, 0.05, 0.09), y, -44, 44)
    img[:, :, 3] *= 0.92
    k.stroke(img, slot + 1.5, (*accent, 0.75), 2)
    atlas.add("slot", img, border=(28, 28, 28, 28))


# ------------------------------------------------------------------------------------------------ audio


def pluck(freq, seconds, bright=0.6, seed=0):
    """A dutor-like plucked string: a few harmonics with fast decay and a tiny noise attack."""
    n = int(seconds * k.RATE)
    t = np.arange(n) / k.RATE
    out = np.zeros(n)
    for h, amp in ((1, 1.0), (2, 0.5 * bright), (3, 0.3 * bright), (4, 0.15 * bright), (5, 0.08 * bright)):
        out += amp * np.sin(2 * np.pi * freq * h * t) * np.exp(-t * (3.5 + h * 1.6))
    out += k.noise(seconds, seed)[:n] * np.exp(-t * 60) * 0.25
    return out * np.clip(t / 0.003, 0, 1)


def drum(seconds=0.35, low=78.0, seed=1, slap=0.4):
    """A doira hit: a pitched membrane thump plus jingles."""
    n = int(seconds * k.RATE)
    t = np.arange(n) / k.RATE
    body = np.sin(2 * np.pi * (low * t + 20 * (1 - np.exp(-t * 30)) / 30)) * np.exp(-t * 14)
    jingle = k.noise(seconds, seed)[:n] * np.exp(-t * 18) * slap
    jingle = jingle - np.concatenate(([0], jingle[:-1]))  # bright
    return body + jingle * 0.6


def drone(freq, seconds, seed=0):
    t = np.arange(int(seconds * k.RATE)) / k.RATE
    out = np.sin(2 * np.pi * freq * t) * 0.6 + np.sin(2 * np.pi * freq * 1.5 * t) * 0.25 + np.sin(2 * np.pi * freq * 2 * t) * 0.15
    wobble = 0.85 + 0.15 * np.sin(2 * np.pi * 0.25 * t + seed)
    return out * wobble


SCALE = {"hijaz": [0, 1, 4, 5, 7, 8, 10, 12], "minor": [0, 2, 3, 5, 7, 8, 10, 12], "dorian": [0, 2, 3, 5, 7, 9, 10, 12]}


def music_loop(seconds, root, scale, tempo, seed, melody_density=0.6, drone_gain=0.25, drums=True, dark=False):
    """A seamless loop: plucked melody over a drone and a frame-drum pattern (16th grid, fixed seed)."""
    rng = np.random.default_rng(seed)
    n = int(seconds * k.RATE)
    out = np.zeros(n + k.RATE)
    beat = 60.0 / tempo
    step = beat / 2
    steps = int(seconds / step)
    degrees = SCALE[scale]
    note = 2
    for i in range(steps):
        at = i * step
        if drums:
            if i % 8 == 0:
                k.place(out, drum(0.4, 72 if not dark else 58, seed + i), at, 0.9)
            elif i % 8 == 3 or i % 8 == 6:
                k.place(out, drum(0.25, 140, seed + i, 0.8), at, 0.45)
            elif i % 2 == 1:
                k.place(out, drum(0.12, 300, seed + i, 1.0) * 0.5, at, 0.18)
        if rng.random() < melody_density:
            note = int(np.clip(note + rng.integers(-2, 3), 0, len(degrees) - 1))
            f = root * 2 ** ((degrees[note] + 12) / 12)
            k.place(out, pluck(f, 0.9, 0.5 if dark else 0.7, seed + i), at, 0.5)
    out[:n] += drone(root, seconds, seed) * drone_gain
    # wrap the tail into the start: the loop is seamless
    tail = out[n:]
    out = out[:n]
    out[:len(tail)] += tail
    return out


def sfx(kind, seed=0):
    r = k.RATE
    if kind == "swing":
        n = k.noise(0.22, seed)
        return k.highpass(n, 1200) * k.env(len(n), 0.02, 0.18)
    if kind == "hit":
        t = np.arange(int(0.18 * r)) / r
        return (np.sin(2 * np.pi * 140 * t * (1 - t * 2)) * 0.8 + k.noise(0.18, seed)[:len(t)] * 0.5) * np.exp(-t * 22)
    if kind == "bolt":
        t = np.arange(int(0.35 * r)) / r
        f = 900 * np.exp(-t * 6) + 200
        return k.osc(f, 0.35, "saw") * np.exp(-t * 7) * 0.5 + k.noise(0.35, seed)[:len(t)] * np.exp(-t * 9) * 0.3
    if kind == "coin":
        return k.mix(pluck(1568, 0.3, 0.3) * 0.6, np.pad(pluck(2093, 0.4, 0.3), (int(0.06 * r), 0)) * 0.6)
    if kind == "lamp":
        return k.mix(*[np.pad(pluck(k.note_freq(nm), 1.2, 0.4), (int(i * 0.09 * r), 0)) * 0.5 for i, nm in enumerate(["E5", "G#5", "B5", "E6"])])
    if kind == "chest":
        t = np.arange(int(0.5 * r)) / r
        creak = k.osc(180 + 60 * np.sin(t * 30), 0.5, "saw") * np.exp(-t * 4) * 0.3
        return k.mix(creak, np.pad(sfx("coin"), (int(0.2 * r), 0)))
    if kind == "death":
        t = np.arange(int(0.6 * r)) / r
        return k.osc(220 * np.exp(-t * 2.5), 0.6, "tri") * np.exp(-t * 4) * 0.7
    if kind == "hurt":
        t = np.arange(int(0.25 * r)) / r
        return k.osc(330 * np.exp(-t * 3), 0.25, "square") * np.exp(-t * 12) * 0.4
    if kind == "levelup":
        return k.mix(*[np.pad(pluck(k.note_freq(nm), 1.0, 0.6), (int(i * 0.11 * r), 0)) * 0.55 for i, nm in enumerate(["C5", "E5", "G5", "C6", "E6"])])
    if kind == "ui":
        return pluck(1320, 0.12, 0.2) * 0.6
    if kind == "victory":
        return k.mix(*[np.pad(pluck(k.note_freq(nm), 1.6, 0.7), (int(i * 0.16 * r), 0)) * 0.55 for i, nm in enumerate(["D4", "A4", "D5", "F#5", "A5", "D6"])],
                     np.pad(drum(0.6, 70, 3), (int(0.0 * r), 0)))
    if kind == "defeat":
        return k.mix(*[np.pad(pluck(k.note_freq(nm), 1.4, 0.3), (int(i * 0.25 * r), 0)) * 0.6 for i, nm in enumerate(["A3", "F3", "D3"])], drone(55, 1.6) * 0.15)
    if kind == "nova":
        t = np.arange(int(0.7 * r)) / r
        return k.osc(120 + 600 * t, 0.7, "sine")[:len(t)] * np.exp(-t * 4) * 0.6 + k.highpass(k.noise(0.7, seed), 2000)[:len(t)] * np.exp(-t * 6) * 0.3
    if kind == "gate":
        t = np.arange(int(0.9 * r)) / r
        return k.mix(k.osc(70 + 15 * np.sin(t * 9), 0.9, "saw") * np.exp(-t * 2) * 0.35, drum(0.5, 55, seed) * 0.6)
    raise ValueError(kind)


# ------------------------------------------------------------------------------------------------ routing


def astar(walkable, start, goal):
    """4-neighbour A* on a boolean grid [y][x]; returns the cell path (start..goal) or None."""
    h, w = len(walkable), len(walkable[0])
    open_ = [(0, start)]
    came = {start: None}
    cost = {start: 0}
    while open_:
        _, cur = heapq.heappop(open_)
        if cur == goal:
            path = []
            while cur is not None:
                path.append(cur)
                cur = came[cur]
            return path[::-1]
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = cur[0] + dx, cur[1] + dy
            if 0 <= nx < w and 0 <= ny < h and walkable[ny][nx]:
                c = cost[cur] + 1
                if (nx, ny) not in cost or c < cost[(nx, ny)]:
                    cost[(nx, ny)] = c
                    came[(nx, ny)] = cur
                    heapq.heappush(open_, (c + abs(nx - goal[0]) + abs(ny - goal[1]), (nx, ny)))
    return None


def clear_line(walkable, a, b, clearance=1):
    """True when the straight segment between cell centres a and b keeps `clearance` cells from walls."""
    ax, ay = a[0] + 0.5, a[1] + 0.5
    bx, by = b[0] + 0.5, b[1] + 0.5
    steps = int(max(abs(bx - ax), abs(by - ay)) * 4) + 1
    h, w = len(walkable), len(walkable[0])
    for i in range(steps + 1):
        t = i / steps
        px, py = ax + (bx - ax) * t, ay + (by - ay) * t
        for dx in range(-clearance, clearance + 1):
            for dy in range(-clearance, clearance + 1):
                cx, cy = int(math.floor(px + dx * 0.45)), int(math.floor(py + dy * 0.45))
                if not (0 <= cx < w and 0 <= cy < h) or not walkable[cy][cx]:
                    return False
    return True


def route(walkable, points, max_leg=4):
    """Waypoints through `points` (cells): A* between consecutive points, then string-pulled while the line stays
    clear and legs stay at most `max_leg` cells. Returns a list of (cell, index of the point it reaches or -1)."""
    out = []
    for i in range(len(points) - 1):
        path = astar(walkable, points[i], points[i + 1])
        assert path, f"no route {points[i]} -> {points[i + 1]}"
        j = 0
        while j < len(path) - 1:
            best = j + 1
            for m in range(len(path) - 1, j, -1):
                if abs(path[m][0] - path[j][0]) + abs(path[m][1] - path[j][1]) <= max_leg and clear_line(walkable, path[j], path[m]):
                    best = m
                    break
            out.append((path[best], i + 1 if best == len(path) - 1 else -1))
            j = best
    return out
