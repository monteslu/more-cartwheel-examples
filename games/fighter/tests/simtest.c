/* Rules tests for the fight simulation: frame data, blocking, motions, combos,
 * throws, rounds, and a long computer-vs-computer soak. Plain C, no engine
 * window: tests/run-simtest.sh builds and runs it. Exit 0 = all passed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../sim.h"

static int passed, failed;
#define CHECK(c) do { if (c) passed++; else { failed++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static char *slurp(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { printf("cannot open %s\n", path); exit(2); }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  char *b = malloc((size_t)n + 1); if (fread(b, 1, (size_t)n, f) != (size_t)n) exit(2); b[n] = 0; fclose(f); return b;
}
static ft_roster R;
static ft_game G;
static ft_input NONE;

static void tick(ft_input a, ft_input b) { ft_input in[2] = { a, b }; ft_step(&G, in); }
static ft_input btn(uint16_t b) { ft_input i = { 0 }; i.btn = b; return i; }
static ft_input dirbtn(int l, int r, int u, int d, uint16_t b) { ft_input i = { (uint8_t)l, (uint8_t)r, (uint8_t)u, (uint8_t)d, b }; return i; }

/* a fresh fight (training rules: no round flow), p0 at x0, p1 at x1 */
static void fresh(int d0, int d1, int x0, int x1) {
  memset(&G, 0, sizeof G);
  ft_game_start(&G, &R, d0, d1);
  G.phase = P_FIGHT; G.timer = 30000;
  for (int i = 0; i < 2; i++) G.f[i].state = S_IDLE;
  G.f[0].x = x0; G.f[1].x = x1;
}

static void test_parse(void) {
  CHECK(R.n == 4);
  CHECK(!strcmp(R.d[0].id, "bolt") && R.d[0].mv[M_5L].su == 4 && R.d[0].mv[M_5L].ac == 2 && R.d[0].mv[M_5L].rc == 7);
  CHECK(R.d[3].mo[0].charge == 1); /* Mara: charge back, forward */
  CHECK(R.d[1].mv[M_SP1].flags & FM_THROW);
  ft_roster bad; const char *err = NULL;
  CHECK(ft_roster_load(&bad, "fighter a hp=100\nmove a 5X su=1\n", &err) < 0 && err);
  CHECK(ft_roster_load(&bad, "fighter a sp1=206\n", &err) < 0);
  CHECK(ft_roster_load(&bad, "nonsense line\n", &err) < 0);
  CHECK(ft_roster_load(&bad, "move * 5L su=4\nfighter a\n", &err) < 0); /* a move with no active frames */
}

static void test_walk_and_facing(void) {
  fresh(0, 1, -1500, 1500);
  for (int i = 0; i < 30; i++) tick(dirbtn(0, 1, 0, 0, 0), NONE);
  CHECK(G.f[0].x > -1500 + 30 * 30 && G.f[0].state == S_WALKF);
  CHECK(G.f[0].facing == 1 && G.f[1].facing == -1);
  /* they cannot walk through each other */
  for (int i = 0; i < 200; i++) tick(dirbtn(0, 1, 0, 0, 0), NONE);
  CHECK(G.f[1].x - G.f[0].x >= 540);
  /* walls */
  fresh(0, 1, -4300, 4300);
  for (int i = 0; i < 100; i++) tick(dirbtn(1, 0, 0, 0, 0), NONE);
  CHECK(G.f[0].x >= -4400);
}

/* press a button once and run the attacker's frames until idle; returns frames spent in the attack state */
static int attack_frames(int slot_btn_dir_crouch, uint16_t b) {
  fresh(0, 1, -450, 450);
  G.f[1].x = 3500; /* out of range: a whiff */
  tick(dirbtn(0, 0, 0, slot_btn_dir_crouch, b), NONE);
  int n = 0;
  for (int i = 0; i < 80 && G.f[0].state == S_ATTACK; i++) { n++; tick(NONE, NONE); }
  return n;
}
static void test_frame_data(void) {
  /* the move lasts its startup + active + recovery frames */
  for (int s = 0; s < 3; s++) {
    const ft_move *m = &R.d[0].mv[M_5L + s];
    int n = attack_frames(0, (uint16_t)(1 << s));
    CHECK(n >= ft_move_total(m) - 1 && n <= ft_move_total(m) + 1);
  }
  const ft_move *m = &R.d[0].mv[M_2L];
  int n = attack_frames(1, B_L);
  CHECK(n >= ft_move_total(m) - 1 && n <= ft_move_total(m) + 1);
}

/* the frame advantage the sim measures equals the table's number */
static int measure_adv(int fighter, int slot_btn, bool block, bool crouch) {
  fresh(fighter, 0, -450, 450);
  G.f[1].x = G.f[0].x + 800;
  ft_input a = dirbtn(0, 0, 0, crouch, (uint16_t)(1 << slot_btn));
  tick(a, block ? dirbtn(0, 1, 0, 0, 0) : NONE); /* the defender holds back (the right button direction) */
  for (int i = 0; i < 120; i++) {
    tick(NONE, block ? dirbtn(0, 1, 0, 0, 0) : NONE);
    if (G.adv_valid) return G.adv;
  }
  return -999;
}
static void test_advantage(void) {
  const ft_move *m = &R.d[0].mv[M_5M];
  int hit = measure_adv(0, 1, false, false), blk = measure_adv(0, 1, true, false);
  printf("  5M measured on hit %+d (table %+d), on block %+d (table %+d)\n", hit, ft_advantage(m, false), blk, ft_advantage(m, true));
  CHECK(hit == ft_advantage(m, false));
  CHECK(blk == ft_advantage(m, true));
  m = &R.d[0].mv[M_5L];
  CHECK(measure_adv(0, 0, false, false) == ft_advantage(m, false));
  CHECK(measure_adv(0, 0, true, false) == ft_advantage(m, true));
  m = &R.d[2].mv[M_5L];
  /* the second fighter's data differs: a faster jab */
  fresh(2, 0, -450, 450); (void)m;
  CHECK(R.d[2].mv[M_5L].su < R.d[0].mv[M_5L].su);
}

static void test_block_and_levels(void) {
  /* a low must be blocked crouching: standing block takes the hit */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 800;
  tick(dirbtn(0, 0, 0, 1, B_L), dirbtn(0, 1, 0, 0, 0));
  int hp = G.f[1].hp;
  for (int i = 0; i < 20; i++) tick(NONE, dirbtn(0, 1, 0, 0, 0));
  CHECK(G.f[1].hp < hp); /* 2L is a low: standing block fails */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 800;
  tick(dirbtn(0, 0, 0, 1, B_L), dirbtn(0, 1, 0, 1, 0));
  hp = G.f[1].hp;
  for (int i = 0; i < 20; i++) tick(NONE, dirbtn(0, 1, 0, 1, 0));
  CHECK(G.f[1].hp == hp); /* crouch block holds */
  /* chip damage only from specials */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 800;
  tick(btn(B_L), dirbtn(0, 1, 0, 0, 0));
  for (int i = 0; i < 20; i++) tick(NONE, dirbtn(0, 1, 0, 0, 0));
  CHECK(G.f[1].hp == R.d[0].hp);
}

/* feed a motion then a button: dirs are numpad digits for a fighter facing right */
static void motion(const char *dirs, uint16_t b, ft_input other) {
  for (const char *p = dirs; *p; p++) {
    int d = *p - '0';
    int x = (d % 3 == 0) - (d % 3 == 1), y = d >= 7 ? 1 : (d <= 3 ? -1 : 0);
    bool last = !p[1];
    tick(dirbtn(x < 0, x > 0, y > 0, y < 0, last ? b : 0), other);
    tick(dirbtn(x < 0, x > 0, y > 0, y < 0, last ? b : 0), other);
  }
}
static void test_specials(void) {
  fresh(0, 0, -2500, 2500);
  motion("236", B_L, NONE);
  bool seen = false;
  for (int i = 0; i < 40; i++) { tick(NONE, NONE); for (int p = 0; p < 8; p++) if (G.proj[p].live) seen = true; }
  CHECK(seen); /* the fireball spawned from the motion */
  for (int i = 0; i < 90; i++) tick(NONE, NONE);
  CHECK(G.f[1].hp < R.d[0].hp); /* and hit the far fighter */
  /* a wrong motion does nothing */
  fresh(0, 0, -2500, 2500);
  motion("632", B_L, NONE);
  CHECK(G.f[0].state != S_ATTACK || G.f[0].mv != M_SP1);
  /* the rising strike is invulnerable on its first frames and beats a jump-in */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 1000;
  tick(NONE, dirbtn(0, 0, 1, 0, 0));
  for (int i = 0; i < 4; i++) tick(NONE, NONE);
  motion("623", B_H, NONE);
  for (int i = 0; i < 70; i++) tick(NONE, NONE);
  CHECK(G.f[0].hp == R.d[0].hp || G.f[1].hp < R.d[0].hp); /* never loses to the jump without trading */
  /* the charge motion needs the charge */
  fresh(3, 0, -2500, 2500);
  for (int i = 0; i < 40; i++) tick(dirbtn(1, 0, 0, 0, 0), NONE); /* hold back (left, facing right) */
  motion("6", B_L, NONE);
  CHECK(G.f[0].state == S_ATTACK && G.f[0].mv == M_SP1);
  fresh(3, 0, -2500, 2500);
  for (int i = 0; i < 8; i++) tick(dirbtn(1, 0, 0, 0, 0), NONE);
  motion("6", B_L, NONE);
  CHECK(!(G.f[0].state == S_ATTACK && G.f[0].mv == M_SP1));
}

static void test_combo_and_scaling(void) {
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 750;
  /* a light cancels into a medium and a heavy while it connects: a chain */
  tick(btn(B_L), NONE);
  for (int i = 0; i < 5; i++) tick(NONE, NONE);
  tick(btn(B_M), NONE);
  for (int i = 0; i < 12; i++) tick(NONE, NONE);
  tick(btn(B_H), NONE);
  for (int i = 0; i < 60; i++) tick(NONE, NONE);
  CHECK(G.f[1].hp < R.d[0].hp - 100);
  CHECK(G.last_combo >= 3);
  /* the scaling table: a later hit in a combo does less than the same hit on its own */
  CHECK(G.last_dmg < R.d[0].mv[M_5H].dmg);
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 750;
  /* a launcher then a juggle: 5H sends them up; hits keep landing while airborne */
  tick(btn(B_H), NONE);
  for (int i = 0; i < 60; i++) tick(NONE, NONE);
  CHECK(G.f[1].hp < R.d[0].hp);
  CHECK(G.f[1].state == S_AIRHIT || G.f[1].state == S_KD || G.f[1].state == S_GETUP || G.f[1].state == S_IDLE);
}

static void test_throw(void) {
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 700;
  tick(btn(B_T), NONE);
  for (int i = 0; i < 90; i++) tick(NONE, NONE);
  CHECK(G.f[1].hp == R.d[0].hp - R.d[0].mv[M_THROW].dmg);
  /* a throw ignores block */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 700;
  tick(btn(B_T), dirbtn(0, 1, 0, 0, 0));
  for (int i = 0; i < 90; i++) tick(NONE, dirbtn(0, 1, 0, 0, 0));
  CHECK(G.f[1].hp < R.d[0].hp);
  /* a defender who presses throw in time teches it */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 700;
  tick(btn(B_T), btn(B_T));
  for (int i = 0; i < 90; i++) tick(NONE, NONE);
  CHECK(G.f[1].hp == R.d[0].hp);
}

static void test_meter_super(void) {
  fresh(0, 0, -2500, 2500);
  G.f[0].meter = FT_METER_MAX;
  motion("236236", B_M, NONE);
  CHECK(G.f[0].state == S_ATTACK && G.f[0].mv == M_SUPER);
  CHECK(G.f[0].meter == 0);
  fresh(0, 0, -2500, 2500);
  G.f[0].meter = 400;
  motion("236236", B_M, NONE);
  CHECK(!(G.f[0].state == S_ATTACK && G.f[0].mv == M_SUPER)); /* not enough meter */
}

static void test_rounds_ko(void) {
  memset(&G, 0, sizeof G);
  G.cpu[0] = G.cpu[1] = true;
  G.cpu_level[0] = 2; G.cpu_level[1] = 2;
  ft_game_start(&G, &R, 0, 1);
  CHECK(G.phase == P_INTRO);
  int guard = 0;
  while (G.phase != P_MATCH_END && guard++ < 60 * 60 * 20) tick(NONE, NONE);
  CHECK(G.phase == P_MATCH_END);
  CHECK(G.match_winner == 0 || G.match_winner == 1);
  CHECK(G.f[G.match_winner].wins == 2);
}

/* a long computer-vs-computer soak: every pairing, every tick the state is sane; no soft-lock */
static void test_soak(int matches) {
  int locked = 0, ends = 0;
  uint32_t fold = 2166136261u;
  for (int m = 0; m < matches; m++) {
    memset(&G, 0, sizeof G);
    G.cpu[0] = G.cpu[1] = true;
    G.cpu_level[0] = (uint8_t)(m % 3); G.cpu_level[1] = (uint8_t)((m / 3) % 3);
    ft_game_start(&G, &R, m % R.n, (m / R.n + 1 + m) % R.n);
    G.rng = (uint32_t)(m * 7919 + 1);
    int guard = 0;
    uint32_t last_hash = 0; int same = 0;
    while (G.phase != P_MATCH_END && guard++ < 60 * 60 * 15) {
      tick(NONE, NONE);
      for (int i = 0; i < 2; i++) {
        const ft_fighter *f = &G.f[i];
        if (f->x < -FT_WALL - 1 || f->x > FT_WALL + 1 || f->y < 0 || f->hp < 0 || f->hp > R.d[f->def].hp || f->meter < 0 || f->meter > FT_METER_MAX) {
          failed++; printf("FAIL soak match %d: bad state x=%d y=%d hp=%d meter=%d\n", m, f->x, f->y, f->hp, f->meter); return;
        }
      }
      uint32_t h = ft_hash(&G);
      if (G.hitstop == 0 && h == last_hash) same++; else same = 0;
      last_hash = h;
    }
    if (G.phase == P_MATCH_END) ends++; else { locked++; printf("  soak match %d never ended (phase %d round %d)\n", m, G.phase, G.round_no); }
    fold = (fold ^ ft_hash(&G)) * 16777619u;
  }
  CHECK(locked == 0);
  printf("  soak: %d matches, %d ended, fold %08x\n", matches, ends, fold);
}

static void test_determinism(void) {
  uint32_t hs[2];
  for (int run = 0; run < 2; run++) {
    memset(&G, 0, sizeof G);
    G.cpu[0] = G.cpu[1] = true; G.cpu_level[0] = 1; G.cpu_level[1] = 2;
    ft_game_start(&G, &R, 0, 2);
    for (int i = 0; i < 6000; i++) tick(NONE, NONE);
    hs[run] = ft_hash(&G);
  }
  CHECK(hs[0] == hs[1]);
  /* the control: a different seed must give a different state */
  memset(&G, 0, sizeof G);
  G.cpu[0] = G.cpu[1] = true; G.cpu_level[0] = 1; G.cpu_level[1] = 2;
  ft_game_start(&G, &R, 0, 2);
  G.rng = 99;
  for (int i = 0; i < 6000; i++) tick(NONE, NONE);
  CHECK(ft_hash(&G) != hs[0]);
  printf("  hash after 6000 ticks: %08x\n", hs[0]);
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "assets/data/moves.txt";
  int soak = argc > 2 ? atoi(argv[2]) : 24;
  const char *err = NULL;
  char *txt = slurp(path);
  if (ft_roster_load(&R, txt, &err) < 0) { printf("moves: %s\n", err); return 2; }
  free(txt);
  test_parse(); test_walk_and_facing(); test_frame_data(); test_advantage(); test_block_and_levels(); test_specials();
  test_combo_and_scaling(); test_throw(); test_meter_super(); test_rounds_ko(); test_determinism(); test_soak(soak);
  printf("simtest: %d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
