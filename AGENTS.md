# Working in more-cartwheel-examples

Games built with [cartwheel-engine](https://github.com/monteslu/cartwheel-engine),
one directory each under `games/`. They build against the prebuilt engine SDKs
pinned in `scripts/deps.lock` and ship as dual GL + WebGPU carts (`.wasc`).

## Build and test

| Command | Does |
|---|---|
| `scripts/fetch-deps.sh` | fetches the pinned SDKs and the engine source at the same commit (its test tools) into `deps/` |
| `scripts/build.sh [app ...]` | builds `build/native` (this machine) and `build/cart-wgpu` (the cart that ships); `TARGETS=native` for one tree |
| `node tests/gate.mjs --gates-file <game>-gates.json [gate ...]` | runs a game's gates: the same run on every host it names, frames and debug state compared, each with a control run that must fail |
| `node tests/cart-smoke.mjs <game>` | a short headless run of each game's cart (what CI runs) |
| `node deps/cartwheel-engine/tools/check-prompts.mjs games/<game>` | the prompt check below (CI runs it for every game listed in `PROMPT_GAMES`) |

Gates and screenshots that use the GPU go through the engine's GPU lock:
`node deps/cartwheel-engine/tools/with-gpu-suite.mjs <name> <command>`.
`scripts/partypack-suite.sh` shows how a game's whole suite is split into short
lock holds.

## Newcomer and idle-seat gates

Bots and soaks know the map and always press something, so they cannot catch a
game that only someone who already knows it can finish, or one that a second
pad put down on the table holds hostage. Every game gets two gates of its own,
both on the built cart through romdev, on GL and on WebGPU:

- **Newcomer gate.** From power-on to the end of the first level, race, round
  or match, driven only by what the screen shows: the title's prompts, the
  objective text, markers and arrows (read from the debug fields the game
  draws them from), never a route or map the test knows in advance. Its
  control turns the cue off (a `sw` debug switch) and must leave the player
  short of the goal in the same frames.
- **Idle-seat gate.** A second pad joins, then never touches anything. The
  first player must still reach play and finish the first level, race or round:
  no ready check, select screen or instructions card that waits for ever, no
  shared camera or tether that holds the player back, no lives or quota spent or
  raised for the seat nobody plays. Its control (a `sw` switch that brings the
  old wait or tether back) must fail.

Pause, resume, retry and quit belong in one of the two. The existing ones show
the shape: `tests/crawler-find-the-way.mjs` (newcomer), and
`tests/crawler-left-behind.mjs`, `tests/coopplatformer-idle-seat.mjs`,
`tests/fighter-idle-seat.mjs`, `tests/partypack-idle-seat.mjs`,
`tests/pirates-idle-seat.mjs` and `tests/arenafps-idle-seat.mjs` (idle seat).
Look at the frames they save: a frame hash cannot show that the way is
readable. A new game is not done until both gates pass.

The repository pins one engine release. When you move the pin, change the engine
commit and every SDK line (with its sha256) in `scripts/deps.lock`, run
`scripts/fetch-deps.sh`, and re-run every gate of every game you touch: a new
engine can change what a frame looks like.

## Showing controls

Prompts use the engine's button prompts (`cartwheel/prompt.h`): text names the
action, and the engine draws the glyph for the device each player holds. A pad
gets a diamond of four dots with the button to press solid, a keyboard a keycap,
a touch screen the on-screen control.

- Never write a button's name or colour in text or art: `{jump}: hop a gap`, not
  a letter or "the green button". Different pads put different letters and
  colours on the same button, and a keyboard or phone has none. The word "Start"
  is the one exception.
- Text says what the button does; the glyph says which one. Draw text that holds
  prompts with `cw_prompt_text` (markup: `{action}`, `{left|right}` for a pair,
  `{pad:west}` for one physical input), and single glyphs with
  `cw_prompt_action` or `cw_prompt_binding`.
- Colours belong to the game. A green lane is fine; telling the player to press
  green is not.
- When a design is about position (lanes, answer slots), lay it out where the
  buttons sit: west, south, east left to right, or a diamond.
- Confirm is south and back is east on every pad.
- Do not track the last device yourself: ask `cw_prompt_device_of`.
- Pass the seat to a prompt that one player reads (a split-screen HUD, a ready
  slot) and `-1` to one every seat reads (a title, an instructions card).
- Every pad has four face buttons, a d-pad, two shoulders, Start and Back.
  Sticks and analog triggers are not always there: anything a game needs must
  also have a binding on a button or the d-pad.

`check-prompts.mjs` fails on the usual slips in string literals ("A: jump",
"(B)", "press X", "LB / RB", "RT: gas"). It cannot see letters drawn in art or in
code, so review those by hand, and move keyboard hints such as "W / S: gas" to
the markup too. A line that really means it carries `prompt-ok: <reason>` on it
or on the line above.
