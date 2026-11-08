#!/usr/bin/env bash
# Rendering benchmark: ZaminBench --render (Tests/RenderBench.cpp) - 10 000 sprites, 1 000 meshes (with and without
# GPU instancing) and both in one scene, drawn the way the player draws them, ms per frame on the CPU and with the
# GPU (glFinish). A GL context is needed: under xvfb (a fixed 1600x900x24 screen) when there is no display, and
# always with Mesa's software rasteriser (llvmpipe), so that the numbers compare between machines and with the
# baseline in docs/Benchmarks.md (a GPU: ZAMIN_RENDER_BENCH_GPU=1).
#   Tests/render_bench.sh                          every case, 120 frames, 1280x720
#   Tests/render_bench.sh --sprites 20000 --meshes 2000 --frames 300
#   Tests/render_bench.sh --case meshes            only the cases whose name has this
#   Tests/render_bench.sh --shots DIR              each case's last frame as a PNG
#   Tests/render_bench.sh --max-avg 250            a case slower than this (average ms, with the GPU) fails it
#   ZAMIN_BUILD_DIR=build-release Tests/render_bench.sh    a Release build (the numbers that matter)
# The report: <build>/RenderBench/render-bench.json (--out DIR for another folder). Exit code: 0 when every case
# ran (and kept within --max-avg), 1 otherwise, 2 when ZaminBench is missing.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ZAMIN_BUILD_DIR:-$root/build}"
[[ "$build" != /* ]] && build="$root/$build"
bench="$build/bin/ZaminBench"
out="$build/RenderBench"
max_avg=""
args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) out="$2"; shift 2 ;;
        --max-avg) max_avg="$2"; shift 2 ;;
        -h|--help) sed -n '2,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) args+=("$1"); shift ;;
    esac
done
[[ "$out" != /* ]] && out="$PWD/$out"
if [[ ! -x "$bench" ]]; then
    echo "ZaminBench topilmadi: $bench (avval: cmake --build \"$build\" --target ZaminBench)" >&2
    exit 2
fi
mkdir -p "$out" || exit 2
[[ -z "${ZAMIN_RENDER_BENCH_GPU:-}" ]] && export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
runner=()
if [[ -z "${DISPLAY:-}" ]]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        echo "Displey yo'q va xvfb-run topilmadi (apt install xvfb)" >&2
        exit 2
    fi
    runner=(xvfb-run -a -s "-screen 0 1600x900x24 -nolisten tcp")
fi
report="$out/render-bench.json"
rm -f "$report"
# (The engine's own log lines out: the table only.)
home="${ZAMIN_BENCH_HOME:-$(mktemp -d)}"
HOME="$home" "${runner[@]}" "$bench" --render --json "$report" "${args[@]}" 2>&1 \
    | sed 's/\x1b\[[0-9;]*m//g' | grep -vE '^\[[0-9:]+\] (ZAMIN|APP): |^ALSA|^Cannot connect|jack|Jack|XDG_RUNTIME_DIR'
code=${PIPESTATUS[0]}
[[ -z "${ZAMIN_BENCH_HOME:-}" ]] && rm -rf "$home"
if [[ $code -ne 0 || ! -f "$report" ]]; then
    echo "Render benchmark ishlamadi (chiqish kodi $code)"
    exit 1
fi
if [[ -n "$max_avg" ]]; then
    slow="$(python3 - "$report" "$max_avg" <<'EOF'
import json, sys
report, limit = json.load(open(sys.argv[1])), float(sys.argv[2])
print(" ".join(f"{c['Name']}={c['FrameMs']['Average']:.1f}" for c in report["Cases"] if c["FrameMs"]["Average"] > limit))
EOF
)"
    if [[ -n "$slow" ]]; then
        echo "Chegaradan ($max_avg ms) sekin: $slow"
        exit 1
    fi
fi
exit 0
