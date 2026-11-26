#!/usr/bin/env bash
# Player performance test (headless): runs the sample games' start scenes in ZaminPlayer --benchmark under
# xvfb (without a display) and prints how long their frames take - average, median, p95, p99 (ms), FPS.
# Each run writes its JSON report (every frame's time too) to build/PlayerBench/<project>.json.
#   Tests/player_bench.sh                         the three samples, 300 frames each after 60 warm-up frames
#   Tests/player_bench.sh --frames 600 --warmup 120
#   Tests/player_bench.sh --project Projects/Showcase3D --scene Scenes/Showcase.zscene
#   Tests/player_bench.sh --max-avg 50 --max-p95 80    limits (ms): a run over them fails (exit code 1)
#   Tests/player_bench.sh --resolution 1920x1080 --out /tmp/bench
# Other options go to ZaminPlayer as they are (--quality Low, --lang en, --msaa 4 ...). The players run with a
# fresh HOME (no saved settings, logs in it; ZAMIN_BENCH_HOME=<dir> to keep one). Exit code: 0 when every run
# finished within its limits, 1 otherwise, 2 when the player is missing or the output folder cannot be written.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ZAMIN_BUILD_DIR:-$root/build}"
player="$build/bin/ZaminPlayer"
frames=300
warmup=60
resolution=1280x720
out="$build/PlayerBench"
max_avg=""
max_p95=""
projects=()
scene=""
extra=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --frames) frames="$2"; shift 2 ;;
        --warmup) warmup="$2"; shift 2 ;;
        --resolution) resolution="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --max-avg) max_avg="$2"; shift 2 ;;
        --max-p95) max_p95="$2"; shift 2 ;;
        --project) projects+=("$2"); shift 2 ;;
        --scene) scene="$2"; shift 2 ;;
        -h|--help) sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) extra+=("$1"); shift ;;
    esac
done
if [[ ! -x "$player" ]]; then
    echo "ZaminPlayer not found: $player (first: cmake --build \"$build\")" >&2
    exit 2
fi
if [[ ${#projects[@]} -eq 0 ]]; then
    projects=("$root/Projects/Platformer2D" "$root/Projects/Showcase3D" "$root/Projects/Adventure3D")
fi
runner=()
if [[ -z "${DISPLAY:-}" ]]; then
    if command -v xvfb-run >/dev/null 2>&1; then
        runner=(xvfb-run -a -s "-screen 0 1920x1080x24")
    else
        echo "No display and xvfb-run was not found - the player window cannot open" >&2
        exit 2
    fi
fi
if ! mkdir -p "$out" 2>/dev/null || [[ ! -d "$out" || ! -w "$out" ]]; then
    echo "The results folder could not be created or is not writable: $out" >&2
    exit 2
fi
home="${ZAMIN_BENCH_HOME:-}"
cleanup_home=0
if [[ -z "$home" ]]; then
    home="$(mktemp -d)"
    cleanup_home=1
fi

# A number of the report's "Frame" block ("Average", "P95" ...).
frame_value() {
    awk -v key="\"$2\":" '/"Frame": \{/ { inside = 1; next } inside && /\}/ { inside = 0 } inside && $1 == key { gsub(/,/, "", $2); print $2; exit }' "$1"
}

failed=0
summary=()
for project in "${projects[@]}"; do
    name="$(basename "$project")"
    report="$out/$name.json"
    log="$out/$name.log"
    rm -f "$report"
    args=("$project" --benchmark "$frames" --benchmark-warmup "$warmup" --benchmark-out "$report" --resolution "$resolution" --windowed
          --log-file "$log")
    [[ -n "$scene" ]] && args+=(--scene "$scene")
    [[ -n "$max_avg" ]] && args+=(--benchmark-max-avg "$max_avg")
    [[ -n "$max_p95" ]] && args+=(--benchmark-max-p95 "$max_p95")
    args+=("${extra[@]}")
    echo "== $name: $frames frames ($warmup warm-up), $resolution"
    start=$(date +%s)
    HOME="$home" "${runner[@]}" "$player" "${args[@]}" >/dev/null 2>&1
    code=$?
    seconds=$(( $(date +%s) - start ))
    if [[ ! -f "$report" ]]; then
        echo "   FAILED: no result was written (exit code $code, $seconds s) - log: $log"
        summary+=("$(printf '%-14s %8s %8s %8s %8s %8s  %s' "$name" - - - - - "failed ($code)")")
        failed=1
        continue
    fi
    avg=$(frame_value "$report" Average)
    median=$(frame_value "$report" Median)
    p95=$(frame_value "$report" P95)
    p99=$(frame_value "$report" P99)
    fps=$(frame_value "$report" AverageFPS)
    status="passed"
    if [[ $code -eq 3 ]]; then
        status="over the limit"
        failed=1
    elif [[ $code -ne 0 ]]; then
        status="failed ($code)"
        failed=1
    fi
    echo "   average $avg ms, median $median, p95 $p95, p99 $p99 ms ($fps FPS) - $status, $seconds s"
    summary+=("$(printf '%-14s %8s %8s %8s %8s %8s  %s' "$name" "$avg" "$median" "$p95" "$p99" "$fps" "$status")")
done
[[ $cleanup_home -eq 1 ]] && rm -rf "$home"

echo
printf '%-14s %8s %8s %8s %8s %8s  %s\n' "Project" "avg ms" "med.ms" "p95 ms" "p99 ms" "FPS" "result"
for line in "${summary[@]}"; do
    echo "$line"
done
echo "Reports: $out"
exit $failed
