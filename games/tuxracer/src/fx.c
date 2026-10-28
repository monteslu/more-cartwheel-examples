/* Effects: the snow spray Tux throws up (the original's turn, roll and brake
 * particles, now GPU particles with its snowparticles texture), falling snow
 * for snowy races, and the particle texture is the original's. */
#include "game.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"

#define TRACK_DECALS 64
static struct {
  cw_node spray, turn_spray, powder, snow, streaks, motes;
  cw_node sparkle[4], treesnow[2]; /* one-shot bursts: a herring's sparkle, snow shaken off a hit tree */
  int sparkle_next, treesnow_next;
  bool snowing;
  cw_node track[TRACK_DECALS]; /* the original buttprint, laid behind the belly on snow */
  int track_next;
  t3_vec3 track_last;
} F;

static void track_init(cw_node parent);

static cw_node emitter(cw_node parent, const char *name, int amount, float life) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_set(n, "amount", cw_val_int(amount));
  cw_node_set(n, "lifetime", cw_val_float(life));
  if (!strcmp(name, "Snowfall")) cw_node_set(n, "texture", cw_val_string("original/common/snowparticles.png")); /* falling flakes keep the sprite; spray is powder: soft dots */
  cw_node_set(n, "blend", cw_val_enum("ALPHA"));
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "seed", cw_val_int(7));
  cw_node_add_child(parent, n);
  return n;
}

void fx_init(cw_node parent) {
  /* the spray: a fine fast stream of crystals (stretched) plus, on top of it, a slow powder plume of big
   * soft puffs that hangs in the air behind him; the turn spray throws a rooster tail to the outside */
  F.spray = emitter(parent, "Spray", 400, 0.7f);
  cw_node_set(F.spray, "emission_shape", cw_val_enum("SPHERE"));
  cw_node_set(F.spray, "emission_radius", cw_val_float(0.25f));
  cw_node_set(F.spray, "spread", cw_val_float(35));
  cw_node_set(F.spray, "speed_min", cw_val_float(2.5f));
  cw_node_set(F.spray, "speed_max", cw_val_float(6));
  cw_node_set(F.spray, "gravity", cw_val_vec3(0, -9.8f, 0));
  cw_node_set(F.spray, "size", cw_val_float(0.05f));
  cw_node_set(F.spray, "size_random", cw_val_float(0.6f));
  cw_node_set(F.spray, "color_ramp", cw_val_string("0 1 1 1 0.9, 0.6 1 1 1 0.5, 1 1 1 1 0"));
  cw_node_set(F.spray, "soft_distance", cw_val_float(0.3f));
  cw_node_set(F.spray, "draw_mode", cw_val_enum("STRETCHED"));
  cw_node_set(F.spray, "stretch", cw_val_float(0.014f));
  cw_node_set(F.spray, "collide_depth", cw_val_bool(true));
  cw_node_set(F.spray, "bounce", cw_val_float(0.25f));
  F.powder = cw_node_new("GPUParticles3D", "Powder"); /* no sprite: the engine's soft dot */
  cw_node_set(F.powder, "amount", cw_val_int(300));
  cw_node_set(F.powder, "lifetime", cw_val_float(1.4f));
  cw_node_set(F.powder, "blend", cw_val_enum("ALPHA"));
  cw_node_set(F.powder, "emitting", cw_val_bool(false));
  cw_node_set(F.powder, "seed", cw_val_int(11));
  cw_node_add_child(parent, F.powder);
  cw_node_set(F.powder, "emission_shape", cw_val_enum("SPHERE"));
  cw_node_set(F.powder, "emission_radius", cw_val_float(0.35f));
  cw_node_set(F.powder, "spread", cw_val_float(45));
  cw_node_set(F.powder, "speed_min", cw_val_float(0.8f));
  cw_node_set(F.powder, "speed_max", cw_val_float(2.2f));
  cw_node_set(F.powder, "gravity", cw_val_vec3(0, -0.9f, 0));
  cw_node_set(F.powder, "damping", cw_val_float(1.2f));
  cw_node_set(F.powder, "turbulence", cw_val_float(0.8f));
  cw_node_set(F.powder, "turbulence_scale", cw_val_float(0.6f));
  cw_node_set(F.powder, "size", cw_val_float(0.28f));
  cw_node_set(F.powder, "size_random", cw_val_float(0.4f));
  cw_node_set(F.powder, "size_curve", cw_val_string("0 0.4, 0.3 1, 1 1.6"));
  cw_node_set(F.powder, "color_ramp", cw_val_string("0 1 1 1 0.5, 0.4 1 1 1 0.3, 1 1 1 1 0"));
  cw_node_set(F.powder, "soft_distance", cw_val_float(0.6f));
  cw_node_set(F.powder, "spin", cw_val_float(0.8f));
  F.turn_spray = emitter(parent, "TurnSpray", 900, 0.9f);
  cw_node_set(F.turn_spray, "emission_shape", cw_val_enum("SPHERE"));
  cw_node_set(F.turn_spray, "emission_radius", cw_val_float(0.3f));
  cw_node_set(F.turn_spray, "spread", cw_val_float(40));
  cw_node_set(F.turn_spray, "speed_min", cw_val_float(4));
  cw_node_set(F.turn_spray, "speed_max", cw_val_float(10));
  cw_node_set(F.turn_spray, "gravity", cw_val_vec3(0, -9.8f, 0));
  cw_node_set(F.turn_spray, "size", cw_val_float(0.07f));
  cw_node_set(F.turn_spray, "size_random", cw_val_float(0.6f));
  cw_node_set(F.turn_spray, "color_ramp", cw_val_string("0 1 1 1 0.9, 0.5 1 1 1 0.5, 1 1 1 1 0"));
  cw_node_set(F.turn_spray, "soft_distance", cw_val_float(0.3f));
  cw_node_set(F.turn_spray, "draw_mode", cw_val_enum("STRETCHED"));
  cw_node_set(F.turn_spray, "stretch", cw_val_float(0.018f));
  cw_node_set(F.turn_spray, "collide_depth", cw_val_bool(true));
  cw_node_set(F.turn_spray, "bounce", cw_val_float(0.25f));
  track_init(parent);
  /* speed: snow dust whipping past the camera above ~50 km/h, thin streaks along the travel */
  F.streaks = emitter(parent, "Streaks", 500, 0.4f);
  cw_node_set(F.streaks, "emission_shape", cw_val_enum("BOX"));
  cw_node_set(F.streaks, "emission_extents", cw_val_vec3(4, 2.5f, 5));
  cw_node_set(F.streaks, "spread", cw_val_float(3));
  cw_node_set(F.streaks, "gravity", cw_val_vec3(0, 0, 0));
  cw_node_set(F.streaks, "size", cw_val_float(0.012f));
  cw_node_set(F.streaks, "size_random", cw_val_float(0.5f));
  cw_node_set(F.streaks, "draw_mode", cw_val_enum("STRETCHED"));
  cw_node_set(F.streaks, "stretch", cw_val_float(0.04f));
  cw_node_set(F.streaks, "color_ramp", cw_val_string("0 1 1 1 0, 0.25 1 1 1 0.55, 1 1 1 1 0"));
  cw_node_set(F.streaks, "texture", cw_val_string(""));
  /* a herring: a burst of golden sparkles that rise and fade; a tree hit: a clump of snow dropping off it */
  for (int i = 0; i < 4; i++) {
    cw_node n = cw_node_new("GPUParticles3D", "Sparkle");
    cw_node_set(n, "amount", cw_val_int(60));
    cw_node_set(n, "lifetime", cw_val_float(0.7f));
    cw_node_set(n, "lifetime_random", cw_val_float(0.4f));
    cw_node_set(n, "one_shot", cw_val_bool(true));
    cw_node_set(n, "explosiveness", cw_val_float(1));
    cw_node_set(n, "emitting", cw_val_bool(false));
    cw_node_set(n, "seed", cw_val_int(20 + i));
    cw_node_set(n, "emission_shape", cw_val_enum("SPHERE"));
    cw_node_set(n, "emission_radius", cw_val_float(0.35f));
    cw_node_set(n, "spread", cw_val_float(180));
    cw_node_set(n, "speed_min", cw_val_float(1.5f));
    cw_node_set(n, "speed_max", cw_val_float(4));
    cw_node_set(n, "gravity", cw_val_vec3(0, 2.5f, 0));
    cw_node_set(n, "damping", cw_val_float(2));
    cw_node_set(n, "size", cw_val_float(0.06f));
    cw_node_set(n, "size_random", cw_val_float(0.5f));
    cw_node_set(n, "size_curve", cw_val_string("0 1, 0.7 0.8, 1 0"));
    cw_node_set(n, "blend", cw_val_enum("ADD"));
    cw_node_set(n, "energy", cw_val_float(2.5f));
    cw_node_set(n, "color_ramp", cw_val_string("0 1 0.95 0.5 1, 0.5 1 0.75 0.2 0.8, 1 1 0.5 0.1 0"));
    cw_node_add_child(parent, n);
    F.sparkle[i] = n;
  }
  for (int i = 0; i < 2; i++) {
    cw_node n = cw_node_new("GPUParticles3D", "TreeSnow");
    cw_node_set(n, "amount", cw_val_int(120));
    cw_node_set(n, "lifetime", cw_val_float(1.3f));
    cw_node_set(n, "lifetime_random", cw_val_float(0.3f));
    cw_node_set(n, "one_shot", cw_val_bool(true));
    cw_node_set(n, "explosiveness", cw_val_float(0.8f));
    cw_node_set(n, "emitting", cw_val_bool(false));
    cw_node_set(n, "seed", cw_val_int(30 + i));
    cw_node_set(n, "emission_shape", cw_val_enum("BOX"));
    cw_node_set(n, "emission_extents", cw_val_vec3(0.9f, 0.7f, 0.9f));
    cw_node_set(n, "direction", cw_val_vec3(0, -1, 0));
    cw_node_set(n, "spread", cw_val_float(60));
    cw_node_set(n, "speed_min", cw_val_float(0.3f));
    cw_node_set(n, "speed_max", cw_val_float(1.5f));
    cw_node_set(n, "gravity", cw_val_vec3(0, -4, 0));
    cw_node_set(n, "turbulence", cw_val_float(0.6f));
    cw_node_set(n, "size", cw_val_float(0.09f));
    cw_node_set(n, "size_random", cw_val_float(0.6f));
    cw_node_set(n, "size_curve", cw_val_string("0 0.6, 0.2 1, 1 1.2"));
    cw_node_set(n, "blend", cw_val_enum("ALPHA"));
    cw_node_set(n, "color_ramp", cw_val_string("0 1 1 1 0.9, 0.6 1 1 1 0.6, 1 1 1 1 0"));
    cw_node_set(n, "soft_distance", cw_val_float(0.4f));
    cw_node_set(n, "collide_depth", cw_val_bool(true));
    cw_node_set(n, "bounce", cw_val_float(0.1f));
    cw_node_add_child(parent, n);
    F.treesnow[i] = n;
  }
  /* sunlit motes: fine snow dust hanging in the air around the camera, catching the sun */
  F.motes = cw_node_new("GPUParticles3D", "Motes");
  cw_node_set(F.motes, "amount", cw_val_int(900));
  cw_node_set(F.motes, "lifetime", cw_val_float(6));
  cw_node_set(F.motes, "lifetime_random", cw_val_float(0.5f));
  cw_node_set(F.motes, "preprocess", cw_val_float(6));
  cw_node_set(F.motes, "seed", cw_val_int(41));
  cw_node_set(F.motes, "emission_shape", cw_val_enum("BOX"));
  cw_node_set(F.motes, "emission_extents", cw_val_vec3(14, 5, 14));
  cw_node_set(F.motes, "direction", cw_val_vec3(0, -1, 0));
  cw_node_set(F.motes, "spread", cw_val_float(90));
  cw_node_set(F.motes, "speed_min", cw_val_float(0.05f));
  cw_node_set(F.motes, "speed_max", cw_val_float(0.25f));
  cw_node_set(F.motes, "gravity", cw_val_vec3(0, -0.05f, 0));
  cw_node_set(F.motes, "turbulence", cw_val_float(0.25f));
  cw_node_set(F.motes, "turbulence_scale", cw_val_float(0.3f));
  cw_node_set(F.motes, "size", cw_val_float(0.032f));
  cw_node_set(F.motes, "size_random", cw_val_float(0.6f));
  cw_node_set(F.motes, "blend", cw_val_enum("ADD"));
  cw_node_set(F.motes, "energy", cw_val_float(1.6f));
  cw_node_set(F.motes, "color_ramp", cw_val_string("0 1 0.98 0.9 0, 0.2 1 0.98 0.9 0.7, 0.8 1 0.98 0.9 0.7, 1 1 0.98 0.9 0"));
  cw_node_set(F.motes, "emitting", cw_val_bool(false));
  cw_node_add_child(parent, F.motes);
  F.snow = emitter(parent, "Snowfall", 6000, 12);
  cw_node_set(F.snow, "emission_shape", cw_val_enum("BOX"));
  cw_node_set(F.snow, "emission_extents", cw_val_vec3(45, 18, 45));
  cw_node_set(F.snow, "direction", cw_val_vec3(0, -1, 0));
  cw_node_set(F.snow, "spread", cw_val_float(10));
  cw_node_set(F.snow, "speed_min", cw_val_float(1.0f));
  cw_node_set(F.snow, "speed_max", cw_val_float(2.0f));
  cw_node_set(F.snow, "gravity", cw_val_vec3(0, -0.6f, 0));
  cw_node_set(F.snow, "damping", cw_val_float(0.4f));
  cw_node_set(F.snow, "turbulence", cw_val_float(1.5f));
  cw_node_set(F.snow, "turbulence_scale", cw_val_float(0.08f));
  cw_node_set(F.snow, "size", cw_val_float(0.1f));
  cw_node_set(F.snow, "size_random", cw_val_float(0.5f));
  cw_node_set(F.snow, "preprocess", cw_val_float(12));
  cw_node_set(F.snow, "soft_distance", cw_val_float(0.5f));
  cw_node_set(F.snow, "color_ramp", cw_val_string("0 1 1 1 0.9, 1 1 1 1 0.9"));
}

static void track_init(cw_node parent) {
  for (int i = 0; i < TRACK_DECALS; i++) {
    cw_node d = cw_node_new("Decal3D", "Track");
    cw_node_set(d, "texture", cw_val_string("original/common/buttprint.png"));
    cw_node_set(d, "blend", cw_val_enum("MULTIPLY"));
    cw_node_set(d, "size", cw_val_vec3(0.7f, 0.7f, 0.8f));
    cw_node_set(d, "color", cw_val_color(0.6f, 0.64f, 0.76f, 0.8f));
    cw_node_set(d, "normal_fade", cw_val_float(0.4f));
    cw_node_set(d, "visible", cw_val_bool(false));
    cw_node_add_child(parent, d);
    F.track[i] = d;
  }
}

void fx_track_reset(void) {
  for (int i = 0; i < TRACK_DECALS; i++) cw_node_set(F.track[i], "visible", cw_val_bool(false));
  F.track_last = (t3_vec3){ 1e9f, 0, 0 };
}

/* a print every 0.45 m of ground travel on mostly snow, facing down, turned along the velocity */
static void track_update(const tux_phys *p, bool racing) {
  if (!getenv("TUXRACER_BUTTPRINTS")) return; /* the groove in the snow shader is the trail now; the prints read as paving slabs */
  if (!racing || !p->enabled || p->airborne || p->speed < 2 || p->surf_w[SURF_SNOW] < 0.5f) return;
  t3_vec3 at = t3_vec3_add(p->pos, t3_vec3_scale(p->surf_nml, -TUX_RADIUS));
  if (t3_vec3_distance_sq(at, F.track_last) < 0.45f * 0.45f) return;
  F.track_last = at;
  cw_node d = F.track[F.track_next];
  F.track_next = (F.track_next + 1) % TRACK_DECALS;
  float yaw = atan2f(-p->vel.x, -p->vel.z);
  cw_node3d_set_position(d, t3_vec3_add(at, t3_vec3_scale(p->surf_nml, 0.25f)));
  cw_node3d_set_rotation(d, (t3_vec3){ -1.5707963f, 0, yaw });
  cw_node_set(d, "visible", cw_val_bool(true));
}

void fx_set_snowing(bool on) {
  F.snowing = on;
  cw_node_set(F.snow, "emitting", cw_val_bool(on));
}
/* the flakes drift with the course's wind (the original's wind blows along +x) */
void fx_set_wind(bool windy) {
  cw_node_set(F.snow, "direction", windy ? cw_val_vec3(0.9f, -1, 0) : cw_val_vec3(0, -1, 0));
  cw_node_set(F.snow, "speed_max", cw_val_float(windy ? 4.5f : 2.0f));
  cw_node_set(F.snow, "turbulence", cw_val_float(windy ? 3 : 1.5f));
}

/* phys_sim.c generate_particles: spray while turning, rolling and braking on
 * the ground above a few m/s; the snowfall box rides with the camera */
void fx_update(const tux_phys *p, t3_vec3 cam_pos, t3_vec3 cam_dir, bool racing) {
  /* the streak box rides 6 m ahead of the camera; the dust flies back along the travel at most of Tux's speed */
  bool fast = racing && p->enabled && p->speed > 14;
  cw_node_set(F.streaks, "emitting", cw_val_bool(fast));
  if (fast) {
    t3_vec3 d = t3_vec3_scale(p->vel, -1 / p->speed);
    cw_node3d_set_position(F.streaks, t3_vec3_add(cam_pos, t3_vec3_scale(cam_dir, 6)));
    cw_node_set(F.streaks, "direction", cw_val_vec3(d.x, d.y, d.z));
    cw_node_set(F.streaks, "speed_min", cw_val_float(p->speed * 0.5f));
    cw_node_set(F.streaks, "speed_max", cw_val_float(p->speed * 0.8f));
    float a = fminf(1, (p->speed - 14) / 12); /* fades in from 50 to 95 km/h */
    cw_node_set(F.streaks, "color", cw_val_color(1, 1, 1, a));
  }
  bool ground = racing && p->enabled && !p->airborne && p->speed > 5;
  float turn = p->turn_fact < 0 ? -p->turn_fact : p->turn_fact;
  bool base = ground && p->speed > 12;
  bool hard = ground && (turn > 0.3f || p->braking);
  t3_vec3 v = p->vel, back = { 0, 1, 0 };
  if (p->speed > 1e-3f) back = t3_vec3_normalize(t3_vec3_add(t3_vec3_scale(v, -0.6f / p->speed), (t3_vec3){ 0, 1, 0 }));
  t3_vec3 at = t3_vec3_add(p->pos, t3_vec3_scale(p->surf_nml, -TUX_RADIUS * 0.6f));
  if (p->speed > 1e-3f) at = t3_vec3_add(at, t3_vec3_scale(v, -0.7f / p->speed)); /* thrown up behind the belly */
  cw_node3d_set_position(F.spray, at);
  cw_node3d_set_position(F.turn_spray, at);
  cw_node3d_set_position(F.powder, at);
  cw_node_set(F.spray, "direction", cw_val_vec3(back.x, back.y, back.z));
  cw_node_set(F.powder, "direction", cw_val_vec3(back.x, back.y + 0.4f, back.z));
  cw_node_set(F.powder, "emitting", cw_val_bool(ground && p->speed > 9));
  t3_vec3 side = t3_vec3_cross((t3_vec3){ 0, 1, 0 }, v);
  if (t3_vec3_length_sq(side) > 1e-4f) side = t3_vec3_scale(t3_vec3_normalize(side), p->turn_fact < 0 ? 1 : -1);
  t3_vec3 td = t3_vec3_normalize(t3_vec3_add(t3_vec3_add(back, t3_vec3_scale(side, 0.8f * turn)), (t3_vec3){ 0, 0.4f, 0 }));
  cw_node_set(F.turn_spray, "direction", cw_val_vec3(td.x, td.y, td.z));
  cw_node_set(F.spray, "emitting", cw_val_bool(base));
  cw_node_set(F.turn_spray, "emitting", cw_val_bool(hard));
  if (F.snowing) cw_node3d_set_position(F.snow, (t3_vec3){ cam_pos.x, cam_pos.y + 10, cam_pos.z });
  cw_node3d_set_position(F.motes, t3_vec3_add(cam_pos, t3_vec3_scale(cam_dir, 8)));
  track_update(p, racing);
}

void fx_pickup(t3_vec3 at) {
  cw_node n = F.sparkle[F.sparkle_next];
  F.sparkle_next = (F.sparkle_next + 1) % 4;
  cw_node3d_set_position(n, at);
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "emitting", cw_val_bool(true)); /* a new emission window */
}
/* snow drops off the tree he hit: the box sits in its crown, 1.5-3.5 m up */
void fx_tree_hit(t3_vec3 tux_pos, t3_vec3 vel) {
  cw_node n = F.treesnow[F.treesnow_next];
  F.treesnow_next = (F.treesnow_next + 1) % 2;
  t3_vec3 d = t3_vec3_length_sq(vel) > 1e-4f ? t3_vec3_normalize(vel) : (t3_vec3){ 0, 0, -1 };
  cw_node3d_set_position(n, (t3_vec3){ tux_pos.x + d.x * 0.9f, tux_pos.y + 1.5f, tux_pos.z + d.z * 0.9f });
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "emitting", cw_val_bool(true));
}

void fx_set_motes(bool on) { cw_node_set(F.motes, "emitting", cw_val_bool(on)); }
