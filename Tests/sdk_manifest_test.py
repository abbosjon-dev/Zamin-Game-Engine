#!/usr/bin/env python3
"""Exercise the exported SDK consumer workflow without mutating engine sources."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--build', default=os.environ.get('ZAMIN_BUILD_DIR', str(root / 'build')))
args = p.parse_args()
build = Path(args.build).resolve()
config = (build / 'ZaminConfig.cmake').read_text()
compiler = re.search(r'^CMAKE_CXX_COMPILER:FILEPATH=(.+)$', (build / 'CMakeCache.txt').read_text(), re.M).group(1)
with tempfile.TemporaryDirectory(prefix='zamin-sdk-manifest-') as folder:
    work = Path(folder)
    sdk = work / 'sdk'
    headers = sdk / 'Zamin/src'
    actual = root / 'Zamin/src'
    for source in actual.rglob('*'):
        if source.is_file() and source.suffix in ('.h', '.hpp', '.inl'):
            dest = headers / source.relative_to(actual)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, dest)
    sdk_config = sdk / 'ZaminConfig.cmake'
    copied_config = config.replace(str(actual), str(headers))
    sdk_config.write_text(copied_config)
    project = work / 'game'
    project.mkdir()
    (project / 'empty.cpp').write_text('int sdk_test_source = 0;\n')
    (project / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(SDKManifestCheck LANGUAGES CXX)
find_package(Zamin REQUIRED CONFIG)
zamin_add_game_module(SDKManifestCheck empty.cpp)
''')
    command = ['cmake', '-S', str(project), '-B', str(work / 'build'), '-G', 'Ninja',
               '-DZamin_DIR=' + str(sdk), '-DCMAKE_CXX_COMPILER=' + compiler]
    def configure(success, message=''):
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
        if (result.returncode == 0) != success or (message and message not in result.stdout):
            raise RuntimeError(result.stdout)
    configure(True)
    print('PASS: copied exported SDK config and headers configure a game')
    record = headers / 'Zamin/Scripting/ModuleABIRecord.h'
    original = record.read_bytes()
    record.write_bytes(original + b'\n// SDK header changed after engine build\n')
    configure(False, 'SDK headers changed')
    print('PASS: stale SDK headers are rejected before module build')
    record.write_bytes(original)
    configure(True)
    sdk_config.write_text(re.sub(r'set\(ZAMIN_SDK_ABI_TOOLCHAIN "[^"]*"\)',
                                'set(ZAMIN_SDK_ABI_TOOLCHAIN "OtherCompiler|0|x86_64|8|cxx20")', copied_config))
    configure(False, 'toolchain mismatch')
    print('PASS: compiler/toolchain mismatch has an explicit diagnostic')
