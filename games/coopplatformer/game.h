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
typedef struct gate { int x0, x1, need, flag; } gate; /* an ability gate: columns x0..x1 cannot be crossed without `need`; flag = the last flag before it */
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
  int ncoins, npickups, nchecks, nenemies, nplat, ngates;
  gate gates[8];
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
  int rescue_timer, off_screen, retry_t;
  float draw_x, draw_y;          /* interpolated feet, set at render */
  int anim;                      /* 0 idle 1 run 2 rise 3 fall 4 wall 5 swim 6 dash */
  float run_phase, squash;
  int land_fx, jump_fx, hurt_fx; /* one-frame event flags for the renderer */
} player;

typedef struct input_state { float x, y; bool jump_pressed, jump_held, dash_pressed, down, retry; } input_state;

typedef struct camera_rig {
  float x, y, z;       /* world focus and distance */
  float tx, ty, tz;
  float px, py, pz;    /* x, y, z at the start of this tick: the renderer draws between them (alpha) */
  float vx, vy, vz;    /* follow velocity (a critically damped spring with bounded acceleration) */
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
  int checkpoint;      /* the flag respawns go to (the last one touched) */
  uint32_t reached;    /* bit k: flag k has been touched (the map can warp to it) */
  int impacts;        /* boss landings, hits and blasts, a player hurt: the impacts of the shake check (g.camoffset) */
  struct { float low, high; int ms; } rumble[MAX_PLAYERS]; /* this tick's rumble for each player's pad (sim_rumble):
                       * the renderer sends it and clears it; presentation only, never read by the rules or the hash */
  int hitstops;        /* how many hit-stops have happened (a counter for tests) */
  int hitstop;         /* ticks the whole world stands still after a hit (stomp, hurt, boss down) */
  int hit_flash;       /* ticks of screen flash the renderer shows after a hit */
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
void world_update_backdrop(float time);
int world_water_spans(float out[][4], int cap); /* x0, x1, bottom, surface */
int world_lava_spans(float out[][4], int cap);
#define MAX_TORCHES 24
extern bool g_cam_nofloor; /* sim.c: the camera may look under the world (a gate control only) */
int world_torches(float out[][4], int cap); /* braziers in the caves and ruins: x, flame y, z, region */
void world_surface_audit(int out[3]); /* surfaces with a PBR material, with a plain colour, seam strips drawn */
int world_uv_repeat(void); /* percent of wall corners whose texture phase repeats every 4 m */
bool world_flame_seated(float x, float y, float z); /* the flame burns in a seated brazier's basket */
int world_platform(int biome, float w, int seed, t3_mesh **out); /* a moving platform's meshes, built around its centre */
void world_backdrop(game *g);
int world_scenery_check(const level *L, int *checked, float *worst_mm); /* seated props off their ground (0 = none) */
bool world_solid(const level *L, int x, int y);
bool world_break_crack(game *g, int x, int y);
void world_hide_crack(int x, int y);

/* sim.c */
void sim_reset(game *g, int players);
void sim_step(game *g, const input_state in[MAX_PLAYERS], float dt);
void sim_bot_input(const game *g, int p, input_state *in);
void sim_respawn_player(game *g, int p);
bool sim_warp(game *g, int flag);
void sim_return(game *g);          /* everyone back to the last flag (or the start): the way out of anything */   /* the living players go to a reached flag; false if it is not reached */
uint32_t sim_hash(const game *g);
/* Rumble (the camera never shakes): sim_rumble asks one player's pad for low (heavy) and high (light) motor 0..1 for ms;
 * sim_rumble_at a heavy impact at a world point (a boss landing, a boss hit, a blast), amount 0..1, felt by each player
 * by their distance from it (full within 4 m, nothing past 24 m). The strongest request of a tick wins. */
void sim_rumble(game *g, int p, float low, float high, int ms);
void sim_rumble_at(game *g, float x, float y, float amount);
#endif
