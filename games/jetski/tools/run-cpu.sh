#!/bin/sh
# Run jetski_auto on the CPU (software GL, no GPU lock), for sim checks. usage: tools/run-cpu.sh <frames> [app]
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
app=${2:-jetski_auto}
mkdir -p "$root/out"
cd "$root/build/native/project/games/jetski" 2>/dev/null || cd "$root/build/native/games/jetski" || exit 1
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy JETSKI_TRACE=1 CW_FRAMES="$1" \
  nice -n 19 "./$app" > "$root/out/t.txt" 2> "$root/out/e.txt"
echo "exit $?"
