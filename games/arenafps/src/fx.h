#ifndef ARENA_FX_H
#define ARENA_FX_H
#include "game.h"

enum { SND_RIFLE, SND_SCATTER, SND_ROCKET, SND_BOOM, SND_RAIL, SND_HIT, SND_HURT, SND_PICKUP, SND_DIE, SND_CLICK, SND_SPAWN, SND_STEP, SND_SHELL, SND_TICK, SND_PAD, SND_LAND, SND_COUNT };

void fx_init(cw_node root); /* makes the pools under root (after the level is built) */
void fx_reset(void);        /* forget the pools (their nodes went with the level) */
void fx_tick(void);
void fx_play(int snd, t3_vec3 at, float volume, float pitch);
void fx_play_ui(int snd, float volume);
void fx_spark(t3_vec3 p, t3_vec3 n, int kind); /* 0 world hit: sparks and dust, 1 body hit */
void fx_hole(t3_vec3 p, t3_vec3 n);
void fx_explosion(t3_vec3 p, t3_vec3 n);
void fx_tracer(t3_vec3 a, t3_vec3 b, int kind); /* 0 bullet, 1 rail */
void fx_casing(t3_vec3 p, t3_vec3 right, t3_vec3 up);
#endif
