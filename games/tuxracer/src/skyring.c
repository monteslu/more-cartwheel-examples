/* The sunny sky: the engine's ProceduralSky draws the sky itself (a crisp
 * gradient and the sun that lights the scene), the original cube's far
 * mountains ride a ring around the camera (assets/sky/sunny_ring.png,
 * tools/make-sky.mjs: the faces resampled by direction, the sky above the
 * peaks faded out), and a high cloud deck drifts over (assets/sky/clouds.png).
 * Both are unlit, fogless, transparent meshes that write no depth, drawn
 * before the rest of the transparent scene; the ring sits inside the far
 * plane and follows the camera, so it never parallaxes. Night and cloudy
 * keep the original cubes as the background. */
#include "game.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/resource.h"
#include "cartwheel/shading.h"

#define RING_RADIUS 2600.0f /* inside the camera's far plane (3000) */
#define RING_E0 -50.0f
#define RING_E1 30.0f
#define CLOUD_HEIGHT 420.0f
#define CLOUD_SIZE 24000.0f /* its edge is 2 degrees up: behind the ring's peaks */

static struct {
  t3_mesh *ring, *clouds, *aurora;
  cw_res ring_res;
  bool on, night;
  t3_renderer *renderer;
  float cloud_prm[4], cloud_sun[4], cloud_cam[4], aurora_prm[4];
} R;

/* the shading parts' values (shaders/clouds.mjs, shaders/aurora.mjs) */
static void clouds_fill(t3_extension_ctx *c, void *u) {
  (void)u;
  t3_extension_set_uniform(c, "cloudParams", R.cloud_prm, 4);
  t3_extension_set_uniform(c, "cloudSun", R.cloud_sun, 4);
  t3_extension_set_uniform(c, "cloudCam", R.cloud_cam, 4);
}
static void aurora_fill(t3_extension_ctx *c, void *u) {
  (void)u;
  t3_extension_set_uniform(c, "auroraParams", R.aurora_prm, 4);
}
void sky_ring_renderer(t3_renderer *r) {
  if (!r || r == R.renderer) return;
  R.renderer = r;
  cw_shading_part(r, "clouds", clouds_fill, NULL);
  cw_shading_part_batching(r, "clouds", true);
  cw_shading_part(r, "aurora", aurora_fill, NULL);
  cw_shading_part_batching(r, "aurora", true);
}

static t3_geometry *band_geometry(float e0, float e1, float radius) {
  enum { SEG = 96, ROWS = 12 };
  int nv = (SEG + 1) * (ROWS + 1);
  float *p = malloc(sizeof(float) * 3 * (size_t)nv), *n = malloc(sizeof(float) * 3 * (size_t)nv), *uv = malloc(sizeof(float) * 2 * (size_t)nv);
  uint32_t *ix = malloc(sizeof(uint32_t) * 6 * SEG * ROWS);
  for (int r = 0; r <= ROWS; r++) {
    float e = TR_DEG(e1 - (e1 - e0) * (float)r / ROWS); /* row 0 at the top, as the image */
    for (int s = 0; s <= SEG; s++) {
      float phi = (float)s / SEG * 6.2831853f; /* 0 = toward -z, increasing toward +x: make-sky.mjs */
      int i = r * (SEG + 1) + s;
      p[i * 3] = sinf(phi) * radius, p[i * 3 + 1] = tanf(e) * radius, p[i * 3 + 2] = -cosf(phi) * radius;
      n[i * 3] = -sinf(phi), n[i * 3 + 1] = 0, n[i * 3 + 2] = cosf(phi);
      uv[i * 2] = (float)s / SEG, uv[i * 2 + 1] = (float)r / ROWS;
    }
  }
  int k = 0;
  for (int r = 0; r < ROWS; r++)
    for (int s = 0; s < SEG; s++) {
      uint32_t a = (uint32_t)(r * (SEG + 1) + s), b = a + 1, c = a + SEG + 1, d = c + 1;
      ix[k++] = a, ix[k++] = c, ix[k++] = b; /* seen from inside */
      ix[k++] = b, ix[k++] = c, ix[k++] = d;
    }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *ap = t3_attribute_new(T3_FLOAT32, p, nv, 3), *an = t3_attribute_new(T3_FLOAT32, n, nv, 3), *au = t3_attribute_new(T3_FLOAT32, uv, nv, 2), *ai = t3_attribute_new(T3_UINT32, ix, k, 1);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, ap);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, an);
  t3_geometry_set_attribute(g, T3_ATTR_UV, au);
  t3_geometry_set_index(g, ai);
  t3_release(ap), t3_release(an), t3_release(au), t3_release(ai);
  free(p), free(n), free(uv), free(ix);
  return g;
}

static t3_material *unlit(t3_texture *t) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  if (t) t3_material_set_map(m, t);
  m->transparent = true;
  m->depth_write = false;
  m->fog = false;
  m->side = T3_DOUBLE_SIDE;
  return m;
}

void sky_ring_init(t3_scene *scene) {
  if (R.ring) return;
  R.ring_res = cw_res_load("sky/sunny_ring.png", CW_RES_TEXTURE);
  t3_texture *ring_tex = cw_res_texture(R.ring_res);
  if (!ring_tex) cw_warn("sky ring: sky/sunny_ring.png missing");
  if (ring_tex) ring_tex->wrap_s = ring_tex->wrap_t = T3_REPEAT; /* one wrapping mode on both axes: the generated programs know "rep" */
  t3_geometry *rg = band_geometry(RING_E0, RING_E1, RING_RADIUS);
  t3_material *rm = unlit(ring_tex);
  rm->color = (t3_color){ 0.84f, 0.87f, 0.93f }; /* the cube was shown linear (darker): keep the mountains from washing out */
  R.ring = t3_mesh_new(rg, rm);
  t3_release(rg);
  R.ring->base.frustum_culled = false;
  R.ring->base.cast_shadow = R.ring->base.receive_shadow = false;
  R.ring->base.render_order = -200; /* after the clouds: the mountains cover the deck's far edge */
  t3_object_add((t3_object *)scene, R.ring);
  t3_geometry *cg = t3_plane_geometry_new(CLOUD_SIZE, CLOUD_SIZE, 1, 1);
  t3_mat4 flat;
  t3_mat4_compose(&flat, (t3_vec3){ 0, 0, 0 }, t3_quat_from_axis_angle((t3_vec3){ 1, 0, 0 }, TR_DEG(90)), (t3_vec3){ 1, 1, 1 }); /* facing down */
  t3_geometry_apply_matrix4(cg, &flat);
  t3_material *cm = unlit(NULL); /* the clouds part draws the deck */
  cw_shading_add(cm, "clouds");
  R.clouds = t3_mesh_new(cg, cm);
  t3_release(cg);
  R.clouds->base.frustum_culled = false;
  R.clouds->base.cast_shadow = R.clouds->base.receive_shadow = false;
  R.clouds->base.render_order = -210; /* the first transparent draw */
  t3_object_add((t3_object *)scene, R.clouds);
  /* the aurora: a band from 7 to 60 degrees up, additive, night only */
  t3_geometry *ag = band_geometry(3, 85, RING_RADIUS * 0.95f);
  t3_material *am = unlit(NULL);
  am->blending = T3_ADDITIVE_BLENDING;
  cw_shading_add(am, "aurora");
  R.aurora = t3_mesh_new(ag, am);
  t3_release(ag);
  R.aurora->base.frustum_culled = false;
  R.aurora->base.cast_shadow = R.aurora->base.receive_shadow = false;
  R.aurora->base.render_order = -195;
  t3_object_add((t3_object *)scene, R.aurora);
  sky_ring_set(false, false);
}

static bool cloudy_mode;
void sky_ring_set(bool sunny, bool night) {
  bool cloudy = !sunny && !night;
  cloudy_mode = cloudy;
  R.on = true; /* every condition: the ring and the deck */
  R.night = night;
  const char *dbg = getenv("TUXRACER_SKY"); /* dev: "noring", "noclouds", "noaurora" hide one */
  if (R.ring) {
    R.ring->base.visible = !(dbg && strstr(dbg, "noring"));
    /* by night the same range under the moon: deep blue; under an overcast: flat grey-blue */
    R.ring->materials[0]->color = night ? (t3_color){ 0.07f, 0.1f, 0.2f } : cloudy ? (t3_color){ 0.5f, 0.53f, 0.6f } : (t3_color){ 0.84f, 0.87f, 0.93f };
  }
  if (R.clouds) R.clouds->base.visible = !night && !(dbg && strstr(dbg, "noclouds"));
  if (R.aurora) R.aurora->base.visible = night && !(dbg && strstr(dbg, "noaurora"));
}

/* per frame: everything centred on the camera; the clouds world-fixed and drifting with the wind */
void sky_ring_update(t3_vec3 cam, t3_vec3 to_sun, float time, bool windy) {
  if (!R.ring) return;
  if (R.on) {
    t3_object_set_position(R.ring, cam.x, cam.y, cam.z);
    t3_object_set_position(R.clouds, cam.x, cam.y + CLOUD_HEIGHT, cam.z);
  }
  if (R.night) t3_object_set_position(R.aurora, cam.x, cam.y, cam.z);
  float w = windy ? 9.0f : 3.5f; /* m/s of drift */
  /* an overcast: the deck nearly closed, lower, its lit side dull */
  R.cloud_prm[0] = time, R.cloud_prm[1] = w, R.cloud_prm[2] = w * 0.35f, R.cloud_prm[3] = cloudy_mode ? 1.0f : windy ? 0.55f : 0.45f;
  R.cloud_sun[0] = to_sun.x, R.cloud_sun[1] = to_sun.y, R.cloud_sun[2] = to_sun.z, R.cloud_sun[3] = cloudy_mode ? 0.62f : 1.0f;
  if (R.on) t3_object_set_position(R.clouds, cam.x, cam.y + (cloudy_mode ? 260.0f : CLOUD_HEIGHT), cam.z);
  R.cloud_cam[0] = cam.x, R.cloud_cam[1] = cam.y, R.cloud_cam[2] = cam.z, R.cloud_cam[3] = CLOUD_SIZE * 0.5f;
  R.aurora_prm[0] = time, R.aurora_prm[1] = 1.0f;
}
