/* arenafps: the fighters' bodies (models/soldier_*.glb, tools/blender/soldier.py): the soldier on the full humanoid
 * skeleton of the motion libraries its clips come from (anim/ual.glb, tools/blender/anims.py: Quaternius' Universal
 * Animation Library 1 and 2, CC0), retargeted onto it when the cart is packed (models/soldier_*.glb.cwimport).
 *
 * Each body is an instance of one loaded model per team colour (geometry shared, one material cloned per body for its
 * rim), with an AnimationTree stepped here once a tick after the rules:
 *   legs   a state machine of two 2D blend spaces of locomotion over the body's own velocity (x its left, y its front),
 *          each clip at the ground speed its planted feet really travel: the front half (stand, walk and run forward,
 *          45 degrees off and sideways, the hips turned toward the way it goes and the chest kept on the aim) and the
 *          back half (the cycles run backwards, behind, 45 degrees off behind and sideways facing away), and the air;
 *          the cycle runs faster past the run's own speed (no sliding feet)
 *   arms   both hands on the rifle (the hold), blended toward the recoil by a spring per shot, over the legs from the
 *          shoulders out: the soldier runs and shoots at once
 *   shots  one-shots timed to the rules: the jump and the landing, the reload (a weapon change), the rifle jab (the
 *          bash), the grenade throw (left handed: the right hand keeps the rifle), a hit from the front, behind, either
 *          side or to the head, and a fall (only when no ragdoll is free)
 * and skeleton modifiers after it: the spine, the chest and the head turned to the aim (the chest so that the rifle's
 * bore lies on the aim line), the feet placed on the ground (stairs, terraces, ramps). The dead fall as ragdolls
 * (cartwheel/ragdoll.h) on the level's own collision, pushed by the blow that downed them.
 *
 * Animation detail falls with distance from the nearest player's eye: the tree is evaluated every tick near by,
 * then every 2nd, 4th and 8th (cw_anim_lod_divisor); the feet are placed only near by, the aim only within reach of
 * sight; far bodies skip the fingers (their tree is masked to the 22 main bones), cast no shadow and draw a coarser
 * mesh (the packer's LODs). A body no view can see (the one person's own, in one view) is not animated at all. */
#include "game.h"
#include "cartwheel/anim.h"
#include "cartwheel/ik.h"
#include "cartwheel/ragdoll.h"
#include "cartwheel/resource.h"
#include "cartwheel/retarget.h"
#include "soldier.h"
#if __has_include("cartwheel/rim_light.h")
#include "cartwheel/rim_light.h"
#endif

#define RAG_LAYER 2
#define RAG_MAX 6
/* the bore's way when the chest is at rest, in the model (tools/blender/anims.py prints it as "hold" and writes
 * out/hold.json barrel_rest_gltf): the chest's look-at turns this onto the aim */
static const t3_vec3 BARREL_REST = { 0.22965f, 0.24730f, 0.94133f };
static const float ANKLE = 0.099f; /* the ankle (foot bone) above the sole at rest, the soldier's size */

/* the locomotion clips: name, and which half of the circle */
static const char *const LOCO_F[] = { "walk_f", "walk_fl", "walk_fr", "walk_l", "walk_r", "run_f", "run_fl", "run_fr", "run_l", "run_r" };
static const char *const LOCO_B[] = { "walk_b", "walk_bl", "walk_br", "walk_bl90", "walk_br90", "run_b", "run_bl", "run_br", "run_bl90", "run_br90" };
#define NLOCO 10
static const char *const SHOTS[] = { "jump", "land", "reload", "bash", "throw", "hit_f", "hit_b", "hit_l", "hit_r", "hit_head", "die" };
enum { SH_JUMP, SH_LAND, SH_RELOAD, SH_BASH, SH_THROW, SH_HIT_F, SH_HIT_B, SH_HIT_L, SH_HIT_R, SH_HIT_HEAD, SH_DIE, NSHOT };
static const char *const LOOPED[] = { "idle", "air", "hold", "kick", "dead" };

static const char *const MAIN_BONES[] = { "root", "pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "Head", "clavicle_l", "upperarm_l",
                                          "lowerarm_l", "hand_l", "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r", "thigh_l", "calf_l",
                                          "foot_l", "ball_l", "thigh_r", "calf_r", "foot_r", "ball_r" };
#define NMAIN ((int)(sizeof MAIN_BONES / sizeof *MAIN_BONES))

static const cw_ragdoll_part PARTS[] = {
  { .bone = "pelvis", .tip = "spine_02", .radius = 0.14f, .density_scale = 1.2f },
  { .bone = "spine_02", .tip = "spine_03", .radius = 0.13f, .joint = CW_RAGDOLL_BALL, .swing = 0.45f, .lower = -0.35f, .upper = 0.35f },
  { .bone = "spine_03", .tip = "neck_01", .radius = 0.15f, .joint = CW_RAGDOLL_BALL, .swing = 0.35f, .lower = -0.3f, .upper = 0.3f },
  { .bone = "Head", .tip_offset = { 0, 0.2f, 0.02f }, .radius = 0.1f, .joint = CW_RAGDOLL_BALL, .swing = 0.7f, .lower = -0.6f, .upper = 0.6f },
  { .bone = "upperarm_l", .tip = "lowerarm_l", .radius = 0.055f, .joint = CW_RAGDOLL_BALL, .swing = 1.7f, .lower = -0.7f, .upper = 0.7f },
  { .bone = "lowerarm_l", .tip = "hand_l", .radius = 0.05f, .joint = CW_RAGDOLL_BALL, .swing = 1.2f, .lower = -0.3f, .upper = 0.3f },
  { .bone = "upperarm_r", .tip = "lowerarm_r", .radius = 0.055f, .joint = CW_RAGDOLL_BALL, .swing = 1.7f, .lower = -0.7f, .upper = 0.7f },
  { .bone = "lowerarm_r", .tip = "hand_r", .radius = 0.05f, .joint = CW_RAGDOLL_BALL, .swing = 1.2f, .lower = -0.3f, .upper = 0.3f },
  { .bone = "thigh_l", .tip = "calf_l", .radius = 0.085f, .joint = CW_RAGDOLL_BALL, .swing = 1.25f, .lower = -0.45f, .upper = 0.45f },
  { .bone = "calf_l", .tip = "foot_l", .radius = 0.065f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -0.05f, .upper = 2.4f },
  { .bone = "foot_l", .tip_offset = { 0, -0.06f, 0.15f }, .radius = 0.05f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -0.6f, .upper = 0.7f },
  { .bone = "thigh_r", .tip = "calf_r", .radius = 0.085f, .joint = CW_RAGDOLL_BALL, .swing = 1.25f, .lower = -0.45f, .upper = 0.45f },
  { .bone = "calf_r", .tip = "foot_r", .radius = 0.065f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -0.05f, .upper = 2.4f },
  { .bone = "foot_r", .tip_offset = { 0, -0.06f, 0.15f }, .radius = 0.05f, .joint = CW_RAGDOLL_HINGE, .axis = { 1, 0, 0 }, .lower = -0.6f, .upper = 0.7f },
};
#define NPARTS ((int)(sizeof PARTS / sizeof *PARTS))

/* one model per team colour, loaded once; its clips converted once and shared by every body of that colour */
static struct {
  cw_res res;
  int nclips;
  cw_anim clip[64];
  char name[64][16];
  t3_vec2 gv_f[NLOCO], gv_b[NLOCO]; /* each locomotion clip's travel (x left, y front), m/s */
  float run_speed;
  cw_anim lean[4];
  bool ready;
} MODEL[2];
/* the leans, one clip a way (an additive's weight is 0 to 1): into a turn to the left or the right (the sideways pull
 * of speed times turn rate), forward into a start and back into a stop; each spread over the pelvis and the spine,
 * keyed in each bone's own frame from a turn about a model axis (the model faces +z, its left is +x) */
static const struct { const char *n; t3_vec3 ax; float deg; } LEANS[4] = { { "lean_l", { 0, 0, -1 }, 11 }, { "lean_r", { 0, 0, 1 }, 11 },
                                                                          { "lean_fw", { 1, 0, 0 }, 9 }, { "lean_bk", { -1, 0, 0 }, 6 } };
static cw_anim lean_clip(const cw_pose *pz, const char *name, t3_vec3 axis, float deg) {
  static const char *const LB[] = { "pelvis", "spine_01", "spine_02", "spine_03" };
  static const float SHARE[] = { 0.2f, 0.25f, 0.3f, 0.25f };
  cw_anim a = cw_anim_new(name, 1, CW_LOOP_LINEAR);
  cw_anim_set_stored(a, false);
  for (int k = 0; k < 4; k++) {
    int bi = cw_pose_find(pz, LB[k]);
    if (bi < 0) continue;
    /* the axis in the bone's own frame at rest, and the key: its rest rotation, then the turn */
    t3_quat m = pz->mrot[bi], mi = { -m.x, -m.y, -m.z, m.w };
    t3_vec3 u = { mi.x, mi.y, mi.z }, t = v3scale(v3cross(u, axis), 2);
    t3_vec3 la = v3norm(v3add(v3add(axis, v3scale(t, mi.w)), v3cross(u, t)));
    float sn, cs;
    cw_sincos(deg * SHARE[k] * CW_PI / 360.0f, &sn, &cs);
    t3_quat d = { la.x * sn, la.y * sn, la.z * sn, cs }, r = pz->rest_rot[bi];
    t3_quat q = { r.w * d.x + r.x * d.w + r.y * d.z - r.z * d.y, r.w * d.y - r.x * d.z + r.y * d.w + r.z * d.x,
                  r.w * d.z + r.x * d.y - r.y * d.x + r.z * d.w, r.w * d.w - r.x * d.x - r.y * d.y - r.z * d.z };
    char path[48];
    snprintf(path, sizeof path, "Model@%s:quaternion", LB[k]);
    int tr = cw_anim_add_value_track(a, path, CW_INTERP_LINEAR);
    cw_value v = { CW_T_QUAT };
    v.v[0] = q.x, v.v[1] = q.y, v.v[2] = q.z, v.v[3] = q.w;
    cw_anim_add_key(a, tr, 0, v);
  }
  return a;
}

typedef struct body {
  bool made;
  int variant;
  t3_object *inst;
  t3_material *mat; /* this body's own copy of the soldier's material (its rim) */
  cw_node model, anim, tree, feet, look[4];
  cw_blend_id shot[NSHOT], legs, loco[2][NLOCO], idle[2], top;
  float bx, by, rate, kick, look_w, feet_w, back, lean_s, lean_f, last_yaw, last_speed;
  int air_t, last_weapon, act_t, act, last_mhold, last_hit_t, last_throw_t, last_bash_t;
  bool was_alive, was_floor, lying;
  int lod, divisor, mesh_lod;
  bool shadow, masked, posed;
  cw_pose pose;
  bool bound;
  cw_ragdoll *rd;
  int rd_t;
  bool still;
  float acc; /* time not yet given to the tree (it is advanced every divisor-th tick, with all of it) */
} body;
#define CROWD_MAX 48
static body B[MAXF + CROWD_MAX]; /* the fighters' bodies, then the bench crowd's */
static int crowd_n, crowd_t;
static int rag_live;
soldier_stats SOLDIER_ST;

static t3_mat4 model_to_world(t3_vec3 at, float yaw) {
  t3_mat4 m;
  float s, c;
  cw_sincos((yaw + CW_PI) * 0.5f, &s, &c); /* the model faces +Z; the body faces its yaw (-Z at 0) */
  t3_mat4_compose(&m, at, (t3_quat){ 0, s, 0, c }, (t3_vec3){ 1, 1, 1 });
  return m;
}

static int clip_index(int v, const char *n) {
  for (int k = 0; k < MODEL[v].nclips; k++) if (!strcmp(MODEL[v].name[k], n)) return k;
  return -1;
}

/* the pose back at its rest (the transforms it was bound at) */
static void pose_rest(cw_pose *pz) {
  for (int i = 0; i < pz->count; i++) pz->pos[i] = pz->rest_pos[i], pz->rot[i] = pz->rest_rot[i], pz->scl[i] = pz->rest_scl[i];
}

/* a cycle's travel over the ground, in the model (x the soldier's left, y its front), m/s: the clip sampled at 60 Hz on
 * the rest pose, each foot's ball (the toe joint) in contact over the lowest sixth of its cycle, its velocity there,
 * the median over the contact ticks, reversed (the body moves against its planted foot). (cw_anim_ground_velocity reads
 * the stance from the ankle, which a sprint lifts through most of its three-key stance: it measured 2.1 m/s here for a
 * sprint whose planted toes travel 8.4) */
static t3_vec2 clip_travel(cw_anim a, cw_pose *pz) {
  int nt = cw_anim_track_count(a), *bone = calloc((size_t)nt, sizeof(int)), lb = cw_pose_find(pz, "ball_l"), rb = cw_pose_find(pz, "ball_r");
  bool *isrot = calloc((size_t)nt, sizeof(bool));
  for (int k = 0; k < nt; k++) {
    const char *path = cw_anim_track_path(a, k), *at = path ? strchr(path, '@') : NULL, *colon = at ? strchr(at, ':') : NULL;
    bone[k] = -1;
    if (!at || !colon) continue;
    char nm[48];
    snprintf(nm, sizeof nm, "%.*s", (int)(colon - at - 1), at + 1);
    bone[k] = cw_pose_find(pz, nm);
    isrot[k] = !strcmp(colon + 1, "quaternion");
    if (!isrot[k] && strcmp(colon + 1, "position")) bone[k] = -1;
  }
  float len = cw_anim_length(a);
  int n = (int)(len * 60.0f + 0.5f);
  if (n < 4 || lb < 0 || rb < 0) { free(bone), free(isrot); return (t3_vec2){ 0, 0 }; }
  t3_vec3 *P = calloc((size_t)(n + 1) * 2, sizeof(t3_vec3));
  for (int i = 0; i <= n; i++) {
    pose_rest(pz);
    float t = len * (float)i / (float)n;
    for (int k = 0; k < nt; k++) {
      cw_value v;
      if (bone[k] < 0 || !cw_anim_sample(a, k, t, &v)) continue;
      if (isrot[k]) pz->rot[bone[k]] = (t3_quat){ v.v[0], v.v[1], v.v[2], v.v[3] };
      else pz->pos[bone[k]] = (t3_vec3){ v.v[0], v.v[1], v.v[2] };
    }
    cw_pose_update_model(pz);
    P[i * 2] = cw_pose_model_position(pz, lb), P[i * 2 + 1] = cw_pose_model_position(pz, rb);
  }
  float vx[512], vz[512];
  int m = 0;
  /* each foot's stance: its lowest sixth of the cycle (a sprint's lasts three keys; a walk's half the cycle) */
  int want = CW_MAX(3, n / 6);
  for (int f = 0; f < 2; f++) {
    bool *used = calloc((size_t)n + 1, sizeof(bool));
    for (int q = 0; q < want && m < 512; q++) {
      int best = -1;
      for (int i = 1; i <= n; i++) if (!used[i] && (best < 0 || P[i * 2 + f].y < P[best * 2 + f].y)) best = i;
      if (best < 0) break;
      used[best] = true;
      vx[m] = -(P[best * 2 + f].x - P[(best - 1) * 2 + f].x) * 60.0f, vz[m] = -(P[best * 2 + f].z - P[(best - 1) * 2 + f].z) * 60.0f, m++;
    }
    free(used);
  }
  free(P), free(bone), free(isrot);
  if (!m) return (t3_vec2){ 0, 0 };
  /* medians */
  for (int i = 1; i < m; i++) for (int j = i; j > 0 && vx[j] < vx[j - 1]; j--) { float t = vx[j]; vx[j] = vx[j - 1], vx[j - 1] = t; }
  for (int i = 1; i < m; i++) for (int j = i; j > 0 && vz[j] < vz[j - 1]; j--) { float t = vz[j]; vz[j] = vz[j - 1], vz[j - 1] = t; }
  return (t3_vec2){ vx[m / 2], vz[m / 2] };
}

static bool model_load(int v) {
  if (MODEL[v].ready) return true;
  char path[48];
  snprintf(path, sizeof path, "models/soldier_%c.glb", v ? 'b' : 'a');
  if (!MODEL[v].res.id) MODEL[v].res = cw_res_load(path, CW_RES_GLTF);
  const cw_gltf *g = cw_res_gltf(MODEL[v].res);
  if (!g || !g->gltf) { cw_error("arenafps: %s did not load", path); return false; }
  for (int k = 0; k < g->gltf->animation_count && MODEL[v].nclips < 64; k++) {
    const char *nm = g->gltf->animations[k]->name ? g->gltf->animations[k]->name : "";
    cw_anim a = cw_anim_from_t3_clip(g->gltf->animations[k], "Model");
    cw_anim_set_stored(a, false);
    bool loop = !strncmp(nm, "walk_", 5) || !strncmp(nm, "run_", 4);
    for (int i = 0; i < (int)(sizeof LOOPED / sizeof *LOOPED); i++) loop |= !strcmp(nm, LOOPED[i]);
    cw_anim_set_loop(a, loop ? CW_LOOP_LINEAR : CW_LOOP_NONE); /* (glTF clips load looping) */
    int n = MODEL[v].nclips++;
    MODEL[v].clip[n] = a;
    snprintf(MODEL[v].name[n], sizeof MODEL[v].name[n], "%s", nm);
  }
  /* each cycle's travel: the opposite of its planted feet's velocity, in the model (x the soldier's left, z its front) */
  t3_object *probe = cw_gltf_instantiate(MODEL[v].res);
  cw_pose pz = { 0 };
  bool posed = probe && cw_pose_bind(&pz, probe);
  int lf = posed ? cw_pose_find(&pz, "foot_l") : -1, rf = posed ? cw_pose_find(&pz, "foot_r") : -1;
  for (int half = 0; half < 2; half++)
    for (int k = 0; k < NLOCO; k++) {
      int c = clip_index(v, half ? LOCO_B[k] : LOCO_F[k]);
      t3_vec2 t = c >= 0 && lf >= 0 && rf >= 0 ? clip_travel(MODEL[v].clip[c], &pz) : (t3_vec2){ 0, 0 };
      if (t.x * t.x + t.y * t.y < 0.09f) { /* (a clip that measures nothing: its nominal direction and speed) */
        static const float ANG[NLOCO] = { 0, 45, -45, 90, -90, 0, 45, -45, 90, -90 };
        float a = ANG[k] * CW_PI / 180.0f + (half ? CW_PI : 0), sp = k < 5 ? 1.4f : 5.5f;
        if (half && (k % 5) >= 1) a = (k % 5 == 1 ? 135 : k % 5 == 2 ? -135 : k % 5 == 3 ? 90 : -90) * CW_PI / 180.0f;
        t = (t3_vec2){ cw_sin(a) * sp, cw_cos(a) * sp };
      }
      if (half) MODEL[v].gv_b[k] = t; else MODEL[v].gv_f[k] = t;
    }
  if (posed) {
    pose_rest(&pz);
    cw_pose_update_model(&pz);
    for (int k = 0; k < 4; k++) MODEL[v].lean[k] = lean_clip(&pz, LEANS[k].n, LEANS[k].ax, LEANS[k].deg);
  }
  MODEL[v].run_speed = sqrtf(MODEL[v].gv_f[5].x * MODEL[v].gv_f[5].x + MODEL[v].gv_f[5].y * MODEL[v].gv_f[5].y);
  if (MODEL[v].run_speed < 1) MODEL[v].run_speed = 5.5f;
  if (posed) cw_pose_free(&pz);
  if (probe) t3_release(probe);
  MODEL[v].ready = true;
  return true;
}

static void set_cast(t3_object *o, bool on) {
  if (o->type == T3_SKINNED_MESH || o->type == T3_MESH) o->cast_shadow = on, o->receive_shadow = true, o->frustum_culled = false;
  for (int i = 0; i < o->child_count; i++) set_cast(o->children[i], on);
}
/* each body its own copy of the soldier's one material, so its rim (team, spawn guard, power-up) is its own */
static t3_material *own_material(t3_object *o) {
  t3_material *got = NULL;
  if (o->type == T3_SKINNED_MESH || o->type == T3_MESH) {
    t3_mesh *m = (t3_mesh *)o;
    for (int k = 0; k < m->material_count; k++) {
      if (!m->materials[k]) continue;
      t3_material *c = t3_material_clone(m->materials[k]);
      t3_release(m->materials[k]);
      m->materials[k] = c;
      got = c;
    }
  }
  for (int i = 0; i < o->child_count; i++) {
    t3_material *g = own_material(o->children[i]);
    if (g) got = g;
  }
  return got;
}

static cw_blend_id clipnode(cw_node t, const char *n) { return cw_animtree_clip(t, n); }
static void filter(cw_node t, cw_blend_id id, const char *spec) {
  const char *names[24];
  char buf[256];
  snprintf(buf, sizeof buf, "%s", spec);
  int n = 0;
  for (char *p = strtok(buf, "|"); p && n < 24; p = strtok(NULL, "|")) names[n++] = p;
  cw_animtree_set_filter(t, id, names, n);
}

static void tree_make(body *b, int v) {
  b->tree = cw_node_new("AnimationTree", "Tree");
  cw_node_add_child(cw_node_parent(b->anim), b->tree);
  cw_node_set(b->tree, "player", cw_val_string("../Anim"));
  cw_node_set(b->tree, "process", cw_val_enum("MANUAL"));
  cw_node t = b->tree;
  cw_blend_id lf = cw_animtree_blend2d(t, "mx", "my"), lb = cw_animtree_blend2d(t, "mx", "my");
  cw_animtree_blend2d_add(t, lf, b->idle[0] = clipnode(t, "idle"), 0, 0);
  cw_animtree_blend2d_add(t, lb, b->idle[1] = clipnode(t, "idle"), 0, 0);
  for (int k = 0; k < NLOCO; k++) {
    cw_animtree_blend2d_add(t, lf, b->loco[0][k] = clipnode(t, LOCO_F[k]), MODEL[v].gv_f[k].x, MODEL[v].gv_f[k].y);
    cw_animtree_blend2d_add(t, lb, b->loco[1][k] = clipnode(t, LOCO_B[k]), MODEL[v].gv_b[k].x, MODEL[v].gv_b[k].y);
  }
  cw_animtree_set_sync(t, lf, true);
  cw_animtree_set_sync(t, lb, true);
  cw_blend_id sm = cw_animtree_state_machine(t);
  cw_animtree_sm_add_state(t, sm, "front", lf);
  cw_animtree_sm_add_state(t, sm, "back", lb);
  cw_animtree_sm_add_state(t, sm, "air", clipnode(t, "air"));
  cw_transition tr = { .xfade = 0.22f, .advance = CW_ADVANCE_AUTO, .switch_mode = CW_SWITCH_IMMEDIATE };
  tr.condition = "back > 0.5", cw_animtree_sm_add_transition(t, sm, "front", "back", &tr);
  tr.condition = "back < 0.5", cw_animtree_sm_add_transition(t, sm, "back", "front", &tr);
  tr.xfade = 0.15f, tr.condition = "air > 0.5";
  cw_animtree_sm_add_transition(t, sm, "front", "air", &tr);
  cw_animtree_sm_add_transition(t, sm, "back", "air", &tr);
  tr.xfade = 0.1f, tr.condition = "air < 0.5", cw_animtree_sm_add_transition(t, sm, "air", "front", &tr);
  cw_animtree_sm_start(t, sm, "front");
  b->legs = sm;
  cw_blend_id base = cw_animtree_time_scale(t, sm, "rate");
  /* the hold: both hands on the rifle from the shoulders out, toward the recoil by the kick spring */
  cw_blend_id arms = cw_animtree_blend2(t, clipnode(t, "hold"), clipnode(t, "kick"), "kick");
  for (int k = 0; k < 4; k++) if (MODEL[v].lean[k].id) base = cw_animtree_add(t, base, clipnode(t, LEANS[k].n), LEANS[k].n);
  base = cw_animtree_layer(t, base, arms, "armed");
  filter(t, base, "clavicle_l+|clavicle_r+");
  base = cw_animtree_blend2(t, base, clipnode(t, "dead"), "lying");
  static const float IN[NSHOT] = { 0.04f, 0.04f, 0.12f, 0.05f, 0.1f, 0.03f, 0.03f, 0.03f, 0.03f, 0.03f, 0.08f };
  static const float OUT[NSHOT] = { 0.15f, 0.2f, 0.15f, 0.15f, 0.2f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f, 0.05f };
  static const char *const MASK[NSHOT] = {
    NULL, "pelvis|spine_01|thigh_l+|thigh_r+", "clavicle_l+|clavicle_r+", "spine_02|spine_03|clavicle_l+|clavicle_r+", "spine_01+",
    "spine_01|spine_02|spine_03|neck_01|Head", "spine_01|spine_02|spine_03|neck_01|Head", "spine_01|spine_02|spine_03|neck_01|Head",
    "spine_01|spine_02|spine_03|neck_01|Head", "spine_01|spine_02|spine_03|neck_01|Head", NULL };
  for (int k = 0; k < NSHOT; k++) {
    b->shot[k] = -1;
    if (clip_index(v, SHOTS[k]) < 0) continue;
    base = b->shot[k] = cw_animtree_oneshot(t, base, clipnode(t, SHOTS[k]), SHOTS[k], IN[k], OUT[k]);
    if (MASK[k]) filter(t, base, MASK[k]);
  }
  /* the whole graph under one node, so far off a mask can drop the fingers (a mask on a layer or a one-shot holds
   * for its overlay only) */
  b->top = cw_animtree_time_scale(t, base, "all");
  cw_animtree_set_root(t, b->top);
  cw_animtree_set_param(t, "all", 1);
  cw_animtree_set_param(t, "rate", 1);
  cw_animtree_set_param(t, "armed", 1);
}

void soldier_make(fighter *f, int slot, int variant, cw_node world, bool bigheads) {
  body *b = &B[slot];
  soldier_free(slot);
  memset(b, 0, sizeof *b);
  b->variant = variant;
  f->body = cw_node_new("Node3D", "Body");
  cw_node_add_child(world, f->body);
  f->head = NULL;
  if (!model_load(variant)) return;
  t3_object *inst = cw_gltf_instantiate(MODEL[variant].res);
  if (!inst) return;
  b->inst = inst;
  b->mat = own_material(inst);
  set_cast(inst, true);
  b->shadow = true;
  b->model = cw_node_new("Node3D", "Model");
  cw_node_add_child(f->body, b->model);
  cw_node3d_set_object(b->model, inst);
  cw_node3d_set_rotation(b->model, (t3_vec3){ 0, CW_PI, 0 }); /* the model faces +Z */
  b->anim = cw_node_new("AnimationPlayer", "Anim");
  cw_node_add_child(f->body, b->anim);
  for (int k = 0; k < MODEL[variant].nclips; k++) cw_anim_player_add(b->anim, MODEL[variant].name[k], MODEL[variant].clip[k]);
  for (int k = 0; k < 4; k++) if (MODEL[variant].lean[k].id) cw_anim_player_add(b->anim, LEANS[k].n, MODEL[variant].lean[k]);
  tree_make(b, variant);
  f->head = bigheads ? t3_object_get_by_name(inst, "Head") : NULL; /* big heads: drawn only */
  /* the feet on the ground: rays against the level only (never a ragdoll) */
  b->feet = cw_node_new("FootPlacement3D", "Feet");
  cw_node_add_child(b->model, b->feet);
  cw_node_set(b->feet, "legs", cw_val_string("thigh_l,calf_l,foot_l;thigh_r,calf_r,foot_r"));
  cw_node_set(b->feet, "pelvis", cw_val_string("pelvis"));
  cw_node_set(b->feet, "collision_mask", cw_val_int(1));
  cw_node_set(b->feet, "foot_height", cw_val_float(ANKLE));
  cw_node_set(b->feet, "max_step_down", cw_val_float(0.5f));
  cw_node_set(b->feet, "ray_up", cw_val_float(0.6f));
  /* the aim: the spine takes a share of the turn, the chest the rest (its bore onto the aim line), the head looks */
  static const char *const LB[4] = { "spine_01", "spine_02", "spine_03", "Head" };
  for (int k = 0; k < 4; k++) {
    b->look[k] = cw_node_new("LookAtModifier3D", LB[k]);
    cw_node_add_child(b->model, b->look[k]);
    cw_node_set(b->look[k], "bone", cw_val_string(LB[k]));
    cw_node_set(b->look[k], "limit_angle", cw_val_float(k == 2 ? 3.0f : k == 3 ? 1.0f : 1.2f));
    cw_node_set(b->look[k], "influence", cw_val_float(0));
  }
  t3_vec3 fa = BARREL_REST;
  cw_node_set(b->look[2], "forward_axis", cw_val_vec3(fa.x, fa.y, fa.z));
  b->rate = 1, b->last_weapon = f->weapon, b->was_alive = f->alive, b->was_floor = true;
  b->last_hit_t = b->last_throw_t = b->last_bash_t = 1000;
  b->mesh_lod = -1, b->divisor = 1;
  b->made = true;
  f->anim = b->anim;
}

t3_object *soldier_object(int slot) { return B[slot].made ? B[slot].inst : NULL; }
t3_material *soldier_material(int slot) { return B[slot].made ? B[slot].mat : NULL; }

static void rag_end(body *b) {
  if (b->rd) cw_ragdoll_destroy(b->rd), b->rd = NULL, rag_live--;
}

void soldier_free(int slot) {
  body *b = &B[slot];
  rag_end(b);
  if (b->bound) cw_pose_free(&b->pose), b->bound = false;
  if (b->inst) {
#ifdef CW_RIM_LIGHT_H
    cw_rim_light_remove_tree(b->inst);
#endif
    t3_release(b->inst);
  }
  b->inst = NULL, b->mat = NULL, b->made = false;
}

void soldier_free_all(void) {
  for (int i = 0; i < MAXF + CROWD_MAX; i++) soldier_free(i);
  crowd_n = 0;
  rag_live = 0;
}

/* ── the fall ──────────────────────────────────────────────────────────── */
static void rag_start(body *b, fighter *f) {
  if (rag_live >= RAG_MAX || !A.world.id) return;
  if (!b->bound) {
    if (!cw_pose_bind(&b->pose, b->inst)) return;
    b->bound = true;
  }
  cw_pose_capture(&b->pose);
  t3_mat4 m = model_to_world(f->mv.position, f->yaw);
  cw_ragdoll_def def = cw_ragdoll_default_def(PARTS, NPARTS);
  def.joint_friction = 2.5f;
  def.angular_damping = 0.8f;
  def.layer = RAG_LAYER, def.mask = 1u;
  b->rd = cw_ragdoll_create(A.world, &def, &b->pose, &m);
  if (!b->rd) return;
  rag_live++;
  b->rd_t = 0, b->still = false;
  /* the blow that downed it: along its way, harder for a big hit (a rocket throws, a rail knocks) */
  t3_vec3 d = f->hit_dir;
  float k = CW_CLAMP(v3len(d), 0, 1.5f);
  d = v3norm(v3(d.x, 0, d.z));
  t3_vec3 vel = v3add(v3scale(f->mv.velocity, 0.6f), v3(d.x * (1.5f + 2.5f * k), 0.8f + 1.2f * k, d.z * (1.5f + 2.5f * k)));
  cw_ragdoll_set_velocity(b->rd, vel, v3(d.z * 3, 0, -d.x * 3));
  cw_node_set(b->feet, "active", cw_val_bool(false));
  for (int i = 0; i < 4; i++) cw_node_set(b->look[i], "active", cw_val_bool(false));
}

/* ── the motion recorder (tests/arenafps-motion.mjs): up to four fighters' bones each tick, in the world ───────
 * A row is the pose the last tick drew (after the modifiers) with the state that made it: the fighter's place,
 * facing, aim and velocity, whether it stood on the floor, its detail level, the strongest locomotion clip and its
 * weight, the ground under each ankle (a ray down through the level), and the bones. */
static const char *const MREC_BONES[] = { "pelvis", "foot_l", "foot_r", "ball_l", "ball_r", "spine_03", "Head", "hand_l", "hand_r", "rifle", "muzzle" };
#define MREC_NB ((int)(sizeof MREC_BONES / sizeof *MREC_BONES))
#define MREC_MAX 9600
#define MREC_SLOTS 4
typedef struct {
  int tick, slot, alive, floor, lod;
  float x, y, z, yaw, pitch, vx, vz, back;
  char clip[12];
  float w, ground[2];
  float p[MREC_NB][3];
} mrec_row;
typedef struct { int alive, floor, lod; t3_vec3 pos, vel; float yaw, pitch, back; bool set; } mrec_state;
static mrec_row *MREC;
static int mrec_n, mrec_want, mrec_tick, mrec_ns, mrec_slots[MREC_SLOTS];
static mrec_state MPREV[MREC_SLOTS];
static cw_pose MPOSE[MREC_SLOTS];
static int MPOSE_OF[MREC_SLOTS] = { -1, -1, -1, -1 }, MIDX[MREC_SLOTS][MREC_NB];

static void cmd_motion(int argc, const char **argv, cw_reply *rp) {
  if (argc >= 2 && !strcmp(argv[1], "rec")) {
    mrec_want = argc >= 3 ? CW_CLAMP(atoi(argv[2]), 1, MREC_MAX) : 600;
    mrec_ns = 0;
    for (int k = 3; k < argc && mrec_ns < MREC_SLOTS; k++) mrec_slots[mrec_ns++] = CW_CLAMP(atoi(argv[k]), 0, MAXF - 1);
    if (!mrec_ns) mrec_slots[mrec_ns++] = 0;
    for (int k = 0; k < MREC_SLOTS; k++) MPREV[k].set = false;
    if (!MREC) MREC = (mrec_row *)calloc(MREC_MAX, sizeof *MREC);
    mrec_n = 0;
    cw_reply_printf(rp, "{\"recording\":%d,\"slots\":%d}", mrec_want, mrec_ns);
    return;
  }
  if (argc >= 2 && !strcmp(argv[1], "info")) {
    cw_reply_printf(rp, "{\"rows\":%d,\"want\":%d,\"ankle\":%.4f,\"bones\":[", mrec_n, mrec_want, (double)ANKLE);
    for (int b = 0; b < MREC_NB; b++) cw_reply_printf(rp, "%s\"%s\"", b ? "," : "", MREC_BONES[b]);
    cw_reply_printf(rp, "],\"travel\":{");
    for (int v = 0, n = 0; v < 2; v++)
      for (int k = 0; MODEL[v].ready && k < NLOCO; k++)
        cw_reply_printf(rp, "%s\"%c:%s\":[%.3f,%.3f],\"%c:%s\":[%.3f,%.3f]", n++ ? "," : "", 'a' + v, LOCO_F[k], (double)MODEL[v].gv_f[k].x,
                        (double)MODEL[v].gv_f[k].y, 'a' + v, LOCO_B[k], (double)MODEL[v].gv_b[k].x, (double)MODEL[v].gv_b[k].y);
    cw_reply_printf(rp, "}}");
    return;
  }
  if (argc >= 4 && !strcmp(argv[1], "get")) {
    int from = CW_CLAMP(atoi(argv[2]), 0, mrec_n), n = CW_CLAMP(atoi(argv[3]), 0, mrec_n - from);
    cw_reply_printf(rp, "[");
    for (int k = 0; k < n; k++) {
      const mrec_row *w = &MREC[from + k];
      cw_reply_printf(rp, "%s[%d,%d,%d,%d,%d,%.4f,%.4f,%.4f,%.5f,%.5f,%.4f,%.4f,%.3f,\"%s\",%.3f,%.4f,%.4f", k ? "," : "", w->tick, w->slot, w->alive,
                      w->floor, w->lod, w->x, w->y, w->z, w->yaw, w->pitch, w->vx, w->vz, w->back, w->clip, w->w, w->ground[0], w->ground[1]);
      for (int b = 0; b < MREC_NB; b++) cw_reply_printf(rp, ",%.4f,%.4f,%.4f", w->p[b][0], w->p[b][1], w->p[b][2]);
      cw_reply_printf(rp, "]");
    }
    cw_reply_printf(rp, "]");
    return;
  }
  cw_reply_error(rp, "motion rec <ticks> [slot ...] | motion info | motion get <from> <n>");
}

static void mrec_take(void) {
  if (!MREC || mrec_n >= mrec_want) return;
  for (int s = 0; s < mrec_ns && mrec_n < mrec_want; s++) {
    int slot = mrec_slots[s];
    body *b = &B[slot];
    if (!b->made || !b->inst || !MPREV[s].set) continue;
    if (MPOSE_OF[s] != slot) {
      if (MPOSE_OF[s] >= 0) cw_pose_free(&MPOSE[s]);
      MPOSE_OF[s] = -1;
      if (!cw_pose_bind(&MPOSE[s], b->inst)) continue;
      MPOSE_OF[s] = slot;
      for (int k = 0; k < MREC_NB; k++) MIDX[s][k] = cw_pose_find(&MPOSE[s], MREC_BONES[k]);
    }
    cw_pose_capture(&MPOSE[s]);
    cw_pose_update_model(&MPOSE[s]);
    const mrec_state *m = &MPREV[s];
    mrec_row *w = &MREC[mrec_n++];
    w->tick = mrec_tick, w->slot = slot, w->alive = m->alive, w->floor = m->floor, w->lod = m->lod;
    w->x = m->pos.x, w->y = m->pos.y, w->z = m->pos.z, w->yaw = m->yaw, w->pitch = m->pitch;
    w->vx = m->vel.x, w->vz = m->vel.z, w->back = m->back;
    float best = -1;
    w->clip[0] = 0;
    const char *const *L[2] = { LOCO_F, LOCO_B };
    for (int h = 0; h < 2; h++)
      for (int k = 0; k < NLOCO; k++) {
        float wt = cw_animtree_clip_weight(b->tree, b->loco[h][k]);
        if (wt > best) best = wt, snprintf(w->clip, sizeof w->clip, "%s", L[h][k]);
      }
    float wi = cw_animtree_clip_weight(b->tree, b->idle[0]) + cw_animtree_clip_weight(b->tree, b->idle[1]);
    if (wi > best) best = wi, snprintf(w->clip, sizeof w->clip, "idle");
    w->w = best;
    t3_mat4 mw = model_to_world(m->pos, m->yaw);
    for (int k = 0; k < MREC_NB; k++) {
      t3_vec3 p = MIDX[s][k] >= 0 ? cw_pose_model_position(&MPOSE[s], MIDX[s][k]) : (t3_vec3){ 0 };
      t3_vec3 q = t3_vec3_apply_mat4(p, &mw);
      w->p[k][0] = q.x, w->p[k][1] = q.y, w->p[k][2] = q.z;
    }
    for (int k = 0; k < 2; k++) { /* the ground under each ankle */
      t3_vec3 a = { w->p[1 + k][0], w->p[1 + k][1], w->p[1 + k][2] }, hp;
      w->ground[k] = arena_ray(v3add(a, v3(0, 0.6f, 0)), v3add(a, v3(0, -1.5f, 0)), &hp, NULL) ? hp.y : -1000.0f;
    }
  }
}

static void mrec_note(int slot, const fighter *f, const body *b) {
  for (int s = 0; s < mrec_ns; s++)
    if (mrec_slots[s] == slot) {
      mrec_state *m = &MPREV[s];
      m->alive = f->alive, m->floor = f->mv.on_floor, m->lod = b->lod, m->pos = f->mv.position, m->vel = f->mv.velocity;
      m->yaw = f->yaw, m->pitch = f->pitch, m->back = b->back, m->set = true;
    }
}

/* ── per tick ─────────────────────────────────────────────────────────── */
static void shot(body *b, int k) {
  if (b->shot[k] >= 0) cw_animtree_set_param(b->tree, SHOTS[k], 1);
}

/* ── the bench crowd (autopilot builds, ARENA_CROWD=n: tests/arenafps-anim-cost.mjs) ─────────────────────────
 * n more bodies that are no fighters: each runs a circle of 3 to 5 m round a point of the level at a run, a walk or a
 * stand, some facing the way they go, some the circle's middle (strafing) and some away from it (backing round), and
 * fires now and then, so the cost of n soldiers on screen is measured through the same path as the fighters' bodies,
 * without the bots' brains or the rules. */
static void tick_body(int i, fighter *f, body *b, const t3_vec3 *eyes, int neyes, bool own_hidden);
static fighter CROWD[CROWD_MAX];
static struct { t3_vec3 c; float r, w, phase; int face; } CR[CROWD_MAX];

void soldier_crowd(int n, cw_node world, const t3_vec3 *centres, int ncentres) {
  for (int k = 0; k < crowd_n; k++) soldier_free(MAXF + k);
  crowd_n = CW_CLAMP(n, 0, CROWD_MAX);
  for (int k = 0; k < crowd_n && ncentres > 0; k++) {
    fighter *f = &CROWD[k];
    memset(f, 0, sizeof *f);
    f->active = f->alive = true;
    f->mv.on_floor = true;
    CR[k].c = centres[k % ncentres];
    CR[k].r = 3.0f + (float)(k % 3);
    CR[k].w = (k % 4 == 3) ? 0 : (k % 4 == 2 ? 0.45f : 1.4f) * (k & 1 ? 1 : -1); /* rad/s: a run, a walk, a stand */
    CR[k].phase = (float)k * 1.7f;
    CR[k].face = k % 3; /* 0 the way it goes, 1 the middle, 2 away from it */
    soldier_make(f, MAXF + k, k & 1, world, false);
  }
}
static void crowd_tick(const t3_vec3 *eyes, int neyes) {
  crowd_t++;
  for (int k = 0; k < crowd_n; k++) {
    fighter *f = &CROWD[k];
    body *b = &B[MAXF + k];
    if (!b->made || !b->inst) continue;
    float a = CR[k].phase + CR[k].w * (float)crowd_t * TICK_DT, sa, ca;
    cw_sincos(a, &sa, &ca);
    t3_vec3 p = v3add(CR[k].c, v3(ca * CR[k].r, 0, sa * CR[k].r));
    t3_vec3 hp;
    if (arena_ray(v3add(p, v3(0, 2, 0)), v3add(p, v3(0, -4, 0)), &hp, NULL)) p.y = hp.y;
    t3_vec3 vel = v3scale(v3(-sa, 0, ca), CR[k].w * CR[k].r);
    float yaw = CR[k].face == 0 ? cw_atan2(-vel.x, -vel.z) : CR[k].face == 1 ? cw_atan2(ca, sa) : cw_atan2(-ca, -sa);
    if (CR[k].w == 0) yaw = a;
    f->mv.position = p, f->mv.velocity = vel, f->yaw = yaw;
    f->mhold = (crowd_t + k * 7) % 90 < 20 && (crowd_t + k) % 6 == 0 ? 9 : 0; /* bursts */
    f->throw_t = f->bash_t = f->hit_t = 1000;
    f->weapon = 0;
    float sy, cy;
    cw_sincos(yaw * 0.5f, &sy, &cy);
    cw_node3d_set_position(f->body, p);
    cw_node3d_set_quaternion(f->body, (t3_quat){ 0, sy, 0, cy });
    tick_body(MAXF + k, f, b, eyes, neyes, false);
  }
}

/* one body a tick: i its slot, f the fighter (or a crowd stand-in) it shows */
static bool crowded; /* more than 16 fighters (a battle): the animation's level of detail steps down sooner */
static void tick_body(int i, fighter *f, body *b, const t3_vec3 *eyes, int neyes, bool own_hidden) {
  /* the nearest eye that can see it */
  float d2 = 1e9f;
  for (int e = 0; e < neyes; e++) {
    if (own_hidden && e == i) continue;
    t3_vec3 d = v3sub(f->mv.position, eyes[e]);
    d2 = fminf(d2, v3dot(d, d));
  }
  if (own_hidden && i == 0 && neyes <= 1) d2 = 1e9f;
  float dist = sqrtf(d2);
  bool seen = !(own_hidden && i == 0);
  SOLDIER_ST.bodies++;
  /* detail by distance */
  int lod = dist < 14 ? 0 : dist < 30 ? 1 : dist < 50 ? 2 : 3;
  b->lod = lod;
  b->divisor = seen ? cw_anim_lod_divisor(dist, 14) : 8;
  if (crowded && seen) b->divisor = dist < 8 ? 1 : dist < 16 ? 2 : dist < 30 ? 4 : dist < 60 ? 8 : 16; /* a battle's crowd: each tree costs about 0.15 ms an update */
  cw_animtree_set_update_divisor(b->tree, crowded ? 1 : b->divisor); /* (a crowd: kept by hand below) */
  bool far = lod >= 2;
  if (far != b->masked) {
    b->masked = far;
    if (far) cw_animtree_set_filter(b->tree, b->top, MAIN_BONES, NMAIN);
    else cw_animtree_set_filter(b->tree, b->top, NULL, 0);
  }
  bool sh = lod <= 1;
  if (sh != b->shadow) set_cast(b->inst, sh), b->shadow = sh;
  /* the mesh: the full one near by, past 15 m the packer's quarter (about 2,500 triangles: models/soldier_*.glb.cwimport),
   * past 50 m its eighth; one primitive and one material, so a body costs one colour draw (and one shadow draw near by) */
  int ml = dist < 15 ? 0 : dist < 50 ? 2 : 3;
#ifdef ARENA_AUTOPILOT
  { /* ARENA_SOLDIER_LOD=n: every body at mesh level n (the same-picture check's control: the far mesh at 5 m) */
    static int force = -2;
    if (force == -2) force = getenv("ARENA_SOLDIER_LOD") ? atoi(getenv("ARENA_SOLDIER_LOD")) : -1;
    if (force >= 0) ml = force;
  }
#endif
  if (ml != b->mesh_lod) {
    int tris = cw_gltf_instance_set_lod(b->inst, ml);
    b->mesh_lod = ml;
    static int told[8];
    if (ml >= 0 && ml < 8 && !told[ml]) told[ml] = 1, cw_info("arenafps: soldier mesh level %d draws %d triangles", ml, tris);
  }
  SOLDIER_ST.lod[lod]++;
  mrec_note(i, f, b);
  /* the body's own frame: its front (-Z at yaw 0) and its left */
  float sy, cy;
  cw_sincos(f->yaw, &sy, &cy);
  t3_vec3 fw = v3(-sy, 0, -cy), lt = v3(-cy, 0, sy);
  t3_vec3 v = f->mv.velocity;
  float vf = v3dot(v, fw), vl = v3dot(v, lt), sp = sqrtf(vf * vf + vl * vl);
  bool floor = f->mv.on_floor;
  if (!f->alive) vf = vl = sp = 0;
  /* the dead */
  if (!f->alive && b->was_alive) {
    rag_start(b, f);
    if (!b->rd) shot(b, SH_DIE), b->act_t = 0;
    b->lying = false;
  }
  if (f->alive && !b->was_alive) { /* up again: back on its feet */
    rag_end(b);
    b->still = false, b->lying = false;
    cw_animtree_set_param(b->tree, "lying", 0);
    cw_node_set(b->feet, "active", cw_val_bool(true));
    for (int k = 0; k < 4; k++) cw_node_set(b->look[k], "active", cw_val_bool(true));
  }
  b->was_alive = f->alive;
  if (b->rd) {
    SOLDIER_ST.ragdolls++;
    cw_pose_capture(&b->pose);
    t3_mat4 m = model_to_world(f->mv.position, f->yaw);
    cw_ragdoll_blend_pose(b->rd, &b->pose, &m, 1);
    cw_pose_flush(&b->pose);
    if (cw_ragdoll_resting(b->rd, 0.1f) || ++b->rd_t > 240) rag_end(b), b->still = true;
    return;
  }
  if (b->still) { cw_pose_flush(&b->pose); return; }
  if (!f->alive) { /* a fall without a ragdoll: the clip, then lying */
    b->act_t++;
    if (b->act_t > 70 && !b->lying) b->lying = true, cw_animtree_set_param(b->tree, "lying", 1);
  }
  /* the legs: the blend space's place eases toward the body's velocity (a start, a stop or a turn moves through
   * the blend), faster up to speed than down (a stop is quick, the feet barely slide) */
  /* (the library's sprint travels 8.4 m/s, its walk 0.9: from 60% of the sprint's speed up the soldier runs on the
   * sprint alone, its cycle slowed or quickened to the speed it goes, so its planted feet keep to the ground; below,
   * the walk and the sprint blend at the speed itself) */
  float run = MODEL[b->variant].run_speed;
  bool ring = sp > 0.6f * run;
  float k = ring ? run / sp : 1;
  float tx = vl * k, ty = vf * k;
  const float ease = 0.35f, up = 1.0f, dn = 1.5f;
  bool faster = fabsf(tx) + fabsf(ty) > fabsf(b->bx) + fabsf(b->by);
  b->bx += CW_CLAMP((tx - b->bx) * ease, faster ? -up : -dn, faster ? up : dn);
  b->by += CW_CLAMP((ty - b->by) * ease, faster ? -up : -dn, faster ? up : dn);
  float want_rate = ring ? CW_CLAMP(sp / run, 0.6f, 1.7f) : 1;
  b->rate += (want_rate - b->rate) * 0.2f;
  /* front or back: by the way it goes against the way it faces, with a margin either side of square */
  if (sp > 0.8f) {
    float ang = fabsf(cw_atan2(vl, vf));
    if (ang > 1.85f) b->back = 1;
    else if (ang < 1.4f) b->back = 0;
  }
  /* the air: off the ground a few ticks (a step down is not a jump) */
  b->air_t = floor ? 0 : b->air_t + 1;
  if (f->alive && !floor && b->was_floor && v.y > 3) shot(b, SH_JUMP);
  if (f->alive && floor && !b->was_floor) {
    /* a landing after a real fall (it was in the air at least a third of a second) */
    if (b->act >= 20) shot(b, SH_LAND);
  }
  b->act = floor ? 0 : b->act + 1;
  b->was_floor = floor;
  cw_node t = b->tree;
  /* the tree is advanced here, outside the fixed phase, where its own update divisor does not apply: the level of
   * detail is kept by hand, every divisor-th tick (staggered by slot) with all the time since. Between its updates a
   * body only follows the rules' events (a shot, a hit, a throw); its parameters and its aim and feet are set on the
   * ticks it updates (each set is a lookup by name, about 0.1 ms a body a tick with them all) */
  b->acc += TICK_DT;
  bool adv = seen && (!crowded || b->divisor <= 1 || (mrec_tick + i) % b->divisor == 0); /* (the other modes as they were: every tick) */
  float dt_since = b->acc;
  if (!seen) b->acc = fminf(b->acc, 0.5f);
  if (adv) {
    cw_animtree_set_param(t, "mx", b->bx);
    cw_animtree_set_param(t, "my", b->by);
    cw_animtree_set_param(t, "back", b->back);
    cw_animtree_set_param(t, "air", f->alive && b->air_t > 8 ? 1 : 0);
    cw_animtree_set_param(t, "rate", b->rate);
  }
  { /* the leans: into a turn by its sideways pull (speed x turn rate), forward into a start and back into a stop, each
     * held over a fraction of a second (a lean, not each step's sway) */
    float dy = f->yaw - b->last_yaw;
    while (dy > CW_PI) dy -= 2 * CW_PI;
    while (dy < -CW_PI) dy += 2 * CW_PI;
    float acc = (sp - b->last_speed) / TICK_DT;
    bool moving = f->alive && floor && sp > 1;
    float ws = moving ? CW_CLAMP(dy / TICK_DT * sp / 10.0f, -1, 1) : 0, wf = moving ? CW_CLAMP(acc / 20.0f, -1, 1) : 0;
    b->lean_s += (ws - b->lean_s) * 0.12f, b->lean_f += (wf - b->lean_f) * 0.1f;
    b->last_yaw = f->yaw, b->last_speed = sp;
    if (adv) {
      cw_animtree_set_param(t, "lean_l", fmaxf(0, b->lean_s));
      cw_animtree_set_param(t, "lean_r", fmaxf(0, -b->lean_s));
      cw_animtree_set_param(t, "lean_fw", fmaxf(0, b->lean_f));
      cw_animtree_set_param(t, "lean_bk", fmaxf(0, -b->lean_f));
    }
  }
  if (adv) cw_animtree_set_param(t, "armed", f->alive ? 1 : 0);
  /* the actions, from the rules' own state */
  float look_want = f->alive ? 1 : 0;
  if (f->alive) {
    /* each event is a counter the rules reset: a shot sets the muzzle light's hold to 9 (it is 8 here, a tick's fade
     * on), a throw, a bash or a hit set their ticks-since to 0 (1 here when the rules counted it on in the same tick) */
    bool fired = f->mhold > b->last_mhold || (f->mhold == 8 && b->last_mhold == 8);
    if (fired) b->kick = 1;
    else b->kick *= 0.78f;
    if (f->weapon != b->last_weapon) shot(b, SH_RELOAD);
    if (f->throw_t < b->last_throw_t) shot(b, SH_THROW), b->look_w = fminf(b->look_w, 0.5f);
    if (f->bash_t < b->last_bash_t) shot(b, SH_BASH);
    if (f->hit_t < b->last_hit_t) {
      int h = SH_HIT_F;
      if (f->hit_head) h = SH_HIT_HEAD;
      else if (v3dot(f->hit_dir, f->hit_dir) > 1e-4f) {
        /* where the blow came from, in the body's frame: the blow travels hit_dir */
        float a = cw_atan2(-v3dot(f->hit_dir, lt), -v3dot(f->hit_dir, fw)); /* 0: from the front, + from the left */
        h = fabsf(a) < 0.8f ? SH_HIT_F : fabsf(a) > 2.35f ? SH_HIT_B : a > 0 ? SH_HIT_L : SH_HIT_R;
      }
      shot(b, h);
      b->look_w = fminf(b->look_w, 0.35f);
    }
  } else b->kick = 0;
  b->last_weapon = f->weapon, b->last_mhold = f->mhold, b->last_hit_t = f->hit_t, b->last_throw_t = f->throw_t, b->last_bash_t = f->bash_t;
  if (adv) cw_animtree_set_param(t, "kick", CW_CLAMP(b->kick, 0, 1));
  /* the aim: the spine and the chest turn the rifle onto the aim line, the head looks along it */
  b->look_w += (look_want - b->look_w) * 0.15f;
  bool aim = f->alive && lod <= 2;
  t3_vec3 e = v3add(f->mv.position, v3(0, EYE_H, 0)), d = look_dir(f->yaw, f->pitch);
  t3_vec3 tp = v3add(e, v3scale(d, 30));
  static const float SHARE[4] = { 0.25f, 0.4f, 1.0f, 1.0f };
  for (int q = 0; q < 4 && adv; q++) {
    cw_node_set(b->look[q], "active", cw_val_bool(aim));
    cw_node_set(b->look[q], "target_position", cw_val_vec3(tp.x, tp.y, tp.z));
    cw_node_set(b->look[q], "influence", cw_val_float(CW_CLAMP(b->look_w, 0, 1) * SHARE[q]));
  }
  /* the feet: placed while it stands or runs on the ground near by, eased in and out */
  bool plant = f->alive && floor && lod <= 1;
  b->feet_w += ((plant ? 1.0f : 0.0f) - b->feet_w) * 0.2f;
  if (adv) {
    cw_node_set(b->feet, "active", cw_val_bool(b->feet_w > 0.01f));
    cw_node_set(b->feet, "influence", cw_val_float(CW_CLAMP(b->feet_w, 0, 1)));
  }
  if (adv) cw_animtree_advance(t, dt_since), b->acc = 0, SOLDIER_ST.animated++;
}

#ifdef ARENA_AUTOPILOT
#include <time.h>
static double sol_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9; }
#else
static double sol_now(void) { return 0; }
#endif
void soldier_tick(fighter *fs, int nf, const t3_vec3 *eyes, int neyes, bool own_hidden) {
  static bool cmd;
  if (!cmd) cw_debug_command("motion", "motion rec <ticks> [slot ...] | info | get <from> <n>: the soldiers' motion recorder (tests/arenafps-motion.mjs)", cmd_motion), cmd = true;
  mrec_tick++;
  mrec_take();
  int32_t us[3];
  memcpy(us, SOLDIER_ST.us, sizeof us);
  memset(&SOLDIER_ST, 0, sizeof SOLDIER_ST);
  memcpy(SOLDIER_ST.us, us, sizeof us);
  crowded = nf > 16;
  double t0 = sol_now();
  if (rag_live > 0) cw_world3d_step(A.world, TICK_DT); /* only the ragdolls move in the level's world */
  double t1 = sol_now();
  for (int i = 0; i < nf; i++)
    if (fs[i].active && B[i].made && B[i].inst) tick_body(i, &fs[i], &B[i], eyes, neyes, own_hidden);
  double t2 = sol_now();
  crowd_tick(eyes, neyes);
  double t3 = sol_now();
  SOLDIER_ST.us[0] += (int32_t)((t1 - t0) * 1e6), SOLDIER_ST.us[1] += (int32_t)((t2 - t1) * 1e6), SOLDIER_ST.us[2] += (int32_t)((t3 - t2) * 1e6);
}
