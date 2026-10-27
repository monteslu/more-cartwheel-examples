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

/* chars.c: skinned fighters built in code, posed from the simulation */
typedef struct fighter_model fighter_model;
fighter_model *model_new(int def, cw_node parent);
void model_free(fighter_model *m);
/* pose from the sim state: alpha is the render interpolation across the tick */
void model_pose(fighter_model *m, const ft_game *g, int who, float dt);
void model_set_visible(fighter_model *m, bool v);
t3_vec3 model_joint(fighter_model *m, int joint); /* world position of a joint, for effects */
enum { J_HEAD, J_HAND_F, J_HAND_B, J_FOOT_F, J_CHEST, J_HIPS };

/* fx.c: hit effects, all reading sim events */
void fx_init(cw_node root);
void fx_reset(void);

void fx_frame(const ft_game *g, float dt);
void fx_bind_lights(cw_node stage);
float fx_shake(void); /* camera shake amount now */
float fx_flash(void); /* a white screen flash 0..1 (super) */
extern int fx_count_hit, fx_count_block;

#endif
