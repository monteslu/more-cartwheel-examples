/* Party Pack (working title): a party game for one to four players on one
 * shared screen; CPUs fill the empty seats.
 *
 * Flow (the shell does title, join, pause, settings and saves):
 *   menu -> party tournament (board, one mini-game per round, points move the
 *   tokens along the ladder) or free play (any unlocked mini-game) or the
 *   trophy room. Each mini-game runs: intro card with ready-up, 3-2-1, play,
 *   finish, results. The framework lives here; the games are in g_*.c and
 *   see mg.h.
 *
 * PP_AUTOPILOT builds play a whole tournament with four CPU players and no
 * input (tests, screenshots).
 *
 * Debug fields: game.state game.round game.mg game.tick game.score[4]
 * game.points[4] game.hash game.points_total game.unlocked */
#include "mg.h"
#include <stdlib.h>

#include "cartwheel/platform.h"
#include "cartwheel/project.h"
#include "cartwheel/quality.h"
#include "cartwheel/shell.h"

int MG_N;
const mg_def *MG_LIST[MG_MAX];
static void register_games(void) {
  static const mg_def *all[] = {
    &mg_dodge, &mg_memory, &mg_beat, &mg_quiz, &mg_blast, &mg_raft, &mg_tower, &mg_kart,
    &mg_sumo, &mg_coins, &mg_tiles,
  };
  MG_N = (int)(sizeof all / sizeof all[0]);
  for (int i = 0; i < MG_N; i++) MG_LIST[i] = all[i];
}

/* ── save ───────────────────────────────────────────────────────────── */
typedef struct save_data {
  int32_t points_total;    /* lifetime party points of the people playing */
  int32_t tournaments;     /* tournaments finished */
  int32_t wins[NP];        /* tournaments won by seat */
  int32_t plays[MG_MAX];
  float best[MG_MAX];      /* best score per mini-game */
  uint32_t unlocked;       /* bit per mini-game */
  int32_t rounds_opt;      /* 0..2 -> 4, 6, 8 rounds */
  int32_t skill;           /* CPU skill 0..2 */
} save_data;
static save_data *SV;

/* ── state ──────────────────────────────────────────────────────────── */
enum { S_MENU, S_PICK, S_TROPHY, S_BOARD, S_INTRO, S_COUNT, S_PLAY, S_FINISH, S_RESULT, S_FINAL };

static struct {
  int32_t state, st_ticks;     /* state and ticks spent in it */
  int32_t menu_item, pick_item;
  bool tournament;
  int32_t rounds, round;       /* tournament length and current round */
  int32_t order[32];           /* mini-game per round */
  int32_t cur;                 /* mini-game now (index into MG_LIST) */
  int32_t points[NP];          /* tournament points */
  int32_t pos[NP], from[NP];   /* board cells now and at the round's start */
  uint32_t star_taken;         /* star cells already claimed */
  int32_t round_pts[NP], place[NP];
  bool ready[NP];
  mg_ctx c;
  int32_t serial;
  uint32_t hash;
  bool scene3d;                /* a game's scene is loaded */
  int32_t unlock_toast;        /* mini-game just unlocked (or -1) */
  int32_t winner;
  float clock;                 /* seconds, render time for animation */
  bool new_best;
  int32_t humans;
  bool human[NP];              /* seat p is held by a person (seats keep their numbers when players join or leave) */
  int32_t mid[NP];             /* points after the round's own points, before the board events */
  int32_t underdog[NP];        /* the catch-up point a seat got this round (0 or 1) */
  int32_t round_wins[NP];      /* rounds won this tournament (the tie-break after points) */
  uint64_t ev_taken[4];        /* board cells whose event is used up, per event kind */
  struct { int32_t kind, p, q, amt; } ev[16]; /* what happened on the board this round, in order */
  int32_t nev;
  const char *tiebreak;        /* what split a tie for first on the final screen (NULL: no tie) */
  int32_t hitstop_shown;
  int32_t hist[16][NP];        /* points after each round (balance runs) */
  int32_t hold;                /* ticks left of a hit-stop (a freeze on a big hit) */
  float load_ms, start_ms;     /* measured: scene switch and the game's own setup (not in gates) */
} G;

static cw_action a_left, a_right, a_up, a_down, a_a, a_b, a_x, a_y, u_up, u_down, u_left, u_right, u_ok, u_back;
static cw_bus bus_music, bus_sfx;
cw_bus pp_music_bus(void) { return bus_music; }
cw_bus pp_sfx_bus(void) { return bus_sfx; }

static const mg_def *cur_def(void) { return MG_LIST[G.cur]; }
static bool is_unlocked(int i) { return (SV->unlocked >> i) & 1; }

static void set_state(int s) { G.state = s; G.st_ticks = 0; }

/* Screens whose 3D scene is not on screen (the menus, the board, the 2D mini-games draw over a cleared window) ask
 * the renderer for the least: a quality scope lowers resolution, shadows, ambient occlusion, anti-aliasing and
 * effects for the hub scene they leave unseen, and ends when a 3D mini-game's scene is shown. */
static bool scoped;
static void apply_scope(void) {
  bool want = !G.scene3d;
  if (want == scoped) return;
  if (want) {
    static const int8_t caps[CW_GFX_ENGINE_COUNT] = { 0, 0, 0, 0, 0 }; /* resolution, ssao, shadows, aa, effects: lowest */
    scoped = cw_quality_scope_push("hub", caps);
  } else {
    cw_quality_scope_pop();
    scoped = false;
  }
}

#ifdef PP_AUTOPILOT
static int batch_total, batch_done; /* balance runs, see batch_run */
#define AUTO 1
#else
#define AUTO 0
#endif

/* ── input helpers ──────────────────────────────────────────────────── */

static bool any_human_pressed(cw_action a) {
  for (int p = 0; p < NP; p++) if (G.human[p] && cw_action_just_pressed(p, a)) return true;
  return false;
}
static int menu_nav(void) { /* -2 up, 2 down, -1 left, 1 right, 0 none */
  if (any_human_pressed(u_up)) return -2;
  if (any_human_pressed(u_down)) return 2;
  if (any_human_pressed(u_left)) return -1;
  if (any_human_pressed(u_right)) return 1;
  return 0;
}
static bool menu_ok(void) { return any_human_pressed(u_ok) || any_human_pressed(a_a); }
static bool menu_back(void) { return any_human_pressed(u_back) || any_human_pressed(a_b); }

/* ── 3D scene switching ─────────────────────────────────────────────── */

static bool G_hub_live; /* the hub scene is the one loaded */
static int board_cells(void);
static void load_scene(const char *path) {
  G_hub_live = path == NULL;
#ifdef PP_AUTOPILOT
  extern int pp_batch_active(void);
  if (pp_batch_active()) return; /* a balance run has 2D games only: no scene to load */
#endif
  if (!path) path = "scenes/hub.cwscene";
  cw_scene_app_change_scene(path, false);
  if (G_hub_live) hub_build(G.humans);
}

/* ── mini-game lifecycle ────────────────────────────────────────────── */

static void begin_game(int idx) {
  G.cur = idx;
  const mg_def *d = cur_def();
  memset(&G.c.score, 0, sizeof G.c.score);
  memset(&G.c.eliminated, 0, sizeof G.c.eliminated);
  G.c.humans = G.humans;
  for (int p = 0; p < NP; p++) G.c.human[p] = G.human[p];
  G.c.skill = SV->skill;
  for (int p = 0; p < NP; p++) G.c.cpu_skill[p] = SV->skill % 3;
#ifdef PP_AUTOPILOT
  { const char *sk = getenv("PP_SKILLS"); /* "2,1,1,0": each seat's CPU skill, for the balance runs */
    if (sk) for (int p = 0; p < NP && sk[2 * p]; p++) G.c.cpu_skill[p] = sk[2 * p] - '0'; }
#endif
  G.c.round = G.round;
  G.c.tick = 0;
  G.c.done = false;
  G.c.time_total = G.c.time_left = d->seconds;
  cw_rng_seed(&G.c.rng, 9000u + (uint32_t)G.serial * 7919u + (uint32_t)idx * 131u);
  G.serial++;
  fx_reset();
  G.scene3d = d->scene != NULL;
  apply_scope();
  double t0 = cw_time();
  load_scene(d->scene);
  G.load_ms = (float)((cw_time() - t0) * 1000.0);
  d->start(&G.c);
  G.start_ms = (float)((cw_time() - t0) * 1000.0) - G.load_ms;
  memset(G.ready, 0, sizeof G.ready);
  for (int p = 0; p < NP; p++) G.ready[p] = !G.human[p];
  set_state(S_INTRO);
  sfx_music(1);
}

static void end_game_cleanup(void) {
  if (G.hold > 0 && G.scene3d && cw_scene_app_tree()) cw_tree_set_paused(cw_scene_app_tree(), false);
  G.hold = 0;
  if (G.cur >= 0 && G.cur < MG_N && cur_def()->stop) cur_def()->stop(&G.c);
  if (G.scene3d) load_scene(NULL);
  G.scene3d = false;
  apply_scope();
}

/* Points per place: the winner gets 5, then 3, 2, 1; the round before the last counts double and the
 * last one triple. Every seat that is 5 or more
 * points behind the leader gets 1 more (the underdog point, 20% of the winner's 5, shown on the board). */
static const int PLACE_PTS[4] = { 5, 3, 2, 1 };
enum { UNDERDOG_GAP = 5 };

/* Board cells with an event. Each cell's event happens once, to the first seat that lands on it exactly. */
enum { EV_NONE, EV_STAR, EV_STEAL, EV_SWAP, EV_UNDERDOG };
static int board_cells(void) { return G.rounds * 5 + 10; }
static int cell_kind(int cell) {
  if (cell <= 0) return EV_NONE;
  if (cell % 7 == 5) return EV_STAR;
  if (cell % 11 == 8) return EV_STEAL;
  if (cell % 13 == 10) return EV_SWAP;
  return EV_NONE;
}
static bool is_star(int cell) { return cell_kind(cell) == EV_STAR; }
static bool cell_used(int cell) { return cell < 64 && ((G.ev_taken[0] >> cell) & 1); }
static int per_row(void) { int n = board_cells() + 1; return n <= 40 ? 10 : (n + 3) / 4; }
#define BOARD_PER_ROW per_row()

/* the round before the last counts double and the last one triple (said on the board before they start): a seat
 * behind after the first rounds can still win, which the comeback target asks for */
static bool is_double_round(int r) { return r >= G.rounds - 2; }

static void rank_players(void) {
  const mg_def *d = cur_def();
  int idx[NP] = { 0, 1, 2, 3 };
  for (int i = 1; i < NP; i++) /* stable insertion sort, best first */
    for (int j = i; j > 0; j--) {
      float a = G.c.score[idx[j - 1]], b = G.c.score[idx[j]];
      bool swap = d->higher_is_better ? b > a : b < a;
      if (!swap) break;
      int t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t;
    }
  int place = 0;
  for (int i = 0; i < NP; i++) {
    if (i > 0 && G.c.score[idx[i]] != G.c.score[idx[i - 1]]) place = i;
    G.place[idx[i]] = place;
  }
  int mult = !G.tournament ? 1 : G.round == G.rounds - 1 ? 3 : is_double_round(G.round) ? 2 : 1;
  for (int p = 0; p < NP; p++) G.round_pts[p] = PLACE_PTS[G.place[p]] * mult;
}


static void ev_add(int kind, int p, int q, int amt) {
  if (G.nev < 16) G.ev[G.nev].kind = kind, G.ev[G.nev].p = p, G.ev[G.nev].q = q, G.ev[G.nev].amt = amt, G.nev++;
}

/* One round on the board: the round's points, the underdog point, then each seat's landing cell, best place first.
 * Everything here is deterministic (the seat order and the game's rng) and every step is told to the players. */
static void board_round(void) {
  G.nev = 0;
  int leader = 0;
  for (int p = 1; p < NP; p++) if (G.points[p] > G.points[leader]) leader = p;
  int low = 0;
  for (int p = 1; p < NP; p++) if (G.points[p] < G.points[low]) low = p;
  memset(G.underdog, 0, sizeof G.underdog);
  for (int p = 0; p < NP; p++) {
    G.from[p] = G.points[p];
    G.points[p] += G.round_pts[p];
    if (G.place[p] == 0) G.round_wins[p]++;
  }
  for (int p = 0; p < NP; p++)
    if (G.from[p] <= G.from[leader] - UNDERDOG_GAP && G.place[p] != 0) { G.underdog[p] = 1; G.points[p] += 1; ev_add(EV_UNDERDOG, p, -1, 1); }
  for (int p = 0; p < NP; p++) G.mid[p] = G.points[p];
  int order[NP], k = 0;
  (void)k;
  for (int pl = 0; pl < NP; pl++) for (int j = 0; j < NP; j++) { int p = (j + G.round) % NP; if (G.place[p] == pl) order[k++] = p; } /* tied places: the seat that goes first rotates */
  cw_rng r;
  cw_rng_seed(&r, 31337u + (uint32_t)G.serial * 101u + (uint32_t)G.round);
  for (int i = 0; i < NP; i++) {
    int p = order[i], cell = G.points[p];
    if (cell <= 0 || cell > board_cells()) continue;
    int kind = cell_kind(cell);
    if (kind == EV_NONE || cell_used(cell)) continue;
    G.ev_taken[0] |= 1ull << cell;
    if (kind == EV_STAR) { G.points[p] += 2; ev_add(EV_STAR, p, -1, 2); }
    else if (kind == EV_STEAL) { /* take 2 from the seat just ahead of you; nothing when you lead */
      int ahead = -1;
      for (int q = 0; q < NP; q++) if (q != p && G.points[q] > G.points[p] && (ahead < 0 || G.points[q] < G.points[ahead])) ahead = q;
      if (ahead >= 0) { int t = G.points[ahead] >= 2 ? 2 : G.points[ahead]; G.points[ahead] -= t; G.points[p] += t; ev_add(EV_STEAL, p, ahead, t); }
      else ev_add(EV_STEAL, p, -1, 0);
    } else if (kind == EV_SWAP) { /* trade places with a random other seat */
      int q = cw_rng_int(&r, 0, NP - 2);
      if (q >= p) q++;
      int t = G.points[p]; G.points[p] = G.points[q]; G.points[q] = t;
      ev_add(EV_SWAP, p, q, 0);
    }
  }
  if (G.round < 16) for (int p = 0; p < NP; p++) G.hist[G.round][p] = G.points[p];
}

static void award_points(void) {
  const mg_def *d = cur_def();
  for (int p = 0; p < NP; p++) if (G.human[p]) SV->points_total += G.round_pts[p];
  SV->plays[G.cur]++;
  G.new_best = false;
  float best = 0;
  for (int p = 0; p < NP; p++) if (G.human[p]) best = d->higher_is_better ? fmaxf(best, G.c.score[p]) : best;
  if (best > SV->best[G.cur]) { SV->best[G.cur] = best; G.new_best = G.humans > 0; }
  G.unlock_toast = -1;
  for (int i = 0; i < MG_N; i++)
    if (!is_unlocked(i) && SV->points_total >= MG_LIST[i]->unlock_points) { SV->unlocked |= 1u << i; G.unlock_toast = i; }
  if (G.tournament) board_round();
  cw_shell_save_commit();
}

static void to_results(void) {
  rank_players();
  award_points();
#ifdef PP_AUTOPILOT
  if (batch_total)
    fprintf(stderr, "GAME id=%s skills=%d%d%d%d scores=%g,%g,%g,%g places=%d,%d,%d,%d ticks=%d\n", cur_def()->id, G.c.cpu_skill[0], G.c.cpu_skill[1], G.c.cpu_skill[2],
           G.c.cpu_skill[3], (double)G.c.score[0], (double)G.c.score[1], (double)G.c.score[2], (double)G.c.score[3], G.place[0], G.place[1], G.place[2], G.place[3], G.c.tick);
#endif
  bool human_won = false;
  for (int p = 0; p < NP; p++) if (G.human[p] && G.place[p] == 0) human_won = true;
  sfx(human_won ? SFX_WIN : SFX_LOSE, 1, 1);
  if (human_won) fx_confetti(cw_rect_make(0, -10, STAGE_W, 10), 60);
  set_state(S_RESULT);
}

/* ── tournament ─────────────────────────────────────────────────────── */

/* The games for the tournament: a shuffled bag of every unlocked game, drawn without repeats until the bag is
 * empty (a tournament longer than the bag starts a fresh bag, never repeating the game just played). */
static void new_order(void) {
  int pool[MG_MAX], n = 0;
  for (int i = 0; i < MG_N; i++) if (is_unlocked(i)) pool[n++] = i;
#ifdef PP_AUTOPILOT
  { /* balance runs play only games that need no scene; PP_GAME keeps every tournament on one game */
    const char *only = getenv("PP_GAME");
    int m = 0;
    for (int k = 0; k < n; k++) {
      const mg_def *d = MG_LIST[pool[k]];
      if (batch_total && d->scene) continue;
      if (only && strcmp(d->id, only)) continue;
      pool[m++] = pool[k];
    }
    if (m > 0) n = m;
  }
#endif
  cw_rng r;
  cw_rng_seed(&r, 4242u + (uint32_t)SV->tournaments * 977u + (uint32_t)G.serial);
  int bag[MG_MAX], left = 0, last = -1;
  for (int k = 0; k < G.rounds; k++) {
    if (left == 0) {
      for (int i = 0; i < n; i++) bag[i] = pool[i];
      left = n;
      for (int i = n - 1; i > 0; i--) { int j = cw_rng_int(&r, 0, i), t = bag[i]; bag[i] = bag[j]; bag[j] = t; }
      if (n > 1 && bag[left - 1] == last) { int t = bag[0]; bag[0] = bag[left - 1]; bag[left - 1] = t; } /* the next one drawn is bag[left - 1] */
    }
    G.order[k] = bag[--left];
    last = G.order[k];
  }
}

static void start_tournament(void) {
  static const int rounds[3] = { 4, 6, 8 };
  G.tournament = true;
  G.rounds = rounds[SV->rounds_opt % 3];
  G.round = 0;
  memset(G.points, 0, sizeof G.points);
  memset(G.pos, 0, sizeof G.pos);
  memset(G.from, 0, sizeof G.from);
  memset(G.mid, 0, sizeof G.mid);
  memset(G.round_wins, 0, sizeof G.round_wins);
  memset(G.ev_taken, 0, sizeof G.ev_taken);
  G.nev = 0;
  G.tiebreak = NULL;
  G.star_taken = 0;
  new_order();
  G.winner = -1;
  set_state(S_BOARD);
  sfx_music(1);
}

static void finish_tournament(void) {
  /* first by points; a tie goes to the seat with more round wins, then to a draw from the tournament's seeded rng
   * (never the seat number, which favoured seat 1); the screen says which one decided it */
  int top = G.points[0];
  for (int p = 1; p < NP; p++) if (G.points[p] > top) top = G.points[p];
  int cand[NP], nc = 0, most = -1;
  for (int p = 0; p < NP; p++) if (G.points[p] == top && G.round_wins[p] > most) most = G.round_wins[p];
  bool tied_pts = false;
  { int n = 0; for (int p = 0; p < NP; p++) n += G.points[p] == top; tied_pts = n > 1; }
  for (int p = 0; p < NP; p++) if (G.points[p] == top && G.round_wins[p] == most) cand[nc++] = p;
  G.tiebreak = NULL;
  int best = cand[0];
  if (nc > 1) {
    cw_rng r;
    cw_rng_seed(&r, 7177u + (uint32_t)SV->tournaments * 433u + (uint32_t)G.serial * 29u);
    best = cand[cw_rng_int(&r, 0, nc - 1)];
    G.tiebreak = "a draw";
  } else if (tied_pts) G.tiebreak = "most round wins";
  G.winner = best;
#ifdef PP_AUTOPILOT
  if (batch_total) {
    fprintf(stderr, "TOURN skills=%d%d%d%d rounds=%d winner=%d final=%d,%d,%d,%d after2=%d,%d,%d,%d wins=%d,%d,%d,%d tie=%s\n", G.c.cpu_skill[0], G.c.cpu_skill[1], G.c.cpu_skill[2],
           G.c.cpu_skill[3], G.rounds, best, G.points[0], G.points[1], G.points[2], G.points[3], G.hist[1][0], G.hist[1][1], G.hist[1][2], G.hist[1][3],
           G.round_wins[0], G.round_wins[1], G.round_wins[2], G.round_wins[3], G.tiebreak ? G.tiebreak : "no");
    batch_done++;
  }
#endif
  SV->tournaments++;
  if (G.human[best]) SV->wins[best]++;
  cw_shell_save_commit();
  fx_confetti(cw_rect_make(0, -10, STAGE_W, 10), 120);
  sfx(SFX_WIN, 1, 1);
  set_state(S_FINAL);
}

static void to_menu(void) {
  G.tournament = false;
  set_state(S_MENU);
  sfx_music(1);
}

static void after_results(void) {
  end_game_cleanup();
  if (G.tournament) {
    G.round++;
    set_state(S_BOARD);
    if (G.round >= G.rounds) { G.round = G.rounds; }
  } else {
    set_state(S_PICK);
  }
  sfx_music(1);
}

/* ── the shell's callbacks ──────────────────────────────────────────── */

static void on_start(int players, void *user) {
  (void)user;
  G.humans = AUTO ? 0 : players;
  for (int p = 0; p < NP; p++) G.human[p] = p < G.humans;
  G.cur = 0;
  G.serial = 0;
  G.hash = 0;
  G.menu_item = 0;
  G.pick_item = 0;
  load_scene(NULL);
  G.scene3d = false;
  apply_scope();
  fx_reset();
  set_state(S_MENU);
  sfx_music(1);
#ifdef PP_AUTOPILOT
  SV->unlocked = ~0u;
  SV->rounds_opt = 0;
  { const char *b = getenv("PP_BATCH"); batch_total = b ? atoi(b) : 0; }
  { const char *r = getenv("PP_ROUNDS"); if (r) SV->rounds_opt = atoi(r); }
  if (batch_total) for (int i = 0; i < MG_N; i++) if (MG_LIST[i]->scene) SV->unlocked &= ~(1u << i); /* 2D games only */
  start_tournament();
  { const char *only = getenv("PP_GAME"); /* a single game on repeat, for screenshots and probes */
    if (only) for (int i = 0; i < MG_N; i++) if (!strcmp(MG_LIST[i]->id, only)) for (int k = 0; k < G.rounds; k++) G.order[k] = i; }
#endif
}
static void on_quit(void *user) {
  (void)user;
  end_game_cleanup();
  G.tournament = false;
  sfx_music_stop();
  if (scoped) cw_quality_scope_pop();
  scoped = false;
}
/* a pad joined or left in the middle of a session (the shell's join screen, or a pad that went away) */
/* a 3D game's hit-stop holds the scene paused, which stops fixed_update: the countdown runs from a tree hook */
static void hold_hook(cw_tree *t, double dt, void *user) {
  (void)dt, (void)user;
  if (G.hold <= 0 || cw_shell_screen_now() != CW_SHELL_PLAY || !cw_tree_paused(t)) return; /* the shell's own pause is not ours */
  if (G.state != S_PLAY || !G.scene3d) { G.hold = 0; cw_tree_set_paused(t, false); return; } /* the game ended inside the freeze */
  G.hitstop_shown++;
  if (--G.hold == 0) cw_tree_set_paused(t, false);
}
static void on_players_changed(int players, void *user) {
  (void)players, (void)user;
  if (AUTO) return;
  bool was[NP];
  memcpy(was, G.human, sizeof was);
  G.humans = 0;
  for (int p = 0; p < NP; p++) {
    G.human[p] = cw_shell_seat_taken(p);
    G.c.human[p] = G.human[p];
    G.humans += G.human[p];
    if (G.state == S_INTRO && G.human[p] != was[p]) G.ready[p] = !G.human[p]; /* a newcomer readies up, a CPU is always ready */
    if (G.human[p] != was[p]) sfx(G.human[p] ? SFX_COIN : SFX_BACK, 1, G.human[p] ? 1.3f : 0.8f);
  }
  G.c.humans = G.humans;
}
static void on_sound(cw_shell_sound s, void *user) {
  (void)user;
  sfx(s == CW_SHELL_SND_MOVE ? SFX_BLIP : s == CW_SHELL_SND_BACK ? SFX_BACK : SFX_SELECT, 1, 1);
}
static void on_background(cw_shell_screen s, cw_vec2 canvas, void *user) {
  (void)user, (void)canvas;
  art_world(false);
  stage_begin();
  if (!G_hub_live) ui_backdrop(G.clock, cw_rgba(0.12f, 0.1f, 0.35f, 1), cw_rgba(0.45f, 0.18f, 0.5f, 1));
  else ui_vignette(1.4f);
  stage_end();
}
static void on_screen(cw_shell_screen from, cw_shell_screen to, void *user) {
  (void)from, (void)user;
  if (to == CW_SHELL_TITLE || to == CW_SHELL_JOIN) sfx_music(1);
}

/* ── per-tick play ──────────────────────────────────────────────────── */

static void read_inputs(void) {
  for (int p = 0; p < NP; p++) {
    mg_in *in = &G.c.in[p];
    mg_in old = *in;
    memset(in, 0, sizeof *in);
    if (G.human[p]) {
      in->x = cw_input_axis(p, a_left, a_right);
      in->y = cw_input_axis(p, a_up, a_down);
      in->a = cw_action_pressed(p, a_a);
      in->b = cw_action_pressed(p, a_b);
      in->c = cw_action_pressed(p, a_x);
      in->d = cw_action_pressed(p, a_y);
    } else if (G.state == S_PLAY) {
      cur_def()->think(&G.c, p, in);
    }
    in->pa = in->a && !old.a;
    in->pb = in->b && !old.b;
    in->pc = in->c && !old.c;
    in->pd = in->d && !old.d;
  }
}

static void tick_play(void) {
  /* a big hit freezes play for a few ticks, the clock too; before the inputs are read, so a press made during the
   * freeze is still a fresh press on the first tick after it */
  if (!G.scene3d && G.hold > 0) { G.hold--; G.hitstop_shown++; return; }
  read_inputs();
  /* a knocked-out person is never left idle: A cheers (confetti and a note), and some games give ghosts a job */
  for (int p = 0; p < NP; p++)
    if (G.c.eliminated[p] && G.human[p] && G.c.in[p].pa) {
      fx_burst(cw_v2(160 + 320 * (float)p, 700), 10, PCOL[p], 240, 0.8f, 6);
      fx_text(cw_v2(160 + 320 * (float)p, 640), "Go!", PCOL[p], 30);
      sfx(SFX_NOTE0 + (G.c.tick / 7 + p) % 8, 0.5f, 1);
    }
  const mg_def *d = cur_def();
  d->tick(&G.c);
  { int hs = fx_hitstop_take();
    if (hs > 0) { G.hold = hs; if (G.scene3d) cw_tree_set_paused(cw_scene_app_tree(), true); } }
  G.c.tick++;
  if (d->seconds > 0) {
    G.c.time_left -= 1.0f / 60.0f;
    if (G.c.time_left <= 0) { G.c.time_left = 0; G.c.done = true; }
  }
  for (int p = 0; p < NP; p++) G.hash = hash_u32(G.hash, (uint32_t)(int32_t)(G.c.score[p] * 100.0f));
  if (G.c.done) { sfx(SFX_GO, 1, 0.7f); set_state(S_FINISH); }
}

static void sim_step(void);
#ifdef PP_AUTOPILOT
/* Balance runs (tools/balance.mjs): PP_BATCH=<n> plays n whole tournaments of 2D mini-games per process as fast as
 * the CPU goes inside one tick, and prints one GAME line per mini-game and one TOURN line per tournament. PP_SKILLS
 * ("2,1,1,0") sets each seat's CPU skill, PP_ROUNDS (0..2) the length. Measurement only: nothing here ships in the
 * game build. */
static void batch_run(void) {
  int guard = 0;
  while (batch_total && batch_done < batch_total && guard++ < 2000000) sim_step();
  if (batch_total && batch_done >= batch_total) { fprintf(stderr, "BATCH DONE %d\n", batch_done); batch_total = 0; cw_quit(); }
}
#endif

#ifdef PP_AUTOPILOT
int pp_batch_active(void) { return batch_total != 0; }
#endif

static void fixed_update(void *user, double dt) {
  (void)user, (void)dt;
  if (!cw_shell_update()) return;
  /* join and leave in the middle of a session: a pad without a seat presses A and takes the CPU's seat at once (one
   * press; players_changed hands the seat over); Start on such a pad, or Y from a seated player between rounds (the
   * board, the menu, the picker), opens the shell's join screen over the game, which is also how a player leaves */
  if (!AUTO && G.state != S_COUNT && G.state != S_FINISH) {
    if (cw_shell_join_press()) return;
    bool ask = cw_shell_join_requested();
    if (G.state == S_MENU || G.state == S_PICK || G.state == S_BOARD || G.state == S_RESULT || G.state == S_FINAL)
      for (int p = 0; p < NP; p++) if (G.human[p] && cw_action_just_pressed(p, a_y)) ask = true;
    if (ask) { cw_shell_join_open(); return; }
  }
  sim_step();
#ifdef PP_AUTOPILOT
  batch_run();
#endif
}

static void sim_step(void) {
  G.st_ticks++;
  switch (G.state) {
  case S_MENU: {
    int n = menu_nav();
    const int items = 5;
    if (n == -2 || n == 2) { G.menu_item = (G.menu_item + (n > 0 ? 1 : items - 1)) % items; sfx(SFX_BLIP, 1, 1); }
    if ((n == -1 || n == 1) && G.menu_item == 3) { SV->rounds_opt = (SV->rounds_opt + (n > 0 ? 1 : 2)) % 3; sfx(SFX_BLIP, 1, 1); cw_shell_save_commit(); }
    if ((n == -1 || n == 1) && G.menu_item == 4) { SV->skill = (SV->skill + (n > 0 ? 1 : 2)) % 3; sfx(SFX_BLIP, 1, 1); cw_shell_save_commit(); }
    if (menu_ok()) {
      sfx(SFX_SELECT, 1, 1);
      if (G.menu_item == 0) start_tournament();
      else if (G.menu_item == 1) set_state(S_PICK);
      else if (G.menu_item == 2) set_state(S_TROPHY);
      else if (G.menu_item == 3) { SV->rounds_opt = (SV->rounds_opt + 1) % 3; cw_shell_save_commit(); }
      else { SV->skill = (SV->skill + 1) % 3; cw_shell_save_commit(); }
    }
    break;
  }
  case S_PICK: {
    int n = menu_nav();
    if (n == 1) G.pick_item = (G.pick_item + 1) % MG_N;
    if (n == -1) G.pick_item = (G.pick_item + MG_N - 1) % MG_N;
    if (n == 2) G.pick_item = (G.pick_item + 3) % MG_N;
    if (n == -2) G.pick_item = (G.pick_item + MG_N - 3) % MG_N;
    if (n) sfx(SFX_BLIP, 1, 1);
    if (menu_back()) { sfx(SFX_BACK, 1, 1); to_menu(); }
    else if (menu_ok()) {
      if (is_unlocked(G.pick_item)) { sfx(SFX_SELECT, 1, 1); G.tournament = false; begin_game(G.pick_item); }
      else sfx(SFX_BUZZ, 1, 1);
    }
    break;
  }
  case S_TROPHY:
    if (menu_back() || menu_ok()) { sfx(SFX_BACK, 1, 1); to_menu(); }
    break;
  case S_BOARD: {
    bool done_anim = G.st_ticks > 150;
    if (G.round >= G.rounds && G.tournament) {
      if (G.st_ticks > 200 && (menu_ok() || AUTO)) finish_tournament();
    } else if (done_anim && (menu_ok() || (AUTO && G.st_ticks > 220) || G.st_ticks > 60 * 14)) begin_game(G.order[G.round]);
    if (menu_back() && !AUTO) { sfx(SFX_BACK, 1, 1); end_game_cleanup(); to_menu(); }
    break;
  }
  case S_INTRO: {
    for (int p = 0; p < NP; p++) if (G.human[p] && !G.ready[p] && cw_action_just_pressed(p, a_a)) { G.ready[p] = true; sfx(SFX_COIN, 0.7f, 1.2f); }
    bool all = true;
    for (int p = 0; p < NP; p++) all = all && G.ready[p];
    if (AUTO && G.st_ticks > 20) all = true;
    if (all && G.st_ticks > 30) { set_state(S_COUNT); sfx_music(2); }
    break;
  }
  case S_COUNT:
    if (G.st_ticks % 60 == 1) sfx(G.st_ticks > 150 ? SFX_GO : SFX_TICK, 1, 1);
    if (G.st_ticks >= 180) { set_state(S_PLAY); }
    break;
  case S_PLAY: tick_play(); break;
  case S_FINISH:
    if (G.st_ticks > 100) to_results();
    break;
  case S_RESULT:
    if (G.st_ticks > 60 && (menu_ok() || (AUTO && G.st_ticks > 120) || G.st_ticks > 60 * 20)) after_results();
    break;
  case S_FINAL:
    if (G.st_ticks > 120 && (menu_ok() || AUTO)) { end_game_cleanup(); to_menu(); if (AUTO) start_tournament(); }
    break;
  }
}

/* ── drawing ────────────────────────────────────────────────────────── */

static cw_vec2 board_cell_pos(int i, int rows) {
  int r = i / BOARD_PER_ROW, c = i % BOARD_PER_ROW;
  if (r % 2 == 1) c = BOARD_PER_ROW - 1 - c;
  float gx = 130 + c * 113.0f;
  float gy = rows <= 3 ? 250 + r * 125.0f : 230 + r * 105.0f;
  return cw_v2(gx, gy);
}

static void draw_chip(int p, float x, float y, float w, const char *line, float alpha) {
  cw_color c = PCOL[p];
  bool out = G.c.eliminated[p];
  cw_rect r = cw_rect_make(x, y, w, 58);
  ui_panel(r, cw_rgba(0.08f, 0.08f, 0.16f, 0.82f * alpha), col_a(c, out ? 0.2f : 0.9f));
  ui_blob(cw_v2(x + 30, y + 31), 18, p, 0, 0, 1, out ? 3 : 0, !G.human[p]);
  ui_text(!G.human[p] ? "CPU" : PNAME[p], cw_v2(x + 58, y + 3), 22, col_a(c, out ? 0.4f : 1), CW_ALIGN_LEFT);
  if (line && line[0]) ui_text(line, cw_v2(x + 58, y + 27), 26, cw_rgba(1, 1, 1, out ? 0.4f : 1), CW_ALIGN_LEFT);
}

static void draw_strip(void) {
  const mg_def *d = cur_def();
  float cw_ = 270, gap = 14, x0 = (STAGE_W - (cw_ * NP + gap * (NP - 1))) * 0.5f;
  for (int p = 0; p < NP; p++) {
    char line[48] = "";
    if (d->score_text) d->score_text(&G.c, p, line, sizeof line);
    draw_chip(p, x0 + p * (cw_ + gap), 10, cw_, line, 1);
  }
  if (d->seconds > 0 && G.state == S_PLAY) {
    char t[16];
    snprintf(t, sizeof t, "%d", (int)ceilf(G.c.time_left));
    ui_pill(cw_rect_make(STAGE_W * 0.5f - 44, 76, 88, 40), cw_rgba(0, 0, 0, 0.5f));
    ui_text(t, cw_v2(STAGE_W * 0.5f, 78), 34, G.c.time_left < 6 ? cw_rgba(1, 0.4f, 0.35f, 1) : cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
}

static void draw_menu(void) {
  ui_vignette(1.6f);
  ui_title("PARTY PACK", cw_v2(STAGE_W / 2, 24), 96, G.clock);
  static const char *labels[5] = { "Party Tournament", "Free Play", "Trophy Room", "Rounds", "CPU Skill" };
  static const char *skills[3] = { "Easy", "Medium", "Hard" };
  for (int i = 0; i < 5; i++) {
    bool sel = i == G.menu_item;
    cw_rect r = cw_rect_make(STAGE_W / 2 - 250, 330 + i * 64, 500, 54);
    ui_panel(r, sel ? cw_rgba(1, 0.85f, 0.3f, 0.95f) : cw_rgba(0.08f, 0.07f, 0.2f, 0.8f), sel ? cw_rgba(1, 1, 1, 0.9f) : cw_rgba(1, 1, 1, 0.15f));
    cw_color tc = sel ? cw_rgba(0.15f, 0.1f, 0.25f, 1) : cw_rgba(1, 1, 1, 1);
    ui_text(labels[i], cw_v2(r.x + 28, r.y + 6), 34, tc, CW_ALIGN_LEFT);
    char v[24] = "";
    if (i == 3) snprintf(v, sizeof v, "< %d >", 4 + 2 * (SV->rounds_opt % 3));
    if (i == 4) snprintf(v, sizeof v, "< %s >", skills[SV->skill % 3]);
    if (v[0]) ui_text(v, cw_v2(r.x + r.w - 28, r.y + 6), 34, tc, CW_ALIGN_RIGHT);
  }
  char s[96];
  snprintf(s, sizeof s, "%d player%s + %d CPU     new pad: A joins     Y: join or leave", G.humans, G.humans == 1 ? "" : "s", NP - G.humans);
  ui_text(s, cw_v2(STAGE_W / 2, 668), 26, cw_rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER);
}

static void draw_pick(void) {
  ui_grad(cw_rect_make(0, 0, STAGE_W, STAGE_H), cw_rgba(0.04f, 0.05f, 0.16f, 0.82f), cw_rgba(0.08f, 0.04f, 0.18f, 0.9f));
  ui_text("FREE PLAY", cw_v2(STAGE_W / 2, 24), 64, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  for (int i = 0; i < MG_N; i++) {
    int cx = i % 3, cy = i / 3;
    cw_rect r = cw_rect_make(110 + cx * 370, 120 + cy * 170, 350, 150);
    bool sel = i == G.pick_item, lock = !is_unlocked(i);
    const mg_def *d = MG_LIST[i];
    ui_panel(r, sel ? cw_rgba(1, 0.85f, 0.3f, 0.95f) : cw_rgba(0.06f, 0.08f, 0.2f, 0.85f), sel ? cw_rgba(1, 1, 1, 1) : cw_rgba(1, 1, 1, 0.2f));
    cw_color tc = sel ? cw_rgba(0.12f, 0.08f, 0.2f, 1) : cw_rgba(1, 1, 1, 1);
    ui_text(d->name, cw_v2(r.x + 18, r.y + 10), 36, tc, CW_ALIGN_LEFT);
    ui_text_wrap(lock ? "Locked" : d->tagline, cw_rect_make(r.x + 18, r.y + 58, r.w - 36, 60), 24, tc, CW_ALIGN_LEFT);
    char s[48];
    if (lock) snprintf(s, sizeof s, "needs %d party points", d->unlock_points);
    else snprintf(s, sizeof s, "%s   best %g", d->kind, SV->best[i]);
    ui_text(s, cw_v2(r.x + 18, r.y + 112), 22, sel ? tc : cw_rgba(0.8f, 0.85f, 1, 0.9f), CW_ALIGN_LEFT);
    if (lock) cw_r2d_round_rect(r, 16, 8, cw_rgba(0, 0, 0, 0.45f));
  }
  ui_text("B: back", cw_v2(60, 670), 26, cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_LEFT);
}

static void draw_trophy(void) {
  ui_vignette(1.5f);
  ui_text("TROPHY ROOM", cw_v2(STAGE_W / 2, 24), 64, cw_rgba(1, 0.9f, 0.4f, 1), CW_ALIGN_CENTER);
  char s[200];
  snprintf(s, sizeof s, "party points %d   tournaments %d", SV->points_total, SV->tournaments);
  ui_text(s, cw_v2(STAGE_W / 2, 120), 34, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  for (int p = 0; p < NP; p++) {
    ui_blob(cw_v2(240 + p * 270, 250), 44, p, 0, 0, 1, SV->wins[p] ? 1 : 0, false);
    snprintf(s, sizeof s, "%d win%s", SV->wins[p], SV->wins[p] == 1 ? "" : "s");
    ui_text(s, cw_v2(240 + p * 270, 310), 30, PCOL[p], CW_ALIGN_CENTER);
  }
  int n = 0;
  for (int i = 0; i < MG_N; i++) {
    snprintf(s, sizeof s, "%s: %s  played %d  best %g", MG_LIST[i]->name, is_unlocked(i) ? "open" : "locked", SV->plays[i], SV->best[i]);
    ui_text(s, cw_v2(180 + (n % 2) * 480, 380 + (n / 2) * 42), 26, is_unlocked(i) ? cw_rgba(1, 1, 1, 1) : cw_rgba(1, 1, 1, 0.5f), CW_ALIGN_LEFT);
    n++;
  }
}

static void draw_board(void) {
  ui_vignette(1.2f);
  int N = board_cells();
  char s[96];
  if (G.round >= G.rounds) snprintf(s, sizeof s, "FINAL STANDINGS");
  else snprintf(s, sizeof s, "ROUND %d OF %d", G.round + 1, G.rounds);
  ui_text(s, cw_v2(STAGE_W / 2, 18), 58, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  for (int i = 0; i <= N; i++) { /* labels on the special tiles */
    int kind = i == N ? EV_NONE : cell_used(i) ? EV_NONE : cell_kind(i);
    if (i != N && kind == EV_NONE) continue;
    cw_vec2 p = hub_cell_screen(i);
    if (i == N) ui_text("END", cw_v2(p.x, p.y - 30), 26, cw_rgba(1, 0.95f, 0.5f, 1), CW_ALIGN_CENTER);
    else if (kind == EV_STAR) ui_text("+2", cw_v2(p.x, p.y - 30), 24, cw_rgba(1, 0.7f, 0.95f, 1), CW_ALIGN_CENTER);
    else if (kind == EV_STEAL) ui_text("TAKE", cw_v2(p.x, p.y - 28), 20, cw_rgba(1, 0.65f, 0.3f, 1), CW_ALIGN_CENTER);
    else if (kind == EV_SWAP) ui_text("SWAP", cw_v2(p.x, p.y - 28), 20, cw_rgba(0.4f, 0.95f, 0.9f, 1), CW_ALIGN_CENTER);
  }
  for (int p = 0; p < NP; p++) {
    snprintf(s, sizeof s, "%d", G.st_ticks < 110 ? G.mid[p] : G.points[p]);
    draw_chip(p, 70 + p * 285, 640, 270, s, 1);
  }
  if (G.tournament && G.round > 0 && G.st_ticks > 110) { /* what the board did, in words */
    for (int i = 0; i < G.nev; i++) {
      char e[96];
      int p = G.ev[i].p, q = G.ev[i].q;
      if (G.ev[i].kind == EV_STAR) snprintf(e, sizeof e, "%s lands on a star: +2", PNAME[p]);
      else if (G.ev[i].kind == EV_STEAL) {
        if (q >= 0) snprintf(e, sizeof e, "%s takes %d from %s", PNAME[p], G.ev[i].amt, PNAME[q]);
        else snprintf(e, sizeof e, "%s lands on a take space: nobody is ahead", PNAME[p]);
      } else if (G.ev[i].kind == EV_SWAP) snprintf(e, sizeof e, "%s swaps places with %s", PNAME[p], PNAME[q]);
      else snprintf(e, sizeof e, "%s: underdog point, +1", PNAME[p]);
      cw_r2d_rect(cw_rect_make(STAGE_W / 2 - 250, 140 + (float)i * 30 - 2, 500, 28), cw_rgba(0, 0, 0, 0.35f));
      ui_text(e, cw_v2(STAGE_W / 2, 140 + (float)i * 30), 22, PCOL[p], CW_ALIGN_CENTER);
    }
  }
  if (G.round < G.rounds) {
    snprintf(s, sizeof s, !G.tournament || !is_double_round(G.round) ? "Next: %s" : G.round == G.rounds - 1 ? "Next: %s   (triple points)" : "Next: %s   (double points)", MG_LIST[G.order[G.round]]->name);
    ui_text(s, cw_v2(STAGE_W / 2, 96), 40, cw_rgba(1, 0.92f, 0.5f, 1), CW_ALIGN_CENTER);
    if (G.st_ticks > 150) ui_text("press A", cw_v2(STAGE_W / 2, 580), 28, cw_rgba(1, 1, 1, 0.5f + 0.5f * sinf(G.clock * 6)), CW_ALIGN_CENTER);
  } else {
    ui_text("press A", cw_v2(STAGE_W / 2, 580), 28, cw_rgba(1, 1, 1, 0.5f + 0.5f * sinf(G.clock * 6)), CW_ALIGN_CENTER);
  }
}

static void draw_intro(void) {
  const mg_def *d = cur_def();
  cw_r2d_rect(cw_rect_make(0, 0, STAGE_W, STAGE_H), cw_rgba(0.02f, 0.02f, 0.08f, 0.6f));
  float k = ease_back((float)G.st_ticks / 25.0f);
  cw_rect r = cw_rect_make(240, 130 + (1 - k) * 60, 800, 440);
  ui_panel(r, cw_rgba(0.1f, 0.1f, 0.25f, 0.95f), cw_rgba(1, 0.85f, 0.3f, 1));
  ui_text(d->name, cw_v2(STAGE_W / 2, r.y + 14), 72, cw_rgba(1, 0.92f, 0.5f, 1), CW_ALIGN_CENTER);
  ui_text(d->tagline, cw_v2(STAGE_W / 2, r.y + 100), 30, cw_rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER);
  /* the controls as pictures first (one glyph per letter of keys: A B X Y, or S for the stick), then the lines */
  { int nk = d->keys ? (int)strlen(d->keys) : 0;
    float gx = STAGE_W / 2 - (float)(nk - 1) * 40;
    for (int i = 0; i < nk; i++) {
      cw_vec2 gp = cw_v2(gx + (float)i * 80, r.y + 172);
      if (d->keys[i] == 'S') ui_stick_glyph(gp, 26); else ui_button_glyph(gp, 24, d->keys[i]);
    } }
  for (int i = 0; i < 3 && d->how[i]; i++) ui_text(d->how[i], cw_v2(r.x + 60, r.y + 210 + i * 40), 28, cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
  for (int p = 0; p < NP; p++) {
    cw_vec2 c = cw_v2(r.x + 130 + p * 180, r.y + 350);
    ui_blob(c, 30, p, 0, 0, G.ready[p] ? 1 + 0.08f * sinf(G.clock * 8) : 1, G.ready[p] ? 1 : 0, !G.human[p]);
    ui_text(G.ready[p] ? "READY" : "press A", cw_v2(c.x, c.y + 42), 22, G.ready[p] ? cw_rgba(0.6f, 1, 0.6f, 1) : cw_rgba(1, 1, 1, 0.6f), CW_ALIGN_CENTER);
  }
}

static void draw_count(void) {
  int n = 3 - G.st_ticks / 60;
  float f = (float)(G.st_ticks % 60) / 60.0f;
  char s[16];
  if (n > 0) snprintf(s, sizeof s, "%d", n); else snprintf(s, sizeof s, "GO!");
  float sz = (n > 0 ? 190 : 150) * (1 + 0.5f * (1 - ease_out(f * 3)));
  ui_text(s, cw_v2(STAGE_W / 2, STAGE_H / 2 - sz * 0.6f), sz, n > 0 ? cw_rgba(1, 1, 1, 1 - f * 0.5f) : cw_rgba(0.5f, 1, 0.5f, 1), CW_ALIGN_CENTER);
}

static void draw_results(void) {
  cw_r2d_rect(cw_rect_make(0, 0, STAGE_W, STAGE_H), cw_rgba(0.02f, 0.02f, 0.08f, 0.72f));
  ui_text("RESULTS", cw_v2(STAGE_W / 2, 60), 70, cw_rgba(1, 0.92f, 0.5f, 1), CW_ALIGN_CENTER);
  int order[NP], k = 0;
  for (int pl = 0; pl < NP; pl++) for (int p = 0; p < NP; p++) if (G.place[p] == pl) order[k++] = p;
  static const char *place_name[4] = { "1st", "2nd", "3rd", "4th" };
  for (int i = 0; i < NP; i++) {
    int p = order[i];
    float slide = ease_out(((float)G.st_ticks - i * 10) / 30.0f);
    float x = flerp(-500, 330, slide), y = 160 + i * 100;
    cw_rect r = cw_rect_make(x, y, 620, 84);
    ui_panel(r, cw_rgba(0.1f, 0.1f, 0.25f, 0.95f), col_a(PCOL[p], 1));
    ui_text(place_name[G.place[p]], cw_v2(x + 20, y + 16), 44, G.place[p] == 0 ? cw_rgba(1, 0.9f, 0.3f, 1) : cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
    ui_blob(cw_v2(x + 190, y + 42), 28, p, 0, 0, 1, G.place[p] == 0 ? 1 : G.place[p] == 3 ? 3 : 0, !G.human[p]);
    char s[48] = "";
    if (cur_def()->score_text) cur_def()->score_text(&G.c, p, s, sizeof s);
    ui_text(!G.human[p] ? "CPU" : PNAME[p], cw_v2(x + 240, y + 8), 26, PCOL[p], CW_ALIGN_LEFT);
    ui_text(s, cw_v2(x + 240, y + 40), 28, cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
    snprintf(s, sizeof s, "+%d", G.round_pts[p]);
    ui_text(s, cw_v2(x + 590, y + 16), 46, cw_rgba(0.6f, 1, 0.6f, 1), CW_ALIGN_RIGHT);
    if (G.underdog[p] && G.tournament) ui_text("+1 underdog", cw_v2(x + 590, y + 62), 20, cw_rgba(1, 0.85f, 0.4f, 1), CW_ALIGN_RIGHT);
  }
  if (G.new_best) ui_text("New best!", cw_v2(STAGE_W / 2, 580), 40, cw_rgba(1, 0.8f, 0.3f, 1), CW_ALIGN_CENTER);
  if (G.unlock_toast >= 0) {
    char s[96];
    snprintf(s, sizeof s, "Unlocked: %s", MG_LIST[G.unlock_toast]->name);
    ui_text(s, cw_v2(STAGE_W / 2, 625), 40, cw_rgba(0.6f, 1, 0.9f, 1), CW_ALIGN_CENTER);
  }
  if (G.st_ticks > 60) ui_text("press A", cw_v2(STAGE_W / 2, 675), 26, cw_rgba(1, 1, 1, 0.5f + 0.5f * sinf(G.clock * 6)), CW_ALIGN_CENTER);
}

static void draw_final(void) {
  ui_vignette(1.3f);
  ui_title("TOURNAMENT OVER", cw_v2(STAGE_W / 2, 20), 64, G.clock);
  int order[NP] = { 0, 1, 2, 3 };
  for (int i = 1; i < NP; i++)
    for (int j = i; j > 0; j--) {
      int a = order[j - 1], b = order[j];
      if (G.points[b] > G.points[a] || (G.points[b] == G.points[a] && G.round_wins[b] > G.round_wins[a])) { order[j] = a; order[j - 1] = b; } else break;
    }
  for (int i = 0; i < NP; i++) {
    int p = order[i];
    cw_vec2 sp = hub_podium_screen(i);
    char s[48];
    snprintf(s, sizeof s, "%d pts", G.points[p]);
    float k = ease_out(((float)G.st_ticks - 30) / 30.0f);
    ui_text(!G.human[p] ? "CPU" : PNAME[p], cw_v2(sp.x, sp.y - 62 * k), 28, col_a(PCOL[p], k), CW_ALIGN_CENTER);
    ui_text(s, cw_v2(sp.x, sp.y - 32 * k), 32, cw_rgba(1, 1, 1, k), CW_ALIGN_CENTER);
  }
  if (G.winner >= 0) {
    char s[64];
    snprintf(s, sizeof s, "%s wins!", !G.human[G.winner] ? "CPU" : PNAME[G.winner]);
    ui_text(s, cw_v2(STAGE_W / 2, 118), 56, PCOL[G.winner], CW_ALIGN_CENTER);
  }
  if (G.tiebreak) {
    char tb[64];
    snprintf(tb, sizeof tb, "Tied on points: decided by %s", G.tiebreak);
    ui_text(tb, cw_v2(STAGE_W / 2, 182), 26, cw_rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER);
  }
  ui_text("press A", cw_v2(STAGE_W / 2, 672), 26, cw_rgba(1, 1, 1, 0.5f + 0.5f * sinf(G.clock * 6)), CW_ALIGN_CENTER);
}

/* the 3D hub follows the game's state: stage, board or podium */
static void hub_sync(float dt) {
  static int last_mode = -1, last_cells = -1;
  static bool last_live;
  if (!G_hub_live) { last_live = false; return; }
  if (!last_live) { last_mode = -1; last_cells = -1; last_live = true; }
  int mode = G.state == S_BOARD ? 1 : G.state == S_FINAL ? 2 : 0;
  int N = board_cells();
  if (N != last_cells) { hub_cells(N); last_cells = N; last_mode = -1; }
  if (mode != last_mode) { hub_mode(mode); last_mode = mode; }
  if (mode == 1) {
    for (int i = 0; i <= N; i++) hub_cell(i, i == N ? 2 : (cell_kind(i) == EV_STAR && !cell_used(i)) ? 1 : 0);
    float t = ease_out((float)G.st_ticks / 100.0f), t2 = ease_out(((float)G.st_ticks - 110.0f) / 50.0f);
    for (int p = 0; p < NP; p++) {
      /* first the round's points, then what the cells did (stars, takes, swaps, the underdog point) */
      float cell = !(G.tournament && G.round > 0) ? (float)G.points[p]
                 : G.st_ticks < 110 ? flerp((float)G.from[p], (float)G.mid[p], t) : flerp((float)G.mid[p], (float)G.points[p], t2);
      if (cell > (float)N) cell = (float)N;
      hub_token(p, cell, 0);
    }
  } else if (mode == 2) {
    int order[NP] = { 0, 1, 2, 3 };
    for (int i = 1; i < NP; i++)
      for (int j = i; j > 0; j--) {
        int a = order[j - 1], b = order[j];
        if (G.points[b] > G.points[a] || (G.points[b] == G.points[a] && G.round_wins[b] > G.round_wins[a])) { order[j] = a; order[j - 1] = b; } else break;
      }
    for (int i = 0; i < NP; i++) hub_podium_slot(order[i], i, ease_out(((float)G.st_ticks - 8.0f * (float)(3 - i)) / 40.0f));
  }
  hub_update(dt);
}

static void render(void *user, double alpha) {
  (void)user;
  static double last_t; double now_t = cw_time(); float dt_ = last_t > 0 ? fminf((float)(now_t - last_t), 0.1f) : 0.016f; last_t = now_t; G.clock += dt_;
  cw_shell_screen sc = cw_shell_screen_now();
  hub_sync(sc == CW_SHELL_PAUSE ? 0 : dt_);
  if (sc == CW_SHELL_PLAY || sc == CW_SHELL_PAUSE) {
    bool in_game = G.state >= S_INTRO && G.state <= S_RESULT;
    bool over3d = in_game && G.scene3d;
    cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = !(over3d || G_hub_live), .clear_color = cw_rgba(0.05f, 0.05f, 0.1f, 1), .ambient = art_ambient });
    art_world(false);
    if (G.state == S_RESULT || G.state == S_FINISH || G.state == S_PLAY || G.state == S_COUNT || G.state == S_INTRO) fx_update(sc == CW_SHELL_PLAY ? dt_ : 0);
    stage_begin();
    if (in_game) {
      const mg_def *d = cur_def();
      if (!G.scene3d) art_world(true);   /* the 2D world is lit: sprites with normal maps, real 2D lights */
      d->draw(&G.c, (float)alpha, G.clock);
      art_world(false);                  /* the HUD, particles and banners are not */
      fx_draw();
      draw_strip();
      if (G.state == S_INTRO) draw_intro();
      if (G.state == S_COUNT) draw_count();
      if (G.state == S_FINISH) ui_text("FINISH!", cw_v2(STAGE_W / 2, 280), 130 * (1 + 0.3f * ease_out(1 - G.st_ticks / 20.0f)), cw_rgba(1, 0.9f, 0.3f, 1), CW_ALIGN_CENTER);
      if (G.state == S_RESULT) draw_results();
    } else {
      switch (G.state) {
      case S_MENU: draw_menu(); break;
      case S_PICK: draw_pick(); break;
      case S_TROPHY: draw_trophy(); break;
      case S_BOARD: draw_board(); break;
      case S_FINAL: draw_final(); break;
      }
      fx_draw();
    }
    stage_end();
    cw_r2d_end();
  }
  cw_shell_draw();
}

/* ── setup ──────────────────────────────────────────────────────────── */

static void init(void *user) {
  (void)user;
  cw_project_load_input();
  cw_r2d_layer_set(0, &(cw_r2d_layer_desc){ .lit = true });
  a_left = cw_action_find("move_left"); a_right = cw_action_find("move_right");
  a_up = cw_action_find("move_up"); a_down = cw_action_find("move_down");
  a_a = cw_action_find("btn_a"); a_b = cw_action_find("btn_b"); a_x = cw_action_find("btn_x"); a_y = cw_action_find("btn_y");
  cw_shell_init(&(cw_shell_desc){
    .title = "Party Pack",
    .subtitle = "one screen, up to four players",
    .max_players = NP,
    .game_save_size = sizeof(save_data),
    .game_save_version = 1,
    .start = on_start,
    .quit = on_quit,
    .sound = on_sound,
    .screen_changed = on_screen,
    .draw_background = on_background,
    .players_changed = on_players_changed,
  });
  u_up = cw_action_find("ui_up"); u_down = cw_action_find("ui_down");
  u_left = cw_action_find("ui_left"); u_right = cw_action_find("ui_right");
  u_ok = cw_action_find("ui_accept"); u_back = cw_action_find("ui_back");
  cw_tree_add_hook(cw_scene_app_tree(), 0, 900, hold_hook, NULL);
  bus_music = cw_shell_music_bus();
  bus_sfx = cw_shell_sfx_bus();
  register_games();
  SV = cw_shell_game_save();
  for (int i = 0; i < MG_N; i++) if (MG_LIST[i]->unlock_points == 0) SV->unlocked |= 1u << i;
  sfx_init();
  load_scene(NULL); /* the 3D hub behind the title and join screens */
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.round", &G.round);
  cw_debug_i32("game.mg", &G.cur);
  cw_debug_i32("game.tick", &G.c.tick);
  cw_debug_field("game.score", G.c.score, CW_DBG_F32, NP);
  cw_debug_field("game.points", G.points, CW_DBG_I32, NP);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_i32("game.points_total", &SV->points_total);
  cw_debug_u32("game.unlocked", &SV->unlocked);
  cw_debug_i32("game.winner", &G.winner);
  cw_debug_i32("game.humans", &G.humans);
  cw_debug_i32("game.events", &G.nev);
  cw_debug_field("game.wins", G.round_wins, CW_DBG_I32, NP);
  cw_debug_i32("game.hitstops", &G.hitstop_shown);
  cw_debug_field("probe.load_ms", &G.load_ms, CW_DBG_F32, 1);
  cw_debug_field("probe.start_ms", &G.start_ms, CW_DBG_F32, 1);
#ifdef PP_AUTOPILOT
  cw_shell_start(1); /* straight into a tournament of four CPUs */
#endif
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .width = 1280, .height = 720, .debug = true, .init = init, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("scenes/hub.cwscene", &app);
}
