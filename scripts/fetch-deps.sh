#!/usr/bin/env bash
# Fetch what the games build with, pinned in scripts/deps.lock:
#
#  - the prebuilt engine SDKs (cartwheel-engine's release assets, sha256
#    checked) into deps/cartwheel-sdk/<target>/, and the toolchains they pin
#    (each SDK's scripts/fetch-deps.sh);
#  - cartwheel-engine's source at the same commit into deps/cartwheel-engine,
#    for its test tools (tests/gate.mjs runs the engine's gate runner). Its own
#    dependencies are not fetched unless the engine builds from source.
#
#   scripts/fetch-deps.sh                       this machine's SDK and the cart SDKs
#   CW_SDK_TARGETS="windows-x64 android-arm64" scripts/fetch-deps.sh   (plus these)
#   CW_TOOLCHAINS=1 scripts/fetch-deps.sh       also each native target's toolchain
#                                               (llvm-mingw, sysroots) into the host SDK
#   CARTWHEEL_ENGINE_SOURCE=1 scripts/fetch-deps.sh
#                                               the engine from source instead (its own
#                                               scripts/fetch-deps.sh: three.c, Box2D, wasi-sdk, ...)
#   CARTWHEEL_ENGINE=../cartwheel-engine scripts/fetch-deps.sh
#                                               an engine checkout, from source (working on
#                                               the engine and the games together)
#
# Other variables (WASI_SDK, NATIVE_DAWN_PREFIX, FORCE) pass through to the
# engine's script and the SDKs'.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS="$ROOT/deps"
DST="$DEPS/cartwheel-engine"
mkdir -p "$DEPS"
read -r _ URL REV < <(grep -E '^cartwheel-engine[[:space:]]' "$ROOT/scripts/deps.lock")

# ── the engine's source ─────────────────────────────────────────────
if [ -n "${CARTWHEEL_ENGINE:-}" ]; then
  src="$(cd "$CARTWHEEL_ENGINE" && pwd)"
  rm -rf "$DST" && ln -sfn "$src" "$DST"
  echo "cartwheel-engine: $src (CARTWHEEL_ENGINE, built from source)"
  "$DST/scripts/fetch-deps.sh"
  exit 0
fi
[ -L "$DST" ] && rm -f "$DST"
if [ ! -d "$DST/.git" ]; then
  rm -rf "$DST" && git init -q "$DST"
fi
if [ "$(git -C "$DST" rev-parse -q --verify HEAD 2>/dev/null)" != "$REV" ]; then
  git -C "$DST" fetch -q --depth 1 "$URL" "$REV"
  git -C "$DST" -c advice.detachedHead=false checkout -q "$REV"
fi
echo "cartwheel-engine: $REV"
if [ -n "${CARTWHEEL_ENGINE_SOURCE:-}" ]; then
  "$DST/scripts/fetch-deps.sh"
  exit 0
fi

# ── the SDKs ────────────────────────────────────────────────────────
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) HOST=linux-x64 ;;
  Linux-aarch64) HOST=linux-arm64 ;;
  Darwin-arm64) HOST=macos-arm64 ;;
  *) echo "no prebuilt SDK runs on $(uname -s) $(uname -m): CARTWHEEL_ENGINE_SOURCE=1 builds the engine" >&2; exit 1 ;;
esac
TARGETS="$HOST"
# the cart SDKs need wasi-sdk, whose pinned build runs on Linux x64
[ "$HOST" = linux-x64 ] && TARGETS="$TARGETS cart cart-wgpu"
for t in ${CW_SDK_TARGETS:-}; do [[ " $TARGETS " == *" $t "* ]] || TARGETS="$TARGETS $t"; done

sha_check() { if command -v sha256sum >/dev/null; then sha256sum -c --quiet -; else shasum -a 256 -c --quiet -; fi; }
fetch_sdk() { # target
  local t=$1 line url ver sha dst="$DEPS/cartwheel-sdk/$1"
  line="$(grep -E "^cartwheel-sdk:$t[[:space:]]" "$ROOT/scripts/deps.lock")" || { echo "scripts/deps.lock pins no cartwheel-sdk:$t" >&2; exit 1; }
  read -r _ url ver sha <<< "$line"
  if [ -f "$dst/.version" ] && [ "$(cat "$dst/.version")" = "$ver $sha" ] && [ "${FORCE:-0}" != 1 ]; then
    echo "cartwheel-sdk $t: $ver"; return
  fi
  local tgz="$DEPS/.dl/$(basename "$url")"
  mkdir -p "$DEPS/.dl"
  if [ ! -f "$tgz" ]; then
    curl -fsSL "$url" -o "$tgz.part" || { echo "cartwheel-sdk $t: download failed: $url" >&2; exit 1; }
    mv -f "$tgz.part" "$tgz"
  fi
  echo "$sha  $tgz" | sha_check || { echo "cartwheel-sdk $t: sha256 mismatch ($tgz)" >&2; rm -f "$tgz"; exit 1; }
  rm -rf "$dst" && mkdir -p "$dst"
  tar -xzf "$tgz" -C "$dst" --strip-components=1
  echo "$ver $sha" > "$dst/.version"
  echo "cartwheel-sdk $t: $ver"
}
for t in $TARGETS; do fetch_sdk "$t"; done

# the toolchains the SDKs pin: wasi-sdk and binaryen inside each cart SDK
# (its cmake/cart.cmake looks there); with CW_TOOLCHAINS, each native
# target's toolchain inside the host SDK (the CW_EXPORT_DEPS of its builds)
# (fetched once; the other cart SDK links the same)
first=""
for t in $TARGETS; do
  case "$t" in
    cart|cart-wgpu)
      if [ -z "$first" ]; then
        "$DEPS/cartwheel-sdk/$t/scripts/fetch-deps.sh" --only wasi-sdk,binaryen
        first=$t
      else
        for d in wasi-sdk binaryen; do rm -rf "$DEPS/cartwheel-sdk/$t/deps/$d"; ln -sfn "../../$first/deps/$d" "$DEPS/cartwheel-sdk/$t/deps/$d"; done
      fi ;;
  esac
done
if [ -n "${CW_TOOLCHAINS:-}" ]; then
  want=""
  for t in $TARGETS; do
    case "$t" in
      linux-x64|linux-arm64) want="$want,export/llvm-mingw,export/sysroot-$t,export/appimage-runtime-${t#linux-}" ;;
      windows-x64|android-arm64) want="$want,export/llvm-mingw" ;;
    esac
  done
  if [ -n "$want" ]; then "$DEPS/cartwheel-sdk/$HOST/scripts/fetch-deps.sh" --only "${want#,}"; fi
fi
