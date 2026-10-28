/* Tux's smooth skin: the torso, chest, neck and head ellipsoids of tux.tcl
 * (black bodies with the white fronts) as one blended surface instead of
 * seven intersecting spheres, skinned to the torso, neck and head joints so
 * the slide pose and the walk still turn his head. The surface is the
 * isosurface of a sum of soft ellipsoid fields (metaballs), extracted with
 * surface nets on a regular grid at load, coloured per vertex by each
 * field's share (the white chest blends into the black back) and weighted
 * to the bones the same way. The other spheres (beak, eyes, flippers, legs,
 * feet, tail) stay as they are and ride their joints. */
#include "game.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/skin_shading.h"
#include "cartwheel/shading.h"

#define SK_RES 72           /* grid corners per axis */
#define SK_SOFT 1.09f       /* field radius over the sphere radius: isolated spheres keep their size */

typedef struct blob { t3_mat4 inv; int bone; float white; } blob;
static blob BL[8];
static int nbl;

static float field_one(const blob *b, t3_vec3 p) {
  t3_vec3 q = t3_vec3_apply_mat4(p, &b->inv);
  float r2 = t3_vec3_length_sq(q) / (SK_SOFT * SK_SOFT);
  if (r2 >= 1) return 0;
  float t = 1 - r2;
  return t * t;
}
static float field(t3_vec3 p) {
  float s = 0;
  for (int i = 0; i < nbl; i++) s += field_one(&BL[i], p);
  return s;
}
static const float ISO = (1 - 1 / (SK_SOFT * SK_SOFT)) * (1 - 1 / (SK_SOFT * SK_SOFT)); /* an isolated sphere's own surface */

typedef struct skv { float p[3], n[3], c[3], w[3]; } skv;

/* the outward normal: the field grows toward the centres, so minus its gradient */
static t3_vec3 gradient(t3_vec3 p, float h) {
  return t3_vec3_normalize((t3_vec3){ -field((t3_vec3){ p.x + h, p.y, p.z }) + field((t3_vec3){ p.x - h, p.y, p.z }),
                                      -field((t3_vec3){ p.x, p.y + h, p.z }) + field((t3_vec3){ p.x, p.y - h, p.z }),
                                      -field((t3_vec3){ p.x, p.y, p.z + h }) + field((t3_vec3){ p.x, p.y, p.z - h }) });
}

/* the body spheres: node index, bone (0 torso, 1 neck, 2 head), white front */
static const struct { int node, bone, white; } BODY[] = { { 3, 0, 0 }, { 4, 0, 1 }, { 7, 0, 0 }, { 8, 0, 1 }, { 13, 1, 0 }, { 14, 1, 1 }, { 19, 2, 0 } };

bool tux_skin_build(tux_model *m) {
  int bones_idx[3] = { 1, m->joint[J_NECK], m->joint[J_HEAD] };
  if (bones_idx[1] < 0 || bones_idx[2] < 0 || m->count < 20) return false;
  t3_object_update_world_matrix(m->root, false, true); /* the rest pose, the root at the origin */
  nbl = 0;
  t3_vec3 lo = { 1e9f, 1e9f, 1e9f }, hi = { -1e9f, -1e9f, -1e9f };
  for (size_t i = 0; i < sizeof BODY / sizeof BODY[0]; i++) {
    tux_node *nd = &m->nodes[BODY[i].node];
    if (!nd->sphere || !nd->obj) continue;
    blob *b = &BL[nbl++];
    t3_mat4_invert(&b->inv, &nd->obj->matrix_world);
    b->bone = BODY[i].bone;
    b->white = (float)BODY[i].white;
    for (int k = 0; k < 8; k++) { /* the ellipsoid's box: the unit cube's corners through the matrix */
      t3_vec3 c = { k & 1 ? 1.2f : -1.2f, k & 2 ? 1.2f : -1.2f, k & 4 ? 1.2f : -1.2f };
      c = t3_vec3_apply_mat4(c, &nd->obj->matrix_world);
      lo.x = fminf(lo.x, c.x), lo.y = fminf(lo.y, c.y), lo.z = fminf(lo.z, c.z);
      hi.x = fmaxf(hi.x, c.x), hi.y = fmaxf(hi.y, c.y), hi.z = fmaxf(hi.z, c.z);
    }
    nd->obj->visible = false; /* the sphere is in the skin now */
  }
  if (nbl < 2) return false;
  const int N = SK_RES;
  t3_vec3 ext = t3_vec3_sub(hi, lo);
  float h = fmaxf(ext.x, fmaxf(ext.y, ext.z)) / (N - 3);
  t3_vec3 org = { (lo.x + hi.x - h * (N - 1)) * 0.5f, (lo.y + hi.y - h * (N - 1)) * 0.5f, (lo.z + hi.z - h * (N - 1)) * 0.5f };
  float *F = malloc(sizeof(float) * (size_t)N * N * N);
  int *cell = malloc(sizeof(int) * (size_t)(N - 1) * (N - 1) * (N - 1));
#define FI(i, j, k) F[((k) * N + (j)) * N + (i)]
#define CI(i, j, k) cell[((k) * (N - 1) + (j)) * (N - 1) + (i)]
  for (int k = 0; k < N; k++)
    for (int j = 0; j < N; j++)
      for (int i = 0; i < N; i++) FI(i, j, k) = field((t3_vec3){ org.x + i * h, org.y + j * h, org.z + k * h }) - ISO;
  /* surface nets: one vertex per cell the surface crosses, at the mean of its edge crossings */
  skv *vs = NULL;
  int nv = 0, vcap = 0;
  static const int E[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
  for (int k = 0; k < N - 1; k++)
    for (int j = 0; j < N - 1; j++)
      for (int i = 0; i < N - 1; i++) {
        float f[8];
        for (int c = 0; c < 8; c++) f[c] = FI(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1));
        int inside = 0;
        for (int c = 0; c < 8; c++) inside |= (f[c] > 0) << c;
        CI(i, j, k) = -1;
        if (inside == 0 || inside == 255) continue;
        t3_vec3 sum = { 0, 0, 0 };
        int cnt = 0;
        for (int e = 0; e < 12; e++) {
          int a = E[e][0], b = E[e][1];
          if ((f[a] > 0) == (f[b] > 0)) continue;
          float t = f[a] / (f[a] - f[b]);
          t3_vec3 pa = { org.x + (i + (a & 1)) * h, org.y + (j + ((a >> 1) & 1)) * h, org.z + (k + ((a >> 2) & 1)) * h };
          t3_vec3 pb = { org.x + (i + (b & 1)) * h, org.y + (j + ((b >> 1) & 1)) * h, org.z + (k + ((b >> 2) & 1)) * h };
          sum = t3_vec3_add(sum, t3_vec3_lerp(pa, pb, t));
          cnt++;
        }
        if (nv == vcap) { vcap = vcap ? vcap * 2 : 4096; vs = realloc(vs, sizeof *vs * (size_t)vcap); }
        skv *v = &vs[nv];
        t3_vec3 p = t3_vec3_scale(sum, 1.0f / cnt);
        t3_vec3 n = gradient(p, h * 0.5f);
        v->p[0] = p.x, v->p[1] = p.y, v->p[2] = p.z;
        v->n[0] = n.x, v->n[1] = n.y, v->n[2] = n.z;
        /* colour and bone weights: each field's share at the vertex */
        float tot = 0, white = 0, w[3] = { 0, 0, 0 };
        for (int b = 0; b < nbl; b++) { float fb = field_one(&BL[b], p); tot += fb; white += fb * BL[b].white; w[BL[b].bone] += fb; }
        if (tot < 1e-6f) tot = 1e-6f;
        float wh = white / tot;
        float shade = 0.035f + (0.92f - 0.035f) * (wh < 0.35f ? 0 : wh > 0.65f ? 1 : (wh - 0.35f) / 0.3f); /* a clean edge between black and white */
        v->c[0] = v->c[1] = v->c[2] = shade;
        for (int b = 0; b < 3; b++) v->w[b] = w[b] / tot;
        CI(i, j, k) = nv++;
      }
  /* quads across every crossed edge, between the four cells around it; wound with the gradient */
  uint32_t *ix = NULL;
  int ni = 0, icap = 0;
  for (int k = 1; k < N - 1; k++)
    for (int j = 1; j < N - 1; j++)
      for (int i = 1; i < N - 1; i++) {
        float f0 = FI(i, j, k);
        for (int axis = 0; axis < 3; axis++) {
          int i1 = i + (axis == 0), j1 = j + (axis == 1), k1 = k + (axis == 2);
          if (i1 >= N || j1 >= N || k1 >= N) continue;
          float f1 = FI(i1, j1, k1);
          if ((f0 > 0) == (f1 > 0)) continue;
          int q[4];
          if (axis == 0) q[0] = CI(i, j - 1, k - 1), q[1] = CI(i, j, k - 1), q[2] = CI(i, j, k), q[3] = CI(i, j - 1, k);
          else if (axis == 1) q[0] = CI(i - 1, j, k - 1), q[1] = CI(i - 1, j, k), q[2] = CI(i, j, k), q[3] = CI(i, j, k - 1);
          else q[0] = CI(i - 1, j - 1, k), q[1] = CI(i, j - 1, k), q[2] = CI(i, j, k), q[3] = CI(i - 1, j, k);
          if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[3] < 0) continue;
          if (f0 > 0) { int t = q[1]; q[1] = q[3]; q[3] = t; } /* leaving the solid along the axis: flip */
          if (ni + 6 > icap) { icap = icap ? icap * 2 : 8192; ix = realloc(ix, sizeof *ix * (size_t)icap); }
          ix[ni++] = (uint32_t)q[0], ix[ni++] = (uint32_t)q[1], ix[ni++] = (uint32_t)q[2];
          ix[ni++] = (uint32_t)q[0], ix[ni++] = (uint32_t)q[2], ix[ni++] = (uint32_t)q[3];
        }
      }
  free(F), free(cell);
  /* every triangle wound against its vertices' normals (the axis rule above is a guess per axis) */
  for (int t = 0; t < ni; t += 3) {
    const skv *a = &vs[ix[t]], *b = &vs[ix[t + 1]], *c = &vs[ix[t + 2]];
    t3_vec3 e1 = { b->p[0] - a->p[0], b->p[1] - a->p[1], b->p[2] - a->p[2] }, e2 = { c->p[0] - a->p[0], c->p[1] - a->p[1], c->p[2] - a->p[2] };
    t3_vec3 fn = t3_vec3_cross(e1, e2), vn = { a->n[0] + b->n[0] + c->n[0], a->n[1] + b->n[1] + c->n[1], a->n[2] + b->n[2] + c->n[2] };
    if (t3_vec3_dot(fn, vn) < 0) { uint32_t tmp = ix[t + 1]; ix[t + 1] = ix[t + 2]; ix[t + 2] = tmp; }
  }
  /* the geometry */
  float *P = malloc(sizeof(float) * 3 * (size_t)nv), *Nn = malloc(sizeof(float) * 3 * (size_t)nv), *C = malloc(sizeof(float) * 3 * (size_t)nv);
  float *SI = calloc((size_t)nv * 4, sizeof(float)), *SW = calloc((size_t)nv * 4, sizeof(float));
  for (int v = 0; v < nv; v++) {
    memcpy(P + v * 3, vs[v].p, 12), memcpy(Nn + v * 3, vs[v].n, 12), memcpy(C + v * 3, vs[v].c, 12);
    SI[v * 4] = 0, SI[v * 4 + 1] = 1, SI[v * 4 + 2] = 2, SI[v * 4 + 3] = 0;
    SW[v * 4] = vs[v].w[0], SW[v * 4 + 1] = vs[v].w[1], SW[v * 4 + 2] = vs[v].w[2], SW[v * 4 + 3] = 0;
  }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *ap = t3_attribute_new(T3_FLOAT32, P, nv, 3), *an = t3_attribute_new(T3_FLOAT32, Nn, nv, 3), *ac = t3_attribute_new(T3_FLOAT32, C, nv, 3),
               *asi = t3_attribute_new(T3_FLOAT32, SI, nv, 4), *asw = t3_attribute_new(T3_FLOAT32, SW, nv, 4), *ai = t3_attribute_new(T3_UINT32, ix, ni, 1);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, ap);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, an);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, ac);
  t3_geometry_set_attribute(g, T3_ATTR_SKIN_INDEX, asi);
  t3_geometry_set_attribute(g, T3_ATTR_SKIN_WEIGHT, asw);
  t3_geometry_set_index(g, ai);
  t3_release(ap), t3_release(an), t3_release(ac), t3_release(asi), t3_release(asw), t3_release(ai);
  free(P), free(Nn), free(C), free(SI), free(SW), free(ix), free(vs);
  /* the plumage: one material, the colour from the vertices */
  t3_material *mat = t3_mesh_physical_material_new(0xffffff);
  mat->vertex_colors = true;
  mat->roughness = 0.55f;
  mat->metalness = 0;
  mat->sheen = 0.5f;
  mat->sheen_roughness = 0.7f;
  mat->sheen_color = (t3_color){ 0.55f, 0.58f, 0.66f };
  cw_shading_add(mat, "feather"); /* shaders/feather.mjs: striations and a rim light */
  { /* v0.18 skin shading: light wraps past the terminator and shows through the flippers from behind, so the
     * black back is not a flat silhouette against the snow; off on Low by the engine */
    cw_skin_shading_desc sd = cw_skin_shading_default();
    sd.scatter = (t3_color){ 0.55f, 0.42f, 0.36f };
    sd.wrap = 0.3f, sd.translucency = 0.12f, sd.distortion = 0.3f, sd.power = 4;
    cw_skin_shading_apply(mat, &sd);
  }
  m->skin_mat = mat;
  t3_skinned_mesh *sm = t3_skinned_mesh_new(g, mat);
  t3_release(g);
  sm->mesh.base.frustum_culled = false;
  sm->mesh.base.cast_shadow = true;
  sm->mesh.base.receive_shadow = true;
  t3_object_add(m->root, sm);
  t3_object *bones[3] = { m->nodes[bones_idx[0]].obj, m->nodes[bones_idx[1]].obj, m->nodes[bones_idx[2]].obj };
  t3_skeleton *sk = t3_skeleton_new(bones, 3, NULL);
  t3_object_update_world_matrix(sm, false, false);
  t3_skinned_mesh_bind(sm, sk, NULL);
  t3_release(sk);
  m->skin = sm;
  cw_info("tux skin: %d vertices, %d triangles from %d blobs (grid %d, cell %.1f mm)", nv, ni / 3, nbl, N, h * 1000);
  return true;
}
