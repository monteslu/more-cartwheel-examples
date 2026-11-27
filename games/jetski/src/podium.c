/* The podium after a championship race: a judges' pier beside the finish, three tiers on it, the first three riders
 * parked on them with their arms up, the camera slowly round the front. The pier stands on piles driven into the bed
 * (the sea comes and goes on its legs, as on the ramps), its deck over the highest water the next half minute brings.
 *
 * Everything it does is measured for the grounding gate (debug podium.*): the lowest point of each parked hull against
 * the top of its tier, each pile's foot against the bed under it, the water against the deck's underside, and the
 * camera against the water and the land. sw podium_lift <m> raises the craft off their tiers (the gate's control). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/prompt.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene.h"
#include "game.h"

#define POD_W 15.6f    /* the pier's deck, across its front */
#define POD_D 7.4f     /* and front to back */
#define POD_SLAB 0.32f /* its thickness */
#define TIER_W 4.6f
#define TIER_D 5.4f
#define PILES_X 4
#define PILES_Z 2
static const float TIER_H[3] = { 1.35f, 0.88f, 0.48f };   /* 1st, 2nd, 3rd: over the deck */
static const float TIER_X[3] = { 0.0f, -4.95f, 4.95f };   /* across the pier: 2nd on the viewer's left, 3rd on the right */
static const float TIER_RGB[3][3] = { { 0.9f, 0.62f, 0.12f }, { 0.5f, 0.55f, 0.62f }, { 0.55f, 0.28f, 0.12f } };

int g_podium_on;      /* the podium is up (the sim holds the craft still) */
int g_podium_use = 1; /* sw podium 0: no podium, straight to the results (the menu gate's control) */
float g_podium_lift;  /* sw podium_lift <m>: the parked craft this much over their tiers (the grounding gate's control) */
/* podium.site: x, z, yaw, deck top. podium.meas: worst hull gap over a tier (m, + above it), worst pile foot over the
 * bed (m, + a pile that stops short), water over the deck's underside (m, + wet), the camera's lowest over the water
 * (m), frames it was over land, frames measured */
float g_podium_site[4], g_podium_meas[6];
int32_t g_podium_who[3]; /* the racers on the 1st, 2nd and 3rd tiers */

static cw_node pier_n, tier_n[3], pile_n[PILES_X * PILES_Z], rail_n[2], band_n[3];
static float pier_x, pier_z, pier_yaw, deck_top, pile_foot[PILES_X * PILES_Z], cam_t;
static t3_material *mat_new(float r, float g, float b, float rough, float metal) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = (t3_color){ r, g, b };
  m->roughness = rough, m->metalness = metal;
  return m;
}
static cw_node box_node(cw_node parent, const char *name, float w, float h, float d, t3_material *m) {
  t3_geometry *g = t3_box_geometry_new(w, h, d, 1, 1, 1);
  t3_mesh *mesh = t3_mesh_new(g, m);
  mesh->base.cast_shadow = true, mesh->base.receive_shadow = true;
  cw_node n = cw_node_new("Node3D", name);
  cw_node_add_child(parent, n);
  cw_node3d_set_object(n, (t3_object *)mesh);
  t3_release(mesh);
  t3_release(g);
  return n;
}

void podium_init(cw_node parent) {
  /* a composite deck (matte, a little worn), white tier tops, the tiers' fronts in gold, silver and bronze paint, dark steel piles */
  t3_material *deck = mat_new(0.30f, 0.27f, 0.23f, 0.86f, 0.0f), *top = mat_new(0.86f, 0.87f, 0.88f, 0.7f, 0.0f), *steel = mat_new(0.16f, 0.17f, 0.18f, 0.55f, 0.6f);
  pier_n = cw_node_new("Node3D", "Podium");
  cw_node_add_child(parent, pier_n);
  cw_node slab = box_node(pier_n, "Deck", POD_W, POD_SLAB, POD_D, deck);
  cw_node3d_set_position(slab, (t3_vec3){ 0, -POD_SLAB * 0.5f, 0 });
  for (int k = 0; k < 3; k++) {
    char name[16];
    snprintf(name, sizeof name, "Tier%d", k + 1);
    tier_n[k] = box_node(pier_n, name, TIER_W, TIER_H[k], TIER_D, top);
    cw_node3d_set_position(tier_n[k], (t3_vec3){ TIER_X[k], TIER_H[k] * 0.5f, 0 });
    /* the place's colour: a painted band round the tier under its top edge, standing just proud of its faces */
    t3_material *paint = mat_new(TIER_RGB[k][0], TIER_RGB[k][1], TIER_RGB[k][2], 0.3f, 0.85f);
    snprintf(name, sizeof name, "Band%d", k + 1);
    band_n[k] = box_node(pier_n, name, TIER_W + 0.04f, TIER_H[k] * 0.62f, TIER_D + 0.04f, paint);
    cw_node3d_set_position(band_n[k], (t3_vec3){ TIER_X[k], TIER_H[k] * 0.38f - 0.06f, 0 });
    t3_release(paint);
  }
  /* a rail along the back and the ends, on posts that stand on the deck */
  rail_n[0] = box_node(pier_n, "RailBack", POD_W - 0.2f, 0.08f, 0.08f, steel);
  cw_node3d_set_position(rail_n[0], (t3_vec3){ 0, 1.05f, -POD_D * 0.5f + 0.12f });
  rail_n[1] = box_node(pier_n, "RailPosts", POD_W - 0.2f, 1.05f, 0.06f, steel);
  cw_node3d_set_position(rail_n[1], (t3_vec3){ 0, 0.525f, -POD_D * 0.5f + 0.12f });
  cw_node3d_set_scale(rail_n[1], (t3_vec3){ 1, 1, 1 });
  for (int k = 0; k < PILES_X * PILES_Z; k++) {
    char name[16];
    snprintf(name, sizeof name, "Pile%d", k);
    t3_geometry *g = t3_cylinder_geometry_new(0.24f, 0.24f, 1.0f, 12, 1, false);
    t3_mesh *m = t3_mesh_new(g, steel);
    m->base.cast_shadow = true, m->base.receive_shadow = true;
    pile_n[k] = cw_node_new("Node3D", name);
    cw_node_add_child(pier_n, pile_n[k]);
    cw_node3d_set_object(pile_n[k], (t3_object *)m);
    t3_release(m); t3_release(g);
  }
  t3_release(deck); t3_release(top); t3_release(steel);
  cw_node_set(pier_n, "visible", cw_val_bool(false));
}

/* the pier's own frame: across (+x toward the viewer's right), up, out of its front (+z, toward the course) */
static void pier_point(float lx, float lz, float *x, float *z) {
  float s, c;
  cw_sincos(pier_yaw, &s, &c);
  *x = pier_x + c * lx + s * lz, *z = pier_z - s * lx + c * lz;
}
static void pile_local(int k, float *lx, float *lz) {
  *lx = (-0.5f + ((float)(k % PILES_X) + 0.5f) / PILES_X) * (POD_W - 1.2f);
  *lz = (k / PILES_X ? 1.0f : -1.0f) * (POD_D * 0.5f - 0.6f);
}
static float water_now(float x, float z, double t) { return cw_water_height_at(G.water, x, z, t); }

/* a place for the pier: beside the course near the line, on a bed 0.7 m or more under the water (the deeper the later it is tried), the camera's arc in front
 * of it over open water. False: none on this course (the results come at once) */
int32_t g_podium_why[5]; /* places tried and turned down: land or shoal under it, too deep, the camera's arc, a mark, a ramp */
static bool find_site(void) {
  memset(g_podium_why, 0, sizeof g_podium_why);
  float level = G.tide + wx_surge();
  static const float DEEP[3] = { 14.0f, 30.0f, 80.0f };
  for (int pass = 0; pass < 3; pass++) {
    float deep = DEEP[pass];
    for (float back = 30; back <= 900 && back < COURSE_LEN; back += 15) for (int sd = 0; sd < 2; sd++) for (float off = 24; off <= 90; off += 6) {
      float s = COURSE_LEN - back, tx, tz, dx, dz;
      track_at(s, &tx, &tz, &dx, &dz);
      float rx = sd ? dz : -dz, rz = sd ? -dx : dx;
      pier_x = tx + rx * off, pier_z = tz + rz * off;
      pier_yaw = cw_atan2(-rx, -rz); /* its front to the course */
      bool ok = true;
      for (int i = 0; i <= 4 && ok; i++) for (int j = 0; j <= 2 && ok; j++) {
        float x, z, lx = (-0.5f + (float)i / 4.0f) * (POD_W + 3.0f), lz = (-0.5f + (float)j / 2.0f) * (POD_D + 3.0f);
        pier_point(lx, lz, &x, &z);
        float g = ground_at(x, z);
        if (g > level - 0.7f) ok = false, g_podium_why[0]++;
        else if (g < level - deep) ok = false, g_podium_why[1]++;
      }
      /* the camera's arc: open water, no land within 4 m of it */
      for (float a = -0.45f; a <= 0.45f && ok; a += 0.15f) for (float r = 9.0f; r <= 15.0f && ok; r += 2.0f) {
        float x, z, sa, ca;
        cw_sincos(a, &sa, &ca);
        pier_point(sa * r, POD_D * 0.5f + ca * r, &x, &z);
        if (ground_at(x, z) > level - 0.2f) ok = false, g_podium_why[2]++;
      }
      /* clear of the marks and the ramps */
      for (int b = 0; b < BUOY_N && ok; b++) { float ex = BUOYS[b][0] - pier_x, ez = BUOYS[b][1] - pier_z; if (ex * ex + ez * ez < 16.0f * 16.0f) ok = false, g_podium_why[3]++; }
      for (int r = 0; r < RAMPS && ok; r++) { float ex = G.ramp_x[r] - pier_x, ez = G.ramp_z[r] - pier_z; if (ex * ex + ez * ez < 30.0f * 30.0f) ok = false, g_podium_why[4]++; }
      if (ok) return true;
    }
  }
  return false;
}

/* the racers on the tiers: the race's first three in order */
static void park(void) {
  for (int i = 0; i < RACERS; i++) {
    craft *c = &G.c[i];
    c->vx = c->vy = c->vz = 0, c->speed = 0, c->pr = c->rr = 0;
  }
  for (int k = 0; k < 3; k++) {
    int who = G.order[k];
    g_podium_who[k] = who;
    craft *c = &G.c[who];
    float x, z;
    pier_point(TIER_X[k], 0.15f, &x, &z);
    c->x = c->px = x, c->z = c->pz = z;
    /* the hull's lowest point stands this far under the craft's middle: the sit-down's keel, the stand-up's raised hull */
    float keel = g_craft == CRAFT_STAND ? -0.155f : 0.20f;
    c->y = c->py = deck_top + TIER_H[k] + keel + g_podium_lift;
    c->yaw = c->pyaw = pier_yaw, c->pitch = c->ppitch = 0, c->roll = c->proll = 0;
    c->air = false, c->air_time = 0, c->spill = 0, c->dive_t = 0, c->trick_kind = 0, c->legout = 0, c->kick = 0, c->load = 0, c->steer = 0, c->lean = 0;
    c->plane = 1, c->wobble = 0, c->pole_t = 0;
  }
}

bool podium_begin(void) {
  if (!g_podium_use || !cw_node_valid(pier_n) || g_mode != MODE_CHAMPIONSHIP) return false;
  if (!find_site()) { cw_log(CW_LOG_INFO, "jetski: no place for the podium on %s", DEF->name); return false; }
  /* the deck stands clear of the highest water the next 40 s bring anywhere under it, ripples and all, by 0.45 m */
  double t0 = cw_water_time(G.water);
  float hi = -1e9f;
  for (int s = 0; s <= 80; s++) for (int i = 0; i <= 4; i++) for (int j = 0; j <= 2; j++) {
    float x, z;
    pier_point((-0.5f + (float)i / 4.0f) * POD_W, (-0.5f + (float)j / 2.0f) * POD_D, &x, &z);
    hi = fmaxf(hi, water_now(x, z, t0 + (double)s * 0.5) + cw_water_ripple_at(G.water, x, z));
  }
  deck_top = hi + 0.45f + POD_SLAB;
  cw_node3d_set_position(pier_n, (t3_vec3){ pier_x, deck_top, pier_z });
  cw_node3d_set_rotation(pier_n, (t3_vec3){ 0, pier_yaw, 0 });
  /* each pile from under the deck down into the bed, half a metre deep */
  for (int k = 0; k < PILES_X * PILES_Z; k++) {
    float lx, lz, x, z;
    pile_local(k, &lx, &lz);
    pier_point(lx, lz, &x, &z);
    float foot = ground_at(x, z) - 0.5f, top = deck_top - POD_SLAB, len = top - foot;
    pile_foot[k] = foot;
    cw_node3d_set_position(pile_n[k], (t3_vec3){ lx, -POD_SLAB - len * 0.5f, lz });
    cw_node3d_set_scale(pile_n[k], (t3_vec3){ 1, len, 1 });
  }
  cw_node_set(pier_n, "visible", cw_val_bool(true));
  g_podium_site[0] = pier_x, g_podium_site[1] = pier_z, g_podium_site[2] = pier_yaw, g_podium_site[3] = deck_top;
  memset(g_podium_meas, 0, sizeof g_podium_meas);
  g_podium_meas[1] = g_podium_meas[2] = -1e9f, g_podium_meas[3] = 1e9f;
  g_podium_on = 1;
  cam_t = 0;
  park();
  G.cam[0] = 0; /* a cut */
  return true;
}
void podium_end(void) {
  g_podium_on = 0;
  if (cw_node_valid(pier_n)) cw_node_set(pier_n, "visible", cw_val_bool(false));
}
bool podium_shows(int i) { if (!g_podium_on) return true; for (int k = 0; k < 3; k++) if (g_podium_who[k] == i) return true; return false; }

/* the sim's tick while the podium is up: the parked craft stay parked (a new place set by sw podium_lift is taken at once) */
void podium_tick(void) {
  if (!g_podium_on) return;
  park();
}

/* the camera: in front of the pier, a little above the top tier, swinging slowly a few degrees either way. Never shakes:
 * one smooth sine, a long period */
bool podium_camera(float dt) {
  if (!g_podium_on) return false;
  bool cut = G.cam[0] == 0;
  memcpy(G.cam_prev, G.cam, sizeof G.cam);
  memcpy(G.look_prev, G.look, sizeof G.look);
  G.cam_fov_prev = G.cam_fov;
  cam_t += dt;
  float a = 0.3f * sinf(cam_t * 0.16f), r = 11.0f - 1.0f * (1.0f - cw_exp(-cam_t * 0.25f)), sa, ca, x, z, lx, lz;
  cw_sincos(a, &sa, &ca);
  pier_point(sa * r, POD_D * 0.5f + ca * r, &x, &z);
  pier_point(0, 0, &lx, &lz);
  float y = deck_top + TIER_H[0] + 1.3f, wh = water_now(x, z, G.wt);
  if (y < wh + 1.5f) y = wh + 1.5f;
  G.cam[0] = x, G.cam[1] = y, G.cam[2] = z;
  G.look[0] = lx, G.look[1] = deck_top + TIER_H[0] + 0.9f, G.look[2] = lz;
  G.cam_fov = 46.0f;
  G.cam_roll = G.cam_roll_prev = 0;
  if (cut) { memcpy(G.cam_prev, G.cam, sizeof G.cam); memcpy(G.look_prev, G.look, sizeof G.look); G.cam_fov_prev = G.cam_fov; }
  cw_water_ripple_follow(G.water, lx, lz);
  /* measured as drawn: the camera over the water, and never over land */
  g_podium_meas[3] = fminf(g_podium_meas[3], y - wh);
  if (ground_at(x, z) > wh - 0.3f) g_podium_meas[4]++;
  return true;
}

/* the lowest point of what is drawn of each parked hull, the piles' feet and the water under the deck, every frame */
void podium_measure(float (*hull_low)(int)) {
  if (!g_podium_on) return;
  for (int k = 0; k < 3; k++) {
    float low = hull_low(g_podium_who[k]);
    /* a hull sunk into its tier counts as much as one standing off it */
    if (low < -1e8f) continue;
    float gap = low - (deck_top + TIER_H[k]);
    if (fabsf(gap) > fabsf(g_podium_meas[0])) g_podium_meas[0] = gap;
  }
  for (int k = 0; k < PILES_X * PILES_Z; k++) {
    float lx, lz, x, z;
    pile_local(k, &lx, &lz);
    pier_point(lx, lz, &x, &z);
    g_podium_meas[1] = fmaxf(g_podium_meas[1], pile_foot[k] - ground_at(x, z));
  }
  for (int i = 0; i <= 4; i++) for (int j = 0; j <= 2; j++) {
    float x, z;
    pier_point((-0.5f + (float)i / 4.0f) * POD_W, (-0.5f + (float)j / 2.0f) * POD_D, &x, &z);
    g_podium_meas[2] = fmaxf(g_podium_meas[2], water_now(x, z, G.wt) + cw_water_ripple_at(G.water, x, z) - (deck_top - POD_SLAB));
  }
  g_podium_meas[5]++;
}

/* the names over the riders and what to press, drawn by the menu's podium page */
static bool project(t3_vec3 p, float w, float h, cw_vec2 *out) {
  t3_camera *cam = cw_camera3d_t3(G.camera);
  if (!cam) return false;
  t3_vec3 v = t3_vec3_apply_mat4(p, &cam->matrix_world_inverse);
  if (v.z > -1) return false;
  t3_vec3 q = t3_vec3_apply_mat4(v, &cam->projection_matrix);
  out->x = (q.x * 0.5f + 0.5f) * w, out->y = (0.5f - q.y * 0.5f) * h;
  return true;
}
float g_podium_label[3][2]; /* where each place's label is drawn (canvas pixels), for the gate */
void podium_draw(cw_font big, cw_font small, float w, float h) {
  if (!g_podium_on) return;
  float u = h / 1080.0f;
  static const char *const ORD[3] = { "1st", "2nd", "3rd" };
  for (int k = 0; k < 3; k++) {
    float x, z;
    pier_point(TIER_X[k], 0, &x, &z);
    cw_vec2 p;
    g_podium_label[k][0] = g_podium_label[k][1] = -1;
    if (!project((t3_vec3){ x, deck_top + TIER_H[k] + 2.35f, z }, w, h, &p)) continue;
    const float *tc = TIER_RGB[k];
    cw_text_style st = { .size = 64 * u, .color = cw_rgba(tc[0] * 0.5f + 0.5f, tc[1] * 0.5f + 0.5f, tc[2] * 0.5f + 0.5f, 1), .outline_width = 3.2f * u, .outline_color = cw_rgba(0, 0, 0, 0.6f) };
    cw_text_metrics m = cw_font_measure(big, ORD[k], &st);
    float ty = p.y - m.height - 34 * u * (cw_text_drawn_size(small, 34 * u) / (34 * u));
    cw_r2d_text(big, ORD[k], cw_v2(p.x - m.width * 0.5f, ty), &st);
    cw_text_style ns = { .size = 34 * u, .color = cw_rgba(1, 1, 1, 0.96f), .outline_width = 1.8f * u, .outline_color = cw_rgba(0, 0, 0, 0.6f) };
    const char *name = seat_name(g_podium_who[k]);
    cw_text_metrics nm = cw_font_measure(small, name, &ns);
    cw_r2d_text(small, name, cw_v2(p.x - nm.width * 0.5f, ty + m.height), &ns);
    g_podium_label[k][0] = p.x, g_podium_label[k][1] = ty;
  }
  cw_text_style hs = { .size = 56 * u, .color = cw_rgba(1, 1, 1, 0.97f), .outline_width = 2.6f * u, .outline_color = cw_rgba(0, 0, 0, 0.5f) };
  cw_text_metrics hm = cw_font_measure(big, DEF->name, &hs);
  cw_r2d_text(big, DEF->name, cw_v2(w * 0.5f - hm.width * 0.5f, 46 * u), &hs);
  cw_prompt_text(small, "{ui_accept} results", 0, cw_v2(w * 0.5f, h - 40 * u - cw_text_drawn_size(small, 32 * u) * 1.3f), 32 * u, cw_rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER);
}
