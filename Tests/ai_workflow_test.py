#!/usr/bin/env python3
"""Real 2D/3D AI patrol/perception/combat/memory/return/prefab acceptance in Player, editor and exports."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ACCEPT = 'AI_ACCEPTANCE patrol=1 chase=1 attack=1 investigate=1 return=1 dead=1 spawn=1'
TAPE = '65:E;130:hold=D;155:E;190:release=D;200:E;300:LeftControl'


def validate(log):
    text = log.read_text(errors='replace')
    for marker in ('GL_INVALID_', 'Assertion failed', "Visual graph '", 'Visual graph error', 'AI_CONFIG_FAILED', 'AI_ORACLE_FAILED', 'AI_MISSING_'):
        if marker in text:
            raise RuntimeError(f'{marker}: {log}')
    if ACCEPT not in text:
        raise RuntimeError(f'Missing AI patrol/combat acceptance: {log}')


def overrides(scene, auto):
    data = json.loads(scene.read_text())
    for entity in data['Entities']:
        if entity['Entity'] in (800, 900):
            values = {'Auto': auto}
            if entity['Entity'] == 900:
                values['Checks'] = True
            entity['NativeScript']['Fields']['Overrides'] = {'Type': 'String', 'Value': json.dumps(values)}
    scene.write_text(json.dumps(data, indent=2)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT/'build')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--no-editor', action='store_true')
    parser.add_argument('--exports', action='store_true')
    parser.add_argument('--scene', choices=('AI2D', 'AI3D'))
    args = parser.parse_args()
    build = args.build.resolve()
    out = (args.out or build/'AIGameplay/Workflow').resolve()
    out.mkdir(parents=True, exist_ok=True)
    runner = [] if os.environ.get('DISPLAY') else ['xvfb-run', '-a', '-s', '-screen 0 1600x1000x24 -nolisten tcp']
    report = []
    with tempfile.TemporaryDirectory(prefix='zamin-ai-') as temp:
        for course in ((args.scene,) if args.scene else ('AI2D', 'AI3D')):
            project = Path(temp)/course
            shutil.copytree(ROOT/'Projects/AIWorkshop', project, ignore=shutil.ignore_patterns('.zamin', 'Build'))
            manifest = project/'AIWorkshop.zproject'
            data = json.loads(manifest.read_text())
            assert not data['Project']['ScriptModulePath']
            data['Project']['StartScene'] = f'Scenes/{course}.zscene'
            manifest.write_text(json.dumps(data, indent=2)+'\n')
            scene = project/f'Assets/Scenes/{course}.zscene'
            env = dict(os.environ, LIBGL_ALWAYS_SOFTWARE='1', LP_NUM_THREADS='2', ZAMIN_GL_DEBUG='1', ZAMIN_CAPTURE_FPS='30')
            for mode in (('player',) if args.no_editor else ('player', 'editor')):
                auto = mode == 'editor'
                overrides(scene, auto)
                home = Path(temp)/(course+'-'+mode+'-home')
                home.mkdir()
                png = out/f'{course}-{mode}.png'
                log = out/f'{course}-{mode}.log'
                runenv = dict(env, HOME=str(home), ZAMIN_SCREENSHOT=str(png), ZAMIN_MAX_FRAMES='420')
                if auto:
                    runenv.update(ZAMIN_EDITOR_PLAY='1', ZAMIN_EDITOR_SELECT='Player')
                else:
                    runenv['ZAMIN_PLAYER_INPUT'] = TAPE
                cmd = [str(build/'bin'/('ZaminEditor' if auto else 'ZaminPlayer')), str(manifest)]
                if not auto:
                    cmd.append('--no-splash')
                with log.open('w') as stream:
                    subprocess.run(runner+cmd, env=runenv, stdout=stream, stderr=subprocess.STDOUT, timeout=360, check=True)
                validate(log)
                subprocess.run(['python3', str(ROOT/'Tests/png_check.py'), str(png)], check=True)
                report.append({'Scene': course, 'Mode': mode, 'NativeGameModule': False, 'Passed': True})
                print(f'PASS {course}/{mode}: patrol, chase, attack, investigate, return, dead, spawn', flush=True)
            if args.exports:
                overrides(scene, True)
                exportenv = dict(env, ZAMIN_BUILD_DIR=str(build))
                for key in ('ZAMIN_MAX_FRAMES', 'ZAMIN_SCREENSHOT', 'ZAMIN_EDITOR_PLAY', 'ZAMIN_EDITOR_SELECT', 'ZAMIN_PLAYER_INPUT'):
                    exportenv.pop(key, None)
                exportout = out/(course+'-export')
                subprocess.run(['bash', str(ROOT/'Tests/export_test.sh'), '--project', str(project), '--frames', '420',
                                '--shot-frames', '450', '--out', str(exportout)], env=exportenv, timeout=900, check=True)
                for phase in ('player.log', 'player-shot.log'):
                    validate(exportout/phase)
                report.append({'Scene': course, 'Mode': 'export-and-relocated-version-check', 'Passed': True})
    (out/'Report.json').write_text(json.dumps({'Passed': True, 'Checks': report}, indent=2)+'\n')


if __name__ == '__main__':
    main()
