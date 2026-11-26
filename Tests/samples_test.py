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
          comes quickly, the results panel too. The games log "Menu: ..." for each panel they open.
  smoke - the three older samples (Platformer2D, Showcase3D, Adventure3D): they start, run, show a picture.
  records - with every demo (ZAMIN_DEMO_RECORDS=1: the bot's results count as records): the run must log a new record
          ("New record: ...") and leave it in the game's SaveData file (savedata.json under the run's HOME); a second,
          short run with the same HOME (no bot) must read it back - "Rekordlar: ..." with the same value - and show
          the main menu (with the record on it).
  gamepad - the six games driven by a simulated gamepad only (ZAMIN_PLAYER_INPUT pad= / stick=): A on "Play", the
          game's own controls bound in the project input map (stick, buttons, triggers), Start for the pause menu
          and back; the control hints on screen switch to the gamepad's buttons.
Every run: exit code 0, a log without ERROR / CRITICAL lines, the script module loaded, a last-frame screenshot that
is not one flat colour (Tests/png_check.py). The game clock is fixed (ZAMIN_CAPTURE_FPS, --capture-fps: 20 frames a
second of game time), so N frames are the same game time on any computer; the "fresh" random seeds are fixed too
(ZAMIN_RANDOM_SEED, --random-seed: a human game's new enemy order is the same every run); each run has a fresh HOME
(no saved records or settings). ZAMIN_BUILD_DIR=<dir> (or --build) selects the build. Exit code: 0 all passed, 1 a check failed, 2 the
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
            return False, "no such line"
        value = values[-1] if last else max(values)
        shown = int(value) if value == int(value) else value
        return value >= minimum, f"{shown} (at least {minimum})"
    return (description, check)


def count_at_least(description, pattern, minimum):
    def check(text):
        n = len(re.findall(pattern, text))
        return n >= minimum, f"{n} times (at least {minimum})"
    return (description, check)


def in_order(description, patterns):
    """The patterns appear in this order (each after the previous one's match)."""
    def check(text):
        at = 0
        for i, pattern in enumerate(patterns):
            m = re.compile(pattern).search(text, at)
            if not m:
                return False, f"step {i + 1} not found: {pattern}"
            at = m.end()
        return True, f"{len(patterns)} steps in order"
    return (description, check)


def absent(description, pattern):
    def check(text):
        m = re.search(pattern, text)
        return not m, (m.group(0)[:90] if m else "none")
    return (description, check)


def present(description, pattern):
    def check(text):
        m = re.search(pattern, text)
        return bool(m), (m.group(0)[:90] if m else "not found")
    return (description, check)


# The panel lines the games write (Projects/*/Source: ShowMenu, OpenPause, ClosePause, results).
MAIN_MENU = r"Menu: main menu"
PAUSE = r"Menu: pause\n"
PAUSE_CLOSED = r"Menu: pause closed"
RESULTS = r"Menu: results"


def menu_flow(started):
    return in_order("menus: main menu -> game -> pause -> resume -> pause -> main menu",
                    [MAIN_MENU, started, PAUSE, PAUSE_CLOSED, PAUSE, MAIN_MENU])


def keys(*items):
    """ZAMIN_PLAYER_INPUT text from (frame, key) pairs."""
    return ";".join(f"{frame}:{key}" for frame, key in items)


# Pause menu: "Resume" is selected; two Downs reach "Main menu" (FPS Arena: three - the sensitivity slider).
def pause_to_menu(start, downs=2):
    return [(start + 10 * (i + 1), "Down") for i in range(downs)] + [(start + 10 * (downs + 1), "Enter")]


class Record:
    """A record a demo run must save and the next run (same HOME) must read back: `new` finds the value in the first
    run's log ("New record: ..."; the last match counts), `loaded` (with {0}: that value) in the second run's log;
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
    return Case(game, "menus", frames, [present("the game started", started), menu_flow(started)],
                env={"ZAMIN_PLAYER_INPUT": keys(*script)})


def sokoban_menus():
    # Level 1 ("First step"): two steps right push the crate onto its target - the results panel
    # ("Next level" selected; two Downs: "Main menu").
    script = [(30, "Enter"), (90, "Escape"), (110, "Escape"), (130, "D"), (150, "D"),
              (230, "Down"), (240, "Down"), (250, "Enter")]
    flow = in_order("menus: main menu -> level -> pause -> resume -> results -> main menu",
                    [MAIN_MENU, r"Warehouse Keeper: level 1", PAUSE, PAUSE_CLOSED, r"Menu: results - level 1 solved, 2 steps", MAIN_MENU])
    levels = in_order("the level menu opens and closes", [MAIN_MENU, r"Menu: levels", MAIN_MENU])
    return [Case("Sokoban", "menus", 280, [present("the level started", r"Warehouse Keeper: level 1"), flow],
                 env={"ZAMIN_PLAYER_INPUT": keys(*script)}),
            Case("Sokoban", "levels", 120, [levels],
                 env={"ZAMIN_PLAYER_INPUT": keys((30, "Down"), (40, "Enter"), (90, "Escape"))},
                 note="Down + Enter in the main menu: levels, Esc: back"),
            # The bot behind the main menu solves level 1 and goes on (about 4 s a level); "Play" pressed after it
            # still starts the player's level 1, and two quick taps (the second while the first step still moves)
            # both count: the crate reaches its target in 2 steps.
            Case("Sokoban", "menuwait", 330, [
                present("bot orqada 1-darajani yechdi", r"Warehouse Keeper: level 2 behind the menu"),
                in_order("play after waiting: level 1 (not the bot's), two quick moves -> solved",
                         [MAIN_MENU, r"Warehouse Keeper: level 1", r"Menu: results - level 1 solved, 2 steps"]),
                absent("the bot's level did not start", r"Warehouse Keeper: level [2-9] \("),
            ], env={"ZAMIN_PLAYER_INPUT": keys((200, "Enter"), (260, "D"), (262, "D"))},
                note="wait 10 s in the main menu (the bot moves on to the next level), Enter, D D (2 frames apart)")]


def pad_case(game, started, script, checks=(), frames=None, downs=2, menu_at=None, note=""):
    """A game played with a simulated gamepad only: A on "Play" (frame 30), the game's own inputs (`script`), then
    Start: pause, Start: back, Start: pause, D-pad down x `downs` + A: the main menu. The control hints switch to
    the gamepad (the engine logs the device change once, and never back to the keyboard)."""
    at = menu_at or max(frame for frame, _ in script) + 20
    flow = [(30, "pad=A")] + list(script) + [(at, "pad=Start"), (at + 20, "pad=Start"), (at + 40, "pad=Start")]
    flow += [(at + 50 + 10 * i, "pad=DPadDown") for i in range(downs)] + [(at + 60 + 10 * downs, "pad=A")]
    return Case(game, "gamepad", frames or at + 60 + 10 * downs + 40, [
        present("the game started with the gamepad", started),
        in_order("gamepad: main menu -> game -> pause (Start) -> resume -> pause -> main menu (D-pad, A)",
                 [MAIN_MENU, started, PAUSE, PAUSE_CLOSED, PAUSE, MAIN_MENU]),
        present("the hints switched to the gamepad", r"Input device: gamepad"),
        absent("did not switch back to the keyboard (the gamepad played)", r"Input device: gamepad[\s\S]*Input device: keyboard"),
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
    # Neon Arena: left stick moves, the right stick sweeps the aim, RT fires, A dashes; leaving to the main menu
    # counts the score as a record - kills made with the gamepad.
    pad_case("TopDownShooter", r"Neon Arena: game started", [
        (36, "stick=LeftX,0.5"), (36, "stick=RightTrigger,1"), (110, "pad=A"), (150, "stick=LeftX,-0.5"), (260, "stick=LeftX,0"),
        *sweep_aim(40, 330, 6),
        (340, "stick=RightX,0"), (340, "stick=RightY,0"), (340, "stick=RightTrigger,0")],
        [at_least("fired with RT (the result on returning to the main menu)", r"Neon Arena: result \(returned to the main menu\) - .* (\d+) shots", 20)],
        note="left stick - move, right stick - aim, RT - fire, A - dash"),
    # Breakout: A launches the ball, the left stick moves the paddle; the bricks it breaks are the record.
    pad_case("Breakout", r"Breakout: game started", [
        (45, "pad=A"), (50, "stick=LeftX,-0.8"), (75, "stick=LeftX,0.8"), (110, "stick=LeftX,0"), (140, "pad=A")],
        [at_least("points with the gamepad (a record on returning to the main menu)", r"New record: score (\d+) .*returned to the main menu", 1)],
        note="A - launch the ball, left stick - paddle"),
    # FPS Arena: the left stick walks (forward: up), the right stick looks, L3 sprints, A jumps, B crouches.
    pad_case("FPSArena", r"FPS Arena: game started \(parkour\)", [
        (36, "stick=LeftY,-1"), (36, "padhold=LeftThumb"), (50, "stick=RightX,0.5"), (60, "stick=RightX,0"), (70, "pad=A"),
        (90, "pad=B"), (100, "padrelease=LeftThumb"), (110, "stick=LeftY,0"), (115, "pad=X")], downs=3,
        note="left stick - move, right stick - look, L3 - sprint, A - jump, B - crouch, X - reload"),
    # Warehouse Keeper: two D-pad steps right push level 1's crate onto its target - solved with the gamepad, a record.
    Case("Sokoban", "gamepad", 290, [
        present("the level started with the gamepad", r"Warehouse Keeper: level 1"),
        in_order("gamepad: main menu -> pause (Start) -> resume -> solved with the D-pad -> main menu",
                 [MAIN_MENU, r"Warehouse Keeper: level 1", PAUSE, PAUSE_CLOSED, r"Menu: results - level 1 solved, 2 steps", MAIN_MENU]),
        present("the solution was recorded", r"New record: level 1: 2 steps"),
        present("the hints switched to the gamepad", r"Input device: gamepad"),
        absent("did not switch back to the keyboard (the gamepad played)", r"Input device: gamepad[\s\S]*Input device: keyboard"),
    ], env={"ZAMIN_PLAYER_INPUT": keys((30, "pad=A"), (90, "pad=Start"), (110, "pad=Start"), (130, "pad=DPadRight"),
                                       (150, "pad=DPadRight"), (230, "pad=DPadDown"), (240, "pad=DPadDown"), (250, "pad=A"))},
        note="A - play, Start - pause, D-pad - move (push a crate)"),
    # Silk Road: D-pad / stick change lanes, A jumps, B slides (the pause comes before the first obstacle).
    pad_case("EndlessRunner", r"Silk Road: run started", [
        (36, "pad=DPadLeft"), (44, "stick=LeftX,1"), (48, "stick=LeftX,0"), (52, "pad=A"), (60, "pad=B")],
        [at_least("the run record (on returning to the main menu)", r"New record: score (\d+) .*returned to the main menu", 1)],
        note="D-pad / left stick - lane, A - jump, B - slide"),
    # Valley Race: RT accelerates (after the start lights), the left stick steers, X is the handbrake.
    pad_case("Racing", r"Valley Race: race started", [
        (40, "stick=RightTrigger,1"), (130, "stick=LeftX,0.3"), (150, "pad=X"), (160, "stick=LeftX,0"),
        (170, "stick=RightTrigger,0")],
        note="RT - throttle, left stick - steer, X - handbrake"),
]


CASES = [
    # ---- Neon Arena (top-down shooter)
    Case("TopDownShooter", "demo", 900, [
        at_least("score", r"Demo \(summary\): score (\d+)", 500),
        at_least("enemies killed", r"killed (\d+)", 8),
        count_at_least("the second wave started", r"Demo: wave 2 started", 1),
        at_least("best combo", r"best combo (\d+)", 3, last=True),
    ], env={"ZAMIN_DEMO": "1"},
        record=Record(r"New record: score (\d+)", r"Records: score {0}, wave \d+", ["Neon.BestScore", "Neon.BestWave"])),
    menu_case("TopDownShooter", r"Neon Arena: game started"),
    # ---- Zamin Breakout
    Case("Breakout", "demo", 1000, [
        at_least("score", r"Demo \(summary\): score (\d+)", 1000),
        at_least("bricks broken", r"bricks broken (\d+)", 50, last=True),
        count_at_least("level 1 cleared, the next one started", r"Demo: level 2 \(.*\) started", 1),
    ], env={"ZAMIN_DEMO": "1"},
        record=Record(r"New record: score (\d+)", r"Records: score {0}, level \d+", ["Breakout.BestScore", "Breakout.BestLevel"])),
    Case("Breakout", "level6", 500, [
        count_at_least("level 6 (the last) loaded", r"Demo: level 6 \(.*\) started - \d+ bricks", 1),
        at_least("bricks broken", r"bricks broken (\d+)", 5, last=True),
    ], env={"ZAMIN_DEMO": "1", "ZAMIN_START_LEVEL": "6"}, note="ZAMIN_START_LEVEL=6: the last level is built and played"),
    menu_case("Breakout", r"Breakout: game started"),
    # ---- FPS Arena
    Case("FPSArena", "demo", 700, [
        at_least("parkour laps", r"Demo summary: (\d+) laps", 1),
        count_at_least("checkpoints", r"Demo: point \d+", 6),
        at_least("targets hit", r"(\d+) targets hit", 4, last=True),
        at_least("points", r"score (\d+), accuracy", 100, last=True),
    ], env={"ZAMIN_DEMO": "1"},
        record=Record(r"New record: lap (\S+)", r"Records: best lap {0},", ["FPSArena.BestLap"])),
    menu_case("FPSArena", r"FPS Arena: game started \(parkour\)", downs=3),
    # ---- Warehouse Keeper (Sokoban)
    Case("Sokoban", "demo", 800, [
        count_at_least("levels solved", r"Demo: level \d+ solved", 3),
        count_at_least("the solver found a solution", r"Demo: level \d+ solution found", 3),
        count_at_least("undo", r"Demo: undone \(undo\)", 1),
    ], env={"ZAMIN_DEMO": "1"},
        record=Record(r"New record: (level \d+: \d+) steps", r"Records: \d+/10 levels solved - steps: .*{0}", ["Sokoban.Best.1", "Sokoban.Best.2"])),
    *sokoban_menus(),
    # ---- Silk Road (endless runner)
    Case("EndlessRunner", "demo", 700, [
        at_least("masofa (m)", r"Demo summary: \d+ runs, the last one (\d+) m", 400),
        at_least("coins", r"Demo summary: .*, (\d+) coins", 30),
        at_least("the speed rose (m/s; 11 at the start)", r"speed (\d+\.\d) m/s", 13.0),
        absent("the bot hit no obstacle", r"Demo: fell.*"),
    ], env={"ZAMIN_DEMO": "1"},
        record=Record(r"New record: score (\d+)", r"Records: score {0}, \d+ m", ["EndlessRunner.Best", "EndlessRunner.BestDistance"])),
    # Nobody steers: the runner hits the first obstacle - the results panel ("Restart" selected; Down: "Main menu").
    Case("EndlessRunner", "menus", 420, [
        present("the run started", r"Silk Road: run started"),
        in_order("menus: main menu -> run -> pause -> resume -> results -> main menu",
                 [MAIN_MENU, r"Silk Road: run started", PAUSE, PAUSE_CLOSED, RESULTS, MAIN_MENU]),
    ], env={"ZAMIN_PLAYER_INPUT": keys((30, "Enter"), (90, "Escape"), (110, "Escape"), (380, "Down"), (390, "Enter"))},
        note="nobody steers: the runner hits the first obstacle and the results window comes up"),
    # ---- Valley Race (racing)
    Case("Racing", "demo", 900, [
        count_at_least("a lap finished", r"Demo: lap 1 - time", 1),
        at_least("place (1-4)", r"place (\d)/4", 1),
    ], env={"ZAMIN_DEMO": "1"},
        record=Record(r"New record: lap (\S+)", r"Records: best lap {0}$", ["Racing.BestLap"])),
    menu_case("Racing", r"Valley Race: race started"),
    # ---- the older samples
    Case("Platformer2D", "smoke", 150, [present("the scene loaded", r"Scene load time")]),
    # Menu.zscene: Enter on "Play" - the loading screen loads Level1 in the background and switches; Esc: pause.
    Case("Platformer2D", "menus", 200, [
        in_order("menu -> loading screen -> level 1 -> pause",
                 [r"Game menu: opening '.*'", r"Loading screen: 'Scenes/Level1.zscene' ready", r"Player ready", r"APP: Paused"]),
    ], env={"ZAMIN_PLAYER_INPUT": keys((30, "Enter"), (150, "Escape"))}, scene="Scenes/Menu.zscene",
        note="from Menu.zscene: Play -> Loading -> Level1 (loaded on a background thread), Esc - pause"),
    Case("Showcase3D", "smoke", 120, [present("the scene loaded", r"Scene load time")]),
    Case("Adventure3D", "smoke", 150, [present("the hero is ready", r"Hero ready")]),
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
                "ZAMIN_CAPTURE_FPS": str(args.capture_fps), "ZAMIN_RANDOM_SEED": str(args.random_seed)})
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
    results = [(prefix + "exit code 0", code == 0, str(code))]
    error_lines = [line for line in text.splitlines() if ERROR_LINE.match(line + "\n")]
    results.append((prefix + "no errors in the log", bool(text) and not error_lines,
                    "no log was written" if not text else (f"{len(error_lines)} errors: {error_lines[0][:120]}" if error_lines else f"{len(text.splitlines())} lines")))
    if has_module(project, case.game):
        m = re.search(r"Script module loaded: (.*)", text)
        results.append((prefix + "the script module loaded", bool(m), m.group(1) if m else "no"))
    if os.path.exists(shot):
        ok, detail = check_picture(shot, args.resolution)
        results.append((prefix + "the picture of the last frame", ok, detail.replace(shot + ": ", "")))
    else:
        results.append((prefix + "the picture of the last frame", False, "no screenshot was saved"))
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
    results.append(("record: a new record was written", value is not None, f"{len(found)} times, the last {value}" if found else "no such line"))
    save = find_save_file(home)
    data = {}
    if save:
        try:
            with open(save, encoding="utf-8") as f:
                data = json.load(f).get("Data", {})
        except (OSError, ValueError) as e:
            results.append(("record: the SaveData file can be read", False, str(e)))
    missing = [k for k in record.keys if not isinstance(data.get(k), (int, float)) or data.get(k) <= 0]
    results.append(("record: in the SaveData file", bool(save) and not missing,
                    "no file" if not save else (f"missing: {', '.join(missing)}" if missing else
                                                  ", ".join(f"{k}={data[k]:g}" if isinstance(data[k], float) else f"{k}={data[k]}" for k in record.keys))))
    # The second run: the same HOME (the same save file), no bot - the main menu with the record on it.
    text2, code2, seconds2, log2, shot2 = run_game(case, args, runner, work, tag + "-record", home, record.frames, {})
    results += common_checks(case, args, text2, code2, shot2, prefix="record (2nd run): ")
    if value is not None:
        pattern = record.loaded.format(re.escape(value))
        m = re.search(pattern, text2, re.M)
        results.append(("record: read on the next run", bool(m), m.group(0)[:90] if m else f"not found: {pattern}"))
    results.append(("record: the main menu opened", bool(re.search(MAIN_MENU, text2)), "yes" if re.search(MAIN_MENU, text2) else "no"))
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
    parser.add_argument("--random-seed", type=int, default=1,
                        help="ZAMIN_RANDOM_SEED: the games' fresh random seeds (a new enemy order every human game) are the same every run")
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
            print(f"{case.name:28} {case.frames:4} frames  {env}{('  - ' + case.note) if case.note else ''}")
            for description, _ in case.checks:
                print(f"{'':30}- {description}")
            if case.record:
                print(f"{'':30}- record: written to SaveData ({', '.join(case.record.keys)}) and read on the next run")
        return 0
    if not cases:
        print("No case was selected (--game / --kind)", file=sys.stderr)
        return 2
    args.player = args.player or os.path.join(args.build, "bin", "ZaminPlayer")
    if args.exported:
        args.exported = os.path.abspath(args.exported)
        missing = sorted({c.game for c in cases if not os.access(game_command(c, args)[0], os.X_OK)})
        if missing:
            print(f"Exported game not found ({args.exported}): {', '.join(missing)}", file=sys.stderr)
            return 2
    elif not os.access(args.player, os.X_OK):
        print(f"ZaminPlayer not found: {args.player} (first: cmake --build build)", file=sys.stderr)
        return 2
    runner = []
    if not os.environ.get("DISPLAY"):
        if not shutil.which("xvfb-run"):
            print("No display and xvfb-run was not found - the player window cannot open", file=sys.stderr)
            return 2
        runner = ["xvfb-run", "-a", "-s", "-screen 0 1920x1080x24"]

    work = tempfile.mkdtemp(prefix="zamin-samples-")
    if args.out:
        os.makedirs(args.out, exist_ok=True)
        work = os.path.abspath(args.out)
    print(f"Sample games test: {len(cases)} cases, {args.resolution}, game time 1/{args.capture_fps} s per frame ({work})")
    reports = []
    for case in cases:
        print(f"[{case.name}] {int(case.frames * args.frames_scale)} frames ...", flush=True)
        report = run_case(case, args, runner, work)
        reports.append(report)
        for check in report["checks"]:
            print(f"  {'OK  ' if check['passed'] else 'FAIL'} {check['check']}: {check['detail']}")
        print(f"  => {'passed' if report['passed'] else 'FAILED'} ({report['seconds']} s)", flush=True)
    failed = [r for r in reports if not r["passed"]]
    with open(os.path.join(work, "report.json"), "w", encoding="utf-8") as f:
        json.dump({"cases": reports, "failed": len(failed)}, f, ensure_ascii=False, indent=2)
    total = sum(r["seconds"] for r in reports)
    print(f"\nResult: {len(reports) - len(failed)}/{len(reports)} cases passed, {total:.0f} s"
          + (f"; failed: {', '.join(r['case'] for r in failed)}" if failed else ""))
    if failed:
        print(f"Logs and pictures: {work}")
    elif not args.out:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
