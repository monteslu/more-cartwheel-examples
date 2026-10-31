#!/bin/sh
# ms per frame, through the machine's GPU lock on the 890M: tools/perf.sh <gl|webgpu> [frames]
# prints perf.ms: game tick, frame GPU, water pass GPU, scene pass GPU, sync, water CPU, scene CPU
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
gpu=${1:-gl}; frames=${2:-600}
cd "$root/deps/cartwheel-engine" && CW_PROJECT_ROOT="$root" node tools/run-cart.mjs --host native --gpu "$gpu" --app jetski_auto --frames "$frames" --shot "$root/out/perf-$gpu.png" --state "$root/out/perf-$gpu.json" >/dev/null
node -e "const s=JSON.parse(require('fs').readFileSync(process.argv[1]));console.log(process.argv[2],s['perf.ms'].map(v=>v.toFixed(2)).join(' '))" "$root/out/perf-$gpu.json" "$gpu"
