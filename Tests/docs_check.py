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
  5. messages - every log / UI message quoted in docs/FAQ.md (a blockquote that is one code span: > `...`) is a string
                of the sources (Zamin/src, Editor/src, Runtime/src), "<...>" standing for a formatted value ({}, %s).
  6. build    - every ZAMIN_* CMake option / cache variable (CMakeLists.txt, cmake/*.cmake, Zamin/CMakeLists.txt) is in
                docs/BuildAndExport.md, and the Doxyfile's PROJECT_NUMBER is the engine's version (project(Zamin VERSION)).
  7. catalog  - the unit test list in docs/Testing.md is up to date (Tests/test_catalog.py).
  8. changelog - CHANGELOG.md has a section for every milestone of the git history and every milestone with a finished
                task, its summary line matches the history, every commit it names exists (Tests/changelog.py).
  9. index    - docs/README.md links every document of docs/ and CONTRIBUTING.md, CHANGELOG.md.
 10. images   - every file of docs/images is used by a Markdown file, is at most 400 KB and at most 1280 px wide.
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
import changelog  # noqa: E402
import docs_components  # noqa: E402
import test_catalog  # noqa: E402

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


# ------------------------------------------------------------------ 5. quoted messages
def source_strings():
    """Every string literal of the engine, the editor and the player; adjacent literals ("a" "b") joined."""
    literal = r'"(?:[^"\\\n]|\\.)*"'
    joined = re.compile(literal + r'(?:\s*' + literal + r')*')
    out = []
    for base in ("Zamin/src", "Editor/src", "Runtime/src"):
        for dirpath, _, files in os.walk(os.path.join(ROOT, base)):
            for name in files:
                if not name.endswith((".cpp", ".h")):
                    continue
                text = read(os.path.join(dirpath, name))
                for m in joined.finditer(text):
                    parts = re.findall(literal, m.group(0))
                    out.append("".join(p[1:-1] for p in parts))
    return out


def check_faq_messages():
    doc_path = os.path.join(ROOT, "docs", "FAQ.md")
    if not os.path.exists(doc_path):
        return
    strings = source_strings()
    count = 0
    for lineno, line in enumerate(read(doc_path).splitlines(), 1):
        m = re.match(r"^>\s*`([^`]+)`\s*$", line)
        if not m:
            continue
        count += 1
        message = m.group(1)
        # "<...>": a value the program formats in ({}, {:.1f}, {0}, %s, %zu) - or the same text literally.
        pattern = ""
        pos = 0
        for ph in re.finditer(r"<[^<>]+>", message):
            pattern += re.escape(message[pos:ph.start()])
            pattern += r"(?:\{[^{}]*\}|%[-+ #0-9.]*[a-z]+|" + re.escape(ph.group(0)) + ")"
            pos = ph.end()
        pattern += re.escape(message[pos:])
        rx = re.compile(pattern)
        if not any(rx.search(s) for s in strings):
            problem(f"docs/FAQ.md:{lineno}: xabar manba kodda topilmadi: {message}")
    notes.append(f"FAQ xabarlari: {count} ta")


# ------------------------------------------------------------------ 6. build options
def check_build_options():
    doc_path = os.path.join(ROOT, "docs", "BuildAndExport.md")
    if not os.path.exists(doc_path):
        return
    doc = read(doc_path)
    files = [os.path.join(ROOT, "CMakeLists.txt"), os.path.join(ROOT, "Zamin", "CMakeLists.txt")]
    cmake_dir = os.path.join(ROOT, "cmake")
    files += [os.path.join(cmake_dir, n) for n in sorted(os.listdir(cmake_dir)) if n.endswith(".cmake")]
    options = []
    for path in files:
        text = read(path)
        for m in re.finditer(r"^\s*option\(\s*(ZAMIN_\w+)|^\s*set\(\s*(ZAMIN_\w+)[^)]*\bCACHE\b", text, re.M):
            name = m.group(1) or m.group(2)
            if name not in options:
                options.append(name)
    for name in options:
        if not re.search(r"(?<![\w])" + re.escape(name) + r"(?![\w])", doc):
            problem(f"docs/BuildAndExport.md: CMake varianti hujjatlanmagan: {name}")
    version = re.search(r"project\(\s*Zamin\s+VERSION\s+([0-9.]+)", read(os.path.join(ROOT, "CMakeLists.txt")))
    doxyfile = os.path.join(ROOT, "Doxyfile")
    if os.path.exists(doxyfile) and version:
        number = re.search(r"^PROJECT_NUMBER\s*=\s*(\S+)", read(doxyfile), re.M)
        if not number or number.group(1) != version.group(1):
            problem(f"Doxyfile: PROJECT_NUMBER ({number.group(1) if number else '-'}) dvigatel versiyasi emas ({version.group(1)})")
    notes.append(f"CMake variantlari: {len(options)} ta")


# ------------------------------------------------------------------ 7. unit test catalogue
def check_test_catalog():
    doc_path = os.path.join(ROOT, "docs", "Testing.md")
    text = read(doc_path)
    if test_catalog.BEGIN not in text:
        problem("docs/Testing.md: unit testlar katalogi belgilari yo'q")
        return
    sections = test_catalog.catalogue()
    if test_catalog.replace_block(text, test_catalog.markdown(sections)) != text:
        problem("docs/Testing.md: unit testlar ro'yxati eskirgan - Tests/test_catalog.py --update docs/Testing.md")
    notes.append(f"unit testlar katalogi: {sum(len(n) for _, n in sections)} ta test")


# ------------------------------------------------------------------ 8. changelog
def check_changelog():
    found = []
    notes.append(changelog.check(os.path.join(ROOT, "CHANGELOG.md"), found))
    for p in found:
        problem(p)


# ------------------------------------------------------------------ 9. documentation index
def check_index():
    index_path = os.path.join(ROOT, "docs", "README.md")
    if not os.path.exists(index_path):
        problem("docs/README.md (hujjatlar indeksi) yo'q")
        return
    text = strip_code(read(index_path))
    linked = set()
    for m in _link.finditer(text):
        target = m.group(1).partition("#")[0]
        if target and not re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I):
            linked.add(os.path.normpath(os.path.join(ROOT, "docs", target)))
    wanted = [os.path.join(ROOT, "docs", n) for n in sorted(os.listdir(os.path.join(ROOT, "docs")))
              if n.endswith(".md") and n != "README.md"]
    wanted += [os.path.join(ROOT, n) for n in ("CONTRIBUTING.md", "CHANGELOG.md") if os.path.exists(os.path.join(ROOT, n))]
    for path in wanted:
        if os.path.normpath(path) not in linked:
            problem(f"docs/README.md: {rel(path)} indeksda yo'q")
    notes.append(f"indeks: {len(wanted)} ta hujjat")


# ------------------------------------------------------------------ 10. images
MAX_IMAGE_BYTES = 400 * 1024
MAX_IMAGE_WIDTH = 1280


def image_width(path):
    """Pixel width of a PNG or JPEG (None when the format is not recognised)."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] == b"\x89PNG\r\n\x1a\n" and len(data) >= 24:
        return int.from_bytes(data[16:20], "big")
    if data[:2] == b"\xff\xd8":
        i = 2
        while i + 9 < len(data):
            if data[i] != 0xFF:
                i += 1
                continue
            marker = data[i + 1]
            if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
                i += 2
                continue
            length = int.from_bytes(data[i + 2:i + 4], "big")
            if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
                return int.from_bytes(data[i + 7:i + 9], "big")
            i += 2 + length
    return None


def check_images():
    folder = os.path.join(ROOT, "docs", "images")
    if not os.path.isdir(folder):
        return
    used = set()
    for md in markdown_files():
        text = strip_code(read(md))
        for target in [m.group(1) for m in _link.finditer(text)] + [m.group(1) for m in _html_src.finditer(text)]:
            used.add(os.path.normpath(os.path.join(os.path.dirname(md), target.partition("#")[0])))
    names = sorted(n for n in os.listdir(folder) if not n.startswith("."))
    total = 0
    for name in names:
        path = os.path.join(folder, name)
        size = os.path.getsize(path)
        total += size
        if os.path.normpath(path) not in used:
            problem(f"docs/images/{name}: hech bir hujjatda ishlatilmagan")
        if size > MAX_IMAGE_BYTES:
            problem(f"docs/images/{name}: {size // 1024} KB - {MAX_IMAGE_BYTES // 1024} KB dan katta (siqing yoki JPG qiling)")
        width = image_width(path)
        if width is None:
            problem(f"docs/images/{name}: PNG yoki JPEG emas")
        elif width > MAX_IMAGE_WIDTH:
            problem(f"docs/images/{name}: kengligi {width} px - {MAX_IMAGE_WIDTH} px dan katta")
    notes.append(f"rasmlar: {len(names)} ta, {total // 1024} KB")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()
    check_links()
    check_components()
    check_scripting()
    check_player_flags()
    check_faq_messages()
    check_build_options()
    check_test_catalog()
    check_changelog()
    check_index()
    check_images()
    for p in problems:
        print(p)
    if args.verbose or not problems:
        for n in notes:
            print(n)
    print(f"Hujjatlar tekshiruvi: {len(problems)} muammo")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
