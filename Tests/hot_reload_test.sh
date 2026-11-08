#!/usr/bin/env bash
# Script hot reload test: a small project (Tests/Fixtures/HotReload: one C++ script, a scene that gives its fields
# values) is copied into a temporary folder and opened in ZaminEditor (under xvfb when there is no display):
#   1. the editor builds the script module itself (it is missing: cmake configure + build, as on a new project),
#      loads it and plays the scene (ZAMIN_EDITOR_HOT_RELOAD_TEST) - version 1 of the script runs (moves along X)
#      with the scene's field values;
#   2. the test changes the source (version 2: moves along Y, one more field) and builds the module again
#      outside the editor (cmake --build, as an IDE does) while the editor runs;
#   3. the editor sees the new module, reloads it (stops the game first) and plays again: version 2 runs, with
#      the same field values the scene gave (they survive the reload), the new field with its default;
#   4. nothing logged an error, the scene file was not changed, the editor closed with exit code 0.
#   Tests/hot_reload_test.sh
#   Tests/hot_reload_test.sh --keep                  keep the temporary folder (the project, the logs)
#   Tests/hot_reload_test.sh --work /tmp/zamin-hr    use (and keep) that folder
#   ZAMIN_BUILD_DIR=build-clang Tests/hot_reload_test.sh
# Exit code: 0 when every check passed, 1 when one failed, 2 when the editor is missing.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ZAMIN_BUILD_DIR:-$root/build}"
[[ "$build" != /* ]] && build="$root/$build"
editor="$build/bin/ZaminEditor"
keep=0
work=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --keep) keep=1; shift ;;
        --work) work="$2"; keep=1; shift 2 ;;
        -h|--help) sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "noma'lum variant: $1 (--help)" >&2; exit 2 ;;
    esac
done
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
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe

[[ -z "$work" ]] && work="$(mktemp -d "${TMPDIR:-/tmp}/zamin-hot-reload.XXXXXX")"
project="$work/HotReload"
rm -rf "$project" "$work/home"
mkdir -p "$work/home"
cp -r "$root/Tests/Fixtures/HotReload" "$project"
source_file="$project/Source/HotReloadProbe.cpp"
log="$work/editor.log"

failures=0
pass() { echo "  [OK]   $1"; }
fail() { echo "  [XATO] $1"; failures=$((failures + 1)); }
# Waits until the editor's output has a line matching $1 (at most $2 s); false when the editor ended first.
wait_for() {
    local deadline=$(( $(date +%s) + $2 ))
    while (( $(date +%s) < deadline )); do
        grep -qE "$1" "$log" 2>/dev/null && return 0
        kill -0 "$pid" 2>/dev/null || { grep -qE "$1" "$log" 2>/dev/null; return $?; }
        sleep 0.5
    done
    return 1
}
clean() { sed 's/\x1b\[[0-9;]*m//g' "$log"; }

echo "Hot reload testi: $project"
start=$(date +%s)
# ZAMIN_MAX_FRAMES: a safety net that also keeps the frame rate unlimited (the editor closes itself).
env HOME="$work/home" ZAMIN_EDITOR_HOT_RELOAD_TEST=60 ZAMIN_MAX_FRAMES=1000000 \
    timeout 900 "${runner[@]}" "$editor" "$project" >"$log" 2>&1 &
pid=$!

# 1. The editor's own build of the missing module, version 1 playing.
if wait_for "HotReloadProbe v1: 20 kadr" 600; then
    pass "editor modulni o'zi kompilyatsiya qildi va yukladi, 1-versiya o'ynadi ($(( $(date +%s) - start )) s)"
else
    fail "1-versiya ishga tushmadi (log: $log)"
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    clean | grep -E "error|Error|xato|XATO" | head -n 10 | sed 's/^/           /'
    echo "Hot reload testi: $failures ta xato (papka saqlandi: $work)"
    exit 1
fi

# 2. The source changes; the module is built again outside the editor.
sed -i 's/^#define HOT_RELOAD_VERSION 1$/#define HOT_RELOAD_VERSION 2/' "$source_file"
grep -q '^#define HOT_RELOAD_VERSION 2$' "$source_file" || fail "manba fayli o'zgartirilmadi"
rebuild_start=$(date +%s)
if cmake --build "$project/Build" >"$work/rebuild.log" 2>&1; then
    pass "o'zgargan skript tashqarida qayta kompilyatsiya qilindi ($(( $(date +%s) - rebuild_start )) s)"
else
    fail "qayta kompilyatsiya xatosi (log: $work/rebuild.log)"
fi

# 3. The editor reloads it and plays again; it closes itself 60 frames later.
wait "$pid"
code=$?
echo "  editor: $(( $(date +%s) - start )) s (log: $log)"
output="$(clean)"
if [[ $code -eq 0 ]]; then pass "editor yopildi (chiqish kodi 0)"; else fail "editor chiqish kodi $code"; fi
check_line() { # $1 regex, $2 what it shows
    local line
    line="$(echo "$output" | grep -oE "$1" | head -n 1)"
    if [[ -n "$line" ]]; then pass "$2: $line"; else fail "$2 - topilmadi: $1"; fi
}
check_line "Skriptlar muvaffaqiyatli kompilyatsiya qilindi \([0-9.]+ s\)" "editor kompilyatsiyasi"
check_line "HotReloadProbe v1: Speed=7.5 Label=saqlangan Count=3" "1-versiya sahnadagi qiymatlar bilan"
check_line "HotReloadProbe v1: 20 kadr, pozitsiya \([1-9][0-9.]*, 0\.00\)" "1-versiya X bo'ylab yurdi"
check_line "Skript moduli o'zgardi - qayta yuklanmoqda" "editor yangi modulni ko'rdi"
check_line "Skriptlar qayta yuklandi \(hot reload\)" "hot reload"
check_line "HotReloadProbe v2: Speed=7.5 Label=saqlangan Count=3 Extra=42" "2-versiya: maydonlar saqlandi, yangi maydon standart qiymatda"
check_line "HotReloadProbe v2: 20 kadr, pozitsiya \(0\.00, [1-9][0-9.]*\)" "2-versiya (yangi kod) Y bo'ylab yurdi"
loads=$(echo "$output" | grep -c "Skript moduli yuklandi: libHotReload.so")
if [[ $loads -eq 2 ]]; then pass "modul 2 marta yuklandi"; else fail "modul $loads marta yuklandi (2 kutilgan)"; fi
# The editor's log file: no errors (also none from a half written module).
editor_log="$(ls -t "$work/home/.local/share/zamin/editor/logs/"*.log 2>/dev/null | head -n 1)"
if [[ -z "$editor_log" ]]; then
    fail "editor log fayli topilmadi"
else
    errors="$(grep -E '^\[[^]]*\] (ERROR|CRITICAL) ' "$editor_log")"
    if [[ -z "$errors" ]]; then pass "editor logida xato yo'q"; else fail "editor logida xatolar:"; echo "$errors" | head -n 10 | sed 's/^/           /'; fi
fi
if cmp -s "$root/Tests/Fixtures/HotReload/Assets/Scenes/Main.zscene" "$project/Assets/Scenes/Main.zscene"; then
    pass "sahna fayli o'zgarmadi"
else
    fail "sahna fayli o'zgardi"
fi

echo
if [[ $failures -eq 0 ]]; then
    echo "Hot reload testi: hammasi o'tdi ($(( $(date +%s) - start )) s)"
    if [[ $keep -eq 1 ]]; then echo "Papka: $work"; else rm -rf "$work"; fi
    exit 0
fi
echo "Hot reload testi: $failures ta xato (papka saqlandi: $work)"
exit 1
