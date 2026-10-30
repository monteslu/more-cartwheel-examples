/* The rider's body, posed every frame: no clips. Hands stay on the bars, boots in the footwells, and the hips, spine
 * and head work against what the craft is doing: leaning into a carve, crouching for a landing, standing up the stick's
 * pull, looking through the turn. The bind pose has identity rotations, so a bone's local rotation is the inverse of its
 * parent's world rotation times the world rotation asked for (all in the craft's frame). */
#include <math.h>

#include "game.h"

enum { HIPS, SPINE, CHEST, NECK, HEAD, ARML, FOREL, HANDL, ARMR, FORER, HANDR, THIGHL, SHINL, FOOTL, THIGHR, SHINR, FOOTR, BONES };
static const char *const NAMES[BONES] = { "Hips", "Spine", "Chest", "Neck", "Head", "ArmL", "ForeL", "HandL", "ArmR", "ForeR", "HandR", "ThighL", "ShinL", "FootL", "ThighR", "ShinR", "FootR" };
static t3_object *B[RACERS][BONES];

static t3_vec3 V(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 vadd(t3_vec3 a, t3_vec3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 vsub(t3_vec3 a, t3_vec3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static t3_vec3 vmul(t3_vec3 a, float k) { return V(a.x * k, a.y * k, a.z * k); }
static float vdot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float vlen(t3_vec3 a) { return sqrtf(vdot(a, a)); }
static t3_vec3 vnorm(t3_vec3 a) { float l = vlen(a); return l > 1e-5f ? vmul(a, 1 / l) : V(0, 1, 0); }
static t3_vec3 vcross(t3_vec3 a, t3_vec3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static t3_vec3 qrot(t3_quat q, t3_vec3 v) {
  t3_vec3 u = V(q.x, q.y, q.z), t = vmul(vcross(u, v), 2);
  return vadd(vadd(v, vmul(t, q.w)), vcross(u, t));
}
static t3_quat qinv(t3_quat q) { return (t3_quat){ -q.x, -q.y, -q.z, q.w }; }
/* the shortest rotation taking unit a to unit b */
static t3_quat qbetween(t3_vec3 a, t3_vec3 b) {
  float d = vdot(a, b);
  if (d < -0.9995f) return (t3_quat){ 1, 0, 0, 0 };
  t3_vec3 c = vcross(a, b);
  return t3_quat_normalize((t3_quat){ c.x, c.y, c.z, 1 + d });
}
static t3_quat euler(float x, float y, float z) { return t3_quat_from_euler((t3_euler){ x, y, z, T3_XYZ }); }

void rider_init(void) {}

void rider_bind(int i, t3_object *rider) {
  for (int b = 0; b < BONES; b++) B[i][b] = t3_object_get_by_name(rider, NAMES[b]);
}

/* the knee or elbow between a and target for bones of lengths l1, l2, bent toward pole */
static t3_vec3 joint(t3_vec3 a, t3_vec3 target, float l1, float l2, t3_vec3 pole) {
  t3_vec3 d = vsub(target, a);
  float len = vlen(d);
  float m = l1 + l2 - 0.002f;
  if (len > m) {
    d = vmul(d, m / len);
    len = m;
  }
  t3_vec3 u = vmul(d, 1 / (len > 1e-4f ? len : 1e-4f));
  float x = (l1 * l1 - l2 * l2 + len * len) / (2 * len);
  float h2 = l1 * l1 - x * x, h = h2 > 0 ? sqrtf(h2) : 0;
  t3_vec3 p = vsub(pole, vmul(u, vdot(pole, u)));
  return vadd(vadd(a, vmul(u, x)), vmul(vnorm(p), h));
}

void rider_pose(int i, const craft *c, float alpha) {
  (void)alpha;
  if (!B[i][HIPS]) return;
  /* the wave of the body: everything comes from what the craft does */
  float steer = c->steer, lean = c->lean < 0 ? c->lean : c->lean;
  float crouch = c->kick * 0.28f + c->load * 0.14f;
  float stand = c->air ? 0.05f : 0.0f;
  float speedk = c->speed / 28.0f;
  /* hips over the seat's back, sliding inside the turn and back with the pull */
  float hx = -steer * 0.13f, hz = -0.62f - 0.22f * (lean > 0 ? lean : 0) + 0.1f * (lean < 0 ? -lean : 0);
  float hy = 0.92f - crouch + stand + 0.03f * speedk;
  t3_vec3 hips = V(hx, hy, hz);
  /* the upper body leans into the corner and over the bars, more when it hunts for speed */
  float fwd = 0.5f + 0.18f * speedk - 0.26f * (lean > 0 ? lean : 0) + 0.22f * c->kick;
  float side = steer * 0.2f;
  t3_quat qh = euler(fwd * 0.45f, -steer * 0.15f, side * 0.4f);
  t3_quat qs = t3_quat_multiply(qh, euler(fwd * 0.3f, -steer * 0.12f, side * 0.3f));
  t3_quat qc = t3_quat_multiply(qs, euler(fwd * 0.25f, -steer * 0.12f, side * 0.3f));
  B[i][HIPS]->position = hips;
  B[i][HIPS]->quaternion = qh;
  B[i][SPINE]->quaternion = t3_quat_multiply(qinv(qh), qs);
  B[i][CHEST]->quaternion = t3_quat_multiply(qinv(qs), qc);
  /* the head stays level and looks through the turn */
  t3_quat qhead = euler(0.1f - (fwd * 0.2f), -steer * 0.55f, 0);
  B[i][NECK]->quaternion = t3_quat_multiply(qinv(qc), qc);
  B[i][HEAD]->quaternion = t3_quat_multiply(qinv(qc), qhead);
  /* world (craft frame) joints from the bones' bind offsets */
  t3_vec3 spine = vadd(hips, qrot(qh, V(0, 0.14f, 0)));
  t3_vec3 chest = vadd(spine, qrot(qs, V(0, 0.2f, 0)));
  /* arms to the grips (they turn with the bars) */
  float bar = steer * 0.42f;
  float bs = sinf(bar), bc = cosf(bar);
  for (int s = 0; s < 2; s++) {
    float sg = s ? -1.0f : 1.0f;
    int arm = s ? ARMR : ARML, fore = s ? FORER : FOREL, hand = s ? HANDR : HANDL;
    t3_vec3 sh = vadd(chest, qrot(qc, V(sg * 0.2f, 0.2f, 0)));
    t3_vec3 grip = V(0 + sg * 0.285f * bc, 0.885f, 0.3f + sg * 0.285f * bs - 0.03f);
    t3_vec3 elbow = joint(sh, grip, 0.29f, 0.26f, V(sg * 0.9f, -0.3f, -0.35f));
    t3_vec3 d0 = V(sg, 0, 0);
    t3_quat qa = qbetween(d0, vnorm(vsub(elbow, sh)));
    t3_quat qf = qbetween(d0, vnorm(vsub(grip, elbow)));
    B[i][arm]->quaternion = t3_quat_multiply(qinv(qc), qa);
    B[i][fore]->quaternion = t3_quat_multiply(qinv(qa), qf);
    B[i][hand]->quaternion = t3_quat_multiply(qinv(qf), qf);
  }
  /* legs to the footwells */
  for (int s = 0; s < 2; s++) {
    float sg = s ? -1.0f : 1.0f;
    int th = s ? THIGHR : THIGHL, sh = s ? SHINR : SHINL, ft = s ? FOOTR : FOOTL;
    t3_vec3 hipj = vadd(hips, qrot(qh, V(sg * 0.1f, -0.06f, 0)));
    t3_vec3 ankle = V(sg * 0.36f, 0.27f, -0.42f);
    t3_vec3 knee = joint(hipj, ankle, 0.44f, 0.43f, V(sg * 0.5f, 0.2f, 1));
    t3_vec3 d0 = V(0, -1, 0);
    t3_quat qt = qbetween(d0, vnorm(vsub(knee, hipj)));
    t3_quat qn = qbetween(d0, vnorm(vsub(ankle, knee)));
    B[i][th]->quaternion = t3_quat_multiply(qinv(qh), qt);
    B[i][sh]->quaternion = t3_quat_multiply(qinv(qt), qn);
    B[i][ft]->quaternion = qinv(qn);
  }
}
