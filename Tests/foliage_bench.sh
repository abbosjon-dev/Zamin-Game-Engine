#!/usr/bin/env bash
# Dense packed foliage benchmark: ZaminBench --foliage (Tests/FoliageBench.cpp) - ~1M instances in 16 packed
# components, CPU culling against GPU compute culling + indirect draws, with and without HLOD: frame time (CPU and
# with glFinish), culling time (CPU; GPU timestamp queries), draw calls, instances per LOD, the shadow set and HLOD
# cells. A GL context is needed: under xvfb (a fixed 1600x900x24 screen) when there is no display, and always with
# Mesa's software rasteriser (llvmpipe) - so the numbers compare between machines and with docs/Benchmarks.md; they
# are NOT a physical GPU's (ZAMIN_RENDER_BENCH_GPU=1 for the real one).
#   Tests/foliage_bench.sh                         every case, 1 048 576 instances, 60 frames, 1280x720
#   Tests/foliage_bench.sh --instances 262144 --frames 20
#   Tests/foliage_bench.sh --case gpu              only the cases whose name has this
#   Tests/foliage_bench.sh --shots DIR [--lod-colors]   each case's last frame as a PNG (LOD/HLOD debug colours)
#   Tests/foliage_bench.sh --compare               compare with Tests/Baselines/foliage-<build type>.json (bench_compare.py)
#   ZAMIN_BUILD_DIR=build-release Tests/foliage_bench.sh    a Release build (the numbers that matter)
# The report: <build>/FoliageBench/foliage-bench.json (--out DIR for another folder). Exit code: 0 when every case
# ran and the CPU and GPU paths ended with the same counts, 1 otherwise, 2 when ZaminBench is missing.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ZAMIN_BUILD_DIR:-$root/build}"
[[ "$build" != /* ]] && build="$root/$build"
bench="$build/bin/ZaminBench"
out="$build/FoliageBench"
compare=0
args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) out="$2"; shift 2 ;;
        --compare) compare=1; shift ;;
        -h|--help) sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) args+=("$1"); shift ;;
    esac
done
[[ "$out" != /* ]] && out="$PWD/$out"
if [[ ! -x "$bench" ]]; then
    echo "ZaminBench not found: $bench (first: cmake --build \"$build\" --target ZaminBench)" >&2
    exit 2
fi
mkdir -p "$out" || exit 2
[[ -z "${ZAMIN_RENDER_BENCH_GPU:-}" ]] && export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
runner=()
if [[ -z "${DISPLAY:-}" ]]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        echo "No display and xvfb-run was not found (apt install xvfb)" >&2
        exit 2
    fi
    runner=(xvfb-run -a -s "-screen 0 1600x900x24 -nolisten tcp")
fi
report="$out/foliage-bench.json"
rm -f "$report"
home="${ZAMIN_BENCH_HOME:-$(mktemp -d)}"
HOME="$home" "${runner[@]}" "$bench" --foliage --json "$report" "${args[@]}" 2>&1 \
    | sed 's/\x1b\[[0-9;]*m//g' | grep -vE '^\[[0-9:]+\] (ZAMIN|APP): |^ALSA|^Cannot connect|jack|Jack|XDG_RUNTIME_DIR'
code=${PIPESTATUS[0]}
[[ -z "${ZAMIN_BENCH_HOME:-}" ]] && rm -rf "$home"
if [[ $code -ne 0 || ! -f "$report" ]]; then
    echo "The foliage benchmark failed (exit code $code)"
    exit 1
fi
if [[ $compare -eq 1 ]]; then
    type="$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['Build'].lower())" "$report")"
    python3 "$root/Tests/bench_compare.py" "$root/Tests/Baselines/foliage-$type.json" "$report" || exit 1
fi
exit 0
