# Changelog

## 0.4.0

All eight games on cartwheel-engine v0.23.1.

- hordeshooter: about three times faster on the GPU at full 1080p (late wave,
  2000 enemies: 12.2 to about 3.7 ms on GL and 11.1 to about 3.7 ms on
  WebGPU on a Radeon 890M), redrawn the way weak-GPU games do it: the yard and
  its floodlights baked into a lightmap with soft shadows, light probes for
  the crowd, blob shadows and light pools, puddles on a baked reflection,
  light-shaft cards instead of volumetric fog, one particle system per pool
  of one-shot effects. Never a lower render resolution. No more camera
  shake: explosions, hits and heavy weapons rumble each player's own pad.
  The crowd is a neutral grey instead of mauve.
- brawler: boss fights about a minute solo (they took two to three, and some
  never ended), every enemy his own skin tone and clothes, two new enemies
  (the redneck in his red "Dumb" cap, the creep in the trench coat who enjoys
  being hit), blood and gore, faces that show fear, anger or delight when
  hit, Arthur's pompadour rebuilt. No camera shake: big hits rumble the pad.
- fighter: held directions walk (diagonals too), one press of up jumps, up
  with an attack gives the high attack, down crouches; d-pad, stick and
  keyboard each tested.
- No camera shake in any game: arenafps, coopplatformer, fighter, partypack,
  skyfighter and tuxracer rumble the player's pad on explosions, big hits and
  landings instead, with a Rumble On/Off setting where a game has settings.

## 0.3.0

All eight games on cartwheel-engine v0.21.1.

- Touch controls: a game's on-screen stick keeps a fixed home in the bottom
  left and steers from there instead of jumping to wherever the thumb lands,
  and it sizes with the screen (engine v0.21.0). No game needed a change.
- coopplatformer: its pools of water draw on every WebGPU device, including
  ones in WebGPU's compatibility mode (engine v0.21.1: a Water3D built while
  the camera was under its surface skipped its draws there).
- partypack: Raft Rush's canyon corners are closed; water no longer shows
  through the back walls.
- CI: a `v*` tag publishes the carts that the tagged commit's run on `main`
  already built, instead of building them again.

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
