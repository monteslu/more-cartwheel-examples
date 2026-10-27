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

enum { K_FLOOR, K_WALL, K_ACCENT, K_METAL, K_CRATE, K_LEAF, K_KINDS };
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


/* ── props: pieces of assets/models/props.glb (tools/blender/props.py), named "<prop>__<material>", merged into the level meshes ── */
typedef struct prop_piece { char prop[24]; int kind; float rgb[3]; float uv; t3_geometry *geo; } prop_piece;
static prop_piece PP[96];
static int npp;
static t3_gltf *PROPS;

static int kind_of_material(const char *m, float *uv) {
  *uv = 0.5f;
  if (!strcmp(m, "crate") || !strcmp(m, "crate_frame")) return K_CRATE;
  if (!strcmp(m, "hedge")) return K_LEAF;
  if (!strcmp(m, "concrete")) { *uv = 0.25f; return K_WALL; }
  if (!strcmp(m, "paint")) { *uv = 0.25f; return K_ACCENT; }
  return K_METAL;
}

static void props_scan(t3_object *o) {
  if (o->type == T3_MESH && o->name && strstr(o->name, "__") && npp < (int)(sizeof PP / sizeof *PP)) {
    t3_mesh *me = (t3_mesh *)o;
    prop_piece *p = &PP[npp++];
    const char *sep = strstr(o->name, "__");
    size_t n = (size_t)(sep - o->name);
    if (n >= sizeof p->prop) n = sizeof p->prop - 1;
    memcpy(p->prop, o->name, n), p->prop[n] = 0;
    char matname[32];
    snprintf(matname, sizeof matname, "%s", sep + 2);
    char *dot = strchr(matname, '.');
    if (dot) *dot = 0;
    p->kind = kind_of_material(matname, &p->uv);
    t3_color c = me->materials[0]->color;
    p->rgb[0] = c.r, p->rgb[1] = c.g, p->rgb[2] = c.b;
    p->geo = me->geometry;
  }
  for (int i = 0; i < o->child_count; i++) props_scan(o->children[i]);
}

static void props_load(void) {
  if (PROPS) return;
  size_t len;
  void *glb = cw_asset_load("models/props.glb", &len);
  if (!glb) { cw_error("arenafps: models/props.glb missing"); return; }
  PROPS = t3_gltf_parse(glb, len, NULL, NULL);
  free(glb);
  if (PROPS) props_scan(PROPS->scene);
}

/* put a prop (visual only) at x, y0 (its base), z turned by yaw and scaled */
static void prop(const char *name, float x, float y0, float z, float yaw, float sx, float sy, float sz) {
  for (int i = 0; i < npp; i++) {
    prop_piece *p = &PP[i];
    if (strcmp(p->prop, name)) continue;
    const float *rgb = p->kind == K_WALL ? TINT[K_WALL] : p->kind == K_ACCENT ? TINT[K_ACCENT] : p->rgb;
    mb_add_geo(&MB[p->kind], p->geo, v3(x, y0, z), yaw, v3(sx, sy, sz), rgb, p->uv);
  }
}
/* the same, with a collision box (hx, hy, hz full size, standing on y0) so shots, movement and the navmesh agree */
static void solid_prop(const char *name, float x, float y0, float z, float yaw, float sx, float sy, float sz, float hx, float hy, float hz) {
  prop(name, x, y0, z, yaw, sx, sy, sz);
  float sn, cs;
  cw_sincos(yaw * 0.5f, &sn, &cs);
  t3_quat q = { 0, sn, 0, cs };
  body_box(v3(x, y0 + hy / 2, z), v3(hx, hy, hz), q);
  grow(v3(x - hx / 2, y0, z - hz / 2), v3(x + hx / 2, y0 + hy, z + hz / 2));
}

/* a plain visual box in a kind (trim, skirting, cornices): no collision */
static void trim(float x, float y0, float z, float sx, float sy, float sz, int kind, float yaw) {
  float sn, cs;
  cw_sincos(yaw * 0.5f, &sn, &cs);
  mb_box(&MB[kind], v3(x, y0 + sy / 2, z), (t3_quat){ 0, sn, 0, cs }, v3(sx, sy, sz), kind == K_METAL ? 2.0f : 4.0f, TINT[kind]);
}

/* an accent light: coloured, short range, cheap (no shadow) */
static void accent_light(float x, float y, float z, float r, float g, float b, float energy, float range) {
  cw_node l = cw_node_new("OmniLight3D", "Accent");
  cw_node_set(l, "color", cw_val_color(r, g, b, 1));
  cw_node_set(l, "energy", cw_val_float(energy));
  cw_node_set(l, "range", cw_val_float(range));
  cw_node3d_set_position(l, v3(x, y, z));
  cw_node_add_child(A.root, l);
}

/* a wall floodlight: bracket and hood from props, a glowing lens and a light; dir yaw faces into the arena */
static void floodlight(float x, float y, float z, float yaw, cw_color c, float energy) {
  prop("lamp", x, y, z, yaw, 1, 1, 1);
  float sn, cs;
  cw_sincos(yaw, &sn, &cs);
  float fx = -sn * 0.68f, fz = -cs * 0.68f; /* the lens sits 0.68 m out along the lamp's front */
  cw_node lens = mesh_box(v3(x + fx, y + 0.1f, z + fz), v3(0.36f, 0.18f, 0.02f), c, (t3_quat){ 0, sinf(yaw * 0.5f), 0, cosf(yaw * 0.5f) }, 0.3f, false);
  cw_node_set(lens, "emissive", cw_val_color(c.r * 8, c.g * 8, c.b * 8, 1));
  accent_light(x + fx * 1.5f, y - 0.2f, z + fz * 1.5f, c.r, c.g, c.b, energy, 15);
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


/* ── dressing: wall skirting, cornices and pilasters, then each arena's own props ── */
static void wall_dress(float half, float h, int pilaster_every) {
  const float PI = 3.14159265f;
  for (int w = 0; w < 4; w++) {
    float yaw = w == 0 ? PI : w == 1 ? 0 : w == 2 ? -PI / 2 : PI / 2; /* the wall's face turned into the arena */
    float sn, cs;
    cw_sincos(yaw, &sn, &cs);
    float nx = -sn, nz = -cs;             /* into the arena */
    float cx = -nx * half, cz = -nz * half; /* the wall's inner face centre */
    float tx = cs, tz = -sn;               /* along the wall */
    trim(cx + nx * 0.08f, 0, cz + nz * 0.08f, 2 * half, 0.4f, 0.16f, K_METAL, yaw);
    trim(cx + nx * 0.14f, h - 0.5f, cz + nz * 0.14f, 2 * half, 0.5f, 0.28f, K_METAL, yaw);
    trim(cx + nx * 0.05f, h * 0.55f, cz + nz * 0.05f, 2 * half, 0.1f, 0.1f, K_METAL, yaw);
    for (float s = -half + pilaster_every * 0.5f; s < half; s += (float)pilaster_every)
      trim(cx + tx * s + nx * 0.15f, 0, cz + tz * s + nz * 0.15f, 0.7f, h - 0.5f, 0.3f, K_WALL, yaw);
  }
}

static void pipe_runs(float half, float y, int walls_mask) {
  const float PI = 3.14159265f;
  for (int w = 0; w < 4; w++) {
    if (!(walls_mask & (1 << w))) continue;
    float yaw = w == 0 ? PI : w == 1 ? 0 : w == 2 ? -PI / 2 : PI / 2;
    float sn, cs;
    cw_sincos(yaw, &sn, &cs);
    float nx = -sn, nz = -cs, cx = -nx * half, cz = -nz * half, tx = cs, tz = -sn;
    for (float s = -half + 1; s < half; s += 2)
      prop("pipe", cx + tx * s + nx * 0.4f, y, cz + tz * s + nz * 0.4f, yaw, 1, 1, 1);
  }
}

static void vents(float half, float y, float every) {
  const float PI = 3.14159265f;
  for (int w = 0; w < 4; w++) {
    float yaw = w == 0 ? PI : w == 1 ? 0 : w == 2 ? -PI / 2 : PI / 2;
    float sn, cs;
    cw_sincos(yaw, &sn, &cs);
    float nx = -sn, nz = -cs, cx = -nx * half, cz = -nz * half, tx = cs, tz = -sn;
    for (float s = -half + every * 0.5f; s < half; s += every) prop("vent", cx + tx * s + nx * 0.02f, y, cz + tz * s + nz * 0.02f, yaw, 1, 1, 1);
  }
}

static void crate_stack(float x, float z, float yaw, float y0) {
  solid_prop("crate", x, y0, z, yaw, 1, 1, 1, 1.0f, 1.0f, 1.0f);
  solid_prop("crate", x + 1.05f * cosf(yaw), y0, z - 1.05f * sinf(yaw), yaw + 0.1f, 1, 1, 1, 1.0f, 1.0f, 1.0f);
  solid_prop("crate_s", x + 0.3f * cosf(yaw), y0 + 1.0f, z - 0.3f * sinf(yaw), yaw + 0.5f, 1, 1, 1, 0.6f, 0.6f, 0.6f);
}
static void barrels(float x, float z, float y0) {
  solid_prop("barrel", x, y0, z, 0, 1, 1, 1, 0.6f, 0.9f, 0.6f);
  solid_prop("barrel", x + 0.65f, y0, z + 0.2f, 1.1f, 1, 1, 1, 0.6f, 0.9f, 0.6f);
  solid_prop("barrel", x + 0.3f, y0, z - 0.6f, 2.2f, 1, 1, 1, 0.6f, 0.9f, 0.6f);
}

/* ── flow: gameplay geometry (spawn nooks) ──
 * A spawn nook closes a d x d corner of an arena of half side `half` with two walls `h` high; its 3 m door opens along the
 * outer wall, so the hall cannot see in, and its two spawn points sit away from the door. Spawning in the open let a fighter be
 * seen (and shot) the moment it appeared: tools/pacing.mjs measured 70 to 98 percent of spawns in sight of an enemy. door_x
 * picks which outer wall the door runs along. */
static void spawn_nook(float sx, float sz, float half, float d, float y0, float h, bool door_x) {
  const float T = 0.6f, DOOR = 3.0f;
  float in = half - d, len = d - DOOR;
  if (door_x) {
    block(sx * (half - d / 2), y0, sz * in, d + T, h, T, K_WALL);
    block(sx * in, y0, sz * (in + len / 2), T, h, len, K_WALL);
    spawn_at(sx * (half - 2), y0 + 0.05f, sz * (in + 2));
    spawn_at(sx * (half - 5), y0 + 0.05f, sz * (in + 1.8f));
  } else {
    block(sx * in, y0, sz * (half - d / 2), T, h, d + T, K_WALL);
    block(sx * (in + len / 2), y0, sz * in, len, h, T, K_WALL);
    spawn_at(sx * (in + 2), y0 + 0.05f, sz * (half - 2));
    spawn_at(sx * (in + 1.8f), y0 + 0.05f, sz * (half - 5));
  }
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
  wall_dress(H, 9, 7);
  pipe_runs(H, 6.2f, 0xF);
  pipe_runs(H, 7.0f, 0x3);
  vents(H, 3.6f, 14);
  for (int i = 0; i < 4; i++) {
    float sx = (i & 1) ? 1 : -1, sz = (i & 2) ? 1 : -1;
    crate_stack(sx * 10, sz * 24.5f, 0.2f * sx, 0);
    crate_stack(sx * 24.5f, sz * 10, 1.4f, 0);
    barrels(sx * 26, sz * 5.5f, 0);
    solid_prop("jersey", sx * 9, 0, sz * 15.5f, 0, 1, 1, 1, 2.0f, 0.8f, 0.6f);
    solid_prop("jersey", sx * 15.5f, 0, sz * 9, 1.5708f, 1, 1, 1, 0.6f, 0.8f, 2.0f);
  }
  floodlight(0, 7.6f, -H + 0.2f, 3.14159265f, p->accent, 120);
  floodlight(0, 7.6f, H - 0.2f, 0, p->accent, 120);
  floodlight(-H + 0.2f, 7.6f, 0, -1.5708f, p->accent, 120);
  floodlight(H - 0.2f, 7.6f, 0, 1.5708f, p->accent, 120);
  accent_light(0, 6.5f, 0, 0.5f, 0.75f, 1.0f, 70, 22);
  solid_prop("console", 0, 3.0f, -5.5f, 0, 1, 1, 1, 0.9f, 1.1f, 0.5f);
  solid_prop("console", 0, 3.0f, 5.5f, 3.14159265f, 1, 1, 1, 0.9f, 1.1f, 0.5f);
  for (int i = 0; i < 4; i++) spawn_nook((i & 1) ? 1 : -1, (i & 2) ? 1 : -1, H, 8, 0, 4.5f, i == 0 || i == 3);
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
  wall_dress(H, 8, 6);
  vents(H, 3.2f, 12);
  for (int i = 0; i < 4; i++) {
    float sx = (i & 1) ? 1 : -1, sz = (i & 2) ? 1 : -1;
    solid_prop("column", sx * 2.3f, 0.8f, sz * 2.3f, 0, 1, 1, 1, 0.9f, 3.3f, 0.9f);
    solid_prop("planter", sx * 8, 0, sz * 28.4f, 0, 1, 1, 1, 2.2f, 1.6f, 1.0f);
    solid_prop("planter", sx * 21, 0, sz * 28.4f, 0, 1, 1, 1, 2.2f, 1.6f, 1.0f);
    crate_stack(sx * 27, sz * 8, 1.6f, 0);
    barrels(sx * 22, sz * 22, 0);
    solid_prop("jersey", sx * 6.5f, 0, sz * 20, 0, 1, 1, 1, 2.0f, 0.8f, 0.6f);
  }
  floodlight(0, 6.4f, -H + 0.2f, 3.14159265f, p->accent, 90);
  floodlight(0, 6.4f, H - 0.2f, 0, p->accent, 90);
  floodlight(-H + 0.2f, 6.4f, 0, -1.5708f, p->accent, 90);
  floodlight(H - 0.2f, 6.4f, 0, 1.5708f, p->accent, 90);
  accent_light(0, 3.5f, 0, 1.0f, 0.7f, 0.35f, 55, 16);
  /* spawns inside the four buildings (their doors face the middle), far from the doorways */
  for (int i = 0; i < 4; i++) {
    float sx = (i & 1) ? 1 : -1, sz = (i & 2) ? 1 : -1;
    spawn_at(sx * 18, 0.05f, sz * 18);
    if (i == 0 || i == 3) spawn_at(sx * 18, 0.05f, sz * 10.5f);
    else spawn_at(sx * 10.5f, 0.05f, sz * 18);
  }
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
  /* water over the pit floor: it ripples and reflects, shallow enough to wade (visual only) */
  A.water = cw_node_new("Water3D", "Pit");
  cw_node_set(A.water, "size", cw_val_vec2(48, 48));
  cw_node_set(A.water, "subdivisions", cw_val_int(64));
  cw_node_set(A.water, "color", cw_val_color(0.12f, 0.35f, 0.4f, 1));
  cw_node_set(A.water, "deep_color", cw_val_color(0.03f, 0.12f, 0.16f, 1));
  cw_node_set(A.water, "murk", cw_val_float(0.55));
  cw_node3d_set_position(A.water, v3(0, 0.3f, 0));
  cw_node_add_child(A.root, A.water);
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
  wall_dress(H, 10, 8);
  pipe_runs(H, 7.0f, 0xF);
  vents(H, 5.0f, 13);
  for (int i = 0; i < 4; i++) {
    float sx = (i & 1) ? 1 : -1, sz = (i & 2) ? 1 : -1;
    crate_stack(sx * 11, sz * 24, 0.3f, t2);
    barrels(sx * 24, sz * 12, t2);
    solid_prop("planter", sx * 14.2f, t2, sz * 24, 0, 1, 1, 1, 2.2f, 1.6f, 1.0f); /* clear of the spawn nook's door */
    solid_prop("jersey", sx * 6, t1, sz * 15.5f + sz * 0.0f, 0, 1, 1, 1, 2.0f, 0.8f, 0.6f);
  }
  floodlight(0, 8.2f, -H + 0.2f, 3.14159265f, p->accent, 130);
  floodlight(0, 8.2f, H - 0.2f, 0, p->accent, 130);
  floodlight(-H + 0.2f, 8.2f, 0, -1.5708f, p->accent, 130);
  floodlight(H - 0.2f, 8.2f, 0, 1.5708f, p->accent, 130);
  accent_light(0, 2.4f, 0, 0.2f, 0.85f, 0.8f, 60, 18);
  for (int i = 0; i < 4; i++) spawn_nook((i & 1) ? 1 : -1, (i & 2) ? 1 : -1, H, 7, t2, 3.6f, true);
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
  props_load();
  const cw_color metal = { 0.55f, 0.56f, 0.6f, 1 }, wood = { 0.8f, 0.7f, 0.55f, 1 }, leaf = { 0.35f, 0.55f, 0.3f, 1 };
  const cw_color *kc[K_KINDS] = { &p->floor, &p->wall, &p->accent, &metal, &wood, &leaf };
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
    static const int TEXK[ARENAS][K_KINDS] = { { TEX_CONCRETE, TEX_CONCRETE, TEX_PAINT, TEX_STEEL, TEX_WOOD, TEX_GRASS }, { TEX_GRAVEL, TEX_CONCRETE, TEX_WOOD, TEX_STEEL, TEX_WOOD, TEX_GRASS }, { TEX_GRASS, TEX_CONCRETE, TEX_STEEL, TEX_STEEL, TEX_WOOD, TEX_GRASS } };
    static const float ROUGH[K_KINDS] = { 0.9f, 0.85f, 0.55f, 0.45f, 0.75f, 0.95f };
    for (int k = 0; k < K_KINDS; k++) {
      mesh_build *b = &MB[k];
      if (!b->nv) continue;
      t3_geometry *geo = mb_finish(b);
      t3_material *m = t3_mesh_physical_material_new(0xffffff);
      m->vertex_colors = true;
      m->roughness = ROUGH[k];
      m->metalness = k == K_METAL ? 0.8f : (k == K_ACCENT && id == 2 ? 0.7f : 0.05f);
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

/* the level's mood: sky, sun, fog, grade. Autopilot builds read ARENA_T_<name> from the environment to try values without a rebuild. */
static float tune(const char *name, float def) {
#ifdef ARENA_AUTOPILOT
  char key[48];
  snprintf(key, sizeof key, "ARENA_T_%s", name);
  const char *e = getenv(key);
  if (e) return (float)atof(e);
#else
  (void)name;
#endif
  return def;
}

typedef struct mood {
  float tod, decl, north;           /* sun position */
  float sun, sky, mie, rayleigh;    /* sun and sky energy, haze */
  float ambient;                    /* flat ambient on top of the sky's */
  float fog_density, fog_falloff, fog_scatter;
  cw_color fog;
  float ev, bloom, bloom_thresh;
  float ssr;
  const char *lut;
} mood;

static const mood MOODS[ARENAS] = {
  /* FOUNDRY: late afternoon through high windows, warm key, cool deep shadows, orange accents */
  { 15.6f, 14, 20, 3.2f, 0.55f, 1.6f, 1.0f, 0.10f, 0.012f, 0.09f, 0.7f, { 0.55f, 0.5f, 0.45f, 1 }, -0.25f, 0.5f, 1.0f, 1, "luts/foundry.cube" },
  /* COURTYARD: bright morning, long soft shadows, golden fog haze */
  { 9.4f, 20, 130, 3.4f, 0.7f, 1.2f, 1.0f, 0.12f, 0.007f, 0.07f, 0.5f, { 0.72f, 0.68f, 0.6f, 1 }, -0.2f, 0.4f, 1.0f, 0, "luts/courtyard.cube" },
  /* TERRACES: dusk, low red sun over wet teal stone */
  { 17.4f, 10, 250, 2.6f, 0.5f, 2.0f, 1.2f, 0.08f, 0.011f, 0.08f, 0.9f, { 0.4f, 0.32f, 0.42f, 1 }, -0.1f, 0.55f, 0.9f, 1, "luts/terraces.cube" },
};

void arena_mood(int id, cw_node env, cw_node sun, cw_node sky, cw_node fog, cw_node post) {
  const palette *p = &PAL[id];
  const mood *m = &MOODS[id];
  cw_node_set(env, "background", cw_val_color(p->sky_bottom.r, p->sky_bottom.g, p->sky_bottom.b, 1));
  cw_node_set(env, "sky_color", cw_val_color(p->sky_top.r, p->sky_top.g, p->sky_top.b, 1));
  cw_node_set(env, "ground_color", cw_val_color(p->floor.r * 0.5f, p->floor.g * 0.5f, p->floor.b * 0.5f, 1));
  cw_node_set(env, "ambient_energy", cw_val_float(tune("ambient", m->ambient)));
  cw_node_set(sky, "time_of_day", cw_val_float(tune("tod", m->tod)));
  cw_node_set(sky, "declination", cw_val_float(m->decl));
  cw_node_set(sky, "north", cw_val_float(m->north));
  cw_node_set(sky, "sun_energy", cw_val_float(tune("sun", m->sun)));
  cw_node_set(sky, "sky_energy", cw_val_float(tune("sky", m->sky)));
  cw_node_set(sky, "mie", cw_val_float(m->mie));
  cw_node_set(sky, "rayleigh", cw_val_float(m->rayleigh));
  cw_node_set(fog, "density", cw_val_float(tune("fog", m->fog_density)));
  cw_node_set(fog, "height_falloff", cw_val_float(m->fog_falloff));
  cw_node_set(fog, "sun_scatter", cw_val_float(m->fog_scatter));
  cw_node_set(fog, "color", cw_val_color(m->fog.r, m->fog.g, m->fog.b, 1));
  cw_node_set(fog, "volumetric", cw_val_bool(tune("shafts", 1) > 0.5f));
  cw_node_set(fog, "volumetric_distance", cw_val_float(48));
  cw_node_set(post, "exposure_ev", cw_val_float(tune("ev", m->ev)));
  cw_node_set(post, "bloom_intensity", cw_val_float(tune("bloom", m->bloom)));
  cw_node_set(post, "bloom_threshold", cw_val_float(m->bloom_thresh));
  cw_node_set(post, "ssr", cw_val_bool(m->ssr != 0 && tune("ssr", 1) > 0.5f));
  cw_node_set(post, "grade", cw_val_bool(tune("grade", 1) > 0.5f));
  cw_node_set(post, "lut", cw_val_string(m->lut));
  (void)sun;
}
