/* Tux Racer on cartwheel: the app and the game flow of the original 0.61:
 *   title (Cup / Practice / Credits / Quit) -> cup race list with lives, or
 *   practice course list with conditions, wind and mirror -> loading ->
 *   intro (the walk-in keyframes) -> racing <-> pause -> race over -> back.
 * The engine owns the frame: fixed ticks drive the state machine, render
 * draws the HUD or the menus over the 3D scene. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/environment.h"
#include "cartwheel/gi.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/platform.h"
#include "cartwheel/project.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/gpu.h"
#include "cartwheel/quality.h"
#include "cartwheel/quality_menu.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "game.h"

typedef enum { ST_TITLE, ST_PRACTICE, ST_CUP, ST_CREDITS, ST_LOADING, ST_INTRO, ST_RACING, ST_PAUSED, ST_RACE_OVER } game_state;
enum { COND_SUNNY, COND_CLOUDY, COND_NIGHT, COND_COUNT };
enum { MODE_PRACTICE, MODE_CUP };
#define INIT_NUM_LIVES 4
#define DIFFICULTY 1 /* DIFFICULTY_LEVEL_NORMAL, the original's default */
#define MAX_COURSES 16
#define MAX_RACES 8
#define MAX_KEYS 32

typedef struct course_info { char dir[64], title[64], desc[256]; float par, length; bool contrib; } course_info;
typedef struct best_entry { uint32_t course_hash; float time; int32_t herring, score; } best_entry;
typedef struct save_data {
  uint32_t magic;            /* 'TRSV' */
  uint16_t version, size;
  uint32_t checksum;         /* FNV-1a over everything after this field */
  best_entry best[MAX_COURSES];
  int32_t cup_race;          /* the next race of the Canadian Cup */
  int32_t cup_lives;
  uint8_t cup_won;
  uint8_t gfx[3];            /* the graphics settings (cw_quality_saved, 24 bits little-endian); 0 = never chosen */
} save_data;
static save_data SAVE;
typedef struct cup_race { char dir[64]; int herring[4]; float time[4]; int score[4]; bool mirrored, windy, snowing; int conditions; } cup_race;
typedef struct cup_def { char name[64]; cup_race races[MAX_RACES]; int count; } cup_def;
static const char *const COND_NAME[COND_COUNT] = { "Sunny", "Cloudy", "Night" };

static struct {
  cw_tree *tree;
  t3_texture *sky_cube[3]; /* the original sunny / cloudy / night cubes */
  t3_pmrem_generator *pmrem;
  t3_render_target *sky_env[3]; /* each cube prefiltered: the scene's image-based light */
  int env_pending;              /* condition whose environment waits for the renderer (-1 none) */
  cw_node tux_shadow;           /* a soft contact shadow under the belly */
  float squash, squash_v;       /* landing squash (render only) */
  bool was_airborne;
  cw_node root, cam, sky, sun, moon, env, fog, post, vis, probe;
  course course;
  tux_model tux;
  tux_phys phys;
  tux_view view, view_prev;
  float dbg_alpha, dbg_body_r[3], dbg_cam_r[6];  /* view_prev: the view at the previous fixed tick (render interpolates) */
  bool course_loaded;
  bool backdrop;           /* the loaded course is the menus' backdrop (camera orbits, Tux hidden) */
  float backdrop_t;
  /* data */
  course_info courses[MAX_COURSES];
  int ncourses;
  cup_def cups[2];
  int ncups;
  tux_keyframe keys[MAX_KEYS];
  int nkeys;
  char credits[40][96];
  int ncredits;
  /* flow */
  game_state state;
  int mode, cursor, pause_cursor;
  int cup_idx, race_idx, lives;
  int practice_course, practice_cond;
  bool practice_wind, practice_mirror, practice_snow;
  /* the race */
  const course_info *info;
  int conditions;
  bool windy, mirrored, snowing;
  float race_time, key_time, intro_time, over_time;
  double fx_time; /* a running clock for the shaders (ticks) */
  int herring, score;
  bool finished, aborted, won, new_best;
  int reset_ticks;
  int herring_req, score_req;
  float time_req;
  /* actions */
  cw_action a_left, a_right, a_paddle, a_brake, a_jump, a_reset, a_view, a_quit, a_pause;
  cw_action m_up, m_down, m_left, m_right, m_accept, m_back;
  int lever_edges;           /* the "Tree edges" graphics lever: Sharp / Smooth (MSAA 2x + alpha to coverage) */
  int lever_terrain;
  int lever_tux_shadow, lever_snow;      /* the "Tux shadow" lever: contact decal only, or the sun's cascades too */         /* the "Terrain" lever: the tier (Low..Ultra), the preset's by default */
  /* debug */
  int32_t ticks, dbg_state, dbg_finished, dbg_airborne, dbg_tree, dbg_contacts;
  float dbg_pos[3], dbg_vel[3], dbg_speed, dbg_fric;
  uint32_t phys_hash;
} G;

/* ── save data ───────────────────────────────────────────────────────── */

static uint32_t fnv(const void *p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 16777619u;
  return h;
}
static uint32_t save_checksum(void) { return fnv((const uint8_t *)&SAVE + offsetof(save_data, best), sizeof SAVE - offsetof(save_data, best)); }
static void save_write(void) {
  SAVE.magic = 0x56535254u, SAVE.version = 1, SAVE.size = sizeof SAVE;
  SAVE.checksum = save_checksum();
  cw_save_commit();
}
static uint32_t gfx_saved(void) { return SAVE.gfx[0] | (uint32_t)SAVE.gfx[1] << 8 | (uint32_t)SAVE.gfx[2] << 16; }
static void gfx_store(void) {
  uint32_t q = cw_quality_saved();
  SAVE.gfx[0] = q & 255, SAVE.gfx[1] = (q >> 8) & 255, SAVE.gfx[2] = (q >> 16) & 255;
}
static void save_read(void) {
  if (SAVE.magic != 0x56535254u || SAVE.version != 1 || SAVE.size != sizeof SAVE || SAVE.checksum != save_checksum()) memset(&SAVE, 0, sizeof SAVE);
}
static best_entry *best_of(const char *dir) {
  uint32_t h = fnv(dir, strlen(dir));
  best_entry *free_slot = NULL;
  for (int i = 0; i < MAX_COURSES; i++) {
    if (SAVE.best[i].course_hash == h) return &SAVE.best[i];
    if (!free_slot && !SAVE.best[i].course_hash) free_slot = &SAVE.best[i];
  }
  if (free_slot) free_slot->course_hash = h;
  return free_slot;
}

/* ── data files ──────────────────────────────────────────────────────── */

static char *field(char **p) { /* "a | b | c" fields of courses.txt */
  char *s = *p;
  if (!s) return "";
  char *bar = strstr(s, " | ");
  if (bar) { *bar = 0; *p = bar + 3; } else *p = NULL;
  return s;
}

static void load_data(void) {
  size_t len;
  char *txt = cw_asset_load("courses/courses.txt", &len), *save = NULL;
  if (txt) {
    for (char *line = strtok_r(txt, "\n", &save); line && G.ncourses < MAX_COURSES; line = strtok_r(NULL, "\n", &save)) {
      course_info *c = &G.courses[G.ncourses];
      int contrib, n = 0;
      if (sscanf(line, "course %63s %f %d %f%n", c->dir, &c->par, &contrib, &c->length, &n) != 4) continue;
      c->contrib = contrib != 0;
      char *p = line + n;
      field(&p); /* the rest of the numbers line up to the first bar */
      snprintf(c->title, sizeof c->title, "%s", field(&p));
      snprintf(c->desc, sizeof c->desc, "%s", field(&p));
      G.ncourses++;
    }
    free(txt);
  }
  txt = cw_asset_load("courses/cups.txt", &len), save = NULL;
  if (txt) {
    cup_def *cup = NULL;
    for (char *line = strtok_r(txt, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
      if (!strncmp(line, "cup ", 4) && G.ncups < 2) { cup = &G.cups[G.ncups++]; snprintf(cup->name, sizeof cup->name, "%s", line + 4); }
      else if (!strncmp(line, "race ", 5) && cup && cup->count < MAX_RACES) {
        cup_race *r = &cup->races[cup->count];
        char cond[16];
        int m, w, s;
        if (sscanf(line + 5, "%63s %d %d %d %d %f %f %f %f %d %d %d %d %d %15s %d %d", r->dir, &r->herring[0], &r->herring[1], &r->herring[2], &r->herring[3],
                   &r->time[0], &r->time[1], &r->time[2], &r->time[3], &r->score[0], &r->score[1], &r->score[2], &r->score[3], &m, cond, &w, &s) == 17) {
          r->mirrored = m, r->windy = w, r->snowing = s;
          r->conditions = !strcmp(cond, "night") ? COND_NIGHT : !strcmp(cond, "cloudy") ? COND_CLOUDY : COND_SUNNY;
          cup->count++;
        }
      }
    }
    free(txt);
  }
  G.nkeys = tux_load_walk(G.keys, MAX_KEYS);
  txt = cw_asset_load("credits.txt", &len), save = NULL;
  if (txt) {
    for (char *line = strtok_r(txt, "\n", &save); line && G.ncredits < 40; line = strtok_r(NULL, "\n", &save)) snprintf(G.credits[G.ncredits++], 96, "%s", line);
    free(txt);
  }
  cw_info("data: %d courses, %d cups, %d keyframes", G.ncourses, G.ncups, G.nkeys);
}

static const course_info *find_course(const char *dir) {
  for (int i = 0; i < G.ncourses; i++) if (!strcmp(G.courses[i].dir, dir)) return &G.courses[i];
  return G.ncourses ? &G.courses[0] : NULL;
}

/* ── the world ───────────────────────────────────────────────────────── */

static cw_node add(cw_node parent, const char *type, const char *name) {
  cw_node n = cw_node_new(type, name);
  cw_node_add_child(parent, n);
  return n;
}

static void build_world(void) {
  G.root = cw_tree_root(G.tree);
  G.vis = add(G.root, "VisibilityManager", "Visibility");
  cw_node_set(G.vis, "occlusion_cull", cw_val_bool(false));
  G.sun = add(G.root, "DirectionalLight3D", "Sun");
  cw_node_set(G.sun, "shadow", cw_val_bool(true));
  cw_node_set(G.sun, "shadow_size", cw_val_int(2048));
  cw_node_set(G.sun, "shadow_cascades", cw_val_int(3));
  cw_node_set(G.sun, "shadow_distance", cw_val_float(160));
  cw_node_set(G.sun, "shadow_bias", cw_val_float(-0.0004f));
  cw_node_set(G.sun, "shadow_normal_bias", cw_val_float(0.05f));
  cw_node_set(G.sun, "cascade_fade", cw_val_bool(true));
  cw_node_set(G.sun, "fog_energy", cw_val_float(1));
  G.env_pending = -1;
  G.moon = add(G.root, "DirectionalLight3D", "Moon");
  cw_node_set(G.moon, "color", cw_val_color(0.39f, 0.51f, 0.88f, 1));
  cw_node_set(G.moon, "energy", cw_val_float(0));
  cw_node_set(G.moon, "shadow", cw_val_bool(!getenv("TUXRACER_NO_MOON_SHADOW"))); /* dev: the moon's cascades off */
  cw_node_set(G.moon, "shadow_size", cw_val_int(2048));
  cw_node_set(G.moon, "shadow_cascades", cw_val_int(3));
  cw_node_set(G.moon, "shadow_distance", cw_val_float(120));
  cw_node_set(G.moon, "shadow_bias", cw_val_float(-0.0004f));
  cw_node_set(G.moon, "shadow_normal_bias", cw_val_float(0.05f));
  cw_node_set(G.moon, "fog_energy", cw_val_float(1.5f));
  cw_node3d_look_at(G.moon, (t3_vec3){ -1, -1.2f, -0.6f }, (t3_vec3){ 0, 1, 0 });
  G.sky = add(G.root, "ProceduralSky", "Sky");
  cw_node_set(G.sky, "sun", cw_val_string("../Sun"));
  cw_node_set(G.sky, "latitude", cw_val_float(52));
  cw_node_set(G.sky, "declination", cw_val_float(-5));
  cw_node_set(G.sky, "north", cw_val_float(200));
  cw_node_set(G.sky, "ground_color", cw_val_color(0.86f, 0.9f, 0.97f, 1)); /* snowfield below the horizon: never a beige floor past the course's edge */
  G.env = add(G.root, "WorldEnvironment", "Env");
  G.fog = add(G.root, "HeightFog", "Fog");
  /* a reflection probe riding with Tux: he and what is near him reflect the snow and the trees and
   * take their ambient light from them (snow bounce on his belly) instead of from the sky cube alone */
  /* a reflection probe riding with Tux was tried (TUXRACER_PROBE=1): captured on demand at the start it
   * made no visible difference next to the sky cube's environment (snow everywhere either way), and a
   * capture every 24 frames hitched 5 ms, so it is off */
  if (getenv("TUXRACER_PROBE")) {
  G.probe = add(G.root, "ReflectionProbe3D", "TuxProbe");
  cw_node_set(G.probe, "size", cw_val_vec3(60, 40, 60));
  cw_node_set(G.probe, "blend_distance", cw_val_float(12));
  cw_node_set(G.probe, "resolution", cw_val_int(64));
  cw_node_set(G.probe, "update_mode", cw_val_enum("ON_DEMAND")); /* a capture every few frames hitched 5 ms */
  cw_node_set(G.probe, "far", cw_val_float(400));
  cw_node_set(G.probe, "ambient", cw_val_bool(true));
  }
  G.post = add(G.root, "PostProcess", "Post");
  cw_node_set(G.post, "tonemap", cw_val_enum("AGX"));
  cw_node_set(G.post, "exposure", cw_val_float(0.95f));
  cw_node_set(G.post, "bloom", cw_val_bool(true));
  cw_node_set(G.post, "bloom_intensity", cw_val_float(0.06f));
  cw_node_set(G.post, "bloom_threshold", cw_val_float(1.1f));
  cw_node_set(G.post, "bloom_knee", cw_val_float(0.5f));
  /* no SSAO: on flat bright snow the GTAO has nothing to occlude and its
   * screen-space noise tile shows as an 8-pixel grid crawling over the
   * ground ("plaid cheesecloth"); measured as a periodic pattern at every
   * preset, gone with it off. The quality setting "Ambient occlusion" then
   * has nothing to turn on, which is right for this game. */
  /* ambient occlusion: contact shade under the trees and Tux. 4 slices x 10 steps hides the GTAO noise tile
   * that showed as a grid at the default 2 x 6 (measured: no periodic peak on the snow at these settings) */
  cw_node_set(G.post, "ssao", cw_val_bool(false)); /* measured 2026-10-23: 7 ms of a 12 ms retina frame even at 3 slices x 6 steps, radius 0.45; the contact shade is not worth that */
  cw_node_set(G.post, "ssao_slices", cw_val_int(3));
  cw_node_set(G.post, "ssao_steps", cw_val_int(6));
  cw_node_set(G.post, "ssao_radius", cw_val_float(0.45f)); /* a 0.9 radius cost 6 ms at retina (long steps thrash the cache) */
  cw_node_set(G.post, "ssao_intensity", cw_val_float(0.7f));
  cw_node_set(G.post, "ssr", cw_val_bool(true));          /* ice mirrors the sky cube and the trees */
  cw_node_set(G.post, "ssr_max_distance", cw_val_float(1.5f));
  cw_node_set(G.post, "ssr_intensity", cw_val_float(1));
  cw_node_set(G.post, "taa", cw_val_bool(false));         /* the engine's TAA has no object velocity yet: Tux (the one thing the camera tracks) shimmered under the jitter; the AA setting (SMAA / FXAA) does the edges */
  cw_node_set(G.post, "aa", cw_val_enum("NONE"));
  cw_node_set(G.post, "vignette", cw_val_bool(false)); /* the user's call: no darkened corners */
  G.tux_shadow = add(G.root, "Decal3D", "TuxShadow");
  cw_node_set(G.tux_shadow, "blend", cw_val_enum("MULTIPLY"));
  cw_node_set(G.tux_shadow, "size", cw_val_vec3(1.1f, 1.1f, 1.2f));
  cw_node_set(G.tux_shadow, "color", cw_val_color(0.45f, 0.5f, 0.62f, 0.7f));
  cw_node_set(G.tux_shadow, "normal_fade", cw_val_float(0.5f));
  cw_node_set(G.tux_shadow, "visible", cw_val_bool(false));
  fx_init(G.root);
  G.cam = add(G.root, "Camera3D", "Camera");
  cw_node_set(G.cam, "fov", cw_val_float(60));
  cw_node_set(G.cam, "near", cw_val_float(0.2f));
  cw_node_set(G.cam, "far", cw_val_float(3000));
  if (!tux_load(&G.tux, cw_render3d_scene(G.tree))) CW_FATAL("tux model did not load");
  tux_set_visible(&G.tux, false);
  static const char *const CUBES[3] = { "sky/sunny.cube", "sky/cloudy.cube", "sky/night.cube" };
  for (int i = 0; i < 3; i++) {
    size_t len;
    uint8_t *raw = cw_asset_load(CUBES[i], &len);
    if (!raw) { cw_warn("%s missing", CUBES[i]); continue; }
    int size = (int)sqrtf((float)(len / 24));
    const uint8_t *faces[6];
    for (int f = 0; f < 6; f++) faces[f] = raw + (size_t)f * size * size * 4;
    G.sky_cube[i] = t3_cube_texture_new(size, faces);
    free(raw);
  }
}

/* the original's lighting scripts (sunny_light / foggy_light / night_light.tcl) as engine settings */
static void apply_conditions(int cond) {
  bool night = cond == COND_NIGHT, cloudy = cond == COND_CLOUDY;
  /* sunny: the engine's sky with the cube's mountains on a ring (skyring.c); night and cloudy: the original cube */
  bool sunny = !night && !cloudy;
  sky_ring_init(cw_render3d_scene(G.tree));
  sky_ring_set(sunny, night);
  course_set_night(night);
  fx_set_motes(sunny);
  t3_scene_set_background_texture(cw_render3d_scene(G.tree), NULL); /* every condition: the engine's sky, the ring and the deck */
  cw_node_set(G.sky, "time_of_day", cw_val_float(night ? 23.5f : 9.2f)); /* a low morning sun: long tree shadows, warm snow */
  cw_node_set(G.sky, "sun_energy", cw_val_float(cloudy ? 1.6f : 5.2f));
  cw_node_set(G.sky, "mie", cw_val_float(cloudy ? 4 : 0.45f));
  cw_node_set(G.sky, "rayleigh", cw_val_float(cloudy ? 1.6f : 1.15f));
  cw_node_set(G.sky, "night_color", night ? cw_val_color(0.0f, 0.004f, 0.016f, 1) : cw_val_color(0.0004f, 0.0006f, 0.0012f, 1));
  cw_node_set(G.sky, "background", cw_val_bool(true));
  cw_node_set(G.sky, "sky_energy", cw_val_float(sunny ? 1.7f : cloudy ? 1.3f : 1)); /* the sunny exposure (0.62) would dim a physical sky */
  cw_node_set(G.sky, "sun_disk_intensity", cw_val_float(0.02f));
  cw_node_set(G.sky, "environment", cw_val_bool(false)); /* the cube itself lights the scene (env_pending) */
  G.env_pending = cond;
  cw_node_set(G.env, "use_background", cw_val_bool(false));
  /* one directional light at a time (a hidden light is not in the material's light count) */
  cw_node_set(G.sun, "visible", cw_val_bool(!night));
  cw_node_set(G.moon, "visible", cw_val_bool(night));
  cw_node_set(G.moon, "energy", cw_val_float(0.9f));
  cw_render3d_scene(G.tree)->background_intensity = night ? 0.4f : 1;
  cw_node_set(G.env, "ambient_energy", cw_val_float(night ? 0.07f : cloudy ? 0.12f : 0.05f)); /* night: the sky glow fills the slopes the moon misses */
  cw_node_set(G.env, "ambient_color", night ? cw_val_color(0.0f, 0.3f, 1.0f, 1) : cw_val_color(1, 1, 1, 1));
  cw_node_set(G.env, "hemi_energy", cw_val_float(night ? 0.14f : cloudy ? 0.35f : 0.2f));
  cw_node_set(G.env, "sky_color", night ? cw_val_color(0.1f, 0.2f, 0.5f, 1) : cloudy ? cw_val_color(0.6f, 0.62f, 0.68f, 1) : cw_val_color(0.6f, 0.75f, 1.0f, 1));
  cw_node_set(G.env, "ground_color", night ? cw_val_color(0.05f, 0.08f, 0.15f, 1) : cw_val_color(0.8f, 0.85f, 0.9f, 1));
  cw_node_set(G.fog, "density", cw_val_float(cloudy ? 0.006f : night ? 0.0025f : 0.0007f)); /* cloudy: mist that still shows the overcast deck and the near range */
  cw_node_set(G.fog, "height_falloff", cw_val_float(cloudy ? 0 : 0.004f));
  cw_node_set(G.fog, "base_height", cw_val_float(-400));
  cw_node_set(G.fog, "use_sky_color", cw_val_bool(false));
  cw_node_set(G.fog, "color", night ? cw_val_color(0.02f, 0.04f, 0.1f, 1) : cloudy ? cw_val_color(0.58f, 0.59f, 0.65f, 1) : cw_val_color(0.70f, 0.80f, 0.95f, 1));
  cw_node_set(G.fog, "sun_scatter", cw_val_float(cloudy ? 0.1f : 0.3f));
  cw_node_set(G.fog, "max_opacity", cw_val_float(cloudy ? 0.9f : night ? 0.38f : 0.6f)); /* night: the far snow stays snow, not a blue plane */
  cw_node_set(G.fog, "start_distance", cw_val_float(cloudy ? 0 : 80));
  cw_node_set(G.fog, "sky_distance", cw_val_float(0)); /* the sky is never fogged: the overcast deck is the ceiling, its own far fade does the rest */
  cw_node_set(G.fog, "volumetric", cw_val_bool(night || cloudy)); /* moonbeams through the trees, a real overcast */
  cw_node_set(G.fog, "volumetric_distance", cw_val_float(night ? 90 : 70));
  cw_node_set(G.fog, "volumetric_ambient", cw_val_float(cloudy ? 1 : 0.6f));
  cw_node_set(G.post, "exposure", cw_val_float(night ? 0.85f : cloudy ? 0.9f : 0.62f));
  /* a colour grade per condition (tools/make-luts.mjs) and film grain in the dark */
  cw_node_set(G.post, "grade", cw_val_bool(true));
  cw_node_set(G.post, "lut", cw_val_string(night ? "luts/night.cube" : cloudy ? "luts/cloudy.cube" : "luts/sunny.cube"));
  cw_node_set(G.post, "grade_intensity", cw_val_float(0.8f));
  cw_node_set(G.post, "grain", cw_val_bool(night));
  cw_node_set(G.post, "grain_intensity", cw_val_float(0.03f)); /* sunny: the snow out of AgX's shoulder, so its texture, glints and cloud shadows show */
}

/* ── races ───────────────────────────────────────────────────────────── */

static t3_vec3 start_pos(void) {
  const course *c = &G.course;
  return (t3_vec3){ c->start_x, course_height(c, c->start_x, -c->start_d), -c->start_d };
}

static void unload_race(void) {
  if (!G.course_loaded) return;
  course_destroy_objects(&G.course);
  course_unload(&G.course);
  fx_set_snowing(false);
  G.course_loaded = false;
  tux_set_visible(&G.tux, false);
}

static void begin_loading(const char *dir, int conditions, bool windy, bool mirrored, bool snowing) {
  G.backdrop = false;
  G.info = find_course(dir);
  G.conditions = conditions, G.windy = windy, G.mirrored = mirrored, G.snowing = snowing;
  G.state = ST_LOADING;
  audio_music(MUS_NONE);
}

static void load_race(void) {
  unload_race();
  if (G.backdrop) {
    /* the title's backdrop: the course under the sun, nobody on it */
    if (!course_load(&G.course, G.info->dir, G.root, false)) { cw_error("course %s did not load", G.info->dir); G.state = ST_TITLE; G.backdrop = false; return; }
    course_build_objects(&G.course, G.tree);
    G.course_loaded = true;
    apply_conditions(G.conditions);
    fx_set_snowing(false);
    fx_track_reset();
    tux_set_visible(&G.tux, false);
    G.state = ST_TITLE;
    return;
  }
  if (!course_load(&G.course, G.info->dir, G.root, G.mirrored)) { cw_error("course %s did not load", G.info->dir); G.state = ST_TITLE; return; }
  course_build_objects(&G.course, G.tree);
  G.course_loaded = true;
  apply_conditions(G.conditions);
  fx_set_snowing(G.snowing);
  fx_set_wind(G.windy);
  if (!cw_body3d_valid(G.phys.body)) physics_init(&G.phys, G.tree, &G.course);
  G.phys.course = &G.course;
  G.phys.windy = G.windy;
  G.phys.enabled = false;
  t3_vec3 s = start_pos();
  physics_place(&G.phys, (t3_vec3){ s.x, s.y + TUX_RADIUS, s.z }, false);
  fx_track_reset();
  G.race_time = 0, G.herring = 0, G.score = 0, G.finished = G.aborted = G.won = false, G.reset_ticks = 0;
  G.key_time = G.nkeys ? G.keys[0].time : 0;
  G.intro_time = 0;
  view_reset(&G.view, VIEW_ABOVE);
  tux_set_visible(&G.tux, true);
  G.state = ST_INTRO;
  audio_music(MUS_RACE);
}

static void start_racing(void) {
  t3_vec3 s = start_pos();
  physics_place(&G.phys, (t3_vec3){ s.x, s.y + TUX_RADIUS, s.z }, true);
  G.phys.enabled = true;
  view_reset(&G.view, VIEW_BEHIND);
  G.state = ST_RACING;
  textures_trail_reset(G.phys.pos);
  if (cw_node_valid(G.probe)) cw_reflection_probe_update(G.probe); /* one capture of the course around the start */
}

/* intro.c + keyframe.c: the walk-in; root yaw and pitch, shoulders and hips from the keys */
static void intro_tick(float dt) {
  G.key_time += dt;
  int idx = 1;
  while (idx < G.nkeys && G.key_time >= G.keys[idx].time) idx++;
  if (idx >= G.nkeys || G.nkeys < 2) { start_racing(); return; }
  const tux_keyframe *a = &G.keys[idx - 1], *b = &G.keys[idx];
  float frac = fabsf(a->time - b->time) < 1e-6f ? 1 : (G.key_time - b->time) / (a->time - b->time); /* keyframe.c's interp: frac * v1 + (1 - frac) * v2 */
#define LERP(f) (frac * a->f + (1 - frac) * b->f)
  const course *c = &G.course;
  /* tux_walk.tcl's columns are x, z, y (keyframe.c: pos.x = v + start.x,
   * pos.z = -v + start.z, pos.y = v + the terrain): he walks in from the
   * side a metre uphill, turns and flops onto the start */
  t3_vec3 pos = { LERP(pos[0]) + c->start_x, LERP(pos[2]), -LERP(pos[1]) - c->start_d };
  /* the keyframes lift him 0.455 above the ground, tux.tcl's feet reach 0.525 below his origin: the
   * difference (the original walked 7 cm deep too) plus a hair so the soles sit on the snow */
  pos.y += course_height(c, pos.x, pos.z) + (0.525f - 0.455f) + 0.01f;
  t3_quat q = t3_quat_multiply(tr_quat_axis_angle((t3_vec3){ 0, 1, 0 }, TR_DEG(LERP(yaw))), tr_quat_axis_angle((t3_vec3){ 1, 0, 0 }, TR_DEG(LERP(pitch))));
  tux_set_transform(&G.tux, pos, q);
  tux_joint_reset(&G.tux, J_LSHOULDER);
  tux_joint_rotate(&G.tux, J_LSHOULDER, 'z', LERP(l_shoulder));
  tux_joint_reset(&G.tux, J_RSHOULDER);
  tux_joint_rotate(&G.tux, J_RSHOULDER, 'z', LERP(r_shoulder));
  tux_joint_reset(&G.tux, J_LHIP);
  tux_joint_rotate(&G.tux, J_LHIP, 'z', LERP(l_hip));
  tux_joint_reset(&G.tux, J_RHIP);
  tux_joint_rotate(&G.tux, J_RHIP, 'z', LERP(r_hip));
  for (int j = J_LKNEE; j <= J_RANKLE; j++) tux_joint_reset(&G.tux, j);
  tux_joint_reset(&G.tux, J_NECK);
  tux_joint_reset(&G.tux, J_HEAD);
  tux_joint_reset(&G.tux, J_TAIL);
#undef LERP
  G.phys.pos = pos, G.phys.vel = (t3_vec3){ 0, 0, 0 }, G.phys.speed = 0;
  cw_body3d_set_transform(G.phys.body, cw_xform3_at(pos.x, pos.y + TUX_RADIUS, pos.z));
  view_update(&G.view, &G.phys, c, dt);
  /* the walk-in as a shot: a low camera on his face as he comes in from the side, swinging round and
   * pulling back to settle where the behind camera starts (so the cut to racing is seamless) */
  float total = G.keys[G.nkeys - 1].time;
  float t = total > 0 ? fminf(1, G.key_time / total) : 1;
  float e = t * t * (3 - 2 * t);
  float phi = TR_DEG(135.0f * (1 - e)); /* from his front-left round to straight behind */
  float r = 5.0f - 1.7f * e, h = 0.9f + 1.4f * e;
  t3_vec3 eye = { pos.x + sinf(phi) * r, pos.y + h, pos.z + cosf(phi) * r };
  float gy = course_height(c, eye.x, eye.z); /* the start pad has berms: keep the eye above the ground around it too */
  for (int k = 0; k < 4; k++) { float ax = eye.x + (k & 1 ? 1.2f : -1.2f), az = eye.z + (k & 2 ? 1.2f : -1.2f); float h2 = course_height(c, ax, az); if (h2 > gy) gy = h2; }
  gy += getenv("TUXRACER_INTRO_FLOOR") ? (float)atof(getenv("TUXRACER_INTRO_FLOOR")) : 0.9f;
  if (eye.y < gy) eye.y = gy;
  t3_vec3 look = { pos.x, pos.y + 0.35f, pos.z };
  G.view.pos = eye;
  G.view.dir = t3_vec3_normalize(t3_vec3_sub(look, eye));
  G.view.up = (t3_vec3){ 0, 1, 0 };
  G.view.initialized = true;
}

/* Depth of field (v0.18 PostProcess dof) frames the shots that are not
 * played: the menus' drifting backdrop, the walk-in and the results stay on
 * their subject with the far course soft. Racing is sharp to the horizon:
 * the player reads the course far ahead. */
static void apply_dof(void) {
  if (!cw_node_valid(G.post)) return;
  bool menu = G.state == ST_TITLE || G.state == ST_PRACTICE || G.state == ST_CUP || G.state == ST_CREDITS;
  bool on = G.course_loaded && (G.state == ST_INTRO || G.state == ST_RACE_OVER || (menu && G.backdrop));
  cw_node_set(G.post, "dof", cw_val_bool(on));
  if (!on) return;
  float dist, range, blur;
  if (menu) dist = 24, range = 14, blur = 5; /* the backdrop orbits 28 m out: the slope mid-frame sharp, the ring soft */
  else { /* on Tux, the range opening with the distance so the start pad stays readable */
    dist = t3_vec3_length(t3_vec3_sub(G.phys.pos, G.view.pos));
    range = 1.5f + dist * 0.35f;
    blur = 7;
  }
  cw_node_set(G.post, "dof_focus_distance", cw_val_float(dist));
  cw_node_set(G.post, "dof_focus_range", cw_val_float(range));
  cw_node_set(G.post, "dof_blur", cw_val_float(blur));
  cw_node_set(G.post, "dof_near_blur", cw_val_float(0.6f));
}

static void race_over(bool aborted) {
  G.phys.enabled = false;
  G.aborted = aborted;
  float par = G.mode == MODE_CUP ? G.cups[G.cup_idx].races[G.race_idx].time[0] : G.info->par; /* game_logic_util.c: the easy time is par */
  G.score = aborted ? 0 : (int)fmaxf(0, 100 * (par - G.race_time) + 200 * (float)G.herring);
  if (G.mode == MODE_CUP) {
    const cup_race *r = &G.cups[G.cup_idx].races[G.race_idx];
    G.time_req = r->time[DIFFICULTY], G.herring_req = r->herring[DIFFICULTY], G.score_req = r->score[DIFFICULTY];
    G.won = !aborted && G.race_time <= G.time_req && G.herring >= G.herring_req && G.score >= G.score_req;
    if (!G.won) G.lives--;
  }
  /* best score (and its time and herring) per course; the cup's progress */
  G.new_best = false;
  if (!aborted) {
    best_entry *b = best_of(G.info->dir);
    if (b && (b->score == 0 && b->time == 0 ? true : G.score > b->score)) { b->score = G.score, b->time = G.race_time, b->herring = G.herring; G.new_best = true; }
  }
  if (G.mode == MODE_CUP) {
    SAVE.cup_race = G.won ? G.race_idx + 1 : G.race_idx;
    SAVE.cup_lives = G.lives;
    if (G.won && G.race_idx + 1 >= G.cups[G.cup_idx].count) { SAVE.cup_won = 1; SAVE.cup_race = 0; SAVE.cup_lives = INIT_NUM_LIVES; }
    if (G.lives <= 0) { SAVE.cup_race = 0; SAVE.cup_lives = INIT_NUM_LIVES; }
  }
  save_write();
  G.over_time = 0;
  G.state = ST_RACE_OVER;
  G.dbg_finished = 1;
  audio_slide(G.phys.surf_w, 0, false, false);
  audio_music(MUS_OVER);
}

static void racing_tick(float dt) {
  tux_phys *p = &G.phys;
  float turn = cw_input_axis(0, G.a_left, G.a_right);
  physics_control(p, turn, cw_action_pressed(0, G.a_brake), cw_action_pressed(0, G.a_paddle), cw_action_pressed(0, G.a_jump));
  if (cw_action_just_pressed(0, G.a_reset) && !G.reset_ticks) G.reset_ticks = TICK_HZ;
  if (G.reset_ticks) {
    G.reset_ticks--;
    p->enabled = false;
    tux_set_visible(&G.tux, (G.reset_ticks / 4) & 1);
    if (G.reset_ticks == TICK_HZ / 2) {
      t3_vec3 r = course_reset_point(&G.course, p->pos);
      r.y += TUX_RADIUS;
      physics_place(p, r, true);
      view_reset(&G.view, G.view.mode);
    }
    if (!G.reset_ticks) { p->enabled = true; tux_set_visible(&G.tux, true); }
  } else {
    G.race_time += dt;
  }
  if (cw_action_just_pressed(0, G.a_view)) view_reset(&G.view, G.view.mode == VIEW_BEHIND ? VIEW_FOLLOW : VIEW_BEHIND);
  physics_after_step(p, dt);
  /* landing squash (render only): a spring kicked by the vertical speed lost on touchdown */
  if (G.was_airborne && !p->airborne) {
    G.squash_v += fminf(0.35f, fabsf(p->vel.y) * 0.03f + 0.06f);
    cw_input_rumble(0, fminf(1, fabsf(p->vel.y) * 0.12f + 0.2f), 0.2f, 140); /* the touchdown in the pad (v0.16 rumble; a no-op without motors) */
  }
  G.was_airborne = p->airborne;
  G.squash_v += (-G.squash * 160 - G.squash_v * 14) * dt;
  G.squash += G.squash_v * dt;
  view_update(&G.view, p, &G.course, dt);
  /* the camera widens with speed: 60 degrees at rest, up to 74 */
  cw_node_set(G.cam, "fov", cw_val_float(60 + fminf(14, p->speed * 0.45f)));
  int got = course_collect(&G.course, p->pos);
  if (got) { G.herring += got; audio_sfx(SFX_PICKUP); fx_pickup((t3_vec3){ p->pos.x, p->pos.y + 0.5f, p->pos.z }); cw_input_rumble(0, 0, 0.45f, 70); }
  if (p->tree_hit_now) { audio_sfx(SFX_TREE); fx_tree_hit(p->pos, p->vel); cw_input_rumble(0, 0.9f, 0.5f, 260); }
  audio_slide(p->surf_w, p->speed, p->airborne, p->enabled);
  /* rock under the belly: a low buzz that grows with the share of rock (renewed every 6 ticks, each call replaces the last) */
  if (p->enabled && !p->airborne && p->surf_w[SURF_ROCK] > 0.3f && p->speed > 4 && (G.ticks % 6) == 0)
    cw_input_rumble(0, 0.12f * p->surf_w[SURF_ROCK], 0.28f * p->surf_w[SURF_ROCK], 120);
  if (course_finished(&G.course, p->pos)) { G.finished = true; race_over(false); return; }
  if (cw_action_just_pressed(0, G.a_quit)) { race_over(true); return; }
  if (cw_action_just_pressed(0, G.a_pause)) { G.state = ST_PAUSED; G.pause_cursor = 0; audio_slide(p->surf_w, 0, false, false); }
}

static void pose_tux(void) {
  const tux_phys *p = &G.phys;
  float paddling_factor = 0, flap_factor = 0;
  if (p->paddling) {
    float f = (p->time - p->paddle_time) / 0.4f;
    if (p->airborne) flap_factor = f; else paddling_factor = f;
  }
  if (p->jumping) flap_factor = (p->time - p->jump_start) / 0.2f;
  t3_quat conj = { -p->orientation.x, -p->orientation.y, -p->orientation.z, p->orientation.w };
  t3_vec3 local_force = t3_vec3_apply_quat(p->net_force, conj);
  tux_pose_slide(&G.tux, p->turn_anim, p->braking, paddling_factor, p->speed, local_force, flap_factor);
}

#define TUX_Y_CORRECTION_ON_STOMACH 0.33f /* phys_sim.c */
static void place_tux(t3_vec3 body_pos) {
  t3_vec3 into = t3_vec3_apply_quat((t3_vec3){ 0, 0, 1 }, G.phys.orientation);
  t3_vec3 at = t3_vec3_add(body_pos, t3_vec3_scale(into, TUX_RADIUS)); /* the surface point (the original's pos) */
  at.y += TUX_Y_CORRECTION_ON_STOMACH; /* phys_sim.c set_tux_pos: the model root rides 0.33 above it */
  /* the model leans a third further into turns than the physics orientation (render only: the
   * orientation itself steers the flap force, so it stays the original's) */
  float extra = (G.phys.braking ? 55.0f : 30.0f) * 0.35f * G.phys.turn_anim;
  t3_quat lean = t3_quat_multiply(G.phys.orientation, tr_quat_axis_angle((t3_vec3){ 0, 1, 0 }, TR_DEG(extra)));
  tux_set_transform(&G.tux, at, lean);
  tux_set_squash(&G.tux, G.squash);
  /* the contact shadow: on the surface under the belly, fading as he flies */
  bool show = G.course_loaded && G.state >= ST_INTRO && G.phys.dist_from_surface < 2.5f;
  cw_node_set(G.tux_shadow, "visible", cw_val_bool(show));
  if (show) {
    t3_vec3 n = G.phys.surf_nml;
    t3_vec3 ground = { body_pos.x, course_height(&G.course, body_pos.x, body_pos.z), body_pos.z };
    cw_node3d_set_position(G.tux_shadow, t3_vec3_add(ground, t3_vec3_scale(n, 0.4f)));
    cw_node3d_look_at(G.tux_shadow, t3_vec3_sub(ground, n), (t3_vec3){ 0, 0, -1 });
    float fade = 1 - fminf(1, fmaxf(0, G.phys.dist_from_surface) / 2.5f);
    cw_node_set(G.tux_shadow, "color", cw_val_color(0.45f, 0.5f, 0.62f, 0.7f * fade));
  }
}

/* the menus' camera: a slow orbit over the course below the start */
static void backdrop_tick(float dt) {
  if (!G.course_loaded) return;
  G.backdrop_t += dt;
  const course *c = &G.course;
  float cx = c->width * 0.5f, cz = -fminf(c->play_length * 0.25f, 90);
  float a = G.backdrop_t * 0.08f;
  t3_vec3 at = { cx, course_height(c, cx, cz) + 2, cz };
  t3_vec3 eye = { cx + cw_cos(a) * 28, at.y + 9, cz + cw_sin(a) * 28 };
  float ground = course_height(c, eye.x, eye.z);
  if (eye.y < ground + 3) eye.y = ground + 3;
  G.view.pos = eye;
  G.view.dir = t3_vec3_normalize(t3_vec3_sub(at, eye));
  G.view.up = (t3_vec3){ 0, 1, 0 };
}

/* ── menus ───────────────────────────────────────────────────────────── */

static bool menu_nav(int *cursor, int count) {
  if (cw_action_just_pressed(0, G.m_down)) *cursor = (*cursor + 1) % count;
  if (cw_action_just_pressed(0, G.m_up)) *cursor = (*cursor + count - 1) % count;
  return cw_action_just_pressed(0, G.m_accept);
}

static void title_tick(void) {
  audio_music(MUS_START);
  if (cw_quality_menu_is_open()) return; /* the graphics page has this frame's presses */
  if (menu_nav(&G.cursor, 5)) {
    switch (G.cursor) {
      case 0:
        G.mode = MODE_CUP, G.cup_idx = 0, G.state = ST_CUP;
        G.race_idx = SAVE.cup_race >= 0 && SAVE.cup_race < G.cups[0].count ? SAVE.cup_race : 0;
        G.lives = SAVE.cup_lives > 0 && SAVE.cup_lives <= INIT_NUM_LIVES ? SAVE.cup_lives : INIT_NUM_LIVES;
        break;
      case 1: G.mode = MODE_PRACTICE; G.state = ST_PRACTICE; G.cursor = 0; break;
      case 2: cw_quality_menu_open(); break;
      case 3: G.state = ST_CREDITS; audio_music(MUS_OPTIONS); break;
      case 4: cw_quit(); break;
    }
  }
}

static void practice_tick(void) {
  /* rows: course (left/right), conditions, wind, snow, mirror, Race, Back */
  int row = G.cursor;
  if (cw_action_just_pressed(0, G.m_down)) row = (row + 1) % 7;
  if (cw_action_just_pressed(0, G.m_up)) row = (row + 6) % 7;
  G.cursor = row;
  int d = (cw_action_just_pressed(0, G.m_right) ? 1 : 0) - (cw_action_just_pressed(0, G.m_left) ? 1 : 0);
  if (d && row == 0) G.practice_course = (G.practice_course + G.ncourses + d) % G.ncourses;
  if (d && row == 1) G.practice_cond = (G.practice_cond + COND_COUNT + d) % COND_COUNT;
  if (d && row == 2) G.practice_wind = !G.practice_wind;
  if (d && row == 3) G.practice_snow = !G.practice_snow;
  if (d && row == 4) G.practice_mirror = !G.practice_mirror;
  bool accept = cw_action_just_pressed(0, G.m_accept);
  if (accept && (row == 5 || row == 0)) begin_loading(G.courses[G.practice_course].dir, G.practice_cond, G.practice_wind, G.practice_mirror, G.practice_snow);
  if ((accept && row == 6) || cw_action_just_pressed(0, G.m_back)) { G.state = ST_TITLE; G.cursor = 1; }
}

static void cup_tick(void) {
  if (cw_action_just_pressed(0, G.m_back)) { G.state = ST_TITLE; G.cursor = 0; return; }
  if (cw_action_just_pressed(0, G.m_accept) && G.ncups) {
    const cup_race *r = &G.cups[G.cup_idx].races[G.race_idx];
    begin_loading(r->dir, r->conditions, r->windy, r->mirrored, r->snowing);
  }
}

static void paused_tick(void) {
  if (cw_quality_menu_is_open()) return;
  if (menu_nav(&G.pause_cursor, 3)) {
    if (G.pause_cursor == 0) G.state = ST_RACING;
    else if (G.pause_cursor == 1) cw_quality_menu_open();
    else race_over(true);
  }
  if (cw_action_just_pressed(0, G.m_back) || cw_action_just_pressed(0, G.a_pause)) G.state = ST_RACING;
}

static void race_over_tick(float dt) {
  G.over_time += dt;
  physics_after_step(&G.phys, dt);
  view_update(&G.view, &G.phys, &G.course, dt);
  if (G.over_time < 0.5f) return;
  if (cw_action_just_pressed(0, G.m_accept) || cw_action_just_pressed(0, G.m_back)) {
    if (G.mode == MODE_PRACTICE) { G.state = ST_PRACTICE; G.cursor = 4; }
    else {
      const cup_def *cup = &G.cups[G.cup_idx];
      if (G.won && G.race_idx + 1 < cup->count) G.race_idx++;
      else if (G.won) { G.race_idx = 0; G.lives = INIT_NUM_LIVES; }      /* the cup is won: play it again */
      else if (G.lives <= 0) { G.race_idx = 0; G.lives = INIT_NUM_LIVES; } /* out of lives: the cup restarts */
      G.state = ST_CUP;
    }
    G.backdrop = true;
    G.phys.enabled = false;
    tux_set_visible(&G.tux, false);
    fx_set_snowing(false);
    view_reset(&G.view, VIEW_ABOVE);
    audio_music(G.state == ST_PRACTICE || G.state == ST_CUP ? MUS_START : MUS_NONE);
  }
}

/* ── the app ─────────────────────────────────────────────────────────── */

/* a graphics setting changed (a preset, the menu, a restore): the game's own lever */
static int snow_mask_override = -1; /* dev: snowfx <mask> */
static void apply_snow_lever(void) {
  int v = G.lever_snow < 0 ? 2 : cw_gfx_get(G.lever_snow);
  int mask = v == 0 ? 0 : v == 1 ? 1 | 8 : 15;
  textures_snow_features(snow_mask_override >= 0 ? snow_mask_override : mask);
}
static void cmd_snowfx(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_str(r, "usage: snowfx <mask|auto> (1 glints, 2 detail, 4 trail, 8 clouds)"); return; }
  snow_mask_override = !strcmp(argv[1], "auto") ? -1 : atoi(argv[1]);
  apply_snow_lever();
  cw_reply_str(r, "ok");
}
static void quality_changed(cw_quality q, void *u) {
  apply_snow_lever();
  (void)q, (void)u;
  bool smooth = G.lever_edges >= 0 && cw_gfx_get(G.lever_edges) > 0;
  if (cw_node_valid(G.post)) cw_node_set(G.post, "msaa", cw_val_int(smooth ? 2 : 0));
  course_set_tree_coverage(smooth);
  /* the terrain's tier (its near shader and how far the tiles reach) is the
   * frame's biggest cost after the resolution: its own lever */
  course_set_terrain_quality(G.lever_terrain >= 0 ? cw_gfx_get(G.lever_terrain) : -1);
  /* Tux in the sun's cascades is ~25 meshes x 3 cascades of draws a frame: a
   * cart host pays dearly per draw, and the contact decal already grounds him */
  tux_set_cast_shadow(&G.tux, G.lever_tux_shadow < 0 || cw_gfx_get(G.lever_tux_shadow) > 0);
}

static void cmd_race(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_str(r, "usage: race <course dir> [sunny|cloudy|night] [wind] [mirror]"); return; }
  int cond = COND_SUNNY;
  bool wind = false, mirror = false, snow = false;
  for (int i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "cloudy")) cond = COND_CLOUDY;
    else if (!strcmp(argv[i], "night")) cond = COND_NIGHT;
    else if (!strcmp(argv[i], "wind")) wind = true;
    else if (!strcmp(argv[i], "mirror")) mirror = true;
    else if (!strcmp(argv[i], "snow")) snow = true;
  }
  G.mode = MODE_PRACTICE;
  begin_loading(argv[1], cond, wind, mirror, snow);
  cw_reply_str(r, "loading");
}

static void pre_render_hook(cw_tree *t, double alpha, void *u);
static bool snow_pass_active(const cw_pass_ctx *ctx, void *u);
static void snow_pass_exec(cw_pass_ctx *ctx, void *u);
static bool env_pass_active(const cw_pass_ctx *ctx, void *u);
static void env_pass_exec(cw_pass_ctx *ctx, void *u);

/* perf triage: hide <tux|trees|splats|items|all> [0|1] (1 = hide, the default) */
static void cmd_hide(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_str(r, "usage: hide <tux|trees|splats|items|all> [0|1]"); return; }
  bool hide = argc < 3 || atoi(argv[2]) != 0;
  if (!strcmp(argv[1], "batching")) { /* three.c's auto-instancing of meshes sharing geometry + material */
    t3_renderer *rr = cw_render3d_renderer(G.tree);
    if (rr) t3_renderer_set_auto_instancing(rr, !hide);
    cw_reply_str(r, rr ? (hide ? "batching off" : "batching on") : "no renderer");
    return;
  }
  if (!strcmp(argv[1], "tux") || !strcmp(argv[1], "all")) tux_set_visible(&G.tux, !hide);
  if (strcmp(argv[1], "tux")) course_objects_show(argv[1], !hide);
  cw_reply_str(r, hide ? "hidden" : "shown");
}

/* dev: tp <x> <z>: put Tux on the surface there (racing), to test items and spots */
static void cmd_tp(int argc, const char **argv, cw_reply *r) {
  if (argc < 3 || !G.course_loaded || G.state != ST_RACING || !G.phys.course) { cw_reply_str(r, "usage: tp <x> <z> (while racing)"); return; }
  float x = (float)atof(argv[1]), z = (float)atof(argv[2]);
  physics_place(&G.phys, (t3_vec3){ x, course_height(&G.course, x, z) + TUX_RADIUS, z }, true);
  cw_reply_str(r, "placed");
}

/* dev: cam <x> <y> <z> [tx ty tz]: park the camera there looking at the target (default Tux); cam off releases it */
static void cmd_cam(int argc, const char **argv, cw_reply *r) {
  if (argc >= 2 && !strcmp(argv[1], "off")) { view_reset(&G.view, VIEW_BEHIND); cw_reply_str(r, "released"); return; }
  if (argc < 4) { cw_reply_str(r, "usage: cam <x> <y> <z> [tx ty tz] | cam off"); return; }
  t3_vec3 eye = { (float)atof(argv[1]), (float)atof(argv[2]), (float)atof(argv[3]) };
  t3_vec3 at = argc >= 7 ? (t3_vec3){ (float)atof(argv[4]), (float)atof(argv[5]), (float)atof(argv[6]) } : G.phys.pos;
  G.view.mode = VIEW_FIXED;
  cw_node3d_set_position(G.cam, eye);
  cw_node3d_look_at(G.cam, at, (t3_vec3){ 0, 1, 0 });
  cw_reply_str(r, "parked");
}

static void init(void *user) {
  (void)user;
  cw_project_load_input();
  G.a_left = cw_action_find("left"), G.a_right = cw_action_find("right");
  G.a_paddle = cw_action_find("paddle"), G.a_brake = cw_action_find("brake"), G.a_jump = cw_action_find("jump");
  G.a_reset = cw_action_find("reset"), G.a_view = cw_action_find("view"), G.a_quit = cw_action_find("quit_race"), G.a_pause = cw_action_find("pause");
  G.m_up = cw_action_find("menu_up"), G.m_down = cw_action_find("menu_down"), G.m_left = cw_action_find("menu_left"), G.m_right = cw_action_find("menu_right");
  G.m_accept = cw_action_find("menu_accept"), G.m_back = cw_action_find("menu_back");
  G.tree = cw_scene_app_tree();
  load_data();
  build_world();
  hud_init();
  audio_init();
  G.state = ST_TITLE;
  G.lives = INIT_NUM_LIVES;
  save_read();
  /* graphics: the engine's presets plus one lever of ours; Medium unless the player chose */
  static const char *const EDGES[] = { "Sharp", "Smooth" };
  static const uint8_t EDGES_PRESET[CW_QUALITY_COUNT] = { 0, 0, 1, 1 };
  /* GL only: the engine's WebGPU path has no MSAA on an HDR scene, and alpha
   * to coverage is three.c's GL feature; the lever would be a no-op there */
  G.lever_edges = cw_gpu_backend() == CW_GPU_WEBGPU ? -1 : cw_quality_lever_add("Tree edges", EDGES, 2, EDGES_PRESET);
  static const char *const TERRAIN[] = { "Low", "Medium", "High", "Ultra" };
  /* High's tier (4 control texels blended, height blending) costs 5 ms of a
   * 14 ms frame at 3024x1702 and draws the original tiles no better (a pixel
   * diff of the same frame: 0.02 mean on bunny_hill; the rock on
   * path_of_daggers smears where Medium's triplanar keeps it crisp), so the
   * High preset takes Medium's and Ultra takes High's */
  static const uint8_t TERRAIN_PRESET[CW_QUALITY_COUNT] = { 0, 1, 1, 2 };
  G.lever_terrain = cw_quality_lever_add("Terrain", TERRAIN, 4, TERRAIN_PRESET);
  static const char *const TUXSH[] = { "Contact", "Cast" };
  static const uint8_t TUXSH_PRESET[CW_QUALITY_COUNT] = { 0, 0, 1, 1 };
  G.lever_tux_shadow = cw_quality_lever_add("Tux shadow", TUXSH, 2, TUXSH_PRESET);
  static const char *const SNOWD[] = { "Plain", "Glints", "Full" };
  static const uint8_t SNOWD_PRESET[CW_QUALITY_COUNT] = { 0, 1, 2, 2 }; /* Low: nothing; Medium: glints + cloud shadows; High: + ripples and the trail */
  G.lever_snow = cw_quality_lever_add("Snow detail", SNOWD, 3, SNOWD_PRESET);
  cw_quality_on_change(quality_changed, NULL);
  if (gfx_saved()) cw_quality_restore(gfx_saved());
  else cw_quality_set(CW_CART ? CW_QUALITY_MEDIUM : CW_QUALITY_HIGH, false); /* a cart host is the unknown machine */
  quality_changed(cw_quality_get(), NULL);
  cw_tree_add_hook(G.tree, 2, -10, pre_render_hook, NULL); /* before the nodes (order 0) push their transforms */
  cw_pipeline_add_pass(cw_pipeline_of(G.tree), &(cw_pass_desc){ .name = "tux_snow", .phase = CW_PHASE_SHADOW, .order = -100,
                                                                .active = snow_pass_active, .execute = snow_pass_exec, .webgpu = true });
  cw_pipeline_add_pass(cw_pipeline_of(G.tree), &(cw_pass_desc){ .name = "tux_env", .phase = CW_PHASE_SHADOW, .order = 50,
                                                                .active = env_pass_active, .execute = env_pass_exec, .webgpu = true });
  /* a title backdrop: Bunny Hill under the sun, the camera parked on the slope */
  apply_conditions(COND_SUNNY);
  cw_debug_command("race", "race <course dir> [sunny|cloudy|night] [wind] [mirror]: start a practice race", cmd_race);
  cw_debug_command("cam", "cam <x> <y> <z> [tx ty tz] | cam off: park the camera (dev)", cmd_cam);
  cw_debug_command("tp", "tp <x> <z>: put Tux on the surface there (dev)", cmd_tp);
  cw_debug_command("hide", "hide <tux|trees|splats|items|all> [0|1]: hide (1) or show (0) a group of objects, for perf triage", cmd_hide);
  cw_debug_command("snowfx", "snowfx <mask|auto>: the snow shader's features (1 glints, 2 detail, 4 trail, 8 clouds), for perf triage", cmd_snowfx);
  const char *start = getenv("TUXRACER_COURSE"); /* dev: straight into a practice race */
  if (start && *start) begin_loading(start, COND_SUNNY, false, false, false);
  else { /* the title's backdrop: Bunny Hill under the sun */
    G.info = find_course("bunny_hill");
    G.conditions = COND_SUNNY;
    G.backdrop = true;
    if (G.info) G.state = ST_LOADING;
  }
  cw_debug_i32("game.state", &G.dbg_state);
  cw_debug_f32_array("tux.pos", G.dbg_pos, 3);
  cw_debug_f32_array("tux.vel", G.dbg_vel, 3);
  cw_debug_f32("tux.speed", &G.dbg_speed);
  cw_debug_f32("surface.friction", &G.dbg_fric);
  cw_debug_f32("race.time", &G.race_time);
  cw_debug_i32("race.herring", &G.herring);
  cw_debug_i32("race.score", &G.score);
  cw_debug_i32("race.finished", &G.dbg_finished);
  cw_debug_i32("cup.lives", &G.lives);
  cw_debug_i32("cup.race", &G.race_idx);
  cw_debug_u32("phys.hash", &G.phys_hash);
  cw_debug_i32("phys.airborne", &G.dbg_airborne);
  cw_debug_i32("phys.tree", &G.dbg_tree);
  cw_debug_i32("phys.contacts", &G.dbg_contacts);
  cw_debug_i32("ticks", &G.ticks);
  cw_debug_f32("dbg.alpha", &G.dbg_alpha);             /* render-time: the interpolation alpha */
  cw_debug_f32_array("dbg.body_r", G.dbg_body_r, 3);   /* render-time: the interpolated body position Tux is drawn at */
  cw_debug_f32_array("dbg.cam_r", G.dbg_cam_r, 6);     /* render-time: the camera position and direction */
}

static void fixed_update(void *user, double ddt) {
  (void)user;
  float dt = (float)ddt;
  G.ticks++;
  G.view_prev = G.view; /* render interpolates the camera between this tick and the last, as it does Tux */
  if (G.backdrop && G.state != ST_LOADING) backdrop_tick(dt);
  switch (G.state) {
    case ST_TITLE: title_tick(); break;
    case ST_PRACTICE: practice_tick(); break;
    case ST_CUP: cup_tick(); break;
    case ST_CREDITS: if (cw_action_just_pressed(0, G.m_back) || cw_action_just_pressed(0, G.m_accept)) { G.state = ST_TITLE; G.cursor = 2; } break;
    case ST_LOADING: load_race(); break;
    case ST_INTRO:
      intro_tick(dt);
      if (cw_action_just_pressed(0, G.m_accept) || cw_action_just_pressed(0, G.a_paddle)) start_racing();
      break;
    case ST_RACING: racing_tick(dt); break;
    case ST_PAUSED: paused_tick(); break;
    case ST_RACE_OVER: race_over_tick(dt); break;
  }
  apply_dof();
  G.fx_time += dt;
  { cw_sky_state sk; cw_sky_get_state(G.tree, &sk); sky_ring_update(G.view.pos, sk.active ? sk.sun_direction : (t3_vec3){ 0, 1, 0 }, (float)G.fx_time, G.windy); }
  if (cw_node_valid(G.probe) && G.course_loaded) cw_node3d_set_position(G.probe, (t3_vec3){ G.phys.pos.x, G.phys.pos.y + 1.5f, G.phys.pos.z });
  if (G.course_loaded && G.state == ST_RACING && G.phys.enabled && !G.phys.airborne && G.phys.speed > 1)
    textures_trail_stamp(G.phys.pos, 0.5f, G.phys.surf_w[SURF_SNOW] > 0.5f ? 1.0f : 0.3f);
  if (G.course_loaded) {
    if (G.state != ST_INTRO) pose_tux();
    course_update_billboards(&G.course, G.view.pos);
    course_trees_update(&G.course, dt, G.windy, G.view.pos);
    fx_update(&G.phys, G.view.pos, G.view.dir, G.state == ST_RACING);
    /* the camera is placed per frame in pre_render_hook, interpolated like Tux */
  }
  const tux_phys *p = &G.phys;
  G.dbg_state = G.state;
  G.dbg_pos[0] = p->pos.x, G.dbg_pos[1] = p->pos.y, G.dbg_pos[2] = p->pos.z;
  G.dbg_vel[0] = p->vel.x, G.dbg_vel[1] = p->vel.y, G.dbg_vel[2] = p->vel.z;
  G.dbg_speed = p->speed;
  G.dbg_fric = p->fric_coeff;
  G.dbg_airborne = p->airborne, G.dbg_tree = p->in_tree, G.dbg_contacts = p->contact_count;
  G.phys_hash = cw_body3d_valid(p->body) ? cw_world3d_stats(p->world).hash : 0;
}

/* the condition's sky cube, prefiltered, as the scene's image-based light (needs the renderer, so from a frame) */
/* The condition's sky cube, PMREM-filtered into the scene environment. A
 * render pass (SHADOW phase, before the engine's sky_env at 100): on WebGPU
 * the PMREM records its own command buffers, which only exist inside a frame
 * (from update it dereferenced a null encoder: a segfault natively, the
 * "beginRenderPass of undefined" on romdev; GL happened to tolerate it). */
/* the snow shading part's hook must exist on the renderer before the lights pass (SHADOW 0) marks the
 * terrain's material, or the lighting would not compose its shadows with it (and remembers that) */
static bool snow_pass_active(const cw_pass_ctx *ctx, void *u) { (void)u; return ctx->renderer != NULL; }
static void snow_pass_exec(cw_pass_ctx *ctx, void *u) {
  (void)u;
  textures_snow_renderer(ctx->renderer);
  course_trees_renderer(ctx->renderer);
  sky_ring_renderer(ctx->renderer);
  /* the direction toward the light: the sky's sun by day; the moon looks along its -Z, so +Z points back at it */
  cw_sky_state sky;
  cw_sky_get_state(G.tree, &sky);
  t3_vec3 to_sun = sky.active ? sky.sun_direction : (t3_vec3){ 0, 1, 0 };
  bool sunny = G.conditions == COND_SUNNY, night = G.conditions == COND_NIGHT;
  if (night) { const t3_mat4 *mm = cw_node3d_global_transform(G.moon); if (mm) to_sun = t3_vec3_normalize((t3_vec3){ mm->e[8], mm->e[9], mm->e[10] }); }
  float glint = sunny ? 1.0f : night ? 0.35f : 0.0f, clouds = sunny ? 0.4f : 0.0f;
  float ice = night ? 0.5f : 1.0f; /* at night a mirror-smooth ice patch reflects a near-black sky: flat blue facets */
  const char *dbg = getenv("TUXRACER_SNOW"); /* dev: "glint clouds ice" overrides */
  if (dbg) sscanf(dbg, "%f %f %f", &glint, &clouds, &ice);
  textures_snow_set(to_sun, glint, clouds, G.windy ? 2.5f : 1.0f, ice, (float)G.fx_time);
  textures_trail_frame(1.0f);
}
static bool env_pass_active(const cw_pass_ctx *ctx, void *u) { (void)ctx, (void)u; return G.env_pending >= 0; }
static void env_pass_exec(cw_pass_ctx *ctx, void *u) {
  (void)u;
  t3_renderer *r = ctx->renderer;
  int c = G.env_pending;
  G.env_pending = -1;
  if (!r || !G.sky_cube[c]) return;
  bool wgpu = cw_gpu_backend() == CW_GPU_WEBGPU;
  if (wgpu) t3_renderer_set_external_encoder(r, 0); /* as the engine's sky pass: the PMREM submits its own buffers */
  if (!G.pmrem) G.pmrem = t3_pmrem_generator_new(r);
  if (!G.sky_env[c]) G.sky_env[c] = t3_pmrem_from_cubemap(G.pmrem, G.sky_cube[c]);
  if (!wgpu) t3_renderer_reset_bindings(r);
  if (G.sky_env[c]) t3_scene_set_environment(cw_render3d_scene(G.tree), G.sky_env[c]->texture);
}

static void update(void *user, double dt) {
  (void)user; (void)dt;
  /* the graphics page drives itself from the frame's raw input; save its changes when it closes */
  if (cw_quality_menu_is_open()) {
    cw_quality_menu_update();
    if (!cw_quality_menu_is_open() && cw_quality_menu_changed()) { gfx_store(); save_write(); }
  }
  audio_update();
}

/* the live course stays bright behind the menus: only a soft gradient at the top and bottom edges, the
 * items themselves sit on a card (draw_menu) */
static void menu_backdrop(void) {
  cw_vec2 size = cw_r2d_canvas_size();
  if (!G.course_loaded) { hud_box(0, 0, size.x, size.y, (cw_color){ 0.02f, 0.08f, 0.2f, 1 }); return; }
  const int bands = 24;
  float h = size.y * 0.22f / bands;
  for (int i = 0; i < bands; i++) { /* bands that touch without overlapping: a stacked edge draws as a line */
    float t = (float)i / bands, a = 0.38f * (1 - t) * (1 - t);
    float y0 = floorf(i * h), y1 = floorf((i + 1) * h);
    hud_box(0, y0, size.x, y1 - y0, (cw_color){ 0.02f, 0.05f, 0.12f, a });
    hud_box(0, size.y - y1, size.x, y1 - y0, (cw_color){ 0.02f, 0.05f, 0.12f, a });
  }
}

static void render_overlay(void);

static void draw_menu(const char *title, const char *const *items, int count, int cursor, float y0) {
  cw_vec2 size = cw_r2d_canvas_size();
  if (!strcmp(title, "Tux Racer")) hud_image("original/textures/menu_title.png", size.x / 2 - 192, 36, 384, 192); /* the original logo */
  else hud_title(title, size.x / 2, 60, 64, (cw_color){ 1, 0.9f, 0.2f, 1 }, 1);
  if (G.course_loaded) hud_panel(size.x / 2 - 170, y0 - 18, 340, 48 * (float)count + 26, 0.42f);
  for (int i = 0; i < count; i++) {
    if (i == cursor) hud_panel(size.x / 2 - 150, y0 + 48 * (float)i - 4, 300, 44, 0.35f); /* the cursor's row a shade deeper */
    hud_title(items[i], size.x / 2, y0 + 48 * (float)i, 30, i == cursor ? (cw_color){ 1, 0.89f, 0.01f, 1 } : (cw_color){ 1, 1, 1, 1 }, 1);
  }
}

/* Per frame, before the nodes sync to three.c and the scene draws (a
 * pre_render hook at order -10): Tux and the camera at the same sub-tick
 * phase. The app's render callback runs AFTER the 3D scene (the "app"
 * overlay pass), so anything placed there is drawn a frame late; and a
 * camera written in the fixed tick sits at tick positions while Tux is
 * interpolated, so at any frame rate but exactly 60 the two drift against
 * each other every frame: measured as a 6-pixel vibration at 120 Hz. */
static void pre_render_hook(cw_tree *t, double alpha, void *u) {
  (void)t, (void)u;
  if (!G.course_loaded) return;
  if (G.state != ST_INTRO && cw_body3d_valid(G.phys.body)) {
    cw_xform3 bx = cw_body3d_interpolated(G.phys.body, (float)alpha);
    place_tux(bx.p);
    G.dbg_alpha = (float)alpha;
    G.dbg_body_r[0] = bx.p.x, G.dbg_body_r[1] = bx.p.y, G.dbg_body_r[2] = bx.p.z;
  }
  if (G.view.mode == VIEW_FIXED || !G.view.initialized) return; /* parked by the cam dev command */
  float a = G.view_prev.initialized && G.view.mode == G.view_prev.mode ? (float)alpha : 1.0f;
  t3_vec3 pos = t3_vec3_lerp(G.view_prev.pos, G.view.pos, a);
  t3_vec3 dir = t3_vec3_normalize(t3_vec3_lerp(G.view_prev.dir, G.view.dir, a));
  t3_vec3 up = t3_vec3_normalize(t3_vec3_lerp(G.view_prev.up, G.view.up, a));
  cw_node3d_set_position(G.cam, pos);
  cw_node3d_look_at(G.cam, t3_vec3_add(pos, dir), up);
  G.dbg_cam_r[0] = pos.x, G.dbg_cam_r[1] = pos.y, G.dbg_cam_r[2] = pos.z;
  G.dbg_cam_r[3] = dir.x, G.dbg_cam_r[4] = dir.y, G.dbg_cam_r[5] = dir.z;
}

static void render(void *user, double alpha) {
  (void)user;
  cw_vec2 size;
  char buf[160];
  switch (G.state) {
    case ST_TITLE: {
      static const char *const ITEMS[] = { "Cup", "Practice", "Settings", "Credits", "Quit" };
      hud_begin();
      size = cw_r2d_canvas_size();
      menu_backdrop();
      draw_menu("Tux Racer", ITEMS, 5, G.cursor, 236);
      hud_text("Tux Racer 0.61 by Sunspire Studios, on the cartwheel engine", size.x / 2, size.y - 30, 14, (cw_color){ 1, 1, 1, 0.55f }, 1);
      hud_end();
      break;
    }
    case ST_PRACTICE: {
      hud_begin();
      size = cw_r2d_canvas_size();
      menu_backdrop();
      const course_info *c = &G.courses[G.practice_course];
      char preview[128];
      snprintf(preview, sizeof preview, "courses/%s/preview.png", c->dir);
      hud_title("Practice", size.x / 2, 40, 48, (cw_color){ 1, 0.9f, 0.2f, 1 }, 1);
      hud_panel(size.x / 2 - 260, 104, 520, 404, 0.42f);
      hud_panel(size.x - 200, 100, 180, 180, 0.42f);
      hud_image(preview, size.x - 190, 110, 160, 160);
      const char *rows[7];
      snprintf(buf, sizeof buf, "<  %s  >", c->title);
      char r1[64], r2[64], r3[64], r4[64];
      snprintf(r1, sizeof r1, "Conditions:  < %s >", COND_NAME[G.practice_cond]);
      snprintf(r2, sizeof r2, "Wind:  < %s >", G.practice_wind ? "On" : "Off");
      snprintf(r3, sizeof r3, "Snow:  < %s >", G.practice_snow ? "On" : "Off");
      snprintf(r4, sizeof r4, "Mirror:  < %s >", G.practice_mirror ? "On" : "Off");
      rows[0] = buf, rows[1] = r1, rows[2] = r2, rows[3] = r3, rows[4] = r4, rows[5] = "Race!", rows[6] = "Back";
      for (int i = 0; i < 7; i++)
        hud_title(rows[i], size.x / 2, 120 + 42 * (float)i, i == 0 ? 30 : 24, i == G.cursor ? (cw_color){ 1, 0.89f, 0.01f, 1 } : (cw_color){ 1, 1, 1, 1 }, 1);
      hud_text(c->desc, size.x / 2, 430, 17, (cw_color){ 1, 1, 1, 0.85f }, 1);
      snprintf(buf, sizeof buf, "Par time %.0f s   by %s", c->par, c->contrib ? "contributor" : "Sunspire");
      hud_text(buf, size.x / 2, 458, 16, (cw_color){ 1, 1, 1, 0.6f }, 1);
      const best_entry *b = NULL;
      for (int i = 0; i < MAX_COURSES; i++) if (SAVE.best[i].course_hash == fnv(c->dir, strlen(c->dir)) && (SAVE.best[i].score || SAVE.best[i].time)) b = &SAVE.best[i];
      if (b) {
        snprintf(buf, sizeof buf, "Best: %02d:%02d.%02d   %d herring   score %06d", (int)(b->time / 60), (int)b->time % 60, (int)(b->time * 100) % 100, b->herring, b->score);
        hud_text(buf, size.x / 2, 484, 16, (cw_color){ 1, 0.95f, 0.01f, 0.9f }, 1);
      }
      hud_end();
      break;
    }
    case ST_CUP: {
      hud_begin();
      size = cw_r2d_canvas_size();
      menu_backdrop();
      if (G.ncups) {
        const cup_def *cup = &G.cups[G.cup_idx];
        hud_title(cup->name, size.x / 2, 40, 48, (cw_color){ 1, 0.9f, 0.2f, 1 }, 1);
        hud_panel(size.x / 2 - 300, 112, 600, 282, 0.42f);
        for (int i = 0; i < cup->count; i++) {
          const course_info *ci = find_course(cup->races[i].dir);
          snprintf(buf, sizeof buf, "%d. %s", i + 1, ci ? ci->title : cup->races[i].dir);
          hud_title(buf, size.x / 2, 130 + 46 * (float)i, 26, i == G.race_idx ? (cw_color){ 1, 0.89f, 0.01f, 1 } : i < G.race_idx ? (cw_color){ 0.6f, 1, 0.6f, 1 } : (cw_color){ 1, 1, 1, 0.5f }, 1);
        }
        const cup_race *r = &cup->races[G.race_idx];
        int m = (int)(r->time[DIFFICULTY] / 60), s = (int)r->time[DIFFICULTY] % 60, hs = (int)(r->time[DIFFICULTY] * 100) % 100;
        snprintf(buf, sizeof buf, "Requirements:  Time %02d:%02d.%02d   Herring %03d   Score %06d", m, s, hs, r->herring[DIFFICULTY], r->score[DIFFICULTY]);
        hud_text(buf, size.x / 2, 330, 18, (cw_color){ 1, 0.95f, 0.01f, 1 }, 1);
        snprintf(buf, sizeof buf, "Lives: %d", G.lives);
        hud_text(buf, size.x / 2, 360, 20, (cw_color){ 1, 1, 1, 1 }, 1);
        hud_text("Press Enter to race, Escape to go back", size.x / 2, 470, 16, (cw_color){ 1, 1, 1, 0.6f }, 1);
      }
      hud_end();
      break;
    }
    case ST_CREDITS: {
      hud_begin();
      size = cw_r2d_canvas_size();
      menu_backdrop();
      hud_panel(size.x / 2 - 330, 16, 660, size.y - 32, 0.45f);
      float y = 30;
      for (int i = 0; i < G.ncredits; i++) {
        const char *bar = strchr(G.credits[i], '|');
        if (!bar) continue;
        bool h1 = !strncmp(G.credits[i], "h1", 2), h2 = !strncmp(G.credits[i], "h2", 2), small = !strncmp(G.credits[i], "text_small", 10);
        float sz = h1 ? 30 : h2 ? 22 : small ? 13 : 16;
        if (h1 || h2) hud_title(bar + 1, size.x / 2, y, sz, (cw_color){ 1, 0.9f, 0.2f, 1 }, 1);
        else hud_text(bar + 1, size.x / 2, y, sz, (cw_color){ 1, 1, 1, 1 }, 1);
        y += sz * 1.15f;
      }
      hud_end();
      break;
    }
    case ST_LOADING: {
      hud_begin();
      size = cw_r2d_canvas_size();
      hud_box(0, 0, size.x, size.y, (cw_color){ 0.02f, 0.08f, 0.2f, 1 });
      snprintf(buf, sizeof buf, "Loading %s...", G.info ? G.info->title : "");
      hud_title(buf, size.x / 2, size.y / 2 - 20, 32, (cw_color){ 1, 0.9f, 0.2f, 1 }, 1);
      hud_end();
      break;
    }
    case ST_INTRO: { /* the walk-in is a shot: letterboxed, no HUD; the bars slide away over the last half second */
      hud_begin();
      size = cw_r2d_canvas_size();
      float total = G.nkeys ? G.keys[G.nkeys - 1].time : 1, left = total - G.key_time;
      float bar = size.y * 0.09f * (left < 0.5f ? left / 0.5f : 1);
      if (bar > 0) { hud_box(0, 0, size.x, bar, (cw_color){ 0, 0, 0, 1 }); hud_box(0, size.y - bar, size.x, bar, (cw_color){ 0, 0, 0, 1 }); }
      hud_end();
      break;
    }
    case ST_RACING:
      hud_draw(&G.phys, G.race_time, G.herring, G.mode == MODE_CUP ? G.lives : 0);
      break;
    case ST_PAUSED: {
      static const char *const ITEMS[] = { "Resume", "Settings", "Quit race" };
      hud_draw(&G.phys, G.race_time, G.herring, G.mode == MODE_CUP ? G.lives : 0);
      hud_begin();
      size = cw_r2d_canvas_size();
      hud_box(0, 0, size.x, size.y, (cw_color){ 0, 0, 0, 0.45f });
      draw_menu("Paused", ITEMS, 3, G.pause_cursor, 220);
      hud_end();
      break;
    }
    case ST_RACE_OVER: {
      hud_draw(&G.phys, G.race_time, G.herring, G.mode == MODE_CUP ? G.lives : 0);
      hud_begin();
      size = cw_r2d_canvas_size();
      hud_panel(size.x / 2 - 300, 110, 600, 300, 0.55f);
      hud_title("Race Over", size.x / 2, 130, 44, (cw_color){ 1, 0.9f, 0.2f, 1 }, 1);
      int m = (int)(G.race_time / 60), s = (int)G.race_time % 60, hs = (int)(G.race_time * 100) % 100;
      snprintf(buf, sizeof buf, "Time: %02d:%02d.%02d", m, s, hs);
      hud_title(buf, size.x / 2, 200, 26, (cw_color){ 1, 1, 1, 1 }, 1);
      snprintf(buf, sizeof buf, "Herring: %03d", G.herring);
      hud_title(buf, size.x / 2, 240, 26, (cw_color){ 1, 1, 1, 1 }, 1);
      snprintf(buf, sizeof buf, "Score: %06d", G.score);
      hud_title(buf, size.x / 2, 280, 26, (cw_color){ 1, 1, 1, 1 }, 1);
      const char *msg = G.aborted ? "Race aborted." : G.mode == MODE_PRACTICE ? (G.new_best ? "You beat your best score!" : "") :
        G.won && G.race_idx + 1 >= G.cups[G.cup_idx].count ? "Congratulations! You won the cup!" : G.won ? "You advanced to the next race!" :
        G.lives <= 0 ? "You didn't advance, and you're out of lives." : "You didn't advance.";
      hud_text(msg, size.x / 2, 340, 20, (cw_color){ 1, 0.95f, 0.01f, 1 }, 1);
      if (G.over_time >= 0.5f) hud_text("Press Enter to continue", size.x / 2, 375, 16, (cw_color){ 1, 1, 1, 0.6f }, 1);
      hud_end();
      break;
    }
  }
  render_overlay();
}

static void render_overlay(void) {
  if (!cw_quality_menu_is_open()) return;
  hud_begin();
  cw_quality_menu_draw(&(cw_quality_menu_style){ .font = hud_font(), .accent = { 1, 0.89f, 0.01f, 1 }, .title = "Graphics" });
  hud_end();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cw_app_desc base = {
    .title = "Tux Racer",
    .save = &SAVE,
    .save_size = sizeof SAVE,
    .width = 960,
    .height = 540,
    .fixed_hz = TICK_HZ,
    .init = init,
    .fixed_update = fixed_update,
    .update = update,
    .render = render,
    .debug = true,
    .gpu = CW_GPU_WEBGPU, /* 15-20% faster than GL on the Mac and a third of the cart's draw CPU; CW_GPU=gl for GL (a dual cart follows its host) */
    .user = &G,
  };
  cw_project_apply_app(&base);
  return cw_scene_app(NULL, &base);
}
