#!/usr/bin/env python3
"""Writes Zamin/src/Zamin/ImGui/IconRanges.inl: the Font Awesome codepoints of every ICON_FA_ define in Icons.h
(inside ICON_MIN_FA ... ICON_MAX_FA) as ImGui glyph ranges - the editor bakes only these icons into its font atlas.

    Tools/gen_icon_ranges.py        after adding an icon to Icons.h (ZaminTests IconGlyphRangesCoverIconsHeader checks it)
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "Zamin", "src", "Zamin", "ImGui", "Icons.h")
OUT = os.path.join(ROOT, "Zamin", "src", "Zamin", "ImGui", "IconRanges.inl")


def main():
    src = open(HEADER, encoding="utf-8").read()
    lo = int(re.search(r"#define\s+ICON_MIN_FA\s+(0x[0-9a-fA-F]+)", src).group(1), 16)
    hi = int(re.search(r"#define\s+ICON_MAX_FA\s+(0x[0-9a-fA-F]+)", src).group(1), 16)
    codepoints = set()
    for m in re.finditer(r'#define\s+ICON_FA_\w+\s+"((?:\\x[0-9a-fA-F]{2})+)"', src):
        raw = bytes(int(h, 16) for h in re.findall(r"\\x([0-9a-fA-F]{2})", m.group(1)))
        cp = ord(raw.decode("utf-8"))
        if lo <= cp <= hi:
            codepoints.add(cp)
    ranges = []
    for cp in sorted(codepoints):
        if ranges and ranges[-1][1] + 1 == cp:
            ranges[-1][1] = cp
        else:
            ranges.append([cp, cp])
    lines, row = [], []
    for a, b in ranges:
        row.append(f"0x{a:04x}, 0x{b:04x},")
        if len(row) == 6:
            lines.append("    " + " ".join(row))
            row = []
    if row:
        lines.append("    " + " ".join(row))
    text = ("// The Font Awesome codepoints the editor uses (every ICON_FA_ define of Icons.h) as ImGui glyph ranges: only these\n"
            "// icons are baked into the editor's font atlas instead of the font's whole block (start-up time, task 495). Made from\n"
            "// Icons.h by Tools/gen_icon_ranges.py; ZaminTests (IconGlyphRangesCoverIconsHeader) fails when an icon is missing.\n"
            "// clang-format off\n" + "\n".join(lines) + "\n// clang-format on\n")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"{len(codepoints)} ta ikonka, {len(ranges)} ta oraliq: {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
