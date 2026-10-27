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
  float load_ms, start_ms;     /* measured: scene switch and the game's own setup (not in gates) */
} G;

static cw_action a_left, a_right, a_up, a_down, a_a, a_b, a_x, a_y, u_up, u_down, u_left, u_right, u_ok, u_back;
static cw_bus bus_music, bus_sfx;
cw_bus pp_music_bus(void) { return bus_music; }
cw_bus pp_sfx_bus(void) { return bus_sfx; }

static const mg_def *cur_def(void) { return MG_LIST[G.cur]; }
static bool is_unlocked(int i) { return (SV->unlocked >> i) & 1; }

static void set_state(int s) { G.state = s; G.st_ticks = 0; }

#ifdef PP_AUTOPILOT
#define AUTO 1
#else
#define AUTO 0
#endif

/* ── input helpers ──────────────────────────────────────────────────── */

static bool any_human_pressed(cw_action a) {
  for (int p = 0; p < G.humans; p++) if (cw_action_just_pressed(p, a)) return true;
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

static void load_scene(const char *path) {
  if (!path) path = "scenes/hub.cwscene";
  cw_scene_app_change_scene(path, false);
}

/* ── mini-game lifecycle ────────────────────────────────────────────── */

static void begin_game(int idx) {
  G.cur = idx;
  const mg_def *d = cur_def();
  memset(&G.c.score, 0, sizeof G.c.score);
  memset(&G.c.eliminated, 0, sizeof G.c.eliminated);
  G.c.humans = G.humans;
  for (int p = 0; p < NP; p++) G.c.human[p] = p < G.humans;
  G.c.skill = SV->skill;
  G.c.round = G.round;
  G.c.tick = 0;
  G.c.done = false;
  G.c.time_total = G.c.time_left = d->seconds;
  cw_rng_seed(&G.c.rng, 9000u + (uint32_t)G.serial * 7919u + (uint32_t)idx * 131u);
  G.serial++;
  fx_reset();
  G.scene3d = d->scene != NULL;
  double t0 = cw_time();
  load_scene(d->scene);
  G.load_ms = (float)((cw_time() - t0) * 1000.0);
  d->start(&G.c);
  G.start_ms = (float)((cw_time() - t0) * 1000.0) - G.load_ms;
  memset(G.ready, 0, sizeof G.ready);
  for (int p = G.humans; p < NP; p++) G.ready[p] = true;
  set_state(S_INTRO);
  sfx_music(1);
}

static void end_game_cleanup(void) {
  if (G.cur >= 0 && G.cur < MG_N && cur_def()->stop) cur_def()->stop(&G.c);
  if (G.scene3d) load_scene(NULL);
  G.scene3d = false;
}

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
  int mult = (G.tournament && G.round == G.rounds - 1) ? 2 : 1;
  for (int p = 0; p < NP; p++) G.round_pts[p] = (4 - G.place[p]) * mult;
}

#define BOARD_PER_ROW 10
static int board_cells(void) { return G.rounds * 4 + 4; }
static bool is_star(int cell) { return cell > 0 && cell % 7 == 5; }

static void award_points(void) {
  const mg_def *d = cur_def();
  for (int p = 0; p < G.humans; p++) SV->points_total += G.round_pts[p];
  SV->plays[G.cur]++;
  G.new_best = false;
  float best = 0;
  for (int p = 0; p < G.humans; p++) best = d->higher_is_better ? fmaxf(best, G.c.score[p]) : best;
  if (best > SV->best[G.cur]) { SV->best[G.cur] = best; G.new_best = G.humans > 0; }
  G.unlock_toast = -1;
  for (int i = 0; i < MG_N; i++)
    if (!is_unlocked(i) && SV->points_total >= MG_LIST[i]->unlock_points) { SV->unlocked |= 1u << i; G.unlock_toast = i; }
  if (G.tournament) {
    for (int p = 0; p < NP; p++) {
      G.from[p] = G.pos[p];
      G.points[p] += G.round_pts[p];
      int cell = G.pos[p] + G.round_pts[p];
      if (cell > board_cells()) cell = board_cells();
      if (is_star(cell) && !((G.star_taken >> cell) & 1)) { G.star_taken |= 1u << cell; cell += 2; G.points[p] += 2; }
      if (cell > board_cells()) cell = board_cells();
      G.pos[p] = cell;
    }
  }
  cw_shell_save_commit();
}

static void to_results(void) {
  rank_players();
  award_points();
  bool human_won = false;
  for (int p = 0; p < G.humans; p++) if (G.place[p] == 0) human_won = true;
  sfx(human_won ? SFX_WIN : SFX_LOSE, 1, 1);
  if (human_won) fx_confetti(cw_rect_make(0, -10, STAGE_W, 10), 60);
  set_state(S_RESULT);
}

/* ── tournament ─────────────────────────────────────────────────────── */

static void new_order(void) {
  int pool[MG_MAX], n = 0;
  for (int i = 0; i < MG_N; i++) if (is_unlocked(i)) pool[n++] = i;
  cw_rng r;
  cw_rng_seed(&r, 4242u + (uint32_t)SV->tournaments * 977u + (uint32_t)G.serial);
  int last = -1;
  for (int k = 0; k < G.rounds; k++) {
    if (k % n == 0 && k > 0) { /* reshuffle a fresh bag */ }
    int pick, tries = 0;
    do { pick = pool[cw_rng_int(&r, 0, n - 1)]; tries++; } while (n > 1 && tries < 20 && (pick == last || (k >= 1 && pick == G.order[k - 1])));
    /* prefer games not yet used this tournament */
    for (int t = 0; t < 12; t++) {
      bool used = false;
      for (int j = 0; j < k; j++) if (G.order[j] == pick) used = true;
      if (!used || n <= k) break;
      pick = pool[cw_rng_int(&r, 0, n - 1)];
    }
    G.order[k] = pick;
    last = pick;
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
  G.star_taken = 0;
  new_order();
  G.winner = -1;
  set_state(S_BOARD);
  sfx_music(1);
}

static void finish_tournament(void) {
  int best = 0;
  for (int p = 1; p < NP; p++) if (G.pos[p] > G.pos[best] || (G.pos[p] == G.pos[best] && G.points[p] > G.points[best])) best = p;
  G.winner = best;
  SV->tournaments++;
  if (best < G.humans) SV->wins[best]++;
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
  G.cur = 0;
  G.serial = 0;
  G.hash = 0;
  G.menu_item = 0;
  G.pick_item = 0;
  load_scene(NULL);
  G.scene3d = false;
  fx_reset();
  set_state(S_MENU);
  sfx_music(1);
#ifdef PP_AUTOPILOT
  SV->unlocked = ~0u;
  SV->rounds_opt = 0;
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
}
static void on_sound(cw_shell_sound s, void *user) {
  (void)user;
  sfx(s == CW_SHELL_SND_MOVE ? SFX_BLIP : s == CW_SHELL_SND_BACK ? SFX_BACK : SFX_SELECT, 1, 1);
}
static void on_background(cw_shell_screen s, cw_vec2 canvas, void *user) {
  (void)user, (void)canvas;
  stage_begin();
  ui_backdrop(G.clock, cw_rgba(0.12f, 0.1f, 0.35f, 1), cw_rgba(0.45f, 0.18f, 0.5f, 1));
  if (s != CW_SHELL_PAUSE) for (int p = 0; p < NP; p++) {
    float bob = sinf(G.clock * 3 + (float)p) * 10;
    ui_blob(cw_v2(STAGE_W / 2 - 330 + (float)p * 220, 560 + bob), 70, p, sinf(G.clock + (float)p), 0, 1 + 0.05f * sinf(G.clock * 6 + (float)p), 1, false);
  }
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
    if (p < G.humans) {
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
  read_inputs();
  const mg_def *d = cur_def();
  d->tick(&G.c);
  G.c.tick++;
  if (d->seconds > 0) {
    G.c.time_left -= 1.0f / 60.0f;
    if (G.c.time_left <= 0) { G.c.time_left = 0; G.c.done = true; }
  }
  for (int p = 0; p < NP; p++) G.hash = hash_u32(G.hash, (uint32_t)(int32_t)(G.c.score[p] * 100.0f));
  if (G.c.done) { sfx(SFX_GO, 1, 0.7f); set_state(S_FINISH); }
}

static void fixed_update(void *user, double dt) {
  (void)user, (void)dt;
  if (!cw_shell_update()) return;
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
    for (int p = 0; p < G.humans; p++) if (!G.ready[p] && cw_action_just_pressed(p, a_a)) { G.ready[p] = true; sfx(SFX_COIN, 0.7f, 1.2f); }
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
  ui_blob(cw_v2(x + 30, y + 31), 18, p, 0, 0, 1, out ? 3 : 0, p >= G.humans);
  ui_text(p >= G.humans ? "CPU" : PNAME[p], cw_v2(x + 58, y + 3), 22, col_a(c, out ? 0.4f : 1), CW_ALIGN_LEFT);
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
  ui_backdrop(G.clock, cw_rgba(0.12f, 0.1f, 0.35f, 1), cw_rgba(0.45f, 0.18f, 0.5f, 1));
  ui_text("PARTY PACK", cw_v2(STAGE_W / 2, 40), 92, cw_rgba(1, 0.92f, 0.5f, 1), CW_ALIGN_CENTER);
  for (int p = 0; p < NP; p++) {
    float bob = sinf(G.clock * 3 + p) * 8;
    ui_blob(cw_v2(STAGE_W / 2 - 165 + p * 110, 190 + bob), 38, p, sinf(G.clock + p), 0, 1 + 0.04f * sinf(G.clock * 6 + p), p < G.humans ? 1 : 0, p >= G.humans);
  }
  static const char *labels[5] = { "Party Tournament", "Free Play", "Trophy Room", "Rounds", "CPU Skill" };
  static const char *skills[3] = { "Easy", "Medium", "Hard" };
  for (int i = 0; i < 5; i++) {
    bool sel = i == G.menu_item;
    cw_rect r = cw_rect_make(STAGE_W / 2 - 250, 270 + i * 70, 500, 58);
    ui_panel(r, sel ? cw_rgba(1, 0.85f, 0.3f, 0.95f) : cw_rgba(0.08f, 0.07f, 0.2f, 0.8f), sel ? cw_rgba(1, 1, 1, 0.9f) : cw_rgba(1, 1, 1, 0.15f));
    cw_color tc = sel ? cw_rgba(0.15f, 0.1f, 0.25f, 1) : cw_rgba(1, 1, 1, 1);
    ui_text(labels[i], cw_v2(r.x + 28, r.y + 9), 36, tc, CW_ALIGN_LEFT);
    char v[24] = "";
    if (i == 3) snprintf(v, sizeof v, "< %d >", 4 + 2 * (SV->rounds_opt % 3));
    if (i == 4) snprintf(v, sizeof v, "< %s >", skills[SV->skill % 3]);
    if (v[0]) ui_text(v, cw_v2(r.x + r.w - 28, r.y + 9), 36, tc, CW_ALIGN_RIGHT);
  }
  char s[96];
  snprintf(s, sizeof s, "%d player%s + %d CPU", G.humans, G.humans == 1 ? "" : "s", NP - G.humans);
  ui_text(s, cw_v2(STAGE_W / 2, 650), 28, cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER);
}

static void draw_pick(void) {
  ui_backdrop(G.clock, cw_rgba(0.1f, 0.25f, 0.4f, 1), cw_rgba(0.1f, 0.12f, 0.3f, 1));
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
  ui_backdrop(G.clock, cw_rgba(0.3f, 0.2f, 0.1f, 1), cw_rgba(0.15f, 0.1f, 0.3f, 1));
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
  ui_backdrop(G.clock, cw_rgba(0.1f, 0.3f, 0.3f, 1), cw_rgba(0.1f, 0.1f, 0.3f, 1));
  int N = board_cells();
  int rows = (N + 1 + BOARD_PER_ROW - 1) / BOARD_PER_ROW;
  char s[96];
  if (G.round >= G.rounds) snprintf(s, sizeof s, "FINAL STANDINGS");
  else snprintf(s, sizeof s, "ROUND %d OF %d", G.round + 1, G.rounds);
  ui_text(s, cw_v2(STAGE_W / 2, 18), 58, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  for (int i = 0; i < N; i++) { /* the path */
    cw_vec2 a = board_cell_pos(i, rows), b = board_cell_pos(i + 1, rows);
    cw_r2d_line(a, b, 14, cw_rgba(0, 0, 0, 0.3f));
  }
  for (int i = 0; i <= N; i++) {
    cw_vec2 p = board_cell_pos(i, rows);
    bool star = is_star(i) && !((G.star_taken >> i) & 1);
    cw_color c = i == N ? cw_rgba(1, 0.85f, 0.2f, 1) : star ? cw_rgba(0.95f, 0.5f, 0.9f, 1) : cw_rgba(0.85f, 0.9f, 1, 0.9f);
    cw_r2d_circle(p, i == N ? 30 : 22, 20, c);
    cw_r2d_circle_lines(p, i == N ? 30 : 22, 3, 20, cw_rgba(0, 0, 0, 0.5f));
    if (i == N) ui_text("END", cw_v2(p.x, p.y - 12), 24, cw_rgba(0.2f, 0.1f, 0.2f, 1), CW_ALIGN_CENTER);
    else if (star) ui_text("+2", cw_v2(p.x, p.y - 12), 22, cw_rgba(0.2f, 0.05f, 0.2f, 1), CW_ALIGN_CENTER);
  }
  float t = ease_out((float)G.st_ticks / 100.0f);
  for (int p = 0; p < NP; p++) {
    float cell = flerp((float)G.from[p], (float)G.pos[p], G.tournament && G.round > 0 ? t : 1);
    int c0 = (int)cell;
    float f = cell - (float)c0;
    cw_vec2 a = board_cell_pos(c0, rows), b = board_cell_pos(c0 + 1 > N ? N : c0 + 1, rows);
    cw_vec2 pos = cw_v2(flerp(a.x, b.x, f) + (p - 1.5f) * 12, flerp(a.y, b.y, f) - 16 - fabsf(sinf(cell * 3.14159f)) * 14);
    ui_blob(pos, 22, p, 0, 0, 1, 1, p >= G.humans);
  }
  for (int p = 0; p < NP; p++) {
    snprintf(s, sizeof s, "%d", G.points[p]);
    draw_chip(p, 70 + p * 285, 640, 270, s, 1);
  }
  if (G.round < G.rounds) {
    snprintf(s, sizeof s, "Next: %s", MG_LIST[G.order[G.round]]->name);
    ui_text(s, cw_v2(STAGE_W / 2, 96), 40, cw_rgba(1, 0.92f, 0.5f, 1), CW_ALIGN_CENTER);
    if (G.st_ticks > 150) ui_text("press A", cw_v2(STAGE_W / 2, 560), 28, cw_rgba(1, 1, 1, 0.5f + 0.5f * sinf(G.clock * 6)), CW_ALIGN_CENTER);
  } else {
    ui_text("press A", cw_v2(STAGE_W / 2, 560), 28, cw_rgba(1, 1, 1, 0.5f + 0.5f * sinf(G.clock * 6)), CW_ALIGN_CENTER);
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
  for (int i = 0; i < 3 && d->how[i]; i++) ui_text(d->how[i], cw_v2(r.x + 60, r.y + 160 + i * 44), 30, cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
  for (int p = 0; p < NP; p++) {
    cw_vec2 c = cw_v2(r.x + 130 + p * 180, r.y + 350);
    ui_blob(c, 30, p, 0, 0, G.ready[p] ? 1 + 0.08f * sinf(G.clock * 8) : 1, G.ready[p] ? 1 : 0, p >= G.humans);
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
    ui_blob(cw_v2(x + 190, y + 42), 28, p, 0, 0, 1, G.place[p] == 0 ? 1 : G.place[p] == 3 ? 3 : 0, p >= G.humans);
    char s[48] = "";
    if (cur_def()->score_text) cur_def()->score_text(&G.c, p, s, sizeof s);
    ui_text(p >= G.humans ? "CPU" : PNAME[p], cw_v2(x + 240, y + 8), 26, PCOL[p], CW_ALIGN_LEFT);
    ui_text(s, cw_v2(x + 240, y + 40), 28, cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
    snprintf(s, sizeof s, "+%d", G.round_pts[p]);
    ui_text(s, cw_v2(x + 590, y + 16), 46, cw_rgba(0.6f, 1, 0.6f, 1), CW_ALIGN_RIGHT);
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
  ui_backdrop(G.clock, cw_rgba(0.35f, 0.2f, 0.45f, 1), cw_rgba(0.1f, 0.1f, 0.3f, 1));
  ui_text("TOURNAMENT OVER", cw_v2(STAGE_W / 2, 30), 70, cw_rgba(1, 0.92f, 0.5f, 1), CW_ALIGN_CENTER);
  int order[NP] = { 0, 1, 2, 3 };
  for (int i = 1; i < NP; i++)
    for (int j = i; j > 0; j--) {
      int a = order[j - 1], b = order[j];
      if (G.pos[b] > G.pos[a] || (G.pos[b] == G.pos[a] && G.points[b] > G.points[a])) { order[j] = a; order[j - 1] = b; } else break;
    }
  static const float hts[4] = { 230, 170, 130, 90 };
  static const int slot[4] = { 1, 0, 2, 3 }; /* podium positions: 2nd left, 1st centre */
  (void)slot;
  for (int i = 0; i < NP; i++) {
    int p = order[i];
    float x = 250 + (i == 0 ? 1 : i == 1 ? 0 : i == 2 ? 2 : 3) * 260;
    float h = hts[i] * ease_out((float)G.st_ticks / 50.0f);
    cw_rect r = cw_rect_make(x - 100, 620 - h, 200, h);
    cw_r2d_rect(r, col_mix(PCOL[p], cw_rgba(0, 0, 0, 1), 0.3f));
    cw_r2d_rect(cw_rect_make(r.x, r.y, r.w, 10), PCOL[p]);
    float hop = i == 0 ? fabsf(sinf(G.clock * 5)) * 18 : 0;
    ui_blob(cw_v2(x, 620 - h - 44 - hop), 40, p, 0, 0, 1, i == 0 ? 1 : i == 3 ? 3 : 0, p >= G.humans);
    char s[48];
    snprintf(s, sizeof s, "%d pts", G.points[p]);
    ui_text(s, cw_v2(x, 640 - h * 0.5f - 18), 30, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
  if (G.winner >= 0) {
    char s[64];
    snprintf(s, sizeof s, "%s wins!", G.winner >= G.humans ? "CPU" : PNAME[G.winner]);
    ui_text(s, cw_v2(STAGE_W / 2, 118), 56, PCOL[G.winner], CW_ALIGN_CENTER);
  }
  ui_text("press A", cw_v2(STAGE_W / 2, 672), 26, cw_rgba(1, 1, 1, 0.5f + 0.5f * sinf(G.clock * 6)), CW_ALIGN_CENTER);
}

static void render(void *user, double alpha) {
  (void)user;
  static double last_t; double now_t = cw_time(); float dt_ = last_t > 0 ? fminf((float)(now_t - last_t), 0.1f) : 0.016f; last_t = now_t; G.clock += dt_;
  cw_shell_screen sc = cw_shell_screen_now();
  if (sc == CW_SHELL_PLAY || sc == CW_SHELL_PAUSE) {
    bool in_game = G.state >= S_INTRO && G.state <= S_RESULT;
    bool over3d = in_game && G.scene3d;
    cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = !over3d, .clear_color = cw_rgba(0.05f, 0.05f, 0.1f, 1) });
    if (G.state == S_RESULT || G.state == S_FINISH || G.state == S_PLAY || G.state == S_COUNT || G.state == S_INTRO) fx_update(sc == CW_SHELL_PLAY ? dt_ : 0);
    stage_begin();
    if (in_game) {
      const mg_def *d = cur_def();
      d->draw(&G.c, (float)alpha, G.clock);
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
  });
  u_up = cw_action_find("ui_up"); u_down = cw_action_find("ui_down");
  u_left = cw_action_find("ui_left"); u_right = cw_action_find("ui_right");
  u_ok = cw_action_find("ui_accept"); u_back = cw_action_find("ui_back");
  bus_music = cw_shell_music_bus();
  bus_sfx = cw_shell_sfx_bus();
  register_games();
  SV = cw_shell_game_save();
  for (int i = 0; i < MG_N; i++) if (MG_LIST[i]->unlock_points == 0) SV->unlocked |= 1u << i;
  sfx_init();
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
