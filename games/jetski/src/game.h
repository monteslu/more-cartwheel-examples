/* jetski: wave racing on one water. Shared declarations. */
#ifndef JETSKI_GAME_H
#define JETSKI_GAME_H
#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/math.h"
#include "cartwheel/scene.h"
#include "cartwheel/terrain.h"
#include "cartwheel/water.h"
#include "three.h"

#define RACERS 6
#define LAPS 3
#define TAU 6.2831853f
#define LEVELS 5
#define RAMP_MAX 14
#define BUOY_MAX 64
#define RAMP_HALF_LEN_N 20.0f /* a rigid structure on piles: tools/make-models.mjs builds these sizes */
#define RAMP_HALF_W_N 9.0f
#define RAMP_BIG_XZ 1.5f    /* a big ramp's model is scaled so */
#define RAMP_BIG_Y 1.92f
#define RAMP_BIG_RISE 7.0f  /* and its top edge stands this much higher */
#define KICK_T 0.8f         /* a kick out in the air, seconds */
#define DIVE_FLOOR 1.0f     /* a diving hull stays this far over the bed */
#define RAMP_SLOPE 0.36f
#define RAMP_FOOT 50.0f     /* how far its piles reach under its top edge */
static const char *const RIDER_NAMES[6] = { "You", "Marlin", "Kestrel", "Tide", "Juno", "Reef" };
extern int g_difficulty; /* 0 easy, 1 normal, 2 hard */
enum { MODE_CHAMPIONSHIP, MODE_TIME_TRIAL };
extern int g_mode;

enum { RACE_ATTRACT, RACE_GRID, RACE_ON, RACE_DONE };

/* a course: what tools/make-course.mjs generates from courses/<id>.mjs (src/courses/) */
typedef struct course_scatter { const char *model, *far_model, *map; float density, smin, smax, range; int seed; bool sink; float tilt; } course_scatter;
typedef struct course_def {
  const char *id, *name;
  int quads;
  float spacing, minh, maxh, len;
  int track_n; const float (*track)[4];
  int buoy_n; const float (*buoys)[4];
  int ramp_n; const float (*ramps)[6]; float ramp_top;
  int scatter_n; const course_scatter *scatter;
  int model_n; const char *const *models; int placed_n; const float (*placed)[8]; /* model, x, z, yaw, scale, solid half length, half width, floats */
  int prop_n; const char *const (*props)[3];
  int layer_n; const char *layer[8]; float layer_uv[8];
  const char *heights, *control, *mask; int mask_n, map_n;
  const char *sky, *landmark; float landmark_at[3];
  float start[4];
  float waves[4][5], swell[2], tide[2];
  float sun_disc, rain; /* rain: 0 none, 1 a squall with lightning */
  float side_detile;    /* cliffs hide the rock layer's repeat: not a course prop, setting the node's property rebuilds the land */
  /* kind 1: an arch over a cut, shut while the tide stands high: see hook_craft */
  struct { int kind; float current, x, z, dirx, dirz, half_w, half_len, lintel, s_in, s_out; const char *model; } hook;
} course_def;
extern const course_def *const COURSES[LEVELS];
#define DEF (COURSES[G.level])
#define TRACK (DEF->track)
#define TRACK_N (DEF->track_n)
#define BUOYS (DEF->buoys)
#define BUOY_N (DEF->buoy_n)
#define COURSE_LEN (DEF->len)
#define RAMPS (DEF->ramp_n)
#define START_X (DEF->start[0])
#define START_Z (DEF->start[1])
#define START_DX (DEF->start[2])
#define START_DZ (DEF->start[3])

typedef struct craft_input {
  float steer, lean; /* -1..1; lean > 0: pulled back */
  float throttle;
  bool trick;
} craft_input;

typedef struct craft {
  float x, y, z, vx, vy, vz;
  float yaw, pitch, roll, pr, rr; /* pr, rr: pitch and roll rates */
  float lean, steer, load;        /* smoothed lean; smoothed steer; compression of the rider's legs (m/s of landing) */
  float px, py, pz, pyaw, ppitch, proll; /* last tick, for drawing between ticks */
  float wet, speed, surf;         /* share of hull in the water; horizontal speed; water height under the craft */
  float top;                      /* top speed now */
  bool air, big_splash;
  float air_time, air_rise;
  int trick_kind, last_trick;     /* 0 none, 1 barrel roll, 2 back flip */
  float trick_t, trick_dir;
  int meter;                      /* 0..5 */
  int next_buoy;
  int track_i;                    /* nearest track sample */
  int lap;                        /* laps done */
  float dist;                     /* metres of race done */
  float finish;                   /* seconds, when done */
  int place, misses, tricks, passes;
  float flash_ok, flash_miss, boost, grounded, kick;
  float skill;                    /* computer racers */
  float stock;                    /* turbos in hand, 0..3 */
  float turbo_t, turbo_hold;      /* the shove now, then the raised ceiling after it */
  int turbos;                     /* fired */
  int shown_place, want_place, place_ticks; /* the place a person is told: only once it has held */
  int place_changes;              /* held changes of place */
  int slow3_ticks, slow_max;      /* under 3 m/s, throttle or not */
  craft_input in;
  bool human;
  bool bump;                      /* hit something this tick */
  bool crashed;                   /* hit something square, hard */
  int ram_cool;                   /* ticks before another hit from a rival counts */
  bool on_ramp;                   /* on a ramp's deck this tick */
  bool hook_passed;               /* through the hook's far side, until it comes round again */
  bool trick_held;                /* the trick button last tick */
  bool in_turbo;                  /* the turbo button pressed this tick */
  bool in_kick;                   /* the kick button pressed this tick */
  bool leg_paid;
  float legout, leg_side;         /* a kick out: seconds left, which side */
  int kicks, dives, big_ramps;
  float dive_t, dive_len, dive_depth, dive_entry, dive_max, dive_under; /* under the water after piercing it */
  bool wake_on;                   /* over a tanker's wake */
  bool big_slide;                 /* the slide that threw the rider */
  int wake_hit;                   /* riding over a rival's wake */
  int beached;                    /* ticks on land */
  int escapes;                    /* times it got back to the water under way */
  float spill;                    /* seconds left of a spill: the rider is off and climbing back on */
  int spills;
  float radius;                   /* the path's turn radius, smoothed (m) */
  float heading;                  /* the direction of travel */
  float land_keep;                /* share of its speed the last landing kept */
  float slip;                     /* sideways speed through the water */
  float slide_t;                  /* seconds in a hard slide */
  float out_t;                    /* seconds out of the course area */
  int dq;                         /* 1 too many misses, 2 left the course */
  int ok_tick;                    /* passed a buoy this tick: which, plus one */
  int shortcuts;                  /* times through the hook's short way */
  int shut_hits;                  /* times it ran into the hook while it was shut */
  float flash_short;
  float current;                  /* how fast the water under it runs */
  float sink_max;                 /* deepest the hull's centre has been under the surface (m) */
  int ramps;                      /* ramps taken */
  int wall_hits;                  /* times it ran into a ramp's side */
  int slow_ticks;
  float air_max;                  /* longest time in the air (s) */
} craft;

/* what happened to the human this tick: the HUD, the sounds, the rumble and the bursts all read this list, each from its own place in it */
enum { EV_PASS, EV_TOP, EV_TRICK, EV_LAND, EV_MISS, EV_GAIN, EV_LOSE, EV_LAP, EV_FINAL, EV_WALL, EV_CRASH, EV_CHARGE, EV_TURBO, EV_KINDS };
typedef struct game_event { int kind; int a; float x, z; } game_event;
#define EV_RING 64
void ev_push(int kind, int a, float x, float z);
extern game_event g_ev[EV_RING];
extern unsigned g_ev_n;
extern int g_ev_on;                /* ev_on 0: nothing answers, the feedback gate's control */
extern int g_fb[EV_KINDS][4];      /* how each kind was answered: popups, sounds, rumbles, bursts */
extern int g_turbo_on;             /* turbo_on 0: the turbo gate's control */
extern float g_pack;               /* pack 0.25: the old small pull on rivals and none on the human, the pack gate's control */
typedef struct game {
  int state;
  int level;
  float clock;      /* seconds since the race began (negative: countdown) */
  float swell;      /* 0 calm .. 1 full: grows lap to lap */
  float tide;       /* the rest level, metres */
  int order[RACERS];
  uint32_t tick;
  cw_node water, camera, sun, sky, clouds, sun_node;
  cw_terrain *land;
  double wt;        /* the waves' time this tick */
  double wt_extra;  /* sim checks that run many ticks a frame */
  craft c[RACERS];
  float cam[3], cam_prev[3], look[3], look_prev[3], cam_fov, cam_fov_prev;
  float cam_orbit;  /* the attract loop's cut timer */
  int cam_mode;
  float ramp_x[RAMP_MAX], ramp_z[RAMP_MAX], ramp_yaw[RAMP_MAX], ramp_ta[RAMP_MAX]; /* place; and this tick: height, tilt along and across */
  int32_t miss_at[BUOY_MAX]; /* computer riders' misses, per buoy */
  float flash;     /* lightning now, 0..1 (the sim's clock picks when: the same on every host) */
  float tanker[3]; /* x, z, how far along its way */
  int32_t tanker_hits, wake_rides;
  int32_t ai_stuck; /* the most ticks in a row any computer rider has spent under 2 m/s */
  int32_t ai_ramps, ai_big_ramps, ai_spills, ai_misses, ai_passes, ai_walls; /* all the computer riders so far */
  int32_t ramp_points, ramp_gap_points;      /* points measured around the ramps; of them, a pile's foot over the water */
  int32_t ramp_dry_entry, ramp_swamped;      /* ticks the deck's low end stood clear of the water; ticks the water reached over the top */
  float ramp_gap_max, ramp_entry_min, ramp_lip_min, ramp_foot_ground_max; /* worst of each, metres */
  int32_t cam_under; /* ticks the chase camera spent under the surface */
  int32_t attract_under, attract_land, attract_blocked, attract_hidden; /* ticks the attract camera was under water, in land, had land in its view of the craft, or a crest over the hull */
  uint32_t sim_hash;
} game;
extern game G;

/* sim.c */
extern float g_swell_hold, g_tide_hold, g_ramp_lift, g_current;
extern int g_ai_ramps;    /* debug: ai_ramps 0, no computer rider goes for a ramp: the control for what ramps cost them */
extern int g_ramp_seesaw; /* debug: ramp_seesaw 1, the jump gate's control: decks that tip with the swell */
void sim_hook_start(float back);
extern int g_ride_top;
void sim_reset(int state);
bool buoy_on(int b);      /* difficulty: how many of the course's buoys count */
bool racer_on(int i);     /* a time trial is the human alone */
bool hook_open(void);     /* the course's hook lets a rider through now */
void sim_tick(float dt);
float ground_at(float x, float z);
void track_at(float s, float *x, float *z, float *dx, float *dz);
float ramp_hl(int r), ramp_hw(int r), ramp_slope(int r), ramp_top_at(int r);
float ramp_deck(int r, float a); /* the deck's height at a metres along it from its middle */
bool ramp_over(float x, float z, float *deck); /* over a ramp's deck: its height there */
void sim_ramp_start(int r, float back, float aside); /* the human lined up with a ramp, at speed */
/* view.c */
extern int g_attract_clear;
void view_init(void);
void view_sync(double alpha);
void view_course_apply(void);    /* G.level changed: land, sea, sky and marks for that course */
void view_tick(float dt);
extern bool g_peek_on;  /* debug: peek holds the camera, for the grounding sweep */
extern float g_peek[6];
/* rider.c */
extern float rider_torso[RACERS];
extern float rider_foot_side[RACERS]; /* how far out to the side the further foot is, metres from the craft's middle */
void rider_init(void);
void rider_pose(int i, const craft *c, float alpha);
void rider_bind(int i, t3_object *rider);
/* fx.c */
void fx_init(cw_node parent);
void fx_tick(float dt);
/* menu.c */
void menu_init(void);
void menu_tick(float dt);
void menu_draw(void);
bool menu_blocks_sim(void);
bool menu_active(void);
void menu_off(void);
void menu_course(int level); /* debug and tests: that course now */
void menu_debug(void);
float menu_best_time(void); /* this course's best time trial, 0: none */
/* sim.c */
void sim_place(float s);
/* hud.c */
extern int g_cam_cut, g_cam_drawn_under;
extern int g_fair, g_land_hist[21], g_dive_on;
extern int g_hud_label_old, g_hud_label_hits, g_hud_words_n, g_hud_pop_n, g_hud_pop_keep;
extern float g_hud_label[4];
void hud_race_reset(void);
extern float g_ram_last[3];
extern int g_ram_on, g_ram_hits, g_ram_wipes, g_ram_setup;
void sim_ram_setup(int kind);
extern int g_old_crash, g_dist_step, g_place_hold, g_hold_ticks, g_raw_flips, g_near_n;
extern float g_near, g_near_sum;
void sim_ramp_side(int r, float across);
extern int g_buoy_style, g_no_dq, g_try_ramp, g_try_res, g_try_hit, g_try_prev, g_try_missed;
void sim_ramp_try(int r, float aside, float look, const float *at);
float mark_size(void);
extern int g_hud_ptr_mode;      /* the next buoy's mark on screen just now: 0 none, 1 over the buoy, 2 at the screen's edge */
extern float g_hud_ptr[2], g_hud_buoy[4]; /* where it is drawn; where the buoy's top falls on screen, 1 if it is in front of the camera, the waterline's row */
extern int g_hud_mark, g_buoy_hide, g_freeze; /* the distance gate's picture pairs: no HUD mark, no buoys, the world held still */
extern int g_hud_wrong;
extern int g_map_on, g_map_skew;
extern float g_map_scale, g_map_rect[4], g_map_dots[RACERS * 2], g_map_next[2]; /* the minimap's rectangle, every racer's dot, the next buoy's, for its gate */
void hud_init(void);
void hud_draw(void);
/* audio.c */
void audio_init(void);
void audio_tick(float dt);
#endif
