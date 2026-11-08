#!/usr/bin/env python3
"""The sample games exported and played the way a player gets them (ctest SampleExports, labels samples;gui;slow).

Every sample is exported by the editor (Tests/export_test.sh: ZAMIN_EDITOR_EXPORT headless, its Release script module,
the export's own folder check, <Game>.sh --version, a timed run, a run with a screenshot, the folder moved elsewhere),
then the exported games - not ZaminPlayer with the project folder - run their headless cases from Tests/samples_test.py
(--exported): the six M24 games' bot demos (score, levels, laps ... and the record saved and read back) and the older
samples' smoke runs. A table of the exports (size, files, frame time, result) is written to <out>/exports.md.

    Tests/samples_export.py                                every sample (about 40 minutes on 4 cores, llvmpipe)
    Tests/samples_export.py --game Breakout --game Racing  these samples only
    Tests/samples_export.py --out build/SampleExports      the exports, logs and reports kept there (the default)
    Tests/samples_export.py --skip-export                  the exports already in --out: only the play cases
    Tests/samples_export.py --kind demo --kind gamepad     other cases of samples_test.py on the exported games
ZAMIN_BUILD_DIR=<dir> (or --build) selects the editor / player build. Exit code: 0 all passed, 1 an export or a case
failed, 2 the editor / player is missing.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True

SAMPLES = ["TopDownShooter", "Breakout", "FPSArena", "Sokoban", "EndlessRunner", "Racing",
           "Platformer2D", "Showcase3D", "Adventure3D"]


def folder_size(path):
    total, files = 0, 0
    for folder, _, names in os.walk(path):
        for name in names:
            full = os.path.join(folder, name)
            if os.path.isfile(full) and not os.path.islink(full):
                total += os.path.getsize(full)
                files += 1
    return total, files


def benchmark_average(work):
    """The average frame time of export_test.sh's timed run (benchmark.json in its work folder)."""
    try:
        with open(os.path.join(work, "benchmark.json"), encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return None
    frame = data.get("Frame") or data.get("Results", {}).get("Frame") or {}
    return frame.get("Average")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", action="append", default=[], help="only this sample (more than once: several)")
    parser.add_argument("--kind", action="append", default=[], help="samples_test.py cases on the exports (default: demo, smoke)")
    parser.add_argument("--build", default=os.environ.get("ZAMIN_BUILD_DIR", os.path.join(ROOT, "build")))
    parser.add_argument("--out", help="exports, logs and reports (default: <build>/SampleExports)")
    parser.add_argument("--frames", type=int, default=60, help="frames of export_test.sh's timed run")
    parser.add_argument("--skip-export", action="store_true", help="use the exports already in --out")
    args = parser.parse_args()

    build = os.path.abspath(args.build)
    out = os.path.abspath(args.out or os.path.join(build, "SampleExports"))
    games = [g for g in SAMPLES if not args.game or g.lower() in {x.lower() for x in args.game}]
    if not games:
        print("Hech qanday namuna tanlanmadi (--game)", file=sys.stderr)
        return 2
    for tool in ("ZaminEditor", "ZaminPlayer"):
        if not os.access(os.path.join(build, "bin", tool), os.X_OK):
            print(f"{tool} topilmadi: {build}/bin (avval: cmake --build {build})", file=sys.stderr)
            return 2
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, ZAMIN_BUILD_DIR=build)

    rows, failed = [], []
    if not args.skip_export:
        for game in games:
            work = os.path.join(out, game)
            shutil.rmtree(work, ignore_errors=True)
            print(f"== {game}: eksport", flush=True)
            start = time.time()
            proc = subprocess.run(["bash", os.path.join(ROOT, "Tests", "export_test.sh"), "--project", os.path.join(ROOT, "Projects", game),
                                   "--frames", str(args.frames), "--work", work], env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
            text = proc.stdout.decode("utf-8", "replace")
            with open(os.path.join(out, game + "-export.log"), "w", encoding="utf-8") as f:
                f.write(text)
            print("\n".join("   " + line for line in text.splitlines() if "[OK]" in line or "[XATO]" in line or "Eksport testi" in line), flush=True)
            size, files = folder_size(os.path.join(work, "export"))
            avg = benchmark_average(work)
            ok = proc.returncode == 0
            rows.append({"game": game, "export": ok, "seconds": round(time.time() - start), "size": size, "files": files, "avg": avg})
            if not ok:
                failed.append(f"{game} (eksport)")

    # The play cases on the exported games: the bot demos (+ records) and the older samples' smoke runs.
    playable = [g for g in games if os.path.isdir(os.path.join(out, g, "export"))]
    report = {}
    if playable:
        command = [sys.executable, os.path.join(ROOT, "Tests", "samples_test.py"), "--exported", out, "--build", build,
                   "--out", os.path.join(out, "cases")]
        for game in playable:
            command += ["--game", game]
        for kind in args.kind or ["demo", "smoke"]:
            command += ["--kind", kind]
        print(f"== eksport qilingan o'yinlar: {', '.join(playable)}", flush=True)
        code = subprocess.run(command, env=env).returncode
        try:
            with open(os.path.join(out, "cases", "report.json"), encoding="utf-8") as f:
                report = json.load(f)
        except (OSError, ValueError):
            report = {}
        if code != 0:
            failed.append("o'yin holatlari")
    for name in sorted({g for g in games} - set(playable)):
        failed.append(f"{name} (eksport papkasi yo'q)")

    # The table (docs/Samples.md quotes it).
    cases = {}
    for case in report.get("cases", []):
        cases.setdefault(case["game"], []).append(case)
    lines = ["| Namuna | Eksport | Hajmi | Fayllar | Kadr (o'rtacha, Debug) | Holatlar |", "|---|---|---:|---:|---:|---|"]
    for row in rows or [{"game": g, "export": True, "size": folder_size(os.path.join(out, g, "export"))[0],
                         "files": folder_size(os.path.join(out, g, "export"))[1], "avg": benchmark_average(os.path.join(out, g)),
                         "seconds": 0} for g in playable]:
        played = cases.get(row["game"], [])
        verdict = ", ".join(f"{c['kind']} {'OK' if c['passed'] else 'XATO'}" for c in played) or "-"
        avg = f"{row['avg']:.1f} ms" if isinstance(row["avg"], (int, float)) else "-"
        lines.append(f"| {row['game']} | {'OK' if row['export'] else 'XATO'} | {row['size'] / 1048576:.1f} MiB | {row['files']} | {avg} | {verdict} |")
    with open(os.path.join(out, "exports.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("\n" + "\n".join(lines))
    if failed:
        print(f"\nNatija: o'tmadi - {', '.join(failed)} (papka: {out})")
        return 1
    print(f"\nNatija: {len(games)} ta namuna eksport qilindi va eksportdan o'ynaldi (papka: {out})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
