/* hordeshooter floors: each floor's .cwlevel (LEVELS.md section 5, read by the level builder the
 * levelview cart and the game share) turned into the simulation's floor layouts: its solid
 * placements become obstacles, its water strips hazards, its power-up spots and its waves' kind
 * shares. The yard has no .cwlevel: its layout is built in code (sim.c). */
#include <stdio.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/vfs.h"
#include "game.h"
#include "levelbuild.h"

const char *const FLOOR_ID[NLEVELS] = { "yard", "sluice", "ossuary", "foundry", "grotto", "cathedral", "rift", "core" };

static bool floor_def_from(const lb_level *lv, floor_def *fd) {
  memset(fd, 0, sizeof *fd);
  for (int i = 0; i < lv->nat && fd->nob < MAXO; i++) {
    const lb_at *a = &lv->at[i];
    int solid = lv->prop[a->prop].solid;
    if (solid == LB_SCENERY || a->y > 0.05f) continue; /* scenery, or a prop stacked on another (the one under it is the obstacle) */
    int kind = solid == LB_WALL ? OK_WALL : solid == LB_CRATE ? OK_CRATE : solid == LB_BARREL ? OK_BARREL : OK_PILLAR;
    float hx = a->hx > 0 ? a->hx : 0.5f, hz = a->hz > 0 ? a->hz : 0.5f;
    fd->ob[fd->nob++] = (floor_obstacle){ (uint8_t)kind, a->x, a->z, hx, hz };
  }
  for (int i = 0; i < lv->nhaz && fd->nhaz < 32; i++)
  {
    const char *k = lv->haz[i].kind;
    int kind = !strcmp(k, "water") ? HZ_WATER : !strcmp(k, "molten") || !strcmp(k, "lava") ? HZ_MOLTEN : !strcmp(k, "spore") ? HZ_SPORE : -1;
    if (kind >= 0) fd->haz[fd->nhaz++] = (floor_hazard){ (uint8_t)kind, lv->haz[i].x, lv->haz[i].z, lv->haz[i].hx, lv->haz[i].hz };
  }
  for (int i = 0; i < lv->npick && fd->npick < 16; i++) fd->pick[fd->npick][0] = lv->pick[i][0], fd->pick[fd->npick][1] = lv->pick[i][1], fd->npick++;
  for (int i = 0; i < lv->nspawn && fd->nspawn < 16; i++)
    fd->spawn[fd->nspawn][0] = lv->spawn[i][0], fd->spawn[fd->nspawn][1] = lv->spawn[i][1], fd->spawn[fd->nspawn][2] = lv->spawn[i][2], fd->nspawn++;
  fd->nwave = CW_MIN(lv->waves, 8);
  for (int w = 0; w < fd->nwave; w++) {
    const lb_wave *wv = &lv->wave[w];
    float sum = 0;
    for (int k = 0; k < wv->n; k++) {
      int kind = sim_kind_by_name(wv->kind[k]);
      if (kind < 0) {
        cw_error("horde: %s wave %d: no creature called %s", lv->id, w + 1, wv->kind[k]);
        continue;
      }
      fd->share[w][kind] += wv->share[k], sum += wv->share[k];
    }
    for (int k = 0; k < EK_KINDS && sum > 0; k++) fd->share[w][k] /= sum;
  }
  fd->set = true;
  return true;
}

int horde_floors_load(sim *s, lb_level *levels) {
  int n = 0;
  for (int f = 1; f < NLEVELS; f++) {
    char path[64];
    snprintf(path, sizeof path, "levels/%s.cwlevel", FLOOR_ID[f]);
    if (!cw_vfs_exists(path)) continue; /* not made yet: the floor is played in the yard */
    static lb_level tmp;
    lb_level *lv = levels ? &levels[f] : &tmp;
    if (!levelbuild_parse(path, lv)) continue;
    if (!lv->lightmap[0] || !cw_vfs_exists(lv->lightmap)) continue; /* not baked yet: still played in the yard */
    if (!floor_def_from(lv, &s->fl[f])) continue;
    n++;
  }
  sim_floor_apply(s);
  return n;
}
