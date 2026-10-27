/* Sumo Push (2D, last standing): four blobs on a round plate that shrinks. Shove the others off the edge;
 * A is a dash that hits hard. Out players become ghosts at the rim: left and right slide the ghost around the
 * edge and A sends a bump inward, so nobody sits and waits. The last blob on the plate wins; the rest rank by how
 * long they lasted. */
#include "mg.h"

#define CX 640.0f
#define CY 410.0f
#define R0 270.0f
#define BR 30.0f
static struct {
  struct { float x, y, vx, vy, fx, fy; int cd, dash; bool alive; int out_t; float ang; int gcd; } b[NP];
  float radius;
  int alive;
} S;

static void start(mg_ctx *c) {
  (void)c;
  memset(&S, 0, sizeof S);
  S.radius = R0;
  S.alive = NP;
  for (int i = 0; i < NP; i++) {
    float a = TAU * (float)i / NP + 0.7854f;
    S.b[i].x = CX + cosf(a) * 150;
    S.b[i].y = CY + sinf(a) * 150;
    S.b[i].alive = true;
    S.b[i].fx = -cosf(a);
    S.b[i].fy = -sinf(a);
    S.b[i].ang = a;
  }
}

static void tick(mg_ctx *c) {
  const float dt = 1.0f / 60.0f;
  /* the plate shrinks after 8 s, in steps you can see coming */
  if (c->tick > 480) S.radius = fmaxf(120, R0 - (float)(c->tick - 480) * 0.11f);
  for (int i = 0; i < NP; i++) {
    __typeof__(S.b[0]) *b = &S.b[i];
    const mg_in *in = &c->in[i];
    if (!b->alive) { /* ghost on the rim */
      b->ang += in->x * 2.6f * dt;
      if (b->gcd > 0) b->gcd--;
      if (in->pa && b->gcd == 0) {
        b->gcd = 150;
        float gx = CX + cosf(b->ang) * (S.radius + 40), gy = CY + sinf(b->ang) * (S.radius + 40);
        fx_ring(cw_v2(gx, gy), 10, 90, 0.4f, col_a(PCOL[i], 0.9f));
        sfx(SFX_BOUNCE, 0.4f, 1.3f);
        for (int k = 0; k < NP; k++) {
          if (!S.b[k].alive) continue;
          float dx = S.b[k].x - gx, dy = S.b[k].y - gy, d = sqrtf(dx * dx + dy * dy);
          if (d < 260 && d > 1) { S.b[k].vx += dx / d * 190; S.b[k].vy += dy / d * 190; }
        }
      }
      continue;
    }
    float ax = in->x, ay = in->y, l = sqrtf(ax * ax + ay * ay);
    if (l > 1) ax /= l, ay /= l;
    b->vx += ax * 1250 * dt;
    b->vy += ay * 1250 * dt;
    if (l > 0.3f) b->fx = ax / (l > 1 ? 1 : l), b->fy = ay / (l > 1 ? 1 : l);
    if (b->cd > 0) b->cd--;
    if (b->dash > 0) b->dash--;
    if (in->pa && b->cd == 0) {
      float fl = sqrtf(b->fx * b->fx + b->fy * b->fy);
      if (fl < 0.01f) fl = 1;
      b->vx += b->fx / fl * 700;
      b->vy += b->fy / fl * 700;
      b->cd = 70;
      b->dash = 14;
      sfx(SFX_JUMP, 0.5f, 1.4f);
      fx_burst(cw_v2(b->x, b->y), 6, col_a(PCOL[i], 0.8f), 160, 0.3f, 5);
    }
    float sp = sqrtf(b->vx * b->vx + b->vy * b->vy), cap = b->dash > 0 ? 780 : 330;
    if (sp > cap) { b->vx *= cap / sp; b->vy *= cap / sp; }
    b->vx *= 0.93f;
    b->vy *= 0.93f;
    b->x += b->vx * dt;
    b->y += b->vy * dt;
  }
  /* blob against blob */
  for (int i = 0; i < NP; i++)
    for (int j = i + 1; j < NP; j++) {
      __typeof__(S.b[0]) *a = &S.b[i], *b = &S.b[j];
      if (!a->alive || !b->alive) continue;
      float dx = b->x - a->x, dy = b->y - a->y, d = sqrtf(dx * dx + dy * dy);
      if (d >= BR * 2 || d < 0.01f) continue;
      float nx = dx / d, ny = dy / d, over = BR * 2 - d;
      a->x -= nx * over * 0.5f; a->y -= ny * over * 0.5f;
      b->x += nx * over * 0.5f; b->y += ny * over * 0.5f;
      float rel = (b->vx - a->vx) * nx + (b->vy - a->vy) * ny;
      float ja = a->dash > 0 ? 420 : 0, jb = b->dash > 0 ? 420 : 0;
      float pa_ = (rel < 0 ? -rel * 0.9f : 0) + jb, pb_ = (rel < 0 ? -rel * 0.9f : 0) + ja;
      a->vx -= nx * pa_; a->vy -= ny * pa_;
      b->vx += nx * pb_; b->vy += ny * pb_;
      if (ja + jb > 0 || -rel > 200) {
        fx_burst(cw_v2((a->x + b->x) * 0.5f, (a->y + b->y) * 0.5f), 10, cw_rgba(1, 1, 0.8f, 1), 220, 0.4f, 6);
        fx_shake = fmaxf(fx_shake, 5);
        fx_hitstop(ja + jb > 0 ? 4 : 2);
        sfx(SFX_HIT, 0.6f, 1.1f);
      }
    }
  for (int i = 0; i < NP; i++) {
    __typeof__(S.b[0]) *b = &S.b[i];
    if (!b->alive) continue;
    float dx = b->x - CX, dy = b->y - CY, d = sqrtf(dx * dx + dy * dy);
    if (d > S.radius + 4) { /* off the plate */
      b->alive = false;
      b->out_t = c->tick;
      b->ang = atan2f(dy, dx);
      b->gcd = 90;
      S.alive--;
      c->eliminated[i] = true;
      c->score[i] = (float)(c->tick / 6);
      sfx(SFX_SPLASH, 0.8f, 0.9f);
      fx_burst(cw_v2(b->x, b->y), 16, PCOL[i], 260, 0.7f, 8);
      fx_text(cw_v2(CX + dx * 0.9f, CY + dy * 0.9f), "OUT", PCOL[i], 36);
      fx_hitstop(5);
    }
  }
  /* the survivors' score ticks up while they are in, so the strip reads right; the last one standing gets a bonus */
  for (int i = 0; i < NP; i++) if (S.b[i].alive) c->score[i] = (float)(c->tick / 6);
  if (S.alive <= 1) {
    for (int i = 0; i < NP; i++) if (S.b[i].alive) c->score[i] += 100;
    c->done = true;
  }
}

static void think(mg_ctx *c, int p, mg_in *out) {
  memset(out, 0, sizeof *out);
  __typeof__(S.b[0]) *me = &S.b[p];
  uint32_t h = hash_u32((uint32_t)p * 77u + 5u, (uint32_t)c->tick);
  if (!me->alive) { /* ghost: circle round to the thickest crowd and bump now and then */
    float best = 0, bx = 0;
    for (int k = 0; k < NP; k++) if (S.b[k].alive) { bx = atan2f(S.b[k].y - CY, S.b[k].x - CX); best = 1; break; }
    if (best > 0) {
      float da = bx - me->ang;
      while (da > 3.14159f) da -= TAU;
      while (da < -3.14159f) da += TAU;
      out->x = da > 0.1f ? 1 : da < -0.1f ? -1 : 0;
    }
    out->a = me->gcd == 0 && (h % 100) < 8;
    return;
  }
  static const float margin[3] = { 40, 75, 110 };
  static const int dash_pct[3] = { 3, 7, 14 };
  float mx = CX - me->x, my = CY - me->y, dc = sqrtf(mx * mx + my * my);
  int tgt = -1;
  float bd = 1e9f;
  for (int k = 0; k < NP; k++) {
    if (k == p || !S.b[k].alive) continue;
    float d = hypotf(S.b[k].x - me->x, S.b[k].y - me->y);
    if (d < bd) bd = d, tgt = k;
  }
  if (dc > S.radius - margin[c->cpu_skill[p]]) { /* too near the edge: back to the middle */
    out->x = mx / dc; out->y = my / dc;
  } else if (tgt >= 0) {
    float dx = S.b[tgt].x - me->x, dy = S.b[tgt].y - me->y, d = bd;
    /* come at the target from the middle side, so a hit pushes it outward */
    float tx = S.b[tgt].x - CX, ty = S.b[tgt].y - CY, tl = sqrtf(tx * tx + ty * ty) + 0.001f;
    float gx = S.b[tgt].x - tx / tl * 46, gy = S.b[tgt].y - ty / tl * 46;
    dx = gx - me->x; dy = gy - me->y;
    float dl = sqrtf(dx * dx + dy * dy) + 0.001f;
    out->x = dx / dl; out->y = dy / dl;
    if (d < 120 && me->cd == 0 && (int)(h % 100) < dash_pct[c->cpu_skill[p]] * 2) { out->a = true; out->x = (S.b[tgt].x - me->x) / d; out->y = (S.b[tgt].y - me->y) / d; }
  }
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha;
  ui_backdrop(t, cw_rgba(0.2f, 0.12f, 0.35f, 1), cw_rgba(0.06f, 0.05f, 0.15f, 1));
  /* the plate: its edge, a warning band, and a floor that shows how far it has shrunk */
  cw_r2d_circle(cw_v2(CX, CY + 26), R0 + 14, 48, cw_rgba(0, 0, 0, 0.3f));
  cw_r2d_circle(cw_v2(CX, CY), S.radius + 10, 48, cw_rgba(0.95f, 0.45f, 0.3f, 1));
  cw_r2d_circle(cw_v2(CX, CY), S.radius, 48, cw_rgba(0.28f, 0.22f, 0.45f, 1));
  for (int k = 1; k < 4; k++) cw_r2d_circle_lines(cw_v2(CX, CY), S.radius * (float)k / 4.0f, 2, 40, cw_rgba(1, 1, 1, 0.07f));
  if (c->tick > 420 && c->tick < 480 && (c->tick / 8) % 2 == 0) cw_r2d_circle_lines(cw_v2(CX, CY), S.radius - 6, 6, 48, cw_rgba(1, 0.4f, 0.3f, 0.8f));
  for (int i = 0; i < NP; i++) {
    __typeof__(S.b[0]) *b = &S.b[i];
    if (!b->alive) { /* the ghost at the rim */
      float gx = CX + cosf(b->ang) * (S.radius + 44), gy = CY + sinf(b->ang) * (S.radius + 44);
      ui_blob(cw_v2(gx, gy), 20, i, 0, 0, 1, b->gcd > 0 ? 3 : 1, !c->human[i]);
      cw_r2d_circle_lines(cw_v2(gx, gy), 26, 2, 16, col_a(PCOL[i], b->gcd > 0 ? 0.3f : 0.9f));
      continue;
    }
    float sq = b->dash > 0 ? 0.8f : 1;
    ui_blob(cw_v2(b->x, b->y), BR, i, b->fx, b->fy, sq, b->dash > 0 ? 2 : 0, !c->human[i]);
    float rdy = 1 - (float)b->cd / 70.0f;
    cw_r2d_rect(cw_rect_make(b->x - 22, b->y + BR + 8, 44 * rdy, 5), col_a(PCOL[i], rdy >= 1 ? 1.0f : 0.6f));
  }
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "%s", S.b[p].alive ? "in" : (c->human[p] ? "ghost" : "out")); }

const mg_def mg_sumo = {
  .id = "sumo", .name = "Sumo Push", .tagline = "Shove everyone off the shrinking plate. Last blob on it wins.",
  .how = { "Stick: move", "A: dash and shove (then it recharges)", "Out? Slide your ghost round the rim, A bumps them" },
  .buttons = 1, .keys = "SA",
  .kind = "2D", .scene = NULL, .seconds = 50, .unlock_points = 0, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
