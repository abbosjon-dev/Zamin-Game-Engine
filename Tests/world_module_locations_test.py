#!/usr/bin/env python3
"""Camera-tour native module discovery across supported CMake output layouts."""
from pathlib import Path
import tempfile
from world_streaming_test import find_module

with tempfile.TemporaryDirectory(prefix="zamin-module-locations-") as temp:
    root = Path(temp)
    build, project = root / "build", root / "project"
    build.mkdir()
    (project / "Binaries").mkdir(parents=True)
    name = "WorldWorkshop3D"
    filename = "lib" + name + ".so"
    default = project / "Binaries" / filename
    default.write_bytes(b"default")
    assert find_module(build, project, name) == default
    isolated = build / "GameModules" / filename
    isolated.parent.mkdir()
    isolated.write_bytes(b"isolated")
    assert find_module(build, project, name) == isolated
    custom = root / "custom modules" / filename
    custom.parent.mkdir()
    custom.write_bytes(b"custom")
    (build / "CMakeCache.txt").write_text("ZAMIN_GAME_MODULE_OUTPUT_DIR:PATH=" + str(custom.parent) + "\n")
    assert find_module(build, project, name) == custom
    relative = build / "local modules" / filename
    relative.parent.mkdir()
    relative.write_bytes(b"relative")
    (build / "CMakeCache.txt").write_text("ZAMIN_GAME_MODULE_OUTPUT_DIR:PATH=local modules\n")
    assert find_module(build, project, name) == relative
    (build / "CMakeCache.txt").write_text("ZAMIN_GAME_MODULE_OUTPUT_DIR:PATH=/missing\n")
    assert find_module(build, project, name) == isolated
    isolated.unlink()
    default.unlink()
    try:
        find_module(build, project, name)
    except RuntimeError as error:
        assert filename in str(error)
    else:
        raise AssertionError("Missing module silently accepted")
print("PASS native module locations: default, isolated, custom absolute/relative, missing")
