/* The 2D layer: the racing HUD (time, herring count with the original icon,
 * the speed and jump-charge gauges on the original gauge art, lives) and
 * the text helpers the menus draw with. */
#include "game.h"

#include <math.h>
#include <stdio.h>

#include "cartwheel/render2d.h"

static cw_font font_big, font_small;
static cw_texture tex_herring, tex_life, tex_gauge, tex_speed_mask, tex_energy_mask;

void hud_init(void) {
  font_big = cw_font_load("fonts/TitanOne-Regular.ttf", NULL);
  font_small = cw_font_load("fonts/Fredoka-SemiBold.ttf", NULL);
  tex_herring = cw_texture_load("original/textures/herringicon.png", NULL);
  tex_life = cw_texture_load("original/textures/tuxlife.png", NULL);
  tex_gauge = cw_texture_load("original/textures/gaugeoutline.png", NULL);
  tex_speed_mask = cw_texture_load("original/textures/gaugespeedmask.png", NULL);
  tex_energy_mask = cw_texture_load("original/textures/gaugeenergymask.png", NULL);
}

cw_font hud_font(void) { return font_small; }
void hud_begin(void) { cw_r2d_begin(&(cw_r2d_pass_desc){ 0 }); }
void hud_end(void) { cw_r2d_end(); }
/* every HUD text carries a soft drop shadow: it stays readable on white snow */
static void text_shadowed(cw_font f, const char *s, float x, float y, float size, cw_color c, int align) {
  cw_text_style st = { .size = size, .color = c, .align = (cw_text_align)align };
  if (align) { cw_text_metrics m = cw_font_measure(f, s, &st); x -= align == 1 ? m.width / 2 : m.width; }
  cw_text_style sh = st;
  sh.color = (cw_color){ 0, 0.02f, 0.06f, 0.55f * c.a };
  float d = size > 24 ? 2.5f : 1.5f;
  cw_r2d_text(f, s, (cw_vec2){ x + d, y + d }, &sh);
  cw_r2d_text(f, s, (cw_vec2){ x, y }, &st);
}
void hud_text(const char *s, float x, float y, float size, cw_color c, int align) { text_shadowed(font_small, s, x, y, size, c, align); }
void hud_title(const char *s, float x, float y, float size, cw_color c, int align) { text_shadowed(font_big, s, x, y, size, c, align); }
static void panel(float x, float y, float w, float h, float alpha);
/* a translucent dark panel with rounded corners (the HUD's cards, the menus) */
void hud_panel(float x, float y, float w, float h, float alpha) { panel(x, y, w, h, alpha); }
static void panel(float x, float y, float w, float h, float alpha) {
  const float r = 14;
  cw_color c = { 0.02f, 0.05f, 0.1f, alpha };
  cw_r2d_rect((cw_rect){ x + r, y, w - 2 * r, h }, c);
  cw_r2d_rect((cw_rect){ x, y + r, r, h - 2 * r }, c);
  cw_r2d_rect((cw_rect){ x + w - r, y + r, r, h - 2 * r }, c);
  for (int k = 0; k < 4; k++) {
    float cx = k & 1 ? x + w - r : x + r, cy = k & 2 ? y + h - r : y + r;
    cw_vec2 q[10];
    q[0] = (cw_vec2){ cx, cy };
    for (int i = 0; i < 9; i++) {
      float a = ((k == 0 ? 180 : k == 1 ? 270 : k == 2 ? 90 : 0) + 90.0f * i / 8) * 0.017453292f;
      q[i + 1] = (cw_vec2){ cx + cosf(a) * r, cy + sinf(a) * r };
    }
    cw_r2d_polygon(q, 10, c);
  }
}
static void disc(float cx, float cy, float r, cw_color c) {
  cw_vec2 q[48];
  for (int i = 0; i < 48; i++) { float a = 6.2831853f * i / 48; q[i] = (cw_vec2){ cx + cosf(a) * r, cy + sinf(a) * r }; }
  cw_r2d_polygon(q, 48, c);
}
void hud_box(float x, float y, float w, float h, cw_color c) { cw_r2d_rect((cw_rect){ x, y, w, h }, c); }
void hud_image(const char *asset, float x, float y, float w, float h) {
  cw_texture t = cw_texture_load(asset, NULL);
  cw_r2d_sprite(&(cw_sprite){ .texture = t, .pos = { x, y }, .size = { w, h } });
}

/* the original gauge: a 128 px image, the speed bar sweeping from 225 to 45
 * degrees around (71, 55), green to 60 km/h, yellow to 100, red to 160 */
static void gauge(const tux_phys *p, float x, float y) {
  const float S = 1.4f; /* 128 px art at 179 px */
  disc(x + 71 * S, y + (128 - 55) * S, 74 * S, (cw_color){ 0.02f, 0.05f, 0.1f, 0.42f }); /* a dark dish behind the dial */
  cw_r2d_sprite(&(cw_sprite){ .texture = tex_gauge, .pos = { x, y }, .size = { 128 * S, 128 * S } });
  float kmh = p->speed * 3.6f;
  float frac = kmh <= 60 ? kmh / 60 * 0.5f : kmh <= 100 ? 0.5f + (kmh - 60) / 40 * 0.25f : 0.75f + fminf(1, (kmh - 100) / 60) * 0.25f;
  cw_color col = kmh <= 60 ? (cw_color){ 0.25f, 0.95f, 0.3f, 0.9f } : kmh <= 100 ? (cw_color){ 1, 0.9f, 0.2f, 0.9f } : (cw_color){ 1, 0.25f, 0.2f, 0.9f };
  float cx = x + 71 * S, cy = y + (128 - 55) * S, r0 = 40 * S, r1 = 68 * S;
  float a0 = 225, a1 = 225 + (45 - 225) * frac;
  int segs = (int)fabsf(a1 - a0) / 6 + 1;
  for (int i = 0; i < segs; i++) {
    float a = a0 + (a1 - a0) * (float)i / (float)segs, b = a0 + (a1 - a0) * (float)(i + 1) / (float)segs;
    float ca = cosf(TR_DEG(a)), sa = -sinf(TR_DEG(a)), cb = cosf(TR_DEG(b)), sb = -sinf(TR_DEG(b));
    cw_vec2 q[4] = { { cx + ca * r0, cy + sa * r0 }, { cx + ca * r1, cy + sa * r1 }, { cx + cb * r1, cy + sb * r1 }, { cx + cb * r0, cy + sb * r0 } };
    cw_r2d_polygon(q, 4, col);
  }
  /* the energometer: a vertical bar filling the gauge's left column with the jump charge */
  float e = p->jump_amt < 0 ? 0 : p->jump_amt > 1 ? 1 : p->jump_amt;
  cw_r2d_rect((cw_rect){ x + 8 * S, y + (128 - 3 - 103 * e) * S, 10 * S, 103 * e * S }, (cw_color){ 0.3f, 0.6f, 1, 0.9f });
  char buf[16];
  snprintf(buf, sizeof buf, "%.0f", kmh);
  hud_title(buf, cx, cy - 22, 30, (cw_color){ 1, 1, 1, 1 }, 1);
  hud_text("km/h", cx, cy + 10, 16, (cw_color){ 1, 1, 1, 0.9f }, 1);
}

void hud_draw(const tux_phys *p, float race_time, int herring, int lives) {
  hud_begin();
  cw_vec2 size = cw_r2d_canvas_size();
  char buf[64];
  int minutes = (int)(race_time / 60), seconds = (int)race_time % 60, hundredths = (int)(race_time * 100) % 100;
  cw_color yellow = { 1, 0.9f, 0.2f, 1 };
  panel(12, 10, 236, 70, 0.42f);
  hud_text("TIME", 28, 14, 17, (cw_color){ 1, 1, 1, 0.8f }, 0);
  snprintf(buf, sizeof buf, "%02d:%02d:%02d", minutes, seconds, hundredths);
  hud_title(buf, 28, 32, 34, yellow, 0);
  panel(size.x - 180, 10, 168, 50, 0.42f);
  cw_r2d_sprite(&(cw_sprite){ .texture = tex_herring, .pos = { size.x - 168, 20 }, .size = { 48, 30 } });
  snprintf(buf, sizeof buf, "%03d", herring);
  hud_title(buf, size.x - 108, 14, 34, yellow, 0);
  if (lives > 0) {
    panel(size.x - 40 - 30 * (float)lives - 8, 66, 30 * (float)lives + 22, 38, 0.42f);
    for (int i = 0; i < lives; i++) cw_r2d_sprite(&(cw_sprite){ .texture = tex_life, .pos = { size.x - 40 - 30 * (float)i, 72 }, .size = { 26, 26 } });
  }
  gauge(p, size.x - 190, size.y - 190);
  hud_end();
}
