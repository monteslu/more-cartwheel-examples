/* coopplatformer (working title): a co-op side-scrolling platformer. 1-4 players share one camera that zooms to
 * keep them together; a player who falls too far behind is carried to the group in a bubble.
 *   world.c   the level: tile grid, generator, colliders, meshes, textures
 *   sim.c     the rules: players, enemies, hazards, water, pickups, camera, save
 *   main.c    the app: shell, input, scene, lights, post, HUD, audio
 * Units: 1 tile = 1 metre, x right, y up, z toward the camera. */
#ifndef COOP_GAME_H
#define COOP_GAME_H

#include <stdint.h>
#include <stdbool.h>

#include "cartwheel/platformer2d.h"
#include "cartwheel/physics2d.h"
#include "cartwheel/scene.h"
#include "three.h"

#define WORLD_W 288
#define WORLD_H 64
#define MAX_PLAYERS 4
#define MAX_ENEMIES 48
#define MAX_COINS 160
#define MAX_PICKUPS 8
#define MAX_PLATFORMS 12
#define MAX_BITS 8

enum { T_AIR, T_ROCK, T_GRASS, T_ONEWAY, T_SPIKE, T_LAVA, T_WATER, T_CRACK, T_BRICK };
enum { BIOME_MEADOW, BIOME_CAVES, BIOME_RUINS, BIOME_VOLCANO, BIOMES };
enum { AB_DOUBLE = 1, AB_CLAWS = 2, AB_DASH = 4 };
enum { LAYER_WORLD = 0, LAYER_ONEWAY = 5, LAYER_PLAYER = 1 };

typedef struct pickup { int x, y, ability; bool taken; } pickup;
typedef struct coin { float x, y; bool taken; bool secret; } coin;
typedef struct checkpoint { int x, y; } checkpoint;
typedef struct mplat { float x0, y0, x1, y1, w; float t, speed; cw_body2d body; } mplat;
typedef struct enemy {
  int kind;            /* 0 walker, 1 flyer, 2 boss */
  float x, y, vx, vy, home_x, range;
  int hp, hurt, dir;
  bool alive;
  cw_mover2d m;
} enemy;

typedef struct level {
  uint8_t t[WORLD_H][WORLD_W];   /* [y][x], y up */
  uint8_t biome[WORLD_W];
  int ground[WORLD_W];           /* surface height of the main path per column (bot and camera hints) */
  int ncoins, npickups, nchecks, nenemies, nplat;
  coin coins[MAX_COINS];
  pickup pickups[MAX_PICKUPS];
  checkpoint checks[16];
  mplat plats[MAX_PLATFORMS];
  enemy enemies[MAX_ENEMIES];
  int start_x, start_y, goal_x;
  int boss_x;
  float water_current[WORLD_W];  /* horizontal current in water columns */
} level;

typedef struct player {
  bool joined, alive;
  cw_platformer2d pf;
  int hp, coins, invuln, bubble, color;
  float bubble_x, bubble_y;
  int rescue_timer, off_screen;
  float draw_x, draw_y;          /* interpolated feet, set at render */
  int anim;                      /* 0 idle 1 run 2 rise 3 fall 4 wall 5 swim 6 dash */
  float run_phase, squash;
  int land_fx, jump_fx, hurt_fx; /* one-frame event flags for the renderer */
} player;

typedef struct input_state { float x, y; bool jump_pressed, jump_held, dash_pressed, down; } input_state;

typedef struct camera_rig {
  float x, y, z;       /* world focus and distance */
  float tx, ty, tz;
  float shake;
} camera_rig;

typedef struct game {
  level L;
  cw_world2d world;
  player p[MAX_PLAYERS];
  int nplayers;
  uint32_t abilities;
  int tick;
  int coins_total, kills, deaths, rescues, secrets;
  int boss_hp, won, win_tick;
  camera_rig cam;
  uint8_t seen[WORLD_H / 4][WORLD_W / 4]; /* map exploration */
  uint32_t hash;
  int events;          /* bitmask of this tick's sound events */
  int checkpoint;
  int paused_map;
} game;

extern game G;

/* chars.c: skinned characters (glTF from tools/blender/characters.py) posed in code */
typedef struct rig rig;
enum { RIG_HERO, RIG_WALKER, RIG_FLYER, RIG_BOSS, RIG_KINDS };
rig *rig_load(int kind);
void rig_attach(rig *r, cw_node holder);
void rig_free(rig *r);
void rig_tint(rig *r, const float fur[3], const float scarf[3], int accessory);
void rig_flash(rig *r, float k);
void rig_glow(rig *r, float k);
void rig_pose_hero(rig *r, int anim, float ph, float t, float st, float wd, float vy);
void rig_pose_walker(rig *r, float ph, float t, float squash);
void rig_pose_flyer(rig *r, float ph, float t);
void rig_pose_boss(rig *r, float t, float rage);

/* world.c */
void world_generate(level *L, uint32_t seed);
void world_build_physics(game *g);
void world_destroy_physics(game *g);
void world_scene_build(game *g, cw_node root);
void world_scene_free(void);
void world_update_water_mesh(float time);
void world_backdrop(game *g);
bool world_solid(const level *L, int x, int y);
bool world_break_crack(game *g, int x, int y);
void world_hide_crack(int x, int y);

/* sim.c */
void sim_reset(game *g, int players);
void sim_step(game *g, const input_state in[MAX_PLAYERS], float dt);
void sim_bot_input(const game *g, int p, input_state *in);
void sim_respawn_player(game *g, int p);
uint32_t sim_hash(const game *g);
#endif
