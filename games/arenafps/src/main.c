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
#include "cartwheel/aim.h"
#include "cartwheel/anim.h"
#include "cartwheel/ui.h"
#include "cartwheel/environment.h"
#include "fx.h"
#include "game.h"

#ifdef ARENA_AUTOPILOT
#define AUTO 1
#else
#define AUTO 0
#endif

#define VM_SCALE 0.62f
#define MAXP 24 /* rockets in flight */
#define MAX_FEED 5

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
  cw_node node, light, trail;
} proj;

typedef struct feed_line {
  char text[64];
  int ticks;
} feed_line;

static const char *MODE_NAME[MODE_COUNT] = { "DEATHMATCH", "TEAM DEATHMATCH", "CO-OP WAVES" };
static const char *TEAM_NAME[2] = { "RED", "BLUE" };
static const float TEAM_RGB[2][3] = { { 1.0f, 0.25f, 0.2f }, { 0.25f, 0.5f, 1.0f } };

static struct game {
  cw_tree *tree;
  cw_node root, world, env, sun, sky, fog, post, lighting;
  cw_node cam[MAXH], vm[MAXH], vm_flash[MAXH], vm_wp[MAXH][NWEAP];
  game_state state;
  int mode, arena_id, bots_setting, bot_level, aim_assist, players, nf;
  int setup_row;
  fighter f[MAXF];
  proj pr[MAXP];
  cw_rng rng;
  int ticks, match_ticks, over_ticks, time_limit, frag_limit;
  int team_score[2];
  int wave, wave_state, wave_timer, lives;
  uint32_t hash;
  feed_line feed[MAX_FEED];
  int winner; /* fighter index or team, -1 none */
  /* per human view feedback */
  float hit_marker[MAXH], dmg_flash[MAXH], dmg_dir[MAXH], shake[MAXH], kill_flash[MAXH];
  char kill_name[MAXH][16];
  cw_aim_state aim[MAXH];
  /* actions */
  cw_action mv_l, mv_r, mv_f, mv_b, lk_l, lk_r, lk_u, lk_d, fire, jump, w_next, w_prev;
  /* nav cost */
  int path_requests, path_done;
  float nav_tick_ms;
  int views;
  int32_t d_score[MAXF], d_hp[MAXF], d_deaths[MAXF], d_weapon[MAXF], d_alive[MAXF];
  float d_pos[MAXF * 2];
  int32_t kills_total, rockets_fired;
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
  struct { int32_t enc, enc_gap_max, kills, spawn_kills, quick_deaths, ttk_sum, ttk_n, gap_max, last_kill, by_weapon[NWEAP], fighter_ticks;
           int32_t eng, eng_gap_max, contact_ticks, spawns, unsafe_spawns; } st;
  cw_font font;
} G;

static fighter *F(int i) { return &G.f[i]; }
static bool is_bot(const fighter *f) { return AUTO || f->human < 0; }

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
}

static void spawn_fighter(fighter *f) {
  int s = pick_spawn(f);
  t3_vec3 p = A.spawns[s];
  cw_mover3d_teleport(&f->mv, p);
  f->mv.velocity = v3(0, 0, 0);
  f->yaw = cw_rng_float(&G.rng) * 6.2831853f;
  /* face the middle */
  t3_vec3 c = v3(-p.x, 0, -p.z);
  if (v3len(c) > 1) f->yaw = cw_atan2(-c.x, -c.z);
  f->pitch = 0;
  f->alive = true;
  f->dead_ticks = 0;
  f->spawn_guard = 120;
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
      if (dist < 15 || (dist < 45 && !arena_ray(eye_pos(o), eye, NULL, NULL))) f->spawn_seen_by |= 1u << i;
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
  v->streak = 0;
  fx_spark(chest_pos(v), v3(0, 1, 0), 1);
  fx_play(SND_DIE, v->mv.position, 0.8f, 1);
  if (k && k != v) {
    G.st.kills++;
    if (weapon >= 0 && weapon < NWEAP) G.st.by_weapon[weapon]++;
    if (v->since_spawn < 360) G.st.quick_deaths++;
    if (v->since_spawn < 300 && (v->spawn_seen_by >> (k - G.f) & 1u)) G.st.spawn_kills++;
    if (v->first_dmg_tick >= 0) G.st.ttk_sum += G.ticks - v->first_dmg_tick, G.st.ttk_n++;
    if (G.ticks - G.st.last_kill > G.st.gap_max) G.st.gap_max = G.ticks - G.st.last_kill;
    G.st.last_kill = G.ticks;
    k->score++;
    k->streak++;
    if (k->human >= 0) {
      G.kill_flash[k->human] = 1;
      snprintf(G.kill_name[k->human], sizeof G.kill_name[0], "%s", v->name);
      fx_play_ui(SND_HIT, 0.8f);
    }
    if (G.mode == MODE_TDM) G.team_score[k->team]++;
    feed("%s fragged %s", k->name, v->name);
    (void)weapon;
  } else {
    v->score--;
    if (G.mode == MODE_TDM) G.team_score[v->team]--;
    feed("%s fell", v->name, "");
  }
}

static void damage(fighter *v, fighter *k, int dmg, t3_vec3 dir, float knock, int weapon, bool head) {
  if (!v->alive || v->spawn_guard > 0) return;
  if (v->first_dmg_tick < 0) v->first_dmg_tick = G.ticks;
  if (k && !enemies(k, v) && k != v) return; /* no friendly fire */
  if (head) dmg = dmg * 3 / 2;
  int absorbed = CW_MIN(v->armor, dmg / 2);
  v->armor -= absorbed;
  v->hp -= dmg - absorbed;
  if (knock > 0) {
    float kk = knock * (float)dmg / 40.0f;
    if (kk > 14) kk = 14;
    v->mv.velocity = v3add(v->mv.velocity, v3(dir.x * kk, dir.y * kk * 0.6f + (v->mv.on_floor ? kk * 0.3f : 0), dir.z * kk));
  }
  if (v->human >= 0) {
    G.dmg_flash[v->human] = 1;
    if (k) {
      t3_vec3 d = v3sub(k->mv.position, v->mv.position);
      G.dmg_dir[v->human] = cw_atan2(-d.x, -d.z) - v->yaw;
    }
    G.shake[v->human] = CW_MAX(G.shake[v->human], 0.4f + (float)dmg * 0.01f);
    fx_play_ui(SND_HURT, 0.5f);
  }
  if (k && k->human >= 0 && k != v) {
    G.hit_marker[k->human] = 1;
    fx_play_ui(SND_TICK, 0.5f);
  }
  if (v->hp <= 0) kill_fighter(v, k, weapon);
}

/* ── shooting ──────────────────────────────────────────────────────── */

static t3_vec3 spread_dir(const fighter *f, float spread) {
  t3_vec3 d = look_dir(f->yaw, f->pitch);
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
  if (w->kind == 2) fx_tracer(v3add(o, v3(0, -0.12f, 0)), end, 1);
  else if (w->pellets == 1 || cw_rng_int(&G.rng, 0, 3) == 0) fx_tracer(v3add(o, v3(0, -0.12f, 0)), end, 0);
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
    cw_node_set(p->light, "energy", cw_val_float(14));
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

static void explode(proj *p, t3_vec3 at, t3_vec3 n) {
  p->on = false;
  cw_node_set(p->node, "visible", cw_val_bool(false));
  cw_node_set(p->trail, "emitting", cw_val_bool(false));
  cw_node_set(p->light, "energy", cw_val_float(0));
  cw_node3d_set_position(p->node, v3(0, -500, 0));
  cw_node3d_set_position(p->light, v3(0, -500, 0));
  fx_explosion(at, n);
  fighter *owner = F(p->owner);
  const float RADIUS = 5.0f;
  for (int i = 0; i < G.nf; i++) {
    fighter *v = F(i);
    if (!v->active || !v->alive) continue;
    t3_vec3 c = chest_pos(v), d = v3sub(c, at);
    float dist = v3len(d);
    if (dist > RADIUS) continue;
    float k = 1 - dist / RADIUS;
    if (dist > 0.8f && arena_ray(at, c, NULL, NULL)) k *= 0.4f; /* behind cover */
    int dmg = (int)(W[2].damage * k);
    if (dmg < 4) continue;
    t3_vec3 dir = dist > 0.01f ? v3scale(d, 1 / dist) : v3(0, 1, 0);
    dir.y += 0.35f;
    dir = v3norm(dir);
    float knock = (v == owner) ? 10.0f : 7.0f;
    if (v == owner) dmg = dmg / 2; /* self damage is halved: rocket jumps are fair */
    G.shake[0] = G.shake[0];
    if (v->human >= 0) G.shake[v->human] = CW_MAX(G.shake[v->human], 0.6f * k + 0.1f);
    int was = v->spawn_guard;
    if (v == owner) v->spawn_guard = 0;
    damage(v, owner, dmg, dir, knock * 40.0f / (float)CW_MAX(dmg, 20), 2, false);
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
  f->ammo[f->weapon]--;
  f->cooldown = w->cooldown;
  f->spawn_guard = 0; /* firing ends spawn protection */
  f->flash = 3;
  f->kick += w->kind == 0 && w->pellets == 1 ? 0.012f : w->kind == 2 ? 0.06f : 0.05f;
  t3_vec3 e = eye_pos(f);
  t3_vec3 d0 = look_dir(f->yaw, f->pitch);
  t3_vec3 right = v3norm(v3cross(d0, v3(0, 1, 0)));
  t3_vec3 muzzle = v3add(e, v3add(v3scale(d0, 0.5f), v3scale(right, 0.12f)));
  if (w->kind == 1) {
    fire_rocket(f, idx);
    fx_play(SND_ROCKET, e, 0.9f, 1);
  } else {
    for (int k = 0; k < w->pellets; k++) {
      t3_vec3 d = spread_dir(f, w->spread);
      trace(f, idx, k == 0 ? muzzle : muzzle, d, w, w->kind == 2);
    }
    fx_play(f->weapon == 0 ? SND_RIFLE : f->weapon == 1 ? SND_SCATTER : SND_RAIL, e, 0.8f, 0.97f + cw_rng_float(&G.rng) * 0.06f);
    if (f->weapon <= 1) fx_casing(muzzle, right, v3(0, 1, 0));
  }
  if (cw_node_valid(f->light)) {
    cw_node_set(f->light, "energy", cw_val_float(f->weapon == 1 ? 60 : 28));
    cw_node3d_set_position(f->light, v3add(muzzle, v3scale(d0, 0.9f))); /* out past the barrel: at the barrel it bleaches the first person gun */
  }
}

/* ── movement ──────────────────────────────────────────────────────── */

static void move_fighter(fighter *f, float fwd, float strafe, bool jump) {
  const float MAXSPEED = 7.2f, GROUND_ACCEL = 14.0f, AIR_ACCEL = 2.4f, FRICTION = 7.0f, GRAV = 24.0f, JUMP = 8.0f;
  float sy, cy;
  cw_sincos(f->yaw, &sy, &cy);
  t3_vec3 fw = v3(-sy, 0, -cy), rt = v3(cy, 0, -sy);
  t3_vec3 wish = v3add(v3scale(fw, fwd), v3scale(rt, strafe));
  float wl = v3len(wish);
  if (wl > 1) wish = v3scale(wish, 1 / wl), wl = 1;
  t3_vec3 wd = wl > 0.001f ? v3scale(wish, 1 / wl) : v3(0, 0, 0);
  float wishspeed = wl * MAXSPEED;
  t3_vec3 *vel = &f->mv.velocity;
  bool ground = f->mv.on_floor;
  if (ground) {
    float sp = sqrtf(vel->x * vel->x + vel->z * vel->z);
    if (sp > 0.01f) {
      float drop = (sp < 1.5f ? 1.5f : sp) * FRICTION * TICK_DT;
      float ns = CW_MAX(sp - drop, 0) / sp;
      vel->x *= ns, vel->z *= ns;
    }
    vel->y = 0;
    if (jump) vel->y = JUMP;
  } else {
    vel->y -= GRAV * TICK_DT;
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
  cw_mover3d_move(&f->mv, TICK_DT);
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

static void bot_follow(fighter *f, float *fwd, float *strafe, float speed) {
  t3_vec3 p = f->mv.position;
  if (f->npath > 0 && f->pidx < f->npath) {
    t3_vec3 w = f->path[f->pidx];
    float dx = w.x - p.x, dz = w.z - p.z, d = sqrtf(dx * dx + dz * dz);
    if (d < 0.9f && f->pidx + 1 < f->npath) { f->pidx++; return bot_follow(f, fwd, strafe, speed); }
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
    if (dist > 45 || dist > bd) continue;
    if (arena_ray(e, chest_pos(o), NULL, NULL)) continue;
    bd = dist, best = i;
  }
  if (best >= 0) {
    if (f->target != best) f->react = 8 + (int)((1 - f->skill) * 24);
    f->target = best;
    f->saw_t = f->alive_t;
    f->last_pos = F(best)->mv.position;
    f->wander_wait = 0;
  } else if (f->target >= 0 && (!F(f->target)->alive || F(f->target)->spawn_guard > 0)) {
    f->target = -1;
  }
  /* goal: fight, or fetch something */
  bool see = best >= 0;
  if (!see) {
    float bs = 0;
    int bi = -1;
    for (int i = 0; i < A.nitems; i++) {
      item *it = &A.items[i];
      if (it->respawn > 0) continue;
      float need = 1;
      switch (it->kind) {
        case ITEM_HEALTH: need = f->hp < 80 ? 4.0f : 0.3f; break;
        case ITEM_ARMOR: need = f->armor < 60 ? 3.0f : 0.3f; break;
        case ITEM_AMMO: need = f->ammo[0] < 80 ? 2.0f : 0.5f; break;
        case ITEM_SCATTER: need = (f->have & 2) ? 0.6f : 3.0f; break;
        case ITEM_ROCKET: need = (f->have & 4) ? 0.8f : 4.0f; break;
        case ITEM_RAIL: need = (f->have & 8) ? 0.8f : 4.0f; break;
      }
      t3_vec3 d = v3sub(it->pos, f->mv.position);
      float dist = sqrtf(d.x * d.x + d.z * d.z) + fabsf(d.y) * 2;
      float s = need / (6 + dist);
      if (s > bs) bs = s, bi = i;
    }
    if (f->last_pos.x != 0 || f->last_pos.z != 0) {
      if (f->target < 0 && f->wander_wait++ < 240 && bi < 0) bi = -2;
    }
    /* hunt: co-op bots (both sides) go after the nearest enemy, and any bot that has seen no enemy for 12 s goes looking
     * (the longest stretch without an encounter stays short); only a pressing need (health, a power weapon) comes first */
    if ((G.mode == MODE_COOP || f->alive_t - f->saw_t > 12 * 60) && (bi < 0 || bs < 0.12f)) {
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
        if (!near_spawn) break;
      }
    }
    if (v3len(v3sub(g, f->goal)) > 0.5f) bot_set_goal(f, g);
  } else {
    /* in a fight: hold a preferred range along the path toward the enemy when far */
    fighter *t = F(f->target);
    float dist = v3len(v3sub(t->mv.position, f->mv.position));
    if (dist > W[f->weapon].range_pref * 1.6f) bot_set_goal(f, t->mv.position);
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
    if (k == 3) s = dist > 14 ? 3.5f : 1.5f;
    if (k == 0) s = 1.2f;
    if (s > best) best = s, want = k;
  }
  if (want != f->weapon && f->cooldown < 10) f->weapon = want;
}

static void bot_tick(fighter *f, int idx) {
  float fwd = 0, strafe = 0;
  bool jump = false;
  if (--f->think <= 0) {
    f->think = 10 + (idx & 3);
    bot_think(f, idx);
  }
  /* path requests: one outstanding per bot; the tick budget answers them */
  if (f->ticket) {
    t3_vec3 pts[24];
    int cnt = 0;
    cw_nav_path_status st = cw_nav_take_path(A.nav, f->ticket, pts, 24, &cnt);
    if (st == CW_NAV_PATH_DONE || st == CW_NAV_PATH_PARTIAL || st == CW_NAV_PATH_FAILED) {
      f->ticket = 0;
      G.path_done++;
      if (cnt > 1) {
        memcpy(f->path, pts, sizeof(t3_vec3) * (size_t)cnt);
        f->npath = cnt;
        f->pidx = 1;
      } else {
        f->npath = 0;
      }
    }
  }
  if (!f->ticket && --f->repath <= 0) {
    f->repath = 50 + (idx * 7) % 20;
    f->ticket = cw_nav_request_path(A.nav, f->mv.position, f->goal, NULL);
    G.path_requests++;
  }
  fighter *t = f->target >= 0 && F(f->target)->alive && F(f->target)->spawn_guard <= 0 ? F(f->target) : NULL;
  bool see = false;
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
  if (see) {
    /* strafe while fighting, close or open the range */
    if (--f->strafe_t <= 0) {
      f->strafe_t = 30 + cw_rng_int(&G.rng, 0, 50);
      f->strafe_dir = cw_rng_int(&G.rng, 0, 1) ? 1 : -1;
    }
    float pref = W[f->weapon].range_pref;
    strafe = (float)f->strafe_dir * (0.5f + 0.5f * f->skill);
    if (dist > pref * 1.3f) bot_follow(f, &fwd, &strafe, 1.0f);
    else if (dist < pref * 0.6f) fwd = -0.7f;
    if (((idx * 13 + G.ticks) % 97) == 0 && f->mv.on_floor) jump = true;
    /* aim: turn toward the target, with an error that settles with skill */
    t3_vec3 d = v3sub(aim_at, eye_pos(f));
    float hd = sqrtf(d.x * d.x + d.z * d.z);
    if (G.ticks % 24 == idx % 24) {
      f->aim_err_yaw = cw_rng_range(&G.rng, -1, 1) * (1 - f->skill * 0.8f) * 0.06f;
      f->aim_err_pitch = cw_rng_range(&G.rng, -1, 1) * (1 - f->skill * 0.8f) * 0.04f;
    }
    float ty = cw_atan2(-d.x, -d.z) + f->aim_err_yaw, tp = cw_atan2(d.y, hd) + f->aim_err_pitch;
    float rate = (2.5f + f->skill * 5.5f) * TICK_DT;
    float dy = cw_wrap_angle(ty - f->yaw), dp = tp - f->pitch;
    f->yaw += cw_clampf(dy, -rate, rate);
    f->pitch += cw_clampf(dp, -rate, rate);
    if (f->react > 0) f->react--;
    else if (fabsf(dy) < 0.06f + (1 - f->skill) * 0.06f && f->cooldown == 0) do_fire(f, idx);
  } else {
    bot_follow(f, &fwd, &strafe, 1.0f);
    /* look where we go */
    if (f->npath > 0 && f->pidx < f->npath) {
      t3_vec3 w = f->path[f->pidx];
      float ty = cw_atan2(-(w.x - f->mv.position.x), -(w.z - f->mv.position.z));
      float dy = cw_wrap_angle(ty - f->yaw), rate = 5.0f * TICK_DT;
      f->yaw += cw_clampf(dy, -rate, rate);
      f->pitch += cw_clampf(-f->pitch, -0.05f, 0.05f);
    }
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
  move_fighter(f, cw_clampf(fwd, -1, 1), cw_clampf(strafe, -1, 1), jump);
}

/* ── humans ────────────────────────────────────────────────────────── */

static void human_tick(fighter *f, int idx) {
  int h = f->human;
  cw_vec2 mv = cw_input_vector(h, G.mv_l, G.mv_r, G.mv_f, G.mv_b, CW_DEADZONE_SCALED_RADIAL, 0.18f);
  cw_vec2 lk = cw_input_vector(h, G.lk_l, G.lk_r, G.lk_u, G.lk_d, CW_DEADZONE_RADIAL, 0);
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
  if (cw_action_just_pressed(h, G.w_next)) {
    for (int k = 1; k <= NWEAP; k++) { int n = (f->weapon + k) % NWEAP; if ((f->have >> n & 1)) { f->weapon = n; break; } }
    fx_play_ui(SND_CLICK, 0.4f);
  }
  if (cw_action_just_pressed(h, G.w_prev)) {
    for (int k = 1; k <= NWEAP; k++) { int n = (f->weapon + NWEAP - k) % NWEAP; if ((f->have >> n & 1)) { f->weapon = n; break; } }
    fx_play_ui(SND_CLICK, 0.4f);
  }
  bool wants_fire = cw_action_pressed(h, G.fire);
  if (wants_fire) do_fire(f, idx);
  move_fighter(f, -mv.y, mv.x, cw_action_pressed(h, G.jump));
}

/* ── pickups ───────────────────────────────────────────────────────── */

static void item_tick(void) {
  for (int i = 0; i < A.nitems; i++) {
    item *it = &A.items[i];
    float spin = (float)G.ticks * 0.04f + (float)i;
    cw_node3d_set_rotation(it->node, v3(0.4f, spin, 0.2f));
    cw_node3d_set_position(it->node, v3add(it->pos, v3(0, 0.55f + 0.08f * cw_sin((float)G.ticks * 0.05f + (float)i), 0)));
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
        case ITEM_HEALTH: if (f->hp < 100) f->hp = CW_MIN(100, f->hp + 35), take = true; respawn = 60 * 22; break;
        case ITEM_ARMOR: if (f->armor < 100) f->armor = CW_MIN(100, f->armor + 50), take = true; respawn = 60 * 30; break;
        case ITEM_AMMO:
          for (int k = 0; k < NWEAP; k++) if (f->have >> k & 1) f->ammo[k] = CW_MIN(W[k].max_ammo, f->ammo[k] + W[k].start_ammo / 2);
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
          break;
        }
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
  }
}

/* ── the match ─────────────────────────────────────────────────────── */

static const char *BOT_NAMES[] = { "Ash", "Brick", "Cinder", "Dune", "Ember", "Flint", "Gale", "Husk", "Iron", "Jolt", "Knox", "Lark", "Moss", "Nox", "Onyx", "Pike" };

/* the skinned fighter (assets/models/soldier_*.glb, tools/blender/soldier.py): one mesh on a 15 bone rig, clips idle, run and die */
static t3_gltf *RIG_GLTF[MAXF + 8];
static int rig_count;
static void *soldier_bytes[2];
static size_t soldier_len[2];

static void set_cast(t3_object *o) {
  if (o->type == T3_SKINNED_MESH || o->type == T3_MESH) o->cast_shadow = true, o->receive_shadow = true, o->frustum_culled = false;
  for (int i = 0; i < o->child_count; i++) set_cast(o->children[i]);
}

static cw_node make_body(int team, fighter *f) {
  int variant = (team & 1) ? 0 : 1; /* soldier_a is the blue one, soldier_b the red one */
  if (!soldier_bytes[variant]) {
    char path[48];
    snprintf(path, sizeof path, "models/soldier_%c.glb", variant ? 'b' : 'a');
    soldier_bytes[variant] = cw_asset_load(path, &soldier_len[variant]);
  }
  cw_node b = cw_node_new("Node3D", "Body");
  cw_node model = cw_node_new("Node3D", "Model");
  cw_node anim = cw_node_new("AnimationPlayer", "Anim");
  cw_node_add_child(b, model);
  cw_node_add_child(b, anim);
  cw_node_add_child(G.world, b);
  if (soldier_bytes[variant] && rig_count < (int)(sizeof RIG_GLTF / sizeof *RIG_GLTF)) {
    t3_gltf *gl = t3_gltf_parse(soldier_bytes[variant], soldier_len[variant], NULL, NULL);
    if (gl) {
      RIG_GLTF[rig_count++] = gl;
      cw_node3d_set_object(model, gl->scene);
      set_cast(gl->scene);
      for (int i = 0; i < gl->animation_count; i++) {
        cw_anim a = cw_anim_from_t3_clip(gl->animations[i], "Model");
        cw_anim_set_stored(a, false);
        cw_anim_set_loop(a, gl->animations[i]->name && gl->animations[i]->name[0] == 'd' ? CW_LOOP_NONE : CW_LOOP_LINEAR);
        cw_anim_player_add(anim, gl->animations[i]->name, a);
        cw_anim_release(a);
      }
      cw_anim_player_play(anim, "idle", 0);
    }
  }
  f->anim = anim;
  f->clip = "idle";
  return b;
}

static void clear_world(void) {
  if (cw_node_valid(G.world)) {
    fx_reset();
    cw_node_free(G.world);
    for (int i = 0; i < rig_count; i++) t3_release(RIG_GLTF[i]);
    rig_count = 0;
  }
  arena_free();
  memset(G.f, 0, sizeof G.f);
  memset(G.pr, 0, sizeof G.pr);
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
}

/* the setup's bot level scales every bot's skill: easy 0.65x, normal as drawn, hard pulled up toward 0.95 */
static float bot_skill(float s) {
  if (G.bot_level == 0) return s * 0.65f;
  if (G.bot_level == 2) return CW_MIN(0.95f, 0.6f + s * 0.4f);
  return s;
}

/* the bot count the AUTO setting fills in: the arenas play best with five or six fighters (an encounter every 8 to 20 s, see
 * tools/pacing.mjs), so one or two people get three bots, three or four people get two (team deathmatch rounds up to even teams) */
static int auto_bots(void) {
  int b = G.players <= 2 ? 3 : 2;
  if (G.mode == MODE_TDM && ((G.players + b) & 1)) b++;
  return b;
}

static void setup_fighters(void) {
  G.nf = 0;
  int bots;
  if (G.mode == MODE_COOP) bots = 0;
  else bots = G.bots_setting < 0 ? auto_bots() : G.bots_setting;
  int total_bots = G.mode == MODE_COOP ? 0 : CW_MIN(bots, MAXF - G.players);
  for (int i = 0; i < G.players + total_bots; i++) {
    fighter *f = F(i);
    memset(f, 0, sizeof *f);
    f->active = true;
    f->human = i < G.players ? i : -1;
    f->team = G.mode == MODE_DM ? i : (G.mode == MODE_COOP ? (i < G.players ? 0 : 1) : i % 2);
    if (G.mode == MODE_TDM) f->team = i % 2;
    f->skill = bot_skill(0.35f + 0.5f * (float)((i * 37) % 10) / 10.0f);
    if (f->human >= 0) snprintf(f->name, sizeof f->name, "P%d", i + 1);
    else snprintf(f->name, sizeof f->name, "%s", BOT_NAMES[i % 16]);
    cw_mover3d_def md = cw_mover3d_default_def();
    md.radius = PLAYER_R;
    md.height = PLAYER_HT;
    md.step_height = 0.45f;
    cw_mover3d_init(&f->mv, A.world, &md, v3(0, 0, 0));
    f->target = -1;
    f->goal = v3(0, -1000, 0);
    f->body = make_body(G.mode == MODE_DM ? i : f->team, f);
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
  int n = CW_MIN(2 + G.wave + G.players * (1 + G.wave / 2), MAXF - G.players);
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
    snprintf(f->name, sizeof f->name, "%s", BOT_NAMES[(slot + G.wave) % 16]);
    cw_mover3d_def md = cw_mover3d_default_def();
    md.radius = PLAYER_R;
    md.height = PLAYER_HT;
    md.step_height = 0.45f;
    cw_mover3d_init(&f->mv, A.world, &md, v3(0, 0, 0));
    f->goal = v3(0, -1000, 0);
    f->body = make_body(1, f);
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
  for (int i = 0; i < G.players; i++) cams[i] = G.cam[i];
  G.views = cw_shell_split(NULL, cams);
  cw_pipeline *pl = cw_pipeline_of(G.tree);
  cw_pipeline_set_views_shared_post(pl, G.players > 1);
  cw_node_set(G.cam[0], "current", cw_val_bool(true));
}

static void view_hook(int view, void *u);

static void begin_match(void) {
  build_world(G.arena_id);
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
  G.wave_state = 0;
  G.wave_timer = 60 * 3;
  G.lives = 4 + 3 * G.players;
  memset(G.feed, 0, sizeof G.feed);
  memset(G.hit_marker, 0, sizeof G.hit_marker);
  memset(G.kill_flash, 0, sizeof G.kill_flash);
  memset(G.dmg_flash, 0, sizeof G.dmg_flash);
  memset(G.aim, 0, sizeof G.aim);
  G.frag_limit = G.mode == MODE_TDM ? 30 : 15;
  G.time_limit = 60 * 60 * 5;
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
  setup_fighters();
  G.state = ST_PLAY;
}

static void end_match(int winner) {
  G.state = ST_OVER;
  G.winner = winner;
  G.over_ticks = 0;
}

static void check_rules(void) {
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
      for (int i = G.players; i < MAXF; i++) if (F(i)->active) { F(i)->active = false; if (cw_node_valid(F(i)->body)) cw_node_free(F(i)->body); F(i)->body = (cw_node){ 0 }; }
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

/* cameras and bodies follow the fighters: drawn from the tick's state */
static void sync_nodes(void) {
  for (int i = 0; i < G.nf; i++) {
    fighter *f = F(i);
    if (!f->active) continue;
    if (cw_node_valid(f->body)) {
      cw_node3d_set_position(f->body, f->mv.position);
      cw_node3d_set_rotation(f->body, v3(0, f->yaw, 0));
      float spd = v3len(v3(f->mv.velocity.x, 0, f->mv.velocity.z));
      const char *want = !f->alive ? "die" : (spd > 1.2f && f->mv.on_floor ? "run" : "idle");
      if (cw_node_valid(f->anim) && want != f->clip) {
        f->clip = want;
        cw_anim_player_play(f->anim, want, want[0] == 'd' ? 0.05f : 0.15f);
        if (want[0] == 'd') cw_anim_player_seek(f->anim, 0, false);
      }
      if (cw_node_valid(f->anim)) cw_node_set(f->anim, "speed_scale", cw_val_float(want[0] == 'r' ? CW_CLAMP(spd / 6.0f, 0.6f, 1.5f) : 1.0f));
      if (!f->alive && f->dead_ticks > 100) cw_node_set(f->body, "visible", cw_val_bool(false));
    }
    if (f->flash > 0) f->flash--;
    else if (cw_node_valid(f->light)) cw_node_set(f->light, "energy", cw_val_float(0));
  }
  if (G.views < 2 && G.nf > 0) { /* no view hook with one view: this player's own gun, not own body */
    if (cw_node_valid(F(0)->body)) cw_node_set(F(0)->body, "visible", cw_val_bool(false));
    cw_node_set(G.vm[0], "visible", cw_val_bool(F(0)->alive));
  }
  for (int h = 0; h < G.players; h++) {
    fighter *f = F(h);
    float sy = 0, bob = 0;
    if (f->alive) {
      bob = cw_sin(f->bob * 2.0f) * 0.035f * CW_MIN(1, v3len(v3(f->mv.velocity.x, 0, f->mv.velocity.z)) / 7.0f);
      sy = bob;
    }
    t3_vec3 e = eye_pos(f);
    float sk = G.shake[h];
    float jx = sk > 0.01f ? cw_rng_range(&G.rng, -1, 1) * sk * 0.025f : 0;
    float jy = sk > 0.01f ? cw_rng_range(&G.rng, -1, 1) * sk * 0.025f : 0;
    G.shake[h] *= 0.88f;
    float pitch = f->pitch + f->kick + jy;
    float yaw = f->yaw + jx;
    if (!f->alive) { /* a dead player's view drops and tilts */
      e.y = f->mv.position.y + 0.5f;
      pitch = -0.5f;
    }
    t3_vec3 d = look_dir(yaw, pitch);
    cw_node3d_set_position(G.cam[h], v3add(e, v3(0, sy, 0)));
    cw_node3d_look_at(G.cam[h], v3add(v3add(e, v3(0, sy, 0)), d), v3(0, 1, 0));
    f->kick *= 0.82f;
    /* the viewmodel: the gun drops in when drawn, lags the look (sway), bobs with the stride and kicks back and up on fire */
    static const float MUZ[NWEAP] = { 0.78f, 0.8f, 0.71f, 0.82f };
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
    cw_node3d_set_position(G.vm[h], v3(0.2f + bx - dyaw[h] * 0.25f, -0.2f + by - draw[h] * 0.3f + dpitch[h] * 0.2f, -0.36f + kick * 0.075f));
    cw_node3d_set_rotation(G.vm[h], v3(kick * 0.1f + draw[h] * 0.5f + dpitch[h] * 0.5f, dyaw[h] * 0.5f + 0.03f, -bx * 1.5f));
    cw_node3d_set_position(G.vm_flash[h], v3(0, 0.04f * VM_SCALE, -(MUZ[f->weapon] + 0.05f) * VM_SCALE));
    cw_node_set(G.vm_flash[h], "visible", cw_val_bool(f->flash > 0 && f->alive));
    G.hit_marker[h] *= 0.9f;
    G.dmg_flash[h] *= 0.93f;
    G.kill_flash[h] *= 0.965f;
  }
}

/* the view hook: each view shows its own gun and hides its own body */
static void set_obj_visible(cw_node n, bool v) {
  t3_object *o = cw_node3d_object(n);
  if (o) o->visible = v; /* the hook runs after the tree's pre-render: set the drawn object itself */
}
static void view_hook(int view, void *u) {
  (void)u;
  for (int h = 0; h < G.players; h++) set_obj_visible(G.vm[h], h == view && F(h)->alive);
  for (int i = 0; i < G.nf; i++) {
    fighter *f = F(i);
    if (cw_node_valid(f->body)) set_obj_visible(f->body, i != view && (f->alive || f->dead_ticks <= 100));
  }
}

static void game_step(void);
static void fixed_update(void *user, double dt) {
  (void)user, (void)dt;
  if (!cw_shell_update()) return;
#ifdef ARENA_AUTOPILOT
  /* ARENA_SIM_BATCH=n: n game ticks per engine tick, for pacing and soak runs that measure the simulation only (every seat is a
   * bot, so no input is lost); the game time per drawn frame grows n times */
  static int batch = -1;
  if (batch < 0) batch = getenv("ARENA_SIM_BATCH") ? CW_CLAMP(atoi(getenv("ARENA_SIM_BATCH")), 1, 1000) : 1;
  for (int b = 0; b < batch; b++) game_step();
#else
  game_step();
#endif
}

static void game_step(void) {
  G.steps++; /* every game tick since the start, across matches (soaks) */
  if (G.state == ST_SETUP) {
    int h = 0;
    if (cw_action_just_pressed(h, G.mv_f)) G.setup_row = (G.setup_row + 5) % 6, fx_play_ui(SND_CLICK, 0.4f);
    if (cw_action_just_pressed(h, G.mv_b)) G.setup_row = (G.setup_row + 1) % 6, fx_play_ui(SND_CLICK, 0.4f);
    int dir = cw_action_just_pressed(h, G.mv_r) - cw_action_just_pressed(h, G.mv_l);
    if (dir) {
      fx_play_ui(SND_TICK, 0.5f);
      if (G.setup_row == 0) { G.arena_id = (G.arena_id + ARENAS + dir) % ARENAS; build_world(G.arena_id); }
      else if (G.setup_row == 1) G.mode = (G.mode + MODE_COUNT + dir) % MODE_COUNT;
      else if (G.setup_row == 2) G.bots_setting = CW_CLAMP(G.bots_setting + dir, -1, MAXF - 4);
      else if (G.setup_row == 3) G.bot_level = (G.bot_level + 3 + dir) % 3;
      else if (G.setup_row == 4) G.aim_assist = !G.aim_assist;
    }
    if (cw_action_just_pressed(h, G.jump) || cw_action_just_pressed(h, G.fire)) {
      { begin_match(); set_views(); }
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
    if (G.over_ticks > 60 * 3 && (cw_action_just_pressed(0, G.fire) || cw_action_just_pressed(0, G.jump) || AUTO)) {
      if (AUTO && G.over_ticks < 60 * 8) return;
      G.state = ST_SETUP;
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
  /* the path budget: oldest requests first, a few a tick */
  cw_nav_tick(A.nav, 3, 700);
  for (int i = 0; i < G.nf; i++) {
    fighter *f = F(i);
    if (!f->active) continue;
    if (f->cooldown > 0) f->cooldown--;
    if (f->spawn_guard > 0) f->spawn_guard--;
    if (!f->alive) {
      f->dead_ticks++;
      bool can_respawn = G.mode != MODE_COOP || f->human < 0 ? G.mode != MODE_COOP : G.lives > 0;
      if (f->dead_ticks > (f->human >= 0 ? 150 : 120) && can_respawn) {
        if (G.mode == MODE_COOP && f->human >= 0) G.lives--;
        spawn_fighter(f);
      }
      continue;
    }
    f->since_spawn++;
    f->alive_t++;
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
    if (is_bot(f)) bot_tick(f, i);
    else human_tick(f, i);
    /* a footstep now and then */
    if (f->mv.on_floor && (int)(f->bob * 0.5f) != (int)((f->bob - 0.12f) * 0.5f) && v3len(v3(f->mv.velocity.x, 0, f->mv.velocity.z)) > 3 && f->human >= 0) fx_play_ui(SND_STEP, 0.18f);
  }
  proj_tick();
  item_tick();
  check_rules();
  fx_tick();
  for (int i = 0; i < MAX_FEED; i++) if (G.feed[i].ticks > 0) G.feed[i].ticks--;
  sync_nodes();
  for (int i = 0; i < MAXF; i++) {
    const fighter *f = F(i);
    G.d_score[i] = f->score, G.d_hp[i] = f->hp, G.d_deaths[i] = f->deaths, G.d_weapon[i] = f->weapon, G.d_alive[i] = f->active && f->alive;
    G.d_pos[i * 2] = f->mv.position.x, G.d_pos[i * 2 + 1] = f->mv.position.z;
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

static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  cw_r2d_text(G.font, s, cw_v2(x, y), &(cw_text_style){ .size = size, .color = c, .outline_width = size * 0.09f, .align = align, .wrap_width = wrap });
}

static void hud_view(int h) {
  fighter *f = F(h);
  cw_rect r = cw_shell_player_rect(h);
  float u = r.h / 22.0f; /* a unit that scales with the view's height */
  if (u < 8) u = 8;
  float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  /* damage flash at the edges */
  if (G.dmg_flash[h] > 0.02f) {
    cw_color c = cw_rgba(0.9f, 0.05f, 0.05f, G.dmg_flash[h] * 0.45f);
    cw_r2d_rect(cw_rect_make(r.x, r.y, r.w, u * 0.8f), c);
    cw_r2d_rect(cw_rect_make(r.x, r.y + r.h - u * 0.8f, r.w, u * 0.8f), c);
    cw_r2d_rect(cw_rect_make(r.x, r.y, u * 0.8f, r.h), c);
    cw_r2d_rect(cw_rect_make(r.x + r.w - u * 0.8f, r.y, u * 0.8f, r.h), c);
  }
  /* where the hit came from: a bar on a ring around the crosshair, rotated to the shooter's bearing */
  if (G.dmg_flash[h] > 0.05f && f->alive) {
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
    float gap = u * (0.35f + (f->weapon == 1 ? 1.2f : 0.25f) + f->kick * 20), len = u * 0.7f, th = CW_MAX(2.0f, u * 0.12f);
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
  }
  /* a kill: a gold cross over the crosshair and the victim's name, longer than a hit marker */
  if (G.kill_flash[h] > 0.05f) {
    float a = CW_MIN(1.0f, G.kill_flash[h] * 1.5f), d = u * 1.2f;
    cw_color kc = cw_rgba(1, 0.85f, 0.2f, a);
    cw_r2d_line(cw_v2(cx - d, cy - d), cw_v2(cx + d, cy + d), CW_MAX(3.0f, u * 0.2f), kc);
    cw_r2d_line(cw_v2(cx + d, cy - d), cw_v2(cx - d, cy + d), CW_MAX(3.0f, u * 0.2f), kc);
    char ks[40];
    snprintf(ks, sizeof ks, "FRAGGED %s", G.kill_name[h]);
    text(ks, r.x, cy + u * 2.6f, u * 1.2f, cw_rgba(1, 0.85f, 0.2f, a), CW_ALIGN_CENTER, r.w);
  }
  /* health and armour bars, bottom left */
  float bw = r.w * 0.28f, bh = u * 0.7f, bx = r.x + u, by = r.y + r.h - u * 3.2f;
  cw_r2d_rect(cw_rect_make(bx, by, bw, bh), cw_rgba(0, 0, 0, 0.55f));
  float hpk = cw_clampf((float)f->hp / 100.0f, 0, 1);
  cw_r2d_rect(cw_rect_make(bx, by, bw * hpk, bh), f->hp > 30 ? cw_rgba(0.25f, 0.85f, 0.35f, 0.95f) : cw_rgba(0.95f, 0.25f, 0.2f, 0.95f));
  cw_r2d_rect(cw_rect_make(bx, by + bh * 1.4f, bw, bh * 0.6f), cw_rgba(0, 0, 0, 0.55f));
  cw_r2d_rect(cw_rect_make(bx, by + bh * 1.4f, bw * cw_clampf((float)f->armor / 100.0f, 0, 1), bh * 0.6f), cw_rgba(0.3f, 0.6f, 1, 0.95f));
  char s[48];
  snprintf(s, sizeof s, "%d", f->hp > 0 ? f->hp : 0);
  text(s, bx + bw + u * 0.4f, by - u * 0.45f, u * 1.8f, cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT, 0);
  /* weapon and ammo, bottom right */
  /* the name over a big ammo count: two short lines, so neither meets the health number in a half-width view */
  snprintf(s, sizeof s, "%d", f->ammo[f->weapon]);
  text(s, r.x, r.y + r.h - u * 3.4f, u * 1.8f, cw_rgba(1, 0.9f, 0.6f, 1), CW_ALIGN_RIGHT, r.w - u);
  text(W[f->weapon].name, r.x, r.y + r.h - u * 4.8f, u * 0.85f, cw_rgba(1, 0.9f, 0.6f, 0.85f), CW_ALIGN_RIGHT, r.w - u);
  /* weapon pips */
  for (int k = 0; k < NWEAP; k++) {
    bool have = f->have >> k & 1;
    cw_color c = k == f->weapon ? cw_rgba(1, 0.85f, 0.3f, 1) : have ? cw_rgba(1, 1, 1, 0.6f) : cw_rgba(1, 1, 1, 0.15f);
    cw_r2d_rect(cw_rect_make(r.x + r.w - u * (1 + 1.3f * (float)(NWEAP - k)), r.y + r.h - u * 0.9f, u * 1.0f, u * 0.25f), c);
  }
  /* score, top left */
  if (G.mode == MODE_COOP) snprintf(s, sizeof s, "WAVE %d   LIVES %d", G.wave, G.lives);
  else snprintf(s, sizeof s, "FRAGS %d", f->score);
  text(s, r.x + u, r.y + u * 0.5f, u * 1.3f, cw_rgba(1, 1, 1, 1), CW_ALIGN_LEFT, 0);
  if (G.mode == MODE_TDM) {
    snprintf(s, sizeof s, "%s %d : %d %s", TEAM_NAME[0], G.team_score[0], G.team_score[1], TEAM_NAME[1]);
    text(s, r.x + u, r.y + u * 2.1f, u * 0.9f, cw_rgba(TEAM_RGB[f->team & 1][0], TEAM_RGB[f->team & 1][1], TEAM_RGB[f->team & 1][2], 1), CW_ALIGN_LEFT, 0);
  }
  if (!f->alive) {
    text("DOWN", cx - r.w / 2, cy - u * 3, u * 3.2f, cw_rgba(1, 0.3f, 0.25f, 1), CW_ALIGN_CENTER, r.w);
    if (!(G.mode == MODE_COOP && G.lives <= 0)) {
      char rs[24];
      snprintf(rs, sizeof rs, "back in %d", CW_MAX(1, (150 - f->dead_ticks + 59) / 60));
      text(rs, cx - r.w / 2, cy + u * 0.3f, u * 1.2f, cw_rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER, r.w);
    }
    if (G.mode == MODE_COOP && G.lives <= 0) text("NO LIVES LEFT", cx - r.w / 2, cy, u * 1.2f, cw_rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER, r.w);
  }
  if (f->spawn_guard > 0 && f->alive) cw_r2d_rect_lines(r, 3, cw_rgba(0.4f, 0.8f, 1, 0.5f));
}

static void hud_feed(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 40;
  float y = u;
  for (int i = 0; i < MAX_FEED; i++) {
    if (G.feed[i].ticks <= 0) continue;
    float a = CW_MIN(1.0f, (float)G.feed[i].ticks / 30.0f);
    text(G.feed[i].text, 0, y, u * 1.05f, cw_rgba(1, 1, 1, a), CW_ALIGN_RIGHT, cs.x - u);
    y += u * 1.3f;
  }
  /* co-op: the next wave announced while the last is cleared, so the lull reads as a pause and not a stall */
  if (G.state == ST_PLAY && G.mode == MODE_COOP && G.wave_state == 0) {
    char s[48];
    snprintf(s, sizeof s, "WAVE %d IN %d", G.wave + 1, G.wave_timer / 60 + 1);
    text(s, 0, cs.y * 0.14f, u * 2.2f, cw_rgba(1, 0.85f, 0.3f, 1), CW_ALIGN_CENTER, cs.x);
  }
  /* the clock in the middle top */
  if (G.state == ST_PLAY) {
    char s[32];
    int secs = (G.time_limit - G.match_ticks) / 60;
    snprintf(s, sizeof s, "%d:%02d", secs / 60, secs % 60);
    text(s, cs.x / 2 - u * 3, cs.y / 2 - u * 0.7f, u * 1.4f, cw_rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER, u * 6);
  }
}

static void hud_setup(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 18;
  cw_r2d_rect(cw_rect_make(0, 0, cs.x, cs.y), cw_rgba(0, 0, 0, 0.55f));
  text("ARENA", 0, u * 1.2f, u * 2.4f, cw_rgba(1, 0.85f, 0.4f, 1), CW_ALIGN_CENTER, cs.x);
  text("choose the match, then press fire", 0, u * 3.6f, u * 0.9f, cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, cs.x);
  char s[96];
  const char *rows[6];
  char r0[64], r1[64], r2[64], r3[64], r4[64];
  snprintf(r0, sizeof r0, "<  %s  >", ARENA_INFO[G.arena_id].name);
  snprintf(r1, sizeof r1, "<  %s  >", MODE_NAME[G.mode]);
  if (G.mode == MODE_COOP) snprintf(r2, sizeof r2, "waves of bots");
  else if (G.bots_setting < 0) snprintf(r2, sizeof r2, "<  AUTO: %d bots  >", auto_bots());
  else snprintf(r2, sizeof r2, "<  %d bots  >", G.bots_setting);
  static const char *LEVEL[3] = { "EASY", "NORMAL", "HARD" };
  snprintf(r3, sizeof r3, "<  bots: %s  >", LEVEL[G.bot_level]);
  rows[0] = r0, rows[1] = r1, rows[2] = r2, snprintf(r4, sizeof r4, "<  aim assist: %s  >", G.aim_assist ? "ON" : "OFF");
  rows[3] = r3, rows[4] = r4, rows[5] = "START";
  for (int i = 0; i < 6; i++) {
    bool sel = i == G.setup_row;
    text(rows[i], 0, u * (5.2f + 1.5f * (float)i), u * 1.3f, sel ? cw_rgba(1, 0.9f, 0.3f, 1) : cw_rgba(1, 1, 1, 0.75f), CW_ALIGN_CENTER, cs.x);
  }
  snprintf(s, sizeof s, "%s", ARENA_INFO[G.arena_id].blurb);
  text(s, 0, u * 14.7f, u * 0.9f, cw_rgba(1, 1, 1, 0.7f), CW_ALIGN_CENTER, cs.x);
  snprintf(s, sizeof s, "%d player%s", G.players, G.players == 1 ? "" : "s");
  text(s, 0, u * 15.9f, u * 0.9f, cw_rgba(1, 1, 1, 0.7f), CW_ALIGN_CENTER, cs.x);
}

static void hud_over(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 22;
  cw_r2d_rect(cw_rect_make(0, cs.y * 0.18f, cs.x, cs.y * 0.64f), cw_rgba(0, 0, 0, 0.62f));
  char s[96];
  if (G.mode == MODE_COOP) snprintf(s, sizeof s, G.winner == 0 ? "ARENA HELD  -  all %d waves cleared" : "OVERRUN  -  wave %d", G.winner == 0 ? 6 : G.wave);
  else if (G.mode == MODE_TDM) snprintf(s, sizeof s, "%s TEAM WINS", TEAM_NAME[G.winner & 1]);
  else snprintf(s, sizeof s, "%s WINS", G.winner >= 0 ? F(G.winner)->name : "NOBODY");
  text(s, 0, cs.y * 0.22f, u * 2.2f, cw_rgba(1, 0.85f, 0.3f, 1), CW_ALIGN_CENTER, cs.x);
  /* the board, best first */
  int order[MAXF], n = 0;
  for (int i = 0; i < G.nf; i++) if (F(i)->active || F(i)->deaths || F(i)->score) order[n++] = i;
  for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (F(order[b])->score > F(order[a])->score) { int t = order[a]; order[a] = order[b]; order[b] = t; }
  for (int k = 0; k < n && k < 10; k++) {
    fighter *f = F(order[k]);
    snprintf(s, sizeof s, "%-8s  %3d frags  %3d deaths", f->name, f->score, f->deaths);
    text(s, 0, cs.y * 0.34f + u * 1.25f * (float)k, u * 1.0f, f->human >= 0 ? cw_rgba(1, 1, 0.6f, 1) : cw_rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER, cs.x);
  }
  if (G.over_ticks > 60 * 3) text("press fire to continue", 0, cs.y * 0.74f, u * 0.9f, cw_rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, cs.x);
}

static void render(void *user, double alpha) {
  (void)user, (void)alpha;
  cw_shell_screen sc = cw_shell_screen_now();
  cw_quality_auto_measure(sc == CW_SHELL_PLAY && G.state == ST_PLAY); /* Auto steps the preset down when play runs slow */
  if (sc == CW_SHELL_PLAY || sc == CW_SHELL_PAUSE) {
    cw_r2d_begin(NULL);
    if (G.state != ST_SETUP) for (int h = 0; h < G.players; h++) hud_view(h);
    hud_feed();
    if (G.state == ST_SETUP) hud_setup();
    if (G.state == ST_OVER) hud_over();
    cw_r2d_end();
  }
  cw_shell_draw();
}

/* ── start, init ───────────────────────────────────────────────────── */

/* the glTF objects of a viewmodel sit on the viewmodel layer, never cast shadows (own body shadow would look wrong) and are never culled by their own bounds */
static void set_vm_layers(t3_object *o) {
  o->layers = 2;
  o->cast_shadow = false;
  o->receive_shadow = false; /* the gun hangs at the eye: a sun shadow across it reads as a black slab */
  o->frustum_culled = false;
  for (int i = 0; i < o->child_count; i++) set_vm_layers(o->children[i]);
}

static void make_viewmodels(void) {
  for (int h = 0; h < MAXH; h++) {
    G.vm[h] = cw_node_new("Node3D", "VM");
    cw_node_set(G.vm[h], "layers", cw_val_int(2));
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
      set_vm_layers(gl->scene);
    }
    G.vm_flash[h] = cw_node_new("MeshInstance3D", "Flash");
    cw_node_set(G.vm_flash[h], "primitive", cw_val_enum("SPHERE"));
    cw_node_set(G.vm_flash[h], "size", cw_val_vec3(0.06f, 0, 0));
    cw_node_set(G.vm_flash[h], "color", cw_val_color(1, 0.8f, 0.4f, 1));
    cw_node_set(G.vm_flash[h], "emissive", cw_val_color(7, 4.5f, 1.6f, 1));
    cw_node_set(G.vm_flash[h], "cast_shadow", cw_val_bool(false));
    cw_node_set(G.vm_flash[h], "visible", cw_val_bool(false));
    cw_node3d_set_position(G.vm_flash[h], v3(0, 0.02f, -0.72f));
    cw_node_add_child(G.vm[h], G.vm_flash[h]);
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

static void start(int players, void *user) {
  (void)user;
  G.players = players;
  G.state = ST_SETUP;
  G.setup_row = 5;
  for (int i = 0; i < MAXH; i++) cw_node_set(G.cam[i], "current", cw_val_bool(i == 0));
  build_world(G.arena_id);
  set_views();
  overview_cams();
#ifdef ARENA_AUTOPILOT
  begin_match();
  set_views();
#endif
}

static void cmd_match(int argc, const char **argv, cw_reply *r) {
  if (argc > 0) G.arena_id = CW_CLAMP(atoi(argv[0]), 0, ARENAS - 1);
  if (argc > 1) G.mode = CW_CLAMP(atoi(argv[1]), 0, MODE_COUNT - 1);
  if (argc > 2) G.bots_setting = CW_CLAMP(atoi(argv[2]), -1, MAXF - 4);
  if (argc > 3) G.players = CW_CLAMP(atoi(argv[3]), 1, MAXH);
  if (G.players < 1) G.players = 1;
  begin_match();
  set_views();
  cw_reply_raw(r, "{\"ok\": true}");
}

static void quality_changed(cw_quality q, void *u) {
  (void)u;
  (void)q;
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
  G.w_next = cw_action_find("weapon_next"), G.w_prev = cw_action_find("weapon_prev");
  G.font = cw_ui_font_sans();
  G.arena_id = 0;
  G.mode = MODE_DM;
  G.bots_setting = -1; /* AUTO */
  G.bot_level = 1;
  G.aim_assist = 1;
  cw_shell_init(&(cw_shell_desc){ .title = "ARENA", .subtitle = "split screen shooter", .max_players = MAXH, .start = start });
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
    cw_node_set(G.cam[h], "viewmodel_layers", cw_val_int(2));
    cw_node_set(G.cam[h], "viewmodel_fov", cw_val_float(52));
    cw_node_add_child(G.root, G.cam[h]);
  }
  cw_node_set(G.cam[0], "current", cw_val_bool(true));
  make_viewmodels();
  build_world(G.arena_id); /* the join screen shows the arena, not an empty room */
  overview_cams();
  cw_pipeline_set_view_hook(cw_pipeline_of(G.tree), view_hook, NULL);
  cw_quality_on_change(quality_changed, NULL);
  cw_quality_auto(true, 0);
  cw_debug_i32("game.state", (int32_t *)&G.state);
  cw_debug_i32("game.mode", &G.mode);
  cw_debug_i32("game.arena", &G.arena_id);
  cw_debug_i32("game.players", &G.players);
  cw_debug_i32("game.ticks", &G.ticks);
  cw_debug_i32("game.steps", &G.steps);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_i32("match.wave", &G.wave);
  cw_debug_i32("match.lives", &G.lives);
  cw_debug_i32("match.winner", &G.winner);
  cw_debug_i32("match.nf", &G.nf);
  cw_debug_i32("nav.requests", &G.path_requests);
  cw_debug_i32("nav.done", &G.path_done);
  cw_debug_i32("views", &G.views);
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
  cw_debug_field("stats.by_weapon", G.st.by_weapon, CW_DBG_I32, NWEAP);
  cw_debug_field("fighters.score", G.d_score, CW_DBG_I32, MAXF);
  cw_debug_field("fighters.hp", G.d_hp, CW_DBG_I32, MAXF);
  cw_debug_field("fighters.deaths", G.d_deaths, CW_DBG_I32, MAXF);
  cw_debug_field("fighters.weapon", G.d_weapon, CW_DBG_I32, MAXF);
  cw_debug_field("fighters.alive", G.d_alive, CW_DBG_I32, MAXF);
  cw_debug_field("fighters.pos", G.d_pos, CW_DBG_F32, MAXF * 2);
  cw_debug_command("match", "match [arena] [mode] [bots] [players]: start a match now", cmd_match);
#ifdef ARENA_AUTOPILOT
  /* the autopilot: ARENA_ID, ARENA_MODE, ARENA_BOTS, ARENA_PLAYERS choose the match (tests, benches) */
  const char *e;
  if ((e = getenv("ARENA_ID"))) G.arena_id = CW_CLAMP(atoi(e), 0, ARENAS - 1);
  if ((e = getenv("ARENA_MODE"))) G.mode = CW_CLAMP(atoi(e), 0, MODE_COUNT - 1);
  if ((e = getenv("ARENA_BOTS"))) G.bots_setting = CW_CLAMP(atoi(e), -1, MAXF - 4);
  if ((e = getenv("ARENA_LEVEL"))) G.bot_level = CW_CLAMP(atoi(e), 0, 2);
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
