/* arenafps: the background battle. A fight past the walls that nobody in the arena can reach: two lines of soldiers
 * in a few places on the ground outside (the courtyard's valley slopes, the terraces' streets), running up, standing
 * to fire, falling and coming back; their muzzle flashes, the tracers between the lines, now and then a blast far
 * off, and a low rumble of it all. Visual and sound only: nothing here feeds the simulation (its own random stream).
 *
 * The figures are not soldiers with skeletons: the soldier model's coarsest mesh (models/soldier_*.glb.cwimport, its
 * last level) is skinned once on the CPU into a few still poses (four of the run, a stand, a fall), each pose one
 * instanced mesh per side, so the whole crowd is twelve instanced draws with no skinning and no animation player.
 * The flashes and the blasts are one instanced mesh, the tracers another. Each flash, tracer and blast swells over a
 * few ticks and fades over more: nothing strobes (AGENTS.md, "No strobing"), and a figure fires bursts at most about
 * twice a second, each its own swell and fade.
 *
 * The switch: crowd_set (the Battle mode); off, nothing is built, drawn or heard. ARENA_BATTLE_BG=1 turns it on in
 * the autopilot builds (the checks). */
#include "crowd.h"
#include "cartwheel/anim.h"
#include "cartwheel/ik.h"
#include "cartwheel/resource.h"

#define FIG_MAX 240    /* figures in all, both sides */
#define ZONE_MAX 6
#define NPOSE 6        /* run 0..3, stand (aim), down */
#define POSE_STAND 4
#define POSE_DOWN 5
#define FLASH_MAX 160  /* muzzle flashes and blasts (one instanced mesh) */
#define TRACER_MAX 96
#define UPDATE_EVERY 2 /* the figures move and the buffers are written every 2nd tick (30 Hz) */

crowd_stats CROWD_ST;

/* ── its own random stream (never the simulation's) ── */
static uint32_t RS = 0xBA77u;
static float rnd(void) { RS = RS * 1664525u + 1013904223u; return (float)(RS >> 8) / 16777216.0f; }
static float rnd2(float a, float b) { return a + (b - a) * rnd(); }

/* ── the baked poses (made once, kept for the process) ── */
static struct {
  bool tried, ok;
  t3_geometry *geo[NPOSE];
  t3_material *mat[2]; /* a clone of each side's soldier material */
  int tris;
} BK;

static cw_anim clip_named(const t3_gltf *g, const char *name) {
  for (int k = 0; k < g->animation_count; k++)
    if (g->animations[k]->name && !strcmp(g->animations[k]->name, name)) return cw_anim_from_t3_clip(g->animations[k], "Model");
  return (cw_anim){ 0 };
}

static t3_skinned_mesh *find_skinned(t3_object *o) {
  if (o->type == T3_SKINNED_MESH) return (t3_skinned_mesh *)o;
  for (int i = 0; i < o->child_count; i++) {
    t3_skinned_mesh *s = find_skinned(o->children[i]);
    if (s) return s;
  }
  return NULL;
}

/* the pose of clip a at time t onto the bound pose (rest first), then the bones */
static int pose_at(cw_pose *pz, cw_anim a, float t) {
  for (int i = 0; i < pz->count; i++) pz->pos[i] = pz->rest_pos[i], pz->rot[i] = pz->rest_rot[i], pz->scl[i] = pz->rest_scl[i];
  int hit = 0, nt = a.id ? cw_anim_track_count(a) : 0;
  for (int k = 0; k < nt; k++) {
    const char *path = cw_anim_track_path(a, k), *at = path ? strchr(path, '@') : NULL, *colon = at ? strchr(at, ':') : NULL;
    if (!at || !colon) continue;
    char nm[48];
    snprintf(nm, sizeof nm, "%.*s", (int)(colon - at - 1), at + 1);
    int bi = cw_pose_find(pz, nm);
    cw_value v;
    if (bi < 0 || !cw_anim_sample(a, k, t, &v)) continue;
    if (!strcmp(colon + 1, "quaternion")) pz->rot[bi] = (t3_quat){ v.v[0], v.v[1], v.v[2], v.v[3] }, hit++;
    else if (!strcmp(colon + 1, "position")) pz->pos[bi] = (t3_vec3){ v.v[0], v.v[1], v.v[2] };
  }
  cw_pose_flush(pz);
  return hit;
}

static float attr_get(const t3_attribute *a, int i, int c) {
  switch (a->type) {
    case T3_UINT16: { float v = (float)((const uint16_t *)a->array)[i * a->item_size + c]; return a->normalized ? v / 65535.0f : v; }
    case T3_UINT32: return (float)((const uint32_t *)a->array)[i * a->item_size + c];
    default: return ((const float *)a->array)[i * a->item_size + c];
  }
}
static int index_get(const t3_attribute *a, int i) {
  return a->type == T3_UINT16 ? ((const uint16_t *)a->array)[i] : a->type == T3_UINT32 ? (int)((const uint32_t *)a->array)[i] : (int)((const float *)a->array)[i];
}

/* the mesh as it stands now (its bones posed), skinned on the CPU, in the instance's space; only the vertices the
 * current index uses, renumbered */
static t3_geometry *skin_now(t3_object *root, t3_skinned_mesh *sm) {
  t3_object_update_matrix_world(root, true);
  t3_geometry *g = sm->mesh.geometry;
  t3_attribute *P = g->attributes[T3_ATTR_POSITION], *N = g->attributes[T3_ATTR_NORMAL], *UV = g->attributes[T3_ATTR_UV];
  t3_attribute *SI = g->attributes[T3_ATTR_SKIN_INDEX], *SW = g->attributes[T3_ATTR_SKIN_WEIGHT], *IX = g->index;
  t3_skeleton *sk = sm->skeleton;
  if (!P || !SI || !SW || !IX || !sk) return NULL;
  int nb = sk->bone_count, nv = P->count;
  int start = g->draw_start > 0 ? g->draw_start : 0, count = g->draw_count >= 0 ? g->draw_count : IX->count - start;
  if (start + count > IX->count) count = IX->count - start;
  /* each bone's skinning matrix, mesh to the instance: world . bind^-1 . bone . inverse . bind */
  t3_mat4 *S = malloc(sizeof(t3_mat4) * (size_t)nb), pre, tmp;
  t3_mat4_multiply(&pre, &sm->mesh.base.matrix_world, &sm->bind_matrix_inverse);
  for (int j = 0; j < nb; j++) {
    t3_mat4_multiply(&tmp, &sk->bones[j]->matrix_world, &sk->bone_inverses[j]);
    t3_mat4 t2;
    t3_mat4_multiply(&t2, &pre, &tmp);
    t3_mat4_multiply(&S[j], &t2, &sm->bind_matrix);
  }
  int *map = malloc(sizeof(int) * (size_t)nv);
  for (int i = 0; i < nv; i++) map[i] = -1;
  int used = 0;
  for (int i = 0; i < count; i++) { int v = index_get(IX, start + i); if (v >= 0 && v < nv && map[v] < 0) map[v] = used++; }
  float *pos = calloc((size_t)used * 3, sizeof(float)), *nor = calloc((size_t)used * 3, sizeof(float)), *uv = UV ? calloc((size_t)used * 2, sizeof(float)) : NULL;
  for (int v = 0; v < nv; v++) {
    int o = map[v];
    if (o < 0) continue;
    float p[3] = { attr_get(P, v, 0), attr_get(P, v, 1), attr_get(P, v, 2) }, n[3] = { 0, 1, 0 };
    if (N) n[0] = attr_get(N, v, 0), n[1] = attr_get(N, v, 1), n[2] = attr_get(N, v, 2);
    float rp[3] = { 0, 0, 0 }, rn[3] = { 0, 0, 0 };
    for (int k = 0; k < 4; k++) {
      float w = attr_get(SW, v, k);
      int j = (int)(attr_get(SI, v, k) + 0.5f);
      if (w <= 0 || j < 0 || j >= nb) continue;
      const float *e = S[j].e;
      for (int c = 0; c < 3; c++) {
        rp[c] += w * (e[c] * p[0] + e[4 + c] * p[1] + e[8 + c] * p[2] + e[12 + c]);
        rn[c] += w * (e[c] * n[0] + e[4 + c] * n[1] + e[8 + c] * n[2]);
      }
    }
    float l = sqrtf(rn[0] * rn[0] + rn[1] * rn[1] + rn[2] * rn[2]);
    if (l < 1e-6f) rn[0] = 0, rn[1] = 1, rn[2] = 0, l = 1;
    for (int c = 0; c < 3; c++) pos[o * 3 + c] = rp[c], nor[o * 3 + c] = rn[c] / l;
    if (uv) uv[o * 2] = attr_get(UV, v, 0), uv[o * 2 + 1] = attr_get(UV, v, 1);
  }
  uint32_t *idx = malloc(sizeof(uint32_t) * (size_t)count);
  for (int i = 0; i < count; i++) idx[i] = (uint32_t)map[index_get(IX, start + i)];
  t3_geometry *out = t3_geometry_new();
  t3_geometry_set_attribute(out, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, used, 3));
  t3_geometry_set_attribute(out, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nor, used, 3));
  if (uv) t3_geometry_set_attribute(out, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, used, 2));
  t3_geometry_set_index(out, t3_attribute_new(T3_UINT32, idx, count, 1));
  t3_geometry_compute_bounding_sphere(out);
  for (int s = 0; s < T3_ATTR_COUNT; s++) if (out->attributes[s]) t3_release(out->attributes[s]); /* the geometry holds them */
  t3_release(out->index);
  free(S), free(map), free(pos), free(nor), free(uv), free(idx);
  return out;
}

static bool bake(void) {
  if (BK.tried) return BK.ok;
  BK.tried = true;
  cw_res res[2] = { cw_res_load("models/soldier_a.glb", CW_RES_GLTF), cw_res_load("models/soldier_b.glb", CW_RES_GLTF) };
  const cw_gltf *g = cw_res_gltf(res[0]);
  if (!g || !g->gltf) { cw_warn("crowd: the soldier model did not load"); return false; }
  for (int s = 0; s < 2; s++) {
    t3_object *o = cw_gltf_instantiate(res[s]);
    t3_skinned_mesh *sm = o ? find_skinned(o) : NULL;
    if (sm && sm->mesh.material_count > 0 && sm->mesh.materials[0]) {
      t3_material *m = t3_material_clone(sm->mesh.materials[0]);
      m->fog = true;
      BK.mat[s] = m;
    }
    if (o) t3_release(o);
  }
  if (!BK.mat[0] || !BK.mat[1]) { cw_warn("crowd: no soldier material"); return false; }
  t3_object *probe = cw_gltf_instantiate(res[0]);
  t3_skinned_mesh *sm = probe ? find_skinned(probe) : NULL;
  cw_pose pz = { 0 };
  if (!sm || !cw_pose_bind(&pz, probe)) { cw_warn("crowd: no skinned soldier"); if (probe) t3_release(probe); return false; }
  BK.tris = cw_gltf_instance_set_lod(probe, 7); /* the coarsest level it has */
  cw_anim run = clip_named(g->gltf, "run_f"), idle = clip_named(g->gltf, "idle"), die = clip_named(g->gltf, "die");
  float rl = run.id ? cw_anim_length(run) : 1, dl = die.id ? cw_anim_length(die) : 1;
  int hits = 0;
  for (int p = 0; p < NPOSE; p++) {
    if (p < 4) hits += pose_at(&pz, run, rl * (float)p / 4.0f);
    else if (p == POSE_STAND) hits += pose_at(&pz, idle, 0);
    else hits += pose_at(&pz, die, dl);
    BK.geo[p] = skin_now(probe, sm);
  }
  if (run.id) cw_anim_release(run);
  if (idle.id) cw_anim_release(idle);
  if (die.id) cw_anim_release(die);
  cw_pose_free(&pz);
  t3_release(probe);
  BK.ok = hits > 0;
  for (int p = 0; p < NPOSE; p++) BK.ok = BK.ok && BK.geo[p];
  if (!BK.ok) cw_warn("crowd: the poses did not bake (%d keyed bones)", hits);
  else cw_info("crowd: %d poses baked, %d triangles a figure", NPOSE, BK.tris);
  return BK.ok;
}

/* ── the battle ── */
typedef struct zone { t3_vec3 c, n, l; float gap, depth, width; } zone;
enum { F_RUN, F_FIRE, F_DOWN, F_GONE };
typedef struct fig {
  int zone, side, state, t, dur, pose_t, burst;
  float x, z, y, tx, tz, yaw, speed, scale;
} fig;
typedef struct flash { t3_vec3 p; float size, peak; int t, up, down; t3_color c; } flash;
typedef struct tracer { t3_vec3 a, d; float len, dist, speed; int t; } tracer;

static struct {
  bool on, built;
  cw_node world, nodes[2 * NPOSE + 2];
  t3_instanced_mesh *im[2][NPOSE], *fx, *tr;
  int nzone, nfig;
  zone zn[ZONE_MAX];
  fig f[FIG_MAX];
  flash fl[FLASH_MAX];
  tracer tc[TRACER_MAX];
  int nfl, ntc, tick;
  float strength[2];
  cw_sound bed;
  cw_voice voice;
  float vol;
} C = { .strength = { 1, 1 } };

void crowd_set(bool on) { C.on = on; }
bool crowd_is_on(void) {
#ifdef ARENA_AUTOPILOT
  static int env = -1;
  if (env < 0) env = getenv("ARENA_BATTLE_BG") && atoi(getenv("ARENA_BATTLE_BG"));
  if (env) return true;
#endif
  return C.on;
}
void crowd_strength(float blue, float red) { C.strength[0] = CW_CLAMP(blue, 0.0f, 1.0f), C.strength[1] = CW_CLAMP(red, 0.0f, 1.0f); }

/* a place on the ground of zone z for side s: a rank depth back from the gap, anywhere along the line */
static bool spot(const zone *z, int s, float back, float *x, float *zz, float *y) {
  for (int tries = 0; tries < 14; tries++) {
    float along = rnd2(-z->width, z->width), d = z->gap + back * z->depth;
    float sd = s ? 1.0f : -1.0f;
    float px = z->c.x + z->n.x * d * sd + z->l.x * along, pz = z->c.z + z->n.z * d * sd + z->l.z * along;
    if (arena_outside_ground(px, pz, 0.5f, y)) { *x = px, *zz = pz; return true; }
  }
  return false;
}

/* the zones: where the level has open ground past the walls, high enough to show over them (the courtyard's slopes; a
 * flat city hides its streets behind the walls, so the terraces get the far blasts and the sound only). Tried on a circle of bearings and distances; a zone needs
 * most of its sample points on open ground */
static void zones_make(void) {
  C.nzone = 0;
  for (int k = 0; k < 64 && C.nzone < 4; k++) {
    float a = (float)k * 2.39996f, r = 96 + (float)(k % 5) * 11, sa, ca;
    cw_sincos(a, &sa, &ca);
    zone z = { .c = { sa * r, 0, ca * r }, .gap = 11, .depth = 10, .width = 12 };
    /* the sides face each other across the bearing (the line runs out from the arena), so both show from inside */
    z.n = v3(ca, 0, -sa), z.l = v3(sa, 0, ca);
    /* a street grid (open only along its streets): the line runs along the street */
    float y;
    int open = 0, total = 0;
    for (int i = -2; i <= 2; i++)
      for (int j = -2; j <= 2; j++) {
        float px = z.c.x + z.n.x * (float)i * 8 + z.l.x * (float)j * 5, pz = z.c.z + z.n.z * (float)i * 8 + z.l.z * (float)j * 5;
        total++, open += arena_outside_ground(px, pz, 0.5f, &y);
      }
    if (open * 10 < total * 6) { /* not open: try the nearest street through it (a grid of 30 m) */
      float sx = roundf(z.c.x / 30) * 30, sz = roundf(z.c.z / 30) * 30;
      bool along_z = fabsf(z.c.x - sx) < fabsf(z.c.z - sz);
      zone st = { .c = { along_z ? sx : z.c.x, 0, along_z ? z.c.z : sz }, .gap = 13, .depth = 12, .width = 1.6f };
      st.n = along_z ? v3(0, 0, 1) : v3(1, 0, 0), st.l = along_z ? v3(1, 0, 0) : v3(0, 0, 1);
      open = total = 0;
      for (int i = -3; i <= 3; i++) total++, open += arena_outside_ground(st.c.x + st.n.x * (float)i * 6, st.c.z + st.n.z * (float)i * 6, 0.4f, &y);
      if (open * 10 < total * 7) continue;
      z = st;
    }
    /* seen from the roofs over the walls: the ground there must stand well above the arena's roof line */
    if (!arena_outside_ground(z.c.x, z.c.z, -1, &y) || y < 7) continue;
    bool near = false;
    for (int q = 0; q < C.nzone; q++) { t3_vec3 d = v3sub(C.zn[q].c, z.c); near |= v3dot(d, d) < 55 * 55; }
    if (near) continue;
    C.zn[C.nzone++] = z;
  }
}

static void fig_start(fig *f, bool fresh) {
  const zone *z = &C.zn[f->zone];
  float x, zz, y;
  if (!spot(z, f->side, fresh ? rnd() : 0.7f + rnd() * 0.3f, &x, &zz, &y)) { f->state = F_GONE, f->t = 0, f->dur = 60; return; }
  f->x = x, f->z = zz, f->y = y;
  f->state = F_FIRE, f->t = 0, f->dur = (int)rnd2(60, 240), f->burst = (int)rnd2(0, 40);
  f->speed = rnd2(3.6f, 5.0f), f->scale = rnd2(0.96f, 1.04f);
}
static void fig_face_enemy(fig *f) {
  const zone *z = &C.zn[f->zone];
  float sd = f->side ? -1.0f : 1.0f; /* side 0 stands on -n and faces +n */
  f->yaw = cw_atan2(z->n.x * sd, z->n.z * sd) + rnd2(-0.25f, 0.25f);
}

static void build(cw_node world) {
  C.world = world;
  C.built = true;
  if (!bake()) return;
  zones_make();
  if (!C.nzone) cw_info("crowd: no open ground past the walls here: flashes and tracers only");
  for (int q = 0; q < C.nzone; q++) cw_info("crowd: zone %d at %.0f %.0f (bearing %.0f deg)", q, (double)C.zn[q].c.x, (double)C.zn[q].c.z, (double)(atan2f(C.zn[q].c.x, C.zn[q].c.z) * 57.29578f));
  C.nfig = C.nzone ? FIG_MAX / C.nzone * C.nzone : 0;
  for (int i = 0; i < C.nfig; i++) {
    fig *f = &C.f[i];
    memset(f, 0, sizeof *f);
    f->zone = i % C.nzone, f->side = (i / C.nzone) & 1;
    fig_start(f, true);
    fig_face_enemy(f);
  }
  /* the meshes: a pose a side, then the flashes and the tracers */
  int nn = 0;
  for (int s = 0; s < 2; s++)
    for (int p = 0; p < NPOSE; p++) {
      t3_instanced_mesh *im = t3_instanced_mesh_new(BK.geo[p], BK.mat[s], FIG_MAX / 2 + 8);
      im->mesh.base.cast_shadow = false, im->mesh.base.receive_shadow = false;
      im->count = 0;
      t3_instanced_mesh_set_bounding_sphere(im, &(t3_sphere){ { 0, 0, 0 }, 260 });
      C.im[s][p] = im;
      cw_node n = cw_node_new("MeshInstance3D", "BattleFigures");
      cw_node_add_child(world, n);
      cw_mesh3d_set_procedural(n, (t3_mesh *)im);
      C.nodes[nn++] = n;
      t3_release(im);
    }
  for (int k = 0; k < 2; k++) {
    t3_geometry *geo = k ? t3_box_geometry_new(1, 1, 1, 1, 1, 1) : t3_icosahedron_geometry_new(0.5f, 1);
    if (k) t3_geometry_translate(geo, 0, 0, 0.5f); /* a tracer: from its tail (z 0) to its head (z 1) */
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->blending = T3_ADDITIVE_BLENDING, m->transparent = true, m->depth_write = false, m->fog = true;
    int cap = k ? TRACER_MAX : FLASH_MAX;
    t3_instanced_mesh *im = t3_instanced_mesh_new(geo, m, cap);
    for (int i = 0; i < cap; i++) t3_instanced_mesh_set_color_at(im, i, (t3_color){ 0, 0, 0 });
    im->mesh.base.cast_shadow = false, im->mesh.base.receive_shadow = false;
    im->count = 0;
    t3_instanced_mesh_set_bounding_sphere(im, &(t3_sphere){ { 0, 0, 0 }, 320 });
    if (k) C.tr = im; else C.fx = im;
    cw_node n = cw_node_new("MeshInstance3D", k ? "BattleTracers" : "BattleFlashes");
    cw_node_add_child(world, n);
    cw_mesh3d_set_procedural(n, (t3_mesh *)im);
    C.nodes[nn++] = n;
    t3_release(im), t3_release(geo), t3_release(m);
  }
  C.nfl = C.ntc = 0;
}

/* ── flashes, tracers, blasts ── */
static void flash_add(t3_vec3 p, float size, float peak, int up, int down, t3_color c) {
  if (C.nfl >= FLASH_MAX) return;
  C.fl[C.nfl++] = (flash){ p, size, peak, 0, up, down, c };
}
static void tracer_add(t3_vec3 a, t3_vec3 b) {
  if (C.ntc >= TRACER_MAX) return;
  t3_vec3 d = v3sub(b, a);
  float dist = v3len(d);
  if (dist < 1) return;
  C.tc[C.ntc++] = (tracer){ a, v3scale(d, 1 / dist), rnd2(3.5f, 5.5f), dist, rnd2(110, 150), 0 };
}
static float env(int t, int up, int down) { /* swell (smooth) then fade (smooth) */
  if (t < up) { float x = (float)(t + 1) / (float)up; return x * x * (3 - 2 * x); }
  float x = 1 - (float)(t - up) / (float)down;
  return x <= 0 ? 0 : x * x * (3 - 2 * x);
}

static void write_fx(void) {
  float *m = t3_attribute_f32(C.fx->instance_matrix), *col = C.fx->instance_color ? t3_attribute_f32(C.fx->instance_color) : NULL;
  int n = 0;
  for (int i = 0; i < C.nfl; i++) {
    flash *f = &C.fl[i];
    float e = env(f->t, f->up, f->down), s = f->size * (0.55f + 0.45f * e);
    float *o = m + n * 16;
    memset(o, 0, 16 * sizeof(float));
    o[0] = o[5] = o[10] = s, o[12] = f->p.x, o[13] = f->p.y, o[14] = f->p.z, o[15] = 1;
    if (col) col[n * 3] = f->c.r * e * f->peak, col[n * 3 + 1] = f->c.g * e * f->peak, col[n * 3 + 2] = f->c.b * e * f->peak;
    n++;
  }
  C.fx->count = n;
  C.fx->instance_matrix->version++;
  if (C.fx->instance_color) C.fx->instance_color->version++;
  float *tm = t3_attribute_f32(C.tr->instance_matrix), *tcol = C.tr->instance_color ? t3_attribute_f32(C.tr->instance_color) : NULL;
  n = 0;
  for (int i = 0; i < C.ntc; i++) {
    tracer *t = &C.tc[i];
    float head = t->speed * (float)t->t * TICK_DT, tail = head - t->len;
    if (tail < 0) tail = 0;
    if (head > t->dist) head = t->dist;
    float len = head - tail;
    if (len <= 0.01f) continue;
    /* brightness: up over the first ticks, down over the last stretch (never on and off at once) */
    float e = fminf(fminf(1, (float)(t->t + 1) / 3.0f), fminf(1, (t->dist - tail) / (t->len * 1.5f)));
    t3_vec3 zx = v3scale(t->d, len), up = fabsf(t->d.y) > 0.9f ? v3(1, 0, 0) : v3(0, 1, 0);
    t3_vec3 xx = v3scale(v3norm(v3cross(up, t->d)), 0.13f), yy = v3scale(v3norm(v3cross(t->d, xx)), 0.13f);
    t3_vec3 at = v3add(t->a, v3scale(t->d, tail));
    float *o = tm + n * 16;
    o[0] = xx.x, o[1] = xx.y, o[2] = xx.z, o[3] = 0, o[4] = yy.x, o[5] = yy.y, o[6] = yy.z, o[7] = 0;
    o[8] = zx.x, o[9] = zx.y, o[10] = zx.z, o[11] = 0, o[12] = at.x, o[13] = at.y, o[14] = at.z, o[15] = 1;
    if (tcol) tcol[n * 3] = 2.2f * e, tcol[n * 3 + 1] = 1.5f * e, tcol[n * 3 + 2] = 0.6f * e;
    n++;
  }
  C.tr->count = n;
  C.tr->instance_matrix->version++;
  if (C.tr->instance_color) C.tr->instance_color->version++;
  CROWD_ST.flashes = C.nfl, CROWD_ST.tracers = n;
}

static void step_fx(void) {
  int w = 0;
  for (int i = 0; i < C.nfl; i++) { C.fl[i].t++; if (C.fl[i].t < C.fl[i].up + C.fl[i].down) C.fl[w++] = C.fl[i]; }
  C.nfl = w;
  w = 0;
  for (int i = 0; i < C.ntc; i++) {
    C.tc[i].t++;
    if (C.tc[i].speed * (float)C.tc[i].t * TICK_DT - C.tc[i].len < C.tc[i].dist) C.tc[w++] = C.tc[i];
  }
  C.ntc = w;
}

/* ── the figures ── */
static void step_figs(int dt) {
  int alive[2] = { 0, 0 };
  for (int i = 0; i < C.nfig; i++) {
    fig *f = &C.f[i];
    const zone *z = &C.zn[f->zone];
    f->t += dt;
    float sd = f->side ? -1.0f : 1.0f;
    switch (f->state) {
      case F_FIRE: {
        alive[f->side]++;
        f->burst -= dt;
        if (f->burst <= 0) { /* a burst: the flash at the muzzle, a tracer toward the other line (some of them) */
          float sy, cy;
          cw_sincos(f->yaw, &sy, &cy);
          t3_vec3 mz = v3(f->x + sy * 0.6f * f->scale, f->y + 1.35f * f->scale, f->z + cy * 0.6f * f->scale);
          flash_add(mz, 0.55f, 2.4f, 2, 10, (t3_color){ 1.0f, 0.62f, 0.25f });
          if (rnd() < 0.55f) {
            float dist = z->gap * 2 + rnd2(0, z->depth), miss = rnd2(-3, 3);
            t3_vec3 to = v3(mz.x + z->n.x * sd * dist + z->l.x * miss, mz.y + rnd2(-1.0f, 2.5f), mz.z + z->n.z * sd * dist + z->l.z * miss);
            tracer_add(mz, to);
          }
          f->burst = (int)(rnd2(28, 90) / (0.4f + 0.6f * C.strength[f->side]));
        }
        if (f->t >= f->dur) {
          /* the other side's strength is how likely this one goes down now */
          if (rnd() < 0.22f * (0.3f + C.strength[!f->side])) { f->state = F_DOWN, f->t = 0, f->dur = (int)rnd2(240, 480); break; }
          /* or a rush: a few metres forward or back, at a run */
          float step = rnd2(-5, 7), along = rnd2(-4, 4);
          float nx = f->x + z->n.x * sd * step + z->l.x * along, nz = f->z + z->n.z * sd * step + z->l.z * along, y;
          /* never into the gap's middle or past the back of its own ranks */
          float d = (nx - z->c.x) * z->n.x + (nz - z->c.z) * z->n.z;
          float mine = -sd * d;
          if (mine < z->gap * 0.7f || mine > z->gap + z->depth * 1.3f || !arena_outside_ground(nx, nz, 0.5f, &y)) { f->t = 0, f->dur = (int)rnd2(40, 120); break; }
          f->tx = nx, f->tz = nz, f->state = F_RUN, f->t = 0;
          f->yaw = cw_atan2(nx - f->x, nz - f->z);
        }
      } break;
      case F_RUN: {
        alive[f->side]++;
        float dx = f->tx - f->x, dz = f->tz - f->z, d = sqrtf(dx * dx + dz * dz), st = f->speed * TICK_DT * (float)dt;
        if (d <= st) { f->x = f->tx, f->z = f->tz, f->state = F_FIRE, f->t = 0, f->dur = (int)rnd2(90, 300); fig_face_enemy(f); }
        else f->x += dx / d * st, f->z += dz / d * st;
        float y;
        if (arena_outside_ground(f->x, f->z, -1, &y)) f->y = y;
        f->pose_t += dt;
      } break;
      case F_DOWN:
        if (f->t >= f->dur) { f->state = F_GONE, f->t = 0, f->dur = (int)rnd2(60, 200) + (int)(400 * (1 - C.strength[f->side])); }
        break;
      default: /* back in at the rear of its side */
        if (f->t >= f->dur) { fig_start(f, false); fig_face_enemy(f); }
        break;
    }
  }
  CROWD_ST.alive = alive[0] + alive[1];
}

static void write_figs(void) {
  int cnt[2][NPOSE] = { { 0 } };
  for (int i = 0; i < C.nfig; i++) {
    fig *f = &C.f[i];
    if (f->state == F_GONE) continue;
    int p = f->state == F_DOWN ? POSE_DOWN : f->state == F_FIRE ? POSE_STAND : (f->pose_t * 4 / 26 + i) % 4; /* a stride in about 0.43 s */
    t3_instanced_mesh *im = C.im[f->side][p];
    int k = cnt[f->side][p]++;
    if (k >= FIG_MAX / 2 + 8) continue;
    t3_mat4 m;
    float s, c;
    cw_sincos(f->yaw * 0.5f, &s, &c);
    t3_mat4_compose(&m, v3(f->x, f->y, f->z), (t3_quat){ 0, s, 0, c }, v3(f->scale, f->scale, f->scale));
    memcpy(t3_attribute_f32(im->instance_matrix) + k * 16, m.e, sizeof m.e);
  }
  int draws = 0, figs = 0;
  for (int s = 0; s < 2; s++)
    for (int p = 0; p < NPOSE; p++) {
      t3_instanced_mesh *im = C.im[s][p];
      im->count = CW_MIN(cnt[s][p], FIG_MAX / 2 + 8);
      im->instance_matrix->version++;
      figs += im->count, draws += im->count > 0;
    }
  CROWD_ST.figures = figs, CROWD_ST.draws = draws + (C.fx->count > 0) + (C.tr->count > 0);
}

static void blasts(void) {
  /* now and then a blast far off: in a zone, or over the horizon where there is none; it swells over a fifth of a
   * second and fades over a second */
  if (rnd() > 1.0f / 150.0f * (UPDATE_EVERY)) return;
  t3_vec3 p;
  if (C.nzone && rnd() < 0.7f) {
    const zone *z = &C.zn[(int)(rnd() * (float)C.nzone) % C.nzone];
    float y = 0, side = rnd() < 0.5f ? -1.0f : 1.0f, d = z->gap + rnd2(0, z->depth), along = rnd2(-z->width, z->width);
    p = v3(z->c.x + z->n.x * d * side + z->l.x * along, 0, z->c.z + z->n.z * d * side + z->l.z * along);
    arena_outside_ground(p.x, p.z, -1, &y);
    p.y = y + 1.5f;
  } else {
    float a = rnd() * 6.2831853f, r = rnd2(110, 160), sa, ca;
    cw_sincos(a, &sa, &ca);
    p = v3(sa * r, rnd2(2, 10), ca * r);
  }
  flash_add(p, rnd2(4, 7), 1.4f, 12, 60, (t3_color){ 1.0f, 0.5f, 0.18f });
  CROWD_ST.blasts++;
}

/* ── the sound: a few seconds of distant battle made once (rumble, far shots in bursts, thumps), looped ── */
static void bed_make(void) {
  int rate = cw_audio_sample_rate();
  if (rate <= 0) rate = 48000;
  const int secs = 8, n = rate * secs, fade = rate / 2;
  float *pcm = calloc((size_t)(n + fade), sizeof(float));
  uint32_t r = 0xBEDu;
#define RN() (r = r * 1664525u + 1013904223u, (float)(r >> 8) / 8388608.0f - 1.0f)
  float lp1 = 0, lp2 = 0, br = 0;
  for (int i = 0; i < n + fade; i++) { /* the rumble: brown noise through two low passes */
    br = br * 0.995f + RN() * 0.05f;
    lp1 += (br - lp1) * 0.02f, lp2 += (lp1 - lp2) * 0.02f;
    pcm[i] += lp2 * 1.6f;
  }
  for (float t = 0; t < (float)secs; ) { /* far shots in bursts: short dull pops */
    int k = 2 + (int)((RN() + 1) * 3);
    for (int b = 0; b < k; b++) {
      int at = (int)((t + (float)b * (0.09f + 0.03f * RN())) * (float)rate);
      float amp = 0.10f + 0.06f * RN(), q = 0;
      for (int j = 0; j < rate / 25 && at + j < n + fade; j++) {
        q += (RN() - q) * 0.12f;
        pcm[at + j] += q * amp * expf(-(float)j / ((float)rate * 0.012f));
      }
    }
    t += 0.25f + (RN() + 1) * 0.6f;
  }
  for (float t = 0.6f; t < (float)secs; t += 1.6f + (RN() + 1) * 1.2f) { /* thumps: low, slow */
    int at = (int)(t * (float)rate);
    float f0 = 38 + 14 * RN(), q = 0;
    for (int j = 0; j < rate && at + j < n + fade; j++) {
      float e = expf(-(float)j / ((float)rate * 0.35f)) * (1 - expf(-(float)j / ((float)rate * 0.02f)));
      q += (RN() - q) * 0.03f;
      pcm[at + j] += (0.32f * sinf(6.2831853f * f0 * (float)j / (float)rate) + q * 0.5f) * e;
    }
  }
#undef RN
  for (int i = 0; i < fade; i++) { float w = (float)i / (float)fade; pcm[i] = pcm[i] * w + pcm[n + i] * (1 - w); } /* a seamless loop */
  float peak = 1e-6f;
  for (int i = 0; i < n; i++) peak = fmaxf(peak, fabsf(pcm[i]));
  for (int i = 0; i < n; i++) pcm[i] *= 0.7f / peak;
  C.bed = cw_sound_from_pcm(pcm, n, 1, rate, NULL);
  free(pcm);
}

void crowd_reset(void) {
  if (C.voice.id) cw_voice_stop(C.voice, 0.3f), C.voice = (cw_voice){ 0 };
  C.built = false;
  C.world = (cw_node){ 0 };
  memset(C.nodes, 0, sizeof C.nodes);
  memset(C.im, 0, sizeof C.im);
  C.fx = C.tr = NULL;
  C.nfl = C.ntc = 0;
  memset(&CROWD_ST, 0, sizeof CROWD_ST);
}

void crowd_tick(cw_node world, const t3_vec3 *eyes, int neyes) {
  (void)eyes, (void)neyes;
  static bool fields;
  if (!fields) {
    fields = true;
    cw_debug_i32("crowd.figures", &CROWD_ST.figures), cw_debug_i32("crowd.alive", &CROWD_ST.alive);
    cw_debug_i32("crowd.flashes", &CROWD_ST.flashes), cw_debug_i32("crowd.tracers", &CROWD_ST.tracers);
    cw_debug_i32("crowd.blasts", &CROWD_ST.blasts), cw_debug_i32("crowd.draws", &CROWD_ST.draws);
  }
  bool on = crowd_is_on();
  if (C.built && (!on || C.world.id != world.id)) { /* off, or a new world: what was built goes */
    bool same = C.world.id == world.id;
    for (int i = 0; i < (int)(sizeof C.nodes / sizeof *C.nodes); i++) if (same && cw_node_valid(C.nodes[i])) cw_node_free(C.nodes[i]);
    crowd_reset();
  }
  if (!on || !cw_node_valid(world)) return;
  if (!C.built) {
    RS = 0xBA77u;
    build(world);
    if (!C.bed.id) bed_make();
    if (C.bed.id) C.voice = cw_audio_play(C.bed, &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = 0.001f, .loop = true, .fade_in = 2 });
    C.vol = 0;
  }
  if (!C.fx) return;
  C.tick++;
  if (C.tick % UPDATE_EVERY) return;
  step_fx();
  step_figs(UPDATE_EVERY);
  blasts();
  if (C.nfig) write_figs();
  write_fx();
  { /* the bed: low, louder while more are fighting */
    float want = 0.22f * (0.35f + 0.65f * (C.nfig ? (float)CROWD_ST.alive / (float)C.nfig : 0.6f));
    C.vol += (want - C.vol) * 0.05f;
    if (C.voice.id) cw_voice_set_volume(C.voice, C.vol);
  }
}
