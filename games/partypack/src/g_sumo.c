/* Sumo Push (2D, last standing): four blobs on a round plate that shrinks. Shove the others off the edge;
 * South is a dash that hits hard. Out players become ghosts at the rim: left and right slide the ghost around the
 * edge and south sends a bump inward, so nobody sits and waits. The last blob on the plate wins; the rest rank by how
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
  uint32_t seed; /* this game's CPU noise */
} S;

static void build_set(mg_ctx *c);
static void start(mg_ctx *c) {
  memset(&S, 0, sizeof S);
  if (pp_set_on) build_set(c);
  S.seed = cw_rng_u32(&c->rng);
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
  if (c->tick > 420) S.radius = fmaxf(105, R0 - (float)(c->tick - 420) * 0.14f);
  for (int i = 0; i < NP; i++) {
    __typeof__(S.b[0]) *b = &S.b[i];
    const mg_in *in = &c->in[i];
    if (!b->alive) { /* ghost on the rim */
      b->ang += in->x * 2.6f * dt;
      if (b->gcd > 0) b->gcd--;
      if (in->psouth && b->gcd == 0) {
        b->gcd = 150;
        float gx = CX + cosf(b->ang) * (S.radius + 40), gy = CY + sinf(b->ang) * (S.radius + 40);
        fx_ring(cw_v2(gx, gy), 10, 90, 0.4f, col_a(PCOL[i], 0.9f));
        sfx(SFX_BOUNCE, 0.4f, 1.3f);
        for (int k = 0; k < NP; k++) {
          if (!S.b[k].alive || pp_mates(c, i, k)) continue; /* a ghost never bumps its own teammate */
          float dx = S.b[k].x - gx, dy = S.b[k].y - gy, d = sqrtf(dx * dx + dy * dy);
          float g = pp_solo(c, k) ? 0.6f : 1.0f; /* the one alone (1 vs 3) is the heavier */
          if (d < 260 && d > 1) { S.b[k].vx += dx / d * 190 * g; S.b[k].vy += dy / d * 190 * g; }
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
    if (in->psouth && b->cd == 0) {
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
      bool mates = pp_mates(c, i, j) && !pp_mates_knock; /* teammates bump like anyone, but a teammate's dash never knocks */
      if (pp_mates(c, i, j) && !mates && (a->dash > 0 || b->dash > 0)) pp_mate_knocks++;
      float ja = a->dash > 0 && !mates ? 420 : 0, jb = b->dash > 0 && !mates ? 420 : 0;
      if (pp_solo(c, i)) ja *= 1.3f;
      if (pp_solo(c, j)) jb *= 1.3f;
      float pa_ = (rel < 0 ? -rel * 0.9f : 0) + jb, pb_ = (rel < 0 ? -rel * 0.9f : 0) + ja;
      if (pp_solo(c, i)) pa_ *= 0.6f; /* the one alone takes less of a knock */
      if (pp_solo(c, j)) pb_ *= 0.6f;
      a->vx -= nx * pa_; a->vy -= ny * pa_;
      b->vx += nx * pb_; b->vy += ny * pb_;
      if (ja + jb > 0 || -rel > 200) {
        fx_burst(cw_v2((a->x + b->x) * 0.5f, (a->y + b->y) * 0.5f), 10, cw_rgba(1, 1, 0.8f, 1), 220, 0.4f, 6);
        /* a bump: both feel it, the one who was rammed the harder */
        float hard = ja + jb > 0 ? 1.0f : 0.5f;
        pp_rumble(c, i, 0.15f * hard, (jb > 0 ? 0.75f : 0.35f) * hard, 90);
        pp_rumble(c, j, 0.15f * hard, (ja > 0 ? 0.75f : 0.35f) * hard, 90);
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
      fx_slowmo(36); /* the fall plays at half speed: the moment everyone remembers */
      pp_rumble(c, i, 0.5f, 0.5f, 200); /* out of the ring */
    }
  }
  /* the survivors' score ticks up while they are in, so the strip reads right; the last one standing gets a bonus */
  for (int i = 0; i < NP; i++) if (S.b[i].alive) c->score[i] = (float)(c->tick / 6);
  bool in[NP];
  for (int i = 0; i < NP; i++) in[i] = S.b[i].alive;
  if (pp_one_side_left(c, in)) { /* the last one on (or the last team): just ahead of the last to fall */
    for (int i = 0; i < NP; i++) if (S.b[i].alive) c->score[i] += 5;
    c->done = true;
  }
}

static void think(mg_ctx *c, int p, mg_in *out) {
  memset(out, 0, sizeof *out);
  __typeof__(S.b[0]) *me = &S.b[p];
  uint32_t h = hash_u32(S.seed + (uint32_t)p * 77u + 5u, (uint32_t)c->tick);
  if (!me->alive) { /* ghost: circle round to the thickest crowd and bump now and then */
    /* the blob nearest round the rim from the ghost (the lowest seat used to be everyone's target) */
    float best = 0, bx = 0, bd = 1e9f;
    for (int k = 0; k < NP; k++) if (S.b[k].alive && !pp_mates(c, p, k)) {
      float a = atan2f(S.b[k].y - CY, S.b[k].x - CX), da = a - me->ang;
      while (da > 3.14159f) da -= TAU;
      while (da < -3.14159f) da += TAU;
      if (fabsf(da) < bd) bd = fabsf(da), bx = a, best = 1;
    }
    if (best > 0) {
      float da = bx - me->ang;
      while (da > 3.14159f) da -= TAU;
      while (da < -3.14159f) da += TAU;
      out->x = da > 0.1f ? 1 : da < -0.1f ? -1 : 0;
    }
    out->south = me->gcd == 0 && (h % 100) < 8;
    return;
  }
  { static const int lag[3] = { 12, 5, 0 }; if ((int)(h % 100) < lag[c->cpu_skill[p]]) return; } /* lower skill reacts late now and then */
  static const float margin[3] = { 20, 25, 30 };
  static const int dash_pct[3] = { 3, 6, 14 };
  float mx = CX - me->x, my = CY - me->y, dc = sqrtf(mx * mx + my * my);
  int tgt = -1;
  float bd = 1e9f;
  for (int k = 0; k < NP; k++) {
    if (k == p || !S.b[k].alive || pp_mates(c, p, k)) continue;
    /* nearest, with a little per-game noise so an exact tie (the square start) is not always the lower seat */
    float d = hypotf(S.b[k].x - me->x, S.b[k].y - me->y) + (float)(hash_u32(S.seed + (uint32_t)p * 13u, (uint32_t)k + (uint32_t)(c->tick / 120) * 7u) % 24u);
    if (c->cpu_skill[p] == 2) { /* a hard CPU goes for whoever is nearest the edge, within reach */
      float er = hypotf(S.b[k].x - CX, S.b[k].y - CY);
      if (d < 260) d -= er * 0.8f;
    }
    if (d < bd) bd = d, tgt = k;
  }
  if (dc > S.radius - margin[c->cpu_skill[p]]) { /* too near the edge: back to the middle */
    out->x = mx / dc; out->y = my / dc;
  } else if (tgt >= 0) {
    float dx = S.b[tgt].x - me->x, dy = S.b[tgt].y - me->y, d = hypotf(dx, dy) + 0.001f;
    /* come at the target from the middle side, so a hit pushes it outward */
    float tx = S.b[tgt].x - CX, ty = S.b[tgt].y - CY, tl = sqrtf(tx * tx + ty * ty) + 0.001f;
    float gx = S.b[tgt].x - tx / tl * 46, gy = S.b[tgt].y - ty / tl * 46;
    dx = gx - me->x; dy = gy - me->y;
    float dl = sqrtf(dx * dx + dy * dy) + 0.001f;
    out->x = dx / dl; out->y = dy / dl;
    /* a dash flies far: a careful CPU only dashes when the target is nearer the edge than itself and it has room
     * behind it (an easy one dashes whenever) */
    float ux = (S.b[tgt].x - me->x) / (d + 0.001f), uy = (S.b[tgt].y - me->y) / (d + 0.001f);
    float after = hypotf(me->x + ux * 150 - CX, me->y + uy * 150 - CY); /* where the dash would carry me */
    bool safe = c->cpu_skill[p] == 0 || after < S.radius + (c->cpu_skill[p] == 2 ? 15.0f : 30.0f);
    if (safe && d < 120 && me->cd == 0 && (int)(h % 100) < dash_pct[c->cpu_skill[p]] * 2) { out->south = true; out->x = (S.b[tgt].x - me->x) / d; out->y = (S.b[tgt].y - me->y) / d; }
  }
}

/* ── the set: a lacquered wooden plate on a stone post in a pool, in a little stadium (2.5D: the rules above are
 * the 2D ones, in stage units; 60 stage units are a metre) ── */
#define WS (1.0f / 60.0f)
#define WATER_Y -0.8f
static struct {
  cw_node plate, rim, mark, blob[NP], ring[NP];
  bool was_alive[NP];
  float fall_t[NP], fx[NP], fy[NP], fz[NP], fvx[NP], fvz[NP], fvy[NP], yaw[NP], last;
  bool splashed[NP];
} V;
static cw_vec2 to_screen(cw_vec2 s) { return n3_project((t3_vec3){ (s.x - CX) * WS, 0, (s.y - CY) * WS }); }
static void cam(void) { n3_camera(0, 7.4f, 7.0f, 0, -0.3f, 0.45f); }

static void build_set(mg_ctx *c) {
  memset(&V, 0, sizeof V);
  cw_node lv = n3_level();
  float r0 = R0 * WS;
  V.rim = n3_prim(lv, "Rim", "CYLINDER", (t3_vec3){ 0, -0.17f, 0 }, (t3_vec3){ 2 * r0 + 0.24f, 0.3f, 2 * r0 + 0.24f }, (cw_color){ 0.75f, 0.2f, 0.12f, 1 });
  cw_node_set(V.rim, "roughness", cw_val_float(0.45f));
  V.plate = n3_prim(lv, "Plate", "CYLINDER", (t3_vec3){ 0, -0.14f, 0 }, (t3_vec3){ 2 * r0, 0.28f, 2 * r0 }, (cw_color){ 0.62f, 0.4f, 0.22f, 1 });
  cw_node_set(V.plate, "roughness", cw_val_float(0.38f)); /* lacquered wood */
  V.mark = n3_prim(lv, "Mark", "CYLINDER", (t3_vec3){ 0, 0.004f, 0 }, (t3_vec3){ 1.7f, 0.01f, 1.7f }, (cw_color){ 0.92f, 0.86f, 0.7f, 1 });
  cw_node_set(V.mark, "cast_shadow", cw_val_bool(false));
  for (int k = 0; k < 2; k++) { /* two start lines across the middle, as on a real ring */
    cw_node l = n3_box(lv, "Line", (t3_vec3){ k ? 0.55f : -0.55f, 0.012f, 0 }, (t3_vec3){ 0.08f, 0.01f, 0.9f }, (cw_color){ 0.2f, 0.1f, 0.06f, 1 });
    cw_node_set(l, "cast_shadow", cw_val_bool(false));
  }
  n3_prim(lv, "Post", "CYLINDER", (t3_vec3){ 0, -2.2f, 0 }, (t3_vec3){ 3.2f, 4, 3.2f }, (cw_color){ 0.42f, 0.42f, 0.45f, 1 });
  n3_box(lv, "PoolFloor", (t3_vec3){ 0, -3.4f, 0 }, (t3_vec3){ 30, 0.4f, 24 }, (cw_color){ 0.2f, 0.42f, 0.5f, 1 });
  /* the stadium: a ring of wall segments with a cap, two tiers of benches behind, banner lights on poles */
  for (int k = 0; k < 32; k++) {
    float a = TAU * (float)k / 32, rr = 9.6f;
    cw_node w = n3_box(lv, "Wall", (t3_vec3){ cosf(a) * rr, -1.0f, sinf(a) * rr }, (t3_vec3){ 0.5f, 3.4f, 1.95f }, k % 2 ? (cw_color){ 0.85f, 0.82f, 0.76f, 1 } : (cw_color){ 0.78f, 0.74f, 0.68f, 1 });
    n3_rot(w, 0, -a, 0);
    cw_node cap = n3_box(lv, "Cap", (t3_vec3){ cosf(a) * rr, 0.75f, sinf(a) * rr }, (t3_vec3){ 0.7f, 0.12f, 2.0f }, (cw_color){ 0.55f, 0.16f, 0.14f, 1 });
    n3_rot(cap, 0, -a, 0);
    for (int tier = 0; tier < 2; tier++) {
      float tr = rr + 1.4f + 1.6f * (float)tier;
      cw_node bench = n3_box(lv, "Bench", (t3_vec3){ cosf(a) * tr, 0.3f + 0.8f * (float)tier, sinf(a) * tr }, (t3_vec3){ 1.5f, 0.6f + 1.6f * (float)tier, 2.4f + 0.4f * (float)tier }, (cw_color){ 0.3f + 0.05f * (float)tier, 0.32f, 0.42f, 1 });
      n3_rot(bench, 0, -a, 0);
      cw_node_set(bench, "cast_shadow", cw_val_bool(false));
    }
  }
  for (int i = 0; i < NP; i++) {
    float a = TAU * (float)i / NP + 0.7854f;
    cw_node pole = n3_prim(lv, "Pole", "CYLINDER", (t3_vec3){ cosf(a) * 9.6f, 2.0f, sinf(a) * 9.6f }, (t3_vec3){ 0.16f, 2.6f, 0.16f }, (cw_color){ 0.3f, 0.3f, 0.32f, 1 });
    (void)pole;
    cw_node flag = n3_box(lv, "Banner", (t3_vec3){ cosf(a) * 9.6f, 2.6f, sinf(a) * 9.6f }, (t3_vec3){ 0.06f, 1.1f, 0.9f }, PCOL[i]);
    n3_rot(flag, 0, -a, 0);
    cw_node_set(flag, "roughness", cw_val_float(1)); /* cloth: matte */
  }
  for (int i = 0; i < NP; i++) {
    V.blob[i] = n3_blob(lv, i, !c->human[i]);
    V.ring[i] = n3_prim(lv, "SwimRing", "CYLINDER", (t3_vec3){ 0, WATER_Y, 0 }, (t3_vec3){ 1.15f, 0.26f, 1.15f }, PCOL[i]);
    cw_node_set(V.ring[i], "roughness", cw_val_float(0.35f)); /* rubber */
    cw_node_set(V.ring[i], "visible", cw_val_bool(false));
    V.was_alive[i] = true;
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
  float k = S.radius / R0;
  n3_scale(V.plate, k, 1, k);
  n3_scale(V.rim, (S.radius * WS * 2 + 0.24f) / (R0 * WS * 2 + 0.24f), 1, (S.radius * WS * 2 + 0.24f) / (R0 * WS * 2 + 0.24f));
  /* the plate is about to shrink: its rim glows up and down, slow and smooth (about one swell a second) */
  float warn = c->tick > 360 && c->tick < 480 ? 0.5f - 0.5f * cosf((float)(c->tick - 360) / 60.0f * TAU) : 0;
  n3_emissive(V.rim, (cw_color){ 0.25f + 2.2f * warn, 0.05f + 0.4f * warn, 0.02f, 1 });
  for (int i = 0; i < NP; i++) {
    __typeof__(S.b[0]) *b = &S.b[i];
    float x = (b->x - CX) * WS, z = (b->y - CY) * WS;
    if (b->alive) {
      V.was_alive[i] = true;
      if (b->fx * b->fx + b->fy * b->fy > 0.01f) {
        float want = cw_atan2(b->fx, b->fy), d = want - V.yaw[i];
        while (d > 3.14159f) d -= TAU;
        while (d < -3.14159f) d += TAU;
        V.yaw[i] += d * fminf(1, dt * 14);
      }
      float sq = b->dash > 0 ? 0.8f : 1 + 0.03f * sinf(t * 7 + (float)i);
      n3_blob_pose(V.blob[i], x, 0, z, V.yaw[i], sq);
      continue;
    }
    if (V.was_alive[i]) { /* just went over the edge: fly out, drop into the pool, splash (looks only) */
      V.was_alive[i] = false;
      float l = sqrtf(x * x + z * z) + 1e-4f;
      V.fx[i] = x; V.fz[i] = z; V.fy[i] = 0;
      V.fvx[i] = x / l * 2.6f; V.fvz[i] = z / l * 2.6f; V.fvy[i] = 2.2f;
      V.fall_t[i] = 0; V.splashed[i] = false;
      sfx(SFX_OOH, 0.55f, 1.0f + 0.06f * (float)i);
    }
    V.fall_t[i] += dt;
    if (!V.splashed[i]) {
      V.fvy[i] -= 14 * dt;
      V.fx[i] += V.fvx[i] * dt; V.fz[i] += V.fvz[i] * dt; V.fy[i] += V.fvy[i] * dt;
      n3_blob_pose(V.blob[i], V.fx[i], V.fy[i], V.fz[i], V.yaw[i] + V.fall_t[i] * 9, 1.15f);
      if (V.fy[i] < WATER_Y - 0.3f) {
        V.splashed[i] = true;
        n3_fx("splash", (t3_vec3){ V.fx[i], WATER_Y, V.fz[i] }, 1.1f);
        n3_decal("art/decal_foam.png", (t3_vec3){ V.fx[i], WATER_Y + 0.1f, V.fz[i] }, 2.2f, (cw_color){ 1, 1, 1, 0.8f }, "ADD");
      }
      continue;
    }
    /* a ghost: bobbing in its swim ring on the water at the rim */
    float gx = cosf(b->ang) * (S.radius + 44) * WS, gz = sinf(b->ang) * (S.radius + 44) * WS;
    float bob = 0.04f * sinf(t * 2.2f + (float)i);
    cw_node_set(V.ring[i], "visible", cw_val_bool(true));
    n3_pos(V.ring[i], gx, WATER_Y + bob, gz);
    n3_blob_pose(V.blob[i], gx, WATER_Y + 0.02f + bob, gz, -b->ang - 1.5708f + 3.14159f, b->gcd > 0 ? 0.9f : 1);
    n3_scale(V.blob[i], 0.75f, 0.75f, 0.75f);
  }
  /* the dash's recharge under each blob on the plate, and a ghost's bump ready ring (HUD) */
  for (int i = 0; i < NP; i++) {
    __typeof__(S.b[0]) *b = &S.b[i];
    if (b->alive) {
      cw_vec2 f = to_screen(cw_v2(b->x, b->y + BR * 0.6f));
      float rdy = 1 - (float)b->cd / 70.0f;
      cw_r2d_rect(cw_rect_make(f.x - 22, f.y + 6, 44, 6), cw_rgba(0, 0, 0, 0.45f));
      cw_r2d_rect(cw_rect_make(f.x - 22, f.y + 6, 44 * rdy, 6), col_a(PCOL[i], rdy >= 1 ? 1.0f : 0.6f));
    } else if (V.splashed[i]) {
      cw_vec2 g = n3_project((t3_vec3){ cosf(b->ang) * (S.radius + 44) * WS, WATER_Y + 1.3f, sinf(b->ang) * (S.radius + 44) * WS });
      cw_r2d_circle_lines(g, 12, 3, 16, col_a(PCOL[i], b->gcd > 0 ? 0.3f : 0.95f));
    }
  }
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "%s", S.b[p].alive ? "in" : (c->human[p] ? "ghost" : "out")); }

const mg_def mg_sumo = {
  .id = "sumo", .name = "Sumo Push", .tagline = "Shove everyone off the shrinking plate. Last blob on it wins.",
  .solo_tip = "Alone? Outlast two of the three. You are heavy and hit harder",
  .team_tip = "A teammate's dash never knocks you: shove the other side off",
  .how = { MOVE4 ": move", "{btn_south}: dash and shove (then it recharges)", "Out? Slide your ghost round the rim, {btn_south} bumps them" },
  .controls = MOVE4 "  {btn_south}",
  .kind = "2D", .scene = NULL, .set = "scenes/sumo.cwscene", .seconds = 50, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
