/* The arenas: static geometry as Box3D boxes (collision, shots and the
 * navmesh all read the same bodies) with a MeshInstance3D per box for
 * drawing, spawn points and pickups. Three layouts, each its own palette. */
#include "game.h"

arena A;

const arena_info ARENA_INFO[ARENAS] = {
  { "FOUNDRY", "a raised deck, four pillars, hard cover" },
  { "COURTYARD", "walls and doorways: corners decide it" },
  { "TERRACES", "stepped levels and a wet pit" },
};

typedef struct palette {
  cw_color floor, wall, trim, accent, sky_top, sky_bottom;
  float sun, ambient;
  t3_vec3 sun_rot;
  cw_color sun_color;
} palette;

static palette PAL[ARENAS] = {
  { { 0.30f, 0.30f, 0.32f, 1 }, { 0.46f, 0.44f, 0.42f, 1 }, { 1.0f, 0.45f, 0.1f, 1 }, { 0.65f, 0.3f, 0.12f, 1 },
    { 0.2f, 0.34f, 0.6f, 1 }, { 0.45f, 0.5f, 0.6f, 1 }, 4.0f, 0.5f, { -0.9f, 0.6f, 0 }, { 1, 0.92f, 0.8f, 1 } },
  { { 0.42f, 0.38f, 0.32f, 1 }, { 0.62f, 0.5f, 0.38f, 1 }, { 0.1f, 0.8f, 0.9f, 1 }, { 0.3f, 0.45f, 0.35f, 1 },
    { 0.3f, 0.45f, 0.75f, 1 }, { 0.7f, 0.72f, 0.75f, 1 }, 4.5f, 0.55f, { -1.0f, -0.5f, 0 }, { 1, 0.95f, 0.85f, 1 } },
  { { 0.25f, 0.33f, 0.28f, 1 }, { 0.4f, 0.42f, 0.46f, 1 }, { 0.9f, 0.9f, 0.2f, 1 }, { 0.2f, 0.4f, 0.5f, 1 },
    { 0.12f, 0.15f, 0.3f, 1 }, { 0.4f, 0.3f, 0.35f, 1 }, 2.6f, 0.45f, { -0.55f, 0.9f, 0 }, { 1, 0.6f, 0.4f, 1 } },
};

static cw_node mesh_box(t3_vec3 center, t3_vec3 size, cw_color c, t3_quat q, float rough, bool shadow) {
  cw_node n = cw_node_new("MeshInstance3D", "Box");
  cw_node_set(n, "primitive", cw_val_enum("BOX"));
  cw_node_set(n, "size", cw_val_vec3(size.x, size.y, size.z));
  cw_node_set(n, "color", cw_val_color(c.r, c.g, c.b, 1));
  cw_node_set(n, "roughness", cw_val_float(rough));
  cw_node_set(n, "cast_shadow", cw_val_bool(shadow));
  cw_node_set(n, "receive_shadow", cw_val_bool(true));
  cw_node3d_set_position(n, center);
  cw_node3d_set_quaternion(n, q);
  cw_node_add_child(A.root, n);
  return n;
}

static void body_box(t3_vec3 center, t3_vec3 size, t3_quat q) {
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  bd.position = center;
  bd.rotation = q;
  cw_body3d b = cw_body3d_create(A.world, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  cw_shape3d_box(b, &sd, v3(size.x / 2, size.y / 2, size.z / 2), cw_xform3_identity());
}

enum { K_FLOOR, K_WALL, K_ACCENT, K_KINDS };
static mesh_build MB[K_KINDS];
static float TINT[K_KINDS][3];
static const t3_quat QI = { 0, 0, 0, 1 };

static void grow(t3_vec3 lo, t3_vec3 hi) {
  if (lo.x < A.bounds_min.x) A.bounds_min.x = lo.x;
  if (lo.y < A.bounds_min.y) A.bounds_min.y = lo.y;
  if (lo.z < A.bounds_min.z) A.bounds_min.z = lo.z;
  if (hi.x > A.bounds_max.x) A.bounds_max.x = hi.x;
  if (hi.y > A.bounds_max.y) A.bounds_max.y = hi.y;
  if (hi.z > A.bounds_max.z) A.bounds_max.z = hi.z;
}

/* a block with its bottom at y0, x/z centre, size sx sy sz */
static void block(float x, float y0, float z, float sx, float sy, float sz, int kind) {
  t3_vec3 ctr = v3(x, y0 + sy / 2, z), sz3 = v3(sx, sy, sz);
  body_box(ctr, sz3, QI);
  mb_box(&MB[kind], ctr, QI, sz3, 4.0f, TINT[kind]);
  grow(v3(x - sx / 2, y0, z - sz / 2), v3(x + sx / 2, y0 + sy, z + sz / 2));
}

/* a glowing strip, drawn only */
static void strip(float x, float y, float z, float sx, float sy, float sz, cw_color c, float glow) {
  cw_node n = mesh_box(v3(x, y, z), v3(sx, sy, sz), c, QI, 0.4f, false);
  cw_node_set(n, "emissive", cw_val_color(c.r * glow, c.g * glow, c.b * glow, 1));
}

/* a ramp from (x, y0, z) rising `rise` over `len` metres along dir (0 +x, 1 -x, 2 +z, 3 -z), width w */
static void ramp(float x, float y0, float z, float len, float rise, float w, int dir, int kind) {
  float a = atan2f(rise, len), l = sqrtf(len * len + rise * rise), th = 0.4f;
  float sn, cs;
  cw_sincos(a * 0.5f, &sn, &cs);
  float ca, sa;
  cw_sincos(a, &sa, &ca);
  t3_quat q;
  t3_vec3 size, ctr;
  float mx = len * 0.5f, my = rise * 0.5f;
  switch (dir) {
    case 0: q = (t3_quat){ 0, 0, sn, cs }; size = v3(l, th, w); ctr = v3(x + mx + sa * th / 2, y0 + my - ca * th / 2, z); break;
    case 1: q = (t3_quat){ 0, 0, -sn, cs }; size = v3(l, th, w); ctr = v3(x - mx - sa * th / 2, y0 + my - ca * th / 2, z); break;
    case 2: q = (t3_quat){ -sn, 0, 0, cs }; size = v3(w, th, l); ctr = v3(x, y0 + my - ca * th / 2, z + mx + sa * th / 2); break;
    default: q = (t3_quat){ sn, 0, 0, cs }; size = v3(w, th, l); ctr = v3(x, y0 + my - ca * th / 2, z - mx - sa * th / 2); break;
  }
  body_box(ctr, size, q);
  mb_box(&MB[kind], ctr, q, size, 4.0f, TINT[kind]);
}

static void spawn_at(float x, float y, float z) {
  if (A.nspawns < MAX_SPAWNS) A.spawns[A.nspawns++] = v3(x, y, z);
}
static void item_at(int kind, float x, float y, float z) {
  if (A.nitems < MAX_ITEMS) A.items[A.nitems++] = (item){ .kind = kind, .pos = v3(x, y, z) };
}

/* the walls round the arena, half = half the side */
static void shell_walls(float half, float h, int wall, int floor, cw_color trim) {
  block(0, -1, 0, half * 2 + 2, 1, half * 2 + 2, floor);
  block(0, 0, -half - 0.5f, half * 2 + 2, h, 1, wall);
  block(0, 0, half + 0.5f, half * 2 + 2, h, 1, wall);
  block(-half - 0.5f, 0, 0, 1, h, half * 2, wall);
  block(half + 0.5f, 0, 0, 1, h, half * 2, wall);
  strip(0, 2.2f, -half + 0.02f, half * 2 - 2, 0.12f, 0.05f, trim, 2.5f);
  strip(0, 2.2f, half - 0.02f, half * 2 - 2, 0.12f, 0.05f, trim, 2.5f);
  strip(-half + 0.02f, 2.2f, 0, 0.05f, 0.12f, half * 2 - 2, trim, 2.5f);
  strip(half - 0.02f, 2.2f, 0, 0.05f, 0.12f, half * 2 - 2, trim, 2.5f);
}

static void level_foundry(const palette *p) {
  const float H = 28;
  shell_walls(H, 9, K_WALL, K_FLOOR, p->trim);
  /* the deck, its four ramps, glowing edge */
  block(0, 0, 0, 14, 3, 14, K_ACCENT);
  strip(0, 3.02f, 7.02f, 14, 0.06f, 0.06f, p->trim, 3);
  strip(0, 3.02f, -7.02f, 14, 0.06f, 0.06f, p->trim, 3);
  ramp(7, 0, 0, 7, 3, 5, 0, K_FLOOR);
  ramp(-7, 0, 0, 7, 3, 5, 1, K_FLOOR);
  ramp(0, 0, 7, 7, 3, 5, 2, K_FLOOR);
  ramp(0, 0, -7, 7, 3, 5, 3, K_FLOOR);
  /* pillars */
  for (int i = 0; i < 4; i++) {
    float sx = (i & 1) ? 1 : -1, sz = (i & 2) ? 1 : -1;
    block(sx * 18, 0, sz * 18, 2.4f, 9, 2.4f, K_WALL);
    strip(sx * 18, 4.5f, sz * 18 + sz * 1.22f, 1.6f, 6, 0.04f, p->trim, 1.5f);
    block(sx * 18, 0, sz * 8.5f, 3, 1.2f, 3, K_WALL);
  }
  /* cover along the lanes */
  for (int i = 0; i < 4; i++) {
    float s = (i & 1) ? 1 : -1;
    if (i < 2) block(s * 20, 0, 0, 2, 1.3f, 5, K_ACCENT);
    else block(0, 0, s * 20, 5, 1.3f, 2, K_ACCENT);
  }
  block(-12, 0, 12, 4, 0.9f, 2, K_WALL);
  block(12, 0, -12, 4, 0.9f, 2, K_WALL);
  block(12, 0, 12, 2, 1.1f, 4, K_WALL);
  block(-12, 0, -12, 2, 1.1f, 4, K_WALL);
  static const float sp[8][2] = { { -24, -24 }, { 24, -24 }, { -24, 24 }, { 24, 24 }, { 0, -24 }, { 0, 24 }, { -24, 0 }, { 24, 0 } };
  for (int i = 0; i < 8; i++) spawn_at(sp[i][0], 0.05f, sp[i][1]);
  spawn_at(0, 3.05f, 0);
  item_at(ITEM_ROCKET, 0, 3.4f, 0);
  item_at(ITEM_RAIL, 0, 0.5f, -22);
  item_at(ITEM_SCATTER, 0, 0.5f, 22);
  item_at(ITEM_ARMOR, -22, 0.5f, 0);
  item_at(ITEM_ARMOR, 22, 0.5f, 0);
  item_at(ITEM_HEALTH, -12, 0.5f, 0);
  item_at(ITEM_HEALTH, 12, 0.5f, 0);
  item_at(ITEM_HEALTH, 0, 0.5f, 12);
  item_at(ITEM_HEALTH, 0, 0.5f, -12);
  item_at(ITEM_AMMO, -17, 1.8f, -8);
  item_at(ITEM_AMMO, 17, 1.8f, 8);
}

static void level_courtyard(const palette *p) {
  const float H = 30;
  shell_walls(H, 8, K_WALL, K_FLOOR, p->trim);
  /* four quadrant buildings with doorways, flat roofs reached by ramps */
  for (int i = 0; i < 4; i++) {
    float sx = (i & 1) ? 1 : -1, sz = (i & 2) ? 1 : -1;
    float cx = sx * 14, cz = sz * 14;
    /* walls of a 12 x 12 building, 4 high, doors on the sides facing the middle */
    /* four walls, a doorway in the two that face the middle */
    float dz = cz - sz * 6, oz = cz + sz * 6, dx = cx - sx * 6, ox = cx + sx * 6;
    block(cx - 3.75f, 0, dz, 4.5f, 4, 0.6f, K_WALL);
    block(cx + 3.75f, 0, dz, 4.5f, 4, 0.6f, K_WALL);
    block(cx, 3, dz, 3, 1, 0.6f, K_WALL);
    block(cx, 0, oz, 12.6f, 4, 0.6f, K_WALL);
    block(dx, 0, cz - 3.75f, 0.6f, 4, 4.5f, K_WALL);
    block(dx, 0, cz + 3.75f, 0.6f, 4, 4.5f, K_WALL);
    block(dx, 3, cz, 0.6f, 1, 3, K_WALL);
    block(ox, 0, cz, 0.6f, 4, 12.6f, K_WALL);
    /* carve doors by laying the cut walls out of pieces instead: a roof with a hole */
    block(cx, 4, cz, 12.6f, 0.4f, 12.6f, K_ACCENT);
    strip(cx, 4.45f, cz - 6.3f, 12.6f, 0.06f, 0.06f, p->trim, 3);
    ramp(ox + sx * 9.3f, 0, cz - sz * 2, 9, 4.4f, 3, sx > 0 ? 1 : 0, K_FLOOR);
  }
  /* the middle: a low plinth and four hedges of cover */
  block(0, 0, 0, 6, 0.8f, 6, K_ACCENT);
  block(0, 0.8f, 0, 2, 1.2f, 2, K_WALL);
  for (int i = 0; i < 4; i++) {
    float a = i * 1.5708f + 0.785f, s, c;
    cw_sincos(a, &s, &c);
    block(s * 8, 0, c * 8, 2.5f, 1.2f, 2.5f, K_WALL);
  }
  static const float sp[8][2] = { { -27, -27 }, { 27, -27 }, { -27, 27 }, { 27, 27 }, { 0, -27 }, { 0, 27 }, { -27, 0 }, { 27, 0 } };
  for (int i = 0; i < 8; i++) spawn_at(sp[i][0], 0.05f, sp[i][1]);
  item_at(ITEM_ROCKET, 0, 2.2f, 0);
  item_at(ITEM_RAIL, -14, 4.9f, -14);
  item_at(ITEM_SCATTER, 14, 4.9f, 14);
  item_at(ITEM_SCATTER, -14, 0.5f, 14);
  item_at(ITEM_ARMOR, 14, 0.5f, -14);
  item_at(ITEM_HEALTH, 0, 0.5f, -8);
  item_at(ITEM_HEALTH, 0, 0.5f, 8);
  item_at(ITEM_HEALTH, -8, 0.5f, 0);
  item_at(ITEM_HEALTH, 8, 0.5f, 0);
  item_at(ITEM_AMMO, 22, 0.5f, 0);
  item_at(ITEM_AMMO, -22, 0.5f, 0);
}

static void level_terraces(const palette *p) {
  const float H = 26;
  shell_walls(H, 10, K_WALL, K_FLOOR, p->trim);
  /* a stepped bowl: the middle is a pit (water in the picture), levels climb to the rim */
  block(0, 0, 0, 52, 0.01f, 52, K_FLOOR);
  /* ring terraces at 1.2 and 2.4 m */
  float t1 = 1.2f, t2 = 2.4f;
  block(0, 0, -21, 52, t2, 10, K_WALL);
  block(0, 0, 21, 52, t2, 10, K_WALL);
  block(-21, 0, 0, 10, t2, 32, K_WALL);
  block(21, 0, 0, 10, t2, 32, K_WALL);
  block(0, 0, -15.5f, 42, t1, 1.0f, K_ACCENT);
  block(0, 0, 15.5f, 42, t1, 1.0f, K_ACCENT);
  block(-15.5f, 0, 0, 1.0f, t1, 30, K_ACCENT);
  block(15.5f, 0, 0, 1.0f, t1, 30, K_ACCENT);
  /* ramps down into the pit from the middle of each side */
  ramp(0, 0, -16, 7, 0, 4, 2, K_FLOOR);
  /* a bridge across the pit and four islands */
  block(0, 0.2f, 0, 4, 0.8f, 40, K_ACCENT);
  block(0, 0.2f, 0, 40, 0.8f, 4, K_ACCENT);
  block(0, 0, 0, 7, 1.8f, 7, K_WALL);
  strip(0, 1.82f, 3.52f, 7, 0.06f, 0.06f, p->trim, 3);
  for (int i = 0; i < 4; i++) {
    float sx = (i & 1) ? 1 : -1, sz = (i & 2) ? 1 : -1;
    block(sx * 9, 0, sz * 9, 4, 1.0f, 4, K_WALL);
  }
  /* stairs up to the terraces: ramps from the pit floor */
  ramp(-12, 0, -10, 7, t2, 4, 0, K_FLOOR);
  ramp(12, 0, 10, 7, t2, 4, 1, K_FLOOR);
  ramp(10, 0, -12, 7, t2, 4, 2, K_FLOOR);
  ramp(-10, 0, 12, 7, t2, 4, 3, K_FLOOR);
  static const float sp[8][2] = { { -23, -23 }, { 23, -23 }, { -23, 23 }, { 23, 23 }, { 0, -22 }, { 0, 22 }, { -22, 0 }, { 22, 0 } };
  for (int i = 0; i < 8; i++) spawn_at(sp[i][0], t2 + 0.05f, sp[i][1]);
  item_at(ITEM_ROCKET, 0, 2.3f, 0);
  item_at(ITEM_RAIL, 0, 3.0f, -22);
  item_at(ITEM_SCATTER, 9, 1.5f, 9);
  item_at(ITEM_SCATTER, -9, 1.5f, -9);
  item_at(ITEM_ARMOR, 0, 3.0f, 22);
  item_at(ITEM_HEALTH, 9, 1.5f, -9);
  item_at(ITEM_HEALTH, -9, 1.5f, 9);
  item_at(ITEM_HEALTH, -22, 3.0f, 0);
  item_at(ITEM_HEALTH, 22, 3.0f, 0);
  item_at(ITEM_AMMO, 0, 1.2f, 10);
  item_at(ITEM_AMMO, 0, 1.2f, -10);
}

static cw_color ITEM_COLOR[ITEM_KINDS] = {
  { 0.2f, 1, 0.3f, 1 }, { 0.3f, 0.6f, 1, 1 }, { 1, 0.7f, 0.2f, 1 }, { 1, 0.3f, 0.2f, 1 }, { 0.8f, 0.4f, 1, 1 }, { 1, 1, 0.4f, 1 },
};

void arena_build(int id, cw_node world_root) {
  memset(&A, 0, sizeof A);
  A.id = id;
  A.root = world_root;
  A.bounds_min = v3(1e9f, 1e9f, 1e9f);
  A.bounds_max = v3(-1e9f, -1e9f, -1e9f);
  A.kill_y = -20;
  cw_world3d_def wd = cw_world3d_default_def();
  A.world = cw_world3d_create(&wd);
  const palette *p = &PAL[id];
  memset(MB, 0, sizeof MB);
  const cw_color *kc[K_KINDS] = { &p->floor, &p->wall, &p->accent };
  for (int k = 0; k < K_KINDS; k++) {
    t3_color l = lin(kc[k]->r, kc[k]->g, kc[k]->b);
    TINT[k][0] = l.r, TINT[k][1] = l.g, TINT[k][2] = l.b;
  }
  switch (id) {
    case 0: level_foundry(p); break;
    case 1: level_courtyard(p); break;
    default: level_terraces(p); break;
  }
  {
    static const int TEXK[ARENAS][K_KINDS] = { { TEX_CONCRETE, TEX_CONCRETE, TEX_PAINT }, { TEX_GRAVEL, TEX_CONCRETE, TEX_WOOD }, { TEX_GRASS, TEX_CONCRETE, TEX_STEEL } };
    static const float ROUGH[K_KINDS] = { 0.9f, 0.85f, 0.55f };
    for (int k = 0; k < K_KINDS; k++) {
      mesh_build *b = &MB[k];
      if (!b->nv) continue;
      t3_geometry *geo = mb_finish(b);
      t3_material *m = t3_mesh_physical_material_new(0xffffff);
      m->vertex_colors = true;
      m->roughness = ROUGH[k];
      m->metalness = k == K_ACCENT && id == 2 ? 0.7f : 0.05f;
      tex_apply(m, TEXK[id][k], 1, 0.45f, 1.0f);
      t3_mesh *mesh = t3_mesh_new(geo, m);
      mesh->base.cast_shadow = true;
      mesh->base.receive_shadow = true;
      cw_node n = cw_node_new("MeshInstance3D", "Level");
      cw_node_add_child(A.root, n);
      cw_mesh3d_set_procedural(n, mesh);
      t3_release(geo);
      t3_release(m);
    }
  }
  /* pickups: a spinning gem and a small coloured light each */
  for (int i = 0; i < A.nitems; i++) {
    item *it = &A.items[i];
    cw_color c = ITEM_COLOR[it->kind];
    it->node = cw_node_new("MeshInstance3D", "Item");
    cw_node_set(it->node, "primitive", cw_val_enum(it->kind == ITEM_HEALTH || it->kind == ITEM_ARMOR ? "SPHERE" : "BOX"));
    float s = it->kind >= ITEM_SCATTER && it->kind != ITEM_AMMO ? 0.55f : 0.4f;
    cw_node_set(it->node, "size", cw_val_vec3(s, s, s));
    cw_node_set(it->node, "color", cw_val_color(c.r, c.g, c.b, 1));
    cw_node_set(it->node, "emissive", cw_val_color(c.r * 1.5f, c.g * 1.5f, c.b * 1.5f, 1));
    cw_node3d_set_position(it->node, v3add(it->pos, v3(0, 0.5f, 0)));
    cw_node_add_child(A.root, it->node);
    it->light = cw_node_new("OmniLight3D", "ItemLight");
    cw_node_set(it->light, "color", cw_val_color(c.r, c.g, c.b, 1));
    cw_node_set(it->light, "energy", cw_val_float(14));
    cw_node_set(it->light, "range", cw_val_float(5));
    cw_node3d_set_position(it->light, v3add(it->pos, v3(0, 1.0f, 0)));
    cw_node_add_child(A.root, it->light);
  }
  /* the navmesh over every static shape */
  cw_nav_geom g = { 0 };
  cw_nav_geom_add_world3d(&g, A.world, NULL, CW_NAV_AREA_WALK);
  cw_nav_config nc = cw_nav_config_default();
  nc.cell_size = 0.25f;
  nc.cell_height = 0.15f;
  nc.agent_height = PLAYER_HT;
  nc.agent_radius = 0.5f;
  nc.agent_max_climb = 0.5f;
  nc.agent_max_slope = 40;
  nc.tile_size = 64;
  A.nav = cw_navmesh_build(&nc, &g);
  cw_nav_geom_free(&g);
  cw_navmesh_stats st = cw_navmesh_get_stats(A.nav);
  A.nav_build_ms = st.build_ms;
  A.nav_polys = st.polys;
  cw_navmesh_set_query_extents(A.nav, v3(2, 4, 2));
  cw_info("arena %s: %d triangles-polys, navmesh %.1f ms", ARENA_INFO[id].name, st.polys, st.build_ms);
}

void arena_free(void) {
  if (cw_navmesh_valid(A.nav)) cw_navmesh_free(A.nav);
  if (cw_world3d_valid(A.world)) cw_world3d_destroy(A.world);
  memset(&A, 0, sizeof A);
}

bool arena_ray(t3_vec3 from, t3_vec3 to, t3_vec3 *point, t3_vec3 *normal) {
  cw_ray_hit3d h;
  if (!cw_world3d_raycast(A.world, from, v3sub(to, from), NULL, &h) || !h.shape.id) return false;
  if (point) *point = h.point;
  if (normal) *normal = h.normal;
  return true;
}

/* the level's mood: sky, fog, the sun */
void arena_mood(int id, cw_node env, cw_node sun, cw_node sky, cw_node fog) {
  const palette *p = &PAL[id];
  cw_node_set(env, "background", cw_val_color(p->sky_bottom.r, p->sky_bottom.g, p->sky_bottom.b, 1));
  cw_node_set(env, "sky_color", cw_val_color(p->sky_top.r, p->sky_top.g, p->sky_top.b, 1));
  cw_node_set(env, "ground_color", cw_val_color(p->floor.r * 0.4f, p->floor.g * 0.4f, p->floor.b * 0.4f, 1));
  cw_node_set(env, "ambient_energy", cw_val_float(p->ambient * 0.3f));
  cw_node_set(env, "fog_color", cw_val_color(p->sky_bottom.r, p->sky_bottom.g, p->sky_bottom.b, 1));
  static const float TOD[ARENAS] = { 15.2f, 10.8f, 16.6f }, DECL[ARENAS] = { 12, 18, 8 }, NORTH[ARENAS] = { 20, 130, 250 };
  cw_node_set(sky, "time_of_day", cw_val_float(TOD[id]));
  cw_node_set(sky, "declination", cw_val_float(DECL[id]));
  cw_node_set(sky, "north", cw_val_float(NORTH[id]));
  cw_node_set(sky, "sun_energy", cw_val_float(p->sun * 1.1f));
  cw_node_set(sky, "sky_energy", cw_val_float(0.45));
  cw_node_set(sky, "mie", cw_val_float(id == 2 ? 2.2 : 1.3));
  cw_node_set(fog, "density", cw_val_float(id == 2 ? 0.003 : 0.0012));
  cw_node_set(fog, "color", cw_val_color(p->sky_bottom.r, p->sky_bottom.g, p->sky_bottom.b, 1));
  (void)sun;
}
