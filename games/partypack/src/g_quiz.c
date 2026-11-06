/* Quick Quiz (2D, trivia by pad): eight questions, four answers in a diamond,
 * each on the face button that sits there. Answer first and right to score the most; a wrong answer scores
 * nothing. Questions come from a built-in bank. */
#include "mg.h"

typedef struct { const char *q, *a[4]; int right; } qa;
/* the right answer's slot is shuffled per game; right holds its index in a[] */
static const qa BANK[] = {
  { "How many legs does a spider have?", { "Six", "Eight", "Ten", "Four" }, 1 },
  { "Which planet is closest to the Sun?", { "Venus", "Earth", "Mercury", "Mars" }, 2 },
  { "What is the capital of Japan?", { "Kyoto", "Osaka", "Tokyo", "Nagoya" }, 2 },
  { "What do bees make?", { "Honey", "Silk", "Milk", "Paper" }, 0 },
  { "How many minutes are in two hours?", { "100", "90", "180", "120" }, 3 },
  { "Which gas do plants take in?", { "Oxygen", "Carbon dioxide", "Helium", "Neon" }, 1 },
  { "What is the largest ocean?", { "Atlantic", "Indian", "Arctic", "Pacific" }, 3 },
  { "How many sides does a hexagon have?", { "Five", "Six", "Seven", "Eight" }, 1 },
  { "Which animal is the tallest?", { "Giraffe", "Elephant", "Camel", "Horse" }, 0 },
  { "What is 7 times 8?", { "54", "48", "56", "64" }, 2 },
  { "Water freezes at what temperature?", { "0 Celsius", "10 Celsius", "-10 Celsius", "100 Celsius" }, 0 },
  { "Which continent is Egypt in?", { "Asia", "Europe", "Africa", "Australia" }, 2 },
  { "What color do you get mixing blue and yellow?", { "Purple", "Green", "Orange", "Brown" }, 1 },
  { "How many days are in a leap year?", { "364", "365", "366", "367" }, 2 },
  { "Which is the hardest natural material?", { "Gold", "Iron", "Quartz", "Diamond" }, 3 },
  { "A baby kangaroo is called a what?", { "Joey", "Cub", "Pup", "Kit" }, 0 },
  { "What is the chemical symbol for water?", { "CO2", "H2O", "O2", "NaCl" }, 1 },
  { "Which instrument has black and white keys?", { "Drum", "Flute", "Piano", "Violin" }, 2 },
  { "How many continents are there?", { "Five", "Six", "Eight", "Seven" }, 3 },
  { "What is the square root of 81?", { "9", "8", "7", "11" }, 0 },
  { "Which planet has rings you can see easily?", { "Mars", "Saturn", "Mercury", "Earth" }, 1 },
  { "What is the largest mammal?", { "Elephant", "Hippo", "Blue whale", "Orca" }, 2 },
  { "How many strings does a standard guitar have?", { "Four", "Five", "Eight", "Six" }, 3 },
  { "Which season comes after winter?", { "Spring", "Summer", "Autumn", "Monsoon" }, 0 },
  { "What do caterpillars turn into?", { "Beetles", "Butterflies", "Bees", "Worms" }, 1 },
  { "What is 15 plus 27?", { "41", "43", "42", "52" }, 2 },
  { "Which direction does the Sun rise?", { "West", "North", "South", "East" }, 3 },
  { "How many players are on a soccer team on the field?", { "Eleven", "Nine", "Ten", "Twelve" }, 0 },
  { "Which is a primary color?", { "Green", "Red", "Pink", "Brown" }, 1 },
  { "What is the freezing point of water in Fahrenheit?", { "0", "100", "32", "50" }, 2 },
  { "Which organ pumps blood?", { "Lung", "Liver", "Kidney", "Heart" }, 3 },
  { "What is the capital of France?", { "Paris", "Lyon", "Nice", "Lille" }, 0 },
  { "How many zeros are in one thousand?", { "Two", "Three", "Four", "Five" }, 1 },
  { "Which bird cannot fly?", { "Sparrow", "Crow", "Penguin", "Owl" }, 2 },
  { "A triangle's angles add up to how many degrees?", { "90", "270", "360", "180" }, 3 },
};
#define NQ ((int)(sizeof BANK / sizeof BANK[0]))
#define ROUNDS 8
#define QTICKS (60 * 9)

static struct {
  int pick[ROUNDS], perm[ROUNDS][4]; /* perm[r][slot] = answer index shown in that slot */
  int q, t, phase;                   /* phase 0 asking, 1 reveal */
  int ans[NP], ans_t[NP];            /* slot chosen (-1 none), tick */
  int gained[NP], total[NP];
  int cpu_slot[NP], cpu_t[NP];
  int right_slot;
  float px[NP], py[NP]; /* the stick last tick: a flick picks an answer too (down south, right east, left west, up north) */
} Q;

static void setup_round(mg_ctx *c) {
  memset(Q.ans, -1, sizeof Q.ans);
  memset(Q.gained, 0, sizeof Q.gained);
  Q.t = 0;
  Q.phase = 0;
  for (int s = 0; s < 4; s++) if (Q.perm[Q.q][s] == BANK[Q.pick[Q.q]].right) Q.right_slot = s;
  for (int p = 0; p < NP; p++) { /* a CPU decides up front: how long it thinks, whether it knows */
    static const int know[3] = { 55, 75, 92 };
    int r = cw_rng_int(&c->rng, 0, 99);
    int wrong = cw_rng_int(&c->rng, 0, 2);
    int ws = (Q.right_slot + 1 + wrong) % 4;
    Q.cpu_slot[p] = r < know[c->cpu_skill[p]] ? Q.right_slot : ws;
    Q.cpu_t[p] = 70 + cw_rng_int(&c->rng, 0, 220 - c->cpu_skill[p] * 50);
  }
}

static void start(mg_ctx *c) {
  memset(&Q, 0, sizeof Q);
  int used[ROUNDS];
  for (int r = 0; r < ROUNDS; r++) {
    int q, ok;
    do {
      q = cw_rng_int(&c->rng, 0, NQ - 1);
      ok = 1;
      for (int j = 0; j < r; j++) if (used[j] == q) ok = 0;
    } while (!ok);
    used[r] = q;
    Q.pick[r] = q;
    int pm[4] = { 0, 1, 2, 3 };
    for (int i = 3; i > 0; i--) { int j = cw_rng_int(&c->rng, 0, i); int t = pm[i]; pm[i] = pm[j]; pm[j] = t; }
    for (int s = 0; s < 4; s++) Q.perm[r][s] = pm[s];
  }
  setup_round(c);
}

static void tick(mg_ctx *c) {
  Q.t++;
  float ox[NP], oy[NP];
  for (int p = 0; p < NP; p++) { ox[p] = Q.px[p]; oy[p] = Q.py[p]; Q.px[p] = c->in[p].x; Q.py[p] = c->in[p].y; }
  if (Q.phase == 0) {
    bool all = true;
    for (int p = 0; p < NP; p++) {
      if (Q.ans[p] < 0) {
        int s = -1;
        const mg_in *in = &c->in[p];
        if (in->psouth) s = 0; else if (in->peast) s = 1; else if (in->pwest) s = 2; else if (in->pnorth) s = 3;
        else if (in->y > 0.6f && oy[p] <= 0.6f) s = 0;
        else if (in->x > 0.6f && ox[p] <= 0.6f) s = 1;
        else if (in->x < -0.6f && ox[p] >= -0.6f) s = 2;
        else if (in->y < -0.6f && oy[p] >= -0.6f) s = 3;
        if (s >= 0) {
          Q.ans[p] = s; Q.ans_t[p] = Q.t;
          sfx(SFX_BLIP, 0.6f, 1.2f);
        }
      }
      all = all && Q.ans[p] >= 0;
    }
    if (all || Q.t >= QTICKS) {
      Q.phase = 1; Q.t = 0;
      for (int p = 0; p < NP; p++) {
        if (Q.ans[p] == Q.right_slot) {
          int left = QTICKS - Q.ans_t[p];
          Q.gained[p] = 100 + left * 100 / QTICKS;
          Q.total[p] += Q.gained[p];
          sfx(SFX_COIN, 0.5f, 1);
        } else if (Q.ans[p] >= 0) sfx(SFX_BUZZ, 0.3f, 1);
        c->score[p] = (float)Q.total[p];
      }
    }
  } else if (Q.t > 150) {
    Q.q++;
    if (Q.q >= ROUNDS) c->done = true; else setup_round(c);
  }
}

static void think(mg_ctx *c, int p, mg_in *out) {
  (void)c;
  memset(out, 0, sizeof *out);
  if (Q.phase != 0 || Q.ans[p] >= 0 || Q.t < Q.cpu_t[p]) return;
  int s = Q.cpu_slot[p];
  out->south = s == 0; out->east = s == 1; out->west = s == 2; out->north = s == 3;
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha;
  art_world(false);
  ui_backdrop(t, cw_rgba(0.1f, 0.3f, 0.5f, 1), cw_rgba(0.25f, 0.1f, 0.4f, 1));
  for (int i = 0; i < 4; i++) { /* game-show spotlights */
    float sw = sinf(t * 0.5f + (float)i * 1.7f);
    cw_vec2 top = cw_v2(150 + (float)i * 330, -10), foot = cw_v2(top.x + sw * 180, 520);
    cw_color bc = cw_rgba(0.8f, 0.9f, 1, 0.07f);
    cw_vertex2d v[4] = { { cw_v2(top.x - 10, top.y), cw_v2(0, 0), bc }, { cw_v2(top.x + 10, top.y), cw_v2(1, 0), bc }, { cw_v2(foot.x + 100, foot.y), cw_v2(1, 1), col_a(bc, 0) }, { cw_v2(foot.x - 100, foot.y), cw_v2(0, 1), col_a(bc, 0) } };
    static const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    cw_r2d_set_blend(CW_BLEND_ADD);
    cw_r2d_triangles((cw_texture){ 0 }, v, 4, idx, 6);
    cw_r2d_set_blend(CW_BLEND_ALPHA);
  }
  if (Q.q >= ROUNDS) return;
  const qa *q = &BANK[Q.pick[Q.q]];
  char s[64];
  snprintf(s, sizeof s, "Question %d of %d", Q.q + 1, ROUNDS);
  /* At 100% the page is as it always was. At a larger text size every block moves down by what the one over it grew,
   * and when the diamond would then run into the markers it closes up: the west and east answers stand at the sides,
   * level between the north and south ones, so it is two answers tall, not three */
  float sh = ui_hud_shift(), y = 100 + sh;
  ui_text(s, cw_v2(STAGE_W / 2, y), 26, cw_rgba(1, 1, 1, 0.7f), CW_ALIGN_CENTER);
  y = 135 + sh + ui_grew(26);
  float qh = ui_wrap_h(q->q, 940, 40), ph = fmaxf(110, qh + 40);
  art_nine("panel", cw_rect_make(140, y, 1000, ph), 26, 1.0f, cw_rgba(0.85f, 0.9f, 1.0f, 0.95f));
  cw_r2d_round_rect_lines(cw_rect_make(143, y + 3, 994, ph - 6), 16, 3, 8, cw_rgba(1, 1, 1, 0.35f));
  ui_text_wrap(q->q, cw_rect_make(170, y + 25, 940, qh), 40, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  float f = Q.phase == 0 ? 1.0f - (float)Q.t / QTICKS : 0;
  y += ph + 17;
  ui_bar(cw_rect_make(240, y, 800, 16), f, f < 0.25f ? cw_rgba(1, 0.4f, 0.3f, 1) : cw_rgba(0.4f, 0.9f, 1, 1));
  float dy = y + 30 - 292; /* how far the diamond moved down */
  /* the markers' row from the foot up: the line under each blob, the blob */
  float mk_text = fminf(600, STAGE_H - 10 - ui_line_h(34) - (ui_line_h(22) > 0 ? 0 : 0)), blob_y = fminf(560, mk_text - 40);
  if (ui_grow(22) > 1.001f) mk_text = STAGE_H - 10 - fmaxf(ui_line_h(22), ui_line_h(34)), blob_y = mk_text - 36;
  /* the answers sit as the buttons do, in a diamond: slot 0 south, 1 east, 2 west, 3 north, so the slot, its button and
   * the flick all point the same way */
  static const char *SLOT_KEY[4] = { "{btn_south}", "{btn_east}", "{btn_west}", "{btn_north}" };
  static const float SLOT_X[4] = { 395, 650, 140, 395 }, SLOT_Y[4] = { 444, 368, 368, 292 };
  float sw = 490, shh = 68, ax[4], ay[4], gw = 0, aw = 0;
  for (int sl = 0; sl < 4; sl++) { gw = fmaxf(gw, ui_prompt_size(SLOT_KEY[sl], -1, 34).x); aw = fmaxf(aw, ui_text_w(q->a[Q.perm[Q.q][sl]], 32)); }
  float ah = ui_line_h(32), tight = 0;
  shh = fmaxf(68, fmaxf(ah, ui_prompt_size("{btn_south}", -1, 34).y) + 14 * ui_grow(32));
  for (int sl = 0; sl < 4; sl++) ax[sl] = SLOT_X[sl], ay[sl] = SLOT_Y[sl] + dy;
  if (ay[0] + shh > blob_y - 34) { /* closed up: three columns, two answers tall */
    sw = fminf(490, (STAGE_W - 40 - 32) / 3.0f);
    float top = y + 30, half = shh * 0.5f + 4;
    ax[2] = 20, ay[2] = top + half;                      /* west */
    ax[3] = STAGE_W / 2 - sw / 2, ay[3] = top;           /* north */
    ax[0] = ax[3], ay[0] = top + 2 * half;               /* south */
    ax[1] = STAGE_W - 20 - sw, ay[1] = top + half;       /* east */
    tight = 1;
  }
  (void)aw;
  for (int sl = 0; sl < 4; sl++) {
    float x = ax[sl], yy = ay[sl];
    bool rev = Q.phase == 1, right = sl == Q.right_slot;
    cw_color fill = !rev ? cw_rgba(0.45f, 0.55f, 0.95f, 0.95f) : right ? cw_rgba(0.4f, 0.95f, 0.5f, 1) : cw_rgba(0.3f, 0.3f, 0.45f, 0.65f);
    art_nine("tile", cw_rect_make(x, yy, sw, shh), 24, 0.9f, fill);
    if (right && rev) art_glow("glow", cw_v2(x + sw * 0.5f, yy + shh * 0.5f), 560, cw_rgba(0.4f, 1, 0.5f, 0.3f));
    float gy = yy + (shh - ui_prompt_size(SLOT_KEY[sl], -1, 34).y) * 0.5f, ty = yy + (shh - ah) * 0.5f;
    if (!tight && ui_grow(32) < 1.001f) gy = yy + 16, ty = yy + 15;
    cw_vec2 g = ui_prompt(SLOT_KEY[sl], -1, cw_v2(x + 22, gy), 34, cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
    ui_text(q->a[Q.perm[Q.q][sl]], cw_v2(x + 34 + (g.x > 40 ? g.x : 40), ty), 32, cw_rgba(1, 1, 1, rev && !right ? 0.55f : 1), CW_ALIGN_LEFT);
    for (int p = 0; p < NP; p++) /* who picked it: shown after the reveal */
      if (Q.ans[p] == sl && (rev || c->human[p] == false)) {
        if (rev) ui_blob(cw_v2(x + sw - 160 + p * 38, yy + shh * 0.5f), 16, p, 0, 0, 1, right ? 1 : 3, !c->human[p]);
      }
  }
  (void)gw;
  for (int p = 0; p < NP; p++) { /* locked-in markers */
    float x = 190 + p * 270;
    ui_blob(cw_v2(x, blob_y), 30, p, 0, 0, Q.ans[p] >= 0 ? 1.1f : 1, Q.ans[p] < 0 ? 0 : Q.phase == 1 ? (Q.ans[p] == Q.right_slot ? 1 : 3) : 2, !c->human[p]);
    ui_text(Q.ans[p] >= 0 && Q.phase == 0 ? "locked" : Q.ans[p] < 0 ? "thinking" : "", cw_v2(x, mk_text), 22, cw_rgba(1, 1, 1, 0.7f), CW_ALIGN_CENTER);
    if (Q.phase == 1 && Q.gained[p]) { snprintf(s, sizeof s, "+%d", Q.gained[p]); ui_text(s, cw_v2(x, ui_grow(22) > 1.001f ? mk_text : 625), 34, cw_rgba(0.6f, 1, 0.6f, 1), CW_ALIGN_CENTER); }
  }
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "%d pts", (int)c->score[p]); }

const mg_def mg_quiz = {
  .id = "quiz", .name = "Quick Quiz", .tagline = "Eight questions. Fast and right scores most.",
  .how = { "Press the answer's button, or flick " MOVE4 " toward it", "Faster correct answers score more", "Wrong answers score nothing" },
  .controls = MOVE4,
  .kind = "2D", .scene = NULL, .seconds = 0, .unlock_points = 20, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
