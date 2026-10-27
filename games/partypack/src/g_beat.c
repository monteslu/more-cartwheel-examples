/* Beat Box (2D, rhythm): the same song for everyone, one lane each. Notes
 * fall onto your line; press the matching button (A green, B red, X blue)
 * as they land. Perfect hits build a combo multiplier. A hit plays the
 * note, so a good lane makes the music. */
#include "mg.h"

#define MAXN 200
#define TRAVEL 96 /* ticks a note falls */
typedef struct { int t, kind, pitch; } note;
static struct {
  note n[MAXN];
  int count;
  uint8_t hit[NP][MAXN];
  int combo[NP], best_combo[NP], judge[NP], judge_t[NP], flash[NP][3];
  int score[NP], perfects[NP], misses[NP];
  uint32_t seed;
  int beat_t;
} B;

static const int BPM_TICKS = 28; /* ticks per beat (about 128 bpm) */
static const cw_color KCOL[3] = { { 0.3f, 0.85f, 0.4f, 1 }, { 0.95f, 0.3f, 0.3f, 1 }, { 0.3f, 0.5f, 0.98f, 1 } };
static const char KLBL[3] = { 'A', 'B', 'X' };

static void start(mg_ctx *c) {
  memset(&B, 0, sizeof B);
  int t = TRAVEL + 2 * BPM_TICKS, bar = 0;
  int motif[8];
  while (t < 42 * 60 && B.count < MAXN) {
    if (bar % 4 == 0) for (int i = 0; i < 8; i++) motif[i] = cw_rng_int(&c->rng, 0, 7);
    for (int i = 0; i < 8; i++) {
      int dens = cw_rng_int(&c->rng, 0, 99);
      bool place = (i % 2 == 0) ? dens < 85 : dens < 35 + (bar > 3 ? 20 : 0);
      if (place && B.count < MAXN) {
        int kind = cw_rng_int(&c->rng, 0, 2);
        B.n[B.count++] = (note){ t + i * BPM_TICKS / 2, kind, motif[i] };
      }
    }
    t += 4 * BPM_TICKS;
    bar++;
  }
  B.seed = (uint32_t)cw_rng_u32(&c->rng);
}

static void judge(mg_ctx *c, int p, int kind) {
  int best = -1, bd = 99;
  for (int i = 0; i < B.count; i++) {
    if (B.hit[p][i]) continue;
    int dt = B.n[i].t - c->tick;
    if (dt > 12) break;
    if (B.n[i].kind != kind) continue;
    if (abs(dt) < bd) { bd = abs(dt); best = i; }
  }
  B.flash[p][kind] = 8;
  if (best >= 0 && bd <= 9) {
    B.hit[p][best] = 1;
    bool perfect = bd <= 3;
    B.combo[p]++;
    if (B.combo[p] > B.best_combo[p]) B.best_combo[p] = B.combo[p];
    int mult = 1 + B.combo[p] / 8;
    if (mult > 4) mult = 4;
    B.score[p] += (perfect ? 100 : 50) * mult;
    B.perfects[p] += perfect;
    B.judge[p] = perfect ? 2 : 1;
    B.judge_t[p] = 20;
    sfx(SFX_NOTE0 + B.n[best].pitch, c->human[p] ? 0.55f : 0.22f, 1);
    float lx = 40 + p * 310 + 135;
    fx_burst(cw_v2(lx + (kind - 1) * 70, 560), perfect ? 14 : 7, KCOL[kind], 220, 0.5f, 6);
    if (perfect) fx_ring(cw_v2(lx + (kind - 1) * 70, 560), 10, 46, 0.35f, KCOL[kind]);
  } else {
    B.combo[p] = 0;
    B.judge[p] = 0;
    B.judge_t[p] = 20;
    B.score[p] -= B.score[p] > 20 ? 20 : B.score[p];
    if (c->human[p]) sfx(SFX_BUZZ, 0.3f, 1.4f);
  }
}

static void tick(mg_ctx *c) {
  if (c->tick % BPM_TICKS == 0 && c->tick > 0) sfx((c->tick / BPM_TICKS) % 4 == 0 ? SFX_BOUNCE : SFX_BLIP, 0.25f, (c->tick / BPM_TICKS) % 4 == 0 ? 0.6f : 1.5f);
  for (int p = 0; p < NP; p++) {
    const mg_in *in = &c->in[p];
    if (in->pa) judge(c, p, 0);
    if (in->pb) judge(c, p, 1);
    if (in->pc) judge(c, p, 2);
    for (int i = 0; i < B.count; i++) {
      if (B.hit[p][i]) continue;
      if (B.n[i].t - c->tick < -9) { B.hit[p][i] = 2; B.combo[p] = 0; B.misses[p]++; B.judge[p] = 0; B.judge_t[p] = 20; }
      if (B.n[i].t - c->tick > 12) break;
    }
    if (B.judge_t[p] > 0) B.judge_t[p]--;
    for (int k = 0; k < 3; k++) if (B.flash[p][k] > 0) B.flash[p][k]--;
    c->score[p] = (float)B.score[p];
  }
  if (c->tick > B.n[B.count - 1].t + 40) c->done = true;
}

static void think(mg_ctx *c, int p, mg_in *out) {
  memset(out, 0, sizeof *out);
  static const int range[3] = { 14, 8, 4 }, miss[3] = { 18, 8, 2 };
  int s = c->skill % 3;
  for (int i = 0; i < B.count; i++) {
    if (B.hit[p][i]) continue;
    int dt = B.n[i].t - c->tick;
    if (dt > 12) break;
    uint32_t h = hash_u32(hash_u32(B.seed, (uint32_t)p), (uint32_t)i);
    if ((int)(h % 100) < miss[s]) continue;
    int jit = (int)((h >> 8) % (uint32_t)range[s]) - range[s] / 2;
    if (dt == -jit + 0 || (c->tick == B.n[i].t + jit)) {
      int k = B.n[i].kind;
      out->a = k == 0; out->b = k == 1; out->c = k == 2;
      return;
    }
  }
}

static const char *GEM[3] = { "gem_mint", "gem_ruby", "gem_sky" };
static const char *KPAD[3] = { "pad_mint", "pad_ruby", "pad_sky" };
static const char *KPAD_ON[3] = { "pad_mint_on", "pad_ruby_on", "pad_sky_on" };

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha;
  float pulse = fabsf(sinf((float)c->tick * 3.14159f / BPM_TICKS));
  art_ambient = cw_rgba(0.72f + 0.1f * pulse, 0.68f, 0.85f, 1);
  ui_backdrop(t, cw_rgba(0.2f + 0.08f * pulse, 0.06f, 0.35f, 1), cw_rgba(0.05f, 0.05f, 0.2f, 1));
  /* stage lights sweeping over the crowd */
  for (int i = 0; i < 3; i++) {
    float sw = sinf(t * 0.7f + (float)i * 2.1f);
    cw_vec2 top = cw_v2(STAGE_W * (0.2f + 0.3f * (float)i), -20);
    cw_vec2 foot = cw_v2(top.x + sw * 360, 420);
    cw_color bc = i == 0 ? cw_rgba(0.5f, 1, 0.6f, 0.07f + 0.06f * pulse) : i == 1 ? cw_rgba(1, 0.5f, 0.6f, 0.07f + 0.06f * pulse) : cw_rgba(0.5f, 0.7f, 1, 0.07f + 0.06f * pulse);
    cw_vertex2d v[4] = { { cw_v2(top.x - 12, top.y), cw_v2(0, 0), bc }, { cw_v2(top.x + 12, top.y), cw_v2(1, 0), bc }, { cw_v2(foot.x + 110, foot.y), cw_v2(1, 1), col_a(bc, 0) }, { cw_v2(foot.x - 110, foot.y), cw_v2(0, 1), col_a(bc, 0) } };
    static const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    cw_r2d_set_blend(CW_BLEND_ADD);
    cw_r2d_triangles((cw_texture){ 0 }, v, 4, idx, 6);
    cw_r2d_set_blend(CW_BLEND_ALPHA);
  }
  for (int p = 0; p < NP; p++) {
    float lx = 40 + p * 310, w = 270;
    art_nine("panel", cw_rect_make(lx, 130, w, 540), 26, 1.0f, cw_rgba(0.9f, 0.9f, 1.0f, 0.78f));
    cw_r2d_rect_lines(cw_rect_make(lx + 3, 133, w - 6, 534), 3, col_a(PCOL[p], 0.85f));
    art_world(false); /* glossy pads and gems keep their baked light */
    for (int k = 0; k < 3; k++) {
      float x = lx + w / 2 + (k - 1) * 70;
      cw_r2d_rect(cw_rect_make(x - 28, 140, 56, 520), cw_rgba(1, 1, 1, 0.05f));
      bool on = B.flash[p][k] > 0;
      art_draw(on ? KPAD_ON[k] : KPAD[k], cw_v2(x, 560), cw_v2(64, 64), 0, (cw_color){ 0 });
      if (on) art_light(cw_v2(x, 560), 220, KCOL[k], 1.3f, false);
      char bl[2] = { KLBL[k], 0 };
      ui_text(bl, cw_v2(x, 545), 28, cw_rgba(1, 1, 1, on ? 1 : 0.6f), CW_ALIGN_CENTER);
    }
    for (int i = 0; i < B.count; i++) {
      if (B.hit[p][i] == 1) continue;
      float dt = (float)(B.n[i].t - c->tick);
      if (dt > TRAVEL) break;
      if (dt < -24) continue;
      float y = 560 - dt * (430.0f / TRAVEL);
      float x = lx + w / 2 + (B.n[i].kind - 1) * 70;
      cw_color tint = B.hit[p][i] == 2 ? cw_rgba(0.35f, 0.35f, 0.35f, 0.8f) : (cw_color){ 0 };
      art_draw(GEM[B.n[i].kind], cw_v2(x, y), cw_v2(54, 54), t * 0.3f, tint);
      if (B.hit[p][i] != 2) art_glow("glow", cw_v2(x, y), 90, col_a(KCOL[B.n[i].kind], 0.35f));
    }
    art_world(true);
    ui_blob(cw_v2(lx + w / 2, 640 - pulse * 10), 26, p, 0, 0, 1 + 0.12f * pulse, B.combo[p] > 5 ? 1 : 0, p >= c->humans);
    if (B.judge_t[p] > 0) {
      const char *j = B.judge[p] == 2 ? "PERFECT" : B.judge[p] == 1 ? "good" : "miss";
      cw_color jc = B.judge[p] == 2 ? cw_rgba(1, 0.9f, 0.3f, 1) : B.judge[p] == 1 ? cw_rgba(0.7f, 1, 0.7f, 1) : cw_rgba(1, 0.5f, 0.5f, 1);
      ui_text(j, cw_v2(lx + w / 2, 400 - (20 - B.judge_t[p]) * 1.5f), 32, col_a(jc, B.judge_t[p] / 20.0f + 0.2f), CW_ALIGN_CENTER);
    }
    if (B.combo[p] >= 4) {
      char s[24];
      snprintf(s, sizeof s, "x%d  combo", B.combo[p]);
      ui_text(s, cw_v2(lx + w / 2, 150), 26, cw_rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER);
    }
  }
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "%d", (int)c->score[p]); }

const mg_def mg_beat = {
  .id = "beatbox", .name = "Beat Box", .tagline = "Hit the notes as they land on your line.",
  .how = { "A green, B red, X blue", "Press as the note reaches the ring", "Perfect hits build a combo" },
  .kind = "2D", .scene = NULL, .seconds = 0, .unlock_points = 12, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
