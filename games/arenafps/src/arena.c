/* The arenas. The layout (rooms, corridors, open halls, doors, roofs, stairs, jump pads, spawns, items) is data: tools/make-levels.mjs
 * writes levels/<name>.cwscene, bakes its CSG shapes with the engine's cwcsg tool into levels/<name>_<root>.cwcsg, and writes
 * levels/<name>.lvl for what the game places itself. Here the baked meshes become the level's drawn meshes (one per surface
 * kind, with the dressing of that kind) and Box3D mesh shapes (collision, shots and the navmesh read the same triangles); the
 * outer shell and what hangs on it stay boxes. Each arena has its own palette, materials and mood. */
#include <time.h>

#include "cartwheel/csg.h"
#include "game.h"
#include "cartwheel/quality.h"

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
#define LAMP_REACH_VL (tune("reach_vl", 0.55f))
#define LAMP_REACH_LOW (tune("reach_low", 0.55f))
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
    [K_WALL] = { TEX_SLAB, 2.5f, 1, 0, 1, { 0.92f, 0.92f, 0.95f } }, /* at 4 m a repeat its pits blur into blocks at arm's length */
    [K_ACCENT] = { TEX_PANEL_TEAL, 4, 1, 0.1f, 1, { 1, 1, 1 } },
    [K_METAL] = { TEX_STEEL, 2, 0.9f, 0.85f, 1, { 0.42f, 0.44f, 0.47f } },
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
static cw_value val_vec4(float a, float b, float c, float d) { cw_value v = { CW_T_VEC4 }; v.v[0] = a, v.v[1] = b, v.v[2] = c, v.v[3] = d; return v; }
static void pickup_model(cw_node into, int kind);

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
/* the grounding check (tests/arenafps-grounding.mjs): every solid prop stands on something. arena_ground_check runs on the first
 * tick (the world answers rays once it has stepped): a ray down just off each corner of its footprint must find a surface
 * within 3 cm of its base; a prop can overhang an edge, so one corner is enough. ARENA_PROP_LIFT=1, the control, lifts the first by 0.5 m */
static struct { t3_vec3 p; float h; } STAND[256];
static int nstand;
void arena_ground_check(void) {
  A.prop_count = nstand, A.prop_floating = 0;
  for (int i = 0; i < nstand; i++) {
    bool stands = false;
    for (int k = 0; k < 4 && !stands; k++) {
      float h = STAND[i].h * 0.5f + 0.04f;
      t3_vec3 from = v3add(STAND[i].p, v3(k & 1 ? h : -h, 0.04f, k & 2 ? h : -h)), hit;
      stands = arena_ray(from, v3add(from, v3(0, -0.07f, 0)), &hit, NULL);
    }
    if (!stands) {
      A.prop_floating++;
      cw_error("arenafps: a %.1f m prop floats at %.2f %.2f %.2f", (double)STAND[i].h, (double)STAND[i].p.x, (double)STAND[i].p.y, (double)STAND[i].p.z);
    }
  }
}
static void solid_prop(const char *name, float x, float y0, float z, float yaw, float sx, float sy, float sz, float hx, float hy, float hz) {
  {
    static int lift = -1;
    if (lift < 0) lift = getenv("ARENA_PROP_LIFT") && atoi(getenv("ARENA_PROP_LIFT"));
    if (lift && nstand == 0) y0 += 0.5f;
    if (nstand < 256) STAND[nstand].p = v3(x, y0, z), STAND[nstand].h = hx < hz ? hx : hz, nstand++;
  }
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
static cw_node accent_light(float x, float y, float z, float r, float g, float b, float energy, float range) {
  if (tune("accent", 1) < 0.5f) return (cw_node){ 0 }; /* ARENA_T_accent=0 (autopilot): measuring the lights' cost */
  cw_node l = cw_node_new("OmniLight3D", "Accent");
  cw_node_set(l, "color", cw_val_color(r, g, b, 1));
  cw_node_set(l, "energy", cw_val_float(energy));
  cw_node_set(l, "range", cw_val_float(range * tune("arange", 1)));
  cw_node3d_set_position(l, v3(x, y, z));
  cw_node_add_child(A.root, l);
  return l;
}

/* a wall floodlight: bracket and hood from props, a glowing lens and a light; dir yaw faces into the arena */
static void floodlight(float x, float y, float z, float yaw, cw_color c, float energy) {
  prop("lamp", x, y, z, yaw, 1, 1, 1);
  float sn, cs;
  cw_sincos(yaw, &sn, &cs);
  float fx = -sn * 0.68f, fz = -cs * 0.68f; /* the lens sits 0.68 m out along the lamp's front */
  cw_node lens = mesh_box(v3(x + fx, y + 0.1f, z + fz), v3(0.36f, 0.18f, 0.02f), c, (t3_quat){ 0, sinf(yaw * 0.5f), 0, cosf(yaw * 0.5f) }, 0.3f, false);
  cw_node_set(lens, "emissive", cw_val_color(c.r * 8, c.g * 8, c.b * 8, 1));
  if (tune("flood", 1) > 0.5f) accent_light(x + fx * 1.5f, y - 0.2f, z + fz * 1.5f, c.r, c.g, c.b, energy, 15);
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
      if (tune("walllamps", 1) > 0.5f) accent_light(f.cx + f.tx * s + f.nx * 0.6f, y - 0.2f, f.cz + f.tz * s + f.nz * 0.6f, c[0], c[1], c[2], energy, range);
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
  cw_node l = tune("ceil", 1) > 0.5f ? accent_light(x, y - 0.6f, z, c[0], c[1], c[2], spawn ? 14 : 20, spawn ? 8 : 10) : (cw_node){ 0 };
  if (cw_node_valid(l) && A.nlamps < MAX_LAMPS) A.lamps[A.nlamps] = l, A.lamp_range[A.nlamps++] = spawn ? 8 : 10;
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
  /* a round emitter set in a dark collar on the steel base: a square at full emissive read as a flat white slab */
  cw_node collar = cw_node_new("MeshInstance3D", "PadCollar");
  cw_node_set(collar, "primitive", cw_val_enum("CYLINDER"));
  cw_node_set(collar, "size", cw_val_vec3(0.82f, 0.03f, 0));
  cw_node_set(collar, "color", cw_val_color(0.05f, 0.055f, 0.06f, 1));
  cw_node_set(collar, "roughness", cw_val_float(0.45));
  cw_node_set(collar, "metalness", cw_val_float(0.6));
  cw_node3d_set_position(collar, v3(p.x, p.y + 0.135f, p.z));
  cw_node_add_child(A.root, collar);
  cw_node disc = cw_node_new("MeshInstance3D", "PadGlow");
  cw_node_set(disc, "primitive", cw_val_enum("CYLINDER"));
  cw_node_set(disc, "size", cw_val_vec3(0.62f, 0.036f, 0));
  cw_node_set(disc, "color", cw_val_color(0.1f, 0.28f, 0.36f, 1));
  cw_node_set(disc, "emissive", cw_val_color(0.3f, 1.05f, 1.45f, 1));
  cw_node_set(disc, "cast_shadow", cw_val_bool(false));
  cw_node3d_set_position(disc, v3(p.x, p.y + 0.14f, p.z));
  cw_node_add_child(A.root, disc);
  if (tune("padl", 1) > 0.5f) accent_light(p.x, p.y + 0.8f, p.z, 0.4f, 0.8f, 1.0f, 18, 6);
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
      cw_node_set(A.water, "caustics", cw_val_float(0.18)); /* the default drew a white net of veins over the pit floor */
      cw_node_set(A.water, "caustic_size", cw_val_float(3.2));
      cw_node_set(A.water, "crest_foam", cw_val_float(0));
      cw_node_set(A.water, "shore_foam", cw_val_float(0.25));
      /* a flooded pit, not a sea: the default waves stood as a hill across it */
      cw_node_set(A.water, "wave0", val_vec4(20, 5, 0.06f, 0.5f));
      cw_node_set(A.water, "wave1", val_vec4(75, 3, 0.04f, 0.5f));
      cw_node_set(A.water, "wave2", val_vec4(-40, 1.6f, 0.02f, 0.4f));
      cw_node_set(A.water, "wave3", val_vec4(140, 0.9f, 0.01f, 0.3f));
      cw_node3d_set_position(A.water, v3(v[0], v[1], v[2]));
      cw_node_add_child(A.root, A.water);
    }
  }
  free(buf);
  return true;
}

/* the pickups' models (tools/blender/pickups.py), parsed once and shared; each item gets its own meshes over the shared geometry and materials */
static void pickup_meshes(t3_object *o, cw_node into) {
  if (o->type == T3_MESH) {
    t3_mesh *src = (t3_mesh *)o;
    t3_mesh *me = t3_mesh_new(src->geometry, src->materials[0]);
    me->base.cast_shadow = true;
    me->base.receive_shadow = false;
    cw_node n = cw_node_new("MeshInstance3D", "ItemPart");
    cw_node_add_child(into, n);
    cw_mesh3d_set_procedural(n, me);
    t3_release(me);
  }
  for (int i = 0; i < o->child_count; i++) pickup_meshes(o->children[i], into);
}
static void pickup_model(cw_node into, int kind) {
  static t3_gltf *GL[ITEM_KINDS];
  static const char *FILE_[ITEM_KINDS] = { "health", "armor", "scatter", "rocket", "rail", "ammo" };
  if (!GL[kind]) {
    char path[48];
    snprintf(path, sizeof path, "models/pk_%s.glb", FILE_[kind]);
    size_t len;
    void *glb = cw_asset_load(path, &len);
    GL[kind] = glb ? t3_gltf_parse(glb, len, NULL, NULL) : NULL;
    free(glb);
    if (!GL[kind]) { cw_error("arenafps: %s missing", path); return; }
  }
  pickup_meshes(GL[kind]->scene, into);
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
  if (tune("centre", 1) > 0.5f) accent_light(0, 3.5f, 0, 1.0f, 0.7f, 0.35f, 55, 16);
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

/* ── the world past the walls: each arena stands in a place, not a void. The foundry's works here (low-poly silhouettes 55 to
 * 120 m out, two draws: the hulls, and the warning bands and the flare); the courtyard's hill town and the terraces' city at
 * the end of the file (skyline) ── */
static void arena_release_mesh(t3_mesh *mesh);
static void skyline(int id);
static uint32_t sk_rng;
static float sk_rand(void) { sk_rng = sk_rng * 1664525u + 1013904223u; return (float)(sk_rng >> 8) / 16777216.0f; }
/* a turn about y, then a tilt about the turned x axis */
static t3_quat sk_q(float yaw, float tilt) {
  t3_quat qy = { 0, sinf(yaw * 0.5f), 0, cosf(yaw * 0.5f) }, qx = { sinf(tilt * 0.5f), 0, 0, cosf(tilt * 0.5f) };
  return (t3_quat){ qy.w * qx.x + qy.x * qx.w + qy.y * qx.z - qy.z * qx.y, qy.w * qx.y - qy.x * qx.z + qy.y * qx.w + qy.z * qx.x,
                    qy.w * qx.z + qy.x * qx.y - qy.y * qx.x + qy.z * qx.w, qy.w * qx.w - qy.x * qx.x - qy.y * qx.y - qy.z * qx.z };
}
/* an upright prism of n sides (a stack, a tank, a tower), base at y0 */
static void sk_cyl(mesh_build *b, float x, float y0, float z, float r0, float r1, float h, int n, const float rgb[3]) {
  for (int i = 0; i < n; i++) {
    float a0 = 6.2831853f * (float)i / (float)n, a1 = 6.2831853f * (float)(i + 1) / (float)n, s0, c0, s1, c1, sm, cm;
    cw_sincos(a0, &s0, &c0), cw_sincos(a1, &s1, &c1), cw_sincos((a0 + a1) * 0.5f, &sm, &cm);
    t3_vec3 nn = { sm, 0, cm };
    int v0 = mb_vert(b, (t3_vec3){ x + s0 * r0, y0, z + c0 * r0 }, nn, (float)i, 0), v1 = mb_vert(b, (t3_vec3){ x + s1 * r0, y0, z + c1 * r0 }, nn, (float)i + 1, 0);
    int v2 = mb_vert(b, (t3_vec3){ x + s1 * r1, y0 + h, z + c1 * r1 }, nn, (float)i + 1, h / 4), v3i = mb_vert(b, (t3_vec3){ x + s0 * r1, y0 + h, z + c0 * r1 }, nn, (float)i, h / 4);
    mb_color(b, v0, rgb[0], rgb[1], rgb[2]), mb_color(b, v1, rgb[0], rgb[1], rgb[2]), mb_color(b, v2, rgb[0], rgb[1], rgb[2]), mb_color(b, v3i, rgb[0], rgb[1], rgb[2]);
    mb_quad(b, v0, v1, v2, v3i);
  }
}
static void skyline_foundry(void) {
  const int id = 0;
  mesh_build hull = { 0 }, lit = { 0 };
  float macro = mb_macro;
  mb_macro = 0;
  sk_rng = 0x5C1E + (uint32_t)id * 7919u;
  { /* the foundry's works: sheds with saw-tooth roofs, smoke stacks, gantry cranes, gas holders, a flare stack */
    const float shed[3] = { 0.30f, 0.29f, 0.28f }, steel[3] = { 0.36f, 0.2f, 0.16f }, stack[3] = { 0.42f, 0.38f, 0.35f }, tank[3] = { 0.3f, 0.33f, 0.34f };
    for (int i = 0; i < 22; i++) {
      float a = 6.2831853f * ((float)i + sk_rand() * 0.6f) / 22.0f, r = 55 + sk_rand() * 50, sa, ca;
      cw_sincos(a, &sa, &ca);
      float w = 18 + sk_rand() * 22, d = 12 + sk_rand() * 14, h = 14 + sk_rand() * 16, yaw = a + 1.5708f;
      t3_quat q = { 0, sinf(yaw * 0.5f), 0, cosf(yaw * 0.5f) };
      mb_box(&hull, (t3_vec3){ sa * r, h * 0.5f - 1, ca * r }, q, (t3_vec3){ w, h, d }, 8, shed);
      for (int k = 0; k < 3; k++) { /* the saw teeth: sloped slabs along the shed */
        float off = ((float)k - 1) * w / 3, sy, cy;
        cw_sincos(yaw, &sy, &cy);
        mb_box(&hull, (t3_vec3){ sa * r + cy * off, h + 1.4f, ca * r - sy * off }, sk_q(yaw + 1.5708f, 0.35f), (t3_vec3){ d * 0.95f, 0.4f, w / 3 - 0.4f }, 8, shed);
      }
      if (i % 4 == 1) { /* a stack beside the shed */
        float sh = 50 + sk_rand() * 30;
        sk_cyl(&hull, sa * (r + d), -1, ca * (r + d), 2.2f, 1.6f, sh, 12, stack);
        sk_cyl(&lit, sa * (r + d), sh - 3, ca * (r + d), 1.62f, 1.62f, 0.6f, 12, (const float[3]){ 1.0f, 0.3f, 0.15f }); /* an aircraft warning band */
      }
      if (i % 5 == 3) { /* a gas holder */
        sk_cyl(&hull, sa * (r + 14), -1, ca * (r + 14), 11, 11, 22 + sk_rand() * 10, 16, tank);
      }
      if (i % 6 == 0) { /* a gantry crane: two legs and the beam over them */
        float cr = r - 12, sy, cy;
        cw_sincos(yaw, &sy, &cy);
        for (int s = -1; s <= 1; s += 2) mb_box(&hull, (t3_vec3){ sa * cr + cy * (float)s * 9, 11, ca * cr - sy * (float)s * 9 }, q, (t3_vec3){ 1.4f, 24, 1.4f }, 8, steel);
        mb_box(&hull, (t3_vec3){ sa * cr, 23.5f, ca * cr }, q, (t3_vec3){ 26, 2.2f, 2 }, 8, steel);
      }
    }
    { /* the flare stack: a thin tower and a burning tip (lit) */
      float a = 2.3f, r = 120, sa, ca;
      cw_sincos(a, &sa, &ca);
      sk_cyl(&hull, sa * r, -1, ca * r, 1.4f, 0.8f, 52, 8, stack);
      sk_cyl(&lit, sa * r, 51, ca * r, 1.2f, 0.2f, 5, 8, (const float[3]){ 1.0f, 0.55f, 0.15f });
    }
  }
  mb_macro = macro;
  for (int pass = 0; pass < 2; pass++) {
    mesh_build *b = pass ? &lit : &hull;
    if (!b->nv) continue;
    t3_geometry *geo = mb_finish(b);
    /* the level's own material shape (physical, its maps, vertex colours): no new shader program on any preset */
    t3_material *m = t3_mesh_physical_material_new(0xffffff);
    m->vertex_colors = true;
    m->roughness = 0.95f;
    if (!pass) tex_apply(m, TEX_CONCRETE, 1, 0.4f, 1.0f);
    else { /* a flare, the stacks' warning bands */
      tex_apply(m, TEX_CONCRETE, 1, 0.4f, 1.0f);
      m->emissive = (t3_color){ 1.0f, 0.42f, 0.12f };
      m->emissive_intensity = 6;
    }
    t3_mesh *mesh = t3_mesh_new(geo, m);
    mesh->base.cast_shadow = false;
    mesh->base.receive_shadow = true; /* out past the shadow's reach it reads as lit; kept for the level's program */
    cw_node n = cw_node_new("MeshInstance3D", pass ? "SkylineLit" : "Skyline");
    cw_node_add_child(A.root, n);
    cw_mesh3d_set_procedural(n, mesh);
    t3_release(geo);
    t3_release(m);
    arena_release_mesh(mesh);
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
  nstand = 0;
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
  if (tune("skyline", 1) > 0.5f) { if (id == 0) skyline_foundry(); else skyline(id); } /* ARENA_T_skyline=0 (autopilot): the void, for before and after */
  { /* faces laid flush on others (decks, trims, wall boxes sharing a face) 5 mm proud: no surface fights another for the depth test */
    clock_t t0 = clock();
    int left = 0, moved = 0;
    /* ARENA_T_separate=0 (autopilot): left as built, the flash sweep's control */
    for (int pass = 0; pass < 3 && (pass == 0 || left) && tune("separate", 1) > 0.5f; pass++) moved += mb_separate_coplanar(MB, K_KINDS, 0.005f, &left); /* (slivers a pass missed) */
    cw_info("arena %s: %d vertices moved off shared planes in %.1f ms, %d pairs left", ARENA_INFO[id].name, moved, (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC, left);
    A.coplanar_moved = moved, A.coplanar_left = left;
    if (left) cw_warn("arena %s: %d faces still share a plane after separating", ARENA_INFO[id].name, left);
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
    A.probe = pr;
  }
  /* bounce light: a grid of light probes about 3 m apart over the arena, baked at load with one bounce, so a shaded street
   * takes the warm light its sunlit floor throws back, corners and undersides darken and a coloured wall tints what is
   * next to it. From Medium up (arena_gi_quality); there it takes over the probe's single ambient. Walls are thinner than
   * the spacing, so a little light reaches through them (the grid has no visibility term): the probes sit between the
   * floor and the roofs only. */
  /* the courtyard only: its sunlit stone throws a strong warm bounce into the shaded streets; in the foundry's halls and on the
   * terraces at dusk the grid on top of their flat ambient lit the shadowed walls evenly and flattened the rooms (sheets in the
   * research notes), so they keep the probe's ambient */
  if (A.id == 1 && tune("grid", 1) > 0.5f) {
    t3_vec3 c = v3scale(v3add(A.bounds_min, A.bounds_max), 0.5f), s = v3sub(A.bounds_max, A.bounds_min);
    float y0 = CW_MAX(A.bounds_min.y, -4.0f), h = A.top + 4.0f - y0;
    cw_node g = cw_node_new("LightProbeGrid3D", "Bounce");
    cw_node3d_set_position(g, v3(c.x, y0 + h * 0.5f, c.z));
    cw_node_set(g, "size", cw_val_vec3(s.x, h, s.z));
    cw_node_set(g, "probes", cw_val_vec3(CW_CLAMP((int)(s.x / 3.0f) + 1, 2, 32), CW_CLAMP((int)(h / 3.0f) + 1, 2, 32), CW_CLAMP((int)(s.z / 3.0f) + 1, 2, 32)));
    cw_node_set(g, "bounces", cw_val_int(1));
    cw_node_set(g, "bake_per_frame", cw_val_int(64));
    cw_node_set(g, "far", cw_val_float(80));
    cw_node_set(g, "intensity", cw_val_float(tune("grid_i", 1)));
    A.grid = g;
  }
  arena_gi_quality(); /* the grid and the lamps' reach for the preset now */
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
    it->floor_y = it->pos.y - 0.5f;
    it->node = cw_node_new("Node3D", "Item");
    cw_node3d_set_position(it->node, v3(it->pos.x, it->floor_y + 0.58f, it->pos.z));
    cw_node_add_child(A.root, it->node);
    pickup_model(it->node, it->kind);
    { /* the spot on the floor it stands over, lit in its colour so the item is grounded and the empty spot is still a place */
      cw_node disc = cw_node_new("MeshInstance3D", "ItemSpot");
      cw_node_set(disc, "primitive", cw_val_enum("CYLINDER"));
      cw_node_set(disc, "size", cw_val_vec3(0.62f, 0.03f, 0));
      cw_node_set(disc, "color", cw_val_color(0.05f, 0.05f, 0.055f, 1));
      cw_node_set(disc, "roughness", cw_val_float(0.5));
      cw_node3d_set_position(disc, v3(it->pos.x, it->floor_y + 0.018f, it->pos.z));
      cw_node_add_child(A.root, disc);
      it->spot = disc;
      cw_node glow = cw_node_new("MeshInstance3D", "ItemSpotGlow");
      cw_node_set(glow, "primitive", cw_val_enum("CYLINDER"));
      cw_node_set(glow, "size", cw_val_vec3(0.46f, 0.036f, 0));
      cw_node_set(glow, "color", cw_val_color(c.r * 0.3f, c.g * 0.3f, c.b * 0.3f, 1));
      cw_node_set(glow, "emissive", cw_val_color(c.r * 1.1f, c.g * 1.1f, c.b * 1.1f, 1));
      cw_node_set(glow, "cast_shadow", cw_val_bool(false));
      cw_node3d_set_position(glow, v3(it->pos.x, it->floor_y + 0.02f, it->pos.z));
      cw_node_add_child(A.root, glow);
      it->spot_glow = glow;
    }
    if (tune("itemlight", 1) < 0.5f) continue;
    it->light = cw_node_new("OmniLight3D", "ItemLight");
    cw_node_set(it->light, "color", cw_val_color(c.r, c.g, c.b, 1));
    cw_node_set(it->light, "energy", cw_val_float(14));
    cw_node_set(it->light, "range", cw_val_float(5));
    cw_node3d_set_position(it->light, v3(it->pos.x, it->floor_y + 1.5f, it->pos.z));
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

void arena_gi_quality(void) {
  /* the ceiling lamps reach less far on Very Low and Low: they have no shadows, so most of a 10 m reach is light through the
   * room's own walls onto the street outside, and on a weak GPU that spill was the biggest part of the scene's cost (the
   * courtyard's sixteen lamps: 0.5 ms of 2.6 at Very Low). The room under each lamp stays lit */
  int fx = cw_gfx_effective(CW_GFX_EFFECTS);
  float reach = fx <= CW_QUALITY_VERY_LOW ? LAMP_REACH_VL : fx == CW_QUALITY_LOW ? LAMP_REACH_LOW : 1.0f;
  /* the courtyard in morning sun: on Very Low and Low its arcade lamps are left dark (their pools hardly show by day; the
   * lenses still glow) */
  bool dark = fx <= CW_QUALITY_LOW && A.id == 1 && tune("daylamps", 0) < 0.5f;
  for (int i = 0; i < A.nlamps; i++)
    if (cw_node_valid(A.lamps[i])) cw_node_set(A.lamps[i], "range", cw_val_float(A.lamp_range[i] * reach)), cw_node_set(A.lamps[i], "visible", cw_val_bool(!dark));
  if (!cw_node_valid(A.grid)) return;
  bool on = cw_gfx_effective(CW_GFX_EFFECTS) >= CW_QUALITY_MEDIUM, in = cw_node_parent(A.grid).id != 0;
  if (on && !in) cw_node_add_child(A.root, A.grid);
  else if (!on && in) cw_node_remove_child(A.root, A.grid);
  /* the grid gives the ambient where it is on; the probe's one capture where it is off */
  if (cw_node_valid(A.probe)) cw_node_set(A.probe, "ambient", cw_val_bool(!on));
}

void arena_free(void) {
  if (cw_node_valid(A.grid) && !cw_node_parent(A.grid).id) cw_node_free(A.grid); /* off (Low and Very Low): not in the tree */
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
float arena_tune(const char *name, float def) { return tune(name, def); }
/* values set by the debug command "sw tune <name> <value>" (any build: the flash sweep's controls on a cart), ahead of
 * ARENA_T_<name> (autopilot builds) */
#define TUNE_SET_MAX 16
static struct { char name[24]; float v; } TUNE_SET[TUNE_SET_MAX];
static int tune_set_n;
void arena_tune_set(const char *name, float v) {
  for (int i = 0; i < tune_set_n; i++)
    if (!strcmp(TUNE_SET[i].name, name)) { TUNE_SET[i].v = v; return; }
  if (tune_set_n == TUNE_SET_MAX) return;
  snprintf(TUNE_SET[tune_set_n].name, sizeof TUNE_SET[0].name, "%s", name);
  TUNE_SET[tune_set_n++].v = v;
}
static float tune(const char *name, float def) {
  for (int i = 0; i < tune_set_n; i++)
    if (!strcmp(TUNE_SET[i].name, name)) return TUNE_SET[i].v;
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
  float ambient;                    /* flat ambient on top of the sky's: at 0 shadowed walls and the gun went black, and enemies with them */
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
  { 16.0f, 14, 20, 4.4f, 1.0f, 0.8f, 1.0f, 0.26f, 0.0025f, 0.12f, 0.45f, { 0.3f, 0.29f, 0.28f, 1 }, -0.1f, 0.3f, 1.6f, 1, "luts/foundry.cube", 0.12f, 1 },
  /* COURTYARD: clear morning, crisp shadows, warm stone against a deep blue sky */
  { 9.6f, 20, 130, 4.6f, 1.1f, 0.6f, 1.0f, 0.1f, 0.0015f, 0.1f, 0.35f, { 0.45f, 0.5f, 0.58f, 1 }, -0.15f, 0.25f, 1.8f, 0, "luts/courtyard.cube", 0.3f, 0 },
  /* TERRACES: dusk, a low red sun, violet haze, the lamps coming on over wet slate */
  { 17.5f, 10, 250, 3.6f, 1.0f, 1.2f, 1.1f, 0.3f, 0.004f, 0.1f, 0.6f, { 0.2f, 0.16f, 0.24f, 1 }, 0.1f, 0.4f, 1.4f, 1, "luts/terraces.cube", 0.15f, 1 },
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
  { /* the sun's shadow camera stands where the light does, looking along it, and draws only what lies past its near plane:
     * at the origin (where the sun node was) every caster on the sun's side of a plane through the arena's middle was cut
     * out of the map, and that half of the arena had no sun shadow. It stands out along the sun from the arena's middle,
     * past every corner, and its square covers them all */
    const float d2r = 3.14159265f / 180.0f;
    float h = (tune("tod", m->tod) - 12) * 15 * d2r, lat = 35 * d2r, dec = m->decl * d2r; /* (sky.c sun_direction) */
    float east = -cosf(dec) * sinf(h), north = sinf(dec) * cosf(lat) - cosf(dec) * cosf(h) * sinf(lat), up = sinf(dec) * sinf(lat) + cosf(dec) * cosf(h) * cosf(lat);
    float so = sinf(m->north * d2r), co = cosf(m->north * d2r);
    t3_vec3 s = { east * co - north * so, up, -east * so - north * co };
    float sl = sqrtf(s.x * s.x + s.y * s.y + s.z * s.z);
    s = (t3_vec3){ s.x / sl, s.y / sl, s.z / sl };
    t3_vec3 c = v3scale(v3add(A.bounds_min, A.bounds_max), 0.5f), half = v3scale(v3sub(A.bounds_max, A.bounds_min), 0.5f);
    float r = sqrtf(half.x * half.x + half.y * half.y + half.z * half.z);
    /* ARENA_T_sunpos=0 (autopilot): the sun node left at the origin, for before and after */
    if (tune("sunpos", 1) > 0.5f) cw_node3d_set_position(sun, v3add(c, v3scale(s, r + 20)));
    cw_node_set(sun, "shadow_extent", cw_val_float(fmaxf(46, r + 1)));
    cw_node_set(sun, "shadow_far", cw_val_float(2 * r + 60));
  }
  cw_node_set(sky, "background", cw_val_bool(tune("skybg", 1) > 0.5f));
  cw_node_set(post, "ssao", cw_val_bool(tune("ssao", 1) > 0.5f));
  {
    static const char *DV[] = { "NONE", "DEPTH", "NORMALS", "AO", "VELOCITY", "BLOOM" };
    int dv = (int)tune("dv", 0);
    cw_node_set(post, "debug_view", cw_val_enum(DV[dv < 0 || dv > 5 ? 0 : dv]));
  }
}

/* ── the skyline past the courtyard and the terraces. A real place seen from a distance: the courtyard sits low in a valley,
 * a hill town of plastered houses with tiled roofs on the slopes round it, a church tower, cypresses and olive groves, the
 * hills closing the horizon; the terraces stand in a city at dusk, a street grid of dark blocks and towers with lit
 * windows, street lamps and red warning lamps, the tall ones against the sunset. 40 to 175 m from the middle (the cameras'
 * far plane is 220 m from anywhere in the arena), sized to show just over the wall line from the arena floor and more
 * from its heights.
 *
 * One mesh, one draw, the level's own material shape (no new program): lit by the sun and the sky like the level (vertex
 * colours), never casting shadows. Aerial perspective: each vertex fades toward the sky behind it with distance (thinner
 * with height). The fade is the vertex colour scaled down by (1 - haze) plus emitted sky light, read from a small table
 * (the emissive map) of the sky's own horizon radiance (cw_sky_radiance: the sky's model on the CPU, the
 * same sun) by bearing from the sun and haze amount; the vertex's uv picks the cell. Lit windows and lamps are cells of
 * the same table: their glow, hazed the same way. Nothing floats: every building stands sunk into the ground under its
 * lowest corner, on a stone base where the slope needs one. ── */
#include "cartwheel/environment.h"

#define SK_W 128     /* haze steps */
#define SK_ROWS 32   /* rows 0..15: the sky's haze by bearing from the sun (0 toward it, 15 away); 16..31: four glows x four bearings */
#define SK_HZ 16
enum { GLOW_WARM, GLOW_PALE, GLOW_COOL, GLOW_RED, GLOWS };

static struct {
  mesh_build b;
  float sun_az;           /* the sun's bearing (atan2(x, z)) */
  float k, hscale, fmax;  /* haze per metre past the wall, the height it thins over, the most it hides */
  int id;
} SK;

static t3_vec3 sk_v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 sk_add(t3_vec3 a, t3_vec3 b) { return (t3_vec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static t3_vec3 sk_mul(t3_vec3 a, float s) { return (t3_vec3){ a.x * s, a.y * s, a.z * s }; }
static t3_vec3 sk_norm(t3_vec3 a) { float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); return l > 1e-9f ? sk_mul(a, 1 / l) : a; }
static t3_vec3 sk_cross(t3_vec3 a, t3_vec3 b) { return (t3_vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

static float sk_haze(t3_vec3 p) {
  float d = sqrtf(p.x * p.x + p.z * p.z) - 30;
  if (d < 0) d = 0;
  return SK.fmax * (1 - expf(-d * SK.k * expf(-fmaxf(p.y, 0) / SK.hscale)));
}
static int sk_bearing(t3_vec3 p, int steps) { /* 0 toward the sun .. steps-1 away from it */
  float a = atan2f(p.x, p.z) - SK.sun_az;
  while (a > 3.14159265f) a -= 6.2831853f;
  while (a < -3.14159265f) a += 6.2831853f;
  int r = (int)(fabsf(a) / 3.14159265f * (float)(steps - 1) + 0.5f);
  return r < 0 ? 0 : r > steps - 1 ? steps - 1 : r;
}
static float sk_u(float f) { return (f * (float)(SK_W - 1) + 0.5f) / (float)SK_W; }
static float sk_vrow(int row) { return ((float)row + 0.5f) / (float)SK_ROWS; }
/* a vertex of the hull: albedo (linear) faded by the haze, the haze emitted from the table */
static int sk_vert(t3_vec3 p, t3_vec3 n, const float alb[3]) {
  float f = sk_haze(p);
  int v = mb_vert(&SK.b, p, n, sk_u(f), sk_vrow(sk_bearing(p, SK_HZ)));
  mb_color(&SK.b, v, alb[0] * (1 - f), alb[1] * (1 - f), alb[2] * (1 - f));
  return v;
}
static void sk_quad(t3_vec3 a, t3_vec3 b, t3_vec3 c, t3_vec3 d, const float alb[3]) { /* counter-clockwise seen from outside */
  t3_vec3 n = sk_norm(sk_cross(sk_add(b, sk_mul(a, -1)), sk_add(d, sk_mul(a, -1))));
  int i0 = sk_vert(a, n, alb), i1 = sk_vert(b, n, alb), i2 = sk_vert(c, n, alb), i3 = sk_vert(d, n, alb);
  mb_quad(&SK.b, i0, i1, i2, i3);
}
static void sk_tri(t3_vec3 a, t3_vec3 b, t3_vec3 c, const float alb[3]) {
  t3_vec3 n = sk_norm(sk_cross(sk_add(b, sk_mul(a, -1)), sk_add(c, sk_mul(a, -1))));
  mb_tri(&SK.b, sk_vert(a, n, alb), sk_vert(b, n, alb), sk_vert(c, n, alb));
}
/* a glowing quad (a lit window, a lamp's face): its glow and haze from the table, one cell for the whole quad */
static void sk_glow_quad(t3_vec3 a, t3_vec3 b, t3_vec3 c, t3_vec3 d, int glow) {
  t3_vec3 m = sk_mul(sk_add(sk_add(a, b), sk_add(c, d)), 0.25f);
  t3_vec3 n = sk_norm(sk_cross(sk_add(b, sk_mul(a, -1)), sk_add(d, sk_mul(a, -1))));
  float u = sk_u(sk_haze(m)), v = sk_vrow(SK_HZ + glow * 4 + sk_bearing(m, 4));
  int i[4];
  t3_vec3 p[4] = { a, b, c, d };
  for (int k = 0; k < 4; k++) {
    i[k] = mb_vert(&SK.b, p[k], n, u, v);
    mb_color(&SK.b, i[k], 0.01f, 0.01f, 0.01f);
  }
  mb_quad(&SK.b, i[0], i[1], i[2], i[3]);
}

/* a building's frame: base centre, yaw, the local axes X (along w) and Z (along d) */
typedef struct sk_frame { t3_vec3 c, X, Z; } sk_frame;
static sk_frame sk_frame_at(float x, float y, float z, float yaw) {
  float s = sinf(yaw), co = cosf(yaw);
  return (sk_frame){ sk_v3(x, y, z), sk_v3(co, 0, -s), sk_v3(s, 0, co) };
}
static t3_vec3 sk_at(const sk_frame *f, float lx, float ly, float lz) {
  return sk_add(f->c, sk_add(sk_mul(f->X, lx), sk_add(sk_v3(0, ly, 0), sk_mul(f->Z, lz))));
}
/* an upright box w x d from y0 to y1 (frame-relative heights), sides and top */
static void sk_box(const sk_frame *f, float ox, float oz, float w, float d, float y0, float y1, const float alb[3], bool top) {
  float hx = w / 2, hz = d / 2;
  t3_vec3 c[8];
  for (int k = 0; k < 8; k++) c[k] = sk_at(f, ox + ((k & 1) ? hx : -hx), (k & 4) ? y1 : y0, oz + ((k & 2) ? hz : -hz));
  /* corners: bit0 +x, bit1 +z, bit2 top */
  sk_quad(c[3], c[1], c[5], c[7], alb); /* +x: from +z to -z */
  sk_quad(c[0], c[2], c[6], c[4], alb); /* -x */
  sk_quad(c[2], c[3], c[7], c[6], alb); /* +z */
  sk_quad(c[1], c[0], c[4], c[5], alb); /* -z */
  if (top) sk_quad(c[6], c[7], c[5], c[4], alb);
}
/* a roof over a w x d footprint whose walls end at yt: gable (ridge along x), hip, or a pyramid (hip on a square) */
static void sk_roof(const sk_frame *f, float w, float d, float yt, float pitch, bool hip, const float tile[3], const float wall[3]) {
  const float o = 0.45f, tp = tanf(pitch), rise = d * 0.5f * tp, ye = yt - o * tp;
  const float under[3] = { tile[0] * 0.3f, tile[1] * 0.3f, tile[2] * 0.3f };
  float rx = hip ? fmaxf(w * 0.5f - d * 0.5f, 0) : w * 0.5f + o; /* the ridge's half length */
  t3_vec3 r0 = sk_at(f, -rx, yt + rise, 0), r1 = sk_at(f, rx, yt + rise, 0);
  t3_vec3 e0 = sk_at(f, -w / 2 - o, ye, d / 2 + o), e1 = sk_at(f, w / 2 + o, ye, d / 2 + o);
  t3_vec3 b0 = sk_at(f, -w / 2 - o, ye, -d / 2 - o), b1 = sk_at(f, w / 2 + o, ye, -d / 2 - o);
  sk_quad(e0, e1, r1, r0, tile);      /* the front slope */
  sk_quad(b1, b0, r0, r1, tile);      /* the back slope */
  sk_quad(r0, r1, e1, e0, under);     /* the eaves seen from below */
  sk_quad(r1, r0, b0, b1, under);
  if (hip) {
    sk_tri(e1, b1, r1, tile);         /* the hipped ends */
    sk_tri(b0, e0, r0, tile);
  } else { /* the gable ends, in the wall's colour */
    sk_tri(sk_at(f, w / 2, yt, d / 2), sk_at(f, w / 2, yt, -d / 2), sk_at(f, w / 2, yt + rise, 0), wall);
    sk_tri(sk_at(f, -w / 2, yt, -d / 2), sk_at(f, -w / 2, yt, d / 2), sk_at(f, -w / 2, yt + rise, 0), wall);
  }
}
/* the faces of a box that look back toward the arena (others are never seen from it): calls fn per face with its frame */
typedef struct sk_face { t3_vec3 o, r, n; float w; } sk_face; /* bottom centre, right (seen from outside), out, width */
static int sk_faces_toward(const sk_frame *f, float w, float d, sk_face out[4]) {
  int n = 0;
  sk_face all[4] = {
    { sk_at(f, w / 2, 0, 0), sk_mul(f->Z, -1), f->X, d }, { sk_at(f, -w / 2, 0, 0), f->Z, sk_mul(f->X, -1), d },
    { sk_at(f, 0, 0, d / 2), f->X, f->Z, w }, { sk_at(f, 0, 0, -d / 2), sk_mul(f->X, -1), sk_mul(f->Z, -1), w },
  };
  for (int k = 0; k < 4; k++) {
    t3_vec3 to = sk_norm(sk_v3(-all[k].o.x, 0, -all[k].o.z));
    if (all[k].n.x * to.x + all[k].n.z * to.z > 0.1f) out[n++] = all[k];
  }
  return n;
}
static void sk_rect(const sk_face *fc, float x, float y, float w, float h, float out, const float *alb, int glow) {
  t3_vec3 o = sk_add(fc->o, sk_mul(fc->n, out));
  t3_vec3 a = sk_add(o, sk_add(sk_mul(fc->r, x - w / 2), sk_v3(0, y, 0))), b = sk_add(o, sk_add(sk_mul(fc->r, x + w / 2), sk_v3(0, y, 0)));
  t3_vec3 c = sk_add(b, sk_v3(0, h, 0)), d = sk_add(a, sk_v3(0, h, 0));
  if (glow >= 0) sk_glow_quad(a, b, c, d, glow);
  else sk_quad(a, b, c, d, alb);
}

static float sk_rnd(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return (float)(*s >> 8) / 16777216.0f; }
static float sk_lin1(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
static void sk_srgb(float r, float g, float b, float out[3]) { out[0] = sk_lin1(r), out[1] = sk_lin1(g), out[2] = sk_lin1(b); }

/* ── the courtyard's valley: the ground's height (smooth hills, a ring of higher ridges to close the horizon) ── */
static float sk_hill(float x, float z) {
  float r = sqrtf(x * x + z * z), a = atan2f(x, z);
  if (r < 44) return -0.3f;
  float slope = 0.17f + 0.09f * sinf(2 * a + 0.7f) + 0.06f * sinf(3 * a + 2.1f) + 0.03f * sinf(5 * a + 0.4f);
  float t = (r - 44) / 30;
  float ease = t < 1 ? t * t * (3 - 2 * t) : 1;                         /* the valley floor curves up into the slopes */
  float h = (r - 44) * slope * ease;
  h += (r > 140 ? (r - 140) * (0.45f + 0.25f * sinf(4 * a + 1.3f)) : 0); /* the far ridges */
  h += ease * (2.2f * sinf(x * 0.071f + 0.4f) * sinf(z * 0.063f + 1.1f) + 1.1f * sinf(x * 0.17f + z * 0.13f));
  return h - 0.3f;
}
static float sk_city(float x, float z) { (void)x, (void)z; return -0.3f; }

/* the ground: a ring from just past the walls to the edge, its colour from fields and groves (courtyard) or the city's
 * roofs and streets (terraces) */
static void sk_ground(float (*hf)(float, float), bool town) {
  const int NA = 128, NR = 34;
  float rad[NR + 1];
  for (int j = 0; j <= NR; j++) { float t = (float)j / NR; rad[j] = 33 + 142 * t * (0.55f + 0.45f * t); }
  int base = SK.b.nv;
  for (int j = 0; j <= NR; j++)
    for (int i = 0; i <= NA; i++) {
      float a = 6.2831853f * (float)i / NA, r = rad[j], x = sinf(a) * r, z = cosf(a) * r, e = 0.6f;
      t3_vec3 p = sk_v3(x, hf(x, z), z);
      t3_vec3 n = sk_norm(sk_v3(hf(x - e, z) - hf(x + e, z), 2 * e, hf(x, z - e) - hf(x, z + e)));
      float c[3];
      if (town) { /* fields, groves and dry grass in patches, darker in the folds */
        float m = 0.5f + 0.5f * sinf(x * 0.05f + 0.3f) * sinf(z * 0.043f + 1.7f), q = 0.5f + 0.5f * sinf(x * 0.11f - z * 0.09f + 0.8f);
        float g[3], dry[3], field[3];
        sk_srgb(0.27f, 0.32f, 0.15f, g), sk_srgb(0.48f, 0.43f, 0.27f, dry), sk_srgb(0.40f, 0.30f, 0.19f, field);
        for (int k = 0; k < 3; k++) c[k] = g[k] * m + dry[k] * (1 - m) * (1 - q * 0.6f) + field[k] * (1 - m) * q * 0.6f;
      } else sk_srgb(0.07f, 0.07f, 0.08f, c);
      sk_vert(p, n, c);
    }
  for (int j = 0; j < NR; j++)
    for (int i = 0; i < NA; i++) {
      int v0 = base + j * (NA + 1) + i;
      mb_quad(&SK.b, v0, v0 + NA + 1, v0 + NA + 2, v0 + 1); /* seen from above */
    }
}
static float sk_lowest(float (*hf)(float, float), const sk_frame *f, float w, float d, float *hi) {
  float lo = 1e9f, h2 = -1e9f;
  for (int k = 0; k < 9; k++) {
    t3_vec3 p = sk_at(f, (float)(k % 3 - 1) * w / 2, 0, (float)(k / 3 - 1) * d / 2);
    float y = hf(p.x, p.z);
    lo = fminf(lo, y), h2 = fmaxf(h2, y);
  }
  *hi = h2;
  return lo;
}

/* a house on the slope: a stone base where the ground falls away, plastered walls, small dark windows (or shutters), a
 * tiled roof, a chimney now and then */
typedef struct sk_spot { float x, z, r; } sk_spot;
static sk_spot SPOT[400];
static int nspot;
static bool sk_free(float x, float z, float r) {
  if (x * x + z * z > 172 * 172 || fabsf(x) < 36 + r && fabsf(z) < 36 + r) return false;
  for (int i = 0; i < nspot; i++) {
    float dx = SPOT[i].x - x, dz = SPOT[i].z - z, m = SPOT[i].r + r;
    if (dx * dx + dz * dz < m * m) return false;
  }
  return true;
}
static void sk_house(uint32_t *rs, float x, float z, float w, float d, float storeys, float yaw) {
  static const float PL[6][3] = { { 0.86f, 0.78f, 0.62f }, { 0.80f, 0.62f, 0.42f }, { 0.90f, 0.86f, 0.78f }, { 0.78f, 0.56f, 0.48f },
                                  { 0.72f, 0.66f, 0.52f }, { 0.84f, 0.72f, 0.50f } };
  static const float TL[4][3] = { { 0.60f, 0.30f, 0.20f }, { 0.55f, 0.33f, 0.24f }, { 0.66f, 0.38f, 0.25f }, { 0.48f, 0.28f, 0.22f } };
  float wall[3], tile[3], stone[3], dark[3], shut[3];
  const float *pl = PL[(int)(sk_rnd(rs) * 6) % 6], *tl = TL[(int)(sk_rnd(rs) * 4) % 4];
  float v = 0.9f + sk_rnd(rs) * 0.15f;
  sk_srgb(pl[0] * v, pl[1] * v, pl[2] * v, wall), sk_srgb(tl[0], tl[1], tl[2], tile);
  sk_srgb(0.52f, 0.48f, 0.42f, stone), sk_srgb(0.07f, 0.06f, 0.06f, dark);
  if (sk_rnd(rs) < 0.5f) sk_srgb(0.30f, 0.38f, 0.30f, shut); else sk_srgb(0.42f, 0.30f, 0.20f, shut);
  sk_frame f = sk_frame_at(x, 0, z, yaw);
  float hi, lo = sk_lowest(sk_hill, &f, w, d, &hi);
  float floor0 = hi + 0.15f, h = storeys * 3.0f + 0.4f;
  if (hi - lo > 0.6f) sk_box(&f, 0, 0, w + 0.5f, d + 0.5f, lo - 0.6f, floor0, stone, true); /* the terrace it stands on */
  sk_box(&f, 0, 0, w, d, lo - 0.6f, floor0 + h, wall, false);
  bool hip = sk_rnd(rs) < 0.4f;
  sk_roof(&f, w, d, floor0 + h, 0.42f + sk_rnd(rs) * 0.1f, hip, tile, wall);
  if (sk_rnd(rs) < 0.45f) sk_box(&f, w * 0.25f, -d * 0.2f, 0.7f, 0.7f, floor0 + h, floor0 + h + d * 0.5f * 0.5f + 1.2f, wall, true);
  sk_face fc[4];
  int nf = sk_faces_toward(&f, w, d, fc);
  bool shutters = sk_rnd(rs) < 0.35f;
  for (int k = 0; k < nf; k++) {
    int cols = (int)(fc[k].w / 2.6f);
    if (cols < 1) continue;
    for (int s = 0; s < (int)storeys; s++)
      for (int c = 0; c < cols; c++) {
        if (sk_rnd(rs) < 0.15f) continue;
        float cx = -fc[k].w / 2 + fc[k].w * ((float)c + 0.5f) / (float)cols, y = floor0 + 0.9f + (float)s * 3.0f;
        sk_rect(&fc[k], cx, y, 0.85f, 1.3f, 0.06f, shutters ? shut : dark, -1);
      }
  }
  if (nspot < 400) SPOT[nspot++] = (sk_spot){ x, z, 0.5f * sqrtf(w * w + d * d) + 1.0f };
}
/* an upright frustum of n sides (smooth), base at y0; a cap when it does not close to a point */
static void sk_cyl_v(float x, float y0, float z, float r0, float r1, float h, int n, const float alb[3]) {
  float sl = (r0 - r1) / h;
  for (int i = 0; i < n; i++) {
    float a0 = 6.2831853f * (float)i / (float)n, a1 = 6.2831853f * (float)(i + 1) / (float)n;
    t3_vec3 n0 = sk_norm(sk_v3(sinf(a0), sl, cosf(a0))), n1 = sk_norm(sk_v3(sinf(a1), sl, cosf(a1)));
    int v0 = sk_vert(sk_v3(x + sinf(a0) * r0, y0, z + cosf(a0) * r0), n0, alb), v1 = sk_vert(sk_v3(x + sinf(a1) * r0, y0, z + cosf(a1) * r0), n1, alb);
    int v2 = sk_vert(sk_v3(x + sinf(a1) * r1, y0 + h, z + cosf(a1) * r1), n1, alb), v3i = sk_vert(sk_v3(x + sinf(a0) * r1, y0 + h, z + cosf(a0) * r1), n0, alb);
    mb_quad(&SK.b, v0, v1, v2, v3i);
    if (r1 > 0.1f) {
      int c = sk_vert(sk_v3(x, y0 + h, z), sk_v3(0, 1, 0), alb);
      mb_tri(&SK.b, c, sk_vert(sk_v3(x + sinf(a0) * r1, y0 + h, z + cosf(a0) * r1), sk_v3(0, 1, 0), alb), sk_vert(sk_v3(x + sinf(a1) * r1, y0 + h, z + cosf(a1) * r1), sk_v3(0, 1, 0), alb));
    }
  }
}
static void sk_tree(uint32_t *rs, float x, float z, bool cypress) {
  float g[3], trunk[3];
  float y = sk_hill(x, z) - 0.4f;
  sk_srgb(0.30f, 0.24f, 0.18f, trunk);
  if (cypress) {
    sk_srgb(0.10f + sk_rnd(rs) * 0.03f, 0.17f + sk_rnd(rs) * 0.04f, 0.09f, g);
    float h = 9 + sk_rnd(rs) * 6, r = 0.9f + sk_rnd(rs) * 0.4f;
    sk_cyl_v(x, y, z, r * 0.7f, r, h * 0.35f, 7, g);
    sk_cyl_v(x, y + h * 0.35f, z, r, 0.05f, h * 0.65f, 7, g);
  } else { /* an olive or a stone pine: a short trunk and a round, flattened crown */
    sk_srgb(0.22f + sk_rnd(rs) * 0.06f, 0.27f + sk_rnd(rs) * 0.05f, 0.14f, g);
    float r = 2.0f + sk_rnd(rs) * 1.6f, t = 1.4f + sk_rnd(rs);
    sk_cyl_v(x, y, z, 0.25f, 0.2f, t + 0.5f, 5, trunk);
    sk_cyl_v(x, y + t, z, r * 0.45f, r, r * 0.45f, 8, g);
    sk_cyl_v(x, y + t + r * 0.45f, z, r, r * 0.3f, r * 0.55f, 8, g);
  }
}

/* the courtyard's hill town: three villages up the slopes (the church in the biggest), farmhouses between, olive groves,
 * cypresses in lines along the lanes and round the farms, trees scattered over the hills */
static void sk_church(uint32_t *rs, float x, float z) {
  float stone[3], tile[3], dark[3], plaster[3];
  sk_srgb(0.74f, 0.68f, 0.56f, stone), sk_srgb(0.58f, 0.31f, 0.21f, tile), sk_srgb(0.06f, 0.05f, 0.05f, dark), sk_srgb(0.84f, 0.78f, 0.64f, plaster);
  float yaw = atan2f(-x, -z);
  sk_frame f = sk_frame_at(x, 0, z, yaw);
  float hi, lo = sk_lowest(sk_hill, &f, 6, 6, &hi), top = hi + 23;
  sk_box(&f, 0, 0, 6, 6, lo - 0.6f, top, stone, false);
  sk_box(&f, 0, 0, 6.6f, 6.6f, top - 0.1f, top + 0.5f, stone, true); /* the cornice */
  sk_roof(&f, 6.6f, 6.6f, top + 0.5f, 0.95f, true, tile, stone);       /* a pyramid cap */
  sk_face fc[4];
  int nf = sk_faces_toward(&f, 6, 6, fc);
  for (int k = 0; k < nf; k++) /* the belfry: two tall openings a side, a clock below */
    for (int s = -1; s <= 1; s += 2) sk_rect(&fc[k], (float)s * 1.3f, top - 5.2f, 1.2f, 3.0f, 0.06f, dark, -1);
  for (int k = 0; k < nf; k++) sk_rect(&fc[k], 0, top - 9.5f, 1.6f, 1.6f, 0.06f, plaster, -1);
  /* the nave beside it: long, a gable roof, a round window over the door */
  sk_frame n = sk_frame_at(x + f.X.x * 12, 0, z + f.X.z * 12, yaw);
  float nhi, nlo = sk_lowest(sk_hill, &n, 18, 10, &nhi);
  if (nhi - nlo > 0.6f) sk_box(&n, 0, 0, 18.6f, 10.6f, nlo - 0.6f, nhi + 0.2f, stone, true);
  sk_box(&n, 0, 0, 18, 10, nlo - 0.6f, nhi + 11, plaster, false);
  sk_roof(&n, 18, 10, nhi + 11, 0.45f, false, tile, plaster);
  int nn = sk_faces_toward(&n, 18, 10, fc);
  for (int k = 0; k < nn; k++)
    for (int c = 0; c < (fc[k].w > 12 ? 4 : 1); c++) {
      float cx = fc[k].w > 12 ? -6 + 4.0f * (float)c : 0;
      sk_rect(&fc[k], cx, nhi + 5.0f, 1.1f, 3.2f, 0.06f, dark, -1);
    }
  if (nspot < 398) SPOT[nspot++] = (sk_spot){ x, z, 6 }, SPOT[nspot++] = (sk_spot){ n.c.x, n.c.z, 11 };
  (void)rs;
}
static void sk_town(void) {
  uint32_t rs = 0x70E1u;
  nspot = 0;
  static const float VIL[3][5] = { /* bearing, spread (rad), nearest, farthest, houses */
    { 0.9f, 0.42f, 72, 150, 70 }, { 3.75f, 0.38f, 70, 140, 50 }, { 5.35f, 0.25f, 95, 160, 24 } };
  sk_church(&rs, sinf(0.95f) * 112, cosf(0.95f) * 112);
  for (int v = 0; v < 3; v++)
    for (int i = 0, made = 0; i < (int)VIL[v][4] * 6 && made < (int)VIL[v][4]; i++) {
      float t = sk_rnd(&rs);
      float a = VIL[v][0] + (sk_rnd(&rs) + sk_rnd(&rs) - 1) * VIL[v][1], r = VIL[v][2] + (VIL[v][3] - VIL[v][2]) * t * t;
      float x = sinf(a) * r, z = cosf(a) * r, w = 6 + sk_rnd(&rs) * 5, d = 5.5f + sk_rnd(&rs) * 3.5f;
      if (!sk_free(x, z, 0.5f * sqrtf(w * w + d * d) + 0.8f)) continue;
      float yaw = atan2f(-x, -z) + (sk_rnd(&rs) - 0.5f) * 0.35f + (sk_rnd(&rs) < 0.3f ? 1.5708f : 0);
      float storeys = 2 + (sk_rnd(&rs) < 0.45f ? 1 : 0) + (t < 0.3f && sk_rnd(&rs) < 0.3f ? 1 : 0);
      sk_house(&rs, x, z, w, d, storeys, yaw);
      made++;
    }
  for (int i = 0; i < 16; i++) { /* farmhouses with their cypresses */
    float a = 6.2831853f * ((float)i + sk_rnd(&rs) * 0.6f) / 16.0f, r = 85 + sk_rnd(&rs) * 75, x = sinf(a) * r, z = cosf(a) * r;
    if (!sk_free(x, z, 9)) continue;
    sk_house(&rs, x, z, 9 + sk_rnd(&rs) * 5, 7 + sk_rnd(&rs) * 2, 2, atan2f(-x, -z) + (sk_rnd(&rs) - 0.5f) * 0.6f);
    for (int k = 0; k < 4; k++) {
      float ta = a + 0.06f + 0.035f * (float)k, tx = sinf(ta) * (r + 3), tz = cosf(ta) * (r + 3);
      if (sk_free(tx, tz, 1.2f)) { sk_tree(&rs, tx, tz, true); SPOT[nspot < 400 ? nspot++ : 399] = (sk_spot){ tx, tz, 1.2f }; }
    }
  }
  for (int l = 0; l < 7; l++) { /* cypresses along the lanes up the hills */
    float a = 6.2831853f * (float)l / 7 + 0.4f, r0 = 55 + sk_rnd(&rs) * 30;
    for (int k = 0; k < 10; k++) {
      float r = r0 + (float)k * 6, x = sinf(a) * r + cosf(a) * 3, z = cosf(a) * r - sinf(a) * 3;
      if (sk_free(x, z, 1.2f)) { sk_tree(&rs, x, z, true); SPOT[nspot < 400 ? nspot++ : 399] = (sk_spot){ x, z, 1.2f }; }
    }
  }
  for (int g = 0; g < 9; g++) { /* olive groves: rows on the slopes */
    float a = 6.2831853f * ((float)g + 0.5f) / 9 + (sk_rnd(&rs) - 0.5f) * 0.3f, r = 70 + sk_rnd(&rs) * 70;
    float cx = sinf(a) * r, cz = cosf(a) * r, ra = sk_rnd(&rs) * 1.5f;
    for (int i = -3; i <= 3; i++)
      for (int j = -2; j <= 2; j++) {
        float lx = (float)i * 7 + (sk_rnd(&rs) - 0.5f) * 2, lz = (float)j * 7 + (sk_rnd(&rs) - 0.5f) * 2;
        float x = cx + cosf(ra) * lx + sinf(ra) * lz, z = cz - sinf(ra) * lx + cosf(ra) * lz;
        if (sk_free(x, z, 2.5f)) sk_tree(&rs, x, z, false);
      }
  }
  for (int i = 0; i < 90; i++) { /* trees over the hills */
    float a = sk_rnd(&rs) * 6.2831853f, r = 60 + sk_rnd(&rs) * 110, x = sinf(a) * r, z = cosf(a) * r;
    if (sk_free(x, z, 3)) sk_tree(&rs, x, z, sk_rnd(&rs) < 0.35f);
  }
}

/* the terraces' city: a street grid of blocks (the arena's walls run along it), low near the arena, higher further out,
 * the towers downtown toward the sunset; offices with whole floors lit, flats with scattered lit windows, unlit panes on
 * the masonry blocks, street lamps on poles, red warning lamps and a lit crown on the tallest */
static void sk_glow_box(float x, float y, float z, float s, int glow) {
  sk_frame f = sk_frame_at(x, y, z, 0);
  t3_vec3 c[8];
  for (int k = 0; k < 8; k++) c[k] = sk_at(&f, (k & 1) ? s : -s, (k & 4) ? s * 2 : 0, (k & 2) ? s : -s);
  sk_glow_quad(c[3], c[1], c[5], c[7], glow), sk_glow_quad(c[0], c[2], c[6], c[4], glow);
  sk_glow_quad(c[2], c[3], c[7], c[6], glow), sk_glow_quad(c[1], c[0], c[4], c[5], glow);
}
static void sk_building(uint32_t *rs, float x, float z, float w, float d, float h, bool downtown, float *tallest, t3_vec3 *top_at) {
  static const float COL[5][3] = { { 0.07f, 0.08f, 0.10f }, { 0.20f, 0.19f, 0.19f }, { 0.18f, 0.12f, 0.10f }, { 0.26f, 0.24f, 0.22f }, { 0.14f, 0.15f, 0.16f } };
  int kind = downtown && sk_rnd(rs) < 0.6f ? 0 : 1 + (int)(sk_rnd(rs) * 4) % 4;
  bool office = kind == 0 || kind == 4 || (kind == 3 && sk_rnd(rs) < 0.5f);
  float alb[3], pane[3], dark[3];
  float v = 0.85f + sk_rnd(rs) * 0.3f;
  sk_srgb(COL[kind][0] * v, COL[kind][1] * v, COL[kind][2] * v, alb), sk_srgb(0.05f, 0.06f, 0.08f, pane), sk_srgb(0.08f, 0.08f, 0.09f, dark);
  float lit_floor = office ? 0.15f + sk_rnd(rs) * 0.25f : 0.04f, lit_win = office ? 0.08f : 0.12f + sk_rnd(rs) * 0.28f;
  int glow = office ? (sk_rnd(rs) < 0.75f ? GLOW_COOL : GLOW_PALE) : (sk_rnd(rs) < 0.7f ? GLOW_WARM : GLOW_PALE);
  /* a podium and a tower set back on it, or one block */
  int parts = h > 26 && sk_rnd(rs) < 0.6f ? 2 : 1;
  float y0 = -0.8f;
  for (int p = 0; p < parts; p++) {
    float pw = p ? w * (0.55f + sk_rnd(rs) * 0.2f) : w, pd = p ? d * (0.55f + sk_rnd(rs) * 0.2f) : d;
    float y1 = parts == 2 && !p ? h * (0.25f + sk_rnd(rs) * 0.15f) : h;
    sk_frame f = sk_frame_at(x, 0, z, 0);
    sk_box(&f, 0, 0, pw, pd, y0, y1, alb, true);
    sk_face fc[4];
    int nf = sk_faces_toward(&f, pw, pd, fc);
    for (int k = 0; k < nf; k++) {
      int cols = (int)(fc[k].w / 3.0f);
      for (float fy = fmaxf(y0 + 3.0f, 3.0f); fy < y1 - 1.6f; fy += 3.4f) {
        bool floor_on = sk_rnd(rs) < lit_floor;
        if (floor_on && office) { /* a whole lit floor: one band */
          sk_rect(&fc[k], 0, fy, fc[k].w - 1.2f, 1.5f, 0.08f, NULL, glow);
          continue;
        }
        for (int c = 0; c < cols; c++) {
          float cx = -fc[k].w / 2 + fc[k].w * ((float)c + 0.5f) / (float)cols;
          if (floor_on || sk_rnd(rs) < lit_win) sk_rect(&fc[k], cx, fy, 1.8f, 1.5f, 0.08f, NULL, glow);
          else if (kind != 0) sk_rect(&fc[k], cx, fy, 1.8f, 1.5f, 0.06f, pane, -1);
        }
      }
    }
    if (p == parts - 1) {
      if (sk_rnd(rs) < 0.5f) sk_box(&f, pw * 0.15f, -pd * 0.1f, pw * 0.35f, pd * 0.35f, y1 - 0.1f, y1 + 3, dark, true); /* plant room */
      if (h > *tallest) *tallest = h, *top_at = sk_v3(x, y1, z);
      if (h > 36) sk_glow_box(x + pw * 0.4f, y1, z + pd * 0.4f, 0.35f, GLOW_RED);
    }
    y0 = y1 - 0.1f;
  }
}
static void sk_city_build(void) {
  uint32_t rs = 0xC17Eu;
  float tallest = 0;
  t3_vec3 top = { 0 };
  for (int gx = -6; gx <= 5; gx++)
    for (int gz = -6; gz <= 5; gz++) {
      float cx = 15 + 30 * (float)gx, cz = 15 + 30 * (float)gz;
      if (fabsf(cx) - 11 < 31 && fabsf(cz) - 11 < 31) continue;            /* the arena */
      float fx = fabsf(cx) + 11, fz = fabsf(cz) + 11;
      if (fx * fx + fz * fz > 172 * 172) continue;                          /* past the far plane's reach */
      float r = sqrtf(cx * cx + cz * cz), a = atan2f(cx, cz) - SK.sun_az;
      while (a > 3.14159265f) a -= 6.2831853f;
      while (a < -3.14159265f) a += 6.2831853f;
      bool downtown = fabsf(a) < 0.95f && r > 90;
      int lots = 1 + (int)(sk_rnd(&rs) * 3);
      bool along_x = sk_rnd(&rs) < 0.5f;
      for (int l = 0; l < lots; l++) {
        float span = 22.0f / (float)lots, off = -11 + span * ((float)l + 0.5f);
        float w = along_x ? span - 1.5f - sk_rnd(&rs) * 2 : 18 + sk_rnd(&rs) * 3, d = along_x ? 18 + sk_rnd(&rs) * 3 : span - 1.5f - sk_rnd(&rs) * 2;
        float x = cx + (along_x ? off : 0), z = cz + (along_x ? 0 : off);
        float h = r < 70 ? 5 + sk_rnd(&rs) * 6 : 10 + sk_rnd(&rs) * 14;
        if (downtown) h = 24 + sk_rnd(&rs) * 20 + (sk_rnd(&rs) < 0.15f ? 12 : 0);
        else if (r > 80 && sk_rnd(&rs) < 0.08f) h = 30 + sk_rnd(&rs) * 10;
        sk_building(&rs, x, z, w, d, h, downtown, &tallest, &top);
      }
    }
  if (tallest > 0) { /* the tallest: a mast with a warning lamp, and a lit crown under its roof */
    float dark[3];
    sk_srgb(0.12f, 0.12f, 0.13f, dark);
    sk_frame f = sk_frame_at(top.x, 0, top.z, 0);
    sk_box(&f, 0, 0, 0.5f, 0.5f, top.y - 0.2f, top.y + 9, dark, true);
    sk_glow_box(top.x, top.y + 9, top.z, 0.4f, GLOW_RED);
  }
  for (int sx = -6; sx <= 6; sx++) /* street lamps: poles both sides of each street, every 12 m */
    for (int side = -1; side <= 1; side += 2)
      for (int axis = 0; axis < 2; axis++)
        for (float t = -170; t <= 170; t += 12) {
          float s = 30 * (float)sx + 3.2f * (float)side, x = axis ? s : t, z = axis ? t : s;
          if (fabsf(x) < 33 && fabsf(z) < 33) continue;
          if (x * x + z * z > 170 * 170 || x * x + z * z < 36 * 36) continue;
          float pole[3];
          sk_srgb(0.1f, 0.1f, 0.1f, pole);
          sk_frame f = sk_frame_at(x, 0, z, 0);
          sk_box(&f, 0, 0, 0.18f, 0.18f, -0.5f, 5.6f, pole, false);
          sk_glow_box(x, 5.6f, z, 0.3f, GLOW_WARM);
        }
}

static void skyline(int id) {
  const mood *m = &MOODS[id];
  /* the sun, as the sky places it (sky.c sun_direction, latitude 35) */
  const float d2r = 3.14159265f / 180.0f;
  float h = (tune("tod", m->tod) - 12) * 15 * d2r, lat = 35 * d2r, dec = m->decl * d2r;
  float east = -cosf(dec) * sinf(h), north = sinf(dec) * cosf(lat) - cosf(dec) * cosf(h) * sinf(lat), up = sinf(dec) * sinf(lat) + cosf(dec) * cosf(h) * cosf(lat);
  t3_vec3 v = { east, up, -north }, sun;
  float so = sinf(m->north * d2r), co = cosf(m->north * d2r);
  sun = sk_norm(sk_v3(v.x * co + v.z * so, v.y, -v.x * so + v.z * co));
  memset(&SK, 0, sizeof SK);
  SK.id = id;
  SK.sun_az = atan2f(sun.x, sun.z);
  if (id == 1) SK.k = tune("hzk", 1 / 160.0f), SK.hscale = 140, SK.fmax = tune("hzmax", 0.85f); /* morning: the far ridges pale toward the sky */
  else SK.k = tune("hzk", 1 / 200.0f), SK.hscale = 110, SK.fmax = tune("hzmax", 0.6f); /* dusk: the towers toward the sunset stay dark against it */
  sk_ground(id == 1 ? sk_hill : sk_city, id == 1);
  if (id == 1) sk_town();
  else sk_city_build();

  /* the table: the sky's horizon radiance by bearing from the sun times the haze; the glows hazed the same way */
  static float L[SK_ROWS][SK_W][3];
  const float e0 = tune("sun", m->sun) * tune("sky", m->sky) * tune("hazeE", 1);
  const float g = tune("glow", 2.2f);
  static const float GL[GLOWS][3] = { { 1.0f, 0.55f, 0.22f }, { 1.0f, 0.82f, 0.55f }, { 0.62f, 0.78f, 1.0f }, { 2.0f, 0.12f, 0.06f } };
  float E = 1e-4f;
  for (int row = 0; row < SK_ROWS; row++) {
    bool hz = row < SK_HZ;
    int bearing = hz ? row : (row - SK_HZ) % 4, steps = hz ? SK_HZ : 4, gl = hz ? 0 : (row - SK_HZ) / 4;
    float az = SK.sun_az + 3.14159265f * (float)bearing / (float)(steps - 1), el = 0.05f;
    t3_color c = cw_sky_radiance(sk_v3(sinf(az) * cosf(el), sinf(el), cosf(az) * cosf(el)), sun, m->rayleigh, m->mie, 0.76f);
    float sky[3] = { c.r * e0 + 0.0004f, c.g * e0 + 0.0006f, c.b * e0 + 0.0012f };
    for (int i = 0; i < SK_W; i++) {
      float f = (float)i / (float)(SK_W - 1);
      for (int k = 0; k < 3; k++) {
        L[row][i][k] = hz ? sky[k] * f : GL[gl][k] * g * (1 - f) + sky[k] * f;
        if (L[row][i][k] > E) E = L[row][i][k];
      }
    }
  }
  uint8_t *px = malloc(SK_W * SK_ROWS * 4);
  for (int row = 0; row < SK_ROWS; row++)
    for (int i = 0; i < SK_W; i++)
      for (int k = 0; k < 4; k++) {
        float c = k < 3 ? L[row][i][k] / E : 1;
        c = c <= 0.0031308f ? 12.92f * c : 1.055f * powf(c, 1 / 2.4f) - 0.055f;
        px[(row * SK_W + i) * 4 + k] = (uint8_t)(fminf(fmaxf(c, 0), 1) * 255 + 0.5f);
      }
  t3_texture *lut = t3_texture_new(SK_W, SK_ROWS, px);
  free(px);
  lut->wrap_s = lut->wrap_t = T3_CLAMP_TO_EDGE;
  lut->flip_y = false;
  lut->generate_mipmaps = false;
  lut->min_filter = lut->mag_filter = T3_LINEAR; /* each cell read at its centre: no blend with the next row */
  lut->color_space = T3_SRGB_COLOR_SPACE;
  /* plain maps in the level's slots (the same program as the level's lit windows): white, flat, rough, unoccluded */
  uint8_t white[16 * 4], flat[16 * 4];
  for (int i = 0; i < 16; i++) white[i * 4] = white[i * 4 + 1] = white[i * 4 + 2] = white[i * 4 + 3] = 255, flat[i * 4] = flat[i * 4 + 1] = 128, flat[i * 4 + 2] = flat[i * 4 + 3] = 255;
  t3_texture *tw = t3_texture_new(4, 4, white), *tn = t3_texture_new(4, 4, flat), *tr = t3_texture_new(4, 4, white);
  t3_texture *ts[3] = { tw, tn, tr };
  for (int i = 0; i < 3; i++) ts[i]->wrap_s = ts[i]->wrap_t = T3_REPEAT, ts[i]->flip_y = false, ts[i]->generate_mipmaps = false,
                             ts[i]->min_filter = ts[i]->mag_filter = T3_LINEAR;
  tw->color_space = T3_SRGB_COLOR_SPACE;
  t3_material *mt = t3_mesh_physical_material_new(0xffffff);
  mt->vertex_colors = true;
  mt->roughness = 0.9f;
  mt->metalness = 0;
  t3_material_set_texture(mt, T3_MAP, tw);
  t3_material_set_texture(mt, T3_NORMAL_MAP, tn);
  t3_material_set_texture(mt, T3_ROUGHNESS_MAP, tr);
  t3_material_set_texture(mt, T3_AO_MAP, tw);
  t3_material_set_texture(mt, T3_EMISSIVE_MAP, lut);
  mt->normal_scale = (t3_vec2){ 0, 0 };
  mt->ao_map_intensity = 0;
  mt->emissive = (t3_color){ E, E, E };
  mt->emissive_intensity = 1;
  for (int i = 0; i < 3; i++) t3_release(ts[i]);
  t3_release(lut);
  cw_info("arena %s: skyline %d vertices, %d triangles", ARENA_INFO[id].name, SK.b.nv, SK.b.ni / 3);
  t3_geometry *geo = mb_finish(&SK.b);
  t3_mesh *mesh = t3_mesh_new(geo, mt);
  mesh->base.cast_shadow = false;
  mesh->base.receive_shadow = true; /* out past the shadow's reach it reads as lit; kept for the level's program */
  cw_node n = cw_node_new("MeshInstance3D", "Skyline");
  cw_node_add_child(A.root, n);
  cw_mesh3d_set_procedural(n, mesh);
  t3_release(geo);
  t3_release(mt);
  arena_release_mesh(mesh);
}
