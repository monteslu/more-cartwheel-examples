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
#define LEVELS 1
#define LEVEL_NAME_LIST { "Crater Atoll" }
static const char *const RIDER_NAMES[6] = { "You", "Marlin", "Kestrel", "Tide", "Juno", "Reef" };
extern int g_difficulty; /* 0 easy, 1 normal, 2 hard */
enum { MODE_CHAMPIONSHIP, MODE_TIME_TRIAL };
extern int g_mode;

enum { RACE_ATTRACT, RACE_GRID, RACE_ON, RACE_DONE };

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
  int trick_kind;                 /* 0 none, 1 barrel roll, 2 back flip */
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
  craft_input in;
  bool human;
  bool bump;                      /* hit the ground this tick */
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
  float sink_max;                 /* deepest the hull's centre has been under the surface (m) */
} craft;

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
  int32_t cam_under; /* ticks the chase camera spent under the surface */
  int32_t attract_under, attract_land, attract_blocked, attract_hidden; /* ticks the attract camera was under water, in land, had land in its view of the craft, or a crest over the hull */
  uint32_t sim_hash;
} game;
extern game G;

/* sim.c */
extern float g_swell_hold;
extern int g_ride_top;
void sim_reset(int state);
bool buoy_on(int b);      /* difficulty: how many of the course's buoys count */
bool racer_on(int i);     /* a time trial is the human alone */
void sim_tick(float dt);
float ground_at(float x, float z);
void track_at(float s, float *x, float *z, float *dx, float *dz);
/* view.c */
extern int g_attract_clear;
void view_init(void);
void view_sync(double alpha);
void view_tick(float dt);
/* rider.c */
extern float rider_torso[RACERS];
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
void menu_debug(void);
float menu_best_time(void); /* this course's best time trial, 0: none */
/* sim.c */
void sim_place(float s);
/* hud.c */
void hud_init(void);
void hud_draw(void);
/* audio.c */
void audio_init(void);
void audio_tick(float dt);
#endif
