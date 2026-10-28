#!/bin/sh
# Fetch the original Tux Racer 0.61 source and data (GPL v2) into deps/tuxracer/,
# sha256-pinned. tools/convert.mjs turns them into assets/.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dst="$root/../../deps/tuxracer" # the repository's deps/ (gitignored)
mkdir -p "$dst"
fetch() { # name url sha256
  f="$dst/$1"
  if [ ! -f "$f" ]; then
    echo "fetching $1"
    curl -fLsS --retry 3 -o "$f.part" "$2"
    mv "$f.part" "$f"
  fi
  got=$(shasum -a 256 "$f" | cut -d' ' -f1)
  if [ "$got" != "$3" ]; then echo "$1: sha256 mismatch ($got)" >&2; rm -f "$f"; exit 1; fi
}
fetch tuxracer-0.61.tar.gz \
  "https://sourceforge.net/projects/tuxracer/files/tuxracer/0.61/tuxracer-0.61.tar.gz/download" \
  a311d09080598fe556134d4b9faed7dc0c2ed956ebb10d062e5d4df022f91eff
fetch tuxracer-data-0.61.tar.gz \
  "http://archive.debian.org/debian/pool/main/t/tuxracer-data/tuxracer-data_0.61.orig.tar.gz" \
  030578ea0c53aa71c277686ddb4dc1af8134ff6faca50524b730b9277bd72a0b
[ -d "$dst/tuxracer-0.61" ] || tar xzf "$dst/tuxracer-0.61.tar.gz" -C "$dst"
[ -d "$dst/tuxracer-data-0.61.orig" ] || tar xzf "$dst/tuxracer-data-0.61.tar.gz" -C "$dst"
echo "tuxracer 0.61 source and data in $dst"
