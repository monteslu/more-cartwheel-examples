/* Rules checks against FUN-RUBRIC Part B, on the headless simulation only (no window, no GPU):
 *   FI8   no degenerate move: every move of every fighter, frame advantage measured in the sim
 *   FI9   no infinite: an exhaustive combo search per attacker and defender, mid screen and in both corners
 *   FI10  no inescapable setup: every move at every timing around wakeup, against every defender answer
 *   FI11  no dominant option: bots restricted to one move class, against the field
 *   FI13  comebacks: lead reversals and how often the fighter behind wins, from computer matches
 *   FI16  facing: cross-ups, scripted, plus a facing invariant over computer matches
 *   AI    the computer: hard must not lose to any one-move bot, and must stay beatable by a varied one
 * Usage: rulescheck <moves.txt> [--only FI9] [--patch "move bolt 5P bs=30"]... [--matches N] [--attacker bolt] [--side 0|1] [--budget N]
 * Exit 0 = every check run passed. tests/run-rulescheck.sh runs them and the controls that must fail. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../sim.h"

static ft_roster R;
static ft_game G;
static const ft_input NONE;
static int failures;
static int opt_matches = 20000, opt_budget = 7900000, opt_side = -1;
static const char *opt_only, *opt_attacker;

static char *slurp(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { printf("cannot open %s\n", path); exit(2); }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  char *b = malloc((size_t)n + 1); if (fread(b, 1, (size_t)n, f) != (size_t)n) exit(2); b[n] = 0; fclose(f); return b;
}
static bool run_check(const char *name) { return !opt_only || !strcmp(opt_only, name); }
static void verdict(const char *name, bool ok) {
  printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
  if (!ok) failures++;
}

/* an input: dir is numpad relative to facing */
static ft_input rel(int facing, int d, uint16_t b) {
  int x = (d % 3 == 0) - (d % 3 == 1), y = d >= 7 ? 1 : (d <= 3 ? -1 : 0);
  if (facing < 0) x = -x;
  ft_input i = { x < 0, x > 0, y > 0, y < 0, b };
  return i;
}
static void step2(ft_game *g, ft_input a, ft_input b) { ft_input in[2] = { a, b }; ft_step(g, in); }
static void fresh(ft_game *g, int d0, int d1, int x0, int x1) {
  memset(g, 0, sizeof *g);
  ft_game_start(g, &R, d0, d1);
  g->phase = P_FIGHT; g->timer = 30000;
  for (int i = 0; i < 2; i++) { g->f[i].state = S_IDLE; g->f[i].facing = x0 < x1 ? (i ? -1 : 1) : (i ? 1 : -1); }
  g->f[0].x = x0; g->f[1].x = x1;
}
static bool hit_state(int s) { return s == S_HIT || s == S_AIRHIT || s == S_THROWN; }
static bool proj_live(const ft_game *g, int owner) {
  for (int p = 0; p < 8; p++) if (g->proj[p].live && g->proj[p].owner == owner) return true;
  return false;
}

/* The frames of input that give a slot: {dir, buttons}, the last frame carries the button. A motion takes one frame
 * per step, the fastest a hand can. A charge motion starts after its charge (the caller holds it before). */
typedef struct frame_in { uint8_t dir; uint16_t btn; } frame_in;
static int charge_dir(const ft_def *d, int slot) {
  if (slot < M_SP1) return 0;
  const cw_motion *m = &d->mo[slot - M_SP1];
  return m->charge ? m->steps[0] : 0;
}
static int slot_frames(const ft_def *d, int slot, frame_in *out) {
  static const int DIR[8] = { 5, 8, 2, 6, 5, 8, 2, 6 };
  if (slot <= M_6K) { out[0] = (frame_in){ (uint8_t)DIR[slot], slot < M_5K ? B_P : B_K }; return 1; }
  if (slot == M_JP) { out[0] = (frame_in){ 5, B_P }; return 1; }
  if (slot == M_JK) { out[0] = (frame_in){ 5, B_K }; return 1; }
  if (slot == M_THROW) { out[0] = (frame_in){ 5, B_ATTACKS }; return 1; }
  const cw_motion *m = &d->mo[slot - M_SP1];
  uint16_t b = d->mbtn[slot - M_SP1] & B_P ? B_P : B_K;
  int n = 0;
  for (int i = m->charge ? 1 : 0; i < m->count; i++) out[n++] = (frame_in){ m->steps[i], i == m->count - 1 ? b : 0 };
  return n;
}

/* ── FI8: no degenerate move ─────────────────────────────────────────── */

/* Start `slot` for fighter 0 and put fighter 1 in reach on active frame `k`, guarding at the right height. Returns the
 * advantage measured the way the training readout does (the tick each side is free). air_wait: frames after takeoff
 * before a hop attack. Sets *blocks. Returns -999 when it never connected. */
static int measure_block(int f, int slot, int k, int air_wait, int *blocks, int *hits) {
  const ft_def *d = &R.d[f];
  const ft_move *m = &d->mv[slot];
  ft_game *g = &G;
  fresh(g, f, 0, -3000, 3400);
  g->f[0].meter = FT_METER_MAX;
  g->f[1].parry_lock = FT_PARRY_LOCK; /* a held guard, not a timed press */
  g->f[1].hp = 30000;
  ft_input guard = rel(-1, m->lv == LV_LOW ? 2 : 5, B_G);
  frame_in fr[8];
  int nf = slot_frames(d, slot, fr);
  if (m->flags & FM_AIR) {
    step2(g, rel(1, 8, 0), guard);
    for (int i = 0; i < 40 && g->f[0].state != S_AIR; i++) step2(g, NONE, guard);
    for (int i = 0; i < air_wait; i++) step2(g, NONE, guard);
  } else if (charge_dir(d, slot)) {
    for (int i = 0; i < 34; i++) { step2(g, rel(1, charge_dir(d, slot), 0), guard); g->f[0].x = -3000; }
  }
  for (int i = 0; i < nf; i++) step2(g, rel(1, fr[i].dir, fr[i].btn), guard);
  if (g->f[0].state != S_ATTACK || g->f[0].mv != slot) return -999;
  uint32_t b0 = ft_stat.blocks, h0 = ft_stat.hits;
  int atk_free = -1, def_free = -1, placed = 0, seen = 0, gap = (m->flags & FM_PROJ) ? 1050 : m->hit.x1 + 320 - 60;
  if (gap < 600) gap = 600;
  for (int i = 0; i < 300; i++) {
    const ft_fighter *a = &g->f[0];
    if (!placed && a->state == S_ATTACK && a->t >= m->su + k) { g->f[1].x = a->x + a->facing * gap; placed = 1; }
    uint32_t bb = ft_stat.blocks;
    step2(g, NONE, guard);
    if (ft_stat.blocks != bb) { seen = 1; atk_free = def_free = -1; }
    if (!seen) { if (g->hitstop == 0 && a->state != S_ATTACK && a->state != S_AIR && a->state != S_LAND && !proj_live(g, 0)) break; continue; }
    bool af = ft_actionable(&g->f[0]);
    if (af && atk_free < 0) atk_free = i; else if (!af) atk_free = -1;
    bool df = ft_actionable(&g->f[1]);
    if (df && def_free < 0) def_free = i; else if (!df) def_free = -1;
    if (atk_free >= 0 && def_free >= 0 && !proj_live(g, 0)) break;
  }
  *blocks = (int)(ft_stat.blocks - b0);
  *hits = (int)(ft_stat.hits - h0);
  if (!seen || atk_free < 0 || def_free < 0) return -999;
  return def_free - atk_free;
}

/* A move that is plus on block: the defender throws on its first free frame, the attacker repeats the move on its own.
 * True when the throw never lands (the move is safe from throw). Only ground moves can be plus and fast. */
static bool safe_from_throw(int f, int slot) {
  const ft_def *d = &R.d[f];
  const ft_move *m = &d->mv[slot];
  ft_game *g = &G;
  fresh(g, f, 0, -500, 500);
  g->f[1].x = g->f[0].x + m->hit.x1 + 260;
  g->f[1].parry_lock = FT_PARRY_LOCK;
  g->f[1].hp = 30000; g->f[0].hp = 30000;
  ft_input guard = rel(-1, m->lv == LV_LOW ? 2 : 5, B_G);
  frame_in fr[8];
  int nf = slot_frames(d, slot, fr);
  for (int i = 0; i < nf; i++) step2(g, rel(1, fr[i].dir, fr[i].btn), guard);
  int repeat = 0, thrown = 0;
  bool threw = false;
  for (int i = 0; i < 120; i++) {
    ft_input ia = NONE, id = guard;
    if (ft_actionable(&g->f[0]) && repeat < 8) { ia = rel(g->f[0].facing, fr[nf - 1].dir, fr[nf - 1].btn); repeat++; }
    if (g->f[1].state == S_BLOCK && !g->f[1].stun && !threw && ft_stat.blocks) { id = rel(g->f[1].facing, 5, B_ATTACKS); threw = true; }
    if (threw && (g->f[1].state == S_ATTACK || g->f[1].state == S_IDLE)) id = NONE;
    step2(g, ia, id);
    if (g->f[0].state == S_THROWN) thrown = 1;
  }
  return !thrown;
}

static void check_fi8(void) {
  printf("\nFI8 no degenerate move (%d fighters x %d slots, all measured in the sim)\n", R.n, FT_NMOVES);
  int moves = 0, measured = 0, plus = 0, degenerate = 0, table_diff = 0;
  bool fastest_ok = true;
  for (int f = 0; f < R.n; f++) {
    const ft_def *d = &R.d[f];
    int fastest = 999;
    for (int s = 0; s <= M_6K; s++) if (d->mv[s].su < fastest) fastest = d->mv[s].su;
    printf("  %-5s fastest normal startup %d\n", d->id, fastest);
    if (fastest < 3 || fastest > 5) fastest_ok = false;
    for (int s = 0; s < FT_NMOVES; s++) {
      const ft_move *m = &d->mv[s];
      moves++;
      if (m->flags & FM_THROW) continue; /* no block stun: a throw cannot be blocked */
      int su_eff = m->su + ((m->flags & FM_AIR) ? FT_PREJUMP : 0); /* a hop attack starts from the ground: the prejump comes first */
      int first = -999, best = -999, late = -999, blocks = 0, hits = 0, maxblocks = 0;
      for (int k = 0; k < m->ac; k++) {
        int waits = (m->flags & FM_AIR) ? 28 : 1;
        for (int w = 0; w < waits; w += 2) {
          int a = measure_block(f, s, k, w, &blocks, &hits);
          if (a == -999) continue;
          if (blocks > maxblocks) maxblocks = blocks;
          if (k == 0 && (first == -999 || a > first)) first = a;
          if (a > best) best = a;
          if (k > 0 && a > late) late = a;
        }
        if (m->flags & FM_PROJ) break;
      }
      if (first == -999) { printf("  %-5s %-5s %-18s never connected on block\n", d->id, FT_MOVE_ID[s], m->name); table_diff++; continue; }
      measured++;
      /* the table's number is for a single-hit ground move hit on its first active frame */
      bool comparable = !(m->flags & (FM_AIR | FM_PROJ)) && m->hits == 1;
      if (comparable && first != ft_advantage(m, true)) { printf("  %-5s %-5s measured %+d, table %+d\n", d->id, FT_MOVE_ID[s], first, ft_advantage(m, true)); table_diff++; }
      if (maxblocks > m->hits) printf("  %-5s %-5s %-18s blocked %d times from one use (table: %d hits)\n", d->id, FT_MOVE_ID[s], m->name, maxblocks, m->hits);
      if (first > 0 || best > 0) {
        plus++;
        bool fast = su_eff < 8;
        bool safe = fast && first > 0 && !(m->flags & FM_AIR) ? safe_from_throw(f, s) : false;
        bool bad = first > 0 && fast && (safe || (m->flags & FM_AIR));
        if (bad) degenerate++;
        printf("  %-5s %-5s %-18s startup %2d (from neutral %2d)  block first active %+3d, best any frame %+3d %s%s\n", d->id, FT_MOVE_ID[s], m->name, m->su, su_eff, first, best,
               first <= 0 ? "(plus only on a late active frame)" : fast ? (safe ? "SAFE FROM THROW" : "throwable") : "too slow to count",
               bad ? " DEGENERATE" : "");
      }
    }
  }
  printf("  moves %d, measured on block %d (throws cannot be blocked), plus on some frame %d, degenerate %d, measured differs from table %d\n", moves, measured, plus, degenerate, table_diff);
  verdict("FI8", fastest_ok && !degenerate && !table_diff);
}

/* ── FI9: no infinite ────────────────────────────────────────────────── */

/* The search walks every line of play for an attacker against an idle defender. A node is the physical state; an
 * action is: wait w frames (standing, or once free walking, dashing, or hopping), then press something: on each of the
 * first 7 frames a press can do anything, then ever more sparsely (a link wants the early frames). Everything that
 * hits is a child; a state seen again on the current line with hits in between is a loop, and a loop is replayed on
 * the real sim until it has landed 60 hits. Life does not end a line (the defender has 30000) because a real KO ending an
 * infinite does not make it less infinite. */
enum { MD_STAND, MD_WALK, MD_DASH, MD_HOP, MD_HOPF, MD_COUNT };
enum { PR_NONE = -1, PR_THROW = M_THROW, PR_AIRP = FT_NMOVES, PR_AIRK, PR_COUNT };
typedef struct act { int8_t mode; int16_t w; int8_t press; } act;
typedef struct node { uint64_t key; uint32_t hits; int32_t dmg; uint8_t color; act best; uint64_t child; } node;
#define TABLE_BITS 24
#define WMAX 48
static node *T;
static int s_nodes, s_hold, s_a, s_d;
static bool s_trunc, s_infinite;
static int s_inf_hits, s_inf_dmg;
static char s_inf_line[512];
typedef struct stack_ent { uint64_t key; act a; const ft_game *state; } stack_ent;
static stack_ent S_[2048];
static int s_top;

static uint64_t mix(uint64_t h, int64_t v) { h ^= (uint64_t)v; h *= 1099511628211ull; return h ^ (h >> 29); }
static uint64_t key_of(const ft_game *g) {
  uint64_t h = 14695981039346656037ull;
  /* away from the walls only the gap matters: a position counts by its distance to the nearer wall, up to 2500, and
   * positions count to the nearest 25 mm (a third of a walk frame): lines that differ by less are one line. So a miss
   * is possible in principle; a loop it does report is always replayed on the exact sim */
  int ax = g->f[0].x, bx = g->f[1].x, right = FT_WALL - (ax > bx ? ax : bx), left = FT_WALL + (ax < bx ? ax : bx);
  h = mix(h, (ax - bx) / 25); h = mix(h, (right < 2500 ? right : 2500) / 25); h = mix(h, (left < 2500 ? left : 2500) / 25);
  for (int i = 0; i < 2; i++) {
    const ft_fighter *f = &g->f[i];
    h = mix(h, f->y / 25); h = mix(h, f->vx); h = mix(h, f->vy); h = mix(h, f->pvx); h = mix(h, f->facing);
    h = mix(h, f->state); h = mix(h, f->t); h = mix(h, f->mv); h = mix(h, f->connected); h = mix(h, f->hit_count); h = mix(h, f->air_used);
    h = mix(h, f->rehit_t); h = mix(h, f->invuln); h = mix(h, f->stun); h = mix(h, f->juggle);
  }
  h = mix(h, g->f[s_a].meter / 50);
  h = mix(h, g->f[s_d].combo_hits > 10 ? 10 : g->f[s_d].combo_hits);
  h = mix(h, g->hitstop);
  for (int p = 0; p < 8; p++) if (g->proj[p].live) { h = mix(h, (g->proj[p].x - ax) / 25); h = mix(h, g->proj[p].life); h = mix(h, g->proj[p].mv); h = mix(h, g->proj[p].hit); }
  return h;
}
static node *lookup(uint64_t key) {
  uint64_t mask = (1ull << TABLE_BITS) - 1, i = key & mask;
  while (T[i].key && T[i].key != key) i = (i + 1) & mask;
  return &T[i];
}
static bool in_combo(const ft_game *g) { return hit_state(g->f[s_d].state) || proj_live(g, s_a); }
/* the input a wait gives: nothing (or the held charge) until free, then the mode's own. ff counts free frames used. */
static ft_input wait_input(const ft_game *g, int mode, int *ff) {
  const ft_fighter *a = &g->f[s_a];
  if (*ff == 0 && (!ft_actionable(a) || mode == MD_STAND)) return rel(a->facing, s_hold, 0);
  int k = (*ff)++;
  switch (mode) {
  case MD_WALK: return rel(a->facing, 6, 0);
  case MD_DASH: return rel(a->facing, k == 1 ? 5 : k < 3 ? 6 : 5, 0);
  case MD_HOP: return rel(a->facing, k == 0 ? 8 : 5, 0);
  case MD_HOPF: return rel(a->facing, k == 0 ? 9 : 5, 0);
  default: return rel(a->facing, s_hold, 0);
  }
}
static void tick_s(ft_game *g, ft_input a) { ft_input in[2]; in[s_a] = a; in[s_d] = NONE; ft_step(g, in); }
static void settle(ft_game *g) { while (g->hitstop) tick_s(g, rel(g->f[s_a].facing, s_hold, 0)); }
static bool can_press(const ft_game *g, int press) {
  const ft_fighter *a = &g->f[s_a];
  bool ground = ft_actionable(a) || (a->state == S_ATTACK && a->connected && a->t > 0 && !(R.d[a->def].mv[a->mv].flags & FM_AIR));
  bool air = (a->state == S_AIR && !a->air_used) || (a->state == S_ATTACK && a->connected && (R.d[a->def].mv[a->mv].flags & FM_AIR));
  if (press == PR_AIRP || press == PR_AIRK) return air;
  if (press == M_JP || press == M_JK) return false;
  if (press == M_THROW) return ft_actionable(a);
  if (press == M_SUPER && a->meter < FT_METER_MAX) return false;
  return ground;
}
/* one press from the state, stepped until the next hit, the end of the move or the end of the combo. true = a child */
static bool press_out(ft_game *g, int press) {
  const ft_def *d = &R.d[g->f[s_a].def];
  frame_in fr[8];
  int n;
  if (press == PR_AIRP) { fr[0] = (frame_in){ 5, B_P }; n = 1; }
  else if (press == PR_AIRK) { fr[0] = (frame_in){ 5, B_K }; n = 1; }
  else n = slot_frames(d, press, fr);
  int base = g->f[s_d].combo_hits;
  for (int i = 0; i < n; i++) {
    tick_s(g, rel(g->f[s_a].facing, i == n - 1 && fr[i].dir == 5 ? s_hold : fr[i].dir, fr[i].btn));
    if (g->f[s_d].combo_hits != base && i < n - 1) return false; /* a hit from before landed first: the wait path has it */
  }
  if (g->f[s_d].combo_hits > base) return true;
  if (g->f[s_a].state != S_ATTACK && g->f[s_a].state != S_PREJUMP) return false;
  for (int i = 0; i < 90; i++) {
    tick_s(g, rel(g->f[s_a].facing, s_hold, 0));
    if (g->f[s_d].combo_hits > base) return true;
    if (g->f[s_d].combo_hits < base) return false;
    if (g->f[s_a].state != S_ATTACK) return proj_live(g, s_a); /* the shot is still out: play on */
    if (!base && !proj_live(g, s_a) && g->f[s_a].t > ft_move_total(&d->mv[g->f[s_a].mv])) return false;
  }
  return false;
}
/* the action on a state, exactly as the search did it: for the replay of a loop */
static bool apply_act(ft_game *g, act a) {
  int ff = 0;
  for (int w = 0; w < a.w; w++) tick_s(g, wait_input(g, a.mode, &ff));
  if (a.press == PR_NONE) { settle(g); return true; }
  bool ok = press_out(g, a.press);
  settle(g);
  return ok;
}
static const char *act_name(int press) { return press == PR_AIRP ? "JP" : press == PR_AIRK ? "JK" : press == PR_NONE ? "(the move's next hit)" : FT_MOVE_ID[press]; }

static void search(const ft_game *n, bool root, uint32_t *out_hits, int32_t *out_dmg);
/* a state seen again on this line: replay the actions between the two sightings */
static void try_loop(const ft_game *from, int start) {
  ft_game g = *from;
  int d0 = g.f[s_d].hp, hits0 = g.f[s_d].combo_hits, rounds = 0;
  char line[256] = "";
  for (int i = start; i < s_top; i++) { strncat(line, act_name(S_[i].a.press), sizeof line - strlen(line) - 1); strncat(line, " ", sizeof line - strlen(line) - 1); }
  while (rounds < 400 && g.f[s_d].combo_hits - hits0 < 60 && d0 - g.f[s_d].hp < 20000) {
    bool ok = true;
    for (int i = start; i < s_top && ok; i++) ok = apply_act(&g, S_[i].a);
    if (!ok || !hit_state(g.f[s_d].state)) break;
    rounds++;
  }
  int hits = g.f[s_d].combo_hits - hits0;
  if (hits >= 60) {
    s_infinite = true; s_inf_hits = g.f[s_d].combo_hits; s_inf_dmg = d0 - g.f[s_d].hp;
    snprintf(s_inf_line, sizeof s_inf_line, "%s", line);
  }
}
static void consider(const ft_game *child, act a, uint32_t *best_h, int32_t *best_d, act *best_a, uint64_t *best_c, int base_hits, int base_hp) {
  ft_game c = *child;
  settle(&c);
  uint32_t ch; int32_t cd;
  if (s_top >= 2040) { s_infinite = true; s_inf_hits = 2040; snprintf(s_inf_line, sizeof s_inf_line, "line deeper than 2040 actions"); return; }
  S_[s_top++].a = a;
  search(&c, false, &ch, &cd);
  s_top--;
  int gained = c.f[s_d].combo_hits - base_hits;
  if (gained < 0) gained = 0;
  uint32_t h = (uint32_t)gained + ch;
  int32_t dm = base_hp - c.f[s_d].hp + cd;
  if (h > *best_h || (h == *best_h && dm > *best_d)) { *best_h = h; *best_d = dm; *best_a = a; *best_c = key_of(&c); }
}
static void search(const ft_game *n, bool root, uint32_t *out_hits, int32_t *out_dmg) {
  *out_hits = 0; *out_dmg = 0;
  if (s_infinite) return;
  uint64_t key = key_of(n);
  if (!root) {
    node *e = lookup(key);
    if (e->key == key) {
      if (e->color == 2) { *out_hits = e->hits; *out_dmg = e->dmg; return; }
      /* on the current line: a loop if the replay holds up */
      for (int i = s_top - 1; i >= 0; i--)
        if (S_[i].key == key) { try_loop(S_[i].state, i); break; }
      return;
    }
    if (s_nodes >= opt_budget) { s_trunc = true; return; }
    s_nodes++;
    if (s_nodes * 2 > (1 << TABLE_BITS)) { s_trunc = true; return; }
    e->key = key; e->color = 1;
  }
  S_[s_top].key = key; S_[s_top].state = n;
  uint32_t best_h = 0; int32_t best_d = 0; act best_a = { 0, 0, PR_NONE }; uint64_t best_c = 0;
  int base = n->f[s_d].combo_hits, base_hp = n->f[s_d].hp;
  for (int mode = 0; mode < MD_COUNT && !s_infinite; mode++) {
    ft_game w = *n;
    int ff = 0;
    bool started = mode == MD_STAND;
    int since = 0; /* frames a press has been possible */
    for (int t = 0; t <= (root ? (mode == MD_STAND ? 0 : 30) : WMAX) && !s_infinite; t++) {
      if (!root && !in_combo(&w)) break;
      if (w.f[s_d].combo_hits > base) { consider(&w, (act){ (int8_t)mode, (int16_t)t, PR_NONE }, &best_h, &best_d, &best_a, &best_c, base, base_hp); break; }
      if (w.f[s_d].combo_hits < base) break;
      bool any = false;
      for (int p = 0; p < PR_COUNT && !any; p++) any = can_press(&w, p);
      if (any) since++;
      static const uint8_t SPARSE[] = { 9, 11, 14, 17, 21, 25, 31, 37, 43 };
      bool timely = since <= 7;
      for (unsigned q = 0; q < sizeof SPARSE && !timely; q++) timely = since == SPARSE[q];
      if (started && timely) {
        for (int p = 0; p < PR_COUNT && !s_infinite; p++) {
          if (!can_press(&w, p)) continue;
          ft_game b = w;
          if (press_out(&b, p)) {
            if (b.f[s_d].combo_hits > base || proj_live(&b, s_a))
              consider(&b, (act){ (int8_t)mode, (int16_t)t, (int8_t)p }, &best_h, &best_d, &best_a, &best_c, base, base_hp);
          }
        }
      }
      tick_s(&w, wait_input(&w, mode, &ff));
      if (ff > 0) started = true;
      /* the modes differ from standing only once the attacker is free */
      if (mode != MD_STAND && !started && t > WMAX / 2) break;
    }
  }
  *out_hits = best_h; *out_dmg = best_d;
  if (!root) {
    node *e = lookup(key);
    e->color = 2; e->hits = best_h; e->dmg = best_d; e->best = best_a; e->child = best_c;
  } else {
    /* the best line, for the log */
    S_[s_top].a = best_a;
  }
}

/* every move on an idle target: no use of a move hits more often than the table says */
static int check_hits_per_use(void) {
  int bad = 0;
  for (int f = 0; f < R.n; f++) {
    const ft_def *d = &R.d[f];
    for (int s = 0; s < FT_NMOVES; s++) {
      const ft_move *m = &d->mv[s];
      if (m->flags & FM_AIR) continue;
      for (int wall = 0; wall < 2; wall++) {
        ft_game *g = &G;
        fresh(g, f, 0, wall ? 4400 - 900 : -450, wall ? 4400 : 450);
        g->f[1].hp = 30000; g->f[0].meter = FT_METER_MAX;
        if (charge_dir(d, s)) for (int i = 0; i < 34; i++) { int x = g->f[0].x; step2(g, rel(1, charge_dir(d, s), 0), NONE); g->f[0].x = x; }
        frame_in fr[8];
        int nf = slot_frames(d, s, fr);
        uint32_t h0 = ft_stat.hits;
        for (int i = 0; i < nf; i++) step2(g, rel(1, fr[i].dir, fr[i].btn), NONE);
        for (int i = 0; i < 200; i++) step2(g, NONE, NONE);
        int hits = (int)(ft_stat.hits - h0);
        if (hits > m->hits) {
          printf("  %-5s %-5s %-18s %s: %d hits from one use (table %d), %d damage of the defender's life\n", d->id, FT_MOVE_ID[s], m->name, wall ? "corner" : "mid", hits, m->hits, 30000 - g->f[1].hp);
          bad++;
        }
      }
    }
  }
  return bad;
}

/* the plainest loop there is: a fighter in the air in the corner and one normal pressed as soon as it can be. Returns the
 * most hits any normal of any fighter lands before the target gets to the floor */
static int juggle_probe(int *spent_events) {
  int worst = 0;
  *spent_events = 0;
  for (int f = 0; f < R.n; f++)
    for (int s = 0; s <= M_6K; s++) {
      ft_game *g = &G;
      const ft_move *m = &R.d[f].mv[s];
      fresh(g, f, 0, FT_WALL - m->hit.x1 - 200, FT_WALL);
      g->f[1].hp = 30000; g->f[1].state = S_AIRHIT; g->f[1].t = 1; g->f[1].y = 300; g->f[1].vy = 70;
      frame_in fr[8];
      slot_frames(&R.d[f], s, fr);
      uint32_t h0 = ft_stat.hits;
      for (int t = 0; t < 2000 && g->f[1].state == S_AIRHIT && ft_stat.hits - h0 < 200; t++)
      {
        uint32_t ev0 = g->ev_seq;
        step2(g, ft_actionable(&g->f[0]) ? rel(g->f[0].facing, fr[0].dir, fr[0].btn) : NONE, NONE);
        for (uint32_t e = ev0; e != g->ev_seq; e++) if (g->ev[e & 63].type == EV_JUGGLE_SPENT) (*spent_events)++;
      }
      int hits = (int)(ft_stat.hits - h0);
      if (hits > worst) worst = hits;
    }
  return worst;
}

static void check_fi9(void) {
  printf("\nFI9 no infinite (exhaustive combo search, node budget %d per pairing)\n", opt_budget);
  int over_hits = check_hits_per_use();
  printf("  moves that hit more often than their table says: %d\n", over_hits);
  int spent, probe = juggle_probe(&spent);
  printf("  one normal mashed on a target in the air in the corner: at most %d hits%s; strikes that passed through and said so: %d\n", probe, probe > 25 ? " JUGGLE LOOP" : "", spent);
  T = calloc(1ull << TABLE_BITS, sizeof *T);
  static const struct { const char *name; int ax, dx; } START[] = {
    { "mid", 0, 0 }, { "defender in corner", 0, 1 }, { "attacker in corner", 1, 0 },
  };
  static const int GAP[] = { 600, 800, 1000, 1400 };
  int worst = 0, total_nodes = 0, inf = 0, trunc = 0, pairs = 0;
  for (int a = 0; a < R.n; a++) {
    if (opt_attacker && strcmp(opt_attacker, R.d[a].id)) continue;
    int worst_a = 0, worst_dmg = 0, worst_def = 0;
    for (int d = 0; d < R.n; d++) {
      /* a combo sees only the defender's body: fighters with the same body and gravity give the same search */
      bool same = false;
      for (int o = 0; o < d; o++) same = same || (R.d[o].w_half == R.d[d].w_half && R.d[o].h_stand == R.d[d].h_stand && R.d[o].h_crouch == R.d[d].h_crouch && R.d[o].grav == R.d[d].grav);
      if (same) continue;
      for (int side = 0; side < 2; side++) { /* attacker on the left, then on the right */
        if (opt_side >= 0 && side != opt_side) continue;
        int holds = charge_dir(&R.d[a], M_SP1) || charge_dir(&R.d[a], M_SP2) || charge_dir(&R.d[a], M_SP3) ? 3 : 1;
        for (int hold = 0; hold < holds; hold++) {
          static const int HOLD[3] = { 5, 4, 2 };
          memset(T, 0, (1ull << TABLE_BITS) * sizeof *T);
          s_nodes = 0; s_infinite = false; s_trunc = false; s_top = 0;
          s_hold = HOLD[hold];
          s_a = side; s_d = 1 - side;
          uint32_t best = 0; int32_t best_dmg = 0;
          for (unsigned st = 0; st < sizeof START / sizeof START[0] && !s_infinite; st++)
            for (unsigned gp = 0; gp < sizeof GAP / sizeof GAP[0] && !s_infinite; gp++) {
              int dirn = side ? -1 : 1; /* the attacker faces dirn */
              int ax = -dirn * GAP[gp] / 2, dx = dirn * GAP[gp] / 2;
              if (START[st].dx) { dx = dirn * FT_WALL; ax = dx - dirn * GAP[gp]; }
              if (START[st].ax) { ax = -dirn * FT_WALL; dx = ax + dirn * GAP[gp]; }
              ft_game g;
              fresh(&g, side ? d : a, side ? a : d, side ? dx : ax, side ? ax : dx);
              g.f[s_d].hp = 30000; g.f[s_a].meter = FT_METER_MAX;
              if (s_hold != 5) { /* the charge is held before the fight starts here */
                for (int i = 0; i < 34; i++) { int x = g.f[s_a].x; tick_s(&g, rel(g.f[s_a].facing, s_hold, 0)); g.f[s_a].x = x; g.f[s_a].vx = 0; }
              }
              uint32_t h; int32_t dm;
              search(&g, true, &h, &dm);
              if (h > best) best = h;
              if (dm > best_dmg) best_dmg = dm;
            }
          printf("  %-5s vs %-5s from the %s, charge %d: %d states%s, longest %u hits, most damage %d\n", R.d[a].id, R.d[d].id, side ? "right" : "left", s_hold, s_nodes, s_trunc ? " (stopped at the budget)" : "", best, best_dmg);
          total_nodes += s_nodes; pairs++;
          if (s_trunc) trunc++;
          if ((int)best > worst_a) { worst_a = (int)best; worst_def = d; }
          if (best_dmg > worst_dmg) worst_dmg = best_dmg;
          if (s_infinite) {
            inf++;
            printf("  %-5s vs %-5s %s hold %d: LOOP, %d hits and %d damage replayed on the sim: %s\n", R.d[a].id, R.d[d].id, side ? "from the right" : "from the left", s_hold, s_inf_hits, s_inf_dmg, s_inf_line);
          }
        }
      }
    }
    printf("  %-5s longest combo %d hits (against %s), most damage %d (%d%% of the smallest life)\n", R.d[a].id, worst_a, R.d[worst_def].id, worst_dmg, worst_dmg * 100 / 920);
    if (worst_a > worst) worst = worst_a;
  }
  free(T);
  printf("  searched %d attacker/defender/side/charge cases, %d states, %d stopped at the node budget, %d with a verified loop, longest combo %d hits\n", pairs, total_nodes, trunc, inf, worst);
  verdict("FI9", pairs > 0 && !inf && !trunc && worst <= 25 && !over_hits && probe <= 25 && spent > 0);
}

/* ── FI10: no inescapable setup after a knockdown ────────────────────── */

enum { AN_GUARD, AN_FRESH, AN_TECH, AN_REVERSAL, AN_BACKDASH, AN_HOP, AN_JAB, AN_COUNT };
static const char *const AN_NAME[AN_COUNT] = { "held guard", "guard pressed on waking", "punch + kick", "invulnerable special", "back dash", "hop", "fastest normal" };

/* The defender is knocked down at tick 0 and is free after tick T0 = 60. The attacker's move connects around then.
 * true = the defender took a hit. *locked counts first-free frames where the defender's input did nothing. */
static bool oki_run(int a, int d, int ax, int dx, int slot, int press_tick, int air_k, int answer, int *locked, int rev_slot) {
  const ft_def *ad = &R.d[a], *dd = &R.d[d];
  const ft_move *m = &ad->mv[slot];
  ft_game *g = &G;
  fresh(g, a, d, ax, dx);
  g->f[0].meter = FT_METER_MAX;
  g->f[0].hp = 30000; g->f[1].hp = 30000;
  g->f[1].state = S_KD; g->f[1].t = 0;
  g->f[1].parry_lock = answer == AN_GUARD ? FT_PARRY_LOCK : 0;
  frame_in fr[8], rv[8];
  int nf = slot_frames(ad, slot, fr), nr = rev_slot >= 0 ? slot_frames(dd, rev_slot, rv) : 0;
  int cd = charge_dir(ad, slot);
  bool air = m->flags & FM_AIR;
  int first = air ? press_tick - air_k - FT_PREJUMP : press_tick - (nf - 1); /* the tick its input starts */
  int t0 = 60, face_d = g->f[1].facing;
  int guard_dir = m->lv == LV_LOW ? 2 : 5;
  bool got_hit = false;
  for (int t = 1; t <= t0 + 150; t++) {
    ft_input ia = NONE, id = NONE;
    const ft_fighter *fa = &g->f[0], *fd = &g->f[1];
    if (t < first) {
      if (cd) ia = rel(fa->facing, cd, 0);
    } else if (air) {
      if (t == first) ia = rel(fa->facing, 9, 0);
      if (t == press_tick) ia = rel(fa->facing, 5, fr[0].btn);
    } else if (t < first + nf) {
      ia = rel(fa->facing, fr[t - first].dir, fr[t - first].btn);
    }
    if (t < first) g->f[0].x = ax; /* it waits on its spot, whatever a charge would walk */
    int fr_free = t - t0; /* 1 = the first frame the defender can act */
    face_d = fd->facing;
    switch (answer) {
    case AN_GUARD: id = rel(face_d, guard_dir, B_G); break;
    case AN_FRESH: if (fr_free >= 1) id = rel(face_d, guard_dir, B_G); break;
    case AN_TECH: if (fr_free >= 1 && fr_free <= 3) id = rel(face_d, 5, B_ATTACKS); break;
    case AN_REVERSAL: if (rev_slot >= 0) {
        int lead = nr - 1;
        if (fr_free >= 1 - lead && fr_free <= 1) id = rel(face_d, rv[fr_free - 1 + lead].dir, rv[fr_free - 1 + lead].btn);
      }
      break;
    case AN_BACKDASH: if (fr_free == 1 || fr_free == 3) id = rel(face_d, 4, 0); break;
    case AN_HOP: if (fr_free == 1) id = rel(face_d, 7, 0); break;
    case AN_JAB: if (fr_free == 1) id = rel(face_d, 5, B_P); break;
    default: break;
    }
    bool was_idle_wake = fd->state == S_IDLE && fr_free == 1;
    step2(g, ia, id);
    if (was_idle_wake && answer != AN_REVERSAL && answer != AN_BACKDASH && !g->hitstop && g->f[1].state == S_IDLE) (*locked)++; /* a freeze holds everyone: the input waits */
    if (t > t0 - 20 && hit_state(g->f[1].state)) got_hit = true;
    if (t > t0 - 20 && g->f[1].state == S_KD && g->f[1].t < 5 && t > t0) got_hit = true;
    if (t > t0 + 10 && g->f[0].state != S_ATTACK && g->f[0].state != S_AIR && !proj_live(g, 0) && !g->hitstop && g->f[1].state != S_ATTACK && !got_hit && t > press_tick + m->su + 6) break;
    if (got_hit) break;
  }
  return got_hit;
}

static void check_fi10(void) {
  printf("\nFI10 no inescapable setup: every move, every timing around wakeup, every answer\n");
  static const int GAPS[] = { 600, 900, 1300 };
  long cases = 0, sims = 0, no_answer = 0;
  int locked = 0;
  long answered_by[AN_COUNT] = { 0 };
  static int no_ans_move[FT_MAX_ROSTER][FT_NMOVES], tried_move[FT_MAX_ROSTER][FT_NMOVES];
  memset(no_ans_move, 0, sizeof no_ans_move); memset(tried_move, 0, sizeof tried_move);
  for (int a = 0; a < R.n; a++)
    for (int d = 0; d < R.n; d++) {
      int rev = -1;
      for (int s = M_SP1; s <= M_SP3; s++) if (R.d[d].mv[s].inv && !charge_dir(&R.d[d], s) && (rev < 0 || R.d[d].mv[s].inv > R.d[d].mv[rev].inv)) rev = s;
      for (unsigned gp = 0; gp < sizeof GAPS / sizeof GAPS[0]; gp++)
        for (int corner = 0; corner < 2; corner++) {
          int dx = corner ? FT_WALL : GAPS[gp] / 2, ax = dx - GAPS[gp];
          for (int s = 0; s < FT_NMOVES; s++) {
            const ft_move *m = &R.d[a].mv[s];
            bool air = m->flags & FM_AIR;
            /* the press ticks that bring its active frames from before wakeup to well after it */
            int t0 = 60, extra = (m->flags & FM_SUPER) ? 24 : 0;
            int lo = t0 - m->su - m->ac - extra - 2, hi = t0 + 14 - m->su;
            if (m->flags & FM_PROJ) { lo = t0 - 80; hi = t0 + 10; }
            int ks = air ? 4 : 1;
            for (int k = 0; k < ks; k++) {
              int air_k = air ? 2 + k * 8 : 0;
              for (int press = lo < 8 + air_k ? 8 + air_k : lo; press <= hi; press++) {
                cases++; tried_move[a][s]++;
                bool escaped = false;
                for (int an = 0; an < AN_COUNT; an++) {
                  if (an == AN_REVERSAL && rev < 0) continue;
                  sims++;
                  if (!oki_run(a, d, ax, dx, s, press, air_k, an, &locked, rev)) { escaped = true; answered_by[an]++; }
                }
                if (!escaped) { no_answer++; no_ans_move[a][s]++; }
              }
            }
          }
        }
    }
  printf("  cases %ld (move x timing x gap x corner x pairing), simulations %ld\n", cases, sims);
  for (int an = 0; an < AN_COUNT; an++) printf("  escaped by %-26s %ld\n", AN_NAME[an], answered_by[an]);
  for (int a = 0; a < R.n; a++)
    for (int s = 0; s < FT_NMOVES; s++)
      if (no_ans_move[a][s]) printf("  no answer: %s %s %s, %d of %d timings\n", R.d[a].id, FT_MOVE_ID[s], R.d[a].mv[s].name, no_ans_move[a][s], tried_move[a][s]);
  printf("  cases with no answer %ld; first free frames where a pressed answer did nothing %d\n", no_answer, locked);
  verdict("FI10", !no_answer && !locked);
}

/* ── FI11: no dominant option ────────────────────────────────────────── */

enum { C_5P, C_8P, C_2P, C_6P, C_5K, C_8K, C_2K, C_6K, C_JUMP, C_THROW, C_SP1, C_SP2, C_SP3, C_PUNCHES, C_KICKS, C_LOWS, C_HIGHS, C_CHAIN, C_SPECIALS, C_TURTLE, C_RUN, C_PUNISH,
       C_AI0, C_AI1, C_AI2, C_COUNT, C_HUMAN_LOW = C_COUNT, C_HUMAN_MID, C_HUMAN_HIGH };
static const char *const C_NAME[C_COUNT] = { "5P only", "8P only", "2P only", "6P only", "5K only", "8K only", "2K only", "6K only", "hops", "throws", "SP1 only", "SP2 only", "SP3 only",
  "punches", "kicks", "lows", "highs", "punch-kick-special", "specials and super", "guard and throw", "run away", "guard and punish", "computer easy", "computer normal", "computer hard" };

typedef struct bot { uint32_t rng; frame_in q[16]; int8_t qn, qi; int16_t cool; bool guard_roll, guard_on; uint8_t seen_t; int8_t last_opmv; } bot;
static uint32_t brnd(bot *b) { b->rng = b->rng * 1664525u + 1013904223u; return b->rng >> 8; }
static void queue_slot(bot *b, const ft_def *d, int slot) {
  frame_in fr[8];
  int n = slot_frames(d, slot, fr), cd = charge_dir(d, slot);
  b->qn = 0; b->qi = 0;
  if (cd) for (int i = 0; i < 12; i++) b->q[b->qn++] = (frame_in){ (uint8_t)cd, 0 };
  for (int i = 0; i < n; i++) b->q[b->qn++] = fr[i];
}
/* a pick of the class's moves */
static int class_slot(bot *b, const ft_game *g, int who, int cls) {
  const ft_fighter *me = &g->f[who];
  uint32_t r = brnd(b);
  switch (cls) {
  case C_PUNCHES: { static const int S[4] = { M_5P, M_8P, M_2P, M_6P }; return S[r % 4]; }
  case C_KICKS: { static const int S[4] = { M_5K, M_8K, M_2K, M_6K }; return S[r % 4]; }
  case C_LOWS: return r % 2 ? M_2P : M_2K;
  case C_HIGHS: return r % 2 ? M_8P : M_8K;
  case C_SPECIALS: if (me->meter >= FT_METER_MAX) return M_SUPER; return M_SP1 + (int)(r % 3);
  case C_THROW: case C_TURTLE: return M_THROW;
  case C_SP1: return M_SP1;
  case C_SP2: return M_SP2;
  case C_SP3: return M_SP3;
  default: return cls <= C_6K ? cls : M_5P;
  }
}
static int slot_reach(const ft_def *d, int slot) {
  const ft_move *m = &d->mv[slot];
  if (m->flags & FM_PROJ) return 9000;
  int reach = m->hit.x1 + 320 - 80;
  if (m->mv) reach += m->mv * (m->mvto - m->mvfrom + 1);
  return reach;
}
/* what a restricted bot does: guard at the computer's normal level, walk into its move's range, use its move */
static ft_input bot_input(ft_game *g, int who, int cls, bot *b) {
  const ft_fighter *me = &g->f[who], *op = &g->f[1 - who];
  const ft_def *d = &R.d[me->def], *od = &R.d[op->def];
  ft_input none = NONE;
  if (g->phase != P_FIGHT) { b->qn = b->qi = 0; return none; }
  int dist = abs(op->x - me->x);
  bool free_ = ft_actionable(me);
  if (b->qi < b->qn) { frame_in q = b->q[b->qi++]; return rel(me->facing, q.dir, q.btn); }
  /* guard: reacts 6 frames in, guards 65 percent of the time at the right height (the normal computer's numbers) */
  if ((free_ || me->state == S_BLOCK) && op->state == S_ATTACK && dist < 1900) {
    const ft_move *om = &od->mv[op->mv];
    if (!(om->flags & (FM_PROJ | FM_THROW)) && op->t <= om->su + om->ac && op->t >= 6) {
      if (op->t == 6 || b->last_opmv != op->mv) { b->guard_roll = cls == C_TURTLE || cls == C_PUNISH || brnd(b) % 100 < 65; b->last_opmv = (int8_t)op->mv; }
      if (b->guard_roll || me->state == S_BLOCK) return rel(me->facing, om->lv == LV_LOW ? 2 : 5, B_G);
    }
  }
  if (me->state == S_BLOCK && me->stun) return rel(me->facing, op->state == S_ATTACK && od->mv[op->mv].lv == LV_LOW ? 2 : 5, B_G);
  if (!free_ && me->state != S_AIR) return none;
  if (cls == C_JUMP) {
    if (me->state == S_AIR) {
      if (!me->air_used && dist < 1500 && me->vy < 20) return rel(me->facing, 5, brnd(b) % 2 ? B_K : B_P);
      return none;
    }
    if (dist > 2400) return rel(me->facing, 6, 0);
    return rel(me->facing, 9, 0);
  }
  for (int p = 0; p < 8; p++)
    if (g->proj[p].live && g->proj[p].owner != who && abs(g->proj[p].x - me->x) < 1500) return rel(me->facing, 5, B_G);
  if (cls == C_RUN) {
    if (brnd(b) % 40 == 0) { b->q[0] = (frame_in){ 4, 0 }; b->q[1] = (frame_in){ 5, 0 }; b->q[2] = (frame_in){ 4, 0 }; b->qn = 3; b->qi = 0; return none; }
    return rel(me->facing, 4, 0);
  }
  if (cls == C_TURTLE) {
    if (dist < 800 && brnd(b) % 30 == 0) return rel(me->facing, 5, B_ATTACKS);
    if (dist > 1800) return rel(me->facing, 6, 0);
    return rel(me->facing, 5, B_G);
  }
  if (cls == C_PUNISH) {
    /* holds a guard at the right height, and answers any recovery with the strongest move that fits in it */
    if (op->state == S_ATTACK && op->t > od->mv[op->mv].su + od->mv[op->mv].ac) {
      int left = ft_move_total(&od->mv[op->mv]) - op->t, best = -1;
      for (int s = 0; s <= M_6K; s++) if (d->mv[s].su + 1 < left && dist <= slot_reach(d, s) && (best < 0 || d->mv[s].dmg > d->mv[best].dmg)) best = s;
      if (best >= 0) { queue_slot(b, d, best); frame_in q = b->q[b->qi++]; return rel(me->facing, q.dir, q.btn); }
    }
    if (dist > 1700) return rel(me->facing, 6, 0);
    return rel(me->facing, op->state == S_ATTACK && od->mv[op->mv].lv == LV_LOW ? 2 : 5, B_G);
  }
  if (cls == C_CHAIN) {
    if (dist > slot_reach(d, M_5P)) return rel(me->facing, 6, 0);
    b->q[0] = (frame_in){ 5, B_P }; b->q[1] = b->q[2] = b->q[3] = b->q[4] = b->q[5] = (frame_in){ 5, 0 };
    b->q[6] = (frame_in){ 5, B_K };
    for (int i = 7; i < 15; i++) b->q[i] = (frame_in){ 5, 0 };
    b->qn = 15; b->qi = 0;
    return none;
  }
  if (b->cool > 0) { b->cool--; return none; }
  int slot = class_slot(b, g, who, cls);
  if (dist > slot_reach(d, slot)) return rel(me->facing, 6, 0);
  queue_slot(b, d, slot);
  b->cool = (int16_t)(brnd(b) % 10);
  frame_in q = b->q[b->qi++];
  return rel(me->facing, q.dir, q.btn);
}

/* A varied player, three skills, for "is hard a wall". It reacts as a person does (a skill's frames late, and it misses some),
 * picks its height from what it saw, mixes every move it has rather than repeating one, walks in and back, jumps now and
 * then, throws a guard, punishes a recovery only when it notices it, spends its super. It knows only what is on screen. */
static ft_input human_input(ft_game *g, int who, int skill, bot *b) {
  static const struct { int react, guard, punish, idle, reach_err, block, chain; } H[3] = {
    { 22, 45, 15, 40, 300, 10, 15 }, /* new to the game */
    { 16, 65, 40, 25, 150, 25, 50 }, /* plays a lot */
    { 12, 80, 70, 8, 60, 50, 90 },  /* good */
  };
  const ft_fighter *me = &g->f[who], *op = &g->f[1 - who];
  const ft_def *d = &R.d[me->def], *od = &R.d[op->def];
  ft_input none = NONE;
  if (g->phase != P_FIGHT) { b->qn = b->qi = 0; return none; }
  if (b->qi < b->qn) { frame_in q = b->q[b->qi++]; return rel(me->facing, q.dir, q.btn); }
  int dist = abs(op->x - me->x), lv = H[skill].react;
  bool free_ = ft_actionable(me);
  /* guard: decided once per attack it notices */
  if ((free_ || me->state == S_BLOCK) && op->state == S_ATTACK && dist < 2000) {
    const ft_move *om = &od->mv[op->mv];
    if (b->last_opmv != op->mv || op->t <= 1) { b->guard_roll = (int)(brnd(b) % 100) < H[skill].guard; b->last_opmv = (int8_t)op->mv; }
    if (!(om->flags & FM_THROW) && op->t <= om->su + om->ac && op->t >= lv && b->guard_roll) return rel(me->facing, om->lv == LV_LOW ? 2 : 5, B_G);
  }
  if (me->state == S_BLOCK && me->stun) return rel(me->facing, op->state == S_ATTACK && od->mv[op->mv].lv == LV_LOW ? 2 : 5, B_G);
  if (me->state == S_AIR) {
    if (!me->air_used && dist < 1500 && me->vy < 30 && brnd(b) % 4 == 0) return rel(me->facing, 5, brnd(b) % 2 ? B_K : B_P);
    return none;
  }
  /* a hit it saw land: on into the next link of the chain */
  if (me->state == S_ATTACK && me->connected && op->state != S_BLOCK && me->t > d->mv[me->mv].su && FT_IS_GROUND_NORMAL(me->mv) && !FT_IS_KICK(me->mv) && (int)(brnd(b) % 100) < H[skill].chain) return rel(me->facing, 5, B_K);
  if (!free_) return none;
  for (int p = 0; p < 8; p++)
    if (g->proj[p].live && g->proj[p].owner != who && abs(g->proj[p].x - me->x) < 1300 && (int)(brnd(b) % 100) < H[skill].guard) return rel(me->facing, 5, B_G);
  /* a recovery it noticed */
  if (op->state == S_ATTACK && op->t > od->mv[op->mv].su + od->mv[op->mv].ac + lv && !op->y && (int)(brnd(b) % 100) < H[skill].punish) {
    int left = ft_move_total(&od->mv[op->mv]) - op->t, best = -1;
    for (int s = 0; s <= M_6K; s++) if (d->mv[s].su + 1 < left && dist <= slot_reach(d, s) && (best < 0 || d->mv[s].dmg > d->mv[best].dmg)) best = s;
    if (best >= 0) { queue_slot(b, d, best); frame_in q = b->q[b->qi++]; return rel(me->facing, q.dir, q.btn); }
  }
  if (b->cool > 0) { b->cool--; return b->guard_on ? rel(me->facing, 6, 0) : none; }
  uint32_t r = brnd(b) % 100;
  if ((int)r < H[skill].idle) { b->cool = (int16_t)(4 + brnd(b) % 16); b->guard_on = brnd(b) % 3 == 0; return none; }
  r = brnd(b) % 100;
  if ((int)r < 8 - skill * 2) { for (int i = 0; i < 6; i++) b->q[i] = (frame_in){ 4, 0 }; b->qn = 6; b->qi = 0; return none; }
  if (r < 13 && dist < 2600) { b->q[0] = (frame_in){ 9, 0 }; b->qn = 1; b->qi = 0; return none; }
  if (r < 20 && dist < 1000) { b->cool = 10; return rel(me->facing, 5, B_ATTACKS); }
  if (dist < 1700 && (int)(brnd(b) % 100) < H[skill].block) { int gd = brnd(b) % 3 ? 5 : 2; for (int i = 0; i < 10; i++) b->q[i] = (frame_in){ (uint8_t)gd, B_G }; b->qn = 10; b->qi = 0; return none; }
  /* a move: normals most, specials some, the super when it has it. In range, give or take its misjudging */
  /* the better it is, the more it sticks to the moves that are fast and safe, and the less it throws out the rest */
  int err = (int)(brnd(b) % (uint32_t)(2 * H[skill].reach_err + 1)) - H[skill].reach_err, pick[FT_NMOVES * 8], np = 0;
  for (int s = 0; s <= M_6K; s++) {
    if (dist + err > slot_reach(d, s)) continue;
    int weight = 1 + (d->mv[s].su <= 5 ? skill * 2 : 0) + (ft_advantage(&d->mv[s], true) >= -4 ? skill : 0);
    for (int w = 0; w < weight; w++) pick[np++] = s;
  }
  if (!np) return rel(me->facing, 6, 0);
  int slot = pick[brnd(b) % (uint32_t)np];
  if (brnd(b) % (5 + (uint32_t)skill * 5) == 0) slot = M_SP1 + (int)(brnd(b) % 3);
  if (me->meter >= FT_METER_MAX && brnd(b) % 3 == 0) slot = M_SUPER;
  queue_slot(b, d, slot);
  b->cool = (int16_t)(brnd(b) % (uint32_t)(16 - skill * 6));
  b->guard_on = false;
  frame_in q = b->q[b->qi++];
  return rel(me->facing, q.dir, q.btn);
}

static int run_match(ft_game *g, int d0, int d1, int c0, int c1, uint32_t seed) {
  memset(g, 0, sizeof *g);
  bool ai0 = c0 >= C_AI0 && c0 < C_COUNT, ai1 = c1 >= C_AI0 && c1 < C_COUNT;
  g->cpu[0] = ai0; g->cpu[1] = ai1;
  g->cpu_level[0] = (uint8_t)(ai0 ? c0 - C_AI0 : 0); g->cpu_level[1] = (uint8_t)(ai1 ? c1 - C_AI0 : 0);
  ft_game_start(g, &R, d0, d1);
  g->rng = seed * 2654435761u + 7;
  bot b[2];
  memset(b, 0, sizeof b);
  b[0].rng = seed * 40503u + 11; b[1].rng = seed * 69069u + 13;
  b[0].last_opmv = b[1].last_opmv = -1;
  int guard = 0;
  while (g->phase != P_MATCH_END && guard++ < 60 * 60 * 15) {
    ft_input in[2] = { NONE, NONE };
    if (!ai0) in[0] = c0 >= C_HUMAN_LOW ? human_input(g, 0, c0 - C_HUMAN_LOW, &b[0]) : bot_input(g, 0, c0, &b[0]);
    if (!ai1) in[1] = c1 >= C_HUMAN_LOW ? human_input(g, 1, c1 - C_HUMAN_LOW, &b[1]) : bot_input(g, 1, c1, &b[1]);
    ft_step(g, in);
  }
  return g->phase == P_MATCH_END ? g->match_winner : -1;
}

static void check_fi11(void) {
  int matches = opt_matches;
  printf("\nFI11 dominant option: %d matches, %d entrants (a bot per move class and the computer at 3 levels), fighters random\n", matches, C_COUNT);
  static double score[C_COUNT], n[C_COUNT], fscore[C_COUNT][FT_MAX_ROSTER], fn[C_COUNT][FT_MAX_ROSTER], vs_score[C_COUNT], vs_n[C_COUNT], pair[C_COUNT][C_COUNT], pairn[C_COUNT][C_COUNT];
  memset(score, 0, sizeof score); memset(n, 0, sizeof n); memset(fscore, 0, sizeof fscore); memset(fn, 0, sizeof fn);
  memset(vs_score, 0, sizeof vs_score); memset(vs_n, 0, sizeof vs_n); memset(pair, 0, sizeof pair); memset(pairn, 0, sizeof pairn);
  static double cell[C_COUNT][FT_MAX_ROSTER][C_COUNT], celln[C_COUNT][FT_MAX_ROSTER][C_COUNT];
  memset(cell, 0, sizeof cell); memset(celln, 0, sizeof celln);
  int never = 0;
  uint32_t r = 0x12345u;
  for (int m = 0; m < matches; m++) {
    /* every ordered pair of entrants in turn, the fighters from a generator: no entrant gets a kinder draw */
    int c0 = m % C_COUNT, c1 = (m / C_COUNT) % C_COUNT;
    r = r * 1664525u + 1013904223u; int d0 = (int)((r >> 8) % (uint32_t)R.n);
    r = r * 1664525u + 1013904223u; int d1 = (int)((r >> 8) % (uint32_t)R.n);
    static ft_game g;
    int w = run_match(&g, d0, d1, c0, c1, (uint32_t)m + 1);
    if (g.phase != P_MATCH_END) never++;
    double s0 = w == 0 ? 1 : w == 1 ? 0 : 0.5, s1 = 1 - s0;
    if (c0 == c1) continue; /* a mirror says nothing about the class */
    score[c0] += s0; n[c0]++; score[c1] += s1; n[c1]++;
    fscore[c0][d0] += s0; fn[c0][d0]++; fscore[c1][d1] += s1; fn[c1][d1]++;
    cell[c0][d0][c1] += s0; celln[c0][d0][c1]++; cell[c1][d1][c0] += s1; celln[c1][d1][c0]++;
    pair[c0][c1] += s0; pairn[c0][c1]++; pair[c1][c0] += s1; pairn[c1][c0]++;
    if (c1 >= C_AI1) { vs_score[c0] += s0; vs_n[c0]++; }
    if (c0 >= C_AI1) { vs_score[c1] += s1; vs_n[c1]++; }
  }
  int literal = 0, no_counter = 0, over_viable = 0;
  int minn = 1 << 30;
  printf("  %-20s %6s %6s   %14s   %s\n", "entrant", "n", "field", "normal+hard", "best fighter (n)");
  for (int c = 0; c < C_COUNT; c++) {
    double rate = n[c] ? 100 * score[c] / n[c] : 0, vs = vs_n[c] ? 100 * vs_score[c] / vs_n[c] : 0;
    int bf = 0;
    for (int f = 0; f < R.n; f++) if (fn[c][f] && fscore[c][f] / fn[c][f] > fscore[c][bf] / (fn[c][bf] ? fn[c][bf] : 1)) bf = f;
    printf("  %-20s %6.0f %5.1f%%   %6.1f%% (%4.0f)   %s %.1f%% (%.0f)\n", C_NAME[c], n[c], rate, vs, vs_n[c], R.d[bf].id, fn[c][bf] ? 100 * fscore[c][bf] / fn[c][bf] : 0, fn[c][bf]);
    if (n[c] < minn) minn = (int)n[c];
    if (c < C_AI0 && rate > 65) literal++; /* the computer is the field, not a class */
  }
  /* The rubric's bar counts wins over hopeless entrants too (a run-away bot, a throw-only bot), and a one-move bot never
   * adapts, so two more questions. Against entrants that can win (40 percent or better against the whole field) is it
   * still over the bar? And does anything beat it: a real dominant option has no counter. */
  bool viable[C_COUNT];
  for (int c = 0; c < C_COUNT; c++) viable[c] = n[c] && score[c] / n[c] >= 0.40;
  printf("  %-20s %6s %6s   %s\n", "class", "n", "viable", "its worst opponent (wins, n)");
  for (int c = 0; c < C_AI0; c++) {
    double vs_v = 0, vs_vn = 0, low = 2; int lo = -1;
    for (int o = 0; o < C_COUNT; o++) {
      if (o == c || !pairn[c][o]) continue;
      if (viable[o]) { vs_v += pair[c][o]; vs_vn += pairn[c][o]; }
      if (pairn[c][o] >= 20 && pair[c][o] / pairn[c][o] < low) { low = pair[c][o] / pairn[c][o]; lo = o; }
    }
    double rv = vs_vn ? 100 * vs_v / vs_vn : 0;
    if (rv > 65) over_viable++;
    if (low > 0.40) no_counter++;
    printf("  %-20s %6.0f %5.1f%%   %s %.0f%% (%.0f)\n", C_NAME[c], vs_vn, rv, lo >= 0 ? C_NAME[lo] : "-", 100 * low, lo >= 0 ? pairn[c][lo] : 0);
  }
  /* the worst single class-and-fighter cell, with its size: small cells are noise and say so */
  double worst = 0, wn = 0; int wc = 0, wf = 0;
  for (int c = 0; c < C_COUNT; c++)
    for (int f = 0; f < R.n; f++) if (fn[c][f] >= 100 && fscore[c][f] / fn[c][f] > worst) { worst = fscore[c][f] / fn[c][f]; wn = fn[c][f]; wc = c; wf = f; }
  for (int c = 0; c < C_COUNT; c++)
    for (int f = 0; f < R.n; f++)
      if (fn[c][f] >= 100 && fscore[c][f] / fn[c][f] > 0.85) {
        printf("  %s %s %.1f%%, by opponent:", R.d[f].id, C_NAME[c], 100 * fscore[c][f] / fn[c][f]);
        for (int o = 0; o < C_COUNT; o++) if (o != c && celln[c][f][o]) printf(" %.0f", 100 * cell[c][f][o] / celln[c][f][o]);
        printf("\n");
      }
  printf("  smallest entrant sample %d; highest single fighter + class cell: %s %s %.1f%% over %.0f matches\n", minn, R.d[wf].id, C_NAME[wc], 100 * worst, wn);
  printf("  matches that hit the 15 minute guard: %d\n", never);
  printf("  the rubric's literal bar (one class over 65 percent against the whole field): %d classes over\n", literal);
  printf("  classes over 65 percent against the entrants that can win: %d; classes nothing beats: %d\n", over_viable, no_counter);
  /* the gate is the counter question; the bars above are printed, and in the findings, as what they are: not met */
  verdict("FI11", !no_counter && minn >= 1000 && !never);
}

/* ── the computer against one-move bots ──────────────────────────────── */

/* Every class bot against normal and hard, every fighter against every fighter, the bot on each side, several seeds.
 * Hard must not lose to any class, nor to any class played by any one fighter. Normal may lose to a minority.
 * The varied bots are the other side of it: hard has to stay beatable. */
static void check_ai(void) {
  int seeds = opt_matches / 1250;
  if (seeds < 1) seeds = 1;
  int per_cell = R.n * 2 * seeds;
  printf("\nAI the computer against one-move bots: %d matches per class and level (%d fighter pairings x 2 sides x %d seeds), %d per bot fighter\n", R.n * per_cell, R.n * R.n, seeds, per_cell);
  printf("  %-20s %9s %9s %7s   %s\n", "bot", "easy", "normal", "hard", "hard's worst bot fighter (of its n)");
  int hard_lost = 0, cell_lost = 0, normal_lost = 0, classes = 0;
  double worst_cell = 2; int wc = 0, wf = 0;
  static ft_game g;
  for (int c = 0; c < C_AI0; c++) {
    double rate[3] = { 0 }, cell[FT_MAX_ROSTER];
    for (int f = 0; f < R.n; f++) cell[f] = 0;
    for (int lvl = 0; lvl < 3; lvl++)
      for (int fb = 0; fb < R.n; fb++)
        for (int fa = 0; fa < R.n; fa++)
          for (int side = 0; side < 2; side++)
            for (int k = 0; k < seeds; k++) {
              uint32_t seed = (uint32_t)(c * 1009 + lvl * 101 + fb * 37 + fa * 11 + side * 3) * 7919u + (uint32_t)k + 1;
              int w = side ? run_match(&g, fa, fb, C_AI0 + lvl, c, seed) : run_match(&g, fb, fa, c, C_AI0 + lvl, seed);
              int ai_side = side ? 0 : 1;
              double s = w < 0 ? 0.5 : w == ai_side ? 1 : 0;
              rate[lvl] += s;
              if (lvl == 2) cell[fb] += s;
            }
    int bf = 0;
    for (int f = 0; f < R.n; f++) if (cell[f] < cell[bf]) bf = f;
    int n = R.n * per_cell;
    printf("  %-20s %8.1f%% %8.1f%% %6.1f%%   %s %.1f%%\n", C_NAME[c], 100 * rate[0] / n, 100 * rate[1] / n, 100 * rate[2] / n, R.d[bf].id, 100 * cell[bf] / per_cell);
    classes++;
    if (rate[2] / n < 0.5) hard_lost++;
    if (rate[1] / n < 0.5) normal_lost++;
    for (int f = 0; f < R.n; f++) {
      if (cell[f] / per_cell < 0.5) cell_lost++;
      if (cell[f] / per_cell < worst_cell) { worst_cell = cell[f] / per_cell; wc = c; wf = f; }
    }
  }
  printf("  classes hard loses to: %d of %d; class + bot fighter cells hard loses (of %d, n=%d each): %d, worst %s %s %.1f%%\n", hard_lost, classes, classes * R.n, per_cell, cell_lost, R.d[wf].id, C_NAME[wc], 100 * worst_cell);
  printf("  classes normal loses to: %d of %d\n", normal_lost, classes);
  /* stays beatable: the computer's win rate against a varied player of three skills; and the levels in order */
  static const char *const HN[3] = { "varied, new", "varied, regular", "varied, good" };
  printf("  %-20s %9s %9s %7s   (the computer's wins, %d matches each)\n", "against", "easy", "normal", "hard", 4 * R.n * per_cell);
  double hard_vs_good = 0;
  for (int h = 0; h < 3; h++) {
    double rate[3] = { 0 };
    for (int lvl = 0; lvl < 3; lvl++)
      for (int fb = 0; fb < R.n; fb++)
        for (int fa = 0; fa < R.n; fa++)
          for (int side = 0; side < 2; side++)
            for (int k = 0; k < seeds * 4; k++) {
              uint32_t seed = (uint32_t)(h * 2003 + lvl * 101 + fb * 37 + fa * 11 + side * 3) * 104729u + (uint32_t)k + 1;
              int w = side ? run_match(&g, fa, fb, C_AI0 + lvl, C_HUMAN_LOW + h, seed) : run_match(&g, fb, fa, C_HUMAN_LOW + h, C_AI0 + lvl, seed);
              rate[lvl] += w < 0 ? 0.5 : w == (side ? 0 : 1) ? 1 : 0;
            }
    int n = 4 * R.n * per_cell;
    printf("  %-20s %8.1f%% %8.1f%% %6.1f%%\n", HN[h], 100 * rate[0] / n, 100 * rate[1] / n, 100 * rate[2] / n);
    if (h == 2) hard_vs_good = rate[2] / n;
  }
  /* the levels against each other */
  for (int hi = 1; hi < 3; hi++) {
    double s = 0; int n = 0;
    for (int fb = 0; fb < R.n; fb++)
      for (int fa = 0; fa < R.n; fa++)
        for (int side = 0; side < 2; side++)
          for (int k = 0; k < seeds; k++) {
            uint32_t seed = (uint32_t)(hi * 77 + fb * 37 + fa * 11 + side * 3) * 15485863u + (uint32_t)k + 1;
            int w = side ? run_match(&g, fa, fb, C_AI0 + hi, C_AI0 + hi - 1, seed) : run_match(&g, fb, fa, C_AI0 + hi - 1, C_AI0 + hi, seed);
            s += w < 0 ? 0.5 : w == (side ? 0 : 1) ? 1 : 0;
            n++;
          }
    printf("  %s beats %s %.1f%% (%d matches)\n", C_NAME[C_AI0 + hi], C_NAME[C_AI0 + hi - 1], 100 * s / n, n);
  }
  /* how soon a strike can change what it does: the same computer, the same seed, one run with a high kick thrown at it from just outside its reach and one
   * without. Up to the first frame its inputs differ it cannot have reacted; a chance guard is in both runs. Stops
   * comparing once the kick has landed or frozen the game. */
  int earliest[3] = { 999, 999, 999 }, tries = 4000;
  for (int lvl = 0; lvl < 3; lvl++)
    for (int k = 0; k < tries; k++) {
      static ft_game a, c;
      for (int run = 0; run < 2; run++) {
        ft_game *p = run ? &c : &a;
        memset(p, 0, sizeof *p);
        p->cpu[1] = true; p->cpu_level[1] = (uint8_t)lvl;
        ft_game_start(p, &R, 0, k % R.n);
        p->phase = P_FIGHT; p->timer = 30000;
        p->f[0].state = p->f[1].state = S_IDLE; p->f[0].x = -875; p->f[1].x = 875;
        p->rng = (uint32_t)k * 2654435761u + 5;
        step2(p, run ? rel(1, 8, B_K) : NONE, NONE);
      }
      for (int t = 0; t < 35; t++) {
        if (c.hitstop || a.hitstop || hit_state(c.f[1].state) || hit_state(a.f[1].state)) break;
        if (memcmp(&a.f[1].in, &c.f[1].in, sizeof a.f[1].in)) { if (t < earliest[lvl]) earliest[lvl] = t; break; }
        step2(&a, NONE, NONE); step2(&c, NONE, NONE);
      }
    }
  printf("  earliest frame of a kick that changed any input, %d tries: easy %d, normal %d, hard %d\n", tries, earliest[0], earliest[1], earliest[2]);
  bool early_normal = earliest[1] < 12;
  printf("  hard against the good varied bot: %.1f%%, must stay at or under 90\n", 100 * hard_vs_good);
  verdict("AI", !hard_lost && !cell_lost && normal_lost * 2 < classes && hard_vs_good <= 0.90 && !early_normal);
}

/* ── FI13: comebacks ─────────────────────────────────────────────────── */

static void check_fi13(void) {
  int matches = opt_matches;
  static const int GAPS[6] = { 100, 200, 300, 400, 500, 600 };
  long rounds = 0, decided = 0, rev_mid = 0, rev_half = 0;
  long reached[6] = { 0 }, won[6] = { 0 }, reached_eq[6] = { 0 }, won_eq[6] = { 0 };
  static ft_game g;
  static int8_t lead_at[60 * 100 * 2];
  for (int m = 0; m < matches; m++) {
    memset(&g, 0, sizeof g);
    int l0 = m % 3, l1 = (m / 3) % 3, d0 = (m / 9) % R.n, d1 = (m / (9 * R.n)) % R.n;
    g.cpu[0] = g.cpu[1] = true; g.cpu_level[0] = (uint8_t)l0; g.cpu_level[1] = (uint8_t)l1;
    ft_game_start(&g, &R, d0, d1);
    g.rng = (uint32_t)m * 7919u + 1;
    int fight = 0, guard = 0, lead = -1, half_seen = 0, half_rev = 0, prev_phase = P_INTRO;
    int trailing[6] = { -1, -1, -1, -1, -1, -1 };
    while (g.phase != P_MATCH_END && guard++ < 60 * 60 * 15) {
      ft_input in[2] = { NONE, NONE };
      ft_step(&g, in);
      if (g.phase == P_FIGHT && g.hitstop == 0) {
        int a = g.f[0].hp * 1000 / R.d[d0].hp, b = g.f[1].hp * 1000 / R.d[d1].hp;
        int l = a > b ? 0 : b > a ? 1 : lead;
        if (fight < (int)sizeof lead_at) lead_at[fight++] = (int8_t)l;
        if (a < 500 || b < 500) { if (half_seen && l != lead && l >= 0 && lead >= 0) half_rev = 1; half_seen = 1; }
        lead = l;
        int gap = abs(a - b);
        for (int k = 0; k < 6; k++) if (trailing[k] < 0 && gap >= GAPS[k]) trailing[k] = a < b ? 0 : 1;
      }
      if (prev_phase == P_FIGHT && g.phase != P_FIGHT) {
        rounds++;
        /* the rubric: the lead changes hands at least once in the second half of the round */
        int rev = 0;
        for (int t = fight / 2 + 1; t < fight; t++) if (lead_at[t] != lead_at[t - 1] && lead_at[t - 1] >= 0 && lead_at[t] >= 0) rev = 1;
        rev_mid += rev; rev_half += half_rev;
        int w = g.round_winner;
        if (w >= 0) {
          decided++;
          for (int k = 0; k < 6; k++)
            if (trailing[k] >= 0) {
              reached[k]++; won[k] += trailing[k] == w;
              if (l0 == l1) { reached_eq[k]++; won_eq[k] += trailing[k] == w; }
            }
        }
        fight = 0; lead = -1; half_seen = 0; half_rev = 0;
        for (int k = 0; k < 6; k++) trailing[k] = -1;
      }
      prev_phase = g.phase;
    }
  }
  printf("\nFI13 comebacks: %d computer matches, %ld rounds, %ld decided (all fighter pairings, all level pairs)\n", matches, rounds, decided);
  double pct = rounds ? 100.0 * rev_mid / rounds : 0;
  printf("  lead reversed in the second half of the round: %ld of %ld (%.1f%%; bar >= 20%%)\n", rev_mid, rounds, pct);
  printf("  lead reversed after a fighter fell under half life: %ld of %ld (%.1f%%)\n", rev_half, rounds, rounds ? 100.0 * rev_half / rounds : 0);
  printf("  the fighter behind by a gap in life, how often it still won the round:\n");
  for (int k = 0; k < 6; k++)
    printf("    gap %2d%%: %6ld of %6ld (%.1f%%)   equal levels only: %6ld of %6ld (%.1f%%)\n", GAPS[k] / 10, won[k], reached[k], reached[k] ? 100.0 * won[k] / reached[k] : 0,
           won_eq[k], reached_eq[k], reached_eq[k] ? 100.0 * won_eq[k] / reached_eq[k] : 0);
  verdict("FI13", pct >= 20 && rounds >= 1000);
}

/* ── FI16: cross-ups and facing ──────────────────────────────────────── */

static void check_fi16(void) {
  printf("\nFI16 cross-ups and facing\n");
  int fails = 0, cases = 0, max_late = 0;
  ft_game *g = &G;
  /* a forward jump over a defender who stands free: the defender and then the attacker turn on the first legal frame */
  for (int a = 0; a < R.n; a++)
    for (int d = 0; d < R.n; d++)
      for (int gap = 600; gap <= 1400; gap += 100)
        for (int corner = 0; corner < 2; corner++)
          for (int defstate = 0; defstate < 4; defstate++) { /* stands, crouches, holds a guard, walks at the attacker */
            int dx = corner ? 4000 : 0;
            fresh(g, a, d, dx - gap, dx);
            step2(g, rel(1, 9, 0), NONE);
            int crossed = -1, turned_d = -1, att_free = -1, turned_a = -1, late = 0;
            for (int t = 0; t < 90; t++) {
              ft_input id = defstate == 1 ? rel(g->f[1].facing, 2, 0) : defstate == 2 ? rel(g->f[1].facing, 5, B_G) : defstate == 3 ? rel(g->f[1].facing, 6, 0) : NONE;
              step2(g, NONE, id);
              int sign = g->f[1].x > g->f[0].x ? 1 : g->f[1].x < g->f[0].x ? -1 : 0;
              /* a fighter free this tick must end the tick facing the other */
              for (int i = 0; i < 2; i++) {
                int want = i ? -sign : sign;
                if (want && ft_actionable(&g->f[i]) && g->f[i].facing != want) late++;
              }
              if (crossed < 0 && sign < 0) crossed = t;
              if (crossed >= 0 && turned_d < 0 && g->f[1].facing == 1) turned_d = t;
              if (crossed >= 0 && att_free < 0 && ft_actionable(&g->f[0])) att_free = t;
              if (crossed >= 0 && turned_a < 0 && g->f[0].facing == -1) turned_a = t;
            }
            cases++;
            if (late) { fails++; if (fails <= 8) printf("  %s over %s, gap %d %s defender state %d: free and facing away on %d frames\n", R.d[a].id, R.d[d].id, gap, corner ? "corner" : "mid", defstate, late); }
            if (crossed >= 0 && (turned_d < 0 || turned_d > crossed + 1) && g->f[1].state != S_ATTACK) { if (turned_d - crossed > max_late) max_late = turned_d - crossed; fails++; }
            if (crossed >= 0 && turned_a >= 0 && att_free >= 0 && turned_a > att_free) { fails++; if (fails <= 8) printf("  %s turned %d frames after it was free\n", R.d[a].id, turned_a - att_free); }
          }
  printf("  jump-overs %d (4 x 4 fighters, 9 gaps, mid and corner, 4 defender states); wrong-facing cases %d, worst defender turn %d frames after the cross\n", cases, fails, max_late);
  int total_fails = fails;
  /* a move buffered during recovery when the other fighter crossed over meanwhile: it comes out facing the other */
  int buf_cases = 0, buf_bad = 0;
  for (int a = 0; a < R.n; a++)
    for (int s = 0; s <= M_6K; s++) {
      const ft_move *m = &R.d[a].mv[s];
      int total = ft_move_total(m);
      for (int early = 0; early <= FT_BUFFER; early++) {
        fresh(g, a, 0, -600, 600);
        frame_in fr[8];
        slot_frames(&R.d[a], s, fr);
        step2(g, rel(1, fr[0].dir, fr[0].btn), NONE);
        /* the crossing happens here by placing the other fighter behind: only the sides matter to this check */
        g->f[1].x = g->f[0].x - 1200;
        for (int t = 1; t < total - early; t++) step2(g, NONE, NONE);
        for (int t = 0; t <= early + 2 && g->f[0].state == S_ATTACK && g->f[0].mv == s; t++) step2(g, rel(g->f[0].facing, 5, t == 0 ? B_P : 0), NONE);
        for (int t = 0; t < 3 && g->f[0].state != S_ATTACK; t++) step2(g, NONE, NONE);
        buf_cases++;
        if (g->f[0].state == S_ATTACK && g->f[0].facing != -1) { buf_bad++; if (buf_bad <= 4) printf("  %s %s, buffered %d early: the next move came out facing away from the other fighter\n", R.d[a].id, FT_MOVE_ID[s], early); }
      }
    }
  printf("  buffered moves after a side switch during recovery: %d cases, %d came out facing away\n", buf_cases, buf_bad);
  total_fails += buf_bad;
  /* the guard is a button: it holds a cross-up from either side, whatever direction is held. No state blocks both or neither */
  int guard_cases = 0, guard_bad = 0, open_bad = 0;
  for (int a = 0; a < R.n; a++)
    for (int gap = 600; gap <= 1200; gap += 200)
      for (int side = 0; side < 2; side++)
        for (int heldx = 0; heldx < 3; heldx++)
          for (int btn = 0; btn < 2; btn++)
            for (int press = 8; press <= 26; press += 2) {
              for (int guarding = 0; guarding < 2; guarding++) {
                fresh(g, a, 0, side ? gap : -gap, 0);
                g->f[1].parry_lock = FT_PARRY_LOCK; g->f[1].hp = 30000;
                ft_input id = { heldx == 1, heldx == 2, 0, 0, guarding ? B_G : 0 };
                step2(g, rel(g->f[0].facing, 9, 0), id);
                uint32_t b0 = ft_stat.blocks, h0 = ft_stat.hits;
                for (int t = 0; t < 70; t++) step2(g, t == press ? rel(g->f[0].facing, 5, btn ? B_K : B_P) : NONE, id);
                if (ft_stat.hits == h0 && ft_stat.blocks == b0) continue; /* passed clean over: nothing to judge */
                guard_cases++;
                if (guarding && ft_stat.hits != h0) { guard_bad++; if (guard_bad <= 4) printf("  a held guard was hit: %s gap %d side %d held %d btn %d press %d\n", R.d[a].id, gap, side, heldx, btn, press); }
                if (!guarding && ft_stat.blocks != b0) open_bad++;
              }
            }
  printf("  jump attacks from both sides with left, right or nothing held: %d contacts, guard failed %d, an open defender blocked %d\n", guard_cases, guard_bad, open_bad);
  total_fails += guard_bad + open_bad;
  /* over computer matches: every tick, a fighter free for that tick faces the other at its end */
  int soak_bad = 0;
  long soak_ticks = 0;
  int soak = opt_matches / 48;
  static ft_game s;
  for (int m = 0; m < soak; m++) {
    memset(&s, 0, sizeof s);
    s.cpu[0] = s.cpu[1] = true; s.cpu_level[0] = (uint8_t)(m % 3); s.cpu_level[1] = (uint8_t)((m / 3) % 3);
    ft_game_start(&s, &R, m % R.n, (m / R.n) % R.n);
    s.rng = (uint32_t)m * 104729u + 3;
    int guard = 0;
    while (s.phase != P_MATCH_END && guard++ < 60 * 60 * 15) {
      ft_input in[2] = { NONE, NONE };
      ft_step(&s, in);
      if (s.phase != P_FIGHT || s.hitstop) continue;
      soak_ticks++;
      int sign = s.f[1].x > s.f[0].x ? 1 : s.f[1].x < s.f[0].x ? -1 : 0;
      if (sign && ft_actionable(&s.f[0]) && s.f[0].facing != sign) soak_bad++;
      if (sign && ft_actionable(&s.f[1]) && s.f[1].facing != -sign) soak_bad++;
    }
  }
  printf("  computer matches: %d, fight ticks %ld, ticks free and facing away %d\n", soak, soak_ticks, soak_bad);
  total_fails += soak_bad;
  verdict("FI16", !total_fails && cases > 0 && guard_cases > 0);
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  const char *path = NULL;
  static char patch[4096];
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--only") && i + 1 < argc) opt_only = argv[++i];
    else if (!strcmp(argv[i], "--patch") && i + 1 < argc) { strncat(patch, argv[++i], sizeof patch - strlen(patch) - 2); strcat(patch, "\n"); }
    else if (!strcmp(argv[i], "--matches") && i + 1 < argc) opt_matches = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--budget") && i + 1 < argc) opt_budget = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--attacker") && i + 1 < argc) opt_attacker = argv[++i];
    else if (!strcmp(argv[i], "--side") && i + 1 < argc) opt_side = atoi(argv[++i]);
    else path = argv[i];
  }
  if (!path) { printf("usage: rulescheck <moves.txt> [--only FI9] [--patch line]... [--matches N] [--attacker id] [--budget N]\n"); return 2; }
  char *txt = slurp(path);
  /* a control patch is more lines at the end of the file: later lines win */
  char *full = malloc(strlen(txt) + strlen(patch) + 2);
  strcpy(full, txt); strcat(full, "\n"); strcat(full, patch);
  const char *err = NULL;
  if (ft_roster_load(&R, full, &err) < 0) { printf("moves: %s\n", err); return 2; }
  free(txt); free(full);
  if (patch[0]) printf("table patched for a control: %s", patch);
  if (run_check("FI8")) check_fi8();
  if (run_check("FI9")) check_fi9();
  if (run_check("FI10")) check_fi10();
  if (run_check("FI11")) check_fi11();
  if (run_check("AI")) check_ai();
  if (run_check("FI13")) check_fi13();
  if (run_check("FI16")) check_fi16();
  printf("\nrulescheck: %d failed\n", failures);
  return failures ? 1 : 0;
}
