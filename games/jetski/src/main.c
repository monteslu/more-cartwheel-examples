/* jetski: wave racing on a volcanic atoll. See README.md. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/environment.h"
#include "cartwheel/input.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/platform.h"
#include "cartwheel/project.h"
#include "cartwheel/render3d.h"
#include "cartwheel/render2d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "game.h"

static float perf[7], sun_dbg[3];
static int32_t ground_total, ground_gaps;
static float ground_worst;
static int32_t gpu_programs; /* programs compiled so far: if this grows during play, the warm-up missed them */
static cw_action a_left, a_right, a_back, a_fwd, a_throttle, a_trick, a_turbo, a_kick;

static void start(int players, void *user) {
  (void)players, (void)user;
#ifdef JETSKI_AUTOPILOT
  sim_reset(RACE_GRID);
#else
  sim_reset(RACE_ATTRACT);
#endif
}

static void cmd_race(int argc, const char **argv, cw_reply *r) {
  (void)argc, (void)argv;
  menu_off();
  sim_reset(RACE_GRID);
  cw_reply_raw(r, "true");
}
/* drop <pitch> <height> <speed>: the human in the air over the water here, nose at that pitch: the landing gate's start */
static void cmd_drop(int argc, const char **argv, cw_reply *r) {
  craft *c = &G.c[0];
  menu_off();
  G.state = RACE_ON;
  if (G.clock < 1) G.clock = 1;
  float sy, cy;
  cw_sincos(c->yaw, &sy, &cy);
  float v = argc > 3 ? (float)atof(argv[3]) : 22;
  c->pitch = c->ppitch = argc > 1 ? (float)atof(argv[1]) : 0;
  c->y = c->py = c->surf + (argc > 2 ? (float)atof(argv[2]) : 2);
  c->vx = sy * v, c->vz = cy * v, c->vy = 0, c->pr = 0, c->rr = 0, c->roll = 0, c->air_time = 0.5f, c->air = true;
  if (r) cw_reply_raw(r, "true");
}
/* set the human going at a speed straight on here */
static void cmd_go(int argc, const char **argv, cw_reply *r) {
  float s = argc > 1 ? (float)atof(argv[1]) : 120;
  menu_off();
  sim_place(s);
  G.state = RACE_ON;
  if (G.clock < 1) G.clock = 1;
  if (argc > 2) { G.c[0].yaw = G.c[0].pyaw = (float)atof(argv[2]); float a, b; cw_sincos(G.c[0].yaw, &a, &b); G.c[0].vx = a * 22, G.c[0].vz = b * 22; }
  if (r) cw_reply_raw(r, "true");
}

static void cmd_warp(int argc, const char **argv, cw_reply *r) {
  /* warp <track metres>: every racer jumps there, for a look along the course */
  if (argc < 2) { if (r) cw_reply_raw(r, "false"); return; }
  sim_place((float)atof(argv[1]));
  if (r) cw_reply_raw(r, "true");
}

#ifdef JETSKI_AUTOPILOT
/* a look along the course: JETSKI_WARP=<metres> starts the race there, at speed */
static void warp_from_env(void) {
  const char *w = getenv("JETSKI_WARP");
  if (!w) return;
  const char *args[2] = { "warp", w };
  cw_reply *none = NULL;
  G.state = RACE_ON;
  G.clock = 1;
  for (int i = 0; i < RACERS; i++) G.c[i].lap = 0;
  cmd_warp(2, args, none);
}
#endif

/* beach <track metres>: the human on the first sand left or right of the course there, nose inland, race running: what a
 * rider does who misses a corner. The beaching gate starts here. */
static void cmd_beach(int argc, const char **argv, cw_reply *r) {
  float s = argc > 1 ? (float)atof(argv[1]) : 300;
  float x, z, dx, dz;
  menu_off();
  track_at(s, &x, &z, &dx, &dz);
  G.state = RACE_ON;
  if (G.clock < 1) G.clock = 1;
  craft *c = &G.c[0];
  for (int sd = 0; sd < 2; sd++) {
    float rx = sd ? dz : -dz, rz = sd ? -dx : dx;
    for (float d = 5; d < 260; d += 2) {
      float px = x + rx * d, pz = z + rz * d, gy = ground_at(px, pz);
      if (gy > 0.7f && gy < 3.0f) {
        c->x = c->px = px, c->z = c->pz = pz, c->y = c->py = gy + 0.22f;
        c->yaw = c->pyaw = cw_atan2(rx, rz);
        c->vx = c->vy = c->vz = 0;
        c->pr = c->rr = 0, c->pitch = c->roll = 0;
        c->lap = 0;
        c->beached = 0, c->escapes = 0;
        G.cam[0] = 0, G.look[0] = 0;
        if (r) cw_reply_printf(r, "{\"x\":%.1f,\"z\":%.1f,\"ground\":%.2f,\"away\":%.0f}", px, pz, gy, d);
        return;
      }
    }
  }
  if (r) cw_reply_raw(r, "false");
}

/* finish: the human crosses the line now, for the results pages' gate */
static void cmd_finish(int argc, const char **argv, cw_reply *r) {
  (void)argc, (void)argv;
  if (G.state == RACE_GRID || G.state == RACE_ON) { G.state = RACE_ON; G.c[0].lap = LAPS, G.c[0].finish = G.clock > 1 ? G.clock : 1; }
  if (r) cw_reply_raw(r, "true");
}

/* cam_clear 0: the control for the attract camera's swell check */
static void cmd_cam_clear(int argc, const char **argv, cw_reply *r) {
  g_attract_clear = argc > 1 ? atoi(argv[1]) : 1;
  if (r) cw_reply_raw(r, "true");
}
/* swell <0..1>: hold the sea at that size; difficulty <0..2> */
static void cmd_swell(int argc, const char **argv, cw_reply *r) {
  g_swell_hold = argc > 1 ? (float)atof(argv[1]) : 0;
  if (r) cw_reply_raw(r, "true");
}
static void cmd_difficulty(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) { int d = atoi(argv[1]); g_difficulty = d < 0 ? 0 : d > 2 ? 2 : d; }
  if (r) cw_reply_raw(r, "true");
}
/* ride_top 0: the control for the hull staying on the surface */
static void cmd_ride_top(int argc, const char **argv, cw_reply *r) {
  g_ride_top = argc > 1 ? atoi(argv[1]) : 1;
  if (r) cw_reply_raw(r, "true");
}
/* ramp <index> [back metres] [aside metres]: the human lined up with a ramp at speed, the race running */
static void cmd_ramp(int argc, const char **argv, cw_reply *r) {
  menu_off();
  G.state = RACE_ON;
  if (G.clock < 1) G.clock = 1;
  sim_ramp_start(argc > 1 ? atoi(argv[1]) % RAMPS : 0, argc > 2 ? (float)atof(argv[2]) : 40, argc > 3 ? (float)atof(argv[3]) : 0);
  if (r) cw_reply_raw(r, "true");
}
/* course <index>: that course now, attract running */
static void cmd_course(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) menu_course(atoi(argv[1]));
  sim_reset(RACE_ATTRACT);
  if (r) cw_reply_printf(r, "{\"course\":\"%s\"}", DEF->name);
}
/* tide <metres>: hold it there; arch <back metres>: the human lined up with the hook's way through at speed, the race running */
static void cmd_tide(int argc, const char **argv, cw_reply *r) {
  g_tide_hold = argc > 1 ? (float)atof(argv[1]) : -100;
  if (r) cw_reply_raw(r, "true");
}
static void cmd_arch(int argc, const char **argv, cw_reply *r) {
  menu_off();
  G.state = RACE_ON;
  if (G.clock < 1) G.clock = 1;
  sim_hook_start(argc > 1 ? (float)atof(argv[1]) : 40);
  if (r) cw_reply_raw(r, "true");
}
/* peek x y z, at x y z, heights from the ground if the y is 0; peek alone lets go: a held camera for the grounding sweep */
static void cmd_peek(int argc, const char **argv, cw_reply *r) {
  g_peek_on = argc >= 7;
  for (int i = 0; i < 6 && i + 1 < argc; i++) g_peek[i] = (float)atof(argv[i + 1]);
  if (r) cw_reply_printf(r, "{\"ground\":%.2f}", g_peek_on ? ground_at(g_peek[3], g_peek[5]) : 0.0f);
}
/* buoy_style 0: the small buoy and the old chevron, for the distance gate's control */
static void cmd_buoy_style(int argc, const char **argv, cw_reply *r) {
  g_buoy_style = argc > 1 ? atoi(argv[1]) : 1;
  if (r) cw_reply_raw(r, "true");
}
/* for the distance gate: pictures of one moment with and without the buoys */
int g_freeze;
static void cmd_buoy_shot(int argc, const char **argv, cw_reply *r) {
  /* buoy_shot <hud mark 0/1> <buoys 0/1> <freeze 0/1> */
  if (argc > 3) g_hud_mark = atoi(argv[1]), g_buoy_hide = !atoi(argv[2]), g_freeze = atoi(argv[3]);
  if (r) cw_reply_raw(r, "true");
}
/* next <buoy>: make that the human's next, for a look at one from farther off than the course's spacing */
static void cmd_next(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) G.c[0].next_buoy = atoi(argv[1]);
  if (r) cw_reply_raw(r, "true");
}
/* no_dq 1: the computer rider in the human's seat rides the whole race whatever it misses, for race checks */
static void cmd_no_dq(int argc, const char **argv, cw_reply *r) {
  g_no_dq = argc > 1 ? atoi(argv[1]) : 1;
  if (r) cw_reply_raw(r, "true");
}
/* ramp_try <ramp> <aside metres> <lookahead metres> [x z yaw]: can it be reached from the buoy before and still make the next; x z yaw: stood there instead (the gate's control) */
static void cmd_ramp_try(int argc, const char **argv, cw_reply *r) {
  menu_off();
  float at[3] = { 0, 0, 0 };
  bool moved = argc > 6;
  if (moved) at[0] = (float)atof(argv[4]), at[1] = (float)atof(argv[5]), at[2] = (float)atof(argv[6]);
  sim_ramp_try(argc > 1 ? atoi(argv[1]) : 0, argc > 2 ? (float)atof(argv[2]) : 0, argc > 3 ? (float)atof(argv[3]) : 30, moved ? at : NULL);
  if (r) cw_reply_raw(r, "true");
}
/* map <on> [skew] [scale]: the minimap, for its gate. skew 1 draws the dots 60 m off, scale 3 three times too big: the gate's controls */
static void cmd_map(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) g_map_on = atoi(argv[1]);
  g_map_skew = argc > 2 ? atoi(argv[2]) : 0;
  g_map_scale = argc > 3 ? (float)atof(argv[3]) : 1;
  if (r) cw_reply_raw(r, "true");
}
static void cmd_fair(int argc, const char **argv, cw_reply *r) {
  g_fair = argc > 1 ? atoi(argv[1]) : 1;
  if (r) cw_reply_raw(r, "true");
}
/* gates' switches */
int g_force_turbo;
static void cmd_switch(int argc, const char **argv, cw_reply *r) {
  /* sw <name> <value>: ev_on, turbo_on, pack, stock, turbo */
  if (argc > 2) {
    float v = (float)atof(argv[2]);
    if (!strcmp(argv[1], "ev_on")) g_ev_on = (int)v;
    else if (!strcmp(argv[1], "turbo_on")) g_turbo_on = (int)v;
    else if (!strcmp(argv[1], "pack")) g_pack = v;
    else if (!strcmp(argv[1], "stock")) G.c[0].stock = v;
    else if (!strcmp(argv[1], "meter")) G.c[0].meter = (int)v;
    else if (!strcmp(argv[1], "turbo")) g_force_turbo = (int)v;
    else if (!strcmp(argv[1], "ram")) g_ram_on = (int)v;
    else if (!strcmp(argv[1], "ram_setup")) sim_ram_setup((int)v);
    else if (!strcmp(argv[1], "cam_cut")) g_cam_cut = (int)v;
    else if (!strcmp(argv[1], "wrong_side")) { /* the human just past the next buoy on its wrong side, for the feedback gate */
      craft *c = &G.c[0]; const float *b = BUOYS[c->next_buoy]; float sx, sz, tx, tz;
      track_at(b[3], &sx, &sz, &tx, &tz);
      c->x = c->px = b[0] + tx * 1 - tz * 6 * b[2], c->z = c->pz = b[1] + tz * 1 + tx * 6 * b[2];
      c->vx = tx * 25, c->vz = tz * 25;
    }
    else if (!strcmp(argv[1], "dive")) g_dive_on = (int)v;
    else if (!strcmp(argv[1], "stand_hips")) g_stand_hips = v;
    else if (!strcmp(argv[1], "stand_old")) g_stand_old = (int)v;
    else if (!strcmp(argv[1], "screen_clear")) g_screen[0] = g_screen[1] = 1e9f, g_screen[2] = 0;
    else if (!strcmp(argv[1], "cam_jitter")) { extern int g_cam_jitter; g_cam_jitter = (int)v; }
    else if (!strcmp(argv[1], "cam_acc_clear")) { extern float g_cam_acc_max; extern int32_t g_cam_flips, g_cam_ticks; g_cam_acc_max = 0, g_cam_flips = g_cam_ticks = 0; }
    else if (!strcmp(argv[1], "ground_clear")) { extern float g_hull_hi, g_hull_lo, g_foot_gap_max, g_hands_gap_max; extern int32_t g_hull_ticks, g_pose_frames; g_hull_hi = -1e9f, g_hull_lo = 1e9f, g_hull_ticks = 0, g_foot_gap_max = g_hands_gap_max = 0, g_pose_frames = 0; }
    else if (!strcmp(argv[1], "trick_guard")) g_trick_guard = (int)v;
    else if (!strcmp(argv[1], "autosteer")) g_autosteer = (int)v;
    else if (!strcmp(argv[1], "audio_old")) { extern int g_audio_old; g_audio_old = (int)v; }
    else if (!strcmp(argv[1], "waterline")) { extern int g_waterline; g_waterline = (int)v; }
    else if (!strcmp(argv[1], "plate")) { extern float g_plate; g_plate = v; }
    else if (!strcmp(argv[1], "sink")) { extern float g_sink; g_sink = v; }
    else if (!strcmp(argv[1], "floats")) { extern int g_floats; g_floats = (int)v; }
    else if (!strcmp(argv[1], "spray_lit")) { extern int g_spray_lit; g_spray_lit = (int)v; }
    else if (!strcmp(argv[1], "finish_hold")) g_finish_hold = v;
    else if (!strcmp(argv[1], "fov_span")) { extern float g_fov_span; g_fov_span = v; }
    else if (!strcmp(argv[1], "speedlines")) { extern int g_speedlines_on; g_speedlines_on = (int)v; }
    else if (!strcmp(argv[1], "cam_roll")) { extern float g_cam_roll_max; g_cam_roll_max = v; }
    else if (!strcmp(argv[1], "label_old")) g_hud_label_old = (int)v;
    else if (!strcmp(argv[1], "pop_keep")) g_hud_pop_keep = (int)v;
    else if (!strcmp(argv[1], "old_crash")) g_old_crash = (int)v;
    else if (!strcmp(argv[1], "dist_step")) g_dist_step = (int)v;
    else if (!strcmp(argv[1], "place_hold")) g_place_hold = (int)v;
    else if (!strcmp(argv[1], "hold")) g_hold_ticks = (int)v;
    else if (!strcmp(argv[1], "ramp_side")) sim_ramp_side((int)v, argc > 3 ? (float)atof(argv[3]) : 1);
  }
  if (r) cw_reply_raw(r, "true");
}
/* detile <0..1>: the land's side_detile live, for the plaid comparison */
static void cmd_detile(int argc, const char **argv, cw_reply *r) {
  if (G.land) cw_terrain_set_side_detile(G.land, argc > 1 ? (float)atof(argv[1]) : 1);
  if (r) cw_reply_raw(r, "true");
}
/* grounding [lift metres]: every scatter instance of this course against the ground around its foot, and each mark against the
 * sea and the ground under it. lift is the must-fail control. foot[]: how low each model reaches and how wide its foot is */
static void cmd_grounding(int argc, const char **argv, cw_reply *r) {
  float lift = argc > 1 ? (float)atof(argv[1]) : 0;
  const course_def *d = DEF;
  int cell_m = 32, cells = (int)((float)d->quads * d->spacing) / cell_m + 2;
  static float buf[5 * 4096];
  int total = 0, gaps = 0;
  float worst = -1e9f, worst_at[2] = { 0, 0 };
  int by_layer[16] = { 0 };
  for (int l = 0; l < d->scatter_n; l++) {
    const course_scatter *s = &d->scatter[l];
    bool tree = !s->sink;
    float low = tree ? -0.4f : -0.67f, foot = tree ? 0.28f : 0.6f;
    for (int cz = -cells; cz < cells; cz++) for (int cx = -cells; cx < cells; cx++) {
      int n = cw_terrain_scatter_cell(G.land, l, cx, cz, buf, 4096);
      for (int i = 0; i < n; i++) {
        const float *o = &buf[i * 5];
        float bottom = o[1] + lift + low * o[3], g = 1e9f;
        for (int k = 0; k < 8; k++) {
          float a = (float)k * 0.785398f, gy = ground_at(o[0] + foot * o[3] * cw_cos(a), o[2] + foot * o[3] * cw_sin(a));
          if (gy < g) g = gy;
        }
        /* above the lowest ground around its foot: the foot's edge hangs in the air */
        float gap = bottom - g;
        if (gap > worst) worst = gap, worst_at[0] = o[0], worst_at[1] = o[2];
        if (gap > 0.15f) gaps++, by_layer[l]++;
        total++;
      }
    }
  }
  ground_total = total, ground_gaps = gaps, ground_worst = worst;
  /* marks: floats, so the ground must stay under the lowest the water goes (the tide's low less the swell) */
  float tl = d->tide[0] < d->tide[1] ? d->tide[0] : d->tide[1], amp = 0;
  for (int i = 0; i < 4; i++) amp += d->waves[i][2] + d->waves[i][3] * d->swell[1];
  float shallowest_buoy = -1e9f, shallowest_ramp = -1e9f;
  for (int b = 0; b < d->buoy_n; b++) { float g = ground_at(d->buoys[b][0], d->buoys[b][1]); if (g > shallowest_buoy) shallowest_buoy = g; }
  for (int q = 0; q < d->ramp_n; q++) { float g = ground_at(G.ramp_x[q], G.ramp_z[q]); if (g > shallowest_ramp) shallowest_ramp = g; }
  if (r) cw_reply_printf(r, "{\"course\":\"%s\",\"instances\":%d,\"gaps\":%d,\"worst\":%.3f,\"at\":[%.0f,%.0f],\"by_layer\":[%d,%d,%d,%d,%d],\"tide_low\":%.2f,\"swell_amp\":%.2f,\"shallowest_buoy_ground\":%.2f,\"shallowest_ramp_ground\":%.2f,\"ramp_gap_points\":%d}",
    d->name, total, gaps, worst, worst_at[0], worst_at[1], by_layer[0], by_layer[1], by_layer[2], by_layer[3], by_layer[4], tl, amp, shallowest_buoy, shallowest_ramp, G.ramp_gap_points);
}
static void cmd_current(int argc, const char **argv, cw_reply *r) {
  g_current = argc > 1 ? (float)atof(argv[1]) : 1;
  if (r) cw_reply_raw(r, "true");
}
static void cmd_ai_ramps(int argc, const char **argv, cw_reply *r) {
  g_ai_ramps = argc > 1 ? atoi(argv[1]) : 1;
  if (r) cw_reply_raw(r, "true");
}
static void cmd_ramp_seesaw(int argc, const char **argv, cw_reply *r) {
  g_ramp_seesaw = argc > 1 ? atoi(argv[1]) : 1;
  if (r) cw_reply_raw(r, "true");
}
static void cmd_ramp_lift(int argc, const char **argv, cw_reply *r) {
  g_ramp_lift = argc > 1 ? (float)atof(argv[1]) : 0;
  if (r) cw_reply_raw(r, "true");
}
/* ramp_where <index>: where it is and how it floats now */
static void cmd_ramp_where(int argc, const char **argv, cw_reply *r) {
  int q = argc > 1 ? atoi(argv[1]) % RAMP_MAX : 0;
  float sy, cy;
  cw_sincos(G.ramp_yaw[q], &sy, &cy);
  if (r) cw_reply_printf(r, "{\"n\":%d,\"x\":%.2f,\"z\":%.2f,\"top\":%.2f,\"sy\":%.3f,\"cy\":%.3f}", RAMPS, G.ramp_x[q], G.ramp_z[q], DEF->ramp_top, sy, cy);
}
/* trial [swell]: the human alone at rest on open water (no land within 160 m around, 260 m clear ahead), the sea held calm,
 * the race running, its handling measures cleared: the craft gate's start (tests/jetski-standup.mjs) */
int g_trial;
float g_tr[8]; /* top speed on the straight, the clock at the first throttle, the turn's distance and heading change at full lock, its radius, its speed, the time to plane, turned yet */
static void cmd_trial(int argc, const char **argv, cw_reply *r) {
  menu_off();
  g_mode = MODE_TIME_TRIAL, g_no_dq = 1;
  g_swell_hold = argc > 1 ? (float)atof(argv[1]) : 0.05f;
  sim_reset(RACE_ON);
  int at = -1;
  for (int i = 0; i < TRACK_N && at < 0; i += 2) {
    float x = TRACK[i][0], z = TRACK[i][1], dx = TRACK[i][2], dz = TRACK[i][3];
    bool ok = true;
    for (int k = 0; k < 24 && ok; k++) { float s, c; cw_sincos((float)k * TAU / 24.0f, &s, &c); for (float d = 10; d <= 160 && ok; d += 15) if (ground_at(x + s * d, z + c * d) > -3.0f) ok = false; }
    for (float d = 10; d <= 260 && ok; d += 10) if (ground_at(x + dx * d, z + dz * d) > -3.0f) ok = false;
    if (ok) at = i;
  }
  if (at < 0) { cw_reply_error(r, "no open water on this course"); return; }
  craft *c = &G.c[0];
  c->x = c->px = TRACK[at][0], c->z = c->pz = TRACK[at][1];
  c->y = c->py = cw_water_height_at(G.water, c->x, c->z, cw_water_time(G.water));
  c->yaw = c->pyaw = cw_atan2(TRACK[at][2], TRACK[at][3]);
  c->vx = c->vy = c->vz = 0, c->track_i = at, c->meter = 5;
  G.clock = 0.001f;
  G.cam[0] = 0, G.look[0] = 0;
  g_trial = 1;
  memset(g_tr, 0, sizeof g_tr);
  if (r) cw_reply_printf(r, "{\"at\":%d}", at);
}
/* craft <0 sit-down, 1 stand-up>: every racer on that craft (not saved: the menu's choice is the player's) */
static void cmd_craft(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) g_craft = atoi(argv[1]) ? CRAFT_STAND : CRAFT_SIT;
  if (r) cw_reply_printf(r, "{\"craft\":%d}", g_craft);
}
/* mode <0 championship, 1 time trial, 2 stunt, 3 free roam> */
static void cmd_mode(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) { int m = atoi(argv[1]); g_mode = m < 0 || m >= MODE_N ? MODE_CHAMPIONSHIP : m; }
  if (r) cw_reply_raw(r, "true");
}

/* debug channel: "bind <action> <bindings>" replaces an action's bindings (the text form, "key:x, pad:north"): for checking
 * that a rebound action's prompt shows its new button */
static void cmd_bind(int argc, const char **argv, cw_reply *r) {
  if (argc < 3) { cw_reply_error(r, "bind <action> <bindings>"); return; }
  cw_action a = cw_action_find(argv[1]);
  if (a < 0) { cw_reply_error(r, "no such action"); return; }
  char list[256] = "";
  for (int i = 2; i < argc; i++) { if (i > 2) strncat(list, " ", sizeof list - strlen(list) - 1); strncat(list, argv[i], sizeof list - strlen(list) - 1); }
  cw_action_clear_bindings(a);
  if (!cw_action_bind_text(a, list)) { cw_reply_error(r, "bad bindings"); return; }
  cw_reply_printf(r, "ok");
}

static void init(void *user) {
  (void)user;
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("jetski: input.cfg had errors");
  a_left = cw_action_find("steer_left"), a_right = cw_action_find("steer_right");
  a_back = cw_action_find("lean_back"), a_fwd = cw_action_find("lean_fwd");
  a_throttle = cw_action_find("throttle"), a_trick = cw_action_find("trick"), a_turbo = cw_action_find("turbo"), a_kick = cw_action_find("kick");
  cw_shell_init(&(cw_shell_desc){ .title = "jetski", .max_players = 1, .start = start, .game_save_size = 128, .game_save_version = 1 });
  view_init();
  hud_init();
  audio_init();
  menu_init();
#ifdef JETSKI_AUTOPILOT
  menu_off();
  { const char *lv = getenv("JETSKI_LEVEL"); if (lv) menu_course(atoi(lv)); } /* the gates' run on another course */
  { const char *cr = getenv("JETSKI_CRAFT"); if (cr) g_craft = atoi(cr) ? CRAFT_STAND : CRAFT_SIT; }
#endif
  if (getenv("JETSKI_SMALL")) { /* sim checks: nothing costly on the software rasteriser */
    cw_node_set(G.water, "reflection", cw_val_bool(false));
    cw_node_set(G.water, "refraction", cw_val_bool(false));
    cw_node_set(G.sun, "shadow", cw_val_bool(false));
    cw_node_set(G.water, "subdivisions", cw_val_int(16));
    cw_node_set(G.water, "ripple_cells", cw_val_int(64));
    /* nothing to look at: the sim needs the data, not the drawing */
    cw_node_set(G.camera, "far", cw_val_float(1.0f));
    cw_node_set(G.camera, "near", cw_val_float(0.9f));
    cw_node_set(G.water, "visible", cw_val_bool(false));
    cw_node_set(cw_node_get(cw_node_get(cw_tree_root(cw_scene_app_tree()), "Root"), "Land"), "visible", cw_val_bool(false));
    cw_node_set(cw_node_get(cw_node_get(cw_tree_root(cw_scene_app_tree()), "Root"), "Post"), "bloom", cw_val_bool(false));
  }
  sim_reset(RACE_ATTRACT);
  cw_shell_start(1);
#ifndef JETSKI_AUTOPILOT
  sim_reset(RACE_ATTRACT);
#endif
#ifdef JETSKI_AUTOPILOT
  warp_from_env();
#endif
  cw_debug_i32("race.state", &G.state);
  cw_debug_command("bind", "bind <action> <bindings>: replace an action's bindings", cmd_bind);
  cw_debug_i32("gpu.programs", &gpu_programs);
  cw_debug_i32("ground.total", &ground_total);
  cw_debug_i32("ground.gaps", &ground_gaps);
  cw_debug_f32("ground.worst", &ground_worst);
  cw_debug_i32("race.level", &G.level);
  cw_debug_i32("p0.shortcuts", &G.c[0].shortcuts);
  cw_debug_i32("p0.shut_hits", &G.c[0].shut_hits);
  cw_debug_i32("race.mode", &g_mode);
  cw_debug_i32("race.craft", &g_craft);
  cw_debug_command("craft", "craft <0 sit-down, 1 stand-up>: every racer on that craft", cmd_craft);
  cw_debug_command("trial", "trial [swell]: the human alone at rest on open water, the handling measures cleared", cmd_trial);
  cw_debug_f32_array("trial", g_tr, 8);
  { extern float g_cam_acc_max; extern int32_t g_cam_flips, g_cam_ticks; cw_debug_f32("cam.acc_max", &g_cam_acc_max); cw_debug_i32("cam.flips", &g_cam_flips); cw_debug_i32("cam.ticks", &g_cam_ticks); }
  cw_debug_f32("p0.plane", &G.c[0].plane);
  cw_debug_f32("p0.plane_at", &G.c[0].plane_at);
  cw_debug_f32("p0.top", &G.c[0].top);
  cw_debug_f32("p0.roll", &G.c[0].roll);
  cw_debug_f32("p0.pole_t", &G.c[0].pole_t);
  cw_debug_i32("p0.subs", &G.c[0].subs);
  cw_debug_i32("p0.spins", &G.c[0].spins);
  cw_debug_i32("p0.supers", &G.c[0].supers);
  cw_debug_i32("p0.poles", &G.c[0].poles);
  { extern float g_hull_hi, g_hull_lo, g_foot_gap_max, g_hands_gap_max; extern int32_t g_hull_ticks, g_pose_frames;
    cw_debug_f32("ground.hull_hi", &g_hull_hi); cw_debug_f32("ground.hull_lo", &g_hull_lo); cw_debug_i32("ground.hull_ticks", &g_hull_ticks);
    cw_debug_f32("ground.foot_gap", &g_foot_gap_max); cw_debug_f32("ground.hands_gap", &g_hands_gap_max); cw_debug_i32("ground.pose_frames", &g_pose_frames); }
  cw_debug_f32("rider.pole", &rider_pole[0]);
  cw_debug_f32_array("stand.pose", g_pose, 4);
  { extern float g_ride[4]; cw_debug_f32_array("p0.ride", g_ride, 4); }
  cw_debug_f32_array("stand.screen", g_screen, 7);
  cw_debug_f32("rider.stand", &rider_stand[0]);
  cw_debug_f32("p0.finish", &G.c[0].finish);
  cw_debug_i32("race.difficulty", &g_difficulty);
  cw_debug_i32("cam.attract_under", &G.attract_under);
  cw_debug_i32("cam.attract_land", &G.attract_land);
  cw_debug_i32("cam.attract_blocked", &G.attract_blocked);
  cw_debug_i32("cam.attract_hidden", &G.attract_hidden);
  menu_debug();
  cw_debug_f32("race.clock", &G.clock);
  cw_debug_f32("race.swell", &G.swell);
  cw_debug_f32("race.tide", &G.tide);
  cw_debug_u32("race.hash", &G.sim_hash);
  cw_debug_u32("race.tick", &G.tick);
  cw_debug_f32_array("p0.pos", &G.c[0].x, 3);
  cw_debug_f32_array("p0.vel", &G.c[0].vx, 3);
  cw_debug_f32("p0.yaw", &G.c[0].yaw);
  cw_debug_f32("p0.speed", &G.c[0].speed);
  cw_debug_f32("p0.surf", &G.c[0].surf);
  cw_debug_f32("p0.air_time", &G.c[0].air_time);
  cw_debug_i32("p0.meter", &G.c[0].meter);
  cw_debug_i32("p0.lap", &G.c[0].lap);
  cw_debug_i32("p0.place", &G.c[0].place);
  cw_debug_i32("p0.next_buoy", &G.c[0].next_buoy);
  cw_debug_i32("p0.misses", &G.c[0].misses);
  cw_debug_i32("p0.passes", &G.c[0].passes);
  cw_debug_i32("p0.track_i", &G.c[0].track_i);
  cw_debug_f32("p0.dist", &G.c[0].dist);
  cw_debug_f32("p1.dist", &G.c[1].dist);
  cw_debug_f32("p2.dist", &G.c[2].dist);
  cw_debug_f32_array("cam.pos", G.cam, 3);
  cw_debug_i32("cam.under_ticks", &G.cam_under);
  cw_debug_f32_array("perf.ms", perf, 7);
  cw_debug_f32_array("sun.dir", sun_dbg, 3);
  cw_debug_f32("p0.radius", &G.c[0].radius);
  cw_debug_f32("p0.land_keep", &G.c[0].land_keep);
  cw_debug_f32("p0.slip", &G.c[0].slip);
  cw_debug_f32("p0.spill", &G.c[0].spill);
  cw_debug_i32("p0.spills", &G.c[0].spills);
  cw_debug_i32("p0.ramps", &G.c[0].ramps);
  cw_debug_i32("p0.tricks", &G.c[0].tricks);
  cw_debug_f32("p0.air_max", &G.c[0].air_max);
  cw_debug_field("ai.miss_at", G.miss_at, CW_DBG_I32, BUOY_MAX);
  cw_debug_f32("p0.current", &G.c[0].current);
  cw_debug_i32("hook.tanker_hits", &G.tanker_hits);
  cw_debug_i32("hook.wake_rides", &G.wake_rides);
  cw_debug_f32_array("hook.tanker", G.tanker, 3);
  cw_debug_i32("ai.stuck", &G.ai_stuck);
  cw_debug_i32("ai.ramps", &G.ai_ramps);
  cw_debug_i32("ai.big_ramps", &G.ai_big_ramps);
  cw_debug_i32("ai.spills", &G.ai_spills);
  cw_debug_i32("ai.misses", &G.ai_misses);
  cw_debug_i32("ai.passes", &G.ai_passes);
  cw_debug_i32("ai.walls", &G.ai_walls);
  cw_debug_i32("ramp.points", &G.ramp_points);
  cw_debug_i32("ramp.gap_points", &G.ramp_gap_points);
  cw_debug_i32("ramp.dry_entry", &G.ramp_dry_entry);
  cw_debug_i32("ramp.swamped", &G.ramp_swamped);
  cw_debug_f32("ramp.gap_max", &G.ramp_gap_max);
  cw_debug_f32("ramp.entry_min", &G.ramp_entry_min);
  cw_debug_f32("ramp.lip_min", &G.ramp_lip_min);
  cw_debug_f32("ramp.foot_ground_max", &G.ramp_foot_ground_max);
  cw_debug_f32("p0.sink_max", &G.c[0].sink_max);
  cw_debug_i32("p0.dq", &G.c[0].dq);
  cw_debug_f32("p0.out_t", &G.c[0].out_t);
  cw_debug_f32("rider.torso", &rider_torso[0]);
  cw_debug_i32("p0.beached", &G.c[0].beached);
  cw_debug_i32("p0.escapes", &G.c[0].escapes);
  cw_debug_command("beach", "beach <metres>: the human on the sand beside the course there, nose inland", cmd_beach);
  cw_debug_command("drop", "drop <pitch> <height> <speed>", cmd_drop);
  cw_debug_command("go", "go <metres>: the race running at speed there", cmd_go);
  cw_debug_command("race", "start a race", cmd_race);
  cw_debug_command("cam_clear", "cam_clear 0: the attract camera ignores the swell", cmd_cam_clear);
  cw_debug_command("swell", "swell <0..1>: hold the sea at that size (0: let it build)", cmd_swell);
  cw_debug_command("difficulty", "difficulty <0 easy, 1 normal, 2 hard>", cmd_difficulty);
  cw_debug_command("ride_top", "ride_top 0: the hull held by its spring alone", cmd_ride_top);
  cw_debug_command("ramp", "ramp <index> [back] [aside]: lined up with a ramp at speed", cmd_ramp);
  cw_debug_command("course", "course <index>: that course now", cmd_course);
  cw_debug_command("tide", "tide <metres>: hold the tide there", cmd_tide);
  cw_debug_command("arch", "arch <back>: lined up with the course's arch", cmd_arch);
  cw_debug_command("buoy_shot", "buoy_shot <hud mark> <buoys> <freeze>: picture pairs for the distance gate", cmd_buoy_shot);
  cw_debug_command("next", "next <buoy>: that one is the human's next", cmd_next);
  cw_debug_command("no_dq", "no_dq 1: no disqualification, for race checks", cmd_no_dq);
  cw_debug_command("ramp_try", "ramp_try <ramp> [aside]: the seat tries to go buoy, ramp, next buoy", cmd_ramp_try);
  cw_debug_i32("try.res", &g_try_res);
  cw_debug_i32("try.hit", &g_try_hit);
  cw_debug_i32("try.prev", &g_try_prev);
  cw_debug_i32("try.missed", &g_try_missed);
  cw_debug_command("map", "map <on> [skew] [scale]: the minimap; skew and scale are its gate's controls", cmd_map);
  cw_debug_f32_array("hud.map", g_map_rect, 4);
  cw_debug_f32_array("hud.map_dots", g_map_dots, RACERS * 2);
  cw_debug_f32_array("hud.map_next", g_map_next, 2);
  cw_debug_command("fair", "fair 0: hops priced as deliberate landings, the landing gate's control", cmd_fair);
  cw_debug_field("land.hist", g_land_hist, CW_DBG_I32, 21);
  cw_debug_command("sw", "sw <ev_on|turbo_on|pack|stock|meter|turbo> <value>: the gates' switches", cmd_switch);
  cw_debug_field("fb", g_fb, CW_DBG_I32, EV_KINDS * 4);
  cw_debug_i32("cam.drawn_under", &g_cam_drawn_under);
  cw_debug_i32("p0.kicks", &G.c[0].kicks);
  cw_debug_i32("p0.styles", &G.c[0].styles);
  cw_debug_f32("cam.roll", &G.cam_roll);
  { extern int g_waterline_stamps; cw_debug_i32("fx.waterline", &g_waterline_stamps); }
  { extern int32_t g_near_floats; cw_debug_i32("fx.near_floats", &g_near_floats); extern int float_count; cw_debug_i32("fx.floats", &float_count); }
  { extern float g_spray_glow; cw_debug_f32("fx.spray_glow", &g_spray_glow); }
  cw_debug_i32("race.rocket", &G.rocket);
  cw_debug_f32("race.fin_t", &G.fin_t);
  cw_debug_f32("race.done_after", &G.done_after);
  cw_debug_i32("race.real_finishes", &G.real_finishes);
  { extern float g_finish_times[RACERS]; cw_debug_f32_array("race.finish_times", g_finish_times, RACERS); }
  { extern float g_speedlines; cw_debug_f32("hud.speedlines", &g_speedlines); }
  { extern int32_t g_snd_shots, g_music_on, g_rival_heard; cw_debug_i32("audio.shots", &g_snd_shots); cw_debug_i32("audio.music", &g_music_on); cw_debug_i32("audio.rivals_near", &g_rival_heard); }
  cw_debug_i32("p0.trick_wipes", &G.c[0].trick_wipes);
  cw_debug_i32("p0.dives", &G.c[0].dives);
  cw_debug_f32("p0.dive_max", &G.c[0].dive_max);
  cw_debug_f32("p0.dive_under", &G.c[0].dive_under);
  cw_debug_f32("p0.legout", &G.c[0].legout);
  cw_debug_i32("hud.pop_n", &g_hud_pop_n);
  cw_debug_i32("hud.pops", &g_hud_pops);
  cw_debug_i32("hud.pop_tick", &g_hud_pop_tick);
  /* the player's latest flight, kept by the game (game.h fly_probe): a check reads them once after a ranged run */
  cw_debug_i32("fly.tick", &g_fly.tick);
  cw_debug_i32("fly.flights", &g_fly.flights);
  cw_debug_f32("fly.low", &g_fly.low);
  cw_debug_f32("fly.foot", &g_fly.foot);
  cw_debug_f32("fly.turn_pitch", &g_fly.turn_pitch);
  cw_debug_f32("fly.turn_roll", &g_fly.turn_roll);
  cw_debug_i32("fly.turning", &g_fly.turning);
  cw_debug_i32("fly.down_ticks", &g_fly.down_ticks);
  cw_debug_i32("fly.land_tick", &g_fly.land_tick);
  cw_debug_i32("hud.words_n", &g_hud_words_n);
  cw_debug_i32("hud.label_hits", &g_hud_label_hits);
  cw_debug_f32_array("hud.label", g_hud_label, 4);
  cw_debug_f32("p0.pitch", &G.c[0].pitch);
  cw_debug_f32("p0.roll", &G.c[0].roll);
  cw_debug_f32("p0.dive_t", &G.c[0].dive_t);
  cw_debug_f32("p0.dive_len", &G.c[0].dive_len);
  cw_debug_f32("p0.dive_depth", &G.c[0].dive_depth);
  cw_debug_f32("p0.x", &G.c[0].x);
  cw_debug_f32("p0.y", &G.c[0].y);
  cw_debug_f32("p0.z", &G.c[0].z);
  cw_debug_f32("rider.foot_side", &rider_foot_side[0]);
  cw_debug_i32("ram.hits", &g_ram_hits);
  cw_debug_f32_array("ram.last", g_ram_last, 3);
  cw_debug_i32("ram.wipes", &g_ram_wipes);
  cw_debug_i32("p1.spills", &G.c[1].spills);
  cw_debug_i32("race.raw_flips", &g_raw_flips);
  cw_debug_i32("race.near_n", &g_near_n);
  cw_debug_f32("race.near", &g_near);
  cw_debug_f32("race.near_sum", &g_near_sum);
  cw_debug_f32("p0.stock", &G.c[0].stock);
  cw_debug_i32("p0.turbos", &G.c[0].turbos);
  cw_debug_i32("p0.place_changes", &G.c[0].place_changes);
  cw_debug_i32("p0.shown_place", &G.c[0].shown_place);
  cw_debug_i32("p0.slow_max", &G.c[0].slow_max);
  cw_debug_i32("p0.wall_hits", &G.c[0].wall_hits);
  cw_debug_f32("cam.fov", &G.cam_fov);
  cw_debug_command("buoy_style", "buoy_style 0: the small buoy, the distance gate's control", cmd_buoy_style);
  cw_debug_i32("hud.ptr_mode", &g_hud_ptr_mode);
  cw_debug_f32_array("hud.ptr", g_hud_ptr, 2);
  cw_debug_f32_array("hud.buoy", g_hud_buoy, 4);
  { extern int g_hud_chevron; cw_debug_i32("hud.chevron", &g_hud_chevron); }
  { extern float g_nc_look[6]; cw_debug_f32_array("nc.look", g_nc_look, 6); } /* the newcomer gate: one read a decision */
  cw_debug_i32("hud.wrong", &g_hud_wrong);
  cw_debug_command("detile", "detile <0..1>: the land's side_detile live", cmd_detile);
  cw_debug_command("peek", "peek x y z ax ay az: hold the camera there", cmd_peek);
  cw_debug_command("grounding", "grounding [lift]: every scatter instance against the ground, and the marks against the sea", cmd_grounding);
  cw_debug_command("current", "current 0: the river stands still", cmd_current);
  cw_debug_command("ai_ramps", "ai_ramps 0: no computer rider goes for a ramp", cmd_ai_ramps);
  cw_debug_command("ramp_seesaw", "ramp_seesaw 1: decks tip with the swell, the jump gate's control", cmd_ramp_seesaw);
  cw_debug_command("ramp_lift", "ramp_lift <metres>: the control for the ramps' gap count", cmd_ramp_lift);
  cw_debug_command("ramp_where", "ramp_where <index>: where that ramp is now", cmd_ramp_where);
  cw_debug_command("mode", "mode <0 championship, 1 time trial, 2 stunt, 3 free roam>", cmd_mode);
  wx_debug();
  { extern float g_top_now[RACERS]; cw_debug_f32_array("ai.top", g_top_now, RACERS); }
  { extern float g_dome_seen[3]; cw_debug_f32_array("wx.dome", g_dome_seen, 3); cw_debug_f32_array("wx.dome_want", g_wx_dome, 3); }
  { extern int g_hud_lost, g_hud_waves, g_hud_quiet; cw_debug_i32("hud.lost", &g_hud_lost); cw_debug_i32("hud.waves", &g_hud_waves); cw_debug_i32("hud.quiet", &g_hud_quiet); }
  cw_debug_command("finish", "finish: the human crosses the line now", cmd_finish);
  cw_debug_command("warp", "warp <metres>: every racer to that place on the course", cmd_warp);
}

/* a text trace for the sim's CPU tests (tools/sim-check.mjs) */
static void trace_line(void) {
  static int trace = -1;
  if (trace < 0) trace = getenv("JETSKI_TRACE") ? 1 : 0;
  if (!trace) return;
  float air = 0;
  printf("T %u %.3f", G.tick, G.clock);
  for (int i = 0; i < RACERS; i++) {
    const craft *k = &G.c[i];
    printf(" | %.1f %.2f %.1f %.2f %.1f %d %d %d %d %d %d %.2f %.3f %.3f", k->x, k->y, k->z, k->surf, k->speed, k->air ? 1 : 0, k->lap, k->meter, k->misses, k->passes, k->place, k->grounded, k->pitch, k->roll);
    air += k->air_time;
  }
  printf(" | %.3f %.3f\n", G.swell, air);
}

static void fixed_update(void *user, double dt) {
  (void)user;
  if (!cw_shell_update()) return;
  craft *c = &G.c[0];
  c->in.steer = cw_input_axis(0, a_left, a_right);
  c->in.lean = cw_action_strength(0, a_back) - cw_action_strength(0, a_fwd);
  c->in.throttle = cw_action_strength(0, a_throttle);
  c->in.trick = cw_action_pressed(0, a_trick);
  c->in_turbo = cw_action_pressed(0, a_turbo) || g_force_turbo > 0;
  c->in_kick = cw_action_pressed(0, a_kick);
  if (g_hold_ticks > 0) c->in.throttle = 0;
  if (g_force_turbo > 0) g_force_turbo--;
  menu_tick((float)dt);
  if (menu_blocks_sim() || g_freeze) {
    /* paused: the sea holds still with the race */
    cw_value v;
    if (cw_node_get_prop(G.water, "time_offset", &v)) cw_node_set(G.water, "time_offset", cw_val_float((float)v.f - (float)dt));
    return;
  }
  /* a whole race on the CPU in a minute: JETSKI_SIMX=<ticks a frame> with JETSKI_SMALL (native checks; a cart has no environment) */
  { static int simx = -1;
    if (simx < 0) simx = getenv("JETSKI_SIMX") ? atoi(getenv("JETSKI_SIMX")) : 0;
    for (int i = 0; i < simx; i++) { sim_tick((float)dt); G.wt_extra += dt; trace_line(); } }
  double t0 = cw_time();
  sim_tick((float)dt);
  view_tick((float)dt);
  fx_tick((float)dt);
  audio_tick((float)dt);
  float ms = (float)((cw_time() - t0) * 1000.0);
  perf[0] += (ms - perf[0]) * 0.02f; /* the game's own fixed tick, averaged */
  trace_line();
}

/* frame numbers for the perf script: game tick ms; frame GPU ms; the water pass; the scene pass; frame CPU (render callback's sync) */
static void perf_read(void) {
  { cw_render3d_stats rs; cw_render3d_get_stats(cw_scene_app_tree(), &rs); gpu_programs = (int32_t)rs.programs; }
  { cw_sky_state sk; cw_sky_get_state(cw_scene_app_tree(), &sk); sun_dbg[0] = sk.sun_direction.x, sun_dbg[1] = sk.sun_direction.y, sun_dbg[2] = sk.sun_direction.z; }
  cw_tree *t = cw_scene_app_tree();
  const cw_pass_stat *st;
  int n = cw_pipeline_pass_stats(cw_pipeline_of(t), &st);
  for (int i = 0; i < n; i++) {
    if (!strcmp(st[i].name, "water")) perf[2] = st[i].gpu_ms_avg, perf[5] = st[i].cpu_ms_avg;
    if (!strcmp(st[i].name, "scene")) perf[3] = st[i].gpu_ms_avg, perf[6] = st[i].cpu_ms_avg;
  }
  perf[1] += (cw_pipeline_gpu_ms(cw_pipeline_of(t)) - perf[1]) * 0.05f;
}

/* JETSKI_FRAMELOG=<file> (native): every slow frame by number and time, what the warm-up leaves for the first seconds of play */
static void frame_log(void) {
  static int on = -1, n;
  static double last;
  static FILE *out;
  if (on < 0) { const char *p = getenv("JETSKI_FRAMELOG"); out = p ? fopen(p, "w") : NULL; on = out ? 1 : 0; }
  if (!on) return;
  double now = cw_time();
  if (n > 0 && now - last > 0.022) fprintf(out, "SLOW %d %.1f state %d\n", n, (now - last) * 1000.0, G.state);
  if (n == 0) fprintf(out, "FIRST %.1f\n", now * 1000.0);
  last = now, n++;
  if (n % 60 == 0) {
    fprintf(out, "AT %d state %d\n", n, G.state);
    /* where the frame goes */
    cw_tree *t = cw_scene_app_tree();
    const cw_pass_stat *st;
    int np = cw_pipeline_pass_stats(cw_pipeline_of(t), &st);
    for (int i = 0; i < np; i++) if (st[i].ran) fprintf(out, "PASS %s gpu %.2f cpu %.2f\n", st[i].name, st[i].gpu_ms_avg, st[i].cpu_ms_avg);
    cw_render3d_stats rs;
    cw_render3d_get_stats(t, &rs);
    fprintf(out, "DRAW calls %d tris %d frame_gpu %.2f\n", rs.draw_calls, rs.triangles, cw_pipeline_gpu_ms(cw_pipeline_of(t)));
    fflush(out);
  }
}

static void render(void *user, double alpha) {
  (void)user;
  frame_log();
  double t1 = cw_time();
  view_sync(alpha);
  perf_read();
  if (cw_shell_hud_shown()) hud_draw(); /* not under the shell's menus */
  cw_shell_draw();
  perf[4] += ((float)((cw_time() - t1) * 1000.0) - perf[4]) * 0.05f;
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .width = 1920, .height = 1080, .debug = true, .reversed_depth = true, .init = init, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
  if (getenv("JETSKI_SMALL")) app.width = 192, app.height = 108; /* sim checks on a software GL */
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &app);
}
