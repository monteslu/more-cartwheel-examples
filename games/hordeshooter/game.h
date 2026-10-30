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

enum { EK_RUNNER, EK_BRUTE, EK_BLOATER, EK_BOSS, EK_SKITTER, EK_BONEWALKER, EK_KINDS }; /* skitter: the sluice's; bonewalker: the ossuary's */
const char *sim_kind_name(int kind); /* the .cwlevel's name of a kind ("runner" ...) */
int sim_kind_by_name(const char *name); /* -1 when not a kind */
enum { OK_CRATE, OK_BARREL, OK_WALL, OK_PILLAR, OK_KINDS };
enum { EV_SHOT, EV_HIT, EV_KILL, EV_BOOM, EV_CRATE, EV_BARREL, EV_WALL, EV_GEM, EV_HURT, EV_LEVEL, EV_WAVE, EV_BOSS, EV_DOWN, EV_ROCKET, EV_POWER, EV_POWER_SPAWN, EV_SWIPE,
       EV_MELT, EV_CLEAR, EV_HATCH, EV_DESCEND, EV_FLOOR }; /* EV_MELT: one of the horde melting after its boss fell (a its kind); EV_CLEAR: the floor is clear;
       EV_HATCH: the hatch opens; EV_DESCEND: a player stepped in; EV_FLOOR: a new floor starts (a its index) */ /* EV_SWIPE: an enemy's strike (a kind, b 1 when it hit; z, w its facing) */

/* power-ups (pickup kind PK_POWER + type): temporary weapons, ammo, throwables and helpers */
enum { PU_SHOTGUN, PU_MINIGUN, PU_ROCKETS, PU_RAIL, PU_FLAME, /* weapons, 12 s */
       PU_EXPLOSIVE, PU_RAPID,                                /* ammo, 12 s */
       PU_GRENADES, PU_MINES,                                 /* charges */
       PU_SAWS, PU_SHIELD, PU_MAGNET,                         /* helpers */
       PU_KINDS };
#define PK_POWER 2
#define MAXM 32 /* mines on the ground */
#define PU_WEAPON_TICKS (60 * 12)
const char *sim_power_name(int t);
int sim_power_category(int t); /* 0 weapon, 1 ammo, 2 charges, 3 saws, 4 shield, 5 magnet */

typedef struct input { float mx, my, ax, ay; bool fire, bomb; int pick; /* 1..3 takes that upgrade offer, 0 none */ } input;

typedef struct player {
  bool active, down;
  float x, y, vx, vy, aim, hp;
  int cd, bomb_cd, inv, score, kills, level, xp, xp_next, down_ticks;
  /* upgrades */
  int rate, shots, dmg, pierce, rocket, speed, magnet, radius, armor;
  int last_up;
  /* power-ups held: a weapon and an ammo with their ticks left, grenades and mines, saws, shield, magnet */
  int pu_weapon, pu_weapon_t, pu_ammo, pu_ammo_t, grenades, mines, saw_t, shield_t, magnet_t;
  float saw_a;
  int offer[3], offer_ticks, pending; /* an upgrade choice on screen: offer_ticks > 0 */
} player;

typedef struct enemy {
  bool alive;
  uint8_t kind;
  int hp, flash, spawn_age, ph, hits, first_hit; /* hits taken and the tick of the first (stats only) */
  int atk, atk_cd, tgt; /* the melee attack: ticks into it (0: none), ticks before the next may start, the player it is aimed at */
  float x, y, kx, ky, face, speed;
  cw_crowd_agent agent;
} enemy;

typedef struct bullet { bool alive; uint8_t owner, kind, fx; float x, y, vx, vy, dmg; int life, pierce; } bullet; /* kind: 0 round, 1 rocket, 2 grenade, 3 rail, 4 flame; fx 1: explosive */
typedef struct mine { bool alive; uint8_t owner; float x, y; int arm; } mine;
typedef struct obstacle { bool alive; uint8_t kind; float x, y, hx, hy; int hp, maxhp; uint32_t nav; } obstacle;
typedef struct pickup { bool alive; uint8_t kind; float x, y, vx, vy; int life, value, age; } pickup; /* value: the xp a gem gives (gems close together merge) */
typedef struct boom { float x, y, r, dmg; int delay; uint8_t owner; bool big; } boom;
#define FINAL_WAVE 44 /* the run's waves: the last floor's boss wave */
#define NLEVELS_MAX 8
typedef struct event { uint8_t kind, a, b; float x, y, z, w; } event;

/* a floor's gameplay layout and waves (from its .cwlevel, read by the game; the yard's is built in code) */
enum { HZ_WATER, HZ_KINDS };
typedef struct floor_obstacle { uint8_t kind; float x, y, hx, hy; } floor_obstacle;
typedef struct floor_hazard { uint8_t kind; float x, y, hx, hy; } floor_hazard;
typedef struct floor_def {
  bool set;
  int nob, nspawn, npick, nhaz, nwave;
  floor_obstacle ob[MAXO];
  float spawn[16][3]; /* a gap the horde comes in through: x, y, half width */
  float pick[16][2];  /* power-up spots */
  floor_hazard haz[32];
  float share[8][EK_KINDS]; /* each wave's kinds, shares summing to 1 */
} floor_def;

typedef struct sim {
  int tick, wave, wave_tick, players, state, state_ticks;
  int level, level_wave; /* the floor (0..NLEVELS-1) and its wave (1..sim_level_waves); wave counts the run's waves */
  int kills, killed_total, spawn_cap, boss_alive, boss_wave, boss_hp, boss_maxhp, boss_phase; /* phase: 0 walking, 1 winding up a charge, 2 charging */
  cw_rng rng;
  player p[MAXP];
  enemy e[MAXE];
  bullet b[MAXB];
  obstacle o[MAXO];
  pickup g[MAXG];
  boom x[MAXX];
  mine m[MAXM];
  int pu_next, pu_spots;
  bool no_power; /* development: no power-ups at all (the A/B of the bot runs) */
  bool no_spawn; /* tests: the director spawns nothing (sim_spawn places the enemies) */
  float spot_x[16], spot_y[16];
  floor_def fl[NLEVELS_MAX]; /* each floor's layout, set by the game before it starts (none: the yard's, built in code) */
  /* power-up stats (not state): pickups per type, kills and player-ticks while a type was held */
  int pu_taken[PU_KINDS], pu_kills[PU_KINDS], pu_ticks[PU_KINDS], plain_kills, plain_ticks;
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
  /* time to kill, per wave (0..FINAL_WAVE) and kind, for the kills a player made: kills, hits taken, ticks from the first hit (stats only) */
  int ttk_n[FINAL_WAVE + 1][EK_KINDS], ttk_hits[FINAL_WAVE + 1][EK_KINDS], ttk_ticks[FINAL_WAVE + 1][EK_KINDS];
} sim;

enum { ST_PLAY, ST_OVER, ST_WON, ST_CLEAR, ST_HATCH, ST_DESCEND };
/* ST_CLEAR: the floor's boss fell and the horde melts; ST_HATCH: the hatch is open, waiting for a player to step in;
 * ST_DESCEND: the fade to the next floor */
#define NLEVELS 8          /* the descent: floors, one under the other (LEVELS.md) */
#define WAVE_TICKS (60 * 25) /* a wave; a boss wave lasts until its boss falls */
#define HATCH_R 1.7f       /* a living player this close to the hatch's middle steps in */
#define HATCH_OPEN_TICKS 60
#define MELT_TICKS 90
#define DESCEND_TICKS 60
int sim_level_waves(int level);  /* waves on that floor, the last its boss wave */
int sim_level_first_wave(int level); /* the run's wave number of that floor's first wave */
#define TETHER_HW 16.0f /* the living team box is at most 2 * this wide and 2 * TETHER_HH deep: one camera at its farthest (45 m) shows it */
#define TETHER_HH 8.5f

void sim_init(sim *s, int players, uint32_t seed, int start_wave);
void sim_free(sim *s);
void sim_step(sim *s, const input in[MAXP]);
int sim_alive_players(const sim *s);
void horde_bot(const sim *s, int i, input *in); /* bot.c */
const char *sim_upgrade_name(int u);
/* team centre and the spread of the living players, for the camera */
/* an enemy's melee attack as the view draws it: -1 none, 0 the wind-up, 1 the strike, 2 the recovery; *tell 0..1
 * how far into the wind-up (the telegraph) */
extern const char *const FLOOR_ID[NLEVELS]; /* the floors' ids (LEVELS.md): yard sluice ossuary ... */
struct lb_level;
int horde_floors_load(sim *s, struct lb_level *levels); /* each floor's .cwlevel that exists into s->fl (levels: also the parsed files, NLEVELS); then lays out the current floor; how many were read */
void sim_floor_apply(sim *s); /* after setting s->fl: lay out the current floor (its obstacles, spots) */
int sim_spawn(sim *s, int kind, float x, float y); /* tests: one enemy there; its slot or -1 */
void sim_dev_kill_boss(sim *s); /* development: the floor's boss falls now (player 1 gets it) */
int sim_melee_windup(int kind); /* the ticks of that kind's wind-up */
int sim_attack_frame(const enemy *e, float *tell);
void sim_power_give(sim *s, int pi, int t);
void sim_power_drop(sim *s, float x, float y, int t); /* hand a power-up to a player (pickups; development) */
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
extern float g_cam_dist; /* development: a fixed camera distance (0: the game frames the team) */
void view_floor(int level, const struct lb_level *lv); /* build that floor's look (0: the yard) */
int view_floor_now(void);
void view_hatch(const sim *s); /* the hatch's beam and ring of light, by the floor's state */
int view_attack_drawn(void); /* enemies drawn in an attack pose this frame */
void view_camera(const sim *s, cw_node camera, float aspect, float control_x, float control_y);
bool view_screen(t3_vec3 p, float *u, float *v); /* a world point on screen (0..1, y down) */
typedef struct view_stats { int enemies_drawn, bullets, pickups; } view_stats;
view_stats view_get_stats(void);
void view_set_sun(t3_vec3 dir); /* where the sunlight travels (world): the blob shadows' direction and length */
bool view_lightmap(cw_tree *t); /* bake (HORDE_BAKE) or load the yard's lightmap and apply it; once */
/* fx.c */
void fx_init(cw_node root);
void fx_free(void);
void fx_events(sim *s); /* consumes the events */
void fx_tick(const sim *s);
void fx_control_shake(float *x, float *y); /* zero, except under the camera gate's control (HORDE_SHAKE_CONTROL) */
typedef struct fx_stats { int systems_live, decals_live, lights_live, gibs_live, booms; } fx_stats;
fx_stats fx_get_stats(void);
extern int g_fx_level; /* 0..3 effects detail the fx module asked for (render only) */
