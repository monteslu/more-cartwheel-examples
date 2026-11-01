/* The levels: five worlds of five levels each, included by worldgen.c. Every level is written out here by hand from the
 * pieces above: a new thing is met somewhere safe first, then tested, then mixed with the last one. Every level has a secret
 * room, flags far enough apart to matter and close enough to forgive, and a long straight before any gap that asks for a
 * running jump. A standing jump clears 4 tiles. A running jump clears 9. The air jump adds about 4, and is a bonus: nothing
 * before the claws world needs it except the things marked as needing it.
 *
 * The words are the Management's. They own all of this and would like you to know that. */

/* ── the long middle of a level ──
 * After a level has introduced its idea by hand, this writes the rest of its length from what the player has met so far,
 * from a list the level gives, in an order its own seed picks: never the same section twice running, the gaps slowly wider
 * and the timing slowly meaner as the level goes on, a flag every so often, some coins for the eye to follow. */
enum { S_RUNGAP, S_STEPS, S_WALKERS, S_HOPS, S_LEDGES, S_DROP, S_SPRING, S_CRUMBLE, S_CRUMSTEP, S_SAW, S_PLAT, S_SHAFT, S_FALL, S_BELT, S_PRESS, S_POOL, S_PLATE, S_CRACK, S_ICE, S_FAN, S_CANNON, S_LAVAGAP };
static void section(gen *G, int s, int biome, float hard) {
  level *L = G->L;
  int wide = 7 + (int)(hard * 1.2f + (float)rnd(G, 2)); /* 7 to 9: a good running jump clears 10 */
  int hz = biome == BIOME_VOLCANO ? T_LAVA : T_SPIKE;
  switch (s) {
    case S_RUNGAP:
      flat(G, 12, biome);
      coin_arc(G, G->x, wide, G->g + 2);
      gap(G, wide, biome, hz);
      flat(G, 4, biome);
      break;
    case S_STEPS: {
      int d = G->g > 30 ? -1 : 1;
      for (int i = 0; i < 3 + (int)rnd(G, 3); i++) rise(G, d * (1 + (int)rnd(G, 2)), 4 + (int)rnd(G, 3), biome);
      coin_row(G, G->x - 4, 3, G->g + 2);
      break;
    }
    case S_WALKERS:
      flat(G, 12, biome);
      if (L->nenemies < 22) walker(G, G->x - 8, G->g, 3, 0);
      if (L->nenemies < 22 && hard > 0.5f) walker(G, G->x - 3, G->g, 2, biome == BIOME_VOLCANO || rnd(G, 3) == 0 ? 1 : 0);
      coin_row(G, G->x - 9, 5, G->g + 3);
      break;
    case S_HOPS:
      for (int i = 0; i < 3; i++) { flat(G, 3 + (int)rnd(G, 2), biome); gap(G, 3 + (int)rnd(G, 2), biome, hz); }
      flat(G, 3, biome);
      break;
    case S_LEDGES: /* ledges up to coins, over a straight */
      flat(G, 14, biome);
      oneway(G, G->x - 11, G->g + 3, 3);
      oneway(G, G->x - 7, G->g + 5, 3);
      coin_row(G, G->x - 7, 3, G->g + 6);
      break;
    case S_DROP:
      if (G->g > 14) rise(G, -3 - (int)rnd(G, 2), 8, biome);
      else flat(G, 8, biome);
      break;
    case S_SPRING:
      if (G->g < 34) {
        flat(G, 5, biome);
        spring(G, G->x - 2);
        flat(G, 3, biome);
        rise(G, 5, 6, biome);
        coin_row(G, G->x - 5, 3, G->g + 2);
      } else flat(G, 8, biome);
      break;
    case S_CRUMBLE:
      flat(G, 8, biome);
      coin_row(G, G->x + 2, 4, G->g + 2);
      crumble_bridge(G, 8 + (int)(hard * 6.0f));
      flat(G, 3, biome);
      break;
    case S_CRUMSTEP:
      flat(G, 5, biome);
      crumble_steps(G, 3 + (int)(hard * 2.0f), G->g > 30 ? -1 : 1);
      flat(G, 4, biome);
      break;
    case S_SAW:
      flat(G, 14, biome);
      saw(G, G->x - 7, G->g + 1, 4, 2.4f + hard * 1.6f);
      if (hard > 0.5f) saw(G, G->x - 3, G->g + 3, 2, 3.0f);
      coin_row(G, G->x - 9, 4, G->g + 4);
      break;
    case S_PLAT:
      flat(G, 5, biome);
      plat(G, 8 + (int)(hard * 3.0f), 2.2f, 0.35f + hard * 0.2f, biome);
      flat(G, 4, biome);
      break;
    case S_SHAFT:
      flat(G, 6, biome);
      if (G->g < 24) shaft(G, 3 + (int)rnd(G, 2), 7 + (int)(hard * 4.0f), biome);
      else { rise(G, -6, 8, biome); }
      flat(G, 4, biome);
      break;
    case S_FALL:
      flat(G, 8, biome);
      faller(G, G->x - 3);
      if (hard > 0.4f) { flat(G, 5, biome); faller(G, G->x - 2); }
      flat(G, 4, biome);
      break;
    case S_BELT: {
      float sp = (rnd(G, 2) ? 1.0f : -1.0f) * (3.0f + hard * 1.5f);
      flat(G, 3, biome);
      coin_row(G, G->x + 3, 5, G->g + 1);
      belt(G, 12 + (int)rnd(G, 6), sp, biome);
      if (sp > 0) gap(G, wide, biome, hz);
      flat(G, 4, biome);
      break;
    }
    case S_PRESS:
      flat(G, 18, biome);
      press(G, G->x - 12, 190 - (int)(hard * 40.0f), (int)rnd(G, 60));
      press(G, G->x - 4, 190 - (int)(hard * 40.0f), (int)rnd(G, 60));
      flat(G, 3, biome);
      break;
    case S_POOL:
      if (L->nchecks && G->x > 10) { flat(G, 6, biome); pool(G, 14 + (int)rnd(G, 8), 5 + (int)rnd(G, 2), (rnd(G, 2) ? 1.0f : -1.0f) * 3.0f, 0); flat(G, 5, biome); }
      break;
    case S_PLATE:
      flat(G, 3, biome);
      plate_door(G, 220 - (int)(hard * 40.0f), 10 + (int)(hard * 6.0f));
      flat(G, 3, biome);
      break;
    case S_CRACK:
      flat(G, 6, biome);
      crack_wall(G);
      coin_row(G, G->x, 2, G->g + 1);
      flat(G, 5, biome);
      break;
    case S_ICE:
      flat(G, 3, biome);
      coin_row(G, G->x + 3, 4, G->g + 1);
      ice(G, 12 + (int)rnd(G, 6), biome);
      if (rnd(G, 2)) gap(G, wide, biome, hz);
      ice(G, 5, biome);
      flat(G, 3, biome);
      break;
    case S_FAN:
      flat(G, 8, biome);
      coin_row(G, G->x + 2, 4, G->g + 5);
      fan_gap(G, 8 + (int)(hard * 3.0f), 8);
      flat(G, 4, biome);
      break;
    case S_CANNON:
      flat(G, 5, biome);
      cannon(G, G->x - 2, 13.0f, 16.0f);
      coin_arc(G, G->x + 2, 10, G->g + 3);
      gap(G, 12 + (int)(hard * 2.0f), biome, hz);
      flat(G, 8, biome);
      break;
    case S_LAVAGAP:
      flat(G, 12, biome);
      coin_arc(G, G->x, wide, G->g + 2);
      gap(G, wide, biome, T_LAVA);
      flat(G, 4, biome);
      break;
    default: break;
  }
}
static void extend(gen *G, int biome, const int *sections, int n, int columns) {
  int x0 = G->x, last = -1, lastflag = G->x;
  while (G->x - x0 < columns) {
    int k = (int)rnd(G, n);
    if (sections[k] == last) k = (k + 1) % n;
    last = sections[k];
    section(G, last, biome, (float)(G->x - x0) / (float)columns);
    if (G->x - lastflag > 42) { flat(G, 3, biome); check(G); lastflag = G->x; }
  }
  flat(G, 4, biome);
  check(G);
}
#define EXTEND(cols, ...) do { static const int S_[] = { __VA_ARGS__ }; extend(G, B, S_, (int)(sizeof S_ / sizeof S_[0]), cols); } while (0)

static void L_finish(gen *G, int biome) {
  flat(G, 8, biome);
  goal_booth(G);
  flat(G, 6, biome);
  close_level(G, biome);
}

/* ── world 1: the meadow ─────────────────────────────────────────── */
static void l_mandatory_fun(gen *G) {
  enum { B = BIOME_MEADOW };
  begin(G, B, 18);
  flat(G, 2, B);
  sign(G, "WELCOME TO THE MEADOW. PLEASE DO NOT ENJOY THE GRASS.");
  flat(G, 16, B);
  sign(G, "HOLD X TO RUN. WE WILL NOT BE HOLDING IT FOR YOU.");
  flat(G, 26, B);                               /* the long straight: speed shows, and the jump gate is measured here */
  coin_row(G, G->x - 14, 6, G->g + 1);
  sign(G, "COINS FLOAT BECAUSE NOBODY HAS FILED THE FORM TO STOP THEM.");
  flat(G, 5, B);
  gap(G, 3, B, T_SPIKE);                        /* a standing jump does this */
  flat(G, 8, B);
  sign(G, "THAT WAS THE EASY GAP. WE ARE LEGALLY REQUIRED TO HAVE ONE.");
  flat(G, 12, B);
  coin_arc(G, G->x, 7, G->g + 2);
  gap(G, 7, B, T_SPIKE);                        /* a standing jump does not: the first running jump */
  flat(G, 6, B);
  check(G);
  walker(G, G->x + 6, G->g, 3, 0);
  sign(G, "THIS BEETLE IS AN UNPAID INTERN. JUMP ON HIM. HE WILL PUT IT ON HIS RESUME.");
  flat(G, 12, B);
  rise(G, 2, 6, B);
  rise(G, 2, 6, B);
  coin_row(G, G->x - 4, 3, G->g + 3);
  flat(G, 10, B);
  gap(G, 8, B, T_SPIKE);
  oneway(G, G->x - 5, G->g + 1, 2);             /* a ledge in the gap for anyone who did not run */
  flat(G, 5, B);
  check(G);
  pocket(G, 4, 3, B);                           /* a room with a hatch in its roof */
  secret_sign(G, "SECRET ROOM. PLEASE ACT SURPRISED.");
  rise(G, -3, 8, B);
  walker(G, G->x - 4, G->g, 3, 0);
  flat(G, 8, B);
  spikes(G, 3, B);
  flat(G, 12, B);
  sign(G, "THE SPIKES ARE DECORATIVE. YOUR INSURANCE IS ALSO DECORATIVE.");
  coin_arc(G, G->x, 8, G->g + 2);
  gap(G, 8, B, T_SPIKE);
  flat(G, 12, B);
  check(G);
  walker(G, G->x - 6, G->g, 4, 0);
  rise(G, -2, 10, B);
  coin_row(G, G->x - 6, 4, G->g + 1);
  gap(G, 4, B, T_SPIKE);
  flat(G, 5, B);
  gap(G, 4, B, T_SPIKE);
  flat(G, 12, B);
  sign(G, "YOU HAVE WALKED FAR ENOUGH TO OWE US A WALKING FEE.");
  gap(G, 8, B, T_SPIKE);
  flat(G, 10, B);
  check(G);
  walker(G, G->x + 4, G->g, 3, 0);
  flat(G, 10, B);
  rise(G, 3, 8, B);
  coin_row(G, G->x - 5, 4, G->g + 3);
  EXTEND(420, S_RUNGAP, S_STEPS, S_WALKERS, S_HOPS, S_DROP, S_RUNGAP);
  sign(G, "THE LEVEL IS OVER SOON. THE FEELING IS NOT.");
  L_finish(G, B);
}

static void l_trampolines(gen *G) {
  enum { B = BIOME_MEADOW };
  begin(G, B, 16);
  flat(G, 12, B);
  sign(G, "THIS TRAMPOLINE IS NOT INSURED. NEITHER ARE YOU.");
  spring(G, G->x + 3);
  flat(G, 6, B);
  rise(G, 5, 8, B);                             /* too high for any jump yet: the spring is the way up */
  coin_row(G, G->x - 6, 4, G->g + 2);
  flat(G, 4, B);
  spring(G, G->x + 2);
  flat(G, 5, B);
  coin_arc(G, G->x, 7, G->g + 3);
  rise(G, 5, 8, B);
  check(G);
  sign(G, "SOMEONE OWNS THESE. NOBODY WILL SAY WHO.");
  flat(G, 12, B);
  coin_arc(G, G->x, 8, G->g + 2);
  gap(G, 8, B, T_SPIKE);
  flat(G, 5, B);
  walker(G, G->x + 3, G->g, 2, 0);
  flat(G, 8, B);
  spring(G, G->x + 2);
  flat(G, 4, B);
  coin_row(G, G->x - 2, 3, G->g + 7);
  gap(G, 5, B, T_SPIKE);
  flat(G, 8, B);
  check(G);
  rise(G, -4, 10, B);
  sign(G, "THE AIR JUMP. IT IS A JUMP, IN THE AIR. OUR LAWYERS SAY THAT IS ALL WE CAN TELL YOU.");
  flat(G, 5, B);
  pedestal(G, AB_DOUBLE);
  flat(G, 5, B);
  sign(G, "PRESS JUMP AGAIN IN THE AIR. GRAVITY HAS BEEN INFORMED AND IS NOT HAPPY.");
  flat(G, 4, B);
  rise(G, 5, 6, B);                             /* a cliff higher than a running jump: the air jump's first test */
  coin_row(G, G->x - 4, 3, G->g + 2);
  check(G);
  flat(G, 8, B);
  gate_mark(G, AB_DOUBLE);
  oneway(G, G->x - 7, G->g + 4, 3);
  oneway(G, G->x - 3, G->g + 7, 3);
  secret_coins(G, G->x - 3, 3, G->g + 8);
  secret_sign(G, "A HIGH LEDGE WITH COINS ON IT. WE CANNOT THINK WHY.");
  flat(G, 8, B);
  rise(G, -2, 10, B);
  gap(G, 10, B, T_SPIKE);                       /* a run and an air jump, or a run and a good landing on the post */
  oneway(G, G->x - 6, G->g, 2);
  flat(G, 10, B);
  check(G);
  walker(G, G->x - 4, G->g, 3, 0);
  flat(G, 10, B);
  spring(G, G->x + 2);
  flat(G, 5, B);
  rise(G, 6, 7, B);
  coin_row(G, G->x - 5, 4, G->g + 2);
  pocket(G, 3, 4, B);
  secret_sign(G, "THE SECOND SECRET ROOM. THE FIRST ONE WAS A LIE.");
  rise(G, -3, 12, B);
  EXTEND(420, S_RUNGAP, S_SPRING, S_WALKERS, S_HOPS, S_LEDGES, S_DROP, S_STEPS);
  sign(G, "YOU MAY KEEP THE AIR JUMP. WE WILL BE BILLING YOU FOR THE AIR.");
  L_finish(G, B);
}

static void l_ground_suggestion(gen *G) {
  enum { B = BIOME_MEADOW };
  begin(G, B, 20);
  flat(G, 10, B);
  sign(G, "THIS BRIDGE WAS BUILT BY THE LOWEST BIDDER. THE LOWEST BIDDER WAS A BEAVER.");
  flat(G, 8, B);
  crumble_bridge(G, 8);
  flat(G, 8, B);
  check(G);
  sign(G, "IF YOU STAND ON IT YOU ARE AGREEING TO THE TERMS.");
  flat(G, 12, B);
  crumble_bridge(G, 10);
  coin_row(G, G->x - 8, 6, G->g + 2);
  flat(G, 6, B);
  walker(G, G->x - 3, G->g, 2, 0);
  flat(G, 12, B);
  gap(G, 8, B, T_SPIKE);
  flat(G, 6, B);
  check(G);
  crumble_steps(G, 6, 1);
  flat(G, 8, B);
  sign(G, "THE GROUND IS A SUGGESTION. THE SPIKES ARE A POLICY.");
  flat(G, 8, B);
  crumble_bridge(G, 14);
  flat(G, 8, B);
  pocket(G, 4, 3, B);
  secret_sign(G, "NOTHING IN THIS ROOM CRUMBLES. WE SAVED THE BUDGET FOR IT.");
  rise(G, -3, 6, B);
  check(G);
  walker(G, G->x - 3, G->g, 2, 0);
  flat(G, 12, B);
  coin_arc(G, G->x, 8, G->g + 2);
  gap(G, 8, B, T_SPIKE);
  crumble_bridge(G, 6);
  flat(G, 12, B);
  crumble_steps(G, 8, -1);
  flat(G, 10, B);
  check(G);
  spring(G, G->x + 2);
  flat(G, 5, B);
  rise(G, 5, 5, B);
  crumble_bridge(G, 12);
  coin_row(G, G->x - 10, 8, G->g + 2);
  flat(G, 10, B);
  EXTEND(440, S_CRUMBLE, S_RUNGAP, S_SPRING, S_CRUMSTEP, S_WALKERS, S_LEDGES, S_DROP);
  sign(G, "CONGRATULATIONS. THE BEAVER HAS BEEN PROMOTED.");
  L_finish(G, B);
}

static void l_gardening(gen *G) {
  enum { B = BIOME_MEADOW };
  begin(G, B, 18);
  flat(G, 10, B);
  sign(G, "SELF-SHARPENING HEDGE TRIMMER. SHE IS IN A GOOD MOOD TODAY. RUN.");
  flat(G, 12, B);
  saw(G, G->x - 7, G->g + 1, 5, 2.4f);
  coin_row(G, G->x - 9, 5, G->g + 4);
  flat(G, 12, B);
  check(G);
  sign(G, "THE LAST GARDENER IS STILL HERE. MOST OF HIM.");
  flat(G, 12, B);
  saw(G, G->x - 7, G->g + 1, 5, 3.2f);
  flat(G, 8, B);
  plat(G, 7, 2.2f, 0.35f, B);
  flat(G, 8, B);
  check(G);
  walker(G, G->x - 3, G->g, 2, 0);
  flat(G, 12, B);
  saw(G, G->x - 8, G->g + 1, 3, 2.8f);
  saw(G, G->x - 3, G->g + 3, 2, 3.0f);
  flat(G, 12, B);
  coin_arc(G, G->x, 8, G->g + 2);
  gap(G, 8, B, T_SPIKE);
  flat(G, 12, B);
  check(G);
  plat(G, 8, 2.2f, 0.4f, B);
  flat(G, 5, B);
  plat(G, 8, 2.2f, 0.5f, B);
  coin_row(G, G->x - 5, 3, G->g + 4);
  flat(G, 6, B);
  sign(G, "KEEP OFF THE LAWN. THE LAWN HAS BEEN WARNED ABOUT YOU.");
  pocket(G, 4, 4, B);
  secret_sign(G, "THIS ROOM IS WHERE THE GARDENER KEPT HIS HOPES.");
  rise(G, -3, 8, B);
  check(G);
  flat(G, 12, B);
  saw(G, G->x - 9, G->g + 1, 4, 3.6f);
  saw(G, G->x - 4, G->g + 1, 3, 3.0f);
  flat(G, 6, B);
  spring(G, G->x + 2);
  flat(G, 4, B);
  rise(G, 5, 12, B);
  saw(G, G->x - 6, G->g + 1, 4, 4.0f);
  EXTEND(420, S_SAW, S_RUNGAP, S_PLAT, S_CRUMBLE, S_WALKERS, S_LEDGES, S_DROP, S_SPRING);
  sign(G, "THE HEDGE TRIMMER WOULD LIKE TO SEE YOU IN HER OFFICE.");
  flat(G, 10, B);
  L_finish(G, B);
}

/* ── a boss arena, the same shape in every world: a long flat room, walled in, its door behind you ── */
static void boss_room(gen *G, int biome, int boss, const char *line) {
  flat(G, 10, biome);
  sign(G, line);
  flat(G, 8, biome);
  check(G);
  flat(G, 4, biome);
  G->L->boss_x = G->x + 12;
  G->L->boss_kind = boss;
  flat(G, 26, biome);
  walker(G, G->L->boss_x, G->g, 15, 2);
  G->L->goal_x = G->x - 3;
  close_level(G, biome);
}
static void l_landlord(gen *G) {
  enum { B = BIOME_MEADOW };
  begin(G, B, 18);
  flat(G, 12, B);
  sign(G, "RENT WAS DUE. THE LANDLORD WOULD LIKE A WORD. IT IS A SHORT WORD.");
  flat(G, 12, B);
  coin_arc(G, G->x, 8, G->g + 2);
  gap(G, 8, B, T_SPIKE);
  flat(G, 6, B);
  spring(G, G->x + 2);
  flat(G, 4, B);
  rise(G, 5, 10, B);
  saw(G, G->x - 7, G->g + 1, 3, 3.0f);
  flat(G, 6, B);
  crumble_bridge(G, 10);
  flat(G, 12, B);
  check(G);
  EXTEND(220, S_RUNGAP, S_SAW, S_CRUMBLE, S_SPRING, S_WALKERS);
  boss_room(G, B, 0, "JUMP ON THE LANDLORD. HE HAS BEEN ON ALL OF YOU FOR YEARS.");
}

/* ── world 2: the caves ──────────────────────────────────────────── */
static void l_claws_out(gen *G) {
  enum { B = BIOME_CAVES };
  begin(G, B, 18);
  int c0 = G->x;
  flat(G, 12, B);
  sign(G, "THE CAVE IS DARK BECAUSE THE BATS HAVE UNIONIZED.");
  flat(G, 8, B);
  pedestal(G, AB_CLAWS);
  flat(G, 6, B);
  sign(G, "CLAWS. PRESS JUMP ON A WALL. THE WALL HAS NOT BEEN CONSULTED.");
  flat(G, 6, B);
  shaft(G, 3, 8, B);                            /* a shaft: door low on the left, up the inside */
  flat(G, 8, B);
  check(G);
  walker(G, G->x - 3, G->g, 2, 0);
  flat(G, 10, B);
  coin_arc(G, G->x, 8, G->g + 2);
  gap(G, 8, B, T_SPIKE);
  flat(G, 5, B);
  shaft(G, 4, 10, B);
  sign(G, "THE CEILING IS NOW THE FLOOR'S PROBLEM.");
  flat(G, 6, B);
  check(G);
  rise(G, -6, 8, B);
  spikes(G, 3, B);
  flat(G, 12, B);
  coin_row(G, G->x - 8, 5, G->g + 1);
  pocket(G, 3, 4, B);
  secret_sign(G, "A BAT LIVED HERE. HE LEFT WHEN THE RENT WENT UP.");
  rise(G, -3, 10, B);
  check(G);
  walker(G, G->x - 3, G->g, 2, 0);
  gap(G, 4, B, T_SPIKE);
  flat(G, 8, B);
  shaft(G, 3, 12, B);
  flat(G, 12, B);
  coin_arc(G, G->x, 8, G->g + 2);
  gap(G, 9, B, T_SPIKE);
  flat(G, 8, B);
  check(G);
  rise(G, -6, 10, B);
  EXTEND(400, S_SHAFT, S_RUNGAP, S_WALKERS, S_HOPS, S_LEDGES, S_DROP);
  sign(G, "YOU HAVE LEFT THE WALL. THE WALL HAS ASKED US TO SAY IT IS FINE.");
  ceiling(G, c0, G->x - c0, 11);
  L_finish(G, B);
}

static void l_complimentary_piano(gen *G) {
  enum { B = BIOME_CAVES };
  begin(G, B, 18);
  int c0 = G->x;
  flat(G, 12, B);
  sign(G, "FALLING OBJECTS ARE COMPLIMENTARY. PLEASE TAKE ONE.");
  flat(G, 12, B);
  faller(G, G->x - 6);
  coin_row(G, G->x - 8, 5, G->g + 1);
  flat(G, 10, B);
  check(G);
  flat(G, 8, B);
  faller(G, G->x - 3);
  flat(G, 8, B);
  faller(G, G->x - 3);
  sign(G, "THE PIANO IS IN TUNE. IT WAS ONLY EVER BADLY PLACED.");
  flat(G, 12, B);
  gap(G, 8, B, T_SPIKE);
  flat(G, 5, B);
  check(G);
  shaft(G, 3, 9, B);
  flat(G, 8, B);
  faller(G, G->x - 2);
  flat(G, 6, B);
  faller(G, G->x - 2);
  flat(G, 6, B);
  pocket(G, 4, 3, B);
  secret_sign(G, "NOTHING FALLS IN HERE. THE ROOF IS HOLDING ITS BREATH.");
  rise(G, -3, 8, B);
  check(G);
  flat(G, 12, B);
  faller(G, G->x - 9);
  faller(G, G->x - 5);
  faller(G, G->x - 2);
  coin_row(G, G->x - 10, 8, G->g + 1);
  gap(G, 5, B, T_SPIKE);
  flat(G, 12, B);
  coin_arc(G, G->x, 8, G->g + 2);
  gap(G, 8, B, T_SPIKE);
  flat(G, 8, B);
  check(G);
  EXTEND(400, S_FALL, S_SHAFT, S_RUNGAP, S_WALKERS, S_HOPS, S_DROP);
  sign(G, "A FINAL PIANO HAS BEEN PREPARED IN YOUR HONOUR.");
  flat(G, 8, B);
  faller(G, G->x - 2);
  ceiling(G, c0, G->x - c0, 11);
  L_finish(G, B);
}

static void l_mine(gen *G) {
  enum { B = BIOME_CAVES };
  begin(G, B, 18);
  int c0 = G->x;
  flat(G, 12, B);
  sign(G, "THE BELT RUNS TOWARD THE MANAGEMENT. EVERYTHING DOES.");
  flat(G, 6, B);
  belt(G, 18, 3.0f, B);
  coin_row(G, G->x - 14, 8, G->g + 1);
  flat(G, 8, B);
  check(G);
  sign(G, "RUN AGAINST IT IF YOU LIKE. MANY HAVE. THEY ARE STILL RUNNING.");
  flat(G, 6, B);
  belt(G, 20, -3.5f, B);
  flat(G, 10, B);
  coin_arc(G, G->x, 8, G->g + 2);
  gap(G, 8, B, T_SPIKE);
  belt(G, 14, 4.0f, B);
  gap(G, 9, B, T_SPIKE);
  flat(G, 8, B);
  check(G);
  shaft(G, 3, 9, B);
  flat(G, 8, B);
  walker(G, G->x - 3, G->g, 2, 0);
  belt(G, 16, -4.0f, B);
  spikes(G, 2, B);
  flat(G, 12, B);
  pocket(G, 3, 4, B);
  secret_sign(G, "A COAL MINE WITH NO COAL. THEY ARE MINING THE COINS. DO NOT ASK WHO PUT THEM THERE.");
  rise(G, -3, 8, B);
  check(G);
  belt(G, 12, 5.0f, B);
  gap(G, 10, B, T_SPIKE);
  belt(G, 12, 5.0f, B);
  gap(G, 10, B, T_SPIKE);
  flat(G, 8, B);
  check(G);
  EXTEND(400, S_BELT, S_WALKERS, S_FALL, S_SHAFT, S_RUNGAP, S_HOPS, S_DROP);
  sign(G, "THIS LEVEL WAS AN APPRENTICESHIP. YOU HAVE BEEN PAID IN EXPERIENCE.");
  flat(G, 8, B);
  ceiling(G, c0, G->x - c0, 11);
  L_finish(G, B);
}

static void l_health_safety(gen *G) {
  enum { B = BIOME_CAVES };
  begin(G, B, 18);
  int c0 = G->x;
  flat(G, 12, B);
  sign(G, "IT IS HEALTH AND SAFETY WEEK. WE HAVE BROUGHT IN A PRESS.");
  flat(G, 12, B);
  press(G, G->x - 6, 200, 0);
  flat(G, 8, B);
  check(G);
  sign(G, "DAYS SINCE LAST INCIDENT: 0. DAYS BEFORE THAT: ALSO 0.");
  flat(G, 16, B);
  press(G, G->x - 12, 180, 0);
  press(G, G->x - 4, 180, 60);
  coin_row(G, G->x - 11, 8, G->g + 1);
  flat(G, 10, B);
  gap(G, 8, B, T_SPIKE);
  flat(G, 5, B);
  check(G);
  shaft(G, 3, 9, B);
  flat(G, 24, B);
  press(G, G->x - 20, 150, 0);
  press(G, G->x - 12, 150, 40);
  press(G, G->x - 4, 150, 80);
  flat(G, 6, B);
  pocket(G, 4, 3, B);
  secret_sign(G, "THIS ROOM IS RATED SAFE. NOBODY HAS BEEN ABLE TO FIND IT TO CHECK.");
  rise(G, -3, 8, B);
  check(G);
  flat(G, 12, B);
  belt(G, 18, 3.5f, B);
  press(G, G->x - 12, 170, 0);
  press(G, G->x - 4, 170, 50);
  gap(G, 8, B, T_SPIKE);
  flat(G, 12, B);
  coin_arc(G, G->x, 8, G->g + 2);
  gap(G, 8, B, T_SPIKE);
  flat(G, 10, B);
  check(G);
  faller(G, G->x - 3);
  EXTEND(400, S_PRESS, S_WALKERS, S_BELT, S_FALL, S_SHAFT, S_RUNGAP, S_DROP);
  sign(G, "THIS WEEK'S WINNER OF THE SAFETY AWARD HAS BEEN SCRAPED OFF AND NOTIFIED.");
  flat(G, 8, B);
  ceiling(G, c0, G->x - c0, 11);
  L_finish(G, B);
}

static void l_tax_golem(gen *G) {
  enum { B = BIOME_CAVES };
  begin(G, B, 18);
  int c0 = G->x;
  flat(G, 14, B);
  sign(G, "YOU HAVE COLLECTED COINS. THE TAX GOLEM HAS BEEN KEEPING COUNT.");
  flat(G, 8, B);
  press(G, G->x - 4, 170, 0);
  belt(G, 12, 3.5f, B);
  gap(G, 8, B, T_SPIKE);
  flat(G, 6, B);
  shaft(G, 3, 8, B);
  flat(G, 6, B);
  faller(G, G->x - 2);
  flat(G, 6, B);
  check(G);
  EXTEND(220, S_PRESS, S_BELT, S_FALL, S_SHAFT, S_RUNGAP);
  boss_room(G, B, 1, "THE TAX GOLEM ONLY RESPONDS TO BEING JUMPED ON. IT IS HOW HE WAS AUDITED.");
  ceiling(G, c0, G->x - c0 - 40, 11);
}

/* ── world 3: the ruins ──────────────────────────────────────────── */
static void l_free_swim(gen *G) {
  enum { B = BIOME_RUINS };
  begin(G, B, 26);
  flat(G, 12, B);
  sign(G, "NO RUNNING BY THE POOL. HOLD X.");
  flat(G, 6, B);
  pool(G, 16, 5, 0, 0);
  flat(G, 8, B);
  check(G);
  sign(G, "THE WATER IS FINE. WE DO NOT KNOW WHOSE IT IS.");
  flat(G, 6, B);
  pool(G, 20, 6, 3.0f, 1);
  flat(G, 8, B);
  check(G);
  walker(G, G->x - 3, G->g, 2, 0);
  flat(G, 12, B);
  coin_arc(G, G->x, 8, G->g + 2);
  gap(G, 8, B, T_SPIKE);
  flat(G, 6, B);
  pool(G, 24, 7, -3.0f, 2);
  secret_sign(G, "A WELL UNDER THE POOL. PEOPLE PAY EXTRA FOR THIS.");
  flat(G, 8, B);
  check(G);
  plat(G, 8, 2.2f, 0.35f, B);
  flat(G, 12, B);
  sign(G, "A LIFEGUARD WAS ON DUTY. HE IS NOW PART OF THE RUINS.");
  gap(G, 9, B, T_SPIKE);
  flat(G, 8, B);
  pool(G, 16, 5, 4.0f, 0);
  flat(G, 8, B);
  check(G);
  rise(G, 3, 6, B);
  pocket(G, 3, 4, B);
  rise(G, -3, 8, B);
  EXTEND(380, S_POOL, S_RUNGAP, S_WALKERS, S_PLAT, S_LEDGES, S_STEPS);
  sign(G, "THANK YOU FOR SWIMMING. THE POOL WILL KEEP WHAT IT HAS.");
  L_finish(G, B);
}

static void l_hold_door(gen *G) {
  enum { B = BIOME_RUINS };
  begin(G, B, 20);
  flat(G, 12, B);
  sign(G, "STAND ON THE PLATE AND THE DOOR OPENS. RUN AND IT MIGHT STILL BE OPEN.");
  flat(G, 4, B);
  plate_door(G, 300, 12);
  flat(G, 8, B);
  check(G);
  sign(G, "WITH A FRIEND THIS IS EASIER. ONE OF YOU STAYS BEHIND. CHOOSE NOW. WE WILL WAIT.");
  flat(G, 4, B);
  plate_door(G, 210, 18);
  flat(G, 6, B);
  coin_row(G, G->x - 4, 3, G->g + 1);
  gap(G, 8, B, T_SPIKE);
  flat(G, 8, B);
  check(G);
  plat(G, 8, 2.2f, 0.35f, B);
  flat(G, 6, B);
  plate_door(G, 180, 14);
  pocket(G, 3, 4, B);
  secret_sign(G, "NOBODY HAS EVER HELD A DOOR FOR YOU THIS FAR IN. FEEL THAT.");
  rise(G, -3, 8, B);
  check(G);
  walker(G, G->x - 3, G->g, 2, 0);
  pool(G, 14, 5, 3.0f, 0);
  flat(G, 4, B);
  plate_door(G, 240, 22);
  flat(G, 8, B);
  check(G);
  saw(G, G->x - 4, G->g + 1, 3, 3.0f);
  flat(G, 8, B);
  EXTEND(400, S_PLATE, S_WALKERS, S_RUNGAP, S_PLAT, S_SAW, S_LEDGES, S_STEPS, S_POOL);
  sign(G, "THE DOOR WOULD LIKE YOU TO KNOW IT NEVER LIKED ANY OF YOU.");
  L_finish(G, B);
}

static void l_plumbing(gen *G) {
  enum { B = BIOME_RUINS };
  begin(G, B, 28);
  flat(G, 12, B);
  sign(G, "THIS PLUMBING IS ANCIENT. SO IS THE SMELL.");
  flat(G, 4, B);
  pool(G, 26, 8, 4.0f, 2);
  flat(G, 6, B);
  check(G);
  pool(G, 26, 8, -4.0f, 0);
  flat(G, 6, B);
  sign(G, "THE CURRENT RUNS BOTH WAYS. SO DOES THE BLAME.");
  check(G);
  fan(G, G->x + 4, 3, 9);
  flat(G, 7, B);
  rise(G, 7, 8, B);
  coin_row(G, G->x - 6, 4, G->g + 2);
  check(G);
  pool(G, 20, 6, 3.0f, 3);
  secret_sign(G, "SOMEONE HAS LOST A SLIPPER DOWN HERE. A VERY LONG TIME AGO.");
  flat(G, 12, B);
  gap(G, 8, B, T_SPIKE);
  flat(G, 8, B);
  check(G);
  plate_door(G, 240, 14);
  pool(G, 14, 5, 4.0f, 0);
  flat(G, 12, B);
  EXTEND(380, S_POOL, S_WALKERS, S_FAN, S_PLATE, S_RUNGAP, S_STEPS, S_LEDGES);
  sign(G, "A REPAIRMAN IS ON THE WAY. HE LEFT IN THE FOURTH CENTURY.");
  L_finish(G, B);
}

static void l_load_bearing(gen *G) {
  enum { B = BIOME_RUINS };
  begin(G, B, 20);
  flat(G, 12, B);
  sign(G, "THE DASH. RIGHT SHOULDER. THROUGH THE CRACKED WALL. IT IS PROBABLY LOAD-BEARING.");
  flat(G, 8, B);
  pedestal(G, AB_DASH);
  flat(G, 8, B);
  crack_wall(G);
  flat(G, 8, B);
  check(G);
  sign(G, "YOU HAVE BROKEN A THOUSAND YEARS OF HISTORY. THE GIFT SHOP WILL BE IN TOUCH.");
  flat(G, 12, B);
  crack_wall(G);
  coin_row(G, G->x - 3, 2, G->g + 1);
  flat(G, 6, B);
  gap(G, 9, B, T_SPIKE);
  flat(G, 4, B);
  crack_wall(G);
  flat(G, 8, B);
  check(G);
  plat(G, 8, 2.2f, 0.35f, B);
  flat(G, 10, B);
  saw(G, G->x - 5, G->g + 1, 3, 3.0f);
  crack_wall(G);
  flat(G, 8, B);
  pocket(G, 4, 3, B);
  secret_sign(G, "A ROOM THAT WAS SEALED FOR A REASON. THE REASON WAS COINS.");
  rise(G, -3, 8, B);
  check(G);
  pool(G, 14, 5, 3.0f, 0);
  flat(G, 12, B);
  coin_arc(G, G->x, 10, G->g + 3);
  gap(G, 12, B, T_SPIKE);                       /* a run and a dash, or a run and an air jump */
  flat(G, 8, B);
  EXTEND(400, S_CRACK, S_WALKERS, S_RUNGAP, S_PLAT, S_SAW, S_PLATE, S_POOL, S_STEPS);
  sign(G, "THE RUINS WERE FINE UNTIL YOU GOT HERE. WE HAVE PHOTOS.");
  L_finish(G, B);
}

static void l_lifeguard(gen *G) {
  enum { B = BIOME_RUINS };
  begin(G, B, 24);
  flat(G, 12, B);
  sign(G, "THE LIFEGUARD HAS SEEN YOU RUNNING. HE HAS SEEN ALL OF IT.");
  flat(G, 6, B);
  pool(G, 14, 5, 3.0f, 0);
  flat(G, 6, B);
  plate_door(G, 240, 12);
  flat(G, 6, B);
  crack_wall(G);
  flat(G, 8, B);
  check(G);
  EXTEND(220, S_POOL, S_PLATE, S_CRACK, S_RUNGAP);
  boss_room(G, B, 2, "THE LIFEGUARD IS STRONG, FAST AND HAS NEVER SAVED ANYONE.");
}

/* ── world 4: the frozen head office ─────────────────────────────── */
static void l_casual_friday(gen *G) {
  enum { B = BIOME_FROST };
  begin(G, B, 20);
  flat(G, 10, B);
  sign(G, "WELCOME TO HEAD OFFICE. THE HEATING WAS CUT TO IMPROVE MORALE.");
  flat(G, 6, B);
  ice(G, 18, B);
  coin_row(G, G->x - 14, 6, G->g + 1);
  flat(G, 6, B);
  check(G);
  sign(G, "THE ICE IS A BENEFIT. WE HAVE CANCELLED THE OTHER BENEFITS.");
  ice(G, 16, B);
  coin_arc(G, G->x, 8, G->g + 2);
  gap(G, 8, B, T_SPIKE);
  ice(G, 10, B);
  spikes(G, 3, B);
  flat(G, 12, B);
  check(G);
  walker(G, G->x - 3, G->g, 2, 0);
  ice(G, 14, B);
  rise(G, 3, 8, B);
  pocket(G, 3, 4, B);
  secret_sign(G, "THE SUPPLY CLOSET. NOBODY HAS DARED TAKE THE LAST PEN.");
  rise(G, -3, 8, B);
  check(G);
  ice(G, 12, B);
  gap(G, 9, B, T_SPIKE);
  ice(G, 12, B);
  gap(G, 9, B, T_SPIKE);
  flat(G, 10, B);
  check(G);
  crumble_bridge(G, 12);
  ice(G, 14, B);
  EXTEND(400, S_ICE, S_RUNGAP, S_CRACK, S_WALKERS, S_SHAFT, S_DROP, S_STEPS);
  sign(G, "PLEASE FILL IN THE FORM ABOUT HOW MUCH YOU ENJOYED CASUAL FRIDAY.");
  L_finish(G, B);
}

static void l_air_con(gen *G) {
  enum { B = BIOME_FROST };
  begin(G, B, 18);
  flat(G, 12, B);
  sign(G, "THE AIR CONDITIONING IS ON. YES. HERE. IT IS IN YOUR CONTRACT.");
  fan(G, G->x + 6, 3, 8);
  flat(G, 9, B);
  rise(G, 6, 8, B);
  coin_row(G, G->x - 6, 4, G->g + 2);
  check(G);
  flat(G, 6, B);
  fan_gap(G, 8, 8);
  flat(G, 8, B);
  sign(G, "THE FAN IS MANAGEMENT'S ONLY FORM OF SUPPORT.");
  check(G);
  ice(G, 10, B);
  fan_gap(G, 10, 9);
  coin_row(G, G->x - 8, 6, G->g + 5);
  ice(G, 10, B);
  fan(G, G->x + 4, 3, 10);
  flat(G, 7, B);
  rise(G, 8, 6, B);
  pocket(G, 4, 3, B);
  secret_sign(G, "THE ONLY WARM ROOM IN THE BUILDING. IT IS A SERVER CUPBOARD.");
  rise(G, -3, 8, B);
  check(G);
  flat(G, 8, B);
  saw(G, G->x - 4, G->g + 1, 3, 3.0f);
  fan_gap(G, 12, 10);
  flat(G, 8, B);
  check(G);
  plat(G, 8, 2.2f, 0.4f, B);
  flat(G, 12, B);
  EXTEND(400, S_FAN, S_WALKERS, S_ICE, S_RUNGAP, S_SAW, S_STEPS, S_DROP);
  sign(G, "THE THERMOSTAT IS A PAINTING OF A THERMOSTAT.");
  L_finish(G, B);
}

static void l_conveyor_opportunity(gen *G) {
  enum { B = BIOME_FROST };
  begin(G, B, 18);
  flat(G, 12, B);
  sign(G, "THE CONVEYOR OF OPPORTUNITY. IT GOES WHERE OPPORTUNITY GOES. SIDEWAYS.");
  belt(G, 16, 3.0f, B);
  ice(G, 8, B);
  belt(G, 16, -3.0f, B);
  flat(G, 8, B);
  check(G);
  press(G, G->x - 3, 170, 0);
  belt(G, 14, 4.0f, B);
  press(G, G->x - 7, 170, 40);
  gap(G, 9, B, T_SPIKE);
  ice(G, 8, B);
  check(G);
  fan_gap(G, 9, 8);
  belt(G, 14, -4.0f, B);
  flat(G, 8, B);
  pocket(G, 3, 4, B);
  secret_sign(G, "A MEETING ROOM NOBODY BOOKED. IT HAS BEEN RUNNING SINCE MARCH.");
  rise(G, -3, 8, B);
  check(G);
  belt(G, 12, 5.0f, B);
  gap(G, 10, B, T_SPIKE);
  ice(G, 12, B);
  flat(G, 8, B);
  EXTEND(400, S_BELT, S_ICE, S_WALKERS, S_FAN, S_PRESS, S_RUNGAP, S_DROP);
  sign(G, "YOU HAVE ARRIVED AT OPPORTUNITY. IT HAS GONE TO LUNCH.");
  L_finish(G, B);
}

static void l_launch_program(gen *G) {
  enum { B = BIOME_FROST };
  begin(G, B, 18);
  flat(G, 12, B);
  sign(G, "YOU ARE BEING LET GO. CLIMB IN AND PRESS JUMP.");
  flat(G, 4, B);
  cannon(G, G->x, 16, 15);
  flat(G, 4, B);
  gap(G, 16, B, T_SPIKE);
  flat(G, 8, B);
  check(G);
  sign(G, "THIS IS THE EMPLOYEE LAUNCH PROGRAM. YOU ARE THE EMPLOYEE.");
  cannon(G, G->x + 2, 7, 29);
  flat(G, 6, B);
  rise(G, 10, 8, B);
  coin_row(G, G->x - 6, 4, G->g + 2);
  check(G);
  cannon(G, G->x + 2, 18, 14);
  flat(G, 5, B);
  gap(G, 18, B, T_SPIKE);
  flat(G, 5, B);
  cannon(G, G->x + 2, 18, 12);
  flat(G, 5, B);
  gap(G, 15, B, T_SPIKE);
  ice(G, 10, B);
  check(G);
  pocket(G, 3, 4, B);
  secret_sign(G, "SEVERANCE PAY. IT IS FOUR COINS. THAT IS ALL OF IT.");
  rise(G, -3, 8, B);
  check(G);
  belt(G, 14, 4.0f, B);
  gap(G, 9, B, T_SPIKE);
  flat(G, 8, B);
  EXTEND(400, S_CANNON, S_WALKERS, S_ICE, S_BELT, S_FAN, S_RUNGAP, S_DROP);
  sign(G, "THANK YOU FOR YOUR FIVE YEARS OF SERVICE. IT HAS BEEN FOUR MINUTES.");
  L_finish(G, B);
}

static void l_regional_manager(gen *G) {
  enum { B = BIOME_FROST };
  begin(G, B, 18);
  flat(G, 14, B);
  sign(G, "THE REGIONAL MANAGER WILL SEE YOU NOW. HE CANNOT SEE ANYTHING ELSE.");
  ice(G, 12, B);
  cannon(G, G->x + 2, 16, 14);
  flat(G, 5, B);
  gap(G, 15, B, T_SPIKE);
  flat(G, 8, B);
  fan_gap(G, 9, 8);
  flat(G, 8, B);
  check(G);
  EXTEND(220, S_ICE, S_CANNON, S_FAN, S_BELT, S_RUNGAP);
  boss_room(G, B, 3, "HE WILL TELL YOU YOUR PERFORMANCE IS BELOW EXPECTATIONS. THEN HE WILL JUMP ON YOU.");
}

/* ── world 5: the crater ─────────────────────────────────────────── */
static void l_literal_lava(gen *G) {
  enum { B = BIOME_VOLCANO };
  begin(G, B, 18);
  flat(G, 12, B);
  sign(G, "THE FLOOR IS LAVA. THIS IS NOT A GAME. IT IS A LEASE.");
  flat(G, 8, B);
  coin_arc(G, G->x, 6, G->g + 2);
  gap(G, 6, B, T_LAVA);
  flat(G, 12, B);
  check(G);
  coin_arc(G, G->x, 9, G->g + 2);
  gap(G, 9, B, T_LAVA);
  flat(G, 6, B);
  walker(G, G->x - 3, G->g, 2, 1);
  flat(G, 10, B);
  sign(G, "THE LAVA WILL RETURN WHATEVER IT FINDS. NOT IN THE SAME CONDITION.");
  gap(G, 9, B, T_LAVA);
  oneway(G, G->x - 6, G->g + 1, 2);
  flat(G, 8, B);
  check(G);
  crumble_bridge(G, 10);
  flat(G, 8, B);
  pocket(G, 3, 4, B);
  secret_sign(G, "THE ONLY ROOM HERE WITH A CEILING. IT IS FOR SALE.");
  rise(G, -3, 8, B);
  check(G);
  plat(G, 9, 2.2f, 0.4f, B);
  flat(G, 12, B);
  gap(G, 10, B, T_LAVA);
  flat(G, 12, B);
  gap(G, 10, B, T_LAVA);
  flat(G, 8, B);
  EXTEND(400, S_LAVAGAP, S_CRUMBLE, S_PLAT, S_HOPS, S_WALKERS, S_DROP, S_STEPS);
  sign(G, "YOUR DEPOSIT HAS EVAPORATED.");
  L_finish(G, B);
}

static void l_complimentary_rocks(gen *G) {
  enum { B = BIOME_VOLCANO };
  begin(G, B, 18);
  flat(G, 12, B);
  sign(G, "COMPLIMENTARY ROCKS. ONE PER CUSTOMER. FROM A GREAT HEIGHT.");
  flat(G, 12, B);
  faller(G, G->x - 7);
  faller(G, G->x - 3);
  flat(G, 8, B);
  check(G);
  gap(G, 9, B, T_LAVA);
  flat(G, 8, B);
  faller(G, G->x - 3);
  gap(G, 6, B, T_LAVA);
  flat(G, 10, B);
  check(G);
  shaft(G, 3, 9, B);
  flat(G, 8, B);
  faller(G, G->x - 2);
  flat(G, 6, B);
  pocket(G, 4, 3, B);
  secret_sign(G, "A ROCK LIVED HERE. IT WAS ASKED TO LEAVE, QUICKLY, DOWNWARD.");
  rise(G, -3, 8, B);
  check(G);
  flat(G, 12, B);
  coin_arc(G, G->x, 10, G->g + 2);
  gap(G, 10, B, T_LAVA);
  flat(G, 8, B);
  faller(G, G->x - 2);
  saw(G, G->x - 6, G->g + 1, 3, 3.0f);
  flat(G, 12, B);
  EXTEND(400, S_FALL, S_LAVAGAP, S_WALKERS, S_SHAFT, S_SAW, S_DROP, S_STEPS);
  sign(G, "THE MOUNTAIN IS NOT ANGRY. IT IS DISAPPOINTED.");
  L_finish(G, B);
}

static void l_severance(gen *G) {
  enum { B = BIOME_VOLCANO };
  begin(G, B, 18);
  flat(G, 12, B);
  sign(G, "YOUR SEVERANCE PACKAGE IS A CANNON. PLEASE READ IT CAREFULLY.");
  cannon(G, G->x + 2, 18, 13);
  flat(G, 5, B);
  gap(G, 16, B, T_LAVA);
  flat(G, 8, B);
  check(G);
  cannon(G, G->x + 2, 7, 27);
  flat(G, 6, B);
  rise(G, 8, 8, B);
  check(G);
  cannon(G, G->x + 2, 18, 14);
  flat(G, 5, B);
  gap(G, 18, B, T_LAVA);
  flat(G, 6, B);
  check(G);
  fan_gap(G, 9, 8);
  flat(G, 8, B);
  pocket(G, 3, 4, B);
  secret_sign(G, "A PENSION. IT WAS STOLEN BY THE LAST PERSON WHO FOUND IT.");
  rise(G, -3, 8, B);
  check(G);
  flat(G, 12, B);
  gap(G, 10, B, T_LAVA);
  flat(G, 10, B);
  EXTEND(400, S_CANNON, S_LAVAGAP, S_WALKERS, S_FAN, S_SHAFT, S_DROP, S_STEPS);
  sign(G, "THE CANNON WOULD LIKE A REFERENCE.");
  L_finish(G, B);
}

static void l_everything(gen *G) {
  enum { B = BIOME_VOLCANO };
  begin(G, B, 18);
  flat(G, 12, B);
  sign(G, "THIS LEVEL IS ALL OF IT AGAIN. THE BUDGET RAN OUT BUT THE SPITE DID NOT.");
  flat(G, 8, B);
  spring(G, G->x + 2);
  flat(G, 5, B);
  rise(G, 5, 6, B);
  crumble_bridge(G, 8);
  flat(G, 6, B);
  saw(G, G->x - 3, G->g + 1, 2, 3.2f);
  check(G);
  belt(G, 12, 4.0f, B);
  gap(G, 10, B, T_LAVA);
  flat(G, 5, B);
  shaft(G, 3, 9, B);
  flat(G, 6, B);
  check(G);
  press(G, G->x - 3, 170, 0);
  flat(G, 6, B);
  faller(G, G->x - 3);
  plate_door(G, 240, 14);
  flat(G, 6, B);
  crack_wall(G);
  flat(G, 8, B);
  check(G);
  ice(G, 12, B);
  gap(G, 9, B, T_LAVA);
  flat(G, 6, B);
  fan_gap(G, 9, 8);
  flat(G, 5, B);
  cannon(G, G->x + 2, 18, 13);
  flat(G, 5, B);
  gap(G, 16, B, T_LAVA);
  flat(G, 8, B);
  check(G);
  pocket(G, 4, 3, B);
  secret_sign(G, "THIS ROOM HAS NO JOKE. WE RAN OUT. HERE ARE SOME COINS.");
  rise(G, -3, 8, B);
  flat(G, 10, B);
  EXTEND(480, S_SPRING, S_CRUMBLE, S_SAW, S_BELT, S_SHAFT, S_PRESS, S_FALL, S_PLATE, S_CRACK, S_ICE, S_FAN, S_CANNON, S_WALKERS, S_LAVAGAP, S_DROP);
  sign(G, "THE FOUNDER IS EXPECTING YOU. HE IS EXPECTING MONEY, MORE PRECISELY.");
  L_finish(G, B);
}

static void l_founder(gen *G) {
  enum { B = BIOME_VOLCANO };
  begin(G, B, 18);
  flat(G, 14, B);
  sign(G, "THE FOUNDER STARTED ALL OF THIS IN A GARAGE. THE GARAGE WAS YOUR HOUSE.");
  flat(G, 12, B);
  gap(G, 9, B, T_LAVA);
  flat(G, 6, B);
  faller(G, G->x - 3);
  flat(G, 4, B);
  check(G);
  EXTEND(260, S_LAVAGAP, S_CANNON, S_FALL, S_SAW, S_PRESS, S_SHAFT);
  boss_room(G, B, 4, "THE FOUNDER. HE IS THE LAST THING STANDING BETWEEN YOU AND THE PAPERWORK.");
}

/* every level, in order */
#ifndef WORLDS_READY
#define WORLDS_READY 1 /* the worlds in the game so far: each joins when it is proven, the old tour goes when the last does */
#endif
static const level_info ALL_LEVELS[] = {
  { "Mandatory Fun", 0, l_mandatory_fun, 0, "Coins found: %d. Coins owed: %d. The Management thanks you for your patience." },
  { "Trampolines Of Unknown Origin", 0, l_trampolines, 0, "A trampoline has been named in your honour. It is suing." },
  { "The Ground Is A Suggestion", 0, l_ground_suggestion, 0, "The beaver sends his regards and an invoice." },
  { "Gardening Accident", 0, l_gardening, 0, "The hedge trimmer has asked for your number." },
  { "The Landlord", 0, l_landlord, AB_DOUBLE, "The Landlord is down. Your deposit stays down with him." },
  { "Claws Out", 1, l_claws_out, AB_DOUBLE, "The walls have filed a complaint. It is on the wall." },
  { "Complimentary Piano", 1, l_complimentary_piano, AB_DOUBLE | AB_CLAWS, "Thank you for attending the recital." },
  { "Mine, All Mine", 1, l_mine, AB_DOUBLE | AB_CLAWS, "You mined it. We own it. That is how mines work." },
  { "Health And Safety Week", 1, l_health_safety, AB_DOUBLE | AB_CLAWS, "Days since last incident: resetting." },
  { "The Tax Golem", 1, l_tax_golem, AB_DOUBLE | AB_CLAWS, "The Golem has been written off." },
  { "Free Swim, Coins Extra", 2, l_free_swim, AB_DOUBLE | AB_CLAWS, "Your towel has been charged to your account." },
  { "Hold The Door", 2, l_hold_door, AB_DOUBLE | AB_CLAWS, "Someone held the door. Nobody thanked them. Good." },
  { "Ancient Plumbing", 2, l_plumbing, AB_DOUBLE | AB_CLAWS, "The plumber will be there between now and the end of history." },
  { "It Is Probably Load-Bearing", 2, l_load_bearing, AB_DOUBLE | AB_CLAWS, "It was load-bearing." },
  { "The Lifeguard", 2, l_lifeguard, AB_DOUBLE | AB_CLAWS | AB_DASH, "He has finally saved someone: himself, a lot of paperwork." },
  { "Casual Friday", 3, l_casual_friday, AB_DOUBLE | AB_CLAWS | AB_DASH, "Your slide has been recorded for training purposes." },
  { "Air Conditioning", 3, l_air_con, AB_DOUBLE | AB_CLAWS | AB_DASH, "Please stop thanking the fan." },
  { "The Conveyor Of Opportunity", 3, l_conveyor_opportunity, AB_DOUBLE | AB_CLAWS | AB_DASH, "Your opportunity will be with you shortly." },
  { "Employee Launch Program", 3, l_launch_program, AB_DOUBLE | AB_CLAWS | AB_DASH, "You have been let go, from a great height." },
  { "The Regional Manager", 3, l_regional_manager, AB_DOUBLE | AB_CLAWS | AB_DASH, "The region is under new management. It is you. Sorry." },
  { "The Floor Is Literally Lava", 4, l_literal_lava, AB_DOUBLE | AB_CLAWS | AB_DASH, "Your shoes have been kept as a deposit." },
  { "Complimentary Rocks", 4, l_complimentary_rocks, AB_DOUBLE | AB_CLAWS | AB_DASH, "Please rate your rock." },
  { "Severance Package", 4, l_severance, AB_DOUBLE | AB_CLAWS | AB_DASH, "Your package has landed. So have you, mostly." },
  { "All Of It Again, Angrier", 4, l_everything, AB_DOUBLE | AB_CLAWS | AB_DASH, "That was everything. We checked." },
  { "The Founder", 4, l_founder, AB_DOUBLE | AB_CLAWS | AB_DASH, "You have inherited the company, its debts and the piano." },
  { "The Grand Tour", 4, l_grand_tour, AB_DOUBLE, "You have seen everything we have built so far." },
};
static level_info level_table[26];
const level_info *const LEVELS = level_table;
int NLEVELS;
/* the table in play: COOP_WORLDS (a developer's test of worlds not yet in) or the worlds proven so far */
void levels_set_worlds(int worlds) {
  int n = worlds * 5;
  for (int i = 0; i < n; i++) level_table[i] = ALL_LEVELS[i];
  if (worlds < 5) level_table[n++] = ALL_LEVELS[25];
  NLEVELS = n;
}
__attribute__((constructor)) static void levels_init(void) {
  const char *e = getenv("COOP_WORLDS");
  levels_set_worlds(e ? atoi(e) : WORLDS_READY);
}

const char *const WORLD_NAMES[NWORLDS] = {
  "The Meadow, Some Restrictions Apply",
  "The Caves, Now With Less Glimmer",
  "The Ruins, Ocean View",
  "Head Office, Frozen Over",
  "The Crater, Deposit Required",
};
