/* Hurdle Dash (2D, platform dodging): four lanes, the same course for all.
 * A jumps the rocks, Down (or B) ducks the hanging bars. Three lives; the
 * runner who lasts the farthest wins. The course speeds up. */
#include "mg.h"

#define LANES NP
#define MAXOBS 64
typedef struct { float x; int kind; } obs; /* kind 0 rock (jump), 1 bar (duck), 2 tall rock (jump high) */

static struct {
  float dist, speed;
  obs o[MAXOBS];
  int n_obs;
  float next_x;
  struct { float y, vy; bool duck, air; int lives; int inv; float out_at; bool alive; float run; int combo; float look; int passed; } p[LANES];
  int roll[LANES][MAXOBS]; /* a CPU's decision per obstacle: 0 clean, 1 miss */
  uint8_t rolled[LANES][MAXOBS];
} D;

static const float PX = 250, GROUND0 = 230, LANE_H = 142, ROCK_H = 46, JUMP_V = 900, GRAV = 2500;

static void start(mg_ctx *c) {
  memset(&D, 0, sizeof D);
  D.speed = 380;
  D.next_x = 900;
  for (int i = 0; i < LANES; i++) { D.p[i].lives = 3; D.p[i].alive = true; }
  /* the whole course up front: every runner meets the same obstacles */
  D.dist = 0;
  while (D.n_obs < MAXOBS) {
    int k = cw_rng_int(&c->rng, 0, 9);
    obs *o = &D.o[D.n_obs++];
    o->x = D.next_x;
    o->kind = k < 4 ? 0 : k < 7 ? 1 : 2;
    float gap = cw_rng_range(&c->rng, 430, 640);
    D.next_x += gap + (D.next_x - 900) * 0.12f;
  }
}

static bool hits(int i, const obs *o) {
  float h = D.p[i].y;
  if (o->kind == 0) return h < ROCK_H - 6;
  if (o->kind == 2) return h < 86;
  return !D.p[i].duck || h > 6; /* the bar hangs at head height: duck on the ground */
}

static void tick(mg_ctx *c) {
  const float dt = 1.0f / 60.0f;
  int alive = 0;
  D.speed = fminf(380 + c->tick * 0.55f, 720);
  D.dist += D.speed * dt;
  for (int i = 0; i < LANES; i++) {
    __typeof__(D.p[0]) *p = &D.p[i];
    if (!p->alive) continue;
    alive++;
    const mg_in *in = &c->in[i];
    bool ground = p->y <= 0 && p->vy <= 0;
    p->duck = ground && (in->y > 0.5f || in->b);
    if (ground && (in->pa || (in->a && p->air)) && !p->duck) { p->vy = JUMP_V; p->air = true; if (c->human[i]) sfx(SFX_JUMP, 0.5f, 1); }
    if (!ground || p->vy > 0) {
      p->vy -= GRAV * dt;
      p->y += p->vy * dt;
      if (p->y <= 0) {
        p->y = 0; p->vy = 0; p->air = false;
        fx_burst(cw_v2(PX, GROUND0 + i * LANE_H), 5, cw_rgba(1, 1, 1, 0.6f), 120, 0.4f, 5);
      }
    }
    if (p->inv > 0) p->inv--;
    p->run += D.speed * dt;
    /* obstacles */
    for (int k = 0; k < D.n_obs; k++) {
      float dx = D.o[k].x - D.dist;
      if (dx < -60) continue;
      if (dx > 60) break;
      if (fabsf(dx) < 26 && p->inv == 0 && hits(i, &D.o[k])) {
        p->lives--;
        p->inv = 100;
        fx_burst(cw_v2(PX, GROUND0 + i * LANE_H - 30), 14, PCOL[i], 260, 0.7f, 8);
        fx_shake = fmaxf(fx_shake, 6);
        sfx(SFX_HIT, 0.8f, 1);
        if (p->lives <= 0) {
          p->alive = false;
          p->out_at = D.dist;
          c->eliminated[i] = true;
          fx_text(cw_v2(PX, GROUND0 + i * LANE_H - 80), "OUT", PCOL[i], 36);
        }
      }
    }
    p->passed = 0;
  }
  for (int i = 0; i < LANES; i++) {
    float d = D.p[i].alive ? D.dist : D.p[i].out_at;
    c->score[i] = (float)(int)(d / 10.0f) + (D.p[i].alive ? D.p[i].lives * 5.0f : 0);
  }
  if (alive <= 0) c->done = true;
  if (alive == 1 && c->humans == 0 && c->tick > 600) { /* keep playing a little so the survivor shows */ }
}

static void think(mg_ctx *c, int p, mg_in *out) {
  memset(out, 0, sizeof *out);
  int k = 0;
  while (k < D.n_obs && D.o[k].x - D.dist < -30) k++;
  if (k >= D.n_obs) return;
  if (!D.rolled[p][k]) { /* decide once per obstacle whether this CPU slips */
    static const int miss_pct[3] = { 30, 12, 3 };
    int r = cw_rng_int(&c->rng, 0, 99);
    D.roll[p][k] = r < miss_pct[c->skill % 3];
    D.rolled[p][k] = 1;
  }
  if (D.roll[p][k]) return;
  float dx = D.o[k].x - D.dist;
  float lead = D.speed * (D.o[k].kind == 2 ? 0.36f : 0.31f);
  if (D.o[k].kind == 1) out->y = dx < D.speed * 0.25f && dx > -30 ? 1.0f : 0.0f;
  else if (dx < lead && dx > lead - D.speed * 0.08f && D.p[p].y <= 0) out->a = true;
}

static void draw_lane(int i, float t) {
  float gy = GROUND0 + i * LANE_H;
  cw_color c = PCOL[i];
  cw_r2d_rect(cw_rect_make(0, gy - 118, STAGE_W, 118 + 24), col_mix(cw_rgba(0.08f, 0.12f, 0.3f, 1), c, 0.12f));
  cw_r2d_rect(cw_rect_make(0, gy, STAGE_W, 24), col_mix(cw_rgba(0.15f, 0.12f, 0.2f, 1), c, 0.35f));
  cw_r2d_rect(cw_rect_make(0, gy, STAGE_W, 5), col_a(c, 0.9f));
  for (int s = 0; s < 14; s++) { /* scrolling ground ticks */
    float x = fmodf(s * 100.0f - D.dist, 1400) ;
    if (x < 0) x += 1400;
    cw_r2d_rect(cw_rect_make(x - 30, gy + 12, 36, 4), cw_rgba(1, 1, 1, 0.12f));
  }
  (void)t;
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha;
  ui_backdrop(t, cw_rgba(0.35f, 0.7f, 0.95f, 1), cw_rgba(0.85f, 0.9f, 1, 1));
  /* far hills, parallax */
  for (int h = 0; h < 8; h++) {
    float x = fmodf(h * 240.0f - D.dist * 0.15f, 1920) - 300;
    if (x < -300) x += 1920;
    cw_r2d_circle(cw_v2(x, 160), 150, 24, cw_rgba(0.5f, 0.75f, 0.9f, 0.7f));
  }
  for (int i = 0; i < LANES; i++) {
    draw_lane(i, t);
    float gy = GROUND0 + i * LANE_H;
    for (int k = 0; k < D.n_obs; k++) {
      float sx = PX + D.o[k].x - D.dist;
      if (sx < -80 || sx > STAGE_W + 80) continue;
      if (D.o[k].kind == 0) {
        cw_vec2 tri[3] = { cw_v2(sx - 26, gy), cw_v2(sx, gy - ROCK_H), cw_v2(sx + 26, gy) };
        cw_r2d_polygon(tri, 3, cw_rgba(0.55f, 0.5f, 0.6f, 1));
        cw_r2d_polygon_lines(tri, 3, 3, cw_rgba(0.15f, 0.1f, 0.2f, 1));
      } else if (D.o[k].kind == 2) {
        cw_rect r = cw_rect_make(sx - 24, gy - 90, 48, 90);
        ui_panel(r, cw_rgba(0.8f, 0.45f, 0.3f, 1), cw_rgba(0.2f, 0.1f, 0.1f, 1));
      } else {
        cw_r2d_rect(cw_rect_make(sx - 4, gy - 118, 8, 62), cw_rgba(0.3f, 0.3f, 0.4f, 1));
        ui_panel(cw_rect_make(sx - 34, gy - 62, 68, 22), cw_rgba(0.95f, 0.75f, 0.2f, 1), cw_rgba(0.2f, 0.1f, 0.1f, 1));
      }
    }
    __typeof__(D.p[0]) *p = &D.p[i];
    if (p->alive || (c->tick % 20 < 10)) {
      bool blink = p->inv > 0 && (p->inv / 4) % 2 == 0;
      if (!blink || !p->alive) {
        float sq = p->duck ? 0.55f : p->y > 4 ? 1.18f : 1 + 0.06f * sinf(p->run * 0.05f);
        float r = 26;
        float bodyy = gy - r * (p->duck ? 0.6f : 1.0f) - p->y;
        if (p->alive) ui_blob(cw_v2(PX, bodyy), r, i, 0.8f, 0, sq, p->inv > 0 ? 2 : 0, i >= c->humans);
        else ui_blob(cw_v2(PX - (D.dist - p->out_at), gy - r), r, i, 0, 0, 0.8f, 3, i >= c->humans);
      }
    }
    for (int l = 0; l < 3; l++) /* lives as hearts */
      cw_r2d_circle(cw_v2(40 + l * 26, gy - 100), 9, 12, l < p->lives ? cw_rgba(1, 0.3f, 0.4f, 1) : cw_rgba(0, 0, 0, 0.3f));
  }
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "%d m", (int)c->score[p]); }

const mg_def mg_dodge = {
  .id = "hurdle", .name = "Hurdle Dash", .tagline = "Jump the rocks, duck the bars, last the longest.",
  .how = { "A: jump (hold for a quick re-jump)", "Down or B: duck under the bars", "Three lives. The course speeds up." },
  .kind = "2D", .scene = NULL, .seconds = 45, .unlock_points = 0, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
