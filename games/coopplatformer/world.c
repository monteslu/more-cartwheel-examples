/* The level's body and picture: Box2D colliders from the tile grid, and the three.c scene (chunk meshes with
 * generated PBR textures, water, lava, spikes, backdrops). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/math.h"
#include "cartwheel/render3d.h"
#include "game.h"
#include <stdio.h>

/* ── physics ──────────────────────────────────────────────────────── */

static cw_body2d g_static;
static struct { int x, y; cw_body2d b; } g_crack[128];
int world_cracks = 0;

static bool is_solid_t(int t) { return t == T_ROCK || t == T_GRASS || t == T_BRICK || t == T_ICE || t == T_BELT; }

void world_build_physics(game *g) {
  cw_world2d_def wd = cw_world2d_default_def();
  wd.gravity = (cw_vec2){ 0, 0 };
  g->world = cw_world2d_create(&wd);
  const level *L = &g->L;
  cw_body2d_def bd = cw_body2d_default_def();
  bd.type = CW_BODY_STATIC;
  g_static = cw_body2d_create(g->world, &bd);
  cw_shape2d_def sd = cw_shape2d_default_def();
  sd.layer = LAYER_WORLD;
  sd.material.friction = 0;
  cw_shape2d_def od = sd;
  od.layer = LAYER_ONEWAY;
  /* merged runs, row by row */
  for (int y = 0; y < WORLD_H; y++) {
    int x = 0;
    while (x < WORLD_W) {
      int t = L->t[y][x];
      if (is_solid_t(t)) {
        int x1 = x;
        while (x1 < WORLD_W && is_solid_t(L->t[y][x1])) x1++;
        float hw = (float)(x1 - x) * 0.5f;
        cw_shape2d_box(g_static, &sd, (cw_vec2){ hw, 0.5f }, (cw_xform2){ { (float)x + hw, (float)y + 0.5f }, { 1, 0 } });
        x = x1;
      } else if (t == T_ONEWAY) {
        int x1 = x;
        while (x1 < WORLD_W && L->t[y][x1] == T_ONEWAY) x1++;
        float hw = (float)(x1 - x) * 0.5f;
        cw_shape2d_box(g_static, &od, (cw_vec2){ hw, 0.12f }, (cw_xform2){ { (float)x + hw, (float)y + 0.88f }, { 1, 0 } });
        x = x1;
      } else x++;
    }
  }
  /* cracked blocks: one body each so a dash can remove them */
  world_cracks = 0;
  for (int y = 0; y < WORLD_H; y++)
    for (int x = 0; x < WORLD_W; x++)
      if (L->t[y][x] == T_CRACK && world_cracks < 128) {
        cw_body2d_def cd = cw_body2d_default_def();
        cd.type = CW_BODY_STATIC;
        cd.position = (cw_pos2){ (double)x + 0.5, (double)y + 0.5 };
        cw_body2d b = cw_body2d_create(g->world, &cd);
        cw_shape2d_box(b, &sd, (cw_vec2){ 0.5f, 0.5f }, (cw_xform2){ { 0, 0 }, { 1, 0 } });
        g_crack[world_cracks].x = x, g_crack[world_cracks].y = y, g_crack[world_cracks].b = b;
        world_cracks++;
      }
  /* doors and crumbling planks: kinematic boxes the rules move out of the way */
  for (int i = 0; i < L->nthings; i++) {
    thing *t = &g->L.things[i];
    if (t->kind != TH_DOOR && t->kind != TH_CRUMBLE) continue;
    float hw = t->kind == TH_DOOR ? t->w * 0.5f : 0.5f, hh = t->kind == TH_DOOR ? t->h * 0.5f : 0.5f;
    cw_body2d_def pd = cw_body2d_default_def();
    pd.type = CW_BODY_KINEMATIC;
    pd.position = (cw_pos2){ t->kind == TH_DOOR ? t->x + hw : t->x + 0.5f, t->y + hh };
    t->body = cw_body2d_create(g->world, &pd);
    cw_shape2d_box(t->body, &sd, (cw_vec2){ hw, hh }, (cw_xform2){ { 0, 0 }, { 1, 0 } });
  }
  /* moving platforms: kinematic boxes, driven by sim_step */
  for (int i = 0; i < L->nplat; i++) {
    cw_body2d_def pd = cw_body2d_default_def();
    pd.type = CW_BODY_KINEMATIC;
    pd.position = (cw_pos2){ L->plats[i].x0, L->plats[i].y0 };
    cw_body2d b = cw_body2d_create(g->world, &pd);
    cw_shape2d_box(b, &sd, (cw_vec2){ L->plats[i].w * 0.5f, 0.2f }, (cw_xform2){ { 0, 0 }, { 1, 0 } });
    g->L.plats[i].body = b;
  }
}

void world_destroy_physics(game *g) {
  if (g->world.id) cw_world2d_destroy(g->world);
  g->world = (cw_world2d){ 0 };
  world_cracks = 0;
}

/* a dash broke the cracked block at tile (x, y): true if there was one */
bool world_break_crack(game *g, int x, int y) {
  for (int i = 0; i < world_cracks; i++)
    if (g_crack[i].x == x && g_crack[i].y == y && g_crack[i].b.id) {
      cw_body2d_destroy(g_crack[i].b);
      g_crack[i].b = (cw_body2d){ 0 };
      g->L.t[y][x] = T_AIR;
      return true;
    }
  return false;
}

/* ── procedural PBR textures: tileable value noise, a height field turned into albedo, normal, roughness, occlusion
 * and (where a surface glows) an emission map ── */

#define TN 256
static float hash2(int x, int y, int s, int per) {
  x = ((x % per) + per) % per, y = ((y % per) + per) % per;
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)s * 83492791u;
  h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
  return (float)(h & 0xffff) / 65535.0f;
}
static float vnoise(float u, float v, int per, int s) {
  float x = u * (float)per, y = v * (float)per;
  int xi = (int)floorf(x), yi = (int)floorf(y);
  float fx = x - (float)xi, fy = y - (float)yi;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  float a = hash2(xi, yi, s, per), b = hash2(xi + 1, yi, s, per), c = hash2(xi, yi + 1, s, per), d = hash2(xi + 1, yi + 1, s, per);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float u, float v, int per, int oct, int s) {
  float sum = 0, amp = 0.5f, tot = 0;
  for (int i = 0; i < oct; i++) {
    sum += amp * vnoise(u, v, per << i, s + i), tot += amp, amp *= 0.5f;
  }
  return sum / tot;
}
static float sstep(float a, float b, float x) {
  float t = (x - a) / (b - a);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
typedef struct texel { float r, g, b, h, rough, ao, er, eg, eb; } texel;

/* value noise stretched into bands: px cells across and py up the 4 m texture, both whole numbers, so it tiles */
static float vband(float u, float v, int px, int py, int s) {
  float x = (u - floorf(u)) * (float)px, y = (v - floorf(v)) * (float)py;
  int ix = (int)x, iy = (int)y;
  float fx = x - (float)ix, fy = y - (float)iy;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
#define VB(a, b) hash2(((a) % px) * 131 + ((b) % py) * 7, ((b) % py), s, 1 << 20)
  float t0 = VB(ix, iy) + (VB(ix + 1, iy) - VB(ix, iy)) * fx, t1 = VB(ix, iy + 1) + (VB(ix + 1, iy + 1) - VB(ix, iy + 1)) * fx;
#undef VB
  return t0 + (t1 - t0) * fy;
}
/* weathered stone: strata, cracks, pits (tinted per region by vertex colour) */
static void gen_rock(float u, float v, texel *t) {
  /* laid down in beds: bands of several thicknesses that wander, each its own tone, soft pits and grit. The lines drawn
   * round noise contours that used to be here read as a pen drawing of a map, half the screen of it */
  float warp = fbm(u, v, 3, 3, 5);
  float bed = vband(u, v + warp * 0.2f, 2, 7, 9), thin = vband(u, v + warp * 0.12f, 3, 23, 10), fine_bed = vband(u, v + warp * 0.08f, 5, 53, 12);
  float n = fbm(u, v, 6, 5, 1), fine = fbm(u, v, 32, 3, 3), grit = vnoise(u, v, 128, 15);
  float k = 0.46f + 0.2f * bed + 0.1f * thin + 0.05f * fine_bed + 0.14f * (n - 0.5f) + 0.1f * (fine - 0.5f) + 0.06f * (grit - 0.5f);
  t->r = k * (0.98f + 0.1f * (bed - 0.5f)), t->g = k * 0.95f, t->b = k * (0.9f - 0.12f * (bed - 0.5f));
  t->h = 0.3f * bed + 0.2f * thin + 0.25f * n + 0.25f * fine;
  t->rough = 0.82f + 0.16f * fine;
  t->ao = 0.8f + 0.2f * bed;
}
/* grass: blades streaked along v, patches of clover and dry tufts */
static void gen_grass(float u, float v, texel *t) {
  float blade = vnoise(u, v, 96, 11) * 0.6f + vnoise(u + 0.13f, v, 48, 12) * 0.4f;
  float patch = fbm(u, v, 5, 4, 14), fine = fbm(u, v, 40, 2, 16);
  float dry = sstep(0.55f, 0.8f, fbm(u, v, 7, 3, 18));
  float lum = 0.35f + 0.65f * blade;
  t->r = mixf(0.07f, 0.34f, lum) + 0.2f * dry, t->g = mixf(0.22f, 0.62f, lum) - 0.08f * dry, t->b = mixf(0.04f, 0.1f, lum);
  float clover = sstep(0.62f, 0.7f, patch) * 0.25f;
  t->r += clover * 0.1f, t->g += clover * 0.2f;
  t->h = blade * 0.6f + fine * 0.4f;
  t->rough = 0.82f - 0.15f * blade;
  t->ao = 0.55f + 0.45f * blade;
}
/* cave stone: cooler, wetter, with a few glinting crystal specks that glow */
static void gen_cave(float u, float v, texel *t) {
  gen_rock(u, v, t);
  /* a full avalanche hash per texel: hash2's mix let neighbouring texels pass together, which drew the specks as
   * rows of white dashes that repeated with the texture */
  uint32_t hx = (uint32_t)(int)(u * TN) * 0x9E3779B1u ^ ((uint32_t)(int)(v * TN) + 0x7F4A7C15u) * 0x85EBCA77u;
  hx ^= hx >> 16, hx *= 0x7FEB352Du, hx ^= hx >> 15, hx *= 0x846CA68Bu, hx ^= hx >> 16;
  float sp = (float)(hx & 0xffff) / 65535.0f;
  float cl = fbm(u, v, 9, 3, 61);
  float speck = sp > 0.9992f ? 1.0f : 0.0f;
  float vein = sstep(0.0f, 0.02f, 0.022f - fabsf(fbm(u, v, 6, 4, 63) - 0.5f)) * sstep(0.58f, 0.7f, cl);
  t->rough = mixf(t->rough, 0.35f, vein);
  t->er = (0.2f * vein + speck) * 0.5f, t->eg = (0.6f * vein + speck) * 0.7f, t->eb = (1.0f * vein + speck) * 0.9f;
  t->b += 0.06f * vein;
}
/* basalt: dark, glassy ridges with glowing seams */
static void gen_basalt(float u, float v, texel *t) {
  gen_rock(u, v, t);
  float seam = fabsf(fbm(u, v, 4, 5, 91) - 0.5f);
  float where = sstep(0.5f, 0.66f, fbm(u, v, 3, 3, 95));
  float glow = (1.0f - sstep(0.0f, 0.02f, seam)) * where;
  float pulse = 0.5f + 0.5f * fbm(u, v, 8, 2, 93);
  float k = 0.26f * t->r;
  t->r = k, t->g = k * 0.85f, t->b = k * 0.85f;
  t->rough = mixf(t->rough * 0.85f, 0.3f, glow);
  t->h -= glow * 0.5f;
  t->er = glow * (1.0f) * (0.5f + 0.7f * pulse), t->eg = glow * 0.32f * (0.5f + 0.7f * pulse), t->eb = glow * 0.04f;
}
/* sandstone brick: courses, chipped edges, moss in the joints */
static void gen_brick(float u, float v, texel *t) {
  float rows = 6, cols = 3;
  float y = v * rows;
  int row = (int)floorf(y);
  float x = u * cols + (float)(row & 1) * 0.5f;
  float fx = x - floorf(x), fy = y - floorf(y);
  float e = fminf(fminf(fx, 1 - fx) * 2.0f, fminf(fy, 1 - fy) * 4.0f);
  float chip = fbm(u, v, 24, 3, 25);
  e += (chip - 0.5f) * 0.1f;
  float edge = e < 0.06f ? 0.0f : (e < 0.18f ? (e - 0.06f) / 0.12f : 1.0f);
  int id = ((int)floorf(x) * 31 + row * 17) & 255;
  float n = fbm(u, v, 8, 4, 22), fine = fbm(u, v, 32, 3, 7);
  float tint = (float)id / 255.0f;
  float c = (0.62f + 0.14f * tint + 0.16f * (n - 0.5f)) * (0.7f + 0.3f * edge);
  float moss = sstep(0.5f, 0.75f, fbm(u, v, 6, 4, 29)) * (1.0f - edge * 0.6f);
  t->r = mixf(c, 0.2f, moss * 0.7f), t->g = mixf(c * 0.9f, 0.4f, moss * 0.7f), t->b = mixf(c * 0.78f, 0.12f, moss * 0.7f);
  t->h = edge * (0.75f + 0.25f * fine) + moss * 0.1f;
  t->rough = 0.85f - 0.1f * edge + 0.1f * moss;
  t->ao = 0.5f + 0.5f * edge;
}
/* planks: four boards across v, grain running along u, dark gaps between them, end joints and nail heads, worn
 * lighter where feet go (the middle of each board) */
static void gen_wood(float u, float v, texel *t) {
  float boards = 4;
  float y = v * boards;
  int bi = (int)floorf(y);
  float fy = y - (float)bi;
  float off = hash2(bi, 0, 131, 64);
  float xu = u * 2.0f + off; /* two board lengths per tile of texture, staggered per board */
  int seg = (int)floorf(xu);
  float fx = xu - (float)seg;
  float edge = fminf(fminf(fy, 1 - fy) * 9.0f, fminf(fx, 1 - fx) * 30.0f);
  edge = edge > 1 ? 1 : edge;
  float tone = hash2(bi, seg, 133, 64);
  float grain = vnoise(u * 2.0f, v * 40.0f + fbm(u, v, 3, 3, 135) * 2.0f, 8, 137);
  float ring = 0.5f + 0.5f * sinf((v * 90.0f + fbm(u, v, 4, 3, 139) * 8.0f) * (1.0f + 0.3f * tone));
  float knot = sstep(0.82f, 0.9f, fbm(u * 1.3f, v * 3.0f, 6, 3, 141));
  float wear = sstep(0.15f, 0.5f, fminf(fy, 1 - fy)) * 0.12f;
  float k = (0.42f + 0.18f * tone + 0.14f * grain + 0.08f * ring + wear) * (0.35f + 0.65f * edge) * (1.0f - 0.45f * knot);
  float nail = 0;
  for (int e = 0; e < 2; e++) {
    float nx = (fx - (e ? 0.95f : 0.05f)) * 30.0f, ny = (fy - 0.5f) * 4.0f;
    if (nx * nx + ny * ny < 0.6f) nail = 1;
  }
  t->r = mixf(k * 0.92f, 0.16f, nail), t->g = mixf(k * 0.62f, 0.15f, nail), t->b = mixf(k * 0.36f, 0.14f, nail);
  t->h = edge * (0.6f + 0.25f * grain + 0.15f * ring) - knot * 0.2f + nail * 0.3f;
  t->rough = mixf(0.82f - 0.12f * wear * 8.0f * 0.12f, 0.45f, nail);
  t->ao = 0.45f + 0.55f * edge;
}
/* lava: a dark crust broken by glowing cracks that the surface flow slides under */
/* glassy pale ice with buried white fractures */
static void gen_ice(float u, float v, texel *t) {
  float n = fbm(u, v, 8, 4, 900), f = fabsf(fbm(u, v, 4, 4, 901) - 0.5f);
  float crack = 1.0f - sstep(0.0f, 0.02f, f);
  t->h = 0.5f + 0.15f * n;
  t->r = 0.72f + 0.18f * crack + 0.08f * n, t->g = 0.86f + 0.1f * crack, t->b = 0.98f;
  t->rough = 0.08f + 0.18f * crack;
  t->ao = 1.0f;
}
/* a rubber belt: dark, matte, ribbed across its run */
static void gen_belt(float u, float v, texel *t) {
  (void)v;
  float rib = 0.5f + 0.5f * cosf(u * 6.2831853f * 8.0f);
  float n = fbm(u, v, 16, 2, 910);
  t->h = 0.3f + 0.4f * rib;
  float c = 0.08f + 0.06f * rib + 0.03f * n;
  t->r = c, t->g = c, t->b = c * 1.1f;
  t->rough = 0.85f;
  t->ao = 0.8f + 0.2f * rib;
}
static void gen_lava(float u, float v, texel *t) {
  float w = fbm(u, v, 4, 4, 51);
  float c1 = fabsf(fbm(u + w * 0.1f, v + w * 0.1f, 5, 5, 53) - 0.5f);
  float c2 = fabsf(fbm(u, v, 11, 3, 57) - 0.5f);
  float hot = 1.0f - sstep(0.0f, 0.045f, c1);
  hot = fmaxf(hot, (1.0f - sstep(0.0f, 0.02f, c2)) * 0.5f);
  float glow = 0.35f + 0.65f * hot;
  t->r = mixf(0.07f, 0.9f, hot), t->g = mixf(0.035f, 0.35f, hot), t->b = mixf(0.03f, 0.04f, hot);
  t->h = (1.0f - hot) * (0.5f + 0.5f * fbm(u, v, 14, 3, 59));
  t->rough = mixf(0.85f, 0.3f, hot);
  t->ao = 1.0f;
  t->er = hot * 1.0f + 0.12f * glow * (1 - hot), t->eg = hot * 0.34f + 0.02f * (1 - hot), t->eb = hot * 0.05f;
}
/* water ripples: a normal map only (its height is two scales of noise) */
static void gen_ripple(float u, float v, texel *t) {
  float a = fbm(u, v, 6, 4, 71), b = fbm(u * 2.0f, v * 2.0f, 8, 3, 73);
  t->r = t->g = t->b = 0.5f;
  t->h = a * 0.6f + b * 0.4f;
  t->rough = 0.1f;
  t->ao = 1.0f;
}

static t3_texture *tex_finish(const uint8_t *px, bool srgb) {
  t3_texture *t = t3_texture_new(TN, TN, px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 8;
  t->color_space = srgb ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
  return t;
}
static uint8_t b8(float x) { return (uint8_t)((x < 0 ? 0 : x > 1 ? 1 : x) * 255.0f + 0.5f); }

/* a material from a generator: albedo, normal, roughness and occlusion maps, plus an emission map when `emissive` */
static t3_material *pbr(void (*gen)(float, float, texel *), float nstrength, float rough_scale, bool emissive, float emis_i) {
  texel *tx = malloc(sizeof *tx * TN * TN);
  uint8_t *a = malloc(TN * TN * 4), *nm = malloc(TN * TN * 4), *ro = malloc(TN * TN * 4), *ao = malloc(TN * TN * 4), *em = emissive ? malloc(TN * TN * 4) : NULL;
  for (int y = 0; y < TN; y++)
    for (int x = 0; x < TN; x++) {
      texel *t = &tx[y * TN + x];
      memset(t, 0, sizeof *t);
      gen(((float)x + 0.5f) / TN, ((float)y + 0.5f) / TN, t);
    }
  for (int y = 0; y < TN; y++)
    for (int x = 0; x < TN; x++) {
      const texel *t = &tx[y * TN + x];
      int i = (y * TN + x) * 4;
      a[i] = b8(t->r), a[i + 1] = b8(t->g), a[i + 2] = b8(t->b), a[i + 3] = 255;
      float hx = tx[y * TN + (x + 1) % TN].h - tx[y * TN + (x + TN - 1) % TN].h;
      float hy = tx[((y + 1) % TN) * TN + x].h - tx[((y + TN - 1) % TN) * TN + x].h;
      float nx = -hx * nstrength, ny = -hy * nstrength, l = sqrtf(nx * nx + ny * ny + 1);
      nm[i] = b8(nx / l * 0.5f + 0.5f), nm[i + 1] = b8(ny / l * 0.5f + 0.5f), nm[i + 2] = b8(1 / l * 0.5f + 0.5f), nm[i + 3] = 255;
      ro[i] = ro[i + 1] = ro[i + 2] = b8(t->rough * rough_scale), ro[i + 3] = 255;
      ao[i] = ao[i + 1] = ao[i + 2] = b8(t->ao), ao[i + 3] = 255;
      if (em) em[i] = b8(t->er), em[i + 1] = b8(t->eg), em[i + 2] = b8(t->eb), em[i + 3] = 255;
    }
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  t3_texture *ts[5] = { tex_finish(a, true), tex_finish(nm, false), tex_finish(ro, false), tex_finish(ao, false), em ? tex_finish(em, true) : NULL };
  static const t3_map_slot SLOTS[5] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP, T3_AO_MAP, T3_EMISSIVE_MAP };
  for (int i = 0; i < 5; i++)
    if (ts[i]) t3_material_set_texture(m, SLOTS[i], ts[i]), t3_release(ts[i]);
  m->normal_scale = (t3_vec2){ 1.0f, 1.0f };
  m->ao_map_intensity = 1.0f;
  m->roughness = 1.0f;
  m->metalness = 0.0f;
  m->vertex_colors = true;
  if (em) m->emissive = (t3_color){ 1, 1, 1 }, m->emissive_intensity = emis_i;
  free(tx), free(a), free(nm), free(ro), free(ao), free(em);
  return m;
}

/* ── mesh builder: vertices with position, normal, colour and uv, quads and triangles, and the solids props are made of ── */

typedef struct mb { float *pos, *nor, *col, *uv; uint32_t *idx; int nv, ni, capv, capi; } mb;
static void mb_room(mb *b, int nv, int ni) {
  if (b->nv + nv > b->capv) {
    while (b->nv + nv > b->capv) b->capv = b->capv ? b->capv * 2 : 4096;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)b->capv);
    b->nor = realloc(b->nor, sizeof(float) * 3 * (size_t)b->capv);
    b->col = realloc(b->col, sizeof(float) * 3 * (size_t)b->capv);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)b->capv);
  }
  if (b->ni + ni > b->capi) {
    while (b->ni + ni > b->capi) b->capi = b->capi ? b->capi * 2 : 6144;
    b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)b->capi);
  }
}
static int mb_vert(mb *b, float x, float y, float z, float nx, float ny, float nz, const float *c, float u, float v) {
  int i = b->nv++;
  b->pos[i * 3] = x, b->pos[i * 3 + 1] = y, b->pos[i * 3 + 2] = z;
  b->nor[i * 3] = nx, b->nor[i * 3 + 1] = ny, b->nor[i * 3 + 2] = nz;
  memcpy(&b->col[i * 3], c, 12);
  b->uv[i * 2] = u, b->uv[i * 2 + 1] = v;
  return i;
}
static void mb_tri_i(mb *b, int a, int c, int d) {
  b->idx[b->ni++] = (uint32_t)a, b->idx[b->ni++] = (uint32_t)c, b->idx[b->ni++] = (uint32_t)d;
}
static void mb_quad(mb *b, const float p[4][3], const float n[3], const float c[4][3], const float uv[4][2]) {
  mb_room(b, 4, 6);
  int v0 = b->nv;
  for (int i = 0; i < 4; i++) mb_vert(b, p[i][0], p[i][1], p[i][2], n[0], n[1], n[2], c[i], uv[i][0], uv[i][1]);
  mb_tri_i(b, v0, v0 + 1, v0 + 2), mb_tri_i(b, v0, v0 + 2, v0 + 3);
}
static t3_geometry *mb_done(mb *b) {
  if (!b->nv) return NULL;
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a = t3_attribute_new(T3_FLOAT32, b->pos, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->nor, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->col, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2);
  t3_geometry_set_attribute(g, T3_ATTR_UV, a), t3_release(a);
  a = t3_attribute_new(T3_UINT32, b->idx, b->ni, 1);
  t3_geometry_set_index(g, a), t3_release(a);
  free(b->pos), free(b->nor), free(b->col), free(b->uv), free(b->idx);
  memset(b, 0, sizeof *b);
  return g;
}

/* an ellipsoid; the colour runs from `lo` at the bottom to `hi` at the top */
static void mb_ellip(mb *b, float cx, float cy, float cz, float rx, float ry, float rz, const float *lo, const float *hi, int seg, int ring) {
  mb_room(b, (seg + 1) * (ring + 1), seg * ring * 6);
  int v0 = b->nv;
  for (int j = 0; j <= ring; j++) {
    float ph = 3.14159265f * (float)j / (float)ring, sy = cosf(ph), sr = sinf(ph);
    float k = 0.5f - 0.5f * sy;
    float c[3] = { mixf(lo[0], hi[0], k), mixf(lo[1], hi[1], k), mixf(lo[2], hi[2], k) };
    for (int i = 0; i <= seg; i++) {
      float th = 6.2831853f * (float)i / (float)seg, cxx = cosf(th) * sr, czz = sinf(th) * sr;
      float nx = cxx / rx, ny = sy / ry, nz = czz / rz, l = sqrtf(nx * nx + ny * ny + nz * nz);
      mb_vert(b, cx + rx * cxx, cy + ry * sy, cz + rz * czz, nx / l, ny / l, nz / l, c, 0, 0);
    }
  }
  for (int j = 0; j < ring; j++)
    for (int i = 0; i < seg; i++) {
      int a = v0 + j * (seg + 1) + i, c2 = a + seg + 1;
      mb_tri_i(b, a, c2, a + 1), mb_tri_i(b, a + 1, c2, c2 + 1);
    }
}
/* a cone or tapered cylinder from the base centre upward (or downward for h < 0), leaning by (lx, lz) at the tip */
static void mb_cone(mb *b, float x, float y, float z, float r0, float r1, float h, float lx, float lz, int sides, const float *c0, const float *c1) {
  mb_room(b, (sides + 1) * 2, sides * 6);
  int v0 = b->nv;
  float sl = (r0 - r1) / (h < 0 ? -h : h);
  for (int k = 0; k < 2; k++)
    for (int i = 0; i <= sides; i++) {
      float th = 6.2831853f * (float)i / (float)sides, cs = cosf(th), sn = sinf(th);
      float r = k ? r1 : r0, f = k ? 1.0f : 0.0f;
      float nx = cs, ny = sl * (h < 0 ? -1.0f : 1.0f), nz = sn, l = sqrtf(nx * nx + ny * ny + nz * nz);
      mb_vert(b, x + cs * r + lx * f, y + h * f, z + sn * r + lz * f, nx / l, ny / l, nz / l, k ? c1 : c0, 0, 0);
    }
  for (int i = 0; i < sides; i++) {
    int a = v0 + i, c2 = a + sides + 1;
    if (h >= 0) mb_tri_i(b, a, c2, a + 1), mb_tri_i(b, a + 1, c2, c2 + 1);
    else mb_tri_i(b, a, a + 1, c2), mb_tri_i(b, a + 1, c2 + 1, c2);
  }
}
/* an axis-aligned box with its own colour at the bottom and the top */
static void mb_box(mb *b, float cx, float cy, float cz, float hx, float hy, float hz, const float *lo, const float *hi) {
  float x0 = cx - hx, x1 = cx + hx, y0 = cy - hy, y1 = cy + hy, z0 = cz - hz, z1 = cz + hz;
  float P[8][3] = { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 }, { x1, y0, z0 }, { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 } };
  static const int F[6][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 5, 0, 3, 6 }, { 1, 4, 7, 2 }, { 3, 2, 7, 6 }, { 5, 4, 1, 0 } };
  static const float N[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
  float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
  for (int f = 0; f < 6; f++) {
    float p[4][3], c[4][3];
    for (int k = 0; k < 4; k++) {
      memcpy(p[k], P[F[f][k]], 12);
      const float *col = p[k][1] > cy ? hi : lo;
      memcpy(c[k], col, 12);
    }
    mb_quad(b, p, N[f], c, uv);
  }
}

/* the same box with uvs in metres / `s` on every face, so a texture keeps its scale on long or thin boxes; `up` turns
 * the texture a quarter so its u runs up the side faces (wood grain along a post) */
static void mb_box_uv(mb *b, float cx, float cy, float cz, float hx, float hy, float hz, const float *lo, const float *hi, float s, bool up) {
  float x0 = cx - hx, x1 = cx + hx, y0 = cy - hy, y1 = cy + hy, z0 = cz - hz, z1 = cz + hz;
  float P[8][3] = { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 }, { x1, y0, z0 }, { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 } };
  static const int F[6][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 5, 0, 3, 6 }, { 1, 4, 7, 2 }, { 3, 2, 7, 6 }, { 5, 4, 1, 0 } };
  static const float N[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
  for (int f = 0; f < 6; f++) {
    float p[4][3], c[4][3], uv[4][2];
    for (int k = 0; k < 4; k++) {
      memcpy(p[k], P[F[f][k]], 12);
      memcpy(c[k], p[k][1] > cy ? hi : lo, 12);
      float a = N[f][0] != 0 ? p[k][2] : p[k][0], bb = N[f][1] != 0 ? p[k][2] : p[k][1];
      uv[k][0] = (up ? bb : a) / s, uv[k][1] = (up ? a : bb) / s; /* up: the grain runs up the box */
    }
    mb_quad(b, p, N[f], c, uv);
  }
}

static float hh(int x, int y, int s) { return hash2(x, y, s, 1 << 20); }
static void rgb(float *o, float r, float g, float bl) { o[0] = r, o[1] = g, o[2] = bl; }
static void shade(float *o, const float *c, float k) { o[0] = c[0] * k, o[1] = c[1] * k, o[2] = c[2] * k; }

/* ── the terrain ───────────────────────────────────────────────────── */

enum { MT_ROCK, MT_GRASS, MT_CAVE, MT_BRICK, MT_BASALT, MT_PROP, MT_GLOW, MT_WOOD, MT_ICE, MT_BELT, MT_COUNT };
/* stone tint per region (the texture is grey-brown, the vertex colour moves it) and the colour of a top surface */
static const float STONE[BIOMES][3] = { { 0.95f, 0.72f, 0.55f }, { 0.55f, 0.66f, 0.9f }, { 1.0f, 0.88f, 0.66f }, { 0.78f, 0.88f, 1.02f }, { 1.1f, 0.85f, 0.85f } };
static const float TOPS[BIOMES][3] = { { 0.85f, 1.0f, 0.7f }, { 0.55f, 0.7f, 0.95f }, { 1.0f, 0.9f, 0.7f }, { 0.95f, 1.0f, 1.05f }, { 1.2f, 0.9f, 0.85f } };

/* world-space noise, one cell per unit at f = 1, for colour that runs smoothly across tiles */
static float wn(float x, float y, float f, int s) { return vnoise(x * f / 4096.0f + 0.5f, y * f / 4096.0f + 0.5f, 4096, s); }
/* the stone colour at a corner of the tile grid, so neighbouring faces agree and nothing reads as a square: a broad
 * light/dark wash, a second warmer or cooler hue in patches, and a darkening with depth under the surface (counted
 * from this corner up, over the two columns that share it) */
static void corner_col(const level *L, int bi, float vx, float vy, float *out) {
  const float *st = STONE[bi];
  float broad = wn(vx, vy, 0.09f, 301), mid = wn(vx, vy, 0.31f, 302), hue = sstep(0.45f, 0.75f, wn(vx, vy, 0.12f, 303));
  float var = 0.72f + 0.34f * broad + 0.12f * (mid - 0.5f);
  int ix = (int)vx, iy = (int)vy, up = 0, cnt = 0;
  for (int cx = ix - 1; cx <= ix; cx++) {
    if (cx < 0 || cx >= WORLD_W) continue;
    int u = 0;
    while (iy + u < WORLD_H && u < 18 && world_solid(L, cx, iy + u)) u++;
    up += u, cnt++;
  }
  float depth = cnt ? (float)up / (float)cnt : 0;
  /* falls away with depth: nearly full for the first two tiles, then down to about a seventh by nine */
  float dk = 1.0f - 0.86f * sstep(1.0f, 9.0f, depth);
  static const float HUE2[BIOMES][3] = { { 0.75f, 0.62f, 0.5f }, { 0.42f, 0.5f, 0.78f }, { 0.85f, 0.8f, 0.6f }, { 0.62f, 0.72f, 0.92f }, { 0.6f, 0.38f, 0.36f } };
  /* a band per region just under the surface: dark loam and paler clay in the meadow, and so on */
  static const float TOPB[BIOMES][3] = { { 0.5f, 0.32f, 0.2f }, { 0.34f, 0.4f, 0.62f }, { 0.78f, 0.66f, 0.46f }, { 0.92f, 0.98f, 1.08f }, { 0.5f, 0.34f, 0.32f } };
  float loam = 1.0f - sstep(0.7f, 2.2f, depth);
  for (int c = 0; c < 3; c++) out[c] = mixf(mixf(st[c], HUE2[bi][c], hue * 0.6f), TOPB[bi][c], loam * 0.8f) * var * dk;
}

static int tile_mat(const level *L, int x, int y) {
  int t = L->t[y][x], bi = L->biome[x];
  if (t == T_BRICK) return MT_BRICK;
  if (t == T_ICE) return MT_ICE;
  if (t == T_BELT) return MT_BELT;
  return bi == BIOME_CAVES ? MT_CAVE : bi == BIOME_VOLCANO ? MT_BASALT : MT_ROCK;
}

#define DEPTH_FRONT 1.5f
#define DEPTH_BACK -1.5f
#define CHUNK 16

/* the uv of a wall corner: the 4 m texture, its coordinates wandering with smooth noise so the same crack, blob or
 * glowing seam does not come back every 4 m across a wall (shared by neighbouring tiles, so no seam). Laid brick
 * keeps its courses straight. */
static bool g_plain_bug;
static void face_uv(float x, float y, int mt, float *uv) {
  float wx = 0, wy = 0;
  if (mt != MT_BRICK && !g_plain_bug) {
    wx = 5.0f * (wn(x, y, 0.07f, 501) - 0.5f) + 1.6f * (wn(x, y, 0.19f, 502) - 0.5f);
    wy = 5.0f * (wn(x, y, 0.07f, 503) - 0.5f) + 1.6f * (wn(x, y, 0.19f, 504) - 0.5f);
  }
  uv[0] = (x + wx) / 4, uv[1] = (y + wy) / 4;
}
/* the repetition audit: percent of sampled wall corners whose texture phase comes back within 5 cm one 4 m repeat on */
int world_uv_repeat(void) {
  int same = 0, n = 0;
  for (int y = 2; y < 60; y += 3)
    for (int x = 3; x < 280; x += 7) {
      float a[2], b[2];
      face_uv((float)x, (float)y, MT_ROCK, a), face_uv((float)x + 4, (float)y, MT_ROCK, b);
      float du = (b[0] - a[0]) - 1.0f, dv = b[1] - a[1];
      if (du * du + dv * dv < (0.05f / 4) * (0.05f / 4)) same++;
      n++;
    }
  return same * 100 / n;
}
/* a pit's walls: how much light is left d metres under its lip (the far wall is in shade: half at the lip, nearly none by
 * two metres), so the hole reads darker than any floor beside it */
static bool g_pit_old; /* COOP_PIT_OLD: the flat pit box, the pit-art check's control */
static float pit_dark(float d) { return 0.03f + 0.52f * (1.0f - sstep(0.0f, 2.0f, d)); }
static void tile_faces(const level *L, int x, int y, mb *bs) {
  int bi = L->biome[x];
  int mt = tile_mat(L, x, y);
  mb *b = &bs[mt];
  float fx = (float)x, fy = (float)y;
  const float *st = STONE[bi], *tp = TOPS[bi];
  bool open_above = !world_solid(L, x, y + 1);
  bool grass_top = open_above && bi == BIOME_MEADOW && mt == MT_ROCK && L->t[y][x] != T_ICE;
  /* a different dark per tile so the wall is not a repeating pattern, plus a fall-off with depth under the surface */
  int below = 0;
  while (y - below > 0 && below < 6 && world_solid(L, x, y - below - 1)) below++;
  float depth_k = 1.0f;
  { int up = 0; while (y + up + 1 < WORLD_H && world_solid(L, x, y + up + 1) && up < 5) up++; depth_k = 1.0f - 0.06f * (float)up; }
  float var = 0.86f + 0.14f * wn(fx + 0.5f, fy + 0.5f, 0.2f, 5);
  float cs[3] = { st[0] * var * depth_k, st[1] * var * depth_k, st[2] * var * depth_k };
  float ct[3] = { tp[0], tp[1], tp[2] };
  /* front face (toward the camera) */
  {
    float p[4][3] = { { fx, fy, DEPTH_FRONT }, { fx + 1, fy, DEPTH_FRONT }, { fx + 1, fy + 1, DEPTH_FRONT }, { fx, fy + 1, DEPTH_FRONT } };
    float n[3] = { 0, 0, 1 };
    /* colour per corner (corner_col): smooth across tiles; a lit lip where the top is open, a little occlusion where
     * the tile below is open (a ledge underside). A per-tile colour drew visible squares on big walls. */
    float c[4][3];
    corner_col(L, bi, fx, fy, c[0]), corner_col(L, bi, fx + 1, fy, c[1]), corner_col(L, bi, fx + 1, fy + 1, c[2]), corner_col(L, bi, fx, fy + 1, c[3]);
    bool open_below = y > 0 && !world_solid(L, x, y - 1);
    for (int k = 0; k < 4; k++) {
      float m = (k >= 2 && open_above) ? 1.12f : (k < 2 && open_below) ? 0.8f : 1.0f;
      shade(c[k], c[k], m);
    }
    float uv[4][2];
    face_uv(fx, fy, mt, uv[0]), face_uv(fx + 1, fy, mt, uv[1]), face_uv(fx + 1, fy + 1, mt, uv[2]), face_uv(fx, fy + 1, mt, uv[3]);
    mb_quad(b, p, n, c, uv);
  }
  if (open_above) { /* top */
    mb *tb = grass_top ? &bs[MT_GRASS] : b;
    float p[4][3] = { { fx, fy + 1, DEPTH_FRONT }, { fx + 1, fy + 1, DEPTH_FRONT }, { fx + 1, fy + 1, DEPTH_BACK }, { fx, fy + 1, DEPTH_BACK } };
    float n[3] = { 0, 1, 0 };
    float c[4][3];
    const float *tc = grass_top ? ct : (bi == BIOME_VOLCANO ? ct : cs);
    float g1[3], g2[3];
    shade(g1, tc, 1.0f), shade(g2, tc, 0.85f);
    memcpy(c[0], g1, 12), memcpy(c[1], g1, 12), memcpy(c[2], g2, 12), memcpy(c[3], g2, 12);
    float uv[4][2] = { { fx / 3, 0.0f }, { (fx + 1) / 3, 0.0f }, { (fx + 1) / 3, 1.0f }, { fx / 3, 1.0f } };
    mb_quad(tb, p, n, c, uv);
  }
  if (y > 0 && !world_solid(L, x, y - 1)) { /* underside */
    float p[4][3] = { { fx, fy, DEPTH_BACK }, { fx + 1, fy, DEPTH_BACK }, { fx + 1, fy, DEPTH_FRONT }, { fx, fy, DEPTH_FRONT } };
    float n[3] = { 0, -1, 0 };
    float c[4][3];
    for (int i = 0; i < 4; i++) shade(c[i], cs, 0.7f);
    float uv[4][2] = { { fx / 4, 0 }, { (fx + 1) / 4, 0 }, { (fx + 1) / 4, 0.75f }, { fx / 4, 0.75f } };
    mb_quad(b, p, n, c, uv);
  }
  /* a side wall that faces a pit goes dark with the depth under the lip, as the pit's far wall does */
  int surf = y;
  while (surf < WORLD_H && world_solid(L, x, surf)) surf++;
  if (x > 0 && !world_solid(L, x - 1, y)) { /* left side */
    float p[4][3] = { { fx, fy, DEPTH_BACK }, { fx, fy, DEPTH_FRONT }, { fx, fy + 1, DEPTH_FRONT }, { fx, fy + 1, DEPTH_BACK } };
    float n[3] = { -1, 0, 0 };
    float c[4][3];
    for (int i = 0; i < 4; i++) shade(c[i], cs, 0.9f);
    if (L->ground[x - 1] == 0 && L->t[2][x - 1] != T_LAVA && !g_pit_old)
      for (int i = 0; i < 4; i++) shade(c[i], c[i], pit_dark((float)surf - p[i][1]));
    float uv[4][2] = { { 0, fy / 4 }, { 0.75f, fy / 4 }, { 0.75f, (fy + 1) / 4 }, { 0, (fy + 1) / 4 } };
    mb_quad(L->ground[x - 1] == 0 && L->t[2][x - 1] != T_LAVA && !g_pit_old && (mt == MT_CAVE || mt == MT_BASALT) ? &bs[MT_ROCK] : b, p, n, c, uv); /* no glowing seams down a pit */
  }
  if (x + 1 < WORLD_W && !world_solid(L, x + 1, y)) { /* right side */
    float p[4][3] = { { fx + 1, fy, DEPTH_FRONT }, { fx + 1, fy, DEPTH_BACK }, { fx + 1, fy + 1, DEPTH_BACK }, { fx + 1, fy + 1, DEPTH_FRONT } };
    float n[3] = { 1, 0, 0 };
    float c[4][3];
    for (int i = 0; i < 4; i++) shade(c[i], cs, 0.9f);
    if (L->ground[x + 1] == 0 && L->t[2][x + 1] != T_LAVA && !g_pit_old)
      for (int i = 0; i < 4; i++) shade(c[i], c[i], pit_dark((float)surf - p[i][1]));
    float uv[4][2] = { { 0, fy / 4 }, { 0.75f, fy / 4 }, { 0.75f, (fy + 1) / 4 }, { 0, (fy + 1) / 4 } };
    mb_quad(L->ground[x + 1] == 0 && L->t[2][x + 1] != T_LAVA && !g_pit_old && (mt == MT_CAVE || mt == MT_BASALT) ? &bs[MT_ROCK] : b, p, n, c, uv);
  }
}

/* ── where scenery stands: every prop that rests on the ground is seated on the surface under its whole footprint
 * (the tile tops in front, the backdrop's 3D ground behind) and recorded, so world_scenery_check can measure each one
 * against the drawn ground. A prop whose footprint runs off a ledge, onto a step, a breakable block or a slope too
 * steep is not placed. ── */

typedef struct placed { float x, y, z, rx, rz; int layer; } placed; /* layer -1: the tiles; 0..3: a backdrop layer */
enum { MAX_PLACED = 8192 };
static placed g_placed[MAX_PLACED];
static int g_nplaced;
static void place_rec(int layer, float x, float y, float z, float rx, float rz) {
  if (g_nplaced < MAX_PLACED) g_placed[g_nplaced++] = (placed){ x, y, z, rx, rz, layer };
}
/* the column's floor at height `top` can carry a prop: solid, not breakable, open above, no hazard on it */
static bool fg_floor(const level *L, int c, float top) {
  int ty = (int)floorf(top + 0.5f) - 1;
  if (c < 0 || c >= WORLD_W || ty < 0 || ty + 1 >= WORLD_H) return false;
  int t = L->t[ty][c], up = L->t[ty + 1][c];
  if (!(t == T_ROCK || t == T_GRASS || t == T_BRICK) || world_solid(L, c, ty + 1)) return false;
  return up != T_SPIKE && up != T_WATER && up != T_LAVA;
}
/* seat a prop of half extent (rx, rz) on the tile tops at `top`; small props slide inside their own tile when the
 * wanted spot overhangs an edge, bigger ones are dropped there */
static bool fg_seat(const level *L, float *x, float top, float *z, float rx, float rz, float tile_x) {
  float lim = DEPTH_FRONT - 0.04f - rz;
  if (lim < 0) return false;
  *z = fmaxf(-lim, fminf(lim, *z));
  for (int pass = 0; pass < 2; pass++) {
    bool ok = true;
    for (int c = (int)floorf(*x - rx); c <= (int)floorf(*x + rx) && ok; c++) ok = fg_floor(L, c, top);
    if (ok) {
      place_rec(-1, *x, top, *z, rx, rz);
      return true;
    }
    if (pass || rx >= 0.45f) return false;
    *x = fmaxf(tile_x + rx + 0.01f, fminf(tile_x + 1.0f - rx - 0.01f, *x));
  }
  return false;
}

/* ── props: grass and flowers, mushrooms and crystals, vines and rubble, spires and ash rock ──
 * Everything goes into two meshes per chunk: lit solids (vertex colours) and glowing parts (an HDR colour that blooms). */

static void grass_tuft(mb *p, float x, float y, float z, int seed, float scale) {
  for (int k = 0; k < 5; k++) {
    float a = (float)k * 1.26f + hh(seed, k, 1) * 2.0f, h = (0.28f + 0.32f * hh(seed, k, 2)) * scale;
    float lean = (hh(seed, k, 3) - 0.5f) * 0.5f * scale;
    float c0[3], c1[3];
    rgb(c0, 0.06f, 0.2f, 0.03f), rgb(c1, 0.3f + 0.2f * hh(seed, k, 4), 0.62f, 0.12f);
    mb_cone(p, x + cosf(a) * 0.06f * scale, y, z + sinf(a) * 0.06f * scale, 0.045f * scale, 0.004f, h, lean * cosf(a), lean * 0.3f, 4, c0, c1);
  }
}
static void flower(mb *p, mb *g, float x, float y, float z, int seed) {
  float h = 0.3f + 0.3f * hh(seed, 1, 6);
  float c0[3], c1[3];
  rgb(c0, 0.05f, 0.18f, 0.03f), rgb(c1, 0.12f, 0.4f, 0.06f);
  mb_cone(p, x, y, z, 0.018f, 0.012f, h, (hh(seed, 2, 6) - 0.5f) * 0.1f, 0, 4, c0, c1);
  static const float PET[5][3] = { { 1.0f, 0.25f, 0.3f }, { 1.0f, 0.82f, 0.2f }, { 0.95f, 0.95f, 1.0f }, { 0.65f, 0.45f, 1.0f }, { 1.0f, 0.55f, 0.15f } };
  const float *pc = PET[(int)(hh(seed, 3, 6) * 5) % 5];
  float lo[3] = { pc[0] * 0.7f, pc[1] * 0.7f, pc[2] * 0.7f }, mid[3];
  for (int k = 0; k < 5; k++) {
    float a = (float)k * 1.2566f;
    mb_ellip(p, x + cosf(a) * 0.07f, y + h, z + sinf(a) * 0.07f, 0.055f, 0.03f, 0.055f, lo, pc, 6, 4);
  }
  rgb(mid, 1.0f, 0.8f, 0.2f);
  mb_ellip(p, x, y + h + 0.01f, z, 0.04f, 0.035f, 0.04f, mid, mid, 6, 4);
  (void)g;
}
static void rock_pile(mb *p, float x, float y, float z, int seed, const float *base, float s) {
  for (int k = 0; k < 3; k++) {
    float rx = (0.16f + 0.2f * hh(seed, k, 8)) * s, ry = rx * (0.55f + 0.3f * hh(seed, k, 9));
    float lo[3], hi[3];
    shade(lo, base, 0.55f + 0.2f * hh(seed, k, 10)), shade(hi, base, 0.95f);
    mb_ellip(p, x + (hh(seed, k, 11) - 0.5f) * 0.6f * s, y + ry * 0.45f, z + (hh(seed, k, 12) - 0.5f) * 0.4f, rx, ry, rx * 0.9f, lo, hi, 8, 5);
  }
}
/* head office, frozen: what was left at the desks when the cold came. Each stands on its floor (y) at z, with a cap of
 * snow; the water cooler's bottle glows a little (glow), the copier's panel too */
static void office_prop(mb *p, mb *g, float x, float y, float z, int seed, int kind) {
  float sn0[3] = { 0.78f, 0.84f, 0.95f }, sn1[3] = { 1.0f, 1.02f, 1.08f };
  float gr0[3] = { 0.32f, 0.34f, 0.38f }, gr1[3] = { 0.55f, 0.58f, 0.62f }, bg0[3] = { 0.52f, 0.48f, 0.4f }, bg1[3] = { 0.78f, 0.74f, 0.64f };
  float dk[3] = { 0.08f, 0.09f, 0.1f };
  if (kind == 0) { /* a desk: a top on two pedestals, a dead monitor on it */
    mb_box(p, x - 0.42f, y + 0.36f, z, 0.14f, 0.36f, 0.3f, gr0, gr1);
    mb_box(p, x + 0.42f, y + 0.36f, z, 0.14f, 0.36f, 0.3f, gr0, gr1);
    mb_box(p, x, y + 0.75f, z, 0.62f, 0.04f, 0.34f, bg0, bg1);
    mb_box(p, x + 0.1f * (hh(seed, 1, 900) - 0.5f), y + 0.98f, z - 0.05f, 0.2f, 0.15f, 0.03f, dk, gr0);
    mb_box(p, x, y + 0.8f, z - 0.05f, 0.05f, 0.03f, 0.05f, dk, gr0);
    mb_box(p, x, y + 0.81f, z + 0.02f, 0.6f, 0.025f, 0.3f, sn0, sn1); /* snow on the desk */
  } else if (kind == 1) { /* a filing cabinet, four drawers, one left open */
    mb_box(p, x, y + 0.66f, z, 0.24f, 0.66f, 0.28f, gr0, gr1);
    for (int d = 0; d < 4; d++) mb_box(p, x, y + 0.18f + 0.32f * (float)d, z + 0.285f + (d == 2 ? 0.18f : 0), 0.2f, 0.12f, (d == 2 ? 0.19f : 0.01f), gr1, gr1);
    mb_box(p, x, y + 1.34f, z, 0.25f, 0.03f, 0.29f, sn0, sn1);
  } else if (kind == 2) { /* a water cooler, its bottle frozen solid and faintly blue */
    float bl0[3] = { 0.3f, 0.6f, 1.1f }, bl1[3] = { 0.6f, 0.9f, 1.4f };
    mb_box(p, x, y + 0.45f, z, 0.17f, 0.45f, 0.17f, sn0, sn1);
    mb_cone(g, x, y + 0.9f, z, 0.15f, 0.12f, 0.42f, 0, 0, 10, bl0, bl1);
  } else { /* a copier, its panel still lit */
    float pn0[3] = { 0.2f, 0.9f, 0.5f }, pn1[3] = { 0.4f, 1.3f, 0.8f };
    mb_box(p, x, y + 0.45f, z, 0.42f, 0.45f, 0.32f, sn0, gr1);
    mb_box(p, x, y + 0.94f, z, 0.4f, 0.04f, 0.3f, gr0, gr1);
    mb_box(g, x + 0.25f, y + 0.82f, z + 0.325f, 0.1f, 0.05f, 0.005f, pn0, pn1);
  }
}
static void bush(mb *p, float x, float y, float z, int seed, float s) {
  for (int k = 0; k < 4; k++) {
    float r = (0.35f + 0.25f * hh(seed, k, 13)) * s;
    float lo[3], hi[3];
    rgb(lo, 0.05f, 0.18f, 0.04f), rgb(hi, 0.18f + 0.12f * hh(seed, k, 14), 0.5f, 0.1f);
    mb_ellip(p, x + (float)(k - 1.5f) * 0.38f * s, y + r * 0.7f, z + (hh(seed, k, 15) - 0.5f) * 0.3f, r, r * 0.85f, r, lo, hi, 10, 7);
  }
}
static void tree(mb *p, float x, float y, float z, int seed, float s) {
  float c0[3], c1[3];
  rgb(c0, 0.2f, 0.1f, 0.05f), rgb(c1, 0.3f, 0.17f, 0.08f);
  float h = (3.2f + 2.0f * hh(seed, 1, 20)) * s;
  mb_cone(p, x, y - 0.2f, z, 0.32f * s, 0.2f * s, h, (hh(seed, 2, 20) - 0.5f) * 0.5f, 0, 8, c0, c1);
  for (int k = 0; k < 4; k++) {
    float r = (1.5f - 0.2f * (float)k) * s;
    float lo[3], hi[3];
    rgb(lo, 0.04f, 0.17f, 0.04f), rgb(hi, 0.16f + 0.08f * hh(seed, k, 21), 0.5f, 0.1f);
    mb_ellip(p, x + (hh(seed, k, 22) - 0.5f) * 0.8f * s, y + h * (0.7f + 0.17f * (float)k), z + (hh(seed, k, 23) - 0.5f) * 0.6f, r, r * 0.8f, r * 0.9f, lo, hi, 12, 8);
  }
}
static void mushroom(mb *p, mb *g, float x, float y, float z, int seed, float s) {
  float c0[3], c1[3];
  rgb(c0, 0.5f, 0.55f, 0.65f), rgb(c1, 0.75f, 0.8f, 0.9f);
  float h = (0.4f + 0.5f * hh(seed, 1, 30)) * s;
  mb_cone(p, x, y, z, 0.07f * s, 0.05f * s, h, (hh(seed, 2, 30) - 0.5f) * 0.1f, 0, 6, c0, c1);
  static const float CAP[3][3] = { { 0.3f, 1.2f, 2.2f }, { 0.4f, 2.0f, 1.2f }, { 1.6f, 0.6f, 2.0f } };
  const float *cc = CAP[(int)(hh(seed, 3, 30) * 3) % 3];
  float dim[3] = { cc[0] * 0.5f, cc[1] * 0.5f, cc[2] * 0.5f };
  mb_ellip(g, x, y + h, z, 0.28f * s, 0.16f * s, 0.28f * s, dim, cc, 10, 6);
}
static void crystal_cluster(mb *g, mb *p, float x, float y, float z, int seed, float s, int hue, bool down) {
  static const float COL[4][3] = { { 0.5f, 1.6f, 3.0f }, { 0.7f, 3.0f, 1.8f }, { 2.6f, 0.7f, 3.0f }, { 3.0f, 1.4f, 0.5f } };
  const float *c = COL[hue & 3];
  float dim[3] = { c[0] * 0.35f, c[1] * 0.35f, c[2] * 0.35f };
  int n = 3 + (int)(hh(seed, 1, 40) * 3);
  float dir = down ? -1.0f : 1.0f;
  for (int k = 0; k < n; k++) {
    float h = (0.5f + 1.1f * hh(seed, k, 41)) * s, lean = (hh(seed, k, 42) - 0.5f) * 0.9f * s;
    mb_cone(g, x + (float)(k - n / 2) * 0.17f * s, y, z + (hh(seed, k, 43) - 0.5f) * 0.3f, 0.11f * s, 0.015f, h * dir, lean, 0, 5, dim, c);
  }
  float rb[3] = { 0.2f, 0.22f, 0.3f };
  rock_pile(p, x, y + (down ? -0.1f : 0.0f), z, seed, rb, 0.6f * s);
}
static void stalactite(mb *p, float x, float y, float z, int seed, float s) {
  float c0[3], c1[3];
  rgb(c0, 0.28f, 0.32f, 0.42f), rgb(c1, 0.12f, 0.14f, 0.22f);
  mb_cone(p, x, y, z, (0.18f + 0.18f * hh(seed, 1, 50)) * s, 0.01f, -(0.8f + 1.6f * hh(seed, 2, 50)) * s, 0, 0, 7, c0, c1);
}
static void vine(mb *p, float x, float y, float z, int seed, float len) {
  float c0[3], c1[3];
  rgb(c0, 0.08f, 0.2f, 0.06f), rgb(c1, 0.18f, 0.4f, 0.1f);
  mb_cone(p, x, y, z, 0.03f, 0.012f, -len, 0.15f * (hh(seed, 1, 60) - 0.5f), 0, 4, c0, c1);
  for (int k = 0; k < 4; k++) {
    float ly = y - len * (0.2f + 0.2f * (float)k);
    float lo[3], hi[3];
    rgb(lo, 0.06f, 0.22f, 0.05f), rgb(hi, 0.2f, 0.5f, 0.1f);
    mb_ellip(p, x + (k & 1 ? 0.1f : -0.1f), ly, z, 0.09f, 0.04f, 0.06f, lo, hi, 6, 4);
  }
}
/* a column on its plinth; a broken one has a broken top and, when rubble_y is a number (the ground where the rubble lies,
 * seated on its own), a pile of rubble beside it */
static void column_prop(mb *p, float x, float y, float z, float h, int seed, bool broken, float rubble_y) {
  float lo[3], hi[3], cap[3];
  rgb(lo, 0.6f, 0.52f, 0.4f), rgb(hi, 0.85f, 0.76f, 0.6f), rgb(cap, 0.9f, 0.82f, 0.66f);
  mb_box(p, x, y + 0.1f, z, 0.55f, 0.3f, 0.55f, lo, cap);
  mb_cone(p, x, y + 0.4f, z, 0.38f, 0.34f, h, 0, 0, 12, lo, hi);
  if (!broken) mb_box(p, x, y + 0.4f + h + 0.15f, z, 0.55f, 0.15f, 0.55f, hi, cap);
  else {
    mb_cone(p, x, y + 0.4f + h, z, 0.34f, 0.12f, 0.3f, 0.1f, 0, 7, hi, lo);
    if (!isnan(rubble_y)) rock_pile(p, x + 0.8f, rubble_y, z + 0.3f, seed, lo, 0.8f);
  }
}
/* the fire stands in something: in the caves an iron cresset on a post set in a stone footing, in the ruins a bronze
 * bowl on a carved sandstone pedestal. Both stand on the floor (seated and checked like every other prop); the
 * coals glow in the basket and the flame particles rise from them. Returns the height of the coals above `y`. */
static float brazier(mb *p, mb *g, float x, float y, float z, int seed, bool ruins) {
  float lo[3], hi[3], c0[3], c1[3], coal0[3], coal1[3];
  rgb(coal0, 2.4f, 0.7f, 0.12f), rgb(coal1, 4.0f, 1.8f, 0.45f);
  float jit = 0.92f + 0.16f * hh(seed, 1, 700);
  if (!ruins) {
    rgb(lo, 0.12f, 0.13f, 0.16f), rgb(hi, 0.22f, 0.23f, 0.27f);
    mb_cone(p, x, y, z, 0.3f, 0.22f, 0.16f, 0, 0, 7, lo, hi);                  /* rough stone footing */
    mb_box(p, x, y + 0.17f, z, 0.2f, 0.015f, 0.2f, hi, hi);                     /* its flat top */
    rgb(c0, 0.045f, 0.04f, 0.038f), rgb(c1, 0.09f, 0.08f, 0.07f);               /* wrought iron, blackened */
    float h = 1.25f * jit, top = 0.17f + h;
    mb_cone(p, x, y + 0.17f, z, 0.055f, 0.045f, h, 0, 0, 8, c0, c1);           /* the post */
    for (int k = 0; k < 3; k++) {                                                /* three legs braced into the footing */
      float a = 6.2831853f * ((float)k / 3.0f + 0.1f * hh(seed, k, 701));
      mb_cone(p, x + 0.2f * cosf(a), y + 0.17f, z + 0.2f * sinf(a), 0.025f, 0.02f, 0.5f, -0.17f * cosf(a), -0.17f * sinf(a), 5, c0, c1);
    }
    mb_cone(p, x, y + 0.17f + 0.5f * h, z, 0.08f, 0.08f, 0.06f, 0, 0, 8, c1, c1); /* a collar */
    mb_cone(p, x, y + top - 0.05f, z, 0.09f, 0.09f, 0.07f, 0, 0, 8, c1, c1);      /* the socket */
    mb_cone(p, x, y + top, z, 0.07f, 0.27f, 0.3f, 0, 0, 10, c0, c1);              /* the basket */
    mb_cone(p, x, y + top + 0.29f, z, 0.285f, 0.285f, 0.035f, 0, 0, 10, c1, c1);  /* its rim */
    for (int k = 0; k < 5; k++) {                                                /* iron tines over the rim */
      float a = 6.2831853f * (float)k / 5.0f + hh(seed, 2, 702);
      mb_cone(p, x + 0.27f * cosf(a), y + top + 0.3f, z + 0.27f * sinf(a), 0.018f, 0.006f, 0.16f, 0.05f * cosf(a), 0.05f * sinf(a), 4, c1, c0);
    }
    mb_ellip(g, x, y + top + 0.22f, z, 0.22f, 0.08f, 0.22f, coal0, coal1, 10, 5);
    return top + 0.3f;
  }
  rgb(lo, 0.34f, 0.27f, 0.19f), rgb(hi, 0.56f, 0.46f, 0.33f);                  /* sandstone, the ruins' brick */
  float sh = 0.8f * jit;
  mb_box(p, x, y + 0.07f, z, 0.32f, 0.07f, 0.32f, lo, hi);                       /* plinth, two steps */
  mb_box(p, x, y + 0.18f, z, 0.25f, 0.04f, 0.25f, hi, hi);
  mb_cone(p, x, y + 0.22f, z, 0.17f, 0.14f, sh, 0, 0, 12, lo, hi);               /* the shaft */
  for (int k = 0; k < 6; k++) {                                                  /* fluting: shallow ribs */
    float a = 6.2831853f * (float)k / 6.0f;
    mb_cone(p, x + 0.155f * cosf(a), y + 0.24f, z + 0.155f * sinf(a), 0.022f, 0.02f, sh - 0.04f, -0.025f * cosf(a), -0.025f * sinf(a), 4, lo, hi);
  }
  float cap = 0.22f + sh;
  mb_box(p, x, cap + y + 0.05f, z, 0.24f, 0.05f, 0.24f, hi, hi);                /* the capital */
  rgb(c0, 0.26f, 0.15f, 0.07f), rgb(c1, 0.55f, 0.36f, 0.16f);                   /* weathered bronze */
  mb_cone(p, x, y + cap + 0.1f, z, 0.1f, 0.13f, 0.06f, 0, 0, 12, c0, c0);         /* foot of the bowl */
  mb_cone(p, x, y + cap + 0.16f, z, 0.13f, 0.4f, 0.24f, 0, 0, 14, c0, c1);        /* the bowl */
  mb_cone(p, x, y + cap + 0.39f, z, 0.42f, 0.42f, 0.04f, 0, 0, 14, c1, c1);       /* its lip */
  mb_ellip(g, x, y + cap + 0.32f, z, 0.33f, 0.09f, 0.33f, coal0, coal1, 12, 5);
  return cap + 0.4f;
}

/* torches: where the brazier stands and where its fire is (x, flame y, z, region), for the lights and flames */
static float g_torch[MAX_TORCHES][4];
static int g_ntorch;
static float g_torch_base[MAX_TORCHES][3]; /* x, floor y, z */
/* every 11 columns in the caves and the ruins: a brazier on the floor near the back edge of the walkway, on the first
 * column nearby that can carry it (solid stone, three tiles of headroom, not beside a flag or an ability crystal) */
static void place_torches(const level *L) {
  g_ntorch = 0;
  for (int x = 6; x < WORLD_W && g_ntorch < MAX_TORCHES; x += 11) {
    int b = L->biome[x];
    if (b != BIOME_CAVES && b != BIOME_RUINS) continue;
    for (int t = 0; t < 11; t++) { /* the column itself, then outward up to 5 either side (over a pool or a pit) */
      int c = x + ((t & 1) ? (t + 1) / 2 : -(t / 2));
      if (c < 1 || c >= WORLD_W - 1 || L->biome[c] != b) continue;
      if (g_ntorch && (float)c + 0.5f - g_torch_base[g_ntorch - 1][0] < 6.0f) continue; /* not crowding the last one */
      bool busy = false;
      for (int k = 0; k < L->nchecks; k++) busy |= abs(L->checks[k].x - c) <= 1;
      for (int k = 0; k < L->npickups; k++) busy |= abs(L->pickups[k].x - c) <= 1;
      if (busy) continue;
      int top = -1;
      for (int y = L->ground[c] + 3; y >= 1 && y > L->ground[c] - 5; y--) {
        if (y + 2 >= WORLD_H || !world_solid(L, c, y - 1)) continue;
        if (world_solid(L, c, y) || world_solid(L, c, y + 1) || world_solid(L, c, y + 2)) continue;
        if (L->t[y][c] != T_AIR || L->t[y + 1][c] != T_AIR) continue;
        top = y;
        break;
      }
      if (top < 0) continue;
      float px = (float)c + 0.5f, pz = -1.0f;
      if (!fg_seat(L, &px, (float)top, &pz, 0.34f, 0.34f, (float)c)) continue;
      g_torch_base[g_ntorch][0] = px, g_torch_base[g_ntorch][1] = (float)top, g_torch_base[g_ntorch][2] = pz;
      g_torch[g_ntorch][0] = px, g_torch[g_ntorch][2] = pz, g_torch[g_ntorch][3] = (float)b;
      g_ntorch++;
      break;
    }
  }
}
static bool near_torch(int x, int y) {
  for (int k = 0; k < g_ntorch; k++)
    if (fabsf(g_torch_base[k][0] - ((float)x + 0.5f)) < 1.6f && (int)g_torch_base[k][1] == y + 1) return true;
  return false;
}
/* the fire check: a flame at (x, y, z) burns in a brazier when it is within 6 cm of a basket's coals and that brazier
 * was seated on the floor (it is in the placed list the scenery check measures against the drawn ground). A flame
 * anywhere else (in front of the playfield, at a lava light, over a pickup) is fire floating in the air. */
bool world_flame_seated(float x, float y, float z) {
  for (int k = 0; k < g_ntorch; k++) {
    float dx = x - g_torch[k][0], dy = y - g_torch[k][1], dz = z - g_torch[k][2];
    if (dx * dx + dy * dy + dz * dz > 0.06f * 0.06f) continue;
    for (int i = 0; i < g_nplaced; i++)
      if (g_placed[i].layer == -1 && fabsf(g_placed[i].x - g_torch_base[k][0]) < 1e-4f && fabsf(g_placed[i].z - g_torch_base[k][2]) < 1e-4f &&
          fabsf(g_placed[i].y - g_torch_base[k][1]) < 1e-4f)
        return true;
  }
  return false;
}
int world_torches(float out[][4], int cap) {
  int n = g_ntorch < cap ? g_ntorch : cap;
  memcpy(out, g_torch, sizeof(float) * 4 * (size_t)n);
  return n;
}

static void lava_spire(mb *p, mb *g, float x, float y, float z, int seed, float s) {
  float c0[3], c1[3];
  rgb(c0, 0.2f, 0.12f, 0.12f), rgb(c1, 0.07f, 0.05f, 0.05f);
  float h = (1.2f + 1.8f * hh(seed, 1, 70)) * s;
  mb_cone(p, x, y - 0.25f, z, 0.34f * s, 0.03f, h, (hh(seed, 2, 70) - 0.5f) * 0.7f, 0, 6, c0, c1);
  float gl0[3] = { 1.2f, 0.3f, 0.04f }, gl1[3] = { 3.0f, 1.0f, 0.2f };
  mb_cone(g, x, y + 0.03f, z + 0.16f * s, 0.045f * s, 0.01f, h * 0.55f, 0, 0, 4, gl0, gl1);
}

/* the faces that look at the camera, broken up: a grass fringe or a stone lip along every open top edge, boulders set
 * into big walls, hexagonal basalt columns in the volcano, roots under the meadow turf and
 * glowing veins in the caves. Everything sits on solid tiles (a boulder only where all eight neighbours are solid too),
 * so nothing covers a player. */
static bool interior(const level *L, int x, int y) {
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if (x + dx < 0 || x + dx >= WORLD_W || y + dy < 0 || y + dy >= WORLD_H || !world_solid(L, x + dx, y + dy)) return false;
  return true;
}
/* a rock set into a face: an ellipsoid whose radius wanders with noise (no smooth pebble), uvs in world metres so the
 * wall's own texture runs across it, coloured like the wall around it */
static void mb_rock(mb *b, float cx, float cy, float cz, float rx, float ry, float rz, const float *lo, const float *hi, int seed) {
  int seg = 14, ring = 9;
  mb_room(b, (seg + 1) * (ring + 1), seg * ring * 6);
  int v0 = b->nv;
  for (int j = 0; j <= ring; j++) {
    float ph = 3.14159265f * (float)j / (float)ring, sy = cosf(ph), sr = sinf(ph);
    float k = 0.5f - 0.5f * sy;
    float c[3] = { mixf(lo[0], hi[0], k), mixf(lo[1], hi[1], k), mixf(lo[2], hi[2], k) };
    for (int i = 0; i <= seg; i++) {
      float th = 6.2831853f * (float)(i % seg) / (float)seg, cxx = cosf(th) * sr, czz = sinf(th) * sr;
      /* lumpy: low-frequency bumps plus flat facets where the noise is high (chipped faces) */
      float n = vnoise((float)(i % seg) / (float)seg, (float)j / (float)ring, 4, seed) * 0.32f + vnoise((float)(i % seg) / (float)seg, (float)j / (float)ring, 8, seed + 1) * 0.12f;
      float f = 0.82f + n - (n > 0.3f ? (n - 0.3f) * 0.8f : 0.0f);
      float px = cx + rx * cxx * f, py = cy + ry * sy * f, pz = cz + rz * czz * f;
      float nx = cxx / rx, ny = sy / ry, nz = czz / rz, l = sqrtf(nx * nx + ny * ny + nz * nz);
      mb_vert(b, px, py, pz, nx / l, ny / l, nz / l, c, px / 4.0f, py / 4.0f);
    }
  }
  for (int j = 0; j < ring; j++)
    for (int i = 0; i < seg; i++) {
      int a = v0 + j * (seg + 1) + i, c2 = a + seg + 1;
      mb_tri_i(b, a, a + 1, c2), mb_tri_i(b, a + 1, c2 + 1, c2); /* wound outward: the tile materials draw front faces only */
    }
}
/* the surface audit (g.surface): gameplay surfaces (moving platforms, wooden ledges, cracked blocks) built with a real
 * PBR material, built with a plain colour, and seam strips drawn across rock faces. COOP_PLAIN_BUG (native) is the
 * control: a plain olive platform and the old grey seams. */
static int g_surface[3];
static int g_ledge_unheld; /* ends of wooden ledges with no post */
void world_surface_audit(int out[3]) { memcpy(out, g_surface, sizeof g_surface); out[2] += g_ledge_unheld; }
/* one thing set into the face, half sunk: a bone, a little skull with its ears still on, a coin, a fossil, a rubber duck */
static void buried(mb *p, float x, float y, float z, int seed, int bi, const float *base) {
  float bone[3] = { 1.3f, 1.2f, 1.0f }, bdk[3] = { 0.8f, 0.72f, 0.56f }, socket[3] = { 0.03f, 0.02f, 0.02f };
  float gold0[3] = { 0.55f, 0.36f, 0.06f }, gold1[3] = { 0.95f, 0.72f, 0.2f };
  (void)base;
  float k = hh(seed, 1, 440), tilt = (hh(seed, 2, 441) - 0.5f) * 1.2f;
  float dep = fminf(1.0f, 0.9f + 0.1f * base[1]);
  (void)dep;
  if (k < 0.4f) { /* a long bone, knuckles at both ends */
    float dx = cosf(tilt) * 0.34f, dy = sinf(tilt) * 0.34f;
    mb_cone(p, x - dx, y - dy, z + 0.01f, 0.05f, 0.04f, 2 * dy, 2 * dx, 0, 5, bdk, bone);
    for (int e = -1; e <= 1; e += 2) {
      mb_ellip(p, x + (float)e * dx, y + (float)e * dy + 0.04f, z + 0.02f, 0.07f, 0.07f, 0.03f, bdk, bone, 8, 4);
      mb_ellip(p, x + (float)e * dx, y + (float)e * dy - 0.04f, z + 0.02f, 0.07f, 0.07f, 0.03f, bdk, bone, 8, 4);
    }
  } else if (k < 0.65f) { /* the skull of something small and cute: the ears survive */
    mb_ellip(p, x, y, z + 0.02f, 0.2f, 0.18f, 0.05f, bdk, bone, 12, 5);
    mb_cone(p, x - 0.13f, y + 0.12f, z + 0.02f, 0.07f, 0.01f, 0.16f, -0.05f, 0, 4, bdk, bone);
    mb_cone(p, x + 0.13f, y + 0.12f, z + 0.02f, 0.07f, 0.01f, 0.16f, 0.05f, 0, 4, bdk, bone);
    mb_ellip(p, x - 0.07f, y + 0.02f, z + 0.06f, 0.05f, 0.055f, 0.01f, socket, socket, 8, 3);
    mb_ellip(p, x + 0.07f, y + 0.02f, z + 0.06f, 0.05f, 0.055f, 0.01f, socket, socket, 8, 3);
    mb_box(p, x, y - 0.1f, z + 0.06f, 0.07f, 0.012f, 0.005f, socket, socket);
  } else if (k < 0.85f) { /* coins nobody got: a stack, tipped over */
    for (int i = 0; i < 4; i++) mb_ellip(p, x - 0.16f + 0.11f * (float)i, y - 0.06f + 0.05f * (float)(i & 1) + tilt * 0.06f * (float)i, z + 0.02f, 0.1f, 0.1f, 0.02f, gold0, gold1, 10, 3);
  } else if (bi == BIOME_FROST) { /* a fish, frozen mid complaint */
    float f0[3] = { 0.3f, 0.45f, 0.7f }, f1[3] = { 0.6f, 0.75f, 0.95f };
    mb_ellip(p, x, y, z + 0.02f, 0.24f, 0.1f, 0.03f, f0, f1, 10, 4);
    mb_cone(p, x + 0.22f, y, z + 0.02f, 0.02f, 0.12f, 0.0f, 0.14f, 0, 4, f0, f1);
    mb_ellip(p, x - 0.15f, y + 0.02f, z + 0.05f, 0.022f, 0.022f, 0.008f, socket, socket, 6, 3);
  } else { /* a rubber duck. Nobody knows */
    float d0[3] = { 0.75f, 0.55f, 0.05f }, d1[3] = { 1.0f, 0.85f, 0.15f }, bk[3] = { 0.9f, 0.35f, 0.05f };
    mb_ellip(p, x, y - 0.05f, z + 0.03f, 0.17f, 0.11f, 0.04f, d0, d1, 10, 4);
    mb_ellip(p, x - 0.11f, y + 0.09f, z + 0.03f, 0.08f, 0.08f, 0.04f, d0, d1, 10, 4);
    mb_box(p, x - 0.21f, y + 0.08f, z + 0.04f, 0.04f, 0.015f, 0.01f, bk, bk);
    mb_ellip(p, x - 0.13f, y + 0.11f, z + 0.07f, 0.012f, 0.012f, 0.006f, socket, socket, 6, 3);
  }
}
static void wall_detail(const level *L, int x0, int x1, mb *prop, mb *glow, mb *tiles) {
  const float zf = DEPTH_FRONT;
  for (int x = x0; x < x1 && x < WORLD_W; x++) {
    int bi = L->biome[x];
    for (int y = 1; y < WORLD_H - 1; y++) {
      int t = L->t[y][x];
      if (!(t == T_ROCK || t == T_GRASS || t == T_BRICK)) continue;
      float fx = (float)x, fy = (float)y;
      bool open_above = !world_solid(L, x, y + 1);
      float base[3];
      corner_col(L, bi, fx + 0.5f, fy + 0.5f, base);
      float r = hh(x, y, 400);
      if (open_above) {
        if (bi == BIOME_MEADOW && t != T_BRICK) {
          /* turf that hangs over the edge: short dark blades, and now and then a root */
          for (int k = 0; k < 7; k++) {
            float bx = fx + ((float)k + hh(x, k, 401)) / 7.0f, len = 0.12f + 0.26f * hh(x, k, 402);
            float c0[3] = { 0.12f, 0.32f, 0.06f }, c1[3] = { 0.05f, 0.14f, 0.03f };
            mb_cone(prop, bx, fy + 1.03f, zf + 0.03f, 0.05f, 0.006f, -len, (hh(x, k, 403) - 0.5f) * 0.12f, 0.05f, 3, c0, c1);
          }
          if (r < 0.34f) {
            float c0[3] = { 0.24f, 0.16f, 0.1f }, c1[3] = { 0.14f, 0.09f, 0.06f };
            /* a side root off the main one, thinning */
            mb_cone(prop, fx + 0.5f, fy + 0.6f, zf + 0.02f, 0.02f, 0.004f, -(0.5f + 0.5f * hh(x, y, 408)), (hh(x, y, 409) - 0.5f) * 0.9f, 0.0f, 3, c0, c1);
            mb_cone(prop, fx + 0.2f + 0.6f * hh(x, y, 404), fy + 0.95f, zf + 0.02f, 0.035f, 0.008f, -(0.4f + 0.7f * hh(x, y, 405)), (hh(x, y, 406) - 0.5f) * 0.3f, 0.04f, 4, c0, c1);
          }
        } else if (bi == BIOME_FROST && t != T_ICE) {
          /* snow over the edge, and icicles under it, none alike */
          float sn0[3] = { 0.8f, 0.87f, 1.0f }, sn1[3] = { 1.15f, 1.2f, 1.28f }, ic0[3] = { 0.55f, 0.75f, 1.0f }, ic1[3] = { 1.0f, 1.15f, 1.3f };
          mb_box(prop, fx + 0.5f, fy + 0.98f, zf + 0.05f, 0.52f, 0.1f + 0.05f * hh(x, y, 451), 0.1f, sn0, sn1);
          for (int k = 0; k < 3; k++)
            if (hh(x, k, 452) < 0.6f) mb_cone(prop, fx + 0.15f + 0.35f * (float)k, fy + 0.92f, zf + 0.06f, 0.06f, 0.004f, -(0.2f + 0.7f * hh(x, k, 453)), 0, 0, 4, ic1, ic0);
        } else {
          /* a worn stone lip: lighter, a little proud of the face */
          float lo[3], hi[3];
          shade(lo, base, 0.85f), shade(hi, base, 1.25f);
          mb_box(prop, fx + 0.5f, fy + 0.93f, zf + 0.03f, 0.5f, 0.07f + 0.02f * hh(x, y, 407), 0.07f, lo, hi);
        }
      }
      if (open_above) continue;
      /* strata rows (no seam is drawn any more: a thin lit seam box per tile read as a grey rail across the face); the
       * boulders below keep off them so they never line up into a row */
      int phase = (int)(wn(fx, 0, 0.05f, 410) * 3.0f);
      if (g_plain_bug && bi != BIOME_VOLCANO && t != T_BRICK && (y + phase) % 5 == 0 && world_solid(L, x - 1, y) && world_solid(L, x + 1, y)) {
        float lo[3], hi[3];
        shade(lo, base, 0.42f), shade(hi, base, 0.62f);
        mb_box(prop, fx + 0.5f, fy + 0.5f, zf + 0.005f, 0.53f, 0.035f, 0.02f, lo, hi);
        g_surface[2]++;
      }
      if (bi == BIOME_VOLCANO && interior(L, x, y)) {
        /* basalt columns: a hexagonal prism per tile, stacked into columns, every other column set back */
        float lo[3], hi[3];
        rgb(lo, 0.07f, 0.055f, 0.055f), rgb(hi, 0.13f, 0.1f, 0.1f); /* near-black glassy rock: the lava light gives it its edges */
        float back = 0.06f + 0.14f * wn(fx, 0, 0.4f, 418);
        mb_cone(prop, fx + 0.5f, fy, zf - 0.36f - back, 0.48f, 0.48f, 1.0f, 0, 0, 6, lo, hi);
        if (r < 0.05f) { /* a glowing seam between two columns */
          float g0[3] = { 1.6f, 0.4f, 0.05f }, g1[3] = { 2.6f, 0.8f, 0.12f };
          mb_box(glow, fx + 1.0f, fy + 0.5f, zf + 0.0f, 0.03f, 0.5f, 0.05f, g0, g1);
        }
        continue;
      }
      if (bi != BIOME_CAVES && t != T_BRICK && interior(L, x, y) && (y + phase) % 5 != 0 && (y + phase + 1) % 5 != 0 && (y + phase + 4) % 5 != 0 && r < (bi == BIOME_RUINS ? 0.03f : 0.05f)) { /* not in the caves (lit blue they read as spots) nor in laid brick (a boulder in masonry read as a hole), never on or beside a strata seam (a row of them along a seam read as wheels on a rail) */
        /* a boulder set into the face */
        float rx = 0.35f + 0.5f * hh(x, y, 412), ry = rx * (0.55f + 0.35f * hh(x, y, 413));
        float lo[3], hi[3];
        shade(lo, base, 0.62f), shade(hi, base, 1.02f);
        mb_rock(&tiles[tile_mat(L, x, y)], fx + 0.5f + 0.2f * (hh(x, y, 419) - 0.5f), fy + 0.5f, zf - rx * 0.35f, rx, ry, rx * 0.6f, lo, hi, x * 7 + y * 131); /* a bulge, half buried, in the wall's own material */
      }
      /* what is in the ground: bones of earlier visitors, coins nobody got, the odd thing that should not be there.
       * Only deep enough to be in solid rock, never in a row */
      if (t != T_BRICK && interior(L, x, y) && hh(x, y, 430) < 0.09f && x % 3 == (y % 3) && y > L->ground[x] - 8 && !(bi == BIOME_VOLCANO && hh(x, y, 431) < 0.4f)) buried(prop, fx + 0.5f, fy + 0.5f, zf, x * 13 + y * 7, bi, base);
      if (bi == BIOME_CAVES && r > 0.93f) { /* a vein of glowing crystal */
        float g0[3] = { 0.2f, 0.9f, 1.8f }, g1[3] = { 0.5f, 1.6f, 2.6f };
        int n = 3 + (int)(hh(x, y, 414) * 4);
        float px = fx + 0.5f, py = fy + 0.5f;
        for (int k = 0; k < n; k++) {
          mb_ellip(glow, px, py, zf + 0.01f, 0.06f + 0.04f * hh(x, k, 415), 0.025f, 0.02f, g0, g1, 6, 3);
          px += 0.12f + 0.05f * hh(x, k, 416), py += (hh(x, k, 417) - 0.5f) * 0.16f;
        }
      }
    }
  }
}

/* scatter props over the surfaces of one chunk */
static void scatter(const level *L, int x0, int x1, mb *prop, mb *glow) {
  for (int x = x0; x < x1 && x < WORLD_W; x++) {
    int bi = L->biome[x];
    for (int y = 1; y < WORLD_H - 1; y++) {
      int t = L->t[y][x];
      bool solid = world_solid(L, x, y);
      bool floor_ = solid && !world_solid(L, x, y + 1) && L->t[y + 1][x] != T_SPIKE && L->t[y + 1][x] != T_WATER && L->t[y + 1][x] != T_LAVA;
      bool ceil_ = solid && !world_solid(L, x, y - 1) && L->t[y - 1][x] == T_AIR && y > 4;
      float fx = (float)x, fy = (float)y + 1.0f;
      int seed = x * 131 + y * 17;
      float r = hh(x, y, 100);
      if (floor_ && t != T_CRACK && !near_torch(x, y)) {
        /* each prop is seated by its contact footprint (fg_seat): half extents along x and z, scaled with the prop */
        float px, pz, s;
#define SEAT(X, Z, RX, RZ) (px = (X), pz = (Z), fg_seat(L, &px, fy, &pz, (RX), (RZ), fx))
        if (bi == BIOME_MEADOW) {
          if (r < 0.55f && SEAT(fx + hh(x, y, 101), (r < 0.3f ? 1.0f : -1.0f) * (0.45f + 0.9f * hh(x, y, 102)), 0.11f, 0.11f)) grass_tuft(prop, px, fy, pz, seed, 1.0f);
          if (r < 0.2f && SEAT(fx + hh(x, y, 103), 0.7f + 0.6f * hh(x, y, 104), 0.09f, 0.09f)) grass_tuft(prop, px, fy, pz, seed + 1, 0.8f);
          if (r > 0.82f && r < 0.93f && SEAT(fx + hh(x, y, 105), (hh(x, y, 106) > 0.5f ? 1.0f : -1.0f) * (0.6f + 0.7f * hh(x, y, 107)), 0.13f, 0.13f)) flower(prop, glow, px, fy, pz, seed);
          if (r > 0.93f && r < 0.965f && SEAT(fx + 0.5f, -0.9f - 0.3f * hh(x, y, 108), 0.62f, 0.5f)) rock_pile(prop, px, fy, pz, seed, STONE[0], 1.0f);
          s = 0.8f + 0.5f * hh(x, y, 109);
          if (r > 0.965f && x % 3 == 0 && SEAT(fx + 0.5f, -1.1f, 0.91f * s, 0.15f + 0.34f * s)) bush(prop, px, fy, pz, seed, s);
        } else if (bi == BIOME_CAVES) {
          s = 0.7f + 0.8f * hh(x, y, 113);
          if (r < 0.1f && SEAT(fx + hh(x, y, 110), (hh(x, y, 111) > 0.5f ? 1.0f : -1.0f) * (0.7f + 0.6f * hh(x, y, 112)), 0.1f * s, 0.1f * s)) mushroom(prop, glow, px, fy, pz, seed, s);
          s = 0.6f + 0.8f * hh(x, y, 115);
          if (r > 0.1f && r < 0.26f && SEAT(fx + 0.5f, -0.9f - 0.4f * hh(x, y, 114), 0.5f * s, 0.25f + 0.2f * s)) crystal_cluster(glow, prop, px, fy, pz, seed, s, x / 40, false);
          if (r > 0.26f && r < 0.35f && SEAT(fx + hh(x, y, 116), -0.9f - 0.4f * hh(x, y, 117), 0.07f, 0.07f)) grass_tuft(prop, px, fy, pz, seed, 0.6f);
        } else if (bi == BIOME_RUINS) {
          if (r < 0.1f && x % 2 == 0 && SEAT(fx + 0.5f, (hh(x, y, 118) > 0.5f ? 1 : -1) * 0.9f, 0.5f, 0.44f)) rock_pile(prop, px, fy, pz, seed, STONE[2], 0.8f);
          if (r > 0.1f && r < 0.3f && SEAT(fx + hh(x, y, 119), (hh(x, y, 120) > 0.5f ? 1.0f : -1.0f) * (0.5f + 0.9f * hh(x, y, 121)), 0.1f, 0.1f)) grass_tuft(prop, px, fy, pz, seed, 0.9f);
          if (r > 0.3f && r < 0.36f && SEAT(fx + hh(x, y, 122), -0.8f, 0.13f, 0.13f)) flower(prop, glow, px, fy, pz, seed);
        } else if (bi == BIOME_FROST) {
          /* snow drifts, a few boulders, ice crystals that catch the light */
          s = 0.5f + 0.7f * hh(x, y, 140);
          if (r < 0.1f && SEAT(fx + 0.5f, -0.9f - 0.3f * hh(x, y, 141), 0.55f * s, 0.3f + 0.2f * s)) crystal_cluster(glow, prop, px, fy, pz, seed, s, 1, false);
          if (r > 0.12f && r < 0.2f && x % 2 == 0 && SEAT(fx + 0.5f, (hh(x, y, 142) > 0.5f ? 1 : -1) * 0.9f, 0.5f, 0.44f)) rock_pile(prop, px, fy, pz, seed, STONE[BIOME_FROST], 0.8f);
          /* the office furniture, at the back so nobody walks into it */
          int ok = (int)(hh(x, y, 143) * 4.0f);
          static const float OW[4][2] = { { 0.76f, 0.36f }, { 0.26f, 0.48f }, { 0.19f, 0.19f }, { 0.44f, 0.34f } };
          if (r > 0.2f && r < 0.42f && x % 3 == 1 && SEAT(fx + 0.5f, -1.05f, OW[ok][0], OW[ok][1])) office_prop(prop, glow, px, fy, pz, seed, ok);
        } else {
          s = 0.8f + 0.9f * hh(x, y, 124);
          if (r < 0.07f && SEAT(fx + 0.5f, -0.9f - 0.5f * hh(x, y, 123), 0.34f * s, 0.34f * s + 0.05f)) lava_spire(prop, glow, px, fy, pz, seed, s);
          if (r > 0.07f && r < 0.2f && SEAT(fx + hh(x, y, 125), (hh(x, y, 126) > 0.5f ? 1 : -1) * (0.7f + 0.6f * hh(x, y, 127)), 0.56f, 0.47f)) rock_pile(prop, px, fy, pz, seed, STONE[3], 0.9f);
        }
#undef SEAT
      }
      if (ceil_ && !floor_) {
        float cy = (float)y; /* the underside of the tile */
        if (bi == BIOME_CAVES) {
          if (r < 0.3f) stalactite(prop, fx + hh(x, y, 130), cy, -0.6f + 1.2f * hh(x, y, 131), seed, 0.8f + 0.8f * hh(x, y, 132));
          if (r > 0.3f && r < 0.36f) crystal_cluster(glow, prop, fx + 0.5f, cy, -0.8f, seed, 0.6f + 0.6f * hh(x, y, 133), x / 30 + 1, true);
        } else if (bi == BIOME_RUINS && r < 0.3f) vine(prop, fx + hh(x, y, 134), cy, 0.9f - 1.8f * hh(x, y, 135) , seed, 0.8f + 1.8f * hh(x, y, 136));
        else if (bi == BIOME_FROST && r < 0.18f && x % 2 == 0) { /* a strip light, screwed to the roof, still on */
          float h0[3] = { 0.5f, 0.52f, 0.56f }, h1[3] = { 0.7f, 0.72f, 0.76f }, l0[3] = { 1.6f, 1.7f, 1.8f };
          mb_box(prop, fx + 0.5f, cy - 0.06f, -0.3f, 0.48f, 0.06f, 0.16f, h0, h1);
          mb_box(glow, fx + 0.5f, cy - 0.125f, -0.3f, 0.42f, 0.006f, 0.1f, l0, l0);
        }
        else if (bi == BIOME_VOLCANO && r < 0.2f) stalactite(prop, fx + hh(x, y, 137), cy, -0.6f + 1.2f * hh(x, y, 138), seed, 1.0f);
      }
    }
  }
}

/* rock that hangs in the air (a lintel over a pedestal, any block the generator leaves with nothing under it) gets
 * stone pillars at its two lower corners, set at the back of the slab so nobody walks into them: every solid tile
 * not joined to the floor of the world or its side walls is part of such a block, and each pillar stands on the first
 * open floor under its corner (no floor within reach: no pillar) */
static void hanging_supports(const level *L, mb *p) {
  static uint8_t seen[WORLD_H][WORLD_W];
  static int stack[WORLD_W * WORLD_H];
  memset(seen, 0, sizeof seen);
  int sp = 0;
  for (int x = 0; x < WORLD_W; x++)
    if (world_solid(L, x, 0)) seen[0][x] = 1, stack[sp++] = x;
  for (int y = 0; y < WORLD_H; y++) {
    if (world_solid(L, 0, y) && !seen[y][0]) seen[y][0] = 1, stack[sp++] = y * WORLD_W;
    if (world_solid(L, WORLD_W - 1, y) && !seen[y][WORLD_W - 1]) seen[y][WORLD_W - 1] = 1, stack[sp++] = y * WORLD_W + WORLD_W - 1;
  }
  while (sp) {
    int c = stack[--sp], x = c % WORLD_W, y = c / WORLD_W;
    static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int k = 0; k < 4; k++) {
      int nx = x + D[k][0], ny = y + D[k][1];
      if (nx < 0 || nx >= WORLD_W || ny < 0 || ny >= WORLD_H || seen[ny][nx] || !world_solid(L, nx, ny)) continue;
      seen[ny][nx] = 1, stack[sp++] = ny * WORLD_W + nx;
    }
  }
  /* each run of hanging tiles along a bottom row: a pillar under each end */
  for (int y = 1; y < WORLD_H; y++)
    for (int x = 0; x < WORLD_W;) {
      bool hang = world_solid(L, x, y) && !seen[y][x] && !world_solid(L, x, y - 1);
      if (!hang) { x++; continue; }
      int x1 = x;
      while (x1 + 1 < WORLD_W && world_solid(L, x1 + 1, y) && !seen[y][x1 + 1] && !world_solid(L, x1 + 1, y - 1)) x1++;
      float ends[2] = { (float)x + 0.45f, (float)x1 + 0.55f };
      for (int e = 0; e < (x1 > x ? 2 : 1); e++) {
        int c = (int)floorf(ends[e]), fy = y - 1;
        while (fy >= 0 && !world_solid(L, c, fy)) fy--;
        if (fy < 0 || y - (fy + 1) > 12) continue;
        float top = (float)(fy + 1), px = ends[e], pz = -1.0f;
        if (!fg_seat(L, &px, top, &pz, 0.4f, 0.4f, (float)c)) continue;
        float lo[3], hi[3], cap[3];
        /* the region's own stone, darker than the faces around it so a pillar reads as standing in the shade under
         * its block (a cream column in the blue caves looked pasted in) */
        int bi = L->biome[c];
        static const float PK[BIOMES][3] = { { 0.42f, 0.5f, 0.6f }, { 0.3f, 0.36f, 0.44f }, { 0.5f, 0.6f, 0.72f }, { 0.5f, 0.6f, 0.75f }, { 0.16f, 0.2f, 0.24f } };
        shade(lo, STONE[bi], PK[bi][0]), shade(hi, STONE[bi], PK[bi][1]), shade(cap, STONE[bi], PK[bi][2]);
        float h = (float)y - top;
        mb_box(p, px, top + 0.1f, pz, 0.4f, 0.3f, 0.4f, lo, cap);                 /* a plinth, sunk a little */
        mb_cone(p, px, top + 0.3f, pz, 0.3f, 0.27f, h - 0.55f, 0, 0, 12, lo, hi); /* the shaft */
        mb_box(p, px, (float)y - 0.13f, pz, 0.4f, 0.14f, 0.4f, hi, cap);        /* a capital under the rock */
      }
      x = x1 + 1;
    }
}

/* ── scene objects ──────────────────────────────────────────────── */

static cw_node g_root;
static t3_material *g_mat[MT_COUNT], *g_spike, *g_lava_m, *g_water_front, *g_foam_m, *g_wall_m[BIOMES];
static t3_mesh *g_crack_mesh[128];
static t3_texture *g_lava_tex;

typedef struct span { float x0, x1, y0, y1; } span;
static span g_waters[16], g_lavas[48];
static int g_nwaters, g_nlavas;
int world_water_spans(float out[][4], int cap) {
  int n = g_nwaters < cap ? g_nwaters : cap;
  for (int i = 0; i < n; i++) out[i][0] = g_waters[i].x0, out[i][1] = g_waters[i].x1, out[i][2] = g_waters[i].y0, out[i][3] = g_waters[i].y1;
  return n;
}
int world_lava_spans(float out[][4], int cap) {
  int n = g_nlavas < cap ? g_nlavas : cap;
  for (int i = 0; i < n; i++) out[i][0] = g_lavas[i].x0, out[i][1] = g_lavas[i].x1, out[i][2] = g_lavas[i].y0, out[i][3] = g_lavas[i].y1;
  return n;
}

static t3_mesh *add_mesh(t3_geometry *g, t3_material *m, float x, float y, float z, bool shadow) {
  cw_node n = cw_node_new("MeshInstance3D", "m");
  cw_node_add_child(g_root, n);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.position = (t3_vec3){ x, y, z };
  me->base.cast_shadow = shadow;
  me->base.receive_shadow = true;
  cw_mesh3d_set_procedural(n, me);
  t3_release(me);
  return me;
}

/* animated surfaces: the lava's top and front slide their uvs, the water's front sheet waves */
typedef struct anim_surf { t3_geometry *geo; float *pos, *uv; int nv; float x0, x1, y_top, y_bot, z; int kind; int cols; } anim_surf;
static anim_surf g_surf[128];
static int g_nsurf;

void world_scene_free(void) {
  g_root = (cw_node){ 0 };
  g_nsurf = 0;
  g_nwaters = g_nlavas = 0;
}

static t3_material *basic_vcol(bool additive, bool transparent, float opacity) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  m->side = T3_DOUBLE_SIDE;
  m->fog = false;
  if (additive) m->blending = 2, m->transparent = true, m->depth_write = false;
  if (transparent) m->transparent = true, m->opacity = opacity, m->depth_write = false;
  return m;
}

/* a moving platform, built around its centre (the game moves the meshes): a slab of the region's own material with a
 * chamfered border and fittings at the corners. Meadow: a plank raft on iron-banded beams. Caves: a stone slab in an
 * iron frame. Ruins: a carved sandstone slab with bronze corner caps and a line of glowing runes along its face and
 * under it (the old magic that carries it). Volcano: basalt in an iron frame. Returns the meshes (at most 2). */
int world_platform(int biome, float w, int seed, t3_mesh **out) {
  mb body = { 0 }, glow = { 0 };
  float hw = 0.5f * w, hh2 = 0.2f, hz = 1.2f;
  float c0[3], c1[3], t0[3], t1[3], m0[3], m1[3];
  int mat = biome == BIOME_MEADOW ? MT_WOOD : biome == BIOME_CAVES ? MT_CAVE : biome == BIOME_VOLCANO ? MT_BASALT : MT_ROCK;
  if (biome == BIOME_RUINS) rgb(c0, 0.62f, 0.5f, 0.36f), rgb(c1, 0.86f, 0.74f, 0.56f), rgb(m0, 0.3f, 0.18f, 0.08f), rgb(m1, 0.62f, 0.42f, 0.18f);
  else if (biome == BIOME_MEADOW) rgb(c0, 0.8f, 0.78f, 0.75f), rgb(c1, 1, 1, 1), rgb(m0, 0.06f, 0.06f, 0.07f), rgb(m1, 0.16f, 0.16f, 0.18f);
  else rgb(c0, 0.55f, 0.58f, 0.66f), rgb(c1, 0.8f, 0.83f, 0.9f), rgb(m0, 0.06f, 0.06f, 0.07f), rgb(m1, 0.16f, 0.16f, 0.18f);
  shade(t0, c0, 1.12f), shade(t1, c1, 1.12f);
  /* the core, a little inside the border so the border reads as a chamfered edge */
  mb_box_uv(&body, 0, -0.03f, 0, hw - 0.06f, hh2 - 0.03f, hz - 0.06f, c0, c1, 2.0f, false);
  /* border: a raised rim along the four top edges, and a lower tier set back underneath */
  mb_box_uv(&body, 0, hh2 - 0.06f, hz - 0.07f, hw, 0.06f, 0.07f, t0, t1, 2.0f, false);
  mb_box_uv(&body, 0, hh2 - 0.06f, -(hz - 0.07f), hw, 0.06f, 0.07f, t0, t1, 2.0f, false);
  mb_box_uv(&body, hw - 0.07f, hh2 - 0.06f, 0, 0.07f, 0.06f, hz - 0.14f, t0, t1, 2.0f, false);
  mb_box_uv(&body, -(hw - 0.07f), hh2 - 0.06f, 0, 0.07f, 0.06f, hz - 0.14f, t0, t1, 2.0f, false);
  mb_box_uv(&body, 0, -hh2 - 0.08f, 0, hw - 0.25f, 0.1f, hz - 0.3f, c0, c0, 2.0f, false);
  /* fittings: caps on the four corners (bronze in the ruins, iron elsewhere) and bands across */
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sz = -1; sz <= 1; sz += 2) mb_box(&body, (hw - 0.08f) * (float)sx, 0.0f, (hz - 0.08f) * (float)sz, 0.11f, hh2 + 0.025f, 0.11f, m0, m1);
  if (biome != BIOME_RUINS)
    for (int k = 0; k < 2; k++) mb_box(&body, (k ? 1.0f : -1.0f) * hw * 0.45f, 0, 0, 0.06f, hh2 + 0.015f, hz + 0.01f, m0, m1);
  else {
    float g0[3] = { 0.3f, 1.4f, 2.2f }, g1[3] = { 0.6f, 2.4f, 3.2f };
    int n = (int)(w * 3.0f);
    for (int k = 0; k < n; k++) { /* runes: short glowing strokes along the front face, each its own small shape */
      float x = -hw + 0.3f + (w - 0.6f) * ((float)k + 0.5f) / (float)n;
      float r = hash2(seed, k, 801, 1 << 20);
      if (r < 0.5f) mb_box(&glow, x, -0.02f, hz - 0.055f, 0.015f, 0.07f, 0.006f, g0, g1);
      else mb_box(&glow, x, -0.02f + 0.04f * (r - 0.75f), hz - 0.055f, 0.06f, 0.012f, 0.006f, g0, g1);
      if (r > 0.3f && r < 0.7f) mb_box(&glow, x + 0.04f, 0.04f, hz - 0.055f, 0.012f, 0.03f, 0.006f, g0, g1);
    }
    mb_box(&glow, 0, -hh2 - 0.185f, 0, hw - 0.4f, 0.006f, hz - 0.45f, g0, g0); /* the seal underneath */
  }
  int nm = 0;
  t3_geometry *gb = mb_done(&body);
  t3_material *bm = g_mat[mat];
  if (g_plain_bug) bm = t3_mesh_physical_material_new(0x8a7a5a);
  g_surface[g_plain_bug ? 1 : 0]++;
  if (gb) out[nm++] = add_mesh(gb, bm, 0, 0, 0, true), t3_release(gb);
  gb = mb_done(&glow);
  if (gb) out[nm++] = add_mesh(gb, g_mat[MT_GLOW], 0, 0, 0, false), t3_release(gb);
  return nm;
}

void world_scene_build(game *g, cw_node root) {
  const level *L = &g->L;
  g_root = root;
  g_nsurf = 0;
  g_nwaters = g_nlavas = 0;
  g_nplaced = 0;
  memset(g_surface, 0, sizeof g_surface);
  g_ledge_unheld = 0;
  g_plain_bug = getenv("COOP_PLAIN_BUG") != NULL;
  g_pit_old = getenv("COOP_PIT_OLD") != NULL;
  if (!g_mat[MT_ROCK]) { /* once: every level draws with the same generated materials, and a level loads without a new set */
  g_mat[MT_ROCK] = pbr(gen_rock, 1.4f, 1.0f, false, 0);
  g_mat[MT_GRASS] = pbr(gen_grass, 2.0f, 1.0f, false, 0);
  g_mat[MT_CAVE] = pbr(gen_cave, 1.6f, 1.0f, true, 0.7f);
  g_mat[MT_BRICK] = pbr(gen_brick, 4.0f, 1.0f, false, 0);
  g_mat[MT_BASALT] = pbr(gen_basalt, 1.8f, 1.0f, true, 1.1f);
  g_mat[MT_PROP] = t3_mesh_physical_material_new(0xffffff);
  g_mat[MT_PROP]->vertex_colors = true, g_mat[MT_PROP]->roughness = 0.75f, g_mat[MT_PROP]->side = T3_DOUBLE_SIDE;
  g_mat[MT_GLOW] = basic_vcol(false, false, 1);
  g_mat[MT_WOOD] = pbr(gen_wood, 3.0f, 1.0f, false, 0);
  g_mat[MT_ICE] = pbr(gen_ice, 1.0f, 1.0f, false, 0);
  g_mat[MT_BELT] = pbr(gen_belt, 2.0f, 1.0f, false, 0);
  }
  place_torches(L); /* first, so the scatter leaves their footing clear */
  /* terrain and props in chunks */
  for (int cx = 0; cx < WORLD_W; cx += CHUNK) {
    mb bs[MT_COUNT];
    memset(bs, 0, sizeof bs);
    for (int y = 0; y < WORLD_H; y++)
      for (int x = cx; x < cx + CHUNK && x < WORLD_W; x++) {
        int t = L->t[y][x];
        if (t == T_ROCK || t == T_GRASS || t == T_BRICK || t == T_ICE || t == T_BELT) tile_faces(L, x, y, bs);
      }
    /* the back of a pit: dark and going darker, so a gap reads as a hole with a bottom you do not want, and a body falling
     * shows against it, instead of seeing through to the hills behind. Over lava it is lit from below */
    for (int x = cx; x < cx + CHUNK && x < WORLD_W; x++) {
      if (L->ground[x] != 0) continue;
      int top = 0;
      for (int k = x - 6; k <= x + 6; k++) if (k >= 0 && k < WORLD_W && L->ground[k] < WORLD_H && L->ground[k] > top) top = L->ground[k];
      bool lava = L->t[2][x] == T_LAVA;
      if (!lava && !g_pit_old) {
        /* the far wall of the hole: the ground's own strata (its texture, its loam band, its colour wash) carried on from
         * the lip, going black over the first three metres, a root or two hanging off the lip and something left over from
         * the last visitor; below that, black to the bottom. It reads as a cut into the ground, darker than any floor */
        int bi = L->biome[x], mt = MT_ROCK; /* the stone texture tinted by region: the caves' and the crater's glowing seams do not run down a pit */
        float fx = (float)x, n[3] = { 0, 0, 1 };
        for (int r = 0; r < 4; r++) {
          float y0 = (float)top - (float)(r + 1), y1 = (float)top - (float)r;
          float p[4][3] = { { fx, y0, DEPTH_BACK }, { fx + 1, y0, DEPTH_BACK }, { fx + 1, y1, DEPTH_BACK }, { fx, y1, DEPTH_BACK } };
          float c[4][3], uv[4][2];
          for (int k = 0; k < 4; k++) {
            float vy = p[k][1], d = (float)top - vy;
            float base[3];
            corner_col(L, bi, p[k][0], (float)top - 1.0f - 0.5f * d, base); /* the band under the surface, then deeper rock */
            shade(c[k], base, pit_dark(d));
            face_uv(p[k][0] + 0.37f, vy, mt, uv[k]);
          }
          mb_quad(&bs[mt], p, n, c, uv);
        }
        float k0[3] = { 0.004f, 0.004f, 0.006f }, mid = (float)top - 4.0f;
        float p1[4][3] = { { fx, 0, DEPTH_BACK }, { fx + 1, 0, DEPTH_BACK }, { fx + 1, mid, DEPTH_BACK }, { fx, mid, DEPTH_BACK } };
        float col1[4][3], uv1[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        for (int k = 0; k < 4; k++) memcpy(col1[k], k0, sizeof k0);
        mb_quad(&bs[MT_PROP], p1, n, col1, uv1);
        /* roots off the lip in the meadow and the ruins, bones set in the strata everywhere now and then */
        float rc0[3] = { 0.16f, 0.1f, 0.06f }, rc1[3] = { 0.3f, 0.2f, 0.12f };
        if ((bi == BIOME_MEADOW || bi == BIOME_RUINS) && hh(x, 1, 901) < 0.7f)
          for (int k = 0; k < 2; k++) {
            float rx = fx + 0.2f + 0.6f * hh(x, k, 902), len = 0.5f + 1.1f * hh(x, k, 903);
            mb_cone(&bs[MT_PROP], rx, (float)top - len, DEPTH_BACK + 0.06f, 0.012f, 0.045f, len, 0.25f * (hh(x, k, 904) - 0.5f), 0.05f, 4, rc0, rc1);
          }
        if (hh(x, 2, 905) < 0.22f) {
          float bc0[3] = { 0.42f, 0.38f, 0.32f }, bc1[3] = { 0.85f, 0.8f, 0.7f };
          float by = (float)top - 1.2f - 0.9f * hh(x, 3, 906), bx = fx + 0.5f;
          mb_ellip(&bs[MT_PROP], bx, by + 0.1f, DEPTH_BACK + 0.04f, 0.16f, 0.13f, 0.12f, bc0, bc1, 8, 6); /* a skull, half in the wall */
          mb_ellip(&bs[MT_PROP], bx + 0.32f, by + 0.04f, DEPTH_BACK + 0.02f, 0.22f, 0.035f, 0.035f, bc0, bc1, 6, 4); /* a long bone, in the strata */
        }
        continue;
      }
      float n[3] = { 0, 0, 1 }, c0[3] = { 0.1f, 0.075f, 0.06f }, c1[3] = { 0.008f, 0.006f, 0.008f };
      if (lava) rgb(c1, 1.1f, 0.3f, 0.06f), rgb(c0, 0.06f, 0.02f, 0.02f);
      float fx = (float)x, midy = (float)top - 4.0f;
      float p0[4][3] = { { fx, midy, DEPTH_BACK }, { fx + 1, midy, DEPTH_BACK }, { fx + 1, (float)top, DEPTH_BACK }, { fx, (float)top, DEPTH_BACK } };
      float col0[4][3], col1[4][3];
      float mid[3] = { (c0[0] + c1[0]) * 0.3f, (c0[1] + c1[1]) * 0.3f, (c0[2] + c1[2]) * 0.3f };
      for (int k = 0; k < 3; k++) col0[0][k] = col0[1][k] = mid[k], col0[2][k] = col0[3][k] = c0[k];
      float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
      mb_quad(&bs[MT_PROP], p0, n, col0, uv);
      float p1[4][3] = { { fx, 0, DEPTH_BACK }, { fx + 1, 0, DEPTH_BACK }, { fx + 1, midy, DEPTH_BACK }, { fx, midy, DEPTH_BACK } };
      for (int k = 0; k < 3; k++) col1[0][k] = col1[1][k] = c1[k], col1[2][k] = col1[3][k] = mid[k];
      mb_quad(&bs[MT_PROP], p1, n, col1, uv);
    }
    scatter(L, cx, cx + CHUNK, &bs[MT_PROP], &bs[MT_GLOW]);
    wall_detail(L, cx, cx + CHUNK, &bs[MT_PROP], &bs[MT_GLOW], bs);
    for (int m = 0; m < MT_COUNT; m++) {
      t3_geometry *gm = mb_done(&bs[m]);
      if (gm) add_mesh(gm, g_mat[m], 0, 0, 0, m != MT_GLOW), t3_release(gm);
    }
  }
  {
    mb sup = { 0 };
    hanging_supports(L, &sup);
    t3_geometry *gs = mb_done(&sup);
    if (gs) add_mesh(gs, g_mat[MT_PROP], 0, 0, 0, true), t3_release(gs);
  }
  {
    mb br = { 0 }, brg = { 0 };
    for (int k = 0; k < g_ntorch; k++) {
      float *tb = g_torch_base[k];
      float h = brazier(&br, &brg, tb[0], tb[1], tb[2], k * 37 + 5, (int)g_torch[k][3] == BIOME_RUINS);
      g_torch[k][1] = tb[1] + h;
    }
    t3_geometry *gb = mb_done(&br);
    if (gb) add_mesh(gb, g_mat[MT_PROP], 0, 0, 0, true), t3_release(gb);
    gb = mb_done(&brg);
    if (gb) add_mesh(gb, g_mat[MT_GLOW], 0, 0, 0, false), t3_release(gb);
  }
  /* cracked blocks, one mesh each (removed when broken): the region's own stone, a shade paler so the eye finds it,
   * split by dark zigzag cracks across the face that looks at the camera */
  int nc = 0;
  memset(g_crack_mesh, 0, sizeof g_crack_mesh);
  for (int y = 0; y < WORLD_H; y++)
    for (int x = 0; x < WORLD_W; x++)
      if (L->t[y][x] == T_CRACK && nc < 128) {
        int bi = L->biome[x];
        mb cb = { 0 };
        float lo[3], hi[3], dk[3];
        shade(lo, STONE[bi], 0.95f), shade(hi, STONE[bi], 1.15f), shade(dk, STONE[bi], 0.1f);
        mb_box_uv(&cb, 0, 0, 0, 0.49f, 0.49f, 1.45f, lo, hi, 4.0f, false);
        for (int k = 0; k < 2; k++) { /* two cracks from a corner region, each three kinked segments */
          float px = -0.35f + 0.7f * hh(x, y + k, 811), py = k ? 0.45f : -0.45f, dir = k ? -1.0f : 1.0f;
          for (int sgm = 0; sgm < 3; sgm++) {
            float dy = (0.22f + 0.12f * hh(x, y * 7 + k * 3 + sgm, 812)) * dir, dx = (hh(x, y * 5 + k * 3 + sgm, 813) - 0.5f) * 0.5f;
            if (px + dx > 0.44f || px + dx < -0.44f) dx = -dx;
            mb_cone(&cb, px, py, 1.452f, 0.03f - 0.006f * (float)sgm, 0.024f - 0.006f * (float)sgm, dy, dx, 0, 3, dk, dk);
            px += dx, py += dy;
          }
        }
        t3_geometry *bg = mb_done(&cb);
        g_surface[0]++;
        g_crack_mesh[nc++] = add_mesh(bg, g_mat[tile_mat(L, x, y) == MT_BRICK ? MT_BRICK : (bi == BIOME_CAVES ? MT_CAVE : MT_ROCK)], (float)x + 0.5f, (float)y + 0.5f, 0, true);
        t3_release(bg);
      }
  /* one-way ledges: a deck of planks on two joists, held up by posts that stand on the floor below, however far down
   * (a deck in the air with nothing under it floated; g.surface[2] also counts an end with no floor to stand on) */
  {
    mb deck = { 0 }, brace = { 0 };
    float bc0[3] = { 0.24f, 0.15f, 0.08f }, bc1[3] = { 0.34f, 0.22f, 0.12f }; /* the braces: plain wood colour (a cone carries no uvs) */
    float wd[3] = { 1, 1, 1 }, jo[3] = { 0.62f, 0.6f, 0.58f }, po0[3] = { 0.5f, 0.48f, 0.46f }, po1[3] = { 0.8f, 0.78f, 0.75f };
    for (int y = 0; y < WORLD_H; y++)
      for (int x = 0; x < WORLD_W; x++) {
        if (L->t[y][x] != T_ONEWAY || (x > 0 && L->t[y][x - 1] == T_ONEWAY)) continue;
        int x1 = x;
        while (x1 < WORLD_W && L->t[y][x1] == T_ONEWAY) x1++;
        float a0 = (float)x, a1 = (float)x1, cx = 0.5f * (a0 + a1), hw = 0.5f * (a1 - a0);
        mb_box_uv(&deck, cx, (float)y + 0.92f, 0, hw, 0.08f, 1.1f, wd, wd, 2.0f, false);                       /* the deck */
        for (int s2 = -1; s2 <= 1; s2 += 2)                                                             /* two joists */
          mb_box_uv(&deck, cx, (float)y + 0.76f, 0.75f * (float)s2, hw - 0.05f, 0.08f, 0.09f, jo, jo, 2.0f, false);
        /* a post under each end, down to the first floor */
        for (int e = 0; e < 2; e++) {
          int col = e ? x1 - 1 : x;
          int fy = y - 1;
          bool over_lava = false;
          while (fy >= 0 && !world_solid(L, col, fy)) over_lava |= L->t[fy][col] == T_LAVA, fy--; /* however deep: past spikes and lava, to the floor */
          if (fy < 0 && !over_lava) { g_ledge_unheld++; continue; } /* a lava span is lava to the bottom row: its pier stands on the bed */
          float bot = (float)fy + 1.0f, top = (float)y + 0.68f, px = e ? a1 - 0.2f : a0 + 0.2f;
          if (top - bot < 0.1f) continue;
          /* at the back only: a post in front of the walkway would stand between the camera and the players */
          if (over_lava) { /* wood would burn: a basalt pier stands in the lava */
            float st0[3] = { 0.1f, 0.075f, 0.07f }, st1[3] = { 0.26f, 0.2f, 0.18f };
            mb_box(&brace, px, 0.5f * (bot + top), -0.75f, 0.16f, 0.5f * (top - bot), 0.16f, st0, st1);
          } else
            mb_box_uv(&deck, px, 0.5f * (bot + top), -0.75f, 0.1f, 0.5f * (top - bot), 0.1f, po0, po1, 7.0f, true); /* one long board */
          if (!over_lava) place_rec(-1, px, bot, -0.75f, 0.1f, 0.1f);
          /* a diagonal brace from the post to the deck, at the back */
          mb_cone(&brace, px, top - fminf(0.9f, top - bot), -0.75f, 0.055f, 0.055f, fminf(0.9f, top - bot), e ? -0.7f : 0.7f, 0, 4, bc0, bc1);
        }
      }
    t3_geometry *gd = mb_done(&deck);
    if (gd) add_mesh(gd, g_mat[MT_WOOD], 0, 0, 0, true), t3_release(gd), g_surface[0]++;
    gd = mb_done(&brace);
    if (gd) add_mesh(gd, g_mat[MT_PROP], 0, 0, 0, true), t3_release(gd);
  }
  /* spikes */
  g_spike = t3_mesh_physical_material_new(0xc8ccd4);
  g_spike->metalness = 0.9f, g_spike->roughness = 0.3f;
  t3_geometry *cone = t3_cone_geometry_new(0.2f, 0.8f, 6, 1, false);
  for (int y = 0; y < WORLD_H; y++)
    for (int x = 0; x < WORLD_W; x++)
      if (L->t[y][x] == T_SPIKE)
        for (int k = 0; k < 2; k++) add_mesh(cone, g_spike, (float)x + 0.25f + 0.5f * (float)k, (float)y + 0.4f, 0.0f, true);
  t3_release(cone);
  /* lava: a glowing surface with a flowing crust, its top and the face that looks at the camera */
  if (!g_lava_m) g_lava_m = pbr(gen_lava, 2.0f, 1.0f, true, 1.7f);
  g_lava_m->vertex_colors = false;
  for (int x = 0; x < WORLD_W; x++)
    if (L->t[2][x] == T_LAVA) {
      int x1 = x;
      while (x1 < WORLD_W && L->t[2][x1] == T_LAVA) x1++;
      float w = (float)(x1 - x), top = 2.75f;
      if (g_nsurf + 2 > 128 || g_nlavas >= 48) break;
      g_lavas[g_nlavas++] = (span){ (float)x, (float)x1, 0, top };
      /* top: a grid so the surface can heave */
      int cols = (int)w * 3, rows = 6, nv = (cols + 1) * (rows + 1);
      float *pos = malloc(sizeof(float) * 3 * (size_t)nv), *nor = malloc(sizeof(float) * 3 * (size_t)nv), *uv = malloc(sizeof(float) * 2 * (size_t)nv);
      uint32_t *idx = malloc(sizeof(uint32_t) * 6 * (size_t)(cols * rows));
      for (int j = 0; j <= rows; j++)
        for (int i = 0; i <= cols; i++) {
          int v = j * (cols + 1) + i;
          pos[v * 3] = (float)x + w * (float)i / (float)cols, pos[v * 3 + 1] = top, pos[v * 3 + 2] = DEPTH_FRONT - 0.05f - (DEPTH_FRONT * 2 - 0.1f) * (float)j / (float)rows;
          nor[v * 3] = 0, nor[v * 3 + 1] = 1, nor[v * 3 + 2] = 0;
          uv[v * 2] = pos[v * 3] / 3.0f, uv[v * 2 + 1] = pos[v * 3 + 2] / 3.0f;
        }
      int k = 0;
      for (int j = 0; j < rows; j++)
        for (int i = 0; i < cols; i++) {
          uint32_t a = (uint32_t)(j * (cols + 1) + i), b2 = a + 1, c = a + (uint32_t)(cols + 1), d = c + 1;
          idx[k++] = a, idx[k++] = c, idx[k++] = b2, idx[k++] = b2, idx[k++] = c, idx[k++] = d;
        }
      t3_geometry *gt = t3_geometry_new();
      t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, nv, 3);
      at->usage = T3_DYNAMIC_DRAW;
      t3_geometry_set_attribute(gt, T3_ATTR_POSITION, at), t3_release(at);
      at = t3_attribute_new(T3_FLOAT32, nor, nv, 3);
      t3_geometry_set_attribute(gt, T3_ATTR_NORMAL, at), t3_release(at);
      at = t3_attribute_new(T3_FLOAT32, uv, nv, 2);
      at->usage = T3_DYNAMIC_DRAW;
      t3_geometry_set_attribute(gt, T3_ATTR_UV, at), t3_release(at);
      at = t3_attribute_new(T3_UINT32, idx, k, 1);
      t3_geometry_set_index(gt, at), t3_release(at);
      free(nor), free(idx);
      add_mesh(gt, g_lava_m, 0, 0, 0, false);
      g_surf[g_nsurf++] = (anim_surf){ gt, pos, uv, nv, (float)x, (float)x1, top, 0, 0, 0, cols };
      /* front: not a wall of lava. A bank of cooled basalt (its seams glow) holds the pool, a crust lip rides its top
       * edge, and only a thin band of molten lava shows above it, so the flowing top surface is what reads */
      float lip = top - 0.32f;
      {
        mb bank = { 0 };
        float fp[4][3] = { { (float)x, 0, DEPTH_FRONT }, { (float)x1, 0, DEPTH_FRONT }, { (float)x1, lip, DEPTH_FRONT }, { (float)x, lip, DEPTH_FRONT } };
        float fn[3] = { 0, 0, 1 }, fc[4][3] = { { 0.5f, 0.42f, 0.4f }, { 0.5f, 0.42f, 0.4f }, { 0.95f, 0.75f, 0.65f }, { 0.95f, 0.75f, 0.65f } };
        float fuv[4][2] = { { (float)x / 4, 0 }, { (float)x1 / 4, 0 }, { (float)x1 / 4, lip / 4 }, { (float)x / 4, lip / 4 } };
        mb_quad(&bank, fp, fn, fc, fuv);
        /* the crust lip: lumps of cooled rock along the edge */
        for (float lx = (float)x + 0.2f; lx < (float)x1; lx += 0.45f + 0.3f * hh((int)(lx * 7), 3, 700)) {
          float lo[3] = { 0.08f, 0.06f, 0.06f }, hi[3] = { 0.22f, 0.16f, 0.14f };
          float r = 0.18f + 0.16f * hh((int)(lx * 7), 4, 700);
          mb_ellip(&bank, lx, lip + 0.02f, DEPTH_FRONT - 0.05f, r, r * 0.55f, 0.2f, lo, hi, 8, 5);
        }
        t3_geometry *gb = mb_done(&bank);
        add_mesh(gb, g_mat[MT_BASALT], 0, 0, 0, false), t3_release(gb);
      }
      float fp[4][3] = { { (float)x, lip, DEPTH_FRONT - 0.08f }, { (float)x1, lip, DEPTH_FRONT - 0.08f }, { (float)x1, top, DEPTH_FRONT - 0.08f }, { (float)x, top, DEPTH_FRONT - 0.08f } };
      float fn[3] = { 0, 0, 1 }, fc[4][3] = { { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 } };
      float fuv[4][2] = { { (float)x / 3, lip / 3 }, { (float)x1 / 3, lip / 3 }, { (float)x1 / 3, top / 3 }, { (float)x / 3, top / 3 } };
      mb fb = { 0 };
      mb_quad(&fb, fp, fn, fc, fuv);
      t3_geometry *gf = mb_done(&fb);
      add_mesh(gf, g_lava_m, 0, 0, 0, false), t3_release(gf);
      x = x1 - 1;
    }
  /* water: a lit translucent front sheet (its top edge is CPU waves) with foam along it; the surface seen from above
   * is a Water3D node made in main.c from world_water_spans */
  t3_texture *rip = NULL;
  {
    uint8_t *nm = malloc(TN * TN * 4);
    texel *tx = malloc(sizeof *tx * TN * TN);
    for (int y = 0; y < TN; y++)
      for (int x = 0; x < TN; x++) gen_ripple(((float)x + 0.5f) / TN, ((float)y + 0.5f) / TN, &tx[y * TN + x]);
    for (int y = 0; y < TN; y++)
      for (int x = 0; x < TN; x++) {
        float hx = tx[y * TN + (x + 1) % TN].h - tx[y * TN + (x + TN - 1) % TN].h, hy = tx[((y + 1) % TN) * TN + x].h - tx[((y + TN - 1) % TN) * TN + x].h;
        float nx = -hx * 5.0f, ny = -hy * 5.0f, l = sqrtf(nx * nx + ny * ny + 1);
        int i = (y * TN + x) * 4;
        nm[i] = b8(nx / l * 0.5f + 0.5f), nm[i + 1] = b8(ny / l * 0.5f + 0.5f), nm[i + 2] = b8(1 / l * 0.5f + 0.5f), nm[i + 3] = 255;
      }
    rip = tex_finish(nm, false);
    free(nm), free(tx);
  }
  g_water_front = t3_mesh_physical_material_new(0xffffff);
  g_water_front->vertex_colors = true;
  g_water_front->transparent = true;
  g_water_front->opacity = 0.78f;
  g_water_front->roughness = 0.08f;
  g_water_front->depth_write = false;
  g_water_front->side = T3_DOUBLE_SIDE;
  t3_material_set_texture(g_water_front, T3_NORMAL_MAP, rip);
  g_water_front->normal_scale = (t3_vec2){ 0.5f, 0.5f };
  t3_release(rip);
  g_foam_m = basic_vcol(true, false, 1.0f);
  for (int x = 0; x < WORLD_W; x++) {
    int ty = -1;
    for (int y = 0; y < WORLD_H; y++)
      if (L->t[y][x] == T_WATER) ty = y;
    if (ty < 0) continue;
    int x1 = x;
    while (x1 < WORLD_W) {
      int top = -1;
      for (int y = 0; y < WORLD_H; y++)
        if (L->t[y][x1] == T_WATER) top = y;
      if (top != ty) break;
      x1++;
    }
    if (x1 == x || g_nsurf + 2 > 128 || g_nwaters >= 16) { x = x1 > x ? x1 - 1 : x; continue; }
    int low = ty;
    for (int xx = x; xx < x1; xx++)
      for (int y = 0; y < WORLD_H; y++)
        if (L->t[y][xx] == T_WATER && y < low) low = y;
    float bottom = (float)low, topy = (float)ty + 1.0f;
    g_waters[g_nwaters++] = (span){ (float)x, (float)x1, bottom, topy };
    int segs = (x1 - x) * 4, nv = (segs + 1) * 2;
    for (int pass = 0; pass < 2; pass++) { /* 0: the sheet, 1: the foam strip */
      float *pos = malloc(sizeof(float) * 3 * (size_t)nv), *nor = malloc(sizeof(float) * 3 * (size_t)nv), *col = malloc(sizeof(float) * 3 * (size_t)nv), *uv = malloc(sizeof(float) * 2 * (size_t)nv);
      uint32_t *idx = malloc(sizeof(uint32_t) * 6 * (size_t)segs);
      for (int i = 0; i <= segs; i++) {
        float px = (float)x + (float)i / 4.0f;
        for (int k = 0; k < 2; k++) {
          int v = i * 2 + k;
          pos[v * 3] = px, pos[v * 3 + 1] = k ? topy : bottom, pos[v * 3 + 2] = 1.25f;
          nor[v * 3] = 0, nor[v * 3 + 1] = 0, nor[v * 3 + 2] = 1;
          uv[v * 2] = px / 3.0f, uv[v * 2 + 1] = (k ? topy : bottom) / 3.0f;
          /* depth colour: bright turquoise at the surface, deep blue at the bottom */
          if (pass == 0) {
            float d = k ? 1.0f : 0.0f;
            col[v * 3] = mixf(0.0f, 0.04f, d), col[v * 3 + 1] = mixf(0.035f, 0.3f, d), col[v * 3 + 2] = mixf(0.1f, 0.42f, d);
          } else col[v * 3] = col[v * 3 + 1] = col[v * 3 + 2] = k ? 0.0f : 1.1f;
        }
      }
      for (int i = 0; i < segs; i++) {
        uint32_t a = (uint32_t)(i * 2), q[6] = { a, a + 2, a + 3, a, a + 3, a + 1 };
        memcpy(&idx[i * 6], q, sizeof q);
      }
      t3_geometry *wg = t3_geometry_new();
      t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, nv, 3);
      at->usage = T3_DYNAMIC_DRAW;
      t3_geometry_set_attribute(wg, T3_ATTR_POSITION, at), t3_release(at);
      at = t3_attribute_new(T3_FLOAT32, nor, nv, 3);
      t3_geometry_set_attribute(wg, T3_ATTR_NORMAL, at), t3_release(at);
      at = t3_attribute_new(T3_FLOAT32, col, nv, 3);
      t3_geometry_set_attribute(wg, T3_ATTR_COLOR, at), t3_release(at);
      at = t3_attribute_new(T3_FLOAT32, uv, nv, 2);
      at->usage = T3_DYNAMIC_DRAW;
      t3_geometry_set_attribute(wg, T3_ATTR_UV, at), t3_release(at);
      at = t3_attribute_new(T3_UINT32, idx, 6 * segs, 1);
      t3_geometry_set_index(wg, at), t3_release(at);
      free(nor), free(idx), free(col);
      add_mesh(wg, pass == 0 ? g_water_front : g_foam_m, 0, 0, 0, false);
      g_surf[g_nsurf++] = (anim_surf){ wg, pos, uv, nv, (float)x, (float)x1, topy, bottom, 1.25f, pass == 0 ? 1 : 2, segs };
    }
    x = x1 - 1;
  }
}

/* each frame: the lava heaves and flows, the water waves and its foam rides the crest */
void world_update_water_mesh(float time, float cam_x) {
  for (int k = 0; k < g_nsurf; k++) {
    anim_surf *s = &g_surf[k];
    if (s->x1 < cam_x - 45.0f || s->x0 > cam_x + 45.0f) continue; /* a long level has many: only what the camera can see moves */
    if (s->kind == 0) { /* lava top: uv slide and a slow heave */
      int cols = s->cols, rows = 6;
      for (int j = 0; j <= rows; j++)
        for (int i = 0; i <= cols; i++) {
          int v = j * (cols + 1) + i;
          float x = s->pos[v * 3], z = s->pos[v * 3 + 2];
          s->pos[v * 3 + 1] = s->y_top + 0.06f * sinf(x * 1.3f + time * 1.1f) + 0.05f * sinf(z * 2.0f + x * 0.7f - time * 0.8f);
          s->uv[v * 2] = x / 3.0f + time * 0.02f;
          s->uv[v * 2 + 1] = z / 3.0f + time * 0.045f;
        }
    } else { /* water sheet or foam */
      for (int i = 0; i <= s->cols; i++) {
        float px = s->x0 + (float)i / 4.0f;
        float h = s->y_top - 0.06f + 0.07f * sinf(px * 1.7f + time * 2.1f) + 0.04f * sinf(px * 3.3f - time * 1.3f);
        s->pos[(i * 2 + 1) * 3 + 1] = s->kind == 1 ? h : h + 0.16f;
        if (s->kind == 2) s->pos[(i * 2) * 3 + 1] = h - 0.1f;
        s->uv[(i * 2) * 2] = px / 3.0f + time * 0.05f, s->uv[(i * 2 + 1) * 2] = px / 3.0f + time * 0.05f;
      }
    }
    t3_attribute *a = s->geo->attributes[T3_ATTR_POSITION];
    memcpy(a->array, s->pos, sizeof(float) * 3 * (size_t)s->nv);
    a->version++;
    a = s->geo->attributes[T3_ATTR_UV];
    if (a) {
      memcpy(a->array, s->uv, sizeof(float) * 2 * (size_t)s->nv);
      a->version++;
    }
  }
}

void world_hide_crack(int x, int y) {
  for (int i = 0; i < 128 && g_crack_mesh[i]; i++) {
    t3_mesh *m = g_crack_mesh[i];
    if ((int)floorf(m->base.position.x) == x && (int)floorf(m->base.position.y) == y) m->base.visible = false;
  }
}

/* ── the things ──────────────────────────────────────────────────────
 * Each is built from boxes, cones and blobs in vertex colours on the props' material, so no new draw state. A thing that moves
 * is its own mesh, moved by world_things_sync; g_thing_mesh[k][1] is its second part where it has one. */
static t3_mesh *g_thing_mesh[MAX_THINGS][2];
static void c3(float *o, float r, float g, float b) { o[0] = r, o[1] = g, o[2] = b; }
static t3_mesh *thing_add(mb *b, float x, float y, float z, bool shadow) {
  t3_geometry *ge = mb_done(b);
  if (!ge) return NULL;
  t3_mesh *m = add_mesh(ge, g_mat[MT_PROP], x, y, z, shadow);
  t3_release(ge);
  return m;
}
/* a sign: a post on the ground and a board above head height, a little crooked */
static void sign_prop(mb *p, float x, float y, float z, int seed) {
  float w0[3], w1[3], d0[3], d1[3];
  c3(w0, 0.36f, 0.24f, 0.13f), c3(w1, 0.55f, 0.38f, 0.2f), c3(d0, 0.62f, 0.5f, 0.3f), c3(d1, 0.85f, 0.72f, 0.46f);
  mb_box(p, x, y + 1.0f, z, 0.07f, 1.0f, 0.07f, w0, w1);
  float tilt = 0.08f * (hh(seed, 3, 951) - 0.5f);
  mb_box(p, x + tilt * 3, y + 2.15f, z + 0.05f, 0.85f, 0.42f, 0.05f, d0, d1);
  c3(d0, 0.2f, 0.13f, 0.07f);
  mb_box(p, x + tilt * 3, y + 2.57f, z + 0.06f, 0.88f, 0.04f, 0.06f, d0, d0);
  mb_box(p, x + tilt * 3, y + 1.73f, z + 0.06f, 0.88f, 0.04f, 0.06f, d0, d0);
  /* the small print, seen from too far away to read: come closer */
  for (int i = 0; i < 4; i++) {
    float w = 0.45f + 0.28f * hh(seed, i, 952);
    mb_box(p, x + tilt * 3 - 0.72f + w, y + 2.38f - 0.14f * (float)i, z + 0.105f, w, 0.028f, 0.004f, d0, d0);
  }
}
void world_things_build(game *g) {
  const level *L = &g->L;
  memset(g_thing_mesh, 0, sizeof g_thing_mesh);
  mb still = { 0 };
  float a[3], b[3], c[3], d[3];
  for (int k = 0; k < L->nthings; k++) {
    const thing *t = &L->things[k];
    mb m = { 0 }, m2 = { 0 };
    switch (t->kind) {
      case TH_SPRING: /* a red pad on a steel coil, on a base plate */
        c3(a, 0.3f, 0.32f, 0.36f), c3(b, 0.55f, 0.58f, 0.62f), c3(c, 0.75f, 0.12f, 0.1f), c3(d, 1.0f, 0.3f, 0.22f);
        mb_box(&still, t->x, t->y + 0.05f, 0, 0.75f, 0.05f, 0.75f, a, b);
        for (int i = 0; i < 4; i++) mb_box(&still, t->x, t->y + 0.14f + 0.1f * (float)i, 0, 0.42f - 0.03f * (float)(i & 1), 0.03f, 0.42f - 0.03f * (float)(i & 1), a, b);
        mb_box(&still, t->x, t->y + 0.56f, 0, 0.8f, 0.09f, 0.8f, c, d);
        break;
      case TH_FAN: /* a steel ring and four blades turning in it, low on its housing; a faint draught of streaks above */
        c3(a, 0.28f, 0.3f, 0.34f), c3(b, 0.5f, 0.54f, 0.6f);
        mb_box(&still, t->x, t->y + 0.12f, 0, t->w, 0.12f, 1.1f, a, b);
        c3(c, 0.7f, 0.74f, 0.8f), c3(d, 0.9f, 0.92f, 0.96f);
        mb_box(&m, 0, 0, 0, t->w * 0.92f, 0.03f, 0.14f, c, d);
        mb_box(&m, 0, 0, 0, 0.14f, 0.03f, 0.98f, c, d);
        g_thing_mesh[k][0] = thing_add(&m, t->x, t->y + 0.3f, 0, false);
        break;
      case TH_SAW: { /* a toothed steel disc on a rail between two posts that stand on the floor */
        c3(a, 0.28f, 0.28f, 0.3f), c3(b, 0.42f, 0.42f, 0.46f);
        float y0 = t->h, rail = t->y;
        mb_box(&still, t->x, rail, -1.0f, t->a + 1.0f, 0.06f, 0.06f, a, b);
        mb_box(&still, t->x - t->a - 1.0f, 0.5f * (y0 + rail), -1.0f, 0.08f, 0.5f * (rail - y0), 0.08f, a, b);
        mb_box(&still, t->x + t->a + 1.0f, 0.5f * (y0 + rail), -1.0f, 0.08f, 0.5f * (rail - y0), 0.08f, a, b);
        /* a toothed disc of dark steel, both faces: 14 raked teeth in the camera's plane, a bright ground edge behind each tip */
        c3(c, 0.2f, 0.21f, 0.24f), c3(d, 0.72f, 0.74f, 0.78f);
        for (int side = 0; side < 2; side++) {
          float nz = side ? -1.0f : 1.0f, z = nz * 0.05f, nrm[3] = { 0, 0, nz };
          mb_room(&m, 60, 90);
          int hub = mb_vert(&m, 0, 0, z, nrm[0], nrm[1], nrm[2], c, 0, 0);
          int first = -1, prev = -1;
          for (int i = 0; i <= 28; i++) {
            float an = (float)(i % 28) * 6.2831853f / 28.0f + ((i & 1) ? 0.13f : 0.0f), r = (i & 1) ? 0.92f : 0.68f;
            int v = mb_vert(&m, cosf(an) * r, sinf(an) * r, z, nrm[0], nrm[1], nrm[2], (i & 1) ? d : c, 0, 0);
            if (first < 0) first = v;
            if (prev >= 0) { if (side) mb_tri_i(&m, hub, v, prev); else mb_tri_i(&m, hub, prev, v); }
            prev = v;
          }
        }
        c3(a, 0.1f, 0.1f, 0.11f), c3(b, 0.3f, 0.3f, 0.33f);
        mb_ellip(&m, 0, 0, 0, 0.2f, 0.2f, 0.09f, a, b, 12, 4); /* the hub */
        c3(a, 0.42f, 0.03f, 0.04f), c3(b, 0.56f, 0.05f, 0.05f); /* she has been working */
        mb_ellip(&m, 0.34f, 0.2f, 0.06f, 0.2f, 0.11f, 0.008f, a, b, 8, 4);
        mb_ellip(&m, -0.3f, -0.3f, 0.06f, 0.14f, 0.09f, 0.008f, a, b, 8, 4);
        g_thing_mesh[k][0] = thing_add(&m, t->x, t->y, 0, true);
        break;
      }
      case TH_PRESS: { /* a heavy block on a frame of two posts on the floor */
        c3(a, 0.26f, 0.27f, 0.3f), c3(b, 0.42f, 0.44f, 0.48f);
        float top = t->y + t->h + 0.6f;
        for (int s = -1; s <= 1; s += 2) mb_box(&still, t->x + (float)s * (t->w + 0.3f), 0.5f * (t->y + top), -1.0f, 0.12f, 0.5f * (top - t->y), 0.12f, a, b);
        mb_box(&still, t->x, top + 0.12f, -1.0f, t->w + 0.42f, 0.14f, 0.14f, a, b);
        c3(c, 0.22f, 0.22f, 0.24f), c3(d, 0.72f, 0.58f, 0.12f);
        mb_box(&m, 0, 0.8f, 0, t->w, 0.8f, 1.2f, c, d);
        /* the warning stripes on its face */
        c3(a, 0.1f, 0.1f, 0.1f);
        for (int i = -1; i <= 1; i++) mb_box(&m, (float)i * t->w * 0.6f, 0.8f, 1.21f, 0.14f, 0.7f, 0.01f, a, a);
        g_thing_mesh[k][0] = thing_add(&m, t->x, t->y, 0, true);
        break;
      }
      case TH_FALLER: { /* a grand piano, or in the crater a boulder. It hangs on a rope from nothing you can see, which is the joke */
        int bi = L->biome[(int)t->x];
        if (bi == BIOME_VOLCANO) {
          c3(c, 0.2f, 0.16f, 0.15f), c3(d, 0.45f, 0.36f, 0.3f);
          mb_rock(&m, 0, 1.0f, 0, 1.05f, 1.0f, 1.05f, c, d, k * 7 + 3);
        } else {
          c3(c, 0.03f, 0.03f, 0.035f), c3(d, 0.12f, 0.12f, 0.14f);
          mb_box(&m, 0, 1.0f, 0, 1.1f, 0.34f, 0.8f, c, d);       /* the case */
          mb_ellip(&m, 0.75f, 1.0f, 0, 0.7f, 0.33f, 0.72f, c, d, 14, 6);
          c3(a, 0.9f, 0.9f, 0.86f), c3(b, 1, 1, 0.96f);
          mb_box(&m, -0.95f, 1.2f, 0, 0.18f, 0.06f, 0.7f, a, b); /* keys */
          c3(a, 0.03f, 0.03f, 0.03f);
          for (int i = 0; i < 3; i++) mb_box(&m, (i == 0 ? -0.8f : 0.8f) , 0.34f, (i == 2 ? -0.5f : 0.5f), 0.07f, 0.34f, 0.07f, a, a); /* legs */
          c3(a, 0.25f, 0.18f, 0.1f), c3(b, 0.42f, 0.32f, 0.2f);
          mb_box(&m, 0, 7.0f, 0, 0.025f, 5.6f, 0.025f, a, b);    /* the rope */
        }
        g_thing_mesh[k][0] = thing_add(&m, t->x, t->y + t->h, 0, true);
        break;
      }
      case TH_CANNON: { /* a fat barrel on a carriage, aimed along its throw, and on its side the only instruction */
        c3(a, 0.36f, 0.24f, 0.13f), c3(b, 0.5f, 0.34f, 0.2f);
        mb_box(&still, t->x, t->y + 0.25f, 0, 0.8f, 0.25f, 0.5f, a, b);
        c3(c, 0.08f, 0.09f, 0.11f), c3(d, 0.28f, 0.3f, 0.34f);
        float len = sqrtf(t->a * t->a + t->b * t->b), lx = t->a / len, ly = t->b / len;
        /* a barrel of rings along its aim, a band every so often, and a black mouth */
        for (int i = 0; i < 9; i++) {
          float k = (float)i * 0.2f - 0.3f, r = i == 8 ? 0.52f : (i & 3) == 1 ? 0.5f : 0.44f;
          mb_ellip(&still, t->x + lx * k, t->y + 0.62f + ly * k, 0, r, r, r, c, d, 12, 6);
        }
        c3(a, 0.0f, 0.0f, 0.0f);
        mb_ellip(&still, t->x + lx * 1.42f, t->y + 0.62f + ly * 1.42f, 0, 0.32f, 0.32f, 0.32f, a, a, 12, 6);
        break;
      }
      case TH_PLATE: /* a slab with a raised pad, sinks a little when pressed */
        c3(a, 0.3f, 0.3f, 0.3f), c3(b, 0.45f, 0.45f, 0.45f), c3(c, 0.7f, 0.5f, 0.1f), c3(d, 1.0f, 0.78f, 0.2f);
        mb_box(&still, t->x, t->y + 0.04f, 0, 0.95f, 0.04f, 0.8f, a, b);
        mb_box(&m, 0, 0, 0, 0.75f, 0.06f, 0.6f, c, d);
        g_thing_mesh[k][0] = thing_add(&m, t->x, t->y + 0.14f, 0, false);
        break;
      case TH_DOOR: /* a portcullis: bars and cross bars, running up into the wall */
        c3(a, 0.18f, 0.18f, 0.2f), c3(b, 0.38f, 0.38f, 0.42f);
        for (int i = 0; i < 5; i++) mb_box(&m, t->w * (0.1f + 0.2f * (float)i) , t->h * 0.5f, 0, 0.08f, t->h * 0.5f, 1.3f, a, b);
        for (int i = 0; i < 4; i++) mb_box(&m, t->w * 0.5f, t->h * (0.12f + 0.25f * (float)i), 0, t->w * 0.5f, 0.07f, 1.32f, a, b);
        g_thing_mesh[k][0] = thing_add(&m, t->x, t->y, 0, true);
        break;
      case TH_CRUMBLE: /* a plank on its way out: a split board on a rough beam */
        c3(a, 0.44f, 0.3f, 0.16f), c3(b, 0.66f, 0.48f, 0.26f);
        for (int i = 0; i < 3; i++) mb_box(&m, 0.2f + 0.3f * (float)i, 0.86f, 0, 0.14f, 0.1f, 1.1f + 0.15f * hh(k, i, 960), a, b);
        c3(c, 0.25f, 0.16f, 0.08f), c3(d, 0.4f, 0.27f, 0.14f);
        mb_box(&m, 0.5f, 0.66f, 0, 0.48f, 0.1f, 0.3f, c, d);
        g_thing_mesh[k][0] = thing_add(&m, t->x, t->y, 0, true);
        break;
      case TH_MIMIC: { /* a coin. It stands still, unlike the others; a dark line where a mouth would be */
        float g0[3] = { 0.6f, 0.4f, 0.06f }, g1[3] = { 1.0f, 0.8f, 0.2f }, mo[3] = { 0.05f, 0.02f, 0.02f };
        mb_ellip(&m, 0, 0, 0, 0.3f, 0.3f, 0.07f, g0, g1, 16, 5);
        mb_box(&m, 0, -0.03f, 0.075f, 0.17f, 0.018f, 0.005f, mo, mo);
        g_thing_mesh[k][0] = thing_add(&m, t->x, t->y, 0, true);
        break;
      }
      case TH_BOOM: { /* the post of a sign like any other, and a small red light that should not be there */
        float rd0[3] = { 1.6f, 0.1f, 0.05f }, rd1[3] = { 3.0f, 0.3f, 0.1f };
        sign_prop(&m, 0, 0, -0.9f, k * 5 + 1);
        mb_ellip(&m, 0.72f, 2.5f, -0.78f, 0.07f, 0.07f, 0.07f, rd0, rd1, 8, 4);
        g_thing_mesh[k][0] = thing_add(&m, t->x, t->y, 0, true);
        break;
      }
      case TH_FAKE: /* the same booth, just standing there, on one mesh so it can fall over */
        c3(a, 0.5f, 0.32f, 0.16f), c3(b, 0.72f, 0.5f, 0.28f), c3(c, 0.74f, 0.14f, 0.12f), c3(d, 0.96f, 0.92f, 0.82f);
        mb_box(&m, 0, 1.1f, -1.1f, 1.5f, 1.1f, 0.3f, a, b);
        mb_box(&m, -1.45f, 1.1f, -0.2f, 0.08f, 1.1f, 0.9f, a, b);
        mb_box(&m, 1.45f, 1.1f, -0.2f, 0.08f, 1.1f, 0.9f, a, b);
        mb_box(&m, 0, 0.5f, 0.55f, 1.5f, 0.5f, 0.1f, a, b);
        for (int i = 0; i < 6; i++) { float *col = (i & 1) ? c : d; mb_box(&m, -1.25f + 0.5f * (float)i, 2.35f, -0.15f, 0.25f, 0.1f, 1.1f, col, col); }
        c3(a, 0.3f, 0.32f, 0.36f), c3(b, 0.82f, 0.84f, 0.88f);
        mb_box(&m, 0, 3.1f, 0, 1.2f, 0.42f, 0.05f, a, b);
        g_thing_mesh[k][0] = thing_add(&m, t->x, t->y, 0, true);
        break;
      case TH_CHASER: { /* enormous, round, per region, and it has a face. The face does not turn with the rest */
        static const float C0[BIOMES][3] = { { 0.1f, 0.26f, 0.08f }, { 0.12f, 0.13f, 0.2f }, { 0.4f, 0.34f, 0.22f }, { 0.75f, 0.82f, 0.92f }, { 0.25f, 0.06f, 0.03f } };
        static const float C1[BIOMES][3] = { { 0.28f, 0.52f, 0.16f }, { 0.34f, 0.36f, 0.5f }, { 0.8f, 0.7f, 0.5f }, { 1.1f, 1.15f, 1.25f }, { 0.75f, 0.2f, 0.06f } };
        int bi = L->biome[(int)t->a];
        mb_rock(&m, 0, 0, 0, t->w, t->w, t->w * 0.8f, C0[bi], C1[bi], k * 3 + 11);
        g_thing_mesh[k][0] = thing_add(&m, t->x, t->y + t->w, 0, true);
        c3(a, 1.5f, 1.5f, 1.45f), c3(b, 0.02f, 0.02f, 0.02f), c3(c, 0.5f, 0.05f, 0.05f);
        /* its face looks where it is going, on the side toward the camera: eyes too big, one a bit higher, a very small smile */
#define ONFACE(X, Y) (t->w * 0.8f * sqrtf(fmaxf(0.02f, 1.0f - ((X) * (X) + (Y) * (Y)) / (t->w * t->w))) + 0.08f)
        for (int s = 0; s < 2; s++) {
          float ex = s ? 2.0f : 0.5f, ey = s ? 1.05f : 1.3f, ez = ONFACE(ex, ey);
          mb_ellip(&m2, ex, ey, ez, 0.72f, 0.78f, 0.16f, a, a, 14, 6);
          mb_ellip(&m2, ex + 0.26f, ey - 0.12f, ez + 0.16f, 0.27f, 0.29f, 0.05f, b, b, 10, 4);
        }
        { float mz = ONFACE(1.2f, -1.0f); mb_box(&m2, 1.2f, -1.0f, mz, 0.85f, 0.2f, 0.06f, c, c);
          for (int i = 0; i < 4; i++) mb_box(&m2, 0.55f + 0.43f * (float)i, -0.86f, mz + 0.05f, 0.16f, 0.1f, 0.03f, a, a); }
#undef ONFACE
        g_thing_mesh[k][1] = thing_add(&m2, t->x, t->y + t->w, 0, true);
        break;
      }
      case TH_BOOTH: { /* the booth at the end: where an unimpressed clerk has been waiting for you to finish */
        c3(a, 0.5f, 0.32f, 0.16f), c3(b, 0.72f, 0.5f, 0.28f), c3(c, 0.74f, 0.14f, 0.12f), c3(d, 0.96f, 0.92f, 0.82f);
        mb_box(&still, t->x, t->y + 1.1f, -1.1f, 1.5f, 1.1f, 0.3f, a, b);       /* the back wall */
        mb_box(&still, t->x - 1.45f, t->y + 1.1f, -0.2f, 0.08f, 1.1f, 0.9f, a, b);
        mb_box(&still, t->x + 1.45f, t->y + 1.1f, -0.2f, 0.08f, 1.1f, 0.9f, a, b);
        mb_box(&still, t->x, t->y + 0.5f, 0.55f, 1.5f, 0.5f, 0.1f, a, b);       /* the counter */
        for (int i = 0; i < 6; i++) { /* a striped awning */
          float *col = (i & 1) ? c : d;
          mb_box(&still, t->x - 1.5f + 0.25f + 0.5f * (float)i, t->y + 2.35f, -0.15f, 0.25f, 0.1f, 1.1f, col, col);
        }
        c3(a, 0.3f, 0.32f, 0.36f), c3(b, 0.82f, 0.84f, 0.88f);
        mb_box(&still, t->x, t->y + 3.1f, 0, 1.2f, 0.42f, 0.05f, a, b);         /* a board for the word over the booth */
        mb_box(&still, t->x - 1.1f, t->y + 2.5f, 0, 0.05f, 0.3f, 0.05f, a, a);
        mb_box(&still, t->x + 1.1f, t->y + 2.5f, 0, 0.05f, 0.3f, 0.05f, a, a);
        break;
      }
      default: break;
    }
    (void)m2;
  }
  for (int k = 0; k < L->nsigns; k++) sign_prop(&still, L->signs[k].x, L->signs[k].y, -0.9f, k * 11 + 2);
  t3_mesh *sm = thing_add(&still, 0, 0, 0, true);
  (void)sm;
}

/* ── the near layer: dark ground between the camera and the play, low in the frame, for depth. It hides the deep rock that is
 * the plainest thing in the picture and gives the eye something to look past. Its top stays well below the lowest ground
 * for a dozen columns either way, so in the camera's perspective it never covers a player. Everything on it stands on it. */
static bool g_near_bug_on(void) { return cw_test_env("COOP_NEAR_BUG") != NULL; } /* a gate switch, from frame 2 */
static float near_top(const level *L, int x) {
  float low = 1e9f;
  for (int k = x - 14; k <= x + 14; k++) {
    if (k < 1 || k >= L->width) continue;
    int g = L->ground[k];
    if (g >= 4 && g < WORLD_H && (float)g < low) low = (float)g;
  }
  return low > 1e8f ? 0.0f : (g_near_bug_on() ? 7.0f : 0.0f) + low - 1.2f - 1.6f * wn((float)x, 0, 0.1f, 970) - 0.5f * wn((float)x, 3, 0.4f, 971);
}
float world_near_height(const level *L, float x) {
  int c = (int)floorf(x);
  c = c < 1 ? 1 : c >= L->width ? L->width - 1 : c;
  return near_top(L, c);
}
void world_near_build(game *g) {
  const level *L = &g->L;
  if (L->width <= 0) return;
  enum { Z = 7 };
  static const float C0[BIOMES][3] = { { 0.012f, 0.03f, 0.012f }, { 0.006f, 0.008f, 0.02f }, { 0.03f, 0.028f, 0.018f }, { 0.08f, 0.1f, 0.14f }, { 0.016f, 0.006f, 0.006f } };
  static const float C1[BIOMES][3] = { { 0.03f, 0.075f, 0.022f }, { 0.012f, 0.018f, 0.04f }, { 0.07f, 0.065f, 0.04f }, { 0.2f, 0.25f, 0.34f }, { 0.05f, 0.016f, 0.012f } };
  for (int cx = -40; cx < L->width + 40; cx += 32) {
    mb m = { 0 };
    mb_room(&m, 64 * 2, 64 * 3);
    int prev0 = -1, prev1 = -1;
    for (int i = 0; i <= 32; i++) {
      int x = cx + i;
      int xi = x < 0 ? 0 : x >= WORLD_W ? WORLD_W - 1 : x;
      int bi = L->biome[xi >= L->width ? L->width - 1 : xi];
      float top = near_top(L, x < 1 ? 1 : x >= L->width ? L->width - 1 : x);
      float n[3] = { 0, 0.3f, 0.95f };
      int a = mb_vert(&m, (float)x, top, Z, n[0], n[1], n[2], C1[bi], (float)x * 0.1f, 1);
      int b = mb_vert(&m, (float)x, top - 30.0f, Z, n[0], n[1], n[2], C0[bi], (float)x * 0.1f, 0);
      if (prev0 >= 0) { mb_tri_i(&m, prev1, b, prev0); mb_tri_i(&m, prev0, b, a); }
      prev0 = a, prev1 = b;
    }
    /* what stands on it: per region, sparse, never a row */
    for (int x = cx; x < cx + 32; x++) {
      int xi = x < 1 ? 1 : x >= L->width ? L->width - 1 : x;
      float top = near_top(L, xi), h = hh(x + 2000, 5, 972);
      int bi = L->biome[xi];
      if (bi == BIOME_MEADOW) {
        if (h < 0.6f) for (int k = 0; k < 5; k++) mb_cone(&m, (float)x + hh(x + 2000, k, 973), top - 0.05f, Z, 0.09f, 0.008f, 0.5f + 0.9f * hh(x + 2000, k, 974), (hh(x + 2000, k, 975) - 0.5f) * 0.5f, 0, 3, C1[bi], C1[bi]);
      } else if (bi == BIOME_CAVES || bi == BIOME_VOLCANO) {
        if (h < 0.14f) mb_cone(&m, (float)x + 0.5f, top - 0.1f, Z, 0.5f + 0.4f * h * 7, 0.03f, 1.0f + 2.0f * hh(x + 2000, 7, 976), 0.2f, 0, 5, C0[bi], C1[bi]);
      } else if (bi == BIOME_RUINS) {
        if (h < 0.06f) mb_box(&m, (float)x + 0.5f, top + 0.7f, Z, 0.4f, 0.8f + 0.5f * h * 16, 0.4f, C0[bi], C1[bi]);
      } else {
        if (h < 0.2f) mb_ellip(&m, (float)x + 0.5f, top - 0.05f, Z, 0.9f + 1.2f * h * 5, 0.35f, 0.5f, C1[bi], C1[bi], 10, 4);
      }
    }
    t3_geometry *ge = mb_done(&m);
    if (ge) add_mesh(ge, g_mat[MT_PROP], 0, 0, 0, false), t3_release(ge);
  }
}

/* where the rules have them. time spins the fans and the saws */
void world_things_sync(const game *g, float time) {
  const level *L = &g->L;
  for (int k = 0; k < L->nthings; k++) {
    const thing *t = &L->things[k];
    t3_mesh *m = g_thing_mesh[k][0];
    if (!m) continue;
    switch (t->kind) {
      case TH_FAN: t3_object_set_rotation(m, 0, time * 18.0f, 0); break;
      case TH_MIMIC: {
        m->base.visible = t->state != 2;
        float hop = t->state == 1 ? t->dy : 0.0f;
        t3_object_set_position(m, t->x + hop * 0.4f, t->y + hop, 0);
        /* it wiggles a tiny bit, every few seconds, and it does not spin */
        t3_object_set_rotation(m, 0, 0, t->state == 1 ? time * 16.0f : 0.14f * sinf(time * 28.0f) * (sinf(time * 1.3f + (float)k) > 0.92f ? 1.0f : 0.0f));
        break;
      }
      case TH_BOOM:
        m->base.visible = t->state != 2;
        if (t->state == 1) { float k2 = t->t * 14.0f; t3_object_set_position(m, t->x + 0.03f * sinf(k2 * 4.0f), t->y, 0); }
        break;
      case TH_FAKE: t3_object_set_rotation(m, t->state == 1 ? -fminf(1.4f, t->t * 1.8f) : 0.0f, 0, 0); break;
      case TH_CHASER: {
        float cx = t->state == 0 ? t->x : t->t, cy = t->y + t->w;
        m->base.visible = t->state != 2;
        t3_object_set_position(m, cx, cy, 0);
        t3_object_set_rotation(m, 0, 0, -t->dy / t->w);
        if (g_thing_mesh[k][1]) { g_thing_mesh[k][1]->base.visible = t->state != 2; t3_object_set_position(g_thing_mesh[k][1], cx, cy, 0); }
        break;
      }
      case TH_SAW:
        t3_object_set_position(m, t->t, t->y, 0);
        t3_object_set_rotation(m, 0, 0, -time * 14.0f);
        break;
      case TH_PRESS: t3_object_set_position(m, t->x, t->y + (t->h - 1.6f) * (1.0f - t->dy), 0); break;
      case TH_FALLER: t3_object_set_position(m, t->x, t->y + t->h - t->dy, 0); m->base.visible = t->state != 2 || t->t < 5.5f; break;
      case TH_PLATE: t3_object_set_position(m, t->x, t->y + (t->pressed ? 0.09f : 0.14f), 0); break;
      case TH_DOOR: t3_object_set_position(m, t->x, t->y + t->dy, 0); break;
      case TH_CRUMBLE: {
        float wob = t->state == 1 ? 0.03f * sinf(time * 60.0f + (float)k) : 0.0f, drop = t->state == 2 ? 14.0f * t->t * t->t : 0.0f;
        t3_object_set_position(m, t->x + wob, t->y - drop, 0);
        m->base.visible = !(t->state == 2 && t->t > 1.2f);
        break;
      }
      default: break;
    }
  }
}

/* the things that stand on the ground stand on the ground; a sign in the air would be an embarrassment */
int world_things_check(const level *L, int *checked) {
  int bad = 0, n = 0;
  for (int k = 0; k < L->nthings; k++) {
    const thing *t = &L->things[k];
    if (t->kind == TH_DOOR || t->kind == TH_CRUMBLE || t->kind == TH_FALLER || t->kind == TH_MIMIC || t->kind == TH_CHASER) continue;
    float base = t->kind == TH_SAW ? t->h : t->y;
    int y = (int)floorf(base) - 1, xs[2] = { (int)floorf(t->x - (t->kind == TH_SAW ? t->a + 1.0f : 0.0f)), (int)floorf(t->x + (t->kind == TH_SAW ? t->a + 1.0f : 0.0f)) };
    n++;
    bool ok = true;
    for (int i = 0; i < 2; i++) if (!world_solid(L, xs[i], y) || world_solid(L, xs[i], y + 1)) ok = false;
    if (!ok) { bad++; printf("things: %d (kind %d) at %.1f,%.1f not on the ground\n", k, t->kind, (double)t->x, (double)base); }
  }
  for (int k = 0; k < L->nsigns; k++) {
    const sign_t *s = &L->signs[k];
    n++;
    int x = (int)floorf(s->x), y = (int)floorf(s->y) - 1;
    if (!world_solid(L, x, y) || world_solid(L, x, y + 1)) { bad++; printf("things: sign %d at %.1f,%.1f not on the ground: %s\n", k, (double)s->x, (double)s->y, s->text); }
  }
  *checked = n;
  return bad;
}

/* ── backdrop: a sky of five colour bands, a sun, drifting clouds, and four layers of silhouettes per region at
 * different depths (the camera's perspective makes the parallax). Each region's layers rise out of the ground where
 * the region begins and sink again where it ends, so the scenery changes with the ground under the players. ── */

static const float SKY_HOR[BIOMES][3] = { { 0.56f, 0.7f, 0.86f }, { 0.07f, 0.1f, 0.2f }, { 0.5f, 0.68f, 0.62f }, { 0.72f, 0.82f, 0.92f }, { 1.0f, 0.4f, 0.12f } };
static const float SKY_MID[BIOMES][3] = { { 0.24f, 0.46f, 0.86f }, { 0.04f, 0.05f, 0.12f }, { 0.2f, 0.42f, 0.5f }, { 0.45f, 0.62f, 0.82f }, { 0.4f, 0.1f, 0.08f } };
static const float SKY_TOP[BIOMES][3] = { { 0.16f, 0.38f, 0.82f }, { 0.015f, 0.02f, 0.06f }, { 0.1f, 0.25f, 0.34f }, { 0.22f, 0.36f, 0.62f }, { 0.08f, 0.02f, 0.03f } };
/* silhouette colours per region and layer, far to near (already hazed toward the sky: far layers are pale) */
static const float LAYER_COL[BIOMES][4][3] = {
  { { 0.62f, 0.72f, 0.88f }, { 0.42f, 0.6f, 0.62f }, { 0.16f, 0.4f, 0.27f }, { 0.1f, 0.3f, 0.12f } },
  { { 0.08f, 0.1f, 0.2f }, { 0.06f, 0.08f, 0.16f }, { 0.05f, 0.06f, 0.13f }, { 0.04f, 0.05f, 0.1f } },
  { { 0.55f, 0.7f, 0.68f }, { 0.4f, 0.56f, 0.52f }, { 0.3f, 0.42f, 0.36f }, { 0.4f, 0.36f, 0.26f } },
  { { 0.74f, 0.84f, 0.94f }, { 0.6f, 0.72f, 0.86f }, { 0.46f, 0.58f, 0.74f }, { 0.34f, 0.44f, 0.6f } },
  { { 0.5f, 0.15f, 0.1f }, { 0.32f, 0.08f, 0.07f }, { 0.16f, 0.05f, 0.05f }, { 0.07f, 0.035f, 0.035f } },
};
static const float LAYER_Z[4] = { -125, -85, -48, -9 };

static float g_bx0[BIOMES], g_bx1[BIOMES];
static float g_gs[WORLD_W + 1];
static t3_mesh *g_clouds[12];
static float g_cloud_x[12];
static int g_ncloud;
static t3_mesh *g_sun;

/* 0..1: how much of region b is present at x (1 inside, easing over 30 units at the borders) */
static int g_bfirst, g_blast; /* the first and last region this level has */
static float bmask(int b, float x) {
  if (g_bx1[b] <= g_bx0[b]) return 0.0f; /* a region this level does not have */
  float lo = b == g_bfirst ? 1.0f : sstep(g_bx0[b] - 14.0f, g_bx0[b] + 40.0f, x);
  float hi = b == g_blast ? 1.0f : 1.0f - sstep(g_bx1[b] - 40.0f, g_bx1[b] + 14.0f, x);
  return lo * hi;
}
static float ridge(float x, float sc, int s) { return 1.0f - fabsf(2.0f * fbm(x * sc, 0.31f, 8, 5, s) - 1.0f); }
static float smoothg(float x) {
  int i = (int)floorf(x);
  i = i < 0 ? 0 : i >= WORLD_W ? WORLD_W - 1 : i;
  return g_gs[i];
}
static float tri(float x) { x -= floorf(x); return x < 0.5f ? x * 2.0f : 2.0f - x * 2.0f; }

/* silhouette heights (world y of the top edge) per region and layer */
static float profile(int b, int l, float x) {
  float u = x / 300.0f;
  switch (b) {
    case BIOME_MEADOW:
      if (l == 0) return 24.0f + 34.0f * ridge(x, 1.0f / 260.0f, 80);
      if (l == 1) return 22.0f + 16.0f * fbm(u * 3.0f, 0.7f, 6, 4, 82);
      if (l == 2) return 20.0f + 7.0f * fbm(u * 6.0f, 0.2f, 8, 3, 84) + 5.0f * tri(x / 4.6f) * (fbm(u * 9.0f, 0.5f, 8, 2, 85) > 0.38f ? 1.0f : 0.25f);
      return smoothg(x) + 2.0f + 1.4f * fbm(x / 40.0f, 0.4f, 8, 3, 86);
    case BIOME_CAVES:
      if (l == 0) return 34.0f + 30.0f * ridge(x, 1.0f / 120.0f, 90);
      if (l == 1) return 28.0f + 22.0f * ridge(x, 1.0f / 70.0f, 91);
      if (l == 2) return 24.0f + 16.0f * ridge(x, 1.0f / 40.0f, 92);
      return smoothg(x) + 4.0f + 3.0f * ridge(x, 1.0f / 20.0f, 93);
    case BIOME_RUINS: {
      float fr = x / (l == 0 ? 9.0f : l == 1 ? 7.0f : l == 2 ? 11.0f : 3.0f);
      int cell = (int)floorf(fr);
      float h = hh(cell, 7, 120 + l);
      float within = fr - floorf(fr);
      if (l == 0) return 30.0f + 22.0f * h + (within > 0.3f && within < 0.7f ? 6.0f * h : 0.0f);                 /* towers */
      if (l == 1) return within < 0.34f ? 34.0f + 14.0f * h : 17.0f + 4.0f * h;                                   /* a colonnade */
      if (l == 2) return 22.0f + 12.0f * h * (within > 0.18f && within < 0.82f ? 1.0f : 0.5f);                    /* broken walls */
      return smoothg(x) + 2.5f + 3.0f * (h > 0.5f ? 1.0f : 0.0f) * (within < 0.7f ? 1.0f : 0.0f);               /* low wall */
    }
    case BIOME_FROST: {
      float fr = x / (l == 0 ? 11.0f : l == 1 ? 8.0f : l == 2 ? 13.0f : 4.0f);
      int cell = (int)floorf(fr);
      float h = hh(cell, 11, 150 + l), within = fr - floorf(fr);
      if (l == 0) return 32.0f + 30.0f * h * (within > 0.12f && within < 0.88f ? 1.0f : 0.2f);                 /* head office towers */
      if (l == 1) return 26.0f + 22.0f * h * (within > 0.2f && within < 0.8f ? 1.0f : 0.15f);
      if (l == 2) return 20.0f + 10.0f * ridge(x, 1.0f / 34.0f, 152);                                          /* snow banks */
      return smoothg(x) + 2.2f + 1.2f * fbm(x / 26.0f, 0.3f, 8, 3, 153);
    }
    default: {
      float cx = 0.5f * (g_bx0[BIOME_VOLCANO] + g_bx1[BIOME_VOLCANO]);
      if (l == 0) return 62.0f - 0.55f * fabsf(x - cx - 20.0f) + 4.0f * fbm(u * 12.0f, 0.3f, 8, 3, 140);       /* the cone */
      if (l == 1) return 20.0f + 36.0f * ridge(x, 1.0f / 90.0f, 141);
      if (l == 2) return 18.0f + 22.0f * ridge(x, 1.0f / 40.0f, 142) + 8.0f * tri(x / 7.0f);
      return smoothg(x) + 3.0f + 3.0f * ridge(x, 1.0f / 14.0f, 143);
    }
  }
}

static t3_mesh *strip(float z, float step, const float *topc, const float *botc, int b, int l, float glow_edge) {
  int n = (int)(((float)WORLD_W + 280.0f) / step) + 1;
  enum { R = 3 };
  float *pos = malloc(sizeof(float) * 3 * (size_t)n * R), *nor = malloc(sizeof(float) * 3 * (size_t)n * R), *col = malloc(sizeof(float) * 3 * (size_t)n * R);
  uint32_t *idx = malloc(sizeof(uint32_t) * 12 * (size_t)n);
  float fall = l == 3 ? 12.0f : l == 2 ? 20.0f : 30.0f;  /* how far under the top edge the colour reaches its dark end */
  for (int i = 0; i < n; i++) {
    float x = -140.0f + (float)i * step, m = bmask(b, x);
    float top = mixf(-45.0f, profile(b, l, x), m);
    float hv = fminf(1.0f, fmaxf(0.0f, (top - 20.0f) / 40.0f));
    float ys[R] = { top, top - fall, -50.0f };
    for (int k = 0; k < R; k++) {
      int v = i * R + k;
      pos[v * 3] = x, pos[v * 3 + 1] = fminf(ys[k], k ? ys[k] : ys[k]), pos[v * 3 + 2] = z;
      if (k && pos[v * 3 + 1] > top) pos[v * 3 + 1] = top;
      nor[v * 3] = 0, nor[v * 3 + 1] = 0, nor[v * 3 + 2] = 1;
      for (int c = 0; c < 3; c++) col[v * 3 + c] = k == 0 ? topc[c] * (0.9f + 0.25f * hv) : botc[c];
      if (glow_edge > 0 && k == 0) col[v * 3] += glow_edge * 0.6f, col[v * 3 + 1] += glow_edge * 0.12f;
    }
  }
  int k = 0;
  for (int i = 0; i + 1 < n; i++)
    for (int r = 0; r + 1 < R; r++) {
      uint32_t a = (uint32_t)(i * R + r), b2 = a + R, c = a + 1, d = b2 + 1;
      idx[k++] = a, idx[k++] = b2, idx[k++] = d, idx[k++] = a, idx[k++] = d, idx[k++] = c;
    }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, n * R, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, at), t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, nor, n * R, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, at), t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, col, n * R, 3);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, at), t3_release(at);
  at = t3_attribute_new(T3_UINT32, idx, k, 1);
  t3_geometry_set_index(g, at), t3_release(at);
  free(pos), free(nor), free(col), free(idx);
  t3_material *m = basic_vcol(false, false, 1);
  t3_mesh *me = add_mesh(g, m, 0, 0, 0, false);
  t3_release(g);
  return me;
}

/* a backdrop layer as lit 3D ground instead of a flat card: a height field LAYER_DEPTH[l] deep around the layer z,
 * highest along its middle row (the profile), falling to the front and back with noise, so the sun shapes its slopes and
 * the shadows give it form. The ruins keep their blocks (flat tops, sheer sides). Colour: the layer colour, eased
 * toward the horizon by distance (aerial perspective; the far layers are not washed out, only cooler and lighter), plus
 * snow on the high meadow peaks and darker gullies. */
static const float LAYER_DEPTH[4] = { 44, 30, 16, 9 };
/* each region's layer grid as drawn (vertex heights; x = x0 + i * step, z = zc + (-1 + 2 j / (nz - 1)) * dz / 2), kept
 * so scenery is seated on the very surface the camera sees */
typedef struct rgrid { float x0, step, zc, dz; int nx, nz; float *y; } rgrid;
static rgrid g_rg[BIOMES][4];
static float rg_vy(const rgrid *g, int i, int j) { return g->y[j * g->nx + i]; }
/* the height of one region's layer at (x, z), on the two triangles of its cell as the index buffer splits it */
static bool rg_height(const rgrid *g, float x, float z, float *out) {
  if (!g->y) return false;
  float fi = (x - g->x0) / g->step, fj = ((z - g->zc) / (g->dz * 0.5f) + 1.0f) * 0.5f * (float)(g->nz - 1);
  if (fi < 0 || fj < 0 || fi > (float)(g->nx - 1) || fj > (float)(g->nz - 1)) return false;
  int i = (int)fi, j = (int)fj;
  if (i > g->nx - 2) i = g->nx - 2;
  if (j > g->nz - 2) j = g->nz - 2;
  float u = fi - (float)i, v = fj - (float)j;
  float a = rg_vy(g, i, j), b = rg_vy(g, i + 1, j), c = rg_vy(g, i, j + 1), d = rg_vy(g, i + 1, j + 1);
  *out = u + v <= 1.0f ? a + u * (b - a) + v * (c - a) : d + (1.0f - u) * (c - d) + (1.0f - v) * (b - d);
  return true;
}
/* the visible ground of backdrop layer l at (x, z): the highest of the regions' surfaces there (they overlap where
 * one region's ground sinks and the next one's rises) */
static bool layer_ground(int l, float x, float z, float *out) {
  bool any = false;
  float best = -1e9f;
  for (int b = 0; b < BIOMES; b++) {
    float h;
    if (rg_height(&g_rg[b][l], x, z, &h) && h > best) best = h, any = true;
  }
  *out = best;
  return any;
}
/* seat a backdrop prop of half extents (rx, rz) on layer l: the ground under its footprint (centre and a ring of
 * points) must exist everywhere and slope no more than max_slope; the prop stands at the lowest point so no side
 * of its base hangs in the air (the uphill side sinks in) */
enum { SEAT_RING = 64 };
static bool bd_seat(int l, float x, float z, float rx, float rz, float max_slope, float *y) {
  float lo, hi, h;
  if (!layer_ground(l, x, z, &h)) return false;
  lo = hi = h;
  float ring[SEAT_RING];
  for (int r = 0; r < 3; r++) /* the base's rim and rings at 2/3 and 1/3 of it */
    for (int k = 0; k < SEAT_RING; k++) {
      float a = 6.2831853f * (float)k / SEAT_RING, f = 1.0f - (float)r / 3.0f;
      if (!layer_ground(l, x + cosf(a) * rx * f, z + sinf(a) * rz * f, &h)) return false;
      if (!r) ring[k] = h;
      lo = fminf(lo, h), hi = fmaxf(hi, h);
    }
  /* gentle under every diameter, and no step or seam anywhere under the base */
  for (int k = 0; k < SEAT_RING / 2; k++) {
    float a = 6.2831853f * (float)k / SEAT_RING, span = 2.0f * sqrtf(cosf(a) * cosf(a) * rx * rx + sinf(a) * sinf(a) * rz * rz);
    if (fabsf(ring[k] - ring[k + SEAT_RING / 2]) > max_slope * span) return false;
  }
  if (hi - lo > max_slope * 2.0f * fmaxf(rx, rz)) return false;
  *y = lo;
  place_rec(l, x, lo, z, rx, rz);
  return true;
}
/* the first spot in the stretch [x, x + room) and across four depths around z0 (spread zs) where bd_seat succeeds */
static bool bd_find(float x, float room, float z0, float zs, float rx, float rz, float max_slope, float *ox, float *oy, float *oz) {
  static const float ZT[4] = { 0.0f, 0.35f, 0.7f, -0.35f };
  for (float dx = 0; dx < room; dx += 0.5f)
    for (int k = 0; k < 4; k++) {
      float z = z0 + ZT[k] * zs;
      if (bd_seat(3, x + dx, z, rx, rz, max_slope, oy)) {
        *ox = x + dx, *oz = z;
        return true;
      }
    }
  return false;
}
static const float RICH[BIOMES][4][3] = {
  { { 0.36f, 0.45f, 0.62f }, { 0.22f, 0.4f, 0.33f }, { 0.13f, 0.34f, 0.15f }, { 0.2f, 0.42f, 0.13f } },
  { { 0.1f, 0.12f, 0.22f }, { 0.08f, 0.1f, 0.18f }, { 0.07f, 0.08f, 0.15f }, { 0.08f, 0.09f, 0.14f } },
  { { 0.4f, 0.48f, 0.45f }, { 0.4f, 0.4f, 0.33f }, { 0.36f, 0.33f, 0.26f }, { 0.24f, 0.3f, 0.15f } },
  { { 0.5f, 0.6f, 0.74f }, { 0.44f, 0.56f, 0.72f }, { 0.38f, 0.48f, 0.64f }, { 0.34f, 0.42f, 0.58f } },
  { { 0.28f, 0.1f, 0.08f }, { 0.24f, 0.09f, 0.07f }, { 0.26f, 0.11f, 0.08f }, { 0.3f, 0.13f, 0.09f } },
};
static const float AERIAL[4] = { 0.42f, 0.24f, 0.1f, 0.0f };
static t3_material *g_ridge_m;
static void ridge3d(int b, int l) {
  float x0 = fmaxf(-140.0f, g_bx0[b] - 70.0f), x1 = fminf((float)WORLD_W + 140.0f, g_bx1[b] + 70.0f);
  if (b == 0) x0 = -140.0f;
  if (b == g_blast) x1 = (float)WORLD_W + 140.0f;
  float step = l == 3 ? 0.5f : l == 2 ? 1.0f : l == 1 ? 2.0f : 3.0f;
  int nx = (int)((x1 - x0) / step) + 1, nz = l == 3 ? 7 : l == 2 ? 9 : 11;
  bool blocks = b == BIOME_RUINS && l < 3;
  float zc = LAYER_Z[l], dz = LAYER_DEPTH[l];
  float *pos = malloc(sizeof(float) * 3 * (size_t)(nx * nz)), *nor = malloc(sizeof(float) * 3 * (size_t)(nx * nz));
  float *col = malloc(sizeof(float) * 3 * (size_t)(nx * nz)), *uv = malloc(sizeof(float) * 2 * (size_t)(nx * nz));
  uint32_t *idx = malloc(sizeof(uint32_t) * 6 * (size_t)((nx - 1) * (nz - 1)));
  float hor[3];
  for (int c = 0; c < 3; c++) hor[c] = SKY_HOR[b][c];
  for (int i = 0; i < nx; i++) {
    float x = x0 + (float)i * step, m = bmask(b, x);
    float top = mixf(-45.0f, profile(b, l, x), m);
    float floor_y = l == 3 ? top - 7.0f : -30.0f;
    for (int j = 0; j < nz; j++) {
      float zr = -1.0f + 2.0f * (float)j / (float)(nz - 1); /* -1 the back row, +1 the front */
      float n = wn(x, zr * dz, 0.08f, 500 + l * 7 + b), n2 = wn(x, zr * dz, 0.35f, 520 + l);
      float sh;
      if (blocks) sh = fabsf(zr) < 0.55f ? 1.0f : 0.0f;
      else {
        float a = fmaxf(0.0f, 1.0f - zr * zr);
        sh = powf(a, 0.55f) * (0.78f + 0.3f * n) + 0.05f * (n2 - 0.5f);
      }
      if (j == 0 || j == nz - 1) sh = 0;
      float y = floor_y + (top - floor_y) * sh;
      int v = j * nx + i;
      pos[v * 3] = x, pos[v * 3 + 1] = y, pos[v * 3 + 2] = zc + zr * dz * 0.5f;
      uv[v * 2] = x / 5.0f, uv[v * 2 + 1] = (y - 0.5f * (zc + zr * dz * 0.5f)) / 5.0f;
      float c3[3];
      const float *rc = RICH[b][l];
      float gully = 0.75f + 0.35f * n2;
      for (int c = 0; c < 3; c++) c3[c] = rc[c] * gully;
      if (b == BIOME_MEADOW && l == 0) { /* snow on the high peaks */
        float snow = sstep(52.0f, 57.0f, y + 4.0f * (n2 - 0.5f));
        for (int c = 0; c < 3; c++) c3[c] = mixf(c3[c], 0.92f, snow);
      }
      if (b == BIOME_VOLCANO && l < 3) c3[0] += 0.35f * sstep(0.62f, 0.9f, n2) * (1.0f - sh); /* glowing scree low down */
      for (int c = 0; c < 3; c++) col[v * 3 + c] = mixf(c3[c], hor[c], AERIAL[l]);
    }
  }
  /* normals from the grid */
  for (int j = 0; j < nz; j++)
    for (int i = 0; i < nx; i++) {
      int v = j * nx + i, il = i > 0 ? v - 1 : v, ir = i < nx - 1 ? v + 1 : v, jb = j > 0 ? v - nx : v, jf = j < nz - 1 ? v + nx : v;
      float ax = pos[ir * 3] - pos[il * 3], ay = pos[ir * 3 + 1] - pos[il * 3 + 1], az = pos[ir * 3 + 2] - pos[il * 3 + 2];
      float bx = pos[jf * 3] - pos[jb * 3], by = pos[jf * 3 + 1] - pos[jb * 3 + 1], bz = pos[jf * 3 + 2] - pos[jb * 3 + 2];
      float cx = by * az - bz * ay, cy = bz * ax - bx * az, cz = bx * ay - by * ax, ln = sqrtf(cx * cx + cy * cy + cz * cz) + 1e-6f;
      if (cy < 0) cx = -cx, cy = -cy, cz = -cz;
      nor[v * 3] = cx / ln, nor[v * 3 + 1] = cy / ln, nor[v * 3 + 2] = cz / ln;
    }
  int k = 0;
  for (int j = 0; j + 1 < nz; j++)
    for (int i = 0; i + 1 < nx; i++) {
      uint32_t a = (uint32_t)(j * nx + i), b2 = a + 1, c = a + (uint32_t)nx, d = c + 1;
      idx[k++] = a, idx[k++] = c, idx[k++] = b2, idx[k++] = b2, idx[k++] = c, idx[k++] = d;
    }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, nx * nz, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, at), t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, nor, nx * nz, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, at), t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, col, nx * nz, 3);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, at), t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, uv, nx * nz, 2);
  t3_geometry_set_attribute(g, T3_ATTR_UV, at), t3_release(at);
  at = t3_attribute_new(T3_UINT32, idx, k, 1);
  t3_geometry_set_index(g, at), t3_release(at);
  {
    rgrid *rg = &g_rg[b][l];
    free(rg->y);
    *rg = (rgrid){ x0, step, zc, dz, nx, nz, malloc(sizeof(float) * (size_t)(nx * nz)) };
    for (int v = 0; v < nx * nz; v++) rg->y[v] = pos[v * 3 + 1];
  }
  free(pos), free(nor), free(col), free(uv), free(idx);
  /* the near layers wear the terrain textures (grass in the meadow, stone elsewhere); the far ones a plain matte */
  t3_material *m = l >= 2 ? (b == BIOME_MEADOW && l == 3 ? g_mat[MT_GRASS] : b == BIOME_CAVES ? g_mat[MT_CAVE] : g_mat[MT_ROCK]) : g_ridge_m;
  add_mesh(g, m, 0, 0, 0, false);
  t3_release(g);
}

static t3_texture *tex_alpha(int w, int h, float (*fn)(float u, float v)) {
  uint8_t *px = malloc((size_t)w * h * 4);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      float a = fn(((float)x + 0.5f) / (float)w, ((float)y + 0.5f) / (float)h);
      int i = (y * w + x) * 4;
      px[i] = px[i + 1] = px[i + 2] = 255, px[i + 3] = b8(a);
    }
  t3_texture *t = t3_texture_new(w, h, px);
  free(px);
  t->flip_y = false;
  t->generate_mipmaps = false;
  t->min_filter = t->mag_filter = T3_LINEAR;
  t->wrap_s = t->wrap_t = T3_CLAMP_TO_EDGE;
  t->color_space = T3_SRGB_COLOR_SPACE;
  return t;
}
static float radial_fn(float u, float v) {
  float d = sqrtf((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2.0f;
  float core = 1.0f - sstep(0.0f, 0.18f, d), halo = (1.0f - sstep(0.0f, 1.0f, d));
  return fminf(1.0f, core + halo * halo * 0.6f);
}
static float cloud_fn(float u, float v) {
  float e = (1.0f - sstep(0.25f, 1.0f, fabsf(u - 0.5f) * 2.0f)) * (1.0f - sstep(0.1f, 1.0f, fabsf(v - 0.5f) * 2.0f));
  float n = fbm(u, v, 5, 5, 150);
  return sstep(0.28f, 0.75f, n * e * 1.7f) * 0.9f;
}
static t3_mesh *sprite_quad(t3_texture *tex, float x, float y, float z, float w, float h, float r, float gc, float b2, bool additive, float opacity) {
  t3_geometry *g = t3_plane_geometry_new(w, h, 1, 1);
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->color = (t3_color){ r, gc, b2 };
  m->fog = false;
  m->transparent = true;
  m->depth_write = false;
  m->opacity = opacity;
  if (additive) m->blending = 2;
  t3_material_set_map(m, tex);
  t3_mesh *me = add_mesh(g, m, x, y, z, false);
  t3_release(g);
  return me;
}

void world_backdrop(game *g) {
  const level *L = &g->L;
  /* region extents and the ground envelope */
  /* region extents: the first run of columns of each region (a stray column of the default region inside the arena's
   * wall must not stretch the meadow) */
  for (int b = 0; b < BIOMES; b++) g_bx0[b] = -1.0f, g_bx1[b] = -1.0f;
  for (int x = 0; x < WORLD_W;) {
    int b = L->biome[x], x1 = x;
    while (x1 < WORLD_W && L->biome[x1] == b) x1++;
    if (g_bx0[b] < 0) g_bx0[b] = (float)x, g_bx1[b] = (float)x1;
    x = x1;
  }
  g_bfirst = -1, g_blast = 0;
  for (int b = 0; b < BIOMES; b++) {
    if (g_bx0[b] < 0) { g_bx0[b] = g_bx1[b] = (float)WORLD_W; continue; }
    if (g_bfirst < 0 || g_bx0[b] < g_bx0[g_bfirst]) g_bfirst = b;
    if (g_bx1[b] >= g_bx1[g_blast] || g_bx1[g_blast] >= (float)WORLD_W) g_blast = b;
  }
  for (int x = 0; x <= WORLD_W; x++) {
    float m = 0;
    for (int k = -7; k <= 7; k++) {
      int xx = x + k < 0 ? 0 : x + k >= WORLD_W ? WORLD_W - 1 : x + k;
      float gr = (float)L->ground[xx];
      if (gr < 40 && gr > m) m = gr;
    }
    g_gs[x] = m;
  }
  /* sky: a grid of colour bands, the colour of the region under each column */
  {
    enum { NX = 40, NY = 6 };
    float *pos = malloc(sizeof(float) * 3 * NX * NY), *nor = malloc(sizeof(float) * 3 * NX * NY), *col = malloc(sizeof(float) * 3 * NX * NY);
    uint32_t *idx = malloc(sizeof(uint32_t) * 6 * (NX - 1) * (NY - 1));
    static const float YS[NY] = { -60, 8, 28, 46, 78, 170 };
    for (int i = 0; i < NX; i++) {
      float x = -280.0f + (float)i * ((float)WORLD_W + 560.0f) / (float)(NX - 1);
      float w[BIOMES], tw = 0;
      for (int b = 0; b < BIOMES; b++) w[b] = bmask(b, x) + 0.001f, tw += w[b];
      float hor[3] = { 0 }, mid[3] = { 0 }, top[3] = { 0 };
      for (int b = 0; b < BIOMES; b++)
        for (int c = 0; c < 3; c++) hor[c] += SKY_HOR[b][c] * w[b] / tw, mid[c] += SKY_MID[b][c] * w[b] / tw, top[c] += SKY_TOP[b][c] * w[b] / tw;
      for (int j = 0; j < NY; j++) {
        int v = j * NX + i;
        pos[v * 3] = x, pos[v * 3 + 1] = YS[j], pos[v * 3 + 2] = -170;
        nor[v * 3] = 0, nor[v * 3 + 1] = 0, nor[v * 3 + 2] = 1;
        float t = (float)j / (NY - 1);
        const float *a = j <= 1 ? hor : j == 2 ? hor : j == 3 ? mid : j == 4 ? mid : top;
        const float *c2 = j <= 1 ? hor : j == 2 ? mid : j == 3 ? mid : j == 4 ? top : top;
        (void)t;
        float k = j == 2 ? 0.55f : j == 4 ? 0.6f : 0;
        for (int c = 0; c < 3; c++) col[v * 3 + c] = mixf(a[c], c2[c], k);
      }
    }
    int k = 0;
    for (int j = 0; j + 1 < NY; j++)
      for (int i = 0; i + 1 < NX; i++) {
        uint32_t a = (uint32_t)(j * NX + i), b2 = a + 1, c = a + NX, d = c + 1;
        idx[k++] = a, idx[k++] = b2, idx[k++] = d, idx[k++] = a, idx[k++] = d, idx[k++] = c;
      }
    t3_geometry *gs = t3_geometry_new();
    t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, NX * NY, 3);
    t3_geometry_set_attribute(gs, T3_ATTR_POSITION, at), t3_release(at);
    at = t3_attribute_new(T3_FLOAT32, nor, NX * NY, 3);
    t3_geometry_set_attribute(gs, T3_ATTR_NORMAL, at), t3_release(at);
    at = t3_attribute_new(T3_FLOAT32, col, NX * NY, 3);
    t3_geometry_set_attribute(gs, T3_ATTR_COLOR, at), t3_release(at);
    at = t3_attribute_new(T3_UINT32, idx, k, 1);
    t3_geometry_set_index(gs, at), t3_release(at);
    free(pos), free(nor), free(col), free(idx);
    add_mesh(gs, basic_vcol(false, false, 1), 0, 0, 0, false), t3_release(gs);
  }
  /* the sun over the meadow, a pale moon-glow over the caves' far end, an ash-red sun behind the volcano */
  t3_texture *rad = tex_alpha(128, 128, radial_fn);
  g_sun = sprite_quad(rad, 0.5f * (g_bx0[0] + g_bx1[0]) + 26.0f, 56.0f, -160.0f, 90, 90, 3.2f, 2.6f, 1.6f, true, 1.0f);
  sprite_quad(rad, 0.5f * (g_bx0[BIOME_VOLCANO] + g_bx1[BIOME_VOLCANO]) - 30.0f, 38.0f, -158.0f, 70, 70, 3.0f, 0.9f, 0.3f, true, 0.8f);
  sprite_quad(rad, 0.5f * (g_bx0[2] + g_bx1[2]) + 10.0f, 52.0f, -158.0f, 80, 80, 1.6f, 2.4f, 2.2f, true, 0.5f);
  t3_release(rad);
  /* clouds over the meadow and the ruins */
  t3_texture *cl = tex_alpha(128, 64, cloud_fn);
  g_ncloud = 0;
  for (int i = 0; i < 12; i++) {
    float x = -60.0f + (float)i * ((g_bx1[0] + 140.0f) / 8.0f) + 20.0f * hh(i, 1, 160);
    if (i >= 8) x = g_bx0[2] + (float)(i - 8) * 24.0f;
    bool ruin = i >= 8;
    float z = -100.0f - 20.0f * hh(i, 2, 160);
    g_clouds[g_ncloud] = sprite_quad(cl, x, 38.0f + 22.0f * hh(i, 3, 160), z, 70.0f + 40.0f * hh(i, 4, 160), 28.0f + 10.0f * hh(i, 5, 160), ruin ? 0.8f : 0.95f, ruin ? 0.92f : 0.95f, ruin ? 0.88f : 0.98f, false, ruin ? 0.45f : 0.7f);
    g_cloud_x[g_ncloud] = x;
    g_ncloud++;
  }
  t3_release(cl);
  /* the scenery layers: for each region, far to near, lit 3D ground (ridge3d) */
  if (!g_ridge_m) {
    g_ridge_m = t3_mesh_standard_material_new(0xffffff);
    g_ridge_m->vertex_colors = true, g_ridge_m->roughness = 0.95f, g_ridge_m->metalness = 0.0f;
  }
  for (int b = 0; b < BIOMES; b++)
    for (int l = 0; l < 4; l++) ridge3d(b, l);
  (void)strip;
  /* a hot glow at the foot of the volcano range */
  {
    enum { N = 60 };
    float *pos = malloc(sizeof(float) * 3 * N * 2), *nor = malloc(sizeof(float) * 3 * N * 2), *col = malloc(sizeof(float) * 3 * N * 2);
    uint32_t *idx = malloc(sizeof(uint32_t) * 6 * N);
    for (int i = 0; i < N; i++) {
      float x = g_bx0[BIOME_VOLCANO] - 60.0f + (float)i * (g_bx1[BIOME_VOLCANO] - g_bx0[BIOME_VOLCANO] + 140.0f) / (float)(N - 1);
      float m = bmask(BIOME_VOLCANO, x);
      for (int k = 0; k < 2; k++) {
        int v = i * 2 + k;
        pos[v * 3] = x, pos[v * 3 + 1] = k ? 26.0f : -4.0f, pos[v * 3 + 2] = -60.0f;
        nor[v * 3] = 0, nor[v * 3 + 1] = 0, nor[v * 3 + 2] = 1;
        float s = k ? 0.0f : 1.0f;
        col[v * 3] = 1.7f * s * m, col[v * 3 + 1] = 0.45f * s * m, col[v * 3 + 2] = 0.06f * s * m;
      }
    }
    for (int i = 0; i + 1 < N; i++) {
      uint32_t a = (uint32_t)(i * 2), q[6] = { a, a + 2, a + 3, a, a + 3, a + 1 };
      memcpy(&idx[i * 6], q, sizeof q);
    }
    t3_geometry *gg = t3_geometry_new();
    t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, N * 2, 3);
    t3_geometry_set_attribute(gg, T3_ATTR_POSITION, at), t3_release(at);
    at = t3_attribute_new(T3_FLOAT32, nor, N * 2, 3);
    t3_geometry_set_attribute(gg, T3_ATTR_NORMAL, at), t3_release(at);
    at = t3_attribute_new(T3_FLOAT32, col, N * 2, 3);
    t3_geometry_set_attribute(gg, T3_ATTR_COLOR, at), t3_release(at);
    at = t3_attribute_new(T3_UINT32, idx, 6 * (N - 1), 1);
    t3_geometry_set_index(gg, at), t3_release(at);
    free(pos), free(nor), free(col), free(idx);
    add_mesh(gg, basic_vcol(true, false, 1), 0, 0, 0, false), t3_release(gg);
  }
  /* the near scenery that is lit and shadowed: trees behind the meadow, colonnades behind the ruins, spires before the
   * volcano, crystals in the caves. Each is seated on layer 3's ground (bd_seat) somewhere in its stretch [x, x + room):
   * the first spot, along x and then across a few depths, whose ground is there under the whole base and gentle
   * enough; a stretch with none (a cliff, a step, the edge of the layer) stays empty. */
  {
    mb back = { 0 }, backglow = { 0 };
    bool float_bug = cw_test_env("COOP_FLOAT_BUG") != NULL; /* the scenery check's must-fail control (from frame 2) */
    float px, py, pz;
#define FIND(X, ROOM, Z0, ZS, RX, RZ, SLOPE) bd_find((float)(X), (float)(ROOM), (Z0), (ZS), (RX), (RZ), (SLOPE), &px, &py, &pz)
    for (int x = 4; x < (int)g_bx1[0] + 10 && x < WORLD_W; x += 4 + (int)(hh(x, 1, 170) * 4)) {
      if (bmask(0, (float)x) < 0.5f) continue;
      float s = 0.9f + 0.6f * hh(x, 3, 170);
      if (!FIND(x, 4, -8.0f - 3.0f * hh(x, 2, 170), 3.0f, 0.34f * s, 0.34f * s, 0.6f)) continue;
      if (float_bug) py += 0.5f, g_placed[g_nplaced - 1].y = py, float_bug = false;
      tree(&back, px, py, pz, x, s);
    }
    for (int x = (int)g_bx0[2]; x < (int)g_bx1[2]; x += 7 + (int)(hh(x, 1, 171) * 5)) {
      bool broken = hh(x, 4, 171) > 0.45f;
      if (!FIND(x, 7, -8.0f - 2.0f * hh(x, 2, 171), 2.5f, 0.6f, 0.6f, 0.35f)) continue;
      float ry = NAN, cy = py;
      if (broken && bd_seat(3, px + 0.8f, pz + 0.3f, 0.5f, 0.45f, 0.6f, &ry) && fabsf(ry - cy) > 0.6f) ry = NAN, g_nplaced--; /* rubble only beside its own column */
      column_prop(&back, px, cy, pz, 3.5f + 4.0f * hh(x, 3, 171), x, broken, ry);
    }
    for (int x = (int)g_bx0[BIOME_VOLCANO]; x < (int)g_bx1[BIOME_VOLCANO] - 20; x += 6 + (int)(hh(x, 1, 172) * 4)) {
      float s = 1.6f + hh(x, 3, 172);
      /* a spire of rock juts from the scree: steeper ground is fine, its uphill side sinks in */
      if (FIND(x, 6, -7.5f - 2.5f * hh(x, 2, 172), 2.5f, 0.34f * s, 0.34f * s + 0.05f, 0.8f)) lava_spire(&back, &backglow, px, py, pz, x, s);
    }
    for (int x = (int)g_bx0[1]; x < (int)g_bx1[1]; x += 5 + (int)(hh(x, 1, 173) * 4)) {
      float s = 1.4f + hh(x, 3, 173);
      if (FIND(x, 5, -8.0f - 2.0f * hh(x, 2, 173), 2.5f, 0.5f * s, 0.25f + 0.2f * s, 0.6f)) crystal_cluster(&backglow, &back, px, py, pz, x, s, x / 25, false);
    }
#undef FIND
    t3_geometry *gb = mb_done(&back);
    if (gb) add_mesh(gb, g_mat[MT_PROP], 0, 0, 0, true), t3_release(gb);
    gb = mb_done(&backglow);
    if (gb) add_mesh(gb, g_mat[MT_GLOW], 0, 0, 0, false), t3_release(gb);
  }
  /* the back wall of the caves: rock, lit by the torches */
  {
    mb wall = { 0 };
    for (int b = 1; b <= 1; b++) {
      float x0 = g_bx0[b] - 2, x1 = g_bx1[b] + 2;
      float c[4][3], p[4][3] = { { x0, 0, -2.6f }, { x1, 0, -2.6f }, { x1, (float)WORLD_H, -2.6f }, { x0, (float)WORLD_H, -2.6f } };
      float n[3] = { 0, 0, 1 };
      float uv[4][2] = { { x0 / 5, 0 }, { x1 / 5, 0 }, { x1 / 5, (float)WORLD_H / 5 }, { x0 / 5, (float)WORLD_H / 5 } };
      for (int i = 0; i < 4; i++) shade(c[i], STONE[1], 0.32f);
      mb_quad(&wall, p, n, c, uv);
    }
    t3_geometry *gw = mb_done(&wall);
    if (gw) add_mesh(gw, g_mat[MT_ROCK], 0, 0, 0, false), t3_release(gw);
  }
}

/* drift the clouds and let the sun breathe */
void world_update_backdrop(float time) {
  for (int i = 0; i < g_ncloud; i++)
    if (g_clouds[i]) g_clouds[i]->base.position.x = g_cloud_x[i] + 3.0f * time * (0.4f + 0.1f * (float)(i % 4));
  if (g_sun) g_sun->base.scale = (t3_vec3){ 1.0f + 0.02f * sinf(time * 0.7f), 1.0f + 0.02f * sinf(time * 0.7f), 1 };
}

/* ── the scenery check: every seated prop measured against the ground as drawn. Backdrop layers: a ray straight down
 * through the triangles of every region's layer mesh (highest hit wins); tiles: the first open tile top at or under
 * the base. A footprint point with no ground under it, or a base more than SCENERY_TOL above or below the lowest
 * ground under the footprint, counts as one bad prop. ── */
#define SCENERY_TOL 0.03f
static bool ray_layer(int l, float x, float z, float *out) {
  bool any = false;
  float best = -1e9f;
  for (int b = 0; b < BIOMES; b++) {
    const rgrid *g = &g_rg[b][l];
    if (!g->y) continue;
    int ic = (int)floorf((x - g->x0) / g->step);
    for (int i = ic - 1; i <= ic + 1; i++) {
      if (i < 0 || i + 1 >= g->nx) continue;
      for (int j = 0; j + 1 < g->nz; j++) {
        float X[4], Z[4], Y[4]; /* a, b (i + 1), c (j + 1), d */
        for (int q = 0; q < 4; q++) {
          int ii = i + (q & 1), jj = j + (q >> 1);
          X[q] = g->x0 + (float)ii * g->step;
          Z[q] = g->zc + (-1.0f + 2.0f * (float)jj / (float)(g->nz - 1)) * g->dz * 0.5f;
          Y[q] = g->y[jj * g->nx + ii];
        }
        static const int TRI[2][3] = { { 0, 2, 1 }, { 1, 2, 3 } };
        for (int t = 0; t < 2; t++) {
          int p0 = TRI[t][0], p1 = TRI[t][1], p2 = TRI[t][2];
          float d = (Z[p1] - Z[p2]) * (X[p0] - X[p2]) + (X[p2] - X[p1]) * (Z[p0] - Z[p2]);
          if (fabsf(d) < 1e-9f) continue;
          float w0 = ((Z[p1] - Z[p2]) * (x - X[p2]) + (X[p2] - X[p1]) * (z - Z[p2])) / d;
          float w1 = ((Z[p2] - Z[p0]) * (x - X[p2]) + (X[p0] - X[p2]) * (z - Z[p2])) / d;
          float w2 = 1.0f - w0 - w1;
          if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;
          float h = w0 * Y[p0] + w1 * Y[p1] + w2 * Y[p2];
          if (h > best) best = h, any = true;
        }
      }
    }
  }
  *out = best;
  return any;
}
static bool ray_tiles(const level *L, float x, float z, float y, float *out) {
  if (fabsf(z) > DEPTH_FRONT) return false;
  int c = (int)floorf(x);
  if (c < 0 || c >= WORLD_W) return false;
  for (int ty = (int)floorf(y + 0.5f) - 1; ty >= 0 && ty >= (int)y - 3; ty--)
    if (world_solid(L, c, ty) && !world_solid(L, c, ty + 1)) {
      *out = (float)(ty + 1);
      return true;
    }
  return false;
}
int world_scenery_check(const level *L, int *checked, float *worst_mm) {
  int bad = 0;
  float worst = 0;
  for (int n = 0; n < g_nplaced; n++) {
    const placed *p = &g_placed[n];
    float lo = 1e9f;
    bool miss = false;
    for (int k = -1; k < 64 * 3 && !miss; k++) { /* the centre, then 64 points on the rim and on rings at 2/3 and 1/3 of it */
      float a = 6.2831853f * (float)(k % 64) / 64.0f, f = 1.0f - (float)(k / 64) / 3.0f, px = k < 0 ? p->x : p->x + cosf(a) * p->rx * f, pz = k < 0 ? p->z : p->z + sinf(a) * p->rz * f, h;
      bool hit = p->layer < 0 ? ray_tiles(L, px, pz, p->y, &h) : ray_layer(p->layer, px, pz, &h);
      if (!hit) miss = true;
      else lo = fminf(lo, h);
    }
    float off = miss ? 1e3f : fabsf(p->y - lo);
    if (off > worst) worst = off;
    if (off > SCENERY_TOL) {
      bad++;
      if (getenv("COOP_SCENERY")) printf("scenery: off the ground layer %d at %.2f,%.2f,%.2f by %.3f%s\n", p->layer, p->x, p->y, p->z, miss ? 0.0 : (double)(p->y - lo), miss ? " (no ground under the footprint)" : "");
    }
  }
  *checked = g_nplaced;
  *worst_mm = worst * 1000.0f;
  return bad;
}
