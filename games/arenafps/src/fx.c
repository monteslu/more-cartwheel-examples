/* Effects and sound. Everything is pooled: nodes are made once and moved
 * and restarted, never created in play. Visual only: nothing here feeds the
 * simulation (its own random stream). */
#include "fx.h"

#include "cartwheel/particles.h"

static cw_rng R;
static cw_node ROOT;

typedef struct pool {
  cw_node n[48];
  int cnt, next;
} pool;
static pool P_SPARK, P_DUST, P_HIT, P_FIRE, P_SMOKE, P_EMBER, P_DECAL, P_SCORCH, P_TRAIL, P_ELIGHT;

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
  cw_node n = new_ps("Spark", 16, 0.45f, 3, 9, 65, -9, 0.045f, "0 1 0.9 0.5 1, 0.3 1 0.5 0.15 1, 1 0.4 0.1 0.05 0", "0 1, 1 0.3", 9, "STRETCHED", "ADD");
  cw_node_set(n, "stretch", cw_val_float(0.07));
  return n;
}
static cw_node mk_hit(int i) {
  (void)i;
  cw_node n = new_ps("HitSpark", 10, 0.3f, 2, 6, 80, -6, 0.05f, "0 1 1 1 1, 0.4 0.5 0.9 1 1, 1 0.2 0.5 1 0", "0 1, 1 0.2", 8, "STRETCHED", "ADD");
  cw_node_set(n, "stretch", cw_val_float(0.06));
  return n;
}
static cw_node mk_dust(int i) {
  (void)i;
  return new_ps("Dust", 8, 0.9f, 0.4f, 1.6f, 55, 0.25f, 0.45f, "0 0.5 0.45 0.4 0.55, 1 0.45 0.4 0.36 0", "0 0.4, 0.4 1, 1 1.4", 1, "BILLBOARD", "ALPHA");
}
static cw_node mk_fire(int i) {
  (void)i;
  cw_node n = new_ps("Fire", 26, 0.65f, 1.2f, 6, 180, 1.2f, 1.6f, "0 1 0.95 0.7 1, 0.2 1 0.55 0.15 1, 0.7 0.35 0.1 0.04 0.7, 1 0.1 0.05 0.03 0", "0 0.5, 0.2 1, 1 1.3", 5, "BILLBOARD", "ADD");
  cw_node_set(n, "emission_shape", cw_val_enum("SPHERE"));
  cw_node_set(n, "emission_radius", cw_val_float(0.5));
  return n;
}
static cw_node mk_smoke(int i) {
  (void)i;
  cw_node n = new_ps("Smoke", 18, 2.4f, 0.6f, 3, 180, 0.9f, 2.0f, "0 0.25 0.22 0.2 0, 0.12 0.22 0.2 0.18 0.75, 1 0.1 0.1 0.1 0", "0 0.4, 1 1.5", 1, "BILLBOARD", "ALPHA");
  cw_node_set(n, "explosiveness", cw_val_float(0.6));
  cw_node_set(n, "emission_shape", cw_val_enum("SPHERE"));
  cw_node_set(n, "emission_radius", cw_val_float(0.6));
  return n;
}
static cw_node mk_ember(int i) {
  (void)i;
  cw_node n = new_ps("Ember", 40, 1.1f, 4, 13, 180, -9, 0.06f, "0 1 0.9 0.5 1, 0.5 1 0.45 0.1 1, 1 0.4 0.1 0.03 0", "0 1, 1 0.2", 8, "STRETCHED", "ADD");
  cw_node_set(n, "stretch", cw_val_float(0.08));
  cw_node_set(n, "collide_depth", cw_val_bool(true));
  cw_node_set(n, "bounce", cw_val_float(0.4));
  return n;
}
static cw_node mk_decal(int i) {
  (void)i;
  cw_node n = cw_node_new("Decal3D", "Hole");
  cw_node_set(n, "size", cw_val_vec3(0.28f, 0.28f, 0.35f));
  cw_node_set(n, "blend", cw_val_enum("MULTIPLY"));
  cw_node_set(n, "color", cw_val_color(0.12f, 0.1f, 0.09f, 1));
  cw_node_set(n, "normal_fade", cw_val_float(0.3));
  cw_node3d_set_position(n, v3(0, -500, 0));
  cw_node_add_child(ROOT, n);
  return n;
}
static cw_node mk_scorch(int i) {
  (void)i;
  cw_node n = cw_node_new("Decal3D", "Scorch");
  cw_node_set(n, "size", cw_val_vec3(3.2f, 3.2f, 1.6f));
  cw_node_set(n, "blend", cw_val_enum("MULTIPLY"));
  cw_node_set(n, "color", cw_val_color(0.08f, 0.07f, 0.06f, 1));
  cw_node_set(n, "normal_fade", cw_val_float(0.4));
  cw_node_set(n, "edge_fade", cw_val_float(0.3));
  cw_node3d_set_position(n, v3(0, -500, 0));
  cw_node_add_child(ROOT, n);
  return n;
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
static cw_node mk_elight(int i) {
  (void)i;
  cw_node n = cw_node_new("OmniLight3D", "Flash");
  cw_node_set(n, "color", cw_val_color(1, 0.6f, 0.25f, 1));
  cw_node_set(n, "energy", cw_val_float(0));
  cw_node_set(n, "range", cw_val_float(14));
  cw_node_add_child(ROOT, n);
  return n;
}

/* a short life for each tracer and explosion light, and the casings (CPU, visual only) */
typedef struct life { int t, t0; } life;
static life TR_LIFE[48], EL_LIFE[48];
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
SND(rifle, 0.16f, tone_into(p, n, rate, 190, 70, 0.7f, 0.9f, 9, 0.5f); tone_into(p, n, rate, 90, 60, 0.1f, 0.5f, 14, 1);)
SND(scatter, 0.4f, tone_into(p, n, rate, 120, 45, 0.85f, 1.2f, 6, 0.3f); tone_into(p, n, rate, 70, 40, 0.2f, 0.8f, 8, 1);)
SND(rocket, 0.5f, tone_into(p, n, rate, 300, 90, 0.8f, 0.8f, 4, 0.2f); tone_into(p, n, rate, 140, 80, 0.1f, 0.5f, 5, 1);)
SND(boom, 1.6f, tone_into(p, n, rate, 70, 28, 0.8f, 1.6f, 3.2f, 0.06f); tone_into(p, n, rate, 48, 24, 0.0f, 1.2f, 3, 1);)
SND(rail, 0.7f, tone_into(p, n, rate, 3200, 220, 0.1f, 0.6f, 4, 1); tone_into(p, n, rate, 90, 50, 0.6f, 0.7f, 8, 0.3f);)
SND(hit, 0.07f, tone_into(p, n, rate, 1500, 900, 0.1f, 0.5f, 8, 1);)
SND(hurt, 0.25f, tone_into(p, n, rate, 220, 90, 0.4f, 0.8f, 5, 0.4f);)
SND(pickup, 0.3f, tone_into(p, n, rate, 780, 1180, 0, 0.5f, 3, 1);)
SND(die, 0.8f, tone_into(p, n, rate, 260, 40, 0.5f, 0.9f, 3, 0.3f);)
SND(click, 0.05f, tone_into(p, n, rate, 600, 500, 0.4f, 0.5f, 10, 0.6f);)
SND(spawn, 0.5f, tone_into(p, n, rate, 300, 900, 0, 0.4f, 2, 1);)
SND(step, 0.1f, tone_into(p, n, rate, 90, 50, 0.9f, 0.5f, 7, 0.15f);)
SND(shell, 0.12f, tone_into(p, n, rate, 3400, 2600, 0.05f, 0.4f, 12, 1);)
SND(tick, 0.06f, tone_into(p, n, rate, 1100, 1100, 0, 0.4f, 6, 1);)

void fx_init(cw_node root) {
  ROOT = root;
  cw_rng_seed(&R, 99);
  fill(&P_SPARK, 12, mk_spark);
  fill(&P_HIT, 12, mk_hit);
  fill(&P_DUST, 10, mk_dust);
  fill(&P_FIRE, 5, mk_fire);
  fill(&P_SMOKE, 5, mk_smoke);
  fill(&P_EMBER, 5, mk_ember);
  fill(&P_DECAL, 48, mk_decal);
  fill(&P_SCORCH, 10, mk_scorch);
  fill(&P_TRAIL, 24, mk_trail);
  fill(&P_ELIGHT, 6, mk_elight);
  for (int i = 0; i < NCASE; i++) {
    cw_node n = cw_node_new("MeshInstance3D", "Casing");
    cw_node_set(n, "primitive", cw_val_enum("CYLINDER"));
    cw_node_set(n, "size", cw_val_vec3(0.025f, 0.07f, 0));
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
    S[SND_SHELL] = shell_m(), S[SND_TICK] = tick_m();
  }
}

void fx_reset(void) {
  /* the pools' nodes belong to the level root and were freed with it: fx_init again */
  memset(&P_SPARK, 0, sizeof P_SPARK), memset(&P_HIT, 0, sizeof P_HIT), memset(&P_DUST, 0, sizeof P_DUST);
  memset(&P_FIRE, 0, sizeof P_FIRE), memset(&P_SMOKE, 0, sizeof P_SMOKE), memset(&P_EMBER, 0, sizeof P_EMBER);
  memset(&P_DECAL, 0, sizeof P_DECAL), memset(&P_SCORCH, 0, sizeof P_SCORCH), memset(&P_TRAIL, 0, sizeof P_TRAIL);
  memset(&P_ELIGHT, 0, sizeof P_ELIGHT);
  memset(TR_LIFE, 0, sizeof TR_LIFE), memset(EL_LIFE, 0, sizeof EL_LIFE);
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
  cw_node d = take(&P_DECAL, NULL);
  cw_node3d_set_position(d, v3add(p, v3scale(n, 0.1f)));
  t3_vec3 up = fabsf(n.y) > 0.98f ? v3(1, 0, 0) : v3(0, 1, 0);
  cw_node3d_look_at(d, v3sub(p, n), up);
}

void fx_explosion(t3_vec3 p, t3_vec3 n) {
  t3_vec3 up = v3(0, 1, 0);
  cw_node f = take(&P_FIRE, NULL);
  aim_node(f, p, up);
  cw_particles_restart(f);
  cw_node s = take(&P_SMOKE, NULL);
  aim_node(s, p, up);
  cw_particles_restart(s);
  cw_node e = take(&P_EMBER, NULL);
  aim_node(e, p, up);
  cw_particles_restart(e);
  cw_node d = take(&P_DUST, NULL);
  aim_node(d, p, up);
  cw_particles_restart(d);
  cw_node sc = take(&P_SCORCH, NULL);
  t3_vec3 nn = n;
  if (v3len(nn) < 0.5f) nn = up;
  cw_node3d_set_position(sc, v3add(p, v3scale(nn, 0.15f)));
  cw_node3d_look_at(sc, v3sub(p, nn), fabsf(nn.y) > 0.98f ? v3(1, 0, 0) : up);
  int li;
  cw_node l = take(&P_ELIGHT, &li);
  cw_node3d_set_position(l, v3add(p, v3scale(nn, 0.8f)));
  EL_LIFE[li] = (life){ 16, 16 };
  fx_play(SND_BOOM, p, 1.0f, 0.95f + cw_rng_float(&R) * 0.1f);
}

void fx_tracer(t3_vec3 a, t3_vec3 b, int kind) {
  int i;
  cw_node n = take(&P_TRAIL, &i);
  t3_vec3 d = v3sub(b, a);
  float len = v3len(d);
  if (len < 0.1f) return;
  TR_LEN[i] = len;
  TR_W[i] = kind == 1 ? 0.07f : 0.025f;
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
  for (int i = 0; i < P_ELIGHT.cnt; i++) {
    float e = 0;
    if (EL_LIFE[i].t > 0) {
      EL_LIFE[i].t--;
      float k = (float)EL_LIFE[i].t / (float)EL_LIFE[i].t0;
      e = 160.0f * k * k;
    }
    cw_node_set(P_ELIGHT.n[i], "energy", cw_val_float(e));
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
