# Fighter

A one-on-one fighting game: 2D rules, 3D look. Two players on one screen, or one player against the computer.
Four fighters, three stages, a training mode that shows hitboxes and frame data.

## Playing

Run `fighter` (or `scripts/build.sh` then `build/native/.../fighter`). Pick Play, join with a pad (south) or the keyboard (enter), choose a fighter and a stage.

| Action | Pad | Keyboard, player 1 | Keyboard, player 2 |
|---|---|---|---|
| Move, jump, crouch | left stick or d-pad | W A S D | arrow keys |
| Light, medium, heavy | X, Y, B | J, K, L | comma, period, slash |
| Throw | A or a shoulder | I | semicolon |

Hold back to block: stand to block mid and overhead hits, crouch to block mid and low hits. Throws cannot be blocked: press throw as you are grabbed to break it. Tap forward twice to dash, back twice to step away. Jump with up.

Special moves are a stick motion plus an attack button (the stronger the button, the stronger the move). Motions are written as numpad directions with 6 forward: `236` is down, down-forward, forward. `[4]6` means hold back for half a second, then forward. The super is `236236` and needs a full meter. Every fighter's moves are listed by `node tools/framedata.mjs`.

A chain works on hit or block: a light cancels into a medium or heavy, any normal cancels into a special, a special into the super. Hits scale their damage in a combo.

## Training mode

Press X on the select screen to switch Versus to Training. It shows the last move's startup, active and recovery frames, the frame advantage of the last hit or block, the damage and combo, and your recent inputs. Keys: F or left-stick click cycles what the dummy does (stand, crouch, jump, block everything, play like the computer), B or right-stick click shows hitboxes (blue: hurtboxes, red: hitboxes), R resets, Tab returns to the select screen.

## Tools

- `tools/framedata.mjs`: the frame data table from `assets/data/moves.txt` (`--json`, `--csv`, `--fighter id`), and a checker. The file is the one the game reads: edit a move, run the tool, play it.
- `tests/run-simtest.sh`: plain-C rules tests (frame data matches the table, blocking, motions, combos, throws, rounds, a computer-vs-computer soak, determinism).
- `tools/make-textures.mjs`, then `tools/make-characters.mjs` (the fighters; `crowd` for the spectators) and `tools/make-world.mjs` (the stage backgrounds): regenerate the models in assets/models.
- `tools/make-stages.mjs`, `tools/make-grade.mjs`, `tools/make-audio.mjs`, `tools/make-gate-script.mjs`: regenerate the stage lighting, colour grades, sound and the gate scripts.
- `tools/blender/pose.py`, `tools/blender/lineup.py`: check renders of the fighters in Blender (CPU).

## How it is built

`sim.c` is the rules: integer, 60 ticks a second, no renderer. Every move has startup, active and recovery frames, a hitbox, a hurtbox extension and stun values, all in `assets/data/moves.txt`. Inputs go through the engine's command buffer and motion matcher (`cartwheel/motion.h`), the camera through its framing helper (`cartwheel/framing.h`). `chars.c` loads each fighter's skinned model and poses its bones from the simulation (including the fingers: fists, open hands for grabs and palm strikes), so animation timing is the frame data; the joint positions are the ones the hitboxes were drawn against. `secondary.c` swings hair, headband tails, coat skirts and bellies on springs and gives skin, cloth, hair and leather their shading. `stage.c` loads the stage background and runs its ambient life (rain, steam, petals, flickering neon, swaying lanterns, turning fans) and the rim lights; `crowd.c` the spectators. `fx.c` has the hit effects, including shockwaves that bend the picture on heavy hits, supers and knockouts.
