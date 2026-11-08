#!/usr/bin/env python3
"""The C++ script classes of the documentation compile (ctest: DocsSnippets).

    Tests/docs_snippets.py [--build build] [--verbose] [--keep]

Every ```cpp block of README.md and docs/*.md that holds a whole script class (`class X : public ScriptableEntity`
... `};`) is compiled against the engine's headers with -fsyntax-only, with the compiler and flags the build uses for
the sample projects' scripts (from <build>/compile_commands.json). All the blocks go into one translation unit
(#line keeps every error at its Markdown file and line); each block's class names get a suffix so that two blocks
may show the same class. Blocks that are
only fragments (no whole class) are left out - docs/*.md keep them short on purpose.

Exit code 1 when a block does not compile or warns (the compiler's messages with the Markdown file and line), 0 when
all compile or the build has no compile_commands.json yet (nothing to compare with - said so). Standard library only.
"""
import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True

_block = re.compile(r"^```cpp\n(.*?)^```", re.S | re.M)
_class = re.compile(r"^class (\w+) : public ScriptableEntity\b", re.M)


def markdown_files():
    files = [os.path.join(ROOT, "README.md")]
    docs = os.path.join(ROOT, "docs")
    files += sorted(os.path.join(docs, f) for f in os.listdir(docs) if f.endswith(".md"))
    return [f for f in files if os.path.isfile(f)]


def script_blocks(path):
    """(line, code) of the blocks with a whole script class."""
    text = open(path, encoding="utf-8").read()
    out = []
    for m in _block.finditer(text):
        code = m.group(1)
        if not _class.search(code) or not re.search(r"^};", code, re.M):
            continue
        line = text.count("\n", 0, m.start()) + 2
        out.append((line, code))
    return out


def rename(code, suffix):
    """The block's own classes get `suffix` (in their declaration, ZM_REGISTER_SCRIPT and ZM_FIELD*)."""
    for name in set(_class.findall(code)):
        code = re.sub(rf"\b{name}\b", name + suffix, code)
    return code


def compile_flags(build):
    path = os.path.join(build, "compile_commands.json")
    if not os.path.isfile(path):
        return None
    for entry in json.load(open(path, encoding="utf-8")):
        file = entry.get("file", "").replace("\\", "/")
        if "/Projects/" in file and "/Source/" in file and file.endswith(".cpp"):
            args = entry.get("arguments") or shlex.split(entry["command"])
            flags, skip = [], False
            for a in args:
                if skip:
                    skip = False
                    continue
                if a in ("-o", "-c", "-MF", "-MT", "-MQ"):
                    skip = True
                    continue
                if a == file or a.endswith(".cpp") or a in ("-g", "-MD", "-MMD"):
                    continue
                flags.append(a)
            return flags, entry.get("directory", build)
    return None


def main():
    parser = argparse.ArgumentParser(description="Documentation C++ script classes compile")
    parser.add_argument("--build", default=os.path.join(ROOT, "build"))
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--keep", action="store_true", help="keep the generated translation units")
    args = parser.parse_args()

    found = compile_flags(os.path.abspath(args.build))
    if not found:
        print(f"Hujjat kodlari tekshiruvi o'tkazib yuborildi: {args.build}/compile_commands.json yo'q (avval build qiling)")
        return 0
    flags, directory = found

    # One translation unit for every file (the engine's headers are read once): the #line directives keep each
    # error at its Markdown file and line.
    parts = ["#include <Zamin/Zamin.h>", "#include <algorithm>", "#include <cmath>", "#include <format>",
             "#include <string>", "#include <vector>", "using namespace Zamin;"]
    total, files = 0, []
    for f, md in enumerate(markdown_files()):
        blocks = script_blocks(md)
        if not blocks:
            continue
        rel = os.path.relpath(md, ROOT)
        files.append(rel)
        for i, (line, code) in enumerate(blocks):
            parts.append(f'#line {line} "{rel}"')
            parts.append(rename(code, f"_Doc{f}_{i}"))
            total += 1
            if args.verbose:
                print(f"  {rel}:{line}: {', '.join(sorted(set(_class.findall(code))))}")
    source_text = "\n".join(parts) + "\n"
    with tempfile.TemporaryDirectory(prefix="zamin-docs-") as tmp:
        source = os.path.join(tmp, "docs_snippets.cpp")
        with open(source, "w", encoding="utf-8") as fh:
            fh.write(source_text)
        if args.keep:
            os.makedirs(args.build, exist_ok=True)
            with open(os.path.join(args.build, "docs_snippets.cpp"), "w", encoding="utf-8") as fh:
                fh.write(source_text)
        result = subprocess.run(flags + ["-fsyntax-only", source], cwd=directory, capture_output=True, text=True)
    # Warnings count too: the samples build warning-free with the same flags, so do their documented copies.
    failures = 0
    if result.returncode != 0 or "warning:" in result.stderr:
        failures = len(re.findall(r"(?:error|warning):", result.stderr)) or 1
        print("Hujjatlardagi skript klasslari kompilyatsiya bo'lmadi:")
        print(result.stderr.strip())
    print(f"Hujjat kodlari: {total} ta skript klassi, {len(files)} ta fayl, {failures} ta xato")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
