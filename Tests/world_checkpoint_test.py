#!/usr/bin/env python3
"""Actual 2D/3D Player checkpoint oracles and editor world wizard; isolated project/data copies."""
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
    out = (args.out or build / "WorldCheckpointRuntime").resolve()
    out.mkdir(parents=True, exist_ok=True)
    runner = [] if os.environ.get("DISPLAY") else ["xvfb-run", "-a", "-s", "-screen 0 1600x900x24 -nolisten tcp"]
    reports = []
    with tempfile.TemporaryDirectory(prefix="zamin-checkpoint-runtime-") as scratch:
        work = Path(scratch)
        for name, mode in (("WorldWorkshop3D", "3d"), ("WorldWorkshop2D", "2d")):
            project = work / name
            shutil.copytree(ROOT / "Projects" / name, project, ignore=shutil.ignore_patterns("Build", "Binaries", ".zamin"))
            binary = project / "Binaries"
            binary.mkdir()
            shutil.copy2(find_module(build, ROOT / "Projects" / name, name), binary / ("lib" + name + ".so"))
            home = work / (name + "-home")
            home.mkdir()
            slot = out / (name + "-checkpoint.json")
            env = os.environ.copy()
            env.update(HOME=str(home), LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="2", ZAMIN_GL_DEBUG="1",
                       ZAMIN_SCRIPT_MODULE_DIR=str(binary), ZAMIN_WORLD_CHECKPOINT_TEST="1", ZAMIN_MAX_FRAMES="540",
                       ZAMIN_WORLD_CHECKPOINT_FILE=str(slot), ZAMIN_SCREENSHOT=str(out / (name + ".png")))
            def run(program, label, expected, timeout=240):
                log = out / (label + ".log")
                with log.open("w") as stream:
                    result = subprocess.run(runner + [str(build / "bin" / program), str(project)], env=env,
                                            stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
                text = log.read_text(errors="replace")
                if result.returncode or any(token not in text for token in expected) or any(token in text for token in ("GL_INVALID_", "Assertion failed", "Checkpoint entity state oracle failed", "Checkpoint command failed")):
                    raise RuntimeError(f"{label}: runtime oracle failed, exit {result.returncode}; see {log}")
                return text
            run("ZaminPlayer", name, ["Checkpoint oracle: queued edits, deletion and spawned actor",
                                     "Checkpoint oracle: unload and return preserved state", "Checkpoint oracle: disk restore passed"])
            doc = json.loads(slot.read_text())
            if doc["Format"] != "ZaminWorldCheckpoint" or not doc["Cells"]:
                raise RuntimeError("Checkpoint file missing cells")
            shutil.copy2(slot, out / (name + "-saved-proof.json"))
            print(f"PASS {name}: edits, deletion, adoption, unload/return and disk restore", flush=True)
            if not args.no_editor:
                env.pop("ZAMIN_WORLD_CHECKPOINT_TEST", None)
                env.update(ZAMIN_EDITOR_WORLD="1", ZAMIN_EDITOR_WORLD_AUTHORING=mode, ZAMIN_MAX_FRAMES="80",
                           ZAMIN_SCREENSHOT=str(out / (name + "-wizard.png")))
                run("ZaminEditor", name + "-wizard", ["World authoring editor QA passed"], 180)
                folder = "AuthoredQA3D" if mode == "3d" else "AuthoredQA2D"
                authored = json.loads((project / "Assets" / folder / "Main.zworld").read_text())
                if len(authored["Cells"]) != 6:
                    raise RuntimeError("Wizard did not publish six cells")
                env.pop("ZAMIN_EDITOR_WORLD_AUTHORING", None)
                env.update(ZAMIN_EDITOR_PLAY="1", ZAMIN_EDITOR_WORLD_CHECKPOINT_QA="1", ZAMIN_MAX_FRAMES="140", ZAMIN_SCREENSHOT=str(out / (name + "-authored-play.png")))
                run("ZaminEditor", name + "-authored-play", ["Cell navigation", "Sahna yuklash vaqti", "World checkpoint editor QA passed"], 180)
                if name == "WorldWorkshop3D":
                    # Keep the generated, portable project available for a separate native export test.
                    target = out / "GeneratedProject3D"
                    if target.exists():
                        shutil.rmtree(target)
                    shutil.copytree(project, target, ignore=shutil.ignore_patterns(".zamin", "Build"))
                print(f"PASS {name}: world wizard, linked anchor and generated world play", flush=True)
            reports.append({"Project": name, "CheckpointCells": len(doc["Cells"]), "Frames": 540, "Wizard": not args.no_editor})
    (out / "report.json").write_text(json.dumps(reports, indent=2) + "\n")


if __name__ == "__main__":
    main()
