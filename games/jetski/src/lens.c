/* Water on the lens: rain, and the spray of a hard landing or a big crest, beads on the camera's glass. Each drop is a
 * small lens of its own: the picture seen through it is the scene behind it turned over and drawn small, with a dark
 * rim and a glint, as a real drop on glass. They run down and, at speed, out to the edges in the wind of the ride, and
 * clear in a second or two, so they never hold the way ahead for long: few are let into the middle third, and none
 * after the drops there cover a tenth of it. sw lens 0: no drops, the lens gate's control. */
#include <math.h>
#include <string.h>

#include "cartwheel/render2d.h"
#include "game.h"

#define DROPS 64
typedef struct drop { float x, y, r, t, life, vx, vy; } drop;
static drop D[DROPS];
static uint32_t lens_seed = 0x6a09e667u;
int g_lens_on = 1;
float g_lens[3]; /* drops now, the share of the middle third they cover, drops ever made */

static float rnd(void) { lens_seed = lens_seed * 1664525u + 1013904223u; return (float)(lens_seed >> 8) / 16777216.0f; }
static float centre_cover(void) {
  float a = 0;
  for (int i = 0; i < DROPS; i++) if (D[i].life > 0 && D[i].x > 0.333f && D[i].x < 0.667f && D[i].y > 0.333f && D[i].y < 0.667f) a += 3.14159f * D[i].r * D[i].r * 16.0f / 9.0f;
  return a / (1.0f / 9.0f);
}
static void spawn(float big) {
  for (int i = 0; i < DROPS; i++) {
    if (D[i].life > 0) continue;
    float x = rnd(), y = rnd() * 0.9f;
    /* the middle third, where the course is, takes few */
    if (x > 0.3f && x < 0.7f && y > 0.28f && y < 0.72f && (rnd() < 0.75f || centre_cover() > 0.08f)) return;
    D[i] = (drop){ x, y, (0.014f + 0.02f * rnd()) * (1.0f + big), 0, 1.2f + 1.4f * rnd(), 0, 0 };
    g_lens[2] += 1;
    return;
  }
}
void lens_reset(void) { memset(D, 0, sizeof D); }
void lens_tick(float dt) {
  if (!g_lens_on || G.state == RACE_ATTRACT) { lens_reset(); return; }
  const craft *c = &G.c[0];
  /* rain on the glass, more in a storm's wind */
  if (wx_rain() && G.state == RACE_ON) { float want = 7.0f * dt; while (want > 0) { if (rnd() < want) spawn(0); want -= 1; } }
  /* a hard landing or a dive throws a sheet over the camera */
  static bool was_air;
  if (was_air && !c->air && c->kick > 0.45f && c->speed > 6) { int n = 6 + (int)(10 * c->kick); for (int k = 0; k < n; k++) spawn(0.6f * rnd()); }
  was_air = c->air;
  if (c->spill > 1.85f || c->dive_t > 0.2f) spawn(0.8f);
  float sp = fminf(1.0f, c->speed / 25.0f);
  int n = 0;
  for (int i = 0; i < DROPS; i++) {
    drop *d = &D[i];
    if (d->life <= 0) continue;
    d->t += dt;
    /* down the glass, and at speed out to the edges in the ride's wind, the big ones fastest */
    float ox = d->x - 0.5f, oy = d->y - 0.45f, ol = sqrtf(ox * ox + oy * oy) + 1e-3f;
    d->vx += (ox / ol) * sp * 0.9f * dt, d->vy += ((oy / ol) * sp * 0.6f + 0.25f * (d->r > 0.012f ? 1.0f : 0.3f)) * dt;
    d->x += d->vx * dt, d->y += d->vy * dt;
    if (d->t > d->life || d->x < -0.05f || d->x > 1.05f || d->y > 1.05f) { d->life = 0; continue; }
    n++;
  }
  g_lens[0] = (float)n, g_lens[1] = centre_cover();
}
void lens_draw(void) {
  if (!g_lens_on) return;
  cw_vec2 sz = cw_r2d_canvas_size();
  float w = sz.x, h = sz.y;
  cw_texture scr = cw_r2d_screen_texture();
  for (int i = 0; i < DROPS; i++) {
    const drop *d = &D[i];
    if (d->life <= 0) continue;
    float fade = fminf(1.0f, d->t / 0.08f) * fminf(1.0f, (d->life - d->t) / 0.5f);
    float cx = d->x * w, cy = d->y * h, r = d->r * h;
    /* stretched along its run as it slides */
    float vl = sqrtf(d->vx * d->vx + d->vy * d->vy), ex = 1.0f + fminf(1.5f, vl * 4.0f);
    float ux = vl > 1e-3f ? d->vx / vl : 0, uy = vl > 1e-3f ? d->vy / vl : 1;
    enum { SEG = 18 };
    cw_vertex2d v[SEG + 1];
    uint32_t idx[SEG * 3];
    /* the scene through the drop: turned over and drawn small, from a patch three times its size */
    v[0] = (cw_vertex2d){ { cx, cy }, { d->x, d->y }, cw_rgba(0.92f, 0.95f, 1.0f, 0.85f * fade) };
    for (int k = 0; k < SEG; k++) {
      float a = (float)k * 6.2831853f / SEG, ca = cosf(a), sa = sinf(a);
      /* along the run: longer behind, a round head in front */
      float along = ca * ux + sa * uy, stretch = along < 0 ? ex : 1.0f;
      float px = cx + ca * r + ux * (along < 0 ? along * r * (stretch - 1.0f) : 0);
      float py = cy + sa * r + uy * (along < 0 ? along * r * (stretch - 1.0f) : 0);
      float su = d->x - ca * d->r * 3.0f * 9.0f / 16.0f, sv = d->y - sa * d->r * 3.0f;
      v[k + 1] = (cw_vertex2d){ { px, py }, { su, sv }, cw_rgba(0.35f, 0.4f, 0.46f, 0.95f * fade) }; /* the rim: darker */
      idx[k * 3] = 0, idx[k * 3 + 1] = (uint32_t)(k + 1), idx[k * 3 + 2] = (uint32_t)((k + 1) % SEG + 1);
    }
    cw_r2d_triangles(scr, v, SEG + 1, idx, SEG * 3);
    /* the glint of the light behind the camera, high on the drop */
    cw_r2d_circle(cw_v2(cx - r * 0.35f, cy - r * 0.4f), r * 0.14f, 8, cw_rgba(1, 1, 1, 0.6f * fade));
  }
}
