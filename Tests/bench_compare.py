#!/usr/bin/env python3
"""Compares two ZaminBench --json results (a baseline and a new run): the change of every number, and which ones
got slower (or bigger) than the tolerance allows. Standard library only.

    Tests/bench_compare.py Tests/Baselines/ecs-release.json build-release/Bench/ecs.json [--tolerance 25] [--min-abs X] [--markdown f.md]
    Tests/bench_compare.py <baseline> run1.json run2.json run3.json    several runs: each number's best of them

Several runs of the same build (Tests/bench.sh --compare runs again when one looks slower): each number is the best
of them (the smallest; the largest for throughputs) - a real regression is slower in every run, a busy computer
(another process, the disk cache) only in some.

The files are the "Results" maps of `ZaminBench --ecs / --serialize / --serialize --prefabs --json <file>`: each key
has "Ms" (the ECS benchmark) or "Value" + "Unit". Lower is better for every unit (ms, ns, MB, B, %) except the
throughputs (".../s", e.g. obj/s). A number that got worse by more than --tolerance percent and by more than the
unit's smallest change that counts (ms 0.5, ms/kadr 0.2, ns 100, B 16, MB 0.2, % 2, obj/s 1000; --min-abs sets one
for all) is a regression: the exit code is then 1. Tiny numbers are noise, and so are the rows marked "Noisy"
(file I/O: the disk cache decides) - they are listed, never counted. A key that is in one file only is listed, not
counted. Numbers from different computers or build types are not comparable (the script says so when the "Build"
fields differ).
"""
import argparse
import json
import sys

# The smallest change (in the unit) that counts: below it a difference is measurement noise.
MIN_ABS = {"ms": 0.5, "ms/kadr": 0.2, "ns": 100.0, "B": 16.0, "MB": 0.2, "%": 2.0, "obj/s": 1000.0}


def load(path):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    results = {}
    for key, entry in data.get("Results", {}).items():
        if not isinstance(entry, dict):
            continue
        noisy = bool(entry.get("Noisy", False))
        if "Ms" in entry:
            results[key] = (float(entry["Ms"]), "ms/kadr" if entry.get("PerFrame") else "ms", noisy)
        elif "Value" in entry:
            results[key] = (float(entry["Value"]), str(entry.get("Unit", "")), noisy)
    return data, results


def best_of(runs):
    """The runs of one build merged: per key the best value (lower is better, higher for ".../s")."""
    data, merged = runs[0]
    merged = dict(merged)
    for _, results in runs[1:]:
        for key, (value, unit, noisy) in results.items():
            if key not in merged:
                merged[key] = (value, unit, noisy)
                continue
            old = merged[key][0]
            better = value > old if unit.endswith("/s") else value < old
            if better:
                merged[key] = (value, unit, noisy)
    return data, merged


def main():
    parser = argparse.ArgumentParser(description="ZaminBench natijalarini boshlang'ich natija bilan solishtirish")
    parser.add_argument("baseline")
    parser.add_argument("current", nargs="+", help="yangi natija(lar); bir nechta bo'lsa har raqamning eng yaxshisi")
    parser.add_argument("--tolerance", type=float, default=25.0, help="ruxsat etilgan sekinlashish, foiz (standart 25)")
    parser.add_argument("--min-abs", type=float, default=None, help="bundan kichik farq hisobga olinmaydi (o'z birligida; standart birlikka qarab)")
    parser.add_argument("--markdown", help="jadvalni Markdown faylga ham yozish")
    args = parser.parse_args()

    base_data, base = load(args.baseline)
    cur_data, cur = best_of([load(path) for path in args.current])
    notes = []
    if base_data.get("Build") != cur_data.get("Build"):
        notes.append(f"build turlari farq qiladi: {base_data.get('Build')} va {cur_data.get('Build')} - raqamlar solishtirilmaydi")
    for field in ("Entities", "Instances", "Frames"):
        if field in base_data and field in cur_data and base_data[field] != cur_data[field]:
            notes.append(f"{field}: {base_data[field]} va {cur_data[field]} - o'lchamlar farq qiladi")

    rows, regressions, faster = [], [], []
    for key in base:
        if key not in cur:
            continue
        old, unit, noisy = base[key]
        new = cur[key][0]
        change = (new - old) / old * 100.0 if old > 0 else 0.0
        higher_better = unit.endswith("/s")
        worse = -change if higher_better else change     # > 0: got worse
        floor = args.min_abs if args.min_abs is not None else MIN_ABS.get(unit, 0.0)
        mark = ""
        if noisy:
            mark = "(I/O, hisobga olinmaydi)" if abs(worse) > args.tolerance else ""
        elif worse > args.tolerance and abs(new - old) > floor:
            mark = "SEKINLASHDI"
            regressions.append(key)
        elif worse < -args.tolerance and abs(new - old) > floor:
            mark = "tezlashdi"
            faster.append(key)
        rows.append((key, old, new, unit, change, mark))
    only_base = [k for k in base if k not in cur]
    only_cur = [k for k in cur if k not in base]

    width = max([len(r[0]) for r in rows] + [10])
    floor_text = f"{args.min_abs:g}" if args.min_abs is not None else "birlikka qarab"
    current = args.current[0] if len(args.current) == 1 else f"{', '.join(args.current)} (har raqamning eng yaxshisi)"
    print(f"Boshlang'ich: {args.baseline}\nYangi:        {current}\nChegara: {args.tolerance:g} % (va farq > {floor_text})\n")
    for note in notes:
        print(f"Diqqat: {note}")
    print(f"{'Kalit':<{width}}  {'avval':>12}  {'hozir':>12}  {'birlik':<7}  {'farq':>8}")
    for key, old, new, unit, change, mark in rows:
        print(f"{key:<{width}}  {old:12.3f}  {new:12.3f}  {unit:<7}  {change:+7.1f}%  {mark}")
    if only_base:
        print(f"\nFaqat boshlang'ichda: {', '.join(only_base)}")
    if only_cur:
        print(f"Faqat yangisida: {', '.join(only_cur)}")
    print(f"\n{len(rows)} raqam solishtirildi: {len(regressions)} sekinlashdi, {len(faster)} tezlashdi")

    if args.markdown:
        with open(args.markdown, "w", encoding="utf-8") as f:
            f.write(f"| Kalit | avval | hozir | birlik | farq | |\n|---|---:|---:|---|---:|---|\n")
            for key, old, new, unit, change, mark in rows:
                f.write(f"| {key} | {old:.3f} | {new:.3f} | {unit} | {change:+.1f}% | {mark} |\n")
            f.write(f"\n{len(rows)} raqam: {len(regressions)} sekinlashdi, {len(faster)} tezlashdi\n")
    return 1 if regressions else 0


if __name__ == "__main__":
    sys.exit(main())
