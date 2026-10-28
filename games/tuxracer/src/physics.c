/* Tux on Box3D: a rotation-locked dynamic sphere in the tree's 3D world,
 * driven each tick by the force model of the original phys_sim.c (friction
 * rotated for steering, brake, paddle, air drag and wind, jump and flap).
 * Box3D supplies gravity, the terrain contact and tree collisions; the
 * original's "glute spring" is replaced by the contact's normal load. */
#include "game.h"

#include <math.h>
#include <stdlib.h>

#include "cartwheel/core.h"
#include "cartwheel/math.h"
#include "cartwheel/physics_nodes.h"

#define EARTH_GRAV 9.81f
#define MIN_TUX_SPEED 1.4f
#define MIN_FRICTION_SPEED 2.8f
#define INIT_TUX_SPEED 3.0f
#define MAX_FRICTIONAL_FORCE 800.0f
#define MAX_TURN_ANGLE 45.0f
#define MAX_TURN_PERPENDICULAR_FORCE 400.0f
#define MAX_TURN_PENALTY 0.15f
#define BRAKE_FORCE 200.0f
#define MAX_ROLL_ANGLE 30.0f
#define BRAKING_ROLL_ANGLE 55.0f
#define IDEAL_ROLL_FRIC_COEFF 0.35f
#define IDEAL_ROLL_SPEED 6.0f
#define TUX_ORIENTATION_TIME_CONSTANT 0.14f
#define TUX_ORIENTATION_AIRBORNE_TIME_CONSTANT 0.5f
#define AIR_DENSITY 1.308f
#define TUX_DIAMETER 0.45f
#define AIR_VISCOSITY 17.00e-6f
#define PADDLING_DURATION 0.40f
#define MAX_PADDLING_FORCE 122.5f
#define MAX_PADDLING_SPEED (60.0f / 3.6f)
#define IDEAL_PADDLING_FRIC_COEFF 0.35f
#define BASE_JUMP_G_FORCE 1.5f
#define MAX_JUMP_G_FORCE 3.0f
#define JUMP_FORCE_DURATION 0.20f
#define MAX_JUMP_AMT 1.0f
#define ROLL_DECAY_TIME_CONSTANT 0.2f
#define GROUND_TOLERANCE 0.06f /* metres the sphere may hover above the triangle and still count as grounded */

float tux_radius = 0.24f;

static const float AIR_LOG_RE[] = { -1, 0, 1, 2, 3, 4, 5, 6 };
static const float AIR_LOG_DRAG[] = { 2.25f, 1.35f, 0.6f, 0.0f, -0.35f, -0.45f, -0.33f, -0.9f };
static const t3_vec3 WIND_VEL = { 250.0f / 3.6f, 0, 0 };

static float lin_interp(const float *xs, const float *ys, float x, int n) {
  if (x <= xs[0]) return ys[0];
  if (x >= xs[n - 1]) return ys[n - 1];
  int i = 0;
  while (i < n - 2 && x > xs[i + 1]) i++;
  float t = (x - xs[i]) / (xs[i + 1] - xs[i]);
  return ys[i] + (ys[i + 1] - ys[i]) * t;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static t3_vec3 wind_force(tux_phys *p, t3_vec3 vel) {
  t3_vec3 total = t3_vec3_scale(vel, -1);
  if (p->windy) {
    p->wind_scale = clampf(p->wind_scale + (cw_rng_float(&p->rng) - 0.5f) * 0.15f, 0, 1);
    total = t3_vec3_add(total, t3_vec3_scale(WIND_VEL, p->wind_scale));
  }
  float speed = t3_vec3_length(total);
  if (speed < 1e-4f) return (t3_vec3){ 0, 0, 0 };
  t3_vec3 dir = t3_vec3_scale(total, 1 / speed);
  float re = AIR_DENSITY * speed * TUX_DIAMETER / AIR_VISCOSITY;
  float drag = cw_pow(10, lin_interp(AIR_LOG_RE, AIR_LOG_DRAG, cw_ln(re) / 2.302585093f, 8));
  float mag = 0.5f * drag * AIR_DENSITY * speed * speed * (3.14159265f * TUX_DIAMETER * TUX_DIAMETER * 0.25f);
  return t3_vec3_scale(dir, mag);
}

/* adjust_surf_nml_for_roll: the surface normal leaned into the turn */
static t3_vec3 surf_nml_for_roll(const tux_phys *p, t3_vec3 vel, float fric, t3_vec3 nml) {
  float speed = t3_vec3_length(vel);
  if (speed < 1e-4f) return nml;
  t3_vec3 v = t3_vec3_normalize(tr_project_into_plane(nml, t3_vec3_scale(vel, 1 / speed)));
  float roll = p->braking ? BRAKING_ROLL_ANGLE : MAX_ROLL_ANGLE;
  float angle = p->turn_fact * roll * clampf(fric / IDEAL_ROLL_FRIC_COEFF, 0, 1) * clampf((speed - MIN_TUX_SPEED) / (IDEAL_ROLL_SPEED - MIN_TUX_SPEED), 0, 1);
  return tr_rotate_about(nml, v, angle);
}

/* the forces of one tick, applied before the world steps (calc_net_force) */
static void pre_step(cw_tree *t, double ddt, void *user) {
  (void)t;
  tux_phys *p = user;
  if (!p->enabled || !cw_body3d_valid(p->body)) return;
  float dt = (float)ddt;
  p->time += dt;
  const course *c = p->course;
  cw_xform3 x = cw_body3d_transform(p->body);
  t3_vec3 pos = x.p, vel = cw_body3d_velocity(p->body);
  float speed = t3_vec3_length(vel);

  /* the surface under the sphere's lowest point */
  float h = course_height(c, pos.x, pos.z);
  t3_vec3 orig_nml = course_normal(c, pos.x, pos.z);
  float dist = t3_vec3_dot(t3_vec3_sub(pos, (t3_vec3){ pos.x, h, pos.z }), orig_nml) - TUX_RADIUS;
  bool grounded = dist <= GROUND_TOLERANCE;
  p->dist_from_surface = dist;
  p->airborne = !grounded;
  course_surface(c, pos.x, pos.z, p->surf_w);
  float fric = 0;
  for (int i = 0; i < SURF_KINDS; i++) fric += p->surf_w[i] * c->friction[i];
  p->fric_coeff = fric;
  t3_vec3 nml = surf_nml_for_roll(p, vel, fric, orig_nml);
  p->surf_nml = orig_nml;

  t3_vec3 force = { 0, 0, 0 };
  /* the normal load the contact carries: gravity's share into the surface */
  float nml_mag = grounded ? fmaxf(0, EARTH_GRAV * TUX_MASS * orig_nml.y) : 0;

  /* jump */
  if (p->begin_jump) {
    p->begin_jump = false;
    if (grounded) { p->jumping = true; p->jump_start = p->time; }
    else p->jumping = false;
  }
  if (p->jumping && p->time - p->jump_start < JUMP_FORCE_DURATION) {
    force.y += BASE_JUMP_G_FORCE * TUX_MASS * EARTH_GRAV + p->jump_amt * (MAX_JUMP_G_FORCE - BASE_JUMP_G_FORCE) * TUX_MASS * EARTH_GRAV;
  } else {
    p->jumping = false;
  }

  /* friction, steering and braking */
  t3_vec3 fric_dir = speed > 1e-4f ? t3_vec3_scale(vel, -1 / speed) : (t3_vec3){ 0, 0, 1 };
  if (grounded && speed > MIN_FRICTION_SPEED) {
    float fric_mag = fminf(MAX_FRICTIONAL_FORCE, nml_mag * fric);
    t3_vec3 fric_f = t3_vec3_scale(fric_dir, fric_mag);
    float steer = p->turn_fact * MAX_TURN_ANGLE;
    if (fabsf(fric_mag * cw_sin(TR_DEG(steer))) > MAX_TURN_PERPENDICULAR_FORCE && fric_mag > 0)
      steer = TR_RAD2DEG(cw_asin(MAX_TURN_PERPENDICULAR_FORCE / fric_mag)) * (p->turn_fact < 0 ? -1 : 1);
    fric_f = tr_rotate_about(fric_f, orig_nml, steer);
    fric_f = t3_vec3_scale(fric_f, 1 + MAX_TURN_PENALTY);
    force = t3_vec3_add(force, fric_f);
    if (speed > MIN_TUX_SPEED && p->braking) force = t3_vec3_add(force, t3_vec3_scale(fric_dir, fric * BRAKE_FORCE));
  }
  (void)nml;

  /* air and wind */
  force = t3_vec3_add(force, wind_force(p, vel));

  /* paddling: a 0.4 s burst while held (racing.c + update_paddling) */
  if (p->paddle_held && !p->paddling) { p->paddling = true; p->paddle_time = p->time; }
  if (p->paddling && p->time - p->paddle_time >= PADDLING_DURATION) p->paddling = false;
  if (p->paddling) {
    if (!grounded) {
      t3_vec3 flap = t3_vec3_apply_quat((t3_vec3){ 0, 0, -TUX_MASS * EARTH_GRAV / 4 }, p->orientation);
      force = t3_vec3_add(force, flap);
    } else {
      float mag = fminf(MAX_PADDLING_FORCE, MAX_PADDLING_FORCE * (MAX_PADDLING_SPEED - speed) / MAX_PADDLING_SPEED * fminf(1, fric / IDEAL_PADDLING_FRIC_COEFF));
      force = t3_vec3_add(force, t3_vec3_scale(fric_dir, -mag));
    }
  }

  /* jump charge (calc_jump_amt) */
  if (p->charge_held && !p->jump_charging && !p->jumping) { p->jump_charging = true; p->jump_start = p->time; }
  if (p->jump_charging) p->jump_amt = fminf(MAX_JUMP_AMT, p->time - p->jump_start);
  else if (p->jumping) p->jump_amt *= 1 - (p->time - p->jump_start) / JUMP_FORCE_DURATION;
  else p->jump_amt = 0;
  if (!p->charge_held && p->jump_charging) { p->jump_charging = false; p->begin_jump = true; }

  p->net_force = t3_vec3_add(force, (t3_vec3){ 0, -EARTH_GRAV * TUX_MASS + nml_mag, 0 });
  cw_body3d_apply_force(p->body, force, NULL);
  static int trace = -1;
  if (trace < 0) trace = getenv("TUXRACER_TRACE") != NULL;
  if (trace) {
    t3_vec3 vdir = speed > 1e-4f ? t3_vec3_scale(vel, 1 / speed) : (t3_vec3){ 0, 0, 0 };
    float along = t3_vec3_dot(force, vdir) + t3_vec3_dot((t3_vec3){ 0, -EARTH_GRAV * TUX_MASS, 0 }, vdir);
    cw_info("trace t=%.2f pos %.1f %.1f %.1f speed %.2f dist %.3f grounded %d slope %.0f fric %.2f nml %.0f F.v %.0f (fric %.0f grav %.0f) contacts %d",
            p->time, pos.x, pos.y, pos.z, speed, dist, grounded, TR_RAD2DEG(cw_acos(orig_nml.y)), fric, nml_mag, along,
            grounded && speed > MIN_FRICTION_SPEED ? -fminf(MAX_FRICTIONAL_FORCE, nml_mag * fric) * (1 + MAX_TURN_PENALTY) : 0.0f,
            t3_vec3_dot((t3_vec3){ 0, -EARTH_GRAV * TUX_MASS, 0 }, vdir), p->contact_count);
  }
}

void physics_init(tux_phys *p, cw_tree *tree, const course *c) {
  memset(p, 0, sizeof *p);
  const char *r = getenv("TUXRACER_RADIUS");
  if (r && atof(r) > 0.05) tux_radius = (float)atof(r);
  p->course = c;
  p->world = cw_physics3d_world(tree);
  cw_world3d_set_gravity(p->world, (t3_vec3){ 0, -EARTH_GRAV, 0 });
  cw_body3d_def bd = cw_body3d_default_def();
  bd.position = (t3_vec3){ c->start_x, course_height(c, c->start_x, -c->start_d) + TUX_RADIUS, -c->start_d };
  bd.lock_rotation = true;
  bd.bullet = true;
  bd.sleep = false;
  bd.name = "tux";
  p->body = cw_body3d_create(p->world, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.material.friction = 0;
  sd.material.restitution = 0;
  sd.density = TUX_MASS / (4.0f / 3.0f * 3.14159265f * TUX_RADIUS * TUX_RADIUS * TUX_RADIUS);
  sd.hit_events = true;
  p->shape = cw_shape3d_sphere(p->body, &sd, (t3_vec3){ 0, 0, 0 }, TUX_RADIUS);
  cw_rng_seed(&p->rng, 0x7a58u);
  p->wind_scale = 0.5f;
  p->orientation = t3_quat_identity();
  cw_tree_add_hook(tree, 0, 50, pre_step, p); /* after the nodes, before the 3D world steps (101) */
  physics_place(p, bd.position, true);
}

void physics_place(tux_phys *p, t3_vec3 pos, bool start_speed) {
  const course *c = p->course;
  t3_vec3 nml = course_normal(c, pos.x, pos.z);
  /* init_physical_simulation: downhill along the slope at INIT_TUX_SPEED */
  t3_vec3 v = start_speed ? t3_vec3_scale(t3_vec3_normalize(tr_rotate_about(nml, (t3_vec3){ 1, 0, 0 }, -90)), INIT_TUX_SPEED) : (t3_vec3){ 0, 0, 0 };
  cw_body3d_set_transform(p->body, cw_xform3_at(pos.x, pos.y, pos.z));
  cw_body3d_set_velocity(p->body, v);
  cw_body3d_set_awake(p->body, true);
  p->pos = pos, p->vel = v, p->speed = INIT_TUX_SPEED;
  p->surf_nml = nml;
  p->orient_init = false;
  p->orientation = t3_quat_identity();
  p->turn_fact = p->turn_anim = 0;
  p->braking = p->paddling = p->jump_charging = p->jumping = p->begin_jump = false;
  p->jump_amt = 0;
  p->airborne = false;
}

void physics_control(tux_phys *p, float turn, bool brake, bool paddle, bool charge) {
  p->turn_fact = clampf(turn, -1, 1);
  p->braking = brake;
  p->paddle_held = paddle;
  p->charge_held = charge;
}

void physics_after_step(tux_phys *p, float dt) {
  if (!cw_body3d_valid(p->body)) return;
  cw_xform3 x = cw_body3d_transform(p->body);
  t3_vec3 pos = x.p, vel = cw_body3d_velocity(p->body);
  const course *c = p->course;
  float h = course_height(c, pos.x, pos.z);
  t3_vec3 nml = course_normal(c, pos.x, pos.z);
  float dist = t3_vec3_dot(t3_vec3_sub(pos, (t3_vec3){ pos.x, h, pos.z }), nml) - TUX_RADIUS;
  bool grounded = dist <= GROUND_TOLERANCE;
  float speed = t3_vec3_length(vel);
  /* adjust_for_tree_collision: at least 30% of the speed goes on a tree hit, more head-on */
  cw_contact3d contacts[8];
  int nc = cw_body3d_contacts(p->body, contacts, 8);
  p->contact_count = nc;
  bool tree_hit = false;
  for (int i = 0; i < nc; i++) if (cw_shape3d_user(contacts[i].other_shape) == (void *)1) tree_hit = true;
  p->tree_hit_now = tree_hit && !p->in_tree;
  if (tree_hit && !p->in_tree && speed > 1e-3f) {
    vel = t3_vec3_scale(vel, 0.7f);
    speed *= 0.7f;
    cw_body3d_set_velocity(p->body, vel);
  }
  p->in_tree = tree_hit;
  if (p->enabled && grounded) {
    /* adjust_velocity: never slower than the minimum, downhill when stopped */
    if (speed < 1e-3f) {
      t3_vec3 down = tr_project_into_plane(nml, (t3_vec3){ 0, -1, 0 });
      vel = t3_vec3_length_sq(down) > 1e-6f ? t3_vec3_normalize(down) : (t3_vec3){ 0, 0, -1 };
      speed = 1;
    }
    if (speed < MIN_TUX_SPEED) {
      vel = t3_vec3_scale(vel, MIN_TUX_SPEED / speed);
      speed = MIN_TUX_SPEED;
      cw_body3d_set_velocity(p->body, vel);
    }
  }
  /* phys_sim.c set_tux_pos: the play area's sides and its top are walls (the
   * position is clamped every step); past its bottom the race is over
   * (course_finished). Without this the sphere leaves the height field at
   * the sides and free-falls for ever. A fall through the ground (a tunnel
   * through a steep bank, a host hiccup) is put back on the surface. */
  {
    float bw = (c->width - c->play_width) * 0.5f, lo = bw, hi = c->width - bw;
    bool moved = false;
    if (pos.x < lo) { pos.x = lo; if (vel.x < 0) vel.x = 0; moved = true; }
    else if (pos.x > hi) { pos.x = hi; if (vel.x > 0) vel.x = 0; moved = true; }
    if (pos.z > 0) { pos.z = 0; if (vel.z > 0) vel.z = 0; moved = true; }
    float floor_y = course_height(c, pos.x, pos.z);
    if (pos.y < floor_y - 2.0f) { pos.y = floor_y + TUX_RADIUS; vel.y = 0; moved = true; }
    if (moved) {
      cw_body3d_set_transform(p->body, cw_xform3_at(pos.x, pos.y, pos.z));
      cw_body3d_set_velocity(p->body, vel);
      speed = t3_vec3_length(vel);
    }
  }
  p->pos = pos, p->vel = vel, p->speed = speed;
  p->surf_nml = nml;
  p->airborne = !grounded;
  p->dist_from_surface = dist;

  /* turn animation (racing.c) */
  if (p->turn_fact != 0) p->turn_anim = clampf(p->turn_anim + (p->turn_fact < 0 ? -1 : 1) * 0.15f * dt / 0.05f, -1, 1);
  else p->turn_anim *= dt < ROLL_DECAY_TIME_CONSTANT ? 1 - dt / ROLL_DECAY_TIME_CONSTANT : 0;

  /* adjust_orientation: y along the velocity, z into the surface, rolled for the turn */
  t3_vec3 new_y, new_z;
  t3_vec3 vdir = speed > 1e-4f ? t3_vec3_scale(vel, 1 / speed) : (t3_vec3){ 0, 0, -1 };
  float roll = (p->braking ? BRAKING_ROLL_ANGLE : MAX_ROLL_ANGLE) * p->turn_fact;
  if (!grounded) {
    new_y = vdir;
    new_z = t3_vec3_normalize(tr_project_into_plane(new_y, (t3_vec3){ 0, -1, 0 }));
    if (t3_vec3_length_sq(new_z) < 1e-6f) new_z = (t3_vec3){ 0, 0, 1 };
    new_z = tr_rotate_about(new_z, t3_vec3_normalize(tr_project_into_plane(new_z, vdir)), roll);
  } else {
    new_z = t3_vec3_scale(nml, -1);
    t3_vec3 vp = tr_project_into_plane(new_z, vdir);
    if (t3_vec3_length_sq(vp) > 1e-6f) new_z = tr_rotate_about(new_z, t3_vec3_normalize(vp), roll);
    new_y = t3_vec3_normalize(tr_project_into_plane(nml, vdir));
    if (t3_vec3_length_sq(new_y) < 1e-6f) new_y = (t3_vec3){ 0, 0, -1 };
  }
  t3_vec3 new_x = t3_vec3_cross(new_y, new_z);
  t3_quat target = tr_quat_from_basis(new_x, new_y, new_z);
  if (!p->orient_init) { p->orientation = target; p->orient_init = true; }
  float tc = grounded ? TUX_ORIENTATION_TIME_CONSTANT : TUX_ORIENTATION_AIRBORNE_TIME_CONSTANT;
  p->orientation = tr_slerp(p->orientation, target, fminf(dt / tc, 1));
}
