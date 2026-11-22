#!/usr/bin/env python3
"""Profiling and regression gate of the two vertical slices (docs/VerticalSlices.md#profiling, docs/Benchmarks.md):
every slice scene runs in ZaminPlayer --benchmark under xvfb (Mesa llvmpipe unless a GPU is there) on a fixed game clock
(ZAMIN_CAPTURE_FPS=30), the levels with the demo pilot playing (ZAMIN_DEMO=1), the menus as a player sees them, in a
small window with the Low quality level (the CPU's share: the game code) and at 1280x720 (recorded and regression-gated).

    python3 Tests/slice_bench.py --build build-release                     every row, the table and slices.json
    python3 Tests/slice_bench.py --build build-release --check             fail (exit 1) when a small-window row breaks a gate
    python3 Tests/slice_bench.py --build build-release --compare Tests/Baselines/slices-release.json
                                                                            fail (exit 1) when a number regressed (bench_compare.py)
    python3 Tests/slice_bench.py --build build-release --write-baseline Tests/Baselines/slices-release.json
    python3 Tests/slice_bench.py --build build-release --runs 3            each row three times, the best of them
    python3 Tests/slice_bench.py --build build-release --soak 18000        the attract loop soak (ZAMIN_DEMO=1, 284x160 Low)
    python3 Tests/slice_bench.py --only LanternKeeper/Boglar               one row (game/scene, both sizes)

Gates of the small window (284x160, Low; Release): the game code (the update layer without drawing: scripts and graphs,
gameplay, physics, AI, animation, particles, audio) mean <= 3 ms (2D) / 4 ms (3D) and p95 <= 6 ms; the frame mean <= 16.6
ms (2D, 60 FPS) / 33.3 ms (3D, 30 FPS: on llvmpipe the CPU rasterizes the 3D level's ~74 000 triangles twice - shadow map
and main pass - which no window size removes); the slowest frame <= 1000 ms. Each row runs --runs times (default 3) with
one HOME: the first run is a first session (the driver compiles its shader variants then - Mesa keeps them in
~/.cache - and that run's slowest frame is reported as "cold"), the gates and the baseline take each number's best run.
The 1280x720 rows are llvmpipe's rasterizer more than the game: they are only compared with the baseline (+15 %).

The soak plays the attract loop (the menu starts a new game, the pilot wins it, the victory returns to the menu, the menu
starts again) for --soak frames and samples the player's memory (ZAMIN_SOAK_STATS): the resident memory may not grow by
more than 32 MiB after the first loop, every return to the menu has the same number of entities, no log errors.
Exit code: 0 passed, 1 a run failed / a gate or the comparison failed, 2 the player is missing.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# (game, scene, kind, demo): the menus without the demo (it would start a game after a second), the levels with it.
ROWS = [
    ("LanternKeeper", "Menu", "2D", False),
    ("LanternKeeper", "Boglar", "2D", True),
    ("LanternKeeper", "Yertola", "2D", True),
    ("BeaconPass", "Menu", "3D", False),
    ("BeaconPass", "Dovon", "3D", True),
]
SIZES = {"Small": ("284x160", "Low"), "720p": ("1280x720", None)}
FRAME_BUDGET = {"2D": 16.6, "3D": 33.3}
HITCH_BUDGET, CODE_P95_BUDGET = 1000.0, 6.0
CODE_BUDGET = {"2D": 3.0, "3D": 4.0}
SOAK_GROWTH_MIB = 32.0
FORBIDDEN = ("GL_INVALID_", "Assertion failed", "Visual graph '", "Instruction budget exceeded", "SLICE_CONFIG_FAILED", "SLICE_IO_FAILED",
             "SLICE_ORACLE_FAILED", "nomli harakat topilmadi", "Snaryad vizual prefabi rad etildi")


def last(pattern, text):
    found = re.findall(pattern, text, re.M)
    return found[-1] if found else None


def profile_top(text, count=5):
    """The biggest parts of the last frame's update layer (ZAMIN_PLAYER_STATS' profiler tree), drawing included."""
    block = text.split("Profiler (oxirgi kadr, ms):", 1)
    if len(block) < 2:
        return []
    parts = []
    for line in block[1].splitlines()[1:]:
        m = re.match(r"^(\s*)(\S.*?) ([0-9.]+) ms", line)
        if not m:
            if line.startswith("[") or not line.strip():
                break
            continue
        if len(m.group(1)) // 2 == 2:
            parts.append((float(m.group(3)), m.group(2)))
    parts.sort(reverse=True)
    return [f"{name} {ms:.2f}" for ms, name in parts[:count]]


def environment(home, demo, extra=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith("ZAMIN_")}
    env.update(HOME=str(home), ZAMIN_PLAYER_STATS="1", ZAMIN_CAPTURE_FPS="30")
    if demo:
        env["ZAMIN_DEMO"] = "1"
    env.update(extra or {})
    return env


def bench(args, runner, out, game, scene, kind, demo, size, home, run):
    resolution, quality = SIZES[size]
    name = f"{game}-{scene}-{size}-{run + 1}"
    log, report = out / f"{name}.log", out / f"{name}.json"
    cmd = runner + [str(args.player), str(ROOT / "Projects" / game), "--scene", scene, "--benchmark", str(args.frames), "--benchmark-warmup", str(args.warmup),
                    "--benchmark-out", str(report), "--log-file", str(log), "--windowed", "--resolution", resolution]
    if quality:
        cmd += ["--quality", quality]
    proc = subprocess.run(cmd, env=environment(home, demo), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout)
    text = log.read_text(errors="replace") if log.exists() else ""
    row = {"game": game, "scene": scene, "kind": kind, "size": size, "resolution": resolution, "quality": quality or "default", "code": proc.returncode}
    try:
        data = json.loads(report.read_text())
        frame, code = data.get("Frame", {}), data.get("GameCode", {})
        row.update(avg=frame.get("Average"), median=frame.get("Median"), p95=frame.get("P95"), p99=frame.get("P99"), max=frame.get("Max"),
                   code_avg=code.get("Average"), code_p95=code.get("P95"), code_max=code.get("Max"), renderer=data.get("Renderer", ""))
    except (OSError, ValueError):
        row["error"] = "no report"
    m = re.search(r"Sahna \(oxirgi kadr\): (\d+) obyekt \((\d+) faol\)", text)
    if m:
        row.update(entities=int(m.group(1)), active=int(m.group(2)))
    stats = last(r"Render statistikasi \(oxirgi kadr\): (.*)$", text)
    if stats:
        dc = re.search(r"draw call (\d+), uchburchak (\d+)", stats)
        quads = re.search(r"quad (\d+)", stats)
        row.update(drawcalls=int(dc.group(1)) if dc else None, triangles=int(dc.group(2)) if dc else None, quads=int(quads.group(1)) if quads else None)
    mem = re.search(r"Xotira: RAM ([0-9.]+) (KB|MB|GB)", text)
    if mem:
        row["rss_mib"] = round(float(mem.group(1)) * {"KB": 1 / 1024, "MB": 1.0, "GB": 1024.0}[mem.group(2)], 1)
    row["profile"] = profile_top(text)
    row["forbidden"] = [f for f in FORBIDDEN if f in text]
    return row


def gates(row):
    """The small window's gates on a row (its best numbers)."""
    found, kind = [], row["kind"]
    if row["size"] != "Small" or not isinstance(row.get("avg"), (int, float)):
        return found
    if row["avg"] > FRAME_BUDGET[kind]:
        found.append(f"frame {row['avg']:.2f} > {FRAME_BUDGET[kind]} ms")
    if (row.get("max") or 0) > HITCH_BUDGET:
        found.append(f"slowest {row['max']:.0f} > {HITCH_BUDGET:.0f} ms")
    if row.get("code_avg") is None:
        found.append("no game code measure")
    else:
        if row["code_avg"] > CODE_BUDGET[kind]:
            found.append(f"game code {row['code_avg']:.2f} > {CODE_BUDGET[kind]} ms")
        if row["code_p95"] > CODE_P95_BUDGET:
            found.append(f"game code p95 {row['code_p95']:.2f} > {CODE_P95_BUDGET} ms")
    return found


def best(rows):
    """Several runs of one row: each number's best (the smallest), the rest of the first run; the first run's slowest
    frame (a first session: the driver's shader compiles) as "cold_max"."""
    merged = dict(rows[0])
    for key in ("avg", "median", "p95", "p99", "max", "code_avg", "code_p95", "code_max"):
        values = [r[key] for r in rows if isinstance(r.get(key), (int, float))]
        if values:
            merged[key] = min(values)
    merged["runs"] = len(rows)
    merged["cold_max"] = rows[0].get("max")
    merged["code"] = max(r["code"] for r in rows)
    merged["forbidden"] = sorted({f for r in rows for f in r["forbidden"]})
    merged["gates"] = gates(merged)
    return merged


def results_json(rows):
    """The bench_compare.py format: one key per number (ms per frame). At 1280x720 llvmpipe's rasterizer threads share
    the 4 cores with the game: there the percentiles swing by 15-20 % between identical runs, so they are recorded as
    "Noisy" (listed by bench_compare.py, never counted); the means are compared."""
    results = {}
    for r in rows:
        key = f"{r['game']}.{r['scene']}.{r['size']}"
        for field, name in (("avg", "Frame"), ("p95", "FrameP95"), ("code_avg", "GameCode"), ("code_p95", "GameCodeP95")):
            if isinstance(r.get(field), (int, float)):
                results[f"{key}.{name}"] = {"Ms": r[field], "PerFrame": True, **({"Noisy": True} if r["size"] != "Small" and name.endswith("P95") else {})}
    return {"Build": "Release", "Frames": None, "Results": results}


def fmt(value, digits=2):
    return f"{value:.{digits}f}" if isinstance(value, (int, float)) else "-"


def soak(args, runner, out):
    """The attract loop: memory and entity counts at each return to the menu (ZAMIN_SOAK_STATS lines)."""
    home = Path(tempfile.mkdtemp(prefix="zamin-slice-soak-"))
    results = []
    for game in ("LanternKeeper", "BeaconPass"):
        log = out / f"soak-{game}.log"
        env = environment(home / game, True, {"ZAMIN_MAX_FRAMES": str(args.soak), "ZAMIN_SOAK_STATS": "30"})
        cmd = runner + [str(args.player), str(ROOT / "Projects" / game), "--no-splash", "--windowed", "--resolution", "284x160", "--quality", "Low",
                        "--log-file", str(log)]
        proc = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout * 6)
        text = log.read_text(errors="replace") if log.exists() else ""
        samples = [(int(f), s, float(rss), int(ent)) for f, s, rss, ent in
                   re.findall(r"SOAK frame=(\d+) scene=(\S+) rss=([0-9.]+) entities=(\d+)", text)]
        menus = [x for x in samples if x[1] == "Menu"]
        # the returns to the menu: a menu sample after a level sample
        returns, previous = [], None
        for x in samples:
            if x[1] == "Menu" and previous and previous[1] != "Menu":
                returns.append(x)
            previous = x
        victories = text.count("SLICE_VICTORY")
        first_return = returns[0][2] if returns else None
        peak_after = max((x[2] for x in samples if returns and x[0] >= returns[0][0]), default=None)
        growth = round(peak_after - first_return, 1) if first_return is not None and peak_after is not None else None
        errors = [line for line in text.splitlines() if " ERROR " in line or " CRITICAL " in line]
        row = {"game": game, "frames": args.soak, "code": proc.returncode, "victories": victories, "menu_returns": len(returns),
               "rss_first_return_mib": first_return, "rss_peak_after_mib": peak_after, "rss_growth_mib": growth,
               "rss_start_mib": samples[0][2] if samples else None, "rss_end_mib": samples[-1][2] if samples else None,
               "menu_entities": sorted({x[3] for x in returns}), "samples": len(samples), "errors": errors[:5],
               "forbidden": [f for f in FORBIDDEN if f in text]}
        gates = []
        if proc.returncode != 0:
            gates.append(f"exit code {proc.returncode}")
        if len(returns) < 2:
            gates.append(f"only {len(returns)} returns to the menu (at least 2 loops needed)")
        if growth is not None and growth > SOAK_GROWTH_MIB:
            gates.append(f"memory grew {growth} MiB > {SOAK_GROWTH_MIB} after the first loop")
        if len(row["menu_entities"]) > 1:
            gates.append(f"menu entity counts differ: {row['menu_entities']}")
        if errors or row["forbidden"]:
            gates.append("log errors")
        row["gates"] = gates
        results.append(row)
        print(f"[soak {game}] {args.soak} frames: {victories} victories, {len(returns)} menu returns, RSS {row['rss_start_mib']} -> first return "
              f"{first_return} -> peak {peak_after} MiB (growth {growth}), menu entities {row['menu_entities']}" + (f" - FAILED: {gates}" if gates else ""),
              flush=True)
    shutil.rmtree(home, ignore_errors=True)
    return results


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--build", type=Path, default=Path(os.environ.get("ZAMIN_BUILD_DIR", ROOT / "build-release")))
    p.add_argument("--out", type=Path)
    p.add_argument("--frames", type=int, default=300)
    p.add_argument("--warmup", type=int, default=90)
    p.add_argument("--runs", type=int, default=3)
    p.add_argument("--timeout", type=int, default=900)
    p.add_argument("--only", action="append", default=[], help="game/scene (more than once: several)")
    p.add_argument("--sizes", default="Small,720p")
    p.add_argument("--check", action="store_true")
    p.add_argument("--compare", type=Path)
    p.add_argument("--write-baseline", type=Path)
    p.add_argument("--soak", type=int, default=0, help="frames of the attract-loop soak (0: none)")
    p.add_argument("--no-bench", action="store_true", help="only the soak")
    args = p.parse_args()
    args.build = args.build.resolve()
    args.player = args.build / "bin/ZaminPlayer"
    if not os.access(args.player, os.X_OK):
        print(f"ZaminPlayer topilmadi: {args.player}", file=sys.stderr)
        return 2
    runner = [] if os.environ.get("DISPLAY") else ["xvfb-run", "-a", "-s", "-screen 0 1600x900x24"]
    out = (args.out or args.build / "SliceBench").resolve()
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    if not args.no_bench:
        for game, scene, kind, demo in ROWS:
            if args.only and f"{game}/{scene}" not in args.only:
                continue
            for size in args.sizes.split(","):
                home = Path(tempfile.mkdtemp(prefix="zamin-slice-bench-"))  # one HOME per row: the first run is a first session
                runs = [bench(args, runner, out, game, scene, kind, demo, size, home, run) for run in range(max(1, args.runs))]
                shutil.rmtree(home, ignore_errors=True)
                row = best(runs)
                rows.append(row)
                print(f"[{game}/{scene} {size}] frame {fmt(row.get('avg'))} ms (p95 {fmt(row.get('p95'))}, slowest {fmt(row.get('max'), 0)}), game code "
                      f"{fmt(row.get('code_avg'))} ms (p95 {fmt(row.get('code_p95'))}), {row.get('entities', '-')} entities, {row.get('drawcalls', '-')} draw calls, "
                      f"RSS {row.get('rss_mib', '-')} MiB, first session's slowest {fmt(row.get('cold_max'), 0)} ms; {', '.join(row['profile'])}" + (f" - GATE: {row['gates']}" if row["gates"] else ""), flush=True)
    lines = ["| Slice / scene | Window | Entities (active) | Draw calls | Triangles / quads | Frame mean | p95 | Slowest (first session) | Game code mean | p95 | RSS | Gate | Top of the update layer (ms) |",
             "|---|---|---:|---:|---|---:|---:|---:|---:|---:|---:|---|---|"]
    for r in rows:
        verdict = "error" if r.get("error") or r["code"] != 0 or r["forbidden"] else ("over" if r["gates"] else ("ok" if r["size"] == "Small" else "baseline"))
        lines.append(f"| {r['game']} / {r['scene']} | {r['resolution']} {r['quality']} | {r.get('entities', '-')} ({r.get('active', '-')}) | {r.get('drawcalls', '-')} | "
                     f"{r.get('triangles', '-')} / {r.get('quads', '-')} | {fmt(r.get('avg'))} ms | {fmt(r.get('p95'))} | {fmt(r.get('max'), 0)} ({fmt(r.get('cold_max'), 0)}) | "
                     f"{fmt(r.get('code_avg'))} ms | {fmt(r.get('code_p95'))} | {r.get('rss_mib', '-')} MiB | {verdict} | {', '.join(r['profile'])} |")
    soaked = soak(args, runner, out) if args.soak else []
    report = {"Rows": rows, "Soak": soaked}
    (out / "slices.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    (out / "slices.md").write_text("\n".join(lines) + "\n")
    if rows:
        (out / "slices-results.json").write_text(json.dumps(results_json(rows), indent=2) + "\n")
        print("\n" + "\n".join(lines))
    failed = [f"{r['game']}/{r['scene']} {r['size']}" for r in rows if r.get("error") or r["code"] != 0 or r["forbidden"]]
    over = [f"{r['game']}/{r['scene']} {r['size']}: {r['gates']}" for r in rows if r["gates"]]
    soak_failed = [f"soak {r['game']}: {r['gates']}" for r in soaked if r["gates"]]
    if args.write_baseline and rows and not failed:
        data = results_json(rows)
        data["Frames"] = args.frames
        args.write_baseline.write_text(json.dumps(data, indent=2) + "\n")
        print(f"baseline: {args.write_baseline}")
    compared = 0
    if args.compare and rows:
        compared = subprocess.run([sys.executable, str(ROOT / "Tests/bench_compare.py"), str(args.compare), str(out / "slices-results.json"), "--tolerance", "15",
                                   "--markdown", str(out / "slices-compare.md")]).returncode
    for message in failed:
        print(f"FAILED: {message}")
    for message in (over if args.check else []) + soak_failed:
        print(f"GATE: {message}")
    return 1 if failed or (args.check and over) or soak_failed or compared else 0


if __name__ == "__main__":
    sys.exit(main())
