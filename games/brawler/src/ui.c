/* brawler HUD: one card per fighter, the boss bar, the GO arrow, banners. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cartwheel/render2d.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include "cartwheel/prompt.h"
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
/* how much the player's text size grows text asked for at `size` (1 at 100%): the HUD spaces its lines by it, so
 * at 100% everything sits where it always did and at 200% the lines move apart as the text grows */
static float grow(float size) { return cw_text_drawn_size(cw_ui_font_sans(), size) / size; }
static float text_w(const char *s, float size) {
  return cw_font_measure(cw_ui_font_sans(), s, &(cw_text_style){ .size = size, .outline_width = size * 0.1f }).width;
}

/* the game camera's projection (game.h CAM_*; level.c places it, drawn back by W.cam_back along its axis) */
static bool to_screen(float x, float y, float z, cw_vec2 sz, cw_vec2 *out) {
  float s = sinf(CAM_PITCH), c = cosf(CAM_PITCH);
  float ry = y - (CAM_Y + W.cam_back * s), rz = z - (CAM_Z + W.cam_back * c);
  float cy = ry * c - rz * s, cz = ry * s + rz * c;
  if (cz > -0.1f) return false;
  float t = CAM_T, asp = sz.x / sz.y;
  out->x = sz.x * 0.5f * (1 + ((x - W.cam_x) / -cz) / (t * asp));
  out->y = sz.y * 0.5f * (1 - (cy / -cz) / t);
  return true;
}

int g_markgap; bool g_legacy_marker; /* debug game.markgap and the gate's control */
void ui_draw(void) {
  cw_vec2 sz = cw_r2d_canvas_size();
  float u = sz.y / 36.0f;
  static const cw_color PC[4] = { { 0.95f, 0.35f, 0.3f, 1 }, { 1.0f, 0.75f, 0.2f, 1 }, { 0.3f, 0.85f, 0.55f, 1 }, { 0.4f, 0.6f, 1.0f, 1 } };
  cw_r2d_begin(NULL);
  /* the fighter cards: name, life bar, score and lives, each line under the last by its drawn size. When the score
   * line is wider than the card (a big text size, a narrow screen) the lives go up beside the name instead */
  float g_name = grow(u * 1.0f), g_score = grow(u * 0.9f), card_bottom = 0;
  for (int i = 0; i < W.players; i++) {
    const ent *p = &W.p[i];
    float x = u * 1.2f + (float)i * (sz.x - u * 2.4f) / 4.0f, y = u * 1.0f, w = (sz.x - u * 2.4f) / 4.0f - u;
    float bar_y = y + u * 1.3f * g_name, score_y = bar_y + u * 0.8f, ch = u * 3.8f + (score_y - (y + u * 2.1f)) + u * 1.1f * (g_score - 1);
    cw_r2d_round_rect(cw_rect_make(x - u * 0.3f, y - u * 0.3f, w + u * 0.6f, ch), u * 0.4f, 6, cw_rgba(0, 0, 0, 0.45f));
    if (y - u * 0.3f + ch > card_bottom) card_bottom = y - u * 0.3f + ch;
    char b[64], lives[16];
    snprintf(b, sizeof b, "%07d   x%d", p->score, p->lives);
    snprintf(lives, sizeof lives, "x%d", p->lives);
    bool split = text_w(b, u * 0.9f) > w;
    if (split) snprintf(b, sizeof b, "%07d", p->score);
    text(b, x, score_y, u * 0.9f, cw_rgba(1, 1, 1, 0.95f), false);
    if (split) text(lives, x + w - text_w(lives, u * 1.0f), y, u * 1.0f, cw_rgba(1, 1, 1, 0.95f), false);
    snprintf(b, sizeof b, "P%d %s", i + 1, DEFS[p->def].name);
    text(b, x, y, u * 1.0f, PC[i], false);
    bar(cw_rect_make(x, bar_y, w, u * 0.7f), p->hp / p->maxhp, p->hp / p->maxhp > 0.3f ? cw_rgba(0.9f, 0.25f, 0.2f, 1) : cw_rgba(1, 0.5f, 0.1f, 1), cw_rgba(0.1f, 0.1f, 0.1f, 0.8f));
    if (p->refund > 0 && p->hp < p->maxhp) { /* the special's cost still to win back by landing blows: green after the bar */
      float f0 = p->hp / p->maxhp, f1 = fminf(1, (p->hp + p->refund) / p->maxhp);
      cw_r2d_round_rect(cw_rect_make(x + w * f0, bar_y + u * 0.12f, w * (f1 - f0), u * 0.46f), u * 0.15f, 4, cw_rgba(0.35f, 0.9f, 0.4f, 0.85f));
    }
  }
  /* a number over every fighter's head in its colour (players differ by more than colour) */
  for (int i = 0; i < W.players; i++) {
    const ent *p = &W.p[i];
    cw_vec2 s;
    if (!p->used || p->st == S_DEAD) continue;
    /* a loose body lies away from its simulated spot: the marker goes with the hips, just over them (it hung in the air over
       nothing, the "P4 with no body"; the gate game.markgap measures the worst distance from it to the hips) */
    t3_vec3 at = { p->x, p->y + 1.95f * char_scale(p->def), p->z }, hips;
    bool loose = rag_hips(p, &hips);
    if (loose) {
      if (!g_legacy_marker) at = (t3_vec3){ hips.x, hips.y + 0.55f, hips.z };
      float gx = at.x - hips.x, gz = at.z - hips.z; int mm = (int)(sqrtf(gx * gx + gz * gz) * 1000.0f + 0.5f);
      if (mm > g_markgap) g_markgap = mm;
    }
    if (!to_screen(at.x, at.y, at.z, sz, &s)) continue;
    char b[8];
    snprintf(b, sizeof b, "P%d", i + 1);
    text(b, s.x, s.y - u * 1.05f * grow(u * 0.95f), u * 0.95f, PC[i], true);
    cw_vec2 tri[3] = { cw_v2(s.x - u * 0.3f, s.y - u * 0.15f), cw_v2(s.x + u * 0.3f, s.y - u * 0.15f), cw_v2(s.x, s.y + u * 0.25f) };
    cw_r2d_polygon(tri, 3, PC[i]);
  }
  /* a comic pop over an enemy's head when a blow lands (B-PT13): sweat drops and "!!" for the frightened big men, an anger
     mark for the furious little ones, hearts for the creep; it pops in, bobs and fades with the reaction */
  for (int i = 0; i < MAXE; i++) {
    const ent *e = &W.e[i];
    int tp = e->used ? temper_of(e->def) : FACE_PLAIN;
    if (!e->used || e->react <= 0 || tp == FACE_PLAIN || e->st == S_DEAD || e->st == S_FALL || e->st == S_DOWN || e->st == S_THROWN) continue;
    cw_vec2 s;
    if (!to_screen(e->x, e->y + 2.1f * (DEFS[e->def].boss ? 1.3f : 1.0f), e->z, sz, &s)) continue;
    float age = (float)(48 - e->react), pop = age < 5 ? age / 5.0f * 1.25f : age < 9 ? 1.25f - (age - 5) * 0.0625f : 1.0f;
    float fade = e->react < 10 ? (float)e->react / 10.0f : 1.0f, k = u * 0.95f * pop, bob = sinf(age * 0.35f) * u * 0.12f;
    float sx = s.x + (float)e->face * -u * 0.9f, sy = s.y - u * 0.4f + bob;
    if (tp == FACE_FEAR) {
      cw_color drop = cw_rgba(0.55f, 0.8f, 1.0f, 0.95f * fade), ink = cw_rgba(0.05f, 0.08f, 0.15f, 0.9f * fade);
      for (int d = 0; d < 2; d++) {
        float dx = sx + (d ? 1.3f : -0.2f) * k, dy = sy + (d ? 0.4f : -0.3f) * k + age * 0.05f * u * 0.1f;
        cw_r2d_circle(cw_v2(dx, dy), k * 0.42f, 14, ink);
        cw_vec2 t[3] = { cw_v2(dx - k * 0.42f, dy), cw_v2(dx + k * 0.42f, dy), cw_v2(dx, dy - k * 1.1f) };
        cw_r2d_polygon(t, 3, ink);
        cw_r2d_circle(cw_v2(dx, dy), k * 0.32f, 14, drop);
        cw_vec2 t2[3] = { cw_v2(dx - k * 0.32f, dy), cw_v2(dx + k * 0.32f, dy), cw_v2(dx, dy - k * 0.95f) };
        cw_r2d_polygon(t2, 3, drop);
      }
      { /* the "!!" is a drawn comic mark like the drops round it (it pops in from nothing): art, drawn as asked */
        cw_text_style st = { .size = k * 2.4f, .outline_width = k * 0.24f, .color = cw_rgba(1, 1, 1, fade), .fixed_size = true };
        cw_r2d_text(cw_ui_font_sans(), "!!", cw_v2(sx - k * 1.6f - cw_font_measure(cw_ui_font_sans(), "!!", &st).width * 0.5f, sy - k * 1.6f), &st);
      }
    } else if (tp == FACE_ANGER) { /* the cross-shaped vein: four bulging arcs */
      cw_color red = cw_rgba(0.95f, 0.12f, 0.1f, fade), ink = cw_rgba(0.15f, 0.0f, 0.0f, 0.9f * fade);
      for (int q = 0; q < 4; q++) {
        float ang = 0.785f + (float)q * 1.5708f, cx = sx + cosf(ang) * k * 0.95f, cy = sy + sinf(ang) * k * 0.95f;
        cw_r2d_arc(cw_v2(cx, cy), k * 0.38f, k * 0.72f, ang + 3.14159f - 1.0f, ang + 3.14159f + 1.0f, 12, ink);
        cw_r2d_arc(cw_v2(cx, cy), k * 0.45f, k * 0.65f, ang + 3.14159f - 0.9f, ang + 3.14159f + 0.9f, 12, red);
      }
    } else { /* hearts rising */
      for (int h = 0; h < 3; h++) {
        float hx = sx + ((float)h - 1) * k * 1.3f, hy = sy - (age * 0.03f + (float)(h & 1) * 0.5f) * k, r = k * (h == 1 ? 0.42f : 0.32f);
        cw_color pink = cw_rgba(1.0f, 0.35f, 0.55f, fade), ink = cw_rgba(0.25f, 0.02f, 0.08f, 0.9f * fade);
        for (int pass = 0; pass < 2; pass++) {
          float rr = r * (pass ? 1.0f : 1.25f);
          cw_color c = pass ? pink : ink;
          cw_r2d_circle(cw_v2(hx - rr * 0.55f, hy), rr * 0.62f, 12, c);
          cw_r2d_circle(cw_v2(hx + rr * 0.55f, hy), rr * 0.62f, 12, c);
          cw_vec2 t[3] = { cw_v2(hx - rr * 1.15f, hy + rr * 0.15f), cw_v2(hx + rr * 1.15f, hy + rr * 0.15f), cw_v2(hx, hy + rr * 1.35f) };
          cw_r2d_polygon(t, 3, c);
        }
      }
    }
  }
  /* the last enemy a fighter hit: its name and a small health bar over its head for two seconds (fading out) */
  if (W.last_hit_t > 0 && W.last_hit >= 0 && W.last_hit < MAXE && W.e[W.last_hit].used) {
    const ent *e = &W.e[W.last_hit];
    cw_vec2 s;
    t3_vec3 at = { e->x, e->y + 2.05f * char_scale(e->def), e->z }, hips;
    if (rag_hips(e, &hips)) at = (t3_vec3){ hips.x, hips.y + 0.6f, hips.z };
    if (to_screen(at.x, at.y, at.z, sz, &s)) {
      float a = W.last_hit_t < 20 ? (float)W.last_hit_t / 20.0f : 1.0f, bw = u * 4.2f, bh = u * 0.38f;
      float f = e->hp > 0 ? e->hp / e->maxhp : 0;
      float ny = s.y - bh - u * 0.25f - u * 1.0f * grow(u * 0.9f);
      text(DEFS[e->def].name, s.x, ny, u * 0.9f, cw_rgba(1, 0.92f, 0.8f, a), true);
      bar(cw_rect_make(s.x - bw * 0.5f, s.y - bh, bw, bh), f, cw_rgba(0.95f, 0.75f, 0.2f, a), cw_rgba(0.08f, 0.06f, 0.06f, 0.75f * a));
    }
  }
  /* the boss bar */
  if (W.boss_active && W.boss_idx >= 0 && W.e[W.boss_idx].used) {
    const ent *bo = &W.e[W.boss_idx];
    float w = sz.x * 0.5f;
    text(DEFS[bo->def].name, sz.x * 0.5f, sz.y - u * 2.2f - u * 1.4f * grow(u * 1.1f), u * 1.1f, cw_rgba(1, 0.85f, 0.4f, 1), true);
    bar(cw_rect_make(sz.x * 0.25f, sz.y - u * 2.2f, w, u * 1.0f), bo->hp / bo->maxhp, cw_rgba(0.85f, 0.15f, 0.55f, 1), cw_rgba(0.1f, 0.05f, 0.1f, 0.85f));
    /* the three phases: notches where the boss changes, the phases it has reached lit */
    for (int k = 1; k <= 2; k++) {
      float f = k == 1 ? 0.67f : 0.34f;
      cw_r2d_round_rect(cw_rect_make(sz.x * 0.25f + w * f - u * 0.12f, sz.y - u * 2.45f, u * 0.24f, u * 1.5f), u * 0.1f, 4, cw_rgba(1, 1, 1, 0.9f));
    }
    for (int k = 0; k < 3; k++)
      cw_r2d_round_rect(cw_rect_make(sz.x * 0.75f + u * 0.6f + (float)k * u * 1.0f, sz.y - u * 2.1f, u * 0.75f, u * 0.75f), u * 0.37f, 8,
                        k <= bo->phase ? cw_rgba(1, 0.35f, 0.25f, 1) : cw_rgba(0.25f, 0.2f, 0.25f, 0.8f));
    if (W.phase_banner > 0 && (W.phase_banner / 10) % 3 != 0) {
      char b[32];
      snprintf(b, sizeof b, bo->phase >= 2 ? "FINAL PHASE" : "PHASE %d", bo->phase + 1);
      text(b, sz.x * 0.5f, sz.y * 0.36f, u * 2.6f, cw_rgba(1, 0.4f, 0.25f, 1), true);
    }
  }
  if (W.combo_n >= 3) {
    char b[32];
    snprintf(b, sizeof b, "%d HIT COMBO", W.combo_n);
    text(b, sz.x * 0.5f, fmaxf(u * 5.0f, card_bottom + u * 0.5f), u * 1.4f, cw_rgba(1, 0.9f, 0.3f, 1), true);
  }
  if (W.go_flash > 0 && (W.go_flash / 18) % 2 == 0) text("GO  >>>", sz.x - fmaxf(u * 6, text_w("GO  >>>", u * 2.2f) * 0.5f + u * 0.6f), sz.y * 0.4f, u * 2.2f, cw_rgba(1, 1, 1, 1), true);
  if (W.stage == 0 && W.wave == 0 && W.tick - W.st.t0 < 900 && W.phase != PH_CLEAR) { /* the first stage: what the buttons do, under the street */
    cw_font f = cw_ui_font_sans();
    float bs = u * 0.95f, k = W.tick - W.st.t0 > 840 ? (float)(900 - (W.tick - W.st.t0)) / 60.0f : 1.0f;
    const char *one = "{attack}: punch   {kick}: kick   {jump}: jump   {grab}: grab   {special}: spin";
    const char *two = "{attack}: punch   {kick}: kick   {jump}: jump\n{grab}: grab   {special}: spin";
    const char *t = cw_prompt_text_size(f, one, 0, bs).x < sz.x * 0.94f ? one : two;
    cw_vec2 m = cw_prompt_text_size(f, t, 0, bs);
    cw_r2d_round_rect(cw_rect_make(sz.x * 0.5f - m.x * 0.5f - u * 0.6f, sz.y - m.y - u * 1.6f, m.x + u * 1.2f, m.y + u * 0.8f), u * 0.4f, 6, cw_rgba(0, 0, 0, 0.5f * k));
    cw_prompt_text(f, t, 0, cw_v2(sz.x * 0.5f, sz.y - m.y - u * 1.2f), bs, cw_rgba(1, 1, 1, 0.95f * k), CW_ALIGN_CENTER);
  }
  if (W.phase == PH_INTRO) {
    char b[48];
    snprintf(b, sizeof b, "STAGE %d", W.stage + 1);
    text(b, sz.x * 0.5f, sz.y * 0.32f, u * 3.0f, cw_rgba(1, 0.9f, 0.5f, 1), true);
    text(stage_name(W.stage), sz.x * 0.5f, sz.y * 0.32f + u * 3.6f, u * 1.8f, cw_rgba(1, 1, 1, 1), true);
  }
  if (W.phase == PH_CLEAR) { /* the grade card: the stage's numbers come up one by one, then the grade */
    int pts, g = stage_grade(&pts), t = W.phase_t;
    float a = fminf(1, (float)t / 20.0f), y = sz.y * 0.2f, cwid = fminf(sz.x * 0.86f, u * 26);
    float lh = u * 1.5f * grow(u * 1.1f);
    cw_r2d_round_rect(cw_rect_make(sz.x * 0.5f - cwid * 0.5f, y - u * 0.8f, cwid, u * 0.8f + u * 3.4f * grow(u * 2.6f) + lh * 4 + u * 4.4f * grow(u * 2.8f)), u * 0.6f, 8, cw_rgba(0.02f, 0.02f, 0.04f, 0.78f * a));
    text("STAGE CLEAR", sz.x * 0.5f, y, u * 2.6f, cw_rgba(1, 0.9f, 0.4f, a), true);
    y += u * 3.4f * grow(u * 2.6f);
    char b[64];
    const char *lab[4] = { "Time", "Damage taken", "Best combo", "Lives lost" };
    int val[4] = { W.st.clear_t / TPS, W.st.dmg_taken, W.st.combo_max, W.st.deaths };
    for (int k = 0; k < 4; k++) {
      float ak = fminf(1, fmaxf(0, (float)(t - 30 - k * 25) / 15.0f));
      if (k == 0) snprintf(b, sizeof b, "%d:%02d", val[0] / 60, val[0] % 60); else snprintf(b, sizeof b, "%d", val[k]);
      text(lab[k], sz.x * 0.5f - cwid * 0.4f, y, u * 1.1f, cw_rgba(0.85f, 0.85f, 0.85f, ak), false);
      text(b, sz.x * 0.5f + cwid * 0.4f - text_w(b, u * 1.1f), y, u * 1.1f, cw_rgba(1, 1, 1, ak), false);
      y += lh;
    }
    float ag = fminf(1, fmaxf(0, (float)(t - 150) / 20.0f)); /* the grade swells in after the numbers */
    static const char *G[5] = { "S", "A", "B", "C", "D" };
    static const cw_color GC[5] = { { 1, 0.85f, 0.3f, 1 }, { 0.5f, 1, 0.55f, 1 }, { 0.5f, 0.8f, 1, 1 }, { 0.9f, 0.9f, 0.9f, 1 }, { 0.9f, 0.5f, 0.45f, 1 } };
    snprintf(b, sizeof b, "GRADE %s", G[g]);
    text(b, sz.x * 0.5f, y + u * 0.6f, u * (2.2f + 0.6f * ag), cw_rgba(GC[g].r, GC[g].g, GC[g].b, ag), true);
  }
  if (W.phase == PH_OVER) text("GAME OVER", sz.x * 0.5f, sz.y * 0.35f, u * 3.6f, cw_rgba(1, 0.3f, 0.25f, 1), true);
  if (W.phase == PH_WIN) {
    char b[64];
    text("THE STREETS ARE YOURS", sz.x * 0.5f, sz.y * 0.3f, u * 3.2f, cw_rgba(1, 0.9f, 0.4f, 1), true);
    snprintf(b, sizeof b, "SCORE %d", W.total_score);
    text(b, sz.x * 0.5f, sz.y * 0.3f + u * 4.5f, u * 2.0f, cw_rgba(1, 1, 1, 1), true);
  }
  cw_r2d_end();
}

/* ── the character select: the fighters stand in the street behind it; one card per seat along the bottom ── */
static const char *const ROLE[NHEROES][2] = {
  { "strong all-rounder", "hits hard, takes a beating" },
  { "the quickest", "fast feet, fast hands" },
  { "reach and pace", "long kicks, long legs" },
  { "the grappler", "grabs far, throws hardest" },
};
static int hero_slot(int def) { for (int k = 0; k < NHEROES; k++) if (HEROES[k] == def) return k; return 0; }
void ui_pick(void) {
  cw_vec2 sz = cw_r2d_canvas_size();
  float u = sz.y / 36.0f, bs = u * 0.9f;
  static const cw_color PC[4] = { { 0.95f, 0.35f, 0.3f, 1 }, { 1.0f, 0.75f, 0.2f, 1 }, { 0.3f, 0.85f, 0.55f, 1 }, { 0.4f, 0.6f, 1.0f, 1 } };
  cw_font f = cw_ui_font_sans();
  cw_r2d_begin(NULL);
  float y = u * 1.4f;
  text("CHOOSE YOUR FIGHTER", sz.x * 0.5f, y, u * 2.0f, cw_rgba(1, 0.88f, 0.5f, 1), true);
  y += u * 2.4f * grow(u * 2.0f);
  if (g_pick.left > 0) {
    char b[96] = "";
    for (int i = 0; i < W.players; i++) if (!g_pick.ready[i]) { char p[8]; snprintf(p, sizeof p, "%sP%d", b[0] ? ", " : "", i + 1); strncat(b, p, sizeof b - strlen(b) - 1); }
    char line[160];
    snprintf(line, sizeof line, "%s not ready: starting in %d", b, (g_pick.left + TPS - 1) / TPS);
    text(line, sz.x * 0.5f, y, u * 1.0f, cw_rgba(1, 1, 1, 0.95f), true);
  }
  int n = W.players;
  float cw = fminf((sz.x - u * 2.4f) / 4.0f - u, u * 16), gap = u;
  float total = (float)n * cw + (float)(n - 1) * gap, x0 = (sz.x - total) * 0.5f;
  /* the card's height: what its lines need at this text size, measured */
  float lh_name = u * 1.9f * grow(u * 1.6f), lh_role = u * 1.05f * grow(u * 0.9f), lh_p = cw_prompt_text_size(f, "{left|right}: choose", 0, bs).y + u * 0.4f;
  float ch = u * 0.8f + u * 1.2f * grow(u * 0.9f) + lh_name + 2 * lh_role + u * 0.6f + 2 * lh_p + u * 0.6f;
  float cy = sz.y - ch - u * 1.2f;
  for (int i = 0; i < n; i++) {
    float x = x0 + (float)i * (cw + gap), ty = cy + u * 0.5f;
    int h = hero_slot(g_heroes[i]);
    bool rd = g_pick.ready[i];
    cw_r2d_round_rect(cw_rect_make(x, cy, cw, ch), u * 0.4f, 6, cw_rgba(0.02f, 0.02f, 0.04f, rd ? 0.82f : 0.7f));
    cw_r2d_round_rect(cw_rect_make(x, cy, cw, u * 0.25f), u * 0.1f, 4, PC[i]);
    char b[32];
    snprintf(b, sizeof b, "P%d", i + 1);
    text(b, x + cw * 0.5f, ty, u * 0.9f, PC[i], true);
    ty += u * 1.2f * grow(u * 0.9f);
    text(DEFS[HEROES[h]].name, x + cw * 0.5f, ty, u * 1.6f, cw_rgba(1, 1, 1, 1), true);
    ty += lh_name;
    for (int l = 0; l < 2; l++) { text(ROLE[h][l], x + cw * 0.5f, ty, u * 0.9f, cw_rgba(0.85f, 0.85f, 0.85f, 1), true); ty += lh_role; }
    ty += u * 0.6f;
    if (rd) {
      text("READY", x + cw * 0.5f, ty, u * 1.0f, cw_rgba(0.5f, 1, 0.5f, 1), true);
      ty += lh_p;
      cw_prompt_text(f, "{kick}: change", i, cw_v2(x + cw * 0.5f, ty), bs, cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER);
    } else {
      cw_prompt_text(f, "{left|right}: choose", i, cw_v2(x + cw * 0.5f, ty), bs, cw_rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER);
      ty += lh_p;
      cw_prompt_text(f, "{attack}: ready", i, cw_v2(x + cw * 0.5f, ty), bs, cw_rgba(1, 0.9f, 0.5f, 1), CW_ALIGN_CENTER);
    }
  }
  cw_r2d_end();
}
