/* Terrain layers: the original course tiles (courses/common/ice.rgb,
 * rock.rgb, snow.rgb) with height, normal and roughness maps derived from
 * their own luminance by tools/convert.mjs, tiled every TEX_SCALE = 6 m as
 * course_render.c did. */
#include "game.h"

#include <math.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/resource.h"
#include "cartwheel/shading.h"

#define TEX_SCALE 6.0f

void textures_terrain_layers(cw_terrain *t) {
  cw_terrain_set_layer(t, SURF_ICE, "tiles/ice_ah.png", "tiles/ice_nr.png", 1.0f / TEX_SCALE);
  cw_terrain_set_layer(t, SURF_ROCK, "tiles/rock_ah.png", "tiles/rock_nr.png", 1.0f / TEX_SCALE);
  cw_terrain_set_layer(t, SURF_SNOW, "tiles/snow_ah.png", "tiles/snow_nr.png", 1.0f / TEX_SCALE);
}

/* The "snow" shading part (shaders/snow.mjs) on the terrain's near material:
 * glints, ice sheen and cloud shadows. Its hook reads nothing of the object,
 * so the terrain's patches still batch. */
static struct {
  float sun[4], prm[4], fx[4];
  t3_renderer *r;
  cw_res detail_res;
  t3_texture *detail; /* sky/snowdetail.png: ripple gradient, grain, cloud density (tools/make-sky.mjs) */
} SN;
void textures_snow_features(int mask) { for (int i = 0; i < 4; i++) SN.fx[i] = (mask >> i) & 1 ? 1.0f : 0.0f; }

static int32_t snow_fills, snow_misses, trail_misses, trail_stamps, trail_max, trail_shifts, trail_shift_frame, trail_frames;
static void snow_fill(t3_extension_ctx *c, void *u) {
  (void)u;
  snow_fills++;
  if (!t3_extension_set_uniform(c, "snowSun", SN.sun, 4)) snow_misses++;
  if (!t3_extension_set_uniform(c, "snowParams", SN.prm, 4)) snow_misses++;
  t3_extension_set_uniform(c, "snowFx", SN.fx, 4);
  if (SN.detail) t3_extension_set_texture(c, "snowDetail", SN.detail);
  extern float textures_trail_prm_(int i);
  extern t3_texture *textures_trail_tex_(void);
  float tp[4] = { textures_trail_prm_(0), textures_trail_prm_(1), textures_trail_prm_(2), textures_trail_prm_(3) };
  if (!t3_extension_set_uniform(c, "snowTrailBox", tp, 4)) trail_misses++;
  if (textures_trail_tex_() && !t3_extension_set_texture(c, "snowTrail", textures_trail_tex_())) trail_misses++;
}

static void feather_fill(t3_extension_ctx *c, void *u) { (void)c, (void)u; }
void textures_snow_renderer(t3_renderer *r) {
  if (!r || r == SN.r) return;
  SN.r = r;
  cw_shading_part(r, "feather", feather_fill, NULL);
  cw_shading_part(r, "snow", snow_fill, NULL);
  SN.detail_res = cw_res_load("sky/snowdetail.png", CW_RES_TEXTURE);
  SN.detail = cw_res_texture(SN.detail_res);
  if (SN.detail) SN.detail->wrap_s = SN.detail->wrap_t = T3_REPEAT, SN.detail->min_filter = T3_LINEAR_MIPMAP_LINEAR, SN.detail->mag_filter = T3_LINEAR;
  else cw_warn("snow: sky/snowdetail.png missing");
  cw_debug_i32("snow.fills", &snow_fills);
  cw_debug_i32("snow.misses", &snow_misses);
  cw_debug_i32("snow.trail_misses", &trail_misses);
  cw_debug_i32("trail.stamps", &trail_stamps);
  cw_debug_i32("trail.max", &trail_max);
  cw_debug_i32("trail.shifts", &trail_shifts);
  cw_debug_i32("trail.shift_frame", &trail_shift_frame); /* the frame (textures_trail_frame calls) of the last window shift */
  cw_shading_part_batching(r, "snow", true);
}

void textures_snow_attach(cw_terrain *t) {
  t3_material *m = cw_terrain_material(t);
  if (m && !cw_shading_add(m, "snow")) cw_warn("terrain: the snow shading part did not fit");
}

void textures_snow_set(t3_vec3 to_sun, float glint, float clouds, float drift, float ice, float time) {
  SN.sun[0] = to_sun.x, SN.sun[1] = to_sun.y, SN.sun[2] = to_sun.z, SN.sun[3] = glint;
  SN.prm[0] = time, SN.prm[1] = clouds, SN.prm[2] = drift, SN.prm[3] = ice;
}

/* The slide trail: a window of the course around Tux (TRAIL_N texels of
 * TRAIL_STEP metres) where his belly pressed the snow, stamped every tick he
 * is on the ground and bound to the snow part as "snowTrail": the shader
 * tilts the normal along its gradient into a groove and darkens the packed
 * snow. The window follows him, shifting by whole texels so what was
 * stamped stays put in the world. */
#define TRAIL_N 256
#define TRAIL_STEP 0.3125f /* 80 m across */
static struct {
  uint8_t *px;         /* TRAIL_N * TRAIL_N RGBA (the red channel carries the depth) */
  t3_texture *tex;
  float ox, oz;        /* world x, z of texel (0, 0) */
  bool dirty, valid;
  float prm[4];
} TR;

static void trail_alloc(void) {
  if (TR.px) return;
  TR.tex = t3_data_texture_new_format(TRAIL_N, TRAIL_N, NULL, T3_RGBA_FORMAT, T3_UNSIGNED_BYTE_TYPE);
  TR.px = TR.tex->pixels; /* the texture's own copy: what version++ re-uploads */
  memset(TR.px, 0, (size_t)TRAIL_N * TRAIL_N * 4);
  TR.tex->wrap_s = TR.tex->wrap_t = T3_CLAMP_TO_EDGE;
  TR.tex->min_filter = TR.tex->mag_filter = T3_LINEAR;
  TR.tex->generate_mipmaps = false;
}

void textures_trail_reset(t3_vec3 at) {
  trail_alloc();
  memset(TR.px, 0, (size_t)TRAIL_N * TRAIL_N * 4);
  TR.ox = floorf((at.x - TRAIL_N * TRAIL_STEP * 0.5f) / TRAIL_STEP) * TRAIL_STEP;
  TR.oz = floorf((at.z - TRAIL_N * TRAIL_STEP * 0.5f) / TRAIL_STEP) * TRAIL_STEP;
  TR.dirty = true;
  TR.valid = true;
}

/* slide the window by (dx, dz) texels, keeping what overlaps */
static void trail_shift(int dx, int dz) {
  uint8_t *n = cw_calloc((size_t)TRAIL_N * TRAIL_N * 4, 1, CW_MEM_GAME);
  for (int z = 0; z < TRAIL_N; z++) {
    int sz = z + dz;
    if (sz < 0 || sz >= TRAIL_N) continue;
    for (int x = 0; x < TRAIL_N; x++) {
      int sx = x + dx;
      if (sx < 0 || sx >= TRAIL_N) continue;
      memcpy(n + ((size_t)z * TRAIL_N + x) * 4, TR.px + ((size_t)sz * TRAIL_N + sx) * 4, 4);
    }
  }
  memcpy(TR.px, n, (size_t)TRAIL_N * TRAIL_N * 4);
  cw_free(n);
  TR.ox += dx * TRAIL_STEP, TR.oz += dz * TRAIL_STEP;
}

void textures_trail_stamp(t3_vec3 pos, float radius, float depth) {
  if (!TR.valid) textures_trail_reset(pos);
  trail_stamps++;
  /* keep him inside the middle half of the window */
  float cx = (pos.x - TR.ox) / TRAIL_STEP, cz = (pos.z - TR.oz) / TRAIL_STEP;
  int dx = 0, dz = 0;
  if (cx < TRAIL_N * 0.25f) dx = (int)(cx - TRAIL_N * 0.5f);
  else if (cx > TRAIL_N * 0.75f) dx = (int)(cx - TRAIL_N * 0.5f);
  if (cz < TRAIL_N * 0.25f) dz = (int)(cz - TRAIL_N * 0.5f);
  else if (cz > TRAIL_N * 0.75f) dz = (int)(cz - TRAIL_N * 0.5f);
  if (dx || dz) { trail_shift(dx, dz); cx -= dx, cz -= dz; trail_shifts++; trail_shift_frame = trail_frames; }
  int r = (int)ceilf(radius / TRAIL_STEP) + 1;
  int ix = (int)cx, iz = (int)cz;
  for (int z = iz - r; z <= iz + r; z++) {
    if (z < 0 || z >= TRAIL_N) continue;
    for (int x = ix - r; x <= ix + r; x++) {
      if (x < 0 || x >= TRAIL_N) continue;
      float wx = TR.ox + (x + 0.5f) * TRAIL_STEP - pos.x, wz = TR.oz + (z + 0.5f) * TRAIL_STEP - pos.z;
      float d2 = (wx * wx + wz * wz) / (radius * radius);
      if (d2 >= 1) continue;
      float v = depth * (1 - d2) * (1 - d2);
      uint8_t *t = TR.px + ((size_t)z * TRAIL_N + x) * 4;
      int nv = (int)(v * 255);
      if (nv > t[0]) t[0] = (uint8_t)(nv > 255 ? 255 : nv);
      if (t[0] > trail_max) trail_max = t[0];
    }
  }
  TR.dirty = true;
}

/* before the frame's draws: upload what changed; the part's values */
void textures_trail_frame(float strength) {
  trail_frames++;
  if (!TR.tex) return;
  if (TR.dirty) { TR.tex->version++; TR.dirty = false; }
  TR.prm[0] = TR.ox, TR.prm[1] = TR.oz, TR.prm[2] = 1.0f / (TRAIL_N * TRAIL_STEP), TR.prm[3] = TR.valid ? strength : 0;
}

float textures_trail_prm_(int i) { return TR.prm[i]; }
t3_texture *textures_trail_tex_(void) { trail_alloc(); return TR.tex; }
