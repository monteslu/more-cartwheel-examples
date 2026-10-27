/* Loop Racers (3D, racing): four karts race four laps of a stadium oval.
 * Left / Right slide across the track, A is a nitro burst (three of them),
 * oil slicks and barrels slow you, blue pads give a free boost. The inside
 * lane is shorter, so everyone wants it, and the hazards sit there. First
 * past four laps wins; the rest rank by distance. */
#include "mg.h"

#define SLEN 22.0f
#define RAD_ 8.0f
#define TW 9.5f /* track width */
#define LAPS 4
#define NSEG 96
#define NHAZ 14
#define PI_ 3.14159265f
static const float LTOT = 2 * SLEN + 2 * PI_ * RAD_;

static struct {
  cw_node kart[NP], body[NP], face[NP], wheel[NP][4];
  float s[NP], d[NP], v[NP], dist[NP];
  int lap[NP], finished[NP], nitro[NP], boost[NP], slow[NP], spin[NP], bump_t[NP];
  struct { float s, d; int kind; cw_node node; } haz[NHAZ];
  uint32_t seed;
  int finish_order;
  float heading[NP];
} Rc;

/* the point and heading of the centreline at s, and the outward (right-hand) normal */
static void track_at(float s, float *x, float *z, float *hx, float *hz, float *nx, float *nz, bool *arc) {
  s = fmodf(s, LTOT);
  if (s < 0) s += LTOT;
  *arc = false;
  if (s < SLEN) { *x = -SLEN / 2 + s; *z = RAD_; *hx = 1; *hz = 0; *nx = 0; *nz = 1; }
  else if (s < SLEN + PI_ * RAD_) {
    float th = (s - SLEN) / RAD_, sn, cs;
    cw_sincos(th, &sn, &cs);
    *x = SLEN / 2 + RAD_ * sn; *z = RAD_ * cs; *hx = cs; *hz = -sn; *nx = sn; *nz = cs; *arc = true;
  } else if (s < 2 * SLEN + PI_ * RAD_) {
    float t = s - SLEN - PI_ * RAD_;
    *x = SLEN / 2 - t; *z = -RAD_; *hx = -1; *hz = 0; *nx = 0; *nz = -1;
  } else {
    float th = (s - 2 * SLEN - PI_ * RAD_) / RAD_, sn, cs;
    cw_sincos(th, &sn, &cs);
    *x = -SLEN / 2 - RAD_ * sn; *z = -RAD_ * cs; *hx = -cs; *hz = sn; *nx = -sn; *nz = -cs; *arc = true;
  }
}

static void world_pos(float s, float d, float *x, float *z, float *hx, float *hz) {
  float cx, cz, nx, nz;
  bool arc;
  track_at(s, &cx, &cz, hx, hz, &nx, &nz, &arc);
  *x = cx + nx * d; *z = cz + nz * d;
}

static void build_track(void) {
  cw_node root = n3_level();
  float seg = LTOT / NSEG;
  for (int i = 0; i < NSEG; i++) {
    float s = (float)i * seg + seg * 0.5f, x, z, hx, hz, nx, nz;
    bool arc;
    track_at(s, &x, &z, &hx, &hz, &nx, &nz, &arc);
    cw_node t = n3_box(root, "Road", (t3_vec3){ x, 0, z }, (t3_vec3){ arc ? seg * (RAD_ + TW * 0.55f) / RAD_ + 0.3f : seg + 0.35f, 0.12f, TW }, (cw_color){ 0.07f, 0.07f, 0.08f, 1 });
    cw_node_set(t, "roughness", cw_val_float(0.75));
    cw_node_set(t, "cast_shadow", cw_val_bool(false));
    n3_rot(t, 0, cw_atan2(-hz, hx), 0);
    /* centre dashes on every other piece */
    if (i % 2 == 0 && i % 4 != 2 && !arc) {
      cw_node dsh = n3_box(root, "Dash", (t3_vec3){ x, 0.07f, z }, (t3_vec3){ seg * 0.5f, 0.02f, 0.18f }, (cw_color){ 0.8f, 0.8f, 0.75f, 1 });
      cw_node_set(dsh, "cast_shadow", cw_val_bool(false));
      n3_rot(dsh, 0, cw_atan2(-hz, hx), 0);
    }
    for (int side = -1; side <= 1; side += 2) { /* kerbs in red and white */
      float ox = x + nx * side * (TW * 0.5f + 0.35f), oz = z + nz * side * (TW * 0.5f + 0.35f);
      bool red = ((i / 2) % 2) == 0;
      cw_node k = n3_box(root, "Kerb", (t3_vec3){ ox, 0.14f, oz }, (t3_vec3){ arc ? seg * (RAD_ + side * TW * 0.55f) / RAD_ + 0.12f : seg + 0.1f, 0.3f, 0.7f }, red ? (cw_color){ 0.8f, 0.1f, 0.08f, 1 } : (cw_color){ 0.9f, 0.9f, 0.88f, 1 });
      n3_rot(k, 0, cw_atan2(-hz, hx), 0);
    }
    if (i == 0) { /* start line */
      cw_node sl = n3_box(root, "Start", (t3_vec3){ x - seg * 0.5f, 0.075f, z }, (t3_vec3){ 0.7f, 0.02f, TW }, (cw_color){ 0.95f, 0.95f, 0.95f, 1 });
      n3_emissive(sl, (cw_color){ 0.2f, 0.2f, 0.2f, 1 });
      cw_node_set(sl, "cast_shadow", cw_val_bool(false));
    }
  }
  /* grandstand blocks outside the far side */
  for (int i = 0; i < 9; i++) {
    float x = -20 + i * 5.0f;
    n3_box(root, "Stand", (t3_vec3){ x, 1.4f, -RAD_ - TW * 0.5f - 4.5f }, (t3_vec3){ 4.2f, 2.8f + (float)(i % 3) * 0.6f, 3 }, (cw_color){ 0.24f + 0.04f * (float)(i % 3), 0.22f, 0.3f, 1 });
  }
}

static void make_kart(int i, bool cpu) {
  char nm[16];
  snprintf(nm, sizeof nm, "Kart%d", i);
  cw_node k = cw_node_new("Node3D", nm);
  cw_node_add_child(n3_level(), k);
  cw_color c = PCOL[i];
  cw_color lin = { c.r * c.r, c.g * c.g, c.b * c.b, 1 };
  cw_node body = n3_box(k, "Body", (t3_vec3){ 0, 0.32f, 0 }, (t3_vec3){ 1.1f, 0.34f, 1.9f }, lin);
  cw_node_set(body, "roughness", cw_val_float(0.3));
  cw_node_set(body, "clearcoat", cw_val_float(0.9));
  cw_node_set(body, "metalness", cw_val_float(0.2));
  n3_box(k, "Nose", (t3_vec3){ 0, 0.28f, 1.05f }, (t3_vec3){ 0.8f, 0.22f, 0.5f }, lin);
  n3_box(k, "Wing", (t3_vec3){ 0, 0.72f, -0.95f }, (t3_vec3){ 1.3f, 0.07f, 0.3f }, (cw_color){ 0.05f, 0.05f, 0.06f, 1 });
  cw_node tail = n3_box(k, "Tail", (t3_vec3){ 0, 0.4f, -0.97f }, (t3_vec3){ 0.7f, 0.12f, 0.06f }, (cw_color){ 0.3f, 0.02f, 0.02f, 1 });
  n3_emissive(tail, (cw_color){ 1.5f, 0.1f, 0.05f, 1 });
  for (int w = 0; w < 4; w++) {
    cw_node wh = n3_prim(k, "Wheel", "CYLINDER", (t3_vec3){ (w % 2 ? 0.62f : -0.62f), 0.26f, (w < 2 ? 0.7f : -0.65f) }, (t3_vec3){ 0.52f, 0.28f, 0.52f }, (cw_color){ 0.04f, 0.04f, 0.045f, 1 });
    n3_rot(wh, 0, 0, 1.5708f);
    Rc.wheel[i][w] = wh;
  }
  cw_node dr = n3_prim(k, "Driver", "SPHERE", (t3_vec3){ 0, 0.82f, -0.1f }, (t3_vec3){ 0.7f, 0.7f, 0.7f }, lin);
  cw_node_set(dr, "roughness", cw_val_float(0.35));
  cw_node_set(dr, "clearcoat", cw_val_float(0.6));
  cw_node face = cw_node_new("Node3D", "Face");
  cw_node_add_child(k, face);
  cw_node3d_set_position(face, (t3_vec3){ 0, 0.85f, -0.1f });
  for (int s = -1; s <= 1; s += 2) {
    n3_prim(face, "Eye", "SPHERE", (t3_vec3){ 0.13f * s, 0.1f, 0.27f }, (t3_vec3){ 0.19f, 0.22f, 0.14f }, (cw_color){ 1, 1, 1, 1 });
    cw_node pu = n3_prim(face, "Pupil", "SPHERE", (t3_vec3){ 0.13f * s, 0.09f, 0.34f }, (t3_vec3){ 0.09f, 0.1f, 0.05f }, (cw_color){ 0.02f, 0.02f, 0.04f, 1 });
    cw_node_set(pu, "cast_shadow", cw_val_bool(false));
  }
  n3_prim(k, "Helmet", "SPHERE", (t3_vec3){ 0, 0.98f, -0.1f }, (t3_vec3){ 0.55f, 0.28f, 0.55f }, (cw_color){ 0.95f, 0.95f, 0.95f, 1 });
  if (cpu) {
    cw_node tip = n3_prim(k, "Tip", "SPHERE", (t3_vec3){ 0, 1.35f, -0.1f }, (t3_vec3){ 0.14f, 0.14f, 0.14f }, (cw_color){ 0.9f, 0.9f, 0.95f, 1 });
    n3_emissive(tip, (cw_color){ 0.3f, 0.9f, 0.4f, 1 });
  }
  Rc.kart[i] = k; Rc.body[i] = body; Rc.face[i] = face;
}

static void start(mg_ctx *c) {
  memset(&Rc, 0, sizeof Rc);
  n3_fx_reset();
  build_track();
  for (int i = 0; i < NP; i++) {
    make_kart(i, i >= c->humans);
    Rc.s[i] = -1.0f - (float)(i / 2) * 2.6f;   /* a staggered grid */
    Rc.d[i] = (i % 2 ? 1.9f : -1.9f);
    Rc.nitro[i] = 3;
  }
  for (int k = 0; k < NHAZ; k++) {
    Rc.haz[k].kind = k % 4 == 3 ? 2 : k % 2; /* 0 oil, 1 barrel, 2 boost pad */
    Rc.haz[k].s = 18.0f + (float)k * (LTOT - 24.0f) / (float)NHAZ + cw_rng_range(&c->rng, -2.0f, 2.0f);
    Rc.haz[k].d = Rc.haz[k].kind == 2 ? cw_rng_range(&c->rng, -2.5f, 2.5f) : (cw_rng_int(&c->rng, 0, 99) < 60 ? cw_rng_range(&c->rng, -4, -0.5f) : cw_rng_range(&c->rng, 0.5f, 4));
    cw_color col = Rc.haz[k].kind == 0 ? (cw_color){ 0.02f, 0.02f, 0.03f, 1 } : Rc.haz[k].kind == 1 ? (cw_color){ 0.8f, 0.4f, 0.1f, 1 } : (cw_color){ 0.1f, 0.4f, 1, 1 };
    float x, z, hx, hz;
    world_pos(Rc.haz[k].s, Rc.haz[k].d, &x, &z, &hx, &hz);
    cw_node n;
    if (Rc.haz[k].kind == 0) { n = n3_prim(n3_level(), "Oil", "CYLINDER", (t3_vec3){ x, 0.08f, z }, (t3_vec3){ 2.0f, 0.02f, 2.0f }, col); cw_node_set(n, "roughness", cw_val_float(0.05)); cw_node_set(n, "cast_shadow", cw_val_bool(false)); }
    else if (Rc.haz[k].kind == 1) { n = n3_prim(n3_level(), "Barrel", "CYLINDER", (t3_vec3){ x, 0.45f, z }, (t3_vec3){ 0.8f, 0.9f, 0.8f }, col); cw_node_set(n, "metalness", cw_val_float(0.4)); }
    else { n = n3_box(n3_level(), "Pad", (t3_vec3){ x, 0.08f, z }, (t3_vec3){ 1.6f, 0.03f, 2.4f }, col); n3_emissive(n, (cw_color){ 0.2f, 0.9f, 2.0f, 1 }); n3_rot(n, 0, cw_atan2(-hz, hx), 0); cw_node_set(n, "cast_shadow", cw_val_bool(false)); }
    Rc.haz[k].node = n;
  }
  Rc.seed = cw_rng_u32(&c->rng);
  n3_camera(0, 36, 21, 0, 0, 0.8f);
}

static void tick(mg_ctx *c) {
  const float dt = 1.0f / 60.0f;
  n3_fx_tick();
  int fin = 0;
  for (int i = 0; i < NP; i++) {
    const mg_in *in = &c->in[i];
    if (Rc.finished[i]) { fin++; Rc.v[i] *= 0.985f; }
    /* steer across the track */
    float steer = Rc.finished[i] ? 0 : in->x;
    if (Rc.spin[i] > 0) { Rc.spin[i]--; steer = 0; }
    Rc.d[i] = fclampf(Rc.d[i] + steer * 4.6f * dt, -TW * 0.5f + 0.9f, TW * 0.5f - 0.9f);
    /* speed */
    float vmax = 11.5f;
    if (Rc.boost[i] > 0) { Rc.boost[i]--; vmax = 18.0f; }
    if (Rc.slow[i] > 0) { Rc.slow[i]--; vmax = 4.5f; }
    if (Rc.finished[i]) vmax = 0;
    if (in->pa && Rc.nitro[i] > 0 && Rc.boost[i] == 0 && !Rc.finished[i]) {
      Rc.nitro[i]--; Rc.boost[i] = 70;
      sfx(SFX_GO, 0.4f, 1.4f);
    }
    float acc = (Rc.v[i] < vmax ? 14.0f : 22.0f) * dt;
    Rc.v[i] += fclampf(vmax - Rc.v[i], -acc, acc);
    float cx, cz, hx, hz, nx, nz;
    bool arc;
    track_at(Rc.s[i], &cx, &cz, &hx, &hz, &nx, &nz, &arc);
    float k = arc ? RAD_ / (RAD_ + Rc.d[i]) : 1.0f; /* the inside lane is shorter */
    float ds = Rc.v[i] * dt * k;
    Rc.s[i] += ds;
    Rc.dist[i] += ds;
    if (Rc.bump_t[i] > 0) Rc.bump_t[i]--;
    if (!Rc.finished[i] && Rc.dist[i] >= LAPS * LTOT) {
      Rc.finished[i] = ++Rc.finish_order;
      c->score[i] = 1000000.0f - (float)c->tick;
      sfx(SFX_WIN, 0.6f, 1);
      fx_burst(cw_v2(640, 360), 0, PCOL[i], 0, 0, 0);
    } else if (!Rc.finished[i]) c->score[i] = Rc.dist[i];
    /* hazards */
    for (int h = 0; h < NHAZ; h++) {
      float rel = fmodf(Rc.s[i] - Rc.haz[h].s + LTOT * 8, LTOT);
      if (rel > LTOT / 2) rel -= LTOT;
      if (fabsf(rel) < 0.9f && fabsf(Rc.d[i] - Rc.haz[h].d) < (Rc.haz[h].kind == 2 ? 1.2f : 1.0f)) {
        float x, z, hx2, hz2;
        world_pos(Rc.s[i], Rc.d[i], &x, &z, &hx2, &hz2);
        if (Rc.haz[h].kind == 2) { if (Rc.boost[i] < 40) { Rc.boost[i] = 40; sfx(SFX_COIN, 0.35f, 1.5f); } }
        else if (Rc.slow[i] == 0 && Rc.spin[i] == 0) {
          Rc.slow[i] = 50;
          if (Rc.haz[h].kind == 0) { Rc.spin[i] = 40; sfx(SFX_BOUNCE, 0.5f, 0.6f); n3_fx("dust", (t3_vec3){ x, 0.2f, z }, 1.0f); }
          else { sfx(SFX_HIT, 0.6f, 1); n3_fx("sparks", (t3_vec3){ x, 0.5f, z }, 0.6f); n3_fx("dust", (t3_vec3){ x, 0.3f, z }, 1); Rc.spin[i] = 20; }
        }
      }
    }
    /* kart against kart: a bump slows the one behind and pushes them apart */
    for (int j = i + 1; j < NP; j++) {
      float rel = fmodf(Rc.s[i] - Rc.s[j] + LTOT * 8, LTOT);
      if (rel > LTOT / 2) rel -= LTOT;
      float dd = Rc.d[i] - Rc.d[j];
      if (fabsf(rel) < 1.9f && fabsf(dd) < 1.25f) {
        float push = (1.25f - fabsf(dd)) * 0.5f * (dd >= 0 ? 1.0f : -1.0f);
        Rc.d[i] += push; Rc.d[j] -= push;
        if (rel > 0) Rc.v[j] *= 0.97f; else Rc.v[i] *= 0.97f;
        if (Rc.bump_t[i] == 0 && Rc.bump_t[j] == 0) {
          float x, z, a, b;
          world_pos(Rc.s[i], Rc.d[i], &x, &z, &a, &b);
          n3_fx("sparks", (t3_vec3){ x, 0.5f, z }, 0.4f);
          sfx(SFX_BOUNCE, 0.3f, 1.2f);
          Rc.bump_t[i] = Rc.bump_t[j] = 20;
        }
      }
    }
    /* place the kart */
    float x, z, hx2, hz2;
    world_pos(Rc.s[i], Rc.d[i], &x, &z, &hx2, &hz2);
    float yaw = cw_atan2(hx2, hz2) + (Rc.spin[i] > 0 ? (float)Rc.spin[i] * 0.4f : steer * -0.18f);
    n3_pos(Rc.kart[i], x, 0.06f, z);
    n3_rot(Rc.kart[i], 0, yaw, 0);
    Rc.heading[i] = yaw;
    if (Rc.boost[i] > 0 && c->tick % 3 == 0) n3_fx("sparks", (t3_vec3){ x - hx2 * 1.1f, 0.4f, z - hz2 * 1.1f }, 0.25f);
    Rc.lap[i] = (int)(Rc.dist[i] / LTOT) + 1;
    if (Rc.lap[i] > LAPS) Rc.lap[i] = LAPS;
  }
  if (fin >= NP) c->done = true;
}

static void think(mg_ctx *c, int p, mg_in *out) {
  memset(out, 0, sizeof *out);
  /* look ahead for a hazard in my lane and pick the freer side; otherwise hug the inside */
  float want = -2.6f;
  static const float look[3] = { 4.5f, 7, 10 };
  for (int h = 0; h < NHAZ; h++) {
    float rel = fmodf(Rc.haz[h].s - Rc.s[p] + LTOT * 8, LTOT);
    if (rel > 0 && rel < look[c->skill % 3] && Rc.haz[h].kind != 2 && fabsf(Rc.haz[h].d - Rc.d[p]) < 1.7f) {
      want = Rc.haz[h].d > Rc.d[p] ? Rc.haz[h].d - 2.2f : Rc.haz[h].d + 2.2f;
      if (want < -TW * 0.5f + 1) want = Rc.haz[h].d + 2.2f;
    } else if (rel > 0 && rel < 9 && Rc.haz[h].kind == 2 && c->skill > 0) want = Rc.haz[h].d;
  }
  /* skill 0 wanders */
  if (c->skill == 0) want += ((float)(hash_u32(Rc.seed + (uint32_t)p, (uint32_t)(c->tick / 50)) % 100) - 50.0f) / 25.0f;
  float dx = want - Rc.d[p];
  out->x = fabsf(dx) < 0.15f ? 0.0f : (dx > 0 ? 1.0f : -1.0f);
  if (Rc.nitro[p] > 0 && Rc.boost[p] == 0 && Rc.slow[p] == 0 && c->tick > 120) {
    float st = fmodf(Rc.s[p], LTOT);
    bool straight = st < SLEN - 5 || (st > SLEN + PI_ * RAD_ && st < 2 * SLEN + PI_ * RAD_ - 5);
    uint32_t h = hash_u32(Rc.seed + (uint32_t)p * 7u, (uint32_t)(c->tick / 20));
    if (straight && (h % 100) < 4 + (uint32_t)c->skill * 3) out->a = true;
  }
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha; (void)t;
  for (int i = 0; i < NP; i++) {
    t3_vec3 p = cw_node3d_position(Rc.kart[i]);
    cw_vec2 s = n3_project((t3_vec3){ p.x, p.y + 2.0f, p.z });
    cw_vec2 tri[3] = { cw_v2(s.x - 11, s.y - 9), cw_v2(s.x + 11, s.y - 9), cw_v2(s.x, s.y + 4) };
    cw_r2d_polygon(tri, 3, PCOL[i]);
    if (Rc.finished[i]) {
      char b[16];
      snprintf(b, sizeof b, "#%d", Rc.finished[i]);
      ui_text(b, cw_v2(s.x, s.y - 40), 28, PCOL[i], CW_ALIGN_CENTER);
    }
  }
  /* nitro charges per player under the strip */
  for (int i = 0; i < NP; i++) {
    float x0 = 160 + i * 285;
    for (int n = 0; n < 3; n++) cw_r2d_circle(cw_v2(x0 + 22 + n * 24, 135), 8, 12, n < Rc.nitro[i] ? cw_rgba(0.3f, 0.8f, 1, 1) : cw_rgba(0, 0, 0, 0.4f));
  }
  (void)c;
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) {
  if (Rc.finished[p]) snprintf(buf, (size_t)cap, "finished #%d", Rc.finished[p]);
  else snprintf(buf, (size_t)cap, "lap %d/%d", Rc.lap[p] ? Rc.lap[p] : 1, LAPS);
  (void)c;
}

const mg_def mg_kart = {
  .id = "loop", .name = "Loop Racers", .tagline = "Four laps. The inside lane is short and full of traps.",
  .how = { "Left / Right: change lane", "A: nitro burst (three of them)", "Blue pads boost, oil and barrels slow" },
  .kind = "3D", .scene = "scenes/kart.cwscene", .seconds = 80, .unlock_points = 16, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
