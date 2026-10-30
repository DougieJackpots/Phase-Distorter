#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${VITASDK:-}" || ! -f "$VITASDK/share/vita.toolchain.cmake" ]]; then
  echo "VITASDK must point to an installed VitaSDK (expected share/vita.toolchain.cmake)." >&2
  exit 1
fi
export PATH="$VITASDK/bin:$PATH"
for tool in cmake python3 ca65 ld65; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "Missing build prerequisite: $tool" >&2
    exit 1
  }
done

root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${BUILD_DIR:-$root/build-vita}"
python3 "$root/vita/validate_assets.py"
cmake -S "$root/vita" -B "$build_dir" \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --target PhaseDistorter.vpk-vpk --parallel "${JOBS:-2}"
echo "VPK: $build_dir/phase-distorter-core/PhaseDistorter.vpk"
