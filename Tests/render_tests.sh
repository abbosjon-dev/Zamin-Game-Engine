#!/usr/bin/env bash
# Rendering regression test (Tests/RenderTests.cpp): renders the reference scenes offscreen and compares
# them with Tests/Reference/*.png - the same way locally and in CI (.github/workflows/linux.yml calls this).
#   Tests/render_tests.sh                 compare; a failed case leaves <name>.png / .diff.png / .compare.png
#   Tests/render_tests.sh --save-all      the pictures of every case (CI artefacts), not only the failed ones
#   Tests/render_tests.sh --update        write new references after an intended change of the look
#   Tests/render_tests.sh --build         build ZaminRenderTests first
#   Tests/render_tests.sh --out DIR       the output folder (default <build>/RenderTests): report.json, summary.md
#   ZAMIN_BUILD_DIR=build-clang Tests/render_tests.sh   another build (another compiler)
# Any other arguments go to ZaminRenderTests (--filter, --threshold, --max-percent, --list ...).
# Stable pictures: the references are Mesa's software rasteriser (llvmpipe) at a fixed size, so this always
# renders with it (LIBGL_ALWAYS_SOFTWARE=1, GALLIUM_DRIVER=llvmpipe) - on a desktop with a GPU too - and,
# without a display (CI, SSH), in a fixed 1280x720x24 xvfb screen. In GitHub Actions the summary table goes
# to the run's page ($GITHUB_STEP_SUMMARY). Exit code: ZaminRenderTests' (0: every case matched), 2: missing.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ZAMIN_BUILD_DIR:-$root/build}"
[[ "$build" != /* ]] && build="$root/$build"
exe="$build/bin/ZaminRenderTests"
out="$build/RenderTests"
do_build=0
args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build) do_build=1; shift ;;
        --out) out="$2"; shift 2 ;;
        -h|--help) sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) args+=("$1"); shift ;;
    esac
done
[[ "$out" != /* ]] && out="$PWD/$out"
if [[ $do_build -eq 1 ]]; then
    cmake --build "$build" --target ZaminRenderTests || exit 2
fi
if [[ ! -x "$exe" ]]; then
    echo "ZaminRenderTests topilmadi: $exe (avval: cmake --build \"$build\" --target ZaminRenderTests yoki --build)" >&2
    exit 2
fi
cd "$root"
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
runner=()
if [[ -z "${DISPLAY:-}" ]]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        echo "Displey yo'q va xvfb-run topilmadi (apt install xvfb)" >&2
        exit 2
    fi
    runner=(xvfb-run -a -s "-screen 0 1280x720x24 -nolisten tcp")
fi
"${runner[@]}" "$exe" --out "$out" "${args[@]}"
code=$?
if [[ -f "$out/summary.md" ]]; then
    echo "Hisobot: $out/report.json, $out/summary.md"
    if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
        { cat "$out/summary.md"; echo; } >> "$GITHUB_STEP_SUMMARY"
    fi
fi
exit $code
