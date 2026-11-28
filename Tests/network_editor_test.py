#!/usr/bin/env python3
"""The networking GUI test (docs/Networking.md, docs/Testing.md): the real editor under xvfb on a private copy of
Projects/NetArena; widgets are found through the layout file the editor writes (ZAMIN_EDITOR_VISUAL_LAYOUT), never at
fixed coordinates.

Phase 1 - graph RPCs: in the Visual Script window the node library's "Network RPC" entry for the graph's Horn RPC adds
a Call Server RPC node; the Network RPCs section creates an RPC (Wave), gives it a parameter and the graph is saved:
the file has the RPC, its handler (Event.Rpc) and the call node, and it validates in the engine's format 7.
Phase 2 - Play with clients: the Network panel turns on "Play: the editor hosts" with 3 players (2 client processes,
without windows here) and the 100 ms / 5% loss link preset; Play: the panel lists both clients connected (RTT measured),
the graph spawned a player for each (the host's too); Stop ends the session and the client processes.

    xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' python3 Tests/network_editor_test.py
Requires xdotool and ImageMagick (import). ZAMIN_BUILD_DIR chooses the build (default build). Screenshots and the
editor logs go to <build>/NetworkEditorTest.
"""
import json
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

repo = Path(__file__).resolve().parent.parent
build = Path(os.environ.get("ZAMIN_BUILD_DIR", str(repo / "build"))).resolve()
out = build / "NetworkEditorTest"
out.mkdir(parents=True, exist_ok=True)
temporary = tempfile.TemporaryDirectory(prefix="zamin-network-ui-")
work = Path(temporary.name)
project = work / "NetArena"
shutil.copytree(repo / "Projects/NetArena", project, ignore=shutil.ignore_patterns(".zamin", "Build"))
(work / "home").mkdir()
layout = work / "layout.json"
graph_file = project / "Assets/Scripts/ArenaRules.zgraph"


def x(*args):
    subprocess.run(["xdotool", *map(str, args)], check=True, stdout=subprocess.DEVNULL)


def click(px, py):
    # A frame for GLFW to see the pointer before the button event (slow software rendering).
    x("mousemove", px, py)
    time.sleep(.6)
    x("mousedown", 1)
    time.sleep(.5)
    x("mouseup", 1)
    time.sleep(1.2)


def data():
    for _ in range(30):
        try:
            return json.loads(layout.read_text())
        except (OSError, ValueError):
            time.sleep(.3)
    raise RuntimeError("no layout file")


def find(key, scroll_region=None):
    """The centre of a recorded widget; in a scrolling region (the graph's properties column) scrolled into view."""
    for _ in range(40):
        d = data()
        match = [v["Rect"] for k, v in d["Items"].items() if k == key or k.endswith("/" + key)]
        region = d["Regions"].get(scroll_region) if scroll_region else None
        if match and region:
            r = match[0]
            cx, cy = int((region[0] + region[2]) / 2), int((region[1] + region[3]) / 2)
            if r[3] > region[3] - 4:
                x("mousemove", cx, cy)
                time.sleep(.3)
                x("click", 5)
                time.sleep(.8)
                continue
            if r[1] < region[1] + 4:
                x("mousemove", cx, cy)
                time.sleep(.3)
                x("click", 4)
                time.sleep(.8)
                continue
        if match:
            r = match[0]
            return int((r[0] + r[2]) / 2), int((r[1] + r[3]) / 2)
        time.sleep(.4)
    raise RuntimeError("widget not found: " + key)


def press(key, region=None):
    click(*find(key, region))


def typed(key, text, region=None):
    press(key, region)
    x("keydown", "ctrl")
    time.sleep(.3)
    x("key", "a")
    time.sleep(.3)
    x("keyup", "ctrl")
    time.sleep(.3)
    x("type", "--delay", "150", text)
    time.sleep(.5)


def value(key):
    return data().get("Values", {}).get(key)


def wait_value(key, test, seconds=60):
    until = time.time() + seconds
    while time.time() < until:
        v = value(key)
        if v is not None and test(v):
            return v
        time.sleep(.5)
    raise RuntimeError(f"{key} never satisfied the check (last: {value(key)})")


def shot(name):
    subprocess.run(["import", "-window", "root", str(out / name)], check=True)


def start_editor(env_extra, log_name):
    env = dict(os.environ, HOME=str(work / "home"), ZAMIN_EDITOR_VISUAL_LAYOUT=str(layout), ZAMIN_MAX_FRAMES="100000", **env_extra)
    log = (work / log_name).open("w")
    app = subprocess.Popen([str(build / "bin/ZaminEditor"), str(project / "NetArena.zproject")], env=env, stdout=log, stderr=subprocess.STDOUT)
    for _ in range(300):
        if app.poll() is not None:
            raise RuntimeError("the editor exited early")
        if "Scene opened:" in (work / log_name).read_text():
            break
        time.sleep(.2)
    time.sleep(4)
    return app, log


def stop_editor(app, log, log_name):
    app.terminate()
    try:
        app.wait(timeout=15)
    except subprocess.TimeoutExpired:
        app.kill()
        app.wait()
    log.close()
    shutil.copyfile(work / log_name, out / log_name)


def graph():
    return json.loads(graph_file.read_text())


passed = []
# ---------------------------------------------------------------- phase 1: graph RPCs from the node library
app, log = start_editor({"ZAMIN_EDITOR_VISUAL_GRAPH": "Scripts/ArenaRules.zgraph"}, "editor-graph.log")
try:
    before = graph()
    calls = lambda g: [n for n in g["Nodes"] if n["Kind"] == "Net.CallServerRpc" and n["Inputs"].get("Name") == "Horn"]
    typed("palette/search", "Horn")
    time.sleep(1)
    press("palette/rpc/Horn/call")
    press("header/Save")
    after = graph()
    assert len(calls(after)) == len(calls(before)) + 1, (len(calls(before)), len(calls(after)))
    passed.append("node library: Network RPC / Call Server RPC Horn added a call node")
    # A new RPC from the Network RPCs section, with a parameter.
    press("Network RPCs", "properties")
    typed("rpcs/RPC name", "Wave", "properties")
    press("rpcs/Create RPC", "properties")
    press("rpcs/Wave", "properties")
    press("rpcs/Wave/Parameters", "properties")
    press("rpcs/Wave/Params/Add parameter", "properties")
    press("rpcs/Wave/Add call node", "properties")
    press("header/Save")
    g = graph()
    assert g["Version"] == 7, g["Version"]
    wave = g["Rpcs"]["Wave"]
    assert wave["Kind"] == "Server" and [p["Name"] for p in wave["Params"]] == ["Value1"], wave
    assert any(n["Kind"] == "Event.Rpc" and n["Inputs"].get("Name") == "Wave" for n in g["Nodes"])
    assert any(n["Kind"] == "Net.CallServerRpc" and n["Inputs"].get("Name") == "Wave" for n in g["Nodes"])
    assert g["Variables"]["Joins"]["Replicated"] is True
    passed.append("Network RPCs: Wave created with a Number parameter, its handler and a call node; format 7 saved")
    shot("network-graph-rpc.png")
except BaseException:
    shot("network-graph-failure.png")
    raise
finally:
    stop_editor(app, log, "editor-graph.log")
# The saved graph validates in the engine (the cooked bytecode path of ZaminBench --graphs is not needed: the Player
# loads it in phase 2 on every side).

# ---------------------------------------------------------------- phase 2: Play with 2 clients, the Network panel
app, log = start_editor({"ZAMIN_EDITOR_NETWORK": "1"}, "editor-play.log")
try:
    press("network/Host on Play")
    press("network/More players")
    press("network/Headless clients")
    press("network/Preset bad link")
    assert value("network/conditioner").startswith("50 ms"), value("network/conditioner")
    press("toolbar/Play")
    wait_value("network/role", lambda v: v == "host", 30)
    clients = wait_value("network/clients", lambda v: v >= 2, 90)
    rtts = {k: v for k, v in data()["Values"].items() if k.startswith("network/client/") and k.endswith("/rttMs")}
    assert len(rtts) >= 2, rtts
    wait_value("network/client/1/rttMs", lambda v: v > 50, 30)
    text = (work / "editor-play.log").read_text()
    for needle in ("NETARENA player spawned for client 0", "NETARENA player spawned for client 1", "NETARENA player spawned for client 2"):
        until = time.time() + 20
        while needle not in text and time.time() < until:
            time.sleep(.5)
            text = (work / "editor-play.log").read_text()
        assert needle in text, needle
    assert value("network/entities") >= 3 + 6, value("network/entities")  # 3 players, 6 coins, the arena and the rules
    passed.append(f"Play with 3 players: {int(clients)} clients connected through the 100 ms / 5% link, RTT {', '.join(f'{v:.0f} ms' for v in rtts.values())}; "
                  f"the graph spawned a player for the host and each client")
    shot("network-editor-play.png")
    press("toolbar/Play")
    wait_value("network/role", lambda v: v == "none", 30)
    time.sleep(2)
    left = subprocess.run(["pgrep", "-f", "ZaminPlayer.*--connect 127.0.0.1:7777"], stdout=subprocess.PIPE, text=True).stdout.split()
    assert not left, left
    passed.append("Stop: the session ended and the client processes are gone")
except BaseException:
    shot("network-editor-failure.png")
    raise
finally:
    stop_editor(app, log, "editor-play.log")
    subprocess.run(["pkill", "-f", "ZaminPlayer.*--connect 127.0.0.1:7777"])

for line in passed:
    print("PASS", line)
print(f"network editor test: {len(passed)}/4 passed; screenshots in {out}")
