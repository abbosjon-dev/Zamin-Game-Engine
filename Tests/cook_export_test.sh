#!/usr/bin/env bash
# Cooked export test: Projects/StreamingWorkshop exported by the editor (Tests/export_test.sh: the exported Player runs
# its benchmark frames, a screenshot and a move to another folder) and then checked for the cook:
#   - export-report.json has a "Cook" section: every texture of the game cooked (the sample sets Cook.Always), the 512 px
#     terrain textures halved to the 256 px limit, the sprite atlas page cooked with its frames;
#   - the exported textures are ZTEX files at their asset paths, the atlas's unused source sprites are not exported,
#     cook-manifest.json lists the textures and the atlas UV table, the tiled terrain's tiles went along;
#   - a second export takes every texture from the cook cache (<project>/Build/CookCache);
#   - the editor shows the report's cooked textures (a screenshot of the Export report window);
#   - Projects/VisualWorkshop exported too: every Visual Script graph cooked to bytecode (export-report.json "GraphCook",
#     ZGBC files at the .zgraph paths, smaller than their sources), the exported Player runs the cooked graphs (the
#     BLUEPRINT_* lines of the 2D scene, the native method graph with the Release module), a second export takes every
#     graph from the cook cache, and the Export report window lists the cooked graphs.
#   Tests/cook_export_test.sh [--work DIR] [--out DIR]     (the logs and screenshots copied to DIR)
# Exit code: 0 when every check passed, 1 when one failed, 2 when the editor / player are missing.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ZAMIN_BUILD_DIR:-$root/build}"
work=""
out=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --work) work="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        -h|--help) sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (--help)" >&2; exit 2 ;;
    esac
done
[[ -x "$build/bin/ZaminEditor" && -x "$build/bin/ZaminPlayer" ]] || { echo "ZaminEditor / ZaminPlayer not found in $build/bin" >&2; exit 2; }
[[ -z "$work" ]] && work="$(mktemp -d "${TMPDIR:-/tmp}/zamin-cook-export.XXXXXX")"
mkdir -p "$work"
# A private copy of the sample (the export writes its cook cache into <project>/Build).
rm -rf "$work/StreamingWorkshop"
cp -r "$root/Projects/StreamingWorkshop" "$work/StreamingWorkshop"
failures=0
pass() { echo "  [ OK ] $1"; }
fail() { echo "  [FAIL] $1"; failures=$((failures + 1)); }

echo "== export and run (Tests/export_test.sh)"
ZAMIN_BUILD_DIR="$build" "$root/Tests/export_test.sh" --project "$work/StreamingWorkshop" --frames 60 --work "$work/first" || fail "export_test.sh failed"
export_dir="$work/first/export"
report="$export_dir/export-report.json"
[[ -f "$report" ]] || { fail "no export report: $report"; echo "$failures check(s) failed"; exit 1; }

echo "== the cook"
python3 - "$report" "$export_dir" <<'EOF' || failures=$((failures + 1))
import json, sys
from pathlib import Path
report, folder = json.load(open(sys.argv[1])), Path(sys.argv[2])
assets = folder / 'Game/Assets'
cook = report.get('Cook')
assert cook, 'the report has no Cook section'
textures = {t['Path']: t for t in cook['Textures']}
cooked = [t for t in textures.values() if t['Action'] == 'cooked']
print(f"  [ OK ] {len(cooked)} of {len(textures)} textures cooked, {cook['CacheHits']} from the cache, {cook['Seconds']:.2f} s")
for path in ('Textures/Terrain/grass-albedo.png', 'Textures/Terrain/rock-albedo.png'):
    t = textures[path]
    assert t['Action'] == 'cooked' and t['Size'] == [256, 256] and t['SourceSize'] == [512, 512], t
    print(f"  [ OK ] {path}: {t['SourceSize'][0]} px -> {t['Size'][0]} px, {t['SourceBytes']} -> {t['CookedBytes']} bytes")
page = textures['Sprites/Gems.atlas.png']
assert page['Action'] == 'cooked', page
for path in textures:
    if textures[path]['Action'] == 'cooked':
        assert (assets / path).read_bytes()[:4] == b'ZTEX', path + ' is not a cooked file'
print('  [ OK ] the cooked textures are ZTEX files at their asset paths')
assert not (assets / 'Sprites/Gems').exists(), 'the atlas sources were exported'
print('  [ OK ] the atlas source sprites stayed out of the export')
manifest = json.load(open(folder / 'Game/cook-manifest.json'))
atlases = manifest['Atlases']
assert atlases and atlases[0]['Page'] == 'Sprites/Gems.atlas.png' and len(atlases[0]['Sprites']) == 4, atlases
print(f"  [ OK ] cook-manifest.json: {len(manifest['Textures'])} textures, atlas {atlases[0]['Atlas']} with {len(atlases[0]['Sprites'])} sprites and their UVs")
tiles = list((assets / 'Maps/Valley.tiles').glob('*.ztile'))
assert len(tiles) == 64 and (assets / 'Maps/Valley.zterrain').exists(), len(tiles)
print('  [ OK ] the tiled terrain went along: Valley.zterrain and 64 tiles')
EOF
echo "== a second export: the cook cache"
rm -rf "$work/second"
ZAMIN_BUILD_DIR="$build" "$root/Tests/export_test.sh" --project "$work/StreamingWorkshop" --frames 20 --work "$work/second" >"$work/second-export.log" 2>&1 \
    || fail "the second export failed (see $work/second-export.log)"
python3 - "$work/second/export/export-report.json" <<'EOF' || failures=$((failures + 1))
import json, sys
cook = json.load(open(sys.argv[1]))['Cook']
assert cook['CacheHits'] == len(cook['Textures']), cook['CacheHits']
print(f"  [ OK ] second export: all {cook['CacheHits']} textures from the cook cache, {cook['Seconds']:.2f} s")
EOF

echo "== the Export report window"
shot="$work/export-report-cooked.png"
runner=()
[[ -z "${DISPLAY:-}" ]] && runner=(xvfb-run -a -s "-screen 0 1600x1000x24 -nolisten tcp")
HOME="$work/first/home" LIBGL_ALWAYS_SOFTWARE=1 ZAMIN_EDITOR_EXPORT_REPORT="$export_dir" ZAMIN_SCREENSHOT="$shot" ZAMIN_MAX_FRAMES=40 \
    "${runner[@]}" "$build/bin/ZaminEditor" "$work/StreamingWorkshop/StreamingWorkshop.zproject" >"$work/report-editor.log" 2>&1
if [[ -f "$shot" ]]; then pass "the Export report window: $shot"; else fail "no screenshot of the Export report window"; fi

echo "== VisualWorkshop: the graph cook (bytecode) and the Player on cooked graphs"
rm -rf "$work/VisualWorkshop" "$work/graphs" "$work/graphs-second"
cp -r "$root/Projects/VisualWorkshop" "$work/VisualWorkshop"
rm -rf "$work/VisualWorkshop/Build" "$work/VisualWorkshop/.zamin"
ZAMIN_BUILD_DIR="$build" "$root/Tests/export_test.sh" --project "$work/VisualWorkshop" --frames 60 --work "$work/graphs" >"$work/graphs-export.log" 2>&1 \
    || fail "the VisualWorkshop export failed (see $work/graphs-export.log)"
graph_export="$work/graphs/export"
python3 - "$graph_export" "$work/graphs/player.log" <<'EOF' || failures=$((failures + 1))
import json, re, sys
from pathlib import Path
folder, log = Path(sys.argv[1]), Path(sys.argv[2]).read_text(errors='replace')
report = json.load(open(folder / 'export-report.json'))
graphs = {g['Path']: g for g in report['GraphCook']['Graphs']}
expected = {'Scripts/' + name + '.zgraph' for name in ('Move2D', 'Spin3D', 'BlueprintGameplay', 'BlueprintBase', 'BlueprintChild', 'NativeMethods', 'AnimatorGameplay', 'AnimatorDisplay2D')}
assert expected <= set(graphs), sorted(graphs)
for path, g in graphs.items():
    assert g['Action'] == 'cooked' and 0 < g['CookedBytes'] < g['SourceBytes'] and g['Nodes'] > 0, g
    assert (folder / 'Game/Assets' / path).read_bytes()[:4] == b'ZGBC', path + ' is not a cooked graph'
source = sum(g['SourceBytes'] for g in graphs.values()); cooked = sum(g['CookedBytes'] for g in graphs.values())
print(f"  [ OK ] {len(graphs)} graphs cooked to bytecode: {source} -> {cooked} bytes ({source / cooked:.1f}x smaller), {report['GraphCook']['Seconds']:.2f} s")
text = (folder / 'export-report.txt').read_text()
assert 'Cooked graphs: ' + str(len(graphs)) + ' of ' + str(len(graphs)) in text, 'the text report does not list the cooked graphs'
for line in ('BLUEPRINT_ACCEPTANCE total=12', 'BLUEPRINT_SUMMARY total=12 rank=Silver', 'BLUEPRINT_INHERIT curve=2.000 bonus=30.00 keys=3', 'BLUEPRINT_NATIVE score=36 medal=Silver hypot=5.00 awards=1'):
    assert line in log, 'the exported Player did not print ' + line
assert "Visual graph '" not in log and 'Cooked graph' not in log, 'a graph error in the exported Player'
print('  [ OK ] the exported Player ran the cooked graphs: BLUEPRINT_ACCEPTANCE/SUMMARY/INHERIT and BLUEPRINT_NATIVE (C++ methods)')
EOF
ZAMIN_BUILD_DIR="$build" "$root/Tests/export_test.sh" --project "$work/VisualWorkshop" --frames 20 --work "$work/graphs-second" >"$work/graphs-second-export.log" 2>&1 \
    || fail "the second VisualWorkshop export failed (see $work/graphs-second-export.log)"
python3 - "$work/graphs-second/export/export-report.json" <<'EOF' || failures=$((failures + 1))
import json, sys
cook = json.load(open(sys.argv[1]))['GraphCook']
assert cook['CacheHits'] == len(cook['Graphs']), cook['CacheHits']
print(f"  [ OK ] second export: all {cook['CacheHits']} graphs from the cook cache, {cook['Seconds']:.2f} s")
EOF
graph_shot="$work/export-report-graphs.png"
HOME="$work/graphs/home" LIBGL_ALWAYS_SOFTWARE=1 ZAMIN_EDITOR_EXPORT_REPORT="$graph_export" ZAMIN_SCREENSHOT="$graph_shot" ZAMIN_MAX_FRAMES=40 \
    "${runner[@]}" "$build/bin/ZaminEditor" "$work/VisualWorkshop/VisualWorkshop.zproject" >"$work/graphs-report-editor.log" 2>&1
if [[ -f "$graph_shot" ]]; then pass "the Export report window with the cooked graphs: $graph_shot"; else fail "no screenshot of the Export report window (graphs)"; fi

if [[ -n "$out" ]]; then
    mkdir -p "$out"
    cp -f "$graph_export/export-report.json" "$out/export-report-graphs.json" 2>/dev/null
    cp -f "$graph_export/export-report.txt" "$out/export-report-graphs.txt" 2>/dev/null
    cp -f "$graph_shot" "$out/" 2>/dev/null
    cp -f "$work/graphs/player.log" "$out/player-graphs.log" 2>/dev/null
    cp -f "$report" "$export_dir/export-report.txt" "$export_dir/Game/cook-manifest.json" "$out/" 2>/dev/null
    cp -f "$shot" "$work/first/"*.png "$out/" 2>/dev/null
    cp -f "$work/first/"*.log "$out/" 2>/dev/null
fi
if [[ $failures -eq 0 ]]; then echo "PASS cooked export: the Player ran on cooked textures, an atlas page, streamed tiles and cooked graphs"; exit 0; fi
echo "$failures check(s) failed (work folder: $work)"
exit 1
