/* jetski (working name): wave racing on a volcanic atoll. See README.md. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/platform.h"
#include "cartwheel/project.h"
#include "cartwheel/render2d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "game.h"

static float perf[7];
static cw_action a_left, a_right, a_back, a_fwd, a_throttle, a_trick, a_start;

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
  sim_reset(RACE_GRID);
  cw_reply_raw(r, "true");
}
static void cmd_warp(int argc, const char **argv, cw_reply *r) {
  /* warp <track metres>: every racer jumps there, for a look along the course */
  if (argc < 2) { cw_reply_raw(r, "false"); return; }
  float s = (float)atof(argv[1]);
  for (int i = 0; i < RACERS; i++) {
    craft *c = &G.c[i];
    float x, z, dx, dz;
    track_at(s - (float)(i * 11), &x, &z, &dx, &dz);
    c->x = c->px = x + dz * (i % 2 ? 3.0f : -3.0f);
    c->z = c->pz = z - dx * (i % 2 ? 3.0f : -3.0f);
    c->y = c->py = cw_water_height_at(G.water, c->x, c->z, cw_water_time(G.water));
    c->yaw = c->pyaw = cw_atan2(dx, dz);
    c->vx = dx * 22, c->vz = dz * 22, c->vy = 0;
    c->track_i = (int)(s / 2587.0f * 256) % 256;
    if (c->track_i < 0) c->track_i += 256;
    for (int k = 0; k < 29; k++) c->next_buoy = c->next_buoy; 
  }
  G.cam[0] = 0;
  G.look[0] = 0;
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

static void init(void *user) {
  (void)user;
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("jetski: input.cfg had errors");
  a_left = cw_action_find("steer_left"), a_right = cw_action_find("steer_right");
  a_back = cw_action_find("lean_back"), a_fwd = cw_action_find("lean_fwd");
  a_throttle = cw_action_find("throttle"), a_trick = cw_action_find("trick");
  a_start = cw_action_find("go");
  cw_shell_init(&(cw_shell_desc){ .title = "jetski", .max_players = 1, .start = start });
  view_init();
  hud_init();
  audio_init();
#ifdef JETSKI_AUTOPILOT
  if (getenv("JETSKI_SMALL")) { /* sim checks: nothing costly on the software rasteriser */
    cw_node_set(G.water, "reflection", cw_val_bool(false));
    cw_node_set(G.water, "refraction", cw_val_bool(false));
    cw_node_set(G.sun, "shadow", cw_val_bool(false));
    cw_node_set(G.water, "subdivisions", cw_val_int(16));
    cw_node_set(G.water, "ripple_cells", cw_val_int(64));
  }
#endif
  sim_reset(RACE_ATTRACT);
  cw_shell_start(1);
#ifdef JETSKI_AUTOPILOT
  warp_from_env();
#endif
  cw_debug_i32("race.state", &G.state);
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
  cw_debug_i32("p0.misses", &G.c[0].misses);
  cw_debug_i32("p0.passes", &G.c[0].passes);
  cw_debug_i32("p0.track_i", &G.c[0].track_i);
  cw_debug_f32("p0.dist", &G.c[0].dist);
  cw_debug_f32("p1.dist", &G.c[1].dist);
  cw_debug_f32("p2.dist", &G.c[2].dist);
  cw_debug_f32_array("cam.pos", G.cam, 3);
  cw_debug_i32("cam.under_ticks", &G.cam_under);
  cw_debug_f32_array("perf.ms", perf, 7);
  cw_debug_command("race", "start a race", cmd_race);
  cw_debug_command("warp", "warp <metres>: every racer to that place on the course", cmd_warp);
}

#ifdef JETSKI_AUTOPILOT
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
#endif

static void fixed_update(void *user, double dt) {
  (void)user;
  if (!cw_shell_update()) return;
  craft *c = &G.c[0];
  c->in.steer = cw_input_axis(0, a_left, a_right);
  c->in.lean = cw_action_strength(0, a_back) - cw_action_strength(0, a_fwd);
  c->in.throttle = cw_action_strength(0, a_throttle);
  c->in.trick = cw_action_pressed(0, a_trick);
#ifndef JETSKI_AUTOPILOT
  if ((G.state == RACE_ATTRACT || G.state == RACE_DONE) && cw_action_just_pressed(0, a_start)) sim_reset(RACE_GRID);
#endif
#ifdef JETSKI_AUTOPILOT
  /* a whole race on the CPU in a minute: JETSKI_SIMX=<ticks a frame> with JETSKI_SMALL */
  { static int simx = -1;
    if (simx < 0) simx = getenv("JETSKI_SIMX") ? atoi(getenv("JETSKI_SIMX")) : 0;
    for (int i = 0; i < simx; i++) { sim_tick((float)dt); G.wt_extra += dt; trace_line(); } }
#endif
  double t0 = cw_time();
  sim_tick((float)dt);
  view_tick((float)dt);
  fx_tick((float)dt);
  audio_tick((float)dt);
  float ms = (float)((cw_time() - t0) * 1000.0);
  perf[0] += (ms - perf[0]) * 0.02f; /* the game's own fixed tick, averaged */
  #ifdef JETSKI_AUTOPILOT
  trace_line();
  #endif
}

/* frame numbers for the perf script: game tick ms; frame GPU ms; the water pass; the scene pass; frame CPU (render callback's sync) */
static void perf_read(void) {
  cw_tree *t = cw_scene_app_tree();
  const cw_pass_stat *st;
  int n = cw_pipeline_pass_stats(cw_pipeline_of(t), &st);
  for (int i = 0; i < n; i++) {
    if (!strcmp(st[i].name, "water")) perf[2] = st[i].gpu_ms_avg, perf[5] = st[i].cpu_ms_avg;
    if (!strcmp(st[i].name, "scene")) perf[3] = st[i].gpu_ms_avg, perf[6] = st[i].cpu_ms_avg;
  }
  perf[1] += (cw_pipeline_gpu_ms(cw_pipeline_of(t)) - perf[1]) * 0.05f;
}

static void render(void *user, double alpha) {
  (void)user;
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
#ifdef JETSKI_AUTOPILOT
  if (getenv("JETSKI_SMALL")) app.width = 192, app.height = 108; /* sim checks on a software GL */
#endif
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &app);
}
