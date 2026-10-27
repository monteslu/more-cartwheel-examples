#!/usr/bin/env bash
# Build the games. Against the prebuilt engine SDKs (scripts/fetch-deps.sh
# fetches them) only the games compile; with the engine from source
# (CARTWHEEL_ENGINE_SOURCE=1 or CARTWHEEL_ENGINE=<checkout> when fetching) the
# engine's CMakeLists.txt is the top of the build and this repository its
# project (CW_PROJECT_DIR).
#
#   scripts/build.sh                        native (this machine), cart and cart-wgpu trees
#   TARGETS="native" scripts/build.sh       some trees: native, cart, cart-wgpu, or (SDK
#                                           builds) an export target: linux-x64, linux-arm64,
#                                           windows-x64, android-arm64, macos-arm64
#   BUILD_TYPE=Debug scripts/build.sh
#   RELEASE=1 scripts/build.sh              carts as releases ship them (ThinLTO and
#                                           wasm-opt); by default carts link as dev
#                                           carts, in seconds, with the same simulation
#   scripts/build.sh bikemesa derby_auto    only these apps (ninja targets)
#
# Trees: build/native (this machine's compiler; the gates run it), build/cart,
# build/cart-wgpu, build/<target> (the target's toolchain: what the release
# packages ship). The cart trees and cross builds pack assets with the host's
# cwpack. CW_CART_LINK_JOBS (default 4) caps the threads one cart link
# (ThinLTO and wasm-opt) uses; at most two carts link at once.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="$ROOT/deps/cartwheel-engine"
SDKS="$ROOT/deps/cartwheel-sdk"
BT="${BUILD_TYPE:-Release}"
# a tree configured from another source (the engine's, or this repository's
# against the SDKs) cannot be reconfigured from the other: start it over
fresh() { # <tree> <source dir> [the same, spelled another way]
  local c="$1/CMakeCache.txt" d
  [ -f "$c" ] || return 0
  for d in "${@:2}"; do grep -qx "CMAKE_HOME_DIRECTORY:INTERNAL=$d" "$c" && return 0; done
  rm -rf "$1"
}
JOBS=()
[ -n "${CW_CART_LINK_JOBS:-}" ] && JOBS=(-DCW_CART_LINK_JOBS="$CW_CART_LINK_JOBS")

if [ -z "${CARTWHEEL_ENGINE_SOURCE:-}" ] && [ ! -L "$ENGINE" ] && [ -d "$SDKS" ]; then
  # ── against the SDKs ──
  case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) HOST=linux-x64 ;;
    Linux-aarch64) HOST=linux-arm64 ;;
    Darwin-arm64) HOST=macos-arm64 ;;
  esac
  HSDK="$SDKS/$HOST"
  [ -f "$HSDK/lib/cmake/cartwheel/cartwheelConfig.cmake" ] || { echo "no $HOST SDK in deps/cartwheel-sdk: run scripts/fetch-deps.sh" >&2; exit 1; }
  CWPACK="$HSDK/bin/cwpack"
  COMMON=(-G Ninja -DCMAKE_BUILD_TYPE="$BT")
  # carts: dev carts (no ThinLTO, no wasm-opt: seconds to link, the same
  # simulation) unless RELEASE=1, which links them as releases ship
  DEV=ON
  [ -n "${RELEASE:-}" ] && DEV=OFF
  DEFAULT="native"
  [ -d "$SDKS/cart" ] && DEFAULT="$DEFAULT cart"
  [ -d "$SDKS/cart-wgpu" ] && DEFAULT="$DEFAULT cart-wgpu"
  for t in ${TARGETS:-$DEFAULT}; do
    case "$t" in
      native) sdk=$HOST; args=() ;;
      cart|cart-wgpu) sdk=$t; args=(-DCMAKE_TOOLCHAIN_FILE="$SDKS/$t/cmake/cart.cmake" ${JOBS[@]+"${JOBS[@]}"} -DCW_CART_DEV="$DEV") ;;
      linux-x64|linux-arm64|windows-x64|android-arm64|macos-arm64)
        sdk=$t; args=(-DCMAKE_TOOLCHAIN_FILE="$SDKS/$t/cmake/export/$t.cmake" -DCW_EXPORT_DEPS="$HSDK/deps/export") ;;
      *) echo "unknown target tree: $t (native, cart, cart-wgpu, linux-x64, linux-arm64, windows-x64, android-arm64, macos-arm64)" >&2; exit 2 ;;
    esac
    [ -f "$SDKS/$sdk/lib/cmake/cartwheel/cartwheelConfig.cmake" ] || { echo "no $sdk SDK in deps/cartwheel-sdk: CW_SDK_TARGETS=$sdk scripts/fetch-deps.sh" >&2; exit 1; }
    fresh "$ROOT/build/$t" "$ROOT"
    cmake -S "$ROOT" -B "$ROOT/build/$t" "${COMMON[@]}" ${args[@]+"${args[@]}"} \
      -Dcartwheel_DIR="$SDKS/$sdk/lib/cmake/cartwheel" -DCW_CWPACK="$CWPACK" >/dev/null
    ninja -C "$ROOT/build/$t" "$@"
  done
  exit 0
fi

# ── the engine from source ──
[ -f "$ENGINE/CMakeLists.txt" ] || { echo "no cartwheel-engine in deps/: run scripts/fetch-deps.sh" >&2; exit 1; }
COMMON=(-G Ninja -DCMAKE_BUILD_TYPE="$BT" -DCW_ENGINE_ONLY=ON -DCW_PROJECT_DIR="$ROOT")
DEV=ON
[ -n "${RELEASE:-}" ] && DEV=OFF
CWPACK="$ROOT/build/native/tools/cwpack/cwpack"
for t in ${TARGETS:-native cart cart-wgpu}; do
  if [ "$t" != native ] && [ ! -x "$CWPACK" ]; then
    echo "build/native has no cwpack yet: build native first (TARGETS=\"native $t\")" >&2; exit 1
  fi
  fresh "$ROOT/build/$t" "$ENGINE" "$(cd "$ENGINE" && pwd -P)"
  case "$t" in
    native) cmake -S "$ENGINE" -B "$ROOT/build/native" "${COMMON[@]}" >/dev/null ;;
    cart) cmake -S "$ENGINE" -B "$ROOT/build/cart" "${COMMON[@]}" ${JOBS[@]+"${JOBS[@]}"} -DCW_CART_DEV="$DEV" \
            -DCMAKE_TOOLCHAIN_FILE="$ENGINE/cmake/cart.cmake" -DCW_CWPACK="$CWPACK" >/dev/null ;;
    cart-wgpu) cmake -S "$ENGINE" -B "$ROOT/build/cart-wgpu" "${COMMON[@]}" ${JOBS[@]+"${JOBS[@]}"} -DCW_CART_DEV="$DEV" \
                 -DCMAKE_TOOLCHAIN_FILE="$ENGINE/cmake/cart.cmake" -DCW_WGPU=ON -DCW_CWPACK="$CWPACK" >/dev/null ;;
    *) echo "unknown target tree: $t (native, cart, cart-wgpu; export targets build against the SDKs)" >&2; exit 2 ;;
  esac
  ninja -C "$ROOT/build/$t" "$@"
done
