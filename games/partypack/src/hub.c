/* The hub in 3D: a game table with a felt top, the board's tiles, the four
 * party characters as tokens, a trophy and a podium, all lit, shadowed and
 * graded by assets/scenes/hub.cwscene. main.c tells it what to show; it moves
 * the camera and animates. Looking only: nothing here changes a result. */
#include "mg.h"

#define MAXCELL 40
static struct {
  bool built;
  cw_node root, table, tile[MAXCELL], star[MAXCELL], endt[MAXCELL], tok[NP], face[NP], stalk[NP], trophy, podium;
  int mode, ncells, rows;
  float cam[6], cam_t[6];            /* position and target now / wanted */
  float cell[NP], hop[NP], lift[NP]; /* token board cell, hop height, podium lift */
  float clock;
  bool cpu[NP];
  int podium_rank[NP];
} H;

/* the board's layout (the 2D version's, in metres): a serpentine of rows */
static void cell_xz(int i, float *x, float *z) {
  const int per = 10;
  int r = i / per, c = i % per;
  if (r % 2 == 1) c = per - 1 - c;
  float gx = 130 + (float)c * 113.0f;
  float gy = H.rows <= 3 ? 250 + (float)r * 125.0f : 230 + (float)r * 105.0f;
  *x = (gx - 640) / 52.0f;
  *z = (gy - 330) / 52.0f;
}

void hub_build(int humans) {
  memset(&H, 0, sizeof H);
  H.built = true;
  H.ncells = 20;
  H.rows = 3;
  cw_node lv = n3_level();
  H.table = n3_glb(lv, "Table", "models/table.glb", true);
  n3_pos(H.table, 0, 0, 0);
  for (int i = 0; i < MAXCELL; i++) {
    H.tile[i] = n3_glb(lv, "Tile", "models/tile_plain.glb", true);
    H.star[i] = n3_glb(lv, "Star", "models/tile_star.glb", true);
    H.endt[i] = n3_glb(lv, "End", "models/tile_end.glb", true);
    cw_node v[3] = { H.tile[i], H.star[i], H.endt[i] };
    for (int k = 0; k < 3; k++) cw_node_set(v[k], "visible", cw_val_bool(false));
  }
  for (int p = 0; p < NP; p++) {
    char mp[40];
    snprintf(mp, sizeof mp, "models/blob_%d.glb", p);
    H.tok[p] = n3_glb(lv, "Token", mp, true);
    H.face[p] = n3_glb(lv, "Face", "models/face.glb", false);
    H.cpu[p] = p >= humans;
    H.stalk[p] = n3_glb(lv, "Stalk", "models/stalk.glb", true);
    cw_node_set(H.stalk[p], "visible", cw_val_bool(H.cpu[p]));
  }
  H.trophy = n3_glb(lv, "Trophy", "models/trophy.glb", true);
  H.podium = n3_glb(lv, "Podium", "models/podium.glb", true);
  cw_node_set(H.podium, "visible", cw_val_bool(false));
  hub_cells(20);
  hub_mode(0);
  for (int i = 0; i < 6; i++) H.cam[i] = H.cam_t[i];
}

/* the board's length: tiles are laid out for n cells plus the end */
void hub_cells(int n) {
  if (!H.built) return;
  H.ncells = n < MAXCELL - 1 ? n : MAXCELL - 1;
  H.rows = (H.ncells + 1 + 9) / 10;
  for (int i = 0; i < MAXCELL; i++) {
    float x, z;
    cell_xz(i, &x, &z);
    cw_node v[3] = { H.tile[i], H.star[i], H.endt[i] };
    for (int k = 0; k < 3; k++) n3_pos(v[k], x, 0.15f, z);
  }
}

/* 0 stage (title, menu, pick, trophy), 1 board, 2 podium */
void hub_mode(int mode) {
  if (!H.built) return;
  H.mode = mode;
  for (int i = 0; i < MAXCELL; i++) {
    cw_node_set(H.tile[i], "visible", cw_val_bool(false));
    cw_node_set(H.star[i], "visible", cw_val_bool(false));
    cw_node_set(H.endt[i], "visible", cw_val_bool(false));
  }
  if (mode == 1) { float c[6] = { 0, 15.5f, 10.5f, 0, 0, 0.9f }; memcpy(H.cam_t, c, sizeof c); }
  else if (mode == 2) { float c[6] = { 0, 3.2f, 8.2f, 0, 1.0f, 0 }; memcpy(H.cam_t, c, sizeof c); }
  else { float c[6] = { 0, 2.3f, 9.0f, 0, 1.1f, 0 }; memcpy(H.cam_t, c, sizeof c); }
  cw_node_set(H.podium, "visible", cw_val_bool(mode == 2));
  cw_node_set(H.trophy, "visible", cw_val_bool(mode != 1));
}

/* a board cell: 0 plain, 1 star (unclaimed), 2 the end */
void hub_cell(int i, int kind) {
  if (!H.built || i >= MAXCELL) return;
  cw_node_set(H.tile[i], "visible", cw_val_bool(H.mode == 1 && kind == 0));
  cw_node_set(H.star[i], "visible", cw_val_bool(H.mode == 1 && kind == 1));
  cw_node_set(H.endt[i], "visible", cw_val_bool(H.mode == 1 && kind == 2));
  if (kind == 2) n3_scale(H.endt[i], 1.25f, 1, 1.25f);
}
void hub_token(int p, float cell, float hop) { if (H.built) { H.cell[p] = cell; H.hop[p] = hop; } }
void hub_podium_slot(int p, int rank, float lift) { if (H.built) { H.podium_rank[p] = rank; H.lift[p] = lift; } }

cw_vec2 hub_cell_screen(int i) {
  float x, z;
  cell_xz(i, &x, &z);
  return n3_project((t3_vec3){ x, 0.45f, z });
}

void hub_update(float dt) {
  if (!H.built) return;
  H.clock += dt;
  float k = 1 - expf(-3.2f * dt);
  for (int i = 0; i < 6; i++) H.cam[i] += (H.cam_t[i] - H.cam[i]) * k;
  float sway = H.mode == 0 ? sinf(H.clock * 0.35f) * 0.6f : 0;
  n3_camera(H.cam[0] + sway, H.cam[1], H.cam[2], H.cam[3], H.cam[4], H.cam[5]);
  for (int p = 0; p < NP; p++) {
    t3_vec3 pos;
    float yaw = 0, sq = 1;
    if (H.mode == 0) { /* the four stand in a row and bob to a beat */
      float ph = H.clock * 3.2f + (float)p * 1.3f;
      float b = fabsf(sinf(ph));
      pos = (t3_vec3){ -3.3f + (float)p * 2.2f, 0.55f + b * 0.45f, 0 };
      yaw = sinf(H.clock * 0.8f + (float)p) * 0.35f;
      sq = 1 - 0.07f * (1 - b);
    } else if (H.mode == 1) {
      int c0 = (int)H.cell[p];
      if (c0 > H.ncells) c0 = H.ncells;
      int c1 = c0 + 1 > H.ncells ? H.ncells : c0 + 1;
      float f = H.cell[p] - (float)c0, x0, z0, x1, z1;
      cell_xz(c0, &x0, &z0); cell_xz(c1, &x1, &z1);
      pos = (t3_vec3){ flerp(x0, x1, f) + ((float)p - 1.5f) * 0.28f, 0.62f + H.hop[p] + fabsf(sinf(H.cell[p] * 3.14159f)) * 0.5f, flerp(z0, z1, f) + ((float)p - 1.5f) * 0.12f };
      yaw = cw_atan2(x1 - x0 + 0.001f, z1 - z0);
      if (f < 0.001f || f > 0.999f) yaw = sinf(H.clock + (float)p) * 0.3f;
    } else {
      static const float px[4] = { -1.35f, 0, 1.35f, 0 };
      static const float ph[4] = { 0.7f, 1.0f, 0.5f, 0.12f };
      int r = H.podium_rank[p];
      float hop = r == 0 ? fabsf(sinf(H.clock * 5)) * 0.6f : 0;
      float x = r == 3 ? 3.4f : px[r == 0 ? 1 : r == 1 ? 0 : 2];
      pos = (t3_vec3){ x, (r == 3 ? 0.12f : ph[r == 0 ? 1 : r == 1 ? 0 : 2]) + 0.55f + hop, 0 };
      pos.y *= H.lift[p];
      pos.y += (1 - H.lift[p]) * 7;
      yaw = r == 0 ? sinf(H.clock * 2) * 0.4f : 0;
    }
    cw_node nodes[3] = { H.tok[p], H.face[p], H.stalk[p] };
    for (int k2 = 0; k2 < 3; k2++) {
      n3_pos(nodes[k2], pos.x, pos.y, pos.z);
      n3_rot(nodes[k2], 0, yaw, 0);
      n3_scale(nodes[k2], 1 / sqrtf(sq), sq, 1 / sqrtf(sq));
    }
  }
  n3_pos(H.trophy, 0, 0.0f, -2.4f);
  n3_rot(H.trophy, 0, H.clock * 0.9f, 0);
  n3_scale(H.trophy, 1.5f, 1.5f, 1.5f);
}

cw_vec2 hub_podium_screen(int rank) {
  static const float px[4] = { 0, -1.35f, 1.35f, 3.4f }, py[4] = { 2.5f, 2.0f, 1.8f, 1.2f };
  return n3_project((t3_vec3){ px[rank & 3], py[rank & 3], 0 });
}
