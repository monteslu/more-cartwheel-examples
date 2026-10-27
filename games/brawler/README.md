# Brawler

One to four players fight through four street stages on one shared scrolling screen. Walk along the street
and up and down its width, fight a crowd that surrounds you, break crates and barrels for food and weapons,
beat a mini boss and a boss at the end of every stage.

## Controls (pad, or keyboard for player 1)

| Action | Pad | Keyboard |
|---|---|---|
| Move | stick or d-pad | WASD or arrows |
| Attack (three hit combo) | X | J or Z |
| Heavy kick | Y | K or X |
| Jump (attack in the air: flying kick) | A | Space |
| Grab | right bumper | L or C |
| Spin move (costs a little health) | left bumper | I or V |

Grab a body in front of you, then Attack to knee it. Three knees throw it, or hold the stick away from it and
press Grab or Kick to throw it over your shoulder. Thrown bodies knock down whoever they hit.

## Run

`brawler_auto` is the same game with bots playing, for tests and screenshots.

## What it is for

It exists to test the engine: skinned characters with a shared clip library, the navigation crowd (enemies steer
and avoid each other; an attack-token pool decides who may hit), GPU particles for hits, dust and debris, a clustered
light pool for flashes, and the game shell. Findings are in internal-cartwheel/findings/brawler.md.

State of the game: see that file for what is finished and what is not.
