# Changelog

## 0.33.0

- Jet ski: two kinds of craft, picked on the menu's new Craft row and remembered; the whole field rides the one picked, so a race stays even.
  - Sit-down: the runabout as before, faster on the straights and steady, with a bow rebuilt as a raked beak (the keel sweeps up to it, the foredeck falls to it without the dip it had), mirrors on the cowl, a glove box lid and a boarding step. Its handling is unchanged to the bit.
  - Stand-up: a short, chunky hull with a closed white hood, the racer's own number on a plate and on the hood's shoulders, a splash decal and stripes in their colours, an open foot tray with a rubber mat, and a long thin handlepole hinged at the bow that rides up and down with the rider's hands. The rider stands in the tray, knees a little bent, and kneels in it at a standstill. It tops out lower (27.2 m/s against 30.3 on the same water), planes later (1.9 s from rest against 1.3), carves far tighter leaning back at full lock (a 5.4 m radius against 9.2), sits low and rocks until it planes, and throws its rider sooner in a slide or a crooked landing. A newcomer on the pad finishes the first course in about the sit-down's time (353 s against 323). Tricks of its own: a submarine and a pole stand on the water at speed, a flat spin and a superman in the air; the grid and the Controls page name them, and each rumbles the pad.
  - The rider wears goggles under a peaked helmet, and the wetsuit has its panel seams.
- Tests: jetski-standup measures the two craft from rest on open water in one input timeline a run (top speed, time to plane, turning circle, the stand-up's tricks), the stand-up rider's pose and the craft from the race camera, boots on the tray and gloves on the grips, the hull on the water, and a camera that never shakes, on GL and WebGPU, each with a control that must fail; with --baseline it holds the sit-down to the bits it raced before. jetski-craft-models reads both models: the sit-down's bow a beak (control: the old bow), the stand-up's hood and hull depth (control: a first, thin hull). jetski-newcomer can pick the stand-up on the menu and race the whole first course on either craft (--craft sit|stand|both --finish), reporting falls, time off the plane and sliding; with both, the stand-up must finish within 25 % of the sit-down. It runs again: a counter named like its input helper stopped every run before the race. The flash sweep runs a race of stand-ups.

## 0.32.2

- Sky Fighter: the jet painted. The airframe wears a two-tone scheme (a deep slate upper side over a light grey underside, split along the fuselage) with a fin flash and a tail number on each fin, wingtip bands, a dark anti-glare panel ahead of the windscreen and a matte radome; the panel lines of the model show in the paint, in its roughness and in a normal map that sinks them a little; the nozzles are dark steel with the straw, violet and blue heat bands of a hot exhaust toward the lip, glossier than the paint; the canopy is tinted glass with a pilot under it (a helmet with its visor over a flight suit). The bandits wear their own paint: dark umber over grey with red markings and a red nose band, so they read as the other side. The wreck sections carry the same surfaces. The paint is made by `games/skyfighter/tools/build-jet-livery.mjs` from the model as downloaded (in `assets/source-models/`), painted in object space over the model's own UV layout.
- Sky Fighter: the farmland under Country Fair is pasture and dry meadow in patches a few hundred metres across (a sixth terrain layer, the grass scan graded dry), so the land reads as fields from altitude instead of one green.
- Sky Fighter: a new cover (the painted jet pulling up over the fair, the title as two lines sized like the other covers'), icon, background and screenshots, rendered from the game through romdev.
- Sky Fighter builds against the engine the repository pins (v0.46.0); its program table carries the states the painted surfaces draw.

## 0.32.1

- Sky Fighter: a new cover rendered from the game through romdev: the jet pulling up and banking toward the camera over the fair, lit by the afternoon sun, a balloon and the fairground below it under a deep blue sky, a 720x1080 crop of the frame with the logo.
- Sky Fighter: the jet and the bandits fly with the landing gear up (the model carried it down on every sortie).
- Sky Fighter: a `keyart` debug command (the camera held at an offset from the jet along its heading, the HUD off, the sky's hour) for key art on a cart, where the SKY_ART_* environment does not reach.

## 0.32.0

- Engine v0.46.0 (from v0.44.2): the flash check and GPU timing run through romdev; a test's scripted finger taps on the frame it lands on a native run.
- No strobing, through romdev: the flash sweep runs every entry on the built cart through romdev on GL and WebGPU side by side, with the engine's `flash-check --host romdev`: one `frame({op:'flash'})` call a run (the server steps the frames, filters each in linear light and applies the WCAG 2.3.1 / Harding rules with the 8 Hz strobe control), 232 requests for 64 runs. Every entry passes on both backends, and Arena's co-op control (the old stand-ins) still fails as it must. What the sweep found, and what changed so the flashes swell and fade:
  - Arena: an enemy's rocket flying at a player grew from a dot to a white ball of glow filling that view and lit the wall beside it, three a second. A rocket near a view's eye now dims its light and shrinks its hot exhaust, a blast near one fires a quarter of its flash (the light, the flash sprite, the fire) at 3 m and all of it from 12 m, and a downed view sinks and turns at most 3.6 rad a second, easing in (it whipped from a wall to the open court in two frames).
  - Co-op Platformer: near the founder, speech bubbles popped in, hopped to a clear place and blinked out, 4 flashes in a second over a sixth of the screen. A bubble now swells in over a fifth of a second, glides to its new place, and fades out where it stood when it has to wait.
  - Horde: on a late floor three players firing fans of thick tracers into a crowd pulsed the middle of the screen at the fire rate, 5 flashes in a second and a regular 4.5 Hz flicker. A round is now a slim line of light grown from the muzzle that trails back about the distance between two volleys, with a small hot head, so a gun's volleys are a steady stream.
- Tests: the killer cam gate counts from 64 dead ticks, once the slower turn is done.

## 0.31.3

- Tests: the flash sweep runs on the built carts through romdev, GL and WebGPU, every frame of a stretch taken in one call. Arena's co-op stretch passes on both (it was marked as a known failure): the stand-ins that play the seats in the test build now turn the way a person on a pad does, easing the look stick in and out and staying on the enemy they fight, instead of swinging their views at full speed between two enemies and round every corner. The old stand-in is kept as a control that must still fail. The shipped games are unchanged.

## 0.31.2

- Tux Racer: a new Tux. The same skeleton and moves (the walk-in, the belly slide, the tuck, the jump, the tree hits), with a smooth sculpted body over them in place of the blended spheres of the original model: a fit, streamlined racer with a matte blue-black back, a clean white front up to the chin, a defined head with expressive eyes, flat flippers, and an orange-yellow beak, legs and webbed feet. His feet stand on the snow in the walk-in and his belly rides on it as he slides (he no longer hovers a few centimetres up or sinks his toes in). New cover, icon, background and screenshots with him in them.
- Tests: tuxracer-grounding holds his soles and belly on the snow on the cart, GL and WebGPU.

## 0.31.1

- Tux Racer: a new cover. Tux belly-slides down Bunny Hill toward you in the sun, big in the frame, with the snow spraying behind him and the pines along the slope, rendered from the game itself.

## 0.31.0

- Engine v0.44.2 (from v0.42.0). The menus take touch: a tap works the title, pause, settings and graphics pages, and a tap on the join screen seats you and starts. While a game loads, its first frames draw the shadows and lights play draws instead of leaving out the sunlit parts of the scene (and a red "no program" warning up for good). In split screen the volumetric fog reads as on.
- Every game carries its name for launchers and frontends: Arena, Street Toughs, Co-op Platformer, Crawler, Fighter, Horde, Jetski, Party Pack, Pirates, Sky Fighter and Tux Racer.
- New covers for Arena (a robot charging across the sunlit courtyard), Horde (a soldier firing up at the yard's boss) and Tux Racer (Tux on Bunny Hill), each rendered from the game itself.
- Tests: every game's cart surveyed as a player's machine runs it (no seed, the warm-up under the loading screen, every graphics preset and switch, GL and WebGPU) finds no missing program. jetski-air and jetski-hud-words run again, a few requests a ride instead of one a frame: Jet ski keeps each flight's measures and the words it puts up in its own debug fields. CI checks every cart's launcher art with the engine's check-art. A keyart command in the Arena and Horde autopilot builds, and Tux Racer's key art switches, reach a cart.

## 0.30.0

- Engine v0.42.0 (from v0.38.9, three.c v0.187.7). Terrain reads two cells for its tiling instead of three (Tux Racer, Jet ski and Sky Fighter draw their ground a little faster, with a slightly crisper surface), reflection probes capture at their own size and spread the work over frames, and a distortion no longer splits into rainbow colours.
- Arena FPS: grenades finish fights (full force close up, the bots throw them at a wounded target behind cover and side-step one they can see or hear), every bot goes hunting when nobody has scored for 20 s, a rules row on the setup panel (instagib, rockets only, low gravity, big heads), frag callouts and awards on the results. The sun's shadow covers the whole arena (half of each arena had none). Surfaces laid flush on each other no longer flicker between their two looks as the camera moves. In split screen the damage tint swells and fades at most once a second, and a body right against a view's camera is not drawn in that view.
- Brawler: the first stage is where a newcomer learns the game: its crowd hits softer and its bruiser goes down sooner. Bodies bounce off the edge of the picture and off props for two more blows in the air; the docks' crane drops cargo on a ringed spot and the roof's barrels roll down a marked lane; enemies keep to the street. The regular rowdy enemy is the Heckler. Every character rebuilt to human proportions: fit, athletic builds, legs that stay apart, heavy builds that read as muscle.
- Co-op platformer: five bosses, five fights (the Landlord, the Tax Golem, the Lifeguard, the Regional Manager, the Founder), each attack shown before it lands and a stomp in the tell calls it off, a second phase from half health, each boss in its own colour with a rim that keeps its outline in the caves. A wardrobe on the results card: coins buy hats and scarf colours (saves go to version 6 and carry versions 4 and 5 forward). A boss's words move beside it instead of covering it, and the results card rises whole from the foot of the screen.
- Crawler: thrown axes and hammers leave a ribbon along their flight, and the hero who finds the stair walks down it as the picture fades.
- Fighter: every fighter and the crowd rebuilt to human proportions, athletic builds; Grit is a muscular heavyweight with his bracers on his forearms.
- Horde shooter: the dash goes through the small ones and stops against the big ones; each gun pushes its own way; upgrades that combine (a fan of rounds, cluster rockets, a dash that bowls the small ones over), named on the card. A seat nobody has touched for 20 s holds nobody back: the horde is sized for the players who play, and an idle soldier is carried along instead of holding the camera and the way down.
- Jet ski: lines of floats strung out from each mark, riding the swell, so the course and your speed read close up.
- Party pack: the board forks at every row's end (a shortcut bridge picked in the shop), team rounds in every game and 1 vs 3 rounds for a clear leader, the 6 and 8 round boards whole. The board's words and SHORTCUT labels never overlap, at any text size, and the camera keeps every row and the END in view.
- Pirates: the wind shown and sailed by: flags stream downwind, sails fill on a reach and go slack and shiver into the wind, lines of wind run over the sea, smoke drifts with it, and the computer captains sail by it. Sails tear as a ship takes damage.
- Sky Fighter: PULL UP when the flight path meets the ground inside 3 s; flying into a balloon or blimp costs a third of the airframe instead of the sortie; clouds stay in the sky when the graphics step down to Very Low or Low in flight.
- Tux Racer: the Cup can be won on a first run: each race asks for a share of the herring inside a time limit (Bunny Hill 4 inside 1:00, Twisty Slope 2 inside 0:55, Bumpy Ride 3 inside 0:45), shown on the Cup's card and the HUD and named on the results when missed; Practice keeps the original's numbers. A Tricks mode in Practice (spins and flips paid on a straight landing). Herring are gold fish, the banners, title logo and HUD herring are redrawn sharp, lamps light the gates at night, and the aurora folds slowly. No more missing ground shadows after switching the graphics preset in a race.
- Tests: every game's gate switches reach a cart on romdev (read from the second frame), and the scripts that play through romdev decide every 100 to 300 ms and step the frames between in one request; newcomer gates for the brawler, horde shooter, Sky Fighter and Tux Racer that play from power-on by what the screen shows with a person's reaction time and aim error, beside an idle second pad where the game takes more than one.

## 0.29.1

- Engine v0.38.9 (from v0.38.8). The shell's Settings page has no Screen shake row: no camera shakes, and hits, landings and blasts are felt in the pads.
- Fighter: no strobing in a fast combo. Only one blow in 30 ticks lights up; the others throw a small spray of sparks with no light, at most one spray every 8 ticks, so a flurry no longer flickers. A hit's light swells up over about 5 frames and fades over about 20 instead of jumping on, and a heavy blow lights once instead of twice. Supers and the deciding knockout always light up, and a point-blank beam super still draws its beam, with a softer glow that no longer washes out the attacker. The flash sweep plays the beam super, the deciding knockout, flurries and a heavy blow every 8 ticks.
- Fighter: a soft first match. A session opens against an Easy computer that seldom guards, punishes nothing and gives time, and it steps up to Normal after the first win; in round 1 against a person, Easy and Normal drop a combo before its third hit.
- Fighter: hits look and feel their strength (a light hit is a small pale spray, a heavy one throws embers), and a knockdown spreads dust with a few dark grit chips that land. A super winds up with the camera moving in and the attacker's rim light in their colour, and a beam super starts at the attacker's hands. Strikes wind up, snap and follow through, hit reactions follow the height of the blow, each fighter has a second win pose and an idle rhythm of their own. The crowd answers as a ripple, the deciding knockout plays in slow motion, and a deciding round at low life gets tense music. The select screen has an arcade ladder and a random pick.
- Fighter: the rooftop floor is nine slabs a side, each with its own cast and sheen, some stained and a few cracked, with grime in the joints, so it no longer repeats at fighting distance. The billboard shows an advertisement instead of a flat lilac panel.
- Arena FPS: in team deathmatch the bots go looking for the other team after 12 s without an enemy in sight (the longest stretch without a kill on Foundry is 22 s, was 46). Grenades go off half a second after landing, hit harder close up, and the bots throw them more at a target that just went behind cover.
- Tests: `tests/arenafps-newcomer.mjs` (from power-on through a first deathmatch and back to the setup by what the screen shows, with a pause and resume), `tests/fighter-newcomer.mjs`, fighter-crowd and fighter-slowmo, each with a control that must fail. arenafps-killcam and arenafps-grounding find the autopilot from the build's own record of it and name a tree without that build; hordeshooter-grounding does the same, takes the GPU suite lock, and names a timeout or a signal.

## 0.29.0

- Engine v0.38.8 (from v0.38.3). Its explosion kit swells over a few frames instead of flashing on the first one.
- No strobing. `tests/flash-sweep.mjs` plays a stretch of each game's brightest play (natively, on GL) through the engine's flash check, which holds it to the photosensitive flash limits: at most three general or red flashes a second over a sixth of the screen, small lights switching together, no regular flicker above 3 Hz. Every entry runs a control, the same frames under an 8 Hz full-screen strobe, that must fail. AGENTS.md has a No strobing section. The flashes it found now rise over a few frames and fade over many, and any blink is at 2 Hz or slower:
  - Jet ski: Gale Bank's lightning strokes swell.
  - Co-op platformer: the boss's hurt glow and the invulnerable blink.
  - Arena FPS: the muzzle light follows the aim on an envelope, short of walls; the damage tint and the spawn shimmer are slow swells.
  - Horde shooter: hit glows; the flashlight beam turns after the aim at a limited rate; a held muzzle light stays steady through a burst; slimmer, dimmer tracers; the shield runs out in a 2 Hz blink.
  - Crawler: hit flashes and flash lights. Sky Fighter: the red damage tint swells. Fighter: the super and KO white wash swells and fades. Brawler's teleporter pulse, Party pack's fuse pulses and invulnerable blink and Tux racer's reset blink are 2 Hz or slower.
  - Known failures, waiting on an engine fix (marked in `tests/flash-sweep.json`): arenafps-match and arenafps-coop. In four-view split screen a moving omni light (a muzzle light, an explosion kit light) switches its lit area on and off frame to frame.
- Arena FPS: a killer cam (the downed view turns to whoever downed you and follows them, with their weapon, distance and health left); grenades that arc, bounce and go off 0.8 s after landing, and a melee bash, both used and dodged by the bots, who lead a grenade at where the target will be; a power-up in each arena's middle every 60 s (quad damage, haste, regeneration), its holder glowing its colour.
- Arena FPS: the fighters are rebuilt as soldiers (human proportions, helmet, visor, plates over a matte suit, pouches and pack, guards and boots, a detailed rifle) on the same rig. Skylines past the walls: a hill town on the slopes round the Courtyard's valley, a dusk city of dark blocks and towers with lit windows round the Terraces, hazed toward the sky's horizon colour with distance. The setup rows are all in view, a line says when Auto steps the graphics down, and the title drifts through the arena.
- Brawler: character select before stage 1 (a seat nobody touches is counted down and left out); a lower, closer camera over a shallower street with lane assist; heavier hits (longer hit-stop, a squash and a body thud); a dash, running kick, backfist, a launcher that chains into a juggle, a stomp, and the special's cost won back by landing blows. Droplet blood, darker stains, staggered entrances, a last-hit bar, a grade card and the title's fighters either side of the menu. The first stage shows what the buttons do for its first 15 s. Fog on the Foggy Docks only, as plain height fog, and shadow cascades fitted to the street camera, keeping every stage in its GPU budget.
- Co-op platformer: pits that read as holes; heroes, hats and enemies stand on soft contact blobs instead of throwing shadows on the hills behind; each hero has a rim light in its own colour; the hint line's halves retire once learned; the results card counts each player's coins, stomps, throws and falls and names a winner of each. Four new creatures (a hopper, a dropper on a thread, a shell a stomp shuts and a touch kicks, a slider with a spiked front). Three big coins a level, worth five, shown under the ability chips, on the results card and the map. The frozen head office is furnished. Saves go to version 5: a version 4 save carries forward with its settings, unlocked levels and progress (big coins empty).
- Horde shooter: a dash (4.5 m in 0.2 s, untouchable through it, a recharge ring round the soldier); gun feel (fat tracers, hit flares, recoil, a stagger pose, a sound per gun family and on every kill); a flashlight cone ahead of each gun at every preset and a spot light from Medium; upgrade choices as three cards in the player's corner (the yard slows to 30% while they are up, alone); a contact shadow round every prop and ambient occlusion from Medium; stains and scorches stay on the floor; a score multiplier up to x5; a boss banner and name over its bar. The end screen sits on a dark panel sized to its lines, with a short form when four players or large text would not fit.
- Jet ski: sound (wind, hull slaps, rival engines placed with doppler, a sound per race event, a music loop per course); speed feel (the view widens with speed and leans into carves, speed lines near top speed and under a turbo); a course card on the grid and a rocket start; a finish whose camera rides round the craft while the rivals race on for real times; spray lit by a low sun, foam collars at ramp legs and buoys, cloth that darkens when wet and clear-coated helmet and deck from Medium.
- Party pack: every mini-game is open on a fresh save, and party points unlock hats each person picks. The tournament board has decisions: coins by place, a roll with a place bonus, a shop before anyone moves, a star that costs 10 coins and moves on when bought, coin, take and swap spaces; every third round from the second is 2 vs 2; three awards at the end. Sumo Push, Coin Rush, Tile Drop and Stack Attack keep their 2D rules on 3D sets. A ring-out or a fall plays at half speed for a moment with a splash and a crowd's ooh. Loop Racers' camera follows the pack, Hurdle Dash's runners are larger, and every seat's character has a rim light in its colour.
- Pirates: the island's and the harbors' light is baked (soft palm, hut and hill shadows, dark coves, light bounced off the sand); Very Low and Low lay a blob shadow under each ship. A fair first minute: in a Cooperative Voyage the patrol and the merchant leave a captain alone until it has banked once or fired first, and its harbor keeps it safe. A wind on each map (fastest on the beam, slowest into it; the ash drifts downwind; rougher patches of chop). Cannonballs fly in arcs, with a ring on the water each side where a broadside would land. A hit ship sits lower and lists to the side it was hit, and trails smoke below half its hull. Labels and damage numbers never stack.
- Sky Fighter: a kill swells into a fireball with a white-hot core, a rolling smoke column, a heat ripple and burning pieces that fall trailing smoke, and TARGET DESTROYED is bigger. Bandits trail vapour with a hot exhaust and a warm edge (gold on the ace) and come in pairs or threes; five kills in Coastal Scramble bring in a second flight. A damaged jet or bandit smokes. Very Low and Low draw soft sunlit cloud billboards where the volumes stand. Every fourth round is a thick tracer; the objective cue points at the next target ahead; Steel Tide flies at dusk, and a low pass over open water throws spray for a bonus.
- Tux racer: a ghost of the best run, medal times, a speed camera option, herring chains and a gold herring, low spray, a sunnier contrast, a finish moment; the aurora folds, the boughs carry snow, and the gauge is drawn as shapes.
- Tests: arenafps-killcam, brawler moves and lane gates, coopplatformer-shadows, -rim, -hint, -tally and -save (a stored version 4 save), hordeshooter-grounding and -melee-dash, jetski-finish, -sound-life, -trick-mash, -newcomer and -flash, partypack's shop and unlocks in its idle-seat gate, pirates-first-minute, -labels and -arcs, skyfighter-feel; each with a control that must fail.

## 0.28.0

- Sky Fighter: nothing floats in any mission.
  - A building stands only where the ground under its whole footprint (corners, centre and the ground between) lies within 2.5 m, on the lowest point. Coastal Scramble's town hung blocks off the cliff in the first view, because the slope was only tested along one axis at the centre.
  - Time Warp's alpine village (every one of its 170 houses floated) stands on the valley sides on stone basements that reach into the ground.
  - Barns, silos, farmhouses, workshops, the palace, ministries, churches, the bell tower, the fortress walls, the North Pole, the elf gun crews (their twin barrels now rest on a mount), the presents and the candy canes stand on the lowest ground under them, on a plinth where the ground falls away. Trees stand on the lowest ground under their trunks. A destroyed building's ruin reaches into the ground on a slope. The far city blocks stand on the distant land as it is drawn.
  - Steel Tide's grey slabs, 5 m above the sea, are barges riding in it, and the rigs' legs reach below the wave troughs.
  - Red Fortress's bridges have embankments from the deck ends down to the banks.
- Crawler: a corridor is a line to mow. Light monsters die to one shot at low strength and a kill with damage to spare goes on through the line; deep floors are harder by numbers and spawners. A tileset's first floor is a breather, and a team starting there gets an elixir a player for each tileset passed by and two potions. Each floor ends on a tally card (kills, treasure, food, keys, the best of each). Heroes call out low health, shot food, a theft and more over their heads. The floor banner sits in the band where no hero stands. The rock round the maze is stone in the tileset's colour, the abyss's warm monsters have white-hot edges, the cathedral floor is darker and the Ranger's arrow sheds a streak.
- Tests: `tests/skyfighter-grounding.mjs` checks every prop of every mission against the ground, the sea's wave troughs or a prop that carries it (native build, software GL); its control, the old district placement, must fail in Coastal Scramble and Steel Tide. `tests/crawler-mow.mjs` and `tests/crawler-banner.mjs`, each with a control that must fail.

## 0.27.7

- A second pad that joins and is then put down no longer holds up the player who plays:
  - Fighter: once one seat locks in, the other gets 12 s (the last 6 counted down on screen); then the computer plays it. Before, the select screen waited for ever.
  - Party pack: once one person is ready on a mini-game's instructions card, the card waits 10 s for the rest (a countdown by each seat still out), then starts with everyone. Before, the card waited for ever, and its Start meant ready, so the player could not even pause.
  - Co-op platformer: a seat whose pad is untouched for 5 s holds nobody back. The camera and the bubble rescue follow the players who play, the view glides off the idle one, and it is carried to the group in a bubble. Before, the player was floated back to the idle seat at the start every time, short of level 1's first flag.
  - Pirates: in a shared voyage, a captain whose controller is left alone for 20 s while another person sails goes on autopilot ("P2 AUTOPILOT"), and any of its controls takes the helm back. Before, the idle captain raised the quota and the patrol for the one person playing.
  - Arena FPS: in co-op, a seat untouched for 20 s stays down once downed ("press any button to come back in"), spends no shared lives and does not grow the waves.
- Arena FPS: the program for bullet holes on walls in shadow was missing from the cart, which put a red error banner over the timer two minutes into the first deathmatch and left those holes undrawn.
- Tests: newcomer and idle-seat gates, each the built cart through romdev on GL and WebGPU with a control that must fail: `tests/fighter-idle-seat.mjs`, `tests/partypack-idle-seat.mjs`, `tests/coopplatformer-idle-seat.mjs`, `tests/pirates-idle-seat.mjs`, `tests/arenafps-idle-seat.mjs`. Each of these and the crawler gates tears its romdev host down when it exits. AGENTS.md says every new game needs a newcomer gate and an idle-seat gate.

## 0.27.6

- Engine v0.38.3 (from v0.38.1, and v0.36.1 before 0.27.5). The engine's grounding check judges a wall or a row of stands at its foot nearest the camera and leaves out what stands around the camera, instead of the middle of its box; cascaded sun shadows skip their lookups where no cascade weighs in (frames the same to the bit); a terrain obeys visible. From v0.37.0 and v0.38.0 the 2D renderer gains an HDR canvas, a post chain, soft shadows and global illumination, which the games do not turn on.
- Every game has a program for everything it draws at every preset on GL and WebGPU with the new engine, and the quality range gates pass with the limits of 0.27.2.

## 0.27.5

- Engine v0.38.1: every cart's manifest says which version it is. A release's carts carry the release's version (every cart said 0.1.0 before), and a cart built from any other commit says `<last tag>-dev.g<commit>`, so it is never taken for a release. The engine works the version out from the checkout on every build; `project.cwproj` no longer carries one. Engine v0.37.0 and v0.38.0 (the 2D renderer's HDR canvas, post chain, tiled lights and global illumination) come with it.
- A `v*` tag builds its own carts (it took `main`'s before), and the release checks that every cart's manifest carries the tag's version (`tests/cart-version.mjs`, which every build runs too; its control, a cart rewritten to another version, must fail).

## 0.27.4

- Crawler: the way down reads from the overhead camera on every preset. A shut door's gate now fills its tile with a
  glowing key on its top and warm light either side (from above it read as a low rail); a key is drawn half as big
  again in a strong pool of light; every way down has EXIT over it (DOWN n for a stair that skips floors) in a ring of
  light, with a wide pool of light round the hatch. After 20 seconds on a floor an arrow by the leading hero points the
  way: to the exit, or to the nearest key while a shut door is in the way and nobody holds one.
- Tests: tests/crawler-find-the-way.mjs (the cart through romdev, GL and WebGPU) stands a player on floor 1 until the
  arrow appears, then follows only the arrow to the key, through the door and down to floor 2; its control
  (`sw guide_off 1`) leaves the player on floor 1 with nothing to follow.

## 0.27.3

- Crawler: a seat nobody plays no longer pins the team at the start. A second pad that joined and was then put
  down held the player within one screen of its idle hero, so floor 1's key and exit (more than a screen from the
  start) could not be reached. A hero whose pad has not been touched for five seconds now holds nobody back: the
  view glides over two seconds to the players who play, and the idle hero is carried along beside them when it
  would leave the screen. When nobody plays, everyone counts as before.
- Tests: tests/crawler-left-behind.mjs clears floor 1 on the cart through romdev with the stick, on GL and WebGPU,
  alone and beside an idle seat (the view never jerks), with a control (`sw idle_anchor 1`, the old rule) that must
  stay on floor 1; the crawler level check in CI clears floor 1 with one bot and one or three idle seats, with a
  control that must fail.

## 0.27.2

- Engine v0.36.1: on WebGPU a game's GPU time now includes its reflection probe and sky captures (they were untimed), every frame is timed, and the range gates count a frame as the sum of each pass's average. Arena FPS's WebGPU High reads about 10 ms where it read 9.7.
- The fighter: Low and Very Low light the stage with at most 4 of its omni lights: the rims behind the fighters, then the stage's strongest, without the explosion flashes. On the select screen the portrait's key and fill take the stage lights' places. Medium and up light every one, as before.
- GPU time on Very Low and Low as a share of High (1080p, Radeon 890M, GL / WebGPU, measured on a quiet machine): Pirates 0.22 / 0.20 and 0.37 / 0.33; Jetski 0.18 / 0.18 and 0.34 / 0.34; Arena FPS 0.23 / 0.22 and 0.31 / 0.30; Tux Racer 0.35 / 0.38 and 0.43 / 0.48; Sky Fighter 0.31 / 0.35 and 0.52 / 0.57; the co-op platformer 0.38 / 0.35 and 0.49 / 0.46; the fighter 0.45 / 0.45 and 0.57 / 0.59; the brawler 0.62 / 0.56 and 0.78 / 0.74; the horde shooter 0.51 / 0.50 and 0.65 / 0.63; crawler 0.58 / 0.53 and 0.69 / 0.65; the party pack 1.01 and 1.00 on GL. The range gates hold each game to these shares plus 0.06 (Pirates, Jetski and Arena FPS to Very Low 0.3 and Low 0.45 of High).

## 0.27.1

- The game's HUD goes away under the pause menu, its settings and the join screen in every game on the
  game shell (Arena FPS, the brawler, the co-op platformer, the crawler, the fighter, the horde shooter,
  Jetski and the party pack), so the menu stands alone at large text (the engine's cw_shell_hud_shown).
  The party pack keeps its mini-game in view and hides only its panels and banners.
- Party pack: its text reaches the full 200% size on its own stage (an engine fix since v0.35.0), and
  the screens are laid out for it: the tournament-over podium labels keep below the heading and stack
  instead of running into each other, and the results heading no longer sits on the score strip.
- Party pack: Blast Ring draws everything once Auto steps down to Very Low in the middle of a round
  (three programs were missing from its table).
- Co-op platformer: a sign's caption never sits on a player marker, a boss's words and what players
  say never stack (words wait for a clear place), and the ability chips under the player panels keep
  the level name and the markers below them; the results card hides them instead of cutting them.
- Tests: coopplatformer-words and coopplatformer-words-boss (four players at 200% text, each with a
  control that must fail); the party pack's tournament-over screen in its text check;
  skyfighter-settings checks the native save holds the current graphics word and that the row wraps
  from Ultra to Very Low (it still expected the four-preset values); coopplatformer-signs counts the
  frames its caption now waits for a marker to move.

## 0.27.0

- Engine v0.36.0: Low and Very Low cost less GPU time with the same picture or close to it. Both draw the
  scene into a smaller HDR target (half the bytes a pixel), and Low runs the fog, the edge smoothing and
  bloom inside one final pass, with a lighter first bloom level. Low draws no volumetric fog or heat haze;
  a game whose fog is volumetric (Arena FPS, Sky Fighter, the co-op platformer) now draws it as plain
  height fog on Low and Very Low, where Very Low drew none before.
- Arena FPS: Low and Very Low draw the arena after a depth pass (each pixel of its stacked halls, roofs
  and girders is shaded once) and filter its textures less steeply at grazing angles (4x on Low, 2x on
  Very Low, 8x from Medium up).
- Tux Racer: Low draws the Very Low snow terrain tier.
- GPU time on Very Low and Low as a share of High (1080p, Radeon 890M, GL / WebGPU), before -> after:
  Pirates 0.22 / 0.27 -> 0.22 / 0.23 and 0.39 / 0.47 -> 0.37 / 0.36; Jetski 0.26 / 0.21 -> 0.19 / 0.23
  and 0.42 / 0.51 -> 0.34 / 0.42; Arena FPS 0.48 / 0.42 -> 0.23 / 0.24 and 0.62 / 0.59 -> 0.31 / 0.31;
  Tux Racer 0.39 / 0.45 -> 0.35 / 0.38 and 0.60 / 0.77 -> 0.43 / 0.45; Sky Fighter 0.34 / 0.41 -> 0.32 /
  0.37 and 0.68 / 0.77 -> 0.54 / 0.65; the co-op platformer 0.37 / 0.34 -> 0.37 / 0.33 and 0.65 / 0.57 ->
  0.50 / 0.43; the fighter 0.54 / 0.56 -> 0.48 / 0.51 and 0.73 / 0.76 -> 0.63 / 0.65; the brawler 0.66 /
  0.63 -> 0.58 / 0.55 and 0.88 / 0.88 -> 0.75 / 0.76; the horde shooter 0.69 / 0.72 -> 0.50 / 0.50 and
  0.95 / 1.00 -> 0.63 / 0.60; crawler 0.67 / 0.66 -> 0.60 / 0.54 and 0.92 / 0.89 -> 0.67 / 0.59; the party
  pack 1.00 -> 0.99 on GL (its cost is its 2D drawing, which no preset changes).
- The range gates hold each game to these shares (Pirates, Jetski and Arena FPS to Very Low 0.3 and Low
  0.45 of High); the party pack is measured on GL only, since WebGPU does not time its 2D pass.

## 0.26.1

- Engine v0.34.3: control hints for movement show the d-pad, with the arms to press solid, wherever the d-pad and
  the left stick both move: moving in Arena FPS, the horde shooter, the brawler, the crawler, the co-op platformer,
  the fighter and every party pack mini-game, sailing in Pirates, flying in Sky Fighter, steering and leaning in
  Jetski, and the menus. A single direction (Jetski's lean back) lights its one arm. Actions only the right stick
  does, looking in Arena FPS and Sky Fighter, still show the stick.
- The graphics page keeps every row on screen at 200% text: its names and values are laid out from their measured
  widths.

## 0.26.0

- Engine v0.34.1: text you can read on a small screen. Every game's Settings now has Text size (100%,
  125%, 150%, 200%) and Text background (Off, Outline, Panel: a dark outline or a dark panel behind
  text over the scene). Both are saved. Shell games get the rows from the game shell; Sky Fighter and
  Tux Racer add them to their own settings (Tux Racer gains a Settings page), Jetski to its Audio and
  text page, and Pirates sets both from its title and pause screens with a button each.
- Every game's text meets one floor at 100%: body text (HUD values, menus, hints, results) at least
  26 px at 1080 lines (2.4% of the screen height), nothing anywhere under 18 px (1.7%). Title logos
  drawn as text are marked as art and left as they are.
- Every HUD, menu and results screen is laid out from measured text, so at 200% nothing overlaps or
  runs off screen; long menus and lists scroll with the selection, and wide hint lines break into lines.
- Each game has `tests/<game>-text.json`, its screens for the engine's `tools/check-text.mjs`, checked at
  every text size at 1920x1080 and 1280x800. Every gate now fails a run that draws text under the floor.
- Pirates: the ship, harbor and captain panel labels (17 to 22 px at 1080 lines before) are body size
  now; the HUD lays out from measured text, labels keep clear of the top bar and the captain panels,
  and the world labels step aside under the pause and results boxes.
- Sky Fighter: the HUD (speed and altitude blocks, warnings, status panel, notices) is stacked by
  measured height; the title tiles, controls table and settings are measured and scroll when they do not
  fit; small labels (11 px on its 540-line canvas) are at least 13.
- Jetski: the menus scroll with their selection, the Controls page puts a control under its label when
  both do not fit, and the results table is measured; the hint line is larger.
- Party pack: every mini-game's HUD, intro card and results grow with the text; the free play picker
  (whose last row was off screen) and the trophy room scroll.
- Tux Racer, Arena FPS, the brawler, the co-op platformer, crawler, the fighter and the horde shooter:
  HUD values, menus and results at the body size and laid out for 200%, split-screen cards included.

## 0.25.0

- Engine v0.33.1: Very Low draws every lit surface with a cheaper lighting model (the same colours,
  textures and normal maps, a simpler highlight, no reflections sampled), runs the fog and edge
  smoothing inside one final pass, and reads the ground textures once. Every game regenerated its
  program tables for it and passes its quality sweep on Very Low with nothing missing.
- Tux Racer: the Terrain setting gains a Very Low value (the cheaper ground), picked by the Very Low
  preset. Its Ultra value is gone (the Ultra preset already drew High's terrain).
- GPU time on Very Low against High (1080p, Radeon 890M, GL): Pirates 1.4 against 5.7 ms, Jetski
  1.5 against 7.5 ms, Sky Fighter 2.1 against 6.6 ms, the co-op platformer 1.6 against 4.2 ms, Arena
  FPS 3.8 against 9.9 ms, Tux Racer 1.9 against 4.7 ms, the fighter 3.2 against 5.9 ms, the brawler
  2.0 against 3.2 ms, crawler 1.7 against 2.5 ms, the horde shooter 1.3 against 1.9 ms.
- The range gates hold each game's Very Low share of High to these numbers.

## 0.24.0

- New game: crawler, a co-op maze dungeon crawler for one to four players on one shared top-down
  screen. 120 floors in six tilesets (the crypt, the sewers, the foundry, the grotto, the cathedral and the abyss),
  each a maze with spawners pouring monsters out, locked doors and their keys, food, potions, treasure and a stair
  down; health drains all the time and food gives it back. Four heroes (the Brute, the Warden, the Mage and the
  Ranger), twelve kinds of monster, sixteen charms and six elixirs. The floors are generated in bulk and checked in
  CI: every exit reachable with the keys on the way, nothing in a wall, every prop grounded, every floor's light
  baked and current, and every floor cleared by bots. The spawners keep pouring, but one screen holds at most 120
  monsters, so a corridor reads as a line to shoot through.
- Crawler ships with its launcher art, HUD and menu text of at least 26 px at 1080p (2.4% of the screen
  height), button prompts on every control hint and the Very Low preset.

## 0.23.0

- Every game carries its own launcher art (engine v0.32.0, wasmcart 0.38.0): an icon, a cover
  with the game's name, a background, a logo, the title screen and three to five labelled
  screenshots of real play, with a one or two sentence description and the author, packed under
  `art/` in each `.wasc`. A launcher that knows the manifest's `art` shows them without running
  the cart; a host that does not runs the cart as before. The pictures live in
  `games/<game>/art`, named in its `art.json`, and `tests/cart-art.mjs` checks every release cart
  carries them.
- Tux Racer: a race started on a course directly draws Tux's shadow again (a program state was
  missing from its table).

## 0.22.0

- Engine v0.31.0: every game has a Very Low graphics preset below Low, for handhelds and older
  laptops. It keeps the full resolution and gives up what costs most: shadow maps shrink to the sun
  alone with small objects left out (characters and props keep their shadows), no multisampling,
  bloom, ambient occlusion, volumetric fog, reflections or distortion, fewer particles and less
  grass, and a cheaper water surface that keeps its shore foam and wakes. Low and Medium also drop
  the 4x multisampling now. Auto (the default in Pirates and Arena FPS) walks down one setting at a
  time and jumps straight to the level a slow device needs within a few seconds of play.
- The resolution setting is its own choice: picking a preset no longer resets it.
- Pirates: on Very Low the GPU time is about a quarter of High's (1.5 ms against 5.5 ms on a
  Radeon 890M at 1080p) and the cart's CPU time per frame about 40% (1.2 ms against 3.1 ms):
  palms, plants and rocks draw at a coarser level of detail (new Scenery detail setting), the
  island and ports cast no shadow, and the sea uses the cheaper surface. The engine now handles the
  multisampling Pirates used to switch itself.
- Measured on Very Low against High (GPU time, 1080p, Radeon 890M, GL): Jetski 2.4 against 7.5 ms,
  Arena FPS 4.8 against 9.7 ms, the co-op platformer 2.5 against 5.1 ms, Tux Racer 2.3 against
  4.7 ms, Sky Fighter 3.3 against 6.6 ms.
- tuxracer keeps the larger graphics settings word in its save; older saves still load.

## 0.21.2

- Pirates: graphics quality is automatic. On a device that cannot hold the frame rate the game
  steps down from High to Medium (no 4x multisampling, a half-size water grid, no water
  refraction) and then Low (also no island or port shadows), and remembers the level it reached.
  Medium takes about half of High's GPU time and Low about 40%; a desktop that holds the frame
  rate keeps High.

## 0.21.1

- Engine v0.30.1: the analog stick prompt is a picture of a thumbstick, with the other stick shown
  smaller beside it, instead of a ring and dot that read as an eye. Single-letter keys are drawn
  larger so they read in small hint text.

## 0.21.0

- Every game shows its controls with the engine's button prompts (engine v0.30.0). On a pad a
  four-dot diamond marks the face button to press, with shoulder, trigger and stick pictures for
  the rest; on a keyboard it shows the key. Prompts follow whichever device each player last
  used and follow rebinding, so the hint always names the button that does the job.
- partypack: Beat Box lanes are the west, south and east face buttons, left to right. Quick Quiz
  answers sit in a diamond that matches the face buttons, and Echo's pads show their buttons.
- pirates: each broadside has its own button, so the hint shows which one fires left and which
  fires right. Pads without analog triggers fire the left side on the left shoulder.
- pirates: larger hint text, a taller cannon bar, and the harbor panel sits higher.
- skyfighter: flares also fire on Back, for pads without triggers. Keyboard players see their roll
  keys; stick look is marked as pad only.
- tuxracer: the cup screen and race end show their buttons as prompts.
- The game READMEs name buttons by position instead of by letter.

## 0.20.2

- pirates: shore stones are lumpier and lopsided, sit deeper in the sand, and are lighter so they
  stay readable in palm shade.

## 0.20.1

- pirates: shore rocks are weathered grey and tan stone in three shapes instead of smooth dark
  boulders, and each one leans with the beach so its whole base sits in the sand.

## 0.20.0

- pirates: a new game. Sail with one to four players on one screen: collect cargo, bank it and
  repair at your harbor, buy ship upgrades you can see on the hull, and trade broadsides with
  rivals. Play solo or together against computer captains, or race each other for treasure, free
  for all or in teams, on a volcanic island chain or a calmer palm lagoon.

## 0.19.0

- jetski: two bigger ramps each on Crater Atoll, Cold Sound, Iron Quay and Gale Bank, good for
  about 40% more air than a normal ramp. Rival riders take them too.
- jetski: flips. Press X in the air with the stick back for a back flip or forward for a front
  flip; the camera swings to the side to show it. A flip that isn't finished throws the rider.
- jetski: a kick out on Y (key I) while in the air.
- jetski: hold the stick forward in the air to point the nose down. Going in nose first takes
  the craft under the water for a moment and back up with most of its speed. Landing tail first
  or flat is a wipeout.
- jetski: trick words from the last race no longer show at the start of the next one.
- jetski: the distance to the next mark moves clear of the countdown and other words on screen.
- jetski: the underside of the craft is lighter, with a red keel, so it reads in a flip.
- jetski: rival riders keep the nose down through long jumps instead of landing tail first.
- jetski: more shrubs on Crater Atoll and more rocks and plants on Red Reach's banks.

## 0.18.3

- coopplatformer: signs are a fixed caption that fades across when a player reaches them,
  instead of a panel that jumped around to avoid players.
- coopplatformer: the camera stops at a level's end wall, so a boss arena no longer shows half
  a screen of wall.
- coopplatformer: bosses stay clear of the walls, so their arms don't go through them, and you
  get a little more room behind them.
- coopplatformer: a boss's words are a caption, clear of the player markers, and speech bubbles
  stay off markers and players.
- coopplatformer: the results card and fake finish banner are solid, with no labels showing
  through.

## 0.18.2

- coopplatformer: a sign's panel stays solid and steps aside from a player, instead of going
  see-through on top of them.
- coopplatformer: the results card keeps player portraits and a long closing line off the line
  below them.

## 0.18.1

- All games: cartwheel-engine v0.29.1. A paused scene holds still, including the water, and
  steep terrain walls no longer catch bright shards of light at a grazing angle.
- Water near the shore no longer shows dark slivers where its refraction crossed the shoreline.

## 0.18.0

- jetski: a hard hit from another craft at speed can put one rider or both in the water. A rider
  taken on the flank goes down more often than the one who hit them, and a side-by-side rub
  never does.
- jetski: a race reached from the results page starts right behind your craft. The camera used
  to fly in from the last course, and could show a frame from under the sea.

## 0.17.0

- coopplatformer: saves from earlier versions start over.
- coopplatformer: all five worlds, 25 levels, and the old single level is gone. Each level
  ends its own way: a chase, a piano recital, a collapsing bridge, a corridor of presses, a
  cannon relay.
- coopplatformer: grab and throw a friend with the left shoulder.
- coopplatformer: coins that bite, signs that explode and a fake finish.
- coopplatformer: graves that get ruder as they pile up, and an incident report at the end of
  each level.
- coopplatformer: bosses that differ by colour, hat and lines, and three that behave
  differently. All the secrets in a level earn a terrible hat.
- coopplatformer: a falling player stays in view from above the lip of a pit, and pits have a
  dark back wall.
- coopplatformer: new ground with sediment beds and buried junk, a near layer in front of the
  action, and icicles.
- coopplatformer: coins climb a scale as you collect them, and a jump off something moving
  keeps its momentum.

## 0.16.0

- jetski: turbo you spend. Right shoulder or L fires it, and past full power a buoy or trick
  charges up to three shots.
- jetski: the pack races you: rivals close up and ease off, and don't wait for a stopped rider.
- jetski: fair landings. A hop off the sea keeps its speed, and nose dives still cost.
- jetski: real crashes. A glance slides off and a square hit is a wipeout, and you're moving
  again in about 1.6 seconds, with sound, rumble and spray.
- jetski: your place holds steady instead of flickering, with your neighbours' names and gaps
  under it. Rivals still out at the finish get an estimated time.
- jetski: every action now answers with at least two of a popup, a sound, rumble or spray.
- jetski: no more lime patches on the water on WebGPU.
- jetski: the cliffs on Cold Sound and Red Reach read as rock faces from a distance, Gale
  Bank's skerries are steep rock, and Crater Atoll's walls lose a comb of regular teeth.

## 0.15.1

- jetski: Crater Atoll's cliffs read as rock faces from a distance: spurs, gullies, ledges and
  uneven rock beds, with new rock textures. The other four courses follow in later releases.
- jetski: all five courses lose a grid of straight seams on their steep ground.

## 0.15.0

- coopplatformer: hold West to run and press South to jump, so a running jump goes more than
  twice as far as a standing one. Dash moves to the right shoulder. On a keyboard, Shift or X
  runs and C or F dashes.
- coopplatformer: touch screens get controls: a stick, A, RUN and DASH.
- coopplatformer: world 1, five long levels with a boss, signs and level names. The old level
  follows it for now. Worlds 2 to 5 come in later releases.
- coopplatformer: music, original tracks for the title, world 1 and bosses.
- coopplatformer: bad days. Saws, presses and falling pianos are always a gory gag, lava,
  spikes and enemies sometimes, with blood, bones, stains and a remark from a friend. Land on
  a friend's head and you flatten them.
- coopplatformer: saves from earlier versions start over.

## 0.14.0

- jetski: a minimap, bottom left, with no panel behind it: the course, your rivals, the buoys
  with the next one ringed, and you as an arrow.
- jetski: every ramp can be reached between one buoy and the next. Ramps sit on the line or
  about 20 m off it, and the shortcuts and three-ramp clusters are gone.
- jetski: a small hop off a wave no longer costs speed when you ask nothing of the craft.

## 0.13.2

- skyfighter: the settings menu reaches Ultra graphics. Left and right cycled through only
  Low, Medium and High.

## 0.13.1

- coopplatformer: coins stand up and spin face on. They lay flat and turned edge on before.
- coopplatformer: every coin and room can be reached. Each mound room has a wooden hatch in
  its roof (Down + Jump in, jump and air jump out), so its coins no longer need the dash,
  which you find long after you see those rooms. The coin stuck in the cave wall is gone.

## 0.13.0

- jetski: three new courses, five in all: Red Reach, a river in a red canyon whose current
  carries you; Iron Quay, a port at night with a tanker under way; Gale Bank, an open-sea
  squall whose swell builds through the race.
- jetski: cliffs show less of the rock texture's repeat (engine v0.29.0).
- jetski: buoys you can find: tall masts with arrow boards, an on-screen pointer to the next
  buoy, and a WRONG WAY warning.
- jetski: six different riders, and a craft slides along a ship's hull instead of pinning.
- All games: engine v0.29.0. Every game has a gate that every graphics preset draws
  everything.

## 0.12.8

- Street Toughs: the heckler has a real beer gut: round, full from the ribs down to a fold
  over his belt, with his T-shirt draped over it, a belly that bounces, and a cap that
  no longer hides his eyes.
- Street Toughs: the men's tops are T-shirts with the sleeves cut off, not straps.
- Street Toughs: the creep wears dirty white briefs and dark socks on bare hairy legs. When
  he flashes, his coat opens with his arms and he turns to show you.

## 0.12.7

- partypack: at Low graphics, Blast Ring keeps its arena and players for the whole round.
  0.12.6 fixed only the first part of a round.
- jetski: pines and palms use simpler far versions past 140 m, about 400,000 fewer
  triangles a frame where a course's trees are in view.

## 0.12.6

- jetski: each course now has 12 ramps, with a cluster of three side by side where the
  course opens up and two shortcut ramps well off the racing line, aimed at the correct
  side of the next buoy.
- jetski: computer riders miss fewer buoys around the ramps on Cold Sound.
- fighter: at Low graphics, fighters and the crowd no longer lose their skin and heads.
- partypack: at Low graphics, Blast Ring no longer loses its arena and all four players.

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
  gore on the street and the fighters, a softer hit flash, the heckler out of
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
  longer reads like Arthur. The trucker is now the heckler.

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
  (the heckler in his red "Dumb" cap, the creep in the trench coat who enjoys
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
