/* crawler effects: render only; they read the simulation's events and the shots in flight, and never feed it.
 * Built on the engine's effect kit: Explosion3D (fire and smoke flipbooks blended along baked motion vectors,
 * sparks, debris with smoke sub-emitters through PARENT emission, a ground Shockwave3D, a pooled flash light and a
 * scorch mark that cools), burst slots (one system fires many one-shots at their own places), DISTORT heat haze,
 * clustered omni lights.
 *
 *   the Mage's fireball   a white-hot core (view.c) with a trail of flame and embers from the fire flipbook, heat
 *                         haze behind it and a moving light that lights the walls as it flies; on impact a small
 *                         explosion (fire, sparks that leave smoke, a shockwave ring, debris, a cooling scorch)
 *   the Brute             each swing leaves a hot arc of streaks and sparks where it lands; the axe a pale trail
 *   the Warden            every hammer hit throws dust and a ground ring; heavy sparks
 *   the Ranger            every arrow a streak and a glint at its head; a pinned spark where it lands
 *   potions               a wave of light across the screen, a flash, every monster it takes goes up in a wisp
 *   spawners              falling: a blast in the monster's colour with debris; hit: sparks of that colour
 *   pickups               a rising spiral of the pickup's colour and a flash; treasure a shower of glints
 * No camera shake: the pads rumble instead. The effects preset thins particle counts and secondary lights; every
 * attack keeps its effect. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/explosions.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "game.h"

#define BIGS 6
#define SMALLB 12
#define WAVES 6
#define SHOT_LIGHTS 6
#define LIGHTS 6

static struct {
  bool ready;
  cw_node root;
  cw_node big[BIGS], small[SMALLB], wave[WAVES];
  int nb, ns, nw;
  /* burst pools */
  cw_node trail, trail_embers, trail_haze, streak, glint, axe_trail, hit, slash, dust, sparkle, wisp, spiral, coins, gore, frost, arc;
  cw_node shot_light[SHOT_LIGHTS], light[LIGHTS];
  cw_node motes; /* the air: dust, spores or embers drifting over the visible maze, in the tileset's colour */
  int motes_theme;
  float light_e[LIGHTS], light_e0[LIGHTS];
  int nl;
  /* rumble: per pad the strength running and when it ends; this frame's strongest request */
  float rum_k[MAXP];
  uint32_t rum_end[MAXP];
  float want_low[MAXP], want_high[MAXP];
  int want_ms[MAXP], rumbles;
  uint32_t tick;
  int frame_hits, frame_kills, frame_booms;
  int booms_total;
} FX;

int g_fx_level = 2;

static cw_node pnode(cw_node parent, const char *name, const char *const *kv, int seed) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_add_child(parent, n);
  for (int i = 0; kv[i]; i += 2) {
    cw_value v;
    const cw_prop *p = cw_node_find_prop(n, kv[i], NULL);
    if (p && p->type == CW_T_ENUM) {
      cw_node_set(n, kv[i], cw_val_enum(kv[i + 1]));
      continue;
    }
    if (!p || !cw_value_parse(kv[i + 1], p->type, &v)) {
      cw_warn("crawler: bad particle setting %s = %s", kv[i], kv[i + 1]);
      continue;
    }
    cw_node_set(n, kv[i], v);
  }
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "seed", cw_val_int(seed));
  cw_node3d_set_position(n, cw_node_is(parent, "Explosion3D") ? (t3_vec3){ 0, 0.5f, 0 } : (t3_vec3){ 0, -60, 0 });
  return n;
}
/* a pool of one-shots as one system of burst slots: each burst fires the settings at its own place */
static cw_node bnode(const char *name, const char *const *kv, int seed, int slots) {
  cw_node n = pnode(FX.root, name, kv, seed);
  cw_value v;
  int per = cw_node_get_prop(n, "amount", &v) ? (int)v.i : 1;
  cw_node_set(n, "bursts", cw_val_int(slots));
  cw_node_set(n, "amount", cw_val_int(per * slots));
  cw_node3d_set_position(n, (t3_vec3){ 0, 0, 0 });
  return n;
}
static void burst(cw_node n, float x, float y, float z, float dx, float dy, float dz) { cw_particles_burst(n, (t3_vec3){ x, y, z }, (t3_vec3){ dx, dy, dz }); }

/* ── the parts ──────────────────────────────────────────────────────── */

/* the fireball's trail: small flipbook flames shed behind it, glowing past white so bloom takes them */
static const char *const TRAIL[] = { "amount", "3", "lifetime", "0.42", "lifetime_random", "0.3", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.12", "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "0.2", "speed_max", "0.9",
  "gravity", "vec3(0, 1.8, 0)", "damping", "2.5", "size", "0.85", "size_random", "0.35", "size_curve", "\"0 1, 0.5 0.8, 1 0.2\"",
  "color_ramp", "\"0 1 0.95 0.8 1, 0.25 1 0.6 0.2 0.9, 1 0.5 0.1 0.04 0\"", "energy", "3.2", "blend", "ADD",
  "texture", "\"fx/fire.png\"", "motion_texture", "\"fx/fire_mv.png\"", "hframes", "8", "vframes", "8", "spin", "1.2", NULL };
static const char *const TRAIL_EMBERS[] = { "amount", "2", "lifetime", "0.9", "lifetime_random", "0.5", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.15", "spread", "180", "speed_min", "0.5", "speed_max", "2.2",
  "gravity", "vec3(0, -2.5, 0)", "damping", "1.2", "size", "0.05", "size_random", "0.5",
  "color_ramp", "\"0 1 0.85 0.5 1, 0.6 1 0.45 0.1 1, 1 0.6 0.1 0.02 0\"", "energy", "9", "blend", "ADD",
  "turbulence", "1.6", "turbulence_scale", "0.5", "draw_mode", "STRETCHED", "stretch", "0.03", NULL };
static const char *const TRAIL_HAZE[] = { "amount", "1", "lifetime", "0.5", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "30", "speed_min", "0.2", "speed_max", "0.6", "direction", "vec3(0, 1, 0)",
  "blend", "DISTORT", "distort_strength", "0.09", "size", "1.4", "size_curve", "\"0 0.5, 1 1.4\"",
  "color_ramp", "\"0 1 1 1 0, 0.2 1 1 1 1, 1 1 1 1 0\"", "texture", "\"fx/haze.png\"", NULL };
/* the Ranger's arrow: a thin streak shed along its flight, and a glint riding its head */
static const char *const STREAK[] = { "amount", "2", "lifetime", "0.16", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "2", "speed_min", "0.5", "speed_max", "1", "draw_mode", "STRETCHED", "stretch", "0.09", "size", "0.035",
  "color_ramp", "\"0 0.9 1 0.85 1, 1 0.4 0.8 0.5 0\"", "energy", "6", "blend", "ADD", NULL };
static const char *const GLINT[] = { "amount", "1", "lifetime", "0.05", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "0", "speed_min", "0", "speed_max", "0", "size", "0.42", "size_curve", "\"0 1, 1 0.6\"",
  "color_ramp", "\"0 1 1 0.9 1, 1 1 1 0.9 0\"", "energy", "10", "blend", "ADD", NULL };
/* the Brute's thrown axe: a pale motion trail */
static const char *const AXE_TRAIL[] = { "amount", "3", "lifetime", "0.18", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "spread", "180", "speed_min", "0", "speed_max", "0.3", "size", "0.16", "size_curve", "\"0 1, 1 0.2\"",
  "color_ramp", "\"0 0.9 0.92 1 0.5, 1 0.6 0.7 0.9 0\"", "energy", "2", "blend", "ADD", NULL };
static const char *const HIT[] = { "amount", "14", "lifetime", "0.35", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.05", "spread", "70", "speed_min", "2", "speed_max", "7",
  "gravity", "vec3(0, -8, 0)", "draw_mode", "STRETCHED", "stretch", "0.04", "size", "0.05",
  "color_ramp", "\"0 1 0.92 0.6 1, 1 1 0.4 0.1 0\"", "energy", "6", "blend", "ADD", NULL };
/* the Brute's swing: a hot arc of streaks raked across in front */
static const char *const SLASH[] = { "amount", "26", "lifetime", "0.22", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "70", "speed_min", "6", "speed_max", "12", "draw_mode", "STRETCHED", "stretch", "0.06",
  "size", "0.07", "color_ramp", "\"0 1 0.95 0.85 1, 0.5 1 0.55 0.15 1, 1 0.7 0.15 0.05 0\"", "energy", "7", "blend", "ADD", NULL };
/* the Warden's hammer: dust thrown up where it lands */
static const char *const DUST[] = { "amount", "8", "lifetime", "1.1", "lifetime_random", "0.3", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.4", "direction", "vec3(0, 1, 0)", "spread", "80", "speed_min", "1", "speed_max", "3.2",
  "gravity", "vec3(0, 0.4, 0)", "damping", "2.2", "blend", "ALPHA", "soft_distance", "0.4", "size", "1.4", "size_curve", "\"0 0.4, 0.4 1, 1 1.5\"",
  "color_ramp", "\"0 0.7 0.64 0.55 0, 0.12 0.55 0.5 0.44 0.6, 1 0.4 0.38 0.35 0\"",
  "texture", "\"fx/smoke.png\"", "motion_texture", "\"fx/smoke_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.3", NULL };
/* glitter: treasure, the magic's wisps, a pickup's spiral */
static const char *const SPARKLE[] = { "amount", "18", "lifetime", "0.9", "lifetime_random", "0.4", "explosiveness", "0.9", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.35", "direction", "vec3(0, 1, 0)", "spread", "50", "speed_min", "1.5", "speed_max", "4",
  "gravity", "vec3(0, -4, 0)", "damping", "1.2", "size", "0.09", "size_random", "0.6",
  "color_ramp", "\"0 1 0.95 0.6 1, 0.6 1 0.8 0.3 1, 1 1 0.6 0.2 0\"", "energy", "8", "blend", "ADD", NULL };
static const char *const WISP[] = { "amount", "6", "lifetime", "0.8", "lifetime_random", "0.3", "explosiveness", "0.8", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "direction", "vec3(0, 1, 0)", "spread", "25", "speed_min", "2", "speed_max", "4.5",
  "gravity", "vec3(0, 1.5, 0)", "damping", "1.5", "size", "0.5", "size_curve", "\"0 0.4, 0.3 1, 1 0\"",
  "color_ramp", "\"0 0.7 0.85 1 1, 0.5 0.45 0.6 1 0.8, 1 0.3 0.3 1 0\"", "energy", "5", "blend", "ADD",
  "turbulence", "1.2", "turbulence_scale", "0.6", NULL };
static const char *const SPIRAL[] = { "amount", "40", "lifetime", "0.9", "explosiveness", "0.6", "one_shot", "true",
  "emission_shape", "RING", "emission_radius", "0.8", "direction", "vec3(0, 1, 0)", "spread", "8", "speed_min", "2.5", "speed_max", "4.5",
  "emission_inner_radius", "0.6", "damping", "0.8", "trail_length", "8", "trail_width", "0.05", "size", "0.08", "size_random", "0.5",
  "color_ramp", "\"0 1 1 1 1, 0.5 1 1 1 1, 1 1 1 1 0\"", "energy", "9", "blend", "ADD", NULL };
static const char *const GORE[] = { "amount", "10", "lifetime", "0.6", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.2", "spread", "70", "direction", "vec3(0, 1, 0)", "speed_min", "1.5", "speed_max", "4",
  "gravity", "vec3(0, -12, 0)", "size", "0.4", "size_curve", "\"0 0.5, 0.4 1, 1 1.2\"",
  "color_ramp", "\"0 0.35 0.32 0.3 0.8, 1 0.22 0.2 0.2 0\"", "blend", "ALPHA", "soft_distance", "0.3",
  "texture", "\"fx/smoke.png\"", "hframes", "8", "vframes", "8", NULL };
static const char *const FROST[] = { "amount", "16", "lifetime", "0.5", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.15", "spread", "180", "speed_min", "2", "speed_max", "7",
  "gravity", "vec3(0, -9, 0)", "draw_mode", "STRETCHED", "stretch", "0.05", "size", "0.07",
  "color_ramp", "\"0 0.85 0.95 1 1, 1 0.4 0.7 1 0\"", "energy", "4", "blend", "ADD", NULL };
/* the air: motes drifting over the whole screen (the emitter follows the camera) */
static const char *const MOTES[] = { "amount", "220", "lifetime", "7", "lifetime_random", "0.4", "one_shot", "false",
  "emission_shape", "BOX", "emission_extents", "vec3(22, 0.6, 13)", "direction", "vec3(0, 1, 0)", "spread", "90", "speed_min", "0.1", "speed_max", "0.5",
  "gravity", "vec3(0.15, 0.05, 0.1)", "turbulence", "0.6", "turbulence_scale", "0.25", "size", "0.11", "size_random", "0.6",
  "color_ramp", "\"0 1 1 1 0, 0.2 1 1 1 1, 0.8 1 1 1 1, 1 1 1 1 0\"", "energy", "3", "blend", "ADD", NULL };
static const char *const ARC[] = { "amount", "12", "lifetime", "0.14", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "BOX", "emission_extents", "vec3(0.05, 0.05, 1)", "spread", "180", "speed_min", "0", "speed_max", "0.6",
  "draw_mode", "STRETCHED", "stretch", "0.08", "size", "0.06", "color_ramp", "\"0 0.8 0.9 1 1, 1 0.5 0.7 1 0\"", "energy", "12", "blend", "ADD", NULL };

/* the explosion kit: fire, flames, smoke, sparks that leave smoke (PARENT), debris that trails smoke, embers, haze */
static const char *const K_FIRE[] = { "amount", "10", "lifetime", "1.1", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.6", "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "0.4", "speed_max", "2",
  "gravity", "vec3(0, 1.6, 0)", "damping", "2.2", "size", "3.4", "size_random", "0.35", "size_curve", "\"0 0.5, 0.3 1, 1 1.2\"",
  "color_ramp", "\"0 1 1 1 0, 0.06 1 1 1 1, 0.75 1 0.85 0.8 0.85, 1 1 0.7 0.6 0\"", "energy", "1.8",
  "texture", "\"fx/fire.png\"", "motion_texture", "\"fx/fire_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.25", "soft_distance", "0.8", NULL };
static const char *const K_SMOKE[] = { "amount", "14", "lifetime", "3.0", "lifetime_random", "0.2", "explosiveness", "0.75", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.7", "direction", "vec3(0, 1, 0)", "spread", "120", "speed_min", "1", "speed_max", "3.2",
  "gravity", "vec3(0, 0.9, 0)", "damping", "1.1", "turbulence", "1.1", "turbulence_scale", "0.3", "blend", "ALPHA", "sort", "true",
  "soft_distance", "0.9", "size", "3.4", "size_random", "0.3", "size_curve", "\"0 0.45, 0.3 1, 1 1.55\"",
  "color_ramp", "\"0 1 0.9 0.8 0, 0.08 0.55 0.52 0.48 0.85, 0.45 0.28 0.27 0.26 0.6, 1 0.22 0.22 0.23 0\"",
  "texture", "\"fx/smoke.png\"", "motion_texture", "\"fx/smoke_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.18", "camera_fade", "18", NULL };
static const char *const K_SPARKS[] = { "amount", "120", "lifetime", "1.0", "lifetime_random", "0.4", "explosiveness", "0.95", "one_shot", "true",
  "emission_shape", "POINT", "spread", "180", "speed_min", "4", "speed_max", "13", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -12, 0)", "damping", "0.5", "draw_mode", "STRETCHED", "stretch", "0.045", "size", "0.05",
  "color_ramp", "\"0 1 0.95 0.7 1, 0.5 1 0.5 0.12 1, 1 0.6 0.1 0.05 0\"", "energy", "8", "bounce", "0.45",
  "collision_radius", "0.03", "collide_depth", "true", "blend", "ADD", NULL };
static const char *const K_SPARKSMOKE[] = { "amount", "120", "lifetime", "0.8", "emission_shape", "PARENT", "parent", "\"../Sparks\"",
  "one_shot", "true", "inherit_velocity", "0.02", "spread", "180", "speed_min", "0", "speed_max", "0.3", "gravity", "vec3(0, 0.5, 0)",
  "blend", "ALPHA", "soft_distance", "0.3", "size", "0.35", "size_curve", "\"0 0.3, 1 1.3\"",
  "color_ramp", "\"0 0.6 0.55 0.5 0, 0.2 0.4 0.38 0.36 0.35, 1 0.25 0.25 0.25 0\"",
  "texture", "\"fx/smoke.png\"", "hframes", "8", "vframes", "8", NULL };
static const char *const K_DEBRIS[] = { "amount", "14", "lifetime", "1.5", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "direction", "vec3(0, 1, 0)", "spread", "65", "speed_min", "4", "speed_max", "9",
  "gravity", "vec3(0, -12, 0)", "draw_mode", "MESH", "mesh_primitive", "BOX", "blend", "OPAQUE", "size", "0.16", "size_random", "0.6",
  "color", "color(0.16, 0.14, 0.13, 1)", "spin", "6", "bounce", "0.3", "collide_depth", "true", NULL };
static const char *const K_TRAIL[] = { "amount", "140", "lifetime", "1.0", "emission_shape", "PARENT", "parent", "\"../Debris\"",
  "one_shot", "true", "inherit_velocity", "0.05", "spread", "180", "speed_min", "0.1", "speed_max", "0.5", "gravity", "vec3(0, 0.6, 0)",
  "blend", "ALPHA", "soft_distance", "0.4", "size", "0.6", "size_curve", "\"0 0.4, 1 1.4\"",
  "color_ramp", "\"0 0.8 0.75 0.7 0, 0.15 0.5 0.47 0.45 0.5, 1 0.3 0.3 0.3 0\"",
  "texture", "\"fx/smoke.png\"", "hframes", "8", "vframes", "8", "spin", "0.4", NULL };
static const char *const K_EMBERS[] = { "amount", "50", "lifetime", "1.8", "lifetime_random", "0.5", "explosiveness", "0.8",
  "one_shot", "true", "emission_shape", "SPHERE", "emission_radius", "0.8", "spread", "180", "speed_min", "0.5", "speed_max", "3",
  "gravity", "vec3(0, 1.2, 0)", "damping", "1", "size", "0.06", "size_random", "0.5",
  "color_ramp", "\"0 1 0.7 0.3 1, 0.7 1 0.4 0.1 1, 1 0.6 0.1 0.02 0\"", "energy", "7", "blend", "ADD",
  "turbulence", "2", "turbulence_scale", "0.4", NULL };
static const char *const K_HAZE[] = { "amount", "18", "lifetime", "1.6", "explosiveness", "0.35", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.9", "direction", "vec3(0, 1, 0)", "spread", "50", "speed_min", "0.8", "speed_max", "2.0",
  "turbulence", "0.7", "blend", "DISTORT", "distort_strength", "0.12", "size", "3", "size_curve", "\"0 0.6, 1 1.6\"",
  "color_ramp", "\"0 1 1 1 0, 0.15 1 1 1 1, 1 1 1 1 0\"", NULL };
static const char *const S_FIRE[] = { "amount", "5", "lifetime", "0.6", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.25", "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "0.3", "speed_max", "1.6",
  "gravity", "vec3(0, 1.4, 0)", "damping", "2", "size", "1.6", "size_random", "0.3", "size_curve", "\"0 0.5, 0.3 1, 1 1.1\"",
  "color_ramp", "\"0 1 1 1 0, 0.08 1 1 1 1, 1 1 0.75 0.6 0\"", "energy", "2.4",
  "texture", "\"fx/fire.png\"", "motion_texture", "\"fx/fire_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.4", "soft_distance", "0.5", NULL };
static const char *const S_SPARKS[] = { "amount", "36", "lifetime", "0.7", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "180", "speed_min", "3", "speed_max", "8", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -12, 0)", "draw_mode", "STRETCHED", "stretch", "0.04", "size", "0.04", "bounce", "0.4", "collide_depth", "true",
  "color_ramp", "\"0 1 0.9 0.6 1, 1 1 0.4 0.1 0\"", "energy", "7", "blend", "ADD", NULL };
static const char *const S_SPARKSMOKE[] = { "amount", "36", "lifetime", "0.6", "emission_shape", "PARENT", "parent", "\"../Sparks\"",
  "one_shot", "true", "spread", "180", "speed_min", "0", "speed_max", "0.2", "gravity", "vec3(0, 0.6, 0)",
  "blend", "ALPHA", "soft_distance", "0.3", "size", "0.25", "size_curve", "\"0 0.3, 1 1.2\"",
  "color_ramp", "\"0 0.6 0.55 0.5 0, 0.2 0.4 0.38 0.36 0.3, 1 0.25 0.25 0.25 0\"", "texture", "\"fx/smoke.png\"", "hframes", "8", "vframes", "8", NULL };
static const char *const S_SMOKE[] = { "amount", "4", "lifetime", "1.6", "explosiveness", "0.9", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "direction", "vec3(0, 1, 0)", "spread", "90", "speed_min", "0.4", "speed_max", "1.4",
  "gravity", "vec3(0, 0.8, 0)", "damping", "1.2", "blend", "ALPHA", "soft_distance", "0.5", "size", "1.4", "size_curve", "\"0 0.4, 0.3 1, 1 1.4\"",
  "color_ramp", "\"0 0.8 0.7 0.6 0, 0.12 0.45 0.42 0.4 0.6, 1 0.25 0.25 0.26 0\"",
  "texture", "\"fx/smoke.png\"", "motion_texture", "\"fx/smoke_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.2", NULL };
static const char *const S_DEBRIS[] = { "amount", "6", "lifetime", "1.0", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.2", "direction", "vec3(0, 1, 0)", "spread", "60", "speed_min", "3", "speed_max", "6",
  "gravity", "vec3(0, -12, 0)", "draw_mode", "MESH", "mesh_primitive", "BOX", "blend", "OPAQUE", "size", "0.1", "size_random", "0.5",
  "color", "color(0.14, 0.12, 0.11, 1)", "spin", "6", "bounce", "0.3", "collide_depth", "true", NULL };

static cw_node boom_node(const char *name, bool big, int seed) {
  cw_node e = cw_node_new("Explosion3D", name);
  cw_node_add_child(FX.root, e);
  cw_node_set(e, "autostart", cw_val_bool(false));
  cw_node_set(e, "duration", cw_val_float(big ? 3.4f : 2.0f));
  cw_node_set(e, "flash_energy", cw_val_float(big ? 150.0f : 55.0f));
  cw_node_set(e, "flash_range", cw_val_float(big ? 13.0f : 7.0f));
  cw_node_set(e, "flash_duration", cw_val_float(big ? 0.45f : 0.28f));
  cw_node_set(e, "scorch_texture", cw_val_string("fx/scorch.png"));
  cw_node_set(e, "scorch_size", cw_val_float(big ? 4.5f : 1.8f));
  cw_node_set(e, "scorch_time", cw_val_float(big ? 6.0f : 2.5f));
  cw_node_set(e, "scorch_life", cw_val_float(big ? 30.0f : 14.0f));
  cw_node_set(e, "scorch_glow", cw_val_float(big ? 3.0f : 2.0f));
  cw_node_set(e, "shake_amount", cw_val_float(0)); /* never: the pads rumble instead */
  cw_node3d_set_position(e, (t3_vec3){ 0, -60, 0 });
  if (big) {
    pnode(e, "Fire", K_FIRE, seed), pnode(e, "Smoke", K_SMOKE, seed + 2);
    pnode(e, "Sparks", K_SPARKS, seed + 3), pnode(e, "SparkSmoke", K_SPARKSMOKE, seed + 4);
    pnode(e, "Debris", K_DEBRIS, seed + 5), pnode(e, "DebrisTrail", K_TRAIL, seed + 6);
    pnode(e, "Embers", K_EMBERS, seed + 7), pnode(e, "Haze", K_HAZE, seed + 8);
  } else {
    pnode(e, "Fire", S_FIRE, seed), pnode(e, "Sparks", S_SPARKS, seed + 1), pnode(e, "SparkSmoke", S_SPARKSMOKE, seed + 2);
    pnode(e, "Smoke", S_SMOKE, seed + 3), pnode(e, "Debris", S_DEBRIS, seed + 4);
  }
  cw_node w = cw_node_new("Shockwave3D", "Wave");
  cw_node_add_child(e, w);
  cw_node_set(w, "ground", cw_val_bool(true));
  cw_node_set(w, "autostart", cw_val_bool(false));
  cw_node_set(w, "max_radius", cw_val_float(big ? 7.0f : 2.6f));
  cw_node_set(w, "duration", cw_val_float(big ? 0.7f : 0.4f));
  cw_node_set(w, "thickness", cw_val_float(big ? 1.2f : 0.6f));
  cw_node_set(w, "strength", cw_val_float(big ? 0.6f : 0.35f));
  cw_node3d_set_position(w, (t3_vec3){ 0, 0.3f, 0 });
  return e;
}
static cw_node wave_node(void) {
  cw_node w = cw_node_new("Shockwave3D", "Ring");
  cw_node_add_child(FX.root, w);
  cw_node_set(w, "ground", cw_val_bool(true));
  cw_node_set(w, "autostart", cw_val_bool(false));
  cw_node_set(w, "max_radius", cw_val_float(3.0f));
  cw_node_set(w, "duration", cw_val_float(0.45f));
  cw_node_set(w, "thickness", cw_val_float(0.7f));
  cw_node_set(w, "strength", cw_val_float(0.22f));
  cw_node3d_set_position(w, (t3_vec3){ 0, -60, 0 });
  return w;
}
static cw_node omni(const char *name, float range) {
  cw_node l = cw_node_new("OmniLight3D", name);
  cw_node_add_child(FX.root, l);
  cw_node3d_set_position(l, (t3_vec3){ 0, -60, 0 });
  cw_node_set(l, "energy", cw_val_float(0));
  cw_node_set(l, "range", cw_val_float(range));
  cw_node_set(l, "decay", cw_val_float(2));
  cw_node_set(l, "fog_energy", cw_val_float(0));
  return l;
}

void fx_free(void) { memset(&FX, 0, sizeof FX); }

void fx_init(cw_node root) {
  fx_free();
  FX.root = cw_node_new("Node3D", "FX");
  cw_node_add_child(root, FX.root);
  cw_node ls = cw_node_new("LightingSettings", "Lighting");
  cw_node_add_child(FX.root, ls);
  cw_node_set(ls, "mode", cw_val_enum("CLUSTERED"));
  cw_node_set(ls, "cluster_far", cw_val_float(60));
  for (int i = 0; i < BIGS; i++) FX.big[i] = boom_node("BigBlast", true, 100 + i * 10);
  for (int i = 0; i < SMALLB; i++) FX.small[i] = boom_node("SmallBlast", false, 200 + i * 10);
  for (int i = 0; i < WAVES; i++) FX.wave[i] = wave_node();
  FX.trail = bnode("FireTrail", TRAIL, 300, 96);
  FX.trail_embers = bnode("FireTrailEmbers", TRAIL_EMBERS, 310, 96);
  FX.trail_haze = bnode("FireTrailHaze", TRAIL_HAZE, 320, 32);
  FX.streak = bnode("ArrowStreak", STREAK, 330, 96);
  FX.glint = bnode("ArrowGlint", GLINT, 340, 48);
  FX.axe_trail = bnode("AxeTrail", AXE_TRAIL, 350, 48);
  FX.hit = bnode("Hit", HIT, 360, 32);
  FX.slash = bnode("Slash", SLASH, 370, 16);
  FX.dust = bnode("Dust", DUST, 380, 16);
  FX.sparkle = bnode("Sparkle", SPARKLE, 390, 24);
  FX.wisp = bnode("Wisp", WISP, 400, 64);
  FX.spiral = bnode("Spiral", SPIRAL, 410, 8);
  FX.coins = bnode("Coins", SPARKLE, 420, 12);
  FX.gore = bnode("Gore", GORE, 430, 32);
  FX.frost = bnode("Frost", FROST, 440, 16);
  FX.arc = bnode("Arc", ARC, 450, 24);
  FX.motes = pnode(FX.root, "Motes", MOTES, 470);
  cw_node_set(FX.motes, "emitting", cw_val_bool(true));
  FX.motes_theme = -1;
  for (int i = 0; i < SHOT_LIGHTS; i++) FX.shot_light[i] = omni("ShotLight", 7.5f);
  for (int i = 0; i < LIGHTS; i++) FX.light[i] = omni("Flash", 10);
  FX.ready = true;
}

/* ── rumble ─────────────────────────────────────────────────────────── */

static void rumble(int p, float low, float high, int ms) {
  if (p < 0 || p >= MAXP || low + high <= FX.want_low[p] + FX.want_high[p]) return;
  FX.want_low[p] = low, FX.want_high[p] = high, FX.want_ms[p] = ms;
}
static void rumble_near(const sim *s, float x, float y, float reach, float k0, int ms) {
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (!p->active) continue;
    float d = sqrtf((p->x - x) * (p->x - x) + (p->y - y) * (p->y - y)), k = 1.0f - d / reach;
    if (k <= 0.05f) continue;
    rumble(i, k0 * k * k, 0.3f * k0 * k * k, ms);
  }
}
static void rumble_send(void) {
  for (int i = 0; i < MAXP; i++) {
    float k = FX.want_low[i] + FX.want_high[i];
    if (k > 0 && (FX.tick >= FX.rum_end[i] || k >= FX.rum_k[i])) {
      cw_input_rumble(i, FX.want_low[i], FX.want_high[i], FX.want_ms[i]);
      FX.rum_k[i] = k, FX.rum_end[i] = FX.tick + (uint32_t)(FX.want_ms[i] * 60 / 1000), FX.rumbles++;
    }
    FX.want_low[i] = FX.want_high[i] = 0, FX.want_ms[i] = 0;
  }
}

fx_stats fx_get_stats(void) {
  fx_stats s = { 0 };
  s.booms = FX.booms_total;
  s.rumbles = FX.rumbles;
  for (int i = 0; i < LIGHTS; i++) s.lights_live += FX.light_e[i] > 0.01f;
  return s;
}

static void flash(float x, float y, float z, float e, float r, float g, float b) {
  if (g_fx_level <= 0 && e < 40) return; /* the Low preset keeps the big flashes only */
  int i = FX.nl++ % LIGHTS;
  cw_node3d_set_position(FX.light[i], (t3_vec3){ x, y, z });
  cw_node_set(FX.light[i], "color", cw_val_color(r, g, b, 1));
  FX.light_e[i] = FX.light_e0[i] = e;
}
static void blast(float x, float z, float r, bool big) {
  FX.booms_total++;
  if (FX.frame_booms++ > (big ? 3 : 6)) return; /* a chain of blasts in one frame: the first few carry it */
  cw_node e = big ? FX.big[FX.nb++ % BIGS] : FX.small[FX.ns++ % SMALLB];
  cw_node3d_set_position(e, (t3_vec3){ x, 0, z });
  if (big) cw_node_set(cw_node_get(e, "Wave"), "max_radius", cw_val_float(CW_MAX(r * 1.6f, 4.0f)));
  cw_node3d_global_transform(e);
  cw_explosion_fire(e);
}
static void ring(float x, float z, float r) {
  cw_node w = FX.wave[FX.nw++ % WAVES];
  cw_node3d_set_position(w, (t3_vec3){ x, 0.25f, z });
  cw_node_set(w, "max_radius", cw_val_float(r));
  cw_node3d_global_transform(w);
  cw_shockwave_fire(w);
}

static const float KCOL[EK_KINDS][3] = { { 0.6f, 0.8f, 1.0f }, { 0.9f, 0.85f, 0.6f }, { 1.0f, 0.45f, 0.15f }, { 1.0f, 0.55f, 0.2f }, { 0.75f, 0.35f, 1.0f }, { 0.5f, 0.7f, 1.0f },
                                         { 0.9f, 0.8f, 0.3f }, { 0.9f, 0.3f, 0.2f }, { 1.0f, 0.35f, 0.1f }, { 0.5f, 1.0f, 0.3f }, { 0.4f, 1.0f, 0.8f }, { 0.3f, 1.0f, 0.6f } };

void fx_events(sim *s) {
  if (!FX.ready) { s->ev_n = 0; return; }
  FX.frame_hits = FX.frame_kills = FX.frame_booms = 0;
  int lvl = g_fx_level;
  for (int i = 0; i < s->ev_n; i++) {
    const event *e = &s->ev[i];
    float x = e->x, z = e->y;
    switch (e->kind) {
    case EV_SHOT:
      if (e->b == SK_FIRE) flash(x + e->z * 0.6f, 1.4f, z + e->w * 0.6f, 18, 1, 0.55f, 0.2f), burst(FX.trail, x + e->z * 0.6f, 1.1f, z + e->w * 0.6f, e->z, 0, e->w);
      if (e->a < MAXP) rumble(e->a, 0.0f, e->b == SK_AXE || e->b == SK_HAMMER ? 0.25f : 0.12f, 50);
      break;
    case EV_HIT: {
      if (FX.frame_hits++ > (lvl >= 2 ? 16 : lvl >= 1 ? 10 : 6)) break;
      float l = sqrtf(e->z * e->z + e->w * e->w) + 1e-4f, dx = -e->z / l, dz = -e->w / l;
      if (e->b == SK_HAMMER) { /* the Warden's hammer: dust and a ring */
        burst(FX.dust, x, 0.3f, z, 0, 1, 0), burst(FX.hit, x, 0.9f, z, dx, 0.4f, dz);
        if (lvl >= 1) ring(x, z, 2.2f);
      } else if (e->b == SK_ARROW) burst(FX.hit, x, 1.0f, z, dx, 0.2f, dz), burst(FX.glint, x, 1.0f, z, 0, 0, 0);
      else if (e->b == SK_AXE) burst(FX.hit, x, 1.0f, z, dx, 0.3f, dz), burst(FX.slash, x, 1.0f, z, dx, 0, dz);
      else if (e->b == SK_FIRE) burst(FX.trail, x, 1.0f, z, 0, 1, 0);
      else burst(FX.hit, x, 1.0f, z, dx, 0.3f, dz);
      break;
    }
    case EV_MELEE: {
      if (FX.frame_hits++ > 16) break;
      if (e->b == CL_BRUTE) burst(FX.slash, x, 1.1f, z, e->z, 0.05f, e->w), burst(FX.hit, x, 1.0f, z, e->z, 0.3f, e->w), flash(x, 1.4f, z, 10, 1, 0.6f, 0.3f);
      else if (e->b == CL_WARDEN) burst(FX.dust, x, 0.3f, z, 0, 1, 0), burst(FX.hit, x, 0.9f, z, e->z, 0.4f, e->w), ring(x, z, 1.8f);
      else burst(FX.hit, x, 1.0f, z, e->z, 0.3f, e->w);
      if (e->a < MAXP) rumble(e->a, 0.35f, 0.25f, 70);
      break;
    }
    case EV_KILL:
      if (FX.frame_kills++ > (lvl >= 2 ? 24 : lvl >= 1 ? 14 : 8)) break;
      if (e->a == EK_SHADE || e->a == EK_DREAD) burst(FX.wisp, x, 0.8f, z, 0, 1, 0);
      else burst(FX.gore, x, 0.5f, z, 0, 1, 0);
      if (e->a == EK_OGRE && e->b < MAXP) rumble(e->b, 0.5f, 0.2f, 90);
      break;
    case EV_BOOM:
      blast(x, z, e->z, e->a);
      rumble_near(s, x, z, e->a ? 24.0f : 10.0f, e->a ? 1.0f : 0.4f, e->a ? 260 : 120);
      break;
    case EV_HURT:
      if (e->a < MAXP) rumble(e->a, 0.35f, 0.85f, 90);
      burst(FX.hit, x, 1.1f, z, 0, 1, 0);
      break;
    case EV_DOWN: blast(x, z, 1.5f, false); if (e->a < MAXP) rumble(e->a, 1.0f, 0.6f, 400); break;
    case EV_REJOIN: burst(FX.spiral, x, 0.1f, z, 0, 1, 0), flash(x, 2, z, 40, 1, 1, 1); break;
    case EV_MAGIC: { /* a wave of light across the screen from the caster's middle */
      float k = e->b ? 0.4f : 1.0f;
      ring(x, z, e->b ? 5.0f : 20.0f);
      flash(x, 4, z, 260 * k, 0.7f, 0.85f, 1.0f);
      for (int j = 0; j < (e->b ? 3 : 10); j++) burst(FX.wisp, x + cw_cos((float)j * 0.628f) * 4 * k, 1.0f, z + cw_sin((float)j * 0.628f) * 3 * k, 0, 1, 0);
      if (e->a < MAXP) rumble(e->a, 0.9f * k, 0.5f * k, 300);
      break;
    }
    case EV_SPAWN: if (lvl >= 1) burst(FX.wisp, x, 0.6f, z, 0, 1, 0); break;
    case EV_SPAWNER_HIT: burst(FX.hit, x, 1.0f, z, 0, 1, 0); break;
    case EV_SPAWNER_DIE:
      blast(x, z, 3.0f, true);
      for (int j = 0; j < 2; j++) burst(FX.sparkle, x, 1.0f, z, 0, 1, 0);
      flash(x, 2, z, 120, KCOL[e->a][0], KCOL[e->a][1], KCOL[e->a][2]);
      if (e->b < MAXP) rumble(e->b, 0.7f, 0.4f, 200);
      break;
    case EV_PICK:
      if (e->b == IT_KEY || e->b == IT_POTION) burst(FX.spiral, x, 0.1f, z, 0, 1, 0), flash(x, 1.5f, z, 30, e->b == IT_KEY ? 1 : 0.4f, e->b == IT_KEY ? 0.85f : 0.6f, e->b == IT_KEY ? 0.3f : 1);
      else if (e->b >= IT_ROAST && e->b <= IT_PLATE) burst(FX.sparkle, x, 0.6f, z, 0, 1, 0);
      break;
    case EV_TREASURE: case EV_CHEST: burst(FX.coins, x, 0.6f, z, 0, 1, 0), flash(x, 1.5f, z, 25, 1, 0.85f, 0.4f); break;
    case EV_CHARM: case EV_ELIXIR:
      burst(FX.spiral, x, 0.1f, z, 0, 1, 0), burst(FX.sparkle, x, 1.0f, z, 0, 1, 0), flash(x, 1.8f, z, 70, 0.8f, 0.9f, 1);
      if (e->a < MAXP) rumble(e->a, 0.2f, 0.5f, 120);
      break;
    case EV_DOOR: burst(FX.dust, x, 0.4f, z, 0, 1, 0), burst(FX.sparkle, x, 1.2f, z, 0, 1, 0), flash(x, 2, z, 50, 1, 0.8f, 0.4f); break;
    case EV_WALL_BREAK: blast(x, z, 1.6f, false), burst(FX.dust, x, 0.5f, z, 0, 1, 0); break;
    case EV_TRAP: ring(x, z, 6.0f), flash(x, 2, z, 50, 1, 0.5f, 0.3f); break;
    case EV_AMBUSH: ring(x, z, 9.0f), flash(x, 2.5f, z, 120, 1, 0.3f, 0.2f), rumble_near(s, x, z, 12, 0.6f, 250); break;
    case EV_TELEPORT: burst(FX.spiral, x, 0.1f, z, 0, 1, 0), burst(FX.spiral, e->z, 0.1f, e->w, 0, 1, 0), flash(e->z, 2, e->w, 50, 0.4f, 0.8f, 1); break;
    case EV_EXIT: burst(FX.spiral, x, 0.1f, z, 0, 1, 0), flash(x, 2, z, 90, 1, 0.85f, 0.5f); break;
    case EV_FOOD_BREAK: burst(FX.gore, x, 0.4f, z, 0, 1, 0); break;
    case EV_POTION_BREAK: ring(x, z, 5), burst(FX.wisp, x, 0.6f, z, 0, 1, 0), flash(x, 1.5f, z, 60, 0.4f, 0.6f, 1); break;
    case EV_LOB_LAND: blast(x, z, 1.2f, false); break;
    case EV_CLOUD: burst(FX.gore, x, 0.5f, z, 0, 1, 0); break;
    case EV_DRAIN: burst(FX.wisp, e->z, 1.0f, e->w, x - e->z, 0.5f, z - e->w); if (e->a < MAXP) rumble(e->a, 0.3f, 0.3f, 250); break;
    case EV_SHATTER: burst(FX.frost, x, 0.9f, z, 0, 1, 0); break;
    case EV_ARC: burst(FX.arc, (x + e->z) * 0.5f, 1.0f, (z + e->w) * 0.5f, e->z - x, 0, e->w - z); break;
    case EV_BOUNCE: burst(FX.hit, x, 1.0f, z, 0, 1, 0); break;
    case EV_STUN: burst(FX.sparkle, x, 1.6f, z, 0, 1, 0); if (e->a < MAXP) rumble(e->a, 0.2f, 0.4f, 120); break;
    case EV_SWIPE: if (e->b && lvl >= 1) burst(FX.slash, x + e->z * 0.6f, 0.9f, z + e->w * 0.6f, e->z, -0.1f, e->w); break;
    case EV_ESHOT: if (e->a == EK_IMP) flash(x, 1.2f, z, 12, 1, 0.45f, 0.15f); break;
    case EV_FIREBOMB: break;
    default: break;
    }
  }
  s->ev_n = 0;
}

/* every frame: the shots' trails and their moving lights, the flashes dying away, the pads */
void fx_tick(const sim *s, float time) {
  if (!FX.ready) return;
  FX.tick++;
  (void)time;
  int nl = 0, lvl = g_fx_level;
  { /* the air over the screen, in the tileset's colour: crypt dust, sewer spores, foundry embers, grotto glow-spores,
     * cathedral motes in the light, abyss ash */
    static const float MC[MZ_THEMES][3] = { { 1.0f, 0.95f, 0.8f }, { 0.6f, 1.0f, 0.8f }, { 1.0f, 0.5f, 0.15f }, { 0.4f, 1.0f, 0.9f }, { 1.0f, 0.85f, 0.55f }, { 0.85f, 0.4f, 1.0f } };
    int th = CW_CLAMP(s->m.theme, 0, MZ_THEMES - 1);
    if (th != FX.motes_theme) FX.motes_theme = th, cw_node_set(FX.motes, "color", cw_val_color(MC[th][0], MC[th][1], MC[th][2], 1));
    cw_node3d_set_position(FX.motes, (t3_vec3){ s->cam_x, 1.4f, s->cam_y });
    static int last_amount = -1;
    int amount = lvl >= 2 ? 220 : lvl >= 1 ? 140 : 80;
    if (amount != last_amount) last_amount = amount, cw_node_set(FX.motes, "amount", cw_val_int(amount));
  }
  for (int j = 0; j < MAXB; j++) {
    const bullet *b = &s->b[j];
    if (!b->alive) continue;
    float vl = sqrtf(b->vx * b->vx + b->vy * b->vy) + 1e-4f, dx = b->vx / vl, dz = b->vy / vl;
    switch (b->kind) {
    case SK_FIRE: /* the showpiece: flames, embers, haze and a light that rides it */
      burst(FX.trail, b->x - dx * 0.2f, 1.0f, b->y - dz * 0.2f, -dx * 0.6f, 0.2f, -dz * 0.6f);
      if (lvl >= 1 || (FX.tick & 1)) burst(FX.trail_embers, b->x, 1.0f, b->y, -dx, 0.3f, -dz);
      if (lvl >= 1 && (FX.tick % 3) == 0) burst(FX.trail_haze, b->x - dx * 0.5f, 1.0f, b->y - dz * 0.5f, 0, 1, 0);
      if (nl < SHOT_LIGHTS) {
        cw_node3d_set_position(FX.shot_light[nl], (t3_vec3){ b->x, 1.2f, b->y });
        cw_node_set(FX.shot_light[nl], "color", cw_val_color(1, 0.55f, 0.22f, 1));
        cw_node_set(FX.shot_light[nl], "energy", cw_val_float(42.0f + 6.0f * cw_sin((float)FX.tick * 0.9f + (float)j)));
        nl++;
      }
      break;
    case SK_IMPFIRE:
      if (FX.tick & 1) burst(FX.trail, b->x, 1.0f, b->y, -dx * 0.4f, 0.2f, -dz * 0.4f);
      if (nl < SHOT_LIGHTS && lvl >= 1) {
        cw_node3d_set_position(FX.shot_light[nl], (t3_vec3){ b->x, 1.2f, b->y });
        cw_node_set(FX.shot_light[nl], "color", cw_val_color(1, 0.4f, 0.1f, 1));
        cw_node_set(FX.shot_light[nl], "energy", cw_val_float(20.0f));
        nl++;
      }
      break;
    case SK_HEX:
      if (FX.tick & 1) burst(FX.arc, b->x, 1.0f, b->y, dx, 0, dz);
      break;
    case SK_ARROW: /* a streak along the flight, a glint on the head */
      burst(FX.streak, b->x - dx * 0.4f, 1.0f, b->y - dz * 0.4f, -dx, 0, -dz);
      burst(FX.glint, b->x + dx * 0.45f, 1.0f, b->y + dz * 0.45f, 0, 0, 0);
      break;
    case SK_AXE: case SK_HAMMER:
      if ((FX.tick & 1) || lvl >= 2) burst(FX.axe_trail, b->x, 1.0f, b->y, 0, 0, 0);
      break;
    case SK_FIREBOMB:
      burst(FX.trail, b->x, 1.6f, b->y, 0, 0.3f, 0);
      break;
    default: break;
    }
  }
  for (int i = nl; i < SHOT_LIGHTS; i++) cw_node_set(FX.shot_light[i], "energy", cw_val_float(0));
  for (int i = 0; i < LIGHTS; i++) {
    if (FX.light_e[i] <= 0.01f) continue;
    FX.light_e[i] *= 0.82f;
    cw_node_set(FX.light[i], "energy", cw_val_float(FX.light_e[i] > 0.5f ? FX.light_e[i] : 0));
  }
  rumble_send();
}
