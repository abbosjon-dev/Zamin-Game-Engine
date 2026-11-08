#!/usr/bin/env python3
"""Performance check of the sample games (docs/Samples.md, "Performance"): every sample runs headless in ZaminPlayer
--benchmark under xvfb (Mesa's software rasterizer, llvmpipe, unless a GPU is there), the new samples with their bot
playing (ZAMIN_DEMO) on a fixed game clock (ZAMIN_CAPTURE_FPS=30: the same moments of the game on any computer), and
the frames are timed. With the player's statistics (ZAMIN_PLAYER_STATS) the log also has the last frame's scene (how
many entities, which components), what was drawn and the profiler tree - where the frame went.

    Tests/samples_bench.py                                  every sample, the build in $ZAMIN_BUILD_DIR or build/
    ZAMIN_BUILD_DIR=build-release Tests/samples_bench.py    a Release build (its own game modules: <build>/GameModules)
    Tests/samples_bench.py --game Racing --frames 600       one game, longer
    Tests/samples_bench.py --check                          fail (exit code 1) when a game is over its budget: its average
                                                            frame (per game, below), its slowest frame (HITCH_BUDGET) or
                                                            the game's own CPU work in a frame (GAME_CPU_BUDGET)
    Tests/samples_bench.py --out build/SampleBench          the reports, logs and table there (default)
Exit code: 0 done (and, with --check, every game within its budget), 1 a run failed or a budget was exceeded, 2 the
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

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (game, kind, environment, budget for the average frame in ms on llvmpipe at 1280x720 - Release build)
# The budgets are the reference computer's (docs/Samples.md: 4 vCPU, no GPU - Mesa llvmpipe) frame times with some room
# (~15%): a regression guard, not a target. On llvmpipe the frame is the rasterizer's work (the 3D scene's shading, SSAO,
# bloom, 2D lights per pixel); the game's own code is a small part of it (GAME_CPU_BUDGET). On a real GPU every sample
# runs far above 60 FPS.
# Every game: its slowest frame (a hitch: a shader the driver compiles, a texture made) at most this, and the game's own
# CPU work in a frame (scripts, physics, particles, animation, audio - GAME_CPU) at most this, in a Release build.
HITCH_BUDGET = 1000.0
GAME_CPU_BUDGET = 3.0

SAMPLES = [
    ("TopDownShooter", "2D", {"ZAMIN_DEMO": "1"}, 65.0),
    ("Breakout", "2D", {"ZAMIN_DEMO": "1"}, 55.0),
    ("FPSArena", "3D", {"ZAMIN_DEMO": "1"}, 145.0),
    ("Sokoban", "3D", {"ZAMIN_DEMO": "1"}, 150.0),
    ("EndlessRunner", "3D", {"ZAMIN_DEMO": "1"}, 180.0),
    ("Racing", "3D", {"ZAMIN_DEMO": "1"}, 135.0),
    ("Platformer2D", "2D", {}, 15.0),
    ("Showcase3D", "3D", {}, 125.0),
    ("Adventure3D", "3D", {}, 185.0),
]


def last_match(pattern, text):
    found = re.findall(pattern, text, re.M)
    return found[-1] if found else None


# The parts of a frame that are the game's own work on the CPU (the profiler's entries under "Layer'lar (update)"):
# everything else of a frame is drawing - on llvmpipe the rasterizer's work, on a GPU the driver's.
GAME_CPU = ("Skriptlar", "Fizika", "Particle'lar", "Animatsiya", "Audio", "AI", "Navigatsiya", "UI")


def profile_entries(text):
    """(depth, name, ms) of the last frame's profiler tree the player logs at exit (ZAMIN_PLAYER_STATS)."""
    block = text.split("Profiler (oxirgi kadr, ms):", 1)
    if len(block) < 2:
        return []
    entries = []
    for line in block[1].splitlines()[1:]:
        m = re.match(r"^(\s*)(\S.*?) ([0-9.]+) ms", line)
        if not m:
            if line.startswith("[") or not line.strip():
                break
            continue
        entries.append((len(m.group(1)) // 2, m.group(2), float(m.group(3))))
    return entries


def top_profile(entries, count=4):
    """The biggest parts of the frame (the post-processing passes counted under it)."""
    parts = [(ms, name) for depth, name, ms in entries if depth == 2 or (depth == 3 and name in ("Bloom", "SSAO", "FXAA"))]
    parts.sort(reverse=True)
    return [f"{name} {ms:.1f}" for ms, name in parts[:count]]


def run(game, kind, env_extra, budget, args, runner, out):
    project = os.path.join(ROOT, "Projects", game)
    log = os.path.join(out, game + ".log")
    report = os.path.join(out, game + ".json")
    home = tempfile.mkdtemp(prefix="zamin-bench-home-")
    # The caller's ZAMIN_* settings are left out (a ZAMIN_DEMO meant for another run ...); the build's own game
    # modules when it has them (a Release build configured with ZAMIN_GAME_MODULE_OUTPUT_DIR=<build>/GameModules).
    env = {k: v for k, v in os.environ.items() if not k.startswith("ZAMIN_")}
    env.update({"HOME": home, "ZAMIN_PLAYER_STATS": "1", "ZAMIN_CAPTURE_FPS": "30", **env_extra})
    modules = os.path.join(args.build, "GameModules")
    if os.path.isdir(modules):
        env["ZAMIN_SCRIPT_MODULE_DIR"] = modules
    command = runner + [args.player, project, "--benchmark", str(args.frames), "--benchmark-warmup", str(args.warmup),
                        "--benchmark-out", report, "--log-file", log, "--windowed", "--resolution", args.resolution]
    proc = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout)
    shutil.rmtree(home, ignore_errors=True)
    text = open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else ""
    row = {"game": game, "kind": kind, "budget": budget, "code": proc.returncode}
    try:
        with open(report, encoding="utf-8") as f:
            data = json.load(f)
        frame, update = data.get("Frame", {}), data.get("Update", {})
        row.update(avg=frame.get("Average"), median=frame.get("Median"), p95=frame.get("P95"), p99=frame.get("P99"),
                   max=frame.get("Max"), update=update.get("Average"), renderer=data.get("Renderer", ""))
    except (OSError, ValueError):
        row["error"] = "hisobot yo'q"
    m = re.search(r"Sahna \(oxirgi kadr\): (\d+) obyekt \((\d+) faol\), (\d+) komponent, (\d+) skript; eng ko'p: (.*)", text)
    if m:
        row.update(entities=int(m.group(1)), active=int(m.group(2)), components=int(m.group(3)), scripts=int(m.group(4)), top=m.group(5))
    stats = last_match(r"Render statistikasi \(oxirgi kadr\): (.*)$", text)
    if stats:
        dc = re.search(r"draw call (\d+), uchburchak (\d+)", stats)
        quads = re.search(r"quad (\d+)", stats)
        particles = re.search(r"particle (\d+)", stats)
        row.update(drawcalls=int(dc.group(1)) if dc else None, triangles=int(dc.group(2)) if dc else None,
                   quads=int(quads.group(1)) if quads else None, particles=int(particles.group(1)) if particles else None)
    entries = profile_entries(text)
    row["profile"] = top_profile(entries)
    row["game_cpu"] = round(sum(ms for depth, name, ms in entries if depth == 2 and name in GAME_CPU), 2) if entries else None
    with_frames = isinstance(row.get("avg"), (int, float))
    row["over"] = with_frames and (row["avg"] > budget or (row.get("max") or 0) > HITCH_BUDGET or (row["game_cpu"] or 0) > GAME_CPU_BUDGET)
    return row


def fmt(value, digits=1):
    return f"{value:.{digits}f}" if isinstance(value, (int, float)) else "-"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", action="append", default=[], help="only this sample (more than once: several)")
    parser.add_argument("--build", default=os.environ.get("ZAMIN_BUILD_DIR", os.path.join(ROOT, "build")))
    parser.add_argument("--out", help="reports, logs and the table (default: <build>/SampleBench)")
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--warmup", type=int, default=90)
    parser.add_argument("--resolution", default="1280x720")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--check", action="store_true", help="exit code 1 when a game's average frame is over its budget")
    args = parser.parse_args()
    args.build = os.path.abspath(args.build)
    args.player = os.path.join(args.build, "bin", "ZaminPlayer")
    if not os.access(args.player, os.X_OK):
        print(f"ZaminPlayer topilmadi: {args.player}", file=sys.stderr)
        return 2
    runner = []
    if not os.environ.get("DISPLAY"):
        if not shutil.which("xvfb-run"):
            print("Displey yo'q va xvfb-run topilmadi", file=sys.stderr)
            return 2
        runner = ["xvfb-run", "-a", "-s", "-screen 0 1600x900x24"]
    out = os.path.abspath(args.out or os.path.join(args.build, "SampleBench"))
    os.makedirs(out, exist_ok=True)
    wanted = {g.lower() for g in args.game}
    rows = []
    for game, kind, env, budget in SAMPLES:
        if wanted and game.lower() not in wanted:
            continue
        print(f"[{game}] {args.frames} kadr ({args.warmup} isitish) ...", flush=True)
        row = run(game, kind, env, budget, args, runner, out)
        rows.append(row)
        print(f"  o'rtacha {fmt(row.get('avg'))} ms, p95 {fmt(row.get('p95'))} ms, eng sekin {fmt(row.get('max'), 0)} ms, "
              f"o'yin kodi {fmt(row.get('game_cpu'), 2)} ms, yangilash {fmt(row.get('update'))} ms, "
              f"{row.get('entities', '-')} obyekt, {row.get('drawcalls', '-')} draw call; {', '.join(row['profile'])}", flush=True)
    lines = ["| Namuna | Tur | Obyekt (faol) | Draw call | Uchburchak / quad | Kadr o'rtacha | p95 | Eng sekin | FPS | O'yin kodi (CPU) | Byudjet | Eng qimmat qismlar (ms) |",
             "|---|---|---:|---:|---|---:|---:|---:|---:|---:|---|---|"]
    for r in rows:
        fps = 1000.0 / r["avg"] if isinstance(r.get("avg"), (int, float)) and r["avg"] > 0 else None
        geometry = f"{r.get('triangles', '-')} / {r.get('quads', '-')}"
        verdict = "XATO" if r.get("error") or r["code"] not in (0,) else ("oshdi" if r["over"] else "OK")
        lines.append(f"| {r['game']} | {r['kind']} | {r.get('entities', '-')} ({r.get('active', '-')}) | {r.get('drawcalls', '-')} | {geometry} | "
                     f"{fmt(r.get('avg'))} ms | {fmt(r.get('p95'))} ms | {fmt(r.get('max'), 0)} ms | {fmt(fps, 0)} | {fmt(r.get('game_cpu'), 2)} ms | "
                     f"{r['budget']:.0f} ms: {verdict} | {', '.join(r['profile'])} |")
    table = "\n".join(lines)
    with open(os.path.join(out, "samples-bench.md"), "w", encoding="utf-8") as f:
        f.write(table + "\n")
    with open(os.path.join(out, "samples-bench.json"), "w", encoding="utf-8") as f:
        json.dump(rows, f, ensure_ascii=False, indent=2)
    print("\n" + table)
    failed = [r["game"] for r in rows if r.get("error") or r["code"] != 0]
    over = [r["game"] for r in rows if r["over"]]
    if failed:
        print(f"\nIshlamadi: {', '.join(failed)} (loglar: {out})")
    if over:
        print(f"\nByudjetdan oshdi: {', '.join(over)}")
    return 1 if failed or (args.check and over) else 0


if __name__ == "__main__":
    sys.exit(main())
