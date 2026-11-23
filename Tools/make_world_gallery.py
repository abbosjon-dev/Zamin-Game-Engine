#!/usr/bin/env python3
"""Generates the WorldGallery3D streaming demo: a grid of cells, each with its own ground texture
and a textured monument model, a streaming manifest with asset budgets and a repeatable camera tour.

Deterministic and dependency-free (PNG and OBJ are written by hand), so the acceptance tests can
generate a heavier copy (--texture-size 2048 --model-detail 3) without committing large files.
Existing .zmeta files are kept: rerunning the script does not change asset UUIDs.

    Tools/make_world_gallery.py Projects/WorldGallery3D
"""
import argparse
import json
import math
from pathlib import Path
import struct
import zlib

CELL = (32.0, 16.0, 32.0)
FONT = {  # 3x5 digits, rows top to bottom
    "0": "111101101101111", "1": "010110010010111", "2": "111001111100111", "3": "111001111001111",
    "4": "101101111001001", "5": "111100111001111", "6": "111100111101111", "7": "111001010010010",
    "8": "111101111101111", "9": "111101111001111", "-": "000000111000000",
}
PALETTE = [(196, 92, 72), (88, 150, 196), (120, 176, 92), (212, 170, 78), (148, 108, 190), (78, 168, 160),
           (204, 120, 150), (136, 136, 152)]


def write_png(path, width, height, rows):
    """rows: `height` bytes objects of width * 3 RGB bytes, top row first."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + row for row in rows)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def label_texture(path, size, color, label, accent):
    """A 64x64 design scaled up: tinted checker, border, a diagonal accent and the label in digits."""
    grid = 64
    dark = tuple(c * 5 // 8 for c in color)
    light = tuple(min(255, c * 9 // 8 + 12) for c in color)
    cells = [[(light if (x // 8 + y // 8) % 2 else color) for x in range(grid)] for y in range(grid)]
    for i in range(grid):
        for j in (0, 1, grid - 2, grid - 1):
            cells[i][j] = dark
            cells[j][i] = dark
        if 6 <= i < grid - 6:
            cells[i][grid - 1 - i] = accent
    glyph_scale = 3
    x0 = (grid - (len(label) * 4 - 1) * glyph_scale) // 2
    y0 = (grid - 5 * glyph_scale) // 2
    for index, char in enumerate(label):
        bits = FONT.get(char, FONT["-"])
        for gy in range(5):
            for gx in range(3):
                if bits[gy * 3 + gx] == "1":
                    for sy in range(glyph_scale):
                        for sx in range(glyph_scale):
                            cells[y0 + gy * glyph_scale + sy][x0 + (index * 4 + gx) * glyph_scale + sx] = (250, 250, 244)
    scale = max(1, size // grid)
    rows = []
    for line in cells:
        row = b"".join(bytes(pixel) * scale for pixel in line)
        rows.extend([row] * scale)
    write_png(path, grid * scale, grid * scale, rows)


def write_monument(path, variant, detail):
    """A surface of revolution with a variant profile: positions, normals and texture coordinates."""
    segments, rings = 24 * detail, 12 * detail
    lobes = 2 + variant % 4
    lines = [f"# WorldGallery3D monument {variant}", "o Monument"]
    profile = []
    for r in range(rings + 1):
        t = r / rings
        radius = 2.4 * (1.0 - t) ** 0.6 * (0.72 + 0.28 * math.cos(t * math.pi * (1 + variant % 3))) + 0.25
        profile.append((radius, t * 9.0))
    for r, (radius, y) in enumerate(profile):
        for s in range(segments + 1):
            a = s / segments * math.tau
            bulge = 1.0 + 0.12 * math.sin(a * lobes + r * 0.35 * (1 + variant % 2))
            lines.append(f"v {radius * bulge * math.cos(a):.4f} {y:.4f} {radius * bulge * math.sin(a):.4f}")
    for r in range(rings + 1):
        for s in range(segments + 1):
            lines.append(f"vt {s / segments * 3:.4f} {r / rings * 3:.4f}")
    for r in range(rings + 1):
        a0 = profile[max(r - 1, 0)]
        a1 = profile[min(r + 1, rings)]
        slope = (a0[0] - a1[0]) / max(a1[1] - a0[1], 1e-6)
        for s in range(segments + 1):
            a = s / segments * math.tau
            n = (math.cos(a), slope, math.sin(a))
            length = math.sqrt(sum(c * c for c in n))
            lines.append(f"vn {n[0] / length:.4f} {n[1] / length:.4f} {n[2] / length:.4f}")
    row = segments + 1
    for r in range(rings):
        for s in range(segments):
            a, b, c, d = r * row + s + 1, r * row + s + 2, (r + 1) * row + s + 2, (r + 1) * row + s + 1
            lines.append(f"f {a}/{a}/{a} {d}/{d}/{d} {c}/{c}/{c}")
            lines.append(f"f {a}/{a}/{a} {c}/{c}/{c} {b}/{b}/{b}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n")


def tour(columns, rows):
    def center(x, z):
        return [x * CELL[0] + CELL[0] / 2, 46.0, z * CELL[2] + CELL[2] / 2 + 14.0]
    keys = [{"Frame": 0, "Position": center(0, 0), "Jump": False},
            {"Frame": 240, "Position": center(columns - 1, 0), "Jump": False},
            {"Frame": 420, "Position": center(columns - 1, rows - 1), "Jump": False},
            {"Frame": 660, "Position": center(0, rows - 1), "Jump": False}]
    # Fast camera: a teleport between far corners every six frames - loads are cancelled before they finish.
    corners = [center(columns - 1, 0), center(0, 0), center(columns - 1, rows - 1), center(0, rows - 1)]
    frame = 666
    for i in range(20):
        keys.append({"Frame": frame, "Position": corners[i % 4], "Jump": True})
        frame += 6
    keys.append({"Frame": 800, "Position": [5000.0, 46.0, 5000.0], "Jump": True})   # an empty region: everything unloads
    keys.append({"Frame": 920, "Position": center(0, 0), "Jump": True})             # the return
    keys.append({"Frame": 1079, "Position": center(1, 0), "Jump": False})
    return {"Format": "ZaminWorldTour", "Version": 1, "Name": "Gallery sweep, dash, void and return", "Warmup": 240, "Keys": keys}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("project", type=Path)
    parser.add_argument("--name", default="WorldGallery3D")
    parser.add_argument("--columns", type=int, default=6)
    parser.add_argument("--rows", type=int, default=4)
    parser.add_argument("--texture-size", type=int, default=512, help="ground / monument texture size (a multiple of 64)")
    parser.add_argument("--model-detail", type=int, default=1, help="monument tessellation: 1 = 576 triangles, 3 = 5184")
    parser.add_argument("--models", type=int, default=8, help="monument variants (cells share them round-robin)")
    parser.add_argument("--legacy", action="store_true", help="AssetStreaming off: the pre-budget main-thread path")
    parser.add_argument("--window", default="1280x720", help="the Player's window (WIDTHxHEIGHT)")
    args = parser.parse_args()
    if not (1 <= args.columns <= 11 and 1 <= args.rows <= 11 and args.texture_size % 64 == 0 and 64 <= args.texture_size <= 4096
            and 1 <= args.model_detail <= 8 and 1 <= args.models <= len(PALETTE)):
        parser.error("grid up to 11x11, texture size a multiple of 64 up to 4096, detail 1..8, up to 8 models")
    try:
        width, height = (int(v) for v in args.window.split("x"))
    except ValueError:
        parser.error("--window WIDTHxHEIGHT")
    project, assets = args.project, args.project / "Assets"
    for variant in range(args.models):
        write_monument(assets / f"Models/Monument_{variant}.obj", variant, args.model_detail)
        label_texture(assets / f"Textures/Monument_{variant}.png", args.texture_size, PALETTE[(variant + 3) % len(PALETTE)], str(variant), (40, 40, 48))
        material = {"Version": 1, "Shader": "Lit", "BlendMode": "Opaque", "Albedo": [1, 1, 1, 1], "AlbedoMap": f"Textures/Monument_{variant}.png",
                    "Metallic": 0.1, "Roughness": 0.55, "Tiling": [1, 1], "Offset": [0, 0]}
        (assets / "Materials").mkdir(parents=True, exist_ok=True)
        (assets / f"Materials/Monument_{variant}.zmat").write_text(json.dumps(material, indent=2, sort_keys=True) + "\n")
    cells = []
    for z in range(args.rows):
        for x in range(args.columns):
            index = z * args.columns + x
            color = PALETTE[(x + z * 3) % len(PALETTE)]
            label_texture(assets / f"Textures/Ground_{x}_{z}.png", args.texture_size, color, f"{x}-{z}", (250, 250, 244))
            base = 30000000 + index * 100
            variant = index % args.models
            entities = [
                {"Entity": base + 1, "Tag": f"Ground {x},{z}", "Transform": {"Translation": [16, 0, 16], "Rotation": [0, 0, 0], "Scale": [31.6, 0.2, 31.6]},
                 "MeshRenderer": {"Mesh": "Cube", "AlbedoMap": f"Textures/Ground_{x}_{z}.png", "Albedo": [1, 1, 1, 1], "Roughness": 0.85, "Tiling": 2}},
                {"Entity": base + 2, "Tag": f"Monument {variant}", "Transform": {"Translation": [16, 0.1, 16], "Rotation": [0, (x * 0.7 + z * 1.3) % 6.28, 0], "Scale": [1, 1, 1]},
                 "MeshRenderer": {"Mesh": "Model", "Model": f"Models/Monument_{variant}.obj", "Material": f"Materials/Monument_{variant}.zmat", "CastShadows": True}},
            ]
            for k, (px, pz) in enumerate(((5, 5), (27, 5), (5, 27), (27, 27))):
                tint = [c / 255 for c in PALETTE[(index + k) % len(PALETTE)]] + [1]
                entities.append({"Entity": base + 10 + k, "Tag": "Pillar", "Transform": {"Translation": [px, 1.6 + k * 0.4, pz], "Rotation": [0, 0, 0], "Scale": [1.6, 3 + k * 0.8, 1.6]},
                                 "MeshRenderer": {"Mesh": "Cube", "Albedo": tint, "Roughness": 0.5, "Metallic": 0.1}})
            scene = {"Scene": f"Gallery {x},{z}", "Version": 5, "Entities": entities}
            (assets / "Scenes/Cells").mkdir(parents=True, exist_ok=True)
            (assets / f"Scenes/Cells/Cell_{x}_{z}.zscene").write_text(json.dumps(scene, indent=1) + "\n")
            bytes_gpu = args.texture_size * args.texture_size * 4
            cells.append({"ID": f"{index + 1:016x}", "Coordinate": [x, 0, z], "Layer": "main", "Scene": f"Scenes/Cells/Cell_{x}_{z}.zscene",
                          "CPUBytes": 1048576, "GPUBytes": bytes_gpu, "Dependencies": []})
    anchor = {"Scene": args.name, "Version": 5, "Entities": [
        {"Entity": 10000001, "Tag": "Gallery camera", "Transform": {"Translation": [16, 46, 30], "Rotation": [-1.12, 0, 0], "Scale": [1, 1, 1]},
         "Camera": {"ProjectionType": 0, "Primary": True, "PerspectiveFOV": 0.95, "PerspectiveNear": 0.1, "PerspectiveFar": 400,
                    "OrthographicSize": 32, "OrthographicNear": -100, "OrthographicFar": 100, "ClearColor": [0.06, 0.09, 0.14, 1]}},
        {"Entity": 10000002, "Tag": "Sun", "Transform": {"Translation": [0, 0, 0], "Rotation": [-0.9, -0.4, 0], "Scale": [1, 1, 1]},
         "DirectionalLight": {"Color": [1, 0.96, 0.86], "Intensity": 2.2, "CastShadows": True}}],
        "Settings": {"DrawSky": True, "Ambient": 0.65, "SkyTop": [0.12, 0.24, 0.42], "SkyHorizon": [0.55, 0.72, 0.82], "Ground": [0.12, 0.18, 0.24],
                     "ShadowDistance": 140}}
    (assets / "Scenes").mkdir(parents=True, exist_ok=True)
    (assets / "Scenes/Anchor.zscene").write_text(json.dumps(anchor, indent=1) + "\n")
    streaming = {"LoadRadius": 36, "UnloadRadius": 48, "AxisMask": 5, "Layers": ["main"], "MaxResidentCells": 12, "MaxResidentEntities": 4096,
                 "MaxEntitiesPerCell": 512, "MaxBuildingCells": 1, "MaxActivationsPerFrame": 1, "MaxUnloadsPerFrame": 2, "MainThreadMilliseconds": 3,
                 "MaxResolveEntitiesPerFrame": 64, "DeclaredCPUBytes": 268435456, "DeclaredGPUBytes": 1073741824, "CacheEntriesPerKind": 16,
                 "MaxCacheEvictionsPerFrame": 4, "ObservedRSSLimitBytes": 0, "RequestedGPULimitBytes": 0, "PressureResumeRatio": 0.85,
                 "EvictOnPressure": True, "PressureCooldownFrames": 120, "AssetStreaming": not args.legacy, "MaxAssetFilesPerCell": 64,
                 "MaxAssetUploadsPerFrame": 8, "MaxAssetUploadBytesPerFrame": 2097152, "MaxAssetReleaseBytesPerFrame": 16777216, "MaxAssetFileBytes": 33554432,
                 "MaxAssetBytesPerCell": 134217728, "MaxStagedAssetBytes": 268435456}
    world = {"Format": "ZaminWorld", "Version": 1, "Name": args.name, "Origin": [0, 0, 0], "CellSize": list(CELL), "Cells": cells, "Streaming": streaming}
    (assets / "Worlds").mkdir(parents=True, exist_ok=True)
    (assets / "Worlds/Main.zworld").write_text(json.dumps(world, indent=2) + "\n")
    (assets / "Worlds/Gallery.ztour").write_text(json.dumps(tour(args.columns, args.rows), indent=2) + "\n")
    config = {"Version": 2, "Project": {"Name": args.name, "StartScene": "Scenes/Anchor.zscene", "StreamingWorld": "Worlds/Main.zworld",
                                        "AssetDirectory": "Assets",
                                        "Description": "3D world streaming with real assets: budgeted texture/model streaming, residency limits and a repeatable camera tour.",
                                        "Thumbnail": "Thumbnail.png"},
              "Player": {"Product": args.name, "Width": width, "Height": height, "VSync": True},
              "BuildScenes": [{"Path": "Scenes/Anchor.zscene", "Enabled": True}], "Export": {"ExcludeUnusedAssets": True}}
    (project / f"{args.name}.zproject").write_text(json.dumps(config, indent=2) + "\n")
    if not (project / "Thumbnail.png").exists():
        label_texture(project / "Thumbnail.png", 256, PALETTE[1], "3-2", (250, 250, 244))
    print(f"{args.name}: {args.columns}x{args.rows} cells, {args.texture_size}px textures, {args.models} models "
          f"({2 * 24 * 12 * args.model_detail ** 2} triangles each) in {project}")


if __name__ == "__main__":
    main()
