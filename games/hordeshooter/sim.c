/* hordeshooter rules: 1-4 players hold a walled yard against a horde.
 *
 *   enemies   every enemy is an agent of the engine's crowd (a dynamic
 *             navmesh, ORCA avoidance). A grid flow field from the players
 *             gives each one its heading; near a player it goes straight at
 *             them. Thousands at a time.
 *   weapons   twin stick: move, aim, hold to fire. XP gems level a player up
 *             and each level adds the next upgrade in the ladder.
 *   world     crates, barrels (they explode), cinder walls (explosions break
 *             them), pillars. A destroyed piece leaves the navmesh at once.
 *   waves     a wave every 30 s, a boss every fifth.
 * Everything here runs on the fixed tick with cartwheel math and cw_rng: the
 * same inputs give the same game on every host. */
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/profile.h"
#include "game.h"

static const char *const UP_NAME[] = { "Rapid fire", "Extra barrel", "Heavy rounds", "Piercing", "Rockets", "Boots", "Magnet", "Bigger blasts", "Armor" };
static const uint8_t LADDER[] = { 0, 1, 2, 3, 4, 0, 5, 1, 6, 7, 2, 8, 0, 3, 7, 4, 1, 5 };
const char *sim_upgrade_name(int u) { return UP_NAME[u % 9]; }
static const char *const PU_NAME[PU_KINDS] = { "SHOTGUN", "MINIGUN", "ROCKETS", "RAIL GUN", "FLAMER", "EXPLOSIVE ROUNDS", "RAPID FIRE",
                                               "GRENADES", "MINES", "BUZZSAWS", "SHIELD", "MAGNET",
                                               "ARC CASTER", "STAKE REPEATER", "SLAG MORTAR", "SEEKER SWARM", "SAINT'S WHEEL", "FROST LANCE",
                                               "HELLFIRE LANCE" };
const char *sim_power_name(int t) { return PU_NAME[(unsigned)t % PU_KINDS]; }
int sim_power_category(int t) { return pu_is_weapon(t) ? 0 : t <= PU_RAPID ? 1 : t <= PU_MINES ? 2 : t == PU_SAWS ? 3 : t == PU_SHIELD ? 4 : 5; }

static float rf(sim *s) { return cw_rng_float(&s->rng); }
static void ev(sim *s, int kind, float x, float y, float z, float w, int a, int b) {
  if (s->ev_n >= MAXEV) return;
  s->ev[s->ev_n++] = (event){ (uint8_t)kind, (uint8_t)a, (uint8_t)b, x, y, z, w };
}

/* ── enemies ────────────────────────────────────────────────────────── */

typedef struct ekind { float radius, speed_lo, speed_hi; int hp_base; float hp_wave; int dmg, xp; } ekind;
static const ekind EK[EK_KINDS] = {
  { 0.34f, 3.0f, 4.4f, 2, 0.34f, 7, 1 },
  { 0.72f, 1.8f, 2.4f, 16, 4.0f, 18, 6 },
  { 0.55f, 1.4f, 2.0f, 5, 0.8f, 6, 3 },
  { 1.7f, 1.5f, 1.9f, 700, 260.0f, 30, 90 },
  { 0.38f, 3.9f, 5.7f, 1, 0.2f, 6, 1 },   /* skitter: 1.3x a runner's speed, 60% of its health, in packs */
  { 0.42f, 2.4f, 3.5f, 4, 0.9f, 9, 2 },   /* bonewalker: 0.8x a runner's speed, a slow stiff lurch */
  { 0.6f, 2.1f, 3.1f, 7, 1.4f, 10, 3 },   /* slagborn: 0.7x, heavy; lobs slag from range, punches up close */
  { 0.45f, 2.7f, 4.0f, 3, 0.7f, 7, 2 },   /* sporeling: 0.9x, a swaying waddle; bursts into a spore cloud */
  { 0.4f, 3.3f, 4.8f, 4, 0.8f, 9, 2 },    /* penitent: 1.1x, a gliding wraith; draws back and rushes 4 m */
  { 0.45f, 3.0f, 4.4f, 5, 0.9f, 9, 2 },   /* gargoyle: hops; its wings flare at the top, then it dives */
  { 0.42f, 4.5f, 6.6f, 3, 0.7f, 10, 2 },  /* hellhound: 1.5x, the fastest thing in the game; a 4 m pounce */
};
static const char *const KIND_NAME[EK_KINDS] = { "runner", "brute", "bloater", "boss", "skitter", "bonewalker", "slagborn", "sporeling", "penitent", "gargoyle", "hellhound" };
const char *sim_kind_name(int kind) { return kind >= 0 && kind < EK_KINDS ? KIND_NAME[kind] : "?"; }
int sim_kind_by_name(const char *name) {
  for (int k = 0; k < EK_KINDS; k++)
    if (!strcmp(name, KIND_NAME[k])) return k;
  return -1;
}

/* melee: a wind-up the player can read (the attack clip's first pose, held), the strike on one tick, a
 * recovery; the damage lands only on a player still in reach and in front at the strike, so stepping away
 * during the wind-up dodges it. Ticks at 60 a second. Tuned after twin-stick horde games (Dead Nation's
 * shamblers and runners, Alienation, Helldivers 2's melee bugs): fast fodder winds up for about a quarter
 * second and lunges a little past contact, heavies a little over half a second with a long reach, so a
 * dense late wave still lands hits on a player who stands in it while one who keeps moving can slip a
 * single attacker.
 *   trigger: how far past touching an attack starts; reach: how far past touching the strike still lands;
 *   arc: the cosine of the half angle in front that the strike covers */
typedef struct melee { int windup, recover, cooldown; float trigger, reach, arc; } melee;
static const melee MEL[EK_KINDS] = {
  { 15, 14, 8, 0.25f, 0.7f, 0.3f },    /* runner: a claw swipe that turns into a lunge */
  { 34, 26, 12, 0.5f, 1.1f, 0.2f },    /* brute: an overhead smash */
  { 24, 22, 10, 0.3f, 0.8f, -0.2f },   /* bloater: a belly slam, wide */
  { 40, 30, 20, 1.0f, 2.2f, -0.3f },   /* boss: a big sweep, wider still */
  { 24, 18, 12, 2.6f, 2.8f, 0.5f },    /* skitter: crouches 0.4 s (forelegs up), springs about 3 m and bites */
  { 30, 22, 10, 0.3f, 0.9f, 0.2f },    /* bonewalker: a backhand slash, the blade arm swung far back for 0.5 s */
  { 26, 22, 14, 0.3f, 0.9f, 0.3f },    /* slagborn: a punch (its lob is its ranged attack) */
  { 24, 18, 12, 0.3f, 0.8f, 0.4f },    /* sporeling: a headbutt, the cap tilted back for 0.4 s */
  { 30, 24, 14, 3.4f, 3.8f, 0.6f },    /* penitent: draws back and rises 0.5 s, then rushes 4 m */
  { 24, 20, 14, 2.6f, 3.0f, 0.4f },    /* gargoyle: wings flared 0.4 s, then the dive */
  { 21, 18, 12, 3.4f, 3.8f, 0.6f },    /* hellhound: the throat flares 0.35 s, then a 4 m pounce */
};
#define STRIKE_HOLD 6 /* ticks the strike pose shows after the hit */

/* the floors' bosses (LEVELS.md): their arena attack and summon. Ticks at 60 a second.
 *   Vhessa (the sluice): the acid jet (her belly glows 1.2 s, then a 40-degree cone of acid that burns 6 s), the
 *   brood (she squats 1 s and drops 8 skitters behind her); her pincer sweep is her melee (140 degrees). Above 50%
 *   she sweeps and jets in turn; below, she broods every 12 s and moves faster; below 20% the jets come in pairs.
 *   Ossivar (the ossuary): the bone lances (his chest flares 1 s, then 5 slow spears in a fan; two fans below 25%),
 *   raise the dead (he kneels 1.5 s and every bone pile stands up, from 60% every 15 s); his crozier slam is his
 *   melee (a ring of spikes at 3 m all round him). */
enum { BOSS_YARD, BOSS_VHESSA, BOSS_OSSIVAR, BOSS_ANVILGRAVE, BOSS_MYCEL, BOSS_SERAPHEL, BOSS_KHARVAAL, BOSS_UNMAKER };
static int spawn_enemy(sim *s, int kind, float x, float y);
static bool in_box(const obstacle *o, float x, float y, float pad);
static bool in_hazard(const sim *s, int kind, float x, float y);
static void kill_enemy(sim *s, int i, int owner, float dx, float dy);
static void add_zone(sim *s, int kind, float x, float y, float r, int life);
static int boss_of(const sim *s) {
  static const int B[NLEVELS] = { BOSS_YARD, BOSS_VHESSA, BOSS_OSSIVAR, BOSS_ANVILGRAVE, BOSS_MYCEL, BOSS_SERAPHEL, BOSS_KHARVAAL, BOSS_UNMAKER };
  return s->fl[s->level].set ? B[s->level] : BOSS_YARD;
}
/*   Anvilgrave (the foundry): the pour (he tips a crucible 1.2 s: a molten river 12 m ahead that burns 8 s), the
 *   bellows (a roar: 6 slagborn rise); his hammer fall is his melee (a shockwave ring out to about 8 m).
 *   Mother Mycel (the grotto): the spore mortar (her crown swells 1.2 s, then 6 spore balls land where the players
 *   stand, marked a second before), the root eruption (she sinks 1 s: roots burst up in a line toward the farthest
 *   player); her root lash is her melee. */
/*   Seraphel (the cathedral): the choir beam (wings spread 1.5 s, then a sweep of fire across 90 degrees in front),
 *   the shroud step (wings folded 0.6 s: it reappears behind the farthest player, penitents rising); the censer sweeps
 *   a full circle at 4 m. Khar-Vaal (the rift): the shard nova (the crown spins 1.2 s, then a ring of 16 shards with
 *   two gaps), the fracture (a 1.5 s glow, then the floor gives way in a ring round him); the blade cleaves 120
 *   degrees. The Unmaker (the core): the hellstorm (1.5 s, then fire on circles round every player), the legion (a
 *   roar: the earlier floors' creatures pour in); the riven blade comes down along a 14 m line. */
static const struct { int tell[3], release, recover; } BACT[8] = {
  { { 0, 0, 0 }, 0, 0 },
  { { 0, 72, 60 }, 12, 30 },  /* Vhessa: jet tell 1.2 s, brood squat 1 s */
  { { 0, 60, 90 }, 12, 30 },  /* Ossivar: chest flare 1 s, the kneel 1.5 s */
  { { 0, 72, 60 }, 12, 36 },  /* Anvilgrave: the tip 1.2 s, the roar 1 s */
  { { 0, 72, 60 }, 12, 30 },  /* Mycel: the swelling 1.2 s, the sinking 1 s */
  { { 0, 90, 36 }, 12, 30 },  /* Seraphel: the wings 1.5 s, the fold 0.6 s */
  { { 0, 72, 90 }, 12, 30 },  /* Khar-Vaal: the crown 1.2 s, the glow 1.5 s */
  { { 0, 90, 60 }, 12, 36 },  /* the Unmaker: 1.5 s, the roar 1 s */
};
static void add_zone(sim *s, int kind, float x, float y, float r, int life) {
  for (int z = 0; z < MAXZ; z++)
    if (!s->z[z].alive) {
      s->z[z] = (zone){ true, (uint8_t)kind, cw_clampf(x, -HALF + 1, HALF - 1), cw_clampf(y, -HALF + 1, HALF - 1), r, life };
      ev(s, EV_ACID, s->z[z].x, s->z[z].y, r, 0, kind, 0);
      return;
    }
}
static float boss_frac(const sim *s, const enemy *e) { return s->boss_maxhp > 0 ? (float)e->hp / (float)s->boss_maxhp : 1; }
static void boss_release(sim *s, enemy *e, int b) {
  const player *tp = e->tgt >= 0 && e->tgt < MAXP ? &s->p[e->tgt] : NULL;
  float fx = cw_cos(e->face), fy = cw_sin(e->face), frac = boss_frac(s, e);
  if (b == BOSS_VHESSA && e->act == 1) { /* the acid jet: puddles along a 40-degree cone */
    for (int k = 0; k < 7; k++) {
      float a = e->face + (rf(s) - 0.5f) * 0.7f, d = 3.0f + rf(s) * 8.0f;
      for (int z = 0; z < MAXZ; z++)
        if (!s->z[z].alive) {
          s->z[z] = (zone){ true, 0, e->x + cw_cos(a) * d, e->y + cw_sin(a) * d, 1.3f + rf(s) * 0.5f, 360 };
          ev(s, EV_ACID, s->z[z].x, s->z[z].y, s->z[z].r, 0, 0, 0);
          break;
        }
    }
  } else if (b == BOSS_VHESSA && e->act == 2) { /* the brood: eight skitters behind her */
    for (int k = 0; k < 8; k++) {
      float a = e->face + 3.1416f + (rf(s) - 0.5f) * 1.6f, d = 2.5f + rf(s) * 2.0f;
      float x = cw_clampf(e->x + cw_cos(a) * d, -HALF + 1, HALF - 1), y = cw_clampf(e->y + cw_sin(a) * d, -HALF + 1, HALF - 1);
      if (spawn_enemy(s, EK_SKITTER, x, y) >= 0) ev(s, EV_BROOD, x, y, 0, 0, 0, 0);
    }
  } else if (b == BOSS_OSSIVAR && e->act == 1) { /* the bone lances: a fan of five (two fans when nearly dead) */
    int fans = frac < 0.25f ? 2 : 1;
    for (int f = 0; f < fans; f++)
      for (int k = 0; k < 5; k++) {
        float a = e->face + ((float)k - 2.0f) * 0.22f + (float)f * 0.11f, v = 9.0f - (float)f * 1.5f;
        for (int j = 0; j < MAXES; j++)
          if (!s->es[j].alive) {
            s->es[j] = (eshot){ true, e->x + cw_cos(a) * 1.8f, e->y + cw_sin(a) * 1.8f, cw_cos(a) * v, cw_sin(a) * v, 150, 12 };
            ev(s, EV_LANCE, s->es[j].x, s->es[j].y, s->es[j].vx, s->es[j].vy, 0, 0);
            break;
          }
      }
  } else if (b == BOSS_OSSIVAR && e->act == 2) { /* raise the dead: every pile of bones stands up */
    for (int k = 0; k < MAXPILE; k++) s->pl[k].rise = s->pl[k].alive ? 1 : s->pl[k].rise;
  } else if (b == BOSS_ANVILGRAVE && e->act == 1) { /* the pour: a molten river 12 m along his facing */
    for (int k = 0; k < 7; k++) add_zone(s, 1, e->x + fx * (2.5f + (float)k * 1.6f), e->y + fy * (2.5f + (float)k * 1.6f), 1.2f, 480);
  } else if (b == BOSS_ANVILGRAVE && e->act == 2) { /* the bellows: six slagborn rise round him */
    for (int k = 0; k < 6; k++) {
      float a = (float)k * 1.047f + rf(s) * 0.5f;
      if (spawn_enemy(s, EK_SLAGBORN, cw_clampf(e->x + cw_cos(a) * 5, -HALF + 1, HALF - 1), cw_clampf(e->y + cw_sin(a) * 5, -HALF + 1, HALF - 1)) >= 0)
        ev(s, EV_RISE, e->x + cw_cos(a) * 5, e->y + cw_sin(a) * 5, 0, 0, 0, 0);
    }
  } else if (b == BOSS_SERAPHEL && e->act == 1) { /* the choir beam: fire swept across 90 degrees in front */
    for (int k = 0; k < 9; k++) {
      float a = e->face + ((float)k - 4.0f) * 0.196f;
      for (int d = 0; d < 3; d++) add_zone(s, 1, e->x + cw_cos(a) * (4.0f + 3.0f * (float)d), e->y + cw_sin(a) * (4.0f + 3.0f * (float)d), 1.0f, 50);
    }
  } else if (b == BOSS_SERAPHEL && e->act == 2) { /* the shroud step: behind the farthest player, penitents rising where it was */
    int far = -1;
    float fd = -1;
    for (int k = 0; k < MAXP; k++) {
      float dx = s->p[k].x - e->x, dy = s->p[k].y - e->y;
      if (s->p[k].active && !s->p[k].down && dx * dx + dy * dy > fd) fd = dx * dx + dy * dy, far = k;
    }
    for (int k = 0; k < 4; k++) spawn_enemy(s, EK_PENITENT, cw_clampf(e->x + (float)(k % 2) * 3 - 1.5f, -HALF + 1, HALF - 1), cw_clampf(e->y + (float)(k / 2) * 3 - 1.5f, -HALF + 1, HALF - 1));
    if (far >= 0) {
      const player *p = &s->p[far];
      float a = p->aim + 3.1416f, nx = cw_clampf(p->x + cw_cos(a) * 4, -HALF + 2, HALF - 2), ny = cw_clampf(p->y + cw_sin(a) * 4, -HALF + 2, HALF - 2);
      cw_crowd_teleport(s->crowd, e->agent, (t3_vec3){ nx, 0, ny });
      e->x = nx, e->y = ny;
      ev(s, EV_RISE, nx, ny, 0, 0, 0, 0);
    }
  } else if (b == BOSS_KHARVAAL && e->act == 1) { /* the shard nova: 16 shards in a ring, two gaps */
    int gap = (int)(rf(s) * 16);
    for (int k = 0; k < 16; k++) {
      if (k == gap || k == (gap + 8) % 16) continue;
      float a = (float)k * 0.3927f;
      for (int j = 0; j < MAXES; j++)
        if (!s->es[j].alive) {
          s->es[j] = (eshot){ true, e->x + cw_cos(a) * 2.2f, e->y + cw_sin(a) * 2.2f, cw_cos(a) * 8.0f, cw_sin(a) * 8.0f, 150, 12 };
          ev(s, EV_LANCE, s->es[j].x, s->es[j].y, s->es[j].vx, s->es[j].vy, 0, 0);
          break;
        }
    }
  } else if (b == BOSS_KHARVAAL && e->act == 2) { /* the fracture: the floor falls in a ring round him */
    for (int k = 0; k < 10; k++) add_zone(s, 1, e->x + cw_cos((float)k * 0.628f) * 6.0f, e->y + cw_sin((float)k * 0.628f) * 6.0f, 1.6f, 120);
  } else if (b == BOSS_UNMAKER && e->act == 1) { /* the hellstorm: fire on circles round every player */
    for (int k = 0; k < MAXP; k++)
      if (s->p[k].active && !s->p[k].down)
        for (int j = 0; j < 3; j++) add_zone(s, 1, s->p[k].x + (rf(s) - 0.5f) * 6, s->p[k].y + (rf(s) - 0.5f) * 6, 1.5f, 120);
  } else if (b == BOSS_UNMAKER && e->act == 2) { /* the legion: the earlier floors' creatures */
    static const int LEG[6] = { EK_SKITTER, EK_BONEWALKER, EK_SLAGBORN, EK_SPORELING, EK_PENITENT, EK_GARGOYLE };
    for (int k = 0; k < 8; k++) {
      float a = rf(s) * 6.28f, d = 14 + rf(s) * 8;
      spawn_enemy(s, LEG[k % 6], cw_clampf(e->x + cw_cos(a) * d, -HALF + 1, HALF - 1), cw_clampf(e->y + cw_sin(a) * d, -HALF + 1, HALF - 1));
    }
  } else if (b == BOSS_MYCEL && e->act == 1) { /* the spore mortar: where the players stand (they were marked during the tell) */
    for (int k = 0, n = 0; k < MAXP && n < 6; k++)
      if (s->p[k].active && !s->p[k].down)
        for (int j = 0; j < (frac < 0.6f ? 2 : 1) + 1 && n < 6; j++, n++) add_zone(s, 0, s->p[k].x + (rf(s) - 0.5f) * 3, s->p[k].y + (rf(s) - 0.5f) * 3, 1.6f, 150);
  } else if (b == BOSS_MYCEL && e->act == 2) { /* the root eruption: a line of roots toward the farthest player */
    int far = -1;
    float fd = -1;
    for (int k = 0; k < MAXP; k++) {
      float dx = s->p[k].x - e->x, dy = s->p[k].y - e->y;
      if (s->p[k].active && !s->p[k].down && dx * dx + dy * dy > fd) fd = dx * dx + dy * dy, far = k;
    }
    if (far >= 0) {
      float l = sqrtf(fd), dx = (s->p[far].x - e->x) / l, dy = (s->p[far].y - e->y) / l;
      for (int k = 0; k < 8; k++) add_zone(s, 1, e->x + dx * (3.0f + (float)k * 2.0f), e->y + dy * (3.0f + (float)k * 2.0f), 1.1f, 70);
    }
  }
  (void)tp, (void)fx, (void)fy;
}
/* a floor boss's turn: count down to its next arena attack or summon, tell it, release it, recover */
static void boss_step(sim *s, enemy *e) {
  int b = boss_of(s);
  if (b == BOSS_YARD) return;
  float frac = boss_frac(s, e);
  if (e->act) {
    e->act_t++;
    const int tell = BACT[b].tell[e->act];
    if (e->tgt >= 0 && e->act_t < tell - 6) { /* it turns to its target through the tell, then commits */
      const player *p = &s->p[e->tgt];
      float dx = p->x - e->x, dy = p->y - e->y;
      if (dx * dx + dy * dy > 1e-4f) e->face = cw_atan2(dy, dx);
    }
    if (e->act_t == tell) boss_release(s, e, b);
    if (e->act_t >= tell + BACT[b].release + BACT[b].recover) e->act = 0, e->act_t = 0;
    return;
  }
  if (e->atk > 0) return; /* in melee */
  if (--e->act_next > 0) return;
  /* what next: the summon when its phase has come and it is due, else the arena attack */
  bool summon = b == BOSS_VHESSA || b == BOSS_ANVILGRAVE || b == BOSS_SERAPHEL ? frac < 0.5f && (e->act_n % 2 == 1) : frac < 0.6f && (e->act_n % 3 == 2);
  e->act = summon ? 2 : 1;
  e->act_t = 0;
  e->act_n++;
  int best = -1;
  float bd = 1e9f;
  for (int k = 0; k < MAXP; k++) {
    const player *p = &s->p[k];
    float dx = p->x - e->x, dy = p->y - e->y;
    if (p->active && !p->down && dx * dx + dy * dy < bd) bd = dx * dx + dy * dy, best = k;
  }
  e->tgt = best;
  e->act_next = b == BOSS_VHESSA || b == BOSS_ANVILGRAVE ? (frac < 0.2f ? 180 : 360) : (frac < 0.6f ? 300 : 360);
  ev(s, EV_BOSS_TELL, e->x, e->y, 0, 0, e->act, b);
}
bool sim_boss_pose(const sim *s, const enemy *e, int *clip, int *frame, float *tell) {
  int b = boss_of(s);
  if (b == BOSS_YARD || !e->act) return false;
  int t = BACT[b].tell[e->act];
  *clip = e->act == 1 ? 2 : 3;
  *frame = e->act_t < t ? 0 : e->act_t < t + BACT[b].release ? 1 : 2;
  if (*clip == 3 && *frame > 1) *frame = 1; /* the summon has two frames: gather, release */
  if (tell) *tell = e->act_t < t ? (float)e->act_t / (float)t : 0;
  return true;
}

/* acid burns, lances fly, bones stand up */
static void boss_world_step(sim *s) {
  for (int i = 0; i < MAXZ; i++) {
    zone *z = &s->z[i];
    if (!z->alive) continue;
    if (--z->life <= 0) { z->alive = false; continue; }
    if (s->tick % 20 || z->kind == 2) continue; /* spores slow (the movement reads them), they do not burn */
    if (z->kind == 3) { /* the mortar's molten puddle burns the horde */
      for (int k = 0; k < MAXE; k++) {
        enemy *e = &s->e[k];
        if (!e->alive || (e->x - z->x) * (e->x - z->x) + (e->y - z->y) * (e->y - z->y) > z->r * z->r) continue;
        e->hp -= 3, e->flash = 2;
        if (e->hp <= 0) kill_enemy(s, k, 0, 0, 0);
      }
      continue;
    }
    for (int k = 0; k < MAXP; k++) {
      player *p = &s->p[k];
      float dx = p->x - z->x, dy = p->y - z->y;
      if (!p->active || p->down || p->inv > 0 || p->shield_t > 0 || dx * dx + dy * dy > z->r * z->r) continue;
      p->hp -= 4, p->inv = 6;
      ev(s, EV_HURT, p->x, p->y, 0, 0, k, 255);
      if (p->hp <= 0) p->down = true, p->down_ticks = 0, ev(s, EV_DOWN, p->x, p->y, 0, 0, k, 0);
    }
  }
  for (int i = 0; i < MAXES; i++) {
    eshot *e = &s->es[i];
    if (!e->alive) continue;
    e->x += e->vx / TICK, e->y += e->vy / TICK;
    if (--e->life <= 0 || fabsf(e->x) > HALF || fabsf(e->y) > HALF) { e->alive = false; continue; }
    for (int k = 0; k < MAXO; k++)
      if (s->o[k].alive && in_box(&s->o[k], e->x, e->y, 0.05f)) { e->alive = false; break; }
    if (!e->alive) continue;
    for (int k = 0; k < MAXP; k++) {
      player *p = &s->p[k];
      float dx = p->x - e->x, dy = p->y - e->y;
      if (!p->active || p->down || dx * dx + dy * dy > 0.7f * 0.7f) continue;
      e->alive = false;
      if (p->inv > 0 || p->shield_t > 0) break;
      p->hp -= e->dmg, p->inv = 12;
      ev(s, EV_HURT, p->x, p->y, e->vx, e->vy, k, EK_BOSS);
      if (p->hp <= 0) p->down = true, p->down_ticks = 0, ev(s, EV_DOWN, p->x, p->y, 0, 0, k, 0);
      break;
    }
  }
  if (s->tick % 20 == 0) /* a floor's molten strips burn whoever stands in them */
    for (int k = 0; k < MAXP; k++) {
      player *p = &s->p[k];
      if (!p->active || p->down || p->inv > 0 || p->shield_t > 0 || !in_hazard(s, HZ_MOLTEN, p->x, p->y)) continue;
      p->hp -= 3, p->inv = 6;
      ev(s, EV_HURT, p->x, p->y, 0, 0, k, 255);
      if (p->hp <= 0) p->down = true, p->down_ticks = 0, ev(s, EV_DOWN, p->x, p->y, 0, 0, k, 0);
    }
  for (int i = 0; i < MAXPILE; i++) {
    pile *pl = &s->pl[i];
    if (!pl->alive || pl->rise <= 0 || --pl->rise > 0) continue;
    pl->alive = false;
    int k = spawn_enemy(s, EK_BONEWALKER, pl->x, pl->y);
    if (k >= 0) s->e[k].act_n = 1, s->e[k].spawn_age = 40, ev(s, EV_RISE, pl->x, pl->y, 0, 0, 0, 0); /* risen once: it stays down next time */
  }
}

int sim_melee_windup(int kind) { return MEL[CW_CLAMP(kind, 0, EK_KINDS - 1)].windup; }

int sim_attack_frame(const enemy *e, float *tell) {
  if (tell) *tell = 0;
  if (!e->alive || e->atk <= 0) return -1;
  const melee *m = &MEL[e->kind];
  if (e->atk <= m->windup) {
    if (tell) *tell = (float)e->atk / (float)m->windup;
    return 0;
  }
  return e->atk <= m->windup + STRIKE_HOLD ? 1 : 2;
}

/* the difficulty a wave plays at: each floor starts two and a half waves above the one before it (its first wave a
 * breather, LEVELS.md), so the eighth floor's last wave plays about like the old run's twentieth */
static int dwave(const sim *s) { return 1 + (5 * s->level) / 2 + s->level_wave - 1; }

/* loot (LEVELS.md section 2): a boss bursts into gems and 3 to 5 caches, each a weapon of the floor's pool at a
 * rarity, the odds a step better every two floors */
const float RARITY_RGB[RARE_KINDS][3] = { { 0.85f, 0.85f, 0.85f }, { 0.36f, 0.94f, 0.36f }, { 0.3f, 0.55f, 1.0f }, { 0.71f, 0.36f, 1.0f }, { 1.0f, 0.6f, 0.15f } };
static const char *const RARITY_NAME[RARE_KINDS] = { "COMMON", "UNCOMMON", "RARE", "EPIC", "LEGENDARY" };
const char *sim_rarity_name(int r) { return RARITY_NAME[CW_CLAMP(r, 0, RARE_KINDS - 1)]; }
static const int RARITY_TICKS[RARE_KINDS] = { 60 * 12, 60 * 15, 60 * 18, 60 * 24, 60 * 30 };
static const float RARITY_DMG[RARE_KINDS] = { 1.0f, 1.15f, 1.3f, 1.5f, 1.8f };
/* the weapons each floor has unlocked: the yard's shotgun, minigun and rockets, the rail gun from the sluice, the
 * flamer from the ossuary (a gun a floor; the floors' own new guns come with their content) */
static int weapon_pool(const sim *s, int *out) {
  int n = 0;
  out[n++] = PU_SHOTGUN, out[n++] = PU_MINIGUN, out[n++] = PU_ROCKETS;
  if (s->level >= 1) out[n++] = PU_RAIL, out[n++] = PU_ARC;
  if (s->level >= 2) out[n++] = PU_STAKE;
  if (s->level >= 3) out[n++] = PU_FLAME, out[n++] = PU_MORTAR;
  if (s->level >= 4) out[n++] = PU_SEEKER;
  if (s->level >= 5) out[n++] = PU_WHEEL;
  if (s->level >= 6) out[n++] = PU_FROST;
  if (s->level >= 7) out[n++] = PU_HELLFIRE;
  return n;
}
static int roll_rarity(sim *s) {
  static const float ODDS[RARE_KINDS] = { 0.30f, 0.30f, 0.22f, 0.13f, 0.05f };
  float r = rf(s), acc = 0;
  int k = RARE_KINDS - 1;
  for (int i = 0; i < RARE_KINDS; i++)
    if (r < (acc += ODDS[i])) { k = i; break; }
  k = CW_MIN(k + s->level / 2 * (rf(s) < 0.5f ? 1 : 0), RARE_KINDS - 1); /* a step up every two floors, about half the time */
  if (s->level == NLEVELS - 1 && rf(s) < 0.4f) k = CW_MAX(k, RARE_EPIC); /* the core's caches: epic or better 40% of the time */
  return k;
}
static void drop_value(sim *s, int kind, float x, float y, int value);
static void loot_shower(sim *s, float x, float y) {
  int pool[8], n = weapon_pool(s, pool), caches = CW_MIN(3 + (s->players > 2) + (s->level >= 4), 5);
  for (int k = 0; k < caches; k++) {
    float a = (float)k * 6.2832f / (float)caches + rf(s) * 0.5f, d = 2.5f + rf(s) * 1.5f;
    int w = pool[(int)(cw_rng_u32(&s->rng) % (uint32_t)n)], r = roll_rarity(s);
    float cx = cw_clampf(x + cw_cos(a) * d, -HALF + 1.5f, HALF - 1.5f), cy = cw_clampf(y + cw_sin(a) * d, -HALF + 1.5f, HALF - 1.5f);
    drop_value(s, PK_CACHE, cx, cy, w | r << 8);
    ev(s, EV_CACHE, cx, cy, 0, 0, r, w);
  }
}

static int spawn_enemy(sim *s, int kind, float x, float y) {
  int slot = -1;
  for (int i = 0; i < MAXE; i++)
    if (!s->e[i].alive) { slot = i; break; }
  if (slot < 0) return -1;
  cw_crowd_agent_params ap = cw_crowd_agent_params_default();
  ap.radius = EK[kind].radius;
  ap.height = 1.6f;
  float sp = EK[kind].speed_lo + (EK[kind].speed_hi - EK[kind].speed_lo) * rf(s);
  ap.max_speed = sp;
  ap.max_accel = kind == EK_RUNNER ? 16 : 9;
  ap.neighbor_dist = 2.5f;
  ap.max_neighbors = kind == EK_RUNNER ? 5 : 8;
  ap.time_horizon = 0.7f;
  ap.avoidance_priority = kind == EK_RUNNER ? 0.4f : kind == EK_BOSS ? 1.0f : 0.7f;
  cw_crowd_agent a = cw_crowd_add(s->crowd, (t3_vec3){ x, 0, y }, &ap);
  if (CW_HANDLE_IS_NULL(a)) return -1;
  enemy *e = &s->e[slot];
  *e = (enemy){ .alive = true, .kind = (uint8_t)kind, .x = x, .y = y, .agent = a, .speed = sp };
  {
    float w = (float)dwave(s), f = 1.0f + 0.07f * w + 0.005f * w * w; /* the horde toughens with the wave (x1.08 at 1, x2.2 at 10, x4.4 at 20) slower than the guns grow: the pressure is numbers */
    e->hp = kind == EK_BOSS ? EK[kind].hp_base + (int)(EK[kind].hp_wave * w) : CW_MAX(1, (int)((float)EK[kind].hp_base * f + 0.5f));
  }
  /* more players, tougher enemies: 20% per extra player (more of them too), a boss 60% */
  e->hp = (int)((float)e->hp * (1.0f + (kind == EK_BOSS ? 0.6f : 0.2f) * (float)(s->players > 1 ? s->players - 1 : 0)) + 0.5f);
  if (kind == EK_BOSS) s->boss_maxhp = e->hp;
  e->face = rf(s) * 6.28f;
  e->atk = 0, e->atk_cd = 0, e->tgt = -1;
  s->nenemies++;
  if (kind == EK_BOSS) s->boss_alive++;
  return slot;
}

static void queue_boom(sim *s, float x, float y, float r, float dmg, int delay, int owner, bool big) {
  for (int i = 0; i < MAXX; i++)
    if (s->x[i].r == 0) {
      s->x[i] = (boom){ x, y, r, dmg, delay, (uint8_t)owner, big };
      return;
    }
}

/* a pickup at (x, y). A gem carries its xp; one dropped within GEM_MERGE of another gem joins it
 * instead (the ground holds a few rich gems, not a carpet of poor ones) */
#define GEM_MERGE 2.2f
#define GEM_MAX 60
static void drop_value(sim *s, int kind, float x, float y, int value) {
  if (kind == 0)
    for (int i = 0; i < MAXG; i++) {
      pickup *g = &s->g[i];
      if (!g->alive || g->kind != 0 || g->value + value > GEM_MAX) continue;
      float dx = g->x - x, dy = g->y - y;
      if (dx * dx + dy * dy < GEM_MERGE * GEM_MERGE) {
        g->value += value;
        g->life = CW_MAX(g->life, 60 * 30);
        return;
      }
    }
  for (int i = 0; i < MAXG; i++)
    if (!s->g[i].alive) {
      float a = rf(s) * 6.28f, v = 1.5f + rf(s) * 2.5f;
      s->g[i] = (pickup){ true, (uint8_t)kind, x, y, kind == PK_POWER ? 0 : cw_cos(a) * v, kind == PK_POWER ? 0 : cw_sin(a) * v, 60 * (kind == PK_POWER ? 30 : 40), value, 0 };
      return;
    }
}
static void drop(sim *s, int kind, float x, float y) { drop_value(s, kind, x, y, 1); }

/* each upgrade stops at a top level, so power grows on a curve a design can keep up with */
static const int UP_MAX[9] = { 7, 4, 8, 4, 4, 5, 5, 5, 6 };
static int *up_slot(player *p, int u) {
  int *f[9] = { &p->rate, &p->shots, &p->dmg, &p->pierce, &p->rocket, &p->speed, &p->magnet, &p->radius, &p->armor };
  return f[u % 9];
}

static void apply_upgrade(sim *s, player *p, int pi, int u) {
  p->last_up = u;
  int *lv = up_slot(p, u);
  if (*lv < UP_MAX[u % 9]) ++*lv;
  p->hp = CW_MIN(p->hp + 25, 100.0f);
  ev(s, EV_LEVEL, p->x, p->y, 0, 0, pi, u);
}

/* three different upgrades to choose from, none of them at its top level. The ladder's next one is
 * offered first, so a player who ignores the offer still grows; once nearly everything is maxed the
 * offer repeats what is left (a heal and a ring of sparks at the end of the road) */
static void roll_offer(sim *s, player *p) {
  int avail[9], n = 0;
  for (int u = 0; u < 9; u++)
    if (*up_slot(p, u) < UP_MAX[u]) avail[n++] = u;
  if (n == 0) avail[n++] = 8;
  int first = LADDER[(p->level - 2) % (int)sizeof LADDER];
  p->offer[0] = *up_slot(p, first) < UP_MAX[first] ? first : avail[(int)(cw_rng_u32(&s->rng) % (uint32_t)n)];
  for (int k = 1; k < 3; k++) {
    int u = p->offer[0], tries = 0;
    do {
      u = avail[(int)(cw_rng_u32(&s->rng) % (uint32_t)n)];
    } while (tries++ < 20 && (u == p->offer[0] || (k == 2 && u == p->offer[1])));
    p->offer[k] = u;
  }
  p->offer_ticks = 60 * 8; /* the first offer is taken when the time runs out */
}

static void level_up(sim *s, player *p, int pi) {
  (void)pi;
  p->level++;
  p->xp -= p->xp_next;
  p->xp_next = 10 + p->level * 8 + p->level * p->level * 4; /* a choice every 25 to 60 s early, still about a minute late */
  if (p->offer_ticks > 0) p->pending++;
  else roll_offer(s, p);
}

static void kill_enemy(sim *s, int i, int owner, float dx, float dy) {
  enemy *e = &s->e[i];
  if (!e->alive) return;
  e->alive = false;
  cw_crowd_remove(s->crowd, e->agent);
  s->nenemies--;
  s->kills++;
  s->killed_total++;
  if (owner >= 0 && owner < MAXP && e->hits > 0) {
    int w = CW_CLAMP(s->wave, 0, FINAL_WAVE);
    s->ttk_n[w][e->kind]++, s->ttk_hits[w][e->kind] += e->hits, s->ttk_ticks[w][e->kind] += s->tick - e->first_hit;
  }
  if (owner >= 0 && owner < MAXP) {
    const player *kp = &s->p[owner];
    int held = kp->pu_weapon_t > 0 ? kp->pu_weapon : kp->pu_ammo_t > 0 ? kp->pu_ammo : kp->saw_t > 0 ? PU_SAWS : -1;
    if (held >= 0) s->pu_kills[held]++;
    else s->plain_kills++;
    s->p[owner].kills++;
    s->p[owner].score += 10 * (1 + e->kind * 4);
  }
  ev(s, EV_KILL, e->x, e->y, dx, dy, e->kind, owner);
  if (e->kind == EK_BOSS) s->boss_alive--;
  if (e->kind == EK_SPORELING) add_zone(s, 2, e->x, e->y, 1.8f, 180); /* a spore cloud: slows for 3 s */
  if (e->frozen > 0 && owner >= 0) /* a frozen one shatters into its neighbours */
    for (int k = 0; k < MAXE; k++) {
      enemy *n = &s->e[k];
      if (!n->alive || n == e || (n->x - e->x) * (n->x - e->x) + (n->y - e->y) * (n->y - e->y) > 1.6f * 1.6f) continue;
      n->hp -= 6, n->flash = 4;
      if (n->hp <= 0) kill_enemy(s, k, owner, 0, 0);
    }
  if (e->kind == EK_BONEWALKER && s->state == ST_PLAY) /* it falls apart: a pile of bones, one in five to stand up once in 4 s */
    for (int k = 0; k < MAXPILE; k++)
      if (!s->pl[k].alive) {
        s->pl[k] = (pile){ true, e->x, e->y, e->act_n == 0 && rf(s) < 0.2f ? 240 : 0 };
        break;
      }
  if (owner < 0) return; /* the yard clearing itself (a reset, the win) drops nothing */
  /* the kind's xp in gems. A runner leaves its gem less often as the waves thicken: a thick horde
   * is many more kills a minute, so the xp a minute still grows on the curve the level costs follow */
  int xp = EK[e->kind].xp;
  if (e->kind == EK_RUNNER) {
    if (rf(s) < 6.0f / (6.0f + (float)dwave(s))) drop_value(s, 0, e->x, e->y, xp);
  } else {
    int gems = e->kind == EK_BOSS ? 6 : 1;
    for (int k = 0; k < gems; k++) drop_value(s, 0, e->x + (float)(k % 3) * 0.9f - 0.9f, e->y + (float)(k / 3) * 0.9f, xp / gems);
  }
  float r = rf(s);
  if (e->kind != EK_RUNNER ? r < 0.35f : r < 0.012f) drop(s, 1, e->x, e->y);
  if (e->kind == EK_BRUTE && rf(s) < 0.03f && !s->no_power) drop_value(s, PK_POWER, e->x, e->y, (int)(cw_rng_u32(&s->rng) % PU_BASE_KINDS)); /* an elite now and then */
  if (e->kind == EK_BOSS && !s->no_power) loot_shower(s, e->x, e->y); /* the boss's loot: caches by rarity */
  if (e->kind == EK_BLOATER) queue_boom(s, e->x, e->y, 3.4f, 14, 4, owner, false);
  if (e->kind == EK_BOSS) {
    for (int k = 0; k < 14; k++) {
      float a = rf(s) * 6.28f, d = rf(s) * 2.2f;
      queue_boom(s, e->x + cw_cos(a) * d, e->y + cw_sin(a) * d, 4.2f, 40, 6 + k * 7, owner, true);
    }
  }
}

/* ── obstacles ──────────────────────────────────────────────────────── */

static void add_obstacle(sim *s, int kind, float x, float y, float hx, float hy) {
  for (int i = 0; i < MAXO; i++)
    if (!s->o[i].alive) {
      obstacle *o = &s->o[i];
      o->alive = true;
      o->kind = (uint8_t)kind;
      o->x = x, o->y = y, o->hx = hx, o->hy = hy;
      o->hp = o->maxhp = kind == OK_CRATE ? 14 : kind == OK_BARREL ? 6 : kind == OK_WALL ? 90 : 1 << 20;
      o->nav = cw_nav_obstacle_add_box(s->nav, (t3_vec3){ x, 0, y }, (t3_vec3){ hx, 1.5f, hy }, 0);
      s->nav_dirty = true;
      return;
    }
}

static bool in_zone(const sim *s, int kind, float x, float y) {
  for (int i = 0; i < MAXZ; i++) {
    const zone *z = &s->z[i];
    if (z->alive && z->kind == kind && (x - z->x) * (x - z->x) + (y - z->y) * (y - z->y) < z->r * z->r) return true;
  }
  return false;
}
static bool in_hazard(const sim *s, int kind, float x, float y) {
  const floor_def *fd = &s->fl[s->level];
  if (!fd->set) return false;
  for (int i = 0; i < fd->nhaz; i++) {
    const floor_hazard *h = &fd->haz[i];
    if (h->kind == kind && fabsf(x - h->x) < h->hx && fabsf(y - h->y) < h->hy) return true;
  }
  return false;
}

static void remove_obstacle(sim *s, int i);
static void layout(sim *s);
static void build_flow(sim *s);
/* the floor's obstacles and power-up spots: its .cwlevel's when the game set one, else the yard's (built in code) */
static void floor_layout(sim *s) {
  for (int i = 0; i < MAXO; i++)
    if (s->o[i].alive) remove_obstacle(s, i);
  const floor_def *fd = &s->fl[s->level];
  if (!fd->set) {
    layout(s);
    return;
  }
  for (int i = 0; i < fd->nob; i++) add_obstacle(s, fd->ob[i].kind, fd->ob[i].x, fd->ob[i].y, fd->ob[i].hx, fd->ob[i].hy);
  s->pu_spots = 0;
  for (int i = 0; i < fd->npick && i < 16; i++) s->spot_x[i] = fd->pick[i][0], s->spot_y[i] = fd->pick[i][1], s->pu_spots++;
}
void sim_floor_apply(sim *s) { /* after the game set the floors' layouts: the current floor's (the yard keeps the one sim_init made) */
  if (!s->fl[s->level].set) return;
  floor_layout(s);
  cw_nav_update(s->nav);
  s->nav_dirty = false;
  build_flow(s);
}

static void remove_obstacle(sim *s, int i) {
  obstacle *o = &s->o[i];
  o->alive = false;
  if (o->nav) cw_nav_obstacle_remove(s->nav, o->nav); /* a full change queue is applied by the engine */
  o->nav = 0;
  s->nav_dirty = true;
  s->flow_age = 1000;
}

static void hurt_obstacle(sim *s, int i, int dmg) {
  obstacle *o = &s->o[i];
  if (!o->alive || o->kind == OK_PILLAR) return;
  o->hp -= dmg;
  if (o->hp > 0) return;
  switch (o->kind) {
  case OK_CRATE:
    ev(s, EV_CRATE, o->x, o->y, 0, 0, 0, 0);
    if (rf(s) < 0.5f) drop(s, 0, o->x, o->y);
    if (rf(s) < 0.15f) drop(s, 1, o->x, o->y);
    break;
  case OK_BARREL:
    queue_boom(s, o->x, o->y, 4.2f, 30, 3, MAXP, true);
    ev(s, EV_BARREL, o->x, o->y, 0, 0, 0, 0);
    break;
  default: ev(s, EV_WALL, o->x, o->y, o->hx, o->hy, 0, 0); break;
  }
  remove_obstacle(s, i);
}

static bool in_box(const obstacle *o, float x, float y, float r) {
  return x > o->x - o->hx - r && x < o->x + o->hx + r && y > o->y - o->hy - r && y < o->y + o->hy + r;
}

static void build_flow_blocked(sim *s) {
  memset(s->blocked, 0, sizeof s->blocked);
  for (int i = 0; i < MAXO; i++) {
    const obstacle *o = &s->o[i];
    if (!o->alive) continue;
    int x0 = (int)(o->x - o->hx + HALF - 0.3f), x1 = (int)(o->x + o->hx + HALF + 0.3f);
    int y0 = (int)(o->y - o->hy + HALF - 0.3f), y1 = (int)(o->y + o->hy + HALF + 0.3f);
    for (int y = CW_MAX(y0, 0); y <= CW_MIN(y1, FW - 1); y++)
      for (int x = CW_MAX(x0, 0); x <= CW_MIN(x1, FW - 1); x++) s->blocked[y * FW + x] = 1;
  }
}

/* breadth first distances from every living player's cell: one field, one
 * lookup per enemy */
static void build_flow(sim *s) {
  static int16_t queue[FW * FW];
  build_flow_blocked(s);
  for (int i = 0; i < FW * FW; i++) s->flow[i] = 0xffff;
  int qh = 0, qt = 0;
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (!p->active || p->down) continue;
    int cx = CW_CLAMP((int)(p->x + HALF), 0, FW - 1), cy = CW_CLAMP((int)(p->y + HALF), 0, FW - 1);
    if (s->flow[cy * FW + cx] == 0) continue;
    s->flow[cy * FW + cx] = 0;
    queue[qt++] = (int16_t)(cy * FW + cx);
  }
  static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
  while (qh < qt) {
    int c = queue[qh++], cx = c % FW, cy = c / FW;
    uint16_t d = s->flow[c];
    for (int k = 0; k < 8; k++) {
      int nx = cx + DX[k], ny = cy + DY[k];
      if (nx < 0 || ny < 0 || nx >= FW || ny >= FW) continue;
      int n = ny * FW + nx;
      if (s->blocked[n] || s->flow[n] != 0xffff) continue;
      if (k >= 4 && (s->blocked[cy * FW + nx] || s->blocked[ny * FW + cx])) continue; /* no cutting corners */
      s->flow[n] = (uint16_t)(d + (k < 4 ? 2 : 3));
      queue[qt++] = (int16_t)n;
    }
  }
  s->flow_age = 0;
}

/* ── setup ──────────────────────────────────────────────────────────── */

void sim_free(sim *s) {
  if (!CW_HANDLE_IS_NULL(s->nav)) cw_navmesh_free(s->nav);
  s->nav = (cw_navmesh){ 0 };
  s->crowd = (cw_crowd){ 0 };
}

static void layout(sim *s) {
  /* a ring of cinder walls with gaps, pillars, scattered crates and barrels */
  for (int k = 0; k < 4; k++) {
    float a0 = (float)k * 1.5708f + 0.2f;
    for (int j = 0; j < 7; j++) {
      float a = a0 + (float)j * 0.17f;
      float r = 17;
      add_obstacle(s, OK_WALL, cw_cos(a) * r, cw_sin(a) * r, 1.0f, 1.0f);
    }
  }
  for (int k = 0; k < 8; k++) {
    float a = (float)k * 0.7854f + 0.4f;
    add_obstacle(s, OK_PILLAR, cw_cos(a) * 28, cw_sin(a) * 28, 1.2f, 1.2f);
  }
  for (int k = 0; k < 90; k++) {
    float x = (rf(s) - 0.5f) * 2 * (HALF - 3), y = (rf(s) - 0.5f) * 2 * (HALF - 3);
    if (x * x + y * y < 36) continue;
    bool clash = false;
    for (int i = 0; i < MAXO; i++)
      if (s->o[i].alive && in_box(&s->o[i], x, y, 1.4f)) clash = true;
    if (clash) continue;
    if (rf(s) < 0.42f) add_obstacle(s, OK_BARREL, x, y, 0.5f, 0.5f);
    else add_obstacle(s, OK_CRATE, x, y, 0.65f, 0.65f);
  }
}

void sim_init(sim *s, int players, uint32_t seed, int start_wave) {
  sim_free(s);
  memset(s, 0, sizeof *s);
  cw_rng_seed(&s->rng, seed);
  s->players = players;
  cw_nav_geom g = { 0 };
  cw_nav_geom_add_box(&g, (t3_vec3){ 0, -0.25f, 0 }, (t3_vec3){ HALF, 0.25f, HALF }, CW_NAV_AREA_WALK);
  cw_nav_config c = cw_nav_config_default();
  c.dynamic = true;
  c.max_obstacles = 300;
  c.cell_size = 0.25f;
  c.cell_height = 0.2f;
  c.agent_radius = 0.4f;
  c.agent_height = 1.6f;
  c.tile_size = 64;
  s->nav = cw_navmesh_build(&c, &g);
  cw_nav_geom_free(&g);
  s->crowd = cw_crowd_new(s->nav, MAXE);
  cw_crowd_set_plan_budget(s->crowd, 8, 0);
  layout(s);
  cw_nav_update(s->nav);
  { /* power-up spots: four corners, the four gaps in the cinder ring, the lane ends, behind four crates */
    float c = HALF - 4.0f;
    float sp[][2] = { { -c, -c }, { c, -c }, { -c, c }, { c, c }, { 0, -21 }, { 21, 0 }, { 0, 21 }, { -21, 0 }, { -c, 10 }, { c, 10 } };
    int n = 0;
    for (int k = 0; k < (int)(sizeof sp / sizeof sp[0]); k++) s->spot_x[n] = sp[k][0], s->spot_y[n] = sp[k][1], n++;
    for (int i = 0; i < MAXO && n < 14; i++)
      if (s->o[i].alive && s->o[i].kind == OK_CRATE && (s->o[i].x * s->o[i].x + s->o[i].y * s->o[i].y) > 150) {
        float l = cw_v2_length(cw_v2(s->o[i].x, s->o[i].y)); /* on the far side of the crate from the middle */
        s->spot_x[n] = s->o[i].x + s->o[i].x / l * 1.6f, s->spot_y[n] = s->o[i].y + s->o[i].y / l * 1.6f, n++;
        i += 11;
      }
    s->pu_spots = n;
  }
  for (int i = 0; i < players; i++) {
    player *p = &s->p[i];
    float a = (float)i * 6.2832f / (float)players;
    *p = (player){ .active = true, .hp = 100, .x = cw_cos(a) * 2.0f, .y = cw_sin(a) * 2.0f, .xp_next = 14, .level = 1, .aim = a };
  }
  s->wave = CW_CLAMP(start_wave > 0 ? start_wave : 1, 1, FINAL_WAVE);
  while (s->level < NLEVELS - 1 && s->wave >= sim_level_first_wave(s->level + 1)) s->level++;
  s->level_wave = s->wave - sim_level_first_wave(s->level) + 1;
  for (int i = 0; i < players; i++) { /* a run started at a later wave begins with about the levels a played run has there */
    player *p = &s->p[i];
    for (int k = 1; k < dwave(s); k++) {
      p->level++;
      p->xp_next = 10 + p->level * 8 + p->level * p->level * 4;
      int u = LADDER[(p->level - 2) % (int)sizeof LADDER];
      int *lv = up_slot(p, u);
      if (*lv < UP_MAX[u]) ++*lv;
    }
  }
  s->spawn_cap = 40 + dwave(s) * 90;
  build_flow(s);
}

/* ── per tick ───────────────────────────────────────────────────────── */

static void build_grid(sim *s) {
  static int16_t cnt[41 * 41 + 1];
  memset(cnt, 0, sizeof cnt);
  for (int i = 0; i < MAXE; i++) {
    if (!s->e[i].alive) continue;
    int cx = CW_CLAMP((int)((s->e[i].x + HALF) * 0.5f), 0, 40), cy = CW_CLAMP((int)((s->e[i].y + HALF) * 0.5f), 0, 40);
    cnt[cy * 41 + cx + 1]++;
  }
  for (int i = 1; i <= 41 * 41; i++) cnt[i] = (int16_t)(cnt[i] + cnt[i - 1]);
  memcpy(s->cell_start, cnt, sizeof cnt);
  static int16_t fill[41 * 41];
  memcpy(fill, cnt, sizeof fill);
  for (int i = 0; i < MAXE; i++) {
    if (!s->e[i].alive) continue;
    int cx = CW_CLAMP((int)((s->e[i].x + HALF) * 0.5f), 0, 40), cy = CW_CLAMP((int)((s->e[i].y + HALF) * 0.5f), 0, 40);
    s->cell_idx[fill[cy * 41 + cx]++] = (int16_t)i;
  }
}

/* enemies within r of (x, y); fn(i) per enemy */
typedef void (*enemy_fn)(sim *s, int i, void *u);
static void each_enemy(sim *s, float x, float y, float r, enemy_fn fn, void *u) {
  int x0 = CW_CLAMP((int)((x - r + HALF) * 0.5f), 0, 40), x1 = CW_CLAMP((int)((x + r + HALF) * 0.5f), 0, 40);
  int y0 = CW_CLAMP((int)((y - r + HALF) * 0.5f), 0, 40), y1 = CW_CLAMP((int)((y + r + HALF) * 0.5f), 0, 40);
  for (int cy = y0; cy <= y1; cy++)
    for (int cx = x0; cx <= x1; cx++)
      for (int k = s->cell_start[cy * 41 + cx]; k < s->cell_start[cy * 41 + cx + 1]; k++) {
        int i = s->cell_idx[k];
        enemy *e = &s->e[i];
        if (!e->alive) continue;
        float dx = e->x - x, dy = e->y - y, rr = r + EK[e->kind].radius;
        if (dx * dx + dy * dy <= rr * rr) fn(s, i, u);
      }
}

typedef struct bullet_ctx { int b; bool stop; } bullet_ctx;
static void bullet_hit(sim *s, int i, void *u) {
  bullet_ctx *c = u;
  bullet *b = &s->b[c->b];
  if (c->stop || !b->alive) return;
  enemy *e = &s->e[i];
  if (!e->alive) return;
  float d = b->dmg;
  if (!e->hits++) e->first_hit = s->tick;
  e->hp -= (int)(d + 0.5f);
  e->flash = 4;
  float k = 0.9f / (1.0f + (float)(e->kind) * 1.5f);
  e->kx += b->vx * 0.012f * k;
  e->ky += b->vy * 0.012f * k;
  ev(s, EV_HIT, b->x, b->y, b->vx, b->vy, e->kind, 0);
  if (e->hp <= 0) kill_enemy(s, i, b->owner, b->vx, b->vy);
  if (b->kind == 1 || b->kind == 2) { /* a rocket or a grenade bursts on the first thing it touches */
    if (b->kind == 2) queue_boom(s, b->x, b->y, 5.0f + (float)s->p[b->owner].radius * 0.5f, 30, 0, b->owner, true);
    else queue_boom(s, b->x, b->y, 2.4f + (float)s->p[b->owner].radius * 0.5f, 3.0f + b->dmg * 2.0f, 0, b->owner, false);
    b->alive = false;
    c->stop = true;
    return;
  }
  if (b->fx == 1 && b->kind == 0) queue_boom(s, b->x, b->y, 1.6f, 2.0f + b->dmg * 0.5f, 0, b->owner, false); /* explosive rounds */
  if (b->kind == 8 || b->kind == 7) { /* a seeker or a mortar shell bursts on what it touches */
    queue_boom(s, b->x, b->y, b->kind == 7 ? 3.0f : 1.8f, b->kind == 7 ? 18 : 6 + b->dmg, 0, b->owner, b->kind == 7);
    if (b->kind == 7) add_zone(s, 3, b->x, b->y, 1.6f, 180);
    b->alive = false, c->stop = true;
    return;
  }
  if (b->kind == 10) { if (e->slow > 0) e->frozen = 90; e->slow = 120; } /* frost: slowed, and frozen when already slowed */
  if (b->kind == 11) e->burn = CW_MAX(e->burn, 120), e->burn_owner = b->owner; /* hellfire sets it alight */
  if (b->kind == 6 && b->pierce == 0) e->pin = 60; /* the stake's last target is nailed in place */
  if (b->kind == 9) { b->pierce = 99; return; } /* the wheel cuts through everything */
  if (b->pierce-- <= 0) {
    b->alive = false;
    c->stop = true;
  }
}

typedef struct boom_ctx { const boom *bm; } boom_ctx;
static void boom_hit(sim *s, int i, void *u) {
  const boom *bm = ((boom_ctx *)u)->bm;
  enemy *e = &s->e[i];
  float dx = e->x - bm->x, dy = e->y - bm->y, d = cw_clampf(1 - (dx * dx + dy * dy) / (bm->r * bm->r), 0.2f, 1);
  int dmg = (int)(bm->dmg * d);
  e->hp -= dmg;
  e->flash = 6;
  float l = cw_v2_length(cw_v2(dx, dy));
  float ux = l > 0.01f ? dx / l : 1, uy = l > 0.01f ? dy / l : 0;
  float push = (bm->big ? 0.5f : 0.35f) * d / (1.0f + (float)e->kind * 1.2f);
  e->kx += ux * push;
  e->ky += uy * push;
  if (e->hp <= 0) kill_enemy(s, i, bm->owner, ux * 20, uy * 20);
}

static void do_boom(sim *s, const boom *bm) {
  ev(s, EV_BOOM, bm->x, bm->y, bm->r, bm->big ? 1.0f : 0.0f, bm->big, bm->owner);
  boom_ctx c = { bm };
  each_enemy(s, bm->x, bm->y, bm->r, boom_hit, &c);
  for (int i = 0; i < MAXO; i++) {
    obstacle *o = &s->o[i];
    if (!o->alive || o->kind == OK_PILLAR) continue;
    float dx = o->x - bm->x, dy = o->y - bm->y, rr = bm->r + o->hx;
    if (dx * dx + dy * dy < rr * rr) hurt_obstacle(s, i, (int)bm->dmg);
  }
  for (int i = 0; i < MAXP; i++) {
    player *p = &s->p[i];
    if (!p->active || p->down || p->inv > 0) continue;
    float dx = p->x - bm->x, dy = p->y - bm->y;
    if (dx * dx + dy * dy < bm->r * bm->r * 0.6f && (bm->owner >= MAXP || bm->owner == i ? true : false)) {
      p->hp -= bm->owner == i ? 6.0f : 14.0f;
      p->inv = 30;
      ev(s, EV_HURT, p->x, p->y, 0, 0, i, 255); /* b 255: a blast, not a melee strike */
    }
  }
}

typedef struct saw_ctx { int owner; float x, y; } saw_ctx;
static void saw_hit(sim *s, int i, void *u) {
  const saw_ctx *c = u;
  enemy *e = &s->e[i];
  if (!e->hits++) e->first_hit = s->tick;
  e->hp -= 3 + s->p[c->owner].dmg;
  e->flash = 4;
  float dx = e->x - c->x, dy = e->y - c->y, l = cw_v2_length(cw_v2(dx, dy));
  if (l > 0.01f) e->kx += dx / l * 0.12f, e->ky += dy / l * 0.12f;
  ev(s, EV_HIT, c->x, c->y, dx, dy, e->kind, 0);
  if (e->hp <= 0) kill_enemy(s, i, c->owner, dx * 8, dy * 8);
}

static void player_step(sim *s, int i, const input *in) {
  player *p = &s->p[i];
  if (!p->active) return;
  if (p->down) {
    p->down_ticks++;
    return;
  }
  float sp = 6.0f + (float)p->speed * 0.6f;
  if (in_hazard(s, HZ_WATER, p->x, p->y) || in_hazard(s, HZ_SPORE, p->x, p->y) || in_zone(s, 2, p->x, p->y)) sp *= 0.7f; /* wading, spores */
  float mx = in->mx, my = in->my, ml = cw_v2_length(cw_v2(mx, my));
  if (ml > 1) mx /= ml, my /= ml;
  p->vx += (mx * sp - p->vx) * 0.35f;
  p->vy += (my * sp - p->vy) * 0.35f;
  p->x += p->vx / TICK;
  p->y += p->vy / TICK;
  p->x = cw_clampf(p->x, -HALF + 0.6f, HALF - 0.6f);
  p->y = cw_clampf(p->y, -HALF + 0.6f, HALF - 0.6f);
  for (int k = 0; k < MAXO; k++) { /* push out of boxes */
    const obstacle *o = &s->o[k];
    if (!o->alive) continue;
    float r = 0.42f;
    if (!in_box(o, p->x, p->y, r)) continue;
    float dx = p->x - o->x, dy = p->y - o->y;
    float px = o->hx + r - (dx < 0 ? -dx : dx), py = o->hy + r - (dy < 0 ? -dy : dy);
    if (px < py) p->x += dx < 0 ? -px : px;
    else p->y += dy < 0 ? -py : py;
  }
  for (int k = 0; k < MAXP; k++) { /* friends are solid, softly */
    const player *q = &s->p[k];
    if (k == i || !q->active || q->down) continue;
    float dx = p->x - q->x, dy = p->y - q->y, d2 = dx * dx + dy * dy;
    if (d2 < 0.8f * 0.8f) {
      float d = d2 > 0.0001f ? cw_v2_length(cw_v2(dx, dy)) : 0.01f, push = (0.8f - d) * 0.5f;
      p->x += (d2 > 0.0001f ? dx / d : (i > k ? 1.0f : -1.0f)) * push;
      p->y += (d2 > 0.0001f ? dy / d : 0.0f) * push;
    }
  }
  /* tether: the living team's box never grows past what the camera at its farthest shows */
  {
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    int n = 0;
    for (int k = 0; k < MAXP; k++) {
      const player *q = &s->p[k];
      if (k == i || !q->active || q->down) continue;
      x0 = CW_MIN(x0, q->x), x1 = CW_MAX(x1, q->x), y0 = CW_MIN(y0, q->y), y1 = CW_MAX(y1, q->y), n++;
    }
    if (n) {
      p->x = cw_clampf(p->x, x1 - 2 * TETHER_HW, x0 + 2 * TETHER_HW);
      p->y = cw_clampf(p->y, y1 - 2 * TETHER_HH, y0 + 2 * TETHER_HH);
    }
  }
  if (p->offer_ticks > 0) {
    int pick = in->pick >= 1 && in->pick <= 3 ? in->pick - 1 : --p->offer_ticks == 0 ? 0 : -1;
    if (pick >= 0) {
      p->offer_ticks = 0;
      apply_upgrade(s, p, i, p->offer[pick]);
      if (p->pending > 0) {
        p->pending--;
        roll_offer(s, p);
      }
    }
  }
  float al = cw_v2_length(cw_v2(in->ax, in->ay));
  if (al > 0.001f) p->aim = cw_atan2(in->ay, in->ax); /* the dead zone is applied where the stick is read */
  if (p->cd > 0) p->cd--;
  if (p->bomb_cd > 0) p->bomb_cd--;
  if (p->inv > 0) p->inv--;
  bool fire = in->fire || al > 0.3f;
  if (fire && p->cd == 0) {
    static const int PERIOD[8] = { 10, 8, 7, 6, 5, 4, 4, 3 }; /* ticks a shot: 6 a second, each rapid-fire level clearly faster */
    int period = PERIOD[CW_CLAMP(p->rate, 0, 7)];
    int w = p->pu_weapon_t > 0 ? p->pu_weapon : -1;
    if (w == PU_MINIGUN) period = CW_MAX(2, period / 2);
    else if (w == PU_SHOTGUN) period = period * 2;
    else if (w == PU_RAIL) period = period * 3;
    else if (w == PU_FLAME) period = 2;
    else if (w == PU_ARC || w == PU_FROST) period = period * 3;
    else if (w == PU_STAKE) period = period * 3 / 2;
    else if (w == PU_MORTAR || w == PU_SEEKER) period = period * 4;
    else if (w == PU_WHEEL) period = period * 2;
    else if (w == PU_HELLFIRE) period = 2;
    if (p->pu_ammo_t > 0 && p->pu_ammo == PU_RAPID) period = CW_MAX(2, period / 2);
    p->cd = period;
    float rar = w >= 0 ? RARITY_DMG[CW_CLAMP(p->pu_rarity, 0, RARE_KINDS - 1)] : 1.0f;
    bool legend = w >= 0 && p->pu_rarity == RARE_LEGENDARY;
    p->beam = w == PU_HELLFIRE ? p->beam + period : 0;
    if (w == PU_ARC) { /* lightning: the nearest in front, then jumps to up to 3 more within 5 m (on and on when legendary); one in the water passes it through the water */
      float x = p->x, y = p->y, dmg = (8.0f + 1.5f * (float)p->dmg) * rar;
      int hops = legend ? 24 : 4, hit[32], nh = 0;
      for (int h = 0; h < hops; h++) {
        int best = -1;
        float bd = h == 0 ? 14.0f * 14.0f : 5.0f * 5.0f;
        for (int k = 0; k < MAXE; k++) {
          const enemy *e = &s->e[k];
          if (!e->alive) continue;
          float dx = e->x - x, dy = e->y - y, d = dx * dx + dy * dy;
          if (d > bd) continue;
          if (h == 0 && (dx * cw_cos(p->aim) + dy * cw_sin(p->aim)) < 0.85f * sqrtf(d)) continue; /* the first in front */
          bool seen = false;
          for (int j = 0; j < nh; j++) seen |= hit[j] == k;
          if (!seen) bd = d, best = k;
        }
        if (best < 0) break;
        enemy *e = &s->e[best];
        hit[nh++] = best;
        ev(s, EV_ARC, x, y, e->x, e->y, i, 0);
        x = e->x, y = e->y;
        bool wet = in_hazard(s, HZ_WATER, x, y);
        e->hp -= (int)(dmg + 0.5f), e->flash = 4;
        if (e->hp <= 0) kill_enemy(s, best, i, 0, 0);
        if (wet) /* the water carries it to everything standing in it close by */
          for (int k = 0; k < MAXE; k++) {
            enemy *o = &s->e[k];
            if (!o->alive || k == best || (o->x - x) * (o->x - x) + (o->y - y) * (o->y - y) > 36 || !in_hazard(s, HZ_WATER, o->x, o->y)) continue;
            ev(s, EV_ARC, x, y, o->x, o->y, i, 1);
            o->hp -= (int)(dmg * 0.6f + 0.5f), o->flash = 4;
            if (o->hp <= 0) kill_enemy(s, k, i, 0, 0);
          }
      }
      ev(s, EV_SHOT, p->x, p->y, cw_cos(p->aim), cw_sin(p->aim), i, 0);
      goto fired;
    }
    if (w == PU_WHEEL) { /* the saint's wheel: one in the air at a time (two when legendary) */
      int out = 0;
      for (int j = 0; j < MAXB; j++) out += s->b[j].alive && s->b[j].kind == 9 && s->b[j].owner == i;
      if (out >= (legend ? 2 : 1)) goto fired;
    }
    int n = 1 + p->shots + (w >= 0 && p->pu_rarity >= RARE_RARE ? 1 : 0); /* a rare gun or better: one more */
    if (w == PU_SHOTGUN) n += 4;
    if (w == PU_FLAME) n = 3;
    if (w == PU_SEEKER) n = 8;                 /* the drum's eight */
    if (w == PU_MORTAR) n = legend ? 3 : 1;    /* a cluster when legendary */
    if (w == PU_WHEEL || w == PU_HELLFIRE || w == PU_FROST || w == PU_STAKE) n = CW_MIN(n, w == PU_STAKE ? 2 : 1);
    float spread = w == PU_SHOTGUN ? 0.36f : w == PU_FLAME ? 0.3f : w == PU_SEEKER ? 0.7f : w == PU_MORTAR ? 0.18f : w == PU_MINIGUN ? 0.07f * (float)(n - 1) + 0.06f : 0.07f * (float)(n - 1) + 0.02f;
    bool rocket = (p->rocket > 0 && ((s->tick / period) % 4 == 0)) || w == PU_ROCKETS;
    for (int k = 0; k < n; k++) {
      int slot = -1;
      for (int j = 0; j < MAXB; j++)
        if (!s->b[j].alive) { slot = j; break; }
      if (slot < 0) break;
      float off = n > 1 ? ((float)k / (float)(n - 1) - 0.5f) * 2 * spread : 0;
      float jit = (rf(s) - 0.5f) * 0.03f;
      bool rk = rocket && k == n / 2;
      float a = p->aim + off + jit + (w == PU_FLAME ? (rf(s) - 0.5f) * 0.25f : 0), v = rk ? 22.0f : w == PU_RAIL ? 70.0f : w == PU_FLAME ? 13.0f + rf(s) * 4 : 32.0f;
      static const uint8_t NK[PU_KINDS] = { [PU_STAKE] = 6, [PU_MORTAR] = 7, [PU_SEEKER] = 8, [PU_WHEEL] = 9, [PU_FROST] = 10, [PU_HELLFIRE] = 11 };
      static const float NV[PU_KINDS] = { [PU_STAKE] = 45, [PU_MORTAR] = 15, [PU_SEEKER] = 16, [PU_WHEEL] = 18, [PU_FROST] = 70, [PU_HELLFIRE] = 60 };
      uint8_t kind = rk ? 1 : w == PU_RAIL ? 3 : w == PU_FLAME ? 4 : w >= PU_ARC ? NK[w] : 0;
      if (kind >= 6) v = NV[w];
      float dmg = (2.0f + (float)p->dmg + (rk ? 3.0f : 0.0f)) * (w >= 0 ? RARITY_DMG[CW_CLAMP(p->pu_rarity, 0, RARE_KINDS - 1)] : 1.0f); /* 2 damage and through one body from the start; +1 a heavy-rounds level; a cache's gun by its rarity */
      int life = rk ? 80 : 50, pierce = p->pierce + 1 + (w >= 0 && p->pu_rarity >= RARE_EPIC ? 1 : 0);
      if (kind == 3) dmg = 5.0f + 1.5f * (float)p->dmg, pierce = 12, life = 40; /* the rail goes through a line of them */
      if (kind == 4) dmg = 1.4f + 0.5f * (float)p->dmg, pierce = 4, life = 22;   /* a short hot cone */
      if (kind == 6) dmg = (6.0f + (float)p->dmg) * rar, pierce = 2 + (p->pu_rarity >= RARE_EPIC), life = 50; /* the stake: through three, the last nailed */
      if (kind == 7) dmg = 18.0f * rar, pierce = 0, life = 36;                    /* the shell lands where the grenade does */
      if (kind == 8) dmg = (2.0f + (float)p->dmg) * rar, pierce = 0, life = 90;  /* a seeker */
      if (kind == 9) dmg = (7.0f + (float)p->dmg) * rar, pierce = 99, life = 150; /* the wheel */
      if (kind == 10) dmg = (6.0f + (float)p->dmg) * rar, pierce = 12, life = 40;
      if (kind == 11) dmg = (1.0f + (float)CW_MIN(p->beam, 180) / 60.0f + 0.5f * (float)p->dmg) * rar, pierce = 6, life = 9; /* thicker and hotter the longer it burns */
      s->b[slot] = (bullet){ true, (uint8_t)i, kind, (uint8_t)(kind == 9 ? 5 : p->pu_ammo_t > 0 && p->pu_ammo == PU_EXPLOSIVE && kind == 0 ? 1 : 0),
                             p->x + cw_cos(p->aim) * 0.6f, p->y + cw_sin(p->aim) * 0.6f, cw_cos(a) * v, cw_sin(a) * v, dmg, life, pierce };
      ev(s, rk || kind == 7 || kind == 8 ? EV_ROCKET : EV_SHOT, s->b[slot].x, s->b[slot].y, s->b[slot].vx, s->b[slot].vy, i, 0);
    }
  fired:;
  } else if (!fire) p->beam = 0;
  if (in->bomb && p->bomb_cd == 0 && p->grenades > 0) { /* a grenade in an arc toward the aim */
    p->grenades--;
    p->bomb_cd = 25;
    for (int j = 0; j < MAXB; j++)
      if (!s->b[j].alive) {
        s->b[j] = (bullet){ true, (uint8_t)i, 2, 0, p->x, p->y, cw_cos(p->aim) * 15.0f, cw_sin(p->aim) * 15.0f, 0, 36, 0 };
        break;
      }
  } else if (in->bomb && p->bomb_cd == 0 && p->mines > 0) { /* a mine at the feet, armed after half a second */
    p->mines--;
    p->bomb_cd = 25;
    for (int j = 0; j < MAXM; j++)
      if (!s->m[j].alive) { s->m[j] = (mine){ true, (uint8_t)i, p->x, p->y, 30 }; break; }
  } else if (in->bomb && p->bomb_cd == 0) {
    p->bomb_cd = 60 * 7;
    float bx = p->x + cw_cos(p->aim) * 6.0f, by = p->y + cw_sin(p->aim) * 6.0f;
    queue_boom(s, bx, by, 6.0f + (float)p->radius * 0.6f, 60, 8, i, true);
    queue_boom(s, bx + 1.8f, by - 1.2f, 4, 40, 18, i, false);
    queue_boom(s, bx - 1.8f, by + 1.2f, 4, 40, 26, i, false);
  }
  if (p->pu_weapon_t > 0) p->pu_weapon_t--;
  if (p->pu_ammo_t > 0) p->pu_ammo_t--;
  if (p->shield_t > 0) p->shield_t--;
  if (p->magnet_t > 0) p->magnet_t--;
  if (p->saw_t > 0) { /* two saws orbit the player and shred what they touch */
    p->saw_t--;
    p->saw_a += 0.11f;
    if (s->tick % 4 == 0)
      for (int k = 0; k < 2; k++) {
        float a = p->saw_a + (float)k * 3.14159265f;
        saw_ctx c = { i, p->x + cw_cos(a) * 2.4f, p->y + cw_sin(a) * 2.4f };
        each_enemy(s, c.x, c.y, 0.55f, saw_hit, &c);
      }
  }
  { /* stats: which power-up was held this tick */
    int held = p->pu_weapon_t > 0 ? p->pu_weapon : p->pu_ammo_t > 0 ? p->pu_ammo : p->saw_t > 0 ? PU_SAWS : -1;
    if (held >= 0) s->pu_ticks[held]++;
    else s->plain_ticks++;
  }
}

static void bullets_step(sim *s) {
  for (int j = 0; j < MAXB; j++) {
    bullet *b = &s->b[j];
    if (!b->alive) continue;
    if (b->kind == 8 && b->life < 84) { /* a seeker turns toward the nearest enemy in a 60-degree cone ahead */
      float sp = sqrtf(b->vx * b->vx + b->vy * b->vy), fx = b->vx / sp, fy = b->vy / sp, bd = 12 * 12;
      int best = -1;
      for (int k = 0; k < MAXE; k += 3) {
        const enemy *e = &s->e[k];
        if (!e->alive) continue;
        float dx = e->x - b->x, dy = e->y - b->y, d = dx * dx + dy * dy;
        if (d < bd && dx * fx + dy * fy > 0.5f * sqrtf(d)) bd = d, best = k;
      }
      if (best >= 0) {
        float dx = s->e[best].x - b->x, dy = s->e[best].y - b->y, l = sqrtf(dx * dx + dy * dy);
        b->vx += (dx / l * sp - b->vx) * 0.15f, b->vy += (dy / l * sp - b->vy) * 0.15f;
      }
    }
    if (b->kind == 9 && (b->fx == 0 || b->life < 70)) { /* the wheel comes home */
      const player *o = &s->p[b->owner];
      float dx = o->x - b->x, dy = o->y - b->y, l = sqrtf(dx * dx + dy * dy);
      if (l < 0.9f) { b->alive = false; continue; }
      b->vx = dx / l * 20.0f, b->vy = dy / l * 20.0f, b->life = CW_MAX(b->life, 2);
    }
    b->x += b->vx / TICK;
    b->y += b->vy / TICK;
    if (b->kind == 9 && b->fx > 0 && (fabsf(b->x) > HALF - 0.5f || fabsf(b->y) > HALF - 0.5f)) { /* off the arena's edge */
      if (fabsf(b->x) > HALF - 0.5f) b->vx = -b->vx;
      if (fabsf(b->y) > HALF - 0.5f) b->vy = -b->vy;
      b->fx--;
    }
    if (--b->life <= 0 || b->x < -HALF || b->x > HALF || b->y < -HALF || b->y > HALF) {
      if (b->kind == 7 && b->alive) queue_boom(s, b->x, b->y, 3.0f, b->dmg, 0, b->owner, true), add_zone(s, 3, b->x, b->y, 1.6f, 180); /* the shell lands */
      if (b->kind == 8 && b->alive) queue_boom(s, b->x, b->y, 1.8f, 6, 0, b->owner, false);
      if (b->kind == 1 && b->alive) queue_boom(s, b->x, b->y, 2.4f, 8, 0, b->owner, false);
      if (b->kind == 2 && b->alive) queue_boom(s, b->x, b->y, 5.0f + (float)s->p[b->owner].radius * 0.5f, 30, 0, b->owner, true); /* the grenade lands */
      b->alive = false;
      continue;
    }
    for (int k = 0; k < MAXO && b->kind != 7; k++) { /* (the mortar's shells fly over cover) */
      obstacle *o = &s->o[k];
      if (!o->alive || !in_box(o, b->x, b->y, 0.05f)) continue;
      if (b->kind == 9 && b->fx > 0) { /* the wheel glances off: back the way it came on the axis it struck */
        float px = b->x - o->x, py = b->y - o->y;
        if (fabsf(px) / o->hx > fabsf(py) / o->hy) b->vx = -b->vx; else b->vy = -b->vy;
        b->x += b->vx / TICK * 2, b->y += b->vy / TICK * 2;
        b->fx--;
        break;
      }
      if (b->kind == 9) break;
      if (b->kind == 1) queue_boom(s, b->x, b->y, 2.4f + (float)s->p[b->owner].radius * 0.5f, 3.0f + b->dmg * 2.0f, 0, b->owner, false);
      else if (b->kind == 2) queue_boom(s, b->x - b->vx / TICK, b->y - b->vy / TICK, 5.0f, 30, 0, b->owner, true);
      else hurt_obstacle(s, k, (int)b->dmg);
      b->alive = false;
      break;
    }
    if (!b->alive) continue;
    if (b->kind == 7) continue; /* a shell in flight touches nothing until it lands */
    for (int k = 0; k < MAXPILE; k++) { /* a shot pile of bones stays down */
      pile *pl = &s->pl[k];
      if (pl->alive && fabsf(pl->x - b->x) < 0.6f && fabsf(pl->y - b->y) < 0.6f) pl->alive = false;
    }
    bullet_ctx c = { j, false };
    each_enemy(s, b->x, b->y, 0.12f, bullet_hit, &c);
  }
}

/* the descent's floors: levels 1 to 4 have 5 waves, 5 to 8 have 6 (LEVELS.md section 1) */
static const int LEVEL_WAVES[NLEVELS] = { 5, 5, 5, 5, 6, 6, 6, 6 };
int sim_level_waves(int level) { return LEVEL_WAVES[CW_CLAMP(level, 0, NLEVELS - 1)]; }
int sim_level_first_wave(int level) {
  int w = 1;
  for (int k = 0; k < level && k < NLEVELS; k++) w += LEVEL_WAVES[k];
  return w;
}

static void spawn_director(sim *s) {
  s->wave_tick++;
  bool boss_wave = s->level_wave >= LEVEL_WAVES[s->level];
  if (boss_wave && s->boss_wave == s->wave && s->boss_alive == 0 && s->wave_tick > 90) { /* the floor's boss is down: the horde melts */
    s->state = ST_CLEAR;
    s->state_ticks = 0;
    return;
  }
  if (s->wave_tick >= WAVE_TICKS && !boss_wave) { /* a boss wave lasts until its boss falls */
    s->wave_tick = 0;
    s->wave++, s->level_wave++;
    ev(s, EV_WAVE, 0, 0, 0, 0, s->wave, 0);
  }
  /* a slow start, a horde late: about 25 at wave 1, 330 at 5, 1000 at 10,
   * 2900 by 20, more with more players */
  {
    int w = dwave(s);
    float k = 0.6f + 0.2f * (float)s->players;
    s->spawn_cap = CW_MIN((int)((float)(20 + 40 * (w - 1) + 6 * w * w) * k), 2900);
  }
  float cx, cy, hw, hh;
  sim_team_box(s, &cx, &cy, &hw, &hh);
  s->cam_x = cx, s->cam_y = cy;
  if (boss_wave && s->wave_tick == 30 && s->boss_alive == 0 && s->boss_wave != s->wave) { /* the floor's last wave: its boss */
    s->boss_wave = s->wave;
    float bx = 0, by = 0;
    for (int t = 0; t < 24; t++) { /* 24 m out, on ground the horde can walk from to the team (not shut in behind a wall) */
      float a = rf(s) * 6.28f, d = t < 12 ? 24.0f : 14.0f;
      bx = cw_clampf(cx + cw_cos(a) * d, -HALF + 3, HALF - 3), by = cw_clampf(cy + cw_sin(a) * d, -HALF + 3, HALF - 3);
      int c = CW_CLAMP((int)(by + HALF), 0, FW - 1) * FW + CW_CLAMP((int)(bx + HALF), 0, FW - 1);
      if (!s->blocked[c] && s->flow[c] != 0xffff) break;
    }
    int bi = spawn_enemy(s, EK_BOSS, bx, by);
    if (bi >= 0) s->e[bi].act_next = 240, ev(s, EV_BOSS, bx, by, 0, 0, 0, 0);
  }
  int budget = 14;
  while (s->nenemies < s->spawn_cap && budget-- > 0) {
    float a = rf(s) * 6.2832f, d = s->tick < 600 ? 16.0f + rf(s) * 6.0f : 22.0f + rf(s) * 10.0f; /* the opening is closer: a first kill within 3 s */
    int host = (int)(cw_rng_u32(&s->rng) % (uint32_t)MAXP); /* the ring is around one living player, so every player is met at spawn distance */
    for (int t = 0; t < MAXP && !(s->p[host].active && !s->p[host].down); t++) host = (host + 1) % MAXP;
    float hx = s->p[host].active && !s->p[host].down ? s->p[host].x : cx, hy = s->p[host].active && !s->p[host].down ? s->p[host].y : cy;
    float x = cw_clampf(hx + cw_cos(a) * d, -HALF + 1, HALF - 1), y = cw_clampf(hy + cw_sin(a) * d, -HALF + 1, HALF - 1);
    int gx = (int)(x + HALF), gy = (int)(y + HALF);
    if (s->blocked[gy * FW + gx]) continue;
    bool close = false; /* never on top of a player (a corner of the yard can bring the ring near one) */
    for (int k = 0; k < MAXP; k++) {
      const player *p = &s->p[k];
      float dx = p->x - x, dy = p->y - y;
      if (p->active && dx * dx + dy * dy < 14.0f * 14.0f) close = true;
    }
    if (close) continue;
    float r = rf(s);
    int kind = EK_RUNNER;
    int dw = dwave(s);
    const floor_def *fd = &s->fl[s->level];
    if (fd->set && fd->nwave > 0) { /* the floor's own mix (LEVELS.md): this wave's shares */
      const float *sh = fd->share[CW_CLAMP(s->level_wave - 1, 0, fd->nwave - 1)];
      float acc = 0;
      for (int k = 0; k < EK_KINDS; k++) {
        acc += sh[k];
        if (r < acc) { kind = k; break; }
      }
      if (kind == EK_BOSS) kind = EK_RUNNER;
    } else if (dw >= 2 && r < CW_MIN(0.02f + 0.004f * (float)dw, 0.07f)) kind = EK_BRUTE;
    else if (dw >= 3 && r < CW_MIN(0.06f + 0.006f * (float)dw, 0.14f)) kind = EK_BLOATER;
    if (spawn_enemy(s, kind, x, y) < 0) break;
  }
}

static void enemies_step(sim *s) {
  if (s->flow_age++ >= 6) { CW_PROFILE_SCOPE("sim.flow"); build_flow(s); }
  int living = 0;
  for (int i = 0; i < MAXP; i++) living += s->p[i].active && !s->p[i].down;
  for (int i = 0; i < MAXE; i++) {
    enemy *e = &s->e[i];
    if (!e->alive) continue;
    /* nearest living player */
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < MAXP; k++) {
      const player *p = &s->p[k];
      if (!p->active || p->down) continue;
      float dx = p->x - e->x, dy = p->y - e->y, d = dx * dx + dy * dy;
      if (d < bd) bd = d, best = k;
    }
    float vx = 0, vy = 0;
    if (best >= 0) {
      const player *p = &s->p[best];
      if (bd < 7.0f * 7.0f) {
        float l = cw_v2_length(cw_v2(p->x - e->x, p->y - e->y));
        if (l > 0.01f) vx = (p->x - e->x) / l, vy = (p->y - e->y) / l;
      } else {
        int cx = CW_CLAMP((int)(e->x + HALF), 0, FW - 1), cy = CW_CLAMP((int)(e->y + HALF), 0, FW - 1);
        uint16_t bestd = s->flow[cy * FW + cx];
        int bx = cx, by = cy;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            int nx = cx + dx, ny = cy + dy;
            if (nx < 0 || ny < 0 || nx >= FW || ny >= FW) continue;
            uint16_t d = s->flow[ny * FW + nx];
            if (d < bestd) bestd = d, bx = nx, by = ny;
          }
        if (bx != cx || by != cy) {
          float tx = (float)bx + 0.5f - HALF - e->x, ty = (float)by + 0.5f - HALF - e->y;
          float l = cw_v2_length(cw_v2(tx, ty));
          vx = tx / l, vy = ty / l;
        } else if (bd > 0) {
          float l = cw_v2_length(cw_v2(p->x - e->x, p->y - e->y));
          vx = (p->x - e->x) / l, vy = (p->y - e->y) / l;
        }
      }
    }
    float sp = e->speed * (e->spawn_age < 40 ? 0.5f : 1.0f);
    if (e->kind == EK_BOSS && boss_of(s) == BOSS_YARD) { /* walk, stand still and glow (the telegraph), then charge straight ahead */
      e->ph = (e->ph + 1) % 330;
      if (e->ph >= 230 && e->ph < 290) sp = 0;
      else if (e->ph >= 290) sp *= 3.0f;
    } else if (e->kind == EK_SLAGBORN && best >= 0) { /* the slagborn's lob: it stops and cups its hands 0.7 s, then a slag blob arcs to where the player stood */
      const player *p = &s->p[best];
      if (e->act) {
        sp = 0;
        if (++e->act_t == 42) add_zone(s, 1, p->x, p->y, 1.1f, 180), e->act = 0, e->act_t = 0, e->act_next = 240 + (int)(rf(s) * 120);
      } else if (--e->act_next <= 0 && bd > 5 * 5 && bd < 13 * 13 && e->atk == 0) e->act = 1, e->act_t = 0;
    } else if (e->kind == EK_BOSS) { /* a floor's boss: its arena attacks and summons, planted while it does them */
      boss_step(s, e);
      if (e->act) sp = 0;
      else if (boss_of(s) == BOSS_VHESSA && boss_frac(s, e) < 0.5f) sp *= 1.25f;
    }
    if (e->atk > 0) sp = 0; /* planted through the wind-up, the strike and the recovery */
    if (e->pin > 0) e->pin--, sp = 0;            /* nailed by a stake */
    if (e->frozen > 0) e->frozen--, sp = 0;      /* frozen solid */
    if (e->slow > 0) e->slow--, sp *= 0.5f;      /* chilled */
    if (e->burn > 0 && --e->burn % 20 == 0) {   /* burning: a point every third of a second */
      e->hp -= 1, e->flash = 2;
      if (e->hp <= 0) { kill_enemy(s, i, e->burn_owner, 0, 0); continue; }
    }
    if (e->kind != EK_SKITTER && in_hazard(s, HZ_WATER, e->x, e->y)) sp *= 0.7f; /* wading */
    e->spawn_age++;
    vx = vx * sp + e->kx * 60.0f;
    vy = vy * sp + e->ky * 60.0f;
    e->kx *= 0.86f;
    e->ky *= 0.86f;
    cw_crowd_set_velocity(s->crowd, e->agent, (t3_vec3){ vx, 0, vy });
    if (e->flash > 0) e->flash--;
    if (e->atk_cd > 0) e->atk_cd--;
  }
  (void)living;
  { CW_PROFILE_SCOPE("sim.crowd"); cw_crowd_step(s->crowd, 1.0f / TICK); }
  s->crowd_ms = cw_crowd_get_stats(s->crowd).step_ms;
  for (int i = 0; i < MAXE; i++) {
    enemy *e = &s->e[i];
    if (!e->alive) continue;
    t3_vec3 p = cw_crowd_position(s->crowd, e->agent);
    float dx = p.x - e->x, dy = p.z - e->y;
    if (dx * dx + dy * dy > 0.0004f) e->face = cw_atan2(dy, dx);
    e->x = p.x, e->y = p.z;
  }
  { CW_PROFILE_SCOPE("sim.grid"); build_grid(s); }
  CW_PROFILE_BEGIN("sim.contact");
  /* melee: an enemy next to a player winds up, strikes on one tick and recovers (MEL) */
  for (int i = 0; i < MAXE; i++) {
    enemy *e = &s->e[i];
    if (!e->alive) continue;
    const melee *m = &MEL[e->kind];
    static const melee VHESSA = { 48, 30, 30, 1.0f, 2.4f, -0.34f }, OSSIVAR = { 42, 30, 30, 1.6f, 1.8f, -1.0f };
    if (e->kind == EK_BOSS && boss_of(s) == BOSS_VHESSA) m = &VHESSA;
    if (e->kind == EK_BOSS && boss_of(s) == BOSS_OSSIVAR) m = &OSSIVAR;
    static const melee ANVIL = { 60, 36, 40, 1.4f, 6.0f, -1.0f }, MYCEL = { 48, 30, 30, 1.2f, 2.6f, 0.0f }; /* the hammer fall's shockwave ring; the root lash */
    if (e->kind == EK_BOSS && boss_of(s) == BOSS_ANVILGRAVE) m = &ANVIL;
    if (e->kind == EK_BOSS && boss_of(s) == BOSS_MYCEL) m = &MYCEL;
    static const melee SERA = { 42, 30, 30, 2.0f, 3.0f, -1.0f }, KHAR = { 48, 30, 30, 1.4f, 2.6f, 0.5f }, UNMK = { 60, 36, 40, 2.0f, 6.0f, 0.9f };
    if (e->kind == EK_BOSS && boss_of(s) == BOSS_SERAPHEL) m = &SERA; /* the censer's circle */
    if (e->kind == EK_BOSS && boss_of(s) == BOSS_KHARVAAL) m = &KHAR; /* the blade's 120 degrees */
    if (e->kind == EK_BOSS && boss_of(s) == BOSS_UNMAKER) m = &UNMK;  /* the riven blade's long line */
    float touch = EK[e->kind].radius + 0.45f;
    if (e->atk == 0) {
      if (e->atk_cd > 0 || (e->kind == EK_BOSS && (boss_of(s) == BOSS_YARD ? e->ph >= 230 : e->act != 0))) continue; /* a boss's charge or arena attack is its own */
      int best = -1;
      float bd = (touch + m->trigger) * (touch + m->trigger);
      for (int k = 0; k < MAXP; k++) {
        const player *p = &s->p[k];
        if (!p->active || p->down) continue;
        float dx = p->x - e->x, dy = p->y - e->y, d = dx * dx + dy * dy;
        if (d <= bd) bd = d, best = k;
      }
      if (best < 0) continue;
      e->atk = 1, e->tgt = best;
    } else {
      e->atk++;
    }
    const player *tp = e->tgt >= 0 ? &s->p[e->tgt] : NULL;
    /* it turns to its target through the wind-up, and commits for its last few ticks (a late dodge sideways works) */
    if (tp && e->atk <= m->windup - 4) {
      float dx = tp->x - e->x, dy = tp->y - e->y;
      if (dx * dx + dy * dy > 1e-4f) e->face = cw_atan2(dy, dx);
    }
    if (e->atk == m->windup + 1) { /* the strike: every player in reach and in front */
      float fx = cw_cos(e->face), fy = cw_sin(e->face), reach = touch + m->reach;
      if (e->kind == EK_SKITTER || e->kind == EK_GARGOYLE) e->kx += fx * 0.12f, e->ky += fy * 0.12f; /* the spring, the dive: it lands where it bit */
      if (e->kind == EK_PENITENT || e->kind == EK_HELLHOUND) e->kx += fx * 0.16f, e->ky += fy * 0.16f; /* the rush, the pounce */
      bool hit = false;
      for (int k = 0; k < MAXP; k++) {
        player *p = &s->p[k];
        if (!p->active || p->down) continue;
        float dx = p->x - e->x, dy = p->y - e->y, d2 = dx * dx + dy * dy;
        if (d2 > reach * reach) continue;
        float d = sqrtf(d2);
        if (d > 0.3f && (dx * fx + dy * fy) / d < m->arc) continue;
        if (p->shield_t > 0) { /* the bubble shoves them off */
          if (d > 0.01f) e->kx -= dx / d * 0.35f, e->ky -= dy / d * 0.35f;
          continue;
        }
        if (p->inv > 0) continue;
        float dmg = (float)EK[e->kind].dmg * (1.0f - 0.08f * (float)CW_MIN(p->armor, 6));
        p->hp -= dmg;
        p->inv = 12;
        hit = true;
        ev(s, EV_HURT, p->x, p->y, dx, dy, k, e->kind);
        if (p->hp <= 0) {
          p->down = true;
          p->down_ticks = 0;
          ev(s, EV_DOWN, p->x, p->y, 0, 0, k, 0);
        }
      }
      ev(s, EV_SWIPE, e->x + fx * touch, e->y + fy * touch, fx, fy, e->kind, hit ? 1 : 0);
      if (e->kind == EK_BOSS && (boss_of(s) == BOSS_OSSIVAR || boss_of(s) == BOSS_ANVILGRAVE)) ev(s, EV_SPIKES, e->x, e->y, touch + m->reach, 0, 0, 0); /* the crozier's ring of spikes */
    }
    if (e->atk > m->windup + m->recover) e->atk = 0, e->atk_cd = m->cooldown, e->tgt = -1;
  }
  CW_PROFILE_END();
  /* the boss calls runners */
  s->boss_hp = 0, s->boss_phase = 0;
  for (int i = 0; i < MAXE; i++)
    if (s->e[i].alive && s->e[i].kind == EK_BOSS) {
      s->boss_hp += s->e[i].hp;
      s->boss_phase = s->e[i].ph >= 230 && s->e[i].ph < 290 ? 1 : s->e[i].ph >= 290 ? 2 : 0;
    }
  if (s->boss_alive && s->tick % 300 == 0)
    for (int i = 0; i < MAXE; i++)
      if (s->e[i].alive && s->e[i].kind == EK_BOSS) {
        for (int k = 0; k < 3 + s->players; k++) {
          float a = rf(s) * 6.28f;
          spawn_enemy(s, EK_RUNNER, s->e[i].x + cw_cos(a) * 2.2f, s->e[i].y + cw_sin(a) * 2.2f);
        }
      }
}

/* a power-up lying at (x, y) (development: HORDE_POWER_SHOW) */
int sim_spawn(sim *s, int kind, float x, float y) { return spawn_enemy(s, kind, x, y); }
void sim_dev_kill_boss(sim *s) {
  for (int i = 0; i < MAXE; i++)
    if (s->e[i].alive && s->e[i].kind == EK_BOSS) kill_enemy(s, i, 0, 0, 0);
}
void sim_power_drop(sim *s, float x, float y, int t) { drop_value(s, PK_POWER, x, y, t % PU_KINDS); }

/* what a power-up gives */
void sim_power_give(sim *s, int pi, int t) {
  player *p = &s->p[pi];
  switch (sim_power_category(t)) {
  case 0: p->pu_weapon = t, p->pu_weapon_t = PU_WEAPON_TICKS, p->pu_rarity = RARE_COMMON; break;
  case 1: p->pu_ammo = t, p->pu_ammo_t = PU_WEAPON_TICKS; break;
  case 2:
    if (t == PU_GRENADES) p->grenades = CW_MIN(p->grenades + 4, 8);
    else p->mines = CW_MIN(p->mines + 3, 6);
    break;
  case 3: p->saw_t = 60 * 12; break;
  case 4: p->shield_t = 60 * 8; break;
  default: p->magnet_t = 60 * 10; break;
  }
  s->pu_taken[t]++;
}
/* how much a player holds already: of several players at a power-up, the one holding least takes it, and
 * nobody takes a second of a timed kind they still hold for more than 5 s (no hogging in co-op) */
static int power_load(const player *p, int t) {
  int c = sim_power_category(t);
  if ((c == 0 && p->pu_weapon_t > 300) || (c == 1 && p->pu_ammo_t > 300) || (c == 3 && p->saw_t > 300) || (c == 4 && p->shield_t > 120) ||
      (c == 5 && p->magnet_t > 120) || (t == PU_GRENADES && p->grenades >= 8) || (t == PU_MINES && p->mines >= 6))
    return -1;
  return (p->pu_weapon_t > 0) + (p->pu_ammo_t > 0) + (p->saw_t > 0) + (p->shield_t > 0) + (p->magnet_t > 0) + (p->grenades > 0) + (p->mines > 0);
}
static void cache_pickup(sim *s, int gi) {
  pickup *g = &s->g[gi];
  for (int n = 0; n < MAXP; n++) {
    int k = (n + s->tick) % MAXP;
    player *p = &s->p[k];
    if (!p->active || p->down) continue;
    float dx = p->x - g->x, dy = p->y - g->y;
    if (dx * dx + dy * dy > 1.0f) continue;
    int w = g->value & 0xff, r = CW_CLAMP(g->value >> 8, 0, RARE_KINDS - 1);
    if (p->pu_weapon_t > 0 && p->pu_rarity > r) continue; /* a better gun in hand: leave it for a friend */
    g->alive = false;
    p->pu_weapon = w, p->pu_weapon_t = RARITY_TICKS[r], p->pu_rarity = r;
    s->pu_taken[w]++;
    ev(s, EV_POWER, g->x, g->y, 0, 0, k, w);
    return;
  }
}
static void power_pickup(sim *s, int gi) {
  pickup *g = &s->g[gi];
  int best = -1, bl = 1 << 20;
  for (int n = 0; n < MAXP; n++) {
    int k = (n + s->tick) % MAXP; /* ties go round the players */
    const player *p = &s->p[k];
    if (!p->active || p->down) continue;
    float dx = p->x - g->x, dy = p->y - g->y;
    if (dx * dx + dy * dy > 1.0f) continue;
    int l = power_load(p, g->value);
    if (l >= 0 && l < bl) bl = l, best = k;
  }
  if (best < 0) return;
  g->alive = false;
  sim_power_give(s, best, g->value);
  ev(s, EV_POWER, g->x, g->y, 0, 0, best, g->value);
}

/* power-ups placed in the yard: the risky corners, the gaps in the cinder ring the horde pours through, the
 * ends of the painted lane and behind the crates; one every 30 s at the next free spot (a seeded order),
 * at most three lying at once */
static void power_director(sim *s) {
  if (s->no_power || s->state != ST_PLAY || s->tick < 60 * 20 || s->tick % (60 * 30) != 0 || !s->pu_spots) return;
  int lying = 0;
  for (int i = 0; i < MAXG; i++) lying += s->g[i].alive && s->g[i].kind == PK_POWER;
  if (lying >= 3) return;
  for (int tries = 0; tries < s->pu_spots; tries++) {
    int k = (s->pu_next++ * 7) % s->pu_spots;
    float x = s->spot_x[k], y = s->spot_y[k];
    bool taken = false;
    for (int i = 0; i < MAXG; i++)
      if (s->g[i].alive && s->g[i].kind == PK_POWER && (s->g[i].x - x) * (s->g[i].x - x) + (s->g[i].y - y) * (s->g[i].y - y) < 4) taken = true;
    if (taken) continue;
    int t = (int)(cw_rng_u32(&s->rng) % PU_BASE_KINDS);
    static const int NEW_GUN[NLEVELS] = { -1, PU_ARC, PU_STAKE, PU_MORTAR, PU_SEEKER, PU_WHEEL, PU_FROST, PU_HELLFIRE };
    if (s->level > 0 && NEW_GUN[s->level] >= 0 && t % 3 == 0) t = NEW_GUN[s->level]; /* a floor's own gun, a third of the time */
    drop_value(s, PK_POWER, x, y, t);
    ev(s, EV_POWER_SPAWN, x, y, 0, 0, 0, t);
    return;
  }
}

static void mines_step(sim *s) {
  for (int j = 0; j < MAXM; j++) {
    mine *m = &s->m[j];
    if (!m->alive) continue;
    if (m->arm > 0) { m->arm--; continue; }
    bool near = false;
    int x0 = CW_CLAMP((int)((m->x - 1.4f + HALF) * 0.5f), 0, 40), x1 = CW_CLAMP((int)((m->x + 1.4f + HALF) * 0.5f), 0, 40);
    int y0 = CW_CLAMP((int)((m->y - 1.4f + HALF) * 0.5f), 0, 40), y1 = CW_CLAMP((int)((m->y + 1.4f + HALF) * 0.5f), 0, 40);
    for (int cy = y0; cy <= y1 && !near; cy++)
      for (int cx = x0; cx <= x1 && !near; cx++)
        for (int k = s->cell_start[cy * 41 + cx]; k < s->cell_start[cy * 41 + cx + 1]; k++) {
          const enemy *e = &s->e[s->cell_idx[k]];
          if (e->alive && (e->x - m->x) * (e->x - m->x) + (e->y - m->y) * (e->y - m->y) < 1.4f * 1.4f) { near = true; break; }
        }
    if (near) {
      m->alive = false;
      queue_boom(s, m->x, m->y, 4.5f, 35, 0, m->owner, true);
    }
  }
}

static void pickups_step(sim *s) {
  for (int i = 0; i < MAXG; i++) {
    pickup *g = &s->g[i];
    if (!g->alive) continue;
    if (--g->life <= 0) { g->alive = false; continue; }
    if (++g->age > 60 * 8 && g->kind == 0) { /* a gem left on the ground for 8 s flies to the nearest living player */
      int near = -1;
      float bd = 1e9f;
      for (int k = 0; k < MAXP; k++) {
        const player *p = &s->p[k];
        if (!p->active || p->down) continue;
        float dx = p->x - g->x, dy = p->y - g->y, d = dx * dx + dy * dy;
        if (d < bd) bd = d, near = k;
      }
      if (near >= 0 && bd > 0.0001f) {
        float d = cw_v2_length(cw_v2(s->p[near].x - g->x, s->p[near].y - g->y));
        g->vx += (s->p[near].x - g->x) / d * 0.9f, g->vy += (s->p[near].y - g->y) / d * 0.9f;
      }
    }
    g->x += g->vx / TICK;
    g->y += g->vy / TICK;
    g->vx *= 0.93f;
    g->vy *= 0.93f;
    g->x = cw_clampf(g->x, -HALF + 0.5f, HALF - 0.5f);
    g->y = cw_clampf(g->y, -HALF + 0.5f, HALF - 0.5f);
    if (g->kind == PK_POWER) { power_pickup(s, i); continue; }
    if (g->kind == PK_CACHE) { cache_pickup(s, i); continue; }
    for (int k = 0; k < MAXP; k++) {
      player *p = &s->p[k];
      if (!p->active || p->down) continue;
      float dx = p->x - g->x, dy = p->y - g->y, d2 = dx * dx + dy * dy, mr = p->magnet_t > 0 ? 30.0f : 3.0f + (float)p->magnet * 1.6f;
      if (d2 < mr * mr && d2 > 0.0001f) {
        float d = cw_v2_length(cw_v2(dx, dy)), pull = 0.22f;
        g->vx += dx / d * pull * TICK * 0.12f;
        g->vy += dy / d * pull * TICK * 0.12f;
      }
      if (d2 < 0.55f) {
        g->alive = false;
        if (g->kind == 0) {
          p->xp += g->value;
          p->score += 2 * g->value;
          while (p->xp >= p->xp_next) level_up(s, p, k);
        } else p->hp = CW_MIN(p->hp + 35, 100.0f);
        ev(s, EV_GEM, g->x, g->y, 0, 0, g->kind, k);
        break;
      }
    }
  }
}

int sim_alive_players(const sim *s) {
  int n = 0;
  for (int i = 0; i < MAXP; i++) n += s->p[i].active && !s->p[i].down;
  return n;
}

void sim_team_box(const sim *s, float *cx, float *cy, float *hw, float *hh) {
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  int n = 0;
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (!p->active || p->down) continue;
    x0 = CW_MIN(x0, p->x), x1 = CW_MAX(x1, p->x), y0 = CW_MIN(y0, p->y), y1 = CW_MAX(y1, p->y);
    n++;
  }
  if (!n) {
    *cx = s->cam_x, *cy = s->cam_y, *hw = *hh = 4;
    return;
  }
  *cx = (x0 + x1) * 0.5f, *cy = (y0 + y1) * 0.5f, *hw = (x1 - x0) * 0.5f, *hh = (y1 - y0) * 0.5f;
}

/* a breadth-first walk out from the hatch's middle over the cells the walls leave open */
static void build_hatch_flow(sim *s) {
  static int16_t queue[FW * FW];
  build_flow_blocked(s);
  for (int i = 0; i < FW * FW; i++) s->hflow[i] = 0xffff;
  int c0 = (int)HALF * FW + (int)HALF, qh = 0, qt = 0;
  s->hflow[c0] = 0, queue[qt++] = (int16_t)c0;
  static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
  while (qh < qt) {
    int c = queue[qh++], cx = c % FW, cy = c / FW;
    for (int k = 0; k < 8; k++) {
      int nx = cx + DX[k], ny = cy + DY[k];
      if (nx < 0 || ny < 0 || nx >= FW || ny >= FW) continue;
      int n = ny * FW + nx;
      if (s->blocked[n] || s->hflow[n] != 0xffff) continue;
      if (k >= 4 && (s->blocked[cy * FW + nx] || s->blocked[ny * FW + cx])) continue;
      s->hflow[n] = (uint16_t)(s->hflow[c] + (k < 4 ? 2 : 3));
      queue[qt++] = (int16_t)n;
    }
  }
}
bool sim_hatch_way(const sim *s, float x, float y, float *dx, float *dy) {
  int cx = CW_CLAMP((int)(x + HALF), 0, FW - 1), cy = CW_CLAMP((int)(y + HALF), 0, FW - 1);
  if (s->hflow[cy * FW + cx] <= 4) { /* at the hatch: straight in */
    float l = sqrtf(x * x + y * y);
    if (l < 0.05f) return false;
    *dx = -x / l, *dy = -y / l;
    return true;
  }
  uint16_t best = s->hflow[cy * FW + cx];
  int bx = cx, by = cy;
  for (int oy = -1; oy <= 1; oy++)
    for (int ox = -1; ox <= 1; ox++) {
      int nx = cx + ox, ny = cy + oy;
      if (nx < 0 || ny < 0 || nx >= FW || ny >= FW) continue;
      if (s->hflow[ny * FW + nx] < best) best = s->hflow[ny * FW + nx], bx = nx, by = ny;
    }
  if (bx == cx && by == cy) return false;
  float tx = (float)bx + 0.5f - HALF - x, ty = (float)by + 0.5f - HALF - y, l = sqrtf(tx * tx + ty * ty);
  *dx = tx / l, *dy = ty / l;
  return true;
}

/* the next floor: the team stands up on its closed hatch with its levels and score, power-ups end */
static void floor_next(sim *s) {
  if (s->level >= NLEVELS - 1) { /* the last floor: the run is won */
    s->state = ST_WON;
    s->state_ticks = 0;
    return;
  }
  s->level++;
  s->level_wave = 1;
  s->wave = sim_level_first_wave(s->level);
  s->wave_tick = 0;
  s->boss_wave = 0;
  for (int i = 0; i < MAXE; i++)
    if (s->e[i].alive) kill_enemy(s, i, -1, 0, 0);
  memset(s->b, 0, sizeof s->b);
  memset(s->g, 0, sizeof s->g);
  memset(s->x, 0, sizeof s->x);
  memset(s->m, 0, sizeof s->m);
  memset(s->z, 0, sizeof s->z), memset(s->es, 0, sizeof s->es), memset(s->pl, 0, sizeof s->pl);
  floor_layout(s);
  s->flow_age = 1000;
  for (int i = 0; i < MAXP; i++) {
    player *p = &s->p[i];
    if (!p->active) continue;
    float a = (float)i * 6.2832f / (float)s->players;
    p->x = cw_cos(a) * 2.0f, p->y = cw_sin(a) * 2.0f;
    p->down = false, p->hp = 100, p->inv = 120;
    p->pu_weapon_t = p->pu_ammo_t = p->saw_t = p->shield_t = p->magnet_t = 0;
  }
  s->state = ST_PLAY;
  s->state_ticks = 0;
  ev(s, EV_FLOOR, 0, 0, 0, 0, s->level, 0);
}

/* after a floor's boss: the horde melts over MELT_TICKS (staggered, nobody hurt), the hatch opens, the first living
 * player to step in takes the team down after DESCEND_TICKS */
static void floor_step(sim *s) {
  s->state_ticks++;
  for (int i = 0; i < MAXP; i++) s->p[i].inv = CW_MAX(s->p[i].inv, 10);
  if (s->state == ST_CLEAR) {
    int left = 0;
    for (int i = 0; i < MAXE; i++) left += s->e[i].alive;
    int due = MELT_TICKS - s->state_ticks, n = due > 0 ? (left + due - 1) / due : left;
    for (int i = 0; i < MAXE && n > 0; i++) {
      int k = (i * 7919 + s->state_ticks * 31) % MAXE; /* scattered, not in slot order */
      enemy *e = &s->e[k];
      if (!e->alive) continue;
      ev(s, EV_MELT, e->x, e->y, 0, 0, e->kind, 0);
      kill_enemy(s, k, -1, 0, 0);
      n--;
    }
    if (s->state_ticks >= MELT_TICKS && left <= n) {
      for (int i = 0; i < MAXE; i++)
        if (s->e[i].alive) kill_enemy(s, i, -1, 0, 0);
      memset(s->x, 0, sizeof s->x); /* no bloater's burst outlives the floor */
      ev(s, EV_CLEAR, 0, 0, 0, 0, s->level, 0);
      ev(s, EV_HATCH, 0, 0, 0, 0, s->level, 0);
      build_hatch_flow(s);
      s->state = ST_HATCH;
      s->state_ticks = 0;
    }
  } else if (s->state == ST_HATCH) {
    if (s->state_ticks < HATCH_OPEN_TICKS) return; /* still opening */
    for (int i = 0; i < MAXP; i++) {
      const player *p = &s->p[i];
      if (p->active && !p->down && p->x * p->x + p->y * p->y < HATCH_R * HATCH_R) {
        ev(s, EV_DESCEND, p->x, p->y, 0, 0, i, 0);
        s->state = ST_DESCEND;
        s->state_ticks = 0;
        return;
      }
    }
  } else if (s->state == ST_DESCEND && s->state_ticks >= DESCEND_TICKS) {
    floor_next(s);
  }
}

void sim_step(sim *s, const input in[MAXP]) {
  s->tick++;
  s->ev_n = s->ev_n > MAXEV - 400 ? 0 : s->ev_n; /* nothing draws these: drop the backlog */
  if (s->state == ST_WON) { /* the horde falls apart, nobody gets hurt */
    s->state_ticks++;
    for (int i = 0; i < MAXP; i++) s->p[i].inv = 10;
    int n = 0;
    for (int i = 0; i < MAXE && n < 30; i++)
      if (s->e[i].alive && (i + s->tick) % 3 == 0) kill_enemy(s, i, -1, 0, 0), n++;
  }
  if (s->state == ST_CLEAR || s->state == ST_HATCH || s->state == ST_DESCEND) floor_step(s);
  if (s->state == ST_OVER) {
    if (++s->state_ticks > 60 * 6) {
      for (int i = 0; i < MAXP; i++)
        if (s->p[i].active) {
          player *p = &s->p[i];
          p->down = false, p->hp = 100, p->inv = 120;
        }
      s->state = ST_PLAY;
      if (s->level_wave > 1) s->wave--, s->level_wave--; /* a wave back, never above the floor's first */
      s->wave_tick = 0;
      s->boss_wave = 0;
      for (int i = 0; i < MAXE; i++)
        if (s->e[i].alive) kill_enemy(s, i, -1, 0, 0);
      memset(s->m, 0, sizeof s->m);
    }
  }
  { CW_PROFILE_SCOPE("sim.players"); for (int i = 0; i < MAXP; i++) player_step(s, i, &in[i]); }
  /* a downed player gets up when a teammate stands near for 2 s, or at each wave */
  for (int i = 0; i < MAXP; i++) {
    player *p = &s->p[i];
    if (!p->active || !p->down) continue;
    if (s->wave_tick == 1) { p->down = false, p->hp = 50, p->inv = 120; continue; }
    if (p->down_ticks > 60 * 8) { /* nobody came: the downed player is carried to the nearest living friend */
      int near = -1;
      float bd = 1e9f;
      for (int k = 0; k < MAXP; k++) {
        const player *q = &s->p[k];
        float dx = q->x - p->x, dy = q->y - p->y, d = dx * dx + dy * dy;
        if (k != i && q->active && !q->down && d < bd) bd = d, near = k;
      }
      if (near >= 0) { p->x = cw_clampf(s->p[near].x + 1.0f, -HALF + 0.6f, HALF - 0.6f), p->y = s->p[near].y, p->down = false, p->hp = 40, p->inv = 90; continue; }
    }
    for (int k = 0; k < MAXP; k++) {
      const player *q = &s->p[k];
      if (k == i || !q->active || q->down) continue;
      float dx = q->x - p->x, dy = q->y - p->y;
      if (dx * dx + dy * dy < 2.5f * 2.5f && p->down_ticks > 60 * 2 && s->tick % 60 == 0) { p->down = false, p->hp = 40, p->inv = 90; }
    }
  }
  { /* a player who got up (a wave, a friend, a rescue) may be far from the others: the team box is pulled back to one view */
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    int n = 0;
    for (int i = 0; i < MAXP; i++) {
      const player *p = &s->p[i];
      if (!p->active || p->down) continue;
      x0 = CW_MIN(x0, p->x), x1 = CW_MAX(x1, p->x), y0 = CW_MIN(y0, p->y), y1 = CW_MAX(y1, p->y), n++;
    }
    if (n > 1)
      for (int i = 0; i < MAXP; i++) {
        player *p = &s->p[i];
        if (!p->active || p->down) continue;
        float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
        if (x1 - x0 > 2 * TETHER_HW) p->x = cw_clampf(p->x, mx - TETHER_HW, mx + TETHER_HW);
        if (y1 - y0 > 2 * TETHER_HH) p->y = cw_clampf(p->y, my - TETHER_HH, my + TETHER_HH);
      }
  }
  if (s->state == ST_PLAY && s->players > 0 && sim_alive_players(s) == 0) {
    s->state = ST_OVER;
    s->state_ticks = 0;
  }
  if (s->state == ST_PLAY && !s->no_spawn) spawn_director(s);
  { CW_PROFILE_SCOPE("sim.enemies"); enemies_step(s); }
  { CW_PROFILE_SCOPE("sim.bullets"); bullets_step(s); }
  boss_world_step(s);
  for (int i = 0; i < MAXX; i++) { /* explosions chain: each can start another this very tick */
    boom *b = &s->x[i];
    if (b->r == 0) continue;
    if (b->delay-- > 0) continue;
    boom copy = *b;
    b->r = 0;
    s->nboom_total++;
    do_boom(s, &copy);
  }
  { CW_PROFILE_SCOPE("sim.pickups"); pickups_step(s); }
  mines_step(s);
  if (s->state == ST_PLAY) power_director(s);
  if (s->nav_dirty) {
    cw_nav_update(s->nav);
    s->nav_dirty = false;
  }
  uint32_t h = s->hash;
  for (int i = 0; i < MAXP; i++) {
    int32_t q[3] = { (int32_t)(s->p[i].x * 1000), (int32_t)(s->p[i].y * 1000), (int32_t)s->p[i].hp };
    for (size_t j = 0; j < 3; j++) h = (h ^ (uint32_t)q[j]) * 16777619u;
  }
  h = (h ^ (uint32_t)s->nenemies) * 16777619u;
  h = (h ^ (uint32_t)s->kills) * 16777619u;
  s->hash = h;
}
