/* The Tux model: the original tux.tcl hierarchy (tools/convert.mjs ->
 * assets/model/tux.txt) as three.c groups and unit spheres, every node's
 * local matrix set directly (hier.c's post-multiplied transforms), joints
 * rotated at run time as tux.c's adjust_tux_joints does. */
#include "game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/math.h"
#include "cartwheel/platform.h"

t3_quat tr_quat_axis_angle(t3_vec3 axis, float radians) {
  float s = cw_sin(radians * 0.5f);
  return (t3_quat){ axis.x * s, axis.y * s, axis.z * s, cw_cos(radians * 0.5f) };
}
t3_quat tr_slerp(t3_quat a, t3_quat b, float t) {
  float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (d < 0) { b = (t3_quat){ -b.x, -b.y, -b.z, -b.w }; d = -d; }
  float wa, wb;
  if (d > 0.9995f) { wa = 1 - t; wb = t; }
  else {
    float th = cw_acos(d), s = cw_sin(th);
    wa = cw_sin((1 - t) * th) / s;
    wb = cw_sin(t * th) / s;
  }
  return t3_quat_normalize((t3_quat){ a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb });
}
t3_quat tr_rotation_between(t3_vec3 s, t3_vec3 t) {
  float d = t3_vec3_dot(s, t);
  if (d > 0.999999f) return t3_quat_identity();
  if (d < -0.999999f) {
    t3_vec3 axis = t3_vec3_cross((t3_vec3){ 1, 0, 0 }, s);
    if (t3_vec3_length_sq(axis) < 1e-6f) axis = t3_vec3_cross((t3_vec3){ 0, 0, 1 }, s);
    return tr_quat_axis_angle(t3_vec3_normalize(axis), 3.14159265f);
  }
  return tr_quat_axis_angle(t3_vec3_normalize(t3_vec3_cross(s, t)), cw_acos(d));
}
t3_quat tr_quat_from_basis(t3_vec3 x, t3_vec3 y, t3_vec3 z) {
  t3_mat4 m;
  t3_mat4_identity(&m);
  m.e[0] = x.x, m.e[1] = x.y, m.e[2] = x.z;
  m.e[4] = y.x, m.e[5] = y.y, m.e[6] = y.z;
  m.e[8] = z.x, m.e[9] = z.y, m.e[10] = z.z;
  return t3_quat_normalize(t3_quat_from_rotation_matrix(&m));
}

static void mul16(double *r, const double *a, const double *b) { /* r = a * b, column-major */
  double t[16];
  for (int c = 0; c < 4; c++)
    for (int rr = 0; rr < 4; rr++) {
      double s = 0;
      for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k];
      t[c * 4 + rr] = s;
    }
  memcpy(r, t, sizeof t);
}
static void rot16(double *m, char axis, float deg) {
  double s = sin(deg * 0.017453292519943295), c = cos(deg * 0.017453292519943295);
  memset(m, 0, 16 * sizeof *m);
  m[0] = m[5] = m[10] = m[15] = 1;
  if (axis == 'x') m[5] = c, m[9] = -s, m[6] = s, m[10] = c;
  else if (axis == 'y') m[0] = c, m[8] = s, m[2] = -s, m[10] = c;
  else m[0] = c, m[4] = -s, m[1] = s, m[5] = c;
}

static const char *const JOINT_NAMES[J_COUNT] = { "neck", "head", "tail", "left_shoulder", "right_shoulder", "left_hip", "right_hip",
                                                  "left_knee", "right_knee", "left_ankle", "right_ankle" };

bool tux_load(tux_model *m, t3_scene *scene) {
  memset(m, 0, sizeof *m);
  for (int j = 0; j < J_COUNT; j++) m->joint[j] = -1;
  size_t len;
  char *txt = cw_asset_load("model/tux.txt", &len);
  if (!txt) { cw_error("model/tux.txt missing"); return false; }
  t3_geometry *geo = t3_sphere_geometry_new(1, 28, 18);
  char *save = NULL;
  for (char *line = strtok_r(txt, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
    if (!strncmp(line, "material ", 9)) {
      char name[48];
      float d[3], s[3], exp;
      if (sscanf(line + 9, "%47s %f %f %f %f %f %f %f", name, &d[0], &d[1], &d[2], &s[0], &s[1], &s[2], &exp) != 8) continue;
      if (m->mat_count >= TUX_MAX_MATERIALS) continue;
      /* tux.tcl's diffuse colours were tuned for fixed-function GL, where a 0.58 grey
       * under a bright sun clamps to white: as PBR albedo they read as two greys.
       * The plumage gets the albedo the look implies; the beak, feet and eyes keep theirs. */
      if (!strcmp(name, "white_penguin")) d[0] = d[1] = d[2] = 0.92f;
      if (!strcmp(name, "black_penguin")) d[0] = d[1] = d[2] = 0.035f;
      uint32_t rgb = ((uint32_t)(d[0] * 255 + 0.5f) << 16) | ((uint32_t)(d[1] * 255 + 0.5f) << 8) | (uint32_t)(d[2] * 255 + 0.5f);
      t3_material *mat = t3_mesh_physical_material_new(rgb);
      /* the Phong exponent as roughness: 5 -> 0.8, 90 -> 0.25 */
      mat->roughness = exp >= 80 ? 0.25f : exp >= 40 ? 0.45f : exp >= 15 ? 0.6f : 0.8f;
      mat->metalness = 0;
      if (!strcmp(name, "black_penguin") || !strcmp(name, "white_penguin")) { mat->sheen = 0.35f; mat->sheen_roughness = 0.8f; mat->sheen_color = (t3_color){ 0.6f, 0.6f, 0.65f }; }
      if (!strcmp(name, "beak_colour")) { mat->clearcoat = 0.6f; mat->clearcoat_roughness = 0.3f; }
      if (!strcmp(name, "iris_colour")) { mat->clearcoat = 1; mat->clearcoat_roughness = 0.05f; }
      m->mats[m->mat_count++] = mat;
    } else if (!strncmp(line, "node ", 5)) {
      int idx, parent, mat, shadow, n = 0;
      char kind[16];
      if (sscanf(line + 5, "%d %d %15s %d %d%n", &idx, &parent, kind, &mat, &shadow, &n) != 5) continue;
      if (idx < 0 || idx >= TUX_MAX_NODES || idx != m->count) { cw_error("tux.txt: node %d out of order", idx); continue; }
      tux_node *nd = &m->nodes[idx];
      nd->parent = parent, nd->material = mat, nd->sphere = !strcmp(kind, "sphere"), nd->shadow = shadow != 0;
      const char *p = line + 5 + n;
      for (int k = 0; k < 16; k++) { char *end; nd->base[k] = strtod(p, &end); p = end; }
      memcpy(nd->cur, nd->base, sizeof nd->cur);
      if (nd->sphere) {
        t3_material *mat_p = mat >= 0 && mat < m->mat_count ? m->mats[mat] : m->mats[0];
        t3_mesh *mesh = t3_mesh_new(geo, mat_p);
        mesh->base.cast_shadow = nd->shadow;
        mesh->base.receive_shadow = true;
        nd->obj = (t3_object *)mesh;
      } else {
        nd->obj = t3_group_new();
      }
      if (parent >= 0 && parent < idx) t3_object_add(m->nodes[parent].obj, nd->obj);
      else m->root = nd->obj;
      if (parent >= 0) { nd->obj->matrix_auto_update = false; t3_object_set_matrix_d(nd->obj, nd->base); }
      m->count++;
    } else if (!strncmp(line, "joint ", 6)) {
      char name[32];
      int idx;
      if (sscanf(line + 6, "%31s %d", name, &idx) != 2) continue;
      for (int j = 0; j < J_COUNT; j++)
        if (!strcmp(name, JOINT_NAMES[j]) && idx >= 0 && idx < m->count) m->joint[j] = idx;
    }
  }
  free(txt);
  if (!m->root) { cw_error("tux.txt: no root node"); return false; }
  t3_object_add((t3_object *)scene, m->root);
  if (!getenv("TUXRACER_SPHERES")) tux_skin_build(m); /* dev: TUXRACER_SPHERES=1 keeps the original spheres */
  cw_info("tux: %d nodes, %d materials", m->count, m->mat_count);
  return true;
}

void tux_unload(tux_model *m) {
  if (m->root && m->root->parent) t3_object_remove(m->root->parent, m->root);
  memset(m, 0, sizeof *m);
}

void tux_set_transform(tux_model *m, t3_vec3 pos, t3_quat q) {
  if (!m->root) return;
  t3_object_set_position(m->root, pos.x, pos.y, pos.z);
  t3_object_set_quaternion(m->root, q);
}
void tux_set_visible(tux_model *m, bool visible) { if (m->root) m->root->visible = visible; }
/* the sun's cascades: on, the spheres tux.tcl marks cast; off, only the contact decal shadows him */
void tux_set_cast_shadow(tux_model *m, bool on) {
  for (int i = 0; i < m->count; i++)
    if (m->nodes[i].sphere && m->nodes[i].obj) ((t3_mesh *)m->nodes[i].obj)->base.cast_shadow = on && m->nodes[i].shadow;
  if (m->skin) m->skin->mesh.base.cast_shadow = on;
}
/* squash > 0 flattens him along his belly axis (local z) and widens him; < 0 stretches */
void tux_set_squash(tux_model *m, float squash) {
  if (!m->root) return;
  float s = squash < -0.3f ? -0.3f : squash > 0.45f ? 0.45f : squash;
  t3_object_set_scale(m->root, 1 + s * 0.5f, 1 + s * 0.15f, 1 - s);
}

void tux_joint_reset(tux_model *m, int joint) {
  int i = m->joint[joint];
  if (i < 0) return;
  memcpy(m->nodes[i].cur, m->nodes[i].base, sizeof m->nodes[i].cur);
  t3_object_set_matrix_d(m->nodes[i].obj, m->nodes[i].cur);
}
void tux_joint_rotate(tux_model *m, int joint, char axis, float deg) {
  int i = m->joint[joint];
  if (i < 0) return;
  double r[16];
  rot16(r, axis, deg);
  mul16(m->nodes[i].cur, m->nodes[i].cur, r);
  t3_object_set_matrix_d(m->nodes[i].obj, m->nodes[i].cur);
}

#define MAX_ARM_ANGLE 30.0f
#define MAX_PADDLING_ANGLE 35.0f
#define MAX_EXT_PADDLING_ANGLE 30.0f
#define MAX_KICK_PADDLING_ANGLE 20.0f
static float fminf_(float a, float b) { return a < b ? a : b; }
static float fmaxf_(float a, float b) { return a > b ? a : b; }

void tux_pose_slide(tux_model *m, float turn_fact, bool braking, float paddling_factor, float speed, t3_vec3 local_force, float flap_factor) {
  float braking_angle = braking ? MAX_ARM_ANGLE : 0;
  float paddling_angle = MAX_PADDLING_ANGLE * sinf(paddling_factor * 3.14159265f);
  float ext_paddling_angle = MAX_EXT_PADDLING_ANGLE * sinf(paddling_factor * 3.14159265f);
  float kick_paddling_angle = MAX_KICK_PADDLING_ANGLE * sinf(paddling_factor * 3.14159265f * 2);
  float turning_angle[2] = { fmaxf_(-turn_fact, 0) * MAX_ARM_ANGLE, fmaxf_(turn_fact, 0) * MAX_ARM_ANGLE };
  float flap_angle = MAX_ARM_ANGLE * (0.5f + 0.5f * sinf(3.14159265f * flap_factor * 6 - 3.14159265f / 2));
  tux_joint_reset(m, J_LSHOULDER);
  tux_joint_reset(m, J_RSHOULDER);
  tux_joint_rotate(m, J_LSHOULDER, 'z', fminf_(braking_angle + paddling_angle + turning_angle[0], MAX_ARM_ANGLE) + flap_angle);
  tux_joint_rotate(m, J_RSHOULDER, 'z', fminf_(braking_angle + paddling_angle + turning_angle[1], MAX_ARM_ANGLE) + flap_angle);
  tux_joint_rotate(m, J_LSHOULDER, 'y', -ext_paddling_angle);
  tux_joint_rotate(m, J_RSHOULDER, 'y', ext_paddling_angle);
  float force_angle = fmaxf_(-20, fminf_(20, -local_force.z / 300));
  float turn_leg_angle = turn_fact * 10;
  tux_joint_reset(m, J_LHIP);
  tux_joint_rotate(m, J_LHIP, 'z', -20 + turn_leg_angle + force_angle);
  tux_joint_reset(m, J_RHIP);
  tux_joint_rotate(m, J_RHIP, 'z', -20 - turn_leg_angle + force_angle);
  tux_joint_reset(m, J_LKNEE);
  tux_joint_rotate(m, J_LKNEE, 'z', -10 + turn_leg_angle - fminf_(35, speed) + kick_paddling_angle + force_angle);
  tux_joint_reset(m, J_RKNEE);
  tux_joint_rotate(m, J_RKNEE, 'z', -10 - turn_leg_angle - fminf_(35, speed) - kick_paddling_angle + force_angle);
  tux_joint_reset(m, J_LANKLE);
  tux_joint_rotate(m, J_LANKLE, 'z', -20 + fminf_(50, speed));
  tux_joint_reset(m, J_RANKLE);
  tux_joint_rotate(m, J_RANKLE, 'z', -20 + fminf_(50, speed));
  tux_joint_reset(m, J_TAIL);
  tux_joint_rotate(m, J_TAIL, 'z', turn_fact * 20);
  tux_joint_reset(m, J_NECK);
  tux_joint_rotate(m, J_NECK, 'z', -50);
  tux_joint_reset(m, J_HEAD);
  tux_joint_rotate(m, J_HEAD, 'z', -30);
  tux_joint_rotate(m, J_HEAD, 'y', -turn_fact * 70);
}

int tux_load_walk(tux_keyframe *out, int cap) {
  size_t len;
  char *txt = cw_asset_load("model/tux_walk.txt", &len);
  if (!txt) return 0;
  int n = 0;
  char *save = NULL;
  for (char *line = strtok_r(txt, "\n", &save); line && n < cap; line = strtok_r(NULL, "\n", &save)) {
    tux_keyframe *k = &out[n];
    if (sscanf(line, "%f %f %f %f %f %f %f %f %f %f", &k->time, &k->pos[0], &k->pos[1], &k->pos[2], &k->yaw, &k->pitch, &k->l_shoulder, &k->r_shoulder, &k->l_hip, &k->r_hip) == 10) n++;
  }
  free(txt);
  return n;
}
