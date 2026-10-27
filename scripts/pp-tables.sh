#!/usr/bin/env bash
# pp-tables.sh: regenerate the program tables from the states the software runs found missing (out/proglog/*/partypack_auto.log).
# Mock-GPU capture only, no device. Then rebuild.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="$ROOT/deps/cartwheel-engine"
ARGS=()
for f in "$ROOT"/out/proglog/*/partypack_auto*.log "$ROOT"/out/proglog/*/partypack*.log; do [ -f "$f" ] && ARGS+=(--missing "$f"); done
cd "$ENGINE"
node tools/programs.mjs --table "$ROOT/games/partypack/programs/partypack.txt" --build "$ROOT/build/native" "${ARGS[@]}"
ninja -C "$ROOT/build/native" -j4 partypack_auto partypack
