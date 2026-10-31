# Fighter

A one-on-one fighting game: 2D rules, 3D look. Two players on one screen, or one player against the computer.
Four fighters, three stages, a training mode that shows hitboxes and frame data.

## Playing

Run `fighter` (or `scripts/build.sh` then `build/native/.../fighter`). Pick Play, join with a pad (A) or the keyboard (enter), choose a fighter (left / right) and a stage (up / down), lock in with A (kick).

The camera never shakes: hits, throws, wall splats, knockdowns, supers and the KO are felt in each player's own pad (rumble; the computer's fighter has none).

Three buttons: punch, kick and block. The stick (or d-pad) picks where a punch or kick goes.

| Action | Pad | Keyboard, player 1 | Keyboard, player 2 |
|---|---|---|---|
| Walk (hold), dash (tap twice) | left stick or d-pad, left / right | A, D | left, right arrows |
| Jump (up-forward, up-back: that way) | up | W | up arrow |
| Crouch (hold) | down | S | down arrow |
| Punch | X | J | comma |
| Kick | A | K | period |
| Block | B or right shoulder | L | slash |
| Throw (punch + kick together) | Y or left shoulder, or X + A | I, or J + K | semicolon |

Attack height: up with a button for a high attack (press them together), nothing for a mid one, down for a low one, forward for a
stepping one (it moves in). Every fighter has its own eight: for example Bolt has a straight (punch), a rising elbow
(up + punch), a low jab (down + punch), a lunge punch (forward + punch), a side kick (kick), a roundhouse (up + kick),
a sweep (down + kick) and a step kick (forward + kick).

Blocking is a button, not holding back:
- Block alone guards high and mid attacks. Down + block is a low guard: it stops low and mid attacks, and high
  attacks pass over it (a crouch).
- So: a high is blocked standing (or ducked), a low must be blocked low, a mid is blocked either way.
- A low attack crouches the attacker too, so a high attack whiffs over it.
- Blocking costs nothing except chip damage from specials (an eighth of the damage, never a knockout). Blocked hits fill a
  guard gauge under the life bar; when it fills, the guard breaks and you are open for a moment. It drains when you
  stop blocking.
- A parry: press block (fresh, not held) in the last few frames before a hit lands, at the right height, and you are
  free at once while the attacker is still recovering. Pressing block again within half a second of the last press
  opens no parry window, so mashing does not work.
- Throws beat blocking; press punch + kick as you are grabbed to break the throw.

Moving, jumping and crouching:
- Hold forward or back to walk, diagonals included. A walk rolled onto up-forward or down-forward (a thumb drifting on
  the d-pad) keeps walking: the diagonal becomes a forward jump only after 5 frames held, a crouch after 6.
- Up jumps on one press, every time the same jump, after a 4-frame takeoff; up-forward and up-back jump that way.
  Held, up jumps again on landing. Punch or kick in the air is a jump attack; a jump clears low attacks.
- Up and a button together is the high attack, not a jump: a button pressed up to 3 frames after up (inside the
  takeoff) or up to 3 frames before it gives the high punch or kick. Down and a button is the low one.
- Hold down to crouch: high attacks pass over a crouch, mids and lows hit it. Down + block is the low guard.
- Tap forward twice to dash in, back twice to step away (the taps need a moment of neutral between them, so rolling
  across the d-pad never dashes).
- A button or up pressed during a move's recovery is kept for 6 frames and comes out on the first free frame.
- The stick has a dead zone (35 percent) and an 8-way gate whose up, down, forward and back sectors are 50 degrees
  wide, so forward pushed a little high still walks. On the d-pad and keys, left + right together is neutral and up +
  down is up.

Special moves are a stick motion plus their button, shown on the select screen and in the training move list.
Motions are written as numpad directions with 6 forward: `236` is down, down-forward, forward; `236 P` is that motion
then punch. `[4]6` means hold back for half a second, then forward. The super is `236236` with punch or kick and
needs a full meter. Every fighter's moves are listed by `node tools/framedata.mjs`.

A chain works on hit or block: a punch cancels into a kick (or the lunge), any normal into a special, a special into
the super. Hits scale their damage in a combo, and a long combo runs out of hitstun. A fighter knocked into the air takes three more hits at most, then falls through the rest (the HUD says JUGGLE OVER).

## Training mode

Press X (punch) on the select screen to switch Versus to Training. It shows the last move's name and height, whether it hit, was blocked, parried or ducked, its startup, active and recovery frames, the frame advantage, the damage and combo, and your recent inputs (direction digits with P, K and G for punch, kick and block). A move list for your fighter sits on the right. Keys: F or left-stick click cycles what the dummy does (nothing, stand, block, block low, jump, block everything at the right height, play like the computer), B or right-stick click shows hitboxes (blue: hurtboxes, red: hitboxes), R resets, Tab returns to the select screen.

## Tools

- `tools/framedata.mjs`: the frame data table from `assets/data/moves.txt` (`--json`, `--csv`, `--fighter id`), and a checker. The file is the one the game reads: edit a move, run the tool, play it.
- `tests/run-simtest.sh`: plain-C rules tests (every direction + button gives its move, move lengths and frame advantage equal the table, the high / mid / low guard triangle, parries, guard breaks, held walks through d-pad drift, one-press jumps, up + button highs, buffering, motions, combos, throws, rounds, determinism) and a computer-vs-computer soak that prints win rates per fighter and per pairing, round length, comebacks, and how often parries, guard breaks and ducked highs happen.
- `tests/run-rulescheck.sh`: plain-C checks of the rules against the fun rubric, the simulation only, no window or GPU: no degenerate move over the whole move table, an exhaustive combo search for infinites per fighter in the middle and both corners, every move at every timing around wakeup against every answer, bots restricted to one move class against the field, the computer against every one of those bots and against a varied player, comeback rates, cross-ups. Each check has a control that breaks the table or a copy of the rules and must fail. `controls` or `checks` runs one half.
- `tools/frame-trace.mjs`: frame times from a profiler trace (the select-screen stutter check).
- `tools/make-textures.mjs`, then `tools/make-characters.mjs` (the fighters; `crowd` for the spectators) and `tools/make-world.mjs` (the stage backgrounds): regenerate the models in assets/models.
- `tools/make-stages.mjs`, `tools/make-grade.mjs`, `tools/make-audio.mjs`, `tools/make-gate-script.mjs`: regenerate the stage lighting, colour grades, sound and the gate scripts.
- `tools/blender/pose.py`, `tools/blender/lineup.py`: check renders of the fighters in Blender (CPU).

## How it is built

`sim.c` is the rules: integer, 60 ticks a second, no renderer. Every move has startup, active and recovery frames, a hitbox, a hurtbox extension and stun values, all in `assets/data/moves.txt`. Inputs go through the engine's command buffer and motion matcher (`cartwheel/motion.h`), the camera through its framing helper (`cartwheel/framing.h`). `chars.c` loads each fighter's skinned model once at start-up (a glTF resource; every fighter on screen is an instance of it, so picking fighters on the select screen loads nothing) and poses its bones from the simulation (including the fingers: fists, open hands for grabs and palm strikes), so animation timing is the frame data; the joint positions are the ones the hitboxes were drawn against. `secondary.c` swings hair, headband tails, coat skirts and bellies on springs and gives skin, cloth, hair and leather their shading. `stage.c` loads the stage background and runs its ambient life (rain, steam, petals, flickering neon, swaying lanterns, turning fans) and the rim lights; `crowd.c` the spectators. `fx.c` has the hit effects, including shockwaves that bend the picture on heavy hits, supers and knockouts.
