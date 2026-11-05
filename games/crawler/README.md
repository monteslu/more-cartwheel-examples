# Crawler (working title)

One to four players fight their way down a dungeon of 120 maze floors on one shared top-down screen. Every floor
is a maze of corridors and rooms with spawners pouring monsters out, locked doors and the keys that open them,
food, potions, treasure and a stair down. Health is the clock: it drains all the time, monsters take more, food
gives it back. Four heroes, each with its own strengths. The design is in [DESIGN.md](DESIGN.md).

## Playing

Every player has one stick and two buttons; the game shows each player's own buttons as prompts.

| | Keyboard (player 1) | Pad |
|---|---|---|
| move | W A S D or the arrow keys | left stick or d-pad |
| shoot (hold: stand and shoot the way you face, turn with the stick) | Space or J | the bottom face button, or the right trigger |
| magic (a potion, or a firebomb when you hold some) | Left Shift, K or E | the right or left face button, or the left shoulder |
| join, or back in after falling | Enter | the top face button |
| pause | Escape | Start |

- Choose a hero at the start: the **Brute** fights best hand to hand and throws axes, the **Warden** wears the
  heaviest armour and throws a hammer, the **Mage** throws fireballs and has the strongest magic (it cracks
  spawners too), the **Ranger** is the fastest, with the quickest bow.
- Walk into a monster to fight it. Shots stop at walls. Shooting food or a potion breaks it (a shot potion goes off
  weaker where it lies); shooting a friend stuns them for a moment.
- A key opens a whole door: walk into the door holding one. Every floor can be finished with the keys found on
  the way. A floor held for three minutes opens all its doors.
- Health drains by one every 1.2 seconds. Roast, bread, flagons and covered plates give it back. A fallen player
  comes back with fresh health on the join button (at half their score); when the whole team falls the floor
  starts again.
- Potions blast every monster on the screen. Elixirs raise armour, shot power, shot speed, magic, melee or speed
  for the rest of the run. Charms last a while: triple shot, rapid fire, pierce, bounce, seekers, chain lightning,
  frost, orbiting embers, ward, repulse, shroud, lodestone, firebombs, haste, giant and fortune.
- Spawners make one kind of monster each, faster at higher strength; three hits take a strength off. Shades touch
  once and vanish, ghouls slash, imps spit fire, slingers lob over walls, hexers flicker out of sight, ogres smash,
  hounds pounce, bloaters burst into poison, pilferers steal a potion or a key and run for the exit, and a dread
  drains health and only magic ends it.
- Treasure, cups and chests add to the score; one chest in six is a trap.
- Breakable walls fall to two hits, trap plates make walls vanish, teleporters carry the whole team, and some
  floors have a second stair that skips four or eight floors.
- Six tilesets of twenty floors: the crypt, the sewers, the foundry, the grotto, the cathedral and the abyss. The
  hero screen offers to start at the first floor of the furthest tileset reached.

## How the floors are made

- Each floor is a `.cwmaze` text file in `assets/levels/` (the format is in `maze.h`): a grid of 2 m tiles and lines
  for what stands on it. `tools/gen-levels.mjs` writes all 120 from a seed each (rooms and corridors, braided mazes,
  caves, halls of pillars, rings, vaults), places doors across the way to the exit with their keys before them,
  spawners away from the start, food, treasure, charms and the tileset's torches and decor. A floor drawn by hand
  goes in `tools/levels/<nnn>.txt` and replaces the generated one.
- `tools/levelview` (`crawler_levelview`) builds a floor exactly as the game does (`mazebuild.c`) and bakes its
  lighting: build it first (`scripts/build.sh crawler_levelview`: it bakes the floors as packed), then
  `crawler_levelview --bake games/crawler/assets/lightmaps` bakes all 120 in under a minute and writes
  each bake's hash beside it. As a cart it shows the floors for review (left and right change floor).
- `tools/check-levels.mjs` (run in CI) fails a floor whose exit cannot be reached with the keys on the way (even
  spending keys on side rooms first), that has anything standing in a wall, a prop that floats or sinks, a missing or
  stale bake, or that the bots cannot clear in the soak build (`--soak build/native/games/crawler/crawler_soak`). The
  floors in `tools/levels-broken/` are each broken on purpose in one way, and the check fails if it passes one.
- `tools/gen-roots.mjs` writes the pack list (`assets/roots.txt`).

## Building and testing

`scripts/fetch-deps.sh`, then `scripts/build.sh crawler crawler_auto`. `crawler_auto` is the game played by bots;
`crawler_bench` is four bots on floor 58 with 1800 monsters packed round them (the perf gate's scene).

- `crawler_soak --clear <floor.cwmaze>`: the bots play one floor (the level check's clear run).
- `crawler_soak [minutes] [players] [first floor]`: floor after floor with no window or GPU, checking every tick for
  stray positions, players inside walls, counts that disagree and a team spread past one screen.
- `node tests/crawler-perf.mjs`: the perf gate (GPU time a frame at 1920x1080 High on GL and WebGPU, a crowded late
  floor of every tileset, with controls that must fail).
- Development switches (native): `CRAWLER_FLOOR=n` starts on floor n, `CRAWLER_PLAYERS=n` the auto build's players,
  `CRAWLER_CROWD=n` packs n monsters round the start, `CRAWLER_CHARM=n` keeps charm n held, `CRAWLER_BENCH=1`
  prints timings, `CRAWLER_QUALITY=0..3` a graphics preset. The auto cart answers the debug command `floor <n>`.

## How it is drawn

- The heroes, the new monsters, the six tile kits (walls in three variants and a cut-down stub, breakable walls,
  doors, spawners, torches, decor) and every item are modelled in headless Blender by `tools/blender/crawlerchars.py`
  and `crawlerkit.py`; shades, ghouls, imps, slingers, ogres, hounds, bloaters, skitters and sporelings are
  hordeshooter's creatures reworked for the overhead view (`tools/blender/hordechars.py` and `levelchars.py`).
  Each one is a dark body with one accent colour of its own, and the crowd draws an edge in that colour round every
  body. Each animation frame is baked into `assets/models/*.cwchar`; the crowd draws one
  instanced mesh per frame, so thousands of monsters cost a handful of draws and no skinning.
- Every floor's light is baked (the torches, the spawners' glow, the exit) and the floor draws unlit from it; the
  walls and everything that moves take the bake's light where they stand. The heroes carry lights, shots carry
  lights, and every moving light shows on the baked floor as a pool.
- The camera looks down at a fixed wide zoom (12 tiles top to bottom), tilted 15 degrees so the walls show depth,
  with everything that stands leaning to match, and scrolls with the group; the walls are low blocks whose tops
  carry each tileset's look. Every floor is lit mid-key to its corners; torches, glows and the shots' lights are
  accents on top of it. Heroes, shots and pickups are drawn over the crowd.
- One screen holds at most 120 monsters: the spawners keep pouring, but at the cap the ones near the screen hold
  back and the monsters near it wait off it.
- The Mage's fireball burns past white with a trail of flame and embers, heat haze and a light that lights the
  walls as it flies, and bursts in a small explosion with sparks that leave smoke, a ring and a cooling scorch.
  The Brute's swings leave hot arcs, the Warden's hammer throws dust and a ring, the Ranger's arrows streak and glint.
- The camera never shakes; the pads rumble instead.
