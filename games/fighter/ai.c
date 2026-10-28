/* The computer opponent. It plays through the same inputs a pad does (punch, kick, block and a direction): a
 * motion is queued as a macro of directions and buttons that the command buffer then reads, so the AI exercises
 * the real input path. Levels: 0 easy (guards late and at the wrong height often), 1 normal, 2 hard (reads the
 * opponent's guard height, times parries now and then). */
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
/* a special with its own button (P when it takes either) */
static void special(ft_game *g, int who, const ft_def *d, int k) { do_motion(g, who, &d->mo[k], d->mbtn[k] & B_P ? B_P : B_K); }
/* one normal: the direction held a frame before and with the button */
static void normal(ft_game *g, int who, int dir, uint16_t btn) { push(g, who, dir, 1, 0); push(g, who, dir, 1, btn); push(g, who, 5, 2, 0); }
static int dist_to(const ft_game *g, int who) {
  int d = g->f[1 - who].x - g->f[who].x;
  return d < 0 ? -d : d;
}

ft_input ft_ai(ft_game *g, int who) {
  ft_fighter *me = &g->f[who], *op = &g->f[1 - who];
  const ft_def *d = &g->roster->d[me->def];
  const ft_def *od = &g->roster->d[op->def];
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
  bool free_ = ft_actionable(me);
  /* guard: the opponent's strike is coming. Low strikes need a low guard; highs pass over one, so a low guard is
   * also fine against them; a mid is blocked either way. The computer reacts after a delay (easy guards at a random
   * height); a block pressed in the last frames before the hit is a parry, which it only does on purpose and only
   * sometimes: otherwise a strike it saw too late lands. */
  if ((free_ || me->state == S_BLOCK) && op->state == S_ATTACK && dist < 1900) {
    const ft_move *om = &od->mv[op->mv];
    static const int REACT[3] = { 9, 6, 4 }, PARRY[3] = { 0, 10, 30 };
    if (!(om->flags & (FM_PROJ | FM_THROW)) && op->t <= om->su + om->ac && (me->state == S_BLOCK || op->t >= REACT[lvl])) {
      int chance = lvl == 0 ? 35 : lvl == 1 ? 65 : 85;
      bool guarding = me->state == S_BLOCK;
      if (guarding || (int)r < chance) {
        bool low = om->lv == LV_LOW || (om->lv == LV_HIGH && lvl >= 1 && r % 3 == 0);
        if (lvl == 0 && r % 3 == 0) low = !low;
        int to_active = om->su + 1 - op->t;
        if (!guarding && to_active <= FT_PARRY_WINDOW + 1 && (me->parry_lock || (int)(rnd(g) % 100) >= PARRY[lvl])) return none;
        return raw_dir(low ? 2 : 5, me->facing, B_G);
      }
    }
  }
  if (me->state == S_BLOCK && me->stun) return raw_dir(op->state == S_ATTACK && od->mv[op->mv].lv == LV_LOW ? 2 : 5, me->facing, B_G);
  if (!free_) return none;
  /* projectile guard (or a parry at hard) */
  for (int p = 0; p < 8; p++)
    if (g->proj[p].live && g->proj[p].owner != who) {
      int pd = g->proj[p].x - me->x; if (pd < 0) pd = -pd;
      if (pd < 1500 && lvl >= 1 && (int)r < (lvl == 1 ? 55 : 85)) return raw_dir(5, me->facing, B_G);
    }
  /* super when full */
  if (me->meter >= FT_METER_MAX && dist < 3000 && (op->state == S_HIT || op->state == S_BLOCK || (lvl == 2 && r < 20))) {
    special(g, who, d, 3);
    return none;
  }
  /* anti-air: the rising special, or a high punch */
  if (ft_airborne(op) && dist < 1700 && lvl >= 1 && r < 70) {
    if (!d->mo[1].charge && r < 45) special(g, who, d, 1);
    else normal(g, who, 8, B_P);
    return none;
  }
  bool op_low_guard = op->state == S_BLOCK && op->crouch_block, op_guard = op->state == S_BLOCK;
  if (dist > 3000) {
    bool mine = false;
    for (int p = 0; p < 8; p++) if (g->proj[p].live && g->proj[p].owner == who) mine = true;
    if (r < 12 && !mine && d->mv[M_SP1].flags & FM_PROJ) { special(g, who, d, 0); return none; }
    if (r < 32 && lvl >= 1) { push(g, who, 6, 2, 0); push(g, who, 5, 2, 0); push(g, who, 6, 2, 0); return none; }
    return raw_dir(6, me->facing, 0);
  }
  if (dist > 1500) {
    if (r < 18) return raw_dir(5, me->facing, 0);
    if (r < 30) { normal(g, who, 6, B_P); return none; }               /* the lunge closes the gap */
    if (r < 40) { normal(g, who, 6, B_K); return none; }               /* the step kick */
    if (r < 47 && dist < 2000) { normal(g, who, 2, B_K); return none; } /* the sweep at its range */
    if (r < 53 && lvl >= 1) { push(g, who, 9, 2, 0); push(g, who, 5, 2, 0); push(g, who, 9, 10, 0); push(g, who, 9, 1, B_K); return none; } /* a hop in */
    if (r < 58 && d->mv[M_SP1].flags & FM_PROJ) { special(g, who, d, 0); return none; }
    return raw_dir(6, me->facing, 0);
  }
  /* close: read the guard at normal and hard. A low guard is opened by a mid or a throw; a standing one by a low */
  if (op_guard && lvl >= 1 && r < 45) {
    if (r < 18) { push(g, who, 5, 2, B_P | B_K); return none; }      /* throw a guard */
    if (op_low_guard) { normal(g, who, 5, r & 1 ? B_K : B_P); return none; }
    normal(g, who, 2, r & 1 ? B_K : B_P);
    return none;
  }
  if (r < 18) { normal(g, who, 5, B_P); push(g, who, 5, 3, 0); normal(g, who, 5, B_K); return none; } /* punch into kick */
  if (r < 28) { normal(g, who, 2, B_P); push(g, who, 2, 3, 0); normal(g, who, 2, B_K); return none; } /* low jab into sweep */
  if (r < 36) { normal(g, who, 8, B_P); return none; }
  if (r < 43) { normal(g, who, 8, B_K); return none; }
  if (r < 50) { normal(g, who, 5, B_K); return none; }
  if (r < 58) { special(g, who, d, 2); return none; }
  if (r < 64 && lvl >= 1) { push(g, who, 5, 2, B_P | B_K); return none; }
  if (r < 74) { push(g, who, r & 1 ? 2 : 5, 14, B_G); return none; } /* hold a guard a moment */
  if (r < 84) return raw_dir(4, me->facing, 0);
  return none;
}
