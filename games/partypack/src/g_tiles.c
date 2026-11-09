/* Tile Drop (2D, last standing): the floor is made of tiles, and a tile you stand on cracks and falls away, and so
 * do random ones. Keep moving and hop gaps with south. When a tile goes under you, you fall. Out? Move your ghost
 * cursor with the stick and press south to crack a tile under it. The last blob standing wins. */
#include "mg.h"

#define COLS 10
#define ROWS 5
#define TW 100.0f
#define TH 84.0f
#define X0 140.0f
#define Y0 190.0f
#define WEAR 110 /* ticks of standing before a tile cracks */
static struct {
  struct { int8_t st; int16_t wear, t; } tile[ROWS][COLS]; /* st: 0 whole, 1 cracking, 2 gone */
  struct { float x, y, vx, vy; int air; bool alive; int fall, out_t; float gx, gy; int gcd; int tr, tc, hold, idle; } p[NP];
  int alive;
  uint32_t seed; /* this game's CPU noise */
} T;

static void build_set(mg_ctx *c);
static void start(mg_ctx *c) {
  memset(&T, 0, sizeof T);
  if (pp_set_on) build_set(c);
  T.seed = cw_rng_u32(&c->rng);
  T.alive = NP;
  for (int i = 0; i < NP; i++) {
    static const int col0[NP] = { 1, 3, 6, 8 }; /* mirror-symmetric starts: no seat nearer the middle than its twin */
    T.p[i].x = X0 + TW * ((float)col0[i] + 0.5f);
    T.p[i].tr = -1;
    T.p[i].y = Y0 + TH * 2.5f;
    T.p[i].alive = true;
    T.p[i].gx = X0 + TW * 5;
    T.p[i].gy = Y0 + TH * 2.5f;
  }
}

static bool floor_at(float x, float y, int *r, int *cc) {
  int col = (int)floorf((x - X0) / TW), row = (int)floorf((y - Y0) / TH);
  if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return false;
  *r = row; *cc = col;
  return T.tile[row][col].st != 2;
}
static void crack(int r, int cc) {
  if (T.tile[r][cc].st != 0) return;
  T.tile[r][cc].st = 1;
  T.tile[r][cc].t = 42;
  sfx(SFX_BUZZ, 0.12f, 1.6f);
}

static void tick(mg_ctx *c) {
  const float dt = 1.0f / 60.0f;
  int every = 95 - c->tick / 60;
  if (every < 30) every = 30;
  if (c->tick > 420 && c->tick % every == 0) { /* a random tile goes */
    for (int tries = 0; tries < 12; tries++) {
      int r = cw_rng_int(&c->rng, 0, ROWS - 1), cc = cw_rng_int(&c->rng, 0, COLS - 1);
      if (T.tile[r][cc].st == 0) { crack(r, cc); break; }
    }
  }
  for (int r = 0; r < ROWS; r++)
    for (int k = 0; k < COLS; k++)
      if (T.tile[r][k].st == 1 && --T.tile[r][k].t <= 0) {
        T.tile[r][k].st = 2;
        fx_burst(cw_v2(X0 + TW * ((float)k + 0.5f), Y0 + TH * ((float)r + 0.5f)), 10, cw_rgba(0.6f, 0.5f, 0.45f, 1), 180, 0.6f, 7);
        sfx(SFX_BOUNCE, 0.3f, 0.5f);
      }
  for (int i = 0; i < NP; i++) {
    __typeof__(T.p[0]) *p = &T.p[i];
    const mg_in *in = &c->in[i];
    if (!p->alive) { /* ghost cursor */
      p->gx = fclampf(p->gx + in->x * 380 * dt, X0 + 10, X0 + TW * COLS - 10);
      p->gy = fclampf(p->gy + in->y * 380 * dt, Y0 + 10, Y0 + TH * ROWS - 10);
      if (p->gcd > 0) p->gcd--;
      if (in->psouth && p->gcd == 0) {
        int r, cc;
        if (floor_at(p->gx, p->gy, &r, &cc)) { crack(r, cc); p->gcd = 170; fx_ring(cw_v2(p->gx, p->gy), 8, 60, 0.4f, PCOL[i]); }
      }
      continue;
    }
    if (p->fall > 0) { /* falling: gone when it ends */
      p->fall++;
      p->y += 160 * dt;
      if (p->fall > 40) {
        p->alive = false;
        p->out_t = c->tick;
        T.alive--;
        c->eliminated[i] = true;
        c->score[i] = (float)(c->tick / 6);
        p->gx = X0 + TW * 5; p->gy = Y0 + TH * 2.5f; p->gcd = 60;
        sfx(SFX_SPLASH, 0.6f, 0.7f);
        fx_text(cw_v2(fclampf(p->x, 120, 1160), 330), "DROP", PCOL[i], 34);
        fx_hitstop(4);
        pp_rumble(c, i, 0.45f, 0.3f, 180); /* dropped through */
      }
      continue;
    }
    float ax = in->x, ay = in->y, l = sqrtf(ax * ax + ay * ay);
    if (l > 1) ax /= l, ay /= l;
    float sp = p->air > 0 ? 300 : 230;
    p->vx += (ax * sp - p->vx) * 0.25f;
    p->vy += (ay * sp - p->vy) * 0.25f;
    if (in->psouth && p->air == 0) { p->air = 34; sfx(SFX_JUMP, 0.4f, 1.2f); }
    if (p->air > 0) p->air--;
    p->x = fclampf(p->x + p->vx * dt, X0 + 6, X0 + TW * COLS - 6);
    p->y = fclampf(p->y + p->vy * dt, Y0 + 6, Y0 + TH * ROWS - 6);
    if (p->air == 0) {
      int r, cc;
      if (!floor_at(p->x, p->y, &r, &cc)) { p->fall = 1; sfx(SFX_BUZZ, 0.4f, 0.6f); fx_slowmo(30); /* the drop plays at half speed */ }
      else {
        if (++T.tile[r][cc].wear >= WEAR) crack(r, cc); /* standing wears a tile out */
      }
    }
  }
  for (int i = 0; i < NP; i++) if (T.p[i].alive) c->score[i] = (float)(c->tick / 6);
  bool in[NP];
  for (int i = 0; i < NP; i++) in[i] = T.p[i].alive;
  if (pp_one_side_left(c, in)) { /* the last one standing (or the last team): just ahead of the one who fell last */
    for (int i = 0; i < NP; i++) if (T.p[i].alive) c->score[i] += 5;
    c->done = true;
  }
}

static void think(mg_ctx *c, int p, mg_in *out) {
  memset(out, 0, sizeof *out);
  __typeof__(T.p[0]) *me = &T.p[p];
  uint32_t h = hash_u32(T.seed + (uint32_t)p * 91u + 3u, (uint32_t)c->tick);
  if (!me->alive) { /* ghost: crack a tile under the nearest blob */
    int tgt = -1;
    float bd = 1e9f;
    for (int k = 0; k < NP; k++) if (T.p[k].alive && !pp_mates(c, p, k)) { float d = hypotf(T.p[k].x - me->gx, T.p[k].y - me->gy); if (d < bd) bd = d, tgt = k; }
    if (tgt >= 0) {
      float dx = T.p[tgt].x - me->gx, dy = T.p[tgt].y - me->gy, d = bd + 0.001f;
      out->x = dx / d; out->y = dy / d;
      out->south = me->gcd == 0 && bd < 60 && (h % 100) < 20;
    }
    return;
  }
  /* pick a tile to head for and keep it a while (a target re-rolled every tick only jitters on the spot, wearing
   * one tile out): whole, little worn, near, away from the others. Better CPUs re-plan sooner and wear less */
  static const int care[3] = { 55, 25, 20 }, patience[3] = { 50, 55, 55 }, leap_pct[3] = { 30, 65, 95 };
  int sk = c->cpu_skill[p];
  int mr = (int)floorf((me->y - Y0) / TH), mc = (int)floorf((me->x - X0) / TW);
  int r, cc;
  bool on = floor_at(me->x, me->y, &r, &cc);
  bool replan = me->tr < 0 || T.tile[me->tr][me->tc].st != 0 || --me->hold <= 0 || (on && r == me->tr && cc == me->tc && T.tile[r][cc].wear > WEAR * (4 - sk) / 6);
  if (replan) {
    float bs = -1e9f;
    for (int rr = 0; rr < ROWS; rr++)
      for (int k = 0; k < COLS; k++) {
        if (T.tile[rr][k].st != 0) continue;
        int dr = rr - mr, dc = k - mc;
        if (dr * dr + dc * dc > 9 || (dr == 0 && dc == 0)) continue;
        float s = -(float)T.tile[rr][k].wear * 0.8f - (float)(dr * dr + dc * dc) * 4.0f;
        for (int q = 0; q < NP; q++) if (q != p && T.p[q].alive && fabsf(T.p[q].x - (X0 + TW * ((float)k + 0.5f))) < TW && fabsf(T.p[q].y - (Y0 + TH * ((float)rr + 0.5f))) < TH) s -= 25;
        if (sk > 0) { /* a better CPU prefers a way there with no gap or crack on it */
          float tx = X0 + TW * ((float)k + 0.5f), ty = Y0 + TH * ((float)rr + 0.5f);
          for (int st = 1; st <= 3; st++) {
            int pr, pc;
            float f = (float)st / 4.0f;
            if (!floor_at(me->x + (tx - me->x) * f, me->y + (ty - me->y) * f, &pr, &pc) || T.tile[pr][pc].st == 1) s -= sk == 2 ? 45.0f : 15.0f;
          }
        }
        if (sk == 2) /* a hard CPU also keeps clear of the ghosts' cursors */
          for (int q = 0; q < NP; q++) if (!T.p[q].alive && hypotf(T.p[q].gx - (X0 + TW * ((float)k + 0.5f)), T.p[q].gy - (Y0 + TH * ((float)rr + 0.5f))) < TW * 1.5f) s -= 30;
        s += (float)(hash_u32(h, (uint32_t)(rr * COLS + k)) % (uint32_t)(10 + care[sk] * 2));
        if (s > bs) bs = s, me->tr = rr, me->tc = k;
      }
    me->hold = patience[sk] + (int)(h % 20);
  }
  if (me->idle > 0) { me->idle--; return; } /* an easy CPU daydreams now and then */
  if (sk == 0 && (h % 1000) < 6) me->idle = 20 + (int)(h % 30);
  if (me->tr >= 0) {
    float bx = X0 + TW * ((float)me->tc + 0.5f), by = Y0 + TH * ((float)me->tr + 0.5f);
    float dx = bx - me->x, dy = by - me->y, d = sqrtf(dx * dx + dy * dy) + 0.001f;
    if (d > 12) { out->x = dx / d; out->y = dy / d; }
    /* a gap on the way: hop it */
    int nr, nc;
    bool ahead = floor_at(me->x + dx / d * 50, me->y + dy / d * 50, &nr, &nc);
    if (me->air == 0 && !ahead && (h % 100) < (uint32_t)leap_pct[sk]) out->south = true;
    /* a better CPU reads the tile ahead: a cracking one on the way means another plan */
    if (sk > 0 && ahead && T.tile[nr][nc].st == 1 && (h % 100) < (uint32_t)(sk * 40)) me->hold = 0;
  }
  if (me->air == 0 && !on) out->south = true; /* too late to hop, but try */
  if (on && me->air == 0 && T.tile[r][cc].st == 1 && (h % 100) < (uint32_t)(20 + leap_pct[sk] / 3)) out->south = true; /* cracking under us: leap */
}

/* ── the set: glossy tiles resting on a steel lattice high over a deep pool; a tile that goes tips and falls (2.5D: the
 * rules above are the 2D ones, in stage units; 60 stage units are a metre) ── */
#define WS (1.0f / 60.0f)
#define MX (X0 + TW * COLS * 0.5f)
#define MY (Y0 + TH * ROWS * 0.5f)
#define POOL_Y -9.0f
static struct {
  cw_node tile[ROWS][COLS], blob[NP], cursor[NP];
  float fall[ROWS][COLS], fvy[ROWS][COLS]; /* a gone tile's drop: metres below the floor, and its speed */
  int8_t shown_st[ROWS][COLS];
  float last, yaw[NP], drop_y[NP], drop_v[NP], drop_x[NP], drop_z[NP];
  bool splashed[NP];
} V;
static cw_vec2 to_screen(cw_vec2 s) { return n3_project((t3_vec3){ (s.x - MX) * WS, 0, (s.y - MY) * WS }); }
static void cam(void) { n3_camera(0, 9.6f, 9.4f, 0, -0.6f, 0.9f); }
static float tx_(int k) { return (X0 + TW * ((float)k + 0.5f) - MX) * WS; }
static float tz_(int r) { return (Y0 + TH * ((float)r + 0.5f) - MY) * WS; }

static void build_set(mg_ctx *c) {
  memset(&V, 0, sizeof V);
  cw_node lv = n3_level();
  float w = TW * COLS * WS, d = TH * ROWS * WS;
  for (int r = 0; r < ROWS; r++)
    for (int k = 0; k < COLS; k++) {
      cw_node n = n3_box(lv, "Tile", (t3_vec3){ tx_(k), -0.12f, tz_(r) }, (t3_vec3){ TW * WS - 0.08f, 0.24f, TH * WS - 0.08f }, (cw_color){ 0.35f, 0.7f, 0.95f, 1 });
      cw_node_set(n, "roughness", cw_val_float(0.25f)); /* glazed */
      V.tile[r][k] = n;
    }
  /* the lattice the tiles rest on: beams under every seam, four columns down into the pool */
  cw_color steel = { 0.32f, 0.33f, 0.37f, 1 };
  for (int r = 0; r <= ROWS; r++) { cw_node bm = n3_box(lv, "BeamX", (t3_vec3){ 0, -0.32f, -d * 0.5f + d * (float)r / ROWS }, (t3_vec3){ w + 0.3f, 0.16f, 0.14f }, steel); cw_node_set(bm, "metalness", cw_val_float(0.8f)); cw_node_set(bm, "roughness", cw_val_float(0.45f)); }
  for (int k = 0; k <= COLS; k++) { cw_node bm = n3_box(lv, "BeamZ", (t3_vec3){ -w * 0.5f + w * (float)k / COLS, -0.32f, 0 }, (t3_vec3){ 0.14f, 0.16f, d + 0.3f }, steel); cw_node_set(bm, "metalness", cw_val_float(0.8f)); cw_node_set(bm, "roughness", cw_val_float(0.45f)); }
  for (int q = 0; q < 4; q++) {
    float x = (q % 2 ? 1 : -1) * (w * 0.5f + 0.4f), z = (q < 2 ? -1 : 1) * (d * 0.5f + 0.4f);
    n3_box(lv, "Column", (t3_vec3){ x, (POOL_Y - 0.4f) * 0.5f, z }, (t3_vec3){ 0.7f, -POOL_Y + 0.4f, 0.7f }, (cw_color){ 0.45f, 0.42f, 0.4f, 1 });
    n3_prim(lv, "Lamp", "SPHERE", (t3_vec3){ x, 0.2f, z }, (t3_vec3){ 0.5f, 0.5f, 0.5f }, (cw_color){ 1, 0.9f, 0.7f, 1 });
  }
  n3_box(lv, "PoolFloor", (t3_vec3){ 0, POOL_Y - 3, 0 }, (t3_vec3){ 60, 0.5f, 40 }, (cw_color){ 0.1f, 0.25f, 0.32f, 1 });
  for (int i = 0; i < NP; i++) {
    V.blob[i] = n3_blob(lv, i, !c->human[i]);
    V.cursor[i] = n3_decal("art/decal_ring.png", (t3_vec3){ 0, -50, 0 }, 1.1f, (cw_color){ PCOL[i].r * 2, PCOL[i].g * 2, PCOL[i].b * 2, 1 }, "ADD");
    cw_node_set(V.cursor[i], "visible", cw_val_bool(false));
  }
  cam();
  fx_map = to_screen;
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha;
  if (!pp_set_on) return;
  float dt = n3_frame_dt(&V.last) * pp_anim_speed();
  cam();
  n3_fx_tick();
  for (int r = 0; r < ROWS; r++)
    for (int k = 0; k < COLS; k++) {
      cw_node n = V.tile[r][k];
      int st = T.tile[r][k].st;
      if (st == 2) { /* gone: it tips and drops into the pool (looks only) */
        if (V.shown_st[r][k] != 2) { V.shown_st[r][k] = 2; V.fall[r][k] = 0; V.fvy[r][k] = 0; }
        if (V.fall[r][k] > POOL_Y - 2) {
          V.fvy[r][k] += 16 * dt;
          V.fall[r][k] -= V.fvy[r][k] * dt;
          float tip = fminf(1.4f, -V.fall[r][k] * 0.5f);
          n3_pos(n, tx_(k), -0.12f + V.fall[r][k], tz_(r));
          n3_rot(n, tip * ((r + k) % 2 ? 1 : -1), 0, tip * 0.6f);
          if (V.fall[r][k] <= POOL_Y - 2) { cw_node_set(n, "visible", cw_val_bool(false)); n3_fx("splash", (t3_vec3){ tx_(k), POOL_Y, tz_(r) }, 0.9f); }
        }
        continue;
      }
      V.shown_st[r][k] = (int8_t)st;
      float wear = fminf((float)T.tile[r][k].wear / (float)WEAR, 1);
      cw_color top = col_mix(cw_rgba(0.35f, 0.7f, 0.95f, 1), cw_rgba(0.95f, 0.45f, 0.3f, 1), st == 1 ? 1.0f : wear * 0.8f);
      float glow = 0, wob = 0;
      if (st == 1) { /* cracking: it glows up and down (slow and smooth) and rocks on the beams */
        float ph = 0.5f - 0.5f * cosf((float)(42 - T.tile[r][k].t) / 42.0f * TAU * 1.0f);
        glow = 0.15f + 0.6f * ph;
        wob = sinf((float)T.tile[r][k].t * 0.55f) * 0.05f * (1 - (float)T.tile[r][k].t / 42.0f);
      }
      n3_color(n, top);
      n3_emissive(n, (cw_color){ glow, glow * 0.45f, glow * 0.2f, 1 });
      n3_rot(n, wob, 0, wob * 0.7f);
    }
  for (int i = 0; i < NP; i++) {
    __typeof__(T.p[0]) *p = &T.p[i];
    float x = (p->x - MX) * WS, z = (p->y - MY) * WS;
    if (p->alive && p->fall == 0) {
      V.drop_y[i] = 0; V.drop_v[i] = 0; V.splashed[i] = false;
      if (p->vx * p->vx + p->vy * p->vy > 400) {
        float want = cw_atan2(p->vx, p->vy), d = want - V.yaw[i];
        while (d > 3.14159f) d -= TAU;
        while (d < -3.14159f) d += TAU;
        V.yaw[i] += d * fminf(1, dt * 14);
      }
      float lift = p->air > 0 ? sinf((float)p->air / 34.0f * 3.14159f) * 34 * WS : 0;
      n3_blob_pose(V.blob[i], x, lift, z, V.yaw[i], p->air > 0 ? 1.12f : 1 + 0.03f * sinf(t * 7 + (float)i));
    } else if (!V.splashed[i]) { /* through the floor: down into the pool, flailing (looks only) */
      if (V.drop_y[i] == 0) { sfx(SFX_OOH, 0.5f, 1.0f + 0.06f * (float)i); V.drop_x[i] = x; V.drop_z[i] = z; V.drop_y[i] = -1e-4f; }
      V.drop_v[i] += 14 * dt;
      V.drop_y[i] -= V.drop_v[i] * dt;
      n3_blob_pose(V.blob[i], V.drop_x[i], V.drop_y[i], V.drop_z[i], V.yaw[i] + V.drop_y[i] * 1.6f, 1.15f); /* where it went through */
      if (V.drop_y[i] < POOL_Y) { V.splashed[i] = true; cw_node_set(V.blob[i], "visible", cw_val_bool(false)); n3_fx("splash", (t3_vec3){ V.drop_x[i], POOL_Y, V.drop_z[i] }, 1.2f); }
    }
    if (!p->alive) { /* the ghost's cursor: a ring of light on the floor */
      cw_node_set(V.cursor[i], "visible", cw_val_bool(true));
      n3_pos(V.cursor[i], (p->gx - MX) * WS, 0.1f, (p->gy - MY) * WS);
      float k = p->gcd > 0 ? 0.6f : 2.0f;
      cw_node_set(V.cursor[i], "color", cw_val_color(PCOL[i].r * k, PCOL[i].g * k, PCOL[i].b * k, 1));
      cw_vec2 g = to_screen(cw_v2(p->gx, p->gy));
      ui_blob(cw_v2(g.x, g.y - 34), 13, i, 0, 0, 1, p->gcd > 0 ? 3 : 1, !c->human[i]);
    }
  }
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "%s", T.p[p].alive ? "on" : (c->human[p] ? "ghost" : "out")); }

const mg_def mg_tiles = {
  .id = "tiles", .name = "Tile Drop", .tagline = "The floor crumbles. Keep moving and be the last one standing.",
  .team_tip = "The last team standing wins",
  .how = { MOVE4 ": run, a tile you stand on cracks", "{btn_south}: hop a gap (a short flight)", "Out? Ghost cursor, {btn_south} cracks a tile under it" },
  .controls = MOVE4 "  {btn_south}",
  .kind = "2D", .scene = NULL, .set = "scenes/tiles.cwscene", .seconds = 50, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
