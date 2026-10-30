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
} craft;

typedef struct game {
  int state;
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
  uint32_t rumble_low;
  uint32_t sim_hash;
} game;
extern game G;

/* sim.c */
void sim_reset(int state);
void sim_tick(float dt);
float ground_at(float x, float z);
void track_at(float s, float *x, float *z, float *dx, float *dz);
/* view.c */
void view_init(void);
void view_sync(double alpha);
void view_tick(float dt);
/* rider.c */
void rider_init(void);
void rider_pose(int i, const craft *c, float alpha);
void rider_bind(int i, t3_object *rider);
/* fx.c */
void fx_init(cw_node parent);
void fx_tick(float dt);
/* hud.c */
void hud_init(void);
void hud_draw(void);
#endif
