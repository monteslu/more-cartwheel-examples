# arenafps (working title)

A first-person arena shooter for one to four players on one screen (split screen, one pad per player), with
bots that find their way over a navigation mesh, jump pads, ledge jumps and drops from the roofs included
(the level tool writes the jumps and drops as navigation links; the pads add their own).

## Play

- Title -> join (each pad presses South to join) -> pick the arena, the mode, the number of bots, how good the bots are (easy, normal, veteran, hard; normal is for someone new to the game), aim assist on or off and rumble on or off -> fire to start.
- Move: left stick (keyboard: W A S D). Sprint: click the left stick (keyboard: left Shift), forward only, and firing
  ends it. Look: right stick (keyboard: arrow keys). Fire: right trigger or right shoulder (keyboard: J). Jump: South
  (keyboard: Space): high enough for a 1.2 m ledge. Next / previous weapon: North / West (keyboard: E / Q).
- Jump pads (glowing discs with rising sparks) throw you up onto the roofs.
- Pause with Start.

Modes
- Deathmatch: everyone for themselves, first to 25 frags or the best score after eight minutes.
- Team deathmatch: red against blue, first team to 40.
- Co-op waves: the players hold the arena against six waves of bots, with a shared pool of lives. A wave is bigger and tougher each time and bigger again for every extra player; between waves everyone heals a little and the rifle is topped up, and the next wave is counted down on screen.

Feedback: the view never shakes, bobs or kicks; blasts (by how close), hits taken, big hits and frags dealt, hard landings and each rocket or rail shot are felt in your own pad instead (rumble, Rumble on the setup screen; nothing without motors). A hit throws the target back a moment, a red cross and a dry thwack on a hit, a white cross, the word HEAD and a bright ping on a head shot, a gold cross, the victim's name, your streak and a rising chime on a frag, the name of whoever downed you, a red bar on a ring around the crosshair points at whoever hit you, the edges pulse red when health is low, and a downed player sees a respawn countdown (about 2.5 s; two seconds of protection after you spawn).

Weapons (found on the map and kept until you die; you always have the rifle). Scatter guns come back 10 s after they
are taken, rockets, rails and armour on a timer you can read over the empty spot (20 to 30 s). The rifle's shots walk off the
crosshair in a fixed pattern over a long burst and settle back after it, and the crosshair opens with them (the view itself never moves): short bursts stay on target. Time to kill from full health with perfect aim (no armour): rifle 1.1 s (0.7 s with head shots), scatter gun 0.77 s at point blank, rocket 0.73 s (two hits), rail 0.77 s (two hits, one head shot).
- Pulse rifle: fast hitscan, low damage, a head shot does half again.
- Scatter gun: eleven pellets, deadly close up.
- Rocket: slow shot, big blast and knockback (you can launch yourself; your own blast hurts half as much).
- Rail: pierces every fighter on the line; two body hits or one head shot.

Arenas (each two heights: rooms and halls below, roofs above, joined by stairs, jump pads and drops; every room has two
or more ways out):
- Foundry: an open core hall with a raised deck, open courts east and west, side halls north and south, spawn rooms on
  the diagonals and a corridor all round the outside; the roofs over all of it are the upper level.
- Courtyard: four houses of four rooms each round a plaza, streets between them and round the outside, bridges from roof
  to roof over the streets.
- Terraces: a flooded pit with an island, ledges 1.2 m up its walls, tunnels under the terrace to a gallery and spawn
  rooms whose stairs come up through the terrace floor.
The layouts are data: tools/make-levels.mjs writes levels/*.cwscene (CSG shapes), bakes them with the engine's cwcsg
tool into levels/*.cwcsg and checks their flow (loops, dead ends, how far spawns are from the power items).

The HUD shows the time left and the frag leader, and enemies carry a rim of their side's colour that you can read at
30 m (deathmatch: everyone is red); a fighter under spawn protection shimmers pale blue.

Aim assist (on by default, for pads): the view slows a little over an enemy, drifts gently toward one close to the crosshair while you aim or move, and a shot that only just misses an enemy in clear sight bends onto the edge of the body. Never through a wall, never a snap.

Bots notice a new enemy inside a view cone (all round on hard), or when hit or when a shot goes off within 30 m; they take a moment to react. The levels are tuned against a scripted pad player (`tools/human-matches.mjs`, reaction time and noisy stick aim through the game's own look curve): a casual player wins or nearly wins most matches on normal, a skilled one is pushed on hard.

## How it is made (the look)

- Fighters are one skinned mesh on a 15 bone rig with idle, run and die clips; the four weapons are first person models with
  gloved hands that sway, bob, kick and drop in when drawn; arena props (crates, barrels, barriers, columns, planters, pipes,
  vents, floodlights) are merged into the level meshes per material. All of it is modelled by the Blender scripts in
  `tools/blender/` (`tools/blender/build.sh soldier.py`, `weapons.py`, `props.py`: headless Blender 5.2) and shipped as glTF in `models/`.
- Each arena has its own light design: a sun with cascaded shadows, coloured accent and floodlight lights, height fog with
  light shafts, a colour grade (`tools/make-luts.mjs`), bloom, ambient occlusion and reflections.
- Rocket blasts use the engine's explosion tools: a shockwave ring and heat haze that bend the picture, fire, smoke, embers and
  stone chunks. Bullet holes and scorch marks are mesh decals that stick to the wall or floor they hit.
- In the autopilot build `ARENA_T_sun=2.5 ARENA_T_ev=-0.3 ...` (sun, sky, ambient, fog, ev, bloom, tod, shafts, ssr, grade)
  tries other light values without a rebuild.

## Build

See the repository README: `scripts/fetch-deps.sh`, then `scripts/build.sh`.
`arenafps_auto` is the same game with the bots playing every seat (ARENA_ID, ARENA_MODE, ARENA_BOTS,
ARENA_PLAYERS choose the match): the tests and benchmarks run it.
`tests/arenafps-split-bench.mjs` measures the cost of one to four views. `tools/playtest-human.mjs` plays the scripted human through romdev's pad on the cart.
