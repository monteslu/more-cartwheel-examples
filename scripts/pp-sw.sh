#!/usr/bin/env bash
# pp-sw.sh <tag> <frames> game...: a quick screenshot per game on Mesa's CPU rasteriser (llvmpipe). No GPU is touched, so it
# needs no suite lock: for looking at layouts and program-state logs while the GPU queue is long. Looks differ a little
# from the real GPU (no timing, slower), so final evidence is always taken on the GPU. Logs the program states the tables lack.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tag=$1; fr=$2; shift 2
mkdir -p "$ROOT/out/v"
for g in "$@"; do
  n=${g%%:*}; mkdir -p "$ROOT/out/proglog/$n"; f=$fr; case $g in *:*) f=${g##*:};; esac
  ( cd "$ROOT/build/native/project/games/partypack" && env -u DISPLAY -u WAYLAND_DISPLAY LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=${PP_LPT:-6} SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    PP_GAME=$n CW_FRAMES=$f CW_WARMUP_SPREAD=0 CW_PROGRAMS_LOG_DIR="$ROOT/out/proglog/$n" CW_SCREENSHOT="$ROOT/out/v/$tag-$n-sw.png" timeout 300 ./partypack_auto > "$ROOT/out/v/$tag-$n-sw.log" 2>&1 ) || echo "FAIL $n"
done
