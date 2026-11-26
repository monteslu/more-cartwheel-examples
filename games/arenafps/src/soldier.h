/* arenafps: the fighters' bodies (soldier.c) */
#ifndef ARENA_SOLDIER_H
#define ARENA_SOLDIER_H
#include "game.h"

/* a body for fighter f in one team colour (0: soldier_a, blue; 1: soldier_b, red): f->body, f->anim, f->head */
void soldier_make(fighter *f, int slot, int variant, cw_node world, bool bigheads);
void soldier_free(int slot);
void soldier_free_all(void);
/* once a fixed tick after the rules: every body's animation from its fighter's state (fs[0..nf)). eyes: the players' eyes
 * (detail falls with distance from the nearest); own_hidden: player 0's own body is not drawn (one view) */
void soldier_tick(fighter *fs, int nf, const t3_vec3 *eyes, int neyes, bool own_hidden);
/* the bench crowd (autopilot builds): n more bodies running circles round the centres (0 removes them) */
void soldier_crowd(int n, cw_node world, const t3_vec3 *centres, int ncentres);
struct t3_object *soldier_object(int slot);    /* the body's model (NULL before it is made) */
struct t3_material *soldier_material(int slot); /* its own material (the rim goes on it) */

/* what the bodies cost and how detailed they are, last tick (debug fields) */
typedef struct soldier_stats {
  int32_t bodies, animated, ragdolls, lod[4];
} soldier_stats;
extern soldier_stats SOLDIER_ST;
#endif
