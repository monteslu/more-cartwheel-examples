# more-cartwheel-examples

[![CI](https://github.com/monteslu/more-cartwheel-examples/actions/workflows/ci.yml/badge.svg)](https://github.com/monteslu/more-cartwheel-examples/actions/workflows/ci.yml)

Six more games built with
[cartwheel-engine](https://github.com/monteslu/cartwheel-engine), a 2D and 3D
game engine in C, after the ones in
[cartwheel-examples](https://github.com/monteslu/cartwheel-examples). Each
game is written to push the engine: to find what it lacks (split screen for
four, crowds of thousands, skinned fighters, ragdolls, a party game's many
small scenes) so the generic fixes go back into the engine. Every game is for
one to four players on one machine, one pad each.

Each game builds as a native executable and as a
[wasmcart](https://github.com/wasmcart/wasmcart) cart (`.wasc`) that runs in
a browser, in Node, in a native player and in RetroArch.

Everything in the games is made by code in this repository (models,
textures, levels, sound and music) except a few fonts, which ship with their
licence (SIL Open Font License). Each game's `CREDITS` file says what is
what.

## The games

### arenafps

![arenafps: four-way split screen deathmatch in the Foundry](docs/screenshots/arenafps.jpg)

A first-person arena shooter for one to four players in split screen, with
bots that find their way over a navigation mesh, take the jump pads and drop
from the roofs. Three two-level arenas (Foundry, Courtyard, Terraces), each
with its own light design, and three modes: deathmatch, team deathmatch and
co-op waves against the bots. Four weapons: a pulse rifle, a scatter gun,
rockets and a rail gun. Move with the left stick, look with the right, fire
with the right trigger, jump with (A), change weapons with (Y) and (X).
`games/arenafps/` (its [README](games/arenafps/README.md) has the full
controls).

### brawler

![brawler: three players in an eleven-hit combo in the alley](docs/screenshots/brawler.jpg)

A side-scrolling street brawler for one to four players on one shared
screen. Four stages (an alley, the docks, an arcade and a roof), a crowd that
surrounds you, crates and barrels with food and weapons, and a three-phase
boss at the end of each stage. Knocked-down bodies fall loose as ragdolls.
Attack with (X), kick with (Y), jump with (A), grab with the right bumper,
spin with the left. `games/brawler/` (its [README](games/brawler/README.md)
has the full controls).

### coopplatformer

![coopplatformer: two players jumping through the meadow](docs/screenshots/coopplatformer.jpg)

A co-op side-scrolling platformer for one to four players on one shared
screen. The camera pulls back to keep everyone in view, and anyone left behind
is carried to the group in a bubble. Four regions left to right (a meadow,
torchlit caves, a flooded ruin and a volcano with a boss), three abilities
that open the way (an air jump, wall claws and a dash), secrets and a map
screen. Jump with (A), dash with (X), the map on Back.
`games/coopplatformer/` (its [README](games/coopplatformer/README.md) has the
full controls).

### fighter

![fighter: Vex lands a kick on Bolt on the rooftop stage](docs/screenshots/fighter.jpg)

A one-on-one fighting game: 2D rules, 3D look. Two players, or one against
the computer. Four fighters, three stages, special moves on stick motions,
parries, throws, and a training mode that shows hitboxes and frame data.
Punch with (X), kick with (A), block with (B); the stick picks a high, mid or
low attack. `games/fighter/` (its [README](games/fighter/README.md) has the
full controls and the move notation).

### hordeshooter

![hordeshooter: the horde pouring into the yard at dusk](docs/screenshots/hordeshooter.jpg)

One to four players hold a walled yard against a horde that never stops
coming, on one shared camera that zooms out to keep everyone in view. Gems
level you up into upgrades, power-ups lie around the yard (shotgun, rail
gun, flamer, buzzsaws, shields...), a boss comes every fifth wave, and the
yard is held after the boss of wave 20. Move with the left stick, aim and
fire with the right, throw a blast with the left shoulder.
`games/hordeshooter/` (its [README](games/hordeshooter/README.md) has the full
controls and the power-up list).

### partypack

![partypack: four CPUs racing Hurdle Dash](docs/screenshots/partypack.jpg)

A party game for one to four players on one shared screen; CPU players fill
the empty seats. A board tournament of four, six or eight rounds over eleven
mini-games (2D, 2D physics, 3D racing and 3D water: Hurdle Dash, Echo, Stack
Attack, Blast Ring, Beat Box, Loop Racers, Quick Quiz, Raft Rush, Sumo Push,
Coin Rush, Tile Drop), free play and a trophy room. Each mini-game's card
names the buttons it uses. `games/partypack/` (its
[README](games/partypack/README.md) has more).

## Play

Tagged versions are on the
[Releases](https://github.com/monteslu/more-cartwheel-examples/releases)
page: the six carts and each game's native builds.

| File | Platform | Notes |
|---|---|---|
| `<game>.wasc` | any wasmcart host | GL and WebGPU in one cart: WebGPU where the host has it, GL otherwise |
| `<game>-<v>-linux-x64.tar.gz`, `.AppImage` | Linux x64 | needs glibc 2.34 or newer |
| `<game>-<v>-linux-arm64.tar.gz`, `.AppImage` | Linux arm64 | the same |
| `<game>-<v>-windows-x64.zip` | Windows 10 or later, x64 | unzip and run `<game>.exe` |
| `<game>-<v>-macos-arm64.zip` | macOS 11 or later, Apple silicon | signed ad hoc, not notarized: the first time, right-click the app and choose Open |
| `<game>-<v>-android-arm64-debug.apk` | Android 7 or later, arm64 | signed with a debug key, for sideloading |

The native builds render with WebGPU (Dawn) and fall back to OpenGL ES 3.0
when WebGPU does not start; `CW_GPU=gl` or `CW_GPU=webgpu` in the environment
picks one. Every push also builds everything: the repository's
[Actions](https://github.com/monteslu/more-cartwheel-examples/actions) tab,
the latest green run on `main`, then **Artifacts** (`release-carts`,
`release-<platform>`).

Hosts that run the carts:

- [wasmcart](https://github.com/wasmcart/wasmcart), the web and Node player:
  `npx wasmcart partypack.wasc`
- [wasmcart-native](https://github.com/wasmcart/wasmcart-native), a native
  player
- [wasmcart-libretro](https://github.com/wasmcart/wasmcart-libretro), a
  libretro core for RetroArch (it runs a cart's GL path)

## Build

The games build against cartwheel-engine's prebuilt SDKs: the engine,
three.c, Box2D, Box3D, SDL2, Dawn and the rest come compiled (one SDK per
target, attached to the engine's release; see the engine's
[docs/sdk.md](https://github.com/monteslu/cartwheel-engine/blob/main/docs/sdk.md)),
so a build compiles only the games. You need cmake, ninja, a C17 compiler,
Node.js, git and curl; on Linux also the EGL and GLESv2 runtime (Mesa). On
Ubuntu:

```sh
sudo apt-get install cmake ninja-build clang nodejs npm curl libegl1 libgles2
```

Then:

```sh
scripts/fetch-deps.sh   # the SDKs for this machine and for carts, pinned with sha256 in scripts/deps.lock
scripts/build.sh        # build/native and build/cart-wgpu
```

`scripts/build.sh` builds these trees:

| Tree | What | Where |
|---|---|---|
| `build/native` | native executables, this machine's compiler | `build/native/games/<game>/<app>` |
| `build/cart-wgpu` | dual GL + WebGPU carts (the only cart: GL is tested by running it on a GL host) | `build/cart-wgpu/carts/<app>.wasc` |
| `build/<target>` | a platform's release build, with the target's SDK and toolchain: `linux-x64`, `linux-arm64`, `windows-x64` (cross-compiled), `android-arm64`, `macos-arm64` (on a Mac) | `build/<target>/games/<game>/<app>` |

`TARGETS="native"` (or any of the trees) builds some trees. Carts link as dev
carts by default (no ThinLTO, no wasm-opt: seconds instead of a minute, the
same simulation); `RELEASE=1` links them as releases ship.
`BUILD_TYPE=Debug` makes a debug build, and any arguments go to ninja:
`scripts/build.sh -j4 fighter` builds only fighter in each tree. A platform
tree needs its SDK and toolchain:
`CW_SDK_TARGETS=windows-x64 CW_TOOLCHAINS=1 scripts/fetch-deps.sh`. Each
game's `project.cwproj` holds its export presets;
`node deps/cartwheel-sdk/<host>/tools/cwexport.mjs --project games/<game> --preset <target> --build build/<target>`
packages a built tree as the releases do.

To build the engine from source instead (to work on it, or on a machine no
SDK runs on), fetch with `CARTWHEEL_ENGINE_SOURCE=1 scripts/fetch-deps.sh`
(the engine at the pinned commit and its own dependencies) or
`CARTWHEEL_ENGINE=../cartwheel-engine scripts/fetch-deps.sh` (a checkout);
`scripts/build.sh` then builds the engine with the games, as the engine's
project.

## Run

Natively:

```sh
build/native/games/arenafps/arenafps
```

The carts with any wasmcart host:

```sh
npx wasmcart build/cart-wgpu/carts/arenafps.wasc
```

Each game also builds a `<game>_auto` app that plays by itself (bots or CPUs
in every seat), for the tests and screenshots.

## Tests

The gates are in `tests/gates.json` (arenafps, hordeshooter) and
`tests/<game>-gates.json` (the others). A gate runs an app on every host it
names with the same frame count and input script, and compares each run with
the native one: the frames within a pixel bound, and the game's named debug
state (scores, positions, hashes of the simulation) exactly. Each gate also
runs a control (one frame short, or a different input) that must fail, so a
gate that cannot tell runs apart fails too. Gates with a `webgpu` entry run
again on WebGPU and are compared with the GL run.

```sh
node tests/gate.mjs --hosts native                                   # tests/gates.json, native, GL
node tests/gate.mjs --gates-file fighter-gates.json --gpu webgpu     # fighter's gates on WebGPU, every host they name
```

`tests/gate.mjs` runs cartwheel-engine's gate runner with `CW_PROJECT_ROOT`
set to this repository. The engine's
[testing guide](https://github.com/monteslu/cartwheel-engine/blob/main/docs/guides/testing.md)
covers the hosts (native, Chromium, wasmcart-native, romdev), the test GPU
settings it expects in `~/.config/cartwheel/`, and the gate format. Without
GPU hardware, `CW_TEST_GPU=software` runs on Mesa's software renderers
(llvmpipe for GL, lavapipe for WebGPU), as CI does.

`tests/native-replay.mjs` runs each game's replay gate (a self-playing
`<game>_auto` session) on any platform's build without the gate runner, and
`tests/compare-replays.mjs` holds every platform's state to Linux x64 GL's.
`tests/arenafps-split-bench.mjs` measures what each extra split-screen view
costs, and `scripts/partypack-suite.sh` runs partypack's checks in pieces.

## CI

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs on every push to
`main`, on pull requests and by hand (workflow_dispatch). Every job builds
against the pinned SDKs.

- **Carts**: the six dual carts.
- **Gates** (nightly, and by hand): each game's replay gate in full, natively
  on GL and then on WebGPU, with Mesa's software renderers at the games' own
  resolution (`CW_RENDER_SCALE=1`). The full gate set runs locally with
  `tests/gate.mjs`.
- **Native** (one job per platform): the games built with the target's SDK
  and toolchain and packaged with cwexport. Where the runner can run them
  (Linux x64 and arm64 on Mesa's software renderers, macOS on the runner's
  Metal device), the first 200 frames of each game's replay run on GL and on
  WebGPU (macOS runners draw GL at seconds a frame, so there GL is
  launch-checked on one game). Windows builds cross-compile on Linux and run
  on a Windows runner (ANGLE on Direct3D 11, Dawn on Direct3D 12; one game's
  slice), which also adds Microsoft's Visual C++ runtime, which Dawn links,
  beside each game. The Android APKs are launched on an emulator.
- **Replays across platforms**: every platform's and API's state after those
  frames must equal Linux x64 GL's, field for field.

Pushing a `v*` tag runs the same jobs and publishes a GitHub release with the
six carts and each game's native builds, its notes taken from
[CHANGELOG.md](CHANGELOG.md): `git tag v0.1.0 && git push origin v0.1.0`.

## Layout

| Path | Contents |
|---|---|
| `games/<game>/` | one game: its C sources, `CMakeLists.txt`, `CREDITS`, `README.md`, `project.cwproj` (export presets), its assets and `tools/` |
| `games/<game>/tools/` | the scripts that generate the game's assets (models, textures, levels, colour grades, sound), committed next to their output |
| `games/<game>/programs/` | the game's three.c program table: `<game>.txt` and the C generated from it |
| `tests/` | the gates, their input scripts, the gate and replay runners, benchmarks |
| `scripts/deps.lock` | the pinned cartwheel-engine commit and its SDKs (sha256) |
| `scripts/fetch-deps.sh`, `scripts/build.sh` | fetch the SDKs (or the engine) and build |
| `CMakeLists.txt` | the top of the build against an SDK (`find_package(cartwheel)`), adding each game; with the engine from source, the engine's project |
| `deps/`, `build/`, `out/` | fetched dependencies, build trees, test output (all gitignored) |

## Working on the games

- Each game is a cartwheel app: `cw_main` returns a `cw_app_desc` with a
  fixed-tick update and a render callback. The engine's headers
  (`deps/cartwheel-sdk/<host>/include/cartwheel/*.h`) are its API reference.
- The simulation is deterministic on every host: fixed ticks, randomness from
  `cw_rng`, trig from `cartwheel/math.h`, no fast-math. State a gate checks
  is published by name with `cw_debug_*`.
- Generated assets come from the game's `tools/` scripts: change the script,
  rerun it and commit both.
- When a game draws a new material state, the gates fail and name it. Add it
  to the game's program table with the engine's tool,
  `node deps/cartwheel-sdk/<host>/tools/programs.mjs <app> --build build/native`,
  and commit the generated files.
- To work on the engine and the games together, link an engine checkout:
  `CARTWHEEL_ENGINE=../cartwheel-engine scripts/fetch-deps.sh`. Engine
  changes go into cartwheel-engine first; then pin its new release in
  `scripts/deps.lock`.
- A change is done when its gates pass natively on GL and WebGPU.

## License

MIT, see [LICENSE](LICENSE). The fonts keep their own licence (SIL Open Font
License 1.1), shipped beside each font.
