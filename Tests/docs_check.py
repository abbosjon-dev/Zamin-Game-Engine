#!/usr/bin/env python3
"""Documentation consistency check (ctest: DocsCheck). Reads the sources and the Markdown files; nothing is built.

    Tests/docs_check.py            every check; exit code 1 when one fails (each problem on its own line)
    Tests/docs_check.py --verbose  also what was checked

Checks:
  1. links    - every relative Markdown link and image in README.md, docs/*.md and the other tracked .md files points to
                an existing file; a "#anchor" into a Markdown file names one of its headings (GitHub's anchors).
  2. components - every component of AllComponents (Zamin/src/Zamin/Scene/Components.h) has its section in
                docs/Components.md and the generated blocks there are up to date (Tests/docs_components.py).
  3. scripting - every public function of ScriptableEntity (Zamin/src/Zamin/Scripting/ScriptableEntity.h) is named in
                docs/Scripting.md.
  4. player   - every command line option of the player's --help (Zamin/src/Zamin/Project/PlayerLaunch.cpp,
                PlayerCommandLine::GetHelpText) is in the player / export documentation (docs/Runtime.md,
                docs/BuildAndExport.md when it exists).
Standard library only.
"""
import argparse
import os
import re
import subprocess
import sys
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True  # no __pycache__ next to the scripts
sys.path.insert(0, os.path.join(ROOT, "Tests"))
import docs_components  # noqa: E402

problems = []
notes = []


def problem(text):
    problems.append(text)


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def rel(path):
    return os.path.relpath(path, ROOT)


# ------------------------------------------------------------------ 1. links
def markdown_files():
    try:
        out = subprocess.run(["git", "ls-files", "*.md", "**/*.md"], cwd=ROOT, capture_output=True, text=True, check=True).stdout
        files = sorted({os.path.join(ROOT, f) for f in out.split() if f})
    except (OSError, subprocess.CalledProcessError):
        files = []
    # New files not added to git yet count too (docs/ and the root).
    for folder in (ROOT, os.path.join(ROOT, "docs")):
        for name in os.listdir(folder):
            if name.endswith(".md"):
                files.append(os.path.join(folder, name))
    files = sorted({f for f in files if os.path.isfile(f) and not rel(f).startswith(("build", "vendor"))})
    return files


def strip_fences(text):
    """Fenced code blocks (their lines kept as empty lines)."""
    return re.sub(r"^```.*?^```", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S | re.M)


def strip_code(text):
    """Fenced code blocks and inline code are not links."""
    return re.sub(r"`[^`\n]*`", "", strip_fences(text))


def github_slug(heading):
    """GitHub's heading anchor: lower case, markup and punctuation dropped (letters of any script, digits, '-' and
    '_' kept), spaces turned into '-'."""
    text = heading.strip().lower()
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    out = []
    for ch in text:
        if ch == " ":
            out.append("-")
        elif ch in "-_":
            out.append(ch)
        elif unicodedata.category(ch)[0] in ("L", "N"):
            out.append(ch)
    return "".join(out)


_anchor_cache = {}


def anchors(path):
    if path not in _anchor_cache:
        found, counts = set(), {}
        # Headings keep their inline code: GitHub's anchor has its text (github_slug drops the backticks).
        for line in strip_fences(read(path)).splitlines():
            m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", line)
            if not m:
                continue
            slug = github_slug(m.group(2))
            n = counts.get(slug, 0)
            counts[slug] = n + 1
            found.add(slug if n == 0 else f"{slug}-{n}")
        for m in re.finditer(r'<a\s+(?:name|id)="([^"]+)"', read(path)):
            found.add(m.group(1))
        _anchor_cache[path] = found
    return _anchor_cache[path]


_link = re.compile(r"!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\s*\)")
_html_src = re.compile(r"<img[^>]+src=\"([^\"]+)\"", re.I)


def check_links():
    count = 0
    for md in markdown_files():
        text = strip_code(read(md))
        targets = [m.group(1) for m in _link.finditer(text)] + [m.group(1) for m in _html_src.finditer(text)]
        for target in targets:
            if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I):  # http:, https:, mailto: ...
                continue
            count += 1
            path, _, anchor = target.partition("#")
            dest = md if not path else os.path.normpath(os.path.join(os.path.dirname(md), path))
            if path and not os.path.exists(dest):
                problem(f"{rel(md)}: havola topilmadi: {target}")
                continue
            if anchor and dest.endswith(".md") and os.path.isfile(dest) and anchor not in anchors(dest):
                problem(f"{rel(md)}: '{rel(dest)}' da '#{anchor}' sarlavhasi yo'q")
    notes.append(f"havolalar: {count} ta, {len(markdown_files())} ta Markdown fayl")


# ------------------------------------------------------------------ 2. components
def check_components():
    doc_path = os.path.join(ROOT, "docs", "Components.md")
    if not os.path.exists(doc_path):
        problem("docs/Components.md yo'q")
        return
    names, blocks = docs_components.generate()
    doc = read(doc_path)
    new, missing = docs_components.apply(doc, blocks)
    for name in missing:
        problem(f"docs/Components.md: {name} hujjatlanmagan (bo'lim va <!-- gen:{name} --> bloki yo'q)")
    for name in names:
        if not re.search(r"^###\s+" + re.escape(name) + r"\s*$", doc, re.M):
            problem(f"docs/Components.md: '### {name}' sarlavhasi yo'q")
    if not missing and new != doc:
        problem("docs/Components.md: yaratilgan bloklar eskirgan - Tests/docs_components.py --update docs/Components.md")
    notes.append(f"komponentlar: {len(names)} ta")


# ------------------------------------------------------------------ 3. scripting API
def public_script_functions():
    text = read(os.path.join(ROOT, "Zamin", "src", "Zamin", "Scripting", "ScriptableEntity.h"))
    start = text.index("    class ScriptableEntity\n")
    body = text[start:]
    body = body[:body.index("\n    private:")]
    names = []
    for line in body.splitlines():
        # The class's own declarations are indented by exactly 8 spaces (inline bodies are deeper).
        if not line.startswith("        ") or line.startswith("         "):
            continue
        code = line.split("//")[0].strip()
        code = re.sub(r"^template\s*<[^>]*>\s*", "", code)
        if not code or code.startswith(("public:", "private:", "friend", "using", "{", "}")):
            continue
        m = re.match(r"^(?:virtual\s+|static\s+|inline\s+|explicit\s+)*[\w:<>,\s\*&]+?[\s\*&](~?\w+)\s*\(", code)
        if m and m.group(1) not in ("ScriptableEntity", "~ScriptableEntity", "operator"):
            if m.group(1) not in names:
                names.append(m.group(1))
    return names


def check_scripting():
    doc_path = os.path.join(ROOT, "docs", "Scripting.md")
    if not os.path.exists(doc_path):
        problem("docs/Scripting.md yo'q")
        return
    doc = read(doc_path)
    names = public_script_functions()
    if len(names) < 100:
        problem(f"ScriptableEntity.h: ommaviy funksiyalar ro'yxati o'qilmadi ({len(names)} ta)")
    for name in names:
        if not re.search(r"(?<![\w])" + re.escape(name) + r"(?![\w])", doc):
            problem(f"docs/Scripting.md: ScriptableEntity::{name} tilga olinmagan")
    notes.append(f"ScriptableEntity: {len(names)} ta ommaviy funksiya")


# ------------------------------------------------------------------ 4. player command line
def check_player_flags():
    source = read(os.path.join(ROOT, "Zamin", "src", "Zamin", "Project", "PlayerLaunch.cpp"))
    a = source.find("std::string PlayerCommandLine::GetHelpText")
    if a < 0:
        problem("PlayerLaunch.cpp: PlayerCommandLine::GetHelpText topilmadi")
        return
    b = source.find("\n    }\n", a)
    flags = sorted(set(re.findall(r"(?<![\w-])(--[a-z][a-z0-9-]*|-[hv])\b", source[a:b])))
    docs = [os.path.join(ROOT, "docs", n) for n in ("Runtime.md", "BuildAndExport.md")]
    text = "\n".join(read(p) for p in docs if os.path.exists(p))
    for flag in flags:
        if not re.search(r"(?<![\w-])" + re.escape(flag) + r"(?![\w-])", text):
            problem(f"player variantining hujjati yo'q: {flag} (docs/Runtime.md yoki docs/BuildAndExport.md)")
    notes.append(f"player variantlari: {len(flags)} ta")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()
    check_links()
    check_components()
    check_scripting()
    check_player_flags()
    for p in problems:
        print(p)
    if args.verbose or not problems:
        for n in notes:
            print(n)
    print(f"Hujjatlar tekshiruvi: {len(problems)} muammo")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
