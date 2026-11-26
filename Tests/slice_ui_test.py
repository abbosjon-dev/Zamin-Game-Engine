#!/usr/bin/env python3
"""The vertical slices' UI input / scale gate in the real Player (P5 item 3; docs/VerticalSlices.md, docs/UI.md).

    python3 Tests/slice_ui_test.py [--build build] [--game LanternKeeper,BeaconPass] [--out DIR] [--phases U1,U2,U3,UW]
                                   [--fuzz-frames 2000] [--seed 7]

Every Player run audits its UI (ZAMIN_UI_AUDIT, Scene::AuditUI + CheckUIAudit - UIAudit.h) each time the scene or its
open gameplay UI panel changes, at the six view sizes of the matrix (800x600, 1024x768, 1280x720, 1600x900, 1920x1080,
2560x1080; laid out without drawing, measured with the loaded fonts), and the run fails on any rule broken: usable
widgets 8 px inside the view, no overlapping siblings, texts that fit, 12/14 px text at least, the selection inside the
open panel and every widget of it reachable by navigation, nothing off-anchor in a 21:9 view's outer tenth.
  U1  keyboard tour: main menu -> settings (music slider, the UI scale slider to 120 %, back: SLICE_SETTINGS ui=1.20)
      -> credits -> new game -> the HUD -> pause -> inventory -> back -> back; screens audited at 120 % from the settings on
  U2  a fresh process with the same HOME (the 120 % setting re-applied at start), then the gamepad tour of the same tree
      (D-pad, A, B, Start; the UI scale back to 80 %: ui=0.80; "Input device: gamepad"), and a mouse run that
      clicks two menu buttons where the audit says they are
  U3  seeded fuzz: --fuzz-frames of random Up/Down/Left/Right/Submit/Back/Pause/Inventory on the keyboard, then on the
      gamepad (a quit from the menu starts the next segment in a fresh process): no error, at most 8 panels open, the
      game's time back at 1 whenever no panel is open, every audit clean
  UW  the title menu in a real 2560x1080 window (ultrawide): audited and screenshotted
Software GL (llvmpipe), fixed 30 fps capture; private project copies; one HOME per device. Report: <out>/Report.json.
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
sys.path.insert(0, str(ROOT / "Tests"))
from slice_workflow_test import FORBIDDEN, private_copy  # noqa: E402

SIZES = "800x600,1024x768,1280x720,1600x900,1920x1080,2560x1080"
AUDIT = re.compile(r"UI_AUDIT scene=(\S*) frame=(\d+) (\{.*\})")
# Per game: the window of the tours (the 3D slice smaller and at the Low quality level: llvmpipe), the fuzz window.
GAMES = {
    "LanternKeeper": dict(tour="1280x720", fuzz="960x540", quality=None, level="Orchards"),
    "BeaconPass": dict(tour="960x540", fuzz="640x360", quality="Low", level="Pass"),
}


def keyboard_tour(game):
    """Main -> Settings (music Right x3, UI scale Right x2, Down x3 -> Back) -> Credits (the main panel focuses
    New game again: Down x2 with no Continue) -> back -> New game -> the level: pause, inventory, back, back."""
    menu = ("40:Down;46:Enter;70:Right;76:Right;82:Right;96:Down;100:Down;106:Right;112:Right;126:Down;130:Down;134:Down;140:Enter;"
            "164:Down;168:Down;176:Enter;210:Escape;240:Up;246:Up;252:Up;260:Enter;")
    if game == "LanternKeeper":
        return menu + "380:Escape;400:Down;406:Enter;440:Escape;470:Escape", 520
    return menu + "430:Escape;450:Down;456:Enter;490:Escape;520:Escape", 570


def gamepad_tour(game):
    """The same tree on the pad: D-pad, A (submit), B (back), Start (pause); the UI scale 120 % -> 80 % (Left x4); the
    run of U1 left a game to continue, so Credits is three down from New game."""
    menu = ("40:pad=DPadDown;46:pad=A;70:pad=DPadDown;76:pad=DPadDown;82:pad=DPadLeft;88:pad=DPadLeft;94:pad=DPadLeft;100:pad=DPadLeft;"
            "116:pad=DPadDown;120:pad=DPadDown;124:pad=DPadDown;130:pad=A;154:pad=DPadDown;158:pad=DPadDown;162:pad=DPadDown;170:pad=A;200:pad=B;"
            "230:pad=DPadUp;236:pad=DPadUp;"
            "242:pad=DPadUp;250:pad=A;")
    if game == "LanternKeeper":
        return menu + "370:pad=Start;390:pad=DPadDown;396:pad=A;430:pad=B;460:pad=B", 510
    return menu + "420:pad=Start;440:pad=DPadDown;446:pad=A;480:pad=B;510:pad=B", 560


class Lcg:
    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFFFFFFFFFF

    def next(self):
        self.state = (self.state * 6364136223846793005 + 1442695040888963407) & 0xFFFFFFFFFFFFFFFF
        return self.state >> 33


def fuzz_tape(seed, frames, pad):
    keys = (["pad=DPadUp", "pad=DPadDown", "pad=DPadLeft", "pad=DPadRight", "pad=A", "pad=B", "pad=Start", "pad=Back"] if pad else
            ["Up", "Down", "Left", "Right", "Enter", "Escape", "P", "Tab"])
    rng, items, frame = Lcg(seed), [], 30
    while frame < frames:
        # navigation more often than the other actions; a submit / back every few presses
        r = rng.next() % 100
        key = keys[r % 4] if r < 55 else keys[4] if r < 72 else keys[5] if r < 84 else keys[6] if r < 92 else keys[7]
        items.append((frame, key))
        frame += 3 + rng.next() % 6
    return items


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--build", type=Path, default=ROOT / "build")
    p.add_argument("--game", default="LanternKeeper,BeaconPass")
    p.add_argument("--out", type=Path)
    p.add_argument("--phases", default="U1,U2,U3,UW")
    p.add_argument("--fuzz-frames", type=int, default=2000)
    p.add_argument("--seed", type=int, default=7)
    args = p.parse_args()
    build = args.build.resolve()
    out = (args.out or build / "SliceUI").resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    phases = [x for x in args.phases.split(",") if x]
    base_env = dict(os.environ, LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="2", ZAMIN_GL_DEBUG="1", ZAMIN_CAPTURE_FPS="30")
    base_env.pop("ZAMIN_DEMO", None)
    report = {"Games": {}, "Sizes": SIZES.split(",")}
    failures = []

    for game in [g for g in args.game.split(",") if g]:
        cfg = GAMES[game]
        gout = out / game
        gout.mkdir()
        homes = {"keys": gout / "home-keyboard", "pad": gout / "home-gamepad", "fuzz": gout / "home-fuzz"}
        rows = report["Games"].setdefault(game, {})

        def run(name, home, frames, tape="", resolution=None, audit="top", elements=False, screen=None, sizes=SIZES):
            home.mkdir(parents=True, exist_ok=True)
            manifest = private_copy(gout / f"work-{name}", game, "Menu", False, 0, False)
            log, png = gout / f"{name}.log", gout / f"{name}.png"
            env = dict(base_env, HOME=str(home), ZAMIN_MAX_FRAMES=str(frames), ZAMIN_SCREENSHOT=str(png), ZAMIN_PLAYER_INPUT=tape,
                       ZAMIN_UI_AUDIT=audit, ZAMIN_UI_AUDIT_SIZES=sizes)
            if elements:
                env["ZAMIN_UI_AUDIT_ELEMENTS"] = "1"
            cmd = [str(build / "bin/ZaminPlayer"), str(manifest), "--no-splash", "--resolution", resolution or cfg["tour"]]
            if cfg["quality"]:
                cmd += ["--quality", cfg["quality"]]
            runner = [] if os.environ.get("DISPLAY") else ["xvfb-run", "-a", "-s", f"-screen 0 {screen or '1600x1000'}x24 -nolisten tcp"]
            begin = time.time()
            with log.open("w") as stream:
                subprocess.run(runner + cmd, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=2400, check=True)
            text = log.read_text(errors="replace")
            for marker in FORBIDDEN:
                if marker in text:
                    raise RuntimeError(f"{game} {name}: forbidden '{marker}' in {log}")
            audits = []
            for m in AUDIT.finditer(text):
                doc = json.loads(m.group(3))
                doc["Scene"], doc["Frame"] = m.group(1), int(m.group(2))
                audits.append(doc)
            return text, audits, time.time() - begin, png

        def check_audits(name, audits, minimum):
            screens = {(a["Scene"], a["Top"]) for a in audits}
            issues = [(a["Scene"], a["Top"], a["Viewport"], i) for a in audits for i in a["Issues"]]
            for scene, top, view, issue in issues[:12]:
                print(f"    {game} {name}: {scene}/{top or 'HUD'} {view[0]}x{view[1]}: {issue['Rule']} {issue['Detail']}")
            if issues:
                raise RuntimeError(f"{game} {name}: {len(issues)} UI audit issue(s)")
            if len(screens) < minimum:
                raise RuntimeError(f"{game} {name}: {len(screens)} screens audited (at least {minimum} expected): {sorted(screens)}")
            fonts = [a["MinFontPx"] for a in audits if a.get("Texts")]
            return {"Audits": len(audits), "Screens": sorted(f"{s}/{t or 'HUD'}" for s, t in screens), "MinFontPx": round(min(fonts), 2) if fonts else 0,
                    "UIScales": sorted({a["UIScale"] for a in audits})}

        def need(name, text, markers):
            last = -1
            for m in markers:
                at = text.find(m, last + 1)
                if at < 0:
                    raise RuntimeError(f"{game} {name}: missing '{m}' (after the previous marker)")
                last = at

        try:
            if "U1" in phases:
                tape, frames = keyboard_tour(game)
                text, audits, seconds, png = run("U1-keyboard", homes["keys"], frames, tape)
                need("U1", text, ["Menu: Main continue=0", "Menu: Settings", "SLICE_SETTINGS music=", "ui=1.20", "Menu: Main", "Menu: Credits",
                                  "Menu: New game", "SLICE_LEVEL_START lvl=1 entry=new"])
                row = check_audits("U1", audits, 6)
                if 1.2 not in row["UIScales"] or 1.0 not in row["UIScales"]:
                    raise RuntimeError(f"{game} U1: the UI scale setting was not applied ({row['UIScales']})")
                row["Seconds"] = round(seconds, 1)
                rows["U1"] = row
                shutil.copy(png, out / f"{game}-U1.png")
                print(f"  {game} U1: {row['Audits']} audit, {len(row['Screens'])} screens, smallest text {row['MinFontPx']} px, {seconds:.0f} s")
            if "U2" in phases:
                # The same HOME: the 120 % setting comes back at start; then the gamepad tour sets 80 %.
                tape, frames = gamepad_tour(game)
                text, audits, seconds, png = run("U2-gamepad", homes["keys"], frames, tape)
                need("U2", text, ["Menu: Main continue=", "Input device: gamepad", "Menu: Settings", "ui=0.80", "Menu: Credits",
                                  "Menu: New game", "SLICE_LEVEL_START lvl=1 entry=new"])
                first = next((a for a in audits if a["Scene"] == "Menu"), None)
                if not first or abs(first["UIScale"] - 1.2) > 1e-3:
                    raise RuntimeError(f"{game} U2: the saved UI scale (120 %) was not re-applied in a fresh process ({first and first['UIScale']})")
                row = check_audits("U2", audits, 6)
                if 0.8 not in row["UIScales"]:
                    raise RuntimeError(f"{game} U2: the 80 % UI scale was not applied ({row['UIScales']})")
                row["Seconds"] = round(seconds, 1)
                rows["U2-gamepad"] = row
                print(f"  {game} U2 gamepad: {row['Audits']} audit, {len(row['Screens'])} screens, scales {row['UIScales']}, {seconds:.0f} s")
                # Mouse: where the audit says the Settings button and the settings' Back are (window px, y down).
                text, audits, _, _ = run("U2-probe", homes["pad"], 60, audit="40", elements=True, sizes=cfg["tour"])
                size = [int(v) for v in cfg["tour"].split("x")]
                view = next(a for a in audits if a["Viewport"] == size)

                def centre(audit, tag):
                    e = next(x for x in audit["Elements"] if x["Tag"] == tag)
                    return int((e["Rect"][0] + e["Rect"][2]) / 2), int(size[1] - (e["Rect"][1] + e["Rect"][3]) / 2)
                sx, sy = centre(view, "Settings")
                tape = f"30:mouse={sx},{sy};34:click;"
                text, audits, seconds, _ = run("U2-mouse-open", homes["pad"], 90, tape, audit="80", elements=True, sizes=cfg["tour"])
                need("U2 mouse", text, ["Menu: Settings"])
                settings = next(a for a in audits if a["Viewport"] == size)
                bx, by = centre(settings, "Back")
                tape += f"90:mouse={bx},{by};94:click"
                text, audits, seconds, _ = run("U2-mouse", homes["pad"], 140, tape)
                need("U2 mouse", text, ["Menu: Settings", "SLICE_SETTINGS music=", "Menu: Main"])
                rows["U2-mouse"] = {"Clicks": [[sx, sy], [bx, by]], "Seconds": round(seconds, 1)}
                print(f"  {game} U2 mouse: Settings ({sx},{sy}) and Back ({bx},{by}) clicked")
            if "U3" in phases:
                for device, pad in (("keyboard", False), ("gamepad", True)):
                    tape = fuzz_tape(args.seed + (1000 if pad else 0) + (0 if game == "LanternKeeper" else 7), args.fuzz_frames, pad)
                    done, segments, every, breaks, depth = 0, 0, [], 0, 0
                    seconds = 0.0
                    while done < args.fuzz_frames - 30 and segments < 12:
                        segments += 1
                        rest = [(f - done, k) for f, k in tape if f >= done + 30]
                        frames = args.fuzz_frames - done
                        marks = ",".join(str(f) for f in range(50, frames, 50))
                        text, audits, s, _ = run(f"U3-{device}-{segments}", homes["fuzz"], frames, ";".join(f"{f}:{k}" for f, k in rest),
                                                 resolution=cfg["fuzz"], audit="top," + marks)
                        seconds += s
                        every += audits
                        for a in audits:
                            depth = max(depth, a["Depth"])
                            if a["Depth"] > 8 or (a["Depth"] == 0 and (a["Paused"] or abs(a["TimeScale"] - 1.0) > 1e-3)):
                                breaks += 1
                        last = max((a["Frame"] for a in audits), default=0)
                        if "The game asked to quit" in text:
                            done += max(last, 30) # quit from the menu: the rest in a fresh process
                        else:
                            done = args.fuzz_frames
                    issues = sum(len(a["Issues"]) for a in every)
                    for a in every:
                        for i in a["Issues"][:3]:
                            print(f"    {game} U3 {device}: {a['Scene']}/{a['Top'] or 'HUD'} frame {a['Frame']} {a['Viewport']}: {i['Rule']} {i['Detail']}")
                    row = {"Frames": args.fuzz_frames, "Segments": segments, "Audits": len(every), "MaxDepth": depth, "InvariantBreaks": breaks,
                           "Issues": issues, "Screens": sorted({f"{a['Scene']}/{a['Top'] or 'HUD'}" for a in every}), "Seconds": round(seconds, 1)}
                    rows[f"U3-{device}"] = row
                    print(f"  {game} U3 {device}: {args.fuzz_frames} frames, {segments} segments, {len(every)} audits, deepest {depth} panels, "
                          f"{breaks} invariant breaks, {issues} issues, {seconds:.0f} s")
                    if breaks or issues:
                        raise RuntimeError(f"{game} U3 {device}: {breaks} invariant break(s), {issues} audit issue(s)")
            if "UW" in phases:
                text, audits, seconds, png = run("UW-ultrawide", gout / "home-wide", 70, resolution="2560x1080", audit="60", screen="2600x1500")
                wide = [a for a in audits if a["Viewport"] == [2560, 1080]]
                if not wide or wide[0]["Issues"]:
                    raise RuntimeError(f"{game} UW: the 2560x1080 audit is missing or has issues")
                subprocess.run(["python3", str(ROOT / "Tests/png_check.py"), str(png)], check=True)
                shutil.copy(png, out / f"{game}-ultrawide.png")
                rows["UW"] = {"Audits": len(audits), "Seconds": round(seconds, 1)}
                print(f"  {game} UW: 2560x1080 menu, {len(audits)} audits, {seconds:.0f} s")
        except Exception as error:  # noqa: BLE001
            failures.append(f"{game}: {error}")
            print(f"FAIL {game}: {error}")
    (out / "Report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
    print(f"Report: {out / 'Report.json'}")
    if failures:
        print("\n".join(failures))
        return 1
    print("UI gate: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
