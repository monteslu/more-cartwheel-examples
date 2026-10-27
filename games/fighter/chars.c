/* fighter characters: a real skinned mesh per fighter, built in code (no model
 * files). A 17-bone skeleton, limbs and torso as tapered elliptical tubes
 * whose rings blend between neighbouring bones at the joints (smooth elbows
 * and knees), rigid hardware (helmets, gauntlets, pads, hair) on bone objects,
 * secondary motion on ponytails and headbands, PBR fabric and skin materials.
 *
 * Poses come from the simulation: the state, the frame of the move, and the
 * move's frame data drive the animation, so what you see is exactly what the
 * rules do (startup winds up, active frames hold the strike, recovery returns).
 * Everything here only reads the sim; nothing feeds back. */
#include <stdlib.h>
#include <string.h>

#include "game.h"

enum { HIPS, SPINE, CHEST, NECK, HEAD, UAR_L, FAR_L, HAND_L, UAR_R, FAR_R, HAND_R, THI_L, SHI_L, FOO_L, THI_R, SHI_R, FOO_R, JN };
static const int8_t PARENT[JN] = { -1, HIPS, SPINE, CHEST, NECK, CHEST, UAR_L, FAR_L, CHEST, UAR_R, FAR_R, HIPS, THI_L, SHI_L, HIPS, THI_R, SHI_R };
/* rest positions (metres, y up, +z forward, +x the model's left), absolute */
static const float REST[JN][3] = {
  { 0, 0.95f, 0 }, { 0, 1.05f, 0 }, { 0, 1.28f, 0 }, { 0, 1.47f, 0 }, { 0, 1.55f, 0 },
  { 0.20f, 1.43f, 0 }, { 0.21f, 1.13f, 0 }, { 0.21f, 0.86f, 0 },
  { -0.20f, 1.43f, 0 }, { -0.21f, 1.13f, 0 }, { -0.21f, 0.86f, 0 },
  { 0.10f, 0.93f, 0 }, { 0.10f, 0.50f, 0 }, { 0.10f, 0.07f, 0 },
  { -0.10f, 0.93f, 0 }, { -0.10f, 0.50f, 0 }, { -0.10f, 0.07f, 0 },
};

typedef struct spec {
  const char *name;
  float scale, bulk, shoulder;
  float suit[3], accent[3], skin[3], trim[3];
  float stance, guard, bounce, kickiness; /* stance width, guard height, idle bounce, how much it kicks */
  int kind;
} spec;
/* the roster in data order: bolt, grit, vex, mara */
static const spec SPECS[4] = {
  { "bolt", 1.00f, 1.00f, 1.00f, { 0.93f, 0.93f, 0.90f }, { 0.85f, 0.12f, 0.1f }, { 0.78f, 0.55f, 0.42f }, { 0.15f, 0.15f, 0.17f }, 0.12f, 1.0f, 1.0f, 0.4f, 0 },
  { "grit", 1.06f, 1.38f, 1.25f, { 0.32f, 0.34f, 0.38f }, { 0.95f, 0.5f, 0.08f }, { 0.55f, 0.36f, 0.27f }, { 0.12f, 0.12f, 0.13f }, 0.17f, 0.8f, 0.5f, 0.2f, 1 },
  { "vex", 0.94f, 0.82f, 0.9f, { 0.08f, 0.09f, 0.12f }, { 0.1f, 0.85f, 0.95f }, { 0.85f, 0.65f, 0.55f }, { 0.2f, 0.2f, 0.24f }, 0.14f, 1.1f, 1.6f, 1.0f, 2 },
  { "mara", 0.97f, 0.88f, 0.92f, { 0.15f, 0.42f, 0.28f }, { 0.95f, 0.78f, 0.2f }, { 0.9f, 0.72f, 0.58f }, { 0.12f, 0.12f, 0.1f }, 0.11f, 1.2f, 1.3f, 0.7f, 3 },
};

/* ── mesh building ───────────────────────────────────────────────────── */

typedef struct vbuf { float *p, *n, *uv, *c, *si, *sw; uint32_t *idx; int nv, ni, cv, ci; } vbuf;
static void vb_reserve(vbuf *b, int add_v, int add_i) {
  if (b->nv + add_v > b->cv) {
    b->cv = (b->nv + add_v) * 2;
    b->p = realloc(b->p, sizeof(float) * 3 * (size_t)b->cv);
    b->n = realloc(b->n, sizeof(float) * 3 * (size_t)b->cv);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)b->cv);
    b->c = realloc(b->c, sizeof(float) * 3 * (size_t)b->cv);
    b->si = realloc(b->si, sizeof(float) * 4 * (size_t)b->cv);
    b->sw = realloc(b->sw, sizeof(float) * 4 * (size_t)b->cv);
  }
  if (b->ni + add_i > b->ci) {
    b->ci = (b->ni + add_i) * 2;
    b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)b->ci);
  }
}
static void v3sub(const float *a, const float *b, float *o) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static float v3len(const float *a) { return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
static void v3cross(const float *a, const float *b, float *o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }

typedef struct seg {
  float a[3], b[3];
  float rx0, rz0, rx1, rz1, bulge;
  int pb, ob, cb;   /* bone blended at the start, own bone, bone blended at the end */
  int skin;         /* 0 suit, 1 skin */
  float col[3], col2[3]; /* colour at the start and at the end */
  bool cap0, cap1;
} seg;

static float srgb2lin(float c) { return c <= 0.04045f ? c / 12.92f : cw_pow((c + 0.055f) / 1.055f, 2.4f); }
static float sm(float e0, float e1, float x) { float t = cw_clampf((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t); }

#define SIDES 14
static void add_tube(vbuf *vb, const seg *s) {
  float ax[3];
  v3sub(s->b, s->a, ax);
  float L = v3len(ax);
  ax[0] /= L; ax[1] /= L; ax[2] /= L;
  float ref[3] = { 1, 0, 0 };
  if (fabsf(ax[0]) > 0.9f) { ref[0] = 0; ref[1] = 1; }
  float dr = ref[0] * ax[0] + ref[1] * ax[1] + ref[2] * ax[2];
  float u[3] = { ref[0] - ax[0] * dr, ref[1] - ax[1] * dr, ref[2] - ax[2] * dr };
  float ul = v3len(u);
  u[0] /= ul; u[1] /= ul; u[2] /= ul;
  float w[3];
  v3cross(ax, u, w);
  /* rings: caps at both ends, then the body */
  struct ring { float d, k, na; float t; } rings[24];
  int nr = 0;
  static const float CAPA[3] = { 70, 40, 15 };
  float rmax0 = fmaxf(s->rx0, s->rz0), rmax1 = fmaxf(s->rx1, s->rz1);
  if (s->cap0) {
    rings[nr++] = (struct ring){ -rmax0, 0.0f, -1, 0 };
    for (int i = 0; i < 3; i++) {
      float a = CAPA[i] * 0.0174533f, sa = (float)cw_sin(a), ca = (float)cw_cos(a);
      rings[nr++] = (struct ring){ -rmax0 * sa, ca, -sa, 0 };
    }
  }
  const int BODY = 7;
  for (int i = 0; i <= BODY; i++) {
    float t = (float)i / BODY;
    rings[nr++] = (struct ring){ t * L, 1.0f, 0, t };
  }
  if (s->cap1) {
    for (int i = 2; i >= 0; i--) {
      float a = CAPA[i] * 0.0174533f, sa = (float)cw_sin(a), ca = (float)cw_cos(a);
      rings[nr++] = (struct ring){ L + rmax1 * sa, ca, sa, 1 };
    }
    rings[nr++] = (struct ring){ L + rmax1, 0.0f, 1, 1 };
  }
  vb_reserve(vb, nr * (SIDES + 1), nr * SIDES * 6);
  int base = vb->nv;
  for (int r = 0; r < nr; r++) {
    float t = rings[r].t;
    float rx = (s->rx0 + (s->rx1 - s->rx0) * t) * (1 + s->bulge * (float)cw_sin(t * 3.14159265f)) * rings[r].k;
    float rz = (s->rz0 + (s->rz1 - s->rz0) * t) * (1 + s->bulge * (float)cw_sin(t * 3.14159265f)) * rings[r].k;
    float wp = 0.5f * (1 - sm(0, 0.35f, t)), wc = 0.5f * sm(0.65f, 1, t);
    if (s->pb == s->ob) wp = 0;
    if (s->cb == s->ob) wc = 0;
    for (int k = 0; k <= SIDES; k++) {
      float phi = (float)k / SIDES * 6.2831853f, cp = (float)cw_cos(phi), sp = (float)cw_sin(phi);
      int i = vb->nv++;
      for (int c = 0; c < 3; c++) {
        vb->p[i * 3 + c] = s->a[c] + ax[c] * rings[r].d + u[c] * cp * rx + w[c] * sp * rz;
        float rad = u[c] * cp / (rx > 0.001f ? rx : 0.001f) + w[c] * sp / (rz > 0.001f ? rz : 0.001f);
        vb->n[i * 3 + c] = rad;
      }
      /* normal: the ellipse normal blended with the cap's axial direction */
      float na = rings[r].na;
      float nx = vb->n[i * 3], ny = vb->n[i * 3 + 1], nz = vb->n[i * 3 + 2];
      float nl = sqrtf(nx * nx + ny * ny + nz * nz);
      if (nl < 1e-6f) nl = 1;
      float rad_w = na == 0 ? 1.0f : sqrtf(fmaxf(0.0f, 1 - na * na));
      for (int c = 0; c < 3; c++) vb->n[i * 3 + c] = vb->n[i * 3 + c] / nl * rad_w + ax[c] * na;
      float l2 = v3len(&vb->n[i * 3]);
      if (l2 > 1e-6f) for (int c = 0; c < 3; c++) vb->n[i * 3 + c] /= l2;
      vb->uv[i * 2] = (float)k / SIDES;
      vb->uv[i * 2 + 1] = (rings[r].d / 0.5f);
      float tt = cw_clampf(t, 0, 1);
      for (int c = 0; c < 3; c++) vb->c[i * 3 + c] = srgb2lin(s->col[c] + (s->col2[c] - s->col[c]) * tt);
      vb->si[i * 4] = (float)s->pb; vb->si[i * 4 + 1] = (float)s->ob; vb->si[i * 4 + 2] = (float)s->cb; vb->si[i * 4 + 3] = 0;
      vb->sw[i * 4] = wp; vb->sw[i * 4 + 1] = 1 - wp - wc; vb->sw[i * 4 + 2] = wc; vb->sw[i * 4 + 3] = 0;
    }
  }
  for (int r = 0; r + 1 < nr; r++)
    for (int k = 0; k < SIDES; k++) {
      uint32_t a0 = (uint32_t)(base + r * (SIDES + 1) + k), a1 = a0 + 1, b0 = a0 + SIDES + 1, b1 = b0 + 1;
      vb->idx[vb->ni++] = a0; vb->idx[vb->ni++] = b0; vb->idx[vb->ni++] = a1;
      vb->idx[vb->ni++] = a1; vb->idx[vb->ni++] = b0; vb->idx[vb->ni++] = b1;
    }
}

static t3_geometry *vb_geometry(vbuf *vb) {
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, vb->p, vb->nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, vb->n, vb->nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, vb->uv, vb->nv, 2));
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, vb->c, vb->nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_SKIN_INDEX, t3_attribute_new(T3_FLOAT32, vb->si, vb->nv, 4));
  t3_geometry_set_attribute(g, T3_ATTR_SKIN_WEIGHT, t3_attribute_new(T3_FLOAT32, vb->sw, vb->nv, 4));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, vb->idx, vb->ni, 1));
  return g;
}

/* ── the model ───────────────────────────────────────────────────────── */

typedef struct tail { t3_object *piv[3]; float ang[3], vel[3]; int bone; } tail;

struct fighter_model {
  int def;
  const spec *sp;
  cw_node node;
  t3_object *root, *bones[JN];
  t3_skeleton *sk;
  float cur[JN][3], root_off[3], yaw, tilt;
  tail tails[2];
  int ntails;
  float last_x, vx;
  bool inited;
};

static t3_material *phys(t3_color c, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->color = c; m->roughness = rough; m->metalness = metal;
  return m;
}
static t3_mesh *part(t3_object *parent, t3_geometry *g, t3_material *m, float x, float y, float z) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.position = (t3_vec3){ x, y, z };
  me->base.cast_shadow = true;
  me->base.receive_shadow = true;
  t3_object_add(parent, me);
  t3_release(me);
  t3_release(g);
  t3_release(m);
  return me;
}
static t3_geometry *rbox(float w, float h, float d) { return t3_box_geometry_new(w, h, d, 2, 2, 2); }
static t3_color col(const float *c) { return lin(c[0], c[1], c[2]); }
static t3_color col_k(const float *c, float k) { return lin(c[0] * k, c[1] * k, c[2] * k); }
static t3_material *emis(const float *c, float k) {
  t3_material *m = phys(col_k(c, 0.3f), 0.4f, 0);
  m->emissive = col(c);
  m->emissive_intensity = k;
  return m;
}
/* an object at a joint's rest position (children carry rest offsets relative to it) */
static t3_object *at_bone(fighter_model *m, int j) { return m->bones[j]; }
static const float *rest_of(int j) { return REST[j]; }

static void build_geometry(fighter_model *m, vbuf *suit, vbuf *skin) {
  const spec *s = m->sp;
  float sc = s->scale, bk = s->bulk, sh = s->shoulder;
  float P[JN][3];
  for (int j = 0; j < JN; j++) {
    P[j][0] = REST[j][0] * sh;
    if (j == THI_L || j == THI_R || j == SHI_L || j == SHI_R || j == FOO_L || j == FOO_R) P[j][0] = REST[j][0] * (0.85f + 0.3f * bk);
    P[j][1] = REST[j][1] * sc; P[j][2] = REST[j][2];
  }
  (void)P;
  const float *su = s->suit, *ac = s->accent, *sk = s->skin, *tr = s->trim;
  float dark[3] = { su[0] * 0.55f, su[1] * 0.55f, su[2] * 0.55f };
  /* limb colours by kind: bolt is a gi with bare forearms and shins; grit is plated; vex a full suit; mara a jacket and tights */
  int bare_arm = s->kind == 0 || s->kind == 3, bare_leg = s->kind == 0;
  const float *armcol = bare_arm ? sk : su, *legcol = bare_leg ? sk : (s->kind == 3 ? tr : su);
  int armskin = bare_arm, legskin = bare_leg;
  float W = bk;
  seg segs[] = {
    /* pelvis, waist, chest, shoulders line */
    { { 0, 0.88f * sc, 0 }, { 0, 1.07f * sc, 0 }, 0.165f * W, 0.105f * W, 0.150f * W, 0.098f * W, 0.04f, HIPS, HIPS, SPINE, 0, { dark[0], dark[1], dark[2] }, { su[0], su[1], su[2] }, true, false },
    { { 0, 1.05f * sc, 0 }, { 0, 1.30f * sc, 0 }, 0.150f * W, 0.098f * W, 0.205f * W * sh, 0.125f * W, 0.03f, SPINE, SPINE, CHEST, 0, { su[0], su[1], su[2] }, { su[0], su[1], su[2] }, false, false },
    { { 0, 1.28f * sc, 0 }, { 0, 1.44f * sc, 0 }, 0.205f * W * sh, 0.125f * W, 0.11f * W, 0.075f, 0.0f, CHEST, CHEST, NECK, 0, { su[0], su[1], su[2] }, { ac[0] * 0.5f + su[0] * 0.5f, ac[1] * 0.5f + su[1] * 0.5f, ac[2] * 0.5f + su[2] * 0.5f }, false, true },
    { { 0, 1.44f * sc, 0 }, { 0, 1.57f * sc, 0 }, 0.055f, 0.055f, 0.05f, 0.05f, 0, NECK, NECK, HEAD, 1, { sk[0], sk[1], sk[2] }, { sk[0], sk[1], sk[2] }, false, false },
    { { 0, 1.56f * sc, 0 }, { 0, 1.70f * sc, 0 }, 0.088f, 0.100f, 0.080f, 0.092f, 0.15f, HEAD, HEAD, HEAD, 1, { sk[0], sk[1], sk[2] }, { sk[0] * 0.95f, sk[1] * 0.95f, sk[2] * 0.95f }, true, true },
  };
  for (size_t i = 0; i < sizeof segs / sizeof segs[0]; i++) add_tube(segs[i].skin ? skin : suit, &segs[i]);
  for (int side = 0; side < 2; side++) {
    float x = side ? -1 : 1;
    int ua = side ? UAR_R : UAR_L, fa = side ? FAR_R : FAR_L, ha = side ? HAND_R : HAND_L;
    int th = side ? THI_R : THI_L, sn = side ? SHI_R : SHI_L, ft = side ? FOO_R : FOO_L;
    float ax = 0.20f * sh * x, lx = 0.10f * (0.85f + 0.3f * bk) * x;
    float arm = 0.058f * (0.8f + 0.3f * bk), arm2 = 0.05f * (0.8f + 0.3f * bk);
    seg arms[] = {
      { { ax, 1.43f * sc, 0 }, { ax * 1.05f, 1.13f * sc, 0 }, arm * 1.2f, arm * 1.2f, arm2, arm2, 0.08f, ua, ua, fa, 0, { su[0], su[1], su[2] }, { su[0], su[1], su[2] }, true, false },
      { { ax * 1.05f, 1.13f * sc, 0 }, { ax * 1.05f, 0.86f * sc, 0 }, arm2, arm2, arm2 * 0.78f, arm2 * 0.78f, 0.06f, fa, fa, ha, armskin, { armcol[0], armcol[1], armcol[2] }, { armcol[0], armcol[1], armcol[2] }, false, false },
      { { ax * 1.05f, 0.86f * sc, 0 }, { ax * 1.05f, 0.77f * sc, 0 }, 0.045f, 0.040f, 0.036f, 0.034f, 0.1f, ha, ha, ha, 1, { sk[0], sk[1], sk[2] }, { sk[0], sk[1], sk[2] }, false, true },
    };
    for (size_t i = 0; i < 3; i++) add_tube(arms[i].skin ? skin : suit, &arms[i]);
    float th0 = 0.088f * (0.8f + 0.35f * bk), th1 = 0.062f * (0.8f + 0.3f * bk);
    seg legs[] = {
      { { lx, 0.95f * sc, 0 }, { lx, 0.50f * sc, 0 }, th0, th0 * 0.95f, th1, th1, 0.05f, th, th, sn, 0, { su[0], su[1], su[2] }, { su[0] * 0.9f, su[1] * 0.9f, su[2] * 0.9f }, false, false },
      { { lx, 0.50f * sc, 0 }, { lx, 0.09f * sc, 0 }, th1, th1, 0.042f, 0.045f, 0.1f, sn, sn, ft, legskin, { legcol[0], legcol[1], legcol[2] }, { legcol[0] * 0.8f, legcol[1] * 0.8f, legcol[2] * 0.8f }, false, false },
      { { lx, 0.07f * sc, -0.04f }, { lx, 0.04f * sc, 0.17f }, 0.050f, 0.040f, 0.045f, 0.030f, 0.0f, ft, ft, ft, 0, { tr[0], tr[1], tr[2] }, { tr[0], tr[1], tr[2] }, true, true },
    };
    for (size_t i = 0; i < 3; i++) add_tube(legs[i].skin ? skin : suit, &legs[i]);
  }
  (void)ac; (void)dark;
}

static void attach_hardware(fighter_model *m) {
  const spec *s = m->sp;
  float sc = s->scale, sh = s->shoulder, bk = s->bulk;
  t3_object *head = m->bones[HEAD], *chest = m->bones[CHEST], *hips = m->bones[HIPS];
  /* rest offsets are relative to the bone object, so subtract the joint's rest position */
  #define OFF(j, x, y, z) (x), ((y) * sc - REST[j][1] * sc), (z)
  t3_material *accent = phys(col(s->accent), 0.35f, 0.0f);
  t3_material *trim = phys(col(s->trim), 0.5f, 0.1f);
  t3_color mc = col(s->suit);
  (void)mc;
  /* belts and hips */
  part(hips, rbox(0.34f * bk, 0.07f, 0.23f * bk), t3_retain(accent), OFF(HIPS, 0, 1.0f, 0));
  switch (s->kind) {
  case 0: { /* bolt: headband with tails, spiky hair, glove wraps */
    t3_material *hair = phys(lin(0.1f, 0.07f, 0.06f), 0.55f, 0);
    for (int i = 0; i < 5; i++) {
      float a = -0.5f + 0.25f * (float)i;
      t3_mesh *h = part(head, t3_cone_geometry_new(0.05f, 0.14f, 8, 1, false), t3_retain(hair), OFF(HEAD, a * 0.12f, 1.78f, -0.01f));
      h->base.quaternion = (t3_quat){ 0, 0, a * 0.35f, 1 };
    }
    part(head, rbox(0.205f, 0.035f, 0.205f), t3_retain(accent), OFF(HEAD, 0, 1.66f, 0));
    for (int side = 0; side < 2; side++) {
      t3_object *wrap = m->bones[side ? HAND_R : HAND_L];
      part(wrap, rbox(0.085f, 0.07f, 0.085f), t3_retain(accent), OFF(HAND_L, 0, 0.83f, 0.0f));
    }
    t3_release(hair);
    break;
  }
  case 1: { /* grit: pauldrons, bald head, big gauntlets and boots, belly plate */
    t3_material *plate = phys(col(s->trim), 0.35f, 0.85f);
    for (int side = 0; side < 2; side++) {
      float x = side ? -1 : 1;
      t3_mesh *p = part(chest, t3_sphere_geometry_new(0.115f * bk, 14, 10), t3_retain(plate), OFF(CHEST, 0.27f * sh * x, 1.46f, 0));
      p->base.scale = (t3_vec3){ 1.0f, 0.75f, 1.0f };
      part(m->bones[side ? FAR_R : FAR_L], rbox(0.12f, 0.16f, 0.12f), t3_retain(plate), OFF(FAR_L, 0.215f * sh * x, 1.0f, 0));
      part(m->bones[side ? FOO_R : FOO_L], rbox(0.1f, 0.07f, 0.24f), t3_retain(plate), OFF(FOO_L, 0.1f * x, 0.06f, 0.06f));
    }
    part(chest, rbox(0.3f * bk, 0.2f, 0.06f), t3_retain(plate), OFF(CHEST, 0, 1.2f, 0.11f));
    t3_release(plate);
    break;
  }
  case 2: { /* vex: a helmet with a glowing visor, neon stripes, shin guards */
    t3_material *helm = phys(lin(0.07f, 0.08f, 0.1f), 0.25f, 0.6f);
    t3_mesh *hm = part(head, t3_sphere_geometry_new(0.112f, 16, 12), helm, OFF(HEAD, 0, 1.655f, -0.005f));
    hm->base.scale = (t3_vec3){ 1, 1.05f, 1.1f };
    t3_material *vis = emis(s->accent, 3.0f);
    part(head, rbox(0.17f, 0.045f, 0.07f), vis, OFF(HEAD, 0, 1.66f, 0.085f));
    t3_material *strip = emis(s->accent, 1.8f);
    for (int side = 0; side < 2; side++) {
      float x = side ? -1 : 1;
      part(chest, rbox(0.015f, 0.34f, 0.01f), t3_retain(strip), OFF(CHEST, 0.09f * x, 1.17f, 0.12f));
      part(m->bones[side ? SHI_R : SHI_L], rbox(0.015f, 0.3f, 0.01f), t3_retain(strip), OFF(SHI_L, 0.1f * x, 0.30f, 0.05f));
      part(m->bones[side ? FAR_R : FAR_L], rbox(0.1f, 0.12f, 0.1f), t3_retain(trim), OFF(FAR_L, 0.22f * sh * x, 0.97f, 0));
    }
    t3_release(strip);
    break;
  }
  default: { /* mara: goggles, a ponytail and a long scarf */
    t3_material *hair = phys(lin(0.35f, 0.18f, 0.08f), 0.5f, 0);
    t3_mesh *cap = part(head, t3_sphere_geometry_new(0.108f, 14, 10), t3_retain(hair), OFF(HEAD, 0, 1.675f, -0.012f));
    cap->base.scale = (t3_vec3){ 1.0f, 0.8f, 1.05f };
    t3_material *gl = phys(lin(0.1f, 0.12f, 0.15f), 0.1f, 0.8f);
    part(head, rbox(0.19f, 0.05f, 0.04f), gl, OFF(HEAD, 0, 1.69f, 0.09f));
    part(head, rbox(0.215f, 0.03f, 0.2f), t3_retain(accent), OFF(HEAD, 0, 1.73f, -0.005f));
    /* ponytail: three pivots hung from the head, swung by motion */
    t3_object *prev = head;
    float py = 1.70f * sc - REST[HEAD][1] * sc, pz = -0.1f;
    for (int i = 0; i < 3; i++) {
      t3_object *piv = t3_group_new();
      piv->position = (t3_vec3){ 0, i == 0 ? py : -0.07f, i == 0 ? pz : -0.02f };
      t3_object_add(prev, piv);
      t3_release(piv);
      part(piv, t3_cylinder_geometry_new(0.04f - 0.008f * (float)i, 0.03f - 0.008f * (float)i, 0.1f, 8, 1, false), t3_retain(hair), 0, -0.05f, 0);
      m->tails[0].piv[i] = piv;
      prev = piv;
    }
    m->tails[0].bone = HEAD;
    m->ntails = 1;
    /* scarf */
    t3_object *pv = chest;
    for (int i = 0; i < 3; i++) {
      t3_object *piv = t3_group_new();
      piv->position = (t3_vec3){ 0.0f, i == 0 ? (1.38f * sc - REST[CHEST][1] * sc) : -0.1f, i == 0 ? -0.12f : 0.0f };
      t3_object_add(pv, piv);
      t3_release(piv);
      part(piv, rbox(0.09f, 0.11f, 0.015f), t3_retain(accent), 0, -0.05f, 0);
      m->tails[1].piv[i] = piv;
      pv = piv;
    }
    m->tails[1].bone = CHEST;
    m->ntails = 2;
    t3_release(hair);
    break;
  }
  }
  (void)at_bone; (void)rest_of; (void)trim;
  t3_release(accent);
}

fighter_model *model_new(int def, cw_node parent) {
  fighter_model *m = calloc(1, sizeof *m);
  m->def = def;
  m->sp = &SPECS[def % 4];
  m->root = t3_group_new();
  /* bones: local = rest - parent rest */
  float sc = m->sp->scale;
  for (int j = 0; j < JN; j++) {
    m->bones[j] = t3_bone_new();
    float px = REST[j][0] * (j >= THI_L ? 1 : m->sp->shoulder), py = REST[j][1] * sc;
    if (j >= THI_L) px = REST[j][0] * (0.85f + 0.3f * m->sp->bulk);
    float qx = 0, qy = 0;
    if (PARENT[j] >= 0) {
      int p = PARENT[j];
      qx = REST[p][0] * (p >= THI_L ? 1 : m->sp->shoulder);
      if (p >= THI_L) qx = REST[p][0] * (0.85f + 0.3f * m->sp->bulk);
      qy = REST[p][1] * sc;
    }
    m->bones[j]->position = (t3_vec3){ px - qx, py - qy, 0 };
    if (PARENT[j] < 0) t3_object_add(m->root, m->bones[j]);
    else t3_object_add(m->bones[PARENT[j]], m->bones[j]);
  }
  /* the arm joints sit at x = 0.205 * shoulder; the legs share the hip width: rebuild positions consistently */
  vbuf suit = { 0 }, skin = { 0 };
  build_geometry(m, &suit, &skin);
  t3_material *msuit = phys(lin(1, 1, 1), 0.62f, 0.0f);
  msuit->vertex_colors = true;
  msuit->sheen = 0.5f;
  tex_apply(msuit, TEX_FABRIC, 1, 1, 0.25f, 0.5f);
  t3_material *mskin = phys(lin(1, 1, 1), 0.55f, 0.0f);
  mskin->vertex_colors = true;
  mskin->sheen = 0.3f;
  mskin->sheen_color = lin(1.0f, 0.8f, 0.7f);
  t3_object_update_matrix_world(m->root, true);
  t3_skeleton *sk = t3_skeleton_new(m->bones, JN, NULL);
  t3_geometry *gs = vb_geometry(&suit), *gk = vb_geometry(&skin);
  t3_skinned_mesh *sm1 = t3_skinned_mesh_new(gs, msuit), *sm2 = t3_skinned_mesh_new(gk, mskin);
  t3_skinned_mesh *sms[2] = { sm1, sm2 };
  for (int i = 0; i < 2; i++) {
    sms[i]->mesh.base.cast_shadow = true;
    sms[i]->mesh.base.receive_shadow = true;
    sms[i]->mesh.base.frustum_culled = false;
    t3_object_add(m->root, sms[i]);
    t3_skinned_mesh_bind(sms[i], sk, NULL);
    t3_release(sms[i]);
  }
  t3_release(gs); t3_release(gk); t3_release(msuit); t3_release(mskin);
  free(suit.p); free(suit.n); free(suit.uv); free(suit.c); free(suit.si); free(suit.sw); free(suit.idx);
  free(skin.p); free(skin.n); free(skin.uv); free(skin.c); free(skin.si); free(skin.sw); free(skin.idx);
  m->sk = sk;
  attach_hardware(m);
  m->node = cw_node_new("Node3D", def % 2 ? "Fighter1" : "Fighter0");
  cw_node_add_child(parent, m->node);
  cw_node3d_set_object(m->node, m->root);
  cw_node_set(m->node, "interpolate", cw_val_bool(false));
  return m;
}

void model_free(fighter_model *m) {
  if (!m) return;
  if (cw_node_valid(m->node)) cw_node_free(m->node);
  free(m);
}
void model_set_visible(fighter_model *m, bool v) { m->root->visible = v; }
t3_vec3 model_joint(fighter_model *m, int j) {
  static const int MAP[] = { [J_HEAD] = HEAD, [J_HAND_F] = HAND_R, [J_HAND_B] = HAND_L, [J_FOOT_F] = FOO_R, [J_CHEST] = CHEST, [J_HIPS] = HIPS };
  const t3_mat4 *w = &m->bones[MAP[j]]->matrix_world;
  return (t3_vec3){ w->e[12], w->e[13], w->e[14] };
}

/* ── poses ───────────────────────────────────────────────────────────── */

typedef struct pose { float r[JN][3]; float off[3]; float yaw, tilt; } pose;

/* arm in lead / rear terms: forward raise, out to the side, elbow bend */
static void arm(pose *p, int lead, int near_is_right, float fwd, float out, float elbow, float twist) {
  int right = lead ? near_is_right : !near_is_right;
  int ua = right ? UAR_R : UAR_L, fa = right ? FAR_R : FAR_L;
  float s = right ? -1.0f : 1.0f;
  p->r[ua][0] = -fwd; p->r[ua][2] = out * s; p->r[ua][1] = twist * s;
  p->r[fa][0] = -elbow;
}
static void leg(pose *p, int lead, int near_is_right, float fwd, float out, float knee) {
  int right = lead ? near_is_right : !near_is_right;
  int th = right ? THI_R : THI_L, sn = right ? SHI_R : SHI_L, ft = right ? FOO_R : FOO_L;
  float s = right ? -1.0f : 1.0f;
  p->r[th][0] = -fwd; p->r[th][2] = out * s;
  p->r[sn][0] = knee;
  p->r[ft][0] = -0.3f * knee - 0.3f * (-fwd);
}

static void stance(pose *p, const spec *s, int nr, float t) {
  float bob = (float)cw_sin(t * 0.11f) * 0.012f * s->bounce;
  p->off[1] = -0.07f + bob;
  leg(p, 1, nr, 0.35f, s->stance, 0.55f);
  leg(p, 0, nr, -0.2f, s->stance, 0.45f);
  p->r[SPINE][0] = 0.08f; p->r[SPINE][1] = 0.22f;
  p->r[CHEST][0] = 0.06f; p->r[CHEST][1] = 0.1f;
  p->r[HEAD][0] = -0.1f; p->r[HEAD][1] = -0.28f;
  float g = s->guard;
  arm(p, 1, nr, 1.15f * g, 0.22f, 1.5f, 0.0f);
  arm(p, 0, nr, 0.95f * g, 0.30f, 1.7f, 0.0f);
  p->yaw = 0;
}

static void blend(pose *o, const pose *a, const pose *b, float k) {
  for (int j = 0; j < JN; j++) for (int c = 0; c < 3; c++) o->r[j][c] = a->r[j][c] + (b->r[j][c] - a->r[j][c]) * k;
  for (int c = 0; c < 3; c++) o->off[c] = a->off[c] + (b->off[c] - a->off[c]) * k;
  o->yaw = a->yaw + (b->yaw - a->yaw) * k;
  o->tilt = a->tilt + (b->tilt - a->tilt) * k;
}

/* the extended pose of each move slot, in the same terms as the stance */
static void strike(pose *p, const spec *s, int nr, int slot, int strength) {
  switch (slot) {
  case M_5L: arm(p, 1, nr, 1.5f, 0.1f, 0.15f, 0); p->r[SPINE][1] = 0.0f; p->r[CHEST][1] = -0.2f; p->off[2] = 0.05f; break;
  case M_5M: arm(p, 0, nr, 1.5f, 0.1f, 0.1f, 0); p->r[SPINE][1] = -0.55f; p->r[CHEST][1] = -0.35f; p->off[2] = 0.12f;
             leg(p, 1, nr, 0.5f, s->stance, 0.8f); break;
  case M_5H: arm(p, 0, nr, 1.2f, 0.9f, 0.3f, 0.3f); arm(p, 1, nr, 0.6f, 0.3f, 1.2f, 0); p->r[SPINE][1] = -0.9f; p->r[SPINE][0] = 0.3f; p->r[CHEST][1] = -0.4f;
             p->off[2] = 0.22f; p->off[1] -= 0.05f; leg(p, 1, nr, 0.75f, s->stance + 0.1f, 1.0f); break;
  case M_2L: arm(p, 1, nr, 1.4f, 0.1f, 0.2f, 0); p->off[1] = -0.35f; leg(p, 1, nr, 1.1f, 0.1f, 1.1f); leg(p, 0, nr, 0.7f, 0.2f, 1.9f); p->r[SPINE][0] = 0.25f; break;
  case M_2M: p->off[1] = -0.38f; leg(p, 1, nr, 1.35f, 0.1f, 0.15f); leg(p, 0, nr, 0.8f, 0.25f, 1.9f); p->r[SPINE][0] = 0.15f; p->r[SPINE][2] = 0.1f;
             arm(p, 1, nr, 0.5f, 0.5f, 1.0f, 0); arm(p, 0, nr, 0.5f, 0.5f, 1.0f, 0); break;
  case M_2H: p->off[1] = -0.5f; p->off[2] = 0.25f; leg(p, 1, nr, 1.45f, 0.05f, 0.05f); leg(p, 0, nr, 0.5f, 0.5f, 2.2f); p->r[SPINE][0] = 0.5f; p->r[CHEST][0] = 0.4f;
             arm(p, 1, nr, 0.3f, 0.7f, 0.6f, 0); arm(p, 0, nr, -0.5f, 0.9f, 0.3f, 0); break;
  case M_JL: arm(p, 1, nr, 1.55f, 0.1f, 0.1f, 0); p->r[SPINE][0] = 0.3f; leg(p, 1, nr, 1.1f, 0.1f, 1.5f); leg(p, 0, nr, 0.5f, 0.1f, 1.8f); break;
  case M_JM: leg(p, 1, nr, 1.4f, 0.05f, 0.3f); leg(p, 0, nr, 0.4f, 0.1f, 2.0f); p->r[SPINE][0] = 0.4f; arm(p, 1, nr, 0.8f, 0.4f, 1.2f, 0); arm(p, 0, nr, 0.8f, 0.4f, 1.2f, 0); break;
  case M_JH: leg(p, 1, nr, 1.2f, 0.15f, 0.9f); leg(p, 0, nr, 0.3f, 0.2f, 1.5f); p->r[SPINE][0] = 0.6f; p->r[CHEST][0] = 0.4f;
             arm(p, 1, nr, 2.4f, 0.1f, 0.1f, 0); arm(p, 0, nr, 2.4f, 0.1f, 0.1f, 0); break;
  case M_THROW: case M_SP1: case M_SUPER:
    if (slot == M_THROW || (s->kind == 1 && slot == M_SP1) || (s->kind == 1 && slot == M_SUPER)) {
      arm(p, 1, nr, 1.55f, 0.35f, 0.1f, 0.5f); arm(p, 0, nr, 1.55f, 0.35f, 0.1f, 0.5f); p->r[SPINE][0] = 0.45f; p->off[1] = -0.2f;
    } else if (s->kind == 2 && slot == M_SP1) { /* slide */
      p->off[1] = -0.55f; p->off[2] = 0.3f; leg(p, 1, nr, 1.5f, 0.05f, 0.05f); leg(p, 0, nr, 0.3f, 0.4f, 2.0f); p->r[SPINE][0] = -0.5f; p->r[CHEST][0] = -0.3f;
    } else { /* a thrust of both hands */
      arm(p, 1, nr, 1.5f, 0.05f, 0.05f, 0); arm(p, 0, nr, 1.5f, 0.05f, 0.05f, 0); p->r[SPINE][0] = 0.25f; p->r[SPINE][1] = -0.3f; p->off[2] = 0.1f;
      leg(p, 1, nr, 0.7f, s->stance + 0.05f, 0.9f);
    }
    break;
  case M_SP2: arm(p, 0, nr, 2.9f, 0.15f, 0.1f, 0); arm(p, 1, nr, 1.0f, 0.4f, 1.4f, 0); p->r[SPINE][0] = -0.25f; p->r[CHEST][0] = -0.15f; p->off[1] = 0.15f;
              leg(p, 1, nr, 0.9f, 0.1f, 1.7f); leg(p, 0, nr, 0.0f, 0.1f, 0.2f); break;
  case M_SP3: leg(p, 1, nr, 1.4f, 0.1f, 0.2f); leg(p, 0, nr, 0.2f, 0.1f, 1.3f); p->r[SPINE][0] = 0.1f; p->r[SPINE][2] = 0.35f; p->off[1] = 0.05f;
              arm(p, 1, nr, 0.4f, 1.3f, 0.3f, 0); arm(p, 0, nr, 0.4f, 1.3f, 0.3f, 0); break;
  }
  (void)strength;
}

static float envelope(const ft_move *m, int t) {
  int su = m->su, ac = m->ac, rc = m->rc;
  if (t <= su) {
    float p = (float)t / (float)su;
    float pull = p < 0.45f ? -0.22f * (float)cw_sin(p / 0.45f * 3.14159265f) : 0;
    return pull + (p < 0.45f ? 0 : sm(0.45f, 1.0f, p));
  }
  if (t <= su + ac) return 1;
  return 1 - sm(0, (float)rc, (float)(t - su - ac));
}

static void pose_for(fighter_model *m, const ft_game *g, int who, pose *out) {
  const ft_fighter *f = &g->f[who];
  const spec *s = m->sp;
  const ft_def *d = &g->roster->d[f->def];
  int nr = f->facing > 0; /* the near (camera side) arm is the model's right when facing +x */
  pose base;
  memset(&base, 0, sizeof base);
  float t = (float)(g->tick + (unsigned)who * 17);
  stance(&base, s, nr, t);
  *out = base;
  switch (f->state) {
  case S_INTRO: {
    float k = (float)cw_sin((float)f->t * 0.1f);
    out->r[HEAD][1] = 0.2f * k; arm(out, 1, nr, 2.5f, 0.2f, 0.6f, 0); arm(out, 0, nr, 1.2f, 0.7f, 1.4f, 0);
    break;
  }
  case S_WALKF: case S_WALKB: {
    float ph = (float)f->t * (f->state == S_WALKF ? 0.28f : 0.22f);
    float sw = (float)cw_sin(ph), cs = (float)cw_cos(ph);
    out->off[1] = base.off[1] + 0.025f * (float)cw_sin(ph * 2);
    leg(out, 1, nr, 0.35f + 0.45f * sw, s->stance, 0.55f + 0.35f * (cs > 0 ? cs : 0));
    leg(out, 0, nr, -0.2f - 0.45f * sw, s->stance, 0.45f + 0.35f * (cs < 0 ? -cs : 0));
    break;
  }
  case S_CROUCH: case S_PREJUMP: case S_LAND: {
    out->off[1] = -0.38f; leg(out, 1, nr, 1.15f, s->stance, 1.7f); leg(out, 0, nr, 0.95f, s->stance + 0.05f, 1.6f);
    out->r[SPINE][0] = 0.3f;
    if (f->state == S_PREJUMP) { out->off[1] = -0.4f; }
    break;
  }
  case S_AIR: {
    out->off[1] = 0; leg(out, 1, nr, 0.9f, 0.1f, 1.4f); leg(out, 0, nr, 0.5f, 0.12f, 1.1f);
    arm(out, 1, nr, 1.2f, 0.4f, 1.4f, 0); arm(out, 0, nr, 1.0f, 0.5f, 1.4f, 0);
    break;
  }
  case S_DASHF: out->r[SPINE][0] = 0.5f; out->off[2] = 0.1f; leg(out, 1, nr, 0.9f, 0.1f, 1.2f); leg(out, 0, nr, -0.6f, 0.1f, 0.9f); break;
  case S_DASHB: out->r[SPINE][0] = -0.2f; leg(out, 1, nr, 0.3f, 0.1f, 1.4f); leg(out, 0, nr, 0.6f, 0.1f, 1.2f); out->off[1] = -0.1f; break;
  case S_BLOCK: {
    float k = (float)cw_sin((float)f->t * 0.8f) * 0.04f;
    arm(out, 1, nr, 1.9f, 0.1f, 2.0f + k, 0); arm(out, 0, nr, 1.8f, 0.15f, 2.0f, 0);
    out->r[SPINE][0] = 0.25f; out->r[HEAD][0] = 0.2f;
    if (f->crouch_block) { out->off[1] = -0.38f; leg(out, 1, nr, 1.15f, s->stance, 1.7f); leg(out, 0, nr, 0.95f, s->stance, 1.6f); }
    break;
  }
  case S_HIT: {
    float k = cw_clampf((float)f->stun / 12.0f, 0, 1);
    out->r[SPINE][0] = -0.35f * k; out->r[CHEST][0] = -0.25f * k; out->r[HEAD][0] = -0.5f * k; out->off[2] = -0.06f * k;
    arm(out, 1, nr, 0.5f, 0.6f, 0.8f, 0); arm(out, 0, nr, 0.4f, 0.7f, 0.7f, 0);
    break;
  }
  case S_AIRHIT: case S_THROWN: {
    float k = (float)f->t * 0.12f;
    out->tilt = -0.9f - 0.3f * (float)cw_sin(k); out->off[1] = 0.0f;
    arm(out, 1, nr, 0.6f, 0.9f, 0.4f, 0); arm(out, 0, nr, 0.3f, 1.0f, 0.4f, 0);
    leg(out, 1, nr, 0.5f, 0.2f, 0.7f); leg(out, 0, nr, 0.0f, 0.3f, 0.4f);
    break;
  }
  case S_KD: case S_KO: {
    out->tilt = -1.57f; out->off[1] = -0.62f + 0.62f * 0.0f; out->off[2] = -0.55f;
    arm(out, 1, nr, 0.4f, 0.4f, 0.3f, 0); arm(out, 0, nr, 0.2f, 0.6f, 0.3f, 0);
    leg(out, 1, nr, 0.1f, 0.2f, 0.2f); leg(out, 0, nr, 0.0f, 0.3f, 0.1f);
    out->r[HEAD][0] = 0.3f;
    break;
  }
  case S_GETUP: {
    float k = cw_clampf((float)f->t / 20.0f, 0, 1);
    pose down; memset(&down, 0, sizeof down); down.tilt = -1.57f; down.off[1] = -0.62f; down.off[2] = -0.55f;
    blend(out, &down, &base, sm(0.0f, 1.0f, k));
    break;
  }
  case S_WIN: {
    float k = (float)cw_sin((float)f->t * 0.15f);
    arm(out, 1, nr, 3.0f, 0.2f + 0.1f * k, 0.2f, 0); arm(out, 0, nr, 2.7f, 0.5f, 0.4f, 0);
    out->off[1] = -0.02f + 0.03f * k; out->r[HEAD][0] = -0.2f;
    leg(out, 1, nr, 0.1f, 0.14f, 0.1f); leg(out, 0, nr, 0.0f, 0.14f, 0.1f); out->r[SPINE][0] = -0.15f;
    break;
  }
  case S_ATTACK: {
    const ft_move *mv = &d->mv[f->mv];
    float e = envelope(mv, f->t);
    pose ex = base;
    strike(&ex, s, nr, f->mv, f->strength);
    blend(out, &base, &ex, e);
    if (f->mv == M_SP3 && s->kind != 2) out->yaw = e * 12.5f * cw_clampf((float)(f->t - mv->su + 1) / (float)(mv->ac + 1), 0, 1);
    else if (f->mv == M_SP3) { /* the overhead leap */
      out->off[1] += 0.3f * (float)cw_sin(cw_clampf((float)f->t / 24.0f, 0, 1) * 3.14159265f);
    }
    if (mv->flags & FM_SUPER) out->r[HEAD][0] -= 0.1f;
    break;
  }
  default: break;
  }
}

static void quat_euler(const float r[3], t3_quat *q) {
  float sx, cx, sy, cy, sz, cz;
  cw_sincos(r[0] * 0.5f, &sx, &cx); cw_sincos(r[1] * 0.5f, &sy, &cy); cw_sincos(r[2] * 0.5f, &sz, &cz);
  t3_quat qx = { sx, 0, 0, cx }, qy = { 0, sy, 0, cy }, qz = { 0, 0, sz, cz };
  *q = t3_quat_multiply(t3_quat_multiply(qy, qx), qz);
}

void model_pose(fighter_model *m, const ft_game *g, int who, float dt) {
  const ft_fighter *f = &g->f[who];
  pose p;
  pose_for(m, g, who, &p);
  /* joints ease toward the target (about 3 ticks), so state changes read as motion, not snaps */
  float k = m->inited ? 1.0f - cw_exp(-dt * 28.0f) : 1.0f;
  if (f->state == S_ATTACK) k = 1.0f - cw_exp(-dt * 60.0f);
  for (int j = 0; j < JN; j++) for (int c = 0; c < 3; c++) m->cur[j][c] += (p.r[j][c] - m->cur[j][c]) * k;
  for (int c = 0; c < 3; c++) m->root_off[c] += (p.off[c] - m->root_off[c]) * k;
  m->yaw = p.yaw;
  m->tilt += (p.tilt - m->tilt) * k;
  m->inited = true;
  for (int j = 0; j < JN; j++) {
    t3_quat q;
    quat_euler(m->cur[j], &q);
    m->bones[j]->quaternion = q;
  }
  /* hips carry the body's offset and lean (the knockdown pitch) */
  float sc = m->sp->scale;
  m->bones[HIPS]->position = (t3_vec3){ m->root_off[0], REST[HIPS][1] * sc + m->root_off[1] * sc, m->root_off[2] };
  t3_quat hq, tq;
  quat_euler(m->cur[HIPS], &hq);
  float tr[3] = { m->tilt, 0, 0 };
  quat_euler(tr, &tq);
  m->bones[HIPS]->quaternion = t3_quat_multiply(tq, hq);
  /* the root: world position, facing, and a spin for the kick */
  float face = f->facing > 0 ? 1.5707963f : -1.5707963f;
  float yaw[3] = { 0, face + m->yaw, 0 };
  t3_quat yq;
  quat_euler(yaw, &yq);
  cw_node3d_set_quaternion(m->node, yq);
  cw_node3d_set_position(m->node, (t3_vec3){ (float)f->x / 1000.0f, (float)f->y / 1000.0f, 0 });
  /* secondary motion: tails lag behind the body's velocity */
  float vx = ((float)f->x - m->last_x) / 1000.0f / dt;
  m->last_x = (float)f->x;
  m->vx += (vx - m->vx) * 0.3f;
  for (int i = 0; i < m->ntails; i++) {
    tail *tl = &m->tails[i];
    float local_vx = m->vx * (f->facing > 0 ? 1.0f : -1.0f); /* forward speed in the model's frame */
    float wind = -local_vx * 0.12f + (f->state == S_AIR ? -0.4f : 0.0f);
    for (int s = 0; s < 3; s++) {
      float target = (s == 0 ? 0.5f : 0.0f) + wind * (1.0f + (float)s * 0.3f);
      tl->vel[s] += ((target - tl->ang[s]) * 160.0f - tl->vel[s] * 9.0f) * dt;
      tl->ang[s] += tl->vel[s] * dt;
      float r[3] = { tl->ang[s], 0, 0 };
      t3_quat q;
      quat_euler(r, &q);
      tl->piv[s]->quaternion = q;
    }
  }
  (void)model_joint;
}
