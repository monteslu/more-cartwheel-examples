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
#include "cartwheel/lightmap.h"
#include "cartwheel/splats.h"
#include <stdio.h>
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
      { /* the exported triangle order shades each vertex about twice: reorder for the vertex cache (the same triangles) */
        float before, after;
        if (cw_geometry_optimize_vertex_cache(geo, &before, &after) && f == 0 && getenv("HORDE_BENCH"))
          fprintf(stderr, "horde: %s group %d: %d triangles, vertex cache ACMR %.2f -> %.2f\n", path, g, nt, before, after);
      }
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
  float bc_y[EK_KINDS], br[EK_KINDS]; /* a sphere round every frame of a kind (its centre height, its radius): culling */
  t3_instanced_mesh *eb[EK_KINDS][CH_MAXF];
  t3_mesh *pm[MAXP][CH_MAXC * CH_MAXF][CH_MAXG];
  t3_instanced_mesh *bullet, *rocket, *gem[3], *health, *crate, *barrel, *wall, *pillar, *shadowblob;
  t3_instanced_mesh *power[PU_KINDS], *power_glow, *grenade, *rail, *flame, *saw, *mine;
  t3_mesh *shield[MAXP];
  t3_object *pl[MAXP];
  cw_node pn[MAXP];
  int pl_frame[MAXP];
  t3_mesh *pl_ring[MAXP];
  t3_material *pl_mat[MAXP]; /* the soldier's body: tinted each frame by the floor's light under it */
  cw_node pl_light[MAXP];
  view_stats st;
  cw_splats *shadows, *pools, *sheen; /* blob shadows under everything that moves, light pools under the moving lights */
  float sun_x, sun_z, sun_len, sun_yaw; /* where shadows fall on the ground (unit) and how long they are for a 1 m caster */
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
      float lit = 1.0f;
      if (kind == TX_GROUND) { /* the floor draws baked and unlit (no normal map): its relief is painted into the colour, lit by the
                                * low sun the yard is baked with (its light travels 0.89, -0.21, -0.41): grooves and crack edges
                                * catch it on one side and fall dark on the other. Texture v runs along world -z. */
        const float LX = -0.889f, LY = 0.212f, LZ = 0.405f;
        float nx = -dx / l, ny = 1.0f / l, nz = -dy / l;
        float sh = (nx * LX + ny * LY + nz * LZ) / LY;
        lit = 1.0f + (CW_CLAMP(sh, 0.3f, 2.2f) - 1.0f) * 0.45f;
      }
      for (int k = 0; k < 3; k++) a8[i + k] = (uint8_t)(CW_CLAMP(alb[(y * N + x) * 3 + k] * lit, 0, 1) * 255.0f);
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
enum { RC = 24 };
static struct { int kind; float repeat; t3_texture *t[3]; } rc[RC]; /* the repeated copies made (view_free drops them) */
static int nrc;
static void pbr_apply(t3_material *m, int kind, float repeat, float normal_scale) {
  const pbr *p = pbr_get(kind);
  t3_texture *ts[3] = { p->map, p->normal, p->rough };
  t3_map_slot slots[3] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP };
  /* one copy per (set, repeat): the horde's 32 meshes of a kind share their three textures, so the GPU
   * holds them once and a frame binds them once, not three per draw */
  int hit = -1;
  for (int k = 0; k < nrc; k++)
    if (rc[k].kind == kind && rc[k].repeat == repeat && rc[k].t[0]) hit = k;
  for (int i = 0; i < 3; i++) {
    t3_texture *c = ts[i];
    if (repeat != 1 && hit >= 0) c = t3_retain(rc[hit].t[i]);
    else if (repeat != 1) { /* the repeat lives on the texture in three.js: a copy that shares the pixels */
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
    if (repeat != 1 && hit < 0 && nrc < RC) rc[nrc].t[i] = t3_retain(c);
    if (c != ts[i]) t3_release(c);
  }
  if (repeat != 1 && hit < 0 && nrc < RC) rc[nrc].kind = kind, rc[nrc].repeat = repeat, nrc++;
  m->normal_scale = (t3_vec2){ normal_scale, normal_scale };
  m->roughness = 1.0f;
}

static void put(t3_instanced_mesh *im, int i, float x, float y, float z, float yaw, float sx, float sy, float sz);
static void touch(t3_instanced_mesh *im, bool color);
static void view_shafts(void);
static void shafts_face(t3_vec3 eye);
/* ── the yard's set dressing: Blender-made props (tools/blender/hordeprops.py)
 * placed once, deterministically, as instanced meshes. A concrete wall round
 * the yard, floodlight masts with real lights, and the industrial ground
 * outside it (containers, barriers, rubble, tyres, pipes) that the high camera
 * sees over the wall. */
typedef struct place { float x, y, z, yaw; } place;
enum { LAMPS = 6 };
static t3_vec3 lamp_head[LAMPS], lamp_aim[LAMPS]; /* each floodlight and where its pool falls (pools apart, dark between) */

/* scenery that never moves: an instanced mesh under a STATIC MeshInstance3D, so the lightmap bake sees it (it
 * occludes the floodlights and the sun and bounces their light onto the yard; cartwheel/lightmap.h) */
static t3_instanced_mesh *imesh_static(const char *name, t3_geometry *g, t3_material *m, int n) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  im->mesh.base.cast_shadow = false;
  im->mesh.base.receive_shadow = false;
  for (int i = 0; i < n; i++) {
    t3_instanced_mesh_set_matrix_at(im, i, &ZERO);
    t3_instanced_mesh_set_color_at(im, i, (t3_color){ 1, 1, 1 });
  }
  cw_node node = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(V.root, node);
  cw_node_set(node, "gi_mode", cw_val_enum("STATIC"));
  cw_mesh3d_set_procedural(node, &im->mesh);
  t3_release(g);
  t3_release(m);
  im->count = 0;
  t3_release(im); /* the node holds it */
  return im;
}

static struct { t3_instanced_mesh *im; const struct place *pl; int n; } PROPS[16]; /* for relighting from the floor */
static int nprops;

static void dress_mesh(const char *name, const char *file, int tx, float rep, float rough, float metal, const place *pl, int n, bool shadow, bool lamp) {
  chr c;
  if (!chr_load(&c, file)) return;
  t3_instanced_mesh *ims[2] = { 0, 0 };
  (void)shadow;
  for (int g = 0; g < c.ngroup; g++) {
    t3_material *m;
    if (g == 0) {
      m = mat(1, 1, 1, rough, metal, true);
      pbr_apply(m, tx, rep, 0.9f);
    } else
      m = glow(1.0f, 0.88f, 0.62f, 6);
    t3_retain(c.geo[g][0]);
    char nm[48];
    snprintf(nm, sizeof nm, "%s%d", name, g);
    ims[g] = imesh_static(nm, c.geo[g][0], m, n);
  }
  chr_free(&c);
  for (int g = 0; g < 2; g++) {
    if (!ims[g]) continue;
    for (int i = 0; i < n; i++) put(ims[g], i, pl[i].x, pl[i].y, pl[i].z, pl[i].yaw, 1, 1, 1);
    ims[g]->count = n;
    touch(ims[g], true);
    t3_sphere sph = { { 0, 0, 0 }, HALF * 2.5f };
    t3_instanced_mesh_set_bounding_sphere(ims[g], &sph);
    if (g == 0 && nprops < 16) PROPS[nprops].im = ims[g], PROPS[nprops].pl = pl, PROPS[nprops].n = n, nprops++;
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
    { static const float AIM[LAMPS][2] = { { 0.42f, 0.2f }, { 0.55f, -0.25f }, { 0.5f, 0.3f }, { 0.4f, -0.15f }, { 0.62f, 0.12f }, { 0.48f, -0.3f } };
      float ax = lamp_head[i].x * AIM[i][0], az = lamp_head[i].z * AIM[i][0];
      /* off the line to the centre, each its own way */
      lamp_aim[i] = (t3_vec3){ ax + az * AIM[i][1], 0, az - ax * AIM[i][1] }; }
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
    cw_node_set(l, "energy", cw_val_float(1100));
    cw_node_set(l, "range", cw_val_float(46));
    cw_node_set(l, "decay", cw_val_float(2));
    cw_node_set(l, "angle", cw_val_float(0.66f));
    cw_node_set(l, "penumbra", cw_val_float(0.65f));
    cw_node_set(l, "bake_direct", cw_val_bool(true)); /* the yard's pool is in the lightmap */
    cw_node_set(l, "realtime", cw_val_bool(false)); /* and only there: what moves takes it from the floor (view_ground_light) */
    cw_node3d_look_at(l, lamp_aim[i], (t3_vec3){ 0, 1, 0 });
  }
}

/* ── power-ups: one modelled, glowing pickup per type, each its own silhouette and colour ─────────── */
static const float PUC[PU_KINDS][3] = {
  { 1.0f, 0.62f, 0.15f }, { 1.0f, 0.95f, 0.3f }, { 1.0f, 0.3f, 0.15f }, { 0.35f, 0.75f, 1.0f }, { 1.0f, 0.45f, 0.05f }, /* weapons */
  { 1.0f, 0.25f, 0.55f }, { 0.95f, 1.0f, 0.4f },                                                                       /* ammo */
  { 0.45f, 1.0f, 0.35f }, { 0.9f, 0.9f, 0.25f },                                                                       /* charges */
  { 0.85f, 0.88f, 0.95f }, { 0.3f, 0.6f, 1.0f }, { 0.75f, 0.35f, 1.0f },                                              /* helpers */
};
static t3_mat4 MRY(float x, float y, float z, float sx, float sy, float sz, float rz) {
  t3_mat4 m;
  t3_quat q = { 0, 0, cw_sin(rz * 0.5f), cw_cos(rz * 0.5f) };
  t3_mat4_compose(&m, (t3_vec3){ x, y, z }, q, (t3_vec3){ sx, sy, sz });
  return m;
}
static t3_geometry *power_model(int t) {
  part p[12];
  int n = 0;
  const t3_color hi = { 1, 1, 1 }, mid = { 0.55f, 0.55f, 0.6f }, dk = { 0.18f, 0.18f, 0.2f };
#define P(g, m, c) p[n++] = (part){ g, m, c }
  switch (t) {
  case PU_SHOTGUN: /* a fan of three short barrels on a stock */
    for (int k = -1; k <= 1; k++) P(t3_cylinder_geometry_new(0.05f, 0.05f, 0.55f, 8, 1, false), MR(0.1f * (float)k, 0, 0.1f, 1, 1, 1, 1.5708f), mid);
    P(t3_box_geometry_new(0.3f, 0.12f, 0.3f, 1, 1, 1), M(0, 0, -0.25f, 1, 1, 1), dk);
    break;
  case PU_MINIGUN: /* a ring of six barrels */
    for (int k = 0; k < 6; k++) P(t3_cylinder_geometry_new(0.035f, 0.035f, 0.6f, 6, 1, false), MR(0.09f * cw_cos((float)k * 1.047f), 0.09f * cw_sin((float)k * 1.047f), 0, 1, 1, 1, 1.5708f), mid);
    P(t3_cylinder_geometry_new(0.16f, 0.16f, 0.16f, 12, 1, false), MR(0, 0, -0.25f, 1, 1, 1, 1.5708f), dk);
    break;
  case PU_ROCKETS: /* a rocket: body, nose, fins */
    P(t3_cylinder_geometry_new(0.09f, 0.09f, 0.5f, 10, 1, false), M(0, 0, 0, 1, 1, 1), mid);
    P(t3_cone_geometry_new(0.09f, 0.2f, 10, 1, false), M(0, 0.35f, 0, 1, 1, 1), hi);
    for (int k = 0; k < 3; k++) P(t3_box_geometry_new(0.02f, 0.16f, 0.14f, 1, 1, 1), MR(0.1f * cw_cos((float)k * 2.09f), -0.2f, 0.1f * cw_sin((float)k * 2.09f), 1, 1, 1, 0), dk);
    break;
  case PU_RAIL: /* a long barrel with coils */
    P(t3_box_geometry_new(0.07f, 0.07f, 0.8f, 1, 1, 1), M(0, 0, 0, 1, 1, 1), mid);
    for (int k = 0; k < 4; k++) P(t3_torus_geometry_new(0.1f, 0.025f, 6, 12, 6.2832f), M(0, 0, -0.25f + 0.17f * (float)k, 1, 1, 1), hi);
    break;
  case PU_FLAME: /* a tank and a nozzle */
    P(t3_cylinder_geometry_new(0.13f, 0.13f, 0.4f, 12, 1, false), M(0, 0, 0, 1, 1, 1), mid);
    P(t3_cone_geometry_new(0.08f, 0.25f, 8, 1, false), MR(0, 0.05f, 0.3f, 1, 1, 1, 1.5708f), hi);
    break;
  case PU_EXPLOSIVE: /* a shell with a band */
    P(t3_cylinder_geometry_new(0.08f, 0.08f, 0.3f, 10, 1, false), M(0, 0, 0, 1, 1, 1), hi);
    P(t3_cone_geometry_new(0.08f, 0.16f, 10, 1, false), M(0, 0.23f, 0, 1, 1, 1), hi);
    P(t3_torus_geometry_new(0.085f, 0.025f, 6, 14, 6.2832f), MR(0, -0.05f, 0, 1, 1, 1, 1.5708f), dk);
    break;
  case PU_RAPID: /* two chevrons */
    for (int k = 0; k < 2; k++) {
      P(t3_box_geometry_new(0.3f, 0.07f, 0.07f, 1, 1, 1), MRY(-0.08f, 0.12f - 0.2f * (float)k, 0, 1, 1, 1, 0.6f), hi);
      P(t3_box_geometry_new(0.3f, 0.07f, 0.07f, 1, 1, 1), MRY(0.08f, 0.12f - 0.2f * (float)k, 0, 1, 1, 1, -0.6f), hi);
    }
    break;
  case PU_GRENADES: /* a pineapple grenade: body, lever, ring */
    P(t3_sphere_geometry_new(0.16f, 12, 8), M(0, 0, 0, 1, 1.15f, 1), mid);
    P(t3_box_geometry_new(0.05f, 0.18f, 0.04f, 1, 1, 1), M(0.1f, 0.12f, 0, 1, 1, 1), dk);
    P(t3_torus_geometry_new(0.06f, 0.015f, 6, 12, 6.2832f), M(-0.06f, 0.22f, 0, 1, 1, 1), hi);
    break;
  case PU_MINES: /* a flat disc with a dome */
    P(t3_cylinder_geometry_new(0.24f, 0.26f, 0.08f, 16, 1, false), M(0, 0, 0, 1, 1, 1), mid);
    P(t3_sphere_geometry_new_ex(0.08f, 10, 6, 0, 6.2832f, 0, 1.5708f), M(0, 0.04f, 0, 1, 1, 1), hi);
    break;
  case PU_SAWS: /* a toothed blade */
    P(t3_cylinder_geometry_new(0.26f, 0.26f, 0.03f, 20, 1, false), M(0, 0, 0, 1, 1, 1), hi);
    for (int k = 0; k < 8; k++) P(t3_box_geometry_new(0.08f, 0.03f, 0.08f, 1, 1, 1), MR(0.28f * cw_cos((float)k * 0.785f), 0, 0.28f * cw_sin((float)k * 0.785f), 1, 1, 1, 0), mid);
    break;
  case PU_SHIELD: /* a kite shield */
    P(t3_box_geometry_new(0.34f, 0.4f, 0.06f, 1, 1, 1), M(0, 0.05f, 0, 1, 1, 1), hi);
    P(t3_cone_geometry_new(0.24f, 0.22f, 4, 1, false), MR(0, -0.25f, 0, 1, 0.25f, 1, 3.1416f), hi);
    break;
  default: /* a horseshoe magnet */
    P(t3_torus_geometry_new(0.18f, 0.06f, 8, 16, 3.1416f), M(0, 0.05f, 0, 1, 1, 1), hi);
    P(t3_box_geometry_new(0.12f, 0.16f, 0.12f, 1, 1, 1), M(-0.18f, -0.08f, 0, 1, 1, 1), mid);
    P(t3_box_geometry_new(0.12f, 0.16f, 0.12f, 1, 1, 1), M(0.18f, -0.08f, 0, 1, 1, 1), mid);
    break;
  }
#undef P
  return merge(p, n);
}
static void view_power_init(void) {
  for (int t = 0; t < PU_KINDS; t++) {
    t3_material *m = mat(PUC[t][0], PUC[t][1], PUC[t][2], 0.3f, 0.5f, true);
    m->emissive = lin(PUC[t][0], PUC[t][1], PUC[t][2]);
    m->emissive_intensity = 1.2f;
    V.power[t] = imesh(sim_power_name(t), power_model(t), m, 8, true);
  }
  { /* the glow ring under each lying power-up */
    t3_material *rm = t3_mesh_basic_material_new(0xffffff);
    rm->blending = T3_ADDITIVE_BLENDING, rm->transparent = true, rm->depth_write = false, rm->side = T3_DOUBLE_SIDE;
    t3_geometry *g = t3_ring_geometry_new(0.55f, 0.85f, 32, 1);
    t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1 } });
    V.power_glow = imesh("PowerGlow", g, rm, 64, false);
  }
  V.grenade = imesh("Grenades", t3_sphere_geometry_new(0.14f, 10, 8), glow(0.5f, 1, 0.35f, 2), 64, false);
  V.rail = imesh("RailShots", t3_box_geometry_new(0.06f, 0.06f, 3.0f, 1, 1, 1), glow(0.4f, 0.8f, 1, 14), 128, false);
  { /* flamer shots: soft additive balls of fire, deep orange */
    t3_material *fm = t3_mesh_basic_material_new(0xffffff);
    fm->color = lin(1.0f, 0.38f, 0.06f);
    fm->blending = T3_ADDITIVE_BLENDING, fm->transparent = true, fm->depth_write = false;
    V.flame = imesh("FlameShots", t3_icosahedron_geometry_new(0.16f, 1), fm, 512, false);
  }
  {
    t3_material *sm = mat(0.85f, 0.88f, 0.95f, 0.25f, 0.9f, false);
    sm->emissive = lin(0.9f, 0.9f, 1.0f), sm->emissive_intensity = 0.6f;
    V.saw = imesh("Saws", power_model(PU_SAWS), sm, 2 * MAXP, true);
  }
  V.mine = imesh("Mines", power_model(PU_MINES), mat(0.6f, 0.6f, 0.2f, 0.4f, 0.6f, true), MAXM, true);
  for (int i = 0; i < MAXP; i++) { /* a bubble round a shielded player */
    t3_material *bm = t3_mesh_basic_material_new(0xffffff);
    bm->color = lin(0.25f, 0.5f, 1.0f);
    bm->blending = T3_ADDITIVE_BLENDING, bm->transparent = true, bm->depth_write = false, bm->opacity = 0.35f;
    t3_mesh *m = t3_mesh_new(t3_icosahedron_geometry_new(1.1f, 2), bm);
    m->base.visible = false;
    m->base.position = (t3_vec3){ 0, 0.9f, 0 };
    t3_object_add(V.pl[i], m);
    V.shield[i] = m;
    t3_release(m), t3_release(bm);
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
  /* the yard floor is baked: every light that stands still (the sun, the rim, the floodlights) and the shadows
   * of the scenery are in its lightmap, and it draws unlit from it (view_lightmap) */
  t3_material *gm = t3_mesh_standard_material_new(0xffffff);
  gm->vertex_colors = true;
  pbr_apply(gm, TX_GROUND, GS / 19.0f, 1.2f);
  t3_mesh *gmesh = t3_mesh_new(pg, gm);
  cw_node gn = cw_node_new("MeshInstance3D", "Ground");
  cw_node_add_child(V.root, gn);
  cw_node_set(gn, "gi_mode", cw_val_enum("STATIC"));
  cw_node_set(gn, "lightmap_uv", cw_val_enum("UV0")); /* the plane's own uv0 is a clean [0, 1] layout: the lightmap lines up on it */
  cw_mesh3d_set_procedural(gn, gmesh);
  t3_release(gmesh), t3_release(pg), t3_release(gm);
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
      { /* the bounding sphere of every frame, scaled by the 10% swell of a hit flash */
        float lo = 1e9f, hi = -1e9f, r2 = 0;
        for (int f = 0; f < V.bn[k]; f++) {
          const t3_attribute *pa = V.ch[k].geo[0][f]->attributes[T3_ATTR_POSITION];
          const float *p = t3_attribute_f32((t3_attribute *)pa);
          for (int v = 0; v < pa->count; v++) lo = CW_MIN(lo, p[v * 3 + 1]), hi = CW_MAX(hi, p[v * 3 + 1]);
        }
        V.bc_y[k] = 0.5f * (lo + hi) * 1.1f;
        for (int f = 0; f < V.bn[k]; f++) {
          const t3_attribute *pa = V.ch[k].geo[0][f]->attributes[T3_ATTR_POSITION];
          const float *p = t3_attribute_f32((t3_attribute *)pa);
          for (int v = 0; v < pa->count; v++) {
            float dx = p[v * 3], dy = p[v * 3 + 1] * 1.1f - V.bc_y[k], dz = p[v * 3 + 2];
            r2 = CW_MAX(r2, dx * dx + dy * dy + dz * dz);
          }
        }
        V.br[k] = sqrtf(r2) * 1.1f + 0.05f;
      }
      { /* the crowd's baked colours desaturated toward a cool grey of the same luminance (each vertex: no hue is
         * swung, so a red hide greys instead of turning magenta); the green kind keeps most of its green to stay readable */
        static const float DESAT[EK_KINDS] = { 0.78f, 0.7f, 0.2f, 0.7f }, COOL[3] = { 0.88f, 1.0f, 1.14f };
        for (int f = 0; f < V.bn[k]; f++) {
          t3_attribute *ca = V.ch[k].geo[0][f]->attributes[T3_ATTR_COLOR];
          float *cv = t3_attribute_f32(ca);
          for (int v = 0; v < ca->count; v++) {
            float *p = cv + v * 3;
            if (p[0] > 1.5f || p[1] > 1.5f || p[2] > 1.5f) continue; /* glowing eyes and the like */
            float l = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
            for (int c = 0; c < 3; c++) p[c] += DESAT[k] * (l * COOL[c] - p[c]);
          }
          ca->version++;
        }
      }
      int bc = k == EK_BOSS ? 4 : cap[k] / V.bn[k] * 3 / 2 + 48;
      /* the crowd is small on screen and thousands strong: Lambert (diffuse only, no normal or roughness map),
       * its look in its baked vertex colours; the players keep the full PBR model */
      t3_material *m = t3_mesh_lambert_material_new(0xffffff);
      m->vertex_colors = true;
      (void)rough, (void)rep_;
      for (int f = 0; f < V.bn[k]; f++) {
        t3_retain(V.ch[k].geo[0][f]);
        V.eb[k][f] = imesh(nm[k], V.ch[k].geo[0][f], t3_retain(m), bc, false); /* imesh takes both references */
        V.eb[k][f]->mesh.base.receive_shadow = false;
        (void)shadow;
      }
      t3_release(m);
    }
  }
  V.bullet = imesh("Bullets", t3_box_geometry_new(0.09f, 0.09f, 1.2f, 1, 1, 1), glow(1, 0.82f, 0.35f, 9), MAXB, false); /* a long hot tracer */
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
    V.pl_mat[i] = bm;
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
      cw_node_set(l, "range", cw_val_float(3.5f));
      cw_node_set(l, "decay", cw_val_float(2));
      cw_node_set(l, "fog_energy", cw_val_float(0));
      cw_node3d_set_position(l, (t3_vec3){ 0, -60, 0 });
      V.pl_light[i] = l;
    }
    t3_release(mr), t3_release(ring), t3_release(bm), t3_release(tm), t3_release(rm);
    t3_release(g);
  }
  view_power_init(); /* after the players: the shield bubble hangs on each */
  view_shafts();
  V.shadows = cw_splats_new(V.root, "Shadows", CW_SPLATS_SHADOW, MAXE + MAXP + MAXO);
  V.pools = cw_splats_new(V.root, "LightPools", CW_SPLATS_LIGHT, 96);
  { /* the damp asphalt's sheen: streaks of light whose texture breaks up into wet patches and dry gaps */
    V.sheen = cw_splats_new(V.root, "Sheen", CW_SPLATS_LIGHT, 16);
    enum { SN = 128 };
    static uint8_t px[SN * SN * 4];
    for (int y = 0; y < SN; y++)
      for (int x = 0; x < SN; x++) {
        float u = ((float)x + 0.5f) / SN * 2 - 1, v = ((float)y + 0.5f) / SN * 2 - 1, r = sqrtf(u * u + v * v);
        float fall = r >= 1 ? 0 : (1 - r * r) * (1 - r * r);
        float wet = fbm((float)x / SN, (float)y / SN, 4, 4, 6161u);
        wet = CW_CLAMP((wet - 0.38f) * 3.2f, 0.0f, 1.0f);
        uint8_t c = (uint8_t)CW_CLAMP((int)(fall * (0.15f + 0.85f * wet) * 255.0f), 0, 255);
        uint8_t *p = px + (y * SN + x) * 4;
        p[0] = p[1] = p[2] = c, p[3] = 255;
      }
    t3_texture *t = t3_texture_new(SN, SN, px);
    t->wrap_s = t->wrap_t = T3_CLAMP_TO_EDGE;
    t->flip_y = false;
    t->generate_mipmaps = true;
    t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
    t->mag_filter = T3_LINEAR;
    t->color_space = T3_NO_COLOR_SPACE;
    cw_splats_set_texture(V.sheen, t);
    t3_release(t);
  }
  V.sun_x = 0.6f, V.sun_z = 0.8f, V.sun_len = 1.5f, V.sun_yaw = cw_atan2(0.6f, 0.8f);
}

void view_set_sun(t3_vec3 d) { /* d: where the sunlight travels */
  float h = sqrtf(d.x * d.x + d.z * d.z);
  if (h < 1e-4f) return;
  V.sun_x = d.x / h, V.sun_z = d.z / h;
  V.sun_yaw = cw_atan2(V.sun_x, V.sun_z);
  V.sun_len = CW_CLAMP(h / CW_MAX(-d.y, 0.05f), 0.2f, 3.0f); /* shadow length of a 1 m caster */
}

/* a blob shadow for a caster at (x, z) of radius r and height ht: it starts under its feet and runs away from
 * the sun as far as a low sun throws it, fading along */
static void shadow_at(float x, float z, float r, float ht, float strength) {
  float len = 2 * r + ht * V.sun_len * 0.35f;
  float yaw = V.sun_yaw;
  cw_splats_add(V.shadows, (t3_vec3){ x, 0, z }, len, 2.0f * r, yaw, (len - 2 * r) / len, (t3_color){ 1, 1, 1 }, strength);
}

/* ── baked lighting ─────────────────────────────────────────────────────
 * The yard floor's light is baked (cartwheel/lightmap.h): the sun, the rim and the six floodlights, their shadows
 * from the walls, masts and containers, the dusk sky, one bounce. The floor then draws unlit from its lightmap
 * (LightmapGI shading BAKED): a few texture reads a pixel however many lights there are. The lights stay real
 * time for everything that moves. The bake ships as lightmaps/yard.cwlm; HORDE_BAKE=<file> (native,
 * development) bakes it again from the yard as built and writes it there. */
static cw_lightmap_data *LM;

/* ── light from the floor ──────────────────────────────────────────────
 * The floodlights are in the bake only (no real-time cost). Everything that moves still walks through their
 * pools and the walls' shadows: each takes the baked floor's light under it (cw_lightmap_irradiance_below) as a
 * colour multiplier over what the real-time sun, rim and sky give it, measured against open ground far from
 * any floodlight. The floor's light is read once into a half-metre grid; a lookup a frame per enemy is a bilinear
 * read of it. */
enum { GLN = 200 };
static const float GL_LO = -50.0f, GL_STEP = 0.5f;
static t3_color GLG[GLN][GLN];
static bool gl_ready;
static t3_color ground_light(float x, float z) {
  if (!gl_ready) return (t3_color){ 1, 1, 1 };
  float gx = (x - GL_LO) / GL_STEP, gz = (z - GL_LO) / GL_STEP;
  int x0 = CW_CLAMP((int)floorf(gx), 0, GLN - 2), z0 = CW_CLAMP((int)floorf(gz), 0, GLN - 2);
  float fx = CW_CLAMP(gx - (float)x0, 0.0f, 1.0f), fz = CW_CLAMP(gz - (float)z0, 0.0f, 1.0f);
  const t3_color a = GLG[z0][x0], b = GLG[z0][x0 + 1], c = GLG[z0 + 1][x0], d = GLG[z0 + 1][x0 + 1];
  t3_color o;
  o.r = (a.r * (1 - fx) + b.r * fx) * (1 - fz) + (c.r * (1 - fx) + d.r * fx) * fz;
  o.g = (a.g * (1 - fx) + b.g * fx) * (1 - fz) + (c.g * (1 - fx) + d.g * fx) * fz;
  o.b = (a.b * (1 - fx) + b.b * fx) * (1 - fz) + (c.b * (1 - fx) + d.b * fx) * fz;
  return o;
}
static void ground_light_build(void) {
  t3_color open;
  if (!cw_lightmap_irradiance_below(LM, (t3_vec3){ 66, 1, -66 }, &open)) return; /* open ground, 90 m from any floodlight */
  float on = CW_MAX((open.r + open.g + open.b) / 3.0f, 1e-4f), po = 0;
  { t3_color pop; if (cw_lightmap_probe_irradiance(LM, (t3_vec3){ 66, 1, -66 }, (t3_vec3){ 0, 0.88f, 0.47f }, &pop)) po = CW_MAX((pop.r + pop.g + pop.b) / 3.0f, 1e-4f); }
  for (int z = 0; z < GLN; z++)
    for (int x = 0; x < GLN; x++) {
      t3_color e;
      t3_vec3 at = { GL_LO + (float)x * GL_STEP, 1, GL_LO + (float)z * GL_STEP };
      cw_lightmap_irradiance_below(LM, at, &e);
      /* and the probe grid at chest height, on the side the camera sees (it looks down 62 degrees from the south): a
       * floodlight's light on the crowd from where the lamp stands, half of the tint */
      t3_color pe;
      if (cw_lightmap_probe_irradiance(LM, at, (t3_vec3){ 0, 0.88f, 0.47f }, &pe) && po > 0)
        e = (t3_color){ 0.5f * e.r + 0.5f * pe.r * on / po, 0.5f * e.g + 0.5f * pe.g * on / po, 0.5f * e.b + 0.5f * pe.b * on / po };
      /* against the open ground's brightness (its warm dusk colour is what the real-time sun already gives) */
      /* half its hue: the floodlights' cool white and the sun's warmth show on the crowd without turning it blue */
      float l = (e.r + e.g + e.b) / 3.0f;
      GLG[z][x] = (t3_color){ CW_CLAMP((l + 0.35f * (e.r - l)) / on, 0.3f, 2.2f), CW_CLAMP((l + 0.35f * (e.g - l)) / on, 0.3f, 2.2f),
                              CW_CLAMP((l + 0.35f * (e.b - l)) / on, 0.3f, 2.2f) };
    }
  gl_ready = true;
  /* the scenery stands still: its light from the floor is set once (a little toward the yard, off its own
   * footprint, which the bake leaves dark) */
  for (int k = 0; k < nprops; k++) {
    for (int i = 0; i < PROPS[k].n; i++) {
      float x = PROPS[k].pl[i].x, z = PROPS[k].pl[i].z, l = sqrtf(x * x + z * z);
      if (l > 1) x -= x / l * 1.6f, z -= z / l * 1.6f;
      t3_instanced_mesh_set_color_at(PROPS[k].im, i, ground_light(x, z));
    }
    PROPS[k].im->instance_color->version++;
  }
}
const cw_lightmap_data *view_lightmap_data(void) { return LM; }

/* ── puddles ───────────────────────────────────────────────────────────
 * The yard is wet in patches, not all over: dark water lying in the low spots, each reflecting the baked
 * reflection cube (the dusk sky, the walls and the floodlight heads round the yard) for one texture read, where
 * screen-space reflections cost a G-buffer and a trace every frame. Six shapes (noise-bent blobs) in one
 * atlas, each puddle a shape turned, flipped and stretched by its own seed. */
enum { PUD_SHAPES = 6, PUD_TEX = 512, PUDDLES = 54 };
static t3_instanced_mesh *PUD[PUD_SHAPES];
static struct { float x, z, rx, rz, yaw; } PUDL[PUDDLES]; /* where each lies, for the floodlights' glints */
static t3_texture *puddle_atlas(void) {
  static uint8_t px[PUD_TEX * PUD_TEX * 4];
  const int CW_ = PUD_TEX / 3, CH_ = PUD_TEX / 2;
  for (int y = 0; y < PUD_TEX; y++)
    for (int x = 0; x < PUD_TEX; x++) {
      int cx = x / CW_, cy = y / CH_, k = cy * 3 + cx;
      float u = ((float)(x % CW_) + 0.5f) / (float)CW_ * 2 - 1, v = ((float)(y % CH_) + 0.5f) / (float)CH_ * 2 - 1;
      uint32_t sd = 7000u + (uint32_t)k * 131u;
      /* a blob whose edge the noise pushes in and out, a second lobe on some */
      float n = fbm(u * 0.5f + 0.5f, v * 0.5f + 0.5f, 3, 4, sd);
      float r = sqrtf(u * u + v * v) * (0.8f + 0.55f * n);
      if (k & 1) { float u2 = u - 0.45f, v2 = v + 0.3f; r = CW_MIN(r, sqrtf(u2 * u2 + v2 * v2) * 1.5f * (0.8f + 0.5f * n)); }
      float a = 1.0f - CW_CLAMP((r - 0.62f) / 0.1f, 0.0f, 1.0f); /* water has a crisp edge */
      a = a * a * (3 - 2 * a);
      if (x % CW_ < 2 || y % CH_ < 2 || x % CW_ >= CW_ - 2 || y % CH_ >= CH_ - 2) a = 0; /* no bleed across cells */
      uint8_t *p = px + (y * PUD_TEX + x) * 4;
      p[0] = p[1] = p[2] = 255;
      p[3] = (uint8_t)(a * 255.0f + 0.5f);
    }
  t3_texture *t = t3_texture_new(PUD_TEX, PUD_TEX, px);
  t->wrap_s = t->wrap_t = T3_CLAMP_TO_EDGE;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->color_space = T3_SRGB_COLOR_SPACE;
  return t;
}
static void view_puddles(void) {
  t3_texture *atlas = puddle_atlas();
  int placed[PUD_SHAPES] = { 0 };
  for (int k = 0; k < PUD_SHAPES; k++) {
    t3_geometry *g = t3_plane_geometry_new(1, 1, 1, 1);
    t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1 } });
    { /* this shape's cell of the atlas */
      t3_attribute *ua = g->attributes[T3_ATTR_UV];
      float *uv = (float *)ua->array;
      for (int i = 0; i < ua->count; i++) {
        uv[i * 2] = ((float)(k % 3) + 0.02f + 0.96f * uv[i * 2]) / 3.0f;
        uv[i * 2 + 1] = ((float)(k / 3) + 0.02f + 0.96f * uv[i * 2 + 1]) / 2.0f;
      }
      ua->version++;
    }
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->color = lin(0.05f, 0.05f, 0.055f); /* standing water: the asphalt under it darker, the sky faint in it */
    m->opacity = 0.5f;
    m->transparent = true;
    m->depth_write = false;
    t3_material_set_texture(m, T3_MAP, atlas);
    cw_lightmap_use_reflection(LM, m, 0.7f);
    PUD[k] = imesh("Puddles", g, m, PUDDLES, false);
    PUD[k]->mesh.base.receive_shadow = false;
    PUD[k]->mesh.base.render_order = -3; /* under the blob shadows and the light pools */
  }
  t3_release(atlas);
  for (int i = 0; i < PUDDLES; i++) {
    uint32_t h = hash2((uint32_t)i, 9191u);
    float x = (h01((uint32_t)i, 9192u) * 2 - 1) * (HALF - 3), z = (h01((uint32_t)i, 9193u) * 2 - 1) * (HALF - 3);
    int k = (int)(h % PUD_SHAPES);
    float sx = 1.3f + 2.6f * h01((uint32_t)i, 9194u), sz = sx * (0.55f + 0.7f * h01((uint32_t)i, 9195u));
    if (h & 0x100) sx = -sx; /* flipped */
    float yaw = h01((uint32_t)i, 9196u) * 6.2832f;
    put(PUD[k], placed[k]++, x, 0.012f, z, yaw, sx, 1, sz);
    PUDL[i].x = x, PUDL[i].z = z, PUDL[i].rx = fabsf(sx) * 0.38f, PUDL[i].rz = sz * 0.38f, PUDL[i].yaw = yaw;
  }
  for (int k = 0; k < PUD_SHAPES; k++) PUD[k]->count = placed[k], touch(PUD[k], false);
}

/* ── light shafts ──────────────────────────────────────────────────────
 * The haze under each floodlight as a soft card along the beam, turned about the beam's axis to face the camera
 * each frame (the volumetric fog's beams for the price of six additive quads): bright at the lamp, fading
 * down the beam and across it. */
enum { SHAFT_TEX = 128 };
static t3_mesh *SHAFT[LAMPS];
static t3_vec3 shaft_dir[LAMPS];
static t3_texture *shaft_texture(void) {
  static uint8_t px[SHAFT_TEX * SHAFT_TEX * 4];
  for (int y = 0; y < SHAFT_TEX; y++)
    for (int x = 0; x < SHAFT_TEX; x++) {
      float u = ((float)x + 0.5f) / SHAFT_TEX * 2 - 1, t = ((float)y + 0.5f) / SHAFT_TEX; /* across, along (0 at the lamp) */
      float w = 0.12f + 0.88f * t; /* the beam widens away from the lamp */
      float across = CW_CLAMP(1.0f - fabsf(u) / w, 0.0f, 1.0f);
      across = across * across * (3 - 2 * across);
      float along = cw_pow(1.0f - t, 1.6f) * CW_CLAMP(t * 12.0f, 0.0f, 1.0f);
      float n = 0.8f + 0.4f * fbm((float)x / SHAFT_TEX, t * 0.4f, 6, 2, 4321u); /* dust in the beam */
      uint8_t c = (uint8_t)CW_CLAMP((int)(across * along * n * 255.0f), 0, 255);
      uint8_t *p = px + (y * SHAFT_TEX + x) * 4;
      p[0] = p[1] = p[2] = c;
      p[3] = 255;
    }
  t3_texture *tx = t3_texture_new(SHAFT_TEX, SHAFT_TEX, px);
  tx->wrap_s = tx->wrap_t = T3_CLAMP_TO_EDGE;
  tx->flip_y = false;
  tx->generate_mipmaps = true;
  tx->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  tx->mag_filter = T3_LINEAR;
  tx->color_space = T3_NO_COLOR_SPACE;
  return tx;
}
static void view_shafts(void) {
  t3_texture *tx = shaft_texture();
  for (int i = 0; i < LAMPS; i++) {
    t3_geometry *g = t3_plane_geometry_new(1, 1, 1, 1); /* x across, y along (top = the lamp) */
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->color = (t3_color){ 0.72f * 0.22f, 0.86f * 0.22f, 1.0f * 0.22f }; /* the floodlight's cool white, faint */
    m->blending = T3_ADDITIVE_BLENDING;
    m->transparent = true;
    m->depth_write = false;
    m->side = T3_DOUBLE_SIDE;
    m->fog = false;
    t3_material_set_texture(m, T3_MAP, tx);
    t3_mesh *ms = t3_mesh_new(g, m);
    ms->base.cast_shadow = ms->base.receive_shadow = false;
    cw_node n = cw_node_new("Node3D", "Shaft");
    cw_node_add_child(V.root, n);
    t3_object *w = t3_group_new();
    t3_object_add(w, ms);
    cw_node3d_set_object(n, w);
    t3_release(w), t3_release(g), t3_release(m);
    SHAFT[i] = ms;
    t3_release(ms);
    t3_vec3 tg = lamp_aim[i];
    shaft_dir[i] = t3_vec3_normalize((t3_vec3){ tg.x - lamp_head[i].x, tg.y - lamp_head[i].y, tg.z - lamp_head[i].z });
  }
  t3_release(tx);
}
/* each card along its beam, turned about the beam to face the camera */
static void shafts_face(t3_vec3 eye) {
  const float LEN = 13.0f, WIDE = 5.5f;
  for (int i = 0; i < LAMPS; i++) {
    if (!SHAFT[i]) continue;
    t3_vec3 a = shaft_dir[i], c = { lamp_head[i].x + a.x * LEN * 0.5f, lamp_head[i].y + a.y * LEN * 0.5f, lamp_head[i].z + a.z * LEN * 0.5f };
    t3_vec3 v = t3_vec3_normalize((t3_vec3){ eye.x - c.x, eye.y - c.y, eye.z - c.z });
    t3_vec3 x = t3_vec3_normalize(t3_vec3_cross(a, v)), y = (t3_vec3){ -a.x, -a.y, -a.z }, z = t3_vec3_cross(x, y);
    t3_mat4 *m = &SHAFT[i]->base.matrix;
    *m = (t3_mat4){ { x.x * WIDE, x.y * WIDE, x.z * WIDE, 0, y.x * LEN, y.y * LEN, y.z * LEN, 0, z.x, z.y, z.z, 0, c.x, c.y, c.z, 1 } };
    SHAFT[i]->base.matrix_auto_update = false;
    SHAFT[i]->base.matrix_world_needs_update = true;
  }
}

bool view_lightmap(cw_tree *t) {
  if (LM) return true;
  cw_node root = cw_tree_root(t);
  const char *out = getenv("HORDE_BAKE");
  if (out && *out) {
    cw_lightmap_settings s = cw_lightmap_settings_default();
    s.texel_density = 7;
    s.max_size = 1024;
    s.samples = 48;
    s.bounces = 1;
    s.probe_spacing = 2.5f; /* the SH probe grid: the floodlights at body height, for the crowd (ground_light_build) */
    s.probe_samples = 64;
    s.denoise = 2;
    s.direct_samples = 12;
    s.shadow_softness = 0.025f;
    s.reflection_size = 32;
    s.reflection_origin[0] = 0, s.reflection_origin[1] = 1.2f, s.reflection_origin[2] = 0;
    LM = cw_lightmap_bake(t, root, &s);
    if (!LM) return false;
    if (!cw_lightmap_save(LM, out)) cw_error("horde: could not write the bake to %s", out);
    cw_lightmap_info info;
    cw_lightmap_get_info(LM, &info);
    fprintf(stderr, "horde: baked %s: %d meshes, %d texels, %lld rays, hash %016llx\n", out, info.meshes, info.texels, info.rays,
            (unsigned long long)info.hash);
  } else LM = cw_lightmap_load("lightmaps/yard.cwlm");
  if (!LM) {
    cw_error("horde: no yard lightmap (lightmaps/yard.cwlm): the floor draws unlit and dark");
    return false;
  }
  cw_lightmap_apply_shaded(t, root, LM, 1, CW_LIGHTMAP_BAKED);
  ground_light_build();
  view_puddles();
  return true;
}

void view_free(void) { /* LM stays: the floor's lightmap textures read its texels */
  for (int k = 0; k < EK_KINDS; k++) chr_free(&V.ch[k]);
  chr_free(&V.soldier);
  for (int i = 0; i < TX_KINDS; i++) {
    if (!PB[i].map) continue;
    t3_release(PB[i].map), t3_release(PB[i].normal), t3_release(PB[i].rough);
    free(PB[i].px[0]), free(PB[i].px[1]), free(PB[i].px[2]);
  }
  memset(PB, 0, sizeof PB);
  for (int k = 0; k < nrc; k++)
    for (int i = 0; i < 3; i++)
      if (rc[k].t[i]) t3_release(rc[k].t[i]);
  memset(rc, 0, sizeof rc);
  nrc = 0;
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

/* the camera view_camera placed this frame, as its four side planes (the near and far planes cut
 * nothing in the yard) */
typedef struct view_frustum { bool ok; float n[4][3], d[4]; } view_frustum;
static view_frustum view_frustum_now(void) {
  view_frustum v = { 0 };
  t3_vec3 f = { V.at.x - V.eye.x, V.at.y - V.eye.y, V.at.z - V.eye.z };
  float fl = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
  if (fl < 1e-4f || V.aspect <= 0) return v;
  f.x /= fl, f.y /= fl, f.z /= fl;
  t3_vec3 r = { -f.z, 0, f.x };
  float rl = sqrtf(r.x * r.x + r.z * r.z);
  r.x /= rl, r.z /= rl;
  t3_vec3 u = { r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };
  float ty = tanf(38.0f * 3.14159265f / 360.0f), tx = ty * V.aspect;
  /* inward normals: f * t +- side (normalised) */
  const t3_vec3 side[4] = { r, { -r.x, -r.y, -r.z }, u, { -u.x, -u.y, -u.z } };
  const float t[4] = { tx, tx, ty, ty };
  for (int k = 0; k < 4; k++) {
    float nx = f.x * t[k] - side[k].x, ny = f.y * t[k] - side[k].y, nz = f.z * t[k] - side[k].z;
    float nl = sqrtf(nx * nx + ny * ny + nz * nz);
    v.n[k][0] = nx / nl, v.n[k][1] = ny / nl, v.n[k][2] = nz / nl;
    v.d[k] = -(v.n[k][0] * V.eye.x + v.n[k][1] * V.eye.y + v.n[k][2] * V.eye.z);
  }
  v.ok = true;
  return v;
}
static bool view_frustum_sphere(const view_frustum *v, float x, float y, float z, float r) {
  for (int k = 0; k < 4; k++)
    if (v->n[k][0] * x + v->n[k][1] * y + v->n[k][2] * z + v->d[k] < -r) return false;
  return true;
}

void view_sync(const sim *s, float time) {
  if (!V.root.id) return;
  int cnt[EK_KINDS][CH_MAXF];
  memset(cnt, 0, sizeof cnt);
  cw_splats_begin(V.shadows);
  static const float rate[EK_KINDS] = { 1.9f, 0.9f, 1.1f, 0.6f }; /* cycles per second */
  /* The horde outside the view is not drawn. The instanced meshes have one bounding sphere for the whole
   * yard, so three.c draws every instance; at a late wave about half of them are off screen and still cost
   * the vertex work of the colour pass and of the SSR G-buffer pass. Only the kinds that cast no shadow are
   * culled (a brute or the boss off screen can throw its shadow into the view); nothing else reads them
   * (SSR is screen space, the fog reads the shadow map). HORDE_CULL=0 (development) draws them all. */
  static const bool cull_kind[EK_KINDS] = { true, true, true, true }; /* nothing in the crowd casts a real-time shadow now (blob shadows) */
  static int cull_on = -1;
  if (cull_on < 0) cull_on = !getenv("HORDE_CULL") || atoi(getenv("HORDE_CULL")) != 0;
  view_frustum fr_ = view_frustum_now();
  for (int i = 0; i < MAXE; i++) {
    const enemy *e = &s->e[i];
    if (!e->alive) continue;
    if (cull_on && fr_.ok && cull_kind[e->kind] && !view_frustum_sphere(&fr_, e->x, V.bc_y[e->kind], e->y, V.br[e->kind])) continue;
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
    static const float SR[EK_KINDS] = { 0.42f, 0.75f, 0.62f, 1.5f }, SH[EK_KINDS] = { 1.6f, 2.2f, 1.5f, 4.0f };
    shadow_at(e->x, e->y, SR[k] * sq, SH[k], 0.62f);
    /* each enemy its own hide from its kind's palette (sallow, ashen, olive, rust, bruised, ochre), its brightness
     * varied too: a crowd of many, each kind still read by its shape and its baked colours */
    static const float HIDE[EK_KINDS][6][3] = {
      { { 0.95f, 0.86f, 0.74f }, { 0.78f, 0.8f, 0.84f }, { 0.78f, 0.86f, 0.7f }, { 0.98f, 0.74f, 0.62f }, { 0.84f, 0.74f, 0.86f }, { 0.96f, 0.84f, 0.6f } },
      { { 0.82f, 0.78f, 0.74f }, { 0.7f, 0.72f, 0.78f }, { 0.86f, 0.72f, 0.62f }, { 0.74f, 0.78f, 0.68f }, { 0.9f, 0.82f, 0.72f }, { 0.78f, 0.72f, 0.8f } },
      { { 0.9f, 0.95f, 0.78f }, { 1.0f, 0.92f, 0.7f }, { 0.82f, 0.92f, 0.8f }, { 0.95f, 0.86f, 0.74f }, { 0.86f, 0.9f, 0.7f }, { 1.0f, 0.95f, 0.82f } },
      { { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 } },
    };
    const float *hide = HIDE[k][hash2((uint32_t)i, 77u) % 6];
    float tint = 0.39f + 0.2f * h01((uint32_t)i, 5);
    /* the hides over the greyed baked colours: the variety stays */
    t3_color c = e->flash > 0 ? (t3_color){ 4, 2.8f, 2.2f }
                              : (t3_color){ tint * (0.35f * hide[0] + 0.65f * 0.8f), tint * (0.35f * hide[1] + 0.65f * 0.8f), tint * (0.35f * hide[2] + 0.65f * 0.86f) };
    {
      t3_color gl = ground_light(e->x, e->y);
      c.r *= gl.r, c.g *= gl.g, c.b *= gl.b;
    }
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
  int nb = 0, nr = 0, ngr = 0, nrl = 0, nfl = 0;
  for (int i = 0; i < MAXB; i++) {
    const bullet *b = &s->b[i];
    if (!b->alive) continue;
    float yaw = cw_atan2(-b->vx, -b->vy);
    if (b->kind == 2) { /* a grenade in its arc */
      float t = 1.0f - (float)b->life / 36.0f;
      if (ngr < V.grenade->instance_matrix->count) put(V.grenade, ngr++, b->x, 0.9f + 5.0f * t * (1 - t) * 1.6f, b->y, (float)b->life * 0.4f, 1, 1, 1);
      continue;
    }
    if (b->kind == 3) {
      if (nrl < V.rail->instance_matrix->count) put(V.rail, nrl++, b->x, 0.9f, b->y, yaw, 1, 1, 1);
      continue;
    }
    if (b->kind == 4) { /* a ball of flame that grows as it slows */
      float k = 0.6f + 1.6f * (1.0f - (float)b->life / 22.0f);
      if (nfl < V.flame->instance_matrix->count) put(V.flame, nfl++, b->x, 0.8f + 0.3f * (1.0f - (float)b->life / 22.0f), b->y, (float)(i * 7 + b->life), k, k, k);
      continue;
    }
    t3_instanced_mesh *im = b->kind ? V.rocket : V.bullet;
    int *c = b->kind ? &nr : &nb;
    if (*c >= im->instance_matrix->count) continue;
    if (b->kind) {
      t3_mat4 m;
      t3_quat qy = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) }, qx = { 0.70710678f, 0, 0, 0.70710678f };
      t3_quat q = t3_quat_multiply(qy, qx);
      t3_mat4_compose(&m, (t3_vec3){ b->x, 0.9f, b->y }, q, (t3_vec3){ 1, 1, 1 });
      t3_instanced_mesh_set_matrix_at(im, *c, &m);
    } else put(im, *c, b->x, 0.9f, b->y, yaw, 1, 1, 1 + (b->dmg > 2 ? 0.4f : 0));
    (*c)++;
  }
  V.bullet->count = nb, V.rocket->count = nr, V.grenade->count = ngr, V.rail->count = nrl, V.flame->count = nfl;
  touch(V.bullet, false), touch(V.rocket, false), touch(V.grenade, false), touch(V.rail, false), touch(V.flame, false);
  V.st.bullets = nb + nr + ngr + nrl + nfl;
  { /* mines */
    int nm = 0;
    for (int j = 0; j < MAXM; j++)
      if (s->m[j].alive) put(V.mine, nm++, s->m[j].x, 0.05f, s->m[j].y, 0, 1, s->m[j].arm > 0 ? 0.6f : 1.0f, 1);
    V.mine->count = nm;
    touch(V.mine, false);
  }
  { /* saws round their players */
    int nsw = 0;
    for (int k = 0; k < MAXP; k++) {
      const player *p = &s->p[k];
      if (!p->active || p->down || p->saw_t <= 0) continue;
      for (int j = 0; j < 2; j++) {
        float a = p->saw_a + (float)j * 3.14159265f;
        put(V.saw, nsw++, p->x + cw_cos(a) * 2.4f, 0.7f, p->y + cw_sin(a) * 2.4f, time * 25.0f, 1.3f, 1.3f, 1.3f);
      }
    }
    V.saw->count = nsw;
    touch(V.saw, false);
  }
  /* pickups */
  int ng[3] = { 0, 0, 0 }, nh = 0, np_[PU_KINDS] = { 0 }, npg = 0;
  for (int i = 0; i < MAXG; i++) {
    const pickup *g = &s->g[i];
    if (!g->alive) continue;
    float blink = g->life < 180 && ((g->life / 6) & 1) ? 0 : 1;
    if (g->kind == 0) {
      int t = g->value >= 20 ? 2 : g->value >= 5 ? 1 : 0;
      if (ng[t] >= V.gem[t]->instance_matrix->count) continue;
      float sc = blink * (t == 2 ? 1.9f : t == 1 ? 1.4f : 1.0f);
      put(V.gem[t], ng[t]++, g->x, 0.45f + 0.12f * cw_sin(time * 3.2f + (float)i * 0.7f), g->y, time * 1.6f + (float)i, sc, sc, sc);
    } else if (g->kind == PK_POWER) {
      int t = g->value % PU_KINDS;
      if (np_[t] >= V.power[t]->instance_matrix->count || npg >= V.power_glow->instance_matrix->count) continue;
      float pulse = 1.0f + 0.12f * cw_sin(time * 5.0f + (float)i);
      put(V.power[t], np_[t]++, g->x, 0.85f + 0.15f * cw_sin(time * 2.5f + (float)i), g->y, time * 1.4f + (float)i, 1.7f * blink, 1.7f * blink, 1.7f * blink);
      put(V.power_glow, npg, g->x, 0.04f, g->y, 0, pulse * blink, 1, pulse * blink);
      t3_instanced_mesh_set_color_at(V.power_glow, npg++, lin(PUC[t][0], PUC[t][1], PUC[t][2]));
    } else if (nh < 64) put(V.health, nh++, g->x, 0.55f + 0.1f * cw_sin(time * 3 + (float)i), g->y, time + (float)i, blink, blink, blink);
  }
  for (int t = 0; t < PU_KINDS; t++) V.power[t]->count = np_[t], touch(V.power[t], false);
  V.power_glow->count = npg;
  touch(V.power_glow, true);
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
      { t3_color gl = ground_light(o->x, o->y); t3_instanced_mesh_set_color_at(V.crate, nc++, (t3_color){ sh * gl.r, sh * gl.g, sh * gl.b }); }
      break;
    case OK_BARREL:
      put(V.barrel, nba, o->x, 0, o->y, 0, 1, 1, 1);
      t3_instanced_mesh_set_color_at(V.barrel, nba++, ground_light(o->x, o->y));
      break;
    case OK_WALL:
      put(V.wall, nw, o->x, 1.1f, o->y, 0, 1, 1, 1);
      { t3_color gl = ground_light(o->x, o->y); t3_instanced_mesh_set_color_at(V.wall, nw++, (t3_color){ sh * gl.r, sh * 0.95f * gl.g, sh * 0.9f * gl.b }); }
      break;
    default:
      if (np < 16) put(V.pillar, np++, o->x, 2.25f, o->y, 0, 1, 1, 1);
    }
  }
  for (int i = 0; i < MAXO; i++) { /* the yard's pieces: their soft contact shadows */
    const obstacle *o = &s->o[i];
    if (!o->alive) continue;
    static const float OR[OK_KINDS] = { 0.8f, 0.55f, 1.2f, 1.35f }, OH[OK_KINDS] = { 1.3f, 1.1f, 2.2f, 4.5f };
    shadow_at(o->x, o->y, OR[o->kind], OH[o->kind], 0.7f);
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
    if (V.pl_mat[i]) V.pl_mat[i]->color = ground_light(p->x, p->y);
    shadow_at(p->x, p->y, 0.42f, 1.8f, 0.7f);
    cw_node_set(V.pl_light[i], "energy", cw_val_float(p->down ? 8.0f : 22.0f));
    if (V.shield[i]) V.shield[i]->base.visible = p->shield_t > 0 && !p->down && (p->shield_t > 90 || (p->shield_t / 6) & 1);
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
  cw_splats_end(V.shadows);
  { /* development (A/B timing): HORDE_HIDE bits hide 1 the crowd, 2 the blob shadows, 4 the light pools */
    static int hide = -1;
    if (hide < 0) hide = getenv("HORDE_HIDE") ? atoi(getenv("HORDE_HIDE")) : 0;
    if (hide & 1) for (int k = 0; k < EK_KINDS; k++) for (int f = 0; f < V.bn[k]; f++) V.eb[k][f]->count = 0;
    if (hide & 2) cw_splats_mesh(V.shadows)->count = 0;
    if (hide & 4) cw_splats_mesh(V.pools)->mesh.base.visible = false;
  }
  /* the floor is baked and takes no real-time light: each moving light (a player's lamp, a muzzle flash, a
   * blast, a burning barrel) shows on it as a pool of added light (the floor's albedo is about 0.16 where the lights fall) */
  cw_splats_begin(V.pools);
  cw_splats_begin(V.sheen);
  cw_splats_add_lights(V.pools, cw_node_parent(V.root), 0, 0.1f / 3.14159265f);
  /* the wet asphalt's sheen: each floodlight's blurred mirror image on the ground, where the eye sees it reflected,
   * a long soft streak toward the camera that slides as the camera moves (what the floodlights' specular on a wet
   * floor showed, for six additive discs) */
  for (int l = 0; l < LAMPS; l++) {
    t3_vec3 m = { lamp_head[l].x, -lamp_head[l].y, lamp_head[l].z };
    float t = V.eye.y / (V.eye.y - m.y);
    float px = V.eye.x + (m.x - V.eye.x) * t, pz = V.eye.z + (m.z - V.eye.z) * t;
    float yaw = cw_atan2(V.eye.x - px, V.eye.z - pz);
    cw_splats_add(V.sheen, (t3_vec3){ px, 0, pz }, 16.0f, 6.0f, yaw, -0.25f, (t3_color){ 0.72f, 0.86f, 1.0f }, 0.22f);
  }
  /* each floodlight's mirror image in the water: where the line from the eye to the lamp's reflection below the
   * ground crosses it, a bright glint stretched toward the camera, if a puddle lies there */
  if (LM)
    for (int l = 0; l < LAMPS + MAXP; l++) { /* the floodlights, then the lamps the players carry */
      bool pl_ = l >= LAMPS;
      if (pl_ && (!s->p[l - LAMPS].active || s->p[l - LAMPS].down)) continue;
      t3_vec3 m = pl_ ? (t3_vec3){ s->p[l - LAMPS].x, -2.4f, s->p[l - LAMPS].y } : (t3_vec3){ lamp_head[l].x, -lamp_head[l].y, lamp_head[l].z };
      float t = V.eye.y / (V.eye.y - m.y);
      float px = V.eye.x + (m.x - V.eye.x) * t, pz = V.eye.z + (m.z - V.eye.z) * t;
      if (pl_) { /* the damp asphalt round a player catches its lamp too: a soft streak toward the camera, everywhere */
        static const float PCS[MAXP][3] = { { 0.2f, 0.7f, 1 }, { 1, 0.55f, 0.1f }, { 0.4f, 0.95f, 0.35f }, { 0.95f, 0.3f, 0.75f } };
        const float *c3 = PCS[l - LAMPS];
        cw_splats_add(V.sheen, (t3_vec3){ px, 0, pz }, 3.2f, 1.4f, cw_atan2(V.eye.x - px, V.eye.z - pz), 0,
                      (t3_color){ 0.55f + 0.45f * c3[0], 0.55f + 0.45f * c3[1], 0.55f + 0.45f * c3[2] }, 0.5f);
      }
      for (int i = 0; i < PUDDLES; i++) {
        float dx = px - PUDL[i].x, dz = pz - PUDL[i].z, c = cw_cos(PUDL[i].yaw), sn = cw_sin(PUDL[i].yaw);
        float lx = (dx * c - dz * sn) / PUDL[i].rx, lz = (dx * sn + dz * c) / PUDL[i].rz, e = lx * lx + lz * lz;
        if (e >= 1) continue;
        float k = (1 - e) * (1 - e);
        float yaw = cw_atan2(V.eye.x - px, V.eye.z - pz);
        static const float PC_[MAXP][3] = { { 0.2f, 0.7f, 1 }, { 1, 0.55f, 0.1f }, { 0.4f, 0.95f, 0.35f }, { 0.95f, 0.3f, 0.75f } };
        t3_color gc = pl_ ? (t3_color){ 0.55f + 0.45f * PC_[l - LAMPS][0], 0.55f + 0.45f * PC_[l - LAMPS][1], 0.55f + 0.45f * PC_[l - LAMPS][2] } : (t3_color){ 0.72f, 0.86f, 1.0f };
        cw_splats_add(V.pools, (t3_vec3){ px, 0, pz }, pl_ ? 1.0f : 1.6f, pl_ ? 0.5f : 0.45f, yaw, 0, gc, (pl_ ? 3.0f : 2.2f) * k);
        break;
      }
    }
  cw_splats_end(V.pools);
  cw_splats_end(V.sheen);
}

void view_camera(const sim *s, cw_node camera, float aspect, float control_x, float control_y) {
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
  t3_vec3 at = { V.cx + control_x, 0, V.cy + control_y }; /* no shake: the offsets are the camera gate's control only */
  t3_vec3 eye = { at.x, cw_sin(tilt) * V.cd, at.z + cw_cos(tilt) * V.cd };
  cw_node3d_set_position(camera, eye);
  cw_node3d_look_at(camera, at, (t3_vec3){ 0, 1, 0 });
  V.eye = eye, V.at = at, V.aspect = aspect;
  shafts_face(eye);
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
