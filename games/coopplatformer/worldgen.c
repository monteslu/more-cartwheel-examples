/* The level: a connected world of four biomes laid left to right, with ability gates (a gap that needs the air
 * jump, a shaft that needs wall jumps, a lava span that needs the dash), a hidden room behind a cracked wall, a
 * pool to swim through, a moving platform, checkpoints and a boss arena. Generated from a seed with integer maths
 * only, so every host builds the same world. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/base.h"
#include "game.h"

struct gen {
  level *L;
  uint32_t s;
  int x;     /* the cursor: next free column */
  int g;     /* surface height of the path at the cursor */
};

static uint32_t rnd(gen *G, int n) {
  G->s = G->s * 1664525u + 1013904223u;
  return (G->s >> 8) % (uint32_t)n;
}
static bool in_range(int x, int y) { return x >= 0 && x < WORLD_W && y >= 0 && y < WORLD_H; }
static void set(level *L, int x, int y, int t) {
  if (in_range(x, y)) L->t[y][x] = (uint8_t)t;
}
bool g_level_old; /* COOP_REACH_OLD: the reachability gate's control, the level as it was before C-PT4 */
bool world_solid(const level *L, int x, int y) {
  if (x < 0 || x >= WORLD_W) return true;
  if (y < 0) return true;
  if (y >= WORLD_H) return false;
  int t = L->t[y][x];
  return t == T_ROCK || t == T_GRASS || t == T_CRACK || t == T_BRICK || t == T_ICE || t == T_BELT || t == T_CRUMBLE;
}

static void column(gen *G, int x, int g, int biome) {
  level *L = G->L;
  if (x < 0 || x >= WORLD_W) return;
  L->ground[x] = g;
  L->biome[x] = (uint8_t)biome;
  for (int y = 0; y < g; y++) set(L, x, y, y == g - 1 && biome != BIOME_CAVES ? (biome == BIOME_MEADOW ? T_GRASS : T_ROCK) : (biome == BIOME_RUINS ? T_BRICK : T_ROCK));
}
static void flat(gen *G, int n, int biome) {
  for (int i = 0; i < n; i++) column(G, G->x++, G->g, biome);
}
static void rise(gen *G, int dy, int n, int biome) {
  G->g += dy;
  flat(G, n, biome);
}
static void coin_row(gen *G, int x0, int n, int y) {
  level *L = G->L;
  for (int i = 0; i < n && L->ncoins < MAX_COINS; i++) L->coins[L->ncoins++] = (coin){ (float)(x0 + i) + 0.5f, (float)y + 0.5f, false, false };
}
/* coins along the arc of a jump over the next n columns, y the height at the ends */
static void coin_arc(gen *G, int x0, int n, int y) {
  level *L = G->L;
  for (int i = 0; i < n && L->ncoins < MAX_COINS; i++) {
    int up = i * (n - 1 - i) * 8 / ((n - 1) * (n - 1)); /* 0 at the ends, 2 in the middle */
    L->coins[L->ncoins++] = (coin){ (float)(x0 + i) + 0.5f, (float)(y + up) + 0.5f, false, false };
  }
}
/* a gap of n columns: nothing below, the bottom filled with `hazard` (T_AIR = a plain pit) */
static void gap(gen *G, int n, int biome, int hazard) {
  for (int i = 0; i < n; i++) {
    int x = G->x++;
    G->L->biome[x] = (uint8_t)biome;
    G->L->ground[x] = 0;
    for (int y = 0; y < 3; y++) set(G->L, x, y, hazard == T_LAVA ? T_LAVA : (y == 0 ? T_ROCK : T_AIR));
    if (hazard == T_SPIKE) set(G->L, x, 1, T_SPIKE);
  }
}
/* a mound three tiles high with a pocket in its foot (3 tiles tall inside), a cracked wall in front of it and 'air'
 * columns of room: 4 or 3 secret coins inside. The way in is a wooden hatch in its roof, there from the moment the room is
 * seen: Down + Jump drops through it, and a jump and an air jump come back up. It was sealed, the cracked wall the only way
 * in, and the dash that breaks one is found much later: people standing on the roof over the coins could not reach them.
 * The wall still breaks to a dash, a way out and a shortcut. */
static void pocket(gen *G, int air, int tail, int biome) {
  level *L = G->L;
  int mx = G->x, mg = G->g, n = 1 + air + tail;
  for (int i = 0; i < n; i++) {
    int x = G->x++;
    column(G, x, mg, biome);
    for (int y = mg - 1; y <= mg + 1; y++) set(L, x, y, i == 0 ? T_CRACK : (i <= air ? T_AIR : T_ROCK));
    set(L, x, mg + 2, T_ROCK);
    if (!g_level_old && i >= 2 && i <= 3) set(L, x, mg + 2, T_ONEWAY); /* the hatch, over the middle of the room */
    L->ground[x] = mg + 3;
  }
  G->g = mg + 3;
  coin_row(G, mx + 1, air, mg);
  for (int i = 0; i < air; i++) L->coins[L->ncoins - 1 - i].secret = true;
}
static void gate_begin(gen *G, int need, int *x0) {
  *x0 = G->x - 1;
  (void)need;
}
static void gate_end(gen *G, int need, int x0) {
  level *L = G->L;
  if (L->ngates < 8) L->gates[L->ngates++] = (gate){ x0, G->x, need, L->nchecks - 1 };
}
static void spikes(gen *G, int n, int biome) {
  for (int i = 0; i < n; i++) {
    int x = G->x;
    column(G, G->x++, G->g, biome);
    set(G->L, x, G->g, T_SPIKE);
  }
}
static void oneway(gen *G, int x, int y, int n) {
  for (int i = 0; i < n; i++) set(G->L, x + i, y, T_ONEWAY);
}
static void walker(gen *G, int x, int y, int range, int kind) {
  level *L = G->L;
  if (L->nenemies >= MAX_ENEMIES) return;
  enemy *e = &L->enemies[L->nenemies++];
  *e = (enemy){ .kind = kind, .x = (float)x + 0.5f, .y = (float)y, .home_x = (float)x + 0.5f, .range = (float)range, .hp = kind == 2 ? 9 : 1, .dir = -1, .alive = true };
}
static void check(gen *G) {
  level *L = G->L;
  if (L->nchecks < MAX_CHECKS) L->checks[L->nchecks++] = (checkpoint){ G->x - 1, G->g };
}
static void pedestal(gen *G, int ability) {
  level *L = G->L;
  if (L->npickups < MAX_PICKUPS) L->pickups[L->npickups++] = (pickup){ G->x - 2, G->g, ability, false };
  /* an arch over the pedestal: the way on goes under it, so every player of the group (and every bot) touches it, and
   * an ability is never missed. It is a tall lintel (7 tiles), not a thin one: a group that came down from higher
   * ground used to run over a 2-tile arch and drop past the claws, then stall at the claws shaft */
  for (int x = G->x - 5; x < G->x - 1; x++)
    for (int y = G->g + 3; y < G->g + 10; y++) set(L, x, y, T_ROCK);
}
/* a solid block of rock from (x, y0) to (x + w, y1) */
static void block(level *L, int x, int y0, int w, int y1, int t) {
  for (int xx = x; xx < x + w; xx++)
    for (int y = y0; y < y1; y++) set(L, xx, y, t);
}
/* a cave ceiling over the next n columns, `clear` tiles above the surface */
static void ceiling(gen *G, int x0, int n, int clear) {
  for (int x = x0; x < x0 + n && x < WORLD_W; x++) {
    int g = 0; /* the highest floor within 6 columns: a shaft or a gap does not lower the roof */
    for (int k = x - 6; k <= x + 6; k++)
      if (k >= 0 && k < WORLD_W && G->L->ground[k] > g && G->L->ground[k] < WORLD_H) g = G->L->ground[k];
    for (int y = g + clear; y < WORLD_H; y++)
      if (G->L->t[y][x] == T_AIR) set(G->L, x, y, T_ROCK);
  }
}

/* ── the pieces new levels are written with ── */
static void begin(gen *G, int biome, int height) {
  G->x = 0, G->g = height;
  flat(G, 8, biome);
  G->L->start_x = 5, G->L->start_y = G->g;
}
static thing *add_thing(level *L, int kind, float x, float y) {
  if (L->nthings >= MAX_THINGS) return NULL;
  thing *t = &L->things[L->nthings++];
  memset(t, 0, sizeof *t);
  t->kind = kind, t->x = x, t->y = y;
  return t;
}
static void sign_at(gen *G, int x, const char *text, bool secret) {
  level *L = G->L;
  if (L->nsigns < MAX_SIGNS) L->signs[L->nsigns++] = (sign_t){ (float)x + 0.5f, (float)L->ground[x < 0 ? 0 : x], text, secret };
}
static void sign(gen *G, const char *text) { sign_at(G, G->x - 2, text, false); }
/* a sign inside the room just made: no ground number, the floor is what's under the room */
static void secret_sign(gen *G, const char *text) {
  level *L = G->L;
  int x = G->x - 3, y = L->ground[x];
  for (int k = 0; k < 14; k++) {
    int xk = G->x - 1 - k;
    if (xk > 0 && !world_solid(L, xk, y - 2)) { /* the air room below the roof: find its floor */
      int fy = y - 2;
      while (fy > 0 && !world_solid(L, xk, fy - 1)) fy--;
      if (L->nsigns < MAX_SIGNS) L->signs[L->nsigns++] = (sign_t){ (float)xk + 0.5f, (float)fy, text, true };
      return;
    }
  }
  sign_at(G, x, text, true);
}
static void secret_coins(gen *G, int x0, int n, int y) {
  coin_row(G, x0, n, y);
  for (int i = 0; i < n; i++) G->L->coins[G->L->ncoins - 1 - i].secret = true;
}
static void gate_mark(gen *G, int need) { (void)G, (void)need; }
static void spring(gen *G, int x) {
  thing *t = add_thing(G->L, TH_SPRING, (float)x + 0.5f, (float)G->g);
  if (t) t->w = 0.9f;
}
static void fan(gen *G, int x, int w, int h) {
  thing *t = add_thing(G->L, TH_FAN, (float)x + 0.5f * (float)w, (float)G->g);
  if (t) t->w = (float)w * 0.5f, t->h = (float)h;
}
/* a pit with a fan in the bottom of it: fall in and be carried up and over */
static void fan_gap(gen *G, int n, int h) {
  int biome = G->L->biome[G->x - 1], g = G->g, x0 = G->x;
  gap(G, n, biome, T_AIR);
  for (int x = x0; x < x0 + n; x++) for (int y = 0; y < 4; y++) set(G->L, x, y, T_ROCK);
  thing *t = add_thing(G->L, TH_FAN, (float)x0 + 0.5f * (float)n, 4.0f);
  if (t) t->w = (float)n * 0.5f, t->h = (float)(g + h) - 4.0f;
}
static void saw(gen *G, int x, int y, int range, float speed) {
  /* her rail stands on two posts on the ground: fit it to the flat that is really there */
  level *L = G->L;
  while (x + range + 1 >= G->x && range > 1) range--;
  while (x + range + 1 >= G->x) x--;
  bool ok = false;
  while (!ok) {
    ok = true;
    for (int e = -1; e <= 1; e += 2) {
      int px = x + e * (range + 1);
      if (px < 1 || !world_solid(L, px, G->g - 1) || world_solid(L, px, G->g)) ok = false;
    }
    if (!ok) {
      if (range > 1) range--;
      else return; /* no room for her here */
    }
  }
  thing *t = add_thing(L, TH_SAW, (float)x + 0.5f, (float)y + 0.3f);
  if (t) t->a = (float)range, t->b = speed, t->c = (float)G->L->nthings * 0.37f, t->h = (float)G->g;
}
static void press(gen *G, int x, int period, int offset) {
  thing *t = add_thing(G->L, TH_PRESS, (float)x + 0.5f, (float)G->g);
  if (t) t->a = (float)period, t->b = (float)offset, t->w = 1.5f, t->h = 6.0f;
}
static void faller(gen *G, int x) {
  thing *t = add_thing(G->L, TH_FALLER, (float)x + 0.5f, (float)G->g);
  if (t) t->w = 1.1f, t->h = 13.0f;
}
static void cannon(gen *G, int x, float vx, float vy) {
  thing *t = add_thing(G->L, TH_CANNON, (float)x + 0.5f, (float)G->g);
  if (t) t->a = vx * 1.6f, t->b = vy, t->w = 0.9f;
}
static void plat(gen *G, int n, float w, float speed, int biome) {
  level *L = G->L;
  gap(G, n, biome, T_SPIKE);
  if (L->nplat < MAX_PLATFORMS) L->plats[L->nplat++] = (mplat){ (float)(G->x - n) + w * 0.5f, (float)G->g - 0.5f, (float)G->x - w * 0.5f, (float)G->g - 0.5f, w, 0, speed };
}
/* ice over the next n columns */
static void ice(gen *G, int n, int biome) {
  for (int i = 0; i < n; i++) {
    int x = G->x;
    column(G, G->x++, G->g, biome);
    set(G->L, x, G->g - 1, T_ICE);
  }
}
static void belt(gen *G, int n, float speed, int biome) {
  for (int i = 0; i < n; i++) {
    int x = G->x;
    column(G, G->x++, G->g, biome);
    set(G->L, x, G->g - 1, T_BELT);
    G->L->belt[x] = speed;
  }
}
/* a span over spikes, one tile of crumbling plank a step, that drops some time after someone stands on it */
static void crumble_bridge(gen *G, int n) {
  level *L = G->L;
  int biome = L->biome[G->x - 1], x0 = G->x;
  gap(G, n, biome, T_SPIKE);
  for (int x = x0; x < x0 + n; x++) {
    set(L, x, G->g - 1, T_CRUMBLE);
    L->ground[x] = G->g;
    thing *t = add_thing(L, TH_CRUMBLE, (float)x, (float)G->g - 1.0f);
    (void)t;
  }
}
/* single crumbling steps going up (dir 1) or down (dir -1) across a gap */
static void crumble_steps(gen *G, int n, int dir) {
  level *L = G->L;
  int biome = L->biome[G->x - 1], x0 = G->x;
  gap(G, n * 3, biome, T_SPIKE);
  for (int i = 0; i < n; i++) {
    int y = G->g - 1 + dir * (i + 1), x = x0 + i * 3 + 1;
    set(L, x, y, T_CRUMBLE);
    L->ground[x] = y + 1;
    add_thing(L, TH_CRUMBLE, (float)x, (float)y);
  }
  G->g += dir * n;
}
/* a wall to climb: a low door in the near wall, a well, a tall far wall, a platform on top */
static void shaft(gen *G, int w, int h, int biome) {
  level *L = G->L;
  int sx = G->x, base = G->g;
  flat(G, 1, biome);
  block(L, sx, base, 1, base + h + 2, T_ROCK);
  for (int y = base; y < base + 3; y++) set(L, sx, y, T_AIR);
  L->ground[sx] = base + h + 2;
  for (int i = 0; i < w; i++) column(G, G->x++, base - 2, biome);
  int rx = G->x;
  column(G, G->x++, base, biome);
  block(L, rx, base, 1, base + h, T_ROCK);
  L->ground[rx] = base + h;
  G->g = base + h;
  for (int i = 0; i < 4; i++) column(G, G->x++, base + h, biome);
  coin_row(G, sx + 1, w, base + h / 2 + 1);
}
/* a pool: walls either side, a current, 'well' columns of secret well under its floor */
static void pool(gen *G, int len, int depth, float current, int well) {
  level *L = G->L;
  int surf = G->g, x0 = G->x;
  for (int i = 0; i < len; i++) {
    int x = G->x++;
    L->biome[x] = BIOME_RUINS;
    int floor = surf - depth + (i < 2 ? (2 - i) * 2 : 0) + (i > len - 3 ? (i - (len - 3)) * 2 : 0);
    L->ground[x] = floor;
    for (int y = 0; y < floor; y++) set(L, x, y, T_BRICK);
    for (int y = floor; y < surf; y++) set(L, x, y, T_WATER);
    L->water_current[x] = (i > len / 3 && i < len * 2 / 3) ? current : 0.0f;
  }
  coin_row(G, x0 + 3, len - 6, surf - depth + 2);
  oneway(G, x0 + len / 2 - 1, surf - 2, 3);
  if (well > 0) {
    int wx = x0 + 4;
    for (int i = 0; i < well; i++)
      for (int y = L->ground[wx + i] - 3; y < L->ground[wx + i]; y++) set(L, wx + i, y, T_WATER);
    secret_coins(G, wx, well, L->ground[wx] - 2);
  }
}
/* a plate, a run of ground, a door in a wall that goes to the top of the world: stand on the plate and the door stays open 'ticks' more */
static void plate_door(gen *G, int ticks, int dist) {
  level *L = G->L;
  int biome = L->biome[G->x - 1];
  int px = G->x + 1;
  flat(G, 3 + dist, biome);
  int dx = G->x;
  flat(G, 2, biome);
  for (int x = dx; x < dx + 2; x++)
    for (int y = G->g + 4; y < WORLD_H; y++) set(L, x, y, biome == BIOME_RUINS ? T_BRICK : T_ROCK);
  thing *door = add_thing(L, TH_DOOR, (float)dx, (float)G->g);
  if (door) door->w = 2, door->h = 4;
  thing *plate = add_thing(L, TH_PLATE, (float)px + 0.5f, (float)G->g);
  if (plate) plate->w = 1.0f, plate->a = (float)ticks, plate->link = L->nthings - 2;
  flat(G, 12, biome); /* a run past the door before whatever comes next */
}
/* a wall of cracked rock and a tall top of solid rock across the way: only a dash breaks it */
static void crack_wall(gen *G) {
  level *L = G->L;
  int biome = L->biome[G->x - 1], wx = G->x;
  flat(G, 1, biome);
  for (int y = G->g; y < G->g + 4; y++) set(L, wx, y, T_CRACK);
  for (int y = G->g + 4; y < WORLD_H; y++) set(L, wx, y, biome == BIOME_RUINS ? T_BRICK : T_ROCK);
}
static void goal_booth(gen *G) {
  level *L = G->L;
  L->goal_x = G->x - 2;
  add_thing(L, TH_BOOTH, (float)G->x - 4.0f, (float)G->g);
}
static void close_level(gen *G, int biome) {
  level *L = G->L;
  if (G->x + 3 >= WORLD_W) cw_error("coopplatformer: level too wide: %d columns", G->x);
  L->width = G->x;
  block(L, G->x, 0, 3, WORLD_H, T_ROCK);
  for (int x = G->x; x < G->x + 3 && x < WORLD_W; x++) L->ground[x] = WORLD_H;
  for (int x = G->x + 3; x < WORLD_W; x++) {
    L->biome[x] = (uint8_t)biome;
    L->ground[x] = WORLD_H;
    for (int y = 0; y < WORLD_H; y++) set(L, x, y, T_ROCK);
  }
  block(L, 0, 0, 1, WORLD_H, T_ROCK);
  for (int x = 0; x < WORLD_W; x++)
    if (L->t[0][x] == T_AIR) L->t[0][x] = T_ROCK;
}

/* the level that was the whole game until the worlds came, kept at the end until the last world replaces it */
static void l_grand_tour(gen *G) {
  level *L = G->L;
  G->g = 18;
  /* ── meadow: sun, grass, the first jumps; the air jump pedestal ── */
  flat(G, 14, BIOME_MEADOW);
  L->start_x = 4, L->start_y = G->g;
  coin_row(G, 7, 5, G->g + 1);
  rise(G, 2, 5, BIOME_MEADOW);
  flat(G, 3, BIOME_MEADOW);
  coin_row(G, 20, 4, G->g + 3);
  gap(G, 4, BIOME_MEADOW, T_SPIKE);
  oneway(G, G->x - 3, G->g + 2, 2);
  flat(G, 5, BIOME_MEADOW);
  walker(G, G->x - 3, G->g, 3, 0);
  spikes(G, 2, BIOME_MEADOW);
  flat(G, 4, BIOME_MEADOW);
  check(G);
  /* the first running jump: a long straight, then a gap a standing jump cannot clear. Coins arc over it to show the way */
  rise(G, -1, 13, BIOME_MEADOW);
  coin_arc(G, G->x, 7, G->g + 2);
  gap(G, 7, BIOME_MEADOW, T_SPIKE);
  flat(G, 7, BIOME_MEADOW);
  pedestal(G, AB_DOUBLE);
  coin_row(G, G->x - 5, 3, G->g + 2);
  flat(G, 3, BIOME_MEADOW);
  int gx0;
  gate_begin(G, AB_DOUBLE, &gx0);
  flat(G, 1, BIOME_MEADOW);
  rise(G, 5, 1, BIOME_MEADOW);                    /* the air jump gate: a cliff higher than a running jump, however long the run */
  gate_end(G, AB_DOUBLE, gx0);
  flat(G, 6, BIOME_MEADOW);
  walker(G, G->x - 3, G->g, 3, 0);
  flat(G, 4, BIOME_MEADOW);
  /* a hidden room: stand on the high ledge reached with the air jump */
  oneway(G, G->x - 8, G->g + 4, 3);
  oneway(G, G->x - 4, G->g + 7, 3);
  coin_row(G, G->x - 4, 3, G->g + 8);
  for (int i = 0; i < 3 && L->ncoins; i++) L->coins[L->ncoins - 1 - i].secret = true;
  rise(G, 2, 4, BIOME_MEADOW);
  /* a sealed vault in the hillside: only a dash breaks its cracked wall, and the dash is found much later, so
   * this is somewhere to come back to (the map's flag warp makes the trip short) */
  pocket(G, 4, 3, BIOME_MEADOW);
  rise(G, -3, 3, BIOME_MEADOW);
  /* ── caves: dark, torchlit; claws; a shaft; a cracked wall ── */
  int cave0 = G->x;
  rise(G, 0, 6, BIOME_CAVES);
  check(G);
  pedestal(G, AB_CLAWS);
  flat(G, 6, BIOME_CAVES);
  walker(G, G->x - 3, G->g, 3, 0);
  gap(G, 3, BIOME_CAVES, T_SPIKE);
  flat(G, 5, BIOME_CAVES);
  spikes(G, 3, BIOME_CAVES);
  flat(G, 3, BIOME_CAVES);
  {
    /* the shaft: two walls 3 apart, 6 and 9 tall; wall jump up the inside to the ledge on top */
    int sx = G->x;
    int base = G->g;
    int sgx0;
    gate_begin(G, AB_CLAWS, &sgx0);
    flat(G, 1, BIOME_CAVES);                       /* left wall base */
    block(L, sx, base, 1, base + 10, T_ROCK);
    L->ground[sx] = base + 10;
    for (int i = 0; i < 3; i++) column(G, G->x++, base - 2, BIOME_CAVES); /* the well floor, 2 lower */
    int rx = G->x;
    column(G, G->x++, base, BIOME_CAVES);
    block(L, rx, base, 1, base + 8, T_ROCK);
    L->ground[rx] = base + 8;
    G->g = base + 8;
    for (int i = 0; i < 4; i++) column(G, G->x++, base + 8, BIOME_CAVES);
    gate_end(G, AB_CLAWS, sgx0);
    /* open the way in from the left under the left wall: a low door so the shaft can be entered */
    for (int y = base; y < base + 3; y++) set(L, sx, y, T_AIR);
    for (int y = base + 3; y < base + 10; y++) set(L, sx, y, T_ROCK);
    /* in the well, not in its far wall: the fourth coin used to sit inside the rock */
    coin_row(G, sx + 1, g_level_old ? 4 : 3, base + 5);
  }
  walker(G, G->x - 3, G->g, 3, 0);
  flat(G, 6, BIOME_CAVES);
  check(G);
  /* a mound with a sealed pocket in its foot: a dash into the cracked wall opens it (the path goes over the top) */
  pocket(G, 3, 3, BIOME_CAVES);
  rise(G, -4, 3, BIOME_CAVES);
  rise(G, -3, 6, BIOME_CAVES);
  walker(G, G->x - 3, G->g, 3, 0);
  flat(G, 4, BIOME_CAVES);
  ceiling(G, cave0, G->x - cave0, 8);
  /* ── ruins: old brick, a flooded hall; swim; a current; a dash pedestal ── */
  int ruin0 = G->x;
  flat(G, 6, BIOME_RUINS);
  check(G);
  {
    int surf = G->g;
    int depth = 6, len = 18;
    for (int i = 0; i < len; i++) {
      int x = G->x++;
      L->biome[x] = BIOME_RUINS;
      int floor = surf - depth + (i < 2 ? (2 - i) * 2 : 0) + (i > len - 3 ? (i - (len - 3)) * 2 : 0);
      L->ground[x] = floor;
      for (int y = 0; y < floor; y++) set(L, x, y, T_BRICK);
      for (int y = floor; y < surf; y++) set(L, x, y, T_WATER);
      L->water_current[x] = (i > 6 && i < 12) ? 3.0f : 0.0f;
    }
    /* ledges in the pool and a sunken coin trail */
    coin_row(G, G->x - len + 3, 12, surf - depth + 3);
    oneway(G, G->x - 9, surf - 2, 3);
    /* a hidden well under the pool floor: swim down into it for a secret handful of coins */
    int wx = G->x - len + 4; /* clear of the ledge (x - 9) and the current (columns 7 to 11) */
    for (int i = 0; i < 3; i++)
      for (int y = L->ground[wx + i] - 3; y < L->ground[wx + i]; y++) set(L, wx + i, y, T_WATER);
    coin_row(G, wx, 3, L->ground[wx] - 2);
    for (int i = 0; i < 3; i++) L->coins[L->ncoins - 1 - i].secret = true;
  }
  flat(G, 4, BIOME_RUINS);
  walker(G, G->x - 2, G->g, 2, 0);
  gap(G, 7, BIOME_RUINS, T_SPIKE);                 /* crossable with a moving platform or the air jump */
  L->plats[L->nplat++] = (mplat){ (float)(G->x - 7), (float)G->g - 0.5f, (float)(G->x - 2), (float)G->g - 0.5f, 2.2f, 0, 0.35f };
  flat(G, 5, BIOME_RUINS);
  check(G);
  rise(G, 2, 4, BIOME_RUINS);
  rise(G, 2, 6, BIOME_RUINS);
  pedestal(G, AB_DASH);
  walker(G, G->x - 3, G->g, 3, 1);
  flat(G, 3, BIOME_RUINS);
  (void)ruin0;
  /* ── volcano: lava spans (dash and air jump), embers, the boss ── */
  rise(G, -3, 6, BIOME_VOLCANO);
  check(G);
  walker(G, G->x - 3, G->g, 3, 0);
  gap(G, 9, BIOME_VOLCANO, T_LAVA);                /* a lava span: jump and air jump */
  flat(G, 5, BIOME_VOLCANO);
  spikes(G, 3, BIOME_VOLCANO);
  flat(G, 3, BIOME_VOLCANO);
  check(G);
  {
    /* the dash gate: a wall of cracked rock from the floor to the top of the world, across the way; only a dash breaks it */
    int dgx0;
    gate_begin(G, AB_DASH, &dgx0);
    flat(G, 3, BIOME_VOLCANO);
    int wx = G->x;
    flat(G, 1, BIOME_VOLCANO);
    for (int y = G->g; y < WORLD_H; y++) set(L, wx, y, T_CRACK); /* to the top of the world: wall jumps cannot climb it */
    flat(G, 3, BIOME_VOLCANO);
    gate_end(G, AB_DASH, dgx0);
  }
  rise(G, 2, 3, BIOME_VOLCANO);
  rise(G, 2, 3, BIOME_VOLCANO);
  check(G);
  gap(G, 6, BIOME_VOLCANO, T_LAVA);
  oneway(G, G->x - 4, G->g + 1, 2);
  flat(G, 4, BIOME_VOLCANO);
  walker(G, G->x - 3, G->g, 3, 1);
  rise(G, -4, 6, BIOME_VOLCANO);
  check(G);
  /* the arena: flat, walled in, the boss at the far end */
  flat(G, 4, BIOME_VOLCANO);
  L->boss_x = G->x + 10;
  flat(G, 20, BIOME_VOLCANO);
  walker(G, L->boss_x, G->g, 8, 2);
  L->goal_x = G->x - 3;
  block(L, G->x, 0, 3, WORLD_H, T_ROCK);
  for (int x = G->x; x < G->x + 3 && x < WORLD_W; x++) L->ground[x] = WORLD_H;
  for (int x = G->x + 3; x < WORLD_W; x++) {
    L->biome[x] = BIOME_VOLCANO;
    L->ground[x] = WORLD_H;
    for (int y = 0; y < WORLD_H; y++) set(L, x, y, T_ROCK);
  }
  /* a roof over the arena's two ends keeps everyone in: a low arch near the walls only */
  /* the world's left edge */
  block(L, 0, 0, 1, WORLD_H, T_ROCK);
  /* a bottom skirt: nothing falls past y = 0 */
  for (int x = 0; x < WORLD_W; x++)
    if (L->t[0][x] == T_AIR) L->t[0][x] = T_ROCK;
}

#include "levels.inc"

void world_generate(level *L, int index) {
  memset(L, 0, sizeof *L);
  gen g0 = { L, (uint32_t)index * 2654435761u + 12345u, 0, 18 };
  if (index < 0) index = 0;
  LEVELS[index % NLEVELS].build(&g0);
  /* a coin row laid over ground that rises under it: lifted clear, so no coin sits in rock (the control keeps them there) */
  if (!g_level_old)
    for (int k = 0; k < L->ncoins; k++) {
      coin *c = &L->coins[k];
      while (c->y < (float)WORLD_H - 1 && world_solid(L, (int)floorf(c->x), (int)floorf(c->y))) c->y += 1.0f;
    }
}
