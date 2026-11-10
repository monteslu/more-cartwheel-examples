#!/bin/bash
# The weather's no-strobe check: each stretch on the built dual cart through romdev, every frame of it checked in one
# frame({op:'flash'}) call by the engine's tools/flash-check.mjs, with its control (the same frames under an 8 Hz strobe)
# that must fail. A storm on a course made for evening sun (lightning, rain, spume), a calm (the sun's glint off glassy
# water), and Gale Bank's storm with the big waves going through.
#   tests/jetski-weather-flash.sh gl|webgpu
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CW_PROJECT_ROOT="$ROOT"
gpu="${1:-webgpu}" fail=0
run() { # name frames from cmds
  echo "== $1 ($gpu)"
  node "$ROOT/deps/cartwheel-engine/tools/flash-check.mjs" --host romdev --app jetski_auto --gpu "$gpu" --frames "$2" --from "$3" --start-cmds "$4" --label "$1" || fail=1
}
run jetski-wx-storm 1200 40 "course 0;weather 2;race"
run jetski-wx-calm 900 40 "course 1;weather 0;race"
run jetski-wx-event 1500 40 "course 4;weather 2;race;go 900;event"
exit $fail
