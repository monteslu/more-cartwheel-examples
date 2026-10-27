/* brawler levels: four stages built from scene nodes, wave tables, breakable props, the camera. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "game.h"

typedef struct theme {
  const char *name;
  float sky[3], ambient[3], amb_e, sun[3], sun_e, sun_rot[2];
  float ground[3], wall[3][3], glow[3], lamp[3];
  float lamp_e;
} theme;

static const theme THEMES[4] = {
  { "Back Alley", { 0.05f, 0.04f, 0.09f }, { 0.55f, 0.5f, 0.7f }, 0.35f, { 1.0f, 0.62f, 0.4f }, 2.2f, { -0.7f, 0.5f },
    { 0.16f, 0.16f, 0.17f }, { { 0.32f, 0.2f, 0.16f }, { 0.28f, 0.24f, 0.22f }, { 0.22f, 0.18f, 0.26f } }, { 1.0f, 0.78f, 0.4f }, { 1.0f, 0.7f, 0.35f }, 1.0f },
  { "Foggy Docks", { 0.03f, 0.05f, 0.07f }, { 0.4f, 0.55f, 0.7f }, 0.4f, { 0.55f, 0.72f, 1.0f }, 1.6f, { -0.8f, -0.5f },
    { 0.12f, 0.14f, 0.16f }, { { 0.18f, 0.24f, 0.3f }, { 0.26f, 0.28f, 0.3f }, { 0.3f, 0.2f, 0.14f } }, { 0.5f, 0.85f, 1.0f }, { 0.7f, 0.85f, 1.0f }, 1.2f },
  { "Neon Arcade", { 0.04f, 0.02f, 0.07f }, { 0.55f, 0.4f, 0.8f }, 0.45f, { 0.8f, 0.5f, 1.0f }, 1.5f, { -0.75f, 0.3f },
    { 0.1f, 0.09f, 0.14f }, { { 0.2f, 0.12f, 0.28f }, { 0.12f, 0.18f, 0.3f }, { 0.3f, 0.12f, 0.2f } }, { 1.0f, 0.3f, 0.8f }, { 0.4f, 0.9f, 1.0f }, 1.5f },
  { "Factory Roof", { 0.12f, 0.07f, 0.05f }, { 0.8f, 0.55f, 0.4f }, 0.5f, { 1.0f, 0.5f, 0.25f }, 3.0f, { -0.35f, 0.7f },
    { 0.2f, 0.18f, 0.17f }, { { 0.34f, 0.2f, 0.12f }, { 0.25f, 0.25f, 0.27f }, { 0.4f, 0.28f, 0.14f } }, { 1.0f, 0.5f, 0.2f }, { 1.0f, 0.6f, 0.3f }, 1.1f },
};

typedef struct wave_def { float lock; int n; int8_t def[10]; } wave_def;
#define T D_THUG
#define P D_PUNK
#define R D_RUNNER
#define K D_KNIFER
#define B D_BRUISER
static const wave_def STAGE1[] = {
  { 0, 3, { T, T, T } }, { 14, 4, { T, T, P, T } }, { 28, 5, { T, P, R, T, P } },
  { 42, 4, { B, T, T, P } }, { 56, 1, { D_FOREMAN } },
};
static const wave_def STAGE2[] = {
  { 0, 4, { T, P, R, T } }, { 14, 5, { K, T, P, R, T } }, { 28, 5, { R, R, K, P, T } },
  { 42, 4, { B, K, T, P } }, { 56, 1, { D_MATRON } },
};
static const wave_def STAGE3[] = {
  { 0, 5, { K, R, P, T, K } }, { 14, 6, { K, K, R, P, T, R } }, { 28, 4, { B, K, R, P } },
  { 42, 6, { K, R, K, P, R, T } }, { 56, 1, { D_RINGMASTER } },
};
static const wave_def STAGE4[] = {
  { 0, 6, { K, R, P, T, K, R } }, { 14, 3, { B, K, R } }, { 28, 7, { K, K, R, R, P, P, T } },
  { 42, 4, { B, B, K, R } }, { 56, 1, { D_COLOSSUS } },
};
#undef T
#undef P
#undef R
#undef K
#undef B
static const wave_def *STAGES[4] = { STAGE1, STAGE2, STAGE3, STAGE4 };
static const int NWAVES[4] = { 5, 5, 5, 5 };

int stage_wave_count(int s) { return NWAVES[s & 3]; }
const char *stage_name(int s) { return THEMES[s & 3].name; }
float level_length(void) { return 56.0f + 14.0f; }

void stage_wave_load(int stage, int wave) {
  const wave_def *w = &STAGES[stage & 3][wave];
  W.lock_x = w->lock;
  W.wave_spawned = 0;
  W.spawn_q_n = 0;
  int n = 0;
  for (int i = 0; i < w->n; i++) {
    W.spawn_q[n].def = w->def[i], W.spawn_q[n].side = (i & 1) ? 1 : -1, W.spawn_q[n].delay = 20 + i * 45;
    n++;
  }
  /* more fighters on screen: more to fight */
  if (W.players >= 3) for (int i = 0; i < w->n && n < 22; i++)
    if (w->def[i] != D_BRUISER && !DEFS[w->def[i]].boss && (i % 2) == 0) { W.spawn_q[n].def = w->def[i], W.spawn_q[n].side = (i & 1) ? -1 : 1, W.spawn_q[n].delay = 60 + i * 40; n++; }
  if (W.players == 2) for (int i = 0; i < w->n && n < 22; i += 3)
    if (w->def[i] != D_BRUISER && !DEFS[w->def[i]].boss) { W.spawn_q[n].def = w->def[i], W.spawn_q[n].side = 1, W.spawn_q[n].delay = 80 + i * 40; n++; }
  W.spawn_q_n = n;
}

/* ── nodes ──────────────────────────────────────────────────────────── */
static cw_node stage_node;
static cw_node tree_root(void) { return cw_node_get(cw_tree_root(cw_scene_app_tree()), "Level"); }
static int ncount;

static cw_node mesh(cw_node parent, const char *prim, t3_vec3 pos, t3_vec3 size, const float *c, float rough, float metal) {
  char nm[24];
  snprintf(nm, sizeof nm, "m%d", ncount++);
  cw_node n = cw_node_new("MeshInstance3D", nm);
  cw_node_add_child(parent, n);
  cw_node_set(n, "primitive", cw_val_enum(prim));
  cw_node_set(n, "size", cw_val_vec3(size.x, size.y, size.z));
  cw_node_set(n, "color", cw_val_color(c[0], c[1], c[2], 1));
  cw_node_set(n, "roughness", cw_val_float(rough));
  cw_node_set(n, "metalness", cw_val_float(metal));
  cw_node3d_set_position(n, pos);
  return n;
}
static void glow(cw_node n, const float *c, float e) { cw_node_set(n, "emissive", cw_val_color(c[0] * e, c[1] * e, c[2] * e, 1)); }

static float hf(int i, int salt) { uint32_t h = (uint32_t)(i * 2654435761u) ^ (uint32_t)(salt * 40503u); h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return (float)(h & 0xffff) / 65535.0f; }

void level_free(void) {
  if (stage_node.id && cw_node_valid(stage_node)) cw_node_free(stage_node);
  stage_node = (cw_node){ 0 };
  for (int i = 0; i < MAXPROP; i++) W.prop[i].node = (cw_node){ 0 };
  for (int i = 0; i < MAXITEM; i++) W.item[i].node = (cw_node){ 0 };
}

void level_build(int stage) {
  level_free();
  const theme *th = &THEMES[stage & 3];
  float len = level_length();
  stage_node = cw_node_new("Node3D", "Stage");
  cw_node_add_child(tree_root(), stage_node);
  cw_node s = stage_node;
  /* environment and lights from the theme */
  cw_node env = cw_node_get(tree_root(), "Env");
  cw_node_set(env, "background", cw_val_color(th->sky[0], th->sky[1], th->sky[2], 1));
  cw_node_set(env, "ambient_color", cw_val_color(th->ambient[0], th->ambient[1], th->ambient[2], 1));
  cw_node_set(env, "ambient_energy", cw_val_float(th->amb_e));
  cw_node rim = cw_node_get(tree_root(), "Rim");
  cw_node_set(rim, "color", cw_val_color(th->glow[0] * 0.6f + 0.3f, th->glow[1] * 0.6f + 0.3f, th->glow[2] * 0.6f + 0.4f, 1));
  cw_node sun = cw_node_get(tree_root(), "Sun");
  cw_node_set(sun, "color", cw_val_color(th->sun[0], th->sun[1], th->sun[2], 1));
  cw_node_set(sun, "energy", cw_val_float(th->sun_e * 0.85f));
  cw_node3d_set_rotation(sun, (t3_vec3){ th->sun_rot[0], th->sun_rot[1], 0 });
  /* ground: asphalt road, raised pavement behind */
  mesh(s, "BOX", (t3_vec3){ len * 0.5f, -0.15f, 0.8f }, (t3_vec3){ len + 50, 0.3f, 8.4f }, th->ground, 0.85f, 0.0f);
  float pave[3] = { th->ground[0] * 1.5f, th->ground[1] * 1.5f, th->ground[2] * 1.5f };
  mesh(s, "BOX", (t3_vec3){ len * 0.5f, 0.05f, -4.6f }, (t3_vec3){ len + 50, 0.34f, 2.4f }, pave, 0.7f, 0.0f);
  mesh(s, "BOX", (t3_vec3){ len * 0.5f, -0.2f, -9.0f }, (t3_vec3){ len + 50, 0.2f, 12 }, th->ground, 0.9f, 0.0f);
  float paint[3] = { 0.8f, 0.78f, 0.6f };
  for (float x = -8; x < len + 12; x += 4.0f) mesh(s, "BOX", (t3_vec3){ x, 0.01f, 3.2f }, (t3_vec3){ 1.6f, 0.02f, 0.14f }, paint, 0.6f, 0);
  /* buildings along the back with lit windows */
  int bi = 0;
  for (float x = -14; x < len + 20; x += 5.8f, bi++) {
    float h = 5.0f + hf(bi, stage) * 7.0f;
    float c[3] = { th->wall[bi % 3][0] * 0.55f, th->wall[bi % 3][1] * 0.55f, th->wall[bi % 3][2] * 0.55f };
    mesh(s, "BOX", (t3_vec3){ x, h * 0.5f, -7.4f - hf(bi, 9) * 1.2f }, (t3_vec3){ 5.5f, h, 3.0f }, c, 0.8f, 0);
    for (int wy = 0; wy < (int)(h / 1.7f); wy++)
      for (int wx = 0; wx < 3; wx++) {
        if (hf(bi * 31 + wy * 7 + wx, stage + 3) < 0.45f) continue;
        float wc[3] = { th->glow[0], th->glow[1], th->glow[2] };
        cw_node w = mesh(s, "BOX", (t3_vec3){ x - 1.8f + (float)wx * 1.8f, 1.7f + (float)wy * 1.6f, -5.88f - hf(bi, 9) * 1.2f }, (t3_vec3){ 0.8f, 0.9f, 0.06f }, wc, 0.3f, 0);
        glow(w, wc, 1.1f);
      }
  }
  /* lamp posts with a light each */
  float steel[3] = { 0.12f, 0.12f, 0.14f };
  for (float x = 2; x < len + 6; x += 11.0f) {
    mesh(s, "CYLINDER", (t3_vec3){ x, 2.2f, -3.6f }, (t3_vec3){ 0.14f, 4.4f, 0 }, steel, 0.5f, 0.8f);
    cw_node head = mesh(s, "BOX", (t3_vec3){ x, 4.45f, -3.4f }, (t3_vec3){ 0.5f, 0.12f, 0.5f }, steel, 0.5f, 0.8f);
    (void)head;
    cw_node bulb = mesh(s, "SPHERE", (t3_vec3){ x, 4.3f, -3.4f }, (t3_vec3){ 0.3f, 0, 0 }, th->lamp, 0.2f, 0);
    glow(bulb, th->lamp, 6);
    char nm[24];
    snprintf(nm, sizeof nm, "L%d", ncount++);
    cw_node l = cw_node_new("OmniLight3D", nm);
    cw_node_add_child(s, l);
    cw_node3d_set_position(l, (t3_vec3){ x, 4.0f, -2.2f });
    cw_node_set(l, "color", cw_val_color(th->lamp[0], th->lamp[1], th->lamp[2], 1));
    cw_node_set(l, "energy", cw_val_float(th->lamp_e * 40));
    cw_node_set(l, "range", cw_val_float(11));
  }
  /* breakable props */
  cw_rng r;
  cw_rng_seed(&r, 99 + (uint64_t)stage);
  int np = 0;
  for (float x = 8; x < len - 4 && np < MAXPROP; x += 5.2f + cw_rng_float(&r) * 2.0f, np++) {
    prop_t *p = &W.prop[np];
    p->used = true, p->kind = cw_rng_int(&r, 0, 2), p->hp = 3;
    p->x = x, p->z = cw_rng_range(&r, -2.0f, 2.0f);
    p->drop = cw_rng_int(&r, 0, 9);
    float crate[3] = { 0.42f, 0.28f, 0.15f }, barrel[3] = { 0.2f, 0.32f, 0.5f }, bin[3] = { 0.22f, 0.4f, 0.26f };
    if (p->kind == 0) p->node = mesh(s, "BOX", (t3_vec3){ x, 0.45f, p->z }, (t3_vec3){ 0.9f, 0.9f, 0.9f }, crate, 0.8f, 0);
    else if (p->kind == 1) p->node = mesh(s, "CYLINDER", (t3_vec3){ x, 0.5f, p->z }, (t3_vec3){ 0.7f, 1.0f, 0 }, barrel, 0.4f, 0.6f);
    else p->node = mesh(s, "BOX", (t3_vec3){ x, 0.55f, p->z }, (t3_vec3){ 0.8f, 1.1f, 0.7f }, bin, 0.5f, 0.5f);
    cw_node_set(p->node, "cast_shadow", cw_val_bool(true));
    cw_node_set(p->node, "receive_shadow", cw_val_bool(true));
  }
  ncount = 0;
}

void prop_hit(prop_t *p) {
  if (!p->used) return;
  p->hp--;
  p->shake = 8;
  fx_hit(p->x, 0.7f, p->z, 0, 1);
  sfx(SFX_HIT1);
  if (p->hp > 0) return;
  p->used = false;
  if (p->node.id) cw_node_set(p->node, "visible", cw_val_bool(false));
  fx_debris(p->x, 0.7f, p->z, p->kind == 0 ? 0x6b4526 : p->kind == 1 ? 0x335580 : 0x386644);
  fx_dust(p->x, p->z, 1.5f);
  sfx(SFX_BREAK);
  int kind = -1;
  if (p->drop < 3) kind = 0;
  else if (p->drop < 5) kind = 1 + (p->drop & 1) * 1;
  else if (p->drop < 7) kind = 3;
  else if (p->drop < 9) kind = 4;
  if (kind >= 0)
    for (int i = 0; i < MAXITEM; i++)
      if (!W.item[i].used) { W.item[i] = (item_t){ .used = true, .kind = kind, .x = p->x, .z = p->z, .y = 0.9f }; break; }
}

/* items and props each frame */
void level_update(void) {
  if (!W.wave_spawned && W.cam_x >= W.lock_x - 0.4f) W.wave_spawned = 1;
  for (int i = 0; i < MAXPROP; i++) {
    prop_t *p = &W.prop[i];
    if (!p->used || !p->node.id) continue;
    if (p->shake > 0) {
      p->shake--;
      float s = (float)(p->shake & 1 ? 1 : -1) * 0.04f * (float)p->shake / 8.0f;
      float y = p->kind == 0 ? 0.45f : p->kind == 1 ? 0.5f : 0.55f;
      cw_node3d_set_position(p->node, (t3_vec3){ p->x + s, y, p->z });
    }
  }
  for (int i = 0; i < MAXITEM; i++) {
    item_t *it = &W.item[i];
    if (it->used && !it->node.id) {
      static const char *prim[5] = { "SPHERE", "BOX", "BOX", "BOX", "BOX" };
      static const float col[5][3] = { { 0.85f, 0.12f, 0.1f }, { 0.45f, 0.28f, 0.12f }, { 0.4f, 0.42f, 0.46f }, { 0.8f, 0.82f, 0.9f }, { 1.0f, 0.78f, 0.15f } };
      static const t3_vec3 sz[5] = { { 0.32f, 0, 0 }, { 0.85f, 0.09f, 0.09f }, { 1.0f, 0.09f, 0.09f }, { 0.34f, 0.04f, 0.06f }, { 0.2f, 0.2f, 0.06f } };
      it->node = mesh(stage_node, prim[it->kind], (t3_vec3){ it->x, it->y, it->z }, sz[it->kind], col[it->kind], 0.35f, it->kind >= 2 ? 0.8f : 0.1f);
      if (it->kind == 4) glow(it->node, col[4], 3);
    }
    if (!it->used && it->node.id) { cw_node_free(it->node); it->node = (cw_node){ 0 }; }
    if (it->used && it->node.id) {
      float s, c;
      cw_sincos((float)(it->t + i * 9) * 0.07f, &s, &c);
      cw_node3d_set_position(it->node, (t3_vec3){ it->x, it->y + 0.18f + 0.06f * s + (it->kind == 4 ? 0.3f : 0), it->z });
      cw_node3d_set_rotation(it->node, (t3_vec3){ 0, (float)it->t * (it->kind == 4 ? 0.08f : 0.02f), 0 });
    }
  }
}

/* ── camera: follows the fighters' centre, never backward, held at the wave's lock ── */
void level_camera(float dt) {
  (void)dt;
  float sx = 0, n = 0, maxx = -1e9f;
  for (int i = 0; i < W.players; i++) {
    const ent *p = &W.p[i];
    if (!p->used || p->st == S_DEAD) continue;
    sx += p->x, n++;
    if (p->x > maxx) maxx = p->x;
  }
  float goal = W.cam_x;
  if (n > 0) goal = sx / n + 0.6f;
  /* the scroll is released only when the wave is cleared: lock_x is the next stop */
  if (goal > W.lock_x) goal = W.lock_x;
  if (goal < W.cam_x) goal = W.cam_x;
  W.cam_x += (goal - W.cam_x) * 0.06f;
  if (goal - W.cam_x < 0.002f) W.cam_x = goal;

  cw_node cam = cw_node_get(tree_root(), "Camera");
  float shx = 0, shy = 0;
  if (W.shake > 0) {
    float a = W.shake_amp * (float)W.shake / 10.0f;
    shx = (hf(W.tick, 1) - 0.5f) * a * 2, shy = (hf(W.tick, 2) - 0.5f) * a * 2;
  }
  cw_node3d_set_position(cam, (t3_vec3){ W.cam_x + shx, 3.9f + shy, 7.7f });
  cw_node3d_set_rotation(cam, (t3_vec3){ -0.3f, 0, 0 });
  cw_node sun = cw_node_get(tree_root(), "Sun");
  cw_node3d_set_position(sun, (t3_vec3){ W.cam_x + 6, 10, 4 });
}
