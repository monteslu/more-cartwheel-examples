#!/usr/bin/env bash
# One GPU suite for the partypack checks, run as
#   node deps/cartwheel-engine/tools/with-gpu-suite.mjs pp-suite bash scripts/pp-suite.sh [part ...]
# parts: gates (all hosts, GL), wgpu (gates on WebGPU), balance, soak, perf. Default: gates wgpu balance.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CW_PROJECT_ROOT="$ROOT"
E="$ROOT/deps/cartwheel-engine"
mkdir -p "$ROOT/out/pp"
parts=("$@"); [ ${#parts[@]} -eq 0 ] && parts=(gates wgpu balance)
for p in "${parts[@]}"; do
  case "$p" in
    gates) node "$E/tests/gate.mjs" --gates-file partypack-gates.json ;;
    wgpu) node "$E/tests/gate.mjs" --gates-file partypack-gates.json --gpu webgpu ;;
    balance) bash "$ROOT/games/partypack/tools/balance.sh" "${PP_N:-100}" ;;
    soak) # ten minutes (36000 frames) of each mini-game on repeat
      for g in hurdle echo beatbox quiz stack blast raft loop sumo coins tiles; do
        PP_GAME=$g CW_FRAMES=36000 node "$E/tools/run-cart.mjs" --host native --app partypack_auto --frames 36000 --allow-skipped \
          --state "$ROOT/out/pp/soak-$g.json" --log "$ROOT/out/pp/soak-$g.log" > "$ROOT/out/pp/soak-$g.out" 2>&1; echo "soak $g exit $?" ;
      done ;;
  esac
done
