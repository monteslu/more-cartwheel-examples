/* The rules. One fixed step: players (platformer controller), moving platforms, hazards, water, enemies, pickups,
 * coins, cracked walls, checkpoints, the shared camera and the bubble rescue. Float + - * / only. */
#include <math.h>
#include <string.h>

#include "game.h"

game G;
bool g_bot_no_run; /* COOP_BOT_NO_RUN: the levels gate's control: bots that never hold Run */

enum { EV_JUMP = 1, EV_LAND = 2, EV_COIN = 4, EV_HURT = 8, EV_SPLASH = 16, EV_DASH = 32, EV_POWER = 64, EV_STOMP = 128,
       EV_BREAK = 256, EV_BUBBLE = 512, EV_POP = 1024, EV_DIE = 2048, EV_WIN = 4096, EV_CHECK = 8192, EV_WALL = 16384 };

static int tile_at(const level *L, float x, float y) {
  int ix = (int)floorf(x), iy = (int)floorf(y);
  if (ix < 0 || ix >= WORLD_W || iy < 0 || iy >= WORLD_H) return T_ROCK;
  return L->t[iy][ix];
}

static cw_platformer2d_def player_def(void) {
  cw_platformer2d_def d = cw_platformer2d_default_def();
  d.mover = cw_mover2d_default_def();
  d.mover.radius = 0.32f;
  d.mover.height = 1.5f;
  d.mover.step_height = 0.05f;
  d.mover.one_way_mask = 1u << LAYER_ONEWAY;
  d.mover.mask = (1u << LAYER_WORLD) | (1u << LAYER_ONEWAY);
  d.air_jumps = 1;
  d.wall_jump = true;
  d.dash_speed = 17;
  d.run_speed = WALK_SPEED;
  d.jump_height = JUMP_STAND;
  d.wall_slide_speed = 2.6f;
  d.wall_jump_speed_x = 8.5f;
  d.wall_jump_speed_y = 13.5f;
  d.wall_jump_lock = 0.2f;
  return d;
}

static void place_player(game *g, int i, float x, float y) {
  player *p = &g->p[i];
  cw_platformer2d_def d = player_def();
  cw_platformer2d_init(&p->pf, g->world, &d, (cw_pos2){ x, y });
  p->pf.can_dash = (g->abilities & AB_DASH) != 0;
  p->pf.can_wall_jump = (g->abilities & AB_CLAWS) != 0;
  p->pf.max_air_jumps = (g->abilities & AB_DOUBLE) ? 1 : 0;
  p->alive = true;
  p->bubble = 0;
  p->run = 0, p->launched = 0, p->launch_vx = 0, p->pancake = 0, p->in_jump = false;
  if (p->carrying) g->p[p->carrying - 1].carried_by = 0;
  if (p->carried_by) g->p[p->carried_by - 1].carrying = 0;
  p->carrying = p->carried_by = 0;
  p->hp = 3;
  p->invuln = 90;
}

void sim_reset(game *g, int players) {
  uint32_t keep_ab = g->abilities;
  world_destroy_physics(g);
  world_generate(&g->L, g->level);
  memset(g->p, 0, sizeof g->p);
  g->nplayers = players;
  g->tick = 0;
  g->margin_jump = g->margin_run = 1;
  memset(g->last_jump, 0, sizeof g->last_jump);
  g->top_speed = 0;
  g->abilities = keep_ab;
  g->coins_total = g->kills = g->deaths = g->rescues = g->secrets = 0;
  g->medals = 0;
  g->won = 0;
  g->win_tick = 0;
  g->checkpoint = -1;
  g->reached = 0;
  g->hitstop = g->hit_flash = g->hitstops = 0;
  g->events = 0;
  g->boss_hp = 0;
  for (int k = 0; k < g->L.nenemies; k++) if (g->L.enemies[k].kind == 2) g->L.enemies[k].hp = 5 + g->L.boss_kind, g->boss_hp = g->L.enemies[k].hp;
  memset(&g->bz, 0, sizeof g->bz), memset(g->shots, 0, sizeof g->shots), memset(g->boss_stats, 0, sizeof g->boss_stats);
  g->bz.cd = 50; /* its first attack a moment after it wakes */
  memset(g->betrayals, 0, sizeof g->betrayals);
  g->things_hit = 0;
  g->fake_t = 0, g->mimic_bites = 0;
  memset(g->throws, 0, sizeof g->throws);
  memset(g->stomps, 0, sizeof g->stomps);
  memset(g->falls, 0, sizeof g->falls);
  g->hash = 2166136261u;
  memset(g->seen, 0, sizeof g->seen);
  world_build_physics(g);
  for (int i = 0; i < players; i++) {
    g->p[i].joined = true;
    g->p[i].color = i;
    g->p[i].lead_w = 1; /* everyone plays until their pad has sat untouched for IDLE_TICKS */
    place_player(g, i, (float)g->L.start_x + 0.8f * (float)i, (float)g->L.start_y);
    g->p[i].pf.body.velocity = (cw_vec2){ 0, 0 };
  }
  g->cam = (camera_rig){ 0 };
  g->cam.x = g->cam.tx = g->cam.px = (float)g->L.start_x;
  g->cam.y = g->cam.ty = g->cam.py = (float)g->L.start_y + 3;
  g->cam.z = g->cam.tz = g->cam.pz = 20;
}

void sim_respawn_player(game *g, int i) {
  const level *L = &g->L;
  float x = (float)L->start_x, y = (float)L->start_y;
  if (g->checkpoint >= 0) x = (float)L->checks[g->checkpoint].x, y = (float)L->checks[g->checkpoint].y;
  place_player(g, i, x + 0.6f * (float)i, y);
  g->p[i].pf.body.velocity = (cw_vec2){ 0, 0 };
}

static float pos_x(const player *p) { return (float)p->pf.body.position.x; }
static float pos_y(const player *p) { return (float)p->pf.body.position.y; }

static void note_death(game *g, int i, int cause);
static void hurt_player(game *g, int i, float kx, int dmg, int cause) {
  player *p = &g->p[i];
  if (p->invuln > 0 || p->bubble || !p->alive) return;
  p->hp -= dmg;
  p->invuln = 100;
  p->hurt_fx = 1;
  g->hitstop = 5;
  g->hitstops++;
  g->impacts++;
  g->hit_flash = 8;
  sim_rumble(g, i, 0.2f, p->hp <= 0 ? 0.9f : 0.65f, p->hp <= 0 ? 200 : 130); /* hurt: short and sharp */
  g->events |= EV_HURT;
  p->pf.body.velocity.x = kx * 7.0f;
  p->pf.body.velocity.y = 9.0f;
  p->pf.dash_t = 0;
  if (p->hp <= 0) {
    p->alive = false;
    g->deaths++;
    g->events |= EV_DIE;
    note_death(g, i, cause);
  }
}

static void note_death(game *g, int i, int cause) {
  player *p = &g->p[i];
  g->falls[i]++;
  p->die_cause = cause;
  if (g->ndeaths_q < 8) g->deaths_q[g->ndeaths_q++] = (typeof(g->deaths_q[0])){ (float)p->pf.body.position.x, (float)p->pf.body.position.y, i, cause };
}
static void kill_player(game *g, int i, int cause) {
  player *p = &g->p[i];
  if (!p->alive || p->bubble) return;
  p->hp = 0;
  p->alive = false;
  g->deaths++;
  g->events |= EV_DIE;
  note_death(g, i, cause);
}

/* a seat nobody plays (IDLE_TICKS without a touch of its pad) holds nobody back: the players the group follows are the
 * ones who play. When nobody plays everyone counts, as before; the control switch (idle_anchor) makes everyone count. */
static bool someone_plays(const game *g) {
  for (int i = 0; i < g->nplayers; i++)
    if (g->p[i].joined && g->p[i].idle < IDLE_TICKS) return true;
  return false;
}
static bool plays(const game *g, int i) { return g->p[i].idle < IDLE_TICKS || g->idle_anchor || !someone_plays(g); }
bool sim_player_leads(const game *g, int i) {
  const player *p = &g->p[i];
  return p->joined && p->alive && !p->bubble && plays(g, i);
}
/* the others who can carry a fallen player's bubble: up, and playing */
static int others_leading(const game *g, int self) {
  int n = 0;
  for (int i = 0; i < g->nplayers; i++)
    if (i != self && sim_player_leads(g, i)) n++;
  return n;
}

/* the four kinds each world adds (game.h EK_*): one idea each */
static bool e_floor(const level *L, float x, float y) { return world_solid(L, (int)floorf(x), (int)floorf(y)); }
static void e_fall(game *g, enemy *e, float dt) { /* gravity onto the ground (the walker's rule) */
  const level *L = &g->L;
  e->vy -= 30.0f * dt;
  float ny = e->y + e->vy * dt;
  if (e->vy <= 0 && e_floor(L, e->x, ny - 0.01f)) ny = floorf(ny - 0.01f) + 1.0f, e->vy = 0;
  e->y = ny;
  if (e->y < -2.0f) e->alive = false; /* down a pit */
}
static void e_walk(game *g, enemy *e, float sp, float dt, bool turn_at_edge) {
  const level *L = &g->L;
  float half = 0.45f, nx = e->x + (float)e->dir * sp * dt;
  bool wall = e_floor(L, nx + (float)e->dir * half, e->y + 0.3f) || e_floor(L, nx + (float)e->dir * half, e->y + 0.8f);
  bool edge = turn_at_edge && e->vy == 0 && !e_floor(L, nx + (float)e->dir * half, e->y - 0.2f);
  if (wall || edge || nx > e->home_x + e->range || nx < e->home_x - e->range) e->dir = -e->dir, nx = e->x;
  e->x = nx;
}
static void new_enemy_step(game *g, enemy *e, float dt) {
  const level *L = &g->L;
  e->t += 1;
  switch (e->kind) {
    case EK_HOPPER: /* walks, and hops on a beat of its own: a stomp has to be timed */
      e_walk(g, e, 2.0f, dt, true);
      if (e->vy == 0 && e_floor(L, e->x, e->y - 0.05f) && ((int)e->t + (int)(e->home_x * 7)) % 80 == 0) e->vy = 10.5f;
      e_fall(g, e, dt);
      break;
    case EK_DROPPER:
      if (e->state == 0) { /* hangs from the roof; anyone who walks under it gets it on the head */
        e->y = e->hang_y;
        for (int i = 0; i < g->nplayers; i++) {
          const player *p = &g->p[i];
          if (p->joined && p->alive && !p->bubble && fabsf(pos_x(p) - e->x) < 1.1f && pos_y(p) < e->y) { e->state = 1, e->t = 0, e->vy = 0; break; }
        }
      } else if (e->state == 1) { /* falls */
        e_fall(g, e, dt);
        if (e->vy == 0) e->state = 2, e->t = 0;
      } else if (e->state == 2) { /* dazed on the floor: the moment to stomp it, then it wanders a little */
        if (e->t > 45) e_walk(g, e, 1.2f, dt, true);
        e_fall(g, e, dt);
        if (e->t > 220) e->state = 3, e->t = 0;
      } else { /* climbs back up its thread */
        e->x += (e->home_x - e->x) * fminf(1.0f, 3.0f * dt);
        e->y += 2.4f * dt;
        if (e->y >= e->hang_y) e->y = e->hang_y, e->state = 0, e->t = 0;
      }
      break;
    case EK_SHELL:
      if (e->state == 0) { e_walk(g, e, 1.6f, dt, true); e_fall(g, e, dt); }
      else if (e->state == 1) { e_fall(g, e, dt); if (e->t > 600) e->state = 0, e->t = 0; } /* it comes out again */
      else { /* kicked: slides, bounces off walls, takes out whatever it meets, friends included */
        float sp = 10.0f, nx = e->x + (float)e->dir * sp * dt;
        if (e_floor(L, nx + (float)e->dir * 0.45f, e->y + 0.3f)) {
          e->dir = -e->dir, nx = e->x;
          if (++e->bounces >= 4) { e->alive = false; g->kills++; sim_rumble_at(g, e->x, e->y + 0.4f, 0.3f); break; }
        }
        e->x = nx;
        e_fall(g, e, dt);
        for (int k = 0; k < L->nenemies; k++) {
          enemy *o = &g->L.enemies[k];
          if (o == e || !o->alive || o->kind == EK_BOSS) continue;
          if (fabsf(o->x - e->x) < 0.8f && fabsf(o->y - e->y) < 0.9f) o->alive = false, g->kills++, sim_rumble_at(g, o->x, o->y + 0.5f, 0.3f);
        }
      }
      break;
    case EK_SLIDER: /* skates its stretch fast, a spiked front first */
      e_walk(g, e, 4.4f, dt, true);
      e_fall(g, e, dt);
      break;
  }
}

/* ── the bosses: five fights, not one ─────────────────────────────────────────────────────────────────────────────────
 * Each boss chases the nearest player and, every few seconds, stops to show what it is about to do (the tell: the stop is
 * also the moment to jump on its head, and a stomp in the tell calls the attack off), then does it:
 *   LEAP   it crouches, leaps at you and lands with a wave along the floor both ways (jump the wave)
 *   RAIN   it raises its arms; things fall from above on spots shaded on the floor first (step out of the shade)
 *   CHARGE it paws the floor, then runs flat out until a wall stops it, and stands dazed there (jump it, then stomp it)
 *   ROLL   it sets something rolling along the floor at you; it comes back once off the far wall (jump it)
 *   THROW  it winds up and lobs something at where you stand; its shadow grows where it will land (move)
 * Each boss has its own list, and a longer one from half its health down (the second phase, quicker too). */
bool g_boss_old;
int g_boss_tell;
static const int8_t BOSS_MENU[5][2][3] = {
  { { BA_LEAP }, { BA_LEAP, BA_THROW } },                       /* the Landlord: comes down on you; then lobs final notices too */
  { { BA_RAIN }, { BA_RAIN, BA_LEAP } },                        /* the Tax Golem: forms rain from the roof; then he stamps as well */
  { { BA_CHARGE }, { BA_CHARGE, BA_ROLL } },                    /* the Lifeguard: charges into the wall; then rolls rings at you */
  { { BA_ROLL }, { BA_ROLL, BA_THROW } },                       /* the Regional Manager: rolls water cooler bottles; then mugs */
  { { BA_RAIN, BA_CHARGE }, { BA_RAIN, BA_CHARGE, BA_LEAP } },  /* the Founder: lava rocks and charges; then all of it */
};
static const int BOSS_TELL[BA_KINDS] = { 0, 40, 45, 38, 32, 28 };
static int boss_tell(int a) { return g_boss_tell > 0 ? g_boss_tell : BOSS_TELL[a]; }
static int fall_warn(void) { return g_boss_tell > 0 ? g_boss_tell : 54; }
int sim_fall_warn(void) { return fall_warn(); }
bool sim_boss_uses(int kind, int atk) {
  for (int ph = 0; ph < 2; ph++) for (int m = 0; m < 3; m++) if (BOSS_MENU[kind % 5][ph][m] == atk) return true;
  return false;
}
static bshot *shot_new(game *g, int kind, float x, float y, float vx, float vy, float floor_y, int t0) {
  for (int i = 0; i < MAX_BSHOTS; i++)
    if (!g->shots[i].alive) {
      g->shots[i] = (bshot){ .alive = true, .kind = kind, .t = t0, .x = x, .y = y, .vx = vx, .vy = vy, .floor = floor_y, .land_t = -1 };
      return &g->shots[i];
    }
  return NULL;
}
static void boss_end(game *g, int k) {
  boss_ai *b = &g->bz;
  b->atk = BA_NONE;
  b->cd = b->phase ? 112 - 6 * k : 168 - 8 * k;
}
static bool boss_wall(const level *L, float x, float y) {
  return world_solid(L, (int)floorf(x), (int)floorf(y + 0.3f)) || world_solid(L, (int)floorf(x), (int)floorf(y + 0.8f));
}
/* a stomp during the tell calls the attack off: hitting it when it stops is the reward for reading the tell */
void sim_boss_stomped(game *g) {
  boss_ai *b = &g->bz;
  if (g_boss_old || b->atk == BA_NONE || b->t >= boss_tell(b->atk)) return;
  b->atk = BA_NONE, b->cd = 70;
}
static void boss_step(game *g, enemy *e, float dt) {
  const level *L = &g->L;
  boss_ai *b = &g->bz;
  int k = L->boss_kind, maxhp = 5 + k;
  float half = boss_half(k), best = 1e9f, tx = 0;
  int who = -1;
  for (int i = 0; i < g->nplayers; i++) {
    const player *p = &g->p[i];
    if (!p->joined || !p->alive || p->bubble) continue;
    float d = pos_x(p) - e->x;
    if (d * d < best) best = d * d, tx = d, who = i;
  }
  bool awake = best <= 28 * 28; /* asleep until a player comes near */
  int ph = e->hp * 2 <= maxhp ? 1 : 0;
  if (ph > b->phase) { b->phase = ph, g->boss_stats[3] = ph; if (b->cd > 40) b->cd = 40; }
  float sp = 0;
  bool grounded = e->vy == 0;
  if (b->daze > 0) b->daze--;
  else if (b->atk == BA_NONE) {
    if (awake) {
      sp = (3.2f + 0.45f * (float)k) * (b->phase ? 1.25f : 1.0f);
      if (who >= 0) e->dir = tx > 0 ? 1 : -1;
      if (fabsf(tx) < 0.5f) sp = 0; /* under a player in the air: wait for it to come down */
      if (--b->cd <= 0 && grounded) {
        int n = 0;
        while (n < 3 && BOSS_MENU[k][b->phase][n]) n++;
        b->atk = BOSS_MENU[k][b->phase][b->n++ % n];
        b->t = 0, b->dir = e->dir, b->floor = e->y, b->vx = 0;
        b->tx = who >= 0 ? pos_x(&g->p[who]) : e->x;
        g->boss_stats[0]++;
      }
    }
  } else {
    int T = boss_tell(b->atk);
    b->t++;
    switch (b->atk) {
    case BA_LEAP:
      if (b->t == T) {
        e->vy = 13.0f + 0.4f * (float)k;
        float d = (b->tx - e->x) / 0.9f;
        b->vx = d > 9.0f ? 9.0f : d < -9.0f ? -9.0f : d;
        grounded = false;
      }
      if (b->t > T) {
        float nx = e->x + b->vx * dt;
        if (!boss_wall(L, nx + (b->vx > 0 ? half : -half), e->y)) e->x = nx; else b->vx = 0;
        if (grounded && b->t > T + 4) { /* down: the wave runs out both ways along the floor */
          shot_new(g, SH_WAVE, e->x + half * 0.6f, e->y, 7.5f, 0, e->y, 0);
          shot_new(g, SH_WAVE, e->x - half * 0.6f, e->y, -7.5f, 0, e->y, 0);
          sim_rumble_at(g, e->x, e->y, 0.8f);
          boss_end(g, k);
        }
      }
      break;
    case BA_RAIN:
      if (b->t == T) { /* one over each player in the arena and more between them, never two on one spot */
        float xs[8];
        int n = 0, want = 3 + 2 * b->phase;
        float lo = e->home_x - e->range + 1.0f, hi = e->home_x + e->range - 1.0f;
        for (int i = 0; i < g->nplayers && n < want; i++) {
          const player *p = &g->p[i];
          if (!p->joined || !p->alive || p->bubble || pos_x(p) < lo || pos_x(p) > hi) continue;
          xs[n++] = pos_x(p);
        }
        uint32_t h = (uint32_t)g->tick * 2654435761u ^ (uint32_t)k * 40503u;
        for (int tries = 0; n < want && tries < 40; tries++) {
          h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
          float x = lo + (hi - lo) * (float)(h & 1023) / 1023.0f;
          bool ok = true;
          for (int j = 0; j < n; j++) if (fabsf(xs[j] - x) < 1.9f) ok = false;
          if (ok) xs[n++] = x;
        }
        for (int j = 0; j < n; j++) shot_new(g, SH_FALL, xs[j], b->floor + 11.0f, 0, 0, b->floor, -10 * j);
      }
      if (b->t >= T + 20) boss_end(g, k);
      break;
    case BA_CHARGE:
      if (b->t > T) {
        e->dir = b->dir;
        float nx = e->x + (float)b->dir * (9.5f + 0.6f * (float)k) * dt;
        if (boss_wall(L, nx + (float)b->dir * half, e->y)) { /* into the wall: it stands there seeing stars */
          b->daze = 96, g->boss_stats[4]++;
          sim_rumble_at(g, e->x + (float)b->dir * half, e->y + 1.0f, 0.9f);
          boss_end(g, k);
        } else if (nx > e->home_x + e->range + 2.0f || nx < e->home_x - e->range - 2.0f || b->t > T + 150) { /* run out of room or breath: it stops, winded, a shorter daze */
          b->daze = 64, g->boss_stats[4]++;
          boss_end(g, k);
        }
        else e->x = nx;
      }
      break;
    case BA_ROLL:
      if (b->t == T) shot_new(g, SH_ROLL, e->x + (float)b->dir * (half + 0.3f), b->floor, (float)b->dir * (6.2f + 0.5f * (float)b->phase), 0, b->floor, 0);
      if (b->t >= T + 14) boss_end(g, k);
      break;
    case BA_THROW:
      if (b->t == T) {
        float sx = e->x + (float)b->dir * 0.5f, sy = e->y + 2.6f, ft = 1.05f;
        shot_new(g, SH_ARC, sx, sy, (b->tx - sx) / ft, (b->floor - sy) / ft + 9.0f * ft, b->floor, 0);
      }
      if (b->t >= T + 12) boss_end(g, k);
      break;
    default: boss_end(g, k);
    }
  }
  if (sp > 0) {
    float nx = e->x + (float)e->dir * sp * dt;
    if (!boss_wall(L, nx + (float)e->dir * half, e->y) && nx <= e->home_x + e->range && nx >= e->home_x - e->range) e->x = nx;
  }
  e->vy -= 30.0f * dt;
  float ny = e->y + e->vy * dt;
  if (e->vy <= 0 && world_solid(L, (int)floorf(e->x), (int)floorf(ny - 0.01f))) {
    ny = floorf(ny - 0.01f) + 1.0f;
    if (e->vy < -6) sim_rumble_at(g, e->x, e->y, 0.5f);
    e->vy = 0;
  }
  e->y = ny;
}

static void hurt_player(game *g, int i, float kx, int dmg, int cause);
/* what the bosses threw: a wave, a fall, a roll, a lob. Jumping a wave or a roll, or standing clear of a fall, is a dodge */
static void shots_step(game *g, float dt) {
  const level *L = &g->L;
  for (int n = 0; n < MAX_BSHOTS; n++) {
    bshot *s = &g->shots[n];
    if (!s->alive) continue;
    s->t++;
    if (s->t < 0) continue;
    float sg = s->vx > 0 ? 1.0f : -1.0f;
    switch (s->kind) {
    case SH_WAVE:
    case SH_ROLL: {
      s->x += s->vx * dt;
      if (boss_wall(L, s->x + sg * 0.45f, s->floor)) {
        if (s->kind == SH_WAVE || ++s->bounces >= 2) { s->alive = false; continue; }
        s->vx = -s->vx, sg = -sg;
      }
      if (s->kind == SH_WAVE && s->t > 54) { s->alive = false; continue; }
      if (s->t > 420) { s->alive = false; continue; }
      float reach = s->kind == SH_WAVE ? 0.45f : 0.55f, top = s->kind == SH_WAVE ? 0.4f : 0.8f;
      for (int i = 0; i < g->nplayers; i++) {
        const player *p = &g->p[i];
        if (!p->joined || !p->alive || p->bubble) continue;
        float dx = pos_x(p) - s->x, py = pos_y(p);
        if (fabsf(dx) > reach + 0.15f || py > s->floor + 3.0f || py < s->floor - 0.5f) continue;
        if (py < s->floor + top && fabsf(dx) < reach && p->invuln <= 0) { g->boss_stats[1]++; hurt_player(g, i, sg, 1, DIE_BOSS); }
        else if (py >= s->floor + top && !(s->seen >> i & 1)) s->seen |= 1u << i, g->boss_stats[2]++;
      }
      break;
    }
    case SH_FALL:
      if (s->land_t >= 0) { if (s->t > s->land_t + 26) s->alive = false; break; }
      if (s->t < fall_warn()) break;
      s->vy -= 30.0f * dt, s->y += s->vy * dt;
      if (s->y <= s->floor) {
        s->y = s->floor, s->land_t = s->t;
        sim_rumble_at(g, s->x, s->floor, 0.45f);
        for (int i = 0; i < g->nplayers; i++) {
          const player *p = &g->p[i];
          if (!p->joined || !p->alive || p->bubble) continue;
          float dx = pos_x(p) - s->x, py = pos_y(p);
          if (fabsf(dx) < 0.8f && py < s->floor + 1.8f && py > s->floor - 0.5f) { if (p->invuln <= 0) g->boss_stats[1]++; hurt_player(g, i, dx >= 0 ? 1.0f : -1.0f, 1, DIE_BOSS); }
          else if (fabsf(dx) < 2.4f && fabsf(py - s->floor) < 2.0f) g->boss_stats[2]++;
        }
      }
      break;
    case SH_ARC:
      s->x += s->vx * dt, s->vy -= 18.0f * dt, s->y += s->vy * dt;
      for (int i = 0; i < g->nplayers; i++) {
        const player *p = &g->p[i];
        if (!p->joined || !p->alive || p->bubble) continue;
        float dx = pos_x(p) - s->x, dy = pos_y(p) + 0.8f - s->y;
        bool landed = s->y <= s->floor;
        if ((fabsf(dx) < 0.45f && fabsf(dy) < 0.7f) || (landed && fabsf(dx) < 0.7f && pos_y(p) < s->floor + 1.5f && pos_y(p) > s->floor - 0.5f)) {
          if (p->invuln <= 0) g->boss_stats[1]++;
          hurt_player(g, i, dx >= 0 ? 1.0f : -1.0f, 1, DIE_BOSS);
          s->alive = false;
        }
      }
      if (s->alive && (s->y <= s->floor || boss_wall(L, s->x, s->y - 0.3f))) { s->alive = false; sim_rumble_at(g, s->x, s->floor, 0.25f); }
      break;
    }
  }
}

static void enemy_step(game *g, enemy *e, float dt) {
  const level *L = &g->L;
  if (!e->alive) return;
  if (e->hurt > 0) e->hurt--;
  if (e->kind >= EK_HOPPER) { new_enemy_step(g, e, dt); return; }
  if (e->kind == EK_BOSS && !g_boss_old) { boss_step(g, e, dt); return; }
  float half = e->kind == 2 ? (g_boss_clip_bug ? 1.0f : boss_half(L->boss_kind)) : 0.45f;
  float h = e->kind == 2 ? 2.0f : 0.9f;
  if (e->kind == 1) { /* flyer: a slow sine around its home */
    e->vx = (float)e->dir * 2.0f;
    e->x += e->vx * dt;
    if (e->x > e->home_x + e->range) e->dir = -1;
    if (e->x < e->home_x - e->range) e->dir = 1;
    float ph = (float)(g->tick % 240) / 240.0f;
    float tri = ph < 0.5f ? ph * 2 : 2 - ph * 2;
    e->y = e->home_x * 0 + (float)L->ground[(int)e->x < 0 ? 0 : (int)e->x >= WORLD_W ? WORLD_W - 1 : (int)e->x] + 2.2f + tri * 1.6f;
    return;
  }
  float sp = e->kind == 2 ? 3.2f + 0.45f * (float)L->boss_kind : 1.8f;
  /* the bosses are not the same boss: the Lifeguard charges, the Manager will not stop in time, the Founder does both */
  if (e->kind == 2 && (L->boss_kind == 2 || L->boss_kind == 4) && (g->tick % 210) < 45) sp *= 2.6f;
  if (e->kind == 2 && (L->boss_kind == 3 || L->boss_kind == 4) && (g->tick % 90) < 20) e->dir = e->vx0_dir ? e->vx0_dir : e->dir;
  if (e->kind == 2) e->vx0_dir = e->dir;
  float tx = 0;
  if (e->kind == 2) { /* boss: chases the nearest player inside the arena, leaps now and then */
    float best = 1e9f;
    for (int i = 0; i < g->nplayers; i++) {
      if (!g->p[i].alive || g->p[i].bubble) continue;
      float d = pos_x(&g->p[i]) - e->x;
      if (d * d < best) best = d * d, tx = d;
    }
    if (best < 1e8f) e->dir = tx > 0 ? 1 : -1;
    bool awake = best <= 28 * 28; /* asleep until a player comes near: it neither walks nor leaps */
    if (!awake) sp = 0;
    if (awake && (g->tick % (150 - 12 * L->boss_kind)) == 0 && e->vy == 0) e->vy = 11.0f + 0.5f * (float)L->boss_kind;
  }
  float nx = e->x + (float)e->dir * sp * dt;
  bool wall = world_solid(L, (int)floorf(nx + (float)e->dir * half), (int)floorf(e->y + 0.3f)) ||
              world_solid(L, (int)floorf(nx + (float)e->dir * half), (int)floorf(e->y + 0.8f));
  bool edge = e->kind == 0 && !world_solid(L, (int)floorf(nx + (float)e->dir * half), (int)floorf(e->y - 0.2f));
  if (wall || edge || nx > e->home_x + e->range || nx < e->home_x - e->range) {
    e->dir = -e->dir;
    nx = e->x;
  }
  e->x = nx;
  /* gravity onto the ground */
  e->vy -= 30.0f * dt;
  float ny = e->y + e->vy * dt;
  if (e->vy <= 0 && world_solid(L, (int)floorf(e->x), (int)floorf(ny - 0.01f))) {
    /* stand on top of the tile that was hit. (floorf(ny) + 1 put a body whose feet reached the top within 0.01 m
     * one whole tile ABOVE it: it fell a metre, landed hard and was lifted again, every 20 ticks forever.) */
    ny = floorf(ny - 0.01f) + 1.0f;
    if (e->vy < -6 && e->kind == 2) sim_rumble_at(g, e->x, e->y, 0.5f); /* a real leap landing, not a step: the ground thuds */
    e->vy = 0;
  }
  e->y = ny;
  (void)h;
}

void sim_rumble(game *g, int p, float low, float high, int ms) {
  if (p < 0 || p >= g->nplayers || !g->p[p].joined) return;
  if (low > g->rumble[p].low) g->rumble[p].low = low;
  if (high > g->rumble[p].high) g->rumble[p].high = high;
  if (ms > g->rumble[p].ms) g->rumble[p].ms = ms;
}

void sim_rumble_at(game *g, float x, float y, float amount) {
  g->impacts++;
  for (int i = 0; i < g->nplayers; i++) {
    const player *p = &g->p[i];
    if (!p->joined || !p->alive || p->bubble) continue;
    float dx = (float)p->pf.body.position.x - x, dy = (float)p->pf.body.position.y - y, d = sqrtf(dx * dx + dy * dy);
    float k = d <= 4 ? 1.0f : d >= 24 ? 0.0f : 1.0f - (d - 4) / 20.0f;
    if (k <= 0) continue;
    k *= k * amount;
    sim_rumble(g, i, 0.2f + 0.8f * k, 0.25f * k, 110 + (int)(300 * k));
  }
}

/* the lowest the camera may look: the bottom of the view stays on the world's floor row (y 0), so a pit never shows
 * the underside of the world and the backdrop behind it. The view is 2 tan(20 deg) z tall (fov 40). */
bool g_cam_nofloor; /* the pit camera gate's control (COOP_PIT_CAM_BUG, set by main.c): no floor */
bool g_cam_endwall_bug, g_boss_clip_bug, g_bot_no_boss;
static float cam_floor(float z) { return g_cam_nofloor ? -1e9f : 0.364f * z + 0.6f; }
/* the lip of whatever pit x is over */
float sim_lip_of(const level *L, float x) {
  /* the ground either side of the pit, the nearest that is not pit; a tall wall further along has no say */
  int c = (int)floorf(x), best = 0;
  for (int dir = -1; dir <= 1; dir += 2)
    for (int k = c + dir, n = 0; k >= 0 && k < WORLD_W && n < 30; k += dir, n++) {
      int gr = L->ground[k];
      if (gr == 0) continue;
      if (gr < WORLD_H && gr > best) best = gr;
      break;
    }
  return (float)best;
}
/* nothing to stand on all the way down from the path: a pit (a pool's or a shaft's floor is a floor) */
bool sim_open_pit(const level *L, float x) {
  int c = (int)floorf(x);
  if (c < 0 || c >= WORLD_W) return false;
  return L->ground[c] == 0;
}
static void camera_step(game *g, float dt) {
  float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
  int n = 0;
  /* a player left behind (lead_w falling from 1 to 0 over two seconds) is framed nearer and nearer the centre of the ones
   * who play, so the view glides off them instead of cutting */
  float cx = 0, cy = 0, cw = 0;
  for (int i = 0; i < g->nplayers; i++) {
    const player *p = &g->p[i];
    if (!p->joined || !p->alive || p->bubble) continue;
    cx += pos_x(p) * p->lead_w, cy += pos_y(p) * p->lead_w, cw += p->lead_w;
  }
  if (cw > 1e-4f) cx /= cw, cy /= cw;
  for (int i = 0; i < g->nplayers; i++) {
    const player *p = &g->p[i];
    if (!p->joined || !p->alive || p->bubble || p->lead_w <= 1e-4f || cw <= 1e-4f) continue;
    float x = cx + (pos_x(p) - cx) * p->lead_w, y = cy + (pos_y(p) - cy) * p->lead_w;
    /* the camera stays up at the lip of a pit and watches someone drop out of the bottom of the picture; it never goes down
     * after them to stare at two walls */
    if (!g_cam_nofloor && sim_open_pit(&g->L, x)) { float lip = sim_lip_of(&g->L, x); if (y < lip) y = lip; }
    minx = fminf(minx, x), maxx = fmaxf(maxx, x), miny = fminf(miny, y), maxy = fmaxf(maxy, y), n++;
  }
  if (n == 0 && cw <= 1e-4f) { /* nobody up who plays: follow a bubble (one left behind is glided to a stop) */
    for (int i = 0; i < g->nplayers; i++)
      if (g->p[i].bubble && plays(g, i)) {
        minx = maxx = g->p[i].bubble_x, miny = maxy = g->p[i].bubble_y, n = 1;
        break;
      }
  }
  camera_rig *c = &g->cam;
  c->px = c->x, c->py = c->y, c->pz = c->z;
  const float W[3] = { 7.0f, 6.0f, 4.0f }, AMAX = 90.0f, VMAX = 30.0f;
  float *pos[3] = { &c->x, &c->y, &c->z }, *vel[3] = { &c->vx, &c->vy, &c->vz };
  if (n == 0) { /* nobody to follow (everyone down): glide to a stop, never a dead stop from a fall's speed */
    for (int a = 0; a < 3; a++) {
      float v = *vel[a], dv = AMAX * dt;
      *vel[a] = v > dv ? v - dv : v < -dv ? v + dv : 0;
      *pos[a] += *vel[a] * dt;
    }
    if (c->y < cam_floor(c->z)) c->y = cam_floor(c->z), c->vy = c->vy < 0 ? 0 : c->vy;
    return;
  }
  float spanx = maxx - minx, spany = maxy - miny;
  c->tx = (minx + maxx) * 0.5f;
  c->ty = (miny + maxy) * 0.5f + 2.2f;
  /* the view is about 1.29 * z wide at this fov and aspect: keep every player inside with a margin */
  float need = fmaxf(spanx / 1.0f + 12.0f, (spany + 6.0f) * 1.8f);
  float z = need * 0.78f;
  c->tz = z < 17 ? 17 : z > 34 ? 34 : z;
  if (c->ty < cam_floor(c->tz)) c->ty = cam_floor(c->tz); /* a player low in a pit stays inside the bottom of the view */
  /* the wall at the end of a level: the view stops with a strip of it in frame, enough to see that it is the end. Half the
   * view is tan(20 deg) z high and 16:9 wide, measured at the wall's front face. Everybody stays in view: the rightmost is
   * short of the wall, and the shift only gives the others more room */
  if (!g_cam_endwall_bug && g->L.width > 0) {
    float edge = (float)g->L.width + 0.6f - 0.64706f * (c->tz - 1.5f);
    if (c->tx > edge) c->tx = edge;
  }
  /* a target that jumped (a respawn, a warp, the group's centre moving to a rescued player) is a cut, not a whip
   * across the level */
  if (fabsf(c->tx - c->x) > 9.0f || fabsf(c->ty - c->y) > 9.0f) {
    c->x = c->tx, c->y = c->ty, c->z = c->tz;
    c->vx = c->vy = c->vz = 0;
    return;
  }
  /* follow: a critically damped spring per axis (no overshoot, no oscillation) whose acceleration and speed are
   * bounded, so the view never jolts: velocity changes by at most 2.5 cm per tick */
  const float tgt[3] = { c->tx, c->ty, c->tz };
  for (int a = 0; a < 3; a++) {
    float acc = W[a] * W[a] * (tgt[a] - *pos[a]) - 2.0f * W[a] * *vel[a];
    acc = acc > AMAX ? AMAX : acc < -AMAX ? -AMAX : acc;
    float v = *vel[a] + acc * dt;
    v = v > VMAX ? VMAX : v < -VMAX ? -VMAX : v;
    *vel[a] = v;
    *pos[a] += v * dt;
  }
}

/* ── the things in a level ────────────────────────────────────────── */
static float thing_tri(float t) { float f = t - floorf(t); return f < 0.5f ? f * 2 : 2 - f * 2; }

/* what a thing does to one player's movement this tick: shared with the reachability check, so what it proves is real */
static void things_move(game *g, player *p, const input_state *is, float dt) {
  level *L = &g->L;
  cw_platformer2d *pf = &p->pf;
  float fx = pos_x(p), fy = pos_y(p);
  for (int k = 0; k < L->nthings; k++) {
    thing *t = &L->things[k];
    float dx = fx - t->x, dy = fy - t->y;
    switch (t->kind) {
      case TH_SPRING:
        if (fabsf(dx) < t->w && dy > -0.2f && dy < 0.45f && pf->body.velocity.y <= 0.5f) {
          pf->body.velocity.y = 23.0f, pf->body.on_floor = false, p->launched = 40, p->jump_fx = 1;
          g->things_hit++;
          g->events |= EV_JUMP;
        }
        break;
      case TH_FAN:
        if (fabsf(dx) < t->w && fy > t->y - 0.5f && fy < t->y + t->h) {
          float top = 1.0f - fminf(1.0f, fmaxf(0.0f, (fy - t->y) / t->h)); /* weaker at the top: you hang there */
          float v = pf->body.velocity.y;
          v += (40.0f + 90.0f * top) * dt;
          pf->body.velocity.y = v > 10.0f ? 10.0f : v;
          pf->body.on_floor = false;
          p->launched = p->launched < 2 ? 2 : p->launched;
        }
        break;
      case TH_CANNON:
        if (fabsf(dx) < t->w + 0.2f && dy > -0.2f && dy < 1.6f && is->jump_pressed) {
          pf->body.velocity = (cw_vec2){ t->a, t->b };
          pf->body.on_floor = false;
          pf->facing = t->a > 0 ? 1 : -1;
          p->launched = 70, p->launch_vx = t->a, p->jump_fx = 2;
          g->things_hit++;
          g->events |= EV_BREAK;
        }
        break;
      case TH_PLATE:
        if (fabsf(dx) < t->w && dy > -0.2f && dy < 0.4f) {
          t->pressed = true;
          if (t->link >= 0) {
            thing *d = &L->things[t->link];
            if (g->reach && d->state == 0) g->reach_changed = true;
            d->state = g->reach ? 1 << 30 : (d->state < (int)t->a ? (int)t->a : d->state);
            if (g->reach) cw_body2d_set_transform(d->body, (cw_xform2){ { d->x + d->w * 0.5f, d->y + d->h * 1.5f }, { 1, 0 } });
          }
        }
        break;
      default: break;
    }
  }
}

/* the things move on their own: a tick of the clock (only in play: the reachability check holds them still) */
static void things_step(game *g, float dt) {
  level *L = &g->L;
  for (int k = 0; k < L->nthings; k++) {
    thing *t = &L->things[k];
    switch (t->kind) {
      case TH_SAW: {
        float ph = (float)g->tick * dt * t->b / (t->a * 4.0f + 0.01f) + t->c;
        t->t = t->x + t->a * (thing_tri(ph) * 2.0f - 1.0f);
        break;
      }
      case TH_PRESS: {
        int ph = (g->tick + (int)t->b) % (int)t->a, slam = (int)t->a - 60;
        /* up, a drop in 10 ticks, a rest of 20, 30 back up */
        float d = ph < slam ? 0.0f : ph < slam + 10 ? (float)(ph - slam) / 10.0f : ph < slam + 30 ? 1.0f : 1.0f - (float)(ph - slam - 30) / 30.0f;
        t->dy = d;
        break;
      }
      case TH_FALLER:
        if (t->state == 0) {
          for (int i = 0; i < g->nplayers; i++) {
            const player *p = &g->p[i];
            if (p->joined && p->alive && !p->bubble && pos_x(p) > t->x - 4.5f && pos_x(p) < t->x + 1.0f) t->state = 1, t->t = 0;
          }
        } else if (t->state == 1) {
          t->t += dt;
          t->dy = 19.0f * t->t * t->t;
          if (t->dy >= t->h) t->dy = t->h, t->state = 2, t->t = 0, g->events |= EV_LAND, sim_rumble_at(g, t->x, t->y, 0.5f);
        } else {
          t->t += dt;
          if (t->t > 6.0f) t->state = 0, t->dy = 0, t->t = 0; /* another one is on its way up there */
        }
        break;
      case TH_CRUMBLE: {
        bool on = false;
        if (t->state < 2)
          for (int i = 0; i < g->nplayers; i++) {
            const player *p = &g->p[i];
            float dx = pos_x(p) - (t->x + 0.5f), dy = pos_y(p) - (t->y + 1.0f);
            if (p->joined && p->alive && !p->bubble && p->pf.body.on_floor && fabsf(dx) < 0.85f && fabsf(dy) < 0.25f) on = true;
          }
        if (t->state == 0 && on) t->state = 1, t->t = 0;
        else if (t->state == 1) {
          t->t += dt;
          if (t->t > 0.55f) {
            t->state = 2, t->t = 0;
            cw_body2d_set_transform(t->body, (cw_xform2){ { t->x + 0.5f, -40.0f }, { 1, 0 } });
            L->t[(int)t->y][(int)t->x] = T_AIR;
            g->events |= EV_BREAK;
          }
        } else if (t->state == 2) {
          t->t += dt;
          if (t->t > 5.0f) {
            bool clear = true;
            for (int i = 0; i < g->nplayers; i++) if (fabsf(pos_x(&g->p[i]) - (t->x + 0.5f)) < 1.0f && fabsf(pos_y(&g->p[i]) - t->y) < 1.6f) clear = false;
            if (clear) {
              t->state = 0, t->t = 0;
              cw_body2d_set_transform(t->body, (cw_xform2){ { t->x + 0.5f, t->y + 0.5f }, { 1, 0 } });
              L->t[(int)t->y][(int)t->x] = T_CRUMBLE;
            }
          }
        }
        break;
      }
      case TH_DOOR: {
        /* it does not come down on anyone standing in the doorway */
        bool under = false;
        for (int i = 0; i < g->nplayers; i++) {
          const player *p = &g->p[i];
          if (p->joined && !p->bubble && pos_x(p) > t->x - 0.4f && pos_x(p) < t->x + t->w + 0.4f && pos_y(p) < t->y + t->h) under = true;
        }
        if (t->state > 0 && !(t->state == 1 && under)) t->state--;
        t->dy += ((t->state > 0 ? t->h : 0.0f) - t->dy) * fminf(1.0f, 10.0f * dt); /* slides up into the wall, drops back when time is up */
        cw_body2d_set_transform(t->body, (cw_xform2){ { t->x + t->w * 0.5f, t->y + t->h * 0.5f + t->dy }, { 1, 0 } });
        break;
      }
      case TH_PLATE: t->pressed = false; break; /* a player's step presses it again below */
      case TH_MIMIC: /* a coin that has been waiting. It wiggles a little, for anyone paying attention. It lunges, bites, flees */
        t->t += dt;
        if (t->state == 0) {
          for (int i = 0; i < g->nplayers; i++) {
            player *p = &g->p[i];
            if (!p->joined || !p->alive || p->bubble || p->invuln > 0) continue;
            float dx = pos_x(p) - t->x, dy = pos_y(p) + 0.7f - t->y;
            if (dx * dx + dy * dy < 1.2f * 1.2f) {
              hurt_player(g, i, dx >= 0 ? 1.0f : -1.0f, 1, DIE_COIN);
              t->state = 1, t->dy = 0, g->mimic_bites++;
              break;
            }
          }
        } else if (t->state == 1) { /* off it goes, laughing, up out of the world */
          t->dy += 9.0f * dt;
          if (t->dy > 14.0f) t->state = 2;
        }
        break;
      case TH_BOOM: /* a sign with something to say, and a fuse: read it too long and up you go. Hurts nobody, offends everyone */
        if (t->state == 0) {
          for (int i = 0; i < g->nplayers; i++) {
            const player *p = &g->p[i];
            if (p->joined && p->alive && !p->bubble && fabsf(pos_x(p) - t->x) < 2.2f && fabsf(pos_y(p) - t->y) < 3.0f) t->state = 1, t->t = 0;
          }
        } else if (t->state == 1) {
          t->t += dt;
          if (t->t > 0.9f) {
            t->state = 2;
            g->events |= EV_BREAK;
            sim_rumble_at(g, t->x, t->y + 1, 0.8f);
            for (int i = 0; i < g->nplayers; i++) {
              player *p = &g->p[i];
              if (p->joined && p->alive && !p->bubble && fabsf(pos_x(p) - t->x) < 3.2f && fabsf(pos_y(p) - t->y) < 3.5f) {
                p->pf.body.velocity = (cw_vec2){ pos_x(p) >= t->x ? 10.0f : -10.0f, 17.0f };
                p->pf.body.on_floor = false, p->launched = 40, p->launch_vx = p->pf.body.velocity.x;
              }
            }
          }
        }
        break;
      case TH_FAKE: /* the end of the level. No */
        if (t->state == 0)
          for (int i = 0; i < g->nplayers; i++)
            if (g->p[i].joined && g->p[i].alive && pos_x(&g->p[i]) >= t->x - 1.0f) { t->state = 1, t->t = 0, g->fake_t = 150, g->events |= EV_WIN; break; }
        if (t->state == 1) t->t += dt;
        break;
      case TH_CHASER: { /* something enormous, with a face. It wakes when the first player passes its mark and it does not stop */
        float lead = -1e9f, last = 1e9f;
        int alive = 0;
        for (int i = 0; i < g->nplayers; i++) {
          const player *p = &g->p[i];
          if (!p->joined || !p->alive || p->bubble) continue;
          lead = fmaxf(lead, pos_x(p)), last = fminf(last, pos_x(p)), alive++;
        }
        if (t->state == 0 && alive && lead > t->a) t->state = 1, t->t = t->x; /* t: where its middle is */
        if (t->state == 1) {
          t->t += 9.4f * dt; /* faster than a walk, slower than a run: it is why you run */
          t->dy += 9.4f * dt;
          if ((g->tick % 36) == 0) sim_rumble_at(g, t->t, t->y, 0.45f); /* every thud of it comes up through the pad */
          if (t->t > t->b) t->state = 2; /* it has met the wall at the end of the level */
          if (alive && last < t->x - 2.0f && lead < t->a - 2.0f) t->state = 0, t->t = t->x, t->dy = 0; /* everyone sent back past its start: it goes home too */
        } else if (t->state == 2 && alive && lead < t->a - 2.0f) t->state = 0, t->t = t->x, t->dy = 0;
        break;
      }
      default: break;
    }
  }
}

/* what the things do to a player: a saw, a press and a falling object end things at once */
static void things_hurt(game *g, int i) {
  level *L = &g->L;
  player *p = &g->p[i];
  if (p->invuln > 0 || p->bubble || !p->alive) return;
  float fx = pos_x(p), fy = pos_y(p);
  for (int k = 0; k < L->nthings; k++) {
    const thing *t = &L->things[k];
    switch (t->kind) {
      case TH_SAW: {
        float dx = fx - t->t, dy = fy + 0.7f - t->y;
        if (dx * dx + dy * dy < 1.05f * 1.05f) kill_player(g, i, DIE_SAW);
        break;
      }
      case TH_PRESS:
        if (fabsf(fx - t->x) < t->w && t->dy > 0.8f && fy < t->y + 2.0f) kill_player(g, i, DIE_PRESS);
        break;
      case TH_FALLER:
        if (t->state == 1 && fabsf(fx - t->x) < t->w && fy + 1.5f > t->y + t->h - t->dy - 0.2f && fy < t->y + t->h - t->dy + 2.0f) kill_player(g, i, DIE_FALLER);
        break;
      case TH_CHASER:
        if (t->state == 1 && fx < t->t + t->w - 0.6f && fx > t->t - t->w) kill_player(g, i, DIE_CHASER);
        break;
      default: break;
    }
    if (!p->alive) return;
  }
}

/* one player's movement for one tick: the water they are in, their abilities, the controller. The reachability check
 * (reach.c) moves its players through this too, so what it proves is what the game does. True when in water. */
bool sim_player_move(game *g, player *p, const input_state *is, float dt) {
  level *L = &g->L;
  cw_platformer2d *pf = &p->pf;
  float cx = pos_x(p), cy = pos_y(p) + 0.7f;
  int tc = tile_at(L, cx, cy);
  bool in_water = tc == T_WATER;
  pf->submerged = in_water ? (tile_at(L, cx, cy + 0.9f) == T_WATER ? 1.0f : 0.7f) : 0.0f;
  pf->current = (cw_vec2){ in_water ? L->water_current[(int)floorf(cx)] : 0.0f, 0 };
  pf->can_dash = (g->abilities & AB_DASH) != 0;
  pf->can_wall_jump = (g->abilities & AB_CLAWS) != 0;
  pf->max_air_jumps = (g->abilities & AB_DOUBLE) ? 1 : 0;
  things_move(g, p, is, dt);
  /* momentum: West held while moving builds it on the ground, letting go or stopping drains it, a wall ends it. It is kept
   * through a jump, so the speed and height a run built carry the whole way across */
  float vx0 = pf->body.velocity.x;
  bool moving = is->x > 0.2f || is->x < -0.2f, against = moving && vx0 * is->x < -0.5f;
  if (in_water) p->run = 0;
  else if (pf->body.on_floor) {
    if (is->run && moving && !against) p->run = fminf(1.0f, p->run + dt / RUN_BUILD);
    else p->run = fmaxf(0.0f, p->run - dt / (against ? RUN_DROP * 0.4f : RUN_DROP));
    if (pf->body.on_wall && moving) p->run = 0;
  }
  /* underfoot: ice lets go of you, and a belt carries you */
  {
    int ty = (int)floorf(pos_y(p) - 0.2f), tx = (int)floorf(pos_x(p));
    int under = pf->body.on_floor && tx >= 0 && tx < WORLD_W && ty >= 0 && ty < WORLD_H ? L->t[ty][tx] : T_AIR;
    pf->def.ground_accel = under == T_ICE ? 14.0f : 70.0f;
    pf->def.ground_brake = under == T_ICE ? 7.0f : 90.0f;
    if (under == T_BELT) {
      float nx = pos_x(p) + L->belt[tx] * dt, ahead = nx + (L->belt[tx] > 0 ? 0.4f : -0.4f);
      if (!world_solid(L, (int)floorf(ahead), (int)floorf(pos_y(p) + 0.3f))) pf->body.position.x = nx;
    }
  }
  pf->def.run_speed = (WALK_SPEED + (RUN_SPEED - WALK_SPEED) * p->run) * g->margin_run;
  if (p->launched > 0 && p->launch_vx != 0 && fabsf(p->launch_vx) > pf->def.run_speed) pf->def.run_speed = fabsf(p->launch_vx); /* a cannon's speed is not braked to a run */
  pf->def.jump_height = (JUMP_STAND + (JUMP_RUN - JUMP_STAND) * p->run) * g->margin_jump;
  cw_platformer2d_input pin = { .move_x = is->x, .move_y = is->y, .jump_pressed = is->jump_pressed, .jump_held = is->jump_held || p->launched > 0,
                                .dash_pressed = is->dash_pressed, .down_held = is->down };
  if (p->launched > 0) {
    p->launched--;
    if (pf->body.on_floor && pf->body.velocity.y <= 0) p->launched = 0;
    /* a cannon's throw is a throw: letting go of the stick does not stop it, pushing back does */
    if (p->launch_vx != 0 && p->launched > 0 && (is->x > -0.2f && is->x < 0.2f)) pin.move_x = p->launch_vx > 0 ? 1.0f : -1.0f;
    if (p->launched == 0) p->launch_vx = 0;
  }
  cw_vec2 lift = pf->body.platform_velocity;
  cw_platformer2d_step(pf, &pin, dt);
  /* a jump off something moving keeps what it was doing */
  if ((pf->events & CW_PF_JUMP) && (lift.x != 0 || lift.y > 0)) {
    pf->body.velocity.x += lift.x;
    if (lift.y > 0) pf->body.velocity.y += lift.y;
  }
  /* the measurement of a jump: from the ground it left to the ground it lands on */
  float fx = pos_x(p), fy = pos_y(p);
  if (pf->events & CW_PF_JUMP) p->in_jump = true, p->jump_x = fx, p->jump_y = fy, p->jump_top = fy, g->last_jump[2] = fabsf(vx0);
  if (p->in_jump) {
    if (fy > p->jump_top) p->jump_top = fy;
    if (pf->events & (CW_PF_LAND | CW_PF_AIR_JUMP | CW_PF_WALL_JUMP | CW_PF_DASH)) {
      if (pf->events & CW_PF_LAND) g->last_jump[0] = fabsf(fx - p->jump_x), g->last_jump[1] = p->jump_top - p->jump_y;
      p->in_jump = false;
    }
  }
  float sp = fabsf((float)pf->body.velocity.x);
  if (pf->dash_t <= 0 && sp > g->top_speed) g->top_speed = sp;
  return in_water;
}

/* a dash breaks a cracked wall in its way (hide: also take it off the screen). True when something broke. */
bool sim_dash_cracks(game *g, player *p, bool hide) {
  const level *L = &g->L;
  const cw_platformer2d *pf = &p->pf;
  if (pf->dash_t <= 0) return false;
  bool broke = false;
  float fx = pos_x(p) + (float)pf->facing * 0.9f;
  for (int dy = 0; dy < 3; dy++)
    if (tile_at(L, fx, pos_y(p) + 0.4f + (float)dy * 0.9f) == T_CRACK) {
      int tx = (int)floorf(fx);
      for (int y = 0; y < WORLD_H; y++)
        if (L->t[y][tx] == T_CRACK && world_break_crack(g, tx, y)) {
          if (hide) world_hide_crack(tx, y), g->events |= EV_BREAK;
          broke = true;
        }
    }
  return broke;
}

/* the tile hazards at a player's feet: 0 none, 1 spikes (hurt), 2 lava or the bottom of a pit (the end) */
int sim_hazard_at(const level *L, float fx, float fy) {
  if (tile_at(L, fx, fy + 0.2f) == T_LAVA || tile_at(L, fx, fy) == T_LAVA || fy < 2.2f) return 2;
  if (sim_open_pit(L, fx) && fy < sim_lip_of(L, fx) - 9.0f) return 2;
  int tf = tile_at(L, fx, fy + 0.15f), tb = tile_at(L, fx, fy + 0.9f);
  return tf == T_SPIKE || tb == T_SPIKE ? 1 : 0;
}

/* a coin touches a body whose feet are at fx, fy */
bool sim_coin_touch(const coin *c, float fx, float fy) {
  float dx = fx - c->x, dy = fy + 0.7f - c->y;
  return dx * dx + dy * dy < 0.9f;
}
/* an ability pedestal touches anywhere on the body */
bool sim_pickup_touch(const pickup *pk, float fx, float fy) {
  float dx = fx - ((float)pk->x + 0.5f), dy = fy - (float)pk->y;
  return fabsf(dx) < 1.0f && dy > -0.8f && dy < 2.6f;
}

void sim_step(game *g, const input_state in[MAX_PLAYERS], float dt) {
  level *L = &g->L;
  g->events = 0;
  g->ndeaths_q = 0;
  g->ngrief_q = 0;
  g->tick++;
  for (int i = 0; i < g->nplayers; i++) { /* any touch of the pad: this player plays */
    player *p = &g->p[i];
    const input_state *s = &in[i];
    if (s->x * s->x + s->y * s->y > 0.0625f || s->jump_held || s->run || s->dash_pressed || s->grab_pressed || s->down || s->retry) p->idle = 0;
    else if (p->idle < 1 << 30) p->idle++;
  }
  for (int i = 0; i < g->nplayers; i++) {
    player *p = &g->p[i];
    float tw = plays(g, i) ? 1.0f : 0.0f, k = dt / 2.0f;
    p->lead_w = p->lead_w + (tw - p->lead_w > k ? k : tw - p->lead_w < -k ? -k : tw - p->lead_w);
  }
  if (g->hit_flash > 0) g->hit_flash--;
  if (g->hitstop > 0) { /* hit-stop: the world stands still for a few ticks so a hit lands with weight */
    g->hitstop--;
    camera_step(g, dt); /* the world stands still; the view eases to the frozen players instead of stopping dead */
    for (int i = 0; i < g->nplayers; i++) {
      g->p[i].pf.body.prev_position = g->p[i].pf.body.position;
      if (in[i].jump_pressed) g->p[i].pf.buffer = g->p[i].pf.def.jump_buffer; /* a press during the freeze still counts */
    }
    g->hash = sim_hash(g);
    return;
  }
  /* moving platforms: a triangle wave between two points */
  for (int i = 0; i < L->nplat; i++) {
    mplat *m = &L->plats[i];
    m->t += m->speed * dt * 0.3f;
    float ph = m->t - floorf(m->t);
    float tri = ph < 0.5f ? ph * 2 : 2 - ph * 2;
    float x = m->x0 + (m->x1 - m->x0) * tri, y = m->y0 + (m->y1 - m->y0) * tri;
    cw_body2d_set_target(m->body, (cw_xform2){ { x, y }, { 1, 0 } }, dt);
  }
  things_step(g, dt);
  if (g->fake_t > 0) g->fake_t--;
  for (int i = 0; i < g->nplayers; i++) {
    player *p = &g->p[i];
    if (!p->joined) continue;
    p->land_fx = p->jump_fx = 0;
    if (p->pancake > 0) p->pancake--;
    if (p->invuln > 0) p->invuln--;
    if (p->bubble) continue;
    if (!p->alive) {
      /* a fallen player: carried in a bubble when someone is left, else back to the checkpoint */
      if (others_leading(g, i) > 0) {
        p->bubble = 1;
        p->bubble_x = pos_x(p), p->bubble_y = pos_y(p) + 1.0f;
        p->rescue_timer = 0;
        g->events |= EV_BUBBLE;
      } else {
        p->rescue_timer++;
        if (p->rescue_timer > 45) {
          bool all_dead = true;
          for (int k = 0; k < g->nplayers; k++)
            if (sim_player_leads(g, k)) all_dead = false; /* one nobody plays carries nobody */
          if (all_dead) {
            for (int k = 0; k < g->nplayers; k++)
              if (g->p[k].joined) sim_respawn_player(g, k);
          }
        }
      }
      continue;
    }
    cw_platformer2d *pf = &p->pf;
    /* held overhead by a friend: no say in the matter, except a jump, which wriggles free */
    if (p->carried_by) {
      player *h = &g->p[p->carried_by - 1];
      if (!h->joined || !h->alive || h->bubble || in[i].jump_pressed) {
        h->carrying = 0, p->carried_by = 0;
        pf->body.velocity = (cw_vec2){ 0, 9.0f };
      } else {
        pf->body.prev_position = pf->body.position;
        pf->body.position = (cw_pos2){ h->pf.body.position.x, h->pf.body.position.y + 1.55 };
        pf->body.velocity = (cw_vec2){ 0, 0 };
        pf->body.on_floor = false;
        p->anim = 3;
        continue;
      }
    }
    /* the grab button: pick up a friend in reach; with a friend overhead, throw them, wherever you happen to be facing */
    if (in[i].grab_pressed) {
      if (p->carrying) {
        player *q = &g->p[p->carrying - 1];
        q->carried_by = 0, p->carrying = 0;
        q->pf.body.velocity = (cw_vec2){ (float)pf->facing * 15.0f + (float)pf->body.velocity.x * 0.5f, 13.0f };
        q->launched = 45, q->launch_vx = q->pf.body.velocity.x, q->jump_fx = 2;
        g->throws[i]++;
        g->events |= EV_DASH;
        if (g->ngrief_q < 4) g->grief_q[g->ngrief_q++] = (typeof(g->grief_q[0])){ pos_x(q), pos_y(q), i, -1 - (int)(q - g->p) };
      } else {
        for (int k = 0; k < g->nplayers; k++) {
          player *q = &g->p[k];
          if (k == i || !q->joined || !q->alive || q->bubble || q->carried_by || q->carrying) continue;
          if (fabsf(pos_x(q) - pos_x(p)) < 1.3f && fabsf(pos_y(q) - pos_y(p)) < 1.2f) { p->carrying = k + 1, q->carried_by = i + 1; break; }
        }
      }
    }
    p->retry_t = in[i].retry ? p->retry_t + 1 : 0;
    if (p->retry_t >= 60) {
      p->retry_t = 0;
      sim_return(g);
      return;
    }
    const input_state *is = &in[i];
    float vy0 = (float)pf->body.velocity.y;
    bool in_water = sim_player_move(g, p, is, dt);
    uint32_t e = pf->events;
    if ((e & CW_PF_LAND) && vy0 < -14.0f) sim_rumble(g, i, fminf(0.35f, 0.08f + (-vy0 - 14.0f) * 0.02f), 0, 60); /* a long fall lands: a light bump */
    if (e & (CW_PF_JUMP | CW_PF_AIR_JUMP)) g->events |= EV_JUMP, p->jump_fx = 1;
    if (e & CW_PF_WALL_JUMP) g->events |= EV_WALL, p->jump_fx = 2;
    if (e & CW_PF_LAND) g->events |= EV_LAND, p->land_fx = 1;
    if (e & CW_PF_DASH) g->events |= EV_DASH;
    if (e & (CW_PF_ENTER_WATER | CW_PF_LEAVE_WATER)) g->events |= EV_SPLASH, p->land_fx = 2;
    sim_dash_cracks(g, p, true);
    /* animation state */
    if (in_water) p->anim = 5;
    else if (pf->dash_t > 0) p->anim = 6;
    else if (pf->body.on_floor) p->anim = (is->x > 0.1f || is->x < -0.1f) ? 1 : 0;
    else if (pf->body.on_wall) p->anim = 4;
    else p->anim = pf->body.velocity.y > 0 ? 2 : 3;
    /* hazards */
    float fx = pos_x(p), fy = pos_y(p);
    int tf = tile_at(L, fx, fy + 0.15f), tb = tile_at(L, fx, fy + 0.9f);
    if (tf == T_SPIKE || tb == T_SPIKE) hurt_player(g, i, pf->body.velocity.x >= 0 ? -1.0f : 1.0f, 1, DIE_SPIKE);
    if (tile_at(L, fx, fy + 0.2f) == T_LAVA || tile_at(L, fx, fy) == T_LAVA) kill_player(g, i, DIE_LAVA);
    /* a pit ends well below the ground beside it, not at the bottom of the world: the fall has been seen by then and nobody
     * waits for the rest of it */
    if (fy < 2.2f || (sim_open_pit(L, fx) && fy < sim_lip_of(L, fx) - 9.0f)) kill_player(g, i, DIE_PIT);
    things_hurt(g, i);
    /* the goal: walk into the booth (a boss level ends with the boss) */
    if (!L->boss_x && !g->won && L->goal_x > 0 && fx >= (float)L->goal_x) g->won = 1, g->win_tick = g->tick, g->events |= EV_WIN;
    /* checkpoints: touching a flag (its column, from the ground to a jump above it) makes it the respawn point */
    for (int k = 0; k < L->nchecks; k++) {
      float dx = fx - ((float)L->checks[k].x + 0.5f), dy = fy - (float)L->checks[k].y;
      if (dx > -1.6f && dx < 1.6f && dy > -1.0f && dy < 7.0f && k != g->checkpoint) {
        if (!(g->reached & (1u << k))) g->events |= EV_CHECK;
        g->reached |= 1u << k;
        g->checkpoint = k;
      }
    }
    /* pickups and coins */
    for (int k = 0; k < L->npickups; k++) {
      pickup *pk = &L->pickups[k];
      if (pk->taken) continue;
      /* a box over the whole body: a player who jumps under the arch over a pedestal still touches it (a radius
       * around the waist missed a jump that kept the feet 1.5 m up, and the group went on without the claws) */
      if (sim_pickup_touch(pk, fx, fy)) {
        pk->taken = true;
        g->abilities |= (uint32_t)pk->ability;
        g->events |= EV_POWER;
      }
    }
    for (int k = 0; k < L->ncoins; k++) {
      coin *c = &L->coins[k];
      if (c->taken) continue;
      if (sim_coin_touch(c, fx, fy)) {
        c->taken = true;
        g->coins_total++;
        p->coins++;
        if (c->secret) g->secrets++;
        if (c->medal) g->medals |= 1 << (c->medal - 1), p->coins += 4, g->coins_total += 4; /* a big coin is worth five */
        g->events |= EV_COIN;
      }
    }
    /* exploration */
    for (int dy = -3; dy <= 3; dy++)
      for (int dx = -4; dx <= 4; dx++) {
        int mx = ((int)fx + dx * 3) / 4, my = ((int)fy + dy * 3) / 4;
        if (mx >= 0 && mx < WORLD_W / 4 && my >= 0 && my < WORLD_H / 4) g->seen[my][mx] = 1;
      }
  }
  cw_world2d_step(g->world, dt);
  /* landing on a friend's head: you bounce, they go flat for a moment. Nobody is hurt. Nobody forgets */
  for (int i = 0; i < g->nplayers; i++) {
    player *a = &g->p[i];
    if (!a->joined || !a->alive || a->bubble || a->pf.body.velocity.y > -2.0f) continue;
    for (int k = 0; k < g->nplayers; k++) {
      player *b = &g->p[k];
      if (k == i || !b->joined || !b->alive || b->bubble || b->pancake > 0) continue;
      float dx = pos_x(a) - pos_x(b), dy = pos_y(a) - pos_y(b);
      if (fabsf(dx) < 0.55f && dy > 1.1f && dy < 1.7f) {
        a->pf.body.velocity.y = 12.0f;
        b->pancake = 45;
        a->jump_fx = 1;
        g->betrayals[i]++;
        g->events |= EV_STOMP;
        if (g->ngrief_q < 4) g->grief_q[g->ngrief_q++] = (typeof(g->grief_q[0])){ pos_x(b), pos_y(b), i, k };
        sim_rumble(g, k, 0.2f, 0.5f, 90);
        break;
      }
    }
  }
  /* enemies and contact */
  for (int k = 0; k < L->nenemies; k++) {
    enemy *e = &L->enemies[k];
    if (!e->alive) continue;
    enemy_step(g, e, dt);
    if (e->kind == EK_BOSS && !g_boss_old) shots_step(g, dt);
    float eh = e->kind == 2 ? 2.0f : 0.9f, ew = e->kind == 2 ? 1.1f : 0.5f;
    for (int i = 0; i < g->nplayers; i++) {
      player *p = &g->p[i];
      if (!p->joined || !p->alive || p->bubble) continue;
      float px = pos_x(p), py = pos_y(p);
      float dx = px - e->x;
      if (dx < -(ew + 0.3f) || dx > ew + 0.3f) continue;
      if (py > e->y + eh + 0.2f || py + 1.5f < e->y) continue;
      bool stomp = p->pf.body.velocity.y < -1.0f && py > e->y + eh * 0.55f;
      if (e->kind == EK_SHELL && (e->state > 0 || stomp)) { /* the shell: a stomp shuts it in, a touch kicks it, a stomp stops it */
        if (stomp) p->pf.body.velocity.y = 11.0f, p->pf.air_jumps_left = p->pf.max_air_jumps, g->events |= EV_STOMP;
        if (e->state == 0) { if (e->hurt == 0) e->state = 1, e->t = 0, e->hurt = 12, g->stomps[i]++, sim_rumble(g, i, 0.1f, 0.4f, 70); }
        else if (e->state == 1) { if (e->hurt == 0) e->state = 2, e->t = 0, e->bounces = 0, e->dir = px < e->x ? 1 : -1, e->hurt = 14, sim_rumble(g, i, 0.15f, 0.5f, 80); }
        else if (stomp) { if (e->hurt == 0) e->state = 1, e->t = 0, e->hurt = 12, g->stomps[i]++; }
        else if (e->hurt == 0) hurt_player(g, i, dx >= 0 ? 1.0f : -1.0f, 1, DIE_SHELL);
        continue;
      }
      if (e->kind == EK_SLIDER && (px - e->x) * (float)e->dir > 0.15f) stomp = false; /* its front is all spikes: from behind or nothing */
      if (e->kind == EK_DROPPER && e->state < 2) stomp = false; /* hanging or falling: it is the one doing the landing */
      if (stomp && e->hurt == 0) {
        e->hp--;
        g->stomps[i]++;
        e->hurt = e->kind == 2 && !g_boss_old ? 75 : 40; /* a boss shakes a stomp off for 1.25 s: long enough for its next attack to come round */
        p->pf.body.velocity.y = 12.0f;
        p->pf.air_jumps_left = p->pf.max_air_jumps;
        g->events |= EV_STOMP;
        g->hitstop = e->hp <= 0 ? 6 : 4;
        g->hitstops++;
        g->hit_flash = 5;
        /* the stomper feels the hit (sharp); a boss hit, the boss going down and a small enemy's blast thud for everyone near */
        sim_rumble(g, i, e->kind == 2 ? 0.3f : 0.1f, e->kind == 2 ? 0.7f : 0.4f, e->kind == 2 ? 120 : 70);
        if (e->kind == 2) g->boss_hp = e->hp, sim_rumble_at(g, e->x, e->y + 1.0f, e->hp <= 0 ? 1.0f : 0.6f), sim_boss_stomped(g);
        else if (e->hp <= 0) sim_rumble_at(g, e->x, e->y + 0.5f, 0.3f); /* the blast of a small enemy */
        if (e->hp <= 0) {
          e->alive = false;
          g->kills++;
          if (e->kind == 2 && !g->won) g->won = 1, g->win_tick = g->tick, g->events |= EV_WIN;
        }
      } else if ((e->hurt == 0 || e->kind != 2) && !(e->kind == 2 && g->bz.daze > 0 && !g_boss_old)) { /* a dazed boss hurts nobody */
        hurt_player(g, i, dx >= 0 ? 1.0f : -1.0f, 1, e->kind == 2 ? DIE_BOSS : DIE_ENEMY);
      }
    }
  }
  /* bubbles: drift toward the group, pop on contact */
  float gx = 0, gy = 0;
  int ng = 0;
  float lead = -1e9f;
  for (int i = 0; i < g->nplayers; i++) {
    const player *p = &g->p[i];
    if (sim_player_leads(g, i)) {
      gx += pos_x(p), gy += pos_y(p), ng++;
      if (pos_x(p) > lead) lead = pos_x(p);
    }
  }
  if (ng) gx /= (float)ng, gy /= (float)ng;
  for (int i = 0; i < g->nplayers; i++) {
    player *p = &g->p[i];
    if (!p->joined) continue;
    if (p->bubble && ng) {
      p->rescue_timer++;
      float dx = gx - p->bubble_x, dy = gy + 1.5f - p->bubble_y, d2 = dx * dx + dy * dy;
      float d = sqrtf(d2 + 1e-6f), sp = 7.5f * dt;
      if (d > sp) p->bubble_x += dx / d * sp, p->bubble_y += dy / d * sp;
      if (d < 1.6f || p->rescue_timer > 600) {
        float rx = p->bubble_x, ry = p->bubble_y;
        /* land on the ground below the bubble */
        for (int s = 0; s < 40 && !world_solid(L, (int)floorf(rx), (int)floorf(ry - 0.1f)); s++) ry -= 0.5f;
        while (world_solid(L, (int)floorf(rx), (int)floorf(ry + 0.4f))) ry += 0.5f;
        place_player(g, i, rx, ry);
        p->pf.body.velocity = (cw_vec2){ 0, 0 };
        p->invuln = 90;
        g->rescues++;
        g->events |= EV_POP;
      }
    } else if (p->bubble && !ng) {
      p->bubble = 0; /* nobody left to carry it: back to the checkpoint */
      sim_respawn_player(g, i);
    }
    /* falling too far behind the group: off screen for 2.5 s becomes a bubble. One nobody plays is off screen when it is
     * out of the view, which follows the others */
    bool leads = sim_player_leads(g, i);
    if (p->alive && !p->bubble && (ng > 1 || (ng > 0 && !leads))) {
      float dx = pos_x(p) - gx, dy = pos_y(p) - gy;
      bool off = dx > 24 || dx < -24 || dy > 15 || dy < -15;
      if (!leads) off = fabsf(pos_x(p) - g->cam.x) > 0.645f * g->cam.z + 0.5f || fabsf(pos_y(p) - g->cam.y) > 0.364f * g->cam.z + 1.5f;
      if (off) p->off_screen++;
      else p->off_screen = 0;
      if (p->off_screen > 150) {
        if (!leads) g->carried++;
        p->bubble = 1;
        p->bubble_x = pos_x(p), p->bubble_y = pos_y(p) + 1.0f;
        p->rescue_timer = 0;
        p->off_screen = 0;
        g->events |= EV_BUBBLE;
      }
    }
  }
  camera_step(g, dt);
  g->hash = sim_hash(g);
}

/* holding Retry for one second sends the whole group back to the last flag it touched (the start before any): there
 * is always a way out of a pit, a wedge or a bad fall */
void sim_return(game *g) {
  g->returns++;
  if (g->checkpoint >= 0 && sim_warp(g, g->checkpoint)) return;
  for (int i = 0; i < g->nplayers; i++)
    if (g->p[i].joined) sim_respawn_player(g, i);
}

bool sim_warp(game *g, int flag) {
  const level *L = &g->L;
  if (flag < 0 || flag >= L->nchecks || !(g->reached & (1u << flag))) return false;
  g->checkpoint = flag;
  int n = 0;
  for (int i = 0; i < g->nplayers; i++) {
    player *p = &g->p[i];
    if (!p->joined) continue;
    place_player(g, i, (float)L->checks[flag].x + 0.6f * (float)n, (float)L->checks[flag].y);
    p->pf.body.velocity = (cw_vec2){ 0, 0 };
    p->invuln = 60;
    p->bubble = 0;
    p->off_screen = 0;
    n++;
  }
  g->cam.x = g->cam.tx = g->cam.px = (float)L->checks[flag].x;
  g->cam.y = g->cam.ty = g->cam.py = (float)L->checks[flag].y + 3;
  g->events |= EV_CHECK;
  return true;
}

uint32_t sim_hash(const game *g) {
  uint32_t h = g->hash;
  for (int i = 0; i < g->nplayers; i++) {
    const player *p = &g->p[i];
    union { float f; uint32_t u; } a = { pos_x(p) }, b = { pos_y(p) };
    h = (h ^ a.u) * 16777619u;
    h = (h ^ b.u) * 16777619u;
    h = (h ^ (uint32_t)p->hp) * 16777619u;
  }
  h = (h ^ (uint32_t)g->coins_total) * 16777619u;
  h = (h ^ g->abilities) * 16777619u;
  return h;
}

/* ── the autopilot: runs right, jumps what is in the way, swims, wall jumps, dashes ── */

void sim_bot_input(const game *g, int i, input_state *in) {
  memset(in, 0, sizeof *in);
  const player *p = &g->p[i];
  const level *L = &g->L;
  if (!p->joined || !p->alive || p->bubble) return;
  const cw_platformer2d *pf = &p->pf;
  float x = pos_x(p), y = pos_y(p);
  float vy = pf->body.velocity.y;
  /* wait for the group when far ahead */
  float minx = x;
  for (int k = 0; k < g->nplayers; k++)
    if (g->p[k].joined && g->p[k].alive && !g->p[k].bubble && pos_x(&g->p[k]) < minx) minx = pos_x(&g->p[k]);
  (void)minx;
  in->x = 1.0f; /* nobody waits: a straggler is carried to the group in a bubble (that is the co-op rule being tested) */
  in->run = !g_bot_no_run; /* the bots run everywhere, as a person does */
  /* a boss in reach: go for it, as a person does. Run at it, take off close enough to come down on its head, and steer over it
   * in the air (a boss keeps off the end wall, so running on past it never meets it) */
  if (L->boss_x > 0 && !g_bot_no_boss)
    for (int k = 0; k < L->nenemies; k++) {
      const enemy *e = &L->enemies[k];
      if (e->kind != 2 || !e->alive || fabsf(e->x - x) > 26.0f || fabsf(e->y - y) > 8.0f) continue;
      float dx = e->x - x;
      in->x = fabsf(dx) < 0.3f ? 0.0f : dx > 0 ? 1.0f : -1.0f;
      in->jump_held = true;
      if (pf->body.on_floor && fabsf(dx) < 4.5f) in->jump_pressed = true;
      /* what it throws, seen as a person sees it (a fifth of a second late): jump a wave or a roll coming this way, walk
       * out from under a shaded spot, jump a charge */
      if (!g_boss_old) {
        for (int n = 0; n < MAX_BSHOTS; n++) {
          const bshot *s = &g->shots[n];
          if (!s->alive || s->t < 12) continue;
          float sdx = s->x - x;
          if ((s->kind == SH_WAVE || s->kind == SH_ROLL) && sdx * s->vx < 0 && fabsf(sdx) < 2.6f && fabsf(sdx) > 0.8f && y < s->floor + 0.6f) {
            if (pf->body.on_floor) in->jump_pressed = true;
            in->jump_held = true;
            return;
          }
          if (s->kind == SH_FALL && s->land_t < 0 && fabsf(sdx) < 1.4f && fabsf(y - s->floor) < 2.0f) {
            in->x = sdx > 0 ? -1.0f : 1.0f, in->jump_pressed = false;
            return;
          }
        }
        const boss_ai *b = &g->bz;
        if (b->atk == BA_CHARGE && b->t > 14 && (x - e->x) * (float)b->dir > 0 && fabsf(dx) < 3.5f && pf->body.on_floor) in->jump_pressed = true;
      }
      return;
    }
  /* stuck in one place for a few seconds: back up and take a run at it, as a person does */
  {
    player *me = (player *)p;
    if (fabsf(x - me->bot_anchor) > 3.0f) me->bot_anchor = x, me->bot_stuck = 0;
    else me->bot_stuck++;
    if (me->bot_stuck > 200 && me->bot_back == 0 && pf->body.on_floor) me->bot_back = 50, me->bot_stuck = 0;
    if (me->bot_back > 0) { me->bot_back--; in->x = -1.0f; if (pf->body.on_floor) return; }
  }
  bool wet = pf->submerged > 0.5f;
  if (wet) {
    /* swim: stroke up when below the surface line or blocked, cross to the right */
    int top = (int)floorf(y);
    while (top < WORLD_H - 1 && L->t[top + 1][(int)x] == T_WATER) top++;
    float want = (float)top + 0.2f;
    bool blocked = world_solid(L, (int)floorf(x + 1.0f), (int)floorf(y + 0.5f));
    if (y + 0.7f < want - 0.3f && (g->tick % 14) == 0) in->jump_pressed = true;
    if (blocked || (y < want - 1.5f && (g->tick % 9) == 0)) in->jump_pressed = true;
    in->jump_held = in->jump_pressed;
    in->y = blocked ? 1.0f : 0.0f;
    /* against the pool wall: stroke straight up, off the wall (a stroke while touching it is a wall jump with the claws,
     * which threw the bot back into the pool, over and over) */
    if (blocked && pf->body.on_wall) {
      in->x = -0.6f; /* back off the wall a little, then stroke: the next stroke is a swim stroke, not a wall jump */
      in->jump_pressed = in->jump_held = false;
    }
    return;
  }
  /* inside a shaft (a wall within 5 tiles on both sides): cross to the far wall and wall jump up */
  bool in_shaft = false;
  if (!pf->body.on_floor && (g->abilities & AB_CLAWS)) {
    bool lw = false, rw = false;
    for (int k = 1; k <= 5; k++) {
      for (int dy = 1; dy <= 6; dy++) { /* beside the body and over it: the near wall of a shaft stands on a low door */
        if (world_solid(L, (int)floorf(x) - k, (int)floorf(y) + dy)) lw = true;
        if (world_solid(L, (int)floorf(x) + k, (int)floorf(y) + dy)) rw = true;
      }
    }
    in_shaft = lw && rw;
    if (in_shaft && !pf->body.on_wall) {
      in->x = (float)pf->facing;
      in->jump_held = true;
      return;
    }
  }
  /* a cannon under the feet: climb in and press jump */
  for (int k = 0; k < L->nthings; k++) {
    const thing *t = &L->things[k];
    if (t->kind == TH_CANNON && fabsf(x - t->x) < t->w && y - t->y > -0.2f && y - t->y < 1.6f) {
      in->jump_pressed = true, in->jump_held = true;
      return;
    }
    /* a saw on its way, jump it; a press about to come down, wait for it */
    if (pf->body.on_floor && t->kind == TH_SAW && fabsf(t->t - (x + 2.2f)) < 1.6f && fabsf(t->y - (y + 0.7f)) < 1.6f) { in->jump_pressed = true, in->jump_held = true; return; }
    if (pf->body.on_floor && t->kind == TH_PRESS && x < t->x && t->x - x < 4.0f && ((g->tick + (int)t->b) % (int)t->a) > (int)t->a - 100) { in->x = 0; in->run = false; return; }
  }
  /* wall jumps are for shafts: on the face of an ordinary wall the bot lets go and slides down to try again */
  if (pf->body.on_wall && !pf->body.on_floor && (g->abilities & AB_CLAWS) && in_shaft) {
    in->jump_pressed = true, in->jump_held = true;
    in->x = (float)pf->facing;
    return;
  }
  /* on a wall in a pit too wide to count as a shaft (nothing to stand on for 4 tiles below): climb the one wall, a wall
   * jump and then back in toward it, the way a player does. A bot used to slide down into the spikes of the ruins pit
   * forever (2 bots, 30 s stalls) */
  if (pf->body.on_wall && !pf->body.on_floor && (g->abilities & AB_CLAWS) && !in_shaft) {
    bool deep = true;
    for (int dy = 1; dy <= 4; dy++)
      if (world_solid(L, (int)floorf(x), (int)floorf(y) - dy)) deep = false;
    if (deep) {
      in->jump_pressed = vy < 2.0f, in->jump_held = true;
      in->x = 1.0f;
      return;
    }
  }
  /* a wall in front in mid-air: find the first free row above it; when the air jump can lift the feet that high, use
   * it now (while rising slowly or falling), else keep going and let the wall jump or the next hop deal with it */
  if (!pf->body.on_floor && pf->air_jumps_left > 0 && vy < 5.0f) {
    int ex = (int)floorf(x + 0.9f), ey = (int)floorf(y + 0.3f);
    if (world_solid(L, ex, ey) || world_solid(L, ex, ey + 1)) {
      int r = ey;
      while (r < ey + 8 && (world_solid(L, ex, r) || world_solid(L, ex, r + 1))) r++;
      if ((float)r - y < 3.0f) {
        in->x = 1.0f, in->jump_pressed = true, in->jump_held = true;
        return;
      }
    }
  }
  if (!pf->body.on_floor && pf->wall_lock > 0) in->x = (float)pf->facing;
  /* at a run, take off at the edge: a jump from 2 m back spends 2 m of the jump on ground */
  bool fast = fabsf((float)pf->body.velocity.x) > 8.5f;
  float ax1 = x + 1.0f, ax2 = x + (fast ? 0.75f : 2.2f);
  int fy = (int)floorf(y + 0.05f);
  bool wall_ahead = world_solid(L, (int)floorf(ax1), fy) || world_solid(L, (int)floorf(ax1), fy + 1);
  bool ground2 = false, haz2 = false;
  for (int dy = 0; dy <= 3; dy++) {
    if (world_solid(L, (int)floorf(ax2), fy - dy) || L->t[(fy - dy) < 0 ? 0 : fy - dy][(int)ax2 < WORLD_W ? (int)ax2 : WORLD_W - 1] == T_ONEWAY) {
      ground2 = true;
      break;
    }
  }
  int t1 = tile_at(L, ax1, y + 0.1f), t2 = tile_at(L, ax2, y + 0.1f);
  haz2 = t1 == T_SPIKE || t2 == T_SPIKE || t1 == T_LAVA || t2 == T_LAVA;
  int ft = tile_at(L, ax1, y - 0.5f);
  bool pit = !ground2 || (!fast && ft == T_AIR && !world_solid(L, (int)floorf(ax1), fy - 1) && !world_solid(L, (int)floorf(ax1), fy - 2));
  /* a ledge or ceiling ahead high up: just keep going */
  if (pf->body.on_floor) {
    /* cracked blocks guard side rooms, never the way on: the bots leave them alone (a bot that dashes into a pocket
     * only walks into its far wall) */
    /* a tall wall of cracked rock across the way (a dash gate): dash through it. Short cracks guard side rooms: leave them */
    bool tall_crack = false;
    for (float fw = 0.5f; fw <= 1.61f; fw += 0.55f)
      if (tile_at(L, x + fw, y + 0.6f) == T_CRACK && tile_at(L, x + fw, y + 3.5f) == T_CRACK) tall_crack = true;
    if (tall_crack && (g->abilities & AB_DASH) && pf->dash_cd <= 0) in->dash_pressed = true;
    else if (wall_ahead || haz2 || pit) in->jump_pressed = true, in->jump_held = true;
    /* a high ledge at the cave shaft or a stack of steps: try again if blocked for long */
    if (!in->jump_pressed && x - minx < 0.01f && g->tick % 120 == 0 && wall_ahead) in->jump_pressed = true;
  } else {
    /* in the air over somewhere to land with a gap coming: ease off and land here, as a person does, instead of sailing
     * on into the far wall of the gap */
    if (vy < 3.0f && pf->wall_lock <= 0) {
      bool here = false, ahead = false;
      for (int dy = 0; dy <= 9; dy++) {
        if (world_solid(L, (int)floorf(x), (int)floorf(y) - dy)) here = true;
        if (world_solid(L, (int)floorf(x + 1.8f), (int)floorf(y) - dy)) ahead = true;
      }
      if (here && !ahead) in->x = pf->body.velocity.x > 2.0f ? -0.8f : 0.0f;
    }
    in->jump_held = vy > 0 ? true : false;
    if (vy > 0 && pf->coyote <= 0) in->jump_held = true;
    /* a chasm (not a mere drop): nothing solid for 12 tiles below the feet */
    bool nothing_below = true;
    for (int dy = 0; dy <= 12; dy++)
      if (world_solid(L, (int)floorf(x), (int)floorf(y) - dy)) nothing_below = false;
    if (nothing_below && vy < 1.0f && pf->air_jumps_left > 0 && (g->tick & 3) == 0) in->jump_pressed = true, in->jump_held = true;
    /* dash across what is left of a gap: only when the far side's top is within 4 m below the feet and 5 columns on,
     * so the 2.7 m of the dash is spent where it reaches the ledge (dashing at the top of the arc falls short) */
    if (nothing_below && vy < 0.0f && (g->abilities & AB_DASH) && pf->dash_cd <= 0 && pf->dash_ready) {
      int cx = (int)floorf(x);
      for (int dx = 1; dx <= 5 && !in->dash_pressed; dx++) {
        int top = (int)floorf(y) + 3;
        while (top >= 0 && !world_solid(L, cx + dx, top)) top--;
        if (top >= 0 && (float)(top + 1) > y - 4.0f) in->dash_pressed = true;
      }
    }
    if (nothing_below && pf->air_jumps_left > 0 && vy < 4.0f) in->jump_held = true;
  }
  /* stuck against a wall: hop */
  if (pf->body.on_wall && pf->body.on_floor && (g->tick % 40) == 0) in->jump_pressed = true, in->jump_held = true;
}
