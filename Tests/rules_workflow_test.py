#!/usr/bin/env python3
"""Real 2D/3D combat, death/respawn, key/heal/door/coin acceptance in Player, editor and exports."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ACCEPT = 'RULES_ACCEPTANCE death=1 respawn=1 key=1 kill=1 heal=1 unlock=1 coin=1 finish=1'
TAPE = '15:hold=A;30:release=A;45:hold=D;61:E;68:release=D;72:LeftControl;84:LeftControl;96:LeftControl;103:E;110:hold=D;139:E;179:release=D'


def validate(log):
    text = log.read_text(errors='replace')
    for marker in ('GL_INVALID_', 'Assertion failed', "Visual graph '", 'Visual graph error', 'RULES_ORACLE_FAILED', 'RULES_MISSING_'):
        if marker in text:
            raise RuntimeError(f'{marker}: {log}')
    if ACCEPT not in text:
        raise RuntimeError(f'Missing combat/inventory acceptance: {log}')


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
    parser.add_argument('--scene', choices=('Rules2D', 'Rules3D'))
    args = parser.parse_args()
    build = args.build.resolve()
    out = (args.out or build/'GameplayRules/Workflow').resolve()
    out.mkdir(parents=True, exist_ok=True)
    runner = [] if os.environ.get('DISPLAY') else ['xvfb-run', '-a', '-s', '-screen 0 1600x1000x24 -nolisten tcp']
    report = []
    with tempfile.TemporaryDirectory(prefix='zamin-rules-') as temp:
        for course in ((args.scene,) if args.scene else ('Rules2D', 'Rules3D')):
            project = Path(temp)/course
            shutil.copytree(ROOT/'Projects/RulesWorkshop', project, ignore=shutil.ignore_patterns('.zamin', 'Build'))
            manifest = project/'RulesWorkshop.zproject'
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
                runenv = dict(env, HOME=str(home), ZAMIN_SCREENSHOT=str(png), ZAMIN_MAX_FRAMES='240')
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
                print(f'PASS {course}/{mode}: death, respawn, key, kill, heal, unlock, coin, finish', flush=True)
            if args.exports:
                overrides(scene, True)
                exportenv = dict(env, ZAMIN_BUILD_DIR=str(build))
                for key in ('ZAMIN_MAX_FRAMES', 'ZAMIN_SCREENSHOT', 'ZAMIN_EDITOR_PLAY', 'ZAMIN_EDITOR_SELECT', 'ZAMIN_PLAYER_INPUT'):
                    exportenv.pop(key, None)
                exportout = out/(course+'-export')
                subprocess.run(['bash', str(ROOT/'Tests/export_test.sh'), '--project', str(project), '--frames', '240',
                                '--shot-frames', '270', '--out', str(exportout)], env=exportenv, timeout=900, check=True)
                for phase in ('player.log', 'player-shot.log'):
                    validate(exportout/phase)
                report.append({'Scene': course, 'Mode': 'export-and-relocated-version-check', 'Passed': True})
    (out/'Report.json').write_text(json.dumps({'Passed': True, 'Checks': report}, indent=2)+'\n')


if __name__ == '__main__':
    main()
