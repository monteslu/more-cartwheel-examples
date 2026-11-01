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

#define WORLD_W 1024
#define WORLD_H 64
#define MAX_PLAYERS 4
#define MAX_ENEMIES 32
#define MAX_COINS 320
#define MAX_PICKUPS 8
#define MAX_PLATFORMS 16
#define MAX_BITS 8
#define MAX_THINGS 256
#define MAX_SIGNS 32
#define MAX_CHECKS 32
#define NWORLDS 5

enum { T_AIR, T_ROCK, T_GRASS, T_ONEWAY, T_SPIKE, T_LAVA, T_WATER, T_CRACK, T_BRICK, T_ICE, T_BELT, T_CRUMBLE, T_TILES };
enum { BIOME_MEADOW, BIOME_CAVES, BIOME_RUINS, BIOME_FROST, BIOME_VOLCANO, BIOMES };
/* what a level is made of besides tiles. Each has its rule in sim.c (things_*), its look in world.c (world_things_build) */
enum { TH_SPRING, TH_FAN, TH_SAW, TH_PRESS, TH_FALLER, TH_CANNON, TH_PLATE, TH_DOOR, TH_CRUMBLE, TH_BOOTH, TH_MIMIC, TH_BOOM, TH_FAKE, TH_CHASER };
/* how a player died, for the death gags (main.c): the big ones belong to the things that do them */
enum { DIE_SPIKE, DIE_LAVA, DIE_PIT, DIE_ENEMY, DIE_BOSS, DIE_SAW, DIE_PRESS, DIE_FALLER, DIE_COIN, DIE_CHASER, DIE_KINDS };
enum { AB_DOUBLE = 1, AB_CLAWS = 2, AB_DASH = 4 };
enum { LAYER_WORLD = 0, LAYER_ONEWAY = 5, LAYER_PLAYER = 1 };

typedef struct pickup { int x, y, ability; bool taken; } pickup;
typedef struct coin { float x, y; bool taken; bool secret; } coin;
typedef struct checkpoint { int x, y; } checkpoint;
typedef struct gate { int x0, x1, need, flag; } gate; /* an ability gate: columns x0..x1 cannot be crossed without `need`; flag = the last flag before it */
typedef struct thing {
  int kind;
  float x, y, w, h;  /* base position (feet level, column left edge for wide things), size */
  float a, b, c;     /* SAW range speed phase; PRESS period offset; CANNON vx vy; PLATE ticks open; FAN top */
  int link;          /* PLATE: the door it opens */
  float t, dy;       /* running state: FALLER drop, PRESS drop, DOOR rise, SAW x */
  int state;         /* FALLER 0 hung 1 falling 2 down; CRUMBLE 0 sound 1 shaking 2 gone; DOOR ticks left open */
  bool pressed;
  cw_body2d body;    /* DOOR and CRUMBLE: a kinematic box */
} thing;
typedef struct sign_t { float x, y; const char *text; bool secret; } sign_t;
typedef struct mplat { float x0, y0, x1, y1, w; float t, speed; cw_body2d body; } mplat;
typedef struct enemy {
  int kind;            /* 0 walker, 1 flyer, 2 boss */
  float x, y, vx, vy, home_x, range;
  int hp, hurt, dir, vx0_dir;
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
  checkpoint checks[MAX_CHECKS];
  thing things[MAX_THINGS];
  sign_t signs[MAX_SIGNS];
  int nthings, nsigns;
  float belt[WORLD_W];           /* conveyor speed of T_BELT tiles in each column */
  int boss_kind;                 /* which boss this level's arena holds */
  int width;                     /* the columns the level uses */
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
  float run;                     /* running momentum 0..1: built on the ground, kept through a jump */
  float jump_x, jump_y, jump_top; /* where the last jump left the ground and the highest the feet went (g.jump) */
  bool in_jump;
  int launched;                  /* ticks since a spring or cannon threw this player: the throw is not cut short */
  float launch_vx;
  int pancake;                   /* ticks of being flat: a friend landed on this head */
  float bot_anchor; int bot_stuck, bot_back; /* the autopilot: where it has been stuck and for how long */
  int carrying, carried_by;      /* player index + 1 of the friend held overhead, of the friend holding this one; 0 none */
  int die_cause;
  float run_phase, squash;
  int land_fx, jump_fx, hurt_fx; /* one-frame event flags for the renderer */
} player;

typedef struct input_state { float x, y; bool jump_pressed, jump_held, dash_pressed, run, down, retry, grab_pressed; } input_state;

/* Running (West held): the speed and the height of a jump build with momentum. A standing jump is the short one, a running
 * jump goes about twice as far and over a metre higher. RUN_BUILD seconds of running on the ground to full speed. */
#define WALK_SPEED 6.2f
#define RUN_SPEED 11.5f
#define JUMP_STAND 3.0f
#define JUMP_RUN 4.3f
#define RUN_BUILD 1.0f
#define RUN_DROP 0.3f

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
  float margin_jump, margin_run; /* the reachability check's margins on height and speed; 1 in play */
  float last_jump[3];  /* the last jump to land: distance, rise, speed at takeoff (the running-jump gate reads these) */
  float top_speed;     /* the fastest any player has run */
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
  int level;           /* the index in LEVELS */
  bool reach;          /* the reachability check is running: things stay still and doors stay open once pressed */
  bool reach_changed;  /* the check opened a door: its search starts again */
  int betrayals[MAX_PLAYERS]; /* landing on a friend's head */
  int throws[MAX_PLAYERS];    /* friends thrown */
  int fake_t;                 /* ticks left of the level pretending to have ended */
  int mimic_bites;
  struct { float x, y; int who, cause; } deaths_q[8]; /* this tick's deaths for the renderer's gags (not in the hash) */
  int ndeaths_q;
  struct { float x, y; int who, victim; } grief_q[4];
  int ngrief_q;
  int things_hit;      /* landings on things: springs and cannons (a counter for tests) */
} game;

typedef struct gen gen;
typedef struct level_info { const char *name; int world; void (*build)(gen *G); uint32_t abilities; const char *line; } level_info;
extern const level_info *const LEVELS;
extern int NLEVELS;
void levels_set_worlds(int worlds); /* the worlds in the table (dev.worlds: a sweep can see one before it joins) */
extern const char *const WORLD_NAMES[NWORLDS];

extern game G;
extern bool g_bot_no_run;

/* chars.c: skinned characters (glTF from tools/blender/characters.py) posed in code */
typedef struct rig rig;
enum { RIG_HERO, RIG_WALKER, RIG_FLYER, RIG_BOSS, RIG_KINDS };
rig *rig_load(int kind);
void rig_attach(rig *r, cw_node holder);
void rig_free(rig *r);
void rig_tint(rig *r, const float fur[3], const float scarf[3], int accessory);
void rig_flash(rig *r, float k);
void rig_shade(rig *r, const float k[3]);
void rig_glow(rig *r, float k);
void rig_pose_hero(rig *r, int anim, float ph, float t, float st, float wd, float vy);
void rig_pose_walker(rig *r, float ph, float t, float squash);
void rig_pose_flyer(rig *r, float ph, float t);
void rig_pose_boss(rig *r, float t, float rage);

/* world.c */
void world_generate(level *L, int index);
float world_near_height(const level *L, float x); /* the near layer's top at x */
void world_near_build(game *g);      /* the dark near layer in front of the play */
void world_things_build(game *g);    /* the things' meshes */
void world_things_sync(const game *g, float time); /* move them to where the rules have them */
int world_things_check(const level *L, int *checked); /* things with no floor under them (or no ceiling over a press) */
void world_build_physics(game *g);
void world_destroy_physics(game *g);
void world_scene_build(game *g, cw_node root);
void world_scene_free(void);
void world_update_water_mesh(float time, float cam_x);
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
extern bool g_level_old; /* the reachability gate's control */
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
bool sim_player_move(game *g, player *p, const input_state *is, float dt); /* one player's movement this tick; true in water */
bool sim_dash_cracks(game *g, player *p, bool hide);
int sim_hazard_at(const level *L, float fx, float fy);
float sim_lip_of(const level *L, float x); /* the highest ground near x */
bool sim_open_pit(const level *L, float x); /* nothing to stand on under x */
bool sim_coin_touch(const coin *c, float fx, float fy);
bool sim_pickup_touch(const pickup *pk, float fx, float fy);
/* reach.c: prove from real movement which coins, pedestals and rooms a player can get to (COOP_REACH=1) */
int reach_check(void);
/* Rumble (the camera never shakes): sim_rumble asks one player's pad for low (heavy) and high (light) motor 0..1 for ms;
 * sim_rumble_at a heavy impact at a world point (a boss landing, a boss hit, a blast), amount 0..1, felt by each player
 * by their distance from it (full within 4 m, nothing past 24 m). The strongest request of a tick wins. */
void sim_rumble(game *g, int p, float low, float high, int ms);
void sim_rumble_at(game *g, float x, float y, float amount);
#endif
