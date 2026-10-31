/* What is drawn: the land, the buoys, the craft and riders, the camera. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/environment.h"
#include "cartwheel/platform.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "cartwheel/rim_light.h"
#include "cartwheel/scene_file.h"
#include "course_data.h"
#include "game.h"

static cw_node craft_n[RACERS], rider_n[RACERS], buoy_n[BUOY_N];
static t3_object *bars_o[RACERS];
static t3_material *band_mat[2], *lamp_mat[2];
static float buoy_phase[BUOY_N];
static cw_node gate_n;

static const float TEAMS[RACERS][6] = {
  /* deck, vest */
  { 0.88f, 0.1f, 0.06f, 0.92f, 0.34f, 0.05f },
  { 0.05f, 0.42f, 0.86f, 0.05f, 0.5f, 0.86f },
  { 0.95f, 0.72f, 0.05f, 0.92f, 0.76f, 0.08f },
  { 0.08f, 0.68f, 0.36f, 0.86f, 0.86f, 0.82f },
  { 0.6f, 0.12f, 0.7f, 0.7f, 0.25f, 0.76f },
  { 0.86f, 0.86f, 0.84f, 0.12f, 0.14f, 0.18f },
};

static void shadows(t3_object *o) {
  if (o->type == T3_SKINNED_MESH || o->type == T3_MESH) o->cast_shadow = true, o->receive_shadow = true;
  for (int i = 0; i < o->child_count; i++) shadows(o->children[i]);
}

/* this racer's paint: its own copies of the materials named deck and vest */
static void paint(t3_object *o, int team) {
  if ((o->type == T3_MESH || o->type == T3_SKINNED_MESH)) {
    t3_mesh *m = (t3_mesh *)o;
    for (int k = 0; k < m->material_count; k++) {
      t3_material *mat = m->materials[k];
      if (!mat || !mat->name) continue;
      const float *c = NULL;
      if (!strcmp(mat->name, "deck")) c = TEAMS[team];
      else if (!strcmp(mat->name, "vest") || !strcmp(mat->name, "accent")) c = TEAMS[team] + 3;
      if (!c) continue;
      t3_material *cl = t3_material_clone(mat);
      cl->color = (t3_color){ c[0], c[1], c[2] };
      t3_retain(cl); t3_release(m->materials[k]); m->materials[k] = cl;
      t3_release(cl);
    }
  }
  for (int i = 0; i < o->child_count; i++) paint(o->children[i], team);
}

/* The evening sky: one painted dome around the camera (the ProceduralSky still gives the sun's direction, the sun's colour
 * and the light it casts; its own picture is off). In the reflection too. The sun itself is a bright sprite, so the bloom
 * takes it. */
static t3_object *sky_dome, *sun_sprite;
static t3_geometry *grid_geo(int nu, int nv, void (*fn)(float u, float v, float *p)) {
  int nvert = (nu + 1) * (nv + 1);
  float *pos = malloc((size_t)nvert * 12), *uv = malloc((size_t)nvert * 8), *nrm = calloc((size_t)nvert, 12);
  uint32_t *idx = malloc((size_t)(nu * nv * 6) * 4);
  for (int j = 0; j <= nv; j++)
    for (int i = 0; i <= nu; i++) {
      int k = j * (nu + 1) + i;
      float u = (float)i / (float)nu, v = (float)j / (float)nv;
      fn(u, v, pos + k * 3);
      nrm[k * 3 + 1] = 1;
      uv[k * 2] = u, uv[k * 2 + 1] = v;
    }
  int w = 0;
  for (int j = 0; j < nv; j++)
    for (int i = 0; i < nu; i++) {
      uint32_t a = (uint32_t)(j * (nu + 1) + i), b = a + 1, c = a + (uint32_t)nu + 1, d = c + 1;
      idx[w++] = a, idx[w++] = c, idx[w++] = b;
      idx[w++] = b, idx[w++] = c, idx[w++] = d;
    }
  t3_geometry *g = t3_geometry_new();
  { t3_attribute *a_ = t3_attribute_new(T3_FLOAT32, pos, nvert, 3); t3_geometry_set_attribute(g, T3_ATTR_POSITION, a_); t3_release(a_); }
  { t3_attribute *a_ = t3_attribute_new(T3_FLOAT32, nrm, nvert, 3); t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a_); t3_release(a_); }
  { t3_attribute *a_ = t3_attribute_new(T3_FLOAT32, uv, nvert, 2); t3_geometry_set_attribute(g, T3_ATTR_UV, a_); t3_release(a_); }
  { t3_attribute *a_ = t3_attribute_new(T3_UINT32, idx, w, 1); t3_geometry_set_index(g, a_); t3_release(a_); }
  free(pos), free(uv), free(nrm), free(idx);
  return g;
}
#define SKY_R 5200.0f
static void dome_vertex(float u, float v, float *p) {
  /* v = 1 at the horizon: elevation = (1 - v)^1.7 of a quarter turn, below it the horizon's colour hangs down */
  float el = (float)pow(1.0f - v, 1.7f) * TAU * 0.25f, se, ce, sa, ca;
  cw_sincos(el, &se, &ce);
  cw_sincos((u - 0.5f) * TAU, &sa, &ca);
  p[0] = sa * ce * SKY_R, p[1] = se * SKY_R - (v > 0.999f ? 260.0f : 0.0f), p[2] = ca * ce * SKY_R;
}
static void quad_vertex(float u, float v, float *p) { p[0] = u - 0.5f, p[1] = 0.5f - v, p[2] = 0; }
static void clouds_init(void) {
  cw_res tex = cw_res_load("res://textures/sky.png", CW_RES_TEXTURE);
  t3_geometry *g = grid_geo(96, 48, dome_vertex);
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->map = cw_res_texture(tex);
  if (m->map) { t3_retain(m->map); m->map->wrap_s = T3_REPEAT; m->map->color_space = T3_SRGB_COLOR_SPACE; }
  m->color = (t3_color){ 1.0f, 1.0f, 1.0f };
  m->depth_write = false;
  m->side = T3_DOUBLE_SIDE;
  m->fog = false;
  t3_mesh *mesh = t3_mesh_new(g, m);
  t3_release(g), t3_release(m);
  mesh->base.frustum_culled = false;
  mesh->base.render_order = -100;
  sky_dome = (t3_object *)mesh;
  cw_node root = cw_node_get(cw_tree_root(cw_scene_app_tree()), "Root");
  G.clouds = cw_node_new("Node3D", "SkyDome");
  cw_node_add_child(root, G.clouds);
  cw_node3d_set_object(G.clouds, sky_dome);
  /* the sun: a soft dot far too bright for the screen */
  t3_geometry *q = grid_geo(1, 1, quad_vertex);
  t3_material *sm = t3_mesh_basic_material_new(0xffffff);
  cw_res dot = cw_res_load("res://effects/sun.png", CW_RES_TEXTURE);
  sm->map = cw_res_texture(dot);
  if (sm->map) t3_retain(sm->map);
  sm->color = (t3_color){ 42, 28, 13 };
  sm->transparent = true;
  sm->blending = T3_ADDITIVE_BLENDING;
  sm->depth_write = false;
  sm->fog = false;
  t3_mesh *sq = t3_mesh_new(q, sm);
  t3_release(q), t3_release(sm);
  sq->base.frustum_culled = false;
  sq->base.render_order = -99;
  sun_sprite = (t3_object *)sq;
  G.sun_node = cw_node_new("Node3D", "SunDisc");
  cw_node_add_child(root, G.sun_node);
  cw_node3d_set_object(G.sun_node, sun_sprite);
}

/* a scatter layer from a model and a density map; every instance stands in the ground it is placed on */
static void scatter_add(const char *model, const char *map, float density, float smin, float smax, float range, uint32_t seed, bool tilt, float sink) {
  cw_res res = cw_res_load(model, CW_RES_GLTF);
  t3_object *po = cw_gltf_instantiate(res);
  if (!po) return;
  cw_scatter_def s = cw_scatter_default_def();
  t3_object *stack[32];
  int sp = 0, parts = 0;
  stack[sp++] = po;
  while (sp > 0 && parts < CW_SCATTER_PARTS) {
    t3_object *o = stack[--sp];
    if (o->type == T3_MESH) {
      t3_mesh *m = (t3_mesh *)o;
      s.geometry[0][parts] = m->geometry;
      s.material[0][parts] = m->materials[0];
      t3_retain(m->geometry);
      t3_retain(s.material[0][parts]);
      parts++;
    }
    for (int i = 0; i < o->child_count && sp < 32; i++) stack[sp++] = o->children[i];
  }
  size_t db;
  uint8_t *dm = cw_asset_load(map, &db);
  s.density = density;
  s.density_map = dm;
  s.dm_w = s.dm_h = PALMS_N;
  s.scale_min = smin, s.scale_max = smax;
  s.visible_range = range;
  s.fade = range * 0.08f;
  s.cast_shadow = true;
  s.density_knob = "scatter";
  s.seed = seed;
  s.sink = sink;
  s.align_to_normal = tilt;
  cw_terrain_scatter_add(G.land, &s);
  cw_scatter_def_release(&s);
  free(dm);
  t3_release(po);
}

static void land_init(cw_node root) {


  cw_node n = cw_node_new("Terrain3D", "Land");
  cw_node_set(n, "size_x", cw_val_int(COURSE_QUADS));
  cw_node_set(n, "size_z", cw_val_int(COURSE_QUADS));
  cw_node_set(n, "vertex_spacing", cw_val_float(COURSE_SPACING));
  cw_node_set(n, "min_height", cw_val_float(COURSE_MINH));
  cw_node_set(n, "max_height", cw_val_float(COURSE_MAXH));
  cw_node_set(n, "far_distance", cw_val_float(900));
  cw_node_set(n, "physics", cw_val_bool(true));
  cw_node_set(n, "heightmap", cw_val_string("course/heights.r16"));
  cw_node_add_child(root, n);
  G.land = cw_terrain_of(n);
  { t3_material *lm = cw_terrain_material(G.land); if (lm) lm->color = (t3_color){ 1.55f, 1.5f, 1.45f }; }
  size_t bytes;
  uint32_t *ctl = cw_asset_load("course/control.u32", &bytes);
  if (!ctl || bytes != (size_t)(COURSE_QUADS + 1) * (COURSE_QUADS + 1) * 4) CW_FATAL("course control missing");
  cw_terrain_set_control(G.land, 0, 0, COURSE_QUADS + 1, COURSE_QUADS + 1, ctl);
  free(ctl);
  static const char *const names[4] = { "sand", "coast", "rock", "grass" };
  static const float uv[4] = { 0.16f, 0.22f, 0.075f, 0.09f };
  for (int i = 0; i < 4; i++) {
    char p[64];
    size_t la, ln;
    snprintf(p, sizeof p, "terrain/%s-albedo.rgba", names[i]);
    uint8_t *a = cw_asset_load(p, &la);
    snprintf(p, sizeof p, "terrain/%s-normal.rgba", names[i]);
    uint8_t *nn = cw_asset_load(p, &ln);
    if (!a || !nn) CW_FATAL("terrain layer missing");
    cw_terrain_set_layer_pixels(G.land, i, a, nn, 1024, 1024, uv[i]);
    free(a);
    free(nn);
  }
  scatter_add("res://models/palm.glb", "course/palms.u8", 0.012f, 0.75f, 1.45f, 900, 77, false, 0.25f);
  scatter_add("res://models/shrub.glb", "course/shrubs.u8", 0.03f, 0.8f, 2.0f, 650, 78, false, 0.35f);
  scatter_add("res://models/rock0.glb", "course/rocks.u8", 0.05f, 0.6f, 2.4f, 700, 79, true, 0.3f);
  scatter_add("res://models/rock1.glb", "course/rocks.u8", 0.04f, 0.5f, 1.8f, 700, 80, true, 0.25f);
  scatter_add("res://models/rock2.glb", "course/rocks.u8", 0.03f, 0.7f, 3.2f, 700, 81, true, 0.35f);
}

/* the buoy's band and lamp take its side's colour; one shared material per side */
static void buoy_paint(t3_object *o, int side) {
  if (o->type == T3_MESH) {
    t3_mesh *m = (t3_mesh *)o;
    for (int k = 0; k < m->material_count; k++) {
      t3_material *mat = m->materials[k];
      if (!mat || !mat->name) continue;
      bool lamp = !strcmp(mat->name, "lamp");
      if (!lamp && strcmp(mat->name, "band")) continue;
      t3_material **slot = lamp ? &lamp_mat[side] : &band_mat[side];
      if (!*slot) {
        *slot = t3_material_clone(mat);
        t3_color col = side ? (t3_color){ 1.0f, 0.12f, 0.03f } : (t3_color){ 0.98f, 0.72f, 0.02f };
        (*slot)->color = col;
        if (lamp) (*slot)->emissive = (t3_color){ col.r * 6, col.g * 6, col.b * 6 };
      }
      t3_retain(*slot);
      t3_release(m->materials[k]);
      m->materials[k] = *slot;
    }
  }
  for (int i = 0; i < o->child_count; i++) buoy_paint(o->children[i], side);
}

static cw_node model_node(cw_node parent, const char *name, t3_object *o) {
  cw_node n = cw_node_new("Node3D", name);
  cw_node_add_child(parent, n);
  cw_node3d_set_object(n, o);
  return n;
}

void view_init(void) {
  cw_tree *t = cw_scene_app_tree();
  cw_node root = cw_node_get(cw_tree_root(t), "Root");
  G.water = cw_node_get(root, "Water");
  G.camera = cw_node_get(root, "Camera");
  G.sun = cw_node_get(root, "Sun");
  G.sky = cw_node_get(root, "Sky");
  land_init(root);
  clouds_init();
  size_t mb;
  float *mask = cw_asset_load("course/wavemask.f32", &mb);
  if (!mask || mb != (size_t)MASK_N * MASK_N * 4) CW_FATAL("wave mask missing");
  cw_water_set_wave_mask(G.water, mask, MASK_N, MASK_N);
  free(mask);

  cw_res craft_res = cw_res_load("res://models/craft.glb", CW_RES_GLTF);
  cw_res rider_res = cw_res_load("res://models/rider.glb", CW_RES_GLTF);
  cw_res buoy_res = cw_res_load("res://models/buoy.glb", CW_RES_GLTF);
  for (int i = 0; i < RACERS; i++) {
    char name[24];
    snprintf(name, sizeof name, "Craft%d", i);
    t3_object *co = cw_gltf_instantiate(craft_res);
    paint(co, i);
    shadows(co);
    craft_n[i] = model_node(root, name, co);
    cw_water_refraction_hide(G.water, craft_n[i]);
    bars_o[i] = t3_object_get_by_name(co, "Bars");
    snprintf(name, sizeof name, "Rider%d", i);
    rider_n[i] = cw_node_new("Node3D", name);
    cw_node_add_child(craft_n[i], rider_n[i]);
    t3_object *ro = cw_gltf_instantiate(rider_res);
    paint(ro, i);
    shadows(ro);
    cw_node3d_set_object(rider_n[i], ro);
    rider_bind(i, ro);
    { cw_rim_light_desc rl = cw_rim_light_default(); rl.color = (t3_color){ 1.0f, 0.82f, 0.62f }; rl.strength = 0.1f; rl.power = 5.0f; cw_rim_light_apply_tree(ro, &rl); }
    t3_release(ro);
    t3_release(co);
  }
  /* the buoys: one colour per side */
  for (int b = 0; b < BUOY_N; b++) {
    char name[24];
    snprintf(name, sizeof name, "Buoy%d", b);
    t3_object *o = cw_gltf_instantiate(buoy_res);
    int side = BUOYS[b][2] > 0;
    buoy_paint(o, side);
    shadows(o);
    buoy_n[b] = model_node(root, name, o);
    t3_release(o);
    buoy_phase[b] = (float)b * 1.7f;
  }
  /* the lighthouse stands on the islet's top, the gate floats on the line */
  {
    cw_res lr = cw_res_load("res://models/lighthouse.glb", CW_RES_GLTF);
    t3_object *lo = cw_gltf_instantiate(lr);
    shadows(lo);
    cw_node ln = model_node(root, "Lighthouse", lo);
    cw_node3d_set_position(ln, (t3_vec3){ LIGHT_X, LIGHT_Y, LIGHT_Z });
    t3_release(lo);
    cw_res gr = cw_res_load("res://models/gate.glb", CW_RES_GLTF);
    t3_object *go = cw_gltf_instantiate(gr);
    shadows(go);
    gate_n = model_node(root, "Gate", go);
    t3_release(go);
  }
  fx_init(cw_node_get(root, "Fx"));
  rider_init();
}

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float lerp_angle(float a, float b, float t) {
  float d = b - a;
  while (d > 3.14159265f) d -= TAU;
  while (d < -3.14159265f) d += TAU;
  return a + d * t;
}

static t3_quat craft_quat(float yaw, float pitch, float roll) {
  /* forward +z: yaw about y, then nose up (minus x), then roll about z */
  return t3_quat_from_euler((t3_euler){ -pitch, yaw, roll, T3_YXZ });
}

/* the attract loop: cuts around the leader. It opens under the water looking up as the racers pass overhead, and rises
 * through the surface into the evening. */
static bool attract_camera(float dt) {
  if (G.state != RACE_ATTRACT) return false;
  const craft *c = &G.c[G.order[0]];
  float sy, cy;
  cw_sincos(c->yaw, &sy, &cy);
  G.cam_orbit += dt;
  if (G.cam_orbit > 32.0f) G.cam_orbit -= 32.0f;
  float t = G.cam_orbit;
  int shot = t < 9 ? 0 : t < 16 ? 1 : t < 24 ? 2 : 3;
  static int last_shot = -1;
  static float ax, az;
  float cx, cz, ch, lx = c->x, ly = c->y + 0.7f, lz = c->z, h;
  if (shot != last_shot) {
    /* anchor the fixed shots where the leader will be in a few seconds */
    ax = c->x + c->vx * 3.2f, az = c->z + c->vz * 3.2f;
    last_shot = shot;
    G.cam_prev[0] = 0;
  }
  if (shot == 0) {
    /* under the surface ahead of the leader, rising to break through as they pass */
    float rise = t / 9.0f;
    cx = ax + cy * 2.5f, cz = az - sy * 2.5f;
    h = cw_water_height_at(G.water, cx, cz, G.wt);
    float depth = rise < 0.55f ? -3.2f + 2.0f * rise / 0.55f : -1.2f + 2.9f * (rise - 0.55f) / 0.45f;
    ch = h + depth;
    if (rise > 0.6f) lx = c->x + sy * 4, lz = c->z + cy * 4;
  } else if (shot == 1) {
    /* low beside the craft, the spray in the sun */
    cx = c->x + cy * 5.2f - sy * 2.2f, cz = c->z - sy * 5.2f - cy * 2.2f;
    h = cw_water_height_at(G.water, cx, cz, G.wt);
    ch = h + 0.75f;
  } else if (shot == 2) {
    /* high and wide, turning slowly around the leader: the atoll */
    float a = t * 0.08f, sa, ca;
    cw_sincos(a, &sa, &ca);
    cx = c->x + sa * 62, cz = c->z + ca * 62;
    ch = c->y + 26;
    (void)h;
  } else {
    /* the chase */
    cx = c->x - sy * 5.5f, cz = c->z - cy * 5.5f;
    h = cw_water_height_at(G.water, cx, cz, G.wt);
    ch = h + 2.0f;
    lx = c->x + sy * 6, lz = c->z + cy * 6;
  }
  bool cut = G.cam_prev[0] == 0;
  memcpy(G.cam_prev, cut ? (float[3]){ cx, ch, cz } : G.cam, sizeof G.cam);
  memcpy(G.look_prev, cut ? (float[3]){ lx, ly, lz } : G.look, sizeof G.look);
  G.cam[0] = cx, G.cam[1] = ch, G.cam[2] = cz;
  G.look[0] = lx, G.look[1] = ly, G.look[2] = lz;
  G.cam_fov_prev = G.cam_fov = shot == 2 ? 52.0f : 62.0f;
  cw_water_ripple_follow(G.water, c->x + sy * 20.0f, c->z + cy * 20.0f);
  return true;
}

/* the fixed-tick camera: chases the human, its height smoothed so it rides over swells instead of through them */
void view_tick(float dt) {
  if (attract_camera(dt)) return;
  const craft *c = &G.c[0];
  memcpy(G.cam_prev, G.cam, sizeof G.cam);
  memcpy(G.look_prev, G.look, sizeof G.look);
  G.cam_fov_prev = G.cam_fov;
  float sy, cy;
  cw_sincos(c->yaw, &sy, &cy);
  float vs = c->speed > 1 ? 1 : 0;
  float hx = vs ? c->vx / c->speed : sy, hz = vs ? c->vz / c->speed : cy;
  float bx = sy * 0.72f + hx * 0.28f, bz = cy * 0.72f + hz * 0.28f;
  float back = 4.0f + c->speed * 0.035f, up = 1.6f + c->speed * 0.012f;
  float wx = c->x - bx * back, wz = c->z - bz * back;
  float wh = cw_water_height_at(G.water, wx, wz, G.wt);
  float wy = (c->y * 0.45f + wh * 0.55f) + up;
  if (wy < wh + 1.25f) wy = wh + 1.25f;
  float kx = 1.0f - cw_exp(-dt * 18.0f), ky = 1.0f - cw_exp(-dt * 5.5f);
  if (G.tick < 3 || G.cam[0] == 0) kx = ky = 1;
  G.cam[0] += (wx - G.cam[0]) * kx;
  G.cam[2] += (wz - G.cam[2]) * kx;
  G.cam[1] += (wy - G.cam[1]) * ky;
  if (G.cam[1] < wh + 1.1f) G.cam[1] = wh + 1.1f;
  /* the rubric's check: the chase camera at its own x, z is never under the water */
  if (G.cam[1] < cw_water_height_at(G.water, G.cam[0], G.cam[2], G.wt)) G.cam_under++;
  float lx = c->x + sy * 7.0f, lz = c->z + cy * 7.0f, ly = c->y * 0.7f + cw_water_height_at(G.water, lx, lz, G.wt) * 0.3f + 1.0f;
  float kl = 1.0f - cw_exp(-dt * 8.0f);
  if (G.tick < 3 || G.look[0] == 0) kl = 1;
  G.look[0] += (lx - G.look[0]) * kl;
  G.look[1] += (ly - G.look[1]) * kl;
  G.look[2] += (lz - G.look[2]) * kl;
  float fov = 54.0f + c->speed * 0.34f;
  if (G.cam_fov == 0) G.cam_fov = fov;
  G.cam_fov += (fov - G.cam_fov) * (dt * 2.0f);
  /* the ripples live around where the player looks */
  cw_water_ripple_follow(G.water, c->x + sy * 34.0f, c->z + cy * 34.0f);
}

void view_sync(double alpha) {
  float a = (float)alpha;
  for (int i = 0; i < RACERS; i++) {
    const craft *c = &G.c[i];
    t3_vec3 p = { lerpf(c->px, c->x, a), lerpf(c->py, c->y, a), lerpf(c->pz, c->z, a) };
    cw_node3d_set_position(craft_n[i], p);
    cw_node3d_set_quaternion(craft_n[i], craft_quat(lerp_angle(c->pyaw, c->yaw, a), lerp_angle(c->ppitch, c->pitch, a), lerp_angle(c->proll, c->roll, a)));
    if (bars_o[i]) {
      bars_o[i]->quaternion = t3_quat_from_euler((t3_euler){ 0, -c->steer * 0.42f, 0, T3_YXZ });
    }
    rider_pose(i, c, a);
  }
  double wt = cw_water_time(G.water) + (a - 1.0f) * (1.0f / 60.0f);
  for (int b = 0; b < BUOY_N; b++) {
    cw_water_sample s;
    cw_water_sample_at(G.water, BUOYS[b][0], BUOYS[b][1], wt, &s);
    cw_node3d_set_position(buoy_n[b], (t3_vec3){ BUOYS[b][0], s.height, BUOYS[b][1] });
    t3_vec3 up = { 0, 1, 0 };
    t3_vec3 n = s.normal;
    float d = up.x * n.x + up.y * n.y + up.z * n.z;
    t3_vec3 c = { up.y * n.z - up.z * n.y, up.z * n.x - up.x * n.z, up.x * n.y - up.y * n.x };
    cw_node3d_set_quaternion(buoy_n[b], t3_quat_normalize((t3_quat){ c.x, c.y, c.z, 1 + d }));
  }
  {
    float gx, gz, dx, dz;
    track_at(0, &gx, &gz, &dx, &dz);
    cw_water_sample s;
    cw_water_sample_at(G.water, gx, gz, wt, &s);
    cw_node3d_set_position(gate_n, (t3_vec3){ gx, s.height, gz });
    cw_node3d_set_rotation(gate_n, (t3_vec3){ 0, cw_atan2(dx, dz), 0 });
  }
  t3_vec3 cp = { lerpf(G.cam_prev[0], G.cam[0], a), lerpf(G.cam_prev[1], G.cam[1], a), lerpf(G.cam_prev[2], G.cam[2], a) };
  t3_vec3 lp = { lerpf(G.look_prev[0], G.look[0], a), lerpf(G.look_prev[1], G.look[1], a), lerpf(G.look_prev[2], G.look[2], a) };
  cw_node3d_set_position(G.clouds, (t3_vec3){ cp.x, 0, cp.z });
  {
    cw_sky_state sk;
    cw_sky_get_state(cw_scene_app_tree(), &sk);
    if (sk.active) {
      cw_node3d_set_rotation(G.clouds, (t3_vec3){ 0, cw_atan2(sk.sun_direction.x, sk.sun_direction.z), 0 });
      float d = 5000.0f, size = 1400.0f;
      t3_vec3 at = { cp.x + sk.sun_direction.x * d, cp.y + sk.sun_direction.y * d, cp.z + sk.sun_direction.z * d };
      cw_node3d_set_position(G.sun_node, at);
      cw_node3d_set_scale(G.sun_node, (t3_vec3){ size, size, size });
      cw_node3d_look_at(G.sun_node, cp, (t3_vec3){ 0, 1, 0 });
    }
  }
  cw_node3d_set_position(G.camera, cp);
  cw_node3d_look_at(G.camera, lp, (t3_vec3){ 0, 1, 0 });
  if (G.cam_fov > 1) cw_node_set(G.camera, "fov", cw_val_float(lerpf(G.cam_fov_prev > 1 ? G.cam_fov_prev : G.cam_fov, G.cam_fov, a)));
}
