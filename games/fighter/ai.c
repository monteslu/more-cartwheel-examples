/* The computer opponent. It plays through the same inputs a pad does: a
 * motion is queued as a macro of directions and buttons that the command
 * buffer then reads, so the AI exercises the real input path. */
#include <stdlib.h>
#include <string.h>

#include "sim.h"

static uint32_t rnd(ft_game *g) {
  g->rng = g->rng * 1664525u + 1013904223u;
  return g->rng >> 8;
}

static ft_input raw_dir(int dir, int facing, uint16_t btn) {
  /* dir is numpad relative to facing */
  int x = (dir % 3 == 0) - (dir % 3 == 1);
  int y = dir >= 7 ? 1 : (dir <= 3 ? -1 : 0);
  if (facing < 0) x = -x;
  ft_input in = { 0 };
  in.left = x < 0; in.right = x > 0; in.up = y > 0; in.down = y < 0; in.btn = btn;
  return in;
}
static void push(ft_game *g, int who, int dir, int frames, uint16_t btn) {
  if (g->macro_n[who] >= 12) return;
  g->macro[who][g->macro_n[who]].in = raw_dir(dir, g->f[who].facing, btn);
  g->macro[who][g->macro_n[who]].frames = (int16_t)frames;
  g->macro_n[who]++;
}
/* a motion as steps held 2 frames each, then the button; the macro is stored in order */
static void do_motion(ft_game *g, int who, const cw_motion *m, uint16_t btn) {
  int first = 0;
  if (m->charge) { push(g, who, m->steps[0], m->charge_frames + 2, 0); first = 1; }
  for (int i = first; i < m->count; i++) {
    bool last = i == m->count - 1;
    push(g, who, m->steps[i], last ? 3 : 2, last ? btn : 0);
  }
  push(g, who, 5, 1, 0);
}
static int dist_to(const ft_game *g, int who) {
  int d = g->f[1 - who].x - g->f[who].x;
  return d < 0 ? -d : d;
}

ft_input ft_ai(ft_game *g, int who) {
  ft_fighter *me = &g->f[who], *op = &g->f[1 - who];
  const ft_def *d = &g->roster->d[me->def];
  int lvl = g->cpu_level[who];
  ft_input none = { 0 };
  if (g->phase != P_FIGHT) { g->macro_n[who] = 0; return none; }
  /* a queued motion plays out first */
  if (g->macro_n[who] > 0) {
    ft_input in = g->macro[who][0].in;
    if (--g->macro[who][0].frames <= 0) {
      memmove(&g->macro[who][0], &g->macro[who][1], sizeof g->macro[who][0] * (size_t)(g->macro_n[who] - 1));
      g->macro_n[who]--;
    }
    return in;
  }
  int dist = dist_to(g, who);
  uint32_t r = rnd(g) % 100;
  bool blockable = me->state == S_IDLE || me->state == S_WALKF || me->state == S_WALKB || me->state == S_CROUCH || me->state == S_BLOCK;
  /* guard: the opponent's attack is coming */
  if (blockable && op->state == S_ATTACK && dist < 1900) {
    const ft_move *om = &g->roster->d[op->def].mv[op->mv];
    int chance = lvl == 0 ? 25 : lvl == 1 ? 55 : 85;
    if ((om->flags & FM_PROJ) == 0 && (int)r < chance) return raw_dir(om->lv == LV_LOW ? 1 : 4, me->facing, 0);
  }
  if (me->state == S_BLOCK) return raw_dir(op->state == S_ATTACK && g->roster->d[op->def].mv[op->mv].lv == LV_LOW ? 1 : 4, me->facing, 0);
  if (!ft_actionable(me)) return none;
  /* projectile guard */
  for (int p = 0; p < 8; p++)
    if (g->proj[p].live && g->proj[p].owner != who) {
      int pd = g->proj[p].x - me->x; if (pd < 0) pd = -pd;
      if (pd < 1500 && lvl >= 1 && (int)r < (lvl == 1 ? 50 : 80)) return raw_dir(4, me->facing, 0);
    }
  /* super when full */
  if (me->meter >= FT_METER_MAX && dist < 3000 && (op->state == S_HIT || op->state == S_BLOCK || (lvl == 2 && r < 20))) {
    do_motion(g, who, &d->mo[3], B_M);
    return none;
  }
  /* anti-air */
  if (ft_airborne(op) && dist < 1700 && lvl >= 1 && r < 70) {
    do_motion(g, who, &d->mo[d->mo[1].count ? 1 : 0], B_H);
    return none;
  }
  if (dist > 3000) {
    if (r < 25 && g->roster->d[me->def].mv[M_SP1].flags & FM_PROJ) { do_motion(g, who, &d->mo[0], (uint16_t)(1 << (rnd(g) % 3))); return none; }
    if (r < 32 && lvl >= 1) { push(g, who, 6, 2, 0); push(g, who, 5, 2, 0); push(g, who, 6, 2, 0); return none; }
    return raw_dir(6, me->facing, 0);
  }
  if (dist > 1500) {
    if (r < 20) return raw_dir(5, me->facing, 0);
    if (r < 40) { push(g, who, 5, 1, B_M); return none; }
    if (r < 50) { push(g, who, 2, 1, 0); push(g, who, 2, 2, B_M); return none; }
    if (r < 56 && lvl >= 1) { push(g, who, 9, 1, 0); push(g, who, 9, 10, 0); push(g, who, 9, 1, B_M); return none; }
    return raw_dir(6, me->facing, 0);
  }
  /* close */
  if (op->state == S_BLOCK && r < 30) { push(g, who, 5, 1, B_T); return none; }
  if (r < 30) { push(g, who, 5, 1, B_L); push(g, who, 5, 3, 0); push(g, who, 5, 1, B_L); push(g, who, 5, 3, 0); push(g, who, 5, 1, B_M); return none; }
  if (r < 42) { push(g, who, 2, 1, 0); push(g, who, 2, 2, B_L); return none; }
  if (r < 54) { push(g, who, 5, 1, B_H); return none; }
  if (r < 64) { do_motion(g, who, &d->mo[2], B_L); return none; }
  if (r < 74 && lvl >= 1) { push(g, who, 5, 1, B_T); return none; }
  if (r < 84) return raw_dir(4, me->facing, 0);
  return none;
}
