#!/usr/bin/env python3
"""The networking integration test (ctest: NetworkIntegration; docs/Networking.md, docs/Testing.md).

A real dedicated server and two headless bot clients of Projects/NetArena run as separate ZaminPlayer processes on
localhost, every side's packets through the link conditioner (latency 50 +- 10 ms each way: about 100 +- 20 ms of
round trip, 5% loss each way). The bots collect coins (a server RPC the server checks; the server multicasts a burst
effect back). Midway one client is killed with SIGKILL and started again with its saved session file: it must resume
its session (the same client id, its player and score kept). Checks:

  - every process exits 0, no crash, no ERROR line;
  - state agreement: the server and both clients log the server's state every 30 ticks (ZAMIN_NET_STATE_LOG): every
    tick logged on more than one side has byte-identical state on all of them (positions, owners, scores), at least
    20 such ticks;
  - RPC effects: coins were collected (server), and the multicast bursts reached both clients;
  - the graph: the replicated Joins counter's Event.Rep ran on the clients (BLUEPRINT_NET joins=...);
  - reconnect: the restarted client resumed the same client id;
  - RTT 60..250 ms and a bandwidth figure per client are reported (summary.json).

    Tests/network_integration_test.py [--build build] [--out <dir>] [--seconds 30]
Exit code 0 when every check holds, 1 otherwise, 2 when the player or the sample module is missing.
"""
import argparse
import json
import os
import re
import signal
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, "Projects", "NetArena")


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--build", default=os.environ.get("ZAMIN_BUILD_DIR", os.path.join(ROOT, "build")))
    parser.add_argument("--out", default=None)
    parser.add_argument("--seconds", type=float, default=30.0)
    args = parser.parse_args()
    player = os.path.join(args.build, "bin", "ZaminPlayer")
    if not os.path.isfile(player) or not os.path.isfile(os.path.join(PROJECT, "Binaries", "libNetArena.so")):
        print("SKIP-ERROR: ZaminPlayer or libNetArena.so missing (build the engine with the samples)")
        return 2
    out = os.path.abspath(args.out or os.path.join(args.build, "NetworkIntegration"))
    os.makedirs(out, exist_ok=True)
    for name in os.listdir(out):
        if name.endswith((".log", ".json", ".session")):
            os.remove(os.path.join(out, name))
    home = os.path.join(out, "home")
    os.makedirs(home, exist_ok=True)
    env = dict(os.environ, HOME=home, ZAMIN_NET_STATE_LOG="30")
    port = free_port()
    seconds = args.seconds
    conditioner = "latency=50,jitter=10,loss=5"
    procs = {}

    def start(name, extra, quit_after):
        log = open(os.path.join(out, f"{name}.log"), "w")
        cmd = [player, PROJECT, "--no-log-file", "--no-crash-dialog", "--quit-after", str(quit_after),
               "--net-report", os.path.join(out, f"{name}.json")] + extra
        procs[name] = (subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, env=env, cwd=out), log)
        return procs[name][0]

    def client(name, seed, quit_after):
        return start(name, ["--headless", "--connect", f"127.0.0.1:{port}", "--net-bot",
                            "--net-conditioner", f"{conditioner},seed={seed}",
                            "--net-session-file", os.path.join(out, f"{name[0]}.session")], quit_after)

    began = time.time()
    start("server", ["--server", "--port", str(port), "--net-conditioner", f"{conditioner},seed=1"], seconds + 8)
    time.sleep(1.0)
    client("A", 11, seconds)
    client("B1", 22, seconds)
    # Midway: B is killed (no goodbye) and comes back with its session file.
    time.sleep(seconds * 0.4)
    procs["B1"][0].send_signal(signal.SIGKILL)
    procs["B1"][0].wait()
    time.sleep(2.0)
    client("B2", 23, seconds * 0.5)
    codes = {}
    for name, (proc, log) in list(procs.items()):
        try:
            codes[name] = proc.wait(timeout=seconds + 30)
        except subprocess.TimeoutExpired:
            proc.kill()
            codes[name] = "timeout"
        log.close()
    elapsed = time.time() - began

    logs = {name: open(os.path.join(out, f"{name}.log"), errors="replace").read() for name in procs}
    reports = {}
    for name in ("server", "A", "B2"):
        try:
            reports[name] = json.load(open(os.path.join(out, f"{name}.json")))
        except (OSError, ValueError):
            reports[name] = None
    results = []

    def check(description, ok, detail=""):
        results.append({"check": description, "ok": bool(ok), "detail": detail})
        print(f"  [{'PASS' if ok else 'FAIL'}] {description}{': ' + detail if detail else ''}")

    print(f"NetArena: server and 2 clients on 127.0.0.1:{port}, {conditioner} on every side, {elapsed:.0f} s")
    check("the server and the clients exit 0", all(codes[n] == 0 for n in ("server", "A", "B2")), str(codes))
    for name, text in logs.items():
        bad = re.findall(r".*(?:ERROR|CRITICAL|CRASH).*", text)
        check(f"{name}: no error or crash in the log", not bad, bad[0][:160] if bad else "")
        check(f"{name}: the game's C++ module loaded", "Script module loaded: libNetArena.so" in text)

    # State agreement on the ticks logged by more than one side.
    states = {}
    for name in ("server", "A", "B1", "B2"):
        for m in re.finditer(r"NETSTATE tick=(\d+)(.*)", logs[name]):
            states.setdefault(int(m.group(1)), {})[name] = m.group(2).strip()
    common = {t: v for t, v in states.items() if "server" in v and len(v) >= 2}
    disagree = [t for t, v in common.items() if len(set(v.values())) != 1]
    check("state agreement: the same state on every side for the same tick", len(common) >= 20 and not disagree,
          f"{len(common)} ticks compared, {len(disagree)} differ" + (f" (first: {min(disagree)})" if disagree else ""))
    three = sum(1 for v in common.values() if {"server", "A"} <= set(v) and ("B1" in v or "B2" in v))
    check("state agreement includes ticks seen by all three", three >= 10, f"{three} ticks")

    collected = len(re.findall(r"NETARENA collect by client", logs["server"]))
    check("RPC: the server accepted Collect calls", collected >= 3, f"{collected} coins collected")
    for name in ("A", "B2"):
        bursts = len(re.findall(r"NETARENA burst #", logs[name]))
        check(f"RPC: {name} got the multicast bursts", bursts >= 1, f"{bursts} bursts")
    check("graph: the replicated Joins counter reached the clients (Event.Rep)", "BLUEPRINT_NET joins=" in logs["A"] and "BLUEPRINT_NET joins=" in logs["B2"])

    first = re.search(r"Connected to \S+ as client (\d+)", logs["B1"])
    again = re.search(r"Connected to \S+ as client (\d+) \(session resumed\)", logs["B2"])
    check("reconnect: the killed client resumed its session (same client id)", first and again and first.group(1) == again.group(1),
          f"{first.group(1) if first else '?'} -> {again.group(1) if again else 'no resume'}")
    if again and reports.get("B2"):
        mine = [e for e in reports["B2"]["Entities"] if e["Name"] == "Player" and str(e["Owner"]) == again.group(1)]
        check("reconnect: its player came back with it", len(mine) == 1, json.dumps(mine)[:120])

    summary = {"Seconds": round(elapsed, 1), "Conditioner": conditioner, "Collected": collected, "ComparedTicks": len(common), "Clients": {}}
    for name in ("A", "B2"):
        r = reports.get(name)
        if not r or "Connection" not in r:
            check(f"{name}: a report with its connection", False)
            continue
        c = r["Connection"]
        summary["Clients"][name] = {"RttMs": round(c["RttMs"], 1), "LossPercent": round(c["LossPercent"], 1),
                                     "DownKBps": round(c["ReceivedBytesPerSecond"] / 1024, 2), "UpKBps": round(c["SentBytesPerSecond"] / 1024, 2),
                                     "Corrections": r["Stats"]["Corrections"], "SnapshotsReceived": r["Stats"]["SnapshotsReceived"]}
        check(f"{name}: RTT through the conditioner is 60..250 ms", 60 <= c["RttMs"] <= 250, f"{c['RttMs']:.0f} ms, loss {c['LossPercent']:.1f}%, "
              f"down {c['ReceivedBytesPerSecond'] / 1024:.2f} KB/s, up {c['SentBytesPerSecond'] / 1024:.2f} KB/s")
    if reports.get("server"):
        s = reports["server"]["Stats"]
        summary["Server"] = {"AverageTickMs": round(s["AverageTickMs"], 3), "MaxTickMs": round(s["MaxTickMs"], 3), "SnapshotsSent": s["SnapshotsSent"]}
        check("server: the tick cost is reported", s["SnapshotsSent"] > 0, f"avg {s['AverageTickMs']:.3f} ms, max {s['MaxTickMs']:.3f} ms a tick")
    json.dump({"Summary": summary, "Checks": results}, open(os.path.join(out, "summary.json"), "w"), indent=2)
    failed = [r for r in results if not r["ok"]]
    print(f"{len(results) - len(failed)}/{len(results)} checks passed; logs and reports in {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
