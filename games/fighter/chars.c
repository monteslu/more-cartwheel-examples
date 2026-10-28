/* fighter characters: a skinned glb per fighter (tools/make-characters.mjs: anatomy, layered clothing with folds, hair,
 * a modelled face, hands with two-part fingers and a thumb, PBR materials with normal maps), posed bone by bone in code.
 *
 * Poses come from the simulation: the state, the frame of the move, and the move's frame data drive the animation, so
 * what you see is exactly what the rules do (startup winds up, active frames hold the strike, recovery returns). The
 * skeleton's joint positions are the table the hitboxes were drawn against (the generator uses the same numbers), so
 * the training overlay still lines up with fists and feet. Hands close into fists or open (grabs, palm thrusts, hurt and
 * knockdown) through the finger bones. Hair, headband tails, coat skirts and bellies swing on springs (secondary.c).
 * Everything here only reads the sim; nothing feeds back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/resource.h"
#include "game.h"

enum { HIPS, SPINE, CHEST, NECK, HEAD, UAR_L, FAR_L, HAND_L, UAR_R, FAR_R, HAND_R, THI_L, SHI_L, FOO_L, THI_R, SHI_R, FOO_R, JN };
static const char *const BONE[JN] = { "Hips", "Spine", "Chest", "Neck", "Head", "UpperArmL", "LowerArmL", "HandL", "UpperArmR", "LowerArmR", "HandR",
  "UpperLegL", "LowerLegL", "FootL", "UpperLegR", "LowerLegR", "FootR" };
/* the finger bones per hand (model left, model right): knuckles, middle joints, thumb */
static const char *const FING[2][3] = { { "FingersL", "Fingers2L", "ThumbL" }, { "FingersR", "Fingers2R", "ThumbR" } };

typedef struct spec {
  const char *name;
  float scale, bulk, shoulder;
  float stance, guard, bounce, kickiness; /* stance width, guard height, idle bounce, how much it kicks */
  int kind;
} spec;
/* the roster in data order: bolt, grit, vex, mara (scale, bulk and shoulder also shape the model: tools/make-characters.mjs) */
static const spec SPECS[4] = {
  { "bolt", 1.00f, 1.00f, 1.00f, 0.12f, 1.0f, 1.0f, 0.4f, 0 },
  { "grit", 1.06f, 1.38f, 1.25f, 0.17f, 0.8f, 0.5f, 0.2f, 1 },
  { "vex", 0.94f, 0.82f, 0.9f, 0.14f, 1.1f, 1.6f, 1.0f, 2 },
  { "mara", 0.97f, 0.88f, 0.92f, 0.11f, 1.2f, 1.3f, 0.7f, 3 },
};

struct fighter_model {
  int def;
  const spec *sp;
  cw_node node;
  t3_object *root, *bones[JN], *fing[2][3], *face[5];
  t3_vec3 face_rest[5];
  float jaw, lid, brow, pinch, showcase; /* expression now: jaw open, lids closed, brows lowered, brows pinched */
  t3_vec3 hips_rest;
  float cur[JN][3], root_off[3], yaw, tilt, fist[2];
  spring_rig springs;
  int last_state, last_stun;
  bool inited;
};

static float sm(float e0, float e1, float x) { float t = cw_clampf((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t); }

static void shadows_on(t3_object *o) {
  if (o->type == T3_SKINNED_MESH || o->type == T3_MESH) { o->cast_shadow = true; o->receive_shadow = true; o->frustum_culled = false; }
  for (int i = 0; i < o->child_count; i++) shadows_on(o->children[i]);
}

/* one glTF resource per fighter, parsed and uploaded once (models_preload, at start-up); every model is an instance
 * (cw_gltf_instantiate) sharing its geometry and textures, so picking a fighter never parses or uploads anything */
static cw_res RES[4];
bool models_preload(void) {
  for (int d = 0; d < 4; d++) {
    if (cw_res_is_ready(RES[d])) continue;
    char path[64];
    snprintf(path, sizeof path, "models/%s.glb", SPECS[d].name);
    RES[d] = cw_res_load(path, CW_RES_GLTF);
    if (!cw_res_gltf(RES[d])) { cw_error("fighter: %s did not load", path); return false; }
  }
  return true;
}

fighter_model *model_new(int def, cw_node parent) {
  if (!models_preload()) return NULL;
  fighter_model *m = calloc(1, sizeof *m);
  m->def = def;
  m->sp = &SPECS[def % 4];
  m->root = cw_gltf_instantiate(RES[def % 4]);
  if (!m->root) { cw_error("fighter: no instance of %s", m->sp->name); free(m); return NULL; }
  for (int j = 0; j < JN; j++) {
    m->bones[j] = t3_object_get_by_name(m->root, BONE[j]);
    if (!m->bones[j]) { cw_error("fighter: %s has no bone %s", m->sp->name, BONE[j]); t3_release(m->root); free(m); return NULL; }
  }
  for (int h = 0; h < 2; h++) for (int k = 0; k < 3; k++) m->fing[h][k] = t3_object_get_by_name(m->root, FING[h][k]);
  m->hips_rest = m->bones[HIPS]->position;
  static const char *const FACE[5] = { "Jaw", "LidL", "LidR", "BrowL", "BrowR" };
  for (int k = 0; k < 5; k++) { m->face[k] = t3_object_get_by_name(m->root, FACE[k]); if (m->face[k]) m->face_rest[k] = m->face[k]->position; }
  shadows_on(m->root);
  char_shade(m->root);
  sb_bind(&m->springs, m->root);
  char nm[24];
  static int serial;
  snprintf(nm, sizeof nm, "Fighter_%s_%d", m->sp->name, serial++);
  m->node = cw_node_new("Node3D", nm);
  cw_node_add_child(parent, m->node);
  cw_node3d_set_object(m->node, m->root);
  t3_release(m->root); /* the node holds it */
  cw_node_set(m->node, "interpolate", cw_val_bool(false));
  return m;
}

void model_free(fighter_model *m) {
  if (!m) return;
  if (cw_node_valid(m->node)) cw_node_free(m->node);
  free(m);
}
/* back to a first pose: the next model_pose snaps instead of easing from wherever the model last stood */
void model_reset(fighter_model *m) {
  if (!m) return;
  m->inited = false;
  m->showcase = 0;
  m->last_state = 0; m->last_stun = 0;
  sb_reset(&m->springs);
}
/* the node's visible flag (the scene tree writes it to the object every frame: setting the object alone does nothing) */
void model_set_visible(fighter_model *m, bool v) { if (m) { cw_node_set(m->node, "visible", cw_val_bool(v)); m->root->visible = v; } }
/* drawn far under the floor for a few frames at start-up, so its first real appearance creates nothing on the GPU */
void model_park(fighter_model *m) { if (m) { cw_node3d_set_position(m->node, (t3_vec3){ 0, -80, 0 }); model_set_visible(m, true); } }
void model_set_showcase(fighter_model *m, float turn) { if (m) m->showcase = turn; }
t3_vec3 model_joint(fighter_model *m, int j) {
  static const int MAP[] = { [J_HEAD] = HEAD, [J_HAND_F] = HAND_R, [J_HAND_B] = HAND_L, [J_FOOT_F] = FOO_R, [J_CHEST] = CHEST, [J_HIPS] = HIPS };
  const t3_mat4 *w = &m->bones[MAP[j]]->matrix_world;
  return (t3_vec3){ w->e[12], w->e[13], w->e[14] };
}
float model_spring_speed(const fighter_model *m) { return m ? sb_speed(&m->springs) : 0; }

/* ── poses ───────────────────────────────────────────────────────────── */

typedef struct pose { float r[JN][3]; float off[3]; float yaw, tilt, fist[2]; } pose; /* fist: 0 an open hand, 1 a closed fist (model left, right) */

/* arm in lead / rear terms: forward raise, out to the side, elbow bend */
static void arm(pose *p, int lead, int near_is_right, float fwd, float out, float elbow, float twist) {
  int right = lead ? near_is_right : !near_is_right;
  int ua = right ? UAR_R : UAR_L, fa = right ? FAR_R : FAR_L;
  float s = right ? -1.0f : 1.0f;
  p->r[ua][0] = -fwd; p->r[ua][2] = out * s; p->r[ua][1] = twist * s;
  p->r[fa][0] = -elbow;
}
/* how closed the lead or rear hand is (0 open, 1 a fist) */
static void hand(pose *p, int lead, int near_is_right, float v) { int right = lead ? near_is_right : !near_is_right; p->fist[right] = v; }
static void hands(pose *p, float v) { p->fist[0] = p->fist[1] = v; }
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
  hands(p, 1);
}

static void blend(pose *o, const pose *a, const pose *b, float k) {
  for (int j = 0; j < JN; j++) for (int c = 0; c < 3; c++) o->r[j][c] = a->r[j][c] + (b->r[j][c] - a->r[j][c]) * k;
  for (int c = 0; c < 3; c++) o->off[c] = a->off[c] + (b->off[c] - a->off[c]) * k;
  o->yaw = a->yaw + (b->yaw - a->yaw) * k;
  o->tilt = a->tilt + (b->tilt - a->tilt) * k;
  for (int h = 0; h < 2; h++) o->fist[h] = a->fist[h] + (b->fist[h] - a->fist[h]) * k;
}

/* the extended pose of each move slot, in the same terms as the stance */
static void strike(pose *p, const spec *s, int nr, int slot, int strength) {
  (void)strength;
  switch (slot) {
  /* punches: mid straight, high (a rising elbow or hook), low jab, the forward lunge */
  case M_5P: arm(p, 1, nr, 1.4f, 0.1f, 0.15f, 0); p->r[SPINE][0] = 0.12f; p->r[SPINE][1] = 0.0f; p->r[CHEST][1] = -0.2f; p->off[2] = 0.06f; break;
  case M_8P: arm(p, 0, nr, 2.25f, 0.2f, 0.7f, 0); arm(p, 1, nr, 1.1f, 0.3f, 1.6f, 0); p->r[SPINE][0] = -0.12f; p->r[SPINE][1] = -0.5f; p->r[CHEST][1] = -0.3f;
             p->off[1] += 0.04f; p->off[2] = 0.1f; leg(p, 0, nr, -0.1f, s->stance, 0.2f); break;
  case M_2P: arm(p, 1, nr, 1.4f, 0.1f, 0.2f, 0); p->off[1] = -0.35f; leg(p, 1, nr, 1.1f, 0.1f, 1.1f); leg(p, 0, nr, 0.7f, 0.2f, 1.9f); p->r[SPINE][0] = 0.25f; break;
  case M_6P: arm(p, 0, nr, 1.5f, 0.1f, 0.1f, 0); p->r[SPINE][0] = 0.25f; p->r[SPINE][1] = -0.6f; p->r[CHEST][1] = -0.35f; p->off[2] = 0.3f; p->off[1] -= 0.08f;
             leg(p, 1, nr, 0.95f, s->stance, 1.1f); leg(p, 0, nr, -0.55f, s->stance, 0.15f); break;
  /* kicks: mid front kick, high roundhouse, low sweep, the stepping kick */
  case M_5K: leg(p, 1, nr, 1.4f, 0.05f, 0.15f); leg(p, 0, nr, 0.0f, s->stance, 0.25f); p->r[SPINE][0] = -0.22f; p->off[1] = -0.02f;
             arm(p, 1, nr, 1.1f, 0.3f, 1.6f, 0); arm(p, 0, nr, 0.7f, 0.4f, 1.4f, 0); break;
  case M_8K: leg(p, 1, nr, 2.05f, 0.45f, 0.1f); leg(p, 0, nr, -0.05f, s->stance, 0.2f); p->r[SPINE][0] = -0.38f; p->r[SPINE][1] = -0.45f; p->r[SPINE][2] = 0.2f;
             arm(p, 1, nr, 0.9f, 0.6f, 1.3f, 0); arm(p, 0, nr, 0.3f, 0.9f, 0.6f, 0); break;
  case M_2K: p->off[1] = -0.5f; p->off[2] = 0.25f; leg(p, 1, nr, 1.45f, 0.05f, 0.05f); leg(p, 0, nr, 0.5f, 0.5f, 2.2f); p->r[SPINE][0] = 0.5f; p->r[CHEST][0] = 0.4f;
             arm(p, 1, nr, 0.3f, 0.7f, 0.6f, 0); arm(p, 0, nr, -0.5f, 0.9f, 0.3f, 0); break;
  case M_6K: leg(p, 1, nr, 1.55f, 0.12f, 0.2f); leg(p, 0, nr, -0.35f, s->stance, 0.35f); p->r[SPINE][0] = -0.15f; p->r[SPINE][1] = -0.3f; p->off[2] = 0.28f;
             arm(p, 1, nr, 1.0f, 0.35f, 1.5f, 0); arm(p, 0, nr, 0.5f, 0.6f, 1.0f, 0); break;
  /* in a hop */
  case M_JP: arm(p, 1, nr, 1.55f, 0.1f, 0.1f, 0); p->r[SPINE][0] = 0.3f; leg(p, 1, nr, 1.1f, 0.1f, 1.5f); leg(p, 0, nr, 0.5f, 0.1f, 1.8f); break;
  case M_JK: leg(p, 1, nr, 1.4f, 0.05f, 0.3f); leg(p, 0, nr, 0.4f, 0.1f, 2.0f); p->r[SPINE][0] = 0.4f; arm(p, 1, nr, 0.8f, 0.4f, 1.2f, 0); arm(p, 0, nr, 0.8f, 0.4f, 1.2f, 0); break;
  case M_THROW: case M_SP1: case M_SUPER:
    if (slot == M_THROW || (s->kind == 1 && slot == M_SP1) || (s->kind == 1 && slot == M_SUPER)) {
      arm(p, 1, nr, 1.55f, 0.35f, 0.1f, 0.5f); arm(p, 0, nr, 1.55f, 0.35f, 0.1f, 0.5f); p->r[SPINE][0] = 0.45f; p->off[1] = -0.2f;
      hands(p, 0.0f); /* reaching open (the grab closes them once it connects) */
    } else if (s->kind == 2 && slot == M_SP1) { /* slide */
      p->off[1] = -0.55f; p->off[2] = 0.3f; leg(p, 1, nr, 1.5f, 0.05f, 0.05f); leg(p, 0, nr, 0.3f, 0.4f, 2.0f); p->r[SPINE][0] = -0.5f; p->r[CHEST][0] = -0.3f;
    } else { /* a thrust of both hands */
      arm(p, 1, nr, 1.5f, 0.05f, 0.05f, 0); arm(p, 0, nr, 1.5f, 0.05f, 0.05f, 0); p->r[SPINE][0] = 0.25f; p->r[SPINE][1] = -0.3f; p->off[2] = 0.1f;
      hands(p, 0.0f); /* open palms push the shot out */
      leg(p, 1, nr, 0.7f, s->stance + 0.05f, 0.9f);
    }
    break;
  case M_SP2: arm(p, 0, nr, 2.9f, 0.15f, 0.1f, 0); arm(p, 1, nr, 1.0f, 0.4f, 1.4f, 0); p->r[SPINE][0] = -0.25f; p->r[CHEST][0] = -0.15f; p->off[1] = 0.15f;
              leg(p, 1, nr, 0.9f, 0.1f, 1.7f); leg(p, 0, nr, 0.0f, 0.1f, 0.2f); break;
  case M_SP3: leg(p, 1, nr, 1.4f, 0.1f, 0.2f); leg(p, 0, nr, 0.2f, 0.1f, 1.3f); p->r[SPINE][0] = 0.1f; p->r[SPINE][2] = 0.35f; p->off[1] = 0.05f;
              arm(p, 1, nr, 0.4f, 1.3f, 0.3f, 0); arm(p, 0, nr, 0.4f, 1.3f, 0.3f, 0); break;
  }
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
    hand(out, 1, nr, 0.1f);
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
    hands(out, 0.25f);
    break;
  }
  case S_AIRHIT: case S_THROWN: {
    float k = (float)f->t * 0.12f;
    out->tilt = -0.9f - 0.3f * (float)cw_sin(k); out->off[1] = 0.0f;
    arm(out, 1, nr, 0.6f, 0.9f, 0.4f, 0); arm(out, 0, nr, 0.3f, 1.0f, 0.4f, 0);
    leg(out, 1, nr, 0.5f, 0.2f, 0.7f); leg(out, 0, nr, 0.0f, 0.3f, 0.4f);
    hands(out, 0.15f);
    break;
  }
  case S_KD: case S_KO: {
    out->tilt = -1.57f; out->off[1] = -0.62f + 0.62f * 0.0f; out->off[2] = -0.55f;
    arm(out, 1, nr, 0.4f, 0.4f, 0.3f, 0); arm(out, 0, nr, 0.2f, 0.6f, 0.3f, 0);
    leg(out, 1, nr, 0.1f, 0.2f, 0.2f); leg(out, 0, nr, 0.0f, 0.3f, 0.1f);
    out->r[HEAD][0] = 0.3f;
    hands(out, 0.1f);
    break;
  }
  case S_GETUP: {
    float k = cw_clampf((float)f->t / 20.0f, 0, 1);
    pose down; memset(&down, 0, sizeof down); down.tilt = -1.57f; down.off[1] = -0.62f; down.off[2] = -0.55f; hands(&down, 0.1f);
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
    bool grab = f->mv == M_THROW || (s->kind == 1 && (f->mv == M_SP1 || f->mv == M_SUPER));
    if (grab && f->t > mv->su) hands(out, 0.85f);
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

/* fingers: knuckles and middle joints curl toward the palm (about the hand's front axis), the thumb folds across */
static void pose_hand(fighter_model *m, int h, float fist) {
  float sgn = h == 0 ? -1.0f : 1.0f; /* the left hand's palm faces -x */
  float k1 = 0.25f + 1.3f * fist, k2 = 0.3f + 1.45f * fist;
  float r1[3] = { 0.05f * fist, 0, sgn * k1 }, r2[3] = { 0, 0, sgn * k2 }, rt[3] = { 0.55f * fist, sgn * 0.2f * fist, sgn * (0.15f + 1.05f * fist) };
  const float *r[3] = { r1, r2, rt };
  for (int k = 0; k < 3; k++) {
    if (!m->fing[h][k]) continue;
    t3_quat q;
    quat_euler(r[k], &q);
    m->fing[h][k]->quaternion = q;
  }
}

/* the face: blinks, a focused frown in the guard, gritted teeth on heavy strikes and supers, a wince when hit (eyes
 * squeezed, brows down and pinched, the mouth pulled open), eyes shut and the jaw slack when down, a shout when winning */
static void pose_face(fighter_model *m, const ft_game *g, int who, float dt) {
  const ft_fighter *f = &g->f[who];
  float jaw = 0.02f, lid = 0.12f, brow = 0.4f, pinch = 0.3f;
  uint32_t ph = (g->tick + (uint32_t)who * 97u + (uint32_t)m->def * 31u) % 211u;
  if (ph < 6) lid = 1.0f; /* a blink */
  switch (f->state) {
  case S_HIT: case S_AIRHIT: case S_THROWN: jaw = 0.32f; lid = 0.8f; brow = 1.0f; pinch = 1.0f; break;
  case S_KD: case S_KO: jaw = 0.38f; lid = 1.0f; brow = 0.2f; pinch = 0; break;
  case S_GETUP: jaw = 0.1f; lid = 0.5f; brow = 0.8f; pinch = 0.8f; break;
  case S_WIN: jaw = 0.42f; lid = 0.0f; brow = -0.8f; pinch = 0; break;
  case S_BLOCK: jaw = 0.06f; lid = 0.45f; brow = 0.9f; pinch = 0.8f; break;
  case S_ATTACK: {
    const ft_move *mv = &g->roster->d[f->def].mv[f->mv];
    bool big = (mv->flags & (FM_SUPER | FM_SPECIAL)) || f->mv == M_8K || f->mv == M_2K || f->mv == M_6P || f->mv == M_6K || f->mv == M_JK;
    if (big) { jaw = f->t > mv->su ? 0.3f : 0.1f; lid = 0.35f; brow = 1.0f; pinch = 1.0f; } /* a shout on the strike, teeth gritted before it */
    else { jaw = 0.08f; lid = 0.3f; brow = 0.8f; pinch = 0.7f; }
    break;
  }
  default: break;
  }
  float k = 1.0f - cw_exp(-dt * 30.0f);
  m->jaw += (jaw - m->jaw) * k; m->lid += (lid - m->lid) * (lid > m->lid ? 1.0f - cw_exp(-dt * 60.0f) : k);
  m->brow += (brow - m->brow) * k; m->pinch += (pinch - m->pinch) * k;
  float hs = m->sp->scale;
  if (m->face[0]) { t3_quat q; float r[3] = { m->jaw * 0.45f, 0, 0 }; quat_euler(r, &q); m->face[0]->quaternion = q; }
  for (int s = 0; s < 2; s++) {
    float sg = s == 0 ? 1.0f : -1.0f;
    if (m->face[1 + s]) { t3_quat q; float r[3] = { m->lid * 0.95f, 0, 0 }; quat_euler(r, &q); m->face[1 + s]->quaternion = q; }
    if (m->face[3 + s]) {
      t3_vec3 p = m->face_rest[3 + s];
      p.y -= 0.0045f * hs * m->brow; p.x -= sg * 0.0025f * hs * m->pinch;
      m->face[3 + s]->position = p;
      t3_quat q; float r[3] = { 0, 0, sg * 0.22f * m->pinch }; quat_euler(r, &q); m->face[3 + s]->quaternion = q; /* the inner ends drop */
    }
  }
}

void model_pose(fighter_model *m, const ft_game *g, int who, float dt) {
  if (!m) return;
  const ft_fighter *f = &g->f[who];
  pose p;
  pose_for(m, g, who, &p);
  /* joints ease toward the target (about 3 ticks), so state changes read as motion, not snaps */
  float k = m->inited ? 1.0f - cw_exp(-dt * 28.0f) : 1.0f;
  if (f->state == S_ATTACK) k = 1.0f - cw_exp(-dt * 60.0f);
  if (f->state == S_ATTACK && f->t > g->roster->d[f->def].mv[f->mv].su) k = 1.0f; /* active frames: the limb is where the hitbox is, no easing lag */
  for (int j = 0; j < JN; j++) for (int c = 0; c < 3; c++) m->cur[j][c] += (p.r[j][c] - m->cur[j][c]) * k;
  for (int c = 0; c < 3; c++) m->root_off[c] += (p.off[c] - m->root_off[c]) * k;
  for (int h = 0; h < 2; h++) m->fist[h] += (p.fist[h] - m->fist[h]) * (m->inited ? 1.0f - cw_exp(-dt * 30.0f) : 1.0f);
  m->yaw = p.yaw;
  m->tilt += (p.tilt - m->tilt) * k;
  bool first = !m->inited;
  m->inited = true;
  for (int j = 0; j < JN; j++) {
    t3_quat q;
    quat_euler(m->cur[j], &q);
    m->bones[j]->quaternion = q;
  }
  for (int h = 0; h < 2; h++) pose_hand(m, h, m->fist[h]);
  pose_face(m, g, who, dt);
  /* hips carry the body's offset and lean (the knockdown pitch) */
  float sc = m->sp->scale;
  m->bones[HIPS]->position = (t3_vec3){ m->hips_rest.x + m->root_off[0], m->hips_rest.y + m->root_off[1] * sc, m->hips_rest.z + m->root_off[2] };
  t3_quat hq, tq;
  quat_euler(m->cur[HIPS], &hq);
  float tr[3] = { m->tilt, 0, 0 };
  quat_euler(tr, &tq);
  m->bones[HIPS]->quaternion = t3_quat_multiply(tq, hq);
  /* the root: world position, facing, and a spin for the kick */
  float face = f->facing > 0 ? 1.5707963f : -1.5707963f;
  float yaw[3] = { 0, face + m->yaw - (f->facing > 0 ? 1.0f : -1.0f) * m->showcase, 0 };
  t3_quat yq;
  quat_euler(yaw, &yq);
  t3_vec3 pos = { (float)f->x / 1000.0f, (float)f->y / 1000.0f, 0 };
  cw_node3d_set_quaternion(m->node, yq);
  cw_node3d_set_position(m->node, pos);
  /* secondary motion: a hit jolts hair, tails and belly away from the attacker; then the springs follow the pose */
  if (first) sb_reset(&m->springs);
  bool hurt = f->state == S_HIT || f->state == S_AIRHIT;
  if (hurt && (m->last_state != f->state || f->stun > m->last_stun)) {
    float away = (float)(g->f[1 - who].x < f->x ? 1 : -1);
    sb_jolt(&m->springs, away * 2.2f, 0.6f, 0);
  }
  m->last_state = f->state;
  m->last_stun = f->stun;
  sb_step(&m->springs, pos, face + m->yaw, dt);
}
