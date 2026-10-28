/* The course's trees and items. Items are drawn as the original did
 * (course_render.c: one quad facing the camera) and collected as phys_sim.c
 * did (within the diameter and height); trees collide as the original's
 * polyhedron (tree_polyhedron.tcl) scaled to the tree. Drawing the trees is
 * the enhancement phase's: the conifer (tree1) and the barren tree (tree2)
 * are solid meshes (assets/model/trees.bin: sky-mesa's spruce with its
 * snow-dusted needle texture, and its leafless birch; tools/extract-trees.mjs),
 * instanced per branching variant and LOD, swaying on the GPU (the "foliage"
 * shading part, shaders/foliage.mjs). The shrub (tree3) keeps the original's
 * two crossed cards of its texture. */
#include "game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/rim_light.h"
#include "cartwheel/math.h"
#include "cartwheel/physics_nodes.h"
#include "cartwheel/platform.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "cartwheel/shading.h"

static const char *const OBJ_TEXTURE[OBJ_KINDS] = {
  [OBJ_TREE3] = "original/common/shrub.png", [OBJ_TREE1] = "original/common/tree.png", [OBJ_TREE2] = "original/common/tree_barren.png",
  [OBJ_HERRING] = "original/common/herring_standard.png", [OBJ_FLAG] = "original/common/flag.png",
  [OBJ_FINISH] = "original/common/finish.png", [OBJ_START] = "original/common/start.png", [OBJ_FLOAT] = NULL,
};

static struct {
  t3_geometry *quad;             /* 1 x 1 in XY, base at y = 0 */
  t3_material *mat[OBJ_KINDS];
  cw_res tex[OBJ_KINDS];
  t3_instanced_mesh *trees[3][2]; /* the shrub's two crossed quads ([OBJ_TREE3]; the other types are solid) */
  /* solid trees: trees.bin's 12 meshes (see tools/extract-trees.mjs for the order) */
  t3_geometry *tree_geo[12], *tree_cards[2][2]; /* [lod][variant] */
  t3_material *bark_mat, *leaf_mat, *card_mat;
  t3_instanced_mesh *conifer[2][2][3]; /* [lod][variant][part 0 bark, 1 leaves, 2 branch cards] */
  t3_instanced_mesh *barren[2][2];     /* [lod][variant] (bark only: a leafless birch) */
  int *solid_slot;                     /* per course object: its index among the solid trees, -1 otherwise */
  uint8_t *solid_lod;                  /* per solid tree: 0 near, 1 far */
  int solid_count;
  int lod_tick;
  bool lod_dirty;
  float wind_time, wind_scale;
  t3_renderer *renderer;
  void *tree_file;                     /* trees.bin stays loaded: the attributes point into it */
  t3_instanced_mesh *splats;      /* a soft print of trodden snow under every trunk (buttprint.png) */
  t3_geometry *flat;
  t3_material *splat_mat;
  float *tree_phase;              /* per tree: a sway phase */
  int tree_count;
  float sway_t;
  t3_instanced_mesh *item_im[OBJ_KINDS]; /* the items of one kind: one instanced mesh (one draw per pass, not one per item) */
  t3_geometry *fish, *flag_card;      /* item_mesh.c: the herring, the flag's rippling card */
  t3_material *fish_mat;
  t3_object *gate[2];                 /* the start and finish gates: poles and a banner (item_mesh.c) */
  t3_geometry *gate_poles, *gate_banner;
  t3_material *pole_mat;
  float item_time;
  int *item_slot;                 /* per course object: its instance in item_im[type], -1 for trees and reset points */
  int item_count;
  cw_body3d statics;
  t3_vec3 poly[16];
  int poly_count;
  t3_scene *scene;
} O;

static bool night_mode; /* course_set_night: no trunk prints under moonlight */

/* t3_instanced_mesh_set_matrix_at only writes the array: the buffer is
 * re-uploaded when the attribute's version moves (three.js's needsUpdate) */
static void instances_changed(t3_instanced_mesh *im) { if (im && im->instance_matrix) im->instance_matrix->version++; }

static t3_material *material(int type) {
  if (O.mat[type] || !OBJ_TEXTURE[type]) return O.mat[type];
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  O.tex[type] = cw_res_load(OBJ_TEXTURE[type], CW_RES_TEXTURE);
  t3_texture *t = cw_res_texture(O.tex[type]);
  if (t) t3_material_set_map(m, t);
  m->alpha_test = 0.5f;
  m->side = T3_DOUBLE_SIDE;
  m->roughness = 0.9f;
  m->metalness = 0;
  O.mat[type] = m;
  return m;
}

static void load_poly(void) {
  if (O.poly_count) return;
  size_t len;
  char *txt = cw_asset_load("model/tree_poly.txt", &len);
  if (!txt) { cw_error("model/tree_poly.txt missing"); return; }
  char *save = NULL;
  for (char *line = strtok_r(txt, "\n", &save); line && O.poly_count < 16; line = strtok_r(NULL, "\n", &save)) {
    t3_vec3 v;
    if (sscanf(line, "%f %f %f", &v.x, &v.y, &v.z) == 3) O.poly[O.poly_count++] = v;
  }
  free(txt);
}

/* the "foliage" part's values (GPU wind) for every draw of the leaf material */
static void foliage_fill(t3_extension_ctx *ctx, void *u) {
  (void)u;
  t3_extension_set_uniform(ctx, "forestTime", &O.wind_time, 1);
  t3_extension_set_uniform(ctx, "forestWind", &O.wind_scale, 1);
}

static void flag_fill(t3_extension_ctx *ctx, void *u) {
  (void)u;
  t3_extension_set_uniform(ctx, "flagTime", &O.item_time, 1);
  t3_extension_set_uniform(ctx, "flagWind", &O.wind_scale, 1);
}

void course_trees_renderer(t3_renderer *r) {
  if (!r || r == O.renderer) return;
  O.renderer = r;
  cw_shading_part(r, "foliage", foliage_fill, NULL);
  cw_shading_part_batching(r, "foliage", true);
  cw_shading_part(r, "flag", flag_fill, NULL);
  cw_shading_part_batching(r, "flag", true);
}

/* O.tree_geo: the conifers generated by tree_mesh.c ([lod][variant][part] -> lod * 4 + variant * 2 + part),
 * then trees.bin's leafless birch ([lod][variant] -> 8 + lod * 2 + variant) */
enum { TM_CONIFER = 0, TM_BARREN = 8 };
#define CONIFER_WIDTH 0.6f /* the spruce model's diameter at unit height; the birch's */
#define BARREN_WIDTH 0.25f

static bool load_tree_meshes(void) {
  if (O.tree_geo[0]) return true;
  size_t len;
  uint8_t *f = cw_asset_load("model/trees.bin", &len);
  if (!f || len < 8 || memcmp(f, "TRE1", 4)) { cw_error("model/trees.bin missing or not TRE1"); free(f); return false; }
  O.tree_file = f;
  uint32_t count;
  memcpy(&count, f + 4, 4);
  size_t at = 8;
  for (uint32_t m = 0; m < count && m < 4; m++) {
    uint32_t verts, idx;
    if (at + 8 > len) break;
    memcpy(&verts, f + at, 4), memcpy(&idx, f + at + 4, 4);
    at += 8;
    size_t need = (size_t)verts * 32 + (size_t)idx * 4;
    if (at + need > len) { cw_error("model/trees.bin truncated at mesh %u", m); break; }
    const float *pos = (const float *)(f + at), *nrm = pos + verts * 3, *uv = nrm + verts * 3;
    const uint32_t *ix = (const uint32_t *)(uv + verts * 2);
    t3_geometry *g = t3_geometry_new();
    t3_attribute *ap = t3_attribute_new(T3_FLOAT32, pos, (int)verts, 3), *an = t3_attribute_new(T3_FLOAT32, nrm, (int)verts, 3),
                 *au = t3_attribute_new(T3_FLOAT32, uv, (int)verts, 2), *ai = t3_attribute_new(T3_UINT32, ix, (int)idx, 1);
    t3_geometry_set_attribute(g, T3_ATTR_POSITION, ap);
    t3_geometry_set_attribute(g, T3_ATTR_NORMAL, an);
    t3_geometry_set_attribute(g, T3_ATTR_UV, au);
    t3_geometry_set_index(g, ai);
    t3_release(ap), t3_release(an), t3_release(au), t3_release(ai);
    O.tree_geo[TM_BARREN + m] = g;
    at += need;
  }
  if (!O.tree_geo[TM_BARREN + 3]) { cw_error("model/trees.bin: 4 meshes expected"); return false; }
  for (int lod = 0; lod < 2; lod++)
    for (int v = 0; v < 2; v++) {
      O.tree_geo[TM_CONIFER + lod * 4 + v * 2] = tree_mesh_trunk(lod);
      O.tree_geo[TM_CONIFER + lod * 4 + v * 2 + 1] = tree_mesh_spruce(v, lod);
    }
  static cw_res tex[2];
  tex[0] = cw_res_load("textures/tree-bark-v1.jpg", CW_RES_TEXTURE);
  tex[1] = cw_res_load("textures/tree-snow-v1.jpg", CW_RES_TEXTURE);
  t3_texture *bark = cw_res_texture(tex[0]), *snow = cw_res_texture(tex[1]);
  if (bark) bark->wrap_s = bark->wrap_t = T3_REPEAT, bark->anisotropy = 8;
  if (snow) snow->wrap_s = snow->wrap_t = T3_REPEAT, snow->anisotropy = 8;
  O.bark_mat = t3_mesh_standard_material_new(0x8b8171);
  O.bark_mat->roughness = 0.94f;
  O.bark_mat->metalness = 0;
  if (bark) t3_material_set_map(O.bark_mat, bark);
  O.leaf_mat = t3_mesh_standard_material_new(0xffffff);
  O.leaf_mat->roughness = 0.9f;
  O.leaf_mat->metalness = 0;
  if (snow) t3_material_set_map(O.leaf_mat, snow);
  cw_shading_add(O.leaf_mat, "foliage");
  static cw_res sprig_res;
  sprig_res = cw_res_load("textures/sprig.png", CW_RES_TEXTURE);
  t3_texture *sprig = cw_res_texture(sprig_res);
  O.card_mat = t3_mesh_standard_material_new(0xffffff);
  if (sprig) t3_material_set_map(O.card_mat, sprig);
  O.card_mat->alpha_test = 0.5f;
  O.card_mat->side = T3_DOUBLE_SIDE;
  O.card_mat->roughness = 0.9f;
  O.card_mat->metalness = 0;
  cw_shading_add(O.card_mat, "foliage");
  for (int lod = 0; lod < 2; lod++)
    for (int v = 0; v < 2; v++) O.tree_cards[lod][v] = tree_mesh_cards(v, lod);
  return true;
}

static t3_instanced_mesh *solid_instances(t3_geometry *g, t3_material *m, int count, bool casts) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, count);
  im->mesh.base.cast_shadow = casts && !getenv("TUXRACER_NO_TREE_SHADOWS");
  im->mesh.base.receive_shadow = true;
  im->mesh.base.frustum_culled = false; /* the instances span the course */
  im->count = 0;
  t3_object_add((t3_object *)O.scene, im);
  return im;
}

/* a solid tree's matrix: the model is unit height with its base at y = 0 */
static void solid_matrix(t3_mat4 *m, const course_object *o, int i, bool conifer) {
  float yaw = (float)((i * 2654435761u) & 4095) / 4096.0f * 6.2831853f;
  float w = o->diam / (conifer ? CONIFER_WIDTH : BARREN_WIDTH);
  t3_mat4_compose(m, (t3_vec3){ o->x, o->y, o->z }, t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, yaw), (t3_vec3){ w, o->height, w });
}

/* every solid tree into its (lod, variant) instanced mesh */
static void solid_rebuild(const course *c) {
  int used[2][2][2] = { { { 0 } } }; /* [kind][lod][variant] */
  for (int i = 0; i < c->object_count; i++) {
    const course_object *o = &c->objects[i];
    int n = O.solid_slot[i];
    if (n < 0) continue;
    bool conifer = o->type == OBJ_TREE1;
    int lod = O.solid_lod[n], v = i & 1;
    int at = used[conifer ? 0 : 1][lod][v]++;
    t3_mat4 m;
    solid_matrix(&m, o, i, conifer);
    if (conifer) {
      for (int part = 0; part < 3; part++) t3_instanced_mesh_set_matrix_at(O.conifer[lod][v][part], at, &m);
    } else t3_instanced_mesh_set_matrix_at(O.barren[lod][v], at, &m);
  }
  for (int lod = 0; lod < 2; lod++)
    for (int v = 0; v < 2; v++) {
      for (int part = 0; part < 3; part++)
        if (O.conifer[lod][v][part]) { O.conifer[lod][v][part]->count = used[0][lod][v]; instances_changed(O.conifer[lod][v][part]); }
      if (O.barren[lod][v]) { O.barren[lod][v]->count = used[1][lod][v]; instances_changed(O.barren[lod][v]); }
    }
}

void course_build_objects(course *c, cw_tree *tree) {
  load_poly();
  O.scene = cw_render3d_scene(tree);
  if (!O.quad) {
    O.quad = t3_plane_geometry_new(1, 1, 1, 1);
    t3_mat4 up;
    t3_mat4_compose(&up, (t3_vec3){ 0, 0.5f, 0 }, t3_quat_identity(), (t3_vec3){ 1, 1, 1 });
    t3_geometry_apply_matrix4(O.quad, &up);
    /* the engine's textures keep their rows as loaded (no flipY): v runs down the image */
    /* every vertex normal straight up: the cards shade like the ground they stand on instead of
     * flipping dark when seen edge-on (the original lit them toward the eye) */
    t3_attribute *nrm = O.quad->attributes[T3_ATTR_NORMAL];
    if (nrm && nrm->type == T3_FLOAT32) { float *f = nrm->array; for (int i = 0; i < nrm->count; i++) { f[i * 3] = 0; f[i * 3 + 1] = 1; f[i * 3 + 2] = 0; } nrm->version++; }
    t3_attribute *uv = O.quad->attributes[T3_ATTR_UV];
    if (uv && uv->type == T3_FLOAT32) { float *f = uv->array; for (int i = 0; i < uv->count; i++) f[i * 2 + 1] = 1 - f[i * 2 + 1]; uv->version++; }
  }
  int per_type[3] = { 0, 0, 0 };
  for (int i = 0; i < c->object_count; i++)
    if (c->objects[i].type <= OBJ_TREE2) per_type[c->objects[i].type]++;
  cw_world3d world = cw_physics3d_world(tree);
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  bd.name = "course objects";
  O.statics = cw_body3d_create(world, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.material.friction = 0.3f;
  sd.material.restitution = 0.35f;
  sd.user = (void *)1; /* a tree */
  t3_quat turned = t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, TR_DEG(90));
  bool solid = (per_type[OBJ_TREE1] || per_type[OBJ_TREE2]) && load_tree_meshes();
  if (solid) {
    int nc = per_type[OBJ_TREE1], nb = per_type[OBJ_TREE2];
    for (int lod = 0; lod < 2; lod++)
      for (int v = 0; v < 2; v++) {
        if (nc) {
          for (int part = 0; part < 2; part++) O.conifer[lod][v][part] = solid_instances(O.tree_geo[TM_CONIFER + lod * 4 + v * 2 + part], part ? O.leaf_mat : O.bark_mat, nc, lod == 0);
          O.conifer[lod][v][2] = solid_instances(O.tree_cards[lod][v], O.card_mat, nc, lod == 0);
        }
        if (nb) O.barren[lod][v] = solid_instances(O.tree_geo[TM_BARREN + lod * 2 + v], O.bark_mat, nb, lod == 0);
      }
  }
  for (int type = 0; type < 3; type++) {
    if (!per_type[type] || (solid && type != OBJ_TREE3)) continue;
    for (int k = 0; k < 2; k++) {
      t3_instanced_mesh *im = t3_instanced_mesh_new(O.quad, material(type), per_type[type]);
      im->mesh.base.cast_shadow = k == 0 && !getenv("TUXRACER_NO_TREE_SHADOWS"); /* one card casts: half the shadow work */
      im->mesh.base.receive_shadow = true;
      im->mesh.base.frustum_culled = false;
      t3_object_add((t3_object *)O.scene, im);
      O.trees[type][k] = im;
    }
  }
  int filled[3] = { 0, 0, 0 };
  O.tree_count = per_type[0] + per_type[1] + per_type[2];
  if (O.tree_count && getenv("TUXRACER_TRUNK_PRINTS")) { /* off: multiply-blended plates read as pale squares under the trees */
    if (!O.flat) {
      O.flat = t3_plane_geometry_new(1, 1, 1, 1);
      t3_mat4 down;
      t3_mat4_compose(&down, (t3_vec3){ 0, 0, 0 }, t3_quat_from_axis_angle((t3_vec3){ 1, 0, 0 }, TR_DEG(-90)), (t3_vec3){ 1, 1, 1 });
      t3_geometry_apply_matrix4(O.flat, &down);
      t3_attribute *uv = O.flat->attributes[T3_ATTR_UV];
      if (uv && uv->type == T3_FLOAT32) { float *f = uv->array; for (int i = 0; i < uv->count; i++) f[i * 2 + 1] = 1 - f[i * 2 + 1]; uv->version++; }
    }
    if (!O.splat_mat) {
      O.splat_mat = t3_mesh_standard_material_new(0xffffff);
      cw_res r = cw_res_load("original/common/buttprint.png", CW_RES_TEXTURE);
      t3_texture *t = cw_res_texture(r);
      if (t) t3_material_set_map(O.splat_mat, t);
      O.splat_mat->transparent = true;
      O.splat_mat->depth_write = false;
      O.splat_mat->blending = T3_MULTIPLY_BLENDING; /* darkens the snow by the print: trodden ground */
      O.splat_mat->color = (t3_color){ 0.78f, 0.82f, 0.9f };
      O.splat_mat->roughness = 1;
    }
    O.splats = t3_instanced_mesh_new(O.flat, O.splat_mat, O.tree_count);
    O.splats->mesh.base.cast_shadow = false;
    O.splats->mesh.base.receive_shadow = false;
    O.splats->mesh.base.frustum_culled = false;
    O.splats->mesh.base.render_order = 1;
    O.splats->mesh.base.visible = !night_mode;
    t3_object_add((t3_object *)O.scene, O.splats);
  }
  int splat_n = 0;
  O.tree_phase = cw_calloc((size_t)(O.tree_count ? O.tree_count : 1), sizeof *O.tree_phase, CW_MEM_GAME);
  O.item_count = c->object_count;
  O.item_slot = cw_calloc((size_t)(c->object_count ? c->object_count : 1), sizeof *O.item_slot, CW_MEM_GAME);
  O.solid_slot = cw_calloc((size_t)(c->object_count ? c->object_count : 1), sizeof *O.solid_slot, CW_MEM_GAME);
  O.solid_lod = cw_calloc((size_t)(O.tree_count ? O.tree_count : 1), sizeof *O.solid_lod, CW_MEM_GAME);
  O.solid_count = 0;
  int per_kind[OBJ_KINDS] = { 0 }, kind_fill[OBJ_KINDS] = { 0 };
  for (int i = 0; i < c->object_count; i++) {
    O.item_slot[i] = -1;
    O.solid_slot[i] = -1;
    if (c->objects[i].type > OBJ_TREE2 && OBJ_TEXTURE[c->objects[i].type]) per_kind[c->objects[i].type]++;
  }
  bool fish_card = getenv("TUXRACER_FISH_CARD") != NULL;
  if (!O.fish) O.fish = fish_card ? item_mesh_fish_card() : item_mesh_fish();
  if (!O.flag_card) O.flag_card = item_mesh_flag();
  if (!O.fish_mat) { /* the herring's scales: colour from the vertices, a wet shine, a clearcoat */
    O.fish_mat = t3_mesh_physical_material_new(0xffffff);
    O.fish_mat->vertex_colors = true;
    O.fish_mat->roughness = 0.32f;
    O.fish_mat->metalness = 0.3f;
    O.fish_mat->clearcoat = 0.5f;
    O.fish_mat->clearcoat_roughness = 0.2f;
    O.fish_mat->emissive = (t3_color){ 0.9f, 0.6f, 0.15f }; /* a warm glow: the collectible reads from afar */
    O.fish_mat->emissive_intensity = 0.35f;
    { /* v0.19 rim light: a gold outline along the fish that reads 30 m down the course, faint at arm's length */
      cw_rim_light_desc rd = cw_rim_light_default();
      rd.color = (t3_color){ 1.0f, 0.72f, 0.22f };
      rd.strength = 1.6f, rd.power = 2.5f, rd.near = 5, rd.far = 30, rd.close = 0.15f;
      cw_rim_light_apply(O.fish_mat, &rd);
    }
    O.fish_mat->iridescence = 0.7f; /* scales shimmer as it turns */
    O.fish_mat->iridescence_ior = 1.4f;
    O.fish_mat->iridescence_thickness_range[0] = 150, O.fish_mat->iridescence_thickness_range[1] = 420;
    /* (no anisotropy: it needs tangents, and without them the fish became a screen-sized black triangle) */
  }
  if (!O.gate_poles) O.gate_poles = item_mesh_gate_poles();
  if (!O.gate_banner) O.gate_banner = item_mesh_gate_banner();
  if (!O.pole_mat) { O.pole_mat = t3_mesh_standard_material_new(0x5a6478); O.pole_mat->roughness = 0.45f; O.pole_mat->metalness = 0.7f; }
  for (int i = 0; i < c->object_count; i++) { /* the gates: one mesh each, not an item card */
    course_object *o = &c->objects[i];
    if (o->type != OBJ_START && o->type != OBJ_FINISH) continue;
    int g = o->type == OBJ_FINISH;
    if (O.gate[g]) continue;
    t3_object *grp = t3_group_new();
    t3_mesh *poles = t3_mesh_new(O.gate_poles, O.pole_mat);
    poles->base.cast_shadow = poles->base.receive_shadow = true;
    t3_object_add(grp, poles);
    t3_release(poles);
    t3_material *bm = material(o->type);
    bm->side = T3_DOUBLE_SIDE;
    t3_mesh *banner = t3_mesh_new(O.gate_banner, bm);
    banner->base.cast_shadow = banner->base.receive_shadow = true;
    t3_object_add(grp, banner);
    t3_release(banner);
    /* the start gate stands 5 m down the hill from the start point: the camera begins behind Tux, inside
     * the gate's pole when the gate is on the line itself, and this way he slides under the banner */
    float gz = o->z + (o->type == OBJ_START ? -5.0f : 0.0f);
    t3_object_set_position(grp, o->x, o->type == OBJ_START ? course_height(c, o->x, gz) : o->y, gz);
    t3_object_set_scale(grp, o->diam / 9.0f, o->height / 6.0f, 1);
    t3_object_add((t3_object *)O.scene, grp);
    O.gate[g] = grp;
    per_kind[o->type]--; /* no card for it */
  }
  for (int kind = OBJ_TREE2 + 1; kind < OBJ_KINDS; kind++) {
    if (!per_kind[kind] || !OBJ_TEXTURE[kind]) continue;
    t3_material *mat = kind == OBJ_HERRING && !fish_card ? O.fish_mat : material(kind);
    if (kind == OBJ_HERRING && fish_card) { mat->roughness = 0.35f; mat->metalness = 0.05f; }
    if (kind == OBJ_FLAG && !cw_shading_has(mat, "flag")) cw_shading_add(mat, "flag");
    t3_instanced_mesh *im = t3_instanced_mesh_new(kind == OBJ_HERRING ? O.fish : kind == OBJ_FLAG ? O.flag_card : O.quad, mat, per_kind[kind]);
    im->mesh.base.cast_shadow = kind == OBJ_HERRING;
    im->mesh.base.receive_shadow = true;
    im->mesh.base.frustum_culled = false; /* the instances span the course */
    t3_object_add((t3_object *)O.scene, im);
    O.item_im[kind] = im;
  }
  for (int i = 0; i < c->object_count; i++) {
    course_object *o = &c->objects[i];
    t3_vec3 pos = { o->x, o->y, o->z };
    if (o->type <= OBJ_TREE2) {
      int n = filled[o->type]++;
      O.tree_phase[n + (o->type == OBJ_TREE1 ? per_type[0] : o->type == OBJ_TREE2 ? per_type[0] + per_type[1] : 0)] = (float)((i * 2654435761u) & 1023) / 1024.0f * 6.2831853f;
      if (O.trees[o->type][0]) {
        t3_mat4 m;
        t3_mat4_compose(&m, pos, t3_quat_identity(), (t3_vec3){ o->diam, o->height, o->diam });
        t3_instanced_mesh_set_matrix_at(O.trees[o->type][0], n, &m);
        t3_mat4_compose(&m, pos, turned, (t3_vec3){ o->diam, o->height, o->diam });
        t3_instanced_mesh_set_matrix_at(O.trees[o->type][1], n, &m);
      } else {
        O.solid_slot[i] = O.solid_count;
        O.solid_lod[O.solid_count++] = 1; /* far until the first LOD pass */
      }
      /* the snow print under the trunk: laid on the slope (a flat quad on a hill stuck out of the
       * downhill side as a floating sheet), small, just above the surface */
      t3_mat4 sm;
      t3_vec3 nrm = course_normal(c, pos.x, pos.z);
      t3_quat lay = t3_quat_multiply(tr_rotation_between((t3_vec3){ 0, 1, 0 }, nrm),
                                     t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, (float)((i * 2654435761u) & 255) / 255.0f * 6.2831853f));
      t3_vec3 at = t3_vec3_add((t3_vec3){ pos.x, course_height(c, pos.x, pos.z), pos.z }, t3_vec3_scale(nrm, 0.02f));
      t3_mat4_compose(&sm, at, lay, (t3_vec3){ o->diam * 1.15f, 1, o->diam * 1.15f });
      if (O.splats) t3_instanced_mesh_set_matrix_at(O.splats, splat_n++, &sm);
      /* the polyhedron scaled (diam, height, diam) at the tree, as a hull on the static body */
      t3_vec3 pts[16];
      for (int k = 0; k < O.poly_count; k++) pts[k] = (t3_vec3){ pos.x + O.poly[k].x * o->diam, pos.y + O.poly[k].y * o->height, pos.z + O.poly[k].z * o->diam };
      if (O.poly_count >= 4 && !getenv("TUXRACER_NO_TREE_HULLS")) cw_shape3d_hull(O.statics, &sd, pts, O.poly_count);
    } else if (OBJ_TEXTURE[o->type] && O.item_im[o->type] && o->type != OBJ_START && o->type != OBJ_FINISH) {
      int slot = kind_fill[o->type]++;
      O.item_slot[i] = slot;
      t3_mat4 m;
      t3_mat4_compose(&m, pos, t3_quat_identity(), (t3_vec3){ o->diam, o->height, 1 });
      t3_instanced_mesh_set_matrix_at(O.item_im[o->type], slot, &m);
    }
  }
  for (int t = 0; t < 3; t++) for (int k = 0; k < 2; k++) instances_changed(O.trees[t][k]);
  instances_changed(O.splats);
  for (int k = 0; k < OBJ_KINDS; k++) instances_changed(O.item_im[k]);
  O.lod_dirty = true;
  O.wind_scale = 1;
  if (O.solid_count) solid_rebuild(c);
  cw_info("course objects: trees %d/%d/%d (%d solid), %d items", per_type[0], per_type[1], per_type[2], O.solid_count, c->object_count - per_type[0] - per_type[1] - per_type[2]);
}

/* per tick: the solid trees' wind clock and LOD (near meshes within LOD_NEAR of the eye, with
 * hysteresis), and a gentle CPU sway of the shrub cards about their base (render only) */
#define LOD_NEAR 70.0f
#define LOD_FAR 85.0f
void course_trees_update(course *c, float dt, bool windy, t3_vec3 eye) {
  O.wind_time += dt;
  O.wind_scale = windy ? 2.6f : 1.0f;
  if (O.solid_count && ++O.lod_tick >= 10) {
    O.lod_tick = 0;
    for (int i = 0; i < c->object_count; i++) {
      int n = O.solid_slot[i];
      if (n < 0) continue;
      float dx = c->objects[i].x - eye.x, dz = c->objects[i].z - eye.z, d2 = dx * dx + dz * dz;
      uint8_t lod = O.solid_lod[n];
      if (lod && d2 < LOD_NEAR * LOD_NEAR) lod = 0;
      else if (!lod && d2 > LOD_FAR * LOD_FAR) lod = 1;
      if (lod != O.solid_lod[n]) O.solid_lod[n] = lod, O.lod_dirty = true;
    }
    if (O.lod_dirty) { solid_rebuild(c); O.lod_dirty = false; }
  }
  O.sway_t += dt;
  int idx[3] = { 0, 0, 0 }, base[3] = { 0, 0, 0 };
  int per_type[3] = { 0, 0, 0 };
  for (int i = 0; i < c->object_count; i++) if (c->objects[i].type <= OBJ_TREE2) per_type[c->objects[i].type]++;
  base[1] = per_type[0], base[2] = per_type[0] + per_type[1];
  float amp = windy ? 0.045f : 0.018f;
  t3_quat turned = t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, TR_DEG(90));
  for (int i = 0; i < c->object_count; i++) {
    course_object *o = &c->objects[i];
    if (o->type > OBJ_TREE2 || !O.trees[o->type][0]) continue;
    int n = idx[o->type]++;
    float ph = O.tree_phase[base[o->type] + n];
    float ax = cw_sin(O.sway_t * 1.1f + ph) * amp, az = cw_sin(O.sway_t * 0.7f + ph * 1.7f) * amp * 0.6f;
    t3_quat lean = t3_quat_multiply(t3_quat_from_axis_angle((t3_vec3){ 1, 0, 0 }, ax), t3_quat_from_axis_angle((t3_vec3){ 0, 0, 1 }, az));
    t3_vec3 pos = { o->x, o->y, o->z };
    t3_mat4 m;
    t3_mat4_compose(&m, pos, lean, (t3_vec3){ o->diam, o->height, o->diam });
    t3_instanced_mesh_set_matrix_at(O.trees[o->type][0], n, &m);
    t3_mat4_compose(&m, pos, t3_quat_multiply(lean, turned), (t3_vec3){ o->diam, o->height, o->diam });
    t3_instanced_mesh_set_matrix_at(O.trees[o->type][1], n, &m);
  }
  for (int t = 0; t < 3; t++) for (int k = 0; k < 2; k++) instances_changed(O.trees[t][k]);
}

/* flags face the camera (about y); the herring, a solid now, turn slowly and bob; banners keep their normal (0, 0, 1) */
void course_update_billboards(course *c, t3_vec3 eye) {
  O.item_time += 1.0f / 60.0f;
  for (int i = 0; i < c->object_count; i++) {
    course_object *o = &c->objects[i];
    int slot = O.item_slot[i];
    if (slot < 0 || o->collected || o->type == OBJ_FINISH || o->type == OBJ_START) continue;
    float yaw, bob = 0;
    if (o->type == OBJ_HERRING) {
      float ph = (float)((i * 2654435761u) & 255) / 255.0f * 6.2831853f;
      yaw = O.item_time * 1.6f + ph;
      bob = cw_sin(O.item_time * 2.4f + ph) * 0.05f + 0.05f;
    } else yaw = atan2f(eye.x - o->x, eye.z - o->z);
    t3_mat4 m;
    t3_mat4_compose(&m, (t3_vec3){ o->x, o->y + bob, o->z }, t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, yaw), (t3_vec3){ o->diam, o->height, o->type == OBJ_HERRING ? o->diam : 1 });
    t3_instanced_mesh_set_matrix_at(O.item_im[o->type], slot, &m);
  }
  for (int k = OBJ_TREE2 + 1; k < OBJ_KINDS; k++) instances_changed(O.item_im[k]);
}

int course_collect(course *c, t3_vec3 pos) {
  int got = 0;
  for (int i = 0; i < c->object_count; i++) {
    course_object *o = &c->objects[i];
    if (o->type != OBJ_HERRING || o->collected) continue;
    float dx = o->x - pos.x, dz = o->z - pos.z, r = o->diam / 2 + 0.6f;
    if (dx * dx + dz * dz > r * r) continue;
    float lo = pos.y - 0.6f, hi = pos.y + 0.6f, top = o->y + o->height;
    if ((lo >= o->y && lo <= top) || (hi >= o->y && hi <= top) || (lo <= o->y && hi >= top)) {
      o->collected = true;
      if (O.item_slot[i] >= 0) { /* gone: a zero-size instance */
        t3_mat4 m;
        t3_mat4_compose(&m, (t3_vec3){ o->x, o->y, o->z }, t3_quat_identity(), (t3_vec3){ 0, 0, 0 });
        t3_instanced_mesh_set_matrix_at(O.item_im[o->type], O.item_slot[i], &m);
        instances_changed(O.item_im[o->type]);
      }
      got++;
    }
  }
  return got;
}

t3_vec3 course_reset_point(const course *c, t3_vec3 pos) {
  int best = -1;
  for (int i = 0; i < c->object_count; i++) {
    const course_object *o = &c->objects[i];
    if (o->type != OBJ_FLOAT || o->z <= pos.z) continue;
    if (best < 0 || o->z < c->objects[best].z) best = i;
  }
  t3_vec3 r;
  if (best < 0) r = (t3_vec3){ c->width / 2, 0, fminf(pos.z + 10, -1) };
  else r = (t3_vec3){ c->objects[best].x, 0, c->objects[best].z };
  r.y = course_height(c, r.x, r.z);
  return r;
}

bool course_finished(const course *c, t3_vec3 pos) { return pos.z < -c->play_length; }

/* soft card edges: alpha becomes sample coverage on a multisampled scene pass (the "Tree edges" lever) */
/* perf triage (the hide debug command): show or hide one group of objects */
void course_objects_show(const char *what, bool on) {
  bool all = !strcmp(what, "all");
  if (all || !strcmp(what, "trees")) {
    for (int t = 0; t < 3; t++) for (int k = 0; k < 2; k++) if (O.trees[t][k]) O.trees[t][k]->mesh.base.visible = on;
    for (int lod = 0; lod < 2; lod++) for (int v = 0; v < 2; v++) {
      for (int part = 0; part < 3; part++) if (O.conifer[lod][v][part]) O.conifer[lod][v][part]->mesh.base.visible = on;
      if (O.barren[lod][v]) O.barren[lod][v]->mesh.base.visible = on;
    }
  }
  if ((all || !strcmp(what, "splats")) && O.splats) O.splats->mesh.base.visible = on;
  if (all || !strcmp(what, "items")) {
    for (int k = 0; k < OBJ_KINDS; k++) if (O.item_im[k]) O.item_im[k]->mesh.base.visible = on;
    for (int g = 0; g < 2; g++) if (O.gate[g]) O.gate[g]->visible = on;
  }
}

/* the trodden-snow prints under the trunks multiply the ground: by day a soft 0.8, under moonlight the
 * same factor cuts a dim blue snow to dark blue facets, so at night they all but vanish */
void course_set_night(bool night) {
  night_mode = night;
  if (O.splats) O.splats->mesh.base.visible = !night; /* multiply blending cannot be softened: the print texture is dark */
}

void course_set_tree_coverage(bool on) {
  for (int t = 0; t < 3; t++) if (O.mat[t]) O.mat[t]->alpha_to_coverage = on;
  if (O.card_mat) O.card_mat->alpha_to_coverage = on;
}

void course_destroy_objects(course *c) {
  (void)c;
  for (int type = 0; type < 3; type++)
    for (int k = 0; k < 2; k++)
      if (O.trees[type][k]) { t3_object_remove((t3_object *)O.scene, O.trees[type][k]); O.trees[type][k] = NULL; }
  for (int k = 0; k < OBJ_KINDS; k++)
    if (O.item_im[k]) { t3_object_remove((t3_object *)O.scene, O.item_im[k]); O.item_im[k] = NULL; }
  for (int g = 0; g < 2; g++)
    if (O.gate[g]) { t3_object_remove((t3_object *)O.scene, O.gate[g]); O.gate[g] = NULL; }
  for (int lod = 0; lod < 2; lod++)
    for (int v = 0; v < 2; v++) {
      for (int part = 0; part < 3; part++)
        if (O.conifer[lod][v][part]) { t3_object_remove((t3_object *)O.scene, O.conifer[lod][v][part]); O.conifer[lod][v][part] = NULL; }
      if (O.barren[lod][v]) { t3_object_remove((t3_object *)O.scene, O.barren[lod][v]); O.barren[lod][v] = NULL; }
    }
  cw_free(O.solid_slot);
  O.solid_slot = NULL;
  cw_free(O.solid_lod);
  O.solid_lod = NULL;
  O.solid_count = 0;
  cw_free(O.item_slot);
  O.item_slot = NULL;
  O.item_count = 0;
  cw_free(O.tree_phase);
  O.tree_phase = NULL;
  O.tree_count = 0;
  if (O.splats) { t3_object_remove((t3_object *)O.scene, O.splats); O.splats = NULL; }
  if (cw_body3d_valid(O.statics)) cw_body3d_destroy(O.statics);
  O.statics = (cw_body3d){ 0 };
}
