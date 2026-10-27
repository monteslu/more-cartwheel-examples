/* Echo (2D, memory): watch a pattern of four coloured pads light up, then
 * play it back on your pad: A, B, X, Y or the matching direction (down,
 * right, left, up). One wrong press and you are out. Every round adds a
 * step; the last player standing, or the longest memory, wins.
 * A player who is out plays on as a ghost: every round they can play the
 * pattern too, and each full round earns a little bonus that only breaks ties
 * between players who went out on the same pattern (the score is the longest pattern played back) (it never beats a player
 * who lasted a round longer). */
#include "mg.h"

enum { PH_GAP, PH_SHOW, PH_INPUT };
#define MAXSEQ 24
static struct {
  int seq[MAXSEQ], len, ph, t;
  int prog[NP], done[NP], rounds[NP], best[NP]; /* best: the longest pattern played back (the score) */
  int gprog[NP], gdone[NP]; /* a ghost's progress in this round, and the full rounds it played */
  bool alive[NP];
  float dirprev_x[NP], dirprev_y[NP];
  int flash[NP], lit; /* ticks left for a player's last press / the shown pad */
  int last_sym[NP];
  int cpu_wait[NP];
  int msg;
} M;

static const cw_color PAD_COL[4] = { { 0.3f, 0.8f, 0.4f, 1 }, { 0.9f, 0.3f, 0.3f, 1 }, { 0.3f, 0.5f, 0.95f, 1 }, { 0.95f, 0.8f, 0.25f, 1 } };
static const char PAD_LBL[4] = { 'A', 'B', 'X', 'Y' };
static const int NOTE_OF[4] = { 0, 2, 4, 5 };

static void start(mg_ctx *c) {
  memset(&M, 0, sizeof M);
  for (int i = 0; i < MAXSEQ; i++) M.seq[i] = cw_rng_int(&c->rng, 0, 3);
  M.len = 3;
  for (int p = 0; p < NP; p++) M.alive[p] = true;
  M.lit = -1;
}

static int pressed_symbol(mg_ctx *c, int p) {
  const mg_in *in = &c->in[p];
  int s = -1;
  if (in->pa) s = 0;
  else if (in->pb) s = 1;
  else if (in->pc) s = 2;
  else if (in->pd) s = 3;
  else if (in->y > 0.6f && M.dirprev_y[p] <= 0.6f) s = 0;
  else if (in->x > 0.6f && M.dirprev_x[p] <= 0.6f) s = 1;
  else if (in->x < -0.6f && M.dirprev_x[p] >= -0.6f) s = 2;
  else if (in->y < -0.6f && M.dirprev_y[p] >= -0.6f) s = 3;
  M.dirprev_x[p] = in->x;
  M.dirprev_y[p] = in->y;
  return s;
}

static void next_round(mg_ctx *c) {
  int alive = 0;
  for (int p = 0; p < NP; p++) alive += M.alive[p];
  if (alive == 0) { c->done = true; return; }
  if (alive == 1 && c->tick > 0) { /* the survivor gets credit for the round just played */
    /* the survivor played the last pattern the others missed, so its score is already one step ahead */
    c->done = true;
    return;
  }
  if (M.len >= MAXSEQ - 1) { c->done = true; return; }
  M.len++;
  M.ph = PH_GAP;
  M.t = 0;
  M.msg = 0;
}

static void tick(mg_ctx *c) {
  M.t++;
  for (int p = 0; p < NP; p++) if (M.flash[p] > 0) M.flash[p]--;
  if (M.ph == PH_GAP) {
    if (M.t > 50) { M.ph = PH_SHOW; M.t = 0; }
  } else if (M.ph == PH_SHOW) {
    int step = M.len > 8 ? 26 : 34;
    int i = M.t / step, j = M.t % step;
    M.lit = -1;
    if (i < M.len && j < step - 8) M.lit = M.seq[i];
    if (i < M.len && j == 0) sfx(SFX_NOTE0 + NOTE_OF[M.seq[i]], 0.7f, 1);
    if (M.t > M.len * step + 20) { M.ph = PH_INPUT; M.t = 0; M.lit = -1; for (int p = 0; p < NP; p++) { M.prog[p] = 0; M.gprog[p] = 0; M.cpu_wait[p] = 40; } sfx(SFX_GO, 0.5f, 1.3f); }
  } else {
    bool all = true;
    for (int p = 0; p < NP; p++) {
      if (!M.alive[p]) { /* a ghost plays along: a slip just ends its round */
        if (M.gprog[p] >= M.len) continue;
        int gs = pressed_symbol(c, p);
        if (gs < 0) continue;
        M.flash[p] = 12;
        M.last_sym[p] = gs;
        if (gs == M.seq[M.gprog[p]]) {
          sfx(SFX_NOTE0 + NOTE_OF[gs], c->human[p] ? 0.3f : 0.1f, 1);
          if (++M.gprog[p] >= M.len) {
            M.gdone[p]++;
            c->score[p] = (float)M.best[p] + fminf(0.1f * (float)M.gdone[p], 0.4f);
            fx_text(cw_v2(250 + p * 260, 560), "ghost +", PCOL[p], 24);
          }
        } else { M.gprog[p] = M.len; sfx(SFX_BUZZ, 0.3f, 1.4f); }
        continue;
      }
      if (M.prog[p] >= M.len) continue;
      all = false;
      int s = pressed_symbol(c, p);
      if (s < 0) continue;
      M.flash[p] = 12;
      M.last_sym[p] = s;
      if (s == M.seq[M.prog[p]]) {
        M.prog[p]++;
        sfx(SFX_NOTE0 + NOTE_OF[s], c->human[p] ? 0.5f : 0.2f, 1);
        if (M.prog[p] >= M.len) { M.rounds[p]++; M.best[p] = M.len; c->score[p] = (float)M.best[p]; fx_text(cw_v2(250 + p * 260, 560), "done", PCOL[p], 30); }
      } else {
        M.alive[p] = false;
        c->eliminated[p] = true;
        sfx(SFX_BUZZ, 0.8f, 1);
        fx_text(cw_v2(250 + p * 260, 560), "OUT", PCOL[p], 40);
      }
    }
    int limit = 60 * (6 + M.len);
    if (all || M.t > limit) {
      for (int p = 0; p < NP; p++) if (M.alive[p] && M.prog[p] < M.len) { M.alive[p] = false; c->eliminated[p] = true; }
      M.ph = PH_GAP; M.t = 0;
      next_round(c);
    }
  }
}

static void think(mg_ctx *c, int p, mg_in *out) {
  memset(out, 0, sizeof *out);
  if (M.ph != PH_INPUT) return;
  int *pr = M.alive[p] ? &M.prog[p] : &M.gprog[p];
  if (*pr >= M.len) return;
  if (--M.cpu_wait[p] > 0) return;
  static const int err_pct[3] = { 6, 3, 1 }; /* per step, plus a little for length */
  int r = cw_rng_int(&c->rng, 0, 99);
  int s = M.seq[*pr];
  if (r < err_pct[c->cpu_skill[p]] + M.len / 3) s = (s + 1) % 4;
  int d = cw_rng_int(&c->rng, 0, 1);
  if (d) { out->a = s == 0; out->b = s == 1; out->c = s == 2; out->d = s == 3; }
  else { out->y = s == 0 ? 1.0f : s == 3 ? -1.0f : 0; out->x = s == 1 ? 1.0f : s == 2 ? -1.0f : 0; }
  M.cpu_wait[p] = cw_rng_int(&c->rng, 14 - c->cpu_skill[p] * 3, 26 - c->cpu_skill[p] * 5);
}

static const char *PAD[4] = { "pad_mint", "pad_ruby", "pad_sky", "pad_sun" };
static const char *PAD_ON[4] = { "pad_mint_on", "pad_ruby_on", "pad_sky_on", "pad_sun_on" };
static void draw_pad(cw_vec2 ctr, int s, bool lit, float r) {
  cw_color col = PAD_COL[s];
  art_draw("shadow", cw_v2(ctr.x, ctr.y + r * 1.0f), cw_v2(r * 2.3f, r * 0.55f), 0, cw_rgba(1, 1, 1, 0.6f));
  art_draw(lit ? PAD_ON[s] : PAD[s], ctr, cw_v2(r * 1.55f * (lit ? 1.06f : 1), r * 1.55f * (lit ? 1.06f : 1)), 0, (cw_color){ 0 });
  if (lit) {
    art_light(ctr, 520, col, 1.8f, false);
    art_glow("glow", ctr, r * 5, col_a(col, 0.45f));
  }
  char b[2] = { PAD_LBL[s], 0 };
  ui_text(b, cw_v2(ctr.x, ctr.y - r * 0.4f), r * 0.8f, cw_rgba(1, 1, 1, lit ? 1 : 0.55f), CW_ALIGN_CENTER);
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha;
  art_ambient = cw_rgba(0.7f, 0.66f, 0.85f, 1);
  ui_backdrop(t, cw_rgba(0.12f, 0.12f, 0.3f, 1), cw_rgba(0.3f, 0.12f, 0.4f, 1));
  cw_vec2 mid = cw_v2(STAGE_W / 2, 350);
  float d = 96;
  cw_vec2 pos[4] = { cw_v2(mid.x, mid.y + d), cw_v2(mid.x + d * 1.2f, mid.y), cw_v2(mid.x - d * 1.2f, mid.y), cw_v2(mid.x, mid.y - d) };
  art_light(cw_v2(mid.x, mid.y - 200), 900, cw_rgba(0.8f, 0.8f, 1, 1), 0.6f, false);
  for (int s = 0; s < 4; s++) draw_pad(pos[s], s, M.lit == s, 70);
  const char *msg = M.ph == PH_SHOW ? "Watch!" : M.ph == PH_INPUT ? "Your turn!" : "Get ready";
  ui_text(msg, cw_v2(STAGE_W / 2, 98), 44, cw_rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER);
  char s[40];
  snprintf(s, sizeof s, "Pattern length %d", M.len);
  ui_text(s, cw_v2(STAGE_W / 2, 148), 28, cw_rgba(1, 1, 1, 0.7f), CW_ALIGN_CENTER);
  for (int p = 0; p < NP; p++) { /* each player's progress */
    float x = 90 + p * 280, y = 520;
    art_nine("panel", cw_rect_make(x, y, 260, 140), 26, 1.0f, cw_rgba(0.9f, 0.9f, 1.0f, M.alive[p] ? 0.8f : 0.45f));
    cw_r2d_round_rect_lines(cw_rect_make(x + 2, y + 2, 256, 136), 16, 3, 8, col_a(PCOL[p], M.alive[p] ? 0.9f : 0.2f));
    ui_blob(cw_v2(x + 40, y + 50), 26, p, 0, 0, M.flash[p] > 0 ? 1.15f : 1, M.alive[p] ? (M.flash[p] > 0 ? 2 : 0) : 3, !c->human[p]);
    int shown = M.len < 14 ? M.len : 14;
    static const char *GEMS[4] = { "gem_mint", "gem_ruby", "gem_sky", "gem_sun" };
    for (int i = 0; i < shown; i++) {
      bool got = i < M.prog[p];
      cw_vec2 q = cw_v2(x + 90 + (i % 7) * 23, y + 24 + (i / 7) * 26);
      art_draw(M.ph == PH_INPUT && got ? GEMS[M.seq[i]] : "gem_sun", q, cw_v2(20, 20), 0, got ? (M.ph == PH_INPUT ? (cw_color){ 0 } : cw_rgba(1, 1, 1, 0.9f)) : cw_rgba(0.25f, 0.25f, 0.3f, 0.5f));
    }
    if (M.flash[p] > 0) art_draw(GEMS[M.last_sym[p]], cw_v2(x + 40, y + 100), cw_v2(30, 30), 0, (cw_color){ 0 });
    if (!M.alive[p]) ui_text(M.ph == PH_INPUT && M.gprog[p] < M.len ? "ghost: play along" : "ghost", cw_v2(x + 130, y + 112), 20, cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER);
    snprintf(s, sizeof s, "%d", M.best[p]);
    ui_text(s, cw_v2(x + 240, y + 90), 36, cw_rgba(1, 1, 1, M.alive[p] ? 1 : 0.4f), CW_ALIGN_RIGHT);
  }
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "best %d", (int)c->score[p]); }

const mg_def mg_memory = {
  .id = "echo", .name = "Echo", .tagline = "Repeat the pattern. One slip and you are out.",
  .how = { "Flick the stick (or A B X Y) to match", "Watch the pads, then play it back", "Out? Play along as a ghost for a tie-break" },
  .buttons = 0, .keys = "S",
  .kind = "2D", .scene = NULL, .seconds = 85, .unlock_points = 0, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
