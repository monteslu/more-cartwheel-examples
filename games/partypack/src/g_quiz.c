/* Quick Quiz (2D, trivia by pad): eight questions, four answers on A, B, X
 * and Y. Answer first and right to score the most; a wrong answer scores
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
    Q.cpu_slot[p] = r < know[c->skill % 3] ? Q.right_slot : ws;
    Q.cpu_t[p] = 70 + cw_rng_int(&c->rng, 0, 220 - c->skill * 50);
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
  if (Q.phase == 0) {
    bool all = true;
    for (int p = 0; p < NP; p++) {
      if (Q.ans[p] < 0) {
        int s = -1;
        const mg_in *in = &c->in[p];
        if (in->pa) s = 0; else if (in->pb) s = 1; else if (in->pc) s = 2; else if (in->pd) s = 3;
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
  out->a = s == 0; out->b = s == 1; out->c = s == 2; out->d = s == 3;
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha;
  ui_backdrop(t, cw_rgba(0.1f, 0.3f, 0.5f, 1), cw_rgba(0.25f, 0.1f, 0.4f, 1));
  if (Q.q >= ROUNDS) return;
  const qa *q = &BANK[Q.pick[Q.q]];
  char s[64];
  snprintf(s, sizeof s, "Question %d of %d", Q.q + 1, ROUNDS);
  ui_text(s, cw_v2(STAGE_W / 2, 100), 26, cw_rgba(1, 1, 1, 0.7f), CW_ALIGN_CENTER);
  ui_panel(cw_rect_make(140, 135, 1000, 110), cw_rgba(0.05f, 0.05f, 0.18f, 0.9f), cw_rgba(1, 1, 1, 0.3f));
  ui_text_wrap(q->q, cw_rect_make(170, 160, 940, 90), 40, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  float f = Q.phase == 0 ? 1.0f - (float)Q.t / QTICKS : 0;
  ui_bar(cw_rect_make(240, 262, 800, 16), f, f < 0.25f ? cw_rgba(1, 0.4f, 0.3f, 1) : cw_rgba(0.4f, 0.9f, 1, 1));
  static const char LBL[4] = { 'A', 'B', 'X', 'Y' };
  for (int sl = 0; sl < 4; sl++) {
    float x = 140 + (sl % 2) * 510, y = 300 + (sl / 2) * 95;
    bool rev = Q.phase == 1, right = sl == Q.right_slot;
    cw_color fill = !rev ? cw_rgba(0.1f, 0.12f, 0.3f, 0.92f) : right ? cw_rgba(0.15f, 0.5f, 0.25f, 0.95f) : cw_rgba(0.1f, 0.1f, 0.2f, 0.6f);
    ui_panel(cw_rect_make(x, y, 490, 80), fill, cw_rgba(1, 1, 1, right && rev ? 0.9f : 0.2f));
    ui_button_glyph(cw_v2(x + 44, y + 40), 26, LBL[sl]);
    ui_text(q->a[Q.perm[Q.q][sl]], cw_v2(x + 90, y + 20), 34, cw_rgba(1, 1, 1, rev && !right ? 0.5f : 1), CW_ALIGN_LEFT);
    for (int p = 0; p < NP; p++) /* who picked it: shown after the reveal */
      if (Q.ans[p] == sl && (rev || c->human[p] == false)) {
        if (rev) ui_blob(cw_v2(x + 330 + p * 38, y + 40), 16, p, 0, 0, 1, right ? 1 : 3, p >= c->humans);
      }
  }
  for (int p = 0; p < NP; p++) { /* locked-in markers */
    float x = 190 + p * 270;
    ui_blob(cw_v2(x, 560), 30, p, 0, 0, Q.ans[p] >= 0 ? 1.1f : 1, Q.ans[p] < 0 ? 0 : Q.phase == 1 ? (Q.ans[p] == Q.right_slot ? 1 : 3) : 2, p >= c->humans);
    ui_text(Q.ans[p] >= 0 && Q.phase == 0 ? "locked" : Q.ans[p] < 0 ? "thinking" : "", cw_v2(x, 600), 22, cw_rgba(1, 1, 1, 0.7f), CW_ALIGN_CENTER);
    if (Q.phase == 1 && Q.gained[p]) { snprintf(s, sizeof s, "+%d", Q.gained[p]); ui_text(s, cw_v2(x, 625), 34, cw_rgba(0.6f, 1, 0.6f, 1), CW_ALIGN_CENTER); }
  }
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "%d pts", (int)c->score[p]); }

const mg_def mg_quiz = {
  .id = "quiz", .name = "Quick Quiz", .tagline = "Eight questions. Fast and right scores most.",
  .how = { "A B X Y pick the answer", "Faster correct answers score more", "Wrong answers score nothing" },
  .kind = "2D", .scene = NULL, .seconds = 0, .unlock_points = 20, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
