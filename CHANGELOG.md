# Changelog

## 0.2.0

Two more games, from their own repositories, each one dual GL + WebGPU cart
like the rest (eight carts in all):

- skyfighter (Sky Fighter): an arcade jet combat game for one player, eight
  missions each over its own terrain (farmland balloons, an alpine valley, a
  desert canyon, an island, a city under a mothership, a snowbound fortress,
  the North Pole, oil rigs at dusk), guns, guided missiles, flares, an
  afterburner, crashes into Box3D pieces.
- tuxracer (Tux Racer): a work-in-progress reimplementation of the classic
  Tux Racer 0.61 on cartwheel-engine, the original courses, penguin, music and
  sounds converted from the GPL data, the sliding on Box3D, the look rebuilt
  on the engine's terrain, sky, shadows, particles and post effects.
  GPL-2.0-or-later (its own LICENSE and CREDITS).

CI builds and smoke-tests all eight carts; a game without a self-playing
`<game>_auto` cart smoke-tests its own cart.

## 0.1.0

The first release: six games built on cartwheel-engine v0.19.1, each as one
dual GL + WebGPU cart (`<game>.wasc`) that runs in any wasmcart host (the web
and Node player, wasmcart-native, RetroArch through wasmcart-libretro).

- arenafps: a first-person arena shooter for one to four players in split
  screen, with bots on a navigation mesh, three arenas and deathmatch, team
  deathmatch and co-op waves.
- brawler: a side-scrolling street brawler for one to four players on one
  shared screen, four stages with a boss each, ragdoll knockdowns.
- coopplatformer: a co-op platformer for one to four players on one shared
  screen through four regions to a volcano boss, with abilities and a map.
- fighter: a one-on-one fighting game with 2D rules and a 3D look, four
  fighters, three stages and a training mode with frame data.
- hordeshooter: a twin-stick shooter for one to four players holding a walled
  yard against an endless horde for twenty waves, with upgrades and power-ups.
- partypack: a party game for one to four players (CPUs fill empty seats):
  a board tournament over eleven 2D and 3D mini-games.
