#!/usr/bin/env bash
# fetch-anims.sh: the two motion libraries the fighters' clips come from (Quaternius, "Universal Animation Library" and
# "Universal Animation Library 2", the free Standard versions, CC0 1.0), downloaded from OpenGameArt and checked, into
# tools/blender/out/ual/; then tools/blender/anims.py makes anim/ual.glb from them (every clip the game plays, on the
# rig the soldier is built on) and tools/blender/soldier.py builds the soldiers on that rig. Needs flatpak Blender
# (org.blender.Blender, 5.x).
#   https://opengameart.org/content/universal-animation-library
#   https://opengameart.org/content/universal-animation-library-2
#   tools/fetch-anims.sh [sheet dir]   (a sheet dir: workbench renders of the poses, for checking by eye)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GAME="$(cd "$HERE/.." && pwd)"
D="$HERE/blender/out/ual"
mkdir -p "$D"
get() { # url sha256 dir
  local f="$D/$(basename "$1")"
  [ -f "$f" ] || curl -fsSL "$1" -o "$f"
  echo "$2  $f" | sha256sum -c --quiet
  rm -rf "${D:?}/$3"; mkdir -p "$D/$3"
  unzip -q -o "$f" -d "$D/$3"
}
get https://opengameart.org/sites/default/files/universal_animation_librarystandard.zip 18ff1a7215f4852b320203e8aaf02a1578b5c8eef9027fbaedfcedc7b85a3ac2 ual1
get https://opengameart.org/sites/default/files/universal_animation_library_2standard.zip ec0e40d6d78fe9aaad59e322f40865a8675c22f0745e291622e54520391a9217 ual2
FBX="$(find "$D/ual1" -path '*Unreal*' -name 'AL_Standard.fbx' | head -1)"
GLB="$(find "$D/ual2" -path '*Unreal-Godot*' -name 'UAL2_Standard.glb' | head -1)"
blender() { flatpak run --filesystem="$GAME" --command=blender org.blender.Blender -b --factory-startup --python "$@"; }
blender "$HERE/blender/anims.py" -- "$FBX" "$GLB" "$HERE/blender/out/anims.glb" "$HERE/blender/out/hold.json" ${1:+"$1"} 2>&1 \
  | grep -E '^\[anims\]|Error|Traceback|line [0-9]+' || true
mkdir -p "$GAME/anim"
cp "$HERE/blender/out/anims.glb" "$GAME/anim/ual.glb"
blender "$HERE/blender/soldier.py" -- "$GAME/models" "$FBX" "$HERE/blender/out/hold.json" ${1:+"$1"} 2>&1 \
  | grep -E '^\[soldier\]|^wrote|Error|Traceback|line [0-9]+' || true
echo "anim/ual.glb models/soldier_a.glb models/soldier_b.glb"
