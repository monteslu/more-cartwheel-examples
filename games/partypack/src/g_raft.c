/* Raft Rush (3D, water): four rafts bob on a wavy lagoon. Paddle into the
 * floating buoys to collect them (gold ones count three). Rafts shove each
 * other. A boosts your paddle for a burst. Most buoys wins. */
#include "mg.h"
#include "cartwheel/water.h"

#define NBUOY 7
static struct {
  cw_node raft[NP], rider[NP], face[NP], water;
  int boost_cd[NP], boost_fx[NP], got[NP];
  struct { bool on; float x, z; int value, respawn; cw_node node; } buoy[NBUOY];
  uint32_t seed;
  int hint_t[NP];
} R;

static void make_raft(int i, float x, float z, bool cpu) {
  char nm[16];
  snprintf(nm, sizeof nm, "Raft%d", i);
  cw_node b = cw_node_new("RigidBody3D", nm);
  cw_node_set(b, "density", cw_val_float(420));
  cw_node_set(b, "linear_damp", cw_val_float(0.9));
  cw_node_set(b, "angular_damp", cw_val_float(3));
  cw_node_set(b, "can_sleep", cw_val_bool(false));
  cw_node_set(b, "friction", cw_val_float(0.1));
  cw_node sh = cw_node_new("CollisionShape3D", "Shape");
  cw_node_set(sh, "shape", cw_val_enum("BOX"));
  cw_node_set(sh, "size", cw_val_vec3(2, 0.4f, 2));
  cw_node_add_child(b, sh);
  cw_color c = PCOL[i];
  cw_color lin = { c.r * c.r, c.g * c.g, c.b * c.b, 1 };
  cw_node deck = n3_box(b, "Deck", (t3_vec3){ 0, 0, 0 }, (t3_vec3){ 2, 0.4f, 2 }, (cw_color){ 0.5f, 0.32f, 0.16f, 1 });
  cw_node_set(deck, "roughness", cw_val_float(0.8));
  for (int p = -1; p <= 1; p++) /* plank edges */
    n3_box(b, "Plank", (t3_vec3){ p * 0.66f, 0.205f, 0 }, (t3_vec3){ 0.6f, 0.03f, 1.9f }, (cw_color){ 0.62f, 0.42f, 0.22f, 1 });
  n3_prim(b, "Mast", "CYLINDER", (t3_vec3){ -0.7f, 0.9f, -0.7f }, (t3_vec3){ 0.08f, 1.5f, 0.08f }, (cw_color){ 0.35f, 0.25f, 0.15f, 1 });
  cw_node flag = n3_box(b, "Flag", (t3_vec3){ -0.7f, 1.45f, -0.3f }, (t3_vec3){ 0.04f, 0.4f, 0.75f }, lin);
  n3_emissive(flag, (cw_color){ lin.r * 0.3f, lin.g * 0.3f, lin.b * 0.3f, 1 });
  cw_node rider = n3_prim(b, "Rider", "SPHERE", (t3_vec3){ 0.1f, 0.62f, 0.1f }, (t3_vec3){ 0.9f, 0.9f, 0.9f }, lin);
  cw_node_set(rider, "roughness", cw_val_float(0.35));
  cw_node_set(rider, "clearcoat", cw_val_float(0.6));
  cw_node face = cw_node_new("Node3D", "Face");
  cw_node_add_child(b, face);
  cw_node3d_set_position(face, (t3_vec3){ 0.1f, 0.66f, 0.1f });
  for (int s = -1; s <= 1; s += 2) {
    n3_prim(face, "Eye", "SPHERE", (t3_vec3){ 0.16f * s, 0.1f, 0.36f }, (t3_vec3){ 0.24f, 0.28f, 0.18f }, (cw_color){ 1, 1, 1, 1 });
    cw_node pu = n3_prim(face, "Pupil", "SPHERE", (t3_vec3){ 0.16f * s, 0.09f, 0.45f }, (t3_vec3){ 0.11f, 0.13f, 0.07f }, (cw_color){ 0.02f, 0.02f, 0.04f, 1 });
    cw_node_set(pu, "cast_shadow", cw_val_bool(false));
  }
  if (cpu) {
    n3_prim(face, "Stalk", "CYLINDER", (t3_vec3){ 0, 0.55f, 0 }, (t3_vec3){ 0.05f, 0.3f, 0.05f }, (cw_color){ 0.7f, 0.7f, 0.75f, 1 });
    cw_node tip = n3_prim(face, "Tip", "SPHERE", (t3_vec3){ 0, 0.74f, 0 }, (t3_vec3){ 0.14f, 0.14f, 0.14f }, (cw_color){ 0.9f, 0.9f, 0.95f, 1 });
    n3_emissive(tip, (cw_color){ 0.3f, 0.9f, 0.4f, 1 });
  }
  cw_node fl = cw_node_new("Buoyancy3D", "Float");
  cw_node_add_child(b, fl);
  cw_node_add_child(n3_level(), b);
  cw_node3d_set_position(b, (t3_vec3){ x, 0.1f, z });
  R.raft[i] = b; R.rider[i] = rider; R.face[i] = face;
}

static void place_buoy(mg_ctx *c, int k) {
  float x, z;
  for (int tries = 0; tries < 12; tries++) {
    x = cw_rng_range(&c->rng, -16, 16);
    z = cw_rng_range(&c->rng, -10, 10);
    bool ok = true;
    for (int j = 0; j < NBUOY; j++) if (j != k && R.buoy[j].on) { float dx = R.buoy[j].x - x, dz = R.buoy[j].z - z; if (dx * dx + dz * dz < 16) ok = false; }
    for (int i = 0; i < NP; i++) { t3_vec3 p = cw_node3d_position(R.raft[i]); if ((p.x - x) * (p.x - x) + (p.z - z) * (p.z - z) < 6) ok = false; }
    if (ok) break;
  }
  int gold = cw_rng_int(&c->rng, 0, 99) < 16;
  R.buoy[k].on = true; R.buoy[k].x = x; R.buoy[k].z = z; R.buoy[k].value = gold ? 3 : 1;
  cw_color col = gold ? (cw_color){ 0.9f, 0.65f, 0.05f, 1 } : (cw_color){ 0.9f, 0.18f, 0.12f, 1 };
  R.buoy[k].node = n3_prim(n3_level(), "Buoy", "SPHERE", (t3_vec3){ x, 0.2f, z }, (t3_vec3){ gold ? 0.95f : 0.75f, gold ? 0.95f : 0.75f, gold ? 0.95f : 0.75f }, col);
  cw_node_set(R.buoy[k].node, "roughness", cw_val_float(0.3));
  cw_node_set(R.buoy[k].node, "clearcoat", cw_val_float(0.8));
  n3_emissive(R.buoy[k].node, gold ? (cw_color){ 0.9f, 0.55f, 0.02f, 1 } : (cw_color){ 0.35f, 0.03f, 0.02f, 1 });
}

static void start(mg_ctx *c) {
  memset(&R, 0, sizeof R);
  n3_fx_reset();
  R.water = cw_water_find(cw_scene_app_tree());
  static const float sx[4] = { -9, 9, -9, 9 }, sz[4] = { -5, -5, 5, 5 };
  for (int i = 0; i < NP; i++) make_raft(i, sx[i], sz[i], i >= c->humans);
  for (int k = 0; k < NBUOY; k++) place_buoy(c, k);
  R.seed = cw_rng_u32(&c->rng);
  n3_camera(0, 12, 19, 0, -0.5f, 0);
}

static void tick(mg_ctx *c) {
  const float dt = 1.0f / 60.0f;
  n3_fx_tick();
  double wt = cw_water_time(R.water);
  for (int i = 0; i < NP; i++) {
    const mg_in *in = &c->in[i];
    cw_value v;
    cw_node_get_prop(R.raft[i], "linear_velocity", &v);
    float vx = v.v[0], vz = v.v[2];
    float ln = sqrtf(in->x * in->x + in->y * in->y);
    float ix = ln > 1 ? in->x / ln : in->x, iz = ln > 1 ? in->y / ln : in->y;
    float acc = 9 * dt;
    vx += fclampf(ix * 5.2f - vx, -acc, acc);
    vz += fclampf(iz * 5.2f - vz, -acc, acc);
    if (R.boost_cd[i] > 0) R.boost_cd[i]--;
    t3_vec3 p = cw_node3d_position(R.raft[i]);
    if (in->pa && R.boost_cd[i] == 0 && ln > 0.2f) {
      vx += ix * 7; vz += iz * 7;
      R.boost_cd[i] = 80; R.boost_fx[i] = 14;
      sfx(SFX_SPLASH, 0.35f, 1.4f);
      n3_fx("splash", (t3_vec3){ p.x - ix, 0.1f, p.z - iz }, 0.6f);
    }
    if (R.boost_fx[i] > 0) { R.boost_fx[i]--; if (R.boost_fx[i] % 4 == 0) n3_fx("dust", (t3_vec3){ p.x, 0.1f, p.z }, 0.5f); }
    cw_node_set(R.raft[i], "linear_velocity", cw_val_vec3(vx, v.v[1], vz));
    if (fabsf(vx) + fabsf(vz) > 0.6f) cw_node3d_set_rotation(R.face[i], (t3_vec3){ 0, cw_atan2(vx, vz) - 0, 0 });
    /* an upright raft: lean it back toward flat when it tips far (rivals bump hard) */
    cw_value av;
    cw_node_get_prop(R.raft[i], "angular_velocity", &av);
    t3_quat q = cw_node3d_quaternion(R.raft[i]);
    (void)q;
  }
  for (int k = 0; k < NBUOY; k++) {
    if (!R.buoy[k].on) {
      if (--R.buoy[k].respawn <= 0) place_buoy(c, k);
      continue;
    }
    float bx = R.buoy[k].x, bz = R.buoy[k].z;
    float h = cw_water_height_at(R.water, bx, bz, wt);
    float bob = sinf((float)c->tick * 0.07f + (float)k) * 0.05f;
    n3_pos(R.buoy[k].node, bx, h + 0.15f + bob, bz);
    for (int i = 0; i < NP; i++) {
      t3_vec3 p = cw_node3d_position(R.raft[i]);
      float dx = p.x - bx, dz = p.z - bz;
      if (dx * dx + dz * dz < 1.5f * 1.5f) {
        R.got[i] += R.buoy[k].value;
        c->score[i] = (float)R.got[i];
        cw_node_free(R.buoy[k].node);
        R.buoy[k].on = false;
        R.buoy[k].respawn = 40;
        n3_fx("splash", (t3_vec3){ bx, h, bz }, 1);
        n3_flash((t3_vec3){ bx, 1.5f, bz }, R.buoy[k].value == 3 ? (cw_color){ 1, 0.8f, 0.2f, 1 } : (cw_color){ 1, 0.5f, 0.4f, 1 }, 25, 6);
        sfx(R.buoy[k].value == 3 ? SFX_WIN : SFX_COIN, 0.6f, 1 + 0.1f * (float)i);
        char s[8];
        snprintf(s, sizeof s, "+%d", R.buoy[k].value);
        R.hint_t[i] = 30;
        (void)s;
        break;
      }
    }
  }
  n3_camera(0, 12, 19, 0, -0.5f, 0);
  (void)dt;
}

static void think(mg_ctx *c, int p, mg_in *out) {
  memset(out, 0, sizeof *out);
  t3_vec3 me = cw_node3d_position(R.raft[p]);
  int best = -1;
  float bd = 1e9f;
  for (int k = 0; k < NBUOY; k++) {
    if (!R.buoy[k].on) continue;
    float dx = R.buoy[k].x - me.x, dz = R.buoy[k].z - me.z;
    float d = dx * dx + dz * dz - (R.buoy[k].value == 3 ? 40.0f : 0.0f);
    /* easy CPUs sometimes chase a farther one: a hash of the buoy and bot decides */
    uint32_t h = hash_u32(R.seed + (uint32_t)p, (uint32_t)k);
    if (c->skill == 0 && (h % 3) == 0) d += 120;
    if (d < bd) { bd = d; best = k; }
  }
  if (best < 0) return;
  float dx = R.buoy[best].x - me.x, dz = R.buoy[best].z - me.z, l = sqrtf(dx * dx + dz * dz);
  if (l > 0.05f) { out->x = dx / l; out->y = dz / l; }
  if (c->skill > 0 && l > 6 && R.boost_cd[p] == 0 && c->skill + (int)(hash_u32(R.seed, (uint32_t)(c->tick / 30 + p)) % 3) >= 3) out->a = true;
}

static void draw(mg_ctx *c, float alpha, float t) {
  (void)alpha; (void)t;
  for (int i = 0; i < NP; i++) {
    t3_vec3 p = cw_node3d_position(R.raft[i]);
    cw_vec2 s = n3_project((t3_vec3){ p.x, p.y + 2.3f, p.z });
    cw_vec2 tri[3] = { cw_v2(s.x - 12, s.y - 10), cw_v2(s.x + 12, s.y - 10), cw_v2(s.x, s.y + 4) };
    cw_r2d_polygon(tri, 3, PCOL[i]);
    ui_text(i >= c->humans ? "CPU" : PNAME[i], cw_v2(s.x, s.y - 40), 24, PCOL[i], CW_ALIGN_CENTER);
  }
}

static void score_text(mg_ctx *c, int p, char *buf, int cap) { snprintf(buf, (size_t)cap, "%d", (int)c->score[p]); }

const mg_def mg_raft = {
  .id = "raft", .name = "Raft Rush", .tagline = "Paddle for the buoys. Gold ones count three.",
  .how = { "Stick: paddle", "A: burst of speed", "Bump rivals off the buoys" },
  .kind = "3D", .scene = "scenes/raft.cwscene", .seconds = 45, .unlock_points = 0, .higher_is_better = true,
  .start = start, .tick = tick, .think = think, .draw = draw, .score_text = score_text,
};
