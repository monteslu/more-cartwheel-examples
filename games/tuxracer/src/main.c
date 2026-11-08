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

typedef enum { ST_TITLE, ST_PRACTICE, ST_CUP, ST_CREDITS, ST_LOADING, ST_INTRO, ST_RACING, ST_PAUSED, ST_RACE_OVER, ST_SETTINGS } game_state;
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
  uint8_t cup_won : 1;       /* was a whole byte holding 0 or 1: Rumble OFF takes the next bit, so an older save keeps */
  uint8_t rumble_off : 1;    /* its size and reads Rumble on */
  uint8_t gfx_hi : 6;        /* the graphics word's bits 24-29 (zero in an older save, whose word fit in gfx) */
  uint8_t gfx[3];            /* the graphics settings (cw_quality_saved, bits 0-23 little-endian); 0 = never chosen */
  /* version 2 from here on (a version 1 save, which ends above, still loads: these read as never chosen) */
  uint8_t text;              /* Text size and Text background (cw_text_scale_saved); 0 = never chosen */
  uint8_t opts;              /* OPT_*: zero in an older save (the ghost on, the speed camera) */
  uint8_t reserved[2];
  /* version 3 from here on (a version 2 save, which ends above, still loads: no ghosts yet) */
  ghost_run ghosts[MAX_COURSES]; /* the fastest finished run per course */
} save_data;
enum { OPT_GHOST_OFF = 1, OPT_CLASSIC_CAMERA = 2 };
static save_data SAVE;
#define SAVE_V1_SIZE (12 + 16 * MAX_COURSES + 12)
#define SAVE_V2_SIZE (SAVE_V1_SIZE + 4)
_Static_assert(offsetof(save_data, ghosts) == SAVE_V2_SIZE, "a version 2 save is the start of version 3");
static void rumble(float low, float high, int ms);
static int32_t cam_offset[3]; /* the shake check (pre_render_hook) */
static int impacts, impact_frames; /* tree hits and touchdowns, for the shake check */
static int32_t tree_hits[2]; /* debug race.tree_hits: tree hits, and the tick of the last */
static void rumble_stop(void);
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
  int settings_cursor;
  game_state settings_from;  /* the menu the Settings page returns to: the title or the pause menu */
  float credits_scroll;      /* render only: how far the credits roll has scrolled (large text) */
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
  int lever_tux_shadow, lever_snow;      /* the "Tux shadow" lever: contact decal only, or the sun's cascades too */         /* the "Terrain" lever: the tier (Very Low..High), the preset's by default */
  /* the ghost (ghost.c): this run's recording, the course's best run replayed beside him (Practice) */
  tux_model ghost_tux;
  bool ghost_loaded;
  ghost_rec rec;
  ghost_play gplay;
  const ghost_run *ghost;    /* the save's run for this race, NULL for none */
  int32_t race_ticks;        /* racing ticks since the start (the recording's clock: resets count, pauses do not) */
  int32_t medal, best_time_new; /* this finish: 0 none, 1 bronze, 2 silver, 3 gold; a new fastest time */
  int32_t finish_events;     /* finishes this run (the finish moment fires once each) */
  int32_t dbg_ghost[4];      /* ghost: shown, samples, check error (0.1 mm), ticks checked */
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
  SAVE.magic = 0x56535254u, SAVE.version = 3, SAVE.size = (uint16_t)(sizeof SAVE & 0xffff); /* the size field is 16 bits: the low bits, the version says the rest */
  SAVE.checksum = save_checksum();
  cw_save_commit();
}
static uint32_t gfx_saved(void) { return SAVE.gfx[0] | (uint32_t)SAVE.gfx[1] << 8 | (uint32_t)SAVE.gfx[2] << 16 | (uint32_t)SAVE.gfx_hi << 24; }
static void gfx_store(void) {
  uint32_t q = cw_quality_saved();
  SAVE.gfx[0] = q & 255, SAVE.gfx[1] = (q >> 8) & 255, SAVE.gfx[2] = (q >> 16) & 255, SAVE.gfx_hi = (q >> 24) & 63; /* 30 bits */
}
static void save_read(void) {
  bool v1 = SAVE.magic == 0x56535254u && SAVE.version == 1 && SAVE.size == SAVE_V1_SIZE &&
            SAVE.checksum == fnv((const uint8_t *)&SAVE + offsetof(save_data, best), SAVE_V1_SIZE - offsetof(save_data, best));
  if (v1) { memset((uint8_t *)&SAVE + SAVE_V1_SIZE, 0, sizeof SAVE - SAVE_V1_SIZE); return; } /* rewritten as version 3 at the next save */
  bool v2 = SAVE.magic == 0x56535254u && SAVE.version == 2 && SAVE.size == SAVE_V2_SIZE &&
            SAVE.checksum == fnv((const uint8_t *)&SAVE + offsetof(save_data, best), SAVE_V2_SIZE - offsetof(save_data, best));
  if (v2) { memset((uint8_t *)&SAVE + SAVE_V2_SIZE, 0, sizeof SAVE - SAVE_V2_SIZE); return; }
  if (SAVE.magic != 0x56535254u || SAVE.version != 3 || SAVE.size != (uint16_t)(sizeof SAVE & 0xffff) || SAVE.checksum != save_checksum()) memset(&SAVE, 0, sizeof SAVE);
  for (int i = 0; i < MAX_COURSES; i++) if (SAVE.ghosts[i].course_hash && !ghost_valid(&SAVE.ghosts[i])) memset(&SAVE.ghosts[i], 0, offsetof(ghost_run, s));
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
static ghost_run *ghost_of(const char *dir, bool make) {
  uint32_t h = fnv(dir, strlen(dir));
  ghost_run *free_slot = NULL;
  for (int i = 0; i < MAX_COURSES; i++) {
    if (SAVE.ghosts[i].course_hash == h) return &SAVE.ghosts[i];
    if (!free_slot && !SAVE.ghosts[i].course_hash) free_slot = &SAVE.ghosts[i];
  }
  return make ? free_slot : NULL;
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

static void ghost_look(tux_model *m);
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
  G.ghost_loaded = tux_load(&G.ghost_tux, cw_render3d_scene(G.tree));
  if (G.ghost_loaded) { ghost_look(&G.ghost_tux); tux_set_visible(&G.ghost_tux, false); }
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

static float SUNNY_ENV = 0.25f, SUNNY_HEMI = 0.5f; /* TUXRACER_SUNNY="env hemi" overrides (dev) */
/* the original's lighting scripts (sunny_light / foggy_light / night_light.tcl) as engine settings */
static void apply_conditions(int cond) {
  bool night = cond == COND_NIGHT, cloudy = cond == COND_CLOUDY;
  /* sunny: the engine's sky with the cube's mountains on a ring (skyring.c); night and cloudy: the original cube */
  bool sunny = !night && !cloudy;
  if (getenv("TUXRACER_SUNNY")) sscanf(getenv("TUXRACER_SUNNY"), "%f %f", &SUNNY_ENV, &SUNNY_HEMI);
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
  /* sunny snow with contrast: the sky cube (white snow all round) fills the shade less, a blue sky above lights it
   * instead, so shaded slopes and hollows go blue while the sunlit faces stay warm white */
  cw_render3d_scene(G.tree)->environment_intensity = sunny ? SUNNY_ENV : 1;
  cw_node_set(G.env, "ambient_energy", cw_val_float(night ? 0.07f : cloudy ? 0.12f : 0.05f)); /* night: the sky glow fills the slopes the moon misses */
  cw_node_set(G.env, "ambient_color", night ? cw_val_color(0.0f, 0.3f, 1.0f, 1) : cw_val_color(1, 1, 1, 1));
  cw_node_set(G.env, "hemi_energy", cw_val_float(night ? 0.14f : cloudy ? 0.35f : SUNNY_HEMI));
  cw_node_set(G.env, "sky_color", night ? cw_val_color(0.1f, 0.2f, 0.5f, 1) : cloudy ? cw_val_color(0.6f, 0.62f, 0.68f, 1) : cw_val_color(0.42f, 0.6f, 1.0f, 1));
  cw_node_set(G.env, "ground_color", night ? cw_val_color(0.05f, 0.08f, 0.15f, 1) : cloudy ? cw_val_color(0.8f, 0.85f, 0.9f, 1) : cw_val_color(0.45f, 0.5f, 0.62f, 1));
  cw_node_set(G.fog, "density", cw_val_float(cloudy ? 0.006f : night ? 0.0025f : 0.0007f)); /* cloudy: mist that still shows the overcast deck and the near range */
  cw_node_set(G.fog, "height_falloff", cw_val_float(cloudy ? 0 : 0.004f));
  cw_node_set(G.fog, "base_height", cw_val_float(-400));
  cw_node_set(G.fog, "use_sky_color", cw_val_bool(false));
  cw_node_set(G.fog, "color", night ? cw_val_color(0.02f, 0.04f, 0.1f, 1) : cloudy ? cw_val_color(0.58f, 0.59f, 0.65f, 1) : cw_val_color(0.62f, 0.74f, 0.95f, 1)); /* sunny: a blue aerial haze, the far ridges apart from it */
  cw_node_set(G.fog, "sun_scatter", cw_val_float(cloudy ? 0.1f : 0.3f));
  cw_node_set(G.fog, "max_opacity", cw_val_float(cloudy ? 0.9f : night ? 0.38f : 0.5f)); /* night: the far snow stays snow, not a blue plane */
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

/* ── medals and the ghost ────────────────────────────────────────────── */

/* medal times (bronze, silver, gold): a Canadian Cup course takes the Cup's own times for the normal, hard and
 * expert difficulties (cups.txt); the others the same shares of their par (Bunny Hill: 35, 32 and 30 s of par 40) */
static void medal_times(const course_info *ci, float out[3]) {
  for (int k = 0; k < G.ncups; k++)
    for (int i = 0; i < G.cups[k].count; i++)
      if (!strcmp(G.cups[k].races[i].dir, ci->dir)) { for (int m = 0; m < 3; m++) out[m] = G.cups[k].races[i].time[m + 1]; return; }
  out[0] = ci->par * 0.875f, out[1] = ci->par * 0.8f, out[2] = ci->par * 0.75f;
}
static int medal_for(const course_info *ci, float t) {
  float m[3];
  medal_times(ci, m);
  return t <= m[2] ? 3 : t <= m[1] ? 2 : t <= m[0] ? 1 : 0;
}
static const char *const MEDAL_NAME[4] = { "", "Bronze", "Silver", "Gold" };
static const cw_color MEDAL_COLOR[4] = { { 1, 1, 1, 1 }, { 0.86f, 0.55f, 0.3f, 1 }, { 0.82f, 0.86f, 0.92f, 1 }, { 1, 0.82f, 0.2f, 1 } };
/* a run is the ghost of its course as raced: a mirrored course has a ghost of its own */
static const char *ghost_key(const char *dir, bool mirrored) {
  static char k[80];
  snprintf(k, sizeof k, "%s%s", dir, mirrored ? "|mirror" : "");
  return k;
}
static bool ghost_wanted(void) { return G.mode == MODE_PRACTICE && !(SAVE.opts & OPT_GHOST_OFF); }

/* the ghost's look: Tux in pale blue glass, no shadow (a thing that is not there casts none) */
static void ghost_look(tux_model *m) {
  t3_material *mats[TUX_MAX_MATERIALS + 1];
  int n = 0;
  for (int i = 0; i < m->mat_count; i++) mats[n++] = m->mats[i];
  if (m->skin_mat) mats[n++] = m->skin_mat;
  for (int i = 0; i < n; i++) {
    t3_material *mt = mats[i];
    mt->transparent = true;
    mt->opacity = 0.5f;
    mt->depth_write = true; /* his own far side stays hidden: one translucent shape, not a stack of spheres */
    mt->color = (t3_color){ 0.35f + mt->color.r * 0.3f, 0.6f + mt->color.g * 0.25f, 0.95f };
    mt->emissive = (t3_color){ 0.1f, 0.3f, 0.75f };
    mt->emissive_intensity = 0.8f; /* a blue glow: he reads 40 m down the slope */
  }
  tux_set_cast_shadow(m, false);
}

/* the full-rate path of the run being recorded: what `ghost check` holds the samples against */
#define CHECK_TICKS (180 * TICK_HZ)
static float rec_full[CHECK_TICKS + 1][3];

static void ghost_alpha(float a) {
  for (int i = 0; i < G.ghost_tux.mat_count; i++) G.ghost_tux.mats[i]->opacity = a;
  if (G.ghost_tux.skin_mat) G.ghost_tux.skin_mat->opacity = a;
}
/* where the ghost is drawn this frame (render: between the last two ticks, as Tux is) */
static void ghost_draw(double alpha) {
  bool show = false;
  ghost_pose gp;
  if (G.ghost_loaded && G.ghost && (G.state == ST_RACING || G.state == ST_PAUSED) && G.course_loaded) {
    float t = (float)G.race_ticks - 1 + (G.state == ST_RACING ? (float)alpha : 1);
    show = ghost_at(&G.gplay, G.ghost, t < 0 ? 0 : t, &gp) && !gp.hidden;
  }
  tux_set_visible(&G.ghost_tux, show);
  G.dbg_ghost[0] = show;
  if (!show) return;
  /* the physics' target orientation (physics.c adjust_orientation): along the travel, belly into the slope */
  float speed = t3_vec3_length(gp.vel);
  t3_vec3 vdir = speed > 1e-3f ? t3_vec3_scale(gp.vel, 1 / speed) : (t3_vec3){ 0, 0, -1 };
  t3_vec3 nml = course_normal(&G.course, gp.pos.x, gp.pos.z), ny, nz;
  float roll = 30.0f * gp.turn;
  if (gp.airborne) {
    ny = vdir;
    nz = t3_vec3_normalize(tr_project_into_plane(ny, (t3_vec3){ 0, -1, 0 }));
    if (t3_vec3_length_sq(nz) < 1e-6f) nz = (t3_vec3){ 0, 0, 1 };
  } else {
    nz = t3_vec3_scale(nml, -1);
    ny = t3_vec3_normalize(tr_project_into_plane(nml, vdir));
    if (t3_vec3_length_sq(ny) < 1e-6f) ny = (t3_vec3){ 0, 0, -1 };
  }
  t3_vec3 vp = tr_project_into_plane(nz, ny);
  if (t3_vec3_length_sq(vp) > 1e-6f) nz = tr_rotate_about(nz, t3_vec3_normalize(vp), roll);
  t3_quat q = tr_quat_from_basis(t3_vec3_cross(ny, nz), ny, nz);
  t3_vec3 at = t3_vec3_add(gp.pos, t3_vec3_scale(t3_vec3_apply_quat((t3_vec3){ 0, 0, 1 }, q), TUX_RADIUS));
  at.y += 0.33f; /* TUX_Y_CORRECTION_ON_STOMACH, as place_tux */
  tux_set_transform(&G.ghost_tux, at, q);
  /* he fades as he nears Tux: two penguins in one place read as one blur */
  float near = t3_vec3_length(t3_vec3_sub(gp.pos, G.phys.pos)), k = fmaxf(0, fminf(1, (near - 0.8f) / 2.2f));
  ghost_alpha(0.5f * k * k * (3 - 2 * k));
  tux_pose_slide(&G.ghost_tux, gp.turn, false, 0, speed, (t3_vec3){ 0, 0, 0 }, 0);
}

/* ghost check [offset]: the recording so far decoded at every tick against the path it was recorded from; the
 * largest distance (0.1 mm) goes in ghost[2]. An offset compares each tick with the path that many ticks later
 * (the control: a check that cannot tell one tick from the next could not prove anything). */
static void cmd_ghost(int argc, const char **argv, cw_reply *r) {
  if (argc >= 2 && !strcmp(argv[1], "keep")) { /* this run so far as the course's ghost, as if it had finished */
    ghost_rec_flush(&G.rec);
    if (!G.info || G.rec.run.count < 2) { cw_reply_str(r, "no run"); return; }
    ghost_run *g = ghost_of(ghost_key(G.info->dir, G.mirrored), true);
    if (!g) { cw_reply_str(r, "no slot"); return; }
    *g = G.rec.run;
    g->time = G.race_time > 0 ? G.race_time : 1;
    cw_reply_str(r, "kept");
    return;
  }
  if (argc >= 2 && !strcmp(argv[1], "check")) {
    int off = argc >= 3 ? atoi(argv[2]) : 0;
    ghost_rec_flush(&G.rec);
    static ghost_run run; /* 38 kB: not on the stack */
    run = G.rec.run;
    run.time = 1;
    ghost_play gp = { .cache_i = -1 };
    float worst = 0;
    int n = 0;
    for (int t = 0; t + off <= G.rec.ticks && t + off <= CHECK_TICKS; t++) {
      ghost_pose pose;
      if (!ghost_at(&gp, &run, (float)t, &pose)) break;
      float dx = pose.pos.x - rec_full[t + off][0], dy = pose.pos.y - rec_full[t + off][1], dz = pose.pos.z - rec_full[t + off][2];
      worst = fmaxf(worst, sqrtf(dx * dx + dy * dy + dz * dz));
      n++;
    }
    G.dbg_ghost[2] = (int32_t)(worst * 1e4f), G.dbg_ghost[3] = n;
    char buf[96];
    snprintf(buf, sizeof buf, "%d ticks, worst %.2f mm", n, worst * 1000);
    cw_reply_str(r, buf);
    return;
  }
  cw_reply_str(r, "usage: ghost keep | ghost check [offset]");
}

/* The camera's lens. Classic: the original's 60 degrees at the original's distance. Speed (the default): the
 * field of view opens from 60 to 75 degrees and the camera closes from 4 m to 3.3 m behind him as he goes from
 * 15 to 90 km/h, eased (a half-second time constant) so a bump or a tree hit never jerks it; after the finish it
 * eases back. Racing ticks only feed it a speed: the simulation never reads it. */
static float cam_fov = 60, cam_k;
static int32_t dbg_fov[3]; /* cam.fov: now, least and most this race (0.01 degree) */
static int32_t chain[2], chain_tick; /* race.chain: the herring chain now, the longest this race */
static void speed_camera(float speed, float dt) {
  bool classic = SAVE.opts & OPT_CLASSIC_CAMERA;
  float x = fmaxf(0, fminf(1, (speed - 4.0f) / 21.0f)), target = classic ? 0 : x * x * (3 - 2 * x);
  cam_k += (target - cam_k) * fminf(1, dt / 0.5f);
  if (classic) cam_k = 0;
  cam_fov = 60 + 15 * cam_k;
  G.view.dist = classic ? 0 : 4.0f - 0.7f * cam_k;
  cw_node_set(G.cam, "fov", cw_val_float(cam_fov));
  int32_t f = (int32_t)lrintf(cam_fov * 100);
  dbg_fov[0] = f;
  if (!dbg_fov[1] || f < dbg_fov[1]) dbg_fov[1] = f;
  if (f > dbg_fov[2]) dbg_fov[2] = f;
}

/* opt <ghost|camera> <on|off|classic|speed>: the Practice ghost row and the Settings camera row, for tests */
static void cmd_opt(int argc, const char **argv, cw_reply *r) {
  if (argc < 3) { cw_reply_str(r, "usage: opt ghost on|off | opt camera speed|classic"); return; }
  if (!strcmp(argv[1], "ghost")) SAVE.opts = (uint8_t)(!strcmp(argv[2], "off") ? SAVE.opts | OPT_GHOST_OFF : SAVE.opts & ~OPT_GHOST_OFF);
  else if (!strcmp(argv[1], "camera")) SAVE.opts = (uint8_t)(!strcmp(argv[2], "classic") ? SAVE.opts | OPT_CLASSIC_CAMERA : SAVE.opts & ~OPT_CLASSIC_CAMERA);
  else { cw_reply_str(r, "unknown option"); return; }
  cw_reply_str(r, "ok");
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
  if (G.ghost_loaded) tux_set_visible(&G.ghost_tux, false);
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
  G.race_ticks = 0;
  cam_k = 0, dbg_fov[1] = dbg_fov[2] = 0;
  chain[0] = chain[1] = 0, chain_tick = 0;
  speed_camera(0, 0);
  ghost_rec_begin(&G.rec, fnv(ghost_key(G.info->dir, G.mirrored), strlen(ghost_key(G.info->dir, G.mirrored))), G.phys.pos);
  rec_full[0][0] = G.phys.pos.x, rec_full[0][1] = G.phys.pos.y, rec_full[0][2] = G.phys.pos.z;
  G.ghost = ghost_wanted() ? ghost_of(ghost_key(G.info->dir, G.mirrored), false) : NULL;
  if (G.ghost && !ghost_valid(G.ghost)) G.ghost = NULL;
  G.gplay = (ghost_play){ .cache_i = -1 };
  G.dbg_ghost[1] = G.ghost ? G.ghost->count : 0;
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
  bool menu = G.state == ST_TITLE || G.state == ST_PRACTICE || G.state == ST_CUP || G.state == ST_CREDITS ||
              (G.state == ST_SETTINGS && G.settings_from == ST_TITLE);
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
  rumble_stop();
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
  G.medal = 0, G.best_time_new = 0;
  if (!aborted) {
    best_entry *b = best_of(G.info->dir);
    if (b && (b->score == 0 && b->time == 0 ? true : G.score > b->score)) { b->score = G.score, b->time = G.race_time, b->herring = G.herring; G.new_best = true; }
    /* the medal of this time, and the fastest run kept as the course's ghost */
    G.medal = medal_for(G.info, G.race_time);
    G.finish_events++;
    ghost_rec_flush(&G.rec);
    ghost_run *g = ghost_of(ghost_key(G.info->dir, G.mirrored), true);
    if (g && !G.rec.broken && G.rec.run.count >= 2 && (!ghost_valid(g) || G.race_time < g->time)) {
      bool had = ghost_valid(g);
      *g = G.rec.run;
      g->time = G.race_time;
      G.best_time_new = had ? 2 : 1; /* 2: beat a time that stood */
    }
    if (G.medal || G.best_time_new == 2) rumble(0.25f, 0.35f, 220); /* a soft swell in the pad, not a jolt */
  }
  if (G.mode == MODE_CUP) {
    SAVE.cup_race = G.won ? G.race_idx + 1 : G.race_idx;
    SAVE.cup_lives = G.lives;
    if (G.won && G.race_idx + 1 >= G.cups[G.cup_idx].count) { SAVE.cup_won = 1; SAVE.cup_race = 0; SAVE.cup_lives = INIT_NUM_LIVES; }
    if (G.lives <= 0) { SAVE.cup_race = 0; SAVE.cup_lives = INIT_NUM_LIVES; }
  }
  save_write();
  if (!aborted) fx_finish(G.phys.pos, G.phys.vel);
  chain[0] = 0;
  hud_chain(0, 0);
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
  G.race_ticks++;
  ghost_rec_tick(&G.rec, p->pos, p->turn_anim, p->airborne, G.reset_ticks > 0);
  if (G.rec.ticks <= CHECK_TICKS) rec_full[G.rec.ticks][0] = p->pos.x, rec_full[G.rec.ticks][1] = p->pos.y, rec_full[G.rec.ticks][2] = p->pos.z;
  /* landing squash (render only): a spring kicked by the vertical speed lost on touchdown */
  if (G.was_airborne && !p->airborne) {
    G.squash_v += fminf(0.35f, fabsf(p->vel.y) * 0.03f + 0.06f);
    impacts++;
    rumble(fminf(1, fabsf(p->vel.y) * 0.12f + 0.2f), 0.2f, 140); /* the touchdown in the pad (a no-op without motors) */
  }
  G.was_airborne = p->airborne;
  G.squash_v += (-G.squash * 160 - G.squash_v * 14) * dt;
  G.squash += G.squash_v * dt;
  speed_camera(p->speed, dt);
  view_update(&G.view, p, &G.course, dt);
  int got = course_collect(&G.course, p->pos);
  /* a chain: herring caught within 2.5 s of the last one (the sound climbs with it; the score is the original's) */
  if (got) {
    chain[0] = G.race_ticks - chain_tick <= (int)(2.5f * TICK_HZ) && chain[0] ? chain[0] + got : got;
    chain_tick = G.race_ticks;
    if (chain[0] > chain[1]) chain[1] = chain[0];
    audio_pickup(chain[0]);
  } else if (chain[0] && G.race_ticks - chain_tick > (int)(2.5f * TICK_HZ)) chain[0] = 0;
  if (got) { G.herring += got; fx_pickup((t3_vec3){ p->pos.x, p->pos.y + 0.5f, p->pos.z }); rumble(0, 0.45f, 70); }
  if (p->tree_hit_now) { audio_sfx(SFX_TREE); fx_tree_hit(p->pos, p->vel); impacts++; tree_hits[0]++, tree_hits[1] = G.ticks; rumble(0.9f, 0.5f, 260); }
  audio_slide(p->surf_w, p->speed, p->airborne, p->enabled);
  /* rock under the belly: a low buzz that grows with the share of rock (renewed every 6 ticks; a stronger one still running is kept) */
  if (p->enabled && !p->airborne && p->surf_w[SURF_ROCK] > 0.3f && p->speed > 4 && (G.ticks % 6) == 0)
    rumble(0.12f * p->surf_w[SURF_ROCK], 0.28f * p->surf_w[SURF_ROCK], 120);
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

/* rumble (cartwheel/input.h): touchdowns, herring, trees and rock in the pad; the camera never shakes. A request on top
 * of one still running keeps the stronger of each motor and the longer time (the rock buzz never cuts a tree hit short);
 * the motors stop on their own after ms, and at once on pause or when the race ends (rumble_stop) */
static struct { int until; float low, high; int32_t sent; } RB;
static void rumble(float low, float high, int ms) {
  if (SAVE.rumble_off || ms <= 0) return;
  int left = (RB.until - (int)G.ticks) * 1000 / 60;
  if (left > 0) {
    if (low <= RB.low && high <= RB.high && ms <= left) return;
    low = fmaxf(low, RB.low), high = fmaxf(high, RB.high), ms = ms > left ? ms : left;
  }
  RB.low = low, RB.high = high, RB.until = (int)G.ticks + (ms * 60 + 999) / 1000, RB.sent++;
  cw_input_rumble(0, low, high, ms);
}
static void rumble_stop(void) { if (RB.until > (int)G.ticks) cw_input_rumble_stop(0); RB.until = 0; }
static void rumble_toggle(void) {
  SAVE.rumble_off = !SAVE.rumble_off;
  save_write();
  if (SAVE.rumble_off) cw_input_rumble_stop(0);
  else cw_input_rumble(0, 0.3f, 0.3f, 120); /* a taste of it */
}

static bool menu_nav(int *cursor, int count) {
  if (cw_action_just_pressed(0, G.m_down)) *cursor = (*cursor + 1) % count;
  if (cw_action_just_pressed(0, G.m_up)) *cursor = (*cursor + count - 1) % count;
  return cw_action_just_pressed(0, G.m_accept);
}

static void title_tick(void) {
  audio_music(MUS_START);
  if (cw_quality_menu_is_open()) return; /* the graphics page has this frame's presses */
  if (menu_nav(&G.cursor, 6)) {
    switch (G.cursor) {
      case 0:
        G.mode = MODE_CUP, G.cup_idx = 0, G.state = ST_CUP;
        G.race_idx = SAVE.cup_race >= 0 && SAVE.cup_race < G.cups[0].count ? SAVE.cup_race : 0;
        G.lives = SAVE.cup_lives > 0 && SAVE.cup_lives <= INIT_NUM_LIVES ? SAVE.cup_lives : INIT_NUM_LIVES;
        break;
      case 1: G.mode = MODE_PRACTICE; G.state = ST_PRACTICE; G.cursor = 0; break;
      case 2: G.settings_from = ST_TITLE; G.state = ST_SETTINGS; G.settings_cursor = 0; break;
      case 3: rumble_toggle(); break;
      case 4: G.state = ST_CREDITS; audio_music(MUS_OPTIONS); break;
      case 5: cw_quit(); break;
    }
  }
}

static void practice_tick(void) {
  /* rows: course (left/right), conditions, wind, snow, mirror, ghost, Race, Back */
  int row = G.cursor;
  if (cw_action_just_pressed(0, G.m_down)) row = (row + 1) % 8;
  if (cw_action_just_pressed(0, G.m_up)) row = (row + 7) % 8;
  G.cursor = row;
  int d = (cw_action_just_pressed(0, G.m_right) ? 1 : 0) - (cw_action_just_pressed(0, G.m_left) ? 1 : 0);
  if (d && row == 0) G.practice_course = (G.practice_course + G.ncourses + d) % G.ncourses;
  if (d && row == 1) G.practice_cond = (G.practice_cond + COND_COUNT + d) % COND_COUNT;
  if (d && row == 2) G.practice_wind = !G.practice_wind;
  if (d && row == 3) G.practice_snow = !G.practice_snow;
  if (d && row == 4) G.practice_mirror = !G.practice_mirror;
  bool accept = cw_action_just_pressed(0, G.m_accept);
  if ((d || accept) && row == 5) { SAVE.opts ^= OPT_GHOST_OFF; save_write(); }
  if (accept && (row == 6 || row == 0)) begin_loading(G.courses[G.practice_course].dir, G.practice_cond, G.practice_wind, G.practice_mirror, G.practice_snow);
  if ((accept && row == 7) || cw_action_just_pressed(0, G.m_back)) { G.state = ST_TITLE; G.cursor = 1; }
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
  rumble_stop();
  if (menu_nav(&G.pause_cursor, 4)) {
    if (G.pause_cursor == 0) G.state = ST_RACING;
    else if (G.pause_cursor == 1) { G.settings_from = ST_PAUSED; G.state = ST_SETTINGS; G.settings_cursor = 0; }
    else if (G.pause_cursor == 2) rumble_toggle();
    else race_over(true);
  }
  if (cw_action_just_pressed(0, G.m_back) || cw_action_just_pressed(0, G.a_pause)) G.state = ST_RACING;
}

/* Settings: Graphics (the engine's page), Text size, Text background, Camera (Speed: wider and closer with speed; Classic:
 * the original's), Back. The text settings are the player's
 * (cartwheel/render2d.h "text size"): render only, kept in the save */
static void settings_tick(void) {
  if (cw_quality_menu_is_open()) return; /* the graphics page has this frame's presses */
  int row = G.settings_cursor;
  if (cw_action_just_pressed(0, G.m_down)) row = (row + 1) % 5;
  if (cw_action_just_pressed(0, G.m_up)) row = (row + 4) % 5;
  G.settings_cursor = row;
  int d = (cw_action_just_pressed(0, G.m_right) ? 1 : 0) - (cw_action_just_pressed(0, G.m_left) ? 1 : 0);
  bool accept = cw_action_just_pressed(0, G.m_accept);
  if (row == 1 && (d || accept)) {
    if (d < 0) cw_text_scale_cycle_back(); else cw_text_scale_cycle();
    SAVE.text = cw_text_scale_saved(); save_write();
  }
  if (row == 2 && (d || accept)) {
    cw_text_backdrop_cycle();
    if (d < 0) cw_text_backdrop_cycle(), cw_text_backdrop_cycle(); /* three steps: twice more is one back */
    SAVE.text = cw_text_scale_saved(); save_write();
  }
  if (row == 3 && (d || accept)) { SAVE.opts ^= OPT_CLASSIC_CAMERA; save_write(); }
  if (accept && row == 0) cw_quality_menu_open();
  if ((accept && row == 4) || cw_action_just_pressed(0, G.m_back)) {
    G.state = G.settings_from;
    if (G.state == ST_TITLE) G.cursor = 2; else G.pause_cursor = 1;
  }
}

static void race_over_tick(float dt) {
  G.over_time += dt;
  physics_after_step(&G.phys, dt);
  speed_camera(0, dt * 0.4f); /* the lens eases back slowly past the line */
  view_update(&G.view, &G.phys, &G.course, dt);
  if (G.over_time < 0.5f) return;
  if (cw_action_just_pressed(0, G.m_accept) || cw_action_just_pressed(0, G.m_back)) {
    if (G.mode == MODE_PRACTICE) { G.state = ST_PRACTICE; G.cursor = 6; } /* on Race!: one press races again */
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
  /* the lever's values are Very Low .. High: the terrain's quality (cartwheel/terrain.h) */
  course_set_terrain_quality(G.lever_terrain >= 0 ? CW_QUALITY_VERY_LOW + cw_gfx_get(G.lever_terrain) : -1);
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
  if (!strcmp(argv[1], "spray")) { fx_set_spray_hidden(hide); cw_reply_str(r, hide ? "hidden" : "shown"); return; }
  if (!strcmp(argv[1], "tux") || !strcmp(argv[1], "all")) tux_set_visible(&G.tux, !hide);
  if (strcmp(argv[1], "tux")) course_objects_show(argv[1], !hide);
  cw_reply_str(r, hide ? "hidden" : "shown");
}

/* dev: tp <x> <z>: put Tux on the surface there (racing), to test items and spots */
static void cmd_tp(int argc, const char **argv, cw_reply *r) {
  if (argc < 3 || !G.course_loaded || G.state != ST_RACING || !G.phys.course) { cw_reply_str(r, "usage: tp <x> <z> (while racing)"); return; }
  float x = (float)atof(argv[1]), z = (float)atof(argv[2]);
  if (!strcmp(argv[1], "finish")) x = G.course.start_x, z = -G.course.play_length + (float)atof(argv[2]); /* tp finish <m before the line> */
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
  cw_text_scale_restore(SAVE.text); /* the player's Text size and Text background */
  /* graphics: the engine's presets plus one lever of ours; Medium unless the player chose */
  static const char *const EDGES[] = { "Sharp", "Smooth" };
  const cw_quality_lever_presets EDGES_PRESET = { .very_low = 0, .low = 0, .medium = 1, .high = 1, .ultra = 1 };
  /* GL only: the engine's WebGPU path has no MSAA on an HDR scene, and alpha
   * to coverage is three.c's GL feature; the lever would be a no-op there */
  G.lever_edges = cw_gpu_backend() == CW_GPU_WEBGPU ? -1 : cw_quality_lever_add("Tree edges", EDGES, 2, EDGES_PRESET);
  static const char *const TERRAIN[] = { "Very Low", "Low", "Medium", "High" };
  /* High's tier (4 control texels blended, height blending) costs 5 ms of a
   * 14 ms frame at 3024x1702 and draws the original tiles no better (a pixel
   * diff of the same frame: 0.02 mean on bunny_hill; the rock on
   * path_of_daggers smears where Medium's triplanar keeps it crisp), so the
   * High preset takes Medium's and Ultra takes High's. Very Low's tier reads each layer once and switches to the
   * coarser levels sooner; Low takes it too (the snow reads the same from the race camera) */
  const cw_quality_lever_presets TERRAIN_PRESET = { .very_low = 0, .low = 0, .medium = 2, .high = 2, .ultra = 3 };
  G.lever_terrain = cw_quality_lever_add("Terrain", TERRAIN, 4, TERRAIN_PRESET);
  static const char *const TUXSH[] = { "Contact", "Cast" };
  const cw_quality_lever_presets TUXSH_PRESET = { .very_low = 0, .low = 0, .medium = 1, .high = 1, .ultra = 1 };
  G.lever_tux_shadow = cw_quality_lever_add("Tux shadow", TUXSH, 2, TUXSH_PRESET);
  static const char *const SNOWD[] = { "Plain", "Glints", "Full" };
  /* Very Low and Low: nothing; Medium: glints + cloud shadows; High: + ripples and the trail */
  const cw_quality_lever_presets SNOWD_PRESET = { .very_low = 0, .low = 0, .medium = 1, .high = 2, .ultra = 2 };
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
  cw_debug_command("opt", "opt ghost on|off | opt camera speed|classic: the ghost and camera options (tests)", cmd_opt);
  cw_debug_command("ghost", "ghost keep | ghost check [offset]: keep this run as the course's ghost, or check the recording against the path (dev)", cmd_ghost);
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
  cw_debug_field("cam.offset", cam_offset, CW_DBG_I32, 3);
  cw_debug_i32("rumble.sent", &RB.sent);
  cw_debug_field("race.tree_hits", tree_hits, CW_DBG_I32, 2);
  cw_debug_i32("phys.contacts", &G.dbg_contacts);
  cw_debug_i32("ticks", &G.ticks);
  cw_debug_field("ghost", G.dbg_ghost, CW_DBG_I32, 4);
  cw_debug_i32("race.medal", &G.medal);
  cw_debug_field("cam.fov", dbg_fov, CW_DBG_I32, 3);
  cw_debug_field("race.chain", chain, CW_DBG_I32, 2);
  cw_debug_i32("race.best_time_new", &G.best_time_new);
  cw_debug_i32("race.finish_events", &G.finish_events);
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
    case ST_SETTINGS: settings_tick(); break;
  }
  if (G.state != ST_RACING && G.state != ST_PAUSED && G.state != ST_RACE_OVER && cam_fov != 60) { cam_fov = 60, cam_k = 0, G.view.dist = 0; cw_node_set(G.cam, "fov", cw_val_float(60)); }
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

/* Menus lay out from the size their text draws at (the player's text scale, cartwheel/render2d.h "text size"):
 * rows are spaced by hud_drawn, cards sized from measured widths, and a list too long for its space scrolls with
 * its cursor (an arrow says more is above or below). */
static const cw_color MENU_YELLOW = { 1, 0.89f, 0.01f, 1 }, MENU_WHITE = { 1, 1, 1, 1 };
static void more_arrow(float cx, float y, float s, bool up) {
  cw_vec2 q[3] = { { cx - s, up ? y + s * 0.6f : y }, { cx + s, up ? y + s * 0.6f : y }, { cx, up ? y : y + s * 0.6f } };
  cw_r2d_polygon(q, 3, (cw_color){ 1, 1, 1, 0.8f });
}
/* the first row shown so the cursor stays in a window of `fit` rows */
static int list_first(int count, int cursor, int fit) {
  if (fit >= count) return 0;
  int first = cursor - fit / 2;
  return first < 0 ? 0 : first > count - fit ? count - fit : first;
}
/* a list of rows centred on cx, from y0 (the first row's top) to no lower than bottom; each row's text at its
 * size (sizes[i], or size), spaced by pitch per px of drawn size. Returns where it ends. */
static float draw_list(const char *const *items, const float *sizes, int count, int cursor, float cx, float y0, float bottom,
                       float size, float pitch, float min_w, bool card) {
  float drawn = 0, wmax = 0;
  for (int i = 0; i < count; i++) {
    float s = sizes ? sizes[i] : size;
    drawn = fmaxf(drawn, hud_drawn(true, s));
    wmax = fmaxf(wmax, hud_measure(true, items[i], s, 0).width);
  }
  cw_vec2 sz = cw_r2d_canvas_size();
  float step = drawn * pitch, pad = 18 * drawn / size;
  int fit = (int)floorf((bottom - y0) / step);
  if (fit < 1) fit = 1;
  int first = list_first(count, cursor, fit), shown = count - first < fit ? count - first : fit;
  float cw = fminf(sz.x - 24, fmaxf(min_w, wmax + 2 * pad + 20));
  if (card) hud_panel(cx - cw / 2, y0 - pad, cw, step * (float)shown + pad + 8, 0.42f);
  for (int r = 0; r < shown; r++) {
    int i = first + r;
    float s = sizes ? sizes[i] : size, y = y0 + step * (float)r;
    if (i == cursor) hud_panel(cx - cw / 2 + 20, y - 4 * drawn / size, cw - 40, step - 4 * drawn / size, 0.35f); /* the cursor's row a shade deeper */
    hud_title(items[i], cx, y, s, i == cursor ? MENU_YELLOW : MENU_WHITE, 1);
  }
  float a = 9 * drawn / size;
  if (first > 0) more_arrow(cx + cw / 2 - pad - a, y0 - pad + 4, a, true);
  if (first + shown < count) more_arrow(cx + cw / 2 - pad - a, y0 + step * (float)shown - 2, a, false);
  return y0 + step * (float)shown + 8;
}
/* bottom: the lowest the rows' text may reach */
static void draw_menu(const char *title, const char *const *items, int count, int cursor, float y0, float bottom) {
  cw_vec2 size = cw_r2d_canvas_size();
  bool logo = !strcmp(title, "Tux Racer");
  float drawn = hud_drawn(true, 30), step = 48 * drawn / 30, pad = 18 * drawn / 30;
  float wmax = 0;
  for (int i = 0; i < count; i++) wmax = fmaxf(wmax, hud_measure(true, items[i], 30, 0).width);
  float card_w = fmaxf(340, wmax + 2 * pad + 20), list_h = step * (float)count + 8;
  /* the list starts lower by as much as the title grew with the text scale */
  float th = logo ? 0 : hud_measure(true, title, 64, 0).height, top = y0 + th - th * 64 / hud_drawn(true, 64);
  if (top + step * (float)(count - 1) + drawn * 1.0f <= bottom || card_w > size.x / 2 - 16) { /* stacked: the title over the list (which scrolls if it must) */
    if (logo) hud_image("original/textures/menu_title.png", size.x / 2 - 192, 36, 384, 192); /* the original logo */
    else hud_title(title, size.x / 2, 60, 64, (cw_color){ 1, 0.9f, 0.2f, 1 }, 1);
    draw_list(items, NULL, count, cursor, size.x / 2, top, bottom + step - drawn * 1.0f, 30, 48.0f / 30, 340, G.course_loaded);
    return;
  }
  /* side by side when the text is large: the title on the left half, the list on the right */
  float lx = size.x * 0.25f, rx = size.x * 0.75f, avail = bottom;
  if (logo) {
    float w = fminf(384, size.x / 2 - 32), h = w / 2;
    hud_image("original/textures/menu_title.png", lx - w / 2, (size.y - h) / 2, w, h);
  } else hud_text_box(true, title, 16, (size.y - hud_measure(true, title, 64, size.x / 2 - 32).height) / 2, size.x / 2 - 32, 64, (cw_color){ 1, 0.9f, 0.2f, 1 }, 1);
  float y = fmaxf(16 + pad, (avail - list_h) / 2 + pad);
  draw_list(items, NULL, count, cursor, rx, y, bottom + step - drawn * 1.0f, 30, 48.0f / 30, 340, G.course_loaded);
}

/* Per frame, before the nodes sync to three.c and the scene draws (a
 * pre_render hook at order -10): Tux and the camera at the same sub-tick
 * phase. The app's render callback runs AFTER the 3D scene (the "app"
 * overlay pass), so anything placed there is drawn a frame late; and a
 * camera written in the fixed tick sits at tick positions while Tux is
 * interpolated, so at any frame rate but exactly 60 the two drift against
 * each other every frame: measured as a 6-pixel vibration at 120 Hz. */
/* the shake check (cam.offset): the camera as drawn (the camera node's global transform) against the view (view.c) drawn
 * between its last two ticks, on the frames within half a second of a tree hit or a touchdown. cam.offset: those frames,
 * the ones off by over 0.1 mm or 0.1 milliradian, and the largest offset (in 0.1 mm and 0.1 milliradian units). Nothing
 * moves the camera on an impact: [n, 0, 0]. */
static void offsetgate(t3_vec3 pos, t3_vec3 dir) {
  static int seen;
  if (impacts != seen) seen = impacts, impact_frames = 30;
  if (impact_frames <= 0) return;
  impact_frames--;
  const t3_mat4 *m = cw_node3d_global_transform(G.cam);
  if (!m) return;
  float fl = sqrtf(m->e[8] * m->e[8] + m->e[9] * m->e[9] + m->e[10] * m->e[10]);
  t3_vec3 d = { m->e[12] - pos.x, m->e[13] - pos.y, m->e[14] - pos.z }, f = { -m->e[8] / fl - dir.x, -m->e[9] / fl - dir.y, -m->e[10] / fl - dir.z };
  float off = t3_vec3_length(d) + t3_vec3_length(f);
  cam_offset[0]++;
  if (off > 1e-4f) cam_offset[1]++;
  if ((int32_t)(off * 1e4f) > cam_offset[2]) cam_offset[2] = (int32_t)(off * 1e4f);
}

static void pre_render_hook(cw_tree *t, double alpha, void *u) {
  (void)t, (void)u;
  if (!G.course_loaded) return;
  ghost_draw(alpha);
  if (G.state != ST_INTRO && cw_body3d_valid(G.phys.body)) {
    cw_xform3 bx = cw_body3d_interpolated(G.phys.body, (float)alpha);
    place_tux(bx.p);
    G.dbg_alpha = (float)alpha;
    G.dbg_body_r[0] = bx.p.x, G.dbg_body_r[1] = bx.p.y, G.dbg_body_r[2] = bx.p.z;
    const char *kc = getenv("TUXRACER_KEYCAM"); /* dev: key art, the camera held at an offset from Tux, aimed at an offset from him */
    float k[6];
    if (kc && sscanf(kc, "%f %f %f %f %f %f", &k[0], &k[1], &k[2], &k[3], &k[4], &k[5]) == 6) {
      cw_node3d_set_position(G.cam, (t3_vec3){ bx.p.x + k[0], bx.p.y + k[1], bx.p.z + k[2] });
      cw_node3d_look_at(G.cam, (t3_vec3){ bx.p.x + k[3], bx.p.y + k[4], bx.p.z + k[5] }, (t3_vec3){ 0, 1, 0 });
      return;
    }
  }
  if (G.view.mode == VIEW_FIXED || !G.view.initialized) return; /* parked by the cam dev command */
  float a = G.view_prev.initialized && G.view.mode == G.view_prev.mode ? (float)alpha : 1.0f;
  t3_vec3 pos = t3_vec3_lerp(G.view_prev.pos, G.view.pos, a);
  t3_vec3 dir = t3_vec3_normalize(t3_vec3_lerp(G.view_prev.dir, G.view.dir, a));
  t3_vec3 up = t3_vec3_normalize(t3_vec3_lerp(G.view_prev.up, G.view.up, a));
  cw_node3d_set_position(G.cam, pos);
  cw_node3d_look_at(G.cam, t3_vec3_add(pos, dir), up);
  offsetgate(pos, dir);
  G.dbg_cam_r[0] = pos.x, G.dbg_cam_r[1] = pos.y, G.dbg_cam_r[2] = pos.z;
  G.dbg_cam_r[3] = dir.x, G.dbg_cam_r[4] = dir.y, G.dbg_cam_r[5] = dir.z;
}

static void render(void *user, double alpha) {
  (void)user;
  cw_vec2 size;
  char buf[160];
  switch (G.state) {
    case ST_TITLE: {
      const char *const ITEMS[] = { "Cup", "Practice", "Settings", SAVE.rumble_off ? "Rumble: Off" : "Rumble: On", "Credits", "Quit" };
      hud_begin();
      size = cw_r2d_canvas_size();
      menu_backdrop();
      const char *by = "Tux Racer 0.61 by Sunspire Studios, on the cartwheel engine";
      float fh = hud_measure(false, by, 14, size.x - 24).height;
      draw_menu("Tux Racer", ITEMS, 6, G.cursor, 236, size.y - 12 - fh * 0.85f - 4);
      hud_text_box(false, by, 12, size.y - 12 - fh * 0.85f, size.x - 24, 14, (cw_color){ 1, 1, 1, 0.55f }, 1);
      hud_end();
      break;
    }
    case ST_SETTINGS: {
      char r1[48], r2[48];
      snprintf(r1, sizeof r1, "Text size:  < %s >", cw_text_scale_label());
      snprintf(r2, sizeof r2, "Text background:  < %s >", cw_text_backdrop_label());
      const char *const ITEMS[] = { "Graphics", r1, r2, SAVE.opts & OPT_CLASSIC_CAMERA ? "Camera:  < Classic >" : "Camera:  < Speed >", "Back" };
      if (G.settings_from == ST_PAUSED) hud_draw(&G.phys, G.race_time, G.herring, G.mode == MODE_CUP ? G.lives : 0);
      hud_begin();
      size = cw_r2d_canvas_size();
      if (G.settings_from == ST_PAUSED) hud_box(0, 0, size.x, size.y, (cw_color){ 0, 0, 0, 0.45f });
      else menu_backdrop();
      draw_menu("Settings", ITEMS, 5, G.settings_cursor, 200, size.y - 12);
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
      const char *rows[8];
      snprintf(buf, sizeof buf, "<  %s  >", c->title);
      char r1[64], r2[64], r3[64], r4[64], r5[64];
      snprintf(r1, sizeof r1, "Conditions:  < %s >", COND_NAME[G.practice_cond]);
      snprintf(r2, sizeof r2, "Wind:  < %s >", G.practice_wind ? "On" : "Off");
      snprintf(r3, sizeof r3, "Snow:  < %s >", G.practice_snow ? "On" : "Off");
      snprintf(r4, sizeof r4, "Mirror:  < %s >", G.practice_mirror ? "On" : "Off");
      snprintf(r5, sizeof r5, "Ghost:  < %s >", SAVE.opts & OPT_GHOST_OFF ? "Off" : "On");
      rows[0] = buf, rows[1] = r1, rows[2] = r2, rows[3] = r3, rows[4] = r4, rows[5] = r5, rows[6] = "Race!", rows[7] = "Back";
      const float sizes[8] = { 30, 24, 24, 24, 24, 24, 24, 24 };
      /* the card: the rows (they scroll when the text is large), the course's description, par and best under them */
      float card_w = 520;
      for (int i = 0; i < 8; i++) card_w = fmaxf(card_w, hud_measure(true, rows[i], sizes[i], 0).width + 60);
      float cx = size.x / 2, right = size.x - 210; /* the preview's column */
      if (cx + card_w / 2 > right - 8) { card_w = fminf(card_w, right - 20); cx = fmaxf(12 + card_w / 2, right - 8 - card_w / 2); }
      float iw = card_w - 40, ix = cx - iw / 2;
      char par[96], best[128] = "";
      float mt[3];
      medal_times(c, mt);
      snprintf(par, sizeof par, "Par %.0f s     Bronze %.0f s     Silver %.0f s     Gold %.0f s", c->par, mt[0], mt[1], mt[2]);
      const ghost_run *gh = ghost_of(ghost_key(c->dir, G.practice_mirror), false);
      if (gh && !ghost_valid(gh)) gh = NULL;
      const best_entry *b = NULL;
      for (int i = 0; i < MAX_COURSES; i++) if (SAVE.best[i].course_hash == fnv(c->dir, strlen(c->dir)) && (SAVE.best[i].score || SAVE.best[i].time)) b = &SAVE.best[i];
      int bm = gh ? medal_for(c, gh->time) : 0;
      if (b && gh) snprintf(best, sizeof best, "Fastest %02d:%02d.%02d%s%s     Best score %06d", (int)(gh->time / 60), (int)gh->time % 60, (int)(gh->time * 100) % 100, bm ? "  " : "", MEDAL_NAME[bm], b->score);
      else if (b) snprintf(best, sizeof best, "Best: %02d:%02d.%02d   %d herring   score %06d", (int)(b->time / 60), (int)b->time % 60, (int)(b->time * 100) % 100, b->herring, b->score);
      float hd = c->desc[0] ? hud_measure(false, c->desc, 17, iw).height : 0, hp = hud_measure(false, par, 16, iw).height;
      float hb = b ? hud_measure(false, best, 16, iw).height : 0;
      float pitch = 38.0f / 30; /* 38 px a row at 100% */
      float drawn = fmaxf(hud_drawn(true, 30), hud_drawn(true, 24)), step = drawn * pitch;
      float card_top = 40 + hud_measure(true, "Practice", 48, 0).height + 6;
      float info_h = hd + 6 + hp + (b ? 6 + hb : 0) + 12;
      float card_bottom = fminf(size.y - 12, fmaxf(508, card_top + 16 + 8 * step + info_h));
      hud_panel(cx - card_w / 2, card_top, card_w, card_bottom - card_top, 0.42f);
      hud_panel(size.x - 200, 100, 180, 180, 0.42f);
      hud_image(preview, size.x - 190, 110, 160, 160);
      float info_y = fmaxf(card_bottom - info_h, card_top + 16 + 42 * hud_drawn(true, 30) / 30 + 8);
      int fit = (int)floorf((info_y - 8 - (card_top + 16)) / step);
      if (fit < 1) fit = 1;
      int first = list_first(8, G.cursor, fit), shown = 8 - first < fit ? 8 - first : fit;
      for (int r = 0; r < shown; r++) {
        int i = first + r;
        hud_title(rows[i], cx, card_top + 16 + step * (float)r, sizes[i], i == G.cursor ? MENU_YELLOW : MENU_WHITE, 1);
      }
      float a = 9 * drawn / 30;
      if (first > 0) more_arrow(cx + card_w / 2 - 20 - a, card_top + 10, a, true);
      if (first + shown < 8) more_arrow(cx + card_w / 2 - 20 - a, card_top + 16 + step * (float)shown - 8, a, false);
      float y = info_y;
      if (hd > 0) y += hud_text_box(false, c->desc, ix, y, iw, 17, (cw_color){ 1, 1, 1, 0.85f }, 1) + 6;
      y += hud_text_box(false, par, ix, y, iw, 16, (cw_color){ 1, 1, 1, 0.6f }, 1) + 6;
      if (b) hud_text_box(false, best, ix, y, iw, 16, bm ? MEDAL_COLOR[bm] : (cw_color){ 1, 0.95f, 0.01f, 0.9f }, 1);
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
        const char *names[MAX_RACES];
        char rows[MAX_RACES][96];
        for (int i = 0; i < cup->count; i++) {
          const course_info *ci = find_course(cup->races[i].dir);
          snprintf(rows[i], sizeof rows[i], "%d. %s", i + 1, ci ? ci->title : cup->races[i].dir);
          names[i] = rows[i];
        }
        const cup_race *r = &cup->races[G.race_idx];
        int m = (int)(r->time[DIFFICULTY] / 60), s = (int)r->time[DIFFICULTY] % 60, hs = (int)(r->time[DIFFICULTY] * 100) % 100;
        char req[160], lives[32];
        snprintf(req, sizeof req, "Requirements:  Time %02d:%02d.%02d   Herring %03d   Score %06d", m, s, hs, r->herring[DIFFICULTY], r->score[DIFFICULTY]);
        snprintf(lives, sizeof lives, "Lives: %d", G.lives);
        const char *keys = "{menu_accept} race    {menu_back} back";
        float card_w = 600;
        for (int i = 0; i < cup->count; i++) card_w = fmaxf(card_w, hud_measure(true, names[i], 26, 0).width + 60);
        card_w = fminf(card_w, size.x - 24);
        float iw = card_w - 40, ix = size.x / 2 - iw / 2;
        float card_top = 40 + hud_measure(true, cup->name, 48, 0).height + 14;
        float hr = hud_measure(false, req, 18, iw).height, hl = hud_measure(false, lives, 20, iw).height, hk = hud_prompt_size(keys, 18).y;
        float info_h = hr + 8 + hl + 12;
        float step = 46 * hud_drawn(true, 26) / 26;
        float bottom = size.y - 12 - hk - 12; /* the keys line sits under the card */
        int fit = (int)floorf((bottom - info_h - (card_top + 18)) / step);
        if (fit < 1) fit = 1;
        int first = list_first(cup->count, G.race_idx, fit), shown = cup->count - first < fit ? cup->count - first : fit;
        float card_h = 18 + step * (float)shown + info_h;
        hud_panel(size.x / 2 - card_w / 2, card_top, card_w, card_h, 0.42f);
        for (int k = 0; k < shown; k++) {
          int i = first + k;
          hud_title(names[i], size.x / 2, card_top + 18 + step * (float)k, 26,
                    i == G.race_idx ? MENU_YELLOW : i < G.race_idx ? (cw_color){ 0.6f, 1, 0.6f, 1 } : (cw_color){ 1, 1, 1, 0.5f }, 1);
        }
        float y = card_top + 18 + step * (float)shown;
        y += hud_text_box(false, req, ix, y, iw, 18, (cw_color){ 1, 0.95f, 0.01f, 1 }, 1) + 8;
        hud_text_box(false, lives, ix, y, iw, 20, (cw_color){ 1, 1, 1, 1 }, 1);
        hud_prompt(keys, size.x / 2, fmaxf(card_top + card_h + 12, fminf(468, size.y - 12 - hk)), 18, (cw_color){ 1, 1, 1, 0.6f }, 1);
      }
      hud_end();
      break;
    }
    case ST_CREDITS: {
      hud_begin();
      size = cw_r2d_canvas_size();
      menu_backdrop();
      float pw = fminf(660, size.x - 24), iw = pw - 40, top = 16, bottom = size.y - 16;
      hud_panel(size.x / 2 - pw / 2, top, pw, bottom - top, 0.45f);
      /* the roll: wrapped lines; when it is taller than the card (large text) it scrolls by itself, holding at
       * each end (render time: nothing here reaches the simulation) */
      float total = 0;
      for (int i = 0; i < G.ncredits; i++) {
        const char *bar = strchr(G.credits[i], '|');
        if (!bar) continue;
        bool h1 = !strncmp(G.credits[i], "h1", 2), h2 = !strncmp(G.credits[i], "h2", 2), small = !strncmp(G.credits[i], "text_small", 10);
        float sz = h1 ? 30 : h2 ? 22 : small ? 13 : 16;
        int lines = bar[1] ? hud_measure(h1 || h2, bar + 1, sz, iw).lines : 1;
        total += (float)(lines < 1 ? 1 : lines) * 1.15f * hud_drawn(h1 || h2, sz);
      }
      float room = bottom - top - 28, over = total - room;
      static int frames;
      if (over > 0) {
        float speed = 30.0f / 60, hold = 120, run = over / speed, t = fmodf((float)frames, hold + run + hold + run);
        G.credits_scroll = t < hold ? 0 : t < hold + run ? (t - hold) * speed : t < hold + run + hold ? over : over - (t - hold - run - hold) * speed;
        frames++;
      } else G.credits_scroll = 0, frames = 0;
      cw_r2d_push_clip((cw_rect){ size.x / 2 - pw / 2, top + 6, pw, bottom - top - 12 });
      float y = top + 14 - G.credits_scroll;
      for (int i = 0; i < G.ncredits; i++) {
        const char *bar = strchr(G.credits[i], '|');
        if (!bar) continue;
        bool h1 = !strncmp(G.credits[i], "h1", 2), h2 = !strncmp(G.credits[i], "h2", 2), small = !strncmp(G.credits[i], "text_small", 10);
        float sz = h1 ? 30 : h2 ? 22 : small ? 13 : 16;
        int lines = bar[1] ? hud_measure(h1 || h2, bar + 1, sz, iw).lines : 1;
        float h = (float)(lines < 1 ? 1 : lines) * 1.15f * hud_drawn(h1 || h2, sz);
        if (bar[1] && y + h > top && y < bottom)
          hud_text_box(h1 || h2, bar + 1, size.x / 2 - iw / 2, y, iw, sz, h1 || h2 ? (cw_color){ 1, 0.9f, 0.2f, 1 } : (cw_color){ 1, 1, 1, 1 }, 1);
        y += h;
      }
      cw_r2d_pop_clip();
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
      hud_chain(chain[0], (float)(G.race_ticks - chain_tick) / TICK_HZ);
      if (!getenv("TUXRACER_NO_HUD")) hud_draw(&G.phys, G.race_time, G.herring, G.mode == MODE_CUP ? G.lives : 0); /* dev: key art without the HUD */
      break;
    case ST_PAUSED: {
      const char *const ITEMS[] = { "Resume", "Settings", SAVE.rumble_off ? "Rumble: Off" : "Rumble: On", "Quit race" };
      hud_draw(&G.phys, G.race_time, G.herring, G.mode == MODE_CUP ? G.lives : 0);
      hud_begin();
      size = cw_r2d_canvas_size();
      hud_box(0, 0, size.x, size.y, (cw_color){ 0, 0, 0, 0.45f });
      draw_menu("Paused", ITEMS, 4, G.pause_cursor, 220, size.y - 12);
      hud_end();
      break;
    }
    case ST_RACE_OVER: {
      hud_draw(&G.phys, G.race_time, G.herring, G.mode == MODE_CUP ? G.lives : 0);
      hud_begin();
      size = cw_r2d_canvas_size();
      const char *msg = G.aborted ? "Race aborted." :
        G.mode == MODE_PRACTICE ? (G.best_time_new == 2 ? "New best time! Your ghost races this run from now on." :
                                   G.best_time_new == 1 && ghost_wanted() ? "Your ghost will race this run next time." : G.new_best ? "You beat your best score!" : "") :
        G.won && G.race_idx + 1 >= G.cups[G.cup_idx].count ? "Congratulations! You won the cup!" : G.won ? "You advanced to the next race!" :
        G.lives <= 0 ? "You didn't advance, and you're out of lives." : "You didn't advance.";
      int m = (int)(G.race_time / 60), s = (int)G.race_time % 60, hs = (int)(G.race_time * 100) % 100;
      char rows[3][48];
      snprintf(rows[0], sizeof rows[0], "Time: %02d:%02d.%02d", m, s, hs);
      snprintf(rows[1], sizeof rows[1], "Herring: %03d", G.herring);
      snprintf(rows[2], sizeof rows[2], "Score: %06d", G.score);
      /* the card from its measured lines, centred; 600 x 300 at 100% */
      float pw = 600;
      for (int i = 0; i < 3; i++) pw = fmaxf(pw, hud_measure(true, rows[i], 26, 0).width + 60);
      /* between the top cards and the bottom; clear of the speed gauge (bottom right) when it reaches down there */
      float top_limit = hud_top_bottom() + 8, gauge_top = size.y - 190;
      pw = fminf(pw, size.x - 24);
      float iw = pw - 40, ix = size.x / 2 - iw / 2;
      /* the medal is the headline: it swells in over the first second (never a blink), the medal's colour */
      const char *head = !G.aborted && G.medal ? (G.medal == 3 ? "Gold medal!" : G.medal == 2 ? "Silver medal!" : "Bronze medal!") : "Race Over";
      float swell = fminf(1, G.over_time / 0.8f), ease = swell * swell * (3 - 2 * swell);
      cw_color head_c = !G.aborted && G.medal ? MEDAL_COLOR[G.medal] : (cw_color){ 1, 0.9f, 0.2f, 1 };
      if (!G.aborted && G.medal) head_c.a = 0.15f + 0.85f * ease;
      float msg_a = fmaxf(0, fminf(1, (G.over_time - 0.4f) / 0.6f));
      float ht = hud_measure(true, head, 44, iw).height, step = 40 * hud_drawn(true, 26) / 26;
      float hm = msg[0] ? hud_measure(false, msg, 20, iw).height : hud_drawn(false, 20) * 1.2f, hk = hud_prompt_size("{menu_accept} continue", 18).y;
      float ph = 20 + ht + 14 + 3 * step + 20 + hm + 8 + hk + 20;
      ph = fmaxf(ph, fminf(300, size.y - 8 - top_limit));
      float py = fmaxf(top_limit, fminf(110, top_limit + (size.y - 8 - top_limit - ph) / 2));
      if (py + ph > gauge_top && pw > size.x - 2 * 198) { /* narrower, so the card clears the gauge; the text wraps */
        pw = size.x - 2 * 198, iw = pw - 40, ix = size.x / 2 - iw / 2;
        ht = hud_measure(true, head, 44, iw).height;
        hm = msg[0] ? hud_measure(false, msg, 20, iw).height : hm;
        ph = fmaxf(20 + ht + 14 + 3 * step + 20 + hm + 8 + hk + 20, fminf(300, size.y - 8 - top_limit));
        py = fmaxf(top_limit, fminf(110, top_limit + (size.y - 8 - top_limit - ph) / 2));
      }
      hud_panel(size.x / 2 - pw / 2, py, pw, ph, 0.55f);
      float y = py + 20;
      y += hud_text_box(true, head, ix, y, iw, 44, head_c, 1) + 14;
      for (int i = 0; i < 3; i++) hud_title(rows[i], size.x / 2, y + step * (float)i, 26, i == 0 && !G.aborted && G.medal ? MEDAL_COLOR[G.medal] : (cw_color){ 1, 1, 1, 1 }, 1);
      y += 3 * step + 20;
      if (msg[0]) hud_text_box(false, msg, ix, y, iw, 20, (cw_color){ 1, 0.95f, 0.01f, G.mode == MODE_PRACTICE ? msg_a : 1 }, 1);
      y += hm + 8;
      if (G.over_time >= 0.5f) hud_prompt("{menu_accept} continue", size.x / 2, y, 18, (cw_color){ 1, 1, 1, 0.6f }, 1);
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
