# coopplatformer (working title)

A co-op side-scrolling platformer for one to four players on one shared screen, in a world owned by the Management, who would like you to know it. Five worlds of five levels each (the meadow, the caves, the ruins, a frozen head office, the crater), each with its own music and boss, and something new in every level: springs, crumbling bridges, a hedge trimmer, falling pianos, belts, presses, doors, fans, ice, and a cannon that is also your severance package.

The camera pulls back to keep everyone in view; if you fall too far behind (or die while a friend is still up) you are carried to the group in a bubble. A seat whose pad has not been touched for five seconds holds nobody back: the camera follows the players who play, the idle one is carried to them in a bubble when it leaves the screen, and a player who falls with only an idle friend left goes back to the last flag with everyone, as alone. Run first and jump further: a running jump goes more than twice as far as a standing one. Three abilities open the way as you go: an air jump, wall claws and a dash. A wooden hatch drops into each secret room (Down + Jump; a jump and an air jump come back up). The cute animals are not spared. Sometimes it is only a bubble. Sometimes it is not.

## Controls

| | Keyboard | Pad |
|---|---|---|
| Move | arrows / WASD | left stick / d-pad |
| Jump (hold for height; twice with the air jump; off a wall with the claws) | Space / Z | south face button |
| Run (hold: speed builds, and a running jump goes much further and higher) | Shift / X | west face button |
| Dash | C / F | right shoulder |
| Grab a friend; again to throw them (a jump wriggles free) | V / E | left shoulder |
| Drop through a wooden ledge | Down + Jump | down + south face button |
| Swim | stick, Jump to stroke | same |
| Map (left and right pick a flag you have touched, up and down a level you have reached, Jump travels there) | M / Tab | Back |
| Back to the last flag (hold one second; the way out of any pit or wedge) | R | north face button |

Stomp enemies from above. Each world brings its own creature after its first level: a hopper in the meadow, a dropper in the caves that hangs from the roof and drops on whoever walks under (stomp it while it is dazed), a shell in the ruins (a stomp shuts it in, a touch kicks it, and a kicked shell hurts friends too), a slider in the head office with a spiked front (from behind). Every level hides three big coins, worth five each; the pips under the ability chips and the map count them. Spikes hurt, lava, pits and machinery do not forgive. Landing on a friend's head bounces you and flattens them for a moment. You can also pick them up and throw them, over a gap or otherwise. It does no harm and it will be remembered. Not everything that looks like a coin is a coin, and not every finish is a finish. Every level ends with something large behind you; run. Walk through a flag to set a checkpoint. On a touch screen there is a stick and buttons for Jump, Run, Dash and Grab.

The camera never shakes. A hit you take, a stomp you land, a long fall, the boss landing near you, its hits and its end are felt in your own pad instead (rumble; the boss and the blasts by how close you are).

## Building

    scripts/fetch-deps.sh
    scripts/build.sh
    build/native/project/games/coopplatformer/coopplatformer

`coopplatformer_auto` plays itself (two bots) for tests and screenshots.

## Tests

    CW_PROJECT_ROOT=$PWD node deps/cartwheel-engine/tests/gate.mjs --gates-file coopplatformer-gates.json

runs the replay gates in `tests/coopplatformer-gates.json` (`--cpu` for the state checks on the software renderer).
`COOP_SOAK=<ticks> COOP_PLAYERS=<n>` on `coopplatformer_auto` runs the bots through the whole game with no
rendering and reports wins, stalls and deaths; `COOP_FUZZ=<runs>` checks that each ability gate needs its ability;
`COOP_SECRETS=1` runs the scripted secret routes; `COOP_IDLE_SEATS=<n>` leaves the last n seats joined and untouched (`COOP_IDLE_ANCHOR=1`, those seats holding the group back, is its control).
`node tests/coopplatformer-idle-seat.mjs` plays the first level on the cart through romdev (GL and WebGPU) by the pad, alone and beside a seat that joined and was never played; its control, the debug command `sw idle_anchor 1`, must keep the player short of where alone gets.
`node tests/coopplatformer-reach.mjs` searches with the game's own movement for every coin, ability, flag, cracked wall and room nobody can get to with what they have found by then (`games/coopplatformer/reach.c`).

The debug field `dev.warp` puts every player at a column with every ability (romdev: `wasm({op:'write', name:'dev.warp', value:178})`), so a sweep can reach any region. `COOP_MAP=<x0>` prints the level with its region row.
