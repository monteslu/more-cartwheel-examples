#!/bin/sh
# Add the states a software GL run of the game asks for to its program table, no GPU. usage: tools/programs-cpu.sh [frames]
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
log="$root/out/plog"; mkdir -p "$log"
cd "$root/build/native/project/games/jetski" || exit 1
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy CW_PROGRAMS_LOG_DIR="$log" JETSKI_SMALL=1 CW_FRAMES=${1:-20} nice -n 19 ./jetski_auto > /dev/null 2>&1
mv "$log/jetski_auto.log" "$log/jetski.log" 2>/dev/null
cd "$root/deps/cartwheel-engine" && CW_PROJECT_ROOT="$root" nice -n 15 node tools/programs.mjs --logs "$log" --no-run --build "$root/build/native" jetski 2>&1 | tail -2
