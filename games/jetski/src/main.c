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
static cw_action a_left, a_right, a_back, a_fwd, a_throttle, a_trick;

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
  if (r) cw_reply_printf(r, "{\"course\":\"%s\",\"instances\":%d,\"gaps\":%d,\"worst\":%.3f,\"at\":[%.0f,%.0f],\"by_layer\":[%d,%d,%d,%d,%d],\"tide_low\":%.2f,\"swell_amp\":%.2f,\"shallowest_buoy_ground\":%.2f,\"shallowest_ramp_ground\":%.2f,\"ramp_gap_ticks\":%d}",
    d->name, total, gaps, worst, worst_at[0], worst_at[1], by_layer[0], by_layer[1], by_layer[2], by_layer[3], by_layer[4], tl, amp, shallowest_buoy, shallowest_ramp, G.ramp_gap);
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
  if (r) cw_reply_printf(r, "{\"x\":%.2f,\"z\":%.2f,\"h\":%.2f,\"sy\":%.3f,\"cy\":%.3f}", G.ramp_x[q], G.ramp_z[q], G.ramp_h[q], sy, cy);
}
/* mode <0 championship, 1 time trial> */
static void cmd_mode(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) g_mode = atoi(argv[1]) ? MODE_TIME_TRIAL : MODE_CHAMPIONSHIP;
  if (r) cw_reply_raw(r, "true");
}

static void init(void *user) {
  (void)user;
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("jetski: input.cfg had errors");
  a_left = cw_action_find("steer_left"), a_right = cw_action_find("steer_right");
  a_back = cw_action_find("lean_back"), a_fwd = cw_action_find("lean_fwd");
  a_throttle = cw_action_find("throttle"), a_trick = cw_action_find("trick");
  cw_shell_init(&(cw_shell_desc){ .title = "jetski", .max_players = 1, .start = start, .game_save_size = 64 });
  view_init();
  hud_init();
  audio_init();
  menu_init();
#ifdef JETSKI_AUTOPILOT
  menu_off();
  { const char *lv = getenv("JETSKI_LEVEL"); if (lv) menu_course(atoi(lv)); } /* the gates' run on another course */
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
  cw_debug_i32("gpu.programs", &gpu_programs);
  cw_debug_i32("ground.total", &ground_total);
  cw_debug_i32("ground.gaps", &ground_gaps);
  cw_debug_f32("ground.worst", &ground_worst);
  cw_debug_i32("race.level", &G.level);
  cw_debug_i32("p0.shortcuts", &G.c[0].shortcuts);
  cw_debug_i32("p0.shut_hits", &G.c[0].shut_hits);
  cw_debug_i32("race.mode", &g_mode);
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
  cw_debug_i32("ramp.gap", &G.ramp_gap);
  cw_debug_i32("ramp.points", &G.ramp_points);
  cw_debug_i32("ramp.gap_points", &G.ramp_gap_points);
  cw_debug_i32("ramp.belly_points", &G.ramp_belly_points);
  cw_debug_f32("ramp.gap_max", &G.ramp_gap_max);
  cw_debug_f32("ramp.belly_max", &G.ramp_belly_max);
  cw_debug_f32("ramp.tilt_max", &G.ramp_tilt_max);
  cw_debug_f32_array("ramp.now", G.ramp_now, RAMP_MAX);
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
  cw_debug_command("peek", "peek x y z ax ay az: hold the camera there", cmd_peek);
  cw_debug_command("grounding", "grounding [lift]: every scatter instance against the ground, and the marks against the sea", cmd_grounding);
  cw_debug_command("ramp_lift", "ramp_lift <metres>: the control for the ramps' gap count", cmd_ramp_lift);
  cw_debug_command("ramp_where", "ramp_where <index>: where that ramp is now", cmd_ramp_where);
  cw_debug_command("mode", "mode <0 championship, 1 time trial>", cmd_mode);
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
  menu_tick((float)dt);
  if (menu_blocks_sim()) {
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
  if (n % 60 == 0) { fprintf(out, "AT %d state %d\n", n, G.state); fflush(out); }
}

static void render(void *user, double alpha) {
  (void)user;
  frame_log();
  double t1 = cw_time();
  view_sync(alpha);
  perf_read();
  if (cw_shell_screen_now() == CW_SHELL_PLAY) hud_draw();
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
