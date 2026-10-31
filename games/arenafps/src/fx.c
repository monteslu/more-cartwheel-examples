/* Effects and sound. Everything is pooled: nodes are made once and moved
 * and restarted, never created in play. Visual only: nothing here feeds the
 * simulation (its own random stream). */
#include "fx.h"

#include "cartwheel/explosions.h"
#include "cartwheel/particles.h"

static cw_rng R;
static cw_node ROOT;

typedef struct pool {
  cw_node n[48];
  int cnt, next;
} pool;
static pool P_SPARK, P_DUST, P_HIT, P_TRAIL, P_BOOM;

static cw_node new_ps(const char *name, int amount, float life, float smin, float smax, float spread, float gravity, float size,
                      const char *ramp, const char *curve, float energy, const char *mode, const char *blend) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_set(n, "amount", cw_val_int(amount));
  cw_node_set(n, "lifetime", cw_val_float(life));
  cw_node_set(n, "explosiveness", cw_val_float(0.95));
  cw_node_set(n, "one_shot", cw_val_bool(true));
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "direction", cw_val_vec3(0, 0, -1));
  cw_node_set(n, "spread", cw_val_float(spread));
  cw_node_set(n, "speed_min", cw_val_float(smin));
  cw_node_set(n, "speed_max", cw_val_float(smax));
  cw_node_set(n, "gravity", cw_val_vec3(0, gravity, 0));
  cw_node_set(n, "size", cw_val_float(size));
  cw_node_set(n, "size_random", cw_val_float(0.4));
  cw_node_set(n, "color_ramp", cw_val_string(ramp));
  cw_node_set(n, "size_curve", cw_val_string(curve));
  cw_node_set(n, "energy", cw_val_float(energy));
  cw_node_set(n, "draw_mode", cw_val_enum(mode));
  cw_node_set(n, "blend", cw_val_enum(blend));
  cw_node_add_child(ROOT, n);
  return n;
}

static void fill(pool *p, int count, cw_node (*make)(int i)) {
  p->cnt = count;
  for (int i = 0; i < count; i++) p->n[i] = make(i);
  p->next = 0;
}

static cw_node mk_spark(int i) {
  (void)i;
  cw_node n = new_ps("Spark", 14, 0.35f, 2, 5.5f, 50, -9, 0.011f, "0 1 0.9 0.5 1, 0.3 1 0.5 0.15 1, 1 0.4 0.1 0.05 0", "0 1, 1 0.3", 5, "STRETCHED", "ADD"); /* fine: near the eye a big stretched spark is a yellow slab */
  cw_node_set(n, "stretch", cw_val_float(0.012));
  return n;
}
static cw_node mk_hit(int i) {
  (void)i;
  cw_node n = new_ps("HitSpark", 10, 0.3f, 1.5f, 4.5f, 70, -6, 0.014f, "0 1 0.95 0.8 1, 0.4 1 0.7 0.35 1, 1 0.5 0.2 0.05 0", "0 1, 1 0.2", 4, "STRETCHED", "ADD");
  cw_node_set(n, "stretch", cw_val_float(0.01));
  return n;
}
static cw_node mk_dust(int i) {
  (void)i;
  cw_node n = new_ps("Dust", 5, 0.8f, 0.4f, 1.4f, 55, 0.25f, 0.3f, "0 0.5 0.45 0.4 0, 0.12 0.5 0.45 0.4 0.4, 1 0.45 0.4 0.36 0", "0 0.4, 0.4 1, 1 1.4", 1, "BILLBOARD", "ALPHA");
  cw_node_set(n, "texture", cw_val_string("textures/fx/smoke.png")); /* soft puffs: an untextured billboard is a soft-edged square */
  cw_node_set(n, "hframes", cw_val_int(8));
  cw_node_set(n, "vframes", cw_val_int(8));
  return n;
}
/* decals that stick to the surface they hit (mode MESH: the engine clips the triangles under the box and keeps them with the mesh);
 * the engine recycles the oldest past max_mesh_decals, so bullet holes and scorch marks are made as needed and forgotten */
static cw_node make_decal(const char *name, const char *tex, t3_vec3 size, cw_color c, const char *blend, int keep) {
  cw_node n = cw_node_new("Decal3D", name);
  cw_node_set(n, "size", cw_val_vec3(size.x, size.y, size.z));
  cw_node_set(n, "texture", cw_val_string(tex));
  cw_node_set(n, "blend", cw_val_enum(blend));
  cw_node_set(n, "color", cw_val_color(c.r, c.g, c.b, 1));
  cw_node_set(n, "normal_fade", cw_val_float(0.35));
  cw_node_set(n, "edge_fade", cw_val_float(0.15));
  cw_node_set(n, "mode", cw_val_enum("MESH"));
  cw_node_set(n, "max_mesh_decals", cw_val_int(keep));
  cw_node_set(n, "max_triangles", cw_val_int(256));
  return n;
}
/* a rocket blast: the engine's explosion kit (cartwheel/explosions.h). An Explosion3D whose parts are authored below (a hot
 * flash, a flipbook fireball, flames, rolling smoke, sparks, stone chunks trailing smoke, heat haze, a ground ring and an air
 * ring); firing it restarts them all and borrows a flash light and a ground scorch mark from the kit's pools */
static cw_node part(cw_node boom, const char *name, int amount, float life, float explode, const char *shape, float radius, t3_vec3 dir,
                    float spread, float smin, float smax, float gravity, float size, const char *curve, const char *ramp, float energy,
                    const char *blend, uint32_t seed) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "one_shot", cw_val_bool(true));
  cw_node_set(n, "amount", cw_val_int(amount));
  cw_node_set(n, "lifetime", cw_val_float(life));
  cw_node_set(n, "explosiveness", cw_val_float(explode));
  cw_node_set(n, "emission_shape", cw_val_enum(shape));
  cw_node_set(n, "emission_radius", cw_val_float(radius));
  cw_node_set(n, "direction", cw_val_vec3(dir.x, dir.y, dir.z));
  cw_node_set(n, "spread", cw_val_float(spread));
  cw_node_set(n, "speed_min", cw_val_float(smin));
  cw_node_set(n, "speed_max", cw_val_float(smax));
  cw_node_set(n, "gravity", cw_val_vec3(0, gravity, 0));
  cw_node_set(n, "size", cw_val_float(size));
  cw_node_set(n, "size_random", cw_val_float(0.35));
  cw_node_set(n, "size_curve", cw_val_string(curve));
  cw_node_set(n, "color_ramp", cw_val_string(ramp));
  cw_node_set(n, "energy", cw_val_float(energy));
  cw_node_set(n, "blend", cw_val_enum(blend));
  cw_node_set(n, "seed", cw_val_int((int)seed));
  cw_node_add_child(boom, n);
  return n;
}
static cw_node mk_boom(int i) {
  cw_node b = cw_node_new("Explosion3D", "Boom");
  cw_node_set(b, "duration", cw_val_float(3.0));
  cw_node_set(b, "flash_energy", cw_val_float(220));
  cw_node_set(b, "flash_color", cw_val_color(1, 0.62f, 0.3f, 1));
  cw_node_set(b, "flash_range", cw_val_float(15));
  cw_node_set(b, "flash_duration", cw_val_float(0.4));
  cw_node_set(b, "scorch_size", cw_val_float(3.6));
  cw_node_set(b, "scorch_texture", cw_val_string("textures/fx/scorch.png"));
  cw_node_set(b, "shake_amount", cw_val_float(0)); /* the kit records no camera shake: a blast is felt in the pads (main.c, rumble) */
  cw_node3d_set_position(b, v3(0, -500, 0));
  cw_node_add_child(ROOT, b);
  uint32_t s = 100u + (uint32_t)i * 16u;
  const t3_vec3 UP = { 0, 1, 0 };
  part(b, "Flash", 3, 0.16f, 1, "POINT", 0, UP, 180, 0, 0.3f, 0, 3.4f, "0 0.6, 1 1.4", "0 1 0.95 0.8 1, 1 1 0.55 0.2 0", 16, "ADD", s);
  cw_node fire = part(b, "Fire", 9, 1.2f, 1, "SPHERE", 0.6f, UP, 180, 0.4f, 1.8f, 1.4f, 3.6f, "0 0.55, 0.3 1, 1 1.2",
                      "0 1 1 1 0, 0.06 1 1 1 1, 0.8 1 0.85 0.8 0.9, 1 1 0.7 0.6 0", 1.6f, "ADD", s + 1);
  cw_node_set(fire, "texture", cw_val_string("textures/fx/fire.png"));
  cw_node_set(fire, "motion_texture", cw_val_string("textures/fx/fire_mv.png"));
  cw_node_set(fire, "hframes", cw_val_int(8));
  cw_node_set(fire, "vframes", cw_val_int(8));
  cw_node_set(fire, "damping", cw_val_float(2.2));
  cw_node_set(fire, "spin", cw_val_float(0.25));
  cw_node_set(fire, "soft_distance", cw_val_float(0.6));
  cw_node smoke = part(b, "Smoke", 12, 2.6f, 0.6f, "SPHERE", 0.7f, UP, 70, 0.6f, 2.4f, 0.9f, 3.8f, "0 0.45, 0.3 1, 1 1.5",
                       "0 1 0.9 0.8 0, 0.08 0.7 0.66 0.6 0.9, 0.45 0.34 0.33 0.32 0.7, 1 0.28 0.28 0.29 0", 1, "ALPHA", s + 2);
  cw_node_set(smoke, "texture", cw_val_string("textures/fx/smoke.png"));
  cw_node_set(smoke, "motion_texture", cw_val_string("textures/fx/smoke_mv.png"));
  cw_node_set(smoke, "hframes", cw_val_int(8));
  cw_node_set(smoke, "vframes", cw_val_int(8));
  cw_node_set(smoke, "damping", cw_val_float(1.4));
  cw_node_set(smoke, "spin", cw_val_float(0.18));
  cw_node_set(smoke, "sort", cw_val_bool(true));
  cw_node_set(smoke, "soft_distance", cw_val_float(0.8));
  cw_node sparks = part(b, "Sparks", 70, 1.0f, 0.95f, "POINT", 0, UP, 180, 4, 13, -9.8f, 0.016f, "0 1, 1 0.25",
                        "0 1 0.9 0.6 1, 0.5 1 0.45 0.1 1, 1 0.6 0.1 0.05 0", 5, "ADD", s + 3);
  cw_node_set(sparks, "draw_mode", cw_val_enum("STRETCHED"));
  cw_node_set(sparks, "stretch", cw_val_float(0.022));
  cw_node_set(sparks, "collide_depth", cw_val_bool(true));
  cw_node_set(sparks, "bounce", cw_val_float(0.45));
  cw_node chunks = part(b, "Debris", 14, 1.6f, 1, "SPHERE", 0.3f, UP, 70, 5, 11, -16, 0.12f, "0 1, 1 0.8",
                        "0 0.5 0.45 0.4 1, 1 0.3 0.28 0.25 1", 1, "OPAQUE", s + 4);
  cw_node_set(chunks, "draw_mode", cw_val_enum("MESH"));
  cw_node_set(chunks, "mesh_primitive", cw_val_enum("TETRA"));
  cw_node_set(chunks, "spin", cw_val_float(8));
  cw_node_set(chunks, "collide_depth", cw_val_bool(true));
  cw_node_set(chunks, "bounce", cw_val_float(0.35));
  cw_node trail = part(b, "DebrisTrail", 120, 0.9f, 0, "PARENT", 0, UP, 180, 0.1f, 0.4f, 0.6f, 0.5f, "0 0.4, 1 1.4",
                       "0 0.6 0.56 0.52 0, 0.15 0.45 0.43 0.41 0.5, 1 0.3 0.3 0.3 0", 1, "ALPHA", s + 5);
  cw_node_set(trail, "parent", cw_val_string("../Debris"));
  cw_node_set(trail, "inherit_velocity", cw_val_float(0.05));
  cw_node_set(trail, "texture", cw_val_string("textures/fx/smoke.png"));
  cw_node_set(trail, "hframes", cw_val_int(8));
  cw_node_set(trail, "vframes", cw_val_int(8));
  cw_node haze = part(b, "Haze", 30, 1.6f, 0.4f, "SPHERE", 0.9f, UP, 60, 0.8f, 2.4f, 0, 3.2f, "0 0.6, 1 1.5",
                      "0 1 1 1 0, 0.15 1 1 1 1, 1 1 1 1 0", 1, "DISTORT", s + 6);
  cw_node_set(haze, "distort_strength", cw_val_float(0.14));
  cw_node_set(haze, "turbulence", cw_val_float(0.7));
  cw_node wave = cw_node_new("Shockwave3D", "Wave");
  cw_node_set(wave, "autostart", cw_val_bool(false));
  cw_node_set(wave, "ground", cw_val_bool(true));
  cw_node_set(wave, "max_radius", cw_val_float(7));
  cw_node_set(wave, "duration", cw_val_float(0.7));
  cw_node_set(wave, "thickness", cw_val_float(1.2));
  cw_node_set(wave, "strength", cw_val_float(0.5));
  cw_node3d_set_position(wave, v3(0, 0.1f, 0));
  cw_node_add_child(b, wave);
  cw_node air = cw_node_new("Shockwave3D", "WaveAir");
  cw_node_set(air, "autostart", cw_val_bool(false));
  cw_node_set(air, "max_radius", cw_val_float(5));
  cw_node_set(air, "duration", cw_val_float(0.5));
  cw_node_set(air, "thickness", cw_val_float(0.9));
  cw_node_set(air, "strength", cw_val_float(0.8));
  cw_node3d_set_position(air, v3(0, 1.0f, 0));
  cw_node_add_child(b, air);
  return b;
}
static cw_node mk_trail(int i) {
  (void)i;
  cw_node n = cw_node_new("MeshInstance3D", "Tracer");
  cw_node_set(n, "primitive", cw_val_enum("BOX"));
  cw_node_set(n, "size", cw_val_vec3(0.03f, 0.03f, 1));
  cw_node_set(n, "color", cw_val_color(1, 0.9f, 0.5f, 1));
  cw_node_set(n, "emissive", cw_val_color(6, 4.5f, 1.8f, 1));
  cw_node_set(n, "cast_shadow", cw_val_bool(false));
  cw_node_set(n, "visible", cw_val_bool(false));
  cw_node_add_child(ROOT, n);
  return n;
}
/* a short life for each tracer and explosion light, and the casings (CPU, visual only) */
typedef struct life { int t, t0; } life;
static life TR_LIFE[48];
static float TR_LEN[48];
static float TR_W[48];

#define NCASE 20
typedef struct casing {
  cw_node n;
  t3_vec3 p, v, spin;
  int t;
} casing;
static casing CASE[NCASE];
static int case_next;

static cw_sound S[SND_COUNT];

static void tone_into(float *pcm, int n, int rate, float f0, float f1, float noise, float gain, float decay, float lp) {
  uint32_t h = 2463534242u;
  double ph = 0;
  float lo = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / (float)n;
    float f = f0 + (f1 - f0) * t;
    ph += 6.283185307 * f / rate;
    h ^= h << 13, h ^= h >> 17, h ^= h << 5;
    float nz = ((float)(h & 0xffff) / 32768.0f - 1.0f);
    lo += (nz - lo) * lp; /* a one pole low pass on the noise */
    float env = cw_exp(-decay * t);
    float atk = t < 0.01f ? t / 0.01f : 1;
    pcm[i] += ((float)sin(ph) * (1 - noise) + lo * noise * 2.2f) * gain * env * atk;
  }
}
static cw_sound make_snd(float seconds, void (*fn)(float *, int)) {
  enum { RATE = 44100 };
  static float pcm[RATE * 3];
  int n = (int)(seconds * RATE);
  memset(pcm, 0, sizeof(float) * (size_t)n);
  fn(pcm, n);
  for (int i = 0; i < n; i++) pcm[i] = pcm[i] > 1 ? 1 : pcm[i] < -1 ? -1 : pcm[i];
  return cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
}
#define SND(name, secs, body) \
  static void name##_f(float *p, int n) { const int rate = 44100; (void)rate; body } \
  static cw_sound name##_m(void) { return make_snd(secs, name##_f); }
/* the rifle: a bright crack (fast noise), a low body thump and a short dark tail, so it lands like a shot and not a click */
SND(rifle, 0.34f, tone_into(p, n, rate, 2600, 900, 0.95f, 0.75f, 46, 0.85f); tone_into(p, n, rate, 160, 48, 0.12f, 1.25f, 16, 1); tone_into(p, n, rate, 420, 110, 0.9f, 0.45f, 7, 0.1f);)
SND(scatter, 0.48f, tone_into(p, n, rate, 1900, 500, 0.95f, 0.85f, 28, 0.85f); tone_into(p, n, rate, 130, 42, 0.5f, 1.35f, 7, 0.75f); tone_into(p, n, rate, 72, 38, 0.15f, 0.9f, 8, 1);)
SND(rocket, 0.5f, tone_into(p, n, rate, 300, 90, 0.8f, 0.8f, 4, 0.2f); tone_into(p, n, rate, 140, 80, 0.1f, 0.5f, 5, 1);)
SND(boom, 1.6f, tone_into(p, n, rate, 70, 28, 0.8f, 1.6f, 3.2f, 0.06f); tone_into(p, n, rate, 48, 24, 0.0f, 1.2f, 3, 1);)
SND(rail, 0.8f, tone_into(p, n, rate, 5200, 1200, 0.9f, 0.7f, 40, 0.9f); tone_into(p, n, rate, 3200, 220, 0.1f, 0.6f, 4, 1); tone_into(p, n, rate, 90, 50, 0.6f, 0.85f, 7, 0.3f);)
SND(hit, 0.07f, tone_into(p, n, rate, 1500, 900, 0.1f, 0.5f, 8, 1);)
SND(hurt, 0.25f, tone_into(p, n, rate, 220, 90, 0.4f, 0.8f, 5, 0.4f);)
SND(pickup, 0.3f, tone_into(p, n, rate, 780, 1180, 0, 0.5f, 3, 1);)
SND(die, 0.8f, tone_into(p, n, rate, 260, 40, 0.5f, 0.9f, 3, 0.3f);)
SND(click, 0.05f, tone_into(p, n, rate, 600, 500, 0.4f, 0.5f, 10, 0.6f);)
SND(spawn, 0.5f, tone_into(p, n, rate, 300, 900, 0, 0.4f, 2, 1);)
SND(step, 0.1f, tone_into(p, n, rate, 90, 50, 0.9f, 0.5f, 7, 0.15f);)
SND(shell, 0.12f, tone_into(p, n, rate, 3400, 2600, 0.05f, 0.4f, 12, 1);)
SND(tick, 0.09f, tone_into(p, n, rate, 1250, 800, 0.25f, 0.55f, 30, 0.8f); tone_into(p, n, rate, 240, 120, 0.3f, 0.5f, 28, 1);)
SND(head, 0.26f, tone_into(p, n, rate, 2350, 2350, 0, 0.45f, 16, 1); tone_into(p, n, rate, 3525, 3525, 0, 0.28f, 22, 1); tone_into(p, n, rate, 300, 150, 0.3f, 0.4f, 24, 1);)
SND(kill, 0.62f, tone_into(p, n, rate, 170, 55, 0.35f, 0.95f, 11, 0.8f);
    for (int k = 0; k < 2; k++) { int off = k * (int)(0.085f * (float)rate); tone_into(p + off, n - off, rate, k ? 1318 : 880, k ? 1318 : 880, 0, k ? 0.5f : 0.42f, 7, 1); tone_into(p + off, n - off, rate, k ? 2636 : 1760, k ? 2636 : 1760, 0, 0.14f, 11, 1); })
SND(land, 0.18f, tone_into(p, n, rate, 110, 45, 0.7f, 0.9f, 10, 0.2f);)
SND(pad, 0.45f, tone_into(p, n, rate, 160, 620, 0.25f, 0.8f, 4, 0.5f); tone_into(p, n, rate, 80, 140, 0.0f, 0.6f, 5, 1);)

void fx_init(cw_node root) {
  ROOT = root;
  cw_rng_seed(&R, 99);
  fill(&P_SPARK, 12, mk_spark);
  fill(&P_HIT, 12, mk_hit);
  fill(&P_DUST, 10, mk_dust);
  fill(&P_BOOM, 4, mk_boom);
  fill(&P_TRAIL, 24, mk_trail);
  for (int i = 0; i < NCASE; i++) {
    cw_node n = cw_node_new("MeshInstance3D", "Casing");
    cw_node_set(n, "primitive", cw_val_enum("CYLINDER"));
    cw_node_set(n, "size", cw_val_vec3(0.008f, 0.024f, 0));
    cw_node_set(n, "color", cw_val_color(0.8f, 0.6f, 0.2f, 1));
    cw_node_set(n, "metalness", cw_val_float(1));
    cw_node_set(n, "roughness", cw_val_float(0.3));
    cw_node_set(n, "cast_shadow", cw_val_bool(false));
    cw_node3d_set_position(n, v3(0, -500, 0));
    cw_node_add_child(root, n);
    CASE[i].n = n;
  }
  static bool snd_done;
  if (!snd_done) {
    snd_done = true;
    S[SND_RIFLE] = rifle_m(), S[SND_SCATTER] = scatter_m(), S[SND_ROCKET] = rocket_m(), S[SND_BOOM] = boom_m();
    S[SND_RAIL] = rail_m(), S[SND_HIT] = hit_m(), S[SND_HURT] = hurt_m(), S[SND_PICKUP] = pickup_m();
    S[SND_DIE] = die_m(), S[SND_CLICK] = click_m(), S[SND_SPAWN] = spawn_m(), S[SND_STEP] = step_m();
    S[SND_SHELL] = shell_m(), S[SND_TICK] = tick_m(), S[SND_PAD] = pad_m(), S[SND_LAND] = land_m();
    S[SND_HEAD] = head_m(), S[SND_KILL] = kill_m();
  }
}

void fx_reset(void) {
  /* the pools' nodes belong to the level root and were freed with it: fx_init again */
  memset(&P_SPARK, 0, sizeof P_SPARK), memset(&P_HIT, 0, sizeof P_HIT), memset(&P_DUST, 0, sizeof P_DUST);
  memset(&P_BOOM, 0, sizeof P_BOOM), memset(&P_TRAIL, 0, sizeof P_TRAIL);
  memset(TR_LIFE, 0, sizeof TR_LIFE);
  memset(CASE, 0, sizeof CASE);
}

static cw_node take(pool *p, int *idx) {
  int i = p->next;
  p->next = (p->next + 1) % p->cnt;
  if (idx) *idx = i;
  return p->n[i];
}

static void aim_node(cw_node n, t3_vec3 at, t3_vec3 normal) {
  cw_node3d_set_position(n, at);
  t3_vec3 up = fabsf(normal.y) > 0.98f ? v3(1, 0, 0) : v3(0, 1, 0);
  cw_node3d_look_at(n, v3add(at, normal), up);
}

void fx_play(int snd, t3_vec3 at, float volume, float pitch) {
  cw_voice_3d sp = { .position = at, .model = CW_ATTEN_INVERSE, .ref_distance = 4, .max_distance = 90, .rolloff = 1 };
  cw_audio_play(S[snd], &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = volume, .pitch = pitch, .spatial = true, .space = sp });
}
void fx_play_ui(int snd, float volume) { cw_audio_play(S[snd], &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = volume }); }

void fx_spark(t3_vec3 p, t3_vec3 n, int kind) {
  pool *sp = kind == 1 ? &P_HIT : &P_SPARK;
  cw_node s = take(sp, NULL);
  aim_node(s, v3add(p, v3scale(n, 0.04f)), n);
  cw_particles_restart(s);
  if (kind == 0) {
    cw_node d = take(&P_DUST, NULL);
    aim_node(d, v3add(p, v3scale(n, 0.1f)), n);
    cw_particles_restart(d);
  }
}

void fx_hole(t3_vec3 p, t3_vec3 n) {
  float s = 0.1f + cw_rng_float(&R) * 0.05f;
  cw_node d = make_decal("Hole", "textures/hole.png", v3(s, s, 0.3f), (cw_color){ 1, 1, 1, 1 }, "MIX", 80);
  cw_node3d_set_position(d, v3add(p, v3scale(n, 0.1f)));
  t3_vec3 up = fabsf(n.y) > 0.98f ? v3(1, 0, 0) : v3(0, 1, 0);
  cw_node3d_look_at(d, v3sub(p, n), up);
  cw_node_add_child(ROOT, d);
}

void fx_explosion(t3_vec3 p, t3_vec3 n) {
  t3_vec3 nn = v3len(n) < 0.5f ? v3(0, 1, 0) : n;
  bool floor = nn.y > 0.7f;
  cw_node b = take(&P_BOOM, NULL);
  /* the kit scorches the ground under the node: on a floor the node stands on it; on a wall or a ceiling it stands just
   * off the surface and a mesh decal marks the wall instead */
  cw_node3d_set_position(b, floor ? p : v3add(p, v3scale(nn, 0.5f)));
  cw_node_set(b, "scorch_size", cw_val_float(floor ? 3.2f + cw_rng_float(&R) * 0.8f : 0));
  cw_node sp = cw_node_get(b, "Sparks"), db = cw_node_get(b, "Debris");
  if (cw_node_valid(sp)) cw_node_set(sp, "direction", cw_val_vec3(nn.x, nn.y, nn.z));
  if (cw_node_valid(db)) cw_node_set(db, "direction", cw_val_vec3(nn.x, nn.y + (floor ? 0 : 0.4f), nn.z));
  cw_explosion_fire(b);
  if (!floor) {
    float s = 2.6f + cw_rng_float(&R) * 0.8f;
    cw_node sc = make_decal("Scorch", "textures/scorch.png", v3(s, s, 1.6f), (cw_color){ 1, 1, 1, 1 }, "MIX", 16);
    cw_node3d_set_position(sc, v3add(p, v3scale(nn, 0.15f)));
    t3_vec3 up = v3norm(v3cross(nn, v3(cw_rng_range(&R, -1, 1), cw_rng_range(&R, -1, 1), cw_rng_range(&R, -1, 1)))); /* a turned mark each time */
    if (v3len(up) < 0.5f) up = fabsf(nn.y) > 0.98f ? v3(1, 0, 0) : v3(0, 1, 0);
    cw_node3d_look_at(sc, v3sub(p, nn), up);
    cw_node_add_child(ROOT, sc);
  }
  fx_play(SND_BOOM, p, 1.0f, 0.95f + cw_rng_float(&R) * 0.1f);
}

void fx_tracer(t3_vec3 a, t3_vec3 b, int kind) {
  int i;
  cw_node n = take(&P_TRAIL, &i);
  t3_vec3 d = v3sub(b, a);
  float len = v3len(d);
  if (len < 0.1f) return;
  if (kind == 0 && len < 3) return; /* a wall at arm's length: sparks say enough, a tracer is a stick in the face */
  if (kind == 0 && len > 5) { /* a bullet is a short streak somewhere along its path, not a beam from the shooter (a whole beam beside the eye is a band across the view) */
    float at = cw_rng_float(&R) * (len - 5);
    a = v3add(a, v3scale(d, at / len));
    d = v3scale(d, 5 / len);
    len = 5;
  }
  TR_LEN[i] = len;
  TR_W[i] = kind == 1 ? 0.05f : 0.014f;
  TR_LIFE[i] = (life){ kind == 1 ? 18 : 4, kind == 1 ? 18 : 4 };
  cw_node_set(n, "size", cw_val_vec3(TR_W[i], TR_W[i], len));
  if (kind == 1) {
    cw_node_set(n, "color", cw_val_color(0.7f, 0.5f, 1, 1));
    cw_node_set(n, "emissive", cw_val_color(3.5f, 2.2f, 8, 1));
  } else {
    cw_node_set(n, "color", cw_val_color(1, 0.9f, 0.5f, 1));
    cw_node_set(n, "emissive", cw_val_color(7, 5, 2, 1));
  }
  cw_node_set(n, "visible", cw_val_bool(true));
  cw_node3d_set_position(n, v3add(a, v3scale(d, 0.5f)));
  cw_node3d_look_at(n, b, fabsf(d.y / len) > 0.98f ? v3(1, 0, 0) : v3(0, 1, 0));
}

void fx_casing(t3_vec3 p, t3_vec3 right, t3_vec3 up) {
  casing *c = &CASE[case_next];
  case_next = (case_next + 1) % NCASE;
  c->p = p;
  c->v = v3add(v3scale(right, 2.0f + cw_rng_float(&R)), v3(0, 0, 0));
  c->v = v3add(c->v, v3scale(up, 2.0f + cw_rng_float(&R)));
  c->spin = v3(cw_rng_range(&R, -20, 20), cw_rng_range(&R, -20, 20), cw_rng_range(&R, -20, 20));
  c->t = 90;
}

void fx_tick(void) {
  for (int i = 0; i < P_TRAIL.cnt; i++) {
    if (TR_LIFE[i].t > 0) {
      TR_LIFE[i].t--;
      float k = (float)TR_LIFE[i].t / (float)TR_LIFE[i].t0;
      float w = TR_W[i] * (0.3f + 0.7f * k);
      cw_node_set(P_TRAIL.n[i], "size", cw_val_vec3(w, w, TR_LEN[i]));
      if (!TR_LIFE[i].t) cw_node_set(P_TRAIL.n[i], "visible", cw_val_bool(false));
    }
  }
  for (int i = 0; i < NCASE; i++) {
    casing *c = &CASE[i];
    if (c->t <= 0) continue;
    c->t--;
    c->v.y -= 16 * TICK_DT;
    t3_vec3 np = v3add(c->p, v3scale(c->v, TICK_DT));
    if (np.y < 0.04f || arena_ray(c->p, np, NULL, NULL)) {
      np = c->p;
      c->v = v3(c->v.x * 0.4f, -c->v.y * 0.35f, c->v.z * 0.4f);
      if (fabsf(c->v.y) < 0.4f) c->v = v3(0, 0, 0), c->spin = v3(0, 0, 0);
    }
    c->p = np;
    cw_node3d_set_position(c->n, c->t ? c->p : v3(0, -500, 0));
    t3_vec3 r = cw_node3d_quaternion(c->n).w == 0 ? v3(0, 0, 0) : v3(0, 0, 0);
    (void)r;
    cw_node3d_set_rotation(c->n, v3(c->spin.x * (float)(90 - c->t) * TICK_DT, c->spin.y * (float)(90 - c->t) * TICK_DT, c->spin.z * (float)(90 - c->t) * TICK_DT));
  }
}
