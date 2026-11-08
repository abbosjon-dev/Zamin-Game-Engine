#!/usr/bin/env bash
# The headless benchmarks with their results as JSON, optionally compared with the saved baseline: the ECS
# (ZaminBench --ecs, Tests/EcsBench.cpp), scene serialization (--serialize, Tests/SerializeBench.cpp) and linked
# prefab instances (--serialize --prefabs, Tests/PrefabBench.cpp). No display or GPU is needed (the rendering
# benchmark is Tests/render_bench.sh). The numbers that matter come from a Release build:
#   cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-release --target ZaminBench
#   ZAMIN_BUILD_DIR=build-release Tests/bench.sh                 every benchmark, results in <build>/Bench
#   Tests/bench.sh --only ecs,serialize                          some of them (ecs, serialize, prefabs)
#   Tests/bench.sh --compare [--tolerance 25]                    also against Tests/Baselines/<name>-<build>.json:
#                                                                a number slower by more than 25 % fails the run
#   Tests/bench.sh --update-baseline                             the results become the new baseline
#   Tests/bench.sh --out DIR                                     the results somewhere else
# The baselines are machine-dependent: compare on the computer they were made on (docs/Benchmarks.md has where
# they come from), or make them again there first. Exit code: 0 - ran (and no regression), 1 - a benchmark failed
# or got slower, 2 - ZaminBench missing / bad arguments.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ZAMIN_BUILD_DIR:-$root/build}"
[[ "$build" != /* ]] && build="$root/$build"
bench="$build/bin/ZaminBench"
out="$build/Bench"
only="ecs,serialize,prefabs"
compare=0
update=0
tolerance=25
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) out="$2"; shift 2 ;;
        --only) only="$2"; shift 2 ;;
        --compare) compare=1; shift ;;
        --update-baseline) update=1; shift ;;
        --tolerance) tolerance="$2"; shift 2 ;;
        -h|--help) sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Noma'lum parametr: $1 (--help)" >&2; exit 2 ;;
    esac
done
[[ "$out" != /* ]] && out="$PWD/$out"
if [[ ! -x "$bench" ]]; then
    echo "ZaminBench topilmadi: $bench (avval: cmake --build \"$build\" --target ZaminBench)" >&2
    exit 2
fi
mkdir -p "$out" || exit 2
baselines="$root/Tests/Baselines"
failures=0
home="$(mktemp -d)"
trap 'rm -rf "$home"' EXIT

run() # name, ZaminBench arguments...
{
    local name="$1"; shift
    local json="$out/$name.json" log="$out/$name.txt"
    rm -f "$json"
    echo "== $name: ZaminBench $*"
    # The engine's own log lines out: the tables only (all of it in the .txt).
    (cd "$root" && HOME="$home" "$bench" "$@" --json "$json") 2>&1 | sed 's/\x1b\[[0-9;]*m//g' >"$log"
    local code=${PIPESTATUS[0]}
    grep -vE '^\[[0-9:]+\] (ZAMIN|APP): ' "$log"
    if [[ $code -ne 0 || ! -f "$json" ]]; then
        echo "$name: ishlamadi (chiqish kodi $code)"
        failures=$((failures + 1))
        return
    fi
    local type
    type="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["Build"].lower())' "$json")"
    local baseline="$baselines/$name-$type.json"
    if [[ $update -eq 1 ]]; then
        mkdir -p "$baselines" && cp "$json" "$baseline" && echo "Boshlang'ich natija yangilandi: ${baseline#$root/}"
    elif [[ $compare -eq 1 ]]; then
        if [[ ! -f "$baseline" ]]; then
            echo "$name: boshlang'ich natija yo'q (${baseline#$root/}; --update-baseline bilan yarating)"
        elif ! python3 "$root/Tests/bench_compare.py" "$baseline" "$json" --tolerance "$tolerance" --markdown "$out/$name-compare.md"; then
            failures=$((failures + 1))
        fi
    fi
    echo
}

IFS=',' read -ra names <<<"$only"
for name in "${names[@]}"; do
    case "$name" in
        ecs) run ecs --ecs ;;
        serialize) run serialize --serialize ;;
        prefabs) run prefabs --serialize --prefabs ;;
        *) echo "Noma'lum benchmark: $name (ecs, serialize, prefabs)" >&2; exit 2 ;;
    esac
done
echo "Natijalar: $out"
[[ $failures -eq 0 ]] && exit 0 || exit 1
