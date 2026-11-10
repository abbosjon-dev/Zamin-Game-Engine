#!/usr/bin/env bash
# QEMU masks all optional x86 SIMD/bit instructions. A real falling-body test exercises Jolt.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${ZAMIN_BUILD_DIR:-$root/build}"
[[ "$build" != /* ]] && build="$root/$build"
command -v qemu-x86_64 >/dev/null || { echo 'Install qemu-user to run the CPU compatibility test' >&2; exit 2; }
grep -q '^ZAMIN_ENABLE_AVX2:BOOL=OFF$' "$build/CMakeCache.txt" || { echo 'A portable build is required' >&2; exit 2; }
cpu='qemu64,-sse3,-ssse3,-sse4.1,-sse4.2,-avx,-avx2,-fma,-f16c,-bmi1,-bmi2,-abm,-popcnt'
cd "$root"
timeout 120 qemu-x86_64 -cpu "$cpu" "$build/bin/ZaminTests" TerrainJoltCollisionAndPlayCopy
