#!/usr/bin/env python3
"""Real X11/ImGui 2D blend authoring: drag, save, Undo, create and Undo a field.
Run under xvfb-run -s '-screen 0 1600x1000x24 -nolisten tcp'. Requires xdotool/ImageMagick.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('ZAMIN_BUILD_DIR', str(ROOT / 'build'))).resolve()
OUT = BUILD / 'AnimatorGameplay/Authoring'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='zamin-animator-editor-') as scratch:
        work = Path(scratch)
        project = work / 'Project'
        shutil.copytree(ROOT / 'Projects/VisualWorkshop', project, ignore=shutil.ignore_patterns('Build', '.zamin'))
        manifest = project / 'VisualWorkshop.zproject'
        data = json.loads(manifest.read_text())
        data['Project']['StartScene'] = 'Scenes/Visual3D.zscene'
        manifest.write_text(json.dumps(data, indent=2)+'\n')
        home = work / 'home'
        home.mkdir()
        env = dict(os.environ, HOME=str(home), ZAMIN_EDITOR_SELECT='Animator gameplay actor',
                   ZAMIN_EDITOR_ANIMATOR_GRAPH='state=0,viewport', ZAMIN_MAX_FRAMES='10000',
                   LIBGL_ALWAYS_SOFTWARE='1', LP_NUM_THREADS='2')
        scene = project / 'Assets/Scenes/Visual3D.zscene'

        def x(*args):
            subprocess.run(['xdotool', *map(str, args)], check=True, stdout=subprocess.DEVNULL)

        def drag(start, end):
            x('mousemove', *start)
            time.sleep(2.3)
            x('mousedown', 1)
            time.sleep(2.3)
            x('mousemove', *end)
            time.sleep(2.3)
            x('mouseup', 1)
            time.sleep(2)

        def click(px, py):
            x('mousemove', px, py)
            time.sleep(2.3)
            x('mousedown', 1)
            time.sleep(2.3)
            x('mouseup', 1)
            time.sleep(2.3)

        def key(letter):
            x('keydown', 'ctrl')
            time.sleep(.3)
            x('key', letter)
            time.sleep(.4)
            x('keyup', 'ctrl')
            time.sleep(2.3)

        def actor():
            return next(e['Animator'] for e in json.loads(scene.read_text())['Entities'] if e.get('Entity') == 910)

        def screenshot(name):
            subprocess.run(['import', '-window', 'root', str(OUT / name)], check=True)

        with (OUT / 'editor.log').open('w') as log:
            app = subprocess.Popen([str(BUILD / 'bin/ZaminEditor'), str(manifest)], env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                for _ in range(200):
                    if app.poll() is not None:
                        raise RuntimeError('Editor exited before authoring test')
                    if 'Ishga tushish: birinchi kadr' in (OUT / 'editor.log').read_text():
                        break
                    time.sleep(.2)
                else:
                    raise RuntimeError('Editor first frame timed out')
                time.sleep(2)
                # Enlarge the default bottom dock so the blend field is visible.
                for _ in range(3):
                    drag((800, 601), (800, 180))
                    pixel = subprocess.check_output(['import', '-window', 'root', '-crop', '1x1+1080+743', '-depth', '8', 'rgb:-'])
                    if pixel[:3] == bytes((145, 105, 245)):
                        break
                else:
                    raise RuntimeError('Animator dock did not expand to the expected blend field')
                screenshot('expanded.png')
                drag((1080, 743), (1105, 723))
                key('s')
                point = actor()['States'][0]['Motions'][0]['Position']
                assert point[0] > .1 and point[1] > .1, point
                screenshot('dragged.png')
                print('PASS: drag 2D sample and save', point, flush=True)
                key('z')
                key('s')
                point = actor()['States'][0]['Motions'][0]['Position']
                assert abs(point[0]) < 1e-5 and abs(point[1]) < 1e-5, point
                print('PASS: scene Undo restores sample coordinates', flush=True)
                click(762, 230)
                key('s')
                data = actor()
                assert len(data['States']) == 3, data['States']
                state = data['States'][2]
                assert state['Type'] == 'BlendTree2D' and len(state['Motions']) == 3, state
                assert state['BlendParameter'] and state['BlendParameterY']
                screenshot('created.png')
                print('PASS: 2D blend toolbar creates state, motions and parameters', flush=True)
                key('z')
                key('s')
                assert len(actor()['States']) == 2
                print('PASS: scene Undo removes created blend field', flush=True)
                (OUT / 'Report.json').write_text(json.dumps({'Passed': True, 'DragSaveUndo': True, 'CreateStateUndo': True}, indent=2)+'\n')
            except BaseException:
                screenshot('failure.png')
                raise
            finally:
                app.terminate()
                try:
                    app.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    app.kill()
                    app.wait()


if __name__ == '__main__':
    main()
