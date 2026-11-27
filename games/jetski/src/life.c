/* Life in the water and over it: schools of fish under the clear water beside the line, which scatter from a craft
 * that comes close and gather again behind it, and flocks of gulls on the beaches near the line, which lift off as a
 * craft passes, wheel overhead and come back down. All of it is seen, none of it is in the way: nothing here touches
 * the race, the sim or its hash. Each school or flock is one instanced draw, made from our own code (no asset).
 * sw life 0: none, the life gate's control. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/render3d.h"
#include "game.h"

#define SCHOOLS 16
#define FISH 18
#define FLOCKS 6
#define GULLS 7
#define SCATTER_R 13.0f /* a craft this near a school's middle scatters it */
#define NEAR_R 20.0f    /* a school counted as passed close */
#define LIFT_R 38.0f    /* a craft this near a flock lifts it */

int g_life_on = 1;
int g_fish_old;
int g_fish_hide; /* sw fish_hide 1: the schools not drawn (the visibility gate's picture pairs) */
float g_fish_lift; /* sw fish_lift <m>: the schools raised by that much, for a look at the fish themselves */
/* schools placed, flocks placed, scatters, lift-offs, schools passed within 20 m by the player, the deepest bed under a
   school, fish clear of the water in the last frame drawn */
float g_life[7];

typedef struct school { float x, z, bed, r, phase, scatter, ax, az, s, st; bool near, by; } school; /* st: seconds since it was startled (-1: calm) */ /* by: the player within reach of it now */
typedef struct flock { float x, z, y, dir, t, s; int state; bool armed, afloat; } flock; /* afloat: resting on the sea where no beach is near */ /* state 0 resting, 1 lifting and wheeling, 2 coming down */
static school S[SCHOOLS];
static flock F[FLOCKS];
static int school_n, flock_n;
static float fish_k[SCHOOLS][FISH][4]; /* each fish: its angle on the ring, radius, height, size */
static float gull_k[FLOCKS][GULLS][4]; /* each gull: its spot round the flock's middle (x, z), its turn, its flap phase */
static bool fish_up[SCHOOLS][FISH]; /* clear of the water last frame: a leap leaving or entering it splashes */
static t3_instanced_mesh *fish_im, *gull_im, *wing_im;
static cw_node life_n;
static float prev_x, prev_z;
static bool have_prev;

static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
  uint32_t h = a * 374761393u + b * 668265263u + c * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float turn_to(float a, float b, float t) { float d = b - a; while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f; return a + d * t; }
static float h01(uint32_t a, uint32_t b, uint32_t c) { return (float)(hash3(a, b, c) >> 8) / 16777216.0f; }

/* a surface from rows of rings: (rows + 1) x (around + 1) points, normals from the faces */
static t3_geometry *rings_geo(int rows, int around, void (*fn)(int r, int a, float *p)) {
  int nv = (rows + 1) * (around + 1);
  float *pos = malloc((size_t)nv * 12);
  uint32_t *idx = malloc((size_t)rows * (size_t)around * 6 * 4);
  for (int r = 0; r <= rows; r++)
    for (int a = 0; a <= around; a++) fn(r, a % around, pos + (r * (around + 1) + a) * 3);
  int w = 0;
  for (int r = 0; r < rows; r++)
    for (int a = 0; a < around; a++) {
      uint32_t p = (uint32_t)(r * (around + 1) + a), q = p + 1, s = p + (uint32_t)around + 1, t = s + 1;
      idx[w++] = p, idx[w++] = q, idx[w++] = s;
      idx[w++] = q, idx[w++] = t, idx[w++] = s;
    }
  t3_geometry *g = t3_geometry_new();
  { t3_attribute *a_ = t3_attribute_new(T3_FLOAT32, pos, nv, 3); t3_geometry_set_attribute(g, T3_ATTR_POSITION, a_); t3_release(a_); }
  { t3_attribute *a_ = t3_attribute_new(T3_UINT32, idx, w, 1); t3_geometry_set_index(g, a_); t3_release(a_); }
  t3_geometry_compute_vertex_normals(g);
  free(pos), free(idx);
  return g;
}
/* a fish 1 m long along +z: a body that swells behind the head and pinches to a stalk, then a tall flat tail */
#define FISH_ROWS 22
#define FISH_AROUND 10
static void fish_pt(int r, int a, float *p) {
  float t = (float)r / FISH_ROWS, ang = (float)a / FISH_AROUND * 6.2831853f, rx, ry;
  if (t < 0.24f) { float u = (0.24f - t) / 0.24f; rx = 0.012f; ry = 0.02f + 0.17f * powf(u, 1.2f); }
  else { float s = (t - 0.24f) / 0.76f, b = 1.9f * powf(s, 0.55f) * powf(fmaxf(0.0f, 1.0f - s), 0.7f) * 0.15f; rx = 0.6f * b + 0.012f; ry = b + 0.02f; }
  p[0] = cosf(ang) * rx, p[1] = sinf(ang) * ry, p[2] = t - 0.5f;
}
/* a gull 1 m long along +z: a round body, a head up front */
#define GULL_ROWS 16
#define GULL_AROUND 10
static void gull_pt(int r, int a, float *p) {
  float t = (float)r / GULL_ROWS, ang = (float)a / GULL_AROUND * 6.2831853f;
  float body = 0.16f * powf(sinf(3.14159f * fminf(1.0f, t / 0.82f)), 0.7f), head = 0.1f * expf(-powf((t - 0.86f) / 0.07f, 2.0f));
  float rr = fmaxf(body, head) + 0.004f, lift = 0.09f * fmaxf(0.0f, t - 0.6f) / 0.4f;
  if (t > 0.95f) rr *= (1.0f - t) / 0.05f * 0.6f + 0.004f; /* the beak, a point */
  p[0] = cosf(ang) * rr * 0.85f, p[1] = sinf(ang) * rr + lift, p[2] = t - 0.5f;
}
/* the wings: a swept pair in a shallow V (scaled through zero on y they beat), with a top and an underside */
#define WING_SPAN 12
static t3_geometry *wing_geo(void) {
  int nv = (WING_SPAN * 2 + 1) * 2 * 2;
  float *pos = malloc((size_t)nv * 12);
  uint32_t *idx = malloc((size_t)(WING_SPAN * 2) * 12 * 4);
  int k = 0, w = 0;
  for (int side = 0; side < 2; side++) {
    float off = side ? -0.008f : 0.008f;
    int base = k;
    for (int i = 0; i <= WING_SPAN * 2; i++) {
      float x = (float)i / WING_SPAN - 1.0f, ax = fabsf(x), chord = 0.3f - 0.18f * ax, sweep = -0.16f * ax * ax, y = 0.32f * ax + off;
      float lead = 0.12f + sweep, trail = lead - chord;
      pos[k * 3] = x, pos[k * 3 + 1] = y, pos[k * 3 + 2] = lead, k++;
      pos[k * 3] = x, pos[k * 3 + 1] = y, pos[k * 3 + 2] = trail, k++;
    }
    for (int i = 0; i < WING_SPAN * 2; i++) {
      uint32_t a = (uint32_t)(base + i * 2), b = a + 1, c = a + 2, d = a + 3;
      if (!side) idx[w++] = a, idx[w++] = c, idx[w++] = b, idx[w++] = b, idx[w++] = c, idx[w++] = d;
      else idx[w++] = a, idx[w++] = b, idx[w++] = c, idx[w++] = b, idx[w++] = d, idx[w++] = c;
    }
  }
  t3_geometry *g = t3_geometry_new();
  { t3_attribute *a_ = t3_attribute_new(T3_FLOAT32, pos, k, 3); t3_geometry_set_attribute(g, T3_ATTR_POSITION, a_); t3_release(a_); }
  { t3_attribute *a_ = t3_attribute_new(T3_UINT32, idx, w, 1); t3_geometry_set_index(g, a_); t3_release(a_); }
  t3_geometry_compute_vertex_normals(g);
  free(pos), free(idx);
  return g;
}

static bool near_ramp(float x, float z, float r) {
  for (int k = 0; k < RAMPS; k++) { float dx = x - G.ramp_x[k], dz = z - G.ramp_z[k]; if (dx * dx + dz * dz < r * r) return true; }
  return false;
}
/* the water depth at a point that is not near another stretch of the course */
static bool other_stretch(float x, float z, float s, float clear) {
  for (int i = 0; i < TRACK_N; i++) {
    float ds = fabsf((float)i / TRACK_N * COURSE_LEN - s);
    if (ds > COURSE_LEN * 0.5f) ds = COURSE_LEN - ds;
    if (ds < 60) continue;
    float dx = x - TRACK[i][0], dz = z - TRACK[i][1];
    if (dx * dx + dz * dz < clear * clear) return true;
  }
  return false;
}
static void place(void) {
  school_n = flock_n = 0;
  uint32_t lv = (uint32_t)G.level + 1;
  /* schools: one in each sixteenth of the course, beside the line in water at least 1.4 m deep; they swim half a metre under the surface, where the clear water shows them */
  for (int k = 0; k < SCHOOLS; k++) {
    float s0 = ((float)k + 0.5f) / SCHOOLS * COURSE_LEN;
    for (int tr = 0; tr < 80; tr++) {
      float s = s0 + (h01(lv, (uint32_t)k, (uint32_t)tr * 3) - 0.5f) * COURSE_LEN / SCHOOLS * 0.8f, x, z, dx, dz;
      track_at(s, &x, &z, &dx, &dz);
      float l = sqrtf(dx * dx + dz * dz) + 1e-6f, side = h01(lv, (uint32_t)k, (uint32_t)tr * 3 + 1) < 0.5f ? -1.0f : 1.0f, off = 4.0f + 6.0f * h01(lv, (uint32_t)k, (uint32_t)tr * 3 + 2);
      x += -dz / l * side * off, z += dx / l * side * off;
      float bed = ground_at(x, z);
      if (bed > -1.4f || near_ramp(x, z, 16) || other_stretch(x, z, s, 14)) continue;
      bool clash = false;
      for (int b = 0; b < BUOY_N && !clash; b++) { float ex = x - BUOYS[b][0], ez = z - BUOYS[b][1]; clash = ex * ex + ez * ez < 4.0f * 4.0f; }
      if (clash) continue;
      school *sc = &S[school_n];
      *sc = (school){ x, z, bed, 1.4f + 0.9f * h01(lv, (uint32_t)k, 77), h01(lv, (uint32_t)k, 78) * 6.2832f, 0, 0, 0, s, -1, false };
      for (int f = 0; f < FISH; f++) {
        fish_k[school_n][f][0] = (float)f / FISH * 6.2832f + 0.3f * h01(lv, (uint32_t)k, 100u + (uint32_t)f);
        fish_k[school_n][f][1] = 0.55f + 0.45f * h01(lv, (uint32_t)k, 200u + (uint32_t)f);
        fish_k[school_n][f][2] = (h01(lv, (uint32_t)k, 300u + (uint32_t)f) - 0.5f) * 0.22f;
        fish_k[school_n][f][3] = 0.42f + 0.22f * h01(lv, (uint32_t)k, 400u + (uint32_t)f);
      }
      if (-bed > g_life[5]) g_life[5] = fminf(-bed, 40.0f);
      school_n++;
      break;
    }
  }
  /* flocks: on beaches and low shores 10 to 50 m off the line, low and nearly flat, one in each sixth of the course where there is one */
  for (int k = 0; k < FLOCKS; k++) {
    float s0 = ((float)k + 0.5f) / FLOCKS * COURSE_LEN;
    int before = flock_n;
    for (int tr = 0; tr < 160; tr++) {
      float s = s0 + (h01(lv, 900u + (uint32_t)k, (uint32_t)tr * 3) - 0.5f) * COURSE_LEN / FLOCKS * 0.9f, x, z, dx, dz;
      track_at(s, &x, &z, &dx, &dz);
      float l = sqrtf(dx * dx + dz * dz) + 1e-6f, side = h01(lv, 900u + (uint32_t)k, (uint32_t)tr * 3 + 1) < 0.5f ? -1.0f : 1.0f, off = 10.0f + 40.0f * h01(lv, 900u + (uint32_t)k, (uint32_t)tr * 3 + 2);
      x += -dz / l * side * off, z += dx / l * side * off;
      float g = ground_at(x, z);
      if (g < 0.12f || g > 4.0f) continue;
      float sl = fmaxf(fmaxf(fabsf(ground_at(x + 2, z) - g), fabsf(ground_at(x - 2, z) - g)), fmaxf(fabsf(ground_at(x, z + 2) - g), fabsf(ground_at(x, z - 2) - g)));
      if (sl > 1.0f || near_ramp(x, z, 20)) continue;
      bool flat = true;
      for (int q = 0; q < GULLS && flat; q++) {
        float a = (float)q / GULLS * 6.2832f, gx = x + cosf(a) * 2.2f, gz = z + sinf(a) * 2.2f, gg = ground_at(gx, gz);
        flat = gg > 0.05f && fabsf(gg - g) < 1.0f;
      }
      if (!flat) continue;
      flock *fl = &F[flock_n];
      *fl = (flock){ x, z, g, h01(lv, 900u + (uint32_t)k, 5) * 6.2832f, 0, s, 0, true, false };
      for (int q = 0; q < GULLS; q++) {
        float a = (float)q / GULLS * 6.2832f + h01(lv, 950u + (uint32_t)k, (uint32_t)q), r = 0.6f + 1.8f * h01(lv, 960u + (uint32_t)k, (uint32_t)q);
        gull_k[flock_n][q][0] = cosf(a) * r, gull_k[flock_n][q][1] = sinf(a) * r;
        gull_k[flock_n][q][2] = h01(lv, 970u + (uint32_t)k, (uint32_t)q) * 6.2832f;
        gull_k[flock_n][q][3] = h01(lv, 980u + (uint32_t)k, (uint32_t)q) * 6.2832f;
      }
      flock_n++;
      break;
    }
    if (flock_n > before) continue;
    /* no beach in reach: the flock sits on the water 18 to 32 m off the line, clear of the course and the ramps */
    for (int tr = 0; tr < 60; tr++) {
      float s = s0 + (h01(lv, 1900u + (uint32_t)k, (uint32_t)tr * 3) - 0.5f) * COURSE_LEN / FLOCKS * 0.9f, x, z, dx, dz;
      track_at(s, &x, &z, &dx, &dz);
      float l = sqrtf(dx * dx + dz * dz) + 1e-6f, side = h01(lv, 1900u + (uint32_t)k, (uint32_t)tr * 3 + 1) < 0.5f ? -1.0f : 1.0f, off = 18.0f + 14.0f * h01(lv, 1900u + (uint32_t)k, (uint32_t)tr * 3 + 2);
      x += -dz / l * side * off, z += dx / l * side * off;
      if (ground_at(x, z) > -1.2f || near_ramp(x, z, 22) || other_stretch(x, z, s, 16)) continue;
      flock *fl = &F[flock_n];
      *fl = (flock){ x, z, 0, h01(lv, 1900u + (uint32_t)k, 5) * 6.2832f, 0, s, 0, true, true };
      for (int q = 0; q < GULLS; q++) {
        float a = (float)q / GULLS * 6.2832f + h01(lv, 1950u + (uint32_t)k, (uint32_t)q), r = 0.8f + 2.2f * h01(lv, 1960u + (uint32_t)k, (uint32_t)q);
        gull_k[flock_n][q][0] = cosf(a) * r, gull_k[flock_n][q][1] = sinf(a) * r;
        gull_k[flock_n][q][2] = h01(lv, 1970u + (uint32_t)k, (uint32_t)q) * 6.2832f;
        gull_k[flock_n][q][3] = h01(lv, 1980u + (uint32_t)k, (uint32_t)q) * 6.2832f;
      }
      flock_n++;
      break;
    }
  }
  g_life[0] = (float)school_n, g_life[1] = (float)flock_n;
}

/* where along the course school k (or flock k - 100) lies, for a look at it; -1 when there is none */
float life_track_s(int k) {
  if (k >= 0 && k < school_n) return S[k].s;
  if (k >= 100 && k - 100 < flock_n) return F[k - 100].s;
  return -1;
}

/* a look: school k (or flock k - 100, afloat) moved to the given metres ahead of the player's craft and 3.5 m to its side */
bool life_here(int k, float ahead) {
  const craft *c = &G.c[0];
  float sy = sinf(c->yaw), cy = cosf(c->yaw), x = c->x + sy * ahead - cy * 3.5f, z = c->z + cy * ahead + sy * 3.5f;
  if (k >= 0 && k < school_n) { S[k].x = x, S[k].z = z, S[k].bed = fminf(S[k].bed, -3.0f), S[k].scatter = 0, S[k].st = -1; return true; }
  if (k >= 100 && k - 100 < flock_n) { flock *fl = &F[k - 100]; fl->x = x, fl->z = z, fl->y = 0, fl->afloat = true, fl->state = 0, fl->armed = true; return true; }
  return false;
}

void life_reset(void) {
  for (int k = 0; k < school_n; k++) S[k].scatter = 0, S[k].st = -1, S[k].near = S[k].by = false;
  for (int k = 0; k < flock_n; k++) F[k].state = 0, F[k].t = 0, F[k].armed = true;
  g_life[2] = g_life[3] = g_life[4] = 0;
  have_prev = false;
}

void life_course_apply(cw_node root) {
  if (cw_node_valid(life_n)) { cw_node_free(life_n); life_n = (cw_node){ 0 }; fish_im = gull_im = wing_im = NULL; }
  memset(g_life, 0, sizeof g_life);
  school_n = flock_n = 0;
  if (!g_life_on) return;
  place();
  life_reset();
  static t3_geometry *fg, *gg, *wg;
  static t3_material *fm, *gm, *wm;
  if (!fg) {
    fg = rings_geo(FISH_ROWS, FISH_AROUND, fish_pt), gg = rings_geo(GULL_ROWS, GULL_AROUND, gull_pt), wg = wing_geo();
    fm = t3_mesh_standard_material_new(0x26363e), fm->roughness = 0.4f, fm->metalness = 0.2f; /* dark backs: what shows from above, against the sand */
  }
  /* the look by the switch: bright schools by default (gold and silver, a sheen), the old dark backs with fish_old */
  if (g_fish_old) fm->color = (t3_color){ 0.019f, 0.037f, 0.047f }, fm->roughness = 0.4f, fm->metalness = 0.2f, fm->emissive = (t3_color){ 0, 0, 0 };
  else fm->color = (t3_color){ 0.95f, 0.72f, 0.16f }, fm->roughness = 0.3f, fm->metalness = 0.35f, fm->emissive = (t3_color){ 0.12f, 0.08f, 0.01f };
  fm->version++;
  {
    gm = t3_mesh_standard_material_new(0xeeeeea), gm->roughness = 0.85f, gm->metalness = 0;    /* feathers: matte */
    wm = t3_mesh_standard_material_new(0x9da5ad), wm->roughness = 0.85f, wm->metalness = 0;
  }
  life_n = cw_node_new("Node3D", "Life");
  cw_node_add_child(root, life_n);
  t3_sphere big = { { 0, 0, 0 }, 4000 };
  struct { t3_instanced_mesh **im; t3_geometry *g; t3_material *m; int n; const char *name; } parts[3] = {
    { &fish_im, fg, fm, school_n * FISH, "Fish" }, { &gull_im, gg, gm, flock_n * GULLS, "Gulls" }, { &wing_im, wg, wm, flock_n * GULLS, "Wings" } };
  for (int p = 0; p < 3; p++) {
    if (!parts[p].n) continue;
    *parts[p].im = t3_instanced_mesh_new(parts[p].g, parts[p].m, parts[p].n);
    ((t3_object *)*parts[p].im)->receive_shadow = true;
    t3_instanced_mesh_set_bounding_sphere(*parts[p].im, &big);
    cw_node n = cw_node_new("Node3D", parts[p].name);
    cw_node_add_child(life_n, n);
    cw_node3d_set_object(n, (t3_object *)*parts[p].im);
    t3_release(*parts[p].im);
  }
}

/* the nearest a point comes to the segment a..b, squared */
static float seg_d2(float px, float pz, float ax, float az, float bx, float bz) {
  float vx = bx - ax, vz = bz - az, l2 = vx * vx + vz * vz, t = l2 > 1e-6f ? ((px - ax) * vx + (pz - az) * vz) / l2 : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  float dx = ax + vx * t - px, dz = az + vz * t - pz;
  return dx * dx + dz * dz;
}
void life_tick(float dt) {
  if (!g_life_on || (!school_n && !flock_n)) return;
  /* every racer scatters fish and lifts gulls; the player's passes are counted. The segment from last tick's place
     catches a fast craft whatever the tick rate */
  const craft *p = &G.c[0];
  float ax = have_prev ? prev_x : p->x, az = have_prev ? prev_z : p->z;
  for (int k = 0; k < school_n; k++) {
    school *sc = &S[k];
    sc->phase += dt * (0.5f + 1.6f * sc->scatter);
    if (sc->scatter > 0) sc->scatter = fmaxf(0.0f, sc->scatter - dt / 3.5f);
    if (sc->st >= 0) { sc->st += dt; if (sc->st > 3.0f) sc->st = -1; }
    /* a school feeding at the surface: now and then a fish flips and leaves a ring and a fleck of white, so a school
       reads from a racing craft's low camera, where the sea's mirror hides what is under it (presentation only) */
    if (!g_fish_old && sc->scatter < 0.15f) {
      uint32_t t = (uint32_t)(G.tick / 9u) + (uint32_t)k * 7u;
      if (G.tick % 9u == (uint32_t)k % 9u) {
        float a = h01(t, (uint32_t)k, 11) * 6.2832f, r = sc->r * (0.3f + 0.7f * h01(t, (uint32_t)k, 12));
        cw_water_splash(G.water, sc->x + cosf(a) * r, sc->z + sinf(a) * r, 0.7f, 0.06f, 1.0f);
      }
    }
    bool fresh = sc->scatter < 0.15f;
    for (int i = 0; i < RACERS; i++) {
      const craft *c = &G.c[i];
      float bx = c->x, bz = c->z, d2 = i == 0 ? seg_d2(sc->x, sc->z, ax, az, bx, bz) : (sc->x - bx) * (sc->x - bx) + (sc->z - bz) * (sc->z - bz);
      if (d2 < SCATTER_R * SCATTER_R && c->speed > 3 && !c->air) {
        if (fresh) { sc->ax = c->x, sc->az = c->z, sc->st = 0; fresh = false; }
        sc->scatter = 1;
      }
      if (i == 0) {
        /* the player scattering it: each time it comes within reach at speed (a rival ahead may have scattered it already) */
        bool by = d2 < SCATTER_R * SCATTER_R && c->speed > 3 && !c->air;
        if (by && !sc->by && G.state == RACE_ON) g_life[2] += 1;
        sc->by = by;
        bool nr = d2 < NEAR_R * NEAR_R;
        if (nr && !sc->near && G.state == RACE_ON) g_life[4] += 1;
        sc->near = nr;
      }
    }
  }
  for (int k = 0; k < flock_n; k++) {
    flock *fl = &F[k];
    fl->t += dt;
    if (fl->state == 0) {
      float dmin = 1e9f;
      for (int i = 0; i < RACERS; i++) {
        const craft *c = &G.c[i];
        float d2 = i == 0 ? seg_d2(fl->x, fl->z, ax, az, c->x, c->z) : (fl->x - c->x) * (fl->x - c->x) + (fl->z - c->z) * (fl->z - c->z);
        if (d2 < dmin) dmin = d2, fl->dir = cw_atan2(fl->x - c->x, fl->z - c->z); /* away from the nearest */
      }
      if (dmin < LIFT_R * LIFT_R && fl->armed) { fl->state = 1, fl->t = 0, fl->armed = false; g_life[3] += 1; }
      if (dmin > (LIFT_R + 25) * (LIFT_R + 25)) fl->armed = true;
    } else if (fl->state == 1 && fl->t > 16) fl->state = 2, fl->t = 0;
    else if (fl->state == 2 && fl->t > 5) fl->state = 0, fl->t = 0;
  }
  prev_x = p->x, prev_z = p->z, have_prev = true;
}

void life_sync(double wt) {
  if (!g_life_on) return;
  if (fish_im) {
    int n = 0;
    g_life[6] = 0;
    for (int k = 0; k < school_n; k++) {
      const school *sc = &S[k];
      float surf = cw_water_height_at(G.water, sc->x, sc->z, wt), mid = surf - (g_fish_old ? 0.22f : 0.03f) + g_fish_lift; /* (their backs just in the surface: under it, at a racing camera's low angle, the sea's mirror hides them) */ /* just under: the see-through water shows what is in its top half metre */
      float e = sc->scatter * sc->scatter * (3 - 2 * sc->scatter); /* eased: a burst out, a slow drift back */
      for (int f = 0; f < FISH; f++) {
        const float *q = fish_k[k][f];
        float a = q[0] + sc->phase, r = sc->r * q[1];
        float x = sc->x + cosf(a) * r, z = sc->z + sinf(a) * r, y = mid + q[2];
        /* scattered: out from the craft that came by and down */
        float ox = x - sc->ax, oz = z - sc->az, ol = sqrtf(ox * ox + oz * oz) + 1e-3f;
        x += ox / ol * 5.5f * e * q[1], z += oz / ol * 5.5f * e * q[1], y -= 0.35f * e;
        y = fmaxf(y, sc->bed + 0.3f);
        /* startled: half the school leaps clear of the water one after another, nose up then down, in the first second
           and a half after it is startled (what you see of a school from a racing craft: the water itself hides fish a few
           metres off) */
        float pitch = 0;
        if (sc->st >= 0 && !(f & 1)) {
          float u = (sc->st - 0.09f * (float)(f / 2)) / 0.55f;
          if (u > 0 && u < 1) { y = fmaxf(y, surf - 0.2f) + 1.1f * q[1] * sinf(3.14159f * u); pitch = -0.9f * cosf(3.14159f * u); }
        }
        /* heading along the ring (anticlockwise), turned out when fleeing, with a swimming wag */
        float yaw = turn_to(cw_atan2(-sinf(a), cosf(a)), cw_atan2(ox, oz), e) + 0.18f * sinf(sc->phase * 9.0f + (float)f);
        float s = q[3] * (g_fish_old ? 1.0f : 2.0f);
        bool up = y > surf + 0.05f;
        if (up) g_life[6] += 1;
        if (up != fish_up[k][f]) cw_water_splash(G.water, x, z, 0.3f + 0.25f * q[1], 0.02f, 0.55f); /* the ring and foam where it breaks the surface */
        fish_up[k][f] = up;
        t3_mat4 m;
        t3_mat4_compose(&m, (t3_vec3){ x, y, z }, t3_quat_from_euler((t3_euler){ pitch, yaw, 0, T3_YXZ }), (t3_vec3){ s, s, s });
        t3_instanced_mesh_set_matrix_at(fish_im, n++, &m);
      }
    }
    fish_im->count = g_fish_hide ? 0 : n;
    if (fish_im->instance_matrix) fish_im->instance_matrix->version++;
  }
  if (gull_im && wing_im) {
    int n = 0;
    for (int k = 0; k < flock_n; k++) {
      const flock *fl = &F[k];
      for (int q = 0; q < GULLS; q++) {
        const float *g = gull_k[k][q];
        float x = fl->x + g[0], z = fl->z + g[1], y = fl->afloat ? cw_water_height_at(G.water, x, z, wt) - 0.08f : ground_at(x, z) + 0.12f, yaw = g[2], pitch = 0, bank = 0, flap = 0.25f, fold = 0.35f;
        if (fl->state) {
          /* up and away from the craft, then wheeling round over the beach; coming down, the same circle lower and lower */
          float t = fl->t, up = fl->state == 1 ? fminf(1.0f, t / 2.2f) : 1.0f - fminf(1.0f, t / 5.0f), ue = up * up * (3 - 2 * up);
          float orbit = (fl->state == 1 ? t : 16 + t) * 0.32f + g[3], rad = 9.0f + 2.5f * sinf(g[3] * 3.0f);
          float lx = fl->x + sinf(fl->dir) * 10.0f * ue + cosf(orbit) * rad * ue + g[0], lz = fl->z + cosf(fl->dir) * 10.0f * ue + sinf(orbit) * rad * ue + g[1];
          x = lx, z = lz, y = fl->y + 0.12f + ue * (7.0f + 1.5f * sinf(g[3] * 5.0f + t * 0.7f));
          yaw = turn_to(yaw, cw_atan2(-sinf(orbit), cosf(orbit)), ue); /* along the circle */
          bank = -0.35f * ue, pitch = fl->state == 1 && t < 2.2f ? -0.35f : 0;
          float beat = fl->state == 1 && t < 3.0f ? 1.0f : 0.35f + 0.25f * sinf(t * 0.9f + g[3]);
          flap = cosf(t * (fl->state == 1 && t < 3.0f ? 26.0f : 14.0f) + g[3]) * beat, fold = 1.0f;
        }
        t3_quat qr = t3_quat_from_euler((t3_euler){ pitch, yaw, bank, T3_YXZ });
        t3_mat4 m;
        t3_mat4_compose(&m, (t3_vec3){ x, y + 0.16f, z }, qr, (t3_vec3){ 0.42f, 0.42f, 0.42f });
        t3_instanced_mesh_set_matrix_at(gull_im, n, &m);
        /* folded on the sand (narrow, along the back); spread and beating in the air */
        t3_mat4_compose(&m, (t3_vec3){ x, y + 0.2f, z }, qr, (t3_vec3){ 0.55f * fold, 0.55f * (fold < 1 ? 0.3f : flap), 0.55f });
        t3_instanced_mesh_set_matrix_at(wing_im, n, &m);
        n++;
      }
    }
    if (gull_im->instance_matrix) gull_im->instance_matrix->version++;
    if (wing_im->instance_matrix) wing_im->instance_matrix->version++;
  }
}
