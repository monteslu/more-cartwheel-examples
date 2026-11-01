# Pirates

![Four captains trade broadsides around the volcanic archipelago](../../docs/screenshots/pirates.jpg)

A real-time naval game for one to four players sharing one screen. This is
an in-development example, not yet a release-quality game. The current maps
have rounded bays, peninsulas, shallow water and harbors; ships collect cargo,
fire broadsides and buy visible upgrades.

## Play

On the title screen, other players press START on their own controllers to
join. Player one chooses the mode and map, then presses ENTER / START to sail.
Keyboard controls belong to player one; each controller owns one captain.

| Action | Keyboard | Controller |
| --- | --- | --- |
| Turn / sail / reverse | WASD or arrows | Left stick or D-pad |
| Left broadside | J | Left trigger |
| Right broadside | L | Right trigger |
| Bank cargo / repair at your harbor | E | South / A |
| Buy hull upgrade | 1 | West / X |
| Buy sail upgrade | 2 | North / Y |
| Buy cannon upgrade | 3 | East / B |
| Choose mode on title | Q / R | Shoulder buttons |
| Choose map on title | T | SELECT / Back |
| Begin / join | ENTER | START |
| Pause / resume | ESC | START |
| Return to title while paused | Q | SELECT / Back |

Sail over timber bundles, iron barrels and treasure chests to collect them.
Your hold carries six items; a full hold slows the ship. Return within six
metres of your own harbor and bank the cargo. Banking adds score, stores the
resources for upgrades, and repairs up to 20 hull if you delivered cargo.
Each upgrade costs two of its resource: timber for hull, coins for sails,
iron for cannons. Each can be bought twice. Spending stored resources does
not reduce banked score.

Aim with your ship's side, not its bow. Cannons fire a three-shot broadside.
Both sides share one reload timer. The meter above your status panel shows
when the crew is ready, the remaining reload time, and left/right controls.
Sinking drops your carried cargo but keeps banked score and purchased
upgrades. Captains respawn after two seconds with four seconds of protection;
firing ends that protection immediately. Salvage expires after 30 seconds.

## Modes and maps

- **Cooperative Voyage:** supply the fleet, sink the patrol, then deliver a
  final haul before twelve minutes expire. Friendly fire is off. The quota
  and patrol health scale from one to four human captains. Unjoined captain
  slots remain inactive in this mode; solo play is a complete voyage.
- **Treasure Race:** six minutes to bank the most cargo. Unjoined captain
  slots become AI rivals. Equal leading scores produce a draw.
- **Team Treasure Race:** P1 + P3 against P2 + P4, with AI filling empty seats.
  Friendly fire is disabled within each partnership.

**The Gilded Shoals** has an active volcano. Starting at 18 seconds, three
rockfall zones are marked every 30 seconds. Leave the circles during the
3.5-second warning; impacts deal 28 damage within 3.2 metres. Harbor docking
areas are outside the impact zones. **Palm Lagoon** has sheltered coves and
no volcanic hazard, making it the calmer map to learn.

## Build and play

The repository pins cartwheel-engine **v0.29.1**, which includes the water,
scene-pause and WebGPU buffer fixes. No local engine patches are needed.
Committed assets and shader tables are sufficient to build.

From the repository root:

```sh
scripts/fetch-deps.sh
TARGETS="native cart-wgpu" scripts/build.sh pirates pirates_auto
./build/native/games/pirates/pirates
```

The playable cart is `build/cart-wgpu/carts/pirates.wasc`.
`pirates_auto` runs four AI captains in Treasure Race for replay checks.
Use `RELEASE=1 TARGETS=cart-wgpu scripts/build.sh pirates pirates_auto` for
optimized carts. CI builds both carts against the pinned SDK and smoke-tests `pirates_auto`.
Merges to main build `pirates.wasc` into the shared `release-carts` artifact;
the next tagged release publishes it alongside the other games.

For an engine source build, use a separate checkout of tag `v0.29.1`
(commit `506dd0669d77b471a6f824feaf0ae1bde85aa7c9`) and follow the
[source-build instructions](../../README.md#build). Its dependency lock pins
three.c `v0.187.4`; do not apply the older development patches described in
historical verification notes under `art/`.

## Assets

Committed GLBs and program tables are sufficient to build; Blender is needed
only to regenerate art. Run the scripts with Blender 4.2.9, for example:

```sh
blender -b --disable-autoexec -t 8 --python games/pirates/tools/make-coast.py
blender -b --disable-autoexec -t 8 --python games/pirates/tools/make-coast.py -- --lagoon
```

The terrain generator writes the model, albedo, editable blend and matching
collision/height header together. Keep those outputs together. Other Blender
scripts prepare the licensed galleon and generate harbors, cargo and upgrades.
See [CREDITS](CREDITS) for ownership and source information.
The original looping sea ambience is regenerated with
`python3 games/pirates/tools/make-ambience.py` (no Blender or audio packages needed).

## Verification

Use romdev to load the built `pirates.wasc` as platform `wasmcart`, step frames,
exercise controller inputs and inspect screenshots. Successful `loadMedia`
can return empty content; follow it with a frame step. A successful build or
a native screenshot alone is not romdev verification.

Runtime debug commands exercise actual gameplay functions and reset the match:

- `pirates_test_hull`: check bow/stern shore clearance, turning, reverse escape
  and spawns on both maps.
- `pirates_test_route`: skip an unreachable cove pickup and continue banking.
- `pirates_test_escape`: reproduce the previously stalled P3 shoreline turn
  and check safe escape and banking. Add `1` to start it for rendered stepping.
  `pirates_test_escape 0 1` checks the P1 stopped-turn case; `1 1` starts it live.
- `pirates_ai 0` through `5`: inspect a ship's pose, waypoint and shore clearance.
- `pirates_profile 590`: collect continuous rendered frames, then return CPU
  timing statistics inside that run. Use romdev command `maxFrames:600`.
  This avoids including idle gaps between tool calls in the frame timer.
- `pirates_impact 0` / `1` / `2`: fire ordinary projectiles into sea, hull or
  shore for visual checks. `pirates_effects` reports particle pool counts.
- `pirates_simulate N`: reset an AI race and run up to 21,600 gameplay ticks
  without intermediate rendering. Useful for logic diagnosis; reload before
  visual checks because water and particle effects are not advanced normally.
- `pirates_test_recovery`: sinking, protection duration, expiry and firing.
- `pirates_sink 0` through `5`: apply lethal damage through the normal damage
  rules to inspect sinking; existing respawn protection still blocks damage.
- `pirates_test_salvage`: cargo manifest, retained upgrades and restart.
- `pirates_test_voyage 1` through `4`: objective transitions and scaling.
- `pirates_test_eruption`: warning, damage, harbor clearance, Lagoon isolation,
  and 24 AI escape scenarios. Reload before visual evaluation: this scenario
  creates many immediate splashes that persist briefly in the water.
- `pirates_map 0` / `1` and `pirates_match <mode 0..2> <humans 1..4>` set up play.

The replay manifest is `tests/pirates-gates.json`. Native replay entry:

```sh
node tests/native-replay.mjs --bin build/pirates-current/project \
  --out out/pirates-replay-gl --gpu gl pirates

# Complete six-minute match, checking all full-match expectations:
node tests/native-replay.mjs --bin build/pirates-current/project \
  --out out/pirates-full-gl --gpu gl --gate pirates-race-full pirates
```

The native replay runner writes live diagnostics to `<out>/pirates.log` and
the final selected state to `<out>/pirates.json`. A missing final dump is not
a passing result. `--gate` selects the full gate's frame count and assertions;
`--frames` alone is a state capture and does not check end-of-match expectations.

Evidence and limitations for individual features are recorded under `art/`.
`art/FULL-RACE-VERIFICATION.txt` records a complete rendered romdev race.
`art/FULL-TEAMS-VERIFICATION.txt` records a complete rendered team race,
including an unresolved late-run frame-delivery slowdown.
`art/NATIVE-PERFORMANCE-VERIFICATION.txt` records three 1080p hardware GL
replays on a Radeon 890M, with sampled frame averages of 6.65–6.79 ms;
these measurements do not establish full-match or cross-hardware performance.
`art/NATIVE-FULL-PERFORMANCE-VERIFICATION.txt` records a full native GL race
and its final-600-frame timings; the romdev slowdown did not reproduce there,
but WebGPU and host-side causes remain unverified.
`art/NATIVE-WEBGPU-PERFORMANCE-VERIFICATION.txt` adds a full native WebGPU
race with identical gameplay state and no reproduced late-match slowdown.
The remaining performance investigation concerns the cart host and workload
differences. `pirates-race-full` in the gate manifest preserves the full-match
expected state; the native replay runner still defaults to the short gate.
Use `--gate pirates-race-full` for its full-match assertions.
`art/CURRENT-FULL-PERFORMANCE.txt` verifies the latest native WebGPU build
through a complete race, with six timing samples and the full expected state.
It includes the recent terrain, sail, sinking and audio changes; sampled
frame averages were 7.96–8.49 ms at 1080p on a Radeon 890M. Occasional frames
exceeded 16.7 ms. Current full-match cart-host profiling remains outstanding.
`art/SHALLOWS-FULL-RACE-VERIFICATION.txt` records a full rendered romdev
race with the current navigation and shallows: all 13 final-state fields
matched, with zero GPU errors or skipped draws, and results/restart worked.
This correctness check did not collect frame-time samples. Its subsequent
interactive WebGPU window was slow and the romdev connection failed; a native
GL build is available for human playtesting while that host issue is investigated.
Outstanding work includes human playtesting, richer art, broader performance coverage,
cross-host replay coverage, SDK integration and release packaging.
