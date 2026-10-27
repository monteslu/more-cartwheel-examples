#!/usr/bin/env bash
# pp-cycle.sh <tag> <frames> <programs:yes|no> <hosts: gl|webgpu|"gl webgpu"> game...
# One GPU suite lock for the whole cycle: regenerate the program tables for the 3D games named (PP_GAME runs of
# tools/programs.mjs), rebuild, then one screenshot per game and host into out/v/<tag>-<game>-<host>.png.
# A game may be name:frames to pick its own frame.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="$ROOT/deps/cartwheel-engine"
tag=$1; fr=$2; prog=$3; hosts=$4; shift 4
mkdir -p "$ROOT/out/v"
export PP_TAG=$tag PP_FR=$fr PP_ROOT=$ROOT PP_LIST="$*" PP_PROG=$prog PP_HOSTS="$hosts"
cd "$ENGINE"
node tools/with-gpu-suite.mjs "pp-$tag" bash -c '
set -u
if [ "$PP_PROG" = yes ]; then
  for g in $PP_LIST; do n=${g%%:*}; case $n in blast|raft|loop|hub) PP_GAME=$n node tools/programs.mjs partypack_auto --frames 800 --build "$PP_ROOT/build/native" >"$PP_ROOT/out/v/programs-$n.log" 2>&1 || echo "programs FAIL $n";; esac; done
  ninja -C "$PP_ROOT/build/native" -j4 partypack_auto partypack >"$PP_ROOT/out/v/ninja-$PP_TAG.log" 2>&1 || echo "ninja FAIL"
fi
APP="$PP_ROOT/build/native/project/games/partypack/partypack_auto"
for h in $PP_HOSTS; do
  for g in $PP_LIST; do
    n=${g%%:*}; f=$PP_FR; case $g in *:*) f=${g##*:};; esac
    PP_GAME=$n CW_GPU=$h CW_FRAMES=$f CW_WARMUP_SPREAD=0 SDL_AUDIODRIVER=dummy CW_SCREENSHOT="$PP_ROOT/out/v/$PP_TAG-$n-$h.png" timeout 150 "$APP" >"$PP_ROOT/out/v/$PP_TAG-$n-$h.log" 2>&1 || echo "FAIL $n $h"
  done
done' > "$PP_ROOT/out/v/cycle-$tag.log" 2>&1
