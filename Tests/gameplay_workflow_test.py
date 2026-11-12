#!/usr/bin/env python3
"""Exercise the compiler-free motor/camera/checkpoint courses in Player, editor and exports."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ACCEPT = 'GAMEPLAY_ACCEPTANCE moved=1 jump=1 checkpoint=1 goal=1 restore=1'
TAPE = '15:hold=D;36:hold=Space;52:release=Space;85:release=D;95:E'


def validate(log, resumed=False):
    text = log.read_text(errors='replace')
    for marker in ('GL_INVALID_', 'Assertion failed', "Visual graph '", 'Visual graph error',
                   'GAMEPLAY_ORACLE_FAILED', 'GAMEPLAY_MISSING_'):
        if marker in text:
            raise RuntimeError(f'{marker}: {log}')
    if ('GAMEPLAY_RESUMED' if resumed else ACCEPT) not in text:
        raise RuntimeError(f'Missing gameplay acceptance: {log}')


def overrides(scene, auto, checks):
    data = json.loads(scene.read_text())
    for entity in data['Entities']:
        if entity['Entity'] in (800, 900):
            values = {'Auto': auto}
            if entity['Entity'] == 900:
                values['Checks'] = checks
            entity['NativeScript']['Fields']['Overrides'] = {'Type': 'String', 'Value': json.dumps(values)}
    scene.write_text(json.dumps(data, indent=2)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT/'build')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--no-editor', action='store_true')
    parser.add_argument('--exports', action='store_true')
    args = parser.parse_args()
    build = args.build.resolve()
    out = (args.out or build/'GameplayPackage/Workflow').resolve()
    out.mkdir(parents=True, exist_ok=True)
    runner = [] if os.environ.get('DISPLAY') else ['xvfb-run', '-a', '-s', '-screen 0 1600x1000x24 -nolisten tcp']
    report = []
    with tempfile.TemporaryDirectory(prefix='zamin-gameplay-') as temp:
        for course in ('Course2D', 'Course3D'):
            project = Path(temp)/course
            shutil.copytree(ROOT/'Projects/GameplayWorkshop', project, ignore=shutil.ignore_patterns('.zamin', 'Build'))
            manifest = project/'GameplayWorkshop.zproject'
            data = json.loads(manifest.read_text())
            assert not data['Project']['ScriptModulePath']
            data['Project']['StartScene'] = f'Scenes/{course}.zscene'
            manifest.write_text(json.dumps(data, indent=2)+'\n')
            scene = project/f'Assets/Scenes/{course}.zscene'
            home = Path(temp)/(course+'-home')
            home.mkdir()
            env = dict(os.environ, HOME=str(home), LIBGL_ALWAYS_SOFTWARE='1', LP_NUM_THREADS='2',
                       ZAMIN_GL_DEBUG='1', ZAMIN_CAPTURE_FPS='30', ZAMIN_MAX_FRAMES='180')
            for mode in ('player', 'resume') + (() if args.no_editor else ('editor',)):
                auto = mode == 'editor'
                # Each long run starts from the same original spawn; only the restart tests disk recovery.
                if auto:
                    shutil.rmtree(home/'.local', ignore_errors=True)
                overrides(scene, auto, mode != 'resume')
                png = out/f'{course}-{mode}.png'
                log = out/f'{course}-{mode}.log'
                runenv = dict(env, ZAMIN_SCREENSHOT=str(png), ZAMIN_MAX_FRAMES='60' if mode == 'resume' else '180')
                if auto:
                    runenv.update(ZAMIN_EDITOR_PLAY='1', ZAMIN_EDITOR_SELECT='Player')
                else:
                    runenv['ZAMIN_PLAYER_INPUT'] = '' if mode == 'resume' else TAPE
                cmd = [str(build/'bin'/('ZaminEditor' if auto else 'ZaminPlayer')), str(manifest)]
                if not auto:
                    cmd.append('--no-splash')
                with log.open('w') as stream:
                    subprocess.run(runner+cmd, env=runenv, stdout=stream, stderr=subprocess.STDOUT, timeout=300, check=True)
                validate(log, mode == 'resume')
                subprocess.run(['python3', str(ROOT/'Tests/png_check.py'), str(png)], check=True)
                checkpoints = list(home.rglob('gameplay-checkpoints/*.json'))
                if not checkpoints or any(p.stat().st_size > 4096 for p in checkpoints):
                    raise RuntimeError('Missing or oversized persistent checkpoint')
                report.append({'Scene': course, 'Mode': mode, 'NativeGameModule': False, 'Passed': True})
                print(f'PASS {course}/{mode}: motor, camera, jump, checkpoint, goal, restore', flush=True)
            if args.exports:
                overrides(scene, True, True)
                exportenv = dict(env, ZAMIN_BUILD_DIR=str(build))
                for key in ('ZAMIN_MAX_FRAMES', 'ZAMIN_SCREENSHOT', 'ZAMIN_EDITOR_PLAY', 'ZAMIN_EDITOR_SELECT', 'ZAMIN_PLAYER_INPUT'):
                    exportenv.pop(key, None)
                exportout = out/(course+'-export')
                subprocess.run(['bash', str(ROOT/'Tests/export_test.sh'), '--project', str(project),
                                '--frames', '180', '--shot-frames', '210', '--out', str(exportout)], env=exportenv, timeout=900, check=True)
                for phase in ('player.log', 'player-shot.log'):
                    validate(exportout/phase)
                report.append({'Scene': course, 'Mode': 'export-and-relocated-version-check', 'Passed': True})
    (out/'Report.json').write_text(json.dumps({'Passed': True, 'Checks': report}, indent=2)+'\n')


if __name__ == '__main__':
    main()
