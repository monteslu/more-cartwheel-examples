/* Helpers for the 3D mini-games: build scene nodes in code, drive the
 * camera, and project a world point onto the 2D stage for labels. */
#include "mg.h"
#include "cartwheel/resource.h"
#include "three.h"

static float CAM_P[3], CAM_T[3], CAM_FOV = 55;

cw_node n3_level(void) { return cw_node_get(cw_tree_root(cw_scene_app_tree()), "Level"); }
cw_node n3_find(const char *name) { return cw_node_get(n3_level(), name); }

cw_node n3_prim(cw_node parent, const char *name, const char *primitive, t3_vec3 pos, t3_vec3 size, cw_color col) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_set(n, "primitive", cw_val_enum(primitive));
  cw_node_set(n, "size", cw_val_vec3(size.x, size.y, size.z));
  cw_node_set(n, "color", cw_val_color(col.r, col.g, col.b, col.a));
  cw_node_set(n, "cast_shadow", cw_val_bool(true));
  cw_node_set(n, "receive_shadow", cw_val_bool(true));
  cw_node_add_child(parent, n);
  cw_node3d_set_position(n, pos);
  return n;
}
cw_node n3_box(cw_node parent, const char *name, t3_vec3 pos, t3_vec3 size, cw_color col) { return n3_prim(parent, name, "BOX", pos, size, col); }
void n3_color(cw_node n, cw_color c) { cw_node_set(n, "color", cw_val_color(c.r, c.g, c.b, c.a)); }
void n3_emissive(cw_node n, cw_color c) { cw_node_set(n, "emissive", cw_val_color(c.r, c.g, c.b, c.a)); }
void n3_pos(cw_node n, float x, float y, float z) { cw_node3d_set_position(n, (t3_vec3){ x, y, z }); }
void n3_rot(cw_node n, float rx, float ry, float rz) { cw_node3d_set_rotation(n, (t3_vec3){ rx, ry, rz }); }
void n3_scale(cw_node n, float x, float y, float z) { cw_node3d_set_scale(n, (t3_vec3){ x, y, z }); }

void n3_camera(float px, float py, float pz, float tx, float ty, float tz) {
  CAM_P[0] = px; CAM_P[1] = py; CAM_P[2] = pz;
  CAM_T[0] = tx; CAM_T[1] = ty; CAM_T[2] = tz;
  cw_node cam = n3_find("Camera");
  if (!cw_node_valid(cam)) return;
  { cw_value fv; if (cw_node_get_prop(cam, "fov", &fv)) CAM_FOV = (float)fv.f; }
  cw_node3d_set_position(cam, (t3_vec3){ px, py, pz });
  cw_node3d_look_at(cam, (t3_vec3){ tx, ty, tz }, (t3_vec3){ 0, 1, 0 });
}

cw_vec2 n3_project(t3_vec3 w) {
  float f[3] = { CAM_T[0] - CAM_P[0], CAM_T[1] - CAM_P[1], CAM_T[2] - CAM_P[2] };
  float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
  if (fl < 1e-4f) return cw_v2(-1000, -1000);
  for (int i = 0; i < 3; i++) f[i] /= fl;
  float r[3] = { f[1] * 0 - f[2] * 1, f[2] * 0 - f[0] * 0, f[0] * 1 - f[1] * 0 }; /* f x up(0,1,0) */
  r[0] = -f[2]; r[1] = 0; r[2] = f[0];
  float rl = sqrtf(r[0] * r[0] + r[2] * r[2]);
  if (rl < 1e-4f) return cw_v2(-1000, -1000);
  r[0] /= rl; r[2] /= rl;
  float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
  float d[3] = { w.x - CAM_P[0], w.y - CAM_P[1], w.z - CAM_P[2] };
  float x = d[0] * r[0] + d[1] * r[1] + d[2] * r[2];
  float y = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
  float z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
  if (z < 0.05f) return cw_v2(-1000, -1000);
  float t = tanf(CAM_FOV * 0.5f * 3.14159265f / 180.0f);
  return cw_v2(STAGE_W * 0.5f + x / (z * t * (STAGE_W / STAGE_H)) * STAGE_W * 0.5f, STAGE_H * 0.5f - y / (z * t) * STAGE_H * 0.5f);
}

/* ── one-shot effects ───────────────────────────────────────────────── */

typedef struct { cw_node n; int ttl, max; float energy; bool light; } fxn;
static fxn FX[48];

void n3_fx_reset(void) { memset(FX, 0, sizeof FX); }

static fxn *fx_slot(void) {
  for (int i = 0; i < 48; i++) if (!cw_node_valid(FX[i].n)) return &FX[i];
  cw_node_free(FX[0].n);
  return &FX[0];
}

static void setf(cw_node n, const char *k, double v) { cw_node_set(n, k, cw_val_float(v)); }
static void seti(cw_node n, const char *k, int v) { cw_node_set(n, k, cw_val_int(v)); }
static void sets(cw_node n, const char *k, const char *v) { cw_node_set(n, k, cw_val_string(v)); }

void n3_fx(const char *kind, t3_vec3 pos, float s) {
  cw_node n = cw_node_new("GPUParticles3D", kind);
  cw_node_set(n, "one_shot", cw_val_bool(true));
  cw_node_set(n, "explosiveness", cw_val_float(1));
  seti(n, "seed", 1 + (int)(pos.x * 31 + pos.z * 17 + pos.y * 7) % 997);
  float life = 1;
  if (!strcmp(kind, "fire")) {
    seti(n, "amount", 56); life = 0.8f; setf(n, "speed_min", 2 * s); setf(n, "speed_max", 7 * s); setf(n, "spread", 180);
    cw_node_set(n, "gravity", cw_val_vec3(0, 2.5f, 0));
    setf(n, "size", 1.1f * s); setf(n, "damping", 2.5f); setf(n, "energy", 4);
    sets(n, "size_curve", "0 0.4, 0.25 1.2, 1 0");
    sets(n, "color_ramp", "0 1 0.95 0.6 1, 0.25 1 0.5 0.12 1, 1 0.25 0.06 0.02 0");
    cw_node_set(n, "blend", cw_val_enum("ADD"));
  } else if (!strcmp(kind, "smoke")) {
    seti(n, "amount", 28); life = 2.2f; setf(n, "speed_min", 0.8f * s); setf(n, "speed_max", 3 * s); setf(n, "spread", 70);
    cw_node_set(n, "direction", cw_val_vec3(0, 1, 0));
    cw_node_set(n, "gravity", cw_val_vec3(0, 1.2f, 0));
    setf(n, "size", 1.6f * s); setf(n, "damping", 1.2f); setf(n, "spin", 1.2f);
    sets(n, "size_curve", "0 0.3, 0.5 1, 1 1.4");
    sets(n, "color_ramp", "0 0.35 0.3 0.28 0.0, 0.15 0.3 0.27 0.25 0.75, 1 0.12 0.12 0.12 0");
    cw_node_set(n, "blend", cw_val_enum("ALPHA"));
  } else if (!strcmp(kind, "sparks")) {
    seti(n, "amount", 48); life = 1.0f; setf(n, "speed_min", 6 * s); setf(n, "speed_max", 15 * s); setf(n, "spread", 80);
    cw_node_set(n, "gravity", cw_val_vec3(0, -16, 0));
    setf(n, "size", 0.07f); setf(n, "energy", 8); setf(n, "stretch", 0.06f);
    cw_node_set(n, "draw_mode", cw_val_enum("STRETCHED"));
    sets(n, "color_ramp", "0 1 0.9 0.5 1, 1 1 0.3 0.05 0");
    cw_node_set(n, "blend", cw_val_enum("ADD"));
  } else if (!strcmp(kind, "splash")) {
    seti(n, "amount", 64); life = 1.2f; setf(n, "speed_min", 3 * s); setf(n, "speed_max", 8 * s); setf(n, "spread", 35);
    cw_node_set(n, "gravity", cw_val_vec3(0, -14, 0));
    setf(n, "size", 0.16f * s); setf(n, "energy", 1.4f);
    cw_node_set(n, "draw_mode", cw_val_enum("STRETCHED")); setf(n, "stretch", 0.03f);
    sets(n, "color_ramp", "0 0.9 0.97 1 0.9, 1 0.7 0.85 0.95 0");
    cw_node_set(n, "blend", cw_val_enum("ADD"));
  } else { /* dust */
    seti(n, "amount", 14); life = 0.9f; setf(n, "speed_min", 0.4f); setf(n, "speed_max", 1.6f * s); setf(n, "spread", 90);
    cw_node_set(n, "gravity", cw_val_vec3(0, 0.3f, 0));
    setf(n, "size", 0.5f * s);
    sets(n, "color_ramp", "0 0.7 0.65 0.6 0.5, 1 0.6 0.55 0.5 0");
    cw_node_set(n, "blend", cw_val_enum("ALPHA"));
  }
  setf(n, "lifetime", life);
  cw_node_add_child(n3_level(), n);
  cw_node3d_set_position(n, pos);
  fxn *f = fx_slot();
  f->n = n; f->ttl = f->max = (int)(life * 60) + 8; f->light = false;
}

/* at most two flash lights live at once: the scenes carry up to six point lights of their own and three.c lights at
 * most eight of a kind (a ninth lights nothing, which a soak of Blast Ring with ghost bombers hit); a new flash takes
 * over the oldest one's slot */
#define MAX_FLASH 2
void n3_flash(t3_vec3 pos, cw_color c, float energy, float range) {
  int live = 0, oldest = -1;
  for (int i = 0; i < 48; i++)
    if (cw_node_valid(FX[i].n) && FX[i].light) { live++; if (oldest < 0 || FX[i].ttl < FX[oldest].ttl) oldest = i; }
  if (live >= MAX_FLASH && oldest >= 0) { cw_node_free(FX[oldest].n); FX[oldest].n = (cw_node){ 0 }; }
  cw_node n = cw_node_new("OmniLight3D", "Flash");
  cw_node_set(n, "color", cw_val_color(c.r, c.g, c.b, 1));
  cw_node_set(n, "energy", cw_val_float(energy));
  cw_node_set(n, "range", cw_val_float(range));
  cw_node_add_child(n3_level(), n);
  cw_node3d_set_position(n, pos);
  fxn *f = fx_slot();
  f->n = n; f->ttl = f->max = 20; f->energy = energy; f->light = true;
}

void n3_fx_tick(void) {
  for (int i = 0; i < 48; i++) {
    fxn *f = &FX[i];
    if (!cw_node_valid(f->n)) continue;
    if (--f->ttl <= 0) { cw_node_free(f->n); f->n = (cw_node){ 0 }; continue; }
    if (f->light) { float k = (float)f->ttl / (float)f->max; cw_node_set(f->n, "energy", cw_val_float(f->energy * k * k)); }
  }
}

/* an expanding ring that bends the picture (the engine's Shockwave3D) */
void n3_shockwave(t3_vec3 pos, float radius, float seconds, float strength) {
  cw_node n = cw_node_new("Shockwave3D", "Wave");
  cw_node_set(n, "max_radius", cw_val_float(radius));
  cw_node_set(n, "duration", cw_val_float(seconds));
  cw_node_set(n, "strength", cw_val_float(strength));
  cw_node_add_child(n3_level(), n);
  cw_node3d_set_position(n, pos);
  fxn *f = fx_slot();
  f->n = n; f->ttl = f->max = (int)(seconds * 60) + 6; f->light = false;
}

/* ── models and decals ──────────────────────────────────────────────── */

static void shadows_on(t3_object *o, bool cast) {
  if (o->type == T3_SKINNED_MESH || o->type == T3_MESH) { o->cast_shadow = cast; o->receive_shadow = true; }
  for (int i = 0; i < o->child_count; i++) shadows_on(o->children[i], cast);
}
/* a glb from the pack as a Node3D under parent (the engine's resource system shares its geometry and textures) */
cw_node n3_glb(cw_node parent, const char *name, const char *path, bool cast) {
  cw_node n = cw_node_new("Node3D", name);
  cw_node_add_child(parent, n);
  cw_res r = cw_res_load(path, CW_RES_GLTF);
  t3_object *o = cw_gltf_instantiate(r);
  if (!o) { cw_error("partypack: %s did not load", path); return n; }
  shadows_on(o, cast);
  cw_node3d_set_object(n, o);
  t3_release(o);
  return n;
}
/* a decal projected straight down at a point (the engine's Decal3D), freed with the scene */
cw_node n3_decal(const char *texture, t3_vec3 pos, float size, cw_color c, const char *blend) {
  cw_node d = cw_node_new("Decal3D", "Decal");
  cw_node_set(d, "texture", cw_val_string(texture));
  cw_node_set(d, "size", cw_val_vec3(size, size, 1.2f));
  cw_node_set(d, "color", cw_val_color(c.r, c.g, c.b, c.a));
  cw_node_set(d, "blend", cw_val_enum(blend));
  cw_node_add_child(n3_level(), d);
  cw_node3d_set_position(d, pos);
  cw_node3d_set_rotation(d, (t3_vec3){ -1.5707963f, 0, 0 });
  return d;
}

/* a model that casts or receives no shadows at all (a far floor seen through water) */
void n3_receive(cw_node n, bool on) {
  t3_object *o = cw_node3d_object(n);
  if (!o) return;
  /* walk the tree: meshes only */
  t3_object *stack[64]; int sp = 0; stack[sp++] = o;
  while (sp) {
    t3_object *x = stack[--sp];
    if (x->type == T3_SKINNED_MESH || x->type == T3_MESH) { x->receive_shadow = on; if (!on) x->cast_shadow = false; }
    for (int i = 0; i < x->child_count && sp < 63; i++) stack[sp++] = x->children[i];
  }
}
