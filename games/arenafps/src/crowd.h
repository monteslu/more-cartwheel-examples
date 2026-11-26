/* arenafps: the background battle (crowd.c), a fight out of reach past the walls for the Battle mode */
#ifndef ARENA_CROWD_H
#define ARENA_CROWD_H
#include "game.h"

/* the switch (off by default; the Battle mode turns it on). Off: nothing is built, drawn or heard. Autopilot builds
 * also read ARENA_BATTLE_BG=1 from the environment. */
void crowd_set(bool on);
bool crowd_is_on(void);
/* how hard each side still fights, 0..1 (default 1 and 1): a side that is losing thins out and fires less */
void crowd_strength(float blue, float red);
/* the one hook: once a fixed tick after the bodies, with the level's world node (it builds there on first use and
 * again after the world changed) and the players' eyes */
void crowd_tick(cw_node world, const t3_vec3 *eyes, int neyes);
/* the world is going away (its nodes go with it): forget them, stop the sound */
void crowd_reset(void);

/* what it draws, last tick (debug fields) */
typedef struct crowd_stats {
  int32_t figures, alive, flashes, tracers, blasts, draws;
} crowd_stats;
extern crowd_stats CROWD_ST;

/* arena.c: the ground past the walls at (x, z) for a figure of radius clear (clear < 0: the height only, no test
 * against the houses and trees); false where there is nothing to stand on or something stands there already */
bool arena_outside_ground(float x, float z, float clear, float *y);
#endif
