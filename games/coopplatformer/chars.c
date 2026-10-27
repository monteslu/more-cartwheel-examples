/* The characters: skinned glTF models made by tools/blender/characters.py, posed in code every frame (run cycle,
 * squash and stretch, wing flap, breathing). The bones are written with t3_object_set_quaternion from a rest pose,
 * so a pose is a pure function of the game state and the render clock. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/platform.h"
#include "game.h"

typedef struct bone {
  t3_object *o;
  t3_quat rest, parent_w;   /* local rest rotation, and the rest rotation of everything above it */
  t3_vec3 rest_scale, rest_pos;
} bone;

struct rig {
  t3_gltf *gltf;
  t3_object *scene;
  bone b[16];
  int nb;
  t3_material *mats[16];
  t3_color emis[16];
  float emis_i[16];
  int kind[16];             /* 0 other, 1 fur, 2 scarf, 3 accessory */
  int nmat;
  t3_object *acc[4];
};

/* ── quaternion helpers ───────────────────────────────────────────── */
static t3_quat qmul(t3_quat a, t3_quat b) {
  return (t3_quat){ a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
static t3_quat qconj(t3_quat q) { return (t3_quat){ -q.x, -q.y, -q.z, q.w }; }
static t3_quat qaxis(t3_vec3 a, float ang) {
  float s = sinf(ang * 0.5f), c = cosf(ang * 0.5f);
  return (t3_quat){ a.x * s, a.y * s, a.z * s, c };
}
static t3_vec3 qrot(t3_quat q, t3_vec3 v) {
  t3_quat p = qmul(qmul(q, (t3_quat){ v.x, v.y, v.z, 0 }), qconj(q));
  return (t3_vec3){ p.x, p.y, p.z };
}

static const char *const BONES[RIG_KINDS][16] = {
  /* glTF names lose their dots (arm.L is armL) */
  { "root", "body", "head", "armL", "armR", "legL", "legR", "tail", NULL },
  { "root", "body", "head", "leg0L", "leg1L", "leg2L", "leg0R", "leg1R", "leg2R", NULL },
  { "root", "head", "wingL", "wingtipL", "wingR", "wingtipR", NULL },
  { "root", "body", "head", "armL", "armR", "legL", "legR", NULL },
};
static const char *const FILES[RIG_KINDS] = { "models/hero.glb", "models/walker.glb", "models/flyer.glb", "models/boss.glb" };

static void each_mesh(t3_object *o, rig *r, bool in_acc) {
  if (o->name && !strncmp(o->name, "acc", 3)) in_acc = true;
  if (o->type == T3_SKINNED_MESH || o->type == T3_MESH) {
    o->cast_shadow = true;
    o->receive_shadow = true;
    o->frustum_culled = false; /* the bind pose bounds do not follow a posed skin */
    t3_mesh *m = (t3_mesh *)o;
    for (int i = 0; i < m->material_count && r->nmat < 16; i++) {
      t3_material *mt = m->materials[i];
      bool seen = false;
      for (int k = 0; k < r->nmat; k++) seen |= r->mats[k] == mt;
      if (seen) continue;
      r->mats[r->nmat] = mt;
      if (mt->emissive.r + mt->emissive.g + mt->emissive.b > 0.1f) mt->emissive_intensity = 2.6f; /* glowing parts bloom */
      r->emis[r->nmat] = mt->emissive;
      r->emis_i[r->nmat] = mt->emissive_intensity;
      r->kind[r->nmat] = in_acc ? 3 : (fabsf(mt->color.r - 0.9f) < 0.03f && fabsf(mt->color.g - 0.9f) < 0.03f && fabsf(mt->color.b - 0.9f) < 0.03f) ? 1 : (mt->color.r > 0.9f && mt->color.g > 0.7f && mt->color.g < 0.8f && mt->color.b < 0.3f) ? 2 : 0;
      if (getenv("COOP_DEBUG_RIG")) cw_warn("rig mat %d: color %.2f %.2f %.2f emissive %.2f %.2f %.2f x %.2f kind %d", r->nmat, mt->color.r, mt->color.g, mt->color.b, mt->emissive.r, mt->emissive.g, mt->emissive.b, mt->emissive_intensity, r->kind[r->nmat]);
      r->nmat++;
    }
  }
  for (int i = 0; i < o->child_count; i++) each_mesh(o->children[i], r, in_acc);
}

rig *rig_load(int kind) {
  size_t len;
  void *glb = cw_asset_load(FILES[kind], &len);
  if (!glb) {
    cw_error("coopplatformer: %s is missing from the assets", FILES[kind]);
    return NULL;
  }
  rig *r = calloc(1, sizeof *r);
  r->gltf = t3_gltf_parse(glb, len, NULL, NULL);
  free(glb);
  if (!r->gltf) {
    free(r);
    return NULL;
  }
  r->scene = r->gltf->scene;
  for (int i = 0; BONES[kind][i]; i++) {
    t3_object *o = t3_object_get_by_name(r->scene, BONES[kind][i]);
    if (!o) {
      cw_warn("coopplatformer: %s has no bone %s", FILES[kind], BONES[kind][i]);
      continue;
    }
    bone *b = &r->b[i];
    b->o = o;
    b->rest = o->quaternion;
    b->rest_scale = o->scale;
    b->rest_pos = o->position;
    t3_quat p = { 0, 0, 0, 1 };
    for (t3_object *a = o->parent; a && a != r->scene; a = a->parent) p = qmul(a->quaternion, p);
    b->parent_w = p;
    r->nb = i + 1;
  }
  each_mesh(r->scene, r, false);
  char nm[8];
  for (int k = 0; k < 4; k++) {
    snprintf(nm, sizeof nm, "acc%d", k);
    r->acc[k] = t3_object_get_by_name(r->scene, nm);
  }
  return r;
}

void rig_attach(rig *r, cw_node holder) { cw_node3d_set_object(holder, r->scene); }

void rig_free(rig *r) {
  if (!r) return;
  t3_release(r->gltf);
  free(r);
}

/* hero colours: the "fur" material takes the player colour, the scarf and the headgear its accent; headgear k is
 * shown for player k */
void rig_tint(rig *r, const float fur[3], const float scarf[3], int accessory) {
  for (int i = 0; i < r->nmat; i++) {
    t3_material *m = r->mats[i];
    if (r->kind[i] == 1) m->color = (t3_color){ fur[0], fur[1], fur[2] };
    else if (r->kind[i] == 2 || r->kind[i] == 3) m->color = (t3_color){ scarf[0], scarf[1], scarf[2] };
  }
  for (int k = 0; k < 4; k++)
    if (r->acc[k]) r->acc[k]->visible = k == accessory;
}

/* white-hot flash on a hit: add to every material's emission, k in 0..1 */
void rig_flash(rig *r, float k) {
  for (int i = 0; i < r->nmat; i++) {
    t3_material *m = r->mats[i];
    if (k > 0.01f) {
      m->emissive = (t3_color){ 1, 0.95f, 0.85f };
      m->emissive_intensity = r->emis_i[i] + 2.5f * k;
    } else {
      m->emissive = r->emis[i];
      m->emissive_intensity = r->emis_i[i];
    }
  }
}

/* the glowing parts (eyes, a core) pulse: every material that starts out emissive */
void rig_glow(rig *r, float k) {
  for (int i = 0; i < r->nmat; i++)
    if (r->emis[i].r + r->emis[i].g + r->emis[i].b > 0.1f) r->mats[i]->emissive_intensity = r->emis_i[i] * k;
}

static const t3_vec3 AX = { 1, 0, 0 }, AY = { 0, 1, 0 }, AZ = { 0, 0, 1 };

/* rotate bone i about a model-space axis, from its rest pose */
static void rot(rig *r, int i, t3_vec3 axis, float ang) {
  bone *b = &r->b[i];
  if (!b->o) return;
  t3_vec3 la = qrot(qconj(b->parent_w), axis);
  t3_object_set_quaternion(b->o, qmul(qaxis(la, ang), b->o->quaternion));
}
static void rest(rig *r) {
  for (int i = 0; i < r->nb; i++)
    if (r->b[i].o) {
      t3_object_set_quaternion(r->b[i].o, r->b[i].rest);
      t3_object_set_scale(r->b[i].o, r->b[i].rest_scale.x, r->b[i].rest_scale.y, r->b[i].rest_scale.z);
      r->b[i].o->position = r->b[i].rest_pos;
    }
}
static void scale(rig *r, int i, float x, float y, float z) {
  if (r->b[i].o) t3_object_set_scale(r->b[i].o, r->b[i].rest_scale.x * x, r->b[i].rest_scale.y * y, r->b[i].rest_scale.z * z);
}
static void lift(rig *r, float y) {
  if (r->b[0].o) r->b[0].o->position.y = r->b[0].rest_pos.y + y;
}

enum { H_ROOT, H_BODY, H_HEAD, H_ARML, H_ARMR, H_LEGL, H_LEGR, H_TAIL };

/* anim: 0 idle 1 run 2 rise 3 fall 4 wall 5 swim 6 dash. ph is the run phase, t the clock, st/wd the stretch and width
 * (squash and stretch). rot() composes on the current pose, so two calls on one bone add up. */
void rig_pose_hero(rig *r, int anim, float ph, float t, float st, float wd, float vy) {
  rest(r);
  float s = sinf(ph), c = cosf(ph);
  float breath = sinf(t * 2.2f);
  switch (anim) {
    case 1: /* run: opposite arms and legs, lean, a tail flick, a head that stays level */
      rot(r, H_LEGL, AX, -0.95f * s);
      rot(r, H_LEGR, AX, 0.95f * s);
      rot(r, H_ARML, AX, 0.9f * s);
      rot(r, H_ARMR, AX, -0.9f * s);
      rot(r, H_BODY, AX, 0.2f);
      rot(r, H_HEAD, AX, -0.12f + 0.05f * c);
      rot(r, H_TAIL, AX, -0.35f + 0.25f * sinf(ph * 2));
      rot(r, H_TAIL, AY, 0.2f * s);
      lift(r, 0.05f * fabsf(c));
      break;
    case 2: /* rising: arms up and out, knees tucked */
      rot(r, H_ARML, AZ, 2.5f);
      rot(r, H_ARMR, AZ, -2.5f);
      rot(r, H_LEGL, AX, -0.55f);
      rot(r, H_LEGR, AX, 0.25f);
      rot(r, H_BODY, AX, 0.08f);
      rot(r, H_TAIL, AX, 0.35f);
      break;
    case 3: /* falling: arms flung up, legs spread, tail streaming up */
      rot(r, H_ARML, AZ, 2.15f + 0.1f * breath);
      rot(r, H_ARMR, AZ, -2.15f - 0.1f * breath);
      rot(r, H_LEGL, AZ, 0.3f);
      rot(r, H_LEGR, AZ, -0.3f);
      rot(r, H_LEGL, AX, -0.2f);
      rot(r, H_LEGR, AX, 0.2f);
      rot(r, H_TAIL, AX, 0.8f + 0.1f * sinf(t * 14));
      break;
    case 4: /* on a wall: hands up, leaning in */
      rot(r, H_ARML, AZ, 2.9f);
      rot(r, H_ARMR, AZ, -2.9f);
      rot(r, H_BODY, AX, -0.15f);
      rot(r, H_LEGL, AX, -0.7f);
      rot(r, H_LEGR, AX, -0.3f);
      break;
    case 5: /* swim: pitched forward, legs kicking, arms stroking */
      rot(r, H_BODY, AX, 1.15f);
      rot(r, H_HEAD, AX, -0.9f);
      rot(r, H_LEGL, AX, 0.7f * sinf(ph * 0.9f));
      rot(r, H_LEGR, AX, -0.7f * sinf(ph * 0.9f));
      rot(r, H_ARML, AX, 1.5f + 0.7f * sinf(ph * 0.9f + 1.5f));
      rot(r, H_ARMR, AX, 1.5f - 0.7f * sinf(ph * 0.9f + 1.5f));
      rot(r, H_TAIL, AY, 0.5f * sinf(ph * 0.9f));
      break;
    case 6: /* dash: stretched along the move, arms and legs trailing */
      rot(r, H_BODY, AX, 1.0f);
      rot(r, H_HEAD, AX, -0.7f);
      rot(r, H_ARML, AX, 2.0f);
      rot(r, H_ARMR, AX, 2.0f);
      rot(r, H_LEGL, AX, 0.7f);
      rot(r, H_LEGR, AX, 0.5f);
      rot(r, H_TAIL, AX, 1.0f);
      break;
    default: /* idle: breathing, a slow tail sway, a head that wanders */
      rot(r, H_ARML, AZ, 0.1f + 0.05f * breath);
      rot(r, H_ARMR, AZ, -0.1f - 0.05f * breath);
      rot(r, H_HEAD, AX, 0.05f * sinf(t * 1.1f));
      rot(r, H_HEAD, AY, 0.18f * sinf(t * 0.7f));
      rot(r, H_TAIL, AY, 0.4f * sinf(t * 1.6f));
      rot(r, H_TAIL, AX, 0.1f * breath);
      break;
  }
  (void)vy;
  /* squash and stretch on the root: the whole body, the feet stay down */
  scale(r, H_ROOT, wd, st, wd);
}

/* walker: legs scurry in two tripods, the shell rocks */
void rig_pose_walker(rig *r, float ph, float t, float squash) {
  rest(r);
  for (int k = 0; k < 3; k++) {
    float a = ph + (float)k * 2.1f;
    rot(r, 3 + k, AX, 0.6f * sinf(a));
    rot(r, 6 + k, AX, 0.6f * sinf(a + 3.14159f));
    rot(r, 3 + k, AZ, 0.12f * fmaxf(0, sinf(a + 1.5f)));
    rot(r, 6 + k, AZ, -0.12f * fmaxf(0, sinf(a + 3.14159f + 1.5f)));
  }
  rot(r, 1, AZ, 0.06f * sinf(ph * 2));
  rot(r, 2, AX, 0.12f * sinf(ph * 2 + 1) + 0.04f * sinf(t * 3));
  scale(r, 1, 1, squash, 1);
  lift(r, 0.03f * fabsf(sinf(ph * 2)));
}

/* flyer: wings beat, the body bobs against them */
void rig_pose_flyer(rig *r, float ph, float t) {
  rest(r);
  float f = sinf(ph);
  rot(r, 2, AZ, 0.75f * f);
  rot(r, 3, AZ, 0.55f * sinf(ph - 0.9f));
  rot(r, 4, AZ, -0.75f * f);
  rot(r, 5, AZ, -0.55f * sinf(ph - 0.9f));
  rot(r, 1, AX, 0.12f * sinf(t * 2));
  lift(r, 0.06f * f);
}

/* boss: a heavy breath, arms that swing out and slam; rage 0..1 as it is hurt */
void rig_pose_boss(rig *r, float t, float rage) {
  rest(r);
  float b = sinf(t * 1.6f);
  rot(r, 3, AZ, 0.12f + 0.1f * b + 0.5f * rage * fmaxf(0, sinf(t * 5)));
  rot(r, 4, AZ, -0.12f - 0.1f * b - 0.5f * rage * fmaxf(0, sinf(t * 5 + 1)));
  rot(r, 3, AX, 0.25f * sinf(t * 1.1f));
  rot(r, 4, AX, -0.25f * sinf(t * 1.1f + 0.5f));
  rot(r, 2, AY, 0.3f * sinf(t * 0.8f));
  rot(r, 2, AX, 0.05f * b);
  scale(r, 1, 1 + 0.015f * b, 1 + 0.02f * b, 1 + 0.015f * b);
}
