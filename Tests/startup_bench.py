#!/usr/bin/env python3
"""Start-up time of the editor and the player (task 495): each program opens a project and shows one frame
(ZAMIN_MAX_FRAMES=1) a few times; the table has the median of

  * "birinchi kadr" - the engine's own measurement (StartupTimer): from the moment the engine library was loaded
    to the first frame's swap, with its parts (dvigatel: window, GL context, renderer, audio; layer'lar: project,
    scene, editor panels; the first frame itself). A build without StartupTimer (older) has none: "-".
  * "jarayon" - the whole process as seen from outside: exec -> exit after that frame (the shutdown included),
    which any build has, so two builds can be compared with it.

    Tests/startup_bench.py                                   the build in $ZAMIN_BUILD_DIR or build/ (Release: build-release)
    Tests/startup_bench.py --bin /tmp/old/bin --module-dir /tmp/old/modules   another build's programs (its game modules)
    Tests/startup_bench.py --runs 7 --async 1                more runs; the editor's background loading on (as interactive)
    Tests/startup_bench.py --profile                         the slowest start-up scopes of the last run as well
Runs under xvfb (one server for every run) when there is no display. Exit code: 0 done, 1 a run failed, 2 the
programs are missing or there is no display and no Xvfb.
"""
import argparse
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CASES = [
    ("ZaminEditor", "Projects/Showcase3D"),
    ("ZaminEditor", "Projects/Platformer2D"),
    ("ZaminPlayer", "Projects/Showcase3D"),
    ("ZaminPlayer", "Projects/Adventure3D"),
    ("ZaminPlayer", "Projects/Platformer2D"),
]

LINE = re.compile(r"Ishga tushish: birinchi kadr (\d+) ms \(dvigatel (\d+), layer'lar (\d+), birinchi kadr (\d+) ms")


def start_xvfb():
    if os.environ.get("DISPLAY"):
        return None, os.environ["DISPLAY"]
    xvfb = shutil.which("Xvfb")
    if not xvfb:
        return None, None
    for number in range(97, 120):
        if os.path.exists(f"/tmp/.X{number}-lock"):
            continue
        proc = subprocess.Popen([xvfb, f":{number}", "-screen", "0", "1600x900x24"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1.0)
        if proc.poll() is None:
            return proc, f":{number}"
    return None, None


def run_once(program, project, env):
    started = time.perf_counter()
    result = subprocess.run([program, os.path.join(ROOT, project)], cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, errors="replace", timeout=300)
    wall = (time.perf_counter() - started) * 1000.0
    found = LINE.search(result.stdout)
    inner = tuple(float(x) for x in found.groups()) if found else None
    return result.returncode, wall, inner, result.stdout


def main():
    parser = argparse.ArgumentParser(description="Editor / player start-up time")
    parser.add_argument("--bin", default=os.path.join(os.environ.get("ZAMIN_BUILD_DIR", os.path.join(ROOT, "build")), "bin"))
    parser.add_argument("--module-dir", default="", help="ZAMIN_SCRIPT_MODULE_DIR for the runs (another build's game modules)")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--async", dest="async_load", default="", help="ZAMIN_ASYNC_LOAD for the runs (0 / 1)")
    parser.add_argument("--case", action="append", default=[], help="program:project (default: every case)")
    parser.add_argument("--profile", action="store_true")
    args = parser.parse_args()

    cases = [tuple(c.split(":", 1)) for c in args.case] if args.case else CASES
    for program, _ in cases:
        if not os.access(os.path.join(args.bin, program), os.X_OK):
            print(f"Dastur topilmadi: {os.path.join(args.bin, program)}", file=sys.stderr)
            return 2
    xvfb, display = start_xvfb()
    if not display:
        print("Displey yo'q va Xvfb topilmadi", file=sys.stderr)
        return 2
    failed = False
    rows = []
    try:
        with tempfile.TemporaryDirectory(prefix="zamin-startup-") as home:
            env = dict(os.environ, DISPLAY=display, HOME=home, ZAMIN_MAX_FRAMES="1")
            if args.module_dir:
                env["ZAMIN_SCRIPT_MODULE_DIR"] = args.module_dir
            if args.async_load:
                env["ZAMIN_ASYNC_LOAD"] = args.async_load
            if args.profile:
                env["ZAMIN_STARTUP_PROFILE"] = "1"
            for program, project in cases:
                path = os.path.join(args.bin, program)
                run_once(path, project, env)  # warm-up: the disk caches (shaders, font atlases) and the file system
                walls, inners, log = [], [], ""
                for _ in range(args.runs):
                    code, wall, inner, log = run_once(path, project, env)
                    if code != 0:
                        print(f"{program} {project}: chiqish kodi {code}", file=sys.stderr)
                        failed = True
                    walls.append(wall)
                    if inner:
                        inners.append(inner)
                inner = tuple(statistics.median(v[i] for v in inners) for i in range(4)) if inners else None
                rows.append((program, project, statistics.median(walls), min(walls), inner))
                if args.profile and "Eng sekin joylar" in log:
                    print(f"--- {program} {project}:")
                    print(log[log.index("Ishga tushish profili"):].split("\x1b[0m")[0])
    finally:
        if xvfb:
            xvfb.terminate()
    print(f"| Dastur | Loyiha | Birinchi kadr (ms) | dvigatel / layer'lar / 1-kadr | Jarayon, mediana (min) ms |")
    print(f"|---|---|---:|---|---:|")
    for program, project, wall, best, inner in rows:
        first = f"{inner[0]:.0f}" if inner else "-"
        parts = f"{inner[1]:.0f} / {inner[2]:.0f} / {inner[3]:.0f}" if inner else "-"
        print(f"| {program} | {os.path.basename(project)} | {first} | {parts} | {wall:.0f} ({best:.0f}) |")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
