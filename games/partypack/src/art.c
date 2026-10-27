/* The 2D art: baked lit sprites (tools/bake2d.mjs) drawn either through the
 * engine's lit 2D layer (albedo + normal map, lit by real 2D lights) or flat
 * (the same sprite with the light baked in) for menus and HUD. */
#include "mg.h"

cw_color art_ambient = { 0.55f, 0.55f, 0.65f, 1 };
static bool LIT;
#define NART 192
static struct { const char *key; cw_texture lit, flat; bool tried; } T[NART];
static int nt;

static cw_texture load(const char *name, const char *suffix, bool normal_of) {
  char path[96];
  snprintf(path, sizeof path, "art/%s%s.png", name, suffix);
  cw_texture t = cw_texture_load(path, NULL);
  if (normal_of && cw_texture_valid(t)) {
    snprintf(path, sizeof path, "art/%s_n.png", name);
    cw_texture_load_normal_map(t, path);
  }
  return t;
}
static int slot(const char *name) {
  for (int i = 0; i < nt; i++) if (T[i].key == name) return i;
  for (int i = 0; i < nt; i++) if (!strcmp(T[i].key, name)) { T[i].key = name; return i; }
  if (nt >= NART) return -1;
  int i = nt++;
  T[i].key = name;
  T[i].flat = load(name, "", false);
  T[i].lit = load(name, "_a", true);
  return i;
}
void art_preload(const char **names, int n) { for (int i = 0; i < n; i++) slot(names[i]); }

void art_world(bool lit) {
  LIT = lit;
  cw_r2d_use_layer(lit ? 0 : 1);
}
bool art_is_lit(void) { return LIT; }

void art_draw_ex(const char *name, cw_vec2 pos, cw_vec2 size, cw_vec2 pivot, float rot, cw_color tint, bool fx, bool fy) {
  int i = slot(name);
  if (i < 0) return;
  cw_sprite s = { .texture = LIT ? T[i].lit : T[i].flat, .pos = pos, .size = size, .pivot = pivot, .rotation = rot, .color = tint, .flip_x = fx, .flip_y = fy };
  if (!tint.a) s.color = (cw_color){ 1, 1, 1, 1 };
  cw_r2d_sprite(&s);
}
void art_draw(const char *name, cw_vec2 center, cw_vec2 size, float rot, cw_color tint) {
  art_draw_ex(name, center, size, cw_v2(0.5f, 0.5f), rot, tint, false, false);
}
/* a sprite that always draws flat and unlit (glows, halos, sparkles: drawn additively) */
void art_glow(const char *name, cw_vec2 center, float size, cw_color tint) {
  int i = slot(name);
  if (i < 0) return;
  cw_r2d_set_blend(CW_BLEND_ADD);
  cw_sprite s = { .texture = T[i].flat, .pos = center, .size = cw_v2(size, size), .pivot = cw_v2(0.5f, 0.5f), .color = tint };
  cw_r2d_sprite(&s);
  cw_r2d_set_blend(CW_BLEND_ALPHA);
}
void art_light(cw_vec2 p, float radius, cw_color c, float energy, bool shadows) {
  if (!LIT) return;
  cw_r2d_light(&(cw_light2d){ .pos = p, .radius = radius, .color = c, .energy = energy, .height = radius * 0.35f, .shadows = shadows });
}
/* a stretchable plate: the corners keep their size (margin in texture pixels, scaled by `scale`) */
void art_nine(const char *name, cw_rect dst, float margin, float scale, cw_color tint) {
  int i = slot(name);
  if (i < 0) return;
  cw_nine_slice n = { .texture = LIT ? T[i].lit : T[i].flat, .left = margin, .top = margin, .right = margin, .bottom = margin, .dst = dst, .color = tint, .margin_scale = scale };
  if (!tint.a) n.color = (cw_color){ 1, 1, 1, 1 };
  cw_r2d_nine_slice(&n);
}
