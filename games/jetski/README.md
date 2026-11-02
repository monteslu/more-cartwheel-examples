# jetski

A wave racing game in five places, each with its own water, land and light. Six riders race three laps. The water is the
track: it moves, its swells throw you into the air, and on each course it changes as the race goes on.

Status: five courses, one craft, a computer racer field, one player for now. The menu offers a championship (points by place over
every course in turn) and a time trial (you alone on the course you choose, keeping your best time). Difficulty
makes the sea bigger and adds buoys: every second one on Easy, three in four on Normal, all of them on Hard.

## How to ride

| Pad | Keys | |
|---|---|---|
| Left stick, d-pad | Arrows, A / D | Steer, and lean the rider into the turn |
| Stick down | Down, S | Lean back: a tighter carve that costs speed. Throw it forward at a crest to jump higher |
| A, right trigger | Space, J | Throttle |
| X | K | In the air: a barrel roll, a back flip with the stick pulled back, a front flip with it pushed forward |
| Y | I | In the air: kick a foot out |
| Right shoulder | L | Fire a turbo, when you have one |

Buoys mark the course: tall masts over the swell, each with a board whose arrow points the way past it. Pass each one on the side its colour and the arrow show: orange-red on your right, yellow on your
left. The next one stands larger than the rest, and an arrow on screen sits over it with the distance, or at the screen's edge pointing to it when it is out of sight. Ride back along the course and the game says so. Each one passed lights an arrow and raises your top speed, five at most. Miss one and they all go out.
At full power every clean buoy and every trick charges a turbo, up to three in hand, shown by the chips over the arrows. Five misses end your race, and the dots under the lap count show how many you have. So do five seconds away from the course,
with a warning counting them down. Too much stick scrubs speed, and a long hard slide throws you off.
Big steel ramps stand on piles along each course, some on the racing line and some off it. They do not move: the sea comes and goes on their legs, the swell is held down around them, and the same approach gives the same jump, well over two seconds in the air, long enough for a trick. Each press of the trick button is one trick; land mid-trick and you are off the craft. From the side or behind a ramp is a wall.
Four of the courses have a bigger ramp or two for far more air, close to three seconds. Push the stick forward in the air and the nose follows your line down: come down steep like that and you pierce the water, go under for a moment and come back up still going. Tail first, or dropping nose down off your line or straight down, throws you off. Landing straight keeps your speed and a long clean landing gives you a shove; nose down costs you most of it, and leaning
back on the way in saves some. Start or Esc pauses, with restart and quit.

## The courses

- Crater Atoll: a warm evening on a volcanic atoll: out of a pale lagoon, through a gap in the reef, open swell round the sea stacks and back in over the north pass. The sea builds and the tide comes in.
- Red Reach: noon on a river in a red sandstone canyon, round a mesa, cactus and fallen rock on its banks. The current runs one way down the valley: fast with it, a slog against it in midstream, slack at the banks.
- Iron Quay: a working port at night, cranes, stacked containers and lit windows on black water. A tanker under way crosses the course and its wake throws you.
- Gale Bank: a squall on the open sea, rain and lightning, a rig and wrecks. The swell starts big and keeps building, so the last lap is the biggest water in the game.
- Cold Sound: a grey morning in a granite fjord, open to the ocean at its west end. The tide falls through the race: while it stands high a drowned arch shuts the cut through the headland, and once there is room under it, it is the short way home.

A course is a definition in `courses/<id>.mjs`: track, ground, paint, wave mask, scatter, ramps, sea, tide, light and sky, and its hook. `tools/course-lib.mjs` says what each part does. To add one: write the definition, add it to `LEVEL_ORDER` in `tools/make-course.mjs` and raise `LEVELS` in `src/game.h`, then run `tools/make-course.mjs` and rebuild. `tools/course-look.mjs` shows it from the race camera, and `tests/jetski-handling.mjs` checks the computer riders get round it.

## How it is made

- The course comes from `tools/make-course.mjs`: heights, painted ground, the wave mask (the swell dies in the lagoon's
  shelter and on the shoals), palms, track and buoys. `tools/make-models.mjs` makes the craft, the rider, the buoys and the
  palms. `tools/make-effects.mjs` paints the sky. `tools/make-audio.mjs` makes every sound and the music.
- Tall slopes go through `tools/cliff-forms.mjs` after a course's height function: spurs and gullies down the fall line, beds of harder rock standing out as ledges, a pass of water erosion, and a record of where water runs. A course's paint uses those to choose between several graded photo scans, so a wall has big masses, beds and wet streaks as well as near detail. `tests/jetski-cliff-structure.mjs` measures big shapes against fine grain from a distance.
- The craft is four probes on the water (`src/sim.c`): the surface it rides is exactly the surface drawn, through
  `cw_water_sample_at`. A crest throws it up and the surface falling away leaves it in the air: no jump code.
- The rider (`src/rider.c`) has no animation clips. Every bone is posed each frame from what the craft is doing.

`jetski_auto` is the same game with a computer rider, for gates and screenshots.

## Tools on the CPU

`tools/shot-cpu.sh`, `tools/run-cpu.sh`, `tools/sim-check.mjs` and `tools/programs-cpu.sh` run on a software rasteriser, for
work when the GPU is busy: a frame, a whole race as a text trace, what the race did, and the program table.
