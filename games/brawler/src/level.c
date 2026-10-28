/* brawler levels: four stages built from scene nodes, wave tables, breakable props, the camera. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "game.h"

/* per stage mood: the key light (a low sun or moon from behind the buildings, so they cast long shadows and
   shafts across the street), a cool fill from the camera side, a rim from behind, fog, grade, coloured accents. */
typedef struct accent { float dx, y, z, c[3], e, range; } accent;
typedef struct theme {
  const char *name, *lut;
  float bg[3], hemi_sky[3], hemi_ground[3], hemi_e;
  float sun_dir[3], sun[3], sun_e;
  float fill[3], fill_e, rim[3], rim_e;
  float fog[3], fog_density, fog_fall;
  float lamp[3], lamp_e;
  float exposure, bloom, bloom_thr;
  accent acc[3];
} theme;

static const theme THEMES[4] = {
  { "Back Alley", "luts/alley.cube",
    { 0.07f, 0.05f, 0.09f }, { 0.45f, 0.42f, 0.62f }, { 0.16f, 0.1f, 0.08f }, 0.45f,
    { 0.7f, -0.55f, 0.32f }, { 1.0f, 0.52f, 0.26f }, 4.2f,
    { 0.38f, 0.5f, 0.85f }, 0.75f, { 0.9f, 0.45f, 0.7f }, 1.3f,
    { 0.32f, 0.18f, 0.15f }, 0.014f, 0.06f,
    { 1.0f, 0.62f, 0.3f }, 1.1f, 0.2f, 0.16f, 1.2f,
    { { -7, 2.6f, -2.6f, { 1.0f, 0.35f, 0.18f }, 28, 9 }, { 6, 2.4f, -2.8f, { 0.25f, 0.5f, 1.0f }, 22, 9 }, { 0, 3.2f, 3.5f, { 1.0f, 0.7f, 0.4f }, 12, 10 } } },
  { "Foggy Docks", "luts/docks.cube",
    { 0.04f, 0.07f, 0.1f }, { 0.3f, 0.42f, 0.58f }, { 0.07f, 0.09f, 0.1f }, 0.5f,
    { -0.6f, -0.5f, 0.45f }, { 0.55f, 0.72f, 1.0f }, 3.0f,
    { 0.55f, 0.65f, 0.85f }, 0.7f, { 0.4f, 0.8f, 1.0f }, 1.2f,
    { 0.14f, 0.2f, 0.25f }, 0.03f, 0.04f,
    { 0.75f, 0.9f, 1.0f }, 1.3f, 0.2f, 0.14f, 1.2f,
    { { -6, 3.0f, -2.5f, { 1.0f, 0.6f, 0.2f }, 40, 10 }, { 7, 3.0f, -2.5f, { 1.0f, 0.6f, 0.2f }, 40, 10 }, { 0, 2.0f, 4.0f, { 0.3f, 0.6f, 1.0f }, 10, 9 } } },
  { "Neon Arcade", "luts/arcade.cube",
    { 0.04f, 0.02f, 0.08f }, { 0.4f, 0.28f, 0.7f }, { 0.08f, 0.05f, 0.12f }, 0.5f,
    { 0.5f, -0.6f, 0.4f }, { 0.65f, 0.45f, 1.0f }, 2.2f,
    { 0.7f, 0.4f, 0.9f }, 0.5f, { 0.2f, 0.9f, 1.0f }, 1.4f,
    { 0.12f, 0.06f, 0.2f }, 0.015f, 0.05f,
    { 0.4f, 0.9f, 1.0f }, 1.4f, 0.22f, 0.18f, 1.2f,
    { { -6, 2.0f, -1.0f, { 1.0f, 0.15f, 0.6f }, 70, 10 }, { 6, 2.0f, -1.0f, { 0.1f, 0.85f, 1.0f }, 70, 10 }, { 0, 1.6f, 4.2f, { 0.7f, 0.3f, 1.0f }, 30, 9 } } },
  { "Factory Roof", "luts/factory.cube",
    { 0.14f, 0.07f, 0.05f }, { 0.7f, 0.42f, 0.3f }, { 0.2f, 0.12f, 0.08f }, 0.55f,
    { 0.55f, -0.42f, 0.3f }, { 1.0f, 0.45f, 0.18f }, 5.0f,
    { 0.45f, 0.5f, 0.7f }, 0.6f, { 1.0f, 0.6f, 0.3f }, 1.4f,
    { 0.38f, 0.17f, 0.1f }, 0.03f, 0.05f,
    { 1.0f, 0.6f, 0.3f }, 1.1f, 0.22f, 0.15f, 1.2f,
    { { -6, 1.2f, -3.0f, { 1.0f, 0.4f, 0.1f }, 45, 9 }, { 7, 1.4f, -3.0f, { 1.0f, 0.5f, 0.12f }, 45, 9 }, { 0, 3.0f, 3.5f, { 1.0f, 0.55f, 0.3f }, 16, 10 } } },
};

typedef struct wave_def { float lock; int n; int8_t def[10]; } wave_def;
#define T D_THUG
#define P D_PUNK
#define R D_RUNNER
#define K D_KNIFER
#define B D_BRUISER
#define H D_THROWER
#define G D_GUARD
/* every stage has a rusher (runner), a ranged thrower, a shield guard and a heavy (bruiser) before its boss; the first
   stage brings each new one on its own wave so a player meets it alone */
static const wave_def STAGE1[] = {
  { 0, 3, { T, T, T } }, { 14, 4, { T, T, P, T } }, { 28, 5, { T, P, R, H, T } },
  { 42, 5, { B, G, T, P, H } }, { 56, 1, { D_FOREMAN } },
};
static const wave_def STAGE2[] = {
  { 0, 4, { T, P, R, H } }, { 14, 5, { K, T, G, R, T } }, { 28, 5, { R, H, K, P, G } },
  { 42, 4, { B, K, H, P } }, { 56, 1, { D_ARTHUR } },
};
static const wave_def STAGE3[] = { /* the ringmaster runs the arcade floor as a mid-stage mini boss; the Matron owns the stage */
  { 0, 5, { K, R, P, H, K } }, { 14, 6, { K, G, R, P, H, R } }, { 28, 5, { B, K, R, G, D_RINGMASTER } },
  { 42, 6, { K, R, H, P, G, T } }, { 56, 1, { D_MATRON } },
};
static const wave_def STAGE4[] = {
  { 0, 6, { K, R, G, T, H, R } }, { 14, 4, { B, K, H, G } }, { 28, 7, { K, G, R, H, P, P, T } },
  { 42, 4, { B, B, H, G } }, { 56, 1, { D_COLOSSUS } },
};
#undef H
#undef G
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
    if (!DEFS[w->def[i]].mini && !DEFS[w->def[i]].boss && (i % 2) == 0) { W.spawn_q[n].def = w->def[i], W.spawn_q[n].side = (i & 1) ? -1 : 1, W.spawn_q[n].delay = 60 + i * 40; n++; }
  if (W.players == 2) for (int i = 0; i < w->n && n < 22; i += 3)
    if (!DEFS[w->def[i]].mini && !DEFS[w->def[i]].boss) { W.spawn_q[n].def = w->def[i], W.spawn_q[n].side = 1, W.spawn_q[n].delay = 80 + i * 40; n++; }
  W.spawn_q_n = n;
}

/* ── nodes ──────────────────────────────────────────────────────────── */
static cw_node stage_node;
static t3_gltf *stage_gltf;
static cw_node tree_root(void) { return cw_node_get(cw_tree_root(cw_scene_app_tree()), "Level"); }
static int ncount;

static float hf(int i, int salt) { uint32_t h = (uint32_t)(i * 2654435761u) ^ (uint32_t)(salt * 40503u); h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return (float)(h & 0xffff) / 65535.0f; }

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

static void shadows_on(t3_object *o, bool cast) {
  if (o->type == T3_SKINNED_MESH || o->type == T3_MESH) o->cast_shadow = cast, o->receive_shadow = true;
  for (int i = 0; i < o->child_count; i++) shadows_on(o->children[i], cast);
}
/* the puddles take no shadow map: the street under them shows its own shadows through the water */
static void dry_shadow(t3_object *o) {
  if (o->type == T3_MESH) o->cast_shadow = false, o->receive_shadow = false;
  for (int i = 0; i < o->child_count; i++) dry_shadow(o->children[i]);
}
/* every glTF a stage parsed: released with the stage (each stage used to leak its scenery and props) */
static t3_gltf *kept[MAXPROP + 8];
static int nkept;
/* a glb from the pack as a node (the glTF stays alive until level_free) */
static cw_node spawn_glb(cw_node parent, const char *path, t3_gltf **keep, bool cast) {
  char nm[24];
  snprintf(nm, sizeof nm, "g%d", ncount++);
  cw_node n = cw_node_new("Node3D", nm);
  cw_node_add_child(parent, n);
  size_t len;
  void *glb = cw_asset_load(path, &len);
  if (!glb) { cw_error("brawler: %s missing", path); return n; }
  t3_gltf *g = t3_gltf_parse(glb, len, NULL, NULL);
  free(glb);
  if (!g) { cw_error("brawler: %s did not parse", path); return n; }
  shadows_on(g->scene, cast);
  cw_node3d_set_object(n, g->scene);
  if (keep) *keep = g;
  if (nkept < (int)(sizeof kept / sizeof *kept)) kept[nkept++] = g;
  return n;
}

static cw_node light(cw_node parent, const char *prefix, t3_vec3 pos, const float *c, float energy, float range) {
  char nm[24];
  snprintf(nm, sizeof nm, "%s%d", prefix, ncount++);
  cw_node l = cw_node_new("OmniLight3D", nm);
  cw_node_add_child(parent, l);
  cw_node3d_set_position(l, pos);
  cw_node_set(l, "color", cw_val_color(c[0], c[1], c[2], 1));
  cw_node_set(l, "energy", cw_val_float(energy));
  cw_node_set(l, "range", cw_val_float(range));
  cw_node_set(l, "fog_energy", cw_val_float(0)); /* only the accents scatter into the fog (the grid takes 8 lights) */
  return l;
}
static cw_node acc_node[3];
static void aim(cw_node n, const float *d) {
  float h = sqrtf(d[0] * d[0] + d[2] * d[2]);
  cw_node3d_set_rotation(n, (t3_vec3){ atan2f(d[1], h), atan2f(-d[0], -d[2]), 0 });
}

void level_free(void) {
  if (stage_node.id && cw_node_valid(stage_node)) cw_node_free(stage_node);
  stage_node = (cw_node){ 0 };
  for (int i = 0; i < nkept; i++) t3_release(kept[i]);
  nkept = 0, stage_gltf = NULL;
  for (int i = 0; i < MAXPROP; i++) W.prop[i].node = (cw_node){ 0 };
  for (int i = 0; i < MAXITEM; i++) W.item[i].node = (cw_node){ 0 };
}

void level_build(int stage) {
  level_free();
  rag_level_build();
  const theme *th = &THEMES[stage & 3];
  float len = level_length();
  stage_node = cw_node_new("Node3D", "Stage");
  cw_node_add_child(tree_root(), stage_node);
  cw_node s = stage_node;
  /* environment, post and the lights from the theme */
  cw_node env = cw_node_get(tree_root(), "Env");
  cw_node_set(env, "background", cw_val_color(th->bg[0], th->bg[1], th->bg[2], 1));
  cw_node_set(env, "sky_color", cw_val_color(th->hemi_sky[0], th->hemi_sky[1], th->hemi_sky[2], 1));
  cw_node_set(env, "ground_color", cw_val_color(th->hemi_ground[0], th->hemi_ground[1], th->hemi_ground[2], 1));
  cw_node_set(env, "hemi_energy", cw_val_float(th->hemi_e));
  cw_node fog = cw_node_get(tree_root(), "Fog");
  cw_node_set(fog, "color", cw_val_color(th->fog[0], th->fog[1], th->fog[2], 1));
  cw_node_set(fog, "density", cw_val_float(th->fog_density));
  cw_node_set(fog, "height_falloff", cw_val_float(th->fog_fall));
  cw_node post = cw_node_get(tree_root(), "Post");
  cw_node_set(post, "lut", cw_val_string(th->lut));
  cw_node_set(post, "exposure_ev", cw_val_float(th->exposure + 0.25f));
  cw_node_set(post, "bloom_intensity", cw_val_float(th->bloom));
  cw_node_set(post, "bloom_threshold", cw_val_float(th->bloom_thr));
  cw_node sun = cw_node_get(tree_root(), "Sun");
  cw_node_set(sun, "color", cw_val_color(th->sun[0], th->sun[1], th->sun[2], 1));
  cw_node_set(sun, "energy", cw_val_float(th->sun_e));
  aim(sun, th->sun_dir);
  cw_node fill = cw_node_get(tree_root(), "Fill");
  cw_node_set(fill, "color", cw_val_color(th->fill[0], th->fill[1], th->fill[2], 1));
  cw_node_set(fill, "energy", cw_val_float(th->fill_e * 2.2f));
  { float d[3] = { 0.25f, -0.5f, -0.85f }; aim(fill, d); }
  cw_node rim = cw_node_get(tree_root(), "Rim");
  cw_node_set(rim, "color", cw_val_color(th->rim[0], th->rim[1], th->rim[2], 1));
  cw_node_set(rim, "energy", cw_val_float(th->rim_e * 1.6f));
  { float d[3] = { -0.3f, -0.45f, 0.85f }; aim(rim, d); }
  /* the scenery: street, kerb, pavement, buildings with lit windows, lamp posts (one glb per stage) */
  char path[40];
  snprintf(path, sizeof path, "models/stage%d.glb", stage & 3);
  cw_node scen = spawn_glb(s, path, &stage_gltf, true);
  (void)scen;
  /* puddles (wet stages): glossy water blended over the street, many shapes, placed where water collects (make-world.mjs) */
  if ((stage & 3) != 2) {
    t3_gltf *pg = NULL;
    snprintf(path, sizeof path, "models/puddles%d.glb", stage & 3);
    spawn_glb(s, path, &pg, false);
    if (pg) dry_shadow(pg->scene);
  }
  /* a light under every lamp post, and the stage's coloured accents (they follow the camera) */
  for (float x = 2; x < len + 6; x += 11.0f)
    light(s, "L", (t3_vec3){ x, 3.9f, -2.9f }, th->lamp, th->lamp_e * 38, 11);
  for (int i = 0; i < 3; i++) acc_node[i] = light(s, "A", (t3_vec3){ th->acc[i].dx, th->acc[i].y, th->acc[i].z }, th->acc[i].c, th->acc[i].e * (i == 2 ? 2.5f : 1.0f), th->acc[i].range);
  /* breakable props */
  cw_rng r;
  cw_rng_seed(&r, 99 + (uint64_t)stage);
  int np = 0;
  static const char *PROP_GLB[3] = { "models/crate.glb", "models/barrel.glb", "models/bin.glb" };
  for (float x = 8; x < len - 4 && np < MAXPROP; x += 5.2f + cw_rng_float(&r) * 2.0f, np++) {
    prop_t *p = &W.prop[np];
    p->used = true, p->kind = cw_rng_int(&r, 0, 2), p->hp = 3;
    p->x = x, p->z = cw_rng_range(&r, -2.0f, 2.0f);
    p->drop = cw_rng_int(&r, 0, 9);
    p->node = spawn_glb(s, PROP_GLB[p->kind], NULL, true);
    static const float PY[3] = { 0.42f, 0.49f, 0.57f };
    cw_node3d_set_position(p->node, (t3_vec3){ x, PY[p->kind], p->z });
    cw_node3d_set_rotation(p->node, (t3_vec3){ 0, (float)cw_rng_range(&r, -0.4f, 0.4f), 0 });
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
      float y = p->kind == 0 ? 0.42f : p->kind == 1 ? 0.49f : 0.57f;
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
  /* g_cam (debug "cam" command in the autopilot build): zoom toward a point on the street for close-ups */
  cw_node3d_set_position(cam, (t3_vec3){ W.cam_x + g_cam[1] + shx, 1.0f + 2.9f * g_cam[0] + shy, -1.66f + g_cam[2] + 9.36f * g_cam[0] });
  cw_node3d_set_rotation(cam, (t3_vec3){ -0.3f, 0, 0 });
  cw_node sun = cw_node_get(tree_root(), "Sun");
  cw_node3d_set_position(sun, (t3_vec3){ W.cam_x - 8, 14, -8 });
  {
    const theme *th = &THEMES[W.stage & 3];
    for (int i = 0; i < 3; i++)
      if (acc_node[i].id && cw_node_valid(acc_node[i])) cw_node3d_set_position(acc_node[i], (t3_vec3){ W.cam_x + th->acc[i].dx, th->acc[i].y, th->acc[i].z });
  }
}
