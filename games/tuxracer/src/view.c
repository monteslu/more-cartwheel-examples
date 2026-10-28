/* The camera, from the original view.c: BEHIND (a camera on a string),
 * FOLLOW (above and behind) and ABOVE (always uphill), second-order
 * filtered toward their targets, kept above the terrain. */
#include "game.h"

#include <math.h>

#define MIN_CAMERA_HEIGHT 1.5f
#define ABSOLUTE_MIN_CAMERA_HEIGHT 0.3f
#define CAMERA_DISTANCE 4.0f
#define CAMERA_ANGLE_ABOVE_SLOPE 10.0f
#define PLAYER_ANGLE_IN_CAMERA 20.0f
#define MAX_CAMERA_PITCH 40.0f
#define BEHIND_ORBIT_TIME_CONSTANT 0.06f
#define BEHIND_ORIENT_TIME_CONSTANT 0.06f
#define FOLLOW_ORBIT_TIME_CONSTANT 0.06f
#define FOLLOW_ORIENT_TIME_CONSTANT 0.06f
#define MAX_INTERPOLATION_VALUE 0.3f
#define BASELINE_INTERPOLATION_SPEED 4.5f
#define NO_INTERPOLATION_SPEED 2.0f

static const t3_vec3 Y = { 0, 1, 0 };

static t3_vec3 interpolate_view_pos(t3_vec3 plyr1, t3_vec3 plyr2, float max_angle, t3_vec3 pos1, t3_vec3 pos2, float dist, float dt, float tc) {
  t3_vec3 v1 = t3_vec3_normalize(t3_vec3_sub(pos1, plyr1)), v2 = t3_vec3_normalize(t3_vec3_sub(pos2, plyr2));
  t3_quat q1 = tr_rotation_between(Y, v1), q2 = tr_rotation_between(Y, v2);
  float alpha = tc > 0 ? fminf(MAX_INTERPOLATION_VALUE, 1 - expf(-dt / tc)) : 0;
  t3_quat q = t3_quat_slerp(q1, q2, alpha);
  v2 = t3_vec3_apply_quat(Y, q);
  float theta = TR_RAD2DEG(3.14159265f / 2 - acosf(fmaxf(-1, fminf(1, t3_vec3_dot(v2, Y)))));
  if (theta > max_angle) {
    t3_vec3 axis = t3_vec3_cross(Y, v2);
    if (t3_vec3_length_sq(axis) > 1e-8f) v2 = tr_rotate_about(v2, axis, theta - max_angle);
  }
  return t3_vec3_add(plyr2, t3_vec3_scale(v2, dist));
}

static void interpolate_view_frame(t3_vec3 up1, t3_vec3 dir1, t3_vec3 *up2, t3_vec3 *dir2, float dt, float tc) {
  t3_vec3 z1 = t3_vec3_normalize(t3_vec3_scale(dir1, -1)), y1 = t3_vec3_normalize(tr_project_into_plane(z1, up1)), x1 = t3_vec3_cross(y1, z1);
  t3_vec3 z2 = t3_vec3_normalize(t3_vec3_scale(*dir2, -1)), y2 = t3_vec3_normalize(tr_project_into_plane(z2, *up2)), x2 = t3_vec3_cross(y2, z2);
  t3_quat q1 = tr_quat_from_basis(x1, y1, z1), q2 = tr_quat_from_basis(x2, y2, z2);
  float alpha = fminf(MAX_INTERPOLATION_VALUE, 1 - expf(-dt / tc));
  t3_quat q = t3_quat_slerp(q1, q2, alpha);
  *up2 = t3_vec3_apply_quat(Y, q);
  *dir2 = t3_vec3_scale(t3_vec3_apply_quat((t3_vec3){ 0, 0, 1 }, q), -1);
}

void view_reset(tux_view *v, view_mode mode) {
  v->mode = mode;
  v->initialized = false;
}

void view_update(tux_view *v, const tux_phys *p, const course *c, float dt) {
  if (v->mode == VIEW_FIXED) return; /* parked by the cam dev command */
  float speed = p->speed;
  float f = (speed - NO_INTERPOLATION_SPEED) / (BASELINE_INTERPOLATION_SPEED - NO_INTERPOLATION_SPEED);
  f = fmaxf(0, fminf(1, f));
  float tc_mult = f > 1e-4f ? 1 / f : 1e6f;
  t3_vec3 up = Y, dir;
  t3_vec3 vel_dir = speed > 1e-4f ? t3_vec3_scale(p->vel, 1 / speed) : (t3_vec3){ 0, 0, -1 };
  float a = TR_DEG(c->angle - CAMERA_ANGLE_ABOVE_SLOPE + PLAYER_ANGLE_IN_CAMERA);
  t3_vec3 view_vec = t3_vec3_scale((t3_vec3){ 0, sinf(a), cosf(a) }, CAMERA_DISTANCE);
  t3_vec3 view_pt;
  if (v->mode == VIEW_BEHIND || v->mode == VIEW_FOLLOW) {
    t3_vec3 vp = tr_project_into_plane(Y, vel_dir);
    if (t3_vec3_length_sq(vp) > 1e-8f) view_vec = t3_vec3_apply_quat(view_vec, tr_rotation_between((t3_vec3){ 0, 0, -1 }, t3_vec3_normalize(vp)));
    view_pt = t3_vec3_add(p->pos, view_vec);
    float y = course_height(c, view_pt.x, view_pt.z);
    if (view_pt.y < y + MIN_CAMERA_HEIGHT) view_pt.y = y + MIN_CAMERA_HEIGHT;
    if (v->initialized) {
      float tc = (v->mode == VIEW_BEHIND ? BEHIND_ORBIT_TIME_CONSTANT : FOLLOW_ORBIT_TIME_CONSTANT) * tc_mult;
      t3_vec3 ref = v->mode == VIEW_BEHIND ? p->pos : v->plyr_pos;
      for (int i = 0; i < 2; i++) view_pt = interpolate_view_pos(ref, p->pos, MAX_CAMERA_PITCH, v->pos, view_pt, CAMERA_DISTANCE, dt, tc);
    }
    y = course_height(c, view_pt.x, view_pt.z);
    if (view_pt.y < y + ABSOLUTE_MIN_CAMERA_HEIGHT) view_pt.y = y + ABSOLUTE_MIN_CAMERA_HEIGHT;
    view_vec = t3_vec3_sub(view_pt, p->pos);
    t3_vec3 axis = t3_vec3_cross(Y, view_vec);
    dir = t3_vec3_length_sq(axis) > 1e-8f ? t3_vec3_scale(tr_rotate_about(view_vec, axis, PLAYER_ANGLE_IN_CAMERA), -1) : t3_vec3_scale(view_vec, -1);
    if (v->initialized) {
      float tc = v->mode == VIEW_BEHIND ? BEHIND_ORIENT_TIME_CONSTANT : FOLLOW_ORIENT_TIME_CONSTANT;
      for (int i = 0; i < 2; i++) { interpolate_view_frame(v->up, v->dir, &up, &dir, dt, tc); up = Y; }
    }
  } else {
    view_pt = t3_vec3_add(p->pos, view_vec);
    float y = course_height(c, view_pt.x, view_pt.z);
    if (view_pt.y < y + MIN_CAMERA_HEIGHT) view_pt.y = y + MIN_CAMERA_HEIGHT;
    view_vec = t3_vec3_sub(view_pt, p->pos);
    dir = t3_vec3_scale(tr_rotate_about(view_vec, (t3_vec3){ 1, 0, 0 }, PLAYER_ANGLE_IN_CAMERA), -1);
  }
  v->pos = view_pt;
  v->dir = t3_vec3_normalize(dir);
  v->up = up;
  v->plyr_pos = p->pos;
  v->initialized = true;
}
