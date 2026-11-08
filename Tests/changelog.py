#!/usr/bin/env python3
"""The git history grouped by milestone (M01 ... M25 of docs/TASKS.md) - the source of CHANGELOG.md.

    Tests/changelog.py                   one line per milestone: commits, first / last commit, dates (the line
                                         CHANGELOG.md has under each milestone heading)
    Tests/changelog.py --commits M05     that milestone's commits (hash, date, subject) - to write its summary
    Tests/changelog.py --check           CHANGELOG.md matches the history (also run by Tests/docs_check.py)

Grouping: a commit whose subject starts with "Mnn" belongs to milestone Mnn; a commit without one belongs to the
milestone of the commit before it. The commits before the 500-task plan ("500 ta vazifa rejasi ...", which starts
M01) are "M00": the first version of the engine. Standard library only.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAN_SUBJECT = "500 ta vazifa rejasi"


def git_log():
    """[(hash7, date, subject)] oldest first; None when there is no full git history (a source archive, or a shallow
    clone such as actions/checkout's default)."""
    try:
        shallow = subprocess.run(["git", "rev-parse", "--is-shallow-repository"], cwd=ROOT, capture_output=True,
                                 text=True, check=True).stdout.strip()
        if shallow == "true":
            return None
        out = subprocess.run(["git", "log", "--reverse", "--format=%h%x09%ad%x09%s", "--date=short", "--abbrev=7"],
                             cwd=ROOT, capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    commits = []
    for line in out.splitlines():
        parts = line.split("\t", 2)
        if len(parts) == 3:
            commits.append((parts[0][:7], parts[1], parts[2]))
    return commits


def milestone_names():
    """{"M01": "Skript va gameplay API", ...} from docs/TASKS.md, and the milestones with a finished task."""
    names, done = {}, set()
    current = None
    with open(os.path.join(ROOT, "docs", "TASKS.md"), encoding="utf-8") as fh:
        for line in fh:
            m = re.match(r"^## (M\d\d) (.+?)\s*$", line)
            if m:
                current = m.group(1)
                names[current] = m.group(2)
            elif current and re.match(r"^- \[x\] \d{3}\.", line):
                done.add(current)
    return names, done


def group(commits):
    """[(milestone, [commits])] in history order."""
    groups = []
    current = "M00"
    for commit in commits:
        subject = commit[2]
        m = re.match(r"^(M\d\d)\b", subject)
        if m:
            current = m.group(1)
        elif subject.startswith(PLAN_SUBJECT):
            current = "M01"
        if not groups or groups[-1][0] != current:
            groups.append((current, []))
        groups[-1][1].append(commit)
    # A milestone's commits are one run; merge if one ever comes back later (kept in history order).
    merged = {}
    order = []
    for key, items in groups:
        if key not in merged:
            merged[key] = []
            order.append(key)
        merged[key].extend(items)
    return [(key, merged[key]) for key in order]


def task_range(key):
    n = int(key[1:])
    return None if n == 0 else (f"{(n - 1) * 20 + 1:03d}", f"{n * 20:03d}")


def summary_line(key, items, newest):
    """The line CHANGELOG.md has under the milestone's heading."""
    dates = items[0][1] if items[0][1] == items[-1][1] else f"{items[0][1]} – {items[-1][1]}"
    tasks = task_range(key)
    head = f"{dates} · vazifalar {tasks[0]}–{tasks[1]} · " if tasks else f"{dates} · "
    if newest:
        return head + f"`{items[0][0]}` dan boshlab (so'nggi milestone)"
    return head + f"{len(items)} ta commit (`{items[0][0]}` … `{items[-1][0]}`)"


def check(path, out):
    """Problems of CHANGELOG.md against the history: appended to `out`; returns a note."""
    commits = git_log()
    if commits is None:
        return "CHANGELOG: to'liq git tarixi yo'q (arxiv yoki shallow clone), tekshirilmadi"
    if not os.path.exists(path):
        out.append("CHANGELOG.md yo'q")
        return ""
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    groups = group(commits)
    names, done = milestone_names()
    newest = groups[-1][0]
    sections = {}
    lines = text.splitlines()
    for i, line in enumerate(lines):
        m = re.match(r"^## (M\d\d)\b(.*)$", line)
        if not m:
            continue
        body = next((l for l in lines[i + 1:] if l.strip()), "")
        sections[m.group(1)] = (i + 1, m.group(2).strip(), body.strip())
    for key, items in groups:
        if key not in sections:
            out.append(f"CHANGELOG.md: '## {key}' bo'limi yo'q ({len(items)} ta commit) - Tests/changelog.py --commits {key}")
            continue
        lineno, title, body = sections[key]
        if key in names and names[key] not in title:
            out.append(f"CHANGELOG.md:{lineno}: {key} sarlavhasida milestone nomi yo'q: '{names[key]}'")
        expected = summary_line(key, items, key == newest)
        if key == newest:
            # The newest milestone grows with every commit (this file's own too): only where it starts is checked.
            if f"`{items[0][0]}`" not in body:
                out.append(f"CHANGELOG.md:{lineno + 1}: {key} birinchi commit'i `{items[0][0]}` emas; kutilgan: {expected}")
        elif body != expected:
            out.append(f"CHANGELOG.md:{lineno + 1}: {key} qatori tarixga mos emas; kutilgan: {expected}")
    for key in sorted(done):
        if key not in sections:
            out.append(f"CHANGELOG.md: {key} ({names.get(key, '')}) vazifalari bajarilgan, lekin bo'limi yo'q")
    known = {c[0] for c in commits}
    for m in re.finditer(r"`([0-9a-f]{7})`", text):
        if m.group(1) not in known:
            out.append(f"CHANGELOG.md: commit topilmadi: {m.group(1)}")
    return f"CHANGELOG: {len(groups)} ta milestone, {len(commits)} ta commit"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--commits", metavar="MNN", help="shu milestone'ning commit'lari")
    ap.add_argument("--check", action="store_true", help="CHANGELOG.md tarixga mosligini tekshirish")
    args = ap.parse_args()
    commits = git_log()
    if args.check:
        problems = []
        note = check(os.path.join(ROOT, "CHANGELOG.md"), problems)
        for p in problems:
            print(p)
        print(note)
        return 1 if problems else 0
    if commits is None:
        print("to'liq git tarixi topilmadi (arxiv yoki shallow clone)", file=sys.stderr)
        return 2
    groups = group(commits)
    if args.commits:
        key = args.commits.upper()
        for gkey, items in groups:
            if gkey == key:
                for h, date, subject in items:
                    print(f"{h} {date} {subject}")
                return 0
        print(f"{key}: commit'lar yo'q", file=sys.stderr)
        return 1
    names, _ = milestone_names()
    for key, items in groups:
        title = names.get(key, "Dastlabki versiya" if key == "M00" else "")
        print(f"## {key} {title}\n{summary_line(key, items, key == groups[-1][0])}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
