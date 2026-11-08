/* crawler: shared declarations. sim.c is the rules (deterministic, fixed tick, cartwheel math); view.c draws
 * them (render only); fx.c is the effects (render only); maze.c reads the floors (DESIGN.md has the design). */
#ifndef CRAWLER_GAME_H
#define CRAWLER_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/nav.h"
#include "maze.h"

#define MAXP 4
#define MAXE 3000     /* monsters alive at once, at most */
#define MAXB 768      /* shots in flight (the players' and the monsters') */
#define MAXG 640      /* things lying on the floor */
#define MAXSP MZ_MAXSPAWN
#define MAXX 96       /* blasts waiting */
#define MAXEV 4096    /* events for the effects */
#define MAXZ 48       /* clouds and burning patches on the floor */
#define FG (MZ_MAX * 2) /* flow field cells a side (1 m) */
#define TICK 60.0f
#define VIEW_HW 21.3f /* the screen's half width and half height on the floor, metres: 21.3 x 12 tiles of 2 m (DESIGN.md) */
#define VIEW_HH 12.0f
#define TETHER_HW 19.3f /* how far a player may be from the team's middle: the screen less a margin */
#define TETHER_HH 9.6f
/* a player who has not touched the controls for this long holds nobody back: the screen follows the others and
 * carries them along (sim_player_leads) */
#define IDLE_TICKS 300
#define IDLE_FADE 120 /* and the view glides off them over this long */
/* the most monsters on one screen (DESIGN.md, the crowd): the floor's spawners keep pouring, but at the cap the spawners
 * on screen hold back and the monsters off it wait at its edge, so a corridor reads as a line to shoot through, never a
 * carpet */
#define SCREEN_CAP 120

enum { EK_SHADE, EK_GHOUL, EK_IMP, EK_SLINGER, EK_HEXER, EK_DREAD, EK_PILFERER, EK_OGRE, EK_HOUND, EK_BLOATER, EK_SKITTER, EK_SPORELING, EK_KINDS };
const char *sim_kind_name(int kind);
int sim_kind_by_name(const char *name); /* -1 when not a kind */

enum { CL_BRUTE, CL_WARDEN, CL_MAGE, CL_RANGER, CL_KINDS };
const char *sim_class_name(int c);

/* charms (DESIGN.md's table order, which is also the charm model's frame order) */
enum { CH_TRIPLE, CH_RAPID, CH_PIERCE, CH_BOUNCE, CH_SEEKER, CH_CHAIN, CH_FROST, CH_EMBERS, CH_WARD, CH_REPULSE, CH_SHROUD, CH_LODESTONE,
       CH_FIREBOMBS, CH_HASTE, CH_GIANT, CH_FORTUNE, CH_KINDS };
const char *sim_charm_name(int c);
const char *sim_elixir_name(int e);

enum { EV_SHOT, EV_HIT, EV_KILL, EV_BOOM, EV_HURT, EV_DOWN, EV_REJOIN, EV_PICK, EV_KEY, EV_DOOR, EV_SPAWN, EV_SPAWNER_HIT, EV_SPAWNER_DIE,
       EV_MAGIC, EV_FOOD_BREAK, EV_POTION_BREAK, EV_WALL_BREAK, EV_TRAP, EV_TELEPORT, EV_EXIT, EV_FLOOR, EV_MELEE, EV_SWIPE, EV_ESHOT, EV_LOB_LAND,
       EV_DRAIN, EV_STEAL, EV_CLOUD, EV_CHARM, EV_ELIXIR, EV_CHEST, EV_STUN, EV_DOORS_OPEN, EV_SHATTER, EV_ARC, EV_BOUNCE, EV_TREASURE, EV_FIREBOMB, EV_AMBUSH };
/* event fields: a, b small ints, x y where, z w a direction or a second point (each emitter says) */
typedef struct event { uint8_t kind, a, b; float x, y, z, w; } event;

typedef struct input { float mx, my; bool fire, magic, magic_held, join; } input;

typedef struct player {
  bool active, down;
  uint8_t cls;
  float x, y, vx, vy, face; /* face: radians, atan2(dz, dx) in the floor plane */
  int hp, score, keys, potions;
  int cd, melee_cd, stun, inv, hurt_t, down_ticks, firing, shots_out;
  int elixir[EL_KINDS];
  int charm[CH_KINDS];   /* ticks left (firebombs: charges) */
  float ember_a;          /* the embers' orbit */
  int drained;            /* a dread holding on: health taken so far */
  int stolen;             /* what a pilferer took last (for the panel) */
  int at_exit;
  int idle;               /* ticks since this player last touched the controls (IDLE_TICKS: left behind) */
  int behind;             /* ticks left behind (the view glides off over IDLE_FADE) */
  /* this floor's tally (the card at its end): monsters killed, treasure taken (score), food eaten, food shot, keys */
  int fl_kills, fl_gold, fl_food, fl_shot_food, fl_keys;
} player;

typedef struct enemy {
  bool alive;
  uint8_t kind, str;
  int hp, flash, age, atk, atk_cd, tgt, act, act_t, act_next, slow, frozen, burn, hold, carry, spawner;
  int stag, stag_cd; /* staggered by a hit (ticks left; no second stagger until stag_cd runs out) */
  float x, y, kx, ky, face, speed;
  cw_crowd_agent agent;
} enemy;

/* shot kinds: the classes' (0 axe, 1 hammer, 2 fireball, 3 arrow), an imp's fireball (4), a slinger's lob (5),
 * a hexer's bolt (6), a firebomb (7) */
enum { SK_AXE, SK_HAMMER, SK_FIRE, SK_ARROW, SK_IMPFIRE, SK_LOB, SK_HEX, SK_FIREBOMB, SK_KINDS };
typedef struct bullet { bool alive; uint8_t owner, kind; int8_t bounce; float x, y, vx, vy, dmg, tx, ty; int life, pierce, age; } bullet;
/* owner: the player (0..3), or 255 a monster's */

typedef struct pickup { bool alive; uint8_t kind, arg; float x, y, vx, vy; int life, age; bool dropped; } pickup; /* kind IT_*, life 0: stays */
typedef struct spawner { bool alive; uint8_t kind, str, tx, ty; int hp, cd, nalive, pulse; float x, y; uint32_t nav; } spawner;
typedef struct boom { float x, y, r, dmg; int delay; uint8_t owner; bool big; } boom;
typedef struct zone { bool alive; uint8_t kind; float x, y, r; int life; } zone; /* 0 poison cloud, 1 burning lump */

enum { ST_PLAY, ST_EXIT, ST_OVER, ST_WON };

typedef struct sim {
  int tick, state, state_ticks, players, floor, floor_ticks, next_floor, nfloors;
  bool no_spawn;  /* tests: spawners make nothing */
  bool god;       /* tests: nobody takes damage */
  bool no_cap;    /* the perf gate's stress scene: no cap on the monsters on screen */
  bool idle_anchor; /* tests (the control): a player who never moves holds the team back as before */
  bool old_mow;   /* tests (the control): the monsters' health, shots and spawners as before the mowing rules */
  cw_rng rng;
  maze m;                       /* the floor as loaded */
  uint8_t t[MZ_MAX][MZ_MAX];    /* the tiles now (doors open, walls break, trap walls vanish) */
  uint8_t door_open[MZ_MAX][MZ_MAX]; /* ticks since a door tile opened (0 shut), for the view */
  uint8_t wall_hp[MZ_MAX][MZ_MAX];   /* a breakable wall's hits left */
  uint32_t tile_nav[MZ_MAX][MZ_MAX]; /* the obstacle that keeps the crowd off a door, breakable or trap wall */
  int16_t sp_at[MZ_MAX][MZ_MAX];     /* the spawner on a tile + 1 (0 none) */
  player p[MAXP];
  enemy e[MAXE];
  bullet b[MAXB];
  pickup g[MAXG];
  spawner sp[MAXSP];
  boom x[MAXX];
  zone z[MAXZ];
  int nsp, nenemies, kills, killed_total, doors_opened, pilfer_next;
  uint8_t ambush_done[MZ_MAXAMBUSH];
  float cam_x, cam_y; /* the team's middle (the camera's target) */
  float view_x, view_y; /* the screen's middle (the team's, held inside the maze as the camera holds it) */
  int on_screen;        /* monsters on the screen now (the cap, SCREEN_CAP) */
  uint32_t hash;
  /* stats (not state) */
  int carried; /* players left behind and carried to the team */
  int nboom_total, potions_used, food_eaten, keys_used, spawners_killed, hp_lost[MAXP], damage_taken, dmg_kind[EK_KINDS + 1], falls;
  float crowd_ms;
  /* nav and flow */
  cw_navmesh nav;
  cw_crowd crowd;
  int fw, fh;                   /* flow grid size (cells of 1 m) */
  uint16_t flow[FG * FG];       /* steps to the nearest living player */
  uint16_t eflow[FG * FG];      /* steps to the main exit (a pilferer's way out, and the bots') */
  uint8_t blocked[FG * FG];
  int flow_age;
  bool flow_dirty;
  /* the hit grid: monsters in cells of 2 m (one a tile) */
  int16_t cell_start[MZ_MAX * MZ_MAX + 1];
  int16_t cell_idx[MAXE];
  event ev[MAXEV];
  int ev_n;
} sim;

void sim_init(sim *s, int players, const int cls[MAXP], uint32_t seed);
/* start a floor (keeps the players' health, score, keys, potions, elixirs and charms) */
void sim_floor_start(sim *s, const maze *m);
void sim_free(sim *s);
void sim_step(sim *s, const input in[MAXP]);
int sim_alive_players(const sim *s);
bool sim_solid(const sim *s, int tx, int ty); /* a walker cannot enter this tile now (spawners included) */
bool sim_wall_at(const sim *s, float x, float y); /* a shot stops here */
int sim_spawn(sim *s, int kind, int str, float x, float y); /* tests: one monster there; its slot or -1 */
int sim_exit_way(const sim *s, float x, float y, float *dx, float *dy); /* the step toward the main exit round the walls */
int sim_melee_windup(int kind);
int sim_attack_frame(const enemy *e, float *tell); /* -1 none, 0 the wind-up, 1 the strike, 2 the recovery */
bool sim_hexer_hidden(const sim *s, const enemy *e);
void sim_team_box(const sim *s, float *cx, float *cy, float *hw, float *hh); /* the leading players' box */
/* a living player the team keeps to: one who plays, or anyone when nobody does (IDLE_TICKS) */
bool sim_player_leads(const sim *s, int i);
void sim_give_charm(sim *s, int pi, int c);
void sim_add_player(sim *s, int i, int cls); /* a player joins during play */
/* a team starting deep (the furthest tileset's first floor): an elixir a player for each tileset passed by, two
 * potions each */
void sim_head_start(sim *s, int floor);
int sim_enemy_hp(const sim *s, int kind, int str); /* a new monster's health */
void crawler_bot(const sim *s, int i, input *in); /* bot.c */
void crawler_bot_goal(int i, int *kind, int *x, int *y);

/* class numbers (DESIGN.md's table) */
typedef struct class_stats { float speed, armor; int shot_kind, shot_dmg, shot_cd, shots_max, melee, melee_cd, magic; float shot_speed; } class_stats;
extern const class_stats CLASS[CL_KINDS];

#endif

/* ── render side ─────────────────────────────────────────────────────── */
#ifdef CRAWLER_RENDER
#include "cartwheel/scene.h"
#include "three.h"
t3_color lin(float r, float g, float b); /* screen colour -> linear */
extern const float PCOL[MAXP][3];        /* the players' colours */
/* view.c */
bool view_init_step(cw_node root, int *done, int *total);
void view_free(void);
void view_floor(const sim *s);              /* build the floor the sim is on (call after sim_floor_start) */
bool view_lightmap(cw_tree *t, const char *bake_out); /* a frame after view_floor: load (or bake, native) its lightmap */
void view_sync(const sim *s, float time);
void view_camera(const sim *s, cw_node camera, float aspect);
bool sim_on_screen(const sim *s, float x, float y, float margin); /* a point on the screen (sim.c: the cap's screen) */
bool view_screen(t3_vec3 p, float *u, float *v); /* a world point on screen (0..1, y down) */
typedef struct view_stats { int enemies_drawn, walls_drawn, shots, pickups, cut; } view_stats;
view_stats view_get_stats(void);
t3_color view_light_at(float x, float z); /* the bake's light on a thing standing there (a colour multiplier) */
extern float g_cam_dist;
extern float g_tilt, g_lean; /* the view's tilt and the standing things' lean, degrees (view.c) */
extern float g_rim_k; /* the brightness of the crowd's kind-coloured edges (view.c) */
extern bool g_hide_rings; /* key-art captures: no team rings (view.c) */
/* fx.c */
void fx_init(cw_node root);
void fx_free(void);
void fx_events(sim *s); /* consumes the events */
void fx_tick(const sim *s, float time);
typedef struct fx_stats { int systems_live, lights_live, booms, rumbles; } fx_stats;
fx_stats fx_get_stats(void);
extern int g_fx_level;
#endif
