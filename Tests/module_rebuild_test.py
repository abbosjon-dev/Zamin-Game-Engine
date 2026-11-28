#!/usr/bin/env python3
"""An engine header change always rebuilds a game module's object files, and a module linked from a stale one is refused.

Works on a copy of the SDK headers (as sdk_manifest_test.py), never on the engine's own sources:
  1. a two-file module builds against the copy; every object file and the ABI record carry the engine fingerprint
     (ZaminModuleABITests --inspect: 3 translation unit stamps, compatible), and each object lists the generated stamp
     header as an explicit dependency in build.ninja (not only through the compiler's depfile);
  2. a header of the copy changes and its config gets the new fingerprint (what rebuilding the engine does): the next
     build recompiles both files and every stamp in the module is the new fingerprint;
  3. a module linked from the earlier objects plus one object compiled against the changed header (the object file a
     lost dependency record or an edit during a build would leave behind) is refused before it is loaded, with a
     diagnostic that names the stale object count.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def fingerprint(roots, toolchain, definitions):
    """cmake/ModuleABI.cmake's zamin_compute_sdk_fingerprint."""
    manifest = f"zamin-sdk-abi-v1\n{toolchain}\n{definitions}\n"
    for root in roots:
        root = Path(root)
        headers = sorted(str(p.relative_to(root)) for p in root.rglob("*") if p.is_file() and p.suffix in (".h", ".hpp", ".inl"))
        for header in headers:
            manifest += f"{root.name}/{header}:{hashlib.sha256((root / header).read_bytes()).hexdigest()}\n"
    return hashlib.sha256(manifest.encode()).hexdigest()


def stamps(module, work):
    raw = work / "stamps.bin"
    subprocess.run(["objcopy", "-O", "binary", "--only-section=.zamin_abi_tu", str(module), str(raw)], check=True)
    data = raw.read_bytes()
    assert data and len(data) % 128 == 0, f"stamp section size {len(data)}"
    out = []
    for i in range(0, len(data), 128):
        entry = data[i:i + 128]
        assert entry[:16] == b"ZAMIN-SDK-TU-1\0\0", entry[:16]
        out.append(entry[16:80].decode())
    return out


def run(command, **kwargs):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600, **kwargs)
    if result.returncode:
        raise RuntimeError(f"{' '.join(map(str, command))} failed:\n{result.stdout}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default=os.environ.get("ZAMIN_BUILD_DIR", str(ROOT / "build")))
    args = parser.parse_args()
    build = Path(args.build).resolve()
    inspect = build / "bin/ZaminModuleABITests"
    if not inspect.is_file():
        print(f"ZaminModuleABITests not found: {inspect}", file=sys.stderr)
        return 2
    config = (build / "ZaminConfig.cmake").read_text()
    compiler = re.search(r"^CMAKE_CXX_COMPILER:FILEPATH=(.+)$", (build / "CMakeCache.txt").read_text(), re.M).group(1)
    value = lambda name, text: re.search(rf'set\({name} "([^"]*)"\)', text).group(1)
    with tempfile.TemporaryDirectory(prefix="zamin-module-rebuild-") as folder:
        work = Path(folder)
        actual = ROOT / "Zamin/src"
        headers = work / "sdk/Zamin/src"
        for source in actual.rglob("*"):
            if source.is_file() and source.suffix in (".h", ".hpp", ".inl"):
                dest = headers / source.relative_to(actual)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, dest)
        sdk_config = work / "sdk/ZaminConfig.cmake"
        copied = config.replace(str(actual), str(headers))
        sdk_config.write_text(copied)
        roots = value("ZAMIN_SDK_ABI_ROOTS", copied).split(";")
        toolchain, definitions = value("ZAMIN_SDK_ABI_TOOLCHAIN", copied), value("ZAMIN_SDK_ABI_DEFINITIONS", copied)
        engine = value("ZAMIN_SDK_ABI_FINGERPRINT", copied)
        assert fingerprint(roots, toolchain, definitions) == engine, "the test's fingerprint differs from the SDK's"

        project = work / "game"
        (project / "Source").mkdir(parents=True)
        (project / "Source/Probe.cpp").write_text(
            "#include <Zamin/Scripting/ScriptableEntity.h>\n#include <Zamin/Scripting/ScriptEngine.h>\n"
            "class RebuildProbe : public Zamin::ScriptableEntity { public: float Value = 5; };\n"
            "ZM_REGISTER_SCRIPT(RebuildProbe, ZM_FIELD(RebuildProbe, Value))\n")
        (project / "Source/Helper.cpp").write_text(
            "#include <Zamin/Scripting/ScriptableEntity.h>\nint RebuildProbeHelper() { return 7; }\n")
        (project / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\nproject(RebuildProbe LANGUAGES C CXX)\n"
            "find_package(Zamin REQUIRED CONFIG)\n"
            "zamin_add_game_module(RebuildProbe Source/Probe.cpp Source/Helper.cpp)\n")
        tree = work / "build"
        configure = ["cmake", "-S", str(project), "-B", str(tree), "-G", "Ninja", "-DZamin_DIR=" + str(work / "sdk"),
                     "-DCMAKE_CXX_COMPILER=" + compiler, "-DZAMIN_GAME_MODULE_OUTPUT_DIR=" + str(work / "out")]
        run(configure)
        run(["cmake", "--build", str(tree)])
        module = work / "out/libRebuildProbe.so"
        report = subprocess.run([str(inspect), "--inspect", str(module)], text=True, stdout=subprocess.PIPE)
        assert report.returncode == 0 and "units=3 stale=0" in report.stdout, report.stdout
        assert stamps(module, work) == [engine] * 3
        ninja = (tree / "build.ninja").read_text()
        objects = re.findall(r"^build (\S+\.cpp\.o): \S+ \S+ \| (\S+ZaminModuleStamp\.h)", ninja, re.M)
        assert len(objects) == 3, objects
        print("PASS: every object file of the module is stamped and depends on the stamp header explicitly")

        good = work / "good"
        good.mkdir()
        objs = {Path(o).name: tree / o for o, _ in objects}
        for name, path in objs.items():
            shutil.copy2(path, good / name)

        # An engine header changes and the engine is rebuilt: the SDK's fingerprint changes.
        header = headers / "Zamin/Scripting/ScriptableEntity.h"
        header.write_text(header.read_text() + "\n// An engine header change (Tests/module_rebuild_test.py)\n")
        changed = fingerprint(roots, toolchain, definitions)
        assert changed != engine
        sdk_config.write_text(copied.replace(engine, changed))
        output = run(["cmake", "--build", str(tree), "-v"])
        rebuilt = [name for name in objs if re.search(rf"-o \S*{re.escape(name)} -c", output)]
        assert sorted(rebuilt) == sorted(objs), f"rebuilt {rebuilt} of {list(objs)}:\n{output}"
        assert stamps(module, work) == [changed] * 3
        print("PASS: an engine header change recompiles every object file of the module (3 of 3)")

        # A module linked from the old objects and one compiled against the changed header.
        link = run(["ninja", "-C", str(tree), "-t", "commands", "RebuildProbe"]).strip().splitlines()[-1]
        mixed = work / "mixed.so"
        stale = next(name for name in objs if name.startswith("Helper"))
        command = shlex.split(link)  # ": && c++ ... -o <module> <objects> ... && :"
        for i, part in enumerate(command):
            name = Path(part).name
            if name in objs and name != stale:
                command[i] = str(good / name)
            if part == "-o":
                command[i + 1] = str(mixed)
        subprocess.run(["bash", "-c", shlex.join(command).replace("'&&'", "&&")], check=True, cwd=tree)
        report = subprocess.run([str(inspect), "--inspect", str(mixed)], text=True, stdout=subprocess.PIPE)
        assert report.returncode == 1 and "units=3 stale=1" in report.stdout, report.stdout
        assert "built against other engine headers" in report.stdout, report.stdout
        print("PASS: a module linked with a stale object file is refused: " + report.stdout.strip().splitlines()[-1])
    return 0


if __name__ == "__main__":
    sys.exit(main())
