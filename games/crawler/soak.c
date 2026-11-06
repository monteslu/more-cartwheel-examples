/* crawler_soak: the rules and the bots with no window and no GPU.
 *
 *   crawler_soak --clear <floor.cwmaze> [players] [minutes]
 *       The level check's clear run: bots (four by default) play the floor from its start with the game's rules;
 *       exit 0 when one of them reaches the main exit within the time (8 game minutes by default) with at most 8
 *       falls among them (up to three tries, each its own seed), 1 when they do
 *       not (the line says why: the time ran out, the team fell, a check failed), 2 when the file is not a floor.
 *   crawler_soak [minutes] [players] [first floor]
 *       The long soak: floor after floor (a fallen team starts its floor again), checking every tick: no NaN or
 *       stray position, nobody inside a wall, counts that agree with the tables, the team on one screen. A line a
 *       game minute (floor, monsters, kills, health lost, resident memory). Exit 0 = nothing broke.
 * CRAWLER_SOAK_INJECT=1 puts a NaN into player 1 at tick 100 (the control: the checks must catch it). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/jobs.h"
#include "game.h"

static long rss_kb(void) {
  long pages = 0, res = 0;
  FILE *f = fopen("/proc/self/statm", "r");
  if (f) { if (fscanf(f, "%ld %ld", &pages, &res) != 2) res = 0; fclose(f); }
  return res * 4;
}
static int bad(const sim *s, const char *what) {
  printf("soak FAIL floor %d tick %d: %s\n", s->floor, s->tick, what);
  return 1;
}
static int check(const sim *s) {
  const float HW = (float)s->m.w, HH = (float)s->m.h; /* the maze's half size in metres */
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (!p->active) continue;
    if (!isfinite(p->x) || !isfinite(p->y)) return bad(s, "player NaN");
    if (fabsf(p->x) > HW || fabsf(p->y) > HH) return bad(s, "player off the floor");
    if (!p->down && sim_solid(s, mz_tx(&s->m, p->x), mz_ty(&s->m, p->y))) return bad(s, "player inside a wall");
  }
  int n = 0;
  for (int i = 0; i < MAXE; i++) {
    const enemy *e = &s->e[i];
    if (!e->alive) continue;
    n++;
    if (!isfinite(e->x) || !isfinite(e->y)) return bad(s, "monster NaN");
    if (fabsf(e->x) > HW + 1 || fabsf(e->y) > HH + 1) return bad(s, "monster off the floor");
  }
  if (n != s->nenemies) return bad(s, "monster count disagrees with the table");
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  int alive = 0;
  for (int i = 0; i < MAXP; i++)
    if (s->p[i].active && !s->p[i].down) alive++, x0 = fminf(x0, s->p[i].x), x1 = fmaxf(x1, s->p[i].x), y0 = fminf(y0, s->p[i].y), y1 = fmaxf(y1, s->p[i].y);
  if (alive > 1 && (x1 - x0 > 2 * TETHER_HW + 0.5f || y1 - y0 > 2 * TETHER_HH + 0.5f)) return bad(s, "the team spread past one screen");
  return 0;
}

static char *read_text(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *t = malloc((size_t)n + 1);
  if (fread(t, 1, (size_t)n, f) != (size_t)n) n = 0;
  t[n] = 0;
  fclose(f);
  return t;
}
static bool load_floor(int n, maze *m) {
  char path[512], err[200];
#ifdef CRAWLER_ASSETS_DIR
  snprintf(path, sizeof path, "%s/levels/%03d.cwmaze", CRAWLER_ASSETS_DIR, n);
#else
  snprintf(path, sizeof path, "assets/levels/%03d.cwmaze", n);
#endif
  char *t = read_text(path);
  if (!t) return false;
  bool ok = maze_parse(t, m, sim_kind_by_name, err, sizeof err);
  if (!ok) printf("soak: %s: %s\n", path, err);
  free(t);
  return ok;
}

static sim S;
static maze M;

static int clear_once(const char *file, int players, int minutes, uint32_t seed) {
  char *t = read_text(file), err[200];
  if (!t || !maze_parse(t, &M, sim_kind_by_name, err, sizeof err)) { printf("clear: %s: %s\n", file, t ? err : "cannot read"); free(t); return 2; }
  free(t);
  int cls[MAXP] = { CL_BRUTE, CL_MAGE, CL_RANGER, CL_WARDEN };
  sim_init(&S, players, cls, seed);
  sim_floor_start(&S, &M);
  long total = (long)minutes * 3600;
  int most = 0;
  for (long k = 0; k < total; k++) {
    input in[MAXP];
    memset(in, 0, sizeof in);
    for (int i = 0; i < players; i++) crawler_bot(&S, i, &in[i]);
    sim_step(&S, in);
    if (check(&S)) return 1;
    if (S.nenemies > most) most = S.nenemies;
    if (getenv("CRAWLER_TRACE") && k % 120 == 0) {
      printf("t=%.0f mon=%d doors=%d in3(%.2f,%.2f f%d) p3xy(%.2f,%.2f) cd%d out%d", (float)k / 60, S.nenemies, S.doors_opened, in[3].mx, in[3].my, in[3].fire, S.p[3].x, S.p[3].y, S.p[3].cd, S.p[3].shots_out);
      for (int i = 0; i < players; i++) { int gk, gx, gy; crawler_bot_goal(i, &gk, &gx, &gy); printf(" p%d(%d,%d %.2f,%.2f in%.2f,%.2f%s hp%d k%d g%d@%d,%d%s)", i, mz_tx(&S.m, S.p[i].x), mz_ty(&S.m, S.p[i].y), S.p[i].x, S.p[i].y, in[i].mx, in[i].my, in[i].fire ? "F" : "", S.p[i].hp, S.p[i].keys, gk, gx, gy, S.p[i].down ? " down" : ""); }
      printf("\n");
    }
    if (S.falls > 8) { printf("clear: floor %d: the team fell %d times by %.1f s (most monsters %d; damage by", M.floor, S.falls, (float)S.floor_ticks / 60.0f, most);
      for (int k = 0; k <= EK_KINDS; k++) if (S.dmg_kind[k]) printf(" %s %d", k < EK_KINDS ? sim_kind_name(k) : "other", S.dmg_kind[k]);
      printf(")\n");
      return 1;
    }
    if (S.state == ST_EXIT || S.state == ST_WON) {
      printf("clear: floor %d cleared in %.1f s (%d kills, %d doors opened, %d spawners down, most monsters %d, health lost %d, falls %d)\n", M.floor, (float)S.floor_ticks / 60.0f,
             S.killed_total, S.doors_opened, S.spawners_killed, most, S.damage_taken, S.falls);
      return 0;
    }
    if (S.state == ST_OVER) {
      printf("clear: floor %d: the team fell after %.1f s (most monsters %d; damage by", M.floor, (float)S.floor_ticks / 60.0f, most);
      for (int k = 0; k <= EK_KINDS; k++) if (S.dmg_kind[k]) printf(" %s %d", k < EK_KINDS ? sim_kind_name(k) : "other", S.dmg_kind[k]);
      printf("; potions %d, food %d)\n", S.potions_used, S.food_eaten);
      return 1;
    }
  }
  float d = 0;
  for (int i = 0; i < MAXP; i++) if (S.p[i].active) d = (float)S.p[i].keys;
  printf("clear: floor %d: no bot reached the exit in %d minutes (keys held %.0f, doors opened %d, kills %d)\n", M.floor, minutes, d, S.doors_opened, S.killed_total);
  return 1;
}

/* the clear run: up to three tries (each its own seed: the bots are not a person, and one bad scramble is not a
 * broken floor); a floor no try clears fails */
static int clear_run(const char *file, int players, int minutes) {
  int r = 1;
  for (int k = 0; k < 3 && r == 1; k++) {
    r = clear_once(file, players, minutes, 4242u + (uint32_t)k * 7919u);
    if (r == 0 && k) printf("clear: (on try %d)\n", k + 1);
  }
  return r;
}

int main(int argc, char **argv) {
  cw_jobs_init(3);
  if (argc > 2 && !strcmp(argv[1], "--clear")) return clear_run(argv[2], argc > 3 ? atoi(argv[3]) : 4, argc > 4 ? atoi(argv[4]) : 8);
  int minutes = argc > 1 ? atoi(argv[1]) : 30, players = argc > 2 ? atoi(argv[2]) : 4, floor0 = argc > 3 ? atoi(argv[3]) : 1;
  int cls[MAXP] = { CL_BRUTE, CL_WARDEN, CL_MAGE, CL_RANGER };
  sim_init(&S, players, cls, 4242);
  if (!load_floor(floor0, &M)) { printf("soak: no floor %d\n", floor0); return 2; }
  sim_floor_start(&S, &M);
  long total = (long)minutes * 3600, rss0 = 0;
  int floors = 0, overs = 0, most = 0, lost = 0, last_hp[MAXP] = { 0 };
  for (long t = 1; t <= total; t++) {
    input in[MAXP];
    memset(in, 0, sizeof in);
    for (int i = 0; i < players; i++) crawler_bot(&S, i, &in[i]);
    if (t == 100 && getenv("CRAWLER_SOAK_INJECT")) S.p[0].x = NAN;
    sim_step(&S, in);
    if (check(&S)) return 1;
    for (int i = 0; i < players; i++) {
      if (S.p[i].hp < last_hp[i]) lost += last_hp[i] - S.p[i].hp;
      last_hp[i] = S.p[i].hp;
    }
    if (S.nenemies > most) most = S.nenemies;
    if (S.state == ST_WON) {
      floors++;
      int next = S.next_floor;
      if (!load_floor(next, &M)) next = 1, load_floor(1, &M);
      printf("floor %d done at %.1f min: to floor %d\n", S.floor, (float)t / 3600.0f, next);
      sim_floor_start(&S, &M);
    } else if (S.state == ST_OVER && S.state_ticks > 120) {
      overs++;
      printf("floor %d: the team fell at %.1f min; again\n", S.floor, (float)t / 3600.0f);
      sim_floor_start(&S, &M);
    }
    if (t % 3600 == 0) {
      if (!rss0) rss0 = rss_kb();
      printf("minute %ld: floor %d monsters %d kills %d hp_lost %d alive %d rss %ld kB (first minute %ld)\n", t / 3600, S.floor, S.nenemies, S.killed_total, lost, sim_alive_players(&S), rss_kb(), rss0);
      lost = 0;
      fflush(stdout);
    }
  }
  printf("soak ok: %d minutes, %d players, %d floors done, %d falls, most monsters %d, rss %ld kB\n", minutes, players, floors, overs, most, rss_kb());
  sim_free(&S);
  return 0;
}
