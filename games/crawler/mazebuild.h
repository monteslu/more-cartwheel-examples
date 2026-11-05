/* crawler floor builder: a floor's still scenery built under one node, the way the game and the bake tool
 * (tools/levelview) both draw it, so a bake made by the tool applies in the game (a lightmap matches its meshes by
 * node path and geometry).
 *
 * Under a Node3D "Level" added to the given parent:
 *   - "Light": the tileset's light scene (scenes/th_<theme>.cwscene: Sun, Rim, Env, Post);
 *   - "Floor": the ground plane over the whole maze (STATIC, its uv0 the lightmap layout), the tileset's floor
 *     texture, slow mottling in the vertex colours;
 *   - "Wall0".."Wall2": the wall tiles that face open ground, one STATIC instanced mesh per variant of the tileset's
 *     wall kit (kit_<theme>_wall.cwchar frames 0..2), each turned a quarter turn by the tile's hash;
 *   - "Rock": the solid mass between walls (tiles with no open neighbour) and the dark round the maze, one merged
 *     mesh of flat tops at the walls' height;
 *   - "Torch0/1", "Decor0/1": the props (surface and glow groups), STATIC instanced;
 *   - "BakeLights": the floor's light records as bake-only lights, only while baking.
 * Doors, breakable and trap walls, spawners, exits and everything that moves are the view's (view.c). */
#ifndef CRAWLER_MAZEBUILD_H
#define CRAWLER_MAZEBUILD_H

#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/lightmap.h"
#include "cartwheel/scene.h"
#include "maze.h"
#include "three.h"

#define MB_WALL_H 1.2f /* the walls' height (the kit's), the rock's top: low, so the maze reads as a tile layout from above */
enum { MB_VARIANTS = 3 };

/* a .cwchar read into geometries: frame f of group g (group 0 the lit surface, 1 the glow) */
enum { MB_MAXF = 24, MB_MAXG = 2, MB_MAXC = 4 };
typedef struct mb_chr { int nclip, ngroup, nf[MB_MAXC], first[MB_MAXC], total; t3_geometry *geo[MB_MAXG][MB_MAXF]; float lo_y, hi_y, rad; } mb_chr;
bool mb_chr_load(mb_chr *c, const char *path); /* false (logged) when it cannot be read */
void mb_chr_free(mb_chr *c);
void mb_darken_tops(mb_chr *c, float k); /* a wall kit's up-facing vertices take k of their colour */
t3_color mb_lin(float r, float g, float b);

typedef struct mb_built {
  cw_node node;      /* "Level" */
  const maze *m;
  int theme;
  t3_instanced_mesh *wall[MB_VARIANTS]; /* every wall tile facing open ground, by variant (the view culls them) */
  int nwall[MB_VARIANTS];
  uint8_t wx[MB_VARIANTS][MZ_MAX * MZ_MAX], wy[MB_VARIANTS][MZ_MAX * MZ_MAX], wr[MB_VARIANTS][MZ_MAX * MZ_MAX];
  t3_instanced_mesh *torch, *torch_glow, *decor, *decor_glow;
  t3_material *wall_mat;
  float wall_sy; /* the walls' height scale: MB_WALL_H over the kit's (1 for a kit made at that height) */
  cw_lightmap_data *lm;
} mb_built;

/* Build the floor under parent. The lightmap is not touched: call mazebuild_lightmap after the tree has run a
 * frame (the light scene places its sun). */
bool mazebuild_build(cw_node parent, const maze *m, mb_built *out);
/* Bake the floor and write bake_path (.cwlm) and its .hash beside it when bake_path is set (native), else load
 * the floor's shipped bake; then apply it (the floor draws BAKED). text: the floor's .cwmaze text (for the hash).
 * false when neither worked. */
bool mazebuild_lightmap(cw_tree *t, mb_built *b, const char *bake_path, const char *text);
/* The baked light a thing standing at (x, z) takes, as a colour multiplier (1 where nothing was baked). */
t3_color mazebuild_light_at(const mb_built *b, float x, float z);
void mazebuild_free(mb_built *b);
/* The bake's fingerprint: maze_bake_hash of the floor's text folded with the bytes of the tileset's wall, torch
 * and decor kits (a changed kit makes every bake of its tileset stale). tools/check-levels.mjs computes the same. */
uint64_t mazebuild_bake_hash(const char *text, int theme);
/* the tileset's kit files */
void mazebuild_kit(int theme, const char *part, char *out, int len);

#endif
