/* The menus: over the live attract loop, a pause in the race, the results. Pad and keyboard; back works from any page. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/prompt.h"
#include "cartwheel/quality_menu.h"
#include "cartwheel/render2d.h"
#include "cartwheel/shell.h"
#include "game.h"

enum { P_NONE, P_MAIN, P_CONTROLS, P_AUDIO, P_GRAPHICS, P_PAUSE, P_RESULTS };
typedef struct save_data { int32_t magic, level, difficulty, music, sfx, mode; float best[LEVELS]; int32_t craft; } save_data; /* (craft last: a save from before it reads 0, the sit-down) */
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
  s->magic = SAVE_MAGIC, s->level = G.level, s->difficulty = g_difficulty, s->music = music, s->sfx = sfx, s->mode = g_mode, s->craft = g_craft;
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
    if (s->craft == CRAFT_SIT || s->craft == CRAFT_STAND) g_craft = s->craft;
    memcpy(best, s->best, sizeof best);
  }
  apply_audio();
}

static int page_rows(void) {
  switch (page) {
  case P_MAIN: return 8;
  case P_AUDIO: return 5;
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
static const char *const CRAFTS[2] = { "Sit-down", "Stand-up" };

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
    if (!cw_quality_menu_is_open()) back_up(7, 0);
    return;
  }
  bool up = cw_action_just_pressed(0, a_up), down = cw_action_just_pressed(0, a_down), left = cw_action_just_pressed(0, a_left), right = cw_action_just_pressed(0, a_right);
  bool ok = cw_action_just_pressed(0, a_ok), back = cw_action_just_pressed(0, a_back) || (a_pause >= 0 && cw_action_just_pressed(0, a_pause) && page == P_PAUSE);
  int n = page_rows();
  if (up) row = (row + n - 1) % n;
  if (down) row = (row + 1) % n;
  int dx = (right ? 1 : 0) - (left ? 1 : 0);
  { extern int g_menu_sfx; if (up || down || dx) g_menu_sfx = 1; if (ok) g_menu_sfx = 2; if (back) g_menu_sfx = 3; }
  switch (page) {
  case P_MAIN:
    if (row == 1 && (dx || ok)) { g_mode = !g_mode; store(); }
    if (row == 2 && g_mode == MODE_TIME_TRIAL && (dx || ok)) { int l = (G.level + LEVELS + (dx ? dx : 1)) % LEVELS; if (l != G.level) { go_course(l, 0); saved_level = l; } }
    if (row == 3 && (dx || ok)) { g_difficulty = (g_difficulty + 3 + (dx ? dx : 1)) % 3; store(); }
    /* the craft: every racer rides the one chosen, so the race stays even */
    if (row == 4 && (dx || ok)) { g_craft = !g_craft; store(); }
    if (ok) {
      if (row == 0) begin();
      else if (row == 5) open_page(P_CONTROLS);
      else if (row == 6) open_page(P_AUDIO);
      else if (row == 7) { page = P_GRAPHICS; cw_quality_menu_open(); }
    }
    break;
  case P_CONTROLS:
    if (ok || back) back_up(5, 2);
    break;
  case P_AUDIO:
    if (row == 0 && dx) { music = music + dx < 0 ? 0 : music + dx > 10 ? 10 : music + dx; apply_audio(); store(); }
    if (row == 1 && dx) { sfx = sfx + dx < 0 ? 0 : sfx + dx > 10 ? 10 : sfx + dx; apply_audio(); store(); }
    /* Text size (100, 125, 150, 200%) and Text background: left / right step them, stopping at the ends; accept goes round.
     * Kept in the shell's save with the other settings (store commits it) */
    if (row == 2 && (dx || ok)) {
      if (dx < 0 && cw_text_scale() > 1.01f) cw_text_scale_cycle_back();
      else if ((dx > 0 && cw_text_scale() < 1.99f) || ok) cw_text_scale_cycle();
      store();
    }
    if (row == 3 && (dx || ok)) {
      int b = (int)cw_text_backdrop_get();
      cw_text_backdrop_set((cw_text_backdrop)(ok ? (b + 1) % 3 : b + dx < 0 ? 0 : b + dx > 2 ? 2 : b + dx));
      store();
    }
    if ((ok && row == 4) || back) back_up(6, 3);
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
    if (dx) row = (row + 1) % 2; /* the two choices can stand side by side (a large text size) */
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

/* Text is laid out from its measured size, so every page holds at the player's text scale (cartwheel/render2d.h "text
 * size"): rows are as tall as their text draws, panels as wide as their widest line, and a page that runs out of
 * height scrolls (the menu) or moves up over the title (the long pages). */
static cw_text_style tstyle(float size, cw_color c) {
  return (cw_text_style){ .size = size, .color = c, .outline_width = size * 0.04f, .outline_color = cw_rgba(0, 0, 0, 0.5f) };
}
static cw_text_metrics measure(cw_font f, const char *s, float size) {
  cw_text_style st = tstyle(size, cw_rgba(1, 1, 1, 1));
  return cw_font_measure(f, s, &st);
}
/* draws with the box's top-left at (x, y) moved by align (0 left, 0.5 centre, 1 right); returns the box's height */
static float text(cw_font f, const char *s, float x, float y, float size, cw_color c, float align) {
  cw_text_style st = tstyle(size, c);
  cw_text_metrics m = cw_font_measure(f, s, &st);
  x -= m.width * align;
  cw_r2d_text(f, s, cw_v2(x, y), &st);
  return m.height;
}
/* text with button prompts in it (cartwheel/prompt.h markup: "{throttle}"), for every seat: align 0 left, 0.5 centre, 1 right */
static void ptext(cw_font f, const char *s, float x, float y, float size, cw_color c, float align) {
  cw_prompt_text(f, s, -1, cw_v2(x, y), size, c, align == 0 ? CW_ALIGN_LEFT : align == 1 ? CW_ALIGN_RIGHT : CW_ALIGN_CENTER);
}
static void tri(cw_vec2 a, cw_vec2 b, cw_vec2 c, cw_color col) {
  cw_vertex2d v[3] = { { a, { 0, 0 }, col }, { b, { 0, 0 }, col }, { c, { 0, 0 }, col } };
  static const uint32_t idx[3] = { 0, 1, 2 };
  cw_r2d_triangles((cw_texture){ 0 }, v, 3, idx, 3);
}

/* this frame's rows: the label size, how much the text scale grows it, the bar's height and the step to the next */
static float row_size, row_k, row_h, row_step, U;
static void rows_setup(float u) {
  U = u, row_size = 36 * u;
  row_k = cw_text_drawn_size(small, row_size) / row_size;
  row_h = 58 * u * row_k, row_step = row_h + 10 * u;
}
/* the width a row needs for its label and its value (as drawn selected, "< value >") */
static float row_need(const char *label, const char *value) {
  float need = 44 * U * row_k + measure(small, label, row_size).width;
  if (value) {
    char v[64];
    snprintf(v, sizeof v, "< %s >", value);
    need += 40 * U * row_k + measure(small, v, row_size).width;
  }
  return need;
}
/* a row: a dark bar that lights up, its label, its value */
static void item(float x, float y, float w, const char *label, const char *value, bool sel) {
  cw_r2d_round_rect(cw_rect_make(x, y, w, row_h), 8 * U, 8, sel ? cw_rgba(0.98f, 0.58f, 0.14f, 0.92f) : cw_rgba(0.03f, 0.07f, 0.11f, 0.62f));
  cw_color tc = sel ? cw_rgba(0.04f, 0.04f, 0.05f, 1) : cw_rgba(1, 1, 1, 0.92f);
  cw_text_style st = { .size = row_size, .color = tc, .own_backdrop = true };
  float ty = y + 7 * U * row_k, pad = 22 * U * row_k;
  cw_r2d_text(small, label, cw_v2(x + pad, ty), &st);
  if (value) {
    char v[64];
    snprintf(v, sizeof v, sel ? "< %s >" : "%s", value);
    cw_text_metrics m = cw_font_measure(small, v, &st);
    cw_r2d_text(small, v, cw_v2(x + w - m.width - pad, ty), &st);
  }
}
/* a page's rows from y, at most down to y_end: every row when they fit, otherwise as many as fit, scrolled to keep the
 * selected one in view, with an arrow where more are hidden. Returns the y under the last row drawn */
typedef struct row_def { const char *label, *value; } row_def;
static float row_width(const row_def *r, int n, float min_w, float max_w) {
  float pw = min_w;
  for (int i = 0; i < n; i++) pw = fmaxf(pw, row_need(r[i].label, r[i].value));
  return fminf(pw, max_w);
}
static float rows_draw(float x, float y, float y_end, float pw, const row_def *r, int n, int sel) {
  int fit = (int)((y_end - y + 10 * U) / row_step);
  if (fit < 1) fit = 1;
  int first = 0;
  if (fit < n) {
    first = sel - fit / 2;
    if (first < 0) first = 0;
    if (first > n - fit) first = n - fit;
  } else fit = n;
  cw_color arrow = cw_rgba(1, 1, 1, 0.8f);
  float a = 12 * U * row_k, cx = x + pw * 0.5f;
  if (first > 0) tri(cw_v2(cx - a, y - 4 * U), cw_v2(cx + a, y - 4 * U), cw_v2(cx, y - 4 * U - a), arrow);
  for (int i = 0; i < fit; i++) item(x, y + row_step * (float)i, pw, r[first + i].label, r[first + i].value, first + i == sel);
  float end = y + row_step * (float)fit - 10 * U;
  if (first + fit < n) tri(cw_v2(cx - a, end + 4 * U), cw_v2(cx + a, end + 4 * U), cw_v2(cx, end + 4 * U + a), arrow);
  return end;
}

/* the title over the attract loop: the logo is art (drawn as it is at every text scale); returns the y under it */
static float title(float px, float h, float u, bool draw) {
  cw_color white = cw_rgba(1, 1, 1, 0.97f), dim = cw_rgba(1, 1, 1, 0.75f);
  float sub_y = h * 0.24f, sub_h = measure(small, "WAVE RACING", 30 * u).height;
  if (draw) {
    cw_text_style logo = tstyle(124 * u, white);
    logo.fixed_size = true;
    cw_r2d_text(big, "JETSKI", cw_v2(px, h * 0.11f), &logo);
    text(small, "WAVE RACING", px + 6 * u, sub_y, 30 * u, dim, 0);
  }
  return sub_y + sub_h;
}
/* the hint line, at the foot of the page: its parts on one line when they fit, otherwise one under another. Returns its top */
static float hint(const char *const *parts, int n, float x, float w_avail, float bottom, float size, cw_color c) {
  float gap = 2.2f * cw_text_drawn_size(small, size), total = 0, lh = 0;
  for (int i = 0; i < n; i++) { cw_vec2 s = cw_prompt_text_size(small, parts[i], -1, size); total += s.x + (i ? gap : 0); lh = fmaxf(lh, s.y); }
  if (total <= w_avail) {
    float top = bottom - lh, cx = x;
    for (int i = 0; i < n; i++) { ptext(small, parts[i], cx, top, size, c, 0); cx += cw_prompt_text_size(small, parts[i], -1, size).x + gap; }
    return top;
  }
  float top = bottom - lh * (float)n;
  for (int i = 0; i < n; i++) ptext(small, parts[i], x, top + lh * (float)i, size, c, 0);
  return top;
}
static void fmt_time(char *buf, size_t cap, const char *pre, float t) {
  snprintf(buf, cap, "%s%d:%05.2f", pre, (int)(t / 60), t - 60.0f * (float)(int)(t / 60));
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
  float w = sz.x, h = sz.y, u = h / 1080.0f, px = w * 0.07f, y = h * 0.34f;
  cw_color white = cw_rgba(1, 1, 1, 0.97f), dim = cw_rgba(1, 1, 1, 0.75f), gold = cw_rgba(1, 0.78f, 0.25f, 1);
  char buf[64];
  if (page == P_GRAPHICS) {
    cw_quality_menu_style st = { .font = small };
    cw_quality_menu_draw(&st);
    return;
  }
  rows_setup(u);
  bool attract = G.state == RACE_ATTRACT;
  /* the widest a page may grow: the screen less a margin, the left margin shrinking first */
  float margin = 30 * u, max_w = w - 2 * margin;
  if (page == P_PAUSE || ((page == P_AUDIO || page == P_CONTROLS) && !attract) || page == P_RESULTS) cw_r2d_rect(cw_rect_make(0, 0, w, h), cw_rgba(0, 0.02f, 0.05f, 0.42f));
  switch (page) {
  case P_MAIN: {
    float top = title(px, h, u, attract);
    y = fmaxf(y, top + 24 * u);
    char course[48];
    snprintf(course, sizeof course, "%s", g_mode == MODE_CHAMPIONSHIP ? "All, in turn" : DEF->name);
    const row_def r[8] = { { "Start", NULL }, { "Mode", MODES[g_mode] }, { "Course", course }, { "Difficulty", DIFF[g_difficulty] }, { "Craft", CRAFTS[g_craft] }, { "Controls", NULL }, { "Audio and text", NULL }, { "Graphics", NULL } };
    static const char *const parts[3] = { "{ui_up|ui_down} choose", "{ui_left|ui_right} change", "{ui_accept} accept" };
    float hs = 28 * u, ht = hint(parts, 3, px, w - px - margin, h - 40 * u, hs, dim);
    float pw = row_width(r, 8, 560 * u, max_w), x = px + pw > w - margin ? w - margin - pw : px;
    float best_h = 0;
    bool show_best = g_mode == MODE_TIME_TRIAL && best[G.level] > 0;
    if (show_best) best_h = measure(small, "Best", 30 * u).height + 4 * u;
    float end = rows_draw(x, y, ht - 20 * u - best_h, pw, r, 8, row);
    if (show_best) {
      char pre[48];
      snprintf(pre, sizeof pre, "Best on %s: ", DEF->name);
      fmt_time(buf, sizeof buf, pre, best[G.level]);
      text(small, buf, x + 6 * u, end + 4 * u + 10 * u, 30 * u, gold, 0);
    }
    break;
  }
  case P_CONTROLS: {
    /* the page for the craft chosen: the stand-up's tricks are its own */
    enum { NL = 8 };
    static const char *const LS[2][NL][2] = {
      { { "Steer and lean", "{steer_left|steer_right}" }, { "Throttle", "{throttle}" }, { "Lean back: tighter carve", "{lean_back}" }, { "Lean back, then throw forward at a crest", "Jump higher" }, { "In the air: barrel roll", "{trick}" }, { "In the air, stick back or forward: flips", "{lean_back|lean_fwd} + {trick}" }, { "In the air: kick out", "{kick}" }, { "Pause", "{race_pause}" } },
      { { "Steer and lean: carve it tight", "{steer_left|steer_right}" }, { "Throttle: it planes at speed", "{throttle}" }, { "On the water: submarine", "{trick}" }, { "On the water: pole stand", "{kick}" }, { "In the air: barrel roll", "{trick}" }, { "In the air, stick back: back flip", "{lean_back} + {trick}" }, { "In the air, stick forward: flat spin", "{lean_fwd} + {trick}" }, { "In the air: superman", "{kick}" } } };
    const char *const (*L)[2] = LS[g_craft];
    const char *FOOT = g_craft == CRAFT_STAND ? "Stand-up: quick and tight, slower on the straights, and easy to fall off. Pass each buoy on the side its arrow shows." : "Pass each buoy on the side its arrow shows: five in a row is your top speed. A miss loses them all.";
    float ts = 34 * u, k = cw_text_drawn_size(small, ts) / ts, pad = 26 * u * k, lh = measure(small, "Ag", ts).height;
    float lw = 0, vw = 0, vh = lh;
    for (int i = 0; i < NL; i++) { lw = fmaxf(lw, measure(small, L[i][0], ts).width); cw_vec2 v = cw_prompt_text_size(small, L[i][1], -1, ts); vw = fmaxf(vw, v.x); vh = fmaxf(vh, v.y); }
    float pw = fmaxf(980 * u, pad * 2 + lw + 40 * u * k + vw);
    bool two_lines = pw > max_w; /* too narrow for a label and its control side by side: the control goes under its label */
    if (two_lines) pw = max_w;
    float step = fmaxf(62 * u, fmaxf(lh, vh) + 8 * u * k), row2 = two_lines ? vh : 0;
    cw_text_style fs = tstyle(26 * u, dim);
    fs.wrap_width = pw - pad * 2;
    float foot_h = cw_font_measure(small, FOOT, &fs).height;
    /* taller than the screen: the rows close up (the font's line box has room to spare), never below the glyphs' own height */
    { float rest = 20 * u * k + NL * row2 + 10 * u * k + foot_h + 24 * u * k, room = (h - 2 * margin - rest) / NL;
      if (step > room) step = fmaxf(room, fmaxf(lh * 0.8f, vh)); }
    float ph = 20 * u * k + NL * (step + row2) + 10 * u * k + foot_h + 24 * u * k;
    float x = px + pw > w - margin ? w - margin - pw : px;
    float py = fminf(y, h - margin - ph);
    if (py < margin) py = margin;
    if (attract && py >= title(px, h, u, false) + 12 * u) title(px, h, u, true);
    cw_r2d_round_rect(cw_rect_make(x, py, pw, ph), 10 * u, 8, cw_rgba(0.03f, 0.07f, 0.11f, 0.72f));
    float ry = py + 20 * u * k;
    for (int i = 0; i < NL; i++) {
      text(small, L[i][0], x + pad, ry, ts, white, 0);
      ptext(small, L[i][1], x + pw - pad, two_lines ? ry + lh : ry, ts, cw_rgba(1.0f, 0.78f, 0.3f, 1), 1);
      ry += step + row2;
    }
    cw_r2d_text(small, FOOT, cw_v2(x + pad, ry + 10 * u * k), &fs);
    break;
  }
  case P_AUDIO: {
    char m[8], s[8];
    snprintf(m, sizeof m, "%d", music);
    snprintf(s, sizeof s, "%d", sfx);
    const row_def r[5] = { { "Music", m }, { "Effects", s }, { "Text size", cw_text_scale_label() }, { "Text background", cw_text_backdrop_label() }, { "Back", NULL } };
    float top = title(px, h, u, attract);
    if (attract) y = fmaxf(y, top + 24 * u);
    float pw = row_width(r, 5, 560 * u, max_w), x = px + pw > w - margin ? w - margin - pw : px;
    rows_draw(x, y, h - margin, pw, r, 5, row);
    break;
  }
  case P_PAUSE: {
    float th = text(big, "PAUSED", w * 0.5f, h * 0.2f, 96 * u, white, 0.5f);
    const row_def r[5] = { { "Resume", NULL }, { "Restart race", NULL }, { "Controls", NULL }, { "Audio and text", NULL }, { "Quit to title", NULL } };
    float pw = row_width(r, 5, 560 * u, max_w);
    rows_draw(w * 0.5f - pw * 0.5f, fmaxf(y, h * 0.2f + th + 12 * u), h - margin, pw, r, 5, row);
    break;
  }
  case P_RESULTS: {
    static const char *const ord[] = { "1st", "2nd", "3rd", "4th", "5th", "6th" };
    row_def r[2] = { { g_mode == MODE_TIME_TRIAL ? "Try again" : cup_done() ? "New championship" : "Next race", NULL }, { "Back to title", NULL } };
    float pw = row_width(r, 2, 560 * u, (max_w - 20 * u) * 0.5f), yy;
    if (g_mode == MODE_TIME_TRIAL && !G.c[0].dq) {
      float t = G.c[0].finish;
      fmt_time(buf, sizeof buf, "", t);
      yy = h * 0.14f + text(big, buf, w * 0.5f, h * 0.14f, 130 * u, gold, 0.5f);
      yy = fmaxf(h * 0.3f, yy + 8 * u);
      yy += text(small, new_best ? "A new best on this course" : "Not your best", w * 0.5f, yy, 42 * u, white, 0.5f);
      if (best[G.level] > 0) { fmt_time(buf, sizeof buf, "Best ", best[G.level]); yy = fmaxf(h * 0.37f, yy + 4 * u); yy += text(small, buf, w * 0.5f, yy, 36 * u, dim, 0.5f); }
    } else {
      if (G.c[0].dq) yy = h * 0.11f + text(big, G.c[0].dq == 1 ? "FIVE MISSES" : "LEFT THE COURSE", w * 0.5f, h * 0.11f, 90 * u, cw_rgba(1, 0.4f, 0.15f, 1), 0.5f);
      else yy = h * 0.09f + text(big, ord[G.c[0].place - 1 < 0 ? 0 : G.c[0].place - 1], w * 0.5f, h * 0.09f, 130 * u, gold, 0.5f);
      /* the race's order, each rider's time, and the championship's points so far: columns as wide as their widest entry */
      if (g_mode == MODE_CHAMPIONSHIP) {
        float ts = 36 * u, k = cw_text_drawn_size(small, ts) / ts, gap = 40 * u * k, nw = 0, tw = 0, pwid = 0;
        char names[RACERS][48], times[RACERS][16], pts[RACERS][16];
        for (int i = 0; i < RACERS; i++) {
          const craft *c = &G.c[G.order[i]];
          snprintf(names[i], sizeof names[i], "%s  %s", ord[i], G.order[i] == 0 ? "You" : RIDER_NAMES[G.order[i]]);
          times[i][0] = 0;
          if (c->finish > 0) fmt_time(times[i], sizeof times[i], "", c->finish);
          snprintf(pts[i], sizeof pts[i], "%d pts", cup_points[G.order[i]]);
          nw = fmaxf(nw, measure(small, names[i], ts).width), tw = fmaxf(tw, measure(small, times[i], ts).width), pwid = fmaxf(pwid, measure(small, pts[i], ts).width);
        }
        float time_r = fmaxf(470 * u, nw + gap + tw), pts_r = time_r + fmaxf(150 * u, gap + pwid), tx = w * 0.5f - pts_r * 0.5f;
        float ty = fmaxf(h * 0.27f, yy + 10 * u), step = 46 * u * k, tlh = measure(small, "Ag", ts).height;
        /* the rows close up (never tighter than the text's own size) when the table, the champion's line and the two
         * choices side by side would run off the foot of the page */
        float room = h - margin - row_h - 16 * u - (cup_done() ? measure(small, "Ag", 40 * u).height + 8 * u : 0) - ty - tlh;
        if (step * (RACERS - 1) > room) step = fmaxf(room / (RACERS - 1), cw_text_drawn_size(small, ts));
        for (int i = 0; i < RACERS; i++) {
          float ry = ty + (float)i * step;
          text(small, names[i], tx, ry, ts, G.order[i] == 0 ? gold : white, 0);
          if (times[i][0]) text(small, times[i], tx + time_r, ry, ts, white, 1);
          text(small, pts[i], tx + pts_r, ry, ts, gold, 1);
        }
        yy = ty + (float)(RACERS - 1) * step + measure(small, "Ag", ts).height;
      }
      if (cup_done()) {
        int top = 0;
        for (int i = 1; i < RACERS; i++) if (cup_points[i] > cup_points[top]) top = i;
        snprintf(buf, sizeof buf, top == 0 ? "You win the championship" : "%s wins the championship", RIDER_NAMES[top]);
        yy = fmaxf(h * 0.54f, yy + 8 * u);
        yy += text(small, buf, w * 0.5f, yy, 40 * u, white, 0.5f);
      }
    }
    /* the two choices under it all: one over the other, or side by side when the page has no room left for that */
    float iy = fmaxf(h * 0.6f, yy + 16 * u);
    if (iy + row_step + row_h <= h - margin) rows_draw(w * 0.5f - pw * 0.5f, iy, h - margin, pw, r, 2, row);
    else {
      iy = fminf(iy, h - margin - row_h);
      item(w * 0.5f - pw - 10 * u, iy, pw, r[0].label, r[0].value, row == 0);
      item(w * 0.5f + 10 * u, iy, pw, r[1].label, r[1].value, row == 1);
    }
    break;
  }
  default: break;
  }
}
