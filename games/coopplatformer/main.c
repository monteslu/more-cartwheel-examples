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
  t3_mesh *parts[MAX_PLAYERS][8]; /* body, head, eyeL, eyeR, footL, footR, bubble, ring */
  cw_node enode[MAX_ENEMIES];
  t3_mesh *emesh[MAX_ENEMIES][3];
  cw_node coin_node[MAX_COINS];
  t3_mesh *coin_mesh[MAX_COINS];
  cw_node pick_node[MAX_PICKUPS];
  t3_mesh *plat_mesh[MAX_PLATFORMS];
  cw_node fx_dust[MAX_PLAYERS], fx_spark, fx_splash, fx_burst, fx_ember[4], fx_hit;
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
static int g_map_sel = -1; /* the flag the map's cursor is on */
static cw_font g_font;
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
typedef struct mood { float sky[3], fog[3], sun[3], sun_e, amb, hemi, exposure, bloom, fog_near, fog_far, torch; } mood;
static const mood MOODS[BIOMES] = {
  { { 0.32f, 0.58f, 0.95f }, { 0.55f, 0.72f, 0.95f }, { 1.0f, 0.88f, 0.7f }, 3.6f, 0.55f, 1.3f, 1.05f, 0.05f, 60, 420, 0.0f },
  { { 0.04f, 0.05f, 0.1f }, { 0.05f, 0.07f, 0.14f }, { 0.5f, 0.6f, 1.0f }, 0.5f, 0.12f, 0.25f, 1.35f, 0.08f, 20, 260, 1.0f },
  { { 0.22f, 0.4f, 0.45f }, { 0.28f, 0.5f, 0.52f }, { 0.9f, 0.95f, 0.8f }, 2.4f, 0.4f, 0.8f, 1.1f, 0.06f, 40, 320, 0.4f },
  { { 0.25f, 0.06f, 0.04f }, { 0.35f, 0.1f, 0.05f }, { 1.0f, 0.45f, 0.25f }, 1.8f, 0.25f, 0.5f, 1.2f, 0.1f, 30, 280, 0.8f },
};
static mood cur_mood;

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
    cw_node3d_set_rotation(S.sun, (t3_vec3){ -0.9f, 0.35f, 0 });
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
  /* enemies */
  for (int i = 0; i < L->nenemies; i++) {
    const enemy *e = &L->enemies[i];
    S.enode[i] = make_node("Node3D", S.world, "Enemy");
    cw_node eo = S.enode[i];
    float sc = e->kind == 2 ? 2.2f : 1.0f;
    uint32_t col = e->kind == 2 ? 0x8a1a1a : e->kind == 1 ? 0x7a3fb0 : 0xcc4422;
    t3_material *bm = phys(col, 0.4f, 0.0f);
    t3_geometry *sg = t3_sphere_geometry_new(0.5f * sc, 20, 14);
    S.emesh[i][0] = part(eo, sg, bm, 0, 0.45f * sc, 0, 1.0f, 0.85f, 1.0f);
    t3_release(sg);
    t3_material *em = phys(0xffffff, 0.2f, 0);
    em->emissive = (t3_color){ 1, 0.9f, 0.5f }, em->emissive_intensity = 0.6f;
    t3_geometry *eg = t3_sphere_geometry_new(0.13f * sc, 10, 8);
    S.emesh[i][1] = part(eo, eg, em, -0.18f * sc, 0.6f * sc, 0.42f * sc, 1, 1, 1);
    S.emesh[i][2] = part(eo, eg, em, 0.18f * sc, 0.6f * sc, 0.42f * sc, 1, 1, 1);
    t3_release(eg);
    if (e->kind != 1) { /* spikes on the shell */
      t3_geometry *sk = t3_cone_geometry_new(0.1f * sc, 0.3f * sc, 6, 1, false);
      for (int k = 0; k < 5; k++) part(eo, sk, phys(0x2a1a1a, 0.5f, 0.3f), (-0.3f + 0.15f * (float)k) * sc, (0.85f + (k % 2 ? 0.05f : 0)) * sc, 0, 1, 1, 1);
      t3_release(sk);
    }
  }
  /* moving platforms */
  for (int i = 0; i < L->nplat; i++) {
    t3_geometry *pg = t3_box_geometry_new(L->plats[i].w, 0.4f, 2.4f, 1, 1, 1);
    S.plat_mesh[i] = part(wobj, pg, phys(0x8a7a5a, 0.5f, 0.5f), 0, 0, 0, 1, 1, 1);
    t3_release(pg);
  }
  /* characters */
  t3_geometry *body = t3_sphere_geometry_new(0.42f, 24, 16);
  t3_geometry *head = t3_sphere_geometry_new(0.36f, 24, 16);
  t3_geometry *eye = t3_sphere_geometry_new(0.1f, 12, 8);
  t3_geometry *pupil = t3_sphere_geometry_new(0.055f, 8, 6);
  t3_geometry *foot = t3_sphere_geometry_new(0.18f, 12, 8);
  t3_geometry *bub = t3_sphere_geometry_new(1.0f, 24, 16);
  for (int i = 0; i < MAX_PLAYERS; i++) {
    S.pnode[i] = make_node("Node3D", S.root, "Player");
    cw_node po = S.pnode[i];
    uint32_t rgb = ((uint32_t)(PLAYER_RGB[i][0] * 255) << 16) | ((uint32_t)(PLAYER_RGB[i][1] * 255) << 8) | (uint32_t)(PLAYER_RGB[i][2] * 255);
    t3_material *bm = phys(rgb, 0.35f, 0.0f);
    bm->clearcoat = 0.6f, bm->clearcoat_roughness = 0.2f;
    S.parts[i][0] = part(po, body, bm, 0, 0.5f, 0, 1, 1.05f, 0.9f);
    S.parts[i][1] = part(po, head, bm, 0, 1.15f, 0, 1, 1, 0.95f);
    t3_material *wm = phys(0xffffff, 0.2f, 0);
    S.parts[i][2] = part(po, eye, wm, -0.15f, 1.2f, 0.3f, 1, 1.1f, 0.8f);
    S.parts[i][3] = part(po, eye, wm, 0.15f, 1.2f, 0.3f, 1, 1.1f, 0.8f);
    t3_material *pm = phys(0x101018, 0.1f, 0);
    part(po, pupil, pm, -0.15f, 1.2f, 0.37f, 1, 1, 1);
    part(po, pupil, pm, 0.15f, 1.2f, 0.37f, 1, 1, 1);
    S.parts[i][4] = part(po, foot, bm, -0.2f, 0.14f, 0.05f, 1, 0.7f, 1.3f);
    S.parts[i][5] = part(po, foot, bm, 0.2f, 0.14f, 0.05f, 1, 0.7f, 1.3f);
    t3_material *bu = phys(0xaaddff, 0.05f, 0);
    bu->transparent = true, bu->opacity = 0.3f, bu->depth_write = false;
    S.parts[i][6] = part(po, bub, bu, 0, 0.9f, 0, 1, 1, 1);
    S.parts[i][6]->base.visible = false;
    S.parts[i][6]->base.cast_shadow = false;
    cw_node3d_set_position(S.pnode[i], (t3_vec3){ 0, -100, 0 });
  }
  t3_release(body), t3_release(head), t3_release(eye), t3_release(pupil), t3_release(foot), t3_release(bub);
  /* effects */
  if (!cw_node_valid(S.fx_spark)) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
      S.fx_dust[i] = make_node("GPUParticles3D", S.root, "Dust");
      particles_cfg(S.fx_dust[i], 24, 0.6f, 1.0f, 3.0f, 0.35f, cw_rgba(0.8f, 0.75f, 0.65f, 0.5f), -1.0f, 0);
    }
    S.fx_spark = make_node("GPUParticles3D", S.root, "Spark");
    particles_cfg(S.fx_spark, 24, 0.6f, 2.0f, 6.0f, 0.18f, cw_rgba(1, 0.8f, 0.2f, 1), -8.0f, 1);
    S.fx_splash = make_node("GPUParticles3D", S.root, "Splash");
    particles_cfg(S.fx_splash, 60, 0.9f, 3.0f, 8.0f, 0.18f, cw_rgba(0.6f, 0.85f, 1, 0.8f), -14.0f, 0);
    S.fx_burst = make_node("GPUParticles3D", S.root, "Burst");
    particles_cfg(S.fx_burst, 80, 0.8f, 3.0f, 10.0f, 0.3f, cw_rgba(1, 0.55f, 0.2f, 1), -6.0f, 1);
    S.fx_hit = make_node("GPUParticles3D", S.root, "Hit");
    particles_cfg(S.fx_hit, 40, 0.5f, 3.0f, 9.0f, 0.22f, cw_rgba(1, 1, 0.8f, 1), -4.0f, 1);
    /* embers rising from lava: continuous emitters along the lava spans */
    for (int x = 0; x < WORLD_W && S.nember < 4; x++)
      if (L->t[2][x] == T_LAVA && (x % 20) < 2) {
        cw_node n = make_node("GPUParticles3D", S.root, "Ember");
        particles_cfg(n, 60, 2.2f, 1.0f, 3.5f, 0.12f, cw_rgba(1, 0.5f, 0.1f, 1), 1.0f, 1);
        cw_node_set(n, "one_shot", cw_val_bool(false));
        cw_node_set(n, "explosiveness", cw_val_float(0));
        cw_node_set(n, "emitting", cw_val_bool(true));
        cw_node_set(n, "emission_shape", cw_val_enum("BOX"));
        cw_node_set(n, "emission_extents", cw_val_vec3(4, 0.2f, 1));
        cw_node_set(n, "spread", cw_val_float(30));
        cw_node_set(n, "direction", cw_val_vec3(0, 1, 0));
        cw_node3d_set_position(n, (t3_vec3){ (float)x + 4, 3.2f, 0.5f });
        S.fx_ember[S.nember++] = n;
        x += 18;
      }
  }
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
  cw_node_set(S.post, "exposure", cw_val_float(q->exposure));
  cw_node_set(S.post, "bloom_intensity", cw_val_float(q->bloom));
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
    bool inv = p->invuln > 0 && ((p->invuln / 4) & 1);
    if (p->bubble) {
      bubbles++;
      x = p->bubble_x, y = p->bubble_y - 0.6f + 0.1f * sinf(S.time * 3 + (float)i);
      S.parts[i][6]->base.visible = true;
      p->squash = 0;
    } else S.parts[i][6]->base.visible = false;
    if (!p->alive && !p->bubble) {
      cw_node3d_set_position(S.pnode[i], (t3_vec3){ x, -100, 0 });
      continue;
    }
    cw_node3d_set_position(S.pnode[i], (t3_vec3){ x, y, 0 });
    float face = (float)p->pf.facing;
    po->rotation.y = 0;
    t3_object_set_rotation(po, 0, face > 0 ? 0.35f : -0.35f, 0);
    /* secondary animation: squash and stretch, run bounce, foot swing */
    if (p->land_fx) p->squash = 0.35f;
    if (p->jump_fx) p->squash = -0.3f;
    p->squash *= 1.0f - 6.0f * dt > 0 ? 1.0f - 6.0f * dt : 0;
    float vy = p->pf.body.velocity.y;
    float stretch = p->squash < 0 ? -p->squash : 0.0f;
    float sq = p->squash > 0 ? p->squash : 0.0f;
    float st = 1.0f + stretch * 0.6f + (vy > 4 ? 0.1f : 0) - sq * 0.5f;
    float wd = 1.0f - stretch * 0.3f + sq * 0.4f;
    p->run_phase += (p->anim == 1 ? 14.0f : 3.0f) * dt;
    float bob = p->anim == 1 ? 0.06f * fabsf(sinf(p->run_phase)) : 0.02f * sinf(S.time * 2 + (float)i);
    S.parts[i][0]->base.scale = (t3_vec3){ wd, 1.05f * st, 0.9f * wd };
    S.parts[i][0]->base.position.y = 0.5f + bob;
    S.parts[i][1]->base.position.y = 1.15f * st + bob;
    for (int k = 2; k <= 3; k++) S.parts[i][k]->base.position.y = 1.2f * st + bob;
    float sw = p->anim == 1 ? sinf(p->run_phase) * 0.22f : 0;
    S.parts[i][4]->base.position.z = 0.05f + sw;
    S.parts[i][5]->base.position.z = 0.05f - sw;
    if (p->anim == 2 || p->anim == 3) S.parts[i][4]->base.position.y = S.parts[i][5]->base.position.y = 0.22f;
    else S.parts[i][4]->base.position.y = S.parts[i][5]->base.position.y = 0.14f;
    for (int k = 0; k < 8; k++)
      if (S.parts[i][k] && k != 6) S.parts[i][k]->base.visible = !inv;
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
    if (!e->alive) continue;
    float flash = e->hurt > 0 && (e->hurt / 3) % 2 ? 1.4f : 1.0f;
    cw_node3d_set_position(S.enode[i], (t3_vec3){ e->x, e->y + (e->kind == 1 ? 0.2f * sinf(S.time * 5 + (float)i) : 0), 0 });
    t3_object *eo = cw_node3d_object(S.enode[i]);
    t3_object_set_rotation(eo, 0, e->dir > 0 ? 1.1f : -1.1f, 0);
    S.emesh[i][0]->base.scale = (t3_vec3){ flash, 0.85f / flash + 0.05f * sinf(S.time * 8 + (float)i), flash };
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
static void start_match(int players, void *user) {
  (void)user;
  save_t *sv = cw_shell_game_save();
  G.abilities = 0;
  sim_reset(&G, players);
  if (sv->abilities && !getenv("COOP_FRESH")) G.abilities = sv->abilities;
  G.reached |= sv->reached;
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
      printf("tr t=%d pos=%.2f,%.2f vel=%.2f,%.2f fl=%d wl=%d wet=%.1f in=(%.0f,%.0f,j%d%d,d%d) dr=%d dcd=%.2f al=%d ev=%x alive=%d bub=%d\n", t, q->pf.body.position.x, q->pf.body.position.y, q->pf.body.velocity.x, q->pf.body.velocity.y, q->pf.body.on_floor, q->pf.body.on_wall, q->pf.submerged, INPUT[tp].x, INPUT[tp].y, INPUT[tp].jump_pressed, INPUT[tp].jump_held, INPUT[tp].dash_pressed, q->pf.dash_ready, q->pf.dash_cd, q->pf.air_jumps_left, q->pf.events, q->alive, q->bubble);
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
                                  .quit = quit_match, .sound = sound_hook });
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
  if (getenv("COOP_MAP")) print_map(atoi(getenv("COOP_MAP")));
  if (getenv("COOP_SECRETS")) secret_routes(1);
  if (getenv("COOP_FUZZ")) fuzz(atoi(getenv("COOP_FUZZ")), getenv("COOP_PLAYERS") ? atoi(getenv("COOP_PLAYERS")) : 1);
  if (getenv("COOP_SOAK")) soak(atoi(getenv("COOP_SOAK")), getenv("COOP_PLAYERS") ? atoi(getenv("COOP_PLAYERS")) : 2);
  int n = getenv("COOP_PLAYERS") ? atoi(getenv("COOP_PLAYERS")) : 2;
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
  if (!cw_shell_update()) return;
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

static cw_color tile_color(int t, int biome) {
  switch (t) {
    case T_ROCK: case T_GRASS: case T_BRICK: return biome == BIOME_MEADOW ? cw_rgba(0.35f, 0.55f, 0.3f, 1) : biome == BIOME_CAVES ? cw_rgba(0.35f, 0.4f, 0.55f, 1) : biome == BIOME_RUINS ? cw_rgba(0.75f, 0.65f, 0.45f, 1) : cw_rgba(0.6f, 0.25f, 0.2f, 1);
    case T_WATER: return cw_rgba(0.2f, 0.5f, 0.9f, 1);
    case T_LAVA: return cw_rgba(1, 0.4f, 0.1f, 1);
    case T_SPIKE: return cw_rgba(0.9f, 0.9f, 0.9f, 1);
    case T_ONEWAY: return cw_rgba(0.7f, 0.5f, 0.3f, 1);
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

/* the world point as a pixel of the HUD canvas, through the game camera (vertical fov 40, pitched down 0.06) */
static bool project(float wx, float wy, float wz, float W, float H, float *sx, float *sy) {
  const float pitch = 0.06f, tf = 0.36397f; /* tan(20 degrees) */
  float dx = wx - G.cam.x, dy = wy - G.cam.y, dz = wz - G.cam.z;
  float yv = dy * cosf(pitch) - dz * sinf(pitch), zv = dy * sinf(pitch) + dz * cosf(pitch);
  if (zv > -0.5f) return false;
  *sx = (0.5f + 0.5f * (dx / -zv) / (tf * W / H)) * W;
  *sy = (0.5f - 0.5f * (yv / -zv) / tf) * H;
  return true;
}

static void draw_markers(float W, float H, float s) {
  for (int i = 0; i < G.nplayers; i++) {
    const player *p = &G.p[i];
    if (!p->joined || (!p->alive && !p->bubble)) continue;
    float wx = p->bubble ? p->bubble_x : (float)p->pf.body.position.x, wy = (p->bubble ? p->bubble_y + 0.9f : (float)p->pf.body.position.y) + 2.15f;
    float sx, sy;
    if (!project(wx, wy, 0, W, H, &sx, &sy)) continue;
    float r = 11 * s;
    if (sx < r) sx = r;
    if (sx > W - r) sx = W - r;
    if (sy < r + 60 * s) sy = r + 60 * s;
    cw_color c = cw_rgba(PLAYER_RGB[i][0], PLAYER_RGB[i][1], PLAYER_RGB[i][2], 1);
    glyph(i, sx, sy, r + 2 * s, cw_rgba(0, 0, 0, 0.75f)); /* a dark outline so it reads on any background */
    glyph(i, sx, sy, r, c);
    char b[4];
    snprintf(b, sizeof b, "%d", i + 1);
    text(b, sx - r, sy - r * 0.62f, 15 * s, cw_rgba(0.05f, 0.05f, 0.08f, 1), CW_ALIGN_CENTER, 2 * r);
  }
}

static void draw_map(float W, float H) {
  cw_r2d_rect(cw_rect_make(0, 0, W, H), cw_rgba(0.02f, 0.03f, 0.06f, 0.88f));
  float mw = W * 0.92f, cell = mw / (WORLD_W / 4);
  float ox = (W - mw) * 0.5f, oy = H * 0.5f - cell * (WORLD_H / 4) * 0.5f;
  const save_t *sv = cw_shell_game_save();
  for (int by = 0; by < WORLD_H / 4; by++)
    for (int bx = 0; bx < WORLD_W / 4; bx++) {
      if (!G.seen[by][bx] && !sv->seen[by][bx]) continue;
      /* dominant tile of the 4x4 block */
      int best = T_AIR, bc = 0, cnt[9] = { 0 };
      for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) cnt[G.L.t[by * 4 + y][bx * 4 + x]]++;
      for (int t = 1; t < 9; t++)
        if (cnt[t] > bc) bc = cnt[t], best = t;
      float px = ox + (float)bx * cell, py = oy + (float)(WORLD_H / 4 - 1 - by) * cell;
      cw_r2d_rect(cw_rect_make(px, py, cell, cell), best == T_AIR ? cw_rgba(0.08f, 0.1f, 0.16f, 1) : tile_color(best, G.L.biome[bx * 4]));
    }
  for (int i = 0; i < G.nplayers; i++) {
    const player *p = &G.p[i];
    float px = ox + (float)p->pf.body.position.x / 4 * cell, py = oy + ((float)WORLD_H / 4 - (float)p->pf.body.position.y / 4) * cell;
    cw_r2d_circle((cw_vec2){ px, py }, cell * 0.9f, 12, cw_rgba(PLAYER_RGB[i][0], PLAYER_RGB[i][1], PLAYER_RGB[i][2], 1));
  }
  for (int k = 0; k < G.L.npickups; k++) {
    const pickup *pk = &G.L.pickups[k];
    float px = ox + (float)pk->x / 4 * cell, py = oy + ((float)WORLD_H / 4 - (float)pk->y / 4) * cell;
    if (G.seen[pk->y / 4][pk->x / 4]) cw_r2d_circle((cw_vec2){ px, py }, cell * 0.6f, 8, pk->taken ? cw_rgba(0.5f, 0.5f, 0.5f, 1) : cw_rgba(0.4f, 0.9f, 1, 1));
  }
  for (int k = 0; k < G.L.nchecks; k++) {
    if (!(G.reached & (1u << k))) continue;
    float px = ox + (float)G.L.checks[k].x / 4 * cell, py = oy + ((float)WORLD_H / 4 - (float)G.L.checks[k].y / 4) * cell;
    bool sel = k == g_map_sel;
    cw_r2d_line((cw_vec2){ px, py }, (cw_vec2){ px, py - cell * 2.6f }, 1.5f, cw_rgba(1, 1, 1, 1));
    cw_vec2 fl[3] = { { px, py - cell * 2.6f }, { px + cell * 1.8f, py - cell * 2.0f }, { px, py - cell * 1.4f } };
    cw_r2d_polygon(fl, 3, sel ? cw_rgba(1, 0.85f, 0.2f, 1) : cw_rgba(0.9f, 0.9f, 0.95f, 1));
    if (sel) cw_r2d_circle_lines((cw_vec2){ px, py - cell }, cell * 2.6f, 2, 20, cw_rgba(1, 0.85f, 0.2f, 1));
  }
  text("left and right pick a flag, Jump travels there", 0, H * 0.92f, 20 * H / 540, cw_rgba(1, 0.9f, 0.5f, 1), CW_ALIGN_CENTER, W);
  text("MAP   (M or Back to close)", 0, H * 0.04f, 26 * H / 540, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER, W);
}

static void render(void *user, double alpha) {
  (void)user;
  float dt = 1.0f / 60.0f;
  if (G.nplayers && G.world.id && S.built) sync_scene((float)alpha, dt);
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  if (G.nplayers && G.world.id && cw_shell_screen_now() != CW_SHELL_TITLE) {
    /* hearts and coins per player */
    for (int i = 0; i < G.nplayers; i++) {
      const player *p = &G.p[i];
      float x0 = 20 * s + (float)i * 170 * s, y0 = 18 * s;
      cw_r2d_round_rect(cw_rect_make(x0 - 6 * s, y0 - 6 * s, 160 * s, 50 * s), 10 * s, 6, cw_rgba(0, 0, 0, 0.35f));
      glyph(i, x0 + 10 * s, y0 + 12 * s, 10 * s, cw_rgba(PLAYER_RGB[i][0], PLAYER_RGB[i][1], PLAYER_RGB[i][2], 1));
      for (int h = 0; h < 3; h++) {
        cw_color hc = h < p->hp ? cw_rgba(1, 0.3f, 0.35f, 1) : cw_rgba(0.3f, 0.3f, 0.35f, 0.7f);
        cw_r2d_circle((cw_vec2){ x0 + (34 + 22 * (float)h) * s, y0 + 12 * s }, 8 * s, 12, hc);
      }
      char b[32];
      snprintf(b, sizeof b, "%d", p->coins);
      cw_r2d_circle((cw_vec2){ x0 + 10 * s, y0 + 34 * s }, 5 * s, 10, cw_rgba(1, 0.8f, 0.25f, 1));
      text(b, x0 + 24 * s, y0 + 24 * s, 18 * s, cw_rgba(1, 0.95f, 0.8f, 1), CW_ALIGN_LEFT, 0);
    }
    char b[64];
    snprintf(b, sizeof b, "%s%s%s", (G.abilities & AB_DOUBLE) ? "AIR JUMP  " : "", (G.abilities & AB_CLAWS) ? "CLAWS  " : "", (G.abilities & AB_DASH) ? "DASH" : "");
    text(b, 0, 18 * s, 18 * s, cw_rgba(0.6f, 0.9f, 1, 1), CW_ALIGN_RIGHT, W - 20 * s);
    /* boss bar */
    for (int i = 0; i < G.L.nenemies; i++)
      if (G.L.enemies[i].kind == 2 && G.L.enemies[i].alive && fabsf(G.cam.x - G.L.enemies[i].x) < 30) {
        float bw = W * 0.5f, bx = (W - bw) * 0.5f, by = H - 44 * s;
        cw_r2d_round_rect(cw_rect_make(bx, by, bw, 16 * s), 6 * s, 6, cw_rgba(0, 0, 0, 0.6f));
        cw_r2d_round_rect(cw_rect_make(bx, by, bw * (float)G.L.enemies[i].hp / 9.0f, 16 * s), 6 * s, 6, cw_rgba(0.9f, 0.2f, 0.15f, 1));
      }
    draw_markers(W, H, s);
    text("hold R or Y: back to the last flag", 20 * s, H - 30 * s, 14 * s, cw_rgba(1, 1, 1, 0.45f), CW_ALIGN_LEFT, 0);
    /* arrows to players carried in bubbles */
    for (int i = 0; i < G.nplayers; i++)
      if (G.p[i].bubble && G.p[i].joined) {
        const char *t = "RESCUE";
        text(t, 0, H - 80 * s, 20 * s, cw_rgba(PLAYER_RGB[i][0], PLAYER_RGB[i][1], PLAYER_RGB[i][2], 1), CW_ALIGN_CENTER, W);
      }
    if (G.won) {
      cw_r2d_rect(cw_rect_make(0, H * 0.3f, W, H * 0.3f), cw_rgba(0, 0, 0, 0.5f));
      text("THE DEEP IS QUIET", 0, H * 0.34f, 48 * s, cw_rgba(1, 0.85f, 0.3f, 1), CW_ALIGN_CENTER, W);
      char t[96];
      snprintf(t, sizeof t, "coins %d   secrets %d   rescues %d   falls %d", G.coins_total, G.secrets, G.rescues, G.deaths);
      text(t, 0, H * 0.47f, 24 * s, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER, W);
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
