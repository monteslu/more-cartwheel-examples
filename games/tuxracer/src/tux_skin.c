/* Tux's sculpted body: one smooth skinned surface over the original tux.tcl
 * skeleton (the joints and the poses are the original's; only the shapes are
 * new). The surface is the zero set of a signed distance field built from
 * sculpting primitives (ellipsoids and flattened round cones) joined with
 * smooth unions, extracted with surface nets on a regular grid at load. Each
 * primitive rides one joint: the torso, neck, head, tail, the flippers on the
 * shoulders and the legs on the hips, knees and ankles, so every vertex is
 * weighted by its nearest primitives (soft across the neck, tight at the
 * limbs) and the slide, the walk-in and the jump bend him as before. The
 * plumage colour is painted per vertex: a dark blue-black back, a white front
 * from the chin down, an orange-yellow beak, legs and feet. The eyes are their
 * own small glossy mesh on the head joint (a white with a black pupil and a
 * painted catchlight, the iris on the mesh's own latitude rings so its edge is
 * crisp). All coordinates below are the model's own (tux.tcl's units, the
 * torso joint's space: y up, z out of the belly, x to his left), 0.35 m each. */
#include "game.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/skin_shading.h"
#include "cartwheel/shading.h"

#define CELL 0.024f /* the grid's cell: 8.4 mm */

enum { B_TORSO, B_NECK, B_HEAD, B_TAIL, B_LSH, B_RSH, B_LHIP, B_RHIP, B_LKNEE, B_RKNEE, B_LANKLE, B_RANKLE, B_COUNT };
enum { C_PLUMAGE, C_DARK, C_BEAK, C_BEAK_LOW, C_LEG };
enum { P_EGG, P_CONE };

typedef struct prim {
  int kind, bone, col;
  t3_vec3 c, ax, ay, az; /* egg: centre and axes (unit); cone: from c to b */
  t3_vec3 r;             /* egg: radii; cone: r.x at c, r.y at b, r.z the flattening (thickness over width) */
  t3_vec3 b, n;          /* cone: the far end and the flattening normal */
  float k, tau;          /* smooth union radius with what came before; the weight's softness */
  float bound;           /* the bounding sphere's radius around bc */
  t3_vec3 bc;
} prim;

static prim P[40];
static int np;

static t3_vec3 v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 rot_x(t3_vec3 v, float a) { float c = cosf(a), s = sinf(a); return v3(v.x, c * v.y - s * v.z, s * v.y + c * v.z); }
static t3_vec3 rot_y(t3_vec3 v, float a) { float c = cosf(a), s = sinf(a); return v3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z); }
static t3_vec3 rot_z(t3_vec3 v, float a) { float c = cosf(a), s = sinf(a); return v3(c * v.x - s * v.y, s * v.x + c * v.y, v.z); }

/* an ellipsoid turned by pitch (about x), then yaw (about y), then roll (about z), degrees */
static void egg(int bone, int col, t3_vec3 c, t3_vec3 r, float pitch, float yaw, float roll, float k, float tau) {
  prim *p = &P[np++];
  memset(p, 0, sizeof *p);
  p->kind = P_EGG, p->bone = bone, p->col = col, p->c = c, p->r = r, p->k = k, p->tau = tau;
  float a = pitch * 0.0174533f, b = yaw * 0.0174533f, g = roll * 0.0174533f;
  p->ax = rot_z(rot_y(rot_x(v3(1, 0, 0), a), b), g);
  p->ay = rot_z(rot_y(rot_x(v3(0, 1, 0), a), b), g);
  p->az = rot_z(rot_y(rot_x(v3(0, 0, 1), a), b), g);
  p->bc = c, p->bound = fmaxf(r.x, fmaxf(r.y, r.z));
}
/* an ellipsoid on given axes: x along u, y along v (both unit, at right angles) */
static void egg_axes(int bone, int col, t3_vec3 c, t3_vec3 u, t3_vec3 v, t3_vec3 r, float k, float tau) {
  prim *p = &P[np++];
  memset(p, 0, sizeof *p);
  p->kind = P_EGG, p->bone = bone, p->col = col, p->c = c, p->r = r, p->k = k, p->tau = tau;
  p->ax = t3_vec3_normalize(u);
  p->ay = t3_vec3_normalize(t3_vec3_sub(v, t3_vec3_scale(p->ax, t3_vec3_dot(v, p->ax))));
  p->az = t3_vec3_cross(p->ax, p->ay);
  p->bc = c, p->bound = fmaxf(r.x, fmaxf(r.y, r.z));
}
/* a round cone from a (radius ra) to b (radius rb), squashed to flat (0..1) of its width along n (n at right angles to b - a) */
static void cone(int bone, int col, t3_vec3 a, float ra, t3_vec3 b, float rb, t3_vec3 n, float flat, float k, float tau) {
  prim *p = &P[np++];
  memset(p, 0, sizeof *p);
  p->kind = P_CONE, p->bone = bone, p->col = col, p->c = a, p->b = b, p->r = v3(ra, rb, flat), p->k = k, p->tau = tau;
  t3_vec3 d = t3_vec3_sub(b, a);
  t3_vec3 nn = t3_vec3_normalize(t3_vec3_sub(n, t3_vec3_scale(d, t3_vec3_dot(n, d) / t3_vec3_dot(d, d))));
  p->n = nn;
  p->bc = t3_vec3_lerp(a, b, 0.5f), p->bound = t3_vec3_length(d) * 0.5f + fmaxf(ra, rb);
}

static float sd_egg(const prim *p, t3_vec3 q) {
  t3_vec3 d = t3_vec3_sub(q, p->c);
  t3_vec3 l = { t3_vec3_dot(d, p->ax) / p->r.x, t3_vec3_dot(d, p->ay) / p->r.y, t3_vec3_dot(d, p->az) / p->r.z };
  t3_vec3 l2 = { l.x / p->r.x, l.y / p->r.y, l.z / p->r.z };
  float k0 = t3_vec3_length(l), k1 = t3_vec3_length(l2);
  if (k1 < 1e-9f) return -fminf(p->r.x, fminf(p->r.y, p->r.z));
  return k0 * (k0 - 1) / k1;
}
/* iq's round cone, in a space stretched along n by 1 / flat; the distance scaled back (a bound, close near the surface) */
static float sd_cone(const prim *p, t3_vec3 q) {
  t3_vec3 d = t3_vec3_sub(q, p->c);
  float h = t3_vec3_dot(d, p->n), flat = p->r.z;
  d = t3_vec3_add(d, t3_vec3_scale(p->n, h * (1 / flat - 1)));
  t3_vec3 ba = t3_vec3_sub(p->b, p->c);
  float r1 = p->r.x, r2 = p->r.y;
  float l2 = t3_vec3_dot(ba, ba), rr = r1 - r2, a2 = l2 - rr * rr, il2 = 1.0f / l2;
  float y = t3_vec3_dot(d, ba), z = y - l2;
  t3_vec3 xv = t3_vec3_sub(t3_vec3_scale(d, l2), t3_vec3_scale(ba, y));
  float x2 = t3_vec3_dot(xv, xv), y2 = y * y * l2, z2 = z * z * l2;
  float k = rr > 0 ? rr * rr * x2 : -rr * rr * x2;
  float s;
  if ((z > 0 ? 1.f : -1.f) * a2 * z2 > k) s = sqrtf(x2 + z2) * il2 - r2;
  else if ((y > 0 ? 1.f : -1.f) * a2 * y2 < k) s = sqrtf(x2 + y2) * il2 - r1;
  else s = (sqrtf(x2 * a2 * il2) + y * rr) * il2 - r1;
  return s * flat;
}
static float sd_prim(const prim *p, t3_vec3 q) { return p->kind == P_EGG ? sd_egg(p, q) : sd_cone(p, q); }
static float smin(float a, float b, float k) {
  if (k <= 0) return fminf(a, b);
  float h = fmaxf(k - fabsf(a - b), 0) / k;
  return fminf(a, b) - h * h * k * 0.25f;
}
static float sdf(t3_vec3 q) {
  float d = 1e9f;
  for (int i = 0; i < np; i++) {
    const prim *p = &P[i];
    float lb = t3_vec3_length(t3_vec3_sub(q, p->bc)) - p->bound; /* never more than the distance */
    if (lb > d + p->k) continue;
    d = smin(d, sd_prim(p, q), p->k);
  }
  return d;
}
static t3_vec3 gradient(t3_vec3 q, float h) {
  return t3_vec3_normalize(v3(sdf(v3(q.x + h, q.y, q.z)) - sdf(v3(q.x - h, q.y, q.z)), sdf(v3(q.x, q.y + h, q.z)) - sdf(v3(q.x, q.y - h, q.z)),
                              sdf(v3(q.x, q.y, q.z + h)) - sdf(v3(q.x, q.y, q.z - h))));
}

/* the plumage: white over the front from the chin down, the back dark blue-black (linear albedo) */
static const t3_vec3 DARK = { 0.030f, 0.038f, 0.075f }, WHITE = { 0.80f, 0.80f, 0.82f };
static const t3_vec3 ORANGE = { 1.0f, 0.52f, 0.05f }, ORANGE_LOW = { 0.92f, 0.42f, 0.04f };
static float sstep(float a, float b, float x) { float t = fminf(1, fmaxf(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); }
static float white_share(t3_vec3 q) {
  /* the patch: an oval on the front, wide over the belly, narrowing up the throat to the chin */
  float t = fminf(1, fmaxf(0, (q.y + 0.6f) / 1.75f));
  float hw = 0.50f - 0.24f * t * t;              /* its half width */
  float zc = 0.20f + 0.06f * t;                  /* how far forward it starts */
  float top = 1.15f - 1.6f * (q.x * q.x);        /* the chin, a curve under the beak */
  float s = fminf(fminf(1 - (q.x * q.x) / (hw * hw), (q.z - zc) * 4), (top - q.y) * 4);
  return sstep(-0.1f, 0.1f, s);
}
static t3_vec3 colour_of(int col, t3_vec3 q) {
  switch (col) {
    case C_PLUMAGE: { float w = white_share(q); return t3_vec3_lerp(DARK, WHITE, w); }
    case C_DARK: return DARK;
    case C_BEAK: return ORANGE;
    case C_BEAK_LOW: return ORANGE_LOW;
    default: return ORANGE;
  }
}

/* the sculpt (model units; the joints' rest positions are tux.tcl's, the shoulders moved in to the slimmer body) */
static void sculpt(const t3_vec3 jp[B_COUNT]) {
  np = 0;
  /* the torso: a racer's torpedo, broad at the chest and narrowing to the hips */
  egg(B_TORSO, C_PLUMAGE, v3(0, -0.30f, 0.05f), v3(0.58f, 0.72f, 0.70f), 0, 0, 0, 0, 0.10f);
  egg(B_TORSO, C_PLUMAGE, v3(0, 0.32f, 0.04f), v3(0.62f, 0.66f, 0.64f), 0, 0, 0, 0.30f, 0.10f);
  /* the neck, between the chest and the head */
  egg(B_NECK, C_PLUMAGE, v3(0, 0.98f, 0.10f), v3(0.34f, 0.36f, 0.38f), 0, 0, 0, 0.30f, 0.10f);
  /* the head: the crown, the face a little forward, brows over the eyes */
  egg(B_HEAD, C_PLUMAGE, v3(0, 1.47f, 0.13f), v3(0.42f, 0.42f, 0.42f), 0, 0, 0, 0.20f, 0.10f);
  egg(B_HEAD, C_PLUMAGE, v3(0, 1.33f, 0.27f), v3(0.33f, 0.26f, 0.30f), 0, 0, 0, 0.16f, 0.10f);
  egg(B_HEAD, C_DARK, v3(0.135f, 1.60f, 0.44f), v3(0.12f, 0.045f, 0.08f), 0, 12, -14, 0.08f, 0.04f);
  egg(B_HEAD, C_DARK, v3(-0.135f, 1.60f, 0.44f), v3(0.12f, 0.045f, 0.08f), 0, -12, 14, 0.08f, 0.04f);
  /* the beak: a broad upper bill tipping down a little, the lower bill under it */
  egg(B_HEAD, C_BEAK, v3(0, 1.255f, 0.61f), v3(0.135f, 0.065f, 0.24f), 12, 0, 0, 0.05f, 0.012f);
  egg(B_HEAD, C_BEAK_LOW, v3(0, 1.185f, 0.56f), v3(0.12f, 0.045f, 0.16f), 4, 0, 0, 0.03f, 0.012f);
  /* the tail: short stiff feathers down and back */
  egg(B_TAIL, C_DARK, v3(0, -0.98f, -0.32f), v3(0.17f, 0.22f, 0.12f), -30, 0, 0, 0.30f, 0.06f);
  /* the flippers: flat, tapered, along the shoulder's rest chain (tux.tcl's upper arm, forearm and hand) */
  for (int s = 0; s < 2; s++) {
    float sx = s ? -1.0f : 1.0f;
    int bone = s ? B_RSH : B_LSH;
    t3_vec3 sh = jp[bone];
    t3_vec3 a = v3(sh.x + sx * 0.02f, sh.y - 0.02f, sh.z), w = v3(sh.x + sx * 0.49f, sh.y - 0.70f, sh.z);
    t3_vec3 tip = v3(sh.x + sx * 0.50f, sh.y - 0.86f, sh.z - 0.02f);
    t3_vec3 fn = v3(sx * 0.85f, 0.53f, 0); /* one flat face for the whole flipper: no kink at the elbow */
    /* the blade stands just off the side of the chest (the pivot stays inside it) so the whole flipper shows */
    a = t3_vec3_add(a, t3_vec3_scale(fn, 0.05f)), w = t3_vec3_add(w, t3_vec3_scale(fn, 0.07f)), tip = t3_vec3_add(tip, t3_vec3_scale(fn, 0.07f));
    /* one blade: an ellipsoid from the shoulder to the tip, broad front to back, thin across */
    (void)w;
    t3_vec3 along = t3_vec3_sub(tip, a);
    float len = t3_vec3_length(along);
    egg_axes(bone, C_DARK, t3_vec3_lerp(a, tip, 0.5f), along, fn, v3(len * 0.5f + 0.02f, 0.045f, 0.13f), 0.08f, 0.03f);
  }
  /* the legs: a short dark thigh into the body, orange shins, webbed feet with three toes, soles flat at y -1.5 */
  for (int s = 0; s < 2; s++) {
    float sx = s ? -1.0f : 1.0f;
    t3_vec3 hip = jp[s ? B_RHIP : B_LHIP], knee = jp[s ? B_RKNEE : B_LKNEE], ank = jp[s ? B_RANKLE : B_LANKLE];
    int bh = s ? B_RHIP : B_LHIP, bk = s ? B_RKNEE : B_LKNEE, ba = s ? B_RANKLE : B_LANKLE;
    cone(bh, C_PLUMAGE, v3(hip.x * 0.85f, hip.y - 0.06f, hip.z), 0.11f, knee, 0.09f, v3(0, 0, 1), 1, 0.08f, 0.03f);
    cone(bk, C_LEG, knee, 0.085f, v3(ank.x, ank.y + 0.02f, ank.z), 0.06f, v3(0, 0, 1), 1, 0.06f, 0.03f);
    float yaw = sx * 14.0f; /* the toes turned out a little */
    t3_vec3 fwd = rot_y(v3(0, 0, 1), yaw * 0.0174533f);
    t3_vec3 heel = v3(ank.x, -1.455f, ank.z - 0.02f);
    egg(ba, C_LEG, v3(ank.x + fwd.x * 0.17f, -1.455f, ank.z + fwd.z * 0.17f), v3(0.16f, 0.045f, 0.24f), 0, yaw, 0, 0.06f, 0.03f);
    egg(ba, C_LEG, heel, v3(0.085f, 0.045f, 0.09f), 0, yaw, 0, 0.06f, 0.03f);
    for (int t = -1; t <= 1; t++) {
      t3_vec3 dir = rot_y(fwd, t * 0.42f);
      float len = t ? 0.36f : 0.42f;
      t3_vec3 from = v3(heel.x + fwd.x * 0.05f, -1.45f, heel.z + fwd.z * 0.05f);
      t3_vec3 to = v3(from.x + dir.x * len, -1.465f, from.z + dir.z * len);
      cone(ba, C_LEG, from, 0.05f, to, 0.035f, v3(0, 1, 0), 0.8f, 0.04f, 0.03f);
    }
  }
}

typedef struct skv { t3_vec3 p, n, c; float w[B_COUNT]; } skv;

/* the vertex's paint and weights: each primitive's share falls off with how far it is behind the nearest */
static void shade_vertex(skv *v) {
  float d[40], dmin = 1e9f;
  for (int i = 0; i < np; i++) d[i] = sd_prim(&P[i], v->p), dmin = fminf(dmin, d[i]);
  float tot = 0;
  t3_vec3 c = { 0, 0, 0 };
  memset(v->w, 0, sizeof v->w);
  for (int i = 0; i < np; i++) {
    float w = expf(-(d[i] - dmin) / P[i].tau);
    if (w < 1e-4f) continue;
    tot += w;
    c = t3_vec3_add(c, t3_vec3_scale(colour_of(P[i].col, v->p), w));
    v->w[P[i].bone] += w;
  }
  v->c = t3_vec3_scale(c, 1 / tot);
  for (int b = 0; b < B_COUNT; b++) v->w[b] /= tot;
}

static t3_geometry *g_body, *g_eyes; /* built once, shared by Tux and his ghost */
static float g_sole[2][TUX_GROUND_PTS][3], g_belly[TUX_GROUND_PTS][3];
static int g_sole_n[2], g_belly_n;

static t3_geometry *build_body(const t3_vec3 jp[B_COUNT], const t3_mat4 *to_root, const t3_mat4 inv_bone[B_COUNT]) {
  sculpt(jp);
  t3_vec3 lo = { 1e9f, 1e9f, 1e9f }, hi = { -1e9f, -1e9f, -1e9f };
  for (int i = 0; i < np; i++) {
    float r = P[i].bound + 0.05f;
    lo = v3(fminf(lo.x, P[i].bc.x - r), fminf(lo.y, P[i].bc.y - r), fminf(lo.z, P[i].bc.z - r));
    hi = v3(fmaxf(hi.x, P[i].bc.x + r), fmaxf(hi.y, P[i].bc.y + r), fmaxf(hi.z, P[i].bc.z + r));
  }
  const float h = CELL;
  int NX = (int)ceilf((hi.x - lo.x) / h) + 1, NY = (int)ceilf((hi.y - lo.y) / h) + 1, NZ = (int)ceilf((hi.z - lo.z) / h) + 1;
  float *F = malloc(sizeof(float) * (size_t)NX * NY * NZ);
  int *cell = malloc(sizeof(int) * (size_t)NX * NY * NZ);
#define FI(i, j, k) F[((size_t)(k) * NY + (j)) * NX + (i)]
#define CI(i, j, k) cell[((size_t)(k) * NY + (j)) * NX + (i)]
  for (int k = 0; k < NZ; k++)
    for (int j = 0; j < NY; j++)
      for (int i = 0; i < NX; i++) FI(i, j, k) = -sdf(v3(lo.x + i * h, lo.y + j * h, lo.z + k * h)); /* > 0 inside */
  /* surface nets: one vertex per cell the surface crosses, at the mean of its edge crossings */
  skv *vs = NULL;
  int nv = 0, vcap = 0;
  static const int E[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
  for (int k = 0; k < NZ - 1; k++)
    for (int j = 0; j < NY - 1; j++)
      for (int i = 0; i < NX - 1; i++) {
        float f[8];
        int inside = 0;
        for (int c = 0; c < 8; c++) f[c] = FI(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1)), inside |= (f[c] > 0) << c;
        CI(i, j, k) = -1;
        if (inside == 0 || inside == 255) continue;
        t3_vec3 sum = { 0, 0, 0 };
        int cnt = 0;
        for (int e = 0; e < 12; e++) {
          int a = E[e][0], b = E[e][1];
          if ((f[a] > 0) == (f[b] > 0)) continue;
          float t = f[a] / (f[a] - f[b]);
          t3_vec3 pa = { lo.x + (i + (a & 1)) * h, lo.y + (j + ((a >> 1) & 1)) * h, lo.z + (k + ((a >> 2) & 1)) * h };
          t3_vec3 pb = { lo.x + (i + (b & 1)) * h, lo.y + (j + ((b >> 1) & 1)) * h, lo.z + (k + ((b >> 2) & 1)) * h };
          sum = t3_vec3_add(sum, t3_vec3_lerp(pa, pb, t));
          cnt++;
        }
        if (nv == vcap) { vcap = vcap ? vcap * 2 : 8192; vs = realloc(vs, sizeof *vs * (size_t)vcap); }
        skv *v = &vs[nv];
        t3_vec3 p = t3_vec3_scale(sum, 1.0f / cnt);
        /* one Newton step onto the true surface: the mean of the crossings sits a little inside on curves */
        t3_vec3 g = gradient(p, h * 0.5f);
        p = t3_vec3_sub(p, t3_vec3_scale(g, sdf(p)));
        v->p = p;
        v->n = gradient(p, h * 0.5f);
        shade_vertex(v);
        CI(i, j, k) = nv++;
      }
  /* quads across every crossed edge, between the four cells around it */
  uint32_t *ix = NULL;
  int ni = 0, icap = 0;
  for (int k = 1; k < NZ - 1; k++)
    for (int j = 1; j < NY - 1; j++)
      for (int i = 1; i < NX - 1; i++) {
        float f0 = FI(i, j, k);
        for (int axis = 0; axis < 3; axis++) {
          int i1 = i + (axis == 0), j1 = j + (axis == 1), k1 = k + (axis == 2);
          float f1 = FI(i1, j1, k1);
          if ((f0 > 0) == (f1 > 0)) continue;
          int q[4];
          if (axis == 0) q[0] = CI(i, j - 1, k - 1), q[1] = CI(i, j, k - 1), q[2] = CI(i, j, k), q[3] = CI(i, j - 1, k);
          else if (axis == 1) q[0] = CI(i - 1, j, k - 1), q[1] = CI(i - 1, j, k), q[2] = CI(i, j, k), q[3] = CI(i, j, k - 1);
          else q[0] = CI(i - 1, j - 1, k), q[1] = CI(i, j - 1, k), q[2] = CI(i, j, k), q[3] = CI(i - 1, j, k);
          if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[3] < 0) continue;
          if (ni + 6 > icap) { icap = icap ? icap * 2 : 16384; ix = realloc(ix, sizeof *ix * (size_t)icap); }
          /* split along the shorter diagonal, each triangle wound with its vertices' normals */
          float d02 = t3_vec3_length_sq(t3_vec3_sub(vs[q[0]].p, vs[q[2]].p)), d13 = t3_vec3_length_sq(t3_vec3_sub(vs[q[1]].p, vs[q[3]].p));
          int tri[6];
          if (d02 <= d13) tri[0] = q[0], tri[1] = q[1], tri[2] = q[2], tri[3] = q[0], tri[4] = q[2], tri[5] = q[3];
          else tri[0] = q[0], tri[1] = q[1], tri[2] = q[3], tri[3] = q[1], tri[4] = q[2], tri[5] = q[3];
          for (int t = 0; t < 6; t += 3) {
            const skv *a = &vs[tri[t]], *b = &vs[tri[t + 1]], *c = &vs[tri[t + 2]];
            t3_vec3 fn = t3_vec3_cross(t3_vec3_sub(b->p, a->p), t3_vec3_sub(c->p, a->p));
            t3_vec3 vn = t3_vec3_add(t3_vec3_add(a->n, b->n), c->n);
            if (t3_vec3_dot(fn, vn) < 0) { int tmp = tri[t + 1]; tri[t + 1] = tri[t + 2]; tri[t + 2] = tmp; }
            ix[ni++] = (uint32_t)tri[t], ix[ni++] = (uint32_t)tri[t + 1], ix[ni++] = (uint32_t)tri[t + 2];
          }
        }
      }
  free(F), free(cell);
#undef FI
#undef CI
  /* the attributes, in the root's space (the skinned mesh is the root's child); four strongest bones a vertex */
  float *Pp = malloc(sizeof(float) * 3 * (size_t)nv), *Nn = malloc(sizeof(float) * 3 * (size_t)nv), *C = malloc(sizeof(float) * 3 * (size_t)nv);
  float *SI = calloc((size_t)nv * 4, sizeof(float)), *SW = calloc((size_t)nv * 4, sizeof(float));
  g_sole_n[0] = g_sole_n[1] = g_belly_n = 0;
  for (int v = 0; v < nv; v++) {
    t3_vec3 p = t3_vec3_apply_mat4(vs[v].p, to_root), n = t3_vec3_normalize(t3_vec3_transform_direction(vs[v].n, to_root));
    Pp[v * 3] = p.x, Pp[v * 3 + 1] = p.y, Pp[v * 3 + 2] = p.z;
    Nn[v * 3] = n.x, Nn[v * 3 + 1] = n.y, Nn[v * 3 + 2] = n.z;
    C[v * 3] = vs[v].c.x, C[v * 3 + 1] = vs[v].c.y, C[v * 3 + 2] = vs[v].c.z;
    float w[B_COUNT];
    memcpy(w, vs[v].w, sizeof w);
    float sum = 0;
    for (int s = 0; s < 4; s++) {
      int best = 0;
      for (int b = 1; b < B_COUNT; b++) if (w[b] > w[best]) best = b;
      SI[v * 4 + s] = (float)best, SW[v * 4 + s] = w[best], sum += w[best];
      w[best] = -1;
    }
    for (int s = 0; s < 4; s++) SW[v * 4 + s] /= sum;
    /* the grounding samples: the soles (y at the bottom of the feet) and the belly, in their bones' rest space */
    for (int s = 0; s < 2; s++) {
      int b = s ? B_RANKLE : B_LANKLE;
      if (vs[v].p.y < -1.49f && vs[v].w[b] > 0.95f && g_sole_n[s] < TUX_GROUND_PTS && (v % 3) == 0) {
        t3_vec3 l = t3_vec3_apply_mat4(p, &inv_bone[b]);
        g_sole[s][g_sole_n[s]][0] = l.x, g_sole[s][g_sole_n[s]][1] = l.y, g_sole[s][g_sole_n[s]][2] = l.z, g_sole_n[s]++;
      }
    }
    if (vs[v].p.z > 0.66f && vs[v].w[B_TORSO] > 0.95f && g_belly_n < TUX_GROUND_PTS && (v % 2) == 0) {
      t3_vec3 l = t3_vec3_apply_mat4(p, &inv_bone[B_TORSO]);
      g_belly[g_belly_n][0] = l.x, g_belly[g_belly_n][1] = l.y, g_belly[g_belly_n][2] = l.z, g_belly_n++;
    }
  }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *ap = t3_attribute_new(T3_FLOAT32, Pp, nv, 3), *an = t3_attribute_new(T3_FLOAT32, Nn, nv, 3), *ac = t3_attribute_new(T3_FLOAT32, C, nv, 3),
               *asi = t3_attribute_new(T3_FLOAT32, SI, nv, 4), *asw = t3_attribute_new(T3_FLOAT32, SW, nv, 4), *ai = t3_attribute_new(T3_UINT32, ix, ni, 1);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, ap);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, an);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, ac);
  t3_geometry_set_attribute(g, T3_ATTR_SKIN_INDEX, asi);
  t3_geometry_set_attribute(g, T3_ATTR_SKIN_WEIGHT, asw);
  t3_geometry_set_index(g, ai);
  t3_release(ap), t3_release(an), t3_release(ac), t3_release(asi), t3_release(asw), t3_release(ai);
  free(Pp), free(Nn), free(C), free(SI), free(SW), free(ix), free(vs);
  cw_info("tux body: %d vertices, %d triangles from %d primitives (grid %dx%dx%d, %d sole and %d belly samples)", nv, ni / 3, np, NX, NY, NZ,
          g_sole_n[0] + g_sole_n[1], g_belly_n);
  return g;
}

/* where a ray from far in front along -z at (x, y) meets the surface (model units) */
static float surface_z(float x, float y) {
  float z = 1.2f;
  for (int i = 0; i < 200; i++) {
    float d = sdf(v3(x, y, z));
    if (d < 1e-4f) break;
    z -= fmaxf(d, 1e-3f);
  }
  return z;
}

/* the eyes: two ovals set into the face under the brows, each a sphere whose pole looks out along its gaze, so the
 * pupil's edge is a latitude ring; a catchlight painted on the pupil */
#define EYE_SEG 64
#define EYE_RING 48
static t3_geometry *build_eyes(const t3_mat4 *to_local) {
  int per = (EYE_RING + 1) * (EYE_SEG + 1), nv = per * 2, ni = EYE_RING * EYE_SEG * 6 * 2;
  float *Pp = malloc(sizeof(float) * 3 * (size_t)nv), *Nn = malloc(sizeof(float) * 3 * (size_t)nv), *C = malloc(sizeof(float) * 3 * (size_t)nv);
  uint32_t *ix = malloc(sizeof(uint32_t) * (size_t)ni);
  int v = 0, n = 0;
  for (int s = 0; s < 2; s++) {
    float sx = s ? -1.0f : 1.0f;
    float ex = 0.128f * sx, ey = 1.455f;
    t3_vec3 rad = { 0.098f, 0.128f, 0.06f };     /* across, up, along the gaze */
    float zc = surface_z(ex, ey) - 0.035f;       /* set in: the oval stands about 9 mm out of the face */
    t3_vec3 c = { ex, ey, zc };
    /* the gaze: forward, turned out 12 degrees and down 4 (the head pitches up when he slides) */
    t3_vec3 gz = t3_vec3_normalize(rot_y(rot_x(v3(0, 0, 1), 4 * 0.0174533f), sx * 12 * 0.0174533f));
    t3_vec3 gx = t3_vec3_normalize(t3_vec3_cross(v3(0, 1, 0), gz)), gy = t3_vec3_cross(gz, gx);
    /* the pupil looks a little in and up from the oval's centre: focused ahead */
    float pup_x = -sx * 0.10f, pup_y = 0.06f;
    t3_vec3 pdir = t3_vec3_normalize(v3(pup_x, pup_y, 1));
    int base = v;
    for (int r = 0; r <= EYE_RING; r++) {
      float th = 3.14159265f * r / EYE_RING; /* 0 at the pole the gaze runs out of */
      for (int g = 0; g <= EYE_SEG; g++) {
        float ph = 6.2831853f * g / EYE_SEG;
        /* the unit direction in the eye's frame, the pole on the pupil's line */
        t3_vec3 u = { sinf(th) * cosf(ph), sinf(th) * sinf(ph), cosf(th) };
        t3_vec3 zax = pdir, xax = t3_vec3_normalize(t3_vec3_cross(v3(0, 1, 0), zax)), yax = t3_vec3_cross(zax, xax);
        t3_vec3 d = t3_vec3_add(t3_vec3_add(t3_vec3_scale(xax, u.x), t3_vec3_scale(yax, u.y)), t3_vec3_scale(zax, u.z));
        t3_vec3 le = { d.x * rad.x, d.y * rad.y, d.z * rad.z };
        t3_vec3 ln = t3_vec3_normalize(v3(d.x / rad.x, d.y / rad.y, d.z / rad.z));
        t3_vec3 p = t3_vec3_add(c, t3_vec3_add(t3_vec3_add(t3_vec3_scale(gx, le.x), t3_vec3_scale(gy, le.y)), t3_vec3_scale(gz, le.z)));
        t3_vec3 nn = t3_vec3_add(t3_vec3_add(t3_vec3_scale(gx, ln.x), t3_vec3_scale(gy, ln.y)), t3_vec3_scale(gz, ln.z));
        p = t3_vec3_apply_mat4(p, to_local);
        nn = t3_vec3_normalize(t3_vec3_transform_direction(nn, to_local));
        float deg = th * 57.29578f;
        t3_vec3 col;
        if (deg < 27) col = v3(0.006f, 0.006f, 0.008f);          /* the pupil */
        else if (deg < 31) col = v3(0.03f, 0.022f, 0.016f);      /* its rim */
        else col = v3(0.86f, 0.86f, 0.84f);                       /* the white */
        /* the catchlight: up and to his left on both pupils (one light) */
        float hx = u.x - 0.17f, hy = u.y - 0.22f;
        if (deg < 27 && hx * hx + hy * hy < 0.075f * 0.075f) col = v3(0.95f, 0.95f, 0.95f);
        Pp[v * 3] = p.x, Pp[v * 3 + 1] = p.y, Pp[v * 3 + 2] = p.z;
        Nn[v * 3] = nn.x, Nn[v * 3 + 1] = nn.y, Nn[v * 3 + 2] = nn.z;
        C[v * 3] = col.x, C[v * 3 + 1] = col.y, C[v * 3 + 2] = col.z;
        v++;
      }
    }
    for (int r = 0; r < EYE_RING; r++)
      for (int g = 0; g < EYE_SEG; g++) {
        uint32_t a = (uint32_t)(base + r * (EYE_SEG + 1) + g), b = a + EYE_SEG + 1;
        ix[n++] = a, ix[n++] = b, ix[n++] = a + 1;
        ix[n++] = a + 1, ix[n++] = b, ix[n++] = b + 1;
      }
  }
  /* wound outward: check one triangle against its normal, flip all if needed */
  {
    const float *a = Pp + ix[3] * 3, *b = Pp + ix[4] * 3, *c = Pp + ix[5] * 3;
    t3_vec3 fn = t3_vec3_cross(v3(b[0] - a[0], b[1] - a[1], b[2] - a[2]), v3(c[0] - a[0], c[1] - a[1], c[2] - a[2]));
    t3_vec3 vn = v3(Nn[ix[3] * 3], Nn[ix[3] * 3 + 1], Nn[ix[3] * 3 + 2]);
    if (t3_vec3_dot(fn, vn) < 0)
      for (int t = 0; t < n; t += 3) { uint32_t tmp = ix[t + 1]; ix[t + 1] = ix[t + 2]; ix[t + 2] = tmp; }
  }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *ap = t3_attribute_new(T3_FLOAT32, Pp, nv, 3), *an = t3_attribute_new(T3_FLOAT32, Nn, nv, 3), *ac = t3_attribute_new(T3_FLOAT32, C, nv, 3),
               *ai = t3_attribute_new(T3_UINT32, ix, n, 1);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, ap);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, an);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, ac);
  t3_geometry_set_index(g, ai);
  t3_release(ap), t3_release(an), t3_release(ac), t3_release(ai);
  free(Pp), free(Nn), free(C), free(ix);
  return g;
}

static int bone_node(const tux_model *m, int b) {
  switch (b) {
    case B_TORSO: return 1;
    case B_NECK: return m->joint[J_NECK];
    case B_HEAD: return m->joint[J_HEAD];
    case B_TAIL: return m->joint[J_TAIL];
    case B_LSH: return m->joint[J_LSHOULDER];
    case B_RSH: return m->joint[J_RSHOULDER];
    case B_LHIP: return m->joint[J_LHIP];
    case B_RHIP: return m->joint[J_RHIP];
    case B_LKNEE: return m->joint[J_LKNEE];
    case B_RKNEE: return m->joint[J_RKNEE];
    case B_LANKLE: return m->joint[J_LANKLE];
    default: return m->joint[J_RANKLE];
  }
}

bool tux_skin_build(tux_model *m) {
  if (m->count < 86) return false;
  for (int j = 0; j < J_COUNT; j++) if (m->joint[j] < 0) return false;
  /* the shoulders in by 0.1 to the slimmer chest (the whole flipper chain moves with them) */
  for (int s = 0; s < 2; s++) {
    tux_node *sh = &m->nodes[m->nodes[m->joint[s ? J_RSHOULDER : J_LSHOULDER]].parent];
    sh->base[12] += s ? 0.1 : -0.1;
    memcpy(sh->cur, sh->base, sizeof sh->cur);
    t3_object_set_matrix_d(sh->obj, sh->cur);
  }
  for (int i = 0; i < m->count; i++) if (m->nodes[i].sphere && m->nodes[i].obj) m->nodes[i].obj->visible = false;
  t3_object_update_world_matrix(m->root, false, true); /* the rest pose, the root at the origin */
  t3_object *bones[B_COUNT];
  t3_mat4 inv_bone[B_COUNT];
  t3_vec3 jp[B_COUNT];
  const t3_mat4 *to_root = &m->nodes[1].obj->matrix_world; /* model units to the root's space */
  t3_mat4 from_root;
  t3_mat4_invert(&from_root, to_root);
  for (int b = 0; b < B_COUNT; b++) {
    bones[b] = m->nodes[bone_node(m, b)].obj;
    t3_mat4_invert(&inv_bone[b], &bones[b]->matrix_world);
    t3_vec3 o = { bones[b]->matrix_world.e[12], bones[b]->matrix_world.e[13], bones[b]->matrix_world.e[14] };
    jp[b] = t3_vec3_apply_mat4(o, &from_root);
  }
  if (!g_body) g_body = build_body(jp, to_root, inv_bone);
  else sculpt(jp); /* the primitives again for the eyes' placement */
  memcpy(m->sole, g_sole, sizeof m->sole), memcpy(m->sole_n, g_sole_n, sizeof m->sole_n);
  memcpy(m->belly, g_belly, sizeof m->belly), m->belly_n = g_belly_n;
  m->ground_bone[0] = bones[B_LANKLE], m->ground_bone[1] = bones[B_RANKLE], m->ground_bone[2] = bones[B_TORSO];

  /* the plumage: matte (a rough diffuse, no sheen, a low specular), the colour from the vertices */
  t3_material *mat = t3_mesh_physical_material_new(0xffffff);
  mat->vertex_colors = true;
  mat->roughness = 0.82f;
  mat->metalness = 0;
  mat->specular_intensity = 0.5f;
  mat->diffuse_roughness = 0.6f;
  cw_shading_add(mat, "feather"); /* shaders/feather.mjs: a cool rim on the dark plumage */
  {
    cw_skin_shading_desc sd = cw_skin_shading_default();
    sd.scatter = (t3_color){ 0.45f, 0.42f, 0.40f };
    sd.wrap = 0.2f, sd.translucency = 0.06f, sd.distortion = 0.3f, sd.power = 4;
    cw_skin_shading_apply(mat, &sd);
  }
  m->skin_mat = mat;
  t3_skinned_mesh *sm = t3_skinned_mesh_new(g_body, mat);
  sm->mesh.base.frustum_culled = false;
  sm->mesh.base.cast_shadow = true;
  sm->mesh.base.receive_shadow = true;
  t3_object_add(m->root, sm);
  t3_skeleton *sk = t3_skeleton_new(bones, B_COUNT, NULL);
  t3_object_update_world_matrix(sm, false, false);
  t3_skinned_mesh_bind(sm, sk, NULL);
  t3_release(sk);
  m->skin = sm;

  /* the eyes on the head joint: glossy, so they catch the sky */
  t3_object *head = m->nodes[m->joint[J_HEAD]].obj;
  if (!g_eyes) {
    t3_mat4 to_local, inv_head;
    t3_mat4_invert(&inv_head, &head->matrix_world);
    t3_mat4_multiply(&to_local, &inv_head, to_root);
    g_eyes = build_eyes(&to_local);
  }
  if (m->mat_count < TUX_MAX_MATERIALS) {
    t3_material *em = t3_mesh_physical_material_new(0xffffff);
    em->vertex_colors = true;
    em->roughness = 0.18f;
    em->metalness = 0;
    m->mats[m->mat_count++] = em;
    t3_mesh *eyes = t3_mesh_new(g_eyes, em);
    eyes->base.cast_shadow = false;
    eyes->base.receive_shadow = true;
    t3_object_add(head, eyes);
    m->eyes = eyes;
  }
  return true;
}

/* the grounding samples of a sole (0 left, 1 right) or the belly (2) in the world as the model is posed now; their count */
int tux_ground_points(tux_model *m, int part, t3_vec3 *out, int cap) {
  if (!m->root || part < 0 || part > 2 || !m->ground_bone[part]) return 0;
  t3_object_update_world_matrix(m->root, true, true);
  const t3_mat4 *w = &m->ground_bone[part]->matrix_world;
  int n = part < 2 ? m->sole_n[part] : m->belly_n;
  if (n > cap) n = cap;
  for (int i = 0; i < n; i++) {
    const float *l = part < 2 ? m->sole[part][i] : m->belly[i];
    out[i] = t3_vec3_apply_mat4((t3_vec3){ l[0], l[1], l[2] }, w);
  }
  return n;
}
