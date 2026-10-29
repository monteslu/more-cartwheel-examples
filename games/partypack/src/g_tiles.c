/* Tile Drop (2D, last standing): the floor is made of tiles, and a tile you stand on cracks and falls away, and so
 * do random ones. Keep moving and hop gaps with A. When a tile goes under you, you fall. Out? Move your ghost
 * cursor with the stick and press A to crack a tile under it. The last blob standing wins. */
#include "mg.h"

#define COLS 10
#define ROWS 5
#define TW 100.0f
#define TH 84.0f
#define X0 140.0f
#define Y0 190.0f
#define WEAR 110 /* ticks of standing before a tile cracks */
static struct {
  struct { int8_t st; int16_t wear, t; } tile[ROWS][COLS]; /* st: 0 whole, 1 cracking, 2 gone */
  struct { float x, y, vx, vy; int air; bool alive; int fall, out_t; float gx, gy; int gcd; int tr, tc, hold, idle; } p[NP];
  int alive;
  uint32_t seed; /* this game's CPU noise */
} T;

static void start(mg_ctx *c) {
  memset(&T, 0, sizeof T);
  T.seed = cw_rng_u32(&c->rng);
  T.alive = NP;
  for (int i = 0; i < NP; i++) {
    static const int col0[NP] = { 1, 3, 6, 8 }; /* mirror-symmetric starts: no seat nearer the middle than its twin */
    T.p[i].x = X0 + TW * ((float)col0[i] + 0.5f);
    T.p[i].tr = -1;
    T.p[i].y = Y0 + TH * 2.5f;
    T.p[i].alive = true;
    T.p[i].gx = X0 + TW * 5;
    T.p[i].gy = Y0 + TH * 2.5f;
  }
}

static bool floor_at(float x, float y, int *r, int *cc) {
  int col = (int)floorf((x - X0) / TW), row = (int)floorf((y - Y0) / TH);
  if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return false;
  *r = row; *cc = col;
  return T.tile[row][col].st != 2;
}
static void crack(int r, int cc) {
  if (T.tile[r][cc].st != 0) return;
  T.tile[r][cc].st = 1;
  T.tile[r][cc].t = 42;
  sfx(SFX_BUZZ, 0.12f, 1.6f);
}

static void tick(mg_ctx *c) {
  const float dt = 1.0f / 60.0f;
  int every = 95 - c->tick / 60;
  if (every < 30) every = 30;
  if (c->tick > 420 && c->tick % every == 0) { /* a random tile goes */
    for (int tries = 0; tries < 12; tries++) {
      int r = cw_rng_int(&c->rng, 0, ROWS - 1), cc = cw_rng_int(&c->rng, 0, COLS - 1);
      if (T.tile[r][cc].st == 0) { crack(r, cc); break; }
    }
  }
  for (int r = 0; r < ROWS; r++)
    for (int k = 0; k < COLS; k++)
      if (T.tile[r][k].st == 1 && --T.tile[r][k].t <= 0) {
        T.tile[r][k].st = 2;
        fx_burst(cw_v2(X0 + TW * ((float)k + 0.5f), Y0 + TH * ((float)r + 0.5f)), 10, cw_rgba(0.6f, 0.5f, 0.45f, 1), 180, 0.6f, 7);
        sfx(SFX_BOUNCE, 0.3f, 0.5f);
      }
  for (int i = 0; i < NP; i++) {
    __typeof__(T.p[0]) *p = &T.p[i];
    const mg_in *in = &c->in[i];
    if (!p->alive) { /* ghost cursor */
      p->gx = fclampf(p->gx + in->x * 380 * dt, X0 + 10, X0 + TW * COLS - 10);
      p->gy = fclampf(p->gy + in->y * 380 * dt, Y0 + 10, Y0 + TH * ROWS - 10);
      if (p->gcd > 0) p->gcd--;
      if (in->pa && p->gcd == 0) {
        int r, cc;
        if (floor_at(p->gx, p->gy, &r, &cc)) { crack(r, cc); p->gcd = 170; fx_ring(cw_v2(p->gx, p->gy), 8, 60, 0.4f, PCOL[i]); }
      }
      continue;
    }
    if (p->fall > 0) { /* falling: gone when it ends */
      p->fall++;
      p->y += 160 * dt;
      if (p->fall > 40) {
        p->alive = false;
        p->out_t = c->tick;
        T.alive--;
        c->eliminated[i] = true;
        c->score[i] = (float)(c->tick / 6);
        p->gx = X0 + TW * 5; p->gy = Y0 + TH * 2.5f; p->gcd = 60;
        sfx(SFX_SPLASH, 0.6f, 0.7f);
        fx_text(cw_v2(fclampf(p->x, 120, 1160), 330), "DROP", PCOL[i], 34);
        fx_hitstop(4);
        pp_rumble(c, i, 0.45f, 0.3f, 180); /* dropped through */
      }
      continue;
    }
    float ax = in->x, ay = in->y, l = sqrtf(ax * ax + ay * ay);
    if (l > 1) ax /= l, ay /= l;
    float sp = p->air > 0 ? 300 : 230;
    p->vx += (ax * sp - p->vx) * 0.25f;
    p->vy += (ay * sp - p->vy) * 0.25f;
    if (in->pa && p->air == 0) { p->air = 34; sfx(SFX_JUMP, 0.4f, 1.2f); }
    if (p->air > 0) p->air--;
    p->x = fclampf(p->x + p->vx * dt, X0 + 6, X0 + TW * COLS - 6);
    p->y = fclampf(p->y + p->vy * dt, Y0 + 6, Y0 + TH * ROWS - 6);
    if (p->air == 0) {
      int r, cc;
      if (!floor_at(p->x, p->y, &r, &cc)) { p->fall = 1; sfx(SFX_BUZZ, 0.4f, 0.6f); }
      else {
        if (++T.tile[r][cc].wear >= WEAR) crack(r, cc); /* standing wears a tile out */
      }
    }
  }
  for (int i = 0; i < NP; i++) if (T.p[i].alive) c->score[i] = (float)(c->tick / 6);
  if (T.alive <= 1) {
    for (int i = 0; i < NP; i++) if (T.p[i].alive) c->score[i] += 5; /* the last one standing: just ahead of the one who fell last */
    c->done = true;
  }
}

static void think(mg_ctx *c, int p, mg_in *out) {
  memset(out, 0, sizeof *out);
  __typeof__(T.p[0]) *me = &T.p[p];
  uint32_t h = hash_u32(T.seed + (uint32_t)p * 91u + 3u, (uint32_t)c->tick);
  if (!me->alive) { /* ghost: crack a tile under the nearest blob */
    int tgt = -1;
    float bd = 1e9f;
    for (int k = 0; k < NP; k++) if (T.p[k].alive) { float d = hypotf(T.p[k].x - me->gx, T.p[k].y - me->gy); if (d < bd) bd = d, tgt = k; }
    if (tgt >= 0) {
      float dx = T.p[tgt].x - me->gx, dy = T.p[tgt].y - me->gy, d = bd + 0.001f;
      out->x = dx / d; out->y = dy / d;
      out->a = me->gcd == 0 && bd < 60 && (h % 100) < 20;
    }
    return;
  }
  /* pick a tile to head for and keep it a while (a target re-rolled every tick only jitters on the spot, wearing
   * one tile out): whole, little worn, near, away from the others. Better CPUs re-plan sooner and wear less */
  static const int care[3] = { 55, 25, 20 }, patience[3] = { 50, 55, 55 }, leap_pct[3] = { 30, 65, 95 };
  int sk = c->cpu_skill[p];
  int mr = (int)floorf((me->y - Y0) / TH), mc = (int)floorf((me->x - X0) / TW);
  int r, cc;
  bool on = floor_at(me->x, me->y, &r, &cc);
  bool replan = me->tr < 0 || T.tile[me->tr][me->tc].st != 0 || --me->hold <= 0 || (on && r == me->tr && cc == me->tc && T.tile[r][cc].wear > WEAR * (4 - sk) / 6);
  if (replan) {
    float bs = -1e9f;
    for (int rr = 0; rr < ROWS; rr++)
      for (int k = 0; k < COLS; k++) {
        if (T.tile[rr][k].st != 0) continue;
        int dr = rr - mr, dc = k - mc;
        if (dr * dr + dc * dc > 9 || (dr == 0 && dc == 0)) continue;
        float s = -(float)T.tile[rr][k].wear * 0.8f - (float)(dr * dr + dc * dc) * 4.0f;
        for (int q = 0; q < NP; q++) if (q != p && T.p[q].alive && fabsf(T.p[q].x - (X0 + TW * ((float)k + 0.5f))) < TW && fabsf(T.p[q].y - (Y0 + TH * ((float)rr + 0.5f))) < TH) s -= 25;
        if (sk > 0) { /* a better CPU prefers a way there with no gap or crack on it */
          float tx = X0 + TW * ((float)k + 0.5f), ty = Y0 + TH * ((float)rr + 0.5f);
          for (int st = 1; st <= 3; st++) {
            int pr, pc;
            float f = (float)st / 4.0f;
            if (!floor_at(me->x + (tx - me->x) * f, me->y + (ty - me->y) * f, &pr, &pc) || T.tile[pr][pc].st == 1) s -= sk == 2 ? 45.0f : 15.0f;
          }
        }
        if (sk == 2) /* a hard CPU also keeps clear of the ghosts' cursors */
          for (int q = 0; q < NP; q++) if (!T.p[q].alive && hypotf(T.p[q].gx - (X0 + TW * ((float)k + 0.5f)), T.p[q].gy - (Y0 + TH * ((float)rr + 0.5f))) < TW * 1.5f) s -= 30;
        s += (float)(hash_u32(h, (uint32_t)(rr * COLS + k)) % (uint32_t)(10 + care[sk] * 2));
        if (s > bs) bs = s, me->tr = rr, me->tc = k;
      }
    me->hold = patience[sk] + (int)(h % 20);
  }
  if (me->idle > 0) { me->idle--; return; } /* an easy CPU daydreams now and then */
  if (sk == 0 && (h % 1000) < 6) me->idle = 20 + (int)(h % 30);
  if (me->tr >= 0) {
    float bx = X0 + TW * ((float)me->tc + 0.5f), by = Y0 + TH * ((float)me->tr + 0.5f);
    float dx = bx - me->x, dy = by - me->y, d = sqrtf(dx * dx + dy * dy) + 0.001f;
    if (d > 12) { out->x = dx / d; out->y = dy / d; }
    /* a gap on the way: hop it */
    int nr, nc;
    bool ahead = floor_at(me->x + dx / d * 50, me->y + dy / d * 50, &nr, &nc);
    if (me->air == 0 && !ahead && (h % 100) < (uint32_t)leap_pct[sk]) out->a = true;
    /* a better CPU reads the tile ahead: a cracking one on the way means another plan */
    if (sk > 0 && ahead && T.tile[nr][nc].st == 1 && (h % 100) < (uint32_t)(sk * 40)) me->hold = 0;
  }
  if (me->air == 0 && !on) out->a = true; /* too late to hop, but try */
  if (on && me->air == 0 && T.tile[r][cc].st == 1 && (h % 100) < (uint32_t)(20 + leap_pct[sk] / 3)) out->a = true; /* cracking under us: leap */
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha;
  ui_backdrop(t, cw_rgba(0.05f, 0.03f, 0.12f, 1), cw_rgba(0.12f, 0.05f, 0.2f, 1));
  for (int r = 0; r < ROWS; r++)
    for (int k = 0; k < COLS; k++) {
      float x = X0 + TW * (float)k, y = Y0 + TH * (float)r;
      int st = T.tile[r][k].st;
      if (st == 2) { cw_r2d_rect(cw_rect_make(x + 3, y + 3, TW - 6, TH - 6), cw_rgba(0.02f, 0.01f, 0.05f, 1)); continue; }
      float wear = fminf((float)T.tile[r][k].wear / (float)WEAR, 1);
      cw_color top = col_mix(cw_rgba(0.35f, 0.7f, 0.95f, 1), cw_rgba(0.95f, 0.45f, 0.3f, 1), st == 1 ? 1.0f : wear * 0.8f);
      /* a tile about to drop pulses brighter and darker (it used to jiggle sideways: nothing on screen shakes now) */
      if (st == 1) top = col_mix(top, cw_rgba(1, 0.9f, 0.6f, 1), 0.35f + 0.35f * sinf((float)c->tick * 0.25f + (float)(r * 7 + k)));
      cw_r2d_rect(cw_rect_make(x + 3, y + 9, TW - 6, TH - 6), col_mix(top, cw_rgba(0, 0, 0, 1), 0.5f));
      cw_r2d_rect(cw_rect_make(x + 3, y + 3, TW - 6, TH - 6), top);
      if (wear > 0.4f || st == 1) { /* cracks */
        cw_r2d_line(cw_v2(x + 20, y + 14), cw_v2(x + 50, y + 40), 3, cw_rgba(0.1f, 0.05f, 0.1f, 0.8f));
        cw_r2d_line(cw_v2(x + 50, y + 40), cw_v2(x + 78, y + 30), 3, cw_rgba(0.1f, 0.05f, 0.1f, 0.8f));
      }
    }
  for (int i = 0; i < NP; i++) {
    __typeof__(T.p[0]) *p = &T.p[i];
    if (!p->alive) {
      cw_r2d_circle_lines(cw_v2(p->gx, p->gy), 24, 4, 20, col_a(PCOL[i], p->gcd > 0 ? 0.35f : 1.0f));
      ui_blob(cw_v2(p->gx, p->gy), 14, i, 0, 0, 1, p->gcd > 0 ? 3 : 1, !c->human[i]);
      continue;
    }
    float lift = p->air > 0 ? sinf((float)p->air / 34.0f * 3.14159f) * 34 : 0;
    float s = p->fall > 0 ? fmaxf(0.2f, 1 - (float)p->fall / 50.0f) : 1;
    if (p->fall == 0) cw_r2d_circle(cw_v2(p->x, p->y + 6), 20, 14, cw_rgba(0, 0, 0, 0.3f));
    ui_blob(cw_v2(p->x, p->y - lift - 14), 22 * s, i, p->vx / 230.0f, p->vy / 230.0f, p->air > 0 ? 1.15f : 1, p->fall > 0 ? 2 : 0, !c->human[i]);
  }
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "%s", T.p[p].alive ? "on" : (c->human[p] ? "ghost" : "out")); }

const mg_def mg_tiles = {
  .id = "tiles", .name = "Tile Drop", .tagline = "The floor crumbles. Keep moving and be the last one standing.",
  .how = { "Stick: run, a tile you stand on cracks", "A: hop a gap (a short flight)", "Out? Ghost cursor, A cracks a tile under it" },
  .buttons = 1, .keys = "SA",
  .kind = "2D", .scene = NULL, .seconds = 50, .unlock_points = 0, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
