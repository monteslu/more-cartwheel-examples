/* Tux Racer on cartwheel: shared state and the module interfaces.
 *
 * Coordinates are the original game's: x across the course (0 at the left
 * edge looking downhill), y up, z = -(distance down the course). A course's
 * Terrain3D is placed so its sample (0, 0) sits at (0, 0, -padded length). */
#ifndef TR_GAME_H
#define TR_GAME_H
#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/physics3d.h"
#include "cartwheel/render2d.h"
#include "cartwheel/scene.h"
#include "cartwheel/terrain.h"
#include "three.h"

#define TICK_HZ 60

enum { SURF_ICE, SURF_ROCK, SURF_SNOW, SURF_KINDS };
/* object_types.json order: trees first (tree3 shrub, tree1 fir, tree2 barren), then items */
enum { OBJ_TREE3, OBJ_TREE1, OBJ_TREE2, OBJ_HERRING, OBJ_FLAG, OBJ_FINISH, OBJ_START, OBJ_FLOAT, OBJ_KINDS };

typedef struct course_object {
  uint8_t type;
  float x, z, y;      /* world; y on the terrain (+ above_ground for items) */
  float height, diam; /* this instance's size (trees vary) */
  bool collected;
} course_object;

typedef struct course {
  char dir[64], title[64], description[256];
  float width, length, play_width, play_length;
  float start_x, start_d, angle, par_time;
  float friction[SURF_KINDS];
  int sx, sz;                 /* samples across and along (quads + 1) */
  float spacing, min_h, max_h;
  uint16_t *heights;          /* sx * sz, rows toward +z */
  uint8_t *types;             /* sx * sz, SURF_* */
  course_object *objects;
  int object_count;
  bool mirrored;
  cw_node node;               /* the Terrain3D */
  cw_terrain *terrain;
} course;

/* ── course.c ─────────────────────────────────────────────────────────── */
bool course_load(course *c, const char *dir, cw_node parent, bool mirrored);
void course_unload(course *c);
float course_height(const course *c, float x, float z);           /* the physics triangles' height */
t3_vec3 course_normal(const course *c, float x, float z);
void course_surface(const course *c, float x, float z, float w[SURF_KINDS]); /* barycentric type weights */
float course_friction(const course *c, float x, float z);

/* ── textures.c ───────────────────────────────────────────────────────── */
void textures_terrain_layers(cw_terrain *t); /* layers SURF_ICE, SURF_ROCK, SURF_SNOW */
void textures_snow_renderer(t3_renderer *r);  /* register the "snow" shading part's hook (before the lighting marks materials) */
void textures_snow_attach(cw_terrain *t);     /* put the part on a course's terrain */
void textures_snow_set(t3_vec3 to_sun, float glint, float clouds, float drift, float ice, float time);
void textures_snow_features(int mask); /* bit 0 glints, 1 surface detail, 2 trail, 3 cloud shadows */
void textures_trail_reset(t3_vec3 at);                        /* the slide trail window around here, empty */
void textures_trail_stamp(t3_vec3 pos, float radius, float depth); /* his belly pressed the snow here (0..1 depth) */
void textures_trail_frame(float strength);                    /* before the draws: upload and the part's values */

#endif

/* ── small math on three.c's types ───────────────────────────────────── */
#define TR_DEG(x) ((x) * 0.017453292519943295f)
#define TR_RAD2DEG(x) ((x) * 57.29577951308232f)
static inline t3_vec3 tr_project_into_plane(t3_vec3 n, t3_vec3 v) { return t3_vec3_sub(v, t3_vec3_scale(n, t3_vec3_dot(v, n))); }
/* Simulation code uses these (cartwheel/math.h trig: the same bits on every
 * host) instead of three.c's libm-based quaternion helpers. */
t3_quat tr_quat_axis_angle(t3_vec3 unit_axis, float radians);
t3_quat tr_slerp(t3_quat a, t3_quat b, float t);
static inline t3_vec3 tr_rotate_about(t3_vec3 v, t3_vec3 axis, float deg) {
  return t3_vec3_apply_quat(v, tr_quat_axis_angle(t3_vec3_normalize(axis), TR_DEG(deg)));
}
/* the rotation taking unit s onto unit t (identity when parallel) */
t3_quat tr_rotation_between(t3_vec3 s, t3_vec3 t);
/* the orientation whose local axes are x, y, z (orthonormal) */
t3_quat tr_quat_from_basis(t3_vec3 x, t3_vec3 y, t3_vec3 z);

/* ── tux.c: the model ────────────────────────────────────────────────── */
enum { J_NECK, J_HEAD, J_TAIL, J_LSHOULDER, J_RSHOULDER, J_LHIP, J_RHIP, J_LKNEE, J_RKNEE, J_LANKLE, J_RANKLE, J_COUNT };
#define TUX_MAX_NODES 128
#define TUX_MAX_MATERIALS 8
typedef struct tux_node {
  int parent, material;
  bool sphere, shadow;
  double base[16], cur[16]; /* GL column-major local matrices */
  t3_object *obj;
} tux_node;
typedef struct tux_model {
  tux_node nodes[TUX_MAX_NODES];
  int count;
  t3_material *mats[TUX_MAX_MATERIALS];
  int mat_count;
  t3_object *root;
  int joint[J_COUNT]; /* node index, -1 when the model lacks it */
  t3_skinned_mesh *skin; /* tux_skin.c: the blended torso / neck / head */
  t3_material *skin_mat;
} tux_model;
typedef struct tux_keyframe { float time, pos[3], yaw, pitch, l_shoulder, r_shoulder, l_hip, r_hip; } tux_keyframe;

bool tux_load(tux_model *m, t3_scene *scene);
bool tux_skin_build(tux_model *m); /* tux_skin.c */
void tux_unload(tux_model *m);
void tux_set_transform(tux_model *m, t3_vec3 pos, t3_quat q);
void tux_set_visible(tux_model *m, bool visible);
void tux_set_cast_shadow(tux_model *m, bool on);
void tux_set_squash(tux_model *m, float squash);
void tux_joint_reset(tux_model *m, int joint);
void tux_joint_rotate(tux_model *m, int joint, char axis, float deg);
/* the sliding pose (phys_sim's adjust_tux_joints) */
void tux_pose_slide(tux_model *m, float turn_fact, bool braking, float paddling_factor, float speed, t3_vec3 local_force, float flap_factor);
int tux_load_walk(tux_keyframe *out, int cap);

/* ── physics.c: the Box3D body and the ported force model ────────────── */
#define TUX_MASS 20.0f
extern float tux_radius; /* the sphere's radius (TUXRACER_RADIUS for experiments) */
#define TUX_RADIUS tux_radius
typedef struct tux_phys {
  cw_world3d world;
  cw_body3d body;
  cw_shape3d shape;
  const course *course;
  /* state read back after each step */
  t3_vec3 pos, vel, surf_nml, net_force;
  t3_quat orientation;
  bool orient_init, airborne, in_tree, tree_hit_now;
  int contact_count;
  float speed, fric_coeff, surf_w[SURF_KINDS], dist_from_surface;
  /* controls (set each tick before the step) */
  float turn_fact, turn_anim;
  bool braking, paddle_held, charge_held;
  bool paddling, jump_charging, jumping, begin_jump;
  float paddle_time, jump_amt, jump_start, time;
  /* wind */
  bool windy;
  float wind_scale;
  cw_rng rng;
  bool enabled;      /* the force model runs (racing); off during menus and the intro */
} tux_phys;
void physics_init(tux_phys *p, cw_tree *tree, const course *c);
void physics_place(tux_phys *p, t3_vec3 pos, bool start_speed); /* teleport; the original's init */
void physics_control(tux_phys *p, float turn, bool brake, bool paddle, bool charge);
void physics_after_step(tux_phys *p, float dt); /* read the body, min speed, orientation */

/* ── view.c: the camera ──────────────────────────────────────────────── */
typedef enum { VIEW_BEHIND, VIEW_FOLLOW, VIEW_ABOVE, VIEW_MODES, VIEW_FIXED } view_mode; /* FIXED: the cam dev command parked it */
typedef struct tux_view {
  view_mode mode;
  bool initialized;
  t3_vec3 pos, dir, up, plyr_pos;
} tux_view;
void view_reset(tux_view *v, view_mode mode);
void view_update(tux_view *v, const tux_phys *p, const course *c, float dt);

/* ── course.c: objects ───────────────────────────────────────────────── */
/* Trees as the original's two crossed textured quads (one InstancedMesh pair
 * per type) with the tree polyhedron as their Box3D hull; items as textured
 * quads facing the camera (banners face up the course). */
void course_build_objects(course *c, cw_tree *tree);
void course_update_billboards(course *c, t3_vec3 eye);
void course_trees_update(course *c, float dt, bool windy, t3_vec3 eye); /* GPU wind clock, LOD, shrub sway */
void course_trees_renderer(t3_renderer *r);
t3_geometry *tree_mesh_spruce(int variant, int lod); /* tree_mesh.c: the racer-scale conifer */
t3_geometry *tree_mesh_trunk(int lod);
t3_geometry *tree_mesh_cards(int variant, int lod); /* the branch sprig cards around the core */
t3_geometry *item_mesh_fish(void); /* item_mesh.c */
t3_geometry *item_mesh_flag(void);
t3_geometry *item_mesh_fish_card(void);
t3_geometry *item_mesh_gate_poles(void);
t3_geometry *item_mesh_gate_banner(void);                         /* register the "foliage" part's hook */
int course_collect(course *c, t3_vec3 pos);           /* herring collected at pos this tick */
t3_vec3 course_reset_point(const course *c, t3_vec3 pos); /* the reset point just uphill (reset.c) */
bool course_finished(const course *c, t3_vec3 pos);
void course_destroy_objects(course *c);
void course_set_tree_coverage(bool on);
void course_set_night(bool night); /* the trunk prints under moonlight */
void course_objects_show(const char *what, bool on); /* "trees", "splats", "items", "all" */
/* the terrain's quality tier (cartwheel/quality.h levels; -1 = follow the
 * graphics preset): a graphics lever, kept across course loads */
void course_set_terrain_quality(int q);

/* ── audio.c ─────────────────────────────────────────────────────────── */
enum { MUS_NONE, MUS_START, MUS_RACE, MUS_OVER, MUS_OPTIONS };
enum { SFX_PICKUP, SFX_TREE, SFX_COUNT };
void audio_init(void);
void audio_music(int which);
void audio_sfx(int id);
void audio_slide(const float w[SURF_KINDS], float speed, bool airborne, bool on);
void audio_update(void);

/* ── hud.c ───────────────────────────────────────────────────────────── */
void hud_init(void);
void hud_draw(const tux_phys *p, float race_time, int herring, int lives);
cw_font hud_font(void);
void hud_begin(void);
void hud_end(void);
void hud_text(const char *s, float x, float y, float size, cw_color c, int align); /* align: 0 left 1 centre 2 right */
void hud_title(const char *s, float x, float y, float size, cw_color c, int align);
void hud_box(float x, float y, float w, float h, cw_color c);
void hud_panel(float x, float y, float w, float h, float alpha); /* a rounded translucent card */
void hud_image(const char *asset, float x, float y, float w, float h);

/* ── fx.c ────────────────────────────────────────────────────────────── */
void sky_ring_init(t3_scene *scene); /* skyring.c: the sunny sky's far mountains and clouds */
void sky_ring_set(bool sunny, bool night);
void sky_ring_update(t3_vec3 cam, t3_vec3 to_sun, float time, bool windy);
void sky_ring_renderer(t3_renderer *r); /* register the clouds and aurora parts */
void fx_init(cw_node parent);
void fx_set_snowing(bool on);
void fx_set_motes(bool on); /* sunlit dust around the camera (sunny) */
void fx_set_wind(bool windy);
void fx_track_reset(void);
void fx_update(const tux_phys *p, t3_vec3 cam_pos, t3_vec3 cam_dir, bool racing);
void fx_pickup(t3_vec3 at);                 /* a herring's sparkle burst */
void fx_tree_hit(t3_vec3 tux_pos, t3_vec3 vel); /* snow shaken off the tree ahead */
