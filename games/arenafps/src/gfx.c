/* procedural PBR texture sets and a small mesh builder (the same generators as the demolition derby example: our own code, MIT). */
/* derby textures: every surface gets a full PBR set made here at load time
 * (no image files): albedo (sRGB), a tangent-space normal map from a height
 * field, roughness and ambient occlusion. All tile without seams (periodic
 * noise), are mipmapped with 8x anisotropy, and are made once, on first
 * use, and shared. Integer hashing and cartwheel/math.h only, so every host
 * makes the same texels (debug field tex.hash).
 *
 * The look is grime: churned dirt with tyre ruts and puddles, stained and
 * chipped concrete, worn tread, paint with scuffs and grime that the dents
 * strip back to rust, galvanised steel, weathered planks. */
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

#define N 512

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
static float fractf(float x) { return x - floorf(x); }

/* periodic value noise: period p cells across the texture */
static float vnoise(float x, float y, int p, uint32_t seed) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = smooth(x - x0), fy = smooth(y - y0);
  float a = h01(wrapi(x0, p), wrapi(y0, p), seed), b = h01(wrapi(x0 + 1, p), wrapi(y0, p), seed);
  float c = h01(wrapi(x0, p), wrapi(y0 + 1, p), seed), d = h01(wrapi(x0 + 1, p), wrapi(y0 + 1, p), seed);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float u, float v, int p, int oct, uint32_t seed) {
  float s = 0, amp = 0.5f, norm = 0;
  for (int o = 0; o < oct; o++) {
    s += amp * vnoise(u * p, v * p, p, seed + (uint32_t)o * 101u);
    norm += amp;
    amp *= 0.5f;
    p *= 2;
  }
  return s / norm;
}
static void cells(float u, float v, int p, uint32_t seed, float *d1, float *d2, uint32_t *id) {
  float x = u * p, y = v * p;
  int cx = (int)floorf(x), cy = (int)floorf(y);
  *d1 = *d2 = 9;
  for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
      int gx = cx + i, gy = cy + j;
      float px = gx + h01(wrapi(gx, p), wrapi(gy, p), seed), py = gy + h01(wrapi(gx, p), wrapi(gy, p), seed + 7);
      float d = sqrtf((px - x) * (px - x) + (py - y) * (py - y));
      if (d < *d1) {
        *d2 = *d1;
        *d1 = d;
        *id = hash2(wrapi(gx, p), wrapi(gy, p), seed + 13);
      } else if (d < *d2) *d2 = d;
    }
}

typedef struct texel { float r, g, b, a, h, rough, ao; } texel;
typedef void (*gen_fn)(float u, float v, texel *t);
static void rgb(texel *t, float r, float g, float b) { t->r = r, t->g = g, t->b = b; }

/* ── generators: u, v in 0..1 -> colour (screen sRGB), height, roughness, ao ── */

/* the arena floor: churned mud, tyre tracks pressed in it (soft-edged,
 * crossing), clods and stones, wet patches that go dark and glossy */
static void g_dirt(float u, float v, texel *t) {
  float n1 = fbm(u, v, 4, 5, 11), n2 = fbm(u, v, 24, 4, 12), n3 = fbm(u, v, 64, 2, 16);
  float wet = clamp01((fbm(u, v, 2, 3, 13) - 0.52f) * 4);
  /* tracks: a few wide, soft furrows wandering in two directions */
  float rut = 0;
  for (int k = 0; k < 4; k++) {
    float w = fbm(u, v, 2, 2, 14 + (uint32_t)k) * 0.9f;
    float c = fractf((k & 1 ? v : u) * 2 + w + k * 0.31f);
    float d = fabsf(c - 0.5f);
    rut += clamp01(1 - d * 7) * clamp01(1 - d * 7) * (0.35f + 0.65f * fbm(u, v, 8, 2, 40 + (uint32_t)k));
  }
  rut = clamp01(rut) * 0.8f;
  float d1, d2;
  uint32_t id;
  cells(u, v, 48, 15, &d1, &d2, &id);
  float pebble = (id & 15) == 0 ? clamp01(1 - d1 * 3.2f) : 0;
  float clod = clamp01((n2 - 0.62f) * 5) * (1 - wet);
  float k = 0.72f + 0.28f * n1 + 0.1f * (n3 - 0.5f) - 0.1f * rut + 0.08f * clod;
  rgb(t, 0.36f * k, 0.27f * k, 0.2f * k);
  t->r = mixf(t->r, t->r * 0.5f, wet), t->g = mixf(t->g, t->g * 0.52f, wet), t->b = mixf(t->b, t->b * 0.58f, wet);
  if (pebble > 0) {
    float pc = 0.4f + 0.18f * (float)((id >> 4) & 255) / 255.0f;
    t->r = mixf(t->r, pc, pebble), t->g = mixf(t->g, pc * 0.95f, pebble), t->b = mixf(t->b, pc * 0.9f, pebble);
  }
  t->h = 0.5f + 0.3f * n2 + 0.1f * n3 - 0.3f * rut + 0.35f * pebble + 0.2f * clod;
  t->rough = mixf(0.94f, 0.18f, wet) - 0.15f * pebble;
  t->ao = 0.75f + 0.25f * (1 - rut * 0.6f) - 0.1f * pebble;
}

/* jersey barriers: cast concrete, stained, chipped, a little moss low down */
static void g_concrete(float u, float v, texel *t) {
  float n = fbm(u, v, 8, 5, 21), pit = h01((int)(u * N), (int)(v * N), 22) > 0.982f;
  float stain = clamp01((fbm(u, v, 3, 3, 23) - 0.5f) * 3);
  float streak = clamp01((fbm(u * 4, v, 4, 3, 24) - 0.55f) * 4); /* rain runs down */
  float d1, d2;
  uint32_t id;
  cells(u, v, 6, 25, &d1, &d2, &id);
  float chip = (id & 7) == 0 ? clamp01(1 - d1 * 2.6f) : 0;
  float c = 0.6f + 0.12f * n - 0.16f * stain - 0.12f * streak;
  rgb(t, c, c * 0.98f, c * 0.94f);
  t->r -= 0.05f * chip, t->g -= 0.05f * chip;
  t->h = 0.5f + 0.25f * n - 0.3f * pit - 0.35f * chip;
  t->rough = 0.9f;
  t->ao = 0.85f + 0.15f * n - 0.3f * pit - 0.2f * chip;
}

/* a tyre, laid out as its lathe's uv: u round the tyre (six repeats), v
 * across the cross section: sidewall, tread blocks in the middle, sidewall.
 * The sidewalls carry raised lettering-like blocks (no words, no brands);
 * mud caked into the tread and up the walls */
static void g_rubber(float u, float v, texel *t) {
  float n = fbm(u, v, 16, 3, 31), mud = clamp01((fbm(u, v, 4, 3, 32) - 0.4f) * 2.2f);
  float h = 0.5f, ao = 1, rough = 0.9f;
  if (v > 0.3f && v < 0.7f) {
    /* the tread: staggered blocks with sipes */
    float tv = (v - 0.3f) / 0.4f;
    float bu = fractf(u * 10 + (tv > 0.5f ? 0.5f : 0)), bv = fractf(tv * 3);
    float groove = fminf(clamp01(fminf(bu, 1 - bu) * 12), clamp01(fminf(bv, 1 - bv) * 9));
    float sipe = fabsf(fractf(u * 30) - 0.5f) < 0.04f ? 0.5f : 1;
    h = groove * sipe;
    ao = 0.4f + 0.6f * groove;
    mud *= 1.3f - groove * 0.6f; /* packed into the grooves */
  } else {
    /* sidewall: a ring of raised glyph blocks, then smooth rubber */
    float sv = v < 0.3f ? v / 0.3f : (1 - v) / 0.3f;
    float band = sv > 0.35f && sv < 0.6f ? 1 : 0;
    float gu = fractf(u * 28);
    uint32_t id = (uint32_t)floorf(u * 28) * 7u + 3u;
    float on = (h01((int)id, 0, 33) > 0.25f && gu > 0.15f && gu < 0.85f) ? 1 : 0;
    float row = h01((int)id, 1, 34);
    if (row > 0.5f && sv > 0.46f && sv < 0.5f) on = 0; /* a stroke gap: letters, not bars */
    h = 0.5f + 0.25f * band * on + 0.05f * n;
    ao = 0.85f + 0.15f * n;
    mud *= 0.5f + 0.5f * (1 - sv);
  }
  mud = clamp01(mud);
  float c = 0.065f + 0.03f * n;
  rgb(t, mixf(c, 0.26f, mud), mixf(c, 0.2f, mud), mixf(c, 0.14f, mud));
  t->h = h;
  t->rough = mixf(rough, 0.97f, mud);
  t->ao = ao;
}

/* car paint (near white: the instance / vertex colour is the paint): orange
 * peel, scuffs, grime from the sills up */
static void g_paint(float u, float v, texel *t) {
  float n = fbm(u, v, 32, 2, 41), grime = clamp01((fbm(u, v, 10, 4, 42) - 0.55f) * 2.0f) * 0.7f; /* fine grime, not blotches */
  float scratch = 0;
  for (int k = 0; k < 4; k++) {
    float a = h01(k, 0, 43) * 3.14f, s, c;
    cw_sincos(a, &s, &c);
    float l = fractf((u * c + v * s) * (5 + k * 2) + fbm(u, v, 4, 2, 44 + (uint32_t)k) * 0.3f);
    scratch += 0.5f * cw_pow(1 - clamp01(fabsf(l - 0.5f) * 60), 3) * (fbm(u, v, 6, 2, 50 + (uint32_t)k) > 0.68f);
  }
  scratch = clamp01(scratch);
  float k = 0.9f + 0.06f * n - 0.3f * grime;
  float mott = fbm(u, v, 24, 3, 45); /* oxidised paint, finely mottled */
  /* swirl marks: fine concentric scratches from a buffer, in a few patches */
  float swirl = 0;
  for (int k = 0; k < 4; k++) {
    float cx = h01(k, 5, 46), cy = h01(k, 6, 46);
    float dx = fractf(u - cx + 0.5f) - 0.5f, dy = fractf(v - cy + 0.5f) - 0.5f, d = sqrtf(dx * dx + dy * dy);
    if (d < 0.22f) swirl += cw_pow(1 - clamp01(fabsf(fractf(d * 140 + fbm(u, v, 8, 1, 47 + (uint32_t)k)) - 0.5f) * 5), 6) * (1 - d / 0.22f);
  }
  scratch = clamp01(scratch + swirl * 0.4f);
  k *= 0.95f + 0.06f * mott;
  rgb(t, mixf(k, 0.62f, scratch), mixf(k * 0.98f, 0.6f, scratch), mixf(k * 0.95f, 0.58f, scratch));
  t->h = 0.5f + 0.05f * n - 0.2f * scratch + 0.04f * mott;
  t->rough = 0.42f + 0.3f * grime + 0.25f * scratch + 0.15f * mott;
  t->ao = 0.9f + 0.1f * n;
}

/* bare steel gone to rust: blisters of bubbled paint lifting at the edge
 * of the rot, flaking scale, orange blooms, pits */
static void g_rust(float u, float v, texel *t) {
  float n = fbm(u, v, 8, 5, 61), bloom = clamp01((fbm(u, v, 4, 4, 62) - 0.4f) * 2.5f);
  float d1, d2;
  uint32_t id;
  cells(u, v, 20, 63, &d1, &d2, &id);
  float flake = clamp01((d2 - d1) * 6);
  /* blisters: round domes on a cell grid */
  float b1, b2;
  uint32_t bid;
  cells(u, v, 36, 64, &b1, &b2, &bid);
  float blister = (bid & 3) == 0 ? clamp01(1 - b1 * 2.4f) : 0;
  blister = blister * blister * (3 - 2 * blister);
  float pit = h01((int)(u * N), (int)(v * N), 65) > 0.97f ? 1 : 0;
  rgb(t, mixf(0.36f, 0.5f, bloom) + 0.08f * n, mixf(0.33f, 0.24f, bloom) + 0.04f * n, mixf(0.31f, 0.12f, bloom));
  t->h = 0.45f + 0.25f * n + 0.2f * flake * bloom + 0.35f * blister - 0.25f * pit;
  t->rough = mixf(0.6f, 0.94f, bloom) - 0.1f * blister;
  t->ao = 0.65f + 0.35f * flake - 0.3f * pit;
}

/* primer: flat, sanded in swirls, a few runs */
static void g_primer(float u, float v, texel *t) {
  float n = fbm(u, v, 12, 4, 131);
  float sand = 0;
  for (int k = 0; k < 6; k++) {
    float cx = h01(k, 0, 132), cy = h01(k, 1, 132), r = 0.08f + 0.1f * h01(k, 2, 132);
    float dx = fractf(u - cx + 0.5f) - 0.5f, dy = fractf(v - cy + 0.5f) - 0.5f, d = sqrtf(dx * dx + dy * dy);
    sand += cw_pow(1 - clamp01(fabsf(fractf(d * 90) - 0.5f) * 4), 4) * (d < r ? 1 : 0) * 0.5f;
  }
  float c = 0.86f + 0.08f * n - 0.05f * sand;
  rgb(t, c, c, c * 0.98f);
  t->h = 0.5f + 0.06f * n - 0.04f * sand;
  t->rough = 0.86f + 0.08f * n;
  t->ao = 0.95f;
}

/* galvanised steel: spangle, scratches, a little rust at the seams */
static void g_steel(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 12, 71, &d1, &d2, &id);
  float sp = (float)(id & 255) / 255.0f, n = fbm(u, v, 8, 4, 72);
  float rust = clamp01((fbm(u, v, 3, 3, 73) - 0.62f) * 4);
  float c = 0.55f + 0.12f * sp + 0.08f * n;
  rgb(t, mixf(c, 0.45f, rust), mixf(c, 0.28f, rust), mixf(c * 1.02f, 0.15f, rust));
  t->h = 0.5f + 0.1f * sp + 0.2f * rust * n;
  t->rough = mixf(0.38f + 0.2f * n, 0.9f, rust);
  t->ao = 0.85f + 0.15f * n;
}

/* old planks (bleachers): grain, grey weathering, gaps */
static void g_wood(float u, float v, texel *t) {
  float row = floorf(v * 5), x = u * 1 + h01((int)row, 0, 81) * 3;
  float seg = fractf(x);
  float warp = fbm(u, v, 4, 3, 82 + (uint32_t)row) * 2;
  float line = cw_pow(1 - fabsf(fractf(u * 6 + warp) - 0.5f) * 2, 6);
  float seam = clamp01(fminf(fractf(v * 5), 1 - fractf(v * 5)) * 50) * clamp01(fminf(seg, 1 - seg) * 200);
  float grey = 0.55f + 0.3f * fbm(u, v, 8, 2, 83);
  float k = 0.8f + 0.25f * h01((int)row, (int)floorf(x), 84);
  float r = mixf(0.55f, 0.4f, line) * k, g = mixf(0.42f, 0.3f, line) * k, b = mixf(0.3f, 0.2f, line) * k;
  float l = (r + g + b) / 3;
  rgb(t, mixf(r, l, grey), mixf(g, l, grey), mixf(b, l * 1.03f, grey));
  t->h = 0.55f - line * 0.1f - (1 - seam) * 0.45f;
  t->rough = 0.85f;
  t->ao = 0.55f + 0.45f * seam;
}

/* the lot outside: gravel and tar patches */
static void g_gravel(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 64, 91, &d1, &d2, &id);
  float stone = clamp01((d2 - d1) * 5), tone = (float)(id & 255) / 255.0f;
  float tar = clamp01((fbm(u, v, 3, 3, 92) - 0.6f) * 5);
  float c = 0.32f + 0.18f * tone;
  rgb(t, mixf(c * 1.02f, 0.08f, tar), mixf(c, 0.08f, tar), mixf(c * 0.95f, 0.085f, tar));
  t->h = mixf(stone * 0.8f, 0.3f, tar);
  t->rough = mixf(0.9f, 0.55f, tar);
  t->ao = mixf(0.5f + 0.5f * stone, 0.9f, tar);
}

/* late-summer grass: dry, patchy */
static void g_grass(float u, float v, texel *t) {
  float n1 = fbm(u, v, 4, 4, 101), n2 = fbm(u, v, 32, 3, 102), blade = h01((int)(u * N), (int)(v * N), 103);
  float dry = clamp01((fbm(u, v, 3, 3, 104) - 0.35f) * 2);
  float k = 0.75f + 0.3f * n1 + 0.15f * (blade - 0.5f);
  rgb(t, mixf(0.3f, 0.55f, dry) * k, mixf(0.42f, 0.46f, dry) * k, mixf(0.16f, 0.22f, dry) * k);
  t->h = 0.4f + 0.4f * blade * n2;
  t->rough = 0.92f;
  t->ao = 0.75f + 0.25f * blade;
}

/* chain link: a diamond mesh of wire (alpha cut out), galvanised */
static void g_chain(float u, float v, texel *t) {
  float a = fractf((u + v) * 8), b = fractf((u - v) * 8);
  float wa = 1 - clamp01((fabsf(a - 0.5f) - 0.42f) * 40), wb = 1 - clamp01((fabsf(b - 0.5f) - 0.42f) * 40);
  float wire = clamp01(fmaxf(1 - wa, 1 - wb));
  float n = fbm(u, v, 8, 2, 111);
  float c = 0.55f + 0.15f * n;
  rgb(t, c, c, c * 1.02f);
  t->a = wire;
  t->h = wire;
  t->rough = 0.5f;
  t->ao = 1;
}

/* window glass: grime and smears (the material is mostly see-through) */
static void g_glass(float u, float v, texel *t) {
  float smear = clamp01((fbm(u, v, 4, 4, 121) - 0.4f) * 2), dirt = clamp01((fbm(u, v, 2, 3, 122) - 0.5f) * 3) * (1 - v);
  float c = 0.85f - 0.4f * dirt;
  rgb(t, c, c * 0.98f, c * 0.95f);
  t->h = 0.5f;
  t->rough = 0.05f + 0.35f * smear + 0.4f * dirt;
  t->ao = 1;
}

/* ── arena sets: each carries its own colour (the level tint stays near white) and real contrast ── */

/* poured floor slabs, 2 m (two a tile), saw-cut joints, per-slab tone, aggregate speckle, oil and wet stains, scuffs */
static void g_slab(float u, float v, texel *t) {
  int sx = (int)floorf(u * 2), sy = (int)floorf(v * 2);
  float ju = fminf(fractf(u * 2), 1 - fractf(u * 2)), jv = fminf(fractf(v * 2), 1 - fractf(v * 2));
  float joint = 1 - clamp01((fminf(ju, jv) - 0.004f) * 300); /* 1 in the cut */
  float tone = (h01(sx, sy, 201) - 0.5f) * 0.12f;
  float n = fbm(u, v, 6, 5, 202), fine = fbm(u, v, 48, 2, 203), speck = h01((int)(u * N), (int)(v * N), 204);
  float oil = clamp01((fbm(u, v, 3, 4, 205) - 0.64f) * 5) * 0.75f;
  float scuff = clamp01((fbm(u, v * 8, 4, 3, 206) - 0.6f) * 4) * 0.6f;
  float patch = clamp01((fbm(u, v, 2, 3, 207) - 0.6f) * 6); /* a patched repair, lighter and smoother */
  float c = 0.5f + tone + 0.14f * (n - 0.5f) + 0.05f * (fine - 0.5f) + (speck > 0.93f ? 0.07f : speck < 0.05f ? -0.08f : 0) - 0.08f * scuff + 0.07f * patch;
  rgb(t, c * 1.02f, c, c * 0.95f);
  t->r = mixf(t->r, 0.12f, oil * 0.8f), t->g = mixf(t->g, 0.11f, oil * 0.8f), t->b = mixf(t->b, 0.1f, oil * 0.8f);
  t->r = mixf(t->r, 0.1f, joint), t->g = mixf(t->g, 0.1f, joint), t->b = mixf(t->b, 0.1f, joint);
  t->h = 0.6f + 0.12f * fine + 0.05f * n - 0.5f * joint + 0.04f * (speck > 0.93f);
  t->rough = mixf(0.88f - 0.1f * patch, 0.3f, oil) + 0.06f * (fine - 0.5f);
  t->ao = 1 - 0.6f * joint - 0.1f * oil;
}

/* painted steel cladding in `col`: 1 m panels, 2 m high, seams, rivet rows, pillowed faces, chipped edges showing rust,
 * streaks running down from the rivets, grime at the foot of each panel */
static void panel(float u, float v, texel *t, float cr, float cg, float cb, uint32_t seed) {
  float pu = fractf(u * 4), pv = fractf(v * 2);
  int px = (int)floorf(u * 4), py = (int)floorf(v * 2);
  float eu = fminf(pu, 1 - pu), ev = fminf(pv, 1 - pv);
  float seam = 1 - clamp01((fminf(eu, ev) - 0.006f) * 250);
  /* rivets: rows 4 cm in from the horizontal seams, every 12.5 cm */
  float ru = fractf(u * 32) - 0.5f;
  float dm = fminf(fabsf(pv - 0.02f), fabsf(pv - 0.98f)) * 2.0f, um = ru * 0.125f; /* metres from the rivet */
  float rivet = clamp01(1.4f - sqrtf(um * um + dm * dm) / 0.012f);
  float pillow = (1 - cw_pow(fabsf(pu - 0.5f) * 2, 4)) * (1 - cw_pow(fabsf(pv - 0.5f) * 2, 4));
  float n = fbm(u, v, 8, 4, seed), fine = fbm(u, v, 64, 2, seed + 1);
  float edge = clamp01(1 - fminf(eu * 4, ev * 2) * 6); /* near a seam */
  float chip = clamp01((fbm(u, v, 24, 4, seed + 2) + 0.35f * edge - 0.78f) * 8);
  float streak = 0;
  { /* rust runs below each rivet column: vertical stripes in u, fading down from the top seam */
    float sx = fabsf(ru) < 0.12f ? 1 : 0;
    float run = clamp01((fbm(u * 8, v, 8, 2, seed + 3) - 0.45f) * 3);
    streak = sx * run * clamp01(1 - (0.97f - pv) * 2.2f) * 0.7f + clamp01((fbm(u * 6, v, 4, 3, seed + 4) - 0.62f) * 3) * 0.35f;
  }
  float grime = clamp01((0.3f - pv) * 3) * (0.5f + 0.5f * n);
  float tone = (h01(px, py, seed + 5) - 0.5f) * 0.08f;
  float k = 1 + tone + 0.08f * (n - 0.5f) + 0.03f * (fine - 0.5f) - 0.35f * grime;
  float r = cr * k, g = cg * k, b = cb * k;
  r = mixf(r, 0.36f, streak), g = mixf(g, 0.2f, streak), b = mixf(b, 0.1f, streak);
  float rust = clamp01(fbm(u, v, 16, 3, seed + 6) * 1.5f);
  r = mixf(r, mixf(0.18f, 0.42f, rust), chip), g = mixf(g, mixf(0.17f, 0.22f, rust), chip), b = mixf(b, mixf(0.17f, 0.1f, rust), chip);
  r = mixf(r, 0.05f, seam), g = mixf(g, 0.05f, seam), b = mixf(b, 0.05f, seam);
  r = mixf(r, cr * 0.75f, rivet), g = mixf(g, cg * 0.75f, rivet), b = mixf(b, cb * 0.75f, rivet);
  rgb(t, r, g, b);
  t->h = 0.5f + 0.12f * pillow - 0.45f * seam + 0.35f * rivet - 0.06f * chip + 0.02f * fine;
  t->rough = mixf(0.45f + 0.2f * grime + 0.1f * n, 0.85f, fmaxf(chip, streak * 0.8f));
  t->ao = 1 - 0.55f * seam - 0.15f * grime;
}
static void g_panel_steel(float u, float v, texel *t) { panel(u, v, t, 0.24f, 0.34f, 0.42f, 300); } /* blue-grey */
static void g_panel_teal(float u, float v, texel *t) { panel(u, v, t, 0.1f, 0.42f, 0.42f, 310); }
static void g_panel_red(float u, float v, texel *t) { panel(u, v, t, 0.52f, 0.13f, 0.08f, 320); }

/* hazard trim: 45 degree yellow and black stripes (a quarter tile each), worn to steel at the edges and in scuffs */
static void g_hazard(float u, float v, texel *t) {
  float s = fractf((u + v) * 4);
  float stripe = clamp01((fabsf(s - 0.5f) - 0.25f) * 80 + 0.5f); /* 1 = yellow */
  float n = fbm(u, v, 8, 4, 401), fine = fbm(u, v, 64, 2, 402);
  float wear = clamp01((fbm(u, v, 16, 4, 403) - 0.66f) * 6) + clamp01((fbm(u, v * 8, 4, 2, 404) - 0.7f) * 5);
  wear = clamp01(wear);
  float dirt = clamp01((fbm(u, v, 4, 3, 405) - 0.45f) * 2) * 0.35f;
  float yr = 0.86f, yg = 0.62f, yb = 0.04f, kr = 0.05f;
  float r = mixf(kr, yr, stripe), g = mixf(kr, yg, stripe), b = mixf(kr, yb, stripe);
  float k = 0.92f + 0.12f * (n - 0.5f) + 0.05f * (fine - 0.5f) - dirt;
  r *= k, g *= k, b *= k;
  r = mixf(r, 0.4f, wear), g = mixf(g, 0.39f, wear), b = mixf(b, 0.37f, wear);
  rgb(t, r, g, b);
  t->h = 0.55f - 0.08f * wear + 0.03f * fine;
  t->rough = mixf(0.55f, 0.4f, wear) + 0.2f * dirt;
  t->ao = 1 - 0.2f * dirt;
}

/* bar grating: bearing bars and cross rods over a dark void */
static void g_grate(float u, float v, texel *t) {
  float a = fractf(u * 24), b = fractf(v * 6);
  float bar = 1 - clamp01((fabsf(a - 0.5f) - 0.09f) * 60);      /* bearing bars across u */
  float rod = 1 - clamp01((fabsf(b - 0.5f) - 0.04f) * 80);       /* cross rods */
  float m = fmaxf(bar, rod);
  float n = fbm(u, v, 8, 3, 501), rust = clamp01((fbm(u, v, 4, 3, 502) - 0.6f) * 4);
  float c = 0.42f + 0.1f * n;
  float r = mixf(c, 0.4f, rust), g = mixf(c, 0.24f, rust), bl = mixf(c * 1.03f, 0.12f, rust);
  rgb(t, mixf(0.015f, r, m), mixf(0.015f, g, m), mixf(0.018f, bl, m));
  t->h = 0.15f + 0.85f * m + 0.1f * rod;
  t->rough = mixf(0.9f, mixf(0.4f, 0.85f, rust), m);
  t->ao = mixf(0.1f, 1, m);
}

/* square tiles (8 a tile), grout, per-tile colour and wear, chipped corners, a few cracks */
static void tiles(float u, float v, texel *t, float cr, float cg, float cb, float var, uint32_t seed) {
  float tu = fractf(u * 8), tv = fractf(v * 8);
  int ix = (int)floorf(u * 8), iy = (int)floorf(v * 8);
  float e = fminf(fminf(tu, 1 - tu), fminf(tv, 1 - tv));
  float grout = 1 - clamp01((e - 0.025f) * 120);
  float bevel = clamp01(e * 25);
  float id = h01(ix & 7, iy & 7, seed), id2 = h01(ix & 7, iy & 7, seed + 1);
  float n = fbm(u, v, 16, 4, seed + 2), fine = fbm(u, v, 64, 2, seed + 3);
  float d1, d2;
  uint32_t cid;
  cells(u, v, 8, seed + 4, &d1, &d2, &cid);
  float crack = (id2 > 0.8f) ? (1 - clamp01((d2 - d1) * 40)) * 0.8f : 0;
  float dirt = clamp01((fbm(u, v, 3, 3, seed + 5) - 0.5f) * 2.5f);
  float k = 1 + var * (id - 0.5f) + 0.12f * (n - 0.5f) + 0.05f * (fine - 0.5f) - 0.25f * dirt;
  float r = cr * k, g = cg * k * (1 + 0.05f * (id2 - 0.5f)), b = cb * k;
  r = mixf(r, 0.16f, crack), g = mixf(g, 0.15f, crack), b = mixf(b, 0.14f, crack);
  float gr = 0.3f - 0.12f * dirt;
  rgb(t, mixf(r, gr, grout), mixf(g, gr * 0.97f, grout), mixf(b, gr * 0.92f, grout));
  t->h = 0.35f + 0.5f * bevel * (1 - grout) + 0.04f * n - 0.2f * crack;
  t->rough = mixf(0.55f + 0.25f * id2 + 0.15f * dirt, 0.95f, grout);
  t->ao = 1 - 0.5f * grout - 0.2f * crack;
}
static void g_tiles_sand(float u, float v, texel *t) { tiles(u, v, t, 0.74f, 0.58f, 0.42f, 0.22f, 600); }
static void g_tiles_slate(float u, float v, texel *t) { tiles(u, v, t, 0.34f, 0.38f, 0.4f, 0.3f, 610); }

/* ashlar stone: courses a quarter tile... an eighth high (0.5 m), random lengths, recessed mortar, weathering */
static void g_blocks(float u, float v, texel *t) {
  float rowf = v * 8;
  int row = (int)floorf(rowf);
  float rv = fractf(rowf);
  float off = h01(row & 7, 0, 701);
  float x = u * 4 + off * 4; /* a block is about a metre: 4 a tile */
  float bx = floorf(x), fu = fractf(x);
  int bid = ((int)bx & 3) + (row & 7) * 4;
  float e = fminf(fminf(fu * 2, (1 - fu) * 2), fminf(rv, 1 - rv)); /* in block units (half for u) */
  float mortar = 1 - clamp01((e - 0.03f) * 60);
  float tone = h01(bid, row & 7, 702), n = fbm(u, v, 8, 5, 703), fine = fbm(u, v, 64, 2, 704);
  float weather = clamp01((fbm(u, v, 3, 4, 705) - 0.45f) * 2);
  float streak = clamp01((fbm(u * 4, v, 4, 3, 706) - 0.55f) * 3);
  float k = 0.9f + 0.22f * (tone - 0.5f) + 0.12f * (n - 0.5f) + 0.05f * (fine - 0.5f) - 0.18f * streak - 0.1f * weather;
  float r = 0.72f * k, g = 0.62f * k, b = 0.5f * k;
  r = mixf(r, 0.42f, weather * 0.3f), g = mixf(g, 0.44f, weather * 0.3f), b = mixf(b, 0.36f, weather * 0.3f); /* lichen-grey */
  rgb(t, mixf(r, 0.3f, mortar), mixf(g, 0.28f, mortar), mixf(b, 0.25f, mortar));
  t->h = 0.5f + 0.3f * clamp01(e * 10) * (1 - mortar) + 0.1f * n + 0.03f * fine - 0.3f * mortar;
  t->rough = mixf(0.8f + 0.1f * n, 0.95f, mortar);
  t->ao = 1 - 0.55f * mortar - 0.1f * weather;
}

/* plaster render: warm, patched, cracked, dirt rising from the ground */
static void g_plaster(float u, float v, texel *t) {
  float n = fbm(u, v, 6, 5, 801), fine = fbm(u, v, 48, 3, 802);
  float patch = clamp01((fbm(u, v, 2, 4, 803) - 0.55f) * 5);
  float d1, d2;
  uint32_t id;
  cells(u, v, 5, 804, &d1, &d2, &id);
  float crack = ((id & 3) == 0) ? (1 - clamp01((d2 - d1) * 70)) : 0;
  float rise = clamp01((0.35f - v) * 2.5f) * (0.6f + 0.4f * n);
  float k = 0.95f + 0.12f * (n - 0.5f) + 0.06f * (fine - 0.5f) - 0.3f * rise - 0.1f * patch;
  rgb(t, 0.86f * k, 0.72f * k, 0.55f * k);
  t->r = mixf(t->r, 0.2f, crack), t->g = mixf(t->g, 0.17f, crack), t->b = mixf(t->b, 0.14f, crack);
  t->h = 0.5f + 0.15f * fine + 0.1f * n + 0.06f * patch - 0.4f * crack;
  t->rough = 0.88f + 0.08f * fine;
  t->ao = 1 - 0.4f * crack - 0.1f * rise;
}

/* steel tread plate: raised diagonal lozenges, worn bright on the tops, rust in the low ground */
static void g_tread(float u, float v, texel *t) {
  float a = u * 32, b = v * 32;
  int cx = (int)floorf(a), cy = (int)floorf(b);
  float fu = fractf(a) - 0.5f, fv = fractf(b) - 0.5f;
  float dir = ((cx + cy) & 1) ? 1.f : -1.f; /* alternate the lozenge direction */
  float p = (fu + dir * fv) * 0.7071f, q = (fu - dir * fv) * 0.7071f;
  float lz = clamp01((1 - (p * p / 0.09f + q * q / 0.0049f)) * 2.5f);
  float n = fbm(u, v, 8, 4, 901), rust = clamp01((fbm(u, v, 4, 4, 902) - 0.66f) * 3) * (1 - lz);
  float dirt = clamp01((fbm(u, v, 3, 3, 903) - 0.4f) * 2) * (1 - lz) * 0.6f;
  float c = 0.36f + 0.1f * (n - 0.5f) + 0.16f * lz - 0.18f * dirt;
  rgb(t, mixf(c, 0.38f, rust), mixf(c, 0.2f, rust), mixf(c * 1.04f, 0.09f, rust));
  t->h = 0.3f + 0.6f * lz + 0.05f * n;
  t->rough = mixf(mixf(0.55f, 0.28f, lz), 0.9f, fmaxf(rust, dirt));
  t->ao = 1 - 0.3f * (1 - lz) * dirt - 0.15f * (1 - lz);
}

/* board-formed concrete: plank marks, tie holes on a grid, streaks, real contrast */
static void g_formed(float u, float v, texel *t) {
  float n = fbm(u, v, 8, 5, 1001), fine = fbm(u, v, 64, 2, 1002);
  float plank = fractf(v * 26); /* boards about 15 cm */
  int pid = (int)floorf(v * 26);
  float board = (h01(pid & 63, 0, 1003) - 0.5f) * 0.1f;
  float grain = fbm(u, v * 8, 8, 3, 1004 + (uint32_t)(pid & 7));
  float line = 1 - clamp01(fminf(plank, 1 - plank) * 40);
  float hu = fractf(u * 6) - 0.5f, hv = fractf(v * 4) - 0.5f;
  float hole = clamp01(1 - sqrtf(hu * hu + hv * hv * 2.25f) * 22);
  float stain = clamp01((fbm(u, v, 3, 3, 1005) - 0.5f) * 3), streak = clamp01((fbm(u * 4, v, 4, 3, 1006) - 0.55f) * 4);
  float c = 0.56f + board + 0.12f * (n - 0.5f) + 0.06f * (grain - 0.5f) + 0.04f * (fine - 0.5f) - 0.14f * stain - 0.12f * streak - 0.12f * line;
  rgb(t, c, c * 0.99f, c * 0.96f);
  t->r = mixf(t->r, 0.08f, hole), t->g = mixf(t->g, 0.08f, hole), t->b = mixf(t->b, 0.08f, hole);
  t->h = 0.5f + 0.15f * grain + 0.1f * n - 0.2f * line - 0.5f * hole;
  t->rough = 0.86f + 0.08f * (n - 0.5f);
  t->ao = 1 - 0.6f * hole - 0.2f * line - 0.1f * stain;
}

/* factory glazing: a steel grid of small panes, the light behind them uneven; the map is the albedo AND the emissive mask
 * (the level gives it a dark tint and an emissive colour) */
static void g_windows(float u, float v, texel *t) {
  float a = fractf(u * 12), b = fractf(v * 8);
  int ix = (int)floorf(u * 12), iy = (int)floorf(v * 8);
  float frame = 1 - clamp01((fminf(fminf(a, 1 - a), fminf(b, 1 - b)) - 0.045f) * 80);
  float lit = h01(ix, iy, 1101), grime = fbm(u, v, 8, 3, 1102);
  float k = lit < 0.05f ? 0.15f : 0.6f + 0.4f * h01(ix, iy, 1103); /* a rare dark pane */
  k *= 0.75f + 0.35f * grime;
  float glow = 0.3f + 0.7f * (1 - fabsf(b - 0.5f) * 1.2f); /* brighter in the middle of a pane */
  rgb(t, mixf(0.95f * k * glow, 0.02f, frame), mixf(0.72f * k * glow, 0.02f, frame), mixf(0.45f * k * glow, 0.022f, frame));
  t->h = 0.3f + 0.6f * frame;
  t->rough = mixf(0.15f + 0.3f * grime, 0.6f, frame);
  t->ao = 1 - 0.2f * frame;
}

static const gen_fn GEN[TEX_KINDS] = { g_dirt, g_concrete, g_rubber, g_paint, g_rust, g_steel, g_wood, g_gravel, g_grass, g_chain, g_glass, g_primer,
                                       g_slab, g_panel_steel, g_panel_teal, g_panel_red, g_hazard, g_grate, g_tiles_sand, g_tiles_slate, g_blocks, g_plaster, g_tread, g_formed, g_windows };
static const float NORMAL_STRENGTH[TEX_KINDS] = { 3, 2.5f, 4, 1, 5, 1.5f, 3, 3, 2, 2, 0.5f, 1.5f,
                                                  6, 8, 8, 8, 3, 10, 8, 8, 8, 5, 10, 6, 3 };

static tex_set SETS[TEX_KINDS];
uint32_t tex_hash = 2166136261u;
uint32_t tex_each[TEX_KINDS + 2]; /* per texture (the last two: numbers, crack), to find a host that differs */

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
static uint8_t b8(float x) { return (uint8_t)(clamp01(x) * 255.0f + 0.5f); }
static uint32_t fnv(const uint8_t *p, size_t n, uint32_t h) {
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

/* a texture set in the making: its texels, filled a band of rows per call */
static texel *WARM_TX;
static int warm_kind = -1, warm_row;
#define WARM_ROWS 48

bool tex_warm(int kind) {
  tex_set *s = &SETS[kind];
  if (s->map) return true;
  if (warm_kind != kind) {
    if (!WARM_TX) WARM_TX = malloc(sizeof *WARM_TX * N * N);
    warm_kind = kind, warm_row = 0;
  }
  texel *tx = WARM_TX;
  int y1 = warm_row + WARM_ROWS < N ? warm_row + WARM_ROWS : N;
  for (int y = warm_row; y < y1; y++)
    for (int x = 0; x < N; x++) {
      texel *t = &tx[y * N + x];
      memset(t, 0, sizeof *t);
      t->a = 1;
      GEN[kind]((x + 0.5f) / N, (y + 0.5f) / N, t);
    }
  warm_row = y1;
  if (warm_row < N) return false;
  warm_kind = -1;
  uint8_t *a = malloc(N * N * 4), *nm = malloc(N * N * 4), *ro = malloc(N * N * 4), *ao = malloc(N * N * 4);
  float st = NORMAL_STRENGTH[kind] * N / 256.0f;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      const texel *t = &tx[y * N + x];
      int i = (y * N + x) * 4;
      a[i] = b8(t->r), a[i + 1] = b8(t->g), a[i + 2] = b8(t->b), a[i + 3] = b8(t->a);
      float hx = tx[y * N + (x + 1) % N].h - tx[y * N + (x + N - 1) % N].h;
      float hy = tx[((y + 1) % N) * N + x].h - tx[((y + N - 1) % N) * N + x].h;
      float nx = -hx * st, ny = -hy * st, l = sqrtf(nx * nx + ny * ny + 1);
      nm[i] = b8(nx / l * 0.5f + 0.5f), nm[i + 1] = b8(ny / l * 0.5f + 0.5f), nm[i + 2] = b8(1 / l * 0.5f + 0.5f), nm[i + 3] = 255;
      ro[i] = ro[i + 1] = ro[i + 2] = b8(t->rough), ro[i + 3] = 255;
      ao[i] = ao[i + 1] = ao[i + 2] = b8(t->ao), ao[i + 3] = 255;
    }
  tex_hash = fnv(a, N * N * 4, tex_hash);
  tex_hash = fnv(nm, N * N * 4, tex_hash);
  tex_hash = fnv(ro, N * N * 4, tex_hash);
  tex_each[kind] = fnv(ro, N * N * 4, fnv(nm, N * N * 4, fnv(a, N * N * 4, 2166136261u)));
  s->map = finish(a, true);
  s->normal = finish(nm, false);
  s->rough = finish(ro, false);
  s->ao = finish(ao, false);
  free(a);
  free(nm);
  free(ro);
  free(ao);
  return true;
}

const tex_set *tex_get(int kind) {
  while (!tex_warm(kind)) {}
  return &SETS[kind];
}

void tex_apply_uv(t3_material *m, int kind, float ru, float rv, float normal_scale, float ao_intensity);
void tex_apply(t3_material *m, int kind, float repeat, float normal_scale, float ao_intensity) {
  tex_apply_uv(m, kind, repeat, repeat, normal_scale, ao_intensity);
}

void tex_apply_uv(t3_material *m, int kind, float ru, float rv, float normal_scale, float ao_intensity) {
  const tex_set *s = tex_get(kind);
  if (ru != 1 || rv != 1) { /* three.js keeps the repeat on the texture: a repeated copy */
    t3_texture *ts[4] = { s->map, s->normal, s->rough, s->ao };
    t3_map_slot slots[4] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP, T3_AO_MAP };
    for (int i = 0; i < 4; i++) {
      t3_texture *c = finish(ts[i]->pixels, i == 0);
      c->repeat = (t3_vec2){ ru, rv };
      t3_material_set_texture(m, slots[i], c);
      t3_release(c);
    }
  } else {
    t3_material_set_texture(m, T3_MAP, s->map);
    t3_material_set_texture(m, T3_NORMAL_MAP, s->normal);
    t3_material_set_texture(m, T3_ROUGHNESS_MAP, s->rough);
    t3_material_set_texture(m, T3_AO_MAP, s->ao);
  }
  m->normal_scale = (t3_vec2){ normal_scale, normal_scale };
  m->ao_map_intensity = ao_intensity;
}

void tex_free_all(void) {
  for (int k = 0; k < TEX_KINDS; k++) {
    tex_set *s = &SETS[k];
    if (!s->map) continue;
    t3_release(s->map);
    t3_release(s->normal);
    t3_release(s->rough);
    t3_release(s->ao);
    memset(s, 0, sizeof *s);
  }
}

t3_color lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}

#undef N
/* ── the mesh builder ── */
/* ── the mesh builder ───────────────────────────────────────────────── */

int mb_vert(mesh_build *b, t3_vec3 p, t3_vec3 n, float u, float v) {
  if (b->nv == b->cap_v) {
    b->cap_v = b->cap_v ? b->cap_v * 2 : 1024;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)(uint32_t)b->cap_v);
    b->nor = realloc(b->nor, sizeof(float) * 3 * (size_t)(uint32_t)b->cap_v);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)(uint32_t)b->cap_v);
    b->col = realloc(b->col, sizeof(float) * 3 * (size_t)(uint32_t)b->cap_v);
  }
  float *P = b->pos + b->nv * 3, *N = b->nor + b->nv * 3, *U = b->uv + b->nv * 2, *C = b->col + b->nv * 3;
  P[0] = p.x, P[1] = p.y, P[2] = p.z;
  N[0] = n.x, N[1] = n.y, N[2] = n.z;
  U[0] = u, U[1] = v;
  C[0] = C[1] = C[2] = 1;
  return b->nv++;
}
void mb_color(mesh_build *b, int v, float r, float g, float bl) {
  b->colors = true;
  float *C = b->col + v * 3;
  C[0] = r, C[1] = g, C[2] = bl;
}
static void mb_idx(mesh_build *b, int n) {
  if (b->ni + n > b->cap_i) {
    b->cap_i = b->cap_i ? b->cap_i * 2 : 3072;
    if (b->cap_i < b->ni + n) b->cap_i = b->ni + n;
    b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)(uint32_t)b->cap_i);
  }
}
void mb_tri(mesh_build *b, int a, int c, int d) {
  mb_idx(b, 3);
  b->idx[b->ni++] = (uint32_t)a, b->idx[b->ni++] = (uint32_t)c, b->idx[b->ni++] = (uint32_t)d;
}
void mb_quad(mesh_build *b, int a, int c, int d, int e) {
  mb_tri(b, a, c, d);
  mb_tri(b, a, d, e);
}
t3_geometry *mb_finish(mesh_build *b) {
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, b->pos, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->nor, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2);
  t3_geometry_set_attribute(g, T3_ATTR_UV, a);
  t3_release(a);
  if (b->colors) {
    a = t3_attribute_new(T3_FLOAT32, b->col, b->nv, 3);
    t3_geometry_set_attribute(g, T3_ATTR_COLOR, a);
    t3_release(a);
  }
  a = t3_attribute_new(T3_UINT32, b->idx, b->ni, 1);
  t3_geometry_set_index(g, a);
  t3_release(a);
  free(b->pos);
  free(b->nor);
  free(b->uv);
  free(b->col);
  free(b->idx);
  memset(b, 0, sizeof *b);
  return g;
}

/* append b's vertices and triangles to a (same material), freeing b */
void mb_merge(mesh_build *a, mesh_build *b) {
  int base = a->nv;
  for (int i = 0; i < b->nv; i++) {
    const float *p = b->pos + i * 3, *n = b->nor + i * 3, *u = b->uv + i * 2, *c = b->col + i * 3;
    int v = mb_vert(a, (t3_vec3){ p[0], p[1], p[2] }, (t3_vec3){ n[0], n[1], n[2] }, u[0], u[1]);
    if (b->colors) mb_color(a, v, c[0], c[1], c[2]);
  }
  for (int i = 0; i + 2 < b->ni; i += 3) mb_tri(a, base + (int)b->idx[i], base + (int)b->idx[i + 1], base + (int)b->idx[i + 2]);
  free(b->pos), free(b->nor), free(b->uv), free(b->col), free(b->idx);
  memset(b, 0, sizeof *b);
}

/* append a loaded geometry (positions, normals, uvs, optional index) scaled, turned about Y by yaw and moved to pos, tinted;
 * uvs are multiplied by uv_scale (the authoring uvs are in metres) */
void mb_add_geo(mesh_build *b, const t3_geometry *g, t3_vec3 pos, float yaw, t3_vec3 s, const float rgb[3], float uv_scale) {
  const t3_attribute *P = g->attributes[T3_ATTR_POSITION], *N = g->attributes[T3_ATTR_NORMAL], *U = g->attributes[T3_ATTR_UV], *I = g->index;
  if (!P || P->type != T3_FLOAT32) return;
  float sn, cs;
  cw_sincos(yaw, &sn, &cs);
  int base = b->nv;
  const float *pp = P->array, *nn = N ? N->array : NULL, *uu = U ? U->array : NULL;
  for (int i = 0; i < P->count; i++) {
    float x = pp[i * 3] * s.x, y = pp[i * 3 + 1] * s.y, z = pp[i * 3 + 2] * s.z;
    t3_vec3 w = { pos.x + x * cs + z * sn, pos.y + y, pos.z - x * sn + z * cs };
    t3_vec3 n = { 0, 1, 0 };
    if (nn) {
      float nx = nn[i * 3] / s.x, ny = nn[i * 3 + 1] / s.y, nz = nn[i * 3 + 2] / s.z;
      float l = sqrtf(nx * nx + ny * ny + nz * nz);
      if (l > 1e-6f) nx /= l, ny /= l, nz /= l;
      n = (t3_vec3){ nx * cs + nz * sn, ny, -nx * sn + nz * cs };
    }
    int v = mb_vert(b, w, n, uu ? uu[i * 2] * uv_scale : 0, uu ? uu[i * 2 + 1] * uv_scale : 0);
    mb_color(b, v, rgb[0], rgb[1], rgb[2]);
  }
  int flip = (s.x * s.y * s.z) < 0;
  if (!I) return;
  for (int i = 0; i + 2 < I->count; i += 3) {
    uint32_t a, c, d;
    if (I->type == T3_UINT16) { const uint16_t *ix = I->array; a = ix[i], c = ix[i + 1], d = ix[i + 2]; }
    else { const uint32_t *ix = I->array; a = ix[i], c = ix[i + 1], d = ix[i + 2]; }
    if (flip) mb_tri(b, base + (int)a, base + (int)d, base + (int)c);
    else mb_tri(b, base + (int)a, base + (int)c, base + (int)d);
  }
}

float mb_macro = 0;

/* a smooth world-space tint wobble (about 7 m across) */
static float macro_at(t3_vec3 p) {
  if (mb_macro <= 0) return 1;
  float a = vnoise(p.x * 0.14f + p.y * 0.05f + 512, p.z * 0.14f - p.y * 0.09f + 512, 4096, 77);
  float b = vnoise(p.x * 0.5f + 300, p.z * 0.5f + p.y * 0.4f + 300, 4096, 78);
  return 1 + mb_macro * ((a - 0.5f) * 1.6f + (b - 0.5f) * 0.5f);
}

void mb_box(mesh_build *b, t3_vec3 c, t3_quat q, t3_vec3 s, float uv_m, const float rgb[3]) {
  t3_mat4 m;
  t3_mat4_compose(&m, c, q, (t3_vec3){ 1, 1, 1 });
  t3_vec3 h = { s.x / 2, s.y / 2, s.z / 2 };
  bool upright = fabsf(q.x) < 1e-4f && fabsf(q.z) < 1e-4f, axis = upright && fabsf(q.y) < 1e-4f;
  /* per face: normal, u axis, v axis (local), half extents along them */
  static const float F[6][9] = {
    { 1, 0, 0, 0, 0, -1, 0, 1, 0 }, { -1, 0, 0, 0, 0, 1, 0, 1, 0 }, { 0, 1, 0, 1, 0, 0, 0, 0, -1 },
    { 0, -1, 0, 1, 0, 0, 0, 0, 1 }, { 0, 0, 1, 1, 0, 0, 0, 1, 0 },  { 0, 0, -1, -1, 0, 0, 0, 1, 0 },
  };
  for (int f = 0; f < 6; f++) {
    const float *a = F[f];
    t3_vec3 n = { a[0], a[1], a[2] }, u = { a[3], a[4], a[5] }, v = { a[6], a[7], a[8] };
    float hu = fabsf(u.x) * h.x + fabsf(u.y) * h.y + fabsf(u.z) * h.z;
    float hv = fabsf(v.x) * h.x + fabsf(v.y) * h.y + fabsf(v.z) * h.z;
    float hn = fabsf(n.x) * h.x + fabsf(n.y) * h.y + fabsf(n.z) * h.z;
    /* offset per face so neighbouring boxes do not tile in step */
    float ou = (float)(((int)(c.x * 7.3f) + (int)(c.z * 3.1f) + f * 5) & 7) * 0.37f, ov = (float)(((int)(c.y * 5.9f) + (int)(c.x * 1.7f)) & 7) * 0.41f;
    t3_vec3 wn = t3_vec3_transform_direction(n, &m);
    /* uvs anchored to the world where that keeps boxes in step: floors and tops of unrotated boxes by x/z,
     * the sides of upright boxes by height (panel seams and grime line up along a wall) */
    int mode = (axis && f >= 2 && f <= 3) ? 1 : (upright && f != 2 && f != 3) ? 2 : 0;
    float cell = uv_m > 4 ? uv_m : 4;
    int nu = (int)ceilf(2 * hu / cell - 0.01f), nv = (int)ceilf(2 * hv / cell - 0.01f);
    if (nu < 1 || mb_macro <= 0) nu = 1;
    if (nv < 1 || mb_macro <= 0) nv = 1;
    if (nu > 32) nu = 32;
    if (nv > 32) nv = 32;
    int base = b->nv;
    for (int j = 0; j <= nv; j++)
      for (int i = 0; i <= nu; i++) {
        float lu = -hu + 2 * hu * (float)i / (float)nu, lv = -hv + 2 * hv * (float)j / (float)nv;
        t3_vec3 lp = { n.x * hn + u.x * lu + v.x * lv, n.y * hn + u.y * lu + v.y * lv, n.z * hn + u.z * lu + v.z * lv };
        t3_vec3 wp = t3_vec3_apply_mat4(lp, &m);
        float tu, tv;
        if (mode == 1) tu = wp.x / uv_m, tv = (f == 2 ? -wp.z : wp.z) / uv_m;
        else if (mode == 2) tu = (lu + hu) / uv_m + ou, tv = wp.y / uv_m;
        else tu = (lu + hu) / uv_m + ou, tv = (lv + hv) / uv_m + ov;
        int vi = mb_vert(b, wp, wn, tu, tv);
        float k = macro_at(wp);
        mb_color(b, vi, rgb[0] * k, rgb[1] * k, rgb[2] * k);
      }
    for (int j = 0; j < nv; j++)
      for (int i = 0; i < nu; i++) {
        int v0 = base + j * (nu + 1) + i;
        mb_quad(b, v0, v0 + 1, v0 + nu + 2, v0 + nu + 1);
      }
  }
}

/* a tiling cloud layer for the sky plane: brightness premultiplied by cover (drawn additively), coverage 0..1 */
t3_texture *tex_clouds(float coverage, uint32_t seed) {
  const int n = 512;
  uint8_t *px = malloc((size_t)n * n * 4);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) {
      float u = (x + 0.5f) / n, v = (y + 0.5f) / n;
      float base = fbm(u, v, 4, 6, seed), warp = fbm(u, v, 8, 3, seed + 9);
      float d = clamp01((base + 0.25f * (warp - 0.5f) - (1 - coverage)) * 3.2f);
      d = d * d * (3 - 2 * d);
      float lit = 0.7f + 0.3f * clamp01((fbm(u + 0.004f, v + 0.004f, 4, 6, seed) - base) * 20 + 0.5f); /* a sunward rim */
      float c = d * lit;
      uint8_t *p = px + (y * n + x) * 4;
      p[0] = p[1] = p[2] = b8(c), p[3] = 255;
    }
  t3_texture *t = t3_texture_new(n, n, px);
  free(px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 8;
  t->color_space = T3_SRGB_COLOR_SPACE;
  return t;
}
