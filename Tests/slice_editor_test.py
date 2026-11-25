#!/usr/bin/env python3
"""Real X11/ImGui Blueprint authoring of the vertical slices' engine nodes, and the slices' own graphs in the editor.

Part 1 (VisualWorkshop's Move2D graph, a private copy): each node is added from the node library, saved, changed through
its real widget where it has one, saved, undone/redone and removed again, checking the saved graph every time:
  Gameplay.TransferWrite, Gameplay.TransferLoad, Gameplay.Steer   add / save / Undo
  LoadScene             its FadeOut field typed to 0.6 (the fade pins), Undo, Redo, Undo
  Gameplay.UIBindHUD    its Kind combo scrolled to Kind 8 "Qobiliyat" (ability cooldown), Undo, Redo, Undo
  Gameplay.AIConfigure  its Strike checkbox off (delegated attacks), Undo
Part 2 (both slice projects): ZAMIN_EDITOR_GRAPH_QA opens every slice graph in the real Blueprint window in turn; each
must report 0 compile diagnostics, the log no errors; the last graph's window is captured.
Run under xvfb-run with a 1600x1000x24 screen. Requires xdotool and ImageMagick. ZAMIN_BUILD_DIR chooses the build.
    xvfb-run -a -s "-screen 0 1600x1000x24" python3 Tests/slice_editor_test.py [--out DIR] [--part 1|2]
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("ZAMIN_BUILD_DIR", str(ROOT / "build"))).resolve()
SLICE_GRAPHS = {"LanternKeeper": ("Game", "Enemy", "Menu", "Hero"), "BeaconPass": ("Camera3D", "Game", "Enemy", "Menu", "Hero")}
# Screen positions of the editor's Blueprint window at 1600x1000 (as the other *_editor_test.py): the node library's search
# box and first result, the toolbar's Save / Undo / Redo, the selected-node panel's rows.
SEARCH, FIRST_RESULT, SAVE, UNDO, REDO = (150, 233), (155, 283), (473, 140), (531, 140), (587, 140)


def x(*args):
    subprocess.run(["xdotool", *map(str, args)], check=True, stdout=subprocess.DEVNULL)


def click(px, py):
    # Give GLFW a frame to observe the pointer before the button event on slow software renders.
    x("mousemove", px, py)
    time.sleep(1)
    x("mousedown", 1)
    time.sleep(2)
    x("mouseup", 1)
    time.sleep(2)


def wheel(px, py, notches):
    x("mousemove", px, py)
    time.sleep(.5)
    for _ in range(notches):
        x("click", "5")
        time.sleep(.4)
    time.sleep(1.5)


def shot(path):
    subprocess.run(["import", "-window", "root", str(path)], check=True)


def authoring(out):
    temporary = tempfile.TemporaryDirectory(prefix="zamin-slice-ui-")
    work = Path(temporary.name)
    project = work / "Project"
    shutil.copytree(ROOT / "Projects/VisualWorkshop", project, ignore=shutil.ignore_patterns(".zamin", "Build"))
    (work / "home").mkdir()
    graph_file = project / "Assets/Scripts/Move2D.zgraph"
    env = dict(os.environ, HOME=str(work / "home"), ZAMIN_EDITOR_VISUAL_GRAPH="Scripts/Move2D.zgraph", ZAMIN_EDITOR_SELECT="Player", ZAMIN_MAX_FRAMES="20000")
    log_path = work / "editor.log"
    log = log_path.open("w")
    app = subprocess.Popen([str(BUILD / "bin/ZaminEditor"), str(project / "VisualWorkshop.zproject")], env=env, stdout=log, stderr=subprocess.STDOUT)

    def read():
        return json.loads(graph_file.read_text())

    def saved(what, check):
        # Save until the file shows the expected state (only Save is retried: it is idempotent).
        for _ in range(4):
            click(*SAVE)
            time.sleep(1.2)
            if check(read()):
                return
            time.sleep(1)
        raise AssertionError(f"{what}: {read()['Nodes'][-1]}")

    def last(graph):
        return graph["Nodes"][-1]

    def add(kind):
        click(*SEARCH)
        x("keydown", "ctrl")
        x("key", "a")
        x("keyup", "ctrl")
        x("type", "--clearmodifiers", "--delay", "120", kind)
        time.sleep(1.5)
        click(*FIRST_RESULT)
        saved(f"{kind} added", lambda g: len(g["Nodes"]) == 12 and last(g)["Kind"] == kind)

    def remove(kind):
        click(*UNDO)
        saved(f"{kind} removed", lambda g: len(g["Nodes"]) == 11)

    def pin(kind, name, value, default, change, picture=None):
        change()
        saved(f"{kind}.{name}={value}", lambda g: len(g["Nodes"]) == 12 and last(g)["Inputs"].get(name) == value)
        if picture:
            shot(out / picture)
        click(*UNDO)
        saved(f"{kind}.{name} undone", lambda g: len(g["Nodes"]) == 12 and last(g)["Kind"] == kind and last(g)["Inputs"].get(name, default) == default)
        click(*REDO)
        saved(f"{kind}.{name} redone", lambda g: len(g["Nodes"]) == 12 and last(g)["Inputs"].get(name) == value)
        click(*UNDO)
        saved(f"{kind}.{name} undone again", lambda g: len(g["Nodes"]) == 12 and last(g)["Inputs"].get(name, default) == default)

    def fade_out():
        # LoadScene's FadeOut field (the selected-node panel's second row): typed through the real text input.
        click(1390, 319)
        time.sleep(1)
        x("keydown", "ctrl")
        time.sleep(.4)
        x("key", "a")
        time.sleep(.4)
        x("keyup", "ctrl")
        time.sleep(.5)
        x("type", "--delay", "200", "0.6")
        time.sleep(.6)
        x("key", "Return")
        time.sleep(2)

    def hud_kind_ability():
        # UIBindHUD's Kind combo: its popup shows 8 of the 10 kinds; scrolled to the end, Kind 8 is the second last row.
        click(1385, 395)
        time.sleep(1)
        wheel(1332, 520, 2)
        click(1332, 558)

    def ai_strike_off():
        # AIConfigure's Strike checkbox, below the panel's first page: three wheel notches bring it to y 625.
        wheel(1330, 600, 3)
        click(1307, 625)

    passed = []
    try:
        for _ in range(100):
            if app.poll() is not None:
                raise RuntimeError("Editor exited early")
            if "Scene opened:" in log_path.read_text():
                break
            time.sleep(.2)
        for _ in range(60):
            pixel = subprocess.check_output(["import", "-window", "root", "-crop", "1x1+450+230", "txt:-"], text=True).upper()
            if "#7D2F4C" in pixel or "#7D7D2F2F4C4C" in pixel:
                break
            time.sleep(.4)
        else:
            raise RuntimeError("Graph window did not become ready")
        time.sleep(1)
        for kind in ("Gameplay.TransferWrite", "Gameplay.TransferLoad", "LoadScene", "Gameplay.UIBindHUD", "Gameplay.AIConfigure", "Gameplay.Steer"):
            add(kind)
            if kind == "LoadScene":
                pin(kind, "FadeOut", 0.6, 0, fade_out, "slice-loadscene-fade.png")
            elif kind == "Gameplay.UIBindHUD":
                pin(kind, "Kind", 8, 0, hud_kind_ability, "slice-hud-kind-ability.png")
            elif kind == "Gameplay.AIConfigure":
                ai_strike_off()
                saved("AIConfigure.Strike=false", lambda g: len(g["Nodes"]) == 12 and last(g)["Inputs"].get("Strike") is False)
                shot(out / "slice-ai-strike.png")
                click(*UNDO)
                saved("AIConfigure.Strike undone", lambda g: len(g["Nodes"]) == 12 and last(g)["Inputs"].get("Strike", True) is True)
            remove(kind)
            passed.append(kind)
            print(f"UI: {kind} add/save{'/edit/Undo/Redo' if kind in ('LoadScene', 'Gameplay.UIBindHUD', 'Gameplay.AIConfigure') else ''}/Undo passed", flush=True)
        shot(out / "slice-authoring.png")
    except BaseException:
        shot(out / "slice-authoring-failure.png")
        raise
    finally:
        app.terminate()
        try:
            app.wait(timeout=10)
        except subprocess.TimeoutExpired:
            app.kill()
            app.wait()
        log.close()
        shutil.copyfile(log_path, out / "authoring-editor.log")
        temporary.cleanup()
    return passed


def slice_graphs(out):
    rows = []
    for game, graphs in SLICE_GRAPHS.items():
        temporary = tempfile.TemporaryDirectory(prefix="zamin-slice-graphs-")
        work = Path(temporary.name)
        project = work / game
        shutil.copytree(ROOT / "Projects" / game, project, ignore=shutil.ignore_patterns(".zamin", "Build"))
        (work / "home").mkdir()
        listing = ",".join(f"Scripts/{g}.zgraph" for g in graphs)
        frames = 10 + 20 * len(graphs) + 40
        png = out / f"slice-graphs-{game}.png"
        env = dict(os.environ, HOME=str(work / "home"), ZAMIN_EDITOR_GRAPH_QA=listing, ZAMIN_MAX_FRAMES=str(frames), ZAMIN_SCREENSHOT=str(png), ZAMIN_GL_DEBUG="1")
        log_path = out / f"graphs-{game}.log"
        begin = time.time()
        with log_path.open("w") as log:
            subprocess.run([str(BUILD / "bin/ZaminEditor"), str(project / f"{game}.zproject")], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=900,
                           check=True)
        text = log_path.read_text(errors="replace")
        errors = [line for line in text.splitlines() if re.search(r"\b(ERROR|CRITICAL)\b", line) or "GL_INVALID_" in line]
        if errors:
            raise AssertionError(f"{game}: errors in the editor log: {errors[:5]}")
        for g in graphs:
            m = re.search(rf"Blueprint graph QA: Scripts/{g}\.zgraph - (\d+) nodes, (\d+) links, (\d+) variables, (\d+) diagnostics", text)
            if not m:
                raise AssertionError(f"{game}: {g}.zgraph was not opened by the QA hook")
            if int(m.group(4)) != 0:
                raise AssertionError(f"{game}: {g}.zgraph has {m.group(4)} compile diagnostics")
            rows.append(f"{game}/{g} {m.group(1)}/{m.group(2)}")
        subprocess.run(["python3", str(ROOT / "Tests/png_check.py"), str(png)], check=True)
        print(f"UI: {game} graphs open without diagnostics ({', '.join(r.split('/', 1)[1] for r in rows if r.startswith(game))} node/link), "
              f"{time.time() - begin:.0f} s", flush=True)
        temporary.cleanup()
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", type=Path, default=BUILD / "Slices/Authoring")
    p.add_argument("--part", choices=("1", "2"), help="only this part")
    args = p.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {}
    if args.part in (None, "1"):
        report["Authoring"] = authoring(out)
    if args.part in (None, "2"):
        report["Graphs"] = slice_graphs(out)
    (out / "Report.json").write_text(json.dumps({"Passed": True, **report}, indent=2) + "\n")
    print("PASS slice editor test", flush=True)


if __name__ == "__main__":
    main()
