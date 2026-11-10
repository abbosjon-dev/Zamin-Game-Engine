#!/usr/bin/env python3
"""Exercise the engine's real shared-library link policy with tiny Clang DSOs."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def run(args, *, ok=True, **kwargs):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, **kwargs)
    if ok and result.returncode:
        raise RuntimeError(result.stdout)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    args = parser.parse_args()
    runtime = run([args.compiler, '--print-runtime-dir']).stdout.strip()
    if not all(list(Path(runtime).glob('libclang_rt.' + name + '*.a'))
               for name in ['asan', 'ubsan_standalone']):
        print('SKIP: Clang compiler-rt ASan/UBSan libraries are not installed')
        return 77
    repo = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix='zamin-sanitizer-link-') as temporary:
        root = Path(temporary)
        (root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.22)
project(SanitizerLinkPolicy LANGUAGES CXX)
include("${ENGINE_SOURCE}/cmake/Sanitizers.cmake")
include("${ENGINE_SOURCE}/cmake/ZaminGame.cmake")
add_library(Probe SHARED probe.cpp)
zamin_target_settings(Probe)
zamin_check_shared_library_symbols(Probe)
if(ZAMIN_SANITIZE)
    add_executable(Consumer main.cpp)
    zamin_target_settings(Consumer)
    target_link_libraries(Consumer PRIVATE Probe)
endif()
''')
        (root / 'main.cpp').write_text('''#include <climits>
extern "C" int compute(int);
int main(int argc, char**) { return compute(argc > 1 ? INT_MAX : 1) == 2 ? 0 : 1; }
''')
        (root / 'probe.cpp').write_text('extern "C" int compute(int x) { return x + 1; }\n')
        for mode in ['undefined', 'address', 'address,undefined']:
            build = root / mode.replace(',', '-')
            run(['cmake', '-S', str(root), '-B', str(build), '-G', 'Ninja',
                 '-DCMAKE_CXX_COMPILER=' + args.compiler, '-DENGINE_SOURCE=' + str(repo),
                 '-DZAMIN_SANITIZE=' + mode])
            run(['cmake', '--build', str(build), '--parallel', '2'])
            env = dict(os.environ, UBSAN_OPTIONS='halt_on_error=1')
            run([str(build / 'Consumer')], env=env)
            if 'undefined' in mode:
                bad = run([str(build / 'Consumer'), 'overflow'], ok=False, env=env)
                assert bad.returncode and 'signed integer overflow' in bad.stdout, bad.stdout
            print(f'PASS Clang {mode}: DSO links, consumer runs, sanitizer remains enabled')
        # Regular builds must still reject accidental unresolved engine dependencies.
        (root / 'probe.cpp').write_text('extern void missing_dependency();\nvoid call() { missing_dependency(); }\n')
        build = root / 'regular'
        run(['cmake', '-S', str(root), '-B', str(build), '-G', 'Ninja',
             '-DCMAKE_CXX_COMPILER=' + args.compiler, '-DENGINE_SOURCE=' + str(repo)])
        failed = run(['cmake', '--build', str(build), '--parallel', '2'], ok=False)
        assert failed.returncode and 'missing_dependency' in failed.stdout, failed.stdout
        print('PASS regular Clang: unresolved application symbols still rejected')


if __name__ == '__main__':
    raise SystemExit(main())
