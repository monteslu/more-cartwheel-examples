#!/bin/sh
# Regenerate Tux Racer's three.c program tables (programs/tuxracer_gl.c, _wgpu.c)
# from programs/tuxracer.txt plus the game's own shader parts (shaders/*.mjs,
# three.js TSL: the snow, foliage, flag, clouds, aurora and feather parts).
# Append the states a run log lists as missing to the manifest first, build
# build/native at the repository root, then run this. The WGSL check needs
# lavapipe (Linux); on a Mac the tables are written and the check reports it
# cannot run (the dual cart on a WebGPU host is the check there).
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
repo="$root/../.."
# the tool: the engine checkout's when it has its own dependencies (three.c), else the host SDK's copy
engine="${CARTWHEEL_ENGINE:-$repo/deps/cartwheel-engine}"
if [ ! -d "$engine/deps/three.c" ]; then
  case "$(uname -s)-$(uname -m)" in Linux-x86_64) host=linux-x64 ;; Linux-aarch64) host=linux-arm64 ;; Darwin-arm64) host=macos-arm64 ;; esac
  engine="$repo/deps/cartwheel-sdk/$host"
fi
set -- --table "$root/programs/tuxracer.txt" --build "$repo/build/native"
for m in "$root"/shaders/*.mjs; do set -- "$@" --extend "$m"; done
cd "$engine" && node tools/programs.mjs "$@" ${PROGRAMS_FLAGS:-}
