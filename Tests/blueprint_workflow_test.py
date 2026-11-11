#!/usr/bin/env python3
"""Compiler-free typed Blueprint gameplay in real 2D/3D Player, editor and optional relocated exports."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--no-editor', action='store_true')
    parser.add_argument('--exports', action='store_true')
    args = parser.parse_args()
    build = args.build.resolve()
    out = (args.out or build / 'BlueprintCompletion/Workflow').resolve()
    out.mkdir(parents=True, exist_ok=True)
    runner = [] if os.environ.get('DISPLAY') else ['xvfb-run', '-a', '-s', '-screen 0 1600x1000x24 -nolisten tcp']
    results = []
    with tempfile.TemporaryDirectory(prefix='zamin-blueprint-') as scratch:
        for scene, score in (('Visual2D', 12), ('Visual3D', 18)):
            project = Path(scratch) / scene
            shutil.copytree(ROOT / 'Projects/VisualWorkshop', project, ignore=shutil.ignore_patterns('.zamin', 'Build'))
            manifest = project / 'VisualWorkshop.zproject'
            data = json.loads(manifest.read_text())
            assert not data['Project']['ScriptModulePath'], 'Acceptance must not use a native game module'
            data['Project']['StartScene'] = f'Scenes/{scene}.zscene'
            manifest.write_text(json.dumps(data, indent=2)+'\n')
            home = Path(scratch) / (scene+'-home')
            home.mkdir()
            env = dict(os.environ, HOME=str(home), LIBGL_ALWAYS_SOFTWARE='1', LP_NUM_THREADS='2',
                       ZAMIN_GL_DEBUG='1', ZAMIN_CAPTURE_FPS='30', ZAMIN_MAX_FRAMES='120')
            for mode in (('player',) if args.no_editor else ('player', 'editor')):
                png = out / f'{scene}-{mode}.png'
                env.update(ZAMIN_SCREENSHOT=str(png))
                if mode == 'editor':
                    env.update(ZAMIN_EDITOR_PLAY='1', ZAMIN_EDITOR_SELECT='Blueprint gameplay controller',
                               ZAMIN_EDITOR_VISUAL_GRAPH='Scripts/BlueprintGameplay.zgraph')
                log = out / f'{scene}-{mode}.log'
                executable = build / 'bin' / ('ZaminEditor' if mode == 'editor' else 'ZaminPlayer')
                with log.open('w') as stream:
                    result = subprocess.run(runner + [str(executable), str(manifest)], env=env,
                                            stdout=stream, stderr=subprocess.STDOUT, timeout=240)
                text = log.read_text(errors='replace')
                if result.returncode or not re.search(rf'BLUEPRINT_ACCEPTANCE total={score}(?:\.0+)?(?=$|[\s\x1b])', text):
                    raise RuntimeError(f'{scene}/{mode}: missing score {score}, exit {result.returncode}; {log}')
                if any(marker in text for marker in ('GL_INVALID_', 'Assertion failed', "Visual graph '", 'Visual graph error', 'Visual graph:')):
                    raise RuntimeError(f'{scene}/{mode}: runtime diagnostic; {log}')
                subprocess.run(['python3', str(ROOT / 'Tests/png_check.py'), str(png)], check=True)
                results.append({'Scene': scene, 'Mode': mode, 'Frames': 120, 'Total': score, 'NativeGameModule': False})
                print(f'PASS {scene}/{mode}: delayed typed function + local + snapshot ForEach + named event, total={score}', flush=True)
            if args.exports:
                export_out = out / (scene+'-export')
                export_env = dict(env, ZAMIN_BUILD_DIR=str(build))
                for key in ('ZAMIN_EDITOR_PLAY', 'ZAMIN_EDITOR_SELECT', 'ZAMIN_EDITOR_VISUAL_GRAPH', 'ZAMIN_SCREENSHOT', 'ZAMIN_MAX_FRAMES', 'ZAMIN_CAPTURE_FPS'):
                    export_env.pop(key, None)
                subprocess.run(['bash', str(ROOT / 'Tests/export_test.sh'), '--project', str(project),
                                '--frames', '120', '--out', str(export_out)], env=export_env, check=True, timeout=600)
                for phase in ('player.log', 'player-shot.log'):
                    log = export_out / phase
                    if not log.is_file() or not re.search(rf'BLUEPRINT_ACCEPTANCE total={score}(?:\.0+)?(?=$|[\s\x1b])', log.read_text(errors='replace')):
                        raise RuntimeError(f'{scene}: exported score missing from {log}')
                results.append({'Scene': scene, 'Mode': 'export-with-relocated-version-check', 'Frames': 120, 'Total': score})
    (out / 'Report.json').write_text(json.dumps({'Passed': True, 'Checks': results}, indent=2)+'\n')


if __name__ == '__main__':
    main()
