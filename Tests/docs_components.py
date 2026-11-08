#!/usr/bin/env python3
"""The generated part of docs/Components.md: for every component of AllComponents (Zamin/src/Zamin/Scene/Components.h)
its inspector section and Add Component menu entry (Editor/src/Panels/SceneHierarchyPanel.cpp), its key in the scene
file (Zamin/src/Zamin/Scene/SceneSerializer.cpp) and its data members with their types and default values.

    Tests/docs_components.py                          the list of components and their blocks on stdout
    Tests/docs_components.py --update docs/Components.md   rewrite every generated block in that file
    Tests/docs_components.py --check docs/Components.md    exit code 1 when a block is out of date or a component
                                                          of AllComponents has no section in the file
A block sits between "<!-- gen:<Component> -->" and "<!-- /gen:<Component> -->"; everything else in the file (the
descriptions) is written by hand and left as it is. Standard library only: it reads the sources, nothing is built.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPONENTS_H = os.path.join(ROOT, "Zamin", "src", "Zamin", "Scene", "Components.h")
INSPECTOR_CPP = [os.path.join(ROOT, "Editor", "src", "Panels", f) for f in
                 ("SceneHierarchyPanel.cpp", "SceneHierarchyParticles.cpp", "SceneHierarchyPrefabs.cpp")]
SERIALIZER_CPP = os.path.join(ROOT, "Zamin", "src", "Zamin", "Scene", "SceneSerializer.cpp")

# Components every entity has (not in AllComponents, documented too).
ALWAYS = ["IDComponent", "TagComponent"]

# Scene-file keys that are not written as e["Key"] right after the component's TryGetComponent.
SCENE_KEYS = {
    "IDComponent": '"Entity"',
    "TagComponent": '"Tag"` (nom) va `"Active"',
    "RelationshipComponent": '"Parent"',
    "TagsComponent": '"Tags"',
    "TransformComponent": '"Transform"',
    "EditorMetadataComponent": '"Editor"',
    "PrefabLinkComponent": '"Editor"',
}


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def strip_comments_keep_lines(text):
    """Block comments become spaces (line numbers stay); line comments are kept for the member notes."""
    return re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)


def all_components(text):
    m = re.search(r"using\s+AllComponents\s*=\s*ComponentGroup<(.*?)>;", text, re.S)
    if not m:
        raise SystemExit("Components.h: AllComponents topilmadi")
    return [n.strip() for n in m.group(1).split(",") if n.strip()]


_headers = None


def header_texts():
    global _headers
    if _headers is None:
        _headers = []
        for base, _, files in os.walk(os.path.join(ROOT, "Zamin", "src", "Zamin")):
            for f in sorted(files):
                if f.endswith(".h"):
                    _headers.append(strip_comments_keep_lines(read(os.path.join(base, f))))
    return _headers


def struct_bases(text, name):
    m = re.search(r"\bstruct\s+" + re.escape(name) + r"\s*:\s*([^{;]+)\{", text)
    if not m:
        return []
    return [re.sub(r"^(public|private|protected)\s+", "", b.strip()) for b in m.group(1).split(",") if b.strip()]


def find_struct_anywhere(text, name):
    body = find_struct(text, name)
    if body is not None:
        return text, body
    for h in header_texts():
        body = find_struct(h, name)
        if body is not None:
            return h, body
    return None, None


def find_struct(text, name):
    m = re.search(r"\bstruct\s+" + re.escape(name) + r"\b[^;{]*\{", text)
    if not m:
        return None
    start = m.end()
    depth = 1
    i = start
    while i < len(text) and depth:
        c = text[i]
        if c == "/" and text.startswith("//", i):
            i = text.find("\n", i)
            continue
        if c == '"':
            j = i + 1
            while j < len(text) and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    return text[start:i - 1]


def split_top_level(body):
    """The struct's statements at its own level: (statement, trailing comment, preceding comments). Function
    bodies and nested type definitions are skipped."""
    out = []
    i = 0
    stmt = ""
    comments = []
    pending = []
    n = len(body)
    depth_paren = 0
    blank = False  # a blank line (or a skipped method / nested type) since the last member
    while i < n:
        c = body[i]
        if body.startswith("//", i):
            end = body.find("\n", i)
            end = n if end < 0 else end
            # Doxygen comments read as plain ones: "///", "///<", a member group's "@name" title; its
            # "@{" / "@}" markers carry no words
            comment = re.sub(r"^/*<?\s*", "", body[i + 2:end]).strip()
            comment = re.sub(r"^@name\s+", "", comment)
            if comment in ("@{", "@}", "@code", "@endcode"):
                i = end
                continue
            if stmt.strip():
                comments.append(comment)
            else:
                pending.append(comment)
            i = end
            continue
        if c == '"':
            j = i + 1
            while j < n and body[j] != '"':
                j += 2 if body[j] == "\\" else 1
            stmt += body[i:j + 1]
            i = j + 1
            continue
        if c == "(":
            depth_paren += 1
        elif c == ")":
            depth_paren -= 1
        if c == "{" and depth_paren == 0:
            head = stmt.strip()
            # A function body / nested type definition: skip the braces (an initializer `x{...}` is kept).
            is_init = (re.search(r"[\w>\]]\s*$", head) or head.endswith("=")) \
                and not re.search(r"\)\s*(const)?\s*(noexcept)?\s*(override)?\s*$", head) \
                and not re.match(r"^(struct|enum|class|union)\b", head) and "operator" not in head
            depth = 1
            j = i + 1
            while j < n and depth:
                if body.startswith("//", j):
                    j = body.find("\n", j)
                    j = n if j < 0 else j
                    continue
                if body[j] == "{":
                    depth += 1
                elif body[j] == "}":
                    depth -= 1
                j += 1
            if is_init:
                stmt += body[i:j]
                i = j
                continue
            # skipped: a method body or a nested type; a nested type ends with ';'. The comments before a
            # nested type still lead the members after it ("// runtime" over a helper struct).
            nested = bool(re.match(r"^(struct|enum|class|union)\b", head))
            i = j
            k = i
            while k < n and body[k] in " \t\r\n":
                k += 1
            if k < n and body[k] == ";":
                i = k + 1
            if nested:
                pending = pending + comments
            else:
                pending = []
                blank = True
            stmt, comments = "", []
            continue
        if c == ";" and depth_paren == 0:
            # the trailing comment on the same line
            line_end = body.find("\n", i)
            line_end = n if line_end < 0 else line_end
            rest = body[i + 1:line_end]
            trailing = ""
            if "//" in rest:
                trailing = re.sub(r"^/*<?\s*", "", rest[rest.index("//") + 2:]).strip()
                i = line_end
            else:
                i += 1
            out.append((stmt.strip(), trailing, pending + comments, blank))
            stmt, comments, pending = "", [], []
            blank = False
            continue
        if c == "\n" and not stmt.strip():
            # a blank line ends a comment block that belonged to nothing
            j = i + 1
            while j < n and body[j] in " \t":
                j += 1
            if j < n and body[j] == "\n":
                pending = []
                blank = True
        stmt += c
        i += 1
    return out


def split_commas(s):
    parts, depth, cur = [], 0, ""
    for c in s:
        if c in "<({[":
            depth += 1
        elif c in ">)}]":
            depth -= 1
        if c == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += c
    parts.append(cur)
    return [p.strip() for p in parts]


_member = re.compile(r"^(?P<type>(?:const\s+)?[A-Za-z_][\w:]*(?:\s*<.*>)?(?:\s*[*&])?)\s+(?P<rest>[A-Za-z_]\w*.*)$", re.S)


def members(body):
    result = []
    runtime_block = False  # a "// runtime ..." comment line marks the members after it, up to a blank line
    for stmt, trailing, before, blank in split_top_level(body):
        if blank:
            runtime_block = False
        if any(re.match(r"^runtime\b", c, re.I) or re.search(r"\bruntime\b.*\b(state|data|only|reset|cleared)\b", c, re.I) for c in before):
            runtime_block = True
        s = " ".join(stmt.split())
        if not s or s.startswith(("using ", "friend ", "static ", "typedef ", "template", "enum ", "struct ", "public:",
                                  "private:", "protected:", "constexpr ")):
            continue
        s = re.sub(r"^(public|private|protected):\s*", "", s)
        if "(" in s.split("=")[0].split("{")[0]:
            continue  # a function declaration / constructor
        m = _member.match(s)
        if not m:
            continue
        typ = m.group("type").strip()
        if typ in ("return", "virtual", "explicit", "inline", "operator"):
            continue
        for decl in split_commas(m.group("rest")):
            dm = re.match(r"^([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*(?:=\s*(.+)|(\{.*\}))?$", decl, re.S)
            if not dm:
                continue
            name, array, init_eq, init_brace = dm.groups()
            default = (init_eq or init_brace or "").strip()
            if default.startswith("{") and default.endswith("}"):
                default = default.strip()
            note = " ".join(before + ([trailing] if trailing else []))
            result.append({
                "name": name,
                "type": typ + (array or ""),
                "default": default,
                "runtime": runtime_block or bool(re.search(r"^runtime\b|\(runtime\)|runtime only|runtime-only", note, re.I))
                           or name.startswith("Runtime"),
            })
    return result


def inspector_info():
    sections, menu = {}, {}
    for path in INSPECTOR_CPP:
        if not os.path.exists(path):
            continue
        text = read(path)
        for m in re.finditer(r"AddSection<(\w+)>\(\s*\w+\s*,\s*\"([^\"]+)\"", text):
            sections.setdefault(m.group(1), m.group(2))
        category = ""
        for line in text.splitlines():
            c = re.search(r"SeparatorText\(Tr\(\"([^\"]+)\"\)\)", line)
            if c:
                category = c.group(1)
            d = re.search(r"DisplayAddComponentEntry<(\w+)>\(\"([^\"]+)\"", line)
            if d:
                menu.setdefault(d.group(1), (category, d.group(2)))
    return sections, menu


def scene_keys():
    text = read(SERIALIZER_CPP)
    a = text.find("static json SerializeEntityValues(Entity entity, const EntityWriteFlags& flags)\n")
    body = text[a:] if a >= 0 else text
    keys = {}
    matches = list(re.finditer(r"TryGetComponent<(\w+)>\(\)", body))
    for i, m in enumerate(matches):
        name = m.group(1)
        if name in keys:
            continue
        end = matches[i + 1].start() if i + 1 < len(matches) else len(body)
        k = re.search(r"\be\[\"(\w+)\"\]", body[m.end():end])
        if k:
            keys[name] = f'"{k.group(1)}"'
    keys.update(SCENE_KEYS)
    return keys


def serializer_sections():
    """The serializer's code for each component: from its TryGetComponent<X> (writing) / AddComponent<X> (reading)
    to the next component's - what tells which members a scene file keeps."""
    text = read(SERIALIZER_CPP)
    sections = {}
    for pattern in (r"TryGetComponent<(\w+)>\(\)", r"Add(?:OrReplace)?Component<(\w+)>\("):
        matches = list(re.finditer(pattern, text))
        for i, m in enumerate(matches):
            end = matches[i + 1].start() if i + 1 < len(matches) else min(len(text), m.end() + 4000)
            sections[m.group(1)] = sections.get(m.group(1), "") + text[m.end():end]
    return sections, text


def md_cell(s):
    s = " ".join(s.split())
    return s.replace("|", "\\|")


def block(name, struct_body, sections, menu, keys, serialized):
    lines = [f"<!-- gen:{name} -->"]
    facts = []
    if name in sections:
        facts.append(f"Inspektorda: **{sections[name]}**")
    if name in menu:
        cat, entry = menu[name]
        facts.append(f"qo'shish: **+ Komponent → {cat} → {entry}**")
    elif name in ALWAYS or name in ("TransformComponent", "RelationshipComponent"):
        facts.append("har bir obyektda bor")
    else:
        facts.append("inspektor menyusida yo'q (editor / tizim o'zi qo'shadi)")
    if name in keys:
        facts.append(f"sahna faylida: `{keys[name]}`")
    lines.append("_" + "; ".join(facts) + "._")
    lines.append("")
    fields = []
    for owner, body in struct_body:
        for f in members(body):
            f["owner"] = owner
            fields.append(f)
    if fields:
        lines.append("| Maydon | Tur | Standart |")
        lines.append("|---|---|---|")
        bases = sorted({f["owner"] for f in fields if f["owner"] != name})
        for f in fields:
            default = f"`{md_cell(f['default'])}`" if f["default"] else "—"
            # Runtime-only: the header says so and the serializer never touches it.
            saved = re.search(r"(->|\.)" + re.escape(f["name"]) + r"\b", serialized)
            runtime = f["runtime"] or f["type"].endswith("*")
            mark = " _(faqat o'yin paytida)_" if runtime and not saved else ""
            lines.append(f"| `{f['name']}`{mark} | `{md_cell(f['type'])}` | {default} |")
        if bases:
            own = sum(1 for f in fields if f["owner"] == name)
            names = ", ".join(f"`{b}`" for b in bases)
            lines.append("")
            lines.append(f"_Barcha maydonlar {names} dan meros._" if own == 0
                         else f"_Jadval boshidagi maydonlar {names} dan meros._")
    else:
        lines.append("_Ma'lumot maydonlari yo'q._")
    lines.append(f"<!-- /gen:{name} -->")
    return "\n".join(lines)


def generate():
    raw = read(COMPONENTS_H)
    text = strip_comments_keep_lines(raw)
    names = ALWAYS + all_components(text)
    sections, menu = inspector_info()
    keys = scene_keys()
    serializer, serializer_text = serializer_sections()
    blocks = {}
    for name in names:
        parts = []
        # base structs first (Joint3DComponentBase, UILayoutGroupSettings ...), then the component's own
        for base in struct_bases(text, name):
            _, body = find_struct_anywhere(text, base)
            if body is not None:
                parts.append((base, body))
        body = find_struct(text, name)
        if body is not None:
            parts.append((name, body))
        # The component's own serializer code; one that calls helpers (writeJoint3D(*c) ...) or has no
        # section is looked up in the whole file.
        own = serializer.get(name, "")
        serialized = own if own and not re.search(r"\(\s*\*?c\s*[,)]", own) else serializer_text
        blocks[name] = block(name, parts, sections, menu, keys, serialized)
    return names, blocks


def apply(doc, blocks):
    missing = []
    for name, b in blocks.items():
        pattern = re.compile(r"<!-- gen:" + re.escape(name) + r" -->.*?<!-- /gen:" + re.escape(name) + r" -->", re.S)
        if not pattern.search(doc):
            missing.append(name)
            continue
        doc = pattern.sub(lambda _: b, doc)
    return doc, missing


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--update")
    ap.add_argument("--check")
    args = ap.parse_args()
    names, blocks = generate()
    path = args.update or args.check
    if not path:
        for name in names:
            print(f"## {name}\n\n{blocks[name]}\n")
        return 0
    doc = read(path)
    new, missing = apply(doc, blocks)
    for name in missing:
        print(f"{path}: {name} uchun bo'lim yo'q (<!-- gen:{name} --> ... <!-- /gen:{name} -->)", file=sys.stderr)
    if args.update:
        if new != doc:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(new)
            print(f"{path}: yangilandi ({len(names)} komponent)")
        else:
            print(f"{path}: o'zgarish yo'q")
        return 1 if missing else 0
    if new != doc:
        print(f"{path}: yaratilgan bloklar eskirgan - Tests/docs_components.py --update {os.path.relpath(path, ROOT)}",
              file=sys.stderr)
        return 1
    if missing:
        return 1
    print(f"{path}: {len(names)} komponent, bloklar yangi")
    return 0


if __name__ == "__main__":
    sys.exit(main())
