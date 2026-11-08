"""Projects/Racing: "Vodiy Poygasi" (Valley Race) - its track meshes, textures, sounds, scene and project file.

    python3 Tools/samples/gen_racing.py

The track is a closed Catmull-Rom loop through CONTROL; its centre line (resampled every 2 m) goes to the game
script (RaceGame.TrackPoints) and the road, kerbs and barriers are built along it as .obj meshes. The car physics,
the bots and the race rules are C++ (Projects/Racing/Source).
"""

import math
import os
import random
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import samplekit as k  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NAME = "Racing"
PROJECT = k.project_dir(NAME)  # Projects/<NAME> (or $ZAMIN_SAMPLES_OUT/<NAME>: Tests/samples_assets.py --regen)
ASSETS = os.path.join(PROJECT, "Assets")
ATLAS = "Textures/ui_atlas.png"

# The circuit (x, z): the start line is at the first point, the cars go towards the second.
CONTROL = [(0, 60), (0, -60), (22, -112), (80, -128), (125, -95), (118, -42), (78, -12), (92, 40), (140, 72), (128, 124),
           (64, 138), (18, 112)]
HALF_WIDTH = 6.0      # asphalt
KERB = 1.2            # kerb width outside the asphalt
WALL = 10.5           # barrier distance from the centre line (Track::WallOffset)
CHECKPOINTS = 8

# --------------------------------------------------------------------------------------------- track


def catmull(points, n=40):
    P = [np.array(p, float) for p in points]
    out = []
    N = len(P)
    for i in range(N):
        p0, p1, p2, p3 = P[(i - 1) % N], P[i], P[(i + 1) % N], P[(i + 2) % N]
        for j in range(n):
            t = j / n
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3))
    return np.array(out)


def resample(pts, step):
    closed = np.vstack([pts, pts[:1]])
    seg = np.linalg.norm(np.diff(closed, axis=0), axis=1)
    cum = np.concatenate([[0], np.cumsum(seg)])
    total = cum[-1]
    n = int(round(total / step))
    s = np.linspace(0, total, n, endpoint=False)
    return np.stack([np.interp(s, cum, closed[:, 0]), np.interp(s, cum, closed[:, 1])], 1), total


def centre_line():
    pts, total = resample(catmull(CONTROL), 2.0)
    n = len(pts)
    tangents = np.array([pts[(i + 1) % n] - pts[i - 1] for i in range(n)])
    tangents /= np.linalg.norm(tangents, axis=1)[:, None]
    normals = np.stack([-tangents[:, 1], tangents[:, 0]], 1)  # to the left of the direction of travel... (x, z) turned
    dist = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(np.vstack([pts, pts[:1]]), axis=0), axis=1))])[:n]
    return pts, normals, dist, total


def write_obj(rel, verts, uvs, norms, faces, comment):
    """A mesh as .obj (v / vt / vn, triangles with the same index for all three)."""
    path = os.path.join(ASSETS, rel)
    k.ensure_dir(os.path.dirname(path))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# Zamin Engine: {comment} (Tools/samples/gen_racing.py)\n")
        for v in verts:
            f.write(f"v {v[0]:.3f} {v[1]:.3f} {v[2]:.3f}\n")
        for t in uvs:
            f.write(f"vt {t[0]:.4f} {t[1]:.4f}\n")
        for nn in norms:
            f.write(f"vn {nn[0]:.3f} {nn[1]:.3f} {nn[2]:.3f}\n")
        for a, b, c in faces:
            f.write(f"f {a + 1}/{a + 1}/{a + 1} {b + 1}/{b + 1}/{b + 1} {c + 1}/{c + 1}/{c + 1}\n")
    k.write_zmeta(ASSETS, rel, "Model", {}, NAME)


def ribbon(pts, normals, dist, inner, outer, y, v_tile, u0=0.0, u1=1.0):
    """A flat strip from `inner` to `outer` metres off the centre line (both may be negative), facing up."""
    n = len(pts)
    verts, uvs, norms, faces = [], [], [], []
    for i in range(n + 1):
        j = i % n
        p, nn = pts[j], normals[j]
        d = dist[j] if i < n else dist[-1] + np.linalg.norm(pts[0] - pts[-1])
        for off, u in ((inner, u0), (outer, u1)):
            q = p + nn * off
            verts.append((q[0], y, q[1]))
            uvs.append((u, d / v_tile))
            norms.append((0, 1, 0))
    for i in range(n):
        a, b, c, d = 2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 3
        # counter-clockwise seen from above (the up side is the front; `normals` point right of the travel)
        if (outer - inner) > 0:
            faces += [(a, b, c), (b, d, c)]
        else:
            faces += [(a, c, b), (b, c, d)]
    return verts, uvs, norms, faces


def wall(pts, normals, dist, offset, height, v_tile, facing_in):
    """A vertical barrier `offset` metres off the centre line, its face towards the track."""
    n = len(pts)
    verts, uvs, norms, faces = [], [], [], []
    for i in range(n + 1):
        j = i % n
        p, nn = pts[j], normals[j]
        d = dist[j] if i < n else dist[-1] + np.linalg.norm(pts[0] - pts[-1])
        q = p + nn * offset
        face = -nn * np.sign(offset) if facing_in else nn * np.sign(offset)
        for y, v in ((0.0, 0.0), (height, 1.0)):
            verts.append((q[0], y, q[1]))
            uvs.append((d / v_tile, v))
            norms.append((face[0], 0.0, face[1]))
    for i in range(n):
        a, b, c, d = 2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 3
        if offset > 0:
            faces += [(a, b, c), (b, d, c)]
        else:
            faces += [(a, c, b), (b, c, d)]
    return verts, uvs, norms, faces


def make_track():
    pts, normals, dist, total = centre_line()
    write_obj("Models/road.obj", *ribbon(pts, normals, dist, -HALF_WIDTH, HALF_WIDTH, 0.0, 12.0), "trassa asfalti")
    # kerbs on both sides (one mesh: two strips)
    lv, lu, ln, lf = ribbon(pts, normals, dist, HALF_WIDTH, HALF_WIDTH + KERB, 0.02, 4.0)
    rv, ru, rn, rf = ribbon(pts, normals, dist, -HALF_WIDTH, -HALF_WIDTH - KERB, 0.02, 4.0)
    off = len(lv)
    write_obj("Models/kerbs.obj", lv + rv, lu + ru, ln + rn, lf + [(a + off, b + off, c + off) for a, b, c in rf], "trassa bordyurlari")
    # barriers: both sides, faces towards the road
    lv, lu, ln, lf = wall(pts, normals, dist, WALL, 0.9, 6.0, True)
    rv, ru, rn, rf = wall(pts, normals, dist, -WALL, 0.9, 6.0, True)
    off = len(lv)
    write_obj("Models/barriers.obj", lv + rv, lu + ru, ln + rn, lf + [(a + off, b + off, c + off) for a, b, c in rf], "trassa to'siqlari")
    return pts, normals, dist, total

# ------------------------------------------------------------------------------------------- textures


def save_texture(rel, img):
    k.write_png(os.path.join(ASSETS, rel), np.clip(img, 0, 1))
    k.write_zmeta(ASSETS, rel, "Texture", {}, NAME)


def make_textures():
    s = 256
    ys, xs = np.mgrid[0:s, 0:s].astype(np.float64)
    u, v = (xs + 0.5) / s, (ys + 0.5) / s
    # asphalt: grainy dark grey, white edge lines, a dashed centre line (v: 12 m)
    n = k.value_noise(s, s, 32, seed=1, octaves=3)
    n2 = k.value_noise(s, s, 4, seed=2, octaves=3)
    img = k.image(s, s, (0, 0, 0, 1))
    for i, c in enumerate((0.2, 0.21, 0.23)):
        img[:, :, i] = c * (0.75 + 0.35 * n + 0.15 * n2)
    edge = k.smooth(0.012, 0.004, np.abs(np.minimum(u, 1 - u) - 0.04))
    centre = k.smooth(0.008, 0.003, np.abs(u - 0.5)) * (v % 1.0 < 0.5)
    paint = np.clip(edge + centre, 0, 1)
    for i in range(3):
        img[:, :, i] = img[:, :, i] * (1 - paint) + 0.9 * paint
    save_texture("Textures/asphalt.png", img)
    # kerb: red / white blocks (v: 4 m - two blocks)
    img = k.image(s, s, (0, 0, 0, 1))
    red = (v % 1.0) < 0.5
    for i in range(3):
        img[:, :, i] = np.where(red, (0.85, 0.12, 0.1)[i], 0.92) * (0.9 + 0.1 * n)
    save_texture("Textures/kerb.png", img)
    # barrier: blue / white panels with a dark rail at the top
    img = k.image(s, s, (0, 0, 0, 1))
    panel = (u % 1.0) < 0.5
    for i in range(3):
        img[:, :, i] = np.where(panel, (0.12, 0.35, 0.85)[i], 0.95)
        img[:, :, i] = np.where(v > 0.86, 0.25, img[:, :, i])
        img[:, :, i] = np.where(v < 0.08, 0.15, img[:, :, i])
    save_texture("Textures/barrier.png", img)
    # grass: mown stripes
    n = k.value_noise(s, s, 16, seed=4, octaves=4)
    stripes = ((xs // 64) % 2).astype(np.float64)
    img = k.image(s, s, (0, 0, 0, 1))
    for i, c in enumerate((0.3, 0.55, 0.2)):
        img[:, :, i] = c * (0.8 + 0.25 * n) * (0.92 + 0.12 * stripes)
    save_texture("Textures/grass.png", img)
    # checker (the start line and the gantry)
    img = k.image(s, s, (0, 0, 0, 1))
    check = (((xs // 32) + (ys // 32)) % 2).astype(np.float64)
    for i in range(3):
        img[:, :, i] = 0.08 + 0.85 * check
    save_texture("Textures/checker.png", img)
    # wheel: a tyre with a five-spoke rim (the cylinder's caps)
    x, y = k.coords(s, s)
    r = np.hypot(x + 0.5, y + 0.5) / 128.0
    ang = np.arctan2(y, x)
    img = k.image(s, s, (0, 0, 0, 1))
    tyre = r > 0.68
    spokes = (np.cos(ang * 5) > 0.55) & (r < 0.62)
    rim = (np.abs(r - 0.63) < 0.05) | (r < 0.15) | spokes
    for i in range(3):
        img[:, :, i] = np.where(tyre, 0.06, np.where(rim, (0.8, 0.82, 0.86)[i], 0.12))
    save_texture("Textures/wheel.png", img)


def make_ui_atlas():
    atlas = k.Atlas(512, 256)
    k.ui_panel_sprites(atlas, (0.08, 0.1, 0.14), (0.03, 0.04, 0.06), (1.0, 0.35, 0.2), (1.0, 0.35, 0.2))
    return atlas.save(ASSETS, ATLAS, NAME)


def make_icon():
    size = 256
    img = k.image(size, size)
    x, y = k.coords(size, size)
    sd = k.sd_round_box(x, y, 120, 120, 44)
    bg = k.mix_color((0.25, 0.75, 0.35), (0.05, 0.3, 0.15), np.clip(y / size + 0.5, 0, 1))
    cov = k.smooth(0.5, -0.5, sd)
    for i in range(3):
        img[:, :, i] = bg[:, :, i]
    img[:, :, 3] = cov
    # a checkered flag and a red car silhouette
    flag = (((np.floor((x + 90) / 22) + np.floor((y + 90) / 22)) % 2) == 0)
    k.fill(img, k.sd_round_box(x + 20, y + 30, 70, 50, 4) + 0, (0.1, 0.1, 0.1, 1.0))
    inside = k.sd_round_box(x + 20, y + 30, 70, 50, 4) < 0
    for i in range(3):
        img[:, :, i] = np.where(inside & flag, 0.95, img[:, :, i])
    k.fill(img, k.sd_round_box(x - 20, y - 60, 80, 22, 10), (0.9, 0.2, 0.1, 1.0))
    k.fill(img, k.sd_round_box(x - 30, y - 82, 40, 14, 8), (0.9, 0.2, 0.1, 1.0))
    for wx in (-70, 30):
        k.fill(img, k.sd_circle(x - wx + 0 * x, y - 84 + 0 * y, 0) + k.sd_circle(x - (wx + 0), y - 84, 15), (0.08, 0.08, 0.08, 1.0))
    img[:, :, 3] = np.minimum(img[:, :, 3], cov)
    save_texture("Textures/GameIcon.png", img)

# --------------------------------------------------------------------------------------------- sounds


def make_sounds():
    R = k.RATE
    out = {}
    # engine loop (1 s, seamless: whole cycles), its pitch follows the revs
    seconds = 1.0
    t = k.t_axis(seconds)
    base = 55.0
    eng = (np.sin(2 * np.pi * base * t) * 0.6 + np.sin(2 * np.pi * base * 2 * t) * 0.35 + np.sin(2 * np.pi * base * 3 * t) * 0.2 +
           np.sign(np.sin(2 * np.pi * base * 0.5 * t)) * 0.15)
    rumble = k.lowpass(k.noise(seconds, 31), 300) * 0.4
    out["engine"] = k.lowpass(eng + rumble * (0.7 + 0.3 * np.sin(2 * np.pi * 8 * t)), 1800)
    # skid loop: a band of noise
    n = int(1.0 * R)
    out["skid"] = k.highpass(k.lowpass(k.noise(1.0, 32), 2600), 900) * (0.8 + 0.2 * np.sin(2 * np.pi * 6 * k.t_axis(1.0)))
    n = int(0.6 * R)
    out["hit"] = k.lowpass(k.noise(0.6, 33), np.linspace(3000, 300, n)) * k.exp_decay(n, 0.12) + \
        k.osc(np.linspace(140, 60, n), 0.6, "sine") * k.exp_decay(n, 0.1)
    n = int(0.3 * R)
    out["beep"] = k.osc(660, 0.3, "square") * k.env(n, 0.005, 0.05, 0.6, 0.18, 0.06) * 0.5
    n = int(0.7 * R)
    out["go"] = k.osc(1320, 0.7, "square") * k.env(n, 0.005, 0.05, 0.7, 0.5, 0.1) * 0.5
    buf = np.zeros(int(0.6 * R))
    for i, note in enumerate(("A5", "E6")):
        k.place(buf, k.osc(k.note_freq(note), 0.4, "sine") * k.exp_decay(int(0.4 * R), 0.12), i * 0.06, 0.6)
    out["checkpoint"] = buf
    buf = np.zeros(int(1.2 * R))
    for i, note in enumerate(("C5", "E5", "G5", "C6")):
        k.place(buf, k.osc(k.note_freq(note), 0.5, "square") * k.exp_decay(int(0.5 * R), 0.2), i * 0.09, 0.35)
    out["lap"] = k.lowpass(buf, 5000)
    buf = np.zeros(int(2.4 * R))
    for i, (note, at) in enumerate((("G4", 0), ("C5", 0.15), ("E5", 0.3), ("G5", 0.45), ("E5", 0.65), ("G5", 0.8), ("C6", 0.95))):
        tone = k.osc(k.note_freq(note), 0.9, "saw") * 0.3 + k.osc(k.note_freq(note) * 2, 0.9, "sine") * 0.25
        k.place(buf, k.lowpass(tone, 4000) * k.exp_decay(int(0.9 * R), 0.35), at, 0.5)
    out["finish"] = buf
    n = int(0.06 * R)
    out["ui_click"] = k.osc(1100, 0.06, "square") * k.exp_decay(n, 0.012) * 0.6 + k.osc(2200, 0.06, "sine") * k.exp_decay(n, 0.006) * 0.4
    for name, snd in out.items():
        rel = f"Audio/{name}.wav"
        k.write_wav(os.path.join(ASSETS, rel), snd)
        k.write_zmeta(ASSETS, rel, "Audio", {"LoadMode": "Decompress", "Preload": True, "Volume": 1.0}, NAME)
    # music: 128 bpm, 15 s - kick, hats, a bass line and a synth hook
    seconds = 15.0
    total = int(seconds * R)
    buf = np.zeros(total + R)
    beat = 60.0 / 128.0
    kick = k.osc(np.linspace(140, 45, int(0.25 * R)), 0.25, "sine") * k.exp_decay(int(0.25 * R), 0.07)
    hat = k.highpass(k.noise(0.05, 41), 6000) * k.exp_decay(int(0.05 * R), 0.01) * 0.4
    bass_notes = ["A1", "A1", "C2", "D2", "A1", "A1", "G1", "E1"]
    i = 0
    tm = 0.0
    while tm < seconds:
        k.place(buf, kick, tm, 0.8)
        k.place(buf, hat, tm + beat / 2, 0.6)
        f = k.note_freq(bass_notes[(i // 2) % len(bass_notes)])
        bass = k.lowpass(k.osc(f, beat * 0.9, "saw"), 500) * k.env(int(beat * 0.9 * R), 0.005, 0.1, 0.6, beat * 0.5, 0.1)
        k.place(buf, bass, tm, 0.45)
        if i % 4 == 2:
            hook = ["E4", "G4", "A4", "C5"][(i // 4) % 4]
            k.place(buf, k.lowpass(k.osc(k.note_freq(hook), beat, "square"), 2500) * k.exp_decay(int(beat * R), beat * 0.4), tm, 0.18)
        tm += beat
        i += 1
    buf[:R] += buf[total:total + R]
    rel = "Audio/music.wav"
    k.write_wav(os.path.join(ASSETS, rel), buf[:total], peak=0.55)
    k.write_zmeta(ASSETS, rel, "Audio", {"LoadMode": "Stream", "Preload": False, "Volume": 1.0}, NAME)

# ---------------------------------------------------------------------------------------------- scene


def car(s, name, pos, yaw, paint, number_col, sounds=False):
    root = s.entity(name, pos=pos, rot=(0, yaw, 0))
    body = s.entity("Kuzov", parent=root)
    s.entity("Shassi", parent=body, pos=(0, 0.52, 0), scale=(1.9, 0.42, 4.1), MeshRenderer=k.mesh("Cube", paint, 0.25, 0.6))
    s.entity("Kapot", parent=body, pos=(0, 0.75, -1.25), rot=(0.12, 0, 0), scale=(1.8, 0.12, 1.4), MeshRenderer=k.mesh("Cube", paint, 0.25, 0.6))
    s.entity("Salon", parent=body, pos=(0, 0.92, 0.35), scale=(1.55, 0.44, 1.9),
             MeshRenderer=k.mesh("Cube", (0.06, 0.08, 0.12, 1), 0.08, 0.9))
    s.entity("Tom", parent=body, pos=(0, 1.15, 0.4), scale=(1.4, 0.06, 1.5), MeshRenderer=k.mesh("Cube", paint, 0.25, 0.6))
    s.entity("Chiziq", parent=body, pos=(0, 1.19, 0.4), scale=(0.3, 0.02, 1.5), MeshRenderer=k.mesh("Cube", number_col, 0.3, 0.2))
    s.entity("Chiziq 2", parent=body, pos=(0, 0.82, -1.25), rot=(0.12, 0, 0), scale=(0.3, 0.02, 1.4), MeshRenderer=k.mesh("Cube", number_col, 0.3, 0.2))
    s.entity("Qanot", parent=body, pos=(0, 1.22, 1.85), scale=(1.85, 0.06, 0.42), MeshRenderer=k.mesh("Cube", (0.08, 0.08, 0.1, 1), 0.4, 0.5))
    for side in (-1, 1):
        s.entity("Qanot ustuni", parent=body, pos=(side * 0.6, 0.98, 1.85), scale=(0.06, 0.4, 0.2), MeshRenderer=k.mesh("Cube", (0.08, 0.08, 0.1, 1), 0.4, 0.5))
        s.entity("Fara", parent=body, pos=(side * 0.6, 0.6, -2.06), scale=(0.35, 0.12, 0.04),
                 MeshRenderer=k.mesh("Cube", (1, 1, 0.9, 1), emission=(1.0, 0.95, 0.8), emission_strength=4.0, cast_shadows=False))
    brake = s.entity("Stop chiroqlari", parent=body, pos=(0, 0.62, 2.06), scale=(1.5, 0.1, 0.04),
                     MeshRenderer=k.mesh("Cube", (1, 0.1, 0.05, 1), emission=(1.0, 0.08, 0.04), emission_strength=1.6, cast_shadows=False))
    wheels = {}
    for key, (wx, wz) in (("FL", (-0.98, -1.35)), ("FR", (0.98, -1.35)), ("RL", (-0.98, 1.35)), ("RR", (0.98, 1.35))):
        pivot = s.entity("G'ildirak o'qi " + key, parent=root, pos=(wx, 0.38, wz))
        wheel = s.entity("G'ildirak " + key, parent=pivot, rot=(0, 0, math.radians(90)), scale=(0.76, 0.32, 0.76),
                         MeshRenderer=k.mesh("Cylinder", (1, 1, 1, 1), 0.6, 0.2, albedo_map="Textures/wheel.png"))
        wheels[key] = (pivot, wheel)
    smoke = s.entity("Tutun", parent=root, pos=(0, 0.25, 1.5), ParticleSystem=k.particles(
        EmissionRate=0.0, MaxParticles=500, Shape=3, ShapeExtents=(1.0, 0.1, 0.2), LifetimeMin=0.8, LifetimeMax=1.6, SpeedMin=0.5,
        SpeedMax=1.5, Direction=(0, 1, 0.3), Spread=40.0, Drag=1.2, StartColor=(0.85, 0.85, 0.88, 0.45), EndColor=(0.8, 0.8, 0.82, 0.0),
        StartSize=0.6, EndSize=2.2, Additive=False))
    dust = s.entity("Chang", parent=root, pos=(0, 0.2, 1.6), ParticleSystem=k.particles(
        EmissionRate=0.0, MaxParticles=300, Shape=3, ShapeExtents=(1.0, 0.1, 0.2), LifetimeMin=0.6, LifetimeMax=1.2, SpeedMin=1.0,
        SpeedMax=2.5, Direction=(0, 1, 0.5), Spread=40.0, Gravity=(0, -3, 0), Drag=1.0, StartColor=(0.5, 0.42, 0.25, 0.6),
        EndColor=(0.45, 0.4, 0.3, 0.0), StartSize=0.4, EndSize=1.4, Additive=False))
    if sounds:
        s.add(root, "AudioSource", k.audio_source("Audio/engine.wav", 0.5, loop=True, play=True))
        s.entity("Sirpanish ovozi", parent=root, AudioSource=k.audio_source("Audio/skid.wav", 0.0, loop=True, play=True))
    fields = dict(Body=k.ref(body), WheelFL=k.ref(wheels["FL"][1]), WheelFR=k.ref(wheels["FR"][1]), WheelRL=k.ref(wheels["RL"][1]),
                  WheelRR=k.ref(wheels["RR"][1]), SteerFL=k.ref(wheels["FL"][0]), SteerFR=k.ref(wheels["FR"][0]), BrakeLights=k.ref(brake),
                  Smoke=k.ref(smoke), Dust=k.ref(dust))
    return root, fields


def build_scene(pts, normals, dist, total):
    s = k.SceneBuilder("Valley", 8860000000000000)
    s.settings = {
        "DrawSky": True, "SkyTop": [0.2, 0.45, 0.85], "SkyHorizon": [0.75, 0.85, 0.95], "Ground": [0.35, 0.45, 0.3],
        "Ambient": 0.55, "Exposure": 1.0, "ShadowDistance": 80,
        "Sky": {"Curve": 1.0, "GroundFade": 1.0, "SunDisc": True, "SunSize": 1.0, "SunIntensity": 1.0, "SunGlow": 0.8},
        "Fog": {"Enabled": True, "Color": [0.7, 0.8, 0.92], "Density": 0.0028, "HeightFalloff": 0.02},
        "Post": {"Tonemap": 1, "Bloom": True, "BloomThreshold": 1.0, "BloomKnee": 0.5, "BloomIntensity": 0.45, "BloomRadius": 1.0,
                 "Vignette": 0.22, "Saturation": 1.15, "Contrast": 1.05, "Brightness": 0.0, "Tint": [1, 1, 1], "SSAO": True,
                 "SSAORadius": 0.6, "SSAOIntensity": 1.0, "SSAOBias": 0.03, "FXAA": True},
        "Gravity3D": [0, -9.81, 0],
    }
    s.metadata = {"Description": "Vodiy Poygasi: mashina fizikasi (tezlanish, rul, ilashish va drift), aylanma trassa, nazorat "
                                 "nuqtalari, aylana vaqti, 3 ta bot raqib", "Author": "Zamin", "Tags": ["namuna", "3D", "racing"]}
    s.entity("Quyosh", rot=(math.radians(-48), math.radians(30), 0), DirectionalLight=k.dir_light((1.0, 0.95, 0.85), 3.2, True))
    camera = s.entity("Kamera", pos=(0, 4, 75), rot=(math.radians(-12), 0, 0),
                      Camera=k.camera_persp(math.radians(58), 0.1, 900.0, (0.6, 0.75, 0.9, 1.0),
                                            shake={"MaxOffset": [0.2, 0.2], "MaxAngle": 3.0, "Exponent": 2.0}),
                      AudioListener={"Enabled": True, "Priority": 0, "TrackVelocity": True})

    # ---- the track
    tr = s.entity("Trassa")
    s.entity("O't", parent=tr, pos=(70, -0.04, 5), scale=(900, 1, 900),
             MeshRenderer=k.mesh("Plane", (1, 1, 1, 1), 0.95, 0.0, albedo_map="Textures/grass.png", tiling=(110, 110)))
    s.entity("Asfalt", parent=tr, MeshRenderer=k.mesh("Model", (1, 1, 1, 1), 0.75, 0.0, albedo_map="Textures/asphalt.png", model="Models/road.obj"))
    s.entity("Bordyurlar", parent=tr, MeshRenderer=k.mesh("Model", (1, 1, 1, 1), 0.6, 0.0, albedo_map="Textures/kerb.png", model="Models/kerbs.obj"))
    barrier = s.entity("To'siqlar", parent=tr, MeshRenderer=k.mesh("Model", (1, 1, 1, 1), 0.5, 0.1, albedo_map="Textures/barrier.png",
                                                                     model="Models/barriers.obj"))
    s.get(barrier)["MeshRenderer"]["DoubleSided"] = True
    # the start line, the gantry with the start lights
    p0, n0 = pts[0], normals[0]
    yaw0 = math.atan2(-(pts[1][0] - pts[0][0]), -(pts[1][1] - pts[0][1]))
    s.entity("Start chizig'i", parent=tr, pos=(p0[0], 0.015, p0[1]), rot=(0, yaw0, 0), scale=(HALF_WIDTH * 2, 1, 1.6),
             MeshRenderer=k.mesh("Plane", (1, 1, 1, 1), 0.6, 0.0, albedo_map="Textures/checker.png", tiling=(6, 1), cast_shadows=False))
    gantry = s.entity("Start darvozasi", parent=tr, pos=(p0[0], 0, p0[1]), rot=(0, yaw0, 0))
    for side in (-1, 1):
        s.entity("Ustun", parent=gantry, pos=(side * (WALL + 0.6), 3.5, 0), scale=(0.7, 7, 0.7), MeshRenderer=k.mesh("Cube", (0.85, 0.87, 0.9, 1), 0.4, 0.6))
    s.entity("To'sin", parent=gantry, pos=(0, 6.6, 0), scale=(2 * WALL + 2, 1.2, 0.8), MeshRenderer=k.mesh("Cube", (1, 1, 1, 1), 0.5, 0.2,
                                                                                                         albedo_map="Textures/checker.png", tiling=(12, 1)))
    lights = s.entity("Start chiroqlari", parent=gantry, pos=(0, 5.6, -0.42))
    for i in range(5):
        s.entity(f"Chiroq {i + 1}", parent=lights, pos=(-2.4 + i * 1.2, 0, 0), scale=(0.6, 0.6, 0.3),
                 MeshRenderer=k.mesh("Sphere", (0.08, 0.08, 0.09, 1), 0.3, 0.0, emission=(1.0, 0.1, 0.05), emission_strength=0.0, cast_shadows=False))
    s.entity("Chiroqlar qutisi", parent=gantry, pos=(0, 5.6, -0.25), scale=(6.6, 1.0, 0.3), MeshRenderer=k.mesh("Cube", (0.05, 0.05, 0.06, 1), 0.5, 0.3))
    # checkpoints: glowing posts on both sides
    cps = s.entity("Nazorat nuqtalari", parent=tr)
    for c in range(1, CHECKPOINTS):
        sd = total * c / CHECKPOINTS
        idx = int(np.argmin(np.abs(dist - sd)))
        p, nn = pts[idx], normals[idx]
        gate = s.entity(f"Nuqta {c}", parent=cps)
        for side in (-1, 1):
            q = p + nn * side * (WALL - 0.4)
            s.entity("Ustun", parent=gate, pos=(q[0], 1.3, q[1]), scale=(0.35, 2.6, 0.35),
                     MeshRenderer=k.mesh("Cylinder", (1.0, 0.6, 0.15, 1), 0.3, 0.2, emission=(1.0, 0.55, 0.1), emission_strength=1.2, cast_shadows=False))
    # the grandstand by the main straight
    stand = s.entity("Tribuna", pos=(-20, 0, 0))
    for i in range(4):
        s.entity(f"Pog'ona {i + 1}", parent=stand, pos=(-i * 1.4, 0.5 + i * 0.9, 0), scale=(1.4, 1.0 + i * 1.8, 60),
                 MeshRenderer=k.mesh("Cube", (0.75, 0.77, 0.8, 1), 0.7, 0.0))
    rng = random.Random(7)
    for i in range(70):
        row = rng.randint(0, 3)
        s.entity("Tomoshabin", parent=stand, pos=(-row * 1.4, 1.15 + row * 1.8, rng.uniform(-28, 28)), scale=(0.5, 0.7, 0.5),
                 MeshRenderer=k.mesh("Cube", rng.choice([(0.9, 0.2, 0.15, 1), (0.15, 0.4, 0.9, 1), (0.95, 0.8, 0.2, 1), (0.95, 0.95, 0.95, 1),
                                                          (0.2, 0.7, 0.3, 1)]), 0.8, 0.0, cast_shadows=False))
    s.entity("Soyabon", parent=stand, pos=(-2, 7.2, 0), rot=(0, 0, math.radians(-10)), scale=(7, 0.25, 62),
             MeshRenderer=k.mesh("Cube", (0.9, 0.25, 0.15, 1), 0.5, 0.2))
    # trees on the grass (never near the track), hills and mountains far away
    nature = s.entity("Tabiat")
    placed = 0
    tries = 0
    while placed < 70 and tries < 4000:
        tries += 1
        x, z = rng.uniform(-80, 220), rng.uniform(-200, 210)
        d = np.min(np.hypot(pts[:, 0] - x, pts[:, 1] - z))
        if d < WALL + 4 or (-34 < x < -8 and -32 < z < 32):
            continue
        h = rng.uniform(4, 8)
        tree = s.entity("Daraxt", parent=nature, pos=(x, 0, z), rot=(0, rng.uniform(0, 6.28), 0))
        s.entity("Tana", parent=tree, pos=(0, h * 0.25, 0), scale=(0.35, h * 0.5, 0.35), MeshRenderer=k.mesh("Cylinder", (0.4, 0.28, 0.18, 1), 0.9, 0.0))
        if rng.random() < 0.5:
            s.entity("Barglar", parent=tree, pos=(0, h * 0.62, 0), scale=(h * 0.42, h * 0.75, h * 0.42),
                     MeshRenderer=k.mesh("Cone", (0.12, 0.38, 0.16, 1), 0.85, 0.0))
        else:
            s.entity("Barglar", parent=tree, pos=(0, h * 0.62, 0), scale=(h * 0.55, h * 0.5, h * 0.55),
                     MeshRenderer=k.mesh("Sphere", (0.25, 0.5, 0.18, 1), 0.85, 0.0))
        placed += 1
    for i in range(14):
        a = i * 2 * math.pi / 14 + rng.uniform(-0.1, 0.1)
        r = rng.uniform(420, 520)
        h = rng.uniform(90, 170)
        cx, cz = 70 + math.cos(a) * r, 5 + math.sin(a) * r
        m = s.entity("Tog'", parent=nature, pos=(cx, h * 0.45, cz), scale=(h * 1.6, h, h * 1.6), MeshRenderer=k.mesh("Cone", (0.42, 0.45, 0.5, 1), 0.9, 0.0,
                                                                                                                     cast_shadows=False))
        s.entity("Qor", parent=m, pos=(0, 0.33, 0), scale=(0.36, 0.34, 0.36), MeshRenderer=k.mesh("Cone", (0.95, 0.97, 1.0, 1), 0.6, 0.0, cast_shadows=False))
    for i in range(8):
        x, z = rng.uniform(-150, 300), rng.uniform(-260, 280)
        if np.min(np.hypot(pts[:, 0] - x, pts[:, 1] - z)) < 60:
            continue
        s.entity("Tepalik", parent=nature, pos=(x, -6, z), scale=(rng.uniform(60, 110), rng.uniform(16, 30), rng.uniform(60, 110)),
                 MeshRenderer=k.mesh("Sphere", (0.3, 0.5, 0.22, 1), 0.95, 0.0, cast_shadows=False))
    # tyre stacks on the outside of the bends
    for i in range(0, len(pts), 24):
        a, b = pts[i - 2], pts[(i + 2) % len(pts)]
        turn = (b[0] - pts[i][0]) * (pts[i][1] - a[1]) - (b[1] - pts[i][1]) * (pts[i][0] - a[0])
        if abs(turn) < 0.25:
            continue
        side = -1 if turn > 0 else 1
        q = pts[i] + normals[i] * side * (WALL + 1.0)
        stack = s.entity("Shinalar", parent=nature, pos=(q[0], 0, q[1]))
        for j in range(3):
            s.entity("Shina", parent=stack, pos=(0, 0.2 + j * 0.4, 0), scale=(1.0, 0.38, 1.0), MeshRenderer=k.mesh("Cylinder", (0.07, 0.07, 0.08, 1), 0.8, 0.0))

    # ---- the cars on the grid (the game puts them in place)
    tdir = pts[1] - pts[0]
    yaw = math.atan2(-tdir[0], -tdir[1])
    player, pf = car(s, "O'yinchi mashinasi", (p0[0] - 2.6, 0, p0[1] + 9), yaw, (0.95, 0.3, 0.08, 1), (0.98, 0.98, 0.98, 1), sounds=True)
    rivals = []
    for i, (paint, stripe, skill, line) in enumerate((((0.1, 0.35, 0.9, 1), (1, 0.85, 0.2, 1), 0.93, -1.5),
                                                      ((0.1, 0.65, 0.3, 1), (0.95, 0.95, 0.95, 1), 0.9, 1.2),
                                                      ((0.6, 0.15, 0.75, 1), (0.95, 0.95, 0.95, 1), 0.87, 0.0))):
        root, fields = car(s, f"Raqib {i + 1}", (p0[0] + 2.6 * (1 if i % 2 == 0 else -1), 0, p0[1] + 9 + 7 * ((i + 1) // 2)), yaw, paint, stripe)
        s.add(root, "NativeScript", k.script("Car", Skill=skill, LineOffset=line, **fields))
        rivals.append(root)
    s.add(player, "NativeScript", k.script("Car", Skill=1.0, LineOffset=0.0, **pf))

    # ---- effects
    sparks = s.entity("Uchqunlar", pos=(0, -20, 0), ParticleSystem=k.particles(
        Playing=True, EmissionRate=0.0, MaxParticles=500, Shape=2, ShapeRadius=0.3, LifetimeMin=0.2, LifetimeMax=0.6, SpeedMin=3.0,
        SpeedMax=9.0, Direction=(0, 1, 0), Spread=70.0, Gravity=(0, -12, 0), Drag=1.0, StartColor=(3.0, 2.0, 0.7, 1.0),
        EndColor=(1.0, 0.4, 0.1, 0.0), StartSize=0.08, EndSize=0.0))

    # ---- UI
    game = s.entity("Game")
    canvas = s.entity("UI", UITransform=k.ui_stretch(), UICanvas=k.ui_canvas())
    ui = k.UIKit(s, canvas, ATLAS, game, (0.85, 0.22, 0.12, 1.0), (1.0, 0.36, 0.22, 1.0), (0.6, 0.14, 0.08, 1.0))
    hud = s.entity("HUD", parent=canvas, UITransform=k.ui_stretch())
    s.entity("Yuqori fon", parent=hud, UITransform=k.ui_rect((0, 1), (1, 1), (0.5, 1), (0, 0), (0, 150)),
             UIImage=k.ui_image((0.0, 0.0, 0.03, 0.4), raycast=False))
    position = ui.text("O'rin", hud, "1/4", (0, 1), (0, 1), (50, -20), (400, 100), 88, (1.0, 0.85, 0.3, 1.0))
    lap = ui.text("Aylana", hud, "", (0, 1), (0, 1), (260, -48), (500, 50), 40)
    timer = ui.text("Vaqt", hud, "00:00.00", (0.5, 1), (0.5, 1), (0, -24), (600, 72), 62, h="Center")
    best = ui.text("Eng yaxshi", hud, "", (0.5, 1), (0.5, 1), (0, -96), (700, 36), 28, (1.0, 0.85, 0.5, 0.95), h="Center")
    checkpoint = ui.text("Nuqta", hud, "", (1, 1), (1, 1), (-50, -40), (500, 44), 36, (1.0, 0.7, 0.35, 1.0), h="Right")
    speed = ui.text("Tezlik", hud, "0", (1, 0), (1, 0), (-190, 62), (300, 110), 100, h="Right")
    ui.text("km/soat", hud, "km/soat", (1, 0), (1, 0), (-50, 76), (130, 40), 30, (0.85, 0.85, 0.9, 0.9), h="Right", key="hud.kmh")
    speed_bar = s.entity("Tezlik shkalasi", parent=hud, UITransform=k.ui_rect((1, 0), None, (1, 0), (-50, 40), (380, 14)),
                         UIProgressBar=k.ui_progress(0.0, fill=(1.0, 0.45, 0.15, 1.0), padding=2.0, smooth=12.0))
    drift = ui.text("Drift", hud, "", (0.5, 0), (0.5, 0), (0, 150), (800, 70), 60, (1.0, 0.55, 0.2, 1.0), h="Center")
    banner = ui.text("E'lon", hud, "", (0.5, 0.5), (0.5, 0.5), (0, 160), (1400, 140), 120, (1.0, 0.95, 0.85, 1.0), h="Center",
                     UIAnimation=k.ui_anim("Pop", "Fade", 0.3))
    banner_sub = ui.text("E'lon izohi", banner, "", (0.5, 0.0), (0.5, 1.0), (0, -4), (1200, 50), 40, (1.0, 0.75, 0.4, 1.0), h="Center")
    demo_badge = ui.text("Demo belgisi", hud, "DEMO - avtomatik o'yin", (1, 1), (1, 1), (-50, -90), (600, 32), 26, (1.0, 0.85, 0.5, 0.9),
                         h="Right", key="hud.demo", active=False)

    main_menu = ui.dim("Bosh menyu", True, 0.3, (0.0, 0.02, 0.05))
    title = s.entity("Sarlavha", parent=main_menu, UITransform=k.ui_rect(pos=(0, 290), size=(1700, 180), font=140),
                     UIAnimation=k.ui_anim("SlideDown", "None", 0.6, slide=120))
    s.entity("Sarlavha soyasi", parent=title, UITransform=k.ui_rect((0, 0), (1, 1), pos=(6, -7), size=(0, 0), font=140),
             Text=k.ui_text("VODIY POYGASI", (0.35, 0.05, 0.02, 0.9), "Center", key="menu.title"))
    s.entity("Sarlavha matni", parent=title, UITransform=k.ui_rect((0, 0), (1, 1), size=(0, 0), font=140),
             Text=k.ui_text("VODIY POYGASI", (1.0, 0.6, 0.3, 1.0), "Center", key="menu.title"))
    s.entity("Taglavha", parent=main_menu, UITransform=k.ui_rect(pos=(0, 180), size=(1500, 50), font=38),
             Text=k.ui_text(UZ["menu"]["subtitle"], (1.0, 0.95, 0.9, 0.95), "Center", key="menu.subtitle"))
    menu_panel = ui.panel("Menyu paneli", main_menu, (0, -90), (560, 330))
    play_button = ui.button("O'ynash", menu_panel, "O'ynash", "menu.play", "Play", True)
    language_button = ui.button("Til", menu_panel, "Til: O'zbekcha", None, "Language")
    ui.button("Chiqish", menu_panel, "Chiqish", "menu.quit", "Quit")
    menu_best = s.entity("Bosh menyu rekordi", parent=main_menu, UITransform=k.ui_rect(pos=(0, -300), size=(1200, 44), font=32),
                         Text=k.ui_text("", (1.0, 0.85, 0.45, 1.0), "Center"))
    s.entity("Boshqaruv", parent=main_menu, UITransform=k.ui_rect((0.5, 0), None, (0.5, 0), (0, 50), (1850, 70), font=25),
             Text=k.ui_text(UZ["menu"]["controls"], (0.95, 0.95, 1.0, 0.9), "Center", key="menu.controls"))

    pause_menu = ui.dim("Pauza menyusi", False)
    pause_panel = ui.panel("Pauza paneli", pause_menu, (0, 0), (620, 460))
    ui.label("Pauza sarlavhasi", pause_panel, "PAUZA", (520, 80), 60, (1.0, 0.6, 0.35, 1.0), "pause.title")
    resume_button = ui.button("Davom etish", pause_panel, "Davom etish", "pause.resume", "Resume", True)
    ui.button("Qaytadan", pause_panel, "Qaytadan", "pause.restart", "Restart")
    ui.button("Bosh menyuga", pause_panel, "Bosh menyu", "pause.menu", "Menu")

    results_menu = ui.dim("Natijalar", False, 0.45)
    results_panel = ui.panel("Natijalar paneli", results_menu, (0, 0), (780, 560))
    results_title = ui.label("Natija sarlavhasi", results_panel, "MARRA!", (700, 84), 64, (1.0, 0.75, 0.35, 1.0))
    results_place = ui.label("Natija o'rni", results_panel, "", (700, 64), 52, (1.0, 0.95, 0.8, 1.0))
    results_times = ui.label("Natija vaqtlari", results_panel, "", (700, 40), 30, (0.9, 0.95, 1.0, 1.0))
    results_record = ui.label("Natija rekordi", results_panel, "", (700, 40), 30, (1.0, 0.6, 0.3, 1.0))
    retry_button = ui.button("Yana poyga", results_panel, "Qaytadan", "pause.restart", "Restart", True)
    ui.button("Natijadan menyuga", results_panel, "Bosh menyu", "pause.menu", "Menu")

    track_points = ";".join(f"{p[0]:.2f},{p[1]:.2f}" for p in pts)
    s.add(game, "NativeScript", k.script(
        "RaceGame", PlayerCar=k.ref(player), Rival1=k.ref(rivals[0]), Rival2=k.ref(rivals[1]), Rival3=k.ref(rivals[2]), CameraRig=k.ref(camera),
        StartLights=k.ref(lights), Checkpoints=k.ref(cps), Sparks=k.ref(sparks), TrackPoints=track_points, Laps=3, CheckpointCount=CHECKPOINTS,
        Hud=k.ref(hud), PositionText=k.ref(position), LapText=k.ref(lap), TimerText=k.ref(timer), BestText=k.ref(best),
        CheckpointText=k.ref(checkpoint), SpeedText=k.ref(speed), SpeedBar=k.ref(speed_bar), DriftText=k.ref(drift), Banner=k.ref(banner),
        BannerSub=k.ref(banner_sub), DemoBadge=k.ref(demo_badge), MainMenu=k.ref(main_menu), MenuBest=k.ref(menu_best),
        PlayButton=k.ref(play_button), LanguageButton=k.ref(language_button), PauseMenu=k.ref(pause_menu), ResumeButton=k.ref(resume_button),
        ResultsMenu=k.ref(results_menu), ResultsTitle=k.ref(results_title), ResultsPlace=k.ref(results_place),
        ResultsTimes=k.ref(results_times), ResultsRecord=k.ref(results_record), RetryButton=k.ref(retry_button)))
    s.add(game, "AudioSource", k.audio_source("Audio/music.wav", 0.35, loop=True, play=True, output="Music", stream=True))
    return s


UZ = {
    "$name": "O'zbekcha",
    "menu": {"title": "VODIY POYGASI", "subtitle": "Mashina fizikasi va aylanma trassa  ·  Zamin Engine namunasi", "play": "O'ynash",
             "quit": "Chiqish", "language": "Til: {0}", "best": "Eng yaxshi aylana: {0}",
             "controls": "W / strelka yuqoriga - gaz   ·   S - tormoz / orqaga   ·   A / D - rul   ·   Space - qo'l tormozi (drift)   ·   "
                         "R - trassaga qaytish   ·   Esc - pauza\n"
                         "Gamepad: RT - gaz, LT - tormoz, chap stik - rul, X / A - qo'l tormozi, Y - trassaga qaytish, Start - pauza"},
    "hud": {"demo": "DEMO - avtomatik o'yin", "lap": "Aylana {0}/{1}", "checkpoint": "Nuqta {0}/{1}", "best": "Rekord: {0}", "kmh": "km/soat",
            "drift": "DRIFT!", "go": "KETDIK!", "go_sub": "{0} aylana - hammadan oldin keling", "lap_n": "{0}-AYLANA",
            "last_lap": "OXIRGI AYLANA!", "lap_time": "Aylana vaqti: {0}"},
    "pause": {"title": "PAUZA", "resume": "Davom etish", "restart": "Qaytadan", "menu": "Bosh menyu"},
    "results": {"title": "MARRA!", "win": "G'ALABA!", "place": "{0}-o'rin", "times": "Vaqt: {0}  ·  eng yaxshi aylana: {1}",
                "record": "Yangi aylana rekordi!", "best": "Rekord: {0}"},
}
EN = {
    "$name": "English",
    "menu": {"title": "VALLEY RACE", "subtitle": "Car physics and a circuit  ·  a Zamin Engine sample", "play": "Play", "quit": "Quit",
             "language": "Language: {0}", "best": "Best lap: {0}",
             "controls": "W / up - throttle   ·   S - brake / reverse   ·   A / D - steer   ·   Space - handbrake (drift)   ·   R - back on track   ·   "
                         "Esc - pause\n"
                         "Gamepad: RT - throttle, LT - brake, left stick - steer, X / A - handbrake, Y - back on track, Start - pause"},
    "hud": {"demo": "DEMO - autoplay", "lap": "Lap {0}/{1}", "checkpoint": "Checkpoint {0}/{1}", "best": "Record: {0}", "kmh": "km/h",
            "drift": "DRIFT!", "go": "GO!", "go_sub": "{0} laps - beat them all", "lap_n": "LAP {0}", "last_lap": "FINAL LAP!",
            "lap_time": "Lap time: {0}"},
    "pause": {"title": "PAUSED", "resume": "Resume", "restart": "Restart", "menu": "Main menu"},
    "results": {"title": "FINISH!", "win": "VICTORY!", "place": "Place {0}", "times": "Time: {0}  ·  best lap: {1}", "record": "New lap record!",
                "best": "Record: {0}"},
}


def main():
    pts, normals, dist, total = make_track()
    print(f"  trassa: {total:.0f} m, {len(pts)} nuqta")
    make_textures()
    make_ui_atlas()
    make_icon()
    make_sounds()
    k.write_json(os.path.join(ASSETS, "Localization/uz.json"), UZ)
    k.write_json(os.path.join(ASSETS, "Localization/en.json"), EN)
    scene = build_scene(pts, normals, dist, total)
    uuid = scene.save(ASSETS, "Scenes/Valley.zscene", NAME)
    actions = [
        ("Handbrake", ["Key:Space", "Gamepad:X", "Gamepad:A"]),
        ("Reset", ["Key:R", "Gamepad:Y"]),
        ("Pause", ["Key:Escape", "Key:P", "Gamepad:Start"]),
    ]
    axes = [
        k.axis("Steer", ["Key:A", "Key:Left"], ["Key:D", "Key:Right"], "LeftX", deadzone=0.15),
        k.axis("Throttle", [], ["Key:W", "Key:Up"], "RightTrigger", deadzone=0.05),
        k.axis("Brake", [], ["Key:S", "Key:Down"], "LeftTrigger", deadzone=0.05),
    ]
    k.write_project(PROJECT, NAME, "Vodiy Poygasi", "Scenes/Valley.zscene", uuid, "1.0.0", [("Scenes/Valley.zscene", uuid)], actions, axes,
                    description="3D poyga: mashina fizikasi (rul, ilashish, drift), aylanma trassa, nazorat nuqtalari, aylana vaqti, 3 ta raqib")
    k.write_module_cmake(PROJECT, NAME, "Zamin namuna o'yini: Vodiy Poygasi (racing) - C++ skriptlar moduli (libRacing.so)")
    print("Racing: tayyor")


if __name__ == "__main__":
    main()
