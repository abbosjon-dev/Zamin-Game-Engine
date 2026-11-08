#!/usr/bin/env bash
# Code coverage report (gcov): a coverage build of its own (build-cov: -DZAMIN_COVERAGE=ON, cmake/Coverage.cmake - our
# targets only, -O0), the unit tests run in it, and Tests/coverage_report.py sums gcov's data up per module (the
# engine's folders, Editor, Player, the sample games): lines and functions that ran, the files with the most lines
# that never did. Optionally also the player (the three sample games, headless) and the editor smoke test under
# xvfb, so that the editor's and the renderer's code count too.
#   Tests/coverage.sh                     build-cov, ZaminTests, the report in build-cov/Coverage
#   Tests/coverage.sh --gui               + ZaminPlayer (3 sample games) and Tests/editor_smoke.sh in the same build
#   Tests/coverage.sh --filter "Prefab*"  only some tests (ZaminTests --filter)
#   Tests/coverage.sh --html              also an HTML report per file (gcovr, when it is installed: pip install gcovr)
#   Tests/coverage.sh --no-build          the build as it is (counters reset, the tests run again)
#   Tests/coverage.sh --report-only       only the report again, from the counters the last run left
#   Tests/coverage.sh --build DIR --out DIR --min-engine 50 --clean
# --min-engine X: fails when the engine's line coverage is below X percent. --clean deletes the build folder at
# the end (it takes about 1 GB; the report is copied out first with --out). Exit code: 0 - report written (and the
# minimum kept), 1 - a test failed / below the minimum, 2 - configure / build failed.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$root/build-cov"
out=""
filter=""
gui=0
html=0
do_build=1
do_run=1
clean=0
min_engine=0
compiler=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build) build="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --filter) filter="$2"; shift 2 ;;
        --gui) gui=1; shift ;;
        --html) html=1; shift ;;
        --no-build) do_build=0; shift ;;
        --report-only) do_build=0; do_run=0; shift ;;
        --clean) clean=1; shift ;;
        --min-engine) min_engine="$2"; shift 2 ;;
        --compiler) compiler="$2"; shift 2 ;;
        -h|--help) sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Noma'lum parametr: $1 (--help)" >&2; exit 2 ;;
    esac
done
[[ "$build" != /* ]] && build="$PWD/$build"
out="${out:-$build/Coverage}"
[[ "$out" != /* ]] && out="$PWD/$out"
gcov_tool="gcov"
if [[ "$compiler" == clang ]]; then
    export CC=clang CXX=clang++
    gcov_tool="llvm-cov gcov"
    command -v llvm-cov >/dev/null 2>&1 || gcov_tool="$(ls /usr/bin/llvm-cov-* 2>/dev/null | sort -V | tail -n 1) gcov"
fi

if [[ $do_build -eq 1 ]]; then
    targets=(ZaminTests)
    editor=OFF; runtime=OFF; examples=OFF
    if [[ $gui -eq 1 ]]; then
        targets+=(ZaminPlayer ZaminEditor Platformer2D Showcase3D Adventure3D)
        editor=ON; runtime=ON; examples=ON
    fi
    echo "== Coverage build: $build (${targets[*]})"
    # The particle files are compiled with -O2 in Debug by default: not here (true line counts).
    if ! cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DZAMIN_COVERAGE=ON \
            -DZAMIN_OPTIMIZE_PARTICLES_IN_DEBUG=OFF -DZAMIN_BUILD_EDITOR=$editor -DZAMIN_BUILD_RUNTIME=$runtime \
            -DZAMIN_BUILD_EXAMPLES=$examples >"$build.configure.log" 2>&1; then
        tail -n 30 "$build.configure.log"
        exit 2
    fi
    rm -f "$build.configure.log"
    cmake --build "$build" --target "${targets[@]}" >"$build/coverage-build.log" 2>&1
    code=$?
    grep -E "error|warning|FAILED" "$build/coverage-build.log" | head -n 30
    if [[ $code -ne 0 ]]; then
        echo "Build xato bilan tugadi (log: $build/coverage-build.log)" >&2
        exit 2
    fi
fi
if [[ ! -x "$build/bin/ZaminTests" ]]; then
    echo "ZaminTests topilmadi: $build/bin (--no-build siz ishga tushiring)" >&2
    exit 2
fi

mkdir -p "$out"
failures=0
if [[ $do_run -eq 1 ]]; then
    # Every run starts from zero (the counters add up across runs otherwise).
    find "$build" -name '*.gcda' -not -path '*/_deps/*' -delete

    echo "== ZaminTests (coverage)"
    test_args=(--timing --slowest 5)
    [[ -n "$filter" ]] && test_args+=(--filter "$filter")
    (cd "$root" && "$build/bin/ZaminTests" "${test_args[@]}") >"$out/ZaminTests.log" 2>&1
    code=$?
    tail -n 12 "$out/ZaminTests.log"
    if [[ $code -ne 0 ]]; then
        echo "ZaminTests yiqildi (chiqish kodi $code, log: $out/ZaminTests.log)"
        grep -E "FAIL" "$out/ZaminTests.log" | head -n 20
        failures=$((failures + 1))
    fi

    if [[ $gui -eq 1 ]]; then
        runner=()
        if [[ -z "${DISPLAY:-}" ]]; then
            runner=(xvfb-run -a -s "-screen 0 1600x900x24 -nolisten tcp")
        fi
        home="$(mktemp -d)"
        for game in Platformer2D Showcase3D Adventure3D; do
            echo "== ZaminPlayer $game (coverage, 120 kadr)"
            (cd "$root" && HOME="$home" ZAMIN_MAX_FRAMES=120 ZAMIN_SCRIPT_MODULE_DIR="$build/GameModules" \
                "${runner[@]}" "$build/bin/ZaminPlayer" "Projects/$game") >"$out/Player-$game.log" 2>&1 \
                || { echo "ZaminPlayer $game yiqildi (log: $out/Player-$game.log)"; failures=$((failures + 1)); }
        done
        rm -rf "$home"
        echo "== Editor smoke testi (coverage)"
        if ! ZAMIN_BUILD_DIR="$build" ZAMIN_SCRIPT_MODULE_DIR="$build/GameModules" "$root/Tests/editor_smoke.sh" --out "$out/editor-smoke" \
                >"$out/EditorSmoke.log" 2>&1; then
            echo "Editor smoke testi yiqildi (log: $out/EditorSmoke.log)"
            tail -n 20 "$out/EditorSmoke.log"
            failures=$((failures + 1))
        fi
    fi
fi # do_run

echo "== Hisobot (gcov: $gcov_tool)"
report_args=("$build" --root "$root" --out "$out" --gcov "$gcov_tool")
[[ "$min_engine" != 0 ]] && report_args+=(--min-engine "$min_engine")
python3 "$root/Tests/coverage_report.py" "${report_args[@]}" || failures=$((failures + 1))

if [[ $html -eq 1 ]]; then
    gcovr_cmd=()
    if gcovr --version >/dev/null 2>&1; then
        gcovr_cmd=(gcovr)
    elif python3 -m gcovr --version >/dev/null 2>&1; then
        gcovr_cmd=(python3 -m gcovr)
    fi
    if [[ ${#gcovr_cmd[@]} -gt 0 ]]; then
        mkdir -p "$out/html"
        echo "== HTML (gcovr): $out/html/index.html"
        "${gcovr_cmd[@]}" --root "$root" --object-directory "$build" --gcov-executable "$gcov_tool" \
            --filter "$root/Zamin/src/" --filter "$root/Editor/src/" --filter "$root/Runtime/src/" --filter "$root/Projects/" \
            --exclude '.*/_deps/.*' --gcov-ignore-errors=no_working_dir_found --gcov-ignore-parse-errors=all \
            --html-details "$out/html/index.html" -j 4 >"$out/gcovr.log" 2>&1 \
            || { echo "gcovr xatosi (log: $out/gcovr.log)"; tail -n 5 "$out/gcovr.log"; }
    else
        echo "gcovr o'rnatilmagan (pip install gcovr): HTML hisobot yo'q, Markdown / JSON hisobot bor"
    fi
fi

if [[ $clean -eq 1 ]]; then
    rm -rf "$build"
    echo "Build papkasi o'chirildi: $build"
fi
echo "Hisobot: $out/coverage-summary.md"
[[ $failures -eq 0 ]] && exit 0 || exit 1
