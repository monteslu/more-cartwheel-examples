/* brawler levels: four stages built from scene nodes, wave tables, breakable props, the camera. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/lightmap.h"
#include "cartwheel/explosions.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "game.h"

/* per stage mood: the key light (a low sun or moon from behind the buildings, so they cast long shadows and
   shafts across the street), a cool fill from the camera side, a rim from behind, fog, grade, coloured accents. */
static cw_node lm_root; static int lm_want; static bool lm_pending;
static t3_mesh *lm_ms[64]; static int lm_nm;
static void lm_collect(t3_object *o, void *u) { (void)u; if (o->type == T3_MESH && lm_nm < 64) lm_ms[lm_nm++] = (t3_mesh *)o; }
bool g_legacy_fill; int g_mirror_z; /* see level_build: the front fill light */
static float acc_y[3], acc_z[3];
#define FILL_Y 4.6f
#define FILL_Z 5.6f
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
    { { -6, 3.2f, -2.6f, { 1.0f, 0.15f, 0.6f }, 85, 11 }, { 6, 3.2f, -2.6f, { 0.1f, 0.85f, 1.0f }, 85, 11 }, /* above the far lane, not in the crowd: walking under them blew bodies out white */ { 0, 1.6f, 4.2f, { 0.7f, 0.3f, 1.0f }, 30, 9 } } },
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
#define Y D_WANKER
#define C D_CREEP
/* every stage has a rusher (runner), a ranged thrower, a shield guard and a heavy (bruiser) before its boss; the first
   stage brings each new one on its own wave so a player meets it alone. The wanker (haymaker, shoulder barge) is in
   every stage: first on the alley's second wave with thugs and a punk, then one or two a stage */
static const wave_def STAGE1[] = {
  { 0, 3, { T, T, T } }, { 14, 4, { T, Y, P, T } }, { 28, 5, { T, P, R, H, C } },
  { 42, 5, { B, G, T, P, H } }, { 56, 1, { D_FOREMAN } },
};
static const wave_def STAGE2[] = {
  { 0, 4, { T, P, R, H } }, { 14, 5, { K, Y, G, R, T } }, { 28, 5, { R, H, K, C, G } },
  { 42, 5, { B, K, H, Y, P } }, { 56, 1, { D_ARTHUR } },
};
static const wave_def STAGE3[] = { /* the ringmaster runs the arcade floor as a mid-stage mini boss; the Matron owns the stage */
  { 0, 5, { K, R, P, H, K } }, { 14, 6, { K, G, R, C, H, R } }, { 28, 5, { B, K, R, G, D_RINGMASTER } },
  { 42, 6, { K, R, H, P, G, Y } }, { 56, 1, { D_MATRON } },
};
static const wave_def STAGE4[] = {
  { 0, 6, { K, R, G, T, H, R } }, { 14, 4, { B, K, H, G } }, { 28, 7, { K, G, R, H, Y, P, T } },
  { 42, 5, { B, B, H, G, C } }, { 56, 1, { D_COLOSSUS } },
};
#undef Y
#undef C
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

/* ── the jukebox (stage 2): the model, its light, the ring at its foot and the shockwave on screen ── */
static cw_node juke_node, juke_light, juke_ring, juke_wave;
static t3_gltf *juke_gltf;
static t3_material *juke_mat[3]; /* glow tubes, window, buttons */
static t3_color juke_e0[3];
static int juke_fired_seen;
static void juke_find(t3_object *o) {
  static const char *N[3] = { "juke_glow", "juke_window", "juke_button" };
  if (o->type == T3_MESH) {
    t3_mesh *m = (t3_mesh *)o;
    for (int i = 0; i < m->material_count; i++)
      for (int k = 0; k < 3; k++)
        if (m->materials[i] && m->materials[i]->name && !strcmp(m->materials[i]->name, N[k])) { juke_mat[k] = m->materials[i]; juke_e0[k] = m->materials[i]->emissive; }
  }
  for (int i = 0; i < o->child_count; i++) juke_find(o->children[i]);
}
static void juke_build(cw_node s) {
  memset(juke_mat, 0, sizeof juke_mat);
  juke_fired_seen = 0;
  juke_node = spawn_glb(s, "models/jukebox.glb", &juke_gltf, true);
  cw_node3d_set_position(juke_node, (t3_vec3){ JUKE_X, 0, JUKE_Z });
  if (juke_gltf) juke_find(juke_gltf->scene);
  const float warm[3] = { 1.0f, 0.55f, 0.25f };
  juke_light = light(s, "J", (t3_vec3){ JUKE_X, 1.1f, JUKE_Z + 0.8f }, warm, 4.0f, 6.0f);
  juke_ring = cw_node_new("Decal3D", "JukeRing");
  cw_node_add_child(s, juke_ring);
  cw_node_set(juke_ring, "texture", cw_val_string("decals/ring.png"));
  cw_node_set(juke_ring, "blend", cw_val_enum("ADD"));
  cw_node3d_set_rotation(juke_ring, (t3_vec3){ -1.5708f, 0, 0 });
  cw_node3d_set_position(juke_ring, (t3_vec3){ JUKE_X, -20, JUKE_Z });
  juke_wave = cw_node_new("Shockwave3D", "JukeWave");
  cw_node_add_child(s, juke_wave);
  cw_node_set(juke_wave, "ground", cw_val_bool(true));
  cw_node_set(juke_wave, "max_radius", cw_val_float(JUKE_RMAX));
  cw_node_set(juke_wave, "duration", cw_val_float((float)JUKE_TICKS / TPS));
  cw_node_set(juke_wave, "thickness", cw_val_float(0.7f));
  cw_node_set(juke_wave, "strength", cw_val_float(0.12f));
  cw_node3d_set_position(juke_wave, (t3_vec3){ JUKE_X, 0.05f, JUKE_Z });
}
/* what the jukebox shows: dim and warm until Arthur wakes it; flickering while his fist is up (the tell); then lit and
   cycling colours to the beat of his theme; a bright ring running out along the floor with the shockwave */
static void juke_show(void) {
  if (!juke_node.id || !cw_node_valid(juke_node)) return;
  const ent *a = W.juke.owner >= 0 && W.e[W.juke.owner].used ? &W.e[W.juke.owner] : NULL;
  if (!a) for (int i = 0; i < MAXE; i++) if (W.e[i].used && W.e[i].def == D_ARTHUR) a = &W.e[i];
  bool tele = a && a->st == S_ATK && a->atk == A_JUKE && a->t < 0;
  float tk = tele ? (float)(a->t + 36) / 36.0f : 0;
  float lvl, r, g, b;
  if (W.juke.on) { /* his theme: the tubes cycle red, amber, pink, blue on the beat (152 a minute) */
    float ph = (float)W.tick / (60.0f * 60.0f / 152.0f), f = ph - floorf(ph);
    int k = (int)floorf(ph) & 3;
    static const float C[4][3] = { { 1, 0.25f, 0.15f }, { 1, 0.6f, 0.15f }, { 1, 0.3f, 0.6f }, { 0.35f, 0.55f, 1 } };
    lvl = 3.2f + 2.0f * expf(-f * 5);
    r = C[k][0], g = C[k][1], b = C[k][2];
  } else { lvl = tele ? 1.0f + 3.0f * tk * (((W.tick / 3) & 1) ? 1.0f : 0.25f) : 0.8f; r = 1, g = 0.55f, b = 0.25f; }
  if (W.juke.t > 0 && W.juke.t < 8) lvl += 6.0f * (1 - (float)W.juke.t / 8); /* the thump */
  for (int k = 0; k < 3; k++) if (juke_mat[k]) {
    float m = k == 0 ? lvl : k == 1 ? 0.6f + 0.3f * lvl : 0.5f + 0.4f * lvl;
    t3_color e0 = juke_e0[k];
    juke_mat[k]->emissive = k == 0 ? (t3_color){ 0.7f * r * m, 0.7f * g * m, 0.7f * b * m } : (t3_color){ e0.r * m, e0.g * m, e0.b * m };
  }
  cw_node_set(juke_light, "color", cw_val_color(r, g, b, 1));
  cw_node_set(juke_light, "energy", cw_val_float(1.1f * lvl)); /* (2.5: his theme washed the whole street out, stains and all) */
  if (W.juke.fired != juke_fired_seen) { juke_fired_seen = W.juke.fired; cw_shockwave_fire(juke_wave); }
  if (W.juke.t > 0) { /* the ring on the floor, where the rule says it is */
    float k = 1 - (float)W.juke.t / JUKE_TICKS;
    cw_node3d_set_position(juke_ring, (t3_vec3){ JUKE_X, 0.2f, JUKE_Z });
    cw_node_set(juke_ring, "size", cw_val_vec3(W.juke.r * 2, W.juke.r * 2, 0.6f));
    cw_node_set(juke_ring, "color", cw_val_color(1, 0.6f, 0.25f, 0.9f * k + 0.1f));
  } else if (tele) { /* the tell at its foot: a ring that brightens until the fist comes down */
    cw_node3d_set_position(juke_ring, (t3_vec3){ JUKE_X, 0.2f, JUKE_Z });
    cw_node_set(juke_ring, "size", cw_val_vec3(1.6f + 0.6f * tk, 1.6f + 0.6f * tk, 0.6f));
    cw_node_set(juke_ring, "color", cw_val_color(1, 0.22f, 0.08f, 0.3f + 0.7f * tk));
  } else cw_node3d_set_position(juke_ring, (t3_vec3){ JUKE_X, -20, JUKE_Z });
}

void level_free(void) {
  if (stage_node.id && cw_node_valid(stage_node)) cw_node_free(stage_node);
  stage_node = (cw_node){ 0 };
  juke_node = juke_light = juke_ring = juke_wave = (cw_node){ 0 };
  juke_gltf = NULL;
  memset(juke_mat, 0, sizeof juke_mat);
  for (int i = 0; i < nkept; i++) t3_release(kept[i]);
  nkept = 0, stage_gltf = NULL;
  for (int i = 0; i < MAXPROP; i++) W.prop[i].node = (cw_node){ 0 };
  for (int i = 0; i < MAXITEM; i++) W.item[i].node = (cw_node){ 0 };
}

static void lm_apply(void);
static bool keep_next;
void level_keep_next(void) { keep_next = stage_node.id && cw_node_valid(stage_node); }
void level_apply_bake(void) { if (lm_pending) lm_apply(); }

/* the breakable props: their places and what they drop, from the stage's own seed. keep: the nodes already stand there */
static void level_props(cw_node s, int stage, float len, bool keep) {
  static cw_node nodes[MAXPROP];
  cw_rng r;
  cw_rng_seed(&r, 99 + (uint64_t)stage);
  int np = 0;
  static const char *PROP_GLB[3] = { "models/crate.glb", "models/barrel.glb", "models/bin.glb" };
  for (float x = 8; x < len - 4 && np < MAXPROP; x += 5.2f + cw_rng_float(&r) * 2.0f, np++) {
    prop_t *p = &W.prop[np];
    p->used = true, p->kind = cw_rng_int(&r, 0, 2), p->hp = 3;
    p->x = x, p->z = cw_rng_range(&r, -2.0f, 2.0f);
    p->drop = cw_rng_int(&r, 0, 9);
    if (!keep) nodes[np] = spawn_glb(s, PROP_GLB[p->kind], NULL, true);
    p->node = nodes[np];
    static const float PY[3] = { 0.42f, 0.49f, 0.57f };
    cw_node3d_set_position(p->node, (t3_vec3){ x, PY[p->kind], p->z });
    cw_node3d_set_rotation(p->node, (t3_vec3){ 0, (float)cw_rng_range(&r, -0.4f, 0.4f), 0 });
  }
}

void level_build(int stage) {
  if (keep_next) { /* built under the loading screen: only the round's state is new */
    keep_next = false;
    rag_level_build();
    level_props(stage_node, stage, level_length(), true);
    ncount = 0;
    return;
  }
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
  /* what a frame's GPU time buys (B-PT19, findings/brawler.md: measured on the 890M at 1080p, four fighters and 14 enemies): plain
     no fog grid (1.3 ms); two shadow cascades over the
     24 m the street camera sees (1 ms, shadows and shading together); ambient occlusion with half the rays (0.3 ms) */
  bool oldfx = false;
#ifdef BRAWLER_AUTOPILOT
  { const char *x = getenv("BRAWLER_EXP"); oldfx = x && strstr(x, "oldfx"); } /* the earlier settings: the perf gate's control */
#endif
  if (!oldfx) {
    cw_node_set(fog, "volumetric", cw_val_bool(false));
    cw_node_set(fog, "density", cw_val_float(0)); /* a full-screen pass (0.37 ms) for a haze the near street hardly shows */
    cw_node_set(fog, "visible", cw_val_bool(false));
    cw_node_set(post, "ssao", cw_val_bool(false)); /* the bake carries it (lm_apply) */
    cw_node_set(sun, "shadow_cascades", cw_val_int(2));
    cw_node_set(sun, "shadow_distance", cw_val_float(24));
    cw_node_set(sun, "shadow_size", cw_val_int(1536));
  }
  /* the scenery: street, kerb, pavement, buildings with lit windows, lamp posts (one glb per stage) */
  char path[40];
  snprintf(path, sizeof path, "models/stage%d.glb", stage & 3);
  cw_node scen = spawn_glb(s, path, &stage_gltf, true);
  (void)scen;
  if (!oldfx && !g_legacy_fill && stage_gltf) { /* the scenery takes baked sky light and bounce (LIT: its materials and real-time lights stay): each mesh a STATIC MeshInstance3D */
    lm_nm = 0;
    t3_object_traverse(stage_gltf->scene, lm_collect, NULL);
    t3_mesh **ms = lm_ms; int nm = lm_nm;
    for (int i = 0; i < nm; i++) {
      t3_retain(ms[i]); /* its scene held the only reference */
      t3_object_remove(((t3_object *)ms[i])->parent, (t3_object *)ms[i]);
      char mn[16]; snprintf(mn, sizeof mn, "stage%d", i);
      cw_node mi = cw_node_new("MeshInstance3D", mn);
      cw_node_add_child(s, mi);
      cw_node_set(mi, "gi_mode", cw_val_enum("STATIC"));
      cw_mesh3d_set_procedural(mi, ms[i]);
      t3_release(ms[i]);
    }
    lm_root = s, lm_want = stage & 3, lm_pending = nm > 0;
  }
  /* puddles (wet stages): glossy water blended over the street, many shapes, placed where water collects (make-world.mjs) */
  if ((stage & 3) != 2) {
    t3_gltf *pg = NULL;
    snprintf(path, sizeof path, "models/puddles%d.glb", stage & 3);
    spawn_glb(s, path, &pg, false);
    if (pg) dry_shadow(pg->scene);
  }
  /* a light under every lamp post, and the stage's coloured accents (they follow the camera) */
  for (float x = 2; x < len + 6; x += 11.0f)
    { /* the street lamps are in the bake: their pools on the scenery cost nothing a frame (0.25 ms), bodies walk through by the sun, fill and rim */
      cw_node lamp = light(s, "L", (t3_vec3){ x, 3.9f, -2.9f }, th->lamp, th->lamp_e * 38, 11);
      if (!oldfx) cw_node_set(lamp, "bake_direct", cw_val_bool(true)), cw_node_set(lamp, "realtime", cw_val_bool(false));
    }
  /* the front fill (the third accent) stood low just in front of the near lane: it blew the near bodies out and, mirrored on
     the wet street, made the hot spot at the bottom of the picture. Up behind the camera's line of sight now, further off and
     wider: the near lane takes about a third of what it did, the middle what it had, and its mirror image on the street falls
     below the picture (game.mirrorz, z of the mirror point; the picture's lower edge meets the street near z 5.5) */
  for (int i = 0; i < 3; i++) {
    float e = th->acc[i].e * (i == 2 ? 2.5f : 1.0f), y = th->acc[i].y, zz = th->acc[i].z, rg = th->acc[i].range;
    if (i == 2 && !g_legacy_fill) { /* a whole-screen light in every pixel's loop (0.5 ms): its work goes to the Fill sun, which costs one more term */
      acc_y[i] = FILL_Y, acc_z[i] = FILL_Z, acc_node[i] = (cw_node){ 0 };
      cw_node_set(fill, "energy", cw_val_float(th->fill_e * 2.2f * 1.3f));
      continue;
    }
    acc_y[i] = y, acc_z[i] = zz;
    acc_node[i] = light(s, "A", (t3_vec3){ th->acc[i].dx, y, zz }, th->acc[i].c, e, rg);
    cw_node_set(acc_node[i], "bake", cw_val_bool(false)); /* it follows the camera */
  }
  g_mirror_z = (int)((acc_z[2] * 3.9f + 7.7f * acc_y[2]) / (3.9f + acc_y[2]) * 1000.0f + 0.5f);
  if ((stage & 3) == 1) juke_build(s); /* Arthur's jukebox */
  level_props(s, stage, len, false);
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
  rumble(-1, p->kind == 1 ? 0.7f : 0.5f, 0.2f, p->kind == 1 ? 260 : 180); /* a crate or barrel bursts: heavy, low */
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

/* the scenery's bake (assets/lightmaps/stageN.cwlm; written by the game itself with BRAWLER_BAKE=<file>, brawler_auto natively): indirect
   light only, so the sun and lamps stay real time with their shadows and specular. It replaces the screen-space occlusion on the
   scenery: corners, doorways and the alleys are dark because the sky does not reach them (ssao off: 0.85 ms); the lamps' direct light is in it too */
static void lm_apply(void) {
  static cw_lightmap_data *LM[4];
  lm_pending = false;
  cw_tree *t = cw_scene_app_tree();
#ifdef BRAWLER_AUTOPILOT
  const char *out = getenv("BRAWLER_BAKE");
  if (out && *out) {
    cw_lightmap_settings st = cw_lightmap_settings_default();
    st.texel_density = 5, st.max_size = 2048, st.samples = 64, st.bounces = 2, st.probe_spacing = 0, st.denoise = 2;
    LM[lm_want] = cw_lightmap_bake(t, lm_root, &st);
    if (LM[lm_want]) { cw_lightmap_save(LM[lm_want], out); cw_lightmap_info in; cw_lightmap_get_info(LM[lm_want], &in); fprintf(stderr, "brawler: baked %s: %d meshes, %d texels\n", out, in.meshes, in.texels); }
  }
#endif
  if (!LM[lm_want]) { char pt[40]; snprintf(pt, sizeof pt, "lightmaps/stage%d.cwlm", lm_want); LM[lm_want] = cw_lightmap_load(pt); }
  if (LM[lm_want]) cw_lightmap_apply(t, lm_root, LM[lm_want], 1);
  else cw_error("brawler: stage %d has no lightmap", lm_want);
}

/* items and props each frame */
void level_update(void) {
  if (lm_pending) lm_apply();
  juke_show();
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
    if (it->used && !it->node.id) { /* the item's model (sim.c item_model): an apple, a bat, a pipe, a knife, a coin */
      char nm[16];
      snprintf(nm, sizeof nm, "item%d", ncount++);
      it->node = cw_node_new("Node3D", nm);
      cw_node_add_child(stage_node, it->node);
      t3_object *o = item_model(it->kind);
      if (o) { cw_node3d_set_object(it->node, o); t3_release(o); }
    }
    if (!it->used && it->node.id) { cw_node_free(it->node); it->node = (cw_node){ 0 }; }
    if (it->used && it->node.id) {
      float s, c;
      cw_sincos((float)(it->t + i * 9) * 0.07f, &s, &c);
      /* weapons lie on the street turning slowly; the apple and the coin bob just above it (the coin spins) */
      bool lying = it->kind >= 1 && it->kind <= 3;
      float y = lying ? 0.05f : it->kind == 4 ? 0.2f + 0.04f * s : 0.15f + 0.03f * s;
      cw_node3d_set_position(it->node, (t3_vec3){ it->x, it->y + y, it->z });
      cw_node3d_set_rotation(it->node, (t3_vec3){ 0, (float)it->t * (it->kind == 4 ? 0.06f : 0.012f), lying ? 1.5708f : 0 });
    }
  }
}

bool g_legacy_shake;
int g_camjit; /* debug game.camjit: the largest camera offset from its path on any frame, in mm (0: the camera never shakes) */
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
  if (W.shake > 0 && g_legacy_shake) { /* the camera never shakes now (pads rumble); the old shake only for the gate's control */
    float a = W.shake_amp * (float)W.shake / 10.0f;
    shx = (hf(W.tick, 1) - 0.5f) * a * 2, shy = (hf(W.tick, 2) - 0.5f) * a * 2;
  }
  { int j = (int)((fabsf(shx) + fabsf(shy)) * 1000 + 0.5f); if (j > g_camjit) g_camjit = j; }
  /* g_cam (debug "cam" command in the autopilot build): zoom toward a point on the street for close-ups */
  cw_node3d_set_position(cam, (t3_vec3){ W.cam_x + g_cam[1] + shx, 1.0f + 2.9f * g_cam[0] + shy, -1.66f + g_cam[2] + 9.36f * g_cam[0] });
  cw_node3d_set_rotation(cam, (t3_vec3){ -0.3f, 0, 0 });
  cw_node sun = cw_node_get(tree_root(), "Sun");
  cw_node3d_set_position(sun, (t3_vec3){ W.cam_x - 8, 14, -8 });
  {
    const theme *th = &THEMES[W.stage & 3];
    for (int i = 0; i < 3; i++)
      if (acc_node[i].id && cw_node_valid(acc_node[i])) cw_node3d_set_position(acc_node[i], (t3_vec3){ W.cam_x + th->acc[i].dx, acc_y[i], acc_z[i] });
  }
}
