# arenafps (working title)

A first-person arena shooter for one to four players on one screen (split screen, one pad per player), with
bots that find their way over a navigation mesh.

## Play

- Title -> join (each pad presses South to join) -> pick the arena, the mode and the number of bots -> fire to start.
- Move: left stick (keyboard: W A S D). Look: right stick (keyboard: arrow keys). Fire: right trigger or right
  shoulder (keyboard: J). Jump: South (keyboard: Space). Next / previous weapon: North / West (keyboard: E / Q).
- Pause with Start.

Modes
- Deathmatch: everyone for themselves, first to 15 frags or the best score after five minutes.
- Team deathmatch: red against blue, first team to 30.
- Co-op waves: the players hold the arena against six waves of bots, with a shared pool of lives.

Weapons (found on the map; you always have the rifle)
- Pulse rifle: fast hitscan, low damage, a head shot does half again.
- Scatter gun: eleven pellets, deadly close up.
- Rocket: slow shot, big blast and knockback (you can launch yourself; your own blast hurts half as much).
- Rail: slow, pierces every fighter on the line.

Arenas: Foundry (a raised deck and pillars), Courtyard (buildings with doorways and roofs), Terraces (stepped
levels and a pit).

## Build

See the repository README: `CARTWHEEL_ENGINE=<engine checkout> scripts/fetch-deps.sh`, then `scripts/build.sh`.
`arenafps_auto` is the same game with the bots playing every seat (ARENA_ID, ARENA_MODE, ARENA_BOTS,
ARENA_PLAYERS choose the match): the tests and benchmarks run it.
`tests/arenafps-split-bench.mjs` measures the cost of one to four views.
