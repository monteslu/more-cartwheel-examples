/* The ghost: the player's fastest run on a course, recorded as he slides and
 * replayed beside him in Practice as a translucent Tux. Nothing of it reaches
 * the simulation: it is drawn from the save, never collides and never counts.
 *
 * A run is sampled every GHOST_STEP ticks (20 Hz): the body's position as a
 * step from the previous sample in millimetres (each step taken from the
 * position the decoder will rebuild, so the rounding never drifts), the turn
 * and whether he was in the air. Playback is a Catmull-Rom curve through the
 * samples; the orientation is rebuilt as the physics builds its target (along
 * the travel, belly into the slope, rolled for the turn). */
#include "game.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "cartwheel/core.h"

static t3_vec3 sample_pos(const ghost_run *g, int i, t3_vec3 *cache, int *cache_i) {
  /* positions are a running sum: walk from the cached index (playback moves forward) */
  if (i < 0) i = 0;
  if (i >= g->count) i = g->count - 1;
  if (*cache_i < 0 || i < *cache_i) { *cache_i = 0; *cache = (t3_vec3){ g->origin[0], g->origin[1], g->origin[2] }; }
  while (*cache_i < i) {
    const ghost_sample *s = &g->s[++*cache_i];
    cache->x += s->d[0] * 0.001f, cache->y += s->d[1] * 0.001f, cache->z += s->d[2] * 0.001f;
  }
  return *cache;
}

void ghost_rec_begin(ghost_rec *r, uint32_t course_hash, t3_vec3 at) {
  memset(&r->run, 0, offsetof(ghost_run, s));
  r->run.course_hash = course_hash;
  r->run.origin[0] = at.x, r->run.origin[1] = at.y, r->run.origin[2] = at.z;
  r->run.count = 1;
  r->run.s[0] = (ghost_sample){ { 0, 0, 0 }, 0, 0 };
  r->last = at;
  r->broken = false;
  r->ticks = 0;
}

/* one racing tick (ticks since the start of the run: sampled every GHOST_STEP) */
void ghost_rec_tick(ghost_rec *r, t3_vec3 pos, float turn, bool airborne, bool hidden) {
  r->ticks++;
  if (r->broken || r->ticks % GHOST_STEP) return;
  if (r->run.count >= GHOST_MAX) { r->broken = true; return; } /* longer than the store holds: no ghost of it */
  ghost_sample s = { { 0, 0, 0 }, (int8_t)lrintf(fmaxf(-1, fminf(1, turn)) * 127), (uint8_t)((airborne ? 1 : 0) | (hidden ? 2 : 0)) };
  float d[3] = { pos.x - r->last.x, pos.y - r->last.y, pos.z - r->last.z };
  for (int k = 0; k < 3; k++) {
    long mm = lrintf(d[k] * 1000);
    if (mm < -32767 || mm > 32767) { r->broken = true; return; } /* a reset more than 32 m back */
    s.d[k] = (int16_t)mm;
  }
  r->last.x += s.d[0] * 0.001f, r->last.y += s.d[1] * 0.001f, r->last.z += s.d[2] * 0.001f;
  r->run.s[r->run.count++] = s;
}

bool ghost_valid(const ghost_run *g) { return g && g->course_hash && g->count >= 2 && g->count <= GHOST_MAX && g->time > 0; }

static t3_vec3 catmull(t3_vec3 p0, t3_vec3 p1, t3_vec3 p2, t3_vec3 p3, float t) {
  float t2 = t * t, t3 = t2 * t;
  t3_vec3 r;
  r.x = 0.5f * (2 * p1.x + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);
  r.y = 0.5f * (2 * p1.y + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3);
  r.z = 0.5f * (2 * p1.z + (-p0.z + p2.z) * t + (2 * p0.z - 5 * p1.z + 4 * p2.z - p3.z) * t2 + (-p0.z + 3 * p1.z - 3 * p2.z + p3.z) * t3);
  return r;
}

/* the ghost at `ticks` ticks into its run (fractional: render interpolation). False once it has finished. */
bool ghost_at(ghost_play *gp, const ghost_run *g, float ticks, ghost_pose *out) {
  if (!ghost_valid(g)) return false;
  float f = ticks / GHOST_STEP;
  if (f < 0) f = 0;
  int i = (int)f;
  if (i >= g->count - 1) return false;
  float t = f - (float)i;
  /* four running-sum positions: the cache walks forward from the lowest */
  t3_vec3 p[4];
  for (int k = 0; k < 4; k++) {
    int j = i - 1 + k;
    if (j < 0) j = 0;
    if (j > g->count - 1) j = g->count - 1;
    if (k == 0) p[0] = sample_pos(g, j, &gp->cache, &gp->cache_i);
    else { t3_vec3 c = gp->cache; int ci = gp->cache_i; p[k] = sample_pos(g, j, &c, &ci); }
  }
  out->pos = catmull(p[0], p[1], p[2], p[3], t);
  const ghost_sample *a = &g->s[i], *b = &g->s[i + 1];
  out->turn = (a->turn + (b->turn - a->turn) * t) / 127.0f;
  out->airborne = (t < 0.5f ? a->flags : b->flags) & 1;
  out->hidden = (t < 0.5f ? a->flags : b->flags) & 2;
  t3_vec3 v = t3_vec3_sub(p[2], p[1]);
  out->vel = t3_vec3_scale(v, (float)TICK_HZ / GHOST_STEP);
  return true;
}
