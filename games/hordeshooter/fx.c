/* hordeshooter effects: explosions, fire, smoke, gibs, decals, lights.
 * Render only; they read the simulation's events and never feed it.
 *
 *   big blast   a pooled Explosion3D (the engine's explosion kit): fire and
 *               smoke flipbooks blended along baked motion vectors, sparks
 *               that bounce off the scene, debris chunks trailing smoke (a
 *               sub-emitter), embers, heat haze, a ground shockwave; the node
 *               borrows a pooled flash light, stamps a scorch mark that cools
 *               (no camera shake: the players' pads rumble instead)
 *   small blast a smaller Explosion3D: a fire puff, sparks, smoke, a ring
 *   kills       a spray of gibs (instanced chunks, CPU physics, bounce on
 *               the floor), a stain decal under the body
 *   hits        a puff of sparks, rate limited per frame
 *   fire        barrels leave a burning patch (a long lived emitter and a
 *               light) for some seconds
 *   lights      muzzle flashes: eight pooled omni lights; blast flashes come
 *               from the kit's pool. Clustered lighting: idle lights cost nothing
 * The graphics setting "effects detail" thins every one of these. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/decals.h"
#include "cartwheel/explosions.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "cartwheel/quality.h"
#include "game.h"

#define BIGS 6
#define SMALLB 10
#define LEVELS 8
#define HITS 20
#define MUZZLES 12
#define SPLATS 20
#define SLASHES 16
#define BOLTS 48 /* lightning jumps drawn at once */
#define FIRES 6
#define STAIN 140
#define LIGHTS 8
#define GIBS 1600
typedef struct gib { t3_vec3 p, v, rot, spin; float size; int life; t3_color col; } gib;

static struct {
  bool ready;
  cw_node root;
  cw_node big[BIGS], small[SMALLB], level[LEVELS], hit[HITS], muzzle[MUZZLES], splat[SPLATS], slash, frost, fire[FIRES], fire_light[FIRES];
  int fire_age[FIRES];
  int nb, ns, nlv, nh, nsp, nf, nst, nl, nm;
  cw_node stain[STAIN], light[LIGHTS], fire_haze[FIRES];
  float light_e0[LIGHTS];
  t3_vec3 fire_pos[FIRES];
  float light_e[LIGHTS], light_c[LIGHTS][3];
  float stain_a[STAIN];
  t3_instanced_mesh *gibs;
  t3_instanced_mesh *bolts;
  struct { float x0, y0, x1, y1; int life; } bolt[BOLTS];
  int nbolt;
  gib g[GIBS];
  int next_g, live_g;
  /* each player's pad: the strength of the rumble running and the tick it ends (a weaker request never cuts a
   * stronger one short), and this frame's strongest request */
  float rum_k[MAXP];
  uint32_t rum_end[MAXP];
  float want_low[MAXP], want_high[MAXP];
  int want_ms[MAXP];
  bool shake_control; /* HORDE_SHAKE_CONTROL: the old explosion-kit camera shake, the camera gate's control only */
  t3_vec3 focus;
  uint32_t tick;
  int frame_hits, frame_blood, frame_gibs, frame_booms;
  uint32_t rng;
  int booms_total;
} FX;

int g_fx_level = 2;
static const t3_mat4 ZERO;

static uint32_t rnd32(void) {
  FX.rng = FX.rng * 1664525u + 1013904223u;
  return FX.rng >> 8;
}
static float rnd(void) { return (float)(rnd32() & 0xffff) / 65536.0f; }

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
      cw_warn("hordeshooter: bad particle setting %s = %s", kv[i], kv[i + 1]);
      continue;
    }
    cw_node_set(n, kv[i], v);
  }
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "seed", cw_val_int(seed));
  cw_node3d_set_position(n, cw_node_is(parent, "Explosion3D") ? (t3_vec3){ 0, 0.5f, 0 } : (t3_vec3){ 0, -60, 0 });
  return n;
}
/* a pool of one-shot effects as one system of burst slots (cartwheel/particles.h bursts): each slot fires a
 * one-shot of the settings at its own place, one simulation and one draw for the whole pool */
static cw_node bnode(cw_node parent, const char *name, const char *const *kv, int seed, int slots) {
  cw_node n = pnode(parent, name, kv, seed);
  cw_value v;
  int per = cw_node_get_prop(n, "amount", &v) ? (int)v.i : 1;
  cw_node_set(n, "bursts", cw_val_int(slots));
  cw_node_set(n, "amount", cw_val_int(per * slots));
  cw_node3d_set_position(n, (t3_vec3){ 0, 0, 0 });
  return n;
}
static void burst_at(cw_node n, t3_vec3 at) {
  cw_node3d_set_position(n, at);
  cw_node3d_global_transform(n);
  cw_particles_restart(n);
}

static const char *const FIREHAZE[] = { "amount", "26", "lifetime", "1.8", "lifetime_random", "0.3", "emitting", "false", "one_shot", "false",
  "emission_shape", "SPHERE", "emission_radius", "0.8", "direction", "vec3(0, 1, 0)", "spread", "15", "speed_min", "0.8", "speed_max", "1.8",
  "gravity", "vec3(0, 2.5, 0)", "damping", "0.6", "size", "1.6", "size_random", "0.4", "size_curve", "\"0 0.5, 0.4 1, 1 1.3\"",
  "color_ramp", "\"0 1 1 1 0, 0.25 1 1 1 0.7, 1 1 1 1 0\"", "blend", "DISTORT", "distort_strength", "0.16",
  "texture", "\"fx/haze.png\"", "turbulence", "1.0", "turbulence_scale", "0.5", NULL };
/* the explosion kit's parts (children of an Explosion3D): fire and smoke flipbooks (tools/fx/bake-flipbooks.mjs
 * in the engine bakes them with their motion-vector sheets), sparks, debris with a smoke sub-emitter, embers,
 * heat haze */
static const char *const K_FIRE[] = { "amount", "10", "lifetime", "1.3", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.8", "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "0.4", "speed_max", "2",
  "gravity", "vec3(0, 1.6, 0)", "damping", "2.2", "size", "4.6", "size_random", "0.35", "size_curve", "\"0 0.5, 0.3 1, 1 1.2\"",
  "color_ramp", "\"0 1 1 1 0, 0.06 1 1 1 1, 0.75 1 0.85 0.8 0.85, 1 1 0.7 0.6 0\"", "energy", "1.6",
  "texture", "\"fx/fire.png\"", "motion_texture", "\"fx/fire_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.25", "soft_distance", "0.8", NULL };
static const char *const K_FLAMES[] = { "amount", "14", "lifetime", "1.0", "explosiveness", "0.8", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.5", "direction", "vec3(0, 1, 0)", "spread", "90", "speed_min", "2.5", "speed_max", "6",
  "gravity", "vec3(0, 2, 0)", "damping", "1.6", "size", "2.2", "size_random", "0.5", "size_curve", "\"0 0.5, 0.35 1, 1 0.8\"",
  "color_ramp", "\"0 1 1 1 0, 0.08 1 1 1 1, 1 1 0.8 0.7 0\"", "energy", "1.4",
  "texture", "\"fx/fire.png\"", "motion_texture", "\"fx/fire_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.6", "soft_distance", "0.5", NULL };
static const char *const K_SMOKE[] = { "amount", "18", "lifetime", "3.8", "lifetime_random", "0.2", "explosiveness", "0.75", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.9", "direction", "vec3(0, 1, 0)", "spread", "120", "speed_min", "1", "speed_max", "3.8",
  "gravity", "vec3(0, 0.9, 0)", "damping", "1.1", "turbulence", "1.1", "turbulence_scale", "0.3", "blend", "ALPHA", "sort", "true",
  "soft_distance", "0.9", "size", "4.6", "size_random", "0.3", "size_curve", "\"0 0.45, 0.3 1, 1 1.55\"",
  "color_ramp", "\"0 1 0.9 0.8 0, 0.08 0.6 0.56 0.52 0.9, 0.45 0.3 0.29 0.28 0.7, 1 0.24 0.24 0.25 0\"",
  "texture", "\"fx/smoke.png\"", "motion_texture", "\"fx/smoke_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.18", NULL };
static const char *const K_SPARKS[] = { "amount", "220", "lifetime", "1.4", "lifetime_random", "0.4", "explosiveness", "0.95", "one_shot", "true",
  "emission_shape", "POINT", "spread", "180", "speed_min", "5", "speed_max", "16", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -12, 0)", "damping", "0.5", "draw_mode", "STRETCHED", "stretch", "0.045", "size", "0.05",
  "color_ramp", "\"0 1 0.95 0.7 1, 0.5 1 0.5 0.12 1, 1 0.6 0.1 0.05 0\"", "energy", "7", "bounce", "0.45",
  "collision_radius", "0.03", "collide_depth", "true", "blend", "ADD", NULL };
static const char *const K_DEBRIS[] = { "amount", "22", "lifetime", "1.8", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "direction", "vec3(0, 1, 0)", "spread", "65", "speed_min", "6", "speed_max", "12",
  "gravity", "vec3(0, -12, 0)", "draw_mode", "MESH", "mesh_primitive", "BOX", "blend", "OPAQUE", "size", "0.2", "size_random", "0.6",
  "color", "color(0.16, 0.14, 0.13, 1)", "spin", "6", "bounce", "0.3", "collide_depth", "true", NULL };
static const char *const K_TRAIL[] = { "amount", "220", "lifetime", "1.2", "emission_shape", "PARENT", "parent", "\"../Debris\"",
  "one_shot", "true", "inherit_velocity", "0.05", "spread", "180", "speed_min", "0.1", "speed_max", "0.5", "gravity", "vec3(0, 0.6, 0)",
  "blend", "ALPHA", "soft_distance", "0.4", "size", "0.8", "size_curve", "\"0 0.4, 1 1.4\"",
  "color_ramp", "\"0 0.8 0.75 0.7 0, 0.15 0.5 0.47 0.45 0.5, 1 0.3 0.3 0.3 0\"",
  "texture", "\"fx/smoke.png\"", "hframes", "8", "vframes", "8", "spin", "0.4", NULL };
static const char *const K_EMBERS[] = { "amount", "70", "lifetime", "2.2", "lifetime_random", "0.5", "explosiveness", "0.8",
  "one_shot", "true", "emission_shape", "SPHERE", "emission_radius", "1.0", "spread", "180", "speed_min", "0.5", "speed_max", "3",
  "gravity", "vec3(0, 1.2, 0)", "damping", "1", "size", "0.07", "size_random", "0.5",
  "color_ramp", "\"0 1 0.7 0.3 1, 0.7 1 0.4 0.1 1, 1 0.6 0.1 0.02 0\"", "energy", "6", "blend", "ADD",
  "turbulence", "2", "turbulence_scale", "0.4", NULL };
static const char *const K_HAZE[] = { "amount", "30", "lifetime", "2.2", "explosiveness", "0.35", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "1.2", "direction", "vec3(0, 1, 0)", "spread", "50", "speed_min", "0.8", "speed_max", "2.2",
  "turbulence", "0.7", "blend", "DISTORT", "distort_strength", "0.12", "size", "4", "size_curve", "\"0 0.6, 1 1.6\"",
  "color_ramp", "\"0 1 1 1 0, 0.15 1 1 1 1, 1 1 1 1 0\"", NULL };
/* a small blast: one fire puff, sparks, a little smoke */
static const char *const S_FIRE[] = { "amount", "4", "lifetime", "0.8", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "0.3", "speed_max", "1.4",
  "gravity", "vec3(0, 1.4, 0)", "damping", "2", "size", "2.2", "size_random", "0.3", "size_curve", "\"0 0.5, 0.3 1, 1 1.1\"",
  "color_ramp", "\"0 1 1 1 0, 0.08 1 1 1 1, 1 1 0.75 0.6 0\"", "energy", "1.5",
  "texture", "\"fx/fire.png\"", "motion_texture", "\"fx/fire_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.4", "soft_distance", "0.5", NULL };
static const char *const S_SPARKS[] = { "amount", "50", "lifetime", "0.8", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "180", "speed_min", "3", "speed_max", "9", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -12, 0)", "draw_mode", "STRETCHED", "stretch", "0.04", "size", "0.04",
  "color_ramp", "\"0 1 0.9 0.6 1, 1 1 0.4 0.1 0\"", "energy", "6", "blend", "ADD", NULL };
static const char *const S_SMOKE[] = { "amount", "5", "lifetime", "2.2", "explosiveness", "0.9", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.4", "direction", "vec3(0, 1, 0)", "spread", "90", "speed_min", "0.4", "speed_max", "1.6",
  "gravity", "vec3(0, 0.8, 0)", "damping", "1.2", "blend", "ALPHA", "soft_distance", "0.6", "size", "2.0", "size_curve", "\"0 0.4, 0.3 1, 1 1.4\"",
  "color_ramp", "\"0 0.8 0.7 0.6 0, 0.12 0.45 0.42 0.4 0.7, 1 0.25 0.25 0.26 0\"",
  "texture", "\"fx/smoke.png\"", "motion_texture", "\"fx/smoke_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.2", NULL };
/* a level-up: a ring of cool sparks round the player */
static const char *const LEVELUP[] = { "amount", "60", "lifetime", "0.7", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.5", "direction", "vec3(0, 1, 0)", "spread", "40", "speed_min", "2", "speed_max", "5",
  "gravity", "vec3(0, -3, 0)", "damping", "2", "size", "0.12", "size_random", "0.5",
  "color_ramp", "\"0 0.7 1 0.95 1, 1 0.2 0.8 1 0\"", "energy", "5", "blend", "ADD", NULL };
/* a muzzle flash: a hot cone of short-lived sparks out of the barrel */
static const char *const MUZZLE[] = { "amount", "18", "lifetime", "0.09", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "16", "speed_min", "6", "speed_max", "14", "draw_mode", "STRETCHED", "stretch", "0.03",
  "size", "0.09", "color_ramp", "\"0 1 0.95 0.75 1, 1 1 0.5 0.1 0\"", "energy", "9", "blend", "ADD", NULL };
static const char *const HIT[] = { "amount", "14", "lifetime", "0.35", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.05", "spread", "180", "speed_min", "2", "speed_max", "6",
  "gravity", "vec3(0, -8, 0)", "draw_mode", "STRETCHED", "stretch", "0.04", "size", "0.05",
  "color_ramp", "\"0 1 0.9 0.5 1, 1 1 0.4 0.1 0\"", "energy", "5", "blend", "ADD", NULL };
/* an enemy's strike: a fan of pale streaks raked across in front of it (a claw, a fist, a sweep) */
static const char *const SLASH[] = { "amount", "16", "lifetime", "0.16", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "POINT", "spread", "55", "speed_min", "7", "speed_max", "13", "draw_mode", "STRETCHED", "stretch", "0.05",
  "size", "0.06", "color_ramp", "\"0 0.95 0.92 1 1, 1 1 0.75 0.6 0\"", "energy", "4", "blend", "ADD", NULL };
/* cold: pale blue-white shards (a frost lance's hit, a frozen one shattering) */
static const char *const FROST[] = { "amount", "16", "lifetime", "0.5", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.15", "spread", "180", "speed_min", "2", "speed_max", "7",
  "gravity", "vec3(0, -9, 0)", "draw_mode", "STRETCHED", "stretch", "0.05", "size", "0.07",
  "color_ramp", "\"0 0.85 0.95 1 1, 1 0.4 0.7 1 0\"", "energy", "4", "blend", "ADD", NULL };
static const char *const SPLAT[] = { "amount", "26", "lifetime", "0.7", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.1", "spread", "70", "direction", "vec3(0, 1, 0)", "speed_min", "1.5", "speed_max", "5",
  "gravity", "vec3(0, -12, 0)", "size", "0.12", "size_random", "0.5", "size_curve", "\"0 1, 1 0.4\"",
  "color_ramp", "\"0 0.75 0.1 0.08 1, 1 0.4 0.04 0.04 0\"", "blend", "ALPHA", NULL };
static const char *const FIREP[] = { "amount", "70", "lifetime", "1.1", "lifetime_random", "0.4", "emitting", "false", "one_shot", "false",
  "emission_shape", "SPHERE", "emission_radius", "0.9", "direction", "vec3(0, 1, 0)", "spread", "25", "speed_min", "0.8", "speed_max", "2.6",
  "gravity", "vec3(0, 3, 0)", "damping", "1", "size", "0.9", "size_random", "0.4", "size_curve", "\"0 0.5, 0.3 1, 1 0.1\"",
  "color_ramp", "\"0 1 0.8 0.4 1, 0.3 1 0.45 0.08 0.9, 1 0.3 0.04 0.02 0\"", "energy", "2.2", "blend", "ADD",
  "texture", "\"fx/fire.png\"", "motion_texture", "\"fx/fire_mv.png\"", "hframes", "8", "vframes", "8", "spin", "0.6",
  "turbulence", "1.4", "turbulence_scale", "0.5", NULL };

/* one pooled Explosion3D: its parts are children (parents before sub-emitters) */
static cw_node boom_node(const char *name, bool big, int seed) {
  cw_node e = cw_node_new("Explosion3D", name);
  cw_node_add_child(FX.root, e);
  cw_node_set(e, "autostart", cw_val_bool(false));
  cw_node_set(e, "duration", cw_val_float(big ? 4.0f : 2.4f));
  cw_node_set(e, "flash_energy", cw_val_float(big ? 170.0f : 45.0f));
  cw_node_set(e, "flash_range", cw_val_float(big ? 15.0f : 8.0f));
  cw_node_set(e, "flash_duration", cw_val_float(big ? 0.5f : 0.3f));
  cw_node_set(e, "scorch_texture", cw_val_string("fx/scorch.png"));
  cw_node_set(e, "scorch_size", cw_val_float(big ? 5.5f : 2.6f));
  cw_node_set(e, "scorch_time", cw_val_float(big ? 7.0f : 3.0f));
  cw_node_set(e, "scorch_life", cw_val_float(big ? 40.0f : 18.0f));
  cw_node_set(e, "scorch_glow", cw_val_float(big ? 3.0f : 1.5f));
  /* no camera shake (the pads rumble instead); the old one only for the camera gate's control */
  cw_node_set(e, "shake_amount", cw_val_float(FX.shake_control ? (big ? 0.8f : 0.25f) : 0.0f));
  cw_node_set(e, "shake_radius", cw_val_float(30));
  cw_node_set(e, "shake_time", cw_val_float(big ? 0.8f : 0.4f));
  cw_node3d_set_position(e, (t3_vec3){ 0, -60, 0 });
  if (big) {
    pnode(e, "Fire", K_FIRE, seed), pnode(e, "Flames", K_FLAMES, seed + 1), pnode(e, "Smoke", K_SMOKE, seed + 2);
    pnode(e, "Sparks", K_SPARKS, seed + 3), pnode(e, "Debris", K_DEBRIS, seed + 4), pnode(e, "DebrisTrail", K_TRAIL, seed + 5);
    pnode(e, "Embers", K_EMBERS, seed + 6), pnode(e, "Haze", K_HAZE, seed + 7);
  } else
    pnode(e, "Fire", S_FIRE, seed), pnode(e, "Sparks", S_SPARKS, seed + 1), pnode(e, "Smoke", S_SMOKE, seed + 2);
  cw_node w = cw_node_new("Shockwave3D", "Wave");
  cw_node_add_child(e, w);
  cw_node_set(w, "ground", cw_val_bool(true));
  cw_node_set(w, "autostart", cw_val_bool(false));
  cw_node_set(w, "max_radius", cw_val_float(big ? 9.0f : 4.0f));
  cw_node_set(w, "duration", cw_val_float(big ? 0.8f : 0.5f));
  cw_node_set(w, "thickness", cw_val_float(big ? 1.4f : 0.8f));
  cw_node_set(w, "strength", cw_val_float(big ? 0.7f : 0.4f));
  cw_node3d_set_position(w, (t3_vec3){ 0, 0.3f, 0 });
  return e;
}

void fx_free(void) {
  if (FX.ready) {
    if (FX.gibs) t3_release(FX.gibs);
  }
  memset(&FX, 0, sizeof FX);
}

static t3_instanced_mesh *imesh(const char *name, t3_geometry *g, t3_material *m, int n, bool shadow) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  im->mesh.base.cast_shadow = shadow;
  for (int i = 0; i < n; i++) {
    t3_instanced_mesh_set_matrix_at(im, i, &ZERO);
    t3_instanced_mesh_set_color_at(im, i, (t3_color){ 1, 1, 1 });
  }
  im->instance_matrix->usage = T3_DYNAMIC_DRAW;
  im->instance_color->usage = T3_DYNAMIC_DRAW;
  t3_sphere sph = { { 0, 0, 0 }, HALF * 1.5f };
  t3_instanced_mesh_set_bounding_sphere(im, &sph);
  cw_node node = cw_node_new("Node3D", name);
  cw_node_add_child(FX.root, node);
  t3_object *wrap = t3_group_new();
  t3_object_add(wrap, &im->mesh.base);
  cw_node3d_set_object(node, wrap);
  t3_release(wrap), t3_release(g), t3_release(m);
  im->count = 0;
  return im;
}

static cw_node decal(const char *name, float r, float g, float b, float a) {
  cw_node d = cw_node_new("Decal3D", name);
  cw_node_add_child(FX.root, d);
  cw_node_set(d, "blend", cw_val_enum("MULTIPLY"));
  cw_node_set(d, "color", cw_val_color(r, g, b, a));
  cw_node_set(d, "normal_fade", cw_val_float(0.3f));
  cw_node_set(d, "edge_fade", cw_val_float(0.35f));
  cw_node3d_set_rotation(d, (t3_vec3){ -1.5707963f, 0, 0 });
  cw_node3d_set_position(d, (t3_vec3){ 0, -60, 0 });
  return d;
}

void fx_init(cw_node root) {
  fx_free();
  FX.rng = 12345;
  FX.shake_control = getenv("HORDE_SHAKE_CONTROL") != NULL;
  FX.root = cw_node_new("Node3D", "FX");
  cw_node_add_child(root, FX.root);
  for (int i = 0; i < BIGS; i++) FX.big[i] = boom_node("BigBlast", true, 100 + i * 10);
  for (int i = 0; i < SMALLB; i++) FX.small[i] = boom_node("SmallBlast", false, 200 + i * 10);
  FX.level[0] = bnode(FX.root, "LevelUp", LEVELUP, 600, LEVELS);
  FX.hit[0] = bnode(FX.root, "Hit", HIT, 700, HITS);
  FX.muzzle[0] = bnode(FX.root, "Muzzle", MUZZLE, 750, MUZZLES);
  FX.splat[0] = bnode(FX.root, "Splat", SPLAT, 800, SPLATS);
  FX.slash = bnode(FX.root, "Slash", SLASH, 850, SLASHES);
  FX.frost = bnode(FX.root, "Frost", FROST, 870, 24);
  { /* lightning: bright jagged bolts, a few segments a jump, re-jagged every frame while they last */
    t3_material *bm = t3_mesh_standard_material_new(0xffffff);
    bm->color = (t3_color){ 0.1f, 0.12f, 0.15f };
    bm->emissive = (t3_color){ 0.65f, 0.85f, 1.0f };
    bm->emissive_intensity = 22;
    FX.bolts = imesh("Bolts", t3_box_geometry_new(1, 1, 1, 1, 1, 1), bm, BOLTS * 5, false);
  }
  for (int i = 0; i < FIRES; i++) {
    FX.fire[i] = pnode(FX.root, "Fire", FIREP, 900 + i);
    FX.fire_haze[i] = pnode(FX.root, "FireHaze", FIREHAZE, 950 + i);
    FX.fire_light[i] = cw_node_new("OmniLight3D", "FireLight");
    cw_node_add_child(FX.root, FX.fire_light[i]);
    cw_node3d_set_position(FX.fire_light[i], (t3_vec3){ 0, -60, 0 });
    cw_node_set(FX.fire_light[i], "energy", cw_val_float(0));
    cw_node_set(FX.fire_light[i], "range", cw_val_float(7));
    cw_node_set(FX.fire_light[i], "decay", cw_val_float(2));
    cw_node_set(FX.fire_light[i], "color", cw_val_color(1, 0.5f, 0.15f, 1));
    cw_node_set(FX.fire_light[i], "fog_energy", cw_val_float(0.5f));
  }
  for (int i = 0; i < STAIN; i++) FX.stain[i] = decal("Stain", 0.5f, 0.1f, 0.08f, 0);
  cw_node ls = cw_node_new("LightingSettings", "Lighting");
  cw_node_add_child(FX.root, ls);
  cw_node_set(ls, "mode", cw_val_enum("CLUSTERED"));
  cw_node_set(ls, "cluster_far", cw_val_float(80));
  for (int i = 0; i < LIGHTS; i++) {
    cw_node l = FX.light[i] = cw_node_new("OmniLight3D", "Flash");
    cw_node_add_child(FX.root, l);
    cw_node3d_set_position(l, (t3_vec3){ 0, -60, 0 });
    cw_node_set(l, "energy", cw_val_float(0));
    cw_node_set(l, "range", cw_val_float(12));
    cw_node_set(l, "decay", cw_val_float(2));
    cw_node_set(l, "fog_energy", cw_val_float(0));
  }
  /* gibs: small chunks */
  t3_material *gm = t3_mesh_standard_material_new(0xffffff);
  gm->roughness = 0.75f;
  FX.gibs = imesh("Gibs", t3_box_geometry_new(1, 1, 1, 1, 1, 1), gm, GIBS, false);
  FX.ready = true;
}

/* the camera gate's control only (HORDE_SHAKE_CONTROL): the old explosion-kit shake, to show the gate sees it */
void fx_control_shake(float *x, float *y) {
  *x = *y = 0;
  if (!FX.ready || !FX.shake_control) return;
  t3_vec3 o = cw_shake_offset(cw_explosion_shake(cw_node_tree(FX.root), FX.focus), FX.tick, 0.45f);
  *x = o.x, *y = o.z;
}

/* a rumble for one player's pad this frame: the strongest request of the frame is sent */
static void rumble(int p, float low, float high, int ms) {
  if (p < 0 || p >= MAXP || low + high <= FX.want_low[p] + FX.want_high[p]) return;
  FX.want_low[p] = low, FX.want_high[p] = high, FX.want_ms[p] = ms;
}
/* every player within reach of a blast: the heavy motor, falling off with distance */
static void rumble_blast(const sim *s, float x, float y, bool big) {
  float reach = big ? 30.0f : 20.0f;
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (!p->active) continue;
    float d = sqrtf((p->x - x) * (p->x - x) + (p->y - y) * (p->y - y)), k = 1.0f - d / reach;
    if (k <= 0.05f) continue;
    k *= k;
    rumble(i, (big ? 1.0f : 0.7f) * k, (big ? 0.3f : 0.2f) * k, big ? 280 : 170);
  }
}
static void rumble_send(void) {
  for (int i = 0; i < MAXP; i++) {
    float k = FX.want_low[i] + FX.want_high[i];
    if (k > 0 && (FX.tick >= FX.rum_end[i] || k >= FX.rum_k[i])) {
      cw_input_rumble(i, FX.want_low[i], FX.want_high[i], FX.want_ms[i]);
      FX.rum_k[i] = k, FX.rum_end[i] = FX.tick + (uint32_t)(FX.want_ms[i] * 60 / 1000);
    }
    FX.want_low[i] = FX.want_high[i] = 0, FX.want_ms[i] = 0;
  }
}
fx_stats fx_get_stats(void) {
  fx_stats s = { 0 };
  s.gibs_live = FX.live_g;
  s.booms = FX.booms_total;
  cw_explosion_stats es;
  cw_explosion_get_stats(cw_node_tree(FX.root), &es);
  s.decals_live += es.scorches_active;
  for (int i = 0; i < STAIN; i++) s.decals_live += FX.stain_a[i] > 0;
  for (int i = 0; i < LIGHTS; i++) s.lights_live += FX.light_e[i] > 0.01f;
  return s;
}

static void set_decal(cw_node d, t3_vec3 at, float size, float rot) {
  cw_node3d_set_position(d, (t3_vec3){ at.x, 0.4f, at.z });
  cw_node3d_set_rotation(d, (t3_vec3){ -1.5707963f, 0, rot });
  cw_node_set(d, "size", cw_val_vec3(size, size, 1.0f));
}

static void flash_light(t3_vec3 at, float e, float r, float g, float b) {
  int i = FX.nl++ % LIGHTS;
  cw_node3d_set_position(FX.light[i], (t3_vec3){ at.x, 1.6f, at.z });
  FX.light_e[i] = e;
  FX.light_e0[i] = e;
  FX.light_c[i][0] = r, FX.light_c[i][1] = g, FX.light_c[i][2] = b;
}

static void spawn_gibs(t3_vec3 at, int n, t3_color c, float speed, float dirx, float dirz) {
  for (int k = 0; k < n; k++) {
    gib *g = &FX.g[FX.next_g];
    FX.next_g = (FX.next_g + 1) % GIBS;
    float a = rnd() * 6.2832f, v = speed * (0.4f + rnd());
    g->p = (t3_vec3){ at.x + (rnd() - 0.5f) * 0.4f, 0.4f + rnd() * 0.5f, at.z + (rnd() - 0.5f) * 0.4f };
    g->v = (t3_vec3){ cw_cos(a) * v + dirx * 0.05f, 2.5f + rnd() * 5.0f, cw_sin(a) * v + dirz * 0.05f };
    g->rot = (t3_vec3){ rnd() * 6, rnd() * 6, rnd() * 6 };
    g->spin = (t3_vec3){ (rnd() - 0.5f) * 20, (rnd() - 0.5f) * 20, (rnd() - 0.5f) * 20 };
    g->size = 0.05f + rnd() * 0.09f;
    g->life = 60 * 2 + (int)(rnd() * 60);
    float t = 0.6f + 0.6f * rnd();
    g->col = (t3_color){ c.r * t, c.g * t, c.b * t };
  }
}

static void blast(t3_vec3 at, float r, bool big) {
  FX.booms_total++;
  if (FX.frame_booms++ > (big ? 3 : 6)) return; /* a chain of blasts in one frame: the first few carry it */
  cw_node e = big ? FX.big[FX.nb++ % BIGS] : FX.small[FX.ns++ % SMALLB];
  cw_node3d_set_position(e, (t3_vec3){ at.x, 0, at.z });
  cw_node3d_set_scale(e, (t3_vec3){ 1, 1, 1 });
  if (big) { /* a bigger blast radius draws a bigger mark and ring */
    float k = CW_CLAMP(r / 4.2f, 0.7f, 1.8f);
    cw_node_set(e, "scorch_size", cw_val_float(5.0f * k));
    cw_node_set(cw_node_get(e, "Wave"), "max_radius", cw_val_float(r * 1.9f));
  }
  cw_node3d_global_transform(e);
  cw_explosion_fire(e);
}

void fx_events(sim *s) {
  if (!FX.ready) {
    s->ev_n = 0;
    return;
  }
  FX.frame_hits = FX.frame_blood = FX.frame_gibs = FX.frame_booms = 0;
  int lvl = g_fx_level;
  unsigned shot_seen = 0;
  for (int i = 0; i < s->ev_n; i++) {
    const event *e = &s->ev[i];
    t3_vec3 at = { e->x, 0.7f, e->y };
    switch (e->kind) {
    case EV_BOOM: blast(at, e->z, e->w > 0.5f), rumble_blast(s, e->x, e->y, e->w > 0.5f); break;
    case EV_HIT:
      if (FX.frame_hits++ < (lvl >= 2 ? 10 : lvl >= 1 ? 6 : 3)) cw_particles_burst(e->b == 10 ? FX.frost : FX.hit[0], (t3_vec3){ at.x, 0.9f, at.z }, (t3_vec3){ 0, 0, 0 });
      break;
    case EV_ROCKET:
    case EV_SHOT:
      if (!(shot_seen & (1u << (e->a & 7)))) { /* each player's first shot this frame: a flash of light and a cone of fire at the barrel */
        shot_seen |= 1u << (e->a & 7);
        if (lvl >= 1) flash_light((t3_vec3){ at.x + e->z * 0.02f, 0, at.z + e->w * 0.02f }, 16, 1, 0.75f, 0.35f);
        cw_particles_burst(FX.muzzle[0], (t3_vec3){ at.x, 0.95f, at.z }, (t3_vec3){ e->z, 0.05f, e->w });
        /* a heavy weapon's shot: a light tick (rockets, the rail gun, the shotgun; not every bullet) */
        const player *p = e->a < MAXP ? &s->p[e->a] : NULL;
        int w = p && p->pu_weapon_t > 0 ? p->pu_weapon : -1;
        if (e->kind == EV_ROCKET) rumble(e->a, 0.25f, 0.3f, 70);
        else if (w == PU_RAIL) rumble(e->a, 0.15f, 0.45f, 60);
        else if (w == PU_SHOTGUN) rumble(e->a, 0.3f, 0.25f, 60);
      }
      break;
    case EV_KILL: {
      if (e->a == EK_BRUTE || e->a == EK_BOSS) { /* a big kill reads: a burst of the explosion kit and a short rumble */
        blast(at, e->a == EK_BOSS ? 4.0f : 1.6f, e->a == EK_BOSS);
        if (e->a == EK_BOSS) rumble_blast(s, e->x, e->y, true);
        else rumble(e->b, 0.35f, 0.2f, 90);
      }
      static const t3_color BC[EK_KINDS] = { { 0.16f, 0.05f, 0.04f }, { 0.14f, 0.05f, 0.04f }, { 0.14f, 0.17f, 0.05f }, { 0.15f, 0.03f, 0.05f }, { 0.1f, 0.13f, 0.1f }, { 0.55f, 0.5f, 0.4f }, { 0.12f, 0.08f, 0.06f }, { 0.2f, 0.35f, 0.35f }, { 0.1f, 0.1f, 0.12f }, { 0.3f, 0.28f, 0.3f }, { 0.25f, 0.05f, 0.03f } }; /* skitter, bonewalker, slag, spore, penitent ash, gargoyle stone, hellhound */
      int n = e->a == EK_RUNNER ? 3 : e->a == EK_BOSS ? 90 : 14;
      if (lvl == 0) n = n / 2 + 1;
      if (FX.frame_gibs < 40 || e->a != EK_RUNNER) {
        FX.frame_gibs += n;
        spawn_gibs(at, n, BC[e->a], e->a == EK_BOSS ? 10.0f : 5.0f, e->z, e->w);
      }
      if (FX.frame_blood++ < 6 || e->a != EK_RUNNER) {
        int st = FX.nst++ % STAIN;
        float sz = e->a == EK_RUNNER ? 1.3f : e->a == EK_BOSS ? 6.0f : 2.4f;
        set_decal(FX.stain[st], at, sz * (0.8f + 0.5f * rnd()), rnd() * 6.28f);
        FX.stain_a[st] = 0.85f;
        if (e->a != EK_RUNNER || lvl >= 1) cw_particles_burst(FX.splat[0], (t3_vec3){ at.x, 0.4f, at.z }, (t3_vec3){ 0, 0, 0 });
      }
      break;
    }
    case EV_BARREL: { /* the barrel leaves a fire */
      int f = FX.nf++ % FIRES;
      cw_node3d_set_position(FX.fire[f], (t3_vec3){ at.x, 0.2f, at.z });
      cw_node_set(FX.fire[f], "emitting", cw_val_bool(true));
      cw_node3d_set_position(FX.fire_light[f], (t3_vec3){ at.x, 1.2f, at.z });
      FX.fire_pos[f] = (t3_vec3){ at.x, 0.4f, at.z };
      FX.fire_age[f] = 1;
      break;
    }
    case EV_CRATE: spawn_gibs(at, lvl ? 14 : 7, (t3_color){ 0.5f, 0.32f, 0.15f }, 4.0f, 0, 0); break;
    case EV_WALL: spawn_gibs(at, 26, (t3_color){ 0.5f, 0.5f, 0.48f }, 5.0f, 0, 0); break;
    case EV_HURT: { /* getting hit: short and sharp, heavier for a heavier hitter; a melee hit bleeds */
      static const float RL[EK_KINDS] = { 0.3f, 0.85f, 0.5f, 1.0f, 0.25f, 0.4f, 0.7f, 0.3f, 0.35f, 0.45f, 0.4f }, RH[EK_KINDS] = { 0.85f, 0.6f, 0.5f, 0.7f, 0.9f, 0.8f, 0.5f, 0.7f, 0.85f, 0.7f, 0.9f };
      static const int RMS[EK_KINDS] = { 90, 160, 120, 220, 80, 110, 140, 90, 100, 110, 90 };
      int k = e->b < EK_KINDS ? e->b : 0;
      rumble(e->a, RL[k], RH[k], RMS[k]);
      if (e->b < EK_KINDS) {
        cw_particles_burst(FX.splat[0], (t3_vec3){ at.x, 0.9f, at.z }, (t3_vec3){ 0, 0, 0 });
        int st = FX.nst++ % STAIN;
        set_decal(FX.stain[st], at, 0.9f + 0.4f * rnd(), rnd() * 6.28f);
        FX.stain_a[st] = 0.7f;
      }
      break;
    }
    case EV_MELT: /* the boss fell: the horde burns to ash, staggered over a second and a half (sparks rising, a few grey cinders) */
      if (FX.frame_hits++ < (lvl >= 2 ? 14 : lvl >= 1 ? 8 : 4)) cw_particles_burst(FX.hit[0], (t3_vec3){ at.x, 0.6f, at.z }, (t3_vec3){ 0, 1, 0 });
      if (FX.frame_blood++ < (lvl >= 1 ? 3 : 1)) cw_particles_burst(FX.level[0], (t3_vec3){ at.x, 0.2f, at.z }, (t3_vec3){ 0, 0, 0 }); /* a ring of embers where it stood */
      if (FX.frame_gibs < 60) FX.frame_gibs += 2, spawn_gibs(at, 2, (t3_color){ 0.16f, 0.15f, 0.14f }, 2.5f, 0, 0);
      break;
    case EV_SPIKES: /* a boss's ring (the crozier's spikes, the hammer's shockwave): sparks round it, the blast's light */
      cw_particles_burst(FX.level[0], (t3_vec3){ at.x, 0.2f, at.z }, (t3_vec3){ 0, 0, 0 });
      flash_light(at, 40, 1, 0.75f, 0.45f);
      for (int k = 0; k < MAXP; k++)
        if (s->p[k].active) rumble_blast(s, e->x, e->y, true), k = MAXP;
      break;
    case EV_ACID: case EV_RISE: case EV_BROOD: /* a splash where acid, fire or spores land; bones or a brood stirring */
      if (FX.frame_hits++ < 10) cw_particles_burst(FX.hit[0], (t3_vec3){ at.x, 0.4f, at.z }, (t3_vec3){ 0, 1, 0 });
      break;
    case EV_ARC: { /* lightning: a jagged bolt from jump to jump, a flash at each, the water lit where it carries */
      int k = FX.nbolt++ % BOLTS;
      FX.bolt[k].x0 = e->x, FX.bolt[k].y0 = e->y, FX.bolt[k].x1 = e->z, FX.bolt[k].y1 = e->w, FX.bolt[k].life = 8;
      if (FX.frame_hits++ < 24) cw_particles_burst(FX.frost, (t3_vec3){ e->z, 1.0f, e->w }, (t3_vec3){ 0, 0, 0 });
      flash_light((t3_vec3){ e->z, 0.8f, e->w }, e->b ? 30 : 18, 0.55f, 0.8f, 1.0f);
      break;
    }
    case EV_BOUNCE: /* the wheel glances off: a spray of sparks */
      cw_particles_burst(FX.hit[0], (t3_vec3){ at.x, 0.9f, at.z }, (t3_vec3){ 0, 0, 0 });
      flash_light(at, 10, 1, 0.85f, 0.5f);
      break;
    case EV_SHATTER: /* a frozen one breaks into shards */
      cw_particles_burst(FX.frost, (t3_vec3){ at.x, 0.8f, at.z }, (t3_vec3){ 0, 0, 0 });
      flash_light(at, 10, 0.6f, 0.85f, 1.0f);
      break;
    case EV_LANCE: /* a bone lance thrown */
      cw_particles_burst(FX.muzzle[0], (t3_vec3){ at.x, 1.2f, at.z }, (t3_vec3){ e->z, 0, e->w });
      break;
    case EV_BOSS_TELL: /* a boss winds up its arena attack or summon: a low warning on every pad */
      for (int k = 0; k < MAXP; k++)
        if (s->p[k].active) rumble(k, 0.25f, 0.05f, 300);
      break;
    case EV_HATCH: /* the way down opens: a long low rumble on every pad */
      for (int k = 0; k < MAXP; k++)
        if (s->p[k].active) rumble(k, 0.45f, 0.15f, 700);
      break;
    case EV_SWIPE: /* the strike's streak, raked across the front */
      cw_particles_burst(FX.slash, (t3_vec3){ at.x, e->a == EK_BOSS ? 2.2f : e->a == EK_BRUTE ? 1.2f : 0.8f, at.z }, (t3_vec3){ e->z, -0.15f, e->w });
      break;
    case EV_LEVEL: cw_particles_burst(FX.level[0], (t3_vec3){ at.x, 0.3f, at.z }, (t3_vec3){ 0, 0, 0 }); break;
    case EV_POWER: /* taken: a ring of sparks and a flash in the power-up colour */
      cw_particles_burst(FX.level[0], (t3_vec3){ at.x, 0.3f, at.z }, (t3_vec3){ 0, 0, 0 });
      flash_light(at, 30, 1, 0.9f, 0.6f);
      break;
    case EV_POWER_SPAWN: cw_particles_burst(FX.level[0], (t3_vec3){ at.x, 0.3f, at.z }, (t3_vec3){ 0, 0, 0 }); break;
    default: break;
    }
  }
  s->ev_n = 0;
  rumble_send();
}

void fx_tick(const sim *s) {
  if (!FX.ready) return;
  const float dt = 1.0f / 60.0f;
  FX.tick++;
  {
    float cx, cy, hw, hh;
    sim_team_box(s, &cx, &cy, &hw, &hh);
    FX.focus = (t3_vec3){ cx, 0, cy };
  }
  { /* the lightning's bolts: four segments a jump, jagged afresh every frame */
    int nb = 0;
    for (int k = 0; k < BOLTS; k++) {
      if (FX.bolt[k].life <= 0) continue;
      FX.bolt[k].life--;
      float x0 = FX.bolt[k].x0, y0 = FX.bolt[k].y0, dx = FX.bolt[k].x1 - x0, dy = FX.bolt[k].y1 - y0, l = sqrtf(dx * dx + dy * dy);
      if (l < 0.05f) continue;
      float px = -dy / l, py = dx / l, ax = x0, ay = y0, ah = 1.0f;
      for (int j = 1; j <= 4; j++) {
        float t = (float)j / 4.0f, jit = j < 4 ? (rnd() - 0.5f) * 0.9f : 0;
        float bx = x0 + dx * t + px * jit, by = y0 + dy * t + py * jit, bh = j < 4 ? 0.8f + rnd() * 0.6f : 1.0f;
        float sx = bx - ax, sy = by - ay, sl = sqrtf(sx * sx + sy * sy + (bh - ah) * (bh - ah));
        t3_mat4 m;
        t3_quat q = t3_quat_multiply(t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, cw_atan2(sx, sy)), t3_quat_from_axis_angle((t3_vec3){ 1, 0, 0 }, -asinf((bh - ah) / sl)));
        float w = 0.06f + 0.05f * (float)FX.bolt[k].life / 8.0f;
        t3_mat4_compose(&m, (t3_vec3){ (ax + bx) * 0.5f, (ah + bh) * 0.5f, (ay + by) * 0.5f }, q, (t3_vec3){ w, w, sl });
        t3_instanced_mesh_set_matrix_at(FX.bolts, nb++, &m);
        ax = bx, ay = by, ah = bh;
      }
    }
    FX.bolts->count = nb;
    FX.bolts->instance_matrix->version++;
  }
  /* gibs */
  int n = 0;
  bool recolour = false;
  for (int i = 0; i < GIBS; i++) {
    gib *g = &FX.g[i];
    if (g->life <= 0) continue;
    g->life--;
    g->v.y -= 16 * dt;
    g->p.x += g->v.x * dt, g->p.y += g->v.y * dt, g->p.z += g->v.z * dt;
    if (g->p.y < g->size * 0.5f) {
      g->p.y = g->size * 0.5f;
      g->v.y = -g->v.y * 0.35f;
      g->v.x *= 0.6f, g->v.z *= 0.6f;
      g->spin.x *= 0.5f, g->spin.y *= 0.5f, g->spin.z *= 0.5f;
    }
    g->rot.x += g->spin.x * dt, g->rot.y += g->spin.y * dt, g->rot.z += g->spin.z * dt;
    if (g->life == 0) continue;
    float shrink = g->life < 50 ? (float)g->life / 50.0f : 1;
    t3_mat4 m;
    t3_quat q = t3_quat_from_euler((t3_euler){ g->rot.x, g->rot.y, g->rot.z, T3_XYZ });
    float sz = g->size * shrink;
    t3_mat4_compose(&m, g->p, q, (t3_vec3){ sz * 1.4f, sz, sz });
    t3_instanced_mesh_set_matrix_at(FX.gibs, n, &m);
    t3_instanced_mesh_set_color_at(FX.gibs, n, g->col);
    n++;
    recolour = true;
  }
  FX.live_g = n;
  FX.gibs->count = n;
  FX.gibs->instance_matrix->version++;
  if (recolour) FX.gibs->instance_color->version++;
  /* lights */
  for (int i = 0; i < LIGHTS; i++) {
    FX.light_e[i] *= 0.9f;
    if (FX.light_e[i] < 0.05f) FX.light_e[i] = 0;
    cw_node_set(FX.light[i], "energy", cw_val_float(FX.light_e[i]));
    { /* white-hot at the flash, the blast's own orange as it dies */
      float hot = FX.light_e0[i] > 0 ? CW_CLAMP(FX.light_e[i] / FX.light_e0[i], 0.0f, 1.0f) : 0;
      hot = hot * hot;
      cw_node_set(FX.light[i], "color", cw_val_color(FX.light_c[i][0], FX.light_c[i][1] + (1 - FX.light_c[i][1]) * hot * 0.7f, FX.light_c[i][2] + (1 - FX.light_c[i][2]) * hot * 0.8f, 1));
    }
  }
  /* decals fade */
  for (int i = 0; i < STAIN; i++)
    if (FX.stain_a[i] > 0) {
      FX.stain_a[i] = FX.stain_a[i] > 0.0015f ? FX.stain_a[i] - 0.0005f : 0;
      cw_node_set(FX.stain[i], "color", cw_val_color(0.55f, 0.1f, 0.08f, FX.stain_a[i]));
    }
  for (int f = 0; f < FIRES; f++) {
    if (FX.fire_age[f] <= 0) continue;
    int a = FX.fire_age[f]++;
    float e = a < 20 ? a / 20.0f * 18.0f : 18.0f * (1.0f - (float)(a - 20) / 420.0f) * (0.85f + 0.15f * cw_sin((float)a * 0.9f));
    if (a > 440) {
      FX.fire_age[f] = 0;
      cw_node_set(FX.fire[f], "emitting", cw_val_bool(false));
      e = 0;
    }
    cw_node_set(FX.fire_light[f], "energy", cw_val_float(e > 0 ? e : 0));
    if (a == 2) { cw_node3d_set_position(FX.fire_haze[f], FX.fire_pos[f]); cw_node_set(FX.fire_haze[f], "emitting", cw_val_bool(g_fx_level >= 1)); }
    if (a > 440) cw_node_set(FX.fire_haze[f], "emitting", cw_val_bool(false));
  }
}
