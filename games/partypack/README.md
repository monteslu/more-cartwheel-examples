# Party Pack (working title)

A party game for one to four players on one shared screen. Empty seats are
filled by CPU players, so you can play alone or with friends, and every
mini-game can be played by four CPUs on their own.

## How it plays

The title, player join, pause and settings screens are the engine's game
shell: each pad joins with its bottom (south) face button. Start pauses a game;
on the hub, the boards and the other panels between games it confirms, like
the south button, and the east button on the hub goes back to the title.

After joining you get the hub:

- **Party Tournament**: 4, 6 or 8 rounds, each one mini-game from all eleven, on a board. After
  each game every seat gets coins for its place (5, 3, 2, 1; the round before the last counts double and the last
  one triple) and rolls a die (1 to 4) plus a bonus for its place (3, 2, 1, 0). Before anyone moves, the board
  opens its shop: each person picks with left / right and confirms with the south button (Save, Boot: 3 more
  spaces for 3 coins, Swap: trade places with the seat just ahead for 6, Double: your next game's coins count
  twice for 4). Once one person has picked, the rest get 10 seconds. Then the tokens walk: passing the star with
  10 coins buys it, and the star moves on to a few spaces in front of the seat furthest behind. Landing spaces give
  3 coins, take 3 coins from the seat ahead in the standings, or swap places with a random seat. A seat behind the
  leader gets an underdog coin and space. Most stars wins, then the furthest along, then the most rounds won, then
  a draw. Every third round from the second is 2 vs 2: the leader and the last against the two between, each
  team's two scores added, and both of the better team take first place. The end screen gives three awards from what happened, and the south button starts another tournament.
- **Free Play**: pick any mini-game and play it on its own. All eleven are open from the start.
- **Trophy Room**: party points, tournament wins per seat, best scores, hats.
- **Hat**: party points (the lifetime total of the points people earn) unlock hats (a party hat at 4, a crown
  at 8, a top hat at 12, a flower at 16, a bow at 20); each person turns their own with left / right on this row.
- **Rounds** and **CPU Skill** (easy, medium, hard) are set on the menu.

Every mini-game opens with a card (how to play, press the south button to be ready), a
3-2-1 countdown, play, a finish banner and results. Once one person is ready, the
card waits 10 seconds for the rest (the seats still out show the seconds left) and
then starts with everyone, so a pad that joined and was put down never holds the
others on the card. `tests/partypack-idle-seat.mjs` plays the first round through
romdev alone and with such a seat; its control (`sw ready_forever 1`, the card
waiting for every person) must stay on the card.

## Mini-games

| Name | Kind | Idea |
|---|---|---|
| Hurdle Dash | 2D | jump rocks, duck bars, three lives |
| Echo | 2D | repeat a growing pattern on the four face buttons |
| Stack Attack | 2D physics, 3D set | crane and blocks, tallest standing tower |
| Blast Ring | 3D physics | bombs shove you, the ring shrinks |
| Beat Box | 2D rhythm | hit the notes as they land, in three lanes on the west, south and east buttons |
| Loop Racers | 3D racing | three laps, hazards on the short lane |
| Quick Quiz | 2D trivia | four answers in a diamond, one per face button |
| Raft Rush | 3D water | paddle a raft for floating buoys |
| Sumo Push | 2D rules, 3D set | shove everyone off the shrinking plate |
| Coin Rush | 2D rules, 3D set | grab the most coins, gold ones count three |
| Tile Drop | 2D rules, 3D set | the floor crumbles, be the last one standing |

Sumo Push, Coin Rush, Tile Drop and Stack Attack keep their 2D rules (stage units, the same
simulation) and are shown on 3D sets: a wooden plate in a pool inside a little stadium, a garden yard with
coins standing on edge, glazed tiles on a steel lattice over a deep pool, four pillars under a crane rail. The set is built and moved from the 2D
state in the game's draw, never in its tick, so a replay and the balance runs are what they were.

A fall (a ring-out in Sumo Push or Blast Ring, a drop in Tile Drop) plays at half speed for a moment with a
crowd's gasp: every other play tick is held, like a hit-stop, so the simulation is the same. Echo forgives one
slip in its first three patterns. Quick Quiz draws from about 130 questions in nine categories. Each player's
3D character has a rim light in their colour that shows from far away; Loop Racers' camera follows the pack.

Everything is saved by the shell.

## Controls

Stick or d-pad or arrows / WASD move. The south face button (Space, Enter) is
the main button, east (Shift) the second, west (Z) and north (X) the others.
Player 1 can also use the keyboard. Each mini-game's card shows the buttons it
uses as the engine's button prompts, drawn for the device each player holds
(a pad's four-dot diamond with the one to press solid, or a keycap).

The picture never shakes: hits, bumps, drops and the bombs of Blast Ring (by how close they go off) are felt in each
person's own pad (rumble; Rumble on or off in the menu, saved).

## Building

    scripts/fetch-deps.sh
    scripts/build.sh                 # native and cart-wgpu (the dual GL + WebGPU cart)
    scripts/shot.sh partypack_auto 900 out/shot.png   # a screenshot of four CPUs playing

`partypack_auto` plays a whole tournament with four CPU players and no input
(tests, screenshots, soak runs). Set `PP_GAME=<id>` (hurdle, echo, stack,
blast, beatbox, loop, quiz, raft) to repeat one mini-game.

Tests: `node deps/cartwheel-engine/tests/gate.mjs --gates-file partypack-gates.json`
with `CW_PROJECT_ROOT` set to the repository (`tests/partypack-gates.json`).

## Adding a mini-game

Write an `mg_def` (see `src/mg.h`: start, tick, think for the CPU, draw) in
its own `src/g_*.c`, add it to the list in `src/main.c`. A game reads
`ctx->in[p]` for every player, never a pad, so CPUs and people are the same
to it. 3D games name a scene in `assets/scenes/` and build the rest in code
with the helpers in `src/common3d.c`.

## How it is made to look this way

All art is generated by scripts in `tools/` (see CREDITS); run them in this
order after editing any of them, then `node tools/gen-roots.mjs`:

    node tools/bake2d.mjs         lit 2D sprites (albedo, normal map, baked-light copy), decals
    node tools/make-textures.mjs  PBR texture sets (albedo, normal, occlusion-roughness-metal, emissive)
    node tools/make-models.mjs    glb models that embed those textures
    node tools/make-luts.mjs      colour grades

The 2D mini-games draw their world through the engine's lit 2D layer (sprites
with normal maps, real 2D lights); the HUD and banners are a second, unlit
layer. The 3D scenes have a key light with shadows, coloured accent lights,
HDR bloom, ambient occlusion, a colour grade and projected decals.
The hub is a 3D scene too: a game table with the board's tiles and the four
characters as tokens.
