"""Deterministic binary glTF (.glb) writer for the 3D vertical slice (gen_beacon_pass.py).

Static props are lists of primitives (one material each) built from revolved profiles, tapered tubes, ellipsoids and
boxes. Humanoids share one 21-joint skeleton (Hips -> Spine -> Chest -> Neck -> Head; Chest -> Shoulder/UpperArm/LowerArm/
Hand x2; Hips -> UpperLeg/LowerLeg/Foot/Toe x2), face -Z (the engine's forward), stand on y = 0 and are skinned with one or
two joints per vertex. Their clips are sampled from procedural gait functions (Idle, Walk, Run, Fall, Land, Attack, Cast,
Hurt, Death); walk/run lengths come from the stride so the Animator's blend thresholds equal the real speeds.
No clock, no randomness without a fixed seed: the same arguments write the same bytes.
"""
import json
import math
import os
import struct

import numpy as np

import samplekit as k

# ------------------------------------------------------------------------------------------------ geometry


class Part:
    """Vertices of one material: positions, normals, uvs, up to two joints with weights, triangle indices."""

    def __init__(self):
        self.p, self.n, self.uv, self.j, self.w, self.i = [], [], [], [], [], []
        self.count = 0

    def add(self, p, n, uv, tris, joints=None, weights=None):
        p = np.asarray(p, np.float64).reshape(-1, 3)
        n = np.asarray(n, np.float64).reshape(-1, 3)
        uv = np.asarray(uv, np.float64).reshape(-1, 2)
        length = np.linalg.norm(n, axis=1, keepdims=True)
        n = n / np.where(length > 1e-9, length, 1)
        if joints is None:
            joints = np.zeros((len(p), 2), np.int64)
            weights = np.tile([1.0, 0.0], (len(p), 1))
        self.p.append(p)
        self.n.append(n)
        self.uv.append(uv)
        self.j.append(np.asarray(joints, np.int64).reshape(-1, 2))
        self.w.append(np.asarray(weights, np.float64).reshape(-1, 2))
        self.i.append(np.asarray(tris, np.int64).reshape(-1, 3) + self.count)
        self.count += len(p)

    def arrays(self):
        return (np.concatenate(self.p), np.concatenate(self.n), np.concatenate(self.uv), np.concatenate(self.j), np.concatenate(self.w),
                np.concatenate(self.i))


def _frame(axis):
    axis = np.asarray(axis, np.float64)
    axis = axis / np.linalg.norm(axis)
    helper = np.array([0.0, 0.0, 1.0]) if abs(axis[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    u = np.cross(axis, helper)
    u /= np.linalg.norm(u)
    v = np.cross(axis, u)
    return u, v, axis


def revolve(profile, base=(0, 0, 0), axis=(0, 1, 0), seg=14, scale=(1.0, 1.0), phase=0.0, uv_scale=(1.0, 1.0)):
    """A surface of revolution: profile = [(radius, height), ...] along `axis` from `base`; normals from the profile
    tangent; `scale` squashes the cross-section (x along the frame's first axis, y along its second).
    Returns positions, normals, uvs, triangles and the per-vertex height fraction (0 at the first profile point)."""
    u, v, a = _frame(axis)
    base = np.asarray(base, np.float64)
    prof = np.asarray(profile, np.float64)
    rings = len(prof)
    heights = prof[:, 1]
    span = max(1e-9, heights.max() - heights.min())
    # profile normals (2D: outward from the tangent)
    tang = np.zeros_like(prof)
    tang[1:-1] = prof[2:] - prof[:-2]
    tang[0] = prof[1] - prof[0]
    tang[-1] = prof[-1] - prof[-2]
    pn = np.stack([tang[:, 1], -tang[:, 0]], axis=1)
    pn /= np.maximum(np.linalg.norm(pn, axis=1, keepdims=True), 1e-9)
    P, N, UV, F = [], [], [], []
    for r in range(rings):
        rad, h = prof[r]
        for s in range(seg + 1):
            ang = phase + 2 * math.pi * s / seg
            c, sn = math.cos(ang), math.sin(ang)
            P.append(base + a * h + (u * c * scale[0] + v * sn * scale[1]) * rad)
            N.append(a * pn[r, 1] + (u * c / max(scale[0], 1e-6) + v * sn / max(scale[1], 1e-6)) * pn[r, 0])
            UV.append((s / seg * uv_scale[0], (h - heights.min()) / span * uv_scale[1]))
            F.append((h - heights.min()) / span)
    T = []
    for r in range(rings - 1):
        for s in range(seg):
            a0 = r * (seg + 1) + s
            b0 = a0 + seg + 1
            T += [(a0, a0 + 1, b0), (a0 + 1, b0 + 1, b0)]
    return np.array(P), np.array(N), np.array(UV), np.array(T), np.array(F)


def capsule_profile(r0, r1, length, cap=6, side=4):
    """Profile of a tapered capsule along its axis: a hemisphere of r0 at 0, a cone to r1 at `length`, a hemisphere."""
    prof = []
    for i in range(cap + 1):
        t = -math.pi / 2 + (math.pi / 2) * i / cap
        prof.append((r0 * math.cos(t), r0 * math.sin(t)))
    for i in range(1, side):
        f = i / side
        prof.append((r0 + (r1 - r0) * f, length * f))
    for i in range(cap + 1):
        t = (math.pi / 2) * i / cap
        prof.append((r1 * math.cos(t), length + r1 * math.sin(t)))
    prof[0] = (1e-4, prof[0][1])
    prof[-1] = (1e-4, prof[-1][1])
    return prof


def ellipsoid_profile(ry, rings=8):
    return [(max(1e-4, math.sin(math.pi * i / rings)), -ry * math.cos(math.pi * i / rings)) for i in range(rings + 1)]


def box(center, half, rot_y=0.0):
    """A box with flat normals (24 vertices), optional yaw."""
    cx, cy, cz = center
    hx, hy, hz = half
    faces = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)), ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
             ((0, -1, 0), (1, 0, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
    P, N, UV, T = [], [], [], []
    c, s = math.cos(rot_y), math.sin(rot_y)
    rot = lambda q: np.array([q[0] * c + q[2] * s, q[1], -q[0] * s + q[2] * c])
    for normal, tu, tv in faces:
        n = np.array(normal, np.float64)
        u = np.array(tu, np.float64)
        v = np.array(tv, np.float64)
        ext = np.array([hx, hy, hz])
        start = len(P)
        for du, dv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            q = n * ext + u * ext * du + v * ext * dv
            P.append(rot(q) + np.array([cx, cy, cz]))
            N.append(rot(n))
            UV.append(((du + 1) / 2 * abs(u @ ext) * 2, (dv + 1) / 2 * abs(v @ ext) * 2))
        T += [(start, start + 1, start + 2), (start, start + 2, start + 3)]
    return np.array(P), np.array(N), np.array(UV), np.array(T)


def tube(part, a, b, r0, r1, joints, blend=(0.0, 0.0), seg=12, cap=5, uv=(1, 1)):
    """A tapered capsule from a to b bound to joints[0]; the last blend[1] of its length shares up to 50 % with joints[1]
    (elbows, knees: the neighbouring part does the same from its side, so the joint bends without a gap)."""
    a = np.asarray(a, np.float64)
    b = np.asarray(b, np.float64)
    length = float(np.linalg.norm(b - a))
    P, N, UV, T, F = revolve(capsule_profile(r0, r1, length, cap), a, b - a, seg, uv_scale=uv)
    t = np.clip((((P - a) @ ((b - a) / length)) / length), 0, 1)
    j0, j1 = joints
    w1 = np.zeros(len(P))
    if blend[1] > 0:
        w1 = np.clip((t - (1 - blend[1])) / blend[1], 0, 1) * 0.5
    w0 = np.ones(len(P))
    J = np.stack([np.full(len(P), j0), np.full(len(P), j1)], 1)
    W = np.stack([w0 - w1, w1], 1)
    part.add(P, N, UV, T, J, W)


def blob(part, center, radii, joint, seg=14, rings=9, joint2=None, w2=0.0):
    """An ellipsoid bound to one joint (optionally a constant share of a second one)."""
    prof = ellipsoid_profile(1.0, rings)
    P, N, UV, T, _ = revolve(prof, (0, 0, 0), (0, 1, 0), seg)
    P = P * np.array(radii) + np.array(center)
    N = N / np.array(radii)
    J = np.stack([np.full(len(P), joint), np.full(len(P), joint if joint2 is None else joint2)], 1)
    W = np.tile([1 - w2, w2], (len(P), 1))
    part.add(P, N, UV, T, J, W)


def rigid(part, geometry, joint=0):
    P, N, UV, T = geometry[:4]
    J = np.stack([np.full(len(P), joint), np.full(len(P), joint)], 1)
    part.add(P, N, UV, T, J, np.tile([1.0, 0.0], (len(P), 1)))


# ------------------------------------------------------------------------------------------------ quaternions


def quat(x=0.0, y=0.0, z=0.0):
    """Yaw (y) * pitch (x) * roll (z) as an [x, y, z, w] quaternion."""
    def q_axis(axis, angle):
        s = math.sin(angle / 2)
        return np.array([axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle / 2)])

    def mul(a, b):
        ax, ay, az, aw = a
        bx, by, bz, bw = b
        return np.array([aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
                         aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz])
    q = mul(mul(q_axis((0, 1, 0), y), q_axis((1, 0, 0), x)), q_axis((0, 0, 1), z))
    return q / np.linalg.norm(q)


# ------------------------------------------------------------------------------------------------ glb writer


class GLB:
    def __init__(self, generator="Zamin gltfkit"):
        self.doc = {"asset": {"version": "2.0", "generator": generator}, "scene": 0, "scenes": [{"nodes": []}], "nodes": [], "meshes": [],
                    "materials": [], "accessors": [], "bufferViews": [], "buffers": []}
        self.bin = bytearray()

    def _view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin += b"\0"
        view = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            view["target"] = target
        self.bin += data
        self.doc["bufferViews"].append(view)
        return len(self.doc["bufferViews"]) - 1

    def accessor(self, array, kind, component=5126, target=None, minmax=False, normalized=False):
        array = np.asarray(array)
        dtype = {5126: "<f4", 5123: "<u2", 5125: "<u4", 5121: "u1"}[component]
        data = np.ascontiguousarray(array.astype(dtype)).tobytes()
        view = self._view(data, target)
        count = array.shape[0]
        acc = {"bufferView": view, "componentType": component, "count": int(count), "type": kind}
        if normalized:
            acc["normalized"] = True
        if minmax:
            flat = array.reshape(count, -1).astype(np.float64)
            acc["min"] = [round(float(v), 6) for v in flat.min(axis=0)]
            acc["max"] = [round(float(v), 6) for v in flat.max(axis=0)]
        self.doc["accessors"].append(acc)
        return len(self.doc["accessors"]) - 1

    def material(self, name, color, rough=0.7, metal=0.0, emissive=(0, 0, 0)):
        m = {"name": name, "pbrMetallicRoughness": {"baseColorFactor": [round(float(c), 4) for c in (*color[:3], 1.0)],
                                                     "metallicFactor": metal, "roughnessFactor": rough}}
        if any(emissive):
            m["emissiveFactor"] = [round(float(c), 4) for c in emissive]
        self.doc["materials"].append(m)
        return len(self.doc["materials"]) - 1

    def mesh(self, name, parts, skinned=False):
        """parts = [(material index, Part)]; returns the mesh index."""
        prims = []
        for mat, part in parts:
            P, N, UV, J, W, I = part.arrays()
            attrs = {"POSITION": self.accessor(P, "VEC3", target=34962, minmax=True), "NORMAL": self.accessor(N, "VEC3", target=34962),
                     "TEXCOORD_0": self.accessor(UV, "VEC2", target=34962)}
            if skinned:
                j4 = np.zeros((len(P), 4), np.int64)
                w4 = np.zeros((len(P), 4))
                j4[:, :2] = J
                w4[:, :2] = W
                w4 /= np.maximum(w4.sum(axis=1, keepdims=True), 1e-9)
                attrs["JOINTS_0"] = self.accessor(j4, "VEC4", 5123, target=34962)
                attrs["WEIGHTS_0"] = self.accessor(w4, "VEC4", target=34962)
            comp = 5123 if len(P) < 65535 else 5125
            prims.append({"attributes": attrs, "indices": self.accessor(I.reshape(-1), "SCALAR", comp, target=34963), "material": mat})
        self.doc["meshes"].append({"name": name, "primitives": prims})
        return len(self.doc["meshes"]) - 1

    def node(self, **fields):
        self.doc["nodes"].append(fields)
        return len(self.doc["nodes"]) - 1

    def write(self, path):
        self.doc["buffers"] = [{"byteLength": len(self.bin)}]
        js = json.dumps(self.doc, separators=(",", ":"), sort_keys=True).encode()
        while len(js) % 4:
            js += b" "
        b = bytes(self.bin)
        while len(b) % 4:
            b += b"\0"
        total = 12 + 8 + len(js) + 8 + len(b)
        out = struct.pack("<III", 0x46546C67, 2, total) + struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(b), 0x004E4942) + b
        k.ensure_dir(os.path.dirname(path))
        with open(path, "wb") as f:
            f.write(out)


def write_static(path, parts, name="Prop"):
    """parts = [(material name, color, roughness, metallic, emissive, Part)]."""
    g = GLB()
    mats = []
    for mname, color, rough, metal, emissive, part in parts:
        mats.append((g.material(mname, color, rough, metal, emissive), part))
    mesh = g.mesh(name, mats)
    g.doc["scenes"][0]["nodes"].append(g.node(name=name, mesh=mesh))
    g.write(path)


# ------------------------------------------------------------------------------------------------ humanoid

JOINTS = ["Hips", "Spine", "Chest", "Neck", "Head", "Shoulder.L", "UpperArm.L", "LowerArm.L", "Hand.L", "Shoulder.R", "UpperArm.R",
          "LowerArm.R", "Hand.R", "UpperLeg.L", "LowerLeg.L", "Foot.L", "Toe.L", "UpperLeg.R", "LowerLeg.R", "Foot.R", "Toe.R"]
J = {name: i for i, name in enumerate(JOINTS)}
PARENT = {"Hips": None, "Spine": "Hips", "Chest": "Spine", "Neck": "Chest", "Head": "Neck", "Shoulder.L": "Chest", "UpperArm.L": "Shoulder.L",
          "LowerArm.L": "UpperArm.L", "Hand.L": "LowerArm.L", "Shoulder.R": "Chest", "UpperArm.R": "Shoulder.R", "LowerArm.R": "UpperArm.R",
          "Hand.R": "LowerArm.R", "UpperLeg.L": "Hips", "LowerLeg.L": "UpperLeg.L", "Foot.L": "LowerLeg.L", "Toe.L": "Foot.L", "UpperLeg.R": "Hips",
          "LowerLeg.R": "UpperLeg.R", "Foot.R": "LowerLeg.R", "Toe.R": "Foot.R"}


def skeleton(s=1.0, wide=1.0):
    """Rest offsets (local translations) of the joints for a body scaled by `s` (height) and `wide` (shoulders/hips).
    The character faces -Z: its right is +X."""
    off = {"Hips": (0, 0.95, 0), "Spine": (0, 0.11, 0), "Chest": (0, 0.19, 0), "Neck": (0, 0.21, 0), "Head": (0, 0.09, 0),
           "UpperLeg.L": (-0.10 * wide, -0.06, 0), "LowerLeg.L": (0, -0.42, 0), "Foot.L": (0, -0.40, 0), "Toe.L": (0, -0.06, -0.12),
           "UpperLeg.R": (0.10 * wide, -0.06, 0), "LowerLeg.R": (0, -0.42, 0), "Foot.R": (0, -0.40, 0), "Toe.R": (0, -0.06, -0.12),
           "Shoulder.L": (-0.07 * wide, 0.17, 0), "UpperArm.L": (-0.12 * wide, -0.01, 0), "LowerArm.L": (0, -0.28, 0), "Hand.L": (0, -0.26, 0),
           "Shoulder.R": (0.07 * wide, 0.17, 0), "UpperArm.R": (0.12 * wide, -0.01, 0), "LowerArm.R": (0, -0.28, 0), "Hand.R": (0, -0.26, 0)}
    return {name: np.array(v, np.float64) * s for name, v in off.items()}


def world(rest):
    out = {}
    for name in JOINTS:
        p = PARENT[name]
        out[name] = rest[name] + (out[p] if p else 0)
    return out


def humanoid_mesh(rest, look):
    """Parts per material for a body with the rest offsets `rest`; `look` picks colours and gear."""
    W = world(rest)
    s = look.get("scale", 1.0)
    bulk = look.get("bulk", 1.0)
    parts = {name: Part() for name in ("skin", "cloth", "trim", "leather", "metal", "dark")}
    if look.get("glow"):
        parts["glow"] = Part()
    cloth = parts["cloth"]
    armour = parts["metal"] if look.get("armour") else cloth
    # torso: pelvis, belly (spine -> chest), chest
    blob(parts["dark"] if look.get("dark_pants", True) else cloth, W["Hips"] + np.array([0, -0.02, 0]) * s, np.array([0.165, 0.12, 0.115]) * s * bulk, J["Hips"])
    tube(cloth, W["Spine"] - np.array([0, 0.06, 0]) * s, W["Chest"], 0.145 * s * bulk, 0.16 * s * bulk, (J["Spine"], J["Chest"]), (0, 0.4), seg=14)
    blob(armour, W["Chest"] + np.array([0, 0.09, 0.0]) * s, np.array([0.19, 0.155, 0.125]) * s * bulk, J["Chest"])
    if look.get("armour"):
        # pauldrons
        for side in ("L", "R"):
            blob(parts["metal"], W[f"UpperArm.{side}"] + np.array([0, 0.03, 0]) * s, np.array([0.085, 0.07, 0.085]) * s * bulk, J[f"UpperArm.{side}"])
    # belt
    P, N, UV, T, F = revolve([(0.172 * bulk * s, 0), (0.18 * bulk * s, 0.02 * s), (0.18 * bulk * s, 0.05 * s), (0.172 * bulk * s, 0.07 * s)],
                             W["Hips"] + np.array([0, 0.05, 0]) * s, (0, 1, 0), 16)
    parts["leather"].add(P, N, UV, T, np.stack([np.full(len(P), J["Hips"]), np.full(len(P), J["Spine"])], 1), np.tile([0.6, 0.4], (len(P), 1)))
    # tunic skirt: flares from the belt; the sides follow the upper legs a little
    top, bottom = W["Hips"][1] + 0.05 * s, W["Hips"][1] - look.get("skirt", 0.34) * s
    prof = [(0.175 * bulk * s, 0), (0.2 * bulk * s, -0.4 * (top - bottom)), (0.235 * bulk * s, -0.85 * (top - bottom)), (0.245 * bulk * s, -(top - bottom))]
    P, N, UV, T, F = revolve([(r, h) for r, h in prof][::-1], (0, top, 0), (0, 1, 0), 18)
    t = np.clip((top - P[:, 1]) / (top - bottom), 0, 1)
    side = np.clip(P[:, 0] / (0.24 * bulk * s), -1, 1)
    wl = 0.55 * t * np.clip(-side, 0, 1)
    wr = 0.55 * t * np.clip(side, 0, 1)
    jj = np.where(side < 0, J["UpperLeg.L"], J["UpperLeg.R"])
    ww = np.maximum(wl, wr)
    skirt = parts["trim"] if look.get("skirt_trim") else cloth
    skirt.add(P, N, UV, T, np.stack([np.full(len(P), J["Hips"]), jj], 1), np.stack([1 - ww, ww], 1))
    # neck and head
    tube(parts["skin"], W["Neck"], W["Head"] + np.array([0, 0.02, 0]) * s, 0.055 * s, 0.05 * s, (J["Neck"], J["Head"]), (0, 0.5), seg=10)
    head = W["Head"] + np.array([0, 0.1, 0]) * s
    blob(parts["skin"], head, np.array([0.105, 0.125, 0.115]) * s, J["Head"])
    blob(parts["skin"], head + np.array([0, -0.01, -0.11]) * s, np.array([0.022, 0.03, 0.03]) * s, J["Head"], seg=8, rings=6)  # nose
    for ex in (-0.04, 0.04):
        blob(parts["glow"] if look.get("glow") else parts["dark"], head + np.array([ex, 0.015, -0.098]) * s, np.array([0.016, 0.012, 0.01]) * s, J["Head"], seg=8, rings=5)
    if look.get("beard"):
        blob(parts["dark"], head + np.array([0, -0.075, -0.06]) * s, np.array([0.085, 0.06, 0.07]) * s, J["Head"])
    hat = look.get("hat", "turban")
    if hat == "turban":
        prof = [(0.118 * s, 0), (0.13 * s, 0.03 * s), (0.128 * s, 0.07 * s), (0.11 * s, 0.11 * s), (0.06 * s, 0.14 * s), (1e-3, 0.15 * s)]
        P, N, UV, T, F = revolve(prof, head + np.array([0, 0.03, 0]) * s, (0, 1, 0), 16)
        parts["trim"].add(P, N, UV, T, np.stack([np.full(len(P), J["Head"])] * 2, 1), np.tile([1.0, 0.0], (len(P), 1)))
    elif hat in ("helmet", "horned"):
        prof = [(0.125 * s, 0), (0.13 * s, 0.05 * s), (0.115 * s, 0.11 * s), (0.07 * s, 0.17 * s), (0.015 * s, 0.24 * s if hat == "helmet" else 0.19 * s), (1e-3, 0.25 * s if hat == "helmet" else 0.195 * s)]
        P, N, UV, T, F = revolve(prof, head + np.array([0, 0.015, 0]) * s, (0, 1, 0), 16)
        parts["metal"].add(P, N, UV, T, np.stack([np.full(len(P), J["Head"])] * 2, 1), np.tile([1.0, 0.0], (len(P), 1)))
        if hat == "horned":
            for sx in (-1, 1):
                base = head + np.array([sx * 0.11, 0.09, 0]) * s
                tip = head + np.array([sx * 0.24, 0.24, -0.04]) * s
                tube(parts["trim"], base, tip, 0.03 * s, 0.006 * s, (J["Head"], J["Head"]), seg=8, cap=3)
    # arms
    for side in ("L", "R"):
        sh, el, wr = W[f"UpperArm.{side}"], W[f"LowerArm.{side}"], W[f"Hand.{side}"]
        tube(cloth, sh, el, 0.058 * s * bulk, 0.05 * s * bulk, (J[f"UpperArm.{side}"], J[f"LowerArm.{side}"]), (0, 0.22))
        tube(parts["leather"] if look.get("bracers", True) else cloth, el, wr, 0.05 * s * bulk, 0.04 * s * bulk, (J[f"LowerArm.{side}"], J[f"Hand.{side}"]), (0, 0.2))
        blob(parts["skin"], wr + np.array([0, -0.05, -0.005]) * s, np.array([0.032, 0.06, 0.026]) * s * bulk, J[f"Hand.{side}"])
    # legs
    for side in ("L", "R"):
        hp, kn, an, toe = W[f"UpperLeg.{side}"], W[f"LowerLeg.{side}"], W[f"Foot.{side}"], W[f"Toe.{side}"]
        pants = parts["dark"] if look.get("dark_pants", True) else cloth
        tube(pants, hp, kn, 0.078 * s * bulk, 0.058 * s * bulk, (J[f"UpperLeg.{side}"], J[f"LowerLeg.{side}"]), (0, 0.2))
        tube(pants, kn, an, 0.056 * s * bulk, 0.042 * s * bulk, (J[f"LowerLeg.{side}"], J[f"Foot.{side}"]), (0, 0.15))
        boot = an + np.array([0, 0.13, 0]) * s
        tube(parts["leather"], boot, an + np.array([0, -0.02, 0]) * s, 0.058 * s * bulk, 0.05 * s * bulk, (J[f"LowerLeg.{side}"], J[f"Foot.{side}"]), (0, 0.5), seg=10, cap=3)
        blob(parts["leather"], an + np.array([0, -0.045, -0.055]) * s, np.array([0.05, 0.04, 0.105]) * s * bulk, J[f"Foot.{side}"], joint2=J[f"Toe.{side}"], w2=0.25)
    colours = look["colours"]
    out = []
    for name, part in parts.items():
        if part.count:
            col, rough, metal = colours[name]
            out.append((name, col, rough, metal, look.get("glow") if name == "glow" else (0, 0, 0), part))
    return out


# ---- procedural clips: each returns {joint: (rx, ry, rz)} and the hips height at normalized time p


def _arms_rest():
    return {"UpperArm.L": [0.0, 0.0, -0.12], "UpperArm.R": [0.0, 0.0, 0.12], "LowerArm.L": [0.12, 0, 0], "LowerArm.R": [0.12, 0, 0]}


def gait(p, A, knee_swing, knee_base, bob, arm, elbow, lean, hip_h):
    c = math.cos(2 * math.pi * p)
    sn = math.sin(2 * math.pi * p)
    r = {name: [0.0, 0.0, 0.0] for name in JOINTS}
    for k_, v in _arms_rest().items():
        r[k_] = list(v)
    # legs: the left leg is forward at p = 0 (its heel strike), the right at p = 0.5
    r["UpperLeg.L"][0] = A * c + 0.04
    r["UpperLeg.R"][0] = -A * c + 0.04
    r["LowerLeg.L"][0] = -(knee_base * (0.6 + 0.4 * max(0.0, sn)) + knee_swing * max(0.0, -sn) ** 1.2)
    r["LowerLeg.R"][0] = -(knee_base * (0.6 + 0.4 * max(0.0, -sn)) + knee_swing * max(0.0, sn) ** 1.2)
    for side in ("L", "R"):
        r[f"Foot.{side}"][0] = -0.55 * (r[f"UpperLeg.{side}"][0] + r[f"LowerLeg.{side}"][0])
    # arms swing against the legs
    r["UpperArm.L"][0] = -arm * c
    r["UpperArm.R"][0] = arm * c
    r["LowerArm.L"][0] = elbow + 0.18 * max(0.0, -c) * arm * 2
    r["LowerArm.R"][0] = elbow + 0.18 * max(0.0, c) * arm * 2
    r["Hips"][1] = 0.07 * c * A
    r["Spine"][0] = lean
    r["Chest"][1] = -0.12 * c * A
    r["Neck"][0] = -lean * 0.6
    y = hip_h - bob * (0.5 + 0.5 * math.cos(4 * math.pi * p))
    return r, y


def clip_pose(name, p, hip_h):
    """The pose of clip `name` at normalized time p (0..1)."""
    if name == "Walk":
        return gait(p, 0.42, 0.85, 0.12, 0.028, 0.32, 0.22, -0.04, hip_h)
    if name == "Run":
        return gait(p, 0.66, 1.55, 0.3, 0.055, 0.62, 1.25, -0.2, hip_h - 0.02)
    r = {name_: [0.0, 0.0, 0.0] for name_ in JOINTS}
    for k_, v in _arms_rest().items():
        r[k_] = list(v)
    y = hip_h
    sn = math.sin(2 * math.pi * p)
    if name == "Idle":
        r["Chest"][0] = 0.025 * sn
        r["Neck"][0] = -0.02 * sn
        r["Head"][1] = 0.08 * math.sin(2 * math.pi * p + 0.6)
        r["UpperArm.L"][2] -= 0.025 * (1 + sn)
        r["UpperArm.R"][2] += 0.025 * (1 + sn)
        r["UpperLeg.L"][0], r["UpperLeg.R"][0] = 0.04, -0.03
        r["LowerLeg.L"][0], r["LowerLeg.R"][0] = -0.06, -0.04
        y = hip_h - 0.006 * (1 + sn)
    elif name == "Fall":
        r["UpperArm.L"] = [0.35 + 0.08 * sn, 0, -0.95]
        r["UpperArm.R"] = [0.35 - 0.08 * sn, 0, 0.95]
        r["LowerArm.L"][0] = r["LowerArm.R"][0] = 0.5
        r["UpperLeg.L"][0], r["UpperLeg.R"][0] = 0.45, -0.1
        r["LowerLeg.L"][0], r["LowerLeg.R"][0] = -0.8, -0.35
        r["Spine"][0] = 0.06
        y = hip_h
    elif name == "Land":
        d = math.sin(math.pi * min(1.0, p / 0.85)) ** 0.8
        for side in ("L", "R"):
            r[f"UpperLeg.{side}"][0] = 0.55 * d
            r[f"LowerLeg.{side}"][0] = -1.05 * d
            r[f"Foot.{side}"][0] = 0.5 * d
        r["Spine"][0] = -0.25 * d
        r["UpperArm.L"][2] -= 0.35 * d
        r["UpperArm.R"][2] += 0.35 * d
        y = hip_h - 0.16 * d
    elif name == "Attack":
        # sword in the right hand: wind up over the shoulder, slash down across (Hit at 0.42), recover
        keys = [(0.0, (0.0, 0.12, 0.12, 0.0)), (0.33, (2.5, 0.35, 0.9, 0.38)), (0.47, (0.55, -0.15, 0.15, -0.42)), (0.62, (0.35, -0.1, 0.1, -0.45)), (1.0, (0.0, 0.12, 0.12, 0.0))]
        ua, uz, la, ch = _keys(keys, p)
        r["UpperArm.R"] = [ua, 0.0, 0.12 + uz]
        r["LowerArm.R"][0] = la + 0.12
        r["Chest"][1] = ch
        r["Spine"][0] = -0.12 * math.sin(math.pi * p)
        r["UpperArm.L"] = [0.5 * math.sin(math.pi * p), 0, -0.3]
        r["LowerArm.L"][0] = 0.6 * math.sin(math.pi * p) + 0.12
    elif name == "Cast":
        f = _keys([(0.0, (0.0,)), (0.3, (0.55,)), (0.45, (1.0,)), (0.65, (0.9,)), (1.0, (0.0,))], p)[0]
        r["UpperArm.L"] = [1.45 * f, -0.2 * f, -0.12]
        r["UpperArm.R"] = [1.45 * f, 0.2 * f, 0.12]
        r["LowerArm.L"][0] = r["LowerArm.R"][0] = 0.12 + 0.25 * (1 - f) * min(1.0, p * 3)
        r["Spine"][0] = -0.16 * f
        r["Chest"][1] = 0.1 * f
    elif name == "Hurt":
        f = math.sin(math.pi * min(1.0, p * 1.4)) if p < 1 / 1.4 else 0.0
        r["Chest"][0] = 0.32 * f
        r["Neck"][0] = 0.25 * f
        r["Spine"][0] = 0.1 * f
        r["UpperArm.L"][2] -= 0.3 * f
        r["UpperArm.R"][2] += 0.3 * f
    elif name == "Death":
        f = min(1.0, p / 0.75)
        e = f * f * (3 - 2 * f)
        r["Hips"][0] = 1.42 * e
        r["Spine"][0] = 0.1 * e
        r["Neck"][0] = 0.3 * e
        r["UpperLeg.L"][0], r["UpperLeg.R"][0] = -0.1 * e, 0.12 * e
        r["LowerLeg.L"][0], r["LowerLeg.R"][0] = -0.5 * e, -0.15 * e
        r["UpperArm.L"] = [0.4 * e, 0, -0.12 - 1.1 * e]
        r["UpperArm.R"] = [0.2 * e, 0, 0.12 + 1.25 * e]
        bounce = 0.04 * math.sin(math.pi * max(0.0, min(1.0, (p - 0.75) / 0.2))) if p > 0.75 else 0.0
        y = hip_h - (hip_h - 0.17 * hip_h / 0.95) * e + bounce
    return r, y


def _keys(keys, p):
    for (t0, v0), (t1, v1) in zip(keys, keys[1:]):
        if p <= t1:
            f = (p - t0) / max(1e-9, t1 - t0)
            f = f * f * (3 - 2 * f)
            return [a + (b - a) * f for a, b in zip(v0, v1)]
    return list(keys[-1][1])


def clip_set(walk_speed, run_speed, walk_stride, run_stride):
    """(name, duration, loop) - Walk/Run lengths make one cycle cover its stride at the threshold speed."""
    return [("Idle", 3.0, True), ("Walk", walk_stride / walk_speed, True), ("Run", run_stride / run_speed, True), ("Fall", 0.6, True),
            ("Land", 0.3, False), ("Attack", 0.75, False), ("Cast", 0.8, False), ("Hurt", 0.4, False), ("Death", 1.4, False)]


def write_humanoid(path, look, clips, fps=20):
    s = look.get("scale", 1.0)
    rest = skeleton(s, look.get("wide", 1.0))
    W = world(rest)
    g = GLB()
    # joints as nodes 0..20, the skinned mesh node after them
    children = {name: [] for name in JOINTS}
    for name in JOINTS:
        if PARENT[name]:
            children[PARENT[name]].append(J[name])
    for name in JOINTS:
        node = {"name": name, "translation": [round(float(v), 6) for v in rest[name]]}
        if children[name]:
            node["children"] = children[name]
        g.node(**node)
    parts = humanoid_mesh(rest, look)
    mats = [(g.material(name, col, rough, metal, emissive), part) for name, col, rough, metal, emissive, part in parts]
    mesh = g.mesh(look.get("name", "Body"), mats, skinned=True)
    inverse = np.zeros((len(JOINTS), 16))
    for name in JOINTS:
        m = np.eye(4)
        m[:3, 3] = -W[name]
        inverse[J[name]] = m.T.reshape(-1)  # column-major
    ibm = g.accessor(inverse, "MAT4")
    g.doc["skins"] = [{"joints": list(range(len(JOINTS))), "inverseBindMatrices": ibm, "skeleton": 0}]
    body = g.node(name=look.get("name", "Body"), mesh=mesh, skin=0)
    g.doc["scenes"][0]["nodes"] = [0, body]
    hip_h = W["Hips"][1]
    animations = []
    for name, duration, loop in clips:
        frames = max(2, int(round(duration * fps)) + 1)
        times = np.linspace(0, duration, frames)
        poses = [clip_pose(name, t / duration, hip_h) for t in times]
        t_acc = g.accessor(times.reshape(-1, 1), "SCALAR", minmax=True)
        samplers, channels = [], []
        for joint in JOINTS:
            rot = np.array([quat(*pose[joint]) for pose, _ in poses])
            # keep neighbouring quaternions in one hemisphere (linear interpolation)
            for i in range(1, len(rot)):
                if np.dot(rot[i], rot[i - 1]) < 0:
                    rot[i] = -rot[i]
            samplers.append({"input": t_acc, "output": g.accessor(rot, "VEC4"), "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": J[joint], "path": "rotation"}})
        hips = np.array([[0.0, y, 0.0] for _, y in poses])
        samplers.append({"input": t_acc, "output": g.accessor(hips, "VEC3"), "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": J["Hips"], "path": "translation"}})
        animations.append({"name": name, "samplers": samplers, "channels": channels})
    g.doc["animations"] = animations
    g.write(path)
    return W


# ------------------------------------------------------------------------------------------------ props


def P():
    return Part()


def sword(length=0.95):
    blade, hilt, grip = P(), P(), P()
    blade_geo = revolve([(0.004, 0.0), (0.022, 0.02), (0.02, length * 0.7), (0.012, length * 0.9), (0.001, length)], (0, 0.1, 0), (0, 1, 0), 4,
                        scale=(1.0, 0.18))
    rigid(blade, blade_geo)
    rigid(hilt, box((0, 0.1, 0), (0.09, 0.012, 0.022)))
    rigid(grip, revolve([(0.016, 0), (0.016, 0.1), (0.022, 0.115), (0.001, 0.13)], (0, -0.02, 0), (0, 1, 0), 8)[:4])
    return [("Blade", (0.82, 0.84, 0.88), 0.25, 1.0, (0, 0, 0), blade), ("Hilt", (0.85, 0.62, 0.22), 0.35, 1.0, (0, 0, 0), hilt),
            ("Grip", (0.28, 0.16, 0.09), 0.8, 0.0, (0, 0, 0), grip)]


def spear(length=2.1):
    shaft, tip = P(), P()
    rigid(shaft, revolve([(0.018, -0.6), (0.018, length - 0.6)], (0, 0, 0), (0, 1, 0), 8)[:4])
    rigid(tip, revolve([(0.02, 0), (0.035, 0.06), (0.025, 0.18), (0.001, 0.3)], (0, length - 0.62, 0), (0, 1, 0), 4, scale=(1.0, 0.35))[:4])
    return [("Shaft", (0.45, 0.3, 0.17), 0.8, 0.0, (0, 0, 0), shaft), ("Tip", (0.8, 0.8, 0.83), 0.3, 1.0, (0, 0, 0), tip)]


def mace():
    shaft, head = P(), P()
    rigid(shaft, revolve([(0.025, -0.1), (0.025, 0.75)], (0, 0, 0), (0, 1, 0), 8)[:4])
    rigid(head, revolve([(0.001, 0.0), (0.09, 0.05), (0.12, 0.13), (0.09, 0.21), (0.001, 0.26)], (0, 0.68, 0), (0, 1, 0), 8)[:4])
    for i in range(8):
        a = 2 * math.pi * i / 8
        rigid(head, revolve([(0.03, 0), (0.001, 0.11)], (math.cos(a) * 0.1, 0.81, math.sin(a) * 0.1), (math.cos(a), 0.1, math.sin(a)), 5)[:4])
    return [("Shaft", (0.25, 0.18, 0.12), 0.8, 0.0, (0, 0, 0), shaft), ("Head", (0.32, 0.3, 0.33), 0.35, 1.0, (0, 0, 0), head)]


def shield():
    wood, rim, boss = P(), P(), P()
    rigid(wood, revolve([(0.001, 0.0), (0.3, 0.025), (0.31, 0.03)], (0, 0, 0), (0, 0, -1), 20)[:4])
    rigid(rim, revolve([(0.3, 0.0), (0.33, 0.015), (0.33, 0.04), (0.3, 0.05)], (0, 0, 0.01), (0, 0, -1), 20)[:4])
    rigid(boss, revolve([(0.08, 0.03), (0.07, 0.07), (0.001, 0.1)], (0, 0, 0), (0, 0, -1), 12)[:4])
    return [("Wood", (0.52, 0.3, 0.16), 0.75, 0.0, (0, 0, 0), wood), ("Rim", (0.8, 0.6, 0.25), 0.35, 1.0, (0, 0, 0), rim),
            ("Boss", (0.85, 0.7, 0.3), 0.3, 1.0, (0, 0, 0), boss)]


def pine(seed, height=6.0, far=False):
    rng = np.random.default_rng(seed)
    trunk, needles = P(), P()
    rigid(trunk, revolve([(0.2, 0), (0.14, height * 0.35), (0.05, height * 0.7)], (0, 0, 0), (0, 1, 0), 6 if far else 8)[:4])
    tiers = 2 if far else 4
    for i in range(tiers):
        y0 = height * (0.22 + 0.19 * i * 4 / tiers)
        r = height * (0.3 - 0.055 * i * 4 / tiers) * (0.92 + 0.16 * rng.random())
        prof = [(r * 0.25, 0), (r, height * 0.07), (r * 0.6, height * 0.2), (0.001, height * (0.36 if i == tiers - 1 else 0.3))]
        rigid(needles, revolve(prof, (0, y0, 0), (0, 1, 0), 7 if far else 9, phase=float(rng.random()))[:4])
    return [("Bark", (0.33, 0.22, 0.14), 0.9, 0.0, (0, 0, 0), trunk), ("Needles", (0.16, 0.3, 0.18), 0.85, 0.0, (0, 0, 0), needles)]


def rock(seed, size=1.0, color=(0.52, 0.48, 0.44)):
    rng = np.random.default_rng(seed)
    part = P()
    prof = ellipsoid_profile(1.0, 7)
    Pp, N, UV, T, _ = revolve(prof, (0, 0, 0), (0, 1, 0), 9)
    noise = 1 + 0.22 * np.sin(Pp[:, 0] * 3.1 + seed) * np.cos(Pp[:, 2] * 2.7 + seed * 0.7) + 0.12 * rng.standard_normal(len(Pp)) * 0
    Pp = Pp * np.array([1.2, 0.75, 1.0]) * size * noise[:, None]
    Pp[:, 1] = np.maximum(Pp[:, 1], -0.25 * size)
    # flat shading for a faceted look: unweld
    tri = Pp[T]
    fn = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    Pf = tri.reshape(-1, 3)
    Nf = np.repeat(fn, 3, axis=0)
    UVf = Pf[:, [0, 2]] * 0.5
    part.add(Pf, Nf, UVf, np.arange(len(Pf)).reshape(-1, 3))
    return [("Rock", color, 0.92, 0.0, (0, 0, 0), part)]


def crystal(color, glow, size=0.35):
    part = P()
    Pp, N, UV, T, _ = revolve([(0.001, -size), (size * 0.55, 0.0), (0.001, size * 1.3)], (0, 0, 0), (0, 1, 0), 6)
    tri = Pp[T]
    fn = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    part.add(tri.reshape(-1, 3), np.repeat(fn, 3, axis=0), np.zeros((len(T) * 3, 2)), np.arange(len(T) * 3).reshape(-1, 3))
    return [("Crystal", color, 0.2, 0.0, glow, part)]


def brazier(scale=1.0, legs=3):
    bowl, metal, coals = P(), P(), P()
    rigid(bowl, revolve([(0.001, 0.0), (0.35 * scale, 0.04 * scale), (0.5 * scale, 0.22 * scale), (0.56 * scale, 0.3 * scale), (0.5 * scale, 0.3 * scale),
                         (0.42 * scale, 0.2 * scale)], (0, 0.75 * scale, 0), (0, 1, 0), 16)[:4])
    for i in range(legs):
        a = 2 * math.pi * i / legs
        foot = np.array([math.cos(a) * 0.45, 0.0, math.sin(a) * 0.45]) * scale
        top = np.array([math.cos(a) * 0.3, 0.8, math.sin(a) * 0.3]) * scale
        rigid(metal, revolve([(0.03 * scale, 0), (0.03 * scale, float(np.linalg.norm(top - foot)))], foot, top - foot, 6)[:4])
    rigid(coals, revolve([(0.001, 0.0), (0.45 * scale, 0.02 * scale)], (0, 1.0 * scale, 0), (0, 1, 0), 16)[:4])
    return [("Bowl", (0.3, 0.27, 0.26), 0.45, 1.0, (0, 0, 0), bowl), ("Legs", (0.22, 0.2, 0.2), 0.5, 1.0, (0, 0, 0), metal),
            ("Coals", (0.25, 0.08, 0.03), 0.9, 0.0, (1.6, 0.45, 0.08), coals)]


def tent(radius=2.4, height=2.8, color=(0.78, 0.66, 0.5)):
    cloth, pole = P(), P()
    rigid(cloth, revolve([(radius, 0.0), (radius * 0.98, height * 0.35), (radius * 0.55, height * 0.75), (0.08, height)], (0, 0, 0), (0, 1, 0), 9)[:4])
    rigid(pole, revolve([(0.05, 0), (0.05, height + 0.4)], (0, 0, 0), (0, 1, 0), 6)[:4])
    return [("Felt", color, 0.95, 0.0, (0, 0, 0), cloth), ("Pole", (0.42, 0.28, 0.16), 0.8, 0.0, (0, 0, 0), pole)]


def well():
    stone, wood, water = P(), P(), P()
    rigid(stone, revolve([(0.9, 0.0), (0.92, 0.75), (0.75, 0.8), (0.72, 0.1)], (0, 0, 0), (0, 1, 0), 18)[:4])
    for x in (-0.85, 0.85):
        rigid(wood, box((x, 1.2, 0), (0.07, 0.8, 0.07)))
    rigid(wood, box((0, 2.0, 0), (1.1, 0.07, 0.08)))
    rigid(wood, revolve([(0.001, 0.0), (1.25, 0.0), (0.001, 0.55)], (0, 2.05, 0), (0, 1, 0), 4, phase=math.pi / 4)[:4])
    rigid(water, revolve([(0.001, 0.0), (0.72, 0.0)], (0, 0.45, 0), (0, 1, 0), 16)[:4])
    return [("Stone", (0.62, 0.56, 0.48), 0.9, 0.0, (0, 0, 0), stone), ("Wood", (0.45, 0.3, 0.18), 0.8, 0.0, (0, 0, 0), wood),
            ("Water", (0.08, 0.2, 0.28), 0.1, 0.0, (0, 0, 0), water)]


def shrine():
    stone, glow = P(), P()
    rigid(stone, revolve([(0.55, 0.0), (0.55, 0.18), (0.35, 0.22), (0.28, 0.25), (0.22, 1.25), (0.32, 1.32), (0.32, 1.42), (0.001, 1.43)], (0, 0, 0), (0, 1, 0), 8)[:4])
    gem = crystal((0.4, 0.85, 1.0), (0.6, 1.6, 2.4), 0.16)[0][5]
    Pp, N, UV, J_, W_, T = gem.arrays()
    glow.add(Pp + np.array([0, 1.72, 0]), N, UV, T)
    return [("Stone", (0.7, 0.66, 0.6), 0.85, 0.0, (0, 0, 0), stone), ("Gem", (0.4, 0.85, 1.0), 0.15, 0.0, (0.6, 1.6, 2.4), glow)]


def chest():
    wood, metal = P(), P()
    rigid(wood, box((0, 0.28, 0), (0.45, 0.28, 0.3)))
    rigid(wood, revolve([(0.3, -0.45), (0.3, 0.45)], (0, 0.56, 0), (1, 0, 0), 10, scale=(1.0, 1.0))[:4])
    for x in (-0.3, 0.3):
        rigid(metal, box((x, 0.42, 0), (0.035, 0.45, 0.31)))
    rigid(metal, box((0, 0.5, -0.31), (0.06, 0.08, 0.02)))
    return [("Wood", (0.5, 0.3, 0.16), 0.8, 0.0, (0, 0, 0), wood), ("Iron", (0.75, 0.6, 0.3), 0.4, 1.0, (0, 0, 0), metal)]


def banner(color, trim):
    pole, cloth, edge = P(), P(), P()
    rigid(pole, revolve([(0.04, 0), (0.04, 3.4)], (0, 0, 0), (0, 1, 0), 6)[:4])
    rigid(pole, box((0, 3.2, 0.0), (0.45, 0.03, 0.03)))
    # a hanging cloth with a slight curve
    n = 10
    Pp, N, UV, T = [], [], [], []
    for iy in range(n + 1):
        for ix in range(5):
            x = -0.4 + 0.8 * ix / 4
            y = 3.15 - 1.6 * iy / n
            z = 0.06 * math.sin(iy / n * math.pi) + 0.03 * math.sin(ix * 1.3)
            Pp.append((x, y, z))
            N.append((0, 0, 1))
            UV.append((ix / 4, iy / n))
    for iy in range(n):
        for ix in range(4):
            a = iy * 5 + ix
            T += [(a, a + 5, a + 1), (a + 1, a + 5, a + 6)]
    Pp = np.array(Pp)
    cloth.add(Pp, N, UV, T)
    back = Pp.copy()
    back[:, 2] -= 0.005
    cloth.add(back, [(0, 0, -1)] * len(back), UV, np.array(T)[:, ::-1])
    rigid(edge, box((0, 1.52, 0.04), (0.42, 0.05, 0.02)))
    return [("Pole", (0.42, 0.28, 0.16), 0.8, 0.0, (0, 0, 0), pole), ("Cloth", color, 0.9, 0.0, (0, 0, 0), cloth),
            ("Trim", trim, 0.6, 0.3, (0, 0, 0), edge)]


def crate():
    wood, dark = P(), P()
    rigid(wood, box((0, 0.4, 0), (0.4, 0.4, 0.4)))
    for y in (0.06, 0.74):
        rigid(dark, box((0, y, 0), (0.41, 0.05, 0.41)))
    return [("Wood", (0.6, 0.44, 0.26), 0.85, 0.0, (0, 0, 0), wood), ("Band", (0.36, 0.24, 0.14), 0.85, 0.0, (0, 0, 0), dark)]


def bolt_visual(color, glow, length=0.5):
    part = P()
    rigid(part, revolve([(0.001, -length / 2), (0.08, -length * 0.15), (0.06, length * 0.2), (0.001, length / 2)], (0, 0, 0), (0, 0, -1), 8)[:4])
    return [("Core", color, 0.3, 0.0, glow, part)]


def arrow_visual():
    shaft, tip, fletch = P(), P(), P()
    rigid(shaft, revolve([(0.01, -0.35), (0.01, 0.3)], (0, 0, 0), (0, 0, -1), 5)[:4])
    rigid(tip, revolve([(0.025, 0.3), (0.001, 0.4)], (0, 0, 0), (0, 0, -1), 4)[:4])
    rigid(fletch, box((0, 0, 0.3), (0.003, 0.04, 0.06)))
    rigid(fletch, box((0, 0, 0.3), (0.04, 0.003, 0.06)))
    return [("Shaft", (0.55, 0.4, 0.24), 0.8, 0.0, (0, 0, 0), shaft), ("Tip", (0.75, 0.75, 0.78), 0.3, 1.0, (0, 0, 0), tip),
            ("Fletch", (0.85, 0.25, 0.2), 0.9, 0.0, (0, 0, 0), fletch)]


def ring(radius, color, glow, width=0.15):
    part = P()
    rigid(part, revolve([(radius - width, 0.0), (radius, 0.03), (radius + width, 0.0)], (0, 0, 0), (0, 1, 0), 40)[:4])
    return [("Ring", color, 0.3, 0.0, glow, part)]


def peak(seed, radius, height):
    """A distant mountain: a ridged cone with a snow cap."""
    rng = np.random.default_rng(seed)
    rockp, snow = P(), P()
    prof = [(radius, 0.0), (radius * 0.7, height * 0.35), (radius * 0.38, height * 0.66), (radius * 0.08, height * 0.95), (0.001, height)]
    Pp, N, UV, T, F = revolve(prof, (0, 0, 0), (0, 1, 0), 11, phase=float(rng.random()))
    jitter = 1 + 0.18 * np.sin(np.arange(len(Pp)) * 1.7 + seed)
    Pp[:, 0] *= jitter
    Pp[:, 2] *= jitter
    tri = Pp[T]
    fn = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    mid = tri.mean(axis=1)[:, 1]
    low = mid < height * 0.6
    for target, mask in ((rockp, low), (snow, ~low)):
        sel = tri[mask].reshape(-1, 3)
        target.add(sel, np.repeat(fn[mask], 3, axis=0), sel[:, [0, 2]] * 0.01, np.arange(len(sel)).reshape(-1, 3))
    return [("Rock", (0.36, 0.34, 0.38), 0.95, 0.0, (0, 0, 0), rockp), ("Snow", (0.9, 0.92, 0.96), 0.7, 0.0, (0, 0, 0), snow)]
