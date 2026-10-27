/* hordeshooter: shared declarations. sim.c is the rules (deterministic,
 * fixed tick, cartwheel math); view.c draws them (render only); fx.c is the
 * explosions, fire, smoke, gibs and decals (render only). */
#ifndef HORDE_GAME_H
#define HORDE_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/nav.h"

#define MAXP 4
#define MAXE 3600     /* enemies alive at once, at most */
#define MAXB 1200     /* bullets */
#define MAXO 260      /* obstacles (crates, barrels, walls, pillars) */
#define MAXG 1400     /* pickups */
#define MAXX 96       /* explosions waiting */
#define MAXEV 4096    /* events for the effects */
#define HALF 38.0f    /* the arena is 2 * HALF metres square */
#define FW 80         /* flow field grid (1 m cells) */
#define TICK 60.0f

enum { EK_RUNNER, EK_BRUTE, EK_BLOATER, EK_BOSS, EK_KINDS };
enum { OK_CRATE, OK_BARREL, OK_WALL, OK_PILLAR, OK_KINDS };
enum { EV_SHOT, EV_HIT, EV_KILL, EV_BOOM, EV_CRATE, EV_BARREL, EV_WALL, EV_GEM, EV_HURT, EV_LEVEL, EV_WAVE, EV_BOSS, EV_DOWN, EV_ROCKET };

typedef struct input { float mx, my, ax, ay; bool fire, bomb; int pick; /* 1..3 takes that upgrade offer, 0 none */ } input;

typedef struct player {
  bool active, down;
  float x, y, vx, vy, aim, hp;
  int cd, bomb_cd, inv, score, kills, level, xp, xp_next, down_ticks;
  /* upgrades */
  int rate, shots, dmg, pierce, rocket, speed, magnet, radius, armor;
  int last_up;
  int offer[3], offer_ticks, pending; /* an upgrade choice on screen: offer_ticks > 0 */
} player;

typedef struct enemy {
  bool alive;
  uint8_t kind;
  int hp, flash, atk, spawn_age;
  float x, y, kx, ky, face, speed;
  cw_crowd_agent agent;
} enemy;

typedef struct bullet { bool alive; uint8_t owner, kind; float x, y, vx, vy, dmg; int life, pierce; } bullet;
typedef struct obstacle { bool alive; uint8_t kind; float x, y, hx, hy; int hp, maxhp; uint32_t nav; } obstacle;
typedef struct pickup { bool alive; uint8_t kind; float x, y, vx, vy; int life; } pickup;
typedef struct boom { float x, y, r, dmg; int delay; uint8_t owner; bool big; } boom;
typedef struct event { uint8_t kind, a, b; float x, y, z, w; } event;

typedef struct sim {
  int tick, wave, wave_tick, players, state, state_ticks;
  int kills, killed_total, spawn_cap, boss_alive, boss_wave;
  cw_rng rng;
  player p[MAXP];
  enemy e[MAXE];
  bullet b[MAXB];
  obstacle o[MAXO];
  pickup g[MAXG];
  boom x[MAXX];
  int nenemies, nbullets, nboom_total;
  uint32_t hash;
  /* nav and flow */
  cw_navmesh nav;
  cw_crowd crowd;
  uint16_t flow[FW * FW];
  uint8_t blocked[FW * FW];
  int flow_age;
  bool nav_dirty;
  /* grid of enemies for hits (cells of 2 m) */
  int16_t cell_start[41 * 41 + 1];
  int16_t cell_idx[MAXE];
  /* events for the effects */
  event ev[MAXEV];
  int ev_n;
  float cam_x, cam_y; /* centroid, for the spawner */
  /* stats (not state) */
  float crowd_ms;
} sim;

enum { ST_PLAY, ST_OVER, ST_WON };
#define FINAL_WAVE 20 /* the boss of this wave ends the run */

void sim_init(sim *s, int players, uint32_t seed, int start_wave);
void sim_free(sim *s);
void sim_step(sim *s, const input in[MAXP]);
int sim_alive_players(const sim *s);
const char *sim_upgrade_name(int u);
/* team centre and the spread of the living players, for the camera */
void sim_team_box(const sim *s, float *cx, float *cy, float *hw, float *hh);

#endif

/* ── render side ─────────────────────────────────────────────────────── */
#include "cartwheel/scene.h"
#include "three.h"

t3_color lin(float r, float g, float b); /* screen colour -> linear */
/* view.c */
void view_init(cw_node root);
void view_free(void);
void view_sync(const sim *s, float time);
void view_camera(const sim *s, cw_node camera, float aspect, float shake_x, float shake_y);
typedef struct view_stats { int enemies_drawn, bullets, pickups; } view_stats;
view_stats view_get_stats(void);
/* fx.c */
void fx_init(cw_node root);
void fx_free(void);
void fx_events(sim *s); /* consumes the events */
void fx_tick(const sim *s);
void fx_shake(float *x, float *y);
typedef struct fx_stats { int systems_live, decals_live, lights_live, gibs_live, booms; } fx_stats;
fx_stats fx_get_stats(void);
extern int g_fx_level; /* 0..3 effects detail the fx module asked for (render only) */
