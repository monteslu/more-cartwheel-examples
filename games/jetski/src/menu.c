/* The menus: over the live attract loop, a pause in the race, the results. Pad and keyboard; back works from any page. */
#include <stdio.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/quality_menu.h"
#include "cartwheel/render2d.h"
#include "cartwheel/shell.h"
#include "game.h"

enum { P_NONE, P_MAIN, P_CONTROLS, P_AUDIO, P_GRAPHICS, P_PAUSE, P_RESULTS };
typedef struct save_data { int32_t magic, level, difficulty, music, sfx, mode; float best[LEVELS]; } save_data;
_Static_assert(sizeof(save_data) <= 64, "the save region in cw_shell_desc");
#define SAVE_MAGIC 0x4a534b32
static int page = P_MAIN, row, last_state = -1;
/* the championship: every course in turn, points by place. A rider thrown out scores nothing */
static const int POINTS[RACERS] = { 10, 6, 4, 3, 2, 1 };
static int cup_points[RACERS], cup_race, new_best;
static int pending_level = -1, pending_race, pending_wait, saved_level = -1;
static float best[LEVELS];
static cw_action a_up, a_down, a_left, a_right, a_ok, a_back, a_pause;
static cw_font big, small;
static float anim;
int g_difficulty = 1;
int g_mode = MODE_CHAMPIONSHIP;
static int music = 7, sfx = 8;

static save_data *save(void) { return (save_data *)cw_shell_game_save(); }
static void store(void) {
  save_data *s = save();
  if (!s) return;
  s->magic = SAVE_MAGIC, s->level = G.level, s->difficulty = g_difficulty, s->music = music, s->sfx = sfx, s->mode = g_mode;
  memcpy(s->best, best, sizeof best);
  cw_shell_save_commit();
}
static void apply_audio(void) {
  cw_bus_set_volume(cw_shell_music_bus(), (float)music / 10.0f);
  cw_bus_set_volume(cw_shell_sfx_bus(), (float)sfx / 10.0f);
}

void menu_init(void) {
  a_up = cw_action_find("ui_up"), a_down = cw_action_find("ui_down"), a_left = cw_action_find("ui_left"), a_right = cw_action_find("ui_right");
  a_ok = cw_action_find("ui_accept"), a_back = cw_action_find("ui_back");
  a_pause = cw_action_find("race_pause");
  /* this game has its own pause: the shell's would freeze the attract loop and has no restart */
  cw_action sp = cw_action_find("pause");
  while (sp >= 0 && cw_action_unbind(sp, 0)) {}
  big = cw_font_load("fonts/RussoOne-Regular.ttf", NULL);
  small = cw_font_load("fonts/Oswald-VF.ttf", NULL);
  save_data *s = save();
  if (s && s->magic == SAVE_MAGIC) {
    if (s->level > 0 && s->level < LEVELS) { G.level = s->level; view_course_apply(); }
    if (s->difficulty >= 0 && s->difficulty < 3) g_difficulty = s->difficulty;
    music = s->music, sfx = s->sfx;
    if (s->mode == MODE_CHAMPIONSHIP || s->mode == MODE_TIME_TRIAL) g_mode = s->mode;
    memcpy(best, s->best, sizeof best);
  }
  apply_audio();
}

static int page_rows(void) {
  switch (page) {
  case P_MAIN: return 7;
  case P_AUDIO: return 3;
  case P_PAUSE: return 5;
  case P_RESULTS: return 2;
  default: return 1;
  }
}
static void open_page(int p) { page = p, row = 0; }
/* back out of a page: to the row that opened it */
static void back_up(int row_main, int row_pause) {
  bool main = G.state == RACE_ATTRACT;
  page = main ? P_MAIN : P_PAUSE, row = main ? row_main : row_pause;
}

void menu_off(void) { page = P_NONE; }
void menu_course(int level) { if (level >= 0 && level < LEVELS && level != G.level) { G.level = level; view_course_apply(); } }
/* the race holds still under every page opened from the pause */
bool menu_blocks_sim(void) { return page != P_NONE && page != P_RESULTS && G.state != RACE_ATTRACT; }
float menu_best_time(void) { return best[G.level]; }
void menu_debug(void) {
  cw_debug_i32("menu.page", &page);
  cw_debug_i32("menu.row", &row);
  cw_debug_i32("cup.race", &cup_race);
  cw_debug_field("cup.points", cup_points, CW_DBG_I32, RACERS);
  cw_debug_f32("tt.best", &best[0]);
  cw_debug_i32("tt.new_best", &new_best);
}
bool menu_active(void) { return page != P_NONE; }

static const char *const DIFF[3] = { "Easy", "Normal", "Hard" };
static const char *const MODES[2] = { "Championship", "Time trial" };

static void start_race(void) {
  page = P_NONE;
  sim_reset(RACE_GRID);
}
/* a course to load: the page says so for a frame first */
static void go_course(int level, int race) {
  if (level == G.level) { if (race) start_race(); else sim_reset(RACE_ATTRACT); return; }
  pending_level = level, pending_race = race, pending_wait = 2;
}
static void begin(void) {
  if (g_mode == MODE_CHAMPIONSHIP) {
    memset(cup_points, 0, sizeof cup_points), cup_race = 0;
    go_course(0, 1);
    return;
  }
  start_race();
}
/* what the race just finished is worth */
static void race_over(void) {
  if (g_mode == MODE_TIME_TRIAL) {
    new_best = 0;
    float t = G.c[0].finish;
    if (!G.c[0].dq && t > 0 && (best[G.level] <= 0 || t < best[G.level])) best[G.level] = t, new_best = 1, store();
    return;
  }
  int place = 0;
  for (int i = 0; i < RACERS; i++) {
    int who = G.order[i];
    if (who == 0 && G.c[0].dq) continue;
    cup_points[who] += POINTS[place++];
  }
  cup_race++;
}
static bool cup_done(void) { return g_mode == MODE_CHAMPIONSHIP && cup_race >= LEVELS; }

void menu_tick(float dt) {
  anim += dt;
  if (pending_level >= 0) {
    if (--pending_wait > 0) return;
    G.level = pending_level;
    pending_level = -1;
    view_course_apply();
    if (saved_level >= 0) { saved_level = -1; store(); }
    if (pending_race) start_race(); else sim_reset(RACE_ATTRACT);
    return;
  }
  if (G.state == RACE_DONE && last_state != RACE_DONE) { race_over(); open_page(P_RESULTS); }
  if (G.state == RACE_ATTRACT && last_state != RACE_ATTRACT && page == P_NONE) open_page(P_MAIN);
  last_state = G.state;
  if (page == P_NONE) {
    if (a_pause >= 0 && cw_action_just_pressed(0, a_pause) && (G.state == RACE_ON || G.state == RACE_GRID)) open_page(P_PAUSE);
    return;
  }
  if (page == P_GRAPHICS) {
    cw_quality_menu_update();
    if (!cw_quality_menu_is_open()) back_up(6, 0);
    return;
  }
  bool up = cw_action_just_pressed(0, a_up), down = cw_action_just_pressed(0, a_down), left = cw_action_just_pressed(0, a_left), right = cw_action_just_pressed(0, a_right);
  bool ok = cw_action_just_pressed(0, a_ok), back = cw_action_just_pressed(0, a_back) || (a_pause >= 0 && cw_action_just_pressed(0, a_pause) && page == P_PAUSE);
  int n = page_rows();
  if (up) row = (row + n - 1) % n;
  if (down) row = (row + 1) % n;
  int dx = (right ? 1 : 0) - (left ? 1 : 0);
  switch (page) {
  case P_MAIN:
    if (row == 1 && (dx || ok)) { g_mode = !g_mode; store(); }
    if (row == 2 && g_mode == MODE_TIME_TRIAL && (dx || ok)) { int l = (G.level + LEVELS + (dx ? dx : 1)) % LEVELS; if (l != G.level) { go_course(l, 0); saved_level = l; } }
    if (row == 3 && (dx || ok)) { g_difficulty = (g_difficulty + 3 + (dx ? dx : 1)) % 3; store(); }
    if (ok) {
      if (row == 0) begin();
      else if (row == 4) open_page(P_CONTROLS);
      else if (row == 5) open_page(P_AUDIO);
      else if (row == 6) { page = P_GRAPHICS; cw_quality_menu_open(); }
    }
    break;
  case P_CONTROLS:
    if (ok || back) back_up(4, 2);
    break;
  case P_AUDIO:
    if (row == 0 && dx) { music = music + dx < 0 ? 0 : music + dx > 10 ? 10 : music + dx; apply_audio(); store(); }
    if (row == 1 && dx) { sfx = sfx + dx < 0 ? 0 : sfx + dx > 10 ? 10 : sfx + dx; apply_audio(); store(); }
    if ((ok && row == 2) || back) back_up(5, 3);
    break;
  case P_PAUSE:
    if (back) { page = P_NONE; break; }
    if (ok) {
      if (row == 0) page = P_NONE;
      else if (row == 1) start_race();
      else if (row == 2) open_page(P_CONTROLS);
      else if (row == 3) open_page(P_AUDIO);
      else { sim_reset(RACE_ATTRACT); open_page(P_MAIN); }
    }
    break;
  case P_RESULTS:
    if (ok) {
      if (row == 0 && cup_done()) { begin(); }
      else if (row == 0 && g_mode == MODE_CHAMPIONSHIP) go_course(cup_race, 1);
      else if (row == 0) start_race();
      else { sim_reset(RACE_ATTRACT); open_page(P_MAIN); }
    }
    else if (back) { sim_reset(RACE_ATTRACT); open_page(P_MAIN); }
    break;
  default: break;
  }
  (void)dt;
}

static void text(cw_font f, const char *s, float x, float y, float size, cw_color c, float align) {
  cw_text_style st = { .size = size, .color = c, .outline_width = size * 0.04f, .outline_color = cw_rgba(0, 0, 0, 0.5f) };
  if (align != 0) { cw_text_metrics m = cw_font_measure(f, s, &st); x -= m.width * align; }
  cw_r2d_text(f, s, cw_v2(x, y), &st);
}

/* a row: a dark bar that lights up, its label, its value */
static void item(float x, float y, float w, float u, const char *label, const char *value, bool sel) {
  float h = 58 * u;
  cw_r2d_round_rect(cw_rect_make(x, y, w, h), 8 * u, 8, sel ? cw_rgba(0.98f, 0.58f, 0.14f, 0.92f) : cw_rgba(0.03f, 0.07f, 0.11f, 0.62f));
  cw_color tc = sel ? cw_rgba(0.04f, 0.04f, 0.05f, 1) : cw_rgba(1, 1, 1, 0.92f);
  cw_text_style st = { .size = 36 * u, .color = tc };
  cw_r2d_text(small, label, cw_v2(x + 22 * u, y + 7 * u), &st);
  if (value) {
    char v[64];
    snprintf(v, sizeof v, sel ? "< %s >" : "%s", value);
    cw_text_metrics m = cw_font_measure(small, v, &st);
    cw_r2d_text(small, v, cw_v2(x + w - m.width - 22 * u, y + 7 * u), &st);
  }
}

void menu_draw(void) {
  if (pending_level >= 0) {
    cw_vec2 s = cw_r2d_canvas_size();
    cw_r2d_rect(cw_rect_make(0, 0, s.x, s.y), cw_rgba(0, 0.02f, 0.05f, 0.8f));
    text(big, COURSES[pending_level]->name, s.x * 0.5f, s.y * 0.45f, 80 * s.y / 1080.0f, cw_rgba(1, 1, 1, 1), 0.5f);
    return;
  }
  if (page == P_NONE) return;
  cw_vec2 sz = cw_r2d_canvas_size();
  float w = sz.x, h = sz.y, u = h / 1080.0f, px = w * 0.07f, pw = 560 * u, y = h * 0.34f, step = 68 * u;
  cw_color white = cw_rgba(1, 1, 1, 0.97f), dim = cw_rgba(1, 1, 1, 0.75f);
  char buf[64];
  if (page == P_GRAPHICS) {
    cw_quality_menu_style st = { .font = small };
    cw_quality_menu_draw(&st);
    return;
  }
  if (G.state == RACE_ATTRACT) {
    text(big, "JETSKI", px, h * 0.11f, 124 * u, white, 0);
    text(small, "WAVE RACING", px + 6 * u, h * 0.24f, 30 * u, dim, 0);
  }
  if (page == P_PAUSE || ((page == P_AUDIO || page == P_CONTROLS) && G.state != RACE_ATTRACT) || page == P_RESULTS) cw_r2d_rect(cw_rect_make(0, 0, w, h), cw_rgba(0, 0.02f, 0.05f, 0.42f));
  switch (page) {
  case P_MAIN:
    item(px, y, pw, u, "Start", NULL, row == 0);
    item(px, y + step, pw, u, "Mode", MODES[g_mode], row == 1);
    item(px, y + step * 2, pw, u, "Course", g_mode == MODE_CHAMPIONSHIP ? "All, in turn" : DEF->name, row == 2);
    item(px, y + step * 3, pw, u, "Difficulty", DIFF[g_difficulty], row == 3);
    item(px, y + step * 4, pw, u, "Controls", NULL, row == 4);
    item(px, y + step * 5, pw, u, "Audio", NULL, row == 5);
    item(px, y + step * 6, pw, u, "Graphics", NULL, row == 6);
    if (g_mode == MODE_TIME_TRIAL && best[G.level] > 0) {
      snprintf(buf, sizeof buf, "Best on %s: %d:%05.2f", DEF->name, (int)(best[G.level] / 60), best[G.level] - 60.0f * (float)(int)(best[G.level] / 60));
      text(small, buf, px + 6 * u, y + step * 7 + 4 * u, 30 * u, cw_rgba(1, 0.78f, 0.25f, 1), 0);
    }
    text(small, "Up and down to choose, left and right to change, A or Enter to accept", px, h * 0.92f, 24 * u, dim, 0);
    break;
  case P_CONTROLS: {
    static const char *const L[][2] = { { "Steer and lean", "Left stick, d-pad, arrows" }, { "Throttle", "A, right trigger, Space" }, { "Lean back: tighter carve", "Stick down, S" }, { "Lean back, then throw forward at a crest", "Jump higher" }, { "In the air: barrel roll", "X, K" }, { "In the air, stick back: back flip", "X, K" }, { "Pause", "Start, Esc" } };
    cw_r2d_round_rect(cw_rect_make(px, y, 980 * u, 530 * u), 10 * u, 8, cw_rgba(0.03f, 0.07f, 0.11f, 0.72f));
    for (int i = 0; i < 7; i++) {
      text(small, L[i][0], px + 26 * u, y + (20 + (float)i * 62) * u, 34 * u, white, 0);
      text(small, L[i][1], px + 954 * u, y + (20 + (float)i * 62) * u, 34 * u, cw_rgba(1.0f, 0.78f, 0.3f, 1), 1);
    }
    text(small, "Pass each buoy on the side its arrow shows: five in a row is your top speed. A miss loses them all.", px + 26 * u, y + 462 * u, 26 * u, dim, 0);
    break;
  }
  case P_AUDIO:
    snprintf(buf, sizeof buf, "%d", music);
    item(px, y, pw, u, "Music", buf, row == 0);
    snprintf(buf, sizeof buf, "%d", sfx);
    item(px, y + step, pw, u, "Effects", buf, row == 1);
    item(px, y + step * 2, pw, u, "Back", NULL, row == 2);
    break;
  case P_PAUSE:
    text(big, "PAUSED", w * 0.5f, h * 0.2f, 96 * u, white, 0.5f);
    px = w * 0.5f - pw * 0.5f;
    item(px, y, pw, u, "Resume", NULL, row == 0);
    item(px, y + step, pw, u, "Restart race", NULL, row == 1);
    item(px, y + step * 2, pw, u, "Controls", NULL, row == 2);
    item(px, y + step * 3, pw, u, "Audio", NULL, row == 3);
    item(px, y + step * 4, pw, u, "Quit to title", NULL, row == 4);
    break;
  case P_RESULTS: {
    static const char *const ord[] = { "1st", "2nd", "3rd", "4th", "5th", "6th" };
    if (g_mode == MODE_TIME_TRIAL && !G.c[0].dq) {
      float t = G.c[0].finish;
      snprintf(buf, sizeof buf, "%d:%05.2f", (int)(t / 60), t - 60.0f * (float)(int)(t / 60));
      text(big, buf, w * 0.5f, h * 0.14f, 130 * u, cw_rgba(1, 0.78f, 0.25f, 1), 0.5f);
      text(small, new_best ? "A new best on this course" : "Not your best", w * 0.5f, h * 0.3f, 42 * u, white, 0.5f);
      if (best[G.level] > 0) { snprintf(buf, sizeof buf, "Best %d:%05.2f", (int)(best[G.level] / 60), best[G.level] - 60.0f * (float)(int)(best[G.level] / 60)); text(small, buf, w * 0.5f, h * 0.37f, 36 * u, dim, 0.5f); }
      px = w * 0.5f - pw * 0.5f;
      item(px, h * 0.6f, pw, u, "Try again", NULL, row == 0);
      item(px, h * 0.6f + step, pw, u, "Back to title", NULL, row == 1);
      break;
    }
    if (G.c[0].dq) text(big, G.c[0].dq == 1 ? "FIVE MISSES" : "LEFT THE COURSE", w * 0.5f, h * 0.11f, 90 * u, cw_rgba(1, 0.4f, 0.15f, 1), 0.5f);
    else text(big, ord[G.c[0].place - 1 < 0 ? 0 : G.c[0].place - 1], w * 0.5f, h * 0.09f, 130 * u, cw_rgba(1, 0.78f, 0.25f, 1), 0.5f);
    float tx = w * 0.5f - 300 * u, ty = h * 0.27f;
    /* the race's order, each rider's time, and the championship's points so far */
    for (int i = 0; i < RACERS && g_mode == MODE_CHAMPIONSHIP; i++) {
      const craft *c = &G.c[G.order[i]];
      snprintf(buf, sizeof buf, "%s  %s", ord[i], G.order[i] == 0 ? "You" : RIDER_NAMES[G.order[i]]);
      text(small, buf, tx, ty + (float)i * 46 * u, 36 * u, G.order[i] == 0 ? cw_rgba(1, 0.78f, 0.25f, 1) : white, 0);
      if (c->finish > 0) { snprintf(buf, sizeof buf, "%d:%05.2f", (int)(c->finish / 60), c->finish - 60.0f * (float)(int)(c->finish / 60)); text(small, buf, tx + 470 * u, ty + (float)i * 46 * u, 36 * u, white, 1); }
      snprintf(buf, sizeof buf, "%d pts", cup_points[G.order[i]]);
      text(small, buf, tx + 620 * u, ty + (float)i * 46 * u, 36 * u, cw_rgba(1, 0.78f, 0.25f, 1), 1);
    }
    px = w * 0.5f - pw * 0.5f;
    if (cup_done()) {
      int top = 0;
      for (int i = 1; i < RACERS; i++) if (cup_points[i] > cup_points[top]) top = i;
      snprintf(buf, sizeof buf, top == 0 ? "You win the championship" : "%s wins the championship", RIDER_NAMES[top]);
      text(small, buf, w * 0.5f, h * 0.54f, 40 * u, white, 0.5f);
    }
    item(px, h * 0.6f, pw, u, g_mode == MODE_TIME_TRIAL ? "Try again" : cup_done() ? "New championship" : "Next race", NULL, row == 0);
    item(px, h * 0.6f + step, pw, u, "Back to title", NULL, row == 1);
    break;
  }
  default: break;
  }
}
