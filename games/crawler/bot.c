/* crawler bots: the autopilot app, the soak build and the level check's clear run play with these. A bot walks the
 * maze by tile distances (a breadth-first field from its goal, round the walls), and picks its goal each half second:
 * food when it is low, the exit when the way is open, else a key, else a door it can open (keys in hand); things on
 * the floor near its way; the spawners in sight. It shoots what it sees (standing, as the controls make everyone do),
 * fights what touches it and uses a potion on a crowd. Deterministic: its memory is per player and fed by the sim. */
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

static struct bot_mem {
  int goal_x, goal_y, goal_kind, replan, stuck, last_tx, last_ty, idle, wander, floor, floor_tick;
  uint16_t dist[MZ_MAX * MZ_MAX];
  bool have;
} B[MAXP];

enum { G_NONE, G_EXIT, G_KEY, G_DOOR, G_FOOD, G_ITEM, G_TEAM, G_SPAWNER };

/* a tile a bot walks: open, and not a teleporter pad (it would carry the team off its way) */
static bool passable(const sim *s, int x, int y) {
  if (!mz_in(&s->m, x, y) || sim_solid(s, x, y)) return false;
  for (int k = 0; k < s->m.ntele; k++)
    if (s->m.tele[k][0] == x && s->m.tele[k][1] == y) return false;
  return true;
}

/* tile distances from (gx, gy) to everywhere a bot can walk (the goal itself may be solid: a door, a spawner) */
static void field(const sim *s, struct bot_mem *m, int gx, int gy) {
  static int q[MZ_MAX * MZ_MAX];
  const int W = s->m.w;
  for (int i = 0; i < MZ_MAX * MZ_MAX; i++) m->dist[i] = 0xffff;
  int qh = 0, qt = 0;
  m->dist[gy * W + gx] = 0, q[qt++] = gy * W + gx;
  static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
  while (qh < qt) {
    int c = q[qh++], x = c % W, y = c / W;
    for (int d = 0; d < 4; d++) {
      int nx = x + DX[d], ny = y + DY[d];
      if (!passable(s, nx, ny) || m->dist[ny * W + nx] != 0xffff) continue;
      m->dist[ny * W + nx] = (uint16_t)(m->dist[c] + 1);
      q[qt++] = ny * W + nx;
    }
  }
}

/* the reach of a player: tile steps from it to everywhere (for choosing goals) */
static uint16_t REACH[MZ_MAX * MZ_MAX];
static void reach_from(const sim *s, int x0, int y0) {
  static int q[MZ_MAX * MZ_MAX];
  const int W = s->m.w;
  for (int i = 0; i < MZ_MAX * MZ_MAX; i++) REACH[i] = 0xffff;
  int qh = 0, qt = 0;
  if (!mz_in(&s->m, x0, y0)) return;
  REACH[y0 * W + x0] = 0, q[qt++] = y0 * W + x0;
  static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
  while (qh < qt) {
    int c = q[qh++], x = c % W, y = c / W;
    for (int d = 0; d < 4; d++) {
      int nx = x + DX[d], ny = y + DY[d];
      if (!passable(s, nx, ny) || REACH[ny * W + nx] != 0xffff) continue;
      REACH[ny * W + nx] = (uint16_t)(REACH[c] + 1);
      q[qt++] = ny * W + nx;
    }
  }
}
/* steps to a tile next to (x, y) (for a solid goal: a door, a spawner) */
static int reach_next_to(const sim *s, int x, int y) {
  int best = 0xffff;
  static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
  for (int d = 0; d < 4; d++) {
    int nx = x + DX[d], ny = y + DY[d];
    if (mz_in(&s->m, nx, ny) && REACH[ny * s->m.w + nx] < best) best = REACH[ny * s->m.w + nx];
  }
  return best;
}

/* who leads: the first living player holding a key (only they can open a door), else the first living one */
static int bot_leader(const sim *s) {
  for (int k = 0; k < MAXP; k++)
    if (s->p[k].active && !s->p[k].down && s->p[k].keys > 0) return k;
  for (int k = 0; k < MAXP; k++)
    if (s->p[k].active && !s->p[k].down) return k;
  return -1;
}

static void choose(const sim *s, int i, struct bot_mem *m) {
  const player *p = &s->p[i];
  const maze *mz = &s->m;
  int tx = mz_tx(mz, p->x), ty = mz_ty(mz, p->y);
  reach_from(s, tx, ty);
  int best_kind = G_NONE, bx = tx, by = ty, bd = 0xffff;
#define CONSIDER(K, X, Y, D) do { if ((D) < bd) bd = (D), best_kind = (K), bx = (X), by = (Y); } while (0)
  /* the exit when the way is open (the main one) */
  int exit_d = 0xffff, ex = 0, ey = 0;
  for (int k = 0; k < mz->nexit; k++) {
    if (mz->exit[k][2] != 1) continue;
    int d = REACH[mz->exit[k][1] * mz->w + mz->exit[k][0]];
    if (d < exit_d) exit_d = d, ex = mz->exit[k][0], ey = mz->exit[k][1];
  }
  /* food when low; things on the floor near; keys */
  int key_d = 0xffff, kx = 0, ky = 0, food_d = 0xffff, fx = 0, fy = 0, item_d = 0xffff, ix = 0, iy = 0;
  for (int k = 0; k < MAXG; k++) {
    const pickup *g = &s->g[k];
    if (!g->alive) continue;
    int gx = mz_tx(mz, g->x), gy = mz_ty(mz, g->y);
    if (!mz_in(mz, gx, gy)) continue;
    int d = REACH[gy * mz->w + gx];
    if (d == 0xffff) continue;
    if (g->kind != IT_KEY && (fabsf(g->x - s->cam_x) > TETHER_HW * 0.8f || fabsf(g->y - s->cam_y) > TETHER_HH * 0.8f)) continue; /* only what the team's screen holds */
    if (g->kind == IT_KEY && p->keys < 9 && d < key_d) key_d = d, kx = gx, ky = gy;
    else if (g->kind >= IT_ROAST && g->kind <= IT_PLATE && d < food_d) food_d = d, fx = gx, fy = gy;
    else if (g->kind == IT_POTION && p->potions < 2 && d < 16 && d < item_d) item_d = d / 3, ix = gx, iy = gy; /* a potion is worth a detour */
    else if (g->kind != IT_CHEST_TRAP && g->kind != IT_KEY && d < item_d) item_d = d, ix = gx, iy = gy;
  }
  if (p->hp < 400 && food_d < 40) CONSIDER(G_FOOD, fx, fy, 0);
  /* a spawner close by comes first: the stream stops with it (the way a player plays it) */
  for (int k = 0; k < s->nsp; k++) {
    const spawner *sp = &s->sp[k];
    if (!sp->alive) continue;
    int d = reach_next_to(s, sp->tx, sp->ty);
    if (d <= 10) CONSIDER(G_SPAWNER, sp->tx, sp->ty, d);
  }
  if (exit_d < 0xffff) {
    CONSIDER(G_EXIT, ex, ey, exit_d + 8);
    if (item_d < 6) CONSIDER(G_ITEM, ix, iy, item_d); /* a short way off the path: take it */
    if (key_d < 8) CONSIDER(G_KEY, kx, ky, key_d);
  } else {
    /* the way is shut: a door if a key is in hand, else the nearest key; doors first that lead somewhere new */
    if (p->keys > 0) {
      for (int y = 0; y < mz->h; y++)
        for (int x = 0; x < mz->w; x++)
          if (s->t[y][x] == T_DOOR) { int d = reach_next_to(s, x, y); if (d < 0xffff) CONSIDER(G_DOOR, x, y, d); }
    }
    if (key_d < 0xffff) CONSIDER(G_KEY, kx, ky, p->keys > 0 ? key_d + 6 : key_d);
    if (best_kind == G_NONE) { /* nothing to do: breakable walls and trap plates, then items, then wander the reach */
      for (int y = 0; y < mz->h; y++)
        for (int x = 0; x < mz->w; x++)
          if (s->t[y][x] == T_BREAK) { int d = reach_next_to(s, x, y); if (d < 0xffff) CONSIDER(G_SPAWNER, x, y, d + 4); }
      for (int k = 0; k < mz->ntrap; k++)
        if (mz->trap[k][2]) { int d = REACH[mz->trap[k][1] * mz->w + mz->trap[k][0]]; if (d < 0xffff) CONSIDER(G_ITEM, mz->trap[k][0], mz->trap[k][1], d); }
      if (item_d < 0xffff) CONSIDER(G_ITEM, ix, iy, item_d + 10);
    }
  }
  if (best_kind == G_NONE) { /* the spawners in reach (they may block the way) */
    for (int k = 0; k < s->nsp; k++) {
      const spawner *sp = &s->sp[k];
      if (!sp->alive) continue;
      int d = reach_next_to(s, sp->tx, sp->ty);
      if (d < 0xffff) CONSIDER(G_SPAWNER, sp->tx, sp->ty, d);
    }
  }
  if (best_kind == G_NONE) { /* wander to the farthest reached tile */
    int far = 0;
    for (int k = 0; k < mz->w * mz->h; k++)
      if (REACH[k] != 0xffff && REACH[k] > far && ((k * 7 + m->wander) % 5) == 0) far = REACH[k], bx = k % mz->w, by = k / mz->w, best_kind = G_ITEM;
  }
  /* the team moves together: a bot that is not the first living one follows the leader's goal (the screen holds them
   * together, so two bots pulling two ways would hold each other still), unless it is low on health */
  int leader = bot_leader(s);
  if (leader >= 0 && leader != i && B[leader].have && best_kind != G_FOOD && B[leader].goal_kind != G_NONE) {
    int lx = B[leader].goal_x, ly = B[leader].goal_y, lk = B[leader].goal_kind;
    bool solid_goal = lk == G_DOOR || lk == G_SPAWNER;
    int d = solid_goal ? reach_next_to(s, lx, ly) : REACH[ly * mz->w + lx];
    if (d < 0xffff) bx = lx, by = ly, best_kind = lk;
    /* far behind the leader: its footsteps first (the way to the goal may lead off from the team) */
    const player *L = &s->p[leader];
    int ltx = mz_tx(mz, L->x), lty = mz_ty(mz, L->y);
    float dx = L->x - p->x, dy = L->y - p->y;
    if (dx * dx + dy * dy > 5.0f * 5.0f && mz_in(mz, ltx, lty) && REACH[lty * mz->w + ltx] < 0xffff) bx = ltx, by = lty, best_kind = G_TEAM;
  }
  m->goal_x = bx, m->goal_y = by, m->goal_kind = best_kind;
  field(s, m, bx, by);
  m->have = true;
}

void crawler_bot(const sim *s, int i, input *in) {
  const player *p = &s->p[i];
  struct bot_mem *m = &B[i];
  memset(in, 0, sizeof *in);
  if (s->tick <= 1 || m->floor != s->floor || m->floor_tick > s->floor_ticks) memset(m, 0, sizeof *m), m->floor = s->floor;
  m->floor_tick = s->floor_ticks;
  if (p->down) { in->join = true; return; }
  if (s->state != ST_PLAY) return;
  const maze *mz = &s->m;
  int tx = mz_tx(mz, p->x), ty = mz_ty(mz, p->y);
  if (--m->replan <= 0 || !m->have) choose(s, i, m), m->replan = 30;
  /* stuck on one tile too long: wander a while */
  if (tx == m->last_tx && ty == m->last_ty) m->stuck++;
  else m->stuck = 0, m->last_tx = tx, m->last_ty = ty;
  if (m->stuck > 240) m->wander++, m->stuck = 0, m->replan = 0;
  /* the step: toward the neighbour tile nearest the goal */
  float mx = 0, my = 0;
  {
    const int W = mz->w;
    int best = mz_in(mz, tx, ty) ? m->dist[ty * W + tx] : 0xffff, nx = tx, ny = ty;
    static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    for (int d = 0; d < 8; d++) {
      int ax = tx + DX[d], ay = ty + DY[d];
      if (!mz_in(mz, ax, ay)) continue;
      if (d >= 4 && (!passable(s, tx + DX[d], ty) || !passable(s, tx, ty + DY[d]))) continue;
      int dd = m->dist[ay * W + ax];
      bool goal = ax == m->goal_x && ay == m->goal_y;
      if (goal) dd = 0;
      if (dd < best) best = dd, nx = ax, ny = ay;
    }
    float gx = mz_wx(mz, (float)nx), gy = mz_wz(mz, (float)ny);
    if (nx == tx && ny == ty) gx = mz_wx(mz, (float)m->goal_x), gy = mz_wz(mz, (float)m->goal_y);
    float dx = gx - p->x, dy = gy - p->y, l = sqrtf(dx * dx + dy * dy);
    if (l > 0.05f) mx = dx / l, my = dy / l;
  }
  /* the leader waits for a follower fallen far behind (one screen holds the team) */
  if (bot_leader(s) == i) {
    for (int k = 0; k < MAXP; k++) {
      const player *q = &s->p[k];
      if (k == i || !q->active || q->down) continue;
      if (fabsf(q->x - p->x) > TETHER_HW * 1.1f || fabsf(q->y - p->y) > TETHER_HH * 1.1f) mx = my = 0;
    }
  }
  /* what it sees: the nearest monster in sight within 9 m (or the spawner it is after) */
  float best = 9.0f * 9.0f, ax = 0, ay = 0;
  int crowd = 0;
  bool target = false, close = false;
  for (int k = 0; k < MAXE; k++) {
    const enemy *e = &s->e[k];
    if (!e->alive || e->kind == EK_DREAD) continue;
    float dx = e->x - p->x, dy = e->y - p->y, d = dx * dx + dy * dy;
    if (fabsf(dx) < VIEW_HW && fabsf(dy) < VIEW_HH) crowd++;
    if (d >= best) continue;
    /* in sight: no wall between */
    float l = sqrtf(d);
    bool clear = true;
    for (float t = 0.5f; t < l && clear; t += 0.5f) clear = !sim_wall_at(s, p->x + dx / l * t, p->y + dy / l * t);
    if (!clear) continue;
    best = d, ax = dx / l, ay = dy / l, target = true, close = l < 1.4f;
  }
  /* a spawner in sight is the better target (the stream stops with it), unless something is at arm's length */
  if (best > 2.5f * 2.5f) {
    float sd = 10.0f * 10.0f;
    for (int k = 0; k < s->nsp; k++) {
      const spawner *sp = &s->sp[k];
      if (!sp->alive) continue;
      float dx = sp->x - p->x, dy = sp->y - p->y, d = dx * dx + dy * dy;
      if (d > sd) continue;
      float l = sqrtf(d);
      bool clear = true;
      for (float t = 0.5f; t < l && clear; t += 0.25f) { /* the shot's own line, right up to the spawner's tile */
        int qx = mz_tx(mz, p->x + dx / l * t), qy = mz_ty(mz, p->y + dy / l * t);
        if (qx == sp->tx && qy == sp->ty) break;
        clear = !sim_solid(s, qx, qy);
      }
      if (clear) sd = d, ax = dx / l, ay = dy / l, target = true, best = d, m->goal_kind = m->goal_kind == G_EXIT ? G_EXIT : m->goal_kind;
    }
    if (sd < 10.0f * 10.0f) m->idle = 1; /* (shooting a spawner is never a burst: it stands until the spawner falls) */
    else m->idle = 0;
  } else m->idle = 0;
  if (m->goal_kind == G_SPAWNER || m->goal_kind == G_DOOR) { /* at a breakable wall or a spawner it means to clear: aim at it */
    float gx = mz_wx(mz, (float)m->goal_x), gy = mz_wz(mz, (float)m->goal_y), dx = gx - p->x, dy = gy - p->y, l = sqrtf(dx * dx + dy * dy);
    if (l < 2.6f && l > 0.1f && m->goal_kind == G_SPAWNER) ax = dx / l, ay = dy / l, target = true;
  }
  if (target) /* never into a friend */
    for (int k = 0; k < MAXP; k++) {
      const player *q = &s->p[k];
      if (k == i || !q->active || q->down) continue;
      float dx = q->x - p->x, dy = q->y - p->y, l = sqrtf(dx * dx + dy * dy);
      if (l < 0.01f || l * l > best + 1) continue;
      if ((dx * ax + dy * ay) / l > 0.93f) target = false;
    }
  if (target && best > 6.0f * 6.0f && !m->idle && m->goal_kind != G_SPAWNER && (s->tick + i * 23) % 120 < 70) target = false; /* far off: keep going, shooting in bursts */
  if (target && !(close && p->cls == CL_BRUTE)) { /* stand and shoot (the stick turns the shot) */
    in->mx = ax, in->my = ay, in->fire = true;
    if ((s->tick + i * 11) % 90 < 12) in->fire = false, in->mx = mx, in->my = my; /* keep walking now and then */
  } else if (target && close) {
    in->mx = ax, in->my = ay; /* the Brute walks into them: melee */
  } else in->mx = mx, in->my = my;
  in->magic = (p->potions > 0 && (crowd > 28 || (p->hp < 250 && crowd > 10)) && (s->tick + i * 17) % 60 == 0) || (p->charm[CH_FIREBOMBS] > 0 && crowd > 15 && (s->tick + i * 13) % 90 == 0);
  (void)G_TEAM;
}
/* development: what bot i is after (the soak's trace) */
void crawler_bot_goal(int i, int *kind, int *x, int *y) { *kind = B[i].goal_kind, *x = B[i].goal_x, *y = B[i].goal_y; }
