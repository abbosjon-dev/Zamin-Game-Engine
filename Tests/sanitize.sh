#!/usr/bin/env bash
# Sanitizer test: builds the engine and the unit tests with a sanitizer (cmake -DZAMIN_SANITIZE=..., a build
# folder of its own) and runs ZaminTests under it; with --smoke also the player (the three sample games) and the
# editor headless under xvfb. A run fails on any sanitizer report in our code - and on a vendor report that
# Tests/sanitizers/*.supp does not list - not only on a failed test.
#   Tests/sanitize.sh address               ASan (+ LeakSanitizer at exit): build-asan
#   Tests/sanitize.sh undefined             UBSan, every report in our code fatal: build-ubsan
#   Tests/sanitize.sh leak                  standalone LeakSanitizer (no ASan): build-lsan
#   Tests/sanitize.sh thread                ThreadSanitizer: build-tsan
#   Tests/sanitize.sh address --smoke       also ZaminPlayer (Platformer2D, Showcase3D, Adventure3D) and ZaminEditor
#   Tests/sanitize.sh address --editor-smoke   also the editor smoke test (Tests/editor_smoke.sh: every window, menu,
#                                           popup and inspector of Showcase3D and Platformer2D) under the sanitizer
#   Tests/sanitize.sh undefined --compiler clang --build-dir build-ubsan-clang
#   Tests/sanitize.sh address --filter Prefab   only the tests whose names contain the text (ZAMIN_TESTS)
#   Tests/sanitize.sh address --no-build    run what is built
#   Tests/sanitize.sh address --clean       delete the build folder afterwards (it is large)
# Options: --build-type Debug|RelWithDebInfo (Debug), --jobs N, --frames N (smoke frames, 60), --log-dir DIR
# (<build>/SanitizerLogs; <build>-logs with --clean). ZAMIN_CMAKE_ARGS: more arguments for the first configure.
# <log dir>/summary.md lists every run (also on the GitHub Actions run's page when GITHUB_STEP_SUMMARY is set).
# Exit code: 0 clean, 1 a test failed or a sanitizer reported, 2 bad usage / the build failed.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
supp="$root/Tests/sanitizers"

usage() { sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }
if [[ $# -lt 1 ]]; then usage; exit 2; fi
mode="$1"; shift
case "$mode" in
    address|asan) mode=address; short=asan ;;
    undefined|ubsan) mode=undefined; short=ubsan ;;
    leak|lsan) mode=leak; short=lsan ;;
    thread|tsan) mode=thread; short=tsan ;;
    -h|--help) usage; exit 0 ;;
    *) echo "noma'lum sanitizer: $mode (address, undefined, leak, thread)" >&2; exit 2 ;;
esac

build=""
compiler=""
build_type=Debug
jobs="$(nproc 2>/dev/null || echo 4)"
smoke=0
editor_smoke=0
do_build=1
clean=0
frames=60
filter=""
log_dir=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) build="$2"; shift 2 ;;
        --compiler) compiler="$2"; shift 2 ;;
        --build-type) build_type="$2"; shift 2 ;;
        --jobs) jobs="$2"; shift 2 ;;
        --smoke) smoke=1; shift ;;
        --editor-smoke) editor_smoke=1; shift ;;
        --no-build) do_build=0; shift ;;
        --clean) clean=1; shift ;;
        --frames) frames="$2"; shift 2 ;;
        --filter) filter="$2"; shift 2 ;;
        --log-dir) log_dir="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "noma'lum variant: $1 (--help)" >&2; exit 2 ;;
    esac
done
[[ -z "$build" ]] && build="$root/build-$short"
[[ "$build" != /* ]] && build="$root/$build"
# The logs stay when --clean deletes the build folder.
if [[ -z "$log_dir" ]]; then
    log_dir="$build/SanitizerLogs"
    [[ $clean -eq 1 ]] && log_dir="$build-logs"
fi

# ---- Clang's sanitizer runtimes are a package of their own on Debian / Ubuntu (libclang-rt-<N>-dev) ------------
if [[ "$compiler" == clang ]] && command -v clang >/dev/null 2>&1; then
    case "$mode" in
        address) rt=asan ;;
        undefined) rt=ubsan_standalone ;;
        leak) rt=lsan ;;
        thread) rt=tsan ;;
    esac
    rt_dir="$(clang --print-runtime-dir 2>/dev/null)"
    if [[ -n "$rt_dir" && ! -e "$rt_dir/libclang_rt.$rt-$(uname -m).a" && ! -e "$rt_dir/libclang_rt.$rt.a" ]]; then
        clang_major="$(clang -dumpversion | cut -d. -f1)"
        echo "Clang'ning $mode sanitizer runtime'i topilmadi ($rt_dir/libclang_rt.$rt-$(uname -m).a):" >&2
        echo "  sudo apt-get install libclang-rt-$clang_major-dev" >&2
        exit 2
    fi
fi

# ---- configure + build ----------------------------------------------------------------------------------------
if [[ $do_build -eq 1 ]]; then
    if [[ ! -f "$build/CMakeCache.txt" ]]; then
        cc=()
        case "$compiler" in
            "") ;;
            gcc) cc=(-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++) ;;
            clang) cc=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++) ;;
            *) echo "noma'lum kompilyator: $compiler (gcc, clang)" >&2; exit 2 ;;
        esac
        generator=()
        command -v ninja >/dev/null 2>&1 && generator=(-G Ninja)
        echo "== Sozlash: $build ($mode, $build_type${compiler:+, $compiler})"
        # ZAMIN_CMAKE_ARGS: more cmake arguments (e.g. FETCHCONTENT_SOURCE_DIR_<DEP>=... to reuse downloaded sources).
        read -r -a extra_cmake <<< "${ZAMIN_CMAKE_ARGS:-}"
        if ! cmake -S "$root" -B "$build" "${generator[@]}" "${cc[@]}" -DCMAKE_BUILD_TYPE="$build_type" \
                -DZAMIN_SANITIZE="$mode" "${extra_cmake[@]}" >"$build.configure.log" 2>&1; then
            cat "$build.configure.log" >&2
            echo "cmake sozlash muvaffaqiyatsiz" >&2
            exit 2
        fi
        rm -f "$build.configure.log"
    fi
    targets=(ZaminTests)
    [[ $smoke -eq 1 ]] && targets+=(ZaminPlayer ZaminEditor Platformer2D Showcase3D Adventure3D)
    [[ $smoke -eq 0 && $editor_smoke -eq 1 ]] && targets+=(ZaminEditor Platformer2D Showcase3D)
    echo "== Build: ${targets[*]}"
    if ! cmake --build "$build" --parallel "$jobs" --target "${targets[@]}"; then
        echo "build muvaffaqiyatsiz" >&2
        exit 2
    fi
fi
if [[ ! -x "$build/bin/ZaminTests" ]]; then
    echo "ZaminTests topilmadi: $build/bin (--no-build siz ishga tushiring)" >&2
    exit 2
fi
mkdir -p "$log_dir"

# ---- sanitizer runtime options ----------------------------------------------------------------------------------
# halt_on_error: ASan stops at the first error; UBSan reports in our code are fatal by build flags
# (-fno-sanitize-recover=all), the vendor libraries' reports are recoverable - listed in ubsan.supp, or this
# script fails the run on their "runtime error:" lines.
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=0:strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1:suppressions=$supp/asan.supp:print_suppressions=0"
export LSAN_OPTIONS="suppressions=$supp/lsan.supp:print_suppressions=0:report_objects=0"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0:report_error_type=1:suppressions=$supp/ubsan.supp:print_suppressions=0"
export TSAN_OPTIONS="halt_on_error=1:second_deadlock_stack=1:suppressions=$supp/tsan.supp:print_suppressions=0"
# The sanitizer build's own (instrumented) script modules (cmake/Sanitizers.cmake).
export ZAMIN_SCRIPT_MODULE_DIR="$build/GameModules"

failures=0
rows=()
# check <name> <exit code> <log> <seconds>: a failed run, a sanitizer report or an unsuppressed vendor report.
check() {
    local name="$1" code="$2" log="$3" secs="${4:-?}" reports
    reports="$(grep -cE "ERROR: (AddressSanitizer|LeakSanitizer)|WARNING: ThreadSanitizer|runtime error:|SUMMARY: (Address|Leak|Thread|UndefinedBehavior)Sanitizer" "$log")"
    if [[ "$code" -ne 0 || "$reports" -ne 0 ]]; then
        echo "[XATO] $name: chiqish kodi $code, sanitizer xabarlari $reports - $log"
        grep -E -m 20 -A 12 "ERROR: (AddressSanitizer|LeakSanitizer)|WARNING: ThreadSanitizer|runtime error:" "$log" | head -n 80
        failures=$((failures + 1))
        rows+=("| $name | XATO (kod $code, $reports ta xabar) | $secs s | \`$(basename "$log")\` |")
    else
        echo "[OK] $name ($log)"
        rows+=("| $name | OK | $secs s | \`$(basename "$log")\` |")
    fi
}

# ---- unit tests -------------------------------------------------------------------------------------------------
echo "== ZaminTests ($mode)"
test_log="$log_dir/ZaminTests.log"
start=$(date +%s)
(cd "$root" && ZAMIN_TESTS="$filter" "$build/bin/ZaminTests") >"$test_log" 2>&1
code=$?
tail -n 1 "$test_log"
secs=$(( $(date +%s) - start ))
echo "   $secs s"
if [[ $code -eq 0 ]] && ! tail -n 1 "$test_log" | grep -qE "^[0-9]+ test, 0 xato$"; then
    code=1
fi
check "ZaminTests ($(tail -n 1 "$test_log"))" "$code" "$test_log" "$secs"

# ---- headless smoke: the player (the sample games) and the editor -----------------------------------------------
if [[ $smoke -eq 1 || $editor_smoke -eq 1 ]]; then
    runner=()
    if [[ -z "${DISPLAY:-}" ]]; then
        if ! command -v xvfb-run >/dev/null 2>&1; then
            echo "xvfb-run yo'q: smoke o'tkazib yuborildi" >&2
            exit 2
        fi
        runner=(xvfb-run -a -s "-screen 0 1600x900x24")
    fi
    export HOME="$log_dir/home"  # fresh settings / logs, the user's are left alone
    mkdir -p "$HOME"
fi
if [[ $smoke -eq 1 ]]; then
    # Mesa / X11 / fontconfig / ALSA keep caches and singletons until exit: lsan.supp lists them.
    for game in Platformer2D Showcase3D Adventure3D; do
        log="$log_dir/Player-$game.log"
        echo "== ZaminPlayer $game"
        start=$(date +%s)
        (cd "$root" && ZAMIN_MAX_FRAMES="$frames" "${runner[@]}" "$build/bin/ZaminPlayer" "Projects/$game") >"$log" 2>&1
        check "ZaminPlayer $game" $? "$log" $(( $(date +%s) - start ))
    done
    log="$log_dir/Editor-Showcase3D.log"
    echo "== ZaminEditor Showcase3D"
    start=$(date +%s)
    (cd "$root" && ZAMIN_MAX_FRAMES="$frames" "${runner[@]}" "$build/bin/ZaminEditor" Projects/Showcase3D) >"$log" 2>&1
    check "ZaminEditor Showcase3D" $? "$log" $(( $(date +%s) - start ))
fi
# ---- the editor smoke test (every window / menu / popup / inspector) under the sanitizer --------------------------
if [[ $editor_smoke -eq 1 ]]; then
    smoke_dir="$log_dir/EditorSmoke"
    echo "== Editor smoke testi (Showcase3D, Platformer2D)"
    start=$(date +%s)
    ZAMIN_BUILD_DIR="$build" "$root/Tests/editor_smoke.sh" --out "$smoke_dir" "$root/Projects/Showcase3D" "$root/Projects/Platformer2D" \
        >"$log_dir/EditorSmoke.log" 2>&1
    code=$?
    grep -E "\[(OK|XATO)\]" "$log_dir/EditorSmoke.log" | sed 's/^ */   /'
    # The script's own verdict (exit code) and every project's editor log searched for sanitizer reports.
    reports_log="$log_dir/EditorSmoke-all.log"
    cat "$log_dir/EditorSmoke.log" "$smoke_dir"/*.log >"$reports_log" 2>/dev/null
    check "Editor smoke testi" "$code" "$reports_log" $(( $(date +%s) - start ))
fi

echo
if [[ $failures -eq 0 ]]; then
    verdict="Sanitizer ($mode): xato yo'q"
else
    verdict="Sanitizer ($mode): $failures ta xato"
fi
echo "$verdict"
{
    echo "### $verdict"
    echo
    echo "Build: \`$(basename "$build")\` ($build_type${compiler:+, $compiler}), $(date -u '+%Y-%m-%d %H:%M UTC')"
    echo
    echo "| Ishga tushirish | Natija | Vaqt | Log |"
    echo "|---|---|---|---|"
    printf '%s\n' "${rows[@]}"
} >"$log_dir/summary.md"
[[ -n "${GITHUB_STEP_SUMMARY:-}" ]] && cat "$log_dir/summary.md" >>"$GITHUB_STEP_SUMMARY"
echo "Hisobot: $log_dir/summary.md"
if [[ $clean -eq 1 ]]; then
    rm -rf "$build"
    echo "build papkasi o'chirildi: $build"
fi
[[ $failures -eq 0 ]]
