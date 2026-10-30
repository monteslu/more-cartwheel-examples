/* hordeshooter_soak: the simulation and the bots with no window and no GPU, for long runs.
 *   hordeshooter_soak [minutes] [players] [start_wave]
 * Plays that many game minutes (60 ticks a second), restarting after a win or when the team has been
 * overrun, and checks every tick: no NaN or escaped position, enemy and bullet counts inside their
 * tables, the tether. Prints a line a game minute (wave, enemies, kills, upgrade levels, hp lost, resident
 * memory) and the pacing events (first kill, every level-up). Exit 0 = nothing broke; 1 = a check failed. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/jobs.h"
#include "cartwheel/vfs.h"
#include "game.h"

static long rss_kb(void) {
  long pages = 0, res = 0;
  FILE *f = fopen("/proc/self/statm", "r");
  if (f) { if (fscanf(f, "%ld %ld", &pages, &res) != 2) res = 0; fclose(f); }
  return res * 4;
}
static int bad(const sim *s, const char *what) {
  fprintf(stderr, "soak FAIL tick %d wave %d: %s\n", s->tick, s->wave, what);
  return 1;
}
static int check(const sim *s) {
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (!p->active) continue;
    if (!isfinite(p->x) || !isfinite(p->y) || !isfinite(p->hp)) return bad(s, "player NaN");
    if (fabsf(p->x) > HALF + 0.1f || fabsf(p->y) > HALF + 0.1f) return bad(s, "player out of the yard");
  }
  int n = 0;
  for (int i = 0; i < MAXE; i++) {
    const enemy *e = &s->e[i];
    if (!e->alive) continue;
    n++;
    if (!isfinite(e->x) || !isfinite(e->y)) return bad(s, "enemy NaN");
    if (fabsf(e->x) > HALF + 3 || fabsf(e->y) > HALF + 3) return bad(s, "enemy out of the yard");
  }
  if (n != s->nenemies) return bad(s, "enemy count disagrees with the table");
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  int alive = 0;
  for (int i = 0; i < MAXP; i++)
    if (s->p[i].active && !s->p[i].down) alive++, x0 = fminf(x0, s->p[i].x), x1 = fmaxf(x1, s->p[i].x), y0 = fminf(y0, s->p[i].y), y1 = fmaxf(y1, s->p[i].y);
  if (alive > 1 && (x1 - x0 > 2 * TETHER_HW + 0.5f || y1 - y0 > 2 * TETHER_HH + 0.5f)) return bad(s, "the tether let the team spread past one view");
  return 0;
}

int main(int argc, char **argv) {
  int minutes = argc > 1 ? atoi(argv[1]) : 30, players = argc > 2 ? atoi(argv[2]) : 4, wave0 = argc > 3 ? atoi(argv[3]) : 1;
  cw_jobs_init(3);
  static sim S;
#ifdef HORDE_ASSETS_DIR
  cw_vfs_set_root(HORDE_ASSETS_DIR); /* the floors' .cwlevel files */
#endif
  sim_init(&S, players, 4242, wave0);
  horde_floors_load(&S, NULL);
  S.no_power = getenv("HORDE_SOAK_NOPOWER") != NULL;
  long total = (long)minutes * 3600, rss0 = 0;
  int first_kill = 0, restarts = 0, wins = 0, overs = 0, worst_enemies = 0, lost_min = 0, last_hp[MAXP];
  int last_level[MAXP] = { 0 };
  for (int i = 0; i < MAXP; i++) last_hp[i] = 100;
  for (long t = 1; t <= total; t++) {
    input in[MAXP];
    memset(in, 0, sizeof in);
    for (int i = 0; i < players; i++) horde_bot(&S, i, &in[i]);
    if (t == 100 && getenv("HORDE_SOAK_INJECT")) S.p[0].x = NAN; /* the control: the checks must catch this */
    if (getenv("HORDE_SOAK_NOUP")) /* the starting weapon all run long (time to kill measurements) */
      for (int i = 0; i < players; i++) S.p[i].rate = S.p[i].shots = S.p[i].dmg = S.p[i].pierce = S.p[i].rocket = S.p[i].radius = 0;
    if (getenv("HORDE_SOAK_NOXP")) /* the levels control: gems give nothing, so the levels gate must fail */
      for (int i = 0; i < players; i++) S.p[i].xp = 0;
    if (getenv("HORDE_SOAK_GUN")) /* every player holds that gun all run (the guns' kill rates) */
      for (int i = 0; i < players; i++)
        if (S.p[i].pu_weapon_t < 60) sim_power_give(&S, i, atoi(getenv("HORDE_SOAK_GUN")) % PU_KINDS);
    int was = S.state;
    sim_step(&S, in);
    if (check(&S)) return 1;
    if (!first_kill && S.killed_total) first_kill = S.tick, printf("first kill at tick %d (%.1f s)\n", S.tick, (float)S.tick / 60);
    for (int i = 0; i < players; i++) {
      if (S.p[i].level != last_level[i] && S.p[i].level > 1) printf("levelup tick %d (%.1f s) player %d level %d\n", S.tick, (float)S.tick / 60, i, S.p[i].level);
      last_level[i] = S.p[i].level;
      if ((int)S.p[i].hp < last_hp[i]) lost_min += last_hp[i] - (int)S.p[i].hp;
      last_hp[i] = (int)S.p[i].hp;
    }
    if (getenv("HORDE_SOAK_TRACE") && t % 60 == 0) { /* a line a second: kills, gems on the ground, each player level and xp */
      int ng = 0;
      for (int k = 0; k < MAXG; k++) ng += S.g[k].alive && S.g[k].kind == 0;
      printf("trace t=%ld kills=%d enemies=%d gems=%d", t / 60, S.killed_total, S.nenemies, ng);
      for (int i = 0; i < players; i++) printf(" p%d=L%d/%d/%d", i, S.p[i].level, S.p[i].xp, S.p[i].xp_next);
      printf("\n");
    }
    if (S.nenemies > worst_enemies) worst_enemies = S.nenemies;
    if (was != ST_OVER && S.state == ST_OVER) overs++;
    if (S.state == ST_WON && S.state_ticks == 1) wins++;
    if (S.state == ST_WON && S.state_ticks > 60 * 10) { /* a new run; the time-to-kill stats carry over */
      static sim keep; /* the stats carry over */
      memcpy(keep.ttk_n, S.ttk_n, sizeof S.ttk_n), memcpy(keep.ttk_hits, S.ttk_hits, sizeof S.ttk_hits), memcpy(keep.ttk_ticks, S.ttk_ticks, sizeof S.ttk_ticks);
      memcpy(keep.pu_taken, S.pu_taken, sizeof S.pu_taken), memcpy(keep.pu_kills, S.pu_kills, sizeof S.pu_kills), memcpy(keep.pu_ticks, S.pu_ticks, sizeof S.pu_ticks);
      keep.plain_kills = S.plain_kills, keep.plain_ticks = S.plain_ticks;
      sim_init(&S, players, 4242 + (uint32_t)restarts, wave0);
      horde_floors_load(&S, NULL);
      S.no_power = getenv("HORDE_SOAK_NOPOWER") != NULL;
      memcpy(S.ttk_n, keep.ttk_n, sizeof S.ttk_n), memcpy(S.ttk_hits, keep.ttk_hits, sizeof S.ttk_hits), memcpy(S.ttk_ticks, keep.ttk_ticks, sizeof S.ttk_ticks);
      memcpy(S.pu_taken, keep.pu_taken, sizeof S.pu_taken), memcpy(S.pu_kills, keep.pu_kills, sizeof S.pu_kills), memcpy(S.pu_ticks, keep.pu_ticks, sizeof S.pu_ticks);
      S.plain_kills = keep.plain_kills, S.plain_ticks = keep.plain_ticks;
      restarts++;
      first_kill = 1;
    }
    if (t % 3600 == 0) {
      if (!rss0) rss0 = rss_kb();
      printf("minute %ld: wave %d enemies %d kills %d hp_lost %d alive %d state %d rss %ld kB (first minute %ld)\n", t / 3600, S.wave, S.nenemies, S.killed_total, lost_min, sim_alive_players(&S), S.state, rss_kb(), rss0);
      lost_min = 0;
      fflush(stdout);
    }
  }
  if (getenv("HORDE_SOAK_POWER")) { /* power-ups picked up, and kills a second while each was held against with none */
    printf("power plain: %.2f kills/s over %.0f player-s\n", S.plain_ticks ? S.plain_kills * 60.0 / S.plain_ticks : 0.0, S.plain_ticks / 60.0);
    for (int t = 0; t < PU_KINDS; t++)
      printf("power %-16s taken %3d  held %6.0f player-s  %.2f kills/s\n", sim_power_name(t), S.pu_taken[t], S.pu_ticks[t] / 60.0, S.pu_ticks[t] ? S.pu_kills[t] * 60.0 / S.pu_ticks[t] : 0.0);
  }
  int lo = 1 << 30, hi = 0;
  for (int i = 0; i < players; i++) lo = S.p[i].level < lo ? S.p[i].level : lo, hi = S.p[i].level > hi ? S.p[i].level : hi;
  printf("levels %d to %d (started at wave %d)\n", lo, hi, wave0);
  if (getenv("HORDE_SOAK_TTK")) /* hits and seconds from the first hit to the kill, per kind, at each wave */
    for (int w = 1; w <= FINAL_WAVE; w++) {
      printf("ttk wave %2d", w);
      static const char *KN[EK_KINDS] = { "runner", "brute", "bloater", "boss", "skitter", "bonewalker", "slagborn", "sporeling", "penitent", "gargoyle", "hellhound" };
      for (int e = 0; e < EK_KINDS; e++)
        if (S.ttk_n[w][e]) printf("  %s %.2f hits %.2f s (%d)", KN[e], (double)S.ttk_hits[w][e] / S.ttk_n[w][e], (double)S.ttk_ticks[w][e] / S.ttk_n[w][e] / 60.0, S.ttk_n[w][e]);
      printf("\n");
    }
  printf("soak ok: %d minutes, %d players, wins %d, overruns %d, restarts %d, most enemies %d, rss %ld kB\n", minutes, players, wins, overs, restarts, worst_enemies, rss_kb());
  sim_free(&S);
  return 0;
}
