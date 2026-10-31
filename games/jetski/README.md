# jetski

A wave racing game on one evening's water. Six riders race three laps around a volcanic atoll: out of a pale lagoon,
through a gap in the reef, across open sea, past sea stacks and back in over the north pass. The water is the track. It
moves, its swells throw you into the air, and it builds from lap to lap while the tide comes in.

Status: one course so far, one craft, a computer racer field, one player for now. The menu offers a championship (points by
place over every course, one course until there are more) and a time trial (you alone, keeping your best time). Difficulty
makes the sea bigger and adds buoys: every second one on Easy, three in four on Normal, all of them on Hard.

## How to ride

| Pad | Keys | |
|---|---|---|
| Left stick, d-pad | Arrows, A / D | Steer, and lean the rider into the turn |
| Stick down | Down, S | Lean back: a tighter carve that costs speed. Throw it forward at a crest to jump higher |
| A, right trigger | Space, J | Throttle |
| X, right shoulder | K | In the air: a barrel roll, or a back flip with the stick pulled back |

Buoys mark the course. Pass each one on the side its colour and the chevron show: orange-red on your right, yellow on your
left. Each one passed lights an arrow and raises your top speed, five at most. Miss one and they all go out.
Five misses end your race, and the dots under the lap count show how many you have. So do five seconds away from the course,
with a warning counting them down. Too much stick scrubs speed, and a long hard slide throws you off.
Floating ramps ride the swell on the racing line: hit one square at speed for well over a second in the air, long enough for a trick. Each press of the trick button is one trick; land mid-trick and you are off the craft. From the side a ramp is a wall.
Landing straight keeps your speed and a long clean landing gives you a shove; nose down costs you most of it, and leaning
back on the way in saves some. Start or Esc pauses, with restart and quit.

## How it is made

- The course comes from `tools/make-course.mjs`: heights, painted ground, the wave mask (the swell dies in the lagoon's
  shelter and on the shoals), palms, track and buoys. `tools/make-models.mjs` makes the craft, the rider, the buoys and the
  palms. `tools/make-effects.mjs` paints the sky. `tools/make-audio.mjs` makes every sound and the music.
- The craft is four probes on the water (`src/sim.c`): the surface it rides is exactly the surface drawn, through
  `cw_water_sample_at`. A crest throws it up and the surface falling away leaves it in the air: no jump code.
- The rider (`src/rider.c`) has no animation clips. Every bone is posed each frame from what the craft is doing.

`jetski_auto` is the same game with a computer rider, for gates and screenshots.

## Tools on the CPU

`tools/shot-cpu.sh`, `tools/run-cpu.sh`, `tools/sim-check.mjs` and `tools/programs-cpu.sh` run on a software rasteriser, for
work when the GPU is busy: a frame, a whole race as a text trace, what the race did, and the program table.
