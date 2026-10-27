/* hordeshooter effects: explosions, fire, smoke, gibs, decals, lights.
 * Render only; they read the simulation's events and never feed it.
 *
 *   big blast   a pooled bundle of GPUParticles3D (white-hot burst, flame
 *               sheet, sparks that bounce off the scene, embers, a smoke
 *               column), a shockwave ring, a scorch decal, a short light
 *   small blast the burst and the sparks only
 *   kills       a spray of gibs (instanced chunks, CPU physics, bounce on
 *               the floor), a stain decal under the body
 *   hits        a puff of sparks, rate limited per frame
 *   fire        barrels leave a burning patch (a long lived emitter and a
 *               light) for some seconds
 *   lights      eight pooled omni lights through clustered lighting
 * The graphics setting "effects detail" thins every one of these. */
#include <stdio.h>
#include <string.h>

#include "cartwheel/decals.h"
#include "cartwheel/explosions.h"
#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "cartwheel/quality.h"
#include "game.h"

#define BUNDLES 8
#define SMALLS 24
#define HITS 20
#define SPLATS 20
#define FIRES 6
#define SCORCH 56
#define STAIN 180
#define LIGHTS 8
#define GIBS 3000
#define RINGS 24
#define WAVES 6

typedef struct bundle { cw_node burst, flame, sparks, embers, smoke, haze, flash; } bundle;
typedef struct gib { t3_vec3 p, v, rot, spin; float size; int life; t3_color col; } gib;

static struct {
  bool ready;
  cw_node root;
  bundle big[BUNDLES];
  cw_node small[SMALLS], hit[HITS], splat[SPLATS], fire[FIRES], fire_light[FIRES];
  int fire_age[FIRES];
  int nb, ns, nh, nsp, nf, nsc, nst, nl, ring_i;
  cw_node scorch[SCORCH], glow[SCORCH], stain[STAIN], light[LIGHTS], wave[WAVES], fire_haze[FIRES];
  float glow_a[SCORCH], light_e0[LIGHTS];
  t3_vec3 fire_pos[FIRES];
  int nw;
  float light_e[LIGHTS], light_c[LIGHTS][3];
  float scorch_a[SCORCH], stain_a[STAIN];
  t3_instanced_mesh *gibs, *rings;
  gib g[GIBS];
  int next_g, live_g;
  float ring_age[RINGS], ring_r[RINGS];
  t3_vec3 ring_p[RINGS];
  float shake, shake_t;
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
  cw_node3d_set_position(n, (t3_vec3){ 0, -60, 0 });
  return n;
}
static void burst_at(cw_node n, t3_vec3 at) {
  cw_node3d_set_position(n, at);
  cw_node3d_global_transform(n);
  cw_particles_restart(n);
}

static const char *const BURST[] = { "amount", "180", "lifetime", "0.6", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.4", "spread", "180", "speed_min", "2", "speed_max", "8",
  "gravity", "vec3(0, 3, 0)", "damping", "4", "size", "1.2", "size_random", "0.5", "size_curve", "\"0 0.5, 0.2 1.2, 1 0.2\"",
  "color_ramp", "\"0 1 0.95 0.7 1, 0.15 1 0.6 0.15 1, 0.5 0.9 0.25 0.04 0.8, 1 0.4 0.05 0.02 0\"", "energy", "3", "blend", "ADD",
  "turbulence", "1.5", "turbulence_scale", "0.6", NULL };
static const char *const FLAME[] = { "amount", "140", "lifetime", "0.9", "lifetime_random", "0.3", "explosiveness", "0.7", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "1.2", "direction", "vec3(0, 1, 0)", "spread", "60", "speed_min", "0.5", "speed_max", "3",
  "gravity", "vec3(0, 4, 0)", "damping", "1.5", "size", "1.4", "size_random", "0.4", "size_curve", "\"0 0.6, 0.25 1.1, 1 0.1\"",
  "color_ramp", "\"0 1 0.85 0.5 1, 0.2 1 0.5 0.1 1, 0.6 0.85 0.18 0.03 0.7, 1 0.35 0.04 0.02 0\"", "energy", "2.4", "blend", "ADD",
  "texture", "\"fx/fire_sheet.png\"", "hframes", "4", "vframes", "4", "spin", "1.5",
  "turbulence", "1.2", "turbulence_scale", "0.5", NULL };
static const char *const SPARKS[] = { "amount", "160", "lifetime", "1.1", "lifetime_random", "0.4", "explosiveness", "1",
  "one_shot", "true", "emission_shape", "SPHERE", "emission_radius", "0.3", "spread", "180", "speed_min", "6", "speed_max", "16",
  "direction", "vec3(0, 1, 0)", "gravity", "vec3(0, -14, 0)", "damping", "0.5", "draw_mode", "STRETCHED", "stretch", "0.05",
  "size", "0.06", "color_ramp", "\"0 1 0.95 0.7 1, 0.3 1 0.6 0.15 1, 1 0.7 0.12 0.04 0\"", "energy", "6", "bounce", "0.45",
  "collision_radius", "0.03", "collide_depth", "true", "trail_length", "10", "trail_width", "0.035", NULL };
static const char *const EMBERS[] = { "amount", "90", "lifetime", "2.2", "lifetime_random", "0.5", "explosiveness", "0.8",
  "one_shot", "true", "emission_shape", "SPHERE", "emission_radius", "1.0", "spread", "180", "speed_min", "0.5", "speed_max", "3",
  "gravity", "vec3(0, 1.2, 0)", "damping", "1", "size", "0.07", "size_random", "0.5",
  "color_ramp", "\"0 1 0.7 0.3 1, 0.7 1 0.4 0.1 1, 1 0.6 0.1 0.02 0\"", "energy", "6", "blend", "ADD",
  "turbulence", "2", "turbulence_scale", "0.4", NULL };
static const char *const SMOKE[] = { "amount", "90", "lifetime", "3.2", "lifetime_random", "0.3", "explosiveness", "0.85",
  "one_shot", "true", "emission_shape", "SPHERE", "emission_radius", "0.9", "spread", "180", "speed_min", "0.4", "speed_max", "2",
  "gravity", "vec3(0, 1.4, 0)", "damping", "1.2", "size", "1.1", "size_random", "0.5", "size_curve", "\"0 0.2, 0.3 1, 1 1.5\"",
  "color_ramp", "\"0 0.16 0.14 0.13 0, 0.12 0.2 0.18 0.17 0.55, 0.6 0.26 0.25 0.24 0.35, 1 0.34 0.33 0.32 0\"", "blend", "ALPHA", "soft_distance", "0.6",
  "texture", "\"fx/smoke_sheet.png\"", "hframes", "4", "vframes", "4", "spin", "0.6",
  "turbulence", "1", "turbulence_scale", "0.35", NULL };
/* heat haze over a blast and over a fire: slow big particles that bend what is behind them (blend DISTORT) */
static const char *const HAZE[] = { "amount", "46", "lifetime", "2.6", "lifetime_random", "0.3", "explosiveness", "0.5", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "1.6", "direction", "vec3(0, 1, 0)", "spread", "35", "speed_min", "0.4", "speed_max", "1.6",
  "gravity", "vec3(0, 1.5, 0)", "damping", "0.8", "size", "2.6", "size_random", "0.4", "size_curve", "\"0 0.4, 0.3 1, 1 1.4\"",
  "color_ramp", "\"0 1 1 1 0, 0.2 1 1 1 0.8, 1 1 1 1 0\"", "blend", "DISTORT", "distort_strength", "0.22",
  "texture", "\"fx/haze.png\"", "turbulence", "0.8", "turbulence_scale", "0.4", NULL };
static const char *const FIREHAZE[] = { "amount", "26", "lifetime", "1.8", "lifetime_random", "0.3", "emitting", "false", "one_shot", "false",
  "emission_shape", "SPHERE", "emission_radius", "0.8", "direction", "vec3(0, 1, 0)", "spread", "15", "speed_min", "0.8", "speed_max", "1.8",
  "gravity", "vec3(0, 2.5, 0)", "damping", "0.6", "size", "1.6", "size_random", "0.4", "size_curve", "\"0 0.5, 0.4 1, 1 1.3\"",
  "color_ramp", "\"0 1 1 1 0, 0.25 1 1 1 0.7, 1 1 1 1 0\"", "blend", "DISTORT", "distort_strength", "0.16",
  "texture", "\"fx/haze.png\"", "turbulence", "1.0", "turbulence_scale", "0.5", NULL };
static const char *const SBURST[] = { "amount", "50", "lifetime", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.2", "spread", "180", "speed_min", "1", "speed_max", "4",
  "gravity", "vec3(0, 2, 0)", "damping", "4", "size", "0.7", "size_random", "0.5", "size_curve", "\"0 0.5, 0.2 1.2, 1 0.2\"",
  "color_ramp", "\"0 1 0.9 0.6 1, 0.2 1 0.5 0.12 1, 1 0.4 0.05 0.02 0\"", "energy", "2.4", "blend", "ADD", NULL };
static const char *const HIT[] = { "amount", "14", "lifetime", "0.35", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.05", "spread", "180", "speed_min", "2", "speed_max", "6",
  "gravity", "vec3(0, -8, 0)", "draw_mode", "STRETCHED", "stretch", "0.04", "size", "0.05",
  "color_ramp", "\"0 1 0.9 0.5 1, 1 1 0.4 0.1 0\"", "energy", "5", "blend", "ADD", NULL };
static const char *const SPLAT[] = { "amount", "26", "lifetime", "0.7", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.1", "spread", "70", "direction", "vec3(0, 1, 0)", "speed_min", "1.5", "speed_max", "5",
  "gravity", "vec3(0, -12, 0)", "size", "0.12", "size_random", "0.5", "size_curve", "\"0 1, 1 0.4\"",
  "color_ramp", "\"0 0.75 0.1 0.08 1, 1 0.4 0.04 0.04 0\"", "blend", "ALPHA", NULL };
static const char *const FIREP[] = { "amount", "70", "lifetime", "1.1", "lifetime_random", "0.4", "emitting", "false", "one_shot", "false",
  "emission_shape", "SPHERE", "emission_radius", "0.9", "direction", "vec3(0, 1, 0)", "spread", "25", "speed_min", "0.8", "speed_max", "2.6",
  "gravity", "vec3(0, 3, 0)", "damping", "1", "size", "0.9", "size_random", "0.4", "size_curve", "\"0 0.5, 0.3 1, 1 0.1\"",
  "color_ramp", "\"0 1 0.8 0.4 1, 0.3 1 0.45 0.08 0.9, 1 0.3 0.04 0.02 0\"", "energy", "2.2", "blend", "ADD",
  "texture", "\"fx/fire_sheet.png\"", "hframes", "4", "vframes", "4", "spin", "1.2",
  "turbulence", "1.4", "turbulence_scale", "0.5", NULL };

void fx_free(void) {
  if (FX.ready) {
    if (FX.gibs) t3_release(FX.gibs);
    if (FX.rings) t3_release(FX.rings);
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
  FX.root = cw_node_new("Node3D", "FX");
  cw_node_add_child(root, FX.root);
  for (int i = 0; i < BUNDLES; i++) {
    bundle *b = &FX.big[i];
    b->burst = pnode(FX.root, "Burst", BURST, 100 + i);
    b->flame = pnode(FX.root, "Flame", FLAME, 200 + i);
    b->sparks = pnode(FX.root, "Sparks", SPARKS, 300 + i);
    b->embers = pnode(FX.root, "Embers", EMBERS, 400 + i);
    b->smoke = pnode(FX.root, "Smoke", SMOKE, 500 + i);
    b->haze = pnode(FX.root, "Haze", HAZE, 550 + i);
  }
  for (int i = 0; i < SMALLS; i++) FX.small[i] = pnode(FX.root, "Small", SBURST, 600 + i);
  for (int i = 0; i < HITS; i++) FX.hit[i] = pnode(FX.root, "Hit", HIT, 700 + i);
  for (int i = 0; i < SPLATS; i++) FX.splat[i] = pnode(FX.root, "Splat", SPLAT, 800 + i);
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
  for (int i = 0; i < SCORCH; i++) {
    FX.scorch[i] = decal("Scorch", 0.2f, 0.16f, 0.13f, 0);
    FX.glow[i] = decal("Glow", 1, 0.4f, 0.08f, 0); /* the hot ground: an additive decal that cools to nothing */
    cw_node_set(FX.glow[i], "blend", cw_val_enum("ADD"));
    cw_node_set(FX.glow[i], "energy", cw_val_float(3));
  }
  for (int i = 0; i < WAVES; i++) {
    cw_node w = FX.wave[i] = cw_node_new("Shockwave3D", "Wave");
    cw_node_add_child(FX.root, w);
    cw_node_set(w, "ground", cw_val_bool(true));
    cw_node_set(w, "autostart", cw_val_bool(false));
    cw_node_set(w, "loop", cw_val_bool(false));
    cw_node_set(w, "playing", cw_val_bool(false));
    cw_node_set(w, "duration", cw_val_float(0.7f));
    cw_node_set(w, "thickness", cw_val_float(1.6f));
    cw_node_set(w, "strength", cw_val_float(0.9f));
    cw_node_set(w, "max_radius", cw_val_float(8));
    cw_node3d_set_position(w, (t3_vec3){ 0, -60, 0 });
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
  gm->roughness = 0.6f;
  FX.gibs = imesh("Gibs", t3_box_geometry_new(1, 1, 1, 1, 1, 1), gm, GIBS, false);
  t3_material *rm = t3_mesh_basic_material_new(0xffffff);
  rm->blending = T3_ADDITIVE_BLENDING;
  rm->transparent = true;
  rm->depth_write = false;
  rm->side = T3_DOUBLE_SIDE;
  FX.rings = imesh("Rings", t3_cylinder_geometry_new(1, 1, 0.25f, 40, 1, true), rm, RINGS, false);
  FX.rings->mesh.base.render_order = 8;
  FX.ready = true;
}

void fx_shake(float *x, float *y) {
  *x = FX.shake * 0.35f * cw_sin(FX.shake_t * 53.0f);
  *y = FX.shake * 0.35f * cw_cos(FX.shake_t * 47.0f);
}
fx_stats fx_get_stats(void) {
  fx_stats s = { 0 };
  s.gibs_live = FX.live_g;
  s.booms = FX.booms_total;
  for (int i = 0; i < SCORCH; i++) s.decals_live += FX.scorch_a[i] > 0;
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
    g->size = 0.07f + rnd() * 0.12f;
    g->life = 60 * 6 + (int)(rnd() * 120);
    float t = 0.6f + 0.6f * rnd();
    g->col = (t3_color){ c.r * t, c.g * t, c.b * t };
  }
}

static void blast(t3_vec3 at, float r, bool big) {
  FX.booms_total++;
  if (FX.frame_booms++ > (big ? 5 : 10)) return;
  int lvl = g_fx_level;
  if (big) {
    bundle *b = &FX.big[FX.nb++ % BUNDLES];
    float k = r / 4.2f;
    burst_at(b->burst, (t3_vec3){ at.x, 0.5f, at.z });
    burst_at(b->flame, (t3_vec3){ at.x, 0.4f, at.z });
    burst_at(b->sparks, (t3_vec3){ at.x, 0.5f, at.z });
    if (lvl >= 1) burst_at(b->embers, (t3_vec3){ at.x, 0.6f, at.z });
    burst_at(b->smoke, (t3_vec3){ at.x, 0.9f, at.z });
    if (lvl >= 1) burst_at(b->haze, (t3_vec3){ at.x, 1.2f, at.z });
    (void)k;
    FX.shake = CW_MIN(FX.shake + 0.6f * r / 4.0f, 2.2f);
    flash_light(at, 170, 1, 0.55f, 0.2f);
    { /* a distortion ring that races over the floor, pooled */
      cw_node w = FX.wave[FX.nw++ % WAVES];
      cw_node3d_set_position(w, (t3_vec3){ at.x, 0.3f, at.z });
      cw_node_set(w, "max_radius", cw_val_float(r * 1.9f));
      cw_shockwave_fire(w);
    }
  } else {
    burst_at(FX.small[FX.ns++ % SMALLS], (t3_vec3){ at.x, 0.5f, at.z });
    FX.shake = CW_MIN(FX.shake + 0.25f, 2.2f);
    if (lvl >= 1) flash_light(at, 40, 1, 0.7f, 0.3f);
  }
  /* a shockwave ring, scorch under it */
  int ri = FX.ring_i++ % RINGS;
  FX.ring_age[ri] = 0.0001f, FX.ring_r[ri] = r, FX.ring_p[ri] = (t3_vec3){ at.x, 0.2f, at.z };
  int si = FX.nsc++ % SCORCH;
  set_decal(FX.scorch[si], at, r * (big ? 1.7f : 1.4f), rnd() * 6.28f);
  FX.scorch_a[si] = 0.9f;
  set_decal(FX.glow[si], at, r * (big ? 0.9f : 0.7f), rnd() * 6.28f);
  FX.glow_a[si] = big ? 0.35f : 0.2f;
}

void fx_events(sim *s) {
  if (!FX.ready) {
    s->ev_n = 0;
    return;
  }
  FX.frame_hits = FX.frame_blood = FX.frame_gibs = FX.frame_booms = 0;
  int lvl = g_fx_level;
  for (int i = 0; i < s->ev_n; i++) {
    const event *e = &s->ev[i];
    t3_vec3 at = { e->x, 0.7f, e->y };
    switch (e->kind) {
    case EV_BOOM: blast(at, e->z, e->w > 0.5f); break;
    case EV_HIT:
      if (FX.frame_hits++ < (lvl >= 2 ? 10 : lvl >= 1 ? 6 : 3)) burst_at(FX.hit[FX.nh++ % HITS], (t3_vec3){ at.x, 0.9f, at.z });
      break;
    case EV_ROCKET:
    case EV_SHOT:
      if (lvl >= 1 && e->kind == EV_SHOT && (i & 3) == 0) flash_light((t3_vec3){ at.x + e->z * 0.03f, 0, at.z + e->w * 0.03f }, 6, 1, 0.8f, 0.4f);
      break;
    case EV_KILL: {
      static const t3_color BC[EK_KINDS] = { { 0.5f, 0.12f, 0.08f }, { 0.4f, 0.1f, 0.07f }, { 0.4f, 0.5f, 0.1f }, { 0.4f, 0.06f, 0.1f } };
      int n = e->a == EK_RUNNER ? 5 : e->a == EK_BOSS ? 160 : 24;
      if (lvl == 0) n = n / 2 + 1;
      if (FX.frame_gibs < 90 || e->a != EK_RUNNER) {
        FX.frame_gibs += n;
        spawn_gibs(at, n, BC[e->a], e->a == EK_BOSS ? 10.0f : 5.0f, e->z, e->w);
      }
      if (FX.frame_blood++ < 6 || e->a != EK_RUNNER) {
        int st = FX.nst++ % STAIN;
        float sz = e->a == EK_RUNNER ? 1.3f : e->a == EK_BOSS ? 6.0f : 2.4f;
        set_decal(FX.stain[st], at, sz * (0.8f + 0.5f * rnd()), rnd() * 6.28f);
        FX.stain_a[st] = 0.85f;
        if (e->a != EK_RUNNER || lvl >= 1) burst_at(FX.splat[FX.nsp++ % SPLATS], (t3_vec3){ at.x, 0.4f, at.z });
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
    case EV_WALL: spawn_gibs(at, 26, (t3_color){ 0.5f, 0.5f, 0.48f }, 5.0f, 0, 0); FX.shake = CW_MIN(FX.shake + 0.4f, 2.2f); break;
    case EV_HURT: FX.shake = CW_MIN(FX.shake + 0.3f, 2.2f); break;
    case EV_LEVEL: burst_at(FX.small[FX.ns++ % SMALLS], (t3_vec3){ at.x, 0.8f, at.z }); break;
    default: break;
    }
  }
  s->ev_n = 0;
}

void fx_tick(const sim *s) {
  (void)s;
  if (!FX.ready) return;
  const float dt = 1.0f / 60.0f;
  FX.shake_t += dt;
  FX.shake *= 0.9f;
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
    float shrink = g->life < 40 ? (float)g->life / 40.0f : 1;
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
  /* rings */
  int nr = 0;
  for (int i = 0; i < RINGS; i++) {
    if (FX.ring_age[i] <= 0) continue;
    FX.ring_age[i] += dt;
    float t = FX.ring_age[i] / 0.5f;
    if (t >= 1) {
      FX.ring_age[i] = 0;
      continue;
    }
    float r = FX.ring_r[i] * (0.15f + 1.1f * (1 - (1 - t) * (1 - t)));
    t3_mat4 m;
    t3_mat4_compose(&m, FX.ring_p[i], (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ r, 1, r });
    t3_instanced_mesh_set_matrix_at(FX.rings, nr, &m);
    float f = (1 - t) * (1 - t) * 0.9f;
    t3_instanced_mesh_set_color_at(FX.rings, nr, (t3_color){ 1.0f * f, 0.6f * f, 0.25f * f });
    nr++;
  }
  FX.rings->count = nr;
  FX.rings->instance_matrix->version++;
  FX.rings->instance_color->version++;
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
  for (int i = 0; i < SCORCH; i++)
    if (FX.scorch_a[i] > 0) {
      FX.scorch_a[i] = FX.scorch_a[i] > 0.0015f ? FX.scorch_a[i] - 0.0007f : 0;
      cw_node_set(FX.scorch[i], "color", cw_val_color(0.2f, 0.16f, 0.13f, FX.scorch_a[i]));
    }
  for (int i = 0; i < SCORCH; i++)
    if (FX.glow_a[i] > 0) { /* the ground cools: white-orange, deep red, out over about five seconds */
      float a = FX.glow_a[i] = FX.glow_a[i] > 0.004f ? FX.glow_a[i] - 0.0012f : 0;
      float c = a * a;
      cw_node_set(FX.glow[i], "color", cw_val_color(1.0f * a, 0.38f * c + 0.05f * a * a * a, 0.06f * c * c, a * 0.8f));
    }
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
