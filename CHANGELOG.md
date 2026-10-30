# Changelog

## 0.5.0

hordeshooter is an eight-floor descent.

- Eight floors, each its own place with its own baked light: the yard, the
  drowned sluice, the ossuary, the foundry, the grotto, the cathedral, the
  rift and the core, darker and stranger the deeper you go.
- A new creature on each floor (skitters that spring and swim, bonewalkers
  that rise again from their bone piles, slagborn that lob burning slag,
  sporelings that burst into slowing spores, penitents, gargoyles,
  hellhounds) and floor hazards: water slows, molten strips burn, spore beds
  slow.
- A named boss on each floor with telegraphed attacks: Vhessa, Ossivar,
  Anvilgrave, Mother Mycel, Seraphel, Khar-Vaal and the Unmaker.
- Loot: a fallen boss drops three to five caches, each a gun from the
  floor's pool at a rarity shown by its glow; rarer guns hit harder, pierce
  further and last longer.
- Every floor's late wave draws in under 3.1 ms of GPU time on a Radeon 890M
  at 1080p, on GL and on WebGPU.

## 0.4.2

- brawler: a fighter hit in mid jump no longer floats: a staggered body in
  the air keeps falling, and anyone left above the ground falls. The player's
  controls are as they were in 0.3.0; a new gate drives walking, jumps, jump
  kicks, combos and grabs with human-like input.
- brawler: faces that read from the game camera (a big man flinches with a
  sweat drop, a small one snarls, the creep beams with hearts), bright red
  gore on the street and the fighters, a softer hit flash, the wanker out of
  shape (thin arms, a belly over his belt, the red cap always), lighter skin
  tones for him.
- hordeshooter: the descent begins. The run is eight floors of five or six
  waves, each ending in a boss; when the boss falls the rest of the horde
  burns to ash and a hatch opens in the centre, and a player stepping in
  takes the team down to the next floor with their levels and score. The
  deeper floors' own maps, monsters and bosses follow in the next release.

## 0.4.1

- hordeshooter: the creatures attack. An enemy next to a player winds up,
  strikes and recovers (an attack animation per kind: runner claws and
  lunges, brute smash, bloater slam, boss sweep), facing its target; damage
  lands on the strike frame and only if the player is still in reach, so a
  step away dodges it. Slash streaks, blood and pad rumble on a hit. No
  measurable GPU cost.
- brawler: Rook in a plain black tee instead of the leather jacket, so he no
  longer reads like Arthur. The trucker is now the wanker.

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
  (the wanker in his red "Dumb" cap, the creep in the trench coat who enjoys
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
