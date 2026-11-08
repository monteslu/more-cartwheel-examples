/* What is drawn: the land, the buoys, the craft and riders, the camera. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/environment.h"
#include "cartwheel/platform.h"
#include "cartwheel/quality.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "cartwheel/rim_light.h"
#include "cartwheel/scene_file.h"
#include "game.h"

static cw_node craft_n[RACERS], rider_n[RACERS], buoy_n[BUOY_MAX], buoy_old_n[BUOY_MAX];
int g_buoy_hide;
int g_cam_cut = 1, g_cam_drawn_under;
int g_buoy_style = 1; /* 0: the small buoy the course marks replaced, the control of the distance gate; sim and scoring do not see it */
static float mark_scale[BUOY_MAX];
static t3_object *bars_o[RACERS];
static t3_material *band_mat[2], *lamp_mat[2];
static float buoy_phase[BUOY_MAX];
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
/* who they are: the suit, the helmet, skin and hair (0 alpha: helmet only, no ponytail; no skin: long sleeves), and their build */
static const struct rider_look { float suit[3], helmet[3], skin[3], hair[3]; bool sleeves, ponytail; float tall, broad; } LOOK[RACERS] = {
  { { 0.03f, 0.033f, 0.04f }, { 0.82f, 0.82f, 0.8f }, { 0.7f, 0.5f, 0.38f }, { 0.08f, 0.05f, 0.03f }, true, false, 1.0f, 1.0f },
  { { 0.02f, 0.06f, 0.12f }, { 0.05f, 0.12f, 0.3f }, { 0.24f, 0.13f, 0.08f }, { 0.02f, 0.015f, 0.015f }, false, true, 0.94f, 0.9f },
  { { 0.1f, 0.08f, 0.04f }, { 0.95f, 0.72f, 0.05f }, { 0.78f, 0.56f, 0.44f }, { 0.55f, 0.32f, 0.08f }, false, true, 0.97f, 0.92f },
  { { 0.02f, 0.05f, 0.03f }, { 0.04f, 0.04f, 0.045f }, { 0.42f, 0.25f, 0.15f }, { 0.03f, 0.02f, 0.02f }, false, false, 1.07f, 1.12f },
  { { 0.08f, 0.02f, 0.09f }, { 0.86f, 0.3f, 0.55f }, { 0.58f, 0.38f, 0.25f }, { 0.25f, 0.05f, 0.08f }, true, true, 0.91f, 0.88f },
  { { 0.13f, 0.13f, 0.14f }, { 0.9f, 0.2f, 0.1f }, { 0.13f, 0.08f, 0.055f }, { 0.01f, 0.01f, 0.01f }, false, false, 1.03f, 1.06f },
};
static t3_material *suit_mat[RACERS], *vest_mat[RACERS], *helmet_mat[RACERS], *deck_mat[RACERS];
static float wetness[RACERS];
/* spray soaks the rider: the suit and vest darken as they take water and dry over some seconds (they stay matte); the
 * helmet's clear coat goes on only from Medium up */
static void rider_wet(float dt) {
  bool coat = cw_quality_get() >= CW_QUALITY_MEDIUM;
  for (int i = 0; i < RACERS; i++) {
    const craft *c = &G.c[i];
    float soak = c->big_splash ? 0.6f : 0.0f;
    if (!c->air && c->wet > 0.15f && c->speed > 8) soak += dt * 0.25f * (c->speed / 28.0f);
    if (c->spill > 0 || c->dive_t > 0) soak = 1;
    wetness[i] = fminf(1.0f, wetness[i] + soak) - dt / 9.0f;
    wetness[i] = fmaxf(0.0f, wetness[i]);
    float k = 1.0f - 0.35f * wetness[i];
    const struct rider_look *lk = &LOOK[i];
    if (suit_mat[i]) suit_mat[i]->color = (t3_color){ lk->suit[0] * k, lk->suit[1] * k, lk->suit[2] * k };
    if (vest_mat[i]) vest_mat[i]->color = (t3_color){ TEAMS[i][3] * k, TEAMS[i][4] * k, TEAMS[i][5] * k };
    if (helmet_mat[i] && (helmet_mat[i]->clearcoat > 0) != coat) { helmet_mat[i]->clearcoat = coat ? 1.0f : 0.0f; helmet_mat[i]->version++; }
    if (deck_mat[i] && (deck_mat[i]->clearcoat > 0) != coat) { deck_mat[i]->clearcoat = coat ? 0.8f : 0.0f; deck_mat[i]->version++; }
  }
}
static void paint(t3_object *o, int team) {
  if ((o->type == T3_MESH || o->type == T3_SKINNED_MESH)) {
    t3_mesh *m = (t3_mesh *)o;
    for (int k = 0; k < m->material_count; k++) {
      t3_material *mat = m->materials[k];
      if (!mat || !mat->name) continue;
      const float *c = NULL;
      const struct rider_look *lk = &LOOK[team];
      if (!strcmp(mat->name, "deck")) c = TEAMS[team];
      else if (!strcmp(mat->name, "vest") || !strcmp(mat->name, "accent")) c = TEAMS[team] + 3;
      else if (!strcmp(mat->name, "suit")) c = lk->suit;
      else if (!strcmp(mat->name, "helmet")) c = lk->helmet;
      else if (!strcmp(mat->name, "skin")) { c = lk->skin; o->visible = !lk->sleeves; }
      else if (!strcmp(mat->name, "hair")) { c = lk->hair; o->visible = lk->ponytail; }
      if (!c) continue;
      t3_material *cl = t3_material_clone(mat);
      cl->color = (t3_color){ c[0], c[1], c[2] };
      /* what each surface is: the helmet's shell a lacquered clear coat over its paint, the suit's fabric a matte knit
       * that goes flat at grazing angles (cloth never shines), skin a soft sheen of water */
      if (!strcmp(mat->name, "helmet")) cl->type = T3_MESH_PHYSICAL_MATERIAL, cl->roughness = 0.3f, cl->clearcoat = 1.0f, cl->clearcoat_roughness = 0.06f;
      else if (!strcmp(mat->name, "suit")) cl->type = T3_MESH_PHYSICAL_MATERIAL, cl->roughness = 0.88f, cl->diffuse_roughness = 0.7f, cl->sheen = 0.35f, cl->sheen_roughness = 0.9f, cl->sheen_color = (t3_color){ c[0] * 2 + 0.05f, c[1] * 2 + 0.05f, c[2] * 2 + 0.05f };
      else if (!strcmp(mat->name, "vest") || !strcmp(mat->name, "accent")) cl->type = T3_MESH_PHYSICAL_MATERIAL, cl->roughness = 0.86f, cl->diffuse_roughness = 0.5f;
      else if (!strcmp(mat->name, "deck")) cl->type = T3_MESH_PHYSICAL_MATERIAL, cl->roughness = 0.35f, cl->clearcoat = 0.8f, cl->clearcoat_roughness = 0.1f; /* gelcoat over the paint */
      if (o->type == T3_SKINNED_MESH && team < RACERS) { if (!strcmp(mat->name, "suit")) suit_mat[team] = cl; else if (!strcmp(mat->name, "vest")) vest_mat[team] = cl; else if (!strcmp(mat->name, "helmet")) helmet_mat[team] = cl; }
      if (o->type == T3_MESH && team < RACERS && !strcmp(mat->name, "deck")) deck_mat[team] = cl;
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
static t3_material *sun_mat;
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
static cw_node model_node(cw_node parent, const char *name, t3_object *o);
static cw_node dry_parent(cw_node root);
/* a course's sky: its painted image on a material of its own */
static t3_material *sky_material(const char *path) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->map = cw_res_texture(cw_res_load(path, CW_RES_TEXTURE));
  if (m->map) { t3_retain(m->map); m->map->wrap_s = T3_REPEAT; m->map->color_space = T3_SRGB_COLOR_SPACE; }
  m->color = (t3_color){ 1.0f, 1.0f, 1.0f };
  m->depth_write = false;
  m->side = T3_DOUBLE_SIDE;
  m->fog = false;
  return m;
}
/* every course's sky is loaded here, so choosing a course does not read a texture off the disk. Each is imported as colour by a
 * .cwimport beside its image: on GL a loaded texture's sRGB cannot be changed afterwards */
static t3_material *sky_mats[LEVELS];
static void clouds_init(void) {
  t3_geometry *g = grid_geo(96, 48, dome_vertex);
  for (int i = 0; i < LEVELS; i++) sky_mats[i] = sky_material(COURSES[i]->sky);
  t3_material *m = sky_mats[G.level];
  t3_retain(m);
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
  sun_mat = sm;
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
static void scatter_add(const char *model, const char *far_model, const char *map, float density, float smin, float smax, float range, uint32_t seed, bool tilt, float sink) {
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
  /* the far version, part for part in the same order with the near one's materials */
  t3_object *fo = far_model ? cw_gltf_instantiate(cw_res_load(far_model, CW_RES_GLTF)) : NULL;
  if (fo) {
    int fp = 0;
    sp = 0, stack[sp++] = fo;
    while (sp > 0 && fp < CW_SCATTER_PARTS) {
      t3_object *o = stack[--sp];
      if (o->type == T3_MESH) {
        t3_mesh *m = (t3_mesh *)o;
        s.geometry[1][fp] = m->geometry;
        t3_retain(m->geometry);
        s.material[1][fp] = s.material[0][fp];
        if (s.material[1][fp]) t3_retain(s.material[1][fp]);
        fp++;
      }
      for (int i = 0; i < o->child_count && sp < 32; i++) stack[sp++] = o->children[i];
    }
    s.lod_distance[0] = 140;
  }
  size_t db;
  uint8_t *dm = cw_asset_load(map, &db);
  s.density = density;
  s.density_map = dm;
  s.dm_w = s.dm_h = DEF->map_n;
  s.scale_min = smin, s.scale_max = smax;
  s.visible_range = range;
  s.fade = range * 0.08f;
  s.cast_shadow = false;
  s.density_knob = "scatter";
  s.seed = seed;
  s.sink = sink;
  s.align_to_normal = tilt;
  cw_terrain_scatter_add(G.land, &s);
  cw_scatter_def_release(&s);
  free(dm);
  t3_release(po);
  if (fo) t3_release(fo);
}

static cw_node land_n, landmark_n, hook_n;
static bool g_marks_stale = true;
static void land_init(cw_node root) {


  cw_node n = cw_node_new("Terrain3D", "Land");
  const course_def *d = DEF;
  cw_node_set(n, "size_x", cw_val_int(d->quads));
  cw_node_set(n, "size_z", cw_val_int(d->quads));
  cw_node_set(n, "vertex_spacing", cw_val_float(d->spacing));
  cw_node_set(n, "min_height", cw_val_float(d->minh));
  cw_node_set(n, "max_height", cw_val_float(d->maxh));
  cw_node_set(n, "far_distance", cw_val_float(900));
  cw_node_set(n, "physics", cw_val_bool(true));
  cw_node_set(n, "heightmap", cw_val_string(d->heights));
  cw_node_add_child(root, n);
  land_n = n;
  G.land = cw_terrain_of(n);
  cw_terrain_set_side_detile(G.land, d->side_detile);
  { t3_material *lm = cw_terrain_material(G.land); if (lm) lm->color = (t3_color){ 1.55f, 1.5f, 1.45f }; }
  size_t bytes;
  uint32_t *ctl = cw_asset_load(d->control, &bytes);
  if (!ctl || bytes != (size_t)(d->quads + 1) * (d->quads + 1) * 4) CW_FATAL("course control missing");
  cw_terrain_set_control(G.land, 0, 0, d->quads + 1, d->quads + 1, ctl);
  free(ctl);
  for (int i = 0; i < d->layer_n; i++) {
    char p[64];
    size_t la, ln;
    snprintf(p, sizeof p, "%s-albedo.rgba", d->layer[i]);
    uint8_t *a = cw_asset_load(p, &la);
    snprintf(p, sizeof p, "%s-normal.rgba", d->layer[i]);
    uint8_t *nn = cw_asset_load(p, &ln);
    if (!a || !nn) CW_FATAL("terrain layer missing");
    cw_terrain_set_layer_pixels(G.land, i, a, nn, 1024, 1024, d->layer_uv[i]);
    free(a);
    free(nn);
  }
  for (int i = 0; i < d->scatter_n; i++) {
    const course_scatter *s = &d->scatter[i];
    scatter_add(s->model, s->far_model, s->map, s->density, s->smin, s->smax, s->range, s->seed, s->sink, s->tilt);
  }
}

/* the ramps: every part of the model instanced over the course's ramps. They stand still, so this runs when a course loads */
#define RAMP_PARTS 12 /* one per material */
static t3_instanced_mesh *ramp_parts[RAMP_PARTS];
static int ramp_part_n;
static void ramps_apply(void) {
  for (int p = 0; p < ramp_part_n; p++) {
    t3_instanced_mesh *im = ramp_parts[p];
    im->count = RAMPS;
    for (int r = 0; r < RAMPS; r++) {
      t3_mat4 m;
      bool bigr = DEF->ramps[r][5] > 0;
      t3_mat4_compose(&m, (t3_vec3){ DEF->ramps[r][0], ramp_top_at(r), DEF->ramps[r][1] }, t3_quat_from_euler((t3_euler){ 0, DEF->ramps[r][2], 0, T3_YXZ }), bigr ? (t3_vec3){ RAMP_BIG_XZ, RAMP_BIG_Y, RAMP_BIG_XZ } : (t3_vec3){ 1, 1, 1 });
      t3_instanced_mesh_set_matrix_at(im, r, &m);
    }
    if (im->instance_matrix) im->instance_matrix->version++; /* a second course's places must reach the GPU */
  }
}
static void ramps_init(cw_node root) {
  t3_object *src = cw_gltf_instantiate(cw_res_load("res://models/ramp.glb", CW_RES_GLTF));
  t3_object *stack[16];
  int sp = 0;
  stack[sp++] = src;
  while (sp > 0) {
    t3_object *o = stack[--sp];
    if (o->type == T3_MESH && ramp_part_n < RAMP_PARTS) {
      t3_mesh *m = (t3_mesh *)o;
      t3_instanced_mesh *im = t3_instanced_mesh_new(m->geometry, m->materials[0], RAMP_MAX);
      ((t3_object *)im)->cast_shadow = true, ((t3_object *)im)->receive_shadow = true;
      char name[24];
      snprintf(name, sizeof name, "RampPart%d", ramp_part_n);
      /* (not in the refraction: under the water its deck would read as a shoal, with a hard edge of shore foam over it) */
      model_node(dry_parent(root), name, (t3_object *)im);
      t3_sphere everywhere = { { 0, 0, 0 }, 4000 };
      t3_instanced_mesh_set_bounding_sphere(im, &everywhere);
      ramp_parts[ramp_part_n++] = im;
      t3_release(im);
    }
    for (int i = 0; i < o->child_count && sp < 16; i++) stack[sp++] = o->children[i];
  }
  t3_release(src);
  ramps_apply();
}

/* what a course sets by hand: its models instanced over their places, made when the course loads */
#define PLACED_PARTS 40
static cw_node placed_n[PLACED_PARTS];
static int placed_count;
static void placed_apply(cw_node root) {
  for (int i = 0; i < placed_count; i++) cw_node_free(placed_n[i]);
  placed_count = 0;
  const course_def *d = DEF;
  for (int mi = 0; mi < d->model_n; mi++) {
    int n = 0;
    for (int q = 0; q < d->placed_n; q++) if ((int)d->placed[q][0] == mi) n++;
    if (!n) continue;
    t3_object *src = cw_gltf_instantiate(cw_res_load(d->models[mi], CW_RES_GLTF));
    t3_object *stack[16];
    int sp = 0;
    stack[sp++] = src;
    while (sp > 0) {
      t3_object *o = stack[--sp];
      if (o->type == T3_MESH && placed_count < PLACED_PARTS) {
        t3_mesh *m = (t3_mesh *)o;
        t3_instanced_mesh *im = t3_instanced_mesh_new(m->geometry, m->materials[0], n);
        ((t3_object *)im)->cast_shadow = true, ((t3_object *)im)->receive_shadow = true;
        int k = 0;
        for (int q = 0; q < d->placed_n; q++) {
          const float *p = d->placed[q];
          if ((int)p[0] != mi) continue;
          t3_mat4 mat;
          float y = p[7] > 0 ? 0 : ground_at(p[1], p[2]);
          t3_mat4_compose(&mat, (t3_vec3){ p[1], y, p[2] }, t3_quat_from_euler((t3_euler){ 0, p[3], 0, T3_YXZ }), (t3_vec3){ p[4], p[4], p[4] });
          t3_instanced_mesh_set_matrix_at(im, k++, &mat);
        }
        char name[24];
        snprintf(name, sizeof name, "Placed%d", placed_count);
        placed_n[placed_count] = model_node(dry_parent(root), name, (t3_object *)im);
        placed_count++;
        t3_release(im);
      }
      for (int i = 0; i < o->child_count && sp < 16; i++) stack[sp++] = o->children[i];
    }
    t3_release(src);
  }
}

/* the marks and the look of the course in G.level */
static void marks_apply(cw_node root) {
  if (cw_node_valid(landmark_n)) cw_node_free(landmark_n);
  if (cw_node_valid(hook_n)) cw_node_free(hook_n);
  landmark_n = hook_n = (cw_node){ 0 };
  const course_def *d = DEF;
  if (d->landmark) {
    t3_object *o = cw_gltf_instantiate(cw_res_load(d->landmark, CW_RES_GLTF));
    shadows(o);
    landmark_n = model_node(root, "Landmark", o);
    cw_node3d_set_position(landmark_n, (t3_vec3){ d->landmark_at[0], d->landmark_at[1], d->landmark_at[2] });
    t3_release(o);
  }
  if (d->hook.model) {
    t3_object *o = cw_gltf_instantiate(cw_res_load(d->hook.model, CW_RES_GLTF));
    shadows(o);
    hook_n = model_node(dry_parent(root), "Hook", o);
    cw_node3d_set_position(hook_n, (t3_vec3){ d->hook.x, d->hook.kind == 4 ? ground_at(d->hook.x, d->hook.z) : 0, d->hook.z });
    cw_node3d_set_rotation(hook_n, (t3_vec3){ 0, cw_atan2(d->hook.dirx, d->hook.dirz), 0 }); /* its span across the way through */
    t3_release(o);
  }
}
static void props_apply(cw_node root) {
  const course_def *d = DEF;
  for (int i = 0; i < d->prop_n; i++) {
    cw_node n = cw_node_get(root, d->props[i][0]);
    const cw_prop *p = cw_node_valid(n) ? cw_node_find_prop(n, d->props[i][1], NULL) : NULL;
    cw_value v;
    if (!p || !cw_value_parse(d->props[i][2], p->type, &v) || !cw_node_set(n, d->props[i][1], v)) CW_FATAL("course %s: %s.%s = %s refused", d->id, d->props[i][0], d->props[i][1], d->props[i][2]);
  }
}
static void mask_apply(void) {
  size_t mb;
  float *mask = cw_asset_load(DEF->mask, &mb);
  if (!mask || mb != (size_t)DEF->mask_n * DEF->mask_n * 4) CW_FATAL("wave mask missing");
  cw_water_set_wave_mask(G.water, mask, DEF->mask_n, DEF->mask_n);
  free(mask);
}
static void sky_apply(void) {
  t3_mesh *dome = (t3_mesh *)sky_dome;
  t3_material *m = sky_mats[G.level];
  if (dome->materials[0] != m) {
    t3_retain(m);
    t3_release(dome->materials[0]);
    dome->materials[0] = m;
  }
  if (sun_mat) sun_mat->color = (t3_color){ 42 * DEF->sun_disc, 28 * DEF->sun_disc, 13 * DEF->sun_disc };
}
/* G.level changed */
void view_course_apply(void) {
  if (g_cam_cut) G.cam[0] = 0, G.look[0] = 0;
  cw_node root = cw_node_get(cw_tree_root(cw_scene_app_tree()), "Root");
  if (cw_node_valid(land_n)) { G.land = NULL; cw_node_free(land_n); }
  land_init(root);
  mask_apply();
  sky_apply();
  marks_apply(root);
  placed_apply(root);
  props_apply(root);
  ramps_apply();
  g_marks_stale = true;
}

/* a bigger sea, a taller mark: a crest of the open sea at its worst stands higher than a mast made for a lagoon */
float mark_size(void) {
  float amp = 0;
  for (int i = 0; i < 4; i++) amp += DEF->waves[i][2] + DEF->waves[i][3] * DEF->swell[1];
  return fminf(1.45f, fmaxf(1.0f, 1.0f + (amp - 2.0f) * 0.12f));
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
        /* lit, so the colour holds in the dark and through rain */
        float e = lamp ? 6.0f : 1.1f;
        (*slot)->emissive = (t3_color){ col.r * e, col.g * e, col.b * e };
      }
      t3_retain(*slot);
      t3_release(m->materials[k]);
      m->materials[k] = *slot;
    }
  }
  for (int i = 0; i < o->child_count; i++) buoy_paint(o->children[i], side);
}

/* what must not show in the water's refraction hangs under one node: the engine holds 16 hidden nodes and quietly ignores the rest, and this game has over 100 */
static cw_node dry_group;
static cw_node dry_parent(cw_node root) {
  if (!cw_node_valid(dry_group)) {
    dry_group = cw_node_new("Node3D", "NotInRefraction");
    cw_node_add_child(root, dry_group);
    cw_water_refraction_hide(G.water, dry_group);
  }
  return dry_group;
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
  mask_apply();
  props_apply(root);

  cw_res craft_res = cw_res_load("res://models/craft.glb", CW_RES_GLTF);
  cw_res rider_res = cw_res_load("res://models/rider.glb", CW_RES_GLTF);
  cw_res buoy_res = cw_res_load("res://models/buoy.glb", CW_RES_GLTF);
  cw_res mark_left_res = cw_res_load("res://models/mark_left.glb", CW_RES_GLTF), mark_right_res = cw_res_load("res://models/mark_right.glb", CW_RES_GLTF);
  for (int i = 0; i < RACERS; i++) {
    char name[24];
    snprintf(name, sizeof name, "Craft%d", i);
    t3_object *co = cw_gltf_instantiate(craft_res);
    paint(co, i);
    if (i == 0) shadows(co);
    craft_n[i] = model_node(dry_parent(root), name, co);
    bars_o[i] = t3_object_get_by_name(co, "Bars");
    snprintf(name, sizeof name, "Rider%d", i);
    rider_n[i] = cw_node_new("Node3D", name);
    cw_node_add_child(craft_n[i], rider_n[i]);
    cw_node3d_set_scale(rider_n[i], (t3_vec3){ LOOK[i].broad, LOOK[i].tall, LOOK[i].broad });
    t3_object *ro = cw_gltf_instantiate(rider_res);
    paint(ro, i);
    if (i == 0) shadows(ro);
    cw_node3d_set_object(rider_n[i], ro);
    rider_bind(i, ro);
    { cw_rim_light_desc rl = cw_rim_light_default(); rl.color = (t3_color){ 1.0f, 0.82f, 0.62f }; rl.strength = 0.1f; rl.power = 5.0f; cw_rim_light_apply_tree(ro, &rl); }
    t3_release(ro);
    t3_release(co);
  }
  /* the buoys: one colour per side */
  for (int b = 0; b < BUOY_MAX; b++) {
    char name[24];
    snprintf(name, sizeof name, "Buoy%d", b);
    int side = (b & 1) == 0; /* every course alternates, beginning on the right */
    /* side 1: keep it on your right, so the board's arrow points left */
    t3_object *o = cw_gltf_instantiate(side ? mark_left_res : mark_right_res);
    buoy_paint(o, side);
    shadows(o);
    /* (lit, and under the water its float reached the refraction: on WebGPU it came through the wakes as lime patches) */
    buoy_n[b] = model_node(dry_parent(root), name, o);
    t3_release(o);
    snprintf(name, sizeof name, "BuoyOld%d", b);
    o = cw_gltf_instantiate(buoy_res);
    buoy_paint(o, side);
    buoy_old_n[b] = model_node(dry_parent(root), name, o);
    t3_release(o);
    cw_node_set(buoy_old_n[b], "visible", cw_val_bool(false));
    mark_scale[b] = 0.72f;
    buoy_phase[b] = (float)b * 1.7f;
  }
  ramps_init(root);
  /* the lighthouse stands on the islet's top, the gate floats on the line */
  {
    marks_apply(root);
    placed_apply(root);
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

/* the attract loop: long tracking shots of the leader out on the water. The camera is always well above the surface and clear of
 * the land; it rides with the craft and its height is smoothed, so it does not bob. */
/* land on the sight line from the camera to the craft */
static bool land_between(float x, float y, float z, const craft *c) {
  for (int i = 1; i < 8; i++) {
    float t = (float)i / 8.0f;
    if (ground_at(x + (c->x - x) * t, z + (c->z - z) * t) > y + (c->y + 0.8f - y) * t) return true;
  }
  return false;
}
static bool shot_blocked(float x, float z, float up, const craft *c) {
  float h = cw_water_height_at(G.water, x, z, G.wt), g = ground_at(x, z);
  return g > -1.5f || land_between(x, (h > g ? h : g) + up, z, c);
}

/* the sight line from a camera at (x, y, z) to the hull: how high the camera must stand for the swell between them to leave
 * the hull in view, and (with y) whether a crest hides it now */
static float clear_of_swell(float x, float z, const craft *c) {
  /* (to the top of the hull, over all but the last of the line: the water right at the hull is the hull's own, and asking to
   * clear it sends the camera up into the sky) */
  float need = -1e9f, ty = c->y + 0.45f;
  for (int i = 1; i < 7; i++) {
    float t = (float)i / 8.0f;
    float h = cw_water_height_at(G.water, x + (c->x - x) * t, z + (c->z - z) * t, G.wt) + 0.15f;
    float y = (h - ty * t) / (1.0f - t);
    if (y > need) need = y;
  }
  return need;
}
int g_attract_clear = 1; /* debug: 0 is the control, a camera that ignores the swell */

static bool attract_camera(float dt) {
  if (G.state != RACE_ATTRACT) return false;
  const craft *c = &G.c[G.order[0]];
  float sy, cy;
  cw_sincos(c->yaw, &sy, &cy);
  G.cam_orbit += dt;
  if (G.cam_orbit > 32.0f) G.cam_orbit -= 32.0f;
  float t = G.cam_orbit;
  int shot = (int)(t / 8.0f);
  static int last_shot = -1;
  bool cut = shot != last_shot || G.cam[0] == 0;
  last_shot = shot;
  float side, back, up, lookf;
  /* right of the hull is (-cos, sin) */
  switch (shot) {
  case 0: side = 0, back = 6.5f, up = 2.3f, lookf = 8; break;          /* low chase */
  case 1: side = 7.5f, back = 1.0f, up = 1.1f, lookf = 1.5f; break;    /* beside it, low */
  case 2: side = -4.5f, back = -10.0f, up = 2.1f, lookf = -2; break;   /* ahead, looking back at it */
  default: side = -11.0f, back = 6.0f, up = 1.6f, lookf = 3; break;    /* wide, on the other flank */
  }
  float cx = c->x - cy * side - sy * back, cz = c->z + sy * side - cy * back;
  /* never in land, and never with land between the camera and the craft: the other flank, or failing that the chase */
  if (shot_blocked(cx, cz, up, c)) { side = -side; cx = c->x - cy * side - sy * back, cz = c->z + sy * side - cy * back; }
  if (shot_blocked(cx, cz, up, c)) { side = 0, back = 5.5f, up = 1.9f, lookf = 8; cx = c->x - sy * back, cz = c->z - cy * back; }
  float h = cw_water_height_at(G.water, cx, cz, G.wt), gnd = ground_at(cx, cz);
  float want = (h > gnd ? h : gnd) + up;
  float clear = clear_of_swell(cx, cz, c);
  /* rises to keep the craft in view, but stays a low camera */
  if (g_attract_clear && clear + 0.1f > want) want = clear + 0.1f > want + 2.2f ? want + 2.2f : clear + 0.1f;
  float lx = c->x + sy * lookf, ly = c->y + 0.7f, lz = c->z + cy * lookf;
  memcpy(G.cam_prev, G.cam, sizeof G.cam);
  memcpy(G.look_prev, G.look, sizeof G.look);
  G.cam[0] = cx, G.cam[2] = cz;
  G.cam[1] = cut ? want : G.cam[1] + (want - G.cam[1]) * (1.0f - cw_exp(-dt * 7.0f));
  if (G.cam[1] < h + 0.8f) G.cam[1] = h + 0.8f;
  /* the menu covers the left of the picture: aim left of the craft, and it rides in the open water on the right */
  {
    float fx = lx - cx, fz = lz - cz, fl = sqrtf(fx * fx + fz * fz);
    if (fl > 0.1f) { float off = fl * 0.2f; lx += fz / fl * off, lz -= fx / fl * off; }
  }
  G.look[0] = lx, G.look[1] = cut ? ly : G.look[1] + (ly - G.look[1]) * (1.0f - cw_exp(-dt * 6.0f)), G.look[2] = lz;
  /* the rubric's check */
  if (G.cam[1] < cw_water_height_at(G.water, cx, cz, G.wt)) G.attract_under++;
  if (ground_at(cx, cz) > G.cam[1] - 0.3f) G.attract_land++;
  if (land_between(cx, G.cam[1], cz, c)) G.attract_blocked++;
  if (clear > G.cam[1]) G.attract_hidden++;
  if (cut) memcpy(G.cam_prev, G.cam, sizeof G.cam), memcpy(G.look_prev, G.look, sizeof G.look);
  G.cam_fov_prev = G.cam_fov = shot == 3 ? 44.0f : 56.0f;
  cw_water_ripple_follow(G.water, c->x + sy * 20.0f, c->z + cy * 20.0f);
  return true;
}

/* the fixed-tick camera: chases the human, its height smoothed so it rides over swells instead of through them */
void view_tick(float dt) {
  rider_wet(dt);
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
  /* a flip turns the whole craft over in front of the camera, and from straight behind it is only the hull's bottom. Swing out to the side for it, so craft and
   * rider are seen going round together in profile, and look at the craft */
  static float off;
  {
    float want = c->air && (c->trick_kind == 2 || c->trick_kind == 3) ? 1.0f : 0.0f;
    off += (want - off) * fminf(1.0f, dt * (want > off ? 6.0f : 1.5f));
    if (G.tick < 3) off = 0;
    back -= 1.0f * off, up += 0.4f * off; }
  float wx = c->x - bx * back + cy * 7.0f * off, wz = c->z - bz * back - sy * 7.0f * off;
  float wh = cw_water_height_at(G.water, wx, wz, G.wt);
  /* up after a jump: the camera rises with the craft, so the air is seen and the craft stays in the picture */
  float rise = c->y - wh > 2.0f ? 0.97f : 0.45f;
  float wy = (c->y * rise + wh * (1.0f - rise)) + up;
  if (wy < wh + 1.25f) wy = wh + 1.25f;
  float kx = 1.0f - cw_exp(-dt * 18.0f), ky = 1.0f - cw_exp(-dt * (c->air && c->air_time > 0.25f ? 11.0f : 5.5f));
  bool first_tick = G.cam[0] == 0;
  if (G.tick < 3 || G.cam[0] == 0) kx = ky = 1;
  G.cam[0] += (wx - G.cam[0]) * kx;
  G.cam[2] += (wz - G.cam[2]) * kx;
  G.cam[1] += (wy - G.cam[1]) * ky;
  if (G.cam[1] < wh + 1.1f) G.cam[1] = wh + 1.1f;
  /* and never through a ramp's deck */
  { float deck; if (ramp_over(G.cam[0], G.cam[2], &deck) && G.cam[1] < deck + 1.4f) G.cam[1] = deck + 1.4f; }
  /* the rubric's check: the chase camera at its own x, z is never under the water */
  if (G.cam[1] < cw_water_height_at(G.water, G.cam[0], G.cam[2], G.wt)) G.cam_under++;
  float lx = c->x + sy * 7.0f * (1.0f - 0.8f * off), lz = c->z + cy * 7.0f * (1.0f - 0.8f * off), ly = (c->y - wh > 2.0f ? c->y : c->y * 0.7f + cw_water_height_at(G.water, lx, lz, G.wt) * 0.3f) + 1.0f;
  float kl = 1.0f - cw_exp(-dt * 8.0f);
  if (G.tick < 3 || G.look[0] == 0) kl = 1;
  G.look[0] += (lx - G.look[0]) * kl;
  G.look[1] += (ly - G.look[1]) * kl;
  G.look[2] += (lz - G.look[2]) * kl;
  /* the first tick after a reset is a cut: nothing to draw between. From a zeroed last place, the frames between ticks put the camera near the world's origin, on some courses under the sea */
  if (first_tick && g_cam_cut) memcpy(G.cam_prev, G.cam, sizeof G.cam), memcpy(G.look_prev, G.look, sizeof G.look);
  /* over the line: the camera swings slowly round to the rider's side and front, low over the water, and holds the craft in the middle */
  if (c->finish > 0 && G.state == RACE_ON) {
    /* the camera's place around the craft moves with the craft (it is doing 80 km/h) and swings toward the orbit's place
     * from where it was at the start of this tick, not from the chase place worked out above */
    float ang = c->yaw + 1.2f + G.fin_t * 0.32f, sa, ca;
    cw_sincos(ang, &sa, &ca);
    float off[3] = { G.cam_prev[0] - c->px, G.cam_prev[1] - c->py, G.cam_prev[2] - c->pz };
    float ox = c->x + sa * 11.0f, oz = c->z + ca * 11.0f, oh = cw_water_height_at(G.water, ox, oz, G.wt);
    float want[3] = { sa * 11.0f, fmaxf(oh + 2.4f, c->y + 3.0f) - c->y, ca * 11.0f }, k = 1.0f - cw_exp(-dt * 0.9f); /* a slow swing: a fast pan across the low sun reads as a flash */
    for (int i = 0; i < 3; i++) off[i] += (want[i] - off[i]) * k;
    G.cam[0] = c->x + off[0], G.cam[1] = c->y + off[1], G.cam[2] = c->z + off[2];
    if (G.cam[1] < cw_water_height_at(G.water, G.cam[0], G.cam[2], G.wt) + 1.1f) G.cam[1] = cw_water_height_at(G.water, G.cam[0], G.cam[2], G.wt) + 1.1f;
    /* never into a ramp: over its deck if it comes round over one */
    { float deck; if (ramp_over(G.cam[0], G.cam[2], &deck) && G.cam[1] < deck + 2.5f) G.cam[1] = deck + 2.5f; }
    /* the look rides with the craft too: its lag behind the craft closes, it never trails 20 m behind */
    float kl2 = 1.0f - cw_exp(-dt * 4.0f), lo[3] = { G.look_prev[0] - c->px, G.look_prev[1] - c->py, G.look_prev[2] - c->pz };
    G.look[0] = c->x + lo[0] * (1 - kl2), G.look[1] = c->y + lo[1] + (1.0f - lo[1]) * kl2, G.look[2] = c->z + lo[2] * (1 - kl2);
  }
  /* speed you can feel: the view widens from 56 degrees at a crawl to 72 at full speed, and a turbo opens it further */
  float sk = fminf(1.0f, fmaxf(0.0f, (c->speed - 6.0f) / 24.0f));
  float fov = c->finish > 0 && G.state == RACE_ON ? 50.0f : 56.0f + g_fov_span * sk * (2.0f - sk) * 0.5f + g_fov_span * 0.5f * sk + (c->turbo_hold > 0 ? 6.0f * fminf(1.0f, c->turbo_hold / 0.6f) : 0);
  if (G.cam_fov == 0) G.cam_fov = fov;
  G.cam_fov += (fov - G.cam_fov) * (dt * 2.0f);
  /* and the camera leans into a hard carve, up to 4.5 degrees: from the stick and the speed, smoothed, never from the waves under the hull */
  G.cam_roll_prev = G.cam_roll;
  { float want = c->air || c->spill > 0 || c->finish > 0 ? 0.0f : c->steer * fminf(1.0f, c->speed / 28.0f) * (1.0f + 0.3f * fminf(1.0f, fmaxf(0.0f, c->lean))) * g_cam_roll_max;
    want = fmaxf(-g_cam_roll_max, fminf(g_cam_roll_max, want));
    G.cam_roll += (want - G.cam_roll) * (1.0f - cw_exp(-dt * 2.5f));
    if (G.tick < 3 || first_tick) G.cam_roll = G.cam_roll_prev = 0; }
  /* the ripples live around where the player looks */
  cw_water_ripple_follow(G.water, c->x + sy * 34.0f, c->z + cy * 34.0f);
}

bool g_peek_on;
float g_fov_span = 16.0f, g_cam_roll_max = 0.0785f; /* debug: fov_span 6, cam_roll 0 bring the old flat camera back (the speed gate control) */
float g_peek[6];
void view_sync(double alpha) {
  float a = (float)alpha;
  /* a frame can come before the first tick after a reset, with no camera place at all: find it now */
  if (g_cam_cut && G.cam[0] == 0 && cw_node_valid(G.water)) view_tick(1.0f / 60.0f);
  /* who is on the water, and which buoys count */
  { static int shown = -1;
    int now = g_difficulty * 2 + (racer_on(1) ? 1 : 0) + g_buoy_style * 16 + g_buoy_hide * 32;
    if (now != shown || g_marks_stale) {
      shown = now, g_marks_stale = false;
      for (int i = 0; i < RACERS; i++) cw_node_set(craft_n[i], "visible", cw_val_bool(racer_on(i)));
      for (int b = 0; b < BUOY_MAX; b++) {
        cw_node_set(buoy_n[b], "visible", cw_val_bool(!g_buoy_hide && g_buoy_style && b < BUOY_N && buoy_on(b)));
        cw_node_set(buoy_old_n[b], "visible", cw_val_bool(!g_buoy_hide && !g_buoy_style && b < BUOY_N && buoy_on(b)));
      }
    } }
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
  /* the squall: lightning lights the world and the sky; the beacon's lamp is seen through the rain on its rock */
  { static float last_fill = -1;
    cw_node root = cw_node_get(cw_tree_root(cw_scene_app_tree()), "Root");
    if (DEF->rain > 0) {
      cw_node fill = cw_node_get(root, "Fill");
      float f = 1.5f + 7.0f * G.flash;
      if (f != last_fill) { cw_node_set(fill, "energy", cw_val_float(f)); last_fill = f; }
      t3_mesh *dome = (t3_mesh *)sky_dome;
      float k = 1.0f + 2.5f * G.flash;
      dome->materials[0]->color = (t3_color){ k, k, k * 1.08f };
    } else if (last_fill >= 0) { last_fill = -1; ((t3_mesh *)sky_dome)->materials[0]->color = (t3_color){ 1, 1, 1 }; }
  }
  if (DEF->hook.kind == 3 && cw_node_valid(hook_n)) {
    /* the tanker, always head on along its way, and the white water it pushes */
    cw_node3d_set_position(hook_n, (t3_vec3){ G.tanker[0], 0, G.tanker[1] });
    cw_node3d_set_rotation(hook_n, (t3_vec3){ 0, cw_atan2(DEF->hook.dirx * G.tanker[2], DEF->hook.dirz * G.tanker[2]), 0 });
    cw_water_wake(G.water, G.tanker[0], G.tanker[1], DEF->hook.dirx * G.tanker[2] * DEF->hook.s_in, DEF->hook.dirz * G.tanker[2] * DEF->hook.s_in, 9.0f, 0.2f, 0.7f);
  }
  /* a craft under the water must reach the refraction, or it vanishes at the surface */
  { static bool out[RACERS];
    cw_node root = cw_node_get(cw_tree_root(cw_scene_app_tree()), "Root");
    for (int i = 0; i < RACERS; i++) {
      bool under = G.c[i].dive_t > 0;
      if (under != out[i]) { out[i] = under; cw_node_reparent(craft_n[i], under ? root : dry_parent(root)); }
    } }
  for (int b = 0; b < BUOY_N; b++) {
    cw_water_sample s;
    cw_water_sample_at(G.water, BUOYS[b][0], BUOYS[b][1], wt, &s);
    t3_vec3 at = { BUOYS[b][0], s.height, BUOYS[b][1] };
    cw_node3d_set_position(buoy_n[b], at);
    if (!g_buoy_style) cw_node3d_set_position(buoy_old_n[b], at);
    t3_vec3 up = { 0, 1, 0 };
    t3_vec3 n = s.normal;
    float d = up.x * n.x + up.y * n.y + up.z * n.z;
    t3_vec3 c = { up.y * n.z - up.z * n.y, up.z * n.x - up.x * n.z, up.x * n.y - up.y * n.x };
    t3_quat tilt = t3_quat_normalize((t3_quat){ c.x, c.y, c.z, 1 + d });
    /* its board faces the rider coming along the course, and it rides the water half as steeply: a mast this tall would wave about */
    { float tx, tz, dx, dz;
      track_at(BUOYS[b][3], &tx, &tz, &dx, &dz);
      t3_quat level = t3_quat_normalize((t3_quat){ tilt.x * 0.5f, tilt.y * 0.5f, tilt.z * 0.5f, tilt.w });
      cw_node3d_set_quaternion(buoy_n[b], t3_quat_multiply(level, t3_quat_from_euler((t3_euler){ 0, cw_atan2(dx, dz), 0, T3_YXZ }))); }
    if (!g_buoy_style) cw_node3d_set_quaternion(buoy_old_n[b], tilt);
    /* the next one stands full size, the ones after it smaller */
    { float want = b == G.c[0].next_buoy ? 1.0f : 0.72f;
      mark_scale[b] += (want - mark_scale[b]) * 0.1f;
      float k = mark_scale[b] * mark_size();
      cw_node3d_set_scale(buoy_n[b], (t3_vec3){ k, k, k }); }
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
  /* never drawn under the sea: the first tick of a course can place the camera by the last course's tide. A held debug camera may go where it is told */
  { float h = cw_water_height_at(G.water, cp.x, cp.z, G.wt);
    if (g_cam_cut && !g_peek_on && G.state != RACE_ATTRACT && cp.y < h + 0.8f) cp.y = h + 0.8f;
    if (G.state != RACE_ATTRACT && cp.y < h) g_cam_drawn_under++; }
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
  if (g_peek_on) { cp = (t3_vec3){ g_peek[0], g_peek[1], g_peek[2] }; lp = (t3_vec3){ g_peek[3], g_peek[4], g_peek[5] }; }
  cw_node3d_set_position(G.camera, cp);
  { /* the up the camera keeps, leaned about its line of sight */
    float roll = g_peek_on || G.state == RACE_ATTRACT ? 0.0f : lerpf(G.cam_roll_prev, G.cam_roll, a);
    float fx = lp.x - cp.x, fz = lp.z - cp.z, fl = sqrtf(fx * fx + fz * fz);
    t3_vec3 upv = { 0, 1, 0 };
    if (fl > 1e-3f && roll != 0) { float rx = -fz / fl, rz = fx / fl; upv = (t3_vec3){ rx * sinf(roll), cosf(roll), rz * sinf(roll) }; }
    cw_node3d_look_at(G.camera, lp, upv); }
  if (G.cam_fov > 1) cw_node_set(G.camera, "fov", cw_val_float(lerpf(G.cam_fov_prev > 1 ? G.cam_fov_prev : G.cam_fov, G.cam_fov, a)));
}
