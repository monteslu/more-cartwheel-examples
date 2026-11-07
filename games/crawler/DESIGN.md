# crawler: design

One to four players fight their way down through a long dungeon, one maze floor at a time, on one shared
top-down screen. Every floor is a maze of corridors and rooms with spawners pouring monsters out, locked doors
and the keys that open them, food, potions, treasure and an exit down. Health is the clock: it drains all the
time, monsters take more, food gives it back. Four classes with their own strengths. 16:9, one fixed zoom.

## The view

- The classic overhead maze screen, drawn with a modern renderer. The camera sits over the middle of the living
  players, tilted 15 degrees from straight down toward the south, through a long lens (a 14 degree field of view from
  about 98 m: close to orthographic, so walls do not lean or splay toward the edges). Walls show a thin strip of their
  south face.
- Everything that stands up (heroes, monsters, upright pickups, spawners) leans 15 degrees back toward the camera from
  its feet, in its own instance matrix. The lean matches the tilt, so a character is seen down its own up axis, as
  from straight above: the walls gain depth and the characters keep the overhead read (their faces, colours and
  weapons are modelled on the surfaces that face up). The maze, the rings and the shadows on the floor never lean;
  hits are on the floor plane; a 1.2 m wall at 15 degrees hides at most 0.3 m of floor behind it.
- Heroes, shots and pickups are drawn pulled a few metres toward the camera along their own line of sight (the same
  place on screen, a hair larger), so no monster and no wall is ever drawn over them; their rings and shadows stay on
  the floor.
- 20 degrees of tilt with 20 of lean was tried against 15 and 15 on the same frames: the characters look the same
  (the lean cancels the tilt either way), the walls show more south face and hide up to 0.44 m of floor behind them,
  and the screen holds about the same rows (12.7 tiles of floor against 12.4, less the taller wall faces). 15 and 15
  stays.
- One fixed zoom: 12 tiles of 2 m top to bottom and 21.3 across at 16:9 (the reference showed 13 x 14 tiles on the
  Genesis and 16 x 11 on the NES). The zoom never changes.
- The view scrolls smoothly with the group: it follows the middle of the living players, catching up over about
  eight frames, with no lead ahead (the reference's screen moved one for one with the walking player). Nobody can
  leave the screen: a player can be at most the screen less about 2 m from every other player, so one player at the
  edge holds the others back and the group moves together. The bots and the soak keep to the same limit.
- A seat nobody plays never holds the team: a hero whose pad has not been touched for five seconds (IDLE_TICKS) is
  left behind. It holds nobody back and the view glides over two seconds (IDLE_FADE) to follow the others only; then,
  when it would leave the screen, it is carried to the side of the nearest hero who plays (the teleport's flash). When nobody plays, everyone counts as before. A pad
  that joined and was put down would otherwise pin the team to the start for good (floor 1's exit is 26 m below its
  start, the team's reach 19 m).
- Walls are low (1.2 m) blocky tiles, so the maze reads as a clean tile layout. From straight above only their
  tops show, and the tops carry each tileset's look: a crisp pattern, a bevel, materials, the bake's light and the
  shadow they cast on the floor. Nothing is cut away or faded: nothing stands between the camera and a hero.
- Everything reads from above: heroes about a tile across (1.9 times life size), a mid-dark body with the class
  colour as its accent (crimson Brute, blue Warden, violet Mage, green Ranger), a weapon in bright metal or wood that
  stands off the body, a crisp dark outline (the back faces of a hull pushed out along the normals) and a rim of
  light in the class colour, a thin team ring and small team trim. Monsters at 1.3 to 1.7 times: each kind a dark
  body with one saturated accent of its own, glowing eyes, a shape of its own from above, and an edge in its colour
  round its silhouette (the same hull, unlit), so bodies part from the floor and from each other. A dark contact
  shadow under every body. Keys, potions, charms, spawners and the exit under columns of light with glows on the
  floor.
- Text is at least 2.4% of the screen's height; prompts are the engine's button prompts (a diamond of four dots
  on a pad, a keycap on a keyboard).

## The look: mood without darkness

- Mid-key, never washed out: the screen round the team sits at about 110 to 130 of 255 in mean luma, with real
  contrast (dark crevices between floor stones, a strong key light that shades the characters, a small ambient fill)
  and a filmic tone curve. The floor's value stays under the characters' highlights so they pop, and the walls sit a
  step under the characters: never the brightest thing on screen. Each tileset's floor is brought to about the same
  value (a gain on its bake), so the darker stone of the deep tilesets does not sink the screen.
- Every floor is lit to the corners, so the whole visible maze reads at a glance as it did in the classic. A floor
  that feels moody only because you cannot see it is a failure; so is one so bright that it goes flat and pastel.
- The atmosphere comes from: a colour grade per tileset (the .cube LUTs); warm against cool light (warm torches
  against a cool fill, coloured light from spawners and exits); low haze and drifting dust or embers; floor decals
  (scorch, blood, moss); materials that sell the place (wet stone, rust, glowing runes, emissive cracks); animated
  light (flickering torches, pulsing spawners); sound.
- Lower quality presets thin particles and secondary lights; they are never darker.

## Controls (the same as the classic arcade maze game)

| Action | What it does |
|---|---|
| move | walk in eight directions (stick or d-pad; WASD or arrows) |
| fire | hold: stand still and shoot the way you face; turn with the stick while holding |
| magic | use a potion: blasts every monster on the screen |
| bump | walking into a monster fights it hand to hand (your melee power) |
| Start | join at any time (a fallen player rejoins with fresh health) and pause |

Shots stop at walls. A shot hits the first monster in its way (unless a power-up makes it pierce). Shooting
food or a potion breaks it (a shot potion goes off at a third of its strength where it lies). Shooting another
player stuns them for 0.4 s and does no damage.

## Classes

| | Brute | Warden | Mage | Ranger |
|---|---|---|---|---|
| role | the melee fighter | the armoured guard | the spellcaster | the quick archer |
| look | broad, bare-armed, a two-handed cleaver and a war-axe for throwing; leather and iron | plate and a round shield, a warhammer; a closed helm | a long coat, a staff with a burning crystal, a hood | light leathers, a recurve bow, a hood and a quiver |
| speed | 5.6 m/s | 6.2 m/s | 6.2 m/s | 7.4 m/s |
| armour (damage taken) | 80% | 65% | 100% | 90% |
| shot | a spinning thrown axe: 3 damage, 13 m/s, 2 in flight | a thrown hammer: 2 damage, 15 m/s, 2 in flight | a fireball: 2 damage plus a 1.2 m blast of 1, 14 m/s, 2 in flight | an arrow: 1 damage, 22 m/s, 3 in flight, the quickest rate |
| melee | 4 a hit, 3 hits a second | 3 | 1 | 2 |
| magic (potion) | 2 x | 2 x | 4 x (and it hits spawners) | 3 x |
| signature look | the cleaver's swing leaves a hot arc and sparks; the axe spins with a motion trail | every hammer hit throws dust and a ground ring; heavy impacts | the fireball (below) | every arrow leaves a streak and a glint; hits pin a spark |

The Mage's fireball is the showpiece: a burning core lit past white so bloom catches it, a trail of flame and
embers from the engine's motion-vector flipbooks, heat haze through the distortion pass, a moving light that
lights the corridor walls as it flies, and on impact an explosion (the engine's Explosion3D: fire, sparks that
leave smoke through PARENT sub-emitters, a shockwave ring, debris, a scorch mark that cools).

Hit points: every class starts with 1500 health (the panel shows it; the reference gave 2000 a credit), a fallen player comes back with 1000, food adds to it, the drain takes one every
1.2 s. Health has no cap.

## Monsters

Every monster has a strength of 1, 2 or 3 (size, colour and hit points); a spawner makes its kind at its own
strength. Damage is per touch or per hit, before the player's armour.

| Monster | Our design | Moves | Attack | Health per strength | Score |
|---|---|---|---|---|---|
| Shade | a tattered gliding wraith | 4.6 m/s, glides | touches you for 8 x strength and is gone | 1 | 10 |
| Ghoul | a hunched skeleton with a notched blade | 4.0 m/s | slashes: 4 x strength | 2 | 10 |
| Imp | a small winged stone demon | 4.4 m/s, hops | spits a fireball (6 x strength) from 3 to 9 m when it sees you, bites up close | 2 | 20 |
| Slinger | a heavy slag creature | 3.2 m/s, keeps 6 m away | lobs a burning lump over walls (5 x strength, a 1.2 m splash) | 3 | 20 |
| Hexer | a robed caster with a bone mask | 3.6 m/s, flickers: unseen and unhittable 1.5 s of every 3 | a hex bolt (7 x strength) | 2 | 30 |
| Dread | a tall faceless shroud trailing chains, a cold lantern | 3.8 m/s, ignores walls' corners (floats through crowds) | holds on and drains 10 a quarter second, up to 160, then lets go and fades | shots only push it back; magic destroys it | 1000 |
| Pilferer | a small hunched thief with a sack | 6.8 m/s | grabs a potion, key or power-up from the nearest player and runs for the exit; killed, it drops what it took | 2 | 100 |
| Ogre | a huge horned brawler with a spiked club (hordeshooter's heavy model, reworked) | 2.6 m/s | a two-fisted smash: 12 x strength | 8 | 40 |
| Hound | a lean burning hound | 6.0 m/s | a pounce from 3 m | 2 | 20 |
| Bloater | a swollen sac | 2.0 m/s | bursts in a 2.5 m cloud of poison when it dies or touches you: 10 | 1 | 20 |

## The crowd on screen

A floor's spawners keep pouring (hundreds of monsters a floor), but one screen holds at most SCREEN_CAP, 120
monsters (the classic showed 15 to 20 at once from one generator in its first seconds, on a screen of 13 x 14 tiles;
ours is 12 x 21 and the deep floors are meant to be busier). At the cap, the spawners on or near the screen hold
back and the monsters near it wait off it, so a corridor reads as a line of monsters to shoot through, never a
carpet. Walking onto a waiting queue can carry the screen up to about half again past the cap; spawns never do. Each monster keeps
its drawn footprint in the crowd (wider than its touch), so bodies stand apart and never pass through each other.
The perf gate keeps a stress scene with no cap (1800 monsters packed round the team) as a gate of its own.

Monsters attack with a wind-up the player can read (a raised pose and a warming glow), so stepping away from one
dodges it; in a crowd you cannot step away.

## Spawners

Each spawner makes one kind of monster at one strength. It takes 3 hits a strength level (strength 3: 9 hits)
and drops a strength each 3 hits (its model changes: a cracked, then a broken one). It makes a monster every
1.4 s at strength 1, 1.0 s at 2, 0.7 s at 3 (faster on deeper floors, to 0.4 s), only while a player is within
24 m of it, and keeps up to 40 of its own alive (60 on deep floors). The Mage's magic hits spawners too (every
spawner on screen loses a strength).

The look is the tileset's: a heap of bones in the crypt, a grated sluice mouth in the sewer, a cracked furnace in
the foundry, a pulsing spore pod in the grotto, a reliquary in the cathedral, a rift in the abyss. Each glows in
its monster's colour and pulses as it spawns.

## Keys and doors

- A key is carried (up to 9 a player). Walking into a door with a key opens the whole door: every door tile
  touching it opens at once, and the key is used.
- Keys lie in reach before their doors: every floor's generator places them so that the exit can be reached
  with the keys found on the way (the level check proves it on every floor).
- A floor held for 180 s opens all its doors by themselves.

## Finding the way (read from the overhead camera, on every preset)

- From straight above a column of light is only a disc and a 0.64 m gate of bars is a rail, so what matters is
  marked where the camera sees it:
  - A shut door's gate fills its tile (stretched through it, DOOR_SHUT_DEPTH), with a glowing key lying on its top and
    warm light on the floor either side: it wants a key. Opening, it sinks as modelled.
  - A key is drawn half as big again as other pickups, with a strong pulsing pool of light.
  - Every way down has EXIT over it (DOWN n for a stair that skips floors) in a ring of light, and a wide pool of light
    round the hatch.
- After 20 s on a floor an arrow by the leading hero points a few tiles along the way: to the exit, or to the nearest
  key while a shut door stands between the team and the exit and nobody holds one; a key in sight gets a ring and KEY.
  tests/crawler-find-the-way.mjs proves a player following only the arrow gets from floor 1 to floor 2.

## Health, food and potions

| Item | Gives | Shootable |
|---|---|---|
| roast | +100 health | no |
| bread | +50 | yes (breaks) |
| flagon | +100 | yes (spills) |
| plate (a covered dish) | +200 | no |
| potion | one magic: every monster on the screen takes 8 x your magic (spawners only for the Mage, a strength each) | yes: a third of the blast where it lies |
| elixir (a coloured potion, the rest of the run) | +1 to armour, shot power, shot speed, magic, melee, or speed (its colour says which) | no |

## Treasure

Gold (100), a gem cup (250), a chest (500, takes 3 hits or a key: a trapped chest, 1 in 6, lets out 6 monsters).
A score at the end of each floor; the panel keeps each player's score.

## Power-ups (the expanded set; hordeshooter's, made for the dungeon, and more)

Charms: picked up, shown on the panel with their seconds left. One of each kind at a time; a second of the same
adds time.

| Charm | Seconds | What it does |
|---|---|---|
| Triple shot | 20 | three shots in a fan |
| Rapid | 20 | twice the rate of fire |
| Pierce | 20 | shots go through 4 monsters |
| Bounce | 20 | shots glance off walls twice |
| Seeker | 20 | shots curve toward the nearest monster |
| Chain | 15 | each hit arcs to 3 more within 4 m |
| Frost | 15 | hits slow, a second hit freezes; frozen ones shatter into their neighbours |
| Embers | 15 | two burning orbs circle you and burn what they touch |
| Ward | 10 | nothing hurts you; monsters touching you are shoved back |
| Repulse | 15 | monsters cannot come within 2.5 m |
| Shroud | 15 | monsters lose you unless they touch you |
| Lodestone | 20 | treasure, food and keys within 10 m fly to you |
| Firebombs | 4 charges | magic throws a firebomb (a big blast) before it uses a potion |
| Haste | 20 | 1.4 x speed |
| Giant | 15 | twice the size: melee 3 x and shots 2 x |
| Fortune | 30 | treasure is worth twice |

Charms lie on set spots of the floor (2 to 5 a floor), and a strength 3 monster drops one now and then (2%).

## Floors and exits

- 120 floors in six tilesets of 20 floors: the Crypt (1 to 20), the Sewer (21 to 40), the Foundry (41 to 60),
  the Grotto (61 to 80), the Cathedral (81 to 100), the Abyss (101 to 120). Each tileset has its own wall kit,
  floor, light, spawner, door and the monsters it favours.
- A floor is 24 x 24 to 44 x 44 tiles of 2 m. The exit is a stair down; stepping on it takes the team to the next
  floor (one player is enough: the others follow). Some floors have a second exit that skips ahead 4 or 8 floors
  (marked by its glow and number).
- Floor features: breakable walls (2 hits), trap plates (stepping on one makes the marked walls vanish),
  teleporters (to the nearest other teleporter), and the doors.
- The difficulty climbs with the floor number: spawner strength, monster count cap, spawn rate and the share of
  the harder kinds. Each tileset's first floors are a breather.
- Game over when every player is down; the run continues from the floor reached (the title offers it).

## How floors are made

- Every floor is a `.cwmaze` text file in `assets/levels/`: a grid of characters (one per 2 m tile) and lines for
  what stands on it (spawners, keys, food, potions, treasure, charms, props, lights). Format in the header of
  `maze.h`.
- `tools/gen-levels.mjs` writes them all from a seed per floor: one of several layouts (room-and-corridor,
  braided maze, cave, hall of pillars, concentric rings, cross vaults), then doors and keys (keys placed
  where they are reachable with the keys before them), spawners away from the start, food and treasure along the
  long way, decor and torches from the tileset's kit. A few floors are drawn by hand (`tools/levels/*.txt`) and
  merged by the generator: floor 1 (a gentle start), and each tileset's first and last floor.
- `tools/levelview` builds a floor exactly as the game does and bakes its lightmap (`lightmaps/<n>.cwlm`, the
  floor's light from the tileset's torches, braziers and glows, with the walls' shadows and occlusion). The bake
  records the hash of what it baked, so a floor that changed after its bake is caught.
- `tools/check-levels.mjs` (CI) fails a broken floor: the exit is reachable from the start with the keys found
  on the way, every key comes before its door, nothing spawns or stands inside a wall, every prop is grounded (its
  model's lowest point on the floor or a wall face behind a wall-mounted one), the bake is present and matches,
  and a bot clears the floor in the soak build. A control set of floors broken on purpose must fail each check.

## Performance (inherited from hordeshooter, kept)

- One flow field per floor on a 1 m grid round the maze (doors closed are walls; an opened door updates it), read
  by every monster; the engine crowd (cw_crowd_set_velocity on the job pool) keeps them apart. No per-monster
  path planning.
- Monsters drawn as instanced meshes, one per animation frame, vertex animation baked into `.cwchar` (no
  skinning). Walls, props and pickups instanced too; walls and the crowd culled to the view on the CPU.
- The floor's light baked per floor and drawn unlit; moving lights (players, shots, blasts) through the clustered
  lights and as light pools on the baked floor. Pooled effects, bursts and a decal pool.
- The quality presets thin out particles and secondary lights, never the crowd and never an attack's effect.
- Very Low (engine v0.31.0) takes the engine's cuts and draws the game's own effects as Low: 0.66 to 0.73 of High's GPU
  time in play (tests/quality-range-scenes.json).
- Perf gate: a crowded late floor of each tileset at the screen cap, and a stress scene with no cap (1800 monsters
  packed round the team), 1920 x 1080 High on the 890M, GL and WebGPU, about 4 ms of GPU a frame.
- The crowd stays Lambert (cheap per pixel); its edges are unlit hulls that share the bodies' instance matrices and
  colours (one upload), drawn after the bodies so the depth test leaves only the rim.

## Camera and feel

- No screen shake: big hits and blasts rumble the pads (each player's own, scaled by distance).
- Readability: each player has a team-coloured ring and light and a P1 to P4 tag, a dark outline and a class
  rim, and is drawn over the crowd; monsters are dark bodies edged in their kind's colour; pickups glow and bob.
