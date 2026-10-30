/* hordeshooter level builder (levelbuild.h). */
#include "levelbuild.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/math.h"
#include "cartwheel/render3d.h"
#include "cartwheel/platform.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/vfs.h"

/* the arena: 2 x 38 m square; the floor runs 35 m past it on every side, as the yard's */
#define LB_HALF 38.0f
#define LB_GS (LB_HALF * 2 + 70)

static t3_color lb_lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}

/* ── parsing ─────────────────────────────────────────────────────────── */

static int find_prop(const lb_level *lv, const char *name) {
  for (int i = 0; i < lv->nprop; i++)
    if (!strcmp(lv->prop[i].name, name)) return i;
  return -1;
}

bool levelbuild_parse(const char *asset_path, lb_level *lv) {
  size_t len = 0;
  char *text = cw_vfs_read(asset_path, &len);
  if (!text) {
    cw_error("levelbuild: cannot read %s", asset_path);
    return false;
  }
  memset(lv, 0, sizeof *lv);
  lv->floor_tile = 8, lv->floor_rough = 0.9f, lv->floor_tint[0] = lv->floor_tint[1] = lv->floor_tint[2] = 1;
  char *save = NULL;
  int lineno = 0;
  bool ok = true;
  for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
    lineno++;
    char *hash = strchr(line, '#');
    if (hash) *hash = 0;
    char key[16] = { 0 };
    int used = 0;
    if (sscanf(line, " %15s%n", key, &used) != 1) continue;
    const char *a = line + used;
    int n = 1;
    if (!strcmp(key, "level")) n = sscanf(a, " %23s %63[^\n]", lv->id, lv->name) >= 1;
    else if (!strcmp(key, "waves")) n = sscanf(a, "%d", &lv->waves);
    else if (!strcmp(key, "roster")) {
      int k = 0;
      while (lv->nroster < LB_MAXKIND && sscanf(a, " %15s%n", lv->roster[lv->nroster], &k) == 1) lv->nroster++, a += k;
    } else if (!strcmp(key, "wave")) {
      int w = 0, k = 0;
      if (sscanf(a, "%d%n", &w, &k) == 1 && w >= 1 && w <= LB_MAXWAVE) {
        lb_wave *wv = &lv->wave[w - 1];
        a += k;
        char tok[40];
        while (wv->n < LB_MAXKIND && sscanf(a, " %39s%n", tok, &k) == 1) {
          a += k;
          char *colon = strchr(tok, ':');
          if (!strcmp(tok, "boss")) wv->boss = true;
          else if (colon) {
            *colon = 0;
            snprintf(wv->kind[wv->n], sizeof wv->kind[0], "%s", tok);
            wv->share[wv->n++] = (float)atof(colon + 1);
          }
        }
      } else n = 0;
    } else if (!strcmp(key, "monster")) n = sscanf(a, " %95s", lv->monster);
    else if (!strcmp(key, "boss")) n = sscanf(a, " %95s %63[^\n]", lv->boss, lv->boss_name) >= 1;
    else if (!strcmp(key, "scene")) n = sscanf(a, " %95s", lv->scene);
    else if (!strcmp(key, "lightmap")) n = sscanf(a, " %95s", lv->lightmap);
    else if (!strcmp(key, "floor"))
      n = sscanf(a, " %95s %f %f %f %f %f", lv->floor_tex, &lv->floor_tile, &lv->floor_rough, &lv->floor_tint[0], &lv->floor_tint[1], &lv->floor_tint[2]) == 6;
    else if (!strcmp(key, "prop")) {
      if (lv->nprop >= LB_MAXPROP) { cw_error("levelbuild: %s: too many props", asset_path); ok = false; break; }
      lb_prop *p = &lv->prop[lv->nprop];
      n = sscanf(a, " %31s %95s %15s %f %f %f %d", p->name, p->file, p->tx, &p->rep, &p->rough, &p->metal, &p->solid) == 7;
      if (n) lv->nprop++;
    } else if (!strcmp(key, "at")) {
      if (lv->nat >= LB_MAXAT) { cw_error("levelbuild: %s: too many placements", asset_path); ok = false; break; }
      char name[32];
      lb_at *p = &lv->at[lv->nat];
      int got = sscanf(a, " %31s %f %f %f %f %f %f", name, &p->x, &p->y, &p->z, &p->yaw, &p->hx, &p->hz);
      p->prop = find_prop(lv, name);
      n = got >= 5 && p->prop >= 0;
      if (n) lv->nat++;
    } else if (!strcmp(key, "door")) n = sscanf(a, " %95s %f %f", lv->door, &lv->door_x, &lv->door_z) == 3;
    else if (!strcmp(key, "spawn")) {
      if (lv->nspawn < LB_MAXSPAWN) n = sscanf(a, "%f %f %f", &lv->spawn[lv->nspawn][0], &lv->spawn[lv->nspawn][1], &lv->spawn[lv->nspawn][2]) == 3, lv->nspawn += n;
    } else if (!strcmp(key, "pickup")) {
      if (lv->npick < LB_MAXPICK) n = sscanf(a, "%f %f", &lv->pick[lv->npick][0], &lv->pick[lv->npick][1]) == 2, lv->npick += n;
    } else if (!strcmp(key, "shrine")) {
      if (lv->nshrine < LB_MAXSHRINE) n = sscanf(a, "%f %f", &lv->shrine[lv->nshrine][0], &lv->shrine[lv->nshrine][1]) == 2, lv->nshrine += n;
    } else if (!strcmp(key, "hazard")) {
      if (lv->nhaz < LB_MAXHAZ) {
        lb_hazard *h = &lv->haz[lv->nhaz];
        n = sscanf(a, " %11s %f %f %f %f", h->kind, &h->x, &h->z, &h->hx, &h->hz) == 5;
        lv->nhaz += n;
      }
    } else if (!strcmp(key, "light")) {
      if (lv->nlight < LB_MAXLIGHT) {
        lb_light *l = &lv->light[lv->nlight];
        char type[8];
        int bo = 0;
        n = sscanf(a, " %7s %f %f %f %f %f %f %f %f %f %f %f %f %f %d", type, &l->x, &l->y, &l->z, &l->tx, &l->ty, &l->tz, &l->r, &l->g, &l->b,
                   &l->energy, &l->range, &l->angle, &l->penumbra, &bo) == 15;
        l->spot = !strcmp(type, "spot"), l->bake_only = bo != 0;
        lv->nlight += n;
      }
    } else n = 1; /* unknown records are for the sim or later versions */
    if (!n) {
      cw_error("levelbuild: %s:%d: cannot read the %s record", asset_path, lineno, key);
      ok = false;
    }
  }
  free(text);
  return ok;
}

/* ── .cwchar (the format in tools/blender/hordechars.py) ─────────────── */

typedef struct lb_chr { int nclip, ngroup, total; t3_geometry *geo[2][24]; } lb_chr;

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static bool chr_load(lb_chr *c, const char *path) {
  size_t len = 0;
  uint8_t *buf = cw_vfs_read(path, &len);
  memset(c, 0, sizeof *c);
  if (!buf || len < 16 || memcmp(buf, "CWCH", 4) != 0) {
    cw_error("levelbuild: cannot read the model %s", path);
    free(buf);
    return false;
  }
  c->nclip = (int)rd32(buf + 8), c->ngroup = CW_MIN((int)rd32(buf + 12), 2);
  const uint8_t *p = buf + 16;
  for (int k = 0; k < (int)rd32(buf + 8); k++, p += 4) c->total += (int)rd32(p);
  c->total = CW_MIN(c->total, 24);
  for (int g = 0; g < c->ngroup; g++) {
    int nv = (int)rd32(p), nt = (int)rd32(p + 4);
    p += 8;
    const uint8_t *uv = p, *col = p + 8 * nv, *idx = p + 20 * nv;
    p += 20 * (size_t)nv + (((size_t)nt * 6 + 3) & ~(size_t)3);
    float *cl = malloc(sizeof(float) * 3 * (size_t)nv);
    for (int v = 0; v < nv; v++) {
      float rgb[3];
      memcpy(rgb, col + 12 * v, 12);
      bool emi = rgb[0] > 1.5f || rgb[1] > 1.5f || rgb[2] > 1.5f;
      t3_color l = emi ? (t3_color){ rgb[0], rgb[1], rgb[2] } : lb_lin(rgb[0], rgb[1], rgb[2]);
      cl[v * 3] = l.r, cl[v * 3 + 1] = l.g, cl[v * 3 + 2] = l.b;
    }
    for (int f = 0; f < c->total; f++) {
      t3_geometry *geo = t3_geometry_new();
      t3_geometry_set_attribute(geo, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, p, nv, 3));
      t3_geometry_set_attribute(geo, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, p + 12 * (size_t)nv, nv, 3));
      p += 24 * (size_t)nv;
      t3_geometry_set_attribute(geo, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, nv, 2));
      t3_geometry_set_attribute(geo, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, cl, nv, 3));
      t3_geometry_set_index(geo, t3_attribute_new(T3_UINT16, idx, nt * 3, 1));
      c->geo[g][f] = geo;
    }
    free(cl);
  }
  free(buf);
  return true;
}
static void chr_free(lb_chr *c) {
  for (int g = 0; g < 2; g++)
    for (int f = 0; f < 24; f++)
      if (c->geo[g][f]) t3_release(c->geo[g][f]), c->geo[g][f] = NULL;
}

/* ── building ────────────────────────────────────────────────────────── */

static t3_material *default_material(const char *tx, float rep, float rough, float metal, void *user) {
  (void)tx, (void)rep, (void)user;
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->roughness = rough;
  m->metalness = metal;
  m->vertex_colors = true;
  return m;
}
static t3_material *glow_material(void) { /* a glow group: its vertex colours as they are (HDR), unlit */
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  return m;
}

static cw_node mesh_node(cw_node parent, const char *name, t3_mesh *mesh, bool baked) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, n);
  cw_node_set(n, "gi_mode", cw_val_enum(baked ? "STATIC" : "DYNAMIC"));
  cw_mesh3d_set_procedural(n, mesh);
  return n;
}

static float fhash(int x, int z, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (float)((h ^ (h >> 16)) & 0xffffff) / 16777216.0f;
}
static float vnoise(float x, float z, uint32_t seed) {
  int ix = (int)floorf(x), iz = (int)floorf(z);
  float fx = x - (float)ix, fz = z - (float)iz;
  fx = fx * fx * (3 - 2 * fx), fz = fz * fz * (3 - 2 * fz);
  float a = fhash(ix, iz, seed), b = fhash(ix + 1, iz, seed), c = fhash(ix, iz + 1, seed), d = fhash(ix + 1, iz + 1, seed);
  return (a * (1 - fx) + b * fx) * (1 - fz) + (c * (1 - fx) + d * fx) * fz;
}

static void build_floor(cw_node parent, const lb_level *lv) {
  t3_geometry *pg = t3_plane_geometry_new(LB_GS, LB_GS, 136, 136);
  t3_geometry_apply_matrix4(pg, &(t3_mat4){ { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1 } }); /* face +Y */
  const t3_attribute *pa = pg->attributes[T3_ATTR_POSITION];
  const float *pp = t3_attribute_f32((t3_attribute *)pa);
  float *col = malloc(sizeof(float) * 3 * (size_t)pa->count);
  for (int i = 0; i < pa->count; i++) { /* slow mottling and a darker rim, in the level's tint */
    float x = pp[i * 3], z = pp[i * 3 + 2];
    float m = 0.75f + 0.45f * (vnoise(x / 9, z / 9, 11) - 0.5f) + 0.25f * (vnoise(x / 3.1f, z / 3.1f, 12) - 0.5f);
    float rim = CW_CLAMP((CW_MAX(fabsf(x), fabsf(z)) - (LB_HALF - 4.0f)) / 8.0f, 0.0f, 1.0f);
    m *= 1.0f - 0.45f * rim;
    for (int k = 0; k < 3; k++) col[i * 3 + k] = m * lv->floor_tint[k];
  }
  t3_geometry_set_attribute(pg, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, col, pa->count, 3));
  free(col);
  t3_material *gm = t3_mesh_standard_material_new(0xffffff);
  gm->vertex_colors = true;
  gm->roughness = lv->floor_rough;
  if (lv->floor_tex[0]) {
    size_t len = 0;
    void *png = cw_vfs_read(lv->floor_tex, &len);
    t3_texture *tx = png ? t3_texture_load_memory(png, len) : NULL;
    free(png);
    if (tx) {
      tx->wrap_s = tx->wrap_t = T3_REPEAT;
      tx->repeat = (t3_vec2){ LB_GS / lv->floor_tile, LB_GS / lv->floor_tile };
      tx->flip_y = false;
      tx->generate_mipmaps = true;
      tx->min_filter = T3_LINEAR_MIPMAP_LINEAR;
      tx->mag_filter = T3_LINEAR;
      tx->anisotropy = 8;
      tx->color_space = T3_SRGB_COLOR_SPACE;
      t3_material_set_texture(gm, T3_MAP, tx);
      t3_release(tx);
    } else
      cw_error("levelbuild: the floor texture %s did not load", lv->floor_tex);
  }
  t3_mesh *mesh = t3_mesh_new(pg, gm);
  cw_node n = mesh_node(parent, "Floor", mesh, true);
  cw_node_set(n, "lightmap_uv", cw_val_enum("UV0")); /* the plane's own uv0 is a clean [0, 1] layout */
  t3_release(mesh), t3_release(pg), t3_release(gm);
}

static const t3_mat4 ZERO;
static void build_props(lb_built *b, cw_node parent, const lb_level *lv, lb_material_fn matf, void *user) {
  for (int pi = 0; pi < lv->nprop; pi++) {
    const lb_prop *pr = &lv->prop[pi];
    int n = 0;
    for (int i = 0; i < lv->nat; i++) n += lv->at[i].prop == pi;
    if (!n) continue;
    lb_chr c;
    if (!chr_load(&c, pr->file)) continue;
    for (int g = 0; g < c.ngroup; g++) {
      t3_material *m = g == 0 ? matf(pr->tx, pr->rep, pr->rough, pr->metal, user) : glow_material();
      t3_instanced_mesh *im = t3_instanced_mesh_new(c.geo[g][0], m, n);
      im->mesh.base.cast_shadow = false;
      im->mesh.base.receive_shadow = false;
      int k = 0;
      for (int i = 0; i < lv->nat; i++) {
        const lb_at *a = &lv->at[i];
        if (a->prop != pi) continue;
        t3_mat4 mm;
        t3_quat q = { 0, cw_sin(a->yaw * 0.5f), 0, cw_cos(a->yaw * 0.5f) };
        t3_mat4_compose(&mm, (t3_vec3){ a->x, a->y, a->z }, q, (t3_vec3){ 1, 1, 1 });
        t3_instanced_mesh_set_matrix_at(im, k, &mm);
        t3_instanced_mesh_set_color_at(im, k, (t3_color){ 1, 1, 1 });
        k++;
      }
      im->instance_matrix->version++;
      im->instance_color->version++;
      t3_sphere sph = { { 0, 0, 0 }, LB_GS };
      t3_instanced_mesh_set_bounding_sphere(im, &sph);
      char nm[40];
      snprintf(nm, sizeof nm, "%s%d", pr->name, g);
      mesh_node(parent, nm, &im->mesh, g == 0);
      if (g == 0) b->prop_mesh[pi] = im;
      t3_release(im), t3_release(m);
    }
    chr_free(&c);
  }
}

static void build_hazards(cw_node parent, const lb_level *lv) {
  for (int i = 0; i < lv->nhaz; i++) {
    const lb_hazard *h = &lv->haz[i];
    bool glow = !strcmp(h->kind, "molten") || !strcmp(h->kind, "lava") || !strcmp(h->kind, "spore");
    bool flat = !strcmp(h->kind, "crumble") || !strcmp(h->kind, "vent");
    if (flat) continue; /* drawn by their props and the sim's effects */
    t3_geometry *g = t3_plane_geometry_new(h->hx * 2, h->hz * 2, 1, 1);
    t3_geometry_apply_matrix4(g, &(t3_mat4){ { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, h->x, 0.03f, h->z, 1 } });
    t3_material *m;
    if (glow) {
      m = t3_mesh_basic_material_new(0xffffff);
      m->color = !strcmp(h->kind, "spore") ? (t3_color){ 0.15f, 1.4f, 1.1f } : (t3_color){ 3.2f, 0.9f, 0.15f };
    } else { /* standing water: dark and glossy, the bake under it */
      m = t3_mesh_standard_material_new(0xffffff);
      m->color = lb_lin(0.09f, 0.12f, 0.12f);
      m->roughness = 0.08f;
      m->metalness = 0.0f;
    }
    t3_mesh *mesh = t3_mesh_new(g, m);
    char nm[24];
    snprintf(nm, sizeof nm, "Hazard%d", i);
    mesh_node(parent, nm, mesh, !glow);
    t3_release(mesh), t3_release(g), t3_release(m);
  }
}

/* the level's lights: the real-time ones (bake_only 0) always, the bake-only ones (the lamps' pools, the
 * candles) only while baking, under a node of their own that the bake frees after (three.c lights at most 8 of
 * a kind, and a light that is in the lightmap costs nothing a frame when it is not in the tree at all) */
static cw_node build_lights(cw_node parent, const lb_level *lv, bool bake_only) {
  cw_node group = cw_node_new("Node3D", bake_only ? "BakeLights" : "Lights");
  cw_node_add_child(parent, group);
  parent = group;
  for (int i = 0; i < lv->nlight; i++) {
    const lb_light *l = &lv->light[i];
    if (l->bake_only != bake_only) continue;
    cw_node n = cw_node_new(l->spot ? "SpotLight3D" : "OmniLight3D", l->spot ? "Spot" : "Omni");
    cw_node_add_child(parent, n);
    cw_node3d_set_position(n, (t3_vec3){ l->x, l->y, l->z });
    cw_node_set(n, "color", cw_val_color(l->r, l->g, l->b, 1));
    cw_node_set(n, "energy", cw_val_float(l->energy));
    cw_node_set(n, "range", cw_val_float(l->range));
    cw_node_set(n, "decay", cw_val_float(2));
    if (l->spot) {
      cw_node_set(n, "angle", cw_val_float(l->angle));
      cw_node_set(n, "penumbra", cw_val_float(l->penumbra));
      cw_node3d_look_at(n, (t3_vec3){ l->tx, l->ty, l->tz }, (t3_vec3){ 0, 1, 0 });
    }
    cw_node_set(n, "bake_direct", cw_val_bool(true));
    cw_node_set(n, "realtime", cw_val_bool(!l->bake_only));
  }
  return group;
}

bool levelbuild_build(cw_node parent, const lb_level *lv, lb_material_fn matf, void *user, lb_built *b) {
  memset(b, 0, sizeof *b);
  b->lv = lv;
  if (!matf) matf = default_material;
  b->node = cw_node_new("Node3D", "Level");
  cw_node_add_child(parent, b->node);
  if (lv->scene[0]) {
    cw_node light = cw_scene_instance_path(lv->scene);
    if (cw_node_valid(light)) {
      cw_node_set_name(light, "Light");
      cw_node_add_child(b->node, light);
    } else
      cw_error("levelbuild: the scene %s did not load", lv->scene);
  }
  build_floor(b->node, lv);
  build_props(b, b->node, lv, matf, user);
  build_hazards(b->node, lv);
  build_lights(b->node, lv, false);
  if (lv->door[0]) {
    lb_chr c;
    if (chr_load(&c, lv->door)) {
      b->hatch_groups = c.ngroup;
      b->hatch_frames = CW_MIN(c.total, 8);
      for (int g = 0; g < c.ngroup; g++)
        for (int f = 0; f < b->hatch_frames; f++) b->hatch_geo[g][f] = t3_retain(c.geo[g][f]);
      chr_free(&c);
      t3_material *m = matf("pillar", 1, 0.8f, 0.2f, user);
      b->hatch = t3_mesh_new(b->hatch_geo[0][0], m);
      cw_node hn = mesh_node(b->node, "Hatch", b->hatch, false);
      cw_node3d_set_position(hn, (t3_vec3){ lv->door_x, 0, lv->door_z });
      t3_release(m);
      if (b->hatch_groups > 1) {
        t3_material *gm = glow_material();
        b->hatch_glow = t3_mesh_new(b->hatch_geo[1][0], gm);
        cw_node gn = mesh_node(b->node, "HatchGlow", b->hatch_glow, false);
        cw_node3d_set_position(gn, (t3_vec3){ lv->door_x, 0, lv->door_z });
        t3_release(gm);
      }
    }
  }
  return true;
}

void levelbuild_hatch(lb_built *b, float t) {
  if (!b->hatch || b->hatch_frames < 1) return;
  int f = CW_CLAMP((int)(t * (float)(b->hatch_frames - 1) + 0.5f), 0, b->hatch_frames - 1);
  t3_mesh *ms[2] = { b->hatch, b->hatch_glow };
  for (int g = 0; g < 2; g++) {
    if (!ms[g] || ms[g]->geometry == b->hatch_geo[g][f]) continue;
    t3_retain(b->hatch_geo[g][f]);
    t3_release(ms[g]->geometry);
    ms[g]->geometry = b->hatch_geo[g][f];
  }
}

bool levelbuild_lightmap(cw_tree *t, lb_built *b, const char *bake_path) {
  if (b->lm) return true;
  if (bake_path && *bake_path) {
    cw_lightmap_settings s = cw_lightmap_settings_default(); /* the yard's settings (view.c) */
    s.texel_density = 7;
    s.max_size = 1024;
    s.samples = 48;
    s.bounces = 1;
    s.probe_spacing = 2.5f;
    s.probe_samples = 64;
    s.denoise = 2;
    s.direct_samples = 12;
    s.shadow_softness = 0.025f;
    s.reflection_size = 32;
    s.reflection_origin[0] = 0, s.reflection_origin[1] = 1.2f, s.reflection_origin[2] = 0;
    double t0 = cw_time();
    cw_node lights = build_lights(b->node, b->lv, true);
    cw_tree_pre_render(t, 0); /* the new lights' transforms */
    b->lm = cw_lightmap_bake(t, b->node, &s);
    cw_node_free(lights);
    if (!b->lm) return false;
    if (!cw_lightmap_save(b->lm, bake_path)) cw_error("levelbuild: could not write the bake to %s", bake_path);
    cw_lightmap_info info;
    cw_lightmap_get_info(b->lm, &info);
    fprintf(stderr, "levelbuild: baked %s: %d meshes, %d texels, %d probes, %lld rays, %.1f s, hash %016llx\n", bake_path, info.meshes, info.texels,
            info.probes, info.rays, cw_time() - t0, (unsigned long long)info.hash);
  } else if (b->lv->lightmap[0])
    b->lm = cw_lightmap_load(b->lv->lightmap);
  if (!b->lm) {
    cw_error("levelbuild: no lightmap for %s (%s)", b->lv->id, b->lv->lightmap);
    return false;
  }
  cw_lightmap_apply_shaded(t, b->node, b->lm, 1, CW_LIGHTMAP_BAKED);
  /* the props stand still: their light from the bake is set once, per instance */
  for (int pi = 0; pi < b->lv->nprop; pi++) {
    t3_instanced_mesh *im = b->prop_mesh[pi];
    if (!im) continue;
    int k = 0;
    for (int i = 0; i < b->lv->nat; i++) {
      const lb_at *a = &b->lv->at[i];
      if (a->prop != pi) continue;
      float x = a->x, z = a->z, l = sqrtf(x * x + z * z);
      if (l > 1) x -= x / l * 1.6f, z -= z / l * 1.6f; /* a little toward the middle, off its own footprint */
      t3_instanced_mesh_set_color_at(im, k++, levelbuild_light_at(b, x, z));
    }
    im->instance_color->version++;
  }
  return true;
}

t3_color levelbuild_light_at(const lb_built *b, float x, float z) {
  if (!b->lm) return (t3_color){ 1, 1, 1 };
  t3_color e = { 0, 0, 0 }, f = { 0, 0, 0 };
  bool hp = cw_lightmap_probe_irradiance(b->lm, (t3_vec3){ x, 1.2f, z }, (t3_vec3){ 0, 0.88f, 0.47f }, &e);
  bool hf = cw_lightmap_irradiance_below(b->lm, (t3_vec3){ x, 1.0f, z }, &f);
  if (!hp && !hf) return (t3_color){ 1, 1, 1 };
  /* the real-time key and rim light them a little already: the bake adds to a dim base */
  const float base = 0.35f, gain = 0.5f;
  t3_color c = { base + gain * 0.5f * (e.r + f.r), base + gain * 0.5f * (e.g + f.g), base + gain * 0.5f * (e.b + f.b) };
  c.r = CW_MIN(c.r, 3.0f), c.g = CW_MIN(c.g, 3.0f), c.b = CW_MIN(c.b, 3.0f);
  return c;
}

void levelbuild_free(lb_built *b) {
  if (cw_node_valid(b->node)) cw_node_free(b->node);
  for (int g = 0; g < 2; g++)
    for (int f = 0; f < 8; f++)
      if (b->hatch_geo[g][f]) t3_release(b->hatch_geo[g][f]);
  /* the lightmap stays: its textures are the materials' light maps until the freed node is gone (a level swap
   * keeps one lightmap per level visited, about 10 MB each) */
  memset(b, 0, sizeof *b);
}
