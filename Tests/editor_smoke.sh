#!/usr/bin/env bash
# Editor smoke test: ZaminEditor opens every window, page, menu and popup in turn for a few frames each, selects
# every entity, draws every component's inspector, the asset inspector on each asset type, the layouts, plays
# and simulates the scene (ZAMIN_EDITOR_OPEN_ALL_PANELS=1, Editor/src/EditorSmokeTest.cpp) - in a copy of a
# sample project (the project itself is not touched), with a HOME of its own, under xvfb when there is no display.
#   Tests/editor_smoke.sh                          Showcase3D and Platformer2D
#   Tests/editor_smoke.sh Projects/Adventure3D     these projects
#   Tests/editor_smoke.sh --shots                  some steps' pictures as PNG files too (ZAMIN_EDITOR_SMOKE_SHOTS)
#   Tests/editor_smoke.sh --out DIR                logs (and pictures) there (default <build>/EditorSmoke)
#   Tests/editor_smoke.sh --keep                   keep the project copies (the work folder)
#   ZAMIN_BUILD_DIR=build-clang Tests/editor_smoke.sh    another build
# A project fails on: a crash or another exit code than 0 (a Debug build stops on a failed ImGui check), errors
# in the log while the test ran (the OpenGL debug output - ZAMIN_GL_DEBUG=1 -, failed ImGui checks, the editor's
# own), OpenGL error flags, a run that did not reach its last step. summary.md lists the projects (also on the
# GitHub Actions run's page). Exit code: 0 when every project passed, 1 when one failed, 2 when the editor is missing.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ZAMIN_BUILD_DIR:-$root/build}"
[[ "$build" != /* ]] && build="$root/$build"
editor="$build/bin/ZaminEditor"
out="$build/EditorSmoke"
shots=0
keep=0
projects=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) out="$2"; shift 2 ;;
        --shots) shots=1; shift ;;
        --keep) keep=1; shift ;;
        -h|--help) sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "noma'lum variant: $1 (--help)" >&2; exit 2 ;;
        *) projects+=("$1"); shift ;;
    esac
done
[[ ${#projects[@]} -eq 0 ]] && projects=("$root/Projects/Showcase3D" "$root/Projects/Platformer2D")
[[ "$out" != /* ]] && out="$PWD/$out"
if [[ ! -x "$editor" ]]; then
    echo "ZaminEditor topilmadi: $editor (avval: cmake --build \"$build\" --target ZaminEditor)" >&2
    exit 2
fi
runner=()
if [[ -z "${DISPLAY:-}" ]]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        echo "Displey yo'q va xvfb-run topilmadi (apt install xvfb)" >&2
        exit 2
    fi
    runner=(xvfb-run -a -s "-screen 0 1600x900x24 -nolisten tcp")
fi
# The same rasteriser everywhere (Mesa llvmpipe), as the render tests.
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
mkdir -p "$out"
work="$(mktemp -d "${TMPDIR:-/tmp}/zamin-editor-smoke.XXXXXX")"
summary="$out/summary.md"
{
    echo "### Editor smoke testi"
    echo
    echo "| Loyiha | Natija | Qadamlar | Kadrlar | Vaqt | Xatolar |"
    echo "|---|---|---|---|---|---|"
} >"$summary"

failed=0
for project in "${projects[@]}"; do
    project="$(cd "$project" 2>/dev/null && pwd)" || { echo "Loyiha topilmadi: $project" >&2; failed=$((failed + 1)); continue; }
    name="$(basename "$project")"
    log="$out/$name.log"
    # A copy without the exports and the editor's own state (layouts, caches, autosaves).
    mkdir -p "$work/home-$name"
    tar -C "$(dirname "$project")" --exclude="$name/Build" --exclude="$name/.zamin" -cf - "$name" | tar -C "$work" -xf -
    env_shots=()
    if [[ $shots -eq 1 ]]; then
        rm -rf "$out/$name-shots"
        env_shots=(ZAMIN_EDITOR_SMOKE_SHOTS="$out/$name-shots")
    fi
    echo "Editor smoke testi: $name (log: $log)"
    start=$(date +%s)
    # ZAMIN_MAX_FRAMES: a safety net (the test closes the editor itself) that also keeps the frame rate unlimited.
    env HOME="$work/home-$name" ZAMIN_EDITOR_OPEN_ALL_PANELS=1 ZAMIN_GL_DEBUG=1 ZAMIN_MAX_FRAMES=20000 "${env_shots[@]}" \
        timeout 900 "${runner[@]}" "$editor" "$work/$name" >"$log.raw" 2>&1
    code=$?
    seconds=$(( $(date +%s) - start ))
    # (The terminal colours out.)
    sed 's/\x1b\[[0-9;]*m//g' "$log.raw" >"$log" && rm -f "$log.raw"
    result="$(grep -oE "Editor smoke testi o'(tdi|tmadi): .*" "$log" | tail -n 1)"
    steps="$(echo "$result" | grep -oE '[0-9]+/[0-9]+ qadam' | cut -d' ' -f1)"
    frames="$(echo "$result" | grep -oE '[0-9]+ kadr' | cut -d' ' -f1)"
    errors="$(echo "$result" | grep -oE '[0-9]+ xato' | head -n 1 | cut -d' ' -f1)"
    if [[ $code -eq 0 && "$result" == *"o'tdi"* ]]; then
        echo "  [OK]   $result ($seconds s)"
        echo "| $name | o'tdi | $steps | $frames | $seconds s | 0 |" >>"$summary"
    else
        failed=$((failed + 1))
        reason="chiqish kodi $code"
        [[ $code -eq 124 ]] && reason="vaqt tugadi (900 s)"
        [[ $code -gt 128 && $code -ne 124 ]] && reason="dastur yiqildi (signal $((code - 128)))"
        [[ -n "$result" ]] && reason="$reason; $result"
        echo "  [XATO] $reason"
        grep -E "Smoke xatosi|IM_ASSERT|Assertion|crash|Crash" "$log" | head -n 20 | sed 's/^/           /'
        echo "| $name | **o'tmadi** ($reason) | ${steps:--} | ${frames:--} | $seconds s | ${errors:--} |" >>"$summary"
    fi
    [[ $shots -eq 1 && -d "$out/$name-shots" ]] && echo "  rasmlar: $out/$name-shots ($(ls "$out/$name-shots" | wc -l) ta)"
done

if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    { cat "$summary"; echo; } >>"$GITHUB_STEP_SUMMARY"
fi
if [[ $keep -eq 1 ]]; then echo "Ish papkasi: $work"; else rm -rf "$work"; fi
echo
if [[ $failed -eq 0 ]]; then
    echo "Editor smoke testi: hammasi o'tdi (${#projects[@]} loyiha; hisobot: $summary)"
    exit 0
fi
echo "Editor smoke testi: $failed ta loyiha o'tmadi (hisobot: $summary)"
exit 1
