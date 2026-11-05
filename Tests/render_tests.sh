#!/usr/bin/env bash
# Rendering regression test (Tests/RenderTests.cpp): renders the reference scenes offscreen and
# compares them with Tests/Reference/*.png.
#   Tests/render_tests.sh             compare (diff images go to build/RenderTests on failure)
#   Tests/render_tests.sh --update    write new references after an intended change of the look
# Any other arguments go to ZaminRenderTests (--filter, --threshold, --max-percent, --list ...).
# Without a display (CI, SSH) it runs under xvfb-run.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ZAMIN_BUILD_DIR:-$root/build}"
exe="$build/bin/ZaminRenderTests"
if [[ ! -x "$exe" ]]; then
    echo "ZaminRenderTests topilmadi: $exe (avval: cmake --build \"$build\" --target ZaminRenderTests)" >&2
    exit 2
fi
cd "$root"
if [[ -z "${DISPLAY:-}" ]] && command -v xvfb-run >/dev/null 2>&1; then
    exec xvfb-run -a -s "-screen 0 1280x720x24" "$exe" "$@"
fi
exec "$exe" "$@"
