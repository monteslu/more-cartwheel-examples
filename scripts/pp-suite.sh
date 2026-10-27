#!/usr/bin/env bash
# The partypack checks, in pieces that respect the GPU lock fair share (each with-gpu-suite call holds the lock
# for about 10 minutes at most; long runs never take it):
#   bash scripts/pp-suite.sh [part ...]       (run from anywhere; it takes the lock itself, one call per piece)
# parts:
#   expect   record exact `expect` values for gates that have none (one lock call per gate)
#   gates    every gate on native GL (one lock call per gate)
#   wgpu     every gate on native WebGPU (one lock call per gate)
#   hosts    the ONE shipped cart, the dual GL+WebGPU .wasc of build/cart-wgpu, on every host (one lock call per
#            gate): native WebGPU is the reference, romdev and chromium on WebGPU are "dual cart on WebGPU host",
#            romdev told to render the same cart with GL (run-cart --dual-gl = loadMedia wgpu:false) against native
#            GL is "dual cart on GL host". The GL-only build/cart tree does not ship and is not tested.
#   balance  the balance harness, software GL, no lock
#   soak     ten minutes (36000 frames) of each mini-game, software GL, no lock, one process at a time
# Default: expect gates wgpu hosts balance. Logs go to out/pp/.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CW_PROJECT_ROOT="$ROOT"
E="$ROOT/deps/cartwheel-engine"
O="$ROOT/out/pp"
mkdir -p "$O"
parts=("$@"); [ ${#parts[@]} -eq 0 ] && parts=(expect gates wgpu hosts balance)
GF=partypack-gates.json
names() { node -e "for (const g of JSON.parse(require('fs').readFileSync('$ROOT/tests/$GF','utf8'))) if (!process.argv[1] || !g.expect) console.log(g.name)" "${1:-}"; }
CROSS="partypack-tournament-short partypack-menu partypack-join partypack-drop partypack-join-press"
# one short hold of the GPU lock
locked() { local tag="$1"; shift; (cd "$E" && timeout 900 node tools/with-gpu-suite.mjs "pp-$tag" "$@"); }
# software GL off the lock: llvmpipe, no display, no GPU device touched
soft() { env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy "$@"; }
for p in "${parts[@]}"; do
  case "$p" in
    expect) for n in $(names new); do locked "exp-$n" node "$ROOT/games/partypack/tools/pp-expect.mjs" "$n" >> "$O/expect.log" 2>&1; echo "expect $n exit $?"; done ;;
    gates) for n in $(names); do locked "gl-$n" node "$E/tests/gate.mjs" --gates-file $GF "$n" --hosts native >> "$O/gates.log" 2>&1; echo "gate $n exit $?"; done ;;
    wgpu) for n in $(names); do locked "wg-$n" node "$E/tests/gate.mjs" --gates-file $GF "$n" --hosts native --gpu webgpu >> "$O/wgpu.log" 2>&1; echo "wgpu $n exit $?"; done ;;
    hosts) for n in $CROSS; do locked "dual-$n" node "$E/tests/gate.mjs" --gates-file $GF "$n" --hosts native,romdev,chromium --gpu webgpu >> "$O/hosts.log" 2>&1; echo "hosts $n exit $?"; done ;;
    balance) soft timeout 3000 bash "$ROOT/games/partypack/tools/balance.sh" "${PP_N:-100}" > "$O/balance.log" 2>&1; echo "balance exit $?" ;;
    soak) # ten minutes of game time per mini-game. The 2D games run sim-only (PP_BATCH: four whole tournaments of that
          # one game inside the simulation, about 10 to 12 minutes of play, no rendering); the 3D games need their scene,
          # so they render 36000 frames on software GL. One process at a time, no lock.
      for g in hurdle echo beatbox quiz stack sumo coins tiles; do
        PP_GAME=$g PP_BATCH=4 soft timeout 1800 node "$E/tools/run-cart.mjs" --host native --app partypack_auto --frames 4 --allow-skipped \
          --state "$O/soak-$g.json" --log "$O/soak-$g.log" > "$O/soak-$g.out" 2>&1; echo "soak $g exit $? $(grep -c '^GAME ' "$O/soak-$g.log") games $(grep -o 'BATCH DONE [0-9]*' "$O/soak-$g.log")"; done
      for g in blast raft loop; do
        PP_GAME=$g CW_RUN_TIMEOUT_MS=14400000 soft timeout 14500 node "$E/tools/run-cart.mjs" --host native --app partypack_auto --frames 36000 --allow-skipped \
          --state "$O/soak-$g.json" --log "$O/soak-$g.log" > "$O/soak-$g.out" 2>&1; echo "soak $g exit $?"; done ;;
  esac
done
