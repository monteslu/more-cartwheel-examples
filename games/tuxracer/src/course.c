/* A converted course (tools/convert.mjs): heights and surface types on a
 * uniform grid, the object list, and the Terrain3D node that draws and
 * collides it. */
#include "game.h"

static int terrain_q = -1;               /* course_set_terrain_quality: -1 = the graphics preset */
static cw_terrain *cur_terrain;          /* the loaded course's terrain (the lever reaches it) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/platform.h"

static const struct { float diam, height, vary, above_ground; } OBJ[OBJ_KINDS] = {
  [OBJ_TREE3] = { 1.4f, 1.0f, 0.5f, 0 }, [OBJ_TREE1] = { 1.4f, 2.5f, 0.5f, 0 }, [OBJ_TREE2] = { 1.4f, 2.5f, 0.5f, 0 },
  [OBJ_HERRING] = { 1.0f, 1.0f, 0, 0.2f }, [OBJ_FLAG] = { 1.0f, 1.0f, 0, 0 }, [OBJ_FINISH] = { 9.0f, 6.0f, 0, 0 },
  [OBJ_START] = { 9.0f, 6.0f, 0, 0 }, [OBJ_FLOAT] = { 2.0f, 1.0f, 0, 0 },
};

static bool parse_course_txt(course *c, const char *text) {
  const char *p = text;
  while (*p) {
    const char *nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    char line[512];
    if (len >= sizeof line) len = sizeof line - 1;
    memcpy(line, p, len);
    line[len] = 0;
    p += len + (nl ? 1 : 0);
    char key[32];
    int n = 0;
    if (sscanf(line, "%31s%n", key, &n) != 1) continue;
    const char *v = line + n;
    while (*v == ' ') v++;
    if (!strcmp(key, "width")) c->width = (float)atof(v);
    else if (!strcmp(key, "length")) c->length = (float)atof(v);
    else if (!strcmp(key, "play_width")) c->play_width = (float)atof(v);
    else if (!strcmp(key, "play_length")) c->play_length = (float)atof(v);
    else if (!strcmp(key, "start")) sscanf(v, "%f %f", &c->start_x, &c->start_d);
    else if (!strcmp(key, "angle")) c->angle = (float)atof(v);
    else if (!strcmp(key, "samples")) sscanf(v, "%d %d", &c->sx, &c->sz);
    else if (!strcmp(key, "spacing")) c->spacing = (float)atof(v);
    else if (!strcmp(key, "min_height")) c->min_h = (float)atof(v);
    else if (!strcmp(key, "max_height")) c->max_h = (float)atof(v);
    else if (!strcmp(key, "par_time")) c->par_time = (float)atof(v);
    else if (!strcmp(key, "objects")) c->object_count = atoi(v);
    else if (!strcmp(key, "friction")) sscanf(v, "%f %f %f", &c->friction[SURF_ICE], &c->friction[SURF_ROCK], &c->friction[SURF_SNOW]);
    else if (!strcmp(key, "title")) snprintf(c->title, sizeof c->title, "%s", v);
    else if (!strcmp(key, "description")) snprintf(c->description, sizeof c->description, "%s", v);
  }
  return c->sx > 1 && c->sz > 1 && c->width > 0 && c->length > 0;
}

static uint32_t hash_str(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

bool course_load(course *c, const char *dir, cw_node parent, bool mirrored) {
  memset(c, 0, sizeof *c);
  snprintf(c->dir, sizeof c->dir, "%s", dir);
  c->mirrored = mirrored;
  char path[160];
  size_t len;
  snprintf(path, sizeof path, "courses/%s/course.txt", dir);
  char *txt = cw_asset_load(path, &len);
  if (!txt || !parse_course_txt(c, txt)) { cw_error("course %s: missing or bad course.txt", dir); free(txt); return false; }
  free(txt);
  int n = c->sx * c->sz;
  snprintf(path, sizeof path, "courses/%s/heights.u16", dir);
  uint16_t *h = cw_asset_load(path, &len);
  snprintf(path, sizeof path, "courses/%s/types.u8", dir);
  uint8_t *t = cw_asset_load(path, &len);
  if (!h || !t) { cw_error("course %s: missing heights or types", dir); free(h); free(t); return false; }
  c->heights = cw_alloc((size_t)n * sizeof *c->heights, CW_MEM_GAME);
  c->types = cw_alloc((size_t)n, CW_MEM_GAME);
  for (int j = 0; j < c->sz; j++)
    for (int i = 0; i < c->sx; i++) {
      int si = mirrored ? c->sx - 1 - i : i;
      uint16_t q = h[si + c->sx * j]; /* little-endian in the file; every host here is */
      c->heights[i + c->sx * j] = q;
      c->types[i + c->sx * j] = t[si + c->sx * j];
    }
  free(h);
  free(t);
  float padded_w = (float)(c->sx - 1) * c->spacing, padded_l = (float)(c->sz - 1) * c->spacing;
  if (mirrored) c->start_x = padded_w - c->start_x;

  /* the Terrain3D: sample (0, 0) at (0, 0, -padded_l), rows toward +z */
  cw_node tn = cw_node_new("Terrain3D", "Course");
  cw_node_set(tn, "size_x", cw_val_int(c->sx - 1));
  cw_node_set(tn, "size_z", cw_val_int(c->sz - 1));
  cw_node_set(tn, "region_size", cw_val_int(32));
  cw_node_set(tn, "vertex_spacing", cw_val_float(c->spacing));
  cw_node_set(tn, "min_height", cw_val_float(c->min_h));
  cw_node_set(tn, "max_height", cw_val_float(c->max_h));
  cw_node_set(tn, "far_distance", cw_val_float(900));
  cw_node_set(tn, "physics", cw_val_bool(true));
  cw_node_set(tn, "cast_shadow", cw_val_bool(!getenv("TUXRACER_NO_TERRAIN_SHADOW"))); /* dev: measure the terrain's shadow draws */
  cw_node_set(tn, "roughness", cw_val_float(1));
  cw_node_set(tn, "roughness", cw_val_float(1)); /* the layers' own roughness maps (the _nr tile maps) */
  cw_node3d_set_position(tn, (t3_vec3){ padded_w * 0.5f, 0, -padded_l * 0.5f });
  cw_node_add_child(parent, tn);
  cw_terrain *tr = cw_terrain_of(tn);
  if (!tr) { cw_error("course %s: terrain not built", dir); return false; }
  c->node = tn;
  c->terrain = tr;
  cur_terrain = tr;
  cw_terrain_set_quality(tr, terrain_q);
  cw_terrain_set_heights(tr, 0, 0, c->sx, c->sz, c->heights);
  uint32_t *ctl = cw_frame_alloc((size_t)n * sizeof *ctl);
  for (int k = 0; k < n; k++) ctl[k] = cw_terrain_control(c->types[k], c->types[k], 0, 0, 0, false);
  cw_terrain_set_control(tr, 0, 0, c->sx, c->sz, ctl);
  textures_terrain_layers(tr);
  textures_snow_attach(tr);
  cw_terrain_sync_physics(tr);

  /* objects: trees vary in size by a seeded random, as course_load.c does with rand() */
  snprintf(path, sizeof path, "courses/%s/objects.bin", dir);
  uint8_t *ob = cw_asset_load(path, &len);
  int count = ob ? (int)(len / 12) : 0;
  c->objects = cw_alloc((size_t)(count ? count : 1) * sizeof *c->objects, CW_MEM_GAME);
  memset(c->objects, 0, (size_t)(count ? count : 1) * sizeof *c->objects); /* `collected` starts false: on a second
                                                                             * load (the title's backdrop course came first) cw_alloc hands back used memory */
  c->object_count = count;
  cw_rng rng;
  cw_rng_seed(&rng, hash_str(dir));
  for (int i = 0; i < count; i++) {
    course_object *o = &c->objects[i];
    float x, d;
    memcpy(&x, ob + i * 12 + 4, 4);
    memcpy(&d, ob + i * 12 + 8, 4);
    o->type = ob[i * 12];
    if (o->type >= OBJ_KINDS) o->type = OBJ_FLAG;
    o->x = mirrored ? padded_w - x : x;
    o->z = -d;
    float vary = OBJ[o->type].vary;
    float scale = 1 + (cw_rng_float(&rng) * 2 - 1) * vary;
    o->height = OBJ[o->type].height * scale;
    o->diam = OBJ[o->type].diam * scale;
    o->y = course_height(c, o->x, o->z) + OBJ[o->type].above_ground;
  }
  free(ob);
  cw_info("course %s: %s, %gx%g m, %d objects%s", dir, c->title, c->width, c->length, count, mirrored ? ", mirrored" : "");
  return true;
}

void course_unload(course *c) {
  if (c->terrain == cur_terrain) cur_terrain = NULL;
  if (cw_node_valid(c->node)) cw_node_free(c->node);
  cw_free(c->heights);
  cw_free(c->types);
  cw_free(c->objects);
  memset(c, 0, sizeof *c);
}

void course_set_terrain_quality(int q) {
  terrain_q = q;
  if (cur_terrain) cw_terrain_set_quality(cur_terrain, q);
}

float course_height(const course *c, float x, float z) { return c->terrain ? cw_terrain_height_at(c->terrain, x, z) : 0; }
t3_vec3 course_normal(const course *c, float x, float z) { return c->terrain ? cw_terrain_normal_at(c->terrain, x, z) : (t3_vec3){ 0, 1, 0 }; }

/* The surface type weights of the triangle under (x, z): the quad splits
 * along the diagonal from (i, j + 1) to (i + 1, j), as the terrain does. */
void course_surface(const course *c, float x, float z, float w[SURF_KINDS]) {
  w[0] = w[1] = w[2] = 0;
  if (!c->heights) { w[SURF_SNOW] = 1; return; }
  float fi = x / c->spacing, fj = (z + (float)(c->sz - 1) * c->spacing) / c->spacing;
  if (fi < 0) fi = 0;
  if (fj < 0) fj = 0;
  if (fi > (float)(c->sx - 1) - 1e-3f) fi = (float)(c->sx - 1) - 1e-3f;
  if (fj > (float)(c->sz - 1) - 1e-3f) fj = (float)(c->sz - 1) - 1e-3f;
  int i = (int)fi, j = (int)fj;
  float fx = fi - (float)i, fz = fj - (float)j;
  const uint8_t *T = c->types;
  int sx = c->sx;
  if (fx + fz < 1) {
    w[T[i + sx * j]] += 1 - fx - fz;
    w[T[i + 1 + sx * j]] += fx;
    w[T[i + sx * (j + 1)]] += fz;
  } else {
    w[T[i + 1 + sx * (j + 1)]] += fx + fz - 1;
    w[T[i + 1 + sx * j]] += 1 - fz;
    w[T[i + sx * (j + 1)]] += 1 - fx;
  }
}

float course_friction(const course *c, float x, float z) {
  float w[SURF_KINDS];
  course_surface(c, x, z, w);
  return w[SURF_ICE] * c->friction[SURF_ICE] + w[SURF_ROCK] * c->friction[SURF_ROCK] + w[SURF_SNOW] * c->friction[SURF_SNOW];
}
