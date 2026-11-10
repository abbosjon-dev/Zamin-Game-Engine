#!/usr/bin/env bash
# Run the actual player against the UI-free library; reject accidental editor dependencies.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ZAMIN_BUILD_DIR:-$root/build}"
[[ "$build" != /* ]] && build="$root/$build"
library="$build/bin/runtime-check/libZamin.so"
[[ -f "$library" ]] || { echo "Build ZaminStandalone and ZaminPlayer first" >&2; exit 2; }
work="$(mktemp -d /tmp/zamin-runtime-core.XXXXXX)"
cleanup() {
    local result=$?
    if [[ $result -ne 0 && -f "$work/player.log" ]]; then cat "$work/player.log"; fi
    rm -rf "$work"
}
trap cleanup EXIT
mkdir "$work/home"
nm -D -C "$library" > "$work/symbols"
if grep -E ' (T|W) (ImGui::|ImGuizmo::|Zamin::(UndoHistory|EditorPreferences|ExternalEditor|EditorAutosave|ConsoleFilter|EditorCamera|EditorViewSettings))' "$work/symbols"; then
    echo "Unexpected editor symbols in the runtime library" >&2
    exit 1
fi
env LD_LIBRARY_PATH="$(dirname "$library"):${LD_LIBRARY_PATH:-}" ldd "$build/bin/ZaminPlayer" > "$work/dependencies"
grep -F "$library" "$work/dependencies"
runner=()
[[ -n "${DISPLAY:-}" ]] || runner=(xvfb-run -a -s '-screen 0 1280x720x24 -nolisten tcp')
env HOME="$work/home" LD_LIBRARY_PATH="$(dirname "$library"):${LD_LIBRARY_PATH:-}" \
    LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=2 ZAMIN_GL_DEBUG=1 ZAMIN_MAX_FRAMES=8 \
    timeout 90 "${runner[@]}" "$build/bin/ZaminPlayer" "$root/Projects/MapWorkshop" --no-splash --no-vsync > "$work/player.log" 2>&1
if grep -E 'Assertion|ASSERT|GL_INVALID|GL_OUT_OF_MEMORY|terminate called|Segmentation fault' "$work/player.log"; then
    exit 1
fi
grep -E 'Sahna yuklash vaqti: MapWorkshop \(boshlanish\):.*[1-9][0-9]* obyekt' "$work/player.log" || { cat "$work/player.log"; exit 1; }
echo 'UI-free runtime: no editor service/ImGui symbols; saved terrain map loaded and played'
