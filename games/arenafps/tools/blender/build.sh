#!/usr/bin/env bash
# usage: tools/blender/build.sh <script.py> [args...]  (headless flatpak Blender 5, absolute paths)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
GAME="$(cd "$HERE/../.." && pwd)"
script="$1"; shift
flatpak run --filesystem="$GAME" --command=blender org.blender.Blender -b --factory-startup --python "$HERE/$script" -- "$GAME/models" "$@"
