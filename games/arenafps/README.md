# arenafps (working title)

A first-person arena shooter for one to four players on one screen (split screen, one pad per player), with
bots that find their way over a navigation mesh.

## Play

- Title -> join (each pad presses South to join) -> pick the arena, the mode, the number of bots, how good the bots are (easy, normal, hard) and aim assist on or off -> fire to start.
- Move: left stick (keyboard: W A S D). Look: right stick (keyboard: arrow keys). Fire: right trigger or right
  shoulder (keyboard: J). Jump: South (keyboard: Space). Next / previous weapon: North / West (keyboard: E / Q).
- Pause with Start.

Modes
- Deathmatch: everyone for themselves, first to 15 frags or the best score after five minutes.
- Team deathmatch: red against blue, first team to 30.
- Co-op waves: the players hold the arena against six waves of bots, with a shared pool of lives. A wave is bigger and tougher each time and bigger again for every extra player; between waves everyone heals a little and the rifle is topped up, and the next wave is counted down on screen.

Feedback: a red cross flashes on a hit, a gold cross and the victim's name on a frag, a red bar on a ring around the crosshair points at whoever hit you, the edges pulse red when health is low, and a downed player sees a respawn countdown (about 2.5 s; two seconds of protection after you spawn).

Weapons (found on the map; you always have the rifle). Time to kill from full health with perfect aim (no armour): rifle 1.1 s (0.7 s with head shots), scatter gun 0.77 s at point blank, rocket 0.73 s (two hits), rail 0.77 s (two hits, one head shot).
- Pulse rifle: fast hitscan, low damage, a head shot does half again.
- Scatter gun: eleven pellets, deadly close up.
- Rocket: slow shot, big blast and knockback (you can launch yourself; your own blast hurts half as much).
- Rail: pierces every fighter on the line; two body hits or one head shot.

Arenas: Foundry (a raised deck and pillars), Courtyard (buildings with doorways and roofs), Terraces (stepped
levels and a pit).

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

See the repository README: `CARTWHEEL_ENGINE=<engine checkout> scripts/fetch-deps.sh`, then `scripts/build.sh`.
`arenafps_auto` is the same game with the bots playing every seat (ARENA_ID, ARENA_MODE, ARENA_BOTS,
ARENA_PLAYERS choose the match): the tests and benchmarks run it.
`tests/arenafps-split-bench.mjs` measures the cost of one to four views.
