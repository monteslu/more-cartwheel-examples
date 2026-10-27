/* Hurdle Dash (2D, platform dodging): four lanes, the same course for all.
 * A jumps the rocks, Down (or B) ducks the hanging bars. Three lives; the
 * runner who lasts the farthest wins. The course speeds up.
 * A runner who is out becomes a pebble thrower: A drops a rock into the lane
 * of the runner with the most lives left (a few seconds between throws). It
 * can cost that runner a life but never changes an out runner's own score. */
#include "mg.h"

#define LANES NP
#define MAXOBS 64
#define MAXPEB 6 /* thrown rocks alive in one lane at once */
typedef struct { float x; int kind; } obs; /* kind 0 rock (jump), 1 bar (duck), 2 tall rock (jump high) */

static struct {
  float dist, speed;
  obs o[MAXOBS];
  int n_obs;
  float next_x;
  struct { float y, vy; bool duck, air; int lives; int inv; float out_at; bool alive; float run; int combo; float look; int passed; } p[LANES];
  struct { float x; int by; int roll; } peb[LANES][MAXPEB]; /* x < 0: free slot */
  int throw_cd[LANES], throws[LANES];
  int roll[LANES][MAXOBS]; /* a CPU's decision per obstacle: 0 clean, 1 miss */
  uint8_t rolled[LANES][MAXOBS];
} D;

static const float PX = 250, GROUND0 = 230, LANE_H = 142, ROCK_H = 46, JUMP_V = 900, GRAV = 2500;

static void start(mg_ctx *c) {
  memset(&D, 0, sizeof D);
  D.speed = 380;
  D.next_x = 900;
  for (int i = 0; i < LANES; i++) {
    D.p[i].lives = 3; D.p[i].alive = true;
    for (int k = 0; k < MAXPEB; k++) D.peb[i][k].x = -1;
  }
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
        fx_hitstop(p->lives <= 0 ? 8 : 4);
        sfx(SFX_HIT, 0.8f, 1);
        if (p->lives <= 0) {
          p->alive = false;
          p->out_at = D.dist;
          c->eliminated[i] = true;
          fx_text(cw_v2(PX, GROUND0 + i * LANE_H - 80), "OUT", PCOL[i], 36);
        }
      }
    }
    for (int k = 0; k < MAXPEB; k++) {
      if (D.peb[i][k].x < 0) continue;
      float dx = D.peb[i][k].x - D.dist;
      if (dx < -60) { D.peb[i][k].x = -1; continue; }
      obs po = { D.peb[i][k].x, 0 };
      if (fabsf(dx) < 26 && p->inv == 0 && hits(i, &po)) {
        p->lives--;
        p->inv = 100;
        D.peb[i][k].x = -1;
        fx_burst(cw_v2(PX, GROUND0 + i * LANE_H - 30), 14, PCOL[D.peb[i][k].by], 260, 0.7f, 8);
        fx_shake = fmaxf(fx_shake, 6);
        fx_hitstop(p->lives <= 0 ? 8 : 4);
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
  /* out runners throw pebbles */
  for (int g = 0; g < LANES; g++) {
    if (D.p[g].alive) continue;
    if (D.throw_cd[g] > 0) D.throw_cd[g]--;
    if (!c->in[g].pa || D.throw_cd[g] > 0) continue;
    /* the runner with the most lives; a tie is drawn from the game's rng (not the lowest seat) */
    int most = 0, cand[LANES], nc = 0;
    for (int i = 0; i < LANES; i++) if (D.p[i].alive && D.p[i].lives > most) most = D.p[i].lives;
    for (int i = 0; i < LANES; i++) if (D.p[i].alive && D.p[i].lives == most) cand[nc++] = i;
    int tgt = nc ? cand[cw_rng_int(&c->rng, 0, nc - 1)] : -1;
    if (tgt < 0) continue;
    for (int k = 0; k < MAXPEB; k++) {
      if (D.peb[tgt][k].x >= 0) continue;
      D.peb[tgt][k].x = D.dist + 620;
      D.peb[tgt][k].by = g;
      D.peb[tgt][k].roll = cw_rng_int(&c->rng, 0, 99);
      D.throw_cd[g] = 150;
      D.throws[g]++;
      sfx(SFX_POP, 0.5f, 1);
      fx_text(cw_v2(STAGE_W - 120, GROUND0 + tgt * LANE_H - 60), "!", PCOL[g], 34);
      break;
    }
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
  if (!D.p[p].alive) { /* a CPU ghost throws now and then */
    out->a = D.throw_cd[p] == 0 && cw_rng_int(&c->rng, 0, 99) < 4;
    return;
  }
  /* a thrown rock in my lane that is nearer than the next course obstacle: a jump, like a rock */
  for (int q = 0; q < MAXPEB; q++) {
    if (D.peb[p][q].x < 0) continue;
    float pdx = D.peb[p][q].x - D.dist;
    if (pdx < -30 || D.peb[p][q].roll < (int[3]){ 30, 12, 3 }[c->cpu_skill[p]]) continue;
    float plead = D.speed * 0.31f;
    if (pdx < plead && pdx > plead - D.speed * 0.08f && D.p[p].y <= 0) { out->a = true; return; }
  }
  int k = 0;
  while (k < D.n_obs && D.o[k].x - D.dist < -30) k++;
  if (k >= D.n_obs) return;
  if (!D.rolled[p][k]) { /* decide once per obstacle whether this CPU slips */
    static const int miss_pct[3] = { 30, 12, 3 };
    int r = cw_rng_int(&c->rng, 0, 99);
    D.roll[p][k] = r < miss_pct[c->cpu_skill[p]];
    D.rolled[p][k] = 1;
  }
  if (D.roll[p][k]) return;
  float dx = D.o[k].x - D.dist;
  float lead = D.speed * (D.o[k].kind == 2 ? 0.36f : 0.31f);
  if (D.o[k].kind == 1) out->y = dx < D.speed * 0.25f && dx > -30 ? 1.0f : 0.0f;
  else if (dx < lead && dx > lead - D.speed * 0.08f && D.p[p].y <= 0) out->a = true;
}

static void draw_lane(int i) {
  float gy = GROUND0 + i * LANE_H;
  cw_color c = PCOL[i];
  /* sky for this lane, tinted by the runner's colour, with three ridge layers scrolling at different speeds */
  ui_grad(cw_rect_make(0, gy - 118, STAGE_W, 118 + 10), col_mix(cw_rgba(0.38f, 0.62f, 0.95f, 1), c, 0.18f), col_mix(cw_rgba(0.86f, 0.9f, 1, 1), c, 0.1f));
  bg_ridge("ridge_far", D.dist * 0.08f, gy, 96, cw_rgba(1, 1, 1, 1));
  bg_ridge("ridge_mid", D.dist * 0.2f, gy, 76, cw_rgba(1, 1, 1, 1));
  bg_ridge("ridge_near", D.dist * 0.45f, gy, 52, cw_rgba(1, 1, 1, 1));
  /* the running track: worn rubber, lit, with a lane stripe in the runner's colour */
  float tw = 144, x0 = -fmodf(D.dist * 0.9f, tw);
  for (float x = x0; x < STAGE_W; x += tw) art_draw_ex("track", cw_v2(x, gy - 6), cw_v2(tw + 1, 30), cw_v2(0, 0), 0, (cw_color){ 0 }, false, false);
  cw_r2d_rect(cw_rect_make(0, gy - 6, STAGE_W, 4), col_a(c, 0.95f));
  art_light(cw_v2(PX + 40, gy - 70), 300, col_mix(c, cw_rgba(1, 1, 1, 1), 0.35f), 0.6f, false);
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha;
  art_ambient = cw_rgba(0.5f, 0.52f, 0.62f, 1);
  cw_r2d_rect(cw_rect_make(0, 0, STAGE_W, STAGE_H), cw_rgba(0.1f, 0.14f, 0.3f, 1));
  ui_grad(cw_rect_make(0, 0, STAGE_W, 150), cw_rgba(0.3f, 0.55f, 0.92f, 1), cw_rgba(0.6f, 0.78f, 0.98f, 1));
  bg_clouds(t, 20, 1.0f, 0.9f);
  art_light(cw_v2(STAGE_W * 0.35f, -140), 2000, cw_rgba(1, 0.93f, 0.8f, 1), 0.4f, false);
  for (int i = 0; i < LANES; i++) {
    draw_lane(i);
    float gy = GROUND0 + i * LANE_H;
    for (int k = 0; k < D.n_obs; k++) {
      float sx = PX + D.o[k].x - D.dist;
      if (sx < -80 || sx > STAGE_W + 80) continue;
      cw_vec2 sh = cw_v2(sx + 6, gy + 6);
      if (D.o[k].kind == 0) {
        art_draw_ex("rock", cw_v2(sx, gy + 6), cw_v2(64, 60), cw_v2(0.5f, 1), 0, (cw_color){ 0 }, false, false);
      } else if (D.o[k].kind == 2) {
        art_draw_ex("rock_tall", cw_v2(sx, gy + 6), cw_v2(56, 96), cw_v2(0.5f, 1), 0, (cw_color){ 0 }, false, false);
      } else {
        art_draw_ex("post", cw_v2(sx, gy - 118), cw_v2(12, 66), cw_v2(0.5f, 0), 0, (cw_color){ 0 }, false, false);
        art_draw("bar", cw_v2(sx, gy - 51), cw_v2(76, 27), 0, (cw_color){ 0 });
      }
      (void)sh;
    }
    for (int k = 0; k < MAXPEB; k++) {
      if (D.peb[i][k].x < 0) continue;
      float sx = PX + D.peb[i][k].x - D.dist;
      if (sx < -80 || sx > STAGE_W + 80) continue;
      art_draw_ex("rock", cw_v2(sx, gy + 6), cw_v2(64, 60), cw_v2(0.5f, 1), 0, col_a(PCOL[D.peb[i][k].by], 0.55f), false, false);
    }
    __typeof__(D.p[0]) *p = &D.p[i];
    if (p->alive || (c->tick % 20 < 10)) {
      bool blink = p->inv > 0 && (p->inv / 4) % 2 == 0;
      if (!blink || !p->alive) {
        float sq = p->duck ? 0.55f : p->y > 4 ? 1.18f : 1 + 0.06f * sinf(p->run * 0.05f);
        float r = 26;
        float bodyy = gy - r * (p->duck ? 0.6f : 1.0f) - p->y;
        /* a soft contact shadow that shrinks as the runner jumps */
        float sk = 1 - fminf(p->y / 220.0f, 0.6f);
        art_draw("shadow", cw_v2(PX, gy + 12), cw_v2(70 * sk, 18 * sk), 0, cw_rgba(1, 1, 1, 0.7f * sk));
        if (p->alive) ui_blob(cw_v2(PX, bodyy), r, i, 0.8f, 0, sq, p->inv > 0 ? 2 : 0, !c->human[i]);
        else ui_blob(cw_v2(PX - (D.dist - p->out_at), gy - r), r, i, 0, 0, 0.8f, 3, !c->human[i]);
      }
    }
    if (!p->alive && c->tick % 40 < 28) ui_text(D.throw_cd[i] > 0 ? "ghost" : "A: throw a rock", cw_v2(STAGE_W / 2, gy - 40), 24, col_a(c->human[i] ? PCOL[i] : cw_rgba(1, 1, 1, 1), 0.9f), CW_ALIGN_CENTER);
    for (int l = 0; l < 3; l++) /* lives */
      art_draw("star", cw_v2(46 + l * 34, gy - 98), cw_v2(30, 30), 0, l < p->lives ? cw_rgba(1, 0.45f, 0.55f, 1) : cw_rgba(0.1f, 0.1f, 0.14f, 0.55f));
  }
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "%d m", (int)c->score[p]); }

const mg_def mg_dodge = {
  .id = "hurdle", .name = "Hurdle Dash", .tagline = "Jump the rocks, duck the bars, last the longest.",
  .how = { "A: jump (hold for a quick re-jump)", "Down or B: duck under the bars", "Out? A throws a rock at the leader" },
  .buttons = 1, .keys = "AS",
  .kind = "2D", .scene = NULL, .seconds = 45, .unlock_points = 0, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
