#!/usr/bin/env python3
"""Live 2D/3D barrier, bounded recovery exhaustion, same-intent reopening and graph capture."""
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
    parser.add_argument('--editor-only', action='store_true')
    args = parser.parse_args()
    if args.editor_only and args.no_editor:
        parser.error('--editor-only and --no-editor are mutually exclusive')
    build = args.build.resolve()
    out = (args.out or build / 'WorldRecoveryRuntime').resolve()
    out.mkdir(parents=True, exist_ok=True)
    runner = [] if os.environ.get('DISPLAY') else ['xvfb-run', '-a', '-s', '-screen 0 1600x900x24 -nolisten tcp']
    reports = []
    with tempfile.TemporaryDirectory(prefix='zamin-world-recovery-') as scratch:
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
                       ZAMIN_SCRIPT_MODULE_DIR=str(binary), ZAMIN_WORLD_RECOVERY_TEST='1', ZAMIN_CAPTURE_FPS='30',
                       ZAMIN_MAX_FRAMES='420', ZAMIN_SCREENSHOT=str(out / (name + '.png')))
            expected = ['Recovery oracle: barrier created and destination accepted',
                        'Recovery oracle: retries exhausted, intent retained and actor stopped',
                        'Recovery oracle: stable carving, no mosaic rebuild feedback',
                        'Recovery oracle: barrier opened without resetting destination',
                        'Recovery oracle: exhausted, graph telemetry, same-intent topology recovery and arrival passed']
            if not args.editor_only:
                log = out / (name + '.log')
                with log.open('w') as stream:
                    result = subprocess.run(runner + [str(build / 'bin/ZaminPlayer'), str(project)], env=env,
                                            stdout=stream, stderr=subprocess.STDOUT, timeout=360)
                text = log.read_text(errors='replace')
                if result.returncode or any(marker not in text for marker in expected) or any(marker in text for marker in ('GL_INVALID_', 'Assertion failed')):
                    raise RuntimeError(f'{name}: recovery oracle failed, exit {result.returncode}; see {log}')
                if not (out / (name + '.png')).read_bytes().startswith(b'\x89PNG\r\n\x1a\n'):
                    raise RuntimeError(f'{name}: missing runtime PNG')
            if not args.no_editor:
                env.update(ZAMIN_EDITOR_SELECT='Recovery walker', ZAMIN_EDITOR_SELECT_FRAME='120', ZAMIN_EDITOR_VISUAL_GRAPH='Scripts/RecoveryMonitor.zgraph',
                           ZAMIN_EDITOR_WORLD_AGENT_QA='1', ZAMIN_EDITOR_PLAY='1', ZAMIN_MAX_FRAMES='160',
                           ZAMIN_SCREENSHOT=str(out / (name + '-editor.png')))
                log = out / (name + '-editor.log')
                with log.open('w') as stream:
                    result = subprocess.run(runner + [str(build / 'bin/ZaminEditor'), str(project)], env=env,
                                            stdout=stream, stderr=subprocess.STDOUT, timeout=240)
                text = log.read_text(errors='replace')
                if result.returncode or 'Agent graph editor QA: 8 nodes, 9 links' not in text or 'GL_INVALID_' in text:
                    raise RuntimeError(f'{name}: recovery editor graph failed; see {log}')
                if not (out / (name + '-editor.png')).read_bytes().startswith(b'\x89PNG\r\n\x1a\n'):
                    raise RuntimeError(f'{name}: missing editor PNG')
            reports.append({'Project': name, 'Frames': 0 if args.editor_only else 420, 'EditorFrames': 160 if not args.no_editor else 0, 'Agents': 1, 'Checks': ['Agent graph editor QA: 8 nodes, 9 links'] if args.editor_only else expected, 'Editor': not args.no_editor})
            print(f'PASS {name}: ' + ('recovery editor graph and live variables' if args.editor_only else 'barrier, bounded recovery, graph, stable carving and same-intent arrival'), flush=True)
    (out / 'report.json').write_text(json.dumps(reports, indent=2) + '\n')


if __name__ == '__main__':
    main()
