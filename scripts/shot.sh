#!/usr/bin/env bash
# shot.sh <app> <frames> <out.png> [gl|webgpu]: a screenshot of a game's autopilot, through the GPU suite lock
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="$ROOT/deps/cartwheel-engine"
APP="$ROOT/build/native/project/games/${1%%_*}/$1"
cd "$ENGINE"
CW_GPU="${4:-gl}" CW_FRAMES="$2" CW_WARMUP_SPREAD=0 SDL_AUDIODRIVER=dummy CW_SCREENSHOT="$3" node tools/with-gpu-suite.mjs "shot-$1" "$APP" >/dev/null
