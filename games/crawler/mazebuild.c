/* crawler floor builder (mazebuild.h). */
#include "mazebuild.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/math.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/vfs.h"

t3_color mb_lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}

/* ── .cwchar (tools/blender/cwchar_lib.py writes them) ─────────────────── */

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
bool mb_chr_load(mb_chr *c, const char *path) {
  memset(c, 0, sizeof *c);
  size_t len = 0;
  uint8_t *buf = cw_vfs_read(path, &len);
  if (!buf || len < 16 || memcmp(buf, "CWCH", 4) != 0) {
    cw_error("crawler: cannot read the model %s", path);
    free(buf);
    return false;
  }
  c->nclip = (int)rd32(buf + 8), c->ngroup = CW_MIN((int)rd32(buf + 12), MB_MAXG);
  const uint8_t *p = buf + 16;
  for (int k = 0; k < (int)rd32(buf + 8); k++, p += 4) {
    int n = (int)rd32(p);
    if (k < MB_MAXC) c->nf[k] = n, c->first[k] = c->total;
    c->total += n;
  }
  c->lo_y = 1e9f, c->hi_y = -1e9f;
  float r2 = 0;
  for (int g = 0; g < (int)rd32(buf + 12); g++) {
    int nv = (int)rd32(p), nt = (int)rd32(p + 4);
    p += 8;
    const uint8_t *uv = p, *col = p + 8 * nv, *idx = p + 20 * nv;
    p += 20 * (size_t)nv + (((size_t)nt * 6 + 3) & ~(size_t)3);
    float *cl = malloc(sizeof(float) * 3 * (size_t)nv);
    for (int v = 0; v < nv; v++) {
      float rgb[3];
      memcpy(rgb, col + 12 * v, 12);
      bool emi = rgb[0] > 1.5f || rgb[1] > 1.5f || rgb[2] > 1.5f;
      t3_color l = emi ? (t3_color){ rgb[0], rgb[1], rgb[2] } : mb_lin(rgb[0], rgb[1], rgb[2]);
      cl[v * 3] = l.r, cl[v * 3 + 1] = l.g, cl[v * 3 + 2] = l.b;
    }
    for (int f = 0; f < c->total; f++) {
      if (g < MB_MAXG && f < MB_MAXF) {
        t3_geometry *geo = t3_geometry_new();
        t3_geometry_set_attribute(geo, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, p, nv, 3));
        t3_geometry_set_attribute(geo, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, p + 12 * (size_t)nv, nv, 3));
        t3_geometry_set_attribute(geo, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, nv, 2));
        t3_geometry_set_attribute(geo, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, cl, nv, 3));
        t3_geometry_set_index(geo, t3_attribute_new(T3_UINT16, idx, nt * 3, 1));
        cw_geometry_optimize_vertex_cache(geo, NULL, NULL);
        c->geo[g][f] = geo;
        const float *pp = (const float *)p;
        for (int v = 0; v < nv; v++) {
          float x, y, z;
          memcpy(&x, pp + v * 3, 4), memcpy(&y, pp + v * 3 + 1, 4), memcpy(&z, pp + v * 3 + 2, 4);
          c->lo_y = CW_MIN(c->lo_y, y), c->hi_y = CW_MAX(c->hi_y, y);
          r2 = CW_MAX(r2, x * x + z * z);
        }
      }
      p += 24 * (size_t)nv;
    }
    free(cl);
  }
  c->rad = sqrtf(r2);
  c->total = CW_MIN(c->total, MB_MAXF);
  free(buf);
  return true;
}
/* a wall kit's tops darker and quieter (the eye goes to the floor and the action): every vertex whose normal points up
 * takes k of its colour, the faces keep theirs */
void mb_darken_tops(mb_chr *c, float k) {
  for (int f = 0; f < MB_MAXF; f++) {
    t3_geometry *g = c->geo[0][f];
    if (!g) continue;
    t3_attribute *na = g->attributes[T3_ATTR_NORMAL], *ca = g->attributes[T3_ATTR_COLOR];
    if (!na || !ca) continue;
    const float *n = t3_attribute_f32(na);
    float *col = t3_attribute_f32(ca);
    for (int v = 0; v < ca->count; v++)
      if (n[v * 3 + 1] > 0.6f) col[v * 3] *= k, col[v * 3 + 1] *= k, col[v * 3 + 2] *= k;
    ca->version++;
  }
}
void mb_chr_free(mb_chr *c) {
  for (int g = 0; g < MB_MAXG; g++)
    for (int f = 0; f < MB_MAXF; f++)
      if (c->geo[g][f]) t3_release(c->geo[g][f]);
  memset(c, 0, sizeof *c);
}

void mazebuild_kit(int theme, const char *part, char *out, int len) { snprintf(out, (size_t)len, "models/kit_%s_%s.cwchar", MZ_THEME_ID[CW_CLAMP(theme, 0, MZ_THEMES - 1)], part); }

uint64_t mazebuild_bake_hash(const char *text, int theme) {
  uint64_t h = maze_bake_hash(text);
  static const char *const PARTS[3] = { "wall", "torch", "decor" };
  for (int k = 0; k < 3; k++) {
    char path[96];
    mazebuild_kit(theme, PARTS[k], path, sizeof path);
    size_t len = 0;
    uint8_t *b = cw_vfs_read(path, &len);
    for (size_t i = 0; i < len; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    h = (h ^ (uint64_t)len) * 0x100000001b3ull;
    free(b);
  }
  return h;
}

/* ── building ────────────────────────────────────────────────────────── */

static uint32_t thash(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float fhash(int x, int z, uint32_t seed) { return (float)(thash(x, z, seed) & 0xffffff) / 16777216.0f; }
static float vnoise(float x, float z, uint32_t seed) {
  int ix = (int)floorf(x), iz = (int)floorf(z);
  float fx = x - (float)ix, fz = z - (float)iz;
  fx = fx * fx * (3 - 2 * fx), fz = fz * fz * (3 - 2 * fz);
  float a = fhash(ix, iz, seed), b = fhash(ix + 1, iz, seed), c = fhash(ix, iz + 1, seed), d = fhash(ix + 1, iz + 1, seed);
  return (a * (1 - fx) + b * fx) * (1 - fz) + (c * (1 - fx) + d * fx) * fz;
}

static cw_node mesh_node(cw_node parent, const char *name, t3_mesh *mesh, bool baked) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, n);
  cw_node_set(n, "gi_mode", cw_val_enum(baked ? "STATIC" : "DYNAMIC"));
  cw_mesh3d_set_procedural(n, mesh);
  return n;
}

static t3_texture *load_png(const char *path, float repeat) {
  size_t len = 0;
  void *png = cw_vfs_read(path, &len);
  t3_texture *tx = png ? t3_texture_load_memory(png, len) : NULL;
  free(png);
  if (!tx) return NULL;
  tx->wrap_s = tx->wrap_t = T3_REPEAT;
  tx->repeat = (t3_vec2){ repeat, repeat };
  tx->flip_y = false;
  tx->generate_mipmaps = true;
  tx->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  tx->mag_filter = T3_LINEAR;
  tx->anisotropy = 8;
  tx->color_space = T3_SRGB_COLOR_SPACE;
  return tx;
}

/* the tilesets' floor look: its texture's tile size (m), tint and roughness; the rock's colour */
static const struct { float tile, tint[3], rough, rock[3]; } TS[MZ_THEMES] = {
  { 5.0f, { 1.0f, 0.97f, 0.92f }, 0.9f, { 0.06f, 0.055f, 0.05f } },
  { 4.0f, { 0.85f, 0.95f, 0.92f }, 0.6f, { 0.035f, 0.05f, 0.05f } },
  { 5.0f, { 1.0f, 0.92f, 0.85f }, 0.8f, { 0.05f, 0.04f, 0.035f } },
  { 6.0f, { 1.25f, 1.35f, 1.3f }, 0.85f, { 0.035f, 0.045f, 0.04f } },
  { 5.0f, { 1.0f, 0.98f, 0.95f }, 0.5f, { 0.07f, 0.065f, 0.06f } },
  { 5.0f, { 1.9f, 1.6f, 1.7f }, 0.7f, { 0.04f, 0.025f, 0.035f } },
};

static void build_floor(mb_built *b, const maze *m) {
  float W = (float)m->w * MZ_TILE, H = (float)m->h * MZ_TILE;
  t3_geometry *pg = t3_plane_geometry_new(W, H, m->w * 2, m->h * 2);
  t3_geometry_apply_matrix4(pg, &(t3_mat4){ { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1 } }); /* face +Y */
  const t3_attribute *pa = pg->attributes[T3_ATTR_POSITION];
  const float *pp = t3_attribute_f32((t3_attribute *)pa);
  float *col = malloc(sizeof(float) * 3 * (size_t)pa->count);
  const float *tint = TS[b->theme].tint;
  /* each tileset's accent laid over its floor in patches: moss in the crypt, algae in the sewers, rust in the foundry,
   * glowing lichen in the grotto, red stone in the cathedral, violet ash in the abyss (colour without darkness) */
  static const float ACC[MZ_THEMES][3] = { { 0.75f, 1.15f, 0.6f }, { 0.5f, 1.15f, 0.95f }, { 1.3f, 0.75f, 0.45f }, { 0.55f, 1.2f, 1.25f }, { 1.3f, 0.6f, 0.6f }, { 1.05f, 0.6f, 1.35f } };
  const float *acc = ACC[b->theme];
  for (int i = 0; i < pa->count; i++) { /* slow mottling in the tileset's tint, and its accent in patches */
    float x = pp[i * 3], z = pp[i * 3 + 2];
    float k = 0.85f + 0.3f * (vnoise(x / 7, z / 7, 11 + (uint32_t)m->floor) - 0.5f) + 0.15f * (vnoise(x / 2.3f, z / 2.3f, 12) - 0.5f);
    float a = CW_CLAMP((vnoise(x / 4.5f, z / 4.5f, 31 + (uint32_t)m->floor) - 0.5f) * 3.0f, 0.0f, 1.0f);
    for (int c = 0; c < 3; c++) col[i * 3 + c] = k * tint[c] * (1.0f + a * (acc[c] - 1.0f));
  }
  t3_geometry_set_attribute(pg, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, col, pa->count, 3));
  free(col);
  t3_material *gm = t3_mesh_standard_material_new(0xffffff);
  gm->vertex_colors = true;
  gm->roughness = TS[b->theme].rough;
  char path[64];
  snprintf(path, sizeof path, "textures/%s_floor.png", MZ_THEME_ID[b->theme]);
  t3_texture *tx = load_png(path, 1);
  if (tx) {
    tx->repeat = (t3_vec2){ W / TS[b->theme].tile, H / TS[b->theme].tile };
    t3_material_set_texture(gm, T3_MAP, tx);
    t3_release(tx);
  } else
    cw_error("crawler: the floor texture %s did not load", path);
  t3_mesh *mesh = t3_mesh_new(pg, gm);
  cw_node n = mesh_node(b->node, "Floor", mesh, true);
  cw_node_set(n, "lightmap_uv", cw_val_enum("UV0")); /* the plane's own uv0 is a clean [0, 1] layout */
  t3_release(mesh), t3_release(pg), t3_release(gm);
}

static bool solid_tile(const maze *m, int x, int y) { return !mz_in(m, x, y) || m->t[y][x] == T_WALL || m->t[y][x] == T_VOID; }
/* a wall tile drawn as a block: one with open ground (anything not wall or void) among its eight neighbours */
static bool faces_open(const maze *m, int x, int y) {
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if ((dx || dy) && mz_in(m, x + dx, y + dy) && !solid_tile(m, x + dx, y + dy)) return true;
  return false;
}

static t3_instanced_mesh *static_im(cw_node parent, const char *name, t3_geometry *g, t3_material *mat, int n, bool baked) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, mat, CW_MAX(n, 1));
  im->mesh.base.cast_shadow = false;
  im->mesh.base.receive_shadow = false;
  for (int i = 0; i < CW_MAX(n, 1); i++) t3_instanced_mesh_set_color_at(im, i, (t3_color){ 1, 1, 1 }); /* (makes the colour buffer) */
  im->instance_matrix->usage = T3_DYNAMIC_DRAW;
  im->instance_color->usage = T3_DYNAMIC_DRAW;
  t3_sphere sph = { { 0, 0, 0 }, 200 };
  t3_instanced_mesh_set_bounding_sphere(im, &sph);
  mesh_node(parent, name, &im->mesh, baked);
  im->count = n;
  t3_release(g), t3_release(mat); /* the mesh holds them */
  t3_release(im);                 /* the node holds it */
  return im;
}
static void put(t3_instanced_mesh *im, int i, float x, float y, float z, float yaw) {
  t3_mat4 mm;
  t3_quat q = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
  t3_mat4_compose(&mm, (t3_vec3){ x, y, z }, q, (t3_vec3){ 1, 1, 1 });
  t3_instanced_mesh_set_matrix_at(im, i, &mm);
  t3_instanced_mesh_set_color_at(im, i, (t3_color){ 1, 1, 1 });
}

static t3_geometry *box_geo(float w, float h, float d) {
  t3_geometry *g = t3_box_geometry_new(w, h, d, 1, 1, 1);
  t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, h * 0.5f, 0, 1 } });
  int n = g->attributes[T3_ATTR_POSITION]->count;
  float *c = malloc(sizeof(float) * 3 * (size_t)n);
  for (int i = 0; i < n * 3; i++) c[i] = 0.3f;
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, c, n, 3));
  free(c);
  return g;
}

static void build_walls(mb_built *b, const maze *m) {
  char path[96];
  mazebuild_kit(b->theme, "wall", path, sizeof path);
  mb_chr c;
  bool have = mb_chr_load(&c, path);
  if (have) mb_darken_tops(&c, 1.0f);
  b->wall_sy = have && c.hi_y > 0.1f ? CW_MIN(MB_WALL_H / c.hi_y, 1.0f) : 1.0f;
  b->wall_mat = t3_mesh_lambert_material_new(0xffffff); /* diffuse only: the walls fill much of the screen, so their light is cheap per pixel (stone is matte) */
  b->wall_mat->vertex_colors = true;
  { /* the walls sit a step under the characters: mid-value blocks, never the brightest thing on screen */
    static const float WALL_K[MZ_THEMES] = { 0.5f, 0.85f, 0.8f, 0.7f, 0.42f, 0.95f };
    float k = WALL_K[CW_CLAMP(b->theme, 0, MZ_THEMES - 1)];
    b->wall_mat->color = (t3_color){ k, k, k };
  }

  memset(b->nwall, 0, sizeof b->nwall);
  for (int y = 0; y < m->h; y++)
    for (int x = 0; x < m->w; x++) {
      if (m->t[y][x] != T_WALL || !faces_open(m, x, y)) continue;
      uint32_t h = thash(x, y, 77);
      int v = (int)(h % MB_VARIANTS), n = b->nwall[v]++;
      b->wx[v][n] = (uint8_t)x, b->wy[v][n] = (uint8_t)y, b->wr[v][n] = (uint8_t)((h >> 8) & 3);
    }
  for (int v = 0; v < MB_VARIANTS; v++) {
    t3_geometry *g = have && c.total > v ? t3_retain(c.geo[0][v]) : box_geo(MZ_TILE, MB_WALL_H, MZ_TILE);
    char nm[16];
    snprintf(nm, sizeof nm, "Wall%d", v);
    b->wall[v] = static_im(b->node, nm, g, t3_retain(b->wall_mat), b->nwall[v], true);
    for (int i = 0; i < b->nwall[v]; i++) {
      t3_mat4 mm;
      float yaw = (float)b->wr[v][i] * 1.5707963f;
      t3_quat q = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
      t3_mat4_compose(&mm, (t3_vec3){ mz_wx(m, b->wx[v][i]), 0, mz_wz(m, b->wy[v][i]) }, q, (t3_vec3){ 1, b->wall_sy, 1 });
      t3_instanced_mesh_set_matrix_at(b->wall[v], i, &mm);
    }
    b->wall[v]->count = b->nwall[v];
  }
  if (have) mb_chr_free(&c);
}

/* the rock: a flat dark top over every solid tile that is not a drawn block, and a margin of it round the maze */
static void build_rock(mb_built *b, const maze *m) {
  const int MARGIN = 14;
  int cap = (m->w + 2 * MARGIN) * (m->h + 2 * MARGIN);
  float *pos = malloc(sizeof(float) * 12 * (size_t)cap), *col = malloc(sizeof(float) * 12 * (size_t)cap), *nrm = malloc(sizeof(float) * 12 * (size_t)cap);
  uint32_t *idx = malloc(sizeof(uint32_t) * 6 * (size_t)cap);
  int nq = 0;
  const float *rc = TS[b->theme].rock;
  for (int y = -MARGIN; y < m->h + MARGIN; y++)
    for (int x = -MARGIN; x < m->w + MARGIN; x++) {
      bool in = mz_in(m, x, y);
      if (in && (!solid_tile(m, x, y) || (m->t[y][x] == T_WALL && faces_open(m, x, y)))) continue;
      float cx = mz_wx(m, (float)x), cz = mz_wz(m, (float)y), hs = MZ_TILE * 0.5f;
      float k = 0.75f + 0.5f * fhash(x, y, 9);
      static const float QX[4] = { -1, 1, 1, -1 }, QZ[4] = { -1, -1, 1, 1 };
      for (int v = 0; v < 4; v++) {
        float *p = pos + (nq * 4 + v) * 3, *cc = col + (nq * 4 + v) * 3, *nn = nrm + (nq * 4 + v) * 3;
        p[0] = cx + QX[v] * hs, p[1] = MB_WALL_H - 0.02f, p[2] = cz + QZ[v] * hs;
        cc[0] = rc[0] * k * 0.22f, cc[1] = rc[1] * k * 0.22f, cc[2] = rc[2] * k * 0.22f; /* nearly black: solid rock in the dark */
        nn[0] = 0, nn[1] = 1, nn[2] = 0;
      }
      uint32_t *q = idx + nq * 6, o = (uint32_t)nq * 4;
      q[0] = o, q[1] = o + 2, q[2] = o + 1, q[3] = o, q[4] = o + 3, q[5] = o + 2;
      nq++;
    }
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, nq * 4, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nrm, nq * 4, 3));
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, col, nq * 4, 3));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, idx, nq * 6, 1));
  free(pos), free(col), free(nrm), free(idx);
  t3_material *mat = t3_mesh_basic_material_new(0xffffff);
  mat->vertex_colors = true;
  t3_mesh *mesh = t3_mesh_new(g, mat);
  mesh_node(b->node, "Rock", mesh, false);
  t3_release(mesh), t3_release(g), t3_release(mat);
}

static t3_material *glow_material(void) { /* a glow group: its vertex colours as they are (HDR), unlit */
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  return m;
}

static void build_props(mb_built *b, const maze *m) {
  for (int kind = 0; kind < 2; kind++) {
    int n = 0;
    for (int i = 0; i < m->nprop; i++) n += m->prop[i].kind == kind;
    if (!n) continue;
    char path[96];
    mazebuild_kit(b->theme, kind ? "torch" : "decor", path, sizeof path);
    mb_chr c;
    if (!mb_chr_load(&c, path)) continue;
    /* decor: one instanced mesh per frame used would be many draws; the frames share topology, so each decor frame
     * is its own small mesh: four at most */
    int frames = kind ? 1 : CW_MIN(c.total, 4);
    for (int f = 0; f < frames; f++) {
      int nf = 0;
      for (int i = 0; i < m->nprop; i++) nf += m->prop[i].kind == kind && (kind || m->prop[i].frame % frames == f);
      if (!nf) continue;
      for (int g = 0; g < c.ngroup; g++) {
        char nm[24];
        snprintf(nm, sizeof nm, "%s%d_%d", kind ? "Torch" : "Decor", f, g);
        t3_material *mat;
        if (g == 0) {
          mat = t3_mesh_standard_material_new(0xffffff);
          mat->vertex_colors = true;
          mat->roughness = 0.75f;
        } else
          mat = glow_material();
        t3_instanced_mesh *im = static_im(b->node, nm, t3_retain(c.geo[g][f]), mat, nf, g == 0);
        int k = 0;
        for (int i = 0; i < m->nprop; i++) {
          const mz_prop *p = &m->prop[i];
          if (p->kind != kind || (!kind && p->frame % frames != f)) continue;
          float x = mz_wx(m, p->x), z = mz_wz(m, p->y), yaw = 0;
          if (kind) { /* a torch on the wall face of its side, sticking out over the floor tile */
            static const float SX[4] = { 0, 1, 0, -1 }, SZ[4] = { -1, 0, 1, 0 }, YAW[4] = { 0, -1.5707963f, 3.1415927f, 1.5707963f };
            x += SX[p->side & 3] * MZ_TILE * 0.5f, z += SZ[p->side & 3] * MZ_TILE * 0.5f, yaw = YAW[p->side & 3];
          } else
            yaw = (float)(p->side & 3) * 1.5707963f + fhash(p->x, p->y, 5) * 0.6f;
          put(im, k++, x, 0, z, yaw);
        }
        im->instance_matrix->version++;
        if (kind && g == 0) b->torch = im;
        else if (kind) b->torch_glow = im;
        else if (g == 0 && f == 0) b->decor = im;
      }
    }
    mb_chr_free(&c);
  }
}

static cw_node build_lights(mb_built *b, const maze *m) {
  cw_node group = cw_node_new("Node3D", "BakeLights");
  cw_node_add_child(b->node, group);
  for (int i = 0; i < m->nlight; i++) {
    const mz_light *l = &m->light[i];
    cw_node n = cw_node_new("OmniLight3D", "Omni");
    cw_node_add_child(group, n);
    float x = mz_wx(m, l->x), z = mz_wz(m, l->y);
    for (int k = 0; k < m->nprop; k++) /* a torch's light sits at its flame, out from the wall */
      if (m->prop[k].kind == 1 && m->prop[k].x == l->x && m->prop[k].y == l->y) {
        static const float SX[4] = { 0, 1, 0, -1 }, SZ[4] = { -1, 0, 1, 0 };
        x += SX[m->prop[k].side & 3] * 0.6f, z += SZ[m->prop[k].side & 3] * 0.6f;
        break;
      }
    cw_node3d_set_position(n, (t3_vec3){ x, l->h, z });
    cw_node_set(n, "color", cw_val_color(l->r, l->g, l->b, 1));
    cw_node_set(n, "energy", cw_val_float(l->energy));
    cw_node_set(n, "range", cw_val_float(l->range));
    cw_node_set(n, "decay", cw_val_float(2));
    cw_node_set(n, "bake_direct", cw_val_bool(true));
    cw_node_set(n, "realtime", cw_val_bool(false));
  }
  return group;
}

bool mazebuild_build(cw_node parent, const maze *m, mb_built *b) {
  memset(b, 0, sizeof *b);
  b->m = m;
  b->theme = CW_CLAMP(m->theme, 0, MZ_THEMES - 1);
  b->node = cw_node_new("Node3D", "Level");
  cw_node_add_child(parent, b->node);
  char scene[64];
  snprintf(scene, sizeof scene, "scenes/th_%s.cwscene", MZ_THEME_ID[b->theme]);
  cw_node light = cw_scene_instance_path(scene);
  if (cw_node_valid(light)) {
    cw_node_set_name(light, "Light");
    cw_node_add_child(b->node, light);
  } else
    cw_error("crawler: the light scene %s did not load", scene);
  build_floor(b, m);
  build_walls(b, m);
  build_rock(b, m);
  build_props(b, m);
  return true;
}

bool mazebuild_lightmap(cw_tree *t, mb_built *b, const char *bake_path, const char *text) {
  if (b->lm) return true;
  if (bake_path && *bake_path) {
    cw_lightmap_settings s = cw_lightmap_settings_default();
    s.texel_density = 3;
    s.max_size = 320;
    s.samples = 32;
    s.bounces = 1;
    s.probe_spacing = 3.0f;
    s.probe_samples = 48;
    s.denoise = 2;
    s.direct_samples = 8;
    s.shadow_softness = 0.08f;
    s.reflection_size = 0;
    double t0 = cw_time();
    cw_node lights = build_lights(b, b->m);
    cw_tree_pre_render(t, 0); /* the new lights' transforms */
    b->lm = cw_lightmap_bake(t, b->node, &s);
    cw_node_free(lights);
    if (!b->lm) return false;
    if (!cw_lightmap_save(b->lm, bake_path)) cw_error("crawler: could not write the bake to %s", bake_path);
    char hp[512];
    snprintf(hp, sizeof hp, "%.*s.hash", (int)(strlen(bake_path) > 5 ? strlen(bake_path) - 5 : strlen(bake_path)), bake_path);
    FILE *f = text ? fopen(hp, "wb") : NULL;
    if (f) fprintf(f, "%016llx\n", (unsigned long long)mazebuild_bake_hash(text, b->theme)), fclose(f);
    cw_lightmap_info info;
    cw_lightmap_get_info(b->lm, &info);
    size_t len = 0;
    cw_lightmap_bytes(b->lm, &len);
    fprintf(stderr, "crawler: baked floor %d: %d meshes, %d texels, %d probes, %.1f s, %zu bytes, hash %016llx\n", b->m->floor, info.meshes, info.texels, info.probes,
            cw_time() - t0, len, (unsigned long long)info.hash);
  } else {
    char path[64];
    maze_bake_path(b->m->floor, path, sizeof path);
    b->lm = cw_lightmap_load(path);
    if (!b->lm) {
      cw_error("crawler: floor %d has no bake (%s): it draws unbaked", b->m->floor, path);
      return false;
    }
  }
  /* a mid-key floor under the characters (DESIGN.md, the look): each tileset's floor brought to about the same value, so the
   * darker stone of the deep tilesets does not sink the screen */
  static const float FLOOR_GAIN[MZ_THEMES] = { 1.35f, 1.65f, 2.6f, 1.45f, 2.0f, 2.6f };
  cw_lightmap_apply_shaded(t, b->node, b->lm, FLOOR_GAIN[CW_CLAMP(b->theme, 0, MZ_THEMES - 1)], CW_LIGHTMAP_BAKED);
  /* the scenery stands still: its light from the bake set once per instance (a little out from a wall's middle) */
  for (int v = 0; v < MB_VARIANTS; v++) {
    for (int i = 0; i < b->nwall[v]; i++) {
      const maze *m = b->m;
      int x = b->wx[v][i], y = b->wy[v][i];
      float sx = 0, sz = 0;
      static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
      for (int d = 0; d < 4; d++)
        if (!solid_tile(m, x + DX[d], y + DY[d])) sx += (float)DX[d], sz += (float)DY[d];
      t3_instanced_mesh_set_color_at(b->wall[v], i, mazebuild_light_at(b, mz_wx(m, x) + sx * 1.3f, mz_wz(m, y) + sz * 1.3f));
    }
    b->wall[v]->instance_color->version++;
  }
  return true;
}

t3_color mazebuild_light_at(const mb_built *b, float x, float z) {
  if (!b->lm) return (t3_color){ 1, 1, 1 };
  t3_color e = { 0, 0, 0 }, f = { 0, 0, 0 };
  bool hp = cw_lightmap_probe_irradiance(b->lm, (t3_vec3){ x, 1.2f, z }, (t3_vec3){ 0, 0.88f, 0.47f }, &e);
  bool hf = cw_lightmap_irradiance_below(b->lm, (t3_vec3){ x, 1.0f, z }, &f);
  if (!hp && !hf) return (t3_color){ 1, 1, 1 };
  const float base = 0.3f, gain = 0.55f;
  t3_color c = { base + gain * 0.5f * (e.r + f.r), base + gain * 0.5f * (e.g + f.g), base + gain * 0.5f * (e.b + f.b) };
  c.r = CW_MIN(c.r, 3.0f), c.g = CW_MIN(c.g, 3.0f), c.b = CW_MIN(c.b, 3.0f);
  return c;
}

void mazebuild_free(mb_built *b) {
  if (cw_node_valid(b->node)) cw_node_free(b->node);
  if (b->wall_mat) t3_release(b->wall_mat);
  /* the lightmap stays alive until the next floor's is loaded (the materials read its textures) */
  memset(b, 0, sizeof *b);
}
