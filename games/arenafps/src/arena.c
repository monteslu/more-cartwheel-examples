/* The arenas. The layout (rooms, corridors, open halls, doors, roofs, stairs, jump pads, spawns, items) is data: tools/make-levels.mjs
 * writes levels/<name>.cwscene, bakes its CSG shapes with the engine's cwcsg tool into levels/<name>_<root>.cwcsg, and writes
 * levels/<name>.lvl for what the game places itself. Here the baked meshes become the level's drawn meshes (one per surface
 * kind, with the dressing of that kind) and Box3D mesh shapes (collision, shots and the navmesh read the same triangles); the
 * outer shell and what hangs on it stay boxes. Each arena has its own palette, materials and mood. */
#include "cartwheel/csg.h"
#include "game.h"

arena A;

const arena_info ARENA_INFO[ARENAS] = {
  { "FOUNDRY", "a core hall, side halls, a ring corridor and the roofs over it all" },
  { "COURTYARD", "four houses, streets, a plaza, bridges from roof to roof" },
  { "TERRACES", "a flooded pit, ledges, tunnels and the terrace above" },
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
  float a = cw_atan2(rise, len), l = sqrtf(len * len + rise * rise), th = 0.4f; /* cw_atan2: libm differs by host, the collision must not */
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
  float sy, cy; /* cw_sincos, not libm: these are collision shapes and every host must place them alike */
  cw_sincos(yaw, &sy, &cy);
  solid_prop("crate", x + 1.05f * cy, y0, z - 1.05f * sy, yaw + 0.1f, 1, 1, 1, 1.0f, 1.0f, 1.0f);
  solid_prop("crate_s", x + 0.3f * cy, y0 + 1.0f, z - 0.3f * sy, yaw + 0.5f, 1, 1, 1, 0.6f, 0.6f, 0.6f);
}
static void barrels(float x, float z, float y0) {
  solid_prop("barrel", x, y0, z, 0, 1, 1, 1, 0.6f, 0.9f, 0.6f);
  solid_prop("barrel", x + 0.65f, y0, z + 0.2f, 1.1f, 1, 1, 1, 0.6f, 0.9f, 0.6f);
  solid_prop("barrel", x + 0.3f, y0, z - 0.6f, 2.2f, 1, 1, 1, 0.6f, 0.9f, 0.6f);
}

/* ── the layout: levels/<name>.lvl and the baked CSG meshes (tools/make-levels.mjs writes both) ── */

static int kind_named(const char *s) {
  static const char *N[K_KINDS] = { "floor", "wall", "accent", "metal", "crate", "leaf", "clad", "trim", "grate", "window", "girder", "pillar", "paint", "lamp" };
  for (int k = 0; k < K_KINDS; k++)
    if (!strcmp(N[k], s)) return k;
  return K_WALL;
}
static int item_named(const char *s) {
  static const char *N[ITEM_KINDS] = { "health", "armor", "scatter", "rocket", "rail", "ammo" };
  for (int k = 0; k < ITEM_KINDS; k++)
    if (!strcmp(N[k], s)) return k;
  return ITEM_AMMO;
}

/* a baked CSG mesh: drawn in its kind's material (merged with that kind's other pieces) and a static Box3D mesh shape, so
 * shots, movement and the navmesh see exactly the drawn walls */
static void csg_piece(const char *file, int kind) {
  char path[96];
  snprintf(path, sizeof path, "levels/%s", file);
  size_t len = 0;
  void *bytes = cw_asset_load(path, &len);
  cw_csg_mesh m;
  if (!bytes || !cw_csg_mesh_decode(bytes, len, &m)) {
    cw_error("arenafps: %s missing or bad", path);
    free(bytes);
    return;
  }
  mb_csg(&MB[kind], &m, KM[kind].uvm, TINT[kind]);
  if (A.nmeshes < (int)(sizeof A.meshes / sizeof *A.meshes) && m.index_count >= 3) {
    t3_vec3 *v = malloc(sizeof(t3_vec3) * m.vertex_count);
    for (uint32_t i = 0; i < m.vertex_count; i++) v[i] = v3(m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2]);
    cw_mesh3d cm = cw_mesh3d_create(v, (int)m.vertex_count, (const int32_t *)m.indices, (int)(m.index_count / 3));
    free(v);
    A.meshes[A.nmeshes++] = cm;
    cw_body3d_def bd = cw_body3d_default_def();
    bd.type = CW_BODY_STATIC;
    cw_body3d b = cw_body3d_create(A.world, &bd);
    cw_shape3d_def sd = cw_shape3d_default_def();
    cw_shape3d_mesh(b, &sd, cm, v3(1, 1, 1));
    grow(v3(m.bounds[0], m.bounds[1], m.bounds[2]), v3(m.bounds[3], m.bounds[4], m.bounds[5]));
  }
  free(bytes);
}

/* a ceiling lamp: a steel housing, a glowing panel and a short light (a warmer, dimmer one in the spawn rooms) */
static void ceiling_lamp(float x, float y, float z, bool spawn) {
  const float *c = LAMP_RGB[A.id];
  trim(x, y - 0.12f, z, 1.3f, 0.12f, 0.5f, K_METAL, 0);
  trim(x, y - 0.15f, z, 1.1f, 0.03f, 0.34f, K_LAMP, 0);
  accent_light(x, y - 0.6f, z, c[0], c[1], c[2], spawn ? 14 : 20, spawn ? 8 : 10);
}

/* a doorway's frame on both faces: two jambs and a lintel (axis 'x': the wall runs along z) */
static void door_frame(float x, float z, float w, float h, char axis) {
  const float T = 0.14f, D = 0.86f; /* trim width, depth across the wall (a 0.6 m wall plus a lip each side) */
  if (axis == 'x') {
    trim(x, 0, z - w / 2 - T / 2, D, h, T, K_TRIM, 0);
    trim(x, 0, z + w / 2 + T / 2, D, h, T, K_TRIM, 0);
    trim(x, h, z, D, T, w + 2 * T, K_TRIM, 0);
  } else {
    trim(x - w / 2 - T / 2, 0, z, T, h, D, K_TRIM, 0);
    trim(x + w / 2 + T / 2, 0, z, T, h, D, K_TRIM, 0);
    trim(x, h, z, w + 2 * T, T, D, K_TRIM, 0);
  }
}

/* a jump pad: a steel ring, a glowing disc, a light and a column of rising sparks; the launch velocity reaches the target
 * with an apex 1.4 m over the higher end (the same sum the fighters' gravity uses) */
static void pad_at(t3_vec3 p, t3_vec3 to) {
  if (A.npads >= MAX_PADS) return;
  pad *pd = &A.pads[A.npads++];
  pd->pos = p;
  const float G = 24.0f;
  float apex = CW_MAX(p.y, to.y) + 1.4f;
  float vy = sqrtf(2 * G * (apex - p.y));
  float t = vy / G + sqrtf(2 * (apex - to.y) / G);
  pd->vel = v3((to.x - p.x) / t, vy, (to.z - p.z) / t);
  pd->target = to;
  trim(p.x, p.y, p.z, 2.0f, 0.12f, 2.0f, K_METAL, 0.785f);
  const float *c = LAMP_RGB[A.id];
  cw_node disc = mesh_box(v3(p.x, p.y + 0.13f, p.z), v3(1.5f, 0.02f, 1.5f), (cw_color){ 0.3f, 0.8f, 1, 1 }, QI, 0.3f, false);
  cw_node_set(disc, "emissive", cw_val_color(1.2f, 3.6f, 4.8f, 1));
  accent_light(p.x, p.y + 0.8f, p.z, 0.4f, 0.8f, 1.0f, 18, 6);
  (void)c;
  cw_node ps = cw_node_new("GPUParticles3D", "PadLift");
  cw_node_set(ps, "amount", cw_val_int(24));
  cw_node_set(ps, "lifetime", cw_val_float(1.0));
  cw_node_set(ps, "emitting", cw_val_bool(true));
  cw_node_set(ps, "direction", cw_val_vec3(0, 1, 0));
  cw_node_set(ps, "spread", cw_val_float(6));
  cw_node_set(ps, "speed_min", cw_val_float(1.5));
  cw_node_set(ps, "speed_max", cw_val_float(3.0));
  cw_node_set(ps, "gravity", cw_val_vec3(0, 0, 0));
  cw_node_set(ps, "size", cw_val_float(0.05));
  cw_node_set(ps, "emission_shape", cw_val_enum("BOX"));
  cw_node_set(ps, "emission_box", cw_val_vec3(0.6f, 0.02f, 0.6f));
  cw_node_set(ps, "color_ramp", cw_val_string("0 0.5 0.9 1 0, 0.2 0.5 0.9 1 1, 1 0.3 0.6 1 0"));
  cw_node_set(ps, "energy", cw_val_float(4));
  cw_node_set(ps, "draw_mode", cw_val_enum("BILLBOARD"));
  cw_node_set(ps, "blend", cw_val_enum("ADD"));
  cw_node3d_set_position(ps, v3(p.x, p.y + 0.15f, p.z));
  cw_node_add_child(A.root, ps);
  pd->node = disc;
}

/* a solid box of a kind (roof plant, kerbs): drawn and collided */
static void solid_box(int kind, float x, float y0, float z, float sx, float sy, float sz) { block(x, y0, z, sx, sy, sz, kind); }

/* railings along a bridge's two long sides (rect x0 z0 x1 z1, deck top y): posts, a top rail and a collision bar */
static void bridge_rails(float x0, float z0, float x1, float z1, float y) {
  bool along_x = (x1 - x0) > (z1 - z0);
  for (int s = 0; s < 2; s++) {
    float ex = along_x ? 0 : (s ? x1 - 0.06f : x0 + 0.06f), ez = along_x ? (s ? z1 - 0.06f : z0 + 0.06f) : 0;
    float len = along_x ? x1 - x0 : z1 - z0, cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
    float px = along_x ? cx : ex, pz = along_x ? ez : cz;
    trim(px, y + 1.0f, pz, along_x ? len : 0.07f, 0.07f, along_x ? 0.07f : len, K_METAL, 0);
    trim(px, y + 0.5f, pz, along_x ? len : 0.04f, 0.04f, along_x ? 0.04f : len, K_METAL, 0);
    for (float t = 0; t <= len + 0.01f; t += 2.0f) {
      float qx = along_x ? x0 + t : ex, qz = along_x ? ez : z0 + t;
      trim(qx, y, qz, 0.06f, 1.0f, 0.06f, K_METAL, 0);
    }
    body_box(v3(px, y + 0.5f, pz), v3(along_x ? len : 0.1f, 1.0f, along_x ? 0.1f : len), QI);
  }
}

static bool load_layout(int id) {
  static const char *FILE[ARENAS] = { "foundry", "courtyard", "terraces" };
  char path[64];
  snprintf(path, sizeof path, "levels/%s.lvl", FILE[id]);
  size_t len = 0;
  char *text = cw_asset_load(path, &len);
  if (!text) { cw_error("arenafps: %s missing", path); return false; }
  char *buf = malloc(len + 1);
  memcpy(buf, text, len);
  buf[len] = 0;
  free(text);
  for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
    char a[48], b[48];
    float v[9];
    int n;
    if (line[0] == '#') continue;
    if (sscanf(line, "top %f", &v[0]) == 1) A.top = v[0];
    else if (sscanf(line, "csg %47s %47s", a, b) == 2) csg_piece(a, kind_named(b));
    else if (sscanf(line, "spawn %f %f %f", &v[0], &v[1], &v[2]) == 3) spawn_at(v[0], v[1], v[2]);
    else if (sscanf(line, "item %47s %f %f %f", a, &v[0], &v[1], &v[2]) == 4) item_at(item_named(a), v[0], v[1] + 0.5f, v[2]);
    else if (sscanf(line, "ramp %f %f %f %f %f %f %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &n) == 7)
      ramp(v[0], v[1], v[2], v[3], v[4], v[5], n, K_FLOOR);
    else if (sscanf(line, "pad %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) pad_at(v3(v[0], v[1], v[2]), v3(v[3], v[4], v[5]));
    else if (sscanf(line, "link %f %f %f %f %f %f %47s %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], a, &n) == 8) {
      if (A.nlinks < MAX_LINKS) {
        A.links[A.nlinks].s = v3(v[0], v[1], v[2]);
        A.links[A.nlinks].e = v3(v[3], v[4], v[5]);
        A.links[A.nlinks].tag = !strcmp(a, "jump") ? LINK_JUMP : !strcmp(a, "drop") ? LINK_DROP : LINK_PAD;
        A.links[A.nlinks].two = n != 0;
        A.nlinks++;
      }
    }
    else if (sscanf(line, "lamp %f %f %f %d", &v[0], &v[1], &v[2], &n) == 4) ceiling_lamp(v[0], v[1], v[2], n != 0);
    else if (sscanf(line, "column %f %f %f %f", &v[0], &v[1], &v[2], &v[3]) == 4) block(v[0], v[1], v[2], 0.9f, v[3], 0.9f, K_PILLAR);
    else if (sscanf(line, "doorframe %f %f %f %f %47s", &v[0], &v[1], &v[2], &v[3], a) == 5) door_frame(v[0], v[1], v[2], v[3], a[0]);
    else if (sscanf(line, "rail %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5) bridge_rails(v[0], v[1], v[2], v[3], v[4]);
    else if (sscanf(line, "crates %f %f %f %f", &v[0], &v[1], &v[2], &v[3]) == 4) crate_stack(v[0], v[1], v[2], v[3]);
    else if (sscanf(line, "barrels %f %f %f", &v[0], &v[1], &v[2]) == 3) barrels(v[0], v[1], v[2]);
    else if (sscanf(line, "block %47s %f %f %f %f %f %f", a, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 7)
      solid_box(kind_named(a), v[0], v[1], v[2], v[3], v[4], v[5]);
    else if (sscanf(line, "prop %47s %f %f %f %f %d %f %f %f", a, &v[0], &v[1], &v[2], &v[3], &n, &v[4], &v[5], &v[6]) == 9) {
      if (n) solid_prop(a, v[0], v[1], v[2], v[3], 1, 1, 1, v[4], v[5], v[6]);
      else prop(a, v[0], v[1], v[2], v[3], 1, 1, 1);
    } else if (sscanf(line, "water %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5) {
      A.water = cw_node_new("Water3D", "Pit");
      cw_node_set(A.water, "size", cw_val_vec2(v[3], v[4]));
      cw_node_set(A.water, "subdivisions", cw_val_int(64));
      cw_node_set(A.water, "color", cw_val_color(0.12f, 0.35f, 0.4f, 1));
      cw_node_set(A.water, "deep_color", cw_val_color(0.03f, 0.12f, 0.16f, 1));
      cw_node_set(A.water, "murk", cw_val_float(0.55));
      cw_node3d_set_position(A.water, v3(v[0], v[1], v[2]));
      cw_node_add_child(A.root, A.water);
    }
  }
  free(buf);
  return true;
}

/* ── each arena's shell (the outer walls and what hangs on them, above the playing space) ── */

static void dress_foundry(const palette *p) {
  const float H = 28;
  shell_walls(H, 9, K_WALL, K_FLOOR, p->trim);
  wall_style st = { .y0 = 0, .skirt = K_TRIM, .clad_hi = 3.4f, .pilaster_every = 7, .win_lo = 5.0f, .win_hi = 6.9f, .cornice = K_METAL };
  wall_dress(H, 9, &st);
  pipe_runs(H, 7.9f, 0xF);
  vents(H, 1.4f, 14);
  roof_girders(H, 9.0f, 8);
  wall_lamps(H, 2.6f, 7, 3.5f, 18, 8, 0xF);
  floodlight(0, 7.6f, -H + 0.2f, 3.14159265f, p->accent, 120);
  floodlight(0, 7.6f, H - 0.2f, 0, p->accent, 120);
  floodlight(-H + 0.2f, 7.6f, 0, -1.5708f, p->accent, 120);
  floodlight(H - 0.2f, 7.6f, 0, 1.5708f, p->accent, 120);
  accent_light(0, 6.5f, 0, 0.5f, 0.75f, 1.0f, 70, 22);
  /* the deck: hazard edges on top, a steel lip; hazard feet on the core's pillars */
  floor_frame(0, 0, 3.85f, 3.85f, 2.0f, 0.3f);
  trim(0, 1.75f, 0, 8.12f, 0.25f, 8.12f, K_METAL, 0);
  for (int i = 0; i < 4; i++) {
    float sx = (i & 1) ? 1 : -1, sz = (i & 2) ? 1 : -1;
    trim(sx * 7, 0, sz * 7, 2.1f, 1.2f, 2.1f, K_TRIM, 0);
    trim(sx * 7, 8.3f, sz * 7, 2.5f, 0.7f, 2.5f, K_METAL, 0);
    strip(sx * 7 - 0.6f, 4.7f, sz * 7 + sz * 1.02f, 0.12f, 6.0f, 0.04f, p->trim, 3.0f);
  }
  floor_frame(0, 0, 11.6f, 11.6f, 0, 0.35f);
}

static void dress_courtyard(const palette *p) {
  const float H = 30;
  shell_walls(H, 8, K_WALL, K_FLOOR, p->trim);
  wall_style st = { .y0 = 0, .skirt = K_PILLAR, .clad_hi = 0, .pilaster_every = 6, .win_lo = 0, .win_hi = 0, .cornice = K_PILLAR };
  wall_dress(H, 8, &st);
  vents(H, 3.2f, 12);
  wall_lamps(H, 3.4f, 12, 9, 24, 9, 0xF);
  floodlight(0, 6.4f, -H + 0.2f, 3.14159265f, p->accent, 90);
  floodlight(0, 6.4f, H - 0.2f, 0, p->accent, 90);
  floodlight(-H + 0.2f, 6.4f, 0, -1.5708f, p->accent, 90);
  floodlight(H - 0.2f, 6.4f, 0, 1.5708f, p->accent, 90);
  accent_light(0, 3.5f, 0, 1.0f, 0.7f, 0.35f, 55, 16);
  /* the houses: a cornice and a timber band round each roof, lit windows on the street faces between the doors */
  for (int i = 0; i < 4; i++) {
    float sx = (i & 1) ? 1 : -1, sz = (i & 2) ? 1 : -1;
    float cx = sx * 15, cz = sz * 15;
    trim(cx, 3.42f, cz, 18.2f, 0.18f, 18.2f, K_TRIM, 0);
    static const float WX[2] = { 14.85f, 22.4f }, WZ[2] = { 13.4f, 20.6f };
    for (int k = 0; k < 2; k++) { /* between the doors: the outer faces and the faces on the cross streets */
      trim(sx * 24.04f, 1.2f, sz * WX[k], 0.06f, 1.3f, 1.2f, K_WINDOW, 0);
      trim(sx * WZ[k], 1.2f, sz * 24.04f, 1.2f, 1.3f, 0.06f, K_WINDOW, 0);
      trim(sx * 5.96f, 1.2f, sz * WX[k], 0.06f, 1.3f, 1.2f, K_WINDOW, 0);
      trim(sx * WX[k], 1.2f, sz * 5.96f, 1.2f, 1.3f, 0.06f, K_WINDOW, 0);
    }
    (void)cx, (void)cz;
  }
}

static void dress_terraces(const palette *p) {
  const float H = 26;
  shell_walls(H, 10, K_WALL, K_FLOOR, p->trim);
  wall_style st = { .y0 = A.top, .skirt = K_TRIM, .clad_hi = 5.9f, .pilaster_every = 8, .win_lo = 6.6f, .win_hi = 8.4f, .cornice = K_METAL };
  wall_dress(H, 10, &st);
  pipe_runs(H, 9.0f, 0xF);
  wall_lamps(H, 6.0f, 8, 4, 26, 10, 0xF);
  floodlight(0, 8.4f, -H + 0.2f, 3.14159265f, p->accent, 130);
  floodlight(0, 8.4f, H - 0.2f, 0, p->accent, 130);
  floodlight(-H + 0.2f, 8.4f, 0, -1.5708f, p->accent, 130);
  floodlight(H - 0.2f, 8.4f, 0, 1.5708f, p->accent, 130);
  accent_light(0, 2.4f, 0, 0.2f, 0.85f, 0.8f, 60, 18);
  /* hazard lips along the pit's rim and the ledges */
  for (int s = -1; s <= 1; s += 2) {
    trim(0, A.top, s * 12.15f, 24.6f, 0.014f, 0.3f, K_TRIM, 0);
    trim(s * 12.15f, A.top, 0, 0.3f, 0.014f, 24.6f, K_TRIM, 0);
  }
}

static cw_color ITEM_COLOR[ITEM_KINDS] = {
  { 0.2f, 1, 0.3f, 1 }, { 0.3f, 0.6f, 1, 1 }, { 1, 0.7f, 0.2f, 1 }, { 1, 0.3f, 0.2f, 1 }, { 0.8f, 0.4f, 1, 1 }, { 1, 1, 0.4f, 1 },
};

/* the node keeps its own reference (cw_mesh3d_set_procedural retains the mesh): drop ours, or every match keeps the last
 * one's level meshes, their GPU buffers and the cloud texture alive (+20 to 40 MB a match in the 30 minute soak). The
 * autopilot's ARENA_LEAK=1 keeps them, as the soak gate's control */
static void arena_release_mesh(t3_mesh *mesh) {
#ifdef ARENA_AUTOPILOT
  static int leak = -1;
  if (leak < 0) leak = getenv("ARENA_LEAK") && atoi(getenv("ARENA_LEAK"));
  if (leak) return;
#endif
  t3_release(mesh);
}

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
  A.top = 4.1f;
  load_layout(id);
  switch (id) {
    case 0: dress_foundry(p); break;
    case 1: dress_courtyard(p); break;
    default: dress_terraces(p); break;
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
      arena_release_mesh(mesh);
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
    arena_release_mesh(mesh);
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
#ifdef CW_NAV_LINKS
  /* links: every pad (one way, to where it throws you), then the level's jumps and drops; each end snapped to the floor
   * under it. A drop that lands no lower than 1 m is no drop (a stair top beside a landing): left out */
  for (int i = 0; i < A.npads; i++)
    cw_nav_geom_add_link(&g, &(cw_nav_link){ .start = A.pads[i].pos, .end = A.pads[i].target, .tag = LINK_PAD,
                                             .radius = 1.0f, .duration = 1.0f, .arc = 2.0f });
  for (int i = 0; i < A.nlinks; i++) {
    /* each end: the first floor below the link's higher end (a drop over a landing finds the landing) */
    t3_vec3 s = A.links[i].s, e = A.links[i].e, hit;
    float top = CW_MAX(s.y, e.y) + 0.6f;
    if (arena_ray(v3(s.x, top, s.z), v3(s.x, s.y - 6, s.z), &hit, NULL)) s = hit;
    if (arena_ray(v3(e.x, top, e.z), v3(e.x, e.y - 6, e.z), &hit, NULL)) e = hit;
    if (A.links[i].tag == LINK_DROP && s.y - e.y < 1.0f) continue;
    cw_nav_geom_add_link(&g, &(cw_nav_link){ .start = s, .end = e, .tag = (uint32_t)A.links[i].tag,
                                             .bidirectional = A.links[i].two, .arc = A.links[i].tag == LINK_JUMP ? 0.6f : 0.3f });
  }
#endif
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
#ifdef CW_NAV_LINKS
  A.nav_links = st.links;
  A.nav_links_connected = st.links_connected;
#endif
  cw_navmesh_set_query_extents(A.nav, v3(2, 4, 2));
  cw_info("arena %s: %d triangles-polys, navmesh %.1f ms, %d of %d links connected", ARENA_INFO[id].name, st.polys,
          st.build_ms, A.nav_links_connected, A.nav_links);
}

void arena_free(void) {
  if (cw_navmesh_valid(A.nav)) cw_navmesh_free(A.nav);
  if (cw_world3d_valid(A.world)) cw_world3d_destroy(A.world); /* its shapes first, then the meshes they used */
  for (int i = 0; i < A.nmeshes; i++) cw_mesh3d_destroy(A.meshes[i]);
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
