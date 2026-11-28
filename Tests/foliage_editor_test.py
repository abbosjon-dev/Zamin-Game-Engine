#!/usr/bin/env python3
"""Real X11/ImGui packed foliage inspector regression: GPU/CPU culling, HLOD distance/cell, the LOD/HLOD debug view.
Run under xvfb-run with a 1600x1000x24 screen. Requires xdotool and ImageMagick.
ZAMIN_BUILD_DIR chooses the build (default build); FoliageWorkshop is copied to a private temp directory.
The editor logs its foliage controls' rectangles (ZAMIN_EDITOR_FOLIAGE_RECTS) and the test clicks/types into them,
saves with Ctrl+S, reads the saved scene back, undoes, and keeps screenshots in <build>/FoliageEditor (--out DIR).
"""
import argparse, json, os, re, shutil, subprocess, tempfile, time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--out', default=None)
args = parser.parse_args()
repo = Path(__file__).resolve().parent.parent
build = Path(os.environ.get('ZAMIN_BUILD_DIR', str(repo / 'build'))).resolve()
out = Path(args.out).resolve() if args.out else build / 'FoliageEditor'
out.mkdir(parents=True, exist_ok=True)
temporary = tempfile.TemporaryDirectory(prefix='zamin-foliage-ui-')
work = Path(temporary.name)
project = work / 'Project'
shutil.copytree(repo / 'Projects/FoliageWorkshop', project, ignore=shutil.ignore_patterns('.zamin', 'Build'))
(work / 'home').mkdir(exist_ok=True)
scene_file = project / 'Assets/Scenes/Forest.zscene'
env = dict(os.environ, HOME=str(work / 'home'), LIBGL_ALWAYS_SOFTWARE='1', GALLIUM_DRIVER='llvmpipe', ZAMIN_EDITOR_SELECT='Packed Forest',
           ZAMIN_EDITOR_FOLIAGE_RECTS='1', ZAMIN_EDITOR_STATS='1', ZAMIN_EDITOR_CAMERA='32,0,32,55,30,220', ZAMIN_EDITOR_COLLIDERS='0', ZAMIN_MAX_FRAMES='100000')
log_path = work / 'editor.log'
log = log_path.open('w')
app = subprocess.Popen([str(build / 'bin/ZaminEditor'), str(project / 'FoliageWorkshop.zproject')], env=env, stdout=log, stderr=subprocess.STDOUT)


def x(*a):
    subprocess.run(['xdotool', *map(str, a)], check=True, stdout=subprocess.DEVNULL)


def text():
    return log_path.read_text(errors='replace')


def rects():
    found = {}
    for m in re.finditer(r'FOLIAGE_UI (\S+) (-?\d+) (-?\d+) (\d+) (\d+)', text()):
        found[m.group(1)] = tuple(int(v) for v in m.group(2, 3, 4, 5))
    return found


def center(name):
    # The item rectangle includes the label on the right: aim into the field's left part.
    rx, ry, w, h = rects()[name]
    return rx + min(w // 4, 40), ry + h // 2


def click(px, py, ctrl=False):
    # Give GLFW a frame to see the pointer before the button on a slow software render.
    x('mousemove', px, py)
    time.sleep(1)
    if ctrl:
        x('keydown', 'ctrl')
    x('mousedown', 1)
    time.sleep(1.2)
    x('mouseup', 1)
    if ctrl:
        x('keyup', 'ctrl')
    time.sleep(1.5)


def type_value(name, value):
    click(*center(name), ctrl=True)  # Ctrl+click: the drag field becomes a text field
    x('key', 'ctrl+a')
    x('type', '--delay', '120', str(value))
    x('key', 'Return')
    time.sleep(2)


def foliage():
    for e in json.loads(scene_file.read_text())['Entities']:
        if 'Foliage' in e:
            return e['Foliage']
    raise RuntimeError('no foliage component in the saved scene')


def save_until(check, what, tries=5):
    for attempt in range(tries):
        x('mousemove', 800, 500)
        time.sleep(.5)
        x('key', 'ctrl+s')
        time.sleep(2.5)
        f = foliage()
        if check(f):
            print('UI: ' + what + ' saved', flush=True)
            return f
    raise RuntimeError(what + ' not saved: ' + json.dumps({k: v for k, v in foliage().items() if k != 'Instances'}))


def shot(name):
    subprocess.run(['import', '-window', 'root', str(out / name)], check=True)


def magenta(name):
    # The share of strongly magenta pixels in the viewport (ImageMagick -fx; the viewport's place in the default layout).
    value = subprocess.check_output(['convert', str(out / name), '-crop', '960x540+292+58', '+repage', '-fx', '(r>g+0.25&&b>g+0.2)?1:0',
                                     '-format', '%[fx:mean]', 'info:'], text=True)
    return float(value)


def magenta_now():
    shot('editor-ui.png')
    return magenta('editor-ui.png')


try:
    for i in range(600):
        if app.poll() is not None:
            raise RuntimeError('Editor exited early')
        if 'Scene opened:' in text() and 'FOLIAGE_UI culling' in text() and 'FOLIAGE_UI hlod-cell' in text():
            break
        time.sleep(.2)
    else:
        raise RuntimeError('The foliage inspector did not appear')
    time.sleep(3)
    assert foliage()['Backend'] == 2, 'the sample starts on the GPU path'
    # Scroll the inspector so the whole foliage section is on screen (the rectangles are logged again).
    x('mousemove', 1420, 500)
    time.sleep(1)
    for i in range(6):
        x('click', 5)
        time.sleep(.4)
    time.sleep(2)
    rx, ry, w, h = rects()['debug']
    assert ry + h < 860, f'the debug checkbox is still below the inspector ({ry})'
    shot('foliage-inspector-gpu.png')
    # HLOD on: distance, transition and cell through the inspector's fields.
    for name, key, value in (('lod', 'LODDistance', 15), ('hlod', 'HLODDistance', 30), ('hlod-fade', 'HLODFade', 8), ('hlod-cell', 'HLODCellSize', 16)):
        for attempt in range(3):
            type_value(name, value)
            try:
                save_until(lambda f: f.get(key) == value, key, tries=2)
                break
            except RuntimeError:
                if attempt == 2:
                    raise
    assert foliage().get('LODDistance') == 15 and foliage().get('HLODDistance') == 30 and foliage().get('HLODFade') == 8 and foliage().get('HLODCellSize') == 16
    time.sleep(3)
    shot('foliage-hlod-gpu.png')
    # CPU culling: open the combo and pick its second entry.
    for attempt in range(3):
        cx, cy = center('culling')
        h = rects()['culling'][3]
        click(cx, cy)
        click(cx, cy + h + h // 2 + (h - 2) * 1 + attempt * 2)
        x('key', 'Escape')
        try:
            save_until(lambda f: f['Backend'] == 1, 'CPU culling')
            break
        except RuntimeError:
            if attempt == 2:
                raise
    time.sleep(3)
    shot('foliage-hlod-cpu.png')
    # Undo takes the backend back to GPU and keeps the HLOD settings.
    # A key press can be lost under xvfb; Ctrl+Z is sent again only when the saved scene shows it did not land
    # (the backend still CPU), so a second press can never undo the earlier HLOD edit.
    for attempt in range(3):
        x('mousemove', 800, 500)
        time.sleep(.5)
        x('key', 'ctrl+z')
        time.sleep(2)
        try:
            save_until(lambda f: f['Backend'] == 2 and f.get('HLODDistance') == 30, 'Undo of the culling change', tries=2)
            break
        except RuntimeError:
            if attempt == 2 or foliage()['Backend'] != 1:
                raise
    # The LOD/HLOD debug colours (a renderer view setting: not saved in the scene): the viewport turns magenta (HLOD)
    # and yellow (far LOD) where it was green.
    before = magenta('foliage-hlod-cpu.png')
    for attempt in range(3):
        rx, ry, w, h = rects()['debug']
        click(rx + h // 2, ry + h // 2)
        time.sleep(4)
        shot('foliage-lod-debug.png')
        after = magenta('foliage-lod-debug.png')
        if after > before + 0.01:
            break
    else:
        raise RuntimeError(f'the debug view did not colour the proxies (magenta {before:.4f} -> {after:.4f})')
    print(f'UI: LOD/HLOD debug view: magenta {before:.4f} -> {after:.4f} of the viewport', flush=True)
    click(rx + h // 2, ry + h // 2)
    time.sleep(3)
    assert magenta_now() < after / 2, 'the debug view turned off'
    shot('editor-ui.png')
    print('PASS foliage editor: HLOD fields, CPU/GPU combo, Undo, debug view', flush=True)
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
    temporary.cleanup()
