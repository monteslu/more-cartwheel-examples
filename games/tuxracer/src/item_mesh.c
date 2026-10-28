/* Item meshes for the enhancement phase: the herring as a lens-shaped body
 * wearing the original sprite (volume, sun lighting and a shadow, while the
 * fish still looks like Tux Racer's), and a subdivided card for the flags so
 * the "flag" shading part can ripple the pennant. Both 1 x 1 in XY with the
 * base at y = 0, as the plain item card (course_objects.c), so the same
 * instance matrices place them. The engine's textures keep their rows as
 * loaded (no flipY): v = 1 - y. */
#include "game.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

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
static t3_geometry *finish(mesh_build *b) {
  t3_geometry *g = t3_geometry_new();
  t3_attribute *ap = t3_attribute_new(T3_FLOAT32, b->p, b->v, 3), *an = t3_attribute_new(T3_FLOAT32, b->n, b->v, 3),
               *au = t3_attribute_new(T3_FLOAT32, b->uv, b->v, 2), *ai = t3_attribute_new(T3_UINT32, b->ix, b->i, 1);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, ap);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, an);
  t3_geometry_set_attribute(g, T3_ATTR_UV, au);
  t3_geometry_set_index(g, ai);
  t3_release(ap), t3_release(an), t3_release(au), t3_release(ai);
  free(b->p), free(b->n), free(b->uv), free(b->ix);
  memset(b, 0, sizeof *b);
  return g;
}

static int vertc(mesh_build *b, t3_vec3 p, t3_vec3 n, float u, float v, float *col, float r, float g, float bl) {
  int i = vert(b, p, n, u, v);
  col[i * 3] = r, col[i * 3 + 1] = g, col[i * 3 + 2] = bl;
  return i;
}
static t3_geometry *finish_colored(mesh_build *b, float *col) {
  int nv = b->v;
  t3_geometry *g = finish(b);
  t3_attribute *ac = t3_attribute_new(T3_FLOAT32, col, nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, ac);
  t3_release(ac);
  free(col);
  return g;
}

/* the herring: a swept body (fat in the middle, pointed at the snout, a stalk into a forked tail), a
 * dorsal fin and the tail fin, coloured per vertex: a deep blue back, silver sides and belly, the eye a
 * dark spot. Nose at +x, centred on y 0.5 of the unit card it replaces; about 0.9 long. */
t3_geometry *item_mesh_fish(void) {
  enum { NT = 26, NA = 14 };
  mesh_build b = { 0 };
  float *col = malloc(sizeof(float) * 3 * 4096);
  int id[NT + 1][NA];
  for (int i = 0; i <= NT; i++) {
    float t = (float)i / NT;                      /* 0 tail stalk .. 1 snout */
    float x = -0.38f + t * 0.84f;
    float prof = sinf(t * 3.14159265f);
    float r = 0.012f + 0.16f * powf(prof, 0.75f) * (t < 0.2f ? 0.55f + 2.25f * t : 1); /* the stalk stays thin */
    for (int a = 0; a < NA; a++) {
      float ang = (float)a / NA * 6.2831853f;
      float cy = cosf(ang), sz = sinf(ang);
      float ry = r * 1.35f * (cy > 0 ? 1.0f : 1.15f), rz = r * 0.62f; /* taller than wide, a fuller belly */
      t3_vec3 p = { x, 0.5f + cy * ry, sz * rz };
      t3_vec3 n = t3_vec3_normalize((t3_vec3){ 0, cy / ry, sz / rz });
      float up = cy * 0.5f + 0.5f;                  /* 1 back, 0 belly */
      float R = 0.06f + 0.6f * (1 - up) * (1 - up), G = 0.18f + 0.6f * (1 - up), B = 0.55f + 0.4f * (1 - up);
      if (up < 0.25f) R = 0.8f, G = 0.86f, B = 0.92f; /* silver belly */
      float ex = x - 0.3f, ey = (cy * ry) - 0.06f, eye = ex * ex + ey * ey * 1.4f;
      if (eye < 0.0016f && fabsf(sz) > 0.5f) R = 0.02f, G = 0.02f, B = 0.03f;
      id[i][a] = vertc(&b, p, n, t, (float)a / NA, col, R, G, B);
    }
  }
  for (int i = 0; i < NT; i++)
    for (int a = 0; a < NA; a++) {
      int a1 = (a + 1) % NA;
      tri(&b, id[i][a], id[i + 1][a], id[i + 1][a1]);
      tri(&b, id[i][a], id[i + 1][a1], id[i][a1]);
    }
  /* fins: thin double-sided sheets, a translucent blue-grey */
  const float FR = 0.3f, FG = 0.45f, FB = 0.75f;
  t3_vec3 zn = { 0, 0, 1 };
  int t0 = vertc(&b, (t3_vec3){ -0.38f, 0.5f, 0 }, zn, 0, 0, col, FR, FG, FB);
  int t1 = vertc(&b, (t3_vec3){ -0.5f, 0.5f + 0.2f, 0 }, zn, 0, 0, col, FR, FG, FB);
  int t2 = vertc(&b, (t3_vec3){ -0.44f, 0.5f, 0 }, zn, 0, 0, col, FR, FG, FB);
  int t3 = vertc(&b, (t3_vec3){ -0.5f, 0.5f - 0.19f, 0 }, zn, 0, 0, col, FR, FG, FB);
  tri(&b, t0, t1, t2); tri(&b, t0, t2, t1); tri(&b, t0, t2, t3); tri(&b, t0, t3, t2);
  int d0 = vertc(&b, (t3_vec3){ -0.12f, 0.5f + 0.2f, 0 }, zn, 0, 0, col, FR, FG, FB);
  int d1 = vertc(&b, (t3_vec3){ 0.0f, 0.5f + 0.33f, 0 }, zn, 0, 0, col, FR, FG, FB);
  int d2 = vertc(&b, (t3_vec3){ 0.14f, 0.5f + 0.21f, 0 }, zn, 0, 0, col, FR, FG, FB);
  tri(&b, d0, d1, d2); tri(&b, d0, d2, d1);
  return finish_colored(&b, col);
}

/* the sprite-on-a-lens fish, kept for reference (TUXRACER_FISH_CARD=1) */
t3_geometry *item_mesh_fish_card(void) {
  enum { NX = 24, NY = 14 };
  const float RX = 0.43f, RY = 0.21f, H = 0.085f;
  mesh_build b = { 0 };
  int id[2][NY + 1][NX + 1];
  for (int side = 0; side < 2; side++)
    for (int j = 0; j <= NY; j++)
      for (int i = 0; i <= NX; i++) {
        float u = (float)i / NX, v = (float)j / NY;
        float ex = (u - 0.5f) / RX, ey = (v - 0.5f) / RY, e = ex * ex + ey * ey;
        float z = 0, fu = 0, fv = 0;
        if (e < 0.999f) {
          float s = sqrtf(1 - e);
          z = H * s;
          fu = -H * (u - 0.5f) / (RX * RX) / s; /* dz/du */
          fv = -H * (v - 0.5f) / (RY * RY) / s;
        }
        float sg = side ? -1.0f : 1.0f;
        t3_vec3 n = t3_vec3_normalize((t3_vec3){ -fu * sg, -fv * sg, sg });
        id[side][j][i] = vert(&b, (t3_vec3){ u - 0.5f, v, z * sg }, n, u, 1 - v);
      }
  for (int side = 0; side < 2; side++)
    for (int j = 0; j < NY; j++)
      for (int i = 0; i < NX; i++) {
        int a = id[side][j][i], c = id[side][j][i + 1], d = id[side][j + 1][i + 1], e = id[side][j + 1][i];
        if (!side) { tri(&b, a, c, d); tri(&b, a, d, e); }
        else { tri(&b, a, d, c); tri(&b, a, e, d); }
      }
  return finish(&b);
}

/* the flag card: N x N quads, normals up (lit like the ground, as the item cards are) */
t3_geometry *item_mesh_flag(void) {
  enum { N = 10 };
  mesh_build b = { 0 };
  int id[N + 1][N + 1];
  for (int j = 0; j <= N; j++)
    for (int i = 0; i <= N; i++) {
      float u = (float)i / N, v = (float)j / N;
      id[j][i] = vert(&b, (t3_vec3){ u - 0.5f, v, 0 }, (t3_vec3){ 0, 1, 0 }, u, 1 - v);
    }
  for (int j = 0; j < N; j++)
    for (int i = 0; i < N; i++) {
      tri(&b, id[j][i], id[j][i + 1], id[j + 1][i + 1]);
      tri(&b, id[j][i], id[j + 1][i + 1], id[j + 1][i]);
    }
  return finish(&b);
}

/* a race gate: two poles and the banner between them, the original start / finish art cropped to its
 * banner (the top quarter of the image, between the painted poles). Unit: the gate's 9 x 6 m. */
t3_geometry *item_mesh_gate_poles(void) {
  enum { S = 10 };
  mesh_build b = { 0 };
  for (int side = 0; side < 2; side++) {
    float px = side ? 4.25f : -4.25f;
    int r0[S], r1[S];
    for (int k = 0; k < S; k++) {
      float a = (float)k / S * 6.2831853f;
      t3_vec3 n = { cosf(a), 0, sinf(a) };
      r0[k] = vert(&b, (t3_vec3){ px + n.x * 0.11f, -0.3f, n.z * 0.11f }, n, (float)k / S, 0);
      r1[k] = vert(&b, (t3_vec3){ px + n.x * 0.08f, 6.1f, n.z * 0.08f }, n, (float)k / S, 1);
    }
    for (int k = 0; k < S; k++) {
      int k1 = (k + 1) % S;
      tri(&b, r0[k], r1[k], r1[k1]);
      tri(&b, r0[k], r1[k1], r0[k1]);
    }
    /* a cap */
    int top = vert(&b, (t3_vec3){ px, 6.2f, 0 }, (t3_vec3){ 0, 1, 0 }, 0.5f, 1);
    for (int k = 0; k < S; k++) tri(&b, top, r1[(k + 1) % S], r1[k]);
  }
  return finish(&b);
}
t3_geometry *item_mesh_gate_banner(void) {
  enum { N = 12 };
  mesh_build b = { 0 };
  int id[3][N + 1];
  const float U0 = 0.07f, U1 = 0.93f, V0 = 0.0f, V1 = 0.26f; /* the banner in start.png / finish.png */
  for (int j = 0; j <= 2; j++)
    for (int i = 0; i <= N; i++) {
      float u = (float)i / N, v = (float)j / 2;
      float x = -4.1f + u * 8.2f, y = 6.0f - v * 1.75f;
      /* a little sag and a slow billow in z, baked: the banner hangs, not a flat board */
      float z = 0.06f * sinf(u * 3.14159265f) * (1 - v) + 0.05f * sinf(u * 9.4f) * v;
      id[j][i] = vert(&b, (t3_vec3){ x, y, z }, (t3_vec3){ 0, 0, 1 }, U0 + u * (U1 - U0), V0 + v * (V1 - V0));
    }
  for (int j = 0; j < 2; j++)
    for (int i = 0; i < N; i++) {
      tri(&b, id[j][i], id[j][i + 1], id[j + 1][i + 1]);
      tri(&b, id[j][i], id[j + 1][i + 1], id[j + 1][i]);
    }
  return finish(&b);
}
