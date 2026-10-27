/* arenafps (working title): shared declarations. Units are metres, time is
 * fixed ticks (60 a second). Yaw 0 faces -Z; pitch up is positive. */
#ifndef ARENA_GAME_H
#define ARENA_GAME_H

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/nav.h"
#include "cartwheel/physics.h"
#include "cartwheel/physics3d.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/platform.h"
#include "cartwheel/project.h"
#include "cartwheel/quality.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "three.h"

#define TICK_DT (1.0f / 60.0f)
#define MAXH 4          /* humans */
#define MAXF 16         /* fighters: humans + bots */
#define NWEAP 4
#define EYE_H 1.6f
#define PLAYER_R 0.4f
#define PLAYER_HT 1.8f

typedef enum { MODE_DM, MODE_TDM, MODE_COOP, MODE_COUNT } game_mode;
typedef enum { ST_SETUP, ST_PLAY, ST_OVER } game_state;

static inline t3_vec3 v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static inline t3_vec3 v3add(t3_vec3 a, t3_vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline t3_vec3 v3sub(t3_vec3 a, t3_vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline t3_vec3 v3scale(t3_vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3dot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float v3len(t3_vec3 a) { return sqrtf(v3dot(a, a)); }
static inline t3_vec3 v3norm(t3_vec3 a) {
  float l = v3len(a);
  return l > 1e-6f ? v3scale(a, 1 / l) : v3(0, 0, 0);
}
static inline t3_vec3 v3cross(t3_vec3 a, t3_vec3 b) {
  return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

/* the direction a yaw and pitch look along */
static inline t3_vec3 look_dir(float yaw, float pitch) {
  float sy, cy, sp, cp;
  cw_sincos(yaw, &sy, &cy);
  cw_sincos(pitch, &sp, &cp);
  return v3(-sy * cp, sp, -cy * cp);
}

/* ── gfx.c: procedural PBR textures and a mesh builder ── */
enum { TEX_DIRT, TEX_CONCRETE, TEX_RUBBER, TEX_PAINT, TEX_RUST, TEX_STEEL, TEX_WOOD, TEX_GRAVEL, TEX_GRASS, TEX_CHAIN, TEX_GLASS, TEX_PRIMER, TEX_KINDS };
typedef struct tex_set { t3_texture *map, *normal, *rough, *ao; } tex_set;
const tex_set *tex_get(int kind);
void tex_apply(t3_material *m, int kind, float repeat, float normal_scale, float ao_intensity);
void tex_free_all(void);
extern uint32_t tex_hash;
t3_color lin(float r, float g, float b);
typedef struct mesh_build {
  float *pos, *nor, *uv, *col;
  uint32_t *idx;
  int nv, ni, cap_v, cap_i;
  bool colors;
} mesh_build;
int mb_vert(mesh_build *b, t3_vec3 p, t3_vec3 n, float u, float v);
void mb_color(mesh_build *b, int v, float r, float g, float bl);
void mb_tri(mesh_build *b, int a, int c, int d);
void mb_quad(mesh_build *b, int a, int c, int d, int e);
t3_geometry *mb_finish(mesh_build *b);
/* a box (centre, rotation, full size) with metre-projected uvs (one repeat per uv_m metres), tinted */
void mb_box(mesh_build *b, t3_vec3 centre, t3_quat q, t3_vec3 size, float uv_m, const float rgb[3]);

/* ── the arena (arena.c) ───────────────────────────────────────────── */

#define MAX_SPAWNS 16
#define MAX_ITEMS 24
enum { ITEM_HEALTH, ITEM_ARMOR, ITEM_SCATTER, ITEM_ROCKET, ITEM_RAIL, ITEM_AMMO, ITEM_KINDS };

typedef struct item {
  int kind;
  t3_vec3 pos;
  int respawn; /* ticks until it is back (0 = here) */
  cw_node node, light;
} item;

typedef struct arena_info {
  const char *name, *blurb;
} arena_info;
#define ARENAS 3
extern const arena_info ARENA_INFO[ARENAS];

typedef struct arena {
  int id;
  cw_world3d world;
  cw_navmesh nav;
  cw_node root;           /* every node of the level hangs here */
  t3_vec3 spawns[MAX_SPAWNS];
  int nspawns;
  item items[MAX_ITEMS];
  int nitems;
  t3_vec3 bounds_min, bounds_max;
  float kill_y;
  float nav_build_ms;
  int nav_polys;
} arena;
extern arena A;

void arena_build(int id, cw_node world_root);
void arena_free(void);
void arena_mood(int id, cw_node env, cw_node sun, cw_node sky, cw_node fog);

/* a ray through the level (not fighters): true on a hit */
bool arena_ray(t3_vec3 from, t3_vec3 to, t3_vec3 *point, t3_vec3 *normal);

/* ── fighters (main.c) ─────────────────────────────────────────────── */

typedef struct fighter {
  bool active, alive;
  int human; /* 0..3 for a person, -1 for a bot */
  int team;
  char name[12];
  cw_mover3d mv;
  float yaw, pitch;
  float kick;            /* view kick, radians, decays */
  int hp, armor;
  int weapon;
  int ammo[NWEAP];
  int have;              /* bit mask of owned weapons */
  int cooldown;
  int dead_ticks, spawn_guard;
  int score, deaths, streak;
  float bob;
  cw_node body;          /* the third-person body */
  cw_node light;         /* muzzle flash */
  cw_node leg[2];        /* hip pivots, swung while walking */
  int flash;
  /* bot brain */
  float skill;           /* 0..1 */
  int think;             /* ticks to the next decision */
  int target;            /* fighter index or -1 */
  int react;             /* ticks before it may fire at a new target */
  int strafe_t, strafe_dir;
  t3_vec3 goal;
  t3_vec3 path[24];
  int npath, pidx;
  uint32_t ticket;
  int repath;
  int stuck;
  t3_vec3 last_pos;
  float aim_err_yaw, aim_err_pitch;
  int wander_wait;
} fighter;

#endif
