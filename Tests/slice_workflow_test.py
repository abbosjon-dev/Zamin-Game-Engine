#!/usr/bin/env python3
"""Player/Editor acceptance of the compiler-free vertical slices (docs/VerticalSlices.md).

    python3 Tests/slice_workflow_test.py --game LanternKeeper|BeaconPass [--build build] [--out DIR] [--no-editor] [--exports] [--phases P0,P1,...]

Private project copies (overrides, start scene), one HOME per run, fixed 30 fps capture (ZAMIN_CAPTURE_FPS), software
GL; the 3D slice runs at 640x360 with the Low quality level to keep llvmpipe time bounded. Phases (2D Lantern Keeper):
  P0  keyboard tape: menu -> settings (music slider, back) -> new game -> move/Bolt/strike/inventory/pause in Orchards
  P1  demo write (plan 3): new game -> level 1 -> level card -> transfer -> Crypt -> shrine save, then the pilot stops
  C   damaged saves on copies of P1's files, fresh processes: a changed byte in the shrine save -> its backup (the level 2
      autosave) with the "recovered" banner; the save and its backup torn -> "SLICE_IO_FAILED load", the visible
      message, level 2 again from the transfer
  P2  fresh process resume: the menu continues from the level 2 save -> boss -> victory screen
  P3  fresh process, no replay: the menu offers no Continue after the victory and shows the best time
  P4  lose plan: three deaths -> defeat screen -> reload from the save -> level 1 completed
  P6  gamepad tape: menu (settings slider on the D-pad, back) -> new game -> stick, Bolt, strike, the bag, pause ->
      "Save and quit to menu" (the quit save) -> the menu offers Continue
  E0  Editor Play of Orchards with the pilot
  E1  Editor Play of Crypt straight from the editor next to P1's stale Carry.json: a fresh level 2 run, no carry
  E2  Editor Play of the menu (demo): New game, the scene switch into Orchards inside Play mode
  X   (--exports) the shipped game exported (Tests/export_test.sh); its launcher with ZAMIN_DEMO=1 plays the attract run and
      is killed mid level 2 (after the carry autosave); the folder is really moved (mv); from the new place a fresh
      process continues the save to the victory screen (screenshot), shows no Continue afterwards, and starts a new game
  D   determinism: P1's level 1 trace (the Player, 960x540) equals the headless trace (ZaminTests LanternKeeperDeterminism)
Phases (3D Beacon Pass):
  P0  keyboard tape: menu -> settings (music slider, back) -> new game -> run, jump, strike, Bolt, inventory, pause in Pass
  P1  demo write (plan 3): new game -> camp chest, ledge, gate guards, gate -> courtyard shrine save (v4), then the pilot
      stops
  C   damaged saves (as 2D): the backup is the camp autosave; torn save and backup -> a fresh start at the camp
  P2  fresh process resume: the menu continues from the courtyard save -> stable, plateau shrine, Warden -> beacon ->
      victory screen
  P3  fresh process, no replay: the menu offers no Continue after the victory
  P4  lose plan: three deaths at the gate -> defeat screen -> reload from the autosave -> the gate quest once (100 XP)
  P6  gamepad tape: menu -> new game -> stick run, jump, look, strike, Bolt, the bag, pause -> quit save -> Continue offered
  E0  Editor Play of Pass with the pilot
  E2  Editor Play of the menu (demo): New game, the scene switch into Pass inside Play mode
  X   (--exports) as 2D, killed after the courtyard shrine save
  D   determinism: P1's trace (the Player) is the beginning of the headless trace (ZaminTests BeaconPassDeterminism)
Each phase: required markers, forbidden markers (GL/graph errors, SLICE_*_FAILED, stuck pilot where it must not be),
png_check of the last frame, and JSON checks of the transfer/save documents. Report: <out>/Report.json.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = ("GL_INVALID_", "Assertion failed", "Visual graph '", "Instruction budget exceeded", "SLICE_CONFIG_FAILED", "SLICE_IO_FAILED",
             "SLICE_ORACLE_FAILED", "is not a known action", "Projectile visual prefab rejected")
UNREADABLE = "The save could not be read - the level starts again"
GAMES = {
    # Per game: its level scenes (the hero is entity 1000 in each), the hero's name (Editor selection), the window size and
    # quality level of the Player runs, the default phases (and the export phase with --exports), the save slot the
    # damaged-save phase breaks, the GameState prefix, the attract run's kill frame (between the save the export resumes
    # and the next one) and the resume run's frames.
    "LanternKeeper": dict(levels=("Orchards", "Crypt"), hero="Nilufar", resolution="960x540", quality=None, phases="P0,P1,C,P2,P3,P4,P6,E0,E1,E2,D",
                          slot="Run2", gs="LK", product="LanternKeeper", kill=3300, resume=4200),
    "BeaconPass": dict(levels=("Pass",), hero="Temur", resolution="640x360", quality="Low", phases="P0,P1,C,P2,P3,P4,P6,E0,E2,D",
                       slot="Run1", gs="BP", product="BeaconPass", kill=1500, resume=1900),
}
# The gamepad tape (P6). Menu: D-pad down -> Settings (A), the music slider right x3, down x5 -> Back (A), up -> New
# game (A); in the level: the left stick, Bolt (RB), strike (X), the bag (Back) and closing it (B), pause (Start), down x2 ->
# "Save and quit to menu" (A).
PAD_MENU = ("40:pad=DPadDown;46:pad=A;70:pad=DPadRight;76:pad=DPadRight;82:pad=DPadRight;96:pad=DPadDown;100:pad=DPadDown;104:pad=DPadDown;"
            "108:pad=DPadDown;111:pad=DPadDown;114:pad=A;130:pad=DPadUp;136:pad=A;")
PAD_TAPES = {
    "LanternKeeper": PAD_MENU + ("240:stick=LeftX,1;300:stick=LeftX,0;306:stick=LeftY,-1;330:stick=LeftY,0;336:pad=RightBumper;350:pad=X;360:pad=Back;"
                                 "380:pad=B;400:pad=Start;412:pad=DPadDown;418:pad=DPadDown;426:pad=A"),
    "BeaconPass": PAD_MENU + ("230:stick=LeftY,-1;262:pad=A;300:stick=LeftY,0;304:stick=RightX,0.6;320:stick=RightX,0;326:pad=X;340:pad=RightBumper;"
                              "352:pad=Back;366:pad=B;380:pad=Start;392:pad=DPadDown;398:pad=DPadDown;406:pad=A"),
}


def private_copy(work, game, start, pilot, plan, menu_demo):
    project = work / game
    if project.exists():
        shutil.rmtree(project)
    shutil.copytree(ROOT / "Projects" / game, project, ignore=shutil.ignore_patterns(".zamin", "Build"))
    manifest = project / f"{game}.zproject"
    data = json.loads(manifest.read_text())
    assert not data["Project"]["ScriptModulePath"], "the slice must stay compiler-free"
    data["Project"]["StartScene"] = f"Scenes/{start}.zscene"
    manifest.write_text(json.dumps(data, indent=2) + "\n")
    for scene in GAMES[game]["levels"]:
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


def save_payload(path):
    doc = json.loads(path.read_text())
    assert doc["Format"] == "Zamin.GameSave" and doc["Version"] == 4, (path, doc.get("Format"), doc.get("Version"))
    return doc["Payload"]


def hero_of(payload):
    return next(e for e in payload["Entities"] if e["Key"] == "Hero")


def game_state(home, gs):
    """The persisted GameState keys of a game (savedata.json under HOME)."""
    data = json.loads(find(home, "savedata.json").read_text())
    return {key[len("GameState."):]: value for key, value in data.get("Data", {}).items() if key.startswith(f"GameState.{gs}.")}


def tear(path):
    path.write_text(path.read_text()[:96])


def tamper(path):
    text = path.read_text()
    at = text.index('"Health":') + 9
    path.write_text(text[:at] + "1" + text[at:])


def traces(text, begin=None, end=None):
    """The ordered SLICE_TRACE lines of a log (from the first `begin` marker to the first `end` marker after it)."""
    if begin:
        text = text[text.find(begin):]
    if end and end in text:
        text = text[:text.find(end)]
    return [m.group(0).strip() for m in re.finditer(r"SLICE_TRACE ev=\S+(?: a=\S+)?", text.replace("\x1b[0m", ""))]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--game", default="LanternKeeper", choices=tuple(GAMES))
    p.add_argument("--build", type=Path, default=ROOT / "build")
    p.add_argument("--out", type=Path)
    p.add_argument("--no-editor", action="store_true")
    p.add_argument("--exports", action="store_true", help="also the relocated export phase X")
    p.add_argument("--phases", default="")
    args = p.parse_args()
    game = GAMES[args.game]
    build = args.build.resolve()
    out = (args.out or build / "Slices" / args.game).resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    runner = [] if os.environ.get("DISPLAY") else ["xvfb-run", "-a", "-s", "-screen 0 1600x1000x24 -nolisten tcp"]
    base_env = dict(os.environ, LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="2", ZAMIN_GL_DEBUG="1", ZAMIN_CAPTURE_FPS="30")
    for key in ("ZAMIN_DEMO", "ZAMIN_SCREENSHOT_ON", "ZAMIN_PLAYER_INPUT"):
        base_env.pop(key, None)
    phases = args.phases or game["phases"] + (",X" if args.exports else "")
    phases = [x for x in phases.split(",") if x and not (args.no_editor and x.startswith("E"))]
    report = []
    homes = {"write": out / "home-run", "lose": out / "home-lose", "tape": out / "home-tape", "editor": out / "home-editor", "pad": out / "home-pad"}

    def run(name, home, start, frames, pilot=False, plan=0, menu_demo=False, demo_env=False, tape="", editor=False, launcher=None, extra=None,
            allow=(), check_png=True):
        home.mkdir(parents=True, exist_ok=True)
        log = out / f"{name}.log"
        png = out / f"{name}.png"
        env = dict(base_env, HOME=str(home), ZAMIN_MAX_FRAMES=str(frames), ZAMIN_SCREENSHOT=str(png), ZAMIN_PLAYER_INPUT=tape, **(extra or {}))
        if demo_env:
            env["ZAMIN_DEMO"] = "1"
        if launcher:
            cmd = [str(launcher), "--no-splash", "--resolution", game["resolution"]] + (["--quality", game["quality"]] if game["quality"] else [])
        else:
            manifest = private_copy(out / f"work-{name}", args.game, start, pilot, plan, menu_demo)
            if editor:
                env.update(ZAMIN_EDITOR_PLAY="1")
                if start != "Menu":
                    env["ZAMIN_EDITOR_SELECT"] = game["hero"]
                cmd = [str(build / "bin/ZaminEditor"), str(manifest)]
            else:
                cmd = [str(build / "bin/ZaminPlayer"), str(manifest), "--no-splash", "--resolution", game["resolution"]]
                if game["quality"]:
                    cmd += ["--quality", game["quality"]]
        begin = time.time()
        with log.open("w") as stream:
            subprocess.run(runner + cmd, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=1800, check=True)
        text = log.read_text(errors="replace")
        for marker in FORBIDDEN:
            if marker in text and marker not in allow:
                raise RuntimeError(f"{name}: forbidden '{marker}' in {log}")
        if check_png:
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
        detail = ""
        if phase == "C":
            text, seconds, detail = damaged_phase(args.game, game, run, need, homes, out)
        elif phase == "P6":
            text, seconds, detail = pad_phase(args.game, game, run, need, homes)
        elif phase == "E2":
            text, seconds = run("E2-editor-menu", homes["editor"] / "menu", "Menu", 420 if args.game == "BeaconPass" else 400, pilot=True, menu_demo=True,
                                editor=True)
            need("E2", text, ["Menu: Main continue=0", "Menu: New game", "SLICE_SAVED lvl=1 at=auto", "SLICE_LEVEL_START lvl=1 entry=new"])
        elif phase == "X":
            text, seconds, detail = export_phase(args.game, game, run, need, out, build, base_env)
        elif phase == "D":
            text, seconds, detail = determinism_phase(args.game, game, out, build, base_env)
        elif args.game == "BeaconPass":
            text, seconds = beacon_phase(phase, run, need, homes, find)
        elif phase == "P0":
            tape = ("40:Down;46:Enter;70:Right;76:Right;82:Right;96:Down;100:Down;104:Down;108:Down;111:Down;114:Enter;130:Up;136:Enter;"
                    "240:hold=D;300:release=D;306:hold=W;330:release=W;336:K;350:J;360:Tab;380:Escape")
            text, seconds = run("P0-keyboard", homes["tape"], "Menu", 420, tape=tape)
            need("P0", text, ["Menu: Main continue=0", "Menu: Settings", "SLICE_SETTINGS music=", "Menu: Main", "Menu: New game",
                              "SLICE_LEVEL_START lvl=1 entry=new"])
            if "SLICE_SETTINGS music=0.55" in text:
                raise RuntimeError("P0: the music slider did not move")
        elif phase == "P1":
            text, seconds = run("P1-demo-write", homes["write"], "Menu", 4600, pilot=True, plan=3, demo_env=True)
            need("P1", text, ["Menu: New game", "SLICE_LEVEL_START lvl=1 entry=new", "SLICE_SAVED lvl=1 at=shrine", "SLICE_LEVEL_COMPLETE lvl=1",
                              "SLICE_RESUMED lvl=2 source=carry", "SLICE_LEVEL_START lvl=2 entry=carry", "SLICE_SAVED lvl=2 at=shrine"])
            if "SLICE_BOT_STUCK" in text:
                raise RuntimeError("P1: the pilot got stuck")
            transfer = json.loads(find(homes["write"], "Carry.json").read_text())
            save = json.loads(find(homes["write"], "Run2.json").read_text())
            assert transfer["Format"] == "Zamin.GameplayTransfer" and transfer["Version"] == 1 and transfer["Payload"]["Key"] == "Hero"
            assert transfer["Payload"]["Quests"]["XP"] == 200 and transfer["Payload"]["Scene"] == "Orchards"
            assert save["Format"] == "Zamin.GameSave" and save["Version"] == 4 and save["Payload"]["Scene"] == "Crypt"
        elif phase == "P2":
            text, seconds = run("P2-resume-victory", homes["write"], "Menu", 3600, pilot=True, plan=0, menu_demo=True)
            need("P2", text, ["Menu: Main continue=1", "Menu: Continue", "SLICE_RESUMED lvl=2 source=save", "SLICE_LEVEL_START lvl=2 entry=continue",
                              "SLICE_TRACE ev=boss a=engaged", "SLICE_TRACE ev=boss a=defeated", "SLICE_VICTORY game=LanternKeeper"])
            line = next(x for x in text.splitlines() if "SLICE_VICTORY" in x)
            for term in ("xp=850", "level=4", "quests=5/5"):
                if term not in line:
                    raise RuntimeError(f"P2: victory without {term}: {line}")
            if "SLICE_BOT_STUCK" in text:
                raise RuntimeError("P2: the pilot got stuck")
        elif phase == "P3":
            text, seconds = run("P3-no-replay-menu", homes["write"], "Menu", 90)
            need("P3", text, ["Menu: Main continue=0"])
        elif phase == "P4":
            text, seconds = run("P4-lose", homes["lose"], "Menu", 5600, pilot=True, plan=1, menu_demo=True)
            need("P4", text, ["Menu: New game", "SLICE_TRACE ev=death", "SLICE_DEFEAT lvl=1 deaths=3", "Menu: From the last save",
                              "SLICE_RESUMED lvl=1 source=save", "SLICE_LEVEL_COMPLETE lvl=1"])
            # the lose plan stalls at the locked gate on purpose (no fighting until the defeat); after the reload it must not
            if "SLICE_BOT_STUCK" in text[text.find("Menu: From the last save"):]:
                raise RuntimeError("P4: the pilot got stuck after the reload")
            complete = next(x for x in text.splitlines() if "SLICE_LEVEL_COMPLETE" in x)
            if " xp=200 " not in complete or " coins=13 " not in complete:
                raise RuntimeError(f"P4: progress replayed or lost after the reload: {complete}")
        elif phase == "E0":
            text, seconds = run("E0-editor-orchards", homes["editor"], "Orchards", 420, pilot=True, editor=True)
            need("E0", text, ["SLICE_LEVEL_START lvl=1 entry=new", "SLICE_TRACE ev=collect a=Herb"])
        elif phase == "E1":
            if not find(homes["write"], "Carry.json"):
                raise RuntimeError("E1 needs P1's transfer document (run P1 first)")
            text, seconds = run("E1-editor-crypt", homes["write"], "Crypt", 360, pilot=True, editor=True)
            need("E1", text, ["SLICE_SAVED lvl=2 at=auto", "SLICE_LEVEL_START lvl=2 entry=new", "SLICE_TRACE ev=collect a=Coin"])
            if "source=carry" in text:
                raise RuntimeError("E1: a direct level 2 start picked up the stale transfer")
        else:
            raise SystemExit(f"unknown phase {phase}")
        report.append({"Phase": phase, "Passed": True, "Seconds": round(seconds, 1), **({"Detail": detail} if detail else {})})
        print(f"PASS {args.game}/{phase} ({seconds:.0f} s){': ' + detail if detail else ''}", flush=True)
    (out / "Report.json").write_text(json.dumps({"Game": args.game, "Passed": True, "Phases": report}, indent=2) + "\n")
    for p_ in out.glob("work-*"):
        shutil.rmtree(p_, ignore_errors=True)


def damaged_phase(name, game, run, need, homes, out):
    """C: copies of P1's files with a damaged shrine save, each continued by a fresh process (the demo menu)."""
    if not find(homes["write"], f"{game['slot']}.json.bak"):
        raise RuntimeError("C needs P1's save and its backup (run P1 first)")
    begin, details = time.time(), []
    level = 2 if name == "LanternKeeper" else 1
    for case in ("tampered", "torn"):
        home = out / f"home-damaged-{case}"
        if home.exists():
            shutil.rmtree(home)
        shutil.copytree(homes["write"], home)
        primary = find(home, f"{game['slot']}.json")
        backup = Path(str(primary) + ".bak")
        expected = hero_of(save_payload(backup))
        if case == "tampered":
            tamper(primary)
        else:
            tear(primary)
            tear(backup)
        png = out / f"C-{case}-banner.png"
        text, _ = run(f"C-{case}", home, "Menu", 260, pilot=True, plan=3, menu_demo=True, allow=("SLICE_IO_FAILED",) if case == "torn" else (),
                      extra={"ZAMIN_SCREENSHOT_ON": f"SLICE_LEVEL_START|24|{png}"})
        subprocess.run(["python3", str(ROOT / "Tests/png_check.py"), str(png)], check=True)
        if case == "tampered":
            need("C", text, ["Menu: Main continue=1", "Menu: Continue", f"SLICE_RESUMED lvl={level} source=save recovered=1", f"SLICE_LEVEL_START lvl={level} entry=continue"])
            # the backup (the level's autosave) became the run: its hero, rewritten by nothing yet
            hero = hero_of(save_payload(backup))
            assert hero["Quests"]["XP"] == expected["Quests"]["XP"], (hero["Quests"]["XP"], expected["Quests"]["XP"])
            details.append(f"tampered -> backup (xp {expected['Quests']['XP']})")
        else:
            fallback = "SLICE_RESUMED lvl=2 source=carry" if name == "LanternKeeper" else "SLICE_SAVED lvl=1 at=auto"
            need("C", text, ["Menu: Continue", "SLICE_IO_FAILED load", fallback, f"SLICE_LEVEL_START lvl={level} entry=continue"])
            if "SLICE_IO_FAILED autosave" in text or "SLICE_IO_FAILED transfer" in text:
                raise RuntimeError("C: the fallback after an unreadable save failed")
            # the level's entry wrote a valid save again
            hero = hero_of(save_payload(find(home, f"{game['slot']}.json")))
            want = 200 if name == "LanternKeeper" else 0
            if hero["Quests"]["XP"] != want:
                raise RuntimeError(f"C: the fallback run has {hero['Quests']['XP']} XP, not {want}")
            details.append(f"torn -> {'transfer' if level == 2 else 'fresh start'} (xp {want}), banner '{UNREADABLE}'")
    return text, time.time() - begin, "; ".join(details)


def pad_phase(name, game, run, need, homes):
    """P6: the gamepad tape through the menu and the level, ending with the pause menu's quit save."""
    text, seconds = run("P6-gamepad", homes["pad"], "Menu", 520, tape=PAD_TAPES[name])
    need("P6", text, ["Menu: Main continue=0", "Input device: gamepad", "Menu: Settings", "SLICE_SETTINGS music=", "Menu: New game",
                      "SLICE_SAVED lvl=1 at=auto", "SLICE_LEVEL_START lvl=1 entry=new", "SLICE_SAVED lvl=1 at=quit", "Menu: Main continue=1"])
    if "SLICE_SETTINGS music=0.55" in text:
        raise RuntimeError("P6: the music slider did not move on the D-pad")
    quit_save = find(homes["pad"], "Run1.json")
    moved = hero_of(save_payload(quit_save))["Position"]
    spawn = hero_of(save_payload(Path(str(quit_save) + ".bak")))["Position"]
    distance = sum((a - b) ** 2 for a, b in zip(moved, spawn)) ** 0.5
    if distance < 0.5:
        raise RuntimeError(f"P6: the hero did not move on the stick ({distance:.2f} m)")
    state = game_state(homes["pad"], game["gs"])
    if state.get(f"{game['gs']}.Run.Save") != 1:
        raise RuntimeError(f"P6: the quit save is not the run's save: {state}")
    return text, seconds, f"stick moved the hero {distance:.2f} m, quit save v4, Continue offered"


def export_phase(name, game, run, need, out, build, base_env):
    """X: export the shipped game, play its attract run until killed after a save, move the folder, resume from the new
    place to the victory screen, then no Continue, then a new game."""
    begin = time.time()
    work = out / "export-work"
    project = out / "export-project" / name
    if project.parent.exists():
        shutil.rmtree(project.parent)
    shutil.copytree(ROOT / "Projects" / name, project, ignore=shutil.ignore_patterns(".zamin", "Build"))
    env = dict(base_env, ZAMIN_BUILD_DIR=str(build))
    subprocess.run(["bash", str(ROOT / "Tests/export_test.sh"), "--project", str(project), "--frames", "60", "--shot-frames", "60", "--work", str(work),
                    "--out", str(out / "export")], env=env, timeout=1800, check=True)
    files = sum(1 for f in (work / "export").rglob("*") if f.is_file())
    size = sum(f.stat().st_size for f in (work / "export").rglob("*") if f.is_file()) / (1024 * 1024)
    home = out / "home-export"
    # the attract run, killed (ZAMIN_MAX_FRAMES) between the save it leaves and the next one
    text, _ = run("X1-export-attract-killed", home, "", game["kill"], demo_env=True, launcher=work / "export" / f"{name}.sh")
    if name == "LanternKeeper":
        need("X1", text, ["Menu: New game", "SLICE_LEVEL_COMPLETE lvl=1", "SLICE_RESUMED lvl=2 source=carry", "SLICE_SAVED lvl=2 at=auto"])
        if "SLICE_SAVED lvl=2 at=shrine" in text:
            raise RuntimeError("X1: the attract run got to the level 2 shrine before the kill (lower the kill frame)")
        saved = hero_of(save_payload(find(home, "Run2.json")))
    else:
        need("X1", text, ["Menu: New game", "SLICE_SAVED lvl=1 at=auto", "SLICE_SAVED lvl=1 at=shrine"])
        if text.count("SLICE_SAVED lvl=1 at=shrine") != 1:
            raise RuntimeError("X1: the attract run is not between the courtyard and the plateau shrine at the kill")
        saved = hero_of(save_payload(find(home, "Run1.json")))
    # a real relocation of the exported folder
    relocated = out / "export-relocated" / name
    if relocated.parent.exists():
        shutil.rmtree(relocated.parent)
    relocated.parent.mkdir(parents=True)
    shutil.move(str(work / "export"), str(relocated))
    if (work / "export").exists():
        raise RuntimeError("X: the export folder was not moved")
    launcher = relocated / f"{name}.sh"
    victory = out / f"P5-slice-export-relocated-{name}.png"
    text, _ = run("X2-relocated-resume-victory", home, "", game["resume"], demo_env=True, launcher=launcher,
                  extra={"ZAMIN_SCREENSHOT_ON": f"SLICE_VICTORY|120|{victory}|quit"}, check_png=False)
    level = 2 if name == "LanternKeeper" else 1
    need("X2", text, ["Menu: Main continue=1", "Menu: Continue", f"SLICE_RESUMED lvl={level} source=save recovered=0", f"SLICE_VICTORY game={game['product']}"])
    subprocess.run(["python3", str(ROOT / "Tests/png_check.py"), str(victory)], check=True)
    line = next(x for x in text.splitlines() if "SLICE_VICTORY" in x)
    xp = "xp=850" if name == "LanternKeeper" else "xp=800"
    if xp not in line or "level=4" not in line:
        raise RuntimeError(f"X2: victory without {xp} level=4: {line}")
    state = game_state(home, game["gs"])
    best = state.get(f"{game['gs']}.Best.Time")
    if state.get(f"{game['gs']}.Run.Finished") != 1 or not best:
        raise RuntimeError(f"X2: the victory was not recorded: {state}")
    text, _ = run("X3-relocated-no-replay", home, "", 90, launcher=launcher)
    need("X3", text, ["Menu: Main continue=0"])
    text, _ = run("X4-relocated-new-game", home, "", 300, demo_env=True, launcher=launcher)
    need("X4", text, ["Menu: Main continue=0", "Menu: New game", "SLICE_SAVED lvl=1 at=auto", "SLICE_LEVEL_START lvl=1 entry=new"])
    fresh = hero_of(save_payload(find(home, "Run1.json")))
    state = game_state(home, game["gs"])
    if fresh["Quests"]["XP"] != 0 or state.get(f"{game['gs']}.Best.Time") != best:
        raise RuntimeError(f"X4: the new game is not fresh or the record changed: xp {fresh['Quests']['XP']}, {state}")
    shutil.rmtree(project.parent, ignore_errors=True)
    shutil.rmtree(work, ignore_errors=True)
    won = re.sub(r"\x1b\[[0-9;]*m", "", line.split("SLICE_VICTORY ")[-1]).strip()
    detail = (f"export {files} files {size:.1f} MiB; killed after the save (xp {saved['Quests']['XP']}), moved, resumed to victory "
              f"({won}), no Continue, new game xp 0, best time {best:.1f} kept")
    return text, time.time() - begin, detail


def determinism_phase(name, game, out, build, base_env):
    """D: the Player's trace of P1 against the headless one of the same run (fixed frame time in both)."""
    begin = time.time()
    log = out / "P1-demo-write.log"
    if not log.exists():
        raise RuntimeError("D needs P1's log (run P1 first)")
    trace = out / "headless-trace.txt"
    test = "LanternKeeperDeterminism" if name == "LanternKeeper" else "BeaconPassDeterminism"
    env = dict(base_env, ZAMIN_SLICE_TRACE_OUT=str(trace))
    subprocess.run([str(build / "bin/ZaminTests"), "--filter", test], env=env, check=True, timeout=1800, stdout=subprocess.DEVNULL)
    headless = [x.strip() for x in trace.read_text().splitlines() if x.strip()]
    text = log.read_text(errors="replace")
    if name == "LanternKeeper":
        player = traces(text, "SLICE_LEVEL_START lvl=1", "SLICE_LEVEL_COMPLETE lvl=1")
        same = player == headless
    else:
        player = traces(text, "SLICE_LEVEL_START lvl=1")
        same = len(player) >= 5 and player == headless[:len(player)]
    if not same:
        for i in range(max(len(player), len(headless))):
            print(f"    {player[i] if i < len(player) else '-':40s} | {headless[i] if i < len(headless) else '-'}")
        raise RuntimeError(f"D: the Player's trace ({len(player)} events) differs from the headless one ({len(headless)})")
    return text, time.time() - begin, f"{len(player)} / {len(headless)} trace events equal (Player 960x540 or 640x360 vs headless)"


def beacon_phase(phase, run, need, homes, find):
    """One phase of the 3D slice (Beacon Pass); returns (log text, seconds)."""
    if phase == "P0":
        # menu: Down -> Settings, music slider Right x3, Down x5 -> Back, Up -> New game; in Pass: run north,
        # jump, strike, Bolt, the bag (Tab, Escape), pause (Escape) -> Down -> Inventar (Enter) -> Escape, Escape
        tape = ("40:Down;46:Enter;70:Right;76:Right;82:Right;96:Down;100:Down;104:Down;108:Down;111:Down;114:Enter;130:Up;136:Enter;"
                "230:hold=W;262:Space;300:release=W;306:J;330:K;344:Tab;356:Escape;366:Escape;378:Down;384:Enter;400:Escape;412:Escape")
        text, seconds = run("P0-keyboard", homes["tape"], "Menu", 430, tape=tape)
        need("P0", text, ["Menu: Main continue=0", "Menu: Settings", "SLICE_SETTINGS music=", "Menu: Main", "Menu: New game",
                          "SLICE_SAVED lvl=1 at=auto", "SLICE_LEVEL_START lvl=1 entry=new", "Menu: Inventory"])
        if "SLICE_SETTINGS music=0.55" in text:
            raise RuntimeError("P0: the music slider did not move")
    elif phase == "P1":
        text, seconds = run("P1-demo-write", homes["write"], "Menu", 1560, pilot=True, plan=3, demo_env=True)
        need("P1", text, ["Menu: New game", "SLICE_SAVED lvl=1 at=auto", "SLICE_LEVEL_START lvl=1 entry=new", "SLICE_TRACE ev=collect a=Sabre",
                          "SLICE_TRACE ev=claim a=Pass", "SLICE_SAVED lvl=1 at=shrine"])
        if "SLICE_BOT_STUCK" in text:
            raise RuntimeError("P1: the pilot got stuck")
        payload = save_payload(find(homes["write"], "Run1.json"))
        hero = hero_of(payload)
        stones = hero["Items"].get("FireStone", 0)
        assert payload["Scene"] == "Pass" and payload["Schema"] == "BeaconPass-1" and "Combat" in payload, payload.keys()
        assert hero["Quests"]["XP"] == 100 and stones == 2 and hero["Health"] == 120, (hero["Quests"]["XP"], stones, hero["Health"])
    elif phase == "P2":
        text, seconds = run("P2-resume-victory", homes["write"], "Menu", 1500, pilot=True, plan=0, menu_demo=True)
        need("P2", text, ["Menu: Main continue=1", "Menu: Continue", "SLICE_RESUMED lvl=1 source=save recovered=0", "SLICE_LEVEL_START lvl=1 entry=continue",
                          "SLICE_TRACE ev=claim a=Stones", "SLICE_TRACE ev=boss a=engaged", "SLICE_TRACE ev=boss a=defeated", "SLICE_TRACE ev=beacon a=lit",
                          "SLICE_VICTORY game=BeaconPass", "Menu: Victory"])
        line = next(x for x in text.splitlines() if "SLICE_VICTORY" in x)
        for term in ("xp=800", "level=4", "quests=4/4"):
            if term not in line:
                raise RuntimeError(f"P2: victory without {term}: {line}")
        if "SLICE_BOT_STUCK" in text or "SLICE_ORACLE_FAILED" in text:
            raise RuntimeError("P2: the pilot got stuck or the oracle failed")
    elif phase == "P3":
        text, seconds = run("P3-no-replay-menu", homes["write"], "Menu", 90)
        need("P3", text, ["Menu: Main continue=0"])
    elif phase == "P4":
        text, seconds = run("P4-lose", homes["lose"], "Menu", 4500, pilot=True, plan=1, menu_demo=True)
        need("P4", text, ["Menu: New game", "SLICE_TRACE ev=death", "SLICE_DEFEAT lvl=1 deaths=3", "Menu: From the last save",
                          "SLICE_RESUMED lvl=1 source=save", "SLICE_TRACE ev=claim a=Pass"])
        if "SLICE_BOT_STUCK" in text[text.find("Menu: From the last save"):]:
            raise RuntimeError("P4: the pilot got stuck after the reload")
        if text.count("SLICE_TRACE ev=claim a=Pass") != 1:
            raise RuntimeError("P4: the gate quest was claimed more than once")
    elif phase == "E0":
        text, seconds = run("E0-editor-pass", homes["editor"], "Pass", 420, pilot=True, editor=True)
        need("E0", text, ["SLICE_SAVED lvl=1 at=auto", "SLICE_LEVEL_START lvl=1 entry=new", "SLICE_TRACE ev=collect a=Sabre"])
    else:
        raise SystemExit(f"unknown phase {phase}")
    return text, seconds


if __name__ == "__main__":
    main()
