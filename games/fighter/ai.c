/* The computer opponent. It plays through the same inputs a pad does (punch, kick, block and a direction): a
 * motion is queued as a macro of directions and buttons that the command buffer then reads, so the AI exercises
 * the real input path. It knows what a player can see: the other fighter's pose and position, the moves it has been
 * using, and the frame table, which a player learns. It never looks at the other pad.
 *
 * In order: react to a strike it sees coming, punish a recovery in reach, then, when the other fighter keeps
 * repeating one thing, answer that thing (stay out of its range and hit the whiff, get inside a slow one, guard at
 * the height it has been hitting and punish the block). Otherwise it plays a general game.
 * Levels: 0 easy (late, guards at the wrong height often, no reading), 1 normal, 2 hard (sooner, reads a repeat sooner). */
#include <stdlib.h>
#include <string.h>

#include "sim.h"

/* frames, per level. react: from a strike's first frame to the guard. A person takes 12 or more: only hard is near that.
 * read: how much of its recent moves one move must be before it counts as a habit (256 = all of them).
 * punish_delay: frames into a recovery before it is noticed; punish, aa: percent chance a frame.
 * guard: percent chance a frame that a seen strike is guarded. lapse: one frame in this many it stops paying attention
 * for a second or so, which is when a person gets through. */
static const struct { int react, read, punish_delay, punish, aa, wake_guard, parry, guard, lapse; } LV[3] = {
  { 16, 9999, 14, 8, 0, 25, 0, 40, 1 },
  { 15, 170, 9, 12, 12, 40, 8, 34, 80 },
  { 10, 110, 6, 40, 30, 80, 15, 60, 200 },
};

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
  g->ai[who].soft = false;
}
/* movement or a guard held a moment: a person does not re-decide every frame, but does let go when something happens */
static void hold(ft_game *g, int who, int dir, int frames, uint16_t btn) { push(g, who, dir, frames, btn); g->ai[who].soft = true; }
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
static const int SLOT_DIR[8] = { 5, 8, 2, 6, 5, 8, 2, 6 };
static void normal_slot(ft_game *g, int who, int s) { normal(g, who, SLOT_DIR[s], s < M_5K ? B_P : B_K); }
static int dist_to(const ft_game *g, int who) {
  int d = g->f[1 - who].x - g->f[who].x;
  return d < 0 ? -d : d;
}
/* how far a move reaches from where it starts: its box, the other's body, the step it takes */
static int reach_of(const ft_def *d, int slot) {
  const ft_move *m = &d->mv[slot];
  if (m->flags & FM_PROJ) return 9000;
  return m->hit.x1 + d->w_half + (m->mv ? m->mv * (m->mvto - m->mvfrom + 1) : 0);
}

/* what the other fighter just did, as anyone watching would see it */
static void observe(ft_game *g, int who) {
  ft_ai_mem *a = &g->ai[who];
  const ft_fighter *op = &g->f[1 - who];
  bool atk = op->state == S_ATTACK;
  if (atk && (a->was_state != S_ATTACK || a->was_mv != op->mv || op->t < a->was_t)) {
    /* a chain's later moves only come after the opener lands: the opener is the habit to answer. A move that follows a
     * finished one is a new opener; one that cut the last move short is a chain */
    bool chained = a->was_state == S_ATTACK && a->was_t < ft_move_total(&g->roster->d[op->def].mv[a->was_mv]) - 1;
    if (!chained) {
      for (int s = 0; s < FT_NMOVES; s++) a->score[s] = (int16_t)(a->score[s] - a->score[s] / 8);
      a->score[op->mv] = (int16_t)(a->score[op->mv] + 32);
    }
    a->quiet = 0;
  } else if (a->quiet < 60000) a->quiet++;
  if (op->state == S_BLOCK) { if (a->guard_seen < 256) a->guard_seen++; }
  else if (a->guard_seen > 0 && g->tick % 4 == 0) a->guard_seen--;
  a->was_state = op->state; a->was_mv = op->mv; a->was_t = (uint8_t)(op->t > 255 ? 255 : op->t);
}
/* the move it has been repeating, or -1 */
static int habit(const ft_game *g, int who, int lvl) {
  const ft_ai_mem *a = &g->ai[who];
  int best = 0;
  for (int s = 1; s < FT_NMOVES; s++) if (a->score[s] > a->score[best]) best = s;
  return a->score[best] >= LV[lvl].read ? best : -1;
}

/* the hardest normal that lands in what is left of a recovery from here; -1 if none. Walk: how far it may still walk first */
static int punish_with(const ft_def *d, int dist, int left, int *need_walk) {
  int best = -1, walk = -1;
  for (int s = 0; s <= M_6K; s++) {
    const ft_move *m = &d->mv[s];
    if (m->su + 3 > left) continue;
    int short_by = dist - reach_of(d, s) + 40;
    if (short_by > 0) {
      /* out of reach by a walk it has time for */
      if ((left - m->su - 3) * d->walk >= short_by && walk < 0) walk = s;
      continue;
    }
    if (best < 0 || m->dmg > d->mv[best].dmg) best = s;
  }
  *need_walk = best < 0 && walk >= 0;
  return best >= 0 ? best : walk;
}

/* It keeps doing `fav`. The answer comes from what the move is: what it can reach, how fast, whether it can be beaten
 * in its startup, and where it hits. Returns true with *out set when it has one. */
static bool answer_habit(ft_game *g, int who, int fav, int dist, uint32_t r, ft_input *out) {
  ft_fighter *me = &g->f[who];
  const ft_fighter *op = &g->f[1 - who];
  const ft_def *d = &g->roster->d[me->def], *od = &g->roster->d[op->def];
  const ft_move *fm = &od->mv[fav];
  int lvl = g->cpu_level[who], R = reach_of(od, fav);
  int fw = me->facing;
  bool om_attacking = op->state == S_ATTACK;
  /* the guard that holds it: a high passes over a low guard and then it has whiffed */
  int gdir = fm->lv == LV_LOW || fm->lv == LV_HIGH ? 2 : 5;
  /* hop attacks: the anti-air and the landing are handled before this; wait where the hop lands short */
  if (fm->flags & FM_AIR) {
    if (dist < 900) { hold(g, who, 4, 8, 0); *out = raw_dir(4, fw, 0); return true; }
    if (dist > 1900) { *out = raw_dir(6, fw, 0); return true; }
    *out = raw_dir(5, fw, 0);
    return true;
  }
  /* shots: walk in between them, guarding what is close (the shot guard ran already) */
  if (fm->flags & FM_PROJ) {
    int fast = reach_of(d, M_5K);
    if (dist > fast) { hold(g, who, 6, 8, 0); *out = raw_dir(6, fw, 0); return true; }
    return false;
  }
  /* throws lose to anything already moving: a fast strike before it arrives, from just outside its reach */
  if (fm->flags & FM_THROW) {
    int fastest = M_5P;
    for (int s = 0; s <= M_6K; s++) if (d->mv[s].su < d->mv[fastest].su) fastest = s;
    if (dist <= reach_of(d, M_5K) && r < 30) { normal_slot(g, who, dist <= reach_of(d, fastest) ? fastest : M_5K); *out = raw_dir(5, fw, 0); return true; }
    if (dist < R + 200 && r < 15) { *out = raw_dir(4, fw, 0); return true; }
    if (dist > reach_of(d, M_5K)) { *out = raw_dir(6, fw, 0); return true; }
    *out = raw_dir(5, fw, 0);
    return true;
  }
  /* an invulnerable start cannot be stuffed: stand guarded in its reach and let it come out, the punish does the rest */
  if (fm->inv >= fm->su) {
    if (dist <= R + 250) { hold(g, who, gdir == 2 ? 2 : 5, 12, B_G); *out = raw_dir(gdir, fw, B_G); return true; }
    *out = raw_dir(6, fw, 0);
    return true;
  }
  /* a longer normal than its move: stand just outside its reach and hit it walking in */
  int poke = -1;
  for (int s = 0; s <= M_6K; s++) {
    const ft_move *m = &d->mv[s];
    if (m->mv || (m->lv == LV_HIGH && (fm->flags & FM_CROUCH))) continue; /* a high goes over its crouching move */
    if (reach_of(d, s) > R + 60 && (poke < 0 || m->su < d->mv[poke].su)) poke = s;
  }
  if (poke >= 0) {
    int pr = reach_of(d, poke);
    /* already inside its reach: walking away only gets hit. Guard (the push takes it back out) or get in first */
    if (dist <= R) {
      int quick = M_5P;
      for (int s = 0; s <= M_6K; s++) if (d->mv[s].su < d->mv[quick].su) quick = s;
      if (!om_attacking && r < 25 && d->mv[quick].su <= fm->su && dist <= reach_of(d, quick) - 40) { normal_slot(g, who, quick); *out = raw_dir(5, fw, 0); return true; }
      hold(g, who, gdir, 6, B_G);
      *out = raw_dir(gdir, fw, B_G);
      return true;
    }
    bool cornered = fw > 0 ? me->x <= -FT_WALL + 150 : me->x >= FT_WALL - 150;
    if (dist <= R + 30 && !cornered) { hold(g, who, 4, 5, 0); *out = raw_dir(4, fw, 0); return true; }
    /* it is walking in: the poke goes out where it will be when the poke arrives. It waits for that, not walking to meet it */
    bool coming = op->state == S_WALKF;
    int arrives = dist - (coming ? od->walk * (d->mv[poke].su + 1) : 0);
    if (!om_attacking && arrives <= pr - 20 && (int)r < 40 + lvl * 15) { normal_slot(g, who, poke); *out = raw_dir(5, fw, 0); return true; }
    if (arrives > pr - 20 && !coming) { hold(g, who, 6, 3, 0); *out = raw_dir(6, fw, 0); return true; }
    *out = raw_dir(5, fw, 0);
    return true;
  }
  /* no longer reach: get inside it. Faster normals beat its startup when it is that slow; guard while in its reach */
  int fastest = -1;
  for (int s = 0; s <= M_6K; s++) {
    const ft_move *m = &d->mv[s];
    if (dist <= reach_of(d, s) - 40 && !m->mv && m->su + 2 < fm->su && (fastest < 0 || m->dmg > d->mv[fastest].dmg)) fastest = s;
  }
  if (fastest >= 0 && !om_attacking && r < 50) { normal_slot(g, who, fastest); *out = raw_dir(5, fw, 0); return true; }
  /* its recovery is the time to walk in */
  if (om_attacking && op->t > fm->su + fm->ac) { *out = raw_dir(6, fw, 0); return true; }
  if (om_attacking) { *out = raw_dir(gdir, fw, B_G); return true; }
  if (dist > R) { hold(g, who, 6, 4, 0); *out = raw_dir(6, fw, 0); return true; }
  if ((int)r < 35 + lvl * 10) { hold(g, who, gdir, 8, B_G); *out = raw_dir(gdir, fw, B_G); return true; }
  hold(g, who, 6, 4, 0);
  *out = raw_dir(6, fw, 0);
  return true;
}

ft_input ft_ai(ft_game *g, int who) {
  ft_fighter *me = &g->f[who], *op = &g->f[1 - who];
  const ft_def *d = &g->roster->d[me->def];
  const ft_def *od = &g->roster->d[op->def];
  int lvl = g->cpu_level[who];
  ft_input none = { 0 };
  if (g->phase != P_FIGHT) { g->macro_n[who] = 0; return none; }
  observe(g, who);
  ft_ai_mem *mem = &g->ai[who];
  if (mem->lapse) mem->lapse--;
  else if (rnd(g) % (uint32_t)LV[lvl].lapse == 0) mem->lapse = (uint8_t)(40 + rnd(g) % 50);
  int react = LV[lvl].react + (mem->lapse ? 6 : 0);
  int dist = dist_to(g, who);
  const ft_move *om = op->state == S_ATTACK ? &od->mv[op->mv] : NULL;
  int om_reach = om ? reach_of(od, op->mv) : 0;
  /* a strike it can see coming or a recovery it can see open ends a walk or a held guard */
  if (g->macro_n[who] > 0 && mem->soft && om && op->t >= react) g->macro_n[who] = 0;
  /* a queued motion plays out first */
  if (g->macro_n[who] > 0) {
    ft_input in = g->macro[who][0].in;
    if (--g->macro[who][0].frames <= 0) {
      memmove(&g->macro[who][0], &g->macro[who][1], sizeof g->macro[who][0] * (size_t)(g->macro_n[who] - 1));
      g->macro_n[who]--;
    }
    return in;
  }
  uint32_t r = rnd(g) % 100;
  bool free_ = ft_actionable(me);
  int fav = mem->lapse ? -1 : habit(g, who, lvl);
  /* guard: the opponent's strike is coming and it has seen it, LV.react frames in. Low strikes need a low guard; highs
   * pass over one, so a low guard is also fine against them; a mid is blocked either way. A block pressed in the last
   * frames before the hit is a parry, which it only does on purpose and only sometimes: otherwise a strike it saw too
   * late lands. Out of the strike's reach it does not flinch. */
  if ((free_ || me->state == S_BLOCK) && om && dist < om_reach + 200) {
    if (!(om->flags & (FM_PROJ | FM_THROW)) && op->t <= om->su + om->ac && (me->state == S_BLOCK || op->t >= react)) {
      int chance = LV[lvl].guard;
      bool guarding = me->state == S_BLOCK;
      if (guarding || (int)r < chance) {
        bool low = om->lv == LV_LOW || (om->lv == LV_HIGH && lvl >= 1 && r % 3 == 0);
        if (lvl == 0 && r % 3 == 0) low = !low;
        int to_active = om->su + 1 - op->t;
        if (!guarding && to_active <= FT_PARRY_WINDOW + 1 && (me->parry_lock || (int)(rnd(g) % 100) >= LV[lvl].parry)) return none;
        return raw_dir(low ? 2 : 5, me->facing, B_G);
      }
    }
  }
  if (me->state == S_BLOCK && me->stun) return raw_dir(om && om->lv == LV_LOW ? 2 : 5, me->facing, B_G);
  if (!free_) return none;
  /* waking up with someone near: guard at the height it has been hit at, or punch + kick if it has been thrown */
  if (me->invuln && me->state == S_IDLE && dist < 1900 && (int)r < LV[lvl].wake_guard) {
    if (fav >= 0 && (od->mv[fav].flags & FM_THROW)) return raw_dir(5, me->facing, B_ATTACKS);
    hold(g, who, fav >= 0 && od->mv[fav].lv == LV_LOW ? 2 : 5, 12, B_G);
    return none;
  }
  /* projectile guard */
  for (int p = 0; p < 8; p++)
    if (g->proj[p].live && g->proj[p].owner != who) {
      int pd = g->proj[p].x - me->x; if (pd < 0) pd = -pd;
      if (pd < 1500 && lvl >= 1 && (int)r < (lvl == 1 ? 55 : 85)) return raw_dir(5, me->facing, B_G);
    }
  /* a recovery it has noticed: the hardest normal that fits, walking in first if it has time. A hop landing is one too */
  if (om && !op->y && !mem->lapse && op->t >= om->su + om->ac + LV[lvl].punish_delay && (int)r < LV[lvl].punish) {
    int walk, s = punish_with(d, dist, ft_move_total(om) - op->t, &walk);
    if (s >= 0 && walk) return raw_dir(6, me->facing, 0);
    if (s >= 0) { normal_slot(g, who, s); return none; }
  }
  if (op->state == S_LAND && dist < reach_of(d, M_5P) && (int)r < LV[lvl].punish + 20) { normal(g, who, 5, B_P); return none; }
  /* super when full */
  if (me->meter >= FT_METER_MAX && dist < 3000 && (op->state == S_HIT || op->state == S_BLOCK || (lvl == 2 && r < 20))) {
    special(g, who, d, 3);
    return none;
  }
  /* anti-air: as it comes down, the rising special or a high punch. Up to now it goes under it or guards */
  if (ft_airborne(op) && lvl >= 1) {
    if (dist < 1500 && op->vy < 40 && (int)r < LV[lvl].aa) {
      if (!d->mo[1].charge && r % 2 == 0 && d->mv[M_SP2].inv) special(g, who, d, 1);
      else normal(g, who, 8, B_P);
      return none;
    }
    if (dist < 1200) return raw_dir(5, me->facing, B_G);
  }
  /* someone standing behind a guard: walk up and throw it, a guard has no answer (normal and hard) */
  if (lvl >= 1 && op->state == S_BLOCK && !op->stun && g->ai[who].guard_seen > 60) {
    int tr = d->mv[M_THROW].hit.x1 + d->w_half - 60;
    if (dist > tr) { hold(g, who, 6, 3, 0); return raw_dir(6, me->facing, 0); }
    if (r < 35) { push(g, who, 5, 2, B_P | B_K); return none; }
  }
  /* a habit it has read */
  if (fav >= 0) {
    ft_input in;
    if (answer_habit(g, who, fav, dist, r, &in)) return in;
  }
  /* the lower levels give you time */
  if (lvl == 0 && r % 3 == 0) { hold(g, who, 5, 12, 0); return none; }
  if (lvl == 1 && r % 6 == 0) { hold(g, who, 5, 10, 0); return none; }
  bool op_low_guard = op->state == S_BLOCK && op->crouch_block, op_guard = op->state == S_BLOCK;
  /* it does not walk into a strike it can see starting, at normal and hard */
  if (lvl >= 1 && om && op->t >= react && op->t <= om->su + om->ac && dist > om_reach - 200 && dist < om_reach + 300) return none;
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
    if (r < 53 && lvl >= 1) { push(g, who, 9, 14, 0); push(g, who, 5, 1, B_K); return none; } /* a jump in: up-forward, then kick in the air */
    if (r < 58 && d->mv[M_SP1].flags & FM_PROJ) { special(g, who, d, 0); return none; }
    hold(g, who, 6, 4, 0);
    return raw_dir(6, me->facing, 0);
  }
  /* past the short moves' reach: the long ones or a step in behind a guard, not a punch at the air */
  if (lvl >= 1 && dist > reach_of(d, M_5P) - 40) {
    if (r < 16) { normal(g, who, 5, B_K); return none; }
    if (r < 28) { normal(g, who, 2, B_K); return none; }
    if (r < 44) { hold(g, who, 5, 8, B_G); return none; }
    hold(g, who, 6, 3, 0);
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
  if (r < 74) { hold(g, who, r & 1 ? 2 : 5, 14, B_G); return none; } /* hold a guard a moment */
  if (r < 84) return raw_dir(4, me->facing, 0);
  return none;
}
