#!/usr/bin/env python3
"""Headless tests of the sample games (ctest label "samples"): every sample runs in ZaminPlayer under xvfb (without a
display) and its log says whether it worked.

    Tests/samples_test.py                          every case of every sample (about 15 minutes on 4 cores, llvmpipe)
    Tests/samples_test.py --game Breakout          one game's cases (--game more than once: several)
    Tests/samples_test.py --kind demo              only the bot demos (demo | menus | smoke)
    Tests/samples_test.py --list                   the cases and what each checks
    Tests/samples_test.py --out build/SampleTests  the logs, screenshots and report.json kept there
    Tests/samples_test.py --frames-scale 2         every run twice as long

Cases:
  demo  - the six M24 games with ZAMIN_DEMO=1: the bot plays N frames and the "Demo: ..." lines of the log must show
          progress (score > 0, waves / levels / laps / checkpoints / distance reached, undo used ...).
  menus - the same games without the bot: scripted keys (ZAMIN_PLAYER_INPUT) go through the main menu (Enter on
          "Play"), the pause menu (Esc), back to the game, the pause menu again and its "Main menu" button; where it
          comes quickly, the results panel too. The games log "Menyu: ..." for each panel they open.
  smoke - the three older samples (Platformer2D, Showcase3D, Adventure3D): they start, run, show a picture.
Every run: exit code 0, a log without ERROR / CRITICAL lines, the script module loaded, a last-frame screenshot that
is not one flat colour (Tests/png_check.py). The game clock is fixed (ZAMIN_CAPTURE_FPS, --capture-fps: 20 frames a
second of game time), so N frames are the same game time on any computer; each run has a fresh HOME (no saved records
or settings). ZAMIN_BUILD_DIR=<dir> (or --build) selects the build. Exit code: 0 all passed, 1 a check failed, 2 the
player is missing or there is no display and no xvfb-run.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True


# ------------------------------------------------------------------------------------------- checks
# A check: (description, function(log text) -> (ok, detail)).

def numbers(text, pattern):
    """Every match of `pattern` (its first group) as numbers."""
    return [float(m.group(1)) for m in re.finditer(pattern, text)]


def at_least(description, pattern, minimum, last=False):
    """The largest (or the last) number the pattern finds is at least `minimum`."""
    def check(text):
        values = numbers(text, pattern)
        if not values:
            return False, "qator topilmadi"
        value = values[-1] if last else max(values)
        shown = int(value) if value == int(value) else value
        return value >= minimum, f"{shown} (kamida {minimum})"
    return (description, check)


def count_at_least(description, pattern, minimum):
    def check(text):
        n = len(re.findall(pattern, text))
        return n >= minimum, f"{n} marta (kamida {minimum})"
    return (description, check)


def in_order(description, patterns):
    """The patterns appear in this order (each after the previous one's match)."""
    def check(text):
        at = 0
        for i, pattern in enumerate(patterns):
            m = re.compile(pattern).search(text, at)
            if not m:
                return False, f"{i + 1}-qadam topilmadi: {pattern}"
            at = m.end()
        return True, f"{len(patterns)} qadam tartib bilan"
    return (description, check)


def absent(description, pattern):
    def check(text):
        m = re.search(pattern, text)
        return not m, (m.group(0)[:90] if m else "yo'q")
    return (description, check)


def present(description, pattern):
    def check(text):
        m = re.search(pattern, text)
        return bool(m), (m.group(0)[:90] if m else "topilmadi")
    return (description, check)


# The panel lines the games write (Projects/*/Source: ShowMenu, OpenPause, ClosePause, results).
MAIN_MENU = r"Menyu: bosh menyu"
PAUSE = r"Menyu: pauza\n"
PAUSE_CLOSED = r"Menyu: pauza yopildi"
RESULTS = r"Menyu: natijalar"


def menu_flow(started):
    return in_order("menyular: bosh menyu -> o'yin -> pauza -> davom -> pauza -> bosh menyu",
                    [MAIN_MENU, started, PAUSE, PAUSE_CLOSED, PAUSE, MAIN_MENU])


def keys(*items):
    """ZAMIN_PLAYER_INPUT text from (frame, key) pairs."""
    return ";".join(f"{frame}:{key}" for frame, key in items)


# Pause menu: "Davom etish" is selected; two Downs reach "Bosh menyu" (FPS Arena: three - the sensitivity slider).
def pause_to_menu(start, downs=2):
    return [(start + 10 * (i + 1), "Down") for i in range(downs)] + [(start + 10 * (downs + 1), "Enter")]


class Case:
    def __init__(self, game, kind, frames, checks, env=None, scene=None, note=""):
        self.game, self.kind, self.frames, self.checks = game, kind, frames, checks
        self.env = env or {}
        self.scene = scene
        self.note = note

    @property
    def name(self):
        return f"{self.game}/{self.kind}"


def menu_case(game, started, frames=230, downs=2):
    # Enter on "Play" (selected when the main menu opens), Esc: pause, Esc: back, Esc: pause, Down x N, Enter: menu.
    script = [(30, "Enter"), (90, "Escape"), (110, "Escape"), (140, "Escape")] + pause_to_menu(140, downs)
    return Case(game, "menus", frames, [present("o'yin boshlandi", started), menu_flow(started)],
                env={"ZAMIN_PLAYER_INPUT": keys(*script)})


def sokoban_menus():
    # Level 1 ("Birinchi qadam"): two steps right push the crate onto its target - the results panel
    # ("Keyingi daraja" selected; two Downs: "Bosh menyu").
    script = [(30, "Enter"), (90, "Escape"), (110, "Escape"), (130, "D"), (150, "D"),
              (230, "Down"), (240, "Down"), (250, "Enter")]
    flow = in_order("menyular: bosh menyu -> daraja -> pauza -> davom -> natijalar -> bosh menyu",
                    [MAIN_MENU, r"Omborchi: 1-daraja", PAUSE, PAUSE_CLOSED, r"Menyu: natijalar - 1-daraja yechildi, 2 qadam", MAIN_MENU])
    levels = in_order("darajalar menyusi ochiladi va yopiladi", [MAIN_MENU, r"Menyu: darajalar", MAIN_MENU])
    return [Case("Sokoban", "menus", 280, [present("daraja boshlandi", r"Omborchi: 1-daraja"), flow],
                 env={"ZAMIN_PLAYER_INPUT": keys(*script)}),
            Case("Sokoban", "levels", 120, [levels],
                 env={"ZAMIN_PLAYER_INPUT": keys((30, "Down"), (40, "Enter"), (90, "Escape"))},
                 note="bosh menyuda Down + Enter: darajalar, Esc: orqaga")]


CASES = [
    # ---- Neon Maydon (top-down shooter)
    Case("TopDownShooter", "demo", 900, [
        at_least("hisob", r"Demo \(yakuni\): hisob (\d+)", 500),
        at_least("o'ldirilgan dushmanlar", r"o'ldirilgan (\d+)", 8),
        count_at_least("ikkinchi to'lqin boshlandi", r"Demo: 2-to'lqin boshlandi", 1),
        at_least("eng katta kombo", r"eng katta kombo (\d+)", 3, last=True),
    ], env={"ZAMIN_DEMO": "1"}),
    menu_case("TopDownShooter", r"Neon Maydon: o'yin boshlandi"),
    # ---- Zamin Breakout
    Case("Breakout", "demo", 1000, [
        at_least("hisob", r"Demo \(yakuni\): hisob (\d+)", 1000),
        at_least("singan g'ishtlar", r"singan g'ishtlar (\d+)", 50, last=True),
        count_at_least("1-daraja tugadi, keyingisi boshlandi", r"Demo: 2-daraja \(.*\) boshlandi", 1),
    ], env={"ZAMIN_DEMO": "1"}),
    Case("Breakout", "level6", 500, [
        count_at_least("6-daraja (oxirgisi) yuklandi", r"Demo: 6-daraja \(.*\) boshlandi - \d+ ta g'isht", 1),
        at_least("singan g'ishtlar", r"singan g'ishtlar (\d+)", 5, last=True),
    ], env={"ZAMIN_DEMO": "1", "ZAMIN_START_LEVEL": "6"}, note="ZAMIN_START_LEVEL=6: oxirgi daraja quriladi va o'ynaladi"),
    menu_case("Breakout", r"Breakout: o'yin boshlandi"),
    # ---- FPS Arena
    Case("FPSArena", "demo", 700, [
        at_least("parkur aylanalari", r"Demo yakuni: (\d+) ta aylana", 1),
        count_at_least("nazorat nuqtalari", r"Demo: \d+-nuqta", 6),
        at_least("urilgan nishonlar", r"(\d+) ta nishon urildi", 4, last=True),
        at_least("ochko", r"ochko (\d+), aniqlik", 100, last=True),
    ], env={"ZAMIN_DEMO": "1"}),
    menu_case("FPSArena", r"FPS Arena: o'yin boshlandi \(parkur\)", downs=3),
    # ---- Omborchi (Sokoban)
    Case("Sokoban", "demo", 800, [
        count_at_least("yechilgan darajalar", r"Demo: \d+-daraja yechildi", 3),
        count_at_least("yechuvchi yechim topdi", r"Demo: \d+-daraja yechimi topildi", 3),
        count_at_least("orqaga qaytarish (undo)", r"Demo: orqaga qaytarildi \(undo\)", 1),
    ], env={"ZAMIN_DEMO": "1"}),
    *sokoban_menus(),
    # ---- Ipak Yo'li (endless runner)
    Case("EndlessRunner", "demo", 700, [
        at_least("masofa (m)", r"Demo yakuni: \d+ ta yugurish, oxirgisi (\d+) m", 400),
        at_least("tangalar", r"Demo yakuni: .*, (\d+) tanga", 30),
        at_least("tezlik oshdi (m/s; boshida 11)", r"tezlik (\d+\.\d) m/s", 13.0),
        absent("bot to'siqqa urilmadi", r"Demo: yiqildi.*"),
    ], env={"ZAMIN_DEMO": "1"}),
    # Nobody steers: the runner hits the first obstacle - the results panel ("Qaytadan" selected; Down: "Bosh menyu").
    Case("EndlessRunner", "menus", 420, [
        present("yugurish boshlandi", r"Ipak Yo'li: yugurish boshlandi"),
        in_order("menyular: bosh menyu -> yugurish -> pauza -> davom -> natijalar -> bosh menyu",
                 [MAIN_MENU, r"Ipak Yo'li: yugurish boshlandi", PAUSE, PAUSE_CLOSED, RESULTS, MAIN_MENU]),
    ], env={"ZAMIN_PLAYER_INPUT": keys((30, "Enter"), (90, "Escape"), (110, "Escape"), (380, "Down"), (390, "Enter"))},
        note="hech kim boshqarmaydi: birinchi to'siqqa urilib, natijalar oynasi chiqadi"),
    # ---- Vodiy Poygasi (racing)
    Case("Racing", "demo", 900, [
        count_at_least("aylana tugadi", r"Demo: 1-aylana - vaqt", 1),
        at_least("o'rin (1-4)", r"o'rin (\d)/4", 1),
    ], env={"ZAMIN_DEMO": "1"}),
    menu_case("Racing", r"Vodiy Poygasi: poyga boshlandi"),
    # ---- the older samples
    Case("Platformer2D", "smoke", 150, [present("sahna yuklandi", r"Sahna yuklash vaqti")]),
    # Menu.zscene: Enter on "O'ynash" - the loading screen loads Level1 in the background and switches; Esc: pause.
    Case("Platformer2D", "menus", 200, [
        in_order("menyu -> yuklanish ekrani -> 1-daraja -> pauza",
                 [r"O'yin menyusi: '.*' ochilmoqda", r"Yuklanish ekrani: 'Scenes/Level1.zscene' tayyor", r"O'yinchi tayyor", r"APP: Pauza"]),
    ], env={"ZAMIN_PLAYER_INPUT": keys((30, "Enter"), (150, "Escape"))}, scene="Scenes/Menu.zscene",
        note="Menu.zscene dan: O'ynash -> Loading -> Level1 (fon oqimida yuklash), Esc - pauza"),
    Case("Showcase3D", "smoke", 120, [present("sahna yuklandi", r"Sahna yuklash vaqti")]),
    Case("Adventure3D", "smoke", 150, [present("qahramon tayyor", r"Qahramon tayyor")]),
]

ERROR_LINE = re.compile(r"^\[[^]]*\] (ERROR|CRITICAL) ", re.M)


def has_module(project_dir, game):
    try:
        with open(os.path.join(project_dir, game + ".zproject"), encoding="utf-8") as f:
            return bool(json.load(f).get("Project", {}).get("ScriptModulePath"))
    except (OSError, ValueError):
        return False


def check_picture(png, size):
    """Tests/png_check.py: the right size and not one flat colour."""
    result = subprocess.run([sys.executable, os.path.join(ROOT, "Tests", "png_check.py"), png, "--size", size,
                             "--min-colors", "48", "--min-stddev", "5"], capture_output=True, text=True)
    lines = (result.stdout + result.stderr).strip().splitlines()
    return result.returncode == 0, " ".join(line.strip() for line in lines)


def run_case(case, args, runner, work):
    project = os.path.join(ROOT, "Projects", case.game)
    tag = case.name.replace("/", "-")
    log = os.path.join(work, tag + ".log")
    shot = os.path.join(work, tag + ".png")
    home = tempfile.mkdtemp(prefix="home-", dir=work)
    frames = max(10, int(case.frames * args.frames_scale))
    env = dict(os.environ)
    env.update({"HOME": home, "ZAMIN_MAX_FRAMES": str(frames), "ZAMIN_SCREENSHOT": shot,
                "ZAMIN_CAPTURE_FPS": str(args.capture_fps)})
    env.pop("ZAMIN_DEMO", None)
    env.pop("ZAMIN_PLAYER_INPUT", None)
    env.update(case.env)
    command = runner + [args.player, project, "--windowed", "--resolution", args.resolution, "--no-splash", "--log-file", log]
    if case.scene:
        command += ["--scene", case.scene]
    start = time.time()
    try:
        proc = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout)
        code, output = proc.returncode, proc.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as e:
        code, output = "timeout", (e.stdout or b"").decode("utf-8", "replace")
    seconds = time.time() - start
    with open(os.path.join(work, tag + "-stdout.log"), "w", encoding="utf-8") as f:
        f.write(output)
    text = ""
    if os.path.exists(log):
        with open(log, encoding="utf-8", errors="replace") as f:
            text = f.read()

    results = [("chiqish kodi 0", code == 0, str(code))]
    errors = ERROR_LINE.findall(text)
    error_lines = [line for line in text.splitlines() if ERROR_LINE.match(line + "\n")]
    results.append(("logda xato yo'q", bool(text) and not errors,
                    "log yozilmadi" if not text else (f"{len(error_lines)} ta xato: {error_lines[0][:120]}" if error_lines else f"{len(text.splitlines())} qator")))
    if has_module(project, case.game):
        m = re.search(r"Skript moduli yuklandi: (.*)", text)
        results.append(("skript moduli yuklandi", bool(m), m.group(1) if m else "yo'q"))
    if os.path.exists(shot):
        ok, detail = check_picture(shot, args.resolution)
        results.append(("oxirgi kadr rasmi", ok, detail.replace(shot + ": ", "")))
    else:
        results.append(("oxirgi kadr rasmi", False, "skrinshot saqlanmadi"))
    for description, check in case.checks:
        ok, detail = check(text)
        results.append((description, ok, detail))
    shutil.rmtree(home, ignore_errors=True)
    return {"case": case.name, "game": case.game, "kind": case.kind, "frames": frames, "seconds": round(seconds, 1),
            "passed": all(ok for _, ok, _ in results), "log": log, "screenshot": shot,
            "checks": [{"check": d, "passed": ok, "detail": detail} for d, ok, detail in results]}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", action="append", default=[], help="only this sample (more than once: several)")
    parser.add_argument("--kind", action="append", default=[], help="only these cases: demo, menus, levels, level6, smoke")
    parser.add_argument("--list", action="store_true", help="the cases and their checks")
    parser.add_argument("--build", default=os.environ.get("ZAMIN_BUILD_DIR", os.path.join(ROOT, "build")))
    parser.add_argument("--player", help="ZaminPlayer (default: <build>/bin/ZaminPlayer)")
    parser.add_argument("--out", help="keep the logs, screenshots and report.json in this folder")
    parser.add_argument("--frames-scale", type=float, default=1.0, help="every run's frames times this")
    parser.add_argument("--capture-fps", type=int, default=20, help="game frames per second of game time (ZAMIN_CAPTURE_FPS)")
    parser.add_argument("--resolution", default="960x540")
    parser.add_argument("--timeout", type=int, default=900, help="seconds a run may take")
    args = parser.parse_args()

    games = {g.lower() for g in args.game}
    kinds = {k.lower() for k in args.kind}
    cases = [c for c in CASES if (not games or c.game.lower() in games) and (not kinds or c.kind in kinds)]
    if args.list:
        for case in cases:
            env = " ".join(f"{k}={v}" for k, v in case.env.items() if k != "ZAMIN_PLAYER_INPUT")
            print(f"{case.name:28} {case.frames:4} kadr  {env}{('  - ' + case.note) if case.note else ''}")
            for description, _ in case.checks:
                print(f"{'':30}- {description}")
        return 0
    if not cases:
        print("Hech qanday holat tanlanmadi (--game / --kind)", file=sys.stderr)
        return 2
    args.player = args.player or os.path.join(args.build, "bin", "ZaminPlayer")
    if not os.access(args.player, os.X_OK):
        print(f"ZaminPlayer topilmadi: {args.player} (avval: cmake --build build)", file=sys.stderr)
        return 2
    runner = []
    if not os.environ.get("DISPLAY"):
        if not shutil.which("xvfb-run"):
            print("Displey yo'q va xvfb-run topilmadi - player oynasi ochilmaydi", file=sys.stderr)
            return 2
        runner = ["xvfb-run", "-a", "-s", "-screen 0 1920x1080x24"]

    work = tempfile.mkdtemp(prefix="zamin-samples-")
    if args.out:
        os.makedirs(args.out, exist_ok=True)
        work = os.path.abspath(args.out)
    print(f"Namuna o'yinlar testi: {len(cases)} ta holat, {args.resolution}, o'yin vaqti 1/{args.capture_fps} s / kadr ({work})")
    reports = []
    for case in cases:
        print(f"[{case.name}] {int(case.frames * args.frames_scale)} kadr ...", flush=True)
        report = run_case(case, args, runner, work)
        reports.append(report)
        for check in report["checks"]:
            print(f"  {'OK  ' if check['passed'] else 'XATO'} {check['check']}: {check['detail']}")
        print(f"  => {'o`tdi' if report['passed'] else 'O`TMADI'} ({report['seconds']} s)".replace("`", "'"), flush=True)
    failed = [r for r in reports if not r["passed"]]
    with open(os.path.join(work, "report.json"), "w", encoding="utf-8") as f:
        json.dump({"cases": reports, "failed": len(failed)}, f, ensure_ascii=False, indent=2)
    total = sum(r["seconds"] for r in reports)
    print(f"\nNatija: {len(reports) - len(failed)}/{len(reports)} holat o'tdi, {total:.0f} s"
          + (f"; o'tmadi: {', '.join(r['case'] for r in failed)}" if failed else ""))
    if failed:
        print(f"Loglar va rasmlar: {work}")
    elif not args.out:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
