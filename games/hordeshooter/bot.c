/* hordeshooter bots: used by the autopilot app and by the sim-only soak. */
#include <math.h>
#include "cartwheel/math.h"
#include "game.h"

/* bots: keep clear of the nearest enemies, drift toward gems, shoot the
 * nearest enemy, throw a blast into a thick crowd */
void horde_bot(const sim *s, int i, input *in) {
  const player *p = &s->p[i];
  float best = 1e9f, bx = 0, by = 0, near_x = 0, near_y = 0;
  int crowd = 0;
  for (int k = 0; k < MAXE; k += 1) {
    const enemy *e = &s->e[k];
    if (!e->alive) continue;
    float dx = e->x - p->x, dy = e->y - p->y, d = dx * dx + dy * dy;
    if (d < 64) crowd++;
    if (d < best) best = d, bx = dx, by = dy;
    if (d < 36) near_x -= dx / (d + 1), near_y -= dy / (d + 1);
  }
  float gd = 1e9f, gx = 0, gy = 0, pd = 1e9f, px = 0, py = 0;
  for (int k = 0; k < MAXG; k++) {
    const pickup *g = &s->g[k];
    if (!g->alive) continue;
    float dx = g->x - p->x, dy = g->y - p->y, d = dx * dx + dy * dy;
    if (g->kind == PK_POWER) { if (d < pd) pd = d, px = dx, py = dy; continue; }
    if (d < gd) gd = d, gx = dx, gy = dy;
  }
  float mx = near_x * 6, my = near_y * 6;
  if (gd < 400 && gd > 0.01f) {
    float l = sqrtf(gd);
    mx += gx / l * 0.7f, my += gy / l * 0.7f;
  }
  if (pd < 18 * 18 && pd > 0.01f) { /* a power-up within reach: go for it, braver than for a gem */
    float l = sqrtf(pd);
    mx += px / l * 1.6f, my += py / l * 1.6f;
  }
  /* keep off the walls and crates, apart from the other bots, and in the yard */
  for (int k = 0; k < MAXO; k++) {
    const obstacle *o = &s->o[k];
    if (!o->alive) continue;
    float dx = p->x - o->x, dy = p->y - o->y, d2 = dx * dx + dy * dy, r = 3.2f + o->hx;
    if (d2 < r * r && d2 > 0.01f) mx += dx / d2 * 4.0f, my += dy / d2 * 4.0f;
  }
  for (int k = 0; k < MAXP; k++) {
    const player *q = &s->p[k];
    if (k == i || !q->active) continue;
    float dx = p->x - q->x, dy = p->y - q->y, d2 = dx * dx + dy * dy;
    if (d2 < 100 && d2 > 0.01f) mx += dx / d2 * 6.0f, my += dy / d2 * 6.0f;
  }
  mx -= p->x * 0.03f, my -= p->y * 0.03f;
  float t = (float)s->tick * 0.01f + (float)i * 2.0f;
  mx += cw_sin(t) * 0.25f, my += cw_cos(t * 1.3f) * 0.25f;
  in->mx = mx, in->my = my;
  if (s->state == ST_HATCH && s->state_ticks >= HATCH_OPEN_TICKS - 20) { /* the hatch is open: down it goes (after a look round for loot) */
    float l = sqrtf(p->x * p->x + p->y * p->y);
    if (l > 0.3f) in->mx = -p->x / l, in->my = -p->y / l;
  }
  if (best < 1e8f) {
    float l = sqrtf(best);
    in->ax = bx / l, in->ay = by / l;
  } else in->ax = cw_cos(p->aim), in->ay = cw_sin(p->aim);
  in->fire = best < 400;
  in->bomb = crowd > ((p->grenades > 0 || p->mines > 0) ? 10 : 28);
  if (p->offer_ticks > 0 && p->offer_ticks < 60 * 8 - 45) in->pick = 1 + (p->level + i) % 3; /* a bot takes a while, then picks */
}
