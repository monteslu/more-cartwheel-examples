/* fighter crowd: low detail spectators (models/crowd0..8.glb, tools/make-characters.mjs) standing around the stage.
 * They turn to follow the fight, shift their weight, clap when it gets going and throw their fists up on big hits,
 * supers and knockouts. Excitement comes from the simulation's events; time from its tick (the same on every host).
 * Render only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

enum { C_HIPS, C_SPINE, C_CHEST, C_HEAD, C_UAL, C_LAL, C_UAR, C_LAR, C_FL, C_FR, CB };
static const char *const CBONE[CB] = { "Hips", "Spine", "Chest", "Head", "UpperArmL", "LowerArmL", "UpperArmR", "LowerArmR", "FingersL", "FingersR" };

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
    if (!ok) { t3_release(m->g); m->g = NULL; continue; }
    m->hips = m->b[C_HIPS]->position;
    char nm[16];
    snprintf(nm, sizeof nm, "Crowd%d", i);
    m->node = cw_node_new("Node3D", nm);
    cw_node_add_child(parent, m->node);
    cw_node3d_set_object(m->node, m->g->scene);
    cw_node3d_set_position(m->node, (t3_vec3){ s->x, s->y, s->z });
    m->x = s->x; m->z = s->z;
    m->phase = (float)((i * 53) % 97) * 0.065f;
    m->thr = 0.15f + 0.07f * (float)((i * 29) % 9);
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

void crowd_frame(crowd *c, const ft_game *g, float dt) {
  if (!c->n) return;
  /* excitement: hits stir them, supers and knockouts bring everyone up */
  uint32_t n = g->ev_seq;
  if (n - c->seen > 64) c->seen = n - 64;
  for (; c->seen < n; c->seen++) {
    const ft_event *e = &g->ev[c->seen & 63];
    if (e->type == EV_HIT) c->excite += 0.1f + 0.08f * (float)e->power;
    else if (e->type == EV_THROW || e->type == EV_WALL) c->excite += 0.25f;
    else if (e->type == EV_SUPER) c->excite += 0.7f;
    else if (e->type == EV_KO) c->excite += 1.5f;
  }
  c->excite = cw_clampf(c->excite, 0, 1.5f) * cw_exp(-dt * 0.45f);
  float t = (float)g->tick / 60.0f;
  float cx = ((float)g->f[0].x + (float)g->f[1].x) * 0.0005f;
  for (int i = 0; i < c->n; i++) {
    crowd_member *m = &c->m[i];
    float ph = m->phase, sway = (float)cw_sin(t * 0.7f + ph);
    float cheer = cw_clampf((c->excite - m->thr) * 3.0f, 0, 1), clap = cw_clampf((c->excite - m->thr * 0.4f) * 4.0f, 0, 1) * (1 - cheer);
    m->cheer += (cheer - m->cheer) * (1 - cw_exp(-dt * 6.0f));
    m->clap += (clap - m->clap) * (1 - cw_exp(-dt * 5.0f));
    float r[CB][3];
    memset(r, 0, sizeof r);
    /* watching: weight on one leg, arms loose, head toward the fight */
    r[C_SPINE][2] = 0.03f * sway; r[C_CHEST][0] = 0.04f; r[C_HEAD][0] = 0.05f;
    r[C_UAL][2] = 0.12f; r[C_UAR][2] = -0.12f; r[C_LAL][0] = -0.25f; r[C_LAR][0] = -0.3f;
    float fl = 0.7f, fr = 0.7f;
    /* clapping: forearms up in front, hands meeting four times a second */
    float cl = m->clap, beat = 0.5f + 0.5f * (float)cw_sin(t * 25.0f + ph * 3);
    r[C_UAL][0] += -0.75f * cl; r[C_UAR][0] += -0.75f * cl; r[C_UAL][2] += (-0.1f - 0.25f * beat) * cl; r[C_UAR][2] -= (-0.1f - 0.25f * beat) * cl;
    r[C_LAL][0] += -1.3f * cl; r[C_LAR][0] += -1.3f * cl; fl -= 0.6f * cl; fr -= 0.6f * cl;
    /* cheering: fists up and pumping, a little bounce, chest up */
    float ch = m->cheer, pump = (float)cw_sin(t * 9.0f + ph * 5);
    bool both = (i & 1) == 0;
    r[C_UAR][0] += (-2.7f + 0.25f * pump) * ch; r[C_UAR][2] += -0.25f * ch; r[C_LAR][0] += (-0.4f - 0.3f * pump) * ch;
    if (both) { r[C_UAL][0] += (-2.7f - 0.25f * pump) * ch; r[C_UAL][2] += 0.25f * ch; r[C_LAL][0] += (-0.4f + 0.3f * pump) * ch; }
    else { r[C_UAL][0] += -0.4f * ch; r[C_LAL][0] += -1.2f * ch; }
    r[C_CHEST][0] -= 0.12f * ch; r[C_HEAD][0] -= 0.15f * ch;
    fl += 0.3f * ch; fr += 0.3f * ch;
    for (int b = 1; b < C_FL; b++) { t3_quat q; euler(r[b], &q); m->b[b]->quaternion = q; }
    { t3_quat q; float a[3] = { 0, 0, -1.4f * fl }; euler(a, &q); m->b[C_FL]->quaternion = q; float b2[3] = { 0, 0, 1.4f * fr }; euler(b2, &q); m->b[C_FR]->quaternion = q; }
    float bounce = 0.05f * ch * (0.5f + 0.5f * (float)cw_sin(t * 9.0f + ph * 5));
    m->b[C_HIPS]->position = (t3_vec3){ m->hips.x + 0.02f * sway, m->hips.y + bounce, m->hips.z };
    /* turn toward the fight (the model faces +z) */
    float yaw = (float)atan2((double)(cx - m->x), (double)(0.0f - m->z)), sy, cy;
    cw_sincos(yaw * 0.5f, &sy, &cy);
    cw_node3d_set_quaternion(m->node, (t3_quat){ 0, sy, 0, cy });
  }
}
