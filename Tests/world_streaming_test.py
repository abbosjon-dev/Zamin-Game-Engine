#!/usr/bin/env python3
"""Actual Player camera tours plus editor world-panel capture; no simulated renderer or loader."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ROW = re.compile(r"World tour frame (\d+): active=(\d+) admitted=(\d+) activated=(\d+) unloaded=(\d+) failed=(\d+)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--no-editor", action="store_true")
    args = parser.parse_args()
    build = args.build.resolve()
    out = (args.out or build / "WorldStreamingRuntime").resolve()
    out.mkdir(parents=True, exist_ok=True)
    runner = [] if os.environ.get("DISPLAY") else ["xvfb-run", "-a", "-s", "-screen 0 1600x900x24 -nolisten tcp"]
    report = []
    with tempfile.TemporaryDirectory(prefix="zamin-world-tour-") as scratch:
        work = Path(scratch)
        for name in ("WorldWorkshop3D", "WorldWorkshop2D"):
            project = work / name
            shutil.copytree(ROOT / "Projects" / name, project, ignore=shutil.ignore_patterns("Build", "Binaries", ".zamin"))
            home = work / (name + "-home")
            home.mkdir()
            env = os.environ.copy()
            env.update(HOME=str(home), LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="2", ZAMIN_GL_DEBUG="1",
                       ZAMIN_SCRIPT_MODULE_DIR=str(build / "GameModules"), ZAMIN_MAX_FRAMES="660", ZAMIN_WORLD_AUTOTOUR="1",
                       ZAMIN_SCREENSHOT=str(out / (name + ".png")))
            log = out / (name + ".log")
            with log.open("w") as stream:
                result = subprocess.run(runner + [str(build / "bin/ZaminPlayer"), str(project)], env=env,
                                        stdout=stream, stderr=subprocess.STDOUT, timeout=240)
            text = log.read_text(errors="replace")
            rows = [tuple(map(int, match)) for match in ROW.findall(text)]
            if result.returncode or not rows or rows[-1][0] < 600:
                raise RuntimeError(f"{name}: incomplete tour, exit {result.returncode}; see {log}")
            if any(row[2] > 12 or row[5] for row in rows):
                raise RuntimeError(f"{name}: admission bound or failed cell; see {log}")
            empty = [row for row in rows if row[0] == 420]
            if not empty or empty[0][1] != 0 or rows[-1][1] == 0 or rows[-1][4] == 0 or rows[-1][3] <= rows[0][3]:
                raise RuntimeError(f"{name}: unload/empty-region/return oracle failed: {rows}")
            if "GL_INVALID_" in text or "Assertion failed" in text:
                raise RuntimeError(f"{name}: graphics/assertion error; see {log}")
            picture = out / (name + ".png")
            if not picture.exists() or not picture.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
                raise RuntimeError(f"{name}: screenshot missing")
            report.append({"Project": name, "Frames": rows[-1][0], "Active": rows[-1][1], "Activated": rows[-1][3],
                           "Unloaded": rows[-1][4], "PeakAdmitted": max(row[2] for row in rows), "Failed": 0})
            print(f"PASS {name}: {report[-1]}", flush=True)
            if name == "WorldWorkshop3D" and not args.no_editor:
                env.update(ZAMIN_EDITOR_WORLD="1", ZAMIN_EDITOR_PLAY="1", ZAMIN_MAX_FRAMES="100",
                           ZAMIN_SCREENSHOT=str(out / "world-panel.png"))
                env.pop("ZAMIN_WORLD_AUTOTOUR", None)
                with (out / "world-panel.log").open("w") as stream:
                    result = subprocess.run(runner + [str(build / "bin/ZaminEditor"), str(project)], env=env,
                                            stdout=stream, stderr=subprocess.STDOUT, timeout=180)
                if result.returncode or not (out / "world-panel.png").exists():
                    raise RuntimeError("Editor world-panel capture failed")
                print("PASS World Streaming editor panel", flush=True)
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
