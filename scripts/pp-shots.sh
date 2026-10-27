#!/usr/bin/env bash
# pp-shots.sh <tag> <gl|webgpu> <frames> game...: one screenshot per mini-game (PP_GAME) into out/v/<tag>-<game>-<host>.png.
# One GPU suite lock for the whole batch. A game may be game:frames to pick its own frame.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="$ROOT/deps/cartwheel-engine"
tag=$1; host=$2; fr=$3; shift 3
mkdir -p "$ROOT/out/v"
export PP_TAG=$tag PP_HOST=$host PP_FR=$fr PP_ROOT=$ROOT PP_LIST="$*"
cd "$ENGINE"
node tools/with-gpu-suite.mjs "pp-$tag" bash -c '
for g in $PP_LIST; do
  n=${g%%:*}; f=$PP_FR; case $g in *:*) f=${g##*:};; esac
  APP="$PP_ROOT/build/native/project/games/partypack/partypack_auto"
  [ "$PP_HOST" = webgpu ] && APP="$PP_ROOT/build/native/project/games/partypack/partypack_auto"
  PP_GAME=$n CW_GPU=$PP_HOST CW_FRAMES=$f CW_WARMUP_SPREAD=0 SDL_AUDIODRIVER=dummy CW_SCREENSHOT="$PP_ROOT/out/v/$PP_TAG-$n-$PP_HOST.png" timeout 120 "$APP" >"$PP_ROOT/out/v/$PP_TAG-$n-$PP_HOST.log" 2>&1 || echo "FAIL $n"
done' >/dev/null
