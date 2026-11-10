#!/usr/bin/env python3
"""The unit tests' catalogue (Tests/ZaminTests.cpp): every TEST(...) under the section comment it stands in
("// ---------- title" / "// ---- title"), with the count per section - the list in docs/Testing.md.

    Tests/test_catalog.py                       the catalogue as Markdown on stdout
    Tests/test_catalog.py --update docs/Testing.md   rewrite the part between the markers in that file
    Tests/test_catalog.py --check docs/Testing.md    exit code 1 when that part is out of date (a test added,
                                                    renamed or removed without --update)
The markers: "<!-- test-catalog:begin -->" and "<!-- test-catalog:end -->". Standard library only; it reads the
source, nothing has to be built.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "Tests", "ZaminTests.cpp")
BEGIN = "<!-- test-catalog:begin -->"
END = "<!-- test-catalog:end -->"

_section = re.compile(r"^// -{4,}\s+(.+?)\s*$")
_test = re.compile(r"^\s*TEST\((\w+)\)")


def catalogue():
    sections = []  # [title, [names]]
    cmake = open(os.path.join(ROOT, "Tests", "CMakeLists.txt"), encoding="utf-8").read()
    target = re.search(r"add_executable\(ZaminTests\s+([^)]*)\)", cmake).group(1)
    for filename in target.split():
        if not filename.endswith("Tests.cpp"):
            continue
        source = os.path.join(ROOT, "Tests", filename)
        title = os.path.basename(source)
        for line in open(source, encoding="utf-8"):
            m = _section.match(line)
            if m:
                text = m.group(1).rstrip(":").strip()
                # The framework's own banner lines are no sections of tests.
                if not text.lower().startswith(("mini framework", "test scripts", "tests")):
                    title = text
                elif text.lower().startswith("tests"):
                    title = "Asosiy (sahna, skriptlar, fizika, prefab, particle)"
                continue
            m = _test.match(line)
            if m:
                if not sections or sections[-1][0] != title:
                    sections.append([title, []])
                sections[-1][1].append(m.group(1))
    return sections


def markdown(sections):
    total = sum(len(n) for _, n in sections)
    out = [BEGIN,
           f"_`Tests/test_catalog.py --update docs/Testing.md` yaratgan: {total} ta unit test, {len(sections)} ta bo'lim "
           "(`ZaminTests` targetidagi fayllar tartibida). Bitta testni ishga tushirish: `./build/bin/ZaminTests <Nom>`._",
           "",
           "| # | Bo'lim | Testlar | Nomlari |",
           "|---|---|---|---|"]
    for i, (title, names) in enumerate(sections, 1):
        title = title.replace("|", "/")
        out.append(f"| {i} | {title} | {len(names)} | {', '.join(f'`{n}`' for n in names)} |")
    out.append(END)
    return "\n".join(out) + "\n"


def replace_block(text, block):
    a = text.find(BEGIN)
    b = text.find(END)
    if a < 0 or b < 0 or b < a:
        raise SystemExit(f"belgilar topilmadi: {BEGIN} ... {END}")
    return text[:a] + block.rstrip("\n") + text[b + len(END):]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--update")
    ap.add_argument("--check")
    args = ap.parse_args()
    block = markdown(catalogue())
    if args.update or args.check:
        path = args.update or args.check
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        new = replace_block(text, block)
        if args.check:
            if new != text:
                print(f"{path}: testlar ro'yxati eskirgan (Tests/test_catalog.py --update {path})", file=sys.stderr)
                return 1
            print(f"{path}: testlar ro'yxati yangi")
            return 0
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(new)
        print(f"{path}: yangilandi")
        return 0
    sys.stdout.write(block)
    return 0


if __name__ == "__main__":
    sys.exit(main())
