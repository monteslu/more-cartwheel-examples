/* Reachability: which coins, ability pedestals and rooms can a player get to, proved with the game's own movement.
 *
 * A breadth-first search over player states. Every state came from the start by real ticks of sim_player_move (the
 * controller, its gravity, coyote time, air jump, wall jump, dash, swimming and one-way ledges, against the real
 * colliders), holding a choice of buttons for a few ticks at a time, so anything it reaches a person can reach.
 * States are merged on a grid of position and velocity to keep the search finite, so "not reached" means "not found at
 * this resolution": COOP_REACH_FINE=1 halves the grid to confirm one.
 *
 * What it asks is what a person meets. It runs once per stage of the level: the abilities the group has on reaching each
 * pedestal, in level order. A coin must be got in the stage it is in, with only what was found before it: a room seen
 * long before the ability that opens it, however far back a person might walk later, reads as "cannot be reached" to
 * whoever is standing in front of it, and fails here. Margins, so a route is not frame-perfect: decisions are held for
 * at least HOLD ticks, jumps are REACH_JUMP of their real height and speed REACH_RUN of its real value. A coin only counts
 * with a clear line from the body to it: the touch radius reaches through a thin wall, a hand through rock does not.
 *
 * What it does not allow: taking damage. A route through spikes, lava or a pit ends there, and it gets no stomp bounces
 * off enemies (a walker may not be there). So a coin it finds is reachable without either.
 *
 * Only the autopilot builds call it (COOP_REACH=1). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

#define REACH_JUMP 0.92f
#define REACH_RUN 0.95f
#define MAX_STATES 20000000 /* in one stage: about 5 minutes and 2 GB. A level that needs more fails loudly (TRUNCATED) */
#define HOLD 6        /* ticks one choice of buttons is held */
#define DT (1.0f / 60.0f)
#define PLAT_BINS 4

typedef struct rstate {
  cw_pos2 pos;
  cw_vec2 vel, floor_normal, wall_normal, plat_vel;
  float coyote, buffer, wall_coyote, wall_lock, dash_t, dash_cd, drop_t, floor_cos;
  float plat_t;       /* the moving platforms' clock */
  float run;          /* momentum */
  int8_t facing, wall_side, air, floor_plat; /* floor_plat: index of the platform stood on, or -1 */
  uint8_t flags;
} rstate;
enum { F_FLOOR = 1, F_WALL = 2, F_CEIL = 4, F_DASH = 8, F_WET = 16, F_ONEWAY = 32 };

static rstate *Q;
static size_t Qn, Qcap;
static uint64_t *Set;
static size_t Setcap, Setn;
static float QX = 2, QY = 2, QVX = 0.25f, QVY = 0.17f; /* the merge grid in the air; on the ground x is finer */
static bool HasDash;

static uint8_t Seen[WORLD_H][WORLD_W]; /* tiles a body has been in */
static int CoinStage[MAX_COINS]; /* the stage a coin was first got in, -1 not yet */
static bool PickGot[MAX_PICKUPS];
static int Stage;
static bool Changed;

/* no rock on the straight line from the body's middle to a point */
static bool clear_line(const level *L, float x0, float y0, float x1, float y1) {
  for (int i = 1; i < 12; i++) {
    float k = (float)i / 12.0f;
    if (world_solid(L, (int)floorf(x0 + (x1 - x0) * k), (int)floorf(y0 + (y1 - y0) * k))) return false;
  }
  return true;
}

static int16_t SkyTop[WORLD_W];
static float Run; /* the working player's momentum, set around each step */
static void save(rstate *r, const cw_platformer2d *pf, const level *L, float plat_t) {
  const cw_mover2d *b = &pf->body;
  r->pos = b->position, r->vel = b->velocity;
  r->floor_normal = b->floor_normal, r->wall_normal = b->wall_normal, r->plat_vel = b->platform_velocity;
  r->coyote = pf->coyote, r->buffer = pf->buffer, r->wall_coyote = pf->wall_coyote, r->wall_lock = pf->wall_lock;
  r->dash_t = pf->dash_t, r->dash_cd = pf->dash_cd, r->drop_t = pf->drop_t, r->floor_cos = b->floor_cos;
  r->plat_t = plat_t;
  r->run = Run;
  r->facing = (int8_t)pf->facing, r->wall_side = (int8_t)pf->wall_side, r->air = (int8_t)pf->air_jumps_left;
  r->floor_plat = -1;
  for (int i = 0; i < L->nplat; i++)
    if (b->floor_body.id && memcmp(&b->floor_body, &L->plats[i].body, sizeof b->floor_body) == 0) r->floor_plat = (int8_t)i;
  r->flags = (b->on_floor ? F_FLOOR : 0) | (b->on_wall ? F_WALL : 0) | (b->on_ceiling ? F_CEIL : 0) | (pf->dash_ready ? F_DASH : 0) |
             (pf->was_wet ? F_WET : 0) | (b->on_one_way ? F_ONEWAY : 0);
}
static void load(const rstate *r, cw_platformer2d *pf, const level *L) {
  cw_mover2d *b = &pf->body;
  b->position = b->prev_position = r->pos, b->velocity = r->vel;
  b->floor_normal = r->floor_normal, b->wall_normal = r->wall_normal, b->platform_velocity = r->plat_vel;
  pf->coyote = r->coyote, pf->buffer = r->buffer, pf->wall_coyote = r->wall_coyote, pf->wall_lock = r->wall_lock;
  pf->dash_t = r->dash_t, pf->dash_cd = r->dash_cd, pf->drop_t = r->drop_t, b->floor_cos = r->floor_cos;
  pf->facing = r->facing, pf->wall_side = r->wall_side, pf->air_jumps_left = r->air;
  b->floor_body = r->floor_plat >= 0 ? L->plats[r->floor_plat].body : (cw_body2d){ 0 };
  b->on_floor = r->flags & F_FLOOR, b->on_wall = r->flags & F_WALL, b->on_ceiling = r->flags & F_CEIL;
  pf->dash_ready = r->flags & F_DASH, pf->was_wet = r->flags & F_WET, b->on_one_way = r->flags & F_ONEWAY;
  b->drop_through = false;
  pf->events = 0;
}

static float plat_near(const level *L, float x) {
  float best = 1e9f;
  for (int i = 0; i < L->nplat; i++) {
    const mplat *m = &L->plats[i];
    float lo = fminf(m->x0, m->x1) - 3, hi = fmaxf(m->x0, m->x1) + 3;
    if (x >= lo && x <= hi) best = 0;
  }
  return best;
}

static uint64_t key_of(const rstate *r, const level *L) {
  bool floor_ = r->flags & F_FLOOR;
  int qx = (int)floorf((float)r->pos.x * (floor_ ? QX * 2 : QX)), qy = (int)floorf((float)r->pos.y * QY);
  int vx = (int)floorf(r->vel.x * QVX + 0.5f) + 32, vy = (int)floorf(r->vel.y * QVY + 0.5f) + 32;
  int rn = (r->flags & F_FLOOR) ? (int)(r->run * 3.0f + 0.5f) : 0; /* in the air the speed it left the ground with is in the velocity */
  int ph = 0;
  if (L->nplat && plat_near(L, (float)r->pos.x) == 0) {
    float t = r->plat_t - floorf(r->plat_t);
    ph = (int)(t * PLAT_BINS);
  }
  bool floor = r->flags & F_FLOOR;
  bool dash = HasDash && r->dash_t > 0, lock = false, coy = false, wc = r->wall_coyote > 0;
  uint8_t fl = r->flags & (F_FLOOR | F_WALL | F_WET | (HasDash ? F_DASH : 0) | F_ONEWAY);
  int facing = HasDash && r->dash_t > 0 && r->facing > 0;
  if (r->flags & F_WET) qx = (int)floorf((float)r->pos.x), qy = (int)floorf((float)r->pos.y), vx = (int)floorf(r->vel.x * 0.2f + 0.5f) + 32, vy = (int)floorf(r->vel.y * 0.2f + 0.5f) + 32;
  uint64_t k = (uint64_t)qx | (uint64_t)qy << 13 | (uint64_t)vx << 25 | (uint64_t)vy << 32 | (uint64_t)ph << 39 |
               (uint64_t)(r->air & 3) << 43 | (uint64_t)fl << 45 | (uint64_t)dash << 51 | (uint64_t)lock << 52 |
               (uint64_t)coy << 53 | (uint64_t)wc << 54 | (uint64_t)facing << 55 | (uint64_t)(r->wall_side + 1) << 56 | (uint64_t)rn << 58;
  return k + 1;
}

static void set_clear(void) {
  if (!Set) Setcap = 1u << 25, Set = malloc(Setcap * sizeof *Set);
  memset(Set, 0, Setcap * sizeof *Set);
  Setn = 0;
}
static bool set_add(uint64_t k) {
  size_t h = (size_t)(k * 0x9E3779B97F4A7C15ull) & (Setcap - 1);
  while (Set[h]) {
    if (Set[h] == k) return false;
    h = (h + 1) & (Setcap - 1);
  }
  Set[h] = k, Setn++;
  return true;
}
static void push(const rstate *r, const level *L) {
  if (Setn >= MAX_STATES) return; /* the bound of the search: reported at the end as a failure, never as a pass */
  if (!set_add(key_of(r, L))) return;
  if (Qn == Qcap) {
    Qcap = Qcap ? Qcap * 2 : 1u << 20;
    Q = realloc(Q, Qcap * sizeof *Q);
    if (!Q) { fprintf(stderr, "reach: out of memory\n"); exit(3); }
  }
  Q[Qn++] = *r;
}

static void place_plats(const level *L, float t, float dt) {
  (void)dt;
  for (int i = 0; i < L->nplat; i++) {
    const mplat *m = &L->plats[i];
    float k = m->speed / 0.35f * t; /* each platform's own phase, on one clock */
    float ph = k - floorf(k), tri = ph < 0.5f ? ph * 2 : 2 - ph * 2;
    float x = m->x0 + (m->x1 - m->x0) * tri, y = m->y0 + (m->y1 - m->y0) * tri;
    cw_body2d_set_transform(m->body, (cw_xform2){ { x, y }, { 1, 0 } });
    ph += 0.35f * 0.3f * DT;
    tri = ph < 0.5f ? ph * 2 : (ph < 1 ? 2 - ph * 2 : (ph - 1) * 2);
    cw_body2d_set_target(m->body, (cw_xform2){ { m->x0 + (m->x1 - m->x0) * tri, m->y0 + (m->y1 - m->y0) * tri }, { 1, 0 } }, DT);
  }
}

/* tick by tick, a choice of buttons from a state; everything the game would notice on the way is noticed here */
static void expand(game *g, player *p, const rstate *from, input_state a, int ticks) {
  level *L = &g->L;
  float t = from->plat_t;
  load(from, &p->pf, L);
  p->run = from->run;
  for (int i = 0; i < ticks; i++) {
    input_state in = a;
    in.jump_pressed = a.jump_pressed && i == 0;
    in.dash_pressed = a.dash_pressed && i == 0;
    place_plats(L, t, DT);
    t += 0.35f * 0.3f * DT;
    sim_player_move(g, p, &in, DT);
    float fx = (float)p->pf.body.position.x, fy = (float)p->pf.body.position.y;
    if (sim_hazard_at(L, fx, fy)) return; /* a route that takes damage ends here */
    /* the sky over a level holds nothing: with claws a wall climbs to the top of the world, and the search would fill it.
     * Above the highest thing near here it stops; anything up there would be reported as not reached, never passed */
    if ((int)fx >= 0 && (int)fx < WORLD_W && fy > (float)SkyTop[(int)fx]) return;
    if (sim_dash_cracks(g, p, false)) Changed = true;
    for (int h = 0; h < 3; h++) {
      int cx = (int)floorf(fx), cy = (int)floorf(fy + 0.1f + (float)h * 0.65f);
      if (cx >= 0 && cx < WORLD_W && cy >= 0 && cy < WORLD_H) Seen[cy][cx] = 1;
    }
    for (int k = 0; k < L->ncoins; k++)
      if (CoinStage[k] < 0 && sim_coin_touch(&L->coins[k], fx, fy) && clear_line(L, fx, fy + 0.7f, L->coins[k].x, L->coins[k].y)) CoinStage[k] = Stage;
    for (int k = 0; k < L->npickups; k++)
      if (!PickGot[k] && sim_pickup_touch(&L->pickups[k], fx, fy)) PickGot[k] = true;
    for (int k = 0; k < L->nchecks; k++) {
      float dx = fx - ((float)L->checks[k].x + 0.5f), dy = fy - (float)L->checks[k].y;
      if (dx > -1.6f && dx < 1.6f && dy > -1.0f && dy < 7.0f ) g->reached |= 1u << k;
    }
  }
  rstate r;
  Run = p->run;
  save(&r, &p->pf, L, t);
  push(&r, L);
}

static void successors(game *g, player *p, const rstate *r) {
  bool floor = r->flags & F_FLOOR;
  bool wet = false;
  {
    int cx = (int)floorf((float)r->pos.x), cy = (int)floorf((float)r->pos.y + 0.7f);
    if (cx >= 0 && cx < WORLD_W && cy >= 0 && cy < WORLD_H) wet = g->L.t[cy][cx] == T_WATER;
  }
  static const float MX[3] = { -1, 0, 1 };
  int HOLD_ = floor || (r->flags & F_WALL) ? HOLD : HOLD + 4; /* in the air a choice lasts a little longer */
  /* holding jump only differs from letting go while rising or on a wall; a press only does something where a jump can start */
  bool rising = r->vel.y > 0.1f, wall = r->flags & F_WALL || r->wall_coyote > 0;
  bool can_press = floor || wet || r->coyote > 0 || r->air > 0 || wall;
  for (int m = 0; m < 3; m++) {
    input_state a = { .x = MX[m], .run = true }; /* a person runs; a jump from standing still is a standing jump */
    expand(g, p, r, a, HOLD_); /* run, or stand */
    a.jump_held = true;
    if (rising || wall) expand(g, p, r, a, HOLD_); /* keep holding jump */
    a.jump_pressed = true;
    if (can_press) expand(g, p, r, a, HOLD_); /* press jump */
    if (wet) {
      input_state s = { .x = MX[m], .y = 1, .run = true };
      expand(g, p, r, s, HOLD_);
      s.y = -1;
      s.down = true;
      expand(g, p, r, s, HOLD_);
    }
    /* a dash where a dash does something: at a cracked wall, or falling over a pit. Elsewhere it only multiplies the search */
    bool dash_use = false;
    if ((g->abilities & AB_DASH) && (r->flags & F_DASH) && r->dash_cd <= 0 && r->dash_t <= 0 && m != 1 && !rising) {
      int cx = (int)floorf((float)r->pos.x), cy = (int)floorf((float)r->pos.y);
      for (int dx = -3; dx <= 3; dx++)
        for (int dy = 0; dy < 3; dy++)
          if (cx + dx >= 0 && cx + dx < WORLD_W && cy + dy >= 0 && cy + dy < WORLD_H && g->L.t[cy + dy][cx + dx] == T_CRACK) dash_use = true;
      if (!floor) {
        bool under = false;
        for (int dy = 1; dy <= 6; dy++) if (world_solid(&g->L, cx, cy - dy)) under = true;
        if (!under) dash_use = true;
      }
    }
    if (dash_use) {
      input_state d = { .x = MX[m], .dash_pressed = true, .run = true };
      expand(g, p, r, d, HOLD_);
    }
  }
  if (floor && (r->flags & F_ONEWAY)) {
    input_state d = { .down = true, .y = -1, .jump_pressed = true, .jump_held = true };
    expand(g, p, r, d, HOLD);
  }
}

/* rooms: connected air, water and ledge tiles. One nobody can enter is listed if it holds a coin or is big enough to read
 * as a room (the sky over the level and the rock below it are not rooms: the sky is entered; rock is not air) */
static int rooms_scan(const level *L, bool print);
static int rooms_report(const level *L) { return rooms_scan(L, true); }
static int rooms_unseen(const level *L) { return rooms_scan(L, false); }
static int rooms_scan(const level *L, bool print) {
  static int16_t comp[WORLD_H][WORLD_W];
  memset(comp, 0, sizeof comp);
  int n = 0, bad = 0;
  static int stack[WORLD_W * WORLD_H];
  for (int y = 0; y < WORLD_H; y++)
    for (int x = 0; x < WORLD_W; x++) {
      if (comp[y][x] || world_solid(L, x, y) || L->t[y][x] == T_CRACK) continue;
      n++;
      int top = 0, size = 0, seen = 0, coins = 0, hazard = 0, x0 = x, y0 = y;
      stack[top++] = y * WORLD_W + x;
      comp[y][x] = (int16_t)n;
      while (top) {
        int v = stack[--top], vy = v / WORLD_W, vx = v % WORLD_W;
        size++;
        if (Seen[vy][vx]) seen++;
        if (L->t[vy][vx] == T_SPIKE || L->t[vy][vx] == T_LAVA) hazard++; /* the pit under a bridge is not a room */
        for (int k = 0; k < L->ncoins; k++)
          if ((int)floorf(L->coins[k].x) == vx && (int)floorf(L->coins[k].y) == vy) coins++;
        static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int d = 0; d < 4; d++) {
          int nx = vx + D[d][0], ny = vy + D[d][1];
          if (nx < 0 || nx >= WORLD_W || ny < 0 || ny >= WORLD_H || comp[ny][nx]) continue;
          if (world_solid(L, nx, ny) || L->t[ny][nx] == T_CRACK) continue;
          comp[ny][nx] = (int16_t)n;
          stack[top++] = ny * WORLD_W + nx;
        }
      }
      if (seen == 0 && !hazard && (coins > 0 || size >= 6)) {
        if (print) printf("reach: ROOM never entered at %d,%d: %d tiles, %d coins\n", x0, y0, size, coins);
        bad++;
      }
    }
  return bad;
}

static uint32_t abilities_later(const level *L, float x) {
  uint32_t ab = LEVELS[G.level].abilities;
  for (int k = 0; k < L->npickups; k++)
    if ((float)L->pickups[k].x <= x) ab |= (uint32_t)L->pickups[k].ability;
  return ab;
}

static int rooms_unseen(const level *L);
/* nothing left to find in this stage: stops a search that has done its job. Anything found stays found, so this loses
 * nothing: a search only runs to the end when something is really missing, and then it reports it */
static bool all_done(const level *L, const game *g, uint32_t ab) {
  for (int k = 0; k < L->ncoins; k++)
    if (CoinStage[k] < 0 && !(abilities_later(L, L->coins[k].x) & ~ab)) return false;
  for (int k = 0; k < L->npickups; k++) if (!PickGot[k] && !(abilities_later(L, (float)L->pickups[k].x) & ~ab)) return false;
  for (int k = 0; k < L->nchecks; k++) if (!(g->reached & (1u << k)) && !(abilities_later(L, (float)L->checks[k].x) & ~ab)) return false;
  for (int y = 0; y < WORLD_H; y++) for (int x = 0; x < WORLD_W; x++) if (L->t[y][x] == T_CRACK && (ab & AB_DASH)) return false;
  if (abilities_later(L, (float)L->goal_x) & ~ab) return true; /* the goal is for a later stage */
  bool goal = false;
  for (int y = 0; y < WORLD_H; y++) if (L->goal_x >= 0 && L->goal_x < WORLD_W && Seen[y][L->goal_x]) goal = true;
  return goal && rooms_unseen(L) == 0;
}

/* the abilities a coin at this column should be got with: every pedestal before it in the level */
static uint32_t abilities_at(const level *L, float x) { return abilities_later(L, x); }

int reach_check(void) {
  game *g = &G;
  G.abilities = 0;
  g_level_old = getenv("COOP_REACH_OLD") != NULL;
  G.level = getenv("COOP_LEVEL") ? atoi(getenv("COOP_LEVEL")) : 0;
  G.abilities = LEVELS[G.level].abilities;
  G.reach = true;
  sim_reset(g, 1);
  G.reach = true;
  level *L = &g->L;
  if (getenv("COOP_REACH_FINE")) QX = QY = 4, QVX = 0.5f, QVY = 0.33f;
  player *p = &g->p[0];
  g->margin_jump = REACH_JUMP, g->margin_run = REACH_RUN;
  for (int k = 0; k < L->ncoins; k++) CoinStage[k] = -1;
  for (int x = 0; x < WORLD_W; x++) {
    int top = 0;
    for (int k = x - 10; k <= x + 10; k++) {
      if (k < 0 || k >= WORLD_W) continue;
      if (L->ground[k] < WORLD_H && L->ground[k] > top) top = L->ground[k];
    }
    for (int k = 0; k < L->ncoins; k++) if (fabsf(L->coins[k].x - (float)x) < 10 && (int)L->coins[k].y > top) top = (int)L->coins[k].y;
    SkyTop[x] = (int16_t)(top + 13);
  }
  rstate r0;
  save(&r0, &p->pf, L, 0);
  r0.flags = F_DASH;
  uint32_t stages[MAX_PICKUPS + 1];
  int nstages = 1;
  stages[0] = LEVELS[G.level].abilities;
  /* in level order */
  for (int done = 0; done < L->npickups; done++) {
    int best = -1;
    for (int k = 0; k < L->npickups; k++)
      if ((L->pickups[k].ability & (int)stages[nstages - 1]) == 0 && (best < 0 || L->pickups[k].x < L->pickups[best].x)) best = k;
    if (best < 0) break;
    stages[nstages] = stages[nstages - 1] | (uint32_t)L->pickups[best].ability;
    nstages++;
  }
  size_t total = 0;
  bool truncated = false;
  for (Stage = 0; Stage < nstages; Stage++) {
    g->abilities = stages[Stage];
    HasDash = g->abilities & AB_DASH;
    /* with claws a body can be almost anywhere in the air at almost any speed: merge states harder, or the search spends
     * minutes on the same spot. Coarser can only miss a route (a failure, loudly), never invent one */
    if ((g->abilities & AB_CLAWS) && !getenv("COOP_REACH_FINE")) QX = QY = 1.5f, QVX = 0.2f, QVY = 0.125f;
    /* cracks break for good when a dash reaches them, and open more to the same stage */
    do {
      Changed = false;
      set_clear();
      Qn = 0;
      push(&r0, L);
      for (int k = 0; k < L->nchecks; k++)
        if (g->reached & (1u << k)) { /* flags touched: the map warps there */
          cw_platformer2d_teleport(&p->pf, (cw_pos2){ (float)L->checks[k].x + 0.5f, (float)L->checks[k].y });
          rstate w;
          save(&w, &p->pf, L, 0);
          w.flags = F_DASH;
          push(&w, L);
        }
      for (size_t i = 0; i < Qn; i++) {
        rstate r = Q[i]; /* successors may move the queue */
        if (getenv("COOP_REACH_V") && (i % 200000) == 0) printf("reach: .. %zu of %zu x %.0f\n", i, Qn, r.pos.x), fflush(stdout);
        successors(g, p, &r);
        if ((i % 65536) == 0 && i && all_done(L, g, g->abilities)) { Changed = false; g->reach_changed = false; break; }
      }
      total += Qn;
      if (Setn >= MAX_STATES) truncated = true;
      int got = 0;
      for (int k = 0; k < L->ncoins; k++) got += CoinStage[k] >= 0;
      printf("reach: stage %d abilities %u: %zu states, %d of %d coins, flags %x\n", Stage, g->abilities, Qn, got, L->ncoins, g->reached);
      fflush(stdout);
      if (g->reach_changed) Changed = true, g->reach_changed = false;
    } while (Changed);
  }
  /* the list */
  int bad = 0, rock = 0, late = 0;
  for (int k = 0; k < L->ncoins; k++) {
    const coin *c = &L->coins[k];
    int ix = (int)floorf(c->x), iy = (int)floorf(c->y);
    if (world_solid(L, ix, iy)) rock++, printf("reach: COIN IN ROCK %d at %.1f,%.1f%s\n", k, (double)c->x, (double)c->y, c->secret ? " (secret)" : "");
    if (CoinStage[k] < 0) {
      bad++;
      printf("reach: COIN UNREACHED %d at %.1f,%.1f%s\n", k, (double)c->x, (double)c->y, c->secret ? " (secret)" : "");
    } else if (stages[CoinStage[k]] & ~abilities_at(L, c->x)) {
      late++;
      printf("reach: COIN NEEDS A LATER ABILITY %d at %.1f,%.1f%s: abilities %u, found by then %u\n", k, (double)c->x, (double)c->y, c->secret ? " (secret)" : "", stages[CoinStage[k]], abilities_at(L, c->x));
    }
  }
  for (int k = 0; k < L->npickups; k++)
    if (!PickGot[k]) bad++, printf("reach: PEDESTAL UNREACHED %d at %d,%d ability %d\n", k, L->pickups[k].x, L->pickups[k].y, L->pickups[k].ability);
  int flags_missed = 0;
  for (int k = 0; k < L->nchecks; k++)
    if (!(g->reached & (1u << k))) flags_missed++, printf("reach: FLAG UNREACHED %d at %d,%d\n", k, L->checks[k].x, L->checks[k].y);
  bool goal = false;
  for (int y = 0; y < WORLD_H; y++)
    if (L->goal_x >= 0 && L->goal_x < WORLD_W && Seen[y][L->goal_x]) goal = true;
  if (!goal) printf("reach: GOAL UNREACHED at column %d\n", L->goal_x);
  int cracks = 0;
  for (int y = 0; y < WORLD_H; y++)
    for (int x = 0; x < WORLD_W; x++)
      if (L->t[y][x] == T_CRACK && (g->abilities & AB_DASH)) cracks++, printf("reach: CRACK UNBROKEN %d,%d\n", x, y); /* with no dash in this level a crack is a promise */
  int rooms = rooms_report(L);
  int things_n = 0, things_bad = world_things_check(L, &things_n);
  int fails = bad + rock + late + flags_missed + (goal ? 0 : 1) + cracks + rooms + things_bad;
  printf("reach: level %d \"%s\" %d columns: ", G.level, LEVELS[G.level].name, L->width);
  printf("%d coins, %d not reached, %d need a later ability, %d in rock, %d rooms never entered, %d flags missed, goal %s, %d stages, %zu states%s\n",
         L->ncoins, bad, late, rock, rooms, flags_missed, goal ? "reached" : "NOT reached", nstages, total, truncated ? " TRUNCATED" : "");
  printf("reach: %s\n", fails == 0 && !truncated ? "ok" : "FAIL");
  fflush(stdout);
  return fails == 0 && !truncated ? 0 : 1;
}
