/* The rules. One fixed step: players (platformer controller), moving platforms, hazards, water, enemies, pickups,
 * coins, cracked walls, checkpoints, the shared camera and the bubble rescue. Float + - * / only. */
#include <math.h>
#include <string.h>

#include "game.h"

game G;

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
  d.run_speed = 8.5f;
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
  p->hp = 3;
  p->invuln = 90;
}

void sim_reset(game *g, int players) {
  uint32_t keep_ab = g->abilities;
  world_destroy_physics(g);
  world_generate(&g->L, 7);
  memset(g->p, 0, sizeof g->p);
  g->nplayers = players;
  g->tick = 0;
  g->abilities = keep_ab;
  g->coins_total = g->kills = g->deaths = g->rescues = g->secrets = 0;
  g->won = 0;
  g->win_tick = 0;
  g->checkpoint = -1;
  g->reached = 0;
  g->hitstop = g->hit_flash = g->hitstops = 0;
  g->events = 0;
  g->boss_hp = 9;
  g->hash = 2166136261u;
  memset(g->seen, 0, sizeof g->seen);
  world_build_physics(g);
  for (int i = 0; i < players; i++) {
    g->p[i].joined = true;
    g->p[i].color = i;
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

static void hurt_player(game *g, int i, float kx, int dmg) {
  player *p = &g->p[i];
  if (p->invuln > 0 || p->bubble || !p->alive) return;
  p->hp -= dmg;
  p->invuln = 100;
  p->hurt_fx = 1;
  g->hitstop = 5;
  g->hitstops++;
  g->impacts++;
  g->hit_flash = 8;
  g->events |= EV_HURT;
  p->pf.body.velocity.x = kx * 7.0f;
  p->pf.body.velocity.y = 9.0f;
  p->pf.dash_t = 0;
  if (p->hp <= 0) {
    p->alive = false;
    g->deaths++;
    g->events |= EV_DIE;
  }
}

static void kill_player(game *g, int i) {
  player *p = &g->p[i];
  if (!p->alive || p->bubble) return;
  p->hp = 0;
  p->alive = false;
  g->deaths++;
  g->events |= EV_DIE;
}

static int others_alive(const game *g, int self) {
  int n = 0;
  for (int i = 0; i < g->nplayers; i++)
    if (i != self && g->p[i].alive && !g->p[i].bubble) n++;
  return n;
}

static void enemy_step(game *g, enemy *e, float dt) {
  const level *L = &g->L;
  if (!e->alive) return;
  if (e->hurt > 0) e->hurt--;
  float half = e->kind == 2 ? 1.0f : 0.45f;
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
  float sp = e->kind == 2 ? 3.2f : 1.8f;
  float tx = 0;
  if (e->kind == 2) { /* boss: chases the nearest player inside the arena, leaps now and then */
    float best = 1e9f;
    for (int i = 0; i < g->nplayers; i++) {
      if (!g->p[i].alive || g->p[i].bubble) continue;
      float d = pos_x(&g->p[i]) - e->x;
      if (d * d < best) best = d * d, tx = d;
    }
    if (best < 1e8f) e->dir = tx > 0 ? 1 : -1;
    bool awake = best <= 28 * 28 || g->shake_bug; /* asleep until a player comes near: it neither walks nor leaps */
    if (!awake) sp = 0;
    if (awake && (g->tick % 150) == 0 && e->vy == 0) e->vy = 11.0f;
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
     * one whole tile ABOVE it: it fell a metre, landed hard and was lifted again, every 20 ticks forever, and each
     * landing shook the screen wherever the players were.) */
    if (g->shake_bug) { /* the camera gate's control: the old snap and a shake wherever the camera is */
      ny = floorf(ny) + 1.0f;
      if (e->vy < -2 && e->kind == 2) g->cam.shake = 0.6f;
    } else {
      ny = floorf(ny - 0.01f) + 1.0f;
      if (e->vy < -6 && e->kind == 2) sim_shake(g, e->x, e->y, 0.45f); /* a real leap landing, not a step */
    }
    e->vy = 0;
  }
  e->y = ny;
  (void)h;
}

void sim_shake(game *g, float x, float y, float amount) {
  g->impacts++;
  const camera_rig *c = &g->cam;
  /* the view is about 1.29 z wide and 0.73 z tall (fov 40, 16:9): full shake inside it, fading to none half a view
   * beyond its edge */
  float hx = 0.65f * c->z, hy = 0.37f * c->z;
  float ox = fabsf(x - c->x) - hx, oy = fabsf(y - c->y) - hy;
  float out = fmaxf(ox / hx, oy / hy); /* <= 0 inside the view */
  float k = out <= 0 ? 1.0f : out >= 0.5f ? 0.0f : 1.0f - out * 2.0f;
  float s = fminf(1.0f, g->cam.shake + amount * k);
  if (s > g->cam.shake) g->cam.shake = s;
}

static void camera_step(game *g, float dt) {
  float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
  int n = 0;
  for (int i = 0; i < g->nplayers; i++) {
    const player *p = &g->p[i];
    if (!p->joined || !p->alive || p->bubble) continue;
    float x = pos_x(p), y = pos_y(p);
    minx = fminf(minx, x), maxx = fmaxf(maxx, x), miny = fminf(miny, y), maxy = fmaxf(maxy, y), n++;
  }
  if (n == 0) {
    for (int i = 0; i < g->nplayers; i++)
      if (g->p[i].bubble) {
        minx = maxx = g->p[i].bubble_x, miny = maxy = g->p[i].bubble_y, n = 1;
        break;
      }
  }
  camera_rig *c = &g->cam;
  c->px = c->x, c->py = c->y, c->pz = c->z;
  if (c->shake > 0) c->shake = c->shake > dt * 2.5f ? c->shake - dt * 2.5f : 0;
  const float W[3] = { 7.0f, 6.0f, 4.0f }, AMAX = 90.0f, VMAX = 30.0f;
  float *pos[3] = { &c->x, &c->y, &c->z }, *vel[3] = { &c->vx, &c->vy, &c->vz };
  if (n == 0) { /* nobody to follow (everyone down): glide to a stop, never a dead stop from a fall's speed */
    for (int a = 0; a < 3; a++) {
      float v = *vel[a], dv = AMAX * dt;
      *vel[a] = v > dv ? v - dv : v < -dv ? v + dv : 0;
      *pos[a] += *vel[a] * dt;
    }
    return;
  }
  float spanx = maxx - minx, spany = maxy - miny;
  c->tx = (minx + maxx) * 0.5f;
  c->ty = (miny + maxy) * 0.5f + 2.2f;
  /* the view is about 1.29 * z wide at this fov and aspect: keep every player inside with a margin */
  float need = fmaxf(spanx / 1.0f + 12.0f, (spany + 6.0f) * 1.8f);
  float z = need * 0.78f;
  c->tz = z < 17 ? 17 : z > 34 ? 34 : z;
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

void sim_step(game *g, const input_state in[MAX_PLAYERS], float dt) {
  level *L = &g->L;
  g->events = 0;
  g->tick++;
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
  for (int i = 0; i < g->nplayers; i++) {
    player *p = &g->p[i];
    if (!p->joined) continue;
    p->land_fx = p->jump_fx = 0;
    if (p->invuln > 0) p->invuln--;
    if (p->bubble) continue;
    if (!p->alive) {
      /* a fallen player: carried in a bubble when someone is left, else back to the checkpoint */
      if (others_alive(g, i) > 0) {
        p->bubble = 1;
        p->bubble_x = pos_x(p), p->bubble_y = pos_y(p) + 1.0f;
        p->rescue_timer = 0;
        g->events |= EV_BUBBLE;
      } else {
        p->rescue_timer++;
        if (p->rescue_timer > 45) {
          bool all_dead = true;
          for (int k = 0; k < g->nplayers; k++)
            if (g->p[k].joined && g->p[k].alive) all_dead = false;
          if (all_dead) {
            for (int k = 0; k < g->nplayers; k++)
              if (g->p[k].joined) sim_respawn_player(g, k);
          }
        }
      }
      continue;
    }
    cw_platformer2d *pf = &p->pf;
    p->retry_t = in[i].retry ? p->retry_t + 1 : 0;
    if (p->retry_t >= 60) {
      p->retry_t = 0;
      sim_return(g);
      return;
    }
    float cx = pos_x(p), cy = pos_y(p) + 0.7f;
    int tc = tile_at(L, cx, cy);
    bool in_water = tc == T_WATER;
    pf->submerged = in_water ? (tile_at(L, cx, cy + 0.9f) == T_WATER ? 1.0f : 0.7f) : 0.0f;
    pf->current = (cw_vec2){ in_water ? L->water_current[(int)floorf(cx)] : 0.0f, 0 };
    pf->can_dash = (g->abilities & AB_DASH) != 0;
    pf->can_wall_jump = (g->abilities & AB_CLAWS) != 0;
    pf->max_air_jumps = (g->abilities & AB_DOUBLE) ? 1 : 0;
    const input_state *is = &in[i];
    cw_platformer2d_input pin = { .move_x = is->x, .move_y = is->y, .jump_pressed = is->jump_pressed, .jump_held = is->jump_held,
                                  .dash_pressed = is->dash_pressed, .down_held = is->down };
    cw_platformer2d_step(pf, &pin, dt);
    uint32_t e = pf->events;
    if (e & (CW_PF_JUMP | CW_PF_AIR_JUMP)) g->events |= EV_JUMP, p->jump_fx = 1;
    if (e & CW_PF_WALL_JUMP) g->events |= EV_WALL, p->jump_fx = 2;
    if (e & CW_PF_LAND) g->events |= EV_LAND, p->land_fx = 1;
    if (e & CW_PF_DASH) g->events |= EV_DASH;
    if (e & (CW_PF_ENTER_WATER | CW_PF_LEAVE_WATER)) g->events |= EV_SPLASH, p->land_fx = 2;
    /* a dash breaks a cracked wall in its way */
    if (pf->dash_t > 0) {
      float fx = pos_x(p) + (float)pf->facing * 0.9f;
      for (int dy = 0; dy < 3; dy++)
        if (tile_at(L, fx, pos_y(p) + 0.4f + (float)dy * 0.9f) == T_CRACK) {
          int tx = (int)floorf(fx);
          for (int y = 0; y < WORLD_H; y++)
            if (L->t[y][tx] == T_CRACK && world_break_crack(g, tx, y)) world_hide_crack(tx, y), g->events |= EV_BREAK;
        }
    }
    /* animation state */
    if (in_water) p->anim = 5;
    else if (pf->dash_t > 0) p->anim = 6;
    else if (pf->body.on_floor) p->anim = (is->x > 0.1f || is->x < -0.1f) ? 1 : 0;
    else if (pf->body.on_wall) p->anim = 4;
    else p->anim = pf->body.velocity.y > 0 ? 2 : 3;
    /* hazards */
    float fx = pos_x(p), fy = pos_y(p);
    int tf = tile_at(L, fx, fy + 0.15f), tb = tile_at(L, fx, fy + 0.9f);
    if (tf == T_SPIKE || tb == T_SPIKE) hurt_player(g, i, pf->body.velocity.x >= 0 ? -1.0f : 1.0f, 1);
    if (tile_at(L, fx, fy + 0.2f) == T_LAVA || tile_at(L, fx, fy) == T_LAVA) kill_player(g, i);
    if (fy < 2.2f) kill_player(g, i);
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
      float dx = fx - ((float)pk->x + 0.5f), dy = fy - (float)pk->y;
      if (fabsf(dx) < 1.0f && dy > -0.8f && dy < 2.6f) {
        pk->taken = true;
        g->abilities |= (uint32_t)pk->ability;
        g->events |= EV_POWER;
      }
    }
    for (int k = 0; k < L->ncoins; k++) {
      coin *c = &L->coins[k];
      if (c->taken) continue;
      float dx = fx - c->x, dy = fy + 0.7f - c->y;
      if (dx * dx + dy * dy < 0.9f) {
        c->taken = true;
        g->coins_total++;
        p->coins++;
        if (c->secret) g->secrets++;
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
  /* enemies and contact */
  for (int k = 0; k < L->nenemies; k++) {
    enemy *e = &L->enemies[k];
    if (!e->alive) continue;
    enemy_step(g, e, dt);
    float eh = e->kind == 2 ? 2.0f : 0.9f, ew = e->kind == 2 ? 1.1f : 0.5f;
    for (int i = 0; i < g->nplayers; i++) {
      player *p = &g->p[i];
      if (!p->joined || !p->alive || p->bubble) continue;
      float px = pos_x(p), py = pos_y(p);
      float dx = px - e->x;
      if (dx < -(ew + 0.3f) || dx > ew + 0.3f) continue;
      if (py > e->y + eh + 0.2f || py + 1.5f < e->y) continue;
      bool stomp = p->pf.body.velocity.y < -1.0f && py > e->y + eh * 0.55f;
      if (stomp && e->hurt == 0) {
        e->hp--;
        e->hurt = 40;
        p->pf.body.velocity.y = 12.0f;
        p->pf.air_jumps_left = p->pf.max_air_jumps;
        g->events |= EV_STOMP;
        g->hitstop = e->hp <= 0 ? 6 : 4;
        g->hitstops++;
        g->hit_flash = 5;
        if (e->kind == 2) g->boss_hp = e->hp, sim_shake(g, e->x, e->y + 1.0f, e->hp <= 0 ? 0.8f : 0.5f);
        else if (e->hp <= 0) sim_shake(g, e->x, e->y + 0.5f, 0.25f); /* the blast of a small enemy */
        if (e->hp <= 0) {
          e->alive = false;
          g->kills++;
          if (e->kind == 2 && !g->won) g->won = 1, g->win_tick = g->tick, g->events |= EV_WIN;
        }
      } else if (e->hurt == 0 || e->kind != 2) {
        hurt_player(g, i, dx >= 0 ? 1.0f : -1.0f, 1);
      }
    }
  }
  /* bubbles: drift toward the group, pop on contact */
  float gx = 0, gy = 0;
  int ng = 0;
  float lead = -1e9f;
  for (int i = 0; i < g->nplayers; i++) {
    const player *p = &g->p[i];
    if (p->joined && p->alive && !p->bubble) {
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
    /* falling too far behind the group: off screen for 2.5 s becomes a bubble */
    if (p->alive && !p->bubble && ng > 1) {
      float dx = pos_x(p) - gx, dy = pos_y(p) - gy;
      if (dx > 24 || dx < -24 || dy > 15 || dy < -15) p->off_screen++;
      else p->off_screen = 0;
      if (p->off_screen > 150) {
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
      for (int dy = 1; dy <= 2; dy++) { /* beside the body, above the ground */
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
  float ax1 = x + 1.0f, ax2 = x + 2.2f;
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
  bool pit = !ground2 || (ft == T_AIR && !world_solid(L, (int)floorf(ax1), fy - 1) && !world_solid(L, (int)floorf(ax1), fy - 2));
  /* a ledge or ceiling ahead high up: just keep going */
  if (pf->body.on_floor) {
    /* cracked blocks guard side rooms, never the way on: the bots leave them alone (a bot that dashes into a pocket
     * only walks into its far wall) */
    /* a tall wall of cracked rock across the way (a dash gate): dash through it. Short cracks guard side rooms: leave them */
    bool tall_crack = tile_at(L, x + 1.6f, y + 0.6f) == T_CRACK && tile_at(L, x + 1.6f, y + 6.5f) == T_CRACK && tile_at(L, x + 1.6f, y + 12.5f) == T_CRACK;
    if (tall_crack && (g->abilities & AB_DASH) && pf->dash_cd <= 0) in->dash_pressed = true;
    else if (wall_ahead || haz2 || pit) in->jump_pressed = true, in->jump_held = true;
    /* a high ledge at the cave shaft or a stack of steps: try again if blocked for long */
    if (!in->jump_pressed && x - minx < 0.01f && g->tick % 120 == 0 && wall_ahead) in->jump_pressed = true;
  } else {
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
