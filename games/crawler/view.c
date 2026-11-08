/* crawler view: draws the simulation. Render only: nothing here feeds the rules.
 *
 *   the floor   mazebuild.c builds its still scenery (the baked floor, the wall kit, the rock, torches and decor);
 *               this file culls the walls to the view every frame and cuts down the ones between the camera and a
 *               player (a stub of the same wall rises again when they move on).
 *   things      monsters, heroes, shots, pickups, spawners, doors and the floor's features: instanced meshes (one
 *               per animation frame of a kind: thousands of monsters are a handful of draws and no skinning),
 *               rewritten every frame, culled to the view on the CPU.
 *   light       the floor is baked and drawn unlit; everything that moves takes the bake's light where it stands
 *               (the probe grid), the heroes' lamps and the shots' lights are clustered omni lights, and every moving
 *               light shows on the baked floor as a pool of added light (splats); blob shadows under everything. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/math.h"
#include "cartwheel/render3d.h"
#include "cartwheel/rim_light.h"
#include "cartwheel/splats.h"
#include "cartwheel/vfs.h"
#include "game.h"
#include "mazebuild.h"

t3_color lin(float r, float g, float b) { return mb_lin(r, g, b); }
const float PCOL[MAXP][3] = { { 0.25f, 0.65f, 1.0f }, { 1.0f, 0.55f, 0.12f }, { 0.4f, 0.95f, 0.35f }, { 0.95f, 0.3f, 0.8f } };

static const t3_mat4 ZERO;
#define FAR_SPHERE 400.0f

enum { HERO_CLIPS = 4 };
typedef struct kindv { mb_chr c; int nwalk, natk; t3_instanced_mesh *im[MB_MAXF], *edge[MB_MAXF]; float r; t3_material *edge_mat; } kindv;

static struct {
  cw_node root;
  /* the floor */
  bool have_floor, lit;
  mb_built b;
  int floor_built, theme;
  maze m;            /* the floor the view was built for (mb_built points at it) */
  char *text;        /* its .cwmaze text (the bake hash) */
  t3_color wall_light[MB_VARIANTS][MZ_MAX * MZ_MAX];
  float cut[MZ_MAX][MZ_MAX];
  t3_instanced_mesh *stub, *trapwall, *breakable[2], *door[8], *door_glow[8], *spawner[3], *spawner_glow[3];
  int door_frames;
  t3_instanced_mesh *exit_im, *exit_glow, *tele, *tele_glow, *trap;
  t3_instanced_mesh *lock, *lock_glow; /* the key sign on a shut door's top: it wants a key */
  /* creatures and heroes */
  kindv k[EK_KINDS];
  mb_chr hero[CL_KINDS];
  t3_object *pl[MAXP];
  cw_node pn[MAXP], pl_light[MAXP];
  t3_mesh *pm[MAXP][MB_MAXF][4]; /* body, team trim, (unused), the dark outline */
  int pl_frame[MAXP], pl_cls[MAXP];
  t3_material *pl_mat[MAXP], *pl_trim[MAXP];
  t3_mesh *pl_ring[MAXP];
  cw_node ring_node[MAXP];
  /* shots and pickups */
  t3_instanced_mesh *shot[SK_KINDS], *item[IT_KINDS][4], *item_glow[IT_KINDS][4], *charm[CH_KINDS], *charm_glowf[CH_KINDS], *embers;
  cw_splats *shadows, *pools, *glows;
  t3_instanced_mesh *beam; /* soft columns of light over what matters: keys, potions, charms, spawners, the exit */
  int nbeam;
  view_stats st;
  t3_vec3 eye, at;
  float aspect, cx, cy;
  bool cam_init;
} V;

float g_cam_dist;

/* ── helpers ────────────────────────────────────────────────────────── */

static t3_instanced_mesh *imesh(const char *name, t3_geometry *g, t3_material *m, int n) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  im->mesh.base.cast_shadow = false;
  im->mesh.base.receive_shadow = false;
  for (int i = 0; i < n; i++) {
    t3_instanced_mesh_set_matrix_at(im, i, &ZERO);
    t3_instanced_mesh_set_color_at(im, i, (t3_color){ 1, 1, 1 });
  }
  im->instance_matrix->usage = T3_DYNAMIC_DRAW;
  im->instance_color->usage = T3_DYNAMIC_DRAW;
  t3_sphere sph = { { 0, 0, 0 }, FAR_SPHERE };
  t3_instanced_mesh_set_bounding_sphere(im, &sph);
  cw_node node = cw_node_new("Node3D", name);
  cw_node_add_child(V.root, node);
  t3_object *wrap = t3_group_new();
  t3_object_add(wrap, &im->mesh.base);
  cw_node3d_set_object(node, wrap);
  t3_release(wrap), t3_release(g), t3_release(m);
  im->count = 0;
  return im;
}
static t3_material *mat_std(float rough, float metal) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->vertex_colors = true;
  m->roughness = rough;
  m->metalness = metal;
  return m;
}
static t3_material *mat_glow(void) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  return m;
}
static t3_material *mat_emissive(float r, float g, float b, float k) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->color = (t3_color){ r * k, g * k, b * k };
  return m;
}
/* the view's angle (DESIGN.md): the camera tilts g_tilt degrees from straight down toward the south, and everything that
 * stands up (characters, monsters, upright pickups, spawners) leans g_lean degrees back toward the camera from its
 * feet: the classic three-quarter cheat. The maze, the floor's rings and shadows never lean. Both are debug fields
 * (dev.tilt, dev.lean) for the comparison runs. */
float g_tilt = 15.0f, g_lean = 15.0f;
bool g_hide_rings; /* key-art captures (the autopilot builds): no team rings */
static t3_quat lean_q(float yaw) {
  float l = g_lean * 3.14159265f / 180.0f;
  t3_quat qx = { cw_sin(l * 0.5f), 0, 0, cw_cos(l * 0.5f) }, qy = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
  return t3_quat_multiply(qx, qy);
}
/* a point moved d metres along its line of sight toward the camera: it draws in the same place on screen (a hair
 * larger) but in front of everything less than d above the floor around it. Heroes, shots and pickups come this way, so
 * no crowd and no wall hides them; their shadows and rings stay on the floor. */
static t3_vec3 pull(float x, float y, float z, float d) {
  t3_vec3 r = { V.eye.x - x, V.eye.y - y, V.eye.z - z };
  float l = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z);
  if (l < 1e-3f) return (t3_vec3){ x, y, z };
  return (t3_vec3){ x + r.x * d / l, y + r.y * d / l, z + r.z * d / l };
}
#define PULL_HERO 3.2f
#define PULL_SHOT 3.6f
#define PULL_ITEM 2.6f
#define DOOR_SHUT_DEPTH 3.0f /* a shut door's gate stretched through its tile: 0.64 m of bars read as a rail from above */
/* a standing thing: yawed, then leaned toward the camera from its feet */
static void put_up(t3_instanced_mesh *im, int i, float x, float y, float z, float yaw, float s) {
  t3_mat4 m;
  t3_mat4_compose(&m, (t3_vec3){ x, y, z }, lean_q(yaw), (t3_vec3){ s, s, s });
  t3_instanced_mesh_set_matrix_at(im, i, &m);
}
static void put(t3_instanced_mesh *im, int i, float x, float y, float z, float yaw, float s) {
  t3_mat4 m;
  t3_quat q = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
  t3_mat4_compose(&m, (t3_vec3){ x, y, z }, q, (t3_vec3){ s, s, s });
  t3_instanced_mesh_set_matrix_at(im, i, &m);
}
static void put_s(t3_instanced_mesh *im, int i, float x, float y, float z, float yaw, float sx, float sy, float sz) {
  t3_mat4 m;
  t3_quat q = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
  t3_mat4_compose(&m, (t3_vec3){ x, y, z }, q, (t3_vec3){ sx, sy, sz });
  t3_instanced_mesh_set_matrix_at(im, i, &m);
}
static void put_q(t3_instanced_mesh *im, int i, float x, float y, float z, t3_quat q, float s) {
  t3_mat4 m;
  t3_mat4_compose(&m, (t3_vec3){ x, y, z }, q, (t3_vec3){ s, s, s });
  t3_instanced_mesh_set_matrix_at(im, i, &m);
}
static void done(t3_instanced_mesh *im, int n) {
  if (!im) return;
  im->count = n;
  im->instance_matrix->version++;
  im->instance_color->version++;
}
static uint32_t hash2(uint32_t a, uint32_t b) {
  uint32_t h = a * 374761393u + b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float h01(uint32_t a, uint32_t b) { return (float)(hash2(a, b) & 0xffffff) / 16777216.0f; }

/* a model's frames as instanced meshes (surface and glow), or a stand-in shape when the file is missing */
static int model_meshes(const char *file, const char *name, t3_instanced_mesh **surf, t3_instanced_mesh **glow, int maxf, int cap, t3_geometry *(*fallback)(void), float rough) {
  mb_chr c;
  int n = 0;
  if (cw_vfs_exists(file) && mb_chr_load(&c, file)) {
    n = CW_MIN(c.total, maxf);
    for (int f = 0; f < n; f++) {
      surf[f] = imesh(name, t3_retain(c.geo[0][f]), mat_std(rough, 0.1f), cap);
      if (glow) glow[f] = c.ngroup > 1 && c.geo[1][f] ? imesh(name, t3_retain(c.geo[1][f]), mat_glow(), cap) : NULL;
    }
    mb_chr_free(&c);
  } else if (fallback) {
    surf[0] = imesh(name, fallback(), mat_std(rough, 0.1f), cap);
    if (glow) glow[0] = NULL;
    n = 1;
  }
  return n;
}

static t3_geometry *geo_colored(t3_geometry *g, float r, float gg, float b) {
  int n = g->attributes[T3_ATTR_POSITION]->count;
  float *c = malloc(sizeof(float) * 3 * (size_t)n);
  t3_color l = lin(r, gg, b);
  for (int i = 0; i < n; i++) c[i * 3] = l.r, c[i * 3 + 1] = l.g, c[i * 3 + 2] = l.b;
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, c, n, 3));
  free(c);
  return g;
}
static t3_geometry *fb_box(void) { return geo_colored(t3_box_geometry_new(0.6f, 0.6f, 0.6f, 1, 1, 1), 0.6f, 0.5f, 0.3f); }
static t3_geometry *fb_orb(void) { return geo_colored(t3_sphere_geometry_new(0.25f, 10, 8), 1, 1, 1); }
static t3_geometry *fb_plate(void) {
  t3_geometry *g = t3_cylinder_geometry_new(0.9f, 0.9f, 0.08f, 20, 1, false);
  return geo_colored(g, 0.3f, 0.3f, 0.32f);
}

/* ── init ───────────────────────────────────────────────────────────── */

static const char *const MON_FILE[EK_KINDS] = { "models/shade.cwchar", "models/ghoul.cwchar", "models/imp.cwchar", "models/slinger.cwchar", "models/mon_hexer.cwchar",
                                                "models/mon_dread.cwchar", "models/mon_pilferer.cwchar", "models/ogre.cwchar", "models/hound.cwchar", "models/bloater.cwchar",
                                                "models/skitter.cwchar", "models/sporeling.cwchar" };
static const char *const MON_NAME[EK_KINDS] = { "Shades", "Ghouls", "Imps", "Slingers", "Hexers", "Dreads", "Pilferers", "Ogres", "Hounds", "Bloaters", "Skitters", "Sporelings" };
static const int MON_CAP[EK_KINDS] = { 900, 900, 600, 500, 500, 24, 16, 400, 600, 400, 900, 700 };
static const char *const HERO_FILE[CL_KINDS] = { "models/hero_brute.cwchar", "models/hero_warden.cwchar", "models/hero_mage.cwchar", "models/hero_ranger.cwchar" };

/* a geometry pushed out along its normals by d (an outline's hull: drawn back faces only, it shows as a dark line round
 * the silhouette) */
static t3_geometry *hull(t3_geometry *g, float d) {
  t3_attribute *pa = g->attributes[T3_ATTR_POSITION], *na = g->attributes[T3_ATTR_NORMAL];
  if (!pa || !na || !g->index) return NULL;
  int n = pa->count;
  const float *p = t3_attribute_f32(pa), *nr = t3_attribute_f32(na);
  float *q = malloc(sizeof(float) * 3 * (size_t)n);
  for (int i = 0; i < n * 3; i++) q[i] = p[i] + nr[i] * d;
  t3_geometry *h = t3_geometry_new();
  t3_geometry_set_attribute(h, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, q, n, 3));
  t3_geometry_set_attribute(h, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nr, n, 3));
  t3_geometry_set_index(h, t3_attribute_new(g->index->type, g->index->array, g->index->count, 1));
  free(q);
  return h;
}
/* each kind's identity colour: its rim, its spawner's glow (DESIGN.md, the look) */
static const float KIND_RIM[EK_KINDS][3] = { { 0.55f, 0.95f, 1.0f }, { 1.0f, 0.9f, 0.5f }, { 1.0f, 0.3f, 0.08f }, { 1.0f, 0.65f, 0.1f }, { 0.75f, 0.3f, 1.0f }, { 0.45f, 0.7f, 1.0f },
                                             { 1.0f, 0.85f, 0.15f }, { 1.0f, 0.12f, 0.1f }, { 1.0f, 0.45f, 0.05f }, { 0.55f, 1.0f, 0.15f }, { 0.1f, 1.0f, 0.85f }, { 0.35f, 1.0f, 0.45f } };
float g_rim_k = 1.5f; /* the edge's brightness (debug field dev.rim_k) */
/* a kind's edge on this tileset: its identity colour, but on the abyss's burning floor the warm kinds (imps, slingers,
 * pilferers, ogres, hounds) burn white-hot at the edge instead, so they part from the orange lava by value and hue */
static t3_color edge_color(int k, int theme, float rk) {
  const float *c = KIND_RIM[k];
  t3_color kc = lin(c[0], c[1], c[2]);
  if (theme == MZ_THEME_ABYSS && c[0] > 0.9f && c[2] < 0.2f) kc = lin(1.0f, 0.95f, 0.78f), rk *= 1.5f;
  return (t3_color){ kc.r * rk, kc.g * rk, kc.b * rk };
}
static void kind_load(int k) {
  kindv *kv = &V.k[k];
  const char *file = cw_vfs_exists(MON_FILE[k]) ? MON_FILE[k] : "models/ghoul.cwchar";
  if (!mb_chr_load(&kv->c, file)) return;
  kv->nwalk = kv->c.nf[0];
  kv->natk = kv->c.nclip > 1 ? kv->c.nf[1] : 0;
  kv->r = kv->c.rad;
  /* the crowd's look is its modelled vertex colours (a dark body, the kind's one saturated accent, baked occlusion) under
   * the scene's key light (Lambert: thousands of bodies, cheap per pixel), and an edge in the kind's colour round its
   * silhouette: the back faces of a hull pushed out along the normals, unlit, drawn after the bodies (the depth test
   * leaves only the rim). Each body stands apart from the floor and from the bodies around it. The edge shares the
   * body's instance matrices and colours: one upload, no second pass over the crowd on the CPU. */
  t3_material *m = t3_mesh_lambert_material_new(0xffffff);
  m->vertex_colors = true;
  t3_material *em = t3_mesh_basic_material_new(0xffffff);
  em->side = T3_BACK_SIDE;
  em->color = edge_color(k, V.theme, g_rim_k);
  for (int f = 0; f < kv->nwalk + kv->natk && f < MB_MAXF; f++) {
    int cap = f < kv->nwalk ? MON_CAP[k] / CW_MAX(kv->nwalk, 1) * 3 / 2 + 32 : MON_CAP[k] / 3 + 32;
    kv->im[f] = imesh(MON_NAME[k], t3_retain(kv->c.geo[0][f]), t3_retain(m), cap);
    t3_geometry *hg = hull(kv->c.geo[0][f], 0.035f);
    if (!hg) continue;
    t3_instanced_mesh *e = imesh(MON_NAME[k], hg, t3_retain(em), 1);
    t3_release(e->instance_matrix), t3_release(e->instance_color);
    e->instance_matrix = t3_retain(kv->im[f]->instance_matrix), e->instance_color = t3_retain(kv->im[f]->instance_color);
    e->mesh.base.render_order = 1;
    kv->edge[f] = e;
  }
  kv->edge_mat = em; /* held by its meshes */
  t3_release(em), t3_release(m);
}

bool view_init_step(cw_node root, int *done_, int *total) {
  enum { S_ROOT, S_KINDS, S_HEROES = S_KINDS + EK_KINDS, S_PLAYERS = S_HEROES + CL_KINDS, S_SHOTS = S_PLAYERS + MAXP, S_ITEMS, S_SPLATS, S_DONE };
  static int step;
  *total = S_DONE;
  int s = step++;
  *done_ = step;
  if (s == S_ROOT) {
    memset(&V, 0, sizeof V);
    V.root = cw_node_new("Node3D", "View");
    cw_node_add_child(root, V.root);
    V.floor_built = -1;
  } else if (s >= S_KINDS && s < S_HEROES) {
    kind_load(s - S_KINDS);
  } else if (s >= S_HEROES && s < S_PLAYERS) {
    mb_chr_load(&V.hero[s - S_HEROES], HERO_FILE[s - S_HEROES]);
  } else if (s >= S_PLAYERS && s < S_SHOTS) {
    int i = s - S_PLAYERS;
    t3_object *g = t3_group_new();
    V.pl[i] = g;
    cw_node pn = cw_node_new("Node3D", "Player");
    cw_node_add_child(V.root, pn);
    cw_node3d_set_object(pn, g);
    V.pn[i] = pn;
    V.pl_cls[i] = -1;
    t3_material *rm = mat_emissive(PCOL[i][0], PCOL[i][1], PCOL[i][2], 1.5f); /* the team colour, not a white glare: the hero inside it shows */
    t3_geometry *ring = t3_ring_geometry_new(0.6f, 0.66f, 48, 1); /* thin: under the hero's scale it is a 1.1 m ring round a one-tile hero */
    t3_geometry_apply_matrix4(ring, &(t3_mat4){ { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1 } });
    t3_mesh *mr = t3_mesh_new(ring, rm);
    mr->base.position = (t3_vec3){ 0, 0.04f, 0 };
    rm->depth_test = false, rm->depth_write = false, rm->transparent = true; /* drawn over the crowd: a hero is found even when monsters stand on its ring */
    mr->base.render_order = 10;
    { /* the ring lies on the floor in a node of its own (the hero leans; the ring does not) */
      cw_node rn = cw_node_new("Node3D", "Ring");
      cw_node_add_child(V.root, rn);
      t3_object *rg = t3_group_new();
      t3_object_add(rg, mr);
      mr->base.scale = (t3_vec3){ 1.9f, 1.9f, 1.9f };
      cw_node3d_set_object(rn, rg);
      t3_release(rg);
      V.ring_node[i] = rn;
    }
    V.pl_ring[i] = mr;
    t3_release(mr), t3_release(ring), t3_release(rm);
    t3_release(g);
    cw_node l = cw_node_new("OmniLight3D", "PlayerLight");
    cw_node_add_child(V.root, l);
    cw_node_set(l, "color", cw_val_color(0.6f + 0.4f * PCOL[i][0], 0.6f + 0.4f * PCOL[i][1], 0.6f + 0.4f * PCOL[i][2], 1));
    cw_node_set(l, "energy", cw_val_float(0));
    cw_node_set(l, "range", cw_val_float(6.5f));
    cw_node_set(l, "decay", cw_val_float(2));
    cw_node_set(l, "fog_energy", cw_val_float(0));
    cw_node3d_set_position(l, (t3_vec3){ 0, -60, 0 });
    V.pl_light[i] = l;
  } else if (s == S_SHOTS) {
    static const char *const SF[SK_KINDS] = { "models/shot_axe.cwchar", "models/shot_hammer.cwchar", NULL, "models/shot_arrow.cwchar", NULL, NULL, NULL, NULL };
    for (int k = 0; k < SK_KINDS; k++) {
      if (SF[k] && model_meshes(SF[k], "Shots", &V.shot[k], NULL, 1, 160, NULL, 0.4f)) continue;
      static const float SC[SK_KINDS][3] = { { 0.8f, 0.8f, 0.85f }, { 0.75f, 0.75f, 0.8f }, { 1.0f, 0.55f, 0.2f }, { 0.9f, 0.85f, 0.6f }, { 1.0f, 0.4f, 0.1f }, { 0.4f, 0.32f, 0.28f }, { 0.75f, 0.35f, 1.0f }, { 1.0f, 0.5f, 0.15f } };
      static const float SK[SK_KINDS] = { 1, 1, 9, 2, 7, 0.6f, 7, 5 };
      t3_geometry *g = k == SK_ARROW ? t3_box_geometry_new(0.06f, 0.06f, 0.9f, 1, 1, 1) : t3_sphere_geometry_new(k == SK_FIRE ? 0.22f : k == SK_LOB ? 0.3f : 0.2f, 12, 8);
      V.shot[k] = imesh("Shots", geo_colored(g, 1, 1, 1), SK[k] > 1 ? mat_emissive(SC[k][0], SC[k][1], SC[k][2], SK[k]) : mat_std(0.5f, 0.3f), 160);
      if (SK[k] <= 1) V.shot[k]->mesh.materials[0]->color = lin(SC[k][0], SC[k][1], SC[k][2]);
    }
  } else if (s == S_ITEMS) {
    static const char *const IF[IT_KINDS] = { "models/key.cwchar", "models/potion.cwchar", "models/treasure.cwchar", "models/treasure.cwchar", "models/treasure.cwchar",
                                              "models/treasure.cwchar", "models/food.cwchar", "models/food.cwchar", "models/food.cwchar", "models/food.cwchar",
                                              "models/elixir.cwchar", NULL };
    for (int k = 0; k < IT_KINDS; k++) {
      if (!IF[k]) continue;
      bool dup = false; /* the kinds that share a model share its meshes */
      for (int j = 0; j < k && !dup; j++)
        if (IF[j] && !strcmp(IF[j], IF[k])) {
          dup = true;
          memcpy(V.item[k], V.item[j], sizeof V.item[k]), memcpy(V.item_glow[k], V.item_glow[j], sizeof V.item_glow[k]);
        }
      if (!dup) model_meshes(IF[k], "Items", V.item[k], V.item_glow[k], 4, 128, fb_box, 0.5f);
    }
    t3_instanced_mesh *cg[CH_KINDS] = { 0 };
    int nc = model_meshes("models/charm.cwchar", "Charms", V.charm, cg, CH_KINDS, 24, fb_orb, 0.4f);
    for (int k = nc; k < CH_KINDS; k++) V.charm[k] = V.charm[0];
    memcpy(V.charm_glowf, cg, sizeof cg);
    V.embers = imesh("Embers", geo_colored(t3_sphere_geometry_new(0.28f, 12, 8), 1, 1, 1), mat_emissive(1.0f, 0.5f, 0.15f, 8), MAXP * 2);
  } else if (s == S_SPLATS) {
    V.shadows = cw_splats_new(V.root, "Shadows", CW_SPLATS_SHADOW, MAXE + 256);
    V.pools = cw_splats_new(V.root, "LightPools", CW_SPLATS_LIGHT, 128);
    V.glows = cw_splats_new(V.root, "Glows", CW_SPLATS_LIGHT, MAXG + MAXSP + 64);
    { /* a unit column, open at both ends, bright at the floor and fading to nothing at its top */
      t3_geometry *g = t3_cylinder_geometry_new(1, 0.55f, 1, 16, 4, true);
      t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0.5f, 0, 1 } });
      t3_attribute *pa = g->attributes[T3_ATTR_POSITION];
      const float *pp = t3_attribute_f32(pa);
      float *c = malloc(sizeof(float) * 3 * (size_t)pa->count);
      for (int i = 0; i < pa->count; i++) { float k = 1.0f - pp[i * 3 + 1]; k = k * k; c[i * 3] = c[i * 3 + 1] = c[i * 3 + 2] = k; }
      t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, c, pa->count, 3));
      free(c);
      t3_material *m = t3_mesh_basic_material_new(0xffffff);
      m->vertex_colors = true, m->transparent = true, m->depth_write = false, m->blending = T3_ADDITIVE_BLENDING, m->side = T3_DOUBLE_SIDE;
      V.beam = imesh("Beams", g, m, 256);
      V.beam->mesh.base.render_order = 5;
    }
  }
  return step >= S_DONE;
}

void view_free(void) {
  for (int k = 0; k < EK_KINDS; k++) mb_chr_free(&V.k[k].c);
  for (int i = 0; i < MAXP; i++) if (V.pl_mat[i]) cw_rim_light_remove(V.pl_mat[i]), t3_release(V.pl_mat[i]);
  for (int c = 0; c < CL_KINDS; c++) mb_chr_free(&V.hero[c]);
  if (V.have_floor) mazebuild_free(&V.b);
  free(V.text);
  memset(&V, 0, sizeof V);
}

/* ── the floor ──────────────────────────────────────────────────────── */

static void floor_dynamic_free(void) {
  /* the floor's own instanced meshes (doors, spawners, stub, exits) live under the Level node: freed with it */
  V.stub = V.trapwall = NULL;
  memset(V.breakable, 0, sizeof V.breakable), memset(V.door, 0, sizeof V.door), memset(V.door_glow, 0, sizeof V.door_glow);
  memset(V.spawner, 0, sizeof V.spawner), memset(V.spawner_glow, 0, sizeof V.spawner_glow);
  V.exit_im = V.exit_glow = V.tele = V.tele_glow = V.trap = NULL;
  V.lock = V.lock_glow = NULL;
}

/* an instanced mesh under the floor's Level node (freed with the floor) */
static t3_instanced_mesh *fmesh(const char *name, t3_geometry *g, t3_material *m, int n) {
  cw_node keep = V.root;
  V.root = V.b.node;
  t3_instanced_mesh *im = imesh(name, g, m, n);
  V.root = keep;
  return im;
}
static int kit_meshes(const char *part, const char *name, t3_instanced_mesh **surf, t3_instanced_mesh **glow, int maxf, int cap) {
  char path[96];
  mazebuild_kit(V.theme, part, path, sizeof path);
  if (!cw_vfs_exists(path)) mazebuild_kit(MZ_THEME_CRYPT, part, path, sizeof path); /* a tileset whose kit is not made yet: the crypt's */
  mb_chr c;
  if (!mb_chr_load(&c, path)) return 0;
  int n = CW_MIN(c.total, maxf);
  for (int f = 0; f < n; f++) {
    surf[f] = fmesh(name, t3_retain(c.geo[0][f]), mat_std(0.8f, 0.15f), cap);
    if (glow) glow[f] = c.ngroup > 1 && c.geo[1][f] ? fmesh(name, t3_retain(c.geo[1][f]), mat_glow(), cap) : NULL;
  }
  mb_chr_free(&c);
  return n;
}

void view_floor(const sim *s) {
  if (s->floor == V.floor_built) return;
  if (V.have_floor) mazebuild_free(&V.b), floor_dynamic_free();
  V.have_floor = false, V.lit = false;
  V.m = s->m;
  V.theme = CW_CLAMP(V.m.theme, 0, MZ_THEMES - 1);
  free(V.text), V.text = NULL;
  { /* the floor's text: the bake's hash (a native bake) */
    char path[64];
    maze_path(V.m.floor, path, sizeof path);
    size_t len = 0;
    char *b = (char *)cw_vfs_read(path, &len);
    if (b) V.text = malloc(len + 1), memcpy(V.text, b, len), V.text[len] = 0, free(b);
  }
  if (!mazebuild_build(V.root, &V.m, &V.b)) return;
  V.have_floor = true;
  V.floor_built = s->floor;
  memset(V.cut, 0, sizeof V.cut);
  /* the floor's changing pieces: the cut-down stub, doors, breakable and trap walls, spawners, exits, pads */
  char path[96];
  mazebuild_kit(V.theme, "wall", path, sizeof path);
  if (!cw_vfs_exists(path)) mazebuild_kit(MZ_THEME_CRYPT, "wall", path, sizeof path);
  mb_chr wc;
  if (mb_chr_load(&wc, path)) {
    mb_darken_tops(&wc, 1.0f);
    t3_material *wm = t3_retain(V.b.wall_mat);
    V.stub = fmesh("WallStub", t3_retain(wc.geo[0][CW_MIN(3, wc.total - 1)]), t3_retain(wm), 256);
    V.trapwall = fmesh("TrapWall", t3_retain(wc.geo[0][0]), wm, 64);
    mb_chr_free(&wc);
  }
  kit_meshes("breakable", "Breakable", V.breakable, NULL, 2, 96);
  V.door_frames = kit_meshes("door", "Doors", V.door, V.door_glow, 8, 96);
  kit_meshes("spawner", "Spawners", V.spawner, V.spawner_glow, 3, MAXSP);
  {
    cw_node keep = V.root; /* under the floor's node, so they go with it */
    V.root = V.b.node;
    t3_instanced_mesh *eg[2] = { 0 };
    if (!model_meshes("models/exit.cwchar", "Exit", &V.exit_im, eg, 1, MZ_MAXEXIT, fb_plate, 0.8f)) V.exit_im = NULL;
    V.exit_glow = eg[0];
    eg[0] = NULL;
    model_meshes("models/teleporter.cwchar", "Teleporters", &V.tele, eg, 1, MZ_MAXTELE, fb_plate, 0.5f), V.tele_glow = eg[0];
    model_meshes("models/trap.cwchar", "Traps", &V.trap, NULL, 1, MZ_MAXTRAP, fb_plate, 0.7f);
    eg[0] = NULL;
    if (!model_meshes("models/key.cwchar", "DoorLocks", &V.lock, eg, 1, 96, fb_plate, 0.3f)) V.lock = NULL;
    V.lock_glow = eg[0];
    V.root = keep;
  }
  cw_info("crawler: floor %d (%s, %d x %d tiles) built", V.m.floor, MZ_THEME_ID[V.theme], V.m.w, V.m.h);
}

bool view_lightmap(cw_tree *t, const char *bake_out) {
  if (!V.have_floor || V.lit) return V.lit;
  V.lit = true;
  bool ok = mazebuild_lightmap(t, &V.b, bake_out, V.text);
  for (int v = 0; v < MB_VARIANTS; v++)
    for (int i = 0; i < V.b.nwall[v]; i++) {
      int x = V.b.wx[v][i], y = V.b.wy[v][i];
      float sx = 0, sz = 0;
      static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
      for (int d = 0; d < 4; d++)
        if (mz_in(&V.m, x + DX[d], y + DY[d]) && V.m.t[y + DY[d]][x + DX[d]] != T_WALL && V.m.t[y + DY[d]][x + DX[d]] != T_VOID) sx += (float)DX[d], sz += (float)DY[d];
      V.wall_light[v][i] = mazebuild_light_at(&V.b, mz_wx(&V.m, x) + sx * 1.3f, mz_wz(&V.m, y) + sz * 1.3f);
    }
  return ok;
}
t3_color view_light_at(float x, float z) {
  t3_color c = V.have_floor ? mazebuild_light_at(&V.b, x, z) : (t3_color){ 1, 1, 1 };
  float m = CW_MAX(c.r, CW_MAX(c.g, c.b));
  const float LO = 0.5f; /* a floor under the light: never so dark that a creature or a hero is lost */
  if (m < LO) c = m > 1e-3f ? (t3_color){ c.r * LO / m, c.g * LO / m, c.b * LO / m } : (t3_color){ LO, LO, LO };
  return c;
}

/* ── the camera: one fixed zoom over the team's middle ────────────────── */

/* straight down from right over the team's middle, through a long lens (close to orthographic: walls do not lean or
 * splay toward the edges): 12 tiles of floor top to bottom, 21.3 across at 16:9, the classic overhead screen's scale
 * (crawler-reference); north is up */
#define CAM_FOV 14.0f
static float cam_dist(void) { return g_cam_dist > 0 ? g_cam_dist : 97.5f; }

void view_camera(const sim *s, cw_node camera, float aspect) {
  float cx, cy, hw, hh;
  sim_team_box(s, &cx, &cy, &hw, &hh);
  if (!sim_alive_players(s)) cx = V.cx, cy = V.cy;
  /* keep the view over the maze where the maze is bigger than it */
  float mw = (float)s->m.w, mh = (float)s->m.h;
  if (mw > VIEW_HW) cx = CW_CLAMP(cx, -mw + VIEW_HW - 1.0f, mw - VIEW_HW + 1.0f);
  if (mh > VIEW_HH) cy = CW_CLAMP(cy, -mh + VIEW_HH - 1.5f, mh - VIEW_HH + 1.5f);
  if (!V.cam_init || s->floor_ticks < 2) V.cx = cx, V.cy = cy, V.cam_init = true;
  V.cx += (cx - V.cx) * 0.12f;
  V.cy += (cy - V.cy) * 0.12f;
  float d = cam_dist();
  t3_vec3 at = { V.cx, 0, V.cy };
  float tl = g_tilt * 3.14159265f / 180.0f;
  t3_vec3 eye = { at.x, cw_cos(tl) * d, at.z + cw_sin(tl) * d }; /* g_tilt from straight down, from the south */
  cw_node3d_set_position(camera, eye);
  cw_node_set(camera, "fov", cw_val_float(CAM_FOV));
  cw_node_set(camera, "near", cw_val_float(d * 0.62f)); /* 60 m at the game's 97.5 */
  cw_node_set(camera, "far", cw_val_float(d * 1.44f));
  cw_node3d_look_at(camera, at, g_tilt > 0.5f ? (t3_vec3){ 0, 1, 0 } : (t3_vec3){ 0, 0, -1 });
  V.eye = eye, V.at = at, V.aspect = aspect;
}

bool view_screen(t3_vec3 p, float *u, float *v) {
  if (V.aspect <= 0) return false;
  t3_vec3 f = { V.at.x - V.eye.x, V.at.y - V.eye.y, V.at.z - V.eye.z };
  float fl = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
  if (fl < 1e-4f) return false;
  f.x /= fl, f.y /= fl, f.z /= fl;
  const t3_vec3 r = { 1, 0, 0 }; /* the camera never turns: right is always east */
  t3_vec3 up = { r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x }; /* north, tilted */
  t3_vec3 d = { p.x - V.eye.x, p.y - V.eye.y, p.z - V.eye.z };
  float z = d.x * f.x + d.y * f.y + d.z * f.z;
  if (z <= 0.01f) return false;
  float th = tanf(CAM_FOV * 3.14159265f / 360.0f);
  float x = (d.x * r.x + d.y * r.y + d.z * r.z) / (z * th * V.aspect), y = (d.x * up.x + d.y * up.y + d.z * up.z) / (z * th);
  *u = 0.5f + 0.5f * x, *v = 0.5f - 0.5f * y;
  return true;
}
/* is a point (with a radius) on screen, with a margin */
static bool on_screen(float x, float y, float z, float r) {
  float u, v;
  if (!view_screen((t3_vec3){ x, y, z }, &u, &v)) return false;
  float mu = r / (2 * VIEW_HW), mv = r / (2 * VIEW_HH);
  return u > -0.08f - mu && u < 1.08f + mu && v > -0.12f - mv && v < 1.15f + mv;
}

view_stats view_get_stats(void) { return V.st; }

/* a column of light at (x, z): its height, radius and colour (linear, HDR) */
static void beam(float x, float z, float h, float r, t3_color c) {
  if (!V.beam || V.nbeam >= V.beam->instance_matrix->count) return;
  put_s(V.beam, V.nbeam, x, 0.02f, z, 0, r, h, r);
  t3_instanced_mesh_set_color_at(V.beam, V.nbeam++, c);
}
static void shadow_at(float x, float z, float r, float strength) {
  cw_splats_add(V.shadows, (t3_vec3){ x, 0, z }, 2.3f * r, 2.0f * r, 0, 0, (t3_color){ 1, 1, 1 }, strength);
}

/* ── per frame ──────────────────────────────────────────────────────── */

static const float KIND_GLOW[EK_KINDS][3] = { { 0.6f, 0.8f, 1.0f }, { 0.9f, 0.85f, 0.6f }, { 1.0f, 0.45f, 0.15f }, { 1.0f, 0.55f, 0.2f }, { 0.75f, 0.35f, 1.0f }, { 0.5f, 0.7f, 1.0f },
                                              { 0.9f, 0.8f, 0.3f }, { 0.9f, 0.3f, 0.2f }, { 1.0f, 0.35f, 0.1f }, { 0.5f, 1.0f, 0.3f }, { 0.4f, 1.0f, 0.8f }, { 0.3f, 1.0f, 0.6f } };

static void sync_walls(const sim *s) {
  if (!V.have_floor) return;
  /* which wall tiles are cut down: on the camera's side of a living player (south of it, a tile to each side) */
  static uint8_t want[MZ_MAX][MZ_MAX];
  memset(want, 0, sizeof want);
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (!p->active || p->down) continue;
    int tx = mz_tx(&V.m, p->x), ty = mz_ty(&V.m, p->y);
    for (int dy = 1; dy <= 2; dy++)
      for (int dx = -1; dx <= 1; dx++)
        if (0 && mz_in(&V.m, tx + dx, ty + dy) && (dy == 1 || dx == 0)) want[ty + dy][tx + dx] = 1; /* (no cutaway: the camera looks straight down over low walls) */
  }
  for (int y = 0; y < V.m.h; y++)
    for (int x = 0; x < V.m.w; x++) {
      float c = V.cut[y][x], t = want[y][x] ? 1.0f : 0.0f;
      V.cut[y][x] = c + CW_CLAMP(t - c, -0.07f, 0.11f); /* down in 0.15 s, up again in 0.25 s */
    }
  int nstub = 0, ndrawn = 0, ncut = 0;
  for (int v = 0; v < MB_VARIANTS; v++) {
    t3_instanced_mesh *im = V.b.wall[v];
    if (!im) continue;
    int n = 0;
    for (int i = 0; i < V.b.nwall[v]; i++) {
      int x = V.b.wx[v][i], y = V.b.wy[v][i];
      float wx = mz_wx(&V.m, x), wz = mz_wz(&V.m, y);
      if (!on_screen(wx, 1.3f, wz, 2.2f)) continue;
      float c = V.cut[y][x], yaw = (float)V.b.wr[v][i] * 1.5707963f;
      if (c > 0.97f && V.stub && nstub < 256) { /* down: its stub */
        put(V.stub, nstub, wx, 0, wz, yaw, 1);
        t3_instanced_mesh_set_color_at(V.stub, nstub++, V.wall_light[v][i]);
        ncut++;
        continue;
      }
      put_s(im, n, wx, 0, wz, yaw, 1, V.b.wall_sy, 1);
      t3_instanced_mesh_set_color_at(im, n, V.wall_light[v][i]);
      n++;
    }
    done(im, n);
    ndrawn += n;
  }
  done(V.stub, nstub);
  V.st.walls_drawn = ndrawn + nstub, V.st.cut = ncut;
  /* the walls that change: trap walls (until they vanish), breakable walls (then their rubble), doors (opening) */
  int nt = 0, nb[2] = { 0, 0 }, nd[8] = { 0 }, nl = 0;
  for (int y = 0; y < V.m.h; y++)
    for (int x = 0; x < V.m.w; x++) {
      int t0 = V.m.t[y][x];
      if (t0 != T_DOOR && t0 != T_BREAK && t0 != T_TRAPWALL) continue;
      float wx = mz_wx(&V.m, x), wz = mz_wz(&V.m, y);
      if (!on_screen(wx, 1.3f, wz, 2.2f)) continue;
      t3_color light = view_light_at(wx, wz + 1.2f);
      float cut = V.cut[y][x];
      if (t0 == T_TRAPWALL) {
        if (s->t[y][x] == T_TRAPWALL && V.trapwall && nt < 64) {
          put_s(V.trapwall, nt, wx, 0, wz, 0, 1, V.b.wall_sy, 1);
          t3_instanced_mesh_set_color_at(V.trapwall, nt++, (t3_color){ light.r * 0.92f, light.g * 0.92f, light.b * 0.95f });
        }
      } else if (t0 == T_BREAK) {
        int f = s->t[y][x] == T_BREAK ? 0 : 1;
        t3_instanced_mesh *im = V.breakable[f] ? V.breakable[f] : V.breakable[0];
        if (!im || nb[f] >= 96) continue;
        float hurt = s->wall_hp[y][x] == 1 ? 0.7f : 1.0f;
        put_s(im, nb[f], wx, 0, wz, (float)(hash2((uint32_t)x, (uint32_t)y) & 3) * 1.5707963f, 1, f ? 1 : V.b.wall_sy, 1);
        t3_instanced_mesh_set_color_at(im, nb[f]++, (t3_color){ light.r * hurt, light.g * hurt, light.b * hurt });
      } else if (V.door_frames > 0) {
        /* a door spans its tile across the corridor: along x when walls (or doors) stand east and west of it */
        bool ew = (x > 0 && V.m.t[y][x - 1] != T_FLOOR) || (x < V.m.w - 1 && V.m.t[y][x + 1] != T_FLOOR);
        bool ns = (y > 0 && V.m.t[y - 1][x] != T_FLOOR) && (y < V.m.h - 1 && V.m.t[y + 1][x] != T_FLOOR);
        float yaw = ew && !ns ? 0 : 1.5707963f;
        int o = s->door_open[y][x];
        int f = o == 0 ? 0 : CW_MIN(1 + o / 6, V.door_frames - 1);
        if (nd[f] >= 96) continue;
        /* shut, the gate fills its tile (a slab as thick as a wall, not a rail); opening, it sinks as modelled */
        put_s(V.door[f], nd[f], wx, 0, wz, yaw, 1, V.b.wall_sy, o == 0 ? DOOR_SHUT_DEPTH : 1);
        t3_instanced_mesh_set_color_at(V.door[f], nd[f], light);
        if (V.door_glow[f]) {
          put_s(V.door_glow[f], nd[f], wx, 0, wz, yaw, 1, V.b.wall_sy, o == 0 ? DOOR_SHUT_DEPTH : 1);
          float pulse = 0.8f + 0.2f * cw_sin((float)s->tick * 0.08f + (float)x);
          t3_instanced_mesh_set_color_at(V.door_glow[f], nd[f], (t3_color){ 1.4f * pulse, 1.1f * pulse, 0.5f * pulse });
        }
        nd[f]++;
        if (o == 0 && V.lock && nl < 96) { /* the key sign on its top, and warm light on the floor either side: it wants a key */
          float pulse = 0.75f + 0.25f * cw_sin((float)s->tick * 0.1f + (float)x);
          put(V.lock, nl, wx, 1.24f * V.b.wall_sy + 0.02f, wz, yaw + 0.6f, 2.3f);
          t3_instanced_mesh_set_color_at(V.lock, nl, (t3_color){ 1.2f, 1.0f, 0.6f });
          if (V.lock_glow) put(V.lock_glow, nl, wx, 1.24f * V.b.wall_sy + 0.02f, wz, yaw + 0.6f, 2.3f), t3_instanced_mesh_set_color_at(V.lock_glow, nl, (t3_color){ 3.2f * pulse, 2.4f * pulse, 0.8f * pulse });
          nl++;
          float ox = yaw == 0 ? 0 : 1.6f, oz = yaw == 0 ? 1.6f : 0;
          cw_splats_add(V.glows, (t3_vec3){ wx + ox, 0, wz + oz }, 2.4f, 2.4f, 0, 0, (t3_color){ 1.0f, 0.7f, 0.25f }, 1.3f * pulse);
          cw_splats_add(V.glows, (t3_vec3){ wx - ox, 0, wz - oz }, 2.4f, 2.4f, 0, 0, (t3_color){ 1.0f, 0.7f, 0.25f }, 1.3f * pulse);
        }
      }
    }
  done(V.lock, nl), done(V.lock_glow, nl);
  done(V.trapwall, nt), done(V.breakable[0], nb[0]);
  if (V.breakable[1]) done(V.breakable[1], nb[1]);
  for (int f = 0; f < V.door_frames; f++) done(V.door[f], nd[f]), done(V.door_glow[f], nd[f]);
}

static void sync_spawners(const sim *s) {
  int n[3] = { 0, 0, 0 };
  for (int k = 0; k < s->nsp; k++) {
    const spawner *sp = &s->sp[k];
    if (!sp->alive || !on_screen(sp->x, 1, sp->y, 2)) continue;
    int f = CW_CLAMP(3 - sp->str, 0, 2);
    if (!V.spawner[f]) f = 0;
    if (!V.spawner[f] || n[f] >= MAXSP) continue;
    float yaw = (float)(k * 37 % 4) * 1.5707963f, swell = 1.25f + 0.05f * (float)sp->pulse / 12.0f;
    put_up(V.spawner[f], n[f], sp->x, 0, sp->y, yaw, swell);
    t3_instanced_mesh_set_color_at(V.spawner[f], n[f], view_light_at(sp->x, sp->y + 1.2f));
    const float *kg = KIND_GLOW[sp->kind];
    float pulse = 1.5f + 2.5f * (float)sp->pulse / 12.0f + 0.4f * cw_sin((float)s->tick * 0.1f + (float)k);
    if (V.spawner_glow[f]) {
      put_up(V.spawner_glow[f], n[f], sp->x, 0, sp->y, yaw, swell);
      t3_instanced_mesh_set_color_at(V.spawner_glow[f], n[f], (t3_color){ kg[0] * pulse, kg[1] * pulse, kg[2] * pulse });
    }
    n[f]++;
    cw_splats_add(V.glows, (t3_vec3){ sp->x, 0, sp->y }, 3.6f, 3.6f, 0, 0, lin(kg[0], kg[1], kg[2]), 0.9f + 0.6f * (float)sp->pulse / 12.0f);
    { float k = 0.22f + 0.25f * (float)sp->pulse / 12.0f; beam(sp->x, sp->y, 3.2f, 0.75f, (t3_color){ kg[0] * k, kg[1] * k, kg[2] * k }); }
    shadow_at(sp->x, sp->y, 1.0f, 0.5f);
  }
  for (int f = 0; f < 3; f++) done(V.spawner[f], n[f]), done(V.spawner_glow[f], n[f]);
}

static void sync_features(const sim *s, float time) {
  int ne = 0;
  for (int k = 0; k < s->m.nexit && V.exit_im; k++) {
    float x = mz_wx(&s->m, s->m.exit[k][0]), z = mz_wz(&s->m, s->m.exit[k][1]);
    int skip = s->m.exit[k][2];
    put(V.exit_im, ne, x, 0.0f, z, 0, 1);
    t3_instanced_mesh_set_color_at(V.exit_im, ne, view_light_at(x, z));
    if (V.exit_glow) {
      put(V.exit_glow, ne, x, 0.0f, z, 0, 1);
      float p = 2.6f + 0.9f * cw_sin(time * 2.5f + (float)k);
      t3_instanced_mesh_set_color_at(V.exit_glow, ne, skip > 1 ? (t3_color){ 0.4f * p, 0.9f * p, 1.6f * p } : (t3_color){ 1.6f * p, 1.1f * p, 0.5f * p });
    }
    ne++;
    { /* a wide pool of light that breathes round the hatch: the way down reads from across the screen */
      float br = 0.5f + 0.5f * cw_sin(time * 2.5f + (float)k);
      t3_color ec = skip > 1 ? (t3_color){ 0.3f, 0.6f, 1.0f } : (t3_color){ 1.0f, 0.75f, 0.35f };
      cw_splats_add(V.glows, (t3_vec3){ x, 0, z }, 3.4f, 3.4f, 0, 0, ec, 2.6f);
      cw_splats_add(V.glows, (t3_vec3){ x, 0, z }, 4.2f + 1.4f * br, 4.2f + 1.4f * br, 0, 0, ec, 1.2f + 0.8f * br);
    }
    { float p = 0.45f + 0.12f * cw_sin(time * 2.5f + (float)k); beam(x, z, 5.5f, 0.95f, skip > 1 ? (t3_color){ 0.25f * p, 0.55f * p, 1.0f * p } : (t3_color){ 1.0f * p, 0.7f * p, 0.3f * p }); }
  }
  done(V.exit_im, ne), done(V.exit_glow, ne);
  int nt = 0;
  for (int k = 0; k < s->m.ntele && V.tele; k++) {
    float x = mz_wx(&s->m, s->m.tele[k][0]), z = mz_wz(&s->m, s->m.tele[k][1]);
    put(V.tele, nt, x, 0, z, time * 0.6f, 1);
    if (V.tele_glow) put(V.tele_glow, nt, x, 0, z, time * 0.6f, 1), t3_instanced_mesh_set_color_at(V.tele_glow, nt, (t3_color){ 0.5f, 1.4f, 2.0f });
    nt++;
    cw_splats_add(V.glows, (t3_vec3){ x, 0, z }, 3.0f, 3.0f, 0, 0, (t3_color){ 0.3f, 0.8f, 1.0f }, 1.2f + 0.4f * cw_sin(time * 3.0f + (float)k));
  }
  done(V.tele, nt), done(V.tele_glow, nt);
  int np = 0;
  for (int k = 0; k < s->m.ntrap && V.trap; k++) {
    float x = mz_wx(&s->m, s->m.trap[k][0]), z = mz_wz(&s->m, s->m.trap[k][1]);
    put_s(V.trap, np, x, 0, z, 0, 1, s->m.trap[k][2] ? 1 : 0.4f, 1);
    t3_instanced_mesh_set_color_at(V.trap, np++, view_light_at(x, z));
  }
  done(V.trap, np);
}

static void sync_monsters(const sim *s, float time) {
  static float rk = -1; /* the edge follows its debug field (tuning runs) and the tileset */
  static int rt = -1;
  if (rk != g_rim_k || rt != V.theme) {
    rk = g_rim_k, rt = V.theme;
    for (int k = 0; k < EK_KINDS; k++)
      if (V.k[k].edge_mat) V.k[k].edge_mat->color = edge_color(k, V.theme, rk);
  }
  static int cnt[EK_KINDS][MB_MAXF];
  memset(cnt, 0, sizeof cnt);
  int drawn = 0;
  static const float RATE[EK_KINDS] = { 1.2f, 1.5f, 1.8f, 1.0f, 1.2f, 0.7f, 2.4f, 0.9f, 2.4f, 1.0f, 2.6f, 1.4f };
  static const float SIZE[EK_KINDS] = { 1.5f, 1.5f, 1.4f, 1.4f, 1.6f, 1.6f, 1.7f, 1.3f, 1.4f, 1.4f, 1.5f, 1.5f }; /* toward a tile each, as the classic: they read from straight above */
  for (int i = 0; i < MAXE; i++) {
    const enemy *e = &s->e[i];
    if (!e->alive) continue;
    kindv *kv = &V.k[e->kind];
    if (kv->nwalk <= 0 || !on_screen(e->x, 1, e->y, kv->r)) continue;
    float ph = time * RATE[e->kind] * (0.85f + 0.3f * h01((uint32_t)i, 3)) + h01((uint32_t)i, 4) * 8.0f;
    int fr = (int)((ph - floorf(ph)) * (float)kv->nwalk);
    float tell = 0;
    int af = sim_attack_frame(e, &tell);
    if (af >= 0 && kv->natk > 0) fr = kv->nwalk + CW_MIN(af, kv->natk - 1);
    if (e->frozen > 0) fr = 0;
    t3_instanced_mesh *im = kv->im[fr];
    if (!im || cnt[e->kind][fr] >= im->instance_matrix->count) continue;
    int n = cnt[e->kind][fr]++;
    float sc = SIZE[e->kind] * (0.82f + 0.13f * (float)e->str) * (e->flash > 0 ? 1.08f : 1.0f);
    float yaw = -e->face + 1.5708f;
    float y = e->kind == EK_DREAD || e->kind == EK_SHADE ? 0.05f + 0.06f * cw_sin(time * 2.2f + (float)i) : 0;
    put_up(im, n, e->x, y, e->y, yaw, sc);
    /* the body as modelled (a dark body, the kind's accent) under the light where it stands; strength in the value: a
     * lighter first, the kind's own at the second, a deep warm third */
    static const float STR[4][3] = { { 1, 1, 1 }, { 1.2f, 1.2f, 1.2f }, { 1, 1, 1 }, { 1.0f, 0.72f, 0.62f } };
    const float *st = STR[CW_CLAMP(e->str, 1, 3)];
    t3_color gl = view_light_at(e->x, e->y);
    float lm = CW_MAX(gl.r, CW_MAX(gl.g, gl.b)), lk = lm > 1.4f ? 1.4f / lm : 1.0f; /* a torch tints, never burns out */
    t3_color c = { st[0] * gl.r * lk, st[1] * gl.g * lk, st[2] * gl.b * lk };
    if (e->flash > 0) c = (t3_color){ 4, 3, 2.4f };
    else if (e->frozen > 0) c = (t3_color){ 1.3f, 1.7f, 2.3f };
    else if (e->slow > 0) c.r *= 0.55f, c.g *= 0.8f, c.b *= 1.5f;
    if (af == 0 && e->flash <= 0) { float t = tell * tell; c.r *= 1.0f + 1.2f * t, c.g *= 1.0f + 0.4f * t; }
    if (e->kind == EK_HEXER && sim_hexer_hidden(s, e)) c = (t3_color){ 0.04f, 0.03f, 0.06f };
    if (e->kind == EK_DREAD) c = (t3_color){ 0.45f + 0.15f * gl.r, 0.55f + 0.15f * gl.g, 0.9f + 0.15f * gl.b };
    if (e->kind == EK_SHADE && e->flash <= 0) { /* a shade glows faintly of its own: pale, cold, never lost in the dark */
      float p = 0.85f + 0.15f * cw_sin(time * 3.0f + (float)i);
      c = (t3_color){ (0.7f + 0.15f * gl.r) * p, (0.85f + 0.15f * gl.g) * p, (1.15f + 0.15f * gl.b) * p };
    }
    t3_instanced_mesh_set_color_at(im, n, c);
    shadow_at(e->x, e->y, 0.34f * sc * (e->kind == EK_OGRE ? 1.6f : 1.0f), 0.9f); /* a dark contact shadow: bodies stand apart */
    drawn++;
  }
  for (int k = 0; k < EK_KINDS; k++)
    for (int f = 0; f < V.k[k].nwalk + V.k[k].natk && f < MB_MAXF; f++) {
      done(V.k[k].im[f], cnt[k][f]);
      if (V.k[k].edge[f]) V.k[k].edge[f]->count = cnt[k][f]; /* its matrices are the body's */
    }
  V.st.enemies_drawn = drawn;
}

static void hero_build(int i, int cls) {
  /* the player's meshes for its class: every frame of every clip, shown one at a time */
  for (int f = 0; f < MB_MAXF; f++)
    for (int g = 0; g < 4; g++)
      if (V.pm[i][f][g]) t3_object_remove(V.pl[i], &V.pm[i][f][g]->base), V.pm[i][f][g] = NULL;
  if (V.pl_mat[i]) cw_rim_light_remove(V.pl_mat[i]), t3_release(V.pl_mat[i]), V.pl_mat[i] = NULL; /* the rim holds its material */
  if (V.pl_trim[i]) t3_release(V.pl_trim[i]), V.pl_trim[i] = NULL;
  const mb_chr *c = &V.hero[cls];
  V.pl_cls[i] = cls;
  if (!c->total) return;
  static const float CC[CL_KINDS][3] = { { 1.0f, 0.25f, 0.18f }, { 0.25f, 0.5f, 1.0f }, { 0.75f, 0.3f, 1.0f }, { 0.3f, 1.0f, 0.3f } };
  t3_material *bm = mat_std(0.5f, 0.3f);
  { /* a rim in the class colour: the hero's edge burns against the floor and the crowd */
    cw_rim_light_desc rd = cw_rim_light_default();
    rd.color = lin(CC[cls][0], CC[cls][1], CC[cls][2]);
    rd.strength = 2.4f, rd.power = 2.2f, rd.near = 0, rd.far = 1, rd.close = 1;
    cw_rim_light_apply(bm, &rd);
  }
  t3_material *om = t3_mesh_basic_material_new(0x000000); /* the outline: near black, back faces of the pushed-out hull */
  om->color = (t3_color){ 0.01f, 0.008f, 0.012f };
  om->side = T3_BACK_SIDE;
  t3_material *tm = mat_glow();
  tm->color = (t3_color){ 1.2f + 4.5f * PCOL[i][0], 1.2f + 4.5f * PCOL[i][1], 1.2f + 4.5f * PCOL[i][2] }; /* the team trim burns bright: the hero reads in a crowd */
  for (int f = 0; f < c->total; f++) {
    t3_mesh *mb = t3_mesh_new(c->geo[0][f], bm);
    mb->base.visible = f == 0;
    t3_object_add(V.pl[i], mb);
    V.pm[i][f][0] = mb;
    t3_release(mb);
    t3_geometry *hg = hull(c->geo[0][f], 0.035f);
    if (hg) {
      t3_mesh *mo = t3_mesh_new(hg, om);
      mo->base.visible = f == 0;
      t3_object_add(V.pl[i], mo);
      V.pm[i][f][3] = mo;
      t3_release(mo), t3_release(hg);
    }
    if (c->ngroup > 1 && c->geo[1][f]) {
      t3_mesh *mt = t3_mesh_new(c->geo[1][f], tm);
      mt->base.visible = f == 0;
      t3_object_add(V.pl[i], mt);
      V.pm[i][f][1] = mt;
      t3_release(mt);
    }
  }
  t3_release(om);
  V.pl_mat[i] = bm, V.pl_trim[i] = tm;
  V.pl_frame[i] = 0;
}

static void sync_players(const sim *s, float time) {
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (!p->active) {
      cw_node3d_set_position(V.pn[i], (t3_vec3){ 0, -100, 0 });
      cw_node_set(V.pl_light[i], "energy", cw_val_float(0));
      if (V.pl_ring[i]) V.pl_ring[i]->base.visible = false; /* an empty seat has no ring on the floor */
      continue;
    }
    if (V.pl_cls[i] != p->cls) hero_build(i, p->cls);
    const mb_chr *c = &V.hero[p->cls];
    cw_node3d_set_position(V.pl_light[i], (t3_vec3){ p->x, 2.2f, p->y });
    cw_node_set(V.pl_light[i], "energy", cw_val_float(p->down ? 0 : 6.0f));
    if (V.pl_mat[i]) {
      t3_color gl = view_light_at(p->x, p->y);
      float hurt = p->hurt_t > 0 ? 1.0f + 0.25f * (float)p->hurt_t : 1.0f;
      float lm = CW_MAX(gl.r, CW_MAX(gl.g, gl.b)), lk = lm > 1.3f ? 1.3f / lm : 1.0f; /* the model's own colours (crimson Brute, blue Warden, violet Mage, green Ranger) under the light */
      V.pl_mat[i]->color = (t3_color){ gl.r * lk * hurt, gl.g * lk, gl.b * lk };
    }
    if (!p->down) shadow_at(p->x, p->y, 0.48f, 0.7f);
    float giant = p->charm[CH_GIANT] > 0 ? 1.5f : 1.0f;
    float blink = p->inv > 0 && p->stun <= 0 && (s->tick / 4) % 2 ? 0.0f : 1.0f; /* fresh back in: a flicker */
    cw_node3d_set_position(V.pn[i], p->down ? (t3_vec3){ p->x, -0.5f, p->y } : pull(p->x, 0, p->y, PULL_HERO));
    if (p->down) cw_node3d_set_rotation(V.pn[i], (t3_vec3){ 1.4f, -p->face + 1.5708f, 0 });
    else cw_node3d_set_quaternion(V.pn[i], lean_q(-p->face + 1.5708f));
    if (cw_node_valid(V.ring_node[i])) cw_node3d_set_position(V.ring_node[i], (t3_vec3){ p->x, 0, p->y }); /* the ring stays flat on the floor */
    cw_node3d_set_scale(V.pn[i], (t3_vec3){ 1.9f * giant, 1.9f * giant, 1.9f * giant }); /* a hero about a tile across, as the classic: it reads from straight above */
    if (V.pl_ring[i]) V.pl_ring[i]->base.visible = !p->down && !g_hide_rings;
    if (!c->total) continue;
    float speed = sqrtf(p->vx * p->vx + p->vy * p->vy);
    int clip = 1, fr;
    const class_stats *cs = &CLASS[p->cls];
    if (p->firing > 0 && c->nclip > 2) { /* the throw, the cast, the draw: by where the shot's cooldown is */
      clip = 2;
      float t = 1.0f - (float)p->cd / (float)CW_MAX(cs->shot_cd, 1);
      fr = c->first[2] + CW_CLAMP((int)(t * (float)c->nf[2]), 0, c->nf[2] - 1);
    } else if (p->melee_cd > cs->melee_cd / 3 && c->nclip > 3) {
      clip = 3;
      float t = 1.0f - (float)p->melee_cd / (float)CW_MAX(cs->melee_cd, 1);
      fr = c->first[3] + CW_CLAMP((int)(t * 1.6f * (float)c->nf[3]), 0, c->nf[3] - 1);
    } else if (speed > 0.6f && !p->down) {
      clip = 0;
      float ph = time * CW_MIN(speed / cs->speed, 1.3f) * 1.8f + (float)i * 0.31f;
      fr = c->first[0] + (int)((ph - floorf(ph)) * (float)c->nf[0]);
    } else {
      float ph = time * 0.5f + (float)i * 0.31f;
      fr = c->first[clip] + (int)((ph - floorf(ph)) * (float)CW_MAX(c->nf[clip], 1));
    }
    fr = CW_CLAMP(fr, 0, c->total - 1);
    for (int g = 0; g < 4; g++) {
      if (V.pm[i][V.pl_frame[i]][g]) V.pm[i][V.pl_frame[i]][g]->base.visible = false;
      if (V.pm[i][fr][g]) V.pm[i][fr][g]->base.visible = blink > 0;
    }
    V.pl_frame[i] = fr;
  }
}

static void sync_shots(const sim *s, float time) {
  int n[SK_KINDS] = { 0 };
  for (int j = 0; j < MAXB; j++) {
    const bullet *b = &s->b[j];
    if (!b->alive || !on_screen(b->x, 1, b->y, 1)) continue;
    t3_instanced_mesh *im = V.shot[b->kind];
    if (!im || n[b->kind] >= im->instance_matrix->count) continue;
    float yaw = -cw_atan2(b->vy, b->vx) + 1.5708f;
    float y = 1.0f;
    if (b->kind == SK_LOB || b->kind == SK_FIREBOMB) { /* in its arc */
      float total = b->kind == SK_LOB ? 45.0f : 36.0f, t = 1.0f - (float)b->life / total;
      y = 0.9f + 4.0f * t * (1 - t) * (b->kind == SK_LOB ? 1.6f : 1.2f);
    }
    if (b->kind == SK_AXE || b->kind == SK_HAMMER) { /* spinning end over end */
      t3_quat qy = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
      float a = (float)b->age * 0.55f;
      t3_quat qx = { cw_sin(a * 0.5f), 0, 0, cw_cos(a * 0.5f) };
      t3_vec3 q = pull(b->x, y, b->y, PULL_SHOT);
      put_q(im, n[b->kind], q.x, q.y, q.z, t3_quat_multiply(qy, qx), 1.0f);
    } else {
      t3_vec3 q = pull(b->x, y, b->y, PULL_SHOT);
      put(im, n[b->kind], q.x, q.y, q.z, yaw, b->kind == SK_FIRE ? 1.0f + 0.15f * cw_sin(time * 30.0f + (float)j) : 1.0f);
    }
    t3_instanced_mesh_set_color_at(im, n[b->kind], (t3_color){ 1, 1, 1 });
    n[b->kind]++;
    if (b->kind != SK_ARROW && b->kind != SK_AXE && b->kind != SK_HAMMER) shadow_at(b->x, b->y, 0.25f, 0.35f);
  }
  int total = 0;
  for (int k = 0; k < SK_KINDS; k++) done(V.shot[k], n[k]), total += n[k];
  V.st.shots = total;
  int ne = 0;
  for (int i = 0; i < MAXP && V.embers; i++) {
    const player *p = &s->p[i];
    if (!p->active || p->down || p->charm[CH_EMBERS] <= 0) continue;
    for (int k = 0; k < 2; k++) {
      float a = p->ember_a + (float)k * 3.1416f;
      put(V.embers, ne++, p->x + cw_cos(a) * 1.8f, 1.0f, p->y + cw_sin(a) * 1.8f, 0, 1.0f + 0.2f * cw_sin(time * 20 + (float)k));
    }
  }
  done(V.embers, ne);
}

static const float ITEM_GLOW[IT_KINDS][3] = { { 1.0f, 0.85f, 0.3f }, { 0.35f, 0.6f, 1.0f }, { 1.0f, 0.8f, 0.3f }, { 1.0f, 0.75f, 0.35f }, { 1.0f, 0.75f, 0.3f }, { 1.0f, 0.75f, 0.3f },
                                              { 1.0f, 0.6f, 0.3f }, { 1.0f, 0.7f, 0.4f }, { 0.9f, 0.7f, 0.3f }, { 1.0f, 0.8f, 0.5f }, { 0.8f, 0.4f, 1.0f }, { 0.4f, 1.0f, 0.8f } };
static const float ELIXIR_COL[EL_KINDS][3] = { { 0.7f, 0.75f, 0.85f }, { 1.0f, 0.3f, 0.2f }, { 1.0f, 0.85f, 0.2f }, { 0.4f, 0.4f, 1.0f }, { 1.0f, 0.5f, 0.1f }, { 0.3f, 1.0f, 0.4f } };
static const float CHARM_COL[CH_KINDS][3] = { { 1, 0.8f, 0.3f }, { 1, 0.5f, 0.2f }, { 0.8f, 0.9f, 1 }, { 0.5f, 1, 0.6f }, { 1, 0.4f, 0.9f }, { 0.5f, 0.8f, 1 }, { 0.6f, 0.9f, 1 }, { 1, 0.45f, 0.1f },
                                              { 0.4f, 0.7f, 1 }, { 1, 0.95f, 0.6f }, { 0.6f, 0.5f, 0.9f }, { 1, 0.3f, 0.3f }, { 1, 0.4f, 0.1f }, { 0.6f, 1, 0.9f }, { 1, 0.85f, 0.2f }, { 1, 0.9f, 0.4f } };

static void sync_items(const sim *s, float time) {
  static int n[IT_KINDS][4], nc[CH_KINDS];
  memset(n, 0, sizeof n), memset(nc, 0, sizeof nc);
  int total = 0;
  for (int k = 0; k < MAXG; k++) {
    const pickup *g = &s->g[k];
    if (!g->alive || !on_screen(g->x, 0.5f, g->y, 1)) continue;
    float bob = 0.18f + 0.1f * cw_sin(time * 3.0f + (float)k), spin = time * 1.2f + (float)k;
    const float *gc = ITEM_GLOW[g->kind];
    float glow_k = 1.0f;
    if (g->kind == IT_CHARM) {
      int c = g->arg % CH_KINDS;
      t3_instanced_mesh *im = V.charm[c];
      if (!im) continue;
      int i = nc[c]++;
      if (i >= im->instance_matrix->count) continue;
      t3_vec3 q = pull(g->x, 0.5f + bob, g->y, PULL_ITEM);
      put_up(im, i, q.x, q.y, q.z, spin, 1.7f);
      t3_instanced_mesh_set_color_at(im, i, (t3_color){ 1, 1, 1 });
      if (V.charm_glowf[c]) put_up(V.charm_glowf[c], i, q.x, q.y, q.z, spin, 1.7f), t3_instanced_mesh_set_color_at(V.charm_glowf[c], i, (t3_color){ CHARM_COL[c][0] * 2.5f, CHARM_COL[c][1] * 2.5f, CHARM_COL[c][2] * 2.5f });
      gc = CHARM_COL[c], glow_k = 1.6f;
    } else {
      int f = 0;
      bool still = false;
      if (g->kind >= IT_ROAST && g->kind <= IT_PLATE) f = g->kind - IT_ROAST, still = true;
      else if (g->kind == IT_GOLD) f = 0, still = true;
      else if (g->kind == IT_CUP) f = 1, still = true;
      else if (g->kind == IT_CHEST || g->kind == IT_CHEST_TRAP) f = 2, still = true;
      t3_instanced_mesh *im = V.item[g->kind][f] ? V.item[g->kind][f] : V.item[g->kind][0];
      if (!im) continue;
      int fi = V.item[g->kind][f] ? f : 0;
      /* kinds sharing a model share its counters: count through the first kind that has it */
      int owner = g->kind;
      for (int j = 0; j < g->kind; j++) if (V.item[j][fi] == im) { owner = j; break; }
      int i = n[owner][fi]++;
      if (i >= im->instance_matrix->count) continue;
      float y = still ? 0 : 0.35f + bob, yaw = still ? (float)(k * 53 % 100) * 0.06f : spin;
      float sc_ = still ? 1.35f : g->kind == IT_KEY ? 2.4f : 1.6f; /* a key reads from across the screen */
      t3_vec3 q = pull(g->x, y, g->y, PULL_ITEM);
      if (still) put(im, i, q.x, q.y, q.z, yaw, sc_);
      else put_up(im, i, q.x, q.y, q.z, yaw, sc_);
      t3_instanced_mesh_set_color_at(im, i, view_light_at(g->x, g->y));
      t3_instanced_mesh *gm = V.item_glow[g->kind][fi];
      if (gm) {
        if (still) put(gm, i, q.x, q.y, q.z, yaw, sc_);
        else put_up(gm, i, q.x, q.y, q.z, yaw, sc_);
        const float *lc = g->kind == IT_ELIXIR ? ELIXIR_COL[g->arg % EL_KINDS] : gc;
        float gk = g->kind == IT_KEY ? 4.0f : 2.4f;
        t3_instanced_mesh_set_color_at(gm, i, (t3_color){ lc[0] * gk, lc[1] * gk, lc[2] * gk });
      }
      if (g->kind == IT_ELIXIR) gc = ELIXIR_COL[g->arg % EL_KINDS], glow_k = 1.4f;
      if (g->kind == IT_POTION) glow_k = 1.5f;
      if (g->kind == IT_KEY) glow_k = 2.6f;
      if (still && g->kind != IT_CHEST && g->kind != IT_CHEST_TRAP) glow_k = 0.7f;
    }
    { float gr = g->kind == IT_KEY ? 3.4f + 0.6f * cw_sin(time * 4.0f + (float)k) : 2.2f;
      cw_splats_add(V.glows, (t3_vec3){ g->x, 0, g->y }, gr, gr, 0, 0, lin(gc[0], gc[1], gc[2]), glow_k * (0.85f + 0.15f * cw_sin(time * 4.0f + (float)k))); }
    if (g->kind == IT_KEY || g->kind == IT_POTION || g->kind == IT_ELIXIR || g->kind == IT_CHARM || g->kind == IT_CHEST) {
      float p = 0.3f + 0.08f * cw_sin(time * 3.0f + (float)k);
      t3_color lc = lin(gc[0], gc[1], gc[2]);
      beam(g->x, g->y, 2.6f, 0.42f, (t3_color){ lc.r * p, lc.g * p, lc.b * p });
    }
    total++;
  }
  for (int k = 0; k < IT_KINDS; k++)
    for (int f = 0; f < 4; f++) {
      bool owner = true;
      for (int j = 0; j < k; j++) if (V.item[j][f] && V.item[j][f] == V.item[k][f]) owner = false;
      if (owner && V.item[k][f]) done(V.item[k][f], n[k][f]), done(V.item_glow[k][f], n[k][f]);
    }
  for (int c = 0; c < CH_KINDS; c++) {
    bool owner = true;
    for (int j = 0; j < c; j++) if (V.charm[j] == V.charm[c]) owner = false;
    if (!owner) continue;
    int sum = 0;
    for (int j = c; j < CH_KINDS; j++) if (V.charm[j] == V.charm[c]) sum += nc[j];
    done(V.charm[c], CW_MIN(sum, V.charm[c] ? V.charm[c]->instance_matrix->count : 0)), done(V.charm_glowf[c], nc[c]);
  }
  V.st.pickups = total;
}

void view_sync(const sim *s, float time) {
  if (!V.root.id) return;
  cw_splats_begin(V.shadows);
  cw_splats_begin(V.glows);
  V.nbeam = 0;
  sync_walls(s);
  sync_spawners(s);
  sync_features(s, time);
  sync_monsters(s, time);
  sync_players(s, time);
  sync_shots(s, time);
  sync_items(s, time);
  { /* clouds of poison and burning lumps on the floor */
    for (int i = 0; i < MAXZ; i++) {
      const zone *z = &s->z[i];
      if (!z->alive) continue;
      float fade = cw_clampf((float)z->life / 40.0f, 0, 1), d = z->r * 2.2f;
      cw_splats_add(V.glows, (t3_vec3){ z->x, 0, z->y }, d, d, (float)i, 0, z->kind == 0 ? (t3_color){ 0.35f, 1.0f, 0.2f } : (t3_color){ 1.0f, 0.42f, 0.08f },
                    2.0f * fade * (0.85f + 0.15f * cw_sin((float)s->tick * 0.2f + (float)i)));
    }
  }
  cw_splats_end(V.shadows);
  cw_splats_end(V.glows);
  done(V.beam, V.nbeam);
  { /* development (A/B timing): CRAWLER_HIDE bits hide 1 the crowd, 2 the blob shadows, 4 the glows, 8 the walls, 16 the crowd's edges */
    static int hide = -1;
    if (hide < 0) hide = getenv("CRAWLER_HIDE") ? atoi(getenv("CRAWLER_HIDE")) : 0;
    if (hide & 1) for (int k = 0; k < EK_KINDS; k++) for (int f = 0; f < MB_MAXF; f++) { if (V.k[k].im[f]) V.k[k].im[f]->count = 0; if (V.k[k].edge[f]) V.k[k].edge[f]->count = 0; }
    if (hide & 16) for (int k = 0; k < EK_KINDS; k++) for (int f = 0; f < MB_MAXF; f++) if (V.k[k].edge[f]) V.k[k].edge[f]->count = 0;
    if (hide & 2) cw_splats_mesh(V.shadows)->count = 0;
    if (hide & 4) cw_splats_mesh(V.glows)->count = 0;
    if (hide & 8) for (int v = 0; v < MB_VARIANTS; v++) if (V.b.wall[v]) V.b.wall[v]->count = 0;
  }
  /* the floor is baked and takes no real-time light: each moving light shows on it as a pool */
  cw_splats_begin(V.pools);
  cw_splats_add_lights(V.pools, cw_node_parent(V.root), 0, 0.12f / 3.14159265f);
  cw_splats_end(V.pools);
}
