/* hordeshooter rules: 1-4 players hold a walled yard against a horde.
 *
 *   enemies   every enemy is an agent of the engine's crowd (a dynamic
 *             navmesh, ORCA avoidance). A grid flow field from the players
 *             gives each one its heading; near a player it goes straight at
 *             them. Thousands at a time.
 *   weapons   twin stick: move, aim, hold to fire. XP gems level a player up
 *             and each level adds the next upgrade in the ladder.
 *   world     crates, barrels (they explode), cinder walls (explosions break
 *             them), pillars. A destroyed piece leaves the navmesh at once.
 *   waves     a wave every 30 s, a boss every fifth.
 * Everything here runs on the fixed tick with cartwheel math and cw_rng: the
 * same inputs give the same game on every host. */
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

static const char *const UP_NAME[] = { "Rapid fire", "Extra barrel", "Heavy rounds", "Piercing", "Rockets", "Boots", "Magnet", "Bigger blasts", "Armor" };
static const uint8_t LADDER[] = { 0, 1, 2, 3, 4, 0, 5, 1, 6, 7, 2, 8, 0, 3, 7, 4, 1, 5 };
const char *sim_upgrade_name(int u) { return UP_NAME[u % 9]; }

static float rf(sim *s) { return cw_rng_float(&s->rng); }
static void ev(sim *s, int kind, float x, float y, float z, float w, int a, int b) {
  if (s->ev_n >= MAXEV) return;
  s->ev[s->ev_n++] = (event){ (uint8_t)kind, (uint8_t)a, (uint8_t)b, x, y, z, w };
}

/* ── enemies ────────────────────────────────────────────────────────── */

typedef struct ekind { float radius, speed_lo, speed_hi; int hp_base; float hp_wave; int dmg, xp; } ekind;
static const ekind EK[EK_KINDS] = {
  { 0.34f, 3.0f, 4.4f, 2, 0.34f, 7, 1 },
  { 0.72f, 1.8f, 2.4f, 28, 4.0f, 18, 6 },
  { 0.55f, 1.4f, 2.0f, 8, 0.8f, 6, 3 },
  { 1.7f, 1.5f, 1.9f, 700, 260.0f, 30, 90 },
};

static int spawn_enemy(sim *s, int kind, float x, float y) {
  int slot = -1;
  for (int i = 0; i < MAXE; i++)
    if (!s->e[i].alive) { slot = i; break; }
  if (slot < 0) return -1;
  cw_crowd_agent_params ap = cw_crowd_agent_params_default();
  ap.radius = EK[kind].radius;
  ap.height = 1.6f;
  float sp = EK[kind].speed_lo + (EK[kind].speed_hi - EK[kind].speed_lo) * rf(s);
  ap.max_speed = sp;
  ap.max_accel = kind == EK_RUNNER ? 16 : 9;
  ap.neighbor_dist = 2.5f;
  ap.max_neighbors = kind == EK_RUNNER ? 5 : 8;
  ap.time_horizon = 0.7f;
  ap.avoidance_priority = kind == EK_RUNNER ? 0.4f : kind == EK_BOSS ? 1.0f : 0.7f;
  cw_crowd_agent a = cw_crowd_add(s->crowd, (t3_vec3){ x, 0, y }, &ap);
  if (CW_HANDLE_IS_NULL(a)) return -1;
  enemy *e = &s->e[slot];
  *e = (enemy){ .alive = true, .kind = (uint8_t)kind, .x = x, .y = y, .agent = a, .speed = sp };
  {
    float w = (float)s->wave, f = 1.0f + 0.1f * w + 0.008f * w * w; /* the horde toughens with the wave: x1.1 at 1, x2.8 at 10, x6.2 at 20 */
    e->hp = kind == EK_BOSS ? EK[kind].hp_base + (int)(EK[kind].hp_wave * w) : CW_MAX(1, (int)((float)EK[kind].hp_base * f + 0.5f));
  }
  /* more players, tougher enemies: 25% per extra player, a boss 60% */
  e->hp = (int)((float)e->hp * (1.0f + (kind == EK_BOSS ? 0.6f : 0.25f) * (float)(s->players > 1 ? s->players - 1 : 0)));
  if (kind == EK_BOSS) s->boss_maxhp = e->hp;
  e->face = rf(s) * 6.28f;
  e->atk = 0;
  s->nenemies++;
  if (kind == EK_BOSS) s->boss_alive++;
  return slot;
}

static void queue_boom(sim *s, float x, float y, float r, float dmg, int delay, int owner, bool big) {
  for (int i = 0; i < MAXX; i++)
    if (s->x[i].r == 0) {
      s->x[i] = (boom){ x, y, r, dmg, delay, (uint8_t)owner, big };
      return;
    }
}

static void drop(sim *s, int kind, float x, float y) {
  for (int i = 0; i < MAXG; i++)
    if (!s->g[i].alive) {
      float a = rf(s) * 6.28f, v = 1.5f + rf(s) * 2.5f;
      s->g[i] = (pickup){ true, (uint8_t)kind, x, y, cw_cos(a) * v, cw_sin(a) * v, 60 * 40 };
      return;
    }
}

/* each upgrade stops at a top level, so power grows on a curve a design can keep up with */
static const int UP_MAX[9] = { 7, 4, 8, 4, 4, 5, 5, 5, 6 };
static int *up_slot(player *p, int u) {
  int *f[9] = { &p->rate, &p->shots, &p->dmg, &p->pierce, &p->rocket, &p->speed, &p->magnet, &p->radius, &p->armor };
  return f[u % 9];
}

static void apply_upgrade(sim *s, player *p, int pi, int u) {
  p->last_up = u;
  int *lv = up_slot(p, u);
  if (*lv < UP_MAX[u % 9]) ++*lv;
  p->hp = CW_MIN(p->hp + 25, 100.0f);
  ev(s, EV_LEVEL, p->x, p->y, 0, 0, pi, u);
}

/* three different upgrades to choose from, none of them at its top level. The ladder's next one is
 * offered first, so a player who ignores the offer still grows; once nearly everything is maxed the
 * offer repeats what is left (a heal and a ring of sparks at the end of the road) */
static void roll_offer(sim *s, player *p) {
  int avail[9], n = 0;
  for (int u = 0; u < 9; u++)
    if (*up_slot(p, u) < UP_MAX[u]) avail[n++] = u;
  if (n == 0) avail[n++] = 8;
  int first = LADDER[(p->level - 2) % (int)sizeof LADDER];
  p->offer[0] = *up_slot(p, first) < UP_MAX[first] ? first : avail[(int)(cw_rng_u32(&s->rng) % (uint32_t)n)];
  for (int k = 1; k < 3; k++) {
    int u = p->offer[0], tries = 0;
    do {
      u = avail[(int)(cw_rng_u32(&s->rng) % (uint32_t)n)];
    } while (tries++ < 20 && (u == p->offer[0] || (k == 2 && u == p->offer[1])));
    p->offer[k] = u;
  }
  p->offer_ticks = 60 * 8; /* the first offer is taken when the time runs out */
}

static void level_up(sim *s, player *p, int pi) {
  (void)pi;
  p->level++;
  p->xp -= p->xp_next;
  p->xp_next = 6 + p->level * 5 + p->level * p->level * 3 / 2; /* a choice every 25 to 60 s early, still about a minute late */
  if (p->offer_ticks > 0) p->pending++;
  else roll_offer(s, p);
}

static void kill_enemy(sim *s, int i, int owner, float dx, float dy) {
  enemy *e = &s->e[i];
  if (!e->alive) return;
  e->alive = false;
  cw_crowd_remove(s->crowd, e->agent);
  s->nenemies--;
  s->kills++;
  s->killed_total++;
  if (owner >= 0 && owner < MAXP) {
    s->p[owner].kills++;
    s->p[owner].score += 10 * (1 + e->kind * 4);
  }
  ev(s, EV_KILL, e->x, e->y, dx, dy, e->kind, owner);
  int xp = EK[e->kind].xp;
  int gems = e->kind == EK_BOSS ? 24 : e->kind == EK_BRUTE ? 4 : 1;
  if (e->kind != EK_RUNNER || rf(s) < 6.0f / (6.0f + (float)s->wave)) /* a runner leaves a gem less often as the waves thicken */
    for (int k = 0; k < gems; k++) drop(s, 0, e->x, e->y);
  (void)xp;
  float r = rf(s);
  if (e->kind != EK_RUNNER ? r < 0.35f : r < 0.012f) drop(s, 1, e->x, e->y);
  if (e->kind == EK_BLOATER) queue_boom(s, e->x, e->y, 3.4f, 14, 4, owner, false);
  if (e->kind == EK_BOSS) {
    s->boss_alive--;
    for (int k = 0; k < 14; k++) {
      float a = rf(s) * 6.28f, d = rf(s) * 2.2f;
      queue_boom(s, e->x + cw_cos(a) * d, e->y + cw_sin(a) * d, 4.2f, 40, 6 + k * 7, owner, true);
    }
  }
}

/* ── obstacles ──────────────────────────────────────────────────────── */

static void add_obstacle(sim *s, int kind, float x, float y, float hx, float hy) {
  for (int i = 0; i < MAXO; i++)
    if (!s->o[i].alive) {
      obstacle *o = &s->o[i];
      o->alive = true;
      o->kind = (uint8_t)kind;
      o->x = x, o->y = y, o->hx = hx, o->hy = hy;
      o->hp = o->maxhp = kind == OK_CRATE ? 14 : kind == OK_BARREL ? 6 : kind == OK_WALL ? 90 : 1 << 20;
      o->nav = cw_nav_obstacle_add_box(s->nav, (t3_vec3){ x, 0, y }, (t3_vec3){ hx, 1.5f, hy }, 0);
      s->nav_dirty = true;
      return;
    }
}

static void remove_obstacle(sim *s, int i) {
  obstacle *o = &s->o[i];
  o->alive = false;
  if (o->nav) cw_nav_obstacle_remove(s->nav, o->nav); /* a full change queue is applied by the engine */
  o->nav = 0;
  s->nav_dirty = true;
  s->flow_age = 1000;
}

static void hurt_obstacle(sim *s, int i, int dmg) {
  obstacle *o = &s->o[i];
  if (!o->alive || o->kind == OK_PILLAR) return;
  o->hp -= dmg;
  if (o->hp > 0) return;
  switch (o->kind) {
  case OK_CRATE:
    ev(s, EV_CRATE, o->x, o->y, 0, 0, 0, 0);
    if (rf(s) < 0.5f) drop(s, 0, o->x, o->y);
    if (rf(s) < 0.15f) drop(s, 1, o->x, o->y);
    break;
  case OK_BARREL:
    queue_boom(s, o->x, o->y, 4.2f, 30, 3, MAXP, true);
    ev(s, EV_BARREL, o->x, o->y, 0, 0, 0, 0);
    break;
  default: ev(s, EV_WALL, o->x, o->y, o->hx, o->hy, 0, 0); break;
  }
  remove_obstacle(s, i);
}

static bool in_box(const obstacle *o, float x, float y, float r) {
  return x > o->x - o->hx - r && x < o->x + o->hx + r && y > o->y - o->hy - r && y < o->y + o->hy + r;
}

static void build_flow_blocked(sim *s) {
  memset(s->blocked, 0, sizeof s->blocked);
  for (int i = 0; i < MAXO; i++) {
    const obstacle *o = &s->o[i];
    if (!o->alive) continue;
    int x0 = (int)(o->x - o->hx + HALF - 0.3f), x1 = (int)(o->x + o->hx + HALF + 0.3f);
    int y0 = (int)(o->y - o->hy + HALF - 0.3f), y1 = (int)(o->y + o->hy + HALF + 0.3f);
    for (int y = CW_MAX(y0, 0); y <= CW_MIN(y1, FW - 1); y++)
      for (int x = CW_MAX(x0, 0); x <= CW_MIN(x1, FW - 1); x++) s->blocked[y * FW + x] = 1;
  }
}

/* breadth first distances from every living player's cell: one field, one
 * lookup per enemy */
static void build_flow(sim *s) {
  static int16_t queue[FW * FW];
  build_flow_blocked(s);
  for (int i = 0; i < FW * FW; i++) s->flow[i] = 0xffff;
  int qh = 0, qt = 0;
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (!p->active || p->down) continue;
    int cx = CW_CLAMP((int)(p->x + HALF), 0, FW - 1), cy = CW_CLAMP((int)(p->y + HALF), 0, FW - 1);
    if (s->flow[cy * FW + cx] == 0) continue;
    s->flow[cy * FW + cx] = 0;
    queue[qt++] = (int16_t)(cy * FW + cx);
  }
  static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
  while (qh < qt) {
    int c = queue[qh++], cx = c % FW, cy = c / FW;
    uint16_t d = s->flow[c];
    for (int k = 0; k < 8; k++) {
      int nx = cx + DX[k], ny = cy + DY[k];
      if (nx < 0 || ny < 0 || nx >= FW || ny >= FW) continue;
      int n = ny * FW + nx;
      if (s->blocked[n] || s->flow[n] != 0xffff) continue;
      if (k >= 4 && (s->blocked[cy * FW + nx] || s->blocked[ny * FW + cx])) continue; /* no cutting corners */
      s->flow[n] = (uint16_t)(d + (k < 4 ? 2 : 3));
      queue[qt++] = (int16_t)n;
    }
  }
  s->flow_age = 0;
}

/* ── setup ──────────────────────────────────────────────────────────── */

void sim_free(sim *s) {
  if (!CW_HANDLE_IS_NULL(s->nav)) cw_navmesh_free(s->nav);
  s->nav = (cw_navmesh){ 0 };
  s->crowd = (cw_crowd){ 0 };
}

static void layout(sim *s) {
  /* a ring of cinder walls with gaps, pillars, scattered crates and barrels */
  for (int k = 0; k < 4; k++) {
    float a0 = (float)k * 1.5708f + 0.2f;
    for (int j = 0; j < 7; j++) {
      float a = a0 + (float)j * 0.17f;
      float r = 17;
      add_obstacle(s, OK_WALL, cw_cos(a) * r, cw_sin(a) * r, 1.0f, 1.0f);
    }
  }
  for (int k = 0; k < 8; k++) {
    float a = (float)k * 0.7854f + 0.4f;
    add_obstacle(s, OK_PILLAR, cw_cos(a) * 28, cw_sin(a) * 28, 1.2f, 1.2f);
  }
  for (int k = 0; k < 90; k++) {
    float x = (rf(s) - 0.5f) * 2 * (HALF - 3), y = (rf(s) - 0.5f) * 2 * (HALF - 3);
    if (x * x + y * y < 36) continue;
    bool clash = false;
    for (int i = 0; i < MAXO; i++)
      if (s->o[i].alive && in_box(&s->o[i], x, y, 1.4f)) clash = true;
    if (clash) continue;
    if (rf(s) < 0.42f) add_obstacle(s, OK_BARREL, x, y, 0.5f, 0.5f);
    else add_obstacle(s, OK_CRATE, x, y, 0.65f, 0.65f);
  }
}

void sim_init(sim *s, int players, uint32_t seed, int start_wave) {
  sim_free(s);
  memset(s, 0, sizeof *s);
  cw_rng_seed(&s->rng, seed);
  s->players = players;
  cw_nav_geom g = { 0 };
  cw_nav_geom_add_box(&g, (t3_vec3){ 0, -0.25f, 0 }, (t3_vec3){ HALF, 0.25f, HALF }, CW_NAV_AREA_WALK);
  cw_nav_config c = cw_nav_config_default();
  c.dynamic = true;
  c.max_obstacles = 300;
  c.cell_size = 0.25f;
  c.cell_height = 0.2f;
  c.agent_radius = 0.4f;
  c.agent_height = 1.6f;
  c.tile_size = 64;
  s->nav = cw_navmesh_build(&c, &g);
  cw_nav_geom_free(&g);
  s->crowd = cw_crowd_new(s->nav, MAXE);
  cw_crowd_set_plan_budget(s->crowd, 8, 0);
  layout(s);
  cw_nav_update(s->nav);
  for (int i = 0; i < players; i++) {
    player *p = &s->p[i];
    float a = (float)i * 6.2832f / (float)players;
    *p = (player){ .active = true, .hp = 100, .x = cw_cos(a) * 2.0f, .y = cw_sin(a) * 2.0f, .xp_next = 8, .level = 1, .aim = a };
  }
  s->wave = start_wave > 0 ? start_wave : 1;
  s->spawn_cap = 40 + s->wave * 90;
  build_flow(s);
}

/* ── per tick ───────────────────────────────────────────────────────── */

static void build_grid(sim *s) {
  static int16_t cnt[41 * 41 + 1];
  memset(cnt, 0, sizeof cnt);
  for (int i = 0; i < MAXE; i++) {
    if (!s->e[i].alive) continue;
    int cx = CW_CLAMP((int)((s->e[i].x + HALF) * 0.5f), 0, 40), cy = CW_CLAMP((int)((s->e[i].y + HALF) * 0.5f), 0, 40);
    cnt[cy * 41 + cx + 1]++;
  }
  for (int i = 1; i <= 41 * 41; i++) cnt[i] = (int16_t)(cnt[i] + cnt[i - 1]);
  memcpy(s->cell_start, cnt, sizeof cnt);
  static int16_t fill[41 * 41];
  memcpy(fill, cnt, sizeof fill);
  for (int i = 0; i < MAXE; i++) {
    if (!s->e[i].alive) continue;
    int cx = CW_CLAMP((int)((s->e[i].x + HALF) * 0.5f), 0, 40), cy = CW_CLAMP((int)((s->e[i].y + HALF) * 0.5f), 0, 40);
    s->cell_idx[fill[cy * 41 + cx]++] = (int16_t)i;
  }
}

/* enemies within r of (x, y); fn(i) per enemy */
typedef void (*enemy_fn)(sim *s, int i, void *u);
static void each_enemy(sim *s, float x, float y, float r, enemy_fn fn, void *u) {
  int x0 = CW_CLAMP((int)((x - r + HALF) * 0.5f), 0, 40), x1 = CW_CLAMP((int)((x + r + HALF) * 0.5f), 0, 40);
  int y0 = CW_CLAMP((int)((y - r + HALF) * 0.5f), 0, 40), y1 = CW_CLAMP((int)((y + r + HALF) * 0.5f), 0, 40);
  for (int cy = y0; cy <= y1; cy++)
    for (int cx = x0; cx <= x1; cx++)
      for (int k = s->cell_start[cy * 41 + cx]; k < s->cell_start[cy * 41 + cx + 1]; k++) {
        int i = s->cell_idx[k];
        enemy *e = &s->e[i];
        if (!e->alive) continue;
        float dx = e->x - x, dy = e->y - y, rr = r + EK[e->kind].radius;
        if (dx * dx + dy * dy <= rr * rr) fn(s, i, u);
      }
}

typedef struct bullet_ctx { int b; bool stop; } bullet_ctx;
static void bullet_hit(sim *s, int i, void *u) {
  bullet_ctx *c = u;
  bullet *b = &s->b[c->b];
  if (c->stop || !b->alive) return;
  enemy *e = &s->e[i];
  if (!e->alive) return;
  float d = b->dmg;
  e->hp -= (int)d;
  e->flash = 4;
  float k = 0.9f / (1.0f + (float)(e->kind) * 1.5f);
  e->kx += b->vx * 0.012f * k;
  e->ky += b->vy * 0.012f * k;
  ev(s, EV_HIT, b->x, b->y, b->vx, b->vy, e->kind, 0);
  if (e->hp <= 0) kill_enemy(s, i, b->owner, b->vx, b->vy);
  if (b->kind == 1) { /* a rocket bursts on the first thing it touches */
    queue_boom(s, b->x, b->y, 2.4f + (float)s->p[b->owner].radius * 0.5f, 3.0f + b->dmg * 2.0f, 0, b->owner, false);
    b->alive = false;
    c->stop = true;
    return;
  }
  if (b->pierce-- <= 0) {
    b->alive = false;
    c->stop = true;
  }
}

typedef struct boom_ctx { const boom *bm; } boom_ctx;
static void boom_hit(sim *s, int i, void *u) {
  const boom *bm = ((boom_ctx *)u)->bm;
  enemy *e = &s->e[i];
  float dx = e->x - bm->x, dy = e->y - bm->y, d = cw_clampf(1 - (dx * dx + dy * dy) / (bm->r * bm->r), 0.2f, 1);
  int dmg = (int)(bm->dmg * d);
  e->hp -= dmg;
  e->flash = 6;
  float l = cw_v2_length(cw_v2(dx, dy));
  float ux = l > 0.01f ? dx / l : 1, uy = l > 0.01f ? dy / l : 0;
  float push = (bm->big ? 0.5f : 0.35f) * d / (1.0f + (float)e->kind * 1.2f);
  e->kx += ux * push;
  e->ky += uy * push;
  if (e->hp <= 0) kill_enemy(s, i, bm->owner, ux * 20, uy * 20);
}

static void do_boom(sim *s, const boom *bm) {
  ev(s, EV_BOOM, bm->x, bm->y, bm->r, bm->big ? 1.0f : 0.0f, bm->big, bm->owner);
  boom_ctx c = { bm };
  each_enemy(s, bm->x, bm->y, bm->r, boom_hit, &c);
  for (int i = 0; i < MAXO; i++) {
    obstacle *o = &s->o[i];
    if (!o->alive || o->kind == OK_PILLAR) continue;
    float dx = o->x - bm->x, dy = o->y - bm->y, rr = bm->r + o->hx;
    if (dx * dx + dy * dy < rr * rr) hurt_obstacle(s, i, (int)bm->dmg);
  }
  for (int i = 0; i < MAXP; i++) {
    player *p = &s->p[i];
    if (!p->active || p->down || p->inv > 0) continue;
    float dx = p->x - bm->x, dy = p->y - bm->y;
    if (dx * dx + dy * dy < bm->r * bm->r * 0.6f && (bm->owner >= MAXP || bm->owner == i ? true : false)) {
      p->hp -= bm->owner == i ? 6.0f : 14.0f;
      p->inv = 30;
      ev(s, EV_HURT, p->x, p->y, 0, 0, i, 0);
    }
  }
}

static void player_step(sim *s, int i, const input *in) {
  player *p = &s->p[i];
  if (!p->active) return;
  if (p->down) {
    p->down_ticks++;
    return;
  }
  float sp = 6.0f + (float)p->speed * 0.6f;
  float mx = in->mx, my = in->my, ml = cw_v2_length(cw_v2(mx, my));
  if (ml > 1) mx /= ml, my /= ml;
  p->vx += (mx * sp - p->vx) * 0.35f;
  p->vy += (my * sp - p->vy) * 0.35f;
  p->x += p->vx / TICK;
  p->y += p->vy / TICK;
  p->x = cw_clampf(p->x, -HALF + 0.6f, HALF - 0.6f);
  p->y = cw_clampf(p->y, -HALF + 0.6f, HALF - 0.6f);
  for (int k = 0; k < MAXO; k++) { /* push out of boxes */
    const obstacle *o = &s->o[k];
    if (!o->alive) continue;
    float r = 0.42f;
    if (!in_box(o, p->x, p->y, r)) continue;
    float dx = p->x - o->x, dy = p->y - o->y;
    float px = o->hx + r - (dx < 0 ? -dx : dx), py = o->hy + r - (dy < 0 ? -dy : dy);
    if (px < py) p->x += dx < 0 ? -px : px;
    else p->y += dy < 0 ? -py : py;
  }
  for (int k = 0; k < MAXP; k++) { /* friends are solid, softly */
    const player *q = &s->p[k];
    if (k == i || !q->active || q->down) continue;
    float dx = p->x - q->x, dy = p->y - q->y, d2 = dx * dx + dy * dy;
    if (d2 < 0.8f * 0.8f) {
      float d = d2 > 0.0001f ? cw_v2_length(cw_v2(dx, dy)) : 0.01f, push = (0.8f - d) * 0.5f;
      p->x += (d2 > 0.0001f ? dx / d : (i > k ? 1.0f : -1.0f)) * push;
      p->y += (d2 > 0.0001f ? dy / d : 0.0f) * push;
    }
  }
  /* tether: the living team's box never grows past what the camera at its farthest shows */
  {
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    int n = 0;
    for (int k = 0; k < MAXP; k++) {
      const player *q = &s->p[k];
      if (k == i || !q->active || q->down) continue;
      x0 = CW_MIN(x0, q->x), x1 = CW_MAX(x1, q->x), y0 = CW_MIN(y0, q->y), y1 = CW_MAX(y1, q->y), n++;
    }
    if (n) {
      p->x = cw_clampf(p->x, x1 - 2 * TETHER_HW, x0 + 2 * TETHER_HW);
      p->y = cw_clampf(p->y, y1 - 2 * TETHER_HH, y0 + 2 * TETHER_HH);
    }
  }
  if (p->offer_ticks > 0) {
    int pick = in->pick >= 1 && in->pick <= 3 ? in->pick - 1 : --p->offer_ticks == 0 ? 0 : -1;
    if (pick >= 0) {
      p->offer_ticks = 0;
      apply_upgrade(s, p, i, p->offer[pick]);
      if (p->pending > 0) {
        p->pending--;
        roll_offer(s, p);
      }
    }
  }
  float al = cw_v2_length(cw_v2(in->ax, in->ay));
  if (al > 0.001f) p->aim = cw_atan2(in->ay, in->ax); /* the dead zone is applied where the stick is read */
  if (p->cd > 0) p->cd--;
  if (p->bomb_cd > 0) p->bomb_cd--;
  if (p->inv > 0) p->inv--;
  bool fire = in->fire || al > 0.3f;
  if (fire && p->cd == 0) {
    int period = CW_MAX(3, 11 - p->rate);
    p->cd = period;
    int n = 1 + p->shots;
    float spread = 0.07f * (float)(n - 1) + 0.02f;
    bool rocket = p->rocket > 0 && ((s->tick / period) % 4 == 0);
    for (int k = 0; k < n; k++) {
      int slot = -1;
      for (int j = 0; j < MAXB; j++)
        if (!s->b[j].alive) { slot = j; break; }
      if (slot < 0) break;
      float off = n > 1 ? ((float)k / (float)(n - 1) - 0.5f) * 2 * spread : 0;
      float jit = (rf(s) - 0.5f) * 0.03f;
      float a = p->aim + off + jit, v = rocket && k == n / 2 ? 22.0f : 32.0f;
      bool rk = rocket && k == n / 2;
      s->b[slot] = (bullet){ true, (uint8_t)i, (uint8_t)rk, p->x + cw_cos(p->aim) * 0.6f, p->y + cw_sin(p->aim) * 0.6f,
                             cw_cos(a) * v, cw_sin(a) * v, 1.0f + (float)p->dmg * 0.8f + (rk ? 2.0f : 0.0f), rk ? 80 : 50, p->pierce };
      ev(s, rk ? EV_ROCKET : EV_SHOT, s->b[slot].x, s->b[slot].y, s->b[slot].vx, s->b[slot].vy, i, 0);
    }
  }
  if (in->bomb && p->bomb_cd == 0) {
    p->bomb_cd = 60 * 7;
    float bx = p->x + cw_cos(p->aim) * 6.0f, by = p->y + cw_sin(p->aim) * 6.0f;
    queue_boom(s, bx, by, 6.0f + (float)p->radius * 0.6f, 60, 8, i, true);
    queue_boom(s, bx + 1.8f, by - 1.2f, 4, 40, 18, i, false);
    queue_boom(s, bx - 1.8f, by + 1.2f, 4, 40, 26, i, false);
  }
}

static void bullets_step(sim *s) {
  for (int j = 0; j < MAXB; j++) {
    bullet *b = &s->b[j];
    if (!b->alive) continue;
    b->x += b->vx / TICK;
    b->y += b->vy / TICK;
    if (--b->life <= 0 || b->x < -HALF || b->x > HALF || b->y < -HALF || b->y > HALF) {
      if (b->kind == 1 && b->alive) queue_boom(s, b->x, b->y, 2.4f, 8, 0, b->owner, false);
      b->alive = false;
      continue;
    }
    for (int k = 0; k < MAXO; k++) {
      obstacle *o = &s->o[k];
      if (!o->alive || !in_box(o, b->x, b->y, 0.05f)) continue;
      if (b->kind == 1) queue_boom(s, b->x, b->y, 2.4f + (float)s->p[b->owner].radius * 0.5f, 3.0f + b->dmg * 2.0f, 0, b->owner, false);
      else hurt_obstacle(s, k, (int)b->dmg);
      b->alive = false;
      break;
    }
    if (!b->alive) continue;
    bullet_ctx c = { j, false };
    each_enemy(s, b->x, b->y, 0.12f, bullet_hit, &c);
  }
}

static void spawn_director(sim *s) {
  s->wave_tick++;
  if (s->state == ST_PLAY && s->wave == FINAL_WAVE && s->boss_wave == FINAL_WAVE && s->boss_alive == 0 && s->wave_tick > 90) {
    s->state = ST_WON; /* the last boss is down: the yard is held */
    s->state_ticks = 0;
    return;
  }
  if (s->wave_tick >= 60 * 30 && !(s->wave == FINAL_WAVE && s->boss_alive)) { /* the last wave lasts until its boss falls */
    s->wave_tick = 0;
    s->wave++;
    ev(s, EV_WAVE, 0, 0, 0, 0, s->wave, 0);
  }
  /* a slow start, a horde late: about 25 at wave 1, 330 at 5, 1000 at 10,
   * 2900 by 20, more with more players */
  {
    int w = s->wave;
    float k = 0.6f + 0.2f * (float)s->players;
    s->spawn_cap = CW_MIN((int)((float)(20 + 40 * (w - 1) + 6 * w * w) * k), 2900);
  }
  float cx, cy, hw, hh;
  sim_team_box(s, &cx, &cy, &hw, &hh);
  s->cam_x = cx, s->cam_y = cy;
  if (s->wave % 5 == 0 && s->wave_tick == 30 && s->boss_alive == 0) {
    s->boss_wave = s->wave;
    float a = rf(s) * 6.28f;
    float bx = cw_clampf(cx + cw_cos(a) * 24, -HALF + 3, HALF - 3), by = cw_clampf(cy + cw_sin(a) * 24, -HALF + 3, HALF - 3);
    if (spawn_enemy(s, EK_BOSS, bx, by) >= 0) ev(s, EV_BOSS, bx, by, 0, 0, 0, 0);
  }
  int budget = 14;
  while (s->nenemies < s->spawn_cap && budget-- > 0) {
    float a = rf(s) * 6.2832f, d = s->tick < 600 ? 16.0f + rf(s) * 6.0f : 22.0f + rf(s) * 10.0f; /* the opening is closer: a first kill within 3 s */
    int host = (int)(cw_rng_u32(&s->rng) % (uint32_t)MAXP); /* the ring is around one living player, so every player is met at spawn distance */
    for (int t = 0; t < MAXP && !(s->p[host].active && !s->p[host].down); t++) host = (host + 1) % MAXP;
    float hx = s->p[host].active && !s->p[host].down ? s->p[host].x : cx, hy = s->p[host].active && !s->p[host].down ? s->p[host].y : cy;
    float x = cw_clampf(hx + cw_cos(a) * d, -HALF + 1, HALF - 1), y = cw_clampf(hy + cw_sin(a) * d, -HALF + 1, HALF - 1);
    int gx = (int)(x + HALF), gy = (int)(y + HALF);
    if (s->blocked[gy * FW + gx]) continue;
    bool close = false; /* never on top of a player (a corner of the yard can bring the ring near one) */
    for (int k = 0; k < MAXP; k++) {
      const player *p = &s->p[k];
      float dx = p->x - x, dy = p->y - y;
      if (p->active && dx * dx + dy * dy < 14.0f * 14.0f) close = true;
    }
    if (close) continue;
    float r = rf(s);
    int kind = EK_RUNNER;
    if (s->wave >= 2 && r < CW_MIN(0.02f + 0.004f * (float)s->wave, 0.07f)) kind = EK_BRUTE;
    else if (s->wave >= 3 && r < CW_MIN(0.06f + 0.006f * (float)s->wave, 0.14f)) kind = EK_BLOATER;
    if (spawn_enemy(s, kind, x, y) < 0) break;
  }
}

static void enemies_step(sim *s) {
  if (s->flow_age++ >= 6) build_flow(s);
  int living = 0;
  for (int i = 0; i < MAXP; i++) living += s->p[i].active && !s->p[i].down;
  for (int i = 0; i < MAXE; i++) {
    enemy *e = &s->e[i];
    if (!e->alive) continue;
    /* nearest living player */
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < MAXP; k++) {
      const player *p = &s->p[k];
      if (!p->active || p->down) continue;
      float dx = p->x - e->x, dy = p->y - e->y, d = dx * dx + dy * dy;
      if (d < bd) bd = d, best = k;
    }
    float vx = 0, vy = 0;
    if (best >= 0) {
      const player *p = &s->p[best];
      if (bd < 7.0f * 7.0f) {
        float l = cw_v2_length(cw_v2(p->x - e->x, p->y - e->y));
        if (l > 0.01f) vx = (p->x - e->x) / l, vy = (p->y - e->y) / l;
      } else {
        int cx = CW_CLAMP((int)(e->x + HALF), 0, FW - 1), cy = CW_CLAMP((int)(e->y + HALF), 0, FW - 1);
        uint16_t bestd = s->flow[cy * FW + cx];
        int bx = cx, by = cy;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            int nx = cx + dx, ny = cy + dy;
            if (nx < 0 || ny < 0 || nx >= FW || ny >= FW) continue;
            uint16_t d = s->flow[ny * FW + nx];
            if (d < bestd) bestd = d, bx = nx, by = ny;
          }
        if (bx != cx || by != cy) {
          float tx = (float)bx + 0.5f - HALF - e->x, ty = (float)by + 0.5f - HALF - e->y;
          float l = cw_v2_length(cw_v2(tx, ty));
          vx = tx / l, vy = ty / l;
        } else if (bd > 0) {
          float l = cw_v2_length(cw_v2(p->x - e->x, p->y - e->y));
          vx = (p->x - e->x) / l, vy = (p->y - e->y) / l;
        }
      }
    }
    float sp = e->speed * (e->spawn_age < 40 ? 0.5f : 1.0f);
    if (e->kind == EK_BOSS) { /* walk, stand still and glow (the telegraph), then charge straight ahead */
      e->ph = (e->ph + 1) % 330;
      if (e->ph >= 230 && e->ph < 290) sp = 0;
      else if (e->ph >= 290) sp *= 3.0f;
    }
    e->spawn_age++;
    vx = vx * sp + e->kx * 60.0f;
    vy = vy * sp + e->ky * 60.0f;
    e->kx *= 0.86f;
    e->ky *= 0.86f;
    cw_crowd_set_velocity(s->crowd, e->agent, (t3_vec3){ vx, 0, vy });
    if (e->flash > 0) e->flash--;
    if (e->atk > 0) e->atk--;
  }
  (void)living;
  cw_crowd_step(s->crowd, 1.0f / TICK);
  s->crowd_ms = cw_crowd_get_stats(s->crowd).step_ms;
  for (int i = 0; i < MAXE; i++) {
    enemy *e = &s->e[i];
    if (!e->alive) continue;
    t3_vec3 p = cw_crowd_position(s->crowd, e->agent);
    float dx = p.x - e->x, dy = p.z - e->y;
    if (dx * dx + dy * dy > 0.0004f) e->face = cw_atan2(dy, dx);
    e->x = p.x, e->y = p.z;
  }
  build_grid(s);
  /* contact damage */
  for (int k = 0; k < MAXP; k++) {
    player *p = &s->p[k];
    if (!p->active || p->down) continue;
    for (int i = 0; i < MAXE; i++) {
      enemy *e = &s->e[i];
      if (!e->alive || e->atk > 0) continue;
      float dx = e->x - p->x, dy = e->y - p->y, rr = EK[e->kind].radius + 0.45f;
      if (dx * dx + dy * dy > rr * rr) continue;
      e->atk = 30;
      if (p->inv > 0) continue;
      float dmg = (float)EK[e->kind].dmg * (1.0f - 0.08f * (float)CW_MIN(p->armor, 6));
      p->hp -= dmg;
      p->inv = 12;
      ev(s, EV_HURT, p->x, p->y, 0, 0, k, 0);
      if (p->hp <= 0) {
        p->down = true;
        p->down_ticks = 0;
        ev(s, EV_DOWN, p->x, p->y, 0, 0, k, 0);
      }
    }
  }
  /* the boss calls runners */
  s->boss_hp = 0, s->boss_phase = 0;
  for (int i = 0; i < MAXE; i++)
    if (s->e[i].alive && s->e[i].kind == EK_BOSS) {
      s->boss_hp += s->e[i].hp;
      s->boss_phase = s->e[i].ph >= 230 && s->e[i].ph < 290 ? 1 : s->e[i].ph >= 290 ? 2 : 0;
    }
  if (s->boss_alive && s->tick % 300 == 0)
    for (int i = 0; i < MAXE; i++)
      if (s->e[i].alive && s->e[i].kind == EK_BOSS) {
        for (int k = 0; k < 3 + s->players; k++) {
          float a = rf(s) * 6.28f;
          spawn_enemy(s, EK_RUNNER, s->e[i].x + cw_cos(a) * 2.2f, s->e[i].y + cw_sin(a) * 2.2f);
        }
      }
}

static void pickups_step(sim *s) {
  for (int i = 0; i < MAXG; i++) {
    pickup *g = &s->g[i];
    if (!g->alive) continue;
    if (--g->life <= 0) { g->alive = false; continue; }
    g->x += g->vx / TICK;
    g->y += g->vy / TICK;
    g->vx *= 0.93f;
    g->vy *= 0.93f;
    g->x = cw_clampf(g->x, -HALF + 0.5f, HALF - 0.5f);
    g->y = cw_clampf(g->y, -HALF + 0.5f, HALF - 0.5f);
    for (int k = 0; k < MAXP; k++) {
      player *p = &s->p[k];
      if (!p->active || p->down) continue;
      float dx = p->x - g->x, dy = p->y - g->y, d2 = dx * dx + dy * dy, mr = 2.0f + (float)p->magnet * 1.6f;
      if (d2 < mr * mr && d2 > 0.0001f) {
        float d = cw_v2_length(cw_v2(dx, dy)), pull = 0.22f;
        g->vx += dx / d * pull * TICK * 0.12f;
        g->vy += dy / d * pull * TICK * 0.12f;
      }
      if (d2 < 0.55f) {
        g->alive = false;
        if (g->kind == 0) {
          p->xp++;
          p->score += 2;
          if (p->xp >= p->xp_next) level_up(s, p, k);
        } else p->hp = CW_MIN(p->hp + 35, 100.0f);
        ev(s, EV_GEM, g->x, g->y, 0, 0, g->kind, k);
        break;
      }
    }
  }
}

int sim_alive_players(const sim *s) {
  int n = 0;
  for (int i = 0; i < MAXP; i++) n += s->p[i].active && !s->p[i].down;
  return n;
}

void sim_team_box(const sim *s, float *cx, float *cy, float *hw, float *hh) {
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  int n = 0;
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (!p->active || p->down) continue;
    x0 = CW_MIN(x0, p->x), x1 = CW_MAX(x1, p->x), y0 = CW_MIN(y0, p->y), y1 = CW_MAX(y1, p->y);
    n++;
  }
  if (!n) {
    *cx = s->cam_x, *cy = s->cam_y, *hw = *hh = 4;
    return;
  }
  *cx = (x0 + x1) * 0.5f, *cy = (y0 + y1) * 0.5f, *hw = (x1 - x0) * 0.5f, *hh = (y1 - y0) * 0.5f;
}

void sim_step(sim *s, const input in[MAXP]) {
  s->tick++;
  s->ev_n = s->ev_n > MAXEV - 400 ? 0 : s->ev_n; /* nothing draws these: drop the backlog */
  if (s->state == ST_WON) { /* the horde falls apart, nobody gets hurt */
    s->state_ticks++;
    for (int i = 0; i < MAXP; i++) s->p[i].inv = 10;
    int n = 0;
    for (int i = 0; i < MAXE && n < 30; i++)
      if (s->e[i].alive && (i + s->tick) % 3 == 0) kill_enemy(s, i, -1, 0, 0), n++;
  }
  if (s->state == ST_OVER) {
    if (++s->state_ticks > 60 * 6) {
      for (int i = 0; i < MAXP; i++)
        if (s->p[i].active) {
          player *p = &s->p[i];
          p->down = false, p->hp = 100, p->inv = 120;
        }
      s->state = ST_PLAY;
      s->wave = CW_MAX(1, s->wave - 1);
      s->wave_tick = 0;
      for (int i = 0; i < MAXE; i++)
        if (s->e[i].alive) kill_enemy(s, i, -1, 0, 0);
    }
  }
  for (int i = 0; i < MAXP; i++) player_step(s, i, &in[i]);
  /* a downed player gets up when a teammate stands near for 2 s, or at each wave */
  for (int i = 0; i < MAXP; i++) {
    player *p = &s->p[i];
    if (!p->active || !p->down) continue;
    if (s->wave_tick == 1) { p->down = false, p->hp = 50, p->inv = 120; continue; }
    if (p->down_ticks > 60 * 8) { /* nobody came: the downed player is carried to the nearest living friend */
      int near = -1;
      float bd = 1e9f;
      for (int k = 0; k < MAXP; k++) {
        const player *q = &s->p[k];
        float dx = q->x - p->x, dy = q->y - p->y, d = dx * dx + dy * dy;
        if (k != i && q->active && !q->down && d < bd) bd = d, near = k;
      }
      if (near >= 0) { p->x = cw_clampf(s->p[near].x + 1.0f, -HALF + 0.6f, HALF - 0.6f), p->y = s->p[near].y, p->down = false, p->hp = 40, p->inv = 90; continue; }
    }
    for (int k = 0; k < MAXP; k++) {
      const player *q = &s->p[k];
      if (k == i || !q->active || q->down) continue;
      float dx = q->x - p->x, dy = q->y - p->y;
      if (dx * dx + dy * dy < 2.5f * 2.5f && p->down_ticks > 60 * 2 && s->tick % 60 == 0) { p->down = false, p->hp = 40, p->inv = 90; }
    }
  }
  { /* a player who got up (a wave, a friend, a rescue) may be far from the others: the team box is pulled back to one view */
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    int n = 0;
    for (int i = 0; i < MAXP; i++) {
      const player *p = &s->p[i];
      if (!p->active || p->down) continue;
      x0 = CW_MIN(x0, p->x), x1 = CW_MAX(x1, p->x), y0 = CW_MIN(y0, p->y), y1 = CW_MAX(y1, p->y), n++;
    }
    if (n > 1)
      for (int i = 0; i < MAXP; i++) {
        player *p = &s->p[i];
        if (!p->active || p->down) continue;
        float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
        if (x1 - x0 > 2 * TETHER_HW) p->x = cw_clampf(p->x, mx - TETHER_HW, mx + TETHER_HW);
        if (y1 - y0 > 2 * TETHER_HH) p->y = cw_clampf(p->y, my - TETHER_HH, my + TETHER_HH);
      }
  }
  if (s->state == ST_PLAY && s->players > 0 && sim_alive_players(s) == 0) {
    s->state = ST_OVER;
    s->state_ticks = 0;
  }
  if (s->state == ST_PLAY) spawn_director(s);
  enemies_step(s);
  bullets_step(s);
  for (int i = 0; i < MAXX; i++) { /* explosions chain: each can start another this very tick */
    boom *b = &s->x[i];
    if (b->r == 0) continue;
    if (b->delay-- > 0) continue;
    boom copy = *b;
    b->r = 0;
    s->nboom_total++;
    do_boom(s, &copy);
  }
  pickups_step(s);
  if (s->nav_dirty) {
    cw_nav_update(s->nav);
    s->nav_dirty = false;
  }
  uint32_t h = s->hash;
  for (int i = 0; i < MAXP; i++) {
    int32_t q[3] = { (int32_t)(s->p[i].x * 1000), (int32_t)(s->p[i].y * 1000), (int32_t)s->p[i].hp };
    for (size_t j = 0; j < 3; j++) h = (h ^ (uint32_t)q[j]) * 16777619u;
  }
  h = (h ^ (uint32_t)s->nenemies) * 16777619u;
  h = (h ^ (uint32_t)s->kills) * 16777619u;
  s->hash = h;
}
