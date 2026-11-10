#!/usr/bin/env bash
# The jetski stand-up gate set in four groups, each run one gate after another so a GPU pool job holds one slot:
#   bash tests/jetski-standup-suite.sh a|b|c|d   (logs in out/gate/<name>.log)
cd "$(dirname "$0")/.."
mkdir -p out/gate
run() { n=$1; shift; echo "=== $n"; JETSKI_SESSION=jsg-$n "$@" > out/gate/$n.log 2>&1; echo "exit $?" >> out/gate/$n.log; grep -E '^FAIL|^all ok|FAILED|^exit' out/gate/$n.log | tail -4; }
G=${1:-a}
if [ $G = a ]; then
run craft-models node tests/jetski-craft-models.mjs
run su-gl node tests/jetski-standup.mjs --gl --baseline build/baseline/jetski_auto.wasc
run su-wgpu node tests/jetski-standup.mjs --webgpu
fi
if [ $G = b ]; then
run nc-w node tests/jetski-newcomer.mjs --webgpu --craft both --finish
fi
if [ $G = c ]; then
run nc-g node tests/jetski-newcomer.mjs --gl --craft both --finish
fi
if [ $G = d ]; then
run fl-s env JETSKI_CRAFT=1 node tests/jetski-flash.mjs
run fl node tests/jetski-flash.mjs
for g in air buoys cam-cut hud-words minimap; do run $g node tests/jetski-$g.mjs; done
fi
echo "=== chain $G done"
