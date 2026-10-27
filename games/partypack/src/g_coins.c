/* Coin Rush (2D, collect): coins pop up all over the yard; run over them to grab them. Gold coins are worth three.
 * A is a dash, and a dash that bumps another blob shakes a coin out of its pockets for anyone to take. Most coins
 * after 35 seconds wins. */
#include "mg.h"

#define AX0 90.0f
#define AY0 150.0f
#define AW 1100.0f
#define AH 480.0f
#define MAXC 24
#define BR 26.0f
static struct {
  struct { float x, y, vx, vy; int cd, dash, stun; float fx, fy; int coins; } p[NP];
  struct { float x, y, vx, vy; int val, age; bool on; } c[MAXC];
  int next_gold;
  uint32_t seed; /* this game's CPU noise, so no two games play the same */
} C;

static void spawn(mg_ctx *c, int val, float x, float y, float vx, float vy) {
  for (int i = 0; i < MAXC; i++)
    if (!C.c[i].on) { C.c[i].on = true; C.c[i].val = val; C.c[i].x = x; C.c[i].y = y; C.c[i].vx = vx; C.c[i].vy = vy; C.c[i].age = 0; return; }
  (void)c;
}
static void spawn_random(mg_ctx *c, int val) {
  float x = cw_rng_range(&c->rng, AX0 + 40, AX0 + AW - 40);
  float y = cw_rng_range(&c->rng, AY0 + 40, AY0 + AH - 40);
  spawn(c, val, x, y, 0, 0);
}

static void start(mg_ctx *c) {
  memset(&C, 0, sizeof C);
  for (int i = 0; i < NP; i++) {
    /* the four corners of a square round the middle: every seat has the same room and the same neighbours (a row
     * put two seats in the crowd and two on the edges) */
    C.p[i].x = AX0 + AW * (i % 2 ? 0.68f : 0.32f);
    C.p[i].y = AY0 + AH * (i < 2 ? 0.28f : 0.72f);
    C.p[i].fy = 1;
  }
  C.seed = cw_rng_u32(&c->rng);
  for (int i = 0; i < 8; i++) spawn_random(c, 1);
  C.next_gold = 360;
}

static void tick(mg_ctx *c) {
  const float dt = 1.0f / 60.0f;
  int live = 0;
  for (int i = 0; i < MAXC; i++) live += C.c[i].on;
  if (c->tick % 22 == 0 && live < 16) spawn_random(c, 1);
  if (c->tick >= C.next_gold) { spawn_random(c, 3); C.next_gold = c->tick + 300 + cw_rng_int(&c->rng, 0, 120); }
  for (int i = 0; i < NP; i++) {
    __typeof__(C.p[0]) *p = &C.p[i];
    const mg_in *in = &c->in[i];
    if (p->stun > 0) p->stun--;
    float ax = in->x, ay = in->y, l = sqrtf(ax * ax + ay * ay);
    if (l > 1) ax /= l, ay /= l;
    if (p->stun == 0) {
      p->vx += ax * 1500 * dt;
      p->vy += ay * 1500 * dt;
      if (l > 0.3f) p->fx = ax, p->fy = ay;
      if (p->cd > 0) p->cd--;
      if (in->pa && p->cd == 0) {
        p->vx += p->fx * 520; p->vy += p->fy * 520;
        p->cd = 80; p->dash = 12;
        sfx(SFX_JUMP, 0.4f, 1.5f);
      }
    }
    if (p->dash > 0) p->dash--;
    float sp = sqrtf(p->vx * p->vx + p->vy * p->vy), cap = p->dash > 0 ? 640 : 300;
    if (sp > cap) { p->vx *= cap / sp; p->vy *= cap / sp; }
    p->vx *= 0.9f; p->vy *= 0.9f;
    p->x = fclampf(p->x + p->vx * dt, AX0 + BR, AX0 + AW - BR);
    p->y = fclampf(p->y + p->vy * dt, AY0 + BR, AY0 + AH - BR);
  }
  /* a dash that bumps another blob shakes a coin loose. Who hits whom is decided for every pair first and applied
   * after, so two dashes that meet both land (the lower seat used to stun the other before its hit counted) */
  bool hit[NP][NP] = { { false } };
  for (int i = 0; i < NP; i++)
    for (int j = 0; j < NP; j++) {
      if (i == j || C.p[i].dash == 0 || C.p[j].stun > 0) continue;
      float dx = C.p[j].x - C.p[i].x, dy = C.p[j].y - C.p[i].y, d = sqrtf(dx * dx + dy * dy);
      hit[i][j] = d < BR * 2 && d > 0.01f;
    }
  for (int i = 0; i < NP; i++)
    for (int j = 0; j < NP; j++) {
      if (!hit[i][j]) continue;
      float dx = C.p[j].x - C.p[i].x, dy = C.p[j].y - C.p[i].y, d = sqrtf(dx * dx + dy * dy) + 0.001f;
      {
        C.p[j].vx += dx / d * 380; C.p[j].vy += dy / d * 380;
        C.p[j].stun = 24;
        if (C.p[j].coins > 0) {
          C.p[j].coins--;
          spawn(c, 1, C.p[j].x, C.p[j].y, dx / d * 160 + 40, dy / d * 160 - 40);
        }
        fx_burst(cw_v2(C.p[j].x, C.p[j].y), 8, cw_rgba(1, 0.9f, 0.4f, 1), 200, 0.4f, 6);
        fx_hitstop(3);
        sfx(SFX_HIT, 0.5f, 1.2f);
      }
    }
  for (int k = 0; k < MAXC; k++) {
    if (!C.c[k].on) continue;
    C.c[k].age++;
    C.c[k].x = fclampf(C.c[k].x + C.c[k].vx * dt, AX0 + 14, AX0 + AW - 14);
    C.c[k].y = fclampf(C.c[k].y + C.c[k].vy * dt, AY0 + 14, AY0 + AH - 14);
    C.c[k].vx *= 0.95f; C.c[k].vy *= 0.95f;
    if (C.c[k].age < 20) continue; /* a loosened coin hangs in the air for a moment */
    /* the nearest blob in reach takes it (an exact tie goes to the seat the tick points at, never always seat 1) */
    int who = -1;
    float wd = (BR + 14) * (BR + 14);
    for (int n = 0; n < NP; n++) {
      int i = (n + c->tick) % NP;
      float dx = C.p[i].x - C.c[k].x, dy = C.p[i].y - C.c[k].y, dd = dx * dx + dy * dy;
      if (dd < wd) wd = dd, who = i;
    }
    if (who >= 0) {
      int i = who;
      {
        C.c[k].on = false;
        C.p[i].coins += C.c[k].val;
        sfx(C.c[k].val > 1 ? SFX_WIN : SFX_COIN, 0.5f, 1.0f + 0.1f * (float)(C.p[i].coins % 5));
        fx_burst(cw_v2(C.c[k].x, C.c[k].y), C.c[k].val > 1 ? 14 : 6, cw_rgba(1, 0.9f, 0.3f, 1), 150, 0.4f, 5);
        if (C.c[k].val > 1) { fx_text(cw_v2(C.c[k].x, C.c[k].y - 20), "+3", PCOL[i], 34); fx_hitstop(2); }
      }
    }
  }
  for (int i = 0; i < NP; i++) c->score[i] = (float)C.p[i].coins;
}

static void think(mg_ctx *c, int p, mg_in *out) {
  memset(out, 0, sizeof *out);
  static const int greed[3] = { 40, 25, 10 }; /* how far a CPU will go for a one-coin pickup (smaller: pickier, nearer) */
  uint32_t h = hash_u32(C.seed + (uint32_t)p * 31u + 1u, (uint32_t)(c->tick / (12 - 3 * c->cpu_skill[p])));
  int best = -1;
  float bs = -1e9f;
  for (int k = 0; k < MAXC; k++) {
    if (!C.c[k].on) continue;
    float d = hypotf(C.c[k].x - C.p[p].x, C.c[k].y - C.p[p].y);
    float sc = (float)C.c[k].val * 140.0f - d + (float)((h + (uint32_t)k * 7u) % (uint32_t)(60 + greed[c->cpu_skill[p]] * 4));
    if (c->cpu_skill[p] == 0) sc -= (float)((h >> 5) % 90);
    if (sc > bs) bs = sc, best = k;
  }
  if (best < 0) return;
  float dx = C.c[best].x - C.p[p].x, dy = C.c[best].y - C.p[p].y, d = sqrtf(dx * dx + dy * dy) + 0.001f;
  out->x = dx / d; out->y = dy / d;
  if (d > 150 && C.p[p].cd == 0 && c->cpu_skill[p] > 0 && (h % 100) < 6 + (uint32_t)c->cpu_skill[p] * 6) out->a = true;
  /* a hard CPU also dashes into a rich neighbour that is close */
  if (c->cpu_skill[p] == 2 && C.p[p].cd == 0) {
    for (int q = 0; q < NP; q++) {
      if (q == p || C.p[q].coins < 3) continue;
      float qx = C.p[q].x - C.p[p].x, qy = C.p[q].y - C.p[p].y, qd = sqrtf(qx * qx + qy * qy);
      if (qd < 90 && (h % 100) < 30) { out->x = qx / qd; out->y = qy / qd; out->a = true; }
    }
  }
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha;
  ui_backdrop(t, cw_rgba(0.15f, 0.45f, 0.4f, 1), cw_rgba(0.05f, 0.2f, 0.25f, 1));
  cw_r2d_rect(cw_rect_make(AX0 - 8, AY0 - 8, AW + 16, AH + 16), cw_rgba(0.05f, 0.1f, 0.15f, 1));
  cw_r2d_rect(cw_rect_make(AX0, AY0, AW, AH), cw_rgba(0.2f, 0.5f, 0.35f, 1));
  for (int k = 0; k < 11; k++) cw_r2d_rect(cw_rect_make(AX0 + (float)k * 100 + 50, AY0, 50, AH), cw_rgba(1, 1, 1, 0.04f));
  for (int k = 0; k < MAXC; k++) {
    if (!C.c[k].on) continue;
    bool gold = C.c[k].val > 1;
    float bob = sinf(t * 6 + (float)k) * 3, r = gold ? 17 : 12;
    cw_r2d_circle(cw_v2(C.c[k].x, C.c[k].y + 8), r, 14, cw_rgba(0, 0, 0, 0.25f));
    cw_r2d_circle(cw_v2(C.c[k].x, C.c[k].y + bob), r, 16, gold ? cw_rgba(1, 0.65f, 0.1f, 1) : cw_rgba(1, 0.88f, 0.25f, 1));
    cw_r2d_circle_lines(cw_v2(C.c[k].x, C.c[k].y + bob), r, 3, 16, cw_rgba(0.5f, 0.3f, 0, 1));
    if (gold) ui_text("3", cw_v2(C.c[k].x, C.c[k].y + bob - 13), 24, cw_rgba(0.4f, 0.2f, 0, 1), CW_ALIGN_CENTER);
  }
  for (int i = 0; i < NP; i++) {
    float sq = C.p[i].dash > 0 ? 0.82f : C.p[i].stun > 0 ? 1.2f : 1;
    ui_blob(cw_v2(C.p[i].x, C.p[i].y), BR, i, C.p[i].fx, C.p[i].fy, sq, C.p[i].stun > 0 ? 2 : 0, !c->human[i]);
  }
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "%d coins", (int)c->score[p]); }

const mg_def mg_coins = {
  .id = "coins", .name = "Coin Rush", .tagline = "Grab the most coins. Gold ones are worth three.",
  .how = { "Stick: run over coins to grab them", "A: dash (it recharges)", "A dash that bumps a blob shakes a coin loose" },
  .buttons = 1, .keys = "SA",
  .kind = "2D", .scene = NULL, .seconds = 35, .unlock_points = 0, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
