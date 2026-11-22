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
                  Sfx("name") -> Audio/name.wav) exists; every sound and texture is used by something; a sound named
                  only by its short name is in the project's Export.AlwaysInclude (or the export leaves it out).
  7. --regen    - a generator run gives exactly the committed files, and every committed asset is one it writes.
  8. input      - the project's input map: every action has a keyboard / mouse binding and a gamepad binding, every
                  axis a gamepad stick or buttons and keys (but the mouse-only ones listed in MOUSE_AXES); every
                  action / axis / vector the scripts read is in the map and every one in the map is read; the scripts
                  never poll the keyboard or a gamepad directly (only the mouse, for aiming / looking); the main menu's
                  controls hint (InputHints) names only the map's actions, in both languages.

The compiler-free vertical slices (GRAPH_GAMES: Visual Script graphs instead of C++ scripts, generators named in the
table) get the same checks in graph mode, with their own budget (6 MiB of Assets/, a scene 700 KB, a model 600 KB) and:
  - references  .zgraph / .zmat / .zsanim / .zprefab files count as referrers and as assets (Scripts/, Materials/,
                Animations/, Prefabs/ ...); every texture, sound, model and animation is used
  - input       the actions / axes / vectors the graphs read: ActionPressed / ActionJustPressed / Axis names, the
                Gameplay.Input Move / Jump / Sprint pins (their defaults when not set, nothing when empty), the gameplay
                UI panels' Action - every one in the map, every one in the map read; each action has a keyboard /
                mouse and a gamepad binding
  - language    uz.json and en.json have the same keys; every Text LocalizationKey of a scene and every literal
                Localization.Text Key of a graph is in both; every "[Name]" control hint of a language string or a
                hinted scene text names the map
  - test data   nothing under Assets/ is test data (the headless oracle lives in Tests/oracle.json, outside what an
                export ships)
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
# The vertical slices (graph mode): project -> generator
GRAPH_GAMES = {"LanternKeeper": "gen_lantern_keeper.py", "BeaconPass": "gen_beacon_pass.py"}
GRAPH_MAX_ASSETS = 6 * 1024 * 1024
GRAPH_MAX_FILE = {".png": 512 * 1024, ".wav": 800 * 1024, ".zscene": 700 * 1024, ".glb": 600 * 1024}
MAX_ASSETS = 2 * 1024 * 1024
MAX_FILE = {".png": 512 * 1024, ".wav": 800 * 1024, ".zscene": 700 * 1024}
MAX_PICTURE_SIDE = 2048
MAX_SOUND_SECONDS = 20.0

problems = []

# Axes the keyboard and mouse drive with the mouse itself (Input::GetMouseDelta / the cursor), not with keys - or, in
# the runner, with the Left / Right actions.
MOUSE_AXES = {"FPSArena": {"LookX", "LookY"}, "EndlessRunner": {"Steer"}}


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


def generator_of(game):
    return os.path.join(ROOT, "Tools", "samples", GRAPH_GAMES.get(game, f"gen_{game.lower()}.py"))


def check_game(game, verbose):
    graph = game in GRAPH_GAMES
    project = os.path.join(ROOT, "Projects", game)
    assets = os.path.join(project, "Assets")
    generator = generator_of(game)
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
    budget, limits = (GRAPH_MAX_ASSETS, GRAPH_MAX_FILE) if graph else (MAX_ASSETS, MAX_FILE)
    if total > budget:
        problem(game, f"Assets/ {total / 1048576:.3f} MiB - chegara {budget / 1048576:.0f} MiB")
    by_kind = {}
    for p in data_files:
        ext = os.path.splitext(p)[1].lower()
        size = os.path.getsize(p)
        by_kind.setdefault(ext, [0, 0])
        by_kind[ext][0] += 1
        by_kind[ext][1] += size
        if ext in limits and size > limits[ext]:
            problem(game, f"{rel(p, assets)} {size // 1024} KB - {ext} uchun chegara {limits[ext] // 1024} KB")
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
        if p.endswith((".zscene", ".zmeta", ".json", ".zprefab", ".zanim", ".zsanim", ".zparticle", ".zgraph", ".zmat")):
            with open(p, encoding="utf-8", errors="replace") as f:
                texts.append((rel(p, assets), f.read()))
    with open(os.path.join(project, f"{game}.zproject"), encoding="utf-8") as f:
        texts.append((f"{game}.zproject", f.read()))
    sources = []
    for folder, _, names in os.walk(os.path.join(project, "Source")):
        for n in sorted(names):
            with open(os.path.join(folder, n), encoding="utf-8", errors="replace") as f:
                sources.append((f"Source/{n}", f.read()))
    path_ref = re.compile(r'"((?:Textures|Audio|Models|Scenes|Fonts|Prefabs|Scripts|Materials|Animations)/[^"{}]+\.[A-Za-z0-9]+)"')
    for where, text in texts + sources:
        for m in path_ref.finditer(text):
            target = m.group(1)
            if "*" in target:
                continue  # an Export.AlwaysInclude pattern, not a file
            referenced.add(target)
            if not os.path.exists(os.path.join(assets, target)):
                problem(game, f"{where} {target} ga ishora qiladi - fayl yo'q")
    # Sfx("name") / PlaySound(std::format("Audio/{}.wav", name)): every lower-case string literal of a script that
    # uses that pattern which is a sound's name counts as a use.
    sound_names = {os.path.splitext(rel(p, assets))[0][len("Audio/"):] for p in data_files if rel(p, assets).startswith("Audio/")}
    literal = set(referenced)  # named with their whole path: the export's scan of the sources finds them
    for where, text in sources:
        literals = set(re.findall(r'"([a-z0-9_]+)"', text))
        for name in literals & sound_names:
            referenced.add(f"Audio/{name}.wav")
        for m in re.finditer(r'Sfx\("([a-z0-9_]+)"', text):
            if m.group(1) not in sound_names:
                problem(game, f"{where}: Sfx(\"{m.group(1)}\") - Audio/{m.group(1)}.wav yo'q")
    used_kinds = ("Audio/", "Textures/", "Models/", "Animations/", "Materials/", "Scripts/") if graph else ("Audio/", "Textures/")
    for p in data_files:
        name = rel(p, assets)
        if name.startswith(used_kinds) and name not in referenced and not (graph and referenced_by_name(name, texts)):
            problem(game, f"{name} hech qayerda ishlatilmaydi")
    # The export leaves out what nothing names (ExcludeUnusedAssets): a sound a script names only by its short name
    # (Sfx("hit") -> "Audio/hit.wav" made at runtime) must be in the project's Export.AlwaysInclude, or the exported
    # game cannot load it.
    with open(os.path.join(project, f"{game}.zproject"), encoding="utf-8") as f:
        export = json.load(f).get("Export", {})
    patterns = export.get("AlwaysInclude", []) if export.get("ExcludeUnusedAssets", True) else ["*"]
    for target in sorted(referenced - literal):
        if target.startswith("Audio/") and not any(export_includes(target, pat) for pat in patterns):
            problem(game, f"{target}: skript uni nomi bilan chaqiradi (Sfx), lekin eksport uni olmaydi - loyihaning "
                          f"Export.AlwaysInclude'iga qo'shing (masalan \"Audio/*.wav\")")
    # 8. input (graph mode: what the graphs read; the language tables; no test data in the assets)
    if graph:
        n_actions, n_axes, n_vectors = check_graph_input(game, project, assets, texts)
        check_languages(game, assets, texts, project)
        for p in data_files:
            if re.search(r"oracle|\btest", rel(p, assets), re.IGNORECASE):
                problem(game, f"{rel(p, assets)}: test ma'lumoti Assets/ ichida (eksport uni o'yin bilan birga tarqatadi) - Tests/ ga")
        if not os.path.exists(os.path.join(project, "Tests", "oracle.json")):
            problem(game, "Tests/oracle.json (headless oracle) yo'q")
    else:
        n_actions, n_axes, n_vectors = check_input(game, project, sources, assets)
    if verbose:
        kinds = ", ".join(f"{ext or '-'} {n} ta {size / 1024:.0f} KB" for ext, (n, size) in sorted(by_kind.items()))
        print(f"  {game:15} {total / 1048576:.2f} MiB ({kinds}); ovozlar {sound_seconds:.1f} s; input: {n_actions} harakat, "
              f"{n_axes} o'q, {n_vectors} vektor")
    return {"files": len(data_files), "bytes": total, "sound_seconds": round(sound_seconds, 1)}


def referenced_by_name(name, texts):
    """A model's clip / an animation named by its file stem (Animator states name clips; glb models by path)."""
    stem = os.path.splitext(os.path.basename(name))[0]
    return name.startswith("Animations/") and any(f'"{stem}"' in text for _, text in texts)


def graph_nodes(texts):
    for where, text in texts:
        if where.endswith(".zgraph"):
            try:
                doc = json.loads(text)
            except ValueError:
                continue
            for node in doc.get("Nodes", []):
                yield where, node


def check_graph_input(game, project, assets, texts):
    """Graph mode of 8: the map covers both devices; the graphs read every name of the map and only those."""
    with open(os.path.join(project, f"{game}.zproject"), encoding="utf-8") as f:
        inputs = json.load(f).get("InputActions", {})
    actions = {a["Name"]: a.get("Bindings", []) for a in inputs.get("Actions", [])}
    axes = {a["Name"]: a for a in inputs.get("Axes", [])}
    vectors = {v["Name"]: v for v in inputs.get("Vectors", [])}
    for name, bindings in actions.items():
        devices = {binding_device(b) for b in bindings}
        if "kb" not in devices:
            problem(game, f"input: '{name}' harakatida klaviatura / sichqoncha bog'lanishi yo'q")
        if "pad" not in devices:
            problem(game, f"input: '{name}' harakatida gamepad bog'lanishi yo'q")
    for name, axis in axes.items():
        sides = axis.get("Negative", []) + axis.get("Positive", [])
        if "pad" not in {binding_device(b) for b in sides} and not axis.get("GamepadAxis"):
            problem(game, f"input: '{name}' o'qida gamepad stigi ham, tugmasi ham yo'q")
    read = {}
    for where, node in graph_nodes(texts):
        kind, ins = node.get("Kind", ""), node.get("Inputs", {})
        if kind in ("ActionPressed", "ActionJustPressed"):
            read.setdefault(ins.get("Name", ""), where)
        elif kind == "Axis":
            read.setdefault(ins.get("Name", ""), where)
        elif kind == "Gameplay.Input":
            for pin, default in (("Move", "Move"), ("Jump", "Jump"), ("Sprint", "Sprint")):
                name = ins.get(pin, default)
                if name:
                    read.setdefault(name, where)
        elif kind == "Gameplay.UIConfigure" and ins.get("Action"):
            read.setdefault(ins["Action"], where)
    known = set(actions) | set(axes) | set(vectors)
    for name, where in sorted(read.items()):
        if name not in known:
            problem(game, f"{where}: '{name}' o'qiladi - input xaritasida yo'q")
    used = set(read) | {vectors[v][k] for v in read if v in vectors for k in ("X", "Y")}
    for name in sorted(known - used - {"ToggleFullscreen"}):
        problem(game, f"input: '{name}' xaritada bor, lekin graflar uni o'qimaydi")
    return len(actions), len(axes), len(vectors)


def check_languages(game, assets, texts, project):
    """Graph mode: both language tables have the same keys; the scenes' and graphs' keys exist; hints name the map."""
    tables = {}
    for lang in ("uz", "en"):
        path = os.path.join(assets, "Localization", f"{lang}.json")
        if not os.path.exists(path):
            problem(game, f"Localization/{lang}.json yo'q")
            return
        with open(path, encoding="utf-8") as f:
            tables[lang] = json.load(f)
    for a, b in (("uz", "en"), ("en", "uz")):
        missing = sorted(k for k in tables[a] if k not in tables[b] and not k.startswith("$"))
        for key in missing[:12]:
            problem(game, f"Localization/{b}.json: '{key}' kaliti yo'q ({a}.json da bor)")
    with open(os.path.join(project, f"{game}.zproject"), encoding="utf-8") as f:
        inputs = json.load(f).get("InputActions", {})
    names = {x["Name"] for k in ("Actions", "Axes", "Vectors") for x in inputs.get(k, [])}

    def hints(where, text):
        for token in re.findall(r"\[([^\]]*)\]", text):
            if token.startswith(("kb:", "pad:")):
                continue
            if token.split("|", 1)[0] not in names:
                problem(game, f"{where}: [{token}] input xaritasida yo'q")
    for lang, table in tables.items():
        for key, text in table.items():
            if isinstance(text, str) and not key.startswith("input."):
                hints(f"Localization/{lang}.json {key}", text)
    for where, text in texts:
        if where.endswith(".zscene"):
            doc = json.loads(text)
            for e in doc.get("Entities", []):
                t = e.get("Text")
                if not isinstance(t, dict):
                    continue
                key = t.get("LocalizationKey", "")
                if key and not all(key in tables[lang] for lang in tables):
                    problem(game, f"{where} '{e.get('Tag')}': '{key}' kaliti tillar jadvalida yo'q")
                if t.get("InputHints"):
                    hints(f"{where} '{e.get('Tag')}'", t.get("Text", ""))
    for where, node in graph_nodes(texts):
        if node.get("Kind") == "Localization.Text":
            key = node.get("Inputs", {}).get("Key", "")
            if key and not all(key in tables[lang] for lang in tables):
                problem(game, f"{where} node {node.get('ID')}: Localization.Text '{key}' kaliti tillar jadvalida yo'q")


def export_includes(path, pattern):
    """ExportAssets::MatchesInclude: the file itself, inside the folder, or a '*' pattern ('*' within a name)."""
    pattern = pattern.strip().strip("/")
    if path == pattern or path.startswith(pattern + "/") or pattern == "*":
        return True
    regex = "".join("[^/]*" if c == "*" else re.escape(c) for c in pattern)
    return re.fullmatch(regex, path) is not None


def binding_device(binding):
    source = binding.split(":", 1)[0]
    return "pad" if source in ("Gamepad", "GamepadAxis") else "kb" if source in ("Key", "Mouse") else None


def check_input(game, project, sources, assets):
    """8. input: the map covers both devices, the scripts use only the map (and the mouse), the hints name map entries."""
    with open(os.path.join(project, f"{game}.zproject"), encoding="utf-8") as f:
        inputs = json.load(f).get("InputActions", {})
    actions = {a["Name"]: a.get("Bindings", []) for a in inputs.get("Actions", [])}
    axes = {a["Name"]: a for a in inputs.get("Axes", [])}
    vectors = {v["Name"]: v for v in inputs.get("Vectors", [])}
    if not actions:
        problem(game, "input xaritasida harakat yo'q")
    for name, bindings in actions.items():
        devices = {binding_device(b) for b in bindings}
        if "kb" not in devices:
            problem(game, f"input: '{name}' harakatida klaviatura / sichqoncha bog'lanishi yo'q")
        if "pad" not in devices:
            problem(game, f"input: '{name}' harakatida gamepad bog'lanishi yo'q")
    for name, axis in axes.items():
        sides = axis.get("Negative", []) + axis.get("Positive", [])
        devices = {binding_device(b) for b in sides}
        if "pad" not in devices and not axis.get("GamepadAxis"):
            problem(game, f"input: '{name}' o'qida gamepad stigi ham, tugmasi ham yo'q")
        if "kb" not in devices and name not in MOUSE_AXES.get(game, set()):
            problem(game, f"input: '{name}' o'qida klaviatura tugmasi yo'q")
    for name, vector in vectors.items():
        for key in ("X", "Y"):
            if vector.get(key) not in axes:
                problem(game, f"input: '{name}' vektorining {key} o'qi ('{vector.get(key)}') xaritada yo'q")
    read = set()
    for where, text in sources:
        for m in re.finditer(r'Input::(IsAction\w*|GetAxis2D|GetAxis|GetVector)\("([^"]+)"', text):
            kind, name = m.group(1), m.group(2)
            read.add(name)
            known = actions if kind.startswith("IsAction") else axes if kind == "GetAxis" else vectors if kind == "GetVector" else axes
            if name not in known:
                problem(game, f"{where}: Input::{kind}(\"{name}\") - xaritada bunday nom yo'q")
        for m in re.finditer(r'Input::(IsKey\w*|IsGamepad\w*|GetGamepadAxis|IsAnyKeyJustPressed)\(', text):
            problem(game, f"{where}: Input::{m.group(1)} - skript tugmani to'g'ridan-to'g'ri o'qiydi (input xaritasidagi harakatdan foydalaning)")
    used_axes = read | {vectors[v][k] for v in read if v in vectors for k in ("X", "Y")}
    for name in list(actions) + list(axes) + list(vectors):
        if name not in read and name not in used_axes:
            problem(game, f"input: '{name}' xaritada bor, lekin skriptlar uni o'qimaydi")
    # The main menu's controls hint: "[Name]" of the map only (a typo would show as it is), in every language.
    names = set(actions) | set(axes) | set(vectors)
    for lang in ("uz", "en"):
        path = os.path.join(assets, "Localization", f"{lang}.json")
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as f:
            controls = json.load(f).get("menu", {}).get("controls", "")
        if not controls:
            problem(game, f"Localization/{lang}.json: menu.controls (boshqaruv ishorasi) yo'q")
        for token in re.findall(r"\[([^\]]*)\]", controls):
            if token.startswith(("kb:", "pad:")):
                continue
            if token.split("|", 1)[0] not in names:
                problem(game, f"Localization/{lang}.json: menu.controls dagi [{token}] input xaritasida yo'q")
    return len(actions), len(axes), len(vectors)


def regen(game):
    """7. The generator's output is the committed project, and every committed asset is one it writes."""
    generator = generator_of(game)
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
    every = GAMES + list(GRAPH_GAMES)
    games = [g for g in every if not args.game or g.lower() in {a.lower() for a in args.game}]
    if not games:
        print(f"Noma'lum o'yin: {', '.join(args.game)} (bor: {', '.join(every)})", file=sys.stderr)
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
