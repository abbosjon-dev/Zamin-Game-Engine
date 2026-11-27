#!/usr/bin/env python3
"""Real X11/ImGui regression of the 2D content workflow and the tiled terrain editor:
  1. Sprite Editor: a sheet sliced automatically by alpha (three islands, a glint merged), a frame's pivot edited, saved;
  2. Content browser: a folder made into a sprite atlas from its context menu; a source repainted on disk - the page is
     rebuilt keeping its UUID and frame names (Sprite Renderers keep finding them);
  3. Tiled terrain: the inspector's streaming stats, the tile LOD colours in the viewport, the Statistics panel, a brush
     stroke across tile borders saved with Ctrl+S (the tiles' shared edge samples stay equal on disk).
Run under xvfb-run with a 1600x1000x24 screen. Requires xdotool and ImageMagick. ZAMIN_BUILD_DIR chooses the build
(default build); Projects/StreamingWorkshop is copied to a private temp directory. Widgets are found through the
rectangles the editor writes (ZAMIN_EDITOR_VISUAL_LAYOUT), never fixed coordinates; screenshots go to
<build>/AtlasTerrainEditor (--out DIR).
"""
import argparse, json, math, os, shutil, struct, subprocess, tempfile, time, zlib
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--out', default=None)
args = parser.parse_args()
repo = Path(__file__).resolve().parent.parent
build = Path(os.environ.get('ZAMIN_BUILD_DIR', str(repo / 'build'))).resolve()
out = Path(args.out).resolve() if args.out else build / 'AtlasTerrainEditor'
out.mkdir(parents=True, exist_ok=True)
temporary = tempfile.TemporaryDirectory(prefix='zamin-atlas-terrain-ui-')
work = Path(temporary.name)
project = work / 'Project'
shutil.copytree(repo / 'Projects/StreamingWorkshop', project, ignore=shutil.ignore_patterns('.zamin', 'Build'))
assets = project / 'Assets'
(work / 'home').mkdir(exist_ok=True)


def png(path, w, h, pixel):
    rows = b''.join(b'\0' + b''.join(bytes(pixel(x, y)) for x in range(w)) for y in range(h))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


# A sheet with three islands of visible pixels (and a glint one pixel from the first: merged into it).
def sheet(x, y):
    inside = (4 <= x <= 19 and 4 <= y <= 19) or (x == 21 and y == 8) or (28 <= x <= 41 and 6 <= y <= 25) or (48 <= x <= 59 and 2 <= y <= 13)
    return (230, 180, 40, 255) if inside else (0, 0, 0, 0)


png(assets / 'Sprites/Sheet/items.png', 64, 32, sheet)
for i in range(3):
    png(assets / f'Sprites/Coins/coin_{i}.png', 12, 12, lambda x, y, i=i: (200, 160 + i * 30, 30, 255) if (x - 5.5) ** 2 + (y - 5.5) ** 2 < 30 else (0, 0, 0, 0))
layout = work / 'layout.json'
env = dict(os.environ, HOME=str(work / 'home'), LIBGL_ALWAYS_SOFTWARE='1', GALLIUM_DRIVER='llvmpipe', ZAMIN_EDITOR_VISUAL_LAYOUT=str(layout),
           ZAMIN_EDITOR_SPRITE_EDITOR='Sprites/Sheet/items.png|alpha|prefix=item', ZAMIN_EDITOR_SELECT='Valley', ZAMIN_EDITOR_STATS='1', ZAMIN_EDITOR_FOCUS='SpriteEditor',
           ZAMIN_EDITOR_CAMERA='96,8,96,150,40,225', ZAMIN_MAX_FRAMES='100000')
log_path = work / 'editor.log'
log = log_path.open('w')
app = subprocess.Popen([str(build / 'bin/ZaminEditor'), str(project / 'StreamingWorkshop.zproject')], env=env, stdout=log, stderr=subprocess.STDOUT)


def x(*a):
    subprocess.run(['xdotool', *map(str, a)], check=True, stdout=subprocess.DEVNULL)


def text():
    return log_path.read_text(errors='replace')


def items():
    for _ in range(30):
        try:
            return json.loads(layout.read_text())['Items']
        except (OSError, ValueError, KeyError):
            time.sleep(.3)
    return {}


def rect(key, tries=60):
    for _ in range(tries):
        if app.poll() is not None:
            raise RuntimeError('Editor exited early')
        found = items().get(key)
        if found:
            return found['Rect']
        time.sleep(.5)
    raise RuntimeError('Widget not found: ' + key + ' (have: ' + ', '.join(sorted(items().keys())[:60]) + ')')


def center(key):
    r = rect(key)
    return int((r[0] + r[2]) / 2), int((r[1] + r[3]) / 2)


def regions():
    for _ in range(30):
        try:
            return json.loads(layout.read_text())['Regions']
        except (OSError, ValueError, KeyError):
            time.sleep(.3)
    return {}


def reach(key, region):
    # Scrolls the region (mouse wheel over its scrollbar, right of its inner rectangle) until the widget is inside it, then returns its centre.
    for _ in range(40):
        r, area = rect(key), regions().get(region)
        if area and r[3] > area[3] - 2:
            x('mousemove', int(area[2]) + 4, int((area[1] + area[3]) / 2)); time.sleep(.3); x('click', 5); time.sleep(.8)
        elif area and r[1] < area[1] + 2:
            x('mousemove', int(area[2]) + 4, int((area[1] + area[3]) / 2)); time.sleep(.3); x('click', 4); time.sleep(.8)
        else:
            return int((r[0] + r[2]) / 2), int((r[1] + r[3]) / 2)
    raise RuntimeError('could not scroll to ' + key)


def click(px, py, button=1, ctrl=False):
    # Give GLFW a frame to see the pointer before the button on a slow software render.
    x('mousemove', px, py)
    time.sleep(.8)
    if ctrl:
        x('keydown', 'ctrl')
    x('mousedown', button)
    time.sleep(.8)
    x('mouseup', button)
    if ctrl:
        x('keyup', 'ctrl')
    time.sleep(1.2)


def shot(name):
    subprocess.run(['import', '-window', 'root', str(out / name)], check=True)


def share(name, region, expression):
    rx, ry, rw, rh = region
    value = subprocess.check_output(['convert', str(out / name), '-crop', f'{rw}x{rh}+{rx}+{ry}', '+repage', '-fx', expression, '-format', '%[fx:mean]', 'info:'],
                                    text=True)
    return float(value)


def meta(path):
    return json.loads((assets / (path + '.zmeta')).read_text())


def wait(condition, what, seconds=40):
    for _ in range(int(seconds * 4)):
        if condition():
            return
        if app.poll() is not None:
            raise RuntimeError('Editor exited early')
        time.sleep(.25)
    raise RuntimeError('Timed out: ' + what)


def tile_level0(path):
    data = path.read_bytes()
    res, levels = struct.unpack_from('<II', data, 8)
    offset, size = struct.unpack_from('<QQ', data, 24)
    return res, struct.unpack_from(f'<{res * res}f', data, offset)


try:
    wait(lambda: 'Scene opened:' in text() and 'sprite/slice' in items() and 'tiled/stats' in items(), 'the editor, the Sprite Editor and the Tiled terrain inspector', 120)
    time.sleep(2)
    # ---- 1. alpha slicing and a pivot
    click(*reach('sprite/slice', 'sprite-sidebar'))
    wait(lambda: 'sprite/pivot-frame' in items(), 'a sliced frame selected')
    for attempt in range(3):
        reach('sprite/pivot-frame', 'sprite-sidebar')
        r = rect('sprite/pivot-frame')
        click(int(r[0] + (r[2] - r[0]) * 0.2), int((r[1] + r[3]) / 2), ctrl=True)  # Ctrl+click: the drag field becomes a text field
        x('key', 'ctrl+a')
        x('type', '--delay', '120', '0.25')
        x('key', 'Return')
        time.sleep(1.5)
        click(*center('sprite/save'))
        frames = meta('Sprites/Sheet/items.png').get('Import', {}).get('Sprites', [])
        if frames and abs(frames[0]['Pivot'][0] - 0.25) < 1e-3:
            break
    frames = meta('Sprites/Sheet/items.png')['Import']['Sprites']
    assert len(frames) == 3, frames
    assert frames[0]['Rect'] == [4, 4, 18, 16], frames[0]
    assert abs(frames[0]['Pivot'][0] - 0.25) < 1e-3, frames[0]
    print('UI: alpha slicing gave 3 frames, pivot edited and saved', flush=True)
    shot('atlas-slicing-editor.png')

    # ---- 2. a sprite atlas from a folder (content browser context menu), rebuilt when a source changes
    click(*center('tab/ContentBrowser'))
    r = rect('browser/Assets')
    x('mousemove', int((r[0] + r[2]) / 2), int((r[1] + r[3]) / 2))
    time.sleep(.5)
    x('click', '--repeat', '2', '--delay', '120', '1')
    wait(lambda: 'browser/Sprites' in items(), 'the Assets folder opened')
    time.sleep(1)
    r = rect('browser/Sprites')
    x('mousemove', int((r[0] + r[2]) / 2), int((r[1] + r[3]) / 2))
    time.sleep(.5)
    x('click', '--repeat', '2', '--delay', '120', '1')
    wait(lambda: 'browser/Coins' in items(), 'the Sprites folder opened')
    time.sleep(1)
    click(*center('browser/Coins'), button=3)
    click(*center('menu/create-atlas'))
    wait(lambda: (assets / 'Sprites/Coins.atlas.png.zmeta').exists(), 'the atlas page')
    page = meta('Sprites/Coins.atlas.png')
    names = sorted(f['Name'] for f in page['Import']['Sprites'])
    assert names == ['coin_0', 'coin_1', 'coin_2'], names
    uuid = page['UUID']
    wait(lambda: 'atlas/rebuild' in items(), 'the atlas inspector')
    time.sleep(2)
    shot('atlas-inspector.png')
    # A source repainted bigger: the editor packs the page again by itself (UUID and names kept).
    png(assets / 'Sprites/Coins/coin_1.png', 20, 14, lambda px, py: (90, 220, 120, 255))
    rebuilt = lambda: any(f['Name'] == 'coin_1' and f['Rect'][2] == 20 for f in meta('Sprites/Coins.atlas.png')['Import']['Sprites'])
    wait(rebuilt, 'the automatic rebuild', 30)
    print('UI: atlas rebuilt automatically after the source changed', flush=True)
    page = meta('Sprites/Coins.atlas.png')
    assert page['UUID'] == uuid and sorted(f['Name'] for f in page['Import']['Sprites']) == names
    time.sleep(2)
    shot('atlas-inspector-rebuilt.png')

    # ---- 3. tiled terrain: stats, LOD colours, the Statistics panel (Valley selected again: a click on the terrain)
    viewport = rect('viewport')
    click(int((viewport[0] + viewport[2]) / 2), int(viewport[1] + (viewport[3] - viewport[1]) * 0.75))
    wait(lambda: 'tiled/lod-colors' in items(), 'the Tiled terrain inspector again')
    wait(lambda: 'stats/tiles' in items(), 'the Statistics panel tile lines')
    shot('tiled-terrain-streaming-stats.png')
    viewport = rect('viewport')
    region = (int(viewport[0]), int(viewport[1]), int(viewport[2] - viewport[0]), int(viewport[3] - viewport[1]))
    warm = '(r>0.6&&g<0.6&&b<0.3)?1:0'  # the orange / red of the coarse LOD colours
    before = share('tiled-terrain-streaming-stats.png', region, warm)
    for attempt in range(3):
        click(*center('tiled/lod-colors'))
        time.sleep(4)
        shot('tiled-terrain-lod-debug.png')
        after = share('tiled-terrain-lod-debug.png', region, warm)
        if after > before + 0.01:
            break
    else:
        raise RuntimeError(f'the tile LOD colours did not show ({before:.4f} -> {after:.4f})')
    print(f'UI: tile LOD colours: warm share {before:.4f} -> {after:.4f} of the viewport', flush=True)
    click(*center('tiled/lod-colors'))
    time.sleep(2)

    # ---- 4. a brush stroke across tile borders, saved with Ctrl+S
    tiles = sorted((assets / 'Maps/Valley.tiles').glob('*.ztile'))
    stamps = {p.name: p.stat().st_mtime_ns for p in tiles}
    click(*center('tiled/terrain-window'))
    wait(lambda: 'terrain/brush' in items(), 'the map window brush')
    click(*center('terrain/brush'))
    time.sleep(1)
    cx, cy = int((viewport[0] + viewport[2]) / 2), int((viewport[1] + viewport[3]) / 2)
    x('mousemove', cx - 220, cy)
    time.sleep(1)
    x('mousedown', 1)
    for step in range(45):
        x('mousemove', cx - 220 + step * 10, cy + (step % 5))
        time.sleep(.12)
    x('mouseup', 1)
    time.sleep(1.5)
    x('mousemove', cx, cy)
    time.sleep(.5)
    x('key', 'ctrl+s')
    wait(lambda: sum(p.stat().st_mtime_ns != stamps[p.name] for p in tiles) >= 2, 'edited tiles written', 30)
    changed = [p for p in tiles if p.stat().st_mtime_ns != stamps[p.name]]
    # Shared edges of neighbouring tiles are the same samples on disk.
    data = {p.name: tile_level0(p) for p in tiles}
    worst = 0.0
    for name, (res, h) in data.items():
        tx, tz = map(int, name[:-6].split('_'))
        right = data.get(f'{tx + 1}_{tz}.ztile')
        if right:
            worst = max(worst, max(abs(h[k * res + res - 1] - right[1][k * res]) for k in range(res)))
        down = data.get(f'{tx}_{tz + 1}.ztile')
        if down:
            worst = max(worst, max(abs(h[(res - 1) * res + k] - down[1][k]) for k in range(res)))
    assert worst == 0.0, f'shared edge samples differ by {worst}'
    print(f'UI: brush stroke across borders: {len(changed)} tiles saved, shared edges equal', flush=True)
    time.sleep(2)
    shot('tiled-terrain-sculpt.png')
    print('PASS atlas/terrain editor: alpha slicing + pivot, atlas from folder + rebuild, tiled terrain stats/LOD colours/sculpt', flush=True)
except BaseException:
    shot('editor-ui-failure.png')
    raise
finally:
    app.terminate()
    try:
        app.wait(timeout=10)
    except subprocess.TimeoutExpired:
        app.kill()
        app.wait()
    log.close()
    shutil.copyfile(log_path, out / 'editor.log')
    if layout.exists():
        shutil.copyfile(layout, out / 'layout.json')
    temporary.cleanup()
