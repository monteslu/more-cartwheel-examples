/* brawler: ragdolls for knockdowns, throws and deaths (cartwheel/ragdoll.h).
 *
 * PRESENTATION ONLY. The simulation (sim.c) decides who falls, where the body is, how long it stays down and when it
 * gets up, on its fixed tick, exactly as before. This file reads that state each tick and shows it: a fall turns the
 * character's skeleton into a Box3D ragdoll (a capsule per bone, cone/twist and hinge limits) seeded from the pose it
 * had and pushed by the blow; it lands on the street, the kerb and the props (a world of its own, never read back by
 * the rules); a horizontal spring keeps it within reach of the body's position in the simulation. When the simulation
 * says get up, the settled ragdoll picks the face-up or face-down get-up clip, the model turns to match the way the
 * body lies, and the pose blends from the ragdoll into the clip and back to the fight. The dead stay ragdolls until
 * they rest, then lie still (no simulation) and sink into the street when the simulation removes them.
 *
 * A pool caps live ragdolls (RAG_MAX); a fall with no free slot plays the keyed knockdown clip as before. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "cartwheel/ik.h"
#include "cartwheel/physics3d.h"
#include "cartwheel/ragdoll.h"
#include "cartwheel/profile.h"
#include "cartwheel/debug.h"
#include "cartwheel/math.h"

static inline t3_vec3 vadd3(t3_vec3 a, t3_vec3 b) { return (t3_vec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }

#define RAG_MAX 12
#define RIGS_MAX 96

/* sim.c: the rig pool */
int rig_id_of(const ent *e);
cw_node rig_model(int rid);
cw_node rig_root(int rid);
void rig_release(int rid);
float ent_render_yaw_sim(const ent *e);
void char_play_force(ent *e, const char *clip, float blend);

/* the skeleton the generator writes (tools/make-characters.mjs): 14 parts. Radii are for a 1.0 scale body. */
static const cw_ragdoll_part PARTS[] = {
  { .bone = "Hips", .tip = "Spine", .radius = 0.14f, .density_scale = 1.2f },
  { .bone = "Spine", .tip = "Chest", .radius = 0.13f, .joint = CW_RAGDOLL_BALL, .swing = 0.45f, .lower = -0.35f, .upper = 0.35f },
  { .bone = "Chest", .tip = "Neck", .radius = 0.15f, .joint = CW_RAGDOLL_BALL, .swing = 0.35f, .lower = -0.3f, .upper = 0.3f },
  { .bone = "Head", .tip_offset = { 0, 0.24f, 0.02f }, .radius = 0.105f, .joint = CW_RAGDOLL_BALL, .swing = 0.75f, .lower = -0.7f, .upper = 0.7f },
  { .bone = "UpperArmL", .tip = "LowerArmL", .radius = 0.055f, .joint = CW_RAGDOLL_BALL, .swing = 1.7f, .lower = -0.7f, .upper = 0.7f },
  { .bone = "LowerArmL", .tip = "HandL", .radius = 0.048f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -2.4f, .upper = 0.05f },
  { .bone = "UpperArmR", .tip = "LowerArmR", .radius = 0.055f, .joint = CW_RAGDOLL_BALL, .swing = 1.7f, .lower = -0.7f, .upper = 0.7f },
  { .bone = "LowerArmR", .tip = "HandR", .radius = 0.048f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -2.4f, .upper = 0.05f },
  { .bone = "UpperLegL", .tip = "LowerLegL", .radius = 0.085f, .joint = CW_RAGDOLL_BALL, .swing = 1.25f, .lower = -0.45f, .upper = 0.45f },
  { .bone = "LowerLegL", .tip = "FootL", .radius = 0.065f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -0.05f, .upper = 2.4f },
  { .bone = "FootL", .tip_offset = { 0, -0.06f, 0.15f }, .radius = 0.045f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -0.6f, .upper = 0.7f },
  { .bone = "UpperLegR", .tip = "LowerLegR", .radius = 0.085f, .joint = CW_RAGDOLL_BALL, .swing = 1.25f, .lower = -0.45f, .upper = 0.45f },
  { .bone = "LowerLegR", .tip = "FootR", .radius = 0.065f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -0.05f, .upper = 2.4f },
  { .bone = "FootR", .tip_offset = { 0, -0.06f, 0.15f }, .radius = 0.045f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -0.6f, .upper = 0.7f },
};
#define NPARTS ((int)(sizeof PARTS / sizeof *PARTS))
#define P_HIPS 0
#define P_CHEST 2

enum { M_NONE, M_LIMP, M_GETUP, M_STILL, M_SINK };
typedef struct rstate {
  bool bound;
  cw_pose pose, still;   /* the rig's skeleton; a dead body's last pose */
  cw_ragdoll *rd;
  int mode, t, ent;      /* ent: index (MAXP + e) of the entity it shows, -1 for a sinking corpse */
  float yaw;             /* the model's yaw while it gets up (turned to lie the way the ragdoll lies) */
  t3_vec3 off;           /* render offset of the model root from the simulated position */
  t3_vec3 sink_at;       /* a corpse: where it lies */
} rstate;
static rstate RS[RIGS_MAX];
static cw_world3d world;
static cw_body3d props_body[MAXPROP];
static int live, live_max, made, missed, getups_prone, getups_up;
static float max_knee; /* the largest knee angle any ragdoll showed (limit probe, radians) */
static uint32_t rag_hash;
static double us_sum; static int us_n; static float us_last, us_peak;
static bstate last_st[MAXP + MAXE];

static ent *ent_at(int i) { return i < MAXP ? &W.p[i] : &W.e[i - MAXP]; }
static float body_scale(int def) {
  static const float S[D_COUNT] = { 1.06f, 0.97f, 1.0f, 1.02f, 0.97f, 0.95f, 1.0f, 1.24f, 1.0f, 1.08f, 1.34f, 1.2f, 1.14f, 1.62f, 1.12f, 1.05f, 1.0f };
  return S[def];
}

void rag_debug_fields(void) {
  cw_debug_i32("rag.live", &live);
  cw_debug_i32("rag.live_max", &live_max);
  cw_debug_i32("rag.made", &made);
  cw_debug_i32("rag.missed", &missed);
  cw_debug_i32("rag.getup_prone", &getups_prone);
  cw_debug_i32("rag.getup_up", &getups_up);
  cw_debug_f32("rag.max_knee", &max_knee);
  cw_debug_field("rag.hash", &rag_hash, CW_DBG_U32, 1);
  cw_debug_f32("rag.us", &us_last); /* host clock: never compared */
}

/* the ragdoll world for a stage: the street (top at y 0), the kerb and pavement behind the far lane (0.17 high from
   z -3.4 back), the building fronts, and a box per breakable prop that follows it */
void rag_level_build(void) {
  if (world.id) cw_world3d_destroy(world);
  cw_world3d_def wd = cw_world3d_default_def();
  wd.gravity = (t3_vec3){ 0, -14, 0 }; /* a little heavier than life: bodies hit the street with weight */
  wd.workers = 2;
  wd.substeps = 4;
  world = cw_world3d_create(&wd);
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  cw_body3d ground = cw_body3d_create(world, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.material.friction = 0.8f;
  cw_shape3d_box(ground, &sd, (t3_vec3){ 200, 0.5f, 20 }, cw_xform3_at(60, -0.5f, 0));
  cw_shape3d_box(ground, &sd, (t3_vec3){ 200, 0.085f, 1.4f }, cw_xform3_at(60, 0.085f, -3.4f - 1.4f));
  cw_shape3d_box(ground, &sd, (t3_vec3){ 200, 4, 0.5f }, cw_xform3_at(60, 4, -6.2f - 0.5f));
  memset(props_body, 0, sizeof props_body);
  memset(last_st, 0, sizeof last_st);
  for (int i = 0; i < RIGS_MAX; i++) {
    if (RS[i].mode == M_SINK && cw_node_valid(rig_root(i))) rig_release(i);
    if (RS[i].rd) cw_ragdoll_destroy(RS[i].rd);
    RS[i].rd = NULL;
    RS[i].mode = M_NONE;
  }
  live = 0;
}
/* a new scene freed every rig's nodes (sim.c chars_reset): forget their poses */
void rag_reset(void) {
  for (int i = 0; i < RIGS_MAX; i++) {
    if (RS[i].rd) cw_ragdoll_destroy(RS[i].rd);
    cw_pose_free(&RS[i].pose);
    cw_pose_free(&RS[i].still);
    memset(&RS[i], 0, sizeof RS[i]);
  }
  live = 0;
}

static t3_mat4 model_to_world(t3_vec3 at, float yaw) {
  t3_mat4 m;
  float s = sinf(yaw * 0.5f), c = cosf(yaw * 0.5f);
  t3_mat4_compose(&m, at, (t3_quat){ 0, s, 0, c }, (t3_vec3){ 1, 1, 1 });
  return m;
}
static bool bind(int rid) {
  rstate *s = &RS[rid];
  if (s->bound) return true;
  t3_object *o = cw_node3d_object(rig_model(rid));
  if (!o || !cw_pose_bind(&s->pose, o)) return false;
  return s->bound = true;
}
static void end_ragdoll(rstate *s) {
  if (s->rd) { cw_ragdoll_destroy(s->rd); s->rd = NULL; live--; }
  /* (every clip that follows a fall keys all the part bones: get-ups, the jump in, idle: nothing to reset) */
}

/* the simulated body in the world: root position and the yaw the model is drawn at */
static t3_vec3 sim_pos(const ent *e) { return (t3_vec3){ e->x, e->y, e->z }; }

static void start(int i, ent *e, int rid) {
  rstate *s = &RS[rid];
  if (s->rd) { cw_ragdoll_destroy(s->rd); s->rd = NULL; live--; }
  if (live >= RAG_MAX) { missed++; s->mode = M_NONE; return; }
  if (!bind(rid)) return;
  cw_pose_capture(&s->pose);
  float yaw = s->mode == M_GETUP ? s->yaw : ent_render_yaw_sim(e); /* hit again while getting up: from the pose shown */
  t3_mat4 m = model_to_world(vadd3(sim_pos(e), s->off), yaw);
  if (e->pitch > 0) { /* thrown from the hoist: the body starts tipped flat, as it was drawn */
    t3_quat qy = { 0, sinf(yaw * 0.5f), 0, cosf(yaw * 0.5f) }, qp = { sinf(e->pitch * 0.5f), 0, 0, cosf(e->pitch * 0.5f) };
    t3_quat q = { qy.w * qp.x + qy.x * qp.w + qy.y * qp.z - qy.z * qp.y, qy.w * qp.y - qy.x * qp.z + qy.y * qp.w + qy.z * qp.x,
                  qy.w * qp.z + qy.x * qp.y - qy.y * qp.x + qy.z * qp.w, qy.w * qp.w - qy.x * qp.x - qy.y * qp.y - qy.z * qp.z };
    t3_mat4_compose(&m, vadd3(sim_pos(e), s->off), q, (t3_vec3){ 1, 1, 1 });
    e->pitch = 0;
  }
  cw_ragdoll_part parts[NPARTS];
  float k = body_scale(e->def);
  for (int p = 0; p < NPARTS; p++) { parts[p] = PARTS[p]; parts[p].radius *= k; }
  cw_ragdoll_def def = cw_ragdoll_default_def(parts, NPARTS);
  def.joint_friction = 3.0f * k * k;
  def.angular_damping = 0.8f;
  s->rd = cw_ragdoll_create(world, &def, &s->pose, &m);
  if (!s->rd) { missed++; return; }
  live++, made++;
  if (live > live_max) live_max = live;
  /* the blow carries the whole body (the simulation's own launch velocity) and kicks the chest a little harder; the
     throw spins the body over */
  float dir = e->vx >= 0 ? 1.0f : -1.0f;
  t3_vec3 v = { e->vx * 0.9f, e->vy * 0.55f, 0 };
  t3_vec3 spin = { 0, 0, -dir * (e->st == S_THROWN ? 6.0f : 2.2f) };
  cw_ragdoll_set_velocity(s->rd, v, spin);
  cw_xform3 chest = cw_ragdoll_part_transform(s->rd, P_CHEST);
  cw_ragdoll_apply_impulse(s->rd, P_CHEST, (t3_vec3){ dir * 18.0f * k, 4.0f * k, 0 }, chest.p);
  s->mode = M_LIMP, s->t = 0, s->ent = i;
}

/* the horizontal spring that keeps a limp body near its simulated position (a throw: in all three axes) */
static void tether(rstate *s, const ent *e) {
  cw_body3d hb = cw_ragdoll_body(s->rd, P_HIPS);
  cw_xform3 x = cw_body3d_transform(hb);
  t3_vec3 v = cw_body3d_velocity(hb);
  float mass = 0;
  for (int p = 0; p < NPARTS; p++) mass += cw_body3d_mass(cw_ragdoll_body(s->rd, p));
  bool flying = e->st == S_THROWN;
  float w = flying ? 14.0f : 5.0f, z = flying ? 0.9f : 1.0f;
  t3_vec3 at = sim_pos(e);
  t3_vec3 d = { at.x - x.p.x, flying ? at.y + 0.7f - x.p.y : 0, at.z - x.p.z };
  t3_vec3 f = { mass * (w * w * d.x - 2 * z * w * v.x), flying ? mass * (w * w * d.y - 2 * z * w * v.y) : 0, mass * (w * w * d.z - 2 * z * w * v.z) };
  cw_ragdoll_apply_force(s->rd, f);
}

/* the props: a kinematic box per standing prop (they move only when hit) */
static void sync_props(void) {
  for (int i = 0; i < MAXPROP; i++) {
    const prop_t *p = &W.prop[i];
    if (!p->used) {
      if (props_body[i].id) { cw_body3d_destroy(props_body[i]); props_body[i] = (cw_body3d){ 0 }; }
      continue;
    }
    if (!props_body[i].id) {
      cw_body3d_def bd = cw_body3d_default_def();
      bd.type = CW_BODY_KINEMATIC;
      bd.position = (t3_vec3){ p->x, 0, p->z };
      props_body[i] = cw_body3d_create(world, &bd);
      cw_shape3d_def sd = cw_shape3d_default_def();
      float h = p->kind == 1 ? 0.45f : p->kind == 2 ? 0.55f : 0.4f; /* crate, barrel, bin: half heights */
      cw_shape3d_box(props_body[i], &sd, (t3_vec3){ 0.38f, h, 0.38f }, cw_xform3_at(0, h, 0));
    }
  }
}

void rag_tick(void) {
  if (!world.id) return;
  uint64_t t0 = cw_profile_now_ns();
  sync_props();
  for (int i = 0; i < MAXP + MAXE; i++) {
    ent *e = ent_at(i);
    bstate prev = last_st[i];
    last_st[i] = e->used ? e->st : S_IDLE;
    if (!e->used || !e->root.id) continue;
    int rid = rig_id_of(e);
    if (rid < 0) continue;
    rstate *s = &RS[rid];
    bool down = e->st == S_FALL || e->st == S_THROWN || e->st == S_DOWN || e->st == S_DEAD;
    bool fell = (e->st == S_FALL || e->st == S_THROWN) && !(prev == S_FALL || prev == S_THROWN);
    if (fell && !(s->mode == M_LIMP && prev == S_THROWN && e->st == S_FALL)) start(i, e, rid); /* a thrown body that lands stays the same ragdoll */
    if (s->mode == M_LIMP && e->st == S_GETUP && s->rd) {
      /* face up or down from the chest; the model turns so the clip's lying body points the way the ragdoll lies */
      t3_vec3 fwd = cw_ragdoll_axis(s->rd, P_CHEST, (t3_vec3){ 0, 0, 1 });
      bool prone = fwd.y < 0;
      cw_xform3 h = cw_ragdoll_part_transform(s->rd, P_HIPS), c = cw_ragdoll_part_transform(s->rd, P_CHEST);
      float hx = c.p.x - h.p.x, hz = c.p.z - h.p.z; /* hips to chest: the way the head points */
      /* face up, the get-up clip's head points along the model's -z; face down along +z */
      s->yaw = prone ? cw_atan2(hx, hz) : cw_atan2(-hx, -hz);
      s->off = (t3_vec3){ h.p.x - e->x, 0, h.p.z - e->z };
      char_play_force(e, prone ? "getup_prone" : "getup", 0.0f);
      if (prone) getups_prone++; else getups_up++;
      s->mode = M_GETUP, s->t = 0;
    }
    if (s->mode != M_NONE && s->mode != M_SINK && !down && e->st != S_GETUP) { /* back on its feet some other way (a respawn) */
      end_ragdoll(s);
      s->mode = M_NONE, s->off = (t3_vec3){ 0, 0, 0 };
    }
    if (s->mode == M_LIMP && e->st == S_DEAD && s->rd && (cw_ragdoll_resting(s->rd, 0.12f) || ++s->t > 240)) {
      /* the dead lie still once they settle: keep the pose, free the ragdoll */
      cw_pose_capture(&s->pose);
      t3_mat4 m = model_to_world(vadd3(sim_pos(e), s->off), ent_render_yaw_sim(e));
      cw_ragdoll_write_pose(s->rd, &s->pose, &m);
      if (s->still.count != s->pose.count) { cw_pose_free(&s->still); cw_pose_init(&s->still, s->pose.count); }
      cw_pose_copy_locals(&s->still, &s->pose);
      cw_ragdoll_destroy(s->rd); s->rd = NULL; live--;
      s->mode = M_STILL;
    }
  }
  if (W.hitstop <= 0) cw_world3d_step(world, 1.0f / TPS); /* frozen frames freeze the bodies too */
  uint32_t h = 2166136261u;
  for (int i = 0; i < MAXP + MAXE; i++) {
    ent *e = ent_at(i);
    if (!e->used || !e->root.id) continue;
    int rid = rig_id_of(e);
    if (rid < 0) continue;
    rstate *s = &RS[rid];
    if (s->mode == M_NONE || s->mode == M_SINK || !s->bound) continue;
    if (s->rd && s->mode == M_LIMP) tether(s, e);
    float yaw = s->mode == M_GETUP ? s->yaw : ent_render_yaw_sim(e);
    t3_mat4 m = model_to_world(vadd3(sim_pos(e), s->off), yaw);
    cw_pose_capture(&s->pose);
    if (s->mode == M_STILL) { cw_pose_copy_locals(&s->pose, &s->still); cw_pose_update_model(&s->pose); }
    else if (s->rd) {
      float w = 1;
      if (s->mode == M_GETUP) {
        s->t++;
        float u = (float)s->t / 20.0f; /* a third of a second from the ragdoll into the clip */
        w = u >= 1 ? 0 : 1 - u * u * (3 - 2 * u);
      }
      cw_ragdoll_blend_pose(s->rd, &s->pose, &m, w);
      h = cw_ragdoll_hash(s->rd, h);
      for (int p = 0; p < NPARTS; p++) if (PARTS[p].joint == CW_RAGDOLL_HINGE && PARTS[p].upper > 2) { float a = cw_ragdoll_joint_angle(s->rd, p); if (a > max_knee) max_knee = a; }
      if (s->mode == M_GETUP && w <= 0) { cw_ragdoll_destroy(s->rd); s->rd = NULL; live--; }
    }
    if (s->mode == M_GETUP && !s->rd) {
      /* the rest of the get-up: turn back to the fight and slide onto the simulated spot while standing up */
      s->t++;
      float k = s->t < 26 ? 0 : s->t > 50 ? 1 : (float)(s->t - 26) / 24.0f;
      k = k * k * (3 - 2 * k);
      float target = ent_render_yaw_sim(e), dy = target - s->yaw;
      while (dy > 3.14159265f) dy -= 6.2831853f;
      while (dy < -3.14159265f) dy += 6.2831853f;
      s->yaw += dy * k;
      s->off = (t3_vec3){ s->off.x * (1 - k), 0, s->off.z * (1 - k) };
      if (s->t > 50 || e->st != S_GETUP) { s->mode = M_NONE, s->off = (t3_vec3){ 0, 0, 0 }; end_ragdoll(s); continue; }
    }
    cw_pose_flush(&s->pose);
  }
  if (h != 2166136261u) rag_hash = cw_hash32(&h, sizeof h, rag_hash); /* every tick with a ragdoll, folded: a cross-host check of the presentation */
  /* corpses the simulation removed: lie still, sink into the street, then the rig goes back to the pool */
  for (int rid = 0; rid < RIGS_MAX; rid++) {
    rstate *s = &RS[rid];
    if (s->mode != M_SINK) continue;
    s->t++;
    float k = s->t < 60 ? 0 : (float)(s->t - 60) / 70.0f;
    cw_node3d_set_position(rig_root(rid), (t3_vec3){ s->sink_at.x, s->sink_at.y - 0.45f * k, s->sink_at.z });
    if (s->bound && s->still.count) { cw_pose_copy_locals(&s->pose, &s->still); cw_pose_flush(&s->pose); }
    if (s->t > 130) { s->mode = M_NONE; s->off = (t3_vec3){ 0, 0, 0 }; end_ragdoll(s); rig_release(rid); }
  }
  us_last = (float)(cw_profile_now_ns() - t0) * 1e-3f;
  if (live > 0) { us_sum += us_last, us_n++; if (us_last > us_peak) us_peak = us_last; } /* the cost while any ragdoll is live */
}

/* sim.c char_free: a dead body that lies as a ragdoll stays on the street a little longer and sinks; true when this
   file keeps the rig (it calls rig_release when done) */
bool rag_take_corpse(int rid, const ent *e) {
  if (rid < 0 || rid >= RIGS_MAX) return false;
  rstate *s = &RS[rid];
  if (!(s->mode == M_STILL || s->mode == M_LIMP) || e->st != S_DEAD) {
    if (s->mode != M_NONE) { end_ragdoll(s); s->mode = M_NONE; }
    s->off = (t3_vec3){ 0, 0, 0 };
    return false;
  }
  if (s->rd) { /* still moving: keep its pose as it is now */
    t3_mat4 m = model_to_world(vadd3(sim_pos(e), s->off), ent_render_yaw_sim(e));
    cw_pose_capture(&s->pose);
    cw_ragdoll_write_pose(s->rd, &s->pose, &m);
    if (s->still.count != s->pose.count) { cw_pose_free(&s->still); cw_pose_init(&s->still, s->pose.count); }
    cw_pose_copy_locals(&s->still, &s->pose);
    cw_ragdoll_destroy(s->rd); s->rd = NULL; live--;
  }
  s->mode = M_SINK, s->t = 0, s->ent = -1;
  s->sink_at = (t3_vec3){ e->x + s->off.x, e->y, e->z + s->off.z };
  return true;
}

/* char_sync: the model's yaw and root offset while a ragdoll or a get-up is shown */
bool rag_render(const ent *e, float *yaw, t3_vec3 *off) {
  int rid = rig_id_of(e);
  if (rid < 0) return false;
  const rstate *s = &RS[rid];
  if (s->mode == M_NONE || s->mode == M_SINK) return false;
  *yaw = s->mode == M_GETUP ? s->yaw : ent_render_yaw_sim(e);
  *off = s->off;
  return true;
}
float rag_mean_us(void) { return us_n ? (float)(us_sum / us_n) : 0; }
/* the soak's status line: live ragdolls now and at most, made, missed for want of a slot, get-ups each way, cost */
const char *rag_status(void) {
  static char b[160];
  snprintf(b, sizeof b, "rag_live=%d rag_max=%d rag_made=%d rag_missed=%d rag_prone=%d rag_up=%d rag_us=%d rag_knee_mrad=%d", live, live_max, made, missed,
           getups_prone, getups_up, (int)rag_mean_us(), (int)(max_knee * 1000));
  return b;
}
