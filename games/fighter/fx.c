/* fighter effects. All of them read the simulation's events; nothing here
 * feeds the rules. GPU particles simulate on the tree's fixed step from hashed
 * seeds, so every host draws the same effects.
 *
 *   hit      by strength: a light hit is a small spray of pale sparks and a few short lines, a medium one hot streaking
 *            sparks and impact lines, a heavy one (a knockdown, a launcher) those with a hot core, more lines and
 *            embers that fall to the floor. A flash light and one short soft flash scaled the same way.
 *            Round additive dots are kept small and dim: bright ones clip into flat discs.
 *   block    cool white-blue sparks and short impact lines
 *   landing and dashes  low smoke-flipbook dust along the floor (soft against it)
 *   knockdown and throws  a wide low cloud of that dust rolling out both ways and a few dark grit chips that land on
 *            the floor (particles are unlit: grit coloured like the lit floor, never pale cubes that read as confetti)
 *   fireball a glowing core with an ember trail, a muzzle flash; each fighter
 *            has its own colour
 *   super    the world dims for a moment (swelling in, lifting slowly), a burst of impact lines and streaks, a shockwave that bends the picture
 *   KO       the world dims, a shockwave in the air and one along the floor
 *   heavy hits fire a small Explosion3D at the contact (a hot burst, a fast shockwave that bends the picture, a flash);
 *            supers and knockouts a big one on the floor (rings in the air and along the ground, a flash)
 *
 * The camera never shakes: a hit, a throw, a wall splat, a knockdown and a KO are felt in the pads instead (rumble,
 * cartwheel/input.h), each fighter's in their own player's pad, never a computer fighter's. Presentation only. */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "cartwheel/explosions.h"
#include "cartwheel/input.h"
#include "game.h"

int fx_count_hit, fx_count_block;
int fx_count_impact; /* hits, blocks, throws, wall splats, supers and KOs: the impacts of the shake check (cam.offset) */
int32_t fx_rumble_sent[2]; /* debug rumble.sent: requests sent to each player's pad */

/* rumble: a request on top of one still running keeps the stronger of each motor and the longer time (a block's tick
 * never cuts a KO short); the motors stop on their own after ms */
static struct { const ft_game *g; double now, until[2]; float low[2], high[2]; } RB;
static void rumble(int who, float low, float high, int ms) {
  if (who < 0 || who > 1 || !RB.g || RB.g->cpu[who] || ms <= 0) return;
  double left = (RB.until[who] - RB.now) * 1000.0;
  if (left > 0) {
    if (low <= RB.low[who] && high <= RB.high[who] && ms <= left) return;
    low = fmaxf(low, RB.low[who]), high = fmaxf(high, RB.high[who]), ms = ms > (int)left ? ms : (int)left;
  }
  low = low > 1 ? 1 : low, high = high > 1 ? 1 : high;
  RB.low[who] = low, RB.high[who] = high, RB.until[who] = RB.now + ms / 1000.0;
  fx_rumble_sent[who]++;
  cw_input_rumble(who, low, high, ms);
}
void fx_rumble_stop(void) {
  for (int i = 0; i < 2; i++) if (RB.until[i] > RB.now) cw_input_rumble_stop(i), RB.until[i] = 0;
}

static struct {
  bool ready;
  cw_node root;
  cw_node sparks[4], flash_p[4], ring[4], dust[4], bsparks[3], bring[3], chips[3], streaks[2], lsparks[3], embers[2], dustk[2];
  int si, fi, ri, di, bi, bri, ci, sti, lsi, emi, dki;
  cw_node light[3];
  int light_age[3], next_light;
  float light_e[3];
  cw_node proj[8];
  cw_node hboom[3], sboom;
  int hbi;
  cw_node ember[8];
  /* a super's beam: from where it left the hands to its head, a bright core in a wider glow (additive), in the
   * fighter's colour; it fades over a quarter second once the shot is gone */
  t3_mesh *beam_core[8], *beam_glow[8];
  float beam_x0[8], beam_x1[8], beam_y[8], beam_r[8], beam_a[8], beam_c[8][3];
  bool beam_was[8];
  float flash, flash_want; /* the super / knockout dimming: the level shown, and what the moment asks for */
  uint32_t seen;
} FX;

static cw_node pnode(const char *name, const char *const *kv, int seed) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_add_child(FX.root, n);
  for (int i = 0; kv[i]; i += 2) {
    cw_value v;
    const cw_prop *p = cw_node_find_prop(n, kv[i], NULL);
    if (p && p->type == CW_T_ENUM) { cw_node_set(n, kv[i], cw_val_enum(kv[i + 1])); continue; }
    if (!p || !cw_value_parse(kv[i + 1], p->type, &v)) { cw_warn("fighter: bad particle setting %s = %s", kv[i], kv[i + 1]); continue; }
    cw_node_set(n, kv[i], v);
  }
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "seed", cw_val_int(seed));
  return n;
}

static const char *const SPARKS[] = { "amount", "48", "lifetime", "0.45", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true", "emission_shape", "SPHERE",
  "emission_radius", "0.08", "spread", "180", "direction", "vec3(1, 0.3, 0)", "speed_min", "3", "speed_max", "10", "gravity", "vec3(0, -9, 0)", "damping", "0.8",
  "draw_mode", "STRETCHED", "stretch", "0.08", "size", "0.028", "size_random", "0.4", "size_curve", "\"0 1, 0.6 0.8, 1 0.25\"", "color_ramp", "\"0 1 0.95 0.7 1, 0.3 1 0.55 0.12 1, 1 0.6 0.1 0.03 0\"", "energy", "7",
  "blend", "ADD", NULL };
/* one soft flash at the contact, short and dim enough that its glow keeps its falloff (bright additive dots clip into flat discs) */
static const char *const FLASHP[] = { "amount", "1", "lifetime", "0.09", "explosiveness", "1", "one_shot", "true", "emission_shape", "POINT", "speed_min", "0", "speed_max", "0",
  "size", "0.9", "size_curve", "\"0 0.5, 0.35 1, 1 0.6\"", "color_ramp", "\"0 1 0.95 0.85 0.9, 0.5 1 0.7 0.35 0.5, 1 1 0.45 0.15 0\"", "energy", "2.2", "blend", "ADD", NULL };
/* impact lines: short fast streaks thrown out round the contact (they were round dots, which read as a ring of discs) */
static const char *const RING[] = { "amount", "14", "lifetime", "0.16", "lifetime_random", "0.3", "explosiveness", "1", "one_shot", "true", "emission_shape", "RING", "emission_radius", "0.12",
  "direction", "vec3(1, 0, 0)", "spread", "180", "speed_min", "9", "speed_max", "13", "damping", "7", "draw_mode", "STRETCHED", "stretch", "0.06", "size", "0.035",
  "color_ramp", "\"0 1 0.95 0.8 1, 0.5 1 0.6 0.25 0.8, 1 0.9 0.3 0.1 0\"", "energy", "4", "blend", "ADD", NULL };
/* dust kicked up along the floor: a few puffs of the engine's smoke flipbook (motion-blended frames), each turned and sized
 * at random, rolling out low and fading against the floor (soft), never a cloud of round blobs */
static const char *const DUST[] = { "amount", "8", "lifetime", "0.9", "lifetime_random", "0.35", "explosiveness", "0.9", "one_shot", "true", "emission_shape", "BOX",
  "emission_extents", "vec3(0.35, 0.02, 0.15)", "direction", "vec3(0, 0.2, 0)", "spread", "85", "speed_min", "0.5", "speed_max", "1.7", "gravity", "vec3(0, 0.15, 0)", "damping", "3",
  "size", "0.75", "size_random", "0.45", "size_curve", "\"0 0.45, 0.4 1, 1 1.35\"", "color_ramp", "\"0 0.62 0.57 0.52 0, 0.12 0.6 0.56 0.52 0.42, 1 0.55 0.52 0.5 0\"",
  "blend", "ALPHA", "sort", "true", "soft_distance", "0.35", "spin", "0.7", "texture", "\"fx/smoke.png\"", "motion_texture", "\"fx/smoke_mv.png\"", "hframes", "8", "vframes", "8",
  "turbulence", "0.5", "turbulence_scale", "0.5", NULL };
static const char *const BSPARKS[] = { "amount", "24", "lifetime", "0.35", "explosiveness", "1", "one_shot", "true", "emission_shape", "SPHERE", "emission_radius", "0.06",
  "spread", "180", "direction", "vec3(1, 0.2, 0)", "speed_min", "2", "speed_max", "6", "gravity", "vec3(0, -6, 0)", "draw_mode", "STRETCHED", "stretch", "0.07", "size", "0.025", "size_curve", "\"0 1, 1 0.3\"",
  "color_ramp", "\"0 0.9 0.97 1 1, 0.4 0.5 0.75 1 1, 1 0.3 0.4 0.9 0\"", "energy", "6", "blend", "ADD", NULL };
static const char *const BRING[] = { "amount", "10", "lifetime", "0.14", "lifetime_random", "0.3", "explosiveness", "1", "one_shot", "true", "emission_shape", "RING", "emission_radius", "0.08",
  "direction", "vec3(1, 0, 0)", "spread", "180", "speed_min", "7", "speed_max", "9", "damping", "7", "draw_mode", "STRETCHED", "stretch", "0.05", "size", "0.03",
  "color_ramp", "\"0 0.8 0.95 1 1, 1 0.3 0.5 1 0\"", "energy", "3.5", "blend", "ADD", NULL };
/* grit knocked off the floor: a few small dark chips that hop low and land (depth collision). Particles are unlit, so their
 * colour is what they show: the lit floor's dark grey-brown. At 0.4 grey they read as pale confetti cubes */
static const char *const CHIPS[] = { "amount", "10", "lifetime", "1.1", "explosiveness", "1", "one_shot", "true", "emission_shape", "BOX", "emission_extents", "vec3(0.35, 0.02, 0.1)",
  "direction", "vec3(0, 1, 0)", "spread", "55", "speed_min", "1.2", "speed_max", "3", "gravity", "vec3(0, -12, 0)", "draw_mode", "MESH", "mesh_primitive", "BOX", "size", "0.028",
  "size_random", "0.6", "size_curve", "\"0 1, 0.8 1, 1 0\"", "spin", "8", "blend", "OPAQUE", "color_ramp", "\"0 0.075 0.062 0.05 1, 1 0.06 0.05 0.042 1\"", "collide_depth", "true",
  "bounce", "0.25", "friction", "0.6", "collision_radius", "0.015", NULL };
/* a knockdown's dust: the floor dust spread wide and low, rolling out both ways from where the body lands */
static const char *const DUSTK[] = { "amount", "14", "lifetime", "1.1", "lifetime_random", "0.35", "explosiveness", "0.95", "one_shot", "true", "emission_shape", "BOX",
  "emission_extents", "vec3(0.55, 0.02, 0.22)", "direction", "vec3(0, 0.12, 0)", "spread", "88", "speed_min", "0.9", "speed_max", "2.6", "gravity", "vec3(0, 0.1, 0)", "damping", "2.6",
  "size", "0.85", "size_random", "0.45", "size_curve", "\"0 0.4, 0.35 1, 1 1.5\"", "color_ramp", "\"0 0.62 0.57 0.52 0, 0.1 0.6 0.56 0.52 0.38, 1 0.55 0.52 0.5 0\"",
  "blend", "ALPHA", "sort", "true", "soft_distance", "0.35", "spin", "0.6", "texture", "\"fx/smoke.png\"", "motion_texture", "\"fx/smoke_mv.png\"", "hframes", "8", "vframes", "8",
  "turbulence", "0.5", "turbulence_scale", "0.5", NULL };
/* a light hit: a small quick spray of pale sparks, short and cool, so a jab never looks like a knockout */
static const char *const LSPARKS[] = { "amount", "14", "lifetime", "0.22", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true", "emission_shape", "SPHERE",
  "emission_radius", "0.05", "spread", "180", "direction", "vec3(1, 0.3, 0)", "speed_min", "2", "speed_max", "6", "gravity", "vec3(0, -6, 0)", "damping", "1.5",
  "draw_mode", "STRETCHED", "stretch", "0.05", "size", "0.018", "size_random", "0.4", "size_curve", "\"0 1, 1 0.3\"", "color_ramp", "\"0 1 0.97 0.88 1, 0.5 1 0.85 0.55 0.8, 1 0.8 0.5 0.3 0\"",
  "energy", "4", "blend", "ADD", NULL };
/* a heavy hit's embers: a few slow hot specks that drift down and die before the floor */
static const char *const HEMBERS[] = { "amount", "12", "lifetime", "0.7", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true", "emission_shape", "SPHERE",
  "emission_radius", "0.1", "spread", "180", "direction", "vec3(0, 1, 0)", "speed_min", "0.6", "speed_max", "2.2", "gravity", "vec3(0, -2.5, 0)", "damping", "1.2",
  "size", "0.035", "size_random", "0.5", "size_curve", "\"0 1, 0.7 0.7, 1 0\"", "color_ramp", "\"0 1 0.75 0.35 1, 0.5 1 0.4 0.08 0.8, 1 0.5 0.12 0.02 0\"", "energy", "3", "blend", "ADD", NULL };
/* super and knockout streaks: thin lines that shrink to nothing as they slow (slow fat streaks read as floating white ovals) */
static const char *const STREAKS[] = { "amount", "48", "lifetime", "0.32", "lifetime_random", "0.3", "explosiveness", "1", "one_shot", "true", "emission_shape", "RING", "emission_radius", "0.6",
  "direction", "vec3(1, 0, 0)", "spread", "180", "speed_min", "16", "speed_max", "24", "damping", "1.5", "draw_mode", "STRETCHED", "stretch", "0.09", "size", "0.045", "size_curve", "\"0 1, 0.5 0.6, 1 0\"",
  "color_ramp", "\"0 1 1 1 1, 0.4 0.7 0.9 1 0.8, 1 0.3 0.4 1 0\"", "energy", "8", "blend", "ADD", NULL };
static const char *const CORE[] = { "amount", "14", "lifetime", "0.18", "emission_shape", "POINT", "speed_min", "0", "speed_max", "0.5", "size", "0.7", "size_curve", "\"0 1, 1 0.3\"",
  "color_ramp", "\"0 1 0.95 0.8 1, 0.5 1 0.5 0.15 0.9, 1 1 0.3 0.05 0\"", "energy", "2.2", "blend", "ADD", "local_coords", "false", NULL };
static const char *const EMBER[] = { "amount", "40", "lifetime", "0.4", "lifetime_random", "0.4", "emission_shape", "SPHERE", "emission_radius", "0.1", "spread", "180",
  "speed_min", "0.2", "speed_max", "1.2", "gravity", "vec3(0, 0.6, 0)", "size", "0.08", "size_curve", "\"0 1, 1 0\"",
  "color_ramp", "\"0 1 0.8 0.3 1, 0.5 1 0.35 0.05 0.7, 1 0.5 0.1 0.02 0\"", "energy", "4", "blend", "ADD", "local_coords", "false", NULL };

static void beams_init(void);
void fx_init(cw_node root) {
  memset(&FX, 0, sizeof FX);
  FX.root = cw_node_new("Node3D", "Fx");
  cw_node_add_child(root, FX.root);
  char nm[24];
  for (int i = 0; i < 4; i++) {
    snprintf(nm, sizeof nm, "Spark%d", i); FX.sparks[i] = pnode(nm, SPARKS, 100 + i);
    snprintf(nm, sizeof nm, "Flash%dP", i); FX.flash_p[i] = pnode(nm, FLASHP, 110 + i);
    snprintf(nm, sizeof nm, "Ring%d", i); FX.ring[i] = pnode(nm, RING, 120 + i);
    snprintf(nm, sizeof nm, "Dust%d", i); FX.dust[i] = pnode(nm, DUST, 130 + i);
  }
  for (int i = 0; i < 3; i++) {
    snprintf(nm, sizeof nm, "BSpark%d", i); FX.bsparks[i] = pnode(nm, BSPARKS, 140 + i);
    snprintf(nm, sizeof nm, "BRing%d", i); FX.bring[i] = pnode(nm, BRING, 150 + i);
    snprintf(nm, sizeof nm, "Chips%d", i); FX.chips[i] = pnode(nm, CHIPS, 160 + i);
  }
  for (int i = 0; i < 2; i++) { snprintf(nm, sizeof nm, "Streak%d", i); FX.streaks[i] = pnode(nm, STREAKS, 170 + i); }
  for (int i = 0; i < 3; i++) { snprintf(nm, sizeof nm, "LSpark%d", i); FX.lsparks[i] = pnode(nm, LSPARKS, 210 + i); }
  for (int i = 0; i < 2; i++) {
    snprintf(nm, sizeof nm, "HEmber%d", i); FX.embers[i] = pnode(nm, HEMBERS, 220 + i);
    snprintf(nm, sizeof nm, "DustK%d", i); FX.dustk[i] = pnode(nm, DUSTK, 230 + i);
  }
  for (int i = 0; i < 8; i++) {
    snprintf(nm, sizeof nm, "Proj%d", i); FX.proj[i] = pnode(nm, CORE, 180 + i);
    snprintf(nm, sizeof nm, "Ember%d", i); FX.ember[i] = pnode(nm, EMBER, 190 + i);
  }
  /* the explosion kit: a heavy hit is a small Explosion3D (a hot burst, a fast shockwave, a flash light); a super or a
   * knockout a big one standing on the floor (a burst at body height, a ring in the air and one along the ground, a
   * strong flash). No scorch mark: on these tiled floors a super and then a knockout in one spot left two olive blots
   * that read as overlapping puddles */
  for (int i = 0; i < 3; i++) {
    snprintf(nm, sizeof nm, "HeavyBoom%d", i);
    cw_node b = FX.hboom[i] = cw_node_new("Explosion3D", nm);
    static const char *const KV[] = { "autostart", "false", "duration", "0.6", "flash_energy", "35", "flash_range", "6", "flash_duration", "0.14", "scorch_size", "0",
      "shake_amount", "0", NULL };
    for (int k = 0; KV[k]; k += 2) { const cw_prop *pp = cw_node_find_prop(b, KV[k], NULL); cw_value v; if (pp && cw_value_parse(KV[k + 1], pp->type, &v)) cw_node_set(b, KV[k], v); }
    cw_node_add_child(FX.root, b);
    cw_node w = cw_node_new("Shockwave3D", "Wave");
    cw_node_set(w, "autostart", cw_val_bool(false)); cw_node_set(w, "max_radius", cw_val_float(1.3)); cw_node_set(w, "duration", cw_val_float(0.32));
    cw_node_set(w, "thickness", cw_val_float(0.3)); cw_node_set(w, "strength", cw_val_float(0.09));
    cw_node_add_child(b, w);
    cw_node burst = cw_node_new("GPUParticles3D", "Burst");
    cw_node_add_child(b, burst);
    static const char *const BURST[] = { "amount", "16", "lifetime", "0.2", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true", "emission_shape", "SPHERE",
      "emission_radius", "0.05", "spread", "180", "speed_min", "6", "speed_max", "12", "damping", "4", "draw_mode", "STRETCHED", "stretch", "0.05", "size", "0.04",
      "color_ramp", "\"0 1 0.95 0.8 1, 0.5 1 0.6 0.2 0.9, 1 0.9 0.3 0.05 0\"", "energy", "6", "blend", "ADD", NULL };
    for (int k = 0; BURST[k]; k += 2) { const cw_prop *pp = cw_node_find_prop(burst, BURST[k], NULL); cw_value v; if (pp && pp->type == CW_T_ENUM) { cw_node_set(burst, BURST[k], cw_val_enum(BURST[k + 1])); continue; } if (pp && cw_value_parse(BURST[k + 1], pp->type, &v)) cw_node_set(burst, BURST[k], v); }
    cw_node_set(burst, "emitting", cw_val_bool(false)); cw_node_set(burst, "seed", cw_val_int(200 + i));
  }
  {
    cw_node b = FX.sboom = cw_node_new("Explosion3D", "SuperBoom");
    static const char *const KV[] = { "autostart", "false", "duration", "1.2", "flash_energy", "140", "flash_range", "12", "flash_duration", "0.35", "flash_color", "color(0.8, 0.85, 1, 1)",
      "scorch_size", "0", "shake_amount", "0", NULL };
    for (int k = 0; KV[k]; k += 2) { const cw_prop *pp = cw_node_find_prop(b, KV[k], NULL); cw_value v; if (pp && cw_value_parse(KV[k + 1], pp->type, &v)) cw_node_set(b, KV[k], v); else cw_warn("fighter: Explosion3D %s", KV[k]); }
    cw_node_add_child(FX.root, b);
    cw_node w = cw_node_new("Shockwave3D", "AirWave");
    cw_node_set(w, "autostart", cw_val_bool(false)); cw_node_set(w, "max_radius", cw_val_float(5.0)); cw_node_set(w, "duration", cw_val_float(0.7));
    cw_node_set(w, "thickness", cw_val_float(1.0)); cw_node_set(w, "strength", cw_val_float(0.14)); /* bent air, not colour fringes */
    cw_node3d_set_position(w, (t3_vec3){ 0, 1.05f, 0.2f });
    cw_node_add_child(b, w);
    cw_node gw = cw_node_new("Shockwave3D", "GroundWave");
    cw_node_set(gw, "autostart", cw_val_bool(false)); cw_node_set(gw, "ground", cw_val_bool(true)); cw_node_set(gw, "max_radius", cw_val_float(6.0));
    cw_node_set(gw, "duration", cw_val_float(0.75)); cw_node_set(gw, "thickness", cw_val_float(0.8)); cw_node_set(gw, "strength", cw_val_float(0.12));
    cw_node3d_set_position(gw, (t3_vec3){ 0, 0.03f, 0 });
    cw_node_add_child(b, gw);
  }
  beams_init();
  FX.ready = true;
}

static t3_mesh *beam_mesh(float opacity) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->transparent = true; m->depth_write = false; m->opacity = opacity; m->blending = T3_ADDITIVE_BLENDING;
  /* a cylinder along x (made along y and turned), unit length, unit radius: scaled to the beam every frame */
  t3_mesh *b = t3_mesh_new(t3_cylinder_geometry_new(1, 1, 1, 16, 1, false), m);
  t3_release(m);
  b->base.visible = false;
  b->base.render_order = 40;
  return b;
}
static void beams_init(void) {
  t3_object *root = t3_group_new();
  for (int i = 0; i < 8; i++) {
    FX.beam_glow[i] = beam_mesh(0.16f); t3_object_add(root, FX.beam_glow[i]);
    FX.beam_core[i] = beam_mesh(0.85f); t3_object_add(root, FX.beam_core[i]);
  }
  cw_node n = cw_node_new("Node3D", "Beams");
  cw_node_add_child(FX.root, n);
  cw_node3d_set_object(n, root);
  t3_release(root);
}
static void beam_place(t3_mesh *m, float x0, float x1, float y, float r, float a, const float c[3], float k) {
  float len = fabsf(x1 - x0);
  m->base.visible = a > 0.01f && len > 0.05f;
  if (!m->base.visible) return;
  m->base.position = (t3_vec3){ (x0 + x1) * 0.5f, y, 0.1f };
  m->base.quaternion = (t3_quat){ 0, 0, 0.70710678f, 0.70710678f }; /* y along x */
  m->base.scale = (t3_vec3){ r, len, r };
  m->materials[0]->color = (t3_color){ c[0] * k * a, c[1] * k * a, c[2] * k * a };
}

/* One flash budget shared by every blow's flash (its flash sprite, its light, a heavy one's explosion flash) and the
 * super's: a blow flashes only if nothing flashed in the last 30 ticks (at most 2 flashes a second however fast a combo
 * lands them, inside the three a second of WCAG 2.3.1 and the Harding test); within that it sparks and rings, a heavy one
 * bursts, without the flash. A super or a knockout always flashes, and starts the wait again. fx_flash_cap 0 (test switch sw flash_cap 0): every one flashes,
 * as before. fx_boom_every N (sw boom_every N): a heavy hit's effects every N ticks, the flash check's fast combo */
int fx_flash_cap = 1, fx_boom_every, fx_unlit_sparks = 1;
int32_t fx_flashes; /* debug fx.flashes: explosion flashes lit */
static uint32_t flash_tick;
static bool flash_seen;
/* may a blow flash now? (and if so the wait starts again) */
static bool flash_ok(uint32_t tick) {
  if (fx_flash_cap && flash_seen && tick - flash_tick < 30) return false;
  flash_tick = tick; flash_seen = true; fx_flashes++;
  return true;
}
static void heavy_boom(t3_vec3 p, bool lit) {
  cw_node b = FX.hboom[FX.hbi++ % 3];
  cw_node_set(b, "flash_energy", cw_val_float(lit ? 35 : 0));
  cw_node3d_set_position(b, p);
  cw_explosion_fire(b);
}
static void super_boom(float x, uint32_t tick) {
  flash_tick = tick; flash_seen = true; fx_flashes++;
  cw_node3d_set_position(FX.sboom, (t3_vec3){ x, 0, 0 });
  cw_explosion_fire(FX.sboom);
}

void fx_bind_lights(cw_node stage) {
  for (int i = 0; i < 3; i++) {
    char nm[16];
    snprintf(nm, sizeof nm, "Flash%d", i);
    FX.light[i] = cw_node_get(stage, nm);
  }
}

void fx_reset(void) { fx_rumble_stop(); FX.flash = FX.flash_want = 0; FX.seen = 0; for (int i = 0; i < 3; i++) FX.light_age[i] = 0; }
float fx_flash(void) { return FX.flash; }

static void at(cw_node n, t3_vec3 p) { cw_node3d_set_position(n, p); cw_particles_restart(n); }
static void tint(cw_node n, float r, float g, float b) { cw_node_set(n, "color", cw_val_color(r, g, b, 1)); }
static void light(t3_vec3 p, float e, float cr, float cg, float cb) {
  int i = FX.next_light++ % 3;
  if (!cw_node_valid(FX.light[i])) return;
  cw_node3d_set_position(FX.light[i], p);
  cw_node_set(FX.light[i], "color", cw_val_color(cr, cg, cb, 1));
  FX.light_age[i] = 1;
  FX.light_e[i] = e;
}

static const float PROJ_COL[4][3] = { { 1, 0.45f, 0.1f }, { 0.9f, 0.7f, 0.3f }, { 0.2f, 0.9f, 1 }, { 0.5f, 1, 0.3f } };
static int beam_free(void) { /* a beam slot whose shot is not flying (the oldest fading one) */
  int best = 7; float lo = 2;
  for (int i = 0; i < 8; i++) if (!FX.beam_was[i] && FX.beam_a[i] < lo) { lo = FX.beam_a[i]; best = i; }
  return best;
}
void fx_event(const ft_game *g, const ft_event *e) {
  if (e->type == EV_HIT || e->type == EV_BLOCK || e->type == EV_THROW || e->type == EV_WALL || e->type == EV_SUPER || e->type == EV_KO) fx_count_impact++;
  t3_vec3 p = { (float)e->x / 1000.0f, (float)e->y / 1000.0f, 0.15f };
  float k = (float)e->power;
  int who = e->who, other = 1 - e->who;
  switch (e->type) {
  case EV_HIT: {
    fx_count_hit++;
    { /* a beam super that hits point blank (the shot lands the tick it leaves the hands): its beam still flashes, hands to blow */
      const ft_fighter *at = &g->f[other];
      const ft_move *sm = &g->roster->d[at->def].mv[M_SUPER];
      if (at->state == S_ATTACK && at->mv == M_SUPER && (sm->flags & FM_PROJ) && sm->psize > 0) {
        int b = beam_free();
        FX.beam_x0[b] = (float)at->x / 1000.0f + (float)at->facing * 0.35f; FX.beam_x1[b] = p.x;
        FX.beam_y[b] = p.y; FX.beam_r[b] = (float)sm->psize / 1000.0f; FX.beam_a[b] = 1;
        const float *c = PROJ_COL[at->def % 4];
        FX.beam_c[b][0] = c[0], FX.beam_c[b][1] = c[1], FX.beam_c[b][2] = c[2];
      }
    }
    /* power: 0 light, 1 medium, 2 strong (by damage), 3 and up a knockdown or a launcher */
    bool big = e->power >= 3, light_hit = e->power == 0;
    /* the flash of a blow (its flash sprite, its big light, a heavy one's explosion flash) is in the shared budget: in a
     * fast combo only one blow in 30 ticks flashes, the rest spark without it */
    bool lit = flash_ok(g->tick); /* a light blow's small light counts too */
    int r = FX.ri++ % 4;
    if (light_hit || !lit) { /* no flash sprite: a small spray of sparks */
      cw_node ls = FX.lsparks[FX.lsi++ % 3];
      cw_node_set(ls, "amount", cw_val_int(lit ? 14 : 8));
      if (lit || fx_unlit_sparks) at(ls, p);
      cw_node_set(FX.ring[r], "amount", cw_val_int(light_hit ? 6 : 10));
    } else {
      int i = FX.si++ % 4;
      cw_node_set(FX.sparks[i], "amount", cw_val_int(big ? 48 : e->power >= 2 ? 34 : 24));
      at(FX.sparks[i], p);
      at(FX.flash_p[FX.fi++ % 4], p);
      cw_node_set(FX.ring[r], "amount", cw_val_int(big ? 18 : 10));
    }
    /* outside the budget a blow is only its small spray: the ring, the embers and a heavy one's explosion (its burst and
     * shockwave) are bright enough over a fighter to read as a flash of their own when blows land seven times a second */
    if (lit) {
      at(FX.ring[r], p);
      if (big) at(FX.embers[FX.emi++ % 2], p);
      if (big) heavy_boom(p, true);
    }
    /* outside the budget: no light; a heavy blow's light is its explosion's flash (two lights peaking a few frames apart
     * read as two flashes) */
    if (lit && !big) light(p, light_hit ? 26 : 50, 1, light_hit ? 0.9f : 0.75f, light_hit ? 0.7f : 0.4f);
    /* the one hit: short and sharp on the light motor, a heavy hit (a launcher, a knockdown) with some weight under it */
    rumble(who, big ? 0.35f : 0.05f + 0.03f * k, big ? 0.85f : 0.35f + 0.1f * k, big ? 140 : 70 + 15 * (int)k);
    rumble(other, big ? 0.15f : 0, big ? 0.5f : 0.2f + 0.06f * k, big ? 80 : 50);
    break;
  }
  case EV_BLOCK: {
    fx_count_block++;
    at(FX.bsparks[FX.bi++ % 3], p);
    at(FX.bring[FX.bri++ % 3], p);
    light(p, 22, 0.5f, 0.7f, 1);
    rumble(who, 0, 0.18f, 40); /* a guarded hit: a tick */
    break;
  }
  case EV_THROW: at(FX.dustk[FX.dki++ % 2], (t3_vec3){ p.x, 0.05f, 0.1f }); at(FX.chips[FX.ci++ % 3], (t3_vec3){ p.x, 0.02f, 0.1f }); rumble(who, 0.5f, 0.3f, 120); rumble(other, 0.3f, 0.1f, 80); light(p, 40, 1, 0.8f, 0.5f); break;
  case EV_DUST: {
    /* a dash or a hop is a puff; a body landing from a knockdown (it lies down this tick) the wide cloud and grit */
    bool down = who >= 0 && who < 2 && g->f[who].state == S_KD;
    if (down) { at(FX.dustk[FX.dki++ % 2], (t3_vec3){ p.x, 0.04f, 0.1f }); at(FX.chips[FX.ci++ % 3], (t3_vec3){ p.x, 0.02f, 0.1f }); }
    else at(FX.dust[FX.di++ % 4], (t3_vec3){ p.x, 0.04f, 0.1f });
    if (e->power >= 2) rumble(who, 0.3f, 0, 90); /* a knockdown hits the floor: a light thud */
    break;
  }
  case EV_WALL: rumble(who, 0.45f, 0.2f, 110); break; /* the body hits the floor (the dust and grit came with its EV_DUST) */
  case EV_FIRE: light((t3_vec3){ p.x, 1.05f, 0.5f }, 30, 1, 0.6f, 0.3f); at(FX.flash_p[FX.fi++ % 4], (t3_vec3){ p.x, 1.05f, 0.2f }); break;
  case EV_SUPER: {
    FX.flash_want = 1;
    rumble(who, 0.3f, 0.3f, 120); /* the super starts */
    at(FX.streaks[FX.sti++ % 2], (t3_vec3){ p.x, 1.0f, 0.2f });
    int r = FX.ri++ % 4;
    cw_node_set(FX.ring[r], "amount", cw_val_int(24));
    at(FX.ring[r], (t3_vec3){ p.x, 1.0f, 0.2f });
    light((t3_vec3){ p.x, 1.2f, 1.0f }, 120, 0.7f, 0.8f, 1);
    super_boom(p.x, g->tick);
    break;
  }
  case EV_CLASH: at(FX.flash_p[FX.fi++ % 4], p); at(FX.sparks[FX.si++ % 4], p); light(p, 40, 1, 0.8f, 0.5f); break;
  case EV_TECH: at(FX.bring[FX.bri++ % 3], p); at(FX.bsparks[FX.bi++ % 3], p); light(p, 30, 0.7f, 0.9f, 1); break;
  case EV_KO: {
    FX.flash_want = fmaxf(FX.flash_want, 0.8f);
    rumble(who, 0.9f, 0.6f, 300); /* knocked out: the biggest one */
    rumble(other, 0.5f, 0.3f, 180);
    for (int i = 0; i < 3; i++) at(FX.sparks[FX.si++ % 4], (t3_vec3){ p.x, 1.1f, 0.2f });
    at(FX.streaks[FX.sti++ % 2], (t3_vec3){ p.x, 1.1f, 0.2f });
    light((t3_vec3){ p.x, 1.2f, 1.0f }, 140, 1, 0.9f, 0.7f);
    super_boom(p.x, g->tick);
    break;
  }
  default: break;
  }
  (void)g;
}


void fx_update(float dt) { (void)dt; }
void fx_frame(const ft_game *g, float dt) {
  if (!FX.ready) return;
  RB.g = g, RB.now += dt;
  /* events since last call */
  uint32_t n = g->ev_seq;
  if (n - FX.seen > 64) FX.seen = n - 64;
  while (FX.seen < n) { fx_event(g, &g->ev[FX.seen & 63]); FX.seen++; }
  { /* the test switch's fast combo: a heavy blow's effects on player 2 every fx_boom_every ticks */
    static uint32_t last;
    if (fx_boom_every > 0 && g->tick != last && g->tick % (uint32_t)fx_boom_every == 0) {
      last = g->tick;
      ft_event e = { EV_HIT, 1, 3, 0, g->f[1].x, 1100, g->tick };
      fx_event(g, &e);
    }
  }
  /* projectiles carry their glow */
  for (int i = 0; i < 8; i++) {
    const ft_proj *P = &g->proj[i];
    bool live = P->live, big = live && (g->roster->d[g->f[P->owner].def].mv[P->mv].flags & FM_SUPER);
    cw_node_set(FX.proj[i], "emitting", cw_val_bool(live));
    cw_node_set(FX.ember[i], "emitting", cw_val_bool(live));
    if (live) {
      t3_vec3 pos = { (float)P->x / 1000.0f, (float)P->y / 1000.0f, 0.1f };
      cw_node3d_set_position(FX.proj[i], pos);
      cw_node3d_set_position(FX.ember[i], pos);
      const float *c = PROJ_COL[g->f[P->owner].def % 4];
      tint(FX.proj[i], c[0], c[1], c[2]);
      tint(FX.ember[i], c[0], c[1], c[2]);
      cw_node_set(FX.proj[i], "size", cw_val_float(big ? 1.8 : 0.45 + (double)P->size / 900.0));
    }
    /* the super's beam runs from where the shot left the hands to where its head is now */
    if (live && big) {
      /* from the owner's hands (a shot fired point blank still draws a beam to its head) */
      if (!FX.beam_was[i]) FX.beam_x0[i] = (float)g->f[P->owner].x / 1000.0f + (float)(P->vx > 0 ? 1 : -1) * 0.35f;
      FX.beam_x1[i] = (float)P->x / 1000.0f; FX.beam_y[i] = (float)P->y / 1000.0f; FX.beam_r[i] = (float)P->size / 1000.0f;
      FX.beam_a[i] = 1;
      const float *c = PROJ_COL[g->f[P->owner].def % 4];
      FX.beam_c[i][0] = c[0], FX.beam_c[i][1] = c[1], FX.beam_c[i][2] = c[2];
    } else FX.beam_a[i] = fmaxf(0, FX.beam_a[i] - dt * 4.0f);
    FX.beam_was[i] = live && big;
    /* the core thins as it fades; the glow stays wide */
    beam_place(FX.beam_core[i], FX.beam_x0[i], FX.beam_x1[i], FX.beam_y[i], FX.beam_r[i] * 0.32f * (0.4f + 0.6f * FX.beam_a[i]), FX.beam_a[i], FX.beam_c[i], 1.6f);
    beam_place(FX.beam_glow[i], FX.beam_x0[i], FX.beam_x1[i], FX.beam_y[i], FX.beam_r[i] * 0.85f, FX.beam_a[i], FX.beam_c[i], 1.0f);
  }
  /* lights decay */
  for (int i = 0; i < 3; i++) {
    if (!cw_node_valid(FX.light[i])) continue;
    int a = FX.light_age[i];
    float e = 0;
    if (a > 0) {
      /* a swell, never a one-frame jump: up over 3 frames, then down over about 15 (no strobe: tools/flash-check.mjs) */
      e = a <= 3 ? FX.light_e[i] * (float)a / 3.0f : FX.light_e[i] * cw_exp(-(float)(a - 3) * 0.16f);
      FX.light_age[i] = a < 40 ? a + 1 : 0;
    }
    cw_node_set(FX.light[i], "energy", cw_val_float(e));
  }
  /* the moment dims the world: it swells in over about a tenth of a second and lifts over most of a second, never a
   * white wash or a jump */
  FX.flash += (FX.flash_want - FX.flash) * (1.0f - cw_exp(-dt * (FX.flash_want > FX.flash ? 18.0f : 4.0f)));
  FX.flash_want *= cw_exp(-dt * 3.0f);
}
