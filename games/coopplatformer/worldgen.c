/* The level: a connected world of four biomes laid left to right, with ability gates (a gap that needs the air
 * jump, a shaft that needs wall jumps, a lava span that needs the dash), a hidden room behind a cracked wall, a
 * pool to swim through, a moving platform, checkpoints and a boss arena. Generated from a seed with integer maths
 * only, so every host builds the same world. */
#include <string.h>

#include "game.h"

typedef struct gen {
  level *L;
  uint32_t s;
  int x;     /* the cursor: next free column */
  int g;     /* surface height of the path at the cursor */
} gen;

static uint32_t rnd(gen *G, int n) {
  G->s = G->s * 1664525u + 1013904223u;
  return (G->s >> 8) % (uint32_t)n;
}
static bool in_range(int x, int y) { return x >= 0 && x < WORLD_W && y >= 0 && y < WORLD_H; }
static void set(level *L, int x, int y, int t) {
  if (in_range(x, y)) L->t[y][x] = (uint8_t)t;
}
bool world_solid(const level *L, int x, int y) {
  if (x < 0 || x >= WORLD_W) return true;
  if (y < 0) return true;
  if (y >= WORLD_H) return false;
  int t = L->t[y][x];
  return t == T_ROCK || t == T_GRASS || t == T_CRACK || t == T_BRICK;
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
/* a mound three tiles high with a sealed pocket in its foot (3 tiles tall inside, the same on the way in), a cracked
 * wall in front of it and 'air' columns of room: 4 or 3 secret coins inside */
static void pocket(gen *G, int air, int tail, int biome) {
  level *L = G->L;
  int mx = G->x, mg = G->g, n = 1 + air + tail;
  for (int i = 0; i < n; i++) {
    int x = G->x++;
    column(G, x, mg, biome);
    for (int y = mg - 1; y <= mg + 1; y++) set(L, x, y, i == 0 ? T_CRACK : (i <= air ? T_AIR : T_ROCK));
    set(L, x, mg + 2, T_ROCK);
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
  if (L->nchecks < 16) L->checks[L->nchecks++] = (checkpoint){ G->x - 1, G->g };
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

void world_generate(level *L, uint32_t seed) {
  memset(L, 0, sizeof *L);
  gen g0 = { L, seed * 2654435761u + 12345u, 0, 18 };
  gen *G = &g0;
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
  rise(G, -1, 6, BIOME_MEADOW);
  pedestal(G, AB_DOUBLE);
  coin_row(G, G->x - 5, 3, G->g + 2);
  flat(G, 3, BIOME_MEADOW);
  int gx0;
  gate_begin(G, AB_DOUBLE, &gx0);
  gap(G, 8, BIOME_MEADOW, T_SPIKE);               /* the air jump gate: too wide for one jump */
  gate_end(G, AB_DOUBLE, gx0);
  rise(G, 1, 6, BIOME_MEADOW);
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
    coin_row(G, sx + 1, 4, base + 5);
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
