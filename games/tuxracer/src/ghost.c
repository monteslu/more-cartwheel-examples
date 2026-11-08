/* The ghost: the player's fastest run on a course, recorded as he slides and
 * replayed beside him in Practice as a translucent Tux. Nothing of it reaches
 * the simulation: it is drawn from the save, never collides and never counts.
 *
 * A run is stored as the corners of its path: the recorder follows him every
 * tick and keeps a sample only where a straight line from the last kept one
 * would miss a tick's position by more than GHOST_TOL (a landing, a tree, a
 * carve), so straight running costs a sample every GHOST_MAX_GAP ticks and the
 * playback, a straight line between samples, stays within GHOST_TOL plus the
 * rounding of every tick. Each sample is a step from the previous one in
 * millimetres (taken from the position the decoder rebuilds, so the rounding
 * never drifts), its tick gap, the turn and whether he was in the air. The
 * orientation is rebuilt at playback as the physics builds its target. */
#include "game.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "cartwheel/core.h"

#define GHOST_TOL 0.003f   /* metres */
#define GHOST_MAX_GAP 63   /* ticks between samples at most (6 bits) */

static int gap_of(const ghost_sample *s) { return (s->flags >> 2) + 1; }

void ghost_rec_begin(ghost_rec *r, uint32_t course_hash, t3_vec3 at) {
  memset(&r->run, 0, offsetof(ghost_run, s));
  r->run.course_hash = course_hash;
  r->run.origin[0] = at.x, r->run.origin[1] = at.y, r->run.origin[2] = at.z;
  r->run.count = 1;
  r->run.s[0] = (ghost_sample){ { 0, 0, 0 }, 0, 0 };
  r->last = at;
  r->broken = false;
  r->ticks = 0;
  r->npend = 0;
}

/* keep the pending tick k (1-based in the pending list) as a sample; the pending ticks after it start the next segment */
static void keep(ghost_rec *r, int k) {
  if (r->broken) return;
  if (r->run.count >= GHOST_MAX) { r->broken = true; return; } /* longer than the store holds: no ghost of it */
  const ghost_pend *p = &r->pend[k - 1];
  ghost_sample s = { { 0, 0, 0 }, (int8_t)lrintf(fmaxf(-1, fminf(1, p->turn)) * 127), (uint8_t)(p->flags | ((k - 1) << 2)) };
  float d[3] = { p->pos.x - r->last.x, p->pos.y - r->last.y, p->pos.z - r->last.z };
  for (int c = 0; c < 3; c++) {
    long mm = lrintf(d[c] * 1000);
    if (mm < -32767 || mm > 32767) { r->broken = true; return; } /* a reset more than 32 m back */
    s.d[c] = (int16_t)mm;
  }
  r->last.x += s.d[0] * 0.001f, r->last.y += s.d[1] * 0.001f, r->last.z += s.d[2] * 0.001f;
  r->run.s[r->run.count++] = s;
  memmove(r->pend, r->pend + k, (size_t)(r->npend - k) * sizeof *r->pend);
  r->npend -= k;
}

/* does a straight line from the last kept sample to pending tick n pass every pending tick before it within GHOST_TOL? */
static bool line_holds(const ghost_rec *r, int n) {
  t3_vec3 a = r->last, b = r->pend[n - 1].pos;
  for (int j = 1; j < n; j++) {
    float t = (float)j / (float)n;
    t3_vec3 q = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
    if (t3_vec3_length(t3_vec3_sub(q, r->pend[j - 1].pos)) > GHOST_TOL) return false;
    if (r->pend[j - 1].flags != r->pend[n - 1].flags) return false; /* in the air or not: a corner of its own */
  }
  return true;
}

/* one racing tick */
void ghost_rec_tick(ghost_rec *r, t3_vec3 pos, float turn, bool airborne, bool hidden) {
  r->ticks++;
  if (r->broken) return;
  r->pend[r->npend++] = (ghost_pend){ pos, turn, (uint8_t)((airborne ? 1 : 0) | (hidden ? 2 : 0)) };
  if (r->npend > GHOST_MAX_GAP || !line_holds(r, r->npend)) keep(r, r->npend - 1);
  /* the tick that broke the line starts the next one: it may break it again on its own (a gap of 1 always holds) */
}

/* the run so far ends here: keep what is pending */
void ghost_rec_flush(ghost_rec *r) {
  if (r->npend) keep(r, r->npend);
}

bool ghost_valid(const ghost_run *g) { return g && g->course_hash && g->count >= 2 && g->count <= GHOST_MAX && g->time > 0; }

/* the ghost at `ticks` ticks into its run (fractional: render interpolation). False once it has finished. */
bool ghost_at(ghost_play *gp, const ghost_run *g, float ticks, ghost_pose *out) {
  if (!ghost_valid(g)) return false;
  if (gp->cache_i < 0 || ticks < (float)gp->cache_tick) {
    gp->cache_i = 0, gp->cache_tick = 0;
    gp->cache = (t3_vec3){ g->origin[0], g->origin[1], g->origin[2] };
  }
  /* walk forward to the segment holding `ticks` */
  for (;;) {
    if (gp->cache_i + 1 >= g->count) return false;
    const ghost_sample *n = &g->s[gp->cache_i + 1];
    if ((float)(gp->cache_tick + gap_of(n)) > ticks) break;
    gp->cache.x += n->d[0] * 0.001f, gp->cache.y += n->d[1] * 0.001f, gp->cache.z += n->d[2] * 0.001f;
    gp->cache_tick += gap_of(n);
    gp->cache_i++;
  }
  const ghost_sample *a = &g->s[gp->cache_i], *b = &g->s[gp->cache_i + 1];
  int gap = gap_of(b);
  float t = (ticks - (float)gp->cache_tick) / (float)gap;
  t3_vec3 d = { b->d[0] * 0.001f, b->d[1] * 0.001f, b->d[2] * 0.001f };
  out->pos = t3_vec3_add(gp->cache, t3_vec3_scale(d, t));
  out->vel = t3_vec3_scale(d, (float)TICK_HZ / (float)gap);
  out->turn = (a->turn + (b->turn - a->turn) * t) / 127.0f;
  out->airborne = b->flags & 1;
  out->hidden = b->flags & 2;
  return true;
}
