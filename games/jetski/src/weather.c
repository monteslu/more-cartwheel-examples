/* The day's weather and what it does.
 *
 * A course is made in one weather (its usual: Gale Bank a storm, the others a chop). A race can run calm, choppy or in a
 * storm: the swell and the chop grow or fall against the course as made, the wind blows harder, the mean level of the sea
 * stands higher on the shore the rougher it is (the surge a big sea piles up), and the look follows: glassy, mirror-like
 * water in a calm, dark, heavy, foam-streaked water under rain in a storm.
 *
 * The level opens and shuts things, so a sea state is a route choice: rocks and shoals stand out of a calm and are solid,
 * and a rough sea covers them; each course has one way that only some seas allow (a reef that is awash only when the
 * sea is up, an arch the storm's surge fills, a slab, a barge or a wreck whose foot only a rough sea reaches).
 *
 * The course's big water event: once a race, a set of big waves comes through the pack, carried in the water itself (a
 * band of the wave mask moving across the sea that lifts the swell, so the surface drawn and every height the craft
 * ride are the same), not a shove or a picture.
 *
 * Also here: stunt mode's scoring and the rivals' pace through a championship. Everything in the race runs on the fixed
 * tick with + - * / and cw_ math: the same bits on every host. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/particles.h"
#include "cartwheel/platform.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "cartwheel/scene_file.h"
#include "game.h"

const char *const WX_NAMES[WX_N] = { "Calm", "Choppy", "Storm" };
int g_wx_force = -1;   /* debug: weather <n> */
int g_route_force = -1; /* debug: route_force 1 every route open, 0 every route shut (the weather gate's controls) */
int g_event_on = 1;    /* debug: event 0, no event */
float g_event_amp = 1.7f; /* metres the event's set stands over the swell (event_amp 0: the band without waves, its gate's control) */
int g_cup_day;         /* the championship's day, 0.. */
wx_probe g_wx;
stunt_state g_stunt;
static int race_wx = -1;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static uint32_t hash2(int a, int b) { int v[2] = { a, b }; return cw_hash32(v, sizeof v, 0x5eed1u); }
static float rnd(uint32_t h) { return (float)(h & 0xffffu) / 65536.0f; }

int wx_usual(int level) { return !strcmp(COURSES[level]->id, "storm") ? WX_STORM : WX_CHOPPY; }
int wx_now(void) { return g_wx_force >= 0 ? g_wx_force : race_wx >= 0 ? race_wx : wx_usual(G.level); }
void wx_set_race(int w) { race_wx = w; }
/* the weathers against each other: swell, chop, the surge on the shore, the wind */
static const float SWELL_T[WX_N] = { 0.3f, 0.85f, 1.0f }, CHOP_T[WX_N] = { 0.32f, 0.85f, 1.15f }, SURGE_T[WX_N] = { 0.0f, 0.45f, 0.9f }, WIND_T[WX_N] = { 1.5f, 8.0f, 16.0f };
static const float SHELTER[4] = { 1, 1, 0.55f, 0.2f }; /* the scene's wave_shelter: how far the mask holds each wave */
static struct { int on, done; float c, l0, dx, dz, P, r, end_c; int32_t tick0; } ev;
float wx_wave_k(int i) {
  int w = wx_now(), u = wx_usual(G.level);
  float k = i < 2 ? SWELL_T[w] / SWELL_T[u] : CHOP_T[w] / CHOP_T[u];
  /* a storm at Hard stays inside what the course's ramps stand: never past 1.2 of the course as made at its biggest */
  static const float DIFF_SEA[3] = { 0.8f, 1.0f, 1.18f };
  if (k > 1 && k * DIFF_SEA[g_difficulty] > 1.2f) k = fmaxf(1.0f, 1.2f / DIFF_SEA[g_difficulty]);
  return k;
}
float wx_steep_k(int i) { return ev.on ? 1.0f / (1.0f + ev.P * ev.r * SHELTER[i]) : 1.0f; }
float wx_surge(void) { return SURGE_T[wx_now()] - SURGE_T[wx_usual(G.level)]; }
bool wx_rain(void) { return wx_now() == WX_STORM; }
void wx_wind(float *x, float *z) {
  float s, c, w = WIND_T[wx_now()];
  cw_sincos(DEF->waves[0][0] * 0.01745329f, &s, &c);
  *x = c * w, *z = s * w;
}
float wx_pace(void) {
  static const float DAY[LEVELS] = { 1.0f, 1.006f, 1.012f, 1.018f, 1.024f }; /* the first day as the courses always were, then a little more each day */
  return g_mode == MODE_CHAMPIONSHIP && G.state != RACE_ATTRACT ? DAY[g_cup_day < 0 ? 0 : g_cup_day >= LEVELS ? LEVELS - 1 : g_cup_day] : 1.0f;
}

/* ── the ground, read from the course's own heights (the terrain answers only once drawn: this is the same map, now) ── */
static uint16_t *hts;
static int hts_level = -1;
static float gnd(float x, float z) {
  if (!hts) return -40;
  int N = DEF->quads + 1;
  float u = clampf(x / DEF->spacing + (float)DEF->quads * 0.5f, 0, (float)N - 1.001f), v = clampf(z / DEF->spacing + (float)DEF->quads * 0.5f, 0, (float)N - 1.001f);
  int i = (int)u, j = (int)v;
  float a = u - (float)i, b = v - (float)j, k = (DEF->maxh - DEF->minh) / 65535.0f;
#define HH(ii, jj) (DEF->minh + (float)hts[(jj) * N + (ii)] * k)
  return (HH(i, j) * (1 - a) + HH(i + 1, j) * a) * (1 - b) + (HH(i, j + 1) * (1 - a) + HH(i + 1, j + 1) * a) * b;
#undef HH
}

/* ── routes: one a course ── */
enum { RT_NONE, RT_REEF, RT_RAMP, RT_ARCH };
#define OPEN(w) (1u << (w))
typedef struct route_def {
  const char *id;
  int type;
  unsigned open;               /* the weathers it is open in */
  float ax, az, gx, gz, bx, bz; /* a reef: the way in, the gap, the way out */
  float s_in, s_out;
  float x, z, yaw, low, hl, hw, rise; /* a ramp: its middle, heading, the height of its foot, half length, half width, rise */
  float col[3];
  const char *open_line, *shut_line;
} route_def;
static const route_def ROUTES[] = {
  { "atoll", RT_REEF, OPEN(WX_CHOPPY) | OPEN(WX_STORM), 639.8f, -56.1f, 511.1f, -295.8f, 434.0f, -439.4f, 869, 1415, 0, 0, 0, 0.25f, 0, 0, 0, { 0, 0, 0 },
    "The reef past the lighthouse is awash: a short way through the rocks", "Calm: the reef past the lighthouse stands out of the water" },
  { "sound", RT_ARCH, OPEN(WX_CALM) | OPEN(WX_CHOPPY), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0 },
    "The falling tide opens the arch: the short way home", "Storm: the surge fills the arch, it stays shut" },
  { "canyon", RT_RAMP, OPEN(WX_STORM), 0, 0, 0, 0, 0, 0, 391, 0, -133.9f, -337.0f, 2.1379f, 0.25f, 18, 5, 4.2f, { 0.55f, 0.27f, 0.16f },
    "The flood is up to the red slab's foot: ride it for a jump", "The river is low: the red slab's foot stands dry" },
  { "harbour", RT_RAMP, OPEN(WX_CHOPPY) | OPEN(WX_STORM), 0, 0, 0, 0, 0, 0, 580, 0, 36.3f, 343.7f, 1.1172f, -0.1f, 18, 5, 4.2f, { 0.42f, 0.4f, 0.38f },
    "The swell washes up the sunken barge: ride its deck for a jump", "Calm: the sunken barge's deck stands out of the water" },
  { "storm", RT_RAMP, OPEN(WX_STORM), 0, 0, 0, 0, 0, 0, 3663, 0, -588.6f, -103.3f, 2.8446f, -0.1f, 18, 5, 4.6f, { 0.3f, 0.17f, 0.12f },
    "The storm runs up the wreck's deck: ride it for a jump", "The wreck's deck stands clear of the sea: no way up it" },
};
static const route_def *RT(void) {
  for (unsigned i = 0; i < sizeof ROUTES / sizeof ROUTES[0]; i++) if (!strcmp(ROUTES[i].id, DEF->id)) return &ROUTES[i];
  return NULL;
}
bool wx_route_open(void) {
  const route_def *r = RT();
  if (!r) return false;
  if (r->type == RT_ARCH) return hook_open();
  if (g_route_force >= 0) return g_route_force == 1;
  return (r->open & OPEN(wx_now())) != 0;
}
const char *wx_route_line(void) {
  const route_def *r = RT();
  if (!r) return "";
  if (r->type == RT_ARCH) return wx_now() == WX_STORM && g_route_force != 1 ? r->shut_line : r->open_line;
  return wx_route_open() ? r->open_line : r->shut_line;
}

/* a reef route's gap: the rocks across it, land to land */
#define BAR_MAX 40
static float bar_ext[2]; /* how far the bar reaches each way across the gap from its middle */
/* shoals: rocks by the line that a calm sea shows */
#define SHOALS 5
#define SHOAL_ROCKS 7
typedef struct rock { float x, z, r, top, bottom, yaw, sxz; int model; } rock;
static rock rocks[SHOALS * SHOAL_ROCKS + BAR_MAX * 2];
static int rock_n, bar_first, bar_n;
static float shoal_c[SHOALS][3]; /* x, z, radius */
static int shoal_n;
static float level(void) { return G.tide; } /* the mean level now: tide and surge */
static bool rock_solid(const rock *k) { return k->top > level() + 0.05f; }
static bool shoal_solid(int s) {
  for (int k = 0; k < SHOAL_ROCKS; k++) { const rock *q = &rocks[s * SHOAL_ROCKS + k]; if (q->r > 0 && rock_solid(q)) return true; }
  return false;
}
static void route_frame(const route_def *r, float *ux, float *uz, float *len_ag) {
  float dx = r->bx - r->ax, dz = r->bz - r->az, l = sqrtf(dx * dx + dz * dz);
  *ux = dx / l, *uz = dz / l;
  if (len_ag) *len_ag = (r->gx - r->ax) * *ux + (r->gz - r->az) * *uz;
}

static bool near_marks(float x, float z, float buoy_r, float line_r) {
  for (int b = 0; b < BUOY_N; b++) { float dx = x - BUOYS[b][0], dz = z - BUOYS[b][1]; if (dx * dx + dz * dz < buoy_r * buoy_r) return true; }
  for (int q = 0; q < RAMPS; q++) { float dx = x - DEF->ramps[q][0], dz = z - DEF->ramps[q][1]; if (dx * dx + dz * dz < 34 * 34) return true; }
  for (int i = 0; i < TRACK_N; i++) { float dx = x - TRACK[i][0], dz = z - TRACK[i][1]; if (dx * dx + dz * dz < line_r * line_r) return true; }
  const route_def *r = RT();
  if (r && r->type == RT_RAMP) { float dx = x - r->x, dz = z - r->z; if (dx * dx + dz * dz < 50 * 50) return true; }
  if (r && r->type == RT_REEF) {
    float pts[3][2] = { { r->ax, r->az }, { r->gx, r->gz }, { r->bx, r->bz } };
    for (int s = 0; s < 2; s++) for (int k = 0; k <= 10; k++) {
      float t = (float)k / 10, px = pts[s][0] + (pts[s + 1][0] - pts[s][0]) * t, pz = pts[s][1] + (pts[s + 1][1] - pts[s][1]) * t;
      if ((x - px) * (x - px) + (z - pz) * (z - pz) < 40 * 40) return true;
    }
  }
  if (DEF->hook.kind == 1 || DEF->hook.kind == 3) { float dx = x - DEF->hook.x, dz = z - DEF->hook.z; if (dx * dx + dz * dz < 60 * 60) return true; }
  return false;
}
static void rock_add(float x, float z, float top, float size, uint32_t h) {
  if (rock_n >= (int)(sizeof rocks / sizeof rocks[0])) return;
  rock *k = &rocks[rock_n++];
  k->x = x, k->z = z, k->top = top, k->sxz = size, k->r = 1.15f * size, k->yaw = rnd(h >> 3) * TAU, k->model = (int)(h % 3u);
  /* down to the bed under its whole foot, so no edge of it hangs in the water */
  float g = 1e9f;
  for (int a = 0; a < 8; a++) { float s, c; cw_sincos((float)a * 0.785398f, &s, &c); g = fminf(g, gnd(x + c * k->r, z + s * k->r)); }
  k->bottom = fminf(g, gnd(x, z)) - 0.5f;
}
/* the course's rocks: the shoals, by the line where it runs past shallow water, and a reef route's bar */
static void rocks_build(void) {
  rock_n = 0, shoal_n = 0, bar_n = 0;
  float last_s = -1e9f;
  for (int i = 0; i < TRACK_N && shoal_n < SHOALS; i += 3) {
    float s = (float)i / (float)TRACK_N * COURSE_LEN;
    if (s - last_s < 380 || s < 120) continue;
    bool put = false;
    for (int side = -1; side <= 1 && !put; side += 2)
      for (float off = 18; off <= 34 && !put; off += 4) {
        float x = TRACK[i][0] - TRACK[i][3] * off * (float)side, z = TRACK[i][1] + TRACK[i][2] * off * (float)side, g = gnd(x, z);
        if (g < -3.4f || g > -1.2f || near_marks(x, z, 26, 13)) continue;
        bool shallow = true;
        for (int a = 0; a < 6 && shallow; a++) { float sn, cs; cw_sincos((float)a * 1.047f, &sn, &cs); float gg = gnd(x + cs * 7, z + sn * 7); if (gg < -4.5f || gg > -0.3f) shallow = false; }
        if (!shallow) continue;
        shoal_c[shoal_n][0] = x, shoal_c[shoal_n][1] = z, shoal_c[shoal_n][2] = 0;
        for (int k = 0; k < SHOAL_ROCKS; k++) {
          uint32_t h = hash2(i * 31 + shoal_n, k);
          float a = rnd(h) * TAU, d = k == 0 ? 0 : 2.5f + 4.0f * rnd(h >> 16), sn, cs;
          cw_sincos(a, &sn, &cs);
          /* the tallest stand 0.3 m over the sea as made, the lowest just under it */
          rock_add(x + cs * d, z + sn * d, -0.3f + 0.6f * rnd(h * 2654435761u), 1.2f + 0.8f * rnd(h >> 9), h);
          shoal_c[shoal_n][2] = fmaxf(shoal_c[shoal_n][2], d + 2.6f);
        }
        shoal_n++, last_s = s, put = true;
      }
  }
  /* pad the shoal block so every shoal owns SHOAL_ROCKS slots */
  while (rock_n < SHOALS * SHOAL_ROCKS) rocks[rock_n++] = (rock){ 0 };
  bar_first = rock_n;
  const route_def *r = RT();
  if (r && r->type == RT_REEF) {
    float ux, uz;
    route_frame(r, &ux, &uz, NULL);
    float px = -uz, pz = ux;
    for (int sd = 0; sd < 2; sd++) {
      float d = 4;
      while (d < 70 && gnd(r->gx + px * d * (sd ? 1.0f : -1.0f), r->gz + pz * d * (sd ? 1.0f : -1.0f)) < 0.2f) d += 1;
      bar_ext[sd] = d + 3;
    }
    for (int row = 0; row < 2; row++)
      for (float t = -bar_ext[0]; t <= bar_ext[1] && bar_n < BAR_MAX * 2; t += 3.1f) {
        uint32_t h = hash2((int)(t * 10) + 7000, row);
        float x = r->gx + px * t + ux * ((float)row * 3.4f - 1.7f + (rnd(h) - 0.5f) * 1.2f), z = r->gz + pz * t + uz * ((float)row * 3.4f - 1.7f + (rnd(h) - 0.5f) * 1.2f);
        rock_add(x, z, r->low - 0.42f * rnd(h >> 7), 1.3f + 0.7f * rnd(h >> 13), h);
        bar_n++;
      }
  }
  g_wx.rocks = rock_n - SHOALS * SHOAL_ROCKS + 0;
  for (int k = 0; k < SHOALS * SHOAL_ROCKS; k++) if (rocks[k].r > 0) g_wx.rocks++;
}

/* ── what is drawn: the rocks, instanced, and a ramp route's slab ── */
static cw_node rock_node[3][4], slab_node;
static t3_instanced_mesh *rock_im[3][4];
static int rock_parts[3];
static const float rock_miny[3] = { -0.675f, -0.935f, -0.672f }, rock_h[3] = { 1.534f, 2.15f, 1.47f };
static cw_node root_node(void) { return cw_node_get(cw_tree_root(cw_scene_app_tree()), "Root"); }
static void rocks_draw_build(void) {
  static const char *const M[3] = { "res://models/rock0.glb", "res://models/rock1.glb", "res://models/rock2.glb" };
  for (int m = 0; m < 3; m++) for (int p = 0; p < rock_parts[m]; p++) if (cw_node_valid(rock_node[m][p])) cw_node_free(rock_node[m][p]);
  int count[3] = { 0, 0, 0 };
  for (int k = 0; k < rock_n; k++) if (rocks[k].r > 0) count[rocks[k].model]++;
  for (int m = 0; m < 3; m++) {
    rock_parts[m] = 0;
    if (!count[m]) continue;
    t3_object *src = cw_gltf_instantiate(cw_res_load(M[m], CW_RES_GLTF));
    t3_object *stack[16];
    int sp = 0;
    stack[sp++] = src;
    while (sp > 0) {
      t3_object *o = stack[--sp];
      if (o->type == T3_MESH && rock_parts[m] < 4) {
        t3_mesh *mesh = (t3_mesh *)o;
        t3_instanced_mesh *im = t3_instanced_mesh_new(mesh->geometry, mesh->materials[0], count[m]);
        ((t3_object *)im)->cast_shadow = true, ((t3_object *)im)->receive_shadow = true;
        int n = 0;
        for (int k = 0; k < rock_n; k++) {
          const rock *q = &rocks[k];
          if (q->r <= 0 || q->model != m) continue;
          float sy = (q->top - q->bottom) / rock_h[m];
          t3_mat4 mat;
          t3_mat4_compose(&mat, (t3_vec3){ q->x, q->bottom - rock_miny[m] * sy, q->z }, t3_quat_from_euler((t3_euler){ 0, q->yaw, 0, T3_YXZ }), (t3_vec3){ q->sxz, sy, q->sxz });
          t3_instanced_mesh_set_matrix_at(im, n++, &mat);
        }
        t3_sphere big = { { 0, 0, 0 }, 4000 };
        t3_instanced_mesh_set_bounding_sphere(im, &big);
        cw_node nd = cw_node_new("Node3D", "WeatherRocks");
        cw_node_add_child(root_node(), nd);
        cw_node3d_set_object(nd, (t3_object *)im);
        rock_node[m][rock_parts[m]] = nd, rock_im[m][rock_parts[m]] = im;
        rock_parts[m]++;
        t3_release(im);
      }
      for (int i = 0; i < o->child_count && sp < 16; i++) stack[sp++] = o->children[i];
    }
    t3_release(src);
  }
}
/* (each is beside a straight, on the side the next mark is passed on, so a jump never costs that mark) */
/* a ramp route: a slab, a barge's deck or a wreck's, from its foot at `low` up `rise` metres, its sides down to the bed */
static float slab_bottom;
static void slab_build(void) {
  if (cw_node_valid(slab_node)) cw_node_free(slab_node);
  slab_node = (cw_node){ 0 };
  const route_def *r = RT();
  if (!r || r->type != RT_RAMP) return;
  float sy, cy;
  cw_sincos(r->yaw, &sy, &cy);
  slab_bottom = 1e9f;
  for (int a = -1; a <= 1; a += 2) for (int l = -1; l <= 1; l += 2) {
    float x = r->x + sy * r->hl * (float)a - cy * r->hw * (float)l, z = r->z + cy * r->hl * (float)a + sy * r->hw * (float)l;
    slab_bottom = fminf(slab_bottom, gnd(x, z));
  }
  slab_bottom = fminf(slab_bottom, gnd(r->x, r->z)) - 1.0f;
  /* in its own frame: x across (right is -x), z along, y up */
  float hl = r->hl, hw = r->hw, y0 = r->low, y1 = r->low + r->rise, yb = slab_bottom;
  float P[8][3] = { { -hw, yb, -hl }, { hw, yb, -hl }, { hw, yb, hl }, { -hw, yb, hl }, { -hw, y0, -hl }, { hw, y0, -hl }, { hw, y1, hl }, { -hw, y1, hl } };
  static const int F[6][4] = { { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 1, 2, 6, 5 }, { 3, 0, 4, 7 }, { 3, 2, 1, 0 } };
  float pos[24 * 3], nrm[24 * 3], uv[24 * 2];
  uint32_t idx[36];
  for (int f = 0; f < 6; f++) {
    const float *a = P[F[f][0]], *b = P[F[f][1]], *c = P[F[f][2]];
    float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] }, l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-6f;
    for (int k = 0; k < 4; k++) {
      int v = f * 4 + k;
      for (int q = 0; q < 3; q++) pos[v * 3 + q] = P[F[f][k]][q], nrm[v * 3 + q] = n[q] / l;
      uv[v * 2] = (float)(k == 1 || k == 2), uv[v * 2 + 1] = (float)(k >= 2);
    }
    uint32_t o = (uint32_t)f * 4;
    idx[f * 6] = o, idx[f * 6 + 1] = o + 2, idx[f * 6 + 2] = o + 1, idx[f * 6 + 3] = o, idx[f * 6 + 4] = o + 3, idx[f * 6 + 5] = o + 2;
  }
  /* (wound so each face looks out: the cross products point outward for this vertex order) */
  t3_geometry *g = t3_geometry_new();
  { t3_attribute *a_ = t3_attribute_new(T3_FLOAT32, pos, 24, 3); t3_geometry_set_attribute(g, T3_ATTR_POSITION, a_); t3_release(a_); }
  { t3_attribute *a_ = t3_attribute_new(T3_FLOAT32, nrm, 24, 3); t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a_); t3_release(a_); }
  { t3_attribute *a_ = t3_attribute_new(T3_FLOAT32, uv, 24, 2); t3_geometry_set_attribute(g, T3_ATTR_UV, a_); t3_release(a_); }
  { t3_attribute *a_ = t3_attribute_new(T3_UINT32, idx, 36, 1); t3_geometry_set_index(g, a_); t3_release(a_); }
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = (t3_color){ r->col[0], r->col[1], r->col[2] };
  m->roughness = 0.82f, m->metalness = !strcmp(r->id, "storm") || !strcmp(r->id, "harbour") ? 0.35f : 0.0f;
  m->side = T3_DOUBLE_SIDE;
  t3_mesh *mesh = t3_mesh_new(g, m);
  t3_release(g), t3_release(m);
  ((t3_object *)mesh)->cast_shadow = true, ((t3_object *)mesh)->receive_shadow = true;
  slab_node = cw_node_new("Node3D", "RouteRamp");
  cw_node_add_child(root_node(), slab_node);
  cw_node3d_set_object(slab_node, (t3_object *)mesh);
  t3_release(mesh);
  cw_node3d_set_position(slab_node, (t3_vec3){ r->x, 0, r->z });
  cw_node3d_set_rotation(slab_node, (t3_vec3){ 0, r->yaw, 0 });
}

static float *mask0, *mask_buf;
static int mask_n;
void wx_mask_base(const float *mask, int n) {
  free(mask0), free(mask_buf);
  mask0 = malloc((size_t)n * n * sizeof(float)), mask_buf = malloc((size_t)n * n * sizeof(float));
  memcpy(mask0, mask, (size_t)n * n * sizeof(float));
  mask_n = n;
}

void wx_course_apply(void) {
  if (hts_level != G.level) {
    free(hts);
    size_t b;
    hts = cw_asset_load(DEF->heights, &b);
    if (hts && b != (size_t)(DEF->quads + 1) * (DEF->quads + 1) * 2) { free(hts); hts = NULL; }
    hts_level = G.level;
  }
  rocks_build();
  rocks_draw_build();
  slab_build();
}

/* ── the race ── */
static int phase[RACERS];     /* a route: 0 not on it, 1 on the way to its gap, 2 through, on the way out */
static bool took[RACERS];     /* through it this lap */
static float prev_air[RACERS];
static unsigned st_read;
void wx_race_reset(void) {
  memset(phase, 0, sizeof phase), memset(took, 0, sizeof took), memset(prev_air, 0, sizeof prev_air);
  if (ev.on && mask0 && cw_node_valid(G.water)) cw_water_set_wave_mask(G.water, mask0, mask_n, mask_n);
  memset(&ev, 0, sizeof ev);
  float keep_rocks = (float)g_wx.rocks;
  memset(&g_wx, 0, sizeof g_wx);
  g_wx.rocks = (int32_t)keep_rocks, g_wx.wave_min = 1e9f, g_wx.wave_max = -1e9f;
  memset(&g_stunt, 0, sizeof g_stunt);
  g_stunt.mult = 1;
  st_read = g_ev_n;
}

static bool is_route_ai(const craft *c, int idx) { return !c->human || idx == 0; }
/* along the line: where on its lap this craft is */
static float lap_s(const craft *c) { return (float)c->track_i / (float)TRACK_N * COURSE_LEN; }
bool wx_in_route(const craft *c) {
  const route_def *r = RT();
  if (!r || r->type != RT_REEF || !wx_route_open()) return false;
  /* the corridor's middle, clear of its ends where it meets the course: there nobody else is, and the marks it skips are not
   * counted as passed or missed */
  float ux, uz, len = sqrtf((r->bx - r->ax) * (r->bx - r->ax) + (r->bz - r->az) * (r->bz - r->az));
  route_frame(r, &ux, &uz, NULL);
  float a = (c->x - r->ax) * ux + (c->z - r->az) * uz, l = -(c->x - r->ax) * uz + (c->z - r->az) * ux;
  return a > 30 && a < len - 30 && fabsf(l) < 45;
}

void wx_craft(craft *c) {
  int i = (int)(c - G.c);
  /* rocks standing out of the water are rocks */
  float L = level();
  for (int s = 0; s < shoal_n; s++) {
    float dx = c->x - shoal_c[s][0], dz = c->z - shoal_c[s][1];
    if (dx * dx + dz * dz > (shoal_c[s][2] + 3) * (shoal_c[s][2] + 3)) continue;
    for (int k = 0; k < SHOAL_ROCKS; k++) {
      rock *q = &rocks[s * SHOAL_ROCKS + k];
      if (q->r <= 0 || !rock_solid(q)) continue;
      int before = c->wall_hits;
      sim_solid_circle(c, q->x, q->z, q->r + 0.8f);
      if (c->wall_hits != before) { if (c->human) g_wx.shoal_hits++; else g_wx.ai_shoal_hits++; }
    }
  }
  (void)L;
  const route_def *r = RT();
  if (!r) return;
  bool open = wx_route_open();
  if (r->type == RT_REEF) {
    float ux, uz;
    route_frame(r, &ux, &uz, NULL);
    float px = -uz, pz = ux;
    float a1 = (c->x - r->gx) * ux + (c->z - r->gz) * uz, a0 = (c->px - r->gx) * ux + (c->pz - r->gz) * uz, l = (c->x - r->gx) * px + (c->z - r->gz) * pz;
    bool across = l > -bar_ext[0] && l < bar_ext[1];
    if (!open && across && fabsf(a1) < 2.6f) {
      /* the bar: out the way it came, and it costs */
      float side = a0 < 0 ? -1.0f : 1.0f, push = side * 2.6f - a1;
      c->x += ux * push, c->z += uz * push;
      int before = c->wall_hits;
      sim_craft_hit(c, ux * side, uz * side);
      if (c->wall_hits != before) { if (c->human) g_wx.route_shut_hits++; else g_wx.ai_shut_hits++; }
    }
    if (open && across && a0 < 0 && a1 >= 0 && !took[i]) {
      took[i] = true;
      if (c->human) g_wx.route_through++; else g_wx.ai_route++;
      if (phase[i] == 1) phase[i] = 2;
      sim_shortcut(c, r->s_out);
    }
    float s = lap_s(c);
    if (took[i] && (s < r->s_in - 200 || s > r->s_out + 200)) took[i] = false;
    return;
  }
  if (r->type == RT_RAMP) {
    float sy, cy;
    cw_sincos(r->yaw, &sy, &cy);
    float dx = c->x - r->x, dz = c->z - r->z, a = dx * sy + dz * cy, l = dx * -cy + dz * sy;
    static bool on_slab[RACERS];
    if (a < -r->hl || a > r->hl || l < -r->hw || l > r->hw) { on_slab[i] = false; return; }
    float slope = r->rise / (2 * r->hl), deck = r->low + (a + r->hl) * slope;
    if (c->y > deck + 0.7f) { on_slab[i] = false; return; } /* flying over it */
    float along = c->vx * sy + c->vz * cy;
    /* open: a rough sea carries a rider onto its foot and up it; shut, its foot stands out of the water: a wall */
    bool onto = open && along > 2 && (c->y > deck - 0.6f || a < -r->hl + 6);
    if (onto) {
      if (!on_slab[i]) { on_slab[i] = true; c->ramps++; if (c->human) g_wx.route_through++; else g_wx.ai_route++; }
      c->on_ramp = true;
      c->y = deck + 0.22f, c->vy = along * slope;
      c->air = false, c->air_time = 0;
      c->pitch = cw_atan2(slope, 1.0f), c->pr = 0;
      return;
    }
    float to_end = a + r->hl, to_side = r->hw - fabsf(l);
    float nx, nz, push;
    if (to_end < to_side) nx = -sy, nz = -cy, push = to_end;
    else nx = l < 0 ? cy : -cy, nz = l < 0 ? -sy : sy, push = to_side;
    push = fminf(push * 1.02f, 0.8f);
    c->x += nx * push, c->z += nz * push;
    int before = c->wall_hits;
    sim_craft_hit(c, nx, nz);
    if (c->wall_hits != before) { if (c->human) g_wx.route_shut_hits++; else g_wx.ai_shut_hits++; }
  }
}

/* steer round a circle in the way to (ax, az) */
static void avoid(const craft *c, float cx, float cz, float rad, float *ax, float *az) {
  float dx = *ax - c->x, dz = *az - c->z, l2 = dx * dx + dz * dz + 1e-3f, l = sqrtf(l2);
  float ox = cx - c->x, oz = cz - c->z, ahead = (ox * dx + oz * dz) / l;
  if (ahead < -rad || ahead > 80 + rad) return;
  float t = clampf((ox * dx + oz * dz) / l2, 0, 1), ex = c->x + dx * t - cx, ez = c->z + dz * t - cz, e = sqrtf(ex * ex + ez * ez);
  if (e > rad + 5) return;
  float nx, nz;
  if (e > 0.5f) nx = ex / e, nz = ez / e;
  else nx = -dz / l, nz = dx / l;
  *ax = cx + nx * (rad + 9), *az = cz + nz * (rad + 9);
}
void wx_ai(craft *c, int idx, float *ax, float *az) {
  if (g_try_ramp >= 0 && c->human) return; /* the ramp-reach check steers the seat at one ramp: leave it be */
  const route_def *r = RT();
  bool open = wx_route_open();
  float s = lap_s(c);
  if (r && r->type == RT_REEF) {
    float ux, uz;
    route_frame(r, &ux, &uz, NULL);
    /* (once the mark at its mouth is passed: the way in never costs that mark) */
    if (phase[idx] == 0 && open && is_route_ai(c, idx) && !took[idx] && s > r->s_in - 40 && s < r->s_in + 60 && BUOYS[c->next_buoy][3] > r->s_in + 1) {
      float dx = c->x - r->ax, dz = c->z - r->az;
      if (dx * dx + dz * dz < 90 * 90) phase[idx] = 1;
    }
    if (phase[idx] == 1) {
      if (!open) phase[idx] = 0;
      else { *ax = r->gx + ux * 25, *az = r->gz + uz * 25; return; }
    }
    if (phase[idx] == 2) {
      float dx = c->x - r->bx, dz = c->z - r->bz;
      if (dx * dx + dz * dz < 30 * 30 || !open) phase[idx] = 0;
      else { *ax = r->bx + ux * 20, *az = r->bz + uz * 20; return; }
    }
  }
  if (r && r->type == RT_ARCH && DEF->hook.kind == 1) {
    float hx = DEF->hook.x, hz = DEF->hook.z, ux = DEF->hook.dirx, uz = DEF->hook.dirz, hl = DEF->hook.half_len;
    if (phase[idx] == 0 && hook_open() && is_route_ai(c, idx) && s > DEF->hook.s_in - 40 && s < DEF->hook.s_in + 50 && BUOYS[c->next_buoy][3] > DEF->hook.s_in - 30) phase[idx] = 1;
    float a = (c->x - hx) * ux + (c->z - hz) * uz;
    if (phase[idx] == 1) {
      if (!hook_open() && a < -2.4f) phase[idx] = 0;
      else if (a > hl - 10 || s > DEF->hook.s_out + 50) phase[idx] = 0;
      else {
        /* onto its line first, from well back, then along it */
        float at = a < -hl ? -hl : a + 30;
        *ax = hx + ux * at, *az = hz + uz * at;
        return;
      }
    }
  }
  if (r && r->type == RT_RAMP) {
    float sy, cy;
    cw_sincos(r->yaw, &sy, &cy);
    float dx = c->x - r->x, dz = c->z - r->z, al = dx * sy + dz * cy, la = dx * -cy + dz * sy;
    bool lined = la > -r->hw + 1.5f && la < r->hw - 1.5f;
    /* lined up from well back, as for the course's ramps; one not lined up by 20 m short of its foot rounds it instead */
    bool takes = open && (idx & 1) && al > -110 && al < r->hl && fabsf(la) < 45 && (lined || al < -r->hl - 20);
    if (takes) {
      float at = al < -r->hl - 8 && !lined ? -r->hl - 12 : al + 26;
      *ax = r->x + sy * at, *az = r->z + cy * at;
    } else avoid(c, r->x, r->z, r->hl + 2, ax, az);
  }
  for (int k = 0; k < shoal_n; k++) if (shoal_solid(k)) avoid(c, shoal_c[k][0], shoal_c[k][1], shoal_c[k][2] + 1, ax, az);
}

/* ── the big water event ── */
typedef struct event_def { const char *id; float trig, speed, sigma; const char *line; } event_def;
static const event_def EVENTS[] = {
  { "atoll", 1.3f, 11, 55, "A cliff has fallen into the sea: big waves coming" },
  { "sound", 1.3f, 11, 60, "Rock has broken off the fjord wall: big waves coming" },
  { "canyon", 1.3f, 10, 45, "A flash flood is coming down the river" },
  { "harbour", 1.3f, 11, 50, "A ship's wake is rolling in: big waves coming" },
  { "storm", 1.3f, 12, 60, "A rogue set is coming through" },
};
static const event_def *EV(void) {
  for (unsigned i = 0; i < sizeof EVENTS / sizeof EVENTS[0]; i++) if (!strcmp(EVENTS[i].id, DEF->id)) return &EVENTS[i];
  return &EVENTS[0];
}
const char *wx_event_line(void) { return EV()->line; }
static float bump(float u) { u *= u; return u < 1 ? (1 - u) * (1 - u) : 0; }
float wx_event_at(float x, float z) {
  if (!ev.on) return 0;
  float a = x * ev.dx + z * ev.dz, l = -x * ev.dz + z * ev.dx;
  return ev.r * bump((a - ev.c) / EV()->sigma) * bump((l - ev.l0) / 900.0f);
}
static void event_start(void) {
  const craft *lead = &G.c[G.order[0]];
  float s, c;
  cw_sincos(DEF->waves[0][0] * 0.01745329f, &s, &c);
  ev.dx = c, ev.dz = s;
  float a = lead->x * ev.dx + lead->z * ev.dz;
  ev.l0 = -lead->x * ev.dz + lead->z * ev.dx;
  ev.c = a - 300;
  float amax = -1e9f;
  for (int i = 0; i < TRACK_N; i++) amax = fmaxf(amax, TRACK[i][0] * ev.dx + TRACK[i][1] * ev.dz);
  ev.end_c = amax + 150;
  /* the set stands g_event_amp over the swell the course has now */
  cw_waves w;
  float sw = 0.1f;
  if (cw_water_get_waves(G.water, &w)) sw = fmaxf(0.1f, w.wave[0].amplitude + w.wave[1].amplitude);
  ev.P = g_event_amp / sw;
  ev.on = 1, ev.r = 0, ev.tick0 = (int32_t)G.tick;
  g_wx.event_on = 1, g_wx.event_tick = (int32_t)G.tick;
  ev_push(EV_WAVES, G.level, lead->x - ev.dx * 300, lead->z - ev.dz * 300);
}
static void event_mask(void) {
  if (!mask0 || !mask_buf) return;
  float size = 1792.0f, sig = EV()->sigma;
  for (int j = 0; j < mask_n; j++) {
    float z = ((float)j + 0.5f) / (float)mask_n * size - size * 0.5f;
    for (int i = 0; i < mask_n; i++) {
      float x = ((float)i + 0.5f) / (float)mask_n * size - size * 0.5f, a = x * ev.dx + z * ev.dz, e = 0;
      if (a > ev.c - sig && a < ev.c + sig) e = ev.r * bump((a - ev.c) / sig) * bump((-x * ev.dz + z * ev.dx - ev.l0) / 900.0f);
      mask_buf[j * mask_n + i] = mask0[j * mask_n + i] + ev.P * e;
    }
  }
  cw_water_set_wave_mask(G.water, mask_buf, mask_n, mask_n);
}
static void event_tick(float dt) {
  if (!ev.on) {
    if (!g_event_on || ev.done || G.state != RACE_ON || g_swell_hold > 0 || g_try_ramp >= 0) return;
    float lead = -1e9f;
    for (int i = 0; i < RACERS; i++) if (racer_on(i)) lead = fmaxf(lead, G.c[i].dist);
    bool alone = g_mode == MODE_STUNT || g_mode == MODE_ROAM;
    if ((alone && G.clock >= 40) || (!alone && lead >= EV()->trig * COURSE_LEN)) event_start();
    return;
  }
  ev.c += EV()->speed * dt;
  float since = (float)((int32_t)G.tick - ev.tick0) / 60.0f, fade_in = clampf(since / 6.0f, 0, 1), fade_out = clampf((ev.end_c - ev.c) / 150.0f, 0, 1);
  ev.r = fade_in * fade_in * (3 - 2 * fade_in) * fade_out;
  g_wx.event_c = ev.c;
  if ((G.tick & 1u) == 0) event_mask();
  for (int i = 0; i < RACERS; i++) {
    if (!racer_on(i)) continue;
    const craft *c = &G.c[i];
    float e = wx_event_at(c->x, c->z);
    if (e > 0.5f && !(g_wx.event_hits & (1 << i))) g_wx.event_hits |= 1 << i;
    if (e > 0.3f) g_wx.event_surf[i] = fmaxf(g_wx.event_surf[i], c->surf - G.tide);
  }
  /* the pad feels the set under the hull: a long low rumble, never the camera */
  { const craft *h = &G.c[0];
    float e = wx_event_at(h->x, h->z);
    if (e > 0.2f && G.tick % 15u == 0 && g_ev_on) cw_input_rumble(0, 0.2f + 0.5f * e, 0.1f + 0.25f * e, 280); }
  if (ev.c >= ev.end_c) {
    ev.on = 0, ev.done = g_mode == MODE_ROAM ? 0 : 1;
    if (g_mode == MODE_ROAM) ev.done = 0;
    if (mask0) cw_water_set_wave_mask(G.water, mask0, mask_n, mask_n);
    g_wx.event_on = 0;
  }
}

/* ── stunt mode: tricks scored, chained into combos ── */
static int trick_points(int a) {
  /* sit-down: 1 barrel roll, 2 back flip, 3 front flip, 4 kick out, 5 a hand off the bar; stand-up: 6 submarine, 7 flat spin,
   * 8 superman, 9 pole stand (sim.c) */
  switch (a) { case 1: return 300; case 2: case 3: return 500; case 4: return 150; case 5: return 80; case 6: return 400; case 7: return 450; case 8: return 250; case 9: return 350; default: return 250; }
}
static void stunt_add(int pts) {
  g_stunt.chain++, g_stunt.tricks++;
  g_stunt.pending += pts;
  g_stunt.mult = fminf(4.0f, 1.0f + 0.5f * (float)(g_stunt.chain - 1));
  g_stunt.window = 3.0f;
  if (g_stunt.chain > g_stunt.best_chain) g_stunt.best_chain = g_stunt.chain;
}
static void stunt_bank(void) {
  if (g_stunt.pending <= 0) return;
  int pts = (int)((float)g_stunt.pending * g_stunt.mult + 0.5f);
  g_stunt.score += pts, g_stunt.banks++;
  if (g_stunt.chain >= 2) g_stunt.combos++;
  g_stunt.last_bank = pts, g_stunt.bank_t = 1.6f;
  ev_push(EV_COMBO, pts, G.c[0].x, G.c[0].z);
  g_stunt.pending = 0, g_stunt.chain = 0, g_stunt.mult = 1;
}
static void stunt_tick(float dt) {
  if (g_mode != MODE_STUNT || G.state != RACE_ON) { st_read = g_ev_n; return; }
  const craft *h = &G.c[0];
  if (g_ev_n - st_read > EV_RING) st_read = g_ev_n - EV_RING;
  for (; st_read != g_ev_n; st_read++) {
    const game_event *e = &g_ev[st_read % EV_RING];
    if (e->kind == EV_TRICK) stunt_add(trick_points(e->a));
    else if (e->kind == EV_LAND && e->a == 1 && g_stunt.chain > 0) g_stunt.pending += 100, g_stunt.window = 3.0f;
    else if (e->kind == EV_LAND && e->a == 2) stunt_add(350);
    else if (e->kind == EV_CRASH && g_stunt.pending > 0) { g_stunt.lost++, g_stunt.pending = 0, g_stunt.chain = 0, g_stunt.mult = 1, g_stunt.lost_t = 1.6f; }
  }
  /* a long flight pays for its air */
  if (prev_air[0] > 1.0f && !h->air && h->spill <= 0) g_stunt.pending += (int)(prev_air[0] * 120.0f), g_stunt.window = fmaxf(g_stunt.window, 1.5f);
  prev_air[0] = h->air ? h->air_time : 0;
  /* the combo's clock runs on the water and over the hops the swell throws; a real flight holds it, the next trick may come */
  if ((!h->air || h->air_time < 0.35f) && g_stunt.pending > 0) { g_stunt.window -= dt; if (g_stunt.window <= 0) stunt_bank(); }
  if (g_stunt.lost_t > 0) g_stunt.lost_t -= dt;
  if (g_stunt.bank_t > 0) g_stunt.bank_t -= dt;
  if (G.clock >= STUNT_TIME) { stunt_bank(); G.state = RACE_DONE; G.c[0].finish = G.clock; }
}

void wx_tick(float dt) {
  event_tick(dt);
  stunt_tick(dt);
  if (G.state == RACE_ON) {
    const craft *h = &G.c[0];
    float d = h->surf - G.tide;
    g_wx.wave_max = fmaxf(g_wx.wave_max, d), g_wx.wave_min = fminf(g_wx.wave_min, d);
  }
  { cw_waves w; if (cw_water_get_waves(G.water, &w)) { float a = 0; for (int i = 0; i < w.count; i++) a += w.wave[i].amplitude * w.wave[i].amplitude; g_wx.sea_hs = 4.0f * sqrtf(a * 0.5f); } }
  int solid = 0;
  for (int k = 0; k < rock_n; k++) if (rocks[k].r > 0 && rock_solid(&rocks[k])) solid++;
  g_wx.rocks_solid = solid;
  const route_def *r = RT();
  g_wx.route[0] = r ? (float)r->type : 0, g_wx.route[1] = wx_route_open() ? 1.0f : 0.0f, g_wx.route[2] = (float)wx_now(), g_wx.route[3] = G.tide;
}

/* ── the look ── */
float g_wx_dome[3] = { 1, 1, 1 }, g_wx_sun = 1;
int g_wx_look_key = -1;
void view_props_reset(void);
static void set_f(cw_node n, const char *p, float v) { cw_node_set(n, p, cw_val_float(v)); }
static float get_f(cw_node n, const char *p, float d) { cw_value v; return cw_node_get_prop(n, p, &v) ? (float)v.f : d; }
static void scale_f(cw_node n, const char *p, float k, float lo, float hi) { set_f(n, p, clampf(get_f(n, p, 0) * k, lo, hi)); }
static void tint_c(cw_node n, const char *p, float k, float grey, float mix) {
  cw_value v;
  if (!cw_node_get_prop(n, p, &v)) return;
  float r = v.v[0], g = v.v[1], b = v.v[2], lum = (r + g + b) / 3;
  r = (r + (lum * grey - r) * mix) * k, g = (g + (lum * grey - g) * mix) * k, b = (b + (lum * grey * 1.08f - b) * mix) * k;
  cw_node_set(n, p, cw_val_color(r, g, b, 1));
}
void wx_look_tick(void) {

  static float d_rough = -1, d_reflect, d_spread;
  int now = G.level * 16 + wx_now();
  if (now == g_wx_look_key || !cw_node_valid(G.water)) return;
  g_wx_look_key = now;
  view_props_reset(); /* the course as made, then the day on top of it */
  cw_node root = root_node(), water = G.water, sky = G.sky, fill = cw_node_get(root, "Fill");
  if (d_rough < 0) d_rough = get_f(water, "roughness", 0.1f), d_reflect = get_f(water, "swell_reflect", 0.1f), d_spread = get_f(water, "foam_spread", 0.55f);
  set_f(water, "roughness", d_rough), set_f(water, "swell_reflect", d_reflect), set_f(water, "foam_spread", d_spread);
  set_f(water, "rain", 0);
  int s = wx_now() - wx_usual(G.level); /* steps toward the storm (negative: calmer than the course as made) */
  float ps = (float)s;
  g_wx_dome[0] = g_wx_dome[1] = g_wx_dome[2] = 1, g_wx_sun = 1;
  if (s != 0) {
    scale_f(water, "detail", powf(1.75f, ps), 0.08f, 1.6f);
    scale_f(water, "normal_scale", powf(1.25f, ps), 0.1f, 0.8f);
    scale_f(water, "distortion", powf(1.9f, ps), 0.02f, 0.4f);
    scale_f(water, "crest_light", powf(0.65f, ps), 0, 1);
    scale_f(water, "glint", powf(0.42f, ps), 0.1f, 2.4f);
    scale_f(water, "scatter", powf(0.6f, ps), 0, 1.6f);
    scale_f(water, "murk", powf(1.2f, ps), 0.5f, 2.2f);
    scale_f(water, "flow_speed", powf(1.7f, ps), 0.01f, 0.3f);
    scale_f(sky, "sky_energy", powf(0.62f, ps), 0.3f, 4.0f);
    scale_f(sky, "sun_energy", powf(0.45f, ps), 0.2f, 6.0f);
    scale_f(fill, "energy", powf(0.82f, ps), 0.6f, 4.0f);
  }
  if (s < 0) {
    /* calm: glass. The swell's own tilt moves the reflection, so a still sea mirrors the land and sky; crests carry no foam */
    scale_f(water, "crest_foam", 0.3f, 0, 1);
    scale_f(water, "detail", 0.45f, 0.06f, 1.6f);
    scale_f(water, "distortion", 0.5f, 0.01f, 0.4f);
    set_f(water, "swell_reflect", 0.35f);
    scale_f(water, "glint", 0.6f, 0.1f, 2.0f); /* the sun's sparkle on glassy water: bright, but never a field of flashes */
    set_f(water, "roughness", d_rough * 0.5f);
    tint_c(water, "shallow_color", 1.08f, 1, 0);
    float k = s == -1 ? 1.12f : 1.45f; /* the storm course's own sky painted dark: lifted for its calm */
    g_wx_dome[0] = g_wx_dome[1] = k, g_wx_dome[2] = k * 0.98f;
  }
  if (s > 0) {
    /* storm: dark, heavy water under a low sky, foam streaked by the wind, rain on the surface */
    set_f(water, "crest_foam", 1);
    set_f(water, "swell_reflect", 0.04f);
    set_f(water, "roughness", fminf(1, d_rough * 1.8f + 0.05f));
    set_f(water, "foam_spread", d_spread * 1.8f + 0.3f);
    tint_c(water, "shallow_color", 0.55f, 0.9f, 0.55f);
    tint_c(water, "deep_color", 0.6f, 0.9f, 0.45f);
    tint_c(water, "color", 0.9f, 1, 0.3f);
    tint_c(fill, "color", 1.0f, 1, 0.6f);
    g_wx_dome[0] = 0.2f, g_wx_dome[1] = 0.215f, g_wx_dome[2] = 0.24f;
    g_wx_sun = 0;
    scale_f(sky, "sun_energy", 0.5f, 0.2f, 6.0f);
    scale_f(water, "glint", 0.5f, 0.05f, 2.4f);
  }
  if (wx_rain()) set_f(water, "rain", 3500), set_f(water, "rain_size", 0.07f), set_f(water, "rain_strength", 0.012f);
  if (s < 0 && !strcmp(DEF->id, "storm")) g_wx_sun = 0.6f; /* after the gale: a sun to be seen */
  if (s == 0) g_wx_sun = 1;
  { float wxv, wzv; wx_wind(&wxv, &wzv); float l = sqrtf(wxv * wxv + wzv * wzv) + 1e-3f; cw_node_set(water, "flow_direction", (cw_value){ .type = CW_T_VEC2, .v = { wxv / l, wzv / l } }); }
}

/* ── debug: the weather gates ── */
static void cmd_weather(int argc, const char **argv, cw_reply *r) {
  g_wx_force = argc > 1 ? atoi(argv[1]) : -1;
  if (g_wx_force >= WX_N) g_wx_force = WX_N - 1;
  if (r) cw_reply_printf(r, "{\"weather\":\"%s\"}", WX_NAMES[wx_now()]);
}
static void cmd_event(int argc, const char **argv, cw_reply *r) {
  (void)argc, (void)argv;
  if (!ev.on) event_start();
  if (r) cw_reply_raw(r, "true");
}
/* wx_route [back]: the human lined up with this course's route at speed, the race running */
static void cmd_route(int argc, const char **argv, cw_reply *rp) {
  const route_def *r = RT();
  menu_off();
  G.state = RACE_ON;
  if (G.clock < 1) G.clock = 1;
  float back = argc > 1 ? (float)atof(argv[1]) : 60;
  craft *c = &G.c[0];
  float x, z, yaw;
  if (r && r->type == RT_REEF) { float ux, uz, lag; route_frame(r, &ux, &uz, &lag); x = r->gx - ux * back, z = r->gz - uz * back, yaw = cw_atan2(ux, uz); }
  else if (r && r->type == RT_RAMP) { float sy, cy; cw_sincos(r->yaw, &sy, &cy); x = r->x - sy * (r->hl + back), z = r->z - cy * (r->hl + back), yaw = r->yaw; }
  else { sim_hook_start(back); if (rp) cw_reply_raw(rp, "true"); return; }
  float sy, cy;
  cw_sincos(yaw, &sy, &cy);
  c->x = c->px = x, c->z = c->pz = z, c->y = c->py = cw_water_height_at(G.water, x, z, cw_water_time(G.water));
  c->yaw = c->pyaw = yaw, c->pitch = c->roll = c->pr = c->rr = 0;
  c->vx = sy * 24, c->vz = cy * 24, c->vy = 0, c->spill = 0, c->air = false;
  c->lap = 0;
  if (r && r->type == RT_REEF) { c->track_i = (int)(r->s_in / COURSE_LEN * (float)TRACK_N) % TRACK_N; for (int b = 0; b < BUOY_N; b++) if (buoy_on(b) && BUOYS[b][3] > r->s_in) { c->next_buoy = b; break; } }
  G.cam[0] = 0, G.look[0] = 0;
  if (rp) cw_reply_printf(rp, "{\"x\":%.1f,\"z\":%.1f,\"open\":%d}", x, z, wx_route_open() ? 1 : 0);
}
static void cmd_wxsw(int argc, const char **argv, cw_reply *r) {
  if (argc > 2) {
    float v = (float)atof(argv[2]);
    if (!strcmp(argv[1], "route_force")) g_route_force = (int)v;
    else if (!strcmp(argv[1], "event")) g_event_on = (int)v;
    else if (!strcmp(argv[1], "event_amp")) g_event_amp = v;
    else if (!strcmp(argv[1], "nag")) g_nag_on = (int)v;
    else if (!strcmp(argv[1], "turbo_risk")) g_turbo_risk = (int)v;
    else if (!strcmp(argv[1], "spill_old")) g_spill_old = (int)v;
    else if (!strcmp(argv[1], "cup_day")) g_cup_day = (int)v;
  }
  if (r) cw_reply_raw(r, "true");
}
/* the course's rocks, against the ground and the sea: the grounding gate's part (lift: its control) */
static void cmd_wx_ground(int argc, const char **argv, cw_reply *r) {
  float lift = argc > 1 ? (float)atof(argv[1]) : 0, worst = -1e9f;
  int gaps = 0, n = 0;
  for (int k = 0; k < rock_n; k++) {
    const rock *q = &rocks[k];
    if (q->r <= 0) continue;
    float g = 1e9f;
    for (int a = 0; a < 8; a++) { float s, c; cw_sincos((float)a * 0.785398f, &s, &c); g = fminf(g, ground_at(q->x + c * q->r, q->z + s * q->r)); }
    float gap = q->bottom + lift - g;
    worst = fmaxf(worst, gap), n++;
    if (gap > 0.15f) gaps++;
  }
  const route_def *rt = RT();
  float slab_gap = -1e9f;
  if (rt && rt->type == RT_RAMP) {
    float sy, cy;
    cw_sincos(rt->yaw, &sy, &cy);
    for (int a = -1; a <= 1; a++) for (int l = -1; l <= 1; l++) slab_gap = fmaxf(slab_gap, slab_bottom + lift - ground_at(rt->x + sy * rt->hl * (float)a - cy * rt->hw * (float)l, rt->z + cy * rt->hl * (float)a + sy * rt->hw * (float)l));
    if (slab_gap > 0.15f) gaps++;
  }
  if (r) cw_reply_printf(r, "{\"rocks\":%d,\"shoals\":%d,\"gaps\":%d,\"worst\":%.3f,\"slab_gap\":%.3f}", n, shoal_n, gaps, worst, slab_gap);
}
void wx_debug(void) {
  cw_debug_command("weather", "weather <0 calm, 1 choppy, 2 storm, -1 the course's usual>", cmd_weather);
  cw_debug_command("event", "event: the course's big waves now", cmd_event);
  cw_debug_command("wx_route", "wx_route [back]: lined up with the course's weather route", cmd_route);
  cw_debug_command("wxsw", "wxsw <route_force|event|event_amp|nag|turbo_risk|spill_old|cup_day> <v>", cmd_wxsw);
  cw_debug_command("wx_ground", "wx_ground [lift]: the weather's rocks and slab against the ground", cmd_wx_ground);
  cw_debug_f32("wx.wave_max", &g_wx.wave_max);
  cw_debug_f32("wx.wave_min", &g_wx.wave_min);
  cw_debug_f32("wx.sea_hs", &g_wx.sea_hs);
  cw_debug_i32("wx.route_through", &g_wx.route_through);
  cw_debug_i32("wx.route_shut_hits", &g_wx.route_shut_hits);
  cw_debug_i32("wx.ai_route", &g_wx.ai_route);
  cw_debug_i32("wx.ai_shut_hits", &g_wx.ai_shut_hits);
  cw_debug_i32("wx.shoal_hits", &g_wx.shoal_hits);
  cw_debug_i32("wx.ai_shoal_hits", &g_wx.ai_shoal_hits);
  cw_debug_i32("wx.rocks", &g_wx.rocks);
  cw_debug_i32("wx.rocks_solid", &g_wx.rocks_solid);
  cw_debug_i32("wx.event_on", &g_wx.event_on);
  cw_debug_i32("wx.event_hits", &g_wx.event_hits);
  cw_debug_i32("wx.event_tick", &g_wx.event_tick);
  cw_debug_f32("wx.event_c", &g_wx.event_c);
  cw_debug_f32_array("wx.event_surf", g_wx.event_surf, RACERS);
  cw_debug_f32_array("wx.route", g_wx.route, 4);
  cw_debug_field("wx.phase", phase, CW_DBG_I32, RACERS);
  cw_debug_i32("stunt.score", &g_stunt.score);
  cw_debug_i32("stunt.pending", &g_stunt.pending);
  cw_debug_i32("stunt.chain", &g_stunt.chain);
  cw_debug_i32("stunt.best_chain", &g_stunt.best_chain);
  cw_debug_i32("stunt.combos", &g_stunt.combos);
  cw_debug_i32("stunt.tricks", &g_stunt.tricks);
  cw_debug_i32("stunt.banks", &g_stunt.banks);
  cw_debug_i32("stunt.lost", &g_stunt.lost);
  cw_debug_i32("wx.cup_day", &g_cup_day);
}

/* the minimap's part: the event's band where it is now, and the route (green open, red shut) */
#include "cartwheel/render2d.h"
void wx_minimap_draw(float rx, float ry, float rw, float rh, float minx, float minz, float sc, float ox, float oy, float u) {
#define MP(px, pz) cw_v2(ox + ((px) - minx) * sc, oy + ((pz) - minz) * sc)
  const route_def *r = RT();
  cw_color col = wx_route_open() ? cw_rgba(0.45f, 1, 0.7f, 0.85f) : cw_rgba(1, 0.35f, 0.2f, 0.6f);
  if (r && r->type == RT_REEF) { cw_vec2 p[3] = { MP(r->ax, r->az), MP(r->gx, r->gz), MP(r->bx, r->bz) }; cw_r2d_polyline(p, 3, 2.4f * u, false, col); }
  else if (r && r->type == RT_RAMP) { float sy, cy; cw_sincos(r->yaw, &sy, &cy); cw_vec2 p[2] = { MP(r->x - sy * r->hl, r->z - cy * r->hl), MP(r->x + sy * r->hl, r->z + cy * r->hl) }; cw_r2d_polyline(p, 2, 4 * u, false, col); }
  else if (r && r->type == RT_ARCH) { float hx = DEF->hook.x, hz = DEF->hook.z, ux = DEF->hook.dirx, uz = DEF->hook.dirz, hl = DEF->hook.half_len; cw_vec2 p[2] = { MP(hx - ux * hl, hz - uz * hl), MP(hx + ux * hl, hz + uz * hl) }; cw_r2d_polyline(p, 2, 2.4f * u, false, col); }
  if (ev.on) {
    /* across the map, perpendicular to its travel, at its middle */
    float cx = ev.dx * ev.c - ev.dz * ev.l0, cz = ev.dz * ev.c + ev.dx * ev.l0, px = -ev.dz, pz = ev.dx;
    cw_vec2 a = MP(cx - px * 900, cz - pz * 900), b = MP(cx + px * 900, cz + pz * 900);
    /* clipped to the map's square */
    cw_vec2 p[2] = { a, b };
    float t0 = 0, t1 = 1, dx = b.x - a.x, dy = b.y - a.y, P4[4] = { -dx, dx, -dy, dy }, Q4[4] = { a.x - rx, rx + rw - a.x, a.y - ry, ry + rh - a.y };
    bool in = true;
    for (int k = 0; k < 4 && in; k++) {
      if (fabsf(P4[k]) < 1e-6f) { if (Q4[k] < 0) in = false; continue; }
      float t = Q4[k] / P4[k];
      if (P4[k] < 0) t0 = fmaxf(t0, t); else t1 = fminf(t1, t);
    }
    if (in && t0 < t1) {
      p[0] = cw_v2(a.x + dx * t0, a.y + dy * t0), p[1] = cw_v2(a.x + dx * t1, a.y + dy * t1);
      cw_r2d_polyline(p, 2, 5 * u * ev.r + 1, false, cw_rgba(0.75f, 0.92f, 1, 0.7f * ev.r));
    }
  }
#undef MP
}
