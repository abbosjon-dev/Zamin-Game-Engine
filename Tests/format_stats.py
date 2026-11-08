#!/usr/bin/env python3
"""How far the code base is from .clang-format: every tracked C++ file outside vendor/ is formatted in memory (the
files are not touched) and compared line by line with what is on disk.

Prints the changed lines per module and per kind of change (the conventions clang-format cannot express - several
statements on one line, one-line enums, hand-wrapped lists, hand-aligned columns - are counted apart), the files
that would change the most and how many files would not change at all.

    Tests/format_stats.py                    summary
    Tests/format_stats.py --top 20           and the 20 files that would change the most
    Tests/format_stats.py --show Zamin/src/Zamin/Scene/Entity.h   that file's diff
    Tests/format_stats.py --json out.json    the numbers as JSON too
    Tests/format_stats.py --max-percent 3.5  exit code 1 when more lines than that would change (a guard for
                                             edits of .clang-format)
Usually run through Tests/format_check.sh --stats (which picks the clang-format binary). Standard library only.
"""
import argparse
import collections
import difflib
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXTENSIONS = (".cpp", ".h", ".hpp", ".c", ".cc", ".inl")

KINDS = [
    ("multi-statement", "bir qatorda bir nechta ifoda (a = 1; b = 2;)"),
    ("enum", "bir qatorli qisqa enum"),
    ("list", "bir qatorda bir nechta element (enumerator, ro'yxat) - har biri o'z qatoriga"),
    ("braced-list", "qo'lda bo'lingan {...} ro'yxatlar"),
    ("case-align", "case qatorlarini qo'lda tekislash"),
    ("short-block", "bir qatorli { ...; ...; } bloklar"),
    ("ternary", "?: zanjirlari"),
    ("other", "boshqa (qatorlarni bo'lish / tekislash, chekinish)"),
]
_strings = re.compile(r'"(\\.|[^"\\])*"')
_multi = re.compile(r';\s*[^\s/}]')


def kind_of(old):
    """A rough kind for a changed hunk (by its original lines)."""
    lines = [_strings.sub('""', l) for l in old]
    code = [re.sub(r'\bfor \(.*\)', 'for ()', l) for l in lines]
    if any(re.search(r'\benum\b.*\{.*\}', l) for l in lines):
        return "enum"
    if any(re.match(r'^\s*([^,;(){}]+,\s*){2,}', l) for l in lines):
        return "list"
    if any(_multi.search(l) for l in code):
        return "multi-statement" if not any(re.search(r'\{[^{}]*;[^{}]*;[^{}]*\}', l) for l in code) else "short-block"
    if any(re.search(r'\b(case\s.*|default):', l) for l in lines):
        return "case-align"
    if any(re.search(r'\{[^{}]*\}\s*,', l) or re.match(r'^\s*\{ ', l) for l in lines):
        return "braced-list"
    if any(" ? " in l or l.strip().startswith(": ") for l in lines):
        return "ternary"
    return "other"


def module_of(path):
    parts = path.split("/")
    if parts[0] == "Zamin" and len(parts) > 3:
        return "Zamin/" + parts[3] if len(parts) > 4 else "Zamin"
    if parts[0] == "Editor":
        return "Editor/Panels" if len(parts) > 3 and parts[2] == "Panels" else "Editor"
    if parts[0] == "Projects":
        return "Projects/" + parts[1]
    return parts[0]


def tracked_files():
    out = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True, check=True).stdout
    return [f for f in out.splitlines() if f.endswith(EXTENSIONS) and not f.startswith("vendor/")]


def format_file(cf, path):
    full = os.path.join(ROOT, path)
    with open(full, encoding="utf-8", errors="replace") as fh:
        src = fh.read().splitlines()
    res = subprocess.run([cf, "--style=file", f"--assume-filename={full}", full], capture_output=True, text=True, errors="replace")
    if res.returncode != 0:
        raise RuntimeError(f"{cf} {path}: {res.stderr.strip()[:300]}")
    return src, res.stdout.splitlines()


def measure(cf, path):
    src, new = format_file(cf, path)
    hunks = []
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, src, new, autojunk=False).get_opcodes():
        if tag != "equal":
            hunks.append((kind_of(src[i1:i2]) if i2 > i1 else "other", max(i2 - i1, j2 - j1)))
    return path, len(src), hunks


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--clang-format", default=os.environ.get("CLANG_FORMAT") or shutil.which("clang-format-18") or "clang-format")
    ap.add_argument("--top", type=int, default=0)
    ap.add_argument("--json")
    ap.add_argument("--max-percent", type=float)
    ap.add_argument("--show")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    if args.show:
        src, new = format_file(args.clang_format, args.show)
        sys.stdout.writelines(l + "\n" for l in difflib.unified_diff(src, new, "a/" + args.show, "b/" + args.show, lineterm=""))
        return 0

    files = tracked_files()
    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        results = list(ex.map(lambda f: measure(args.clang_format, f), files))

    total = sum(r[1] for r in results)
    changed = sum(n for r in results for _, n in r[2])
    unchanged = sum(1 for r in results if not r[2])
    kinds = collections.Counter()
    modules = collections.defaultdict(lambda: [0, 0, 0, 0])  # files, lines, changed, unchanged files
    for path, lines, hunks in results:
        m = modules[module_of(path)]
        m[0] += 1; m[1] += lines; m[2] += sum(n for _, n in hunks); m[3] += 0 if hunks else 1
        for k, n in hunks:
            kinds[k] += n

    version = subprocess.run([args.clang_format, "--version"], capture_output=True, text=True).stdout.strip()
    pct = 100.0 * changed / max(total, 1)
    print(f".clang-format va mavjud kod ({version})")
    print(f"  {len(files)} ta fayl, {total} qator: {changed} qator o'zgarardi ({pct:.2f} %), {unchanged} ta fayl o'zgarmasdi")
    print("\n  O'zgarish turi bo'yicha:")
    for k, label in KINDS:
        if kinds[k]:
            print(f"    {kinds[k]:6d}  {100.0 * kinds[k] / max(changed, 1):5.1f} %  {k:16s} {label}")
    print("\n  Modul bo'yicha:")
    print(f"    {'modul':22s} {'fayl':>5s} {'qator':>7s} {'o`zgarardi':>11s} {'%':>6s} {'o`zgarmas fayl':>15s}")
    for name, (nf, nl, nc, nu) in sorted(modules.items(), key=lambda kv: -kv[1][1]):
        print(f"    {name:22s} {nf:5d} {nl:7d} {nc:11d} {100.0 * nc / max(nl, 1):6.2f} {nu:15d}")
    if args.top:
        print(f"\n  Eng ko'p o'zgaradigan {args.top} ta fayl:")
        for path, lines, hunks in sorted(results, key=lambda r: -sum(n for _, n in r[2]))[:args.top]:
            n = sum(c for _, c in hunks)
            if n:
                top_kind = collections.Counter()
                for k, c in hunks:
                    top_kind[k] += c
                print(f"    {n:6d} / {lines:6d}  {path}  ({', '.join(f'{k} {c}' for k, c in top_kind.most_common(3))})")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump({
                "ClangFormat": version, "Files": len(files), "Lines": total, "ChangedLines": changed,
                "ChangedPercent": round(pct, 3), "UnchangedFiles": unchanged,
                "Kinds": dict(kinds), "Modules": {k: {"Files": v[0], "Lines": v[1], "Changed": v[2], "UnchangedFiles": v[3]} for k, v in modules.items()},
            }, fh, indent=2)
    if args.max_percent is not None and pct > args.max_percent:
        print(f"\nXATO: {pct:.2f} % > --max-percent {args.max_percent}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
