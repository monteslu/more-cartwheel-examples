/* hordeshooter view: draws the simulation. Render only: nothing here feeds
 * the rules. Enemies, bullets, pickups, crates, barrels and walls are
 * instanced meshes (one draw each) rewritten every frame; the ground is a
 * generated texture. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/render3d.h"
#include "cartwheel/vfs.h"
#include "cartwheel/core.h"
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
      {
        t3_color lc = ps[i].c.r > 1.5f || ps[i].c.g > 1.5f || ps[i].c.b > 1.5f ? ps[i].c : lin(ps[i].c.r, ps[i].c.g, ps[i].c.b); /* above 1.5: an emissive eye, kept as is */
        col[(v0 + v) * 3] = lc.r, col[(v0 + v) * 3 + 1] = lc.g, col[(v0 + v) * 3 + 2] = lc.b;
      }
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

/* ── baked characters ────────────────────────────────────────────────────
 * tools/blender/hordechars.py models, skins and poses each character in
 * Blender and writes every animation frame as positions and normals over one
 * shared topology (assets/models/*.cwchar). Each frame becomes a geometry;
 * the horde draws one instanced mesh per frame of the cycle, so an animated
 * crowd of thousands is a handful of draws and costs no skinning on the GPU. */
enum { CH_MAXF = 24, CH_MAXG = 2, CH_MAXC = 2 };
typedef struct chr {
  int nclip, ngroup, nf[CH_MAXC], first[CH_MAXC];
  t3_geometry *geo[CH_MAXG][CH_MAXF];
} chr;

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static bool chr_load(chr *c, const char *path) {
  size_t len = 0;
  uint8_t *buf = cw_vfs_read(path, &len);
  if (!buf || len < 16 || memcmp(buf, "CWCH", 4) != 0) {
    cw_error("horde: cannot read the character file %s", path);
    free(buf);
    return false;
  }
  memset(c, 0, sizeof *c);
  c->nclip = (int)rd32(buf + 8), c->ngroup = (int)rd32(buf + 12);
  const uint8_t *p = buf + 16;
  int total = 0;
  for (int k = 0; k < c->nclip; k++, p += 4) c->nf[k] = (int)rd32(p), c->first[k] = total, total += c->nf[k];
  for (int g = 0; g < c->ngroup; g++) {
    int nv = (int)rd32(p), nt = (int)rd32(p + 4);
    p += 8;
    const uint8_t *uv = p, *col = p + 8 * nv, *idx = p + 20 * nv;
    size_t idxb = ((size_t)nt * 6 + 3) & ~(size_t)3;
    p += 20 * (size_t)nv + idxb;
    float *cl = malloc(sizeof(float) * 3 * (size_t)nv);
    for (int v = 0; v < nv; v++) {
      float rgb[3];
      memcpy(rgb, col + 12 * v, 12);
      bool emi = rgb[0] > 1.5f || rgb[1] > 1.5f || rgb[2] > 1.5f;
      t3_color l = emi ? (t3_color){ rgb[0], rgb[1], rgb[2] } : lin(rgb[0], rgb[1], rgb[2]);
      cl[v * 3] = l.r, cl[v * 3 + 1] = l.g, cl[v * 3 + 2] = l.b;
    }
    for (int f = 0; f < total; f++) {
      t3_geometry *geo = t3_geometry_new();
      t3_geometry_set_attribute(geo, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, p, nv, 3));
      t3_geometry_set_attribute(geo, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, p + 12 * (size_t)nv, nv, 3));
      p += 24 * (size_t)nv;
      t3_geometry_set_attribute(geo, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, nv, 2));
      t3_geometry_set_attribute(geo, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, cl, nv, 3));
      t3_geometry_set_index(geo, t3_attribute_new(T3_UINT16, idx, nt * 3, 1));
      c->geo[g][f] = geo;
    }
    free(cl);
  }
  free(buf);
  return true;
}
static void chr_free(chr *c) {
  for (int g = 0; g < CH_MAXG; g++)
    for (int f = 0; f < CH_MAXF; f++)
      if (c->geo[g][f]) t3_release(c->geo[g][f]);
  memset(c, 0, sizeof *c);
}

/* ── state ──────────────────────────────────────────────────────────── */

static struct {
  cw_node root;
  chr ch[EK_KINDS], soldier;
  int bn[EK_KINDS]; /* frames per cycle */
  t3_instanced_mesh *eb[EK_KINDS][CH_MAXF];
  t3_mesh *pm[MAXP][CH_MAXC * CH_MAXF][CH_MAXG];
  t3_instanced_mesh *bullet, *rocket, *gem[3], *health, *crate, *barrel, *wall, *pillar, *shadowblob;
  t3_object *pl[MAXP];
  cw_node pn[MAXP];
  int pl_frame[MAXP];
  t3_mesh *pl_ring[MAXP];
  cw_node pl_light[MAXP];
  view_stats st;
  float cx, cy, cd;
  bool cam_init;
  t3_vec3 eye, at; /* the camera last frame, for view_screen */
  float aspect;
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

/* procedural PBR sets (albedo sRGB, tangent normal map from a height field,
 * roughness), all from integer hashes so every host makes the same texels */
enum { TX_GROUND, TX_CRATE, TX_WALL, TX_PILLAR, TX_HIDE, TX_ARMOR, TX_KINDS };
typedef struct pbr { t3_texture *map, *normal, *rough; uint8_t *px[3]; int n; } pbr;
static pbr PB[TX_KINDS];

static float vnoise(float fx, float fy, int p, uint32_t seed) {
  int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
  float tx = fx - (float)x0, ty = fy - (float)y0;
  tx = tx * tx * (3 - 2 * tx), ty = ty * ty * (3 - 2 * ty);
  uint32_t xa = (uint32_t)(((x0 % p) + p) % p), xb = (uint32_t)((((x0 + 1) % p) + p) % p);
  uint32_t ya = (uint32_t)(((y0 % p) + p) % p), yb = (uint32_t)((((y0 + 1) % p) + p) % p);
  float v00 = h01(xa + seed, ya), v10 = h01(xb + seed, ya), v01 = h01(xa + seed, yb), v11 = h01(xb + seed, yb);
  return v00 + (v10 - v00) * tx + (v01 - v00) * ty + (v00 - v10 - v01 + v11) * tx * ty;
}
static float fbm(float u, float v, int p, int oct, uint32_t seed) {
  float n = 0, a = 0.5f, norm = 0;
  for (int o = 0; o < oct; o++) {
    n += a * vnoise(u * (float)p, v * (float)p, p, seed + (uint32_t)o * 77u);
    norm += a, a *= 0.5f, p *= 2;
  }
  return n / norm;
}
static t3_texture *tex_finish(const uint8_t *px, bool srgb, int n) {
  t3_texture *t = t3_texture_new(n, n, px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 8;
  t->color_space = srgb ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
  return t;
}
static const pbr *pbr_get(int kind) {
  pbr *r = &PB[kind];
  if (r->map) return r;
  const int N = kind == TX_GROUND ? 1024 : 256; /* the yard floor is one big sheet with painted features, the rest tile small */
  float *h = malloc(sizeof(float) * N * N), *alb = malloc(sizeof(float) * N * N * 3), *ro = malloc(sizeof(float) * N * N);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float u = (float)x / N, v = (float)y / N, ht, c0, c1, c2, rough;
      float n = fbm(u, v, 4, 5, 10u + (uint32_t)kind * 31u), g = h01((uint32_t)x + 5000u * (uint32_t)kind, (uint32_t)y);
      switch (kind) {
      case TX_GROUND: { /* wet asphalt yard: slabs with joints, repair patches, cracks, oil, puddles, a painted dashed lane */
        const int S = N / 4;
        int sx = x % S, sy = y % S, cx = x / S, cy = y / S;
        float slab = h01((uint32_t)(cx * 7 + 1), (uint32_t)(cy * 13 + 5));
        float joint = (sx < 4 || sy < 4) ? 1.0f : 0.0f;
        float crack = fbm(u + 0.3f * n, v, 8, 3, 99u);
        crack = (crack > 0.485f && crack < 0.51f) ? 1.0f : 0.0f;
        float stain = fbm(u, v, 3, 3, 123u);
        stain = stain > 0.58f ? CW_MIN((stain - 0.58f) * 6.0f, 1.0f) : 0.0f;
        float pd = fbm(u + 0.06f * n, v, 5, 4, 555u);
        float puddle = pd > 0.6f ? CW_MIN((pd - 0.6f) * 14.0f, 1.0f) : 0.0f;
        float patch = slab > 0.8f ? 1.0f : 0.0f;
        float paint = 0.0f;
        if (cy == 1 && sy > S / 2 - 7 && sy < S / 2 + 7 && ((x / 56) & 1) == 0 && n > 0.3f) paint = 1.0f - 0.7f * h01((uint32_t)x, (uint32_t)(y / 3)); /* worn dashes */
        ht = 0.5f + 0.22f * n + 0.05f * g - 0.5f * joint - 0.2f * crack - 0.05f * patch;
        ht = ht + (0.5f - ht) * puddle * 0.9f;
        float k = 0.3f + 0.1f * n + 0.03f * g + 0.05f * (slab - 0.5f) - 0.12f * joint - 0.1f * crack - 0.08f * stain - 0.06f * puddle - 0.03f * patch;
        c0 = k * 1.04f, c1 = k * 0.99f, c2 = k * 0.94f;
        c0 += 0.62f * paint * 0.9f, c1 += 0.5f * paint * 0.9f, c2 += 0.06f * paint;
        rough = 0.86f - 0.5f * stain - 0.1f * joint - 0.8f * puddle + 0.1f * g * 0.5f;
        if (paint > 0.0f) rough = 0.8f;
        break;
      }
      case TX_CRATE: { /* planks with grain and nails */
        int plank = (int)(v * 4.0f);
        float pv = v * 4.0f - (float)plank;
        float grain = fbm(u * 0.4f + (float)plank * 0.37f, pv * 3.0f, 8, 3, 301u);
        float seam = (pv < 0.04f || pv > 0.96f) ? 1.0f : 0.0f;
        ht = 0.55f + 0.2f * grain - 0.5f * seam;
        float k = 0.45f + 0.35f * grain - 0.25f * seam;
        c0 = k * 0.85f + 0.12f, c1 = k * 0.55f + 0.05f, c2 = k * 0.28f;
        rough = 0.75f + 0.2f * grain;
        break;
      }
      case TX_WALL: { /* cinder block courses */
        int row = (int)(v * 4.0f);
        float bu = u * 2.0f + ((row & 1) ? 0.25f : 0.0f);
        bu -= floorf(bu);
        float bv = v * 4.0f - (float)row;
        float mortar = (bu < 0.04f || bu > 0.96f || bv < 0.06f || bv > 0.94f) ? 1.0f : 0.0f;
        ht = 0.6f + 0.15f * n + 0.05f * g - 0.55f * mortar;
        float k = 0.5f + 0.2f * n + 0.05f * g - 0.2f * mortar;
        c0 = k * 0.95f, c1 = k * 0.93f, c2 = k * 0.88f;
        rough = 0.95f - 0.2f * mortar;
        break;
      }
      case TX_HIDE: { /* creature skin: pores, ridges and mottling; albedo stays near white so the baked vertex colours lead */
        float pores = fbm(u, v, 16, 3, 700u), ridge = fbm(u + 0.2f * n, v, 6, 3, 710u);
        ridge = 1.0f - 2.0f * fabsf(ridge - 0.5f);
        ht = 0.5f + 0.18f * pores + 0.2f * ridge * ridge;
        float k = 0.82f + 0.2f * n - 0.12f * ridge * ridge;
        c0 = k, c1 = k * 0.97f, c2 = k * 0.94f;
        rough = 0.55f + 0.3f * pores;
        break;
      }
      case TX_ARMOR: { /* worn plate: brushed scratches, dents, edge wear */
        float scr = h01((uint32_t)(y * 7 + x / 40), 900u) * (fbm(u * 8.0f, v, 8, 2, 910u) > 0.55f ? 1.0f : 0.0f);
        float dent = fbm(u, v, 3, 4, 920u);
        ht = 0.5f + 0.25f * dent - 0.08f * scr + 0.02f * g;
        float k = 0.86f + 0.12f * dent - 0.18f * scr;
        c0 = k, c1 = k, c2 = k * 1.03f;
        rough = 0.35f + 0.35f * (1.0f - dent) + 0.2f * scr;
        break;
      }
      default: { /* pillar: poured concrete with bands */
        float band = ((int)(v * 8.0f) % 2) ? 0.04f : 0.0f;
        ht = 0.5f + 0.2f * n - band;
        float k = 0.38f + 0.2f * n - band;
        c0 = k, c1 = k, c2 = k * 1.05f;
        rough = 0.7f;
      }
      }
      h[y * N + x] = ht;
      alb[(y * N + x) * 3] = c0, alb[(y * N + x) * 3 + 1] = c1, alb[(y * N + x) * 3 + 2] = c2;
      ro[y * N + x] = rough;
    }
  uint8_t *a8 = malloc(N * N * 4), *n8 = malloc(N * N * 4), *r8 = malloc(N * N * 4);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float dx = (h[y * N + (x + 1) % N] - h[y * N + (x + N - 1) % N]) * 4.0f, dy = (h[((y + 1) % N) * N + x] - h[((y + N - 1) % N) * N + x]) * 4.0f;
      float l = sqrtf(dx * dx + dy * dy + 1.0f);
      int i = (y * N + x) * 4;
      n8[i] = (uint8_t)CW_CLAMP((-dx / l * 0.5f + 0.5f) * 255.0f, 0, 255);
      n8[i + 1] = (uint8_t)CW_CLAMP((dy / l * 0.5f + 0.5f) * 255.0f, 0, 255);
      n8[i + 2] = (uint8_t)CW_CLAMP((1.0f / l * 0.5f + 0.5f) * 255.0f, 0, 255);
      n8[i + 3] = 255;
      for (int k = 0; k < 3; k++) a8[i + k] = (uint8_t)(CW_CLAMP(alb[(y * N + x) * 3 + k], 0, 1) * 255.0f);
      a8[i + 3] = 255;
      r8[i] = r8[i + 1] = r8[i + 2] = (uint8_t)(CW_CLAMP(ro[y * N + x], 0, 1) * 255.0f);
      r8[i + 3] = 255;
    }
  r->map = tex_finish(a8, true, N);
  r->normal = tex_finish(n8, false, N);
  r->rough = tex_finish(r8, false, N);
  r->n = N;
  r->px[0] = a8, r->px[1] = n8, r->px[2] = r8; /* kept: repeated copies are made from them */
  free(h), free(alb), free(ro);
  return r;
}
static void pbr_apply(t3_material *m, int kind, float repeat, float normal_scale) {
  const pbr *p = pbr_get(kind);
  t3_texture *ts[3] = { p->map, p->normal, p->rough };
  t3_map_slot slots[3] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP };
  for (int i = 0; i < 3; i++) {
    t3_texture *c = ts[i];
    if (repeat != 1) { /* the repeat lives on the texture in three.js: a copy that shares the pixels */
      c = t3_texture_new(p->n, p->n, p->px[i]);
      c->wrap_s = c->wrap_t = T3_REPEAT;
      c->flip_y = false;
      c->generate_mipmaps = true;
      c->min_filter = T3_LINEAR_MIPMAP_LINEAR;
      c->mag_filter = T3_LINEAR;
      c->anisotropy = 8;
      c->color_space = i == 0 ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
      c->repeat = (t3_vec2){ repeat, repeat };
    }
    t3_material_set_texture(m, slots[i], c);
    if (c != ts[i]) t3_release(c);
  }
  m->normal_scale = (t3_vec2){ normal_scale, normal_scale };
  m->roughness = 1.0f;
}

static void put(t3_instanced_mesh *im, int i, float x, float y, float z, float yaw, float sx, float sy, float sz);
static void touch(t3_instanced_mesh *im, bool color);
/* ── the yard's set dressing: Blender-made props (tools/blender/hordeprops.py)
 * placed once, deterministically, as instanced meshes. A concrete wall round
 * the yard, floodlight masts with real lights, and the industrial ground
 * outside it (containers, barriers, rubble, tyres, pipes) that the high camera
 * sees over the wall. */
typedef struct place { float x, y, z, yaw; } place;
enum { LAMPS = 6 };
static t3_vec3 lamp_head[LAMPS];

static void dress_mesh(const char *name, const char *file, int tx, float rep, float rough, float metal, const place *pl, int n, bool shadow, bool lamp) {
  chr c;
  if (!chr_load(&c, file)) return;
  t3_instanced_mesh *ims[2] = { 0, 0 };
  for (int g = 0; g < c.ngroup; g++) {
    t3_material *m;
    if (g == 0) {
      m = mat(1, 1, 1, rough, metal, true);
      pbr_apply(m, tx, rep, 0.9f);
    } else
      m = glow(1.0f, 0.88f, 0.62f, 6);
    t3_retain(c.geo[g][0]);
    ims[g] = imesh(name, c.geo[g][0], m, n, shadow && g == 0);
  }
  chr_free(&c);
  for (int g = 0; g < 2; g++) {
    if (!ims[g]) continue;
    for (int i = 0; i < n; i++) put(ims[g], i, pl[i].x, pl[i].y, pl[i].z, pl[i].yaw, 1, 1, 1);
    ims[g]->count = n;
    touch(ims[g], true);
    t3_sphere sph = { { 0, 0, 0 }, HALF * 2.5f };
    t3_instanced_mesh_set_bounding_sphere(ims[g], &sph);
  }
  (void)lamp;
}

static void view_dress(void) {
  enum { PI_ = 3 };
  const float PI = 3.14159265f, W = HALF + 1.0f;
  static place wall[80], lamp[LAMPS], cont[96], barr[48], rub[48], tyr[32], pipe[24];
  int nw = 0, nl = 0, nc = 0, nb = 0, nr = 0, nt = 0, np = 0;
  for (int side = 0; side < 4; side++) {
    float yaw = side == 0 ? 0 : side == 1 ? PI : side == 2 ? PI * 0.5f : -PI * 0.5f;
    for (int i = 0; i < 19; i++) {
      float a = -HALF + 2.0f + 4.0f * (float)i;
      place p = side < 2 ? (place){ a, 0, side == 0 ? -W : W, yaw } : (place){ side == 2 ? -W : W, 0, a, yaw };
      wall[nw++] = p;
    }
  }
  static const float LP[LAMPS][3] = { { -22, 0, 0 }, { 22, 0, 0 }, { -22, 0, 1 }, { 22, 0, 1 }, { 0, 0, 2 }, { 0, 0, 3 } };
  for (int i = 0; i < LAMPS; i++) {
    int side = (int)LP[i][2];
    float yaw = side == 0 ? 0 : side == 1 ? PI : side == 2 ? PI * 0.5f : -PI * 0.5f;
    float m = HALF - 1.3f;
    float x = side < 2 ? LP[i][0] : (side == 2 ? -m : m), z = side < 2 ? (side == 0 ? -m : m) : LP[i][1];
    if (side >= 2) { x = side == 2 ? -m : m; z = LP[i][1]; }
    lamp[nl++] = (place){ x, 0, z, yaw };
    float s = cw_sin(yaw), c = cw_cos(yaw);
    lamp_head[i] = (t3_vec3){ x + 1.66f * s, 7.0f, z + 1.66f * c };
  }
  for (int side = 0; side < 4; side++) {
    float yaw = side == 0 ? 0 : side == 1 ? PI : side == 2 ? PI * 0.5f : -PI * 0.5f;
    for (int i = 0; i < 12; i++) {
      uint32_t h = hash2((uint32_t)side * 100u + (uint32_t)i, 17u);
      float a = -HALF - 4.0f + 7.2f * (float)i + (float)(h & 7) * 0.2f, d = HALF + 7.0f + (float)((h >> 4) & 3);
      float jy = ((float)((h >> 8) & 15) - 7.5f) * 0.012f;
      float x = side < 2 ? a : (side == 2 ? -d : d), z = side < 2 ? (side == 0 ? -d : d) : a;
      float cy = yaw + jy + (side < 2 ? PI * 0.5f : 0.0f); /* the container's long side runs along the wall */
      if ((h >> 12) % 10 < 7 && nc < 90) {
        cont[nc++] = (place){ x, 0, z, cy };
        if ((h >> 16) % 10 < 4 && nc < 90) cont[nc++] = (place){ x, 2.45f, z, cy + 0.02f };
      } else if (nb < 44) {
        for (int k = 0; k < 3 && nb < 44; k++) { /* a short run of barriers */
          float ox = side < 2 ? (float)k * 2.5f : 0, oz = side < 2 ? 0 : (float)k * 2.5f;
          barr[nb++] = (place){ x + ox, 0, z + oz, yaw + PI * 0.5f * (side < 2 ? 1 : 0) + jy };
        }
      }
      if (nr < 44 && (h >> 20) % 3 == 0) rub[nr++] = (place){ x + 3.0f, 0, z + 2.2f, (float)(h & 63) * 0.1f };
      if (nt < 30 && (h >> 22) % 3 == 0) tyr[nt++] = (place){ x - 3.4f, 0, z - 2.0f, (float)(h & 31) * 0.2f };
      if (np < 22 && (h >> 24) % 4 == 0) pipe[np++] = (place){ x + 1.0f, 0, z - 3.0f, yaw + PI * 0.5f };
    }
  }
  /* the four corners of the yard hold a rubble pile each */
  for (int k = 0; k < 4 && nr < 48; k++) rub[nr++] = (place){ (k & 1 ? 1 : -1) * (HALF - 3.2f), 0, (k & 2 ? 1 : -1) * (HALF - 3.2f), (float)k * 1.3f };
  dress_mesh("Walls", "models/wall.cwchar", TX_PILLAR, 1.0f, 0.9f, 0.0f, wall, nw, true, false);
  dress_mesh("Lamps", "models/lamp.cwchar", TX_ARMOR, 1.0f, 0.55f, 0.5f, lamp, nl, true, true);
  dress_mesh("Containers", "models/container.cwchar", TX_ARMOR, 0.7f, 0.55f, 0.35f, cont, nc, true, false);
  dress_mesh("Barriers", "models/barrier.cwchar", TX_PILLAR, 1.0f, 0.9f, 0.0f, barr, nb, true, false);
  dress_mesh("Rubble", "models/rubble.cwchar", TX_PILLAR, 1.4f, 0.95f, 0.0f, rub, nr, true, false);
  dress_mesh("Tyres", "models/tyres.cwchar", TX_HIDE, 3.0f, 0.8f, 0.0f, tyr, nt, false, false);
  dress_mesh("Pipes", "models/pipes.cwchar", TX_ARMOR, 1.0f, 0.5f, 0.5f, pipe, np, false, false);
  /* a floodlight at each lamp head: a cool white pool on the yard, no shadow maps (clustered lights) */
  for (int i = 0; i < LAMPS; i++) {
    cw_node l = cw_node_new("SpotLight3D", "Flood");
    cw_node_add_child(V.root, l);
    cw_node3d_set_position(l, lamp_head[i]);
    cw_node_set(l, "color", cw_val_color(0.72f, 0.86f, 1.0f, 1));
    cw_node_set(l, "energy", cw_val_float(700));
    cw_node_set(l, "range", cw_val_float(46));
    cw_node_set(l, "decay", cw_val_float(2));
    cw_node_set(l, "angle", cw_val_float(0.75f));
    cw_node_set(l, "penumbra", cw_val_float(0.7f));
    cw_node3d_look_at(l, (t3_vec3){ lamp_head[i].x * 0.35f, 0, lamp_head[i].z * 0.35f }, (t3_vec3){ 0, 1, 0 });
  }
}

void view_init(cw_node root) {
  memset(&V, 0, sizeof V);
  V.root = cw_node_new("Node3D", "View");
  cw_node_add_child(root, V.root);
  /* ground */
  const float GS = HALF * 2 + 70;
  t3_geometry *pg = t3_plane_geometry_new(GS, GS, 136, 136);
  t3_geometry_apply_matrix4(pg, &(t3_mat4){ { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1 } }); /* face +Y */
  { /* macro variation the tiling texture cannot give: slow mottling, the rim dirtier than the middle */
    const t3_attribute *pa = pg->attributes[T3_ATTR_POSITION];
    const float *pp = t3_attribute_f32((t3_attribute *)pa);
    float *col = malloc(sizeof(float) * 3 * (size_t)pa->count);
    for (int i = 0; i < pa->count; i++) {
      float x = pp[i * 3], z = pp[i * 3 + 2];
      float m = 0.72f + 0.6f * fbm(x / GS + 0.5f, z / GS + 0.5f, 6, 4, 4242u);
      float rim = CW_CLAMP((CW_MAX(fabsf(x), fabsf(z)) - (HALF - 6.0f)) / 8.0f, 0.0f, 1.0f);
      m *= 1.0f - 0.35f * rim;
      col[i * 3] = m * 1.0f, col[i * 3 + 1] = m * 0.97f, col[i * 3 + 2] = m * 0.93f;
    }
    t3_geometry_set_attribute(pg, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, col, pa->count, 3));
    free(col);
  }
  t3_material *gm = t3_mesh_standard_material_new(0xffffff);
  gm->vertex_colors = true;
  pbr_apply(gm, TX_GROUND, GS / 19.0f, 1.2f);
  t3_mesh *gmesh = t3_mesh_new(pg, gm);
  gmesh->base.receive_shadow = true;
  cw_node gn = cw_node_new("Node3D", "Ground");
  cw_node_add_child(V.root, gn);
  t3_object *w = t3_group_new();
  t3_object_add(w, gmesh);
  cw_node3d_set_object(gn, w);
  t3_release(w), t3_release(gmesh), t3_release(pg), t3_release(gm);
  /* the yard's wall: a low rim of blocks round the edge, one instanced mesh */
  {
    static const char *files[EK_KINDS] = { "models/runner.cwchar", "models/brute.cwchar", "models/bloater.cwchar", "models/boss.cwchar" };
    static const int cap[EK_KINDS] = { MAXE, 400, 600, 4 };
    static const float rough[EK_KINDS] = { 0.7f, 0.62f, 0.5f, 0.55f };
    static const float rep_[EK_KINDS] = { 1.6f, 1.2f, 1.4f, 0.9f };
    static const char *nm[EK_KINDS] = { "Runners", "Brutes", "Bloaters", "Bosses" };
    static const bool shadow[EK_KINDS] = { false, true, false, true };
    for (int k = 0; k < EK_KINDS; k++) {
      if (!chr_load(&V.ch[k], files[k])) continue;
      V.bn[k] = V.ch[k].nf[0];
      int bc = k == EK_BOSS ? 4 : cap[k] / V.bn[k] * 3 / 2 + 48;
      for (int f = 0; f < V.bn[k]; f++) {
        t3_material *m = mat(1, 1, 1, rough[k], 0.0f, true);
        pbr_apply(m, TX_HIDE, rep_[k], 0.8f);
        t3_retain(V.ch[k].geo[0][f]);
        V.eb[k][f] = imesh(nm[k], V.ch[k].geo[0][f], m, bc, shadow[k]);
      }
    }
  }
  V.bullet = imesh("Bullets", t3_box_geometry_new(0.07f, 0.07f, 0.7f, 1, 1, 1), glow(1, 0.85f, 0.35f, 6), MAXB, false);
  V.rocket = imesh("Rockets", t3_cylinder_geometry_new(0.1f, 0.1f, 0.7f, 8, 1, false), glow(1, 0.45f, 0.12f, 5), 64, false);
  { /* xp gems: a faceted shard (a stretched octahedron: each face keeps its own normal), glassy with a glowing core; a richer gem is
     * bigger and changes colour: cool cyan, green, then gold */
    static const float GC[3][3] = { { 0.25f, 0.9f, 1.0f }, { 0.35f, 1.0f, 0.45f }, { 1.0f, 0.86f, 0.3f } };
    static const char *GN[3] = { "Gems", "GemsRich", "GemsGold" };
    for (int k = 0; k < 3; k++) {
      t3_geometry *g = t3_octahedron_geometry_new(0.17f, 0);
      t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 1.9f, 0, 0, 0, 0, 0.8f, 0, 0, 0, 0, 1 } });
      /* leaning 40 degrees: from the high camera the shard shows its long diamond as it turns, not a square */
      t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 0.766f, 0.643f, 0, 0, -0.643f, 0.766f, 0, 0, 0, 0, 1 } });
      t3_material *m = t3_mesh_standard_material_new(0xffffff);
      m->color = lin(GC[k][0] * 0.35f, GC[k][1] * 0.35f, GC[k][2] * 0.35f);
      m->emissive = lin(GC[k][0], GC[k][1], GC[k][2]);
      m->emissive_intensity = k == 2 ? 1.5f : 1.6f + 0.4f * (float)k; /* gold kept gold, not tone-mapped to orange */
      m->roughness = 0.12f;
      m->metalness = 0.3f;
      V.gem[k] = imesh(GN[k], g, m, k == 0 ? MAXG : 256, false);
    }
  }
  {
    part hp[] = {
      { t3_box_geometry_new(0.42f, 0.14f, 0.14f, 1, 1, 1), M(0, 0, 0, 1, 1, 1), { 1, 1, 1 } },
      { t3_box_geometry_new(0.14f, 0.42f, 0.14f, 1, 1, 1), M(0, 0, 0, 1, 1, 1), { 1, 1, 1 } },
    };
    V.health = imesh("Health", merge(hp, 2), glow(1, 0.18f, 0.2f, 3.0f), 64, false);
  }
  {
    t3_material *cm = mat(1, 1, 1, 0.85f, 0, false);
    pbr_apply(cm, TX_CRATE, 1, 1.0f);
    V.crate = imesh("Crates", t3_box_geometry_new(1.3f, 1.3f, 1.3f, 1, 1, 1), cm, MAXO, true);
  }
  {
    part bp[] = {
      { t3_cylinder_geometry_new(0.5f, 0.5f, 1.1f, 14, 1, false), M(0, 0.55f, 0, 1, 1, 1), { 0.8f, 0.12f, 0.1f } },
      { t3_cylinder_geometry_new(0.52f, 0.52f, 0.14f, 14, 1, false), M(0, 0.4f, 0, 1, 1, 1), { 0.95f, 0.8f, 0.15f } },
      { t3_cylinder_geometry_new(0.52f, 0.52f, 0.14f, 14, 1, false), M(0, 0.85f, 0, 1, 1, 1), { 0.95f, 0.8f, 0.15f } },
    };
    V.barrel = imesh("Barrels", merge(bp, 3), mat(1, 1, 1, 0.4f, 0.4f, true), MAXO, true);
  }
  {
    t3_material *wm = mat(1, 1, 1, 0.9f, 0, false);
    pbr_apply(wm, TX_WALL, 1, 1.0f);
    V.wall = imesh("Walls", t3_box_geometry_new(2.0f, 2.2f, 2.0f, 1, 1, 1), wm, MAXO, true);
  }
  {
    t3_material *pm = mat(1, 1, 1, 0.7f, 0.0f, false);
    pbr_apply(pm, TX_PILLAR, 1, 1.0f);
    V.pillar = imesh("Pillars", t3_cylinder_geometry_new(1.2f, 1.3f, 4.5f, 16, 3, false), pm, 16, true);
  }
  view_dress();
  /* players: the soldier's cycles, one mesh per frame (shown by turns), body plus a team-coloured trim */
  static const float PC[MAXP][3] = { { 0.2f, 0.7f, 1 }, { 1, 0.55f, 0.1f }, { 0.4f, 0.95f, 0.35f }, { 0.95f, 0.3f, 0.75f } };
  bool have = chr_load(&V.soldier, "models/soldier.cwchar");
  for (int i = 0; i < MAXP; i++) {
    t3_object *g = t3_group_new();
    t3_material *bm = mat(1, 1, 1, 0.45f, 0.35f, true);
    pbr_apply(bm, TX_ARMOR, 2.0f, 0.9f);
    bm->emissive = lin(PC[i][0] * 0.5f + 0.1f, PC[i][1] * 0.5f + 0.1f, PC[i][2] * 0.5f + 0.1f); /* a faint team glow: the soldier never sinks into the dusk */
    bm->emissive_intensity = 0.35f;
    t3_material *tm = glow(PC[i][0], PC[i][1], PC[i][2], 7);
    if (have)
      for (int f = 0; f < V.soldier.nf[0] + V.soldier.nf[1]; f++) {
        t3_mesh *mb = t3_mesh_new(V.soldier.geo[0][f], bm), *mt = t3_mesh_new(V.soldier.geo[1][f], tm);
        mb->base.cast_shadow = true;
        mb->base.visible = mt->base.visible = f == 0;
        t3_object_add(g, mb), t3_object_add(g, mt);
        V.pm[i][f][0] = mb, V.pm[i][f][1] = mt;
        t3_release(mb), t3_release(mt);
      }
    t3_material *rm = glow(PC[i][0], PC[i][1], PC[i][2], 6);
    t3_geometry *ring = t3_ring_geometry_new(0.78f, 0.95f, 40, 1);
    t3_geometry_apply_matrix4(ring, &(t3_mat4){ { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1 } }); /* flat on the ground */
    t3_mesh *mr = t3_mesh_new(ring, rm);
    mr->base.position = (t3_vec3){ 0, 0.03f, 0 };
    t3_object_add(g, mr);
    V.pl[i] = g;
    V.pl_ring[i] = mr;
    cw_node pn = cw_node_new("Node3D", "Player");
    cw_node_add_child(V.root, pn);
    cw_node3d_set_object(pn, g);
    V.pn[i] = pn;
    { /* a light that rides over the soldier: the team colour washed toward white, a pool on the floor round them */
      cw_node l = cw_node_new("OmniLight3D", "PlayerLight");
      cw_node_add_child(V.root, l);
      cw_node_set(l, "color", cw_val_color(0.55f + 0.45f * PC[i][0], 0.55f + 0.45f * PC[i][1], 0.55f + 0.45f * PC[i][2], 1));
      cw_node_set(l, "energy", cw_val_float(0));
      cw_node_set(l, "range", cw_val_float(6));
      cw_node_set(l, "decay", cw_val_float(2));
      cw_node_set(l, "fog_energy", cw_val_float(0));
      cw_node3d_set_position(l, (t3_vec3){ 0, -60, 0 });
      V.pl_light[i] = l;
    }
    t3_release(mr), t3_release(ring), t3_release(bm), t3_release(tm), t3_release(rm);
    t3_release(g);
  }
}

void view_free(void) {
  for (int k = 0; k < EK_KINDS; k++) chr_free(&V.ch[k]);
  chr_free(&V.soldier);
  for (int i = 0; i < TX_KINDS; i++) {
    if (!PB[i].map) continue;
    t3_release(PB[i].map), t3_release(PB[i].normal), t3_release(PB[i].rough);
    free(PB[i].px[0]), free(PB[i].px[1]), free(PB[i].px[2]);
  }
  memset(PB, 0, sizeof PB);
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
  int cnt[EK_KINDS][CH_MAXF];
  memset(cnt, 0, sizeof cnt);
  static const float rate[EK_KINDS] = { 1.9f, 0.9f, 1.1f, 0.6f }; /* cycles per second */
  for (int i = 0; i < MAXE; i++) {
    const enemy *e = &s->e[i];
    if (!e->alive) continue;
    int k = e->kind, nf = V.bn[k];
    if (nf <= 0) continue;
    float ph = time * rate[k] * (0.85f + 0.3f * h01((uint32_t)i, 3)) + h01((uint32_t)i, 4) * 8.0f;
    int fr = (int)((ph - floorf(ph)) * (float)nf);
    t3_instanced_mesh *im = V.eb[k][fr];
    int n = cnt[k][fr];
    if (n >= im->instance_matrix->count) continue;
    cnt[k][fr]++;
    float sq = 1.0f + (e->flash > 0 ? 0.1f : 0);
    float yaw = -e->face + 1.5708f; /* the model faces +Z; face is atan2(z, x) */
    put(im, n, e->x, 0.0f, e->y, yaw, sq, sq, sq);
    float tint = 0.5f + 0.2f * h01((uint32_t)i, 5);
    t3_color c = e->flash > 0 ? (t3_color){ 4, 2.8f, 2.2f } : (t3_color){ tint * 0.82f, tint * (0.8f + 0.1f * h01((uint32_t)i, 9)), tint * 0.9f };
    t3_instanced_mesh_set_color_at(im, n, c);
  }
  V.st.enemies_drawn = 0;
  for (int k = 0; k < EK_KINDS; k++)
    for (int f = 0; f < V.bn[k]; f++) {
      V.eb[k][f]->count = cnt[k][f];
      touch(V.eb[k][f], true);
      V.st.enemies_drawn += cnt[k][f];
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
  int ng[3] = { 0, 0, 0 }, nh = 0;
  for (int i = 0; i < MAXG; i++) {
    const pickup *g = &s->g[i];
    if (!g->alive) continue;
    float blink = g->life < 180 && ((g->life / 6) & 1) ? 0 : 1;
    if (g->kind == 0) {
      int t = g->value >= 20 ? 2 : g->value >= 5 ? 1 : 0;
      if (ng[t] >= V.gem[t]->instance_matrix->count) continue;
      float sc = blink * (t == 2 ? 1.9f : t == 1 ? 1.4f : 1.0f);
      put(V.gem[t], ng[t]++, g->x, 0.45f + 0.12f * cw_sin(time * 3.2f + (float)i * 0.7f), g->y, time * 1.6f + (float)i, sc, sc, sc);
    } else if (nh < 64) put(V.health, nh++, g->x, 0.55f + 0.1f * cw_sin(time * 3 + (float)i), g->y, time + (float)i, blink, blink, blink);
  }
  for (int t = 0; t < 3; t++) V.gem[t]->count = ng[t], touch(V.gem[t], false);
  V.health->count = nh;
  touch(V.health, false);
  V.st.pickups = ng[0] + ng[1] + ng[2] + nh;
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
      cw_node_set(V.pl_light[i], "energy", cw_val_float(0));
      continue;
    }
    cw_node3d_set_position(V.pl_light[i], (t3_vec3){ p->x, 2.4f, p->y });
    cw_node_set(V.pl_light[i], "energy", cw_val_float(p->down ? 8.0f : 22.0f));
    cw_node3d_set_position(V.pn[i], (t3_vec3){ p->x, p->down ? -0.6f : 0, p->y });
    cw_node3d_set_rotation(V.pn[i], (t3_vec3){ p->down ? 1.4f : 0, -p->aim + 1.5708f, 0 }); /* the model faces +Z */
    if (V.soldier.nclip < 2) continue;
    float speed = sqrtf(p->vx * p->vx + p->vy * p->vy);
    bool run = speed > 0.6f && !p->down;
    int clip = run ? 0 : 1, nf = V.soldier.nf[clip];
    float cyc = run ? CW_MIN(speed / 6.0f, 1.3f) * 1.7f : 0.5f; /* run cycles per second follow the pace */
    float ph = time * cyc + (float)i * 0.31f;
    int fr = V.soldier.first[clip] + (int)((ph - floorf(ph)) * (float)nf);
    if (fr != V.pl_frame[i]) {
      for (int g = 0; g < 2; g++) {
        V.pm[i][V.pl_frame[i]][g]->base.visible = false;
        V.pm[i][fr][g]->base.visible = true;
      }
      V.pl_frame[i] = fr;
    }
  }
}

void view_camera(const sim *s, cw_node camera, float aspect, float shake_x, float shake_y) {
  float cx, cy, hw, hh;
  sim_team_box(s, &cx, &cy, &hw, &hh);
  const float fov = 38.0f * 3.14159f / 180.0f, tilt = 62.0f * 3.14159f / 180.0f;
  /* distance so the spread of the players plus a margin fits the view */
  float half_h = CW_MAX(hh + 8.0f, (hw + 10.0f) / aspect);
  float d = CW_CLAMP(half_h / cw_sin(fov * 0.5f) * cw_cos(fov * 0.5f) * 0.92f, 30.0f, 45.0f); /* a 1.5x zoom range */
  if (!V.cam_init) V.cx = cx, V.cy = cy, V.cd = d, V.cam_init = true;
  V.cx += (cx - V.cx) * 0.08f;
  V.cy += (cy - V.cy) * 0.08f;
  V.cd += (d - V.cd) * 0.04f;
  t3_vec3 at = { V.cx + shake_x, 0, V.cy + shake_y };
  t3_vec3 eye = { at.x, cw_sin(tilt) * V.cd, at.z + cw_cos(tilt) * V.cd };
  cw_node3d_set_position(camera, eye);
  cw_node3d_look_at(camera, at, (t3_vec3){ 0, 1, 0 });
  V.eye = eye, V.at = at, V.aspect = aspect;
}

/* where a world point lands on screen (0..1, y down), from the camera view_camera placed; false behind it */
bool view_screen(t3_vec3 p, float *u, float *v) {
  t3_vec3 f = { V.at.x - V.eye.x, V.at.y - V.eye.y, V.at.z - V.eye.z };
  float fl = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
  if (fl < 1e-4f || V.aspect <= 0) return false;
  f.x /= fl, f.y /= fl, f.z /= fl;
  t3_vec3 r = { -f.z, 0, f.x }; /* forward x up(0,1,0) */
  float rl = sqrtf(r.x * r.x + r.z * r.z);
  r.x /= rl, r.z /= rl;
  t3_vec3 up = { r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };
  t3_vec3 d = { p.x - V.eye.x, p.y - V.eye.y, p.z - V.eye.z };
  float z = d.x * f.x + d.y * f.y + d.z * f.z;
  if (z <= 0.01f) return false;
  float th = tanf(38.0f * 3.14159265f / 360.0f);
  float x = (d.x * r.x + d.y * r.y + d.z * r.z) / (z * th * V.aspect), y = (d.x * up.x + d.y * up.y + d.z * up.z) / (z * th);
  *u = 0.5f + 0.5f * x, *v = 0.5f - 0.5f * y;
  return true;
}
