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

static cw_action a_left, a_right, a_up, a_down, a_jump, a_dash, a_map;
static input_state INPUT[MAX_PLAYERS];
static bool g_map_open;
static cw_font g_font;
static void title_background(cw_shell_screen scr, cw_vec2 cv, void *user);
static int g_state_dbg, g_players_dbg, g_bubbles_dbg, g_hp_dbg;
static float g_pos_dbg[2], g_cam_dbg[3];
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
    -0.7f, 0.55f, { 0.55f, 0.75f, 1.0f }, 0.9f, 0.0012f, 0.9f, 0.5f, { 0.85f, 0.92f, 1.0f }, 0.012f, 0.0f },
  /* caves: cold dark, the torches and crystals are the light, a cyan rim */
  { { 0.04f, 0.05f, 0.1f }, { 0.04f, 0.06f, 0.13f }, { 0.4f, 0.55f, 1.0f }, 0.4f, 0.14f, 0.35f, 1.7f, 0.2f, 24, 280, 1.0f,
    -0.9f, 0.2f, { 0.25f, 0.65f, 1.0f }, 1.6f, 0.003f, 0.5f, 0.3f, { 0.3f, 0.5f, 0.9f }, 0.02f, 0.0f },
  /* ruins: green daylight through broken stone, hard shafts */
  { { 0.22f, 0.42f, 0.46f }, { 0.4f, 0.62f, 0.6f }, { 1.0f, 0.95f, 0.78f }, 3.6f, 0.4f, 0.9f, 1.05f, 0.12f, 45, 360, 0.5f,
    -1.0f, -0.45f, { 0.7f, 1.0f, 0.85f }, 1.0f, 0.0025f, 0.7f, 0.4f, { 0.8f, 1.0f, 0.9f }, 0.014f, 0.0f },
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
      cw_node_set(w, "color", cw_val_color(0.35f, 0.75f, 0.85f, 1));
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
        cw_node_set(v, "density", cw_val_float(0.25));
        cw_node_set(v, "albedo", cw_val_color(0.1f, 0.45f, 0.65f, 1));
        cw_node_set(v, "emission", cw_val_color(0.02f, 0.14f, 0.22f, 1));
        cw_node_set(v, "emission_energy", cw_val_float(0.6));
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

/* ── frame: sync the scene to the sim ─────────────────────────────── */
static void sync_scene(float alpha, float dt) {
  game *g = &G;
  const level *L = &g->L;
  S.time += dt;
  camera_rig *c = &g->cam;
  float sh = c->shake;
  float ox = sh * sinf(S.time * 61.0f) * 0.5f, oy = sh * cosf(S.time * 53.0f) * 0.5f;
  cw_node3d_set_position(S.cam, (t3_vec3){ c->x + ox, c->y + oy, c->z });
  cw_node3d_set_rotation(S.cam, (t3_vec3){ -0.06f, 0, 0 });
  cw_node3d_set_position(S.sun, (t3_vec3){ c->x + 8, c->y + 18, 14 });
  apply_mood(c->x);
  world_update_water_mesh(S.time);
  world_update_backdrop(S.time);
  update_spans(c->x);
  /* the motes follow the camera and take the region's character */
  {
    int b = g->L.biome[(int)c->x < 0 ? 0 : (int)c->x >= WORLD_W ? WORLD_W - 1 : (int)c->x];
    static const float MC[BIOMES][4] = { { 1.0f, 0.95f, 0.5f, 3.0f }, { 0.4f, 0.8f, 1.0f, 2.0f }, { 1.0f, 1.0f, 0.8f, 1.2f }, { 1.0f, 0.4f, 0.1f, 4.0f } };
    static const float MG[BIOMES] = { 0.0f, 0.0f, -0.05f, 0.9f };
    static int last_b = -1;
    cw_node3d_set_position(S.fx_motes, (t3_vec3){ c->x, c->y, 0 });
    if (b != last_b) {
      last_b = b;
      cw_node_set(S.fx_motes, "color", cw_val_color(MC[b][0], MC[b][1], MC[b][2], 1));
      cw_node_set(S.fx_motes, "energy", cw_val_float(MC[b][3]));
      cw_node_set(S.fx_motes, "gravity", cw_val_vec3(0, MG[b], 0));
    }
  }
  /* players */
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
    cw_node3d_set_position(S.pnode[i], (t3_vec3){ x, y, 0 });
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
      bool inv = p->invuln > 0 && ((p->invuln / 4) & 1);
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
      burst(S.fx_debris, e->x, by, 0.5f);
      g->cam.shake = fmaxf(g->cam.shake, 0.25f * bs);
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
  }
}

/* ── the shell and the game flow ──────────────────────────────────── */
static void start_match(int players, void *user) {
  (void)user;
  save_t *sv = cw_shell_game_save();
  G.abilities = 0;
  sim_reset(&G, players);
  if (sv->abilities && !getenv("COOP_FRESH")) G.abilities = sv->abilities;
  if (getenv("COOP_ABILITIES")) G.abilities = (uint32_t)atoi(getenv("COOP_ABILITIES"));
  if (getenv("COOP_START_X")) { /* a test or a screenshot starts anywhere */
    int sx = atoi(getenv("COOP_START_X"));
    if (sx < 2) sx = 2;
    if (sx > WORLD_W - 3) sx = WORLD_W - 3;
    for (int i = 0; i < G.nplayers; i++) {
      cw_platformer2d_teleport(&G.p[i].pf, (cw_pos2){ (double)sx + 0.7 * i, (double)G.L.ground[sx] + 0.05 });
      G.p[i].invuln = 120;
    }
    G.cam.x = G.cam.tx = (float)sx, G.cam.y = G.cam.ty = (float)G.L.ground[sx] + 3;
    G.checkpoint = -1;
    for (int k = 0; k < G.L.nchecks; k++)
      if (G.L.checks[k].x < sx) G.checkpoint = k;
  }
  build_scene();
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
/* COOP_SOAK=<ticks>: run the bots through the whole game with no rendering and report (soft-lock and crash check). */
static void soak(int ticks, int players) {
  sim_reset(&G, players);
  float best = 0;
  int last_progress = 0, longest_stall = 0, wins = 0, resets = 0;
  for (int t = 0; t < ticks; t++) {
    for (int i = 0; i < MAX_PLAYERS; i++) sim_bot_input(&G, i, &INPUT[i]);
    sim_step(&G, INPUT, 1.0f / 60.0f);
    if (getenv("COOP_TRACE") && t >= atoi(getenv("COOP_TRACE")) && t < atoi(getenv("COOP_TRACE")) + 90) {
      const player *q = &G.p[getenv("COOP_TRACE_P") ? atoi(getenv("COOP_TRACE_P")) : 0];
      printf("tr t=%d pos=%.2f,%.2f vel=%.2f,%.2f fl=%d wl=%d ws=%d wc=%.2f fac=%d in=(%.0f,j%d%d) ev=%x\n", t, q->pf.body.position.x, q->pf.body.position.y, q->pf.body.velocity.x, q->pf.body.velocity.y, q->pf.body.on_floor, q->pf.body.on_wall, q->pf.wall_side, q->pf.wall_coyote, q->pf.facing, INPUT[getenv("COOP_TRACE_P") ? atoi(getenv("COOP_TRACE_P")) : 0].x, INPUT[getenv("COOP_TRACE_P") ? atoi(getenv("COOP_TRACE_P")) : 0].jump_pressed, INPUT[getenv("COOP_TRACE_P") ? atoi(getenv("COOP_TRACE_P")) : 0].jump_held, q->pf.events);
    }
    float lead = -1;
    for (int i = 0; i < G.nplayers; i++) {
      float x = (float)G.p[i].pf.body.position.x;
      if (x > lead) lead = x;
    }
    if (lead > best + 0.5f) best = lead, last_progress = t;
    if (t - last_progress > longest_stall) longest_stall = t - last_progress;
    if (t % 3600 == 0) printf("soak t=%d lead=%.1f best=%.1f coins=%d deaths=%d rescues=%d abil=%u ck=%d boss=%d won=%d\n", t, lead, best, G.coins_total, G.deaths, G.rescues, G.abilities, G.checkpoint, G.boss_hp, G.won);
    if (G.won && G.tick - G.win_tick > 300) {
      wins++;
      printf("soak WIN at t=%d (run %d)\n", t, wins);
      sim_reset(&G, players);
      best = 0, last_progress = t, resets++;
    }
    if (t - last_progress > 3600) {
      printf("soak STALL at t=%d x=%.1f: respawning the group\n", t, best);
      if (getenv("COOP_DUMP") && resets == 0) {
        int x0 = (int)best - 12;
        for (int y = 34; y >= 14; y--) {
          for (int x = x0; x < x0 + 40; x++) {
            int c = G.L.t[y][x];
            bool pl = false;
            for (int i = 0; i < G.nplayers; i++) pl |= (int)G.p[i].pf.body.position.x == x && (int)G.p[i].pf.body.position.y == y;
            putchar(pl ? 0x40 : c == T_AIR ? 0x2e : c == T_ROCK ? 0x23 : c == T_GRASS ? 0x67 : c == T_ONEWAY ? 0x2d : c == T_SPIKE ? 0x5e : c == T_WATER ? 0x7e : c == T_BRICK ? 0x62 : c == T_CRACK ? 0x78 : 0x3f);
          }
          putchar(0x0a);
        }
        for (int i = 0; i < G.nplayers; i++) printf("p%d pos=%.2f,%.2f vel=%.2f,%.2f floor=%d wall=%d wallside=%d fac=%d lock=%.2f\n", i, G.p[i].pf.body.position.x, G.p[i].pf.body.position.y, G.p[i].pf.body.velocity.x, G.p[i].pf.body.velocity.y, G.p[i].pf.body.on_floor, G.p[i].pf.body.on_wall, G.p[i].pf.wall_side, G.p[i].pf.facing, G.p[i].pf.wall_lock);
        printf("x0=%d\n", x0);
      }
      for (int i = 0; i < G.nplayers; i++) sim_respawn_player(&G, i);
      last_progress = t;
    }
  }
  printf("soak done ticks=%d wins=%d resets=%d best=%.1f longest_stall=%d deaths=%d rescues=%d hash=%u\n", ticks, wins, resets, best, longest_stall, G.deaths, G.rescues, G.hash);
  fflush(stdout);
  exit(0);
}
#endif

static void init(void *user) {
  (void)user;
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("coopplatformer: input.cfg had errors");
  cw_input_commit_defaults();
  a_left = cw_action_find("left"), a_right = cw_action_find("right"), a_up = cw_action_find("up"), a_down = cw_action_find("down");
  a_jump = cw_action_find("jump"), a_dash = cw_action_find("dash"), a_map = cw_action_find("map");
  cw_shell_init(&(cw_shell_desc){ .title = "Co-op Platformer", .subtitle = "one to four players", .max_players = 4,
                                  .game_save_size = sizeof(save_t), .game_save_version = 1, .start = start_match,
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
  cw_debug_u32("g.hash", &G.hash);
  cw_debug_f32_array("g.pos", g_pos_dbg, 2);
  cw_debug_f32_array("g.cam", g_cam_dbg, 3);
  cw_debug_i32("g.hp", &g_hp_dbg);
  cw_debug_i32("g.bubbles", &g_bubbles_dbg);
  cur_mood = MOODS[0];
#ifdef COOP_AUTOPILOT
  if (getenv("COOP_SOAK")) soak(atoi(getenv("COOP_SOAK")), getenv("COOP_PLAYERS") ? atoi(getenv("COOP_PLAYERS")) : 2);
  int n = getenv("COOP_PLAYERS") ? atoi(getenv("COOP_PLAYERS")) : 2;
  cw_shell_start(n < 1 ? 1 : n > 4 ? 4 : n);
#endif
}

static void save_progress(void) {
  save_t *sv = cw_shell_game_save();
  sv->abilities |= G.abilities;
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

static void fixed_update(void *user, double dt) {
  (void)user;
  if (!cw_shell_update()) return;
  if (G.nplayers == 0 || !G.world.id) return;
  g_run_ticks++;
#ifdef COOP_AUTOPILOT
  for (int i = 0; i < MAX_PLAYERS; i++) sim_bot_input(&G, i, &INPUT[i]);
#else
  read_input();
  if (cw_action_just_pressed(0, a_map)) g_map_open = !g_map_open;
#endif
  sim_step(&G, INPUT, (float)dt);
  play_events();
  g_state_dbg = (int)cw_shell_screen_now();
  g_players_dbg = G.nplayers;
  g_pos_dbg[0] = (float)G.p[0].pf.body.position.x, g_pos_dbg[1] = (float)G.p[0].pf.body.position.y;
  g_cam_dbg[0] = G.cam.x, g_cam_dbg[1] = G.cam.y, g_cam_dbg[2] = G.cam.z;
  g_hp_dbg = G.p[0].hp;
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
  text("M or Back to close", 0, H * 0.93f, 16 * s, cw_rgba(0.8f, 0.85f, 1, 0.9f), CW_ALIGN_CENTER, W);
}

/* the title screen's picture: a painted dusk meadow with three drifting ridges, a sun, fireflies and the four heroes */
static void title_background(cw_shell_screen scr, cw_vec2 cv, void *user) {
  (void)user;
  (void)scr;
  float W = cv.x, H = cv.y, s = H / 540.0f, t = g_ui_t;
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
  for (int i = 0; i < 4; i++) {
    float x = W * (0.22f + 0.19f * (float)i), y = H * 0.84f - 8 * s * fabsf(sinf(t * 2.0f + (float)i * 0.9f));
    cw_r2d_circle((cw_vec2){ x, H * 0.9f }, 30 * s, 16, cw_rgba(0, 0, 0, 0.25f));
    portrait((cw_vec2){ x, y }, 26 * s, i, false);
  }
}

static void render(void *user, double alpha) {
  (void)user;
  float dt = 1.0f / 60.0f;
  g_ui_t += dt;
  if (G.nplayers && G.world.id && S.built) sync_scene((float)alpha, dt);
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  if (G.nplayers && G.world.id && cw_shell_screen_now() != CW_SHELL_TITLE) {
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
      float cw_ = 76 * s, x = W - (16 + (float)(3 - k) * 82) * s, y = 20 * s;
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
