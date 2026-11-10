/* The rider's body, posed every frame: no clips. Hands stay on the bars, boots in the footwells, and the hips, spine
 * and head work against what the craft is doing: leaning into a carve, crouching for a landing, standing up the stick's
 * pull, looking through the turn. The bind pose has identity rotations, so a bone's local rotation is the inverse of its
 * parent's world rotation times the world rotation asked for (all in the craft's frame). */
#include <math.h>

#include "game.h"

enum { HIPS, SPINE, CHEST, NECK, HEAD, ARML, FOREL, HANDL, ARMR, FORER, HANDR, THIGHL, SHINL, FOOTL, THIGHR, SHINR, FOOTR, BONES };
static const char *const NAMES[BONES] = { "Hips", "Spine", "Chest", "Neck", "Head", "ArmL", "ForeL", "HandL", "ArmR", "ForeR", "HandR", "ThighL", "ShinL", "FootL", "ThighR", "ShinR", "FootR" };
static t3_object *B[RACERS][BONES];
float rider_torso[RACERS]; /* how far forward the upper body is thrown (radians, about): the rubric's measure */
float rider_foot_side[RACERS];
float rider_pole[RACERS];     /* the stand-up's handlepole: radians up from lying along the hood */
float rider_scale[RACERS][2] = { { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 } }; /* the rider node's scale across and up (view.c) */
float rider_foot_gap[RACERS]; /* how far a boot ends from where it was asked to stand, metres (the tray, the hood) */
float rider_hands_gap[RACERS];/* how far a glove ends from its grip */
float rider_stand[RACERS];    /* a stand-up rider: 0 kneeling in the tray, 1 standing */
float g_stand_hips;           /* debug: sw stand_hips <metres> lifts the standing hips, the grounding gate's control */
float g_foot_gap_max, g_hands_gap_max; /* the worst of every racer's since the counts were cleared (sw ground_clear 1) */
int32_t g_pose_frames;

static t3_vec3 V(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
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

static void rider_pose_stand(int i, const craft *c, float steer, float by);
void rider_pose(int i, const craft *c, float alpha) {
  (void)alpha;
  if (!B[i][HIPS]) return;
  /* the body works: a spring behind the hips takes the craft's heave and yaw, so a rise compresses the legs, a drop lifts
   * the rider off the seat, and the upper body lags into each turn. Presentation only. */
  static float bv[RACERS], by[RACERS], sv[RACERS], sx[RACERS], lastvy[RACERS];
  float acc = (c->vy - lastvy[i]);
  lastvy[i] = c->vy;
  bv[i] += (-acc * 0.012f - by[i] * 0.16f - bv[i] * 0.3f);
  by[i] += bv[i];
  by[i] = by[i] > 0.14f ? 0.14f : by[i] < -0.22f ? -0.22f : by[i];
  sv[i] += ((c->steer - sx[i]) * 0.1f - sv[i] * 0.28f);
  sx[i] += sv[i];
  float steer = sx[i], lean = c->lean;
  if (g_craft == CRAFT_STAND) { rider_pose_stand(i, c, steer, by[i]); return; }
  /* off the craft: the body hangs back from the hull, flat on the water, arms out, and climbs back on */
  float off = c->spill > 0 ? (c->spill > 1.5f ? (1.9f - c->spill) / 0.4f : c->spill > 0.4f ? 1.0f : c->spill / 0.4f) : 0;
  float crouch = c->kick * 0.26f + c->load * 0.16f - by[i];
  float stand = c->air ? 0.08f : 0.0f;
  float speedk = c->speed / 28.0f;
  /* a riding crouch, not a seat: knees bent, weight over the bars, hips out over the side in a turn, back with the pull */
  float hx = -steer * 0.2f, hz = -0.58f - 0.24f * (lean > 0 ? lean : 0) + 0.12f * (lean < 0 ? -lean : 0);
  float hy = 0.86f - crouch + stand - 0.05f * speedk;
  { float k = c->legout > 0 ? sinf(3.14159265f * clampf(1.0f - c->legout / KICK_T, 0, 1)) : 0; hx -= c->leg_side * 0.14f * k; hy += 0.12f * k; }
  t3_vec3 hips = V(hx, hy - off * 0.5f, hz - off * 2.3f);
  /* the upper body leans into the corner and over the bars, more when it hunts for speed */
  /* off the craft he lies back on the water: flat, on his back, feet toward the hull */
  float fwd = (0.78f + 0.2f * speedk - 0.42f * (lean > 0 ? lean : 0) + 0.25f * c->kick - by[i] * 1.2f) * (1.0f - off) - off * 1.45f;
  rider_torso[i] = fwd;
  float side = steer * 0.34f;
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
    /* let go of the bars, arms out on the water */
    if (off > 0) { t3_vec3 out = vadd(sh, V(sg * 0.5f, -0.04f, -0.05f)); grip = V(grip.x + (out.x - grip.x) * off, grip.y + (out.y - grip.y) * off, grip.z + (out.z - grip.z) * off); }
    /* a style move on a short hop: that hand off the bar, thrown up and out, and back on before the water */
    if (c->style > 0 && sg * c->style_side > 0 && off <= 0) {
      float k = clampf(sinf(3.14159265f * clampf(1.0f - c->style / STYLE_T, 0, 1)) * 1.6f, 0, 1);
      t3_vec3 up = vadd(sh, V(sg * 0.42f, 0.38f, -0.12f));
      grip = V(grip.x + (up.x - grip.x) * k, grip.y + (up.y - grip.y) * k, grip.z + (up.z - grip.z) * k);
    }
    t3_vec3 elbow = joint(sh, grip, 0.29f, 0.26f, V(sg * 0.95f, 0.15f, -0.25f));
    t3_vec3 d0 = V(sg, 0, 0);
    t3_quat qa = qbetween(d0, vnorm(vsub(elbow, sh)));
    t3_quat qf = qbetween(d0, vnorm(vsub(grip, elbow)));
    B[i][arm]->quaternion = t3_quat_multiply(qinv(qc), qa);
    B[i][fore]->quaternion = t3_quat_multiply(qinv(qa), qf);
    B[i][hand]->quaternion = t3_quat_multiply(qinv(qf), qf);
  }
  /* legs to the footwells */
  rider_foot_side[i] = 0;
  for (int s = 0; s < 2; s++) {
    float sg = s ? -1.0f : 1.0f;
    int th = s ? THIGHR : THIGHL, sh = s ? SHINR : SHINL, ft = s ? FOOTR : FOOTL;
    t3_vec3 hipj = vadd(hips, qrot(qh, V(sg * 0.1f, -0.06f, 0)));
    t3_vec3 ankle = V(sg * 0.38f, 0.27f + 0.1f, -0.36f - 0.08f * (sg * steer > 0 ? 1.0f : 0.0f));
    /* off the craft the legs go with the body, out toward the hull */
    if (off > 0) { t3_vec3 out = vadd(hipj, V(sg * 0.14f, 0.02f, 0.8f)); ankle = V(ankle.x + (out.x - ankle.x) * off, ankle.y + (out.y - ankle.y) * off, ankle.z + (out.z - ankle.z) * off); }
    /* a kick out: that foot leaves the craft, straight out to the side and up, the other stays in its well */
    if (c->legout > 0 && sg * c->leg_side > 0) { float k = sinf(3.14159265f * clampf(1.0f - c->legout / KICK_T, 0, 1)); k = clampf(k * 1.6f, 0, 1); ankle = V(ankle.x + (sg * 0.86f - ankle.x) * k, ankle.y + (hips.y + 0.1f - ankle.y) * k, ankle.z + (hips.z - 0.05f - ankle.z) * k); }
    if (fabsf(ankle.x) > rider_foot_side[i]) rider_foot_side[i] = fabsf(ankle.x);
    t3_vec3 knee = joint(hipj, ankle, 0.44f, 0.43f, V(sg * 0.5f, 0.2f, 1));
    t3_vec3 d0 = V(0, -1, 0);
    t3_quat qt = qbetween(d0, vnorm(vsub(knee, hipj)));
    t3_quat qn = qbetween(d0, vnorm(vsub(ankle, knee)));
    B[i][th]->quaternion = t3_quat_multiply(qinv(qh), qt);
    B[i][sh]->quaternion = t3_quat_multiply(qinv(qt), qn);
    B[i][ft]->quaternion = t3_quat_multiply(qinv(qn), qbetween(V(0, 0, 1), vnorm(V(0, 0.05f, 1))));
  }
}

/* The stand-up rider: on their feet in the tray behind the handlepole, fairly upright. Feet apart (the left a little
 * ahead), knees a little bent (about 145 degrees), the torso leaned forward about 20 degrees, the arms down and forward
 * to the bars at hip height, the head up looking ahead. The pole is hinged at the bow and its angle follows the rider's
 * hands, so it rises and falls with them and the knees. In a carve the hips and shoulders go to the inside of the turn (the
 * hull is rolled onto its chine by the sim), and the knees take the chop. Kneeling in the tray only at a standstill. Tricks
 * of its own: a pole stand up on the hood, and a superman in the air, feet off the tray and the body stretched out behind
 * the bars. Everything is placed in the craft's frame and brought into the rider node's (scaled) frame. Sizes are
 * tools/standup-model.mjs's.
 * sw stand_old 1 (stiff and dead upright) and 2 (a deep crouch): the pose gate's controls */
#define POLE_PY 0.94f
#define POLE_PZ 0.8f
#define POLE_LEN 1.06f
#define GRIP_X 0.34f
#define TRAY_Y 0.42f
#define HOOD_Y 1.02f   /* the pad on the hood's top, where a pole stand puts the feet */
#define HOOD_Z 0.33f
#define BOW_Z 1.08f
#define ANKLE_UP 0.12f /* the ankle over the boot's sole */
int g_stand_old;
float g_pose[8]; /* racer 0 this frame: head top over the tray, front knee's angle (deg), torso's lean from upright (deg), hull ahead of the front foot (m) */
static float lerpf2(float a, float b, float t) { return a + (b - a) * t; }
static float smoothk(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static t3_vec3 vlerp(t3_vec3 a, t3_vec3 b, float t) { return V(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
float rider_pts[RACERS][6][3]; /* craft frame: head top, neck, hips, front ankle, rear ankle, bar middle (view.c's screen measure) */
static void rider_pose_stand(int i, const craft *c, float steer, float by) {
  float kb = rider_scale[i][0], kt = rider_scale[i][1];
  #define TO_R(v) V((v).x / kb, (v).y / kt, (v).z / kb)
  #define TO_C(v) V((v).x * kb, (v).y * kt, (v).z * kb)
  float want = c->spill <= 0 && (c->speed > 2.5f || c->in.throttle > 0.3f || c->air || c->dive_t > 0) ? 1.0f : 0.0f;
  rider_stand[i] += (want - rider_stand[i]) * (want > rider_stand[i] ? 0.07f : 0.04f);
  float up = smoothk(rider_stand[i]);
  float off = c->spill > 0 ? (c->spill > 1.5f ? (1.9f - c->spill) / 0.4f : c->spill > 0.4f ? 1.0f : c->spill / 0.4f) : 0;
  float speedk = clampf(c->speed / 25.0f, 0, 1.2f);
  float crouch = c->kick * 0.18f + c->load * 0.1f - by * 0.8f + 0.04f * (1.0f - c->plane);
  float pk = c->pole_t > 0 ? clampf(sinf(3.14159265f * clampf(1.0f - c->pole_t / POLE_T, 0, 1)) * 1.7f, 0, 1) : 0;
  float sk = c->legout > 0 ? clampf(sinf(3.14159265f * clampf(1.0f - c->legout / KICK_T, 0, 1)) * 1.7f, 0, 1) : 0;
  pk = smoothk(pk), sk = smoothk(sk);
  float lean_in = -steer * (0.16f + 0.06f * speedk); /* hips and shoulders to the inside of the carve */
  /* the feet: apart in the tray, the left a little ahead */
  t3_vec3 af_st = V(0.19f, TRAY_Y + ANKLE_UP, -0.42f), ar_st = V(-0.19f, TRAY_Y + ANKLE_UP, -0.55f);
  /* the hips: over the feet, the knees a little bent (about 145 degrees), more as they soak a landing or the chop;
   * the torso leaned forward about 20 degrees */
  t3_vec3 hips_st = V(lean_in - c->wobble * 0.3f, TRAY_Y + ANKLE_UP + 0.86f - crouch, -0.56f);
  float fwd_st = 0.5f + 0.06f * speedk + 0.3f * crouch;
  /* the controls: 1 stiff and dead upright, 2 the deep crouch */
  if (g_stand_old == 1) hips_st.y = TRAY_Y + ANKLE_UP + 0.93f, fwd_st = 0.04f;
  if (g_stand_old == 2) hips_st = V(lean_in, TRAY_Y + ANKLE_UP + 0.56f - crouch, -0.72f), fwd_st = 1.0f + 0.1f * speedk;
  t3_vec3 hips = vlerp(V(0, TRAY_Y + 0.52f, -0.66f), hips_st, up);
  hips = vlerp(hips, V(0, HOOD_Y + ANKLE_UP + 0.62f, HOOD_Z - 0.06f), pk);
  hips = vlerp(hips, V(0, 1.08f, -0.86f), sk);
  hips = V(hips.x, hips.y - off * 0.5f, hips.z - off * 2.0f);
  float fwd = lerpf2(0.3f, fwd_st, up);
  fwd = fwd + (0.25f - fwd) * pk;
  fwd = fwd + (1.45f - fwd) * sk;
  fwd = fwd * (1.0f - off) - off * 1.45f;
  rider_torso[i] = fwd;
  float side = steer * 0.5f - c->wobble * 0.6f;
  t3_quat qh = euler(fwd * 0.45f, -steer * 0.1f, side * 0.4f);
  t3_quat qs = t3_quat_multiply(qh, euler(fwd * 0.3f, -steer * 0.08f, side * 0.3f));
  t3_quat qc = t3_quat_multiply(qs, euler(fwd * 0.25f, -steer * 0.08f, side * 0.3f));
  t3_vec3 hips_r = TO_R(hips);
  /* the pole: its bars where the hands reach, a little below the hips' height ahead of the rider (waist height standing) */
  float hand_y = hips.y + 0.02f - 0.1f * crouch, th; /* the bars at hip height: the arms reach them down and forward */
  { float s = clampf((hand_y - POLE_PY) / POLE_LEN, -0.2f, 0.95f); th = lerpf2(0.12f, asinf(s), up); }
  th = th + (1.08f - th) * pk;
  th = th + (0.55f - th) * sk;
  th = clampf(th, 0.08f, 1.15f);
  rider_pole[i] = th;
  float st = sinf(th), ct = cosf(th), bar = -steer * 0.42f, sb = sinf(bar), cb = cosf(bar);
  t3_vec3 grips[2];
  for (int s = 0; s < 2; s++) {
    float sg = s ? -1.0f : 1.0f, lx = sg * GRIP_X * cb, ly = 0.065f, lz = -sg * GRIP_X * sb - POLE_LEN;
    grips[s] = TO_R(V(lx, POLE_PY + ly * ct - lz * st, POLE_PZ + ly * st + lz * ct));
  }
  /* arms nearly straight: a rider short of the bars goes to them with the whole body */
  /* (forward first, so the knees keep their bend; then down as well, when the bars are lower than the arms reach) */
  for (int pass = 0; pass < 3; pass++) {
    t3_vec3 ch = vadd(vadd(hips_r, qrot(qh, V(0, 0.14f, 0))), qrot(qs, V(0, 0.2f, 0)));
    float worst = 0;
    t3_vec3 to = V(0, 0, 0);
    for (int s = 0; s < 2; s++) { float sg = s ? -1.0f : 1.0f; t3_vec3 sh = vadd(ch, qrot(qc, V(sg * 0.2f, 0.2f, 0))); t3_vec3 d = vsub(grips[s], sh); float l = vlen(d) - 0.535f; if (l > worst) worst = l; to = vadd(to, d); }
    if (worst <= 0 || off > 0) break;
    if (pass == 0) to.y = 0;
    hips_r = vadd(hips_r, vmul(vnorm(to), worst));
  }
  /* and the legs are only so long: hips too far from a boot come down toward it (a rise of the hull lifts the body's spring) */
  if (off <= 0 && sk < 0.01f) for (int s = 0; s < 2; s++) {
    float sg = s ? -1.0f : 1.0f;
    t3_vec3 hipj = vadd(hips_r, qrot(qh, V(sg * 0.1f, -0.06f, 0))), at = TO_R(vlerp(vlerp(V(sg * 0.12f, TRAY_Y + 0.1f, -0.95f), s ? ar_st : af_st, up), V(sg * 0.12f, HOOD_Y + ANKLE_UP, HOOD_Z + sg * 0.07f), pk));
    t3_vec3 d = vsub(at, hipj); float l = vlen(d) - 0.865f;
    if (l > 0) hips_r = vadd(hips_r, vmul(vnorm(d), l));
  }
  hips_r.y += g_stand_hips / kt; /* (the grounding gate's control: hips lifted past what the legs reach) */
  B[i][HIPS]->position = hips_r;
  B[i][HIPS]->quaternion = qh;
  B[i][SPINE]->quaternion = t3_quat_multiply(qinv(qh), qs);
  B[i][CHEST]->quaternion = t3_quat_multiply(qinv(qs), qc);
  /* the head up, looking ahead past the bars (in a superman along the line of flight) */
  t3_quat qhead = euler(0.15f - 0.15f * sk, -steer * 0.5f, 0);
  B[i][NECK]->quaternion = t3_quat_multiply(qinv(qc), qc);
  B[i][HEAD]->quaternion = t3_quat_multiply(qinv(qc), qhead);
  t3_vec3 spine = vadd(hips_r, qrot(qh, V(0, 0.14f, 0)));
  t3_vec3 chest = vadd(spine, qrot(qs, V(0, 0.2f, 0)));
  rider_hands_gap[i] = 0;
  for (int s = 0; s < 2; s++) {
    float sg = s ? -1.0f : 1.0f;
    int arm = s ? ARMR : ARML, fore = s ? FORER : FOREL, hand = s ? HANDR : HANDL;
    t3_vec3 sh = vadd(chest, qrot(qc, V(sg * 0.2f, 0.2f, 0)));
    t3_vec3 grip = grips[s];
    if (off > 0) { t3_vec3 out = vadd(sh, V(sg * 0.5f, -0.04f, -0.05f)); grip = vlerp(grip, out, off); }
    t3_vec3 elbow = joint(sh, grip, 0.29f, 0.26f, V(sg * 0.9f, -0.2f, -0.3f));
    t3_vec3 d0 = V(sg, 0, 0);
    t3_quat qa = qbetween(d0, vnorm(vsub(elbow, sh)));
    t3_quat qf = qbetween(d0, vnorm(vsub(grip, elbow)));
    B[i][arm]->quaternion = t3_quat_multiply(qinv(qc), qa);
    B[i][fore]->quaternion = t3_quat_multiply(qinv(qa), qf);
    B[i][hand]->quaternion = t3_quat_multiply(qinv(qf), qf);
    if (off <= 0) { t3_vec3 reach = vadd(elbow, vmul(vnorm(vsub(grip, elbow)), 0.26f)); float g = vlen(vsub(reach, grip)) * kb; if (g > rider_hands_gap[i]) rider_hands_gap[i] = g; }
  }
  rider_foot_side[i] = 0;
  rider_foot_gap[i] = 0;
  t3_vec3 ank_c[2], knee_c[2], hip_c[2];
  for (int s = 0; s < 2; s++) {
    float sg = s ? -1.0f : 1.0f;
    int thg = s ? THIGHR : THIGHL, shn = s ? SHINR : SHINL, ft = s ? FOOTR : FOOTL;
    t3_vec3 hipj = vadd(hips_r, qrot(qh, V(sg * 0.1f, -0.06f, 0)));
    t3_vec3 a_st = s ? ar_st : af_st;
    t3_vec3 ank = vlerp(V(sg * 0.12f, TRAY_Y + 0.1f, -0.95f), a_st, up);
    ank = vlerp(ank, V(sg * 0.12f, HOOD_Y + ANKLE_UP, HOOD_Z + sg * 0.07f), pk);
    ank = vlerp(ank, V(sg * 0.17f, 1.16f, -1.72f), sk);
    t3_vec3 target = ank;
    t3_vec3 ankle = TO_R(ank);
    if (off > 0) { t3_vec3 out = vadd(hipj, V(sg * 0.14f, 0.02f, 0.8f)); ankle = vlerp(ankle, out, off); }
    if (fabsf(ankle.x) > rider_foot_side[i]) rider_foot_side[i] = fabsf(ankle.x);
    /* the knee forward over the toes, a little out (bow-legged over the tray, never out over the rails) */
    t3_vec3 knee = joint(hipj, ankle, 0.44f, 0.43f, vlerp(V(sg * 0.15f, -0.6f, 1), V(sg * 0.25f, 0.1f, 1), up));
    t3_vec3 d0 = V(0, -1, 0);
    t3_quat qt = qbetween(d0, vnorm(vsub(knee, hipj)));
    t3_quat qn = qbetween(d0, vnorm(vsub(ankle, knee)));
    B[i][thg]->quaternion = t3_quat_multiply(qinv(qh), qt);
    B[i][shn]->quaternion = t3_quat_multiply(qinv(qt), qn);
    t3_vec3 toe = vnorm(vlerp(V(0, -0.35f, -1), V(sg * 0.12f, 0.02f, 1), up));
    toe = vnorm(vlerp(toe, V(0, 0.2f, -1), sk));
    B[i][ft]->quaternion = t3_quat_multiply(qinv(qn), qbetween(V(0, 0, 1), toe));
    t3_vec3 reach = vadd(knee, vmul(vnorm(vsub(ankle, knee)), 0.43f));
    ank_c[s] = TO_C(reach), knee_c[s] = TO_C(knee), hip_c[s] = TO_C(hipj);
    if (off <= 0 && sk < 0.01f) { float g = vlen(vsub(ank_c[s], target)); if (g > rider_foot_gap[i]) rider_foot_gap[i] = g; }
  }
  /* the pose's measures (the pose gate): the head's top, the front knee, the torso's lean, the hull ahead of the feet */
  {
    t3_vec3 neck = vadd(chest, qrot(qc, V(0, 0.27f, 0)));
    t3_vec3 top = vadd(vadd(neck, qrot(qc, V(0, 0.1f, 0))), qrot(qhead, V(0, 0.2f, 0)));
    t3_vec3 neck_c = TO_C(neck), top_c = TO_C(top), hips_c = TO_C(hips_r);
    t3_vec3 a = vsub(hip_c[0], knee_c[0]), b = vsub(ank_c[0], knee_c[0]);
    float knee_deg = acosf(clampf(vdot(a, b) / (vlen(a) * vlen(b) + 1e-6f), -1, 1)) * 57.29578f;
    t3_vec3 tv = vsub(neck_c, hips_c);
    float lean_deg = acosf(clampf(tv.y / (vlen(tv) + 1e-6f), -1, 1)) * 57.29578f;
    float front_z = fmaxf(ank_c[0].z, ank_c[1].z);
    float barm_y = POLE_PY + 0.065f * ct + POLE_LEN * st, barm_z = POLE_PZ + 0.065f * st - POLE_LEN * ct;
    float *pts[6] = { rider_pts[i][0], rider_pts[i][1], rider_pts[i][2], rider_pts[i][3], rider_pts[i][4], rider_pts[i][5] };
    t3_vec3 pv[6] = { top_c, neck_c, hips_c, ank_c[0], ank_c[1], V(0, barm_y, barm_z) };
    for (int k = 0; k < 6; k++) pts[k][0] = pv[k].x, pts[k][1] = pv[k].y, pts[k][2] = pv[k].z;
    if (i == 0) g_pose[0] = top_c.y - TRAY_Y, g_pose[1] = knee_deg, g_pose[2] = lean_deg, g_pose[3] = BOW_Z - front_z;
  }
  if (rider_foot_gap[i] > g_foot_gap_max) g_foot_gap_max = rider_foot_gap[i];
  if (rider_hands_gap[i] > g_hands_gap_max) g_hands_gap_max = rider_hands_gap[i];
  if (i == 0) g_pose_frames++;
  #undef TO_R
  #undef TO_C
}
