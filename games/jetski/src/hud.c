/* The HUD: speed, place, lap, the meter of arrows that sets top speed, the next buoy's side. Drawn in the canvas's own
 * size; everything scales with its height. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

/* Words across the middle of the picture: each one drawn says where it stands, and the next buoy's arrow and distance, drawn after them, keep clear */
typedef struct word_box { float x0, y0, x1, y1; } word_box;
#define WORDS_MAX 12
static word_box words[WORDS_MAX];
static int words_n;
int g_hud_label_old, g_hud_label_hits, g_hud_words_n, g_hud_pop_n, g_hud_pop_keep;
float g_hud_label[4];
static void word(cw_font f, const char *s, float cx, float y, float size, cw_color c) {
  text(f, s, cx, y, size, c, 0.5f);
  if (c.a < 0.03f || words_n >= WORDS_MAX) return;
  cw_text_style st = { .size = size };
  float hw = cw_font_measure(f, s, &st).width * 0.5f;
  words[words_n++] = (word_box){ cx - hw, y, cx + hw, y + size * 1.25f };
}
static bool word_hit(float x0, float y0, float x1, float y1, float *below) {
  bool hit = false;
  for (int i = 0; i < words_n; i++) {
    const word_box *b = &words[i];
    if (x1 < b->x0 || x0 > b->x1 || y1 < b->y0 || y0 > b->y1) continue;
    hit = true;
    if (below && b->y1 > *below) *below = b->y1;
  }
  return hit;
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
float g_map_scale = 1;

/* The minimap: the course's own line in a small corner, no panel behind it, so the water reads through. Rivals pale, the next buoy bright and ringed, you an arrow on top */
int g_map_on = 1, g_map_skew;
float g_map_rect[4], g_map_dots[RACERS * 2], g_map_next[2];
static cw_rect map_rect(float h, float u) { float sz = 236 * u * g_map_scale; return cw_rect_make(44 * u, h - 44 * u - sz, sz, sz); }
static void minimap(const craft *c, float h, float u) {
  cw_rect r = map_rect(h, u);
  static const course_def *fit_def;
  static float minx, maxx, minz, maxz;
  if (fit_def != DEF) {
    fit_def = DEF, minx = minz = 1e9f, maxx = maxz = -1e9f;
    for (int i = 0; i < TRACK_N; i++) { minx = fminf(minx, TRACK[i][0]), maxx = fmaxf(maxx, TRACK[i][0]), minz = fminf(minz, TRACK[i][1]), maxz = fmaxf(maxz, TRACK[i][1]); }
  }
  float sc = fminf(r.w / (maxx - minx + 1e-3f), r.h / (maxz - minz + 1e-3f)), ox = r.x + (r.w - (maxx - minx) * sc) * 0.5f, oy = r.y + (r.h - (maxz - minz) * sc) * 0.5f;
#define MAP_PT(px, pz) cw_v2(ox + ((px) - minx) * sc, oy + ((pz) - minz) * sc)
  g_map_rect[0] = r.x, g_map_rect[1] = r.y, g_map_rect[2] = r.w, g_map_rect[3] = r.h;
  cw_vec2 line[128];
  int n = 0;
  for (int i = 0; i < TRACK_N && n < 128; i += TRACK_N / 96 + 1) line[n++] = MAP_PT(TRACK[i][0], TRACK[i][1]);
  cw_r2d_polyline(line, n, 7 * u, true, cw_rgba(0, 0, 0, 0.28f));
  cw_r2d_polyline(line, n, 3.2f * u, true, cw_rgba(1, 1, 1, 0.62f));
  for (int b = 0; b < BUOY_N; b++) {
    if (!buoy_on(b) || b == c->next_buoy) continue;
    cw_r2d_circle(MAP_PT(BUOYS[b][0], BUOYS[b][1]), 2.6f * u, 8, BUOYS[b][2] > 0 ? cw_rgba(1, 0.3f, 0.15f, 0.55f) : cw_rgba(1, 0.8f, 0.15f, 0.55f));
  }
  for (int i = RACERS - 1; i >= 0; i--) {
    const craft *k = &G.c[i];
    cw_vec2 p = MAP_PT(k->x + (g_map_skew ? 60.0f : 0), k->z);
    g_map_dots[i * 2] = p.x, g_map_dots[i * 2 + 1] = p.y;
    if (i > 0 && racer_on(i)) cw_r2d_circle(p, 4.2f * u, 10, cw_rgba(0.72f, 0.86f, 1, 0.8f));
  }
  /* the next buoy over the rivals */
  const float *nb = BUOYS[c->next_buoy];
  cw_vec2 np = MAP_PT(nb[0], nb[1]);
  g_map_next[0] = np.x, g_map_next[1] = np.y;
  cw_color nc = nb[2] > 0 ? cw_rgba(1, 0.3f, 0.12f, 1) : cw_rgba(1, 0.82f, 0.1f, 1);
  cw_r2d_circle(np, 4.4f * u, 12, nc);
  cw_r2d_circle_lines(np, 8 * u, 1.6f * u, 14, cw_rgba(nc.r, nc.g, nc.b, 0.75f));
  /* you: an arrow the way the craft points */
  cw_vec2 me = cw_v2(g_map_dots[0], g_map_dots[1]);
  float sy = sinf(c->yaw), cy = cosf(c->yaw), L = 11 * u, W = 6.5f * u;
  cw_r2d_triangle(cw_v2(me.x + sy * (L + 2 * u), me.y + cy * (L + 2 * u)), cw_v2(me.x - sy * (L * 0.6f + 2 * u) + cy * (W + 2 * u), me.y - cy * (L * 0.6f + 2 * u) - sy * (W + 2 * u)), cw_v2(me.x - sy * (L * 0.6f + 2 * u) - cy * (W + 2 * u), me.y - cy * (L * 0.6f + 2 * u) + sy * (W + 2 * u)), cw_rgba(0, 0, 0, 0.55f));
  cw_r2d_triangle(cw_v2(me.x + sy * L, me.y + cy * L), cw_v2(me.x - sy * L * 0.6f + cy * W, me.y - cy * L * 0.6f - sy * W), cw_v2(me.x - sy * L * 0.6f - cy * W, me.y - cy * L * 0.6f + sy * W), cw_rgba(1, 1, 1, 1));
#undef MAP_PT
}

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
    float py = p.y - 26 * u, ly = py - sz2 * 1.2f - 40 * u, lhw = 46 * u, ah = sz2 * 1.3f;
    /* a word stands over them: both go down past it together, so the distance stays with its arrow */
    if (!g_hud_label_old) for (int n = 0; n < 4; n++) {
      float below = 0;
      if (!word_hit(p.x - fmaxf(lhw, sz2 * 2.2f), ly, p.x + fmaxf(lhw, sz2 * 2.2f), py + ah, &below)) break;
      float move = below + 8 * u - ly;
      py += move, ly += move;
    }
    pointer(cw_v2(p.x + dir * sz2 * 1.3f, py), dir, 0, sz2 * 1.6f, sz2, col);
    text(small, buf, p.x, ly, 30 * u, cw_rgba(1, 1, 1, 0.95f), 0.5f);
    g_hud_label[0] = p.x - lhw, g_hud_label[1] = ly, g_hud_label[2] = p.x + lhw, g_hud_label[3] = ly + 38 * u;
    if (word_hit(g_hud_label[0], g_hud_label[1], g_hud_label[2], g_hud_label[3], NULL)) g_hud_label_hits++;
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
  /* clear of the minimap in its corner */
  if (g_map_on) { cw_rect mr = map_rect(h, u); if (q.x < mr.x + mr.w + 110 * u && q.y > mr.y - 110 * u) q.y = mr.y - 110 * u; }
  float lx = q.x - dx * 110 * u, ly = q.y - dy * 110 * u - 18 * u, lhw = 46 * u;
  if (!g_hud_label_old) for (int n = 0; n < 4; n++) {
    float below = 0;
    if (!word_hit(fminf(lx - lhw, q.x - 72 * u), fminf(ly, q.y - 72 * u), fmaxf(lx + lhw, q.x + 72 * u), fmaxf(ly + 38 * u, q.y + 72 * u), &below)) break;
    float move = below + 8 * u - fminf(ly, q.y - 72 * u);
    q.y += move, ly += move;
  }
  pointer(q, dx, dy, 72 * u, 44 * u, col);
  text(small, buf, lx, ly, 30 * u, cw_rgba(1, 1, 1, 0.95f), 0.5f);
  g_hud_label[0] = lx - lhw, g_hud_label[1] = ly, g_hud_label[2] = lx + lhw, g_hud_label[3] = ly + 38 * u;
  if (word_hit(g_hud_label[0], g_hud_label[1], g_hud_label[2], g_hud_label[3], NULL)) g_hud_label_hits++;
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

/* Popups: three at most, under the top of the picture, easing in and fading: what you just did, in words */
typedef struct popup { char text[32]; float t; cw_color col; bool big; } popup;
static popup pop[3];
static void pop_add(const char *t, cw_color col, bool big) {
  for (int i = 2; i > 0; i--) pop[i] = pop[i - 1];
  snprintf(pop[0].text, sizeof pop[0].text, "%s", t), pop[0].t = 0, pop[0].col = col, pop[0].big = big;
}
static float meter_pulse, chip_pulse;
static unsigned ev_read;
/* a new race starts clean: words from the last one, and events it had not shown yet, do not hang over it */
void hud_race_reset(void) {
  if (g_hud_pop_keep) return;
  for (int i = 0; i < 3; i++) pop[i].text[0] = 0;
  ev_read = g_ev_n;
  meter_pulse = chip_pulse = 0;
}
static void hud_events(float dt) {
  unsigned read = ev_read;
  static const char *const ord[] = { "1ST", "2ND", "3RD", "4TH", "5TH", "6TH" };
  cw_color gold = cw_rgba(1.0f, 0.78f, 0.25f, 1), white = cw_rgba(1, 1, 1, 1), red = cw_rgba(1, 0.35f, 0.2f, 1), cyan = cw_rgba(0.55f, 0.95f, 1, 1), green = cw_rgba(0.6f, 1, 0.6f, 1);
  if (g_ev_n - read > EV_RING) read = g_ev_n - EV_RING;
  for (; read != g_ev_n; read++) {
    const game_event *e = &g_ev[read % EV_RING];
    if (!g_ev_on) continue;
    char b[32] = { 0 };
    bool shown = true;
    switch (e->kind) {
      case EV_PASS: meter_pulse = 1; shown = false; g_fb[e->kind][0]++; break;
      case EV_TOP: pop_add("MAX POWER", gold, true); meter_pulse = 1; break;
      case EV_TRICK: pop_add(e->a == 2 ? "BACK FLIP" : e->a == 3 ? "FRONT FLIP" : e->a == 4 ? "KICK OUT" : "BARREL ROLL", cyan, true); break;
      case EV_LAND: if (e->a == 2) pop_add("DIVE", cyan, true); else if (e->a) pop_add("PERFECT LANDING", green, false); else { shown = false; g_fb[e->kind][0]++; } break;
      case EV_MISS: shown = false; g_fb[e->kind][0]++; break; /* MISS has its own big word */
      case EV_GAIN: snprintf(b, sizeof b, "UP TO %s", ord[e->a - 1 < 0 ? 0 : e->a - 1 > 5 ? 5 : e->a - 1]); pop_add(b, green, false); break;
      case EV_LOSE: snprintf(b, sizeof b, "DOWN TO %s", ord[e->a - 1 < 0 ? 0 : e->a - 1 > 5 ? 5 : e->a - 1]); pop_add(b, red, false); break;
      case EV_LAP: snprintf(b, sizeof b, "LAP %d", e->a); pop_add(b, white, true); break;
      case EV_FINAL: pop_add("FINAL LAP", gold, true); break;
      case EV_WALL: shown = false; g_fb[e->kind][0]++; break;
      case EV_CRASH: shown = false; g_fb[e->kind][0]++; break; /* WIPEOUT has its own */
      case EV_CHARGE: pop_add("TURBO READY", cyan, false); chip_pulse = 1; break;
      case EV_TURBO: pop_add("TURBO", cyan, true); chip_pulse = 1; break;
    }
    if (shown) g_fb[e->kind][0]++;
  }
  ev_read = read;
  g_hud_pop_n = 0;
  for (int i = 0; i < 3; i++) if (pop[i].text[0]) { pop[i].t += dt; if (pop[i].t > 1.5f) pop[i].text[0] = 0; else g_hud_pop_n++; }
  meter_pulse = fmaxf(0, meter_pulse - dt * 3), chip_pulse = fmaxf(0, chip_pulse - dt * 2.5f);
}
static void hud_popups(float w, float h, float u) {
  float y = h * 0.135f;
  for (int i = 0; i < 3; i++) {
    const popup *p = &pop[i];
    if (!p->text[0]) continue;
    float in = fminf(1, p->t / 0.14f), back = 1 + 0.5f * (1 - in) * (1 - in), a = fminf(in, (1.5f - p->t) / 0.4f) * 0.92f, size = (p->big ? 54.0f : 38.0f) * u * back;
    if (a < 0) a = 0;
    word(big, p->text, w * 0.5f, y, size, cw_rgba(p->col.r, p->col.g, p->col.b, a));
    y += size * 1.15f;
  }
}
/* who is just ahead and behind, and by how long: translucent, under the misses */
static void hud_neighbours(const craft *c, float u) {
  if (g_mode == MODE_TIME_TRIAL || G.state != RACE_ON) return;
  int me = -1;
  for (int i = 0; i < RACERS; i++) if (G.order[i] == 0) me = i;
  float y = 236 * u;
  for (int k = me - 1; k <= me + 1; k += 2) {
    if (k < 0 || k >= RACERS) continue;
    const craft *o = &G.c[G.order[k]];
    if (!racer_on(G.order[k])) continue;
    float gap = fabsf(o->dist - c->dist) / fmaxf(c->speed, 8.0f);
    char b[40];
    snprintf(b, sizeof b, "%s  %s%.1f", RIDER_NAMES[G.order[k]], k < me ? "-" : "+", gap);
    text(small, b, 62 * u, y, 28 * u, cw_rgba(1, 1, 1, 0.62f), 0);
    y += 32 * u;
  }
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
  hud_events(1.0f / 60.0f);
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
    float pu = i == c->meter - 1 ? meter_pulse : 0, gr = 5 * u * pu;
    cw_r2d_triangle(cw_v2(x - gr, y - 4 * u + gr * 0.3f), cw_v2(x + 46 * u + gr, y - 4 * u + gr * 0.3f), cw_v2(x + 23 * u, y - 32 * u - gr * 1.4f), col);
  }
  /* turbos in hand: chips over the arrows, the part-charged one faint */
  for (int i = 0; i < 3; i++) {
    float x = w - 372 * u + (float)i * 58 * u, y = h - 92 * u, fill = fminf(1, fmaxf(0, c->stock - (float)i));
    cw_r2d_round_rect(cw_rect_make(x, y, 46 * u, 15 * u), 7 * u, 6, cw_rgba(1, 1, 1, 0.14f));
    if (fill > 0) { float gl = fill >= 1 ? 0.92f + 0.08f * chip_pulse : 0.5f; cw_r2d_round_rect(cw_rect_make(x, y, 46 * u * fill, 15 * u), 7 * u, 6, cw_rgba(0.55f, 0.95f, 1, gl)); }
  }
  /* place and lap, top left */
  static const char *const ord[] = { "1st", "2nd", "3rd", "4th", "5th", "6th" };
  if (g_mode == MODE_TIME_TRIAL) text(big, "TIME TRIAL", 60 * u, 58 * u, 52 * u, white, 0);
  else { int sp = c->shown_place > 0 ? c->shown_place : c->place; text(big, ord[sp - 1 < 0 ? 0 : sp - 1], 60 * u, 42 * u, 84 * u, white, 0); }
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
  if (g_map_on) minimap(c, h, u);
  hud_neighbours(c, u);
  words_n = 0;
  hud_popups(w, h, u);
  /* misses so far: five and the race is over */
  for (int i = 0; i < 5; i++) {
    float x = 62 * u + (float)i * 34 * u, y = 196 * u;
    cw_r2d_round_rect(cw_rect_make(x, y, 24 * u, 24 * u), 12 * u, 8, i < c->misses ? cw_rgba(1, 0.25f, 0.12f, 0.95f) : cw_rgba(1, 1, 1, 0.2f));
  }
  if (c->out_t > 0.2f) {
    snprintf(buf, sizeof buf, "BACK TO THE COURSE  %d", (int)ceilf(5.0f - c->out_t));
    word(big, buf, w * 0.5f, h * 0.2f, 54 * u, cw_rgba(1, 0.4f, 0.15f, 1));
  }
  if (c->flash_short > 0) word(big, "THE SHORT WAY", w * 0.5f, h * 0.2f, 66 * u, cw_rgba(0.6f, 1, 0.9f, c->flash_short));
  if (DEF->hook.kind == 1 && hook_open() && G.clock - hook_seen < 5 && G.state == RACE_ON) word(small, "The tide has fallen: the arch is open", w * 0.5f, h * 0.14f, 40 * u, cw_rgba(0.6f, 1, 0.9f, 1));
  g_hud_wrong = wrong_way(c);
  if (g_hud_wrong && g_buoy_style && c->out_t <= 0.2f && c->spill <= 0) word(big, "WRONG WAY", w * 0.5f, h * 0.2f, 76 * u, cw_rgba(1, 0.3f, 0.15f, 0.65f + 0.35f * sinf(G.clock * 9)));
  if (c->spill > 0) word(big, "WIPEOUT", w * 0.5f, h * 0.2f, 76 * u, cw_rgba(1, 1, 1, 0.9f));
  if (c->flash_miss > 0) word(big, "MISS", w * 0.5f, h * 0.2f, 76 * u, cw_rgba(1, 0.3f, 0.15f, c->flash_miss));
  if (c->air && c->air_time > 0.5f) {
    snprintf(buf, sizeof buf, "%.1f s", c->air_time);
    word(small, buf, w * 0.5f, h * 0.26f, 44 * u, gold);
  }
  if (G.state == RACE_GRID) {
    int n = (int)ceilf(-G.clock);
    if (n > 3) n = 3;
    snprintf(buf, sizeof buf, "%d", n);
    if (G.clock < 0) word(big, buf, w * 0.5f, h * 0.3f, 190 * u, white);
  } else if (G.state == RACE_ON && G.clock < 1.2f) {
    word(big, "GO", w * 0.5f, h * 0.3f, 190 * u, gold);
  }
  g_hud_words_n = words_n;
  next_mark(c, w, h, u);
}

void hud_draw(void) {
#ifdef JETSKI_AUTOPILOT
  if (getenv("JETSKI_NO_HUD")) return; /* key art: the picture alone */
#endif
  cw_r2d_begin(NULL);
  hud_body();
  menu_draw();
  cw_r2d_end();
}
