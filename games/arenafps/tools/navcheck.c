/* arenafps_navcheck: can a bot walk from every command post to every other? Builds a map with the game's own arena code
 * (src/arena.c: the same level pieces, props, collision and navmesh the game builds), then asks the navmesh for a path
 * between every pair of posts from levels/<map>.lvl and prints one JSON line:
 *   {"arena":3,"posts":5,"pairs":10,"whole":10,"polys":...,"worst_m":...,"pairs_detail":[...]}
 * A pair counts when the path is whole (not partial) and ends within 1.5 m of the far post. The control,
 * NAVCHECK_SEAL=<post>, stands a closed wall round that post (inside its ring, 2 m high) and builds the navmesh again
 * with the game's config: every pair with that post must then fail, or the check could not see a cut-off post.
 *   NAVCHECK_ARENA=3 (default) picks the map. Run headless: tools/battle-check.mjs runs it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/game.h"

#define MAXP 8
static struct { char name[16]; t3_vec3 p; float r; } P[MAXP];
static int np;

static void read_posts(int id) {
  static const char *FILE_[] = { "foundry", "courtyard", "terraces", "battle" };
  char path[64];
  snprintf(path, sizeof path, "levels/%s.lvl", FILE_[id]);
  size_t len = 0;
  char *t = cw_asset_load(path, &len);
  if (!t) return;
  char *buf = malloc(len + 1);
  memcpy(buf, t, len), buf[len] = 0, free(t);
  for (char *l = strtok(buf, "\n"); l; l = strtok(NULL, "\n")) {
    int i, home;
    char nm[16];
    float x, y, z, r;
    if (np < MAXP && sscanf(l, "post %d %15s %f %f %f %f %d", &i, nm, &x, &y, &z, &r, &home) == 7) {
      snprintf(P[np].name, sizeof P[np].name, "%s", nm);
      P[np].p = v3(x, y, z), P[np].r = r, np++;
    }
  }
  free(buf);
}

static void wall_box(cw_world3d w, t3_vec3 c, t3_vec3 size) {
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  bd.position = c;
  cw_body3d b = cw_body3d_create(w, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  cw_shape3d_box(b, &sd, v3(size.x / 2, size.y / 2, size.z / 2), cw_xform3_identity());
}

static void run(void *user) {
  (void)user;
  const char *e = getenv("NAVCHECK_ARENA");
  int id = e ? atoi(e) : 3; /* (3: the Battle map) */
  cw_node root = cw_node_new("Node3D", "World");
  cw_node_add_child(cw_tree_root(cw_scene_app_tree()), root);
  arena_build(id, root);
  read_posts(id);
  cw_navmesh nav = A.nav;
  int seal = (e = getenv("NAVCHECK_SEAL")) ? atoi(e) : -1;
  if (seal >= 0 && seal < np) { /* the control: a closed wall round one post, then the navmesh again with the game's config */
    t3_vec3 c = P[seal].p;
    float h = 6, rr = 5, t = 0.6f;
    wall_box(A.world, v3(c.x, c.y + h / 2 - 1, c.z - rr), v3(2 * rr + t, h, t));
    wall_box(A.world, v3(c.x, c.y + h / 2 - 1, c.z + rr), v3(2 * rr + t, h, t));
    wall_box(A.world, v3(c.x - rr, c.y + h / 2 - 1, c.z), v3(t, h, 2 * rr + t));
    wall_box(A.world, v3(c.x + rr, c.y + h / 2 - 1, c.z), v3(t, h, 2 * rr + t));
    cw_world3d_step(A.world, 1.0f / 60);
    cw_nav_geom g = { 0 };
    cw_nav_geom_add_world3d(&g, A.world, NULL, CW_NAV_AREA_WALK);
    cw_nav_config nc = cw_nav_config_default(); /* (arena.c's values) */
    nc.cell_size = 0.25f, nc.cell_height = 0.15f, nc.agent_height = PLAYER_HT, nc.agent_radius = 0.5f, nc.agent_max_climb = 0.5f;
    nc.agent_max_slope = 40, nc.tile_size = 64;
    nav = cw_navmesh_build(&nc, &g);
    cw_nav_geom_free(&g);
    cw_navmesh_set_query_extents(nav, v3(2, 4, 2));
  }
  int pairs = 0, whole = 0;
  float worst = 0;
  char detail[4096];
  size_t dl = 0;
  detail[0] = 0;
  static t3_vec3 pts[512];
  for (int a = 0; a < np; a++)
    for (int b = a + 1; b < np; b++) {
      pairs++;
      t3_vec3 sa, sb;
      bool ok = cw_nav_nearest(nav, P[a].p, NULL, &sa) && cw_nav_nearest(nav, P[b].p, NULL, &sb);
      bool partial = true;
      int n = ok ? cw_nav_find_path(nav, sa, sb, NULL, pts, 512, &partial) : 0;
      float len = 0, miss = 1e9f;
      for (int k = 1; k < n; k++) len += v3len(v3sub(pts[k], pts[k - 1]));
      if (n > 0) miss = v3len(v3sub(pts[n - 1], sb));
      bool good = ok && n > 0 && !partial && miss < 1.5f;
      if (good) whole++;
      if (good && len > worst) worst = len;
      dl += (size_t)snprintf(detail + dl, sizeof detail - dl, "%s{\"a\":\"%s\",\"b\":\"%s\",\"ok\":%s,\"walk_m\":%.1f,\"points\":%d}", dl ? "," : "",
                             P[a].name, P[b].name, good ? "true" : "false", (double)len, n);
      if (dl >= sizeof detail) dl = sizeof detail - 1;
    }
  printf("{\"arena\":%d,\"posts\":%d,\"pairs\":%d,\"whole\":%d,\"polys\":%d,\"nav_ms\":%.1f,\"seal\":%d,\"longest_walk_m\":%.1f,\"pairs_detail\":[%s]}\n",
         id, np, pairs, whole, A.nav_polys, (double)A.nav_build_ms, seal, (double)worst, detail);
  fflush(stdout);
  exit(whole == pairs && pairs > 0 ? 0 : 1);
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .width = 64, .height = 64, .init = run };
  cw_project_apply_app(&app);
  return cw_scene_app(NULL, &app);
}
