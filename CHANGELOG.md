# Changelog

## 0.12.6

- jetski: each course now has 12 ramps, with a cluster of three side by side where the
  course opens up and two shortcut ramps well off the racing line, aimed at the correct
  side of the next buoy.
- jetski: computer riders miss fewer buoys around the ramps on Cold Sound.

## 0.12.5

- Street Toughs: all 18 characters have human heads and human proportions, about 7.5 heads
  tall with real jaws, chins and short thick necks, in place of round heads on oversized
  bodies. Bodies came in to match: shoulders, waists, hips, knees and calves, hands and
  feet. Faces have sunk eyes, hairlines that grow out of the skin and less rubbery skin.

## 0.12.4

- jetski: the ramps look like steel that has been out there a while: panel seams, rust
  runs, tyre scuffs, hazard stripes worn at the lip, and heavy side girders that show on
  approach. The hard white foam edge over their low end is gone.
- jetski: on the atoll a fill light from opposite the low sun keeps backlit decks, riders
  and land from going black.
- jetski: Low and Medium graphics no longer skip draws or show the red missing shader
  banner.

## 0.12.3

- jetski: the tan bands across Cold Sound's water are gone: they were the sky's sandy
  default ground colour reflected on the backs of waves.
- jetski: Cold Sound's arch is broken granite with moss and a tide stain, not a smooth
  blob.

## 0.12.2

- jetski: the hard-edged light patches behind the rider are gone: they were the skyline
  mirrored on the water, and stronger ripples now break it up. The sea looks busier and
  glitters more on both courses.

## 0.12.1

- hordeshooter: Start answers on the panels before a match, and the wait after pressing
  play is gone: the floor builds under the loading screen, with a progress bar.

## 0.12.0

- jetski: new ramps, and more of them: 9 on the atoll and 10 on Cold Sound. They are
  rigid steel structures on piles, 18 m wide and 40 m long, with an orange lip. They
  don't move, so a jump is the same every time, up to about 9 m high. The swell is held
  down around each one.
- jetski: computer riders use the ramps too.
- jetski: a buoy pass now counts out to 48 m on the correct side, up from 34 m, so
  coming off a ramp doesn't lose you the buoy.

## 0.11.2

- Street Toughs: enemies and heroes no longer flash in the middle of the screen in
  their bind pose when they appear.
- Street Toughs: Start answers on the panels before a match, and the wait after
  pressing play is gone: the stage builds under the loading screen, with a progress bar.
- partypack: Start confirms on the hub, picker, boards, intro and results, and pauses
  in play. B on the hub goes to the title.
- arenafps, fighter, partypack: a pad unplugged on a panel frees its seat.

## 0.11.1

- All games now run on cartwheel-engine v0.28.5. Water reads its floor map and
  ripples where the surface is drawn again, so foam sits where it was made and reads
  whiter and crisper, in every game with water. v0.26.0 had changed this by accident.
- jetski: the rocks come back on both courses, kept off true cliffs so they don't hang.
- coopplatformer: the heroes on the join screen follow who has joined.

## 0.11.0

- All games now run on cartwheel-engine v0.28.4. Shaders compile during the warm-up
  for things the camera can't see yet, so they should no longer compile mid-game.
- jetski: a second course, Cold Sound: a grey morning in a granite fjord, with its
  own light, pines and longer swell. The tide falls through the race. At high tide a
  drowned arch shuts the cut through the headland; at low tide it's the short way home.
- jetski: floating ramps no longer hover above big swells. They fit the water at all
  four corners and sit deeper in it, on both courses.
- jetski: boulders no longer hang in the air off cliffs and sea stacks. The atoll has
  fewer rocks around its shores as a result.
- coopplatformer: four kinds of draw that were missing their shaders, so they did not
  draw when the camera reached them, now have them.
- arenafps: Start begins or continues on the setup and results panels instead of
  opening the pause menu, and the title and join screens now click.
- fighter: Start locks in on character select and means rematch on the result screen.

## 0.10.0

- jetski: four floating ramps on the racing line that ride the swell, for big air:
  about 1.8 seconds airborne in the test, against 0.8 beside the ramp.
- jetski: tricks are one press in the air. Computer racers don't aim for ramps yet,
  and the ramps are a plain first pass.

## 0.9.0

- arenafps: a new default difficulty for someone new. The levels are now EASY,
  NORMAL, VETERAN (the old NORMAL) and HARD. Bots notice you inside a view cone,
  pad aim assist is stronger, and you spawn facing open space, not a wall. A match
  is first to 25 frags, or 8 minutes.
- arenafps: recoil no longer moves the camera: the rifle's burst walks its shots
  and the gun model kicks. Separate sounds for a hit, a head shot and a frag, a head
  shot marker, streaks, who downed you, and the controls shown for the first 9 s.
- arenafps: rebuilt guns with hands in view, a star muzzle flash, pickups as models
  over a lit spot, round jump pads, fuller fighter limbs, finer sparks and dust.
  Shadowed walls and guns are no longer black. A three-player split shows the arena
  and scores in the fourth quadrant.

## 0.8.2

- All games now run on cartwheel-engine v0.28.1.
- jetski: wave crests read as crests, with lighter water and foam at the top where
  one stands close. Not every crest shows foam yet.
- jetski: the hull stays on the water when climbing big waves at speed. It used to
  sink out of sight under its rider.
- jetski: a brighter sea, and the menu pages go back to the row that opened them.

## 0.8.1

- All games now run on cartwheel-engine v0.28.0.
- Join screens answer Start. A pad that hasn't joined takes a free seat and the
  match starts, Start works on the title screen, and every press gets a sound
  and "Starting..." on that same frame. Presses are no longer lost on displays
  faster than 60 Hz.
- Wave crests no longer turn black. Crests are still darker than they should be
  in jetski; more on that to come.
- Low, Medium and Auto graphics no longer lower the render resolution, so they
  cost more GPU than before on a weak machine.

## 0.8.0

- jetski: a real menu, with a pause menu in the race. There's one course so far,
  so the championship is one race for now.
- jetski: handling that answers the rider. Pull back while steering to carve much
  tighter, full stick at top speed scrubs speed, and the hardest turns can throw
  you off, with control back in under 3 seconds. A flat landing keeps its speed
  and a nose-down one loses a lot of it, unless you lean back.
- jetski: leaving the course for 5 seconds disqualifies you, and so do 5 missed
  buoys.
- jetski: a new attract loop, shot from above the water.

## 0.7.3

- Street Toughs has a stupider subtitle: "they are tough and this is a street".

## 0.7.2

- brawler is now called Street Toughs. The cart file is still `brawler.wasc`.

## 0.7.1

- jetski: a beached craft no longer spins on the sand. Throttle drives it back
  to the water, in about 3 seconds in the test, and with no throttle it slides
  back slowly.
- jetski: the spray from the jet stays behind the rider.

## 0.7.0

- jetski, a new game: a wave racing game on one evening's water. Six riders
  race three laps around a volcanic atoll, from a lagoon out across open sea.
  The swells throw you into the air, the sea builds from lap to lap, and each
  buoy passed on the correct side raises your top speed. This is a first
  version: one player, one course, and more polish to come.
- All games now run on cartwheel-engine v0.26.0, which adds open sea water.
- brawler: Dolly joins as the fourth fighter. Runs about twice as fast on the
  GPU: under 5 ms at 1080p on a Radeon 890M on a quiet machine, with baked
  street lighting. The fog is gone for now, including on the Foggy Docks.
- brawler: Arthur combs his hair in view, in an amber comb, and markers stay
  with fallen fighters.
- fighter: no more infinite combos. A juggle ends after three hits, a fourth
  shows JUGGLE OVER with a whoosh, and hitstun shortens over a long combo.
- fighter: Beam and Wide beam hit once for 400 damage and then fly on. Blocked,
  they chip 50 and don't break the guard.
- fighter: the computer reads repeats, so spamming one move stops working. Hard
  beat all 22 one-move bots in the test, normal loses to 5.
- fighter: knockdowns no longer trap a fallen player, fighters keep facing each
  other, Vex has 950 life, and training callouts sit clear of the move list.

## 0.6.0

- All 8 games now run on cartwheel-engine v0.25.0 (three.c v0.187.2, three.js
  r187). Gameplay is unchanged. The visible differences are very small: the
  largest is in arenafps, where about 1.5% of pixels shift slightly on metal
  and walls. Every game was checked on both GL and WebGPU.
- arenafps: split screen costs less GPU time on the new engine, most of all on
  WebGPU (11.9 ms down to 8.9 ms with four players in a short test on a Radeon
  890M at 1080p).

## 0.5.3

- coopplatformer: no more fire floating in the air: each flame sits in its
  own brazier, and the torches are standing braziers (iron cressets in the
  caves, bronze bowls on pedestals in the ruins).
- coopplatformer: the moving platform is a carved stone slab, floating wooden
  ledges are decks on posts (basalt piers over lava), and the grey seam lines
  across the rock are gone. Cracked blocks use the region's stone, and wall
  textures no longer repeat every 4 m.
- coopplatformer: the camera no longer looks under the world from a pit,
  rescue tags and player markers no longer overlap each other or the panels,
  and the respawn blink can't hide the whole group.

## 0.5.2

- hordeshooter: the floor guns you can now see. The arc caster draws jagged
  blue-white bolts between the enemies it chains through, with a wider flash
  across water; the frost lance is a long blue-white beam that tints chilled
  enemies, crusts frozen ones in ice and bursts them into shards; the saint's
  wheel is a spinning golden ring with a trail, sparking on each bounce on
  its way back to the thrower.

## 0.5.1

- hordeshooter: each floor brings its own gun, found on its power-up spots
  and in boss caches from that floor down: the arc caster (lightning that
  jumps between enemies and through water), the stake repeater (pierces
  three and pins the last), the slag mortar (over cover, leaves burning
  pools), the seeker swarm (eight homing rockets), the saint's wheel
  (bounces and returns), the frost lance (slows, freezes, shatters) and the
  hellfire lance (a ramping beam that sets enemies alight).
- hordeshooter: the cathedral boss's choir beam stops at columns and pews,
  and the last boss's legion brings golden elites that drop caches. Bosses
  no longer get knocked out of reach or stuck behind furniture, and a crash
  on the deepest floors (the cache gun pool) is fixed.

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
