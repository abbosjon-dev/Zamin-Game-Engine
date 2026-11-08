"""Shared helpers of the sample game generators (Tools/samples/gen_*.py).

The sample games of Projects/ (TopDownShooter, Breakout, FPSArena, ...) keep no hand-drawn art: their
textures, sprite atlases, sounds and scene files are made by these scripts, so the repository holds the
recipe and the small generated files. Run a generator again after changing it:

    python3 Tools/samples/gen_topdownshooter.py

Requirements: Python 3.9+ and numpy (pictures and sounds are computed as arrays).

What is here:
  - PNG / WAV writers (zlib + struct, the wave module) and .zmeta asset files with stable UUIDs;
  - picture helpers: signed-distance shapes with anti-aliasing and glow, tileable value noise;
  - sound helpers: oscillators, envelopes, noise, simple filters;
  - SceneBuilder: a .zscene (JSON) written the way the engine's SceneSerializer reads it, with helpers for
    the components the samples use (sprites, physics, particles, lights, UI widgets, scripts ...).
"""

import hashlib
import json
import math
import os
import struct
import wave
import zlib

import numpy as np

# ----------------------------------------------------------------------------------------------- files


def ensure_dir(path):
    os.makedirs(path, exist_ok=True)


def write_text(path, text):
    ensure_dir(os.path.dirname(path))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def write_json(path, data, indent=4):
    write_text(path, json.dumps(data, indent=indent, ensure_ascii=False) + "\n")


def dumps_compact(value, indent=2, level=0):
    """JSON like json.dumps(indent=2), but a list of plain values (numbers, strings, booleans) stays on one line:
    "Translation": [0.0, 1.5, 0.0]. A scene of a few hundred entities is a fifth smaller and still diffs well;
    the engine reads it like any JSON (and writes its own layout when the editor saves it)."""
    pad = " " * (indent * level)
    inner = " " * (indent * (level + 1))
    if isinstance(value, dict):
        if not value:
            return "{}"
        items = [f"{inner}{json.dumps(k, ensure_ascii=False)}: {dumps_compact(v, indent, level + 1)}" for k, v in value.items()]
        return "{\n" + ",\n".join(items) + "\n" + pad + "}"
    if isinstance(value, (list, tuple)):
        if not value:
            return "[]"
        if all(not isinstance(v, (dict, list, tuple)) for v in value):
            return "[" + ", ".join(json.dumps(v, ensure_ascii=False) for v in value) + "]"
        items = [inner + dumps_compact(v, indent, level + 1) for v in value]
        return "[\n" + ",\n".join(items) + "\n" + pad + "]"
    return json.dumps(value, ensure_ascii=False)


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def project_dir(name):
    """Where a generator writes its project: Projects/<name> of the repository, or <ZAMIN_SAMPLES_OUT>/<name> when that
    environment variable is set (Tests/samples_assets.py --regen generates there and compares with the repository)."""
    out = os.environ.get("ZAMIN_SAMPLES_OUT")
    return os.path.join(os.path.abspath(out), name) if out else os.path.join(REPO_ROOT, "Projects", name)


def stable_uuid(key):
    """16 hex digits derived from `key` (an asset's path): the same file keeps its UUID run after run."""
    return hashlib.sha1(key.encode("utf-8")).hexdigest()[:16]


def write_zmeta(assets_dir, rel_path, asset_type, import_settings=None, project=""):
    """<asset>.zmeta next to the asset: its UUID (stable) and import settings."""
    meta = {"UUID": stable_uuid(project + "/" + rel_path), "Type": asset_type, "Import": import_settings or {}}
    write_json(os.path.join(assets_dir, rel_path + ".zmeta"), meta)
    return meta["UUID"]


# ------------------------------------------------------------------------------------------------- PNG


def write_png(path, rgba):
    """rgba: H x W x 4 floats 0..1 (or uint8)."""
    ensure_dir(os.path.dirname(path))
    if rgba.dtype != np.uint8:
        rgba = (np.clip(rgba, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    h, w, c = rgba.shape
    assert c == 4
    raw = b"".join(b"\x00" + rgba[y].tobytes() for y in range(h))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def image(w, h, color=(0, 0, 0, 0)):
    img = np.zeros((h, w, 4), dtype=np.float64)
    img[:, :] = color
    return img


def coords(w, h, center=None):
    """Pixel-centre coordinates (x right, y DOWN as in the picture) relative to `center`."""
    cx, cy = center if center is not None else (w / 2.0, h / 2.0)
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    return xs + 0.5 - cx, ys + 0.5 - cy


def smooth(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def sd_circle(x, y, r):
    return np.sqrt(x * x + y * y) - r


def sd_round_box(x, y, hw, hh, r):
    qx = np.abs(x) - (hw - r)
    qy = np.abs(y) - (hh - r)
    return np.sqrt(np.maximum(qx, 0) ** 2 + np.maximum(qy, 0) ** 2) + np.minimum(np.maximum(qx, qy), 0.0) - r


def sd_polygon(x, y, points):
    """Signed distance to a convex or concave polygon (points: list of (x, y))."""
    pts = [np.array(p, dtype=np.float64) for p in points]
    d = None
    s = np.ones_like(x)
    n = len(pts)
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]
        ex, ey = b[0] - a[0], b[1] - a[1]
        wx, wy = x - a[0], y - a[1]
        t = np.clip((wx * ex + wy * ey) / (ex * ex + ey * ey), 0.0, 1.0)
        bx, by = wx - ex * t, wy - ey * t
        dist = bx * bx + by * by
        d = dist if d is None else np.minimum(d, dist)
        c1 = y >= a[1]
        c2 = y < b[1]
        c3 = ex * wy > ey * wx
        flip = (c1 & c2 & c3) | (~c1 & ~c2 & ~c3)
        s = np.where(flip, -s, s)
    return s * np.sqrt(d)


def regular_polygon(n, radius, rotation=0.0, cx=0.0, cy=0.0):
    return [(cx + radius * math.cos(rotation + 2 * math.pi * i / n), cy + radius * math.sin(rotation + 2 * math.pi * i / n)) for i in range(n)]


def fill(img, sd, color, aa=1.0):
    """Paints `color` (r, g, b, a) where the signed distance < 0 (anti-aliased over `aa` px), over img."""
    cov = smooth(aa * 0.5, -aa * 0.5, sd) * color[3]
    over(img, color[:3], cov)
    return cov


def glow(img, sd, color, radius, strength=1.0):
    """A soft halo outside the shape (falls off over `radius` px)."""
    a = np.exp(-np.maximum(sd, 0.0) / max(radius, 1e-3)) * strength * color[3]
    a = np.where(sd < 0, 0.0, a)
    over(img, color[:3], np.clip(a, 0, 1))


def stroke(img, sd, color, width, aa=1.0):
    return fill(img, np.abs(sd) - width * 0.5, color, aa)


def over(img, rgb, alpha):
    """Porter-Duff 'over' of a flat colour with a per-pixel alpha (straight alpha)."""
    alpha = np.clip(alpha, 0.0, 1.0)
    a0 = img[:, :, 3]
    out_a = alpha + a0 * (1.0 - alpha)
    safe = np.where(out_a > 1e-6, out_a, 1.0)
    for i in range(3):
        img[:, :, i] = (rgb[i] * alpha + img[:, :, i] * a0 * (1.0 - alpha)) / safe
    img[:, :, 3] = out_a


def edge_fade(img, px):
    """Alpha falls to 0 over `px` pixels at the picture's border."""
    h, w = img.shape[:2]
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    d = np.minimum(np.minimum(xs + 0.5, w - xs - 0.5), np.minimum(ys + 0.5, h - ys - 0.5))
    img[:, :, 3] *= smooth(0.0, px, d)
    return img


def blit(dst, src, x, y):
    """Copies src into dst at (x, y) (top-left, px)."""
    h, w = src.shape[:2]
    dst[y:y + h, x:x + w] = src


def lerp(a, b, t):
    return a + (b - a) * t


def mix_color(c0, c1, t):
    t = np.asarray(t)[..., None] if np.ndim(t) else t
    return np.asarray(c0, dtype=np.float64) * (1 - t) + np.asarray(c1, dtype=np.float64) * t


def value_noise(w, h, cells, seed=1, octaves=4, tileable=True):
    """fBm of smooth value noise on a `cells` x `cells` grid (tiles seamlessly), 0..1."""
    rng = np.random.default_rng(seed)
    total = np.zeros((h, w))
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        n = cells * (2 ** o)
        grid = rng.random((n + 1, n + 1))
        if tileable:
            grid[n, :] = grid[0, :]
            grid[:, n] = grid[:, 0]
        ys = np.linspace(0, n, h, endpoint=False)
        xs = np.linspace(0, n, w, endpoint=False)
        x0 = np.floor(xs).astype(int)
        y0 = np.floor(ys).astype(int)
        fx = xs - x0
        fy = ys - y0
        fx = fx * fx * (3 - 2 * fx)
        fy = fy * fy * (3 - 2 * fy)
        a = grid[np.ix_(y0, x0)]
        b = grid[np.ix_(y0, x0 + 1)]
        c = grid[np.ix_(y0 + 1, x0)]
        d = grid[np.ix_(y0 + 1, x0 + 1)]
        top = a + (b - a) * fx[None, :]
        bot = c + (d - c) * fx[None, :]
        total += (top + (bot - top) * fy[:, None]) * amp
        norm += amp
        amp *= 0.5
    return total / norm


def normal_map_from_height(height, strength=2.0):
    """A tangent-space normal map (RGBA) from a height field 0..1 (tiles when the height tiles)."""
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * strength
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * strength
    nx, ny, nz = -dx, dy, np.ones_like(height)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    img = np.zeros(height.shape + (4,))
    img[:, :, 0] = nx / length * 0.5 + 0.5
    img[:, :, 1] = ny / length * 0.5 + 0.5
    img[:, :, 2] = nz / length * 0.5 + 0.5
    img[:, :, 3] = 1.0
    return img


class Atlas:
    """A sprite atlas: pictures packed in rows, written as one PNG with the named frames in its .zmeta."""

    def __init__(self, width, height):
        self.img = image(width, height)
        self.frames = []
        self.x = self.y = self.row_h = 0
        self.pad = 2

    def add(self, name, picture, border=None):
        h, w = picture.shape[:2]
        if not border:
            # a glow that reaches the frame's edge would show the quad's square: fade it out there
            picture = edge_fade(picture.copy(), max(3.0, min(w, h) * 0.12))
        if self.x + w > self.img.shape[1]:
            self.x = 0
            self.y += self.row_h + self.pad
            self.row_h = 0
        assert self.y + h <= self.img.shape[0], f"atlas full at {name}"
        blit(self.img, picture, self.x, self.y)
        frame = {"Name": name, "Rect": [self.x, self.y, w, h], "Pivot": [0.5, 0.5]}
        if border:
            frame["Border"] = list(border)
        self.frames.append(frame)
        self.x += w + self.pad
        self.row_h = max(self.row_h, h)

    def save(self, assets_dir, rel_path, project):
        write_png(os.path.join(assets_dir, rel_path), self.img)
        return write_zmeta(assets_dir, rel_path, "Texture", {"Sprites": self.frames}, project)


# ----------------------------------------------------------------------------------------------- sound

RATE = 22050


def t_axis(seconds, rate=RATE):
    return np.arange(int(seconds * rate)) / rate


def osc(freq, seconds, shape="sine", rate=RATE, phase=0.0):
    """freq: a number or an array (a sweep) of len seconds*rate."""
    n = int(seconds * rate)
    f = np.full(n, float(freq)) if np.isscalar(freq) else np.asarray(freq, dtype=np.float64)[:n]
    ph = np.cumsum(f) / rate + phase
    frac = ph - np.floor(ph)
    if shape == "sine":
        return np.sin(2 * np.pi * ph)
    if shape == "square":
        return np.where(frac < 0.5, 1.0, -1.0)
    if shape == "saw":
        return frac * 2.0 - 1.0
    if shape == "tri":
        return 1.0 - 4.0 * np.abs(frac - 0.5)
    raise ValueError(shape)


def noise(seconds, seed=0, rate=RATE):
    return np.random.default_rng(seed).uniform(-1, 1, int(seconds * rate))


def env(n, attack=0.005, decay=0.2, sustain=0.0, hold=0.0, release=0.05, rate=RATE):
    """Attack-decay-sustain-release envelope of n samples (times in seconds; the release starts after
    attack + decay + hold)."""
    t = np.arange(n) / rate
    rise = np.clip(t / max(attack, 1e-6), 0.0, 1.0)
    fall = 1.0 - (1.0 - sustain) * np.clip((t - attack) / max(decay, 1e-6), 0.0, 1.0)
    e = np.where(t < attack, rise, fall)
    release_start = attack + decay + hold
    return np.where(t > release_start, e * np.clip(1.0 - (t - release_start) / max(release, 1e-6), 0.0, 1.0), e)


def exp_decay(n, seconds, rate=RATE):
    return np.exp(-np.arange(n) / rate / max(seconds, 1e-6))


def lowpass(x, cutoff, rate=RATE):
    """One-pole low-pass; cutoff: Hz (number or per-sample array)."""
    cut = np.full(len(x), float(cutoff)) if np.isscalar(cutoff) else np.asarray(cutoff)
    a = 1.0 - np.exp(-2.0 * np.pi * cut / rate)
    y = np.zeros_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc += a[i] * (x[i] - acc)
        y[i] = acc
    return y


def highpass(x, cutoff, rate=RATE):
    return x - lowpass(x, cutoff, rate)


def mix(*parts):
    n = max(len(p) for p in parts)
    out = np.zeros(n)
    for p in parts:
        out[:len(p)] += p
    return out


def place(buffer, sound, at_seconds, gain=1.0, rate=RATE):
    i = int(at_seconds * rate)
    end = min(len(buffer), i + len(sound))
    if i < len(buffer):
        buffer[i:end] += sound[:end - i] * gain


def write_wav(path, samples, rate=RATE, peak=0.89):
    """Mono 16-bit WAV, normalised to `peak`, with 3 ms fades so it never clicks."""
    ensure_dir(os.path.dirname(path))
    x = np.asarray(samples, dtype=np.float64)
    m = np.max(np.abs(x)) if len(x) else 0.0
    if m > 0:
        x = x / m * peak
    fade = min(len(x) // 2, int(0.003 * rate))
    if fade > 0:
        ramp = np.linspace(0, 1, fade)
        x[:fade] *= ramp
        x[-fade:] *= ramp[::-1]
    data = (np.clip(x, -1, 1) * 32767).astype("<i2").tobytes()
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(data)


def note_freq(name):
    """'A4', 'C#5', 'Eb3' -> Hz."""
    names = {"C": -9, "D": -7, "E": -5, "F": -4, "G": -2, "A": 0, "B": 2}
    semis = names[name[0]]
    rest = name[1:]
    if rest.startswith("#"):
        semis += 1
        rest = rest[1:]
    elif rest.startswith("b"):
        semis -= 1
        rest = rest[1:]
    octave = int(rest)
    return 440.0 * 2 ** ((semis + (octave - 4) * 12) / 12.0)


# ----------------------------------------------------------------------------------------------- scene


def v(*xs):
    return [float(x) for x in xs]


def color(r, g, b, a=1.0):
    return [float(r), float(g), float(b), float(a)]


class SceneBuilder:
    """Entities of a .zscene with their components (the engine's JSON keys). IDs: base + n."""

    def __init__(self, name, base_id):
        self.name = name
        self.base = base_id
        self.next = 1
        self.entities = []
        self.by_id = {}
        self.children = {}
        self.roots = []
        self.settings = {}
        self.metadata = {}

    def entity(self, name, parent=None, pos=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1), active=True, **components):
        eid = self.base + self.next
        self.next += 1
        e = {"Entity": eid, "Tag": name}
        if not active:
            e["Active"] = False
        if parent is not None:
            e["Parent"] = parent
            self.children.setdefault(parent, []).append(eid)
        else:
            self.roots.append(eid)
        e["Transform"] = {"Translation": v(*pos), "Rotation": v(*rot), "Scale": v(*scale)}
        for key, data in components.items():
            e[key] = data
        self.entities.append(e)
        self.by_id[eid] = e
        return eid

    def add(self, eid, key, data):
        self.by_id[eid][key] = data

    def get(self, eid):
        return self.by_id[eid]

    def to_json(self):
        ents = []
        for e in self.entities:
            e = dict(e)
            if e["Entity"] in self.children:
                e["ChildOrder"] = self.children[e["Entity"]]
            ents.append(e)
        root = {"Scene": self.name, "Version": 5, "Settings": self.settings}
        if self.metadata:
            root["Metadata"] = self.metadata
        root["RootOrder"] = self.roots
        root["Entities"] = sorted(ents, key=lambda e: e["Entity"])
        return root

    def save(self, assets_dir, rel_path, project):
        write_text(os.path.join(assets_dir, rel_path), dumps_compact(self.to_json()) + "\n")
        return write_zmeta(assets_dir, rel_path, "Scene", {}, project)


# ---- scene settings


def settings_2d(clear_sky=False, ambient2d=(0.5, 0.55, 0.8), ambient2d_intensity=0.6, bloom=(0.85, 0.5, 0.8), vignette=0.3,
                saturation=1.1, contrast=1.05, chroma=0.0, grain=0.0, gravity=(0.0, 0.0)):
    threshold, knee, intensity = bloom
    return {
        "DrawSky": clear_sky,
        "Ambient": 0.6,
        "Exposure": 1.0,
        "Gravity": v(*gravity),
        "Post": {"Tonemap": 1, "Bloom": True, "BloomThreshold": threshold, "BloomKnee": knee, "BloomIntensity": intensity,
                 "BloomRadius": 1.0, "Vignette": vignette, "ChromaticAberration": chroma, "FilmGrain": grain,
                 "Saturation": saturation, "Contrast": contrast, "Brightness": 0.0, "Tint": [1, 1, 1], "FXAA": False},
        "Lighting2D": {"AmbientColor": v(*ambient2d), "AmbientIntensity": ambient2d_intensity},
    }


# ---- components


def camera_ortho(size, clear=(0.02, 0.02, 0.05, 1.0), primary=True, shake=None):
    c = {"ProjectionType": 1, "PerspectiveFOV": 0.7854, "PerspectiveNear": 0.1, "PerspectiveFar": 1000,
         "OrthographicSize": float(size), "OrthographicNear": -100, "OrthographicFar": 100, "Primary": primary,
         "FixedAspectRatio": False, "ClearColor": color(*clear)}
    if shake:
        c["Shake"] = shake
    return c


def camera_persp(fov_radians, near=0.1, far=500.0, clear=(0.1, 0.1, 0.12, 1.0), primary=True, shake=None):
    c = {"ProjectionType": 0, "PerspectiveFOV": float(fov_radians), "PerspectiveNear": near, "PerspectiveFar": far,
         "OrthographicSize": 10, "OrthographicNear": -100, "OrthographicFar": 100, "Primary": primary,
         "FixedAspectRatio": False, "ClearColor": color(*clear)}
    if shake:
        c["Shake"] = shake
    return c


def camera_follow(target, offset=(0, 0), damping=(0.12, 0.12), dead_zone=(0.5, 0.4), look_ahead=(1.2, 1.0), bounds=None):
    c = {"Target": target, "Offset": v(*offset), "Damping": v(*damping), "DeadZone": v(*dead_zone), "LookAhead": v(*look_ahead),
         "LookAheadDamping": 0.4, "LookAheadMinSpeed": 0.5, "UseBounds": bounds is not None,
         "BoundsMin": v(*(bounds[0] if bounds else (0, 0))), "BoundsMax": v(*(bounds[1] if bounds else (0, 0))), "SnapOnStart": True}
    return c


def sprite(texture="", frame=None, col=(1, 1, 1, 1), order=0, layer=0, lit=False, tiling=1.0, pixel_art=False, size=None,
           draw_mode=None, ppu=None):
    s = {"Color": color(*col), "Texture": texture, "Tiling": float(tiling), "SortingOrder": order, "PixelArt": pixel_art,
         "FlipX": False, "FlipY": False}
    if frame:
        s["Sprite"] = frame
    if lit:
        s["Lit"] = True
    if layer:
        s["SortingLayer"] = layer
    if draw_mode:
        s["DrawMode"] = draw_mode
    if size:
        s["Size"] = v(*size)
    if ppu:
        s["PixelsPerUnit"] = float(ppu)
    return s


def circle(col=(1, 1, 1, 1), thickness=1.0, fade=0.005, order=0):
    return {"Color": color(*col), "Thickness": float(thickness), "Fade": float(fade), "SortingOrder": order}


def world_text(text, col=(1, 1, 1, 1), align="Center", order=1000, key=None):
    t = {"Text": text, "Color": color(*col), "Kerning": 0.0, "LineSpacing": 0.0, "AlignH": align, "AlignV": "Middle", "Font": ""}
    if order != 1000:
        t["SortingOrder"] = order
    if key:
        t["LocalizationKey"] = key
    return t


def light2d(col=(1, 1, 1), intensity=1.0, radius=4.0, inner=0.0, falloff=2.0, flicker=0.0):
    return {"Type": "Point", "Color": v(*col), "Intensity": float(intensity), "Radius": float(radius), "InnerRadius": float(inner),
            "Falloff": float(falloff), "SpotAngle": 60.0, "SpotSoftness": 0.3, "Flicker": float(flicker), "FlickerSpeed": 8.0}


def rigidbody2d(body="Dynamic", gravity=0.0, fixed_rotation=True, linear_damping=0.0, angular_damping=0.0, bullet=False, layer=0,
                sleeping=None):
    r = {"BodyType": body, "FixedRotation": fixed_rotation, "GravityScale": float(gravity), "LinearDamping": float(linear_damping),
         "AngularDamping": float(angular_damping), "Bullet": bullet}
    if layer:
        r["Layer"] = layer
    if sleeping:
        r["Sleeping"] = sleeping
    return r


def _collider(offset=(0, 0), density=1.0, friction=0.0, restitution=0.0, sensor=False):
    return {"Offset": v(*offset), "Density": float(density), "Friction": float(friction), "Restitution": float(restitution),
            "RestitutionThreshold": 0.0, "IsSensor": sensor}


def box2d(size=(1, 1), **kw):
    c = _collider(**kw)
    c["Size"] = v(*size)
    return c


def circle2d(radius=0.5, **kw):
    c = _collider(**kw)
    c["Radius"] = float(radius)
    return c


def particles(**kw):
    """A particle system (the first systems' keys; the rest keep the engine's defaults)."""
    p = {"Playing": True, "Looping": True, "PlayInEditor": True, "EmissionRate": 30.0, "Burst": 0, "MaxParticles": 500, "Shape": 0,
         "ShapeRadius": 0.5, "ShapeExtents": [0.5, 0.5, 0.5], "LifetimeMin": 0.5, "LifetimeMax": 1.0, "SpeedMin": 1.0, "SpeedMax": 3.0,
         "Direction": [0, 1, 0], "Spread": 25.0, "Gravity": [0, 0, 0], "Drag": 0.0, "AngularVelocityMax": 0.0,
         "StartColor": [1, 1, 1, 1], "EndColor": [1, 1, 1, 0], "StartSize": 0.3, "EndSize": 0.05, "RenderAsCircle": True,
         "Additive": True, "WorldSpace": True, "Billboard": True, "Texture": "", "SortingOrder": 0}
    for key, value in kw.items():
        p[key] = [float(x) for x in value] if isinstance(value, (tuple, list)) else value
    return p


def audio_source(path, volume=1.0, loop=False, play=False, output="SFX", stream=False, spatial=False):
    a = {"Path": path, "Volume": float(volume), "Pitch": 1.0, "Loop": loop, "PlayOnStart": play, "Spatial": spatial, "MinDistance": 1.0,
         "MaxDistance": 30.0, "Output": output}
    if stream:
        a["Stream"] = True
    return a


def script(cls, **fields):
    """A NativeScript with typed fields: values are (type, value) pairs or plain values (type guessed)."""
    out = {}
    for name, value in fields.items():
        if isinstance(value, tuple) and len(value) == 2 and isinstance(value[0], str) and value[0] in (
                "Float", "Int", "Bool", "Vec2", "Vec3", "Vec4", "String", "Entity", "Enum"):
            kind, val = value
        elif isinstance(value, bool):
            kind, val = "Bool", value
        elif isinstance(value, int):
            kind, val = "Int", value
        elif isinstance(value, float):
            kind, val = "Float", value
        elif isinstance(value, str):
            kind, val = "String", value
        else:
            raise TypeError(f"{cls}.{name}: give the field's type")
        if kind in ("Vec2", "Vec3", "Vec4"):
            val = v(*val)
        out[name] = {"Type": kind, "Value": val}
    return {"Class": cls, "Fields": out}


def ref(eid):
    return ("Entity", int(eid))


def tags(*names):
    return list(names)


# ---- 3D


def mesh(kind="Cube", albedo=(0.8, 0.8, 0.8, 1), roughness=0.6, metallic=0.0, emission=(0, 0, 0), emission_strength=0.0,
         albedo_map="", normal_map="", tiling=1.0, offset=(0, 0), normal_strength=1.0, cast_shadows=True, model=""):
    return {"Mesh": kind, "Model": model, "CastShadows": cast_shadows, "Material": "", "Albedo": color(*albedo), "AlbedoMap": albedo_map,
            "Tiling": tiling if np.isscalar(tiling) else v(*tiling), "Offset": v(*offset), "NormalMap": normal_map,
            "NormalStrength": float(normal_strength), "MetalRoughMap": "", "EmissiveMap": "", "Metallic": float(metallic),
            "Roughness": float(roughness), "Emission": v(*emission), "EmissionStrength": float(emission_strength), "Shader": "Lit",
            "BlendMode": "Opaque", "AlphaCutoff": 0.5, "DoubleSided": False, "PreserveSpecular": False, "SortPriority": 0}


def dir_light(col=(1, 0.95, 0.85), intensity=3.0, shadows=True):
    return {"Color": v(*col), "Intensity": float(intensity), "CastShadows": shadows}


def point_light(col=(1, 1, 1), intensity=2.0, radius=6.0):
    return {"Color": v(*col), "Intensity": float(intensity), "Radius": float(radius)}


def spot_light(col=(1, 1, 1), intensity=10.0, range_=10.0, inner=25.0, outer=40.0):
    return {"Color": v(*col), "Intensity": float(intensity), "Range": float(range_), "InnerAngle": float(inner), "OuterAngle": float(outer)}


def rigidbody3d(body="Static", mass=1.0, linear_damping=0.05, angular_damping=0.05, gravity=1.0, lock_rotation=False, layer=0):
    r = {"BodyType": body, "Mass": float(mass), "LinearDamping": float(linear_damping), "AngularDamping": float(angular_damping),
         "GravityFactor": float(gravity), "LockRotation": lock_rotation}
    if layer:
        r["Layer"] = layer
    return r


def box3d(half=(0.5, 0.5, 0.5), offset=(0, 0, 0), friction=0.6, restitution=0.0, sensor=False):
    return {"HalfExtents": v(*half), "Offset": v(*offset), "Friction": float(friction), "Restitution": float(restitution), "IsSensor": sensor}


def sphere3d(radius=0.5, offset=(0, 0, 0), friction=0.5, restitution=0.2, sensor=False):
    return {"Radius": float(radius), "Offset": v(*offset), "Friction": float(friction), "Restitution": float(restitution), "IsSensor": sensor}


def character3d(radius=0.35, height=1.8, max_slope=50.0, step=0.4, gravity_scale=1.0, layer=0):
    c = {"Radius": float(radius), "Height": float(height), "MaxSlope": float(max_slope), "StepHeight": float(step), "Mass": 70.0,
         "GravityScale": float(gravity_scale), "StickToFloor": True}
    if layer:
        c["Layer"] = layer
    return c


# ---- UI


def ui_rect(anchor_min=(0.5, 0.5), anchor_max=None, pivot=(0.5, 0.5), pos=(0, 0), size=(100, 40), font=0.0, order=0, alpha=None,
            scale=None):
    r = {"AnchorMin": v(*anchor_min), "AnchorMax": v(*(anchor_max if anchor_max is not None else anchor_min)), "Pivot": v(*pivot),
         "Position": v(*pos), "Size": v(*size), "FontSize": float(font), "Order": order}
    if alpha is not None:
        r["Alpha"] = float(alpha)
    if scale is not None:
        r["Scale"] = v(*scale)
    return r


def ui_stretch(margin=(0, 0, 0, 0), order=0):
    """Fills the parent (margins left, right, top, bottom)."""
    l, r, t, b = margin
    return ui_rect((0, 0), (1, 1), (0.5, 0.5), ((l - r) / 2.0, (b - t) / 2.0), (-(l + r), -(t + b)), 0.0, order)


def ui_canvas(ref_res=(1920, 1080)):
    return {"ScaleMode": "ScaleWithScreenSize", "ScaleFactor": 1.0, "ReferenceResolution": v(*ref_res), "MatchMode": "MatchWidthOrHeight",
            "Match": 0.5}


def ui_image(col=(1, 1, 1, 1), texture="", frame=None, kind="Simple", border_scale=None, raycast=True, preserve_aspect=False,
             fill=None):
    i = {"Color": color(*col), "Type": kind, "PreserveAspect": preserve_aspect, "PixelArt": False, "RaycastTarget": raycast,
         "Texture": texture}
    if frame:
        i["Sprite"] = frame
    if border_scale is not None:
        i["BorderScale"] = float(border_scale)
    if fill:
        i["Fill"] = fill
    return i


def ui_text(text, col=(1, 1, 1, 1), h="Center", vert="Middle", key=None, wrap=False, hints=False):
    t = {"Text": text, "Color": color(*col), "Kerning": 0.0, "LineSpacing": 0.0, "AlignH": h, "AlignV": vert, "Font": ""}
    if key:
        t["LocalizationKey"] = key
    if hints:
        t["InputHints"] = True  # "[Action]" -> the controls of the device in use (TextComponent::InputHints)
    if wrap:
        t["WordWrap"] = True
    return t


# The control names of the "[Action]" hints (Input::ExpandBindings) in the games' language files ("input.<id>" keys).
# English repeats the engine's own names: a key missing in a language falls back to the default language (Uzbek).
INPUT_NAMES = {
    "uz": {"key": {"Arrows": "strelkalar", "Left": "chap strelka", "Right": "o'ng strelka", "Up": "yuqori strelka",
                   "Down": "past strelka"},
           "mouse": {"Left": "LKM", "Right": "PKM", "Middle": "o'rta tugma"},
           "stick": {"Left": "chap stik", "Right": "o'ng stik"},
           "pad": {"DPad": "D-pad", "DPadLeft": "D-pad chap", "DPadRight": "D-pad o'ng", "DPadUp": "D-pad yuqori",
                   "DPadDown": "D-pad past", "LeftThumb": "chap stik bosish"}},
    "en": {"key": {"Arrows": "arrows", "Left": "Left", "Right": "Right", "Up": "Up", "Down": "Down"},
           "mouse": {"Left": "LMB", "Right": "RMB", "Middle": "MMB"},
           "stick": {"Left": "left stick", "Right": "right stick"},
           "pad": {"DPad": "D-pad", "DPadLeft": "D-pad left", "DPadRight": "D-pad right", "DPadUp": "D-pad up",
                   "DPadDown": "D-pad down", "LeftThumb": "left stick click"}},
}

# The second line of every game's controls hint: the hints follow the device the player uses.
DEVICE_NOTE = {
    "uz": "[kb:Gamepad ham ishlaydi - uni ishlatsangiz, ishoralar uning tugmalarini ko'rsatadi]"
          "[pad:Gamepad - klaviatura yoki sichqoncha ishlatilsa, ishoralar ularga qaytadi]",
    "en": "[kb:A gamepad works too - use it and the hints show its buttons]"
          "[pad:Gamepad - use the keyboard or mouse and the hints switch back]",
}


def controls_hint(lang, line):
    """A main menu's controls text: the game's line of "[Action] - what it does" hints and the device note."""
    return line + "\n" + DEVICE_NOTE[lang]


def ui_button(normal, hover, pressed, message="", target=0):
    b = {"Normal": color(*normal), "Hover": color(*hover), "Pressed": color(*pressed), "Interactable": True}
    if message:
        b["ClickMessage"] = message
        b["ClickTarget"] = target
    return b


def ui_nav(mode="Vertical", select_on_enable=False, highlight=(1.0, 0.85, 0.3, 1.0)):
    return {"Mode": mode, "Up": 0, "Down": 0, "Left": 0, "Right": 0, "SelectOnEnable": select_on_enable, "ShowHighlight": True,
            "HighlightColor": color(*highlight)}


def ui_anim(show="None", hide="None", duration=0.25, delay=0.0, hover=1.0, press=1.0, show_at_start=True, ease="OutBack",
            slide=160.0):
    return {"Show": show, "Hide": hide, "ShowAtStart": show_at_start, "Duration": float(duration), "ShowDelay": float(delay),
            "ShowEase": ease, "HideEase": "InQuad", "SlideDistance": float(slide), "HoverScale": float(hover), "PressScale": float(press),
            "FeedbackDuration": 0.08}


def ui_layout(kind="Vertical", padding=(0, 0, 0, 0), spacing=12.0, align=4, fit_height=False, control_width=True, expand_width=True):
    return {"Type": kind, "Padding": v(*padding), "Alignment": align, "FitWidth": False, "FitHeight": fit_height, "Spacing": float(spacing),
            "Reverse": False, "ControlWidth": control_width, "ControlHeight": False, "ExpandWidth": expand_width, "ExpandHeight": False}


def ui_progress(value=1.0, back=(0.05, 0.06, 0.1, 0.8), fill=(0.3, 0.9, 1.0, 1.0), padding=3.0, smooth=10.0):
    return {"Value": float(value), "Direction": "LeftToRight", "BackgroundColor": color(*back), "FillColor": color(*fill),
            "UseLowColor": False, "LowColor": [1, 0.2, 0.2, 1], "Padding": float(padding), "SmoothSpeed": float(smooth),
            "ShowPercentage": False, "TextColor": [1, 1, 1, 1]}


# ---------------------------------------------------------------------------------------------- project


def axis(name, negative, positive, gamepad_axis="", invert=False, deadzone=0.2):
    a = {"Name": name, "Negative": negative, "Positive": positive, "Deadzone": deadzone, "Invert": invert}
    if gamepad_axis:
        a["GamepadAxis"] = gamepad_axis
    return a


def write_project(project_dir, name, product, start_scene, start_uuid, version="1.0.0", build_scenes=(), actions=(), axes=(),
                  vectors=(), tags_=(), layers=None, localization=("uz", "en"), audio_groups=None, extra=None, description=""):
    """The .zproject. `description`: the launcher's text; the launcher's picture is Thumbnail.jpg next to the project
    (made from a real screenshot by Tools/samples/make_thumbnails.py) - named here when it exists."""
    proj = {
        "Version": 2,
        "Project": {"Name": name, "StartScene": start_scene, "StartSceneUUID": start_uuid, "AssetDirectory": "Assets",
                    "SourceDirectory": "Source", "ScriptModulePath": f"Binaries/lib{name}.so", "EngineVersion": "0.1.0"},
        "Player": {"Company": "Zamin Studio", "Product": product, "Version": version, "Width": 1280, "Height": 720,
                   "WindowMode": "Windowed", "Resizable": True, "VSync": True, "Title": "", "VersionInTitle": False,
                   "TargetFrameRate": 0, "RunInBackground": True, "Icon": "Textures/GameIcon.png"},
        "BuildScenes": [{"Path": p, "Enabled": True, "UUID": u} for p, u in build_scenes],
        "InputActions": {"Actions": [{"Name": n, "Bindings": b} for n, b in actions], "Axes": list(axes), "Vectors": list(vectors)},
    }
    if description:
        proj["Project"]["Description"] = description
    # (the repository's picture also counts when generating elsewhere: ZAMIN_SAMPLES_OUT)
    if any(os.path.isfile(os.path.join(d, "Thumbnail.jpg")) for d in (project_dir, os.path.join(REPO_ROOT, "Projects", name))):
        proj["Project"]["Thumbnail"] = "Thumbnail.jpg"
    if tags_:
        proj["Tags"] = list(tags_)
    if layers:
        names = (list(layers["names"]) + [""] * 16)[:16]
        proj["CollisionLayers2D"] = {"Names": names, "Collisions": layers["matrix"]}
    if localization:
        proj["Localization"] = {"DefaultLanguage": localization[0], "Languages": list(localization), "Directory": "Localization",
                                "UseSystemLanguage": False}
    proj["Audio"] = {"Groups": audio_groups or [
        {"Name": "Master", "Volume": 1.0},
        {"Name": "Music", "Parent": "Master", "Volume": 0.55},
        {"Name": "SFX", "Parent": "Master", "Volume": 0.9},
        {"Name": "UI", "Parent": "Master", "Volume": 0.8},
    ], "StreamThresholdMB": 4.0}
    if extra:
        proj.update(extra)
    write_json(os.path.join(project_dir, f"{name}.zproject"), proj)


def export_always_include(patterns):
    """The .zproject's "Export" section (the engine's defaults) with files the export takes even though nothing names
    them literally - a script's Sfx("hit") builds "Audio/hit.wav" at runtime, the export's scan of the sources cannot
    see it (Tests/samples_assets.py checks every such sound is covered)."""
    return {"Export": {"ExcludeUnusedAssets": True, "AlwaysInclude": list(patterns), "ReleaseScriptModule": True,
                       "StripBinaries": True}}


def collision_matrix(names, pairs):
    """16 bitmasks: layer i collides with layer j for each (a, b) in pairs (names), both ways."""
    index = {n: i for i, n in enumerate(names)}
    masks = [0] * 16
    for a, b in pairs:
        i, j = index[a], index[b]
        masks[i] |= 1 << j
        masks[j] |= 1 << i
    return masks


def write_module_cmake(project_dir, name, comment):
    write_text(os.path.join(project_dir, "CMakeLists.txt"), f"""# {comment}
cmake_minimum_required(VERSION 3.20)
project({name} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Engine ichida build qilinganda Zamin target mavjud; aks holda
# -DZamin_DIR=<engine build papkasi> orqali topiladi (Editor buni avtomatik beradi).
if(NOT TARGET Zamin)
    find_package(Zamin REQUIRED CONFIG)
endif()

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS ${{CMAKE_CURRENT_SOURCE_DIR}}/Source/*.cpp ${{CMAKE_CURRENT_SOURCE_DIR}}/Source/*.h)
zamin_add_game_module({name} ${{GAME_SOURCES}})
# The sample games are built with the engine's own warnings (cmake/ZaminGame.cmake).
zamin_target_settings({name})
""")
    write_text(os.path.join(project_dir, ".gitignore"), "Build/\nBinaries/\n.zamin/\n")


class UIKit:
    """The sample games' menu pieces: a dimmed full-screen overlay, a 9-slice panel with a vertical layout,
    buttons that send a message to the game's script, labels. `atlas` holds the "panel" and "button" frames."""

    def __init__(self, scene, canvas, atlas, target, normal, hover, pressed, highlight=(1.0, 0.85, 0.3, 1.0)):
        self.s = scene
        self.canvas = canvas
        self.atlas = atlas
        self.target = target
        self.colors = (normal, hover, pressed)
        self.highlight = highlight

    def text(self, name, parent, txt, anchor, pivot, pos, size, font, col=(1, 1, 1, 1), h="Left", key=None, active=True, **extra):
        return self.s.entity(name, parent=parent, active=active, UITransform=ui_rect(anchor, None, pivot, pos, size, font),
                             Text=ui_text(txt, col, h, key=key), **extra)

    def dim(self, name, active, alpha=0.6, tint=(0.0, 0.01, 0.04)):
        return self.s.entity(name, parent=self.canvas, active=active, UITransform=ui_stretch(order=10),
                             UIImage=ui_image((*tint, alpha)), UIAnimation=ui_anim("Fade", "Fade", 0.25))

    def panel(self, name, parent, pos, size, padding=(44, 44, 40, 44), spacing=16):
        return self.s.entity(name, parent=parent, UITransform=ui_rect(pos=pos, size=size),
                             UIImage=ui_image(texture=self.atlas, frame="panel", kind="Sliced", border_scale=1.0),
                             UILayoutGroup=ui_layout("Vertical", padding, spacing, align=1, fit_height=True),
                             UIAnimation=ui_anim("Pop", "None", 0.3))

    def button(self, name, parent, label, key, message, select=False, size=(460, 72), font=32):
        n, h, p = self.colors
        return self.s.entity(name, parent=parent, UITransform=ui_rect(size=size, font=font),
                             UIButton=ui_button(n, h, p, message, self.target),
                             UIImage=ui_image(texture=self.atlas, frame="button", kind="Sliced", border_scale=1.0),
                             Text=ui_text(label, (1, 1, 1, 1), "Center", key=key), UINavigation=ui_nav("Vertical", select, self.highlight),
                             UIAnimation=ui_anim(hover=1.05, press=0.95))

    def label(self, name, parent, txt, size, font, col=(1, 1, 1, 1), key=None):
        return self.s.entity(name, parent=parent, UITransform=ui_rect(size=size, font=font), Text=ui_text(txt, col, "Center", key=key))


def ui_panel_sprites(atlas, fill_top, fill_bottom, rim, glow_col, button_top=(1.0, 1.0, 1.0), button_bottom=(0.78, 0.8, 0.86)):
    """The 'panel' and 'button' 9-slice frames of a sample's atlas."""
    img = image(96, 96)
    x, y = coords(96, 96)
    sd = sd_round_box(x, y, 44, 44, 22)
    glow(img, sd, (*glow_col, 1.0), 3, 0.5)
    fill_col = mix_color(fill_top, fill_bottom, np.clip(y / 96 + 0.5, 0, 1))
    cov = smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = img[:, :, i] * (1 - cov) + fill_col[:, :, i] * cov
    img[:, :, 3] = np.maximum(img[:, :, 3], cov * 0.93)
    stroke(img, sd + 1.5, (*rim, 1.0), 2.5)
    atlas.add("panel", img, border=(30, 30, 30, 30))
    img = image(96, 64)
    x, y = coords(96, 64)
    sd = sd_round_box(x, y, 44, 28, 16)
    shade = mix_color(button_top, button_bottom, np.clip(y / 64 + 0.5, 0, 1))
    cov = smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = shade[:, :, i]
    img[:, :, 3] = cov
    stroke(img, sd + 1.5, (1, 1, 1, 0.9), 2)
    atlas.add("button", img, border=(22, 22, 22, 22))
