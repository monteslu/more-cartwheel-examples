/* coopplatformer (working title): the app. Shell (title, join, pause, settings, saves), input, the three.c scene
 * (lights, post, characters, effects), the HUD and map, and generated sound.
 *
 * Built twice: `coopplatformer` reads the devices; `coopplatformer_auto` plays itself with the bots in sim.c
 * (a two-player run, no input), for tests and screenshots.
 *
 * Debug fields (g.*): state, players, tick, abilities, coins, kills, deaths, rescues, secrets, checkpoint, boss_hp,
 * won, hash, pos (player 0), cam (x, y, z), hp, bubbles. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/debug.h"
#include "cartwheel/explosions.h"
#include "cartwheel/input.h"
#include "cartwheel/particles.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/platform.h"
#include "cartwheel/quality.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "game.h"

/* ── saves ─────────────────────────────────────────────────────────── */
typedef struct save_t {
  uint32_t abilities;
  int32_t coins, best_ticks;
  uint8_t seen[WORLD_H / 4][WORLD_W / 4];
  uint32_t reached; /* flags touched (the map warps between them) */
} save_t;

#define NTORCH 4
static const float PLAYER_RGB[MAX_PLAYERS][3] = { { 0.25f, 0.6f, 1.0f }, { 1.0f, 0.4f, 0.3f }, { 0.45f, 0.85f, 0.35f }, { 1.0f, 0.8f, 0.25f } };

/* ── scene ─────────────────────────────────────────────────────────── */
static struct {
  cw_node root, world, cam, sun, env, post;
  cw_node plight[MAX_PLAYERS];
  cw_node torch[4];
  cw_node pnode[MAX_PLAYERS];
  t3_mesh *bubble[MAX_PLAYERS];
  rig *prig[MAX_PLAYERS];
  cw_node pmodel[MAX_PLAYERS];
  float yaw[MAX_PLAYERS], runph[MAX_PLAYERS];
  float spread_x[MAX_PLAYERS], spread_z[MAX_PLAYERS]; /* drawn apart when players stand in one spot */
  cw_node enode[MAX_ENEMIES], emodel[MAX_ENEMIES];
  rig *erig[MAX_ENEMIES];
  float ephase[MAX_ENEMIES], eyaw[MAX_ENEMIES];
  cw_node coin_node[MAX_COINS];
  t3_mesh *coin_mesh[MAX_COINS];
  cw_node pick_node[MAX_PICKUPS];
  t3_mesh *plat_mesh[MAX_PLATFORMS];
  cw_node fx_dust[MAX_PLAYERS], fx_spark, fx_splash, fx_burst, fx_ember[4], fx_hit;
  cw_node fog, rim, fvol[8], water3d[16], torchfire[4];
  cw_node fx_boom, fx_boomsmoke, fx_boomspark, fx_wave, fx_motes, fx_haze[4], fx_bubbles, fx_foam, fx_lavapop[4], fx_debris;
  bool prev_alive[MAX_ENEMIES];
  int nwspan, nlspan;
  float wspan[16][4], lspan[16][4];
  int nember;
  float time;
  float light_x[64], light_y[64], light_r[64], light_g[64], light_b[64], light_e[64], light_range[64];
  int nlight_src;
  bool built;
} S;

static struct {
  bool on;
  cw_bus music_bus, sfx_bus;
  cw_sound sfx[16];
  cw_sound music;
  cw_voice music_voice;
} A;
enum { SFX_JUMP, SFX_LAND, SFX_COIN, SFX_HURT, SFX_SPLASH, SFX_DASH, SFX_POWER, SFX_STOMP, SFX_BREAK, SFX_BUBBLE, SFX_POP, SFX_DIE, SFX_WIN, SFX_CHECK, SFX_WALL, SFX_COUNT };

static cw_action a_left, a_right, a_up, a_down, a_jump, a_dash, a_map, a_retry;
static input_state INPUT[MAX_PLAYERS];
static bool g_map_open;
static bool g_attract; /* the title and join screens: four bots play the meadow behind the menu */
static int g_map_sel = -1; /* the flag the map's cursor is on */
static cw_font g_font;
static void title_background(cw_shell_screen scr, cw_vec2 cv, void *user);
static int g_state_dbg, g_players_dbg, g_bubbles_dbg, g_hp_dbg;
static float g_pos_dbg[2], g_cam_dbg[3], g_ppos_dbg[MAX_PLAYERS * 2];
/* the camera as drawn, measured every frame (the camera stability gate): g.view is x, y, z and the shake it carried;
 * g.camgate counts frames at rest (every player still for 2.5 s), at-rest frames where the view moved more than
 * 1 mm, frames in motion, and frames in motion where the view jumped (over 0.6 m) or jerked (its step changed by
 * over 8 cm); g.camjit keeps the largest at-rest step, step in motion and step change seen (metres) and the frame of
 * the first frame in motion that broke the bound (frames since the match started) */
static float g_view_dbg[4], g_camjit_dbg[4];
static int g_camgate_dbg[4];
static struct { float last[3], step[3], ppos[MAX_PLAYERS * 2]; int still, frames; } CS;
static void camgate_reset(void) {
  memset(&CS, 0, sizeof CS);
  memset(g_camgate_dbg, 0, sizeof g_camgate_dbg);
  memset(g_camjit_dbg, 0, sizeof g_camjit_dbg);
}
static void camgate_frame(const float view[3], const float *pp, int np, bool snapped) {
  bool still = CS.frames > 0;
  for (int i = 0; i < np * 2; i++) {
    if (fabsf(pp[i] - CS.ppos[i]) > 1e-5f) still = false;
    CS.ppos[i] = pp[i];
  }
  CS.still = still ? CS.still + 1 : 0;
  float st[3], d2 = 0, j2 = 0;
  for (int k = 0; k < 3; k++) {
    st[k] = view[k] - CS.last[k];
    d2 += st[k] * st[k];
    j2 += (st[k] - CS.step[k]) * (st[k] - CS.step[k]);
  }
  float d = sqrtf(d2), jerk = sqrtf(j2);
  if (CS.frames > 1 && !snapped) {
    if (CS.still >= 150) {
      g_camgate_dbg[0]++;
      if (d > 0.001f) g_camgate_dbg[1]++;
      g_camjit_dbg[0] = fmaxf(g_camjit_dbg[0], d);
    } else {
      g_camgate_dbg[2]++;
      if ((d > 0.6f || jerk > 0.08f) && !g_camgate_dbg[3]++) g_camjit_dbg[3] = (float)CS.frames; /* the first one */
      g_camjit_dbg[1] = fmaxf(g_camjit_dbg[1], d);
      g_camjit_dbg[2] = fmaxf(g_camjit_dbg[2], jerk);
    }
  }
  for (int k = 0; k < 3; k++) CS.step[k] = snapped ? 0 : st[k], CS.last[k] = view[k];
  CS.frames++;
}
static int g_run_ticks;

/* ── generated audio ───────────────────────────────────────────────── */
#define SR 22050
static float *synth(float secs, float (*fn)(float t, float dur, void *u), void *u, int *frames) {
  int n = (int)(secs * SR);
  float *buf = malloc(sizeof(float) * (size_t)n);
  for (int i = 0; i < n; i++) buf[i] = fn((float)i / SR, secs, u);
  *frames = n;
  return buf;
}
typedef struct sp { float f0, f1, noise, decay, vol; int wave; } sp;
static float sfx_fn(float t, float dur, void *u) {
  const sp *s = u;
  float k = t / dur;
  float f = s->f0 + (s->f1 - s->f0) * k;
  static float ph, rs = 1;
  if (t < 1e-4f) ph = 0, rs = 1;
  ph += f / SR;
  ph -= floorf(ph);
  float w = s->wave == 0 ? sinf(ph * 6.2831853f) : s->wave == 1 ? (ph < 0.5f ? 1.0f : -1.0f) : 4.0f * fabsf(ph - 0.5f) - 1.0f;
  rs = rs * 1664525.0f + 1013904223.0f;
  rs = fmodf(rs, 65536.0f);
  float nz = (rs / 32768.0f - 1.0f);
  float env = expf(-s->decay * k) * (k < 0.02f ? k / 0.02f : 1.0f);
  return (w * (1 - s->noise) + nz * s->noise) * env * s->vol;
}
static void audio_init(void) {
  A.on = getenv("COOP_NO_AUDIO") == NULL && cw_audio_sample_rate() > 0;
  if (!A.on) return;
  A.music_bus = cw_shell_music_bus();
  A.sfx_bus = cw_shell_sfx_bus();
  static const sp P[SFX_COUNT] = {
    { 380, 760, 0.0f, 5, 0.35f, 2 }, { 140, 60, 0.4f, 8, 0.4f, 0 }, { 880, 1320, 0.0f, 6, 0.3f, 1 }, { 300, 90, 0.3f, 4, 0.5f, 1 },
    { 200, 500, 0.7f, 5, 0.35f, 0 }, { 300, 900, 0.5f, 4, 0.3f, 2 }, { 440, 1760, 0.0f, 2, 0.35f, 0 }, { 500, 200, 0.2f, 6, 0.45f, 1 },
    { 120, 40, 0.8f, 3, 0.55f, 0 }, { 600, 900, 0.0f, 3, 0.3f, 0 }, { 900, 300, 0.1f, 6, 0.35f, 0 }, { 400, 60, 0.3f, 3, 0.5f, 2 },
    { 523, 1046, 0.0f, 1.2f, 0.4f, 1 }, { 660, 990, 0.0f, 4, 0.3f, 0 }, { 300, 600, 0.3f, 6, 0.3f, 2 },
  };
  static const float DUR[SFX_COUNT] = { 0.18f, 0.12f, 0.16f, 0.3f, 0.35f, 0.18f, 0.7f, 0.15f, 0.5f, 0.3f, 0.25f, 0.5f, 1.4f, 0.4f, 0.15f };
  for (int i = 0; i < SFX_COUNT; i++) {
    int n;
    float *pcm = synth(DUR[i], sfx_fn, (void *)&P[i], &n);
    A.sfx[i] = cw_sound_from_pcm(pcm, n, 1, SR, &(cw_sound_desc){ .bus = A.sfx_bus, .max_voices = 4 });
    free(pcm);
  }
}
static void sfx(int id, float vol) {
  if (A.on) cw_audio_play(A.sfx[id], &(cw_play_desc){ .volume = vol });
}

/* ── lights, environment, post ────────────────────────────────────── */
/* a region's whole look: the key light (colour, strength, direction), a rim light from behind, the hemisphere and the
 * fog (analytic and a volumetric height fog that the key light shines through), exposure and bloom */
typedef struct mood {
  float sky[3], fog[3], sun[3], sun_e, amb, hemi, exposure, bloom, fog_near, fog_far, torch;
  float sun_pitch, sun_yaw, rim[3], rim_e, vden, vamb, vg, vcol[3], grain, ca;
} mood;
static const mood MOODS[BIOMES] = {
  /* meadow: a warm afternoon, blue shade, soft haze, shafts under the trees */
  { { 0.32f, 0.58f, 0.95f }, { 0.62f, 0.76f, 0.95f }, { 1.0f, 0.9f, 0.72f }, 4.4f, 0.5f, 1.1f, 1.0f, 0.1f, 80, 520, 0.0f,
    -0.7f, 0.55f, { 0.55f, 0.75f, 1.0f }, 0.9f, 0.0006f, 0.9f, 0.5f, { 0.85f, 0.92f, 1.0f }, 0.012f, 0.0f },
  /* caves: cold dark, the torches and crystals are the light, a cyan rim */
  { { 0.04f, 0.05f, 0.1f }, { 0.04f, 0.06f, 0.13f }, { 0.4f, 0.55f, 1.0f }, 0.4f, 0.14f, 0.35f, 1.7f, 0.2f, 24, 280, 1.0f,
    -0.9f, 0.2f, { 0.25f, 0.65f, 1.0f }, 1.6f, 0.003f, 0.5f, 0.3f, { 0.3f, 0.5f, 0.9f }, 0.02f, 0.0f },
  /* ruins: green daylight through broken stone, hard shafts */
  { { 0.22f, 0.42f, 0.46f }, { 0.4f, 0.62f, 0.6f }, { 1.0f, 0.95f, 0.78f }, 3.6f, 0.4f, 0.9f, 1.05f, 0.12f, 45, 360, 0.5f,
    -1.0f, -0.45f, { 0.7f, 1.0f, 0.85f }, 1.0f, 0.0012f, 0.7f, 0.4f, { 0.8f, 1.0f, 0.9f }, 0.014f, 0.0f },
  /* volcano: a low red sun behind the cone, lava orange rim, ash haze */
  { { 0.25f, 0.06f, 0.04f }, { 0.4f, 0.12f, 0.06f }, { 1.0f, 0.5f, 0.3f }, 1.8f, 0.28f, 0.6f, 1.25f, 0.24f, 34, 300, 0.8f,
    -0.35f, 0.9f, { 1.0f, 0.38f, 0.1f }, 2.4f, 0.0015f, 0.4f, 0.3f, { 1.0f, 0.5f, 0.3f }, 0.016f, 0.0015f },
};
static mood cur_mood;

static cw_value val_vec4(float x, float y, float z, float w) { cw_value v = { CW_T_VEC4 }; v.v[0] = x; v.v[1] = y; v.v[2] = z; v.v[3] = w; return v; }
static cw_node make_node(const char *type, cw_node parent, const char *name) {
  cw_node n = cw_node_new(type, name);
  cw_node_add_child(parent, n);
  return n;
}
static void particles_cfg(cw_node n, int amount, float life, float smin, float smax, float size, cw_color col, float grav, int blend) {
  cw_node_set(n, "amount", cw_val_int(amount));
  cw_node_set(n, "lifetime", cw_val_float(life));
  cw_node_set(n, "one_shot", cw_val_bool(true));
  cw_node_set(n, "explosiveness", cw_val_float(1.0));
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "speed_min", cw_val_float(smin));
  cw_node_set(n, "speed_max", cw_val_float(smax));
  cw_node_set(n, "spread", cw_val_float(180));
  cw_node_set(n, "size", cw_val_float(size));
  cw_node_set(n, "color", cw_val_color(col.r, col.g, col.b, col.a));
  cw_node_set(n, "gravity", cw_val_vec3(0, grav, 0));
  cw_node_set(n, "blend", cw_val_enum(blend ? "ADD" : "ALPHA"));
}
/* a particle system from one call: blend 0 ALPHA, 1 ADD, 2 OPAQUE (instanced meshes), 3 DISTORT */
static void pfx(cw_node n, int amount, float life, float smin, float smax, float size, cw_color col, float grav, int blend, const char *ramp,
                const char *curve, float energy) {
  particles_cfg(n, amount, life, smin, smax, size, col, grav, blend == 1);
  static const char *const B[4] = { "ALPHA", "ADD", "OPAQUE", "DISTORT" };
  cw_node_set(n, "blend", cw_val_enum(B[blend]));
  cw_node_set(n, "color_ramp", cw_val_string(ramp));
  cw_node_set(n, "size_curve", cw_val_string(curve));
  cw_node_set(n, "energy", cw_val_float(energy));
  cw_node_set(n, "size_random", cw_val_float(0.3f));
}
static void burst(cw_node n, float x, float y, float z) {
  if (!cw_node_valid(n)) return;
  cw_node3d_set_position(n, (t3_vec3){ x, y, z });
  cw_particles_restart(n);
}

static t3_mesh *part(cw_node parent, t3_geometry *g, t3_material *m, float x, float y, float z, float sx, float sy, float sz) {
  cw_node n = cw_node_new("MeshInstance3D", "m");
  cw_node_add_child(parent, n);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.position = (t3_vec3){ x, y, z };
  me->base.scale = (t3_vec3){ sx, sy, sz };
  me->base.cast_shadow = true;
  me->base.receive_shadow = true;
  cw_mesh3d_set_procedural(n, me);
  t3_release(me);
  return me;
}
static t3_material *phys(uint32_t rgb, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(rgb);
  m->roughness = rough;
  m->metalness = metal;
  return m;
}

/* ── decals: one generated sheet (tools/gen-assets.mjs), 4 x 2 cells of 256 px, all MULTIPLY so the light stays right ── */
enum { DC_DIRT, DC_SCORCH, DC_MOSS, DC_CRACK, DC_DAMP, DC_LEAVES, DC_SOOT, DC_CRAZE };
static int g_ndecals;
static float dhash(int x, int y, int s) {
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)s * 83492791u;
  h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
  return (float)(h & 0xffff) / 65535.0f;
}
/* a decal on the face that looks at the camera (down = false: projected along -Z onto it) or on a top (projected down) */
static void decal(float x, float y, float w, float h, int cell, float strength, bool down, float spin) {
  if (g_ndecals >= 220) return;
  g_ndecals++;
  cw_node d = make_node("Decal3D", S.world, "Decal");
  cw_node_set(d, "texture", cw_val_string("decals.png"));
  cw_node_set(d, "region", val_vec4((float)(cell % 4) * 256, (float)(cell / 4) * 256, 256, 256));
  cw_node_set(d, "blend", cw_val_enum("MULTIPLY"));
  cw_node_set(d, "color", cw_val_color(1, 1, 1, strength));
  cw_node_set(d, "edge_fade", cw_val_float(0.3f));
  cw_node_set(d, "normal_fade", cw_val_float(0.5f));
  if (down) {
    cw_node_set(d, "size", cw_val_vec3(w, h, 0.5f));
    cw_node3d_set_position(d, (t3_vec3){ x, y + 0.12f, 0 });
    cw_node3d_set_rotation(d, (t3_vec3){ -1.5708f, spin, 0 });
  } else {
    cw_node_set(d, "size", cw_val_vec3(w, h, 0.7f));
    cw_node3d_set_position(d, (t3_vec3){ x, y, 1.75f });
    cw_node3d_set_rotation(d, (t3_vec3){ 0, 0, spin });
  }
}
/* dirt where the turf meets the cliff, damp runs under the ruin ledges, moss, cracks on big walls, soot over the lava,
 * scorched and crazed ground beside it, leaf litter on the meadow paths */
static void place_decals(const level *L) {
  g_ndecals = 0;
  for (int x = 1; x < WORLD_W - 1; x++) {
    int bi = L->biome[x];
    for (int y = 1; y < WORLD_H - 1; y++) {
      int t = L->t[y][x];
      if (!(t == T_ROCK || t == T_GRASS || t == T_BRICK)) continue;
      bool top = !world_solid(L, x, y + 1);
      float r = dhash(x, y, 900), fx = (float)x + 0.5f, fy = (float)y + 0.5f, spin = (dhash(x, y, 901) - 0.5f) * 0.6f;
      bool lava_near = false;
      for (int k = -4; k <= 4; k++)
        if (x + k >= 0 && x + k < WORLD_W && L->t[2][x + k] == T_LAVA) lava_near = true;
      if (top) {
        if (bi == BIOME_MEADOW && r < 0.16f) decal(fx, fy - 0.15f, 2.2f, 1.1f, DC_DIRT, 0.75f, false, spin);
        else if (bi == BIOME_RUINS && r < 0.22f) decal(fx, fy - 0.9f, 1.4f, 2.0f, DC_DAMP, 0.7f, false, 0);
        else if (bi == BIOME_CAVES && r < 0.14f) decal(fx, fy - 0.9f, 1.3f, 2.0f, DC_DAMP, 0.6f, false, 0);
        if (bi == BIOME_MEADOW && r > 0.9f) decal(fx, (float)y + 1.0f, 1.6f, 1.6f, DC_LEAVES, 0.8f, true, spin * 4);
        if (bi == BIOME_VOLCANO && lava_near && r > 0.55f) decal(fx, (float)y + 1.0f, 2.0f, 2.0f, DC_SCORCH, 0.85f, true, spin * 4);
        else if (bi == BIOME_VOLCANO && r > 0.8f) decal(fx, (float)y + 1.0f, 2.2f, 2.2f, DC_CRAZE, 0.7f, true, spin * 4);
        continue;
      }
      if (y < L->ground[x] - 7 && !(bi == BIOME_VOLCANO && lava_near)) continue; /* deep rock under the surface is seldom on screen */
      if (bi == BIOME_VOLCANO && lava_near && y < 9 && r < 0.12f) decal(fx, fy + 0.6f, 1.6f, 2.6f, DC_SOOT, 0.75f, false, 0);
      else if ((bi == BIOME_RUINS || bi == BIOME_MEADOW) && r < 0.035f) decal(fx, fy, 1.8f, 1.8f, DC_MOSS, 0.6f, false, spin);
      else if (r > 0.96f) decal(fx, fy, 1.8f, 1.8f, DC_CRACK, 0.7f, false, spin * 3);
    }
  }
}

static void build_scene(void) {
  game *g = &G;
  cw_tree *tree = cw_scene_app_tree();
  S.root = cw_tree_root(tree);
  /* free whatever a previous match built */
  if (cw_node_valid(S.world)) cw_node_free(S.world);
  S.world = make_node("Node3D", S.root, "World");
  S.nlight_src = 0;
  world_scene_build(g, S.world);
  world_backdrop(g);
  place_decals(&g->L);
  cw_node wobj = S.world;
  const level *L = &g->L;
  /* environment, sun, post, camera: once */
  if (!S.built) {
    S.env = make_node("WorldEnvironment", S.root, "Env");
    S.sun = make_node("DirectionalLight3D", S.root, "Sun");
    cw_node_set(S.sun, "shadow", cw_val_bool(true));
    cw_node_set(S.sun, "shadow_size", cw_val_int(2048));
    cw_node_set(S.sun, "shadow_extent", cw_val_float(26));
    cw_node_set(S.sun, "shadow_far", cw_val_float(90));
    cw_node_set(S.sun, "shadow_bias", cw_val_float(-0.0006));
    cw_node_set(S.sun, "fog_energy", cw_val_float(2.0));
    cw_node3d_set_rotation(S.sun, (t3_vec3){ -0.9f, 0.35f, 0 });
    /* the rim light: from behind and the side, no shadow, so a character's edge catches colour */
    S.rim = make_node("DirectionalLight3D", S.root, "Rim");
    cw_node_set(S.rim, "energy", cw_val_float(1.0));
    cw_node_set(S.rim, "fog_energy", cw_val_float(0.0));
    cw_node3d_set_rotation(S.rim, (t3_vec3){ -0.35f, 3.14159f + 0.6f, 0 });
    /* a height fog the lights shine through (shafts), a few fog volumes for water, lava and mist */
    if (!getenv("COOP_NOFOG")) {
      S.fog = make_node("HeightFog", S.root, "Fog");
      cw_node_set(S.fog, "use_sky_color", cw_val_bool(false));
      cw_node_set(S.fog, "volumetric", cw_val_bool(true));
      cw_node_set(S.fog, "volumetric_distance", cw_val_float(60));
      cw_node_set(S.fog, "height_falloff", cw_val_float(0.02));
      for (int i = 0; i < 8; i++) {
        S.fvol[i] = make_node("FogVolume3D", S.root, "Volume");
        cw_node_set(S.fvol[i], "density", cw_val_float(0));
        cw_node_set(S.fvol[i], "noise", cw_val_float(0.6));
        cw_node_set(S.fvol[i], "noise_scale", cw_val_float(0.35));
        cw_node_set(S.fvol[i], "edge_fade", cw_val_float(0.35));
      }
    }
    S.post = make_node("PostProcess", S.root, "Post");
    cw_node_set(S.post, "tonemap", cw_val_enum("ACES_FILMIC"));
    cw_node_set(S.post, "bloom", cw_val_bool(true));
    cw_node_set(S.post, "bloom_intensity", cw_val_float(0.08));
    cw_node_set(S.post, "bloom_threshold", cw_val_float(0.9));
    cw_node_set(S.post, "ssao", cw_val_bool(true));
    cw_node_set(S.post, "ssao_radius", cw_val_float(0.8));
    cw_node_set(S.post, "vignette", cw_val_bool(true));
    cw_node_set(S.post, "vignette_intensity", cw_val_float(0.4));
    cw_node_set(S.post, "msaa", cw_val_int(4));
    cw_node_set(S.post, "grain", cw_val_bool(true));
    cw_node_set(S.post, "grain_intensity", cw_val_float(0.012));
    cw_node_set(S.post, "chromatic_aberration", cw_val_bool(true));
    cw_node_set(S.post, "ca_intensity", cw_val_float(0.0015));
    cw_node_set(S.post, "bloom_radius", cw_val_float(0.8));
    S.cam = make_node("Camera3D", S.root, "Camera");
    cw_node_set(S.cam, "fov", cw_val_float(40));
    cw_node_set(S.cam, "near", cw_val_float(0.5));
    cw_node_set(S.cam, "far", cw_val_float(400));
    cw_node_set(S.cam, "current", cw_val_bool(true));
    for (int i = 0; i < NTORCH; i++) {
      S.torch[i] = make_node("OmniLight3D", S.root, "Torch");
      cw_node_set(S.torch[i], "range", cw_val_float(14));
      cw_node_set(S.torch[i], "decay", cw_val_float(2));
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
      S.plight[i] = make_node("OmniLight3D", S.root, "PlayerLight");
      cw_node_set(S.plight[i], "range", cw_val_float(8));
      cw_node_set(S.plight[i], "energy", cw_val_float(0));
      cw_node_set(S.plight[i], "color", cw_val_color(PLAYER_RGB[i][0], PLAYER_RGB[i][1], PLAYER_RGB[i][2], 1));
      cw_node_set(S.plight[i], "fog_energy", cw_val_float(0));
    }
    S.built = true;
  }
  /* light sources: torches along the caves and ruins, fire over the lava */
  for (int x = 6; x < WORLD_W; x += 11) {
    int b = L->biome[x];
    if (b == BIOME_CAVES || b == BIOME_RUINS) {
      if (L->ground[x] > 2 && S.nlight_src < 60) {
        int k = S.nlight_src++;
        S.light_x[k] = (float)x + 0.5f, S.light_y[k] = (float)L->ground[x] + 3.2f;
        S.light_r[k] = 1.0f, S.light_g[k] = b == BIOME_CAVES ? 0.62f : 0.8f, S.light_b[k] = b == BIOME_CAVES ? 0.3f : 0.55f;
        S.light_e[k] = 14.0f, S.light_range[k] = 14;
        /* a torch on the wall: a small emissive flame and a bracket */
        t3_material *fm = phys(0x1a0800, 0.5f, 0);
        fm->emissive = (t3_color){ 1.0f, 0.45f, 0.1f }, fm->emissive_intensity = 4.0f;
        t3_geometry *fg = t3_sphere_geometry_new(0.16f, 10, 8);
        part(wobj, fg, fm, S.light_x[k], S.light_y[k], -1.4f, 1, 1.5f, 1);
        t3_release(fg);
        t3_geometry *bg = t3_cylinder_geometry_new(0.05f, 0.07f, 0.7f, 6, 1, false);
        part(wobj, bg, phys(0x3a2a1a, 0.6f, 0.2f), S.light_x[k], S.light_y[k] - 0.4f, -1.4f, 1, 1, 1);
        t3_release(bg);
      }
    }
  }
  for (int x = 0; x < WORLD_W; x += 6) {
    if (L->t[2][x] == T_LAVA && S.nlight_src < 62) {
      int k = S.nlight_src++;
      S.light_x[k] = (float)x + 0.5f, S.light_y[k] = 3.6f;
      S.light_r[k] = 1.0f, S.light_g[k] = 0.32f, S.light_b[k] = 0.06f;
      S.light_e[k] = 20.0f, S.light_range[k] = 16;
    }
  }
  /* pickups: glowing crystals */
  for (int i = 0; i < L->npickups; i++) {
    const pickup *pk = &L->pickups[i];
    S.pick_node[i] = make_node("Node3D", S.world, "Pickup");
    cw_node po = S.pick_node[i];
    static const uint32_t PC[4] = { 0x55ddff, 0xaa66ff, 0x66ff99, 0xffaa33 };
    t3_material *m = phys(PC[pk->ability & 3], 0.15f, 0.1f);
    m->emissive = (t3_color){ 0.4f, 0.7f, 1.0f }, m->emissive_intensity = 2.5f;
    t3_geometry *cg = t3_cone_geometry_new(0.35f, 0.7f, 6, 1, false);
    part(po, cg, m, 0, 0.35f, 0, 1, 1, 1);
    part(po, cg, m, 0, -0.35f, 0, 1, -1, 1);
    t3_release(cg);
    if (S.nlight_src < 64) {
      int k = S.nlight_src++;
      S.light_x[k] = (float)pk->x + 0.5f, S.light_y[k] = (float)pk->y + 1.2f;
      S.light_r[k] = 0.4f, S.light_g[k] = 0.7f, S.light_b[k] = 1.0f, S.light_e[k] = 10.0f, S.light_range[k] = 9;
    }
  }
  /* coins */
  t3_geometry *cg = t3_cylinder_geometry_new(0.28f, 0.28f, 0.07f, 16, 1, false);
  t3_material *cm = phys(0xffc83a, 0.25f, 1.0f);
  cm->emissive = (t3_color){ 0.5f, 0.3f, 0.0f }, cm->emissive_intensity = 0.8f;
  for (int i = 0; i < L->ncoins; i++) {
    S.coin_node[i] = make_node("Node3D", S.world, "Coin");
    S.coin_mesh[i] = part(S.coin_node[i], cg, cm, 0, 0, 0, 1, 1, 1);
    S.coin_mesh[i]->base.rotation.x = 1.5708f;
    cw_node3d_set_position(S.coin_node[i], (t3_vec3){ L->coins[i].x, L->coins[i].y, 0 });
  }
  t3_release(cg);
  /* enemies: skinned models, posed in sync_scene */
  for (int i = 0; i < L->nenemies; i++) {
    const enemy *e = &L->enemies[i];
    S.enode[i] = make_node("Node3D", S.world, "Enemy");
    S.emodel[i] = make_node("Node3D", S.enode[i], "Model");
    rig_free(S.erig[i]);
    S.erig[i] = rig_load(e->kind == 2 ? RIG_BOSS : e->kind == 1 ? RIG_FLYER : RIG_WALKER);
    if (S.erig[i]) rig_attach(S.erig[i], S.emodel[i]);
    float sc = e->kind == 2 ? 1.25f : e->kind == 1 ? 0.85f : 0.95f;
    cw_node3d_set_scale(S.emodel[i], (t3_vec3){ sc, sc, sc });
    S.ephase[i] = (float)i * 1.7f;
  }
  /* moving platforms */
  for (int i = 0; i < L->nplat; i++) {
    t3_geometry *pg = t3_box_geometry_new(L->plats[i].w, 0.4f, 2.4f, 1, 1, 1);
    S.plat_mesh[i] = part(wobj, pg, phys(0x8a7a5a, 0.5f, 0.5f), 0, 0, 0, 1, 1, 1);
    t3_release(pg);
  }
  /* characters: skinned heroes, one headgear each (a marker that is not a colour) */
  t3_geometry *bub = t3_sphere_geometry_new(1.0f, 32, 20);
  static const float ACCENT[MAX_PLAYERS][3] = { { 1.0f, 0.85f, 0.25f }, { 0.95f, 0.95f, 0.95f }, { 1.0f, 0.45f, 0.65f }, { 0.3f, 0.8f, 0.95f } };
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (cw_node_valid(S.pnode[i])) cw_node_free(S.pnode[i]);
    S.pnode[i] = make_node("Node3D", S.root, "Player");
    cw_node po = S.pnode[i];
    S.pmodel[i] = make_node("Node3D", po, "Model");
    rig_free(S.prig[i]);
    S.prig[i] = rig_load(RIG_HERO);
    if (S.prig[i]) {
      rig_attach(S.prig[i], S.pmodel[i]);
      float fur[3] = { PLAYER_RGB[i][0], PLAYER_RGB[i][1], PLAYER_RGB[i][2] };
      rig_tint(S.prig[i], fur, ACCENT[i], i);
    }
    t3_material *bu = phys(0xaaddff, 0.05f, 0);
    bu->transparent = true, bu->opacity = 0.3f, bu->depth_write = false;
    S.bubble[i] = part(po, bub, bu, 0, 0.9f, 0, 1, 1, 1);
    S.bubble[i]->base.visible = false;
    S.bubble[i]->base.cast_shadow = false;
    cw_node3d_set_position(S.pnode[i], (t3_vec3){ 0, -100, 0 });
  }
  t3_release(bub);
  /* water: a Water3D surface (Gerstner waves, a ripple normal map, sky reflection) over each pool, and a volume of
   * tinted fog inside it. Lava spans are listed for the glow volumes and the heat haze. */
  {
    float sp[16][4];
    S.nwspan = world_water_spans(sp, 16);
    for (int i = 0; i < S.nwspan; i++) {
      memcpy(S.wspan[i], sp[i], sizeof sp[i]);
      if (cw_node_valid(S.water3d[i])) cw_node_free(S.water3d[i]);
      S.water3d[i] = make_node("Water3D", S.world, "Pool");
      cw_node w = S.water3d[i];
      cw_node_set(w, "size", cw_val_vec2(sp[i][1] - sp[i][0], 2.9f));
      cw_node_set(w, "subdivisions", cw_val_int(96));
      cw_node_set(w, "wave_count", cw_val_int(2));
      cw_node_set(w, "wave0", val_vec4(90, 3.2f, 0.06f, 0.4f));
      cw_node_set(w, "wave1", val_vec4(0, 1.7f, 0.03f, 0.3f));
      cw_node_set(w, "color", cw_val_color(0.12f, 0.42f, 0.5f, 1));
      cw_node_set(w, "deep_color", cw_val_color(0.01f, 0.08f, 0.16f, 1));
      cw_node_set(w, "reflection", cw_val_bool(true));
      cw_node_set(w, "refraction", cw_val_bool(false));
      cw_node_set(w, "resolution_scale", cw_val_float(0.5f));
      cw_node_set(w, "flow_direction", cw_val_vec2(1, 0));
      cw_node_set(w, "flow_speed", cw_val_float(0.05f));
      cw_node3d_set_position(w, (t3_vec3){ (sp[i][0] + sp[i][1]) * 0.5f, sp[i][3] - 0.08f, 0 });
    }
    S.nlspan = world_lava_spans(sp, 16);
    for (int i = 0; i < S.nlspan; i++) memcpy(S.lspan[i], sp[i], sizeof sp[i]);
  }
  /* effects */
  if (!cw_node_valid(S.fx_spark)) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
      S.fx_dust[i] = make_node("GPUParticles3D", S.root, "Dust");
      pfx(S.fx_dust[i], 28, 0.7f, 1.0f, 3.2f, 0.4f, cw_rgba(0.85f, 0.78f, 0.66f, 0.6f), -0.5f, 0, "0 1 0.95 0.85 0.55, 1 0.8 0.75 0.65 0", "0 0.4, 0.4 1, 1 1.4", 1.0f);
      cw_node_set(S.fx_dust[i], "spread", cw_val_float(70));
      cw_node_set(S.fx_dust[i], "direction", cw_val_vec3(0, 1, 0));
      cw_node_set(S.fx_dust[i], "soft_distance", cw_val_float(0.5f));
    }
    S.fx_spark = make_node("GPUParticles3D", S.root, "Spark");
    pfx(S.fx_spark, 36, 0.7f, 2.0f, 6.5f, 0.1f, cw_rgba(1, 0.8f, 0.25f, 1), -8.0f, 1, "0 1 1 0.8 1, 0.5 1 0.7 0.2 1, 1 1 0.4 0.1 0", "0 1, 1 0.2", 5.0f);
    cw_node_set(S.fx_spark, "draw_mode", cw_val_enum("STRETCHED"));
    S.fx_splash = make_node("GPUParticles3D", S.root, "Splash");
    pfx(S.fx_splash, 90, 1.0f, 3.0f, 9.0f, 0.14f, cw_rgba(0.75f, 0.92f, 1, 0.9f), -16.0f, 1, "0 1 1 1 0.9, 0.7 0.7 0.9 1 0.6, 1 0.6 0.85 1 0", "0 1, 1 0.4", 1.6f);
    cw_node_set(S.fx_splash, "draw_mode", cw_val_enum("STRETCHED"));
    cw_node_set(S.fx_splash, "spread", cw_val_float(38));
    cw_node_set(S.fx_splash, "direction", cw_val_vec3(0, 1, 0));
    S.fx_foam = make_node("GPUParticles3D", S.root, "Foam");
    pfx(S.fx_foam, 36, 1.4f, 0.4f, 1.8f, 0.5f, cw_rgba(0.9f, 0.97f, 1, 0.7f), -0.2f, 0, "0 1 1 1 0.7, 1 0.9 1 1 0", "0 0.5, 0.3 1, 1 1.3", 1.0f);
    cw_node_set(S.fx_foam, "spread", cw_val_float(85));
    cw_node_set(S.fx_foam, "direction", cw_val_vec3(0, 1, 0));
    S.fx_bubbles = make_node("GPUParticles3D", S.root, "Bubbles");
    pfx(S.fx_bubbles, 30, 1.2f, 0.8f, 2.2f, 0.09f, cw_rgba(0.8f, 0.95f, 1, 0.8f), 2.0f, 1, "0 1 1 1 0.9, 1 0.8 0.95 1 0", "0 0.6, 0.2 1, 1 1", 1.4f);
    S.fx_burst = make_node("GPUParticles3D", S.root, "Burst");
    pfx(S.fx_burst, 90, 0.9f, 3.0f, 10.0f, 0.3f, cw_rgba(1, 0.55f, 0.2f, 1), -6.0f, 1, "0 1 0.9 0.5 1, 0.4 1 0.4 0.1 0.8, 1 0.4 0.1 0.05 0", "0 0.5, 0.3 1, 1 1.5", 3.0f);
    S.fx_hit = make_node("GPUParticles3D", S.root, "Hit");
    pfx(S.fx_hit, 48, 0.6f, 3.0f, 10.0f, 0.12f, cw_rgba(1, 1, 0.8f, 1), -5.0f, 1, "0 1 1 0.9 1, 1 1 0.5 0.2 0", "0 1, 1 0.1", 5.0f);
    cw_node_set(S.fx_hit, "draw_mode", cw_val_enum("STRETCHED"));
    /* rock debris: small instanced boxes that fall and bounce on what the camera sees */
    S.fx_debris = make_node("GPUParticles3D", S.root, "Debris");
    pfx(S.fx_debris, 24, 1.8f, 3.0f, 9.0f, 0.12f, cw_rgba(0.55f, 0.42f, 0.32f, 1), -20.0f, 2, "", "", 1.0f);
    cw_node_set(S.fx_debris, "draw_mode", cw_val_enum("MESH"));
    cw_node_set(S.fx_debris, "mesh_primitive", cw_val_enum("BOX"));
    cw_node_set(S.fx_debris, "spin", cw_val_float(6));
    cw_node_set(S.fx_debris, "size_random", cw_val_float(0.6f));
    /* the death blast of an enemy: flash, smoke, sparks, and a ring that bends the picture */
    S.fx_boom = make_node("GPUParticles3D", S.root, "Boom");
    pfx(S.fx_boom, 40, 0.55f, 0.5f, 3.0f, 1.3f, cw_rgba(1, 0.7f, 0.3f, 1), 0.0f, 1, "0 1 0.95 0.7 1, 0.2 1 0.55 0.15 0.9, 1 0.35 0.08 0.03 0", "0 0.4, 0.3 1, 1 1.5", 6.0f);
    S.fx_boomsmoke = make_node("GPUParticles3D", S.root, "BoomSmoke");
    pfx(S.fx_boomsmoke, 36, 1.5f, 0.6f, 2.5f, 1.1f, cw_rgba(0.3f, 0.27f, 0.25f, 0.8f), 1.0f, 0, "0 0.3 0.26 0.24 0, 0.15 0.28 0.25 0.23 0.8, 1 0.4 0.38 0.36 0", "0 0.5, 0.5 1, 1 1.8", 1.0f);
    cw_node_set(S.fx_boomsmoke, "soft_distance", cw_val_float(0.6f));
    S.fx_boomspark = make_node("GPUParticles3D", S.root, "BoomSparks");
    pfx(S.fx_boomspark, 90, 1.1f, 4.0f, 14.0f, 0.1f, cw_rgba(1, 0.8f, 0.4f, 1), -9.0f, 1, "0 1 0.95 0.6 1, 0.5 1 0.5 0.1 1, 1 0.6 0.1 0.05 0", "0 1, 1 0.2", 6.0f);
    cw_node_set(S.fx_boomspark, "draw_mode", cw_val_enum("STRETCHED"));
    S.fx_wave = make_node("Shockwave3D", S.root, "Wave");
    cw_node_set(S.fx_wave, "autostart", cw_val_bool(false));
    cw_node_set(S.fx_wave, "max_radius", cw_val_float(4.5f));
    cw_node_set(S.fx_wave, "duration", cw_val_float(0.6f));
    cw_node_set(S.fx_wave, "thickness", cw_val_float(0.8f));
    cw_node_set(S.fx_wave, "strength", cw_val_float(0.45f));
    /* ambient motes that follow the camera: fireflies, cave dust, pollen, ash */
    S.fx_motes = make_node("GPUParticles3D", S.root, "Motes");
    pfx(S.fx_motes, 70, 5.0f, 0.1f, 0.5f, 0.07f, cw_rgba(1, 1, 0.6f, 1), 0.0f, 1, "0 1 1 1 0, 0.2 1 1 1 1, 0.8 1 1 1 1, 1 1 1 1 0", "0 0.3, 0.5 1, 1 0.3", 3.0f);
    cw_node_set(S.fx_motes, "one_shot", cw_val_bool(false));
    cw_node_set(S.fx_motes, "explosiveness", cw_val_float(0));
    cw_node_set(S.fx_motes, "emitting", cw_val_bool(true));
    cw_node_set(S.fx_motes, "emission_shape", cw_val_enum("BOX"));
    cw_node_set(S.fx_motes, "emission_extents", cw_val_vec3(22, 9, 3));
    cw_node_set(S.fx_motes, "spread", cw_val_float(180));
    cw_node_set(S.fx_motes, "turbulence", cw_val_float(0.5f));
    cw_node_set(S.fx_motes, "preprocess", cw_val_float(3));
    /* lava: embers over every span, a heat haze above, bubbles popping on the surface */
    for (int i = 0; i < 4; i++) {
      S.fx_haze[i] = make_node("GPUParticles3D", S.root, "Haze");
      pfx(S.fx_haze[i], 14, 2.4f, 0.4f, 1.1f, 2.2f, cw_rgba(1, 1, 1, 1), 0.0f, 3, "0 1 1 1 0, 0.3 1 1 1 0.8, 1 1 1 1 0", "0 0.6, 1 1.4", 1.0f);
      cw_node_set(S.fx_haze[i], "distort_strength", cw_val_float(0.10f));
      cw_node_set(S.fx_haze[i], "one_shot", cw_val_bool(false));
      cw_node_set(S.fx_haze[i], "explosiveness", cw_val_float(0));
      cw_node_set(S.fx_haze[i], "emitting", cw_val_bool(false));
      cw_node_set(S.fx_haze[i], "emission_shape", cw_val_enum("BOX"));
      cw_node_set(S.fx_haze[i], "spread", cw_val_float(12));
      cw_node_set(S.fx_haze[i], "direction", cw_val_vec3(0, 1, 0));
      S.fx_lavapop[i] = make_node("GPUParticles3D", S.root, "LavaPop");
      pfx(S.fx_lavapop[i], 40, 1.4f, 2.5f, 7.0f, 0.2f, cw_rgba(1, 0.5f, 0.1f, 1), -9.0f, 1, "0 1 0.8 0.3 1, 0.5 1 0.4 0.08 1, 1 0.5 0.1 0.03 0", "0 1, 1 0.3", 4.0f);
      cw_node_set(S.fx_lavapop[i], "one_shot", cw_val_bool(false));
      cw_node_set(S.fx_lavapop[i], "explosiveness", cw_val_float(0));
      cw_node_set(S.fx_lavapop[i], "emitting", cw_val_bool(false));
      cw_node_set(S.fx_lavapop[i], "emission_shape", cw_val_enum("BOX"));
      cw_node_set(S.fx_lavapop[i], "spread", cw_val_float(25));
      cw_node_set(S.fx_lavapop[i], "direction", cw_val_vec3(0, 1, 0));
      cw_node_set(S.fx_lavapop[i], "draw_mode", cw_val_enum("STRETCHED"));
    }
    for (int i = 0; i < 4; i++) {
      cw_node n = make_node("GPUParticles3D", S.root, "Ember");
      pfx(n, 70, 2.8f, 0.8f, 3.0f, 0.1f, cw_rgba(1, 0.5f, 0.1f, 1), 0.8f, 1, "0 1 0.7 0.2 0, 0.15 1 0.55 0.1 1, 1 0.5 0.1 0.03 0", "0 1, 1 0.3", 4.0f);
      cw_node_set(n, "one_shot", cw_val_bool(false));
      cw_node_set(n, "explosiveness", cw_val_float(0));
      cw_node_set(n, "emitting", cw_val_bool(false));
      cw_node_set(n, "emission_shape", cw_val_enum("BOX"));
      cw_node_set(n, "spread", cw_val_float(30));
      cw_node_set(n, "turbulence", cw_val_float(0.4f));
      cw_node_set(n, "direction", cw_val_vec3(0, 1, 0));
      S.fx_ember[i] = n;
    }
    S.nember = 4;
    /* a real flame at the nearest torches */
    for (int i = 0; i < NTORCH; i++) {
      S.torchfire[i] = make_node("GPUParticles3D", S.torch[i], "Flame");
      pfx(S.torchfire[i], 40, 0.55f, 0.3f, 0.9f, 0.2f, cw_rgba(1, 0.6f, 0.2f, 1), 2.2f, 1, "0 1 0.95 0.6 0, 0.15 1 0.7 0.25 1, 0.7 1 0.35 0.08 0.6, 1 0.4 0.1 0.03 0", "0 0.6, 0.3 1, 1 0.2", 4.5f);
      cw_node_set(S.torchfire[i], "one_shot", cw_val_bool(false));
      cw_node_set(S.torchfire[i], "explosiveness", cw_val_float(0));
      cw_node_set(S.torchfire[i], "emitting", cw_val_bool(true));
      cw_node_set(S.torchfire[i], "emission_shape", cw_val_enum("SPHERE"));
      cw_node_set(S.torchfire[i], "emission_radius", cw_val_float(0.1f));
      cw_node_set(S.torchfire[i], "spread", cw_val_float(14));
      cw_node_set(S.torchfire[i], "direction", cw_val_vec3(0, 1, 0));
      cw_node_set(S.torchfire[i], "local_coords", cw_val_bool(false));
    }
  }
  /* enemies seen alive: a death is a blast */
  for (int i = 0; i < L->nenemies; i++) S.prev_alive[i] = L->enemies[i].alive;
}

static void apply_mood(float x) {
  int xi = (int)x < 0 ? 0 : (int)x >= WORLD_W ? WORLD_W - 1 : (int)x;
  int b = G.L.biome[xi];
  const mood *m = &MOODS[b];
  float k = 0.04f;
  float *c = (float *)&cur_mood;
  const float *t = (const float *)m;
  for (size_t i = 0; i < sizeof(mood) / sizeof(float); i++) c[i] += (t[i] - c[i]) * k;
  const mood *q = &cur_mood;
  cw_node_set(S.env, "background", cw_val_color(q->fog[0], q->fog[1], q->fog[2], 1));
  cw_node_set(S.env, "sky_color", cw_val_color(q->sky[0], q->sky[1], q->sky[2], 1));
  cw_node_set(S.env, "ground_color", cw_val_color(q->sky[0] * 0.3f, q->sky[1] * 0.25f, q->sky[2] * 0.2f, 1));
  cw_node_set(S.env, "hemi_energy", cw_val_float(q->hemi));
  cw_node_set(S.env, "ambient_energy", cw_val_float(q->amb));
  cw_node_set(S.env, "fog_mode", cw_val_enum("LINEAR"));
  cw_node_set(S.env, "fog_color", cw_val_color(q->fog[0], q->fog[1], q->fog[2], 1));
  cw_node_set(S.env, "fog_near", cw_val_float(q->fog_near));
  cw_node_set(S.env, "fog_far", cw_val_float(q->fog_far));
  cw_node_set(S.sun, "color", cw_val_color(q->sun[0], q->sun[1], q->sun[2], 1));
  cw_node_set(S.sun, "energy", cw_val_float(q->sun_e));
  cw_node3d_set_rotation(S.sun, (t3_vec3){ q->sun_pitch, q->sun_yaw, 0 });
  cw_node_set(S.rim, "color", cw_val_color(q->rim[0], q->rim[1], q->rim[2], 1));
  cw_node_set(S.rim, "energy", cw_val_float(q->rim_e));
  cw_node_set(S.post, "exposure", cw_val_float(q->exposure));
  cw_node_set(S.post, "bloom_intensity", cw_val_float(q->bloom));
  cw_node_set(S.post, "grain_intensity", cw_val_float(q->grain));
  cw_node_set(S.post, "ca_intensity", cw_val_float(q->ca));
  /* a colour grade per region (assets/grade/*.cube, tools/gen-assets.mjs): when the region changes the grade fades out,
   * the next LUT is loaded and it fades back in, so a border never snaps */
  {
    static const char *const LUTS[BIOMES] = { "grade/meadow.cube", "grade/caves.cube", "grade/ruins.cube", "grade/volcano.cube" };
    static int lut_b = -1;
    static float gk = 0;
    if (b != lut_b) {
      gk -= 0.04f;
      if (gk <= 0 || lut_b < 0) gk = 0, lut_b = b, cw_node_set(S.post, "lut", cw_val_string(LUTS[b]));
    } else gk += (0.9f - gk) * 0.04f;
    cw_node_set(S.post, "grade", cw_val_bool(true));
    cw_node_set(S.post, "grade_intensity", cw_val_float(gk));
  }
  if (cw_node_valid(S.fog)) {
    cw_node_set(S.fog, "density", cw_val_float(q->vden));
    cw_node_set(S.fog, "color", cw_val_color(q->vcol[0], q->vcol[1], q->vcol[2], 1));
    cw_node_set(S.fog, "volumetric_ambient", cw_val_float(q->vamb));
    cw_node_set(S.fog, "volumetric_albedo", cw_val_color(q->vcol[0], q->vcol[1], q->vcol[2], 1));
    cw_node_set(S.fog, "sun_g", cw_val_float(q->vg));
  }
}

/* the nearest water and lava spans get the fog volumes (water tint, lava glow) and the lava its haze, embers and pops */
static void update_spans(float camx) {
  int vi = 0;
  for (int pass = 0; pass < 2; pass++) {
    int n = pass == 0 ? S.nwspan : S.nlspan;
    for (int i = 0; i < n && vi < 8; i++) {
      const float *sp = pass == 0 ? S.wspan[i] : S.lspan[i];
      float cx = (sp[0] + sp[1]) * 0.5f;
      if (fabsf(cx - camx) > 46 + (sp[1] - sp[0]) * 0.5f) continue;
      cw_node v = S.fvol[vi++];
      if (!cw_node_valid(v)) continue;
      float w = sp[1] - sp[0];
      if (pass == 0) {
        cw_node_set(v, "size", cw_val_vec3(w, sp[3] - sp[2], 3.6f));
        cw_node3d_set_position(v, (t3_vec3){ cx, (sp[2] + sp[3]) * 0.5f, 0 });
        cw_node_set(v, "density", cw_val_float(0.22));
        cw_node_set(v, "albedo", cw_val_color(0.03f, 0.22f, 0.33f, 1));
        cw_node_set(v, "emission", cw_val_color(0.01f, 0.07f, 0.12f, 1));
        cw_node_set(v, "emission_energy", cw_val_float(0.35));
        cw_node_set(v, "noise_wind", cw_val_vec3(0.15f, 0.05f, 0));
      } else {
        cw_node_set(v, "size", cw_val_vec3(w + 6, 8, 5));
        cw_node3d_set_position(v, (t3_vec3){ cx, 5.5f, 0 });
        cw_node_set(v, "density", cw_val_float(0.3));
        cw_node_set(v, "albedo", cw_val_color(1.0f, 0.4f, 0.12f, 1));
        cw_node_set(v, "emission", cw_val_color(1.0f, 0.3f, 0.05f, 1));
        cw_node_set(v, "emission_energy", cw_val_float(1.6 + 0.3 * sinf(S.time * 1.7f)));
        cw_node_set(v, "height_falloff", cw_val_float(0.25));
        cw_node_set(v, "noise_wind", cw_val_vec3(0.1f, 0.5f, 0));
      }
    }
  }
  for (; vi < 8; vi++)
    if (cw_node_valid(S.fvol[vi])) cw_node_set(S.fvol[vi], "density", cw_val_float(0));
  /* lava effects ride the nearest four spans (configured when a slot changes span: setting amount restarts a system) */
  static int slot_span[4] = { -2, -2, -2, -2 };
  int nl = 0;
  for (int i = 0; i < S.nlspan && nl < 4; i++) {
    const float *sp = S.lspan[i];
    float cx = (sp[0] + sp[1]) * 0.5f;
    if (fabsf(cx - camx) > 40) continue;
    float w = sp[1] - sp[0];
    cw_node h = S.fx_haze[nl], pop = S.fx_lavapop[nl], em = S.fx_ember[nl];
    if (slot_span[nl] != i) {
      slot_span[nl] = i;
      cw_node3d_set_position(h, (t3_vec3){ cx, 4.4f, 1.0f });
      cw_node_set(h, "emission_extents", cw_val_vec3(w * 0.5f, 0.3f, 1.2f));
      cw_node_set(h, "emitting", cw_val_bool(true));
      cw_node3d_set_position(pop, (t3_vec3){ cx, 2.9f, 0.5f });
      cw_node_set(pop, "emission_extents", cw_val_vec3(w * 0.5f, 0.1f, 1.2f));
      cw_node_set(pop, "emitting", cw_val_bool(true));
      cw_node3d_set_position(em, (t3_vec3){ cx, 3.2f, 0.5f });
      cw_node_set(em, "emission_extents", cw_val_vec3(w * 0.5f, 0.2f, 1.0f));
      cw_node_set(em, "emitting", cw_val_bool(true));
    }
    nl++;
  }
  for (; nl < 4; nl++)
    if (slot_span[nl] != -1) {
      slot_span[nl] = -1;
      cw_node_set(S.fx_haze[nl], "emitting", cw_val_bool(false));
      cw_node_set(S.fx_lavapop[nl], "emitting", cw_val_bool(false));
      cw_node_set(S.fx_ember[nl], "emitting", cw_val_bool(false));
    }
  /* bubbles in the nearest pool */
  static int bub_span = -2;
  int want = -1;
  for (int i = 0; i < S.nwspan; i++) {
    const float *sp = S.wspan[i];
    if (camx > sp[0] - 30 && camx < sp[1] + 30) want = i;
  }
  if (want != bub_span) {
    bub_span = want;
    if (want >= 0) {
      const float *sp = S.wspan[want];
      cw_node3d_set_position(S.fx_bubbles, (t3_vec3){ (sp[0] + sp[1]) * 0.5f, sp[2] + 0.4f, 0.6f });
      cw_node_set(S.fx_bubbles, "one_shot", cw_val_bool(false));
      cw_node_set(S.fx_bubbles, "explosiveness", cw_val_float(0));
      cw_node_set(S.fx_bubbles, "emission_shape", cw_val_enum("BOX"));
      cw_node_set(S.fx_bubbles, "emission_extents", cw_val_vec3((sp[1] - sp[0]) * 0.45f, 0.2f, 0.8f));
      cw_node_set(S.fx_bubbles, "direction", cw_val_vec3(0, 1, 0));
      cw_node_set(S.fx_bubbles, "spread", cw_val_float(10));
      cw_node_set(S.fx_bubbles, "lifetime", cw_val_float(fmaxf(0.8f, (sp[3] - sp[2]) / 1.6f)));
    }
    cw_node_set(S.fx_bubbles, "emitting", cw_val_bool(want >= 0));
  }
}

/* the player's screen shake setting (the shell's Settings > Screen shake): 1 full, less reduced, 0 off */
static float shake_setting(void) { return cw_shell_screen_shake(); }

/* ── frame: sync the scene to the sim ─────────────────────────────── */
static void sync_scene(float alpha, float dt) {
  game *g = &G;
  const level *L = &g->L;
  S.time += dt;
  camera_rig *c = &g->cam;
  /* the camera moves in the fixed tick like the players: draw it between its last two ticks with the same alpha the
   * players use, or it steps while they glide (a judder on any host whose frame rate is not the tick rate) */
  float a = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
  bool snapped = fabsf(c->x - c->px) > 4 || fabsf(c->y - c->py) > 4; /* a warp or a respawn: no glide */
  float vx = snapped ? c->x : c->px + (c->x - c->px) * a, vy = snapped ? c->y : c->py + (c->y - c->py) * a;
  float vz = snapped ? c->z : c->pz + (c->z - c->pz) * a;
  /* shake: trauma squared, smooth (two slow sines per axis, not a per-frame random jump), scaled by the player's
   * screen shake setting (Settings: Full, Reduced, Off) */
  float sh = c->shake * shake_setting();
  float amp = sh * sh * 0.45f;
  float ox = amp * (0.6f * sinf(S.time * 47.0f) + 0.4f * sinf(S.time * 29.0f + 1.3f));
  float oy = amp * (0.6f * cosf(S.time * 41.0f) + 0.4f * sinf(S.time * 23.0f + 0.7f));
  cw_node3d_set_position(S.cam, (t3_vec3){ vx + ox, vy + oy, vz });
  g_view_dbg[0] = vx + ox, g_view_dbg[1] = vy + oy, g_view_dbg[2] = vz, g_view_dbg[3] = sh;
  if (!g_attract) {
    float pp[MAX_PLAYERS * 2];
    for (int i = 0; i < g->nplayers; i++) {
      cw_pos2 ip = cw_mover2d_interpolated(&g->p[i].pf.body, alpha);
      pp[2 * i] = (float)ip.x, pp[2 * i + 1] = (float)ip.y;
    }
    camgate_frame(g_view_dbg, pp, g->nplayers, snapped);
  }
  cw_node3d_set_rotation(S.cam, (t3_vec3){ -0.06f, 0, 0 });
  cw_node3d_set_position(S.sun, (t3_vec3){ vx + 8, vy + 18, 14 });
  apply_mood(vx);
  world_update_water_mesh(S.time);
  world_update_backdrop(S.time);
  update_spans(vx);
  /* the motes follow the camera and take the region's character */
  {
    int b = g->L.biome[(int)c->x < 0 ? 0 : (int)c->x >= WORLD_W ? WORLD_W - 1 : (int)c->x];
    static const float MC[BIOMES][4] = { { 1.0f, 0.95f, 0.5f, 3.0f }, { 0.4f, 0.8f, 1.0f, 2.0f }, { 1.0f, 1.0f, 0.8f, 1.2f }, { 1.0f, 0.4f, 0.1f, 4.0f } };
    static const float MG[BIOMES] = { 0.0f, 0.0f, -0.05f, 0.9f };
    static int last_b = -1;
    cw_node3d_set_position(S.fx_motes, (t3_vec3){ vx, vy, 0 });
    if (b != last_b) {
      last_b = b;
      cw_node_set(S.fx_motes, "color", cw_val_color(MC[b][0], MC[b][1], MC[b][2], 1));
      cw_node_set(S.fx_motes, "energy", cw_val_float(MC[b][3]));
      cw_node_set(S.fx_motes, "gravity", cw_val_vec3(0, MG[b], 0));
    }
  }
  /* players. Two players in one spot (they pass through each other) would draw as one merged figure: each one of a
   * crowd is drawn a little apart, staggered sideways and in depth by its rank in the crowd, eased in and out. The
   * simulation is untouched. */
  float want_sx[MAX_PLAYERS] = { 0 }, want_sz[MAX_PLAYERS] = { 0 };
  for (int i = 0; i < g->nplayers && i < MAX_PLAYERS; i++) {
    const player *p = &g->p[i];
    if (!p->joined || !p->alive || p->bubble) continue;
    int rank = 0, crowd = 1;
    for (int k = 0; k < g->nplayers && k < MAX_PLAYERS; k++) {
      const player *q = &g->p[k];
      if (k == i || !q->joined || !q->alive || q->bubble) continue;
      double dx = q->pf.body.position.x - p->pf.body.position.x, dy = q->pf.body.position.y - p->pf.body.position.y;
      if (dx * dx < 0.8 * 0.8 && dy * dy < 1.2 * 1.2) {
        crowd++;
        if (k < i) rank++;
      }
    }
    if (crowd > 1) {
      want_sx[i] = ((float)rank - 0.5f * (float)(crowd - 1)) * 0.55f;
      want_sz[i] = (rank & 1 ? -0.5f : 0.35f) * (float)(1 + rank / 2);
      /* never drawn into a wall: a sideways step that would put the body in rock goes the other way */
      float bx = (float)p->pf.body.position.x + want_sx[i] + (want_sx[i] > 0 ? 0.35f : -0.35f);
      if (world_solid(&g->L, (int)floorf(bx), (int)floorf((float)p->pf.body.position.y + 0.6f))) want_sx[i] = -want_sx[i];
    }
  }
  int bubbles = 0;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    player *p = &g->p[i];
    t3_object *po = cw_node3d_object(S.pnode[i]);
    if (!p->joined || i >= g->nplayers) {
      cw_node3d_set_position(S.pnode[i], (t3_vec3){ 0, -100, 0 });
      cw_node_set(S.plight[i], "energy", cw_val_float(0));
      continue;
    }
    cw_pos2 ip = cw_mover2d_interpolated(&p->pf.body, alpha);
    float x = (float)ip.x, y = (float)ip.y;
    if (p->bubble) {
      bubbles++;
      x = p->bubble_x, y = p->bubble_y - 0.6f + 0.1f * sinf(S.time * 3 + (float)i);
      S.bubble[i]->base.visible = true;
      p->squash = 0;
    } else S.bubble[i]->base.visible = false;
    if (!p->alive && !p->bubble) {
      cw_node3d_set_position(S.pnode[i], (t3_vec3){ x, -100, 0 });
      continue;
    }
    float ease = 1.0f - expf(-8.0f * dt);
    S.spread_x[i] += (want_sx[i] - S.spread_x[i]) * ease, S.spread_z[i] += (want_sz[i] - S.spread_z[i]) * ease;
    cw_node3d_set_position(S.pnode[i], (t3_vec3){ x + S.spread_x[i], y, S.spread_z[i] });
    float face = (float)p->pf.facing;
    /* turn toward the way it faces, smoothly: three quarters on to the camera */
    float want = face > 0 ? 1.05f : -1.05f;
    if (p->anim == 4) want = -want; /* on a wall it looks back along the wall */
    S.yaw[i] += (want - S.yaw[i]) * (1.0f - expf(-14.0f * dt));
    cw_node3d_set_rotation(S.pmodel[i], (t3_vec3){ 0, S.yaw[i], 0 });
    /* secondary animation: squash and stretch from events, a run cycle and the pose from the bones */
    if (p->land_fx) p->squash = 0.35f;
    if (p->jump_fx) p->squash = -0.3f;
    p->squash *= 1.0f - 6.0f * dt > 0 ? 1.0f - 6.0f * dt : 0;
    float vy = p->pf.body.velocity.y;
    float stretch = p->squash < 0 ? -p->squash : 0.0f;
    float sq = p->squash > 0 ? p->squash : 0.0f;
    float st = 1.0f + stretch * 0.6f + (vy > 4 ? 0.06f : 0) - sq * 0.5f;
    float wd = 1.0f - stretch * 0.3f + sq * 0.4f;
    float spd = fabsf((float)p->pf.body.velocity.x);
    S.runph[i] += (p->anim == 5 ? 7.0f : 4.0f + spd * 0.9f) * dt;
    if (S.prig[i]) {
      rig_pose_hero(S.prig[i], p->anim, S.runph[i], S.time + (float)i, st, wd, vy);
      /* the blink while invulnerable: each player on its own phase, so a group that respawns together never vanishes
       * all at once (the screen showed four markers and no one under them) */
      bool inv = p->invuln > 0 && (((p->invuln + 2 * i) / 4) & 1);
      cw_node_set(S.pmodel[i], "visible", cw_val_bool(!inv));
    }
    cw_node3d_set_position(S.plight[i], (t3_vec3){ x, y + 1.2f, 2.0f });
    cw_node_set(S.plight[i], "energy", cw_val_float(p->bubble ? 3.0f : 2.0f));
    /* effects from this frame's events */
    if (p->land_fx == 1) burst(S.fx_dust[i], x, y + 0.1f, 0.5f);
    if (p->land_fx == 2) burst(S.fx_splash, x, y + 0.8f, 1.0f);
    if (p->jump_fx) burst(S.fx_dust[i], x, y + 0.1f, 0.5f);
    if (p->hurt_fx) burst(S.fx_hit, x, y + 0.8f, 0.5f), p->hurt_fx = 0;
  }
  /* coins, pickups, platforms, enemies */
  for (int i = 0; i < L->ncoins; i++) {
    bool tk = L->coins[i].taken;
    cw_node_set(S.coin_node[i], "visible", cw_val_bool(!tk));
    if (!tk) cw_node3d_set_rotation(S.coin_node[i], (t3_vec3){ 0, S.time * 2.5f + (float)i, 0 });
  }
  for (int i = 0; i < L->npickups; i++) {
    cw_node_set(S.pick_node[i], "visible", cw_val_bool(!L->pickups[i].taken));
    cw_node3d_set_position(S.pick_node[i], (t3_vec3){ (float)L->pickups[i].x + 0.5f, (float)L->pickups[i].y + 1.1f + 0.12f * sinf(S.time * 2.0f), 0 });
    cw_node3d_set_rotation(S.pick_node[i], (t3_vec3){ 0, S.time * 1.5f, 0 });
  }
  for (int i = 0; i < L->nplat; i++) {
    cw_pos2 pp = cw_body2d_position(L->plats[i].body);
    S.plat_mesh[i]->base.position = (t3_vec3){ (float)pp.x, (float)pp.y, 0 };
  }
  for (int i = 0; i < L->nenemies; i++) {
    const enemy *e = &L->enemies[i];
    cw_node_set(S.enode[i], "visible", cw_val_bool(e->alive));
    if (S.prev_alive[i] && !e->alive) { /* it just died: a blast, bigger for the boss */
      float bs = e->kind == 2 ? 2.4f : 1.0f, by = e->y + (e->kind == 2 ? 1.0f : 0.5f);
      burst(S.fx_boom, e->x, by, 0.8f), burst(S.fx_boomsmoke, e->x, by, 0.6f), burst(S.fx_boomspark, e->x, by, 0.6f);
      cw_node_set(S.fx_wave, "max_radius", cw_val_float(4.5f * bs));
      cw_node3d_set_position(S.fx_wave, (t3_vec3){ e->x, by, 1.0f });
      cw_shockwave_fire(S.fx_wave);
      burst(S.fx_debris, e->x, by, 0.5f); /* its screen shake comes from the sim (sim_shake), where it died */
    }
    S.prev_alive[i] = e->alive;
    if (!e->alive || !S.erig[i]) continue;
    cw_node3d_set_position(S.enode[i], (t3_vec3){ e->x, e->y + (e->kind == 1 ? 0.2f * sinf(S.time * 5 + (float)i) : 0), 0 });
    float want = e->dir > 0 ? 1.2f : -1.2f;
    S.eyaw[i] += (want - S.eyaw[i]) * (1.0f - expf(-10.0f * dt));
    cw_node3d_set_rotation(S.emodel[i], (t3_vec3){ 0, S.eyaw[i], 0 });
    float flash = e->hurt > 0 && (e->hurt / 3) % 2 ? 1.0f : 0.0f;
    S.ephase[i] += dt * (e->kind == 1 ? 16.0f : 11.0f);
    if (e->kind == 0) rig_pose_walker(S.erig[i], S.ephase[i], S.time, 1.0f);
    else if (e->kind == 1) rig_pose_flyer(S.erig[i], S.ephase[i], S.time);
    else {
      rig_pose_boss(S.erig[i], S.time, 1.0f - (float)e->hp / 9.0f);
      rig_glow(S.erig[i], 1.0f + 0.4f * sinf(S.time * 4) + (1.0f - (float)e->hp / 9.0f));
    }
    rig_flash(S.erig[i], flash);
  }
  /* the nearest few light sources to the camera */
  int pick[NTORCH], np = 0;
  for (int k = 0; k < S.nlight_src && np < 64; k++) {
    float d = fabsf(S.light_x[k] - c->x);
    if (d > 26) continue;
    int at = np++;
    while (at > 0 && fabsf(S.light_x[pick[at - 1]] - c->x) > d) pick[at] = pick[at - 1], at--;
    if (at < NTORCH) pick[at] = k; else np--;
    if (np > NTORCH) np = NTORCH;
  }
  for (int i = 0; i < NTORCH; i++) {
    if (i < np) {
      int k = pick[i];
      float flick = 1.0f + 0.12f * sinf(S.time * 11.0f + (float)k * 2.3f) + 0.06f * sinf(S.time * 23.0f + (float)k);
      cw_node3d_set_position(S.torch[i], (t3_vec3){ S.light_x[k], S.light_y[k], 1.5f });
      cw_node_set(S.torch[i], "color", cw_val_color(S.light_r[k], S.light_g[k], S.light_b[k], 1));
      cw_node_set(S.torch[i], "energy", cw_val_float(S.light_e[k] * flick));
      cw_node_set(S.torch[i], "range", cw_val_float(S.light_range[k]));
    } else cw_node_set(S.torch[i], "energy", cw_val_float(0));
  }
  g_bubbles_dbg = bubbles;
}

/* ── input ─────────────────────────────────────────────────────────── */
static void read_input(void) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    input_state *s = &INPUT[i];
    memset(s, 0, sizeof *s);
    if (i >= G.nplayers) continue;
    s->x = cw_input_axis(i, a_left, a_right);
    s->y = -cw_input_axis(i, a_up, a_down);
    s->jump_pressed = cw_action_just_pressed(i, a_jump);
    s->jump_held = cw_action_pressed(i, a_jump);
    s->dash_pressed = cw_action_just_pressed(i, a_dash);
    s->down = cw_action_pressed(i, a_down);
    s->retry = cw_action_pressed(i, a_retry);
  }
}

/* ── the shell and the game flow ──────────────────────────────────── */
/* the autopilot builds that carry a start (gates on hosts that have no environment): players, start column, abilities */
#ifndef COOP_DEF_PLAYERS
#define COOP_DEF_PLAYERS 2
#endif
#ifndef COOP_DEF_START_X
#define COOP_DEF_START_X (-1)
#endif
#ifndef COOP_DEF_ABILITIES
#define COOP_DEF_ABILITIES (-1)
#endif
static int cfg_int(const char *env, int def) {
  const char *v = getenv(env);
  return v ? atoi(v) : def;
}

static void start_match(int players, void *user) {
  (void)user;
  g_attract = false;
  save_t *sv = cw_shell_game_save();
  G.abilities = 0;
  sim_reset(&G, players);
  G.shake_bug = getenv("COOP_SHAKE_BUG") != NULL; /* the camera gate's must-fail control (native only) */
  if (sv->abilities && !getenv("COOP_FRESH")) G.abilities = sv->abilities;
  G.reached |= sv->reached;
  if (cfg_int("COOP_ABILITIES", COOP_DEF_ABILITIES) >= 0) G.abilities = (uint32_t)cfg_int("COOP_ABILITIES", COOP_DEF_ABILITIES);
  if (cfg_int("COOP_START_X", COOP_DEF_START_X) >= 0) { /* a test or a screenshot starts anywhere */
    int sx = cfg_int("COOP_START_X", COOP_DEF_START_X);
    if (sx < 2) sx = 2;
    if (sx > WORLD_W - 3) sx = WORLD_W - 3;
    for (int i = 0; i < G.nplayers; i++) {
      cw_platformer2d_teleport(&G.p[i].pf, (cw_pos2){ (double)sx + 0.7 * i, (double)G.L.ground[sx] + 0.05 });
      G.p[i].invuln = 120;
    }
    G.cam.x = G.cam.tx = G.cam.px = (float)sx, G.cam.y = G.cam.ty = G.cam.py = (float)G.L.ground[sx] + 3;
    G.checkpoint = -1;
    for (int k = 0; k < G.L.nchecks; k++)
      if (G.L.checks[k].x < sx) G.checkpoint = k, G.reached |= 1u << k;
  }
  build_scene();
  camgate_reset();
  cw_info("coopplatformer: pools %d, lava spans %d", S.nwspan, S.nlspan);
  for (int i = 0; i < S.nwspan; i++) cw_info("  pool x %.0f..%.0f y %.0f..%.0f", S.wspan[i][0], S.wspan[i][1], S.wspan[i][2], S.wspan[i][3]);
  g_run_ticks = 0;
  g_map_open = false;
}
static void quit_match(void *user) {
  (void)user;
}
static void sound_hook(cw_shell_sound s, void *user) {
  (void)user;
  if (!A.on) return;
  switch (s) {
    case CW_SHELL_SND_MOVE: sfx(SFX_CHECK, 0.3f); break;
    case CW_SHELL_SND_SELECT: sfx(SFX_COIN, 0.5f); break;
    case CW_SHELL_SND_BACK: sfx(SFX_LAND, 0.5f); break;
    case CW_SHELL_SND_JOIN: sfx(SFX_POP, 0.6f); break;
    case CW_SHELL_SND_PAUSE: sfx(SFX_BUBBLE, 0.4f); break;
  }
}

#ifdef COOP_AUTOPILOT
static uint32_t fz = 12345;
static uint32_t frand(void) {
  fz = fz * 1664525u + 1013904223u;
  return fz >> 8;
}

/* a fresh run: no abilities unless COOP_ABILITIES says the player resumed a save with them */
static void soak_reset(int players) {
  G.abilities = 0;
  sim_reset(&G, players);
  if (getenv("COOP_ABILITIES")) G.abilities = (uint32_t)atoi(getenv("COOP_ABILITIES"));
}

/* COOP_SECRETS=1: scripted routes to the secret coins. A group behind a cracked block: stand 3 tiles left of the
 * block with every ability, dash into it and walk on. A group in water: dive from the surface above it, swim to it
 * and stroke back up. Each must collect every coin of its group (and come back out of the water). */
static void secret_routes(int players) {
  soak_reset(players);
  int groups = 0, ok = 0;
  for (int c0 = 0; c0 < G.L.ncoins; c0++) {
    if (!G.L.coins[c0].secret || (c0 > 0 && G.L.coins[c0 - 1].secret && G.L.coins[c0].x - G.L.coins[c0 - 1].x < 3.5f && G.L.coins[c0].y > G.L.coins[c0 - 1].y - 0.1f && G.L.coins[c0].y < G.L.coins[c0 - 1].y + 0.1f)) continue;
    int c1 = c0;
    while (c1 + 1 < G.L.ncoins && G.L.coins[c1 + 1].secret && G.L.coins[c1 + 1].x - G.L.coins[c1].x < 3.5f) c1++;
    float cx = G.L.coins[c0].x, cy = G.L.coins[c0].y;
    int ix = (int)cx, iy = (int)cy, crack_x = -1;
    for (int x = ix; x >= ix - 8 && x >= 0 && crack_x < 0; x--)
      for (int y = iy - 1; y <= iy + 1; y++)
        if (G.L.t[y][x] == T_CRACK) crack_x = x;
    bool wet = G.L.t[iy][ix] == T_WATER;
    if (crack_x < 0 && !wet) {
      /* a platforming route: the wooden ledges under the coins are waypoints (lowest first, then up to the
       * coins); a small controller walks, jumps and air-jumps from one to the next with every ability */
      float wx[4], wy[4];
      int nw = 0;
      for (int y = iy - 1; y >= iy - 10 && y >= 0 && nw < 3; y--)
        for (int x = ix - 12; x <= ix + 3 && nw < 3; x++)
          if (G.L.t[y][x] == T_ONEWAY && (x == 0 || G.L.t[y][x - 1] != T_ONEWAY)) {
            int x1 = x;
            while (G.L.t[y][x1] == T_ONEWAY) x1++;
            wx[nw] = (float)(x + x1) * 0.5f, wy[nw] = (float)y + 1.0f;
            nw++;
            break; /* one ledge per row */
          }
      for (int i = 0; i < nw / 2; i++) { /* the scan found the highest first: lowest first */
        float tx = wx[i], ty = wy[i];
        wx[i] = wx[nw - 1 - i], wy[i] = wy[nw - 1 - i], wx[nw - 1 - i] = tx, wy[nw - 1 - i] = ty;
      }
      wx[nw] = cx, wy[nw] = cy - 0.7f, nw++;
      soak_reset(players);
      G.abilities = AB_DOUBLE | AB_CLAWS | AB_DASH;
      for (int i = 1; i < G.nplayers; i++) G.p[i].joined = false;
      int sxi = (int)cx - 9;
      while (sxi > 2 && (G.L.ground[sxi] < 8 || G.L.ground[sxi - 1] != G.L.ground[sxi] || G.L.ground[sxi + 1] != G.L.ground[sxi])) sxi--; /* flat ground, not a gap */
      cw_platformer2d_teleport(&G.p[0].pf, (cw_pos2){ (double)sxi, (double)G.L.ground[sxi] + 0.05 });
      int before = G.secrets, group = c1 - c0 + 1, reached = 0;
      for (int w = 0; w < nw && G.p[0].alive; w++) {
        for (int t = 0; t < 600; t++) {
          const cw_platformer2d *pf = &G.p[0].pf;
          float x = (float)pf->body.position.x, y = (float)pf->body.position.y, vy = pf->body.velocity.y;
          memset(INPUT, 0, sizeof INPUT);
          float dx = wx[w] - x;
          INPUT[0].x = dx > 0.25f ? 1.0f : dx < -0.25f ? -1.0f : 0.0f;
          if (pf->body.on_floor && y < wy[w] - 0.5f) INPUT[0].jump_pressed = true;
          if (!pf->body.on_floor && vy < 0.5f && y < wy[w] - 0.2f && pf->air_jumps_left > 0 && (t % 5) == 0) INPUT[0].jump_pressed = true;
          INPUT[0].jump_held = vy > 0 || INPUT[0].jump_pressed;
          sim_step(&G, INPUT, 1.0f / 60.0f);
          if (!G.p[0].alive) break;
          if (pf->body.on_floor && dx < 0.6f && dx > -0.6f && y > wy[w] - 0.6f) {
            if (getenv("COOP_TRACE")) printf("  waypoint %d (%.1f,%.1f) reached at t=%d\n", w, wx[w], wy[w], t);
            reached++;
            break;
          }
          if (G.secrets - before >= group) break;
        }
      }
      if (getenv("COOP_TRACE")) printf("  waypoints %d of %d, end %.2f,%.2f\n", reached, nw, G.p[0].pf.body.position.x, G.p[0].pf.body.position.y);
      groups++;
      bool good = G.secrets - before >= group;
      printf("secret group at %.0f,%.0f (platforming, %d ledges): %d of %d coins\n", cx, cy, nw - 1, G.secrets - before, group);
      if (good) ok++;
      continue;
    }
    groups++;
    soak_reset(players);
    G.abilities = AB_DOUBLE | AB_CLAWS | AB_DASH;
    int before = G.secrets;
    for (int i = 1; i < G.nplayers; i++) G.p[i].joined = false;
    player *p = &G.p[0];
    float sx = wet ? cx : (float)crack_x - 3.0f, sy = wet ? G.L.ground[ix] + 12.0f : (float)G.L.ground[crack_x - 3 < 0 ? 0 : crack_x - 3] + 0.05f;
    if (wet) {
      int top = iy;
      while (top < WORLD_H - 1 && G.L.t[top + 1][ix] == T_WATER) top++;
      sy = (float)top + 2.5f; /* drop in from above the surface */
    }
    cw_platformer2d_teleport(&p->pf, (cw_pos2){ (double)sx, (double)sy });
    int end_t = 0;
    for (int t = 0; t < 900; t++) {
      memset(INPUT, 0, sizeof INPUT);
      if (!wet) {
        INPUT[0].x = 1;
        if (t == 20) INPUT[0].dash_pressed = true;
        if (t > 60) INPUT[0].x = t < 150 ? 1.0f : 0.0f;
      } else {
        /* dive to the nearest coin still there, then up to the surface */
        int tgt = -1;
        float bestd = 1e9f;
        for (int k = c0; k <= c1; k++) {
          if (G.L.coins[k].taken) continue;
          float ddx = G.L.coins[k].x - (float)p->pf.body.position.x, ddy = G.L.coins[k].y - ((float)p->pf.body.position.y + 0.7f);
          if (ddx * ddx + ddy * ddy < bestd) bestd = ddx * ddx + ddy * ddy, tgt = k;
        }
        if (tgt >= 0) {
          float ddx = G.L.coins[tgt].x - (float)p->pf.body.position.x, ddy = G.L.coins[tgt].y - ((float)p->pf.body.position.y + 0.7f);
          INPUT[0].x = ddx > 0.2f ? 1.0f : ddx < -0.2f ? -1.0f : 0.0f;
          INPUT[0].y = ddy > 0.25f ? 1.0f : ddy < -0.25f ? -1.0f : 0.0f;
          INPUT[0].jump_pressed = ddy > 0.25f && (t % 10) == 0;
        } else {
          INPUT[0].y = 1.0f;
          INPUT[0].jump_pressed = (t % 12) == 0;
        }
      }
      sim_step(&G, INPUT, 1.0f / 60.0f);
      end_t = t;
      if (getenv("COOP_TRACE") && t >= 15 && t < 30) printf("  t=%d x=%.2f y=%.2f vx=%.1f dash_t=%.2f ev=%x gev=%x tile=%d can=%d ab=%u\n", t, p->pf.body.position.x, p->pf.body.position.y, p->pf.body.velocity.x, p->pf.dash_t, p->pf.events, G.events, G.L.t[22][68], p->pf.can_dash, G.abilities);
      if (!p->alive) break;
    }
    int got = G.secrets - before, want = c1 - c0 + 1;
    int stop = iy;
    while (stop < WORLD_H - 1 && G.L.t[stop + 1][ix] == T_WATER) stop++;
    bool out = !wet || p->pf.body.position.y > (float)stop - 0.6f; /* back at the surface */
    printf("  start %.1f,%.1f end %.2f,%.2f crack_x=%d\n", sx, sy, p->pf.body.position.x, p->pf.body.position.y, crack_x);
    printf("secret group at %.0f,%.0f (%s): %d of %d coins%s, ended t=%d%s\n", cx, cy, wet ? "swim" : "dash through the crack", got, want, wet ? (out ? ", back out" : ", NOT back out") : "", end_t, p->alive ? "" : " (died)");
    if (got == want && out && p->alive) ok++;
  }
  printf("secret routes: %d of %d scripted groups pass\n", ok, groups);
  fflush(stdout);
  exit(ok == groups ? 0 : 1);
}

/* COOP_FUZZ=<runs>: sequence-break check. For each ability gate the player starts at the flag before it with ONLY the
 * abilities found before it and plays random input for 30 seconds: crossing the gate alive is a break. */
static void fuzz(int runs, int players) {
  soak_reset(players);
  for (int gi = 0; gi < G.L.ngates; gi++) {
    const gate gt = G.L.gates[gi];
    uint32_t have = 0;
    for (int k = 0; k < G.L.npickups; k++)
      if (G.L.pickups[k].x < gt.x0 && G.L.pickups[k].ability != gt.need) have |= (uint32_t)G.L.pickups[k].ability; /* all but the one the gate asks for */
    for (int pass = 0; pass < 2; pass++) {
    int broke = 0;
    float far = 0;
    for (int r = 0; r < runs; r++) {
      soak_reset(players);
      G.abilities = pass ? (have | (uint32_t)gt.need) : have; /* pass 1 is the control: with the ability the gate must be crossable */
      for (int k = 0; k < G.L.npickups; k++) G.L.pickups[k].taken = true; /* no pedestal may hand out the ability on the way */
      G.reached |= 1u << gt.flag;
      sim_warp(&G, gt.flag);
      int hold = 0, mx = 1, jh = 0;
      bool crossed = false;
      static float px[1800], py[1800];
      for (int t = 0; t < 1800 && !crossed; t++) {
        memset(INPUT, 0, sizeof INPUT);
        if (hold-- <= 0) hold = 20 + (int)(frand() % 40), mx = (frand() % 100) < 70 ? 1 : (frand() % 2 ? 0 : -1);
        INPUT[0].x = (float)mx;
        if (frand() % 100 < 5) INPUT[0].jump_pressed = true, jh = 10 + (int)(frand() % 30);
        INPUT[0].jump_held = jh-- > 0;
        if (frand() % 100 < 2) INPUT[0].dash_pressed = true;
        INPUT[0].down = frand() % 100 < 4;
        sim_step(&G, INPUT, 1.0f / 60.0f);
        float x = (float)G.p[0].pf.body.position.x;
        px[t] = x, py[t] = (float)G.p[0].pf.body.position.y;
        if (G.p[0].alive && x > far) far = x;
        if (getenv("COOP_TRACE") && gi == atoi(getenv("COOP_TRACE")) && r == (getenv("COOP_FUZZ_R") ? atoi(getenv("COOP_FUZZ_R")) : 0) && pass == 0 && (t % 4) == 0) printf("  r0 t=%d x=%.1f y=%.1f vy=%.1f fl=%d alive=%d bub=%d ab=%u\n", t, x, (double)G.p[0].pf.body.position.y, G.p[0].pf.body.velocity.y, G.p[0].pf.body.on_floor, G.p[0].alive, G.p[0].bubble, G.abilities);
        if (G.p[0].alive && !G.p[0].bubble && x > (float)gt.x1 + 2.0f) {
          crossed = true;
          if (getenv("COOP_TRACE") && pass == 0 && broke < 1 && gi == atoi(getenv("COOP_TRACE")))
            for (int q = (t > 300 ? t - 300 : 0); q <= t; q += 8) printf("    path t=%d %.1f,%.1f\n", q, px[q], py[q]);
          if (getenv("COOP_TRACE") && pass == 0 && broke < 3) printf("  crossing run %d t=%d at %.1f,%.1f on_floor=%d bubble_rescues=%d deaths=%d\n", r, t, x, (double)G.p[0].pf.body.position.y, G.p[0].pf.body.on_floor, G.rescues, G.deaths);
        }
      }
      broke += crossed;
    }
    printf("fuzz gate %d (x %d..%d, needs %u) %s: %d of %d runs crossed alive, furthest %.1f\n", gi, gt.x0, gt.x1, (unsigned)gt.need, pass ? "WITH it" : "without it", broke, runs, far);
    }
  }
  fflush(stdout);
  exit(0);
}

/* COOP_MAP=<x0>: print the level 48 columns from x0 (and its flags and gates) and exit */
static void print_map(int x0) {
  sim_reset(&G, 1);
  for (int y = WORLD_H - 1; y >= 0; y--) {
    printf("%2d ", y);
    for (int x = x0; x < x0 + 48 && x < WORLD_W; x++) {
      int c = G.L.t[y][x];
      putchar(c == T_AIR ? '.' : c == T_ROCK ? '#' : c == T_GRASS ? 'g' : c == T_ONEWAY ? '-' : c == T_SPIKE ? '^' : c == T_LAVA ? 'L' : c == T_WATER ? '~' : c == T_CRACK ? 'x' : c == T_BRICK ? 'b' : '?');
    }
    putchar('\n');
  }
  for (int k = 0; k < G.L.nchecks; k++) printf("flag %d at %d,%d\n", k, G.L.checks[k].x, G.L.checks[k].y);
  for (int k = 0; k < G.L.ngates; k++) printf("gate %d x %d..%d needs %d flag %d\n", k, G.L.gates[k].x0, G.L.gates[k].x1, G.L.gates[k].need, G.L.gates[k].flag);
  for (int k = 0; k < G.L.npickups; k++) printf("pickup %d at %d,%d ability %d\n", k, G.L.pickups[k].x, G.L.pickups[k].y, G.L.pickups[k].ability);
  fflush(stdout);
  exit(0);
}

/* COOP_SOAK=<ticks>: run the bots through the whole game with no rendering and report (soft-lock and crash check):
 * wins, the longest stretch with no progress, deaths per 16-column screen, the fastest win and a state hash. */
static void soak(int ticks, int players) {
  soak_reset(players);
  float best = 0;
  int last_progress = 0, longest_stall = 0, wins = 0, resets = 0, win_best = 0, run_start = 0;
  bool dumped = false;
  int boss_seen = 9;
  int death_map[WORLD_W / 16 + 1];
  memset(death_map, 0, sizeof death_map);
  bool was_alive[MAX_PLAYERS];
  for (int i = 0; i < MAX_PLAYERS; i++) was_alive[i] = true;
  for (int t = 0; t < ticks; t++) {
    for (int i = 0; i < MAX_PLAYERS; i++) sim_bot_input(&G, i, &INPUT[i]);
    sim_step(&G, INPUT, 1.0f / 60.0f);
    for (int i = 0; i < G.nplayers; i++) {
      float x = (float)G.p[i].pf.body.position.x, y = (float)G.p[i].pf.body.position.y;
      if (x != x || y != y || x < -5 || x > WORLD_W + 5) {
        printf("soak BAD POSITION t=%d p%d (%f,%f)\n", t, i, x, y);
        exit(2);
      }
      if (was_alive[i] && !G.p[i].alive) {
        death_map[(int)(x < 0 ? 0 : x) / 16]++;
        if (getenv("COOP_DEATHS")) printf("death t=%d p%d at %.1f,%.1f vel %.1f,%.1f tile=%d\n", t, i, x, y, G.p[i].pf.body.velocity.x, G.p[i].pf.body.velocity.y, G.L.t[(int)y][(int)x]);
      }
      was_alive[i] = G.p[i].alive;
    }
    if (getenv("COOP_TRACE") && t >= atoi(getenv("COOP_TRACE")) && t < atoi(getenv("COOP_TRACE")) + 90) {
      int tp = getenv("COOP_TRACE_P") ? atoi(getenv("COOP_TRACE_P")) : 0;
      const player *q = &G.p[tp];
      printf("tr t=%d pos=%.2f,%.2f vel=%.2f,%.2f fl=%d wl=%d wet=%.1f in=(%.0f,%.0f,j%d%d,d%d) dr=%d dcd=%.2f al=%d ev=%x alive=%d bub=%d ab=%u\n", t, q->pf.body.position.x, q->pf.body.position.y, q->pf.body.velocity.x, q->pf.body.velocity.y, q->pf.body.on_floor, q->pf.body.on_wall, q->pf.submerged, INPUT[tp].x, INPUT[tp].y, INPUT[tp].jump_pressed, INPUT[tp].jump_held, INPUT[tp].dash_pressed, q->pf.dash_ready, q->pf.dash_cd, q->pf.air_jumps_left, q->pf.events, q->alive, q->bubble, G.abilities);
    }
    float lead = -1;
    for (int i = 0; i < G.nplayers; i++) {
      float x = (float)G.p[i].pf.body.position.x;
      if (x > lead) lead = x;
    }
    if (lead > best + 0.5f) best = lead, last_progress = t;
    if (G.boss_hp != boss_seen) boss_seen = G.boss_hp, last_progress = t; /* a boss fight is progress */
    if (t - last_progress > longest_stall) longest_stall = t - last_progress;
    if (t % 3600 == 0) printf("soak t=%d lead=%.1f best=%.1f coins=%d deaths=%d rescues=%d abil=%u ck=%d boss=%d won=%d\n", t, lead, best, G.coins_total, G.deaths, G.rescues, G.abilities, G.checkpoint, G.boss_hp, G.won);
    if (G.won && G.tick - G.win_tick > 300) {
      wins++;
      int took = t - run_start;
      if (!win_best || took < win_best) win_best = took;
      printf("soak WIN at t=%d (run %d took %d ticks = %.1f min)\n", t, wins, took, (double)took / 3600.0);
      soak_reset(players);
      for (int i = 0; i < MAX_PLAYERS; i++) was_alive[i] = true;
      best = 0, last_progress = t, run_start = t, resets++, boss_seen = 9;
    }
    if (t - last_progress > 1800) { /* 30 s of nothing: a player holds Retry */
      printf("soak STALL at t=%d x=%.1f: returning the group to the flag (the Retry rule)\n", t, best);
      for (int i = 0; i < G.nplayers; i++) printf("  p%d pos=%.2f,%.2f vel=%.2f,%.2f floor=%d wall=%d wet=%.1f alive=%d bubble=%d\n", i, G.p[i].pf.body.position.x, G.p[i].pf.body.position.y, G.p[i].pf.body.velocity.x, G.p[i].pf.body.velocity.y, G.p[i].pf.body.on_floor, G.p[i].pf.body.on_wall, G.p[i].pf.submerged, G.p[i].alive, G.p[i].bubble);
      if (getenv("COOP_DUMP") && resets == 0 && !dumped) {
        dumped = true;
        int x0 = (int)best - 20, yc = G.L.ground[(int)best < WORLD_W ? (int)best : WORLD_W - 1];
        for (int y = yc + 14; y >= yc - 12 && y >= 0; y--) {
          printf("%2d ", y);
          for (int x = x0 < 0 ? 0 : x0; x < x0 + 50 && x < WORLD_W; x++) {
            int c = G.L.t[y][x];
            char ch = c == T_AIR ? '.' : c == T_ROCK ? '#' : c == T_GRASS ? 'g' : c == T_ONEWAY ? '-' : c == T_SPIKE ? '^' : c == T_LAVA ? 'L' : c == T_WATER ? '~' : c == T_CRACK ? 'x' : c == T_BRICK ? 'b' : '?';
            for (int i = 0; i < G.nplayers; i++)
              if ((int)G.p[i].pf.body.position.x == x && (int)G.p[i].pf.body.position.y == y) ch = (char)('0' + i);
            putchar(ch);
          }
          putchar('\n');
        }
        printf("x0=%d\n", x0);
      }
      sim_return(&G); /* what a player does: hold Retry (the rule is tested in sim.c) */
      last_progress = t;
    }
  }
  int tot = 0, mx = 0, nz[WORLD_W / 16 + 1], nn = 0;
  for (int k = 0; k <= WORLD_W / 16; k++) {
    tot += death_map[k];
    if (death_map[k] > mx) mx = death_map[k];
    nz[nn++] = death_map[k];
  }
  for (int a = 0; a < nn; a++) /* sort for the median */
    for (int b = a + 1; b < nn; b++)
      if (nz[b] < nz[a]) {
        int tmp = nz[a];
        nz[a] = nz[b];
        nz[b] = tmp;
      }
  printf("deaths per 16-column screen:");
  for (int k = 0; k <= WORLD_W / 16; k++) printf(" %d", death_map[k]);
  printf("\n  total %d, median %d, max %d\n", tot, nz[nn / 2], mx);
  printf("soak done ticks=%d wins=%d resets=%d best=%.1f longest_stall=%d fastest_win=%d deaths=%d rescues=%d secrets=%d hash=%u\n", ticks, wins, resets, best, longest_stall, win_best, G.deaths, G.rescues, G.secrets, G.hash);
  fflush(stdout);
  exit(0);
}
#endif

static void init(void *user) {
  (void)user;
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("coopplatformer: input.cfg had errors");
  cw_input_commit_defaults();
  a_left = cw_action_find("left"), a_right = cw_action_find("right"), a_up = cw_action_find("up"), a_down = cw_action_find("down");
  a_jump = cw_action_find("jump"), a_dash = cw_action_find("dash"), a_map = cw_action_find("map"), a_retry = cw_action_find("retry");
  cw_shell_init(&(cw_shell_desc){ .title = "Co-op Platformer", .subtitle = "one to four players", .max_players = 4,
                                  .game_save_size = sizeof(save_t), .game_save_version = 2, .start = start_match,
                                  .quit = quit_match, .sound = sound_hook, .draw_background = title_background });
  g_font = cw_ui_font_sans();
  audio_init();
  cw_debug_i32("g.state", &g_state_dbg);
  cw_debug_i32("g.players", &g_players_dbg);
  cw_debug_i32("g.tick", &G.tick);
  cw_debug_u32("g.abilities", &G.abilities);
  cw_debug_i32("g.coins", &G.coins_total);
  cw_debug_i32("g.kills", &G.kills);
  cw_debug_i32("g.deaths", &G.deaths);
  cw_debug_i32("g.rescues", &G.rescues);
  cw_debug_i32("g.secrets", &G.secrets);
  cw_debug_i32("g.checkpoint", &G.checkpoint);
  cw_debug_i32("g.boss_hp", &G.boss_hp);
  cw_debug_i32("g.won", &G.won);
  cw_debug_u32("g.reached", &G.reached);
  cw_debug_i32("g.hitstops", &G.hitstops);
  cw_debug_u32("g.hash", &G.hash);
  cw_debug_f32_array("g.pos", g_pos_dbg, 2);
  cw_debug_f32_array("g.ppos", g_ppos_dbg, MAX_PLAYERS * 2); /* every player, x y (0 0 when not joined) */
  cw_debug_f32_array("g.cam", g_cam_dbg, 3);
  cw_debug_f32_array("g.view", g_view_dbg, 4);
  cw_debug_field("g.camgate", g_camgate_dbg, CW_DBG_I32, 4);
  cw_debug_f32_array("g.camjit", g_camjit_dbg, 4);
  cw_debug_i32("g.hp", &g_hp_dbg);
  cw_debug_i32("g.bubbles", &g_bubbles_dbg);
  cur_mood = MOODS[0];
#ifdef COOP_AUTOPILOT
  if (getenv("COOP_MAP")) print_map(atoi(getenv("COOP_MAP")));
  if (getenv("COOP_SECRETS")) secret_routes(1);
  if (getenv("COOP_FUZZ")) fuzz(atoi(getenv("COOP_FUZZ")), getenv("COOP_PLAYERS") ? atoi(getenv("COOP_PLAYERS")) : 1);
  if (getenv("COOP_SOAK")) soak(atoi(getenv("COOP_SOAK")), getenv("COOP_PLAYERS") ? atoi(getenv("COOP_PLAYERS")) : 2);
  int n = cfg_int("COOP_PLAYERS", COOP_DEF_PLAYERS);
  cw_shell_start(n < 1 ? 1 : n > 4 ? 4 : n);
#endif
}

static void save_progress(void) {
  save_t *sv = cw_shell_game_save();
  sv->abilities |= G.abilities;
  sv->reached |= G.reached;
  if (G.coins_total > sv->coins) sv->coins = G.coins_total;
  for (int y = 0; y < WORLD_H / 4; y++)
    for (int x = 0; x < WORLD_W / 4; x++) sv->seen[y][x] |= G.seen[y][x];
  if (G.won && (sv->best_ticks == 0 || G.win_tick < sv->best_ticks)) sv->best_ticks = G.win_tick;
  cw_shell_save_commit();
}

static void play_events(void) {
  int e = G.events;
  if (!e) return;
  static const int MAP[][2] = { { 1, SFX_JUMP }, { 2, SFX_LAND }, { 4, SFX_COIN }, { 8, SFX_HURT }, { 16, SFX_SPLASH }, { 32, SFX_DASH },
                                { 64, SFX_POWER }, { 128, SFX_STOMP }, { 256, SFX_BREAK }, { 512, SFX_BUBBLE }, { 1024, SFX_POP },
                                { 2048, SFX_DIE }, { 4096, SFX_WIN }, { 8192, SFX_CHECK }, { 16384, SFX_WALL } };
  for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++)
    if (e & MAP[i][0]) sfx(MAP[i][1], 0.7f);
  if (e & 4) burst(S.fx_spark, (float)G.p[0].pf.body.position.x, (float)G.p[0].pf.body.position.y + 1, 0.5f);
  if (e & 128) burst(S.fx_hit, (float)G.p[0].pf.body.position.x, (float)G.p[0].pf.body.position.y, 0.5f);
  if (e & 64) save_progress();
  if (e & 8192) save_progress();
  if (e & 256) burst(S.fx_burst, (float)G.p[0].pf.body.position.x + 1, (float)G.p[0].pf.body.position.y + 1, 0.5f);
  if (e & 4096) {
    save_progress();
    burst(S.fx_burst, G.cam.x, G.cam.y, 0.5f);
  }
}

/* the map is also the shortcut: left and right pick a flag already touched, Jump travels there (everyone goes) */
static void map_cursor(void) {
  int n = G.L.nchecks, dir = 0;
  if (cw_action_just_pressed(0, a_left)) dir = -1;
  if (cw_action_just_pressed(0, a_right)) dir = 1;
  if (dir && n) {
    int k = g_map_sel < 0 ? (dir > 0 ? -1 : n) : g_map_sel;
    for (int tries = 0; tries < n; tries++) {
      k = (k + dir + n) % n;
      if (G.reached & (1u << k)) {
        g_map_sel = k;
        break;
      }
    }
    sfx(SFX_CHECK, 0.3f);
  }
  bool jump = cw_action_just_pressed(0, a_jump);
  memset(&INPUT[0], 0, sizeof INPUT[0]); /* the cursor is not a step */
  if (jump && g_map_sel >= 0 && sim_warp(&G, g_map_sel)) {
    g_map_open = false;
    sfx(SFX_POP, 0.6f);
  }
}

static void fixed_update(void *user, double dt) {
  (void)user;
  bool run = cw_shell_update();
  g_state_dbg = (int)cw_shell_screen_now(); /* even when the shell owns the tick */
  if (!run) return;
  if (g_attract) return; /* the match has not started yet */
  if (G.nplayers == 0 || !G.world.id) return;
  g_run_ticks++;
#ifdef COOP_AUTOPILOT
  for (int i = 0; i < MAX_PLAYERS; i++) sim_bot_input(&G, i, &INPUT[i]);
#else
  read_input();
  if (cw_action_just_pressed(0, a_map)) {
    g_map_open = !g_map_open;
    g_map_sel = G.checkpoint;
  }
  if (g_map_open) map_cursor();
#endif
  sim_step(&G, INPUT, (float)dt);
  play_events();
  g_state_dbg = (int)cw_shell_screen_now();
  g_players_dbg = G.nplayers;
  g_pos_dbg[0] = (float)G.p[0].pf.body.position.x, g_pos_dbg[1] = (float)G.p[0].pf.body.position.y;
  g_cam_dbg[0] = G.cam.x, g_cam_dbg[1] = G.cam.y, g_cam_dbg[2] = G.cam.z;
  g_hp_dbg = G.p[0].hp;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    bool on = G.p[i].joined && i < G.nplayers;
    g_ppos_dbg[2 * i] = on ? (float)G.p[i].pf.body.position.x : 0, g_ppos_dbg[2 * i + 1] = on ? (float)G.p[i].pf.body.position.y : 0;
  }
  if (G.won && G.tick - G.win_tick > 600) {
    save_progress();
#ifdef COOP_AUTOPILOT
    sim_reset(&G, G.nplayers);
    build_scene();
#else
    cw_shell_to_title();
    G.nplayers = 0;
#endif
  }
}

/* ── HUD ───────────────────────────────────────────────────────────── */
static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align al, float w) {
  cw_r2d_text(g_font, s, (cw_vec2){ x + 2, y + 2 }, &(cw_text_style){ .size = size, .color = cw_rgba(0, 0, 0, 0.6f * c.a), .align = al, .wrap_width = w });
  cw_r2d_text(g_font, s, (cw_vec2){ x, y }, &(cw_text_style){ .size = size, .color = c, .align = al, .wrap_width = w });
}


static cw_color rgba3(const float *c, float a) { return cw_rgba(c[0], c[1], c[2], a); }
static cw_color lerpc(cw_color a, cw_color b, float t) { return cw_rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t); }
static float g_ui_t;

/* a vertical gradient panel: a few bands between two colours, with a rounded top and bottom */
static void panel(float x, float y, float w, float h, float r, cw_color top, cw_color bot, cw_color edge) {
  cw_r2d_round_rect(cw_rect_make(x - 1.5f, y - 1.5f, w + 3, h + 3), r + 1.5f, 8, edge);
  int n = 6;
  cw_r2d_round_rect(cw_rect_make(x, y, w, h), r, 8, bot);
  for (int i = 0; i < n; i++) {
    float t0 = (float)i / (float)n, hh2 = h * 0.5f / (float)n;
    cw_color c = lerpc(top, bot, t0 * 1.6f > 1 ? 1 : t0 * 1.6f);
    c.a *= 0.8f;
    cw_r2d_rect(cw_rect_make(x + r * 0.4f, y + 2 + h * 0.5f * t0 * 0.9f, w - r * 0.8f, hh2 * 0.9f), c);
  }
}
static void heart(cw_vec2 c, float r, cw_color col) {
  cw_r2d_circle((cw_vec2){ c.x - r * 0.5f, c.y - r * 0.25f }, r * 0.56f, 12, col);
  cw_r2d_circle((cw_vec2){ c.x + r * 0.5f, c.y - r * 0.25f }, r * 0.56f, 12, col);
  cw_vec2 tri[3] = { { c.x - r * 1.02f, c.y - r * 0.05f }, { c.x + r * 1.02f, c.y - r * 0.05f }, { c.x, c.y + r * 1.0f } };
  cw_r2d_polygon(tri, 3, col);
}
/* a player's face with its headgear: ears, an antenna, a sprout or horns (the marker that is not a colour) */
static void portrait(cw_vec2 c, float r, int i, bool dim) {
  cw_color fur = rgba3(PLAYER_RGB[i], dim ? 0.45f : 1), acc = i == 0 ? cw_rgba(1, 0.85f, 0.25f, 1) : i == 1 ? cw_rgba(0.95f, 0.95f, 0.95f, 1) : i == 2 ? cw_rgba(1, 0.45f, 0.65f, 1) : cw_rgba(0.3f, 0.8f, 0.95f, 1);
  if (dim) acc.a = 0.5f;
  if (i == 1) { /* antenna */
    cw_r2d_line((cw_vec2){ c.x, c.y - r * 0.8f }, (cw_vec2){ c.x, c.y - r * 1.5f }, r * 0.12f, acc);
    cw_r2d_circle((cw_vec2){ c.x, c.y - r * 1.55f }, r * 0.22f, 8, acc);
  } else if (i == 0) { /* ears */
    for (int k = -1; k <= 1; k += 2) {
      cw_vec2 t[3] = { { c.x + k * r * 0.35f, c.y - r * 0.7f }, { c.x + k * r * 0.95f, c.y - r * 0.7f }, { c.x + k * r * 0.75f, c.y - r * 1.4f } };
      cw_r2d_polygon(t, 3, acc);
    }
  } else if (i == 2) { /* sprout */
    for (int k = -1; k <= 1; k += 2) cw_r2d_circle((cw_vec2){ c.x + k * r * 0.35f, c.y - r * 1.0f }, r * 0.3f, 8, acc);
  } else { /* horns */
    for (int k = -1; k <= 1; k += 2) {
      cw_vec2 t[3] = { { c.x + k * r * 0.55f, c.y - r * 0.6f }, { c.x + k * r * 1.0f, c.y - r * 0.5f }, { c.x + k * r * 1.1f, c.y - r * 1.3f } };
      cw_r2d_polygon(t, 3, acc);
    }
  }
  cw_r2d_circle(c, r, 18, fur);
  cw_r2d_circle((cw_vec2){ c.x, c.y + r * 0.3f }, r * 0.5f, 12, cw_rgba(0.96f, 0.9f, 0.78f, dim ? 0.5f : 1));
  cw_r2d_circle((cw_vec2){ c.x - r * 0.35f, c.y - r * 0.1f }, r * 0.22f, 8, cw_rgba(1, 1, 1, 1));
  cw_r2d_circle((cw_vec2){ c.x + r * 0.35f, c.y - r * 0.1f }, r * 0.22f, 8, cw_rgba(1, 1, 1, 1));
  cw_r2d_circle((cw_vec2){ c.x - r * 0.3f, c.y - r * 0.08f }, r * 0.1f, 6, cw_rgba(0.05f, 0.05f, 0.1f, 1));
  cw_r2d_circle((cw_vec2){ c.x + r * 0.3f, c.y - r * 0.08f }, r * 0.1f, 6, cw_rgba(0.05f, 0.05f, 0.1f, 1));
}
static void coin_icon(cw_vec2 c, float r) {
  cw_r2d_circle(c, r, 14, cw_rgba(0.75f, 0.5f, 0.1f, 1));
  cw_r2d_circle(c, r * 0.82f, 14, cw_rgba(1, 0.8f, 0.25f, 1));
  cw_r2d_circle((cw_vec2){ c.x - r * 0.2f, c.y - r * 0.2f }, r * 0.35f, 8, cw_rgba(1, 0.95f, 0.7f, 0.9f));
}

static const char *const REGION_NAMES[BIOMES] = { "Sunlit Meadow", "Glimmer Caves", "Drowned Ruins", "Ember Crater" };

static cw_color tile_color(int t, int biome) {
  switch (t) {
    case T_ROCK: case T_GRASS: case T_BRICK: return biome == BIOME_MEADOW ? cw_rgba(0.3f, 0.6f, 0.28f, 1) : biome == BIOME_CAVES ? cw_rgba(0.35f, 0.45f, 0.7f, 1) : biome == BIOME_RUINS ? cw_rgba(0.8f, 0.7f, 0.48f, 1) : cw_rgba(0.7f, 0.28f, 0.2f, 1);
    case T_WATER: return cw_rgba(0.2f, 0.55f, 0.95f, 1);
    case T_LAVA: return cw_rgba(1, 0.45f, 0.1f, 1);
    case T_SPIKE: return cw_rgba(0.9f, 0.9f, 0.95f, 1);
    case T_ONEWAY: return cw_rgba(0.75f, 0.52f, 0.3f, 1);
    default: return cw_rgba(0, 0, 0, 0);
  }
}

/* each player has a colour AND a shape and number, so four of them stay apart for anyone: 1 triangle, 2 diamond,
 * 3 square, 4 circle */
static void glyph(int i, float cx, float cy, float r, cw_color c) {
  switch (i) {
    case 0: {
      cw_vec2 t[3] = { { cx - r, cy - r * 0.8f }, { cx + r, cy - r * 0.8f }, { cx, cy + r } };
      cw_r2d_polygon(t, 3, c);
      break;
    }
    case 1: {
      cw_vec2 d[4] = { { cx, cy - r }, { cx + r, cy }, { cx, cy + r }, { cx - r, cy } };
      cw_r2d_polygon(d, 4, c);
      break;
    }
    case 2: cw_r2d_rect(cw_rect_make(cx - r * 0.85f, cy - r * 0.85f, r * 1.7f, r * 1.7f), c); break;
    default: cw_r2d_circle((cw_vec2){ cx, cy }, r, 16, c); break;
  }
}

/* the world point as a pixel of the HUD canvas, through the drawn camera (vertical fov 40, pitched down 0.06) */
static bool project(float wx, float wy, float wz, float W, float H, float *sx, float *sy) {
  const float pitch = 0.06f, tf = 0.36397f; /* tan(20 degrees) */
  float dx = wx - g_view_dbg[0], dy = wy - g_view_dbg[1], dz = wz - g_view_dbg[2]; /* the camera as drawn this frame */
  float yv = dy * cosf(pitch) - dz * sinf(pitch), zv = dy * sinf(pitch) + dz * cosf(pitch);
  if (zv > -0.5f) return false;
  *sx = (0.5f + 0.5f * (dx / -zv) / (tf * W / H)) * W;
  *sy = (0.5f - 0.5f * (yv / -zv) / tf) * H;
  return true;
}

static void draw_markers(float W, float H, float s) {
  /* project every marker first, then push markers that would overlap apart along the row (players in one spot get
   * one readable marker each, side by side, never a stack) */
  float px[MAX_PLAYERS], py[MAX_PLAYERS];
  int id[MAX_PLAYERS], n = 0;
  float r = 11 * s;
  for (int i = 0; i < G.nplayers && i < MAX_PLAYERS; i++) {
    const player *p = &G.p[i];
    if (!p->joined || (!p->alive && !p->bubble)) continue;
    float wx = p->bubble ? p->bubble_x : (float)p->pf.body.position.x + S.spread_x[i], wy = (p->bubble ? p->bubble_y + 0.9f : (float)p->pf.body.position.y) + 2.15f;
    float sx, sy;
    if (!project(wx, wy, 0, W, H, &sx, &sy)) continue;
    if (sy < r + 60 * s) sy = r + 60 * s;
    px[n] = sx, py[n] = sy, id[n] = i, n++;
  }
  for (int a = 0; a < n; a++) /* by screen x, so the spreading keeps the order the players stand in */
    for (int b = a + 1; b < n; b++)
      if (px[b] < px[a]) {
        float t = px[a]; px[a] = px[b], px[b] = t;
        t = py[a]; py[a] = py[b], py[b] = t;
        int k = id[a]; id[a] = id[b], id[b] = k;
      }
  float gap = 2.5f * r;
  for (int pass = 0; pass < 4; pass++)
    for (int a = 0; a + 1 < n; a++) {
      float over = gap - (px[a + 1] - px[a]);
      if (over > 0 && fabsf(py[a + 1] - py[a]) < gap) px[a] -= over * 0.5f, px[a + 1] += over * 0.5f;
    }
  for (int k = 0; k < n; k++) {
    int i = id[k];
    float sx = px[k], sy = py[k];
    if (sx < r) sx = r;
    if (sx > W - r) sx = W - r;
    cw_color c = cw_rgba(PLAYER_RGB[i][0], PLAYER_RGB[i][1], PLAYER_RGB[i][2], 1);
    glyph(i, sx, sy, r + 2 * s, cw_rgba(0, 0, 0, 0.75f)); /* a dark outline so it reads on any background */
    glyph(i, sx, sy, r, c);
    char b[12];
    snprintf(b, sizeof b, "%d", i + 1);
    cw_r2d_text(g_font, b, (cw_vec2){ sx - r, sy - r * 0.62f }, &(cw_text_style){ .size = 15 * s, .color = cw_rgba(0.05f, 0.05f, 0.08f, 1), .align = CW_ALIGN_CENTER, .wrap_width = 2 * r });
  }
}

static void draw_map(float W, float H) {
  float s = H / 540.0f;
  cw_r2d_rect(cw_rect_make(0, 0, W, H), cw_rgba(0.01f, 0.02f, 0.05f, 0.9f));
  float mw = W * 0.9f, cell = mw / (WORLD_W / 4);
  float ox = (W - mw) * 0.5f, oy = H * 0.52f - cell * (WORLD_H / 4) * 0.5f;
  const save_t *sv = cw_shell_game_save();
  panel(ox - 10 * s, oy - 10 * s, mw + 20 * s, cell * (WORLD_H / 4) + 20 * s, 12 * s, cw_rgba(0.08f, 0.12f, 0.2f, 0.95f), cw_rgba(0.03f, 0.05f, 0.09f, 0.95f), cw_rgba(0.5f, 0.65f, 0.9f, 0.5f));
  for (int by = 0; by < WORLD_H / 4; by++)
    for (int bx = 0; bx < WORLD_W / 4; bx++) {
      if (!G.seen[by][bx] && !sv->seen[by][bx]) continue;
      int best = T_AIR, bc = 0, cnt[9] = { 0 };
      for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) cnt[G.L.t[by * 4 + y][bx * 4 + x]]++;
      for (int t = 1; t < 9; t++)
        if (cnt[t] > bc) bc = cnt[t], best = t;
      float px = ox + (float)bx * cell, py = oy + (float)(WORLD_H / 4 - 1 - by) * cell;
      int bi = G.L.biome[bx * 4];
      cw_color air = lerpc(cw_rgba(0.1f, 0.13f, 0.22f, 1), rgba3(MOODS[bi].fog, 1), 0.25f);
      cw_r2d_rect(cw_rect_make(px, py, cell + 0.5f, cell + 0.5f), best == T_AIR ? air : tile_color(best, bi));
    }
  for (int k = 0; k < G.L.npickups; k++) {
    const pickup *pk = &G.L.pickups[k];
    float px = ox + (float)pk->x / 4 * cell, py = oy + ((float)WORLD_H / 4 - (float)pk->y / 4) * cell;
    if (G.seen[pk->y / 4][pk->x / 4]) {
      float pulse = 1.0f + 0.2f * sinf(g_ui_t * 4);
      cw_r2d_circle((cw_vec2){ px, py }, cell * 1.0f * pulse, 10, pk->taken ? cw_rgba(0.5f, 0.5f, 0.5f, 0.7f) : cw_rgba(0.4f, 0.9f, 1, 0.45f));
      cw_r2d_circle((cw_vec2){ px, py }, cell * 0.55f, 8, pk->taken ? cw_rgba(0.6f, 0.6f, 0.6f, 1) : cw_rgba(0.7f, 1, 1, 1));
    }
  }
  for (int i = 0; i < G.nplayers; i++) {
    const player *p = &G.p[i];
    float px = ox + (float)p->pf.body.position.x / 4 * cell, py = oy + ((float)WORLD_H / 4 - (float)p->pf.body.position.y / 4) * cell;
    cw_r2d_circle((cw_vec2){ px, py }, cell * (1.5f + 0.25f * sinf(g_ui_t * 5 + (float)i)), 16, rgba3(PLAYER_RGB[i], 0.35f));
    portrait((cw_vec2){ px, py }, cell * 1.05f, i, false);
  }
  /* flags already touched: the map is also the shortcut (left and right pick one, Jump travels there) */
  for (int k = 0; k < G.L.nchecks; k++) {
    if (!(G.reached & (1u << k))) continue;
    float px = ox + (float)G.L.checks[k].x / 4 * cell, py = oy + ((float)WORLD_H / 4 - (float)G.L.checks[k].y / 4) * cell;
    bool sel = k == g_map_sel;
    cw_r2d_line((cw_vec2){ px, py }, (cw_vec2){ px, py - cell * 2.6f }, 1.5f, cw_rgba(1, 1, 1, 1));
    cw_vec2 fl[3] = { { px, py - cell * 2.6f }, { px + cell * 1.8f, py - cell * 2.0f }, { px, py - cell * 1.4f } };
    cw_r2d_polygon(fl, 3, sel ? cw_rgba(1, 0.85f, 0.2f, 1) : cw_rgba(0.9f, 0.9f, 0.95f, 1));
    if (sel) cw_r2d_circle_lines((cw_vec2){ px, py - cell }, cell * 2.6f, 2, 20, cw_rgba(1, 0.85f, 0.2f, 1));
  }
  /* region names under the map, coloured by their light */
  for (int b = 0; b < BIOMES; b++) {
    int x0 = -1, x1 = -1;
    for (int x = 0; x < WORLD_W; x++)
      if (G.L.biome[x] == b && x < WORLD_W - 4) { if (x0 < 0) x0 = x; x1 = x; }
    if (x0 < 0) continue;
    float cx = ox + ((float)(x0 + x1) * 0.5f) / 4 * cell;
    text(REGION_NAMES[b], cx - 110 * s, oy + cell * (WORLD_H / 4) + 16 * s, 16 * s, lerpc(rgba3(MOODS[b].sun, 1), cw_rgba(1, 1, 1, 1), 0.4f), CW_ALIGN_CENTER, 220 * s);
  }
  text("MAP", 0, H * 0.035f, 30 * s, cw_rgba(1, 0.95f, 0.8f, 1), CW_ALIGN_CENTER, W);
  text("left and right pick a flag you have touched, Jump travels there   (M or Back to close)", 0, H * 0.93f, 16 * s, cw_rgba(0.8f, 0.85f, 1, 0.9f), CW_ALIGN_CENTER, W);
}

/* the title screen's picture: a painted dusk meadow with three drifting ridges, a sun, fireflies and the four heroes */
static void title_background(cw_shell_screen scr, cw_vec2 cv, void *user) {
  (void)user;
  float W = cv.x, H = cv.y, s = H / 540.0f, t = g_ui_t;
  if (g_attract && G.world.id && S.built) {
    /* the 3D world plays behind the menu: shade the top for the title and the bottom for the hints, nothing else */
    for (int i = 0; i < 48; i++) { /* bands on whole pixels, edge to edge, so no seam is drawn twice */
      float f = (float)i / 48.0f;
      float t0 = floorf(H * 0.42f * f), t1 = floorf(H * 0.42f * (f + 1.0f / 48.0f));
      float b0 = floorf(H - H * 0.3f * (f + 1.0f / 48.0f)), b1 = floorf(H - H * 0.3f * f);
      cw_r2d_rect(cw_rect_make(0, t0, W, t1 - t0), cw_rgba(0.02f, 0.03f, 0.08f, 0.62f * (1 - f) * (1 - f)));
      cw_r2d_rect(cw_rect_make(0, b0, W, b1 - b0), cw_rgba(0.02f, 0.03f, 0.06f, 0.55f * (1 - f) * (1 - f)));
    }
    if (scr == CW_SHELL_JOIN) {
      int joined = cw_shell_players();
      for (int i = 0; i < 4; i++) {
        bool in = i < joined;
        float x = W * (0.125f + 0.25f * (float)i), y = H * 0.625f - (in ? 7 * s * fabsf(sinf(t * 3.0f + (float)i * 0.9f)) : 0);
        portrait((cw_vec2){ x, y }, 21 * s, i, !in);
      }
    }
    return;
  }
  static const float SKY[6][3] = { { 0.1f, 0.12f, 0.35f }, { 0.28f, 0.2f, 0.5f }, { 0.65f, 0.3f, 0.5f }, { 0.95f, 0.5f, 0.35f }, { 1.0f, 0.75f, 0.45f }, { 1.0f, 0.88f, 0.6f } };
  for (int i = 0; i < 80; i++) {
    float f = (float)i / 80.0f * 5.0f;
    int k0 = (int)f > 4 ? 4 : (int)f;
    float fr = f - (float)k0;
    cw_color c = cw_rgba(SKY[k0][0] + (SKY[k0 + 1][0] - SKY[k0][0]) * fr, SKY[k0][1] + (SKY[k0 + 1][1] - SKY[k0][1]) * fr, SKY[k0][2] + (SKY[k0 + 1][2] - SKY[k0][2]) * fr, 1);
    cw_r2d_rect(cw_rect_make(0, H * 0.78f * (float)i / 80.0f, W, H * 0.78f / 80.0f + 1.5f), c);
  }
  cw_r2d_rect(cw_rect_make(0, H * 0.78f, W, H * 0.22f + 1), cw_rgba(0.12f, 0.2f, 0.22f, 1));
  cw_vec2 sun = { W * 0.72f, H * 0.52f };
  for (int k = 6; k >= 1; k--) cw_r2d_circle(sun, 28 * s * (float)k * 0.9f, 28, cw_rgba(1, 0.8f, 0.45f, 0.06f));
  cw_r2d_circle(sun, 34 * s, 28, cw_rgba(1, 0.95f, 0.75f, 1));
  static const float RC[3][3] = { { 0.4f, 0.3f, 0.55f }, { 0.2f, 0.25f, 0.4f }, { 0.08f, 0.14f, 0.2f } };
  for (int l = 0; l < 3; l++) {
    enum { N = 26 };
    cw_vec2 p[N + 2];
    float base = H * (0.58f + 0.1f * (float)l), amp = H * (0.12f - 0.02f * (float)l);
    for (int i = 0; i <= N; i++) {
      float x = W * (float)i / (float)N, ph = x / W * (4.0f + (float)l * 2.0f) + t * (0.02f + 0.015f * (float)l) + (float)l * 1.7f;
      p[i] = (cw_vec2){ x, base - amp * (0.5f + 0.5f * sinf(ph * 2.0f) * cosf(ph * 0.7f + 1.0f)) };
    }
    p[N + 1] = (cw_vec2){ W, H };
    cw_vec2 poly[N + 3];
    poly[0] = (cw_vec2){ 0, H };
    for (int i = 0; i <= N + 1; i++) poly[i + 1] = p[i];
    cw_r2d_polygon(poly, N + 3, rgba3(RC[l], 1));
  }
  for (int i = 0; i < 40; i++) {
    float fx = fmodf((float)i * 97.3f + t * (8.0f + (float)(i % 5) * 3.0f), W), fy = H * (0.45f + 0.5f * fabsf(sinf((float)i * 1.3f + t * 0.3f + (float)i)));
    float a = 0.4f + 0.6f * fabsf(sinf(t * 1.5f + (float)i));
    cw_r2d_circle((cw_vec2){ fx, fy }, 2.2f * s, 6, cw_rgba(1, 0.95f, 0.5f, a));
    cw_r2d_circle((cw_vec2){ fx, fy }, 6 * s, 8, cw_rgba(1, 0.9f, 0.4f, 0.12f * a));
  }
  if (scr == CW_SHELL_JOIN) {
    /* on the join screen each hero stands in its own slot card (the shell draws four cards across, 0.3 to 0.7 of the
     * height, its labels at 0.36 and 0.5), clear of the hint line at the bottom; a joined hero hops, an open slot waits
     * dimmed */
    int joined = cw_shell_players();
    for (int i = 0; i < 4; i++) {
      bool in = i < joined;
      float x = W * (0.125f + 0.25f * (float)i), y = H * 0.625f - (in ? 7 * s * fabsf(sinf(t * 3.0f + (float)i * 0.9f)) : 0);
      cw_r2d_circle((cw_vec2){ x, H * 0.672f }, 22 * s, 16, cw_rgba(0, 0, 0, 0.22f));
      portrait((cw_vec2){ x, y }, 21 * s, i, !in);
    }
    return;
  }
  for (int i = 0; i < 4; i++) {
    float x = W * (0.22f + 0.19f * (float)i), y = H * 0.84f - 8 * s * fabsf(sinf(t * 2.0f + (float)i * 0.9f));
    cw_r2d_circle((cw_vec2){ x, H * 0.9f }, 30 * s, 16, cw_rgba(0, 0, 0, 0.25f));
    portrait((cw_vec2){ x, y }, 26 * s, i, false);
  }
}

/* attract mode behind the title, join and settings screens: the real world, four bots with every ability running
 * the meadow and the first cave, looped. The shell pauses the tree under its menus, which stops fixed_update, so this
 * runs from render, one fixed step per frame. Its sounds stay off; a match start (start_match) replaces it. */
static void attract_step(void) {
  cw_shell_screen scr = cw_shell_screen_now();
  if (scr != CW_SHELL_TITLE && scr != CW_SHELL_JOIN && scr != CW_SHELL_SETTINGS) return;
  if (!g_attract || !G.world.id || G.nplayers == 0 || G.tick > 60 * 75) {
    G.abilities = AB_DOUBLE | AB_CLAWS | AB_DASH;
    sim_reset(&G, MAX_PLAYERS);
    G.abilities = AB_DOUBLE | AB_CLAWS | AB_DASH;
    for (int i = 0; i < G.nplayers; i++) {
      G.p[i].pf.can_dash = G.p[i].pf.can_wall_jump = true, G.p[i].pf.max_air_jumps = 1;
      G.p[i].invuln = 0;
    }
    build_scene();
    g_attract = true;
  }
  for (int i = 0; i < MAX_PLAYERS; i++) sim_bot_input(&G, i, &INPUT[i]);
  sim_step(&G, INPUT, 1.0f / 60.0f);
  G.events = 0;
}

static void render(void *user, double alpha) {
  (void)user;
  float dt = 1.0f / 60.0f;
  g_ui_t += dt;
  attract_step();
  if (G.nplayers && G.world.id && S.built) sync_scene((float)alpha, dt);
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  if (G.nplayers && G.world.id && !g_attract && cw_shell_screen_now() != CW_SHELL_TITLE) {
    /* a panel per player: face, hearts, coins */
    for (int i = 0; i < G.nplayers; i++) {
      const player *p = &G.p[i];
      float x0 = 20 * s + (float)i * 176 * s, y0 = 16 * s, pw = 166 * s, ph = 58 * s;
      panel(x0, y0, pw, ph, 12 * s, cw_rgba(0.16f, 0.2f, 0.3f, 0.82f), cw_rgba(0.04f, 0.05f, 0.09f, 0.82f), rgba3(PLAYER_RGB[i], p->alive || p->bubble ? 0.9f : 0.3f));
      portrait((cw_vec2){ x0 + 28 * s, y0 + 34 * s }, 17 * s, i, !(p->alive || p->bubble));
      for (int h = 0; h < 3; h++) {
        cw_vec2 hc = { x0 + (70 + 26 * (float)h) * s, y0 + 19 * s };
        if (h < p->hp) {
          heart(hc, 9 * s, cw_rgba(1, 0.3f, 0.38f, 1));
          cw_r2d_circle((cw_vec2){ hc.x - 4 * s, hc.y - 4 * s }, 2.2f * s, 6, cw_rgba(1, 0.85f, 0.9f, 0.9f));
        } else heart(hc, 9 * s, cw_rgba(0.25f, 0.27f, 0.36f, 0.9f));
      }
      char b[32];
      snprintf(b, sizeof b, "%d", p->coins);
      coin_icon((cw_vec2){ x0 + 78 * s, y0 + 44 * s }, 7 * s);
      text(b, x0 + 90 * s, y0 + 31 * s, 18 * s, cw_rgba(1, 0.95f, 0.8f, 1), CW_ALIGN_LEFT, 0);
    }
    /* the abilities: a chip each, lit when found */
    static const char *const AB[3] = { "AIR JUMP", "CLAWS", "DASH" };
    for (int k = 0; k < 3; k++) {
      bool on = (G.abilities >> k) & 1;
      float cw_ = 76 * s, x = W - (16 + (float)(3 - k) * 82) * s, y = (G.nplayers > 3 ? 84 : 20) * s; /* below the panels when four players fill the top */
      panel(x, y, cw_, 30 * s, 10 * s, on ? cw_rgba(0.2f, 0.45f, 0.6f, 0.9f) : cw_rgba(0.12f, 0.14f, 0.2f, 0.6f), on ? cw_rgba(0.05f, 0.12f, 0.2f, 0.9f) : cw_rgba(0.05f, 0.06f, 0.09f, 0.6f), on ? cw_rgba(0.5f, 0.9f, 1, 0.8f) : cw_rgba(0.4f, 0.4f, 0.5f, 0.3f));
      text(AB[k], x, y + 7 * s, 14 * s, on ? cw_rgba(0.85f, 0.98f, 1, 1) : cw_rgba(0.5f, 0.55f, 0.65f, 0.8f), CW_ALIGN_CENTER, cw_);
    }
    /* the region you are in, fading in at the top when it changes */
    {
      static int last_reg = -1;
      static float reg_t = 0;
      int b = G.L.biome[(int)G.cam.x < 0 ? 0 : (int)G.cam.x >= WORLD_W ? WORLD_W - 1 : (int)G.cam.x];
      if (b != last_reg) last_reg = b, reg_t = 3.2f;
      if (reg_t > 0) {
        reg_t -= dt;
        float a = reg_t > 2.6f ? (3.2f - reg_t) / 0.6f : reg_t < 0.8f ? reg_t / 0.8f : 1.0f;
        text(REGION_NAMES[b], 0, H * 0.2f, 34 * s, cw_rgba(1, 0.96f, 0.85f, a), CW_ALIGN_CENTER, W);
        cw_r2d_rect(cw_rect_make(W * 0.5f - 90 * s, H * 0.2f + 44 * s, 180 * s, 2 * s), cw_rgba(1, 0.85f, 0.5f, a * 0.8f));
      }
    }
    /* boss bar */
    for (int i = 0; i < G.L.nenemies; i++)
      if (G.L.enemies[i].kind == 2 && G.L.enemies[i].alive && fabsf(G.cam.x - G.L.enemies[i].x) < 30) {
        float bw = W * 0.5f, bx = (W - bw) * 0.5f, by = H - 52 * s;
        float f = (float)G.L.enemies[i].hp / 9.0f;
        panel(bx - 6 * s, by - 6 * s, bw + 12 * s, 28 * s, 10 * s, cw_rgba(0.2f, 0.1f, 0.1f, 0.9f), cw_rgba(0.06f, 0.03f, 0.03f, 0.9f), cw_rgba(1, 0.5f, 0.3f, 0.6f));
        cw_r2d_round_rect(cw_rect_make(bx, by, bw * f, 16 * s), 7 * s, 8, cw_rgba(0.8f, 0.12f, 0.1f, 1));
        cw_r2d_round_rect(cw_rect_make(bx, by, bw * f, 7 * s), 5 * s, 8, cw_rgba(1, 0.5f, 0.3f, 0.9f));
        text("THE WARDEN", 0, by - 30 * s, 17 * s, cw_rgba(1, 0.85f, 0.7f, 1), CW_ALIGN_CENTER, W);
      }
    draw_markers(W, H, s);
    text("hold R or Y: back to the last flag", 20 * s, H - 30 * s, 14 * s, cw_rgba(1, 1, 1, 0.45f), CW_ALIGN_LEFT, 0);
    /* players carried in bubbles */
    for (int i = 0; i < G.nplayers; i++)
      if (G.p[i].bubble && G.p[i].joined) {
        float x = W * 0.5f + (float)(i - G.nplayers / 2) * 90 * s;
        portrait((cw_vec2){ x - 54 * s, H - 100 * s }, 15 * s, i, false);
        text("RESCUE", x - 34 * s, H - 110 * s, 18 * s, rgba3(PLAYER_RGB[i], 1), CW_ALIGN_LEFT, 0);
      }
    if (G.won) {
      float k = fminf(1.0f, (float)(G.tick - G.win_tick) / 40.0f);
      panel(0, H * 0.28f, W, H * 0.34f * k, 0, cw_rgba(0.1f, 0.08f, 0.02f, 0.88f), cw_rgba(0.02f, 0.02f, 0.03f, 0.88f), cw_rgba(1, 0.8f, 0.3f, 0.7f));
      if (k > 0.9f) {
        text("THE DEEP IS QUIET", 0, H * 0.31f, 52 * s, cw_rgba(1, 0.85f, 0.3f, 1), CW_ALIGN_CENTER, W);
        char t[96];
        coin_icon((cw_vec2){ W * 0.5f - 250 * s, H * 0.485f }, 11 * s);
        snprintf(t, sizeof t, "coins %d     secrets %d     rescues %d     falls %d", G.coins_total, G.secrets, G.rescues, G.deaths);
        text(t, 0, H * 0.46f, 24 * s, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER, W);
        for (int i = 0; i < G.nplayers; i++) portrait((cw_vec2){ W * 0.5f + ((float)i - (float)(G.nplayers - 1) * 0.5f) * 64 * s, H * 0.56f }, 18 * s, i, false);
      }
    }
    if (g_map_open) draw_map(W, H);
  }
  cw_r2d_end();
  cw_shell_draw();
}

static void shutdown_app(void *user) {
  (void)user;
  world_destroy_physics(&G);
  if (A.on) cw_audio_shutdown();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc base = { .title = "Co-op Platformer", .width = 960, .height = 540, .debug = true, .init = init,
                              .fixed_update = fixed_update, .render = render, .shutdown = shutdown_app };
  base.save = cw_shell_save_region();
  base.save_size = cw_shell_save_region_size();
  return cw_scene_app(NULL, &base);
}
