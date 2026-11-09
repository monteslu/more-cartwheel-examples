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
  float best[MG_MAX - 1 - NP]; /* best score per mini-game */
  int32_t hat[NP];         /* each seat's hat (HAT_*): the slots of best[11..14], which no game used (eleven games), so
                            * an older save reads 0, no hat, and keeps its size and everything in it */
  int32_t rumble_off;      /* the Rumble setting, 1 off: the slot of best[MG_MAX - 1], which no game used (fewer than
                            * MG_MAX games), so an older save reads 0, on, and keeps its size and everything in it */
  uint32_t unlocked;       /* bit per mini-game: every one is open from the start (party points unlock hats) */
  int32_t rounds_opt;      /* 0..2 -> 4, 6, 8 rounds */
  int32_t skill;           /* CPU skill 0..2 */
} save_data;
_Static_assert(sizeof(save_data) == 4 * (2 + NP + MG_MAX + MG_MAX + 3), "the save keeps its size: a new layout would reset it");
_Static_assert(MG_MAX - 1 - NP >= 11, "best[] holds every mini-game");
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
  /* the board with decisions: after each mini-game every seat gets coins for its place and rolls (a die plus a bonus
   * for its place), then picks from the shop before it moves; passing the star with enough coins buys it, and the
   * star moves on. Most stars wins, then the furthest along. points[] is each seat's cell on the board. */
  int32_t coins[NP], stars[NP], roll[NP], die[NP], pick[NP], picked[NP], dbl[NP], dbl_next[NP];
  int32_t spent[NP], lasts[NP], best_die[NP], taken[NP]; /* the awards' stats: coins spent in the shop, last places, the best roll, coins taken */
  int32_t award[3], award_p[3];  /* the final screen's three awards (kind, seat; -1 none) */
  int32_t forks_met[NP], cuts[NP]; /* this tournament: forks each seat's walk met, and how many it cut (balance logs) */
  int32_t fork[NP], route[NP], cut[NP]; /* the board's forks: the fork a seat's roll reaches (-1 none), its route there
                                * (0 the long way round the corner, 1 the shortcut across), the fork it cut (-1) */
  int32_t team[NP];             /* a 2 vs 2 round: each seat's team (0 or 1); -1 every seat for itself */
  int32_t team_round;           /* this round is a team round: team_mode 1 (2 vs 2) or 2 (1 vs 3) */
  int32_t team_mode;
  int32_t star_cell, phase, move_t0, decide_t; /* phase 0 decide (the shop), 1 moving; move_t0: st_ticks the move began */
  uint32_t star_taken;         /* star cells already claimed */
  int32_t round_pts[NP], place[NP];
  bool ready[NP];
  int32_t ready_wait;          /* ticks since the first person readied on the card; the rest are counted in at READY_WAIT */
  int32_t ready_left;          /* whole seconds the card still waits (debug), 0 when nobody has readied */
  int32_t ready_forever;       /* test switch (sw ready_forever 1): the card waits for every seat, as it once did */
  int32_t no_forks;            /* test switch (sw no_forks 1): the board has no forks, the control of the fork gate */
  int32_t shop_forever;        /* test switch (sw shop_forever 1): the board's shop waits for every person */
  mg_ctx c;
  int32_t serial;
  uint32_t hash;
  bool scene3d;                /* a game's scene (a 3D game's, or a 2D game's set) is loaded */
  bool tree_sim;               /* the game's rules run in the scene (a 3D game): a hold pauses the tree */
  int32_t slow;                /* ticks left of a fail moment's half speed (fx_slowmo) */
  int32_t slow_shown;          /* debug game.slowmo: held ticks of half speed so far */
  int32_t unlock_toast;        /* hat just unlocked by party points (or -1) */
  int32_t hat_seat;            /* the seat whose hat the menu's Hat row shows (the last to change it) */
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
static bool hat_open(int h) { return SV->points_total >= HAT_POINTS[h]; }
static void hats_sync(void) { for (int p = 0; p < NP; p++) pp_hat[p] = SV->hat[p] >= 0 && SV->hat[p] < HAT_N && hat_open(SV->hat[p]) ? SV->hat[p] : HAT_NONE; }
/* the next open hat for seat p, dir +1 or -1 */
static void hat_step(int p, int dir) {
  int h = SV->hat[p];
  for (int k = 0; k < HAT_N; k++) { h = (h + dir + HAT_N) % HAT_N; if (hat_open(h)) break; }
  SV->hat[p] = h;
  G.hat_seat = p;
  hats_sync();
  cw_shell_save_commit();
}

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
int pp_batch_active(void);
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
  if (pp_batch_active()) return; /* a balance run has 2D games only: no scene to load */
#endif
  if (!path) path = "scenes/hub.cwscene";
  cw_scene_app_change_scene(path, false);
  if (G_hub_live) hub_build(G.humans);
}

/* ── mini-game lifecycle ────────────────────────────────────────────── */

static bool ahead_of(int a, int b);
static int team_mode_now(void);
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
  /* every third round from the second is a team round (team_mode_now): 2 vs 2, the leader and the last against the
   * two between (the pairs as even as the standings allow), or, when one seat is well clear, 1 vs 3: the leader
   * alone against the other three. The games know the teams (mg_ctx.team): teammates never knock each other */
  G.team_mode = team_mode_now();
  G.team_round = G.team_mode != 0;
  for (int p = 0; p < NP; p++) G.team[p] = -1;
  if (G.team_round) {
    int ord[NP] = { 0, 1, 2, 3 };
    for (int i = 1; i < NP; i++) for (int j = i; j > 0 && ahead_of(ord[j], ord[j - 1]); j--) { int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; }
    if (G.team_mode == 2) { G.team[ord[0]] = 0; G.team[ord[1]] = 1; G.team[ord[2]] = 1; G.team[ord[3]] = 1; }
    else { G.team[ord[0]] = 0; G.team[ord[3]] = 0; G.team[ord[1]] = 1; G.team[ord[2]] = 1; }
  }
  for (int p = 0; p < NP; p++) G.c.team[p] = G.team[p];
#ifdef PP_AUTOPILOT
  if (getenv("PP_MATES_KNOCK")) pp_mates_knock = getenv("PP_MATES_KNOCK")[0] == '1';
#endif
  G.c.team_mode = G.team_mode;
  G.c.tick = 0;
  G.c.done = false;
  G.c.time_total = G.c.time_left = d->seconds;
  cw_rng_seed(&G.c.rng, 9000u + (uint32_t)G.serial * 7919u + (uint32_t)idx * 131u);
  G.serial++;
  fx_reset();
  G.tree_sim = d->scene != NULL;
  const char *path = d->scene ? d->scene : d->set;
  G.scene3d = path != NULL;
  G.slow = 0;
  fx_map = NULL;
  apply_scope();
  double t0 = cw_time();
  load_scene(path);
  pp_set_on = d->set && !G_hub_live;
#ifdef PP_AUTOPILOT
  if (pp_batch_active()) pp_set_on = false;
#endif
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
  if (G.hold > 0 && G.tree_sim && cw_scene_app_tree()) cw_tree_set_paused(cw_scene_app_tree(), false);
  G.hold = 0;
  G.slow = 0;
  fx_map = NULL;
  pp_set_on = false;
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
/* the board's numbers: steps are a die (1 to 4) plus a bonus for the place; a star costs STAR_COST coins */
static const int PLACE_STEPS[4] = { 3, 2, 1, 0 };
enum { STAR_COST = 10, DECIDE_WAIT = 60 * 10 };
enum { SHOP_SAVE, SHOP_BOOT, SHOP_SWAP, SHOP_DOUBLE, SHOP_N };
static const char *const SHOP_NAME[SHOP_N] = { "Save", "Boot", "Swap", "Double" };
static const char *const SHOP_WHAT[SHOP_N] = { "keep your coins", "+3 spaces", "trade places with the one ahead", "your next game's coins count twice" };
static const int SHOP_COST[SHOP_N] = { 0, 3, 6, 4 };
enum { SHOP_BOOT_STEPS = 3 };

/* Board cells with an event. Each cell's event happens once, to the first seat that lands on it exactly. */
enum { EV_NONE, EV_COIN, EV_STEAL, EV_SWAP, EV_UNDERDOG, EV_BUY, EV_BOOT, EV_SHOPSWAP, EV_DOUBLE, EV_MISS, EV_CUT };
static int board_cells(void) { return G.rounds * 5 + 10; }
/* Forks: at the end of every row (cells 8, 18, 28, ...) the path splits. The long way runs round the corner, two
 * cells whose first is a +3 coin space; the shortcut is a bridge straight across to the next row (cell F + 3, two
 * steps saved). The star can sit on the corner, so the choice is spaces against coins and the star. */
static bool is_fork(int cell) { return !G.no_forks && cell > 0 && cell % 10 == 8 && cell + 3 <= board_cells(); }
static int cell_kind(int cell) {
  if (cell <= 0) return EV_NONE;
  if (cell % 7 == 5) return EV_COIN;
  if (cell % 11 == 8) return EV_STEAL;
  if (cell % 13 == 10) return EV_SWAP;
  if (cell % 10 == 9 && is_fork(cell - 1)) return EV_COIN; /* the corner on the long way pays */
  return EV_NONE;
}
/* a walk of n steps from cell `from`: the cells stepped on, in order; route 1 takes the shortcut at the first fork it
 * meets (*cut is that fork, else -1), every later fork the long way */
static int walk_path(int from, int n, int route, int *out, int *cut) {
  int N = board_cells(), c = from, k = 0;
  bool met = false;
  *cut = -1;
  while (n-- > 0 && c < N) {
    int nx = c + 1;
    if (is_fork(c) && !met) { met = true; if (route) { nx = c + 3; *cut = c; } }
    if (nx > N) nx = N;
    out[k++] = nx;
    c = nx;
  }
  return k;
}
/* the first fork a walk of up to n steps from `from` meets (-1 none) */
static int fork_in_reach(int from, int n) {
  for (int c = from; c < from + n && c < board_cells(); c++) if (is_fork(c)) return c;
  return -1;
}
static bool cell_used(int cell) { return cell < 64 && ((G.ev_taken[0] >> cell) & 1); }
static int per_row(void) { int n = board_cells() + 1; return n <= 40 ? 10 : (n + 3) / 4; }
#define BOARD_PER_ROW per_row()

/* the round before the last counts double and the last one triple (said on the board before they start): a seat
 * behind after the first rounds can still win, which the comeback target asks for */
static bool is_double_round(int r) { return r >= G.rounds - 2; }

static void rank_players(void) {
  const mg_def *d = cur_def();
  int idx[NP] = { 0, 1, 2, 3 };
  if (G.team_mode == 2) { /* 1 vs 3: the one alone wins by finishing ahead of two of the three (the middle one of
                          * them: an even chance between equals, whatever the game). It takes first and the three share
                          * third; or the three share second and the one is last */
    int solo = 0, beat = 0;
    for (int p = 0; p < NP; p++) if (G.team[p] == 0) solo = p;
    float me = G.c.score[solo];
    for (int p = 0; p < NP; p++) if (p != solo && (d->higher_is_better ? me > G.c.score[p] : me < G.c.score[p])) beat++;
    bool solo_wins = beat >= 2;
    for (int p = 0; p < NP; p++) G.place[p] = p == solo ? (solo_wins ? 0 : 3) : (solo_wins ? 2 : 1);
    int mult = G.round == G.rounds - 1 ? 3 : is_double_round(G.round) ? 2 : 1;
    for (int p = 0; p < NP; p++) G.round_pts[p] = PLACE_PTS[G.place[p]] * mult;
    return;
  }
  if (G.team_round) { /* 2 vs 2: each team's two scores added; both of the better team share first, the others third */
    float t[2] = { 0, 0 };
    for (int p = 0; p < NP; p++) t[G.team[p]] += G.c.score[p];
    bool a_wins = d->higher_is_better ? t[0] > t[1] : t[0] < t[1], tie = t[0] == t[1];
    for (int p = 0; p < NP; p++) G.place[p] = tie ? 0 : (G.team[p] == 0) == a_wins ? 0 : 2;
    int mult = G.round == G.rounds - 1 ? 3 : is_double_round(G.round) ? 2 : 1;
    for (int p = 0; p < NP; p++) G.round_pts[p] = PLACE_PTS[G.place[p]] * mult;
    return;
  }
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

/* the seat ahead on the tournament: more stars, then further along, then more round wins */
static bool ahead_of(int a, int b) {
  if (G.stars[a] != G.stars[b]) return G.stars[a] > G.stars[b];
  if (G.points[a] != G.points[b]) return G.points[a] > G.points[b];
  return G.round_wins[a] > G.round_wins[b];
}
static int standing_score(int p) { return G.stars[p] * 1000 + G.points[p]; } /* balance logs: one number per seat */

/* this round's format: every third round from the second (never the last) is a team round; it is 1 vs 3 when the
 * leader is well clear (a star more than anyone, or 4 spaces ahead of the next), else 2 vs 2. PP_TEAMS=0 in a test
 * turns team rounds off, 1 makes every one 2 vs 2, 2 every one 1 vs 3 */
static int team_mode_now(void) {
  if (!G.tournament || G.round % 3 != 1 || G.round >= G.rounds - 1) return 0;
#ifdef PP_AUTOPILOT
  { const char *t = getenv("PP_TEAMS"); if (t && t[0] >= '0' && t[0] <= '2') return t[0] - '0'; }
#endif
  int ord[NP] = { 0, 1, 2, 3 };
  for (int i = 1; i < NP; i++) for (int j = i; j > 0 && ahead_of(ord[j], ord[j - 1]); j--) { int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; }
  int a = ord[0], b = ord[1];
  return G.stars[a] > G.stars[b] || (G.stars[a] == G.stars[b] && G.points[a] - G.points[b] >= 4) ? 2 : 1;
}

/* the star's next cell: a few spaces in front of the seat furthest behind (a seat behind has the best chance at it) */
static void place_star(cw_rng *r) {
  int low = G.points[0];
  for (int p = 1; p < NP; p++) if (G.points[p] < low) low = G.points[p];
  int c = low + 4 + cw_rng_int(r, 0, 5), N = board_cells();
  if (c > N - 1) c = N - 1 - cw_rng_int(r, 0, 3);
  for (int p = 0; p < NP && c > 1; p++) if (G.points[p] == c) c--; /* never under a token */
  G.star_cell = c < 2 ? 2 : c;
}

/* A CPU's pick from the shop (made the moment the board opens): a star in reach with a Boot comes first, then a Swap
 * when far behind and rich, then Double when it can still afford the star after; otherwise it saves */
static int cpu_pick(int p) {
  int sk = G.c.cpu_skill[p] % 3, c = G.coins[p];
  uint32_t h = hash_u32(0x5eedu + (uint32_t)G.serial * 13u, (uint32_t)(p * 31 + G.round));
  if (sk == 0 && (h % 100) < 50) return SHOP_SAVE; /* an easy CPU often does nothing */
  int to_star = G.star_cell - G.points[p];
  if (c >= SHOP_COST[SHOP_BOOT] + STAR_COST && to_star > G.roll[p] && to_star <= G.roll[p] + 3) return SHOP_BOOT;
  int ahead = -1;
  for (int q = 0; q < NP; q++) if (q != p && G.points[q] > G.points[p] && (ahead < 0 || G.points[q] < G.points[ahead])) ahead = q;
  if (ahead >= 0 && c >= SHOP_COST[SHOP_SWAP] && G.points[ahead] - G.points[p] >= 5 && (sk == 2 || (h % 100) < 40)) return SHOP_SWAP;
  if (c >= SHOP_COST[SHOP_DOUBLE] + (sk == 2 ? STAR_COST : 0) && G.round < G.rounds - 1 && (h % 100) < 30 + 20 * sk) return SHOP_DOUBLE;
  return SHOP_SAVE;
}
/* A CPU's route at a fork in reach: round the corner when the star sits there and it can pay for it (after its shop
 * pick), or for the corner's coins when it is short of a star; otherwise the shortcut. An easy CPU often stays on the
 * long way */
static int cpu_route(int p) {
  int F = G.fork[p];
  if (F < 0) return 0;
  int sk = G.c.cpu_skill[p] % 3, left = G.coins[p] - (G.pick[p] != SHOP_SAVE ? SHOP_COST[G.pick[p]] : 0);
  uint32_t h = hash_u32(0xf02cu + (uint32_t)G.serial * 17u, (uint32_t)(p * 29 + G.round));
  bool star_round = G.star_cell == F + 1 || G.star_cell == F + 2;
  if (star_round) return left >= STAR_COST ? 0 : sk == 0 ? 0 : 1;
  if (sk == 0 && (h % 100) < 50) return 0;
  bool corner_pays = cell_kind(F + 1) == EV_COIN && !cell_used(F + 1);
  if (corner_pays && left < STAR_COST && left + 3 >= STAR_COST && (h % 100) < 70) return 0; /* three coins buy the next star */
  return 1;
}
static bool can_buy(int p, int item) {
  if (G.coins[p] < SHOP_COST[item]) return false;
  if (item == SHOP_SWAP) { for (int q = 0; q < NP; q++) if (q != p && G.points[q] > G.points[p]) return true; return false; }
  if (item == SHOP_DOUBLE) return G.round < G.rounds - 1; /* there is a next game */
  return true;
}

/* After a mini-game: coins for the place (the round's points), the underdog coin and space, and each seat's roll. The
 * board then opens on the shop (decide): every seat picks an item or saves, then they move (board_move). */
static void board_round(void) {
  G.nev = 0;
  int leader = 0;
  for (int p = 1; p < NP; p++) if (ahead_of(p, leader)) leader = p;
  memset(G.underdog, 0, sizeof G.underdog);
  cw_rng r;
  cw_rng_seed(&r, 31337u + (uint32_t)G.serial * 101u + (uint32_t)G.round);
  for (int p = 0; p < NP; p++) {
    G.from[p] = G.points[p];
    G.coins[p] += G.round_pts[p] * (G.dbl[p] ? 2 : 1);
    if (G.dbl[p]) ev_add(EV_DOUBLE, p, -1, G.round_pts[p]);
    G.dbl[p] = 0;
    if (G.place[p] == 0) G.round_wins[p]++;
    G.die[p] = 1 + cw_rng_int(&r, 0, 3);
    if (G.place[p] == NP - 1) G.lasts[p]++;
    G.roll[p] = G.die[p] + PLACE_STEPS[G.place[p]];
    if (G.roll[p] > G.best_die[p]) G.best_die[p] = G.roll[p];
  }
  for (int p = 0; p < NP; p++)
    if (p != leader && G.place[p] != 0 && (G.stars[p] < G.stars[leader] || G.points[p] <= G.points[leader] - UNDERDOG_GAP)) {
      G.underdog[p] = 1; G.coins[p] += 1; G.roll[p] += 1; ev_add(EV_UNDERDOG, p, -1, 1);
    }
  for (int p = 0; p < NP; p++) {
    G.picked[p] = !G.human[p];
    G.pick[p] = G.human[p] ? SHOP_SAVE : cpu_pick(p);
    G.fork[p] = fork_in_reach(G.points[p], G.roll[p] + SHOP_BOOT_STEPS); /* a Boot could carry it there */
    G.cut[p] = -1;
    G.route[p] = G.human[p] ? 0 : cpu_route(p);
  }
  G.phase = 0;
  G.decide_t = 0;
  G.move_t0 = 1 << 30;
}

/* the shop is closed: the items, then each seat walks its roll, best place first; a seat that passes the star with
 * the coins buys it (the star moves on), and the cell it lands on does its thing */
static void board_move(void) {
  int order[NP], k = 0;
  for (int pl = 0; pl < NP; pl++) for (int j = 0; j < NP; j++) { int p = (j + G.round) % NP; if (G.place[p] == pl) order[k++] = p; } /* tied places: the seat that goes first rotates */
  cw_rng r;
  cw_rng_seed(&r, 7331u + (uint32_t)G.serial * 103u + (uint32_t)G.round);
  int N = board_cells();
  for (int i = 0; i < NP; i++) { /* the shop first, in the same order */
    int p = order[i], it = G.pick[p];
    if (!can_buy(p, it) || it == SHOP_SAVE) continue;
    G.coins[p] -= SHOP_COST[it];
    G.spent[p] += SHOP_COST[it];
    if (it == SHOP_BOOT) { G.roll[p] += SHOP_BOOT_STEPS; ev_add(EV_BOOT, p, -1, SHOP_BOOT_STEPS); }
    else if (it == SHOP_DOUBLE) { G.dbl_next[p] = 1; ev_add(EV_DOUBLE, p, -2, 0); }
    else if (it == SHOP_SWAP) {
      int ahead = -1;
      for (int q = 0; q < NP; q++) if (q != p && G.points[q] > G.points[p] && (ahead < 0 || G.points[q] < G.points[ahead])) ahead = q;
      if (ahead >= 0) { int t = G.points[p]; G.points[p] = G.points[ahead]; G.points[ahead] = t; G.from[p] = G.points[p]; G.from[ahead] = G.points[ahead]; ev_add(EV_SHOPSWAP, p, ahead, 0); }
    }
  }
  for (int p = 0; p < NP; p++) { G.dbl[p] = G.dbl_next[p]; G.dbl_next[p] = 0; }
  for (int i = 0; i < NP; i++) { /* the walk, step by step: a shortcut skips the corner (and a star on it) */
    int p = order[i], start = G.points[p], path[32], cut;
    int n = walk_path(start, G.roll[p], G.route[p], path, &cut), end = n ? path[n - 1] : start;
    G.from[p] = start;
    G.cut[p] = cut;
    if (cut >= 0) ev_add(EV_CUT, p, -1, 0);
    if (fork_in_reach(start, n) >= 0 || cut >= 0) { G.forks_met[p]++; if (cut >= 0) G.cuts[p]++; }
    for (int k = 0; k < n; k++)
      if (path[k] == G.star_cell) {
        if (G.coins[p] >= STAR_COST) { G.coins[p] -= STAR_COST; G.stars[p]++; ev_add(EV_BUY, p, -1, STAR_COST); G.points[p] = end; place_star(&r); break; }
        else ev_add(EV_MISS, p, -1, STAR_COST - G.coins[p]);
      }
    G.points[p] = end;
  }
  for (int p = 0; p < NP; p++) G.mid[p] = G.points[p];
  for (int i = 0; i < NP; i++) {
    int p = order[i], cell = G.points[p];
    if (cell <= 0 || cell > N) continue;
    int kind = cell_kind(cell);
    if (kind == EV_NONE || cell_used(cell)) continue;
    G.ev_taken[0] |= 1ull << cell;
    if (kind == EV_COIN) { G.coins[p] += 3; ev_add(EV_COIN, p, -1, 3); }
    else if (kind == EV_STEAL) { /* take 3 coins from the seat ahead in the standings; nothing when you lead */
      int ahead = -1;
      for (int q = 0; q < NP; q++) if (q != p && ahead_of(q, p) && (ahead < 0 || ahead_of(ahead, q))) ahead = q;
      if (ahead >= 0) { int t = G.coins[ahead] >= 3 ? 3 : G.coins[ahead]; G.coins[ahead] -= t; G.coins[p] += t; G.taken[p] += t; ev_add(EV_STEAL, p, ahead, t); }
      else ev_add(EV_STEAL, p, -1, 0);
    } else if (kind == EV_SWAP) { /* trade places with a random other seat */
      int q = cw_rng_int(&r, 0, NP - 2);
      if (q >= p) q++;
      int t = G.points[p]; G.points[p] = G.points[q]; G.points[q] = t;
      ev_add(EV_SWAP, p, q, 0);
    }
  }
  G.phase = 1;
  G.move_t0 = G.st_ticks;
  if (G.round - 1 < 16 && G.round > 0) for (int p = 0; p < NP; p++) G.hist[G.round - 1][p] = standing_score(p);
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
  { int before = SV->points_total; /* a hat whose points this round passed */
    for (int p = 0; p < NP; p++) if (G.human[p]) before -= G.round_pts[p];
    for (int h = 1; h < HAT_N; h++) if (before < HAT_POINTS[h] && hat_open(h)) G.unlock_toast = h; }
  if (G.tournament) board_round();
  cw_shell_save_commit();
}

static void to_results(void) {
  rank_players();
  award_points();
#ifdef PP_AUTOPILOT
  if (batch_total)
    fprintf(stderr, "GAME id=%s skills=%d%d%d%d scores=%g,%g,%g,%g places=%d,%d,%d,%d ticks=%d team=%d teams=%d,%d,%d,%d mate_knocks=%d\n", cur_def()->id, G.c.cpu_skill[0], G.c.cpu_skill[1], G.c.cpu_skill[2],
           G.c.cpu_skill[3], (double)G.c.score[0], (double)G.c.score[1], (double)G.c.score[2], (double)G.c.score[3], G.place[0], G.place[1], G.place[2], G.place[3], G.c.tick,
           G.team_mode, G.team[0], G.team[1], G.team[2], G.team[3], pp_mate_knocks);
  pp_mate_knocks = 0;
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
      if (batch_total && d->scene) continue; /* a 2D game on a set plays its rules without the set */
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
  memset(G.coins, 0, sizeof G.coins);
  memset(G.spent, 0, sizeof G.spent); memset(G.lasts, 0, sizeof G.lasts); memset(G.best_die, 0, sizeof G.best_die); memset(G.taken, 0, sizeof G.taken);
  memset(G.stars, 0, sizeof G.stars);
  memset(G.forks_met, 0, sizeof G.forks_met);
  memset(G.cuts, 0, sizeof G.cuts);
  memset(G.dbl, 0, sizeof G.dbl);
  memset(G.dbl_next, 0, sizeof G.dbl_next);
  G.nev = 0;
  G.tiebreak = NULL;
  G.star_taken = 0;
  G.phase = 1;
  G.move_t0 = 0;
  new_order();
  { cw_rng r; cw_rng_seed(&r, 5150u + (uint32_t)SV->tournaments * 31u + (uint32_t)G.serial); G.star_cell = 6 + cw_rng_int(&r, 0, 3); }
  G.winner = -1;
  set_state(S_BOARD);
  sfx_music(1);
}

static void finish_tournament(void) {
  /* first by stars, then by how far along; a tie on both goes to the seat with more round wins, then to a draw from
   * the tournament's seeded rng (never the seat number, which favoured seat 1); the screen says which one decided it */
  int top = standing_score(0);
  for (int p = 1; p < NP; p++) if (standing_score(p) > top) top = standing_score(p);
  int cand[NP], nc = 0, most = -1;
  for (int p = 0; p < NP; p++) if (standing_score(p) == top && G.round_wins[p] > most) most = G.round_wins[p];
  bool tied_pts = false;
  { int n = 0; for (int p = 0; p < NP; p++) n += standing_score(p) == top; tied_pts = n > 1; }
  for (int p = 0; p < NP; p++) if (standing_score(p) == top && G.round_wins[p] == most) cand[nc++] = p;
  G.tiebreak = NULL;
  int best = cand[0];
  if (nc > 1) {
    cw_rng r;
    cw_rng_seed(&r, 7177u + (uint32_t)SV->tournaments * 433u + (uint32_t)G.serial * 29u);
    best = cand[cw_rng_int(&r, 0, nc - 1)];
    G.tiebreak = "a draw";
  } else if (tied_pts) G.tiebreak = "most round wins";
  G.winner = best;
  { /* three silly awards from what happened (a stat nobody scored gives no award) */
    const int32_t *stat[5] = { G.spent, G.lasts, G.best_die, G.taken, G.round_wins };
    int n = 0;
    for (int k = 0; k < 5 && n < 3; k++) {
      int who = -1, v = 0;
      for (int p = 0; p < NP; p++) if (stat[k][p] > v) v = stat[k][p], who = p;
      if (who >= 0 && (k != 4 || who != best)) { G.award[n] = k; G.award_p[n] = who; n++; }
    }
    for (; n < 3; n++) G.award_p[n] = -1;
  }
#ifdef PP_AUTOPILOT
  if (batch_total) {
    fprintf(stderr, "TOURN skills=%d%d%d%d rounds=%d winner=%d final=%d,%d,%d,%d after2=%d,%d,%d,%d wins=%d,%d,%d,%d tie=%s forks=%d cuts=%d stars=%d,%d,%d,%d\n", G.c.cpu_skill[0], G.c.cpu_skill[1], G.c.cpu_skill[2],
           G.c.cpu_skill[3], G.rounds, best, standing_score(0), standing_score(1), standing_score(2), standing_score(3), G.hist[1][0], G.hist[1][1], G.hist[1][2], G.hist[1][3],
           G.round_wins[0], G.round_wins[1], G.round_wins[2], G.round_wins[3], G.tiebreak ? G.tiebreak : "no",
           G.forks_met[0] + G.forks_met[1] + G.forks_met[2] + G.forks_met[3], G.cuts[0] + G.cuts[1] + G.cuts[2] + G.cuts[3], G.stars[0], G.stars[1], G.stars[2], G.stars[3]);
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
  if (batch_total) for (int i = 0; i < MG_N; i++) if (MG_LIST[i]->scene) SV->unlocked &= ~(1u << i); /* 2D rules only (the sets are looks) */
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
  if (argc >= 3 && !strcmp(argv[1], "ready_forever")) {
    G.ready_forever = atoi(argv[2]) != 0;
    cw_reply_printf(r, "{\"ready_forever\":%d}", G.ready_forever);
  } else if (argc >= 3 && !strcmp(argv[1], "shop_forever")) {
    G.shop_forever = atoi(argv[2]) != 0;
    cw_reply_printf(r, "{\"shop_forever\":%d}", G.shop_forever);
  } else if (argc >= 3 && !strcmp(argv[1], "old_locks")) { /* control: the six games a fresh save once had open, the rest behind party points */
    SV->unlocked = atoi(argv[2]) ? 0x723u : (1u << MG_N) - 1;
    cw_reply_printf(r, "{\"unlocked\":%u}", SV->unlocked);
  } else if (argc >= 4 && !strcmp(argv[1], "place")) { /* test: put a seat's token on a board cell (the fork gate) */
    int p = atoi(argv[2]) & 3, cell = atoi(argv[3]);
    G.points[p] = G.from[p] = G.mid[p] = cell < 0 ? 0 : cell > board_cells() ? board_cells() : cell;
    cw_reply_printf(r, "{\"seat\":%d,\"cell\":%d}", p, G.points[p]);
  } else if (argc >= 3 && !strcmp(argv[1], "no_forks")) { /* control: the board without forks (the shortcut does nothing) */
    G.no_forks = atoi(argv[2]) != 0;
    cw_reply_printf(r, "{\"no_forks\":%d}", G.no_forks);
  } else if (argc >= 3 && !strcmp(argv[1], "mates_knock")) { /* control: teammates' dashes knock again */
    pp_mates_knock = atoi(argv[2]) != 0;
    cw_reply_printf(r, "{\"mates_knock\":%d}", pp_mates_knock);
  } else cw_reply_error(r, "usage: sw ready_forever|shop_forever|old_locks|no_forks|mates_knock <0|1>, sw place <seat> <cell>");
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
  if (G.state != S_PLAY || !G.tree_sim) { G.hold = 0; cw_tree_set_paused(t, false); return; } /* the game ended inside the freeze */
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
  if (!G.tree_sim && G.hold > 0) { G.hold--; G.hitstop_shown++; return; }
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
    if (hs > 0) { G.hold = hs; if (G.tree_sim) cw_tree_set_paused(cw_scene_app_tree(), true); } }
  { int sm = fx_slowmo_take(); /* a fail moment: every other tick held for a while (half speed), the same holds as a hit-stop */
    if (sm > G.slow) G.slow = sm;
    if (G.slow > 0 && --G.slow % 2 == 0 && G.hold == 0) {
      G.hold = 1;
      G.slow_shown++;
      if (G.tree_sim) cw_tree_set_paused(cw_scene_app_tree(), true);
    } }
  G.c.tick++;
  if (d->seconds > 0) {
    G.c.time_left -= 1.0f / 60.0f;
    if (G.c.time_left <= 0) { G.c.time_left = 0; G.c.done = true; }
  }
  for (int p = 0; p < NP; p++) G.hash = hash_u32(G.hash, (uint32_t)(int32_t)(G.c.score[p] * 100.0f));
  if (G.c.done) { sfx(SFX_GO, 1, 0.7f); set_state(S_FINISH); }
}

float pp_anim_speed(void) { return G.state == S_PLAY && G.slow > 0 ? 0.5f : 1.0f; }
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
    const int items = 7;
    if (n == -2 || n == 2) { G.menu_item = (G.menu_item + (n > 0 ? 1 : items - 1)) % items; sfx(SFX_BLIP, 1, 1); }
    if ((n == -1 || n == 1) && G.menu_item == 3) { SV->rounds_opt = (SV->rounds_opt + (n > 0 ? 1 : 2)) % 3; sfx(SFX_BLIP, 1, 1); cw_shell_save_commit(); }
    if ((n == -1 || n == 1) && G.menu_item == 4) { SV->skill = (SV->skill + (n > 0 ? 1 : 2)) % 3; sfx(SFX_BLIP, 1, 1); cw_shell_save_commit(); }
    if ((n == -1 || n == 1) && G.menu_item == 5) toggle_rumble();
    if (G.menu_item == 6) /* each person turns their own hat: left / right, or the confirm button */
      for (int p = 0; p < NP; p++) {
        if (!G.human[p]) continue;
        int d = cw_action_just_pressed(p, u_left) ? -1 : cw_action_just_pressed(p, u_right) || cw_action_just_pressed(p, u_ok) || cw_action_just_pressed(p, a_south) ? 1 : 0;
        if (d) { hat_step(p, d); sfx(SFX_BLIP, 1, 1.2f); }
      }
    if (menu_back() && !AUTO) { sfx(SFX_BACK, 1, 1); on_quit(NULL); cw_shell_to_title(); break; } /* no pause menu on the panels */
    if (menu_ok()) {
      sfx(SFX_SELECT, 1, 1);
      if (G.menu_item == 0) start_tournament();
      else if (G.menu_item == 1) set_state(S_PICK);
      else if (G.menu_item == 2) set_state(S_TROPHY);
      else if (G.menu_item == 3) { SV->rounds_opt = (SV->rounds_opt + 1) % 3; cw_shell_save_commit(); }
      else if (G.menu_item == 4) { SV->skill = (SV->skill + 1) % 3; cw_shell_save_commit(); }
      else if (G.menu_item == 5) toggle_rumble();
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
    if (G.tournament && G.round > 0 && G.phase == 0) { /* the shop: each person picks with left / right and confirms */
      bool all = true, any = false;
      for (int p = 0; p < NP; p++) {
        if (!G.human[p] || G.picked[p]) { all = all && G.picked[p]; continue; }
        all = false;
        int d = cw_action_just_pressed(p, u_left) || cw_action_just_pressed(p, a_left) ? -1 : cw_action_just_pressed(p, u_right) || cw_action_just_pressed(p, a_right) ? 1 : 0;
        for (int k = 0; d && k < SHOP_N; k++) { G.pick[p] = (G.pick[p] + d + SHOP_N) % SHOP_N; if (can_buy(p, G.pick[p])) break; }
        if (d) sfx(SFX_BLIP, 1, 1);
        if (G.fork[p] >= 0) { /* a fork in reach: up / down picks the route */
          int v = cw_action_just_pressed(p, u_up) || cw_action_just_pressed(p, a_up) ? 0 : cw_action_just_pressed(p, u_down) || cw_action_just_pressed(p, a_down) ? 1 : -1;
          if (v >= 0 && v != G.route[p]) { G.route[p] = v; sfx(SFX_BLIP, 1, 1.2f); }
        }
        if (cw_action_just_pressed(p, a_south) || cw_action_just_pressed(p, u_ok) || menu_start(p)) { G.picked[p] = 1; any = true; sfx(G.pick[p] ? SFX_COIN : SFX_SELECT, 0.8f, 1.1f); }
      }
      (void)any;
      for (int p = 0; p < NP && !G.shop_forever; p++) if (G.human[p] && G.picked[p]) { if (++G.decide_t > DECIDE_WAIT) all = true; break; } /* once one has picked, the rest get ten seconds */
      if (G.st_ticks > 60 * 20 && !G.shop_forever) all = true; /* nobody touched anything: everyone saves */
      if (all && G.st_ticks > 40) { for (int p = 0; p < NP; p++) if (!G.picked[p]) G.pick[p] = SHOP_SAVE; board_move(); sfx(SFX_GO, 0.6f, 1.2f); }
      if (menu_back() && !AUTO) { sfx(SFX_BACK, 1, 1); end_game_cleanup(); to_menu(); }
      break;
    }
    int mt = G.st_ticks - (G.tournament && G.round > 0 ? G.move_t0 : 0); /* ticks since the tokens set off */
    bool done_anim = mt > 150;
    if (G.round >= G.rounds && G.tournament) {
      if (mt > 200 && (menu_ok() || AUTO)) finish_tournament();
    } else if (done_anim && (menu_ok() || (AUTO && mt > 220) || mt > 60 * 14)) begin_game(G.order[G.round]);
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
    if (G.st_ticks > 120 && (menu_ok() || AUTO)) { end_game_cleanup(); sfx(SFX_SELECT, 1, 1); start_tournament(); } /* one press: another tournament */
    else if (G.st_ticks > 120 && menu_back()) { sfx(SFX_BACK, 1, 1); end_game_cleanup(); to_menu(); }
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
    if (G.team_round) { /* the team letter on the chip's corner */
      float cx = x0 + p * (cw_ + gap) + cw_ - 26, cy = 10 + ui_chip_h() * 0.5f;
      cw_r2d_circle(cw_v2(cx, cy), 16, 16, G.team[p] ? cw_rgba(0.95f, 0.55f, 0.15f, 1) : cw_rgba(0.25f, 0.6f, 1, 1));
      ui_text(G.team[p] ? "B" : "A", cw_v2(cx, cy - ui_line_h(22) * 0.5f), 22, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
    }
  }
  if (G.team_round && (G.state == S_INTRO || G.state == S_COUNT)) { /* who is with whom, under the strip */
    char t[96];
    int a[3] = { -1, -1, -1 }, b[3] = { -1, -1, -1 };
    for (int p = 0; p < NP; p++) { int *v = G.team[p] ? b : a; if (v[0] < 0) v[0] = p; else if (v[1] < 0) v[1] = p; else v[2] = p; }
    if (G.team_mode == 2) snprintf(t, sizeof t, "1 vs 3: %s alone (team A) against %s, %s and %s", PNAME[a[0]], PNAME[b[0]], PNAME[b[1]], PNAME[b[2]]);
    else snprintf(t, sizeof t, "2 vs 2: team A %s and %s, team B %s and %s", PNAME[a[0]], PNAME[a[1]], PNAME[b[0]], PNAME[b[1]]);
    const char *t2 = G.team_mode == 2 ? (d->solo_tip ? d->solo_tip : "Alone? Finish ahead of two of the three to win") : d->team_tip ? d->team_tip : "Your scores add up";
    float w = fmaxf(ui_text_w(t, 24), ui_text_w(t2, 22)) + 40, y = 14 + ui_chip_h(), lh = ui_line_h(24);
    ui_pill(cw_rect_make(STAGE_W * 0.5f - w * 0.5f, y, w, lh + ui_line_h(22) + 8), cw_rgba(0, 0, 0, 0.6f));
    ui_text(t, cw_v2(STAGE_W * 0.5f, y + 3), 24, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
    ui_text(t2, cw_v2(STAGE_W * 0.5f, y + 3 + lh), 22, cw_rgba(1, 0.9f, 0.55f, 1), CW_ALIGN_CENTER);
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
  static const char *labels[7] = { "Party Tournament", "Free Play", "Trophy Room", "Rounds", "CPU Skill", "Rumble", "Hat" };
  static const char *skills[3] = { "Easy", "Medium", "Hard" };
  char vals[7][32] = { "", "", "", "", "", "", "" };
  snprintf(vals[3], sizeof vals[3], "< %d >", 4 + 2 * (SV->rounds_opt % 3));
  snprintf(vals[4], sizeof vals[4], "< %s >", skills[SV->skill % 3]);
  snprintf(vals[5], sizeof vals[5], "< %s >", SV->rumble_off ? "Off" : "On");
  { int hs = G.human[G.hat_seat] ? G.hat_seat : 0; /* the hat of the person who turned it last */
    snprintf(vals[6], sizeof vals[6], "< %s %s >", PNAME[hs], HAT_NAME[pp_hat[hs]]); }
  /* a pad with no seat joins with its south button (the shell's join press reads the pad itself, so the hint names that
   * input, not an action) */
  char who[48];
  snprintf(who, sizeof who, "%d player%s + %d CPU", G.humans, G.humans == 1 ? "" : "s", NP - G.humans);
  const char *parts[4] = { who, "new pad: {pad:south} joins", "{btn_north} join or leave", "{ui_back} title" };
  float hs = 26, ht = hint_lines(parts, 4, STAGE_W / 2, STAGE_W - 80, 668 + ui_line_h(hs) / ui_grow(hs), hs, cw_rgba(1, 1, 1, 0.85f));
  float g = ui_grow(34), rh = 54 * g, step = rh + 6 * g, w = 500;
  for (int i = 0; i < 7; i++) w = fmaxf(w, 56 * g + ui_text_w(labels[i], 34) + (vals[i][0] ? 30 * g + ui_text_w(vals[i], 34) : 0));
  w = fminf(w, STAGE_W - 40);
  float title_bottom = 24 + 120 * ui_grow(96), top = fmaxf(title_bottom + 12, fminf(300, ht - 12 - 7 * step + 6 * g));
  int fit = (int)((ht - 12 - top + 6 * g) / step), first = scroll_first(7, fit < 1 ? 1 : fit, G.menu_item);
  if (fit > 7) fit = 7;
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
  if (first + fit < 7) scroll_arrow(STAGE_W / 2, top + fit * step - 6 * g + 4, 1, 12 * g);
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
    snprintf(info, sizeof info, "%s   best %g", MG_LIST[i]->kind, SV->best[i]);
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
    snprintf(s, sizeof s, "%s   best %g", d->kind, SV->best[i]);
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
  { int open = 0, next = -1;
    for (int h = 1; h < HAT_N; h++) { if (hat_open(h)) open++; else if (next < 0) next = h; }
    if (next > 0) snprintf(s, sizeof s, "party points %d   tournaments %d   hats %d of %d (next at %d)", SV->points_total, SV->tournaments, open, HAT_N - 1, HAT_POINTS[next]);
    else snprintf(s, sizeof s, "party points %d   tournaments %d   every hat", SV->points_total, SV->tournaments); }
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
    snprintf(lines[i], sizeof lines[i], "%s: played %d  best %g", MG_LIST[i]->name, SV->plays[i], SV->best[i]);
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
  char s[128];
  bool deciding = G.tournament && G.round > 0 && G.phase == 0;
  int mt = G.st_ticks - (G.tournament && G.round > 0 ? G.move_t0 : 0);
  if (G.round >= G.rounds) snprintf(s, sizeof s, "FINAL STANDINGS");
  else snprintf(s, sizeof s, "ROUND %d OF %d", G.round + 1, G.rounds);
  ui_text(s, cw_v2(STAGE_W / 2, 18), 58, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  float y = fmaxf(96, 18 + ui_line_h(58));
  for (int i = 0; i <= N; i++) { /* labels on the special tiles: over the tile, growing upward */
    int kind = i == N ? EV_NONE : cell_used(i) ? EV_NONE : cell_kind(i);
    bool star = G.tournament && i == G.star_cell;
    if (i != N && kind == EV_NONE && !star) continue;
    cw_vec2 p = hub_cell_screen(i);
    if (i == N) ui_text_up("END", cw_v2(p.x, p.y - 30), 26, cw_rgba(1, 0.95f, 0.5f, 1), CW_ALIGN_CENTER);
    else if (star) { snprintf(s, sizeof s, "STAR %d", STAR_COST); ui_text_up(s, cw_v2(p.x, p.y - 30), 24, cw_rgba(1, 0.85f, 0.3f, 1), CW_ALIGN_CENTER); }
    else if (kind == EV_COIN) ui_text_up("+3", cw_v2(p.x, p.y - 30), 24, cw_rgba(1, 0.9f, 0.45f, 1), CW_ALIGN_CENTER);
    else if (kind == EV_STEAL) ui_text_up("TAKE", cw_v2(p.x, p.y - 28), 20, cw_rgba(1, 0.65f, 0.3f, 1), CW_ALIGN_CENTER);
    else if (kind == EV_SWAP) ui_text_up("SWAP", cw_v2(p.x, p.y - 28), 20, cw_rgba(0.4f, 0.95f, 0.9f, 1), CW_ALIGN_CENTER);
  }
  for (int i = 1; i < N; i++) if (is_fork(i)) { /* the bridges: a label in the gap */
    cw_vec2 p = hub_fork_screen(i);
    float w = ui_text_w("SHORTCUT", 18) + 14, h = ui_line_h(18) + 2;
    ui_pill(cw_rect_make(p.x - w * 0.5f, p.y - h * 0.5f, w, h), cw_rgba(0.04f, 0.06f, 0.1f, 0.78f));
    ui_text("SHORTCUT", cw_v2(p.x, p.y - ui_line_h(18) * 0.5f), 18, cw_rgba(0.6f, 0.95f, 1, 1), CW_ALIGN_CENTER);
  }
  float chip_y = STAGE_H - 22 - ui_chip_h();
  for (int p = 0; p < NP; p++) {
    snprintf(s, sizeof s, "%d star%s  %d coin%s", G.stars[p], G.stars[p] == 1 ? "" : "s", G.coins[p], G.coins[p] == 1 ? "" : "s");
    draw_chip(p, 70 + p * 285, chip_y, 270, s, 1);
  }
  if (deciding) { /* the shop: each seat's roll, and its pick over its chip */
    snprintf(s, sizeof s, "Rolls are in. Shop before you move (a star costs %d coins)", STAR_COST);
    ui_text_wrap(s, cw_rect_make(60, y, STAGE_W - 120, ui_wrap_h(s, STAGE_W - 120, 30)), 30, cw_rgba(1, 0.92f, 0.5f, 1), CW_ALIGN_CENTER);
    y += ui_wrap_h(s, STAGE_W - 120, 30) + 6;
    bool any_fork = false;
    for (int p = 0; p < NP; p++) any_fork = any_fork || G.fork[p] >= 0;
    int rows = any_fork ? 5 : 4;
    float lh = ui_line_h(22), top = chip_y - 8 - lh * (float)rows;
    for (int p = 0; p < NP; p++) {
      float cx = 70 + p * 285 + 135;
      int it = G.pick[p];
      cw_r2d_round_rect(cw_rect_make(cx - 132, top - 6, 264, lh * (float)rows + 8), 10, 6, cw_rgba(0.05f, 0.05f, 0.14f, 0.82f));
      if (G.fork[p] >= 0) { /* the fork ahead: the route, picked with up / down */
        const char *rt = G.route[p] ? "shortcut (+2 spaces)" : "long way (+3 coins)";
        if (G.human[p] && !G.picked[p]) { snprintf(s, sizeof s, "{ui_up|ui_down} %s", rt); ui_prompt(s, p, cw_v2(cx, top + lh * 3), 20, cw_rgba(0.6f, 0.95f, 1, 1), CW_ALIGN_CENTER); }
        else { snprintf(s, sizeof s, "fork: %s", rt); ui_text(s, cw_v2(cx, top + lh * 3), 20, cw_rgba(0.6f, 0.95f, 1, 0.9f), CW_ALIGN_CENTER); }
      }
      float pr_row = top + lh * (float)(rows - 1);
      snprintf(s, sizeof s, "rolled %d +%d = %d", G.die[p], G.roll[p] - G.die[p], G.roll[p]);
      ui_text(s, cw_v2(cx, top), 22, PCOL[p], CW_ALIGN_CENTER);
      snprintf(s, sizeof s, G.human[p] && !G.picked[p] ? "< %s %d >" : "%s %d", SHOP_NAME[it], SHOP_COST[it]);
      if (it == SHOP_SAVE) snprintf(s, sizeof s, G.human[p] && !G.picked[p] ? "< Save >" : "Save");
      ui_text(s, cw_v2(cx, top + lh), 22, can_buy(p, it) ? cw_rgba(1, 1, 1, 1) : cw_rgba(1, 0.5f, 0.5f, 1), CW_ALIGN_CENTER);
      ui_text(SHOP_WHAT[it], cw_v2(cx, top + lh * 2), 18, cw_rgba(0.85f, 0.88f, 1, 0.9f), CW_ALIGN_CENTER);
      if (G.human[p] && !G.picked[p]) ui_prompt("{btn_south} pick", p, cw_v2(cx, pr_row), 20, cw_rgba(1, 1, 1, 0.6f + 0.4f * sinf(G.clock * 3)), CW_ALIGN_CENTER);
      else ui_text(G.human[p] ? "ready" : "CPU picked", cw_v2(cx, pr_row), 20, cw_rgba(0.6f, 1, 0.7f, 0.9f), CW_ALIGN_CENTER);
    }
    return;
  }
  /* the prompt over the chips */
  const char *pr = G.round < G.rounds ? "{ui_accept} play" : "{ui_accept} the final standings";
  float pr_y = fminf(580, chip_y - 10 - ui_prompt_size(pr, -1, 28).y);
  if (G.round < G.rounds) {
    int team_next = team_mode_now();
    snprintf(s, sizeof s, team_next == 2 ? "Next: %s   (1 vs 3: the leader alone)" : team_next ? "Next: %s   (2 vs 2)" : !G.tournament || !is_double_round(G.round) ? "Next: %s" : G.round == G.rounds - 1 ? "Next: %s   (triple coins)" : "Next: %s   (double coins)", MG_LIST[G.order[G.round]]->name);
    ui_text(s, cw_v2(STAGE_W / 2, y), 40, cw_rgba(1, 0.92f, 0.5f, 1), CW_ALIGN_CENTER);
    y += ui_line_h(40);
    if (mt > 150) ui_prompt(pr, -1, cw_v2(STAGE_W / 2, pr_y), 28, cw_rgba(1, 1, 1, 0.5f + 0.5f * sinf(G.clock * 6)), CW_ALIGN_CENTER);
  } else if (mt > 150) {
    ui_prompt(pr, -1, cw_v2(STAGE_W / 2, pr_y), 28, cw_rgba(1, 1, 1, 0.5f + 0.5f * sinf(G.clock * 6)), CW_ALIGN_CENTER);
  }
  if (G.tournament && G.round > 0 && mt > 40) { /* what the board did, in words: in two columns when one runs out of room */
    char e[10][96];
    float ew = 0, step = 30 * ui_grow(22), top = fmaxf(140, y + 4);
    int n = G.nev < 10 ? G.nev : 10;
    for (int i = 0; i < n; i++) {
      int p = G.ev[i].p, q = G.ev[i].q, k = G.ev[i].kind;
      if (k == EV_COIN) snprintf(e[i], sizeof e[i], "%s lands on +3 coins", PNAME[p]);
      else if (k == EV_STEAL) {
        if (q >= 0) snprintf(e[i], sizeof e[i], "%s takes %d coins from %s", PNAME[p], G.ev[i].amt, PNAME[q]);
        else snprintf(e[i], sizeof e[i], "%s lands on a take space: nobody is ahead", PNAME[p]);
      } else if (k == EV_SWAP) snprintf(e[i], sizeof e[i], "%s swaps places with %s", PNAME[p], PNAME[q]);
      else if (k == EV_BUY) snprintf(e[i], sizeof e[i], "%s buys the STAR! It moves on", PNAME[p]);
      else if (k == EV_MISS) snprintf(e[i], sizeof e[i], "%s passes the star, %d coins short", PNAME[p], G.ev[i].amt);
      else if (k == EV_BOOT) snprintf(e[i], sizeof e[i], "%s buys a boot: +3 spaces", PNAME[p]);
      else if (k == EV_CUT) snprintf(e[i], sizeof e[i], "%s takes the shortcut across", PNAME[p]);
      else if (k == EV_SHOPSWAP) snprintf(e[i], sizeof e[i], "%s buys a swap and trades places with %s", PNAME[p], PNAME[q]);
      else if (k == EV_DOUBLE) snprintf(e[i], sizeof e[i], q == -2 ? "%s buys a double for the next game" : "%s's double pays: +%d coins", PNAME[p], G.ev[i].amt);
      else snprintf(e[i], sizeof e[i], "%s: underdog bonus, +1 coin and +1 space", PNAME[p]);
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
    snprintf(s, sizeof s, "New hat: %s (pick it on the menu)", HAT_NAME[G.unlock_toast]);
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
      if (ahead_of(b, a)) { order[j] = a; order[j - 1] = b; } else break;
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
    snprintf(tb, sizeof tb, "Tied on stars and spaces: decided by %s", G.tiebreak);
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
    snprintf(L[i].pts, sizeof L[i].pts, "%d star%s", G.stars[p], G.stars[p] == 1 ? "" : "s");
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
  { /* the awards, down the left and right of the podium */
    static const char *const AW[5] = { "Big Spender", "Never Gave Up", "Lucky Feet", "Pickpocket", "Most Games Won" };
    static const char *const AWHY[5] = { "most coins spent in the shop", "most last places, still smiling", "the biggest roll", "most coins taken", "most mini-games won" };
    float k2 = ease_out(((float)G.st_ticks - 70) / 30.0f), ay = fmaxf(260, 182 + ui_line_h(26) + 10);
    for (int i = 0; i < 3; i++) {
      int p = G.award_p[i];
      if (p < 0) continue;
      float x = i == 1 ? STAGE_W - 170 : 170, yy = ay + (i == 2 ? 2 : 0) * ui_line_h(30) * 1.4f;
      if (i == 2) x = 170;
      if (i == 2) yy = ay + ui_line_h(30) * 3.2f;
      char t[64];
      ui_text(AW[G.award[i]], cw_v2(x, yy), 30, col_a(cw_rgba(1, 0.85f, 0.35f, 1), k2), CW_ALIGN_CENTER);
      snprintf(t, sizeof t, "%s", !G.human[p] ? "CPU" : PNAME[p]);
      ui_text(t, cw_v2(x, yy + ui_line_h(30)), 26, col_a(PCOL[p], k2), CW_ALIGN_CENTER);
      ui_text(AWHY[G.award[i]], cw_v2(x, yy + ui_line_h(30) + ui_line_h(26)), 18, col_a(cw_rgba(1, 1, 1, 0.8f), k2), CW_ALIGN_CENTER);
    }
  }
  const char *pr = "{ui_accept} play again   {ui_back} menu";
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
    for (int i = 0; i <= N; i++) hub_cell(i, i == N ? 2 : (G.tournament && i == G.star_cell) ? 1 : 0);
    for (int i = 0; i < N; i++) hub_fork(i, is_fork(i));
    int mt = G.st_ticks - G.move_t0;
    float t = ease_out((float)mt / 100.0f), t2 = ease_out(((float)mt - 110.0f) / 50.0f);
    for (int p = 0; p < NP; p++) {
      /* the shop first (tokens wait), then the roll's walk, then what the cells did (coins, takes, swaps) */
      float cell = !(G.tournament && G.round > 0) ? (float)G.points[p]
                 : G.phase == 0 ? (float)G.from[p]
                 : mt < 110 ? flerp((float)G.from[p], (float)G.mid[p], t) : flerp((float)G.mid[p], (float)G.points[p], t2);
      if (cell > (float)N) cell = (float)N;
      hub_token(p, cell, 0);
      hub_token_cut(p, G.tournament && G.round > 0 && G.phase == 1 && mt < 110 ? G.cut[p] : -1); /* the walk crosses the bridge */
    }
  } else if (mode == 2) {
    int order[NP] = { 0, 1, 2, 3 };
    for (int i = 1; i < NP; i++)
      for (int j = i; j > 0; j--) {
        int a = order[j - 1], b = order[j];
        if (ahead_of(b, a)) { order[j] = a; order[j - 1] = b; } else break;
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
    if (G.state == S_RESULT || G.state == S_FINISH || G.state == S_PLAY || G.state == S_COUNT || G.state == S_INTRO) fx_update(sc == CW_SHELL_PLAY ? dt_ * pp_anim_speed() : 0);
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
  SV->unlocked = (1u << MG_N) - 1; /* every mini-game is open from the start; party points unlock hats */
  hats_sync();
  sfx_init();
  load_scene(NULL); /* the 3D hub behind the title and join screens */
#ifdef PP_AUTOPILOT
  cw_debug_command("game", "game <id>: every round left on that game", cmd_game);
#endif
  cw_debug_command("sw", "sw ready_forever|shop_forever|old_locks|no_forks|mates_knock <0|1>, sw place <seat> <cell>: test switches (the instructions card, or the board's shop, waits for every person; the old game locks; a board without forks; teammates' dashes knock; a token put on a cell)", cmd_sw);
  cw_debug_command("bind", "bind <action> <bindings>: replace an action's bindings (as the controls screen does)", cmd_bind);
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.round", &G.round);
  cw_debug_i32("game.mg", &G.cur);
  cw_debug_i32("game.seen", &dbg_seen);
  cw_debug_i32("game.mg_seen", &dbg_mg_seen);
  cw_debug_i32("game.tick", &G.c.tick);
  cw_debug_field("game.score", G.c.score, CW_DBG_F32, NP);
  cw_debug_field("game.points", G.points, CW_DBG_I32, NP);
  cw_debug_field("game.coins", G.coins, CW_DBG_I32, NP);
  cw_debug_field("game.stars", G.stars, CW_DBG_I32, NP);
  cw_debug_field("game.pick", G.pick, CW_DBG_I32, NP);
  cw_debug_i32("game.phase", &G.phase);
  cw_debug_i32("game.star_cell", &G.star_cell);
  cw_debug_field("game.team", G.team, CW_DBG_I32, NP);
  cw_debug_i32("game.team_mode", &G.team_mode);
  cw_debug_field("game.fork", G.fork, CW_DBG_I32, NP);
  cw_debug_field("game.route", G.route, CW_DBG_I32, NP);
  cw_debug_field("game.cut", G.cut, CW_DBG_I32, NP);
  cw_debug_field("game.awards", G.award_p, CW_DBG_I32, 3);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_i32("game.points_total", &SV->points_total);
  cw_debug_u32("game.unlocked", &SV->unlocked);
  cw_debug_i32("game.winner", &G.winner);
  cw_debug_i32("game.humans", &G.humans);
  cw_debug_i32("game.ready_left", &G.ready_left);
  cw_debug_i32("game.events", &G.nev);
  cw_debug_field("game.wins", G.round_wins, CW_DBG_I32, NP);
  cw_debug_i32("game.hitstops", &G.hitstop_shown);
  cw_debug_i32("game.slowmo", &G.slow_shown);
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
