#!/bin/sh
# Regenerate Sky Fighter's three.c program tables (programs/*.txt and the C
# beside them) with the game's own foliage part (shaders/foliage.mjs). Append
# the states a run log lists as missing to programs/skyfighter.txt first and
# build build/native at the repository root.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
repo="$root/../.."
# the tool: the engine checkout's when it has its own dependencies (three.c), else the host SDK's copy
engine="${CARTWHEEL_ENGINE:-$repo/deps/cartwheel-engine}"
if [ ! -d "$engine/deps/three.c" ]; then
  case "$(uname -s)-$(uname -m)" in Linux-x86_64) host=linux-x64 ;; Linux-aarch64) host=linux-arm64 ;; Darwin-arm64) host=macos-arm64 ;; esac
  engine="$repo/deps/cartwheel-sdk/$host"
fi
cd "$engine" && node tools/programs.mjs --table "$root/programs/skyfighter.txt" --build "$repo/build/native" --extend "$root/shaders/foliage.mjs" ${PROGRAMS_FLAGS:-}
