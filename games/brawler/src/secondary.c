/* brawler: secondary motion and character shading.
 *
 * Spring bones: hair, coat tails and bellies hang on bones no clip keys (HairA, HairB, CoatL, CoatR, Belly). Each fixed
 * tick, after the animation wrote the pose, a bone's tip is a point mass in world space pulled toward where the animated
 * parent would carry it (stiffness, damping, gravity), kept at the bone's length and inside a cone around the animated
 * direction; the bone is then turned (or for a belly, moved) to follow it. Running, turning, landing and hits make it swing.
 * Written generically (bones by name, any model under a Node3D) so the engine can take it as a skeleton modifier later.
 * Only + - * / and sqrt: the same on every host.
 *
 * Shading: the characters' glb splits its meshes by material class (BodySkin, BodyCloth, BodyLeather, BodyHair). Skin,
 * cloth and hair become physical materials with sheen (a soft scattering rim on skin, the fuzz of fabric, the sheen of
 * hair) and a lower specular so skin stops reading as plastic; leather gets a thin clear coat. */
#include <math.h>
#include <string.h>

#include "cartwheel/scene.h"
#include "game.h"
#include "three.h"

static t3_vec3 v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 vadd(t3_vec3 a, t3_vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 vsub(t3_vec3 a, t3_vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static t3_vec3 vmul(t3_vec3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
static float vdot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static t3_vec3 vcross(t3_vec3 a, t3_vec3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float vlen(t3_vec3 a) { return sqrtf(vdot(a, a)); }
static t3_vec3 vnorm(t3_vec3 a) { float l = vlen(a); return l > 1e-8f ? vmul(a, 1.0f / l) : v3(0, -1, 0); }
static t3_quat qmul(t3_quat a, t3_quat b) {
  return (t3_quat){ a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
static t3_quat qconj(t3_quat q) { return (t3_quat){ -q.x, -q.y, -q.z, q.w }; }
static t3_vec3 qrot(t3_quat q, t3_vec3 v) {
  t3_vec3 u = v3(q.x, q.y, q.z), t = vmul(vcross(u, v), 2);
  return vadd(vadd(v, vmul(t, q.w)), vcross(u, t));
}
/* the shortest rotation taking unit a onto unit b */
static t3_quat qfromto(t3_vec3 a, t3_vec3 b) {
  float d = vdot(a, b);
  if (d < -0.9999f) return (t3_quat){ 1, 0, 0, 0 };
  t3_vec3 c = vcross(a, b);
  t3_quat q = { c.x, c.y, c.z, 1 + d };
  float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return (t3_quat){ q.x / l, q.y / l, q.z / l, q.w / l };
}

/* a rigid transform with uniform scale */
typedef struct xf { t3_vec3 p; t3_quat q; float s; } xf;
static xf xmul(xf a, xf b) { return (xf){ vadd(a.p, vmul(qrot(a.q, b.p), a.s)), qmul(a.q, b.q), a.s * b.s }; }
static t3_vec3 xpoint(xf a, t3_vec3 p) { return vadd(a.p, vmul(qrot(a.q, p), a.s)); }
static xf local_of(const t3_object *o) { return (xf){ o->position, o->quaternion, o->scale.x }; }
/* the object's transform in the space of `root` (its ancestor) */
static xf model_xf(const t3_object *o, const t3_object *root) {
  xf r = { { 0, 0, 0 }, { 0, 0, 0, 1 }, 1 };
  const t3_object *chain[24];
  int n = 0;
  for (const t3_object *a = o; a && a != root && n < 24; a = a->parent) chain[n++] = a;
  for (int i = n - 1; i >= 0; i--) r = xmul(r, local_of(chain[i]));
  return r;
}

typedef struct spring_def { const char *bone; float tip[3]; float stiff, damp, grav, cone; bool move; } spring_def;
static const spring_def SDEF[SB_MAX] = {
  { "HairA", { 0, -0.15f, -0.03f }, 70, 7, 4.0f, 0.75f, false },
  { "HairB", { 0, -0.15f, 0 }, 55, 6, 5.0f, 0.9f, false },
  { "CoatL", { 0, -0.36f, -0.02f }, 110, 9, 3.0f, 0.6f, false },
  { "CoatR", { 0, -0.36f, -0.02f }, 110, 9, 3.0f, 0.6f, false },
  { "Belly", { 0, 0, 0.12f }, 260, 9, 0, 0, true },
};

void sb_bind(spring_rig *s, t3_object *scene) {
  memset(s, 0, sizeof *s);
  s->scene = scene;
  for (int i = 0; i < SB_MAX; i++) {
    t3_object *b = t3_object_get_by_name(scene, SDEF[i].bone);
    if (!b) continue;
    s->b[i].bone = b;
    s->b[i].rest = b->position;
    /* the tip: toward the child bone when there is one, else the default length */
    t3_vec3 tip = v3(SDEF[i].tip[0], SDEF[i].tip[1], SDEF[i].tip[2]);
    if (b->child_count > 0 && b->children[0]->type == T3_BONE) tip = b->children[0]->position;
    s->b[i].tip = tip;
  }
}

void sb_reset(spring_rig *s) { for (int i = 0; i < SB_MAX; i++) s->b[i].live = false; }

void sb_jolt(spring_rig *s, float x, float y, float z) {
  for (int i = 0; i < SB_MAX; i++) if (s->b[i].bone) s->b[i].V = vadd(s->b[i].V, v3(x, y, z));
}

void sb_step(spring_rig *s, t3_vec3 pos, float yaw, float dt) {
  if (!s->scene) return;
  /* the model's world transform: position, a turn about y */
  float h = yaw * 0.5f, ch, sh;
  { /* cos and sin of the half angle by a short series (yaw is +-pi/2 here, so |h| <= pi/4) */
    float h2 = h * h;
    ch = 1 - h2 / 2 + h2 * h2 / 24 - h2 * h2 * h2 / 720;
    sh = h * (1 - h2 / 6 + h2 * h2 / 120 - h2 * h2 * h2 / 5040);
  }
  xf world = { pos, { 0, sh, 0, ch }, 1 };
  for (int i = 0; i < SB_MAX; i++) {
    spring_bone *b = &s->b[i];
    if (!b->bone) continue;
    const spring_def *d = &SDEF[i];
    t3_object *par = b->bone->parent;
    xf pw = xmul(world, model_xf(par, s->scene)); /* the animated parent in world space */
    t3_vec3 base = xpoint(pw, b->rest);
    t3_vec3 goal = xpoint(pw, vadd(b->rest, b->tip));
    float L = vlen(vsub(goal, base));
    if (!b->live || vlen(vsub(b->T, goal)) > 1.5f) { b->T = goal; b->V = v3(0, 0, 0); b->live = true; }
    t3_vec3 acc = vsub(vmul(vsub(goal, b->T), d->stiff), vmul(b->V, d->damp));
    acc.y -= d->grav;
    b->V = vadd(b->V, vmul(acc, dt));
    b->T = vadd(b->T, vmul(b->V, dt));
    if (d->move) { /* a belly: the bone moves with the mass, a few centimetres at most */
      t3_vec3 off = vsub(b->T, goal);
      float ol = vlen(off), mx = 0.035f * pw.s;
      if (ol > mx) { off = vmul(off, mx / ol); b->T = vadd(goal, off); }
      t3_vec3 lo = vmul(qrot(qconj(pw.q), off), 1.0f / pw.s);
      b->bone->position = vadd(b->rest, lo);
      continue;
    }
    /* keep the length, and stay within a cone around the animated direction */
    t3_vec3 g = vnorm(vsub(goal, base)), dd = vnorm(vsub(b->T, base));
    float cmin = 1 - d->cone * d->cone * 0.5f; /* cos of the cone angle, near enough */
    for (int k = 0; k < 6 && vdot(dd, g) < cmin; k++) dd = vnorm(vadd(dd, vmul(g, 0.4f)));
    b->T = vadd(base, vmul(dd, L));
    /* the bone's rotation in its parent's frame: from the rest tip direction to the simulated one */
    t3_vec3 local = qrot(qconj(pw.q), dd);
    b->bone->quaternion = qfromto(vnorm(b->tip), vnorm(local));
  }
}

/* ── shading ─────────────────────────────────────────────────────────── */
typedef enum { SH_SKIN, SH_CLOTH, SH_HAIR, SH_LEATHER } shade_class;
static void shade_material(t3_mesh *m, int i, shade_class k) {
  t3_material *old = m->materials[i];
  if (old->type == T3_MESH_PHYSICAL_MATERIAL) return;
  t3_material *p = t3_material_clone(old);
  p->type = T3_MESH_PHYSICAL_MATERIAL;
  t3_color c = old->color;
  switch (k) {
  case SH_SKIN: /* light scattered under the skin shows as a warm soft rim; little mirror-like specular */
    p->sheen = 0.7f, p->sheen_roughness = 0.5f;
    p->sheen_color = (t3_color){ 0.1f + 0.35f * c.r, 0.03f + 0.15f * c.g, 0.02f + 0.1f * c.b };
    p->specular_intensity = 0.45f, p->ior = 1.4f;
    break;
  case SH_CLOTH:
    p->sheen = 0.6f, p->sheen_roughness = 0.65f;
    p->sheen_color = (t3_color){ 0.02f + 0.55f * c.r, 0.02f + 0.55f * c.g, 0.02f + 0.55f * c.b };
    p->specular_intensity = 0.35f;
    break;
  case SH_HAIR:
    p->sheen = 0.6f, p->sheen_roughness = 0.35f;
    p->sheen_color = (t3_color){ 0.06f + 0.5f * c.r, 0.05f + 0.5f * c.g, 0.04f + 0.5f * c.b };
    p->specular_intensity = 0.6f;
    break;
  case SH_LEATHER:
    p->clearcoat = 0.35f, p->clearcoat_roughness = 0.38f;
    break;
  }
  m->materials[i] = p;
  t3_release(old);
}
static void shade_tree(t3_object *o, shade_class k) {
  if (o->type == T3_MESH || o->type == T3_SKINNED_MESH) {
    t3_mesh *m = (t3_mesh *)o;
    for (int i = 0; i < m->material_count; i++) shade_material(m, i, k);
  }
  for (int i = 0; i < o->child_count; i++) shade_tree(o->children[i], k);
}
void char_shade(t3_object *scene) {
  static const struct { const char *node; shade_class k; } C[] = {
    { "BodySkin", SH_SKIN }, { "BodyCloth", SH_CLOTH }, { "BodyHair", SH_HAIR }, { "BodyLeather", SH_LEATHER },
  };
  for (size_t i = 0; i < sizeof C / sizeof *C; i++) {
    t3_object *o = t3_object_get_by_name(scene, C[i].node);
    if (o) shade_tree(o, C[i].k);
  }
}
