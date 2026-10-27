#!/usr/bin/env bash
# Builds and runs the simulation rules tests (plain C: needs only cc and the engine's motion.c).
# CARTWHEEL_ENGINE points at an engine checkout (default: deps/cartwheel-engine).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="${CARTWHEEL_ENGINE:-$HERE/../../deps/cartwheel-engine}"
OUT="${TMPDIR_FIGHTER:-$HERE/../../out}"
mkdir -p "$OUT"
cc -std=gnu17 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-unused-parameter -I"$ENGINE/include" -I"$HERE" \
  "$HERE/tests/simtest.c" "$HERE/sim.c" "$HERE/ai.c" "$ENGINE/src/input/motion.c" -o "$OUT/simtest"
"$OUT/simtest" "$HERE/assets/data/moves.txt" "${1:-24}"
