#!/bin/sh
# A frame from jetski_auto on a software GL, no GPU lock: tools/shot-cpu.sh <out.png> <track metres> <frames> [swell]
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$1; warp=$2; frames=${3:-40}; swell=${4:-0.8}
cd "$root/build/native/project/games/jetski" || exit 1
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy JETSKI_WARP=$warp JETSKI_SWELL=$swell CW_FRAMES=$frames CW_SCREENSHOT="$out" \
  nice -n 19 ./jetski_auto > /dev/null 2> "$root/out/shot-e.txt"
echo "exit $?"; grep -E "no program|missing|error" "$root/out/shot-e.txt" | head -5
