/* brawler HUD: one card per fighter, the boss bar, the GO arrow, banners. */
#include <stdio.h>
#include <string.h>

#include "cartwheel/render2d.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "game.h"

static void bar(cw_rect r, float f, cw_color fill, cw_color back) {
  cw_r2d_round_rect(r, r.h * 0.4f, 6, back);
  if (f > 0) cw_r2d_round_rect(cw_rect_make(r.x, r.y, r.w * (f > 1 ? 1 : f), r.h), r.h * 0.4f, 6, fill);
}
static void text(const char *s, float x, float y, float size, cw_color c, bool center) {
  cw_font f = cw_ui_font_sans();
  cw_text_style st = { .size = size, .outline_width = size * 0.1f, .color = c };
  if (center) { cw_text_metrics m = cw_font_measure(f, s, &st); x -= m.width * 0.5f; }
  cw_r2d_text(f, s, cw_v2(x, y), &st);
}

void ui_draw(void) {
  cw_vec2 sz = cw_r2d_canvas_size();
  float u = sz.y / 36.0f;
  static const cw_color PC[4] = { { 0.95f, 0.35f, 0.3f, 1 }, { 1.0f, 0.75f, 0.2f, 1 }, { 0.3f, 0.85f, 0.55f, 1 }, { 0.4f, 0.6f, 1.0f, 1 } };
  cw_r2d_begin(NULL);
  for (int i = 0; i < W.players; i++) {
    const ent *p = &W.p[i];
    float x = u * 1.2f + (float)i * (sz.x - u * 2.4f) / 4.0f, y = u * 1.0f, w = (sz.x - u * 2.4f) / 4.0f - u;
    cw_r2d_round_rect(cw_rect_make(x - u * 0.3f, y - u * 0.3f, w + u * 0.6f, u * 3.8f), u * 0.4f, 6, cw_rgba(0, 0, 0, 0.45f));
    char b[64];
    snprintf(b, sizeof b, "P%d %s", i + 1, DEFS[p->def].name);
    text(b, x, y, u * 1.0f, PC[i], false);
    bar(cw_rect_make(x, y + u * 1.3f, w, u * 0.7f), p->hp / p->maxhp, p->hp / p->maxhp > 0.3f ? cw_rgba(0.9f, 0.25f, 0.2f, 1) : cw_rgba(1, 0.5f, 0.1f, 1), cw_rgba(0.1f, 0.1f, 0.1f, 0.8f));
    snprintf(b, sizeof b, "%07d   x%d", p->score, p->lives);
    text(b, x, y + u * 2.1f, u * 0.9f, cw_rgba(1, 1, 1, 0.95f), false);
  }
  /* the boss bar */
  if (W.boss_active && W.boss_idx >= 0 && W.e[W.boss_idx].used) {
    const ent *bo = &W.e[W.boss_idx];
    float w = sz.x * 0.5f;
    text(DEFS[bo->def].name, sz.x * 0.5f, sz.y - u * 3.6f, u * 1.1f, cw_rgba(1, 0.85f, 0.4f, 1), true);
    bar(cw_rect_make(sz.x * 0.25f, sz.y - u * 2.2f, w, u * 1.0f), bo->hp / bo->maxhp, cw_rgba(0.85f, 0.15f, 0.55f, 1), cw_rgba(0.1f, 0.05f, 0.1f, 0.85f));
  }
  if (W.combo_n >= 3) {
    char b[32];
    snprintf(b, sizeof b, "%d HIT COMBO", W.combo_n);
    text(b, sz.x * 0.5f, u * 5.0f, u * 1.4f, cw_rgba(1, 0.9f, 0.3f, 1), true);
  }
  if (W.go_flash > 0 && (W.go_flash / 18) % 2 == 0) text("GO  >>>", sz.x - u * 6, sz.y * 0.4f, u * 2.2f, cw_rgba(1, 1, 1, 1), true);
  if (W.phase == PH_INTRO) {
    char b[48];
    snprintf(b, sizeof b, "STAGE %d", W.stage + 1);
    text(b, sz.x * 0.5f, sz.y * 0.32f, u * 3.0f, cw_rgba(1, 0.9f, 0.5f, 1), true);
    text(stage_name(W.stage), sz.x * 0.5f, sz.y * 0.32f + u * 3.6f, u * 1.8f, cw_rgba(1, 1, 1, 1), true);
  }
  if (W.phase == PH_CLEAR) text("STAGE CLEAR", sz.x * 0.5f, sz.y * 0.35f, u * 3.2f, cw_rgba(1, 0.9f, 0.4f, 1), true);
  if (W.phase == PH_OVER) text("GAME OVER", sz.x * 0.5f, sz.y * 0.35f, u * 3.6f, cw_rgba(1, 0.3f, 0.25f, 1), true);
  if (W.phase == PH_WIN) {
    char b[64];
    text("THE STREETS ARE YOURS", sz.x * 0.5f, sz.y * 0.3f, u * 3.2f, cw_rgba(1, 0.9f, 0.4f, 1), true);
    snprintf(b, sizeof b, "SCORE %d", W.total_score);
    text(b, sz.x * 0.5f, sz.y * 0.3f + u * 4.5f, u * 2.0f, cw_rgba(1, 1, 1, 1), true);
  }
  cw_r2d_end();
}
