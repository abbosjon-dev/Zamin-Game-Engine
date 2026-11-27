#!/usr/bin/env python3
"""Projects/StreamingWorkshop: a tiled, streamed terrain (8 x 8 tiles of 33 samples, 24 m each) with foliage in its
tiles, terrain layer textures (cooked smaller by the export) and gem sprites packed into a sprite atlas whose frames
the scene's Sprite Renderers and a Sprite Animation use. Deterministic, no external art.

    Tools/samples/gen_streaming_workshop.py            write the project (the atlas page needs build/bin/ZaminBench:
                                                       its --build-atlas helper packs it with the engine)
"""
from pathlib import Path
import hashlib, json, math, shutil, struct, subprocess, sys, zlib

repo = Path(__file__).resolve().parents[2]
project = repo / 'Projects/StreamingWorkshop'
assets = project / 'Assets'
TILES, RES, SIZE, LEVELS = 8, 33, 24.0, 4


def uuid(path):
    return hashlib.sha1(('StreamingWorkshop/' + path).encode()).hexdigest()[:16]


def meta(path, kind, imports=None):
    (assets / (path + '.zmeta')).write_text(json.dumps({'UUID': uuid(path), 'Type': kind, 'Import': imports or {}}, indent=4) + '\n')


def png(path, w, h, pixel):
    rows = b''.join(b'\0' + b''.join(bytes(pixel(x, y)) for x in range(w)) for y in range(h))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b'')
    (assets / path).parent.mkdir(parents=True, exist_ok=True)
    (assets / path).write_bytes(data)


def height(x, z):
    # Absolute positions: every tile computes its shared edge samples exactly like its neighbours.
    return 7.0 + 5.0 * math.sin(x * 0.045) * math.cos(z * 0.038) + 2.5 * math.sin((x + z) * 0.11) + 4.0 * math.exp(-((x - 96) ** 2 + (z - 96) ** 2) / 2200.0)


def tile(tx, tz):
    step = SIZE / (RES - 1)
    heights, weights = [], []
    for j in range(RES):
        for i in range(RES):
            h = height(tx * SIZE + i * step, tz * SIZE + j * step)
            heights.append(h)
            rock = min(1.0, max(0.0, (h - 9.0) / 4.0))
            weights.append((round((1 - rock) * 255), round(rock * 255), 0, 0))
    foliage = []
    seed = tz * 131 + tx * 17 + 5
    for k in range(10):
        seed = (seed * 1103515245 + 12345) & 0x7fffffff
        px = (seed % 1000) / 1000.0 * SIZE
        seed = (seed * 1103515245 + 12345) & 0x7fffffff
        pz = (seed % 1000) / 1000.0 * SIZE
        h = height(tx * SIZE + px, tz * SIZE + pz)
        if h < 10.5:
            foliage.append((px, h, pz, 0.0, float(seed % 360), 0.0, 0.8 + (seed % 7) / 10.0))
    levels = []
    for level in range(LEVELS):
        s = 1 << level
        r = (RES - 1) // s + 1
        data = b''.join(struct.pack('<f', heights[(j * s) * RES + i * s]) for j in range(r) for i in range(r))
        data += b''.join(bytes(weights[(j * s) * RES + i * s]) for j in range(r) for i in range(r))
        levels.append(data)
    out = b'ZTIL' + struct.pack('<IIIII', 1, RES, LEVELS, len(foliage), 0)
    offset = len(out) + (LEVELS + 1) * 16
    table = b''
    for data in levels:
        table += struct.pack('<QQ', offset, len(data))
        offset += len(data)
    fdata = b''.join(struct.pack('<7f', *f) for f in foliage)
    table += struct.pack('<QQ', offset, len(fdata))
    return out + table + b''.join(levels) + fdata, len(foliage)


def main():
    thumbnail = (project / 'Thumbnail.png').read_bytes() if (project / 'Thumbnail.png').exists() else None  # a Player screenshot, kept
    if project.exists():
        shutil.rmtree(project)
    (assets / 'Maps/Valley.tiles').mkdir(parents=True)
    counts = []
    for tz in range(TILES):
        for tx in range(TILES):
            data, count = tile(tx, tz)
            (assets / f'Maps/Valley.tiles/{tx}_{tz}.ztile').write_bytes(data)
            counts.append(count)
    layers = [
        {'Color': [0.25, 0.46, 0.17], 'TexturePath': 'Textures/Terrain/grass-albedo.png', 'Tiling': 0.25, 'NormalMapPath': '', 'MaskMapPath': '',
         'Roughness': 1.0, 'Metallic': 0.0, 'Occlusion': 1.0, 'NormalStrength': 1.0},
        {'Color': [0.46, 0.47, 0.49], 'TexturePath': 'Textures/Terrain/rock-albedo.png', 'Tiling': 0.25, 'NormalMapPath': '', 'MaskMapPath': '',
         'Roughness': 1.0, 'Metallic': 0.0, 'Occlusion': 1.0, 'NormalStrength': 1.0},
        {'Color': [0.4, 0.27, 0.15], 'TexturePath': '', 'Tiling': 0.25, 'NormalMapPath': '', 'MaskMapPath': '', 'Roughness': 0.95, 'Metallic': 0.0,
         'Occlusion': 1.0, 'NormalStrength': 1.0},
        {'Color': [0.78, 0.68, 0.43], 'TexturePath': '', 'Tiling': 0.25, 'NormalMapPath': '', 'MaskMapPath': '', 'Roughness': 0.95, 'Metallic': 0.0,
         'Occlusion': 1.0, 'NormalStrength': 1.0}]
    terrain = {'Version': 1, 'Tiles': [TILES, TILES], 'TileResolution': RES, 'TileSize': SIZE, 'HeightLimit': 128.0, 'LODLevels': LEVELS,
               'TilesFolder': 'Valley.tiles', 'Layers': layers,
               'Foliage': {'NearType': 'Cone', 'FarType': 'Cube', 'NearModel': '', 'FarModel': '', 'Tint': [0.3, 0.6, 0.18, 1.0], 'LODDistance': 40.0,
                           'CullDistance': 150.0},
               'FoliageCounts': counts}
    (assets / 'Maps/Valley.zterrain').write_text(json.dumps(terrain, indent=2) + '\n')
    # Terrain layer textures (512 px; the export halves them to its 256 px limit).
    for name, base in (('grass', (70, 120, 45)), ('rock', (120, 118, 112))):
        path = f'Textures/Terrain/{name}-albedo.png'
        png(path, 512, 512, lambda x, y, b=base: (min(255, b[0] + (x * 7 + y * 13) % 23), min(255, b[1] + (x * 11 + y * 5) % 19), min(255, b[2] + (x ^ y) % 17), 255))
        meta(path, 'Texture', {'Filter': 'Linear', 'Wrap': 'Repeat', 'Mipmaps': True, 'sRGB': True})
    # Gem sprites: a four-frame turn, packed into Sprites/Gems.atlas.png by the engine.
    for frame in range(4):
        def gem(x, y, f=frame):
            dx, dy = abs(x - 7.5), abs(y - 7.5)
            width = 6.5 * abs(math.cos(f * math.pi / 4)) + 1.0
            if dx / width + dy / 7.0 > 1.0:
                return (0, 0, 0, 0)
            shine = 1.0 if (x - 7.5) * math.cos(f) + (y - 4) < 0 else 0.7
            return (int(80 * shine), int(200 * shine), int(255 * shine), 255)
        path = f'Sprites/Gems/gem_{frame}.png'
        png(path, 16, 16, gem)
        meta(path, 'Texture', {'Filter': 'Nearest', 'Wrap': 'Clamp', 'Mipmaps': False, 'sRGB': True})
    helper = repo / 'build/bin/ZaminBench'
    if not helper.exists() or subprocess.run([str(helper), '--build-atlas', str(assets), 'Sprites/Gems']).returncode != 0:
        sys.exit('the atlas page was not built (build ZaminBench first)')
    page = json.loads((assets / 'Sprites/Gems.atlas.png.zmeta').read_text())
    page['UUID'] = uuid('Sprites/Gems.atlas.png')
    (assets / 'Sprites/Gems.atlas.png.zmeta').write_text(json.dumps(page, indent=4) + '\n')

    def entity(id_, tag, transform, **components):
        e = {'Entity': id_, 'Tag': tag, 'Transform': {'Translation': transform[0], 'Rotation': transform[1], 'Scale': transform[2]}}
        e.update(components)
        return e
    gem_ids = []
    entities = [
        entity(101, 'Main Camera', ([40.0, 28.0, 40.0], [-0.42, -2.356, 0.0], [1, 1, 1]),
               Camera={'Primary': True, 'ProjectionType': 0, 'PerspectiveFOV': 0.9, 'PerspectiveNear': 0.1, 'PerspectiveFar': 600.0,
                       'ClearColor': [0.45, 0.6, 0.8, 1.0]}),
        entity(102, 'Sun', ([0, 0, 0], [-0.7, 0.6, 0.0], [1, 1, 1]), DirectionalLight={'CastShadows': True, 'Color': [1.0, 0.96, 0.9], 'Intensity': 3.0}),
        entity(103, 'Valley', ([0, 0, 0], [0, 0, 0], [1, 1, 1]),
               TiledTerrain={'Path': 'Maps/Valley.zterrain', 'PathUUID': uuid('Maps/Valley.zterrain'), 'LoadRadius': 150.0, 'UnloadRadius': 180.0,
                             'LODDistance': 40.0, 'MaxLOD': 3, 'MemoryBudgetMB': 32, 'MaxLoadsPerFrame': 4, 'Collision': True, 'CollisionRadius': 80.0,
                             'FoliageDistance': 140.0, 'ShowTileLOD': False}),
    ]
    for i in range(6):
        x, z = 52.0 + i * 5.0, 48.0 + (i % 3) * 6.0
        gem_ids.append(200 + i)
        sprite = {'Texture': 'Sprites/Gems.atlas.png', 'TextureUUID': uuid('Sprites/Gems.atlas.png'), 'Sprite': f'gem_{i % 4}', 'PixelArt': True}
        extra = {'SpriteAnimation': {'Frames': ['gem_0', 'gem_1', 'gem_2', 'gem_3'], 'FPS': 6.0, 'Loop': True, 'Playing': True}} if i == 0 else {}
        entities.append(entity(200 + i, f'Gem {i}', ([x, height(x, z) + 2.5, z], [0, -2.356, 0], [2.5, 2.5, 1]), SpriteRenderer=sprite, **extra))
    scene = {'Scene': 'Valley', 'Version': 5, 'RootOrder': [e['Entity'] for e in entities],
             'Settings': {'Ambient': 0.6, 'DrawSky': True, 'ShadowDistance': 80.0}, 'Entities': entities}
    (assets / 'Scenes').mkdir(parents=True, exist_ok=True)
    (assets / 'Scenes/Valley.zscene').write_text(json.dumps(scene, indent=4) + '\n')
    meta('Scenes/Valley.zscene', 'Scene')
    (project / 'StreamingWorkshop.zproject').write_text(json.dumps({
        'Version': 2,
        'Project': {'Name': 'StreamingWorkshop', 'StartScene': 'Scenes/Valley.zscene', 'AssetDirectory': 'Assets', 'SourceDirectory': 'Source',
                    'ScriptModulePath': '', 'EngineVersion': '0.1.0',
                    'Description': 'A tiled terrain streamed around the camera, terrain textures cooked smaller and gem sprites from a sprite atlas.',
                    'Thumbnail': 'Thumbnail.png'},
        'Player': {'Product': 'Zamin Streaming Workshop', 'Width': 1280, 'Height': 720, 'VSync': True},
        'Export': {'ExcludeUnusedAssets': True, 'AlwaysInclude': [], 'ReleaseScriptModule': True, 'StripBinaries': True,
                   'Cook': {'Enabled': True, 'Compress': True, 'MaxTextureSize': 256, 'MatchQuality': True, 'Always': True}},
    }, indent=4) + '\n')
    if thumbnail:
        (project / 'Thumbnail.png').write_bytes(thumbnail)
    (project / 'README.md').write_text(
        '# Streaming Workshop\n\n'
        'Open `StreamingWorkshop.zproject` with ZaminEditor. `Assets/Scenes/Valley.zscene` has a **Valley** entity with a Tiled terrain '
        'component: `Assets/Maps/Valley.zterrain` and its 64 tiles (8 x 8 of 33 samples, 24 m each, 4 LOD levels, foliage in the tiles) '
        'stream around the camera within a 32 MB budget. Select it and turn on **Tile LOD colours** to see the levels. The gems are '
        'frames of the sprite atlas `Assets/Sprites/Gems.zatlas` (packed from `Assets/Sprites/Gems/`); one of them plays the frames as a '
        'Sprite Animation. The export cooks every texture (the 512 px terrain textures are halved to the 256 px limit) and leaves the '
        'unused source sprites out. Deterministic source: `Tools/samples/gen_streaming_workshop.py`.\n')


if __name__ == '__main__':
    main()
