#!/usr/bin/env python3
"""Code coverage report per module from gcov's data (standard library only).

A ZAMIN_COVERAGE=ON build (cmake/Coverage.cmake) leaves a .gcda file next to every object file of our targets once
an instrumented program has run. This script runs gcov on all of them (--json-format, in parallel), joins the
results of every translation unit (a header used by many .cpp files counts once: a line is covered when any of them
ran it), keeps the files of this repository (not the vendor libraries, not the build folders) and sums them up:

  - per module: the engine's folders (Zamin/Core, Zamin/Scene, Zamin/Renderer ...), Editor, Editor/Panels, Player,
    the sample games (Namuna/<project>); the tests' own code only with --include-tests;
  - lines and functions: how many there are, how many ran, the percentage;
  - the files with the most lines that never ran (--top N).

    Tests/coverage_report.py build-cov --out build-cov/Coverage [--gcov gcov] [--jobs 4] [--min-engine 50]

Writes <out>/coverage-summary.md (a Markdown table), <out>/coverage.json (modules, files, totals) and prints the
table. --min-engine X: the exit code is 1 when the engine's line coverage is below X percent. Tests/coverage.sh
builds, runs the tests and calls this.
"""
import argparse
import concurrent.futures
import json
import os
import shlex
import subprocess
import sys
from collections import defaultdict

ENGINE_PREFIX = "Zamin/src/Zamin/"


def find_gcda(build):
    found = []
    for dirpath, dirnames, filenames in os.walk(build):
        # The dependencies' own build folders are not ours.
        dirnames[:] = [d for d in dirnames if d != "_deps"]
        for name in filenames:
            if name.endswith(".gcda"):
                found.append(os.path.join(dirpath, name))
    return sorted(found)


def run_gcov(gcov, files):
    """gcov --json-format --stdout on some .gcda files: one JSON document per line."""
    cmd = shlex.split(gcov) + ["--json-format", "--stdout"] + files
    proc = subprocess.run(cmd, cwd=os.path.dirname(files[0]), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    docs = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("{"):
            try:
                docs.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return docs, proc.returncode, proc.stderr


def module_of(rel):
    """The module a repository file belongs to (None: not reported)."""
    parts = rel.split("/")
    if rel.startswith(ENGINE_PREFIX):
        rest = rel[len(ENGINE_PREFIX):].split("/")
        return "Zamin/" + (rest[0] if len(rest) > 1 else "(asosiy)")
    if rel.startswith("Editor/"):
        return "Editor/Panels" if len(parts) > 3 and parts[2] == "Panels" else "Editor"
    if rel.startswith("Runtime/"):
        return "Player"
    if rel.startswith("Projects/") and len(parts) > 2:
        return "Namuna/" + parts[1]
    if rel.startswith("Tests/"):
        return "Tests"
    return None


def pct(covered, total):
    return 100.0 * covered / total if total else 0.0


def main():
    parser = argparse.ArgumentParser(description="gcov ma'lumotlaridan modullar bo'yicha qamrov hisoboti")
    parser.add_argument("build", help="ZAMIN_COVERAGE=ON build papkasi (.gcda fayllari)")
    parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))), help="repozitoriya ildizi")
    parser.add_argument("--out", help="hisobot papkasi (standart: <build>/Coverage)")
    parser.add_argument("--gcov", default=os.environ.get("GCOV", "gcov"), help="gcov dasturi (Clang: \"llvm-cov gcov\")")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--top", type=int, default=15, help="eng ko'p bajarilmagan qatorli fayllar soni")
    parser.add_argument("--include-tests", action="store_true", help="testlarning o'z kodi ham hisobotga")
    parser.add_argument("--min-engine", type=float, default=0.0, help="engine qatorlar qamrovi bundan past bo'lsa chiqish kodi 1")
    args = parser.parse_args()

    root = os.path.realpath(args.root)
    build = os.path.realpath(args.build)
    out = os.path.realpath(args.out or os.path.join(build, "Coverage"))
    gcda = find_gcda(build)
    if not gcda:
        print(f"{build}: .gcda fayllari yo'q (ZAMIN_COVERAGE=ON build va testlar ishga tushirilganmi?)", file=sys.stderr)
        return 2

    # Chunks of files per gcov run (a run per file costs a process each; one run with all of them is not parallel).
    by_dir = defaultdict(list)
    for f in gcda:
        by_dir[os.path.dirname(f)].append(f)
    chunks = []
    for files in by_dir.values():
        for i in range(0, len(files), 16):
            chunks.append(files[i:i + 16])

    lines = defaultdict(dict)      # file -> {line: count}
    functions = defaultdict(dict)  # file -> {start line: (name, executed)}
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for docs, code, err in pool.map(lambda c: run_gcov(args.gcov, c), chunks):
            if code != 0 and not docs:
                failed += 1
                if failed <= 3:
                    print(f"gcov xatosi: {err.strip()[:300]}", file=sys.stderr)
            for doc in docs:
                cwd = doc.get("current_working_directory", build)
                for entry in doc.get("files", []):
                    path = entry.get("file", "")
                    if not os.path.isabs(path):
                        path = os.path.join(cwd, path)
                    path = os.path.realpath(path)
                    if not path.startswith(root + os.sep) or path.startswith(build + os.sep):
                        continue
                    rel = os.path.relpath(path, root).replace(os.sep, "/")
                    if rel.startswith("build") or rel.startswith("vendor/"):
                        continue
                    file_lines = lines[rel]
                    for ln in entry.get("lines", []):
                        n = ln.get("line_number")
                        file_lines[n] = file_lines.get(n, 0) + int(ln.get("count", 0))
                    file_funcs = functions[rel]
                    for fn in entry.get("functions", []):
                        start = fn.get("start_line")
                        ran = int(fn.get("execution_count", 0)) > 0
                        name = fn.get("demangled_name") or fn.get("name", "")
                        prev = file_funcs.get(start)
                        file_funcs[start] = (prev[0] if prev else name, ran or (prev[1] if prev else False))

    files = {}
    for rel, file_lines in lines.items():
        module = module_of(rel)
        if module is None or (module == "Tests" and not args.include_tests):
            continue
        total = len(file_lines)
        covered = sum(1 for c in file_lines.values() if c > 0)
        funcs = functions.get(rel, {})
        files[rel] = {
            "Module": module,
            "Lines": total,
            "CoveredLines": covered,
            "Functions": len(funcs),
            "CoveredFunctions": sum(1 for _, ran in funcs.values() if ran),
            "Uncovered": sorted(n for n, c in file_lines.items() if c == 0),
        }

    modules = defaultdict(lambda: {"Files": 0, "Lines": 0, "CoveredLines": 0, "Functions": 0, "CoveredFunctions": 0, "UntouchedFiles": 0})
    for rel, f in files.items():
        m = modules[f["Module"]]
        m["Files"] += 1
        for key in ("Lines", "CoveredLines", "Functions", "CoveredFunctions"):
            m[key] += f[key]
        if f["CoveredLines"] == 0 and f["Lines"] > 0:
            m["UntouchedFiles"] += 1

    def total_of(pred):
        t = {"Files": 0, "Lines": 0, "CoveredLines": 0, "Functions": 0, "CoveredFunctions": 0, "UntouchedFiles": 0}
        for name, m in modules.items():
            if pred(name):
                for key in t:
                    t[key] += m[key]
        return t

    totals = {
        "Engine": total_of(lambda n: n.startswith("Zamin/")),
        "Editor": total_of(lambda n: n.startswith("Editor")),
        "Player": total_of(lambda n: n == "Player"),
        "Samples": total_of(lambda n: n.startswith("Namuna/")),
        "All": total_of(lambda n: True),
    }

    def row(name, m):
        return (f"| {name} | {m['Files']} | {m['Lines']} | {m['CoveredLines']} | **{pct(m['CoveredLines'], m['Lines']):.1f} %** | "
                f"{m['Functions']} | {pct(m['CoveredFunctions'], m['Functions']):.1f} % | {m['UntouchedFiles']} |")

    md = ["# Kod qamrovi (gcov)", "",
          f"{len(gcda)} ta .gcda fayl, {len(files)} ta manba fayl (vendor kutubxonalar va build papkalari hisobga olinmaydi"
          f"{'' if args.include_tests else ', testlarning o`z kodi ham'}). "
          "Qator - gcov bajariladigan deb belgilagan qator; funksiya - manbadagi bitta funksiya (shablon nusxalari birga).", "",
          "| Modul | Fayllar | Qatorlar | Bajarilgan | Qatorlar % | Funksiyalar | Funksiyalar % | Umuman tegilmagan fayllar |",
          "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for name in sorted(modules):
        md.append(row(name, modules[name]))
    md.append("|  |  |  |  |  |  |  |  |")
    labels = {"Engine": "**Engine (Zamin) jami**", "Editor": "**Editor jami**", "Player": "**Player jami**",
              "Samples": "**Namuna o'yinlar jami**", "All": "**Hammasi**"}
    for key, label in labels.items():
        if totals[key]["Files"]:
            md.append(row(label, totals[key]))
    worst = sorted(files.items(), key=lambda kv: kv[1]["Lines"] - kv[1]["CoveredLines"], reverse=True)[:args.top]
    md += ["", f"## Eng ko'p bajarilmagan qatorli {len(worst)} fayl", "",
           "| Fayl | Modul | Qatorlar | Bajarilmagan | Qatorlar % |", "|---|---|---:|---:|---:|"]
    for rel, f in worst:
        md.append(f"| `{rel}` | {f['Module']} | {f['Lines']} | {f['Lines'] - f['CoveredLines']} | {pct(f['CoveredLines'], f['Lines']):.1f} % |")
    text = "\n".join(md) + "\n"

    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "coverage-summary.md"), "w", encoding="utf-8") as fh:
        fh.write(text)
    report = {
        "GcdaFiles": len(gcda),
        "Totals": {k: dict(v, LinePercent=round(pct(v["CoveredLines"], v["Lines"]), 2),
                           FunctionPercent=round(pct(v["CoveredFunctions"], v["Functions"]), 2)) for k, v in totals.items()},
        "Modules": {k: dict(v, LinePercent=round(pct(v["CoveredLines"], v["Lines"]), 2),
                            FunctionPercent=round(pct(v["CoveredFunctions"], v["Functions"]), 2)) for k, v in sorted(modules.items())},
        "Files": {k: {key: val for key, val in f.items() if key != "Uncovered"} | {"UncoveredLines": f["Uncovered"]}
                  for k, f in sorted(files.items())},
    }
    with open(os.path.join(out, "coverage.json"), "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1, ensure_ascii=False)
    print(text)
    print(f"Hisobot: {os.path.join(out, 'coverage-summary.md')}, {os.path.join(out, 'coverage.json')}")
    if failed:
        print(f"Diqqat: {failed} ta gcov ishga tushishi xato bilan tugadi", file=sys.stderr)

    engine = pct(totals["Engine"]["CoveredLines"], totals["Engine"]["Lines"])
    if args.min_engine > 0 and engine < args.min_engine:
        print(f"Engine qatorlar qamrovi {engine:.1f} % - chegaradan ({args.min_engine:g} %) past", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
