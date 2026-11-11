#!/usr/bin/env python3
"""Real 2D/3D world crowd: four-way crossing, stop/reverse, graph telemetry and editor capture."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from world_streaming_test import find_module

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--no-editor', action='store_true')
    args = parser.parse_args()
    build = args.build.resolve()
    out = (args.out or build / 'WorldCrowdRuntime').resolve()
    out.mkdir(parents=True, exist_ok=True)
    runner = [] if os.environ.get('DISPLAY') else ['xvfb-run', '-a', '-s', '-screen 0 1600x900x24 -nolisten tcp']
    reports = []
    with tempfile.TemporaryDirectory(prefix='zamin-world-crowd-') as scratch:
        for name in ('WorldWorkshop3D', 'WorldWorkshop2D'):
            source = ROOT / 'Projects' / name
            project = Path(scratch) / name
            shutil.copytree(source, project, ignore=shutil.ignore_patterns('Build', 'Binaries', '.zamin'))
            binary = project / 'Binaries'
            binary.mkdir()
            shutil.copy2(find_module(build, source, name), binary / ('lib' + name + '.so'))
            home = Path(scratch) / (name + '-home')
            home.mkdir()
            env = os.environ.copy()
            env.update(HOME=str(home), LIBGL_ALWAYS_SOFTWARE='1', LP_NUM_THREADS='2', ZAMIN_GL_DEBUG='1',
                       ZAMIN_SCRIPT_MODULE_DIR=str(binary), ZAMIN_WORLD_CROWD_TEST='1', ZAMIN_CAPTURE_FPS='30',
                       ZAMIN_MAX_FRAMES='450', ZAMIN_SCREENSHOT=str(out / (name + '.png')))
            expected = ['Crowd oracle: spawned 4 graph agents', 'Crowd oracle: all first destinations reached',
                        'Crowd oracle: stop and reverse destinations accepted',
                        'Crowd oracle: 2 phases, graph telemetry and separation passed']
            log = out / (name + '.log')
            with log.open('w') as stream:
                result = subprocess.run(runner + [str(build / 'bin/ZaminPlayer'), str(project)], env=env,
                                        stdout=stream, stderr=subprocess.STDOUT, timeout=360)
            text = log.read_text(errors='replace')
            if result.returncode or any(marker not in text for marker in expected) or any(marker in text for marker in ('GL_INVALID_', 'Assertion failed')):
                raise RuntimeError(f'{name}: crowd oracle failed, exit {result.returncode}; see {log}')
            if not (out / (name + '.png')).read_bytes().startswith(b'\x89PNG\r\n\x1a\n'):
                raise RuntimeError(f'{name}: missing runtime PNG')
            if not args.no_editor:
                env.update(ZAMIN_EDITOR_SELECT='Crowd walker 1', ZAMIN_EDITOR_SELECT_FRAME='100', ZAMIN_EDITOR_VISUAL_GRAPH='Scripts/CrowdMonitor.zgraph',
                           ZAMIN_EDITOR_WORLD_AGENT_QA='1', ZAMIN_EDITOR_PLAY='1', ZAMIN_MAX_FRAMES='130',
                           ZAMIN_SCREENSHOT=str(out / (name + '-editor.png')))
                log = out / (name + '-editor.log')
                with log.open('w') as stream:
                    result = subprocess.run(runner + [str(build / 'bin/ZaminEditor'), str(project)], env=env,
                                            stdout=stream, stderr=subprocess.STDOUT, timeout=240)
                text = log.read_text(errors='replace')
                if result.returncode or 'Agent graph editor QA: 5 nodes, 6 links' not in text or 'GL_INVALID_' in text:
                    raise RuntimeError(f'{name}: crowd editor graph failed; see {log}')
                if not (out / (name + '-editor.png')).read_bytes().startswith(b'\x89PNG\r\n\x1a\n'):
                    raise RuntimeError(f'{name}: missing editor PNG')
            reports.append({'Project': name, 'Frames': 450, 'Agents': 4, 'Checks': expected, 'Editor': not args.no_editor})
            print(f'PASS {name}: four-way crowd, two destinations, stop/reverse, graph and separation', flush=True)
    (out / 'report.json').write_text(json.dumps(reports, indent=2) + '\n')


if __name__ == '__main__':
    main()
