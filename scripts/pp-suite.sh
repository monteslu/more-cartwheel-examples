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
#            romdev told to render the same cart with GL (run-cart --dual-gl = loadMedia wgpu:false, always with gpuPower:low-power) against native
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
# a few gates per lock hold (each hold stays well under the fair share)
chunks() { local n=$1; shift; local line=() ; for x in "$@"; do line+=("$x"); if [ ${#line[@]} -ge $n ]; then echo "${line[*]}"; line=(); fi; done; [ ${#line[@]} -gt 0 ] && echo "${line[*]}"; return 0; }
# one short hold of the GPU lock
locked() { local tag="$1"; shift; (cd "$E" && node tools/with-gpu-suite.mjs "pp-$tag" timeout 600 "$@"); } # the timeout bounds the hold, not the wait in the queue
# software GL off the lock: llvmpipe, no display, no GPU device touched
# shared machine: two llvmpipe threads, a job pool of two, nice 10 (a soak at full width took 700% CPU)
soft() { env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=2 CW_WORKERS=2 SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy nice -n 10 "$@"; }
for p in "${parts[@]}"; do
  case "$p" in
    expect) chunks 4 $(names new) | while read -r c; do locked exp node "$ROOT/games/partypack/tools/pp-expect.mjs" $c >> "$O/expect.log" 2>&1; echo "expect [$c] exit $?"; done ;;
    gates) chunks ${PP_CHUNK:-3} $(names) | while read -r c; do locked gl node "$E/tests/gate.mjs" --gates-file $GF $c --hosts native >> "$O/gates.log" 2>&1; echo "gates [$c] exit $?"; done ;;
    wgpu) chunks ${PP_CHUNK:-3} $(names) | while read -r c; do locked wg node "$E/tests/gate.mjs" --gates-file $GF $c --hosts native --gpu webgpu >> "$O/wgpu.log" 2>&1; echo "wgpu [$c] exit $?"; done ;;
    hosts) chunks ${PP_CHUNK_HOSTS:-2} $CROSS | while read -r c; do locked dual node "$E/tests/gate.mjs" --gates-file $GF $c --hosts native,romdev,chromium --gpu webgpu >> "$O/hosts.log" 2>&1; echo "hosts [$c] exit $?"; done ;;
    balance) soft timeout 3000 bash "$ROOT/games/partypack/tools/balance.sh" "${PP_N:-100}" > "$O/balance.log" 2>&1; echo "balance exit $?" ;;
    soak) # ten minutes of game time per mini-game. The 2D games run sim-only (PP_BATCH: four whole tournaments of that
          # one game inside the simulation, about 10 to 12 minutes of play, no rendering); the 3D games need their scene,
          # so they render 36000 frames on software GL. One process at a time, no lock.
      [ "${PP_SOAK_3D_ONLY:-}" = 1 ] || for g in hurdle echo beatbox quiz stack sumo coins tiles; do
        PP_GAME=$g PP_BATCH=4 soft timeout 1800 node "$E/tools/run-cart.mjs" --host native --app partypack_auto --frames 4 --allow-skipped \
          --state "$O/soak-$g.json" --log "$O/soak-$g.log" > "$O/soak-$g.out" 2>&1; echo "soak $g exit $? $(grep -c '^GAME ' "$O/soak-$g.log") games $(grep -o 'BATCH DONE [0-9]*' "$O/soak-$g.log")"; done
      for g in blast raft loop; do
        PP_GAME=$g CW_RENDER_SCALE=0.5 CW_RUN_TIMEOUT_MS=14400000 soft timeout 14500 node "$E/tools/run-cart.mjs" --host native --app partypack_auto --frames 36000 --allow-skipped \
          --state "$O/soak-$g.json" --log "$O/soak-$g.log" > "$O/soak-$g.out" 2>&1; echo "soak $g exit $?"; done ;;
  esac
done
