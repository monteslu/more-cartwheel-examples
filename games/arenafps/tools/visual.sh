#!/usr/bin/env bash
# Native-scale screenshots in one GPU-suite hold:  deps/cartwheel-engine/tools/with-gpu-suite.mjs arenafps-visual bash tools/visual.sh <out dir> spec...
# spec = name:arena:players:gpu:frames[:KEY=val,KEY=val]   (gpu gl | webgpu; the env pairs are passed to the game, e.g. ARENA_T_sun=2)
set -u
GAME="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$GAME/../.." && pwd)"
OUT="$1"; shift
mkdir -p "$OUT"
for spec in "$@"; do
  IFS=: read -r name arena players gpu frames extra <<<"$spec"
  envs=()
  if [ -n "${extra:-}" ]; then IFS=, read -ra envs <<<"$extra"; fi
  ARENA_ID=$arena ARENA_PLAYERS=$players ARENA_BOTS=${ARENA_BOTS:-5} CW_GPU=$gpu CW_FRAMES=$frames CW_WARMUP_SPREAD=0 SDL_AUDIODRIVER=dummy \
    CW_SCREENSHOT="$OUT/$name.png" env "${envs[@]}" timeout 240 "$ROOT/build/native/project/games/arenafps/arenafps_auto" >"$OUT/$name.log" 2>&1 || echo "FAIL $name"
  grep -E "skipped|error|missing" "$OUT/$name.log" | head -3
done
