#!/usr/bin/env bash
# Balance runs for the CPU skill levels and the board: whole tournaments of the 2D mini-games played in fast
# forward by partypack_auto (PP_BATCH), under ONE GPU suite lock:
#   node deps/cartwheel-engine/tools/with-gpu-suite.mjs pp-balance bash games/partypack/tools/balance.sh [tournaments]
# Output: out/balance/<config>.log, then `node games/partypack/tools/balance-report.mjs out/balance`.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
N="${1:-120}"
OUT="$ROOT/out/balance"
mkdir -p "$OUT"
export CW_PROJECT_ROOT="$ROOT"
run() { # <name> <skills> <rounds option>
  PP_BATCH="$N" PP_SKILLS="$2" PP_ROUNDS="${3:-0}" SDL_AUDIODRIVER=dummy \
    node "$ROOT/deps/cartwheel-engine/tools/run-cart.mjs" --host native --app partypack_auto --frames 4 --allow-skipped --log "$OUT/$1.log" > "$OUT/$1.out" 2>&1 || echo "run $1 exited $?"
}
run mid 1,1,1,1
run hard-vs-mid 2,1,1,1
run mixed 2,1,0,1
run easy 0,0,0,0
run hard 2,2,2,2
run mid-long 1,1,1,1 2
