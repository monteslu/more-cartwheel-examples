# Horde (working title)

One to four players hold a walled yard against a crowd that never stops
coming. Everyone shares one camera, which zooms out to keep every living
player on screen.

## Playing

| | Keyboard (player 1) | Pad |
|---|---|---|
| move | W A S D | left stick |
| aim and shoot | arrow keys | right stick (aiming fires) |
| shoot toward where you aim | Space | A or right trigger |
| throw a blast | Left Shift or E | left shoulder |
| take upgrade 1 / 2 / 3 | 1 / 2 / 3 | X / Y / B |
| pause | Escape | Start |

Players 2 to 4 use pads 2 to 4. Join on the title screen.

- Kill things, grab the gems they drop (cyan; a richer gem is bigger and green,
  the richest gold: gems that land close together merge, and a gem left on the
  ground for eight seconds flies to the nearest player). Each level offers three
  upgrades (rapid fire, extra barrel, heavy rounds, piercing, rockets, boots,
  magnet, bigger blasts, armor); pick one, or the first is taken after eight
  seconds. The game keeps running while you choose. Every upgrade stops at a top
  level, and the horde gets tougher each wave and bigger with more players.
- Power-ups lie at set spots in the yard (the four corners, the gaps in the
  cinder ring the horde pours through, the ends of the painted lane, behind
  crates): a new one every 30 seconds at the next free spot, at most three at
  once, and brutes and bosses sometimes drop one. Each is a glowing model of
  its own shape and colour with a ring of light under it. Walk over one to take
  it; when several players reach it together the one holding least gets it, and
  nobody takes a second of a kind they still hold for more than 5 seconds.
  The panel shows what you hold and the seconds left.

  | power-up | what it does |
  |---|---|
  | Shotgun | 12 s: a wide spread of pellets, slower |
  | Minigun | 12 s: twice the rate of fire |
  | Rockets | 12 s: every volley carries a rocket |
  | Rail gun | 12 s: slow, heavy shots that go through a line of them |
  | Flamer | 12 s: a short cone of fire that burns through several |
  | Explosive rounds | 12 s: every round bursts where it hits |
  | Rapid fire | 12 s: half the time between shots |
  | Grenades | 4 charges (up to 8): the blast button throws one in an arc |
  | Mines | 3 charges (up to 6): the blast button drops one at your feet; it goes off when they step near |
  | Buzzsaws | 12 s: two blades orbit you and shred what they touch |
  | Shield | 8 s: a bubble; nothing hurts you and it shoves them off |
  | Magnet | 10 s: every gem in the yard comes to you |

  With grenades or mines held the blast button uses them first.
- Red barrels explode and leave a fire. Crates drop gems and health. The grey
  cinder walls only fall to blasts.
- The run is a descent through eight floors (LEVELS.md), the yard the first. A
  floor has five waves (the deeper four have six), a wave every 25 seconds,
  and its last wave brings the floor's boss. When the boss falls the rest of
  the horde burns to ash over a second and a half, and a hatch opens in the
  middle of the floor with a beam of light over it and an arrow marking it;
  when one living player steps in, the team goes down to the next floor with
  its levels and score (power-ups end at the hatch). Each floor starts a
  little harder than the one above. Beat the eighth floor's boss and the
  descent is done (about 26 minutes for the bots). The seven floors below
  the yard (the sluice, the ossuary, the foundry, the grotto, the cathedral,
  the rift and the core) are built from their own level files (the level
  builder in tools/levelview, so their bakes match).
- Each floor brings its own creature and boss (LEVELS.md). Skitters (the
  sluice) are fast and frail, swim the water and spring 3 m to bite;
  bonewalkers (the ossuary) lurch and slash, and leave bone piles, one in five
  standing up again in 4 s unless shot; slagborn (the foundry) lob burning
  slag from range and punch up close; sporelings (the grotto) headbutt and
  burst into spore clouds that slow; penitents (the cathedral) glide, draw
  back and rush 4 m; gargoyles (the rift) hop and dive; hellhounds (the core)
  are the fastest thing in the game and pounce 4 m. Water slows walkers and players, molten
  strips burn, spore beds slow. The bosses tell every move (a held pose and a
  warming glow): Vhessa sweeps her pincers, jets acid in a cone and drops a
  brood of skitters; Ossivar slams a ring of spikes round him, throws fans of
  slow bone lances and raises every bone pile; Anvilgrave's hammer sends a
  shockwave, he pours a molten river and roars up slagborn; Mother Mycel
  lashes her roots, mortars spores where you stand and bursts roots up in a
  line toward the farthest player; Seraphel sweeps her censer round her, burns
  a choir beam across her front and steps through shadow behind the farthest
  player, penitents rising; Khar-Vaal cleaves, fires a ring of shards with two
  gaps and breaks the floor in a ring round him; the Unmaker brings his blade
  down along a long line, rains fire round every player and calls a legion of
  the earlier floors' creatures.
- A boss bursts into 3 to 5 loot caches, each a gun at a rarity whose colour
  glows on it and on the floor (common grey, uncommon green, rare blue, epic
  violet, legendary orange): a better gun lasts longer, hits harder, fires one
  more (rare) and pierces one more (epic). Each floor adds a gun to the pool
  (the rail gun from the sluice, the flamer from the ossuary). Bloaters burst when they
  die; brutes are slow and tough. A boss walks, stands still and glows for a
  second (a warning, and a bar at the top shows its health), then charges
  straight ahead; it calls a few runners now and then.
- Up close the creatures attack: each winds up (its pose raised and its hide
  warming, the telegraph), strikes on one moment, then recovers, planted and
  facing you the whole time. The strike only hurts if you are still in reach
  and in front of it, so stepping away during the wind-up dodges it; standing
  in a crowd does not. A runner rakes its claw and lunges (a quarter second's
  wind-up), a brute raises both fists and smashes (about half a second, a
  long reach), a bloater rocks back and slams its belly (wide), and the boss
  winds round for a sweep that covers most of its front. A hit bleeds, and
  rumbles your pad, harder for a heavier hitter.
- A downed player gets up at the next wave, or when a friend stands close for
  two seconds, or is carried to the nearest friend after eight. If everyone is
  down the yard resets a wave back. The team stays within one view: nobody can
  run farther from the others than the camera can show (about 32 m across and
  17 m deep).

## Building

`scripts/fetch-deps.sh`, then
`scripts/build.sh`. `hordeshooter_auto` is the same game played by bots for
the tests. Development switches (environment): `HORDE_WAVE=n` starts at wave
n, `HORDE_PLAYERS=n` sets the auto build's player count, `HORDE_QUALITY=0..3`
picks a graphics preset, `HORDE_BENCH=1` prints timings every 120 frames,
`HORDE_REPORT=1` prints the first kill, every level-up and every hit,
`HORDE_BAKE=<file>` bakes the yard's lighting again and writes it there, `HORDE_HIDE=1|2|4` hides the crowd,
the blob shadows or the light pools (timing A/B), `HORDE_POWER=n` hands every
player power-up n (0 to 11, the table order) and keeps it held, `HORDE_POWER_SHOW=1`
lays one of each round the start. A run started at a later
wave begins with about the levels a played run has there.
`HORDE_CULL=0` draws the horde outside the view too (the A/B of the culling).
`HORDE_MELEE_TEST=1` is the melee gate's scene (one player, a runner and a brute, no spawning;
`HORDE_MELEE_DODGE=1` steps the player away from every wind-up, `HORDE_MELEE_LOG=1` prints each strike).
Live switches for romdev (a cart reads no environment; write them with `wasm({op:'write'})` once the game has
started): `dev.hold` 1 makes the bots stand where they are, 2 also holds their fire, so the horde reaches
them; `dev.cam_dist` fixes the camera's distance (16 is a close-up, 0 the game's framing); `dev.boss` 1 skips to the
floor's boss wave and keeps the boss weak, 2 also fells it once it is out, to see the floor end (the melt, the
hatch, the descent), 3 only skips to the boss wave (the boss at full strength, to see its patterns).
`hordeshooter_bench` is the auto build at the run's wave 24 (floor 5, wave 4) with three players from the first frame (about 2000
enemies), the late-wave scene `tests/hordeshooter-perf.mjs` times on GL and WebGPU at 1080p (a cart reads
no environment, so its start is built in).
`hordeshooter_soak [minutes] [players] [wave]` plays the rules with the bots and
no window and no GPU (30 game minutes take a few seconds) and checks every tick
for stray positions, counts that disagree and a team stretched past one view
(`HORDE_SOAK_TRACE=1` prints kills, gems and every player's level each second;
`HORDE_SOAK_TTK=1` hits and seconds to kill each kind at each wave;
`HORDE_SOAK_POWER=1` power-ups taken and kills a second while each was held;
`HORDE_SOAK_NOPOWER=1` no power-ups, `HORDE_SOAK_NOUP=1` the starting weapon all run).

Every texture, model, effect sheet and sound is generated by the game's own
code; see CREDITS.

## How it is drawn

- The soldier, the four creatures and the yard's set dressing (wall, floodlight
  masts, containers, barriers, rubble, tyres, pipes) are modelled and animated
  in Blender by the scripts in `tools/blender/`. Each character's walk or run
  cycle is baked frame by frame into `assets/models/*.cwchar`; the horde draws
  one instanced mesh per frame of the cycle, so thousands of animated creatures
  cost a handful of draws and nothing for skinning. Each creature also has an
  attack clip of three key poses (wind-up, strike, recovery) baked the same
  way; an attacking creature is drawn from those, chosen per instance. The walk
  cycles have six frames (the boss ten), so a kind costs one draw more than
  with eight-frame walks and no attacks.
  Rebuild them with
  `flatpak run org.blender.Blender --background --factory-startup --python <abs path>/tools/blender/hordechars.py -- <abs path>/assets/models`
  (and `hordeprops.py` the same way).
- Light, built to run on weak GPUs: the yard's floor is baked
  (`assets/lightmaps/yard.cwlm`, made by the game itself with `HORDE_BAKE`): a low warm
  sun, a cool rim light and six floodlights, with soft shadows from the walls, masts and
  containers and the dusk sky, drawn unlit from its lightmap. The floodlights live in
  the bake only; the sun and the rim stay real time for everything that moves. The
  crowd, the soldiers and the yard's pieces take the light of the floor under them
  (a floodlight's pool, a wall's shadow), cast soft blob shadows that run away from
  the low sun, and the soldiers' lamps, muzzle flashes, blasts and fires show on the
  floor as pools of light. Puddles reflect a baked reflection cube and glint where a
  floodlight or a soldier's lamp would reflect in them; the damp asphalt catches each
  lamp as a soft streak. Light shaft cards under the floodlights, a warm haze in every
  material, bloom and a dusk colour grade (`tools/gen-lut.mjs`). The crowd is
  Lambert-shaded (it is small on screen and thousands strong); the soldiers keep the
  full PBR model. The yard always draws at the full output size.
- Players stand out: a team-coloured ring, a light that rides over each
  soldier, a faint team glow on the plate and a P1 to P4 tag over each head;
  the horde is darker and cooler than the floor.
- Blasts use the engine's explosion kit (Explosion3D): fire and smoke
  flipbooks blended along baked motion vectors, sparks that bounce, debris
  chunks trailing smoke, embers, heat haze and a ground shockwave that bend the
  picture, a pooled flash light and a scorch mark that glows and cools. Barrel
  fires keep burning for a few seconds with their own haze.
- The camera never shakes. The pads rumble instead (cw_input_rumble), each
  player's own and briefly: blasts on the heavy motor, fading with that
  player's distance; a short sharp buzz when hit; a light tick on a rocket,
  rail gun or shotgun shot (not on every bullet); a short one on a big kill.
