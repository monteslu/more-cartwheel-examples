/* fighter effects. All of them read the simulation's events; nothing here
 * feeds the rules. GPU particles simulate on the tree's fixed step from hashed
 * seeds, so every host draws the same effects.
 *
 *   hit      a flash light, one short soft flash, a burst of hot streaking sparks and impact lines (more with power),
 *            camera shake. Round additive dots are kept small and dim: bright ones clip into flat discs.
 *   block    cool white-blue sparks and short impact lines, a lighter shake
 *   knockdown, landing and dashes  low smoke-flipbook dust along the floor (soft against it) and chips
 *   fireball a glowing core with an ember trail, a muzzle flash; each fighter
 *            has its own colour
 *   super    a white screen flash, a burst of impact lines and streaks, a shockwave that bends the picture
 *   KO       a big flash and shake, a shockwave in the air and one along the floor
 *   heavy hits fire a small Explosion3D at the contact (a hot burst, a fast shockwave that bends the picture, a flash);
 *            supers and knockouts a big one on the floor (rings in the air and along the ground, a flash) */
#include <stdio.h>
#include <string.h>

#include "cartwheel/explosions.h"
#include "game.h"

int fx_count_hit, fx_count_block;

static struct {
  bool ready;
  cw_node root;
  cw_node sparks[4], flash_p[4], ring[4], dust[4], bsparks[3], bring[3], chips[3], streaks[2];
  int si, fi, ri, di, bi, bri, ci, sti;
  cw_node light[3];
  int light_age[3], next_light;
  float light_e[3];
  cw_node proj[8];
  cw_node hboom[3], sboom;
  int hbi;
  cw_node ember[8];
  float shake, flash;
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
static const char *const CHIPS[] = { "amount", "22", "lifetime", "0.8", "explosiveness", "1", "one_shot", "true", "emission_shape", "BOX", "emission_extents", "vec3(0.4, 0.02, 0.1)",
  "direction", "vec3(0, 1, 0)", "spread", "50", "speed_min", "2", "speed_max", "5", "gravity", "vec3(0, -12, 0)", "draw_mode", "MESH", "mesh_primitive", "BOX", "size", "0.05",
  "size_random", "0.6", "spin", "10", "blend", "OPAQUE", "color_ramp", "\"0 0.4 0.38 0.35 1, 1 0.35 0.33 0.3 1\"", "bounce", "0.3", "collision_radius", "0.03", NULL };
/* super and knockout streaks: thin lines that shrink to nothing as they slow (slow fat streaks read as floating white ovals) */
static const char *const STREAKS[] = { "amount", "48", "lifetime", "0.32", "lifetime_random", "0.3", "explosiveness", "1", "one_shot", "true", "emission_shape", "RING", "emission_radius", "0.6",
  "direction", "vec3(1, 0, 0)", "spread", "180", "speed_min", "16", "speed_max", "24", "damping", "1.5", "draw_mode", "STRETCHED", "stretch", "0.09", "size", "0.045", "size_curve", "\"0 1, 0.5 0.6, 1 0\"",
  "color_ramp", "\"0 1 1 1 1, 0.4 0.7 0.9 1 0.8, 1 0.3 0.4 1 0\"", "energy", "8", "blend", "ADD", NULL };
static const char *const CORE[] = { "amount", "14", "lifetime", "0.18", "emission_shape", "POINT", "speed_min", "0", "speed_max", "0.5", "size", "0.7", "size_curve", "\"0 1, 1 0.3\"",
  "color_ramp", "\"0 1 0.95 0.8 1, 0.5 1 0.5 0.15 0.9, 1 1 0.3 0.05 0\"", "energy", "2.2", "blend", "ADD", "local_coords", "false", NULL };
static const char *const EMBER[] = { "amount", "40", "lifetime", "0.4", "lifetime_random", "0.4", "emission_shape", "SPHERE", "emission_radius", "0.1", "spread", "180",
  "speed_min", "0.2", "speed_max", "1.2", "gravity", "vec3(0, 0.6, 0)", "size", "0.08", "size_curve", "\"0 1, 1 0\"",
  "color_ramp", "\"0 1 0.8 0.3 1, 0.5 1 0.35 0.05 0.7, 1 0.5 0.1 0.02 0\"", "energy", "4", "blend", "ADD", "local_coords", "false", NULL };

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
    cw_node_set(w, "thickness", cw_val_float(1.0)); cw_node_set(w, "strength", cw_val_float(0.32));
    cw_node3d_set_position(w, (t3_vec3){ 0, 1.05f, 0.2f });
    cw_node_add_child(b, w);
    cw_node gw = cw_node_new("Shockwave3D", "GroundWave");
    cw_node_set(gw, "autostart", cw_val_bool(false)); cw_node_set(gw, "ground", cw_val_bool(true)); cw_node_set(gw, "max_radius", cw_val_float(6.0));
    cw_node_set(gw, "duration", cw_val_float(0.75)); cw_node_set(gw, "thickness", cw_val_float(0.8)); cw_node_set(gw, "strength", cw_val_float(0.2));
    cw_node3d_set_position(gw, (t3_vec3){ 0, 0.03f, 0 });
    cw_node_add_child(b, gw);
  }
  FX.ready = true;
}

static void heavy_boom(t3_vec3 p) { cw_node b = FX.hboom[FX.hbi++ % 3]; cw_node3d_set_position(b, p); cw_explosion_fire(b); }
static void super_boom(float x) { cw_node3d_set_position(FX.sboom, (t3_vec3){ x, 0, 0 }); cw_explosion_fire(FX.sboom); }

void fx_bind_lights(cw_node stage) {
  for (int i = 0; i < 3; i++) {
    char nm[16];
    snprintf(nm, sizeof nm, "Flash%d", i);
    FX.light[i] = cw_node_get(stage, nm);
  }
}

void fx_reset(void) { FX.shake = FX.flash = 0; FX.seen = 0; for (int i = 0; i < 3; i++) FX.light_age[i] = 0; }
float fx_shake(void) { return FX.shake; }
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

void fx_event(const ft_game *g, const ft_event *e) {
  t3_vec3 p = { (float)e->x / 1000.0f, (float)e->y / 1000.0f, 0.15f };
  float k = (float)e->power;
  switch (e->type) {
  case EV_HIT: {
    fx_count_hit++;
    bool big = e->power >= 3;
    int i = FX.si++ % 4;
    cw_node_set(FX.sparks[i], "amount", cw_val_int(big ? 48 : 28));
    at(FX.sparks[i], p);
    at(FX.flash_p[FX.fi++ % 4], p);
    int r = FX.ri++ % 4;
    cw_node_set(FX.ring[r], "amount", cw_val_int(big ? 18 : 10));
    at(FX.ring[r], p);
    light(p, big ? 90 : 50, 1, 0.75f, 0.4f);
    FX.shake = fmaxf(FX.shake, big ? 0.55f : 0.18f + 0.06f * k);
    if (big) heavy_boom(p);
    break;
  }
  case EV_BLOCK: {
    fx_count_block++;
    at(FX.bsparks[FX.bi++ % 3], p);
    at(FX.bring[FX.bri++ % 3], p);
    light(p, 22, 0.5f, 0.7f, 1);
    FX.shake = fmaxf(FX.shake, 0.08f);
    break;
  }
  case EV_THROW: at(FX.dust[FX.di++ % 4], (t3_vec3){ p.x, 0.05f, 0.1f }); at(FX.chips[FX.ci++ % 3], (t3_vec3){ p.x, 0.02f, 0.1f }); FX.shake = fmaxf(FX.shake, 0.35f); light(p, 40, 1, 0.8f, 0.5f); break;
  case EV_DUST: {
    cw_node d = FX.dust[FX.di++ % 4];
    at(d, (t3_vec3){ p.x, 0.04f, 0.1f });
    if (e->power >= 2) at(FX.chips[FX.ci++ % 3], (t3_vec3){ p.x, 0.02f, 0.1f });
    break;
  }
  case EV_WALL: at(FX.dust[FX.di++ % 4], (t3_vec3){ p.x, 0.04f, 0.1f }); at(FX.chips[FX.ci++ % 3], (t3_vec3){ p.x, 0.02f, 0.1f }); FX.shake = fmaxf(FX.shake, 0.3f); break;
  case EV_FIRE: light((t3_vec3){ p.x, 1.05f, 0.5f }, 30, 1, 0.6f, 0.3f); at(FX.flash_p[FX.fi++ % 4], (t3_vec3){ p.x, 1.05f, 0.2f }); break;
  case EV_SUPER: {
    FX.flash = 1;
    FX.shake = fmaxf(FX.shake, 0.4f);
    at(FX.streaks[FX.sti++ % 2], (t3_vec3){ p.x, 1.0f, 0.2f });
    int r = FX.ri++ % 4;
    cw_node_set(FX.ring[r], "amount", cw_val_int(24));
    at(FX.ring[r], (t3_vec3){ p.x, 1.0f, 0.2f });
    light((t3_vec3){ p.x, 1.2f, 1.0f }, 120, 0.7f, 0.8f, 1);
    super_boom(p.x);
    break;
  }
  case EV_CLASH: at(FX.flash_p[FX.fi++ % 4], p); at(FX.sparks[FX.si++ % 4], p); light(p, 40, 1, 0.8f, 0.5f); break;
  case EV_TECH: at(FX.bring[FX.bri++ % 3], p); at(FX.bsparks[FX.bi++ % 3], p); light(p, 30, 0.7f, 0.9f, 1); break;
  case EV_KO: {
    FX.flash = 0.8f;
    FX.shake = 1.0f;
    for (int i = 0; i < 3; i++) at(FX.sparks[FX.si++ % 4], (t3_vec3){ p.x, 1.1f, 0.2f });
    at(FX.streaks[FX.sti++ % 2], (t3_vec3){ p.x, 1.1f, 0.2f });
    light((t3_vec3){ p.x, 1.2f, 1.0f }, 140, 1, 0.9f, 0.7f);
    super_boom(p.x);
    break;
  }
  default: break;
  }
  (void)g;
}

static const float PROJ_COL[4][3] = { { 1, 0.45f, 0.1f }, { 0.9f, 0.7f, 0.3f }, { 0.2f, 0.9f, 1 }, { 0.5f, 1, 0.3f } };

void fx_update(float dt) { (void)dt; }
void fx_frame(const ft_game *g, float dt) {
  if (!FX.ready) return;
  /* events since last call */
  uint32_t n = g->ev_seq;
  if (n - FX.seen > 64) FX.seen = n - 64;
  while (FX.seen < n) { fx_event(g, &g->ev[FX.seen & 63]); FX.seen++; }
  /* projectiles carry their glow */
  for (int i = 0; i < 8; i++) {
    const ft_proj *P = &g->proj[i];
    bool live = P->live;
    cw_node_set(FX.proj[i], "emitting", cw_val_bool(live));
    cw_node_set(FX.ember[i], "emitting", cw_val_bool(live));
    if (live) {
      t3_vec3 pos = { (float)P->x / 1000.0f, (float)P->y / 1000.0f, 0.1f };
      cw_node3d_set_position(FX.proj[i], pos);
      cw_node3d_set_position(FX.ember[i], pos);
      const float *c = PROJ_COL[g->f[P->owner].def % 4];
      bool big = g->roster->d[g->f[P->owner].def].mv[P->mv].flags & FM_SUPER;
      tint(FX.proj[i], c[0], c[1], c[2]);
      tint(FX.ember[i], c[0], c[1], c[2]);
      cw_node_set(FX.proj[i], "size", cw_val_float(big ? 1.8 : 0.45 + (double)P->size / 900.0));
    }
  }
  /* lights decay */
  for (int i = 0; i < 3; i++) {
    if (!cw_node_valid(FX.light[i])) continue;
    int a = FX.light_age[i];
    float e = 0;
    if (a > 0) {
      e = a < 2 ? FX.light_e[i] : FX.light_e[i] * cw_exp(-(float)(a - 2) * 0.35f);
      FX.light_age[i] = a < 40 ? a + 1 : 0;
    }
    cw_node_set(FX.light[i], "energy", cw_val_float(e));
  }
  FX.shake *= cw_exp(-dt * 9.0f);
  FX.flash *= cw_exp(-dt * 6.0f);
  if (FX.shake < 0.002f) FX.shake = 0;
}
