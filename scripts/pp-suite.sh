#!/usr/bin/env bash
# One GPU suite for the partypack checks, run as
#   node deps/cartwheel-engine/tools/with-gpu-suite.mjs pp-suite bash scripts/pp-suite.sh [part ...]
# parts: expect (record exact values for gates without any), gates (native, GL), wgpu (gates on WebGPU),
# hosts (native + romdev + chromium legs of the cross-host gates), balance, soak (ten minutes of each mini-game).
# Default: expect gates wgpu hosts balance. Every part logs to out/pp/<part>.log and the script reports each exit code.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CW_PROJECT_ROOT="$ROOT"
E="$ROOT/deps/cartwheel-engine"
mkdir -p "$ROOT/out/pp"
parts=("$@"); [ ${#parts[@]} -eq 0 ] && parts=(expect gates wgpu hosts balance)
G="--gates-file partypack-gates.json"
for p in "${parts[@]}"; do
  case "$p" in
    expect) timeout 3000 node "$ROOT/games/partypack/tools/pp-expect.mjs" > "$ROOT/out/pp/expect.log" 2>&1 ;;
    gates) timeout 6000 node "$E/tests/gate.mjs" $G --hosts native > "$ROOT/out/pp/gates.log" 2>&1 ;;
    wgpu) timeout 6000 node "$E/tests/gate.mjs" $G --hosts native --gpu webgpu > "$ROOT/out/pp/wgpu.log" 2>&1 ;;
    hosts) timeout 6000 node "$E/tests/gate.mjs" $G partypack-tournament-short partypack-menu partypack-join partypack-drop --hosts native,romdev,chromium > "$ROOT/out/pp/hosts.log" 2>&1 ;;
    balance) timeout 3000 bash "$ROOT/games/partypack/tools/balance.sh" "${PP_N:-100}" > "$ROOT/out/pp/balance.log" 2>&1 ;;
    soak) # ten minutes (36000 frames) of each mini-game on repeat
      for g in hurdle echo beatbox quiz stack blast raft loop sumo coins tiles; do
        PP_GAME=$g CW_FRAMES=36000 timeout 1500 node "$E/tools/run-cart.mjs" --host native --app partypack_auto --frames 36000 --allow-skipped \
          --state "$ROOT/out/pp/soak-$g.json" --log "$ROOT/out/pp/soak-$g.log" > "$ROOT/out/pp/soak-$g.out" 2>&1; echo "soak $g exit $?" ;
      done > "$ROOT/out/pp/soak.log" 2>&1 ;;
  esac
  echo "part $p exit $?"
done
