/* arenafps (working title): split screen arena shooter for one to four
 * players, with navmesh bots. Deathmatch, team deathmatch and a co-op wave
 * mode, three arenas.
 *
 *   shell (title, join, pause, settings) -> a setup panel (arena, mode, bots)
 *   -> the match -> results -> setup again.
 *
 * The simulation runs in fixed_update only, from the input snapshot and
 * cw_rng, so a replay and every host play the same match. Visual effects
 * (fx.c) have their own random stream and never feed back.
 *
 * Debug fields: game.state, game.mode, game.arena, game.players, game.ticks,
 * game.hash, fighters.* (x, z, hp, score, alive per fighter), match.*,
 * nav.* (path queries, cost), views. */
#include <stdlib.h>
#include "cartwheel/debug.h"
#include "cartwheel/aim.h"
#include "cartwheel/anim.h"
#include "cartwheel/ui.h"
#include "cartwheel/environment.h"
#include "cartwheel/explosions.h"
#if __has_include("cartwheel/rim_light.h") /* the engine after v0.18.0: fighters get their side's rim; an older SDK builds without */
#include "cartwheel/rim_light.h"
#define HAVE_RIM 1
#else
#define HAVE_RIM 0
#endif
#include "fx.h"
#include "game.h"
#include "soldier.h"
#include "crowd.h"

#ifdef ARENA_AUTOPILOT
/* key art for a cart (the debug command keyart): what ARENA_CHASE and ARENA_NOHUD do natively, which a cart cannot
 * read before its first frame */
static char keyart_chase[64];
static int keyart_nohud;
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#define AUTO 1
#else
#define AUTO 0
#endif

#define VM_SCALE 0.62f
#define MAXP 24 /* rockets in flight */
#define MAX_FEED 5
#define SETUP_ROWS 7 /* arena, mode, bots, level, aim assist, rumble, rules (Start or fire begins from any row, as the panel's line says) */

/* ── weapons ───────────────────────────────────────────────────────── */
typedef struct weapon {
  const char *name;
  int damage, cooldown, pellets, max_ammo, start_ammo, kind; /* kind 0 bullets, 1 rocket, 2 rail */
  float spread, knock, range_pref;
} weapon;
static const weapon W[NWEAP] = {
  { "PULSE RIFLE", 9, 6, 1, 200, 90, 0, 0.011f, 1.2f, 12 },
  { "SCATTER GUN", 8, 46, 11, 40, 14, 0, 0.07f, 4.0f, 6 },
  { "ROCKET", 95, 44, 1, 20, 6, 1, 0, 0, 16 },
  { "RAIL", 70, 46, 1, 20, 6, 2, 0.0015f, 8.0f, 24 },
};

typedef struct proj {
  bool on;
  t3_vec3 p, v;
  int owner, life;
  float glow; /* the rocket's light, swelling from 0 after the launch */
  cw_node node, light, trail;
} proj;

typedef struct feed_line {
  char text[64];
  int ticks;
} feed_line;

/* the kinds of kill beyond the four guns (kill_fighter's weapon) */
enum { WK_GRENADE = NWEAP, WK_MELEE = NWEAP + 1, WK_KINDS };
/* power-ups (below, power_tick) */
static const char *POW_NAME[POW_KINDS] = { "", "QUAD DAMAGE", "HASTE", "REGENERATION" };
static const float POW_RGB[POW_KINDS][3] = { { 1, 1, 1 }, { 0.7f, 0.35f, 1 }, { 1, 0.75f, 0.2f }, { 0.35f, 1, 0.55f } };
static int32_t pow_stats[5]; /* debug power.stats: appeared, taken, frags by holders, holders downed, ticks held */
static int auto_note_until; /* Auto stepped the look down: a line says so until this tick (quality_changed, hud_feed) */
static char auto_note[96];
static struct { t3_vec3 pos; bool present; int kind, next, timer; cw_node node, gem, ring, light, disc; } PW; /* the spot, and what is on it */

static const char *MODE_NAME[MODE_COUNT] = { "DEATHMATCH", "TEAM DEATHMATCH", "CO-OP WAVES", "BATTLE" };
static const char *RULE_NAME[RULE_COUNT] = { "STANDARD", "INSTAGIB", "ROCKETS ONLY", "LOW GRAVITY", "BIG HEADS" };
/* low gravity: 0.45 of the fall, jumps a little softer (about 1.4 times as high) and jump pads scaled to land where they did */
#define LOWG_K 0.45f
static const char *TEAM_NAME[2] = { "RED", "BLUE" };
static const float TEAM_RGB[2][3] = { { 1.0f, 0.25f, 0.2f }, { 0.25f, 0.5f, 1.0f } };

static struct game {
  cw_tree *tree;
  cw_node root, world, env, sun, sky, fog, post, lighting;
  cw_node cam[MAXH], vm[MAXH], vm_flash[MAXH], vm_wp[MAXH][NWEAP];
  game_state state;
  int mode, arena_id, bots_setting, bot_level, aim_assist, players, nf;
  int awards;              /* awards on the results screen (drawn only) */
  int rules;               /* match_rule: the party layer's rules for the next match (setup row) */
  int setup_row;
  fighter f[MAXF];
  proj pr[MAXP];
  cw_rng rng;
  int ticks, match_ticks, over_ticks, time_limit, frag_limit;
  int team_score[2];
  int wave, wave_state, wave_timer, lives;
  /* co-op: the game tick each person last touched their pad; a seat untouched for AWAY_TICKS is away (it neither
   * comes back on a shared life nor makes the waves bigger) until its player presses something; sw idle_lives 1 (the
   * control of tests/arenafps-idle-seat.mjs) spends lives on it and counts it as before. away_skips: respawns held back */
  int last_in[MAXH], sw_idle_lives;
  int32_t away_skips, present;
  uint32_t hash;
  feed_line feed[MAX_FEED];
  int winner; /* fighter index or team, -1 none */
  /* per human view feedback */
  float hit_marker[MAXH], dmg_flash[MAXH], dmg_dir[MAXH], kill_flash[MAXH];
  float dmg_want[MAXH]; /* what the hits ask of the damage tint: the tint swells toward it, a burst holds it up */
  int dmg_hold[MAXH];    /* ticks the damage tint holds after the last hit */
  int32_t fl_tint_flashes, fl_near_views; /* the flash meter (tint_meter, view_hook) */
  float fl_tint_peak;
  int sw_view_hook; /* sw view_hook 1: the old shared gun layer and view hook (drawn view by view), the other side of the one-pass tests */
  int sw_hurt_hz; /* sw hurt <hz>: the first seat's damage cue that many times a second, no damage (the tint meter's range) */
  int dmg_taken[MAXH], dmg_dealt[MAXH], kills_now[MAXH]; /* this tick's damage per person, for the rumble */
  int rumble_off;          /* Rumble OFF on the setup screen; it and aim assist are kept in the game's save (arena_save) */
  char kill_name[MAXH][16];
  /* drawn only: who downed this person, a head shot's marker, the last pickup's line, the streak behind a frag */
  char killed_by[MAXH][16], pick_text[MAXH][28];
  /* drawn only, the killer cam: who downed this person (fighter index, -1 none), with what, from how far, and the
   * killer's health then; the dead view turns to them and follows them until the respawn */
  int kc_who[MAXH], kc_weapon[MAXH], kc_hp[MAXH];
  float kc_dist[MAXH];
  int sw_no_cues, cue_over; /* sw cues 0: no damage edges or bearing bar, no continue prompt on the results (the newcomer gate's control); the prompt drawn now */
  int sw_no_killcam; /* sw killcam 0: the old dead view (lies on the floor, looks nowhere): the killer cam gate's control */
  float head_marker[MAXH], pick_t[MAXH];
  int kill_streak[MAXH];
  cw_aim_state aim[MAXH];
  /* actions */
  cw_action mv_l, mv_r, mv_f, mv_b, lk_l, lk_r, lk_u, lk_d, fire, jump, w_next, w_prev, sprint, grenade, melee, ui_start, ui_back, ui_pause;
  /* nav cost */
  int path_requests, path_done;
  int path_links, link_jumps;  /* link waypoints in the paths bots took; jumps bots made up ledges */
  int32_t link_cross[4];       /* links bots crossed (reached the far end), by tag: -, jump, drop, pad */
  int32_t link_start[4];       /* link legs bots set out on (reached the near end), by tag */
  float nav_tick_ms;
  int views;
  int32_t d_score[MAXF], d_hp[MAXF], d_deaths[MAXF], d_weapon[MAXF], d_alive[MAXF];
  float d_pos[MAXF * 2], d_yaw[MAXF];
  int32_t kills_total, rockets_fired;
  float p1_in[5]; /* debug p1.input: what player 1's tick read (move x, y, look x, y, fire) */
  int32_t p1[6]; /* debug p1.stats: player 1's shots, hits, damage dealt, damage taken, kills, deaths to bots (playtest measures) */
  int32_t steps;
  int spawn_used[MAX_SPAWNS]; /* tick each spawn point was last used */
  /* measurements read by the playtest scripts (tools/pacing.mjs), never part of the simulation:
   * - sightings (enc): an enemy in sight, at most one every 5 s per fighter (the first pass's measure, kept for comparison)
   * - encounters (eng): an enemy comes into sight (45 m, clear line) after 3 s alive with none in sight, or first after a spawn;
   *   eng_gap_max is the longest alive time between two encounter starts of one fighter; contact_ticks the alive ticks with an
   *   enemy in sight (sampled every 10 ticks), so a fight that never stops reads as contact, not as one long quiet encounter
   * - spawns / unsafe_spawns: spawns where a living enemy could see the spawn point or stood within 15 m of it
   * - spawn_kills: a death within 5 s of spawning (2 s protection + 3 s) to an enemy that could see the spawn point or stood
   *   within 15 m of it when the victim spawned (killed because of where it spawned)
   * - quick_deaths: any death to an enemy within 6 s of spawning (the first pass's "spawn kill" counter)
   * - time from first damage to death, kills by weapon, the longest stretch without a kill */
  struct { int32_t enc, enc_gap_max, kills, spawn_kills, quick_deaths, ttk_sum, ttk_n, gap_max, last_kill, by_weapon[WK_KINDS], fighter_ticks;
           int32_t eng, eng_gap_max, contact_ticks, spawns, unsafe_spawns; } st;
  cw_font font;
} G;
static float grav_k(void) { return G.rules == RULE_LOWGRAV ? LOWG_K : 1.0f; }

static fighter *F(int i) { return &G.f[i]; }
static bool battle(void) { return G.mode == MODE_BATTLE; }
static bool hm_driving(const fighter *f);
static int present_players(void); /* co-op: the people holding the arena (below) */
static bool is_bot(const fighter *f) { return (AUTO && !hm_driving(f)) || f->human < 0; }

/* bot difficulty (setup row "bots"): skill scale, reaction before the first shot at a newly seen enemy (ticks, plus up to
 * react_span more for the least skilled), turn rate (rad/s = turn + turn_skill * skill) and the damage a bot's hit does to a
 * person. NORMAL is the default and is meant for a new pad player; HARD is the old
 * NORMAL's skill with quicker reactions. */
typedef struct bot_level_def { float skill_mul, skill_add, react, react_span, turn, turn_skill, dmg_to_people, err; bool legacy; float fov; } bot_level_def;
/* fov: the half angle (degrees) of the cone in which a bot notices a new enemy; 180 = all round. A bot that was just hit, or
 * hears a shot within 30 m, notices all round; one already fighting keeps its target in any direction (the bot vision of the
 * classic arena shooter bots: a 90 degree cone unless hurt or the enemy is shooting, wider up close). */
/* Tuned against the scripted human (tools/human-matches.mjs, findings/arenafps.md "A-PT1 human pass"): a casual pad player wins
 * or nearly wins most deathmatches on NORMAL, a skilled one on VETERAN, and is pushed on HARD. */
#define BOT_LEVELS 4
static bot_level_def LEVELS[BOT_LEVELS + 1] = {
  { 0.50f, 0.0f, 28, 8, 1.3f, 1.5f, 0.50f, 2.00f, false, 45 }, /* EASY: 470 to 600 ms, slow turns, wide misses, half damage */
  { 0.70f, 0.0f, 22, 8, 1.7f, 2.5f, 0.70f, 1.50f, false, 60 }, /* NORMAL: 370 to 500 ms; the default, for a newcomer */
  { 0.95f, 0.0f, 15, 9, 2.2f, 4.0f, 0.90f, 1.00f, false, 60 }, /* VETERAN: 250 to 400 ms */
  { 0.40f, 0.6f, 10, 6, 2.5f, 5.5f, 0.85f, 1.0f, false, 180 }, /* HARD: 170 to 270 ms, eyes all round */
  { 1.00f, 0.0f, 8, 24, 2.5f, 5.5f, 1.00f, 1.0f, true, 180 },  /* autopilot ARENA_LEVEL=4 only: the first pass's NORMAL, for before/after */
};
static float bot_fov_override = -1; /* ARENA_BOT_FOV (autopilot): the cone for measuring */
/* the seats' stand-in in an autopilot build (tests, pacing): a pad player of middling skill, as if aim assist and the stick
 * curve were in play: 300 ms reactions, the stick's 3.6 rad/s turn, aim error 0.7 of a 0.5 skill bot's with assist on */
static bool proxy(const fighter *f) { return AUTO && f->human >= 0; }
/* the flash fixes on (default); ARENA_T_strobefix=0 (autopilot) turns them off as the flash sweep's control: the damage
 * tint's old curve, bodies drawn however close to a view's camera, fighters free to stand inside each other */
static bool strobefix(void) { return arena_tune("strobefix", 1) > 0.5f; }
/* the game's own flash meter for its damage tint (debug fields fl.*, read over a range of frames): the tint's drawn
 * opacity, a transition each time it turns round after moving 0.1 or more (WCAG 2.3.1's pair of opposing changes, red
 * counted at any size the eye sees: 0.1 of the red's full opacity), a flash every two transitions, and the most flashes
 * any seat's tint made in any one second. Measurement only: nothing reads it back */
enum { FL_RING = 64 };
static struct { float ext[MAXH]; int dir[MAXH]; int ring[MAXH][FL_RING]; int n[MAXH]; } FLM;
static void tint_meter(int h) {
  float a = G.dmg_flash[h] > 0.02f ? G.dmg_flash[h] * 0.45f : 0; /* as the edges draw it */
  if (a > G.fl_tint_peak) G.fl_tint_peak = a;
  int d = a > FLM.ext[h] ? 1 : a < FLM.ext[h] ? -1 : 0;
  if (d == 0) return;
  if (FLM.dir[h] == 0) FLM.dir[h] = d; /* the first move sets the way it is going */
  if (d == FLM.dir[h]) { FLM.ext[h] = a; return; } /* further the same way: the extreme moves */
  if (fabsf(a - FLM.ext[h]) < 0.1f) return;        /* back, but not yet far enough to count */
  FLM.ring[h][FLM.n[h]++ % FL_RING] = G.steps; /* a transition: the extreme it left */
  FLM.dir[h] = d;
  FLM.ext[h] = a;
  int win = 0, k = FLM.n[h] < FL_RING ? FLM.n[h] : FL_RING;
  for (int j = 0; j < k; j++) win += G.steps - FLM.ring[h][j] < (int)(1.0f / TICK_DT);
  if (win / 2 > G.fl_tint_flashes) G.fl_tint_flashes = win / 2;
}

/* ── helpers ───────────────────────────────────────────────────────── */

static t3_vec3 eye_pos(const fighter *f) { return v3add(f->mv.position, v3(0, EYE_H, 0)); }
static t3_vec3 chest_pos(const fighter *f) { return v3add(f->mv.position, v3(0, 1.15f, 0)); }
static bool enemies(const fighter *a, const fighter *b) {
  if (a == b) return false;
  if (G.mode == MODE_DM) return true;
  return a->team != b->team;
}

static void feed(const char *fmt, const char *a, const char *b) {
  memmove(&G.feed[1], &G.feed[0], sizeof(feed_line) * (MAX_FEED - 1));
  snprintf(G.feed[0].text, sizeof G.feed[0].text, fmt, a, b);
  G.feed[0].ticks = 60 * 5;
}

/* ── Battle: the state (the rules are further down, battle_tick) ───────────────────────────────────────────────────────
 * Two sides (0: the players' side, blue; 1: the bot army, red), each with a reinforcement pool that counts respawns
 * left: a point is taken when a soldier comes back onto the field, never when one dies, and the opening wave is free.
 * At 0 nobody of that side respawns, the living fight on (the last stand), and the side loses when its last soldier
 * dies. A person downed while their side's pool is 0 takes over the nearest living teammate bot (TK below). Command
 * posts: one home post a side that cannot be taken, and capture posts between them (stand in the ring with no enemy
 * in it); a side bleeds one point every 3 s while the other holds more than half the posts. */
#define MAX_POSTS 6
#define POST_R 5.0f      /* the capture ring on today's arenas */
#define CAP_TICKS 420    /* one soldier takes a neutral post in 7 s (two: 1.5 times as fast, three or more: twice) */
typedef struct post {
  t3_vec3 pos;
  float r;        /* the ring's radius */
  char nm[16];    /* a name read from the level */
  int owner;      /* -1 neutral, else the side */
  int home;       /* -1 a capture post, else the side whose home it is (never captured) */
  float meter;    /* -1 side 0 holds it .. 0 neutral .. +1 side 1 holds it */
  int push;       /* the side in the ring alone this tick (-1 none, 2 contested) */
  int changes;    /* times it changed hands (neutral or taken) */
  const char *name;
#define POST_SEGS 20
  cw_node node, seg[POST_SEGS], flag;
  float drawn; /* the meter the look was last set for */
} post;
static const char *BSIDE_NAME[2] = { "BLUE", "RED" };
static const float BSIDE_RGB[2][3] = { { 0.3f, 0.6f, 1.0f }, { 1.0f, 0.3f, 0.22f } };
static const int POOL_CHOICES[4] = { 100, 150, 200, 300 };
static struct battle {
  int pool[2], alive[2], live[2];
  post posts[MAX_POSTS];
  int nposts;
  int bleed_t, hold_all_t[2];
  int ls_ticks[2];        /* ticks a side spent at 0 reinforcements with soldiers alive (the last stand) */
  int zero_tick[2];       /* the match tick a side first had 0 reinforcements and nobody alive (-1 not yet) */
  int pool0_tick[2];      /* the match tick a side's pool first read 0 (-1 not yet) */
  int end_tick;           /* the match tick the battle ended (-1 still on) */
  int bleeds[2], spent[2]; /* points bled, points spent on respawns */
  int ls_called[2];       /* the last stand was announced */
  int reveal_t, reveals;  /* the next reveal ping (ticks) and pings so far */
  t3_vec3 ping[MAXF];     /* where each soldier of a revealed side was at the last ping */
  int ping_t;             /* ticks since the last ping (its pulse swells and fades) */
  int ping_side;          /* the side pinged (-1 none) */
  int sw_reference;       /* sw count_rule reference: the count taken at death, spawns free while any is left, a side loses at 0 (the gates' control) */
  int sw_no_takeover;     /* sw takeover 0: a downed person at 0 only watches (the takeover gate's control) */
  int pool_start;         /* the pools the battle began with */
  int pool_setting;       /* index in POOL_CHOICES (the setup row) */
  int banner_t;           /* drawn: a call across the screen (LAST STAND, a post taken) */
  char banner[48];
  int spawn_post[MAXH];   /* the post each person respawns at (-1: the one nearest the fight) */
  int32_t d_pool[2], d_alive[2], d_owner[MAX_POSTS];
} BT;
/* each seat's soldier: seat h plays fighter SEAT[h] (h until a takeover hands it another) */
static int SEAT[MAXH] = { 0, 1, 2, 3 };
static fighter *F(int i);
static fighter *SF(int h) { return F(SEAT[h]); }
/* a takeover (per seat): 1 the respawn panel shows the pick while the view stays on the fallen body, 2 the view moves
 * to the new soldier's eyes (0.8 s, eased, a turn of at most 3.6 rad/s), 3 out of bodies: watching a teammate */
static struct { int phase, cand[4], ncand, pick, t, watch; t3_vec3 from, cam; float yaw, pitch; } TK[MAXH];
static bool battle_bot_goal(fighter *f, int idx, t3_vec3 *g);
static float tk_start[3]; /* debug tk.start: the soldier taken over, at the handover: x, z, yaw */
static float tk_after[4]; /* debug tk.after: 1.5 s after the handover: the seat 0 soldier's x, z, blue alive, the battle still on (1) */
static int32_t tk_stats[6]; /* debug tk.stats: handovers, the view's fastest turn in a transition (milliradians a second), the
                             * largest gap between view and the soldier's eye after one (mm), the match tick of the last
                             * handover, the frames a transition drew, frames watching */
/* the post of side s nearest the fight: the one it holds closest to a post it does not (its home when nothing else) */
static int post_front(int s) {
  int best = -1;
  float bd = 1e9f;
  for (int i = 0; i < BT.nposts; i++) {
    const post *p = &BT.posts[i];
    if (p->owner != s || (p->home < 0 && (p->push == !s || p->push == 2))) continue; /* never into a ring the enemy stands in */
    float d = 1e8f;
    for (int j = 0; j < BT.nposts; j++)
      if (BT.posts[j].owner != s) d = fminf(d, v3len(v3sub(BT.posts[j].pos, p->pos)));
    if (d < bd) bd = d, best = i;
  }
  return best;
}
/* a free standing spot in a post's ring: eight spots round it on the navmesh, in clear sight of the post, nobody within
 * 1.5 m; the first free one from a seeded start */
static t3_vec3 post_spot(int pi, cw_rng *rng) {
  const post *p = &BT.posts[pi];
  int k0 = cw_rng_int(rng, 0, 7);
  for (int pass = 0; pass < 2; pass++)
    for (int k = 0; k < 8; k++) {
      float sa, ca, r = (pass ? 1.5f : 2.5f) + (float)((k0 + k) & 1) * 1.2f;
      cw_sincos((float)(k0 + k) * 0.785398f, &sa, &ca);
      t3_vec3 c = v3add(p->pos, v3(sa * r, 0, ca * r)), q;
      if (!cw_nav_nearest(A.nav, c, NULL, &q) || fabsf(q.y - p->pos.y) > 1.5f) continue;
      if (arena_ray(v3add(p->pos, v3(0, 1, 0)), v3add(q, v3(0, 1, 0)), NULL, NULL)) continue;
      bool taken = false;
      for (int i = 0; i < G.nf && !taken; i++) {
        const fighter *o = F(i);
        if (!o->active || !o->alive) continue;
        float dx = o->mv.position.x - q.x, dz = o->mv.position.z - q.z;
        taken = dx * dx + dz * dz < 1.5f * 1.5f && fabsf(o->mv.position.y - q.y) < 2;
      }
      if (!taken || pass) return q;
    }
  return p->pos;
}
static t3_vec3 battle_spawn_point(const fighter *f) {
  int pi = f->human >= 0 && f->human < MAXH ? BT.spawn_post[f->human] : -1;
  if (pi < 0 || pi >= BT.nposts || BT.posts[pi].owner != f->team) pi = post_front(f->team);
  if (pi < 0) for (int i = 0; i < BT.nposts; i++) if (BT.posts[i].home == f->team) pi = i;
  return pi >= 0 ? post_spot(pi, &G.rng) : A.spawns[0];
}

/* ray against a fighter's body (a vertical cylinder): the hit distance along dir, or -1; *head set for the top part */
static float ray_fighter(const fighter *f, t3_vec3 o, t3_vec3 d, float maxt, bool *head) {
  t3_vec3 p = f->mv.position;
  float r = 0.45f, ax = o.x - p.x, az = o.z - p.z;
  float a = d.x * d.x + d.z * d.z, b = 2 * (ax * d.x + az * d.z), c = ax * ax + az * az - r * r;
  float t;
  if (a < 1e-6f) {
    if (c > 0) return -1;
    t = 0;
  } else {
    float disc = b * b - 4 * a * c;
    if (disc < 0) return -1;
    float sq = sqrtf(disc);
    t = (-b - sq) / (2 * a);
    if (t < 0) t = (-b + sq) / (2 * a);
    if (t < 0) return -1;
  }
  if (t > maxt) return -1;
  float y = o.y + d.y * t;
  if (y < p.y || y > p.y + PLAYER_HT) return -1;
  if (head) *head = y > p.y + 1.45f;
  return t;
}

/* ── damage, death, spawning ───────────────────────────────────────── */

/* a spawn point is safe when no living enemy can see it (eye to eye, 60 m) and none stands within 15 m of it. Of the safe
 * points the one farthest from the nearest enemy wins (a little jitter so two equal points share the load); a point someone
 * stands on, or that another fighter used in the last 3 s, is avoided. With no safe point, the least bad one is taken. */
static int pick_spawn(const fighter *self) {
  int best = 0;
  float best_score = -1e9f;
  for (int s = 0; s < A.nspawns; s++) {
    t3_vec3 sp = A.spawns[s], eye = v3add(sp, v3(0, EYE_H, 0));
    float md = 60;
    bool seen = false, occupied = false;
    for (int i = 0; i < G.nf; i++) {
      const fighter *o = F(i);
      if (!o->active || !o->alive || o == self) continue;
      t3_vec3 d = v3sub(o->mv.position, sp);
      float dist = sqrtf(d.x * d.x + d.z * d.z);
      if (dist < 1.5f) occupied = true;
      if (!enemies(self, o)) continue;
      if (dist < md) md = dist;
      if (!seen && dist < 60 && !arena_ray(eye_pos(o), eye, NULL, NULL)) seen = true;
    }
    float sc = md + cw_rng_float(&G.rng) * 3;
    if (seen) sc -= 100;
    if (md < 15) sc -= 50;
    if (occupied) sc -= 200;
    if (G.ticks - G.spawn_used[s] < 180) sc -= 20;
    if (sc > best_score) best_score = sc, best = s;
  }
  G.spawn_used[best] = G.ticks;
  return best;
}

static void give_loadout(fighter *f) {
  f->hp = 100;
  f->armor = 0;
  f->weapon = 0;
  f->have = 1;
  memset(f->ammo, 0, sizeof f->ammo);
  f->ammo[0] = W[0].start_ammo;
  f->cooldown = 20;
  f->grenades = 2, f->gcool = 0, f->bash_cool = 0;
  f->bash_t = f->throw_t = 1000;
  if (G.rules == RULE_INSTAGIB) f->have = 1 << 3, f->weapon = 3, f->ammo[0] = 0, f->ammo[3] = 99, f->grenades = 0; /* the rail only */
  else if (G.rules == RULE_ROCKETS) f->have = 1 << 2, f->weapon = 2, f->ammo[0] = 0, f->ammo[2] = 99;
  f->power = POW_NONE, f->power_t = 0; /* a power-up goes with its holder */
}

static void spawn_fighter(fighter *f) {
  t3_vec3 p = battle() ? battle_spawn_point(f) : A.spawns[pick_spawn(f)];
  cw_mover3d_teleport(&f->mv, p);
  f->mv.velocity = v3(0, 0, 0);
  f->yaw = cw_rng_float(&G.rng) * 6.2831853f;
  /* face the longest clear way out, leaning toward the middle: facing the middle put people nose to a wall in the corner rooms */
  {
    t3_vec3 eye = v3add(p, v3(0, EYE_H, 0)), c = v3norm(v3(-p.x, 0, -p.z));
    float best = -1e9f;
    for (int k = 0; k < 16; k++) {
      float sa, ca, yaw = (float)k * 6.2831853f / 16.0f;
      cw_sincos(yaw, &sa, &ca);
      t3_vec3 dir = v3(-sa, 0, -ca), hit;
      float open = arena_ray(eye, v3add(eye, v3scale(dir, 30)), &hit, NULL) ? v3len(v3sub(hit, eye)) : 30;
      float sc = open + 3 * v3dot(dir, c);
      if (sc > best) best = sc, f->yaw = yaw;
    }
  }
  f->pitch = 0;
  f->alive = true;
  f->dead_ticks = 0;
  f->spawn_guard = 120;
  f->retreat = 0;
  f->hold = 0;
  f->since_spawn = 0;
  f->first_dmg_tick = -1;
  f->seen_cool = 0;
  f->last_enc_tick = G.ticks;
  f->last_sight_t = -1000000;
  f->last_eng_t = f->alive_t;
  { /* measurement: who could see this spawn point, or stood within 15 m of it */
    f->spawn_seen_by = 0;
    t3_vec3 eye = v3add(p, v3(0, EYE_H, 0));
    for (int i = 0; i < G.nf; i++) {
      const fighter *o = F(i);
      if (o == f || !o->active || !o->alive || !enemies(f, o)) continue;
      t3_vec3 d = v3sub(o->mv.position, p);
      float dist = v3len(d);
      if (dist < 15 || (dist < 45 && !arena_ray(eye_pos(o), eye, NULL, NULL))) f->spawn_seen_by |= 1ull << i;
    }
    G.st.spawns++;
    if (f->spawn_seen_by) G.st.unsafe_spawns++;
  }
  f->target = -1;
  f->npath = 0;
  f->ticket = 0;
  give_loadout(f);
  if (cw_node_valid(f->body)) cw_node_set(f->body, "visible", cw_val_bool(true));
  fx_play(SND_SPAWN, p, 0.5f, 1);
}

static void kill_fighter(fighter *v, fighter *k, int weapon) {
  v->alive = false;
  v->dead_ticks = 0;
  v->deaths++;
  if (battle() && BT.sw_reference && BT.pool[v->team & 1] > 0) BT.pool[v->team & 1]--; /* the control: the count taken at death */
  if (v->power != POW_NONE) pow_stats[3]++, v->power = POW_NONE, v->power_t = 0; /* a holder downed: the power-up is gone */
  v->streak_was = v->streak;
  v->streak = 0;
  fx_spark(chest_pos(v), v3(0, 1, 0), 1);
  fx_play(SND_DIE, v->mv.position, 0.8f, 1);
  if (v->human >= 0) {
    int h = v->human;
    snprintf(G.killed_by[h], sizeof G.killed_by[0], "%s", k && k != v ? k->name : "");
    G.kc_who[h] = k && k != v ? (int)(k - G.f) : -1;
    G.kc_weapon[h] = weapon;
    G.kc_hp[h] = k ? CW_MAX(0, k->hp) : 0;
    G.kc_dist[h] = k ? v3len(v3sub(k->mv.position, v->mv.position)) : 0;
  }
  if (k && k != v) {
    G.st.kills++;
    if (weapon >= 0 && weapon < WK_KINDS) G.st.by_weapon[weapon]++;
    if (v->since_spawn < 360) G.st.quick_deaths++;
    if (v->since_spawn < 300 && (v->spawn_seen_by >> (k - G.f) & 1ull)) G.st.spawn_kills++;
    if (v->first_dmg_tick >= 0) G.st.ttk_sum += G.ticks - v->first_dmg_tick, G.st.ttk_n++;
    if (G.ticks - G.st.last_kill > G.st.gap_max) G.st.gap_max = G.ticks - G.st.last_kill;
    G.st.last_kill = G.ticks;
    k->score++;
    k->streak++;
    k->best_streak = CW_MAX(k->best_streak, k->streak);
    if (weapon == WK_GRENADE) k->gren_kills++;
    if (weapon == WK_MELEE) k->bash_kills++;
    k->multi = G.ticks - k->last_kill_t < 180 && k->last_kill_t > 0 ? k->multi + 1 : 1; /* frags within 3 s of each other */
    k->last_kill_t = G.ticks;
    if (v->streak_was >= 5) feed("%s ended %s's streak", k->name, v->name);
    else if (k->streak == 5 || k->streak == 10) feed(k->streak == 5 ? "%s: five in a row" : "%s: ten in a row", k->name, "");
    if (k->human == 0) G.p1[4]++;
    if (v->human == 0 && k->human < 0) G.p1[5]++;
    if (k->human >= 0) {
      G.kill_flash[k->human] = 1;
      snprintf(G.kill_name[k->human], sizeof G.kill_name[0], "%s", v->name);
      G.kill_streak[k->human] = k->streak;
      fx_play_ui(SND_KILL, 0.9f);
    }
    if (G.mode == MODE_TDM) G.team_score[k->team]++;
    if (k->power != POW_NONE) pow_stats[2]++;
    feed("%s fragged %s", k->name, v->name);
    (void)weapon;
  } else {
    v->score--;
    if (G.mode == MODE_TDM) G.team_score[v->team]--;
    feed("%s fell", v->name, "");
  }
}

/* ── rumble (cartwheel/input.h) ────────────────────────────────────────
 * The feel of a hit or a blast goes to the pad of the person it concerns, never into the camera. Presentation only:
 * nothing here feeds back into the game. A request on top of one still running keeps the stronger of each motor and
 * the longer of the two (a light tick never cuts a blast short); the motors always stop on their own after ms. */
static struct { int until[MAXH]; float low[MAXH], high[MAXH]; int clock; } RB;
static int32_t rumble_sent[5]; /* debug rumble.sent: requests for blasts, hits taken, hits dealt and kills, landings, heavy shots */
static void rumble(int h, float low, float high, int ms) {
  if (h < 0 || h >= MAXH || h >= G.players || G.rumble_off || ms <= 0) return;
  int now = RB.clock, left = (RB.until[h] - now) * 1000 / 60;
  if (left > 0) {
    if (low <= RB.low[h] && high <= RB.high[h] && ms <= left) return;
    low = CW_MAX(low, RB.low[h]), high = CW_MAX(high, RB.high[h]), ms = CW_MAX(ms, left);
  }
  low = cw_clampf(low, 0, 1), high = cw_clampf(high, 0, 1);
  RB.low[h] = low, RB.high[h] = high, RB.until[h] = now + (ms * 60 + 999) / 1000;
  cw_input_rumble(h, low, high, ms);
}
static void rumble_stop_all(void) {
  for (int h = 0; h < MAXH; h++) if (RB.until[h] > RB.clock) cw_input_rumble_stop(h), RB.until[h] = 0;
}

/* events that used to shake a view (the shake check below, cam.offset) */
static int32_t cam_offset[3];
static int ev_until[MAXH];
static void mark_event(int h) { if (h >= 0 && h < MAXH) ev_until[h] = G.ticks + 30; }

static void damage(fighter *v, fighter *k, int dmg, t3_vec3 dir, float knock, int weapon, bool head) {
  if (!v->alive || v->spawn_guard > 0) return;
  if (v->first_dmg_tick < 0) v->first_dmg_tick = G.ticks;
  if (k && !enemies(k, v) && k != v) return; /* no friendly fire */
  if (head) dmg = dmg * 3 / 2;
  if (G.rules == RULE_INSTAGIB && weapon == 3 && k && k != v) dmg = 999, v->armor = 0; /* instagib: a rail hit downs anyone */
  if (k && k != v && k->power == POW_QUAD) dmg *= 3; /* quad damage (three times: four ended fights before they began) */
  if (k && k->human == 0 && k != v) G.p1[1]++, G.p1[2] += dmg;
  if (k && k != v && k->human < 0 && v->human >= 0) dmg = CW_MAX(1, (int)((float)dmg * LEVELS[G.bot_level].dmg_to_people + 0.5f)); /* bots hit people softer below HARD */
  int absorbed = CW_MIN(v->armor, dmg / 2);
  v->armor -= absorbed;
  v->hp -= dmg - absorbed;
  if (v->human == 0) G.p1[3] += dmg - absorbed;
  if (knock > 0) {
    float kk = knock * (float)dmg / 40.0f;
    if (kk > 14) kk = 14;
    v->mv.velocity = v3add(v->mv.velocity, v3(dir.x * kk, dir.y * kk * 0.6f + (v->mv.on_floor ? kk * 0.3f : 0), dir.z * kk));
  }
  v->flinch = CW_MIN(1.0f, v->flinch + 0.35f + (float)dmg * 0.02f); /* the body is thrown back a moment (drawn only) */
  /* drawn only: the blow's way and weight for the body's hit reaction and its fall (soldier.c) */
  v->hit_dir = v3scale(v3norm(dir), CW_MIN(1.5f, (knock > 0 ? knock : 1) * (float)dmg / 60.0f)), v->hit_head = head;
  v->hit_t = 0;
  if (v->human >= 0) {
    G.dmg_want[v->human] = 1;
    G.dmg_hold[v->human] = 36;
    if (k) {
      t3_vec3 d = v3sub(k->mv.position, v->mv.position);
      G.dmg_dir[v->human] = cw_atan2(-d.x, -d.z) - v->yaw;
    }
    G.dmg_taken[v->human] += dmg - absorbed / 2;
    mark_event(v->human);
    fx_play_ui(SND_HURT, 0.5f);
  }
  if (k && k->human >= 0 && k != v) {
    G.hit_marker[k->human] = 1;
    if (head) G.head_marker[k->human] = 1;
    G.dmg_dealt[k->human] += dmg;
    if (v->hp <= 0) G.kills_now[k->human]++;
    else fx_play_ui(head ? SND_HEAD : SND_TICK, head ? 0.75f : 0.6f); /* a kill has its own sound (kill_fighter) */
  }
  if (v->hp <= 0) kill_fighter(v, k, weapon);
}

/* ── shooting ──────────────────────────────────────────────────────── */

/* bullet magnetism (pad aim assist, on by default, off on the setup screen): a person's hitscan shot that passes close to an
 * enemy in clear sight bends onto it. The cone is the body's own angular half width plus a margin (about 0.9 degrees at
 * 20 m, wider up close), out to 40 m, so a near miss lands and a clear miss stays a miss; the console shooter idea of
 * "auto-aim" behind the reticle, separate from the reticle's slowdown and pull (cw_aim_step and the magnetism in
 * human_tick). Returns the yaw and pitch to add to this shot. */
static float assist_cone = 1.0f; /* ARENA_BULLET_ASSIST (autopilot): scale for measuring, 0 = off */
/* the firing check (gate arenafps-recoil): ticks person 1 held fire with no look input, and those on which the view moved: a
 * gun's kick belongs to the gun model (ARENA_RECOIL_BUG=1, the control, climbs the view 6 milliradians a shot) */
static int32_t cam_fire[2];
static void bullet_assist(const fighter *f, int self, float *ay, float *ap) {
  *ay = *ap = 0;
  if (f->human < 0 || !G.aim_assist || assist_cone <= 0) return;
  float by = f->yaw + (f->weapon == 0 ? f->recoil_y : 0), bp = f->pitch + (f->weapon == 0 ? f->recoil_p : 0);
  float best = 1e9f;
  t3_vec3 e = eye_pos(f);
  for (int i = 0; i < G.nf; i++) {
    const fighter *o = F(i);
    if (i == self || !o->active || !o->alive || !enemies(f, o)) continue;
    t3_vec3 c = chest_pos(o), to = v3sub(c, e);
    float dist = v3len(to);
    if (dist > 40 || dist < 0.5f) continue;
    float hd = sqrtf(to.x * to.x + to.z * to.z);
    float dy = cw_wrap_angle(cw_atan2(-to.x, -to.z) - by), dp = cw_atan2(to.y, hd) - bp;
    float body = atanf(0.45f / dist), cone = (body * 1.6f + 0.006f) * assist_cone;
    float a = sqrtf(dy * dy + dp * dp * 0.5f);
    if (a > cone || a >= best || arena_ray(e, c, NULL, NULL)) continue;
    best = a;
    /* onto the edge of the body nearest the shot, not its centre: the assist saves a near miss, it does not aim */
    float k = a > body * 0.6f ? (a - body * 0.6f) / a : 0;
    *ay = dy * k, *ap = dp * k * 0.7f;
  }
}
static t3_vec3 spread_dir(const fighter *f, float spread, float ay, float ap) {
  /* the rifle burst walks the shots off the crosshair (never the view: the camera is the aim, the kick is the gun model) */
  t3_vec3 d = f->weapon == 0 ? look_dir(f->yaw + f->recoil_y + ay, f->pitch + f->recoil_p + ap) : look_dir(f->yaw + ay, f->pitch + ap);
  if (spread <= 0) return d;
  t3_vec3 right = v3norm(v3cross(d, v3(0, 1, 0))), up = v3cross(right, d);
  float a = cw_rng_range(&G.rng, -1, 1), b = cw_rng_range(&G.rng, -1, 1);
  while (a * a + b * b > 1) {
    a = cw_rng_range(&G.rng, -1, 1);
    b = cw_rng_range(&G.rng, -1, 1);
  }
  return v3norm(v3add(d, v3add(v3scale(right, a * spread), v3scale(up, b * spread))));
}

/* one hitscan ray from the muzzle; rail pierces fighters */
static void trace(fighter *f, int self_i, t3_vec3 o, t3_vec3 d, const weapon *w, bool pierce) {
  const float RANGE = 120;
  t3_vec3 wp, wn;
  float tw = RANGE;
  bool hit_world = arena_ray(o, v3add(o, v3scale(d, RANGE)), &wp, &wn);
  if (hit_world) tw = v3len(v3sub(wp, o));
  float best = tw;
  int hits[MAXF], nh = 0;
  float ts[MAXF];
  bool heads[MAXF];
  for (int i = 0; i < G.nf; i++) {
    fighter *o2 = F(i);
    if (i == self_i || !o2->active || !o2->alive) continue;
    bool head = false;
    float t = ray_fighter(o2, o, d, tw, &head);
    if (t < 0) continue;
    hits[nh] = i, ts[nh] = t, heads[nh] = head, nh++;
  }
  /* nearest first */
  for (int a = 0; a < nh; a++)
    for (int b = a + 1; b < nh; b++)
      if (ts[b] < ts[a]) {
        int ti = hits[a]; hits[a] = hits[b]; hits[b] = ti;
        float tt = ts[a]; ts[a] = ts[b]; ts[b] = tt;
        bool th = heads[a]; heads[a] = heads[b]; heads[b] = th;
      }
  int stop = pierce ? nh : (nh > 0 ? 1 : 0);
  for (int k = 0; k < stop; k++) {
    fighter *v = F(hits[k]);
    t3_vec3 hp = v3add(o, v3scale(d, ts[k]));
    if (!enemies(f, v)) { if (!pierce) { best = ts[k]; hit_world = false; } continue; }
    fx_spark(hp, v3scale(d, -1), 1);
    damage(v, f, w->damage, d, w->knock, f->weapon, heads[k]);
    if (!pierce) { best = ts[k]; hit_world = false; }
  }
  t3_vec3 end = v3add(o, v3scale(d, pierce ? tw : best));
  if (hit_world && (pierce || best == tw)) {
    fx_spark(wp, wn, 0);
    if (w->kind == 0 && w->pellets == 1) fx_hole(wp, wn);
    else if (cw_rng_int(&G.rng, 0, 2) == 0) fx_hole(wp, wn);
  }
  { /* the tracer starts ahead of the gun (from the muzzle it is a bright box in the shooter's face) */
    float tl = v3len(v3sub(end, o));
    t3_vec3 ts = v3add(o, v3add(v3(0, -0.12f, 0), v3scale(d, CW_MIN(2.2f, tl * 0.4f))));
    if (w->kind == 2) fx_tracer(v3add(o, v3add(v3(0, -0.12f, 0), v3scale(d, CW_MIN(1.0f, tl * 0.3f)))), end, 1);
    else if (w->pellets == 1 || cw_rng_int(&G.rng, 0, 3) == 0) fx_tracer(ts, end, 0);
  }
}

static void fire_rocket(fighter *f, int owner) {
  for (int i = 0; i < MAXP; i++) {
    proj *p = &G.pr[i];
    if (p->on) continue;
    t3_vec3 d = look_dir(f->yaw, f->pitch);
    p->on = true;
    p->owner = owner;
    p->p = v3add(eye_pos(f), v3add(v3scale(d, 0.6f), v3(0, -0.15f, 0)));
    p->v = v3scale(d, 24);
    p->life = 60 * 5;
    cw_node_set(p->node, "visible", cw_val_bool(true));
    cw_node_set(p->trail, "emitting", cw_val_bool(true));
    /* the light swells over four ticks (rocket_tick): switched on at full at the barrel it lit a wall at arm's length from
     * dark to bright in one frame */
    p->glow = strobefix() ? 0.0f : 1.0f;
    cw_node_set(p->light, "energy", cw_val_float(14 * p->glow));
    cw_node3d_set_position(p->node, p->p);
    return;
  }
}

/* a smoke and flame trail behind a rocket (continuous, world space), part of the rocket node */
static cw_node rocket_trail(void) {
  cw_node n = cw_node_new("GPUParticles3D", "Trail");
  cw_node_set(n, "amount", cw_val_int(48));
  cw_node_set(n, "lifetime", cw_val_float(0.9));
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "direction", cw_val_vec3(0, 0, 1));
  cw_node_set(n, "spread", cw_val_float(12));
  cw_node_set(n, "speed_min", cw_val_float(0.4));
  cw_node_set(n, "speed_max", cw_val_float(1.2));
  cw_node_set(n, "gravity", cw_val_vec3(0, 0.6f, 0));
  cw_node_set(n, "size", cw_val_float(0.34));
  cw_node_set(n, "size_random", cw_val_float(0.3));
  cw_node_set(n, "color_ramp", cw_val_string("0 0.9 0.8 0.6 0.5, 0.15 0.45 0.42 0.4 0.5, 1 0.2 0.2 0.2 0"));
  cw_node_set(n, "size_curve", cw_val_string("0 0.4, 1 2.2"));
  cw_node_set(n, "draw_mode", cw_val_enum("BILLBOARD"));
  cw_node_set(n, "blend", cw_val_enum("ALPHA"));
  cw_node3d_set_position(n, v3(0, 0, 0.35f));
  return n;
}

static void blast(t3_vec3 at, t3_vec3 n, int owner_i, int max_dmg, float radius, int weapon);
static void explode(proj *p, t3_vec3 at, t3_vec3 n) {
  p->on = false;
  cw_node_set(p->node, "visible", cw_val_bool(false));
  cw_node_set(p->trail, "emitting", cw_val_bool(false));
  cw_node_set(p->light, "energy", cw_val_float(0));
  cw_node3d_set_position(p->node, v3(0, -500, 0));
  cw_node3d_set_position(p->light, v3(0, -500, 0));
  blast(at, n, p->owner, W[2].damage, 5.0f, 2);
}

/* ── grenades: thrown in an arc, bounce, go off 2.5 s after the throw (the second verb: flush a corner) ── */
#define MAXGREN 12
#define GREN_FUSE 150
typedef struct gren { bool on, rest; t3_vec3 p, v; int owner, fuse; cw_node node, light; } gren;
static gren GR[MAXGREN];
static int32_t gren_stats[3]; /* debug gren.stats: thrown, gone off, fighters hurt by them */
/* ARENA_NO_VERBS=1 (autopilot): nobody throws or bashes, the before of the verbs' measure */
static bool no_verbs(void) {
#ifdef ARENA_AUTOPILOT
  static int off = -1;
  if (off < 0) off = getenv("ARENA_NO_VERBS") != NULL;
  return off;
#else
  return false;
#endif
}
static void throw_grenade(fighter *f, int idx, t3_vec3 vel) {
  if (f->grenades <= 0 || f->gcool > 0 || !f->alive || no_verbs()) return;
  for (int i = 0; i < MAXGREN; i++) {
    gren *g = &GR[i];
    if (g->on) continue;
    t3_vec3 d = look_dir(f->yaw, f->pitch), right = v3norm(v3cross(d, v3(0, 1, 0)));
    g->on = true, g->rest = false;
    g->owner = idx;
    g->fuse = GREN_FUSE;
    g->p = v3add(eye_pos(f), v3add(v3scale(d, 0.45f), v3add(v3scale(right, -0.15f), v3(0, -0.1f, 0))));
    if (arena_ray(eye_pos(f), g->p, NULL, NULL)) g->p = eye_pos(f); /* against a wall: it leaves from the eye, never through */
    g->v = vel;
    f->grenades--;
    f->gcool = 36;
    f->throw_t = 0;
    f->spawn_guard = 0;
    gren_stats[0]++;
    if (cw_node_valid(g->node)) cw_node_set(g->node, "visible", cw_val_bool(true)), cw_node3d_set_position(g->node, g->p);
    if (cw_node_valid(g->light)) cw_node_set(g->light, "energy", cw_val_float(1.5f));
    fx_play(SND_THROW, g->p, 0.6f, 1);
    return;
  }
}
/* a person's throw: along the view, a little up, with some of the thrower's own speed */
static t3_vec3 throw_vel(const fighter *f) {
  t3_vec3 d = look_dir(f->yaw, f->pitch);
  return v3add(v3add(v3scale(d, 15.0f), v3(0, 3.2f, 0)), v3scale(v3(f->mv.velocity.x, 0, f->mv.velocity.z), 0.4f));
}
/* a bot's throw to land on a point: flight time from the distance, the lob that reaches it */
static t3_vec3 lob_vel(t3_vec3 from, t3_vec3 to) {
  t3_vec3 d = v3sub(to, from);
  float hd = sqrtf(d.x * d.x + d.z * d.z), T = cw_clampf(hd / 13.0f, 0.45f, 1.4f);
  return v3(d.x / T, (d.y + 0.5f * 20.0f * T * T) / T, d.z / T);
}
static void gren_tick(void) {
  for (int i = 0; i < MAXGREN; i++) {
    gren *g = &GR[i];
    if (!g->on) continue;
    if (--g->fuse <= 0) {
      g->on = false;
      if (cw_node_valid(g->node)) cw_node_set(g->node, "visible", cw_val_bool(false)), cw_node3d_set_position(g->node, v3(0, -500, 0));
      if (cw_node_valid(g->light)) cw_node_set(g->light, "energy", cw_val_float(0));
      gren_stats[1]++;
      blast(v3add(g->p, v3(0, 0.15f, 0)), v3(0, 1, 0), g->owner, 125, 6.0f, WK_GRENADE); /* kills at its feet, softens to 6 m */
      continue;
    }
    if (!g->rest) {
      g->v.y -= 20.0f * grav_k() * TICK_DT;
      t3_vec3 np = v3add(g->p, v3scale(g->v, TICK_DT)), hp, hn;
      if (arena_ray(g->p, np, &hp, &hn)) {
        float vn = v3dot(g->v, hn);
        t3_vec3 vt = v3sub(g->v, v3scale(hn, vn));
        g->v = v3add(v3scale(vt, 0.7f), v3scale(hn, -vn * 0.4f)); /* a dull bounce, it skids */
        g->p = v3add(hp, v3scale(hn, 0.07f));
        if (fabsf(vn) > 2.5f) fx_play(SND_BOUNCE, g->p, CW_MIN(0.6f, fabsf(vn) * 0.06f), 1);
        if (hn.y > 0.7f && g->fuse > 32) g->fuse = 32; /* on the floor: it goes off about half a second later (in the air it waits up to 2.5 s) */
        if (hn.y > 0.7f && v3len(g->v) < 1.2f) g->rest = true, g->v = v3(0, 0, 0);
      } else g->p = np;
    }
    if (cw_node_valid(g->node)) {
      cw_node3d_set_position(g->node, g->p);
      cw_node3d_set_rotation(g->node, v3((float)g->fuse * 0.3f, (float)g->fuse * 0.17f, 0));
    }
    if (cw_node_valid(g->light)) cw_node3d_set_position(g->light, v3add(g->p, v3(0, 0.12f, 0)));
  }
}

/* ── melee: a bash with the gun's stock, close up and in front (the third verb: finish a fight at arm's length) ── */
static int32_t bash_stats[2]; /* debug bash.stats: bashes swung, bashes that landed */
static void bash(fighter *f, int idx) {
  if (f->bash_cool > 0 || !f->alive || no_verbs()) return;
  f->bash_cool = 40;
  f->bash_t = 0;
  f->spawn_guard = 0;
  bash_stats[0]++;
  t3_vec3 e = eye_pos(f), d = look_dir(f->yaw, 0);
  fighter *best = NULL;
  float bd = 2.3f;
  for (int i = 0; i < G.nf; i++) {
    fighter *o = F(i);
    if (i == idx || !o->active || !o->alive || !enemies(f, o)) continue;
    t3_vec3 to = v3sub(o->mv.position, f->mv.position);
    float hd = sqrtf(to.x * to.x + to.z * to.z);
    if (hd > bd || fabsf(to.y) > 1.4f) continue;
    if (hd > 0.3f && (to.x * d.x + to.z * d.z) / hd < 0.55f) continue; /* in front, about 55 degrees either side */
    if (arena_ray(e, chest_pos(o), NULL, NULL)) continue;
    bd = hd, best = o;
  }
  if (best) {
    bash_stats[1]++;
    t3_vec3 dir = v3norm(v3(d.x, 0.25f, d.z));
    fx_spark(chest_pos(best), v3scale(d, -1), 1);
    fx_play(SND_BASH, chest_pos(best), 0.9f, 1);
    if (f->human >= 0) rumble(f->human, 0.45f, 0.35f, 90);
    damage(best, f, 55, dir, 5.0f, WK_MELEE, false);
  } else fx_play(SND_THROW, e, 0.35f, 1.3f); /* a swing at nothing: a whoosh */
}

static void blast(t3_vec3 at, t3_vec3 n, int owner_i, int max_dmg, float radius, int weapon) {
  /* a blast at a player's eye lights less: its flash light at full lit the wall a rocket hit at arm's length from dark to
   * white and back, three rockets a second a strobe in that view (the flash sweep's match run, 4 flashes in a second).
   * A quarter of the light within 3 m of an eye, full from 12 m */
  float near_eye = 1e9f;
  for (int h = 0; h < G.players; h++) near_eye = fminf(near_eye, v3len(v3sub(eye_pos(SF(h)), at)));
  float lk = CW_CLAMP((near_eye - 3.0f) / 9.0f, 0, 1);
  fx_explosion(at, n, 0.25f + 0.75f * lk * lk * (3 - 2 * lk));
  for (int h = 0; h < G.players; h++) {
    float d = v3len(v3sub(eye_pos(SF(h)), at));
    if (d >= 14.0f) continue;
    mark_event(h);
    /* a blast: the heavy motor, strong up close and fading to nothing 14 m out */
    float k = (1 - d / 14.0f) * (1 - d / 14.0f);
    if (SF(h)->alive) rumble_sent[0]++, rumble(h, 0.25f + 0.75f * k, 0.25f * k, 120 + (int)(260 * k));
  }
  fighter *owner = F(owner_i);
  const float RADIUS = radius;
  for (int i = 0; i < G.nf; i++) {
    fighter *v = F(i);
    if (!v->active || !v->alive) continue;
    t3_vec3 c = chest_pos(v), d = v3sub(c, at);
    float dist = v3len(d);
    if (dist > RADIUS) continue;
    float k = 1 - dist / RADIUS;
    if (weapon == WK_GRENADE) {
      /* a grenade: full force within 1.6 m (one at the feet reaches the chest), then a falloff that stays strong to about
       * half the radius and fades at the edge, so one that lands by a wounded fighter finishes them */
      float x = cw_clampf((dist - 1.6f) / (RADIUS - 1.6f), 0, 1);
      k = 1 - x * x;
    }
    if (dist > 0.8f && arena_ray(at, c, NULL, NULL)) k *= 0.4f; /* behind cover */
    int dmg = (int)((float)max_dmg * k);
    if (dmg < 4) continue;
    if (weapon == WK_GRENADE && v != owner && enemies(owner, v)) gren_stats[2]++;
    t3_vec3 dir = dist > 0.01f ? v3scale(d, 1 / dist) : v3(0, 1, 0);
    dir.y += 0.35f;
    dir = v3norm(dir);
    float knock = (v == owner) ? 10.0f : 7.0f;
    if (v == owner) dmg = dmg / 2; /* self damage is halved: rocket jumps are fair */
    int was = v->spawn_guard;
    if (v == owner) v->spawn_guard = 0;
    damage(v, owner, dmg, dir, knock * 40.0f / (float)CW_MAX(dmg, 20), weapon, false);
    if (v == owner) v->spawn_guard = was;
  }
}

static void do_fire(fighter *f, int idx) {
  const weapon *w = &W[f->weapon];
  if (f->cooldown > 0 || !f->alive) return;
  if (f->ammo[f->weapon] <= 0) {
    f->cooldown = 12;
    if (f->human >= 0) fx_play_ui(SND_CLICK, 0.6f);
    /* out: fall back to the rifle or the best weapon with ammo */
    for (int k = NWEAP - 1; k >= 0; k--)
      if ((f->have >> k & 1) && f->ammo[k] > 0) { f->weapon = k; break; }
    return;
  }
  if (G.rules != RULE_INSTAGIB && G.rules != RULE_ROCKETS) f->ammo[f->weapon]--; /* the one-weapon rules never run dry */
  f->cooldown = f->power == POW_HASTE ? w->cooldown * 2 / 3 : w->cooldown; /* haste: a third quicker on the trigger */
  if (f->human == 0) G.p1[0]++;
  f->spawn_guard = 0; /* firing ends spawn protection */
  if (f->weapon == 0) {
    /* the rifle burst walks its shots in a fixed pattern (up, then wandering left and right) and settles after it: short
     * bursts stay on the crosshair. The view does not move (no camera kick); the crosshair opens with the walk.
     * Pitch up is positive. Units: milliradians, half the old view climb */
    static const signed char PAT[12][2] = { { 0, 12 }, { 2, 11 }, { -2, 10 }, { 3, 10 }, { -3, 9 }, { 4, 8 },
                                            { -1, 8 }, { 3, 7 }, { -4, 7 }, { 2, 6 }, { -3, 6 }, { 3, 6 } };
    int k = CW_MIN(f->recoil_n, 11);
    float dp = (float)PAT[k][1] * 0.0005f, dy = (float)PAT[k][0] * 0.0005f;
    f->recoil_p = CW_MIN(f->recoil_p + dp, 0.03f), f->recoil_y = cw_clampf(f->recoil_y + dy, -0.01f, 0.01f);
    {
      static int bug = -1; /* a gate switch: read from frame 2 on every host (cw_test_env) */
      if (bug < 0 && cw_frame_index() > 0) { const char *v = cw_test_env("ARENA_RECOIL_BUG"); bug = v && atoi(v); }
      if (bug > 0) f->pitch += 0.006f;
    }
    f->recoil_n++;
  }
  f->recoil_t = 0;
  f->flash = 3;
  f->kick += w->kind == 0 && w->pellets == 1 ? 0.012f : w->kind == 2 ? 0.06f : 0.05f;
  t3_vec3 e = eye_pos(f);
  t3_vec3 d0 = look_dir(f->yaw, f->pitch);
  t3_vec3 right = v3norm(v3cross(d0, v3(0, 1, 0)));
  t3_vec3 muzzle = v3add(e, v3add(v3scale(d0, 0.5f), v3scale(right, 0.12f)));
  if (f->human >= 0 && w->kind >= 1) rumble_sent[4]++;
  if (f->human >= 0 && w->kind == 1) rumble(f->human, 0.3f, 0.15f, 70);      /* a rocket leaves: a light thump */
  if (f->human >= 0 && w->kind == 2) rumble(f->human, 0.12f, 0.4f, 55);      /* the rail: a sharp tick */
  if (w->kind == 1) {
    fire_rocket(f, idx);
    fx_play(SND_ROCKET, e, 0.9f, 1);
  } else {
    float ay, ap;
    bullet_assist(f, idx, &ay, &ap);
    for (int k = 0; k < w->pellets; k++) {
      t3_vec3 d = spread_dir(f, w->spread, ay, ap);
      trace(f, idx, k == 0 ? muzzle : muzzle, d, w, w->kind == 2);
    }
    fx_play(f->weapon == 0 ? SND_RIFLE : f->weapon == 1 ? SND_SCATTER : SND_RAIL, e, 0.8f, 0.97f + cw_rng_float(&G.rng) * 0.06f);
    /* a person's own casing leaves the gun's port, low on the right of the view: from the eye's muzzle point it filled the screen */
    if (f->weapon <= 1) fx_casing(f->human >= 0 ? v3add(e, v3add(v3add(v3scale(d0, 0.62f), v3scale(right, 0.22f)), v3(0, -0.24f, 0))) : muzzle, right, v3(0, 1, 0));
  }
  if (cw_node_valid(f->light)) {
    /* a wall lit by the shot, not a white-out of the view: the light swells and fades on an envelope (muzzle_light),
     * and a burst holds it up instead of switching it on and off with every shot */
    f->mpeak = fmaxf(f->mhold > 0 ? f->mpeak : 0, f->weapon == 1 ? 16 : 9);
    f->mhold = 9;
    if (f->mglow < 0.05f) f->mreach = 0, f->mnear = 0.5f; /* a fresh shot: the light is placed from the barrel out (muzzle_place) */
  }
}

/* ── movement ──────────────────────────────────────────────────────── */

/* jump 8.8 m/s against 24 m/s2: a 1.6 m apex, so a 1.2 m step or ledge is a jump (FUN-RUBRIC: the Terraces ledges); sprint
 * 9.4 m/s forward only and never while firing (a burst ends it) */
static void move_fighter(fighter *f, float fwd, float strafe, bool jump, bool sprint) {
  const float GROUND_ACCEL = 14.0f, AIR_ACCEL = 2.4f, FRICTION = 7.0f, GRAV = 24.0f, JUMP = 8.8f;
  f->sprint = sprint && fwd > 0.5f && f->cooldown == 0 && f->recoil_t > 12;
  const float MAXSPEED = (f->sprint ? 9.4f : 7.2f) * (f->power == POW_HASTE ? 1.3f : 1.0f); /* haste: 30 percent quicker */
  float sy, cy;
  cw_sincos(f->yaw, &sy, &cy);
  t3_vec3 fw = v3(-sy, 0, -cy), rt = v3(cy, 0, -sy);
  t3_vec3 wish = v3add(v3scale(fw, fwd), v3scale(rt, strafe));
  float wl = v3len(wish);
  if (wl > 1) wish = v3scale(wish, 1 / wl), wl = 1;
  t3_vec3 wd = wl > 0.001f ? v3scale(wish, 1 / wl) : v3(0, 0, 0);
  float wishspeed = wl * MAXSPEED;
  t3_vec3 *vel = &f->mv.velocity;
  bool ground = f->mv.on_floor && f->launched <= 0;
  if (f->launched > 0) f->launched--;
  if (ground)
    for (int i = 0; i < A.npads; i++) { /* a jump pad under the feet: launched toward its target */
      const pad *pd = &A.pads[i];
      float dx = f->mv.position.x - pd->pos.x, dz = f->mv.position.z - pd->pos.z, dy = f->mv.position.y - pd->pos.y;
      if (dx * dx + dz * dz > 0.9f * 0.9f || dy < -0.3f || dy > 0.6f) continue;
      *vel = G.rules == RULE_LOWGRAV ? v3scale(pd->vel, sqrtf(LOWG_K)) : pd->vel; /* the same arc, slower, under low gravity */
      f->launched = 12;
      /* a bot whose path crosses this pad: on to the landing */
      if (f->npath > 0 && f->pidx + 1 < f->npath && f->plink[f->pidx]) {
        f->pidx++;
        G.link_start[LINK_PAD]++;
        G.link_cross[LINK_PAD]++;
        f->counted_leg = f->pidx + 1000 * f->npath;
      }
      f->mv.on_floor = false;
      fx_play(SND_PAD, pd->pos, 0.8f, 1);
      ground = false;
      break;
    }
  if (ground) {
    float sp = sqrtf(vel->x * vel->x + vel->z * vel->z);
    if (sp > 0.01f) {
      float drop = (sp < 1.5f ? 1.5f : sp) * FRICTION * TICK_DT;
      float ns = CW_MAX(sp - drop, 0) / sp;
      vel->x *= ns, vel->z *= ns;
    }
    vel->y = 0;
    if (jump) vel->y = G.rules == RULE_LOWGRAV ? JUMP * 0.8f : JUMP;
  } else {
    vel->y -= GRAV * grav_k() * TICK_DT;
  }
  float accel = ground ? GROUND_ACCEL : AIR_ACCEL;
  float cur = vel->x * wd.x + vel->z * wd.z;
  float add = wishspeed - cur;
  if (add > 0 && wl > 0) {
    float as = accel * wishspeed * TICK_DT;
    if (as > add) as = add;
    vel->x += wd.x * as;
    vel->z += wd.z * as;
  }
  bool was_air = !f->mv.on_floor;
  float vy0 = f->mv.velocity.y;
  /* no fighter stands inside another (its body would fill that one's view from inside the near plane): two closer than
   * MIN_SEP across, at heights that overlap, are each moved apart by half the shortfall this tick, through the mover (walls
   * still hold), and the push is not kept in the velocity */
  t3_vec3 push = { 0, 0, 0 };
  if (f->alive && strobefix()) {
    const float MIN_SEP = 1.1f;
    for (int i = 0; i < G.nf; i++) {
      const fighter *o = F(i);
      if (o == f || !o->active || !o->alive) continue;
      float dx = f->mv.position.x - o->mv.position.x, dz = f->mv.position.z - o->mv.position.z;
      if (fabsf(f->mv.position.y - o->mv.position.y) > PLAYER_HT) continue;
      float d2 = dx * dx + dz * dz;
      if (d2 >= MIN_SEP * MIN_SEP) continue;
      float d = sqrtf(d2);
      if (d < 1e-4f) dx = (f < o) ? 1.0f : -1.0f, dz = 0, d = 1; /* one on top of the other: apart along x, by slot */
      float k = (MIN_SEP - sqrtf(d2)) * 0.5f / TICK_DT / d;
      push.x += dx * k, push.z += dz * k;
    }
    vel->x += push.x, vel->z += push.z;
  }
  cw_mover3d_move(&f->mv, TICK_DT);
  vel->x -= push.x, vel->z -= push.z;
  if (was_air && f->mv.on_floor && vy0 < -4.0f) { /* a landing: a thud, and a light bump in the pad (harder for a longer fall) */
    if (f->human >= 0) rumble_sent[3]++, rumble(f->human, CW_MIN(0.45f, 0.1f + (-vy0 - 4.0f) * 0.025f), 0, 60 + (int)CW_MIN(60.0f, (-vy0 - 4.0f) * 6.0f));
    if (f->human >= 0) fx_play_ui(SND_LAND, CW_MIN(0.7f, 0.25f - vy0 * 0.02f));
    else fx_play(SND_LAND, f->mv.position, CW_MIN(0.8f, 0.3f - vy0 * 0.02f), 1);
  }
  float sp = sqrtf(f->mv.velocity.x * f->mv.velocity.x + f->mv.velocity.z * f->mv.velocity.z);
  if (f->mv.on_floor) f->bob += sp * TICK_DT * 1.1f;
  if (f->mv.position.y < A.kill_y) {
    fighter *k = NULL;
    damage(f, k, 1000, v3(0, 1, 0), 0, 0, false);
  }
}

/* ── bots ──────────────────────────────────────────────────────────── */

static void bot_set_goal(fighter *f, t3_vec3 g) {
  f->goal = g;
  f->repath = 0;
}

/* The link the bot is crossing toward waypoint pidx (its tag; 0 when that leg is walking). */
static int bot_leg_link(const fighter *f) {
#ifdef CW_NAV_LINKS
  if (f->pidx <= 0 || f->pidx >= f->npath || !f->plink[f->pidx - 1]) return 0;
  cw_nav_link l;
  return cw_nav_link_get(A.nav, f->plink[f->pidx - 1], &l) ? (int)l.tag : 0;
#else
  (void)f;
  return 0;
#endif
}

static void bot_follow(fighter *f, float *fwd, float *strafe, float speed) {
  t3_vec3 p = f->mv.position;
  if (f->npath > 0 && f->pidx < f->npath) {
    t3_vec3 w = f->path[f->pidx];
    float dx = w.x - p.x, dz = w.z - p.z, d = sqrtf(dx * dx + dz * dz);
    int link = bot_leg_link(f);
    /* a link's far end counts as reached only at its height (below a ledge is not on it); a pad is stepped on at its middle */
    bool pad_next = false;
    if (f->pidx + 1 < f->npath && f->plink[f->pidx]) {
      f->pidx++;
      pad_next = bot_leg_link(f) == LINK_PAD;
      f->pidx--;
    }
    /* (a landing is not precise: a link's far end is reached within 2 m once at its height) */
    bool there = link ? d < 2.0f && fabsf(w.y - p.y) < 0.8f : d < (pad_next ? 0.4f : 0.9f);
    /* a crossing counts once it is at the far end's height near it */
    if (link > 0 && link < 4 && f->counted_leg != f->pidx + 1000 * f->npath && fabsf(w.y - p.y) < 0.8f && d < 2.5f) {
      G.link_cross[link]++;
      f->counted_leg = f->pidx + 1000 * f->npath;
    }
    if (there && f->pidx + 1 < f->npath) {
      f->pidx++;
      int next = bot_leg_link(f);
      if (next > 0 && next < 4) G.link_start[next]++;
      return bot_follow(f, fwd, strafe, speed);
    }
    if (link == LINK_JUMP && w.y > p.y + 0.5f && f->mv.on_floor) {
      /* up a ledge: jump from the foot of it, running at the top */
      t3_vec3 s = f->path[f->pidx - 1];
      float sx = s.x - p.x, sz = s.z - p.z;
      if (sx * sx + sz * sz < 1.3f * 1.3f) f->link_jump = true;
    }
    if (d > 0.01f) {
      /* move toward the waypoint in world space; convert to the body frame */
      float sy, cy;
      cw_sincos(f->yaw, &sy, &cy);
      float wx = dx / d, wz = dz / d;
      *fwd += (-sy * wx - cy * wz) * speed;
      *strafe += (cy * wx - sy * wz) * speed;
    }
  }
}

static void bot_think(fighter *f, int idx) {
  /* target: the nearest enemy that can be seen */
  int best = -1;
  float bd = 1e9f;
  t3_vec3 e = eye_pos(f);
  for (int i = 0; i < G.nf; i++) {
    fighter *o = F(i);
    if (!o->active || !o->alive || !enemies(f, o) || o->spawn_guard > 0) continue; /* a fighter under spawn protection is left alone */
    t3_vec3 d = v3sub(o->mv.position, f->mv.position);
    float dist = v3len(d);
    if (dist > 45) continue;
    /* a seat's stand-in stays on the enemy it is fighting while it can see it, as a person does, unless another comes
     * in at well under its range: picking the nearest at every think swung its view between two enemies at full stick
     * five times a second (the flash sweep's arenafps-coop). ARENA_T_strobefix=0 (autopilot): the nearest, the control */
    if (i == f->target && proxy(f) && strobefix()) dist *= 0.6f;
    if (dist > bd) continue;
    if (arena_ray(e, chest_pos(o), NULL, NULL)) continue;
    { /* noticing: in the vision cone (wider up close), or all round when just hit or hearing a shot; the current target is kept */
      float fov = bot_fov_override >= 0 ? bot_fov_override : LEVELS[G.bot_level].fov;
      if (i != f->target && fov < 180 && f->hit_t > 120 && !(o->recoil_t < 30 && dist < 30)) {
        float half = fov * 0.0174533f;
        if (dist < 6) half = CW_MAX(half, 1.5708f);
        float dyaw = cw_wrap_angle(cw_atan2(-d.x, -d.z) - f->yaw);
        if (fabsf(dyaw) > half) continue;
      }
    }
    bd = dist, best = i;
  }
  if (best >= 0) {
    if (f->target != best || (!LEVELS[G.bot_level].legacy && f->alive_t - f->saw_t > 30)) { /* a new enemy, or one back in sight after half a second hidden */
      const bot_level_def *L = &LEVELS[G.bot_level];
      f->react = proxy(f) ? 18 : (int)(L->react + (1 - f->skill) * L->react_span);
      if (F(best)->since_spawn < 180 && !LEVELS[G.bot_level].legacy) f->react += 12; /* a fighter that has just respawned gets a moment */
    }
    f->target = best;
    f->saw_t = f->alive_t;
    f->last_pos = F(best)->mv.position;
    f->wander_wait = 0;
  } else if (f->target >= 0 && (!F(f->target)->alive || F(f->target)->spawn_guard > 0)) {
    f->target = -1;
  }
  /* goal: fight, or fetch something */
  bool see = best >= 0;
  if (see) f->hold = 0;
  else if (f->hold == 0 && v3len(v3sub(f->goal, f->mv.position)) < 2.0f && cw_rng_int(&G.rng, 0, 1)) {
    /* at a goal with nobody in sight: hold the spot a moment and look round, as a person does, before the next errand */
    f->hold = f->personality == 1 ? 20 : f->personality == 2 ? 120 + cw_rng_int(&G.rng, 0, 180) : 60 + cw_rng_int(&G.rng, 0, 120);
  }
  if (f->hp >= 60) f->retreat = 0;
  t3_vec3 bg;
  if (!see && battle() && battle_bot_goal(f, idx, &bg)) { /* Battle: the posts (battle_bot_goal) */
    if (v3len(v3sub(bg, f->goal)) > 0.5f) bot_set_goal(f, bg);
  } else if (!see) {
    float bs = 0;
    int bi = -1;
    for (int i = 0; i < A.nitems; i++) {
      item *it = &A.items[i];
      bool power = it->kind == ITEM_ROCKET || it->kind == ITEM_RAIL || it->kind == ITEM_ARMOR;
      /* item timing: a power item due back within 5 s is worth walking to (holders and balanced bots watch the clock,
       * rushers do not); anything else that is not there is not */
      if (it->respawn > 0 && !(power && f->personality != 1 && it->respawn < 60 * 5)) continue;
      float need = 1;
      switch (it->kind) {
        case ITEM_HEALTH: need = f->hp < 80 ? 4.0f : 0.3f; break;
        case ITEM_ARMOR: need = f->armor < 60 ? 3.0f : 0.3f; break;
        case ITEM_AMMO: need = f->ammo[0] < 80 ? 2.0f : 0.5f; break;
        case ITEM_SCATTER: need = (f->have & 2) ? 0.6f : 3.0f; break;
        case ITEM_ROCKET: need = (f->have & 4) ? 0.8f : 4.0f; break;
        case ITEM_RAIL: need = (f->have & 8) ? 0.8f : 4.0f; break;
      }
      if (f->personality == 2 && power) need *= 1.6f; /* holders build their game round the power items */
      if (it->respawn > 0) need *= 0.8f;
      t3_vec3 d = v3sub(it->pos, f->mv.position);
      float dist = sqrtf(d.x * d.x + d.z * d.z) + fabsf(d.y) * 2;
      if (it->respawn > 0 && dist > 30) continue;
      float s = need / (6 + dist);
      if (s > bs) bs = s, bi = i;
    }
    if (G.mode != MODE_COOP && (PW.present || (PW.timer < 60 * 5 && f->personality != 1))) {
      /* the power-up: worth more than anything on the map, and holders and balanced bots are there as it comes back */
      t3_vec3 d = v3sub(PW.pos, f->mv.position);
      float dist = sqrtf(d.x * d.x + d.z * d.z) + fabsf(d.y) * 2, s = (f->personality == 2 ? 7.0f : 5.0f) * (PW.present ? 1 : 0.8f) / (6 + dist);
      if (s > bs) bs = s, bi = -4;
    }
    if (f->last_pos.x != 0 || f->last_pos.z != 0) {
      if (f->target < 0 && f->wander_wait++ < 240 && bi < 0 && bi != -4) bi = -2;
    }
    /* hunt: co-op bots (both sides) go after the nearest enemy, and any bot that has seen no enemy for 20 s (12 s in team
     * deathmatch, where half the fighters are friends and a big arena went quiet for 45 s) goes looking
     * (the longest stretch without an encounter stays short); and when nobody at all has scored for 20 s (two sides
     * trading shots across a hall without closing), every bot pushes in. Only health when hurt comes first */
    bool stall = G.ticks - G.st.last_kill > 20 * 60;
    if ((G.mode == MODE_COOP || stall || f->alive_t - f->saw_t > (G.mode == MODE_TDM ? 12 : 20) * 60) && !(bi >= 0 && A.items[bi].kind == ITEM_HEALTH && f->hp < 50)) {
      float hd = 1e9f;
      int hi = -1;
      for (int i = 0; i < G.nf; i++) {
        const fighter *o = F(i);
        if (!o->active || !o->alive || !enemies(f, o) || o->spawn_guard > 0) continue;
        float d = v3len(v3sub(o->mv.position, f->mv.position));
        if (d < hd) hd = d, hi = i;
      }
      if (hi >= 0) bi = -3, f->hunt = F(hi)->mv.position;
    }
    t3_vec3 g = f->goal;
    if (bi == -3) g = f->hunt;
    else if (bi == -4) g = PW.pos;
    else if (bi >= 0) g = A.items[bi].pos;
    else if (bi == -2) g = f->last_pos;
    else if (v3len(v3sub(f->goal, f->mv.position)) < 2.5f || f->goal.y < -100) {
      t3_vec3 rp;
      cw_rng *r = &G.rng;
      /* roam to a random point, but not into a spawn area: bots that wander onto spawn points spawn-kill */
      for (int tries = 0; tries < 6; tries++) {
        if (!cw_nav_random_point(A.nav, NULL, r, &rp)) break;
        g = rp;
        bool near_spawn = false;
        for (int s = 0; s < A.nspawns && !near_spawn; s++) {
          t3_vec3 d = v3sub(rp, A.spawns[s]);
          near_spawn = d.x * d.x + d.z * d.z < 9 * 9;
        }
        if (near_spawn) continue;
        if (f->personality == 2 && rp.y < A.top - 0.5f && tries < 5) continue; /* holders roam the high ground */
        break;
      }
    }
    if (v3len(v3sub(g, f->goal)) > 0.5f) bot_set_goal(f, g);
  } else if (f->retreat == 0 && f->personality != 1 && f->hp < 70 && f->hit_t < 20 && cw_rng_int(&G.rng, 0, 2) == 0) {
    /* cover: just hit and not healthy, a balanced bot or a holder steps to the nearest spot the enemy cannot see (eight spots
     * round it, 4.5 m out, on the navmesh), waits a moment there and comes back out */
    const fighter *en = F(best);
    t3_vec3 ee = eye_pos(en), bp = v3(0, 0, 0);
    float bd2 = 1e9f;
    for (int k = 0; k < 8; k++) {
      float sa, ca;
      cw_sincos((float)k * 0.785398f + (float)(idx & 3) * 0.2f, &sa, &ca);
      t3_vec3 c = v3add(f->mv.position, v3(sa * 4.5f, 0, ca * 4.5f)), q;
      if (!cw_nav_nearest(A.nav, c, NULL, &q) || fabsf(q.y - f->mv.position.y) > 1.0f) continue;
      if (!arena_ray(ee, v3add(q, v3(0, 1.15f, 0)), NULL, NULL)) continue; /* still in the open */
      float dd = v3len(v3sub(q, f->mv.position));
      if (dd < bd2) bd2 = dd, bp = q;
    }
    if (bd2 < 1e8f) {
      bot_set_goal(f, bp);
      f->retreat = 45 + cw_rng_int(&G.rng, 0, 45);
    }
  } else if (f->hp < (f->personality == 1 ? 25 : f->personality == 2 ? 50 : 40) && f->retreat == 0 && !(battle() && BT.pool[f->team & 1] == 0)) { /* (a last stand never runs) */
    /* hurt in a fight: break off toward the nearest health (away from the enemy when there is none), as players do; a fight
     * that ends this way is not a kill, so lives last longer and encounters spread out */
    int hi = -1;
    float hd = 1e9f;
    for (int i = 0; i < A.nitems; i++) {
      const item *it = &A.items[i];
      if (it->kind != ITEM_HEALTH || it->respawn > 0) continue;
      float d = v3len(v3sub(it->pos, f->mv.position));
      if (d < hd) hd = d, hi = i;
    }
    t3_vec3 away = v3add(f->mv.position, v3scale(v3norm(v3sub(f->mv.position, F(best)->mv.position)), 12));
    bot_set_goal(f, hi >= 0 ? A.items[hi].pos : away);
    f->retreat = 150;
  } else if (f->retreat == 0) {
    /* in a fight: hold a preferred range along the path toward the enemy when far */
    fighter *t = F(f->target);
    float dist = v3len(v3sub(t->mv.position, f->mv.position));
    float pref = W[f->weapon].range_pref * (f->personality == 1 ? 0.6f : f->personality == 2 ? 1.3f : 1.0f);
    if (dist > pref * 1.6f) bot_set_goal(f, t->mv.position);
  }
  (void)idx;
}

static void bot_weapon_choice(fighter *f, float dist) {
  int want = 0;
  float best = -1;
  for (int k = 0; k < NWEAP; k++) {
    if (!(f->have >> k & 1) || f->ammo[k] <= 0) continue;
    float s = 1;
    if (k == 1) s = dist < 9 ? 3 : 0.5f;
    if (k == 2) s = dist > 6 ? 2.5f : 0.7f;
    if (k == 3) s = (dist > 14 ? 3.5f : 1.5f) * (f->personality == 2 ? 1.4f : 1.0f);
    if (k == 0) s = 1.2f;
    if (s > best) best = s, want = k;
  }
  if (want != f->weapon && f->cooldown < 10) f->weapon = want;
}

/* a turn toward an aim dy (yaw) and dp (pitch) radians away. A bot turns at its rate. A seat's stand-in holds a pad: it
 * pushes the look stick by how far it has to go (full past about 15 degrees) through the game's own stick curve
 * (dead zone, response, the held edge's boost), so its view eases in and out of a turn as a thumb does instead of
 * starting, stopping and reversing at full speed from one tick to the next (that bang-bang view, two of them side by
 * side in split screen, swung the walls and the sky through the same part of the screen five times a second: the flash
 * sweep's arenafps-coop). ARENA_T_strobefix=0 (autopilot): the old full-rate turns, the flash sweep's control */
static cw_aim_state proxy_aim[MAXH];
static cw_vec2 proxy_thumb[MAXH];
static int32_t DBG_SEAT[MAXH][2]; /* autopilot: a stand-in's branch this tick (1 retreat, 2 fight, 3 hold, 4 errand) and target */
static void turn_by(fighter *f, float dy, float dp, float rate) {
  if (proxy(f) && strobefix() && f->human < MAXH) {
    const cw_aim_curve c = { .deadzone = 0.15f, .power = 1.8f, .yaw_rate = 3.6f, .pitch_rate = 2.4f, .ramp_seconds = 0.35f,
                             .ramp_gain = 0.6f, .ramp_edge = 0.9f, .assist = 0 };
    cw_vec2 want = cw_v2(-cw_clampf(dy * 3.8f, -1, 1), -cw_clampf(dp * 3.8f, -1, 1));
    /* a thumb takes a few ticks to cross the stick: from one edge to the other in about an eighth of a second */
    cw_vec2 *th = &proxy_thumb[f->human];
    th->x += cw_clampf(want.x - th->x, -0.25f, 0.25f), th->y += cw_clampf(want.y - th->y, -0.25f, 0.25f);
    cw_vec2 stick = *th;
    float l = sqrtf(stick.x * stick.x + stick.y * stick.y);
    if (l > 1) stick = cw_v2(stick.x / l, stick.y / l);
    else if (l > 1e-4f) { /* past the dead zone from the first push, as a thumb that means to turn does */
      float k = (0.15f + 0.85f * l) / l;
      stick = cw_v2(stick.x * k, stick.y * k);
    }
    cw_vec2 t = cw_aim_step(&proxy_aim[f->human], &c, stick, TICK_DT, 0);
    f->yaw -= fabsf(t.x) > fabsf(dy) ? -dy : t.x; /* never past the aim */
    f->pitch -= fabsf(t.y) > fabsf(dp) ? -dp : t.y;
    return;
  }
  f->yaw += cw_clampf(dy, -rate, rate);
  f->pitch += cw_clampf(dp, -rate, rate);
}

static void bot_tick(fighter *f, int idx) {
  float fwd = 0, strafe = 0;
  bool jump = false;
  if (--f->think <= 0) {
    f->think = 10 + (idx & 3);
    bot_think(f, idx);
  }
  /* no new path in the air or halfway across a link (a path planned from mid-air starts wherever the air is over, and
   * one planned from the near end walks back to it); the un-sticking below still forces one */
  bool crossing = !f->mv.on_floor || (bot_leg_link(f) && f->stuck == 0);
  /* path requests: one outstanding per bot; the tick budget answers them */
  if (f->ticket) {
    t3_vec3 pts[24];
    int cnt = 0;
    uint32_t links[24] = { 0 };
#ifdef CW_NAV_LINKS
    cw_nav_path_status st = cw_nav_take_path_links(A.nav, f->ticket, pts, links, 24, &cnt);
#else
    cw_nav_path_status st = cw_nav_take_path(A.nav, f->ticket, pts, 24, &cnt);
#endif
    if (st == CW_NAV_PATH_DONE || st == CW_NAV_PATH_PARTIAL || st == CW_NAV_PATH_FAILED) {
      f->ticket = 0;
      G.path_done++;
      if (crossing) cnt = -1; /* asked for before the crossing began: the crossing's path stands */
      if (cnt > 1) {
        memcpy(f->path, pts, sizeof(t3_vec3) * (size_t)cnt);
        memcpy(f->plink, links, sizeof(uint32_t) * (size_t)cnt);
        for (int k = 0; k < cnt; k++) G.path_links += links[k] != 0;
        f->npath = cnt;
        f->pidx = 1;
      } else if (cnt >= 0) {
        f->npath = 0;
      }
    }
  }
  if (!f->ticket && --f->repath <= 0 && !crossing) {
    f->repath = 50 + (idx * 7) % 20;
    f->ticket = cw_nav_request_path(A.nav, f->mv.position, f->goal, NULL);
    G.path_requests++;
  }
  fighter *t = f->target >= 0 && F(f->target)->alive && F(f->target)->spawn_guard <= 0 ? F(f->target) : NULL;
  bool see = false;
  f->sprint_want = !t && (f->personality == 1 || v3len(v3sub(f->goal, f->mv.position)) > 14) && !proxy(f);
  float dist = 0;
  t3_vec3 aim_at = v3(0, 0, 0);
  if (t) {
    t3_vec3 e = eye_pos(f);
    aim_at = chest_pos(t);
    see = !arena_ray(e, aim_at, NULL, NULL);
    dist = v3len(v3sub(t->mv.position, f->mv.position));
    if (see) {
      bot_weapon_choice(f, dist);
      if (f->weapon == 2) aim_at = v3add(t->mv.position, v3(0, 0.2f, 0)); /* rockets at the feet */
      /* lead the target for projectiles */
      if (f->weapon == 2) {
        float tt = dist / 24.0f * f->skill;
        aim_at = v3add(aim_at, v3scale(v3(t->mv.velocity.x, 0, t->mv.velocity.z), tt));
      }
    }
  }
  /* the other verbs: a bash at arm's length; a grenade at a target that has just gone behind cover (most of the time) or
   * one standing in the open at mid range (now and then), lobbed to land near it, closer the better the bot */
  if (t && see && dist < 1.9f && f->bash_cool == 0 && f->react <= 0) bash(f, idx);
  else if (t && f->grenades > 0 && f->gcool == 0 && f->think == 1) {
    bool hid = !see && f->alive_t - f->saw_t < 45 && dist > 5 && dist < 18;
    bool open = see && f->react <= 0 && dist > 7 && dist < 18 && v3len(v3(t->mv.velocity.x, 0, t->mv.velocity.z)) < 2.0f;
    /* a wounded target is the grenade's best use: one that ducked behind cover hurt (or stands hurt in the open) gets one
     * most of the time, to finish the fight */
    bool hurt = t->hp + CW_MIN(t->armor, t->hp) <= 70;
    int roll = cw_rng_int(&G.rng, 0, 99);
    if ((hid && roll < (hurt ? 60 : 35)) || (open && roll < (hurt ? 25 : 14))) {
      float miss = (1.2f - f->skill) * 2.5f, a = cw_rng_float(&G.rng) * 6.2831853f, sa, ca;
      cw_sincos(a, &sa, &ca);
      t3_vec3 from = eye_pos(f), lead = v3scale(v3(t->mv.velocity.x, 0, t->mv.velocity.z), cw_clampf(dist / 13.0f, 0.45f, 1.4f) * 0.8f * f->skill);
      t3_vec3 to = v3add(v3add(t->mv.position, lead), v3(sa * miss, 0.1f, ca * miss)); /* where it will be, about */
      /* a bot faces the throw at once; a seat's stand-in (a pad player) cannot snap its view round, so it throws as it
       * faces (the lob still goes where it aimed). ARENA_T_strobefix=0 (autopilot): the snap, the flash sweep's control */
      if (!proxy(f) || !strobefix()) f->yaw = cw_atan2(-(to.x - from.x), -(to.z - from.z));
      throw_grenade(f, idx, lob_vel(from, to));
    }
  }
  if (f->retreat > 0) f->retreat--;
  if (proxy(f) && f->human < MAXH) DBG_SEAT[f->human][0] = see && f->retreat > 0 ? 1 : see ? 2 : f->hold > 0 ? 3 : 4, DBG_SEAT[f->human][1] = f->target;
  if (see && f->retreat > 0) {
    /* breaking off: run the path, firing only when the enemy happens to be in front */
    bot_follow(f, &fwd, &strafe, 1.0f);
    if (f->npath > 0 && f->pidx < f->npath) {
      t3_vec3 w = f->path[f->pidx];
      float ty = cw_atan2(-(w.x - f->mv.position.x), -(w.z - f->mv.position.z));
      if (proxy(f)) turn_by(f, cw_wrap_angle(ty - f->yaw), 0, 5.0f * TICK_DT);
      else f->yaw += cw_clampf(cw_wrap_angle(ty - f->yaw), -5.0f * TICK_DT, 5.0f * TICK_DT);
    }
  } else if (see) {
    /* strafe while fighting, close or open the range */
    if (--f->strafe_t <= 0) {
      f->strafe_t = 30 + cw_rng_int(&G.rng, 0, 50);
      f->strafe_dir = cw_rng_int(&G.rng, 0, 1) ? 1 : -1;
    }
    float pref = W[f->weapon].range_pref * (f->personality == 1 ? 0.6f : f->personality == 2 ? 1.3f : 1.0f);
    strafe = (float)f->strafe_dir * (0.5f + 0.5f * f->skill);
    if (dist > pref * 1.3f) bot_follow(f, &fwd, &strafe, 1.0f);
    else if (dist < pref * 0.6f) fwd = -0.7f;
    if (((idx * 13 + G.ticks) % 97) == 0 && f->mv.on_floor) jump = true;
    /* aim: turn toward the target, with an error that settles with skill */
    t3_vec3 d = v3sub(aim_at, eye_pos(f));
    float hd = sqrtf(d.x * d.x + d.z * d.z);
    if (G.ticks % 24 == idx % 24) {
      /* the error grows with distance and with the target's and the bot's own speed (a person misses movers more) */
      float tsp = v3len(v3(t->mv.velocity.x, 0, t->mv.velocity.z)), osp = v3len(v3(f->mv.velocity.x, 0, f->mv.velocity.z));
      float e = (1 - f->skill * 0.8f) * (proxy(f) ? (G.aim_assist ? 0.7f : 1.0f) : LEVELS[G.bot_level].err);
      if (!LEVELS[G.bot_level].legacy || proxy(f)) e *= 1 + dist * 0.02f + tsp * 0.08f + osp * 0.05f;
      f->aim_err_yaw = cw_rng_range(&G.rng, -1, 1) * e * 0.06f;
      f->aim_err_pitch = cw_rng_range(&G.rng, -1, 1) * e * 0.04f;
    }
    float ty = cw_atan2(-d.x, -d.z) + f->aim_err_yaw, tp = cw_atan2(d.y, hd) + f->aim_err_pitch;
    float rate = (proxy(f) ? 3.6f : LEVELS[G.bot_level].turn + f->skill * LEVELS[G.bot_level].turn_skill) * TICK_DT;
    float dy = cw_wrap_angle(ty - f->yaw), dp = tp - f->pitch;
    turn_by(f, dy, dp, rate);
    if (f->react > 0) f->react--;
    else if (fabsf(dy) < 0.06f + (1 - f->skill) * 0.06f && f->cooldown == 0) do_fire(f, idx);
  } else if (f->hold > 0) {
    f->hold--;
    f->yaw += 1.2f * TICK_DT * ((idx & 1) ? 1 : -1);
  } else {
    bot_follow(f, &fwd, &strafe, 1.0f);
    /* look where we go */
    if (f->npath > 0 && f->pidx < f->npath) {
      t3_vec3 w = f->path[f->pidx];
      if (proxy(f) && strobefix()) { /* a person looks down the way, not at the next corner at their feet: the first point
                                      * of the path 4 m off (the last one if none is), so passing a corner does not swing the view */
        for (int k = f->pidx; k < f->npath; k++) {
          w = f->path[k];
          float wx = w.x - f->mv.position.x, wz = w.z - f->mv.position.z;
          if (wx * wx + wz * wz > 16.0f) break;
        }
      }
      float ty = cw_atan2(-(w.x - f->mv.position.x), -(w.z - f->mv.position.z));
      float dy = cw_wrap_angle(ty - f->yaw), rate = 5.0f * TICK_DT;
      if (proxy(f) && strobefix()) turn_by(f, dy, -f->pitch, rate);
      else {
        f->yaw += cw_clampf(dy, -rate, rate);
        f->pitch += cw_clampf(-f->pitch, -0.05f, 0.05f);
      }
    }
  }
  /* a live grenade close by: get away from it (after a moment to notice) */
  for (int g = 0; g < MAXGREN; g++) {
    const gren *gr = &GR[g];
    if (!gr->on || gr->fuse > GREN_FUSE - 30) continue;
    t3_vec3 d = v3sub(f->mv.position, gr->p);
    float dd = sqrtf(d.x * d.x + d.z * d.z);
    if (dd > 5.5f || dd < 1e-3f) continue;
    /* only one it can see in front of it (about 60 degrees either side, in the clear) or hear close by, as a person would:
     * a fighter running away hurt does not see the one lobbed after it */
    if (dd > 2.5f) {
      float sy, cy;
      cw_sincos(f->yaw, &sy, &cy);
      if ((-sy * -d.x + -cy * -d.z) / dd < 0.5f || arena_ray(eye_pos(f), gr->p, NULL, NULL)) continue;
    }
    float sy, cy, k = 2.0f * (5.5f - dd) / 5.5f + 0.8f;
    cw_sincos(f->yaw, &sy, &cy);
    float wx = d.x / dd * k, wz = d.z / dd * k;
    fwd += -sy * wx - cy * wz;
    strafe += cy * wx - sy * wz;
  }
  /* separation from other fighters, and un-sticking */
  for (int i = 0; i < G.nf; i++) {
    fighter *o = F(i);
    if (o == f || !o->active || !o->alive) continue;
    t3_vec3 d = v3sub(f->mv.position, o->mv.position);
    float dd = d.x * d.x + d.z * d.z;
    if (dd < 1.6f * 1.6f && dd > 1e-4f) {
      float l = sqrtf(dd), k = (1.6f - l) / 1.6f;
      float sy, cy;
      cw_sincos(f->yaw, &sy, &cy);
      float wx = d.x / l * k, wz = d.z / l * k;
      fwd += -sy * wx - cy * wz;
      strafe += cy * wx - sy * wz;
    }
  }
  if (G.ticks % 30 == idx % 30) {
    t3_vec3 d = v3sub(f->mv.position, f->last_pos);
    (void)d;
  }
  float moved = v3len(v3sub(f->mv.position, f->last_pos));
  if (G.ticks % 45 == idx % 45) {
    if (moved < 0.5f && f->npath > 0 && !see) { jump = true; f->repath = 0; f->stuck++; }
    f->last_pos = f->mv.position;
    if (f->stuck > 3) { f->stuck = 0; f->goal = v3(0, -1000, 0); f->think = 0; }
  }
  if (f->link_jump) jump = true, G.link_jumps++;
  f->link_jump = false;
  move_fighter(f, cw_clampf(fwd, -1, 1), cw_clampf(strafe, -1, 1), jump, f->sprint_want);
}

/* ── playtest perception (debug field ptest, command `ptest on|off`) ─────────────────────────────────────────────────
 * What a person sees and knows, for a scripted human (tools/playtest-human.mjs) that plays player 1 with the pad through
 * romdev: the own view, the enemies on screen (in the view's frame, in clear sight), the damage indicator, and a walking
 * route (a person knows the map; the script is about aim, reactions and fights, not finding the way). Read only: nothing
 * here feeds the simulation (its own random stream; synchronous path queries do not touch the bots' request queue). */
#define PT_HEAD 32
#define PT_N (PT_HEAD + MAXF * 4)
static float ptest[PT_N];
static struct { bool on; t3_vec3 goal, path[32]; uint32_t links[32]; int n, idx, repath, lastitem; uint32_t rng; } PT;
typedef struct hm_profile { float react_min, react_span, tau, noise, perc, fire_tol, strafe_p; bool jumps, swaps, sprints; } hm_profile;
static const hm_profile HM_PROF[3] = {
  { 20, 11, 0.32f, 0.25f, 0.030f, 1.8f, 0.5f, false, false, false }, /* casual */
  { 13, 5, 0.16f, 0.10f, 0.012f, 1.2f, 1.0f, true, true, true },     /* skilled */
  { 24, 14, 0.42f, 0.45f, 0.075f, 4.0f, 0.3f, false, false, false }, /* newcomer: sprays early and wide, misses most of its shots, the check that casual's near-perfect trigger discipline does not flatter the tuning */
};
static struct { int kind; /* 0 off, 1 casual, 2 skilled */ int tgt, react_until, burst, strafe_t, strafe_dir, dmg_t, swap_cool;
                float ey, ep; bool strafing; float out[8]; bool drive; int lost_t; } HM;
static float pt_rand(void) { PT.rng = PT.rng * 1664525u + 1013904223u; return (float)(PT.rng >> 8) / 16777216.0f; }
static void ptest_goal(const fighter *f) {
  /* the nearest useful item that is there (weapons not held, health when hurt, armour), else a random point */
  float bs = 0;
  int bi = -1;
  for (int i = 0; i < A.nitems; i++) {
    const item *it = &A.items[i];
    if (it->respawn > 0 || i == PT.lastitem) continue;
    float need = 0.4f;
    switch (it->kind) {
      case ITEM_HEALTH: need = f->hp < 70 ? 3.0f : 0.1f; break;
      case ITEM_ARMOR: need = f->armor < 50 ? 1.5f : 0.1f; break;
      case ITEM_AMMO: need = f->ammo[0] < 60 ? 1.5f : 0.1f; break;
      case ITEM_SCATTER: need = (f->have & 2) ? 0.2f : 1.2f; break;
      case ITEM_ROCKET: need = (f->have & 4) ? 0.3f : 2.0f; break;
      case ITEM_RAIL: need = (f->have & 8) ? 0.3f : 1.5f; break;
    }
    float d = v3len(v3sub(it->pos, f->mv.position));
    float s = need / (8 + d) * (0.6f + 0.8f * pt_rand());
    if (s > bs) bs = s, bi = i;
  }
  if (bi >= 0 && bs > 0.03f) { PT.goal = A.items[bi].pos; PT.lastitem = bi; return; }
  cw_rng r;
  cw_rng_seed(&r, PT.rng);
  pt_rand();
  t3_vec3 rp;
  if (cw_nav_random_point(A.nav, NULL, &r, &rp)) PT.goal = rp;
}
static void hm_decide(void);
static void ptest_route(fighter *f);
static void ptest_update(void) {
  memset(ptest, 0, sizeof ptest);
  ptest[0] = (float)G.state;
  if (!PT.on || G.state != ST_PLAY || G.nf == 0) return;
  fighter *f = F(0);
  if (f->human != 0) return;
  ptest[1] = f->alive, ptest[2] = f->yaw, ptest[3] = f->pitch;
  ptest[4] = f->mv.position.x, ptest[5] = f->mv.position.y, ptest[6] = f->mv.position.z;
  ptest[7] = (float)f->hp, ptest[8] = (float)f->armor, ptest[9] = (float)f->weapon, ptest[10] = (float)f->ammo[f->weapon];
  ptest[11] = (float)f->score;
  int best = -9999;
  for (int i = 1; i < G.nf; i++) if (F(i)->active && F(i)->score > best) best = F(i)->score;
  ptest[12] = (float)best;
  ptest[17] = (float)G.match_ticks, ptest[18] = (float)f->deaths, ptest[19] = G.sw_no_cues ? 0 : G.dmg_flash[0], ptest[20] = G.sw_no_cues ? 0 : G.dmg_dir[0]; /* the bearing bar, as drawn */
  ptest[21] = (float)f->have, ptest[22] = (float)G.frag_limit;
  /* the view's half angles: 78 degrees tall, wide by the view's aspect */
  cw_rect vr = cw_shell_player_rect(0);
  float th = tanf(78.0f * 0.5f * 3.14159265f / 180.0f), aspect = vr.h > 1 ? vr.w / vr.h : 16.0f / 9.0f;
  ptest[16] = atanf(th * aspect);
  t3_vec3 e = eye_pos(f), d = look_dir(f->yaw, f->pitch);
  t3_vec3 rt = v3norm(v3cross(d, v3(0, 1, 0))), up = v3cross(rt, d);
  for (int i = 1; i < G.nf; i++) {
    const fighter *o = F(i);
    float *s = &ptest[PT_HEAD + i * 4];
    if (!o->active || !o->alive || !enemies(f, o) || !f->alive) continue;
    t3_vec3 c = chest_pos(o), rel = v3sub(c, e);
    float dist = v3len(rel), z = v3dot(rel, d);
    if (dist > 60 || z < 0.3f) continue;
    float hx = atanf(v3dot(rel, rt) / z), hy = atanf(v3dot(rel, up) / z);
    if (fabsf(hx) > ptest[16] || fabsf(hy) > 78.0f * 0.5f * 3.14159265f / 180.0f) continue;
    if (arena_ray(e, c, NULL, NULL) && arena_ray(e, v3add(o->mv.position, v3(0, 1.65f, 0)), NULL, NULL)) continue;
    float hd = sqrtf(rel.x * rel.x + rel.z * rel.z);
    s[0] = 1, s[1] = cw_wrap_angle(cw_atan2(-rel.x, -rel.z) - f->yaw), s[2] = cw_atan2(rel.y, hd) - f->pitch, s[3] = dist;
  }
  ptest_route(f);
  if (HM.kind && G.ticks % 6 == 0) hm_decide();
  memcpy(&ptest[24], HM.out, sizeof HM.out);
#ifdef ARENA_AUTOPILOT
  { /* ARENA_HUMAN_TRACE=file: the scripted human's view of the match, twice a second (tuning the model) */
    static FILE *tf;
    static int tried;
    if (!tried) { tried = 1; const char *p = getenv("ARENA_HUMAN_TRACE"); if (p) tf = fopen(p, "w"); }
    if (tf && G.ticks % 30 == 0) {
      int vis = 0;
      for (int i = 1; i < MAXF; i++) vis += ptest[PT_HEAD + i * 4] > 0.5f;
      fprintf(tf, "%d al%d pos %.1f %.1f %.1f yaw %.2f pit %.2f hp %d rt %.2f %.1f n%d i%d vis %d tgt %d out %.2f %.2f %.2f %.2f f%.0f j%.0f dmg %.2f\n", G.ticks, f->alive,
              f->mv.position.x, f->mv.position.y, f->mv.position.z, f->yaw, f->pitch, f->hp, ptest[13], ptest[14], PT.n, PT.idx, vis, HM.tgt,
              HM.out[0], HM.out[1], HM.out[2], HM.out[3], HM.out[4], HM.out[5], ptest[19]);
      fflush(tf);
    }
  }
#endif
}
static void ptest_route(fighter *f) {
  /* the route */
  if (!f->alive) { PT.n = 0; return; }
  bool reached = PT.n > 0 && v3len(v3sub(PT.goal, f->mv.position)) < 1.5f;
  if (PT.n == 0 || reached || --PT.repath <= 0) {
    if (PT.n == 0 || reached || PT.goal.y < -100) ptest_goal(f);
    PT.repath = 45;
    if (!f->mv.on_floor) PT.repath = 5;
    else {
#ifdef CW_NAV_LINKS
      PT.n = cw_nav_find_path_links(A.nav, f->mv.position, PT.goal, NULL, PT.path, PT.links, 32, NULL);
#else
      PT.n = cw_nav_find_path(A.nav, f->mv.position, PT.goal, NULL, PT.path, 32, NULL);
      memset(PT.links, 0, sizeof PT.links);
#endif
      PT.idx = 1;
      if (PT.n <= 1) { PT.n = 0; PT.goal = v3(0, -1000, 0); }
    }
  }
  while (PT.n > 0 && PT.idx < PT.n - 1) {
    t3_vec3 w = PT.path[PT.idx];
    float dx = w.x - f->mv.position.x, dz = w.z - f->mv.position.z;
    if (dx * dx + dz * dz < 0.8f * 0.8f && fabsf(w.y - f->mv.position.y) < 1.0f) PT.idx++;
    else break;
  }
  if (PT.n > 0 && PT.idx < PT.n) {
    t3_vec3 w = PT.path[PT.idx];
    float dx = w.x - f->mv.position.x, dz = w.z - f->mv.position.z;
    ptest[13] = cw_wrap_angle(cw_atan2(-dx, -dz) - f->yaw);
    ptest[14] = sqrtf(dx * dx + dz * dz);
    ptest[15] = (PT.links[PT.idx - 1] != 0 && w.y > f->mv.position.y + 0.5f) ? 1.0f : 0.0f; /* a ledge to jump up */
    ptest[23] = w.y - f->mv.position.y;
  }
}
/* ── the scripted human (debug command `ptest casual|skilled`, autopilot ARENA_HUMAN=casual|skilled) ───────────────────
 * A pad player modelled from the perception above only (what is on screen, the damage indicator, a route), decided every
 * 6 ticks (100 ms) like a person's eye-hand loop, with a reaction time before the first aim at a newly seen enemy, an aim
 * error that comes back every decision (perception and motor noise: the stick is pushed by how far off the crosshair is,
 * through the game's real stick curve, so it overshoots and settles), fire held only while the crosshair is near the
 * target, strafing in fights, and a look toward the damage indicator. Its pad state is written to ptest[24..31]
 * (lx, ly, rx, ry, fire, jump, sprint, next weapon); tools/playtest-human.mjs presses exactly that on romdev's pad, the
 * autopilot feeds it to the same human_tick. Presentation only: it never touches the simulation's state or random stream.
 * Profiles (sources in findings/arenafps.md, "A-PT1 human pass"):
 *   casual: a newcomer on a pad. 330 to 520 ms to start aiming at someone new (human choice reaction in games is about
 *     250 ms simple and 400 ms+ with target acquisition), slow and noisy stick (25 percent motor noise, a 0.32 s settle),
 *     fires once within about 1.8x the body's angular radius, strafes in half the fights, never jumps in a fight, never
 *     changes weapons by hand, does not sprint.
 *   skilled: a practised pad player: 220 to 300 ms, 10 percent noise, a 0.16 s settle, tighter fire, always strafes,
 *     jumps now and then, picks the weapon for the range, sprints between fights. */
static bool hm_driving(const fighter *f) { return HM.drive && f->human == 0; }
static float hm_gauss(void) { float a = pt_rand(), b = pt_rand(); return sqrtf(-2.0f * logf(a + 1e-7f)) * cosf(6.2831853f * b); }
/* the stick deflection that turns at w rad/s through the game's look curve (dead zone 0.15, power 1.8) */
static float hm_stick(float w, float full) {
  float n = fabsf(w) / full;
  if (n < 0.003f) return 0;
  float s = 0.15f + 0.85f * powf(CW_MIN(n, 1.0f), 1.0f / 1.8f);
  return w < 0 ? -s : s;
}
static void hm_decide(void) {
  float *o = HM.out;
  memset(o, 0, sizeof HM.out);
  if (!HM.kind || !ptest[1]) { HM.tgt = -1; return; }
  const hm_profile *P = &HM_PROF[HM.kind - 1];
  int t = G.ticks;
  /* the target: keep the one being fought while it stays on screen, else the nearest to the crosshair */
  int best = -1;
  float bs = 1e9f;
  for (int i = 1; i < MAXF; i++) {
    const float *s = &ptest[PT_HEAD + i * 4];
    if (s[0] < 0.5f) continue;
    float a = fabsf(s[1]) + fabsf(s[2]) + s[3] * 0.004f;
    if (i == HM.tgt) a *= 0.3f;
    if (a < bs) bs = a, best = i;
  }
  if (best != HM.tgt) {
    if (best < 0 && HM.tgt >= 0) HM.lost_t = t + 42;
    HM.tgt = best;
    if (best >= 0) {
      HM.react_until = t + (int)(P->react_min + pt_rand() * P->react_span);
      HM.strafing = pt_rand() < P->strafe_p;
      HM.burst = 0;
    }
  }
  float sy_full = 3.6f, sp_full = 2.4f;
  if (best >= 0) {
    const float *s = &ptest[PT_HEAD + best * 4];
    float dist = s[3];
    /* perception: the target's centre is judged with an error that grows with distance; refreshed every decision */
    float ang = atanf(0.45f / CW_MAX(dist, 1.0f)); /* the body's angular half width */
    HM.ey = s[1] + hm_gauss() * (P->perc + ang * 0.25f);
    HM.ep = s[2] + hm_gauss() * (P->perc * 0.7f + ang * 0.15f);
    if (t >= HM.react_until) {
      float wy = HM.ey / P->tau, wp = HM.ep / P->tau;
      o[2] = cw_clampf(-hm_stick(wy, sy_full) * (1 + hm_gauss() * P->noise), -1, 1); /* stick right turns right: yaw goes down */
      o[3] = cw_clampf(-hm_stick(wp, sp_full) * (1 + hm_gauss() * P->noise), -1, 1); /* stick up looks up */
      float tol = ang * P->fire_tol + 0.012f;
      bool on = fabsf(s[1]) < tol && fabsf(s[2]) < tol * 1.6f + 0.02f; /* the eye sees the true picture: fire when it is on */
      if (on) HM.burst = HM.kind == 3 ? 6 : 3;    /* hold a short burst (300 ms; a newcomer holds 600 ms) once started */
      if (HM.burst > 0) o[4] = 1, HM.burst--;
      if (fabsf(s[1]) > tol * 3 && HM.kind != 3) HM.burst = 0;
      if (P->swaps && --HM.swap_cool <= 0) {
        int have = (int)ptest[21], w = (int)ptest[9], want = 0;
        if (dist < 9 && (have & 2)) want = 1;
        else if (dist > 18 && (have & 8)) want = 3;
        else if (dist > 7 && (have & 4)) want = 2;
        if (want != w) o[7] = 1, HM.swap_cool = 4;
      }
    }
    /* moving in a fight */
    if (HM.strafing) {
      if (--HM.strafe_t <= 0) HM.strafe_t = 5 + (int)(pt_rand() * 8), HM.strafe_dir = pt_rand() < 0.5f ? -1 : 1;
      o[0] = (float)HM.strafe_dir;
      if (P->jumps && pt_rand() < 0.04f) o[5] = 1;
    }
    float pref = ptest[9] == 1 ? 6 : ptest[9] == 3 ? 20 : 12;
    if (dist > pref * 1.6f) o[1] = -0.7f;
    else if (dist < pref * 0.4f) o[1] = 0.5f;
    return;
  }
  /* a target just lost behind cover: a person keeps looking there and keeps moving for a moment, then walks on */
  if (HM.tgt != -2 && t < HM.lost_t) { o[0] = (float)HM.strafe_dir * (HM.strafing ? 1.0f : 0); return; }
  /* hit by someone unseen: turn toward the damage indicator after a reaction */
  if (ptest[19] > 0.6f) HM.dmg_t = t + (int)(P->react_min + pt_rand() * P->react_span);
  float route_w = 0.9f;
  if (ptest[19] > 0.05f && t >= HM.dmg_t && HM.dmg_t > 0) {
    float a = cw_wrap_angle(ptest[20]);
    o[2] = cw_clampf(-hm_stick(a / (P->tau * 1.5f), sy_full), -1, 1);
    route_w = 0.4f;
  } else {
    /* walking the route, looking where it goes, the view coming back level */
    float a = ptest[13];
    o[2] = cw_clampf(-hm_stick(a / 0.35f, sy_full), -1, 1);
    o[3] = cw_clampf(-hm_stick((-ptest[3]) / 0.5f, sp_full), -1, 1);
  }
  if (ptest[14] > 0.01f) {
    float a = ptest[13], c = cosf(a), sn = sinf(a);
    float fwd = c, side = -sn; /* the waypoint in the body frame: ahead and to the right */
    float k = route_w / CW_MAX(fabsf(fwd), fabsf(side));
    o[1] = cw_clampf(-fwd * k, -1, 1), o[0] = cw_clampf(side * k, -1, 1);
    if (ptest[15] > 0.5f && ptest[14] < 1.6f) o[5] = 1;
    if (P->sprints && fwd > 0.9f && ptest[14] > 4) o[6] = 1;
  }
}

static void cmd_ptest(int argc, const char **argv, cw_reply *r) {
  if (argc > 1 && !strcmp(argv[1], "give")) { /* every weapon (viewmodel shots) */
    for (int k = 0; k < NWEAP; k++) F(0)->have |= 1 << k, F(0)->ammo[k] = W[k].max_ammo;
    if (r) cw_reply_raw(r, "{\"give\": true}");
    return;
  }
  HM.kind = argc > 1 && !strcmp(argv[1], "casual") ? 1 : argc > 1 && !strcmp(argv[1], "skilled") ? 2 : argc > 1 && !strcmp(argv[1], "newcomer") ? 3 : 0;
  HM.tgt = -1;
  PT.on = argc < 2 || strcmp(argv[1], "off") != 0;
  PT.n = 0, PT.goal = v3(0, -1000, 0), PT.rng = 0x5eed1234u, PT.lastitem = -1;
  if (r) cw_reply_raw(r, PT.on ? "{\"ptest\": true}" : "{\"ptest\": false}");
}

/* ── humans ────────────────────────────────────────────────────────── */

static void human_tick(fighter *f, int idx) {
  int h = f->human;
  cw_vec2 mv = cw_input_vector(h, G.mv_l, G.mv_r, G.mv_f, G.mv_b, CW_DEADZONE_SCALED_RADIAL, 0.18f);
  cw_vec2 lk = cw_input_vector(h, G.lk_l, G.lk_r, G.lk_u, G.lk_d, CW_DEADZONE_RADIAL, 0);
  bool drive = hm_driving(f), b_fire = false, b_jump = false, b_sprint = false, b_next = false;
  if (drive) { /* the autopilot's scripted human: the pad state it decided (what tools/playtest-human.mjs presses on romdev) */
    static bool was_next;
    mv = cw_v2(HM.out[0], HM.out[1]);
    float ml = sqrtf(mv.x * mv.x + mv.y * mv.y);
    if (ml < 0.18f) mv = cw_v2(0, 0);
    else { float k = CW_MIN(1.0f, (ml - 0.18f) / 0.82f) / ml; mv = cw_v2(mv.x * k, mv.y * k); }
    lk = cw_v2(HM.out[2], HM.out[3]);
    b_fire = HM.out[4] > 0.5f, b_jump = HM.out[5] > 0.5f, b_sprint = HM.out[6] > 0.5f;
    b_next = HM.out[7] > 0.5f && !was_next;
    was_next = HM.out[7] > 0.5f;
  }
  cw_aim_curve curve = { .deadzone = 0.15f, .power = 1.8f, .yaw_rate = 3.6f, .pitch_rate = 2.4f, .ramp_seconds = 0.35f,
                                      .ramp_gain = 0.6f, .ramp_edge = 0.9f, .assist = G.aim_assist ? 0.35f : 0 };
  /* the crosshair is on a fighter: a little aim assist */
  t3_vec3 d = look_dir(f->yaw, f->pitch);
  float on = 0;
  for (int i = 0; i < G.nf; i++) {
    fighter *o = F(i);
    if (i == idx || !o->active || !o->alive || !enemies(f, o)) continue;
    /* only a target in the open: never one behind a wall */
    if (ray_fighter(o, eye_pos(f), d, 60, NULL) >= 0 && !arena_ray(eye_pos(f), chest_pos(o), NULL, NULL)) on = 1;
  }
  cw_vec2 turn = cw_aim_step(&G.aim[h], &curve, lk, TICK_DT, on);
  f->yaw -= turn.x;
  f->pitch = cw_clampf(f->pitch - turn.y, -1.45f, 1.45f);
  if (G.aim_assist && (fabsf(lk.x) + fabsf(lk.y) > 0.05f || fabsf(mv.x) + fabsf(mv.y) > 0.1f)) {
    /* gentle magnetism: while the player aims or moves, the view drifts toward an enemy in the open within about 5 degrees
     * of the crosshair (at most 0.5 rad/s, so it helps and never snaps); never one behind a wall */
    float best = 0.09f, by = 0, bp = 0;
    bool got = false;
    for (int i = 0; i < G.nf; i++) {
      fighter *o = F(i);
      if (i == idx || !o->active || !o->alive || !enemies(f, o)) continue;
      t3_vec3 to = v3sub(chest_pos(o), eye_pos(f));
      float dist = v3len(to);
      if (dist > 40 || dist < 0.5f) continue;
      float hd = sqrtf(to.x * to.x + to.z * to.z);
      float dyaw = cw_wrap_angle(cw_atan2(-to.x, -to.z) - f->yaw), dpitch = cw_atan2(to.y, hd) - f->pitch;
      float a = fabsf(dyaw) + fabsf(dpitch);
      if (a >= best || arena_ray(eye_pos(f), chest_pos(o), NULL, NULL)) continue;
      best = a, by = dyaw, bp = dpitch, got = true;
    }
    if (got) {
      float m = 0.5f * TICK_DT;
      f->yaw += cw_clampf(by * 0.15f, -m, m);
      f->pitch += cw_clampf(bp * 0.15f, -m, m);
    }
  }
  if (drive ? b_next : cw_action_just_pressed(h, G.w_next)) {
    for (int k = 1; k <= NWEAP; k++) { int n = (f->weapon + k) % NWEAP; if ((f->have >> n & 1)) { f->weapon = n; break; } }
    fx_play_ui(SND_CLICK, 0.4f);
  }
  if (cw_action_just_pressed(h, G.w_prev)) {
    for (int k = 1; k <= NWEAP; k++) { int n = (f->weapon + NWEAP - k) % NWEAP; if ((f->have >> n & 1)) { f->weapon = n; break; } }
    fx_play_ui(SND_CLICK, 0.4f);
  }
  if (!drive && cw_action_just_pressed(h, G.grenade)) throw_grenade(f, idx, throw_vel(f));
  if (!drive && cw_action_just_pressed(h, G.melee)) bash(f, idx);
  bool wants_fire = drive ? b_fire : cw_action_pressed(h, G.fire);
  float view_y0 = f->yaw, view_p0 = f->pitch;
  if (h == 0) G.p1_in[0] = mv.x, G.p1_in[1] = mv.y, G.p1_in[2] = lk.x, G.p1_in[3] = lk.y, G.p1_in[4] = wants_fire;
  if (wants_fire) do_fire(f, idx);
  if (h == 0 && wants_fire && fabsf(lk.x) + fabsf(lk.y) < 0.05f) {
    cam_fire[0]++;
    if (fabsf(f->yaw - view_y0) + fabsf(f->pitch - view_p0) > 1e-6f) cam_fire[1]++;
  }
  move_fighter(f, -mv.y, mv.x, drive ? b_jump : cw_action_pressed(h, G.jump), drive ? b_sprint : cw_action_pressed(h, G.sprint));
}

/* ── pickups ───────────────────────────────────────────────────────── */

static void item_tick(void) {
  if (A.prop_count == 0) arena_ground_check();
  for (int i = 0; i < A.nitems; i++) {
    item *it = &A.items[i];
    if (!it->grounded) { /* the drawn item and its spot stand on what is really under it (a level's nominal height can be off the floor); only the picture moves */
      t3_vec3 hit, from = v3add(it->pos, v3(0, 0.6f, 0));
      it->grounded = true;
      if (arena_ray(from, v3add(from, v3(0, -12, 0)), &hit, NULL)) {
        int gap = (int)(fabsf(it->pos.y - 0.5f - hit.y + (i == 0 && getenv("ARENA_PROP_LIFT") ? 0.5f : 0)) * 1000); /* the control also lifts the first item nominally */
        if (gap > A.item_gap_mm) A.item_gap_mm = gap;
        it->floor_y = hit.y;
      }
      cw_node3d_set_position(it->spot, v3(it->pos.x, it->floor_y + 0.018f, it->pos.z));
      cw_node3d_set_position(it->spot_glow, v3(it->pos.x, it->floor_y + 0.02f, it->pos.z));
      cw_node3d_set_position(it->light, v3(it->pos.x, it->floor_y + 1.5f, it->pos.z));
    }
    float spin = (float)G.ticks * 0.04f + (float)i;
    cw_node3d_set_rotation(it->node, v3(0, spin, 0));
    cw_node3d_set_position(it->node, v3(it->pos.x, it->floor_y + 0.58f + 0.06f * cw_sin((float)G.ticks * 0.05f + (float)i), it->pos.z));
    if (it->respawn > 0) {
      if (--it->respawn == 0) {
        cw_node_set(it->node, "visible", cw_val_bool(true));
        cw_node_set(it->light, "energy", cw_val_float(14));
      }
      continue;
    }
    for (int j = 0; j < G.nf; j++) {
      fighter *f = F(j);
      if (!f->active || !f->alive) continue;
      float dx = f->mv.position.x - it->pos.x, dz = f->mv.position.z - it->pos.z, dy = f->mv.position.y - (it->pos.y - 0.5f);
      if (dx * dx + dz * dz > 1.4f * 1.4f || dy < -0.6f || dy > 2.2f) continue;
      bool take = false;
      int respawn = 60 * 20;
      switch (it->kind) {
        case ITEM_HEALTH: if (f->hp < 100) f->hp = CW_MIN(100, f->hp + 35), take = true; respawn = 60 * 20; break;
        case ITEM_ARMOR: if (f->armor < 100) f->armor = CW_MIN(100, f->armor + 50), take = true; respawn = 60 * 30; break;
        case ITEM_AMMO:
          for (int k = 0; k < NWEAP; k++) if (f->have >> k & 1) f->ammo[k] = CW_MIN(W[k].max_ammo, f->ammo[k] + W[k].start_ammo / 2);
          f->grenades = CW_MIN(4, f->grenades + 1);
          take = true; respawn = 60 * 15; break;
        default: {
          int k = it->kind == ITEM_SCATTER ? 1 : it->kind == ITEM_ROCKET ? 2 : 3;
          bool had = f->have >> k & 1;
          if (!had || f->ammo[k] < W[k].max_ammo) {
            f->have |= 1 << k;
            f->ammo[k] = CW_MIN(W[k].max_ammo, f->ammo[k] + W[k].start_ammo);
            if (!had && f->weapon == 0) f->weapon = k;
            take = true;
          }
          respawn = k == 1 ? 60 * 10 : 60 * 20; /* scatter guns come back fast, rockets and rails on a 20 s timer shown over the spot */
          break;
        }
      }
      if (take && f->human >= 0) {
        static const char *PICK[ITEM_KINDS] = { "HEALTH +35", "ARMOUR +50", "SCATTER GUN", "ROCKETS", "RAIL", "AMMO, GRENADE +1" };
        snprintf(G.pick_text[f->human], sizeof G.pick_text[0], "%s", PICK[it->kind]);
        G.pick_t[f->human] = 1;
      }
      if (take) {
        it->respawn = respawn;
        cw_node_set(it->node, "visible", cw_val_bool(false));
        cw_node_set(it->light, "energy", cw_val_float(0));
        if (f->human >= 0) fx_play_ui(SND_PICKUP, 0.6f);
        else fx_play(SND_PICKUP, it->pos, 0.4f, 1);
        break;
      }
    }
  }
}

/* ── power-ups ─────────────────────────────────────────────────────────
 * One spot in the middle of each arena (the most fought over ground). A power-up appears there 30 s into the match and
 * then 60 s after the last was taken, quad damage, haste and regeneration in turn, announced to everyone; the holder
 * has it for 20 s or until downed, and glows its colour so everyone can see who to hunt. */
#define POW_HOLD (60 * 20)
static void power_place(void) {
  t3_vec3 q;
  PW.pos = cw_nav_nearest(A.nav, v3(0, 0, 0), NULL, &q) ? q : v3(0, 0, 0);
  for (int i = 0; i < A.nitems; i++) { /* never on an item's spot */
    t3_vec3 d = v3sub(A.items[i].pos, PW.pos);
    if (d.x * d.x + d.z * d.z < 2.5f * 2.5f && fabsf(d.y) < 2) {
      t3_vec3 c = v3add(PW.pos, v3(d.x > 0 ? -2.6f : 2.6f, 0, 0));
      if (cw_nav_nearest(A.nav, c, NULL, &q)) PW.pos = q;
      break;
    }
  }
  { /* stand on what is under it (a ray down from above the navmesh point) */
    t3_vec3 hit, from = v3add(PW.pos, v3(0, 0.8f, 0));
    if (arena_ray(from, v3add(from, v3(0, -3, 0)), &hit, NULL)) PW.pos.y = hit.y;
  }
  PW.present = false, PW.kind = POW_NONE, PW.next = POW_QUAD, PW.timer = 60 * 30;
  if (cw_node_valid(PW.node)) {
    cw_node3d_set_position(PW.node, PW.pos);
    cw_node_set(PW.node, "visible", cw_val_bool(G.mode != MODE_COOP));
    cw_node_set(PW.gem, "visible", cw_val_bool(false));
    cw_node_set(PW.light, "energy", cw_val_float(0));
  }
}
static void power_nodes(void) { /* a dark plinth with a glowing ring, and the power-up: a turning crystal over it */
  PW.node = cw_node_new("Node3D", "PowerSpot");
  cw_node_set(PW.node, "visible", cw_val_bool(false)); /* placed and shown when a match begins (not in co-op) */
  cw_node_add_child(G.world, PW.node);
  PW.disc = cw_node_new("MeshInstance3D", "Plinth");
  cw_node_set(PW.disc, "primitive", cw_val_enum("CYLINDER"));
  cw_node_set(PW.disc, "size", cw_val_vec3(0.9f, 0.08f, 0));
  cw_node_set(PW.disc, "color", cw_val_color(0.06f, 0.06f, 0.07f, 1));
  cw_node_set(PW.disc, "metalness", cw_val_float(0.6f));
  cw_node_set(PW.disc, "roughness", cw_val_float(0.4f));
  cw_node3d_set_position(PW.disc, v3(0, 0.04f, 0));
  cw_node_add_child(PW.node, PW.disc);
  PW.ring = cw_node_new("MeshInstance3D", "Ring");
  cw_node_set(PW.ring, "primitive", cw_val_enum("CYLINDER"));
  cw_node_set(PW.ring, "size", cw_val_vec3(0.72f, 0.09f, 0));
  cw_node_set(PW.ring, "color", cw_val_color(0.2f, 0.2f, 0.2f, 1));
  cw_node_set(PW.ring, "emissive", cw_val_color(1.2f, 1.2f, 1.2f, 1));
  cw_node_set(PW.ring, "cast_shadow", cw_val_bool(false));
  cw_node3d_set_position(PW.ring, v3(0, 0.045f, 0));
  cw_node_add_child(PW.node, PW.ring);
  PW.gem = cw_node_new("MeshInstance3D", "Crystal");
  cw_node_set(PW.gem, "primitive", cw_val_enum("BOX"));
  cw_node_set(PW.gem, "size", cw_val_vec3(0.42f, 0.42f, 0.42f));
  cw_node_set(PW.gem, "metalness", cw_val_float(0.2f));
  cw_node_set(PW.gem, "roughness", cw_val_float(0.15f));
  cw_node_set(PW.gem, "visible", cw_val_bool(false));
  cw_node_add_child(PW.node, PW.gem);
  PW.light = cw_node_new("OmniLight3D", "PowerLight");
  cw_node_set(PW.light, "energy", cw_val_float(0));
  cw_node_set(PW.light, "range", cw_val_float(7));
  cw_node3d_set_position(PW.light, v3(0, 1.4f, 0));
  cw_node_add_child(PW.node, PW.light);
}
static void power_look(void) { /* drawn: the crystal's colour, turn and bob, the ring in the next kind's colour */
  if (!cw_node_valid(PW.node)) return;
  int k = PW.present ? PW.kind : PW.next;
  const float *c = POW_RGB[k];
  float up = PW.present ? 1.0f : 0.25f;
  cw_node_set(PW.ring, "emissive", cw_val_color(c[0] * 2 * up, c[1] * 2 * up, c[2] * 2 * up, 1));
  cw_node_set(PW.gem, "visible", cw_val_bool(PW.present));
  cw_node_set(PW.light, "energy", cw_val_float(PW.present ? 10.0f : 0.0f));
  if (!PW.present) return;
  cw_node_set(PW.gem, "color", cw_val_color(c[0] * 0.5f, c[1] * 0.5f, c[2] * 0.5f, 1));
  cw_node_set(PW.gem, "emissive", cw_val_color(c[0] * 3, c[1] * 3, c[2] * 3, 1));
  cw_node_set(PW.light, "color", cw_val_color(c[0], c[1], c[2], 1));
  float t = (float)G.ticks;
  cw_node3d_set_position(PW.gem, v3(0, 1.0f + 0.08f * cw_sin(t * 0.04f), 0));
  cw_node3d_set_rotation(PW.gem, v3(0.785f, t * 0.03f, 0.615f)); /* stood on a corner: a crystal, not a crate */
}
static void power_tick(void) {
  if (!PW.present) {
    if (--PW.timer <= 0) {
      PW.present = true, PW.kind = PW.next;
      pow_stats[0]++;
      feed("%s is up%s", POW_NAME[PW.kind], " in the middle");
      fx_play_ui(SND_POWER, 0.7f);
    }
  } else {
    for (int j = 0; j < G.nf; j++) {
      fighter *f = F(j);
      if (!f->active || !f->alive) continue;
      t3_vec3 d = v3sub(f->mv.position, PW.pos);
      if (d.x * d.x + d.z * d.z > 1.3f * 1.3f || d.y < -0.6f || d.y > 2.2f) continue;
      f->power = PW.kind, f->power_t = POW_HOLD;
      PW.present = false, PW.timer = 60 * 60, PW.next = PW.kind % (POW_KINDS - 1) + 1;
      pow_stats[1]++;
      feed("%s has %s", f->name, POW_NAME[f->power]);
      if (f->human >= 0) {
        snprintf(G.pick_text[f->human], sizeof G.pick_text[0], "%s", POW_NAME[f->power]);
        G.pick_t[f->human] = 1;
        rumble(f->human, 0.3f, 0.3f, 150);
      }
      fx_play(SND_POWER, PW.pos, 0.9f, 1.25f);
      break;
    }
  }
  for (int j = 0; j < G.nf; j++) {
    fighter *f = F(j);
    if (!f->active || !f->alive || f->power == POW_NONE) continue;
    pow_stats[4]++;
    if (f->power == POW_REGEN && G.ticks % 6 == 0 && f->hp < 150) f->hp++; /* about 10 health a second, up to 150 */
    if (--f->power_t <= 0) f->power = POW_NONE;
  }
  power_look();
}

/* ── rockets ───────────────────────────────────────────────────────── */

static void proj_tick(void) {
  for (int i = 0; i < MAXP; i++) {
    proj *p = &G.pr[i];
    if (!p->on) continue;
    t3_vec3 np = v3add(p->p, v3scale(p->v, TICK_DT));
    t3_vec3 hp, hn;
    bool hit = arena_ray(p->p, np, &hp, &hn);
    /* a fighter in the way */
    float bt = hit ? v3len(v3sub(hp, p->p)) : 1e9f, seg = v3len(v3sub(np, p->p));
    t3_vec3 dir = v3scale(p->v, 1 / 24.0f);
    for (int j = 0; j < G.nf; j++) {
      fighter *f = F(j);
      if (!f->active || !f->alive || j == p->owner) continue;
      float t = ray_fighter(f, p->p, dir, CW_MIN(bt, seg), NULL);
      if (t >= 0 && t < bt) { bt = t; hit = true; hp = v3add(p->p, v3scale(dir, t)); hn = v3scale(dir, -1); }
    }
    if (hit) {
      explode(p, hp, hn);
      continue;
    }
    p->p = np;
    if (--p->life <= 0) { explode(p, np, v3(0, 1, 0)); continue; }
    cw_node3d_set_position(p->node, np);
    cw_node3d_look_at(p->node, v3add(np, p->v), v3(0, 1, 0));
    cw_node3d_set_position(p->light, np);
    /* the light swells over four ticks as it leaves the barrel, and a rocket near a view's eye dims and its hot exhaust
     * shrinks (nothing within 1.5 m, whole from 6.5 m): one flying at a player grew from a dot to a white ball of glow
     * filling that view and lit the wall beside it, three a second in the flash sweep's match run */
    if (p->glow < 1) p->glow = fminf(1.0f, p->glow + 0.25f);
    {
      float de = 1e9f;
      for (int h = 0; h < G.players; h++) de = fminf(de, v3len(v3sub(eye_pos(SF(h)), np)));
      float u = CW_CLAMP((de - 1.5f) / 5.0f, 0, 1), near = u * u * (3 - 2 * u);
      cw_node_set(p->light, "energy", cw_val_float(14 * p->glow * near));
      cw_node ex = cw_node_get(p->node, "Exhaust");
      if (cw_node_valid(ex)) cw_node3d_set_scale(ex, v3(near, near, near));
    }
  }
}

/* ── the match ─────────────────────────────────────────────────────── */

static const char *BOT_NAMES[] = { "Ash", "Brick", "Cinder", "Dune", "Ember", "Flint", "Gale", "Husk", "Iron", "Jolt", "Knox", "Lark", "Moss", "Nox", "Onyx", "Pike" };

/* the skinned fighter: soldier.c (models/soldier_*.glb on the full humanoid skeleton, its clips retargeted from
 * anim/ual.glb; an AnimationTree, the aim, the feet, ragdolls). soldier_a is the blue one, soldier_b the red one */
/* the rim on a fighter's body (cartwheel/rim_light.h): its side's colour, fading in from 8 m so a fighter 30 m off reads
 * as a red or blue outline and one close by is not painted over (deathmatch: everyone is an enemy, red). While spawn
 * protected it shimmers pale blue at every distance. Applying again is a uniform change, so the shimmer pulses per tick */
static void body_rim(const fighter *f, t3_material *mat, bool force) {
  if (!mat || !HAVE_RIM) return;
#if HAVE_RIM
#ifdef ARENA_AUTOPILOT
  if (getenv("ARENA_NO_RIM")) return; /* measuring the rim's cost */
#endif
  int g = f->spawn_guard;
  static int last[MAXF], lastp[MAXF];
  int slot = (int)(f - G.f), pk = f->alive ? f->power : POW_NONE;
  if (!force && g <= 0 && last[slot] <= 0 && lastp[slot] == pk) return;
  last[slot] = g, lastp[slot] = pk;
  cw_rim_light_desc d = cw_rim_light_default();
  bool blue = G.mode == MODE_TDM ? (f->team & 1) : G.mode == MODE_BATTLE ? f->team == 0 : (G.mode == MODE_COOP && f->human >= 0);
  d.color = blue ? (t3_color){ 0.25f, 0.55f, 1.0f } : (t3_color){ 1.0f, 0.28f, 0.18f };
  d.strength = 2.4f, d.power = 2.6f, d.near = 8, d.far = 26, d.close = 0.12f;
  if (pk != POW_NONE && g <= 0) { /* a power-up's holder glows its colour, steady, near and far: the one to hunt */
    d.color = (t3_color){ POW_RGB[pk][0], POW_RGB[pk][1], POW_RGB[pk][2] };
    d.strength = 3.4f, d.power = 1.8f, d.close = 0.8f, d.far = 40;
  }
  if (g > 0) {
    float k = 0.5f + 0.5f * cw_sin((float)G.ticks * 0.18f + (float)slot); /* 1.7 Hz: a slow pulse, never a flicker */
    d.color = (t3_color){ 0.55f, 0.85f, 1.0f };
    d.strength = 1.2f + 2.4f * k, d.power = 1.6f, d.close = 1.0f;
  }
  cw_rim_light_apply(mat, &d);
#else
  (void)f, (void)force, (void)mat;
#endif
}

static cw_node make_body(int team, fighter *f) {
  soldier_make(f, (int)(f - G.f), (team & 1) ? 0 : 1, G.world, G.rules == RULE_BIGHEADS);
  body_rim(f, soldier_material((int)(f - G.f)), true);
  f->clip = "idle";
  return f->body;
}

static void clear_world(void) {
  if (cw_node_valid(G.world)) {
    fx_reset();
    crowd_reset();
    cw_node_free(G.world);
    soldier_free_all();
  }
  arena_free();
  memset(G.f, 0, sizeof G.f);
  memset(G.pr, 0, sizeof G.pr);
  memset(GR, 0, sizeof GR);
  memset(&PW, 0, sizeof PW);
  G.world = (cw_node){ 0 };
}

static void build_world(int arena_id) {
  clear_world();
  G.world = cw_node_new("Node3D", "World");
  cw_node_add_child(G.root, G.world);
  arena_build(arena_id, G.world);
  arena_mood(arena_id, G.env, G.sun, G.sky, G.fog, G.post);
  fx_init(G.world);
  for (int i = 0; i < MAXP; i++) {
    proj *p = &G.pr[i];
    p->node = cw_node_new("Node3D", "Rocket");
    cw_node_set(p->node, "visible", cw_val_bool(false));
    cw_node_add_child(G.world, p->node);
    {
      /* the missile: a body along -Z (its heading), a nose cone, four fins, a glowing exhaust, a smoke trail and a flame */
      cw_node body = cw_node_new("MeshInstance3D", "Body");
      cw_node_set(body, "primitive", cw_val_enum("CYLINDER"));
      cw_node_set(body, "size", cw_val_vec3(0.075f, 0.6f, 0));
      cw_node_set(body, "color", cw_val_color(0.55f, 0.58f, 0.55f, 1));
      cw_node_set(body, "metalness", cw_val_float(0.7f));
      cw_node_set(body, "roughness", cw_val_float(0.35f));
      cw_node3d_set_rotation(body, v3(1.5708f, 0, 0));
      cw_node_add_child(p->node, body);
      cw_node nose = cw_node_new("MeshInstance3D", "Nose");
      cw_node_set(nose, "primitive", cw_val_enum("SPHERE"));
      cw_node_set(nose, "size", cw_val_vec3(0.078f, 0, 0));
      cw_node_set(nose, "color", cw_val_color(0.9f, 0.3f, 0.08f, 1));
      cw_node_set(nose, "roughness", cw_val_float(0.4f));
      cw_node3d_set_position(nose, v3(0, 0, -0.3f));
      cw_node_add_child(p->node, nose);
      for (int k = 0; k < 4; k++) {
        cw_node fin = cw_node_new("MeshInstance3D", "Fin");
        cw_node_set(fin, "primitive", cw_val_enum("BOX"));
        cw_node_set(fin, "size", cw_val_vec3(0.012f, 0.14f, 0.16f));
        cw_node_set(fin, "color", cw_val_color(0.2f, 0.2f, 0.22f, 1));
        cw_node_set(fin, "metalness", cw_val_float(0.8f));
        cw_node3d_set_position(fin, v3(0, 0, 0.22f));
        cw_node3d_set_rotation(fin, v3(0, 0, (float)k * 1.5708f));
        cw_node_add_child(p->node, fin);
      }
      cw_node ex = cw_node_new("MeshInstance3D", "Exhaust");
      cw_node_set(ex, "primitive", cw_val_enum("SPHERE"));
      cw_node_set(ex, "size", cw_val_vec3(0.07f, 0, 0));
      cw_node_set(ex, "color", cw_val_color(1, 0.7f, 0.3f, 1));
      cw_node_set(ex, "emissive", cw_val_color(14, 7, 1.8f, 1));
      cw_node_set(ex, "cast_shadow", cw_val_bool(false));
      cw_node3d_set_position(ex, v3(0, 0, 0.32f));
      cw_node_add_child(p->node, ex);
      p->trail = rocket_trail();
      cw_node_add_child(p->node, p->trail);
    }
    p->light = cw_node_new("OmniLight3D", "RocketLight");
    cw_node_set(p->light, "color", cw_val_color(1, 0.55f, 0.2f, 1));
    cw_node_set(p->light, "energy", cw_val_float(0));
    cw_node_set(p->light, "range", cw_val_float(9));
    cw_node_add_child(G.world, p->light);
  }
  for (int i = 0; i < MAXGREN; i++) { /* a grenade: an olive body with a steel cap and a steady red lamp on it */
    gren *g = &GR[i];
    g->node = cw_node_new("Node3D", "Grenade");
    cw_node_set(g->node, "visible", cw_val_bool(false));
    cw_node3d_set_position(g->node, v3(0, -500, 0));
    cw_node_add_child(G.world, g->node);
    cw_node b = cw_node_new("MeshInstance3D", "Body");
    cw_node_set(b, "primitive", cw_val_enum("SPHERE"));
    cw_node_set(b, "size", cw_val_vec3(0.065f, 0, 0));
    cw_node_set(b, "color", cw_val_color(0.22f, 0.27f, 0.16f, 1));
    cw_node_set(b, "roughness", cw_val_float(0.6f));
    cw_node_set(b, "metalness", cw_val_float(0.3f));
    cw_node_add_child(g->node, b);
    cw_node cap = cw_node_new("MeshInstance3D", "Cap");
    cw_node_set(cap, "primitive", cw_val_enum("CYLINDER"));
    cw_node_set(cap, "size", cw_val_vec3(0.028f, 0.05f, 0));
    cw_node_set(cap, "color", cw_val_color(0.6f, 0.6f, 0.58f, 1));
    cw_node_set(cap, "metalness", cw_val_float(0.9f));
    cw_node_set(cap, "roughness", cw_val_float(0.35f));
    cw_node3d_set_position(cap, v3(0, 0.07f, 0));
    cw_node_add_child(g->node, cap);
    cw_node led = cw_node_new("MeshInstance3D", "Lamp");
    cw_node_set(led, "primitive", cw_val_enum("SPHERE"));
    cw_node_set(led, "size", cw_val_vec3(0.014f, 0, 0));
    cw_node_set(led, "color", cw_val_color(1, 0.2f, 0.1f, 1));
    cw_node_set(led, "emissive", cw_val_color(6, 0.8f, 0.3f, 1));
    cw_node_set(led, "cast_shadow", cw_val_bool(false));
    cw_node3d_set_position(led, v3(0, 0.1f, 0));
    cw_node_add_child(g->node, led); /* no light of its own: the lamp's glow (bloom) marks it, and no light count grows */
  }
  power_nodes();
}

/* the setup's bot level scales every bot's skill: easy 0.65x, normal as drawn, hard pulled up toward 0.95 */
static float bot_skill(float s) {
  const bot_level_def *L = &LEVELS[G.bot_level];
  return CW_MIN(0.95f, L->skill_add + s * L->skill_mul);
}


/* the bot count the AUTO setting fills in: four fighters in all (bots fill the empty seats, none with four people). Measured with
 * tools/pacing.mjs: with six to nine fighters every fighter had an enemy in sight most of the time and met one every 4 to 7 s;
 * four keep the arenas' lanes quiet between fights (and the teams even in team deathmatch) */
static int auto_bots(void) { /* four fighters (one player: three bots, the first pass gave five); team deathmatch rounds up
                                * to even teams. Two bots for one player left 45 to 50 s with nobody met (tools/pacing.mjs) */
  int b = CW_MAX(0, 4 - G.players);
  if (G.mode == MODE_TDM && ((G.players + b) & 1)) b++;
  return b;
}

static void setup_fighters(void) {
  G.nf = 0;
  int bots;
  if (G.mode == MODE_COOP) bots = 0;
  else bots = G.bots_setting < 0 ? auto_bots() : G.bots_setting;
  int total_bots = G.mode == MODE_COOP ? 0 : CW_MIN(bots, MAXF - G.players);
  if (battle()) total_bots = CW_MIN(MAXF, BT.live[0] + BT.live[1]) - G.players; /* each side its live count, the people on side 0 */
  for (int i = 0; i < G.players + total_bots; i++) {
    fighter *f = F(i);
    memset(f, 0, sizeof *f);
    f->active = true;
    f->human = i < G.players ? i : -1;
    f->team = G.mode == MODE_DM ? i : (G.mode == MODE_COOP ? (i < G.players ? 0 : 1) : i % 2);
    if (G.mode == MODE_TDM) f->team = i % 2;
    if (battle()) f->team = i < BT.live[0] ? 0 : 1;
    f->skill = i < G.players ? 0.5f : bot_skill(0.35f + 0.5f * (float)((i * 37) % 10) / 10.0f); /* seats: a person (or the autopilot's stand-in) */
    f->personality = f->human >= 0 ? 0 : (i * 2 + G.arena_id) % 3;
    if (f->human >= 0) snprintf(f->name, sizeof f->name, "P%d", i + 1);
    else if (i < 16) snprintf(f->name, sizeof f->name, "%s", BOT_NAMES[i % 16]);
    else snprintf(f->name, sizeof f->name, "%s %d", BOT_NAMES[i % 16], i / 16 + 1);
    cw_mover3d_def md = cw_mover3d_default_def();
    md.radius = PLAYER_R;
    md.height = PLAYER_HT;
    md.step_height = 0.45f;
    md.mask = ~(1u << 2); /* never the ragdolls (soldier.c: their layer) */
    cw_mover3d_init(&f->mv, A.world, &md, v3(0, 0, 0));
    f->target = -1;
    f->goal = v3(0, -1000, 0);
    make_body(G.mode == MODE_DM ? i : G.mode == MODE_BATTLE ? (f->team ^ 1) : f->team, f);
    f->light = cw_node_new("OmniLight3D", "Muzzle");
    cw_node_set(f->light, "color", cw_val_color(1, 0.75f, 0.4f, 1));
    cw_node_set(f->light, "energy", cw_val_float(0));
    cw_node_set(f->light, "range", cw_val_float(11));
    cw_node_add_child(G.world, f->light);
    spawn_fighter(f);
    G.nf++;
  }
}

static void spawn_wave(void) {
  G.wave++;
  /* the wave grows with the wave number and with how many people hold the arena: 4 bots at wave 1 for one player, 8 for four */
  G.present = present_players();
  int n = CW_MIN(2 + G.wave + G.present * (1 + G.wave / 2), MAXF_CLASSIC - G.players);
  for (int i = 0; i < n; i++) {
    int slot = -1;
    for (int k = G.players; k < MAXF; k++) if (!F(k)->active || !F(k)->alive) { if (!F(k)->active) { slot = k; break; } }
    if (slot < 0) break;
    fighter *f = F(slot);
    memset(f, 0, sizeof *f);
    f->active = true;
    f->human = -1;
    f->team = 1;
    f->skill = bot_skill(CW_MIN(0.95f, 0.3f + 0.1f * (float)G.wave + 0.04f * (float)(i % 5)));
    f->personality = (i + G.wave) % 3;
    snprintf(f->name, sizeof f->name, "%s", BOT_NAMES[(slot + G.wave) % 16]);
    cw_mover3d_def md = cw_mover3d_default_def();
    md.radius = PLAYER_R;
    md.height = PLAYER_HT;
    md.step_height = 0.45f;
    cw_mover3d_init(&f->mv, A.world, &md, v3(0, 0, 0));
    f->goal = v3(0, -1000, 0);
    make_body(1, f);
    f->light = cw_node_new("OmniLight3D", "Muzzle");
    cw_node_set(f->light, "color", cw_val_color(1, 0.75f, 0.4f, 1));
    cw_node_set(f->light, "energy", cw_val_float(0));
    cw_node_set(f->light, "range", cw_val_float(11));
    cw_node_add_child(G.world, f->light);
    spawn_fighter(f);
    f->hp = 60 + 12 * G.wave;
    if (slot >= G.nf) G.nf = slot + 1;
  }
}

static void set_views(void) {
  cw_node cams[MAXH];
  for (int i = 0; i < MAXH; i++) cams[i] = G.cam[i]; /* every slot: the shell reads as many as it has players */
  G.views = cw_shell_split(NULL, cams);
  cw_pipeline *pl = cw_pipeline_of(G.tree);
  cw_pipeline_set_views_shared_post(pl, G.players > 1);
  cw_node_set(G.cam[0], "current", cw_val_bool(true));
}

static void view_hook(int view, void *u);
static void views_mode(void);
static bool view_hook_on(void);
static void view_masks(void);

/* soak measurements (autopilot only): the C heap in use at the start of each match, after its arena is built (glibc hosts;
 * 0 elsewhere). tools/soak.mjs compares the same arena and mode a few cycles apart */
#define SOAK_N 48
static int32_t soak_heap_kb[SOAK_N], soak_matches;
static void soak_note(void) {
#if defined(ARENA_AUTOPILOT) && defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33)) /* mallinfo2: glibc 2.33; the release sysroot is older */
  struct mallinfo2 mi = mallinfo2();
  if (soak_matches < SOAK_N) soak_heap_kb[soak_matches] = (int32_t)((mi.uordblks + mi.hblkhd) / 1024);
#endif
  soak_matches++;
}

/* ── Battle: posts and the rules ─────────────────────────────────────────────────────────────────────────────────── */

/* the live count a side: one player 16, two 12, three or four 8 (the frame budget of 1 view and of 4), three quarters of
 * that on Low and half on Very Low. ARENA_BATTLE_LIVE (a test switch, every host) sets it */
static int battle_live(void) {
  const char *e = cw_test_env("ARENA_BATTLE_LIVE");
  if (e && atoi(e) > 0) return CW_CLAMP(atoi(e), 2, MAXF / 2);
  int n = G.players <= 1 ? 16 : G.players == 2 ? 12 : 8;
  int q = cw_gfx_effective(CW_GFX_EFFECTS);
  if (q <= CW_QUALITY_VERY_LOW) n /= 2;
  else if (q == CW_QUALITY_LOW) n = n * 3 / 4;
  return CW_MAX(n, G.players + 1);
}
static void post_look(post *p) {
  float k = p->meter, a = fabsf(k);
  const float *c = k < 0 ? BSIDE_RGB[0] : BSIDE_RGB[1];
  float r = 0.75f + (c[0] - 0.75f) * a, g = 0.75f + (c[1] - 0.75f) * a, b = 0.75f + (c[2] - 0.75f) * a; /* white when neutral */
  if (fabsf(p->drawn - k) < 0.01f && p->drawn > -5) return;
  p->drawn = k;
  for (int i = 0; i < POST_SEGS; i++) if (cw_node_valid(p->seg[i])) cw_node_set(p->seg[i], "emissive", cw_val_color(r * 1.6f, g * 1.6f, b * 1.6f, 1));
  if (cw_node_valid(p->flag)) {
    cw_node_set(p->flag, "color", cw_val_color(r * 0.6f, g * 0.6f, b * 0.6f, 1));
    cw_node_set(p->flag, "emissive", cw_val_color(r * 1.4f, g * 1.4f, b * 1.4f, 1));
    cw_node3d_set_position(p->flag, v3(0.5f, 1.4f + 1.6f * a, 0)); /* the flag climbs the pole as a side takes it */
  }
}
static void post_make(post *p) {
  t3_vec3 hit, from = v3add(p->pos, v3(0, 0.8f, 0));
  if (arena_ray(from, v3add(from, v3(0, -3, 0)), &hit, NULL)) p->pos.y = hit.y;
  p->node = cw_node_new("Node3D", "Post");
  cw_node3d_set_position(p->node, p->pos);
  cw_node_add_child(G.world, p->node);
  for (int i = 0; i < POST_SEGS; i++) { /* the ring on the floor: low glowing kerb pieces */
    float sa, ca, a = (float)i / (float)POST_SEGS * 6.2831853f;
    cw_sincos(a, &sa, &ca);
    cw_node s = cw_node_new("MeshInstance3D", "Ring");
    cw_node_set(s, "primitive", cw_val_enum("BOX"));
    cw_node_set(s, "size", cw_val_vec3(p->r * 6.2831853f / (float)POST_SEGS * 0.62f, 0.06f, 0.16f));
    cw_node_set(s, "color", cw_val_color(0.2f, 0.2f, 0.2f, 1));
    cw_node_set(s, "cast_shadow", cw_val_bool(false));
    cw_node3d_set_position(s, v3(sa * p->r, 0.03f, ca * p->r));
    cw_node3d_set_rotation(s, v3(0, a, 0)); /* its length along the ring */
    cw_node_add_child(p->node, s);
    p->seg[i] = s;
  }
  p->drawn = -9;
  if (A.id != ARENA_BATTLE) { /* (the battle map builds its own poles) */
  cw_node pole = cw_node_new("MeshInstance3D", "Pole");
  cw_node_set(pole, "primitive", cw_val_enum("CYLINDER"));
  cw_node_set(pole, "size", cw_val_vec3(0.05f, 3.4f, 0));
  cw_node_set(pole, "color", cw_val_color(0.55f, 0.56f, 0.58f, 1));
  cw_node_set(pole, "metalness", cw_val_float(0.8f));
  cw_node_set(pole, "roughness", cw_val_float(0.35f));
  cw_node3d_set_position(pole, v3(0, 1.7f, 0));
  cw_node_add_child(p->node, pole);
  }
  p->flag = cw_node_new("MeshInstance3D", "Flag");
  cw_node_set(p->flag, "primitive", cw_val_enum("BOX"));
  cw_node_set(p->flag, "size", cw_val_vec3(0.9f, 0.55f, 0.03f));
  cw_node_set(p->flag, "roughness", cw_val_float(0.9f));
  cw_node_add_child(p->node, p->flag);
  post_look(p);
}
/* the posts on today's arenas: a home post a side on the two spawn points farthest apart, and one capture post on the
 * ground the arena is built round (the navmesh nearest its middle) */
static void battle_posts(void) {
  memset(BT.posts, 0, sizeof BT.posts);
  BT.nposts = 0;
  if (A.id == ARENA_BATTLE) { /* the battle map: its "post <i> <name> x y z radius home" lines (home -1: a capture post) */
    size_t len;
    char *txt = cw_asset_load("levels/battle.lvl", &len);
    for (char *ln = txt, *e; ln && ln < txt + len; ln = e + 1) {
      e = memchr(ln, '\n', (size_t)(txt + len - ln));
      if (!e) e = txt + len;
      post *p = &BT.posts[BT.nposts];
      int idx, home;
      float x, y, z, r;
      char nm[16], line[160];
      snprintf(line, sizeof line, "%.*s", (int)CW_MIN((size_t)(e - ln), sizeof line - 1), ln); /* (the file is not NUL-terminated) */
      if (BT.nposts < MAX_POSTS && sscanf(line, "post %d %15s %f %f %f %f %d", &idx, nm, &x, &y, &z, &r, &home) == 7) {
        p->pos = v3(x, y, z), p->r = r, p->home = home, p->owner = home, p->meter = home < 0 ? 0 : home ? 1 : -1;
        snprintf(p->nm, sizeof p->nm, "%s", nm), p->name = p->nm;
        BT.nposts++;
      }
    }
    free(txt);
    if (BT.nposts >= 2) { for (int i = 0; i < BT.nposts; i++) BT.posts[i].push = -1, post_make(&BT.posts[i]); return; }
    BT.nposts = 0;
  }
  int a = 0, b = 0;
  float bd = -1;
  for (int i = 0; i < A.nspawns; i++)
    for (int j = i + 1; j < A.nspawns; j++) {
      float d = v3len(v3sub(A.spawns[i], A.spawns[j]));
      if (d > bd) bd = d, a = i, b = j;
    }
  t3_vec3 mid = v3(0, 0, 0), q;
  if (cw_nav_nearest(A.nav, mid, NULL, &q)) mid = q;
  post *p = &BT.posts[BT.nposts++];
  p->pos = A.spawns[a], p->home = 0, p->owner = 0, p->meter = -1, p->name = "BLUE BASE";
  p = &BT.posts[BT.nposts++];
  p->pos = mid, p->home = -1, p->owner = -1, p->meter = 0, p->name = "CENTRE";
  p = &BT.posts[BT.nposts++];
  p->pos = A.spawns[b], p->home = 1, p->owner = 1, p->meter = 1, p->name = "RED BASE";
  for (int i = 0; i < BT.nposts; i++) BT.posts[i].r = POST_R, BT.posts[i].push = -1, post_make(&BT.posts[i]);
}
static void battle_begin(void) {
  battle_posts();
  int live = battle_live(), pool = POOL_CHOICES[CW_CLAMP(BT.pool_setting, 0, 3)];
  const char *e = cw_test_env("ARENA_BATTLE_POOL");
  if (e && atoi(e) >= 0) pool = atoi(e);
  e = cw_test_env("ARENA_COUNT_RULE");
  if (e) BT.sw_reference = !strcmp(e, "reference");
  e = cw_test_env("ARENA_TAKEOVER");
  if (e) BT.sw_no_takeover = atoi(e) == 0;
  for (int s = 0; s < 2; s++) {
    BT.pool[s] = pool, BT.live[s] = live, BT.alive[s] = 0, BT.hold_all_t[s] = 0, BT.ls_ticks[s] = 0;
    BT.zero_tick[s] = BT.pool0_tick[s] = -1, BT.bleeds[s] = BT.spent[s] = 0, BT.ls_called[s] = 0;
  }
  BT.bleed_t = 180, BT.end_tick = -1, BT.reveal_t = 600, BT.reveals = 0, BT.ping_t = 1 << 20, BT.ping_side = -1, BT.banner_t = 0;
  for (int h = 0; h < MAXH; h++) SEAT[h] = h, BT.spawn_post[h] = -1;
  BT.pool_start = pool;
  memset(TK, 0, sizeof TK);
  memset(tk_stats, 0, sizeof tk_stats);
  memset(tk_start, 0, sizeof tk_start), memset(tk_after, 0, sizeof tk_after);
}
static void battle_call(const char *s) { snprintf(BT.banner, sizeof BT.banner, "%s", s); BT.banner_t = 60 * 4; }
/* the rules each tick: who is alive, capture, bleed, holding every post, the last stand, the reveal pings */
static void battle_tick(void) {
  for (int s = 0; s < 2; s++) BT.alive[s] = 0;
  for (int i = 0; i < G.nf; i++) if (F(i)->active && F(i)->alive) BT.alive[F(i)->team & 1]++;
  int held[2] = { 0, 0 };
  for (int pi = 0; pi < BT.nposts; pi++) {
    post *p = &BT.posts[pi];
    if (p->home < 0) {
      int n[2] = { 0, 0 };
      for (int i = 0; i < G.nf; i++) {
        const fighter *f = F(i);
        if (!f->active || !f->alive) continue;
        float dx = f->mv.position.x - p->pos.x, dz = f->mv.position.z - p->pos.z, dy = f->mv.position.y - p->pos.y;
        if (dx * dx + dz * dz <= p->r * p->r && fabsf(dy) < 2.5f) n[f->team & 1]++;
      }
      p->push = n[0] && n[1] ? 2 : n[0] ? 0 : n[1] ? 1 : -1;
      if (p->push == 0 || p->push == 1) {
        int s = p->push, c = n[s];
        float rate = (c >= 3 ? 2.0f : c == 2 ? 1.5f : 1.0f) / CAP_TICKS, want = s ? 1.0f : -1.0f;
        p->meter = want > 0 ? fminf(1, p->meter + rate) : fmaxf(-1, p->meter - rate);
        /* a held post is neutral once the meter crosses the middle, and taken when it is full */
        int owner = p->owner;
        if (p->meter <= -1) owner = 0;
        else if (p->meter >= 1) owner = 1;
        else if ((owner == 0 && p->meter >= 0) || (owner == 1 && p->meter <= 0)) owner = -1;
        if (owner != p->owner) {
          char line[48];
          if (owner >= 0) snprintf(line, sizeof line, "%s TOOK THE %s", BSIDE_NAME[owner], p->name);
          else snprintf(line, sizeof line, "THE %s IS NEUTRAL", p->name);
          feed("%s%s", line, "");
          battle_call(line);
          p->owner = owner, p->changes++;
        }
      }
      post_look(p);
    }
    if (p->owner >= 0) held[p->owner]++;
  }
  /* bleed: a point every 3 s off a side while the other holds more than half the posts, down to 0 and no further */
  if (--BT.bleed_t <= 0) {
    BT.bleed_t = 180;
    for (int s = 0; s < 2; s++)
      if (held[!s] * 2 > BT.nposts && BT.pool[s] > 0) BT.pool[s]--, BT.bleeds[s]++;
  }
  /* every post held for 20 s: the other side's pool goes to 0 and its last stand starts */
  for (int s = 0; s < 2; s++) {
    BT.hold_all_t[s] = held[s] == BT.nposts ? BT.hold_all_t[s] + 1 : 0;
    if (BT.hold_all_t[s] == 60 * 20 && BT.pool[!s] > 0) BT.pool[!s] = 0, feed("%s holds every post", BSIDE_NAME[s], "");
  }
  for (int s = 0; s < 2; s++) {
    if (BT.pool[s] == 0 && BT.pool0_tick[s] < 0) BT.pool0_tick[s] = G.match_ticks;
    if (BT.pool[s] == 0 && BT.alive[s] > 0) {
      BT.ls_ticks[s]++;
      if (!BT.ls_called[s] && !BT.sw_reference) {
        char line[48];
        snprintf(line, sizeof line, "%s: LAST STAND", BSIDE_NAME[s]);
        battle_call(line);
        feed("%s has no reinforcements left: %s", BSIDE_NAME[s], "the last stand");
        BT.ls_called[s] = 1;
      }
    }
    if (BT.pool[s] == 0 && BT.alive[s] == 0 && BT.zero_tick[s] < 0) BT.zero_tick[s] = G.match_ticks;
  }
  /* the reveal: a side in its last stand with 3 or fewer alive is shown to the other every 10 s (a soft pulse) */
  BT.ping_t++;
  if (--BT.reveal_t <= 0) {
    BT.reveal_t = 600;
    for (int s = 0; s < 2; s++) {
      if (BT.pool[s] > 0 || BT.alive[s] == 0 || BT.alive[s] > 3 || BT.sw_reference) continue;
      for (int i = 0; i < G.nf; i++) BT.ping[i] = F(i)->mv.position;
      BT.ping_side = s, BT.ping_t = 0, BT.reveals++;
    }
  }
  if (tk_stats[0] > 0 && G.match_ticks == tk_stats[3] + 90) {
    const fighter *s0 = SF(0);
    tk_after[0] = s0->mv.position.x, tk_after[1] = s0->mv.position.z, tk_after[2] = (float)BT.alive[0], tk_after[3] = s0->alive && G.state == ST_PLAY;
  }
  if (G.match_ticks % 60 == 0) { /* the background battle thins with a side's strength */
    float k[2];
    for (int s = 0; s < 2; s++) k[s] = (float)(BT.pool[s] + BT.alive[s]) / (float)CW_MAX(1, BT.pool_start + BT.live[s]);
    crowd_strength(k[0], k[1]);
  }
  if (BT.banner_t > 0) BT.banner_t--;
  for (int s = 0; s < 2; s++) BT.d_pool[s] = BT.pool[s], BT.d_alive[s] = BT.alive[s];
  for (int i = 0; i < MAX_POSTS; i++) BT.d_owner[i] = i < BT.nposts ? BT.posts[i].owner : -9;
}
/* the end: a side loses when its pool is 0 and its last soldier is down (sw count_rule reference: at 0, whoever is
 * still standing); at the time limit the side with more reinforcements plus soldiers alive wins, equal is a draw (2) */
static void end_match(int winner);
static void battle_check(void) {
  battle_tick();
  bool lose[2];
  for (int s = 0; s < 2; s++) lose[s] = BT.sw_reference ? BT.pool[s] == 0 : BT.pool[s] == 0 && BT.alive[s] == 0;
  if (lose[0] || lose[1]) {
    BT.end_tick = G.match_ticks;
    end_match(lose[0] && lose[1] ? 2 : lose[0] ? 1 : 0);
    return;
  }
  if (G.match_ticks >= G.time_limit) {
    int a = BT.pool[0] + BT.alive[0], b = BT.pool[1] + BT.alive[1];
    BT.end_tick = G.match_ticks;
    end_match(a == b ? 2 : a > b ? 0 : 1);
  }
}
/* where a bot of the battle goes when it sees nobody: health when hurt; a hidden last few of the enemy at their last
 * ping; its own side's last stand regroups and holds at a post it owns; else the nearest post its side does not hold
 * (or one of its own the enemy is in), and when it holds them all, it holds them */
static bool battle_bot_goal(fighter *f, int idx, t3_vec3 *g) {
  int s = f->team & 1;
  if (f->hp < 50) {
    int hi = -1;
    float hd = 25;
    for (int i = 0; i < A.nitems; i++) {
      const item *it = &A.items[i];
      if (it->kind != ITEM_HEALTH || it->respawn > 0) continue;
      float d = v3len(v3sub(it->pos, f->mv.position));
      if (d < hd) hd = d, hi = i;
    }
    if (hi >= 0) { *g = A.items[hi].pos; return true; }
  }
  if (BT.ping_side == !s && BT.ping_t < 600) {
    float bd = 1e9f;
    int bi = -1;
    for (int i = 0; i < G.nf; i++) {
      const fighter *o = F(i);
      if (!o->active || !o->alive || (o->team & 1) != !s) continue;
      float d = v3len(v3sub(BT.ping[i], f->mv.position));
      if (d < bd) bd = d, bi = i;
    }
    if (bi >= 0) { *g = BT.ping[bi]; return true; }
  }
  if (BT.pool[!s] == 0 && !BT.sw_reference) { /* the enemy is in its last stand: hunt its soldiers down, the nearest first */
    float hd = 1e9f;
    int hi = -1;
    for (int i = 0; i < G.nf; i++) {
      const fighter *o = F(i);
      if (!o->active || !o->alive || (o->team & 1) == s) continue;
      float d = v3len(v3sub(o->mv.position, f->mv.position));
      if (d < hd) hd = d, hi = i;
    }
    if (hi >= 0) { *g = F(hi)->mv.position; return true; }
  }
  int best = -1;
  float bd = 1e9f;
  bool last = BT.pool[s] == 0;
  for (int i = 0; i < BT.nposts; i++) {
    const post *p = &BT.posts[i];
    float d = v3len(v3sub(p->pos, f->mv.position));
    bool want = last ? p->owner == s : (p->home < 0 && (p->owner != s || p->push == !s || p->push == 2));
    if (want && d < bd) bd = d, best = i;
  }
  if (best < 0 && !last && idx % 3 == 0) { /* every post that can be taken is ours: a third of the side presses on the enemy's home */
    for (int i = 0; i < BT.nposts; i++) if (BT.posts[i].home == !s) best = i;
  }
  if (best < 0) /* every post that can be taken is ours: hold them (each bot its own, by slot) */
    for (int i = 0, k = 0; i < BT.nposts; i++) if (BT.posts[i].owner == s && BT.posts[i].home < 0) { if (best < 0 || (idx + k) % 2 == 0) best = i; k++; }
  if (best < 0) return false;
  /* a spot of its own in the ring, by slot (squads spread over the ring instead of piling on the pole) */
  const post *p = &BT.posts[best];
  float sa, ca, r = 1.0f + (float)(idx % 3) * 1.3f;
  cw_sincos((float)idx * 2.39996f, &sa, &ca);
  t3_vec3 c = v3add(p->pos, v3(sa * r, 0, ca * r)), q;
  *g = cw_nav_nearest(A.nav, c, NULL, &q) && fabsf(q.y - p->pos.y) < 1.5f ? q : p->pos;
  return true;
}

static void begin_match(void) {
  build_world(G.arena_id);
  soak_note();
  cw_rng_seed(&G.rng, 0xA2E4A + (uint32_t)G.arena_id * 977u + (uint32_t)G.mode);
  G.ticks = 0;
  G.match_ticks = 0;
  G.over_ticks = 0;
  G.hash = 0;
  G.winner = -1;
  G.st.last_kill = 0;
  for (int s = 0; s < MAX_SPAWNS; s++) G.spawn_used[s] = -100000;
  G.team_score[0] = G.team_score[1] = 0;
  G.wave = 0;
  G.away_skips = 0;
  G.present = G.players;
  G.wave_state = 0;
  G.wave_timer = 60 * 3;
  G.lives = 4 + 3 * G.players;
  memset(G.feed, 0, sizeof G.feed);
  memset(G.p1, 0, sizeof G.p1);
  memset(G.hit_marker, 0, sizeof G.hit_marker);
  memset(G.kill_flash, 0, sizeof G.kill_flash);
  memset(G.head_marker, 0, sizeof G.head_marker);
  memset(G.pick_t, 0, sizeof G.pick_t);
  memset(G.killed_by, 0, sizeof G.killed_by);
  for (int h = 0; h < MAXH; h++) G.kc_who[h] = -1;
  memset(G.dmg_flash, 0, sizeof G.dmg_flash);
  memset(G.dmg_want, 0, sizeof G.dmg_want);
  memset(G.dmg_hold, 0, sizeof G.dmg_hold);
  memset(G.aim, 0, sizeof G.aim);
  memset(proxy_aim, 0, sizeof proxy_aim);
  memset(proxy_thumb, 0, sizeof proxy_thumb);
  G.frag_limit = G.mode == MODE_TDM ? 40 : 25; /* about five minutes against bots at 4 fighters (FUN-RUBRIC FP17; 15 frags took 3 minutes) */
  G.time_limit = battle() ? 60 * 60 * 20 : 60 * 60 * 8; /* Battle: 20 minutes, decided by strength (battle_check) */
#ifdef ARENA_AUTOPILOT
  { /* ARENA_FRAGS and ARENA_SECS shorten the autopilot's matches (tests, program tables) */
    const char *e;
    if ((e = getenv("ARENA_FRAGS"))) G.frag_limit = CW_MAX(1, atoi(e));
    if ((e = getenv("ARENA_SECS"))) G.time_limit = 60 * CW_MAX(5, atoi(e));
  }
#endif
  if (cw_node_valid(A.water)) { /* planar reflection and refraction per view: only when the screen is not split three or four ways */
    bool rich = G.players < 3;
    cw_node_set(A.water, "reflection", cw_val_bool(rich));
    cw_node_set(A.water, "refraction", cw_val_bool(rich));
  }
  crowd_set(battle()); /* the background battle: Battle only */
  if (battle()) battle_begin();
  setup_fighters();
#ifdef ARENA_AUTOPILOT
  { /* ARENA_CROWD=n: n more soldiers running circles round the spawn points, the players held still looking at them
     * (tests/arenafps-anim-cost.mjs: what n soldiers on screen cost) */
    const char *e = getenv("ARENA_CROWD");
    if (e) soldier_crowd(atoi(e), G.world, A.spawns, A.nspawns);
  }
#endif
  power_place();
  if (battle()) { PW.timer = 1 << 30; if (cw_node_valid(PW.node)) cw_node_set(PW.node, "visible", cw_val_bool(false)); } /* Battle: no power-ups */
  if (G.mode == MODE_COOP) PW.timer = 1 << 30; /* co-op: no power-ups (a wave bot with quad damage is no fun for anyone) */
#ifdef ARENA_AUTOPILOT
  if (getenv("ARENA_NO_POWER")) PW.timer = 1 << 30; /* the power-up measure's control (before/after) */
#endif
  memset(gren_stats, 0, sizeof gren_stats), memset(bash_stats, 0, sizeof bash_stats), memset(pow_stats, 0, sizeof pow_stats);
  /* the rules: instagib clears the floor of pickups and power-ups (everyone holds the rail), rockets only clears the other
   * weapons (health, armour and ammo with its grenade stay) */
  for (int i = 0; i < A.nitems; i++) {
    item *it = &A.items[i];
    bool off = G.rules == RULE_INSTAGIB || (G.rules == RULE_ROCKETS && (it->kind == ITEM_SCATTER || it->kind == ITEM_ROCKET || it->kind == ITEM_RAIL));
    if (!off) continue;
    it->respawn = 1 << 30;
    cw_node_set(it->node, "visible", cw_val_bool(false));
    if (cw_node_valid(it->light)) cw_node_set(it->light, "energy", cw_val_float(0));
    if (cw_node_valid(it->spot)) cw_node_set(it->spot, "visible", cw_val_bool(false));
    if (cw_node_valid(it->spot_glow)) cw_node_set(it->spot_glow, "visible", cw_val_bool(false));
  }
  if (G.rules == RULE_INSTAGIB) PW.timer = 1 << 30;
  G.state = ST_PLAY;
}

static void end_match(int winner) {
  rumble_stop_all();
  G.state = ST_OVER;
  G.winner = winner;
  G.over_ticks = 0;
}

static void check_rules(void) {
  if (battle()) { battle_check(); return; }
  if (G.mode == MODE_COOP) {
    int alive_humans = 0;
    for (int i = 0; i < G.players; i++) if (F(i)->alive) alive_humans++;
    int bots_alive = 0;
    for (int i = G.players; i < MAXF; i++) if (F(i)->active && F(i)->alive) bots_alive++;
    if (G.wave_state == 0) { /* between waves */
      if (--G.wave_timer <= 0) { spawn_wave(); G.wave_state = 1; }
    } else if (bots_alive == 0) {
      if (G.wave >= 6) { end_match(0); return; }
      G.wave_state = 0;
      G.wave_timer = 60 * 5;
      /* wave bonus: heal everyone */
      for (int i = 0; i < G.players; i++) if (F(i)->alive) F(i)->hp = CW_MIN(100, F(i)->hp + 40), F(i)->ammo[0] = CW_MAX(F(i)->ammo[0], 120);
      for (int i = G.players; i < MAXF; i++) if (F(i)->active) {
        F(i)->active = false;
        if (cw_node_valid(F(i)->body)) cw_node_free(F(i)->body);
        F(i)->body = (cw_node){ 0 };
        soldier_free(i);
      }
    }
    if (alive_humans == 0 && G.lives <= 0) end_match(-1);
    return;
  }
  if (G.mode == MODE_TDM) {
    for (int t = 0; t < 2; t++) if (G.team_score[t] >= G.frag_limit) { end_match(t); return; }
  } else {
    for (int i = 0; i < G.nf; i++) if (F(i)->score >= G.frag_limit) { end_match(i); return; }
  }
  if (G.match_ticks >= G.time_limit) {
    int w = 0;
    if (G.mode == MODE_TDM) w = G.team_score[1] > G.team_score[0];
    else { int bs = -999; for (int i = 0; i < G.nf; i++) if (F(i)->score > bs) bs = F(i)->score, w = i; }
    end_match(w);
  }
}

/* the camera stability check (FUN-RUBRIC, the camera-stability rule): player 1's camera as drawn, every tick. cam.gate counts
 * ticks at rest (alive, the fighter's position, yaw and pitch unchanged for a second), at-rest ticks where the camera moved
 * over 1 mm (position plus the point 1 m ahead plus the up vector), ticks in motion, and ticks in motion where it jumped over
 * 0.6 m or its step changed by over 8 cm; cam.jit the largest at-rest step, step in motion and step change (metres);
 * cam.events the ticks the camera was let move for an event: none, nothing shakes it (a hit or a blast is felt in the pad).
 * ARENA_CAM_BUG=1 (native, the check's control) puts a 2 mm idle sway on the camera at rest. */
static int32_t cam_gate[4], cam_events, cam_jerk_at[4];
static float cam_jit[3];
static struct { float last[9], step[9]; t3_vec3 pos; float yaw, pitch; int still, n; bool floor; } CAMS;
static void camgate(const fighter *f, t3_vec3 at, t3_vec3 d, t3_vec3 up, bool event) {
  float v[9] = { at.x, at.y, at.z, at.x + d.x, at.y + d.y, at.z + d.z, up.x, up.y, up.z };
  bool same = f->alive && v3len(v3sub(f->mv.position, CAMS.pos)) < 1e-5f && fabsf(f->yaw - CAMS.yaw) < 1e-6f && fabsf(f->pitch - CAMS.pitch) < 1e-6f;
  CAMS.still = same ? CAMS.still + 1 : 0;
  CAMS.pos = f->mv.position, CAMS.yaw = f->yaw, CAMS.pitch = f->pitch;
  float d2 = 0, j2 = 0, st[9];
  for (int k = 0; k < 9; k++) {
    st[k] = v[k] - CAMS.last[k];
    d2 += st[k] * st[k];
    j2 += (st[k] - CAMS.step[k]) * (st[k] - CAMS.step[k]);
  }
  float dd = sqrtf(d2), jerk = 0;
  for (int k = 0; k < 3; k++) jerk += (st[k] - CAMS.step[k]) * (st[k] - CAMS.step[k]); /* the eye's own change of step */
  jerk = sqrtf(jerk);
  (void)j2;
  bool fresh = !f->alive || f->since_spawn < 2;
  if (CAMS.n > 1 && !fresh) {
    if (event) cam_events++;
    else if (CAMS.still >= 60) {
      cam_gate[0]++;
      if (dd > 0.001f) cam_gate[1]++;
      cam_jit[0] = fmaxf(cam_jit[0], dd);
    } else {
      cam_gate[2]++;
      /* a take-off or a landing changes the eye's vertical speed in one tick, as it should: a jerk counts on the ground only */
      bool grounded = f->mv.on_floor && CAMS.floor;
      if (dd > 0.6f || (jerk > 0.15f && grounded)) {
        if (cam_gate[3] < 4) cam_jerk_at[cam_gate[3]] = G.ticks;
        cam_gate[3]++;
      }
      cam_jit[1] = fmaxf(cam_jit[1], dd);
      cam_jit[2] = fmaxf(cam_jit[2], jerk);
    }
  }
  for (int k = 0; k < 9; k++) CAMS.step[k] = fresh ? 0 : st[k], CAMS.last[k] = v[k];
  CAMS.floor = f->mv.on_floor;
  CAMS.n++;
}

/* the shake check (cam.offset): every view's camera as drawn (the camera node's global transform) against the gameplay
 * view (the eye, eased over steps, looking along the fighter's yaw and pitch, level), on the ticks within half a second
 * of an event that used to shake it (a hit taken, a blast within 14 m of the eye). cam.offset: those event ticks, the
 * ones whose drawn camera was off the gameplay view by over 0.1 mm or 0.1 milliradian, and the largest offset seen
 * (in 0.1 mm and 0.1 milliradian units, eye plus look and roll). Nothing moves the camera on an event: [n, 0, 0]. */
static void offsetgate(int h, const fighter *f, t3_vec3 e, t3_vec3 d) {
  if (!f->alive || G.state != ST_PLAY || G.ticks > ev_until[h]) return;
  const t3_mat4 *m = cw_node3d_global_transform(G.cam[h]);
  if (!m) return;
  t3_vec3 right = v3norm(v3cross(d, v3(0, 1, 0))), up = v3cross(right, d);
  t3_vec3 pos = v3(m->e[12], m->e[13], m->e[14]), fwd = v3norm(v3(-m->e[8], -m->e[9], -m->e[10])), mup = v3norm(v3(m->e[4], m->e[5], m->e[6]));
  float off = v3len(v3sub(pos, e)) + v3len(v3sub(fwd, d)) + v3len(v3sub(mup, up));
  cam_offset[0]++;
  if (off > 1e-4f) cam_offset[1]++;
  int32_t um = (int32_t)(off * 1e4f);
  if (um > cam_offset[2]) cam_offset[2] = um;
}

/* the killer cam's view per person (drawn only): the dead view's eye, yaw and pitch as eased, and as drawn this tick */
static struct { float yaw[MAXH], pitch[MAXH], yaw_now[MAXH], pitch_now[MAXH]; t3_vec3 eye[MAXH]; } KC;
/* killcam: dead ticks (after the turn) of people downed by someone, and those with the killer inside their view */
static int32_t killcam[2];
/* a point in person h's view as drawn: its pixel position, and whether it is in front and inside the view's rectangle */
static bool kc_project(int h, t3_vec3 p, float *sx, float *sy) {
  cw_rect r = cw_shell_player_rect(h);
  t3_vec3 d = look_dir(KC.yaw_now[h], KC.pitch_now[h]), rt = v3norm(v3cross(d, v3(0, 1, 0))), up = v3cross(rt, d), rel = v3sub(p, KC.eye[h]);
  float z = v3dot(rel, d);
  if (z < 0.3f || r.h < 1) return false;
  float fl = 1.0f / tanf(78.0f * 0.5f * 3.14159265f / 180.0f) * r.h * 0.5f;
  *sx = r.x + r.w * 0.5f + v3dot(rel, rt) / z * fl, *sy = r.y + r.h * 0.5f - v3dot(rel, up) / z * fl;
  return *sx >= r.x && *sx <= r.x + r.w && *sy >= r.y && *sy <= r.y + r.h;
}

/* cameras and bodies follow the fighters: drawn from the tick's state */
/* the muzzle light stands out past the barrel (at the barrel it bleaches the first person gun), followed every tick with the
 * fighter's aim, and short of the first surface ahead: placed 0.9 m out on each shot, a light swung through a wall by a turn
 * or a step lit the wall and went dark behind it shot by shot, a near wall or floor blinking at the fire rate. The reach
 * eases toward what the ray allows, so a wall coming close dims the light in over a few ticks instead of jumping */
static void muzzle_place(fighter *f) {
  t3_vec3 e = eye_pos(f), d0 = look_dir(f->yaw, f->pitch), right = v3norm(v3cross(d0, v3(0, 1, 0)));
  t3_vec3 muzzle = v3add(e, v3add(v3scale(d0, 0.5f), v3scale(right, 0.12f)));
  float want = 0.9f;
  t3_vec3 hp;
  if (arena_ray(e, v3add(e, v3scale(d0, 1.4f + 0.6f)), &hp, NULL)) want = CW_CLAMP(v3len(v3sub(hp, e)) - 0.5f - 0.6f, 0.0f, 0.9f);
  f->mreach += (want - f->mreach) * (want < f->mreach ? 0.5f : 0.2f);
  t3_vec3 at = v3add(muzzle, v3scale(d0, f->mreach));
  cw_node3d_set_position(f->light, at);
  /* a light a hand's width from a wall lights it white, and a step or a turn of a few centimetres along the wall swings it
   * from bright to dark frame by frame (a fighter hugging a wall strobed it): the light keeps its energy only as far as
   * the nearest surface around it allows, eased over a quarter second */
  static const t3_vec3 DIRS[5] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 } };
  float dmin = 1.5f;
  for (int k = 0; k < 5; k++) {
    t3_vec3 hp2;
    if (arena_ray(at, v3add(at, v3scale(DIRS[k], 1.5f)), &hp2, NULL)) dmin = fminf(dmin, v3len(v3sub(hp2, at)));
  }
  float keep = CW_CLAMP((dmin - 0.3f) / 1.2f, 0.0f, 1.0f);
  f->mnear += (keep - f->mnear) * 0.07f;
}

/* ── Battle: a downed person at 0 reinforcements takes over a teammate bot (TK) ─────────────────────────────────────── */

static bool tk_claimed(int h, int bot) { /* another seat already has this bot picked or on its way */
  for (int o = 0; o < MAXH; o++)
    if (o != h && (TK[o].phase == 1 || TK[o].phase == 2) && TK[o].ncand > 0 && TK[o].cand[TK[o].pick] == bot) return true;
  return false;
}
/* the living teammate bots nearest where the person fell (one hit in the last second counts 15 m further: it is in a
 * fight), the healthier first on a tie; up to four */
static void tk_candidates(int h, const fighter *dead) {
  TK[h].ncand = 0;
  float sc[4];
  for (int i = 0; i < G.nf; i++) {
    const fighter *o = F(i);
    if (!o->active || !o->alive || o->human >= 0 || (o->team & 1) != (dead->team & 1) || tk_claimed(h, i)) continue;
    float s = v3len(v3sub(o->mv.position, dead->mv.position)) + (o->hit_t < 60 ? 15.0f : 0.0f) - (float)o->hp * 0.001f;
    int k = TK[h].ncand < 4 ? TK[h].ncand++ : 4;
    if (k == 4) { if (s >= sc[3]) continue; k = 3; }
    while (k > 0 && sc[k - 1] > s) sc[k] = sc[k - 1], TK[h].cand[k] = TK[h].cand[k - 1], k--;
    sc[k] = s, TK[h].cand[k] = i;
  }
  TK[h].pick = 0;
}
static void tk_handover(int h, fighter *old, int bi) {
  fighter *b = F(bi);
  b->human = h, old->human = -1;
  SEAT[h] = bi;
  b->spawn_guard = 120; /* the spawn guard's shimmer, ended by the first shot (do_fire) */
  b->target = -1, b->npath = 0, b->ticket = 0;
  memset(&G.aim[h], 0, sizeof G.aim[h]);
  memset(&proxy_aim[h], 0, sizeof proxy_aim[h]);
  proxy_thumb[h] = cw_v2(0, 0);
  G.dmg_flash[h] = G.dmg_want[h] = 0, G.dmg_hold[h] = 0;
  G.killed_by[h][0] = 0, G.kc_who[h] = -1;
  KC.yaw[h] = b->yaw, KC.pitch[h] = b->pitch;
  TK[h].phase = 0, TK[h].t = 0;
  tk_stats[0]++, tk_stats[3] = G.match_ticks;
  tk_start[0] = b->mv.position.x, tk_start[1] = b->mv.position.z, tk_start[2] = b->yaw;
  char line[48];
  snprintf(line, sizeof line, "P%d took over %s", h + 1, b->name);
  feed("%s%s", line, "");
}
/* seat h's soldier is down and its side has no reinforcements: pick, move the view (0.8 s), hand over; or watch */
static void tk_tick(int h, fighter *f) {
  int sel = 0;
  if (f->human >= 0) sel = cw_action_just_pressed(h, G.fire) - cw_action_just_pressed(h, G.grenade); /* the shoulders */
  if (TK[h].phase == 0) { tk_candidates(h, f); TK[h].phase = TK[h].ncand ? 1 : 3; TK[h].watch = -1; }
  if (TK[h].phase == 1) {
    if (TK[h].ncand == 0 || !F(TK[h].cand[TK[h].pick])->alive) tk_candidates(h, f);
    if (TK[h].ncand == 0) { TK[h].phase = 3; return; }
    if (sel) TK[h].pick = (TK[h].pick + TK[h].ncand + sel) % TK[h].ncand;
    if (f->dead_ticks >= 150) {
      TK[h].phase = 2, TK[h].t = 0;
      TK[h].from = TK[h].cam = KC.eye[h], TK[h].yaw = KC.yaw_now[h], TK[h].pitch = KC.pitch_now[h];
    }
    return;
  }
  if (TK[h].phase == 2) {
    int bi = TK[h].cand[TK[h].pick];
    fighter *b = F(bi);
    if (!b->alive) { /* it fell on the way: the next one, from where the view is */
      tk_candidates(h, f);
      if (TK[h].ncand == 0) { TK[h].phase = 3; return; }
      TK[h].from = TK[h].cam, TK[h].t = 0;
      bi = TK[h].cand[0], b = F(bi);
    }
    TK[h].t++;
    float u = CW_MIN(1.0f, (float)TK[h].t / 48.0f), s = u * u * (3 - 2 * u), arc = cw_sin(3.14159265f * s);
    t3_vec3 eye = eye_pos(b), fw = look_dir(b->yaw, 0);
    /* rises off the fallen body, over a short arc to behind the soldier's head, and settles into its eyes */
    TK[h].cam = v3add(v3add(v3add(TK[h].from, v3scale(v3sub(eye, TK[h].from), s)), v3(0, 0.7f * arc, 0)), v3scale(fw, -0.6f * arc));
    float dy = cw_wrap_angle(b->yaw - TK[h].yaw), dp = b->pitch - TK[h].pitch;
    TK[h].yaw += CW_CLAMP(dy, -0.06f, 0.06f), TK[h].pitch += CW_CLAMP(dp, -0.04f, 0.04f); /* 3.6 and 2.4 rad/s at most */
    if (u >= 1 && fabsf(cw_wrap_angle(b->yaw - TK[h].yaw)) < 0.02f && fabsf(b->pitch - TK[h].pitch) < 0.02f) tk_handover(h, f, bi);
    return;
  }
  /* watching: a living person of the side, else a bot of it, else whoever downed this one; the shoulders change who */
  int team = f->team & 1, list[MAXF], n = 0;
  for (int i = 0; i < G.nf; i++) if (F(i)->active && F(i)->alive && (F(i)->team & 1) == team && F(i)->human >= 0) list[n++] = i;
  for (int i = 0; i < G.nf; i++) if (F(i)->active && F(i)->alive && (F(i)->team & 1) == team && F(i)->human < 0) list[n++] = i;
  if (n == 0) { TK[h].watch = G.kc_who[h]; return; }
  int k = 0;
  for (int j = 0; j < n; j++) if (list[j] == TK[h].watch) k = j;
  if (TK[h].watch < 0 || !F(TK[h].watch)->alive) k = 0;
  if (sel) k = (k + n + sel) % n;
  TK[h].watch = list[k];
}
static bool tk_target(int i) { for (int h = 0; h < MAXH; h++) if (TK[h].phase == 2 && TK[h].ncand > 0 && TK[h].cand[TK[h].pick] == i) return true; return false; }
/* a downed soldier of the battle: respawn at a post for a point, or (at 0) a person takes over a bot */
static void battle_dead(fighter *f) {
  int h = f->human, s = f->team & 1;
  if (h >= 0 && h < MAXH && f->human >= 0) { /* the respawn panel: the d-pad picks the post */
    int d = cw_action_just_pressed(h, G.mv_r) - cw_action_just_pressed(h, G.mv_l);
    if (d) {
      int cur = BT.spawn_post[h] >= 0 ? BT.spawn_post[h] : post_front(s);
      for (int k = 1; k <= BT.nposts; k++) {
        int c = (cur + BT.nposts + d * k) % BT.nposts;
        if (BT.posts[c].owner == s) { BT.spawn_post[h] = c; break; }
      }
      fx_play_ui(SND_TICK, 0.5f);
    }
  }
  bool wait = f->dead_ticks > (h >= 0 ? 150 : 120);
  if (BT.pool[s] > 0) {
    if (wait) {
      if (!BT.sw_reference) BT.pool[s]--, BT.spent[s]++; /* a point for each soldier back on the field */
      spawn_fighter(f);
    }
    return;
  }
  if (h >= 0 && h < MAXH && !BT.sw_reference) {
    if (BT.sw_no_takeover) { if (TK[h].phase != 3) TK[h].phase = 3, TK[h].watch = -1; }
    tk_tick(h, f);
  }
}
/* the battle's test commands (debug command battle, and ARENA_BATTLE_AT="tick:cmd args|tick:cmd args" on every host):
 *   pool <side> <n>     the side's reinforcements
 *   alive <side> <n>    that many of the side stay on the field (people first), the rest are taken off without a death
 *   down <fighter>      down a fighter;  downseat <seat>  down a seat's soldier */
static const char *battle_cmd(int argc, const char **argv) {
  if (argc < 2) return "usage: battle pool <side> <n> | alive <side> <n> | down <i> | downseat <seat>";
  int a = argc > 2 ? atoi(argv[2]) : 0, b = argc > 3 ? atoi(argv[3]) : 0;
  if (!strcmp(argv[1], "pool") && argc > 3) { BT.pool[a & 1] = CW_MAX(0, b); return NULL; }
  if (!strcmp(argv[1], "alive") && argc > 3) {
    int keep = 0;
    for (int pass = 0; pass < 2; pass++)
      for (int i = 0; i < G.nf; i++) {
        fighter *f = F(i);
        if (!f->active || !f->alive || (f->team & 1) != (a & 1) || (pass == 0) != (f->human >= 0)) continue;
        if (keep < b) { keep++; continue; }
        f->alive = false, f->dead_ticks = 1000;
        if (cw_node_valid(f->body)) cw_node_set(f->body, "visible", cw_val_bool(false));
      }
    return NULL;
  }
  if ((!strcmp(argv[1], "down") || !strcmp(argv[1], "downseat")) && argc > 2) {
    int i = !strcmp(argv[1], "downseat") ? SEAT[CW_CLAMP(a, 0, MAXH - 1)] : CW_CLAMP(a, 0, G.nf - 1);
    fighter *v = F(i), *k = NULL;
    float bd = 1e9f;
    for (int j = 0; j < G.nf; j++) if (F(j)->alive && enemies(F(j), v)) { float d = v3len(v3sub(F(j)->mv.position, v->mv.position)); if (d < bd) bd = d, k = F(j); }
    if (v->alive) v->hp = 0, kill_fighter(v, k, 0);
    return NULL;
  }
  return "unknown battle command";
}
static void cmd_battle(int argc, const char **argv, cw_reply *r) {
  const char *err = battle_cmd(argc, argv);
  if (err) cw_reply_error(r, "%s", err);
  else cw_reply_printf(r, "{\"pool\":[%d,%d],\"alive\":[%d,%d]}", BT.pool[0], BT.pool[1], BT.alive[0], BT.alive[1]);
}
/* the battle's test switches, as soon as a host can deliver them (frame 2 on: cw_test_env): the count rule and the
 * takeover at once; a live count or pool other than the match began with starts it again, the same on every host */
static void battle_switches(void) {
  static int done;
  if (done || cw_frame_index() == 0) return;
  done = 1;
  const char *e;
  if ((e = cw_test_env("ARENA_COUNT_RULE"))) BT.sw_reference = !strcmp(e, "reference");
  if ((e = cw_test_env("ARENA_TAKEOVER"))) BT.sw_no_takeover = atoi(e) == 0;
  bool restart = false;
  if ((e = cw_test_env("ARENA_BATTLE_LIVE")) && atoi(e) > 0 && CW_CLAMP(atoi(e), 2, MAXF / 2) != BT.live[0]) restart = true;
  if ((e = cw_test_env("ARENA_BATTLE_POOL")) && atoi(e) >= 0 && atoi(e) != BT.pool_start) restart = true;
  if (restart) begin_match(), set_views();
}
static void battle_script(void) { /* ARENA_BATTLE_AT: commands on match ticks */
  const char *e = cw_test_env("ARENA_BATTLE_AT");
  if (!e) return;
  char buf[512];
  snprintf(buf, sizeof buf, "%s", e);
  for (char *save = NULL, *it = strtok_r(buf, "|", &save); it; it = strtok_r(NULL, "|", &save)) {
    char *colon = strchr(it, ':');
    if (!colon || atoi(it) != G.match_ticks) continue;
    const char *av[8] = { "battle" };
    int ac = 1;
    for (char *s2 = NULL, *w = strtok_r(colon + 1, " ", &s2); w && ac < 8; w = strtok_r(NULL, " ", &s2)) av[ac++] = w;
    battle_cmd(ac, av);
  }
}

static void sync_nodes(void) {
#ifdef ARENA_AUTOPILOT
  { /* the bench crowd: the players stand at a spawn point each, looking at the middle of the others */
    static int crowd = -1;
    if (crowd < 0) crowd = getenv("ARENA_CROWD") != NULL;
    if (crowd && A.nspawns > 0 && G.state == ST_PLAY) {
      t3_vec3 mid = { 0, 0, 0 };
      for (int s = 0; s < A.nspawns; s++) mid = v3add(mid, A.spawns[s]);
      mid = v3scale(mid, 1.0f / (float)A.nspawns);
      for (int h = 0; h < G.players; h++) {
        fighter *f = F(h);
        t3_vec3 at = A.spawns[(h * 5) % A.nspawns], d = v3sub(mid, at);
        f->mv.position = at, f->mv.velocity = v3(0, 0, 0);
        f->yaw = cw_atan2(-d.x, -d.z), f->pitch = -0.08f;
      }
    }
  }
#endif
  for (int i = 0; i < G.nf; i++) {
    fighter *f = F(i);
    if (!f->active) continue;
    if (cw_node_valid(f->body)) {
      cw_node3d_set_position(f->body, f->mv.position);
      { /* facing (a hit's reaction is the body's own clip now: soldier.c) */
        float sy, cy;
        cw_sincos(f->yaw * 0.5f, &sy, &cy);
        cw_node3d_set_quaternion(f->body, (t3_quat){ 0, sy, 0, cy });
        f->flinch *= 0.8f;
        if (f->flinch < 0.01f) f->flinch = 0;
      }
      if (f->head) f->head->scale = (t3_vec3){ 2.1f, 2.1f, 2.1f };
      if (!f->alive && f->dead_ticks > 100) cw_node_set(f->body, "visible", cw_val_bool(false));
    }
    body_rim(f, soldier_material(i), false);
    if (f->flash > 0) f->flash--;
    /* the muzzle light's envelope: up in about three ticks while held (9 ticks after each shot), then a fade of about
     * a fifth of a second; under rapid fire it stays up, a steady glow, never a 10 Hz blink */
    if (f->mhold > 0) f->mhold--, f->mglow += (1.0f - f->mglow) * 0.4f;
    else f->mglow = f->mglow > 0.01f ? f->mglow * 0.8f : 0.0f;
    if (cw_node_valid(f->light) && f->mglow > 0) muzzle_place(f);
    if (cw_node_valid(f->light)) cw_node_set(f->light, "energy", cw_val_float(f->alive ? f->mglow * f->mpeak * f->mnear : 0));
  }
  { /* the bodies' animation, detail by distance from the nearest player's eye */
    t3_vec3 eyes[MAXH];
    int ne = 0;
    for (int h = 0; h < G.players && h < MAXH; h++) eyes[ne++] = eye_pos(SF(h));
    soldier_tick(G.f, G.nf, eyes, ne, G.views < 2);
    crowd_tick(G.world, eyes, ne); /* the background battle (the Battle mode; off otherwise) */
  }
  if (G.views < 2 && G.nf > 0) { /* no view hook with one view: this player's own gun, not own body */
    if (cw_node_valid(SF(0)->body)) cw_node_set(SF(0)->body, "visible", cw_val_bool(false));
    cw_node_set(G.vm[0], "visible", cw_val_bool(SF(0)->alive));
  }
  for (int h = 0; h < G.players; h++) {
    fighter *f = SF(h);
    t3_vec3 e = eye_pos(f);
    { /* the eye follows step-ups and step-downs over a few ticks instead of snapping (stairs, kerbs, the mover's step) */
      static float ey[MAXH];
      static bool ground[MAXH];
      float dy = e.y - ey[h];
      if (f->alive && f->mv.on_floor && ground[h] && fabsf(dy) < 0.6f) ey[h] += dy * 0.35f, e.y = ey[h];
      else ey[h] = e.y;
      if (fabsf(e.y - (f->mv.position.y + EYE_H)) < 1e-4f) ey[h] = e.y = f->mv.position.y + EYE_H;
      ground[h] = f->alive && f->mv.on_floor;
    }
    t3_vec3 e0 = e; /* the gameplay eye, for the shake check */
    /* the camera is the eye and the aim, nothing more: no shake, no view kick, no bob, dip or roll (the feel of a hit,
     * a blast, a landing or a heavy shot is in the pad: rumble); the gun's kick is the viewmodel's, below */
    float pitch = f->pitch, yaw = f->yaw;
    /* a downed view never jumps or whips round: it sinks and turns at most 3.6 rad a second, easing in and out (it came
     * round a third of the turn in its first frames, a view swung from a wall to the open court in two frames, a flash
     * in the co-op sweep's worst second; a fall dropped and tilted the view in one frame) */
    if (!f->alive && f->dead_ticks <= 1) KC.yaw[h] = f->yaw, KC.pitch[h] = f->pitch;
    if (!f->alive && (G.sw_no_killcam || G.kc_who[h] < 0)) { /* no killer (a fall, an own rocket): the view drops and tilts */
      float sink = CW_MIN(1.0f, (float)f->dead_ticks / 20.0f);
      e.y = f->mv.position.y + EYE_H + (0.5f - EYE_H) * sink * sink * (3 - 2 * sink);
      KC.pitch[h] += CW_CLAMP((-0.5f - KC.pitch[h]) * 0.14f, -0.06f, 0.06f);
      pitch = KC.pitch[h];
    } else if (!f->alive) { /* the killer cam: the view sinks a little and turns to whoever downed this person, then follows them */
      const fighter *k = F(G.kc_who[h]);
      float sink = CW_MIN(1.0f, (float)f->dead_ticks / 20.0f);
      e.y = f->mv.position.y + EYE_H - 0.5f * sink * sink * (3 - 2 * sink);
      t3_vec3 to = v3sub(chest_pos(k), e);
      float want_y = cw_atan2(-to.x, -to.z), want_p = cw_atan2(to.y, sqrtf(to.x * to.x + to.z * to.z));
      float dy = want_y - KC.yaw[h];
      while (dy > 3.14159265f) dy -= 6.2831853f;
      while (dy < -3.14159265f) dy += 6.2831853f;
      float ease = CW_MIN(1.0f, (float)f->dead_ticks / 12.0f); /* the turn speeds up over a fifth of a second */
      KC.yaw[h] += CW_CLAMP(dy * 0.14f, -0.06f, 0.06f) * ease; /* half a turn in about a second, then it stays on them */
      KC.pitch[h] += CW_CLAMP((CW_CLAMP(want_p, -1.2f, 1.2f) - KC.pitch[h]) * 0.14f, -0.06f, 0.06f) * ease;
      yaw = KC.yaw[h], pitch = KC.pitch[h];
    }
    if (battle()) { /* a takeover's view on its way to the new soldier, or a view watching a teammate (TK) */
      static t3_vec3 wcam[MAXH];
      static float wyaw[MAXH], last_yaw[MAXH];
      static int wph[MAXH] = { 0 };
      if (TK[h].phase == 2) e = TK[h].cam, yaw = TK[h].yaw, pitch = TK[h].pitch, tk_stats[4] += h == 0;
      else if (TK[h].phase == 3 && TK[h].watch >= 0 && TK[h].watch < G.nf) { /* behind and over the one watched, eased */
        const fighter *w = F(TK[h].watch);
        if (wph[h] != 3) wcam[h] = e, wyaw[h] = yaw;
        wyaw[h] += CW_CLAMP(cw_wrap_angle(w->yaw - wyaw[h]) * 0.1f, -0.06f, 0.06f);
        t3_vec3 c = chest_pos(w), want = v3add(v3add(c, v3scale(look_dir(wyaw[h], 0), -3.2f)), v3(0, 0.9f, 0)), hit;
        if (arena_ray(c, want, &hit, NULL)) want = v3add(hit, v3scale(v3norm(v3sub(c, hit)), 0.3f));
        wcam[h] = v3add(wcam[h], v3scale(v3sub(want, wcam[h]), 0.12f));
        e = wcam[h], yaw = wyaw[h], pitch = -0.22f, tk_stats[5] += h == 0;
      }
      wph[h] = TK[h].phase;
      /* the takeover check (tk.stats): the drawn view's fastest turn through a takeover and the second after it, and the
       * gap between the view and the new soldier's eye after the handover */
      bool after = tk_stats[0] > 0 && G.match_ticks - tk_stats[3] < 60 && SF(h)->alive;
      if (h == 0 && (TK[h].phase == 1 || TK[h].phase == 2 || after)) {
        int32_t w = (int32_t)(fabsf(cw_wrap_angle(yaw - last_yaw[h])) * 60000.0f);
        if (w > tk_stats[1]) tk_stats[1] = w;
      }
      if (h == 0 && after) {
        int32_t gap = (int32_t)(v3len(v3sub(e, eye_pos(SF(h)))) * 1000.0f);
        if (gap > tk_stats[2]) tk_stats[2] = gap;
      }
      last_yaw[h] = yaw;
    }
    KC.eye[h] = e, KC.yaw_now[h] = yaw, KC.pitch_now[h] = pitch;
    t3_vec3 d = look_dir(yaw, pitch);
    t3_vec3 right = v3norm(v3cross(d, v3(0, 1, 0))), up = v3cross(right, d);
    t3_vec3 at = e;
    {
      static int bug = -1; /* a gate switch: read from frame 2 on every host (cw_test_env) */
      if (bug < 0 && cw_frame_index() > 0) { const char *v = cw_test_env("ARENA_CAM_BUG"); bug = v && atoi(v); }
      if (bug > 0) at.y += 0.01f * cw_sin((float)G.ticks * 0.3f); /* the control: a 1 cm idle sway */
    }
    cw_node3d_set_position(G.cam[h], at);
    cw_node3d_look_at(G.cam[h], v3add(at, d), up);
    if (h == 0 && G.state == ST_PLAY) camgate(f, at, d, up, false);
    if (!f->alive && G.state == ST_PLAY && f->dead_ticks >= 64 && G.kc_who[h] >= 0) { /* the killer cam check (killcam), once the turn (at most about a second) is done */
      float sx, sy;
      killcam[0]++;
      if (kc_project(h, chest_pos(F(G.kc_who[h])), &sx, &sy)) killcam[1]++;
    }
    offsetgate(h, f, e0, look_dir(f->yaw, f->pitch));
    {
      static float fov[MAXH] = { 78, 78, 78, 78 };
      float fw = f->alive && f->sprint ? 84.0f : 78.0f;
      fov[h] += (fw - fov[h]) * 0.12f;
      if (fabsf(fov[h] - fw) < 0.01f) fov[h] = fw;
      cw_node_set(G.cam[h], "fov", cw_val_float(fov[h]));
    }
    f->kick *= 0.82f;
    /* the viewmodel: the gun drops in when drawn, lags the look (sway), bobs with the stride and kicks back and up on fire */
    static const float MUZ[NWEAP] = { 0.84f, 0.82f, 0.71f, 0.82f }, MUZ_UP[NWEAP] = { 0.05f, 0.055f, 0, 0.03f };
    static int last_w[MAXH] = { -1, -1, -1, -1 };
    static float draw[MAXH], dyaw[MAXH], dpitch[MAXH], lyaw[MAXH], lpitch[MAXH], bobph[MAXH];
    if (last_w[h] != f->weapon) { last_w[h] = f->weapon; draw[h] = 1; }
    draw[h] *= 0.86f;
    for (int w = 0; w < NWEAP; w++) cw_node_set(G.vm_wp[h][w], "visible", cw_val_bool(f->alive && w == f->weapon));
    float turn_y = f->yaw - lyaw[h], turn_p = f->pitch - lpitch[h];
    if (turn_y > 3.0f || turn_y < -3.0f) turn_y = 0;
    lyaw[h] = f->yaw, lpitch[h] = f->pitch;
    dyaw[h] += (CW_CLAMP(turn_y * 2.0f, -0.12f, 0.12f) - dyaw[h]) * 0.2f;
    dpitch[h] += (CW_CLAMP(turn_p * 2.0f, -0.1f, 0.1f) - dpitch[h]) * 0.2f;
    float rec = f->cooldown > 0 ? (float)f->cooldown / (float)W[f->weapon].cooldown : 0;
    rec = rec > 0.6f ? (rec - 0.6f) / 0.4f : 0;
    float spd = v3len(v3(f->mv.velocity.x, 0, f->mv.velocity.z)) / 7.0f;
    spd = f->mv.on_floor ? CW_MIN(1, spd) : 0;
    bobph[h] += 0.16f * (0.3f + spd);
    float bx = cw_sin(bobph[h]) * 0.012f * spd, by = -fabsf(cw_cos(bobph[h])) * 0.014f * spd + cw_sin(bobph[h] * 0.5f) * 0.0025f;
    float kick = rec * (f->weapon == 2 ? 1.5f : f->weapon == 3 ? 1.2f : 1.0f);
    /* in a tall half width view the gun comes in toward the middle, or it leaves the view and the sleeve fills the corner */
    cw_rect vmr = cw_shell_player_rect(h);
    float vm_x = 0.2f * cw_clampf((vmr.h > 1 ? vmr.w / vmr.h : 1.78f) / 1.78f, 0.45f, 1.0f);
    /* a bash drives the gun forward and across, its stock first; a throw drops it out of the way for the arm */
    float bk = f->bash_t < 18 ? cw_sin(3.14159265f * (float)f->bash_t / 18.0f) : 0, tk = f->throw_t < 26 ? cw_sin(3.14159265f * (float)f->throw_t / 26.0f) : 0;
    cw_node3d_set_position(G.vm[h], v3(vm_x + bx - dyaw[h] * 0.25f - bk * 0.09f, -0.2f + by - draw[h] * 0.3f + dpitch[h] * 0.2f - tk * 0.22f + bk * 0.03f, -0.36f + kick * 0.075f - bk * 0.16f));
    cw_node3d_set_rotation(G.vm[h], v3(kick * 0.1f + draw[h] * 0.5f + dpitch[h] * 0.5f - tk * 0.5f, dyaw[h] * 0.5f + 0.03f + bk * 0.55f, -bx * 1.5f + bk * 0.35f));
    cw_node3d_set_position(G.vm_flash[h], v3(0, MUZ_UP[f->weapon] * VM_SCALE, -(MUZ[f->weapon] + 0.04f) * VM_SCALE));
    /* the star grows and shrinks with the muzzle light's envelope (it swells on a shot and fades; a burst holds it at
     * full size), so rapid fire is a steady glow at the barrel, never a star switched on and off ten times a second */
    bool star = f->mglow > 0.05f && f->alive;
    cw_node_set(G.vm_flash[h], "visible", cw_val_bool(star));
    if (star) { /* the star turns shot by shot (drawn only: hashed from the shot's tick) */
      uint32_t fh = (uint32_t)(G.ticks - (uint32_t)(9 - f->mhold)) * 2654435761u;
      float big = (f->weapon == 1 || f->weapon == 2 ? 0.46f : f->weapon == 3 ? 0.34f : 0.26f) * (0.95f + 0.1f * (float)((fh >> 8) & 255) / 255.0f) * f->mglow;
      cw_node3d_set_scale(G.vm_flash[h], v3(big, big, 1));
      cw_node3d_set_rotation(G.vm_flash[h], v3(0, 0, (float)(fh >> 24) / 255.0f * 6.283f));
    }
    G.hit_marker[h] *= 0.9f;
    /* the damage tint is one envelope: it swells over about a quarter second when damage starts, holds while hits keep
     * coming (each hit holds it 0.6 s more) and fades over two thirds of a second after the last one, so a swell and its
     * fade take a second at the least whatever the hit rate: never a red pulse per hit (WCAG 2.3.1, at most three flashes a
     * second). ARENA_T_strobefix=0 (autopilot): the old curve, rising in four frames at every hit, the flash sweep's control */
    if (G.sw_hurt_hz > 0 && h == 0 && G.steps % CW_MAX(1, (int)(60 / G.sw_hurt_hz)) == 0) /* sw hurt: a hit's cue, no damage */
      G.dmg_want[h] = 1, G.dmg_hold[h] = 36;
    if (strobefix()) {
      if (G.dmg_hold[h] > 0) G.dmg_hold[h]--;
      float to = G.dmg_hold[h] > 0 ? 1.0f : 0.0f;
      G.dmg_flash[h] = to > G.dmg_flash[h] ? fminf(to, G.dmg_flash[h] + 0.065f) : fmaxf(to, G.dmg_flash[h] - 0.025f);
    } else
      G.dmg_flash[h] += (G.dmg_want[h] - G.dmg_flash[h]) * (G.dmg_want[h] > G.dmg_flash[h] ? 0.35f : 0.1f);
    tint_meter(h);
    G.dmg_want[h] *= 0.93f;
    G.kill_flash[h] *= 0.965f;
    G.head_marker[h] *= 0.93f;
    G.pick_t[h] *= 0.985f;
  }
  views_mode();
  if (G.views >= 2 && !view_hook_on()) /* each gun is drawn only by its own camera (vm_layer): shown while its player lives */
    for (int h = 0; h < MAXH; h++) cw_node_set(G.vm[h], "visible", cw_val_bool(h < G.players && SF(h)->alive));
#ifdef ARENA_AUTOPILOT
  if (getenv("ARENA_TOPDOWN")) { /* ARENA_TOPDOWN: view 1 looks straight down on the arena (layout checks) */
    cw_node3d_set_position(G.cam[0], v3(0, 62, 0.01f));
    cw_node3d_look_at(G.cam[0], v3(0, 0, 0), v3(0, 0, -1));
  }
  { /* ARENA_VIEW=x,y,z,yaw,pitch: view 1 from a fixed spot (screenshots of the layout) */
    const char *v = getenv("ARENA_VIEW");
    float x, y, z, yw, pt;
    if (v && sscanf(v, "%f,%f,%f,%f,%f", &x, &y, &z, &yw, &pt) == 5) {
      cw_node3d_set_position(G.cam[0], v3(x, y, z));
      cw_node3d_look_at(G.cam[0], v3add(v3(x, y, z), look_dir(yw, pt)), v3(0, 1, 0));
      cw_node_set(G.vm[0], "visible", cw_val_bool(false));
    }
  }
  { /* ARENA_CHASE=i,fwd,right,up,lookh: view 1 from a spot in fighter i's own frame, looking at it (key art) */
    const char *v = getenv("ARENA_CHASE");
    if (!v && keyart_chase[0]) v = keyart_chase;
    int i;
    float fw, rt, up, lh;
    if (v && sscanf(v, "%d,%f,%f,%f,%f", &i, &fw, &rt, &up, &lh) == 5 && i >= 0 && i < G.nf && cw_node_valid(F(i)->body)) {
      t3_vec3 p = cw_node3d_position(F(i)->body), d = look_dir(F(i)->yaw, 0), r = v3(-d.z, 0, d.x);
      t3_vec3 c = v3(p.x + d.x * fw + r.x * rt, p.y + up, p.z + d.z * fw + r.z * rt);
      cw_node3d_set_position(G.cam[0], c);
      cw_node3d_look_at(G.cam[0], v3(p.x, p.y + lh, p.z), v3(0, 1, 0));
      cw_node_set(G.vm[0], "visible", cw_val_bool(false));
    }
  }
#endif
  view_masks();
}

/* What each split screen view draws. Player h's gun is on a layer of its own (vm_layer), which only camera h draws as
 * its viewmodel; each body says which views draw it (cw_node3d_set_views): never its own player's view, and not a view
 * whose camera it is right up against. That keeps the frame in one pass. sw view_hook 1 (or ARENA_T_view_hook=1) brings
 * back the old way, one shared gun layer and a view hook that shows and hides before each view (drawn view by view):
 * the picture and timing tests' other side */
static uint32_t vm_layer(int h) { return 1u << (4 + h); }
static bool view_hook_on(void) { return G.sw_view_hook || arena_tune("view_hook", 0) > 0.5f; }
/* a body (an enemy, a friend, a fallen one) whose middle is within about 0.6 m of a view's camera plus its own radius is
 * not drawn in that view: from inside the near plane its parts fill the view and change every frame. Hidden under 1.0 m,
 * back over 1.15 m, so one hovering at the edge does not blink */
static bool near_cam[MAXH][MAXF];
static bool body_near(int view, int i) {
  const fighter *f = F(i);
  const t3_mat4 *cm = cw_node3d_global_transform(G.cam[view]);
  t3_vec3 c = v3(cm->e[12], cm->e[13], cm->e[14]), b = f->mv.position;
  float dy = c.y < b.y ? b.y - c.y : c.y > b.y + PLAYER_HT ? c.y - b.y - PLAYER_HT : 0;
  float dist = sqrtf((c.x - b.x) * (c.x - b.x) + (c.z - b.z) * (c.z - b.z) + dy * dy);
  if (strobefix()) near_cam[view][i] = near_cam[view][i] ? dist < 1.15f : dist < 1.0f;
  bool hide = strobefix() && near_cam[view][i];
  if (!hide && dist < 0.6f) G.fl_near_views++; /* the meter: a body drawn inside a view's near reach */
  return hide;
}
/* the masks, once a tick after the cameras are placed (sync_nodes) */
static void view_masks(void) {
  if (view_hook_on()) return;
  int nv = CW_MIN(G.views, MAXH);
  bool shift = arena_tune("mask_shift", 0) > 0.5f; /* sw tune mask_shift 1: each body's mask a view over (tests/arenafps-onepass.mjs's control) */
  for (int i = 0; i < G.nf; i++) {
    fighter *f = F(i);
    if (!cw_node_valid(f->body)) continue;
    uint32_t m = CW_VIEWS_ALL;
    if (nv >= 2) {
      for (int v = 0; v < nv; v++) if (SEAT[v] == i && v < G.players) m &= ~(1u << v); /* a player's own body (its seat's soldier): never in its own view */
      if (f->alive || f->dead_ticks <= 100)
        for (int v = 0; v < nv; v++) if (v != i && body_near(v, i)) m &= ~(1u << v);
    }
    if (shift && nv >= 2) m = ((m << 1) | (m >> (nv - 1))) & ((1u << nv) - 1);
    cw_node3d_set_views(f->body, m);
  }
}
/* the guns' layers and the cameras' viewmodel layers for the way chosen, and the hook (set when it changes) */
static void views_mode(void) {
  static int on = -1;
  bool hook = view_hook_on();
  if (on == (int)hook) return;
  on = hook;
  for (int h = 0; h < MAXH; h++) {
    uint32_t l = hook ? 2u : vm_layer(h);
    cw_node_set(G.vm[h], "layers", cw_val_int((int32_t)l));
    cw_node_set(G.vm_flash[h], "layers", cw_val_int((int32_t)l));
#ifdef ARENA_AUTOPILOT
    if (getenv("ARENA_T_vm") && atoi(getenv("ARENA_T_vm")) == 0) l = 0;
#endif
    cw_node_set(G.cam[h], "viewmodel_layers", cw_val_int((int32_t)l));
  }
  if (hook) for (int i = 0; i < MAXF; i++) if (cw_node_valid(F(i)->body)) cw_node3d_set_views(F(i)->body, CW_VIEWS_ALL);
  cw_pipeline_set_view_hook(cw_pipeline_of(G.tree), hook ? view_hook : NULL, NULL);
}
static void set_obj_visible(cw_node n, bool v) {
  t3_object *o = cw_node3d_object(n);
  if (o) o->visible = v; /* the hook runs after the tree's pre-render: set the drawn object itself */
}
/* the old way (sw view_hook 1): before each view, its own gun shown and the others hidden, its own body and those
 * against its camera hidden */
static void view_hook(int view, void *u) {
  (void)u;
  for (int h = 0; h < G.players; h++) set_obj_visible(G.vm[h], h == view && SF(h)->alive);
  for (int i = 0; i < G.nf; i++) {
    fighter *f = F(i);
    bool show = (view < 0 || view >= MAXH || i != SEAT[view]) && (f->alive || f->dead_ticks <= 100);
    if (show && view >= 0 && view < MAXH && body_near(view, i)) show = false;
    if (cw_node_valid(f->body)) set_obj_visible(f->body, show);
  }
}

/* Start from any seated pad (Enter on the keyboard): tick by tick, so a press on a frame with no tick still counts */
static bool any_start(void) {
  for (int p = 0; p < G.players; p++) if (cw_action_just_pressed(p, G.ui_start)) return true;
  return false;
}

static void game_step(void);
/* a person at the pad: any action held this tick (the sticks are actions too) */
enum { AWAY_TICKS = 60 * 20 };
static void seats_touched(void) {
  const cw_action a[] = { G.mv_l, G.mv_r, G.mv_f, G.mv_b, G.lk_l, G.lk_r, G.lk_u, G.lk_d, G.fire, G.jump, G.w_next, G.w_prev, G.sprint, G.ui_start };
  for (int h = 0; h < MAXH; h++)
    for (size_t k = 0; k < sizeof a / sizeof a[0]; k++) if (cw_action_pressed(h, a[k])) { G.last_in[h] = G.steps; break; }
  if (HM.drive) G.last_in[0] = G.steps;
}
static bool away(int h) { return G.mode == MODE_COOP && !G.sw_idle_lives && !AUTO && G.steps - G.last_in[h] > AWAY_TICKS; }
/* the people holding the arena: every seat but the away ones, at least one */
static int present_players(void) { int n = 0; for (int h = 0; h < G.players; h++) n += !away(h); return CW_MAX(1, n); }
static void ptest_update(void);
static void fixed_update(void *user, double dt) {
  (void)user, (void)dt;
  /* the setup and results panels are not play: Start confirms there (game_step), so the shell does not see that press and
   * does not pause; every other tick it runs as usual, and a pad that goes away frees its seat */
  bool panel = cw_shell_screen_now() == CW_SHELL_PLAY && (G.state == ST_SETUP || G.state == ST_OVER);
  bool pause_press = false;
  for (int p = 0; panel && p < cw_shell_players(); p++) pause_press |= cw_action_just_pressed(p, G.ui_pause);
  if (!pause_press && !cw_shell_update()) { /* menus and pause: the motors stop */
    rumble_stop_all();
    cw_shell_screen sc = cw_shell_screen_now();
    if (sc == CW_SHELL_TITLE || sc == CW_SHELL_JOIN) { /* the title: a slow drift round the arena at head height, under the
                                                        * roof, toward the middle, the town or works beyond the walls (drawn only) */
      static float ang = 0.6f;
      ang += 0.0009f;
      float sa, ca;
      cw_sincos(ang, &sa, &ca);
      t3_vec3 o = v3(0, 3.2f, 0), out = v3(sa * 16.0f, 3.2f, ca * 16.0f), hit;
      static float rad = 15;
      float want = arena_ray(o, out, &hit, NULL) ? fmaxf(3.0f, v3len(v3sub(hit, o)) - 1.2f) : 15.0f; /* never inside a wall */
      rad += (want - rad) * (want < rad ? 0.5f : 0.02f);
      t3_vec3 at = v3(sa * rad, 3.2f, ca * rad);
      cw_node3d_set_position(G.cam[0], at);
      cw_node3d_look_at(G.cam[0], v3(-sa * 6.0f, 4.2f, -ca * 6.0f), v3(0, 1, 0));
      cw_node_set(G.vm[0], "visible", cw_val_bool(false));
    }
    return;
  }
#ifdef ARENA_AUTOPILOT
  /* ARENA_SIM_BATCH=n: n game ticks per engine tick, for pacing and soak runs that measure the simulation only (every seat is a
   * bot, so no input is lost); the game time per drawn frame grows n times */
  static int batch = -1;
  if (batch < 0) batch = getenv("ARENA_SIM_BATCH") ? CW_CLAMP(atoi(getenv("ARENA_SIM_BATCH")), 1, 1000) : 1;
  for (int b = 0; b < batch; b++) game_step(), ptest_update();
#else
  game_step();
  ptest_update();
#endif
}

/* The game's save (cw_shell_game_save): the setup screen's aim assist and rumble choices, kept across runs. Zero is
 * the default (assist on, rumble on), so a save from before it existed, or none, reads as the defaults. */
typedef struct { uint8_t assist_off, rumble_off, pad[2]; } arena_save;
static void setup_load(void) {
  const arena_save *sv = cw_shell_game_save();
  G.aim_assist = !sv->assist_off;
  G.rumble_off = sv->rumble_off != 0;
}
static void setup_store(void) {
  arena_save *sv = cw_shell_game_save();
  sv->assist_off = !G.aim_assist, sv->rumble_off = G.rumble_off != 0;
  cw_shell_save_commit();
}

static void game_step(void) {
  RB.clock++; /* the rumble's clock: every game tick */
  G.steps++; /* every game tick since the start, across matches (soaks) */
  seats_touched();
  if (G.state == ST_SETUP) {
    int h = 0;
    if (cw_action_just_pressed(h, G.mv_f)) G.setup_row = (G.setup_row + SETUP_ROWS - 1) % SETUP_ROWS, fx_play_ui(SND_CLICK, 0.4f);
    if (cw_action_just_pressed(h, G.mv_b)) G.setup_row = (G.setup_row + 1) % SETUP_ROWS, fx_play_ui(SND_CLICK, 0.4f);
    int dir = cw_action_just_pressed(h, G.mv_r) - cw_action_just_pressed(h, G.mv_l);
    if (dir) {
      fx_play_ui(SND_TICK, 0.5f);
      if (G.setup_row == 0) { G.arena_id = (G.arena_id + ARENAS + dir) % ARENAS; build_world(G.arena_id); }
      else if (G.setup_row == 1) G.mode = (G.mode + MODE_COUNT + dir) % MODE_COUNT;
      else if (G.setup_row == 2 && battle()) BT.pool_setting = (BT.pool_setting + 4 + dir) % 4; /* Battle: the reinforcements a side */
      else if (G.setup_row == 2) G.bots_setting = CW_CLAMP(G.bots_setting + dir, -1, MAXF_CLASSIC - 4);
      else if (G.setup_row == 3) G.bot_level = (G.bot_level + BOT_LEVELS + dir) % BOT_LEVELS;
      else if (G.setup_row == 4) G.aim_assist = !G.aim_assist, setup_store();
      else if (G.setup_row == 5) { /* rumble on or off */
        G.rumble_off = !G.rumble_off;
        setup_store();
        if (G.rumble_off) rumble_stop_all();
        else rumble(h, 0.3f, 0.3f, 120); /* a taste of it */
      } else if (G.setup_row == 6) G.rules = (G.rules + RULE_COUNT + dir) % RULE_COUNT;
    }
    if (cw_shell_join_press()) fx_play_ui(SND_CLICK, 0.6f); /* a pad with no seat: A takes one (players_changed) */
    if (cw_action_just_pressed(h, G.jump) || cw_action_just_pressed(h, G.fire) || any_start() || cw_shell_join_requested()) {
      fx_play_ui(SND_CLICK, 0.8f);
      { begin_match(); set_views(); }
    } else if (cw_action_just_pressed(h, G.ui_back)) { /* out to the title: there is no pause menu on this panel */
      fx_play_ui(SND_TICK, 0.5f);
      cw_shell_to_title();
    }
    G.ticks++;
    item_tick();
    fx_tick();
    return;
  }
  if (G.state == ST_OVER) {
    G.over_ticks++;
    fx_tick();
    proj_tick();
    sync_nodes();
    if (AUTO && getenv("ARENA_ONE")) return; /* ARENA_ONE: one match, then the results stay (human-matches.mjs reads them) */
    if (G.over_ticks > 60 * 3 && (cw_action_just_pressed(0, G.fire) || cw_action_just_pressed(0, G.jump) || any_start() || AUTO)) {
      if (AUTO && G.over_ticks < 60 * 8) return;
      G.state = ST_SETUP;
      G.cue_over = 0;
      G.ticks = 0;
      if (AUTO) { /* the autopilot plays the next arena and mode: a soak test sees them all */
        if (!getenv("ARENA_REPEAT")) { /* ARENA_REPEAT: play the same arena and mode again (pacing runs) */
          G.arena_id = (G.arena_id + 1) % ARENAS;
          G.mode = (G.mode + 1) % MODE_COUNT;
        }
        begin_match();
        set_views();
      }
      else { build_world(G.arena_id); }
    }
    return;
  }
  G.ticks++;
  G.match_ticks++;
  if (battle()) battle_switches(), battle_script();
  /* the path budget: oldest requests first, a few a tick */
  cw_nav_tick(A.nav, 3, 700);
  for (int i = 0; i < G.nf; i++) {
    fighter *f = F(i);
    if (!f->active) continue;
    if (f->cooldown > 0) f->cooldown--;
    if (f->spawn_guard > 0) f->spawn_guard--;
    if (f->gcool > 0) f->gcool--;
    if (f->bash_cool > 0) f->bash_cool--;
    if (f->bash_t < 1000) f->bash_t++;
    if (f->throw_t < 1000) f->throw_t++;
    if (!f->alive) {
      f->dead_ticks++;
      if (battle()) { battle_dead(f); continue; }
      bool can_respawn = G.mode != MODE_COOP || f->human < 0 ? G.mode != MODE_COOP : G.lives > 0;
      if (can_respawn && f->human >= 0 && away(f->human)) { /* nobody at that pad: no life is spent on it */
        if (f->dead_ticks == 151) G.away_skips++;
        can_respawn = false;
      }
      if (f->dead_ticks > (f->human >= 0 ? 150 : 120) && can_respawn) {
        if (G.mode == MODE_COOP && f->human >= 0) G.lives--;
        spawn_fighter(f);
      }
      continue;
    }
    f->since_spawn++;
    f->alive_t++;
    if (f->recoil_t < 1000) f->recoil_t++;
    if (f->hit_t < 1000) f->hit_t++;
    if (f->recoil_t > 7 && (f->recoil_p != 0 || f->recoil_y != 0)) { /* the burst is over: the shots settle back on the crosshair */
      float kp = f->recoil_p * 0.18f, ky = f->recoil_y * 0.18f;
      f->recoil_p -= kp, f->recoil_y -= ky;
      if (fabsf(f->recoil_p) < 1e-4f && fabsf(f->recoil_y) < 1e-4f) f->recoil_p = f->recoil_y = 0;
    }
    if (f->recoil_t > 20) f->recoil_n = 0;
    G.st.fighter_ticks++;
    if ((G.ticks + i) % 10 == 0) { /* measurement: encounters and contact time */
      t3_vec3 e = eye_pos(f);
      bool sees = false;
      for (int j = 0; j < G.nf && !sees; j++) {
        const fighter *o = F(j);
        if (j == i || !o->active || !o->alive || !enemies(f, o)) continue;
        if (v3len(v3sub(o->mv.position, f->mv.position)) <= 45 && !arena_ray(e, chest_pos(o), NULL, NULL)) sees = true;
      }
      if (sees) {
        G.st.contact_ticks += 10;
        if (f->alive_t - f->last_sight_t > 180) {
          G.st.eng++;
          if (f->alive_t - f->last_eng_t > G.st.eng_gap_max) G.st.eng_gap_max = f->alive_t - f->last_eng_t;
          f->last_eng_t = f->alive_t;
        }
        f->last_sight_t = f->alive_t;
      }
    }
    if (f->seen_cool > 0) f->seen_cool--;
    else if ((G.ticks + i) % 10 == 0) {
      t3_vec3 e = eye_pos(f);
      for (int j = 0; j < G.nf; j++) {
        const fighter *o = F(j);
        if (j == i || !o->active || !o->alive || !enemies(f, o)) continue;
        if (v3len(v3sub(o->mv.position, f->mv.position)) > 45 || arena_ray(e, chest_pos(o), NULL, NULL)) continue;
        G.st.enc++;
        if (G.ticks - f->last_enc_tick > G.st.enc_gap_max) G.st.enc_gap_max = G.ticks - f->last_enc_tick;
        f->last_enc_tick = G.ticks;
        f->seen_cool = 300;
        break;
      }
    }
    float yaw0 = f->yaw, pitch0 = f->pitch;
    if (is_bot(f)) bot_tick(f, i);
    else human_tick(f, i);
    if (battle() && tk_target(i)) { /* a person's view is on its way to this soldier: it turns no faster than 2.4 rad/s, so the view (3.6) arrives */
      f->yaw = yaw0 + CW_CLAMP(cw_wrap_angle(f->yaw - yaw0), -0.04f, 0.04f);
      f->pitch = pitch0 + CW_CLAMP(f->pitch - pitch0, -0.04f, 0.04f);
    }
    /* footsteps: a person hears their own quietly, everyone else's placed in the world (panned and fading with distance,
     * audible to about 25 m), louder when sprinting */
    if (f->mv.on_floor && (int)(f->bob * 0.5f) != (int)((f->bob - 0.12f) * 0.5f) && v3len(v3(f->mv.velocity.x, 0, f->mv.velocity.z)) > 3) {
      float pitch = 0.9f + (float)((G.ticks * 7 + i * 13) % 20) * 0.01f;
      if (f->human >= 0 && !AUTO) fx_play_ui(SND_STEP, f->sprint ? 0.24f : 0.16f);
      else fx_play(SND_STEP, f->mv.position, f->sprint ? 0.9f : 0.65f, pitch);
    }
  }
  proj_tick();
  gren_tick();
  item_tick();
  power_tick();
  check_rules();
  for (int h = 0; h < G.players; h++) { /* this tick's hits in the pad: taken (sharp, by how much), dealt (a big one or a kill) */
    if (G.dmg_taken[h] >= 15) rumble_sent[1]++, rumble(h, CW_MIN(0.4f, (float)G.dmg_taken[h] * 0.004f), CW_MIN(1, 0.35f + (float)G.dmg_taken[h] * 0.006f), CW_MIN(160, 70 + G.dmg_taken[h]));
    if (G.kills_now[h] || G.dmg_dealt[h] >= 40) rumble_sent[2]++;
    if (G.kills_now[h]) rumble(h, 0.15f, 0.6f, 90);
    else if (G.dmg_dealt[h] >= 40) rumble(h, 0, CW_MIN(0.55f, 0.25f + (float)G.dmg_dealt[h] * 0.003f), 60);
    G.dmg_taken[h] = G.dmg_dealt[h] = G.kills_now[h] = 0;
  }
  fx_tick();
  for (int i = 0; i < MAX_FEED; i++) if (G.feed[i].ticks > 0) G.feed[i].ticks--;
  sync_nodes();
  for (int i = 0; i < MAXF; i++) {
    const fighter *f = F(i);
    G.d_score[i] = f->score, G.d_hp[i] = f->hp, G.d_deaths[i] = f->deaths, G.d_weapon[i] = f->weapon, G.d_alive[i] = f->active && f->alive;
    G.d_pos[i * 2] = f->mv.position.x, G.d_pos[i * 2 + 1] = f->mv.position.z;
    G.d_yaw[i] = f->yaw;
  }
  /* the state hash */
  for (int i = 0; i < G.nf; i++) {
    const fighter *f = F(i);
    int32_t q[5] = { (int32_t)(f->mv.position.x * 1000), (int32_t)(f->mv.position.z * 1000), f->hp, f->score, (int32_t)(f->yaw * 1000) };
    const uint8_t *b = (const uint8_t *)q;
    for (size_t j = 0; j < sizeof q; j++) G.hash = (G.hash ^ b[j]) * 16777619u;
  }
}

/* ── HUD ───────────────────────────────────────────────────────────── */

/* the item colours of the pickups (arena.c draws them), for the HUD's respawn timers */
static const float ITEM_RGB[ITEM_KINDS][3] = { { 0.4f, 1, 0.5f }, { 0.5f, 0.75f, 1 }, { 1, 0.8f, 0.35f }, { 1, 0.45f, 0.35f }, { 0.85f, 0.55f, 1 }, { 1, 1, 0.55f } };

static cw_text_metrics text(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  return cw_r2d_text(G.font, s, cw_v2(x, y), &(cw_text_style){ .size = size, .color = c, .outline_width = size * 0.09f, .outline_color = cw_rgba(0, 0, 0, c.a), .align = align, .wrap_width = wrap }); /* the outline fades with the text: a fading line must not leave black letters */
}
/* the box text() would draw, at the player's text scale (the HUD lays out from these, so it holds at 200%) */
static cw_text_metrics measure(const char *s, float size, float wrap) {
  return cw_font_measure(G.font, s, &(cw_text_style){ .size = size, .outline_width = size * 0.09f, .wrap_width = wrap });
}
/* a size held to the body text floor: 26 px at 1080 lines (cartwheel/render2d.h "text size"), whatever the view's size */
static float body(float size) {
  float m = cw_r2d_canvas_size().y * CW_TEXT_FLOOR_BODY * 1.02f;
  return size > m ? size : m;
}
/* the HUD's top rows' bottom in each view this frame (the kill feed and the wave call go under them); 0: not drawn */
static float hud_top_bottom[MAXH], hud_top_right[MAXH]; /* and their right edge */
/* text with button prompts in it (cartwheel/prompt.h markup: "{jump} jump") for one player (-1: every seat), in the box
 * text() would use */
static void ptext(const char *s, int player, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  float px = align == CW_ALIGN_CENTER ? x + wrap * 0.5f : align == CW_ALIGN_RIGHT ? x + wrap : x;
  cw_prompt_text(G.font, s, player, cw_v2(px, y), size, c, align);
}

/* ── Battle's HUD ─────────────────────────────────────────────────────────────────────────────────────────────────────
 * Top left, both sides in their colours, own side first: reinforcements left and soldiers alive, or at 0 "LAST STAND:
 * n left" with a pip a soldier; an arrow by a side that is bleeding. Top centre under the time: the posts, each a
 * square in its holder's colour (white neutral) filled as far as its meter. In the view: the objective (the nearest
 * post this side does not hold, or one of its own with an enemy in it) as a marker with what to do and how far, at
 * the view's edge when it is behind; the reveal pings; a call across the screen when a post falls or a last stand
 * starts. All of it swells and fades: nothing blinks. */
static cw_color bside(int s, float a) { return cw_rgba(BSIDE_RGB[s & 1][0], BSIDE_RGB[s & 1][1], BSIDE_RGB[s & 1][2], a); }
static int battle_objective(int s, t3_vec3 from, const char **what) {
  int best = -1;
  float bd = 1e9f;
  for (int i = 0; i < BT.nposts; i++) {
    const post *p = &BT.posts[i];
    bool attack = p->home < 0 && p->owner != s, defend = p->owner == s && p->home < 0 && (p->push == !s || p->push == 2);
    if (!attack && !defend) continue;
    float d = v3len(v3sub(p->pos, from));
    if (d < bd) bd = d, best = i, *what = defend ? "DEFEND" : p->owner < 0 ? "CAPTURE" : "TAKE";
  }
  if (best < 0) for (int i = 0; i < BT.nposts; i++) if (BT.posts[i].home < 0 && BT.posts[i].owner == s) best = i, *what = "HOLD";
  return best;
}
static void hud_battle(int h, const fighter *f, cw_rect r, float u, float *top_left, float *top_mid) {
  int me = f->team & 1;
  char s[64];
  for (int k = 0; k < 2; k++) { /* the two sides */
    int side = k ? !me : me;
    float sz = body(u * (k ? 0.95f : 1.1f));
    if (BT.pool[side] > 0 || BT.sw_reference) snprintf(s, sizeof s, "%s  REINF %d   ALIVE %d", BSIDE_NAME[side], BT.pool[side], BT.alive[side]);
    else snprintf(s, sizeof s, "%s  LAST STAND: %d left", BSIDE_NAME[side], BT.alive[side]);
    cw_text_metrics tm = text(s, r.x + u, *top_left, sz, bside(side, 1), CW_ALIGN_LEFT, 0);
    float x = r.x + u + tm.width + u * 0.5f, cy = *top_left + tm.height * 0.5f;
    if (BT.pool[side] == 0 && !BT.sw_reference) /* a pip a soldier left */
      for (int i = 0; i < CW_MIN(BT.alive[side], 16); i++) cw_r2d_circle(cw_v2(x + u * 0.55f * (float)i + u * 0.2f, cy), u * 0.2f, 12, bside(side, 0.95f));
    else { /* bleeding: the enemy holds more than half the posts */
      int held = 0;
      for (int i = 0; i < BT.nposts; i++) held += BT.posts[i].owner == !side;
      if (held * 2 > BT.nposts && BT.pool[side] > 0) {
        float a = u * 0.35f;
        cw_vec2 p[3] = { { x, cy - a * 0.6f }, { x + a * 2, cy - a * 0.6f }, { x + a, cy + a * 0.8f } };
        cw_r2d_polygon(p, 3, cw_rgba(1, 0.85f, 0.3f, 0.95f));
      }
    }
    *top_left += tm.height;
  }
  /* the posts, under the time */
  float ps = u * 0.9f, gap = u * 0.45f, w = (float)BT.nposts * ps + (float)(BT.nposts - 1) * gap, x0 = r.x + r.w * 0.5f - w * 0.5f, y0 = *top_mid + u * 0.15f;
  for (int i = 0; i < BT.nposts; i++) {
    const post *p = &BT.posts[i];
    float x = x0 + (float)i * (ps + gap), a = fabsf(p->meter);
    cw_r2d_rect(cw_rect_make(x, y0, ps, ps), cw_rgba(0, 0, 0, 0.5f));
    cw_color c = p->meter < 0 ? bside(0, 0.95f) : bside(1, 0.95f);
    cw_r2d_rect(cw_rect_make(x, y0 + ps * (1 - a), ps, ps * a), c);
    cw_r2d_rect_lines(cw_rect_make(x, y0, ps, ps), CW_MAX(1.5f, u * 0.08f), p->owner >= 0 ? bside(p->owner, 1) : cw_rgba(1, 1, 1, 0.9f));
  }
  *top_mid = y0 + ps + u * 0.2f;
  if (G.sw_no_cues || !f->alive) return;
  /* the objective marker */
  const char *what = "";
  int oi = battle_objective(me, f->mv.position, &what);
  if (oi >= 0) {
    const post *p = &BT.posts[oi];
    float sx, sy, d = v3len(v3sub(p->pos, f->mv.position));
    bool on = kc_project(h, v3add(p->pos, v3(0, 2.6f, 0)), &sx, &sy);
    if (!on) { /* behind or off to a side: at the edge on its side */
      float yaw = cw_wrap_angle(cw_atan2(-(p->pos.x - KC.eye[h].x), -(p->pos.z - KC.eye[h].z)) - KC.yaw_now[h]);
      sx = yaw > 0 ? r.x + u * 2.2f : r.x + r.w - u * 2.2f, sy = r.y + r.h * 0.45f;
    }
    bool inside = d < p->r;
    float m = u * 0.55f;
    cw_color c = p->owner >= 0 ? bside(p->owner, 0.9f) : cw_rgba(1, 1, 1, 0.9f);
    cw_vec2 dm[4] = { { sx, sy - m }, { sx + m, sy }, { sx, sy + m }, { sx - m, sy } };
    cw_r2d_polygon(dm, 4, c);
    if (inside && p->push != me) snprintf(s, sizeof s, "%s %s: clear the ring", what, p->name);
    else if (inside) snprintf(s, sizeof s, "%s %s %d%%", what, p->name, (int)(fabsf(p->meter) * 100));
    else snprintf(s, sizeof s, "%s %s  %d m", what, p->name, (int)(d + 0.5f));
    float ts = body(u * 0.8f), tw = measure(s, ts, 0).width;
    float tx = CW_CLAMP(sx - tw * 0.5f, r.x + u * 0.3f, r.x + r.w - tw - u * 0.3f);
    text(s, tx, sy + m * 1.3f, ts, cw_rgba(1, 1, 1, 0.92f), CW_ALIGN_LEFT, 0);
  }
  /* the reveal: where the hidden last few of the other side were at the last ping, a pulse that swells and fades */
  if (BT.ping_side == !me && BT.ping_t < 150) {
    float t = (float)BT.ping_t, a = t < 18 ? t / 18.0f : 1 - (t - 18) / 132.0f;
    for (int i = 0; i < G.nf; i++) {
      const fighter *o = F(i);
      float sx, sy;
      if (!o->active || !o->alive || (o->team & 1) != !me || !kc_project(h, v3add(BT.ping[i], v3(0, 1.2f, 0)), &sx, &sy)) continue;
      cw_r2d_circle_lines(cw_v2(sx, sy), u * (0.6f + 0.6f * t / 150.0f), CW_MAX(2.0f, u * 0.12f), 24, bside(!me, 0.9f * a));
      cw_r2d_circle(cw_v2(sx, sy), u * 0.22f, 12, bside(!me, 0.9f * a));
    }
  }
}
/* the respawn panel's lines in a battle (under DOWN) */
static float battle_dead_lines(int h, const fighter *f, cw_rect r, float u, float cx, float dy) {
  int s = f->team & 1;
  char line[80];
  cw_color wc = cw_rgba(1, 1, 1, 0.9f);
  if (BT.pool[s] > 0) {
    int pi = BT.spawn_post[h] >= 0 && BT.spawn_post[h] < BT.nposts && BT.posts[BT.spawn_post[h]].owner == s ? BT.spawn_post[h] : post_front(s);
    snprintf(line, sizeof line, "back in %d at the %s", CW_MAX(1, (150 - f->dead_ticks + 59) / 60), pi >= 0 ? BT.posts[pi].name : "base");
    dy += text(line, cx - r.w / 2, dy, u * 1.2f, wc, CW_ALIGN_CENTER, r.w).height;
    int owned = 0;
    for (int i = 0; i < BT.nposts; i++) owned += BT.posts[i].owner == s;
    if (owned > 1) {
      float hs = body(u * 0.9f);
      ptext("{move_left|move_right} choose the post", h, cx - r.w / 2, dy, hs, cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, r.w);
      dy += cw_prompt_text_size(G.font, "{move_left|move_right} choose the post", h, hs).y;
    }
    return dy;
  }
  if (BT.sw_reference) return dy + text("NO REINFORCEMENTS LEFT", cx - r.w / 2, dy, u * 1.2f, wc, CW_ALIGN_CENTER, r.w).height;
  snprintf(line, sizeof line, "LAST STAND: %d left", BT.alive[s]);
  dy += text(line, cx - r.w / 2, dy, u * 1.2f, bside(s, 1), CW_ALIGN_CENTER, r.w).height;
  if ((TK[h].phase == 1 || TK[h].phase == 2) && TK[h].ncand > 0) {
    const fighter *b = F(TK[h].cand[TK[h].pick]);
    snprintf(line, sizeof line, "%s %s, %d health", TK[h].phase == 2 ? "taking over" : "you take over", b->name, CW_MAX(0, b->hp));
    dy += text(line, cx - r.w / 2, dy, u * 1.1f, wc, CW_ALIGN_CENTER, r.w).height;
    if (TK[h].phase == 1 && TK[h].ncand > 1) {
      float hs = body(u * 0.9f);
      ptext("{grenade|fire} another soldier", h, cx - r.w / 2, dy, hs, cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, r.w);
      dy += cw_prompt_text_size(G.font, "{grenade|fire} another soldier", h, hs).y;
    }
  } else if (TK[h].phase == 3 && TK[h].watch >= 0 && TK[h].watch < G.nf) {
    snprintf(line, sizeof line, "OUT: watching %s", F(TK[h].watch)->name);
    dy += text(line, cx - r.w / 2, dy, u * 1.1f, wc, CW_ALIGN_CENTER, r.w).height;
  }
  return dy;
}

static void hud_view(int h) {
  fighter *f = SF(h);
  cw_rect r = cw_shell_player_rect(h);
  float u = r.h / 22.0f; /* a unit that scales with the view's height */
  if (u < 8) u = 8;
  float cx = r.x + r.w / 2, cy = r.y + r.h / 2, kill_bottom = 0; /* kill_bottom: the kill line's bottom, when one shows */
  /* damage flash at the edges */
  if (G.dmg_flash[h] > 0.02f && !G.sw_no_cues) {
    cw_color c = cw_rgba(0.9f, 0.05f, 0.05f, G.dmg_flash[h] * 0.45f);
    cw_r2d_rect(cw_rect_make(r.x, r.y, r.w, u * 0.8f), c);
    cw_r2d_rect(cw_rect_make(r.x, r.y + r.h - u * 0.8f, r.w, u * 0.8f), c);
    cw_r2d_rect(cw_rect_make(r.x, r.y, u * 0.8f, r.h), c);
    cw_r2d_rect(cw_rect_make(r.x + r.w - u * 0.8f, r.y, u * 0.8f, r.h), c);
  }
  /* where the hit came from: a bar on a ring around the crosshair, rotated to the shooter's bearing */
  if (G.dmg_flash[h] > 0.05f && f->alive && !G.sw_no_cues) {
    float sa, ca;
    cw_sincos(G.dmg_dir[h], &sa, &ca);
    float r0 = CW_MIN(r.w, r.h) * 0.26f, r1 = r0 + u * 1.6f;
    cw_r2d_line(cw_v2(cx - sa * r0, cy - ca * r0), cw_v2(cx - sa * r1, cy - ca * r1), CW_MAX(3.0f, u * 0.4f), cw_rgba(1, 0.15f, 0.1f, CW_MIN(1.0f, G.dmg_flash[h] * 1.4f)));
  }
  /* low health: a slow red pulse on the view's edges */
  if (f->alive && f->hp <= 30) {
    float pulse = 0.5f + 0.5f * cw_sin((float)G.ticks * 0.14f);
    cw_color c = cw_rgba(0.9f, 0.05f, 0.05f, 0.12f + 0.2f * pulse);
    cw_r2d_rect(cw_rect_make(r.x, r.y, r.w, u * 0.5f), c);
    cw_r2d_rect(cw_rect_make(r.x, r.y + r.h - u * 0.5f, r.w, u * 0.5f), c);
    cw_r2d_rect(cw_rect_make(r.x, r.y, u * 0.5f, r.h), c);
    cw_r2d_rect(cw_rect_make(r.x + r.w - u * 0.5f, r.y, u * 0.5f, r.h), c);
  }
  if (f->alive) {
    /* crosshair, a gap that opens with the weapon's spread, a hit marker in red */
    float gap = u * (0.35f + (f->weapon == 1 ? 1.2f : 0.25f) + f->kick * 20 + (f->weapon == 0 ? f->recoil_p * 40 : 0)), len = u * 0.7f, th = CW_MAX(2.0f, u * 0.12f);
    cw_color cc = cw_rgba(1, 1, 1, 0.9f);
    cw_r2d_rect(cw_rect_make(cx - gap - len, cy - th / 2, len, th), cc);
    cw_r2d_rect(cw_rect_make(cx + gap, cy - th / 2, len, th), cc);
    cw_r2d_rect(cw_rect_make(cx - th / 2, cy - gap - len, th, len), cc);
    cw_r2d_rect(cw_rect_make(cx - th / 2, cy + gap, th, len), cc);
    cw_r2d_rect(cw_rect_make(cx - th / 2, cy - th / 2, th, th), cw_rgba(1, 1, 1, 1));
    if (G.hit_marker[h] > 0.05f) {
      float a = G.hit_marker[h], d = u * 0.9f;
      cw_color hc = cw_rgba(1, 0.2f, 0.15f, a);
      cw_r2d_line(cw_v2(cx - d, cy - d), cw_v2(cx - d * 0.45f, cy - d * 0.45f), th, hc);
      cw_r2d_line(cw_v2(cx + d, cy - d), cw_v2(cx + d * 0.45f, cy - d * 0.45f), th, hc);
      cw_r2d_line(cw_v2(cx - d, cy + d), cw_v2(cx - d * 0.45f, cy + d * 0.45f), th, hc);
      cw_r2d_line(cw_v2(cx + d, cy + d), cw_v2(cx + d * 0.45f, cy + d * 0.45f), th, hc);
    }
    if (G.head_marker[h] > 0.05f) { /* a head shot: a white cross outside the red one, and the word */
      float a = G.head_marker[h], d = u * 1.35f;
      cw_color wc = cw_rgba(1, 1, 1, a);
      cw_r2d_line(cw_v2(cx - d, cy - d), cw_v2(cx - d * 0.62f, cy - d * 0.62f), th, wc);
      cw_r2d_line(cw_v2(cx + d, cy - d), cw_v2(cx + d * 0.62f, cy - d * 0.62f), th, wc);
      cw_r2d_line(cw_v2(cx - d, cy + d), cw_v2(cx - d * 0.62f, cy + d * 0.62f), th, wc);
      cw_r2d_line(cw_v2(cx + d, cy + d), cw_v2(cx + d * 0.62f, cy + d * 0.62f), th, wc);
      float hs = body(u * 0.95f); /* the word's bottom clear of the white cross at any text size */
      text("HEAD", r.x, fminf(cy - u * 3.2f, cy - d - u * 0.3f - measure("HEAD", hs, 0).height), hs, wc, CW_ALIGN_CENTER, r.w);
    }
  }
  /* a kill: a gold cross over the crosshair and the victim's name, longer than a hit marker */
  if (G.kill_flash[h] > 0.05f) {
    float a = CW_MIN(1.0f, G.kill_flash[h] * 1.5f), d = u * 1.2f;
    cw_color kc = cw_rgba(1, 0.85f, 0.2f, a);
    cw_r2d_line(cw_v2(cx - d, cy - d), cw_v2(cx + d, cy + d), CW_MAX(3.0f, u * 0.2f), kc);
    cw_r2d_line(cw_v2(cx + d, cy - d), cw_v2(cx - d, cy + d), CW_MAX(3.0f, u * 0.2f), kc);
    char ks[40];
    /* a frag hard on the last one is called out (double, triple), and a run of them (three, five, ten without dying) */
    static const char *MULTI[] = { "", "", "DOUBLE FRAG", "TRIPLE FRAG", "MULTI FRAG" };
    const fighter *kf = SF(h);
    if (kf->multi >= 2) snprintf(ks, sizeof ks, "%s  %s", MULTI[CW_MIN(kf->multi, 4)], G.kill_name[h]);
    else if (G.kill_streak[h] >= 3) snprintf(ks, sizeof ks, "FRAGGED %s  %d IN A ROW", G.kill_name[h], G.kill_streak[h]);
    else snprintf(ks, sizeof ks, "FRAGGED %s", G.kill_name[h]);
    float ky = cy + u * 2.6f, ksz = body(u * 1.2f);
    kill_bottom = ky + text(ks, r.x, ky, ksz, cw_rgba(1, 0.85f, 0.2f, a), CW_ALIGN_CENTER, r.w).height;
  }
  /* health and armour bars, bottom left */
  float bw = r.w * 0.28f, bh = u * 0.7f, bx = r.x + u, by = r.y + r.h - u * 3.2f, bottom = r.y + r.h;
  cw_r2d_rect(cw_rect_make(bx, by, bw, bh), cw_rgba(0, 0, 0, 0.55f));
  float hpk = cw_clampf((float)f->hp / 100.0f, 0, 1);
  cw_r2d_rect(cw_rect_make(bx, by, bw * hpk, bh), f->hp > 30 ? cw_rgba(0.25f, 0.85f, 0.35f, 0.95f) : cw_rgba(0.95f, 0.25f, 0.2f, 0.95f));
  cw_r2d_rect(cw_rect_make(bx, by + bh * 1.4f, bw, bh * 0.6f), cw_rgba(0, 0, 0, 0.55f));
  cw_r2d_rect(cw_rect_make(bx, by + bh * 1.4f, bw * cw_clampf((float)f->armor / 100.0f, 0, 1), bh * 0.6f), cw_rgba(0.3f, 0.6f, 1, 0.95f));
  char s[48];
  float big = u * 1.8f, block_top = by; /* block_top: the bottom rows' top, what the lines above them stay over */
  snprintf(s, sizeof s, "%d", f->hp > 0 ? f->hp : 0);
  float ny = fminf(by - u * 0.45f, bottom - u * 0.4f - measure(s, big, 0).height); /* a larger text size grows it up, not off the view */
  text(s, bx + bw + u * 0.4f, ny, big, cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT, 0);
  block_top = fminf(block_top, ny);
  /* weapon and ammo, bottom right */
  /* the name over a big ammo count: two short lines, so neither meets the health number in a half-width view */
  snprintf(s, sizeof s, "%d", f->ammo[f->weapon]);
  float ay = fminf(bottom - u * 3.4f, bottom - u * 1.1f - measure(s, big, 0).height); /* over the weapon pips */
  text(s, r.x, ay, big, cw_rgba(1, 0.9f, 0.6f, 1), CW_ALIGN_RIGHT, r.w - u);
  float wsz = body(u * 0.85f), wy = fminf(bottom - u * 4.8f, ay - measure(W[f->weapon].name, wsz, 0).height * 0.92f);
  text(W[f->weapon].name, r.x, wy, wsz, cw_rgba(1, 0.9f, 0.6f, 0.85f), CW_ALIGN_RIGHT, r.w - u);
  block_top = fminf(block_top, wy);
  /* weapon pips */
  for (int k = 0; k < NWEAP; k++) {
    bool have = f->have >> k & 1;
    cw_color c = k == f->weapon ? cw_rgba(1, 0.85f, 0.3f, 1) : have ? cw_rgba(1, 1, 1, 0.6f) : cw_rgba(1, 1, 1, 0.15f);
    cw_r2d_rect(cw_rect_make(r.x + r.w - u * (1 + 1.3f * (float)(NWEAP - k)), r.y + r.h - u * 0.9f, u * 1.0f, u * 0.25f), c);
  }
  /* grenades: one olive ball each, left of the weapon pips */
  for (int k = 0; k < 4; k++) {
    cw_vec2 gc = cw_v2(r.x + r.w - u * (1 + 1.3f * (float)NWEAP) - u * (0.55f + 0.75f * (float)k), r.y + r.h - u * 0.78f);
    if (k < f->grenades) cw_r2d_circle(gc, u * 0.27f, 14, cw_rgba(0.62f, 0.72f, 0.42f, 0.95f));
    else cw_r2d_circle_lines(gc, u * 0.27f, CW_MAX(1.5f, u * 0.06f), 14, cw_rgba(1, 1, 1, k < 2 ? 0.3f : 0.12f));
  }
  /* score, top left: each line under the last as measured */
  float top_left = r.y + u * 0.5f, top_mid = top_left;
  if (G.mode == MODE_COOP) snprintf(s, sizeof s, "WAVE %d   LIVES %d", G.wave, G.lives);
  else snprintf(s, sizeof s, "FRAGS %d", f->score);
  cw_text_metrics tm = text(s, r.x + u, top_left, u * 1.3f, cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT, 0);
  float top_right = r.x + u + tm.width;
  top_left += tm.height;
  if (G.mode == MODE_TDM) {
    snprintf(s, sizeof s, "%s %d : %d %s", TEAM_NAME[0], G.team_score[0], G.team_score[1], TEAM_NAME[1]);
    tm = text(s, r.x + u, top_left, body(u * 0.9f), cw_rgba(TEAM_RGB[f->team & 1][0], TEAM_RGB[f->team & 1][1], TEAM_RGB[f->team & 1][2], 1), CW_ALIGN_LEFT, 0);
    top_left += tm.height, top_right = fmaxf(top_right, r.x + u + tm.width);
  }
  if (f->alive && f->power != POW_NONE) { /* the power-up held, in its colour, and its seconds left */
    const float *pc = POW_RGB[f->power];
    snprintf(s, sizeof s, "%s  %d", POW_NAME[f->power], (f->power_t + 59) / 60);
    tm = text(s, r.x + u, top_left, body(u * 1.0f), cw_rgba(pc[0], pc[1], pc[2], 1), CW_ALIGN_LEFT, 0);
    top_left += tm.height, top_right = fmaxf(top_right, r.x + u + tm.width);
  }
  /* top centre: the time left and who leads (deathmatch: the frag leader; team deathmatch shows both teams on the left) */
  if (G.state == ST_PLAY && G.mode != MODE_COOP) {
    int secs = CW_MAX(0, (G.time_limit - G.match_ticks) / 60);
    snprintf(s, sizeof s, "%d:%02d", secs / 60, secs % 60);
    if (cx - measure(s, u * 1.3f, 0).width * 0.5f < top_right + u * 0.6f) top_mid = fmaxf(top_mid, top_left); /* it would meet the score: under it */
    tm = text(s, r.x, top_mid, u * 1.3f, secs < 30 ? cw_rgba(1, 0.45f, 0.35f, 1) : cw_rgba(1, 1, 1, 0.95f), CW_ALIGN_CENTER, r.w); /* top centre: the kill feed owns the screen's top right */
    top_mid += tm.height, top_right = fmaxf(top_right, cx + tm.width * 0.5f);
    if (G.mode == MODE_DM) {
      int lead = -1, ls = -9999;
      for (int i = 0; i < G.nf; i++) if (F(i)->active && F(i)->score > ls) ls = F(i)->score, lead = i;
      if (lead >= 0) {
        bool me = lead == h;
        if (me) snprintf(s, sizeof s, "YOU LEAD  %d / %d", ls, G.frag_limit);
        else snprintf(s, sizeof s, "LEADER %s  %d / %d", F(lead)->name, ls, G.frag_limit);
        tm = text(s, r.x, top_mid, body(u * 0.85f), me ? cw_rgba(1, 0.85f, 0.3f, 1) : cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, r.w);
        top_mid += tm.height, top_right = fmaxf(top_right, cx + tm.width * 0.5f);
      }
    }
  }
  if (battle() && G.state == ST_PLAY) {
    hud_battle(h, f, r, u, &top_left, &top_mid);
    if (BT.banner_t > 0) { /* a call across the view: swells in, holds, fades */
      float a = CW_MIN(1.0f, CW_MIN((float)(240 - BT.banner_t) / 15.0f, (float)BT.banner_t / 40.0f));
      text(BT.banner, r.x, r.y + r.h * 0.24f, body(u * 1.5f), cw_rgba(1, 0.9f, 0.5f, a), CW_ALIGN_CENTER, r.w);
    }
  }
  hud_top_bottom[h] = fmaxf(top_left, top_mid), hud_top_right[h] = top_right;
  if (!f->alive) { /* a stack from the middle down: each line under the last as measured */
    float dy = cy - u * 3;
    dy += text("DOWN", cx - r.w / 2, dy, u * 3.2f, cw_rgba(1, 0.3f, 0.25f, 1), CW_ALIGN_CENTER, r.w).height * 0.86f;
    if (battle()) dy = battle_dead_lines(h, f, r, u, cx, dy);
    else if (!(G.mode == MODE_COOP && G.lives <= 0) && away(h)) /* this seat sits out until someone plays it */
      dy += text("press any button to come back in", cx - r.w / 2, dy, u * 1.2f, cw_rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER, r.w).height;
    else if (!(G.mode == MODE_COOP && G.lives <= 0)) {
      char rs[24];
      snprintf(rs, sizeof rs, "back in %d", CW_MAX(1, (150 - f->dead_ticks + 59) / 60));
      dy += text(rs, cx - r.w / 2, dy, u * 1.2f, cw_rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER, r.w).height;
    } else dy += text("NO LIVES LEFT", cx - r.w / 2, dy, u * 1.2f, cw_rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER, r.w).height;
    if (G.killed_by[h][0]) { /* who, with what, from how far, and how much health they had left */
      char by[64];
      int wk = G.kc_weapon[h];
      const char *wn = wk >= 0 && wk < NWEAP ? W[wk].name : wk == WK_GRENADE ? "GRENADE" : wk == WK_MELEE ? "BASH" : "";
      snprintf(by, sizeof by, "by %s", G.killed_by[h]);
      dy += text(by, cx - r.w / 2, dy, u * 1.1f, cw_rgba(1, 0.75f, 0.65f, 0.95f), CW_ALIGN_CENTER, r.w).height;
      if (wn[0]) {
        snprintf(by, sizeof by, "%s from %d m, %d health left", wn, (int)(G.kc_dist[h] + 0.5f), G.kc_hp[h]);
        text(by, cx - r.w / 2, dy, body(u * 0.9f), cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, r.w);
      }
    }
    float kx, ky; /* the killer cam: a marker over whoever downed this person */
    if (!G.sw_no_killcam && G.kc_who[h] >= 0 && f->dead_ticks >= 12 && kc_project(h, v3add(F(G.kc_who[h])->mv.position, v3(0, PLAYER_HT + 0.35f, 0)), &kx, &ky)) {
      float s = u * 0.7f, a = CW_MIN(1.0f, (float)(f->dead_ticks - 12) / 12.0f);
      cw_vec2 p[3] = { { kx - s, ky - s * 1.2f }, { kx + s, ky - s * 1.2f }, { kx, ky } };
      cw_r2d_polygon(p, 3, cw_rgba(1, 0.3f, 0.25f, 0.9f * a));
    }
  }
  if (f->spawn_guard > 0 && f->alive) cw_r2d_rect_lines(r, 3, cw_rgba(0.4f, 0.8f, 1, 0.5f));
  bool hints = cw_shell_screen_now() != CW_SHELL_PAUSE && G.state == ST_PLAY && G.match_ticks < 60 * 9 && f->alive && f->deaths == 0 && f->score == 0;
  if (hints) { /* a newcomer's first seconds: the controls */
    float a = CW_MIN(1.0f, (float)(60 * 9 - G.match_ticks) / 60.0f) * 0.92f;
    float hs = body(CW_MIN(u * 0.82f, r.w / 30.0f)); /* the longer line is about 26 of these wide: it fits a half width view */
    /* the hints, packed into as few lines as fit the view's width at this text size */
    static const char *const PART[] = { "{move_left|move_right|move_forward|move_back} move", "{look_left|look_right|look_up|look_down} look", "{fire} fire",
                                        "{jump} jump", "{grenade} grenade", "{melee} bash", "{weapon_prev|weapon_next} change weapon", "{sprint} sprint" };
    enum { NPART = sizeof PART / sizeof PART[0] };
    char lines[NPART][320];
    int nl = 0;
    for (int k = 0; k < NPART; k++) {
      char join[sizeof lines[0]];
      bool brk = k == 3; /* the first three on one line, the rest on the next, as they read best */
      if (nl > 0 && !brk) {
        snprintf(join, sizeof join, "%s   %s", lines[nl - 1], PART[k]);
        if (cw_prompt_text_size(G.font, join, h, hs).x <= r.w * 0.94f) { memcpy(lines[nl - 1], join, sizeof lines[0]); continue; }
      }
      snprintf(lines[nl++], sizeof lines[0], "%s", PART[k]);
    }
    float total = 0, lh[NPART];
    for (int k = 0; k < nl; k++) total += (lh[k] = cw_prompt_text_size(G.font, lines[k], h, hs).y * 1.15f);
    /* between the crosshair (and a kill's line) and the bottom rows; when a large text size leaves room for fewer
     * lines than there are, they take turns, a page every two and a half seconds */
    float top = fmaxf(cy + u * 1.8f, kill_bottom), bot = block_top - u * 0.3f;
    int first = 0, count = nl;
    if (total > bot - top) {
      int starts[NPART], np = 0;
      for (int k = 0; k < nl;) {
        float used = 0;
        starts[np++] = k;
        do used += lh[k++]; while (k < nl && used + lh[k] <= bot - top);
      }
      int pg = (G.match_ticks / 150) % np;
      first = starts[pg], count = (pg + 1 < np ? starts[pg + 1] : nl) - first;
      total = 0;
      for (int k = first; k < first + count; k++) total += lh[k];
    }
    float hy = fminf(fmaxf(top, fminf(r.y + r.h * 0.66f, bot - total)), bot - total); /* no room: nearer the crosshair, never on the rows */
    for (int k = first; k < first + count; k++) ptext(lines[k], h, r.x, hy, hs, cw_rgba(1, 1, 1, a), CW_ALIGN_CENTER, r.w), hy += lh[k];
  }
  if (G.pick_t[h] > 0.08f && f->alive) { /* over the bottom rows; while a kill's line holds that space, the kill's line wins;
                                          * while the controls show there, over the crosshair if there is room */
    float psz = body(u * 1.05f), ph = measure(G.pick_text[h], psz, r.w).height, py = fminf(r.y + r.h - u * 6.6f, block_top - ph);
    bool room = !(kill_bottom > 0 && py < kill_bottom);
    if (hints) py = cy - u * 1.8f - ph, room = py >= hud_top_bottom[h] && G.head_marker[h] <= 0.05f;
    if (room) text(G.pick_text[h], r.x, py, psz, cw_rgba(0.75f, 1, 0.8f, CW_MIN(1.0f, G.pick_t[h] * 1.6f)), CW_ALIGN_CENTER, r.w);
  }
  /* respawn timers over the item spots this player can see (not through walls): seconds until it is back */
  if (f->alive && G.state == ST_PLAY) {
    t3_vec3 e = eye_pos(f), d = look_dir(f->yaw, f->pitch);
    t3_vec3 rt = v3norm(v3cross(d, v3(0, 1, 0))), up = v3cross(rt, d);
    float fl = 1.0f / tanf(78.0f * 0.5f * 3.14159265f / 180.0f) * r.h * 0.5f;
    for (int i = 0; i < A.nitems; i++) {
      const item *it = &A.items[i];
      if (it->respawn <= 0) continue;
      t3_vec3 at = v3(it->pos.x, it->floor_y + 0.58f, it->pos.z), rel = v3sub(at, e);
      float z = v3dot(rel, d);
      if (z < 0.5f || z > 40) continue;
      float sx = cx + v3dot(rel, rt) / z * fl, sy = cy - v3dot(rel, up) / z * fl;
      if (sx < r.x || sx > r.x + r.w || sy < r.y || sy > r.y + r.h) continue;
      if (arena_ray(e, at, NULL, NULL)) continue;
      bool power = it->kind == ITEM_ROCKET || it->kind == ITEM_RAIL || it->kind == ITEM_ARMOR;
      float sz = body(u * (power ? 1.1f : 0.8f) * CW_CLAMP(12.0f / z, 0.6f, 1.4f));
      snprintf(s, sizeof s, "%d", (it->respawn + 59) / 60);
      const float *c = ITEM_RGB[it->kind];
      text(s, sx - u * 4, sy - sz * 0.6f, sz, cw_rgba(c[0], c[1], c[2], power ? 0.95f : 0.7f), CW_ALIGN_CENTER, u * 8);
    }
    if (G.mode != MODE_COOP && !PW.present && PW.timer < 60 * 20) { /* the power-up spot: what comes next and when */
      t3_vec3 at = v3add(PW.pos, v3(0, 1.0f, 0)), rel = v3sub(at, e);
      float z = v3dot(rel, d);
      if (z > 0.5f && z < 50 && !arena_ray(e, at, NULL, NULL)) {
        float sx = cx + v3dot(rel, rt) / z * fl, sy = cy - v3dot(rel, up) / z * fl;
        if (sx >= r.x && sx <= r.x + r.w && sy >= r.y && sy <= r.y + r.h) {
          float sz = body(u * 1.0f * CW_CLAMP(12.0f / z, 0.6f, 1.4f));
          const float *c = POW_RGB[PW.next];
          snprintf(s, sizeof s, "%s %d", POW_NAME[PW.next], (PW.timer + 59) / 60);
          text(s, sx - u * 8, sy - sz * 0.6f, sz, cw_rgba(c[0], c[1], c[2], 0.95f), CW_ALIGN_CENTER, u * 16);
        }
      }
    }
  }
}

/* a prompt line split at its wide gaps ("    ") into lines no wider than maxw at this size, '\n' between them */
static const char *fit_hint(const char *markup, float size, float maxw) {
  static char o[256];
  snprintf(o, sizeof o, "%s", markup);
  if (cw_prompt_text_size(G.font, o, -1, size).x <= maxw) return o;
  for (char *p = strstr(o, "    "); p; p = strstr(p + 1, "    ")) p[0] = '\n', memmove(p + 1, p + 4, strlen(p + 4) + 1);
  return o;
}

/* three players: the fourth quadrant shows the arena and the score, not a black square */
static void hud_quadrant(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float x = cs.x * 0.5f, y = cs.y * 0.5f, w = cs.x * 0.5f, h = cs.y * 0.5f, u = h / 22.0f;
  cw_r2d_rect(cw_rect_make(x, y, w, h), cw_rgba(0.035f, 0.04f, 0.05f, 1));
  float ty = y + u * 1.6f;
  ty += text(ARENA_INFO[G.arena_id].name, x, ty, u * 2.2f, cw_rgba(1, 0.85f, 0.4f, 1), CW_ALIGN_CENTER, w).height;
  ty = fmaxf(ty + text(MODE_NAME[G.mode], x, fmaxf(ty, y + u * 4.4f), body(u * 1.1f), cw_rgba(1, 1, 1, 0.75f), CW_ALIGN_CENTER, w).height, y + u * 7.0f);
  int order[MAXF], n = 0;
  for (int i = 0; i < G.nf; i++) if (F(i)->active) order[n++] = i;
  for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (F(order[b])->score > F(order[a])->score) { int t = order[a]; order[a] = order[b]; order[b] = t; }
  char s[48];
  if (G.mode == MODE_COOP) {
    snprintf(s, sizeof s, "WAVE %d   LIVES %d", G.wave, G.lives);
    text(s, x, fmaxf(ty, y + u * 7.2f), u * 1.4f, cw_rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER, w);
    return;
  }
  /* the board, best first, as many rows as the quadrant holds at this text size; the rest as a count */
  float rs = u * 1.3f, step = fmaxf(u * 1.7f, measure("Ag", rs, 0).height * 1.08f), room = y + h - u * 0.6f - ty;
  int fit = CW_MAX(1, (int)(room / step)), show = n <= fit ? n : fit - 1;
  for (int k = 0; k < show && k < 8; k++) {
    const fighter *f = F(order[k]);
    snprintf(s, sizeof s, "%-8s  %3d frags", f->name, f->score);
    text(s, x, ty + step * (float)k, rs, f->human >= 0 ? cw_rgba(1, 1, 0.6f, 1) : cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, w);
  }
  if (show < n && show < 8) {
    snprintf(s, sizeof s, "and %d more", CW_MIN(n, 8) - show);
    text(s, x, ty + step * (float)show, rs, cw_rgba(1, 1, 1, 0.6f), CW_ALIGN_CENTER, w);
  }
}

static void hud_feed(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  if (G.players == 3 && G.state == ST_PLAY) hud_quadrant();
  float u = cs.y / 40;
  /* the feed sits in the top right view, under that view's top rows, and keeps to its top part: at a large text size
   * it shows the newest lines that fit there */
  float fs = u * 1.05f, step = fmaxf(u * 1.3f, measure("Ag", fs, 0).height), feed_w = 0;
  for (int i = 0; i < MAX_FEED; i++) if (G.feed[i].ticks > 0) feed_w = fmaxf(feed_w, measure(G.feed[i].text, fs, 0).width);
  float y = u, limit = cs.y * 0.4f, wave_y = cs.y * 0.14f;
  for (int h = 0; h < G.players && h < MAXH; h++) {
    cw_rect r = cw_shell_player_rect(h);
    if (hud_top_bottom[h] <= 0 || r.y > 1) continue;
    wave_y = fmaxf(wave_y, hud_top_bottom[h] + u * 0.3f); /* the wave call: under every top view's rows */
    if (r.x + r.w < cs.x - 1) continue;
    limit = r.y + r.h * 0.4f;
    if (cs.x - u - feed_w < hud_top_right[h] + u * 0.5f) y = fmaxf(y, hud_top_bottom[h] + u * 0.2f); /* it would meet them */
  }
  int room = CW_MAX(1, (int)((limit - y) / step));
  for (int i = 0; i < MAX_FEED && room > 0; i++) {
    if (G.feed[i].ticks <= 0) continue;
    float a = CW_MIN(1.0f, (float)G.feed[i].ticks / 30.0f);
    text(G.feed[i].text, 0, y, fs, cw_rgba(1, 1, 1, a), CW_ALIGN_RIGHT, cs.x - u);
    y += step, room--;
  }
  /* co-op: the next wave announced while the last is cleared, so the lull reads as a pause and not a stall */
  if (G.state == ST_PLAY && G.mode == MODE_COOP && G.wave_state == 0) {
    char s[48];
    snprintf(s, sizeof s, "WAVE %d IN %d", G.wave + 1, G.wave_timer / 60 + 1);
    wave_y += text(s, 0, wave_y, u * 2.2f, cw_rgba(1, 0.85f, 0.3f, 1), CW_ALIGN_CENTER, cs.x).height;
  }
  if (G.state == ST_PLAY && auto_note_until > G.steps) { /* Auto stepped the look down: say so, fading out */
    float a = CW_MIN(1.0f, (float)(auto_note_until - G.steps) / 60.0f), ns = body(u * 1.0f);
    float w = fminf(cs.x * 0.9f, measure(auto_note, ns, cs.x * 0.9f).width + u), h = measure(auto_note, ns, cs.x * 0.9f).height;
    cw_r2d_rect(cw_rect_make((cs.x - w) * 0.5f, wave_y - u * 0.2f, w, h + u * 0.4f), cw_rgba(0, 0, 0, 0.55f * a));
    text(auto_note, cs.x * 0.05f, wave_y, ns, cw_rgba(1, 1, 1, 0.9f * a), CW_ALIGN_CENTER, cs.x * 0.9f);
  }
}

/* a small triangle: more rows above (up) or below */
static void more_mark(float cx, float y, float d, bool up) {
  float s = d * 0.28f;
  cw_vec2 p[3] = { { cx - s, up ? y + s * 0.5f : y - s * 0.5f }, { cx + s, up ? y + s * 0.5f : y - s * 0.5f }, { cx, up ? y - s * 0.5f : y + s * 0.5f } };
  cw_r2d_polygon(p, 3, cw_rgba(1, 1, 1, 0.7f));
}

static void hud_setup(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 18;
  cw_r2d_rect(cw_rect_make(0, 0, cs.x, cs.y), cw_rgba(0, 0, 0, 0.7f));
  /* from the top down, each line under the last as measured; the rows scroll when a large text size leaves no room */
  float y = u * 1.2f;
  y += text("ARENA", 0, y, u * 2.4f, cw_rgba(1, 0.85f, 0.4f, 1), CW_ALIGN_CENTER, cs.x).height;
  float hs = u * 0.9f;
  const char *hint = fit_hint("choose the match, then press {ui_start} or {fire}    {ui_back} back to the title", hs, cs.x * 0.94f);
  y = fmaxf(y, u * 3.6f);
  ptext(hint, -1, 0, y, hs, cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, cs.x);
  y += cw_prompt_text_size(G.font, hint, -1, hs).y + u * 0.4f;
  char s[96];
  const char *rows[SETUP_ROWS];
  char r0[64], r1[64], r2[64], r3[64], r4[64], r5[64], r6[64];
  snprintf(r0, sizeof r0, "<  %s  >", ARENA_INFO[G.arena_id].name);
  snprintf(r1, sizeof r1, "<  %s  >", MODE_NAME[G.mode]);
  if (G.mode == MODE_COOP) snprintf(r2, sizeof r2, "waves of bots");
  else if (battle()) snprintf(r2, sizeof r2, "<  %d reinforcements a side  >", POOL_CHOICES[BT.pool_setting]);
  else if (G.bots_setting < 0) snprintf(r2, sizeof r2, "<  AUTO: %d bots  >", auto_bots());
  else snprintf(r2, sizeof r2, "<  %d bots  >", G.bots_setting);
  static const char *LEVEL[BOT_LEVELS] = { "EASY", "NORMAL", "VETERAN", "HARD" };
  snprintf(r3, sizeof r3, "<  bots: %s  >", LEVEL[G.bot_level]);
  rows[0] = r0, rows[1] = r1, rows[2] = r2, snprintf(r4, sizeof r4, "<  aim assist: %s  >", G.aim_assist ? "ON" : "OFF");
  snprintf(r5, sizeof r5, "<  rumble: %s  >", G.rumble_off ? "OFF" : "ON");
  snprintf(r6, sizeof r6, "<  rules: %s  >", RULE_NAME[G.rules]);
  rows[3] = r3, rows[4] = r4, rows[5] = r5, rows[6] = r6;
  /* the foot: the arena's line and the player count, from the bottom up */
  float fs = u * 0.9f, foot = cs.y - u * 0.5f;
  snprintf(s, sizeof s, "%d player%s", G.players, G.players == 1 ? "" : "s");
  float ph = measure(s, fs, cs.x * 0.94f).height, bh = measure(ARENA_INFO[G.arena_id].blurb, fs, cs.x * 0.94f).height;
  float py = fminf(u * 15.9f, foot - ph), byy = fminf(u * 14.7f, py - bh);
  float rs = u * 1.3f, d = measure("Ag", rs, 0).height, step = fmaxf(u * 1.35f, d * 1.08f);
  float y0 = fmaxf(y, u * 5.2f), y1 = byy - u * 0.3f;
  bool blurb = SETUP_ROWS * step <= y1 - y0; /* the arena's line gives way to the rows when they need its room */
  if (!blurb) y1 = py - u * 0.3f;
  if (SETUP_ROWS * step > y1 - y0) { /* still short: smaller rows (down to 0.8 of the size) before any row scrolls out of sight, so a
                             * newcomer sees the arena and mode rows on the panel's first frame */
    float rs2 = fmaxf(u * 1.04f, rs * fminf(1.0f, (y1 - y0) / (SETUP_ROWS * step))), d2 = measure("Ag", rs2, 0).height, step2 = fmaxf(u * 1.08f, d2 * 1.04f);
    if (SETUP_ROWS * step2 <= y1 - y0) rs = rs2, d = d2, step = step2;
  }
  text(s, cs.x * 0.03f, py, fs, cw_rgba(1, 1, 1, 0.7f), CW_ALIGN_CENTER, cs.x * 0.94f);
  if (blurb) text(ARENA_INFO[G.arena_id].blurb, cs.x * 0.03f, byy, fs, cw_rgba(1, 1, 1, 0.7f), CW_ALIGN_CENTER, cs.x * 0.94f);
  int first = 0, count = SETUP_ROWS;
  if (SETUP_ROWS * step > y1 - y0) { /* scroll: the selected row stays in view, a mark says there is more */
    y0 += d * 0.4f;
    count = CW_MAX(1, (int)((y1 - y0 - d * 0.4f) / step));
    first = CW_CLAMP(G.setup_row - count / 2, 0, SETUP_ROWS - count);
  }
  for (int i = first; i < first + count && i < SETUP_ROWS; i++) {
    bool sel = i == G.setup_row;
    text(rows[i], 0, y0 + step * (float)(i - first), rs, sel ? cw_rgba(1, 0.9f, 0.3f, 1) : cw_rgba(1, 1, 1, 0.75f), CW_ALIGN_CENTER, cs.x);
  }
  if (first > 0) more_mark(cs.x * 0.5f, y0 - d * 0.25f, d, true);
  if (first + count < SETUP_ROWS) more_mark(cs.x * 0.5f, y0 + step * (float)count + d * 0.1f, d, false);
}

static void hud_over(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 22;
  char s[96];
  if (battle()) {
    if (G.winner == 2) snprintf(s, sizeof s, "A DRAW  -  %d against %d", BT.pool[0] + BT.alive[0], BT.pool[1] + BT.alive[1]);
    else if (G.match_ticks >= G.time_limit) snprintf(s, sizeof s, "%s WINS ON STRENGTH  -  %d against %d", BSIDE_NAME[G.winner & 1], BT.pool[G.winner & 1] + BT.alive[G.winner & 1], BT.pool[!(G.winner & 1)] + BT.alive[!(G.winner & 1)]);
    else if (BT.sw_reference) snprintf(s, sizeof s, "%s WINS  -  %s is out of reinforcements", BSIDE_NAME[G.winner & 1], BSIDE_NAME[!(G.winner & 1)]);
    else snprintf(s, sizeof s, "%s WINS  -  %s's last soldier is down", BSIDE_NAME[G.winner & 1], BSIDE_NAME[!(G.winner & 1)]);
  } else
  if (G.mode == MODE_COOP) snprintf(s, sizeof s, G.winner == 0 ? "ARENA HELD  -  all %d waves cleared" : "OVERRUN  -  wave %d", G.winner == 0 ? 6 : G.wave);
  else if (G.mode == MODE_TDM) snprintf(s, sizeof s, "%s TEAM WINS", TEAM_NAME[G.winner & 1]);
  else snprintf(s, sizeof s, "%s WINS", G.winner >= 0 ? F(G.winner)->name : "NOBODY");
  float ts = u * 2.2f, th = measure(s, ts, cs.x * 0.94f).height;
  /* the board, best first */
  int order[MAXF], n = 0;
  for (int i = 0; i < G.nf; i++) if (F(i)->active || F(i)->deaths || F(i)->score) order[n++] = i;
  for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (F(order[b])->score > F(order[a])->score) { int t = order[a]; order[a] = order[b]; order[b] = t; }
  n = CW_MIN(n, 10);
  const char *cont = "press {ui_start} or {fire} to continue";
  float bs = u * 1.0f, d = measure("Ag", bs, 0).height, step = fmaxf(u * 1.25f, d * 1.05f), cs_ = u * 0.9f, ch = cw_prompt_text_size(G.font, cont, -1, cs_).y;
  /* the panel: the title, the board and the prompt, each measured; a board too long for the screen at this text size
   * pages through, a page every three seconds */
  /* the awards under the board: the longest run of frags, the most grenade frags, the most bashes (each when someone has one) */
  char aw[160] = "";
  {
    int bs_i = -1, gr_i = -1, ba_i = -1;
    for (int k = 0; k < n; k++) {
      fighter *f = F(order[k]);
      if (f->best_streak >= 3 && (bs_i < 0 || f->best_streak > F(bs_i)->best_streak)) bs_i = order[k];
      if (f->gren_kills > 0 && (gr_i < 0 || f->gren_kills > F(gr_i)->gren_kills)) gr_i = order[k];
      if (f->bash_kills > 0 && (ba_i < 0 || f->bash_kills > F(ba_i)->bash_kills)) ba_i = order[k];
    }
    size_t o = 0;
    if (bs_i >= 0) o += (size_t)snprintf(aw + o, sizeof aw - o, "LONGEST RUN %s %d", F(bs_i)->name, F(bs_i)->best_streak);
    if (gr_i >= 0 && o < sizeof aw) o += (size_t)snprintf(aw + o, sizeof aw - o, "%sGRENADIER %s %d", o ? "    " : "", F(gr_i)->name, F(gr_i)->gren_kills);
    if (ba_i >= 0 && o < sizeof aw) o += (size_t)snprintf(aw + o, sizeof aw - o, "%sBRAWLER %s %d", o ? "    " : "", F(ba_i)->name, F(ba_i)->bash_kills);
    G.awards = (bs_i >= 0) + (gr_i >= 0) + (ba_i >= 0);
  }
  float ah = aw[0] ? measure(aw, cs_, cs.x * 0.94f).height + d * 0.3f : 0;
  float top = cs.y * 0.18f, ty = cs.y * 0.22f, by = fmaxf(cs.y * 0.34f, ty + th + d * 0.3f), cy = fmaxf(cs.y * 0.74f, by + step * (float)n + d * 0.2f + ah);
  int per = n, pages = 1;
  if (cy + ch > cs.y - u * 0.3f) {
    float room = cs.y - u * 0.3f - ch - d * 0.2f - by - ah;
    per = CW_MAX(1, (int)(room / step) - (n > (int)(room / step) ? 1 : 0)), pages = (n + per - 1) / per;
    cy = by + step * (float)(per + (pages > 1)) + d * 0.2f + ah;
  }
  float bottom = fmaxf(cs.y * 0.82f, cy + ch + u * 0.3f);
  cw_r2d_rect(cw_rect_make(0, top, cs.x, fminf(bottom, cs.y) - top), cw_rgba(0, 0, 0, 0.62f));
  text(s, cs.x * 0.03f, ty, ts, cw_rgba(1, 0.85f, 0.3f, 1), CW_ALIGN_CENTER, cs.x * 0.94f);
  int page = pages > 1 ? (G.over_ticks / 180) % pages : 0;
  for (int k = page * per, row = 0; k < n && row < per; k++, row++) {
    fighter *f = F(order[k]);
    snprintf(s, sizeof s, "%-8s  %3d frags  %3d deaths", f->name, f->score, f->deaths);
    text(s, 0, by + step * (float)row, bs, f->human >= 0 ? cw_rgba(1, 1, 0.6f, 1) : cw_rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER, cs.x);
  }
  if (pages > 1) {
    snprintf(s, sizeof s, "page %d of %d", page + 1, pages);
    text(s, 0, by + step * (float)per, bs, cw_rgba(1, 1, 1, 0.6f), CW_ALIGN_CENTER, cs.x);
  }
  if (aw[0]) text(aw, cs.x * 0.03f, cy - ah, cs_, cw_rgba(1, 0.85f, 0.4f, 0.95f), CW_ALIGN_CENTER, cs.x * 0.94f);
  G.cue_over = G.over_ticks > 60 * 3 && !G.sw_no_cues;
  if (G.cue_over) ptext(cont, -1, 0, cy, cs_, cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, cs.x);
}

static void render(void *user, double alpha) {
  (void)user, (void)alpha;
  cw_shell_screen sc = cw_shell_screen_now();
  cw_quality_auto_measure(sc == CW_SHELL_PLAY && G.state == ST_PLAY); /* Auto steps the preset down when play runs slow */
#ifdef ARENA_AUTOPILOT
  { static int nohud = -1; if (nohud < 0) nohud = getenv("ARENA_NOHUD") != NULL; /* ARENA_NOHUD: no HUD at all (key art) */
    if (nohud || keyart_nohud) { cw_shell_draw(); return; } }
#endif
  if (sc == CW_SHELL_PLAY || sc == CW_SHELL_PAUSE) {
    cw_r2d_begin(NULL);
    memset(hud_top_bottom, 0, sizeof hud_top_bottom);
    bool paused = !cw_shell_hud_shown(); /* the pause menu stands alone over the dimmed views */
    if (G.state == ST_PLAY && !paused) for (int h = 0; h < G.players; h++) hud_view(h); /* the results replace the views' HUD */
    if (!paused) hud_feed();
    if (G.state == ST_SETUP && !paused) hud_setup(); /* the shell's menus stand alone (cw_shell_hud_shown) */
    if (G.state == ST_OVER && !paused) hud_over();
    cw_r2d_end();
  }
  cw_shell_draw();
}

/* ── start, init ───────────────────────────────────────────────────── */

/* the glTF objects of a viewmodel sit on the viewmodel layer, never cast shadows (own body shadow would look wrong) and are never culled by their own bounds */
static void set_vm_layers(t3_object *o, uint32_t layer) {
  o->layers = layer;
  o->cast_shadow = false;
  o->receive_shadow = false; /* the gun hangs at the eye: a sun shadow across it reads as a black slab */
  o->frustum_culled = false;
  for (int i = 0; i < o->child_count; i++) set_vm_layers(o->children[i], layer);
}

/* the guns' materials get the arena's worn maps by name (the glb's uvs are box projected in gun metres): chipped paint on the
 * bodies, scratched steel, worn wood, a matte grain on gloves and cloth. Metal keeps its own colour (normal and roughness only) */
static void vm_textures(t3_object *o) {
  if (o->type == T3_MESH || o->type == T3_SKINNED_MESH) {
    t3_mesh *me = (t3_mesh *)o;
    for (int i = 0; i < me->material_count; i++) {
      t3_material *m = me->materials[i];
      const char *n = m && m->name ? m->name : "";
      int kind = -1;
      bool colour = false;
      float ns = 0.5f;
      if (!strcmp(n, "vm_body")) kind = TEX_PAINT, colour = true, ns = 0.55f;
      else if (!strcmp(n, "vm_dark")) kind = TEX_PANEL_STEEL, ns = 0.4f;
      else if (!strcmp(n, "vm_steel")) kind = TEX_STEEL, ns = 0.45f;
      else if (!strcmp(n, "vm_wood")) kind = TEX_WOOD, colour = true, ns = 0.7f;
      else if (!strcmp(n, "vm_glove") || !strcmp(n, "vm_sleeve") || !strcmp(n, "vm_rubber")) kind = TEX_CONCRETE, ns = 0.35f;
      if (kind < 0) continue;
      const tex_set *ts = tex_get(kind);
      if (colour) t3_material_set_texture(m, T3_MAP, ts->map), m->color = lin(0.55f, 0.55f, 0.55f);
      t3_material_set_texture(m, T3_NORMAL_MAP, ts->normal);
      t3_material_set_texture(m, T3_ROUGHNESS_MAP, ts->rough);
      m->normal_scale = (t3_vec2){ ns, ns };
      m->roughness = !strcmp(n, "vm_glove") || !strcmp(n, "vm_sleeve") ? 1.0f : m->roughness + 0.25f;
    }
  }
  for (int i = 0; i < o->child_count; i++) vm_textures(o->children[i]);
}

static void make_viewmodels(void) {
  for (int h = 0; h < MAXH; h++) {
    G.vm[h] = cw_node_new("Node3D", "VM");
    cw_node_set(G.vm[h], "layers", cw_val_int((int32_t)vm_layer(h))); /* player h's gun: only camera h draws it (views_mode) */
    cw_node_add_child(G.cam[h], G.vm[h]);
    for (int w = 0; w < NWEAP; w++) {
      cw_node g = cw_node_new("Node3D", "Weapon");
      cw_node_add_child(G.vm[h], g);
      G.vm_wp[h][w] = g;
      static const char *FILE[NWEAP] = { "pulse", "scatter", "rocket", "rail" };
      char path[48];
      snprintf(path, sizeof path, "models/vm_%s.glb", FILE[w]);
      size_t len;
      void *glb = cw_asset_load(path, &len);
      t3_gltf *gl = glb ? t3_gltf_parse(glb, len, NULL, NULL) : NULL; /* kept: the viewmodels live as long as the app */
      free(glb);
      if (!gl) { cw_error("arenafps: %s missing", path); continue; }
      cw_node3d_set_object(g, gl->scene);
      cw_node3d_set_scale(g, v3(VM_SCALE, VM_SCALE, VM_SCALE));
      set_vm_layers(gl->scene, vm_layer(h));
      vm_textures(gl->scene);
    }
    { /* the muzzle flash: a star sprite in the gun's own layer, additive and over-bright so the bloom picks it up, a new turn and size each shot */
      static t3_geometry *fg;
      static t3_material *fm;
      if (!fg) {
        mesh_build q = { 0 };
        int v0 = mb_vert(&q, v3(-0.5f, -0.5f, 0), v3(0, 0, 1), 0, 0), v1 = mb_vert(&q, v3(0.5f, -0.5f, 0), v3(0, 0, 1), 1, 0);
        int v2 = mb_vert(&q, v3(0.5f, 0.5f, 0), v3(0, 0, 1), 1, 1), v3i = mb_vert(&q, v3(-0.5f, 0.5f, 0), v3(0, 0, 1), 0, 1);
        mb_quad(&q, v0, v1, v2, v3i);
        fg = mb_finish(&q);
        fm = t3_mesh_basic_material_new(0xffffff);
        fm->color = (t3_color){ 3.2f, 2.4f, 1.5f };
        t3_texture *st = tex_flash();
        t3_material_set_texture(fm, T3_MAP, st);
        t3_release(st);
        fm->transparent = true;
        fm->blending = T3_ADDITIVE_BLENDING;
        fm->depth_write = false;
        fm->fog = false;
        fm->side = T3_DOUBLE_SIDE;
      }
      t3_mesh *fq = t3_mesh_new(fg, fm);
      set_vm_layers(&fq->base, vm_layer(h));
      G.vm_flash[h] = cw_node_new("MeshInstance3D", "Flash");
      cw_node_set(G.vm_flash[h], "layers", cw_val_int((int32_t)vm_layer(h)));
      cw_node_set(G.vm_flash[h], "visible", cw_val_bool(false));
      cw_node_add_child(G.vm[h], G.vm_flash[h]);
      cw_mesh3d_set_procedural(G.vm_flash[h], fq);
      t3_release(fq);
    }
  }
}

/* in setup and behind the join screen the cameras look over the arena */
static void overview_cams(void) {
  for (int h = 0; h < MAXH; h++) {
    float a = (float)h * 1.5708f;
    cw_node3d_set_position(G.cam[h], v3(cw_sin(a) * 20, 12, cw_cos(a) * 20));
    cw_node3d_look_at(G.cam[h], v3(0, 1, 0), v3(0, 1, 0));
    cw_node_set(G.vm[h], "visible", cw_val_bool(false));
  }
}

/* the shell's menus: the same click as the setup panel's */
static void shell_sound(cw_shell_sound s, void *user) {
  (void)user;
  fx_play_ui(s == CW_SHELL_SND_MOVE ? SND_TICK : SND_CLICK, s == CW_SHELL_SND_MOVE ? 0.5f : 0.8f);
}

/* a pad took a seat on the setup panel */
static void players_changed(int players, void *user) {
  (void)user;
  G.players = players;
  set_views();
}

static void start(int players, void *user) {
  (void)user;
  G.players = players;
  G.state = ST_SETUP;
  G.setup_row = SETUP_ROWS - 1; /* the last row (the gates' input scripts count rows from here) */
  for (int i = 0; i < MAXH; i++) cw_node_set(G.cam[i], "current", cw_val_bool(i == 0));
  build_world(G.arena_id);
  set_views();
  overview_cams();
#ifdef ARENA_AUTOPILOT
  begin_match();
  set_views();
#endif
}

/* match [arena] [mode] [bots] [players]: argv[0] is the command's own name. A different player count restarts the shell
 * with that many seats (the views and HUD follow the shell's count). */
#ifdef ARENA_AUTOPILOT
static void cmd_keyart(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: keyart <i,fwd,right,up,lookh> [hud 0|1] | keyart off"); return; }
  if (!strcmp(argv[1], "off")) keyart_chase[0] = 0, keyart_nohud = 0;
  else snprintf(keyart_chase, sizeof keyart_chase, "%s", argv[1]), keyart_nohud = argc > 2 && atoi(argv[2]) == 0;
  cw_reply_printf(r, "{\"chase\":\"%s\",\"hud\":%d}", keyart_chase, !keyart_nohud);
}
#endif
static void cmd_match(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) G.arena_id = CW_CLAMP(atoi(argv[1]), 0, ARENAS - 1);
  if (argc > 2) G.mode = CW_CLAMP(atoi(argv[2]), 0, MODE_COUNT - 1);
  if (argc > 3) G.bots_setting = CW_CLAMP(atoi(argv[3]), -1, MAXF - 1);
  int players = argc > 4 ? CW_CLAMP(atoi(argv[4]), 1, MAXH) : CW_MAX(G.players, 1);
  if (players != cw_shell_players()) {
    cw_shell_start(players); /* start(): the autopilot begins the match there */
    if (AUTO) { cw_reply_raw(r, "{\"ok\": true}"); return; }
  }
  G.players = players;
  begin_match();
  set_views();
  cw_reply_raw(r, "{\"ok\": true}");
}


/* Auto stepped the look down in play: a line says so for five seconds, so a busy machine's player knows why it changed
 * and where to choose (drawn only) */
static void quality_changed(cw_quality q, void *u) {
  (void)u;
  {
    static int last = -1;
    int score = (int)q * 100 + cw_gfx_effective(CW_GFX_EFFECTS) * 10 + cw_gfx_effective(CW_GFX_SHADOWS) + cw_gfx_effective(CW_GFX_SSAO);
    if (cw_quality_is_auto() && last >= 0 && score < last && G.state == ST_PLAY) {
      snprintf(auto_note, sizeof auto_note, "Auto set graphics to %s to keep play smooth: pause, then Settings, Graphics to choose", cw_quality_name(q));
      auto_note_until = G.steps + 60 * 5;
    }
    last = score;
  }
  /* anisotropic filtering by effects detail: 8 (as authored) from Medium up, 4 on Low, 2 on Very Low */
  int fx = cw_gfx_effective(CW_GFX_EFFECTS);
  tex_anisotropy(fx >= CW_QUALITY_MEDIUM ? 8 : fx == CW_QUALITY_LOW ? 4 : 2);
  /* Low and Very Low draw the arena after a depth prepass: its halls, roofs and girders stack several surfaces deep on
   * every pixel, and the prepass shades each pixel once (the same picture; from Medium up the scene as authored) */
  arena_gi_quality();
  bool prepass = fx <= CW_QUALITY_LOW;
#ifdef ARENA_AUTOPILOT
  if (getenv("ARENA_T_prepass")) prepass = atoi(getenv("ARENA_T_prepass")) != 0; /* measuring the prepass */
#endif
  if (cw_node_valid(G.post)) cw_node_set(G.post, "depth_prepass", cw_val_bool(prepass));
}

/* debug channel: "bind <action> <bindings>" replaces an action's bindings (the text form, "key:x, pad:north"): for checking
 * that a rebound action's prompt shows its new button */
/* the gates' switch: sw idle_lives 1 (the control of tests/arenafps-idle-seat.mjs) */
/* sw killcam 0: the old dead view, the control of tests/arenafps-killcam.mjs */
static void cmd_sw(int argc, const char **argv, cw_reply *r) {
  if (argc >= 4 && !strcmp(argv[1], "tune")) { /* sw tune <name> <value>: as ARENA_T_<name> (the next match's build reads the level's) */
    arena_tune_set(argv[2], (float)atof(argv[3]));
    cw_reply_printf(r, "{\"tune\":\"%s\",\"value\":%g}", argv[2], (double)arena_tune(argv[2], 0));
    return;
  }
  if (argc >= 3 && !strcmp(argv[1], "hurt")) {
    G.sw_hurt_hz = atoi(argv[2]);
    cw_reply_printf(r, "{\"hurt_hz\":%d}", G.sw_hurt_hz);
    return;
  }
  if (argc >= 3 && !strcmp(argv[1], "cues")) {
    G.sw_no_cues = atoi(argv[2]) == 0;
    cw_reply_printf(r, "{\"cues\":%d}", !G.sw_no_cues);
    return;
  }
  if (argc >= 3 && !strcmp(argv[1], "count_rule")) { /* sw count_rule reference|honest: Battle's count (the gates' control) */
    BT.sw_reference = !strcmp(argv[2], "reference");
    cw_reply_printf(r, "{\"count_rule\":\"%s\"}", BT.sw_reference ? "reference" : "honest");
    return;
  }
  if (argc >= 3 && !strcmp(argv[1], "takeover")) { /* sw takeover 0: a downed person at 0 watches instead */
    BT.sw_no_takeover = atoi(argv[2]) == 0;
    cw_reply_printf(r, "{\"takeover\":%d}", !BT.sw_no_takeover);
    return;
  }
  if (argc >= 3 && !strcmp(argv[1], "killcam")) {
    G.sw_no_killcam = atoi(argv[2]) == 0;
    cw_reply_printf(r, "{\"killcam\":%d}", !G.sw_no_killcam);
    return;
  }
  if (argc >= 3 && !strcmp(argv[1], "view_hook")) {
    G.sw_view_hook = atoi(argv[2]) != 0;
    views_mode();
    cw_reply_printf(r, "{\"view_hook\":%d}", G.sw_view_hook);
    return;
  }
  if (argc < 3 || strcmp(argv[1], "idle_lives")) { cw_reply_error(r, "usage: sw idle_lives <0|1> | sw killcam <0|1> | sw cues <0|1> | sw view_hook <0|1> | sw tune <name> <value> | sw hurt <hz>"); return; }
  G.sw_idle_lives = atoi(argv[2]) != 0;
  cw_reply_printf(r, "{\"idle_lives\":%d}", G.sw_idle_lives);
}
static void cmd_bind(int argc, const char **argv, cw_reply *r) {
  if (argc < 3) { cw_reply_error(r, "bind <action> <bindings>"); return; }
  cw_action a = cw_action_find(argv[1]);
  if (a < 0) { cw_reply_error(r, "no such action"); return; }
  char list[256] = "";
  for (int i = 2; i < argc; i++) { if (i > 2) strncat(list, " ", sizeof list - strlen(list) - 1); strncat(list, argv[i], sizeof list - strlen(list) - 1); }
  cw_action_clear_bindings(a);
  if (!cw_action_bind_text(a, list)) { cw_reply_error(r, "bad bindings"); return; }
  cw_reply_printf(r, "ok");
}

static void init(void *user) {
  (void)user;
  cw_project_load_input();
  G.tree = cw_scene_app_tree();
  G.root = cw_tree_root(G.tree);
  G.mv_l = cw_action_find("move_left"), G.mv_r = cw_action_find("move_right");
  G.mv_f = cw_action_find("move_forward"), G.mv_b = cw_action_find("move_back");
  G.lk_l = cw_action_find("look_left"), G.lk_r = cw_action_find("look_right");
  G.lk_u = cw_action_find("look_up"), G.lk_d = cw_action_find("look_down");
  G.fire = cw_action_find("fire"), G.jump = cw_action_find("jump");
  G.ui_start = cw_action_find("ui_start");
  G.w_next = cw_action_find("weapon_next"), G.w_prev = cw_action_find("weapon_prev");
  G.sprint = cw_action_find("sprint");
  G.grenade = cw_action_find("grenade"), G.melee = cw_action_find("melee");
  G.font = cw_ui_font_sans();
  G.arena_id = 0;
  G.mode = MODE_DM;
  G.bots_setting = -1; /* AUTO */
  G.bot_level = 1;
  BT.pool_setting = 2; /* Battle: 200 reinforcements a side */
  cw_shell_init(&(cw_shell_desc){ .title = "ARENA", .subtitle = "split screen shooter", .max_players = MAXH, .start = start, .sound = shell_sound, .players_changed = players_changed,
                                  .game_save_size = sizeof(arena_save), .game_save_version = 1 });
  setup_load();
  G.ui_back = cw_action_find("ui_back"), G.ui_pause = cw_action_find("pause");
  /* the persistent scene: environment, sun, post, lights, four cameras */
  G.env = cw_node_new("WorldEnvironment", "Env");
  cw_node_add_child(G.root, G.env);
  cw_node_set(G.env, "use_background", cw_val_bool(false));
  cw_node_set(G.env, "hemi_energy", cw_val_float(0));
  G.sun = cw_node_new("DirectionalLight3D", "Sun");
  cw_node_add_child(G.root, G.sun);
  cw_node_set(G.sun, "shadow", cw_val_bool(true));
  cw_node_set(G.sun, "shadow_size", cw_val_int(2048));
  cw_node_set(G.sun, "shadow_extent", cw_val_float(46));
  G.sky = cw_node_new("ProceduralSky", "Sky");
  cw_node_add_child(G.root, G.sky);
  cw_node_set(G.sky, "sun", cw_val_string("../Sun"));
  G.fog = cw_node_new("HeightFog", "Fog");
  cw_node_add_child(G.root, G.fog);
  cw_node_set(G.fog, "height_falloff", cw_val_float(0.05));
  cw_node_set(G.fog, "use_sky_color", cw_val_bool(false));
  G.post = cw_node_new("PostProcess", "Post");
  cw_node_add_child(G.root, G.post);
  cw_node_set(G.post, "hdr", cw_val_bool(true));
  cw_node_set(G.post, "msaa", cw_val_int(4));
  cw_node_set(G.post, "bloom", cw_val_bool(true));
  cw_node_set(G.post, "bloom_intensity", cw_val_float(0.35));
  cw_node_set(G.post, "bloom_threshold", cw_val_float(1.0));
  cw_node_set(G.post, "ssao", cw_val_bool(true));
  cw_node_set(G.post, "vignette", cw_val_bool(true));
  cw_node_set(G.post, "vignette_intensity", cw_val_float(0.35));
  G.lighting = cw_node_new("LightingSettings", "Lighting");
  cw_node_add_child(G.root, G.lighting);
  cw_node_set(G.lighting, "mode", cw_val_enum("CLUSTERED"));
  cw_node_set(G.lighting, "shadow_atlas", cw_val_bool(true));
  for (int h = 0; h < MAXH; h++) {
    char name[16];
    snprintf(name, sizeof name, "Cam%d", h + 1);
    G.cam[h] = cw_node_new("Camera3D", name);
    cw_node_set(G.cam[h], "fov", cw_val_float(78));
    cw_node_set(G.cam[h], "near", cw_val_float(0.08));
    cw_node_set(G.cam[h], "far", cw_val_float(220));
    /* viewmodel_layers: views_mode (camera h draws its own player's gun layer; ARENA_T_vm=0, autopilot: none) */
    cw_node_set(G.cam[h], "viewmodel_fov", cw_val_float(52));
    cw_node_add_child(G.root, G.cam[h]);
  }
  cw_node_set(G.cam[0], "current", cw_val_bool(true));
  make_viewmodels();
  build_world(G.arena_id); /* the join screen shows the arena, not an empty room */
  overview_cams();
  views_mode(); /* per-player gun layers and body masks (or, with sw view_hook 1, the old view hook) */
  cw_quality_on_change(quality_changed, NULL);
  quality_changed(cw_quality_get(), NULL); /* the preset the engine started at */
  cw_quality_auto(true, 0);
  cw_debug_i32("game.state", (int32_t *)&G.state);
  cw_debug_i32("fl.tint_flashes", &G.fl_tint_flashes); /* the most damage-tint flashes in one second, any seat */
  cw_debug_f32("fl.tint_peak", &G.fl_tint_peak);       /* the tint's highest drawn opacity */
  cw_debug_i32("fl.near_views", &G.fl_near_views);     /* view frames that drew a body within 0.6 m of the camera */
  cw_debug_command("bind", "bind <action> <bindings>: replace an action's bindings", cmd_bind);
  cw_debug_i32("game.mode", &G.mode);
  cw_debug_i32("soldier.bodies", &SOLDIER_ST.bodies);     /* bodies made */
  cw_debug_i32("soldier.animated", &SOLDIER_ST.animated); /* trees stepped this tick (one no view sees is not) */
  cw_debug_i32("soldier.ragdolls", &SOLDIER_ST.ragdolls); /* falling bodies */
  cw_debug_i32("soldier.lod0", &SOLDIER_ST.lod[0]);       /* bodies by detail: full, every 2nd tick, masked, far */
  cw_debug_i32("soldier.lod1", &SOLDIER_ST.lod[1]);
  cw_debug_i32("soldier.lod2", &SOLDIER_ST.lod[2]);
  cw_debug_i32("soldier.lod3", &SOLDIER_ST.lod[3]);
  cw_debug_i32("game.aim_assist", &G.aim_assist); /* the setup's aim assist and rumble (the save gate reads them after a restart) */
  cw_debug_i32("game.rumble_off", &G.rumble_off);
  cw_debug_i32("game.rules", &G.rules);
  cw_debug_i32("over.awards", &G.awards); /* awards drawn on the results */
  cw_debug_i32("game.arena", &G.arena_id);
  cw_debug_i32("game.players", &G.players);
  cw_debug_i32("game.ticks", &G.ticks);
  cw_debug_i32("game.steps", &G.steps);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_i32("match.wave", &G.wave);
  cw_debug_i32("match.lives", &G.lives);
  for (int h = 0; h < MAXH; h++) G.last_in[h] = -(1 << 29); /* nobody has touched a pad yet */
  cw_debug_i32("coop.away_skips", &G.away_skips); cw_debug_i32("coop.present", &G.present); /* co-op respawns held back for a seat nobody plays; the people the last wave was sized for */
  cw_debug_command("sw", "sw idle_lives <0|1>: test switch (co-op spends shared lives on a seat nobody plays and sizes the waves for it, as before)", cmd_sw);
  cw_debug_i32("match.winner", &G.winner);
  cw_debug_i32("cue.over", &G.cue_over); /* the results' continue prompt is on screen */
  cw_debug_i32("match.nf", &G.nf);
  cw_debug_i32("nav.requests", &G.path_requests);
  cw_debug_i32("nav.done", &G.path_done);
  cw_debug_i32("nav.links", &A.nav_links);
  cw_debug_i32("items.gap_mm", &A.item_gap_mm);
  cw_debug_i32("props.count", &A.prop_count);
  cw_debug_i32("props.floating", &A.prop_floating);
  cw_debug_i32("level.coplanar_left", &A.coplanar_left);   /* faces still sharing a plane with another after the build (0: no depth fight) */
  cw_debug_i32("level.coplanar_moved", &A.coplanar_moved); /* vertices the build moved apart */
  cw_debug_i32("nav.links_connected", &A.nav_links_connected);
  cw_debug_i32("nav.link_paths", &G.path_links);
  cw_debug_i32("nav.link_jumps", &G.link_jumps);
  cw_debug_field("nav.link_cross", G.link_cross, CW_DBG_I32, 4);
  cw_debug_field("nav.link_start", G.link_start, CW_DBG_I32, 4);
  cw_debug_i32("views", &G.views);
  cw_debug_i32("soak.matches", &soak_matches);
  cw_debug_field("cam.gate", cam_gate, CW_DBG_I32, 4);
  cw_debug_field("cam.jit", cam_jit, CW_DBG_F32, 3);
  cw_debug_field("cam.fire", cam_fire, CW_DBG_I32, 2);
  cw_debug_i32("cam.events", &cam_events);
  cw_debug_field("cam.offset", cam_offset, CW_DBG_I32, 3);
  cw_debug_field("killcam", killcam, CW_DBG_I32, 2);
  cw_debug_field("gren.stats", gren_stats, CW_DBG_I32, 3);
  cw_debug_field("bash.stats", bash_stats, CW_DBG_I32, 2);
  cw_debug_field("power.stats", pow_stats, CW_DBG_I32, 5);
  cw_debug_field("rumble.sent", rumble_sent, CW_DBG_I32, 5);
  cw_debug_field("cam.jerk_at", cam_jerk_at, CW_DBG_I32, 4);
  cw_debug_field("soak.heap_kb", soak_heap_kb, CW_DBG_I32, SOAK_N);
  cw_debug_i32("stats.enc", &G.st.enc);
  cw_debug_i32("stats.enc_gap_max", &G.st.enc_gap_max);
  cw_debug_i32("stats.kills", &G.st.kills);
  cw_debug_i32("stats.spawn_kills", &G.st.spawn_kills);
  cw_debug_i32("stats.ttk_sum", &G.st.ttk_sum);
  cw_debug_i32("stats.ttk_n", &G.st.ttk_n);
  cw_debug_i32("stats.gap_max", &G.st.gap_max);
  cw_debug_i32("stats.fighter_ticks", &G.st.fighter_ticks);
  cw_debug_i32("stats.quick_deaths", &G.st.quick_deaths);
  cw_debug_i32("stats.eng", &G.st.eng);
  cw_debug_i32("stats.eng_gap_max", &G.st.eng_gap_max);
  cw_debug_i32("stats.contact_ticks", &G.st.contact_ticks);
  cw_debug_i32("stats.spawns", &G.st.spawns);
  cw_debug_i32("stats.unsafe_spawns", &G.st.unsafe_spawns);
  cw_debug_field("stats.by_weapon", G.st.by_weapon, CW_DBG_I32, WK_KINDS);
  cw_debug_field("fighters.score", G.d_score, CW_DBG_I32, MAXF);
  cw_debug_field("fighters.hp", G.d_hp, CW_DBG_I32, MAXF);
  cw_debug_field("fighters.deaths", G.d_deaths, CW_DBG_I32, MAXF);
  cw_debug_field("fighters.weapon", G.d_weapon, CW_DBG_I32, MAXF);
  cw_debug_field("fighters.alive", G.d_alive, CW_DBG_I32, MAXF);
  cw_debug_field("fighters.pos", G.d_pos, CW_DBG_F32, MAXF * 2);
  cw_debug_field("fighters.yaw", G.d_yaw, CW_DBG_F32, MAXF);
  cw_debug_field("p1.stats", G.p1, CW_DBG_I32, 6);
  cw_debug_field("p1.input", G.p1_in, CW_DBG_F32, 5);
  cw_debug_field("battle.pool", BT.d_pool, CW_DBG_I32, 2);      /* Battle: reinforcements left a side */
  cw_debug_field("battle.alive", BT.d_alive, CW_DBG_I32, 2);    /* soldiers alive a side */
  cw_debug_field("battle.posts", BT.d_owner, CW_DBG_I32, MAX_POSTS); /* each post's side (-1 neutral, -9 none) */
  cw_debug_field("battle.ls_ticks", BT.ls_ticks, CW_DBG_I32, 2); /* ticks in the last stand (0 reinforcements, soldiers alive) */
  cw_debug_field("battle.pool0_tick", BT.pool0_tick, CW_DBG_I32, 2);
  cw_debug_field("battle.zero_tick", BT.zero_tick, CW_DBG_I32, 2); /* the tick a side had nobody and nothing left */
  cw_debug_i32("battle.end_tick", &BT.end_tick);
  cw_debug_i32("battle.reveals", &BT.reveals);
  cw_debug_field("battle.bleeds", BT.bleeds, CW_DBG_I32, 2);
  cw_debug_field("battle.spent", BT.spent, CW_DBG_I32, 2);
  cw_debug_field("seat.fighter", SEAT, CW_DBG_I32, MAXH);
  cw_debug_field("tk.stats", tk_stats, CW_DBG_I32, 6);
  cw_debug_field("tk.start", tk_start, CW_DBG_F32, 3);
  cw_debug_field("tk.after", tk_after, CW_DBG_F32, 4);
  cw_debug_command("battle", "battle pool <side> <n> | alive <side> <n> | down <i> | downseat <seat>: Battle test commands", cmd_battle);
  cw_debug_command("match", "match [arena] [mode] [bots] [players]: start a match now", cmd_match);
  cw_debug_command("ptest", "ptest [on|off]: the playtest perception field (ptest) for a scripted human", cmd_ptest);
  cw_debug_field("ptest", ptest, CW_DBG_F32, PT_N);
#ifdef ARENA_AUTOPILOT
  cw_debug_command("keyart", "keyart <i,fwd,right,up,lookh> [hud 0|1] | keyart off: key art (view 1 from a spot in fighter i's frame, the HUD off)", cmd_keyart);
  { /* each view's camera as drawn (yaw, pitch, eye x and z), one scalar a field so a ranged read gets every frame */
    static const char *const NY[] = { "view.yaw0", "view.yaw1", "view.yaw2", "view.yaw3" }, *const NP[] = { "view.pitch0", "view.pitch1", "view.pitch2", "view.pitch3" };
    static const char *const NX[] = { "view.x0", "view.x1", "view.x2", "view.x3" }, *const NZ[] = { "view.z0", "view.z1", "view.z2", "view.z3" };
    static const char *const NB[] = { "seat.branch0", "seat.branch1", "seat.branch2", "seat.branch3" }, *const NT[] = { "seat.target0", "seat.target1", "seat.target2", "seat.target3" };
    for (int h = 0; h < MAXH; h++) cw_debug_i32(NB[h], &DBG_SEAT[h][0]), cw_debug_i32(NT[h], &DBG_SEAT[h][1]);
    for (int h = 0; h < MAXH; h++)
      cw_debug_f32(NY[h], &KC.yaw_now[h]), cw_debug_f32(NP[h], &KC.pitch_now[h]), cw_debug_f32(NX[h], &KC.eye[h].x), cw_debug_f32(NZ[h], &KC.eye[h].z);
  }
  /* the autopilot: ARENA_ID, ARENA_MODE, ARENA_BOTS, ARENA_PLAYERS choose the match (tests, benches) */
  const char *e;
  if ((e = getenv("ARENA_ID"))) G.arena_id = CW_CLAMP(atoi(e), 0, ARENAS - 1);
  if ((e = getenv("ARENA_MODE"))) G.mode = CW_CLAMP(atoi(e), 0, MODE_COUNT - 1);
  if ((e = getenv("ARENA_RULES"))) G.rules = CW_CLAMP(atoi(e), 0, RULE_COUNT - 1);
  if ((e = getenv("ARENA_BOTS"))) G.bots_setting = CW_CLAMP(atoi(e), -1, MAXF - 1); /* (the live count sweep: up to MAXF fighters) */
  if ((e = getenv("ARENA_LEVEL"))) G.bot_level = CW_CLAMP(atoi(e), 0, BOT_LEVELS);
  if ((e = getenv("ARENA_NO_KILLCAM"))) G.sw_no_killcam = atoi(e) != 0; /* the killer cam gate's control */
  if ((e = getenv("ARENA_HUMAN"))) { /* seat 1 played by the scripted human (casual or skilled) instead of the stand-in */
    const char *av[2] = { "ptest", e };
    cmd_ptest(2, av, NULL);
    HM.drive = HM.kind != 0 && !getenv("ARENA_HUMAN_WATCH"); /* ARENA_HUMAN_WATCH: the model and the route run, the stand-in plays (the read-only check) */
    if ((e = getenv("ARENA_HUMAN_SEED"))) PT.rng = (uint32_t)strtoul(e, NULL, 10) * 2654435761u + 1u;
  }
  if ((e = getenv("ARENA_ASSIST"))) G.aim_assist = atoi(e) != 0;
  if ((e = getenv("ARENA_BOT_FOV"))) bot_fov_override = (float)atof(e);
#ifdef ARENA_AUTOPILOT
  if ((e = getenv("ARENA_L"))) { /* tuning sweeps: skill_mul,react,react_span,turn,turn_skill,dmg_to_people,err for the chosen level */
    bot_level_def *L = &LEVELS[G.bot_level];
    sscanf(e, "%f,%f,%f,%f,%f,%f,%f", &L->skill_mul, &L->react, &L->react_span, &L->turn, &L->turn_skill, &L->dmg_to_people, &L->err);
  }
#endif
  if ((e = getenv("ARENA_BULLET_ASSIST"))) assist_cone = (float)atof(e);
  cw_shell_start((e = getenv("ARENA_PLAYERS")) ? CW_CLAMP(atoi(e), 1, MAXH) : 4);
#endif
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .width = 960, .height = 540, .debug = true, .init = init, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
#ifdef ARENA_AUTOPILOT
  { /* ARENA_RES=WxH: the output size (benches) */
    const char *r = getenv("ARENA_RES");
    int w, h;
    if (r && sscanf(r, "%dx%d", &w, &h) == 2) app.width = w, app.height = h;
    /* ARENA_SUBSTEPS=n: ticks one frame may run (pacing and soak runs at a low CW_FRAME_HZ; the default 5 drops the rest) */
    if ((r = getenv("ARENA_SUBSTEPS"))) app.max_substeps = CW_MAX(1, atoi(r));
  }
#endif
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app(NULL, &app);
}
