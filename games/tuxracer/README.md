# Tux Racer on cartwheel-engine

![Tux Racer: Tux sliding down Bunny Hill](../../docs/screenshots/tuxracer.jpg)

A **work-in-progress reimplementation of the classic [Tux Racer](https://tuxracer.sourceforge.net/)**
on [cartwheel-engine](https://github.com/monteslu/cartwheel-engine). Tux Racer 0.61 was written by
Jasmin F. Patry and Sunspire Studios (1999 to 2001) and released under the GNU General Public License
version 2; its source and data are on [SourceForge](https://sourceforge.net/projects/tuxracer/). This
game is the same idea rebuilt on the engine: Tux belly-slides down the original courses, collecting
herring against the clock, with the original penguin model, music and sound effects converted from the
GPL data, the sliding rebuilt on Box3D, and the look brought up to the engine's PBR terrain,
procedural sky, cascaded sun shadows, GPU particles and post effects. It is GPL-2.0-or-later, see
[LICENSE](LICENSE) and [CREDITS](CREDITS). Tux Racer is a trademark of Jasmin F. Patry.

What is in: the 13 courses of 0.61 (the five stock courses and the eight contributed ones), practice
on any of them in sunny, cloudy or night conditions with wind and mirroring, the Canadian Cup (Bunny
Hill, Twisty Slope, Bumpy Ride, four lives, the time and herring requirements per difficulty), the
walk-in intro, the speedometer and the energometer, best scores saved per course. The physics constants,
camera and rules are ported from the original source. What is still open is in this README's last
section.

## Playing

Run `tuxracer` (from the repository root: `scripts/build.sh tuxracer`, then
`build/native/games/tuxracer/tuxracer`, or the cart `build/cart-wgpu/carts/tuxracer.wasc` with
`npx wasmcart`). Title: Practice (pick a course, the conditions, wind, mirror), Cup, Settings, Rumble on or off
(also on the pause menu, saved), Credits.

| Action | Pad | Keyboard |
|---|---|---|
| Steer | left stick or d-pad, left / right | left / right arrows, J / L |
| Paddle (faster on the flat, flap in the air) | A (south) or up | up arrow, I |
| Brake | B (east) or down | down arrow, K |
| Jump (hold to charge, release to jump) | X (west) | E |
| Reset to the last checkpoint | Back | Backspace |
| Camera: behind / follow | left / right shoulder | 1 / 2 |
| Pause | Start | Escape |
| Quit the race | | Q |
| Menus: move, accept, back | d-pad or stick, A, B | arrows, Enter or Space, Escape |

Score = 100 × (par − time) + 200 × herring, as in the original. In the Cup a race is won when the
time, herring and score requirements are all met; a loss costs a life.

## Layout

| Path | Contents |
|---|---|
| `src/` | the game: `main.c` (app, flow, conditions, post), `course.c` and `course_objects.c` (terrain, trees, items, gates), `tux.c` and `tux_skin.c` (the model: the original tux.tcl hierarchy, skinned as one surface), `physics.c` (the Box3D body and the ported force model), `view.c`, `hud.c`, `fx.c` (spray, snowfall, sparkles), `audio.c`, `textures.c` (snow, ice and rock layers; the slide trail), `skyring.c` (the mountain ring, cloud deck and aurora), `tree_mesh.c`, `item_mesh.c` |
| `shaders/` | the game's own three.c shader parts (three.js TSL): snow surface detail, foliage wind, the flag, the cloud deck, the aurora, Tux's feathers; composed with the engine's lighting at run time |
| `tools/convert.mjs` | the converter: the SGI `.rgb` decoder, a Tcl subset for `tux.tcl`, the course rasters to uniform height grids |
| `tools/` (the rest) | the sky ring from the cube faces and a photograph, the sprig and cloud textures, the colour grades |
| `scripts/fetch-tuxracer.sh` | fetches the original 0.61 source and data (sha256-pinned) into the repository's `deps/tuxracer`, for the converter |
| `scripts/programs.sh` | regenerates the program tables with the shader parts |
| `assets/` | what the converter wrote (courses, model, music, sounds, original textures) plus the generated sky, LUTs and sprig, the fonts, `roots.txt` (the pack manifest), `credits.txt` (the in-game credits) |
| `programs/` | the three.c program table (`tuxracer.txt` and the generated C) |

The gates are `tests/tuxracer-gates.json` at the repository root (a Bunny Hill run from a pad script,
the Box3D slide compared field for field on every host):

```sh
node tests/gate.mjs --gates-file tuxracer-gates.json --hosts native,romdev
```

## Regenerating the data

```sh
games/tuxracer/scripts/fetch-tuxracer.sh   # the original 0.61 tarballs into deps/tuxracer
node games/tuxracer/tools/convert.mjs      # deps/tuxracer -> games/tuxracer/assets (committed; deterministic)
```

The converter is idempotent: a second run changes nothing. The heights use the original formula
(`(v - base) / 255 * elev_scale - distance * tan(angle)`), resampled to a one-metre grid; the terrain
types and the objects (trees, herring, flags, the start and finish) come from the original rasters.

## Engine features used

Terrain3D with splat layers and physics height fields, a Box3D sphere for Tux with the original force
model applied before each step, ProceduralSky with cascaded sun shadows, HeightFog, GPUParticles3D for
the spray and snowfall, a Decal3D shadow under Tux, PostProcess with AgX, bloom, screen-space
reflections on the ice, a colour grade LUT per condition, depth of field on the menus, the walk-in and
the results, pad rumble on tree hits, landings, pickups and rock, a rim light on the herring, skin
shading on Tux, and the game's own shader parts composed with the engine's lighting.

## Open

- The visuals are a step beyond the original but not finished: the far course on the contributed
  courses, the night lighting and the tree variety are the next things to look at.
- Only the Canadian Cup exists (0.61 had one cup); more cups would be data.
- No split screen and no touch controls.
