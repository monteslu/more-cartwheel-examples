/* The level's body and picture: Box2D colliders from the tile grid, and the three.c scene (chunk meshes with
 * generated PBR textures, water, lava, spikes, backdrops). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/math.h"
#include "cartwheel/render3d.h"
#include "game.h"

/* ── physics ──────────────────────────────────────────────────────── */

static cw_body2d g_static;
static struct { int x, y; cw_body2d b; } g_crack[64];
int world_cracks = 0;

static bool is_solid_t(int t) { return t == T_ROCK || t == T_GRASS || t == T_BRICK; }

void world_build_physics(game *g) {
  cw_world2d_def wd = cw_world2d_default_def();
  wd.gravity = (cw_vec2){ 0, 0 };
  g->world = cw_world2d_create(&wd);
  const level *L = &g->L;
  cw_body2d_def bd = cw_body2d_default_def();
  bd.type = CW_BODY_STATIC;
  g_static = cw_body2d_create(g->world, &bd);
  cw_shape2d_def sd = cw_shape2d_default_def();
  sd.layer = LAYER_WORLD;
  sd.material.friction = 0;
  cw_shape2d_def od = sd;
  od.layer = LAYER_ONEWAY;
  /* merged runs, row by row */
  for (int y = 0; y < WORLD_H; y++) {
    int x = 0;
    while (x < WORLD_W) {
      int t = L->t[y][x];
      if (is_solid_t(t)) {
        int x1 = x;
        while (x1 < WORLD_W && is_solid_t(L->t[y][x1])) x1++;
        float hw = (float)(x1 - x) * 0.5f;
        cw_shape2d_box(g_static, &sd, (cw_vec2){ hw, 0.5f }, (cw_xform2){ { (float)x + hw, (float)y + 0.5f }, { 1, 0 } });
        x = x1;
      } else if (t == T_ONEWAY) {
        int x1 = x;
        while (x1 < WORLD_W && L->t[y][x1] == T_ONEWAY) x1++;
        float hw = (float)(x1 - x) * 0.5f;
        cw_shape2d_box(g_static, &od, (cw_vec2){ hw, 0.12f }, (cw_xform2){ { (float)x + hw, (float)y + 0.88f }, { 1, 0 } });
        x = x1;
      } else x++;
    }
  }
  /* cracked blocks: one body each so a dash can remove them */
  world_cracks = 0;
  for (int y = 0; y < WORLD_H; y++)
    for (int x = 0; x < WORLD_W; x++)
      if (L->t[y][x] == T_CRACK && world_cracks < 64) {
        cw_body2d_def cd = cw_body2d_default_def();
        cd.type = CW_BODY_STATIC;
        cd.position = (cw_pos2){ (double)x + 0.5, (double)y + 0.5 };
        cw_body2d b = cw_body2d_create(g->world, &cd);
        cw_shape2d_box(b, &sd, (cw_vec2){ 0.5f, 0.5f }, (cw_xform2){ { 0, 0 }, { 1, 0 } });
        g_crack[world_cracks].x = x, g_crack[world_cracks].y = y, g_crack[world_cracks].b = b;
        world_cracks++;
      }
  /* moving platforms: kinematic boxes, driven by sim_step */
  for (int i = 0; i < L->nplat; i++) {
    cw_body2d_def pd = cw_body2d_default_def();
    pd.type = CW_BODY_KINEMATIC;
    pd.position = (cw_pos2){ L->plats[i].x0, L->plats[i].y0 };
    cw_body2d b = cw_body2d_create(g->world, &pd);
    cw_shape2d_box(b, &sd, (cw_vec2){ L->plats[i].w * 0.5f, 0.2f }, (cw_xform2){ { 0, 0 }, { 1, 0 } });
    g->L.plats[i].body = b;
  }
}

void world_destroy_physics(game *g) {
  if (g->world.id) cw_world2d_destroy(g->world);
  g->world = (cw_world2d){ 0 };
  world_cracks = 0;
}

/* a dash broke the cracked block at tile (x, y): true if there was one */
bool world_break_crack(game *g, int x, int y) {
  for (int i = 0; i < world_cracks; i++)
    if (g_crack[i].x == x && g_crack[i].y == y && g_crack[i].b.id) {
      cw_body2d_destroy(g_crack[i].b);
      g_crack[i].b = (cw_body2d){ 0 };
      g->L.t[y][x] = T_AIR;
      return true;
    }
  return false;
}

/* ── procedural PBR textures: tileable value noise, a height field turned into albedo, normal, roughness, occlusion
 * and (where a surface glows) an emission map ── */

#define TN 256
static float hash2(int x, int y, int s, int per) {
  x = ((x % per) + per) % per, y = ((y % per) + per) % per;
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)s * 83492791u;
  h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
  return (float)(h & 0xffff) / 65535.0f;
}
static float vnoise(float u, float v, int per, int s) {
  float x = u * (float)per, y = v * (float)per;
  int xi = (int)floorf(x), yi = (int)floorf(y);
  float fx = x - (float)xi, fy = y - (float)yi;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  float a = hash2(xi, yi, s, per), b = hash2(xi + 1, yi, s, per), c = hash2(xi, yi + 1, s, per), d = hash2(xi + 1, yi + 1, s, per);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float u, float v, int per, int oct, int s) {
  float sum = 0, amp = 0.5f, tot = 0;
  for (int i = 0; i < oct; i++) {
    sum += amp * vnoise(u, v, per << i, s + i), tot += amp, amp *= 0.5f;
  }
  return sum / tot;
}
static float sstep(float a, float b, float x) {
  float t = (x - a) / (b - a);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
typedef struct texel { float r, g, b, h, rough, ao, er, eg, eb; } texel;

/* weathered stone: strata, cracks, pits (tinted per region by vertex colour) */
static void gen_rock(float u, float v, texel *t) {
  float strata = vnoise(u * 0.5f, v * 6.0f + fbm(u, v, 4, 3, 5) * 1.5f, 2, 9);
  float n = fbm(u, v, 6, 5, 1), fine = fbm(u, v, 32, 3, 3);
  float crack = fabsf(fbm(u, v, 5, 4, 41) - 0.5f);
  float c = crack < 0.018f ? 0.55f + crack * 25.0f : 1.0f;
  float k = 0.55f + 0.3f * n + 0.1f * strata + 0.08f * (fine - 0.5f);
  k *= 0.55f + 0.45f * c;
  t->r = k * 1.0f, t->g = k * 0.95f, t->b = k * 0.9f;
  t->h = 0.5f * n + 0.35f * fine + 0.2f * strata - (1 - c) * 0.5f;
  t->rough = 0.78f + 0.2f * fine;
  t->ao = 0.6f + 0.4f * c * (0.6f + 0.4f * n);
}
/* grass: blades streaked along v, patches of clover and dry tufts */
static void gen_grass(float u, float v, texel *t) {
  float blade = vnoise(u, v, 96, 11) * 0.6f + vnoise(u + 0.13f, v, 48, 12) * 0.4f;
  float patch = fbm(u, v, 5, 4, 14), fine = fbm(u, v, 40, 2, 16);
  float dry = sstep(0.55f, 0.8f, fbm(u, v, 7, 3, 18));
  float lum = 0.35f + 0.65f * blade;
  t->r = mixf(0.07f, 0.34f, lum) + 0.2f * dry, t->g = mixf(0.22f, 0.62f, lum) - 0.08f * dry, t->b = mixf(0.04f, 0.1f, lum);
  float clover = sstep(0.62f, 0.7f, patch) * 0.25f;
  t->r += clover * 0.1f, t->g += clover * 0.2f;
  t->h = blade * 0.6f + fine * 0.4f;
  t->rough = 0.82f - 0.15f * blade;
  t->ao = 0.55f + 0.45f * blade;
}
/* cave stone: cooler, wetter, with a few glinting crystal specks that glow */
static void gen_cave(float u, float v, texel *t) {
  gen_rock(u, v, t);
  float sp = hash2((int)(u * TN), (int)(v * TN), 77, TN);
  float cl = fbm(u, v, 9, 3, 61);
  float speck = sp > 0.9992f ? 1.0f : 0.0f;
  float vein = sstep(0.0f, 0.02f, 0.022f - fabsf(fbm(u, v, 6, 4, 63) - 0.5f)) * sstep(0.58f, 0.7f, cl);
  t->rough = mixf(t->rough, 0.35f, vein);
  t->er = (0.2f * vein + speck) * 0.5f, t->eg = (0.6f * vein + speck) * 0.7f, t->eb = (1.0f * vein + speck) * 0.9f;
  t->b += 0.06f * vein;
}
/* basalt: dark, glassy ridges with glowing seams */
static void gen_basalt(float u, float v, texel *t) {
  gen_rock(u, v, t);
  float seam = fabsf(fbm(u, v, 4, 5, 91) - 0.5f);
  float glow = 1.0f - sstep(0.0f, 0.035f, seam);
  float pulse = 0.5f + 0.5f * fbm(u, v, 8, 2, 93);
  float k = 0.26f * t->r;
  t->r = k, t->g = k * 0.85f, t->b = k * 0.85f;
  t->rough = mixf(t->rough * 0.85f, 0.3f, glow);
  t->h -= glow * 0.5f;
  t->er = glow * (1.0f) * (0.5f + 0.7f * pulse), t->eg = glow * 0.32f * (0.5f + 0.7f * pulse), t->eb = glow * 0.04f;
}
/* sandstone brick: courses, chipped edges, moss in the joints */
static void gen_brick(float u, float v, texel *t) {
  float rows = 6, cols = 3;
  float y = v * rows;
  int row = (int)floorf(y);
  float x = u * cols + (float)(row & 1) * 0.5f;
  float fx = x - floorf(x), fy = y - floorf(y);
  float e = fminf(fminf(fx, 1 - fx) * 2.0f, fminf(fy, 1 - fy) * 4.0f);
  float chip = fbm(u, v, 24, 3, 25);
  e += (chip - 0.5f) * 0.1f;
  float edge = e < 0.06f ? 0.0f : (e < 0.18f ? (e - 0.06f) / 0.12f : 1.0f);
  int id = ((int)floorf(x) * 31 + row * 17) & 255;
  float n = fbm(u, v, 8, 4, 22), fine = fbm(u, v, 32, 3, 7);
  float tint = (float)id / 255.0f;
  float c = (0.62f + 0.14f * tint + 0.16f * (n - 0.5f)) * (0.7f + 0.3f * edge);
  float moss = sstep(0.5f, 0.75f, fbm(u, v, 6, 4, 29)) * (1.0f - edge * 0.6f);
  t->r = mixf(c, 0.2f, moss * 0.7f), t->g = mixf(c * 0.9f, 0.4f, moss * 0.7f), t->b = mixf(c * 0.78f, 0.12f, moss * 0.7f);
  t->h = edge * (0.75f + 0.25f * fine) + moss * 0.1f;
  t->rough = 0.85f - 0.1f * edge + 0.1f * moss;
  t->ao = 0.5f + 0.5f * edge;
}
/* lava: a dark crust broken by glowing cracks that the surface flow slides under */
static void gen_lava(float u, float v, texel *t) {
  float w = fbm(u, v, 4, 4, 51);
  float c1 = fabsf(fbm(u + w * 0.1f, v + w * 0.1f, 5, 5, 53) - 0.5f);
  float c2 = fabsf(fbm(u, v, 11, 3, 57) - 0.5f);
  float hot = 1.0f - sstep(0.0f, 0.045f, c1);
  hot = fmaxf(hot, (1.0f - sstep(0.0f, 0.02f, c2)) * 0.5f);
  float glow = 0.35f + 0.65f * hot;
  t->r = mixf(0.07f, 0.9f, hot), t->g = mixf(0.035f, 0.35f, hot), t->b = mixf(0.03f, 0.04f, hot);
  t->h = (1.0f - hot) * (0.5f + 0.5f * fbm(u, v, 14, 3, 59));
  t->rough = mixf(0.85f, 0.3f, hot);
  t->ao = 1.0f;
  t->er = hot * 1.0f + 0.12f * glow * (1 - hot), t->eg = hot * 0.34f + 0.02f * (1 - hot), t->eb = hot * 0.05f;
}
/* water ripples: a normal map only (its height is two scales of noise) */
static void gen_ripple(float u, float v, texel *t) {
  float a = fbm(u, v, 6, 4, 71), b = fbm(u * 2.0f, v * 2.0f, 8, 3, 73);
  t->r = t->g = t->b = 0.5f;
  t->h = a * 0.6f + b * 0.4f;
  t->rough = 0.1f;
  t->ao = 1.0f;
}

static t3_texture *tex_finish(const uint8_t *px, bool srgb) {
  t3_texture *t = t3_texture_new(TN, TN, px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 8;
  t->color_space = srgb ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
  return t;
}
static uint8_t b8(float x) { return (uint8_t)((x < 0 ? 0 : x > 1 ? 1 : x) * 255.0f + 0.5f); }

/* a material from a generator: albedo, normal, roughness and occlusion maps, plus an emission map when `emissive` */
static t3_material *pbr(void (*gen)(float, float, texel *), float nstrength, float rough_scale, bool emissive, float emis_i) {
  texel *tx = malloc(sizeof *tx * TN * TN);
  uint8_t *a = malloc(TN * TN * 4), *nm = malloc(TN * TN * 4), *ro = malloc(TN * TN * 4), *ao = malloc(TN * TN * 4), *em = emissive ? malloc(TN * TN * 4) : NULL;
  for (int y = 0; y < TN; y++)
    for (int x = 0; x < TN; x++) {
      texel *t = &tx[y * TN + x];
      memset(t, 0, sizeof *t);
      gen(((float)x + 0.5f) / TN, ((float)y + 0.5f) / TN, t);
    }
  for (int y = 0; y < TN; y++)
    for (int x = 0; x < TN; x++) {
      const texel *t = &tx[y * TN + x];
      int i = (y * TN + x) * 4;
      a[i] = b8(t->r), a[i + 1] = b8(t->g), a[i + 2] = b8(t->b), a[i + 3] = 255;
      float hx = tx[y * TN + (x + 1) % TN].h - tx[y * TN + (x + TN - 1) % TN].h;
      float hy = tx[((y + 1) % TN) * TN + x].h - tx[((y + TN - 1) % TN) * TN + x].h;
      float nx = -hx * nstrength, ny = -hy * nstrength, l = sqrtf(nx * nx + ny * ny + 1);
      nm[i] = b8(nx / l * 0.5f + 0.5f), nm[i + 1] = b8(ny / l * 0.5f + 0.5f), nm[i + 2] = b8(1 / l * 0.5f + 0.5f), nm[i + 3] = 255;
      ro[i] = ro[i + 1] = ro[i + 2] = b8(t->rough * rough_scale), ro[i + 3] = 255;
      ao[i] = ao[i + 1] = ao[i + 2] = b8(t->ao), ao[i + 3] = 255;
      if (em) em[i] = b8(t->er), em[i + 1] = b8(t->eg), em[i + 2] = b8(t->eb), em[i + 3] = 255;
    }
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  t3_texture *ts[5] = { tex_finish(a, true), tex_finish(nm, false), tex_finish(ro, false), tex_finish(ao, false), em ? tex_finish(em, true) : NULL };
  static const t3_map_slot SLOTS[5] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP, T3_AO_MAP, T3_EMISSIVE_MAP };
  for (int i = 0; i < 5; i++)
    if (ts[i]) t3_material_set_texture(m, SLOTS[i], ts[i]), t3_release(ts[i]);
  m->normal_scale = (t3_vec2){ 1.0f, 1.0f };
  m->ao_map_intensity = 1.0f;
  m->roughness = 1.0f;
  m->metalness = 0.0f;
  m->vertex_colors = true;
  if (em) m->emissive = (t3_color){ 1, 1, 1 }, m->emissive_intensity = emis_i;
  free(tx), free(a), free(nm), free(ro), free(ao), free(em);
  return m;
}

/* ── mesh builder: vertices with position, normal, colour and uv, quads and triangles, and the solids props are made of ── */

typedef struct mb { float *pos, *nor, *col, *uv; uint32_t *idx; int nv, ni, capv, capi; } mb;
static void mb_room(mb *b, int nv, int ni) {
  if (b->nv + nv > b->capv) {
    while (b->nv + nv > b->capv) b->capv = b->capv ? b->capv * 2 : 4096;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)b->capv);
    b->nor = realloc(b->nor, sizeof(float) * 3 * (size_t)b->capv);
    b->col = realloc(b->col, sizeof(float) * 3 * (size_t)b->capv);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)b->capv);
  }
  if (b->ni + ni > b->capi) {
    while (b->ni + ni > b->capi) b->capi = b->capi ? b->capi * 2 : 6144;
    b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)b->capi);
  }
}
static int mb_vert(mb *b, float x, float y, float z, float nx, float ny, float nz, const float *c, float u, float v) {
  int i = b->nv++;
  b->pos[i * 3] = x, b->pos[i * 3 + 1] = y, b->pos[i * 3 + 2] = z;
  b->nor[i * 3] = nx, b->nor[i * 3 + 1] = ny, b->nor[i * 3 + 2] = nz;
  memcpy(&b->col[i * 3], c, 12);
  b->uv[i * 2] = u, b->uv[i * 2 + 1] = v;
  return i;
}
static void mb_tri_i(mb *b, int a, int c, int d) {
  b->idx[b->ni++] = (uint32_t)a, b->idx[b->ni++] = (uint32_t)c, b->idx[b->ni++] = (uint32_t)d;
}
static void mb_quad(mb *b, const float p[4][3], const float n[3], const float c[4][3], const float uv[4][2]) {
  mb_room(b, 4, 6);
  int v0 = b->nv;
  for (int i = 0; i < 4; i++) mb_vert(b, p[i][0], p[i][1], p[i][2], n[0], n[1], n[2], c[i], uv[i][0], uv[i][1]);
  mb_tri_i(b, v0, v0 + 1, v0 + 2), mb_tri_i(b, v0, v0 + 2, v0 + 3);
}
static t3_geometry *mb_done(mb *b) {
  if (!b->nv) return NULL;
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a = t3_attribute_new(T3_FLOAT32, b->pos, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->nor, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->col, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2);
  t3_geometry_set_attribute(g, T3_ATTR_UV, a), t3_release(a);
  a = t3_attribute_new(T3_UINT32, b->idx, b->ni, 1);
  t3_geometry_set_index(g, a), t3_release(a);
  free(b->pos), free(b->nor), free(b->col), free(b->uv), free(b->idx);
  memset(b, 0, sizeof *b);
  return g;
}

/* an ellipsoid; the colour runs from `lo` at the bottom to `hi` at the top */
static void mb_ellip(mb *b, float cx, float cy, float cz, float rx, float ry, float rz, const float *lo, const float *hi, int seg, int ring) {
  mb_room(b, (seg + 1) * (ring + 1), seg * ring * 6);
  int v0 = b->nv;
  for (int j = 0; j <= ring; j++) {
    float ph = 3.14159265f * (float)j / (float)ring, sy = cosf(ph), sr = sinf(ph);
    float k = 0.5f - 0.5f * sy;
    float c[3] = { mixf(lo[0], hi[0], k), mixf(lo[1], hi[1], k), mixf(lo[2], hi[2], k) };
    for (int i = 0; i <= seg; i++) {
      float th = 6.2831853f * (float)i / (float)seg, cxx = cosf(th) * sr, czz = sinf(th) * sr;
      float nx = cxx / rx, ny = sy / ry, nz = czz / rz, l = sqrtf(nx * nx + ny * ny + nz * nz);
      mb_vert(b, cx + rx * cxx, cy + ry * sy, cz + rz * czz, nx / l, ny / l, nz / l, c, 0, 0);
    }
  }
  for (int j = 0; j < ring; j++)
    for (int i = 0; i < seg; i++) {
      int a = v0 + j * (seg + 1) + i, c2 = a + seg + 1;
      mb_tri_i(b, a, c2, a + 1), mb_tri_i(b, a + 1, c2, c2 + 1);
    }
}
/* a cone or tapered cylinder from the base centre upward (or downward for h < 0), leaning by (lx, lz) at the tip */
static void mb_cone(mb *b, float x, float y, float z, float r0, float r1, float h, float lx, float lz, int sides, const float *c0, const float *c1) {
  mb_room(b, (sides + 1) * 2, sides * 6);
  int v0 = b->nv;
  float sl = (r0 - r1) / (h < 0 ? -h : h);
  for (int k = 0; k < 2; k++)
    for (int i = 0; i <= sides; i++) {
      float th = 6.2831853f * (float)i / (float)sides, cs = cosf(th), sn = sinf(th);
      float r = k ? r1 : r0, f = k ? 1.0f : 0.0f;
      float nx = cs, ny = sl * (h < 0 ? -1.0f : 1.0f), nz = sn, l = sqrtf(nx * nx + ny * ny + nz * nz);
      mb_vert(b, x + cs * r + lx * f, y + h * f, z + sn * r + lz * f, nx / l, ny / l, nz / l, k ? c1 : c0, 0, 0);
    }
  for (int i = 0; i < sides; i++) {
    int a = v0 + i, c2 = a + sides + 1;
    if (h >= 0) mb_tri_i(b, a, c2, a + 1), mb_tri_i(b, a + 1, c2, c2 + 1);
    else mb_tri_i(b, a, a + 1, c2), mb_tri_i(b, a + 1, c2 + 1, c2);
  }
}
/* an axis-aligned box with its own colour at the bottom and the top */
static void mb_box(mb *b, float cx, float cy, float cz, float hx, float hy, float hz, const float *lo, const float *hi) {
  float x0 = cx - hx, x1 = cx + hx, y0 = cy - hy, y1 = cy + hy, z0 = cz - hz, z1 = cz + hz;
  float P[8][3] = { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 }, { x1, y0, z0 }, { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 } };
  static const int F[6][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 5, 0, 3, 6 }, { 1, 4, 7, 2 }, { 3, 2, 7, 6 }, { 5, 4, 1, 0 } };
  static const float N[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
  float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
  for (int f = 0; f < 6; f++) {
    float p[4][3], c[4][3];
    for (int k = 0; k < 4; k++) {
      memcpy(p[k], P[F[f][k]], 12);
      const float *col = p[k][1] > cy ? hi : lo;
      memcpy(c[k], col, 12);
    }
    mb_quad(b, p, N[f], c, uv);
  }
}

static float hh(int x, int y, int s) { return hash2(x, y, s, 1 << 20); }
static void rgb(float *o, float r, float g, float bl) { o[0] = r, o[1] = g, o[2] = bl; }
static void shade(float *o, const float *c, float k) { o[0] = c[0] * k, o[1] = c[1] * k, o[2] = c[2] * k; }

/* ── the terrain ───────────────────────────────────────────────────── */

enum { MT_ROCK, MT_GRASS, MT_CAVE, MT_BRICK, MT_BASALT, MT_PROP, MT_GLOW, MT_COUNT };
/* stone tint per region (the texture is grey-brown, the vertex colour moves it) and the colour of a top surface */
static const float STONE[BIOMES][3] = { { 0.95f, 0.72f, 0.55f }, { 0.55f, 0.66f, 0.9f }, { 1.0f, 0.88f, 0.66f }, { 1.1f, 0.85f, 0.85f } };
static const float TOPS[BIOMES][3] = { { 0.85f, 1.0f, 0.7f }, { 0.55f, 0.7f, 0.95f }, { 1.0f, 0.9f, 0.7f }, { 1.2f, 0.9f, 0.85f } };

static int tile_mat(const level *L, int x, int y) {
  int t = L->t[y][x], bi = L->biome[x];
  if (t == T_BRICK) return MT_BRICK;
  return bi == BIOME_CAVES ? MT_CAVE : bi == BIOME_VOLCANO ? MT_BASALT : MT_ROCK;
}

#define DEPTH_FRONT 1.5f
#define DEPTH_BACK -1.5f
#define CHUNK 16

static void tile_faces(const level *L, int x, int y, mb *bs) {
  int bi = L->biome[x];
  int mt = tile_mat(L, x, y);
  mb *b = &bs[mt];
  float fx = (float)x, fy = (float)y;
  const float *st = STONE[bi], *tp = TOPS[bi];
  bool open_above = !world_solid(L, x, y + 1);
  bool grass_top = open_above && bi == BIOME_MEADOW && mt == MT_ROCK;
  /* a different dark per tile so the wall is not a repeating pattern, plus a fall-off with depth under the surface */
  int below = 0;
  while (y - below > 0 && below < 6 && world_solid(L, x, y - below - 1)) below++;
  float depth_k = 1.0f;
  { int up = 0; while (y + up + 1 < WORLD_H && world_solid(L, x, y + up + 1) && up < 5) up++; depth_k = 1.0f - 0.06f * (float)up; }
  float var = 0.86f + 0.14f * hh(x, y, 5);
  float cs[3] = { st[0] * var * depth_k, st[1] * var * depth_k, st[2] * var * depth_k };
  float ct[3] = { tp[0], tp[1], tp[2] };
  /* front face (toward the camera) */
  {
    float p[4][3] = { { fx, fy, DEPTH_FRONT }, { fx + 1, fy, DEPTH_FRONT }, { fx + 1, fy + 1, DEPTH_FRONT }, { fx, fy + 1, DEPTH_FRONT } };
    float n[3] = { 0, 0, 1 };
    float bot[3], top[3];
    /* a little ambient occlusion where the tile below is open (a ledge underside) and a lit lip where the top is open */
    shade(bot, cs, 0.9f);
    if (open_above) shade(top, cs, 1.15f); else shade(top, cs, 1.0f);
    float c[4][3] = { { bot[0], bot[1], bot[2] }, { bot[0], bot[1], bot[2] }, { top[0], top[1], top[2] }, { top[0], top[1], top[2] } };
    float uv[4][2] = { { fx / 4, fy / 4 }, { (fx + 1) / 4, fy / 4 }, { (fx + 1) / 4, (fy + 1) / 4 }, { fx / 4, (fy + 1) / 4 } };
    mb_quad(b, p, n, c, uv);
  }
  if (open_above) { /* top */
    mb *tb = grass_top ? &bs[MT_GRASS] : b;
    float p[4][3] = { { fx, fy + 1, DEPTH_FRONT }, { fx + 1, fy + 1, DEPTH_FRONT }, { fx + 1, fy + 1, DEPTH_BACK }, { fx, fy + 1, DEPTH_BACK } };
    float n[3] = { 0, 1, 0 };
    float c[4][3];
    const float *tc = grass_top ? ct : (bi == BIOME_VOLCANO ? ct : cs);
    float g1[3], g2[3];
    shade(g1, tc, 1.0f), shade(g2, tc, 0.85f);
    memcpy(c[0], g1, 12), memcpy(c[1], g1, 12), memcpy(c[2], g2, 12), memcpy(c[3], g2, 12);
    float uv[4][2] = { { fx / 3, 0.0f }, { (fx + 1) / 3, 0.0f }, { (fx + 1) / 3, 1.0f }, { fx / 3, 1.0f } };
    mb_quad(tb, p, n, c, uv);
  }
  if (y > 0 && !world_solid(L, x, y - 1)) { /* underside */
    float p[4][3] = { { fx, fy, DEPTH_BACK }, { fx + 1, fy, DEPTH_BACK }, { fx + 1, fy, DEPTH_FRONT }, { fx, fy, DEPTH_FRONT } };
    float n[3] = { 0, -1, 0 };
    float c[4][3];
    for (int i = 0; i < 4; i++) shade(c[i], cs, 0.7f);
    float uv[4][2] = { { fx / 4, 0 }, { (fx + 1) / 4, 0 }, { (fx + 1) / 4, 0.75f }, { fx / 4, 0.75f } };
    mb_quad(b, p, n, c, uv);
  }
  if (x > 0 && !world_solid(L, x - 1, y)) { /* left side */
    float p[4][3] = { { fx, fy, DEPTH_BACK }, { fx, fy, DEPTH_FRONT }, { fx, fy + 1, DEPTH_FRONT }, { fx, fy + 1, DEPTH_BACK } };
    float n[3] = { -1, 0, 0 };
    float c[4][3];
    for (int i = 0; i < 4; i++) shade(c[i], cs, 0.9f);
    float uv[4][2] = { { 0, fy / 4 }, { 0.75f, fy / 4 }, { 0.75f, (fy + 1) / 4 }, { 0, (fy + 1) / 4 } };
    mb_quad(b, p, n, c, uv);
  }
  if (x + 1 < WORLD_W && !world_solid(L, x + 1, y)) { /* right side */
    float p[4][3] = { { fx + 1, fy, DEPTH_FRONT }, { fx + 1, fy, DEPTH_BACK }, { fx + 1, fy + 1, DEPTH_BACK }, { fx + 1, fy + 1, DEPTH_FRONT } };
    float n[3] = { 1, 0, 0 };
    float c[4][3];
    for (int i = 0; i < 4; i++) shade(c[i], cs, 0.9f);
    float uv[4][2] = { { 0, fy / 4 }, { 0.75f, fy / 4 }, { 0.75f, (fy + 1) / 4 }, { 0, (fy + 1) / 4 } };
    mb_quad(b, p, n, c, uv);
  }
}

/* ── props: grass and flowers, mushrooms and crystals, vines and rubble, spires and ash rock ──
 * Everything goes into two meshes per chunk: lit solids (vertex colours) and glowing parts (an HDR colour that blooms). */

static void grass_tuft(mb *p, float x, float y, float z, int seed, float scale) {
  for (int k = 0; k < 5; k++) {
    float a = (float)k * 1.26f + hh(seed, k, 1) * 2.0f, h = (0.28f + 0.32f * hh(seed, k, 2)) * scale;
    float lean = (hh(seed, k, 3) - 0.5f) * 0.5f * scale;
    float c0[3], c1[3];
    rgb(c0, 0.06f, 0.2f, 0.03f), rgb(c1, 0.3f + 0.2f * hh(seed, k, 4), 0.62f, 0.12f);
    mb_cone(p, x + cosf(a) * 0.06f * scale, y, z + sinf(a) * 0.06f * scale, 0.045f * scale, 0.004f, h, lean * cosf(a), lean * 0.3f, 4, c0, c1);
  }
}
static void flower(mb *p, mb *g, float x, float y, float z, int seed) {
  float h = 0.3f + 0.3f * hh(seed, 1, 6);
  float c0[3], c1[3];
  rgb(c0, 0.05f, 0.18f, 0.03f), rgb(c1, 0.12f, 0.4f, 0.06f);
  mb_cone(p, x, y, z, 0.018f, 0.012f, h, (hh(seed, 2, 6) - 0.5f) * 0.1f, 0, 4, c0, c1);
  static const float PET[5][3] = { { 1.0f, 0.25f, 0.3f }, { 1.0f, 0.82f, 0.2f }, { 0.95f, 0.95f, 1.0f }, { 0.65f, 0.45f, 1.0f }, { 1.0f, 0.55f, 0.15f } };
  const float *pc = PET[(int)(hh(seed, 3, 6) * 5) % 5];
  float lo[3] = { pc[0] * 0.7f, pc[1] * 0.7f, pc[2] * 0.7f }, mid[3];
  for (int k = 0; k < 5; k++) {
    float a = (float)k * 1.2566f;
    mb_ellip(p, x + cosf(a) * 0.07f, y + h, z + sinf(a) * 0.07f, 0.055f, 0.03f, 0.055f, lo, pc, 6, 4);
  }
  rgb(mid, 1.0f, 0.8f, 0.2f);
  mb_ellip(p, x, y + h + 0.01f, z, 0.04f, 0.035f, 0.04f, mid, mid, 6, 4);
  (void)g;
}
static void rock_pile(mb *p, float x, float y, float z, int seed, const float *base, float s) {
  for (int k = 0; k < 3; k++) {
    float rx = (0.16f + 0.2f * hh(seed, k, 8)) * s, ry = rx * (0.55f + 0.3f * hh(seed, k, 9));
    float lo[3], hi[3];
    shade(lo, base, 0.55f + 0.2f * hh(seed, k, 10)), shade(hi, base, 0.95f);
    mb_ellip(p, x + (hh(seed, k, 11) - 0.5f) * 0.6f * s, y + ry * 0.45f, z + (hh(seed, k, 12) - 0.5f) * 0.4f, rx, ry, rx * 0.9f, lo, hi, 8, 5);
  }
}
static void bush(mb *p, float x, float y, float z, int seed, float s) {
  for (int k = 0; k < 4; k++) {
    float r = (0.35f + 0.25f * hh(seed, k, 13)) * s;
    float lo[3], hi[3];
    rgb(lo, 0.05f, 0.18f, 0.04f), rgb(hi, 0.18f + 0.12f * hh(seed, k, 14), 0.5f, 0.1f);
    mb_ellip(p, x + (float)(k - 1.5f) * 0.38f * s, y + r * 0.7f, z + (hh(seed, k, 15) - 0.5f) * 0.3f, r, r * 0.85f, r, lo, hi, 10, 7);
  }
}
static void tree(mb *p, float x, float y, float z, int seed, float s) {
  float c0[3], c1[3];
  rgb(c0, 0.2f, 0.1f, 0.05f), rgb(c1, 0.3f, 0.17f, 0.08f);
  float h = (3.2f + 2.0f * hh(seed, 1, 20)) * s;
  mb_cone(p, x, y - 0.2f, z, 0.32f * s, 0.2f * s, h, (hh(seed, 2, 20) - 0.5f) * 0.5f, 0, 8, c0, c1);
  for (int k = 0; k < 4; k++) {
    float r = (1.5f - 0.2f * (float)k) * s;
    float lo[3], hi[3];
    rgb(lo, 0.04f, 0.17f, 0.04f), rgb(hi, 0.16f + 0.08f * hh(seed, k, 21), 0.5f, 0.1f);
    mb_ellip(p, x + (hh(seed, k, 22) - 0.5f) * 0.8f * s, y + h * (0.7f + 0.17f * (float)k), z + (hh(seed, k, 23) - 0.5f) * 0.6f, r, r * 0.8f, r * 0.9f, lo, hi, 12, 8);
  }
}
static void mushroom(mb *p, mb *g, float x, float y, float z, int seed, float s) {
  float c0[3], c1[3];
  rgb(c0, 0.5f, 0.55f, 0.65f), rgb(c1, 0.75f, 0.8f, 0.9f);
  float h = (0.4f + 0.5f * hh(seed, 1, 30)) * s;
  mb_cone(p, x, y, z, 0.07f * s, 0.05f * s, h, (hh(seed, 2, 30) - 0.5f) * 0.1f, 0, 6, c0, c1);
  static const float CAP[3][3] = { { 0.3f, 1.2f, 2.2f }, { 0.4f, 2.0f, 1.2f }, { 1.6f, 0.6f, 2.0f } };
  const float *cc = CAP[(int)(hh(seed, 3, 30) * 3) % 3];
  float dim[3] = { cc[0] * 0.5f, cc[1] * 0.5f, cc[2] * 0.5f };
  mb_ellip(g, x, y + h, z, 0.28f * s, 0.16f * s, 0.28f * s, dim, cc, 10, 6);
}
static void crystal_cluster(mb *g, mb *p, float x, float y, float z, int seed, float s, int hue, bool down) {
  static const float COL[4][3] = { { 0.5f, 1.6f, 3.0f }, { 0.7f, 3.0f, 1.8f }, { 2.6f, 0.7f, 3.0f }, { 3.0f, 1.4f, 0.5f } };
  const float *c = COL[hue & 3];
  float dim[3] = { c[0] * 0.35f, c[1] * 0.35f, c[2] * 0.35f };
  int n = 3 + (int)(hh(seed, 1, 40) * 3);
  float dir = down ? -1.0f : 1.0f;
  for (int k = 0; k < n; k++) {
    float h = (0.5f + 1.1f * hh(seed, k, 41)) * s, lean = (hh(seed, k, 42) - 0.5f) * 0.9f * s;
    mb_cone(g, x + (float)(k - n / 2) * 0.17f * s, y, z + (hh(seed, k, 43) - 0.5f) * 0.3f, 0.11f * s, 0.015f, h * dir, lean, 0, 5, dim, c);
  }
  float rb[3] = { 0.2f, 0.22f, 0.3f };
  rock_pile(p, x, y + (down ? -0.1f : 0.0f), z, seed, rb, 0.6f * s);
}
static void stalactite(mb *p, float x, float y, float z, int seed, float s) {
  float c0[3], c1[3];
  rgb(c0, 0.28f, 0.32f, 0.42f), rgb(c1, 0.12f, 0.14f, 0.22f);
  mb_cone(p, x, y, z, (0.18f + 0.18f * hh(seed, 1, 50)) * s, 0.01f, -(0.8f + 1.6f * hh(seed, 2, 50)) * s, 0, 0, 7, c0, c1);
}
static void vine(mb *p, float x, float y, float z, int seed, float len) {
  float c0[3], c1[3];
  rgb(c0, 0.08f, 0.2f, 0.06f), rgb(c1, 0.18f, 0.4f, 0.1f);
  mb_cone(p, x, y, z, 0.03f, 0.012f, -len, 0.15f * (hh(seed, 1, 60) - 0.5f), 0, 4, c0, c1);
  for (int k = 0; k < 4; k++) {
    float ly = y - len * (0.2f + 0.2f * (float)k);
    float lo[3], hi[3];
    rgb(lo, 0.06f, 0.22f, 0.05f), rgb(hi, 0.2f, 0.5f, 0.1f);
    mb_ellip(p, x + (k & 1 ? 0.1f : -0.1f), ly, z, 0.09f, 0.04f, 0.06f, lo, hi, 6, 4);
  }
}
static void column_prop(mb *p, float x, float y, float z, float h, int seed, bool broken) {
  float lo[3], hi[3], cap[3];
  rgb(lo, 0.6f, 0.52f, 0.4f), rgb(hi, 0.85f, 0.76f, 0.6f), rgb(cap, 0.9f, 0.82f, 0.66f);
  mb_box(p, x, y + 0.2f, z, 0.55f, 0.2f, 0.55f, lo, cap);
  mb_cone(p, x, y + 0.4f, z, 0.38f, 0.34f, h, 0, 0, 12, lo, hi);
  if (!broken) mb_box(p, x, y + 0.4f + h + 0.15f, z, 0.55f, 0.15f, 0.55f, hi, cap);
  else {
    mb_cone(p, x, y + 0.4f + h, z, 0.34f, 0.12f, 0.3f, 0.1f, 0, 7, hi, lo);
    rock_pile(p, x + 0.8f, y, z + 0.3f, seed, lo, 0.8f);
  }
}
static void lava_spire(mb *p, mb *g, float x, float y, float z, int seed, float s) {
  float c0[3], c1[3];
  rgb(c0, 0.2f, 0.12f, 0.12f), rgb(c1, 0.07f, 0.05f, 0.05f);
  float h = (1.2f + 1.8f * hh(seed, 1, 70)) * s;
  mb_cone(p, x, y - 0.1f, z, 0.34f * s, 0.03f, h, (hh(seed, 2, 70) - 0.5f) * 0.7f, 0, 6, c0, c1);
  float gl0[3] = { 1.2f, 0.3f, 0.04f }, gl1[3] = { 3.0f, 1.0f, 0.2f };
  mb_cone(g, x, y + 0.03f, z + 0.16f * s, 0.045f * s, 0.01f, h * 0.55f, 0, 0, 4, gl0, gl1);
}

/* scatter props over the surfaces of one chunk */
static void scatter(const level *L, int x0, int x1, mb *prop, mb *glow) {
  for (int x = x0; x < x1 && x < WORLD_W; x++) {
    int bi = L->biome[x];
    for (int y = 1; y < WORLD_H - 1; y++) {
      int t = L->t[y][x];
      bool solid = world_solid(L, x, y);
      bool floor_ = solid && !world_solid(L, x, y + 1) && L->t[y + 1][x] != T_SPIKE && L->t[y + 1][x] != T_WATER && L->t[y + 1][x] != T_LAVA;
      bool ceil_ = solid && !world_solid(L, x, y - 1) && L->t[y - 1][x] == T_AIR && y > 4;
      float fx = (float)x, fy = (float)y + 1.0f;
      int seed = x * 131 + y * 17;
      float r = hh(x, y, 100);
      if (floor_ && t != T_CRACK) {
        if (bi == BIOME_MEADOW) {
          if (r < 0.55f) grass_tuft(prop, fx + hh(x, y, 101), fy, (r < 0.3f ? 1.0f : -1.0f) * (0.45f + 0.9f * hh(x, y, 102)), seed, 1.0f);
          if (r < 0.2f) grass_tuft(prop, fx + hh(x, y, 103), fy, 0.7f + 0.6f * hh(x, y, 104), seed + 1, 0.8f);
          if (r > 0.82f && r < 0.93f) flower(prop, glow, fx + hh(x, y, 105), fy, (hh(x, y, 106) > 0.5f ? 1.0f : -1.0f) * (0.6f + 0.7f * hh(x, y, 107)), seed);
          if (r > 0.93f && r < 0.965f) rock_pile(prop, fx + 0.5f, fy, -0.9f - 0.3f * hh(x, y, 108), seed, STONE[0], 1.0f);
          if (r > 0.965f && x % 3 == 0) bush(prop, fx + 0.5f, fy, -1.1f, seed, 0.8f + 0.5f * hh(x, y, 109));
        } else if (bi == BIOME_CAVES) {
          if (r < 0.1f) mushroom(prop, glow, fx + hh(x, y, 110), fy, (hh(x, y, 111) > 0.5f ? 1.0f : -1.0f) * (0.7f + 0.6f * hh(x, y, 112)), seed, 0.7f + 0.8f * hh(x, y, 113));
          if (r > 0.1f && r < 0.2f) crystal_cluster(glow, prop, fx + 0.5f, fy, -0.9f - 0.4f * hh(x, y, 114), seed, 0.6f + 0.8f * hh(x, y, 115), x / 40, false);
          if (r > 0.2f && r < 0.35f) grass_tuft(prop, fx + hh(x, y, 116), fy, -0.9f - 0.4f * hh(x, y, 117), seed, 0.6f);
        } else if (bi == BIOME_RUINS) {
          if (r < 0.1f && x % 2 == 0) rock_pile(prop, fx + 0.5f, fy, (hh(x, y, 118) > 0.5f ? 1 : -1) * 0.9f, seed, STONE[2], 0.8f);
          if (r > 0.1f && r < 0.3f) grass_tuft(prop, fx + hh(x, y, 119), fy, (hh(x, y, 120) > 0.5f ? 1.0f : -1.0f) * (0.5f + 0.9f * hh(x, y, 121)), seed, 0.9f);
          if (r > 0.3f && r < 0.36f) flower(prop, glow, fx + hh(x, y, 122), fy, -0.8f, seed);
        } else {
          if (r < 0.07f) lava_spire(prop, glow, fx + 0.5f, fy, -0.9f - 0.5f * hh(x, y, 123), seed, 0.8f + 0.9f * hh(x, y, 124));
          if (r > 0.07f && r < 0.2f) rock_pile(prop, fx + hh(x, y, 125), fy, (hh(x, y, 126) > 0.5f ? 1 : -1) * (0.7f + 0.6f * hh(x, y, 127)), seed, STONE[3], 0.9f);
        }
      }
      if (ceil_ && !floor_) {
        float cy = (float)y; /* the underside of the tile */
        if (bi == BIOME_CAVES) {
          if (r < 0.3f) stalactite(prop, fx + hh(x, y, 130), cy, -0.6f + 1.2f * hh(x, y, 131), seed, 0.8f + 0.8f * hh(x, y, 132));
          if (r > 0.3f && r < 0.36f) crystal_cluster(glow, prop, fx + 0.5f, cy, -0.8f, seed, 0.6f + 0.6f * hh(x, y, 133), x / 30 + 1, true);
        } else if (bi == BIOME_RUINS && r < 0.3f) vine(prop, fx + hh(x, y, 134), cy, 0.9f - 1.8f * hh(x, y, 135) , seed, 0.8f + 1.8f * hh(x, y, 136));
        else if (bi == BIOME_VOLCANO && r < 0.2f) stalactite(prop, fx + hh(x, y, 137), cy, -0.6f + 1.2f * hh(x, y, 138), seed, 1.0f);
      }
    }
  }
}

/* ── scene objects ──────────────────────────────────────────────── */

static cw_node g_root;
static t3_material *g_mat[MT_COUNT], *g_crack_m, *g_oneway_m, *g_spike, *g_lava_m, *g_water_front, *g_foam_m, *g_wall_m[BIOMES];
static t3_mesh *g_crack_mesh[64];
static t3_texture *g_lava_tex;

typedef struct span { float x0, x1, y0, y1; } span;
static span g_waters[16], g_lavas[16];
static int g_nwaters, g_nlavas;
int world_water_spans(float out[][4], int cap) {
  int n = g_nwaters < cap ? g_nwaters : cap;
  for (int i = 0; i < n; i++) out[i][0] = g_waters[i].x0, out[i][1] = g_waters[i].x1, out[i][2] = g_waters[i].y0, out[i][3] = g_waters[i].y1;
  return n;
}
int world_lava_spans(float out[][4], int cap) {
  int n = g_nlavas < cap ? g_nlavas : cap;
  for (int i = 0; i < n; i++) out[i][0] = g_lavas[i].x0, out[i][1] = g_lavas[i].x1, out[i][2] = g_lavas[i].y0, out[i][3] = g_lavas[i].y1;
  return n;
}

static t3_mesh *add_mesh(t3_geometry *g, t3_material *m, float x, float y, float z, bool shadow) {
  cw_node n = cw_node_new("MeshInstance3D", "m");
  cw_node_add_child(g_root, n);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.position = (t3_vec3){ x, y, z };
  me->base.cast_shadow = shadow;
  me->base.receive_shadow = true;
  cw_mesh3d_set_procedural(n, me);
  t3_release(me);
  return me;
}

/* animated surfaces: the lava's top and front slide their uvs, the water's front sheet waves */
typedef struct anim_surf { t3_geometry *geo; float *pos, *uv; int nv; float x0, x1, y_top, y_bot, z; int kind; int cols; } anim_surf;
static anim_surf g_surf[40];
static int g_nsurf;

void world_scene_free(void) {
  g_root = (cw_node){ 0 };
  g_nsurf = 0;
  g_nwaters = g_nlavas = 0;
}

static t3_material *basic_vcol(bool additive, bool transparent, float opacity) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  m->side = T3_DOUBLE_SIDE;
  m->fog = false;
  if (additive) m->blending = 2, m->transparent = true, m->depth_write = false;
  if (transparent) m->transparent = true, m->opacity = opacity, m->depth_write = false;
  return m;
}

void world_scene_build(game *g, cw_node root) {
  const level *L = &g->L;
  g_root = root;
  g_nsurf = 0;
  g_nwaters = g_nlavas = 0;
  g_mat[MT_ROCK] = pbr(gen_rock, 3.0f, 1.0f, false, 0);
  g_mat[MT_GRASS] = pbr(gen_grass, 2.0f, 1.0f, false, 0);
  g_mat[MT_CAVE] = pbr(gen_cave, 3.0f, 1.0f, true, 1.1f);
  g_mat[MT_BRICK] = pbr(gen_brick, 4.0f, 1.0f, false, 0);
  g_mat[MT_BASALT] = pbr(gen_basalt, 3.5f, 1.0f, true, 2.2f);
  g_mat[MT_PROP] = t3_mesh_physical_material_new(0xffffff);
  g_mat[MT_PROP]->vertex_colors = true, g_mat[MT_PROP]->roughness = 0.75f, g_mat[MT_PROP]->side = T3_DOUBLE_SIDE;
  g_mat[MT_GLOW] = basic_vcol(false, false, 1);
  /* terrain and props in chunks */
  for (int cx = 0; cx < WORLD_W; cx += CHUNK) {
    mb bs[MT_COUNT];
    memset(bs, 0, sizeof bs);
    for (int y = 0; y < WORLD_H; y++)
      for (int x = cx; x < cx + CHUNK && x < WORLD_W; x++) {
        int t = L->t[y][x];
        if (t == T_ROCK || t == T_GRASS || t == T_BRICK) tile_faces(L, x, y, bs);
      }
    scatter(L, cx, cx + CHUNK, &bs[MT_PROP], &bs[MT_GLOW]);
    for (int m = 0; m < MT_COUNT; m++) {
      t3_geometry *gm = mb_done(&bs[m]);
      if (gm) add_mesh(gm, g_mat[m], 0, 0, 0, m != MT_GLOW), t3_release(gm);
    }
  }
  /* cracked blocks, one mesh each (removed when broken) */
  g_crack_m = t3_mesh_physical_material_new(0xb89a7a);
  g_crack_m->roughness = 0.9f;
  int nc = 0;
  memset(g_crack_mesh, 0, sizeof g_crack_mesh);
  for (int y = 0; y < WORLD_H; y++)
    for (int x = 0; x < WORLD_W; x++)
      if (L->t[y][x] == T_CRACK && nc < 64) {
        t3_geometry *bg = t3_box_geometry_new(0.98f, 0.98f, 2.9f, 1, 1, 1);
        g_crack_mesh[nc++] = add_mesh(bg, g_crack_m, (float)x + 0.5f, (float)y + 0.5f, 0, true);
        t3_release(bg);
      }
  /* one-way ledges: wooden planks */
  g_oneway_m = t3_mesh_physical_material_new(0x8a5a32);
  g_oneway_m->roughness = 0.7f;
  for (int y = 0; y < WORLD_H; y++)
    for (int x = 0; x < WORLD_W; x++)
      if (L->t[y][x] == T_ONEWAY) {
        t3_geometry *bg = t3_box_geometry_new(1.0f, 0.26f, 2.2f, 1, 1, 1);
        add_mesh(bg, g_oneway_m, (float)x + 0.5f, (float)y + 0.88f, 0, true), t3_release(bg);
      }
  /* spikes */
  g_spike = t3_mesh_physical_material_new(0xc8ccd4);
  g_spike->metalness = 0.9f, g_spike->roughness = 0.3f;
  t3_geometry *cone = t3_cone_geometry_new(0.2f, 0.8f, 6, 1, false);
  for (int y = 0; y < WORLD_H; y++)
    for (int x = 0; x < WORLD_W; x++)
      if (L->t[y][x] == T_SPIKE)
        for (int k = 0; k < 2; k++) add_mesh(cone, g_spike, (float)x + 0.25f + 0.5f * (float)k, (float)y + 0.4f, 0.0f, true);
  t3_release(cone);
  /* lava: a glowing surface with a flowing crust, its top and the face that looks at the camera */
  g_lava_m = pbr(gen_lava, 2.0f, 1.0f, true, 1.7f);
  g_lava_m->vertex_colors = false;
  for (int x = 0; x < WORLD_W; x++)
    if (L->t[2][x] == T_LAVA) {
      int x1 = x;
      while (x1 < WORLD_W && L->t[2][x1] == T_LAVA) x1++;
      float w = (float)(x1 - x), top = 2.75f;
      if (g_nsurf + 2 > 40 || g_nlavas >= 16) break;
      g_lavas[g_nlavas++] = (span){ (float)x, (float)x1, 0, top };
      /* top: a grid so the surface can heave */
      int cols = (int)w * 3, rows = 6, nv = (cols + 1) * (rows + 1);
      float *pos = malloc(sizeof(float) * 3 * (size_t)nv), *nor = malloc(sizeof(float) * 3 * (size_t)nv), *uv = malloc(sizeof(float) * 2 * (size_t)nv);
      uint32_t *idx = malloc(sizeof(uint32_t) * 6 * (size_t)(cols * rows));
      for (int j = 0; j <= rows; j++)
        for (int i = 0; i <= cols; i++) {
          int v = j * (cols + 1) + i;
          pos[v * 3] = (float)x + w * (float)i / (float)cols, pos[v * 3 + 1] = top, pos[v * 3 + 2] = DEPTH_FRONT - 0.05f - (DEPTH_FRONT * 2 - 0.1f) * (float)j / (float)rows;
          nor[v * 3] = 0, nor[v * 3 + 1] = 1, nor[v * 3 + 2] = 0;
          uv[v * 2] = pos[v * 3] / 3.0f, uv[v * 2 + 1] = pos[v * 3 + 2] / 3.0f;
        }
      int k = 0;
      for (int j = 0; j < rows; j++)
        for (int i = 0; i < cols; i++) {
          uint32_t a = (uint32_t)(j * (cols + 1) + i), b2 = a + 1, c = a + (uint32_t)(cols + 1), d = c + 1;
          idx[k++] = a, idx[k++] = c, idx[k++] = b2, idx[k++] = b2, idx[k++] = c, idx[k++] = d;
        }
      t3_geometry *gt = t3_geometry_new();
      t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, nv, 3);
      at->usage = T3_DYNAMIC_DRAW;
      t3_geometry_set_attribute(gt, T3_ATTR_POSITION, at), t3_release(at);
      at = t3_attribute_new(T3_FLOAT32, nor, nv, 3);
      t3_geometry_set_attribute(gt, T3_ATTR_NORMAL, at), t3_release(at);
      at = t3_attribute_new(T3_FLOAT32, uv, nv, 2);
      at->usage = T3_DYNAMIC_DRAW;
      t3_geometry_set_attribute(gt, T3_ATTR_UV, at), t3_release(at);
      at = t3_attribute_new(T3_UINT32, idx, k, 1);
      t3_geometry_set_index(gt, at), t3_release(at);
      free(nor), free(idx);
      add_mesh(gt, g_lava_m, 0, 0, 0, false);
      g_surf[g_nsurf++] = (anim_surf){ gt, pos, uv, nv, (float)x, (float)x1, top, 0, 0, 0, cols };
      /* front: a flat face below the surface */
      float fp[4][3] = { { (float)x, 0, DEPTH_FRONT - 0.05f }, { (float)x1, 0, DEPTH_FRONT - 0.05f }, { (float)x1, top, DEPTH_FRONT - 0.05f }, { (float)x, top, DEPTH_FRONT - 0.05f } };
      float fn[3] = { 0, 0, 1 }, fc[4][3] = { { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 } };
      float fuv[4][2] = { { (float)x / 3, 0 }, { (float)x1 / 3, 0 }, { (float)x1 / 3, top / 3 }, { (float)x / 3, top / 3 } };
      mb fb = { 0 };
      mb_quad(&fb, fp, fn, fc, fuv);
      t3_geometry *gf = mb_done(&fb);
      add_mesh(gf, g_lava_m, 0, 0, 0, false), t3_release(gf);
      x = x1 - 1;
    }
  /* water: a lit translucent front sheet (its top edge is CPU waves) with foam along it; the surface seen from above
   * is a Water3D node made in main.c from world_water_spans */
  t3_texture *rip = NULL;
  {
    uint8_t *nm = malloc(TN * TN * 4);
    texel *tx = malloc(sizeof *tx * TN * TN);
    for (int y = 0; y < TN; y++)
      for (int x = 0; x < TN; x++) gen_ripple(((float)x + 0.5f) / TN, ((float)y + 0.5f) / TN, &tx[y * TN + x]);
    for (int y = 0; y < TN; y++)
      for (int x = 0; x < TN; x++) {
        float hx = tx[y * TN + (x + 1) % TN].h - tx[y * TN + (x + TN - 1) % TN].h, hy = tx[((y + 1) % TN) * TN + x].h - tx[((y + TN - 1) % TN) * TN + x].h;
        float nx = -hx * 5.0f, ny = -hy * 5.0f, l = sqrtf(nx * nx + ny * ny + 1);
        int i = (y * TN + x) * 4;
        nm[i] = b8(nx / l * 0.5f + 0.5f), nm[i + 1] = b8(ny / l * 0.5f + 0.5f), nm[i + 2] = b8(1 / l * 0.5f + 0.5f), nm[i + 3] = 255;
      }
    rip = tex_finish(nm, false);
    free(nm), free(tx);
  }
  g_water_front = t3_mesh_physical_material_new(0xffffff);
  g_water_front->vertex_colors = true;
  g_water_front->transparent = true;
  g_water_front->opacity = 0.78f;
  g_water_front->roughness = 0.08f;
  g_water_front->depth_write = false;
  g_water_front->side = T3_DOUBLE_SIDE;
  t3_material_set_texture(g_water_front, T3_NORMAL_MAP, rip);
  g_water_front->normal_scale = (t3_vec2){ 0.5f, 0.5f };
  t3_release(rip);
  g_foam_m = basic_vcol(true, false, 1.0f);
  for (int x = 0; x < WORLD_W; x++) {
    int ty = -1;
    for (int y = 0; y < WORLD_H; y++)
      if (L->t[y][x] == T_WATER) ty = y;
    if (ty < 0) continue;
    int x1 = x;
    while (x1 < WORLD_W) {
      int top = -1;
      for (int y = 0; y < WORLD_H; y++)
        if (L->t[y][x1] == T_WATER) top = y;
      if (top != ty) break;
      x1++;
    }
    if (x1 == x || g_nsurf + 2 > 40 || g_nwaters >= 16) { x = x1 > x ? x1 - 1 : x; continue; }
    int low = ty;
    for (int xx = x; xx < x1; xx++)
      for (int y = 0; y < WORLD_H; y++)
        if (L->t[y][xx] == T_WATER && y < low) low = y;
    float bottom = (float)low, topy = (float)ty + 1.0f;
    g_waters[g_nwaters++] = (span){ (float)x, (float)x1, bottom, topy };
    int segs = (x1 - x) * 4, nv = (segs + 1) * 2;
    for (int pass = 0; pass < 2; pass++) { /* 0: the sheet, 1: the foam strip */
      float *pos = malloc(sizeof(float) * 3 * (size_t)nv), *nor = malloc(sizeof(float) * 3 * (size_t)nv), *col = malloc(sizeof(float) * 3 * (size_t)nv), *uv = malloc(sizeof(float) * 2 * (size_t)nv);
      uint32_t *idx = malloc(sizeof(uint32_t) * 6 * (size_t)segs);
      for (int i = 0; i <= segs; i++) {
        float px = (float)x + (float)i / 4.0f;
        for (int k = 0; k < 2; k++) {
          int v = i * 2 + k;
          pos[v * 3] = px, pos[v * 3 + 1] = k ? topy : bottom, pos[v * 3 + 2] = 1.25f;
          nor[v * 3] = 0, nor[v * 3 + 1] = 0, nor[v * 3 + 2] = 1;
          uv[v * 2] = px / 3.0f, uv[v * 2 + 1] = (k ? topy : bottom) / 3.0f;
          /* depth colour: bright turquoise at the surface, deep blue at the bottom */
          if (pass == 0) {
            float d = k ? 1.0f : 0.0f;
            col[v * 3] = mixf(0.01f, 0.1f, d), col[v * 3 + 1] = mixf(0.07f, 0.45f, d), col[v * 3 + 2] = mixf(0.2f, 0.6f, d);
          } else col[v * 3] = col[v * 3 + 1] = col[v * 3 + 2] = k ? 0.0f : 1.1f;
        }
      }
      for (int i = 0; i < segs; i++) {
        uint32_t a = (uint32_t)(i * 2), q[6] = { a, a + 2, a + 3, a, a + 3, a + 1 };
        memcpy(&idx[i * 6], q, sizeof q);
      }
      t3_geometry *wg = t3_geometry_new();
      t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, nv, 3);
      at->usage = T3_DYNAMIC_DRAW;
      t3_geometry_set_attribute(wg, T3_ATTR_POSITION, at), t3_release(at);
      at = t3_attribute_new(T3_FLOAT32, nor, nv, 3);
      t3_geometry_set_attribute(wg, T3_ATTR_NORMAL, at), t3_release(at);
      at = t3_attribute_new(T3_FLOAT32, col, nv, 3);
      t3_geometry_set_attribute(wg, T3_ATTR_COLOR, at), t3_release(at);
      at = t3_attribute_new(T3_FLOAT32, uv, nv, 2);
      at->usage = T3_DYNAMIC_DRAW;
      t3_geometry_set_attribute(wg, T3_ATTR_UV, at), t3_release(at);
      at = t3_attribute_new(T3_UINT32, idx, 6 * segs, 1);
      t3_geometry_set_index(wg, at), t3_release(at);
      free(nor), free(idx), free(col);
      add_mesh(wg, pass == 0 ? g_water_front : g_foam_m, 0, 0, 0, false);
      g_surf[g_nsurf++] = (anim_surf){ wg, pos, uv, nv, (float)x, (float)x1, topy, bottom, 1.25f, pass == 0 ? 1 : 2, segs };
    }
    x = x1 - 1;
  }
}

/* each frame: the lava heaves and flows, the water waves and its foam rides the crest */
void world_update_water_mesh(float time) {
  for (int k = 0; k < g_nsurf; k++) {
    anim_surf *s = &g_surf[k];
    if (s->kind == 0) { /* lava top: uv slide and a slow heave */
      int cols = s->cols, rows = 6;
      for (int j = 0; j <= rows; j++)
        for (int i = 0; i <= cols; i++) {
          int v = j * (cols + 1) + i;
          float x = s->pos[v * 3], z = s->pos[v * 3 + 2];
          s->pos[v * 3 + 1] = s->y_top + 0.06f * sinf(x * 1.3f + time * 1.1f) + 0.05f * sinf(z * 2.0f + x * 0.7f - time * 0.8f);
          s->uv[v * 2] = x / 3.0f + time * 0.02f;
          s->uv[v * 2 + 1] = z / 3.0f + time * 0.045f;
        }
    } else { /* water sheet or foam */
      for (int i = 0; i <= s->cols; i++) {
        float px = s->x0 + (float)i / 4.0f;
        float h = s->y_top - 0.06f + 0.07f * sinf(px * 1.7f + time * 2.1f) + 0.04f * sinf(px * 3.3f - time * 1.3f);
        s->pos[(i * 2 + 1) * 3 + 1] = s->kind == 1 ? h : h + 0.16f;
        if (s->kind == 2) s->pos[(i * 2) * 3 + 1] = h - 0.1f;
        s->uv[(i * 2) * 2] = px / 3.0f + time * 0.05f, s->uv[(i * 2 + 1) * 2] = px / 3.0f + time * 0.05f;
      }
    }
    t3_attribute *a = s->geo->attributes[T3_ATTR_POSITION];
    memcpy(a->array, s->pos, sizeof(float) * 3 * (size_t)s->nv);
    a->version++;
    a = s->geo->attributes[T3_ATTR_UV];
    if (a) {
      memcpy(a->array, s->uv, sizeof(float) * 2 * (size_t)s->nv);
      a->version++;
    }
  }
}

void world_hide_crack(int x, int y) {
  for (int i = 0; i < 64 && g_crack_mesh[i]; i++) {
    t3_mesh *m = g_crack_mesh[i];
    if ((int)floorf(m->base.position.x) == x && (int)floorf(m->base.position.y) == y) m->base.visible = false;
  }
}

/* ── backdrop: a sky of five colour bands, a sun, drifting clouds, and four layers of silhouettes per region at
 * different depths (the camera's perspective makes the parallax). Each region's layers rise out of the ground where
 * the region begins and sink again where it ends, so the scenery changes with the ground under the players. ── */

static const float SKY_HOR[BIOMES][3] = { { 0.95f, 0.88f, 0.75f }, { 0.07f, 0.1f, 0.2f }, { 0.72f, 0.85f, 0.7f }, { 1.0f, 0.4f, 0.12f } };
static const float SKY_MID[BIOMES][3] = { { 0.5f, 0.72f, 0.98f }, { 0.04f, 0.05f, 0.12f }, { 0.3f, 0.55f, 0.6f }, { 0.4f, 0.1f, 0.08f } };
static const float SKY_TOP[BIOMES][3] = { { 0.16f, 0.38f, 0.82f }, { 0.015f, 0.02f, 0.06f }, { 0.1f, 0.25f, 0.34f }, { 0.08f, 0.02f, 0.03f } };
/* silhouette colours per region and layer, far to near (already hazed toward the sky: far layers are pale) */
static const float LAYER_COL[BIOMES][4][3] = {
  { { 0.62f, 0.72f, 0.88f }, { 0.42f, 0.6f, 0.62f }, { 0.16f, 0.4f, 0.27f }, { 0.1f, 0.3f, 0.12f } },
  { { 0.08f, 0.1f, 0.2f }, { 0.06f, 0.08f, 0.16f }, { 0.05f, 0.06f, 0.13f }, { 0.04f, 0.05f, 0.1f } },
  { { 0.55f, 0.7f, 0.68f }, { 0.4f, 0.56f, 0.52f }, { 0.3f, 0.42f, 0.36f }, { 0.4f, 0.36f, 0.26f } },
  { { 0.45f, 0.14f, 0.1f }, { 0.3f, 0.08f, 0.07f }, { 0.18f, 0.06f, 0.06f }, { 0.12f, 0.06f, 0.06f } },
};
static const float LAYER_Z[4] = { -125, -85, -48, -9 };

static float g_bx0[BIOMES], g_bx1[BIOMES];
static float g_gs[WORLD_W + 1];
static t3_mesh *g_clouds[12];
static float g_cloud_x[12];
static int g_ncloud;
static t3_mesh *g_sun;

/* 0..1: how much of region b is present at x (1 inside, easing over 30 units at the borders) */
static float bmask(int b, float x) {
  float lo = b == 0 ? 1.0f : sstep(g_bx0[b] - 22.0f, g_bx0[b] + 22.0f, x);
  float hi = b == BIOMES - 1 ? 1.0f : 1.0f - sstep(g_bx1[b] - 22.0f, g_bx1[b] + 22.0f, x);
  return lo * hi;
}
static float ridge(float x, float sc, int s) { return 1.0f - fabsf(2.0f * fbm(x * sc, 0.31f, 8, 5, s) - 1.0f); }
static float smoothg(float x) {
  int i = (int)floorf(x);
  i = i < 0 ? 0 : i >= WORLD_W ? WORLD_W - 1 : i;
  return g_gs[i];
}
static float tri(float x) { x -= floorf(x); return x < 0.5f ? x * 2.0f : 2.0f - x * 2.0f; }

/* silhouette heights (world y of the top edge) per region and layer */
static float profile(int b, int l, float x) {
  float u = x / 300.0f;
  switch (b) {
    case BIOME_MEADOW:
      if (l == 0) return 24.0f + 34.0f * ridge(x, 1.0f / 260.0f, 80);
      if (l == 1) return 22.0f + 16.0f * fbm(u * 3.0f, 0.7f, 6, 4, 82);
      if (l == 2) return 20.0f + 7.0f * fbm(u * 6.0f, 0.2f, 8, 3, 84) + 5.0f * tri(x / 4.6f) * (fbm(u * 9.0f, 0.5f, 8, 2, 85) > 0.38f ? 1.0f : 0.25f);
      return smoothg(x) + 2.0f + 1.4f * fbm(x / 40.0f, 0.4f, 8, 3, 86);
    case BIOME_CAVES:
      if (l == 0) return 34.0f + 30.0f * ridge(x, 1.0f / 120.0f, 90);
      if (l == 1) return 28.0f + 22.0f * ridge(x, 1.0f / 70.0f, 91);
      if (l == 2) return 24.0f + 16.0f * ridge(x, 1.0f / 40.0f, 92);
      return smoothg(x) + 4.0f + 3.0f * ridge(x, 1.0f / 20.0f, 93);
    case BIOME_RUINS: {
      float fr = x / (l == 0 ? 9.0f : l == 1 ? 7.0f : l == 2 ? 11.0f : 3.0f);
      int cell = (int)floorf(fr);
      float h = hh(cell, 7, 120 + l);
      float within = fr - floorf(fr);
      if (l == 0) return 30.0f + 22.0f * h + (within > 0.3f && within < 0.7f ? 6.0f * h : 0.0f);                 /* towers */
      if (l == 1) return within < 0.34f ? 34.0f + 14.0f * h : 17.0f + 4.0f * h;                                   /* a colonnade */
      if (l == 2) return 22.0f + 12.0f * h * (within > 0.18f && within < 0.82f ? 1.0f : 0.5f);                    /* broken walls */
      return smoothg(x) + 2.5f + 3.0f * (h > 0.5f ? 1.0f : 0.0f) * (within < 0.7f ? 1.0f : 0.0f);               /* low wall */
    }
    default: {
      float cx = 0.5f * (g_bx0[BIOME_VOLCANO] + g_bx1[BIOME_VOLCANO]);
      if (l == 0) return 62.0f - 0.55f * fabsf(x - cx - 20.0f) + 4.0f * fbm(u * 12.0f, 0.3f, 8, 3, 140);       /* the cone */
      if (l == 1) return 20.0f + 36.0f * ridge(x, 1.0f / 90.0f, 141);
      if (l == 2) return 18.0f + 22.0f * ridge(x, 1.0f / 40.0f, 142) + 8.0f * tri(x / 7.0f);
      return smoothg(x) + 3.0f + 3.0f * ridge(x, 1.0f / 14.0f, 143);
    }
  }
}

static t3_mesh *strip(float z, float step, const float *topc, const float *botc, int b, int l, float glow_edge) {
  int n = (int)(((float)WORLD_W + 280.0f) / step) + 1;
  enum { R = 3 };
  float *pos = malloc(sizeof(float) * 3 * (size_t)n * R), *nor = malloc(sizeof(float) * 3 * (size_t)n * R), *col = malloc(sizeof(float) * 3 * (size_t)n * R);
  uint32_t *idx = malloc(sizeof(uint32_t) * 12 * (size_t)n);
  float fall = l == 3 ? 12.0f : l == 2 ? 20.0f : 30.0f;  /* how far under the top edge the colour reaches its dark end */
  for (int i = 0; i < n; i++) {
    float x = -140.0f + (float)i * step, m = bmask(b, x);
    float top = mixf(-45.0f, profile(b, l, x), m);
    float hv = fminf(1.0f, fmaxf(0.0f, (top - 20.0f) / 40.0f));
    float ys[R] = { top, top - fall, -50.0f };
    for (int k = 0; k < R; k++) {
      int v = i * R + k;
      pos[v * 3] = x, pos[v * 3 + 1] = fminf(ys[k], k ? ys[k] : ys[k]), pos[v * 3 + 2] = z;
      if (k && pos[v * 3 + 1] > top) pos[v * 3 + 1] = top;
      nor[v * 3] = 0, nor[v * 3 + 1] = 0, nor[v * 3 + 2] = 1;
      for (int c = 0; c < 3; c++) col[v * 3 + c] = k == 0 ? topc[c] * (0.9f + 0.25f * hv) : botc[c];
      if (glow_edge > 0 && k == 0) col[v * 3] += glow_edge * 0.6f, col[v * 3 + 1] += glow_edge * 0.12f;
    }
  }
  int k = 0;
  for (int i = 0; i + 1 < n; i++)
    for (int r = 0; r + 1 < R; r++) {
      uint32_t a = (uint32_t)(i * R + r), b2 = a + R, c = a + 1, d = b2 + 1;
      idx[k++] = a, idx[k++] = b2, idx[k++] = d, idx[k++] = a, idx[k++] = d, idx[k++] = c;
    }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, n * R, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, at), t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, nor, n * R, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, at), t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, col, n * R, 3);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, at), t3_release(at);
  at = t3_attribute_new(T3_UINT32, idx, k, 1);
  t3_geometry_set_index(g, at), t3_release(at);
  free(pos), free(nor), free(col), free(idx);
  t3_material *m = basic_vcol(false, false, 1);
  t3_mesh *me = add_mesh(g, m, 0, 0, 0, false);
  t3_release(g);
  return me;
}

static t3_texture *tex_alpha(int w, int h, float (*fn)(float u, float v)) {
  uint8_t *px = malloc((size_t)w * h * 4);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      float a = fn(((float)x + 0.5f) / (float)w, ((float)y + 0.5f) / (float)h);
      int i = (y * w + x) * 4;
      px[i] = px[i + 1] = px[i + 2] = 255, px[i + 3] = b8(a);
    }
  t3_texture *t = t3_texture_new(w, h, px);
  free(px);
  t->flip_y = false;
  t->generate_mipmaps = false;
  t->min_filter = t->mag_filter = T3_LINEAR;
  t->wrap_s = t->wrap_t = T3_CLAMP_TO_EDGE;
  t->color_space = T3_SRGB_COLOR_SPACE;
  return t;
}
static float radial_fn(float u, float v) {
  float d = sqrtf((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2.0f;
  float core = 1.0f - sstep(0.0f, 0.18f, d), halo = (1.0f - sstep(0.0f, 1.0f, d));
  return fminf(1.0f, core + halo * halo * 0.6f);
}
static float cloud_fn(float u, float v) {
  float e = (1.0f - sstep(0.25f, 1.0f, fabsf(u - 0.5f) * 2.0f)) * (1.0f - sstep(0.1f, 1.0f, fabsf(v - 0.5f) * 2.0f));
  float n = fbm(u, v, 5, 5, 150);
  return sstep(0.28f, 0.75f, n * e * 1.7f) * 0.9f;
}
static t3_mesh *sprite_quad(t3_texture *tex, float x, float y, float z, float w, float h, float r, float gc, float b2, bool additive, float opacity) {
  t3_geometry *g = t3_plane_geometry_new(w, h, 1, 1);
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->color = (t3_color){ r, gc, b2 };
  m->fog = false;
  m->transparent = true;
  m->depth_write = false;
  m->opacity = opacity;
  if (additive) m->blending = 2;
  t3_material_set_map(m, tex);
  t3_mesh *me = add_mesh(g, m, x, y, z, false);
  t3_release(g);
  return me;
}

void world_backdrop(game *g) {
  const level *L = &g->L;
  /* region extents and the ground envelope */
  /* region extents: the first run of columns of each region (a stray column of the default region inside the arena's
   * wall must not stretch the meadow) */
  for (int b = 0; b < BIOMES; b++) g_bx0[b] = -1.0f, g_bx1[b] = -1.0f;
  for (int x = 0; x < WORLD_W;) {
    int b = L->biome[x], x1 = x;
    while (x1 < WORLD_W && L->biome[x1] == b) x1++;
    if (g_bx0[b] < 0) g_bx0[b] = (float)x, g_bx1[b] = (float)x1;
    x = x1;
  }
  for (int b = 0; b < BIOMES; b++)
    if (g_bx0[b] < 0) g_bx0[b] = g_bx1[b] = (float)WORLD_W;
  for (int x = 0; x <= WORLD_W; x++) {
    float m = 0;
    for (int k = -7; k <= 7; k++) {
      int xx = x + k < 0 ? 0 : x + k >= WORLD_W ? WORLD_W - 1 : x + k;
      float gr = (float)L->ground[xx];
      if (gr < 40 && gr > m) m = gr;
    }
    g_gs[x] = m;
  }
  /* sky: a grid of colour bands, the colour of the region under each column */
  {
    enum { NX = 40, NY = 6 };
    float *pos = malloc(sizeof(float) * 3 * NX * NY), *nor = malloc(sizeof(float) * 3 * NX * NY), *col = malloc(sizeof(float) * 3 * NX * NY);
    uint32_t *idx = malloc(sizeof(uint32_t) * 6 * (NX - 1) * (NY - 1));
    static const float YS[NY] = { -60, 8, 28, 46, 78, 170 };
    for (int i = 0; i < NX; i++) {
      float x = -280.0f + (float)i * ((float)WORLD_W + 560.0f) / (float)(NX - 1);
      float w[BIOMES], tw = 0;
      for (int b = 0; b < BIOMES; b++) w[b] = bmask(b, x) + 0.001f, tw += w[b];
      float hor[3] = { 0 }, mid[3] = { 0 }, top[3] = { 0 };
      for (int b = 0; b < BIOMES; b++)
        for (int c = 0; c < 3; c++) hor[c] += SKY_HOR[b][c] * w[b] / tw, mid[c] += SKY_MID[b][c] * w[b] / tw, top[c] += SKY_TOP[b][c] * w[b] / tw;
      for (int j = 0; j < NY; j++) {
        int v = j * NX + i;
        pos[v * 3] = x, pos[v * 3 + 1] = YS[j], pos[v * 3 + 2] = -170;
        nor[v * 3] = 0, nor[v * 3 + 1] = 0, nor[v * 3 + 2] = 1;
        float t = (float)j / (NY - 1);
        const float *a = j <= 1 ? hor : j == 2 ? hor : j == 3 ? mid : j == 4 ? mid : top;
        const float *c2 = j <= 1 ? hor : j == 2 ? mid : j == 3 ? mid : j == 4 ? top : top;
        (void)t;
        float k = j == 2 ? 0.55f : j == 4 ? 0.6f : 0;
        for (int c = 0; c < 3; c++) col[v * 3 + c] = mixf(a[c], c2[c], k);
      }
    }
    int k = 0;
    for (int j = 0; j + 1 < NY; j++)
      for (int i = 0; i + 1 < NX; i++) {
        uint32_t a = (uint32_t)(j * NX + i), b2 = a + 1, c = a + NX, d = c + 1;
        idx[k++] = a, idx[k++] = b2, idx[k++] = d, idx[k++] = a, idx[k++] = d, idx[k++] = c;
      }
    t3_geometry *gs = t3_geometry_new();
    t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, NX * NY, 3);
    t3_geometry_set_attribute(gs, T3_ATTR_POSITION, at), t3_release(at);
    at = t3_attribute_new(T3_FLOAT32, nor, NX * NY, 3);
    t3_geometry_set_attribute(gs, T3_ATTR_NORMAL, at), t3_release(at);
    at = t3_attribute_new(T3_FLOAT32, col, NX * NY, 3);
    t3_geometry_set_attribute(gs, T3_ATTR_COLOR, at), t3_release(at);
    at = t3_attribute_new(T3_UINT32, idx, k, 1);
    t3_geometry_set_index(gs, at), t3_release(at);
    free(pos), free(nor), free(col), free(idx);
    add_mesh(gs, basic_vcol(false, false, 1), 0, 0, 0, false), t3_release(gs);
  }
  /* the sun over the meadow, a pale moon-glow over the caves' far end, an ash-red sun behind the volcano */
  t3_texture *rad = tex_alpha(128, 128, radial_fn);
  g_sun = sprite_quad(rad, 0.5f * (g_bx0[0] + g_bx1[0]) + 26.0f, 56.0f, -160.0f, 90, 90, 3.2f, 2.6f, 1.6f, true, 1.0f);
  sprite_quad(rad, 0.5f * (g_bx0[3] + g_bx1[3]) - 30.0f, 38.0f, -158.0f, 70, 70, 3.0f, 0.9f, 0.3f, true, 0.8f);
  sprite_quad(rad, 0.5f * (g_bx0[2] + g_bx1[2]) + 10.0f, 52.0f, -158.0f, 80, 80, 1.6f, 2.4f, 2.2f, true, 0.5f);
  t3_release(rad);
  /* clouds over the meadow and the ruins */
  t3_texture *cl = tex_alpha(128, 64, cloud_fn);
  g_ncloud = 0;
  for (int i = 0; i < 12; i++) {
    float x = -60.0f + (float)i * ((g_bx1[0] + 140.0f) / 8.0f) + 20.0f * hh(i, 1, 160);
    if (i >= 8) x = g_bx0[2] + (float)(i - 8) * 24.0f;
    bool ruin = i >= 8;
    float z = -100.0f - 20.0f * hh(i, 2, 160);
    g_clouds[g_ncloud] = sprite_quad(cl, x, 38.0f + 22.0f * hh(i, 3, 160), z, 70.0f + 40.0f * hh(i, 4, 160), 28.0f + 10.0f * hh(i, 5, 160), ruin ? 0.9f : 1.15f, ruin ? 1.05f : 1.12f, ruin ? 1.0f : 1.1f, false, ruin ? 0.55f : 0.85f);
    g_cloud_x[g_ncloud] = x;
    g_ncloud++;
  }
  t3_release(cl);
  /* silhouette layers: for each region, far to near */
  for (int b = 0; b < BIOMES; b++)
    for (int l = 0; l < 4; l++) {
      float bot[3];
      for (int c = 0; c < 3; c++) bot[c] = LAYER_COL[b][l][c] * (l == 3 ? 0.45f : l == 2 ? 0.6f : 0.75f);
      float step = l == 3 ? 1.0f : l == 2 ? 1.5f : 3.0f;
      strip(LAYER_Z[l], step, LAYER_COL[b][l], bot, b, l, b == BIOME_VOLCANO && l < 3 ? 0.35f : 0.0f);
    }
  /* a hot glow at the foot of the volcano range */
  {
    enum { N = 60 };
    float *pos = malloc(sizeof(float) * 3 * N * 2), *nor = malloc(sizeof(float) * 3 * N * 2), *col = malloc(sizeof(float) * 3 * N * 2);
    uint32_t *idx = malloc(sizeof(uint32_t) * 6 * N);
    for (int i = 0; i < N; i++) {
      float x = g_bx0[3] - 60.0f + (float)i * (g_bx1[3] - g_bx0[3] + 140.0f) / (float)(N - 1);
      float m = bmask(3, x);
      for (int k = 0; k < 2; k++) {
        int v = i * 2 + k;
        pos[v * 3] = x, pos[v * 3 + 1] = k ? 26.0f : -4.0f, pos[v * 3 + 2] = -60.0f;
        nor[v * 3] = 0, nor[v * 3 + 1] = 0, nor[v * 3 + 2] = 1;
        float s = k ? 0.0f : 1.0f;
        col[v * 3] = 1.7f * s * m, col[v * 3 + 1] = 0.45f * s * m, col[v * 3 + 2] = 0.06f * s * m;
      }
    }
    for (int i = 0; i + 1 < N; i++) {
      uint32_t a = (uint32_t)(i * 2), q[6] = { a, a + 2, a + 3, a, a + 3, a + 1 };
      memcpy(&idx[i * 6], q, sizeof q);
    }
    t3_geometry *gg = t3_geometry_new();
    t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, N * 2, 3);
    t3_geometry_set_attribute(gg, T3_ATTR_POSITION, at), t3_release(at);
    at = t3_attribute_new(T3_FLOAT32, nor, N * 2, 3);
    t3_geometry_set_attribute(gg, T3_ATTR_NORMAL, at), t3_release(at);
    at = t3_attribute_new(T3_FLOAT32, col, N * 2, 3);
    t3_geometry_set_attribute(gg, T3_ATTR_COLOR, at), t3_release(at);
    at = t3_attribute_new(T3_UINT32, idx, 6 * (N - 1), 1);
    t3_geometry_set_index(gg, at), t3_release(at);
    free(pos), free(nor), free(col), free(idx);
    add_mesh(gg, basic_vcol(true, false, 1), 0, 0, 0, false), t3_release(gg);
  }
  /* the near scenery that is lit and shadowed: trees behind the meadow, colonnades behind the ruins */
  {
    mb back = { 0 }, backglow = { 0 };
    for (int x = 4; x < (int)g_bx1[0] + 10 && x < WORLD_W; x += 4 + (int)(hh(x, 1, 170) * 4)) {
      float top = profile(BIOME_MEADOW, 3, (float)x);
      if (bmask(0, (float)x) < 0.5f) continue;
      tree(&back, (float)x, top - 0.6f, -8.0f - 3.0f * hh(x, 2, 170), x, 0.9f + 0.6f * hh(x, 3, 170));
    }
    for (int x = (int)g_bx0[2]; x < (int)g_bx1[2]; x += 7 + (int)(hh(x, 1, 171) * 5)) {
      float top = smoothg((float)x);
      column_prop(&back, (float)x, top - 0.8f, -5.5f - 2.5f * hh(x, 2, 171), 3.5f + 4.0f * hh(x, 3, 171), x, hh(x, 4, 171) > 0.45f);
    }
    for (int x = (int)g_bx0[3]; x < (int)g_bx1[3] - 20; x += 6 + (int)(hh(x, 1, 172) * 4)) {
      float top = profile(BIOME_VOLCANO, 3, (float)x);
      lava_spire(&back, &backglow, (float)x, top - 0.5f, -6.0f - 3.0f * hh(x, 2, 172), x, 1.6f + hh(x, 3, 172));
    }
    for (int x = (int)g_bx0[1]; x < (int)g_bx1[1]; x += 5 + (int)(hh(x, 1, 173) * 4)) {
      crystal_cluster(&backglow, &back, (float)x, smoothg((float)x) + 0.5f, -4.0f - 2.0f * hh(x, 2, 173), x, 1.4f + hh(x, 3, 173), x / 25, false);
    }
    t3_geometry *gb = mb_done(&back);
    if (gb) add_mesh(gb, g_mat[MT_PROP], 0, 0, 0, true), t3_release(gb);
    gb = mb_done(&backglow);
    if (gb) add_mesh(gb, g_mat[MT_GLOW], 0, 0, 0, false), t3_release(gb);
  }
  /* the back wall of the caves: rock, lit by the torches */
  {
    mb wall = { 0 };
    for (int b = 1; b <= 1; b++) {
      float x0 = g_bx0[b] - 2, x1 = g_bx1[b] + 2;
      float c[4][3], p[4][3] = { { x0, 0, -2.6f }, { x1, 0, -2.6f }, { x1, (float)WORLD_H, -2.6f }, { x0, (float)WORLD_H, -2.6f } };
      float n[3] = { 0, 0, 1 };
      float uv[4][2] = { { x0 / 5, 0 }, { x1 / 5, 0 }, { x1 / 5, (float)WORLD_H / 5 }, { x0 / 5, (float)WORLD_H / 5 } };
      for (int i = 0; i < 4; i++) shade(c[i], STONE[1], 0.32f);
      mb_quad(&wall, p, n, c, uv);
    }
    t3_geometry *gw = mb_done(&wall);
    if (gw) add_mesh(gw, g_mat[MT_ROCK], 0, 0, 0, false), t3_release(gw);
  }
}

/* drift the clouds and let the sun breathe */
void world_update_backdrop(float time) {
  for (int i = 0; i < g_ncloud; i++)
    if (g_clouds[i]) g_clouds[i]->base.position.x = g_cloud_x[i] + 3.0f * time * (0.4f + 0.1f * (float)(i % 4));
  if (g_sun) g_sun->base.scale = (t3_vec3){ 1.0f + 0.02f * sinf(time * 0.7f), 1.0f + 0.02f * sinf(time * 0.7f), 1 };
}
