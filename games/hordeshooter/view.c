/* hordeshooter view: draws the simulation. Render only: nothing here feeds
 * the rules. Enemies, bullets, pickups, crates, barrels and walls are
 * instanced meshes (one draw each) rewritten every frame; the ground is a
 * generated texture. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/render3d.h"
#include "game.h"

t3_color lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}

typedef struct part { t3_geometry *g; t3_mat4 m; t3_color c; } part;

/* parts merged into one geometry: positions through each part's matrix,
 * a vertex colour per part, normals recomputed */
static t3_geometry *merge(part *ps, int n) {
  int nv = 0, ni = 0;
  for (int i = 0; i < n; i++) {
    t3_geometry *g = ps[i].g;
    nv += g->attributes[T3_ATTR_POSITION]->count;
    ni += g->index ? g->index->count : g->attributes[T3_ATTR_POSITION]->count;
  }
  float *pos = malloc(sizeof(float) * 3 * (size_t)nv), *col = malloc(sizeof(float) * 3 * (size_t)nv);
  uint32_t *idx = malloc(sizeof(uint32_t) * (size_t)ni);
  int v0 = 0, i0 = 0;
  for (int i = 0; i < n; i++) {
    t3_geometry *g = ps[i].g;
    t3_attribute *pa = g->attributes[T3_ATTR_POSITION];
    const float *src = t3_attribute_f32(pa);
    for (int v = 0; v < pa->count; v++) {
      const t3_mat4 *m = &ps[i].m;
      float x = src[v * 3], y = src[v * 3 + 1], z = src[v * 3 + 2];
      pos[(v0 + v) * 3] = m->e[0] * x + m->e[4] * y + m->e[8] * z + m->e[12];
      pos[(v0 + v) * 3 + 1] = m->e[1] * x + m->e[5] * y + m->e[9] * z + m->e[13];
      pos[(v0 + v) * 3 + 2] = m->e[2] * x + m->e[6] * y + m->e[10] * z + m->e[14];
      col[(v0 + v) * 3] = ps[i].c.r, col[(v0 + v) * 3 + 1] = ps[i].c.g, col[(v0 + v) * 3 + 2] = ps[i].c.b;
    }
    if (g->index) {
      for (int k = 0; k < g->index->count; k++)
        idx[i0++] = (uint32_t)v0 + (g->index->type == T3_UINT16 ? ((uint16_t *)g->index->array)[k] : ((uint32_t *)g->index->array)[k]);
    } else
      for (int k = 0; k < pa->count; k++) idx[i0++] = (uint32_t)(v0 + k);
    v0 += pa->count;
    t3_release(g);
  }
  t3_geometry *out = t3_geometry_new();
  t3_geometry_set_attribute(out, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, nv, 3));
  t3_geometry_set_attribute(out, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, col, nv, 3));
  t3_geometry_set_index(out, t3_attribute_new(T3_UINT32, idx, ni, 1));
  t3_geometry_compute_vertex_normals(out);
  free(pos), free(col), free(idx);
  return out;
}

static t3_mat4 M(float x, float y, float z, float sx, float sy, float sz) {
  t3_mat4 m;
  t3_mat4_compose(&m, (t3_vec3){ x, y, z }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ sx, sy, sz });
  return m;
}
static t3_mat4 MR(float x, float y, float z, float sx, float sy, float sz, float rx) {
  t3_mat4 m;
  t3_quat q = { cw_sin(rx * 0.5f), 0, 0, cw_cos(rx * 0.5f) };
  t3_mat4_compose(&m, (t3_vec3){ x, y, z }, q, (t3_vec3){ sx, sy, sz });
  return m;
}

/* ── state ──────────────────────────────────────────────────────────── */

static struct {
  cw_node root;
  t3_instanced_mesh *enemy[EK_KINDS], *bullet, *rocket, *gem, *health, *crate, *barrel, *wall, *pillar, *shadowblob;
  t3_object *pl[MAXP];
  cw_node pn[MAXP];
  t3_mesh *pl_gun[MAXP];
  t3_mesh *pl_ring[MAXP];
  view_stats st;
  float cx, cy, cd;
  bool cam_init;
  t3_texture *ground_tex;
} V;

static const t3_mat4 ZERO;
static uint32_t hash2(uint32_t a, uint32_t b) {
  uint32_t h = a * 374761393u + b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float h01(uint32_t a, uint32_t b) { return (float)(hash2(a, b) & 0xffffff) / 16777216.0f; }

static t3_instanced_mesh *imesh(const char *name, t3_geometry *g, t3_material *m, int n, bool shadow) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  im->mesh.base.cast_shadow = shadow;
  im->mesh.base.receive_shadow = true;
  for (int i = 0; i < n; i++) {
    t3_instanced_mesh_set_matrix_at(im, i, &ZERO);
    t3_instanced_mesh_set_color_at(im, i, (t3_color){ 1, 1, 1 });
  }
  im->instance_matrix->usage = T3_DYNAMIC_DRAW;
  im->instance_color->usage = T3_DYNAMIC_DRAW;
  t3_sphere sph = { { 0, 0, 0 }, HALF * 1.5f };
  t3_instanced_mesh_set_bounding_sphere(im, &sph);
  cw_node node = cw_node_new("Node3D", name);
  cw_node_add_child(V.root, node);
  t3_object *wrap = t3_group_new();
  t3_object_add(wrap, &im->mesh.base);
  cw_node3d_set_object(node, wrap);
  t3_release(wrap);
  t3_release(g);
  t3_release(m);
  im->count = 0;
  return im;
}

static t3_material *mat(float r, float g, float b, float rough, float metal, bool vcol) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = lin(r, g, b);
  m->roughness = rough;
  m->metalness = metal;
  m->vertex_colors = vcol;
  return m;
}
static t3_material *glow(float r, float g, float b, float k) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = lin(r * 0.2f, g * 0.2f, b * 0.2f);
  m->emissive = lin(r, g, b);
  m->emissive_intensity = k;
  m->roughness = 0.6f;
  return m;
}

static t3_geometry *geo_runner(void) {
  part p[] = {
    { t3_sphere_geometry_new(0.34f, 10, 8), M(0, 0.32f, 0, 1, 0.8f, 1.25f), { 0.85f, 0.4f, 0.3f } },
    { t3_cone_geometry_new(0.1f, 0.34f, 6, 1, false), MR(-0.17f, 0.62f, 0.12f, 1, 1, 1, 0.0f), { 0.95f, 0.9f, 0.7f } },
    { t3_cone_geometry_new(0.1f, 0.34f, 6, 1, false), MR(0.17f, 0.62f, 0.12f, 1, 1, 1, 0.0f), { 0.95f, 0.9f, 0.7f } },
    { t3_sphere_geometry_new(0.07f, 6, 4), M(-0.14f, 0.42f, 0.34f, 1, 1, 1), { 5, 4, 0.5f } },
    { t3_sphere_geometry_new(0.07f, 6, 4), M(0.14f, 0.42f, 0.34f, 1, 1, 1), { 5, 4, 0.5f } },
    { t3_box_geometry_new(0.1f, 0.14f, 0.5f, 1, 1, 1), M(-0.3f, 0.1f, -0.1f, 1, 1, 1), { 0.35f, 0.3f, 0.3f } },
    { t3_box_geometry_new(0.1f, 0.14f, 0.5f, 1, 1, 1), M(0.3f, 0.1f, -0.1f, 1, 1, 1), { 0.35f, 0.3f, 0.3f } },
  };
  return merge(p, 7);
}
static t3_geometry *geo_brute(void) {
  part p[] = {
    { t3_sphere_geometry_new(0.72f, 12, 9), M(0, 0.7f, 0, 1, 0.85f, 1.15f), { 0.42f, 0.2f, 0.17f } },
    { t3_cone_geometry_new(0.16f, 0.6f, 6, 1, false), M(-0.4f, 1.3f, 0.1f, 1, 1, 1), { 0.9f, 0.85f, 0.7f } },
    { t3_cone_geometry_new(0.16f, 0.6f, 6, 1, false), M(0.4f, 1.3f, 0.1f, 1, 1, 1), { 0.9f, 0.85f, 0.7f } },
    { t3_cone_geometry_new(0.14f, 0.5f, 6, 1, false), M(0, 1.35f, -0.35f, 1, 1, 1), { 0.9f, 0.85f, 0.7f } },
    { t3_sphere_geometry_new(0.1f, 6, 4), M(-0.26f, 0.95f, 0.7f, 1, 1, 1), { 5, 1.2f, 0.4f } },
    { t3_sphere_geometry_new(0.1f, 6, 4), M(0.26f, 0.95f, 0.7f, 1, 1, 1), { 5, 1.2f, 0.4f } },
  };
  return merge(p, 6);
}
static t3_geometry *geo_bloater(void) {
  part p[] = {
    { t3_sphere_geometry_new(0.55f, 12, 9), M(0, 0.62f, 0, 1.1f, 1, 1.1f), { 0.7f, 0.85f, 0.35f } },
    { t3_sphere_geometry_new(0.2f, 8, 6), M(0.3f, 0.95f, 0.1f, 1, 1, 1), { 1.0f, 0.95f, 0.4f } },
    { t3_sphere_geometry_new(0.15f, 8, 6), M(-0.35f, 0.4f, 0.2f, 1, 1, 1), { 1.0f, 0.95f, 0.4f } },
    { t3_sphere_geometry_new(0.08f, 6, 4), M(-0.18f, 0.75f, 0.52f, 1, 1, 1), { 4, 0.4f, 0.2f } },
    { t3_sphere_geometry_new(0.08f, 6, 4), M(0.18f, 0.75f, 0.52f, 1, 1, 1), { 4, 0.4f, 0.2f } },
  };
  return merge(p, 5);
}
static t3_geometry *geo_boss(void) {
  part p[] = {
    { t3_sphere_geometry_new(1.7f, 16, 12), M(0, 1.6f, 0, 1.1f, 0.9f, 1.3f), { 0.55f, 0.2f, 0.25f } },
    { t3_cone_geometry_new(0.35f, 1.6f, 8, 1, false), M(-1.1f, 3.0f, 0.3f, 1, 1, 1), { 0.95f, 0.85f, 0.7f } },
    { t3_cone_geometry_new(0.35f, 1.6f, 8, 1, false), M(1.1f, 3.0f, 0.3f, 1, 1, 1), { 0.95f, 0.85f, 0.7f } },
    { t3_cone_geometry_new(0.3f, 1.3f, 8, 1, false), M(0, 3.2f, -0.7f, 1, 1, 1), { 0.95f, 0.85f, 0.7f } },
    { t3_sphere_geometry_new(0.28f, 8, 6), M(-0.7f, 2.0f, 1.8f, 1, 1, 1), { 6, 3, 0.4f } },
    { t3_sphere_geometry_new(0.28f, 8, 6), M(0.7f, 2.0f, 1.8f, 1, 1, 1), { 6, 3, 0.4f } },
    { t3_box_geometry_new(0.6f, 0.5f, 1.8f, 1, 1, 1), M(-1.9f, 0.4f, 0.3f, 1, 1, 1), { 0.3f, 0.2f, 0.2f } },
    { t3_box_geometry_new(0.6f, 0.5f, 1.8f, 1, 1, 1), M(1.9f, 0.4f, 0.3f, 1, 1, 1), { 0.3f, 0.2f, 0.2f } },
  };
  return merge(p, 8);
}

/* the yard's floor: a generated concrete with cracks and oil stains */
static t3_texture *make_ground(void) {
  enum { N = 256 };
  uint8_t *px = malloc(N * N * 4);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float n = 0, a = 0.5f, norm = 0;
      int p = 4;
      for (int o = 0; o < 4; o++) {
        float fx = (float)x / N * (float)p, fy = (float)y / N * (float)p;
        int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
        float tx = fx - (float)x0, ty = fy - (float)y0;
        tx = tx * tx * (3 - 2 * tx), ty = ty * ty * (3 - 2 * ty);
        uint32_t s = (uint32_t)o * 77u;
        float v00 = h01((uint32_t)(x0 % p) + s, (uint32_t)(y0 % p)), v10 = h01((uint32_t)((x0 + 1) % p) + s, (uint32_t)(y0 % p));
        float v01 = h01((uint32_t)(x0 % p) + s, (uint32_t)((y0 + 1) % p)), v11 = h01((uint32_t)((x0 + 1) % p) + s, (uint32_t)((y0 + 1) % p));
        n += a * (v00 + (v10 - v00) * tx + (v01 - v00) * ty + (v00 - v10 - v01 + v11) * tx * ty);
        norm += a;
        a *= 0.5f;
        p *= 2;
      }
      n /= norm;
      float slab = ((x % 64) < 2 || (y % 64) < 2) ? 0.55f : 1.0f; /* slab joints */
      float grit = 0.9f + 0.2f * h01((uint32_t)x, (uint32_t)y + 999u);
      float v = (0.30f + 0.28f * n) * slab * grit;
      px[(y * N + x) * 4] = (uint8_t)(CW_CLAMP(v * 1.02f, 0, 1) * 255);
      px[(y * N + x) * 4 + 1] = (uint8_t)(CW_CLAMP(v * 0.97f, 0, 1) * 255);
      px[(y * N + x) * 4 + 2] = (uint8_t)(CW_CLAMP(v * 0.9f, 0, 1) * 255);
      px[(y * N + x) * 4 + 3] = 255;
    }
  t3_texture *t = t3_texture_new(N, N, px);
  free(px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 8;
  t->color_space = T3_SRGB_COLOR_SPACE;
  t->repeat = (t3_vec2){ 14, 14 };
  return t;
}

void view_init(cw_node root) {
  memset(&V, 0, sizeof V);
  V.root = cw_node_new("Node3D", "View");
  cw_node_add_child(root, V.root);
  /* ground */
  t3_geometry *pg = t3_plane_geometry_new(HALF * 2 + 16, HALF * 2 + 16, 1, 1);
  t3_geometry_apply_matrix4(pg, &(t3_mat4){ { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1 } }); /* face +Y */
  t3_material *gm = t3_mesh_standard_material_new(0xffffff);
  V.ground_tex = make_ground();
  t3_material_set_texture(gm, T3_MAP, V.ground_tex);
  gm->roughness = 0.92f;
  t3_mesh *gmesh = t3_mesh_new(pg, gm);
  gmesh->base.receive_shadow = true;
  cw_node gn = cw_node_new("Node3D", "Ground");
  cw_node_add_child(V.root, gn);
  t3_object *w = t3_group_new();
  t3_object_add(w, gmesh);
  cw_node3d_set_object(gn, w);
  t3_release(w), t3_release(gmesh), t3_release(pg), t3_release(gm);
  /* the yard's wall: a low rim of blocks round the edge, one instanced mesh */
  V.enemy[EK_RUNNER] = imesh("Runners", geo_runner(), mat(1, 1, 1, 0.6f, 0.0f, true), MAXE, false);
  V.enemy[EK_BRUTE] = imesh("Brutes", geo_brute(), mat(1, 1, 1, 0.55f, 0.0f, true), 400, true);
  V.enemy[EK_BLOATER] = imesh("Bloaters", geo_bloater(), mat(1, 1, 1, 0.45f, 0.0f, true), 600, false);
  V.enemy[EK_BOSS] = imesh("Bosses", geo_boss(), mat(1, 1, 1, 0.5f, 0.1f, true), 4, true);
  V.bullet = imesh("Bullets", t3_box_geometry_new(0.07f, 0.07f, 0.7f, 1, 1, 1), glow(1, 0.85f, 0.35f, 6), MAXB, false);
  V.rocket = imesh("Rockets", t3_cylinder_geometry_new(0.1f, 0.1f, 0.7f, 8, 1, false), glow(1, 0.45f, 0.12f, 5), 64, false);
  V.gem = imesh("Gems", t3_octahedron_geometry_new(0.16f, 0), glow(0.3f, 1, 0.8f, 3.5f), MAXG, false);
  V.health = imesh("Health", t3_box_geometry_new(0.34f, 0.34f, 0.34f, 1, 1, 1), glow(1, 0.2f, 0.25f, 3.5f), 64, false);
  V.crate = imesh("Crates", t3_box_geometry_new(1.3f, 1.3f, 1.3f, 1, 1, 1), mat(0.62f, 0.42f, 0.2f, 0.85f, 0, false), MAXO, true);
  {
    part bp[] = {
      { t3_cylinder_geometry_new(0.5f, 0.5f, 1.1f, 14, 1, false), M(0, 0.55f, 0, 1, 1, 1), { 0.8f, 0.12f, 0.1f } },
      { t3_cylinder_geometry_new(0.52f, 0.52f, 0.14f, 14, 1, false), M(0, 0.4f, 0, 1, 1, 1), { 0.95f, 0.8f, 0.15f } },
      { t3_cylinder_geometry_new(0.52f, 0.52f, 0.14f, 14, 1, false), M(0, 0.85f, 0, 1, 1, 1), { 0.95f, 0.8f, 0.15f } },
    };
    V.barrel = imesh("Barrels", merge(bp, 3), mat(1, 1, 1, 0.4f, 0.4f, true), MAXO, true);
  }
  V.wall = imesh("Walls", t3_box_geometry_new(2.0f, 2.2f, 2.0f, 1, 1, 1), mat(0.55f, 0.55f, 0.52f, 0.9f, 0, false), MAXO, true);
  V.pillar = imesh("Pillars", t3_cylinder_geometry_new(1.2f, 1.3f, 4.5f, 10, 1, false), mat(0.38f, 0.38f, 0.42f, 0.7f, 0.2f, false), 16, true);
  /* players */
  static const float PC[MAXP][3] = { { 0.2f, 0.7f, 1 }, { 1, 0.55f, 0.1f }, { 0.4f, 0.95f, 0.35f }, { 0.95f, 0.3f, 0.75f } };
  for (int i = 0; i < MAXP; i++) {
    t3_object *g = t3_group_new();
    t3_material *bm = mat(PC[i][0], PC[i][1], PC[i][2], 0.45f, 0.1f, false);
    t3_material *dm = mat(0.12f, 0.12f, 0.14f, 0.5f, 0.6f, false);
    t3_geometry *body = t3_cylinder_geometry_new(0.34f, 0.4f, 0.9f, 12, 1, false), *head = t3_sphere_geometry_new(0.3f, 12, 8), *gun = t3_box_geometry_new(0.16f, 0.2f, 0.9f, 1, 1, 1);
    t3_mesh *mb = t3_mesh_new(body, bm), *mh = t3_mesh_new(head, bm), *mg = t3_mesh_new(gun, dm);
    mb->base.position = (t3_vec3){ 0, 0.55f, 0 };
    mh->base.position = (t3_vec3){ 0, 1.2f, 0 };
    mg->base.position = (t3_vec3){ 0.0f, 0.85f, -0.55f }; /* the model faces -Z */
    mb->base.cast_shadow = mh->base.cast_shadow = mg->base.cast_shadow = true;
    t3_object_add(g, mb), t3_object_add(g, mh), t3_object_add(g, mg);
    t3_material *rm = glow(PC[i][0], PC[i][1], PC[i][2], 3);
    t3_geometry *ring = t3_cylinder_geometry_new(0.7f, 0.7f, 0.03f, 24, 1, true);
    t3_mesh *mr = t3_mesh_new(ring, rm);
    mr->base.position = (t3_vec3){ 0, 0.03f, 0 };
    t3_object_add(g, mr);
    V.pl[i] = g;
    V.pl_gun[i] = mg;
    V.pl_ring[i] = mr;
    cw_node pn = cw_node_new("Node3D", "Player");
    cw_node_add_child(V.root, pn);
    cw_node3d_set_object(pn, g);
    V.pn[i] = pn;
    t3_release(mb), t3_release(mh), t3_release(mg), t3_release(mr);
    t3_release(body), t3_release(head), t3_release(gun), t3_release(ring), t3_release(bm), t3_release(dm), t3_release(rm);
    t3_release(g);
  }
}

void view_free(void) {
  if (V.ground_tex) t3_release(V.ground_tex);
  memset(&V, 0, sizeof V);
}

static void put(t3_instanced_mesh *im, int i, float x, float y, float z, float yaw, float sx, float sy, float sz) {
  t3_mat4 m;
  t3_quat q = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
  t3_mat4_compose(&m, (t3_vec3){ x, y, z }, q, (t3_vec3){ sx, sy, sz });
  t3_instanced_mesh_set_matrix_at(im, i, &m);
}
static void touch(t3_instanced_mesh *im, bool color) {
  im->instance_matrix->version++;
  if (color) im->instance_color->version++;
}

view_stats view_get_stats(void) { return V.st; }

void view_sync(const sim *s, float time) {
  if (!V.root.id) return;
  int cnt[EK_KINDS] = { 0 };
  float cull = 0;
  (void)cull;
  for (int i = 0; i < MAXE; i++) {
    const enemy *e = &s->e[i];
    if (!e->alive) continue;
    int k = e->kind;
    t3_instanced_mesh *im = V.enemy[k];
    int n = cnt[k];
    if (n >= im->instance_matrix->count) continue;
    cnt[k]++;
    float bob = 0.06f * cw_sin(time * (9.0f + (float)(i & 7)) + (float)i) * (k == EK_RUNNER ? 1.0f : 0.5f);
    float sq = 1.0f + (e->flash > 0 ? 0.18f : 0);
    float yaw = -e->face + 1.5708f; /* the model faces +Z; face is atan2(z, x) */
    put(im, n, e->x, bob, e->y, yaw, sq, sq, sq);
    float tint = 0.8f + 0.4f * h01((uint32_t)i, 5);
    t3_color c = e->flash > 0 ? (t3_color){ 4, 2.8f, 2.2f } : (t3_color){ tint, tint * (0.9f + 0.2f * h01((uint32_t)i, 9)), tint };
    t3_instanced_mesh_set_color_at(im, n, c);
  }
  V.st.enemies_drawn = 0;
  for (int k = 0; k < EK_KINDS; k++) {
    V.enemy[k]->count = cnt[k];
    touch(V.enemy[k], true);
    V.st.enemies_drawn += cnt[k];
  }
  /* bullets */
  int nb = 0, nr = 0;
  for (int i = 0; i < MAXB; i++) {
    const bullet *b = &s->b[i];
    if (!b->alive) continue;
    t3_instanced_mesh *im = b->kind ? V.rocket : V.bullet;
    int *c = b->kind ? &nr : &nb;
    if (*c >= im->instance_matrix->count) continue;
    float yaw = cw_atan2(-b->vx, -b->vy);
    if (b->kind) {
      t3_mat4 m;
      t3_quat qy = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) }, qx = { 0.70710678f, 0, 0, 0.70710678f };
      t3_quat q = t3_quat_multiply(qy, qx);
      t3_mat4_compose(&m, (t3_vec3){ b->x, 0.9f, b->y }, q, (t3_vec3){ 1, 1, 1 });
      t3_instanced_mesh_set_matrix_at(im, *c, &m);
    } else put(im, *c, b->x, 0.9f, b->y, yaw, 1, 1, 1 + (b->dmg > 2 ? 0.4f : 0));
    (*c)++;
  }
  V.bullet->count = nb, V.rocket->count = nr;
  touch(V.bullet, false), touch(V.rocket, false);
  V.st.bullets = nb + nr;
  /* pickups */
  int ng = 0, nh = 0;
  for (int i = 0; i < MAXG; i++) {
    const pickup *g = &s->g[i];
    if (!g->alive) continue;
    float blink = g->life < 180 && ((g->life / 6) & 1) ? 0 : 1;
    if (g->kind == 0) {
      put(V.gem, ng++, g->x, 0.35f + 0.08f * cw_sin(time * 4 + (float)i), g->y, time * 2 + (float)i, blink, blink, blink);
    } else if (nh < 64) put(V.health, nh++, g->x, 0.5f + 0.1f * cw_sin(time * 3 + (float)i), g->y, time + (float)i, blink, blink, blink);
  }
  V.gem->count = ng, V.health->count = nh;
  touch(V.gem, false), touch(V.health, false);
  V.st.pickups = ng + nh;
  /* the world's pieces */
  int nc = 0, nba = 0, nw = 0, np = 0;
  for (int i = 0; i < MAXO; i++) {
    const obstacle *o = &s->o[i];
    if (!o->alive) continue;
    float hurt = o->kind == OK_PILLAR ? 1 : (float)o->hp / (float)o->maxhp;
    float sh = 0.55f + 0.45f * hurt;
    switch (o->kind) {
    case OK_CRATE:
      put(V.crate, nc, o->x, 0.65f, o->y, (float)(i * 37 % 100) * 0.03f, 1, 1, 1);
      t3_instanced_mesh_set_color_at(V.crate, nc++, (t3_color){ sh, sh, sh });
      break;
    case OK_BARREL:
      put(V.barrel, nba, o->x, 0, o->y, 0, 1, 1, 1);
      t3_instanced_mesh_set_color_at(V.barrel, nba++, (t3_color){ 1, 1, 1 });
      break;
    case OK_WALL:
      put(V.wall, nw, o->x, 1.1f, o->y, 0, 1, 1, 1);
      t3_instanced_mesh_set_color_at(V.wall, nw++, (t3_color){ sh, sh * 0.95f, sh * 0.9f });
      break;
    default:
      if (np < 16) put(V.pillar, np++, o->x, 2.25f, o->y, 0, 1, 1, 1);
    }
  }
  V.crate->count = nc, V.barrel->count = nba, V.wall->count = nw, V.pillar->count = np;
  touch(V.crate, true), touch(V.barrel, true), touch(V.wall, true), touch(V.pillar, false);
  /* players */
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    t3_object *g = V.pl[i];
    if (!p->active) {
      cw_node3d_set_position(V.pn[i], (t3_vec3){ 0, -100, 0 });
      continue;
    }
    cw_node3d_set_position(V.pn[i], (t3_vec3){ p->x, p->down ? -0.6f : 0, p->y });
    cw_node3d_set_rotation(V.pn[i], (t3_vec3){ p->down ? 1.4f : 0, -p->aim - 1.5708f, 0 }); /* the gun is on -Z */
  }
}

void view_camera(const sim *s, cw_node camera, float aspect, float shake_x, float shake_y) {
  float cx, cy, hw, hh;
  sim_team_box(s, &cx, &cy, &hw, &hh);
  const float fov = 38.0f * 3.14159f / 180.0f, tilt = 62.0f * 3.14159f / 180.0f;
  /* distance so the spread of the players plus a margin fits the view */
  float half_h = CW_MAX(hh + 8.0f, (hw + 10.0f) / aspect);
  float d = CW_CLAMP(half_h / cw_sin(fov * 0.5f) * cw_cos(fov * 0.5f) * 0.92f, 24.0f, 50.0f);
  if (!V.cam_init) V.cx = cx, V.cy = cy, V.cd = d, V.cam_init = true;
  V.cx += (cx - V.cx) * 0.08f;
  V.cy += (cy - V.cy) * 0.08f;
  V.cd += (d - V.cd) * 0.04f;
  t3_vec3 at = { V.cx + shake_x, 0, V.cy + shake_y };
  t3_vec3 eye = { at.x, cw_sin(tilt) * V.cd, at.z + cw_cos(tilt) * V.cd };
  cw_node3d_set_position(camera, eye);
  cw_node3d_look_at(camera, at, (t3_vec3){ 0, 1, 0 });
}
