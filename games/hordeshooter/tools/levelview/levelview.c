/* hordeshooter_levelview: the descent's levels and creatures as the engine draws them, for review (in
 * romdev, as a cart) and for baking each level's lightmap (native). It builds a level with
 * levelbuild.c, the builder the game links, so a bake made here applies in the game.
 *
 * Input (the game's actions): left / right the previous or next level; up / down the camera (the game's
 * view, a wide view, a low view); fire cycles what stands on the floor (nothing, the level's crowd,
 * its boss going through its clips); bomb opens and shuts the hatch.
 *
 * Native, development: --level <id> (or LEVEL=<id>) starts on that level; --bake <dir> (or LEVEL_BAKE=<dir>)
 * bakes every level (or only that one) into <dir>/<id>.cwlm and exits. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/project.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/vfs.h"
#include "levelbuild.h"

static const char *LEVELS[] = { "sluice", "ossuary", "foundry", "grotto", "cathedral", "rift", "core" };
enum { NLEVELS = (int)(sizeof LEVELS / sizeof LEVELS[0]), CROWD = 14 };

typedef struct chr { int nclip, nf[4], first[4], total; t3_geometry *geo[24]; } chr;

static struct {
  int level, view, show, frames, anim;
  bool hatch_open, built, baking, lm_done;
  float hatch_t;
  lb_level lv;
  lb_built b;
  cw_node root, camera, stage;
  cw_action left, right, up, down, fire, bomb;
  chr mon, boss;
  t3_mesh *mons[CROWD], *bossm;
  const char *bake_dir, *want;
} V;

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static t3_color lin1(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}
/* a creature's group 0, every frame (the glows are its emissive vertex colours) */
static bool chr_load(chr *c, const char *path) {
  memset(c, 0, sizeof *c);
  size_t len = 0;
  uint8_t *buf = cw_vfs_read(path, &len);
  if (!buf || len < 16 || memcmp(buf, "CWCH", 4) != 0) {
    cw_error("levelview: cannot read %s", path);
    free(buf);
    return false;
  }
  c->nclip = CW_MIN((int)rd32(buf + 8), 4);
  const uint8_t *p = buf + 16;
  for (int k = 0; k < (int)rd32(buf + 8); k++, p += 4)
    if (k < 4) c->nf[k] = (int)rd32(p), c->first[k] = c->total, c->total += c->nf[k];
  c->total = CW_MIN(c->total, 24);
  int nv = (int)rd32(p), nt = (int)rd32(p + 4);
  p += 8;
  const uint8_t *uv = p, *col = p + 8 * nv, *idx = p + 20 * nv;
  p += 20 * (size_t)nv + (((size_t)nt * 6 + 3) & ~(size_t)3);
  float *cl = malloc(sizeof(float) * 3 * (size_t)nv);
  for (int v = 0; v < nv; v++) {
    float rgb[3];
    memcpy(rgb, col + 12 * v, 12);
    bool emi = rgb[0] > 1.5f || rgb[1] > 1.5f || rgb[2] > 1.5f;
    t3_color l = emi ? (t3_color){ rgb[0], rgb[1], rgb[2] } : lin1(rgb[0], rgb[1], rgb[2]);
    cl[v * 3] = l.r, cl[v * 3 + 1] = l.g, cl[v * 3 + 2] = l.b;
  }
  for (int f = 0; f < c->total; f++) {
    t3_geometry *g = t3_geometry_new();
    t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, p, nv, 3));
    t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, p + 12 * (size_t)nv, nv, 3));
    p += 24 * (size_t)nv;
    t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, nv, 2));
    t3_geometry_set_attribute(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, cl, nv, 3));
    t3_geometry_set_index(g, t3_attribute_new(T3_UINT16, idx, nt * 3, 1));
    c->geo[f] = g;
  }
  free(cl);
  free(buf);
  return true;
}
static void chr_free(chr *c) {
  for (int f = 0; f < 24; f++)
    if (c->geo[f]) t3_release(c->geo[f]);
  memset(c, 0, sizeof *c);
}
static void set_frame(t3_mesh *m, t3_geometry *g) {
  if (!m || !g || m->geometry == g) return;
  t3_retain(g);
  t3_release(m->geometry);
  m->geometry = g;
}
static t3_mesh *creature(const chr *c, const char *name, float rough) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->vertex_colors = true;
  m->roughness = rough;
  t3_mesh *mesh = t3_mesh_new(c->geo[0], m);
  mesh->base.cast_shadow = true;
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(V.stage, n);
  cw_mesh3d_set_procedural(n, mesh);
  t3_release(m);
  t3_release(mesh); /* the node holds it */
  return mesh;
}

static void unload(void) {
  if (cw_node_valid(V.stage)) cw_node_free(V.stage);
  V.stage = (cw_node){ 0 };
  memset(V.mons, 0, sizeof V.mons);
  V.bossm = NULL;
  chr_free(&V.mon), chr_free(&V.boss);
  if (V.built) levelbuild_free(&V.b);
  V.built = false;
}

static void load(int k) {
  unload();
  V.level = (k + NLEVELS) % NLEVELS;
  char path[96];
  snprintf(path, sizeof path, "levels/%s.cwlevel", LEVELS[V.level]);
  if (!levelbuild_parse(path, &V.lv)) return;
  if (!levelbuild_build(V.root, &V.lv, NULL, NULL, &V.b)) return;
  V.built = true;
  V.lm_done = false;
  V.frames = 0;
  V.hatch_t = V.hatch_open ? 1 : 0;
  V.stage = cw_node_new("Node3D", "Stage");
  cw_node_add_child(V.root, V.stage);
  if (V.lv.monster[0] && chr_load(&V.mon, V.lv.monster))
    for (int i = 0; i < CROWD; i++) {
      char nm[16];
      snprintf(nm, sizeof nm, "Mon%d", i);
      V.mons[i] = creature(&V.mon, nm, 0.6f);
    }
  if (V.lv.boss[0] && chr_load(&V.boss, V.lv.boss)) V.bossm = creature(&V.boss, "Boss", 0.55f);
  cw_info("levelview: %s (%s): %d props, %d placements, %d lights", V.lv.id, V.lv.name, V.lv.nprop, V.lv.nat, V.lv.nlight);
}

static void init(void *u) {
  (void)u;
  V.root = cw_tree_root(cw_scene_app_tree());
  V.camera = cw_node_get(V.root, "View/Camera");
  cw_project_load_input(); /* the game's actions (project.cwproj) */
  V.left = cw_action_find("move_left"), V.right = cw_action_find("move_right");
  V.up = cw_action_find("move_up"), V.down = cw_action_find("move_down");
  V.fire = cw_action_find("fire"), V.bomb = cw_action_find("bomb");
  int start = 0;
#if !CW_CART
  if (!V.want) V.want = getenv("LEVEL");
  for (int i = 0; V.want && i < NLEVELS; i++)
    if (!strcmp(V.want, LEVELS[i])) start = i;
  if (!V.bake_dir) V.bake_dir = getenv("LEVEL_BAKE");
  V.baking = V.bake_dir && *V.bake_dir;
  if (V.baking) cw_info("levelview: baking into %s", V.bake_dir);
#endif
  V.show = 1;
  load(start);
}

static void place_camera(void) {
  /* 0: the game's camera (a team in the middle, about 38 m across), 1: the whole arena, 2: low and close */
  static const float CAM[3][4] = { { 0, 27, 16, 38 }, { 0, 62, 40, 50 }, { 0, 4.5f, 13, 50 } };
  const float *c = CAM[V.view];
  cw_node3d_set_position(V.camera, (t3_vec3){ c[0], c[1], c[2] });
  cw_node_set(V.camera, "fov", cw_val_float(c[3]));
  cw_node3d_look_at(V.camera, (t3_vec3){ 0, V.view == 2 ? 1.6f : 0, V.view == 2 ? 0 : 1 }, (t3_vec3){ 0, 1, 0 });
}

static void update(void *u, double dt) {
  (void)u;
#if !CW_CART
  if (!V.built && V.baking) { /* a level that is not there yet: on to the next (or done) */
    cw_warn("levelview: %s did not load, not baked", LEVELS[V.level]);
    if (V.want || V.level == NLEVELS - 1) exit(V.want ? 1 : 0);
    load(V.level + 1);
    return;
  }
#endif
  if (!V.built) { /* a level that is not there: still let the viewer move on */
    if (cw_action_just_pressed(0, V.right)) load(V.level + 1);
    else if (cw_action_just_pressed(0, V.left)) load(V.level - 1);
    return;
  }
  V.frames++;
  if (!V.lm_done && V.frames > 1) { /* after a frame: the sky has placed the sun */
    char out[256] = { 0 };
    if (V.baking) snprintf(out, sizeof out, "%s/%s.cwlm", V.bake_dir, LEVELS[V.level]);
    levelbuild_lightmap(cw_scene_app_tree(), &V.b, V.baking ? out : NULL);
    V.lm_done = true;
#if !CW_CART
    if (V.baking) {
      if (V.want || V.level == NLEVELS - 1) exit(0);
      load(V.level + 1);
      return;
    }
#endif
  }
  if (cw_action_just_pressed(0, V.right)) { load(V.level + 1); return; }
  if (cw_action_just_pressed(0, V.left)) { load(V.level - 1); return; }
  if (cw_action_just_pressed(0, V.up)) V.view = (V.view + 1) % 3;
  if (cw_action_just_pressed(0, V.down)) V.view = (V.view + 2) % 3;
  if (cw_action_just_pressed(0, V.fire)) V.show = (V.show + 1) % 3;
  if (cw_action_just_pressed(0, V.bomb)) V.hatch_open = !V.hatch_open;
  V.hatch_t = CW_CLAMP(V.hatch_t + (V.hatch_open ? 1 : -1) * (float)dt * 0.6f, 0.0f, 1.0f);
  levelbuild_hatch(&V.b, V.hatch_t);
  place_camera();
  /* the crowd walks in a ring round the hatch at 7 m, attacking in turn; the boss stands north of it and
   * goes through its clips */
  if (V.show == 1) V.anim++; /* the crowd holds still while hidden: a shot with and without it lines up */
  float t = (float)V.anim / 60.0f;
  for (int i = 0; i < CROWD; i++) {
    t3_mesh *m = V.mons[i];
    if (!m) continue;
    m->base.visible = V.show == 1;
    float a = (float)i * 6.2832f / CROWD + t * 0.18f, r = 7.0f + 1.2f * (float)(i % 3);
    int clip = (i % 4 == 0 && V.mon.nclip > 1) ? 1 : 0;
    int f = V.mon.first[clip] + ((int)(t * (clip ? 3 : 10)) + i) % CW_MAX(V.mon.nf[clip], 1);
    set_frame(m, V.mon.geo[f]);
    m->base.position = (t3_vec3){ cw_cos(a) * r, 0, cw_sin(a) * r };
    { /* lit by the bake as the game lights its crowd: a colour multiplier from the probes */
      t3_color c = levelbuild_light_at(&V.b, m->base.position.x, m->base.position.z);
      t3_material *mm = m->materials[0];
      if (mm) mm->color = c;
    }
    /* walkers face along the ring, attackers face the middle */
    t3_object_set_rotation(&m->base, 0, clip ? atan2f(-cw_cos(a), -cw_sin(a)) : -a, 0);
    m->base.matrix_world_needs_update = true;
  }
  if (V.bossm) {
    t = (float)V.frames / 60.0f;
    V.bossm->base.visible = V.show == 2;
    int clip = ((int)(t / 3.0f)) % CW_MAX(V.boss.nclip, 1);
    int nf = CW_MAX(V.boss.nf[clip], 1);
    int f = V.boss.first[clip] + (clip ? CW_MIN((int)(fmodf(t, 3.0f) * 1.5f), nf - 1) : (int)(t * 8) % nf);
    set_frame(V.bossm, V.boss.geo[f]);
    V.bossm->base.position = (t3_vec3){ 0, 0, -9 };
    if (V.bossm->materials[0]) V.bossm->materials[0]->color = levelbuild_light_at(&V.b, 0, -9);
    V.bossm->base.matrix_world_needs_update = true;
  }
}

static void render(void *u, double alpha) { (void)u, (void)alpha; }

cw_app_desc cw_main(int argc, char **argv) {
  for (int i = 1; i + 1 < argc; i++) {
    if (!strcmp(argv[i], "--bake")) V.bake_dir = argv[++i];
    else if (!strcmp(argv[i], "--level")) V.want = argv[++i];
  }
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .title = "Horde levels", .width = 1280, .height = 720, .debug = true, .init = init, .update = update, .render = render };
  cw_project_apply_app(&app);
  return cw_scene_app("scenes/levelview.cwscene", &app);
}
