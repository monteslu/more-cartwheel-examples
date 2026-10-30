/* hordeshooter level builder: reads a level's .cwlevel (format in LEVELS.md, section 5) and builds the
 * level's scenery under one node, the way both the game and the levelview cart draw it. A lightmap
 * matches its meshes by node path and geometry, so a .cwlm baked from this build (levelview's
 * LEVEL_BAKE, or the game's HORDE_BAKE) applies to the same build anywhere.
 *
 * What the build makes, under a Node3D named "Level" added to the given parent:
 *   - the level's lighting scene (scenes/<id>.cwscene: Sun, Rim, Env, Post, sky) instanced as "Light";
 *   - "Floor": the ground plane (STATIC, its uv0 the lightmap layout), albedo from the level's floor
 *     texture, macro mottling in the vertex colours;
 *   - per prop: "<name>0" (the prop's surface, STATIC instanced mesh of every placement) and "<name>1"
 *     (its glow group, if any: unlit vertex colours, not baked);
 *   - per hazard: a flat strip just above the floor ("Hazard<n>"; water baked, the glowing kinds not);
 *   - "Hatch": the door, one mesh whose frame is set by levelbuild_hatch;
 *   - the level's lights (SpotLight3D / OmniLight3D), bake_only ones in the lightmap only.
 * Gameplay data (obstacles, spawns, pickups, hazards, waves) is parsed for the sim to read; the
 * builder only draws. */
#ifndef HORDE_LEVELBUILD_H
#define HORDE_LEVELBUILD_H

#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/lightmap.h"
#include "cartwheel/scene.h"
#include "three.h"

enum { LB_MAXPROP = 40, LB_MAXAT = 640, LB_MAXLIGHT = 64, LB_MAXHAZ = 64, LB_MAXSPAWN = 16, LB_MAXPICK = 24,
       LB_MAXSHRINE = 8, LB_MAXWAVE = 8, LB_MAXKIND = 16 };

/* what a placement is to the sim (the prop line's <solid>) */
enum { LB_SCENERY = 0, LB_SOLID = 1, LB_WALL = 2, LB_CRATE = 3, LB_BARREL = 4 };

typedef struct lb_prop {
  char name[32], file[96], tx[16];
  float rep, rough, metal;
  int solid;
} lb_prop;
typedef struct lb_at { int prop; float x, y, z, yaw, hx, hz; } lb_at;
typedef struct lb_light {
  bool spot, bake_only;
  float x, y, z, tx, ty, tz, r, g, b, energy, range, angle, penumbra;
} lb_light;
typedef struct lb_hazard { char kind[12]; float x, z, hx, hz; } lb_hazard;
typedef struct lb_wave { int n; char kind[LB_MAXKIND][16]; float share[LB_MAXKIND]; bool boss; } lb_wave;

typedef struct lb_level {
  char id[24], name[64], scene[96], lightmap[96], floor_tex[96], monster[96], boss[96], boss_name[64], door[96];
  float floor_tile, floor_rough, floor_tint[3], door_x, door_z;
  int waves;
  int nroster;
  char roster[LB_MAXKIND][16];
  lb_wave wave[LB_MAXWAVE];
  int nprop, nat, nlight, nhaz, nspawn, npick, nshrine;
  lb_prop prop[LB_MAXPROP];
  lb_at at[LB_MAXAT];
  lb_light light[LB_MAXLIGHT];
  lb_hazard haz[LB_MAXHAZ];
  float spawn[LB_MAXSPAWN][3]; /* x, z, half width */
  float pick[LB_MAXPICK][2];
  float shrine[LB_MAXSHRINE][2];
} lb_level;

/* Parse assets/levels/<id>.cwlevel (an asset path). false (logged) when it cannot be read. */
bool levelbuild_parse(const char *asset_path, lb_level *out);

/* The material a prop's surface draws with. The default (NULL) is a white standard material with
 * vertex colours, the given roughness and metalness. A game that dresses props with its own textures
 * passes its own; the colour must stay white (the bake reads the material colour as the albedo). */
typedef t3_material *(*lb_material_fn)(const char *tx, float rep, float rough, float metal, void *user);

typedef struct lb_built {
  cw_node node;             /* "Level" */
  const lb_level *lv;
  t3_mesh *hatch;           /* the door's mesh (its geometry is the current frame) */
  t3_geometry *hatch_geo[2][8];
  int hatch_frames, hatch_groups;
  t3_mesh *hatch_glow;
  cw_lightmap_data *lm;
} lb_built;

/* Build the level under parent. The lightmap is not touched: call levelbuild_lightmap after the tree
 * has run a frame (the sky places the sun). */
bool levelbuild_build(cw_node parent, const lb_level *lv, lb_material_fn mat, void *user, lb_built *out);
/* The hatch, t from 0 (shut) to 1 (open): picks the nearest of its frames. */
void levelbuild_hatch(lb_built *b, float t);
/* Bake the level (its node's subtree) and write the .cwlm to bake_path when it is not NULL, else load
 * lv->lightmap; then apply it (the floor and the props draw BAKED). false when neither worked. */
bool levelbuild_lightmap(cw_tree *t, lb_built *b, const char *bake_path);
/* Free the level's node and its lightmap. */
void levelbuild_free(lb_built *b);

#endif
