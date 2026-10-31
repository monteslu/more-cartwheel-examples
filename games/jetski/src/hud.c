/* The HUD: speed, place, lap, the meter of arrows that sets top speed, the next buoy's side. Drawn in the canvas's own
 * size; everything scales with its height. */
#include <math.h>
#include <stdio.h>

#include "cartwheel/platform.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "course_data.h"
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

static void hud_body(void) {
  cw_vec2 sz = cw_r2d_canvas_size();
  float w = sz.x, h = sz.y, u = h / 1080.0f;
  const craft *c = &G.c[0];
  cw_color white = cw_rgba(1, 1, 1, 0.96f), gold = cw_rgba(1.0f, 0.78f, 0.25f, 1), dim = cw_rgba(1, 1, 1, 0.7f);
  char buf[64];
  if (G.state == RACE_ATTRACT) {
    text(big, "JETSKI", w * 0.5f, h * 0.16f, 118 * u, white, 0.5f);
    float pulse = 0.65f + 0.35f * sinf((float)G.tick * 0.08f);
    text(small, "PRESS A OR ENTER", w * 0.5f, h * 0.86f, 40 * u, cw_rgba(1, 1, 1, pulse), 0.5f);
    return;
  }
  /* speed, bottom right */
  snprintf(buf, sizeof buf, "%d", (int)(c->speed * 3.6f + 0.5f));
  text(big, buf, w - 150 * u, h - 148 * u, 92 * u, white, 1);
  text(small, "km/h", w - 138 * u, h - 96 * u, 32 * u, dim, 0);
  /* meter: five arrows, the speed the buoys have earned */
  for (int i = 0; i < 5; i++) {
    float x = w - 372 * u + (float)i * 58 * u, y = h - 42 * u;
    cw_color col = i < c->meter ? gold : cw_rgba(1, 1, 1, 0.18f);
    if (c->flash_miss > 0) col = cw_rgba(1, 0.2f, 0.12f, 0.2f + 0.7f * c->flash_miss);
    cw_r2d_triangle(cw_v2(x, y - 4 * u), cw_v2(x + 46 * u, y - 4 * u), cw_v2(x + 23 * u, y - 32 * u), col);
  }
  /* place and lap, top left */
  static const char *const ord[] = { "1st", "2nd", "3rd", "4th", "5th", "6th" };
  text(big, ord[c->place - 1 < 0 ? 0 : c->place - 1], 60 * u, 42 * u, 84 * u, white, 0);
  snprintf(buf, sizeof buf, "LAP %d/%d", c->lap < 0 ? 1 : c->lap + 1 > LAPS ? LAPS : c->lap + 1, LAPS);
  text(small, buf, 62 * u, 138 * u, 40 * u, dim, 0);
  if (G.clock >= 0) {
    float tm = c->finish > 0 ? c->finish : G.clock;
    snprintf(buf, sizeof buf, "%d:%05.2f", (int)(tm / 60), tm - 60.0f * (float)(int)(tm / 60));
    text(small, buf, w - 60 * u, 48 * u, 44 * u, white, 1);
  }
  /* the next buoy: a chevron over it on its passing side */
  const float *b = BUOYS[c->next_buoy];
  cw_water_sample s;
  cw_water_sample_at(G.water, b[0], b[1], G.wt, &s);
  cw_vec2 p;
  if (G.state == RACE_ON && to_screen((t3_vec3){ b[0], s.height + 3.3f, b[1] }, w, h, &p)) {
    float dist = sqrtf((b[0] - c->x) * (b[0] - c->x) + (b[1] - c->z) * (b[1] - c->z));
    float k = dist < 25 ? 1.3f : dist > 160 ? 0.55f : 1.3f - 0.75f * (dist - 25) / 135;
    /* side +1: keep the buoy on your right: the chevron points to your way past it, left */
    float dir = b[2] > 0 ? -1.0f : 1.0f;
    cw_color col = b[2] > 0 ? cw_rgba(1, 0.25f, 0.12f, 0.95f) : cw_rgba(1, 0.8f, 0.1f, 0.95f);
    float sz2 = 24 * u * k;
    cw_r2d_triangle(cw_v2(p.x + dir * sz2, p.y), cw_v2(p.x - dir * sz2 * 0.3f, p.y - sz2), cw_v2(p.x - dir * sz2 * 0.3f, p.y + sz2), col);
  }
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
  } else if (G.state == RACE_DONE) {
    snprintf(buf, sizeof buf, "%s", ord[c->place - 1]);
    text(big, buf, w * 0.5f, h * 0.27f, 170 * u, gold, 0.5f);
    text(small, "PRESS A TO RACE AGAIN", w * 0.5f, h * 0.48f, 42 * u, dim, 0.5f);
  }
}

void hud_draw(void) {
  cw_r2d_begin(NULL);
  hud_body();
  cw_r2d_end();
}
