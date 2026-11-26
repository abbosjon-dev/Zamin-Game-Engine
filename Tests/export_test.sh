#!/usr/bin/env bash
# Automatic export test: exports a sample game with the editor (ZAMIN_EDITOR_EXPORT, headless under xvfb)
# into a temporary folder, then checks the exported game the way a player gets it:
#   1. the editor exported without errors (exit code 0, export-report.json "Succeeded": true; the export
#      checks its own folder - ExportFiles::CheckExportFolder);
#   2. <Game>.sh --version answers with the game's name and the engine's version;
#   3. the exported game runs its start scene for some frames (ZaminPlayer --benchmark, from the launcher):
#      exit code 0, its log has no errors, the script module loaded, the benchmark report was written;
#   4. it runs again headless with a screenshot of its last frame (ZAMIN_MAX_FRAMES, ZAMIN_SCREENSHOT): exit code
#      0, a 1280x720 PNG that shows the game (Tests/png_check.py: not one flat colour);
#   5. the folder still runs after it was moved elsewhere (relocatable launcher), and its desktop entry's icon
#      then points into the new place (the launcher rewrites it).
#   Tests/export_test.sh                             Platformer2D, 120 frames
#   Tests/export_test.sh --project Projects/Adventure3D --frames 60
#   Tests/export_test.sh --shot-frames 210          longer gameplay acceptance screenshot run
#   Tests/export_test.sh --keep                      keep the temporary folder (the export, the logs)
#   Tests/export_test.sh --work /tmp/zamin-export    use (and keep) that folder
#   Tests/export_test.sh --out DIR                   the logs, reports and the screenshot copied there (CI artefacts)
#   Tests/export_test.sh --package DIR               when every check passed: the tested export packed as
#                                                    DIR/<Name>-<version>-linux-<arch>.tar.gz (+ .sha256; unpacked
#                                                    again and started from there), a row in DIR/packages.md
# The editor and the game run with a HOME of their own inside the work folder. The project itself is left as
# it is (the export builds the script module in Release in <project>/Build/ExportRelease, as the editor does).
# Exit code: 0 when every check passed, 1 when one failed, 2 when the editor / player are missing.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ZAMIN_BUILD_DIR:-$root/build}"
editor="$build/bin/ZaminEditor"
project="$root/Projects/Platformer2D"
frames=120
shot_frames=90
keep=0
work=""
out=""
package=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --project) project="$2"; shift 2 ;;
        --frames) frames="$2"; shift 2 ;;
        --shot-frames) shot_frames="$2"; shift 2 ;;
        --keep) keep=1; shift ;;
        --work) work="$2"; keep=1; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --package) package="$2"; shift 2 ;;
        -h|--help) sed -n '2,23p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (--help)" >&2; exit 2 ;;
    esac
done
if [[ ! "$shot_frames" =~ ^[1-9][0-9]{0,4}$ ]]; then
    echo "--shot-frames: a positive integer is required (1..99999)" >&2
    exit 2
fi
if [[ ! -x "$editor" || ! -x "$build/bin/ZaminPlayer" ]]; then
    echo "ZaminEditor / ZaminPlayer not found: $build/bin (first: cmake --build \"$build\")" >&2
    exit 2
fi
project="$(cd "$project" && pwd)"
project_file="$(ls "$project"/*.zproject 2>/dev/null | head -n 1)"
if [[ -z "$project_file" ]]; then
    echo "Project file not found: $project/*.zproject" >&2
    exit 2
fi
# The game's name (the launcher is <Name>.sh) and product name from the project file.
name="$(sed -n 's/^ *"Name": *"\(.*\)",\{0,1\}$/\1/p' "$project_file" | head -n 1)"
product="$(sed -n 's/^ *"Product": *"\(.*\)",\{0,1\}$/\1/p' "$project_file" | head -n 1)"
[[ -z "$product" ]] && product="$name"
has_module=0
grep -q '"ScriptModulePath": *"[^"]' "$project_file" && has_module=1

if [[ -z "$work" ]]; then
    work="$(mktemp -d "${TMPDIR:-/tmp}/zamin-export-test.XXXXXX")"
fi
mkdir -p "$work/home"
export_dir="$work/export"
rm -rf "$export_dir" "$work/moved"
runner=()
if [[ -z "${DISPLAY:-}" ]]; then
    if command -v xvfb-run >/dev/null 2>&1; then
        runner=(xvfb-run -a -s "-screen 0 1600x900x24")
    else
        echo "No display and xvfb-run was not found" >&2
        exit 2
    fi
fi

failures=0
pass() { echo "  [OK]   $1"; }
fail() { echo "  [FAIL] $1"; failures=$((failures + 1)); }
# A log's error lines ("[12:00:00] ERROR APP: ...", CRITICAL).
log_errors() { grep -E '^\[[^]]*\] (ERROR|CRITICAL) ' "$1" 2>/dev/null; }

echo "Export test: $name ($product) -> $export_dir"

# 1. The export (the editor's automation hooks run in its 5th frame; it waits for the export there, then closes).
start=$(date +%s)
HOME="$work/home" ZAMIN_EDITOR_EXPORT="$export_dir" ZAMIN_MAX_FRAMES=8 "${runner[@]}" "$editor" "$project" >"$work/editor.log" 2>&1
code=$?
echo "  export: $(( $(date +%s) - start )) s (log: $work/editor.log)"
if [[ $code -eq 0 ]]; then pass "the editor finished the export (exit code 0)"; else fail "editor exit code $code"; fi
report="$export_dir/export-report.json"
if [[ ! -f "$report" ]]; then
    fail "export-report.json was not written"
elif grep -q '"Succeeded": *true' "$report"; then
    pass "export-report.json: success (a report of $(grep -c . "$export_dir/export-report.txt" 2>/dev/null || echo 0) lines)"
else
    fail "export-report.json: with errors - $(grep -A3 '"Errors"' "$report" | tr -d '\n' | head -c 300)"
fi
if grep -q "Export: .*export incomplete" "$work/editor.log"; then
    fail "the export folder is incomplete: $(grep "export incomplete" "$work/editor.log" | head -n 3 | tr '\n' ' ')"
fi
launcher="$export_dir/$name.sh"
if [[ ! -x "$launcher" ]]; then
    fail "no launcher script: $launcher"
    echo "Result: $failures failures (folder: $work)"
    exit 1
fi

# 2. --version through the launcher (no window).
version="$(HOME="$work/home" "$launcher" --version 2>&1)"
code=$?
if [[ $code -eq 0 && "$version" == *"$product"* && "$version" == *"Zamin Engine"* ]]; then
    pass "--version: $(echo "$version" | head -n 2 | tr '\n' ' ')"
else
    fail "--version (code $code): $version"
fi

# 3. The game runs: its start scene, timed (exit code 0), a log without errors.
player_log="$work/player.log"
bench="$work/benchmark.json"
start=$(date +%s)
HOME="$work/home" "${runner[@]}" "$launcher" --benchmark "$frames" --benchmark-warmup 10 --benchmark-out "$bench" --log-file "$player_log" \
    --windowed --resolution 1280x720 >"$work/player-stdout.log" 2>&1
code=$?
echo "  game: $(( $(date +%s) - start )) s (log: $player_log)"
if [[ $code -eq 0 ]]; then pass "the exported game ran $frames frames (exit code 0)"; else fail "game exit code $code"; fi
if [[ -f "$bench" ]] && grep -q "\"Count\": $frames" "$bench"; then
    avg="$(awk '/"Frame": \{/ { f = 1; next } f && /"Average":/ { gsub(/,/, "", $2); print $2; exit }' "$bench")"
    pass "benchmark: $frames frames, average $avg ms"
else
    fail "the benchmark result is missing or incomplete: $bench"
fi
if [[ ! -s "$player_log" ]]; then
    fail "the game log was not written: $player_log"
else
    errors="$(log_errors "$player_log")"
    if [[ -z "$errors" ]]; then pass "no errors in the game log ($(wc -l <"$player_log") lines)"; else fail "errors in the game log:"; echo "$errors" | head -n 10 | sed 's/^/           /'; fi
    if [[ $has_module -eq 1 ]]; then
        if grep -q "Script module loaded" "$player_log"; then
            pass "$(grep -o "Script module loaded.*" "$player_log" | head -n 1)"
        else
            fail "the script module was not loaded"
        fi
    fi
    if grep -q "Scene load time\|Benchmark: " "$player_log"; then pass "the start scene was loaded"; else fail "the start scene was not loaded"; fi
fi

# 4. Headless again, its last frame saved: the picture shows the game (not a black or empty window).
shot="$work/game.png"
rm -f "$shot"
HOME="$work/home" ZAMIN_MAX_FRAMES="$shot_frames" ZAMIN_SCREENSHOT="$shot" "${runner[@]}" "$launcher" --windowed --resolution 1280x720 \
    --log-file "$work/player-shot.log" >"$work/player-shot-stdout.log" 2>&1
code=$?
if [[ $code -eq 0 ]]; then pass "the exported game ran with a screenshot (exit code 0)"; else fail "the screenshot run: exit code $code"; fi
if [[ ! -f "$shot" ]]; then
    fail "the screenshot was not written: $shot"
elif check="$(python3 "$root/Tests/png_check.py" "$shot" --size 1280x720 --min-colors 32 --min-stddev 4 2>&1)"; then
    pass "screenshot: $(echo "$check" | head -n 1 | sed 's|^.*: ||')"
else
    fail "screenshot: $(echo "$check" | tr '\n' ' ')"
fi
errors="$(log_errors "$work/player-shot.log")"
[[ -n "$errors" ]] && { fail "errors in the log of the screenshot run:"; echo "$errors" | head -n 5 | sed 's/^/           /'; }

# 5. Moved elsewhere, it still starts (the launcher finds its files next to itself).
mv "$export_dir" "$work/moved"
version="$(HOME="$work/home" "$work/moved/$name.sh" --version 2>&1)"
if [[ $? -eq 0 && "$version" == *"$product"* ]]; then pass "starts after being moved to another folder too"; else fail "did not start from the moved folder: $version"; fi
# (Desktop entries take only absolute icon paths: the launcher pointed the entry next to it at the icon here.)
desktop="$work/moved/$name.desktop"
if grep -q '^Icon=' "$desktop" 2>/dev/null; then
    if grep -qxF "Icon=$work/moved/$name.png" "$desktop"; then
        pass "in the moved folder the .desktop icon points to the new location"
    else
        fail "the .desktop icon does not point to the new location: $(grep '^Icon=' "$desktop")"
    fi
fi
mv "$work/moved" "$export_dir"

# The logs, reports and the picture for whoever looks later (CI artefacts).
if [[ -n "$out" ]]; then
    mkdir -p "$out"
    cp -f "$work"/*.log "$work/game.png" "$bench" "$export_dir/export-report.json" "$export_dir/export-report.txt" "$out/" 2>/dev/null
    echo "  results: $out"
fi

# The tested export as a player build (CI artefact): a tar.gz keeps the files' executable bits, which an artefact's
# zip does not. It is unpacked again and its launcher started from there.
if [[ -n "$package" && $failures -eq 0 ]]; then
    mkdir -p "$package"
    package="$(cd "$package" && pwd)"
    game_version="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1])).get("Player", {}).get("Version", "") or "0.0.0")' "$project_file" 2>/dev/null)"
    archive="$name-${game_version:-0.0.0}-linux-$(uname -m).tar.gz"
    mkdir -p "$work/package"
    mv "$export_dir" "$work/package/$name"
    # Reproducible-ish: sorted names, no owner names.
    tar -C "$work/package" --sort=name --owner=0 --group=0 --numeric-owner -czf "$package/$archive" "$name"
    code=$?
    mv "$work/package/$name" "$export_dir"
    if [[ $code -ne 0 ]]; then
        fail "the package was not created: $package/$archive"
    else
        (cd "$package" && sha256sum "$archive" >"$archive.sha256")
        files="$(tar -tzf "$package/$archive" | grep -vc '/$')"
        size="$(du -h "$package/$archive" | cut -f1)"
        rm -rf "$work/unpacked" && mkdir -p "$work/unpacked"
        tar -C "$work/unpacked" -xzf "$package/$archive"
        version="$(HOME="$work/home" "$work/unpacked/$name/$name.sh" --version 2>&1)"
        if [[ $? -eq 0 && "$version" == *"$product"* ]]; then
            pass "package: $archive ($size, $files files) - starts after being unpacked"
            [[ -f "$package/packages.md" ]] || printf '| Game | Version | Package | Size | Files | SHA-256 |\n|---|---|---|---|---|---|\n' >"$package/packages.md"
            echo "| $product | $game_version | \`$archive\` | $size | $files | \`$(cut -c1-16 "$package/$archive.sha256")\` |" >>"$package/packages.md"
        else
            fail "did not start after the package was unpacked: $version"
            rm -f "$package/$archive" "$package/$archive.sha256"
        fi
        rm -rf "$work/unpacked"
    fi
fi

echo
if [[ $failures -eq 0 ]]; then
    echo "Export test: all passed"
    [[ $keep -eq 1 ]] && echo "Folder: $work" || rm -rf "$work"
    exit 0
fi
echo "Export test: $failures failures (folder kept: $work)"
exit 1
