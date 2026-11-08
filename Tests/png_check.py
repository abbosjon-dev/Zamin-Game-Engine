#!/usr/bin/env python3
"""Checks a screenshot the tests took (Tests/export_test.sh, Tests/editor_smoke.sh): that it is a PNG of the
expected size and that it shows something - not one flat colour (a black window, a missing frame).

    Tests/png_check.py shot.png                         size, colours, brightness
    Tests/png_check.py shot.png --size 1280x720 --min-colors 64 --min-stddev 8

Only the standard library (zlib): 8-bit grey / RGB / RGBA PNGs, as the engine writes them. Exit code 0 when every
given limit holds, 1 when one does not, 2 when the file cannot be read.
"""
import argparse
import math
import struct
import sys
import zlib


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("PNG emas")
    pos, width, height, depth, kind, idat = 8, 0, 0, 0, 0, []
    while pos < len(data):
        length, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            width, height, depth, kind = struct.unpack(">IIBB", body[:10])
        elif tag == b"IDAT":
            idat.append(body)
        elif tag == b"IEND":
            break
    channels = {0: 1, 2: 3, 6: 4}.get(kind)
    if depth != 8 or channels is None:
        raise ValueError(f"qo'llanmaydigan PNG turi (bit {depth}, rang turi {kind})")
    raw = zlib.decompress(b"".join(idat))
    stride = width * channels
    rows, prev, at = [], bytearray(stride), 0
    for _ in range(height):
        kind_filter = raw[at]
        line = bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if kind_filter == 1:
                line[i] = (line[i] + a) & 255
            elif kind_filter == 2:
                line[i] = (line[i] + b) & 255
            elif kind_filter == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif kind_filter == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return width, height, channels, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("png")
    parser.add_argument("--size", help="WxH it must be")
    parser.add_argument("--min-colors", type=int, default=0, help="at least this many different colours")
    parser.add_argument("--min-stddev", type=float, default=0.0, help="brightness spread (0..255) at least this")
    args = parser.parse_args()
    try:
        width, height, channels, rows = read_png(args.png)
    except (OSError, ValueError, zlib.error, struct.error) as error:
        print(f"{args.png}: o'qilmadi - {error}")
        return 2
    colors, total, total2, count = set(), 0.0, 0.0, 0
    step = max(1, (width * height) // 200000)  # a sample of the pixels on big pictures
    for y, row in enumerate(rows):
        for x in range(0, width, 1 if step == 1 else (step if y % 2 else step + 1)):
            px = row[x * channels:(x + 1) * channels]
            rgb = px[:3] if channels >= 3 else px * 3
            colors.add(bytes(rgb))
            lum = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]
            total += lum
            total2 += lum * lum
            count += 1
    mean = total / count if count else 0.0
    stddev = math.sqrt(max(0.0, total2 / count - mean * mean)) if count else 0.0
    print(f"{args.png}: {width}x{height}, {len(colors)} xil rang, yorqinlik o'rtacha {mean:.1f}, tarqoqlik {stddev:.1f}")
    problems = []
    if args.size and args.size != f"{width}x{height}":
        problems.append(f"o'lcham {width}x{height}, kutilgan {args.size}")
    if len(colors) < args.min_colors:
        problems.append(f"{len(colors)} xil rang (kamida {args.min_colors} kerak) - rasm deyarli bir xil")
    if stddev < args.min_stddev:
        problems.append(f"yorqinlik tarqoqligi {stddev:.1f} (kamida {args.min_stddev} kerak) - rasm bo'sh ko'rinadi")
    for problem in problems:
        print(f"  {problem}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
