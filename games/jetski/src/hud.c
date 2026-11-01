/* The HUD: speed, place, lap, the meter of arrows that sets top speed, the next buoy's side. Drawn in the canvas's own
 * size; everything scales with its height. */
#include <math.h>
#include <stdio.h>

#include "cartwheel/platform.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "game.h"

static cw_font big, small;
static void cw_r2d_triangle(cw_vec2 a, cw_vec2 b, cw_vec2 c, cw_color col) {
  cw_vertex2d v[3] = { { a, { 0, 0 }, col }, { b, { 0, 0 }, col }, { c, { 0, 0 }, col } };
  static const uint32_t idx[3] = { 0, 1, 2 };
  cw_r2d_triangles((cw_texture){ 0 }, v, 3, idx, 3);
}

void hud_init(void) {
  big = cw_font_load("fonts/RussoOne-Regular.ttf", NULL);
  small = cw_font_load("fonts/Oswald-VF.ttf", NULL);
}

static void text(cw_font f, const char *s, float x, float y, float size, cw_color c, float align) {
  cw_text_style st = { .size = size, .color = c, .outline_width = size * 0.05f, .outline_color = cw_rgba(0, 0, 0, 0.45f) };
  if (align != 0) {
    cw_text_metrics m = cw_font_measure(f, s, &st);
    x -= m.width * align;
  }
  cw_r2d_text(f, s, cw_v2(x, y), &st);
}

static bool to_screen(t3_vec3 p, float w, float h, cw_vec2 *out) {
  t3_camera *cam = cw_camera3d_t3(G.camera);
  if (!cam) return false;
  t3_vec3 v = t3_vec3_apply_mat4(p, &cam->matrix_world_inverse);
  if (v.z > -1) return false;
  t3_vec3 q = t3_vec3_apply_mat4(v, &cam->projection_matrix);
  out->x = (q.x * 0.5f + 0.5f) * w;
  out->y = (0.5f - q.y * 0.5f) * h;
  return true;
}

int g_hud_ptr_mode, g_hud_wrong;
float g_hud_ptr[2], g_hud_buoy[4];
int g_hud_mark = 1;

/* A triangle pointing along (dx, dy) with its tip at p, on a dark backing so it reads against bright sea, sky and foam */
static void pointer(cw_vec2 p, float dx, float dy, float len, float half, cw_color col) {
  float nx = -dy, ny = dx, o = len * 0.2f;
  cw_r2d_triangle(cw_v2(p.x + dx * o, p.y + dy * o), cw_v2(p.x - dx * (len + o) + nx * (half + o), p.y - dy * (len + o) + ny * (half + o)), cw_v2(p.x - dx * (len + o) - nx * (half + o), p.y - dy * (len + o) - ny * (half + o)), cw_rgba(0, 0, 0, 0.6f));
  cw_r2d_triangle(p, cw_v2(p.x - dx * len + nx * half, p.y - dy * len + ny * half), cw_v2(p.x - dx * len - nx * half, p.y - dy * len - ny * half), col);
}

/* The next buoy, always: over it with the way past it and how far, or at the screen's edge pointing to it when it is out of the picture. The next buoy can be
 * hidden behind a swell or a headland and this still says where it is */
static void next_mark(const craft *c, float w, float h, float u) {
  g_hud_ptr_mode = 0;
  if (G.state != RACE_ON && G.state != RACE_GRID) return;
  const float *b = BUOYS[c->next_buoy];
  cw_water_sample s;
  cw_water_sample_at(G.water, b[0], b[1], G.wt, &s);
  float top = s.height + (g_buoy_style ? 11.6f * mark_size() : 3.3f);
  float dist = sqrtf((b[0] - c->x) * (b[0] - c->x) + (b[1] - c->z) * (b[1] - c->z));
  /* side +1: keep the buoy on your right: the chevron points to your way past it, left */
  float dir = b[2] > 0 ? -1.0f : 1.0f;
  cw_color col = b[2] > 0 ? cw_rgba(1, 0.25f, 0.12f, 0.97f) : cw_rgba(1, 0.8f, 0.1f, 0.97f);
  cw_vec2 p = { 0, 0 }, base = { 0, 0 };
  bool front = to_screen((t3_vec3){ b[0], top, b[1] }, w, h, &p);
  to_screen((t3_vec3){ b[0], s.height, b[1] }, w, h, &base);
  g_hud_buoy[0] = p.x, g_hud_buoy[1] = p.y, g_hud_buoy[2] = front ? 1 : 0, g_hud_buoy[3] = base.y;
  if (!g_hud_mark) return;
  bool inside = front && p.x > 0 && p.x < w && p.y > 0 && p.y < h;
  if (!g_buoy_style) { /* what the game drew before: nothing unless it is in front of the camera, small far away */
    if (!front) return;
    float k = dist < 25 ? 1.3f : dist > 160 ? 0.55f : 1.3f - 0.75f * (dist - 25) / 135, sz2 = 24 * u * k;
    cw_r2d_triangle(cw_v2(p.x + dir * sz2, p.y), cw_v2(p.x - dir * sz2 * 0.3f, p.y - sz2), cw_v2(p.x - dir * sz2 * 0.3f, p.y + sz2), col);
    if (inside) g_hud_ptr_mode = 1, g_hud_ptr[0] = p.x, g_hud_ptr[1] = p.y;
    return;
  }
  char buf[16];
  snprintf(buf, sizeof buf, "%d m", (int)(dist + 0.5f));
  float mx = 70 * u, my = 90 * u;
  if (inside && p.x > mx && p.x < w - mx && p.y > my && p.y < h - my) {
    float k = dist < 40 ? 1.5f : dist > 200 ? 1.0f : 1.5f - 0.5f * (dist - 40) / 160, sz2 = 26 * u * k;
    float py = p.y - 26 * u;
    pointer(cw_v2(p.x + dir * sz2 * 1.3f, py), dir, 0, sz2 * 1.6f, sz2, col);
    text(small, buf, p.x, py - sz2 * 1.2f - 40 * u, 30 * u, cw_rgba(1, 1, 1, 0.95f), 0.5f);
    g_hud_ptr_mode = 1, g_hud_ptr[0] = p.x + dir * sz2 * 0.5f, g_hud_ptr[1] = py;
    return;
  }
  /* out of the picture: from the middle of the screen toward it, stopped at the edge. Behind the camera the projection turns over, so read the way from the camera's own axes */
  t3_camera *cam = cw_camera3d_t3(G.camera);
  t3_vec3 v = t3_vec3_apply_mat4((t3_vec3){ b[0], top, b[1] }, &cam->matrix_world_inverse);
  float dx = v.x, dy = -v.y;
  if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) dx = 1;
  if (!front) dy = fabsf(dy) * 0.3f + 0.2f * fabsf(dx); /* behind: low on the screen, the way to turn */
  float l = sqrtf(dx * dx + dy * dy);
  dx /= l, dy /= l;
  float hw = w * 0.5f - 100 * u, hh = h * 0.5f - 120 * u, t = fminf(fabsf(dx) > 0.001f ? hw / fabsf(dx) : 1e9f, fabsf(dy) > 0.001f ? hh / fabsf(dy) : 1e9f);
  cw_vec2 q = cw_v2(w * 0.5f + dx * t, h * 0.5f + dy * t);
  pointer(q, dx, dy, 72 * u, 44 * u, col);
  text(small, buf, q.x - dx * 110 * u, q.y - dy * 110 * u - 18 * u, 30 * u, cw_rgba(1, 1, 1, 0.95f), 0.5f);
  g_hud_ptr_mode = 2, g_hud_ptr[0] = q.x - dx * 30 * u, g_hud_ptr[1] = q.y - dy * 30 * u;
}

/* Going back along the course, at speed, for over a second. The game's own opinion of the course never changes for it */
static bool wrong_way(const craft *c) {
  static float since, last;
  float dt = G.clock - last;
  last = G.clock;
  if (G.state != RACE_ON || dt <= 0 || dt > 0.5f) { since = 0; return false; }
  const float *a = TRACK[c->track_i], *n = TRACK[(c->track_i + 2) % TRACK_N];
  float tx = n[0] - a[0], tz = n[1] - a[1], l = sqrtf(tx * tx + tz * tz) + 1e-3f, sp = sqrtf(c->vx * c->vx + c->vz * c->vz);
  bool back = sp > 4 && (c->vx * tx + c->vz * tz) / (l * sp) < -0.35f;
  since = back ? since + dt : 0;
  return since > 1.0f;
}

static void hud_body(void) {
  cw_vec2 sz = cw_r2d_canvas_size();
  float w = sz.x, h = sz.y, u = h / 1080.0f;
  const craft *c = &G.c[0];
  cw_color white = cw_rgba(1, 1, 1, 0.96f), gold = cw_rgba(1.0f, 0.78f, 0.25f, 1), dim = cw_rgba(1, 1, 1, 0.7f);
  char buf[64];
  static float hook_seen;
  static bool was_open;
  { bool op = hook_open(); if (op && !was_open) hook_seen = G.clock; was_open = op; }
  if (G.state == RACE_ATTRACT || G.state == RACE_DONE) return; /* the results page has it all */
  /* speed, bottom right */
  snprintf(buf, sizeof buf, "%d", (int)(c->speed * 3.6f + 0.5f));
  text(big, buf, w - 150 * u, h - 186 * u, 92 * u, white, 1);
  text(small, "km/h", w - 138 * u, h - 134 * u, 32 * u, dim, 0);
  /* meter: five arrows, the speed the buoys have earned */
  for (int i = 0; i < 5; i++) {
    float x = w - 372 * u + (float)i * 58 * u, y = h - 42 * u;
    cw_color col = i < c->meter ? gold : cw_rgba(1, 1, 1, 0.18f);
    if (c->flash_miss > 0) col = cw_rgba(1, 0.2f, 0.12f, 0.2f + 0.7f * c->flash_miss);
    cw_r2d_triangle(cw_v2(x, y - 4 * u), cw_v2(x + 46 * u, y - 4 * u), cw_v2(x + 23 * u, y - 32 * u), col);
  }
  /* place and lap, top left */
  static const char *const ord[] = { "1st", "2nd", "3rd", "4th", "5th", "6th" };
  if (g_mode == MODE_TIME_TRIAL) text(big, "TIME TRIAL", 60 * u, 58 * u, 52 * u, white, 0);
  else text(big, ord[c->place - 1 < 0 ? 0 : c->place - 1], 60 * u, 42 * u, 84 * u, white, 0);
  snprintf(buf, sizeof buf, "LAP %d/%d", c->lap < 0 ? 1 : c->lap + 1 > LAPS ? LAPS : c->lap + 1, LAPS);
  text(small, buf, 62 * u, 138 * u, 40 * u, dim, 0);
  if (G.clock >= 0) {
    float tm = c->finish > 0 ? c->finish : G.clock;
    snprintf(buf, sizeof buf, "%d:%05.2f", (int)(tm / 60), tm - 60.0f * (float)(int)(tm / 60));
    text(small, buf, w - 60 * u, 48 * u, 44 * u, white, 1);
    float best = menu_best_time();
    if (g_mode == MODE_TIME_TRIAL && best > 0) {
      snprintf(buf, sizeof buf, "best %d:%05.2f", (int)(best / 60), best - 60.0f * (float)(int)(best / 60));
      text(small, buf, w - 60 * u, 100 * u, 34 * u, dim, 1);
    }
  }
  next_mark(c, w, h, u);
  /* misses so far: five and the race is over */
  for (int i = 0; i < 5; i++) {
    float x = 62 * u + (float)i * 34 * u, y = 196 * u;
    cw_r2d_round_rect(cw_rect_make(x, y, 24 * u, 24 * u), 12 * u, 8, i < c->misses ? cw_rgba(1, 0.25f, 0.12f, 0.95f) : cw_rgba(1, 1, 1, 0.2f));
  }
  if (c->out_t > 0.2f) {
    snprintf(buf, sizeof buf, "BACK TO THE COURSE  %d", (int)ceilf(5.0f - c->out_t));
    text(big, buf, w * 0.5f, h * 0.2f, 54 * u, cw_rgba(1, 0.4f, 0.15f, 1), 0.5f);
  }
  if (c->flash_short > 0) text(big, "THE SHORT WAY", w * 0.5f, h * 0.2f, 66 * u, cw_rgba(0.6f, 1, 0.9f, c->flash_short), 0.5f);
  if (DEF->hook.kind == 1 && hook_open() && G.clock - hook_seen < 5 && G.state == RACE_ON) text(small, "The tide has fallen: the arch is open", w * 0.5f, h * 0.14f, 40 * u, cw_rgba(0.6f, 1, 0.9f, 1), 0.5f);
  g_hud_wrong = wrong_way(c);
  if (g_hud_wrong && g_buoy_style && c->out_t <= 0.2f && c->spill <= 0) text(big, "WRONG WAY", w * 0.5f, h * 0.2f, 76 * u, cw_rgba(1, 0.3f, 0.15f, 0.65f + 0.35f * sinf(G.clock * 9)), 0.5f);
  if (c->spill > 0) text(big, "WIPEOUT", w * 0.5f, h * 0.2f, 76 * u, cw_rgba(1, 1, 1, 0.9f), 0.5f);
  if (c->flash_miss > 0) text(big, "MISS", w * 0.5f, h * 0.2f, 76 * u, cw_rgba(1, 0.3f, 0.15f, c->flash_miss), 0.5f);
  if (c->air && c->air_time > 0.5f) {
    snprintf(buf, sizeof buf, "%.1f s", c->air_time);
    text(small, buf, w * 0.5f, h * 0.26f, 44 * u, gold, 0.5f);
  }
  if (G.state == RACE_GRID) {
    int n = (int)ceilf(-G.clock);
    if (n > 3) n = 3;
    snprintf(buf, sizeof buf, "%d", n);
    if (G.clock < 0) text(big, buf, w * 0.5f, h * 0.3f, 190 * u, white, 0.5f);
  } else if (G.state == RACE_ON && G.clock < 1.2f) {
    text(big, "GO", w * 0.5f, h * 0.3f, 190 * u, gold, 0.5f);
  }
}

void hud_draw(void) {
  cw_r2d_begin(NULL);
  hud_body();
  menu_draw();
  cw_r2d_end();
}
