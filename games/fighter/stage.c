/* fighter stages: the modelled background of each stage (models/stageN.glb, made by tools/make-world.mjs), its
 * ambient life (fans turning, aircraft lights blinking, lanterns swaying, neon flickering, rain, steam, petals, the
 * crowd) and the rim lights that stand behind each fighter. Render only: nothing here reads input or feeds the rules;
 * time comes from the simulation tick so every host shows the same frame. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

#define MAXPART 32
typedef struct anim_part { t3_object *o; int kind; float phase; t3_quat rest; } anim_part;
enum { PK_FAN, PK_BLINK, PK_LAMP, PK_SIGN };

static struct {
  int stage;
  cw_node node, rim[2], neon[4];
  float neon_e[4];
  t3_gltf *gltf;
  anim_part part[MAXPART];
  int nparts;
  cw_node fx[4];
  int nfx;
  crowd crowd;
} S;

static void shadows(t3_object *o, bool cast) {
  if (o->type == T3_MESH || o->type == T3_SKINNED_MESH) { o->cast_shadow = cast; o->receive_shadow = true; }
  for (int i = 0; i < o->child_count; i++) shadows(o->children[i], cast);
}

static cw_node particles(const char *name, const char *const *kv, int seed, t3_vec3 at) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_add_child(S.node, n);
  for (int i = 0; kv[i]; i += 2) {
    cw_value v;
    const cw_prop *p = cw_node_find_prop(n, kv[i], NULL);
    if (p && p->type == CW_T_ENUM) { cw_node_set(n, kv[i], cw_val_enum(kv[i + 1])); continue; }
    if (!p || !cw_value_parse(kv[i + 1], p->type, &v)) { cw_warn("fighter: bad particle setting %s = %s", kv[i], kv[i + 1]); continue; }
    cw_node_set(n, kv[i], v);
  }
  cw_node_set(n, "seed", cw_val_int(seed));
  cw_node3d_set_position(n, at);
  return n;
}

/* rain over the alley: streaks falling past the lights, and a fine mist where they land */
static const char *const RAIN[] = { "amount", "900", "lifetime", "0.75", "emission_shape", "BOX", "emission_extents", "vec3(13, 0.2, 6)", "direction", "vec3(0.12, -1, 0)",
  "spread", "3", "speed_min", "15", "speed_max", "19", "gravity", "vec3(0, -4, 0)", "draw_mode", "STRETCHED", "stretch", "0.035", "size", "0.012",
  "color_ramp", "\"0 0.7 0.75 0.9 0, 0.15 0.7 0.75 0.9 0.35, 1 0.7 0.75 0.9 0.35\"", "energy", "1.2", "blend", "ADD", "local_coords", "false", NULL };
static const char *const STEAM[] = { "amount", "28", "lifetime", "3.2", "lifetime_random", "0.3", "emission_shape", "BOX", "emission_extents", "vec3(0.35, 0.02, 0.35)",
  "direction", "vec3(0.1, 1, 0.05)", "spread", "14", "speed_min", "0.5", "speed_max", "1.0", "gravity", "vec3(0.12, 0.25, 0)", "damping", "0.3", "size", "0.9", "size_random", "0.4",
  "size_curve", "\"0 0.3, 0.5 1.1, 1 1.9\"", "color_ramp", "\"0 0.65 0.65 0.75 0, 0.2 0.65 0.65 0.75 0.18, 1 0.65 0.65 0.75 0\"", "blend", "ALPHA", "soft_distance", "0.5",
  "turbulence", "0.6", "turbulence_scale", "0.6", "local_coords", "false", NULL };
static const char *const PETALS[] = { "amount", "90", "lifetime", "7", "lifetime_random", "0.3", "emission_shape", "BOX", "emission_extents", "vec3(6, 0.5, 3)",
  "direction", "vec3(1, -0.3, 0.4)", "spread", "40", "speed_min", "0.4", "speed_max", "1.0", "gravity", "vec3(0.35, -0.45, 0.1)", "damping", "0.4",
  "draw_mode", "MESH", "mesh_primitive", "BOX", "size", "0.035", "size_random", "0.3", "spin", "4", "blend", "OPAQUE",
  "color_ramp", "\"0 1 0.72 0.8 1, 1 0.95 0.62 0.72 1\"", "turbulence", "1.2", "turbulence_scale", "0.4", "local_coords", "false", NULL };
static const char *const MOTES[] = { "amount", "60", "lifetime", "6", "emission_shape", "BOX", "emission_extents", "vec3(10, 2, 4)", "direction", "vec3(1, 0.2, 0)",
  "spread", "60", "speed_min", "0.05", "speed_max", "0.25", "gravity", "vec3(0.05, 0.02, 0)", "size", "0.02", "color_ramp", "\"0 1 0.8 0.5 0, 0.5 1 0.8 0.5 0.6, 1 1 0.8 0.5 0\"",
  "energy", "2", "blend", "ADD", "turbulence", "0.4", "turbulence_scale", "0.8", "local_coords", "false", NULL };

void stage_load(int stage, cw_node parent) {
  stage_free();
  memset(&S, 0, sizeof S);
  S.stage = stage;
  S.node = cw_node_new("Node3D", "Scenery");
  cw_node_add_child(parent, S.node);
  char path[48];
  snprintf(path, sizeof path, "models/stage%d.glb", stage + 1);
  size_t len = 0;
  void *glb = cw_asset_load(path, &len);
  if (!glb) { cw_error("fighter: %s missing from the assets", path); return; }
  S.gltf = t3_gltf_parse(glb, len, NULL, NULL);
  free(glb);
  if (!S.gltf) { cw_error("fighter: %s did not parse", path); return; }
  shadows(S.gltf->scene, true);
  cw_node3d_set_object(S.node, S.gltf->scene);
  /* the animated pieces, found by name */
  t3_object *sc = S.gltf->scene;
  for (int i = 0; i < sc->child_count && S.nparts < MAXPART; i++) {
    t3_object *o = sc->children[i];
    const char *nm = o->name ? o->name : "";
    int kind = !strncmp(nm, "Fan", 3) ? PK_FAN : !strncmp(nm, "Blink", 5) ? PK_BLINK : !strncmp(nm, "Lamp", 4) ? PK_LAMP : !strncmp(nm, "Sign", 4) ? PK_SIGN : -1;
    if (kind < 0) continue;
    anim_part *p = &S.part[S.nparts++];
    p->o = o; p->kind = kind; p->rest = o->quaternion;
    p->phase = (float)(S.nparts * 37 % 100) * 0.0628f;
    if (kind == PK_BLINK || kind == PK_SIGN) shadows(o, false);
  }
  for (int i = 0; i < 2; i++) { char nm[8]; snprintf(nm, sizeof nm, "Rim%d", i); S.rim[i] = cw_node_get(parent, nm); }
  for (int i = 0; i < 4; i++) {
    char nm[12]; snprintf(nm, sizeof nm, "NeonL%d", i);
    S.neon[i] = cw_node_get(parent, nm);
    cw_value v = { 0 };
    S.neon_e[i] = cw_node_valid(S.neon[i]) && cw_node_get_prop(S.neon[i], "energy", &v) ? (float)v.f : 0;
  }
  /* ambient particles per stage */
  if (stage == 1) {
    S.fx[S.nfx++] = particles("Rain", RAIN, 301, (t3_vec3){ 0, 11, -1 });
    S.fx[S.nfx++] = particles("Steam", STEAM, 302, (t3_vec3){ -9.4, 0.02, -3.6 });
    S.fx[S.nfx++] = particles("Steam2", STEAM, 303, (t3_vec3){ -13, 6.4, -5.0 });
  } else if (stage == 2) {
    S.fx[S.nfx++] = particles("Petals", PETALS, 311, (t3_vec3){ -6, 5.5, -2.5 });
  } else {
    S.fx[S.nfx++] = particles("Motes", MOTES, 321, (t3_vec3){ 0, 2.2, -2 });
  }
  crowd_load(&S.crowd, stage, S.node);
}

void stage_free(void) {
  crowd_free(&S.crowd);
  if (S.node.id && cw_node_valid(S.node)) cw_node_free(S.node);
  S.node = (cw_node){ 0 };
  if (S.gltf) t3_release(S.gltf);
  S.gltf = NULL;
  S.nparts = 0;
}

static bool showcase;
void stage_set_showcase(bool on) { showcase = on; }

static float hash01(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return (float)(x & 0xffff) / 65535.0f; }

void stage_frame(const ft_game *g, float dt) {
  (void)dt;
  if (!S.gltf) return;
  float t = (float)g->tick / 60.0f;
  for (int i = 0; i < S.nparts; i++) {
    anim_part *p = &S.part[i];
    switch (p->kind) {
    case PK_FAN: { float s, c; cw_sincos(t * 6.0f + p->phase, &s, &c); p->o->quaternion = (t3_quat){ 0, s, 0, c }; break; }
    case PK_BLINK: { float k = (float)((g->tick + (unsigned)(p->phase * 40)) % 90) < 12 ? 1.0f : 0.0f; p->o->visible = k > 0; break; }
    case PK_LAMP: { /* a slow sway, stirred by the wind of the fight */
      float a = 0.06f * (float)cw_sin(t * 1.3f + p->phase) + 0.03f * (float)cw_sin(t * 2.9f + p->phase * 2), b = 0.04f * (float)cw_sin(t * 1.1f + p->phase * 3);
      float sa, ca, sb, cb;
      cw_sincos(a * 0.5f, &sa, &ca); cw_sincos(b * 0.5f, &sb, &cb);
      p->o->quaternion = t3_quat_multiply((t3_quat){ 0, 0, sa, ca }, (t3_quat){ sb, 0, 0, cb });
      break;
    }
    case PK_SIGN: { /* now and then a sign stutters off for a few frames */
      uint32_t slot = (g->tick / 8u) + (uint32_t)(p->phase * 100);
      bool off = hash01(slot * 2654435761u + (uint32_t)i) < 0.04f;
      p->o->visible = !off;
      int li = i % 4;
      if (S.stage == 1 && cw_node_valid(S.neon[li])) cw_node_set(S.neon[li], "energy", cw_val_float(off ? S.neon_e[li] * 0.15f : S.neon_e[li]));
      break;
    }
    }
  }
  /* rim lights: behind and above each fighter, so their outline catches light against the stage */
  for (int i = 0; i < 2; i++) {
    if (!cw_node_valid(S.rim[i])) continue;
    const ft_fighter *f = &g->f[i];
    if (showcase) cw_node3d_set_position(S.rim[i], (t3_vec3){ (float)f->x / 1000.0f + (float)f->facing * 0.6f, 1.9f, 1.3f }); /* a key light in front of the face */
    else cw_node3d_set_position(S.rim[i], (t3_vec3){ (float)f->x / 1000.0f - (float)f->facing * 0.35f, 2.3f + (float)f->y / 1000.0f, -1.7f });
  }
  crowd_frame(&S.crowd, g, dt);
}
