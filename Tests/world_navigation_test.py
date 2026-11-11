#!/usr/bin/env python3
"""Real 2D/3D Player: sparse routes, explicit portal traversal, stale stop and streaming return."""
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
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--no-editor", action="store_true")
    args = parser.parse_args()
    build = args.build.resolve()
    out = (args.out or build / "WorldNavigationRuntime").resolve()
    out.mkdir(parents=True, exist_ok=True)
    runner = [] if os.environ.get("DISPLAY") else ["xvfb-run", "-a", "-s", "-screen 0 1600x900x24 -nolisten tcp"]
    reports = []
    with tempfile.TemporaryDirectory(prefix="zamin-navigation-") as scratch:
        work = Path(scratch)
        for name in ("WorldWorkshop3D", "WorldWorkshop2D"):
            source = ROOT / "Projects" / name
            project = work / name
            shutil.copytree(source, project, ignore=shutil.ignore_patterns("Build", "Binaries", ".zamin"))
            binary = project / "Binaries"
            binary.mkdir()
            shutil.copy2(find_module(build, source, name), binary / ("lib" + name + ".so"))
            home = work / (name + "-home")
            home.mkdir()
            picture = out / (name + ".png")
            env = os.environ.copy()
            env.update(HOME=str(home), LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="2", ZAMIN_GL_DEBUG="1",
                       ZAMIN_SCRIPT_MODULE_DIR=str(binary), ZAMIN_WORLD_NAVIGATION_TEST="1", ZAMIN_MAX_FRAMES="420",
                       ZAMIN_SCREENSHOT=str(picture))
            log = out / (name + ".log")
            with log.open("w") as stream:
                result = subprocess.run(runner + [str(build / "bin/ZaminPlayer"), str(project)], env=env,
                                        stdout=stream, stderr=subprocess.STDOUT, timeout=300)
            text = log.read_text(errors="replace")
            expected = ["Navigation oracle: route ready", "Navigation oracle: explicit portal traversal completed",
                        "Navigation oracle: actor arrived after traversal", "Navigation oracle: stale topology stopped actor",
                        "Navigation oracle: disable, unload, return and replan passed"]
            if result.returncode or any(item not in text for item in expected) or any(item in text for item in ("GL_INVALID_", "Assertion failed")):
                raise RuntimeError(f"{name}: navigation workflow failed (exit {result.returncode}); see {log}")
            if not picture.is_file() or not picture.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
                raise RuntimeError(f"{name}: screenshot missing")
            if not args.no_editor:
                env.pop("ZAMIN_WORLD_NAVIGATION_TEST", None)
                env.update(ZAMIN_EDITOR_WORLD="1", ZAMIN_EDITOR_PLAY="1", ZAMIN_EDITOR_WORLD_NAVIGATION_QA="1",
                           ZAMIN_MAX_FRAMES="120", ZAMIN_SCREENSHOT=str(out / (name + "-editor.png")))
                editor_log = out / (name + "-editor.log")
                with editor_log.open("w") as stream:
                    editor = subprocess.run(runner + [str(build / "bin/ZaminEditor"), str(project)], env=env,
                                            stdout=stream, stderr=subprocess.STDOUT, timeout=240)
                editor_text = editor_log.read_text(errors="replace")
                if editor.returncode or "World navigation editor QA passed" not in editor_text or any(item in editor_text for item in ("GL_INVALID_", "Assertion failed")):
                    raise RuntimeError(f"{name}: editor route preview failed; see {editor_log}")
                editor_picture = out / (name + "-editor.png")
                if not editor_picture.is_file() or not editor_picture.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
                    raise RuntimeError(f"{name}: editor screenshot missing")
            reports.append({"Project": name, "Frames": 420, "Checks": expected, "Screenshot": str(picture)})
            print(f"PASS {name}: route, explicit traversal, disable/stale stop, unload/return and replan", flush=True)
    (out / "report.json").write_text(json.dumps(reports, indent=2) + "\n")


if __name__ == "__main__":
    main()
