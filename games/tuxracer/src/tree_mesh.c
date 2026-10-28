/* A conifer built for the racer's eye level: a trunk and T drooping tiers,
 * each a cone from an apex on the trunk out and down to a jagged rim, with
 * an underside ring so a tier has a belly when seen from below (Tux looks
 * up at the trees). Tiled for the snow-dusted needle texture. Two branching
 * variants (the rim noise seed) and two LODs. Unit height, base at y = 0,
 * widest tier radius ~0.3 (CONIFER_WIDTH 0.6). Deterministic: the same
 * geometry on every host. */
#include "game.h"

#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"

typedef struct mesh_build { float *p, *n, *uv; uint32_t *ix; int v, i, vcap, icap; } mesh_build;

static int vert(mesh_build *b, t3_vec3 p, t3_vec3 n, float u, float v) {
  if (b->v == b->vcap) { b->vcap = b->vcap ? b->vcap * 2 : 256; b->p = realloc(b->p, (size_t)b->vcap * 12); b->n = realloc(b->n, (size_t)b->vcap * 12); b->uv = realloc(b->uv, (size_t)b->vcap * 8); }
  b->p[b->v * 3] = p.x, b->p[b->v * 3 + 1] = p.y, b->p[b->v * 3 + 2] = p.z;
  b->n[b->v * 3] = n.x, b->n[b->v * 3 + 1] = n.y, b->n[b->v * 3 + 2] = n.z;
  b->uv[b->v * 2] = u, b->uv[b->v * 2 + 1] = v;
  return b->v++;
}
static void tri(mesh_build *b, int a, int c, int d) {
  if (b->i + 3 > b->icap) { b->icap = b->icap ? b->icap * 2 : 768; b->ix = realloc(b->ix, (size_t)b->icap * 4); }
  b->ix[b->i++] = (uint32_t)a, b->ix[b->i++] = (uint32_t)c, b->ix[b->i++] = (uint32_t)d;
}
static float hash01(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return (float)(x & 0xffffff) / 16777216.0f; }

static t3_geometry *finish(mesh_build *b) {
  t3_geometry *g = t3_geometry_new();
  t3_attribute *ap = t3_attribute_new(T3_FLOAT32, b->p, b->v, 3), *an = t3_attribute_new(T3_FLOAT32, b->n, b->v, 3),
               *au = t3_attribute_new(T3_FLOAT32, b->uv, b->v, 2), *ai = t3_attribute_new(T3_UINT32, b->ix, b->i, 1);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, ap);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, an);
  t3_geometry_set_attribute(g, T3_ATTR_UV, au);
  t3_geometry_set_index(g, ai);
  t3_release(ap), t3_release(an), t3_release(au), t3_release(ai);
  /* (t3_attribute_new copies) */
  free(b->p), free(b->n), free(b->uv), free(b->ix);
  memset(b, 0, sizeof *b);
  return g;
}

/* the foliage: T tiers of S segments */
t3_geometry *tree_mesh_spruce(int variant, int lod) {
  mesh_build b = { 0 };
  int T = lod ? 6 : 14, S = lod ? 7 : 22; /* near: 14 tiers of 22 segments, the rim no longer a polygon */
  /* needles mapped from above (x, z) plus a share of the height: no stretch into the apex,
   * about one texture repeat per 0.33 of the tree's height */
  #define TUV(q) (q).x * 3.0f + 0.5f, ((q).z + (q).y * 0.5f) * 3.0f + 0.5f
  uint32_t seed = 0x9e3779b9U * (uint32_t)(variant + 1);
  float lean = (hash01(seed + 77) - 0.5f) * 0.06f;
  for (int k = 0; k < T; k++) {
    float t = (float)k / (float)(T - 1);              /* 0 top .. 1 bottom */
    float y_apex = 0.98f - t * 0.74f;                  /* apexes from the tip down to y 0.24 */
    float r = (0.05f + t * 0.27f) * 0.78f;             /* rim radius grows down the tree (the cards reach past it) */
    r *= 0.9f + 0.2f * hash01(seed + 1000 + (uint32_t)k);
    float droop = 0.15f + 0.1f * t;                    /* the rim hangs below the next tier's apex: no bare trunk between tiers */
    t3_vec3 apex = { lean * y_apex, y_apex, 0 };
    int ia = vert(&b, apex, (t3_vec3){ 0, 1, 0 }, TUV(apex));
    int rim[32], mid[32], under[32];
    for (int s = 0; s < S; s++) {
      float a = (float)s / (float)S * 6.2831853f + (float)k * 0.45f;
      uint32_t h = seed + (uint32_t)(k * 64 + s);
      /* a jagged rim: a fine per-segment jitter over a slow lobe around the tier (branch clumps) */
      float lobe = 0.5f + 0.5f * cw_sin(a * 3.0f + hash01(seed + (uint32_t)k * 7) * 6.2831853f);
      float rr = r * (0.8f + 0.18f * lobe + 0.22f * hash01(h));
      float dy = droop * (0.8f + 0.25f * hash01(h + 9) + 0.15f * lobe);
      t3_vec3 p = { apex.x + cw_cos(a) * rr, y_apex - dy, cw_sin(a) * rr };
      /* the tier's slope: out and down, so the normal leans out and up */
      t3_vec3 n = t3_vec3_normalize((t3_vec3){ cw_cos(a) * dy, rr, cw_sin(a) * dy });
      rim[s] = vert(&b, p, n, TUV(p));
      if (!lod) {
        /* a ring halfway out, sagging a little and jittered: the branches droop, the sheet buckles */
        float mr = rr * (0.48f + 0.12f * hash01(h + 21)), sag = 0.02f + 0.03f * hash01(h + 33);
        t3_vec3 pm = { apex.x + cw_cos(a) * mr, y_apex - dy * 0.5f - sag, cw_sin(a) * mr };
        t3_vec3 nm = t3_vec3_normalize((t3_vec3){ cw_cos(a) * dy * 0.6f, rr, cw_sin(a) * dy * 0.6f });
        mid[s] = vert(&b, pm, nm, TUV(pm));
        t3_vec3 q = { apex.x + cw_cos(a) * rr * 0.5f, y_apex - dy - 0.03f, cw_sin(a) * rr * 0.5f };
        under[s] = vert(&b, q, (t3_vec3){ 0, -1, 0 }, TUV(q));
      }
    }
    for (int s = 0; s < S; s++) {
      int s1 = (s + 1) % S;
      if (lod) tri(&b, ia, rim[s1], rim[s]); /* counter-clockwise seen from above: the top faces up */
      else {
        tri(&b, ia, mid[s1], mid[s]);
        tri(&b, mid[s], mid[s1], rim[s1]);
        tri(&b, mid[s], rim[s1], rim[s]);
        /* the belly: rim to the underside ring, facing down */
        tri(&b, rim[s], under[s1], under[s]);
        tri(&b, rim[s], rim[s1], under[s1]);
      }
    }
    /* the rim vertices are shared with the next segment through the fan: a flat-ish shading
     * across each tier, which reads as foliage rather than as a faceted cone */
  }
  return finish(&b);
}

/* the trunk: a tapered tube, bark */
t3_geometry *tree_mesh_trunk(int lod) {
  mesh_build b = { 0 };
  int S = lod ? 5 : 8;
  int ring0[16], ring1[16];
  for (int s = 0; s < S; s++) {
    float a = (float)s / (float)S * 6.2831853f;
    t3_vec3 n = { cw_cos(a), 0, cw_sin(a) };
    ring0[s] = vert(&b, (t3_vec3){ n.x * 0.032f, -0.02f, n.z * 0.032f }, n, (float)s / (float)S * 2.0f, 0);
    ring1[s] = vert(&b, (t3_vec3){ n.x * 0.008f, 0.96f, n.z * 0.008f }, n, (float)s / (float)S * 2.0f, 4.0f);
  }
  for (int s = 0; s < S; s++) {
    int s1 = (s + 1) % S;
    tri(&b, ring0[s], ring1[s], ring1[s1]);
    tri(&b, ring0[s], ring1[s1], ring0[s1]);
  }
  return finish(&b);
}

/* The branch cards around the core: per tier, B branches, each a flat card (the sprig seen from above)
 * and a vertical card crossing it, tilted down from the trunk, reaching past the solid tiers. The
 * sprig texture runs from its base at u 0 (the trunk) to the tip at u 1. Alpha-tested, double-sided. */
t3_geometry *tree_mesh_cards(int variant, int lod) {
  mesh_build b = { 0 };
  int T = lod ? 7 : 12, B = lod ? 4 : 7;
  uint32_t seed = 0x51ed270bU * (uint32_t)(variant + 3);
  for (int k = 0; k < T; k++) {
    float t = (float)k / (float)(T - 1);
    float y0 = 0.95f - t * 0.72f;
    float reach = (0.09f + t * 0.3f) * 1.12f;
    for (int i = 0; i < B; i++) {
      uint32_t h = seed + (uint32_t)(k * 37 + i * 11);
      float a = ((float)i + 0.5f * (float)(k & 1)) / (float)B * 6.2831853f + (hash01(h) - 0.5f) * 0.5f;
      float L = reach * (0.8f + 0.4f * hash01(h + 1)), W = L * 0.42f;
      float tilt = 0.28f + 0.3f * t + 0.15f * hash01(h + 2); /* radians down from level */
      t3_vec3 dir = { cw_cos(a) * cw_cos(tilt), -cw_sin(tilt), cw_sin(a) * cw_cos(tilt) };
      t3_vec3 side = { -cw_sin(a), 0, cw_cos(a) };
      t3_vec3 up = t3_vec3_normalize(t3_vec3_cross(side, dir));
      if (up.y < 0) up = t3_vec3_scale(up, -1);
      t3_vec3 base = { 0, y0, 0 };
      t3_vec3 n = { 0, 1, 0 };
      for (int c = 0; c < 2; c++) { /* 0: flat, 1: vertical */
        t3_vec3 across = c == 0 ? side : up;
        int v0 = vert(&b, t3_vec3_sub(base, t3_vec3_scale(across, W * 0.5f)), n, 0, 0);
        int v1 = vert(&b, t3_vec3_add(base, t3_vec3_scale(across, W * 0.5f)), n, 0, 1);
        t3_vec3 tip = t3_vec3_add(base, t3_vec3_scale(dir, L));
        int v2 = vert(&b, t3_vec3_add(tip, t3_vec3_scale(across, W * 0.5f)), n, 1, 1);
        int v3 = vert(&b, t3_vec3_sub(tip, t3_vec3_scale(across, W * 0.5f)), n, 1, 0);
        tri(&b, v0, v1, v2);
        tri(&b, v0, v2, v3);
      }
    }
  }
  return finish(&b);
}
