#!/bin/bash
# The weather work's checks in one run, one after another (for a single GPU slot): the weather gate, the newcomer,
# the no-strobe stretches and the refraction check, each on the built cart through romdev, GL and WebGPU.
#   tests/jetski-weather-suite.sh [part ...]   parts: weather[:n,...] newcomer flash refraction gates script:<jetski gate> (default:
#   weather newcomer flash refraction)
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
parts=("$@"); [ ${#parts[@]} -eq 0 ] && parts=(weather newcomer flash refraction)
fail=0
for p in "${parts[@]}"; do
  echo "=== $p"
  case "$p" in
    weather) JETSKI_SESSION=jws-weather-$$ node tests/jetski-weather.mjs || fail=1 ;;
    weather@gl:*|weather@webgpu:*) b="${p#weather@}"; JETSKI_SESSION=jws-weather-$ node tests/jetski-weather.mjs --"${b%%:*}" --only "${b#*:}" || fail=1 ;;
    newcomer@*) JETSKI_SESSION=jws-newcomer-$ node tests/jetski-newcomer.mjs --"${p#newcomer@}" || fail=1 ;;
    weather:*) JETSKI_SESSION=jws-weather-$$ node tests/jetski-weather.mjs --only "${p#weather:}" || fail=1 ;;
    newcomer) JETSKI_SESSION=jws-newcomer-$$ node tests/jetski-newcomer.mjs || fail=1 ;;
    flash) for g in gl webgpu; do tests/jetski-weather-flash.sh "$g" || fail=1; done ;;
    refraction) node tests/jetski-refraction.mjs || fail=1 ;;
    script:*) node "tests/jetski-${p#script:}.mjs" || fail=1 ;;
    gates) node tests/gate.mjs --gates-file jetski-gates.json --hosts romdev --gpu both jetski-race jetski-beach jetski-sound jetski-canyon jetski-harbour jetski-storm || fail=1 ;;
    *) echo "unknown part $p"; fail=1 ;;
  esac
done
exit $fail
