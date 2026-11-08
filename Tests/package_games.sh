#!/usr/bin/env bash
# Player builds of the sample games (CI artefacts): every game is exported by the editor, tested the way a player
# gets it (Tests/export_test.sh: --version, a timed run, a headless run with a screenshot, moved elsewhere) and only
# then packed as <Name>-<version>-linux-<arch>.tar.gz with its .sha256 (the archive is unpacked again and started).
#   Tests/package_games.sh                              Platformer2D, Showcase3D, Adventure3D -> <build>/PlayerBuilds
#   Tests/package_games.sh --out dist                   the packages there (dist/logs/<game>: logs, screenshot, reports)
#   Tests/package_games.sh --frames 60 Projects/Adventure3D   these projects, 60 benchmark frames each
#   ZAMIN_BUILD_DIR=build-release Tests/package_games.sh       the engine / player of another build (CI: Release)
# <out>/summary.md lists the packages and every game's result (also on the GitHub Actions run's page).
# Exit code: 0 when every game was exported, tested and packed; 1 when one failed; 2 bad usage.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ZAMIN_BUILD_DIR:-$root/build}"
[[ "$build" != /* ]] && build="$root/$build"
out="$build/PlayerBuilds"
frames=120
projects=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) out="$2"; shift 2 ;;
        --frames) frames="$2"; shift 2 ;;
        -h|--help) sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "noma'lum variant: $1 (--help)" >&2; exit 2 ;;
        *) projects+=("$1"); shift ;;
    esac
done
[[ ${#projects[@]} -eq 0 ]] && projects=("$root/Projects/Platformer2D" "$root/Projects/Showcase3D" "$root/Projects/Adventure3D")
mkdir -p "$out"
out="$(cd "$out" && pwd)"
rm -f "$out"/*.tar.gz "$out"/*.sha256 "$out/packages.md" "$out/summary.md"

failed=0
rows=()
for project in "${projects[@]}"; do
    game="$(basename "$project")"
    echo "== $game"
    start=$(date +%s)
    ZAMIN_BUILD_DIR="$build" "$root/Tests/export_test.sh" --project "$project" --frames "$frames" \
        --out "$out/logs/$game" --package "$out" 2>&1 | tee "$out/$game.log" | sed 's/^/   /'
    code=${PIPESTATUS[0]}
    secs=$(( $(date +%s) - start ))
    mkdir -p "$out/logs/$game" && mv -f "$out/$game.log" "$out/logs/$game/export_test.log"
    if [[ $code -eq 0 ]]; then
        rows+=("| $game | OK | $secs s |")
    else
        rows+=("| $game | XATO (kod $code) | $secs s |")
        failed=$((failed + 1))
    fi
done

{
    if [[ $failed -eq 0 ]]; then echo "### O'yin build'lari: ${#projects[@]} ta o'yin paketlandi"; else echo "### O'yin build'lari: $failed ta o'yinda xato"; fi
    echo
    [[ -f "$out/packages.md" ]] && { cat "$out/packages.md"; echo; }
    echo "| Loyiha | Eksport testi | Vaqt |"
    echo "|---|---|---|"
    printf '%s\n' "${rows[@]}"
} >"$out/summary.md"
[[ -n "${GITHUB_STEP_SUMMARY:-}" ]] && cat "$out/summary.md" >>"$GITHUB_STEP_SUMMARY"
echo
cat "$out/summary.md"
echo
echo "Paketlar: $out"
[[ $failed -eq 0 ]]
