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

enum { K_FLOOR, K_WALL, K_ACCENT, K_METAL, K_CRATE, K_LEAF, K_CLAD, K_TRIM, K_GRATE, K_WINDOW, K_GIRDER, K_PILLAR, K_PAINT, K_LAMP, K_KINDS };
static float tune(const char *name, float def);
/* the glow behind each arena's windows (linear) and the colour of its practical lamps (sRGB) */
static const float WINDOW_GLOW[ARENAS][3] = { { 1.4f, 1.0f, 0.55f }, { 1.6f, 1.0f, 0.45f }, { 1.8f, 1.2f, 0.65f } };
static const float LAMP_RGB[ARENAS][3] = { { 1.0f, 0.62f, 0.25f }, { 1.0f, 0.7f, 0.35f }, { 1.0f, 0.75f, 0.45f } };
static mesh_build MB[K_KINDS];
static float TINT[K_KINDS][3];

/* each arena's materials per kind: texture set, metres a repeat, roughness and metalness factors (the maps carry the detail),
 * normal strength, and a tint (sRGB; the new sets carry their own colour, so most tints are white) */
typedef struct kmat { int tex; float uvm, rough, metal, normal; float tint[3]; } kmat;
static const kmat KMAT[ARENAS][K_KINDS] = {
  { /* FOUNDRY: slab floor, board-formed concrete shell, blue-grey steel cladding, tread plate deck, hazard trims, red girders */
    [K_FLOOR] = { TEX_SLAB, 4, 1, 0, 1, { 1, 1, 1 } },
    [K_WALL] = { TEX_FORMED, 4, 1, 0, 1, { 0.95f, 0.93f, 0.9f } },
    [K_ACCENT] = { TEX_TREAD, 2, 1, 0.75f, 1, { 1, 1, 1 } },
    [K_METAL] = { TEX_STEEL, 2, 0.9f, 0.85f, 1, { 0.75f, 0.76f, 0.8f } },
    [K_CRATE] = { TEX_WOOD, 4, 1, 0, 1, { 1, 0.9f, 0.75f } },
    [K_LEAF] = { TEX_GRASS, 4, 1, 0, 1, { 0.5f, 0.7f, 0.4f } },
    [K_CLAD] = { TEX_PANEL_STEEL, 4, 1, 0.1f, 1, { 1, 1, 1 } },
    [K_TRIM] = { TEX_HAZARD, 1, 1, 0.1f, 1, { 1, 1, 1 } },
    [K_GRATE] = { TEX_GRATE, 1, 1, 0.7f, 1, { 1, 1, 1 } },
    [K_WINDOW] = { TEX_WINDOWS, 3, 1, 0, 0.5f, { 0.12f, 0.12f, 0.13f } },
    [K_GIRDER] = { TEX_PANEL_RED, 4, 0.9f, 0.2f, 0.6f, { 1, 1, 1 } },
    [K_PILLAR] = { TEX_FORMED, 4, 1, 0, 1, { 0.85f, 0.85f, 0.85f } },
    [K_PAINT] = { TEX_PAINT, 1, 1, 0, 1, { 1, 1, 1 } },
    [K_LAMP] = { TEX_GLASS, 1, 1, 0, 0.3f, { 1, 1, 1 } },
  },
  { /* COURTYARD: sandstone tiles, ashlar shell, plaster houses, timber trims, slate roofs */
    [K_FLOOR] = { TEX_TILES_SAND, 4, 1, 0, 1, { 1, 1, 1 } },
    [K_WALL] = { TEX_BLOCKS, 4, 1, 0, 1, { 1, 1, 1 } },
    [K_ACCENT] = { TEX_TILES_SLATE, 4, 1, 0, 1, { 0.9f, 0.85f, 0.8f } },
    [K_METAL] = { TEX_STEEL, 2, 0.9f, 0.8f, 1, { 0.55f, 0.52f, 0.5f } },
    [K_CRATE] = { TEX_WOOD, 4, 1, 0, 1, { 1, 0.9f, 0.75f } },
    [K_LEAF] = { TEX_GRASS, 4, 1, 0, 1, { 0.45f, 0.7f, 0.35f } },
    [K_CLAD] = { TEX_PLASTER, 4, 1, 0, 1, { 1, 1, 1 } },
    [K_TRIM] = { TEX_WOOD, 2, 1, 0, 1, { 0.75f, 0.55f, 0.4f } },
    [K_GRATE] = { TEX_GRATE, 1, 1, 0.7f, 1, { 1, 1, 1 } },
    [K_WINDOW] = { TEX_WINDOWS, 2, 1, 0, 0.5f, { 0.12f, 0.12f, 0.13f } },
    [K_GIRDER] = { TEX_WOOD, 4, 1, 0, 1, { 0.6f, 0.45f, 0.35f } },
    [K_PILLAR] = { TEX_BLOCKS, 4, 1, 0, 1, { 0.95f, 0.95f, 0.95f } },
    [K_PAINT] = { TEX_PAINT, 1, 1, 0, 1, { 1, 1, 1 } },
    [K_LAMP] = { TEX_GLASS, 1, 1, 0, 0.3f, { 1, 1, 1 } },
  },
  { /* TERRACES: slate tiles, formed concrete, teal panels, hazard lips */
    [K_FLOOR] = { TEX_TILES_SLATE, 4, 1, 0, 1, { 1, 1, 1 } },
    [K_WALL] = { TEX_SLAB, 4, 1, 0, 1, { 0.92f, 0.92f, 0.95f } },
    [K_ACCENT] = { TEX_PANEL_TEAL, 4, 1, 0.1f, 1, { 1, 1, 1 } },
    [K_METAL] = { TEX_STEEL, 2, 0.9f, 0.85f, 1, { 0.7f, 0.72f, 0.75f } },
    [K_CRATE] = { TEX_WOOD, 4, 1, 0, 1, { 1, 0.9f, 0.75f } },
    [K_LEAF] = { TEX_GRASS, 4, 1, 0, 1, { 0.45f, 0.65f, 0.4f } },
    [K_CLAD] = { TEX_PANEL_TEAL, 4, 1, 0.1f, 1, { 1, 1, 1 } },
    [K_TRIM] = { TEX_HAZARD, 1, 1, 0.1f, 1, { 1, 1, 1 } },
    [K_GRATE] = { TEX_GRATE, 1, 1, 0.7f, 1, { 1, 1, 1 } },
    [K_WINDOW] = { TEX_WINDOWS, 3, 1, 0, 0.5f, { 0.12f, 0.12f, 0.13f } },
    [K_GIRDER] = { TEX_PANEL_STEEL, 4, 0.9f, 0.2f, 0.6f, { 1, 1, 1 } },
    [K_PILLAR] = { TEX_FORMED, 4, 1, 0, 1, { 0.85f, 0.85f, 0.85f } },
    [K_PAINT] = { TEX_PAINT, 1, 1, 0, 1, { 1, 1, 1 } },
    [K_LAMP] = { TEX_GLASS, 1, 1, 0, 0.3f, { 1, 1, 1 } },
  },
};
static const kmat *KM; /* this arena's row */
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
  mb_box(&MB[kind], ctr, QI, sz3, KM[kind].uvm, TINT[kind]);
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
  int n = rise > 0.5f ? (int)ceilf(rise / 0.25f) : 0;
  if (!n) {
    mb_box(&MB[kind], ctr, q, size, KM[kind].uvm, TINT[kind]);
    return;
  }
  /* drawn as a flight of steps (the collision stays the smooth ramp: the line runs through the middle of each tread),
   * hazard nosings, and steel stringers along both sides */
  float tread = len / (float)n, riser = rise / (float)n;
  float ax = dir == 0 ? 1.f : dir == 1 ? -1.f : 0, az = dir == 2 ? 1.f : dir == 3 ? -1.f : 0;
  for (int i = 0; i < n; i++) {
    float d = ((float)i + 0.5f) * tread, top = ((float)i + 0.5f) * riser;
    t3_vec3 c = v3(x + ax * d, y0 + top * 0.5f, z + az * d);
    t3_vec3 sz = ax != 0 ? v3(tread, top, w) : v3(w, top, tread);
    mb_box(&MB[kind], c, QI, sz, KM[kind].uvm, TINT[kind]);
    float e = (float)i * tread + 0.04f; /* the nosing at the step's front edge */
    t3_vec3 nc = v3(x + ax * e, y0 + top - 0.02f, z + az * e);
    t3_vec3 ns = ax != 0 ? v3(0.09f, 0.05f, w + 0.01f) : v3(w + 0.01f, 0.05f, 0.09f);
    mb_box(&MB[K_TRIM], nc, QI, ns, KM[K_TRIM].uvm, TINT[K_TRIM]);
  }
  for (int sgn = -1; sgn <= 1; sgn += 2) {
    t3_vec3 off = ax != 0 ? v3(0, 0.12f, sgn * (w / 2 + 0.04f)) : v3(sgn * (w / 2 + 0.04f), 0.12f, 0);
    t3_vec3 ss = ax != 0 ? v3(l, 0.42f, 0.08f) : v3(0.08f, 0.42f, l);
    mb_box(&MB[K_METAL], v3add(ctr, off), q, ss, KM[K_METAL].uvm, TINT[K_METAL]);
  }
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
  if (!strcmp(m, "paint") || !strcmp(m, "barrel")) { *uv = 1.0f; return K_PAINT; }
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
  mb_box(&MB[kind], v3(x, y0 + sy / 2, z), (t3_quat){ 0, sn, 0, cs }, v3(sx, sy, sz), KM[kind].uvm, TINT[kind]);
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
  strip(0, 2.2f, -half + 0.1f, half * 2 - 2, 0.08f, 0.05f, trim, 2.5f);
  strip(0, 2.2f, half - 0.1f, half * 2 - 2, 0.08f, 0.05f, trim, 2.5f);
  strip(-half + 0.1f, 2.2f, 0, 0.05f, 0.08f, half * 2 - 2, trim, 2.5f);
  strip(half - 0.1f, 2.2f, 0, 0.05f, 0.08f, half * 2 - 2, trim, 2.5f);
}


/* ── dressing: visual only (no collision), against the walls or above reach ── */

/* one wall's frame: w 0..3 (north, south, west, east); n = into the arena, t = along the wall, c = the inner face's centre */
typedef struct wall_frame { float yaw, nx, nz, tx, tz, cx, cz; } wall_frame;
static wall_frame wall_of(int w, float half) {
  const float PI = 3.14159265f;
  wall_frame f;
  f.yaw = w == 0 ? PI : w == 1 ? 0 : w == 2 ? -PI / 2 : PI / 2; /* the wall's face turned into the arena */
  float sn, cs;
  cw_sincos(f.yaw, &sn, &cs);
  f.nx = -sn, f.nz = -cs, f.tx = cs, f.tz = -sn;
  f.cx = -f.nx * half, f.cz = -f.nz * half;
  return f;
}
/* a box against a wall: s along it, out metres from the face (to its back), y0 up, length along, height, depth out */
static void on_wall(const wall_frame *f, float s, float out, float y0, float len, float hgt, float dep, int kind) {
  float o = out + dep / 2;
  trim(f->cx + f->tx * s + f->nx * o, y0, f->cz + f->tz * s + f->nz * o, len, hgt, dep, kind, f->yaw);
}

typedef struct wall_style {
  float y0;              /* where the walls' visible face starts (a terrace ring hides the foot) */
  int skirt;             /* skirting kind, -1 none */
  float clad_hi;         /* a cladding band up to here (0 none) */
  int pilaster_every;
  float win_lo, win_hi;  /* a window band (win_hi 0 none) */
  int cornice;
} wall_style;

static void wall_dress(float half, float h, const wall_style *st) {
  for (int w = 0; w < 4; w++) {
    wall_frame f = wall_of(w, half);
    if (st->clad_hi > 0) {
      on_wall(&f, 0, 0, st->y0, 2 * half, st->clad_hi - st->y0, 0.06f, K_CLAD);
      on_wall(&f, 0, 0, st->clad_hi - 0.06f, 2 * half, 0.14f, 0.14f, K_METAL); /* a cap rail on the cladding */
    }
    if (st->skirt >= 0) on_wall(&f, 0, 0, st->y0, 2 * half, 0.45f, 0.16f, st->skirt);
    on_wall(&f, 0, 0, h - 0.55f, 2 * half, 0.55f, 0.3f, st->cornice);
    on_wall(&f, 0, 0, h - 0.75f, 2 * half, 0.12f, 0.18f, st->cornice);
    float pe = (float)st->pilaster_every;
    for (float s = -half + pe * 0.5f; s < half; s += pe) {
      on_wall(&f, s, 0, st->y0, 0.8f, h - st->y0 - 0.55f, 0.32f, K_PILLAR);
      on_wall(&f, s, 0, st->y0, 1.0f, 0.6f, 0.4f, K_PILLAR); /* plinth */
      if (st->win_hi > 0 && s + pe < half) { /* a window between this pilaster and the next */
        float c = s + pe * 0.5f, ww = pe - 1.8f, wh = st->win_hi - st->win_lo;
        on_wall(&f, c, 0, st->win_lo - 0.12f, ww + 0.3f, wh + 0.24f, 0.08f, K_METAL);   /* frame */
        on_wall(&f, c, 0.03f, st->win_lo, ww, wh, 0.06f, K_WINDOW);                   /* the glazing, just proud of it */
        on_wall(&f, c, 0, st->win_lo - 0.2f, ww + 0.5f, 0.1f, 0.22f, K_METAL);       /* sill */
      }
    }
  }
}

/* lamps on the walls: a dark housing, a glowing lens and a short warm light each (clustered, no shadow) */
static void wall_lamps(float half, float y, float every, float offset, float energy, float range, int walls_mask) {
  const float *c = LAMP_RGB[A.id];
  for (int w = 0; w < 4; w++) {
    if (!(walls_mask & (1 << w))) continue;
    wall_frame f = wall_of(w, half);
    for (float s = -half + offset; s < half - 0.5f; s += every) {
      on_wall(&f, s, 0, y, 0.6f, 0.26f, 0.22f, K_METAL);
      on_wall(&f, s, 0.22f, y + 0.04f, 0.5f, 0.12f, 0.04f, K_LAMP);
      on_wall(&f, s, 0, y - 0.04f, 0.5f, 0.04f, 0.3f, K_LAMP);
      accent_light(f.cx + f.tx * s + f.nx * 0.6f, y - 0.2f, f.cz + f.tz * s + f.nz * 0.6f, c[0], c[1], c[2], energy, range);
    }
  }
}

/* a maintenance catwalk along a wall at deck height y: grating, edge channel, railings, brackets under it (all above reach) */
static void catwalk(float half, int w, float y, float depth, float from, float to) {
  wall_frame f = wall_of(w, half);
  float len = to - from, mid = (from + to) * 0.5f;
  on_wall(&f, mid, 0, y - 0.06f, len, 0.06f, depth, K_GRATE);
  on_wall(&f, mid, depth - 0.08f, y - 0.22f, len, 0.24f, 0.08f, K_METAL);      /* edge channel */
  on_wall(&f, mid, depth - 0.06f, y, len, 0.1f, 0.03f, K_METAL);                /* toe plate */
  on_wall(&f, mid, depth - 0.07f, y + 1.0f, len, 0.06f, 0.06f, K_METAL);        /* top rail */
  on_wall(&f, mid, depth - 0.06f, y + 0.5f, len, 0.04f, 0.04f, K_METAL);        /* mid rail */
  for (float s = from; s <= to + 0.01f; s += 2.0f) {
    on_wall(&f, s, depth - 0.07f, y, 0.06f, 1.0f, 0.06f, K_METAL);             /* posts */
    on_wall(&f, s, 0, y - 1.2f, 0.12f, 1.0f, 0.12f, K_GIRDER);                 /* bracket upright on the wall */
    on_wall(&f, s, 0, y - 0.28f, 0.12f, 0.22f, depth - 0.1f, K_GIRDER);        /* bracket arm */
  }
}

/* steel roof trusses over the whole arena: I-beams across x every 'every' metres, purlins along z; they throw a shadow lattice */
static void roof_girders(float half, float y, float every) {
  float span = 2 * half + 1;
  for (float z = -half + every * 0.5f; z < half; z += every) {
    trim(0, y, z, span, 0.08f, 0.4f, K_GIRDER, 0);
    trim(0, y + 0.08f, z, span, 0.74f, 0.1f, K_GIRDER, 0);
    trim(0, y + 0.82f, z, span, 0.08f, 0.4f, K_GIRDER, 0);
  }
  for (float x = -half + 2; x < half; x += 4) trim(x, y + 0.9f, 0, 0.14f, 0.22f, span, K_GIRDER, 0);
}

/* flat painted floor markings: a frame of hazard strips (half size, strip width) a hair above the floor at y */
static void floor_frame(float cx, float cz, float hx, float hz, float y, float wdt) {
  trim(cx, y, cz - hz, 2 * hx + wdt, 0.012f, wdt, K_TRIM, 0);
  trim(cx, y, cz + hz, 2 * hx + wdt, 0.012f, wdt, K_TRIM, 0);
  trim(cx - hx, y, cz, wdt, 0.012f, 2 * hz - wdt, K_TRIM, 0);
  trim(cx + hx, y, cz, wdt, 0.012f, 2 * hz - wdt, K_TRIM, 0);
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

/* lane walls: each arena's long straight lanes (round the walls, across the middle) were open end to end, so every fighter saw
 * most others most of the time (tools/pacing.mjs: an enemy in sight 60 to 90 percent of the time, an encounter every 4 to 6 s).
 * A wall across the middle of each outer lane, with a gap on the inner side, splits a lane into two. */
static void lane_walls(float half, float from, float y0, float h) {
  float len = half - from, c = from + len / 2;
  block(0, y0, -c, 0.6f, h, len, K_WALL);
  block(0, y0, c, 0.6f, h, len, K_WALL);
  block(-c, y0, 0, len, h, 0.6f, K_WALL);
  block(c, y0, 0, len, h, 0.6f, K_WALL);
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
    strip(sx * 18 - 0.75f, 4.7f, sz * 18 + sz * 1.22f, 0.12f, 6.0f, 0.04f, p->trim, 3.0f);
    strip(sx * 18 + 0.75f, 4.7f, sz * 18 + sz * 1.22f, 0.12f, 6.0f, 0.04f, p->trim, 3.0f);
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
  {
    wall_style st = { .y0 = 0, .skirt = K_TRIM, .clad_hi = 3.4f, .pilaster_every = 7, .win_lo = 4.1f, .win_hi = 5.9f, .cornice = K_METAL };
    wall_dress(H, 9, &st);
  }
  pipe_runs(H, 7.9f, 0xF);
  vents(H, 1.4f, 14);
  catwalk(H, 2, 6.4f, 1.5f, -H + 1, H - 1);
  catwalk(H, 3, 6.4f, 1.5f, -H + 1, H - 1);
  roof_girders(H, 9.0f, 8);
  wall_lamps(H, 3.6f, 7, 3.5f, 22, 9, 0xF);
  /* pillars: hazard wrapped feet, steel capitals under the trusses */
  for (int i = 0; i < 4; i++) {
    float sx = (i & 1) ? 1 : -1, sz = (i & 2) ? 1 : -1;
    trim(sx * 18, 0, sz * 18, 2.5f, 1.2f, 2.5f, K_TRIM, 0);
    trim(sx * 18, 8.3f, sz * 18, 2.9f, 0.7f, 2.9f, K_METAL, 0);
    trim(sx * 18, 0, sz * 8.5f, 3.08f, 0.12f, 3.08f, K_METAL, 0);
    trim(sx * 18, 1.14f, sz * 8.5f, 3.08f, 0.1f, 3.08f, K_METAL, 0);
  }
  /* the deck: hazard edges on top, a steel lip */
  floor_frame(0, 0, 6.85f, 6.85f, 3.0f, 0.3f);
  floor_frame(0, 0, 15.5f, 15.5f, 0, 0.35f);
  trim(0, 2.75f, 0, 14.12f, 0.25f, 14.12f, K_METAL, 0);
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
  lane_walls(H, 24, 0, 4.5f);
  /* screens round the deck (each side two, the ramp between them): the deck no longer sees the outer ring */
  for (int s = -1; s <= 1; s += 2) {
    block(-8, 0, s * 14.0f, 10, 4, 0.6f, K_WALL);
    block(8, 0, s * 14.0f, 10, 4, 0.6f, K_WALL);
    block(s * 14.0f, 0, -8, 0.6f, 4, 10, K_WALL);
    block(s * 14.0f, 0, 8, 0.6f, 4, 10, K_WALL);
  }
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
    block(cx - 3.75f, 0, dz, 4.5f, 4, 0.6f, K_CLAD);
    block(cx + 3.75f, 0, dz, 4.5f, 4, 0.6f, K_CLAD);
    block(cx, 3, dz, 3, 1, 0.6f, K_CLAD);
    block(cx, 0, oz, 12.6f, 4, 0.6f, K_CLAD);
    block(dx, 0, cz - 3.75f, 0.6f, 4, 4.5f, K_CLAD);
    block(dx, 0, cz + 3.75f, 0.6f, 4, 4.5f, K_CLAD);
    block(dx, 3, cz, 0.6f, 1, 3, K_CLAD);
    block(ox, 0, cz, 0.6f, 4, 12.6f, K_CLAD);
    /* dressing (visual only): timber door frames, a stone plinth, a cornice under the roof, lit windows on the blind walls, lanterns */
    trim(cx, 2.86f, dz, 3.3f, 0.22f, 0.76f, K_TRIM, 0);
    trim(cx - 1.58f, 0, dz, 0.16f, 3.0f, 0.72f, K_TRIM, 0);
    trim(cx + 1.58f, 0, dz, 0.16f, 3.0f, 0.72f, K_TRIM, 0);
    trim(dx, 2.86f, cz, 0.76f, 0.22f, 3.3f, K_TRIM, 0);
    trim(dx, 0, cz - 1.58f, 0.72f, 3.0f, 0.16f, K_TRIM, 0);
    trim(dx, 0, cz + 1.58f, 0.72f, 3.0f, 0.16f, K_TRIM, 0);
    trim(cx, 3.62f, cz, 13.0f, 0.38f, 13.0f, K_PILLAR, 0);
    trim(cx, 3.5f, cz, 12.86f, 0.12f, 12.86f, K_TRIM, 0);
    for (int k = -1; k <= 1; k += 2) {
      float wpos = (float)k * 3.0f;
      trim(cx + wpos, 1.1f, oz + sz * 0.31f, 1.5f, 1.5f, 0.06f, K_TRIM, 0);
      trim(cx + wpos, 1.2f, oz + sz * 0.34f, 1.2f, 1.3f, 0.04f, K_WINDOW, 0);
      trim(cx + wpos, 1.0f, oz + sz * 0.36f, 1.7f, 0.1f, 0.14f, K_PILLAR, 0);
      trim(ox + sx * 0.31f, 1.1f, cz + wpos, 0.06f, 1.5f, 1.5f, K_TRIM, 0);
      trim(ox + sx * 0.34f, 1.2f, cz + wpos, 0.04f, 1.3f, 1.2f, K_WINDOW, 0);
      trim(ox + sx * 0.36f, 1.0f, cz + wpos, 0.14f, 0.1f, 1.7f, K_PILLAR, 0);
    }
    {
      const float *lc = LAMP_RGB[A.id];
      float lz = dz - sz * 0.45f, lx = dx - sx * 0.45f;
      trim(cx + 2.2f, 2.5f, lz, 0.22f, 0.36f, 0.22f, K_LAMP, 0);
      trim(cx + 2.2f, 2.86f, lz, 0.3f, 0.06f, 0.3f, K_METAL, 0);
      accent_light(cx + 2.2f, 2.4f, lz - sz * 0.4f, lc[0], lc[1], lc[2], 18, 8);
      trim(lx, 2.5f, cz - 2.2f, 0.22f, 0.36f, 0.22f, K_LAMP, 0);
      trim(lx, 2.86f, cz - 2.2f, 0.3f, 0.06f, 0.3f, K_METAL, 0);
      accent_light(lx - sx * 0.4f, 2.4f, cz - 2.2f, lc[0], lc[1], lc[2], 18, 8);
    }
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
  {
    wall_style st = { .y0 = 0, .skirt = K_PILLAR, .clad_hi = 0, .pilaster_every = 6, .win_lo = 0, .win_hi = 0, .cornice = K_PILLAR };
    wall_dress(H, 8, &st);
  }
  vents(H, 3.2f, 12);
  wall_lamps(H, 3.4f, 12, 9, 24, 9, 0xF);
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
  lane_walls(H, 24, 0, 4);
  /* the cross of lanes between the buildings ran 60 m end to end: staggered baffles at abs 20 (past the building corners),
   * each closing two thirds of the lane, the gaps on opposite sides */
  block(-2.5f, 0, -21, 11, 4, 0.6f, K_WALL);
  block(2.5f, 0, 21, 11, 4, 0.6f, K_WALL);
  block(21, 0, -2.5f, 0.6f, 4, 11, K_WALL);
  block(-21, 0, 2.5f, 0.6f, 4, 11, K_WALL);
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
  {
    wall_style st = { .y0 = t2, .skirt = K_TRIM, .clad_hi = 5.4f, .pilaster_every = 8, .win_lo = 6.4f, .win_hi = 8.3f, .cornice = K_METAL };
    wall_dress(H, 10, &st);
  }
  pipe_runs(H, 9.0f, 0xF);
  vents(H, 3.6f, 13);
  wall_lamps(H, 5.7f, 8, 4, 26, 10, 0xF);
  /* hazard lips along the terrace edges */
  trim(0, t2, -16.15f, 52, 0.014f, 0.3f, K_TRIM, 0);
  trim(0, t2, 16.15f, 52, 0.014f, 0.3f, K_TRIM, 0);
  trim(-16.15f, t2, 0, 0.3f, 0.014f, 32, K_TRIM, 0);
  trim(16.15f, t2, 0, 0.3f, 0.014f, 32, K_TRIM, 0);
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
  lane_walls(H, 20.5f, t2, 3.6f);
  /* a parapet 1 m high along the rim's inner edge (a 5 m gap in the middle of each side to drop into the pit): the rim and the
   * pit no longer see each other end to end, only near the edge */
  for (int s = -1; s <= 1; s += 2)
    for (int k = -1; k <= 1; k += 2) {
      block(k * 9.0f, t2, s * 16.3f, 13, 1.0f, 0.4f, K_WALL);
      block(s * 16.3f, t2, k * 9.0f, 0.4f, 1.0f, 13, K_WALL);
    }
  /* in the pit, each island gets two 3.5 m walls on the sides facing the bridges: the pit splits into four rooms that see each
   * other only along the bridges */
  for (int i = 0; i < 4; i++) {
    float sx = (i & 1) ? 1 : -1, sz = (i & 2) ? 1 : -1;
    block(sx * 9, 0, sz * 4.6f, 4, 3.5f, 0.6f, K_WALL);
    block(sx * 4.6f, 0, sz * 9, 0.6f, 3.5f, 4, K_WALL);
  }
  item_at(ITEM_ROCKET, 0, 2.3f, 0);
  item_at(ITEM_RAIL, 0, 3.0f, -18.5f); /* in front of the lane wall */
  item_at(ITEM_SCATTER, 9, 1.5f, 9);
  item_at(ITEM_SCATTER, -9, 1.5f, -9);
  item_at(ITEM_ARMOR, 0, 3.0f, 18.5f);
  item_at(ITEM_HEALTH, 9, 1.5f, -9);
  item_at(ITEM_HEALTH, -9, 1.5f, 9);
  item_at(ITEM_HEALTH, -18.5f, 3.0f, 0);
  item_at(ITEM_HEALTH, 18.5f, 3.0f, 0);
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
  KM = KMAT[id];
  for (int k = 0; k < K_KINDS; k++) {
    t3_color l = lin(KM[k].tint[0], KM[k].tint[1], KM[k].tint[2]);
    TINT[k][0] = l.r, TINT[k][1] = l.g, TINT[k][2] = l.b;
  }
  mb_macro = 0.14f;
  switch (id) {
    case 0: level_foundry(p); break;
    case 1: level_courtyard(p); break;
    default: level_terraces(p); break;
  }
  {
    for (int k = 0; k < K_KINDS; k++) {
      mesh_build *b = &MB[k];
      if (!b->nv) continue;
      t3_geometry *geo = mb_finish(b);
      t3_material *m = t3_mesh_physical_material_new(0xffffff);
      m->vertex_colors = true;
      m->roughness = KM[k].rough;
      m->metalness = KM[k].metal;
      tex_apply(m, KM[k].tex, 1, KM[k].normal * tune("normal", 1), 1.0f);
      if (k == K_LAMP) {
        m->emissive = lin(LAMP_RGB[id][0], LAMP_RGB[id][1], LAMP_RGB[id][2]);
        m->emissive_intensity = 6;
      }
      if (k == K_WINDOW) { /* the pane pattern glows: the map is the emissive mask too */
        const tex_set *ts = tex_get(KM[k].tex);
        t3_material_set_texture(m, T3_EMISSIVE_MAP, ts->map);
        m->emissive = (t3_color){ WINDOW_GLOW[id][0], WINDOW_GLOW[id][1], WINDOW_GLOW[id][2] };
        m->emissive_intensity = 1;
      }
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
  /* a reflection probe over the whole arena: shiny surfaces reflect the walls instead of open sky, and (ambient) the fill light
   * comes from what the arena really looks like, darker under the walls, coloured by them, instead of a flat sky dome */
  if (tune("probe", 1) > 0.5f) {
    cw_node pr = cw_node_new("ReflectionProbe3D", "Probe");
    t3_vec3 c = v3scale(v3add(A.bounds_min, A.bounds_max), 0.5f), s = v3sub(A.bounds_max, A.bounds_min);
    cw_node3d_set_position(pr, v3(c.x, 5, c.z));
    cw_node_set(pr, "size", cw_val_vec3(s.x + 1, 16, s.z + 1));
    cw_node_set(pr, "origin_offset", cw_val_vec3(0, -1.5f, 0));
    cw_node_set(pr, "box_projection", cw_val_bool(true));
    cw_node_set(pr, "ambient", cw_val_bool(true));
    cw_node_set(pr, "intensity", cw_val_float(tune("probe_i", 1)));
    cw_node_set(pr, "blend_distance", cw_val_float(2));
    cw_node_set(pr, "far", cw_val_float(150));
    cw_node_add_child(A.root, pr);
  }
  /* clouds: a sky plane high over the arena, added over the procedural sky and faded out well before the far plane */
  if (tune("clouds", 1) > 0.01f) {
    static const float CL[ARENAS][5] = { /* coverage, tint r g b (linear), height */
      { 0.42f, 1.9f, 1.7f, 1.45f, 70 }, { 0.32f, 2.0f, 2.0f, 2.05f, 80 }, { 0.55f, 1.5f, 0.7f, 0.55f, 60 } };
    const float *cl = CL[id];
    mesh_build g = { 0 };
    const int cells = 24;
    const float R = 230;
    for (int j = 0; j <= cells; j++)
      for (int i = 0; i <= cells; i++) {
        float x = -R + 2 * R * (float)i / cells, z = -R + 2 * R * (float)j / cells, r = sqrtf(x * x + z * z);
        int vi = mb_vert(&g, v3(x, cl[4], z), v3(0, -1, 0), x / 140.0f, z / 140.0f);
        float f = 1 - fminf(1, fmaxf(0, (r - 80) / 110));
        f *= f;
        mb_color(&g, vi, f, f, f);
      }
    for (int j = 0; j < cells; j++)
      for (int i = 0; i < cells; i++) {
        int v0 = j * (cells + 1) + i;
        mb_quad(&g, v0, v0 + cells + 1, v0 + cells + 2, v0 + 1);
      }
    t3_geometry *geo = mb_finish(&g);
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->color = (t3_color){ cl[1] * tune("clouds", 1), cl[2] * tune("clouds", 1), cl[3] * tune("clouds", 1) };
    t3_texture *ct = tex_clouds(cl[0], 1200u + (uint32_t)id);
    t3_material_set_texture(m, T3_MAP, ct);
    t3_release(ct);
    m->vertex_colors = true;
    m->transparent = true;
    m->blending = T3_ADDITIVE_BLENDING;
    m->depth_write = false;
    m->fog = false;
    m->side = T3_DOUBLE_SIDE;
    t3_mesh *mesh = t3_mesh_new(geo, m);
    mesh->base.cast_shadow = false;
    mesh->base.receive_shadow = false;
    mesh->base.frustum_culled = false;
    cw_node n = cw_node_new("MeshInstance3D", "Clouds");
    cw_node_add_child(A.root, n);
    cw_mesh3d_set_procedural(n, mesh);
    t3_release(geo);
    t3_release(m);
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
  float volamb;                     /* volumetric fog: the share of fog colour scattered from everywhere (low: shafts stand out) */
  int shafts;                       /* volumetric light shafts on */
} mood;

static const mood MOODS[ARENAS] = {
  /* FOUNDRY: late afternoon sun raking through the roof trusses, warm key, cool steel shadows, sodium lamps */
  { 16.0f, 14, 20, 4.4f, 1.0f, 0.8f, 1.0f, 0.0f, 0.0025f, 0.12f, 0.45f, { 0.3f, 0.29f, 0.28f, 1 }, -0.1f, 0.3f, 1.6f, 1, "luts/foundry.cube", 0.12f, 1 },
  /* COURTYARD: clear morning, crisp shadows, warm stone against a deep blue sky */
  { 9.6f, 20, 130, 4.6f, 1.1f, 0.6f, 1.0f, 0.0f, 0.0015f, 0.1f, 0.35f, { 0.45f, 0.5f, 0.58f, 1 }, -0.15f, 0.25f, 1.8f, 0, "luts/courtyard.cube", 0.3f, 0 },
  /* TERRACES: dusk, a low red sun, violet haze, the lamps coming on over wet slate */
  { 17.5f, 10, 250, 3.6f, 1.0f, 1.2f, 1.1f, 0.0f, 0.004f, 0.1f, 0.6f, { 0.2f, 0.16f, 0.24f, 1 }, 0.1f, 0.4f, 1.4f, 1, "luts/terraces.cube", 0.15f, 1 },
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
  cw_node_set(fog, "volumetric", cw_val_bool(tune("shafts", (float)m->shafts) > 0.5f));
  cw_node_set(fog, "volumetric_distance", cw_val_float(48));
  cw_node_set(post, "exposure_ev", cw_val_float(tune("ev", m->ev)));
  cw_node_set(post, "bloom_intensity", cw_val_float(tune("bloom", m->bloom)));
  cw_node_set(post, "bloom_threshold", cw_val_float(m->bloom_thresh));
  cw_node_set(post, "ssr", cw_val_bool(m->ssr != 0 && tune("ssr", 1) > 0.5f));
  cw_node_set(post, "grade", cw_val_bool(tune("grade", 1) > 0.5f));
  cw_node_set(post, "lut", cw_val_string(m->lut));
  {
    static const char *TM[] = { "NONE", "LINEAR", "REINHARD", "CINEON", "ACES_FILMIC", "AGX", "NEUTRAL" };
    int tm = (int)tune("tm", 4); /* ACES filmic: AgX greys this palette out */
    cw_node_set(post, "tonemap", cw_val_enum(TM[tm < 0 || tm > 6 ? 5 : tm]));
  }
  cw_node_set(sky, "environment", cw_val_bool(tune("skyenv", 1) > 0.5f));
  cw_node_set(fog, "volumetric_ambient", cw_val_float(tune("volamb", m->volamb)));
  cw_node_set(sun, "shadow", cw_val_bool(tune("sunshadow", 1) > 0.5f));
  cw_node_set(sky, "background", cw_val_bool(tune("skybg", 1) > 0.5f));
  cw_node_set(post, "ssao", cw_val_bool(tune("ssao", 1) > 0.5f));
  {
    static const char *DV[] = { "NONE", "DEPTH", "NORMALS", "AO", "VELOCITY", "BLOOM" };
    int dv = (int)tune("dv", 0);
    cw_node_set(post, "debug_view", cw_val_enum(DV[dv < 0 || dv > 5 ? 0 : dv]));
  }
}
