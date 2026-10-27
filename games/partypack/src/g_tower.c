/* Stack Attack (2D physics): each player runs a crane over their own
 * platform. Stick moves it, A drops the block, B turns it. Build the
 * tallest tower that stands when the clock runs out; blocks that fall off
 * are lost. The physics is real: lean too far and the pile topples. */
#include "mg.h"
#include "cartwheel/physics2d.h"

#define MAXB 16
#define PPM 80.0f /* stage pixels per metre */
#define GROUND_Y 660.0f

typedef struct { float w, h; } shape_t;
static const shape_t SHAPES[5] = { { 1.7f, 0.4f }, { 0.8f, 0.8f }, { 0.5f, 1.1f }, { 1.1f, 0.55f }, { 0.6f, 0.6f } };

static struct {
  cw_world2d w[NP]; /* one physics world per player: a block that topples off one tower can never prop up or knock
                     * down a neighbour's, so no seat gains from where it stands in the row */
  cw_body2d ground[NP];
  struct { cw_body2d body; int shape, age; bool rot, alive; } blk[NP][MAXB];
  int dropped[NP], next_shape[NP][MAXB + 1], cur[NP];
  bool turned[NP];
  float cx[NP];       /* crane x in metres within the column, -1.4..1.4 */
  int cooldown[NP];
  int height_cm[NP];
  int cpu_plan_t[NP];
  float cpu_aim[NP];
  bool cpu_turn[NP];
  int lost[NP], calm;
  uint32_t seed;
} T;

static float col_x(int p) { return 2.0f + 4.0f * (float)p; } /* metres */
static float to_px_x(float m) { return m * PPM; }
static float to_px_y(float m) { return GROUND_Y - m * PPM; }

static void start(mg_ctx *c) {
  memset(&T, 0, sizeof T);
  cw_world2d_def d = cw_world2d_default_def();
  d.gravity = cw_v2(0, -14);
  d.workers = 1;
  for (int p = 0; p < NP; p++) {
    T.w[p] = cw_world2d_create(&d);
    cw_body2d_def bd = cw_body2d_default_def();
    bd.type = CW_BODY_STATIC;
    bd.position = (cw_pos2){ col_x(p), -0.25 };
    T.ground[p] = cw_body2d_create(T.w[p], &bd);
    cw_shape2d_def sd = cw_shape2d_default_def();
    sd.material.friction = 0.9f;
    cw_shape2d_box(T.ground[p], &sd, cw_v2(1.35f, 0.25f), cw_xform2_identity());
    for (int i = 0; i <= MAXB; i++) T.next_shape[p][i] = 0;
    T.cx[p] = 0;
    T.cooldown[p] = 30;
  }
  /* every player gets the same block sequence */
  for (int i = 0; i <= MAXB; i++) {
    int s = cw_rng_int(&c->rng, 0, 4);
    for (int p = 0; p < NP; p++) T.next_shape[p][i] = s;
  }
  T.seed = cw_rng_u32(&c->rng);
}

static void stop(mg_ctx *c) {
  (void)c;
  for (int p = 0; p < NP; p++) if (T.w[p].id) cw_world2d_destroy(T.w[p]);
  memset(&T, 0, sizeof T);
}

static void drop(mg_ctx *c, int p) {
  if (T.dropped[p] >= MAXB || T.cooldown[p] > 0) return;
  int s = T.next_shape[p][T.dropped[p]];
  float w = SHAPES[s].w, h = SHAPES[s].h;
  if (T.turned[p]) { float t = w; w = h; h = t; }
  cw_body2d_def bd = cw_body2d_default_def();
  bd.position = (cw_pos2){ col_x(p) + T.cx[p], 7.4 };
  bd.linear_damping = 0.05f;
  cw_body2d b = cw_body2d_create(T.w[p], &bd);
  cw_shape2d_def sd = cw_shape2d_default_def();
  sd.material.friction = 0.8f;
  sd.density = 1;
  cw_shape2d_box(b, &sd, cw_v2(w * 0.5f, h * 0.5f), cw_xform2_identity());
  T.blk[p][T.dropped[p]].body = b;
  T.blk[p][T.dropped[p]].shape = s;
  T.blk[p][T.dropped[p]].rot = T.turned[p];
  T.blk[p][T.dropped[p]].alive = true;
  T.blk[p][T.dropped[p]].age = 0;
  T.dropped[p]++;
  T.turned[p] = false;
  T.cooldown[p] = 50;
  if (c->human[p]) sfx(SFX_BLIP, 0.5f, 0.8f);
}

static void tick(mg_ctx *c) {
  const float dt = 1.0f / 60.0f;
  for (int p = 0; p < NP; p++) {
    const mg_in *in = &c->in[p];
    T.cx[p] = fclampf(T.cx[p] + in->x * 3.2f * dt, -1.35f, 1.35f);
    if (in->pb || in->pc) T.turned[p] = !T.turned[p];
    if (T.cooldown[p] > 0) T.cooldown[p]--;
    if (in->pa) drop(c, p);
  }
  for (int p = 0; p < NP; p++) cw_world2d_step(T.w[p], dt);
  for (int p = 0; p < NP; p++) {
    int top = 0;
    for (int i = 0; i < T.dropped[p]; i++) {
      if (T.blk[p][i].alive) T.blk[p][i].age++;
      if (!T.blk[p][i].alive) continue;
      cw_xform2 x = cw_body2d_transform(T.blk[p][i].body);
      if (x.p.y < -1.0f) { /* fell off */
        cw_body2d_destroy(T.blk[p][i].body);
        T.blk[p][i].alive = false;
        T.lost[p]++;
        sfx(SFX_HIT, 0.25f, 0.7f);
        fx_burst(cw_v2(to_px_x(x.p.x), GROUND_Y + 10), 6, PCOL[p], 120, 0.5f, 5);
        continue;
      }
      cw_vec2 v = cw_body2d_velocity(T.blk[p][i].body);
      cw_aabb2 bb = cw_body2d_bounds(T.blk[p][i].body);
      float dx = x.p.x - col_x(p);
      if (T.blk[p][i].age > 50 && v.x * v.x + v.y * v.y < 1.0f && fabsf(dx) < 1.6f && bb.hi.y > 0.1f) {
        int cm = (int)(bb.hi.y * 100.0f);
        if (cm > top) top = cm;
      }
    }
    if (top > T.height_cm[p] + 4) fx_burst(cw_v2(to_px_x(col_x(p)), to_px_y((float)top / 100.0f)), 3, cw_rgba(1, 1, 1, 0.7f), 80, 0.4f, 4);
    T.height_cm[p] = top;
    c->score[p] = (float)top;
  }
  /* ends when everyone has dropped everything and it has settled */
  bool all = true;
  for (int p = 0; p < NP; p++) all = all && T.dropped[p] >= MAXB;
  if (all && c->tick > 200) {
    bool still = true;
    for (int p = 0; p < NP; p++) for (int i = 0; i < T.dropped[p]; i++) if (T.blk[p][i].alive) {
      cw_vec2 v = cw_body2d_velocity(T.blk[p][i].body);
      if (v.x * v.x + v.y * v.y > 0.25f) still = false;
    }
    T.calm = still ? T.calm + 1 : 0;
    if (T.calm > 90) c->done = true;
  }
}

static void think(mg_ctx *c, int p, mg_in *out) {
  memset(out, 0, sizeof *out);
  if (T.dropped[p] >= MAXB) return;
  if (T.cpu_plan_t[p] <= 0) { /* plan the next block: aim a little off the tower's middle, turn wide blocks flat */
    static const float err[3] = { 0.55f, 0.32f, 0.14f };
    uint32_t h = hash_u32(T.seed + (uint32_t)p, (uint32_t)T.dropped[p]);
    float jitter = ((float)(h % 2000) / 1000.0f - 1.0f) * err[c->cpu_skill[p]];
    T.cpu_aim[p] = jitter;
    int s = T.next_shape[p][T.dropped[p]];
    T.cpu_turn[p] = s == 2 ? ((h >> 12) % 100 < 60 + c->cpu_skill[p] * 15) : (s == 0 ? false : false);
    if (T.cpu_turn[p] != T.turned[p]) out->b = true;
    T.cpu_plan_t[p] = 1;
    return;
  }
  float dx = T.cpu_aim[p] - T.cx[p];
  if (fabsf(dx) > 0.06f) out->x = dx > 0 ? 1.0f : -1.0f;
  else if (T.cooldown[p] == 0) {
    bool calm = true;
    if (T.dropped[p] > 0) {
      if (T.blk[p][T.dropped[p] - 1].alive) {
        cw_vec2 v = cw_body2d_velocity(T.blk[p][T.dropped[p] - 1].body);
        calm = v.x * v.x + v.y * v.y < 0.6f;
      }
    }
    if (calm) { out->a = true; T.cpu_plan_t[p] = 0; }
  }
}

static const char *BLK[4] = { "block_0", "block_1", "block_2", "block_3" };
static void block(cw_xform2 x, float w, float h, int p, cw_color tint) {
  cw_vec2 ctr = cw_v2(to_px_x(x.p.x), to_px_y(x.p.y));
  art_draw(BLK[p], ctr, cw_v2(w * PPM, h * PPM), -atan2f(x.q.s, x.q.c), tint);
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha;
  art_ambient = cw_rgba(0.5f, 0.52f, 0.62f, 1);
  ui_grad(cw_rect_make(0, 0, STAGE_W, STAGE_H), cw_rgba(0.34f, 0.62f, 0.95f, 1), cw_rgba(0.9f, 0.94f, 1, 1));
  bg_clouds(t, 40, 1.2f, 0.95f);
  bg_ridge("ridge_far", t * 6, GROUND_Y, 230, cw_rgba(1, 1, 1, 1));
  bg_ridge("ridge_mid", t * 12, GROUND_Y, 170, cw_rgba(1, 1, 1, 1));
  bg_ridge("ridge_near", t * 22, GROUND_Y + 10, 110, cw_rgba(1, 1, 1, 1));
  art_light(cw_v2(380, -200), 2400, cw_rgba(1, 0.94f, 0.82f, 1), 0.45f, false);
  /* a lawn: lit rubber-track texture tinted green on dark earth */
  ui_grad(cw_rect_make(0, GROUND_Y + 14, STAGE_W, STAGE_H - GROUND_Y - 14), cw_rgba(0.25f, 0.18f, 0.12f, 1), cw_rgba(0.1f, 0.08f, 0.07f, 1));
  for (float x = 0; x < STAGE_W; x += 144) art_draw_ex("track", cw_v2(x, GROUND_Y - 10), cw_v2(145, 34), cw_v2(0, 0), 0, cw_rgba(0.45f, 0.95f, 0.5f, 1), false, false);
  for (int p = 0; p < NP; p++) {
    float px = to_px_x(col_x(p));
    ui_grad(cw_rect_make(px - 160, 120, 320, GROUND_Y - 120), col_a(PCOL[p], 0.0f), col_a(PCOL[p], 0.14f));
    art_light(cw_v2(px, 150), 420, col_mix(PCOL[p], cw_rgba(1, 1, 1, 1), 0.4f), 0.45f, false);
    art_draw("shadow", cw_v2(px, GROUND_Y + 20), cw_v2(250, 36), 0, cw_rgba(1, 1, 1, 0.8f));
    art_nine("tile", cw_rect_make(px - 112, GROUND_Y - 6, 224, 30), 24, 0.5f, col_mix(PCOL[p], cw_rgba(0.3f, 0.3f, 0.35f, 1), 0.4f));
    /* height ruler */
    for (int m = 1; m <= 6; m++) cw_r2d_line(cw_v2(px - 150, to_px_y((float)m)), cw_v2(px - 130, to_px_y((float)m)), 2, cw_rgba(0, 0, 0, 0.25f));
    /* the crane rail and claw */
    art_draw("post", cw_v2(px, 132), cw_v2(10, 300), 1.5708f, (cw_color){ 0 });
    float cxp = px + to_px_x(T.cx[p]);
    art_draw_ex("post", cw_v2(cxp, 134), cw_v2(6, 18), cw_v2(0.5f, 0), 0, (cw_color){ 0 }, false, false);
    if (T.dropped[p] < MAXB) {
      int s = T.next_shape[p][T.dropped[p]];
      float w = SHAPES[s].w, h = SHAPES[s].h;
      if (T.turned[p]) { float tt = w; w = h; h = tt; }
      cw_color col = T.cooldown[p] > 0 ? cw_rgba(1, 1, 1, 0.35f) : (cw_color){ 0 };
      art_draw(BLK[p], cw_v2(cxp, 150 + h * PPM * 0.5f), cw_v2(w * PPM, h * PPM), 0, col);
      /* a drop guide */
      cw_r2d_line(cw_v2(cxp, 150 + h * PPM), cw_v2(cxp, GROUND_Y - 4), 2, cw_rgba(1, 1, 1, 0.14f));
    }
    for (int i = 0; i < T.dropped[p]; i++) {
      if (!T.blk[p][i].alive) continue;
      shape_t s = SHAPES[T.blk[p][i].shape];
      if (T.blk[p][i].rot) { float tt = s.w; s.w = s.h; s.h = tt; }
      cw_xform2 x = cw_body2d_interpolated(T.blk[p][i].body, alpha);
      float k = 0.9f + 0.05f * (float)(i % 3);
      block(x, s.w, s.h, p, cw_rgba(k, k, k, 1));
    }
    if (T.height_cm[p] > 0) {
      float y = to_px_y((float)T.height_cm[p] / 100.0f);
      cw_r2d_line(cw_v2(px - 130, y), cw_v2(px + 130, y), 3, cw_rgba(1, 0.9f, 0.3f, 0.9f));
    }
  }
  (void)c;
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "%.2f m", c->score[p] / 100.0f); }

const mg_def mg_tower = {
  .id = "stack", .name = "Stack Attack", .tagline = "Drop blocks, build the tallest tower that stands.",
  .how = { "Left / Right: move the crane", "A: drop the block, B: turn it", "Blocks that fall off are lost" },
  .buttons = 2, .keys = "SAB",
  .kind = "2D physics", .scene = NULL, .seconds = 50, .unlock_points = 4, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .stop = stop, .score_text = score_text,
};
