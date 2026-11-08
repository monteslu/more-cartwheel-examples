/* Stack Attack (2D physics): each player runs a crane over their own
 * platform. Stick moves it, south drops the block, east turns it. Build the
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

static void build_set(mg_ctx *c);
static void start(mg_ctx *c) {
  memset(&T, 0, sizeof T);
  if (pp_set_on) build_set(c);
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
    if (in->peast || in->pwest) T.turned[p] = !T.turned[p];
    if (T.cooldown[p] > 0) T.cooldown[p]--;
    if (in->psouth) drop(c, p);
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
    if (T.cpu_turn[p] != T.turned[p]) out->east = true;
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
    if (calm) { out->south = true; T.cpu_plan_t[p] = 0; }
  }
}

static const char *BLK[4] = { "block_0", "block_1", "block_2", "block_3" };
static void block(cw_xform2 x, float w, float h, int p, cw_color tint) {
  cw_vec2 ctr = cw_v2(to_px_x(x.p.x), to_px_y(x.p.y));
  art_draw(BLK[p], ctr, cw_v2(w * PPM, h * PPM), -atan2f(x.q.s, x.q.c), tint);
}

/* ── the set: four lit stone pillars on a meadow, a crane rail across the sky, the blocks as solid painted wood
 * (2.5D: the physics above stays 2D, in metres; the set is x - 8, y up, z 0) ── */
static struct { cw_node blk[NP][MAXB], held[NP], claw[NP], bar[NP]; int held_shape[NP], held_rot[NP]; } V;
static cw_vec2 to_screen(cw_vec2 s) { return n3_project((t3_vec3){ s.x / PPM - 8, (GROUND_Y - s.y) / PPM, 0 }); }
static void cam(void) { n3_camera(0, 4.4f, 12.6f, 0, 2.9f, 0); }

static void build_set(mg_ctx *c) {
  (void)c;
  memset(&V, 0, sizeof V);
  cw_node lv = n3_level();
  n3_box(lv, "Meadow", (t3_vec3){ 0, -1.75f, -6 }, (t3_vec3){ 80, 0.5f, 40 }, (cw_color){ 0.26f, 0.5f, 0.2f, 1 });
  for (int k = 0; k < 9; k++) { /* hills behind */
    float x = -24 + (float)k * 6.2f, h = 3 + (float)((k * 5) % 4);
    cw_node hl = n3_prim(lv, "Hill", "SPHERE", (t3_vec3){ x, -1.6f, -16 - (float)(k % 3) * 3 }, (t3_vec3){ 12, h * 2, 8 }, (cw_color){ 0.22f + 0.03f * (float)(k % 2), 0.45f, 0.2f, 1 });
    cw_node_set(hl, "cast_shadow", cw_val_bool(false));
  }
  cw_node rail = n3_box(lv, "Rail", (t3_vec3){ 0, 6.75f, -0.2f }, (t3_vec3){ 16.5f, 0.16f, 0.16f }, (cw_color){ 0.25f, 0.25f, 0.28f, 1 });
  cw_node_set(rail, "metalness", cw_val_float(0.8f));
  for (int q = 0; q < 2; q++) n3_box(lv, "RailPost", (t3_vec3){ q ? 8.3f : -8.3f, 2.5f, -0.2f }, (t3_vec3){ 0.2f, 8.5f, 0.2f }, (cw_color){ 0.25f, 0.25f, 0.28f, 1 });
  for (int p = 0; p < NP; p++) {
    float x = col_x(p) - 8;
    cw_node top = n3_box(lv, "Platform", (t3_vec3){ x, -0.25f, 0 }, (t3_vec3){ 2.7f, 0.5f, 1.2f }, col_mix(PCOL[p], cw_rgba(0.3f, 0.3f, 0.35f, 1), 0.4f));
    cw_node_set(top, "roughness", cw_val_float(0.6f));
    n3_box(lv, "Pillar", (t3_vec3){ x, -1.0f, 0 }, (t3_vec3){ 1.6f, 1.0f, 0.9f }, (cw_color){ 0.55f, 0.53f, 0.5f, 1 });
    for (int m = 1; m <= 6; m++) { /* the height ruler: a tick a metre */
      cw_node tk = n3_box(lv, "Tick", (t3_vec3){ x - 1.75f, (float)m, -0.3f }, (t3_vec3){ 0.25f, 0.03f, 0.03f }, (cw_color){ 0.15f, 0.15f, 0.18f, 1 });
      cw_node_set(tk, "cast_shadow", cw_val_bool(false));
    }
    V.claw[p] = n3_box(lv, "Claw", (t3_vec3){ x, 6.55f, -0.2f }, (t3_vec3){ 0.5f, 0.3f, 0.5f }, (cw_color){ 0.95f, 0.75f, 0.2f, 1 });
    V.bar[p] = n3_box(lv, "Best", (t3_vec3){ x, -5, 0.65f }, (t3_vec3){ 3.2f, 0.05f, 0.05f }, (cw_color){ 1, 0.85f, 0.3f, 1 });
    n3_emissive(V.bar[p], (cw_color){ 1.2f, 0.9f, 0.25f, 1 });
    cw_node_set(V.bar[p], "cast_shadow", cw_val_bool(false));
    for (int i = 0; i < MAXB; i++) {
      float k = 0.85f + 0.07f * (float)(i % 3);
      V.blk[p][i] = n3_box(lv, "Block", (t3_vec3){ 0, -9, 0 }, (t3_vec3){ 1, 1, 0.9f }, col_mix(PCOL[p], cw_rgba(k, k, k, 1), 0.25f));
      cw_node_set(V.blk[p][i], "roughness", cw_val_float(0.7f)); /* painted wood */
      cw_node_set(V.blk[p][i], "visible", cw_val_bool(false));
    }
    V.held[p] = n3_box(lv, "Held", (t3_vec3){ 0, -9, 0 }, (t3_vec3){ 1, 1, 0.9f }, col_mix(PCOL[p], cw_rgba(1, 1, 1, 1), 0.35f));
    V.held_shape[p] = -1;
  }
  cam();
  fx_map = to_screen;
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)t;
  if (!pp_set_on) return;
  cam();
  for (int p = 0; p < NP; p++) {
    float x0 = col_x(p) - 8;
    n3_pos(V.claw[p], x0 + T.cx[p], 6.55f, -0.2f);
    if (T.dropped[p] < MAXB) {
      int s = T.next_shape[p][T.dropped[p]];
      float w = SHAPES[s].w, h = SHAPES[s].h;
      if (T.turned[p]) { float tt = w; w = h; h = tt; }
      if (V.held_shape[p] != s || V.held_rot[p] != (int)T.turned[p]) { V.held_shape[p] = s; V.held_rot[p] = T.turned[p]; cw_node_set(V.held[p], "size", cw_val_vec3(w, h, 0.9f)); }
      cw_node_set(V.held[p], "visible", cw_val_bool(true));
      n3_pos(V.held[p], x0 + T.cx[p], 6.4f - h * 0.5f, 0);
      n3_emissive(V.held[p], T.cooldown[p] > 0 ? (cw_color){ 0, 0, 0, 1 } : (cw_color){ PCOL[p].r * 0.25f, PCOL[p].g * 0.25f, PCOL[p].b * 0.25f, 1 });
    } else cw_node_set(V.held[p], "visible", cw_val_bool(false));
    for (int i = 0; i < MAXB; i++) {
      cw_node n = V.blk[p][i];
      if (i >= T.dropped[p] || !T.blk[p][i].alive) { cw_node_set(n, "visible", cw_val_bool(false)); continue; }
      shape_t s = SHAPES[T.blk[p][i].shape];
      if (T.blk[p][i].rot) { float tt = s.w; s.w = s.h; s.h = tt; }
      cw_xform2 x = cw_body2d_interpolated(T.blk[p][i].body, alpha);
      cw_node_set(n, "visible", cw_val_bool(true));
      cw_node_set(n, "size", cw_val_vec3(s.w, s.h, 0.9f));
      n3_pos(n, (float)x.p.x - 8, (float)x.p.y, 0);
      n3_rot(n, 0, 0, atan2f(x.q.s, x.q.c));
    }
    if (T.height_cm[p] > 0) n3_pos(V.bar[p], x0, (float)T.height_cm[p] / 100.0f, 0.65f);
  }
  (void)c;
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "%.2f m", c->score[p] / 100.0f); }

const mg_def mg_tower = {
  .id = "stack", .name = "Stack Attack", .tagline = "Drop blocks, build the tallest tower that stands.",
  .how = { MOVE_LR ": move the crane", "{btn_south}: drop the block    {btn_east}: turn it", "Blocks that fall off are lost" },
  .controls = MOVE_LR "  {btn_south}  {btn_east}",
  .kind = "2D physics", .scene = NULL, .set = "scenes/stack.cwscene", .seconds = 50, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .stop = stop, .score_text = score_text,
};
