/* The level's body and picture: Box2D colliders from the tile grid, and the three.c scene (chunk meshes with
 * generated PBR textures, water, lava, spikes, backdrops). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/math.h"
#include "cartwheel/render3d.h"
#include "game.h"

#define DEPTH_FRONT 1.5f
#define DEPTH_BACK -1.5f
#define CHUNK 16

/* ── physics ──────────────────────────────────────────────────────── */

static cw_body2d g_static;
static struct { int x, y; cw_body2d b; } g_crack[64];
int world_cracks = 0;

static bool is_solid_t(int t) { return t == T_ROCK || t == T_GRASS || t == T_BRICK; }

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
      if (L->t[y][x] == T_CRACK && world_cracks < 64) {
        cw_body2d_def cd = cw_body2d_default_def();
        cd.type = CW_BODY_STATIC;
        cd.position = (cw_pos2){ (double)x + 0.5, (double)y + 0.5 };
        cw_body2d b = cw_body2d_create(g->world, &cd);
        cw_shape2d_box(b, &sd, (cw_vec2){ 0.5f, 0.5f }, (cw_xform2){ { 0, 0 }, { 1, 0 } });
        g_crack[world_cracks].x = x, g_crack[world_cracks].y = y, g_crack[world_cracks].b = b;
        world_cracks++;
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

/* ── textures: tileable value noise, a height field turned into albedo, normal, roughness and occlusion ── */

#define TN 128
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
typedef struct texel { float r, g, b, h, rough, ao; } texel;

static void gen_rock(float u, float v, texel *t) {
  float strata = vnoise(u * 0.5f, v * 6.0f + fbm(u, v, 4, 3, 5) * 1.5f, 2, 9);
  float n = fbm(u, v, 6, 5, 1), fine = fbm(u, v, 32, 3, 3);
  float crack = fabsf(fbm(u, v, 5, 4, 41) - 0.5f);
  float c = crack < 0.018f ? 0.55f + crack * 25.0f : 1.0f;
  float k = 0.55f + 0.3f * n + 0.1f * strata + 0.08f * (fine - 0.5f);
  k *= 0.55f + 0.45f * c;
  t->r = k * 1.0f, t->g = k * 0.95f, t->b = k * 0.9f;
  t->h = 0.5f * n + 0.35f * fine + 0.2f * strata - (1 - c) * 0.5f;
  t->rough = 0.78f + 0.2f * fine;
  t->ao = 0.6f + 0.4f * c * (0.6f + 0.4f * n);
}
static void gen_brick(float u, float v, texel *t) {
  float rows = 6, cols = 3;
  float y = v * rows;
  int row = (int)floorf(y);
  float x = u * cols + (float)(row & 1) * 0.5f;
  float fx = x - floorf(x), fy = y - floorf(y);
  float e = fminf(fminf(fx, 1 - fx) * 2.0f, fminf(fy, 1 - fy) * 4.0f);
  float edge = e < 0.06f ? 0.0f : (e < 0.18f ? (e - 0.06f) / 0.12f : 1.0f);
  int id = ((int)floorf(x) * 31 + row * 17) & 255;
  float n = fbm(u, v, 8, 4, 22), fine = fbm(u, v, 32, 3, 7);
  float tint = (float)id / 255.0f;
  float c = (0.62f + 0.14f * tint + 0.16f * (n - 0.5f)) * (0.7f + 0.3f * edge);
  t->r = c, t->g = c * 0.9f, t->b = c * 0.78f;
  t->h = edge * (0.75f + 0.25f * fine);
  t->rough = 0.85f - 0.1f * edge;
  t->ao = 0.5f + 0.5f * edge;
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

static t3_material *pbr(void (*gen)(float, float, texel *), float nstrength, float rough_scale) {
  texel *tx = malloc(sizeof *tx * TN * TN);
  uint8_t *a = malloc(TN * TN * 4), *nm = malloc(TN * TN * 4), *ro = malloc(TN * TN * 4), *ao = malloc(TN * TN * 4);
  for (int y = 0; y < TN; y++)
    for (int x = 0; x < TN; x++) gen(((float)x + 0.5f) / TN, ((float)y + 0.5f) / TN, &tx[y * TN + x]);
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
    }
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  t3_texture *ts[4] = { tex_finish(a, true), tex_finish(nm, false), tex_finish(ro, false), tex_finish(ao, false) };
  static const t3_map_slot SLOTS[4] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP, T3_AO_MAP };
  for (int i = 0; i < 4; i++) t3_material_set_texture(m, SLOTS[i], ts[i]), t3_release(ts[i]);
  m->normal_scale = (t3_vec2){ 1.0f, 1.0f };
  m->ao_map_intensity = 1.0f;
  m->roughness = 1.0f;
  m->metalness = 0.0f;
  m->vertex_colors = true;
  free(tx), free(a), free(nm), free(ro), free(ao);
  return m;
}

/* ── chunk meshes ───────────────────────────────────────────────── */

typedef struct mb { float *pos, *nor, *col, *uv; uint32_t *idx; int nv, ni, capv, capi; } mb;
static void mb_quad(mb *b, const float p[4][3], const float n[3], const float c[4][3], const float uv[4][2]) {
  if (b->nv + 4 > b->capv) {
    b->capv = b->capv ? b->capv * 2 : 4096;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)b->capv);
    b->nor = realloc(b->nor, sizeof(float) * 3 * (size_t)b->capv);
    b->col = realloc(b->col, sizeof(float) * 3 * (size_t)b->capv);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)b->capv);
  }
  if (b->ni + 6 > b->capi) {
    b->capi = b->capi ? b->capi * 2 : 6144;
    b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)b->capi);
  }
  for (int i = 0; i < 4; i++) {
    memcpy(&b->pos[(b->nv + i) * 3], p[i], 12), memcpy(&b->nor[(b->nv + i) * 3], n, 12);
    memcpy(&b->col[(b->nv + i) * 3], c[i], 12), memcpy(&b->uv[(b->nv + i) * 2], uv[i], 8);
  }
  uint32_t v = (uint32_t)b->nv;
  uint32_t q[6] = { v, v + 1, v + 2, v, v + 2, v + 3 };
  memcpy(&b->idx[b->ni], q, sizeof q);
  b->nv += 4, b->ni += 6;
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

/* biome tints (linear-ish rgb) for the stone and its top */
static const float STONE[BIOMES][3] = { { 0.78f, 0.62f, 0.5f }, { 0.55f, 0.62f, 0.78f }, { 0.95f, 0.82f, 0.62f }, { 0.42f, 0.3f, 0.3f } };
static const float TOPS[BIOMES][3] = { { 0.32f, 0.72f, 0.22f }, { 0.5f, 0.6f, 0.75f }, { 0.9f, 0.8f, 0.55f }, { 0.55f, 0.28f, 0.2f } };

static void tile_faces(const level *L, int x, int y, mb *b) {
  int t = L->t[y][x];
  int bi = L->biome[x];
  float fx = (float)x, fy = (float)y;
  const float *st = STONE[bi], *tp = TOPS[bi];
  bool grass_top = t == T_GRASS || (t == T_ROCK && y + 1 < WORLD_H && !world_solid(L, x, y + 1) && bi == BIOME_MEADOW);
  float dark = 0.82f + 0.18f * (float)((x * 7 + y * 13) & 7) / 7.0f;
  float cs[3] = { st[0] * dark, st[1] * dark, st[2] * dark };
  float ct[3] = { tp[0], tp[1], tp[2] };
  const float *topcol = (grass_top || bi == BIOME_VOLCANO) ? ct : cs;
  /* front face (toward the camera), z = DEPTH_FRONT: the top row of a grassy tile blends to grass */
  {
    float p[4][3] = { { fx, fy, DEPTH_FRONT }, { fx + 1, fy, DEPTH_FRONT }, { fx + 1, fy + 1, DEPTH_FRONT }, { fx, fy + 1, DEPTH_FRONT } };
    float n[3] = { 0, 0, 1 };
    bool open_above = !world_solid(L, x, y + 1);
    const float *top = (grass_top && open_above) ? ct : cs;
    float c[4][3] = { { cs[0], cs[1], cs[2] }, { cs[0], cs[1], cs[2] }, { top[0], top[1], top[2] }, { top[0], top[1], top[2] } };
    float uv[4][2] = { { fx / 4, fy / 4 }, { (fx + 1) / 4, fy / 4 }, { (fx + 1) / 4, (fy + 1) / 4 }, { fx / 4, (fy + 1) / 4 } };
    mb_quad(b, p, n, c, uv);
  }
  if (!world_solid(L, x, y + 1)) { /* top */
    float p[4][3] = { { fx, fy + 1, DEPTH_FRONT }, { fx + 1, fy + 1, DEPTH_FRONT }, { fx + 1, fy + 1, DEPTH_BACK }, { fx, fy + 1, DEPTH_BACK } };
    float n[3] = { 0, 1, 0 };
    float c[4][3];
    for (int i = 0; i < 4; i++) memcpy(c[i], topcol, 12);
    float uv[4][2] = { { fx / 4, 0.0f }, { (fx + 1) / 4, 0.0f }, { (fx + 1) / 4, 0.75f }, { fx / 4, 0.75f } };
    mb_quad(b, p, n, c, uv);
  }
  if (y > 0 && !world_solid(L, x, y - 1)) { /* underside */
    float p[4][3] = { { fx, fy, DEPTH_BACK }, { fx + 1, fy, DEPTH_BACK }, { fx + 1, fy, DEPTH_FRONT }, { fx, fy, DEPTH_FRONT } };
    float n[3] = { 0, -1, 0 };
    float c[4][3];
    for (int i = 0; i < 4; i++) memcpy(c[i], cs, 12);
    float uv[4][2] = { { fx / 4, 0 }, { (fx + 1) / 4, 0 }, { (fx + 1) / 4, 0.75f }, { fx / 4, 0.75f } };
    mb_quad(b, p, n, c, uv);
  }
  if (x > 0 && !world_solid(L, x - 1, y)) { /* left side */
    float p[4][3] = { { fx, fy, DEPTH_BACK }, { fx, fy, DEPTH_FRONT }, { fx, fy + 1, DEPTH_FRONT }, { fx, fy + 1, DEPTH_BACK } };
    float n[3] = { -1, 0, 0 };
    float c[4][3];
    for (int i = 0; i < 4; i++) memcpy(c[i], cs, 12);
    float uv[4][2] = { { 0, fy / 4 }, { 0.75f, fy / 4 }, { 0.75f, (fy + 1) / 4 }, { 0, (fy + 1) / 4 } };
    mb_quad(b, p, n, c, uv);
  }
  if (x + 1 < WORLD_W && !world_solid(L, x + 1, y)) { /* right side */
    float p[4][3] = { { fx + 1, fy, DEPTH_FRONT }, { fx + 1, fy, DEPTH_BACK }, { fx + 1, fy + 1, DEPTH_BACK }, { fx + 1, fy + 1, DEPTH_FRONT } };
    float n[3] = { 1, 0, 0 };
    float c[4][3];
    for (int i = 0; i < 4; i++) memcpy(c[i], cs, 12);
    float uv[4][2] = { { 0, fy / 4 }, { 0.75f, fy / 4 }, { 0.75f, (fy + 1) / 4 }, { 0, (fy + 1) / 4 } };
    mb_quad(b, p, n, c, uv);
  }
}

/* ── scene objects ──────────────────────────────────────────────── */

static cw_node g_root;
static t3_material *g_rock, *g_brickm, *g_water, *g_lava, *g_spike, *g_crack_m, *g_oneway_m, *g_back;
static t3_geometry *g_water_geo[16];
static float *g_water_pos[16];
static int g_water_n[16], g_water_cols[16], g_nwater;
static float g_water_x0[16], g_water_top[16];
static t3_mesh *g_crack_mesh[64];

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

void world_scene_free(void) {
  g_root = (cw_node){ 0 };
  g_nwater = 0;
}

void world_scene_build(game *g, cw_node root) {
  const level *L = &g->L;
  g_root = root;
  g_rock = pbr(gen_rock, 3.0f, 1.0f);
  g_brickm = pbr(gen_brick, 4.0f, 1.0f);
  /* chunks */
  for (int cx = 0; cx < WORLD_W; cx += CHUNK) {
    mb rock = { 0 }, brick = { 0 };
    for (int y = 0; y < WORLD_H; y++)
      for (int x = cx; x < cx + CHUNK && x < WORLD_W; x++) {
        int t = L->t[y][x];
        if (t == T_BRICK) tile_faces(L, x, y, &brick);
        else if (t == T_ROCK || t == T_GRASS) tile_faces(L, x, y, &rock);
      }
    t3_geometry *gr = mb_done(&rock), *gb = mb_done(&brick);
    if (gr) add_mesh(gr, g_rock, 0, 0, 0, true), t3_release(gr);
    if (gb) add_mesh(gb, g_brickm, 0, 0, 0, true), t3_release(gb);
  }
  /* cracked blocks, one mesh each (removed when broken) */
  g_crack_m = t3_mesh_physical_material_new(0xb89a7a);
  g_crack_m->roughness = 0.9f;
  int nc = 0;
  for (int y = 0; y < WORLD_H; y++)
    for (int x = 0; x < WORLD_W; x++)
      if (L->t[y][x] == T_CRACK && nc < 64) {
        t3_geometry *bg = t3_box_geometry_new(0.98f, 0.98f, 2.9f, 1, 1, 1);
        g_crack_mesh[nc++] = add_mesh(bg, g_crack_m, (float)x + 0.5f, (float)y + 0.5f, 0, true);
        t3_release(bg);
      }
  /* one-way ledges: wooden planks */
  g_oneway_m = t3_mesh_physical_material_new(0x8a5a32);
  g_oneway_m->roughness = 0.7f;
  for (int y = 0; y < WORLD_H; y++)
    for (int x = 0; x < WORLD_W; x++)
      if (L->t[y][x] == T_ONEWAY) {
        t3_geometry *bg = t3_box_geometry_new(1.0f, 0.26f, 2.2f, 1, 1, 1);
        add_mesh(bg, g_oneway_m, (float)x + 0.5f, (float)y + 0.88f, 0, true), t3_release(bg);
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
  /* lava: emissive slabs */
  g_lava = t3_mesh_physical_material_new(0x2a0a00);
  g_lava->emissive = (t3_color){ 1.0f, 0.28f, 0.04f };
  g_lava->emissive_intensity = 2.2f;
  g_lava->roughness = 0.6f;
  for (int x = 0; x < WORLD_W; x++)
    if (L->t[2][x] == T_LAVA) {
      int x1 = x;
      while (x1 < WORLD_W && L->t[2][x1] == T_LAVA) x1++;
      t3_geometry *bg = t3_box_geometry_new((float)(x1 - x), 2.6f, 3.0f, 1, 1, 1);
      add_mesh(bg, g_lava, (float)(x + x1) * 0.5f, 1.3f, 0, false), t3_release(bg);
      x = x1;
    }
  /* water: translucent with an animated surface */
  g_water = t3_mesh_physical_material_new(0x2a7fbf);
  g_water->transparent = true;
  g_water->opacity = 0.55f;
  g_water->roughness = 0.05f;
  g_water->depth_write = false;
  g_water->side = T3_DOUBLE_SIDE;
  g_nwater = 0;
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
    if (x1 == x) { continue; }
    int cols = x1 - x, segs = cols * 4;
    if (g_nwater >= 16) break;
    int k = g_nwater++;
    int low = ty;
    for (int xx = x; xx < x1; xx++)
      for (int y = 0; y < WORLD_H; y++)
        if (L->t[y][xx] == T_WATER && y < low) low = y;
    float bottom = (float)low, topy = (float)ty + 1.0f;
    /* a front sheet: 2 rows of (segs+1) vertices (bottom, surface) */
    int nv = (segs + 1) * 2;
    float *pos = malloc(sizeof(float) * 3 * (size_t)nv), *nor = malloc(sizeof(float) * 3 * (size_t)nv);
    uint32_t *idx = malloc(sizeof(uint32_t) * 6 * (size_t)segs);
    for (int i = 0; i <= segs; i++) {
      float px = (float)x + (float)i / 4.0f;
      pos[(i * 2) * 3 + 0] = px, pos[(i * 2) * 3 + 1] = bottom, pos[(i * 2) * 3 + 2] = 1.2f;
      pos[(i * 2 + 1) * 3 + 0] = px, pos[(i * 2 + 1) * 3 + 1] = topy, pos[(i * 2 + 1) * 3 + 2] = 1.2f;
      nor[(i * 2) * 3 + 0] = 0, nor[(i * 2) * 3 + 1] = 0, nor[(i * 2) * 3 + 2] = 1;
      nor[(i * 2 + 1) * 3 + 0] = 0, nor[(i * 2 + 1) * 3 + 1] = 0, nor[(i * 2 + 1) * 3 + 2] = 1;
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
    at = t3_attribute_new(T3_UINT32, idx, 6 * segs, 1);
    t3_geometry_set_index(wg, at), t3_release(at);
    free(nor), free(idx);
    add_mesh(wg, g_water, 0, 0, 0, false);
    g_water_geo[k] = wg;
    g_water_pos[k] = pos;
    g_water_n[k] = nv;
    g_water_cols[k] = segs;
    g_water_x0[k] = (float)x;
    g_water_top[k] = topy;
    x = x1 - 1;
  }
}

void world_update_water_mesh(float time) {
  for (int k = 0; k < g_nwater; k++) {
    float *pos = g_water_pos[k];
    for (int i = 0; i <= g_water_cols[k]; i++) {
      float px = g_water_x0[k] + (float)i / 4.0f;
      pos[(i * 2 + 1) * 3 + 1] = g_water_top[k] - 0.06f + 0.07f * sinf(px * 1.7f + time * 2.1f) + 0.04f * sinf(px * 3.3f - time * 1.3f);
    }
    t3_attribute *a = g_water_geo[k]->attributes[T3_ATTR_POSITION];
    memcpy(a->array, pos, sizeof(float) * 3 * (size_t)g_water_n[k]);
    a->version++;
  }
}

void world_hide_crack(int x, int y) {
  int nc = 0;
  for (int i = 0; i < 64 && g_crack_mesh[i]; i++, nc++) {
    t3_mesh *m = g_crack_mesh[i];
    if ((int)floorf(m->base.position.x) == x && (int)floorf(m->base.position.y) == y) m->base.visible = false;
  }
  (void)nc;
}

/* ── backdrop: a sky gradient that changes with the region and three layers of ridges at different depths
 * (the camera's perspective gives the parallax) ── */
static const float SKY_TOP[BIOMES][3] = { { 0.18f, 0.42f, 0.85f }, { 0.02f, 0.03f, 0.08f }, { 0.12f, 0.28f, 0.34f }, { 0.12f, 0.03f, 0.03f } };
static const float SKY_BOT[BIOMES][3] = { { 0.75f, 0.88f, 1.0f }, { 0.1f, 0.12f, 0.22f }, { 0.6f, 0.78f, 0.72f }, { 0.95f, 0.4f, 0.15f } };
static const float RIDGE[BIOMES][3] = { { 0.22f, 0.42f, 0.3f }, { 0.12f, 0.14f, 0.24f }, { 0.28f, 0.42f, 0.4f }, { 0.2f, 0.08f, 0.07f } };

static void strip_mesh(float z, float y0, float y1, const float (*top)[3], const float (*bot)[3], const float *xs, int n, bool ridge, int seed, float amp) {
  /* a strip of n columns: bottom edge at y0, top edge at y1 (+ ridge noise) */
  int nv = n * 2;
  float *pos = malloc(sizeof(float) * 3 * (size_t)nv), *nor = malloc(sizeof(float) * 3 * (size_t)nv), *col = malloc(sizeof(float) * 3 * (size_t)nv);
  uint32_t *idx = malloc(sizeof(uint32_t) * 6 * (size_t)n);
  for (int i = 0; i < n; i++) {
    float x = xs[i];
    float h = ridge ? amp * (0.35f + 0.65f * fbm(x / 140.0f, 0.37f * (float)seed, 8, 4, 70 + seed)) : 0.0f;
    float ys[2] = { y0, y1 + h };
    for (int k = 0; k < 2; k++) {
      int v = i * 2 + k;
      pos[v * 3] = x, pos[v * 3 + 1] = ys[k], pos[v * 3 + 2] = z;
      nor[v * 3] = 0, nor[v * 3 + 1] = 0, nor[v * 3 + 2] = 1;
      const float *c = k == 0 ? bot[i] : top[i];
      memcpy(&col[v * 3], c, 12);
    }
  }
  for (int i = 0; i + 1 < n; i++) {
    uint32_t a = (uint32_t)(i * 2), q[6] = { a, a + 2, a + 3, a, a + 3, a + 1 };
    memcpy(&idx[i * 6], q, sizeof q);
  }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, at), t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, nor, nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, at), t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, col, nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, at), t3_release(at);
  at = t3_attribute_new(T3_UINT32, idx, 6 * (n - 1), 1);
  t3_geometry_set_index(g, at), t3_release(at);
  free(pos), free(nor), free(col), free(idx);
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  m->side = T3_DOUBLE_SIDE;
  m->fog = ridge;
  add_mesh(g, m, 0, 0, 0, false);
  t3_release(g);
}

void world_backdrop(game *g) {
  (void)g;
  enum { N = 40 };
  float xs[N], top[N][3], bot[N][3], rc[N][3], rb[N][3];
  for (int i = 0; i < N; i++) {
    float x = -120.0f + (float)i * ((float)WORLD_W + 240.0f) / (float)(N - 1);
    xs[i] = x;
    /* blend between the regions: the biome centre of each quarter, linearly */
    float u = (x / (float)WORLD_W) * 4.0f - 0.5f;
    int b0 = (int)floorf(u < 0 ? 0 : u > 3 ? 3 : u), b1 = b0 + 1 > 3 ? 3 : b0 + 1;
    float f = u - (float)b0;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    f = f * f * (3 - 2 * f);
    for (int k = 0; k < 3; k++) {
      top[i][k] = SKY_TOP[b0][k] + (SKY_TOP[b1][k] - SKY_TOP[b0][k]) * f;
      bot[i][k] = SKY_BOT[b0][k] + (SKY_BOT[b1][k] - SKY_BOT[b0][k]) * f;
      rc[i][k] = RIDGE[b0][k] + (RIDGE[b1][k] - RIDGE[b0][k]) * f;
      rb[i][k] = rc[i][k] * 0.6f;
    }
  }
  strip_mesh(-140, -60, 150, (const float(*)[3])top, (const float(*)[3])bot, xs, N, false, 0, 0);
  /* three ridge layers, nearer ones darker and more saturated (fog lifts the far ones) */
  static const float ZS[3] = { -95, -60, -34 }, AMP[3] = { 70, 50, 34 }, SH[3] = { 1.25f, 1.0f, 0.78f };
  for (int l = 0; l < 3; l++) {
    float ct[N][3], cb[N][3];
    for (int i = 0; i < N; i++)
      for (int k = 0; k < 3; k++) ct[i][k] = rc[i][k] * SH[l], cb[i][k] = rb[i][k] * SH[l];
    strip_mesh(ZS[l], -30, 6.0f + 12.0f * (float)l, (const float(*)[3])ct, (const float(*)[3])cb, xs, N, true, l + 1, AMP[l]);
  }
}
