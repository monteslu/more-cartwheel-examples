/* fighter textures: every surface gets a full PBR set made here at load time:
 * albedo (sRGB), a tangent-space normal map from a height field, roughness and
 * ambient occlusion. They tile without seams (periodic noise), are mipmapped
 * with anisotropy, and are made once. Integer hashing and cartwheel/math.h
 * only, so every host makes the same texels (debug field tex.hash). */
#include <stdlib.h>
#include <string.h>

#include "game.h"

#define N 256
uint32_t tex_hash = 2166136261u;

static uint32_t hash2(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float h01(int x, int y, uint32_t seed) { return (float)(hash2(x, y, seed) & 0xffffff) / 16777216.0f; }
static float smooth(float t) { return t * t * (3 - 2 * t); }
static int wrapi(int a, int p) { return ((a % p) + p) % p; }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static uint8_t b8(float v) { return (uint8_t)(clamp01(v) * 255.0f + 0.5f); }

static float vnoise(float x, float y, int p, uint32_t seed) {
  int x0 = (int)x, y0 = (int)y;
  if (x < 0) x0--;
  if (y < 0) y0--;
  float fx = smooth(x - (float)x0), fy = smooth(y - (float)y0);
  float a = h01(wrapi(x0, p), wrapi(y0, p), seed), b = h01(wrapi(x0 + 1, p), wrapi(y0, p), seed);
  float c = h01(wrapi(x0, p), wrapi(y0 + 1, p), seed), d = h01(wrapi(x0 + 1, p), wrapi(y0 + 1, p), seed);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float u, float v, int p, int oct, uint32_t seed) {
  float s = 0, amp = 0.5f, norm = 0;
  for (int o = 0; o < oct; o++) {
    s += amp * vnoise(u * (float)p, v * (float)p, p, seed + (uint32_t)o * 101u);
    norm += amp;
    amp *= 0.5f;
    p *= 2;
  }
  return s / norm;
}

typedef struct texel { float r, g, b, h, rough, ao; } texel;

static void gen(int kind, float u, float v, int c, int r, texel *t) {
  float n1 = fbm(u, v, 8, 4, 11), n2 = fbm(u, v, 32, 2, 17);
  switch (kind) {
  case TEX_CONCRETE: {
    float pits = h01(c, r, 5) > 0.985f ? 0.5f : 0.0f;
    float k = 0.50f + 0.25f * n1 + 0.08f * n2 - pits * 0.3f;
    *t = (texel){ k * 0.92f, k * 0.9f, k * 0.86f, 0.5f * n1 + 0.3f * n2 - pits, 0.78f + 0.2f * n2, 1 - 0.5f * pits - 0.2f * (1 - n1) };
    break;
  }
  case TEX_TILE: {
    int tiles = 4;
    float fu = u * (float)tiles, fv = v * (float)tiles;
    float gu = fu - (float)(int)fu, gv = fv - (float)(int)fv;
    float edge = fminf(fminf(gu, 1 - gu), fminf(gv, 1 - gv));
    float grout = 1 - clamp01(edge / 0.04f);
    uint32_t id = hash2((int)fu, (int)fv, 3);
    float tone = 0.38f + 0.12f * ((float)(id & 255) / 255.0f) + 0.05f * n2;
    float wear = n1;
    *t = (texel){ tone * (1 - 0.7f * grout) * 1.05f, tone * (1 - 0.7f * grout), tone * 0.9f * (1 - 0.7f * grout), (1 - grout) * (0.6f + 0.2f * wear) + 0.05f * n2,
                  0.45f + 0.4f * grout + 0.2f * wear, 1 - 0.8f * grout };
    break;
  }
  case TEX_WOOD: {
    int planks = 6;
    float fu = u * (float)planks;
    int pi = (int)fu;
    float gu = fu - (float)pi;
    float edge = fminf(gu, 1 - gu);
    float grout = 1 - clamp01(edge / 0.03f);
    float grain = 0.5f + 0.5f * (float)cw_sin((v * 6 + 3.0f * vnoise(u * 8, v * 3, 8, 21 + (uint32_t)pi)) * 6.2831853f * 4 / 4 + h01(pi, 0, 9) * 6.28f);
    float ring = fbm(u * 0.25f + (float)pi * 0.37f, v, 4, 3, 33);
    float tone = 0.32f + 0.14f * grain + 0.1f * ring + 0.06f * h01(pi, 1, 4);
    *t = (texel){ tone * 1.15f * (1 - 0.6f * grout), tone * 0.78f * (1 - 0.6f * grout), tone * 0.5f * (1 - 0.6f * grout), 0.4f * grain + 0.3f * (1 - grout) - 0.2f * grout,
                  0.45f + 0.25f * (1 - grain) + 0.3f * grout, 1 - 0.7f * grout };
    break;
  }
  case TEX_METAL: {
    float brush = vnoise(u * 2, v * 128, 128, 41);
    float panel = 4;
    float gu = u * panel - (float)(int)(u * panel), gv = v * panel - (float)(int)(v * panel);
    float seam = 1 - clamp01(fminf(fminf(gu, 1 - gu), fminf(gv, 1 - gv)) / 0.02f);
    float rivet = 0;
    for (int a = 0; a < 2; a++)
      for (int b = 0; b < 2; b++) {
        float dx = gu - (a ? 0.9f : 0.1f), dy = gv - (b ? 0.9f : 0.1f);
        rivet = fmaxf(rivet, 1 - clamp01(sqrtf(dx * dx + dy * dy) / 0.035f));
      }
    float k = 0.55f + 0.12f * brush + 0.1f * n1 - 0.3f * seam;
    *t = (texel){ k * 0.95f, k, k * 1.05f, 0.25f * brush + rivet * 0.8f - seam * 0.6f, 0.3f + 0.25f * (1 - brush) + 0.3f * n1, 1 - 0.8f * seam };
    break;
  }
  case TEX_BRICK: {
    int rows = 8, cols = 4;
    float fv = v * (float)rows;
    int ri = (int)fv;
    float fu = u * (float)cols + (ri & 1 ? 0.5f : 0.0f);
    float gu = fu - (float)(int)fu, gv = fv - (float)ri;
    float edge = fminf(fminf(gu, 1 - gu) * 2.0f, fminf(gv, 1 - gv) * 3.0f);
    float mortar = 1 - clamp01(edge / 0.08f);
    uint32_t id = hash2((int)fu, ri, 7);
    float tone = 0.35f + 0.12f * ((float)(id & 255) / 255.0f) + 0.08f * n1;
    *t = (texel){ tone * 1.25f * (1 - mortar) + 0.55f * mortar, tone * 0.62f * (1 - mortar) + 0.52f * mortar, tone * 0.5f * (1 - mortar) + 0.48f * mortar,
                  (1 - mortar) * (0.55f + 0.3f * n2) + 0.05f, 0.7f + 0.25f * mortar + 0.1f * n2, 1 - 0.7f * mortar };
    break;
  }
  default: { /* fabric: a woven twill */
    float wu = u * 24, wv = v * 24;
    float a = 0.5f + 0.5f * (float)cw_sin((wu + wv) * 3.14159265f);
    float b = 0.5f + 0.5f * (float)cw_sin((wu - wv) * 3.14159265f);
    float k = 0.8f + 0.12f * (a * b) + 0.06f * n2;
    *t = (texel){ k, k, k, 0.5f * (a * 0.5f + b * 0.5f), 0.7f + 0.25f * n2, 0.8f + 0.2f * a * b };
    break;
  }
  }
}

static t3_texture *finish(const uint8_t *px, bool srgb) {
  t3_texture *t = t3_texture_new(N, N, px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 8;
  t->color_space = srgb ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
  return t;
}
static uint32_t fnv(const uint8_t *d, size_t n, uint32_t h) {
  for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
  return h;
}

static tex_set SETS[TEX_KINDS];
static const tex_set *tex_get(int kind) {
  tex_set *s = &SETS[kind];
  if (s->map) return s;
  texel *tx = malloc(sizeof(texel) * N * N);
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) gen(kind, ((float)c + 0.5f) / N, ((float)r + 0.5f) / N, c, r, &tx[r * N + c]);
  uint8_t *a = malloc(N * N * 4), *nm = malloc(N * N * 4), *ro = malloc(N * N * 4), *ao = malloc(N * N * 4);
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) {
      const texel *t = &tx[r * N + c];
      float hx = tx[r * N + (c + 1) % N].h - tx[r * N + (c + N - 1) % N].h;
      float hy = tx[((r + 1) % N) * N + c].h - tx[((r + N - 1) % N) * N + c].h;
      float nx = -hx * 4.0f, ny = -hy * 4.0f, nz = 1;
      float l = sqrtf(nx * nx + ny * ny + nz * nz);
      int i = (r * N + c) * 4;
      a[i] = b8(t->r); a[i + 1] = b8(t->g); a[i + 2] = b8(t->b); a[i + 3] = 255;
      nm[i] = b8(nx / l * 0.5f + 0.5f); nm[i + 1] = b8(ny / l * 0.5f + 0.5f); nm[i + 2] = b8(nz / l * 0.5f + 0.5f); nm[i + 3] = 255;
      ro[i] = ro[i + 1] = ro[i + 2] = b8(t->rough); ro[i + 3] = 255;
      ao[i] = ao[i + 1] = ao[i + 2] = b8(t->ao); ao[i + 3] = 255;
    }
  tex_hash = fnv(a, N * N * 4, tex_hash);
  tex_hash = fnv(nm, N * N * 4, tex_hash);
  s->map = finish(a, true);
  s->normal = finish(nm, false);
  s->rough = finish(ro, false);
  s->ao = finish(ao, false);
  free(tx); free(a); free(nm); free(ro); free(ao);
  return s;
}

typedef struct rep_set { int kind; float ru, rv; t3_texture *t[4]; } rep_set;
static rep_set REP[48];
static int nrep;

void tex_apply(t3_material *m, int kind, float ru, float rv, float normal_scale, float ao_intensity) {
  const tex_set *s = tex_get(kind);
  t3_texture *ts[4] = { s->map, s->normal, s->rough, s->ao };
  if (ru != 1 || rv != 1) {
    rep_set *r = NULL;
    for (int k = 0; k < nrep && !r; k++)
      if (REP[k].kind == kind && REP[k].ru == ru && REP[k].rv == rv) r = &REP[k];
    if (!r && nrep < (int)(sizeof REP / sizeof REP[0])) {
      r = &REP[nrep++];
      *r = (rep_set){ kind, ru, rv, { 0 } };
      for (int i = 0; i < 4; i++) {
        r->t[i] = finish(ts[i]->pixels, i == 0);
        r->t[i]->repeat = (t3_vec2){ ru, rv };
      }
    }
    if (r)
      for (int i = 0; i < 4; i++) ts[i] = r->t[i];
  }
  static const t3_map_slot SLOTS[4] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP, T3_AO_MAP };
  for (int i = 0; i < 4; i++) t3_material_set_texture(m, SLOTS[i], ts[i]);
  m->normal_scale = (t3_vec2){ normal_scale, normal_scale };
  m->ao_map_intensity = ao_intensity;
}

void tex_free_all(void) {
  for (int k = 0; k < nrep; k++)
    for (int i = 0; i < 4; i++) t3_release(REP[k].t[i]);
  nrep = 0;
  for (int k = 0; k < TEX_KINDS; k++) {
    tex_set *s = &SETS[k];
    if (!s->map) continue;
    t3_release(s->map); t3_release(s->normal); t3_release(s->rough); t3_release(s->ao);
    memset(s, 0, sizeof *s);
  }
}

t3_color lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}
