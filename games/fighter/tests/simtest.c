/* Rules tests for the fight simulation: the punch / kick / block controls, frame data, the high / mid / low guard
 * triangle, parries and guard breaks, motions, combos,
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
  CHECK(!strcmp(R.d[0].id, "bolt") && R.d[0].mv[M_5P].su == 4 && R.d[0].mv[M_5P].ac == 2 && R.d[0].mv[M_5P].rc == 8);
  CHECK(R.d[3].mo[0].charge == 1); /* Mara: charge back, forward */
  CHECK(R.d[1].mv[M_SP1].flags & FM_THROW);
  CHECK(R.d[0].mbtn[0] == B_P && R.d[0].mbtn[2] == B_K && R.d[0].mbtn[3] == B_ATTACKS); /* 236P, 214K, 236236X */
  CHECK(!strcmp(R.d[0].mv[M_SP1].name, "Fireball") && !strcmp(R.d[0].mv[M_JP].name, "Hop punch"));
  CHECK(R.d[0].mv[M_8P].lv == LV_HIGH && R.d[0].mv[M_2K].lv == LV_LOW && R.d[0].mv[M_5K].lv == LV_MID);
  ft_roster bad; const char *err = NULL;
  CHECK(ft_roster_load(&bad, "fighter a hp=100\nmove a 5X su=1\n", &err) < 0 && err);
  CHECK(ft_roster_load(&bad, "fighter a sp1=206P\n", &err) < 0);
  CHECK(ft_roster_load(&bad, "fighter a sp1=236 sp2=623P sp3=214K super=236236X\n", &err) < 0); /* a special with no button */
  CHECK(ft_roster_load(&bad, "nonsense line\n", &err) < 0);
  CHECK(ft_roster_load(&bad, "move * 5P su=4\nfighter a\n", &err) < 0); /* a move with no active frames */
}

/* direction (numpad, facing right) and buttons */
static ft_input dpad(int d, uint16_t b) {
  int x = (d % 3 == 0) - (d % 3 == 1), y = d >= 7 ? 1 : (d <= 3 ? -1 : 0);
  return dirbtn(x < 0, x > 0, y > 0, y < 0, b);
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
  /* up or down held alone does nothing: no jump, no crouch (they only pick the height of an attack or a guard) */
  fresh(0, 1, -1500, 1500);
  for (int i = 0; i < 30; i++) tick(dpad(8, 0), NONE);
  CHECK(G.f[0].y == 0 && G.f[0].state == S_IDLE);
  for (int i = 0; i < 30; i++) tick(dpad(2, 0), NONE);
  CHECK(G.f[0].state == S_IDLE);
  /* up tapped twice: a hop */
  fresh(0, 1, -1500, 1500);
  tick(dpad(8, 0), NONE); tick(dpad(8, 0), NONE); tick(NONE, NONE); tick(NONE, NONE); tick(dpad(8, 0), NONE);
  int top = 0;
  for (int i = 0; i < 60; i++) { tick(NONE, NONE); if (G.f[0].y > top) top = G.f[0].y; }
  CHECK(top > 600);
  CHECK(G.f[0].y == 0); /* and lands */
  /* BLOCK held: a standing guard; with down: a low guard; it roots the fighter */
  fresh(0, 1, -1500, 1500);
  for (int i = 0; i < 10; i++) tick(dirbtn(0, 1, 0, 0, B_G), NONE);
  CHECK(G.f[0].state == S_BLOCK && !G.f[0].crouch_block && G.f[0].x == -1500);
  tick(dirbtn(0, 0, 0, 1, B_G), NONE);
  CHECK(G.f[0].state == S_BLOCK && G.f[0].crouch_block);
  tick(NONE, NONE);
  CHECK(G.f[0].state == S_IDLE);
}

static void test_slots(void) {
  static const struct { int dir; uint16_t b; int slot; } T[] = {
    { 5, B_P, M_5P }, { 8, B_P, M_8P }, { 2, B_P, M_2P }, { 6, B_P, M_6P }, { 5, B_K, M_5K }, { 8, B_K, M_8K }, { 2, B_K, M_2K }, { 6, B_K, M_6K },
    { 9, B_P, M_8P }, { 3, B_K, M_2K }, { 4, B_P, M_5P }, { 7, B_K, M_8K },
  };
  for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
    fresh(0, 1, -450, 3500);
    tick(dpad(T[i].dir, 0), NONE);
    tick(dpad(T[i].dir, T[i].b), NONE);
    if (!(G.f[0].state == S_ATTACK && G.f[0].mv == T[i].slot))
      printf("  slot: dir %d btn %d gave state %d move %d, wanted %d\n", T[i].dir, T[i].b, G.f[0].state, G.f[0].mv, T[i].slot);
    CHECK(G.f[0].state == S_ATTACK && G.f[0].mv == T[i].slot);
  }
  CHECK(ft_slot_for(5, B_G) < 0);
  /* a hop attack: punch in the air */
  fresh(0, 1, -1500, 1500);
  tick(dpad(8, 0), NONE); tick(NONE, NONE); tick(dpad(8, 0), NONE);
  for (int i = 0; i < 10; i++) tick(NONE, NONE);
  tick(btn(B_P), NONE);
  CHECK(G.f[0].state == S_ATTACK && G.f[0].mv == M_JP);
}

/* press a direction + button once at range and count the attacker's frames (the press frame included) */
static int attack_frames(int fighter, int dir, uint16_t b) {
  fresh(fighter, 1, -450, 3500); /* out of range: a whiff */
  tick(dpad(dir, b), NONE);
  int n = 1;
  for (int i = 0; i < 80 && G.f[0].state == S_ATTACK; i++) { tick(NONE, NONE); if (G.f[0].state == S_ATTACK) n++; }
  return n;
}
static const struct { int slot, dir; uint16_t b; } NORMALS[] = {
  { M_5P, 5, B_P }, { M_8P, 8, B_P }, { M_2P, 2, B_P }, { M_6P, 6, B_P }, { M_5K, 5, B_K }, { M_8K, 8, B_K }, { M_2K, 2, B_K }, { M_6K, 6, B_K },
};
static void test_frame_data(void) {
  /* every ground normal of every fighter lasts exactly its startup + active + recovery frames */
  int checked = 0;
  for (int f = 0; f < R.n; f++)
    for (unsigned k = 0; k < 8; k++) {
      const ft_move *m = &R.d[f].mv[NORMALS[k].slot];
      int n = attack_frames(f, NORMALS[k].dir, NORMALS[k].b);
      if (n != ft_move_total(m)) printf("  %s %s lasted %d frames, table %d\n", R.d[f].id, FT_MOVE_ID[NORMALS[k].slot], n, ft_move_total(m));
      CHECK(n == ft_move_total(m));
      checked++;
    }
  printf("  move lengths: %d moves equal the table\n", checked);
}

/* the frame advantage the sim measures equals the table's number; the defender guards at the right height */
static int measure_adv(int fighter, int dir, uint16_t b, bool block, bool low) {
  fresh(fighter, 0, -450, 450);
  G.f[1].x = G.f[0].x + 800;
  G.f[1].parry_lock = FT_PARRY_LOCK; /* a held guard, not a timed press */
  ft_input guard = dirbtn(0, 0, 0, low, block ? B_G : 0);
  tick(dpad(dir, b), guard);
  for (int i = 0; i < 120; i++) {
    tick(NONE, guard);
    if (G.adv_valid) return G.adv;
  }
  return -999;
}
static void test_advantage(void) {
  int checked = 0;
  for (int f = 0; f < R.n; f++)
    for (unsigned k = 0; k < 8; k++) {
      const ft_move *m = &R.d[f].mv[NORMALS[k].slot];
      if (m->mv) continue; /* the stepping moves connect after their step: the training readout shows them */
      bool low = m->lv == LV_LOW;
      if (!m->kd && !m->launch) {
        int hit = measure_adv(f, NORMALS[k].dir, NORMALS[k].b, false, false);
        if (hit != ft_advantage(m, false)) printf("  %s %s on hit %+d, table %+d\n", R.d[f].id, FT_MOVE_ID[NORMALS[k].slot], hit, ft_advantage(m, false));
        CHECK(hit == ft_advantage(m, false));
        checked++;
      }
      int blk = measure_adv(f, NORMALS[k].dir, NORMALS[k].b, true, low);
      if (blk != ft_advantage(m, true)) printf("  %s %s on block %+d, table %+d\n", R.d[f].id, FT_MOVE_ID[NORMALS[k].slot], blk, ft_advantage(m, true));
      CHECK(blk == ft_advantage(m, true));
      checked++;
    }
  printf("  frame advantage: %d measurements equal the table\n", checked);
}

/* fighter att attacks with dir+btn at close range; the defender holds guard; returns the defender's life lost */
static int exchange(int att, int dir, uint16_t b, ft_input guard) {
  fresh(att, 0, -450, 450);
  G.f[1].x = G.f[0].x + 800;
  G.f[1].parry_lock = FT_PARRY_LOCK;
  tick(dpad(dir, b), guard);
  for (int i = 0; i < 40; i++) tick(NONE, guard);
  return R.d[0].hp - G.f[1].hp;
}
static void test_block_triangle(void) {
  ft_input stand = btn(B_G), low = dirbtn(0, 0, 0, 1, B_G), open = NONE;
  /* a low: the standing guard fails, the low guard holds */
  CHECK(exchange(0, 2, B_P, stand) > 0);
  CHECK(exchange(0, 2, B_P, low) == 0);
  CHECK(exchange(0, 2, B_K, stand) > 0);
  CHECK(exchange(0, 2, B_K, low) == 0);
  /* a mid: either guard holds, no guard is hit (the control) */
  CHECK(exchange(0, 5, B_P, stand) == 0);
  CHECK(exchange(0, 5, B_P, low) == 0);
  CHECK(exchange(0, 5, B_P, open) > 0);
  CHECK(exchange(0, 5, B_K, low) == 0);
  /* a high: the standing guard holds, the low guard ducks it (a whiff: no block stun) */
  CHECK(exchange(0, 8, B_P, stand) == 0);
  uint32_t wh = ft_stat.whiffs_high;
  CHECK(exchange(0, 8, B_P, low) == 0);
  CHECK(ft_stat.whiffs_high == wh + 1 && G.f[1].state == S_BLOCK && G.f[1].stun == 0);
  CHECK(exchange(0, 8, B_K, open) > 0);
  /* a high passes over a crouching attack too: 2P beats 8P when both start together */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 800;
  tick(dpad(8, B_P), dpad(2, B_P));
  for (int i = 0; i < 40; i++) tick(NONE, NONE);
  CHECK(G.f[1].hp == R.d[0].hp && G.f[0].hp < R.d[0].hp);
  /* back alone is not a guard any more */
  CHECK(exchange(0, 5, B_P, dirbtn(0, 1, 0, 0, 0)) > 0);
  /* chip: none from normals, an eighth from specials */
  CHECK(exchange(0, 5, B_K, stand) == 0);
  fresh(0, 0, -2500, 2500);
  G.f[1].parry_lock = FT_PARRY_LOCK;
  for (int i = 0; i < 3; i++) { tick(dpad("236"[i] - '0', 0), stand); tick(dpad("236"[i] - '0', i == 2 ? B_P : 0), stand); }
  for (int i = 0; i < 120; i++) tick(NONE, stand);
  CHECK(G.f[1].hp == R.d[0].hp - R.d[0].mv[M_SP1].dmg / 8);
}

static void test_parry_and_guard_break(void) {
  /* a fresh block press 3 frames before the hit lands: a parry. The defender is free at once, the attacker is not */
  const ft_move *m = &R.d[0].mv[M_5K];
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 800;
  uint32_t pr = ft_stat.parries;
  tick(dpad(5, B_K), NONE);
  for (int i = 1; i < m->su; i++) tick(NONE, i >= m->su - 3 ? btn(B_G) : NONE);
  for (int i = 0; i < 60 && !G.adv_valid; i++) tick(NONE, btn(B_G));
  CHECK(ft_stat.parries == pr + 1);
  CHECK(G.f[1].hp == R.d[0].hp);
  printf("  parried 5K: the defender is %+d\n", G.adv);
  CHECK(G.adv_valid && G.adv <= -m->rc); /* free while the kick recovers (advantage is the attacker's: negative) */
  /* mashing: a press 10 frames after the last opens no window, so the hit is only blocked */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 800;
  pr = ft_stat.parries;
  tick(NONE, btn(B_G));
  for (int i = 0; i < 9; i++) tick(NONE, NONE);
  tick(dpad(5, B_K), NONE);
  for (int i = 1; i < m->su; i++) tick(NONE, i >= m->su - 3 ? btn(B_G) : NONE);
  for (int i = 0; i < 40; i++) tick(NONE, btn(B_G));
  CHECK(ft_stat.parries == pr && G.f[1].hp == R.d[0].hp); /* blocked, not parried */
  /* a parry at the wrong height does not stop a low */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 800;
  const ft_move *s = &R.d[0].mv[M_2K];
  tick(dpad(2, B_K), NONE);
  for (int i = 1; i < s->su; i++) tick(NONE, i >= s->su - 3 ? btn(B_G) : NONE);
  for (int i = 0; i < 40; i++) tick(NONE, NONE);
  CHECK(G.f[1].hp < R.d[0].hp);
  /* guard pressure: a nearly full gauge breaks on the next blocked hit into a long stun */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 800;
  uint32_t gb = ft_stat.guard_breaks;
  G.f[1].guard = FT_GUARD_MAX - 50;
  G.f[1].parry_lock = FT_PARRY_LOCK; /* holding, not a fresh press */
  tick(dpad(5, B_K), btn(B_G));
  for (int i = 0; i < 12; i++) tick(NONE, btn(B_G));
  CHECK(ft_stat.guard_breaks == gb + 1 && G.f[1].state == S_HIT);
  /* the gauge drains when the guard is down */
  fresh(0, 0, -450, 450);
  G.f[1].guard = 500;
  for (int i = 0; i < 60; i++) tick(NONE, NONE);
  CHECK(G.f[1].guard < 300);
}

/* feed a motion then a button: dirs are numpad digits for a fighter facing right */
static void motion(const char *dirs, uint16_t b, ft_input other) {
  for (const char *p = dirs; *p; p++) {
    bool last = !p[1];
    tick(dpad(*p - '0', last ? b : 0), other);
    tick(dpad(*p - '0', last ? b : 0), other);
  }
}
static void test_specials(void) {
  fresh(0, 0, -2500, 2500);
  motion("236", B_P, NONE);
  bool seen = false;
  for (int i = 0; i < 40; i++) { tick(NONE, NONE); for (int p = 0; p < 8; p++) if (G.proj[p].live) seen = true; }
  CHECK(seen); /* the fireball spawned from the motion */
  for (int i = 0; i < 90; i++) tick(NONE, NONE);
  CHECK(G.f[1].hp < R.d[0].hp); /* and hit the far fighter */
  /* the right motion with the other button is not the special (236 + K ends forward + kick: the step kick) */
  fresh(0, 0, -2500, 2500);
  motion("236", B_K, NONE);
  CHECK(G.f[0].state == S_ATTACK && G.f[0].mv == M_6K);
  /* a wrong motion does nothing special */
  fresh(0, 0, -2500, 2500);
  motion("632", B_P, NONE);
  CHECK(G.f[0].state != S_ATTACK || G.f[0].mv != M_SP1);
  /* 214K: the spin kick */
  fresh(0, 0, -2500, 2500);
  motion("214", B_K, NONE);
  CHECK(G.f[0].state == S_ATTACK && G.f[0].mv == M_SP3);
  /* the rising strike is invulnerable on its first frames and beats a hop-in */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 1000;
  tick(NONE, dpad(8, 0)); tick(NONE, NONE); tick(NONE, dpad(8, 0));
  for (int i = 0; i < 4; i++) tick(NONE, NONE);
  motion("623", B_P, NONE);
  for (int i = 0; i < 70; i++) tick(NONE, NONE);
  CHECK(G.f[0].hp == R.d[0].hp || G.f[1].hp < R.d[0].hp); /* never loses to the hop without trading */
  /* the charge motion needs the charge */
  fresh(3, 0, -2500, 2500);
  for (int i = 0; i < 40; i++) tick(dirbtn(1, 0, 0, 0, 0), NONE); /* hold back (left, facing right) */
  motion("6", B_P, NONE);
  CHECK(G.f[0].state == S_ATTACK && G.f[0].mv == M_SP1);
  fresh(3, 0, -2500, 2500);
  for (int i = 0; i < 8; i++) tick(dirbtn(1, 0, 0, 0, 0), NONE);
  motion("6", B_P, NONE);
  CHECK(G.f[0].state == S_ATTACK && G.f[0].mv == M_6P); /* not charged: the lunge punch */
  /* Mara's down charge builds under a low guard: [2]8K */
  fresh(3, 0, -2500, 2500);
  for (int i = 0; i < 40; i++) tick(dirbtn(0, 0, 0, 1, B_G), NONE);
  motion("8", B_K, NONE);
  CHECK(G.f[0].state == S_ATTACK && G.f[0].mv == M_SP2);
}

static void test_combo_and_scaling(void) {
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 750;
  /* a punch cancels into a kick while it connects (a chain), the kick into a special */
  tick(btn(B_P), NONE);
  for (int i = 0; i < 5; i++) tick(NONE, NONE);
  tick(btn(B_K), NONE);
  for (int i = 0; i < 10; i++) tick(NONE, NONE);
  CHECK(G.f[0].mv == M_5K);
  motion("214", B_K, NONE);
  for (int i = 0; i < 60; i++) tick(NONE, NONE);
  CHECK(G.f[1].hp < R.d[0].hp - 100);
  CHECK(G.last_combo >= 3);
  /* the scaling table: a later hit in a combo does less than the same hit on its own */
  CHECK(G.last_dmg < R.d[0].mv[M_SP3].dmg);
  /* a kick does not chain into a punch */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 750;
  tick(btn(B_K), NONE);
  for (int i = 0; i < 9; i++) tick(NONE, NONE);
  tick(btn(B_P), NONE);
  CHECK(G.f[0].mv == M_5K);
  /* a launcher: 8K sends them up */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 750;
  tick(dpad(8, B_K), NONE);
  for (int i = 0; i < 60; i++) tick(NONE, NONE);
  CHECK(G.f[1].hp < R.d[0].hp);
}

static void test_throw(void) {
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 700;
  tick(btn(B_P | B_K), NONE);
  for (int i = 0; i < 90; i++) tick(NONE, NONE);
  CHECK(G.f[1].hp == R.d[0].hp - R.d[0].mv[M_THROW].dmg);
  /* punch then kick a frame later is still the throw */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 700;
  tick(btn(B_P), NONE);
  tick(btn(B_P | B_K), NONE);
  CHECK(G.f[0].mv == M_THROW);
  /* a throw beats a guard */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 700;
  tick(btn(B_P | B_K), btn(B_G));
  for (int i = 0; i < 90; i++) tick(NONE, btn(B_G));
  CHECK(G.f[1].hp < R.d[0].hp);
  /* a defender who presses punch + kick in time breaks it */
  fresh(0, 0, -450, 450);
  G.f[1].x = G.f[0].x + 700;
  tick(btn(B_P | B_K), btn(B_P | B_K));
  for (int i = 0; i < 90; i++) tick(NONE, NONE);
  CHECK(G.f[1].hp == R.d[0].hp);
}

static void test_meter_super(void) {
  fresh(0, 0, -2500, 2500);
  G.f[0].meter = FT_METER_MAX;
  motion("236236", B_K, NONE);
  CHECK(G.f[0].state == S_ATTACK && G.f[0].mv == M_SUPER);
  CHECK(G.f[0].meter == 0);
  fresh(0, 0, -2500, 2500);
  G.f[0].meter = 400;
  motion("236236", B_P, NONE);
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
static int round_len[4096], n_rounds, n_timeouts, wins_by_def[8], games_by_def[8], n_comeback;
static int pair_w[4][4], pair_n[4][4];
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
    int fight_ticks = 0, prev_phase = -1, leader = -1, flipped = 0;
    while (G.phase != P_MATCH_END && guard++ < 60 * 60 * 15) {
      tick(NONE, NONE);
      if (G.phase == P_FIGHT) fight_ticks++;
      if (G.phase == P_FIGHT) { /* the lead by life fraction; a reversal after someone fell under half counts as a comeback */
        int a = G.f[0].hp * 1000 / R.d[G.f[0].def].hp, b = G.f[1].hp * 1000 / R.d[G.f[1].def].hp;
        int l = a > b ? 0 : b > a ? 1 : leader;
        if (l != leader && leader >= 0 && (a < 500 || b < 500)) flipped = 1;
        leader = l;
      }
      if (prev_phase == P_FIGHT && G.phase != P_FIGHT && n_rounds < 4096) { round_len[n_rounds++] = fight_ticks; if (G.timer == 0) n_timeouts++; fight_ticks = 0; n_comeback += flipped; flipped = 0; leader = -1; }
      prev_phase = G.phase;
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
    for (int i = 0; i < 2; i++) games_by_def[G.f[i].def]++;
    if (G.match_winner >= 0) wins_by_def[G.f[G.match_winner].def]++;
    if (G.match_winner >= 0 && G.f[0].def != G.f[1].def) { int w = G.f[G.match_winner].def, l = G.f[1 - G.match_winner].def; pair_w[w][l]++; pair_n[w][l]++; pair_n[l][w]++; }
    if (G.phase == P_MATCH_END) ends++; else { locked++; printf("  soak match %d never ended (phase %d round %d)\n", m, G.phase, G.round_no); }
    fold = (fold ^ ft_hash(&G)) * 16777619u;
  }
  CHECK(locked == 0);
  printf("  soak: %d matches, %d ended, fold %08x\n", matches, ends, fold);
  for (int i = 1; i < n_rounds; i++) for (int j = i; j > 0 && round_len[j] < round_len[j - 1]; j--) { int t = round_len[j]; round_len[j] = round_len[j - 1]; round_len[j - 1] = t; }
  printf("  rounds %d, median %.1f s, timed out %d (%.0f%%)\n", n_rounds, n_rounds ? round_len[n_rounds / 2] / 60.0 : 0.0, n_timeouts, n_rounds ? 100.0 * n_timeouts / n_rounds : 0.0);
  printf("  comebacks (lead reversed after a fighter fell under half life): %d of %d rounds (%.0f%%)\n", n_comeback, n_rounds, n_rounds ? 100.0 * n_comeback / n_rounds : 0.0);
  printf("  hits %u, blocks %u, parries %u, guard breaks %u, highs ducked %u, throws %u\n", ft_stat.hits, ft_stat.blocks, ft_stat.parries, ft_stat.guard_breaks, ft_stat.whiffs_high, ft_stat.throws);
  for (int d = 0; d < R.n; d++) printf("  %s wins %d of %d (%.0f%%)\n", R.d[d].id, wins_by_def[d], games_by_def[d], games_by_def[d] ? 100.0 * wins_by_def[d] / games_by_def[d] : 0.0);
  printf("  row beats column:\n");
  for (int a = 0; a < R.n; a++) { printf("   %-5s", R.d[a].id); for (int b = 0; b < R.n; b++) printf(" %5.0f%%", pair_n[a][b] ? 100.0 * pair_w[a][b] / pair_n[a][b] : 0.0); printf("\n"); }
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
  test_parse(); test_walk_and_facing(); test_slots(); test_frame_data(); test_advantage(); test_block_triangle(); test_parry_and_guard_break(); test_specials();
  test_combo_and_scaling(); test_throw(); test_meter_super(); test_rounds_ko(); test_determinism();
  memset(&ft_stat, 0, sizeof ft_stat);
  test_soak(soak);
  printf("simtest: %d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
