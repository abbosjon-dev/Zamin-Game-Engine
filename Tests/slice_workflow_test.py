#!/usr/bin/env python3
"""Player/Editor acceptance of the compiler-free vertical slices (docs/VerticalSlices.md).

    python3 Tests/slice_workflow_test.py --game LanternKeeper [--build build] [--out DIR] [--no-editor] [--phases P0,P1,...]

Private project copies (overrides, start scene), one HOME per run, fixed 30 fps capture (ZAMIN_CAPTURE_FPS), software
GL. Phases (2D Chiroqbon):
  P0  keyboard tape: menu -> settings (music slider, back) -> new game -> move/Bolt/strike in Boglar
  P1  demo write (plan 3): new game -> level 1 -> level card -> transfer -> Yertola -> shrine save, then the pilot stops
  P2  fresh process resume: the menu continues from the level 2 save -> boss -> victory screen
  P3  fresh process, no replay: the menu offers no Continue after the victory and shows the best time
  P4  lose plan: three deaths -> defeat screen -> reload from the save -> level 1 completed
  E0  Editor Play of Boglar with the pilot
  E1  Editor Play of Yertola straight from the editor next to P1's stale Carry.json: a fresh level 2 run, no carry
Each phase: required markers, forbidden markers (GL/graph errors, SLICE_*_FAILED, stuck pilot where it must not be),
png_check of the last frame, and JSON checks of the transfer/save documents. Report: <out>/Report.json.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = ("GL_INVALID_", "Assertion failed", "Visual graph '", "Instruction budget exceeded", "SLICE_CONFIG_FAILED", "SLICE_IO_FAILED",
             "SLICE_ORACLE_FAILED", "nomli harakat topilmadi", "Snaryad vizual prefabi rad etildi")


def private_copy(work, start, pilot, plan, menu_demo):
    project = work / "LanternKeeper"
    if project.exists():
        shutil.rmtree(project)
    shutil.copytree(ROOT / "Projects/LanternKeeper", project, ignore=shutil.ignore_patterns(".zamin"))
    manifest = project / "LanternKeeper.zproject"
    data = json.loads(manifest.read_text())
    assert not data["Project"]["ScriptModulePath"], "the slice must stay compiler-free"
    data["Project"]["StartScene"] = f"Scenes/{start}.zscene"
    manifest.write_text(json.dumps(data, indent=2) + "\n")
    for scene in ("Boglar", "Yertola"):
        path = project / f"Assets/Scenes/{scene}.zscene"
        doc = json.loads(path.read_text())
        for e in doc["Entities"]:
            if e["Entity"] == 1000:
                o = json.loads(e["NativeScript"]["Fields"]["Overrides"]["Value"])
                o.update(Pilot=pilot, Plan=plan)
                e["NativeScript"]["Fields"]["Overrides"]["Value"] = json.dumps(o)
        path.write_text(json.dumps(doc) + "\n")
    if menu_demo:
        path = project / "Assets/Scenes/Menu.zscene"
        doc = json.loads(path.read_text())
        for e in doc["Entities"]:
            if e["Entity"] == 1100:
                e["NativeScript"]["Fields"]["Overrides"] = {"Type": "String", "Value": json.dumps({"Demo": True})}
        path.write_text(json.dumps(doc) + "\n")
    return manifest


def find(home, name):
    hits = sorted(home.rglob(name))
    return hits[0] if hits else None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--game", default="LanternKeeper", choices=("LanternKeeper",))
    p.add_argument("--build", type=Path, default=ROOT / "build")
    p.add_argument("--out", type=Path)
    p.add_argument("--no-editor", action="store_true")
    p.add_argument("--phases", default="P0,P1,P2,P3,P4,E0,E1")
    args = p.parse_args()
    build = args.build.resolve()
    out = (args.out or build / "Slices" / args.game).resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    runner = [] if os.environ.get("DISPLAY") else ["xvfb-run", "-a", "-s", "-screen 0 1600x1000x24 -nolisten tcp"]
    base_env = dict(os.environ, LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="2", ZAMIN_GL_DEBUG="1", ZAMIN_CAPTURE_FPS="30")
    phases = [x for x in args.phases.split(",") if x and not (args.no_editor and x.startswith("E"))]
    report = []
    homes = {"write": out / "home-run", "lose": out / "home-lose", "tape": out / "home-tape", "editor": out / "home-editor"}

    def run(name, home, start, frames, pilot=False, plan=0, menu_demo=False, demo_env=False, tape="", editor=False, resolution="960x540"):
        home.mkdir(parents=True, exist_ok=True)
        work = out / f"work-{name}"
        manifest = private_copy(work, start, pilot, plan, menu_demo)
        log = out / f"{name}.log"
        png = out / f"{name}.png"
        env = dict(base_env, HOME=str(home), ZAMIN_MAX_FRAMES=str(frames), ZAMIN_SCREENSHOT=str(png), ZAMIN_PLAYER_INPUT=tape)
        if demo_env:
            env["ZAMIN_DEMO"] = "1"
        else:
            env.pop("ZAMIN_DEMO", None)
        if editor:
            env.update(ZAMIN_EDITOR_PLAY="1", ZAMIN_EDITOR_SELECT="Nilufar")
            cmd = [str(build / "bin/ZaminEditor"), str(manifest)]
        else:
            cmd = [str(build / "bin/ZaminPlayer"), str(manifest), "--no-splash", "--resolution", resolution]
        begin = time.time()
        with log.open("w") as stream:
            subprocess.run(runner + cmd, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=1500, check=True)
        text = log.read_text(errors="replace")
        for marker in FORBIDDEN:
            if marker in text:
                raise RuntimeError(f"{name}: forbidden '{marker}' in {log}")
        subprocess.run(["python3", str(ROOT / "Tests/png_check.py"), str(png)], check=True)
        return text, time.time() - begin

    def need(name, text, markers, order=True):
        last = -1
        for m in markers:
            at = text.find(m, last + 1 if order else 0)
            if at < 0:
                raise RuntimeError(f"{name}: missing '{m}' (after the previous marker)")
            last = at if order else last

    for phase in phases:
        if phase == "P0":
            tape = ("40:Down;46:Enter;70:Right;76:Right;82:Right;96:Down;100:Down;104:Down;108:Down;114:Enter;130:Up;136:Enter;"
                    "240:hold=D;300:release=D;306:hold=W;330:release=W;336:K;350:J;360:Tab;380:Escape")
            text, seconds = run("P0-keyboard", homes["tape"], "Menu", 420, tape=tape)
            need("P0", text, ["Menyu: Asosiy davom=0", "Menyu: Sozlamalar", "SLICE_SETTINGS music=", "Menyu: Asosiy", "Menyu: Yangi o'yin",
                              "SLICE_LEVEL_START lvl=1 entry=new"])
            if "SLICE_SETTINGS music=0.55" in text:
                raise RuntimeError("P0: the music slider did not move")
        elif phase == "P1":
            text, seconds = run("P1-demo-write", homes["write"], "Menu", 4600, pilot=True, plan=3, demo_env=True)
            need("P1", text, ["Menyu: Yangi o'yin", "SLICE_LEVEL_START lvl=1 entry=new", "SLICE_SAVED lvl=1 at=shrine", "SLICE_LEVEL_COMPLETE lvl=1",
                              "SLICE_RESUMED lvl=2 source=carry", "SLICE_LEVEL_START lvl=2 entry=carry", "SLICE_SAVED lvl=2 at=shrine"])
            if "SLICE_BOT_STUCK" in text:
                raise RuntimeError("P1: the pilot got stuck")
            transfer = json.loads(find(homes["write"], "Carry.json").read_text())
            save = json.loads(find(homes["write"], "Run2.json").read_text())
            assert transfer["Format"] == "Zamin.GameplayTransfer" and transfer["Version"] == 1 and transfer["Payload"]["Key"] == "Hero"
            assert transfer["Payload"]["Quests"]["XP"] == 200 and transfer["Payload"]["Scene"] == "Boglar"
            assert save["Format"] == "Zamin.GameSave" and save["Version"] == 4 and save["Payload"]["Scene"] == "Yertola"
        elif phase == "P2":
            text, seconds = run("P2-resume-victory", homes["write"], "Menu", 3600, pilot=True, plan=0, menu_demo=True)
            need("P2", text, ["Menyu: Asosiy davom=1", "Menyu: Davom etish", "SLICE_RESUMED lvl=2 source=save", "SLICE_LEVEL_START lvl=2 entry=continue",
                              "SLICE_TRACE ev=boss a=engaged", "SLICE_TRACE ev=boss a=defeated", "SLICE_VICTORY game=Chiroqbon"])
            line = next(x for x in text.splitlines() if "SLICE_VICTORY" in x)
            for term in ("xp=850", "level=4", "quests=5/5"):
                if term not in line:
                    raise RuntimeError(f"P2: victory without {term}: {line}")
            if "SLICE_BOT_STUCK" in text:
                raise RuntimeError("P2: the pilot got stuck")
        elif phase == "P3":
            text, seconds = run("P3-no-replay-menu", homes["write"], "Menu", 90)
            need("P3", text, ["Menyu: Asosiy davom=0"])
        elif phase == "P4":
            text, seconds = run("P4-lose", homes["lose"], "Menu", 5600, pilot=True, plan=1, menu_demo=True)
            need("P4", text, ["Menyu: Yangi o'yin", "SLICE_TRACE ev=death", "SLICE_DEFEAT lvl=1 deaths=3", "Menyu: Oxirgi saqlashdan",
                              "SLICE_RESUMED lvl=1 source=save", "SLICE_LEVEL_COMPLETE lvl=1"])
            # the lose plan stalls at the locked gate on purpose (no fighting until the defeat); after the reload it must not
            if "SLICE_BOT_STUCK" in text[text.find("Menyu: Oxirgi saqlashdan"):]:
                raise RuntimeError("P4: the pilot got stuck after the reload")
            complete = next(x for x in text.splitlines() if "SLICE_LEVEL_COMPLETE" in x)
            if " xp=200 " not in complete or " coins=13 " not in complete:
                raise RuntimeError(f"P4: progress replayed or lost after the reload: {complete}")
        elif phase == "E0":
            text, seconds = run("E0-editor-boglar", homes["editor"], "Boglar", 420, pilot=True, editor=True)
            need("E0", text, ["SLICE_LEVEL_START lvl=1 entry=new", "SLICE_TRACE ev=collect a=Herb"])
        elif phase == "E1":
            if not find(homes["write"], "Carry.json"):
                raise RuntimeError("E1 needs P1's transfer document (run P1 first)")
            text, seconds = run("E1-editor-yertola", homes["write"], "Yertola", 360, pilot=True, editor=True)
            need("E1", text, ["SLICE_SAVED lvl=2 at=auto", "SLICE_LEVEL_START lvl=2 entry=new", "SLICE_TRACE ev=collect a=Coin"])
            if "source=carry" in text:
                raise RuntimeError("E1: a direct level 2 start picked up the stale transfer")
        else:
            raise SystemExit(f"unknown phase {phase}")
        report.append({"Phase": phase, "Passed": True, "Seconds": round(seconds, 1)})
        print(f"PASS {args.game}/{phase} ({seconds:.0f} s)", flush=True)
    (out / "Report.json").write_text(json.dumps({"Game": args.game, "Passed": True, "Phases": report}, indent=2) + "\n")
    for p_ in out.glob("work-*"):
        shutil.rmtree(p_, ignore_errors=True)


if __name__ == "__main__":
    main()
