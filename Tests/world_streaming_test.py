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
MEMORY = re.compile(r"World residency frame (\d+): rss=(\d+) gpu=(\d+) caches=(\d+) pinned=(\d+) evicted=(\d+) pressure=(\w+) navTiles=(\d+) navCells=(\d+) navErrors=(\d+)")
NAV = re.compile(r"World navigation frame (\d+): crossPath=(\d+) expanded=(\d+)")
ROW = re.compile(r"World tour frame (\d+): active=(\d+) admitted=(\d+) activated=(\d+) unloaded=(\d+) failed=(\d+)")


def find_module(build, project, name):
    """Normal builds use Projects/*/Binaries; isolated builds can choose another output root."""
    filename = "lib" + name + ".so"
    candidates = []
    cache = build / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text(errors="replace").splitlines():
            if line.startswith("ZAMIN_GAME_MODULE_OUTPUT_DIR:"):
                directory = line.partition("=")[2]
                if directory:
                    value = Path(directory)
                    candidates.append((value if value.is_absolute() else build / value) / filename)
    candidates.extend([build / "GameModules" / filename, project / "Binaries" / filename])
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"Native module for {name} was not built; checked {candidates}")


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
            binary = project / "Binaries"
            binary.mkdir()
            shutil.copy2(find_module(build, ROOT / "Projects" / name, name), binary / ("lib" + name + ".so"))
            home = work / (name + "-home")
            home.mkdir()
            env = os.environ.copy()
            env.update(HOME=str(home), LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="2", ZAMIN_GL_DEBUG="1",
                       ZAMIN_SCRIPT_MODULE_DIR=str(binary), ZAMIN_MAX_FRAMES="660", ZAMIN_WORLD_AUTOTOUR="1",
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
            memory = MEMORY.findall(text)
            navigation = NAV.findall(text)
            if not memory or any(int(row[8]) > 65536 or int(row[9]) or row[6] not in ("false", "0") for row in memory):
                raise RuntimeError(f"{name}: navigation bound / residency diagnostics oracle failed; see {log}")
            if not any(int(row[1]) == 1 for row in navigation):
                raise RuntimeError(f"{name}: no complete cross-cell path; see {log}")
            empty = [row for row in rows if row[0] == 420]
            if not empty or empty[0][1] != 0 or rows[-1][1] == 0 or rows[-1][4] == 0 or rows[-1][3] <= rows[0][3]:
                raise RuntimeError(f"{name}: unload/empty-region/return oracle failed: {rows}")
            if "GL_INVALID_" in text or "Assertion failed" in text:
                raise RuntimeError(f"{name}: graphics/assertion error; see {log}")
            picture = out / (name + ".png")
            if not picture.exists() or not picture.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
                raise RuntimeError(f"{name}: screenshot missing")
            report.append({"Project": name, "Frames": rows[-1][0], "Active": rows[-1][1], "Activated": rows[-1][3],
                           "Unloaded": rows[-1][4], "PeakAdmitted": max(row[2] for row in rows), "Failed": 0,
                           "PeakRSSBytes": max(int(row[1]) for row in memory), "PeakRequestedGPUBytes": max(int(row[2]) for row in memory),
                           "PeakNavigationCells": max(int(row[8]) for row in memory), "CrossCellPaths": sum(int(row[1]) for row in navigation)})
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
                env.pop("ZAMIN_EDITOR_PLAY", None)
                env.pop("ZAMIN_EDITOR_WORLD", None)
                env.update(ZAMIN_EDITOR_TERRAIN="world-test", ZAMIN_MAX_FRAMES="60",
                           ZAMIN_SCREENSHOT=str(out / "terrain-pair-editor.png"))
                with (out / "terrain-pair-editor.log").open("w") as stream:
                    result = subprocess.run(runner + [str(build / "bin/ZaminEditor"), str(project)], env=env,
                                            stdout=stream, stderr=subprocess.STDOUT, timeout=180)
                text = (out / "terrain-pair-editor.log").read_text(errors="replace")
                if result.returncode or "World terrain editor QA passed" not in text or "GL_INVALID_" in text:
                    raise RuntimeError("Terrain pair editor undo/redo QA failed")
                if not (out / "terrain-pair-editor.png").is_file():
                    raise RuntimeError("Terrain pair editor screenshot missing")
                print("PASS terrain pair editor: stitch, undo/redo, border LOD", flush=True)
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
