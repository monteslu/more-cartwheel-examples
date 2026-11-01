#!/bin/sh
# A photo material from Poly Haven (CC0) as a terrain layer's raw pixels: tools/pack-scan.sh <dir with diff.jpg disp.jpg nor_gl.jpg rough.jpg> <name>
# Writes art/scans/<name>-albedo.rgba (colour, height in alpha) and <name>-normal.rgba (normal, roughness in alpha), 1024 square. Roughness is lifted to 0.8..1: dry rock is matte.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out="$here/../art/scans"; mkdir -p "$out"
s=$1; n=$2
convert "$s/diff.jpg" -resize 1024x1024! -colorspace sRGB \( "$s/disp.jpg" -resize 1024x1024! -colorspace gray -auto-level \) -alpha off -compose CopyOpacity -composite -depth 8 "rgba:$out/$n-albedo.rgba"
convert "$s/nor_gl.jpg" -resize 1024x1024! \( "$s/rough.jpg" -resize 1024x1024! -colorspace gray -fx '0.8+0.2*u' \) -alpha off -compose CopyOpacity -composite -depth 8 "rgba:$out/$n-normal.rgba"
