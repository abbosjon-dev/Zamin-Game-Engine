#!/usr/bin/env python3
"""The sample games' procedural assets (ctest: SampleAssets, label "samples"): the six M24 games keep no hand-made art -
every texture, sprite atlas, mesh, sound and scene is made by a generator (Tools/samples/gen_<game>.py). This checks
that what is committed is what the generators make, that it is small, and that it holds together.

    Tests/samples_assets.py              every generated sample
    Tests/samples_assets.py --regen      also run every generator into a temporary folder (ZAMIN_SAMPLES_OUT) and
                                         compare its files with the committed ones byte for byte (needs numpy)
    Tests/samples_assets.py --game Racing --verbose   one game, with its size table

Checks for each game (Projects/<Game>):
  1. generator  - Tools/samples/gen_<game>.py exists and writes this project (NAME = "<Game>").
  2. budget     - Assets/ at most 2 MiB together; a texture at most 512 KB and 2048 px, a sound at most 800 KB and
                  20 s, a scene at most 700 KB.
  3. meta       - every asset has its .zmeta (a UUID, a type) and every .zmeta its asset; the UUIDs are unique
                  (Localization/*.json are tables the engine reads by language, not assets).
  4. pictures   - every PNG is a PNG (its header and size).
  5. sounds     - every WAV is 16-bit PCM, mono or stereo, 11 025 ... 48 000 Hz; not silent, not clipped (procedural
                  sounds from samplekit.write_wav are normalised below full scale).
  6. references - every asset a scene, the project file or a script names (Textures/..., Audio/..., Models/...,
                  Sfx("name") -> Audio/name.wav) exists; every sound and texture is used by something.
  7. --regen    - a generator run gives exactly the committed files, and every committed asset is one it writes.
Standard library only (numpy for --regen). Exit code: 0 when every check holds, 1 otherwise.
"""
import argparse
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True

GAMES = ["TopDownShooter", "Breakout", "FPSArena", "Sokoban", "EndlessRunner", "Racing"]
MAX_ASSETS = 2 * 1024 * 1024
MAX_FILE = {".png": 512 * 1024, ".wav": 800 * 1024, ".zscene": 700 * 1024}
MAX_PICTURE_SIDE = 2048
MAX_SOUND_SECONDS = 20.0

problems = []


def problem(game, text):
    problems.append(f"{game}: {text}")


def rel(path, base):
    return os.path.relpath(path, base).replace(os.sep, "/")


def asset_files(assets):
    out = []
    for folder, _, names in os.walk(assets):
        for name in names:
            out.append(os.path.join(folder, name))
    return sorted(out)


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", head[16:24])


def wav_stats(path):
    """(channels, rate, seconds, peak 0..1) of a 16-bit PCM WAV."""
    with wave.open(path, "rb") as w:
        channels, width, rate, frames = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        data = w.readframes(frames)
    if width != 2:
        raise ValueError(f"{8 * width}-bit (16-bit kerak)")
    count = len(data) // 2
    samples = struct.unpack(f"<{count}h", data[:count * 2]) if count else ()
    peak = max((abs(s) for s in samples), default=0) / 32768.0
    return channels, rate, frames / float(rate or 1), peak


def check_game(game, verbose):
    project = os.path.join(ROOT, "Projects", game)
    assets = os.path.join(project, "Assets")
    generator = os.path.join(ROOT, "Tools", "samples", f"gen_{game.lower()}.py")
    # 1. generator
    if not os.path.exists(generator):
        problem(game, f"generator yo'q: {rel(generator, ROOT)}")
    else:
        with open(generator, encoding="utf-8") as f:
            if f'NAME = "{game}"' not in f.read():
                problem(game, f"{rel(generator, ROOT)} bu loyihani yozmaydi (NAME = \"{game}\" yo'q)")
    if not os.path.isdir(assets):
        problem(game, "Assets/ papkasi yo'q")
        return {}

    files = asset_files(assets)
    data_files = [p for p in files if not p.endswith(".zmeta")]
    metas = [p for p in files if p.endswith(".zmeta")]
    # 2. budget
    total = sum(os.path.getsize(p) for p in files)
    if total > MAX_ASSETS:
        problem(game, f"Assets/ {total / 1048576:.2f} MiB - chegara {MAX_ASSETS / 1048576:.0f} MiB")
    by_kind = {}
    for p in data_files:
        ext = os.path.splitext(p)[1].lower()
        size = os.path.getsize(p)
        by_kind.setdefault(ext, [0, 0])
        by_kind[ext][0] += 1
        by_kind[ext][1] += size
        if ext in MAX_FILE and size > MAX_FILE[ext]:
            problem(game, f"{rel(p, assets)} {size // 1024} KB - {ext} uchun chegara {MAX_FILE[ext] // 1024} KB")
    # 3. meta
    uuids = {}
    for p in data_files:
        meta = p + ".zmeta"
        if not os.path.exists(meta) and not rel(p, assets).startswith("Localization/"):
            problem(game, f"{rel(p, assets)}: .zmeta yo'q")
    for meta in metas:
        if not os.path.exists(meta[:-len(".zmeta")]):
            problem(game, f"{rel(meta, assets)}: asseti yo'q")
            continue
        try:
            with open(meta, encoding="utf-8") as f:
                m = json.load(f)
        except (OSError, ValueError) as e:
            problem(game, f"{rel(meta, assets)}: o'qilmadi ({e})")
            continue
        uuid = str(m.get("UUID", ""))
        if not re.fullmatch(r"[0-9a-f]{16}", uuid) or not m.get("Type"):
            problem(game, f"{rel(meta, assets)}: UUID yoki Type noto'g'ri")
        elif uuid in uuids:
            problem(game, f"{rel(meta, assets)}: UUID {uuid} takrorlanadi ({uuids[uuid]})")
        else:
            uuids[uuid] = rel(meta, assets)
    # 4. pictures, 5. sounds
    sound_seconds = 0.0
    for p in data_files:
        ext = os.path.splitext(p)[1].lower()
        name = rel(p, assets)
        if ext == ".png":
            size = png_size(p)
            if not size:
                problem(game, f"{name}: PNG emas")
            elif max(size) > MAX_PICTURE_SIDE:
                problem(game, f"{name}: {size[0]}x{size[1]} - eng ko'pi {MAX_PICTURE_SIDE} px")
        elif ext == ".wav":
            try:
                channels, rate, seconds, peak = wav_stats(p)
            except (OSError, ValueError, wave.Error, struct.error) as e:
                problem(game, f"{name}: WAV o'qilmadi ({e})")
                continue
            sound_seconds += seconds
            if channels not in (1, 2) or not 11025 <= rate <= 48000:
                problem(game, f"{name}: {channels} kanal, {rate} Hz")
            if seconds > MAX_SOUND_SECONDS:
                problem(game, f"{name}: {seconds:.1f} s - eng ko'pi {MAX_SOUND_SECONDS:.0f} s")
            if peak < 0.05:
                problem(game, f"{name}: deyarli jim (cho'qqi {peak:.3f})")
            elif peak >= 0.999:
                problem(game, f"{name}: kesilgan (cho'qqi {peak:.3f})")
    # 6. references
    referenced = set()
    texts = []
    for p in files:
        if p.endswith((".zscene", ".zmeta", ".json", ".zprefab", ".zanim", ".zsanim", ".zparticle")):
            with open(p, encoding="utf-8", errors="replace") as f:
                texts.append((rel(p, assets), f.read()))
    with open(os.path.join(project, f"{game}.zproject"), encoding="utf-8") as f:
        texts.append((f"{game}.zproject", f.read()))
    sources = []
    for folder, _, names in os.walk(os.path.join(project, "Source")):
        for n in sorted(names):
            with open(os.path.join(folder, n), encoding="utf-8", errors="replace") as f:
                sources.append((f"Source/{n}", f.read()))
    path_ref = re.compile(r'"((?:Textures|Audio|Models|Scenes|Fonts|Prefabs)/[^"{}]+\.[A-Za-z0-9]+)"')
    for where, text in texts + sources:
        for m in path_ref.finditer(text):
            target = m.group(1)
            referenced.add(target)
            if not os.path.exists(os.path.join(assets, target)):
                problem(game, f"{where} {target} ga ishora qiladi - fayl yo'q")
    # Sfx("name") / PlaySound(std::format("Audio/{}.wav", name)): every lower-case string literal of a script that
    # uses that pattern which is a sound's name counts as a use.
    sound_names = {os.path.splitext(rel(p, assets))[0][len("Audio/"):] for p in data_files if rel(p, assets).startswith("Audio/")}
    for where, text in sources:
        literals = set(re.findall(r'"([a-z0-9_]+)"', text))
        for name in literals & sound_names:
            referenced.add(f"Audio/{name}.wav")
        for m in re.finditer(r'Sfx\("([a-z0-9_]+)"', text):
            if m.group(1) not in sound_names:
                problem(game, f"{where}: Sfx(\"{m.group(1)}\") - Audio/{m.group(1)}.wav yo'q")
    for p in data_files:
        name = rel(p, assets)
        if name.startswith(("Audio/", "Textures/")) and name not in referenced:
            problem(game, f"{name} hech qayerda ishlatilmaydi")
    if verbose:
        kinds = ", ".join(f"{ext or '-'} {n} ta {size / 1024:.0f} KB" for ext, (n, size) in sorted(by_kind.items()))
        print(f"  {game:15} {total / 1048576:.2f} MiB ({kinds}); ovozlar {sound_seconds:.1f} s")
    return {"files": len(data_files), "bytes": total, "sound_seconds": round(sound_seconds, 1)}


def regen(game):
    """7. The generator's output is the committed project, and every committed asset is one it writes."""
    generator = os.path.join(ROOT, "Tools", "samples", f"gen_{game.lower()}.py")
    project = os.path.join(ROOT, "Projects", game)
    with tempfile.TemporaryDirectory(prefix="zamin-regen-") as out:
        env = dict(os.environ, ZAMIN_SAMPLES_OUT=out, PYTHONDONTWRITEBYTECODE="1")
        result = subprocess.run([sys.executable, generator], env=env, capture_output=True, text=True)
        if result.returncode != 0:
            problem(game, f"generator xato bilan tugadi: {(result.stderr or result.stdout).strip().splitlines()[-1:]}")
            return
        made = os.path.join(out, game)
        written = set()
        for folder, _, names in os.walk(made):
            for n in names:
                path = os.path.join(folder, n)
                name = rel(path, made)
                written.add(name)
                committed = os.path.join(project, name)
                if not os.path.exists(committed):
                    problem(game, f"generator {name} ni yozadi, repozitoriyda yo'q")
                    continue
                with open(path, "rb") as a, open(committed, "rb") as b:
                    if a.read() != b.read():
                        problem(game, f"{name} generator chiqargani bilan bir xil emas (Tools/samples/gen_{game.lower()}.py ni qayta ishga tushiring)")
        for path in asset_files(os.path.join(project, "Assets")):
            name = rel(path, project)
            if name not in written:
                problem(game, f"{name} generatordan emas (qo'lda qo'shilgan yoki eskirgan fayl)")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", action="append", default=[], help="only this game (more than once: several)")
    parser.add_argument("--regen", action="store_true", help="run the generators and compare (needs numpy)")
    parser.add_argument("--verbose", action="store_true", help="the size table")
    args = parser.parse_args()
    games = [g for g in GAMES if not args.game or g.lower() in {a.lower() for a in args.game}]
    if not games:
        print(f"Noma'lum o'yin: {', '.join(args.game)} (bor: {', '.join(GAMES)})", file=sys.stderr)
        return 2
    regen_ok = args.regen
    if args.regen:
        try:
            import numpy  # noqa: F401
        except ImportError:
            print("numpy yo'q - generatorlar ishga tushirilmaydi (--regen o'tkazib yuborildi)")
            regen_ok = False
    summary = {}
    for game in games:
        summary[game] = check_game(game, args.verbose)
        if regen_ok:
            regen(game)
    for p in problems:
        print(p)
    total = sum(s.get("bytes", 0) for s in summary.values())
    print(f"Namuna assetlari: {len(games)} ta o'yin, {sum(s.get('files', 0) for s in summary.values())} ta fayl, "
          f"{total / 1048576:.1f} MiB{', generatorlar bilan solishtirildi' if regen_ok else ''} - "
          + ("muammo yo'q" if not problems else f"{len(problems)} ta muammo"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
