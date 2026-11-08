/* fighter crowd: low detail spectators (models/crowd0..8.glb, tools/make-characters.mjs) standing around the stage.
 * They turn to follow the fight, shift their weight, clap when it gets going and throw their fists up on big hits,
 * supers and knockouts. Each one hears an event late by their distance from it (a ripple out from the blow, never the
 * whole crowd on one frame), has their own threshold and rhythm, and answers in their own way: both fists up, one
 * fist pumping, pointing at the fight, clapping; a big hit near them makes them flinch back first. Excitement comes from
 * the simulation's events; time from its tick (the same on every host). Render only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "game.h"

enum { C_HIPS, C_SPINE, C_CHEST, C_HEAD, C_UAL, C_LAL, C_UAR, C_LAR, C_FL, C_FR, CB };
static const char *const CBONE[CB] = { "Hips", "Spine", "Chest", "Head", "UpperArmL", "LowerArmL", "UpperArmR", "LowerArmR", "Middle1L", "Middle1R" };
/* the other knuckles curl with the middle finger */
static const char *const CFING[2][3] = { { "Index1L", "Ring1L", "Pinky1L" }, { "Index1R", "Ring1R", "Pinky1R" } };

/* where people stand per stage: x, z, floor height, model */
typedef struct spot { float x, z, y; int model; } spot;
static const spot SPOTS[3][10] = {
  { { -7.4f, -5.6f, 0, 0 }, { -6.2f, -6.3f, 0, 1 }, { -3.8f, -5.4f, 0, 2 }, { -1.2f, -6.1f, 0, 3 }, { 1.6f, -5.5f, 0, 4 }, { 3.4f, -6.4f, 0, 5 },
    { 6.2f, -5.3f, 0, 0 }, { 7.6f, -6.2f, 0, 3 }, { -13.4f, -0.8f, 0, 2 }, { 13.2f, 0.4f, 0, 1 } },
  { { -8.6f, -4.1f, 0, 3 }, { -5.0f, -4.6f, 0, 5 }, { -1.3f, -6.9f, 0, 1 }, { 1.2f, -7.6f, 0, 0 }, { 0.2f, -9.5f, 0, 2 }, { 4.2f, -4.4f, 0, 4 },
    { 8.8f, -4.0f, 0, 2 }, { 11.2f, -4.6f, 0, 0 }, { -11.6f, -4.3f, 0, 4 }, { -2.4f, -10.8f, 0, 3 } },
  { { -10.3f, -6.4f, 0.5f, 6 }, { -8.7f, -6.7f, 0.5f, 7 }, { -7.1f, -6.4f, 0.5f, 6 }, { -1.4f, -7.0f, 0.5f, 8 }, { 7.1f, -6.4f, 0.5f, 7 }, { 8.7f, -6.7f, 0.5f, 6 },
    { 10.3f, -6.4f, 0.5f, 7 }, { 1.2f, -7.0f, 0.5f, 6 }, { -13.6f, -2.2f, 0, 7 }, { 13.8f, -2.6f, 0, 6 } },
};

static void euler(const float r[3], t3_quat *q) {
  float sx, cx, sy, cy, sz, cz;
  cw_sincos(r[0] * 0.5f, &sx, &cx); cw_sincos(r[1] * 0.5f, &sy, &cy); cw_sincos(r[2] * 0.5f, &sz, &cz);
  *q = t3_quat_multiply(t3_quat_multiply((t3_quat){ 0, sy, 0, cy }, (t3_quat){ sx, 0, 0, cx }), (t3_quat){ 0, 0, sz, cz });
}

void crowd_load(crowd *c, int stage, cw_node parent) {
  memset(c, 0, sizeof *c);
  for (int i = 0; i < CROWD_MAX; i++) {
    const spot *s = &SPOTS[stage % 3][i];
    char path[40];
    snprintf(path, sizeof path, "models/crowd%d.glb", s->model);
    size_t len = 0;
    void *glb = cw_asset_load(path, &len);
    if (!glb) { cw_warn("fighter: %s missing from the assets", path); continue; }
    crowd_member *m = &c->m[c->n];
    m->g = t3_gltf_parse(glb, len, NULL, NULL);
    free(glb);
    if (!m->g) continue;
    bool ok = true;
    for (int b = 0; b < CB && ok; b++) ok = (m->b[b] = t3_object_get_by_name(m->g->scene, CBONE[b])) != NULL;
    for (int h = 0; h < 2; h++) for (int k = 0; k < 3; k++) m->fing[h][k] = t3_object_get_by_name(m->g->scene, CFING[h][k]);
    if (!ok) { t3_release(m->g); m->g = NULL; continue; }
    m->hips = m->b[C_HIPS]->position;
    char nm[16];
    snprintf(nm, sizeof nm, "Crowd%d", i);
    m->node = cw_node_new("Node3D", nm);
    cw_node_add_child(parent, m->node);
    char_shade(m->g->scene);
    cw_node3d_set_object(m->node, m->g->scene);
    cw_node3d_set_position(m->node, (t3_vec3){ s->x, s->y, s->z });
    m->x = s->x; m->z = s->z;
    m->phase = (float)((i * 53) % 97) * 0.065f;
    m->thr = 0.15f + 0.07f * (float)((i * 29) % 9);
    m->rate = 8.0f + 0.35f * (float)((i * 41) % 7); /* their own pumping and clapping tempo */
    m->kind = (i * 7 + stage * 3) % 4;
    c->n++;
  }
  c->seen = 0;
}

void crowd_free(crowd *c) {
  for (int i = 0; i < c->n; i++) {
    if (c->m[i].node.id && cw_node_valid(c->m[i].node)) cw_node_free(c->m[i].node);
    if (c->m[i].g) t3_release(c->m[i].g);
  }
  memset(c, 0, sizeof *c);
}

int32_t crowd_same_max;
int crowd_sync; /* test switch (sw crowd_sync 1): everyone hears every event at once and answers alike, as the crowd once did */
static uint32_t chash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

void crowd_frame(crowd *c, const ft_game *g, float dt) {
  if (!c->n) return;
  /* events become kicks at the place they happened; each spectator takes a kick some ticks later: a fifth of a second
   * plus about a tenth for every two metres away, plus a little of their own */
  uint32_t n = g->ev_seq;
  if (n - c->seen > 64) c->seen = n - 64;
  if (c->seen > n) c->seen = n; /* a new match restarts the event count */
  for (; c->seen < n; c->seen++) {
    const ft_event *e = &g->ev[c->seen & 63];
    float amt = e->type == EV_HIT ? 0.1f + 0.08f * (float)e->power : e->type == EV_THROW || e->type == EV_WALL ? 0.25f : e->type == EV_SUPER ? 0.7f : e->type == EV_KO ? 1.5f : 0;
    if (amt <= 0) continue;
    c->k[c->nk & 15] = (crowd_kick){ g->tick, (float)e->x / 1000.0f, amt, e->type, e->power };
    c->nk++;
  }
  float t = (float)g->tick / 60.0f;
  float cx = ((float)g->f[0].x + (float)g->f[1].x) * 0.0005f;
  float decay = cw_exp(-dt * 0.45f);
  for (int i = 0; i < c->n; i++) {
    crowd_member *m = &c->m[i];
    /* the kicks that have reached this spectator */
    if (c->nk - c->done[i] > 16) c->done[i] = c->nk - 16;
    if (c->done[i] > c->nk) c->done[i] = c->nk;
    while (c->done[i] < c->nk) {
      const crowd_kick *k = &c->k[c->done[i] & 15];
      float dx = m->x - k->x, d = sqrtf(dx * dx + m->z * m->z);
      uint32_t delay = crowd_sync ? 0 : 12u + (uint32_t)(d * 3.0f) + chash((uint32_t)i * 977u + c->done[i]) % 7u;
      if (g->tick < k->tick + delay && g->tick + 600 > k->tick) break;
      m->ex += k->amount;
      /* a fresh way to answer every so often, and a flinch when a heavy blow lands near */
      if (k->type == EV_SUPER || k->type == EV_KO || chash((uint32_t)i * 131u + c->done[i] * 7u) % 3 == 0) m->kind = (int)(chash((uint32_t)i * 31u + c->done[i] * 13u) % 4);
      if ((k->type == EV_HIT && k->power >= 3) || k->type == EV_KO) if (d < 9.0f && chash((uint32_t)i + c->done[i] * 5u) % 2) m->flinch = 1;
      c->done[i]++;
    }
    if (crowd_sync) { m->kind = (i & 1) ? 1 : 0; m->rate = 9.0f; m->flinch = 0; m->phase = (float)((i * 53) % 97) * 0.065f; }
    m->ex = cw_clampf(m->ex, 0, 1.5f) * decay;
    m->flinch *= cw_exp(-dt * 3.5f);
    float ph = m->phase, sway = (float)cw_sin(t * 0.7f + ph);
    float cheer = cw_clampf((m->ex - m->thr) * 3.0f, 0, 1), clap = cw_clampf((m->ex - m->thr * 0.4f) * 4.0f, 0, 1) * (1 - cheer);
    if (m->kind == 3) { clap = fmaxf(clap, cheer); cheer = 0; } /* this one claps through the big moments */
    m->cheer += (cheer - m->cheer) * (1 - cw_exp(-dt * 6.0f));
    m->clap += (clap - m->clap) * (1 - cw_exp(-dt * 5.0f));
    float r[CB][3];
    memset(r, 0, sizeof r);
    /* watching: weight on one leg, arms loose, head toward the fight */
    r[C_SPINE][2] = 0.03f * sway; r[C_CHEST][0] = 0.04f; r[C_HEAD][0] = 0.05f;
    r[C_UAL][2] = 0.12f; r[C_UAR][2] = -0.12f; r[C_LAL][0] = -0.25f; r[C_LAR][0] = -0.3f;
    float fl = 0.7f, fr = 0.7f;
    /* clapping: forearms up in front, hands meeting at their own tempo */
    float cl = m->clap, beat = 0.5f + 0.5f * (float)cw_sin(t * m->rate * 3.0f + ph * 3);
    r[C_UAL][0] += -0.75f * cl; r[C_UAR][0] += -0.75f * cl; r[C_UAL][2] += (-0.1f - 0.25f * beat) * cl; r[C_UAR][2] -= (-0.1f - 0.25f * beat) * cl;
    r[C_LAL][0] += -1.3f * cl; r[C_LAR][0] += -1.3f * cl; fl -= 0.6f * cl; fr -= 0.6f * cl;
    /* cheering, their way: both fists up and pumping, one fist pumping, or an arm out pointing at the fight */
    float ch = m->cheer, pump = (float)cw_sin(t * m->rate + ph * 5);
    if (m->kind == 2) { /* pointing: the arm straight out toward the fighters, the other hand on the hip */
      float side = cx > m->x ? -1.0f : 1.0f;
      r[C_UAR][0] += (-1.45f + 0.08f * pump) * ch; r[C_UAR][2] += 0.35f * side * ch; r[C_LAR][0] += -0.05f * ch; fr += 0.2f * ch;
      r[C_UAL][2] += 0.5f * ch; r[C_LAL][0] += -1.6f * ch;
      r[C_CHEST][1] = 0.15f * side * ch;
    } else {
      bool both = m->kind == 0;
      r[C_UAR][0] += (-2.7f + 0.25f * pump) * ch; r[C_UAR][2] += -0.25f * ch; r[C_LAR][0] += (-0.4f - 0.3f * pump) * ch;
      if (both) { r[C_UAL][0] += (-2.7f - 0.25f * pump) * ch; r[C_UAL][2] += 0.25f * ch; r[C_LAL][0] += (-0.4f + 0.3f * pump) * ch; }
      else { r[C_UAL][0] += -0.4f * ch; r[C_LAL][0] += -1.2f * ch; }
    }
    r[C_CHEST][0] -= 0.12f * ch; r[C_HEAD][0] -= 0.15f * ch;
    /* a flinch: leaning back, hands coming up in front of the face */
    float fn = m->flinch;
    r[C_SPINE][0] += 0.18f * fn; r[C_HEAD][0] += 0.12f * fn;
    r[C_UAL][0] += -0.9f * fn; r[C_UAR][0] += -0.9f * fn; r[C_LAL][0] += -1.5f * fn; r[C_LAR][0] += -1.5f * fn;
    fl += 0.3f * ch; fr += 0.3f * ch;
    for (int b = 1; b < C_FL; b++) { t3_quat q; euler(r[b], &q); m->b[b]->quaternion = q; }
    { t3_quat q; float a[3] = { 0, 0, -1.4f * fl }; euler(a, &q); m->b[C_FL]->quaternion = q; for (int k = 0; k < 3; k++) if (m->fing[0][k]) m->fing[0][k]->quaternion = q;
      float b2[3] = { 0, 0, 1.4f * fr }; euler(b2, &q); m->b[C_FR]->quaternion = q; for (int k = 0; k < 3; k++) if (m->fing[1][k]) m->fing[1][k]->quaternion = q; }
    float bounce = 0.05f * ch * (0.5f + 0.5f * (float)cw_sin(t * m->rate + ph * 5));
    m->b[C_HIPS]->position = (t3_vec3){ m->hips.x + 0.02f * sway, m->hips.y + bounce, m->hips.z - 0.05f * fn };
    /* turn toward the fight (the model faces +z) */
    float yaw = (float)atan2((double)(cx - m->x), (double)(0.0f - m->z)), sy, cy;
    cw_sincos(yaw * 0.5f, &sy, &cy);
    cw_node3d_set_quaternion(m->node, (t3_quat){ 0, sy, 0, cy });
  }
  /* the check: how many spectators throw their arms up (cheer passing half) on one and the same frame */
  int same = 0;
  for (int i = 0; i < c->n; i++) { crowd_member *m = &c->m[i]; if (m->cheer >= 0.5f && m->was_cheer < 0.5f) same++; m->was_cheer = m->cheer; }
  if (same > crowd_same_max) crowd_same_max = same;
}
