# coopplatformer (working title)

A co-op side-scrolling platformer for one to four players on one shared screen. The camera pulls back to keep
everyone in view; if you fall too far behind (or fall in a pit while a friend is still up) you are carried to the
group in a bubble. Four regions are laid out left to right: a sunny meadow, torchlit caves, a flooded ruin and a
volcano with a boss at the end. Three abilities open the way: an air jump, wall claws and a dash. A cracked wall
hides a treasure nook, a ledge high in the meadow hides coins, and the map screen shows where you have been.

## Controls

| | Keyboard | Pad |
|---|---|---|
| Move | arrows / WASD | left stick / d-pad |
| Jump (hold for height; twice with the air jump; off a wall with the claws) | Space / Z | A |
| Dash | Shift / X | X |
| Drop through a wooden ledge | Down + Jump | Down + A |
| Swim | stick, Jump to stroke | same |
| Map (left and right pick a flag you have touched, Jump travels there) | M / Tab | Back |
| Back to the last flag (hold one second; the way out of any pit or wedge) | R | Y |

Stomp enemies from above. Spikes hurt, lava and pits do not forgive. Walk through a flag to set a checkpoint.

## Building

    CARTWHEEL_ENGINE=<engine checkout> scripts/fetch-deps.sh
    scripts/build.sh
    build/native/project/games/coopplatformer/coopplatformer

`coopplatformer_auto` plays itself (two bots) for tests and screenshots.
