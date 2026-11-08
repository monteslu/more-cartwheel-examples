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
  float best[MG_MAX - 1];  /* best score per mini-game */
  int32_t rumble_off;      /* the Rumble setting, 1 off: the slot of best[MG_MAX - 1], which no game used (fewer than
                            * MG_MAX games), so an older save reads 0, on, and keeps its size and everything in it */
  uint32_t unlocked;       /* bit per mini-game */
  int32_t rounds_opt;      /* 0..2 -> 4, 6, 8 rounds */
  int32_t skill;           /* CPU skill 0..2 */
} save_data;
_Static_assert(sizeof(save_data) == 4 * (2 + NP + MG_MAX + MG_MAX + 3), "the save keeps its size: a new layout would reset it");
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
  int32_t ready_wait;          /* ticks since the first person readied on the card; the rest are counted in at READY_WAIT */
  int32_t ready_left;          /* whole seconds the card still waits (debug), 0 when nobody has readied */
  int32_t ready_forever;       /* test switch (sw ready_forever 1): the card waits for every seat, as it once did */
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

static cw_action a_left, a_right, a_up, a_down, a_south, a_east, a_west, a_north, u_up, u_down, u_left, u_right, u_ok, u_back, u_start, u_pause;
static bool just_resumed; /* the shell's join or pause screen closed this tick: the Start that closed it is not a press here */
static cw_bus bus_music, bus_sfx;
cw_bus pp_music_bus(void) { return bus_music; }
cw_bus pp_sfx_bus(void) { return bus_sfx; }

static const mg_def *cur_def(void) { return MG_LIST[G.cur]; }
static bool is_unlocked(int i) { return (SV->unlocked >> i) & 1; }

static int dbg_seen, dbg_mg_seen; /* debug fields game.seen, game.mg_seen: every state and every mini-game this run has been in, one bit each */
/* the instructions card waits this long for the other people once one has readied, then the game starts with
 * everyone: a pad that joined and was put down never holds the others on the card (it plays standing still) */
enum { READY_WAIT = 60 * 10 };
static void set_state(int s) { G.state = s; G.st_ticks = 0; dbg_seen |= 1 << s; }

/* Screens whose 3D scene is not on screen (the menus, the board, the 2D mini-games draw over a cleared window) ask
 * the renderer for the least: a quality scope lowers resolution, shadows, ambient occlusion, anti-aliasing and
 * effects for the hub scene they leave unseen, and ends when a 3D mini-game's scene is shown. */
static bool scoped;
static void apply_scope(void) {
  bool want = !G.scene3d;
  if (want == scoped) return;
  if (want) {
    static const int8_t caps[CW_GFX_ENGINE_COUNT] = { [CW_GFX_RESOLUTION] = CW_GFX_RES_50, [CW_GFX_SSAO] = CW_GFX_SSAO_OFF,
                                                      [CW_GFX_SHADOWS] = CW_GFX_SHADOWS_MINIMAL, [CW_GFX_AA] = CW_GFX_AA_OFF,
                                                      [CW_GFX_EFFECTS] = CW_QUALITY_VERY_LOW }; /* every setting at its lowest */
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
static bool menu_start(int p) { return !just_resumed && cw_action_just_pressed(p, u_start); }
static bool menu_ok(void) {
  if (any_human_pressed(u_ok) || any_human_pressed(a_south)) return true;
  for (int p = 0; p < NP; p++) if (G.human[p] && menu_start(p)) return true;
  return false;
}
static bool menu_back(void) { return any_human_pressed(u_back) || any_human_pressed(a_east); }

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
  dbg_mg_seen |= 1 << idx;
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
  G.ready_wait = 0, G.ready_left = 0;
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
#ifdef PP_AUTOPILOT
  { const char *h = getenv("PP_HUMANS"); /* the autopilot with people in it: seats 0..n-1 read their pads (gates that play a mini-game by hand) */
    if (h) G.humans = atoi(h) < 0 ? 0 : atoi(h) > players ? players : atoi(h); }
#endif
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
  if (!getenv("PP_KEYART")) start_tournament(); /* development (key art): PP_KEYART stays on the hub, the four characters on the table */
  { const char *only = getenv("PP_GAME"); /* a single game on repeat, for screenshots and probes */
    if (only) for (int i = 0; i < MG_N; i++) if (!strcmp(MG_LIST[i]->id, only)) for (int k = 0; k < G.rounds; k++) G.order[k] = i; }
#endif
}
#ifdef PP_AUTOPILOT
/* debug channel: "game <id>" keeps every round left on one game, what PP_GAME does where there is no environment (a cart) */
static void cmd_game(int argc, const char **argv, cw_reply *r) {
  for (int i = 0; argc > 1 && i < MG_N; i++)
    if (!strcmp(MG_LIST[i]->id, argv[1])) {
      for (int k = G.round; k < G.rounds; k++) G.order[k] = i;
      cw_reply_printf(r, "{\"game\":\"%s\"}", argv[1]);
      return;
    }
  cw_reply_error(r, "usage: game <id>");
}
#endif
/* debug channel: test switches */
static void cmd_sw(int argc, const char **argv, cw_reply *r) {
  if (argc < 3 || strcmp(argv[1], "ready_forever")) { cw_reply_error(r, "usage: sw ready_forever <0|1>"); return; }
  G.ready_forever = atoi(argv[2]) != 0;
  cw_reply_printf(r, "{\"ready_forever\":%d}", G.ready_forever);
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
  (void)user;
  if (to == CW_SHELL_TITLE || to == CW_SHELL_JOIN) sfx_music(1);
  just_resumed = to == CW_SHELL_PLAY && (from == CW_SHELL_JOIN || from == CW_SHELL_PAUSE);
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
      in->south = cw_action_pressed(p, a_south);
      in->east = cw_action_pressed(p, a_east);
      in->west = cw_action_pressed(p, a_west);
      in->north = cw_action_pressed(p, a_north);
    } else if (G.state == S_PLAY) {
      cur_def()->think(&G.c, p, in);
    }
    in->psouth = in->south && !old.south;
    in->peast = in->east && !old.east;
    in->pwest = in->west && !old.west;
    in->pnorth = in->north && !old.north;
  }
}

static void tick_play(void) {
  /* a big hit freezes play for a few ticks, the clock too; before the inputs are read, so a press made during the
   * freeze is still a fresh press on the first tick after it */
  if (!G.scene3d && G.hold > 0) { G.hold--; G.hitstop_shown++; return; }
  read_inputs();
  /* a knocked-out person is never left idle: south cheers (confetti and a note), and some games give ghosts a job */
  for (int p = 0; p < NP; p++)
    if (G.c.eliminated[p] && G.human[p] && G.c.in[p].psouth) {
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
static int trophy_scroll; /* the trophy room's list, scrolled by up / down when it does not fit (the screen only, no game state) */
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
  /* the hub, the picker, the boards and the readying are panels, not play: Start confirms there (menu_ok), so the shell does
   * not see that press and does not pause; every other tick it runs as usual, and a pad that goes away frees its seat. The
   * countdown, the game and its finish pause as usual */
  bool panel = cw_shell_screen_now() == CW_SHELL_PLAY && G.state != S_COUNT && G.state != S_PLAY && G.state != S_FINISH;
  bool pause_press = false;
  for (int p = 0; panel && p < cw_shell_players(); p++) pause_press |= cw_action_just_pressed(p, u_pause);
  if (!pause_press && !cw_shell_update()) { pp_rumble_stop(); return; } /* the menus and pause: the motors stop */
  pp_rumble_tick();
  if (G.state != S_PLAY && G.state != S_FINISH) pp_rumble_stop(); /* only play (and its finish) rumbles */
  /* join and leave in the middle of a session: a pad without a seat presses south and takes the CPU's seat at once (one
   * press; players_changed hands the seat over); Start on such a pad, or north from a seated player between rounds (the
   * board, the menu, the picker), opens the shell's join screen over the game, which is also how a player leaves */
  if (!AUTO && G.state != S_COUNT && G.state != S_FINISH) {
    if (cw_shell_join_press()) return;
    bool ask = cw_shell_join_requested();
    if (G.state == S_MENU || G.state == S_PICK || G.state == S_BOARD || G.state == S_RESULT || G.state == S_FINAL)
      for (int p = 0; p < NP; p++) if (G.human[p] && cw_action_just_pressed(p, a_north)) ask = true;
    if (ask) { just_resumed = false; cw_shell_join_open(); return; }
  }
  sim_step();
  just_resumed = false;
#ifdef PP_AUTOPILOT
  batch_run();
#endif
}

/* the menu's Rumble item: on or off, saved; turned on, the pads that can rumble give a short taste of it */
static void toggle_rumble(void) {
  SV->rumble_off = !SV->rumble_off;
  pp_rumble_off = SV->rumble_off != 0;
  cw_shell_save_commit();
  sfx(SFX_BLIP, 1, 1);
  for (int p = 0; p < NP; p++)
    if (G.human[p]) { if (pp_rumble_off) cw_input_rumble_stop(p); else cw_input_rumble(p, 0.3f, 0.3f, 120); }
}

static void sim_step(void) {
  G.st_ticks++;
  switch (G.state) {
  case S_MENU: {
    int n = menu_nav();
    const int items = 6;
    if (n == -2 || n == 2) { G.menu_item = (G.menu_item + (n > 0 ? 1 : items - 1)) % items; sfx(SFX_BLIP, 1, 1); }
    if ((n == -1 || n == 1) && G.menu_item == 3) { SV->rounds_opt = (SV->rounds_opt + (n > 0 ? 1 : 2)) % 3; sfx(SFX_BLIP, 1, 1); cw_shell_save_commit(); }
    if ((n == -1 || n == 1) && G.menu_item == 4) { SV->skill = (SV->skill + (n > 0 ? 1 : 2)) % 3; sfx(SFX_BLIP, 1, 1); cw_shell_save_commit(); }
    if ((n == -1 || n == 1) && G.menu_item == 5) toggle_rumble();
    if (menu_back() && !AUTO) { sfx(SFX_BACK, 1, 1); on_quit(NULL); cw_shell_to_title(); break; } /* no pause menu on the panels */
    if (menu_ok()) {
      sfx(SFX_SELECT, 1, 1);
      if (G.menu_item == 0) start_tournament();
      else if (G.menu_item == 1) set_state(S_PICK);
      else if (G.menu_item == 2) set_state(S_TROPHY);
      else if (G.menu_item == 3) { SV->rounds_opt = (SV->rounds_opt + 1) % 3; cw_shell_save_commit(); }
      else if (G.menu_item == 4) { SV->skill = (SV->skill + 1) % 3; cw_shell_save_commit(); }
      else toggle_rumble();
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
  case S_TROPHY: {
    int n = menu_nav(); /* a list longer than the screen at a large text size scrolls */
    if (n == 2) trophy_scroll++;
    if (n == -2 && trophy_scroll > 0) trophy_scroll--;
    if (menu_back() || menu_ok()) { sfx(SFX_BACK, 1, 1); to_menu(); }
    break;
  }
  case S_BOARD: {
    bool done_anim = G.st_ticks > 150;
    if (G.round >= G.rounds && G.tournament) {
      if (G.st_ticks > 200 && (menu_ok() || AUTO)) finish_tournament();
    } else if (done_anim && (menu_ok() || (AUTO && G.st_ticks > 220) || G.st_ticks > 60 * 14)) begin_game(G.order[G.round]);
    if (menu_back() && !AUTO) { sfx(SFX_BACK, 1, 1); end_game_cleanup(); to_menu(); }
    break;
  }
  case S_INTRO: {
    for (int p = 0; p < NP; p++) if (G.human[p] && !G.ready[p] && (cw_action_just_pressed(p, a_south) || menu_start(p))) { G.ready[p] = true; sfx(SFX_COIN, 0.7f, 1.2f); }
    bool all = true, any = false;
    for (int p = 0; p < NP; p++) all = all && G.ready[p], any = any || (G.human[p] && G.ready[p]);
    if (any && !all && !G.ready_forever) {
      G.ready_left = (READY_WAIT - G.ready_wait + 59) / 60;
      if (++G.ready_wait >= READY_WAIT) { all = true; for (int p = 0; p < NP; p++) G.ready[p] = true; }
    } else G.ready_left = 0;
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
  cw_rect r = cw_rect_make(x, y, w, ui_chip_h());
  ui_panel(r, cw_rgba(0.08f, 0.08f, 0.16f, 0.82f * alpha), col_a(c, out ? 0.2f : 0.9f));
  ui_blob(cw_v2(x + 30, y + ui_chip_h() * (31.0f / 58.0f)), 18, p, 0, 0, 1, out ? 3 : 0, !G.human[p]);
  ui_text(!G.human[p] ? "CPU" : PNAME[p], cw_v2(x + 58, y + 3), 22, col_a(c, out ? 0.4f : 1), CW_ALIGN_LEFT);
  if (line && line[0]) ui_text(line, cw_v2(x + 58, y + 3 + 24 * ui_grow(22)), 26, cw_rgba(1, 1, 1, out ? 0.4f : 1), CW_ALIGN_LEFT);
}

static void draw_strip(void) {
  const mg_def *d = cur_def();
  float cw_ = 270, gap = 14, x0 = (STAGE_W - (cw_ * NP + gap * (NP - 1))) * 0.5f;
  for (int p = 0; p < NP; p++) {
    char line[48] = "";
    if (d->score_text) d->score_text(&G.c, p, line, sizeof line);
    draw_chip(p, x0 + p * (cw_ + gap), 10, cw_, line, 1);
  }
  /* the clock: not under the shell's pause menu, whose title would sit on it at a large text size */
  if (d->seconds > 0 && G.state == S_PLAY && cw_shell_screen_now() == CW_SHELL_PLAY) {
    char t[16];
    snprintf(t, sizeof t, "%d", (int)ceilf(G.c.time_left));
    float py = ui_clock_y(), ph = ui_clock_h(), pw = fmaxf(88, ui_text_w(t, 34) + 40 * ui_grow(34));
    ui_pill(cw_rect_make(STAGE_W * 0.5f - pw * 0.5f, py, pw, ph), cw_rgba(0, 0, 0, 0.5f));
    ui_text(t, cw_v2(STAGE_W * 0.5f, py + 2 * ui_grow(34)), 34, G.c.time_left < 6 ? cw_rgba(1, 0.4f, 0.35f, 1) : cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
}

/* Every screen is laid out from its text as drawn (the player's Text size, cartwheel/render2d.h): rows as tall as their
 * text, panels as wide as their widest line, lists that run out of room scroll with their selection, and lines that
 * stand over something (a tile, a podium) grow upward. At 100% each sits where it always did. */

/* the first of n rows to show when `fit` of them fit, keeping `sel` in view */
static int scroll_first(int n, int fit, int sel) {
  if (fit >= n) return 0;
  int first = sel - fit / 2;
  if (first < 0) first = 0;
  if (first > n - fit) first = n - fit;
  return first;
}
static void scroll_arrow(float cx, float y, float dir, float s) {
  cw_vertex2d v[3] = { { cw_v2(cx - s, y), { 0, 0 }, cw_rgba(1, 1, 1, 0.85f) }, { cw_v2(cx + s, y), { 0, 0 }, cw_rgba(1, 1, 1, 0.85f) }, { cw_v2(cx, y + dir * s), { 0, 0 }, cw_rgba(1, 1, 1, 0.85f) } };
  static const uint32_t idx[3] = { 0, 1, 2 };
  cw_r2d_triangles((cw_texture){ 0 }, v, 3, idx, 3);
}
/* a hint of prompt parts, centred, as many to a line as fit in w, the last line's foot at `bottom`; returns the top */
static float hint_lines(const char *const *parts, int n, float cx, float w, float bottom, float size, cw_color c) {
  float gap = 30 * ui_grow(size), lh = 0, wid[8];
  int brk[9], nl = 0;
  for (int i = 0; i < n; i++) { cw_vec2 s = ui_prompt_size(parts[i], -1, size); wid[i] = s.x; lh = fmaxf(lh, s.y); }
  brk[nl++] = 0;
  for (int i = 1, lw = 0; i <= n; i++) {
    float cur = 0;
    for (int k = brk[nl - 1]; k < i; k++) cur += wid[k] + (k > brk[nl - 1] ? gap : 0);
    (void)lw;
    if (i < n && cur + gap + wid[i] > w) brk[nl++] = i;
  }
  brk[nl] = n;
  float top = bottom - lh * (float)nl;
  for (int l = 0; l < nl; l++) {
    float lwid = 0;
    for (int k = brk[l]; k < brk[l + 1]; k++) lwid += wid[k] + (k > brk[l] ? gap : 0);
    float x = cx - lwid * 0.5f;
    for (int k = brk[l]; k < brk[l + 1]; k++) { ui_prompt(parts[k], -1, cw_v2(x, top + lh * (float)l), size, c, CW_ALIGN_LEFT); x += wid[k] + gap; }
  }
  return top;
}

static void draw_menu(void) {
  ui_vignette(1.6f);
  ui_title("PARTY PACK", cw_v2(STAGE_W / 2, 24), 96, G.clock);
  static const char *labels[6] = { "Party Tournament", "Free Play", "Trophy Room", "Rounds", "CPU Skill", "Rumble" };
  static const char *skills[3] = { "Easy", "Medium", "Hard" };
  char vals[6][24] = { "", "", "", "", "", "" };
  snprintf(vals[3], sizeof vals[3], "< %d >", 4 + 2 * (SV->rounds_opt % 3));
  snprintf(vals[4], sizeof vals[4], "< %s >", skills[SV->skill % 3]);
  snprintf(vals[5], sizeof vals[5], "< %s >", SV->rumble_off ? "Off" : "On");
  /* a pad with no seat joins with its south button (the shell's join press reads the pad itself, so the hint names that
   * input, not an action) */
  char who[48];
  snprintf(who, sizeof who, "%d player%s + %d CPU", G.humans, G.humans == 1 ? "" : "s", NP - G.humans);
  const char *parts[4] = { who, "new pad: {pad:south} joins", "{btn_north} join or leave", "{ui_back} title" };
  float hs = 26, ht = hint_lines(parts, 4, STAGE_W / 2, STAGE_W - 80, 668 + ui_line_h(hs) / ui_grow(hs), hs, cw_rgba(1, 1, 1, 0.85f));
  float g = ui_grow(34), rh = 54 * g, step = rh + 6 * g, w = 500;
  for (int i = 0; i < 6; i++) w = fmaxf(w, 56 * g + ui_text_w(labels[i], 34) + (vals[i][0] ? 30 * g + ui_text_w(vals[i], 34) : 0));
  w = fminf(w, STAGE_W - 40);
  float title_bottom = 24 + 120 * ui_grow(96), top = fmaxf(title_bottom + 12, fminf(300, ht - 12 - 6 * step + 6 * g));
  int fit = (int)((ht - 12 - top + 6 * g) / step), first = scroll_first(6, fit < 1 ? 1 : fit, G.menu_item);
  if (fit > 6) fit = 6;
  if (fit < 1) fit = 1;
  for (int k = 0; k < fit; k++) {
    int i = first + k;
    bool sel = i == G.menu_item;
    cw_rect r = cw_rect_make(STAGE_W / 2 - w / 2, top + k * step, w, rh);
    ui_panel(r, sel ? cw_rgba(1, 0.85f, 0.3f, 0.95f) : cw_rgba(0.08f, 0.07f, 0.2f, 0.8f), sel ? cw_rgba(1, 1, 1, 0.9f) : cw_rgba(1, 1, 1, 0.15f));
    cw_color tc = sel ? cw_rgba(0.15f, 0.1f, 0.25f, 1) : cw_rgba(1, 1, 1, 1);
    ui_text(labels[i], cw_v2(r.x + 28 * g, r.y + 6 * g), 34, tc, CW_ALIGN_LEFT);
    if (vals[i][0]) ui_text(vals[i], cw_v2(r.x + r.w - 28 * g, r.y + 6 * g), 34, tc, CW_ALIGN_RIGHT);
  }
  if (first > 0) scroll_arrow(STAGE_W / 2, top - 4, -1, 12 * g);
  if (first + fit < 6) scroll_arrow(STAGE_W / 2, top + fit * step - 6 * g + 4, 1, 12 * g);
}

static void draw_pick(void) {
  ui_grad(cw_rect_make(0, 0, STAGE_W, STAGE_H), cw_rgba(0.04f, 0.05f, 0.16f, 0.82f), cw_rgba(0.08f, 0.04f, 0.18f, 0.9f));
  ui_text("FREE PLAY", cw_v2(STAGE_W / 2, 24), 64, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  /* back, beside the heading */
  ui_prompt("{ui_back} back", -1, cw_v2(60, 24 + (ui_line_h(64) - ui_prompt_size("{ui_back} back", -1, 26).y) * 0.5f), 26, cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_LEFT);
  /* three cards to a row (the pad moves by three); each card as tall as its text wraps. When a card cannot hold the rule
   * as well, the cards show the name and the line under it, and the chosen game's rule runs under the grid */
  float x0 = 110, gx = 20, cwid = 350, inner = cwid - 36, top = 24 + ui_line_h(64) + 12, bottom = STAGE_H - 14;
  float nh = 0, th = 0, ih = ui_line_h(22);
  for (int i = 0; i < MG_N; i++) {
    char info[48];
    if (!is_unlocked(i)) snprintf(info, sizeof info, "needs %d party points", MG_LIST[i]->unlock_points);
    else snprintf(info, sizeof info, "%s   best %g", MG_LIST[i]->kind, SV->best[i]);
    ih = fmaxf(ih, ui_wrap_h(info, inner, 22));
    nh = fmaxf(nh, ui_wrap_h(MG_LIST[i]->name, inner, 36));
    th = fmaxf(th, ui_wrap_h(is_unlocked(i) ? MG_LIST[i]->tagline : "Locked", inner, 24));
  }
  float pad = 10 * ui_grow(36), full_h = pad + nh + 4 + th + 4 + ih + pad, small_h = pad + nh + 4 + ih + pad;
  int rows = (MG_N + 2) / 3, selrow = G.pick_item / 3;
  bool compact = (bottom - top + 20) / (full_h + 20) < 2.0f;
  if (fabsf(ui_grow(24) - 1) < 1e-3f) full_h = fmaxf(full_h, 150); /* 100%: the cards as they always were */
  float ch = compact ? small_h : full_h, detail_top = bottom;
  const mg_def *sd = MG_LIST[G.pick_item];
  if (compact) {
    const char *rule = is_unlocked(G.pick_item) ? sd->tagline : "Locked";
    float dh = ui_wrap_h(rule, STAGE_W - 2 * x0, 24);
    detail_top = bottom - dh;
    ui_text_wrap(rule, cw_rect_make(x0, detail_top, STAGE_W - 2 * x0, dh), 24, cw_rgba(1, 0.92f, 0.6f, 1), CW_ALIGN_CENTER);
    detail_top -= 10;
  }
  int fit = (int)((detail_top - top + 20) / (ch + 20));
  if (fit < 1) fit = 1;
  if (fit > rows) fit = rows;
  int first = scroll_first(rows, fit, selrow);
  for (int i = first * 3; i < MG_N && i < (first + fit) * 3; i++) {
    int cx = i % 3, cy = i / 3 - first;
    cw_rect r = cw_rect_make(x0 + cx * (cwid + gx), top + cy * (ch + 20), cwid, ch);
    bool sel = i == G.pick_item, lock = !is_unlocked(i);
    const mg_def *d = MG_LIST[i];
    ui_panel(r, sel ? cw_rgba(1, 0.85f, 0.3f, 0.95f) : cw_rgba(0.06f, 0.08f, 0.2f, 0.85f), sel ? cw_rgba(1, 1, 1, 1) : cw_rgba(1, 1, 1, 0.2f));
    cw_color tc = sel ? cw_rgba(0.12f, 0.08f, 0.2f, 1) : cw_rgba(1, 1, 1, 1);
    float y = r.y + pad;
    ui_text_wrap(d->name, cw_rect_make(r.x + 18, y, inner, nh), 36, tc, CW_ALIGN_LEFT);
    y += nh + 4;
    if (!compact) { ui_text_wrap(lock ? "Locked" : d->tagline, cw_rect_make(r.x + 18, y, inner, th), 24, tc, CW_ALIGN_LEFT); y += th + 4; }
    char s[48];
    if (lock) snprintf(s, sizeof s, "needs %d party points", d->unlock_points);
    else snprintf(s, sizeof s, "%s   best %g", d->kind, SV->best[i]);
    ui_text_wrap(s, cw_rect_make(r.x + 18, r.y + ch - pad - ih, inner, ih), 22, sel ? tc : cw_rgba(0.8f, 0.85f, 1, 0.9f), CW_ALIGN_LEFT);
    if (lock) cw_r2d_round_rect(r, 16, 8, cw_rgba(0, 0, 0, 0.45f));
  }
  if (first > 0) scroll_arrow(STAGE_W / 2, top - 4, -1, 12);
  if (first + fit < rows) scroll_arrow(STAGE_W / 2, top + fit * (ch + 20) - 16, 1, 12);
}

static void draw_trophy(void) {
  ui_vignette(1.5f);
  ui_text("TROPHY ROOM", cw_v2(STAGE_W / 2, 24), 64, cw_rgba(1, 0.9f, 0.4f, 1), CW_ALIGN_CENTER);
  char s[200];
  snprintf(s, sizeof s, "party points %d   tournaments %d", SV->points_total, SV->tournaments);
  float y = fmaxf(120, 24 + ui_line_h(64));
  ui_text(s, cw_v2(STAGE_W / 2, y), 34, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  y += ui_line_h(34);
  float by = fmaxf(250, y + 10 + 44);
  float wins_bottom = 0;
  for (int p = 0; p < NP; p++) {
    ui_blob(cw_v2(240 + p * 270, by), 44, p, 0, 0, 1, SV->wins[p] ? 1 : 0, false);
    snprintf(s, sizeof s, "%d win%s", SV->wins[p], SV->wins[p] == 1 ? "" : "s");
    ui_text(s, cw_v2(240 + p * 270, by + 60), 30, PCOL[p], CW_ALIGN_CENTER);
    wins_bottom = by + 60 + ui_line_h(30);
  }
  /* the games: two columns when the longest line fits half the width, otherwise one; up / down scroll a list too long */
  float top = fmaxf(380, wins_bottom + 8), lh = fmaxf(42, ui_line_h(26) * 1.05f), widest = 0;
  char lines[MG_MAX][96];
  for (int i = 0; i < MG_N; i++) {
    snprintf(lines[i], sizeof lines[i], "%s: %s  played %d  best %g", MG_LIST[i]->name, is_unlocked(i) ? "open" : "locked", SV->plays[i], SV->best[i]);
    widest = fmaxf(widest, ui_text_w(lines[i], 26));
  }
  int cols = widest <= 460 ? 2 : 1, rows = (MG_N + cols - 1) / cols;
  float colx = cols == 2 ? 180 : fmaxf(40, (STAGE_W - widest) * 0.5f);
  int fit = (int)((STAGE_H - 14 - top) / lh);
  if (fit < 1) fit = 1;
  if (fit > rows) fit = rows;
  if (trophy_scroll > rows - fit) trophy_scroll = rows - fit;
  if (trophy_scroll < 0) trophy_scroll = 0;
  for (int r = 0; r < fit; r++)
    for (int c = 0; c < cols; c++) {
      int i = (trophy_scroll + r) * cols + c;
      if (i >= MG_N) continue;
      ui_text(lines[i], cw_v2(colx + c * 480, top + r * lh), 26, is_unlocked(i) ? cw_rgba(1, 1, 1, 1) : cw_rgba(1, 1, 1, 0.5f), CW_ALIGN_LEFT);
    }
  if (trophy_scroll > 0) scroll_arrow(STAGE_W - 60, top + 12, -1, 12);
  if (trophy_scroll + fit < rows) scroll_arrow(STAGE_W - 60, top + fit * lh - 12, 1, 12);
}

static void draw_board(void) {
  ui_vignette(1.2f);
  int N = board_cells();
  char s[96];
  if (G.round >= G.rounds) snprintf(s, sizeof s, "FINAL STANDINGS");
  else snprintf(s, sizeof s, "ROUND %d OF %d", G.round + 1, G.rounds);
  ui_text(s, cw_v2(STAGE_W / 2, 18), 58, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  float y = fmaxf(96, 18 + ui_line_h(58));
  for (int i = 0; i <= N; i++) { /* labels on the special tiles: over the tile, growing upward */
    int kind = i == N ? EV_NONE : cell_used(i) ? EV_NONE : cell_kind(i);
    if (i != N && kind == EV_NONE) continue;
    cw_vec2 p = hub_cell_screen(i);
    if (i == N) ui_text_up("END", cw_v2(p.x, p.y - 30), 26, cw_rgba(1, 0.95f, 0.5f, 1), CW_ALIGN_CENTER);
    else if (kind == EV_STAR) ui_text_up("+2", cw_v2(p.x, p.y - 30), 24, cw_rgba(1, 0.7f, 0.95f, 1), CW_ALIGN_CENTER);
    else if (kind == EV_STEAL) ui_text_up("TAKE", cw_v2(p.x, p.y - 28), 20, cw_rgba(1, 0.65f, 0.3f, 1), CW_ALIGN_CENTER);
    else if (kind == EV_SWAP) ui_text_up("SWAP", cw_v2(p.x, p.y - 28), 20, cw_rgba(0.4f, 0.95f, 0.9f, 1), CW_ALIGN_CENTER);
  }
  float chip_y = STAGE_H - 22 - ui_chip_h();
  for (int p = 0; p < NP; p++) {
    snprintf(s, sizeof s, "%d", G.st_ticks < 110 ? G.mid[p] : G.points[p]);
    draw_chip(p, 70 + p * 285, chip_y, 270, s, 1);
  }
  /* the prompt over the chips */
  const char *pr = G.round < G.rounds ? "{ui_accept} play" : "{ui_accept} the final standings";
  float pr_y = fminf(580, chip_y - 10 - ui_prompt_size(pr, -1, 28).y);
  if (G.round < G.rounds) {
    snprintf(s, sizeof s, !G.tournament || !is_double_round(G.round) ? "Next: %s" : G.round == G.rounds - 1 ? "Next: %s   (triple points)" : "Next: %s   (double points)", MG_LIST[G.order[G.round]]->name);
    ui_text(s, cw_v2(STAGE_W / 2, y), 40, cw_rgba(1, 0.92f, 0.5f, 1), CW_ALIGN_CENTER);
    y += ui_line_h(40);
    if (G.st_ticks > 150) ui_prompt(pr, -1, cw_v2(STAGE_W / 2, pr_y), 28, cw_rgba(1, 1, 1, 0.5f + 0.5f * sinf(G.clock * 6)), CW_ALIGN_CENTER);
  } else {
    ui_prompt(pr, -1, cw_v2(STAGE_W / 2, pr_y), 28, cw_rgba(1, 1, 1, 0.5f + 0.5f * sinf(G.clock * 6)), CW_ALIGN_CENTER);
  }
  if (G.tournament && G.round > 0 && G.st_ticks > 110) { /* what the board did, in words: in two columns when one runs out of room */
    char e[8][96];
    float ew = 0, step = 30 * ui_grow(22), top = fmaxf(140, y + 4);
    int n = G.nev < 8 ? G.nev : 8;
    for (int i = 0; i < n; i++) {
      int p = G.ev[i].p, q = G.ev[i].q;
      if (G.ev[i].kind == EV_STAR) snprintf(e[i], sizeof e[i], "%s lands on a star: +2", PNAME[p]);
      else if (G.ev[i].kind == EV_STEAL) {
        if (q >= 0) snprintf(e[i], sizeof e[i], "%s takes %d from %s", PNAME[p], G.ev[i].amt, PNAME[q]);
        else snprintf(e[i], sizeof e[i], "%s lands on a take space: nobody is ahead", PNAME[p]);
      } else if (G.ev[i].kind == EV_SWAP) snprintf(e[i], sizeof e[i], "%s swaps places with %s", PNAME[p], PNAME[q]);
      else snprintf(e[i], sizeof e[i], "%s: underdog point, +1", PNAME[p]);
      ew = fmaxf(ew, ui_text_w(e[i], 22) + 24);
    }
    int per = (int)((pr_y - 6 - top) / step);
    if (per < 1) per = 1;
    int cols = n > per ? 2 : 1;
    float bw = fmaxf(500, ew), gap = 20;
    if (cols == 2) bw = fminf(bw, (STAGE_W - 40 - gap) * 0.5f);
    for (int i = 0; i < n; i++) {
      int c = cols == 2 ? i / ((n + 1) / 2) : 0, r = cols == 2 ? i % ((n + 1) / 2) : i;
      float cx = cols == 2 ? STAGE_W / 2 + (c ? 1 : -1) * (bw + gap) * 0.5f : STAGE_W / 2;
      cw_r2d_rect(cw_rect_make(cx - bw / 2, top + (float)r * step - 2, bw, step - 2), cw_rgba(0, 0, 0, 0.35f));
      ui_text(e[i], cw_v2(cx, top + (float)r * step), 22, PCOL[G.ev[i].p], CW_ALIGN_CENTER);
    }
  }
}

/* prompt markup broken into lines (\n) at spaces outside the {glyph} marks, each line no wider than maxw */
static void prompt_wrap(const char *src, float maxw, float size, char *out, size_t cap) {
  snprintf(out, cap, "%s", src);
  size_t len = strlen(out), start = 0;
  int depth = 0;
  long last = -1;
  for (size_t i = 0; i <= len; i++) {
    char c = out[i];
    if (c == '{') depth++;
    else if (c == '}') depth--;
    if ((c == ' ' && depth == 0) || c == 0) {
      out[i] = 0;
      float w = ui_prompt_size(out + start, -1, size).x;
      out[i] = c;
      if (w > maxw && last >= 0) { out[last] = '\n'; start = (size_t)last + 1; }
      if (c == ' ') last = (long)i;
    }
  }
}

static void draw_intro(void) {
  const mg_def *d = cur_def();
  cw_r2d_rect(cw_rect_make(0, 0, STAGE_W, STAGE_H), cw_rgba(0.02f, 0.02f, 0.08f, 0.6f));
  float k = ease_back((float)G.st_ticks / 25.0f);
  /* the card as wide as its widest line (800 at least, the stage at most), the rule wrapped to it, as tall as it all */
  int nhow = 0;
  float need = ui_text_w(d->name, 72);
  if (d->controls) need = fmaxf(need, ui_prompt_size(d->controls, -1, 40).x);
  for (int i = 0; i < 3 && d->how[i]; i++, nhow++) need = fmaxf(need, 120 + ui_prompt_size(d->how[i], -1, 28).x);
  float spacing = 180;
  for (int p = 0; p < NP; p++) spacing = fmaxf(spacing, 20 + (G.human[p] ? ui_prompt_size("{btn_south} ready 10", p, 22).x : ui_text_w("READY", 22)));
  need = fmaxf(need, spacing * NP);
  float cwid = fminf(STAGE_W - 40, fmaxf(800, need + 80)), wrapw = cwid - 80;
  float tag_h = ui_wrap_h(d->tagline, wrapw, 30), tag_1 = ui_line_h(30);
  float g30 = ui_grow(30), g40 = ui_grow(40), g28 = ui_grow(28), g22 = ui_grow(22);
  float o_tag = 86 * ui_grow(72), o_ctl = o_tag + 42 * g30 + (tag_h - tag_1), o_how = o_ctl + 50 * g40;
  /* the control lines wrap to the card when one is wider than it; a wrapped line takes the room of its extra lines */
  char how[3][192];
  float how_extra = 0, how_w = 0;
  for (int i = 0; i < nhow; i++) {
    prompt_wrap(d->how[i], cwid - 120, 28, how[i], sizeof how[i]);
    cw_vec2 hs = ui_prompt_size(how[i], -1, 28);
    how_w = fmaxf(how_w, hs.x);
    if (strchr(how[i], '\n')) how_extra += hs.y - ui_prompt_size(d->how[i], -1, 28).y;
  }
  float o_end = o_how + 3 * 37 * g28 + how_extra, o_blob = o_end + 47 * g28, o_ready = o_blob + 42 * g22;
  float ch = fmaxf(440, o_ready + ui_line_h(22) + 22);
  /* taller than the stage: the controls' picture row goes (the lines under it show the same buttons) and the blobs close up */
  bool no_pics = false;
  if (ch > STAGE_H - 20) {
    no_pics = true;
    o_how = o_ctl, o_end = o_how + 3 * 37 * g28 + how_extra, o_blob = o_end + 34, o_ready = o_blob + 34;
    ch = o_ready + ui_line_h(22) + 14;
  }
  float ry = fmaxf(10, fminf(130, (STAGE_H - ch) * 0.5f));
  cw_rect r = cw_rect_make(STAGE_W / 2 - cwid / 2, ry + (1 - k) * 60, cwid, ch);
  ui_panel(r, cw_rgba(0.1f, 0.1f, 0.25f, 0.95f), cw_rgba(1, 0.85f, 0.3f, 1));
  ui_text(d->name, cw_v2(STAGE_W / 2, r.y + 14), 72, cw_rgba(1, 0.92f, 0.5f, 1), CW_ALIGN_CENTER);
  ui_text_wrap(d->tagline, cw_rect_make(r.x + 40, r.y + 14 + o_tag, wrapw, tag_h), 30, cw_rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER);
  /* the controls as pictures first, then the lines: prompt markup, drawn for every seat's device */
  if (d->controls && !no_pics) ui_prompt(d->controls, -1, cw_v2(STAGE_W / 2, r.y + 14 + o_ctl), 40, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  float hx = fminf(r.x + 60, STAGE_W / 2 - how_w * 0.5f), hy = r.y + 14 + o_how;
  for (int i = 0; i < nhow; i++) {
    ui_prompt(how[i], -1, cw_v2(hx, hy), 28, cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
    hy += 37 * g28 + (strchr(how[i], '\n') ? ui_prompt_size(how[i], -1, 28).y - ui_prompt_size(d->how[i], -1, 28).y : 0);
  }
  for (int p = 0; p < NP; p++) {
    cw_vec2 c = cw_v2(STAGE_W / 2 + ((float)p - 1.5f) * spacing, r.y + 14 + o_blob);
    ui_blob(c, 30, p, 0, 0, G.ready[p] ? 1 + 0.08f * sinf(G.clock * 8) : 1, G.ready[p] ? 1 : 0, !G.human[p]);
    if (G.ready[p] || !G.human[p]) ui_text("READY", cw_v2(c.x, r.y + 14 + o_ready), 22, G.ready[p] ? cw_rgba(0.6f, 1, 0.6f, 1) : cw_rgba(1, 1, 1, 0.6f), CW_ALIGN_CENTER);
    else if (G.ready_left > 0) { /* someone is ready: the seats still out see how long the card waits for them */
      char w[48];
      snprintf(w, sizeof w, "{btn_south} ready %d", G.ready_left);
      ui_prompt(w, p, cw_v2(c.x, r.y + 12 + o_ready), 22, cw_rgba(1, 0.85f, 0.4f, 1), CW_ALIGN_CENTER);
    } else ui_prompt("{btn_south} ready", p, cw_v2(c.x, r.y + 12 + o_ready), 22, cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER); /* each seat sees its own device */
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
  /* the foot first, from the bottom up: the prompt, the unlock, a new best */
  float foot = 675 + ui_prompt_size("{ui_accept} next", -1, 26).y / ui_grow(26);
  float y_prompt = foot - ui_prompt_size("{ui_accept} next", -1, 26).y, y_unlock = fminf(625, y_prompt - ui_line_h(40)), y_best = fminf(580, (G.unlock_toast >= 0 ? y_unlock : y_prompt) - ui_line_h(40));
  float rows_end = (G.new_best ? y_best : G.unlock_toast >= 0 ? y_unlock : y_prompt) - 8;
  /* the rows: two lines (who, then the score) when they fit, otherwise one wider line each */
  char sc[NP][48], nm[NP][16], pts[NP][16];
  float nw = 0, sw = 0, pw = 0;
  for (int i = 0; i < NP; i++) {
    int p = order[i];
    sc[i][0] = 0;
    if (cur_def()->score_text) cur_def()->score_text(&G.c, p, sc[i], sizeof sc[i]);
    snprintf(nm[i], sizeof nm[i], "%s", !G.human[p] ? "CPU" : PNAME[p]);
    snprintf(pts[i], sizeof pts[i], "+%d", G.round_pts[p]);
    nw = fmaxf(nw, ui_text_w(nm[i], 26)), sw = fmaxf(sw, ui_text_w(sc[i], 28)), pw = fmaxf(pw, ui_text_w(pts[i], 46));
  }
  bool under = false;
  for (int p = 0; p < NP; p++) under = under || (G.underdog[p] && G.tournament);
  float place_w = fmaxf(170, ui_text_w("4th", 44) + 40), top = fmaxf(160, 60 + ui_line_h(70) + 4);
  float two_h = fmaxf(84, 8 + 32 * ui_grow(26) + ui_line_h(28) + 8), gap = 16;
  bool one_line = top + NP * (two_h + gap) - gap > rows_end;
  float rh = one_line ? fmaxf(84, fmaxf(ui_line_h(44), ui_line_h(46)) + 16) : two_h;
  float under_w = under ? ui_text_w("+1 underdog", 20) : 0;
  float rw = one_line ? 20 + place_w + 70 + nw + 20 + sw + 30 + fmaxf(pw, under_w) + 30 : fmaxf(620, 240 + fmaxf(nw, sw) + 30 + fmaxf(pw, under_w) + 30);
  rw = fminf(rw, STAGE_W - 40);
  float step = fminf(rh + gap, (rows_end - top) / NP), x_end = STAGE_W / 2 - rw / 2;
  if (fabsf(ui_grow(26) - 1) < 1e-3f) x_end = 330, step = 100; /* 100%: as it always was */
  for (int i = 0; i < NP; i++) {
    int p = order[i];
    float slide = ease_out(((float)G.st_ticks - i * 10) / 30.0f);
    float x = flerp(-rw, x_end, slide), y = top + i * step;
    cw_rect r = cw_rect_make(x, y, rw, rh);
    ui_panel(r, cw_rgba(0.1f, 0.1f, 0.25f, 0.95f), col_a(PCOL[p], 1));
    ui_text(place_name[G.place[p]], cw_v2(x + 20, y + (rh - ui_line_h(44)) * 0.5f), 44, G.place[p] == 0 ? cw_rgba(1, 0.9f, 0.3f, 1) : cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
    float bx = x + 20 + (one_line ? place_w : 170) - 0;
    ui_blob(cw_v2(bx, y + rh * 0.5f), 28, p, 0, 0, 1, G.place[p] == 0 ? 1 : G.place[p] == 3 ? 3 : 0, !G.human[p]);
    float tx = bx + 50;
    if (one_line) {
      ui_text(nm[i], cw_v2(tx, y + (rh - ui_line_h(26)) * 0.5f), 26, PCOL[p], CW_ALIGN_LEFT);
      ui_text(sc[i], cw_v2(tx + nw + 20, y + (rh - ui_line_h(28)) * 0.5f), 28, cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
    } else {
      ui_text(nm[i], cw_v2(tx, y + 8), 26, PCOL[p], CW_ALIGN_LEFT);
      ui_text(sc[i], cw_v2(tx, y + 8 + 32 * ui_grow(26)), 28, cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
    }
    bool ud = G.underdog[p] && G.tournament;
    float ph = ui_line_h(46), uh = ud ? ui_line_h(20) : 0, py = y + (rh - ph - uh) * 0.5f;
    if (fabsf(ui_grow(26) - 1) < 1e-3f) py = y + 16;
    ui_text(pts[i], cw_v2(x + rw - 30, py), 46, cw_rgba(0.6f, 1, 0.6f, 1), CW_ALIGN_RIGHT);
    if (ud) ui_text("+1 underdog", cw_v2(x + rw - 30, fabsf(ui_grow(26) - 1) < 1e-3f ? y + 62 : py + ph), 20, cw_rgba(1, 0.85f, 0.4f, 1), CW_ALIGN_RIGHT);
  }
  if (G.new_best) ui_text("New best!", cw_v2(STAGE_W / 2, y_best), 40, cw_rgba(1, 0.8f, 0.3f, 1), CW_ALIGN_CENTER);
  if (G.unlock_toast >= 0) {
    char s[96];
    snprintf(s, sizeof s, "Unlocked: %s", MG_LIST[G.unlock_toast]->name);
    ui_text(s, cw_v2(STAGE_W / 2, y_unlock), 40, cw_rgba(0.6f, 1, 0.9f, 1), CW_ALIGN_CENTER);
  }
  if (G.st_ticks > 60) ui_prompt("{ui_accept} next", -1, cw_v2(STAGE_W / 2, y_prompt), 26, cw_rgba(1, 1, 1, 0.5f + 0.5f * sinf(G.clock * 6)), CW_ALIGN_CENTER);
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
  /* the heading first: the labels over the podiums keep below it */
  float y = 118, hdr_bot = 20 + ui_line_h(64);
  if (G.winner >= 0) {
    char s[64];
    snprintf(s, sizeof s, "%s wins!", !G.human[G.winner] ? "CPU" : PNAME[G.winner]);
    ui_text(s, cw_v2(STAGE_W / 2, y), 56, PCOL[G.winner], CW_ALIGN_CENTER);
    hdr_bot = y + ui_line_h(56);
    y += 64 * ui_grow(56);
  }
  if (G.tiebreak) {
    char tb[64];
    snprintf(tb, sizeof tb, "Tied on points: decided by %s", G.tiebreak);
    ui_text(tb, cw_v2(STAGE_W / 2, fmaxf(182, y)), 26, cw_rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER);
    hdr_bot = fmaxf(182, y) + ui_line_h(26);
  }
  /* over each podium, growing upward: the name, the points under it. Laid out from the size they draw at: never over
   * the heading, and a label that would meet one ranked above it moves down under it (at 200% text the podiums are
   * closer together than the labels are wide) */
  float k = ease_out(((float)G.st_ticks - 30) / 30.0f);
  float name_h = ui_line_h(28), pts_h = ui_line_h(32);
  struct { float x, top, w; char pts[24]; const char *name; int p; } L[NP];
  for (int i = 0; i < NP; i++) {
    int p = order[i];
    cw_vec2 sp = hub_podium_screen(i);
    L[i].p = p, L[i].x = sp.x, L[i].name = !G.human[p] ? "CPU" : PNAME[p];
    snprintf(L[i].pts, sizeof L[i].pts, "%d pts", G.points[p]);
    L[i].w = fmaxf(ui_text_w(L[i].pts, 32), ui_text_w(L[i].name, 28));
    float pts_top = sp.y - 32 * k + pts_h / ui_grow(32) - pts_h; /* the points' foot stays where it was at 100% */
    L[i].top = fmaxf(pts_top - name_h, hdr_bot + 6);
    for (int pass = 0; pass < 2; pass++)
      for (int j = 0; j < i; j++) {
        bool side = fabsf(L[i].x - L[j].x) < (L[i].w + L[j].w) * 0.5f + 10;
        bool over = L[i].top < L[j].top + name_h + pts_h + 4 && L[j].top < L[i].top + name_h + pts_h + 4;
        if (side && over) L[i].top = L[j].top + name_h + pts_h + 4;
      }
  }
  for (int i = 0; i < NP; i++) {
    ui_text(L[i].name, cw_v2(L[i].x, L[i].top), 28, col_a(PCOL[L[i].p], k), CW_ALIGN_CENTER);
    ui_text(L[i].pts, cw_v2(L[i].x, L[i].top + name_h), 32, cw_rgba(1, 1, 1, k), CW_ALIGN_CENTER);
  }
  const char *pr = "{ui_accept} back to the menu";
  float ph = ui_prompt_size(pr, -1, 26).y;
  ui_prompt(pr, -1, cw_v2(STAGE_W / 2, 672 + ph / ui_grow(26) - ph), 26, cw_rgba(1, 1, 1, 0.5f + 0.5f * sinf(G.clock * 6)), CW_ALIGN_CENTER);
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
    bool art_clean = false; /* development (key art): PP_KEYART or PP_NOHUD draw no 2D over the 3D view */
#ifdef PP_AUTOPILOT
    art_clean = getenv("PP_KEYART") || (getenv("PP_NOHUD") && over3d);
#endif
    cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = !(over3d || G_hub_live), .clear_color = cw_rgba(0.05f, 0.05f, 0.1f, 1), .ambient = art_ambient });
    art_world(false);
    if (G.state == S_RESULT || G.state == S_FINISH || G.state == S_PLAY || G.state == S_COUNT || G.state == S_INTRO) fx_update(sc == CW_SHELL_PLAY ? dt_ : 0);
    stage_begin();
    bool hud = cw_shell_hud_shown(); /* the game's own panels and banners: not under the shell's menus (the world stays) */
    if (art_clean) {
    } else if (in_game) {
      const mg_def *d = cur_def();
      if (!G.scene3d) art_world(true);   /* the 2D world is lit: sprites with normal maps, real 2D lights */
      pp_clock_on = d->seconds > 0;
      d->draw(&G.c, (float)alpha, G.clock);
      art_world(false);                  /* the HUD, particles and banners are not */
      fx_draw();
      if (hud && G.state != S_RESULT) draw_strip(); /* the results table has the same numbers, and its heading needs the top at large text */
      if (hud && G.state == S_INTRO) draw_intro();
      if (hud && G.state == S_COUNT) draw_count();
      if (hud && G.state == S_FINISH) ui_text("FINISH!", cw_v2(STAGE_W / 2, 280), 130 * (1 + 0.3f * ease_out(1 - G.st_ticks / 20.0f)), cw_rgba(1, 0.9f, 0.3f, 1), CW_ALIGN_CENTER);
      if (hud && G.state == S_RESULT) draw_results();
    } else if (hud) {
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

/* debug channel: "bind <action> <bindings>" replaces an action's bindings (the text form, "key:x, pad:north"), as the
 * shell's controls screen does: for checking a rebound action's prompt */
static void cmd_bind(int argc, const char **argv, cw_reply *r) {
  if (argc < 3) { cw_reply_error(r, "bind <action> <bindings>"); return; }
  cw_action a = cw_action_find(argv[1]);
  if (a < 0) { cw_reply_error(r, "no such action"); return; }
  char list[256] = "";
  for (int i = 2; i < argc; i++) { if (i > 2) strncat(list, " ", sizeof list - strlen(list) - 1); strncat(list, argv[i], sizeof list - strlen(list) - 1); }
  cw_action_clear_bindings(a);
  if (!cw_action_bind_text(a, list)) { cw_reply_error(r, "bad bindings"); return; }
  cw_reply_printf(r, "ok");
}

static void init(void *user) {
  (void)user;
  cw_project_load_input();
  cw_r2d_layer_set(0, &(cw_r2d_layer_desc){ .lit = true });
  a_left = cw_action_find("move_left"); a_right = cw_action_find("move_right");
  a_up = cw_action_find("move_up"); a_down = cw_action_find("move_down");
  a_south = cw_action_find("btn_south"); a_east = cw_action_find("btn_east"); a_west = cw_action_find("btn_west"); a_north = cw_action_find("btn_north");
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
  u_ok = cw_action_find("ui_accept"); u_back = cw_action_find("ui_back"); u_start = cw_action_find("ui_start"); u_pause = cw_action_find("pause");
  cw_tree_add_hook(cw_scene_app_tree(), 0, 900, hold_hook, NULL);
  bus_music = cw_shell_music_bus();
  bus_sfx = cw_shell_sfx_bus();
  register_games();
  SV = cw_shell_game_save();
  pp_rumble_off = SV->rumble_off != 0;
  for (int i = 0; i < MG_N; i++) if (MG_LIST[i]->unlock_points == 0) SV->unlocked |= 1u << i;
  sfx_init();
  load_scene(NULL); /* the 3D hub behind the title and join screens */
#ifdef PP_AUTOPILOT
  cw_debug_command("game", "game <id>: every round left on that game", cmd_game);
#endif
  cw_debug_command("sw", "sw ready_forever <0|1>: test switch (the instructions card waits for every person, as it once did)", cmd_sw);
  cw_debug_command("bind", "bind <action> <bindings>: replace an action's bindings (as the controls screen does)", cmd_bind);
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.round", &G.round);
  cw_debug_i32("game.mg", &G.cur);
  cw_debug_i32("game.seen", &dbg_seen);
  cw_debug_i32("game.mg_seen", &dbg_mg_seen);
  cw_debug_i32("game.tick", &G.c.tick);
  cw_debug_field("game.score", G.c.score, CW_DBG_F32, NP);
  cw_debug_field("game.points", G.points, CW_DBG_I32, NP);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_i32("game.points_total", &SV->points_total);
  cw_debug_u32("game.unlocked", &SV->unlocked);
  cw_debug_i32("game.winner", &G.winner);
  cw_debug_i32("game.humans", &G.humans);
  cw_debug_i32("game.ready_left", &G.ready_left);
  cw_debug_i32("game.events", &G.nev);
  cw_debug_field("game.wins", G.round_wins, CW_DBG_I32, NP);
  cw_debug_i32("game.hitstops", &G.hitstop_shown);
  cw_debug_field("cam.offset", cam_offset_dbg, CW_DBG_I32, 3);
  cw_debug_field("rumble.sent", pp_rumble_sent, CW_DBG_I32, NP);
  cw_debug_field("beat.presses", beat_presses_dbg, CW_DBG_I32, 3);
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
