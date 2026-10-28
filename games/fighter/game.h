/* fighter: shared declarations between the game's files. */
#ifndef FT_GAME_H
#define FT_GAME_H

#include <stdint.h>
#include <stdbool.h>

#include "cartwheel/debug.h"
#include "cartwheel/framing.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "cartwheel/platform.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "sim.h"

/* textures.c: procedural PBR sets (albedo sRGB, normal, roughness, ao), made once, integer hashing only */
enum { TEX_CONCRETE, TEX_TILE, TEX_WOOD, TEX_METAL, TEX_BRICK, TEX_FABRIC, TEX_KINDS };
typedef struct tex_set { t3_texture *map, *normal, *rough, *ao; } tex_set;
void tex_apply(t3_material *m, int kind, float ru, float rv, float normal_scale, float ao_intensity);
void tex_free_all(void);
extern uint32_t tex_hash;
t3_color lin(float r, float g, float b); /* screen colour to linear */

/* secondary.c: spring bones (hair, headband tails, coat skirts, belly) and character shading */
enum { SB_MAX = 5 };
typedef struct spring_bone { t3_object *bone; t3_vec3 rest, tip, T, V; bool live; } spring_bone;
typedef struct spring_rig { t3_object *scene; spring_bone b[SB_MAX]; } spring_rig;
void sb_bind(spring_rig *s, t3_object *scene);
void sb_reset(spring_rig *s);
void sb_jolt(spring_rig *s, float x, float y, float z);
void sb_step(spring_rig *s, t3_vec3 pos, float yaw, float dt);
float sb_speed(const spring_rig *s);
void char_shade(t3_object *scene);

/* chars.c: skinned fighters (assets/models, made by tools/make-characters.mjs), posed from the simulation */
typedef struct fighter_model fighter_model;
bool models_preload(void); /* parse and upload the four fighters once (start-up) */
fighter_model *model_new(int def, cw_node parent);  /* an instance of the preloaded model: no parse, no upload */
void model_reset(fighter_model *m);
void model_park(fighter_model *m); /* visible, out of sight under the floor (warming its draw state) */
void model_free(fighter_model *m);
/* pose from the sim state: alpha is the render interpolation across the tick */
void model_pose(fighter_model *m, const ft_game *g, int who, float dt);
void model_set_visible(fighter_model *m, bool v);
/* the select and result screens turn a fighter toward the camera by this many radians (0 in a fight) */
void model_set_showcase(fighter_model *m, float turn);
void model_look_at(fighter_model *m, t3_vec3 p); /* where the eyes look (world) */
t3_vec3 model_joint(fighter_model *m, int joint); /* world position of a joint, for effects */
float model_spring_speed(const fighter_model *m); /* summed spring tip speed (a probe for the secondary motion) */
enum { J_HEAD, J_HAND_F, J_HAND_B, J_FOOT_F, J_CHEST, J_HIPS };

/* crowd.c: spectators around the stage, cheering from the sim's events */
enum { CROWD_MAX = 10 };
typedef struct crowd_member { t3_gltf *g; cw_node node; t3_object *b[10], *fing[2][3]; t3_vec3 hips; float x, z, phase, thr, cheer, clap; } crowd_member;
typedef struct crowd { int n; crowd_member m[CROWD_MAX]; float excite; uint32_t seen; } crowd;
void crowd_load(crowd *c, int stage, cw_node parent);
void crowd_free(crowd *c);
void crowd_frame(crowd *c, const ft_game *g, float dt);

/* stage.c: the modelled stage background, its ambient life and the fighters' rim lights */
void stage_load(int stage, cw_node parent);
void stage_free(void);
void stage_frame(const ft_game *g, float dt);
void stage_set_showcase(bool on); /* the rim lights become key lights on the faces (select and result screens) */

/* fx.c: hit effects, all reading sim events */
void fx_init(cw_node root);
void fx_reset(void);

void fx_frame(const ft_game *g, float dt);
void fx_bind_lights(cw_node stage);
float fx_shake(void); /* camera shake amount now */
float fx_flash(void); /* a white screen flash 0..1 (super) */
extern int fx_count_hit, fx_count_block;

#endif
