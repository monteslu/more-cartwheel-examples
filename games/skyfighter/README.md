# Sky Fighter

![Sky Fighter](../../docs/screenshots/skyfighter.jpg)

An arcade jet combat game with eight freely selectable, goal-based missions, built with cartwheel-engine and three.c. Every mission has its own terrain, layout and targets. There are no unlock requirements; cleared markers and the selected mission are stored in SRAM alongside flight, audio and graphics preferences.

## Missions

| Mission | Setting | Goal |
|---|---|---|
| Country Fair | Inland rolling farmland, country roads and barns | Destroy twelve modern hot air balloons with occupied baskets, including low flights over the fields. Shoot the basket for a precision bonus and a rapidly rising envelope; shoot the envelope to drop the basket under rigid-body physics. |
| Time Warp | Snowy alpine valleys and mountain village | Defeat eight prop fighters and six cross-marked dirigibles. |
| Fuel Chain | Red-rock desert canyon and fuel compounds | Destroy four fuel depots and trigger their secondaries. |
| Coastal Scramble | Mountainous island, airfield and open sea | Defeat ten modern interceptors. |
| Sky Fortress | Daylit city beneath a giant mothership | Destroy four generators above the shield, then the exposed core of the 1.08 km-wide mothership. Its destruction has sequential hull blasts, two airborne shockwaves, long-lived fire/smoke, and eight physical hull sections. |
| Red Fortress | Snow-covered city, river, textured palace, clock tower and domed cathedrals | Destroy three radar sites and the command palace. |
| Operation Snowglobe | North Pole workshops, decorated trees, candy canes and presents | Destroy three workshop power cores to drop giant Santa’s shield, then defeat him. A 326-metre striped pole with a gold finial marks the exact map center; elf AA crews and flying sleigh/reindeer teams defend the workshops. |
| Steel Tide | Open ocean and offshore platforms at dusk | Destroy four oil rigs under aerial and platform defenses. |

The BikeMesa-style mission grid shows all eight previews. Choose with the stick or arrows and launch with A/Enter, or click a mission to fly immediately. Play, Controls and Settings are visible buttons. Pause, victory and defeat use explicit action lists; Mission Select returns to the grid. B/Escape resumes a paused sortie and returns from submenus. Victory also offers A/Space to launch the next scenario. Inland terrain continues beyond the flight boundary to the fog horizon; ocean scenarios retain the continuous horizon water surface.

The scene uses Terrain3D with photographic material layers and anisotropic filtering, procedural atmospheric sky, Water3D, cascaded sun shadows, HDR/AGX, temporal antialiasing and bloom. A paved airfield and roads connect hangars and an inland industrial settlement. The DEM tapers into coves and headlands on every side; its square tile edges stay underwater. A single ocean mesh keeps a dense wave grid around the island and stretches its outer rows to the horizon. It uses one surface and reflection treatment throughout, without overlapping water patches. Distant wave normals fade before their glints become thin streaks. Twin translucent violet/pink exhaust plumes intensify under afterburner. Cannons have bright tracer cores and muzzle flashes. Missiles lock onto aircraft and buildings; destroyed structures leave charred foundations and layered fire/smoke. Large fuel explosions develop a rising stem and billowing cap. Swept fuselage and wing collision probes detect building impacts, destroying both the aircraft and the struck structure while leaving gaps between buildings flyable. Crashes get a 5.5 second impact camera, textured fire, dust, sparks and six actual aircraft sections simulated with Box3D, including terrain contacts and tumbling.

Dense GPU-instanced settlements add farms, alpine villages, canyon compounds, city blocks, woodland and North Pole outbuildings. Each background building has an individual swept collision volume and can be destroyed with weapons or a jet impact. Red Fortress has the Grand Palace, a clock tower with ruby star, a multi-dome cathedral, ministry buildings, gold-domed churches and crenellated perimeter walls. Sky Fortress uses afternoon light, a readable horizon, an artificial horizon instrument and off-screen objective guidance.

## Controls

| Action | Controller | Keyboard |
|---|---|---|
| Bank and pitch | Left stick / D-pad | Arrows / WASD |
| Stunts: rolls / loops | Hold right shoulder + stick | Hold R + arrows/WASD; Q / E rolls |
| Look around | Right stick | — |
| Guns | South (A) / Right trigger | Space / Return |
| Select mission on title | D-pad / left stick | Arrow keys / click mission |
| Mission selection | Choose Mission Select | Choose Mission Select |
| Start sortie / retry | South (A) | Space / Return |
| Cycle target | North (Y) | I |
| Flares (7-second recharge) | Right stick press (R3) / Left trigger | F |
| Guided missile (tap to queue / hold to repeat) | West (X) | M |
| Afterburner | East (B) | Shift |
| Airbrake | Left shoulder | Left Ctrl |
| Pause | Start | P |
| Settings from title or pause | North (Y) | I |
| Back from settings | East (B) | Escape |

Standard flight keeps steering, pitch and visual bank independent: pulling up raises the flight path even while banked or inverted. Normal pitch stops at 85 degrees. Releasing pitch levels the nose after 0.25 seconds even while turning. Hold RB (keyboard R) for continuous loops and barrel rolls; releasing it returns to assisted flight. Expert mode keeps body-axis controls. Afterburner is optional for stunts. Fine aiming, tighter turns, 600 m/s afterburner and 135 m/s airbraking retain their existing response. The control kernel is in `src/flight-controls.h`; `node tools/test-flight-math.mjs` compiles that exact C code to WASM with the bundled SDK and tests recovery at all banks/headings and cruise/boost speeds without rendering.

Each sortie starts with eighteen missiles and rechargeable flares. Press R3, LT, or F to release a salvo; flares recharge after seven seconds and the HUD shows readiness. Guns have a lead cue; missile locks can be selected manually. Enemy missiles launch no more often than once every twelve seconds, with only one unresolved missile threat at a time and a minimum 900-metre launch distance. Their 28-degree-per-second turn limit allows committed turns to beat them. Direction, distance, estimated closing time, and a terminal break cue accompany accelerating alert tones; break turns, flares, and dense cloud can defeat their heat seekers. Flares draw missiles off course, while cloud removes guidance: maneuver after entering cover. Losing guidance does not immediately clear the warning: a missile is only announced defeated once it is safely passing away. Missile impacts sweep both the jet and the projectile through the tick. Enemies use attack passes, extensions and defensive breaks instead of continuously circling the player. Four fuel storage sites can cause nearby secondary destruction. Wingtip condensation appears under high load or in cloud, and close jet passes produce a brief flyby sound.

Eight scattered cumulus clouds use noisy, sunlit volumetric fog: you can fly through them, with visibility reducing inside. World-space wind and disabled fog-grid jitter keep their drift steady as you approach. A denser Medium fog grid and cubic spatial reconstruction soften voxel edges without jittering the cloud density.

Settings offer saved Standard (bank and turn) / Expert (direct roll and pitch) controls, pitch inversion, Low/Medium/High graphics, and separate music/SFX volumes. All preferences save to SRAM, including migration from previous saves. Up/down selects a setting; South or left/right changes it. Output is 1920×1080, with graphics presets controlling internal rendering resolution and effects. The default Medium preset renders the world at 1440×810 with the HUD at output resolution.

Map-edge correction lasts at most 1.5 seconds, cancels as soon as you steer, pitch, or command a roll, and rearms only after returning inside. It corrects the flight path before movement and adds only upward pitch correction, so a sideways or inverted entry cannot turn a recovery command into a dive.

## Build

Sky Fighter is one of the games of this repository: `scripts/build.sh` at the repository root
builds it with the others (`scripts/build.sh skyfighter` builds only this one), natively into
`build/native/games/skyfighter/skyfighter` and as the dual GL + WebGPU cart
`build/cart-wgpu/carts/skyfighter.wasc`, against the pinned engine SDK or the engine from source
(`CARTWHEEL_ENGINE=../cartwheel-engine scripts/fetch-deps.sh`). The playtests under
`tools/playtests/` drive that cart on a romdev server at `http://127.0.0.1:7331` and write their
reports under the repository's `out/skyfighter-review/`. The program tables (`programs/`) are
regenerated with `tools/programs.sh` after a run names a missing state.

## Asset credits

- [F-15 Eagle model](https://www.innerscene.com/tools/library/3d-parts/f-15-eagle-fighter-jet-e1d11142) by Innerscene, CC0.
- [Aerial Rocks 02](https://polyhaven.com/a/aerial_rocks_02), [Aerial Grass Rock](https://polyhaven.com/a/aerial_grass_rock), [Snow 03](https://polyhaven.com/a/snow_03), [coast sand 01](https://polyhaven.com/a/coast_sand_01), [sand 03](https://polyhaven.com/a/sand_03), [factory wall](https://polyhaven.com/a/factory_wall), and [concrete wall 009](https://polyhaven.com/a/concrete_wall_009) from Poly Haven, CC0. `tools/build-terrain-layers.py` packs the terrain maps into engine layer textures.
- Inland relief uses [Mapzen/Tilezen Terrain Tiles](https://registry.opendata.aws/terrain-tiles/), sampled near Grindelwald. Europe elevation data produced using Copernicus data and information funded by the European Union — EU-DEM layers. This is a fictional coast: the relief is translated, vertically scaled and graded around the airfield. `tools/fetch-alpine-height.py` produces the offline height data.
- Afterburner / flyby audio: excerpts of [F101 / T-9 Jet Engine Test Cell](https://commons.wikimedia.org/wiki/File:F101_-_T-9_Jet_Engine_Test_Cell.ogv), Timothy Kirchner / U.S. Air Force, public domain. Loops are filtered, normalized and crossfaded. Used for afterburner and close flybys. The continuous cruise/wind bed and default channel hiss are disabled.
- Architectural facade atlas and runtime textures were generated with the built-in imagegen tool; source image and prompt: `assets/source-architecture/`. They supply red brick, pale palace stone, city stone and glass facades.
- `assets/effects/billow.png` was generated for this project. The identical `.sprite` copy is packed verbatim for the particle PNG decoder.
- Russo One and Oswald fonts, SIL Open Font License; license files are in `assets/fonts/`.

`docs/sky-fighter-screenshot.png` is a gameplay capture from romdev's wasmcart WebGPU host. Retained experimental sky/water images are not packed into the current cart.

## Rebuilding derived assets

`python3 tools/build-terrain-layers.py` downloads 4K Poly Haven sources and packs the runtime terrain layers. `python3 tools/build-wreck-parts.py` partitions the original F-15 triangles into independent sections and emits their collision bounds.

Engine audio preparation (existing ffmpeg):

```sh
ffmpeg -v error -y -ss 8 -t 10 -i assets/source-audio/f101-test-cell.ogv -vn -ac 1 -ar 32000 -af 'highpass=f=40,lowpass=f=1800' -f s16le /private/tmp/f101-idle.pcm
ffmpeg -v error -y -ss 34 -t 10 -i assets/source-audio/f101-test-cell.ogv -vn -ac 1 -ar 32000 -af 'highpass=f=45,lowpass=f=6500' -f s16le /private/tmp/f101-burn.pcm
python3 tools/build-engine-audio.py
```

## Handling and combat references

Standard bank-and-turn and optional direct roll/pitch controls follow the distinction described in [Bandai Namco’s Ace Combat controls guide](https://www.bandainamcoent.com/es_mx/news/8-steps-to-fly-towards-victory-in-ace-combat-7). Response shaping and limited smoothing draw on [War Thunder’s axis guide](https://wiki.warthunder.com/controls/4785-setting-up-control-axis-and-sensitivity). Player agency, varied air/ground encounters, tactical clouds and clear sound feedback are informed by the [Ace Combat 7 development interview](https://www.bandainamcostudios.com/en/behind-the-game/205). These are design references; Sky Fighter uses its own simplified flight and combat model.

Music: the user-provided `assets/audio/music/GallopingRiff.mp3` plays in stereo at 0.10 gain (−20 dB), fades in over two seconds, and ducks further under effects. It decodes on the game thread into a 256 KiB ring; the full eight-minute song stays encoded in memory. The real-time audio callback only reads ready samples. The audio playtest verifies playback, quiet music, exact silence at idle when music is muted, afterburner prominence and zero streaming underruns.

`node tools/playtests/playtest-missile-evasion.mjs` checks real AI launches, straight-flight impacts, left/right warning-timed evasions from ahead and behind, controller flare bindings, and recharge.

Cartwheel passes ABI v4 stick clicks and Guide into Cartwheel's input snapshot. The bundled wasmcart-sdl2 adapter exposes eight buttons and a d-pad hat, so these extra buttons need this bridge; R3 flare deployment is verified through romdev's actual controller input.

## Forests and landmarks

Forests use solid meshes for oaks, birches, spruces and pines, with two branching variants per species and varied age, height, width, rotation and foliage tint. Photo bark/foliage surface textures wrap real branches and irregular crown volumes. The foliage shader adds small leaf gaps, crown sway, flutter and restrained backlighting; no tree billboards or crossed planes are used. Near trees use detailed meshes; distant trees retain simpler 3D crowns, with 900/1100-metre switching hysteresis. Only near forest batches cast dynamic shadows. Winter fortress birches are leafless, and alpine/polar conifers carry snow.

Country Fair adds pitched roofs, textured field parcels, farmhouses and metal grain silos. Red Fortress has a frozen river, embankments and three bridges with real fly-through clearance; each clean underpass awards 500 points once per sortie. Operation Snowglobe has the central striped North Pole and a giant Santa boss beside the pole plaza, with an angry raised-fist idle and a tracking gaze. All three workshop power cores must be destroyed before any weapon or aircraft collision can damage Santa; mission completion additionally requires defeating him.

Tree material source and the complete built-in image generation prompt: [assets/source-trees/README.md](assets/source-trees/README.md). Geometry is reproducible with `python3 tools/build-tree-models.py`; the project-owned shader lives in `shaders/foliage.mjs`. The shared engine lighting composition allows this shader to compose with the engine’s sun and cascaded shadows. `node tools/playtests/playtest-map-details.mjs` checks both graphics backends, species/height variation, GPU wind, near/far geometry, bridge clearance and bonuses, central pole placement, Santa’s immunity, and cores-then-Santa completion.

Time Warp now has six cross-marked dirigibles at several heights along the alpine valley, with gentle forward drift. All six and eight prop fighters are required to clear the sortie.

Boss victories leave the view clear for the destruction: 14 seconds for Santa’s six physical body sections, 18 seconds for the mothership’s eight falling hull sections. Progress saves immediately; results wait until the aftermath finishes. The North Pole collapses into seven striped shaft sections and its gold cap. `node tools/playtests/playtest-destruction.mjs` checks the delayed results, physical movement, replay restoration, and both GPU backends.

## Cartwheel 0.14 rendering integration

Forests now support one, four or sixteen spatial cells per setting. The default retains one cell per species/LOD batch: profiling on the local GL host found that extra cell draws cost more than the culled geometry saved. Each cell is a procedural `MeshInstance3D`, with a fixed conservative sphere derived from the transformed geometry bounds of every actual tree in both LODs, plus a three-metre wind margin. This keeps LOD changes inside stable VisibilityManager bounds. Cartwheel v0.14.1 adds support for these fixed-bound instanced meshes; unbounded instances are not assumed to be safe. Mission groups retain ownership of visibility, and failed belowground placements are omitted from uploads. The renderer upgrade also brings streamed GL instance uploads and bounds-based opaque sorting.

Cannon impacts on surviving structures and destroyed foundations now receive pooled mesh decals in MIX mode. Marks inherit the receiving surface's lights, cascaded sun shadows and fog. Forty-eight slots cap memory and draw growth; launching a sortie clears the pool. Terrain extension meshes and instanced district buildings are not mesh-decal sources, so their existing explosion effects remain. ASCII glyphs for both UI fonts preload during loading. The corrected GL depth prepass is available from the engine but stays disabled by default: it increased render time in the controlled local four-cell comparison.

`node tools/playtests/profile-render-upgrade.mjs` compares one, four and sixteen forest cells and toggles the depth prepass using engine CPU counters. These host measurements include command submission and are not portable GPU timings. The `forest.grid` debug field selects one, two or four cells per axis before a mission is first created. `playtest-upgrade.mjs` explicitly selects four per axis to validate the experimental culling path; normal gameplay retains the original batch count.

`node tools/playtests/playtest-upgrade.mjs` checks four viewing directions, visible and culled forest cells, cached mission switching, foundation decal geometry, ABI conformance and missing/GPU errors on GL and WebGPU. Reports and screenshots are under `out/skyfighter-review/engine-upgrade/`.

Local profiling (20 steady frames, same Country Fair camera and deterministic inputs) produced these engine render-phase CPU/submission times. GL includes this host's translation costs; the figures are not GPU timings or Batocera performance predictions.

| Forest configuration | Draws | GL render ms | WebGPU render ms |
|---|---:|---:|---:|
| One cell, manager culling off | 453 | 127.70 | 7.03 |
| Four cells, culling on | 477 | 198.57 | 8.17 |
| Sixteen cells, culling on | 573 | 246.01 | 9.87 |
| Four cells, GL depth prepass on | 952 | 295.93 | — |

Both backends therefore keep the original unsplit batch count by default. Spatial subdivision and the corrected GL depth prepass remain available for future device-specific profiling, without imposing their measured overhead on normal gameplay. The complete figures are in `out/skyfighter-review/engine-upgrade/profile-report.json`.

The presentation pass gives each sortie its own sun angle, cool skylight and atmospheric density. Cloud emission is restrained so the existing sunlight can read through the volumes; the stable non-temporal froxel configuration remains unchanged. Terrain-following roads and fields now use sampled slope normals. Alpine snow reveals cool rock seams, while city blocks use varied facade values, dark roofs and planted interiors without changing collision geometry or building placement.

The HUD keeps mission progress and navigation together at the upper left, uses the existing Russo face for flight numerals, and reserves detailed captions for the selected contact. Distant balloon baskets and redundant labels no longer obscure the landscape. Existing warnings, lead cue, radar, target cycling and weapon readiness remain available.

`node tools/playtests/playtest-art.mjs` captures all eight sorties on WebGPU with deterministic launch views, checks ABI conformance and missing/skipped/GPU error counters, and saves environment settings plus host CPU counters under `out/skyfighter-review/art/`. `SKY_ART_BACKEND=gl SKY_ART_LEVELS=0,3,4 node tools/playtests/playtest-art.mjs` repeats representative scenes on GL. The developer-only `art.hide_hud` switch supports clean mission-thumbnail captures and is never saved as a user preference.

A matched 30-frame Country Fair comparison against the pre-art Cartwheel 0.14 cart measured 8.277 ms before and 8.269 ms after in the engine render CPU/submission phase, with identical 698 draw calls, 1,647,026 triangles and 24 programs. This is a local host comparison, not a GPU or Batocera frame-rate prediction. `out/skyfighter-review/art/baseline-comparison.json` records the measurements. It also demonstrates that the old combat fixture’s neutral dive selected target 16 on both builds after recovering pitch, so the combat test now uses a clear approach to the actual Fuel Chain depot instead.

Controls and audio audit: see `CONTROLS-AUDIT.txt`. Guided missile taps now survive acquisition/reload, partial RT/LT presses use their declared 0.35 threshold, burner exhaustion requires release/recharge instead of pulsing, and simultaneous airbrake overrides burner. The canonical cartwheel-engine audio-ring fix prevents a long frame from overwriting its own audio burst. `tools/playtests/playtest-controls-audit.mjs`, `tools/playtests/playtest-keyboard-audit.mjs` and `tools/playtests/playtest-audio-continuity.mjs` exercise these behaviors in the actual local runtimes.

## Tests

`tests/skyfighter-gates.json` at the repository root runs a sortie from a pad script (the selected
mission launched from the title, a bank, a pull up, the afterburner) and compares the flight state
with a control that turns the other way:

```sh
node tests/gate.mjs --gates-file skyfighter-gates.json --hosts native
```

Open: the gate runs natively only. On the cart hosts (romdev) the flight model drifts from the native
run in the sixth significant digit after 360 frames (`jet.x`, `jet.heading`), so the cross-host leg
the other games have is still to do (the simulation needs the engine's deterministic math throughout).
The playtests under `tools/playtests/` cover the rest on a romdev server.
