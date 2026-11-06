/* crawler rules: 1-4 players fight down a dungeon of maze floors (DESIGN.md).
 *
 *   monsters  every monster is an agent of the engine's crowd on the floor's navmesh (its open tiles), steered by
 *             one flow field from the living players round the walls (doors shut are walls); near a player in
 *             sight it goes straight at them. Spawners pour them out while a player is near.
 *   players   one stick and two buttons, as the classic arcade maze game: hold fire to stand and shoot the way you
 *             face, walk into a monster to fight it, magic uses a potion on everything on the screen. Health
 *             drains all the time; food gives it back.
 *   the floor walls stop shots; keys open doors (a whole door at once); breakable walls fall to two hits; trap
 *             plates make walls vanish; teleporters; an exit down (some skip floors). After 180 s every door opens.
 * Everything here runs on the fixed tick with cartwheel math and cw_rng: the same inputs give the same game on
 * every host. */
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/profile.h"
#include "game.h"

static float rf(sim *s) { return cw_rng_float(&s->rng); }
static void ev(sim *s, int kind, float x, float y, float z, float w, int a, int b) {
  if (s->ev_n >= MAXEV) return;
  s->ev[s->ev_n++] = (event){ (uint8_t)kind, (uint8_t)a, (uint8_t)b, x, y, z, w };
}

/* ── tables ─────────────────────────────────────────────────────────── */

static const char *const KIND_NAME[EK_KINDS] = { "shade", "ghoul", "imp", "slinger", "hexer", "dread", "pilferer", "ogre", "hound", "bloater", "skitter", "sporeling" };
const char *sim_kind_name(int k) { return k >= 0 && k < EK_KINDS ? KIND_NAME[k] : "?"; }
int sim_kind_by_name(const char *name) {
  for (int k = 0; k < EK_KINDS; k++)
    if (!strcmp(name, KIND_NAME[k])) return k;
  return -1;
}
static const char *const CLASS_NAME[CL_KINDS] = { "BRUTE", "WARDEN", "MAGE", "RANGER" };
const char *sim_class_name(int c) { return CLASS_NAME[(unsigned)c % CL_KINDS]; }
static const char *const CHARM_NAME[CH_KINDS] = { "TRIPLE SHOT", "RAPID", "PIERCE", "BOUNCE", "SEEKER", "CHAIN", "FROST", "EMBERS", "WARD", "REPULSE",
                                                  "SHROUD", "LODESTONE", "FIREBOMBS", "HASTE", "GIANT", "FORTUNE" };
const char *sim_charm_name(int c) { return CHARM_NAME[(unsigned)c % CH_KINDS]; }
static const char *const ELIXIR_NAME[EL_KINDS] = { "ARMOUR", "SHOT POWER", "SHOT SPEED", "MAGIC", "MELEE", "SPEED" };
const char *sim_elixir_name(int e) { return ELIXIR_NAME[(unsigned)e % EL_KINDS]; }
static const int CHARM_TICKS[CH_KINDS] = { 1200, 1200, 1200, 1200, 1200, 900, 900, 900, 600, 900, 900, 1200, 4, 1200, 900, 1800 };

/* speed, armour (damage taken), shot kind, damage, cooldown, in flight, melee, its cooldown, magic, shot speed */
const class_stats CLASS[CL_KINDS] = {
  { 5.6f, 0.80f, SK_AXE, 3, 24, 2, 4, 18, 2, 13.0f },
  { 6.2f, 0.65f, SK_HAMMER, 2, 20, 2, 3, 20, 2, 15.0f },
  { 6.2f, 1.00f, SK_FIRE, 2, 22, 2, 1, 22, 4, 14.0f },
  { 7.4f, 0.90f, SK_ARROW, 1, 12, 3, 2, 20, 3, 22.0f },
};

/* radius, speed, health a strength, damage a strength, score */
typedef struct ekind { float radius, speed; int hp, dmg, score; } ekind;
static const ekind EK[EK_KINDS] = {
  { 0.36f, 4.6f, 1, 8, 10 },   /* shade: touches once and is gone */
  { 0.38f, 4.0f, 2, 4, 10 },   /* ghoul */
  { 0.36f, 4.4f, 2, 3, 20 },   /* imp: its fireball */
  { 0.5f, 3.2f, 3, 3, 20 },    /* slinger: its lob */
  { 0.38f, 3.6f, 2, 4, 30 },   /* hexer: its bolt */
  { 0.45f, 3.8f, 1, 10, 1000 },/* dread: drains (shots only push it) */
  { 0.34f, 6.8f, 2, 0, 100 },  /* pilferer */
  { 0.72f, 2.6f, 8, 7, 40 },   /* ogre */
  { 0.4f, 6.0f, 2, 3, 20 },    /* hound */
  { 0.5f, 2.0f, 1, 10, 20 },   /* bloater: its cloud */
  { 0.34f, 5.4f, 1, 3, 10 },   /* skitter */
  { 0.4f, 3.4f, 2, 5, 20 },    /* sporeling */
};
/* melee: a wind-up the player can read, the strike on one tick, a recovery (ticks); trigger and reach past touching;
 * arc: the cosine of the half angle in front the strike covers */
typedef struct melee { int windup, recover, cooldown; float trigger, reach, arc; } melee;
static const melee MEL[EK_KINDS] = {
  { 0, 0, 0, 0.1f, 0.2f, -1 },          /* shade: the touch */
  { 18, 14, 10, 0.25f, 0.75f, 0.3f },   /* ghoul: a slash */
  { 16, 14, 12, 0.2f, 0.6f, 0.3f },     /* imp: a bite */
  { 24, 22, 14, 0.25f, 0.8f, 0.3f },    /* slinger: a shove */
  { 22, 18, 12, 0.25f, 0.7f, 0.3f },    /* hexer: a claw */
  { 0, 0, 0, 0.1f, 0.3f, -1 },          /* dread: holds on */
  { 0, 0, 0, 0.1f, 0.2f, -1 },          /* pilferer: grabs */
  { 34, 26, 12, 0.5f, 1.1f, 0.2f },     /* ogre: a two-fisted smash */
  { 24, 16, 30, 2.4f, 2.8f, 0.5f },     /* hound: crouches 0.4 s, then a 3 m pounce; a breather after */
  { 0, 0, 0, 0.1f, 0.2f, -1 },          /* bloater: bursts on touch */
  { 14, 12, 8, 0.2f, 0.6f, 0.4f },      /* skitter: a nip */
  { 22, 18, 12, 0.25f, 0.75f, 0.4f },   /* sporeling: a headbutt */
};
#define STRIKE_HOLD 6
/* a monster's damage: its kind's, half again at strength 2, nearly double at 3 */
static float edmg(const enemy *e) { return (float)EK[e->kind].dmg * (0.6f + 0.4f * (float)e->str); }
int sim_melee_windup(int kind) { return MEL[CW_CLAMP(kind, 0, EK_KINDS - 1)].windup; }
int sim_attack_frame(const enemy *e, float *tell) {
  if (tell) *tell = 0;
  if (!e->alive) return -1;
  if (e->act > 0) { /* a ranged attack's tell reads as the wind-up */
    if (tell) *tell = CW_MIN((float)e->act_t / 30.0f, 1.0f);
    return e->act_t < 30 ? 0 : 1;
  }
  if (e->atk <= 0) return -1;
  const melee *m = &MEL[e->kind];
  if (e->atk <= m->windup) {
    if (tell) *tell = m->windup ? (float)e->atk / (float)m->windup : 1;
    return 0;
  }
  return e->atk <= m->windup + STRIKE_HOLD ? 1 : 2;
}
bool sim_hexer_hidden(const sim *s, const enemy *e) { return e->kind == EK_HEXER && ((s->tick + e->spawner * 37 + (int)(e->x * 3)) % 180) >= 90; }

/* ── the tiles ──────────────────────────────────────────────────────── */

static int tile_x(const sim *s, float x) { return mz_tx(&s->m, x); }
static int tile_y(const sim *s, float y) { return mz_ty(&s->m, y); }
bool sim_solid(const sim *s, int tx, int ty) {
  if (!mz_in(&s->m, tx, ty)) return true;
  return s->t[ty][tx] != T_FLOOR || s->sp_at[ty][tx] > 0;
}
bool sim_wall_at(const sim *s, float x, float y) { return sim_solid(s, tile_x(s, x), tile_y(s, y)); }

/* walls, closed doors and spawners as the flow field's 1 m cells */
static void build_blocked(sim *s) {
  memset(s->blocked, 1, sizeof s->blocked);
  for (int cy = 0; cy < s->fh; cy++)
    for (int cx = 0; cx < s->fw; cx++) s->blocked[cy * FG + cx] = sim_solid(s, cx / 2, cy / 2);
}
static void flow_from(sim *s, uint16_t *flow, const int *seeds, int nseed) {
  static int queue[FG * FG];
  for (int i = 0; i < FG * FG; i++) flow[i] = 0xffff;
  int qh = 0, qt = 0;
  for (int k = 0; k < nseed; k++) {
    int c = seeds[k];
    if (c < 0 || flow[c] == 0) continue;
    flow[c] = 0, queue[qt++] = c;
  }
  static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
  while (qh < qt) {
    int c = queue[qh++], cx = c % FG, cy = c / FG;
    uint16_t d = flow[c];
    for (int k = 0; k < 8; k++) {
      int nx = cx + DX[k], ny = cy + DY[k];
      if (nx < 0 || ny < 0 || nx >= s->fw || ny >= s->fh) continue;
      int n = ny * FG + nx;
      if (s->blocked[n] || flow[n] != 0xffff) continue;
      if (k >= 4 && (s->blocked[cy * FG + nx] || s->blocked[ny * FG + cx])) continue; /* no cutting corners */
      flow[n] = (uint16_t)(d + (k < 4 ? 2 : 3));
      queue[qt++] = n;
    }
  }
}
static int flow_cell(const sim *s, float x, float y) {
  int cx = (int)(x + (float)s->m.w + 1000.0f) - 1000, cy = (int)(y + (float)s->m.h + 1000.0f) - 1000;
  if (cx < 0 || cy < 0 || cx >= s->fw || cy >= s->fh) return -1;
  return cy * FG + cx;
}
static void build_flow(sim *s) {
  build_blocked(s);
  int seeds[MAXP], n = 0;
  for (int i = 0; i < MAXP; i++)
    if (s->p[i].active && !s->p[i].down) seeds[n++] = flow_cell(s, s->p[i].x, s->p[i].y);
  flow_from(s, s->flow, seeds, n);
  int ex[MZ_MAXEXIT * 4], ne = 0;
  for (int i = 0; i < s->m.nexit; i++) {
    if (s->m.exit[i][2] != 1) continue;
    float x = mz_wx(&s->m, s->m.exit[i][0]), y = mz_wz(&s->m, s->m.exit[i][1]);
    ex[ne++] = flow_cell(s, x - 0.5f, y - 0.5f), ex[ne++] = flow_cell(s, x + 0.5f, y + 0.5f);
  }
  flow_from(s, s->eflow, ex, ne);
  s->flow_age = 0, s->flow_dirty = false;
}
/* the downhill step of a flow field from (x, y): a unit direction (false when none) */
static bool flow_step(const sim *s, const uint16_t *flow, float x, float y, float *dx, float *dy) {
  int c = flow_cell(s, x, y);
  if (c < 0) return false;
  int cx = c % FG, cy = c / FG, bx = cx, by = cy;
  uint16_t best = flow[c];
  for (int oy = -1; oy <= 1; oy++)
    for (int ox = -1; ox <= 1; ox++) {
      int nx = cx + ox, ny = cy + oy;
      if (nx < 0 || ny < 0 || nx >= s->fw || ny >= s->fh || (!ox && !oy)) continue;
      if (ox && oy && (s->blocked[cy * FG + nx] || s->blocked[ny * FG + cx])) continue;
      uint16_t d = flow[ny * FG + nx];
      if (d < best) best = d, bx = nx, by = ny;
    }
  if (bx == cx && by == cy) return false;
  float tx = (float)bx + 0.5f - (float)s->m.w - x, ty = (float)by + 0.5f - (float)s->m.h - y, l = sqrtf(tx * tx + ty * ty);
  if (l < 1e-4f) return false;
  *dx = tx / l, *dy = ty / l;
  return true;
}
int sim_exit_way(const sim *s, float x, float y, float *dx, float *dy) { return flow_step(s, s->eflow, x, y, dx, dy); }

/* a clear line between two points (no solid tile on it: shots and sight) */
static bool line_clear(const sim *s, float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
  int n = (int)(l / 0.5f) + 1;
  for (int i = 1; i < n; i++) {
    float t = (float)i / (float)n;
    if (sim_wall_at(s, x0 + dx * t, y0 + dy * t)) return false;
  }
  return true;
}

/* ── the navmesh: the floor's open tiles; doors, breakable and trap walls and spawners as removable obstacles ── */

static void nav_build(sim *s) {
  if (!CW_HANDLE_IS_NULL(s->nav)) cw_navmesh_free(s->nav);
  cw_nav_geom g = { 0 };
  const maze *m = &s->m;
  for (int y = 0; y < m->h; y++) {
    int x = 0;
    while (x < m->w) {
      if (m->t[y][x] == T_WALL || m->t[y][x] == T_VOID) { x++; continue; }
      int x0 = x;
      while (x < m->w && m->t[y][x] != T_WALL && m->t[y][x] != T_VOID) x++;
      float cx = (mz_wx(m, (float)x0) + mz_wx(m, (float)(x - 1))) * 0.5f, cz = mz_wz(m, (float)y);
      cw_nav_geom_add_box(&g, (t3_vec3){ cx, -0.25f, cz }, (t3_vec3){ (float)(x - x0) * MZ_TILE * 0.5f, 0.25f, MZ_TILE * 0.5f }, CW_NAV_AREA_WALK);
    }
  }
  cw_nav_config c = cw_nav_config_default();
  c.dynamic = true;
  c.max_obstacles = 512;
  c.cell_size = 0.25f;
  c.cell_height = 0.2f;
  c.agent_radius = 0.3f;
  c.agent_height = 1.6f;
  c.tile_size = 64;
  c.region_min_size = 2;
  s->nav = cw_navmesh_build(&c, &g);
  cw_nav_geom_free(&g);
  s->crowd = cw_crowd_new(s->nav, MAXE);
  cw_crowd_set_plan_budget(s->crowd, 8, 0);
}
static void tile_block(sim *s, int x, int y) {
  if (s->tile_nav[y][x]) return;
  s->tile_nav[y][x] = cw_nav_obstacle_add_box(s->nav, (t3_vec3){ mz_wx(&s->m, (float)x), 0, mz_wz(&s->m, (float)y) }, (t3_vec3){ 1.0f, 1.5f, 1.0f }, 0);
}
static void tile_open(sim *s, int x, int y) {
  s->t[y][x] = T_FLOOR;
  if (s->tile_nav[y][x]) cw_nav_obstacle_remove(s->nav, s->tile_nav[y][x]), s->tile_nav[y][x] = 0;
  s->flow_dirty = true;
}

/* ── monsters ───────────────────────────────────────────────────────── */

static int spawn_enemy(sim *s, int kind, int str, float x, float y) {
  int slot = -1;
  for (int i = 0; i < MAXE; i++)
    if (!s->e[i].alive) { slot = i; break; }
  if (slot < 0 || sim_wall_at(s, x, y)) return -1;
  cw_crowd_agent_params ap = cw_crowd_agent_params_default();
  /* the crowd keeps each body its drawn footprint (wider than its touch: EK.radius), so bodies stand apart and never
   * pass through each other */
  static const float CROWD_R[EK_KINDS] = { 0.5f, 0.5f, 0.55f, 0.55f, 0.48f, 0.6f, 0.42f, 0.85f, 0.5f, 0.65f, 0.6f, 0.52f };
  ap.radius = CW_MAX(CROWD_R[kind], EK[kind].radius) * (0.85f + 0.12f * (float)str);
  ap.height = 1.6f;
  float sp = EK[kind].speed * (0.92f + 0.16f * rf(s));
  ap.max_speed = sp * 1.6f;
  ap.max_accel = 14;
  ap.neighbor_dist = 2.6f;
  ap.max_neighbors = 6;
  ap.time_horizon = 0.6f;
  ap.avoidance_priority = kind == EK_OGRE || kind == EK_DREAD ? 0.9f : 0.5f;
  cw_crowd_agent a = cw_crowd_add(s->crowd, (t3_vec3){ x, 0, y }, &ap);
  if (CW_HANDLE_IS_NULL(a)) return -1;
  enemy *e = &s->e[slot];
  *e = (enemy){ .alive = true, .kind = (uint8_t)kind, .str = (uint8_t)str, .x = x, .y = y, .agent = a, .speed = sp, .tgt = -1, .spawner = -1, .carry = -1 };
  e->hp = EK[kind].hp * str;
  if (s->players > 2 && kind == EK_OGRE) e->hp += str * 2; /* a team of three or four: the heavies a little tougher */
  e->face = rf(s) * 6.28f;
  e->act_next = 60 + (int)(rf(s) * 120);
  s->nenemies++;
  return slot;
}
int sim_spawn(sim *s, int kind, int str, float x, float y) { return spawn_enemy(s, kind, str, x, y); }

static void drop_item(sim *s, int kind, int arg, float x, float y, bool dropped);
static void kill_enemy(sim *s, int i, int owner, float dx, float dy, bool score) {
  enemy *e = &s->e[i];
  if (!e->alive) return;
  e->alive = false;
  cw_crowd_remove(s->crowd, e->agent);
  s->nenemies--;
  s->kills++, s->killed_total++;
  if (e->spawner >= 0 && e->spawner < s->nsp) s->sp[e->spawner].nalive--;
  if (score && owner >= 0 && owner < MAXP) {
    const player *kp = &s->p[owner];
    s->p[owner].score += EK[e->kind].score * e->str * (kp->charm[CH_FORTUNE] > 0 ? 2 : 1);
  }
  ev(s, EV_KILL, e->x, e->y, dx, dy, e->kind, owner);
  if (e->kind == EK_BLOATER) { /* it bursts: a cloud of poison */
    for (int z = 0; z < MAXZ; z++)
      if (!s->z[z].alive) { s->z[z] = (zone){ true, 0, e->x, e->y, 2.0f, 120 }; ev(s, EV_CLOUD, e->x, e->y, 2.0f, 0, 0, 0); break; }
  }
  if (e->frozen > 0 && owner >= 0 && owner < MAXP) { /* a frozen one shatters into its neighbours */
    ev(s, EV_SHATTER, e->x, e->y, 0, 0, e->kind, 0);
    for (int k = 0; k < MAXE; k++) {
      enemy *n = &s->e[k];
      if (!n->alive || n == e || n->kind == EK_DREAD || (n->x - e->x) * (n->x - e->x) + (n->y - e->y) * (n->y - e->y) > 1.6f * 1.6f) continue;
      n->hp -= 2, n->flash = 4;
      if (n->hp <= 0) kill_enemy(s, k, owner, 0, 0, true);
    }
  }
  if (e->carry >= 0) drop_item(s, e->carry & 0xff, e->carry >> 8, e->x, e->y, true); /* a pilferer drops what it took */
  if (score && e->str >= 3 && rf(s) < 0.02f) drop_item(s, IT_CHARM, (int)(cw_rng_u32(&s->rng) % CH_KINDS), e->x, e->y, true);
}

/* ── pickups ────────────────────────────────────────────────────────── */

static void drop_item(sim *s, int kind, int arg, float x, float y, bool dropped) {
  for (int i = 0; i < MAXG; i++)
    if (!s->g[i].alive) {
      s->g[i] = (pickup){ true, (uint8_t)kind, (uint8_t)arg, x, y, 0, 0, 0, 0, dropped };
      return;
    }
}

static void hurt_player(sim *s, int i, float dmg, int from_kind, float fx, float fy);
static void queue_boom(sim *s, float x, float y, float r, float dmg, int delay, int owner, bool big) {
  for (int i = 0; i < MAXX; i++)
    if (s->x[i].r == 0) {
      s->x[i] = (boom){ x, y, r, dmg, delay, (uint8_t)owner, big };
      return;
    }
}

/* ── spawners ───────────────────────────────────────────────────────── */

static void spawner_hurt(sim *s, int k, int dmg, int owner) {
  spawner *sp = &s->sp[k];
  if (!sp->alive) return;
  sp->hp -= dmg;
  ev(s, EV_SPAWNER_HIT, sp->x, sp->y, 0, 0, sp->kind, sp->str);
  int str = (sp->hp + 2) / 3; /* three hits a strength */
  if (str < sp->str && str > 0) sp->str = (uint8_t)str;
  if (sp->hp > 0) return;
  sp->alive = false;
  s->sp_at[sp->ty][sp->tx] = 0;
  if (sp->nav) cw_nav_obstacle_remove(s->nav, sp->nav), sp->nav = 0;
  s->flow_dirty = true;
  s->spawners_killed++;
  if (owner >= 0 && owner < MAXP) s->p[owner].score += 100 * CW_MAX(1, (int)sp->str);
  ev(s, EV_SPAWNER_DIE, sp->x, sp->y, 0, 0, sp->kind, owner);
}

bool sim_on_screen(const sim *s, float x, float y, float margin) {
  return fabsf(x - s->view_x) < VIEW_HW + margin && fabsf(y - s->view_y) < VIEW_HH + margin;
}
static bool at_cap(const sim *s) { return !s->no_cap && s->on_screen >= SCREEN_CAP; }
static void spawners_step(sim *s) {
  if (s->no_spawn) return;
  float deep = cw_clampf(1.0f - (float)s->floor / 240.0f, 0.55f, 1.0f);
  int cap = CW_MIN(24 + s->floor / 3, 64);
  for (int k = 0; k < s->nsp; k++) {
    spawner *sp = &s->sp[k];
    if (!sp->alive) continue;
    if (sp->pulse > 0) sp->pulse--;
    if (--sp->cd > 0) continue;
    static const int RATE[4] = { 0, 84, 60, 42 };
    sp->cd = (int)((float)RATE[CW_CLAMP(sp->str, 1, 3)] * deep) + (int)(rf(s) * 12);
    if (sp->nalive >= cap || s->nenemies >= MAXE - 8) continue;
    if (at_cap(s) && sim_on_screen(s, sp->x, sp->y, 12.0f)) continue; /* the screen is full: one on it or near it holds back */
    bool near = false;
    for (int i = 0; i < MAXP && !near; i++) {
      const player *p = &s->p[i];
      float dx = p->x - sp->x, dy = p->y - sp->y;
      near = p->active && !p->down && dx * dx + dy * dy < 24.0f * 24.0f;
    }
    if (!near) continue;
    /* out of a free side of the spawner's tile */
    static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
    int o = (int)(cw_rng_u32(&s->rng) & 3);
    for (int t = 0; t < 4; t++) {
      int d = (o + t) & 3, tx = sp->tx + DX[d], ty = sp->ty + DY[d];
      if (sim_solid(s, tx, ty)) continue;
      float x = sp->x + (float)DX[d] * 1.25f + (rf(s) - 0.5f) * 0.6f, y = sp->y + (float)DY[d] * 1.25f + (rf(s) - 0.5f) * 0.6f;
      int e = spawn_enemy(s, sp->kind, sp->str, x, y);
      if (e >= 0) s->e[e].spawner = k, sp->nalive++, sp->pulse = 12, ev(s, EV_SPAWN, x, y, sp->x, sp->y, sp->kind, sp->str);
      break;
    }
  }
}

/* ── the floor ──────────────────────────────────────────────────────── */

void sim_free(sim *s) {
  if (!CW_HANDLE_IS_NULL(s->nav)) cw_navmesh_free(s->nav);
  s->nav = (cw_navmesh){ 0 };
  s->crowd = (cw_crowd){ 0 };
}

void sim_init(sim *s, int players, const int cls[MAXP], uint32_t seed) {
  sim_free(s);
  memset(s, 0, sizeof *s);
  cw_rng_seed(&s->rng, seed);
  s->players = players;
  for (int i = 0; i < players; i++) {
    player *p = &s->p[i];
    *p = (player){ .active = true, .hp = 1500, .cls = (uint8_t)(cls ? cls[i] : i % CL_KINDS), .face = -1.5708f };
  }
}

void sim_floor_start(sim *s, const maze *m) {
  s->m = *m;
  s->floor = m->floor;
  memcpy(s->t, m->t, sizeof s->t);
  for (int y = 0; y < MZ_MAX; y++)
    for (int x = 0; x < MZ_MAX; x++) s->t[y][x] = x < m->w && y < m->h ? m->t[y][x] : T_VOID;
  memset(s->door_open, 0, sizeof s->door_open);
  memset(s->tile_nav, 0, sizeof s->tile_nav);
  memset(s->sp_at, 0, sizeof s->sp_at);
  memset(s->e, 0, sizeof s->e);
  memset(s->b, 0, sizeof s->b);
  memset(s->g, 0, sizeof s->g);
  memset(s->x, 0, sizeof s->x);
  memset(s->z, 0, sizeof s->z);
  s->nenemies = 0, s->kills = 0;
  s->state = ST_PLAY, s->state_ticks = 0, s->floor_ticks = 0, s->next_floor = 0, s->doors_opened = 0;
  s->pilfer_next = 60 * 70;
  memset(s->ambush_done, 0, sizeof s->ambush_done);
  s->fw = m->w * 2, s->fh = m->h * 2;
  for (int y = 0; y < m->h; y++)
    for (int x = 0; x < m->w; x++) s->wall_hp[y][x] = m->t[y][x] == T_BREAK ? 2 : 0;
  nav_build(s);
  for (int y = 0; y < m->h; y++)
    for (int x = 0; x < m->w; x++)
      if (m->t[y][x] == T_DOOR || m->t[y][x] == T_BREAK || m->t[y][x] == T_TRAPWALL) tile_block(s, x, y);
  s->nsp = 0;
  for (int i = 0; i < m->nspawn && s->nsp < MAXSP; i++) {
    const mz_spawn *ms = &m->spawn[i];
    spawner *sp = &s->sp[s->nsp];
    *sp = (spawner){ true, ms->kind, ms->str, ms->x, ms->y, 3 * ms->str, 30 + (int)(rf(s) * 90), 0, 0, mz_wx(m, ms->x), mz_wz(m, ms->y), 0 };
    sp->nav = cw_nav_obstacle_add_box(s->nav, (t3_vec3){ sp->x, 0, sp->y }, (t3_vec3){ 0.9f, 1.5f, 0.9f }, 0);
    s->sp_at[ms->y][ms->x] = (int16_t)(s->nsp + 1);
    s->nsp++;
  }
  cw_nav_update(s->nav);
  for (int i = 0; i < m->nitem; i++) drop_item(s, m->item[i].kind, m->item[i].arg, mz_wx(m, m->item[i].x), mz_wz(m, m->item[i].y), false);
  /* the players at the start: the first start tile and the tiles round it */
  int placed = 0;
  for (int i = 0; i < MAXP; i++) {
    player *p = &s->p[i];
    if (!p->active) continue;
    float x, y;
    if (placed < m->nstart && m->nstart > 1) x = mz_wx(m, m->start[placed][0]), y = mz_wz(m, m->start[placed][1]);
    else {
      static const float OX[4] = { -0.6f, 0.6f, -0.6f, 0.6f }, OY[4] = { -0.5f, -0.5f, 0.5f, 0.5f };
      x = mz_wx(m, m->start[0][0]) + OX[placed], y = mz_wz(m, m->start[0][1]) + OY[placed];
    }
    placed++;
    p->x = x, p->y = y, p->vx = p->vy = 0, p->face = -1.5708f, p->at_exit = 0, p->firing = 0, p->shots_out = 0, p->drained = 0, p->stun = 0;
    if (p->down) p->down = false, p->hp = 1000; /* a fallen player comes back for the new floor */
  }
  build_flow(s);
  for (int i = 0; i < m->nmon; i++) spawn_enemy(s, m->mon[i].kind, m->mon[i].str, mz_wx(m, m->mon[i].x), mz_wz(m, m->mon[i].y));
  ev(s, EV_FLOOR, 0, 0, 0, 0, (uint8_t)(m->floor & 0xff), (uint8_t)(m->floor >> 8));
}

/* ── per tick ───────────────────────────────────────────────────────── */

static void build_grid(sim *s) {
  static int16_t cnt[MZ_MAX * MZ_MAX + 1];
  memset(cnt, 0, sizeof cnt);
  const int W = s->m.w, H = s->m.h;
  for (int i = 0; i < MAXE; i++) {
    if (!s->e[i].alive) continue;
    int cx = CW_CLAMP(tile_x(s, s->e[i].x), 0, W - 1), cy = CW_CLAMP(tile_y(s, s->e[i].y), 0, H - 1);
    cnt[cy * W + cx + 1]++;
  }
  for (int i = 1; i <= W * H; i++) cnt[i] = (int16_t)(cnt[i] + cnt[i - 1]);
  memcpy(s->cell_start, cnt, sizeof(int16_t) * (size_t)(W * H + 1));
  static int16_t fill[MZ_MAX * MZ_MAX];
  memcpy(fill, cnt, sizeof(int16_t) * (size_t)(W * H));
  for (int i = 0; i < MAXE; i++) {
    if (!s->e[i].alive) continue;
    int cx = CW_CLAMP(tile_x(s, s->e[i].x), 0, W - 1), cy = CW_CLAMP(tile_y(s, s->e[i].y), 0, H - 1);
    s->cell_idx[fill[cy * W + cx]++] = (int16_t)i;
  }
}
typedef void (*enemy_fn)(sim *s, int i, void *u);
static void each_enemy(sim *s, float x, float y, float r, enemy_fn fn, void *u) {
  const int W = s->m.w, H = s->m.h;
  int x0 = CW_CLAMP(tile_x(s, x - r - 1), 0, W - 1), x1 = CW_CLAMP(tile_x(s, x + r + 1), 0, W - 1);
  int y0 = CW_CLAMP(tile_y(s, y - r - 1), 0, H - 1), y1 = CW_CLAMP(tile_y(s, y + r + 1), 0, H - 1);
  for (int cy = y0; cy <= y1; cy++)
    for (int cx = x0; cx <= x1; cx++)
      for (int k = s->cell_start[cy * W + cx]; k < s->cell_start[cy * W + cx + 1]; k++) {
        int i = s->cell_idx[k];
        enemy *e = &s->e[i];
        if (!e->alive) continue;
        float dx = e->x - x, dy = e->y - y, rr = r + EK[e->kind].radius;
        if (dx * dx + dy * dy <= rr * rr) fn(s, i, u);
      }
}

static void hurt_player(sim *s, int i, float dmg, int from_kind, float fx, float fy) {
  player *p = &s->p[i];
  if (!p->active || p->down || s->god || p->charm[CH_WARD] > 0) return;
  float armor = CLASS[p->cls].armor * cw_pow(0.92f, (float)p->elixir[EL_ARMOR]);
  int d = CW_MAX(1, (int)(dmg * armor + 0.5f));
  p->hp -= d;
  s->hp_lost[i] += d, s->damage_taken += d, s->dmg_kind[from_kind >= 0 && from_kind < EK_KINDS ? from_kind : EK_KINDS] += d;
  p->hurt_t = 12;
  ev(s, EV_HURT, p->x, p->y, fx, fy, i, from_kind);
  if (p->hp <= 0) {
    p->hp = 0, p->down = true, p->down_ticks = 0;
    ev(s, EV_DOWN, p->x, p->y, 0, 0, i, 0), s->falls++;
  }
}

/* a player's shot */
static void fire(sim *s, int i, float ax, float ay) {
  player *p = &s->p[i];
  const class_stats *c = &CLASS[p->cls];
  int max_out = c->shots_max + (p->charm[CH_TRIPLE] > 0 ? 4 : 0) + (p->charm[CH_RAPID] > 0 ? 1 : 0);
  if (p->shots_out >= max_out) return;
  int n = p->charm[CH_TRIPLE] > 0 ? 3 : 1;
  float sp = c->shot_speed * (1.0f + 0.12f * (float)p->elixir[EL_SHOTSPEED]);
  float dmg = (float)(c->shot_dmg + p->elixir[EL_SHOT]) * (p->charm[CH_GIANT] > 0 ? 2.0f : 1.0f);
  float a0 = cw_atan2(ay, ax);
  for (int k = 0; k < n; k++) {
    float a = a0 + (n > 1 ? ((float)k - 1.0f) * 0.22f : 0);
    for (int j = 0; j < MAXB; j++)
      if (!s->b[j].alive) {
        bullet *b = &s->b[j];
        *b = (bullet){ true, (uint8_t)i, (uint8_t)c->shot_kind, (int8_t)(p->charm[CH_BOUNCE] > 0 ? 2 : 0), p->x + cw_cos(a) * 0.6f, p->y + cw_sin(a) * 0.6f,
                       cw_cos(a) * sp, cw_sin(a) * sp, dmg, 0, 0, 60 * 3, p->charm[CH_PIERCE] > 0 ? 4 : (c->shot_kind == SK_AXE ? 1 : 0), 0 };
        p->shots_out++;
        break;
      }
  }
  int cd = c->shot_cd - p->elixir[EL_SHOTSPEED];
  p->cd = CW_MAX(5, p->charm[CH_RAPID] > 0 ? cd / 2 : cd);
  ev(s, EV_SHOT, p->x, p->y, cw_cos(a0), cw_sin(a0), i, c->shot_kind);
}

/* magic: every monster on the screen (the team's view), a third of it at a shot potion */
static void magic_blast(sim *s, int i, float cx, float cy, float hw, float hh, float k) {
  const player *p = &s->p[i];
  int pw = (CLASS[p->cls].magic + p->elixir[EL_MAGIC]) * 6;
  int dmg = CW_MAX(1, (int)((float)pw * k));
  ev(s, EV_MAGIC, cx, cy, hw, hh, i, k < 1 ? 1 : 0);
  for (int e = 0; e < MAXE; e++) {
    enemy *m = &s->e[e];
    if (!m->alive || fabsf(m->x - cx) > hw || fabsf(m->y - cy) > hh) continue;
    if (k >= 1 && !line_clear(s, p->x, p->y, m->x, m->y) && (fabsf(m->x - p->x) > hw * 0.5f || fabsf(m->y - p->y) > hh * 0.5f)) {
      /* behind walls, far off: half */
      m->hp -= dmg / 2;
    } else m->hp -= dmg;
    m->flash = 8;
    if (m->kind == EK_DREAD && k >= 1) m->hp = 0; /* only magic ends a dread */
    if (m->hp <= 0) kill_enemy(s, e, i, m->x - cx, m->y - cy, true);
  }
  if (k >= 1 && p->cls == CL_MAGE) /* the Mage's magic cracks spawners too: a strength each */
    for (int j = 0; j < s->nsp; j++) {
      spawner *sp = &s->sp[j];
      if (sp->alive && fabsf(sp->x - cx) <= hw && fabsf(sp->y - cy) <= hh) spawner_hurt(s, j, 3, i);
    }
}

static void pickup_take(sim *s, int pi, int gi) {
  player *p = &s->p[pi];
  pickup *g = &s->g[gi];
  int fortune = p->charm[CH_FORTUNE] > 0 ? 2 : 1;
  switch (g->kind) {
  case IT_KEY:
    if (p->keys >= 9) return;
    p->keys++;
    break;
  case IT_POTION:
    if (p->potions >= 9) return;
    p->potions++;
    break;
  case IT_GOLD: p->score += 100 * fortune; ev(s, EV_TREASURE, g->x, g->y, 0, 0, pi, 0); break;
  case IT_CUP: p->score += 250 * fortune; ev(s, EV_TREASURE, g->x, g->y, 0, 0, pi, 1); break;
  case IT_CHEST:
  case IT_CHEST_TRAP:
    p->score += 500 * fortune;
    ev(s, EV_CHEST, g->x, g->y, 0, 0, pi, g->kind == IT_CHEST_TRAP);
    if (g->kind == IT_CHEST_TRAP) { /* a trap: monsters burst out */
      int kind = s->nsp ? s->sp[0].kind : EK_GHOUL;
      for (int k = 0; k < 6; k++) spawn_enemy(s, kind, CW_CLAMP(1 + s->floor / 30, 1, 3), g->x + cw_cos((float)k) * 1.2f, g->y + cw_sin((float)k) * 1.2f);
    } else if (rf(s) < 0.4f) drop_item(s, rf(s) < 0.5f ? IT_POTION : IT_CHARM, (int)(cw_rng_u32(&s->rng) % CH_KINDS), g->x, g->y + 0.8f, true);
    break;
  case IT_ROAST: p->hp += 100, s->food_eaten++; break;
  case IT_BREAD: p->hp += 50, s->food_eaten++; break;
  case IT_FLAGON: p->hp += 100, s->food_eaten++; break;
  case IT_PLATE: p->hp += 200, s->food_eaten++; break;
  case IT_ELIXIR:
    p->elixir[g->arg % EL_KINDS] = CW_MIN(p->elixir[g->arg % EL_KINDS] + 1, 4);
    ev(s, EV_ELIXIR, g->x, g->y, 0, 0, pi, g->arg);
    break;
  case IT_CHARM: sim_give_charm(s, pi, g->arg); ev(s, EV_CHARM, g->x, g->y, 0, 0, pi, g->arg); break;
  }
  ev(s, EV_PICK, g->x, g->y, 0, 0, pi, g->kind);
  g->alive = false;
}
void sim_give_charm(sim *s, int pi, int c) {
  player *p = &s->p[pi];
  c = (unsigned)c % CH_KINDS;
  if (c == CH_FIREBOMBS) p->charm[c] = CW_MIN(p->charm[c] + 4, 9);
  else p->charm[c] = CW_MIN(p->charm[c] + CHARM_TICKS[c], CHARM_TICKS[c] * 2);
  (void)s;
}

/* open the door group touching tile (x, y) */
static void open_door(sim *s, int x, int y, int pi) {
  static int qx[MZ_MAX * MZ_MAX], qy[MZ_MAX * MZ_MAX];
  int qh = 0, qt = 0;
  qx[qt] = x, qy[qt++] = y;
  s->t[y][x] = T_FLOOR + 100; /* seen */
  float cx = 0, cy = 0;
  int n = 0;
  while (qh < qt) {
    int cx_ = qx[qh], cy_ = qy[qh++];
    static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
    for (int d = 0; d < 4; d++) {
      int nx = cx_ + DX[d], ny = cy_ + DY[d];
      if (mz_in(&s->m, nx, ny) && s->t[ny][nx] == T_DOOR) s->t[ny][nx] = T_FLOOR + 100, qx[qt] = nx, qy[qt++] = ny;
    }
  }
  for (int k = 0; k < qt; k++) {
    tile_open(s, qx[k], qy[k]);
    s->door_open[qy[k]][qx[k]] = 1;
    cx += mz_wx(&s->m, qx[k]), cy += mz_wz(&s->m, qy[k]), n++;
  }
  s->doors_opened++;
  ev(s, EV_DOOR, cx / (float)n, cy / (float)n, (float)n, 0, pi, 0);
}

static void player_step(sim *s, int i, const input *in) {
  player *p = &s->p[i];
  if (!p->active) return;
  if (p->down) {
    p->down_ticks++;
    if (in->join && p->down_ticks > 120 && sim_alive_players(s) > 0) { /* back in, beside a friend, with fresh health */
      for (int k = 0; k < MAXP; k++)
        if (k != i && s->p[k].active && !s->p[k].down) { p->x = s->p[k].x, p->y = s->p[k].y; break; }
      p->down = false, p->hp = 1000, p->score /= 2, p->inv = 120;
      ev(s, EV_REJOIN, p->x, p->y, 0, 0, i, 0);
    }
    return;
  }
  const class_stats *c = &CLASS[p->cls];
  if (s->tick % 72 == 0 && !s->god) p->hp--; /* the clock: one every 1.2 s */
  if (p->hp <= 0 && !p->down) { p->hp = 0, p->down = true, p->down_ticks = 0; ev(s, EV_DOWN, p->x, p->y, 0, 0, i, 0), s->falls++; return; }
  if (p->cd > 0) p->cd--;
  if (p->melee_cd > 0) p->melee_cd--;
  if (p->inv > 0) p->inv--;
  if (p->hurt_t > 0) p->hurt_t--;
  for (int k = 0; k < CH_KINDS; k++)
    if (k != CH_FIREBOMBS && p->charm[k] > 0) p->charm[k]--;
  if (p->stun > 0) { p->stun--; p->vx *= 0.8f, p->vy *= 0.8f; return; }
  float mx = in->mx, my = in->my, ml = sqrtf(mx * mx + my * my);
  if (ml > 1) mx /= ml, my /= ml, ml = 1;
  if (ml > 0.25f) p->face = cw_atan2(my, mx);
  /* hold fire: stand and shoot the way you face (turning with the stick) */
  p->firing = in->fire ? p->firing + 1 : 0;
  float sp = c->speed * (1.0f + 0.08f * (float)p->elixir[EL_SPEED]) * (p->charm[CH_HASTE] > 0 ? 1.4f : 1.0f);
  if (in->fire) {
    mx = my = 0;
    if (p->cd <= 0) fire(s, i, cw_cos(p->face), cw_sin(p->face));
  }
  if (in->magic) {
    if (p->charm[CH_FIREBOMBS] > 0) { /* a firebomb first: thrown the way you face */
      p->charm[CH_FIREBOMBS]--;
      for (int j = 0; j < MAXB; j++)
        if (!s->b[j].alive) {
          float tx = p->x + cw_cos(p->face) * 7, ty = p->y + cw_sin(p->face) * 7;
          s->b[j] = (bullet){ true, (uint8_t)i, SK_FIREBOMB, 0, p->x, p->y, (tx - p->x) / 0.6f, (ty - p->y) / 0.6f, 20, tx, ty, 36, 0, 0 };
          ev(s, EV_FIREBOMB, p->x, p->y, tx, ty, i, 0);
          break;
        }
    } else if (p->potions > 0) {
      p->potions--;
      s->potions_used++;
      magic_blast(s, i, s->cam_x, s->cam_y, VIEW_HW, VIEW_HH, 1.0f);
    }
  }
  p->vx += (mx * sp - p->vx) * 0.4f;
  p->vy += (my * sp - p->vy) * 0.4f;
  float nx = p->x + p->vx / TICK, ny = p->y + p->vy / TICK;
  /* the team on one screen: nobody past its edge */
  float lo_x = -1e9f, hi_x = 1e9f, lo_y = -1e9f, hi_y = 1e9f;
  {
    for (int k = 0; k < MAXP; k++) {
      const player *q = &s->p[k];
      if (k == i || !q->active || q->down) continue;
      lo_x = CW_MAX(lo_x, q->x - 2 * TETHER_HW), hi_x = CW_MIN(hi_x, q->x + 2 * TETHER_HW);
      lo_y = CW_MAX(lo_y, q->y - 2 * TETHER_HH), hi_y = CW_MIN(hi_y, q->y + 2 * TETHER_HH);
    }
    if (nx < lo_x && nx < p->x) nx = p->x;
    if (nx > hi_x && nx > p->x) nx = p->x;
    if (ny < lo_y && ny < p->y) ny = p->y;
    if (ny > hi_y && ny > p->y) ny = p->y;
  }
  /* players are solid to each other, softly */
  for (int k = 0; k < MAXP; k++) {
    const player *q = &s->p[k];
    if (k == i || !q->active || q->down) continue;
    float dx = nx - q->x, dy = ny - q->y, d2 = dx * dx + dy * dy;
    if (d2 < 0.75f * 0.75f && d2 > 1e-6f) {
      float d = sqrtf(d2), push = (0.75f - d) * 0.5f;
      nx += dx / d * push, ny += dy / d * push;
    }
  }
  /* walls: the player's box pushed out of every solid tile it overlaps, along the shallower side (so it slides along
   * walls and round corners, and is never left inside one); a door bumped with a key in hand opens */
  const float R = p->charm[CH_GIANT] > 0 ? 0.55f : 0.4f, H = MZ_TILE * 0.5f;
  if (p->keys > 0 && ml > 0.25f) { /* a key in hand opens a door within reach the way you walk (a friend in the way
                                    * does not keep you from it) */
    float reach = R + 0.6f;
    int x0 = tile_x(s, p->x - reach), x1 = tile_x(s, p->x + reach), y0 = tile_y(s, p->y - reach), y1 = tile_y(s, p->y + reach);
    for (int ty = y0; ty <= y1 && p->keys > 0; ty++)
      for (int tx = x0; tx <= x1 && p->keys > 0; tx++) {
        if (!mz_in(&s->m, tx, ty) || s->t[ty][tx] != T_DOOR) continue;
        float dx = mz_wx(&s->m, (float)tx) - p->x, dy = mz_wz(&s->m, (float)ty) - p->y;
        if (fabsf(dx) > H + reach || fabsf(dy) > H + reach || dx * mx + dy * my <= 0) continue;
        p->keys--, s->keys_used++;
        open_door(s, tx, ty, i);
      }
  }
  for (int it = 0; it < 3; it++) {
    int x0 = tile_x(s, nx - R), x1 = tile_x(s, nx + R), y0 = tile_y(s, ny - R), y1 = tile_y(s, ny + R);
    bool moved = false;
    for (int ty = y0; ty <= y1; ty++)
      for (int tx = x0; tx <= x1; tx++) {
        if (!sim_solid(s, tx, ty)) continue;
        if (mz_in(&s->m, tx, ty) && s->t[ty][tx] == T_DOOR && p->keys > 0) {
          p->keys--, s->keys_used++;
          open_door(s, tx, ty, i);
          continue;
        }
        float cx = mz_wx(&s->m, (float)tx), cy = mz_wz(&s->m, (float)ty), dx = nx - cx, dy = ny - cy;
        float ox = H + R - fabsf(dx), oy = H + R - fabsf(dy);
        if (ox <= 0 || oy <= 0) continue;
        if (ox < oy) nx += dx < 0 ? -ox : ox, p->vx = 0;
        else ny += dy < 0 ? -oy : oy, p->vy = 0;
        moved = true;
      }
    if (!moved) break;
  }
  /* a push (a friend, a wall) never carries anyone past the screen's edge: then the step does not happen */
  bool out = (nx < lo_x && nx < p->x) || (nx > hi_x && nx > p->x) || (ny < lo_y && ny < p->y) || (ny > hi_y && ny > p->y);
  if (!out) p->x = nx, p->y = ny;
  else p->vx = p->vy = 0;
  /* melee: walking into a monster fights it (the Brute cleaves round in front) */
  if (ml > 0.25f && !in->fire && p->melee_cd <= 0) {
    float fx = cw_cos(p->face), fy = cw_sin(p->face);
    int hits = 0;
    float reach = (p->cls == CL_BRUTE ? 1.25f : 0.95f) * (p->charm[CH_GIANT] > 0 ? 1.4f : 1.0f);
    float arc = p->cls == CL_BRUTE ? 0.2f : 0.55f;
    for (int e = 0; e < MAXE && hits < (p->cls == CL_BRUTE ? 4 : 1); e++) {
      enemy *m = &s->e[e];
      if (!m->alive || sim_hexer_hidden(s, m)) continue;
      float dx = m->x - p->x, dy = m->y - p->y, d2 = dx * dx + dy * dy, rr = reach + EK[m->kind].radius;
      if (d2 > rr * rr) continue;
      float d = sqrtf(d2);
      if (d > 0.05f && (dx * fx + dy * fy) / d < arc) continue;
      int dmg = (c->melee + p->elixir[EL_MELEE]) * (p->charm[CH_GIANT] > 0 ? 3 : 1);
      if (m->kind == EK_DREAD) { m->kx += fx * 0.2f, m->ky += fy * 0.2f; hits++; continue; }
      m->hp -= dmg, m->flash = 6;
      m->kx += fx * 0.08f, m->ky += fy * 0.08f;
      hits++;
      ev(s, EV_MELEE, m->x, m->y, fx, fy, i, p->cls);
      if (m->hp <= 0) kill_enemy(s, e, i, fx * 8, fy * 8, true);
    }
    for (int j = 0; j < s->nsp && !hits; j++) { /* or a spawner in front */
      spawner *sp = &s->sp[j];
      float dx = sp->x - p->x, dy = sp->y - p->y;
      if (sp->alive && dx * dx + dy * dy < (reach + 0.9f) * (reach + 0.9f) && dx * fx + dy * fy > 0) spawner_hurt(s, j, c->melee + p->elixir[EL_MELEE], i), hits++, ev(s, EV_MELEE, sp->x, sp->y, fx, fy, i, p->cls);
    }
    if (hits) p->melee_cd = c->melee_cd - (p->cls == CL_BRUTE ? 0 : 0);
  }
  /* what lies on the floor: walk over it */
  float lr = p->charm[CH_LODESTONE] > 0 ? 10.0f : 0;
  for (int k = 0; k < MAXG; k++) {
    pickup *g = &s->g[k];
    if (!g->alive) continue;
    float dx = g->x - p->x, dy = g->y - p->y, d2 = dx * dx + dy * dy;
    if (lr > 0 && d2 < lr * lr && d2 > 0.5f && g->kind != IT_CHEST && g->kind != IT_CHEST_TRAP) { /* the lodestone pulls */
      float d = sqrtf(d2);
      g->x -= dx / d * 0.18f, g->y -= dy / d * 0.18f;
    }
    if (d2 < 0.8f * 0.8f) pickup_take(s, i, k);
  }
  /* the floor's features under the player */
  int tx = tile_x(s, p->x), ty = tile_y(s, p->y);
  for (int k = 0; k < s->m.ntrap; k++)
    if (s->m.trap[k][0] == tx && s->m.trap[k][1] == ty && s->m.trap[k][2] != 0) {
      int grp = s->m.trap[k][2];
      for (int y = 0; y < s->m.h; y++)
        for (int x = 0; x < s->m.w; x++)
          if (s->t[y][x] == T_TRAPWALL && s->m.tgroup[y][x] == grp) tile_open(s, x, y), s->door_open[y][x] = 1;
      s->m.trap[k][2] = 0;
      ev(s, EV_TRAP, p->x, p->y, 0, 0, i, grp);
    }
  if (s->m.ntele >= 2 && p->inv <= 0)
    for (int k = 0; k < s->m.ntele; k++) {
      if (s->m.tele[k][0] != tx || s->m.tele[k][1] != ty) continue;
      int best = -1;
      float bd = 1e9f;
      for (int j = 0; j < s->m.ntele; j++) {
        if (j == k) continue;
        float dx = (float)s->m.tele[j][0] - (float)tx, dy = (float)s->m.tele[j][1] - (float)ty, d = dx * dx + dy * dy;
        if (d < bd) bd = d, best = j;
      }
      if (best >= 0) {
        /* the whole team goes (one screen holds everyone), each to the pad's middle, a step apart; nobody is sent back
         * while their inv runs */
        float dx = mz_wx(&s->m, s->m.tele[best][0]), dy = mz_wz(&s->m, s->m.tele[best][1]);
        static const float OX[MAXP] = { 0, 0.45f, -0.45f, 0 }, OY[MAXP] = { 0, 0.3f, 0.3f, -0.45f };
        for (int k = 0, n = 0; k < MAXP; k++) {
          player *q = &s->p[k];
          if (!q->active || q->down) continue;
          float ox = q->x, oy = q->y;
          q->x = dx + OX[n], q->y = dy + OY[n], n++;
          q->inv = 90;
          ev(s, EV_TELEPORT, ox, oy, q->x, q->y, k, 0);
        }
      }
      break;
    }
  for (int k = 0; k < s->m.nambush; k++) { /* a room that springs: monsters rise out of the floor all round it */
    const uint8_t *a = s->m.ambush[k];
    if (s->ambush_done[k] || abs(tx - a[0]) > 1 || abs(ty - a[1]) > 1) continue;
    s->ambush_done[k] = 1;
    int made = 0;
    for (int ring = 3; ring <= 6 && made < a[4]; ring++)
      for (int t = 0; t < 24 && made < a[4]; t++) {
        float ang = (float)t * 0.2618f + (float)ring, x = mz_wx(&s->m, a[0]) + cw_cos(ang) * (float)ring * 1.6f, y = mz_wz(&s->m, a[1]) + cw_sin(ang) * (float)ring * 1.6f;
        if (!sim_wall_at(s, x, y) && spawn_enemy(s, a[2], a[3], x, y) >= 0) made++, ev(s, EV_SPAWN, x, y, x, y, a[2], a[3]);
      }
    ev(s, EV_AMBUSH, mz_wx(&s->m, a[0]), mz_wz(&s->m, a[1]), 0, 0, i, made);
  }
  for (int k = 0; k < s->m.nexit; k++)
    if (s->m.exit[k][0] == tx && s->m.exit[k][1] == ty && s->state == ST_PLAY) {
      s->state = ST_EXIT, s->state_ticks = 0;
      s->next_floor = s->floor + s->m.exit[k][2];
      p->at_exit = 1;
      ev(s, EV_EXIT, p->x, p->y, 0, 0, i, s->m.exit[k][2]);
    }
  for (int k = 0; k < CH_KINDS; k++)
    if (k == CH_EMBERS && p->charm[k] > 0) p->ember_a += 0.12f;
}

/* a shot meets a monster */
typedef struct bctx { int b; bool stop; } bctx;
static void bullet_hit(sim *s, int i, void *u) {
  bctx *c = u;
  bullet *b = &s->b[c->b];
  if (c->stop || !b->alive) return;
  enemy *e = &s->e[i];
  if (!e->alive || sim_hexer_hidden(s, e)) return;
  const player *p = &s->p[b->owner];
  if (e->kind == EK_DREAD) { /* shots only push a dread back */
    e->kx += b->vx * 0.004f, e->ky += b->vy * 0.004f, e->flash = 3;
    ev(s, EV_HIT, b->x, b->y, b->vx, b->vy, e->kind, b->kind);
    b->alive = false, c->stop = true;
    return;
  }
  e->hp -= (int)(b->dmg + 0.5f);
  e->flash = 5;
  e->kx += b->vx * 0.006f / (float)e->str, e->ky += b->vy * 0.006f / (float)e->str;
  if (p->charm[CH_FROST] > 0) { if (e->slow > 0) e->frozen = 90; e->slow = 150; }
  ev(s, EV_HIT, b->x, b->y, b->vx, b->vy, e->kind, b->kind);
  if (p->charm[CH_CHAIN] > 0) { /* the hit arcs on to three more within 4 m */
    int n = 0;
    for (int k = 0; k < MAXE && n < 3; k++) {
      enemy *o = &s->e[k];
      if (!o->alive || o == e || o->kind == EK_DREAD) continue;
      float dx = o->x - e->x, dy = o->y - e->y;
      if (dx * dx + dy * dy > 16) continue;
      o->hp -= 1, o->flash = 4, n++;
      ev(s, EV_ARC, e->x, e->y, o->x, o->y, b->owner, 0);
      if (o->hp <= 0) kill_enemy(s, k, b->owner, dx, dy, true);
    }
  }
  if (b->kind == SK_FIRE) queue_boom(s, b->x, b->y, 1.2f, 1, 0, b->owner, false); /* the fireball bursts */
  if (e->hp <= 0) kill_enemy(s, i, b->owner, b->vx, b->vy, true);
  if (b->pierce-- <= 0) b->alive = false, c->stop = true;
}

static void bullet_end(sim *s, bullet *b) {
  if (b->owner < MAXP && s->p[b->owner].shots_out > 0) s->p[b->owner].shots_out--;
  b->alive = false;
}

static void bullets_step(sim *s) {
  for (int j = 0; j < MAXB; j++) {
    bullet *b = &s->b[j];
    if (!b->alive) continue;
    b->age++;
    if (b->kind == SK_LOB || b->kind == SK_FIREBOMB) { /* lobbed over walls: lands where it was aimed */
      b->x += b->vx / TICK, b->y += b->vy / TICK;
      if (--b->life > 0) continue;
      if (b->kind == SK_FIREBOMB) queue_boom(s, b->tx, b->ty, 4.5f, b->dmg, 0, b->owner, true);
      else {
        ev(s, EV_LOB_LAND, b->tx, b->ty, 1.2f, 0, 0, 0);
        for (int k = 0; k < MAXP; k++) {
          const player *p = &s->p[k];
          float dx = p->x - b->tx, dy = p->y - b->ty;
          if (p->active && !p->down && dx * dx + dy * dy < 1.2f * 1.2f) hurt_player(s, k, b->dmg, EK_SLINGER, dx, dy);
        }
        for (int z = 0; z < MAXZ; z++)
          if (!s->z[z].alive) { s->z[z] = (zone){ true, 1, b->tx, b->ty, 1.0f, 120 }; break; }
      }
      bullet_end(s, b);
      continue;
    }
    if (b->owner < MAXP && b->kind != SK_FIREBOMB && s->p[b->owner].charm[CH_SEEKER] > 0 && b->age > 4) { /* seekers curve to the nearest */
      float spd = sqrtf(b->vx * b->vx + b->vy * b->vy), fx = b->vx / spd, fy = b->vy / spd, bd = 10 * 10;
      int best = -1;
      for (int k = 0; k < MAXE; k += 2) {
        const enemy *e = &s->e[k];
        if (!e->alive) continue;
        float dx = e->x - b->x, dy = e->y - b->y, d = dx * dx + dy * dy;
        if (d < bd && dx * fx + dy * fy > 0.4f * sqrtf(d)) bd = d, best = k;
      }
      if (best >= 0) {
        float dx = s->e[best].x - b->x, dy = s->e[best].y - b->y, l = sqrtf(dx * dx + dy * dy);
        b->vx += (dx / l * spd - b->vx) * 0.18f, b->vy += (dy / l * spd - b->vy) * 0.18f;
      }
    }
    float ox = b->x, oy = b->y;
    b->x += b->vx / TICK, b->y += b->vy / TICK;
    if (--b->life <= 0) { bullet_end(s, b); continue; }
    int tx = tile_x(s, b->x), ty = tile_y(s, b->y);
    if (sim_solid(s, tx, ty)) {
      bool bounced = false;
      if (mz_in(&s->m, tx, ty) && s->sp_at[ty][tx] && b->owner < MAXP) spawner_hurt(s, s->sp_at[ty][tx] - 1, (int)(b->dmg + 0.5f), b->owner);
      else if (mz_in(&s->m, tx, ty) && s->t[ty][tx] == T_BREAK && b->owner < MAXP) {
        if (s->wall_hp[ty][tx] > 0 && --s->wall_hp[ty][tx] == 0) tile_open(s, tx, ty), ev(s, EV_WALL_BREAK, mz_wx(&s->m, tx), mz_wz(&s->m, ty), 0, 0, b->owner, 0);
      } else if (b->bounce > 0) { /* glance off: back the way it came on the axis it struck */
        int otx = tile_x(s, ox), oty = tile_y(s, oy);
        if (otx != tx) b->vx = -b->vx;
        if (oty != ty) b->vy = -b->vy;
        b->x = ox, b->y = oy, b->bounce--;
        bounced = true;
        ev(s, EV_BOUNCE, ox, oy, 0, 0, b->owner, 0);
      }
      if (!bounced) {
        if (b->kind == SK_FIRE) queue_boom(s, ox, oy, 1.2f, 1, 0, b->owner, false);
        else ev(s, EV_HIT, ox, oy, b->vx, b->vy, 255, b->kind);
        bullet_end(s, b);
      }
      continue;
    }
    if (b->owner >= MAXP) { /* a monster's shot: the players */
      for (int k = 0; k < MAXP; k++) {
        const player *p = &s->p[k];
        float dx = p->x - b->x, dy = p->y - b->y;
        if (!p->active || p->down || dx * dx + dy * dy > 0.55f * 0.55f) continue;
        hurt_player(s, k, b->dmg, b->kind == SK_HEX ? EK_HEXER : EK_IMP, b->vx, b->vy);
        b->alive = false;
        break;
      }
      continue;
    }
    /* a player's shot: food and potions break, other players are stunned, monsters are hit */
    for (int k = 0; k < MAXG; k++) {
      pickup *g = &s->g[k];
      if (!g->alive || (g->kind != IT_BREAD && g->kind != IT_FLAGON && g->kind != IT_POTION)) continue;
      float dx = g->x - b->x, dy = g->y - b->y;
      if (dx * dx + dy * dy > 0.5f * 0.5f) continue;
      g->alive = false;
      if (g->kind == IT_POTION) magic_blast(s, b->owner, g->x, g->y, 5.0f, 5.0f, 0.34f), ev(s, EV_POTION_BREAK, g->x, g->y, 0, 0, b->owner, 0);
      else ev(s, EV_FOOD_BREAK, g->x, g->y, 0, 0, b->owner, g->kind);
      bullet_end(s, b);
      break;
    }
    if (!b->alive) continue;
    for (int k = 0; k < MAXP; k++) {
      player *p = &s->p[k];
      float dx = p->x - b->x, dy = p->y - b->y;
      if (k == b->owner || !p->active || p->down || dx * dx + dy * dy > 0.5f * 0.5f) continue;
      if (p->stun > 0 || p->inv > 0) { bullet_end(s, b); break; } /* a second stun waits: a friend never locks you down */
      p->stun = 24, p->inv = 24 + 60;
      ev(s, EV_STUN, p->x, p->y, 0, 0, k, b->owner);
      bullet_end(s, b);
      break;
    }
    if (!b->alive) continue;
    bctx c = { j, false };
    each_enemy(s, b->x, b->y, 0.15f, bullet_hit, &c);
    if (!b->alive && b->owner < MAXP && s->p[b->owner].shots_out > 0) s->p[b->owner].shots_out--;
  }
}

static void boom_hit(sim *s, int i, void *u) {
  const boom *bm = u;
  enemy *e = &s->e[i];
  if (e->kind == EK_DREAD) return;
  float dx = e->x - bm->x, dy = e->y - bm->y, d = cw_clampf(1 - (dx * dx + dy * dy) / (bm->r * bm->r), 0.3f, 1);
  e->hp -= CW_MAX(1, (int)(bm->dmg * d + 0.5f));
  e->flash = 6;
  float l = sqrtf(dx * dx + dy * dy);
  if (l > 0.01f) e->kx += dx / l * 0.15f * d, e->ky += dy / l * 0.15f * d;
  if (e->hp <= 0) kill_enemy(s, i, bm->owner, dx * 6, dy * 6, true);
}
static void booms_step(sim *s) {
  for (int i = 0; i < MAXX; i++) {
    boom *bm = &s->x[i];
    if (bm->r == 0 || bm->delay-- > 0) continue;
    boom b = *bm;
    bm->r = 0;
    s->nboom_total++;
    ev(s, EV_BOOM, b.x, b.y, b.r, 0, b.big, b.owner);
    each_enemy(s, b.x, b.y, b.r, boom_hit, &b);
    if (b.big)
      for (int k = 0; k < s->nsp; k++) {
        spawner *sp = &s->sp[k];
        float dx = sp->x - b.x, dy = sp->y - b.y;
        if (sp->alive && dx * dx + dy * dy < b.r * b.r) spawner_hurt(s, k, 3, b.owner);
      }
  }
}

static void zones_step(sim *s) {
  for (int i = 0; i < MAXZ; i++) {
    zone *z = &s->z[i];
    if (!z->alive) continue;
    if (--z->life <= 0) { z->alive = false; continue; }
    if (s->tick % 20) continue;
    for (int k = 0; k < MAXP; k++) {
      const player *p = &s->p[k];
      float dx = p->x - z->x, dy = p->y - z->y;
      if (p->active && !p->down && dx * dx + dy * dy < z->r * z->r) hurt_player(s, k, 2, z->kind == 0 ? EK_BLOATER : EK_SLINGER, 0, 0);
    }
  }
}

/* the monsters' turn */
static void enemies_step(sim *s) {
  if (s->flow_dirty || s->flow_age++ >= 6) { CW_PROFILE_SCOPE("sim.flow"); build_flow(s); }
  for (int i = 0; i < MAXE; i++) {
    enemy *e = &s->e[i];
    if (!e->alive) continue;
    e->age++;
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < MAXP; k++) {
      const player *p = &s->p[k];
      if (!p->active || p->down || (p->charm[CH_SHROUD] > 0 && k != e->tgt)) continue;
      float dx = p->x - e->x, dy = p->y - e->y, d = dx * dx + dy * dy;
      if (d < bd) bd = d, best = k;
    }
    float vx = 0, vy = 0, sp = e->speed;
    if (e->kind == EK_PILFERER && e->carry >= 0) { /* away to the exit with the loot */
      if (!flow_step(s, s->eflow, e->x, e->y, &vx, &vy)) { /* there: gone */
        e->carry = -1;
        kill_enemy(s, i, -1, 0, 0, false);
        continue;
      }
      sp *= 1.1f;
    } else if (best >= 0) {
      const player *p = &s->p[best];
      float d = sqrtf(bd);
      bool sight = d < 9 && line_clear(s, e->x, e->y, p->x, p->y);
      if (sight || d < 1.5f) {
        if (d > 0.01f) vx = (p->x - e->x) / d, vy = (p->y - e->y) / d;
      } else if (!flow_step(s, s->flow, e->x, e->y, &vx, &vy) && d > 0.01f)
        vx = (p->x - e->x) / d, vy = (p->y - e->y) / d;
      /* ranged attacks: a tell, then the shot (imps and hexers by sight, slingers over walls) */
      bool ranged = e->kind == EK_IMP || e->kind == EK_HEXER || e->kind == EK_SLINGER;
      if (ranged) {
        if (e->act) {
          sp = 0;
          if (++e->act_t == 30) {
            float a = cw_atan2(p->y - e->y, p->x - e->x);
            for (int j = 0; j < MAXB; j++)
              if (!s->b[j].alive) {
                bullet *b = &s->b[j];
                if (e->kind == EK_SLINGER) {
                  float tx = p->x + p->vx * 0.4f, ty = p->y + p->vy * 0.4f;
                  *b = (bullet){ true, 255, SK_LOB, 0, e->x, e->y, (tx - e->x) / 0.75f, (ty - e->y) / 0.75f, edmg(e), tx, ty, 45, 0, 0 };
                } else {
                  float v = e->kind == EK_IMP ? 10.0f : 12.0f;
                  *b = (bullet){ true, 255, (uint8_t)(e->kind == EK_IMP ? SK_IMPFIRE : SK_HEX), 0, e->x + cw_cos(a) * 0.5f, e->y + cw_sin(a) * 0.5f, cw_cos(a) * v, cw_sin(a) * v,
                                 edmg(e), 0, 0, 120, 0, 0 };
                }
                ev(s, EV_ESHOT, e->x, e->y, cw_cos(a), cw_sin(a), e->kind, b->kind);
                break;
              }
          }
          if (e->act_t >= 44) e->act = 0, e->act_t = 0, e->act_next = (e->kind == EK_SLINGER ? 300 : 240) + (int)(rf(s) * 120);
        } else if (--e->act_next <= 0 && e->atk == 0) {
          bool can = e->kind == EK_SLINGER ? d > 3.5f && d < 11 : sight && d > 2.5f && d < 9;
          if (can) e->act = 1, e->act_t = 0, e->face = cw_atan2(p->y - e->y, p->x - e->x);
          else e->act_next = 20;
        }
        if (e->kind == EK_SLINGER && d < 5.5f && sight && !e->act) vx = -vx, vy = -vy; /* keeps its distance */
      }
      if (e->kind == EK_DREAD && e->hold > 0) sp = 0;
    }
    /* the screen is full: a monster near it waits off it (a queue, not a carpet) */
    if (at_cap(s) && !sim_on_screen(s, e->x, e->y, 0) && sim_on_screen(s, e->x, e->y, 12.0f)) {
      float ox = e->x + vx * 0.3f, oy = e->y + vy * 0.3f;
      if (fabsf(ox - s->view_x) < fabsf(e->x - s->view_x) || fabsf(oy - s->view_y) < fabsf(e->y - s->view_y)) sp = 0;
    }
    if (e->atk > 0) sp = 0;
    if (e->frozen > 0) e->frozen--, sp = 0;
    if (e->slow > 0) e->slow--, sp *= 0.5f;
    if (e->age < 20) sp *= 0.5f;
    /* repulse: nothing within 2.5 m of a player holding it */
    for (int k = 0; k < MAXP; k++) {
      const player *p = &s->p[k];
      if (!p->active || p->down || p->charm[CH_REPULSE] <= 0) continue;
      float dx = e->x - p->x, dy = e->y - p->y, d2 = dx * dx + dy * dy;
      if (d2 < 2.5f * 2.5f && d2 > 1e-4f) { float d = sqrtf(d2); e->kx += dx / d * 0.05f, e->ky += dy / d * 0.05f; }
    }
    vx = vx * sp + e->kx * 60.0f;
    vy = vy * sp + e->ky * 60.0f;
    e->kx *= 0.82f, e->ky *= 0.82f;
    cw_crowd_set_velocity(s->crowd, e->agent, (t3_vec3){ vx, 0, vy });
    if (e->flash > 0) e->flash--;
    if (e->atk_cd > 0) e->atk_cd--;
  }
  { CW_PROFILE_SCOPE("sim.crowd"); cw_crowd_step(s->crowd, 1.0f / TICK); }
  s->crowd_ms = cw_crowd_get_stats(s->crowd).step_ms;
  for (int i = 0; i < MAXE; i++) {
    enemy *e = &s->e[i];
    if (!e->alive) continue;
    t3_vec3 p = cw_crowd_position(s->crowd, e->agent);
    float dx = p.x - e->x, dy = p.z - e->y;
    if (dx * dx + dy * dy > 0.0004f && e->atk == 0 && e->act == 0) e->face = cw_atan2(dy, dx);
    e->x = p.x, e->y = p.z;
  }
  { CW_PROFILE_SCOPE("sim.grid"); build_grid(s); }
  /* contact: the touchers touch, the fighters wind up and strike, a dread holds on, a pilferer grabs */
  for (int i = 0; i < MAXE; i++) {
    enemy *e = &s->e[i];
    if (!e->alive) continue;
    const melee *m = &MEL[e->kind];
    float touch = EK[e->kind].radius + 0.42f;
    if (e->atk > 0) { /* in an attack: the strike lands on one tick, on a player still in reach and in front */
      e->atk++;
      if (e->atk == m->windup + 1 && e->tgt >= 0) {
        const player *p = &s->p[e->tgt];
        float dx = p->x - e->x, dy = p->y - e->y, d = sqrtf(dx * dx + dy * dy);
        bool hit = p->active && !p->down && d <= touch + m->reach && (d < 0.05f || (dx * cw_cos(e->face) + dy * cw_sin(e->face)) / d >= m->arc);
        if (hit) hurt_player(s, e->tgt, edmg(e), e->kind, dx, dy);
        ev(s, EV_SWIPE, e->x, e->y, cw_cos(e->face), cw_sin(e->face), e->kind, hit);
        if (e->kind == EK_HOUND) { /* the pounce: it leaps in */
          cw_crowd_teleport(s->crowd, e->agent, (t3_vec3){ e->x + dx * 0.6f, 0, e->y + dy * 0.6f });
        }
      }
      if (e->atk > m->windup + m->recover) e->atk = 0, e->atk_cd = m->cooldown;
      continue;
    }
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < MAXP; k++) {
      const player *p = &s->p[k];
      if (!p->active || p->down) continue;
      float dx = p->x - e->x, dy = p->y - e->y, d = dx * dx + dy * dy;
      if (d < bd) bd = d, best = k;
    }
    if (best < 0) continue;
    player *p = &s->p[best];
    float d = sqrtf(bd);
    if (e->kind == EK_SHADE || e->kind == EK_BLOATER) { /* a touch, and it is gone (a bloater bursts) */
      if (d <= touch + m->trigger) {
        hurt_player(s, best, edmg(e), e->kind, p->x - e->x, p->y - e->y);
        kill_enemy(s, i, -1, 0, 0, false);
      }
      continue;
    }
    if (e->kind == EK_DREAD) {
      if (d <= touch + 0.1f) {
        e->hold++;
        if (e->hold % 15 == 0) {
          int before = p->hp;
          hurt_player(s, best, 10.0f / CLASS[p->cls].armor, EK_DREAD, 0, 0);
          p->drained += before - p->hp;
          ev(s, EV_DRAIN, e->x, e->y, p->x, p->y, best, 0);
          if (p->drained >= 160 || p->down) { p->drained = 0; kill_enemy(s, i, -1, 0, 0, false); }
        }
      } else e->hold = 0;
      continue;
    }
    if (e->kind == EK_PILFERER) {
      if (d <= touch + 0.1f && e->carry < 0) { /* grabs a potion, else a key, else a charm's time */
        int what = -1;
        if (p->potions > 0) p->potions--, what = IT_POTION;
        else if (p->keys > 0) p->keys--, what = IT_KEY;
        if (what >= 0) e->carry = what, p->stolen = what, ev(s, EV_STEAL, e->x, e->y, 0, 0, best, what);
        else e->carry = IT_GOLD, p->score = CW_MAX(0, p->score - 200), ev(s, EV_STEAL, e->x, e->y, 0, 0, best, IT_GOLD);
      }
      continue;
    }
    if (e->atk_cd > 0 || e->act) continue;
    if (d <= touch + m->trigger) {
      e->atk = 1, e->tgt = best;
      float dx = p->x - e->x, dy = p->y - e->y;
      if (dx * dx + dy * dy > 1e-4f) e->face = cw_atan2(dy, dx);
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
    x0 = CW_MIN(x0, p->x), x1 = CW_MAX(x1, p->x), y0 = CW_MIN(y0, p->y), y1 = CW_MAX(y1, p->y), n++;
  }
  if (!n) { *cx = s->cam_x, *cy = s->cam_y, *hw = *hh = 0; return; }
  *cx = (x0 + x1) * 0.5f, *cy = (y0 + y1) * 0.5f, *hw = (x1 - x0) * 0.5f, *hh = (y1 - y0) * 0.5f;
}

static void hash_state(sim *s) {
  uint32_t h = 2166136261u;
#define HMIX(v) do { uint32_t _v; float _f = (float)(v); memcpy(&_v, &_f, 4); h = (h ^ _v) * 16777619u; } while (0)
  for (int i = 0; i < MAXP; i++) { HMIX(s->p[i].x); HMIX(s->p[i].y); HMIX(s->p[i].hp); HMIX(s->p[i].score); }
  HMIX(s->nenemies); HMIX(s->killed_total); HMIX(s->floor); HMIX(s->tick);
  for (int i = 0; i < MAXE; i += 7) if (s->e[i].alive) { HMIX(s->e[i].x); HMIX(s->e[i].y); }
  s->hash = h;
}

void sim_step(sim *s, const input in[MAXP]) {
  s->ev_n = 0;
  s->tick++;
  s->state_ticks++;
  if (s->state == ST_EXIT) { /* the team goes down: a second's fade, then the game loads the next floor */
    if (s->state_ticks >= 60) s->state = ST_WON; /* main.c (or the soak) sees ST_WON and calls sim_floor_start */
    hash_state(s);
    return;
  }
  if (s->state == ST_WON) { hash_state(s); return; }
  if (s->state == ST_OVER) {
    for (int i = 0; i < MAXP; i++) if (s->p[i].active) player_step(s, i, &in[i]);
    hash_state(s);
    return;
  }
  s->floor_ticks++;
  if (s->floor_ticks == 180 * 60) { /* held too long: every door opens */
    for (int y = 0; y < s->m.h; y++)
      for (int x = 0; x < s->m.w; x++)
        if (s->t[y][x] == T_DOOR) tile_open(s, x, y), s->door_open[y][x] = 1;
    ev(s, EV_DOORS_OPEN, 0, 0, 0, 0, 0, 0);
  }
  for (int y = 0; y < s->m.h; y++)
    for (int x = 0; x < s->m.w; x++)
      if (s->door_open[y][x] > 0 && s->door_open[y][x] < 255) s->door_open[y][x]++;
  { CW_PROFILE_SCOPE("sim.players"); for (int i = 0; i < MAXP; i++) player_step(s, i, &in[i]); }
  if (s->state != ST_PLAY) { hash_state(s); return; }
  { float hw, hh; sim_team_box(s, &s->cam_x, &s->cam_y, &hw, &hh); }
  { /* the screen: the camera's rule (view.c), and the monsters on it */
    float mw = (float)s->m.w, mh = (float)s->m.h;
    s->view_x = mw > VIEW_HW ? CW_CLAMP(s->cam_x, -mw + VIEW_HW - 1.0f, mw - VIEW_HW + 1.0f) : s->cam_x;
    s->view_y = mh > VIEW_HH ? CW_CLAMP(s->cam_y, -mh + VIEW_HH - 1.5f, mh - VIEW_HH + 1.5f) : s->cam_y;
    int n = 0;
    for (int i = 0; i < MAXE; i++) n += s->e[i].alive && sim_on_screen(s, s->e[i].x, s->e[i].y, 0);
    s->on_screen = n;
  }
  if (s->flow_dirty) cw_nav_update(s->nav);
  spawners_step(s);
  if (s->floor >= 4 && --s->pilfer_next <= 0 && s->nsp > 0) { /* a pilferer slips out of the farthest spawner */
    s->pilfer_next = 60 * (60 + (int)(rf(s) * 40));
    int far = -1;
    float fd = 0;
    for (int k = 0; k < s->nsp; k++) {
      float dx = s->sp[k].x - s->cam_x, dy = s->sp[k].y - s->cam_y, d = dx * dx + dy * dy;
      if (s->sp[k].alive && d > fd && d < 30 * 30) fd = d, far = k;
    }
    if (far >= 0) spawn_enemy(s, EK_PILFERER, 1, s->sp[far].x + 1.2f, s->sp[far].y);
  }
  { CW_PROFILE_SCOPE("sim.enemies"); enemies_step(s); }
  { CW_PROFILE_SCOPE("sim.bullets"); bullets_step(s); }
  booms_step(s);
  zones_step(s);
  for (int i = 0; i < MAXP; i++) { /* embers burn what they touch */
    const player *p = &s->p[i];
    if (!p->active || p->down || p->charm[CH_EMBERS] <= 0 || s->tick % 6) continue;
    for (int k = 0; k < 2; k++) {
      float a = p->ember_a + (float)k * 3.1416f, x = p->x + cw_cos(a) * 1.8f, y = p->y + cw_sin(a) * 1.8f;
      boom b = { x, y, 0.6f, 1, 0, (uint8_t)i, false };
      each_enemy(s, x, y, 0.6f, boom_hit, &b);
    }
  }
  if (sim_alive_players(s) == 0) {
    bool any = false;
    for (int i = 0; i < MAXP; i++) any |= s->p[i].active;
    if (any) s->state = ST_OVER, s->state_ticks = 0;
  }
  hash_state(s);
}

/* a player joins during play (a pad that pressed to join): fresh health, beside a living friend */
void sim_add_player(sim *s, int i, int cls) {
  if (i < 0 || i >= MAXP || s->p[i].active) return;
  player *p = &s->p[i];
  *p = (player){ .active = true, .hp = 1000, .cls = (uint8_t)((unsigned)cls % CL_KINDS), .face = -1.5708f, .inv = 120 };
  for (int k = 0; k < MAXP; k++)
    if (k != i && s->p[k].active && !s->p[k].down) { p->x = s->p[k].x, p->y = s->p[k].y; break; }
  if (!p->x && !p->y && s->m.nstart) p->x = mz_wx(&s->m, s->m.start[0][0]), p->y = mz_wz(&s->m, s->m.start[0][1]);
  if (i >= s->players) s->players = i + 1;
  ev(s, EV_REJOIN, p->x, p->y, 0, 0, i, 0);
}
