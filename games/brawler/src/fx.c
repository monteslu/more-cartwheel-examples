/* brawler effects: pooled GPU particle bursts, a pool of flash lights, and every sound synthesised in code. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/particles.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "game.h"

#define NSPARK 10
#define NDUST 6
#define NDEB 6
#define NFLASH 4
#define NBURST 6
#define NDECAL 18
static cw_node spark[NSPARK], dust[NDUST], deb[NDEB], flash[NFLASH], burst[NBURST], decal[NDECAL];
static int ns, nd, nb, nf, nbu, ndc;
static float flash_e[NFLASH];
static cw_node tree_root(void) { return cw_node_get(cw_tree_root(cw_scene_app_tree()), "Level"); }

static cw_node psys(const char *name) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_add_child(cw_node_get(tree_root(), "Fx"), n);
  return n;
}

void fx_init(void) {
  char nm[24];
  for (int i = 0; i < NSPARK; i++) {
    snprintf(nm, sizeof nm, "spark%d", i);
    cw_node n = spark[i] = psys(nm);
    cw_node_set(n, "amount", cw_val_int(48));
    cw_node_set(n, "lifetime", cw_val_float(0.45));
    cw_node_set(n, "one_shot", cw_val_bool(true));
    cw_node_set(n, "emitting", cw_val_bool(false));
    cw_node_set(n, "explosiveness", cw_val_float(1));
    cw_node_set(n, "emission_shape", cw_val_enum("POINT"));
    cw_node_set(n, "direction", cw_val_vec3(0, 0.4f, 0));
    cw_node_set(n, "spread", cw_val_float(180));
    cw_node_set(n, "speed_min", cw_val_float(2.5));
    cw_node_set(n, "speed_max", cw_val_float(8));
    cw_node_set(n, "gravity", cw_val_vec3(0, -12, 0));
    cw_node_set(n, "damping", cw_val_float(1.2f));
    cw_node_set(n, "draw_mode", cw_val_enum("STRETCHED"));
    cw_node_set(n, "stretch", cw_val_float(0.05));
    cw_node_set(n, "size", cw_val_float(0.07));
    cw_node_set(n, "blend", cw_val_enum("ADD"));
    cw_node_set(n, "energy", cw_val_float(6));
    cw_node_set(n, "color", cw_val_color(1, 0.7f, 0.3f, 1));
    cw_node_set(n, "color_ramp", cw_val_string("0 1 0.95 0.7 1, 0.3 1 0.55 0.2 1, 1 0.6 0.15 0.05 0"));
    cw_node_set(n, "size_curve", cw_val_string("0 1, 1 0.1"));
    cw_node_set(n, "seed", cw_val_int(100 + i));
  }
  for (int i = 0; i < NDUST; i++) {
    snprintf(nm, sizeof nm, "dust%d", i);
    cw_node n = dust[i] = psys(nm);
    cw_node_set(n, "amount", cw_val_int(18));
    cw_node_set(n, "lifetime", cw_val_float(0.9));
    cw_node_set(n, "one_shot", cw_val_bool(true));
    cw_node_set(n, "emitting", cw_val_bool(false));
    cw_node_set(n, "explosiveness", cw_val_float(0.9f));
    cw_node_set(n, "emission_shape", cw_val_enum("SPHERE"));
    cw_node_set(n, "emission_radius", cw_val_float(0.3));
    cw_node_set(n, "direction", cw_val_vec3(0, 0.3f, 0));
    cw_node_set(n, "spread", cw_val_float(90));
    cw_node_set(n, "speed_min", cw_val_float(0.6));
    cw_node_set(n, "speed_max", cw_val_float(2.4));
    cw_node_set(n, "gravity", cw_val_vec3(0, 0.3f, 0));
    cw_node_set(n, "damping", cw_val_float(1.6f));
    cw_node_set(n, "size", cw_val_float(0.6));
    cw_node_set(n, "blend", cw_val_enum("ALPHA"));
    cw_node_set(n, "color", cw_val_color(0.55f, 0.5f, 0.45f, 1));
    cw_node_set(n, "color_ramp", cw_val_string("0 0.6 0.55 0.5 0.0, 0.15 0.6 0.55 0.5 0.55, 1 0.5 0.47 0.44 0"));
    cw_node_set(n, "size_curve", cw_val_string("0 0.3, 1 1"));
    cw_node_set(n, "spin", cw_val_float(1.5));
    cw_node_set(n, "seed", cw_val_int(300 + i));
  }
  for (int i = 0; i < NDEB; i++) {
    snprintf(nm, sizeof nm, "deb%d", i);
    cw_node n = deb[i] = psys(nm);
    cw_node_set(n, "amount", cw_val_int(22));
    cw_node_set(n, "lifetime", cw_val_float(1.0));
    cw_node_set(n, "lifetime_random", cw_val_float(0.4));
    cw_node_set(n, "one_shot", cw_val_bool(true));
    cw_node_set(n, "emitting", cw_val_bool(false));
    cw_node_set(n, "explosiveness", cw_val_float(1));
    cw_node_set(n, "emission_shape", cw_val_enum("SPHERE"));
    cw_node_set(n, "emission_radius", cw_val_float(0.25));
    cw_node_set(n, "direction", cw_val_vec3(0, 1, 0));
    cw_node_set(n, "spread", cw_val_float(70));
    cw_node_set(n, "speed_min", cw_val_float(2));
    cw_node_set(n, "speed_max", cw_val_float(6));
    cw_node_set(n, "gravity", cw_val_vec3(0, -14, 0));
    cw_node_set(n, "draw_mode", cw_val_enum("MESH"));
    cw_node_set(n, "mesh_primitive", cw_val_enum("BOX"));
    cw_node_set(n, "blend", cw_val_enum("OPAQUE"));
    cw_node_set(n, "size", cw_val_float(0.07));
    cw_node_set(n, "size_random", cw_val_float(0.6));
    cw_node_set(n, "size_curve", cw_val_string("0 1, 0.6 0.85, 1 0")); /* chips shrink away, never pop out */
    cw_node_set(n, "spin", cw_val_float(9));
    cw_node_set(n, "seed", cw_val_int(500 + i));
  }
  /* a star of light at every connected hit: one billboard that lives a few frames */
  for (int i = 0; i < NBURST; i++) {
    snprintf(nm, sizeof nm, "burst%d", i);
    cw_node n = burst[i] = psys(nm);
    cw_node_set(n, "amount", cw_val_int(1));
    cw_node_set(n, "lifetime", cw_val_float(0.14));
    cw_node_set(n, "one_shot", cw_val_bool(true));
    cw_node_set(n, "emitting", cw_val_bool(false));
    cw_node_set(n, "explosiveness", cw_val_float(1));
    cw_node_set(n, "emission_shape", cw_val_enum("POINT"));
    cw_node_set(n, "speed_min", cw_val_float(0));
    cw_node_set(n, "speed_max", cw_val_float(0));
    cw_node_set(n, "texture", cw_val_string("decals/burst.png"));
    cw_node_set(n, "blend", cw_val_enum("ADD"));
    cw_node_set(n, "size", cw_val_float(0.9f));
    cw_node_set(n, "energy", cw_val_float(5));
    cw_node_set(n, "color", cw_val_color(1, 0.85f, 0.55f, 1));
    cw_node_set(n, "color_ramp", cw_val_string("0 1 0.95 0.8 1, 0.5 1 0.7 0.35 0.8, 1 1 0.4 0.1 0"));
    cw_node_set(n, "size_curve", cw_val_string("0 0.5, 0.3 1, 1 1.3"));
    cw_node_set(n, "spin", cw_val_float(0));
    cw_node_set(n, "seed", cw_val_int(700 + i));
  }
  /* scuffs and cracks stamped on the ground by hard hits and knockdowns (multiplied onto the lit street) */
  static const char *DEC[3] = { "decals/scuff1.png", "decals/scuff2.png", "decals/crack.png" };
  for (int i = 0; i < NDECAL; i++) {
    snprintf(nm, sizeof nm, "decal%d", i);
    cw_node d = decal[i] = cw_node_new("Decal3D", nm);
    cw_node_add_child(cw_node_get(tree_root(), "Fx"), d);
    cw_node_set(d, "texture", cw_val_string(DEC[i % 3]));
    cw_node_set(d, "blend", cw_val_enum("MULTIPLY"));
    cw_node_set(d, "size", cw_val_vec3(0.01f, 0.01f, 0.01f));
    cw_node_set(d, "color", cw_val_color(1, 1, 1, 0));
    cw_node3d_set_rotation(d, (t3_vec3){ -1.5708f, 0, 0 });
    cw_node3d_set_position(d, (t3_vec3){ 0, -20, 0 });
  }
  for (int i = 0; i < NFLASH; i++) {
    snprintf(nm, sizeof nm, "flash%d", i);
    cw_node n = flash[i] = cw_node_new("OmniLight3D", nm);
    cw_node_add_child(cw_node_get(tree_root(), "Fx"), n);
    cw_node_set(n, "energy", cw_val_float(0));
    cw_node_set(n, "range", cw_val_float(5));
  }
}

/* danger rings: a red ring added to the street under a heavy attack (k is its brightness, 0 hides it) */
enum { NWARN = 8 };
static cw_node warn[NWARN];
static float warn_k[NWARN];
void fx_warn(int slot, float x, float z, float sx, float sz, float k) {
  if (slot < 0 || slot >= NWARN) return;
  cw_node d = warn[slot];
  if (!d.id || !cw_node_valid(d)) {
    if (k <= 0) return;
    char nm[16];
    snprintf(nm, sizeof nm, "warn%d", slot);
    d = warn[slot] = cw_node_new("Decal3D", nm);
    cw_node_add_child(cw_node_get(tree_root(), "Fx"), d);
    cw_node_set(d, "texture", cw_val_string("decals/ring.png"));
    cw_node_set(d, "blend", cw_val_enum("ADD"));
    cw_node3d_set_rotation(d, (t3_vec3){ -1.5708f, 0, 0 });
    warn_k[slot] = -1;
  }
  if (k <= 0) {
    if (warn_k[slot] != 0) { cw_node_set(d, "size", cw_val_vec3(0.01f, 0.01f, 0.01f)); cw_node3d_set_position(d, (t3_vec3){ 0, -20, 0 }); warn_k[slot] = 0; }
    return;
  }
  cw_node3d_set_position(d, (t3_vec3){ x, 0.2f, z });
  cw_node_set(d, "size", cw_val_vec3(sx * 2, sz * 2, 0.5f));
  cw_node_set(d, "color", cw_val_color(1, 0.22f, 0.08f, k));
  warn_k[slot] = k;
}

/* a mark on the street where something heavy landed */
static void fx_mark(float x, float z, float size) {
  cw_node d = decal[ndc++ % NDECAL];
  float yaw = (float)((ndc * 2654435761u) & 0xffff) / 65535.0f * 6.28318f;
  cw_node3d_set_position(d, (t3_vec3){ x, 0.18f, z });
  cw_node3d_set_rotation(d, (t3_vec3){ -1.5708f, 0, yaw });
  cw_node_set(d, "size", cw_val_vec3(size, size, 0.36f));
  cw_node_set(d, "color", cw_val_color(1, 1, 1, 0.5f));
}
void fx_hit(float x, float y, float z, int kind, int face) {
  { cw_node b = burst[nbu++ % NBURST]; cw_node3d_set_position(b, (t3_vec3){ x, y, z + 0.3f }); cw_node_set(b, "size", cw_val_float(kind >= 2 ? 1.5f : 0.9f)); cw_particles_restart(b); }
  if (kind >= 2) fx_mark(x, z, 0.75f);
  cw_node n = spark[ns++ % NSPARK];
  cw_node3d_set_position(n, (t3_vec3){ x, y, z });
  cw_node_set(n, "amount", cw_val_int(kind >= 2 ? 64 : 28));
  cw_node_set(n, "direction", cw_val_vec3((float)face * 0.8f, 0.3f, 0.2f));
  cw_node_set(n, "spread", cw_val_float(kind >= 2 ? 110 : 70));
  cw_particles_restart(n);
  if (kind >= 2) {
    cw_node dn = dust[nd++ % NDUST];
    cw_node3d_set_position(dn, (t3_vec3){ x, y - 0.4f, z });
    cw_particles_restart(dn);
  }
  fx_flash(x, y, z, 1, 0.8f, 0.5f, kind >= 2 ? 16 : 9);
}
void fx_dust(float x, float z, float scale) {
  if (scale > 1.0f) fx_mark(x, z, 0.45f * scale);
  cw_node n = dust[nd++ % NDUST];
  cw_node3d_set_position(n, (t3_vec3){ x, 0.15f, z });
  cw_node_set(n, "emission_radius", cw_val_float(0.25f * scale));
  cw_node_set(n, "size", cw_val_float(0.45f * scale));
  cw_particles_restart(n);
}
void fx_debris(float x, float y, float z, uint32_t color) {
  cw_node n = deb[nb++ % NDEB];
  cw_node3d_set_position(n, (t3_vec3){ x, y, z });
  /* particles are unlit: a chip in the prop's full colour glowed against the dim street like a lamp, so it is shaded
     down to roughly what the street's light makes of that colour */
  const float k = 0.3f / 255.0f;
  cw_node_set(n, "color", cw_val_color((float)((color >> 16) & 255) * k, (float)((color >> 8) & 255) * k, (float)(color & 255) * k, 1));
  cw_particles_restart(n);
}
void fx_flash(float x, float y, float z, float r, float g, float b, float e) {
  int i = nf++ % NFLASH;
  cw_node3d_set_position(flash[i], (t3_vec3){ x, y + 0.3f, z + 0.8f });
  cw_node_set(flash[i], "color", cw_val_color(r, g, b, 1));
  flash_e[i] = e * 4; /* bright enough to light the faces near a hit, not to burn them white */
  cw_node_set(flash[i], "energy", cw_val_float(flash_e[i]));
}
void fx_tick(void) {
  for (int i = 0; i < NFLASH; i++)
    if (flash_e[i] > 0) {
      flash_e[i] *= 0.62f;
      if (flash_e[i] < 0.5f) flash_e[i] = 0;
      cw_node_set(flash[i], "energy", cw_val_float(flash_e[i]));
    }
}

/* ── sound: everything is made here ─────────────────────────────────── */
static cw_sound SND[SFX_COUNT];
static uint32_t rs = 12345;
static float noise(void) { rs = rs * 1664525u + 1013904223u; return (float)(rs >> 8) / 8388608.0f - 1.0f; }

static cw_sound make(float secs, float f0, float f1, float noise_mix, float gain, float decay) {
  enum { RATE = 48000 };
  static float pcm[RATE * 2];
  int n = (int)(secs * RATE);
  if (n > RATE * 2) n = RATE * 2;
  double ph = 0;
  float lp = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / (float)n;
    ph += 2 * 3.14159265 * (f0 + (f1 - f0) * t) / RATE;
    lp += (noise() - lp) * 0.35f;
    float env = expf(-t * decay) * (t < 0.01f ? t / 0.01f : 1);
    pcm[i] = ((float)sin(ph) * (1 - noise_mix) + lp * noise_mix) * gain * env;
  }
  return cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
}

/* a voiced "ayyy" with a swagger: a buzzy glottal source gliding up and settling, through two vowel resonances that move
   from an open "a" to an "ee" (a two-pole filter each), with a little vibrato at the end; a finger snap first */
static cw_sound make_swagger(void) {
  enum { RATE = 48000 };
  static float pcm[RATE];
  int n = (int)(0.85f * RATE);
  double ph = 0;
  float y1[2][2] = { { 0, 0 }, { 0, 0 } };
  for (int i = 0; i < n; i++) {
    float t = (float)i / RATE, u = t / 0.85f;
    float f0 = 118 + 70 * (u < 0.35f ? u / 0.35f : 1 - 0.35f * (u - 0.35f) / 0.65f) + (u > 0.55f ? 5 * sinf(6.2831853f * 5.5f * t) : 0);
    ph += f0 / RATE;
    float src = (float)(ph - floor(ph)) * 2 - 1; /* a buzz */
    src = src * 0.7f + noise() * 0.04f;
    float F[2] = { 720 - 360 * u, 1250 + 1050 * u }, BW[2] = { 90, 130 }, out = 0;
    for (int k = 0; k < 2; k++) {
      float r = expf(-3.14159265f * BW[k] / RATE), c = 2 * r * cosf(6.2831853f * F[k] / RATE);
      float y = (1 - r) * src + c * y1[k][0] - r * r * y1[k][1];
      y1[k][1] = y1[k][0], y1[k][0] = y;
      out += y * (k ? 0.6f : 1.0f);
    }
    float env = (t < 0.04f ? t / 0.04f : 1) * (u > 0.7f ? 1 - (u - 0.7f) / 0.3f : 1);
    pcm[i] = out * 0.9f * env;
    if (t < 0.012f) pcm[i] += noise() * 0.6f * (1 - t / 0.012f); /* the snap */
  }
  return cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
}
void audio_init(void) {
  SND[SFX_HIT1] = make(0.12f, 180, 70, 0.6f, 0.7f, 7);
  SND[SFX_HIT2] = make(0.12f, 220, 90, 0.5f, 0.7f, 7);
  SND[SFX_HIT3] = make(0.3f, 120, 40, 0.5f, 0.95f, 5);
  SND[SFX_WHIFF] = make(0.14f, 900, 300, 0.9f, 0.3f, 6);
  SND[SFX_BREAK] = make(0.3f, 300, 80, 0.95f, 0.7f, 6);
  SND[SFX_PICK] = make(0.14f, 700, 1400, 0.0f, 0.4f, 4);
  SND[SFX_KO] = make(0.5f, 200, 50, 0.4f, 0.8f, 4);
  SND[SFX_THROW] = make(0.3f, 400, 150, 0.7f, 0.5f, 4);
  SND[SFX_SLAM] = make(0.6f, 70, 30, 0.35f, 1.0f, 4);
  SND[SFX_GO] = make(0.4f, 500, 900, 0.0f, 0.4f, 3);
  SND[SFX_HEAL] = make(0.3f, 520, 1040, 0.0f, 0.4f, 3);
  SND[SFX_THUMP] = make(0.5f, 95, 42, 0.45f, 1.0f, 5); /* a fist on the jukebox cabinet */
  SND[SFX_SWAGGER] = make_swagger();
}
void sfx(int id) {
  if (!SND[id].id) return;
  cw_audio_play(SND[id], &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = 0.8f });
}

/* a looping street beat per stage: kick, snare, bass line, all computed */
cw_sound music_theme(void);
cw_sound music_make(int stage) {
  if (stage == 4) return music_theme();
  enum { RATE = 32000 };
  static const float bass[4][8] = { { 55, 55, 65.4f, 55, 73.4f, 55, 65.4f, 49 }, { 49, 49, 58.3f, 49, 65.4f, 49, 58.3f, 43.7f }, { 65.4f, 65.4f, 77.8f, 73.4f, 87.3f, 65.4f, 77.8f, 58.3f }, { 58.3f, 58.3f, 69.3f, 58.3f, 77.8f, 69.3f, 87.3f, 51.9f } };
  float bpm = 112.0f + (float)stage * 6;
  float beat = 60.0f / bpm;
  int n = (int)(beat * 16 * RATE);
  float *pcm = cw_alloc((size_t)n * sizeof(float), CW_MEM_GAME);
  memset(pcm, 0, (size_t)n * sizeof(float));
  for (int s = 0; s < 32; s++) { /* 32 eighth notes */
    int at = (int)((float)s * beat * 0.5f * RATE);
    float f = bass[stage & 3][(s / 2) & 7];
    for (int i = 0; i < (int)(beat * 0.45f * RATE) && at + i < n; i++) {
      float t = (float)i / RATE;
      float saw = fmodf(t * f, 1.0f) * 2 - 1;
      pcm[at + i] += saw * 0.16f * expf(-t * 5);
    }
    if (s % 4 == 0) for (int i = 0; i < (int)(0.18f * RATE) && at + i < n; i++) { float t = (float)i / RATE; pcm[at + i] += sinf(6.28318f * (110.0f * expf(-t * 18) + 40) * t) * 0.5f * expf(-t * 14); }
    if (s % 8 == 4) for (int i = 0; i < (int)(0.16f * RATE) && at + i < n; i++) { float t = (float)i / RATE; pcm[at + i] += noise() * 0.25f * expf(-t * 22); }
    if (s % 2 == 1) for (int i = 0; i < (int)(0.04f * RATE) && at + i < n; i++) { float t = (float)i / RATE; pcm[at + i] += noise() * 0.08f * expf(-t * 80); }
  }
  cw_sound s = cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
  cw_free(pcm);
  return s;
}

/* Arthur's theme (the jukebox): a 1950s rock and roll shuffle, all computed. A twelve bar blues in A at 152 beats a
   minute with swung eighths: a boogie bass line, kick on one and three, a snapping backbeat, a swung hi-hat, piano stabs
   after the backbeat and a reedy lead riff answering itself every four bars. */
static void tone(float *pcm, int n, int at, float f, float secs, float amp, float decay, int kind, int rate) {
  double ph = 0;
  float lp = 0;
  for (int i = 0; i < (int)(secs * (float)rate) && at + i < n; i++) {
    if (at + i < 0) continue;
    float t = (float)i / (float)rate, env = expf(-t * decay) * (t < 0.006f ? t / 0.006f : 1), v;
    ph += f * (1 + (kind == 2 ? 0.006f * sinf(6.2831853f * 5.2f * t) * (t > 0.12f ? 1 : 0) : 0)) / rate;
    float x = (float)(ph - floor(ph));
    if (kind == 0) v = 4 * fabsf(x - 0.5f) - 1;                                  /* bass: triangle */
    else if (kind == 1) v = sinf(6.2831853f * x) + 0.4f * sinf(12.566371f * x) + 0.15f * sinf(18.849556f * x); /* piano-ish */
    else { lp += ((x * 2 - 1) - lp) * 0.18f; v = lp; }                          /* the lead: a soft saw, a reed */
    pcm[at + i] += v * amp * env;
  }
}
cw_sound music_theme(void) {
  enum { RATE = 32000 };
  const float bpm = 152, beat = 60.0f / bpm;
  const int bars = 12, n = (int)(beat * 4 * bars * RATE);
  float *pcm = cw_alloc((size_t)n * sizeof(float), CW_MEM_GAME);
  memset(pcm, 0, (size_t)n * sizeof(float));
  static const int PROG[12] = { 0, 0, 0, 0, 5, 5, 0, 0, 7, 5, 0, 7 }; /* I I I I IV IV I I V IV I V, in semitones over A */
  static const int BOOGIE[8] = { 0, 4, 7, 9, 10, 9, 7, 4 };
  static const int RIFF[6] = { 12, 15, 16, 19, 16, 12 }; /* a blue third bent up to the major, the fifth, back home */
  const float A1 = 55.0f;
#define AT(beat_i, swing) ((int)(((float)(beat_i) + ((swing) ? 0.6667f : 0.0f)) * beat * RATE))
  for (int b = 0; b < bars; b++) {
    float root = A1 * powf(2.0f, (float)PROG[b] / 12.0f);
    for (int q = 0; q < 4; q++) {
      int bi = b * 4 + q;
      for (int e = 0; e < 2; e++) { /* the boogie bass, the hat */
        tone(pcm, n, AT(bi, e), root * powf(2.0f, (float)BOOGIE[q * 2 + e] / 12.0f), beat * 0.6f, 0.34f, 5.5f, 0, RATE);
        for (int i = 0; i < (int)(0.035f * RATE); i++) { int k = AT(bi, e) + i; if (k < n) pcm[k] += noise() * (e ? 0.05f : 0.08f) * expf(-(float)i / RATE * 90); }
      }
      if (q == 0 || q == 2) for (int i = 0; i < (int)(0.2f * RATE); i++) { int k = AT(bi, 0) + i; float t = (float)i / RATE; if (k < n) pcm[k] += sinf(6.2831853f * (95.0f * expf(-t * 20) + 45) * t) * 0.55f * expf(-t * 12); }
      if (q == 1 || q == 3) {
        for (int i = 0; i < (int)(0.18f * RATE); i++) { int k = AT(bi, 0) + i; float t = (float)i / RATE; if (k < n) pcm[k] += (noise() * 0.32f + sinf(6.2831853f * 190 * t) * 0.15f) * expf(-t * 20); }
        static const int CH[4] = { 0, 4, 7, 9 }; /* the stab after the backbeat: a sixth chord */
        for (int c = 0; c < 4; c++) tone(pcm, n, AT(bi, 1), root * 8 * powf(2.0f, (float)CH[c] / 12.0f), beat * 0.4f, 0.05f, 9, 1, RATE);
      }
    }
    if (b % 4 == 0 || b == 9) for (int r = 0; r < 6; r++) /* the lead riff over the first bar of each line */
      tone(pcm, n, AT(b * 4 + r / 2, r & 1), root * 4 * powf(2.0f, (float)RIFF[r] / 12.0f), beat * (r == 5 ? 1.4f : 0.55f), 0.11f, r == 5 ? 2.2f : 4.0f, 2, RATE);
  }
#undef AT
  for (int i = 0; i < n; i++) pcm[i] = tanhf(pcm[i] * 1.1f) * 0.85f; /* a little tape warmth, no clipping */
  cw_sound s = cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
  cw_free(pcm);
  return s;
}
