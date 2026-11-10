#!/usr/bin/env python3
"""Generate deterministic, seamless PBR test textures for the existing MapWorkshop scene.

No external dependencies or downloaded art. The height fields are analytic periodic functions;
normal and ORM images are data derived from those fields. Re-running preserves asset UUIDs.
"""
from pathlib import Path
import hashlib
import json
import math
import struct
import zlib

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "Projects/MapWorkshop"
ASSETS = PROJECT / "Assets"
SIZE = 256


def png(path, rows):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xffffffff)
    raw = b"".join(b"\0" + bytes(row) for row in rows)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def byte(value):
    return max(0, min(255, round(value * 255)))


def generate():
    scene_path = ASSETS / "Scenes/Map.zscene"
    scene = json.loads(scene_path.read_text())
    terrain = next(e["Terrain"] for e in scene["Entities"] if "Terrain" in e)
    for layer, name in enumerate(("grass", "soil", "rock", "sand")):
        height = []
        for y in range(SIZE):
            row = []
            for x in range(SIZE):
                u, v = 2 * math.pi * x / SIZE, 2 * math.pi * y / SIZE
                h = .5 + .16 * math.sin((3 + layer) * u + 2 * math.cos(2 * v))
                h += .12 * math.sin(7 * v + math.cos(3 * u)) + .07 * math.cos(17 * u + 11 * v)
                h += .045 * math.sin(37 * u + 29 * v)
                row.append(h)
            height.append(row)
        buffers = {kind: [] for kind in ("albedo", "normal", "orm")}
        for y in range(SIZE):
            rows = {kind: [] for kind in buffers}
            for x in range(SIZE):
                h = height[y][x]
                shade = .63 + .35 * h
                rows["albedo"] += [byte(shade)] * 3 + [255]
                dx = (height[y][(x + 1) % SIZE] - height[y][(x - 1) % SIZE]) * 2
                dy = (height[(y + 1) % SIZE][x] - height[(y - 1) % SIZE][x]) * 2
                length = math.sqrt(dx * dx + dy * dy + 1)
                rows["normal"] += [byte(.5 - dx / length * .5), byte(.5 + dy / length * .5), byte(.5 + .5 / length), 255]
                rows["orm"] += [byte(.8 + .2 * h), byte(.68 + .28 * h), 0, 255]
            for kind in buffers:
                buffers[kind].append(rows[kind])
        for kind, rows in buffers.items():
            relative = f"Textures/Terrain/{name}-{kind}.png"
            path = ASSETS / relative
            png(path, rows)
            uuid = hashlib.sha1(("MapWorkshop/" + relative).encode()).hexdigest()[:16]
            meta = {"UUID": uuid, "Type": "Texture", "Import": {"Filter": "Linear", "Wrap": "Repeat", "Mipmaps": True, "sRGB": kind == "albedo"}}
            path.with_suffix(".png.zmeta").write_text(json.dumps(meta, indent=4) + "\n")
            field = {"albedo": "Texture", "normal": "NormalMap", "orm": "MaskMap"}[kind]
            terrain["Layers"][layer][field] = relative
            terrain["Layers"][layer][field + "UUID"] = uuid
        terrain["Layers"][layer].update(Roughness=1.0, Metallic=0.0, Occlusion=1.0, NormalStrength=.7)
    scene_path.write_text(json.dumps(scene, indent=2) + "\n")
    print("MapWorkshop: 12 seamless PBR textures and stable UUID references generated")


if __name__ == "__main__":
    generate()
