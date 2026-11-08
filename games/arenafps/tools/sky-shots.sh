#!/usr/bin/env bash
# The skyline sheet's shots: fixed in-arena viewpoints, 1080p, High and Very Low, native autopilot build.
#   bash games/arenafps/tools/sky-shots.sh <out dir> [ARENA_T_x=v ...]   (extra env pairs go to every run)
set -u
GAME="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$GAME/../.." && pwd)"
OUT="$1"; shift
mkdir -p "$OUT"
# name arena x,y,z,yaw,pitch
VIEWS=(
  "t-terrace 2 0,5.7,22,0,0.06"
  "t-corner 2 20,5.7,20,0.785,0.05"
  "t-pit 2 3,1.6,9,0.4,0.12"
  "c-plaza 1 0,1.6,0,0,0.08"
  "c-roof 1 15,5.1,15,0.785,0.04"
  "c-street 1 0,1.6,24,-1.5708,0.06"
  "f-deck 0 0,3.6,0,0.6,0.08"
)
for v in "${VIEWS[@]}"; do
  read -r name arena view <<<"$v"
  if [ -n "${ONLY:-}" ] && [[ ! "$name" =~ $ONLY ]]; then continue; fi
  for q in ${PRESETS:-high verylow}; do
    ARENA_ID=$arena ARENA_PLAYERS=1 ARENA_BOTS=0 ARENA_NOHUD=1 ARENA_VIEW="$view" ARENA_RES=1920x1080 CW_QUALITY=$q CW_GPU=webgpu \
      CW_FRAMES=${FRAMES:-150} CW_WARMUP_SPREAD=0 SDL_AUDIODRIVER=dummy CW_SCREENSHOT="$OUT/$name-$q.png" env "$@" \
      timeout 240 "$ROOT/build/native/games/arenafps/arenafps_auto" >"$OUT/$name-$q.log" 2>&1 || echo "FAIL $name $q"
  done
done
ls "$OUT"/*.png | wc -l
