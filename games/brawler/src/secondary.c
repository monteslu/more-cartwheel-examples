/* brawler: secondary motion (spring bones) and faces (expression morph targets).
 *
 * Spring bones: hair, coat tails and bellies hang on bones no clip keys (HairA, HairB, CoatL, CoatR, Belly). Each fixed
 * tick, after the animation wrote the pose, a bone's tip is a point mass in world space pulled toward where the animated
 * parent would carry it (stiffness, damping, gravity), kept at the bone's length and inside a cone around the animated
 * direction; the bone is then turned (or for a belly, moved) to follow it. Running, turning, landing and hits make it swing.
 * Written generically (bones by name, any model under a Node3D) so the engine can take it as a skeleton modifier later.
 * Only + - * / and sqrt: the same on every host.
 *
 * Shading comes from the glb itself (glTF material extensions and skin shading in extras, read by the engine's glTF
 * loading): nothing is converted here. */
#include <math.h>
#include <string.h>

#include "cartwheel/morph.h"
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

/* ── faces: expression morph targets on the head mesh, driven by name every tick ──
 * The clip a rig plays picks its expression and how strong it is through the clip (a snarl into a punch, a wince on a hit,
 * a slack face when knocked down, a grin on a taunt); the weights ease toward it so a cut between clips never pops.
 * A blink is layered on top every 2 to 5.5 seconds (a fixed sequence per rig) and at once on a hit; it never adds to an
 * expression that already closes the lids. The Jaw bone (keyed in the clips) still opens the mouth.
 * Presentation only: nothing here feeds the simulation. */
enum { FACE_MAX = 96, NT = 11 };
static const char *TG[NT] = { "blink", "snarl", "shout", "wince", "ko", "grin", "effort", "grimace", "fear", "anger", "bliss" };
enum { T_BLINK, T_SNARL, T_SHOUT, T_WINCE, T_KO, T_GRIN, T_EFFORT, T_GRIMACE, T_FEAR, T_ANGER, T_BLISS };
/* how far each target closes the upper lids, as a share of a full blink (tools/face.mjs LID_CLOSE; fear opens them) */
static const float LID_CLOSE[NT] = { 1.0f, 0.303f, 0.333f, 0.879f, 0.576f, 0.167f, 0.212f, 0.485f, -0.364f, 0.485f, 0.667f };
typedef struct face_rig {
  const spring_rig *key;
  t3_object *root;
  float w[NT], wrote[NT];
  int t, next; /* ticks since the last blink started; when the next one starts */
  float react; int react_t; /* the last blow (char_jolt strength over 1.6) and ticks since it: the temper's face (any blow, flinch or not) */
  int temper; /* how the face takes a hit: FACE_PLAIN, FACE_FEAR, FACE_ANGER, FACE_BLISS (face_temper) */
  uint32_t rng;
} face_rig;
static face_rig FR[FACE_MAX];
static int fr_n;
static face_rig *face_of(const spring_rig *s) {
  for (int i = 0; i < fr_n; i++) if (FR[i].key == s) return &FR[i];
  return NULL;
}
static uint32_t fr_rand(face_rig *f) { f->rng = f->rng * 1664525u + 1013904223u; return f->rng >> 8; }
static void face_bind(const spring_rig *s, t3_object *scene) {
  face_rig *f = face_of(s);
  if (!f) { if (fr_n >= FACE_MAX) return; f = &FR[fr_n++]; }
  memset(f, 0, sizeof *f);
  f->key = s;
  f->root = cw_morph_names(scene, NULL, 0) > 0 ? scene : NULL;
  f->rng = 0x9e3779b9u ^ (uint32_t)(f - FR) * 2654435761u;
  for (int i = 0; i < NT; i++) f->wrote[i] = -1;
  f->t = 1000;
  f->next = 60 + (int)(fr_rand(f) % 200);
}
/* the blink's closing amount at tick t of the blink: shut in 4 ticks, held 2, open in 7 */
static float blink_at(int t) {
  if (t < 4) return (float)(t + 1) / 4.0f;
  if (t < 6) return 1.0f;
  if (t < 13) return 1.0f - (float)(t - 5) / 7.0f;
  return 0.0f;
}
static float sst(float a, float b, float x) { float t = (x - a) / (b - a); t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3 - 2 * t); }
static float strike(float peak, float u) { return sst(0, peak, u) * (1 - 0.55f * sst(0.72f, 1, u)); }
static float hit_env(float u) { return sst(0, 0.12f, u) * (1 - 0.6f * sst(0.45f, 1, u)); }
static float bump(float u) { return sinf(u * 3.14159265f); }
/* the expression a clip asks for at u (0..1 through it) */
static void clip_face(const char *c, float u, float *o, int temper) {
  memset(o, 0, sizeof(float) * NT);
  if (!c) return;
#define IS(n) (!strcmp(c, n))
  if (IS("run")) o[T_EFFORT] = 0.4f;
  else if (IS("jump")) o[T_EFFORT] = 0.6f * bump(u);
  else if (IS("punch1")) o[T_SNARL] = strike(0.35f, u);
  else if (IS("punch2")) o[T_SNARL] = strike(0.38f, u);
  else if (IS("kick")) o[T_SNARL] = strike(0.38f, u);
  else if (IS("stab")) o[T_SNARL] = strike(0.4f, u);
  else if (IS("knee")) o[T_SNARL] = strike(0.32f, u);
  else if (IS("flick")) o[T_SNARL] = strike(0.6f, u);
  else if (IS("finisher") || IS("swing")) o[T_SHOUT] = strike(0.45f, u);
  else if (IS("jumpkick")) o[T_SHOUT] = strike(0.25f, u);
  else if (IS("spin")) o[T_SHOUT] = strike(0.2f, u);
  else if (IS("slam")) { o[T_EFFORT] = sst(0, 0.4f, u) * (1 - sst(0.45f, 0.6f, u)); o[T_SHOUT] = sst(0.45f, 0.62f, u) * (1 - 0.6f * sst(0.8f, 1, u)); }
  else if (IS("throw")) { o[T_EFFORT] = sst(0, 0.3f, u) * (1 - sst(0.35f, 0.5f, u)); o[T_SHOUT] = sst(0.35f, 0.55f, u) * (1 - 0.6f * sst(0.8f, 1, u)); }
  else if (IS("thump")) { o[T_EFFORT] = sst(0, 0.3f, u) * (1 - sst(0.55f, 0.62f, u)); o[T_SHOUT] = sst(0.55f, 0.62f, u) * (1 - sst(0.7f, 0.95f, u)); }
  else if (IS("grab")) o[T_EFFORT] = 0.7f;
  else if (IS("shoulder")) o[T_EFFORT] = 0.85f;
  else if (IS("pickup")) o[T_EFFORT] = 0.5f * bump(u);
  else if (IS("getup") || IS("getup_prone")) o[T_EFFORT] = 0.8f * bump(u * 0.9f);
  else if (IS("hit")) o[T_WINCE] = hit_env(u);
  else if (IS("hit_gut")) { o[T_WINCE] = hit_env(u); o[T_GRIMACE] = 0.4f * hit_env(u); }
  else if (IS("hit_hard")) { o[T_WINCE] = hit_env(u); o[T_GRIMACE] = 0.3f * hit_env(u); }
  else if (IS("knockdown")) { o[T_WINCE] = sst(0, 0.1f, u) * (1 - sst(0.4f, 0.7f, u)); o[T_KO] = sst(0.45f, 0.85f, u); }
  else if (IS("down")) o[T_KO] = 1;
  else if (IS("dead")) { o[T_BLINK] = 1; o[T_KO] = 0.5f; }
  else if (IS("grabbed")) o[T_GRIMACE] = 0.85f;
  else if (IS("thrown")) { o[T_WINCE] = 0.7f; o[T_GRIMACE] = 0.4f; }
  else if (IS("comb")) o[T_GRIN] = sst(0.05f, 0.2f, u) * (1 - sst(0.88f, 1, u));
  else if (IS("thumbsup")) o[T_GRIN] = sst(0.04f, 0.18f, u) * (1 - sst(0.86f, 1, u));
  else if (IS("taunt")) o[T_GRIN] = sst(0.05f, 0.25f, u) * (1 - sst(0.85f, 1, u));
  else if (IS("block")) { o[T_WINCE] = 0.35f; o[T_EFFORT] = 0.3f; }
  /* the hit reactions by temper, cartoon-sized (they have to read from the street camera): the big men are frightened by a
     blow and horrified when it floors them, the little ones furious, and the creep loves every one of them */
  if (temper != FACE_PLAIN) {
    bool hit = IS("hit") || IS("hit_gut") || IS("hit_hard"), kd = IS("knockdown"), dn = IS("down") || IS("getup") || IS("getup_prone");
    float k = IS("hit_hard") ? 1.0f : IS("hit_gut") ? 0.9f : 0.8f;
    if (hit || kd || dn) {
      float e = hit ? sst(0, 0.1f, u) * (1 - 0.45f * sst(0.55f, 1, u)) * k : kd ? sst(0, 0.12f, u) : 1.0f;
      if (temper == FACE_FEAR) { o[T_WINCE] *= 0.2f, o[T_GRIMACE] *= 0.2f; o[T_FEAR] = e; if (kd) o[T_KO] *= 0.4f, o[T_SHOUT] = 0.6f * e; if (dn) o[T_KO] *= 0.5f, o[T_FEAR] = 0.6f; }
      else if (temper == FACE_ANGER) { o[T_WINCE] *= 0.25f, o[T_GRIMACE] *= 0.25f; o[T_ANGER] = e; if (kd) o[T_KO] *= 0.3f; if (dn) o[T_KO] *= 0.4f, o[T_ANGER] = 0.8f; }
      else { o[T_WINCE] = 0, o[T_GRIMACE] = 0, o[T_KO] = 0; o[T_BLISS] = hit ? e : kd ? 1.0f : 0.9f; }
    }
  }
#undef IS
}
void face_temper(spring_rig *s, int temper) { face_rig *f = face_of(s); if (f) f->temper = temper; }
void face_express(spring_rig *s, const char *clip, float u) {
  face_rig *f = face_of(s);
  if (!f || !f->root) return;
  if (--f->next <= 0) { f->t = 0; f->next = 120 + (int)(fr_rand(f) % 210); }
  float b = blink_at(f->t++), want[NT];
  clip_face(clip, u, want, f->temper);
  if (f->temper != FACE_PLAIN && f->react > 0) { /* the temper's face on every blow: in at once, held half a second, eased back */
    int t = f->react_t++;
    float env = t < 4 ? (float)(t + 1) / 4.0f : t < 46 ? 1.0f : 1.0f - (float)(t - 46) / 20.0f; /* held most of a second: it has to read */
    if (env <= 0) f->react = 0;
    else {
      int tg = f->temper == FACE_FEAR ? T_FEAR : f->temper == FACE_ANGER ? T_ANGER : T_BLISS;
      float k = env * (0.75f + 0.45f * f->react); /* the harder the blow, the bigger the face */
      if (k > want[tg]) want[tg] = k;
      want[T_WINCE] *= 1 - env, want[T_GRIMACE] *= 1 - env;
      if (tg == T_BLISS) want[T_KO] *= 1 - env;
    }
  }
  for (int i = 0; i < NT; i++) f->w[i] += (want[i] - f->w[i]) * 0.35f;
  float closed = 0;
  for (int i = 1; i < NT; i++) closed += f->w[i] * LID_CLOSE[i];
  if (closed < 0) closed = 0;
  float out[NT];
  for (int i = 0; i < NT; i++) out[i] = f->w[i];
  float bl = b - closed;
  if (bl > out[T_BLINK]) out[T_BLINK] = bl;
  for (int i = 0; i < NT; i++) {
    float d = out[i] - f->wrote[i];
    if (d > 0.002f || d < -0.002f || (out[i] == 0 && f->wrote[i] != 0)) { cw_morph_set(f->root, TG[i], out[i]); f->wrote[i] = out[i]; }
  }
}
float face_weight(const spring_rig *s, int target) { const face_rig *f = face_of(s); return f && target >= 0 && target < NT ? f->wrote[target] : -1; }
void sb_bind(spring_rig *s, t3_object *scene) {
  memset(s, 0, sizeof *s);
  face_bind(s, scene);
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
  face_rig *f = face_of(s);
  if (f && f->t > 8 && f->temper != FACE_FEAR) f->t = 0; /* a hit makes the eyes blink (not a frightened face: its eyes stay wide) */
  if (f) { float k = (x < 0 ? -x : x) / (2.2f * 1.6f); if (k > 1) k = 1; if (f->react <= 0 || k >= f->react * 0.8f) f->react = k, f->react_t = 0; }
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

