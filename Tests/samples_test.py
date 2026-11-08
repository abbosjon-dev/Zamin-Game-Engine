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
  records - with every demo (ZAMIN_DEMO_RECORDS=1: the bot's results count as records): the run must log a new record
          ("Yangi rekord: ...") and leave it in the game's SaveData file (savedata.json under the run's HOME); a second,
          short run with the same HOME (no bot) must read it back - "Rekordlar: ..." with the same value - and show
          the main menu (with the record on it).
  gamepad - the six games driven by a simulated gamepad only (ZAMIN_PLAYER_INPUT pad= / stick=): A on "Play", the
          game's own controls bound in the project input map (stick, buttons, triggers), Start for the pause menu
          and back; the control hints on screen switch to the gamepad's buttons.
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


class Record:
    """A record a demo run must save and the next run (same HOME) must read back: `new` finds the value in the first
    run's log ("Yangi rekord: ..."; the last match counts), `loaded` (with {0}: that value) in the second run's log;
    `keys`: SaveData keys that must be in the save file after the first run."""
    def __init__(self, new, loaded, keys, frames=40):
        self.new, self.loaded, self.keys, self.frames = new, loaded, keys, frames


class Case:
    def __init__(self, game, kind, frames, checks, env=None, scene=None, note="", record=None):
        self.game, self.kind, self.frames, self.checks = game, kind, frames, checks
        self.env = env or {}
        self.scene = scene
        self.note = note
        self.record = record
        if record:
            self.env = dict(self.env, ZAMIN_DEMO_RECORDS="1")

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
                 note="bosh menyuda Down + Enter: darajalar, Esc: orqaga"),
            # The bot behind the main menu solves level 1 and goes on (about 4 s a level); "Play" pressed after it
            # still starts the player's level 1, and two quick taps (the second while the first step still moves)
            # both count: the crate reaches its target in 2 steps.
            Case("Sokoban", "menuwait", 330, [
                present("bot orqada 1-darajani yechdi", r"Omborchi: menyu orqasida 2-daraja"),
                in_order("kutib o'ynash: 1-daraja (botniki emas), tez ikki qadam -> yechildi",
                         [MAIN_MENU, r"Omborchi: 1-daraja", r"Menyu: natijalar - 1-daraja yechildi, 2 qadam"]),
                absent("bot darajasi boshlanmadi", r"Omborchi: [2-9]-daraja \("),
            ], env={"ZAMIN_PLAYER_INPUT": keys((200, "Enter"), (260, "D"), (262, "D"))},
                note="bosh menyuda 10 s kutish (bot keyingi darajaga o'tadi), Enter, D D (2 kadr oralig'ida)")]


def pad_case(game, started, script, checks=(), frames=None, downs=2, menu_at=None, note=""):
    """A game played with a simulated gamepad only: A on "Play" (frame 30), the game's own inputs (`script`), then
    Start: pause, Start: back, Start: pause, D-pad down x `downs` + A: the main menu. The control hints switch to
    the gamepad (the engine logs the device change once, and never back to the keyboard)."""
    at = menu_at or max(frame for frame, _ in script) + 20
    flow = [(30, "pad=A")] + list(script) + [(at, "pad=Start"), (at + 20, "pad=Start"), (at + 40, "pad=Start")]
    flow += [(at + 50 + 10 * i, "pad=DPadDown") for i in range(downs)] + [(at + 60 + 10 * downs, "pad=A")]
    return Case(game, "gamepad", frames or at + 60 + 10 * downs + 40, [
        present("o'yin gamepad bilan boshlandi", started),
        in_order("gamepad: bosh menyu -> o'yin -> pauza (Start) -> davom -> pauza -> bosh menyu (D-pad, A)",
                 [MAIN_MENU, started, PAUSE, PAUSE_CLOSED, PAUSE, MAIN_MENU]),
        present("ishoralar gamepad'ga o'tdi", r"Kiritish qurilmasi: gamepad"),
        absent("klaviaturaga qaytmadi (gamepad o'ynadi)", r"Kiritish qurilmasi: gamepad[\s\S]*Kiritish qurilmasi: klaviatura"),
        *checks,
    ], env={"ZAMIN_PLAYER_INPUT": keys(*flow)}, note=note)


def sweep_aim(start, end, step):
    """The right stick turned around in eight directions, one every `step` frames (twin-stick aiming)."""
    directions = [(1, 0), (0.7, -0.7), (0, -1), (-0.7, -0.7), (-1, 0), (-0.7, 0.7), (0, 1), (0.7, 0.7)]
    items = []
    for i, frame in enumerate(range(start, end, step)):
        x, y = directions[i % len(directions)]
        items += [(frame, f"stick=RightX,{x}"), (frame, f"stick=RightY,{y}")]
    return items


GAMEPAD_CASES = [
    # Neon Maydon: left stick moves, the right stick sweeps the aim, RT fires, A dashes; leaving to the main menu
    # counts the score as a record - kills made with the gamepad.
    pad_case("TopDownShooter", r"Neon Maydon: o'yin boshlandi", [
        (36, "stick=LeftX,0.5"), (36, "stick=RightTrigger,1"), (110, "pad=A"), (150, "stick=LeftX,-0.5"), (260, "stick=LeftX,0"),
        *sweep_aim(40, 330, 6),
        (340, "stick=RightX,0"), (340, "stick=RightY,0"), (340, "stick=RightTrigger,0")],
        [at_least("RT bilan otildi (bosh menyuga qaytilgandagi natija)", r"Neon Maydon: natija \(bosh menyuga qaytildi\) - .* (\d+) ta o'q", 20)],
        note="chap stik - yurish, o'ng stik - nishon, RT - otish, A - siltanish"),
    # Breakout: A launches the ball, the left stick moves the paddle; the bricks it breaks are the record.
    pad_case("Breakout", r"Breakout: o'yin boshlandi", [
        (45, "pad=A"), (50, "stick=LeftX,-0.8"), (75, "stick=LeftX,0.8"), (110, "stick=LeftX,0"), (140, "pad=A")],
        [at_least("gamepad bilan ochko (bosh menyuga qaytilganda rekord)", r"Yangi rekord: hisob (\d+) .*bosh menyuga qaytildi", 1)],
        note="A - to'pni otish, chap stik - raketka"),
    # FPS Arena: the left stick walks (forward: up), the right stick looks, L3 sprints, A jumps, B crouches.
    pad_case("FPSArena", r"FPS Arena: o'yin boshlandi \(parkur\)", [
        (36, "stick=LeftY,-1"), (36, "padhold=LeftThumb"), (50, "stick=RightX,0.5"), (60, "stick=RightX,0"), (70, "pad=A"),
        (90, "pad=B"), (100, "padrelease=LeftThumb"), (110, "stick=LeftY,0"), (115, "pad=X")], downs=3,
        note="chap stik - yurish, o'ng stik - qarash, L3 - yugurish, A - sakrash, B - cho'kkalash, X - qayta o'qlash"),
    # Omborchi: two D-pad steps right push level 1's crate onto its target - solved with the gamepad, a record.
    Case("Sokoban", "gamepad", 290, [
        present("daraja gamepad bilan boshlandi", r"Omborchi: 1-daraja"),
        in_order("gamepad: bosh menyu -> pauza (Start) -> davom -> D-pad bilan yechildi -> bosh menyu",
                 [MAIN_MENU, r"Omborchi: 1-daraja", PAUSE, PAUSE_CLOSED, r"Menyu: natijalar - 1-daraja yechildi, 2 qadam", MAIN_MENU]),
        present("yechim rekordga yozildi", r"Yangi rekord: 1-daraja 2 qadam"),
        present("ishoralar gamepad'ga o'tdi", r"Kiritish qurilmasi: gamepad"),
        absent("klaviaturaga qaytmadi (gamepad o'ynadi)", r"Kiritish qurilmasi: gamepad[\s\S]*Kiritish qurilmasi: klaviatura"),
    ], env={"ZAMIN_PLAYER_INPUT": keys((30, "pad=A"), (90, "pad=Start"), (110, "pad=Start"), (130, "pad=DPadRight"),
                                       (150, "pad=DPadRight"), (230, "pad=DPadDown"), (240, "pad=DPadDown"), (250, "pad=A"))},
        note="A - o'ynash, Start - pauza, D-pad - yurish (qutini itarish)"),
    # Ipak Yo'li: D-pad / stick change lanes, A jumps, B slides (the pause comes before the first obstacle).
    pad_case("EndlessRunner", r"Ipak Yo'li: yugurish boshlandi", [
        (36, "pad=DPadLeft"), (44, "stick=LeftX,1"), (48, "stick=LeftX,0"), (52, "pad=A"), (60, "pad=B")],
        [at_least("yugurish rekordi (bosh menyuga qaytilganda)", r"Yangi rekord: ochko (\d+) .*bosh menyuga qaytildi", 1)],
        note="D-pad / chap stik - yo'lak, A - sakrash, B - sirpanish"),
    # Vodiy Poygasi: RT accelerates (after the start lights), the left stick steers, X is the handbrake.
    pad_case("Racing", r"Vodiy Poygasi: poyga boshlandi", [
        (40, "stick=RightTrigger,1"), (130, "stick=LeftX,0.3"), (150, "pad=X"), (160, "stick=LeftX,0"),
        (170, "stick=RightTrigger,0")],
        note="RT - gaz, chap stik - rul, X - qo'l tormozi"),
]


CASES = [
    # ---- Neon Maydon (top-down shooter)
    Case("TopDownShooter", "demo", 900, [
        at_least("hisob", r"Demo \(yakuni\): hisob (\d+)", 500),
        at_least("o'ldirilgan dushmanlar", r"o'ldirilgan (\d+)", 8),
        count_at_least("ikkinchi to'lqin boshlandi", r"Demo: 2-to'lqin boshlandi", 1),
        at_least("eng katta kombo", r"eng katta kombo (\d+)", 3, last=True),
    ], env={"ZAMIN_DEMO": "1"},
        record=Record(r"Yangi rekord: hisob (\d+)", r"Rekordlar: hisob {0}, \d+-to'lqin", ["Neon.BestScore", "Neon.BestWave"])),
    menu_case("TopDownShooter", r"Neon Maydon: o'yin boshlandi"),
    # ---- Zamin Breakout
    Case("Breakout", "demo", 1000, [
        at_least("hisob", r"Demo \(yakuni\): hisob (\d+)", 1000),
        at_least("singan g'ishtlar", r"singan g'ishtlar (\d+)", 50, last=True),
        count_at_least("1-daraja tugadi, keyingisi boshlandi", r"Demo: 2-daraja \(.*\) boshlandi", 1),
    ], env={"ZAMIN_DEMO": "1"},
        record=Record(r"Yangi rekord: hisob (\d+)", r"Rekordlar: hisob {0}, \d+-daraja", ["Breakout.BestScore", "Breakout.BestLevel"])),
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
    ], env={"ZAMIN_DEMO": "1"},
        record=Record(r"Yangi rekord: aylana (\S+)", r"Rekordlar: eng yaxshi aylana {0},", ["FPSArena.BestLap"])),
    menu_case("FPSArena", r"FPS Arena: o'yin boshlandi \(parkur\)", downs=3),
    # ---- Omborchi (Sokoban)
    Case("Sokoban", "demo", 800, [
        count_at_least("yechilgan darajalar", r"Demo: \d+-daraja yechildi", 3),
        count_at_least("yechuvchi yechim topdi", r"Demo: \d+-daraja yechimi topildi", 3),
        count_at_least("orqaga qaytarish (undo)", r"Demo: orqaga qaytarildi \(undo\)", 1),
    ], env={"ZAMIN_DEMO": "1"},
        record=Record(r"Yangi rekord: (\d+-daraja \d+) qadam", r"Rekordlar: \d+/10 daraja yechilgan - qadamlar: .*{0}", ["Sokoban.Best.1", "Sokoban.Best.2"])),
    *sokoban_menus(),
    # ---- Ipak Yo'li (endless runner)
    Case("EndlessRunner", "demo", 700, [
        at_least("masofa (m)", r"Demo yakuni: \d+ ta yugurish, oxirgisi (\d+) m", 400),
        at_least("tangalar", r"Demo yakuni: .*, (\d+) tanga", 30),
        at_least("tezlik oshdi (m/s; boshida 11)", r"tezlik (\d+\.\d) m/s", 13.0),
        absent("bot to'siqqa urilmadi", r"Demo: yiqildi.*"),
    ], env={"ZAMIN_DEMO": "1"},
        record=Record(r"Yangi rekord: ochko (\d+)", r"Rekordlar: ochko {0}, \d+ m", ["EndlessRunner.Best", "EndlessRunner.BestDistance"])),
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
    ], env={"ZAMIN_DEMO": "1"},
        record=Record(r"Yangi rekord: aylana (\S+)", r"Rekordlar: eng yaxshi aylana {0}$", ["Racing.BestLap"])),
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
    *GAMEPAD_CASES,
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


def game_command(case, args):
    """The program that runs the game: ZaminPlayer with the project folder, or (--exported) the exported game's
    launcher - <exported>/<Game>/<Name>.sh, its files next to it, the way a player gets it."""
    project = os.path.join(ROOT, "Projects", case.game)
    if args.exported:
        name = project_name(project, case.game)
        # Tests/samples_export.py leaves each export in <out>/<Game>/export (export_test.sh's work folder layout).
        for folder in (os.path.join(args.exported, case.game, "export"), os.path.join(args.exported, case.game)):
            if os.path.isfile(os.path.join(folder, name + ".sh")):
                return [os.path.join(folder, name + ".sh")]
        return [os.path.join(args.exported, case.game, name + ".sh")]
    return [args.player, project]


def project_name(project_dir, game):
    try:
        with open(os.path.join(project_dir, game + ".zproject"), encoding="utf-8") as f:
            return json.load(f).get("Project", {}).get("Name") or game
    except (OSError, ValueError):
        return game


def run_game(case, args, runner, work, tag, home, frames, env_extra, scene=None):
    """One run of the game: its log text, exit code, seconds and screenshot path."""
    log = os.path.join(work, tag + ".log")
    shot = os.path.join(work, tag + ".png")
    env = dict(os.environ)
    env.update({"HOME": home, "ZAMIN_MAX_FRAMES": str(frames), "ZAMIN_SCREENSHOT": shot,
                "ZAMIN_CAPTURE_FPS": str(args.capture_fps)})
    for key in ("ZAMIN_DEMO", "ZAMIN_PLAYER_INPUT", "ZAMIN_DEMO_RECORDS", "ZAMIN_START_LEVEL"):
        env.pop(key, None)
    env.update(env_extra)
    command = runner + game_command(case, args) + ["--windowed", "--resolution", args.resolution, "--no-splash", "--log-file", log]
    if scene:
        command += ["--scene", scene]
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
    return text, code, seconds, log, shot


def common_checks(case, args, text, code, shot, prefix=""):
    """Every run: exit code 0, a log without errors, the script module loaded, a last frame that shows something."""
    project = os.path.join(ROOT, "Projects", case.game)
    results = [(prefix + "chiqish kodi 0", code == 0, str(code))]
    error_lines = [line for line in text.splitlines() if ERROR_LINE.match(line + "\n")]
    results.append((prefix + "logda xato yo'q", bool(text) and not error_lines,
                    "log yozilmadi" if not text else (f"{len(error_lines)} ta xato: {error_lines[0][:120]}" if error_lines else f"{len(text.splitlines())} qator")))
    if has_module(project, case.game):
        m = re.search(r"Skript moduli yuklandi: (.*)", text)
        results.append((prefix + "skript moduli yuklandi", bool(m), m.group(1) if m else "yo'q"))
    if os.path.exists(shot):
        ok, detail = check_picture(shot, args.resolution)
        results.append((prefix + "oxirgi kadr rasmi", ok, detail.replace(shot + ": ", "")))
    else:
        results.append((prefix + "oxirgi kadr rasmi", False, "skrinshot saqlanmadi"))
    return results


def find_save_file(home):
    """The game's SaveData file under a run's HOME (<data home>/<company>/<product>/savedata.json)."""
    for folder, _, files in os.walk(os.path.join(home, ".local", "share")):
        if "savedata.json" in files:
            return os.path.join(folder, "savedata.json")
    return None


def record_checks(case, args, runner, work, tag, home, text):
    """Records: the first run saved one (log + save file), a second run with the same HOME reads it back."""
    record = case.record
    results = []
    found = re.findall(record.new, text)
    value = found[-1] if found else None
    results.append(("rekord: yangi rekord yozildi", value is not None, f"{len(found)} marta, oxirgisi {value}" if found else "qator topilmadi"))
    save = find_save_file(home)
    data = {}
    if save:
        try:
            with open(save, encoding="utf-8") as f:
                data = json.load(f).get("Data", {})
        except (OSError, ValueError) as e:
            results.append(("rekord: SaveData fayli o'qiladi", False, str(e)))
    missing = [k for k in record.keys if not isinstance(data.get(k), (int, float)) or data.get(k) <= 0]
    results.append(("rekord: SaveData faylida", bool(save) and not missing,
                    "fayl yo'q" if not save else (f"yo'q: {', '.join(missing)}" if missing else
                                                  ", ".join(f"{k}={data[k]:g}" if isinstance(data[k], float) else f"{k}={data[k]}" for k in record.keys))))
    # The second run: the same HOME (the same save file), no bot - the main menu with the record on it.
    text2, code2, seconds2, log2, shot2 = run_game(case, args, runner, work, tag + "-record", home, record.frames, {})
    results += common_checks(case, args, text2, code2, shot2, prefix="rekord (2-ishga tushirish): ")
    if value is not None:
        pattern = record.loaded.format(re.escape(value))
        m = re.search(pattern, text2, re.M)
        results.append(("rekord: keyingi ishga tushirishda o'qildi", bool(m), m.group(0)[:90] if m else f"topilmadi: {pattern}"))
    results.append(("rekord: bosh menyu ochildi", bool(re.search(MAIN_MENU, text2)), "ha" if re.search(MAIN_MENU, text2) else "yo'q"))
    return results, seconds2


def run_case(case, args, runner, work):
    tag = case.name.replace("/", "-")
    home = tempfile.mkdtemp(prefix="home-", dir=work)
    frames = max(10, int(case.frames * args.frames_scale))
    text, code, seconds, log, shot = run_game(case, args, runner, work, tag, home, frames, case.env, case.scene)
    results = common_checks(case, args, text, code, shot)
    for description, check in case.checks:
        ok, detail = check(text)
        results.append((description, ok, detail))
    if case.record and not args.no_records:
        more, extra_seconds = record_checks(case, args, runner, work, tag, home, text)
        results += more
        seconds += extra_seconds
    shutil.rmtree(home, ignore_errors=True)
    return {"case": case.name, "game": case.game, "kind": case.kind, "frames": frames, "seconds": round(seconds, 1),
            "passed": all(ok for _, ok, _ in results), "log": log, "screenshot": shot,
            "checks": [{"check": d, "passed": ok, "detail": detail} for d, ok, detail in results]}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", action="append", default=[], help="only this sample (more than once: several)")
    parser.add_argument("--kind", action="append", default=[], help="only these cases: demo, menus, levels, menuwait, level6, smoke, gamepad")
    parser.add_argument("--list", action="store_true", help="the cases and their checks")
    parser.add_argument("--build", default=os.environ.get("ZAMIN_BUILD_DIR", os.path.join(ROOT, "build")))
    parser.add_argument("--player", help="ZaminPlayer (default: <build>/bin/ZaminPlayer)")
    parser.add_argument("--out", help="keep the logs, screenshots and report.json in this folder")
    parser.add_argument("--frames-scale", type=float, default=1.0, help="every run's frames times this")
    parser.add_argument("--capture-fps", type=int, default=20, help="game frames per second of game time (ZAMIN_CAPTURE_FPS)")
    parser.add_argument("--resolution", default="960x540")
    parser.add_argument("--timeout", type=int, default=900, help="seconds a run may take")
    parser.add_argument("--no-records", action="store_true", help="skip the record runs after the demos")
    parser.add_argument("--exported", help="run the exported games in this folder (<dir>/<Game>/[export/]<Name>.sh) instead of "
                                           "ZaminPlayer with the projects (Tests/samples_export.py)")
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
            if case.record:
                print(f"{'':30}- rekord: SaveData'ga yoziladi ({', '.join(case.record.keys)}) va keyingi ishga tushirishda o'qiladi")
        return 0
    if not cases:
        print("Hech qanday holat tanlanmadi (--game / --kind)", file=sys.stderr)
        return 2
    args.player = args.player or os.path.join(args.build, "bin", "ZaminPlayer")
    if args.exported:
        args.exported = os.path.abspath(args.exported)
        missing = sorted({c.game for c in cases if not os.access(game_command(c, args)[0], os.X_OK)})
        if missing:
            print(f"Eksport qilingan o'yin topilmadi ({args.exported}): {', '.join(missing)}", file=sys.stderr)
            return 2
    elif not os.access(args.player, os.X_OK):
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
