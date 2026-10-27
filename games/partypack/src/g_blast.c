/* Blast Ring (3D, physics and explosions): four rolling bots on a steel
 * ring over a lava pit. Bombs drop; each blast shoves everything near it.
 * The ring shrinks. Stick to move, A to dash (a dash knocks others hard).
 * Last bot on the ring wins; otherwise the longest survivor.
 * A bot that is knocked off becomes a bomber: its stick moves a marker over
 * the ring and A calls a bomb down there (every few seconds). */
#include "mg.h"

#define NBOMB 6
static struct {
  cw_node ball[NP], face[NP], arena_shape, arena_top, edge[4], line[14];
  bool alive[NP];
  int dash_cd[NP], dash_fx[NP];
  float cur_x[NP], cur_z[NP]; /* a ghost bomber's marker */
  int call_cd[NP];
  float size;               /* arena side, metres */
  int shrink_stage, warn;
  struct { bool on; float x, z; int fuse, drop; cw_node mesh, ring; } bomb[NBOMB];
  int next_bomb;
  float shake;
  int elim_order;
  cw_node scorch[12], ember[6];
  int nscorch, ember_t[6], nember;
  uint32_t seed;
} K;

static const float RAD = 3.4f; /* blast radius */

static cw_vec2 ball_xz(int i) { t3_vec3 p = cw_node3d_position(K.ball[i]); return cw_v2(p.x, p.z); }

static void make_ball(int i, float x, float z, bool cpu) {
  char nm[16];
  snprintf(nm, sizeof nm, "Ball%d", i);
  cw_node b = cw_node_new("RigidBody3D", nm);
  cw_node_set(b, "density", cw_val_float(1));
  cw_node_set(b, "lock_rotation", cw_val_bool(true));
  cw_node_set(b, "linear_damp", cw_val_float(0.15));
  cw_node_set(b, "can_sleep", cw_val_bool(false));
  cw_node_set(b, "bounce", cw_val_float(0.25));
  cw_node_set(b, "friction", cw_val_float(0.3));
  cw_node sh = cw_node_new("CollisionShape3D", "Shape");
  cw_node_set(sh, "shape", cw_val_enum("SPHERE"));
  cw_node_set(sh, "radius", cw_val_float(0.5));
  cw_node_add_child(b, sh);
  cw_color c = PCOL[i];
  char mp[40];
  snprintf(mp, sizeof mp, "models/blob_%d.glb", i);
  n3_glb(b, "Body", mp, true);
  cw_node face = n3_glb(b, "Face", "models/face.glb", false);
  if (cpu) n3_glb(b, "Stalk", "models/stalk.glb", true);
  (void)c;
  cw_node_add_child(n3_level(), b);
  cw_node3d_set_position(b, (t3_vec3){ x, 0.6f, z });
  K.ball[i] = b;
  K.face[i] = face;
}

static void set_size(float s);
static void start(mg_ctx *c) {
  memset(&K, 0, sizeof K);
  n3_fx_reset();
  K.size = 13;
  K.arena_shape = cw_node_get(n3_find("Arena"), "Shape");
  K.arena_top = n3_glb(n3_find("Arena"), "Top", "models/arena.glb", true);
  n3_pos(K.arena_top, 0, 0.5f, 0);
  for (int pi = 0; pi < 4; pi++) {
    cw_node pl = n3_glb(n3_level(), "Pillar", "models/pillar.glb", true);
    n3_pos(pl, (pi % 2 ? 8.5f : -8.5f), 1.0f, (pi < 2 ? -8.5f : 8.5f));
  }
  cw_node lv = n3_glb(n3_level(), "LavaPlane", "models/lava.glb", false);
  n3_pos(lv, 0, -9, 0);
  static const float sx[4] = { -3.5f, 3.5f, -3.5f, 3.5f }, sz[4] = { -3.5f, -3.5f, 3.5f, 3.5f };
  for (int i = 0; i < NP; i++) { make_ball(i, sx[i], sz[i], !c->human[i]); K.alive[i] = true; }
  for (int e = 0; e < 4; e++) {
    K.edge[e] = n3_box(n3_level(), "Edge", (t3_vec3){ 0, 0.07f, 0 }, (t3_vec3){ 13, 0.14f, 0.16f }, (cw_color){ 0.02f, 0.05f, 0.06f, 1 });
    n3_emissive(K.edge[e], (cw_color){ 0.2f, 1.4f, 1.8f, 1 });
  }
  for (int g = -3; g <= 3; g++) for (int ax = 0; ax < 2; ax++) {
    cw_node l = n3_box(n3_level(), "Line", (t3_vec3){ 0, 0.012f, 0 }, (t3_vec3){ 1, 0.02f, 1 }, (cw_color){ 0.02f, 0.05f, 0.06f, 1 });
    n3_emissive(l, (cw_color){ 0.05f, 0.35f, 0.5f, 1 });
    cw_node_set(l, "cast_shadow", cw_val_bool(false));
    K.line[(g + 3) * 2 + ax] = l;
  }
  set_size(K.size);
  K.seed = cw_rng_u32(&c->rng);
  K.next_bomb = 150;
  n3_camera(0, 11, 9.5f, 0, 0, 0.6f);
}

static void set_size(float s) {
  K.size = s;
  cw_node_set(K.arena_shape, "size", cw_val_vec3(s, 1, s));
  n3_scale(K.arena_top, s / 13.0f, 1, s / 13.0f);
  for (int e = 0; e < 4; e++) {
    bool xs = e < 2;
    float o = (e % 2 ? 1 : -1) * s * 0.5f;
    cw_node_set(K.edge[e], "size", cw_val_vec3(xs ? s + 0.16f : 0.16f, 0.14f, xs ? 0.16f : s + 0.16f));
    n3_pos(K.edge[e], xs ? 0 : o, 0.07f, xs ? o : 0);
  }
  for (int g = -3; g <= 3; g++) for (int ax = 0; ax < 2; ax++) {
    cw_node l = K.line[(g + 3) * 2 + ax];
    float off = g * 2.0f;
    bool in = fabsf(off) < s * 0.5f - 0.3f;
    cw_node_set(l, "visible", cw_val_bool(in));
    cw_node_set(l, "size", cw_val_vec3(ax ? 0.05f : s, 0.02f, ax ? s : 0.05f));
    n3_pos(l, ax ? off : 0, 0.012f, ax ? 0 : off);
  }
}

static void explode(int b) {
  float bx = K.bomb[b].x, bz = K.bomb[b].z;
  t3_vec3 at = { bx, 0.4f, bz };
  n3_fx("fire", at, 1);
  n3_fx("sparks", at, 1);
  n3_fx("smoke", (t3_vec3){ bx, 0.6f, bz }, 1);
  n3_shockwave((t3_vec3){ bx, 0.3f, bz }, RAD * 1.25f, 0.55f, 1.0f);
  n3_flash((t3_vec3){ bx, 1.6f, bz }, (cw_color){ 1, 0.55f, 0.22f, 1 }, 70, 12);
  sfx(SFX_BOOM, 0.9f, 0.9f + 0.2f * (float)(b % 3) / 3);
  fx_hitstop(3);
  K.shake = 0.5f;
  { /* a scorch mark that stays, and an ember glow that cools */
    int si = K.nscorch++ % 12;
    if (cw_node_valid(K.scorch[si])) cw_node_free(K.scorch[si]);
    K.scorch[si] = n3_decal("art/decal_scorch.png", (t3_vec3){ bx, 0.3f, bz }, RAD * 1.5f, (cw_color){ 1, 1, 1, 0.92f }, "MULTIPLY");
    cw_node_set(K.scorch[si], "size", cw_val_vec3(RAD * 1.5f, RAD * 1.5f, 0.8f));
    int ei = K.nember++ % 6;
    if (cw_node_valid(K.ember[ei])) cw_node_free(K.ember[ei]);
    K.ember[ei] = n3_decal("art/decal_ember.png", (t3_vec3){ bx, 0.3f, bz }, RAD * 1.1f, (cw_color){ 1, 0.6f, 0.2f, 1 }, "ADD");
    cw_node_set(K.ember[ei], "size", cw_val_vec3(RAD * 1.1f, RAD * 1.1f, 0.8f));
    K.ember_t[ei] = 150;
  }
  for (int i = 0; i < NP; i++) {
    if (!K.alive[i]) continue;
    cw_vec2 p = ball_xz(i);
    float dx = p.x - bx, dz = p.y - bz, d = sqrtf(dx * dx + dz * dz);
    if (d > RAD) continue;
    float k = 1 - d / RAD;
    if (d < 0.05f) { dx = 1; dz = 0; d = 1; }
    cw_value v;
    cw_node_get_prop(K.ball[i], "linear_velocity", &v);
    float kick = 6 + 15 * k;
    cw_node_set(K.ball[i], "linear_velocity", cw_val_vec3(v.v[0] + dx / d * kick, v.v[1] + 4 + 4 * k, v.v[2] + dz / d * kick));
  }
  if (K.bomb[b].mesh.id) cw_node_free(K.bomb[b].mesh);
  if (K.bomb[b].ring.id) cw_node_free(K.bomb[b].ring);
  K.bomb[b].on = false;
}

static bool spawn_bomb_at(float x, float z);
static void spawn_bomb(mg_ctx *c) {
  int s = -1;
  for (int i = 0; i < NBOMB; i++) if (!K.bomb[i].on) { s = i; break; }
  if (s < 0) return;
  float lim = K.size * 0.5f - 1.0f, x, z;
  int pick = cw_rng_int(&c->rng, 0, 99);
  int who = cw_rng_int(&c->rng, 0, NP - 1);
  float jx = cw_rng_range(&c->rng, -2.2f, 2.2f);
  float jz = cw_rng_range(&c->rng, -2.2f, 2.2f);
  float rx = cw_rng_range(&c->rng, -lim, lim);
  float rz = cw_rng_range(&c->rng, -lim, lim);
  if (pick < 55 && K.alive[who]) { cw_vec2 p = ball_xz(who); x = p.x + jx; z = p.y + jz; } else { x = rx; z = rz; }
  x = fclampf(x, -lim, lim);
  z = fclampf(z, -lim, lim);
  spawn_bomb_at(x, z);
}

static bool spawn_bomb_at(float x, float z) {
  int s = -1;
  for (int i = 0; i < NBOMB; i++) if (!K.bomb[i].on) { s = i; break; }
  if (s < 0) return false;
  K.bomb[s].on = true; K.bomb[s].x = x; K.bomb[s].z = z; K.bomb[s].fuse = 80; K.bomb[s].drop = 34;
  K.bomb[s].mesh = n3_prim(n3_level(), "Bomb", "SPHERE", (t3_vec3){ x, 9, z }, (t3_vec3){ 0.7f, 0.7f, 0.7f }, (cw_color){ 0.03f, 0.03f, 0.04f, 1 });
  cw_node_set(K.bomb[s].mesh, "metalness", cw_val_float(0.8));
  cw_node_set(K.bomb[s].mesh, "roughness", cw_val_float(0.3));
  K.bomb[s].ring = n3_decal("art/decal_ring.png", (t3_vec3){ x, 0.3f, z }, RAD * 2, (cw_color){ 1, 1, 1, 1 }, "ADD");
  cw_node_set(K.bomb[s].ring, "size", cw_val_vec3(RAD * 2, RAD * 2, 0.8f));
  return true;
}

static void tick(mg_ctx *c) {
  const float dt = 1.0f / 60.0f;
  n3_fx_tick();
  for (int e = 0; e < 6; e++) if (K.ember_t[e] > 0 && cw_node_valid(K.ember[e])) { K.ember_t[e]--; cw_node_set(K.ember[e], "energy", cw_val_float(1.6f * (float)K.ember_t[e] / 150.0f)); if (!K.ember_t[e]) { cw_node_free(K.ember[e]); K.ember[e] = (cw_node){ 0 }; } }
  /* the ring shrinks in three steps, with a warning first */
  static const int step_at[3] = { 1100, 1800, 2500 };
  static const float step_size[3] = { 11, 9.5f, 8 };
  K.warn = 0;
  if (K.shrink_stage < 3) {
    int at = step_at[K.shrink_stage];
    if (c->tick > at - 120) K.warn = 1;
    if (c->tick >= at) {
      set_size(step_size[K.shrink_stage++]);
      sfx(SFX_HIT, 1, 0.6f);
      K.shake = 0.35f;
      for (int e = 0; e < 8; e++) n3_fx("dust", (t3_vec3){ (e % 2 ? 1 : -1) * K.size * 0.5f * (e < 4 ? 1.0f : 0.0f), 0.2f, (e % 2 ? 1 : -1) * K.size * 0.5f * (e >= 4 ? 1.0f : 0.0f) }, 1.4f);
    }
  }
  /* bombs */
  if (--K.next_bomb <= 0) {
    spawn_bomb(c);
    int gap = 85 - c->tick / 70;
    K.next_bomb = gap < 38 ? 38 : gap;
  }
  for (int b = 0; b < NBOMB; b++) {
    if (!K.bomb[b].on) continue;
    if (K.bomb[b].drop > 0) {
      K.bomb[b].drop--;
      float t = 1 - (float)K.bomb[b].drop / 34.0f;
      cw_node3d_set_position(K.bomb[b].mesh, (t3_vec3){ K.bomb[b].x, flerp(9, 0.4f, t * t), K.bomb[b].z });
      if (K.bomb[b].drop == 0) n3_fx("dust", (t3_vec3){ K.bomb[b].x, 0.2f, K.bomb[b].z }, 0.8f);
      continue;
    }
    K.bomb[b].fuse--;
    float blink = sinf((float)K.bomb[b].fuse * (0.25f + (80 - K.bomb[b].fuse) * 0.01f));
    n3_emissive(K.bomb[b].mesh, (cw_color){ blink > 0 ? 2.5f : 0.15f, 0.1f, 0.05f, 1 });
    cw_node_set(K.bomb[b].ring, "energy", cw_val_float(0.7f + 1.6f * (blink > 0)));
    if (K.bomb[b].fuse <= 0) explode(b);
  }
  /* ghost bombers: the marker follows the stick, A calls a bomb down on it */
  for (int g = 0; g < NP; g++) {
    if (K.alive[g]) continue;
    const mg_in *gi = &c->in[g];
    float lim = K.size * 0.5f - 1.0f;
    K.cur_x[g] = fclampf(K.cur_x[g] + gi->x * 7.0f * dt, -lim, lim);
    K.cur_z[g] = fclampf(K.cur_z[g] + gi->y * 7.0f * dt, -lim, lim);
    if (K.call_cd[g] > 0) K.call_cd[g]--;
    if (gi->pa && K.call_cd[g] == 0 && spawn_bomb_at(K.cur_x[g], K.cur_z[g])) { K.call_cd[g] = 200; sfx(SFX_POP, 0.6f, 0.8f); }
  }
  /* bots */
  int alive = 0;
  for (int i = 0; i < NP; i++) {
    if (!K.alive[i]) continue;
    const mg_in *in = &c->in[i];
    cw_value v;
    cw_node_get_prop(K.ball[i], "linear_velocity", &v);
    float vx = v.v[0], vy = v.v[1], vz = v.v[2];
    t3_vec3 p = cw_node3d_position(K.ball[i]);
    float ln = sqrtf(in->x * in->x + in->y * in->y);
    float ix = ln > 1 ? in->x / ln : in->x, iz = ln > 1 ? in->y / ln : in->y;
    bool grounded = p.y < 0.75f;
    float accel = (grounded ? 30 : 6) * dt;
    float tx = ix * 7.5f, tz = iz * 7.5f;
    vx += fclampf(tx - vx, -accel, accel);
    vz += fclampf(tz - vz, -accel, accel);
    if (K.dash_cd[i] > 0) K.dash_cd[i]--;
    if (in->pa && K.dash_cd[i] == 0 && grounded && ln > 0.2f) {
      vx += ix / (ln > 1 ? 1 : ln) * 10; vz += iz / (ln > 1 ? 1 : ln) * 10;
      K.dash_cd[i] = 70; K.dash_fx[i] = 8;
      sfx(SFX_JUMP, 0.5f, 1.3f);
      n3_fx("dust", (t3_vec3){ p.x, 0.15f, p.z }, 0.8f);
    }
    if (K.dash_fx[i] > 0) K.dash_fx[i]--;
    cw_node_set(K.ball[i], "linear_velocity", cw_val_vec3(vx, vy, vz));
    if (fabsf(vx) + fabsf(vz) > 0.8f) cw_node3d_set_rotation(K.face[i], (t3_vec3){ 0, cw_atan2(vx, vz), 0 });
    if (p.y < -3.0f) {
      K.alive[i] = false;
      K.cur_x[i] = fclampf(p.x, -K.size * 0.5f + 1, K.size * 0.5f - 1);
      K.cur_z[i] = fclampf(p.z, -K.size * 0.5f + 1, K.size * 0.5f - 1);
      K.call_cd[i] = 90;
      c->eliminated[i] = true;
      c->score[i] = (float)c->tick;
      n3_fx("fire", (t3_vec3){ p.x, -7.6f, p.z }, 1.0f);
      n3_flash((t3_vec3){ p.x, -5, p.z }, (cw_color){ 1, 0.4f, 0.1f, 1 }, 120, 12);
      sfx(SFX_SPLASH, 0.8f, 0.7f);
      fx_hitstop(7); /* a knock-out: everything holds for a beat */
      cw_node_free(K.ball[i]);
      K.ball[i] = (cw_node){ 0 };
      continue;
    }
    alive++;
  }
  for (int i = 0; i < NP; i++) if (K.alive[i]) { cw_vec2 p = ball_xz(i); c->score[i] = 10000 - sqrtf(p.x * p.x + p.y * p.y) * 10; }
  if (alive <= 1) {
    for (int i = 0; i < NP; i++) if (K.alive[i]) c->score[i] = 20000.0f;
    if (c->tick > 60) c->done = true;
  }
  K.shake *= 0.9f;
  float sh = K.shake;
  n3_camera(sinf((float)c->tick * 2.1f) * sh, 11 + cosf((float)c->tick * 2.7f) * sh, 9.5f, 0, 0, 0.6f);
}

static void think(mg_ctx *c, int p, mg_in *out) {
  memset(out, 0, sizeof *out);
  if (!K.alive[p]) { /* a CPU ghost steers its marker to a bot and calls the bomb when it is close */
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < NP; i++) {
      if (!K.alive[i]) continue;
      cw_vec2 q = ball_xz(i);
      float d = (q.x - K.cur_x[p]) * (q.x - K.cur_x[p]) + (q.y - K.cur_z[p]) * (q.y - K.cur_z[p]);
      if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return;
    cw_vec2 q = ball_xz(best);
    float dx = q.x - K.cur_x[p], dz = q.y - K.cur_z[p], d = sqrtf(dx * dx + dz * dz);
    if (d > 0.4f) { out->x = dx / d; out->y = dz / d; }
    out->a = K.call_cd[p] == 0 && d < 1.8f - 0.4f * (float)c->cpu_skill[p] && ((hash_u32(K.seed + (uint32_t)p, (uint32_t)(c->tick / 30)) & 3) == 0);
    return;
  }
  cw_vec2 me = ball_xz(p);
  float mx = 0, mz = 0;
  bool flee = false;
  for (int b = 0; b < NBOMB; b++) {
    if (!K.bomb[b].on || K.bomb[b].drop > 0) continue;
    float dx = me.x - K.bomb[b].x, dz = me.y - K.bomb[b].z, d = sqrtf(dx * dx + dz * dz);
    if (d < RAD + 1.2f && K.bomb[b].fuse < 70 + c->cpu_skill[p] * 15) {
      if (d < 0.05f) { dx = 1; d = 1; }
      mx += dx / d; mz += dz / d; flee = true;
    }
  }
  float half = K.size * 0.5f;
  float edge = fmaxf(fabsf(me.x), fabsf(me.y));
  if (flee) {
    /* never flee off the edge: bend toward the centre when close to it */
    if (edge > half - 1.8f) { mx -= me.x * 0.25f; mz -= me.y * 0.25f; }
  } else if (edge > half - 2.2f) { mx = -me.x; mz = -me.y; }
  else {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < NP; i++) {
      if (i == p || !K.alive[i]) continue;
      cw_vec2 q = ball_xz(i);
      float d = (q.x - me.x) * (q.x - me.x) + (q.y - me.y) * (q.y - me.y);
      if (d < bd) { bd = d; best = i; }
    }
    uint32_t h = hash_u32(K.seed + (uint32_t)p, (uint32_t)(c->tick / 40));
    if (best >= 0 && c->cpu_skill[p] > 0 && (h % 3) != 0) {
      cw_vec2 q = ball_xz(best);
      mx = q.x - me.x; mz = q.y - me.y;
      float d = sqrtf(bd);
      float qe = fmaxf(fabsf(q.x), fabsf(q.y));
      if (d < 2.6f && K.dash_cd[p] == 0 && qe > edge - 0.5f && ((h >> 4) % 100) < 20 + c->cpu_skill[p] * 35) out->a = true;
    } else { /* drift about */
      mx = (float)((int)(h % 200) - 100) / 100.0f - me.x * 0.1f;
      mz = (float)((int)((h >> 9) % 200) - 100) / 100.0f - me.y * 0.1f;
    }
  }
  float l = sqrtf(mx * mx + mz * mz);
  if (l > 0.001f) { out->x = mx / l; out->y = mz / l; }
  if (flee && K.dash_cd[p] == 0) {
    for (int b = 0; b < NBOMB; b++) if (K.bomb[b].on && K.bomb[b].drop == 0 && K.bomb[b].fuse < 26) out->a = true;
  }
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha; (void)t;
  for (int i = 0; i < NP; i++) {
    if (!K.alive[i]) continue;
    t3_vec3 p = cw_node3d_position(K.ball[i]);
    cw_vec2 s = n3_project((t3_vec3){ p.x, p.y + 1.3f, p.z });
    cw_vec2 tri[3] = { cw_v2(s.x - 12, s.y - 10), cw_v2(s.x + 12, s.y - 10), cw_v2(s.x, s.y + 4) };
    cw_r2d_polygon(tri, 3, PCOL[i]);
    ui_text(!c->human[i] ? "CPU" : PNAME[i], cw_v2(s.x, s.y - 40), 24, PCOL[i], CW_ALIGN_CENTER);
  }
  for (int g = 0; g < NP; g++) { /* a ghost bomber's marker */
    if (K.alive[g] || !c->eliminated[g]) continue;
    cw_vec2 s = n3_project((t3_vec3){ K.cur_x[g], 0.5f, K.cur_z[g] });
    cw_r2d_circle_lines(s, 18, 4, 20, PCOL[g]);
    cw_r2d_circle(s, 5, 10, PCOL[g]);
    if (c->human[g]) ui_text(K.call_cd[g] > 0 ? "ghost" : "A: bomb", cw_v2(s.x, s.y - 44), 22, PCOL[g], CW_ALIGN_CENTER);
  }
  if (K.warn && (c->tick / 15) % 2 == 0) ui_text("The ring is shrinking!", cw_v2(STAGE_W / 2, 640), 44, cw_rgba(1, 0.6f, 0.3f, 1), CW_ALIGN_CENTER);
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) {
  if (c->eliminated[p]) snprintf(buf, (size_t)cap, "out %ds", (int)(c->score[p] / 60));
  else snprintf(buf, (size_t)cap, "%s", c->score[p] >= 20000 ? "winner" : "alive");
}

const mg_def mg_blast = {
  .id = "blast", .name = "Blast Ring", .tagline = "Bombs shove, the ring shrinks. Stay on.",
  .how = { "Stick: roll", "A: dash (knocks others hard)", "Out? Aim a marker, A drops a bomb" },
  .buttons = 1, .keys = "SA",
  .kind = "3D", .scene = "scenes/blast.cwscene", .seconds = 60, .unlock_points = 8, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
