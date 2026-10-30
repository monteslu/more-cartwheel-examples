/* Plays a gate's pad script through the rules with no window: the sim hash at the gate's last frame, to compare with the
 * hash the rendered gate pins (tests/fighter-gates.json). It does not replace the gate; it shows, without a GPU, whether a
 * change to the rules moved what the gate plays. tests/gatereplay.mjs runs it.
 * Usage: gatereplay <moves.txt> <events file or -> <frames> <auto 0|1> <training 0|1>
 * Events: frame, then left right up down buttons for each pad, as main.c's read_pad gives them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../sim.h"

static ft_roster R;
static ft_game G;
static char text[1 << 18];

int main(int argc, char **argv) {
  if (argc < 6) return 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 2;
  text[fread(text, 1, sizeof text - 1, f)] = 0;
  fclose(f);
  const char *err = NULL;
  if (ft_roster_load(&R, text, &err) < 0) { printf("moves: %s\n", err); return 2; }
  int frames = atoi(argv[3]);
  /* as begin_match does in the test builds: Bolt against Vex on the first stage */
  int d0 = 0, d1 = 2;
  G.cpu[0] = G.cpu[1] = atoi(argv[4]) != 0;
  G.cpu_level[0] = G.cpu_level[1] = 1;
  G.training = atoi(argv[5]) != 0;
  G.dummy = T_STAND;
  ft_game_start(&G, &R, d0, d1);
  G.rng = 12345u + (uint32_t)(d0 * 31 + d1 * 7);
  FILE *ev = strcmp(argv[2], "-") ? fopen(argv[2], "r") : NULL;
  int next = 0, v[10];
  bool have = ev && fscanf(ev, "%d %d %d %d %d %d %d %d %d %d %d", &next, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]) == 11;
  ft_input in[2] = { { 0 }, { 0 } };
  for (int fr = 1; fr <= frames; fr++) {
    while (have && next <= fr) {
      for (int p = 0; p < 2; p++) in[p] = (ft_input){ (uint8_t)v[p * 5], (uint8_t)v[p * 5 + 1], (uint8_t)v[p * 5 + 2], (uint8_t)v[p * 5 + 3], (uint16_t)v[p * 5 + 4] };
      have = fscanf(ev, "%d %d %d %d %d %d %d %d %d %d %d", &next, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]) == 11;
    }
    ft_step(&G, in);
  }
  printf("%u hp=%d,%d wins=%d,%d stats=%u,%u,%u,%u,%u,%u\n", ft_hash(&G), G.f[0].hp, G.f[1].hp, G.f[0].wins, G.f[1].wins, ft_stat.hits, ft_stat.blocks, ft_stat.parries, ft_stat.guard_breaks, ft_stat.whiffs_high, ft_stat.throws);
  return 0;
}
