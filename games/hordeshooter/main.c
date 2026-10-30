/* hordeshooter (working title): a 1-4 player co-op twin-stick survival
 * shooter on one shared camera, on cartwheel and three.c.
 *
 *   play      move with the left stick (WASD), aim with the right stick
 *             (arrow keys); aiming fires. A / Space fires toward the last
 *             aim, the left shoulder / Shift throws a blast. Gems level you
 *             up and each level adds an upgrade. Barrels explode, crates
 *             drop gems, cinder walls fall to blasts. A wave every 30
 *             seconds, a boss every fifth. A downed player gets up at the
 *             next wave or when a friend stands close for two seconds.
 *   camera    one camera frames every living player and zooms to fit them.
 *   hordeshooter_auto plays itself (for hosts that send no input).
 * Environment (development): HORDE_WAVE=n starts at wave n; HORDE_PLAYERS=n
 * sets the player count of the auto build; HORDE_BENCH=1 prints ms per
 * frame every 120 frames; HORDE_BAKE=<file> bakes the yard's lighting again and
 * writes it there (lightmaps/yard.cwlm ships); HORDE_HIDE=bits hides the crowd (1),
 * the blob shadows (2) or the light pools (4) for A/B timing. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/aim.h"
#include "cartwheel/audio.h"
#include "cartwheel/core.h"
#include "cartwheel/decals.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/profile.h"
#include "cartwheel/project.h"
#include "cartwheel/quality.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/ui.h"
#include <time.h>
#include "game.h"
#include "levelbuild.h"

/* wall clock for the bench (cw_time is the fixed test clock there) */
static double cw_wall(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

typedef struct save_data { int32_t best_wave, best_score; } save_data;

static lb_level LEVELS[NLEVELS]; /* each made floor's .cwlevel, parsed once a run */
static struct {
  sim S;
  bool in_match, built;
  int players;
  int32_t state, ticks, wave, enemies, kills, alive_players, boss, best_wave;
  int32_t hp[MAXP], level[MAXP], score[MAXP], pkills[MAXP], px[MAXP], py[MAXP];
  uint32_t hash;
  int banner_t, banner_kind, banner_n;
  float frame_ms;
  double ms_acc;
  int ms_n;
  cw_action move[4], aim[4], fire, bomb, pick[3];
  int won_ticks;
  cw_node camera, sun;
  int level_flash[MAXP], level_up[MAXP];
  /* the camera gate: frames the camera jerked (its position's second difference over 2 cm: a shake, never the
   * smooth follow) and the blasts seen, for tests */
  int32_t cam_jerks, booms;
  /* the melee gate (HORDE_MELEE_TEST): frames that drew an enemy in an attack pose, strikes, strikes that hit,
   * and per kind the ticks from an attack's start to its damage */
  int32_t m_attack_frames, m_strikes, m_hits, m_delay[EK_KINDS];
  int m_test, m_start[MAXE];
  /* the floor gate: the floor (0..7), the ticks the boss's floor cleared, the hatch opened, a player stepped in
   * and the next floor started (0 until it happens), the enemies left when the hatch opened */
  int32_t floor_level, floor_boss_tick, floor_clear_tick, floor_hatch_tick, floor_descend_tick, floor_next_tick, floor_left_at_hatch;
  int floor_stay, fade_t;
  int32_t dev_boss; /* development (romdev): 1 skips to this floor's boss wave and keeps its boss weak, 2 also fells it, to see the floor end */
  int32_t dev_hold; /* development (romdev, screenshots): 1 the bots stand where they are, 2 and hold their fire, so the horde reaches them */
  t3_vec3 cam_p[2];
  int cam_n;
} G;

static cw_font font;
static double T_sim, T_view, T_fx, T_frame, T_last; /* ms, averaged over the bench window */
static int T_n;
static cw_sound snd_boom, snd_shot, snd_kill, snd_gem, snd_move, snd_select, snd_back, snd_music, snd_power, snd_hatch, snd_descend;
/* the floors' names (LEVELS.md); the code reads only these until a floor's own scene lands */
static const char *const FLOOR_NAME[NLEVELS] = { "The Yard", "The Drowned Sluice", "The Ossuary", "The Foundry", "The Grotto", "The Cathedral", "The Rift", "The Core" };
static cw_voice music;

/* ── sounds: a few synthesised waves so the repository needs no audio files ── */

static cw_sound synth(float seconds, float hz0, float hz1, float noise, float gain, float decay) {
  enum { RATE = 48000 };
  static float pcm[RATE * 3];
  int n = CW_MIN((int)(seconds * RATE), RATE * 3);
  double phase = 0;
  uint32_t r = 7;
  float lp = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / (float)n;
    phase += 2 * 3.14159265358979 * (hz0 + (hz1 - hz0) * t) / RATE;
    r = r * 1664525u + 1013904223u;
    float nz = ((float)(r >> 8) / 8388608.0f - 1.0f);
    lp += (nz - lp) * 0.12f;
    pcm[i] = ((float)sin(phase) * (1 - noise) + lp * noise * 2.2f) * gain * (float)exp(-t * decay) * (i < 200 ? (float)i / 200.0f : 1);
  }
  return cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
}
static void play(cw_sound s, cw_bus bus, float gain) { cw_audio_play(s, &(cw_play_desc){ .bus = bus, .volume = gain }); }

static void shell_sound(cw_shell_sound s, void *u) {
  (void)u;
  play(s == CW_SHELL_SND_MOVE ? snd_move : s == CW_SHELL_SND_BACK ? snd_back : snd_select, cw_shell_sfx_bus(), 1);
}
static void shell_screen(cw_shell_screen from, cw_shell_screen to, void *u) {
  (void)from, (void)u;
  bool want = to == CW_SHELL_TITLE || to == CW_SHELL_PLAY;
  if (want && !cw_voice_is_playing(music)) music = cw_audio_play(snd_music, &(cw_play_desc){ .bus = cw_shell_music_bus(), .loop = true, .volume = 0.5f });
  if (!want && cw_voice_is_playing(music)) cw_voice_stop(music, 0.3f);
}

/* ── the match ──────────────────────────────────────────────────────── */

static void build_world(void) {
  if (G.built) return;
  cw_node root = cw_tree_root(cw_scene_app_tree());
  view_init(root);
  fx_init(root);
  G.camera = cw_node_get(root, "Arena/Camera");
  G.sun = cw_node_get(root, "Arena/Sun");
  G.built = true;
}

static void start(int players, void *u) {
  (void)u;
  build_world();
#ifndef HORDE_START_WAVE
#define HORDE_START_WAVE 1
#endif
  int wave = getenv("HORDE_WAVE") ? atoi(getenv("HORDE_WAVE")) : HORDE_START_WAVE; /* hordeshooter_bench: a late wave from the start (carts read no environment) */
  sim_init(&G.S, players, 4242, wave);
  horde_floors_load(&G.S, LEVELS); /* each made floor's layout (the rest are played in the yard) */
  G.floor_stay = getenv("HORDE_FLOOR_STAY") && atoi(getenv("HORDE_FLOOR_STAY"));
  G.floor_level = G.S.level, G.floor_boss_tick = G.floor_clear_tick = G.floor_hatch_tick = G.floor_descend_tick = G.floor_next_tick = 0, G.floor_left_at_hatch = -1;
  G.m_test = getenv("HORDE_MELEE_TEST") ? (getenv("HORDE_MELEE_DODGE") && atoi(getenv("HORDE_MELEE_DODGE")) ? 2 : 1) : 0;
  if (G.m_test) { /* the melee gate: one player standing still (or stepping away from each wind-up), a runner and a brute walking in */
    G.S.no_spawn = true, G.S.no_power = true;
    const player *p = &G.S.p[0];
    sim_spawn(&G.S, EK_RUNNER, p->x + 6.0f, p->y);
    sim_spawn(&G.S, EK_BRUTE, p->x - 7.0f, p->y + 1.0f);
  }
  G.players = players;
  G.in_match = true;
  G.ticks = 0;
  G.banner_t = 180, G.banner_kind = 0;
  if (getenv("HORDE_POWER_SHOW")) /* development (screenshots): one of each power-up in a ring round the team */
    for (int t = 0; t < PU_KINDS; t++) sim_power_drop(&G.S, cw_cos((float)t * 0.5236f) * 7.0f, cw_sin((float)t * 0.5236f) * 5.0f, t);
  if (getenv("HORDE_POWER")) /* development (screenshots, surveys): every player starts holding power-up n, refreshed as it runs out */
    for (int i = 0; i < players; i++) sim_power_give(&G.S, i, atoi(getenv("HORDE_POWER")) % PU_KINDS);
  memset(G.level_flash, 0, sizeof G.level_flash);
}

static void init(void *u) {
  (void)u;
  cw_project_load_input();
  const char *names[4] = { "move_left", "move_right", "move_up", "move_down" }, *an[4] = { "aim_left", "aim_right", "aim_up", "aim_down" };
  for (int i = 0; i < 4; i++) G.move[i] = cw_action_find(names[i]), G.aim[i] = cw_action_find(an[i]);
  G.fire = cw_action_find("fire");
  G.bomb = cw_action_find("bomb");
  G.pick[0] = cw_action_find("pick1"), G.pick[1] = cw_action_find("pick2"), G.pick[2] = cw_action_find("pick3");
  cw_shell_init(&(cw_shell_desc){ .title = "HORDE", .subtitle = "hold the yard together", .max_players = MAXP,
                                  .game_save_size = sizeof(save_data), .game_save_version = 1, .start = start,
                                  .sound = shell_sound, .screen_changed = shell_screen });
  font = cw_ui_font_sans();
  cw_quality_full_resolution(true); /* no preset and no Auto step draws the yard below the output size: no upscaling */
  if (getenv("HORDE_QUALITY")) cw_quality_set((cw_quality)CW_CLAMP(atoi(getenv("HORDE_QUALITY")), 0, 3), true);
  snd_boom = synth(1.1f, 70, 28, 0.75f, 1.0f, 3.5f);
  snd_shot = synth(0.14f, 260, 55, 0.62f, 0.5f, 9); /* a low thump with a crack of noise */
  snd_kill = synth(0.12f, 200, 90, 0.7f, 0.4f, 5);
  snd_gem = synth(0.1f, 1200, 1800, 0, 0.25f, 4);
  snd_power = synth(0.45f, 440, 1320, 0.05f, 0.45f, 2.5f); /* a rising chime: a power-up taken */
  snd_hatch = synth(2.2f, 48, 130, 0.35f, 0.9f, 1.2f);  /* the hatch grinding open, rising */
  snd_descend = synth(1.0f, 220, 55, 0.2f, 0.6f, 2.0f); /* stepping down: a falling sweep */
  snd_move = synth(0.05f, 660, 660, 0, 0.25f, 2);
  snd_select = synth(0.12f, 520, 1040, 0, 0.3f, 3);
  snd_back = synth(0.12f, 520, 260, 0, 0.3f, 3);
  snd_music = synth(2.0f, 55, 55, 0.05f, 0.3f, 0.2f);
  G.best_wave = ((save_data *)cw_shell_game_save())->best_wave;
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.ticks", &G.ticks);
  cw_debug_i32("game.wave", &G.wave);
  cw_debug_i32("game.enemies", &G.enemies);
  cw_debug_i32("game.kills", &G.kills);
  cw_debug_i32("game.alive_players", &G.alive_players);
  cw_debug_i32("game.boss", &G.boss);
  cw_debug_field("player.hp", G.hp, CW_DBG_I32, MAXP);
  cw_debug_field("player.level", G.level, CW_DBG_I32, MAXP);
  cw_debug_field("player.score", G.score, CW_DBG_I32, MAXP);
  cw_debug_field("player.kills", G.pkills, CW_DBG_I32, MAXP);
  cw_debug_field("player.x", G.px, CW_DBG_I32, MAXP);
  cw_debug_field("player.y", G.py, CW_DBG_I32, MAXP);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_i32("view.cam_jerks", &G.cam_jerks);
  cw_debug_i32("fx.booms", &G.booms);
  cw_debug_i32("floor.level", &G.floor_level);
  cw_debug_i32("floor.boss_tick", &G.floor_boss_tick);
  cw_debug_i32("floor.clear_tick", &G.floor_clear_tick);
  cw_debug_i32("floor.hatch_tick", &G.floor_hatch_tick);
  cw_debug_i32("floor.descend_tick", &G.floor_descend_tick);
  cw_debug_i32("floor.next_tick", &G.floor_next_tick);
  cw_debug_i32("floor.left_at_hatch", &G.floor_left_at_hatch);
  cw_debug_i32("dev.hold", &G.dev_hold);
  cw_debug_i32("dev.boss", &G.dev_boss);
  cw_debug_f32("dev.cam_dist", &g_cam_dist);
  cw_debug_i32("dev.cam_boss", &g_cam_boss);
  cw_debug_i32("melee.attack_frames", &G.m_attack_frames);
  cw_debug_i32("melee.strikes", &G.m_strikes);
  cw_debug_i32("melee.hits", &G.m_hits);
  cw_debug_field("melee.hit_delay", G.m_delay, CW_DBG_I32, EK_KINDS);
#ifdef HORDE_AUTOPILOT
#ifndef HORDE_AUTO_PLAYERS
#define HORDE_AUTO_PLAYERS 2
#endif
  cw_shell_start(getenv("HORDE_PLAYERS") ? CW_CLAMP(atoi(getenv("HORDE_PLAYERS")), 1, 4) : HORDE_AUTO_PLAYERS);
#endif
}

/* ── input ──────────────────────────────────────────────────────────── */


/* the aim stick through the engine's aim curve: a 15% dead zone, rescaled so the response starts at
 * zero, direction kept; a twin stick aims at the stick's angle, so there is no turn-rate cap */
static cw_vec2 aim_stick(cw_vec2 raw) {
  static cw_aim_state st;
  cw_aim_curve c = cw_aim_curve_default();
  c.power = 1, c.yaw_rate = c.pitch_rate = 1, c.ramp_seconds = 0;
  return cw_aim_step(&st, &c, raw, 1.0f, 0);
}

static void read_input(input in[MAXP]) {
  memset(in, 0, sizeof(input) * MAXP);
  if (G.m_test) { /* stand still; the dodge control steps straight away from an enemy winding up at player 0 */
    const player *p = &G.S.p[0];
    float ax = 0, ay = 0; /* away from every enemy winding up at player 0 at once */
    for (int i = 0; i < MAXE && G.m_test == 2; i++) {
      const enemy *e = &G.S.e[i];
      if (!e->alive || e->tgt != 0 || e->atk <= 0 || e->atk > sim_melee_windup(e->kind)) continue;
      float dx = p->x - e->x, dy = p->y - e->y, l = sqrtf(dx * dx + dy * dy);
      if (l > 0.01f) ax += dx / l, ay += dy / l;
    }
    float l = sqrtf(ax * ax + ay * ay);
    if (l > 0.01f) in[0].mx = ax / l, in[0].my = ay / l;
    else if (G.m_test == 2 && (ax != 0 || ay != 0)) in[0].mx = -ay, in[0].my = ax; /* pincered from both sides: step out sideways */
    return;
  }
  for (int i = 0; i < G.S.players; i++) {
#ifdef HORDE_AUTOPILOT
    horde_bot(&G.S, i, &in[i]);
    if (G.dev_hold) in[i].mx = in[i].my = 0;
    if (G.floor_stay && G.S.state == ST_HATCH) in[i].mx = in[i].my = 0; /* the floor gate's control: nobody steps in */
    if (G.dev_hold >= 2) in[i].ax = in[i].ay = 0, in[i].fire = in[i].bomb = false; /* 2: and hold fire */
#else
    cw_vec2 m = cw_input_vector(i, G.move[0], G.move[1], G.move[2], G.move[3], CW_DEADZONE_SCALED_RADIAL, 0.2f);
    cw_vec2 a = aim_stick(cw_input_vector(i, G.aim[0], G.aim[1], G.aim[2], G.aim[3], CW_DEADZONE_RADIAL, 0));
    in[i] = (input){ m.x, m.y, a.x, a.y, cw_action_pressed(i, G.fire), cw_action_just_pressed(i, G.bomb), 0 };
    for (int k = 0; k < 3; k++)
      if (cw_action_just_pressed(i, G.pick[k])) in[i].pick = k + 1;
    if (in[i].fire && cw_v2_length(a) < 0.25f) { /* fire toward the walking direction when not aiming */
      if (cw_v2_length(m) > 0.3f) in[i].ax = m.x, in[i].ay = m.y;
    }
#endif
  }
}

static void fixed_update(void *u, double dt) {
  (void)u, (void)dt;
  if (!cw_shell_update()) return;
  if (!G.in_match) return;
  input in[MAXP];
  read_input(in);
  int before_level[MAXP];
  for (int i = 0; i < MAXP; i++) before_level[i] = G.S.p[i].level;
  int booms = G.S.nboom_total, kills = G.S.killed_total;
  double t0 = cw_wall();
  if (getenv("HORDE_POWER") && G.S.tick % 600 == 0) /* keep the development power-up held */
    for (int i = 0; i < G.S.players; i++) sim_power_give(&G.S, i, atoi(getenv("HORDE_POWER")) % PU_KINDS);
  if (G.dev_boss && G.S.state == ST_PLAY) {
    int n = sim_level_waves(G.S.level);
    if (G.S.level_wave < n) G.S.level_wave = n, G.S.wave = sim_level_first_wave(G.S.level) + n - 1, G.S.wave_tick = 0;
    for (int i = 0; i < MAXE; i++)
      if (G.dev_boss <= 2 && G.S.e[i].alive && G.S.e[i].kind == EK_BOSS) G.S.e[i].hp = CW_MIN(G.S.e[i].hp, 60);
    if (G.dev_boss == 2 && G.S.boss_alive) sim_dev_kill_boss(&G.S), G.dev_boss = 0;
    if (G.dev_boss == 3 && G.S.boss_alive) G.dev_boss = 0; /* 3: only skip to the boss, at full strength (its patterns) */
  }
  int ev0 = G.S.ev_n;
  sim_step(&G.S, in);
  for (int i = 0; i < MAXE; i++) { /* the melee gate's bookkeeping: when each attack started */
    const enemy *e = &G.S.e[i];
    if (e->alive && e->atk == 1) G.m_start[i] = G.S.tick;
  }
  for (int k = ev0 <= G.S.ev_n ? ev0 : 0; k < G.S.ev_n; k++) { /* the floors: sounds, the fade, the gate's ticks */
    const event *v = &G.S.ev[k];
    if (v->kind == EV_CLEAR && !G.floor_clear_tick) G.floor_clear_tick = G.S.tick;
    if (v->kind == EV_HATCH) {
      play(snd_hatch, cw_shell_sfx_bus(), 0.9f);
      if (!G.floor_hatch_tick) G.floor_hatch_tick = G.S.tick, G.floor_left_at_hatch = G.S.nenemies;
    }
    if (v->kind == EV_DESCEND) {
      play(snd_descend, cw_shell_sfx_bus(), 0.8f);
      G.fade_t = 1;
      if (!G.floor_descend_tick) G.floor_descend_tick = G.S.tick;
    }
    if (v->kind == EV_FLOOR) {
      if (!G.floor_next_tick) G.floor_next_tick = G.S.tick;
      G.banner_t = 210, G.banner_kind = 3, G.banner_n = G.S.level;
    }
  }
  G.floor_level = G.S.level;
  if (G.S.state == ST_CLEAR && !G.floor_boss_tick) G.floor_boss_tick = G.S.tick; /* the boss fell: the melt began */
  if (G.fade_t > 0 && ++G.fade_t > DESCEND_TICKS + 45) G.fade_t = 0; /* down over the descent, up over the new floor's first moments */
  for (int k = ev0 <= G.S.ev_n ? ev0 : 0; k < G.S.ev_n; k++) {
    const event *v = &G.S.ev[k];
    if (v->kind != EV_SWIPE) continue;
    G.m_strikes++;
    if (getenv("HORDE_MELEE_LOG")) fprintf(stderr, "strike tick=%d kind=%d hit=%d at %.2f %.2f player %.2f %.2f hp %.0f\n", G.S.tick, v->a, (int)v->b, v->x, v->y, G.S.p[0].x, G.S.p[0].y, G.S.p[0].hp);
    if (v->b) G.m_hits++;
    for (int i = 0; i < MAXE; i++) { /* the striker: in its strike tick (one past its wind-up) */
      const enemy *e = &G.S.e[i];
      if (e->alive && e->kind == v->a && e->atk == sim_melee_windup(e->kind) + 1 && v->b && !G.m_delay[e->kind]) G.m_delay[e->kind] = G.S.tick - G.m_start[i];
    }
  }
  for (int k = ev0 <= G.S.ev_n ? ev0 : 0; k < G.S.ev_n; k++) /* the events this tick added */
    if (G.S.ev[k].kind == EV_POWER) play(snd_power, cw_shell_sfx_bus(), 0.7f);
  T_sim += (cw_wall() - t0) * 1000;
  G.ticks = G.S.tick;
  G.wave = G.S.wave;
  G.state = G.S.state;
  G.enemies = G.S.nenemies;
  G.kills = G.S.killed_total;
  if (getenv("HORDE_REPORT")) { /* development: the run's pacing as text, one line per event */
    static int last_kills, last_level[MAXP], last_hp[MAXP];
    if (!last_kills && G.kills) fprintf(stderr, "report first_kill tick=%d\n", G.S.tick);
    last_kills = G.kills;
    for (int i = 0; i < MAXP; i++) {
      if (G.S.p[i].level != last_level[i] && G.S.p[i].level > 1) fprintf(stderr, "report levelup tick=%d player=%d level=%d\n", G.S.tick, i, G.S.p[i].level);
      last_level[i] = G.S.p[i].level;
      if ((int)G.S.p[i].hp < last_hp[i]) fprintf(stderr, "report damage tick=%d player=%d lost=%d\n", G.S.tick, i, last_hp[i] - (int)G.S.p[i].hp);
      last_hp[i] = (int)G.S.p[i].hp;
    }
    if (G.S.tick % 1800 == 0) fprintf(stderr, "report minute tick=%d wave=%d enemies=%d kills=%d alive=%d state=%d boss_hp=%d\n", G.S.tick, G.S.wave, G.S.nenemies, G.S.killed_total, sim_alive_players(&G.S), G.S.state, G.S.boss_hp);
  }
  G.alive_players = sim_alive_players(&G.S);
  G.boss = G.S.boss_alive;
  G.hash = G.S.hash;
  for (int i = 0; i < MAXP; i++) {
    G.hp[i] = (int)G.S.p[i].hp, G.level[i] = G.S.p[i].level, G.score[i] = G.S.p[i].score, G.pkills[i] = G.S.p[i].kills;
    G.px[i] = (int)(G.S.p[i].x * 100), G.py[i] = (int)(G.S.p[i].y * 100);
    if (G.S.p[i].level != before_level[i]) G.level_flash[i] = 150, G.level_up[i] = G.S.p[i].last_up;
    if (G.level_flash[i] > 0) G.level_flash[i]--;
  }
  if (G.S.wave > G.best_wave) {
    G.best_wave = G.S.wave;
    ((save_data *)cw_shell_game_save())->best_wave = G.best_wave;
    cw_shell_save_commit();
  }
  if (G.S.nboom_total != booms) play(snd_boom, cw_shell_sfx_bus(), 0.8f);
  if (G.S.killed_total != kills && G.S.tick % 3 == 0) play(snd_kill, cw_shell_sfx_bus(), 0.4f);
  if (G.S.tick % 9 == 0 && G.S.nbullets >= 0)
    for (int i = 0; i < MAXP; i++)
      if (G.S.p[i].active && !G.S.p[i].down && G.S.p[i].cd > 0) { play(snd_shot, cw_shell_sfx_bus(), 0.25f); break; }
  if (G.S.state == ST_WON && G.S.state_ticks > 60 * 10) start(G.players, NULL); /* a new run */
  if (G.banner_t > 0) G.banner_t--;
  static int last_wave;
  if (G.S.wave != last_wave && G.S.level_wave > 1) { /* a floor's first wave has the floor's own banner */
    last_wave = G.S.wave;
    G.banner_t = 150;
    G.banner_kind = G.S.level_wave >= sim_level_waves(G.S.level) ? 2 : 1;
    G.banner_n = G.S.level_wave;
  }
}

/* ── frame ──────────────────────────────────────────────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_text_style st = { .size = size, .color = c, .outline_width = size * 0.08f, .outline_color = rgba(0, 0, 0, 0.9f) };
  if (al != CW_ALIGN_LEFT) {
    cw_text_metrics m = cw_font_measure(font, s, &st);
    x -= al == CW_ALIGN_CENTER ? m.width * 0.5f : m.width;
  }
  cw_r2d_text(font, s, cw_v2(x, y), &st);
}

static const float PC[MAXP][3] = { { 0.2f, 0.7f, 1 }, { 1, 0.55f, 0.1f }, { 0.4f, 0.95f, 0.35f }, { 0.95f, 0.3f, 0.75f } };

static void hud(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 540.0f;
  cw_r2d_begin(NULL);
  /* a panel per player in its own corner */
  for (int i = 0; i < G.S.players; i++) {
    const player *p = &G.S.p[i];
    float w = 190 * u, h = 52 * u, x = (i & 1) ? cs.x - w - 14 * u : 14 * u, y = (i & 2) ? cs.y - h - 14 * u : 14 * u;
    cw_r2d_rect(cw_rect_make(x, y, w, h), rgba(0.03f, 0.03f, 0.05f, 0.65f));
    cw_r2d_rect(cw_rect_make(x, y, 5 * u, h), rgba(PC[i][0], PC[i][1], PC[i][2], 1));
    char b[64];
    snprintf(b, sizeof b, "P%d  LV %d  %d", i + 1, p->level, p->score);
    text(b, x + 12 * u, y + 4 * u, 15 * u, rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
    float f = cw_clampf(p->hp / 100.0f, 0, 1);
    cw_r2d_rect(cw_rect_make(x + 12 * u, y + 26 * u, w - 22 * u, 9 * u), rgba(0.15f, 0.05f, 0.05f, 1));
    cw_r2d_rect(cw_rect_make(x + 12 * u, y + 26 * u, (w - 22 * u) * f, 9 * u), p->down ? rgba(0.5f, 0.5f, 0.5f, 1) : rgba(0.25f + 0.7f * (1 - f), 0.9f * f + 0.1f, 0.2f, 1));
    float xf = (float)p->xp / (float)p->xp_next;
    cw_r2d_rect(cw_rect_make(x + 12 * u, y + 40 * u, (w - 22 * u) * cw_clampf(xf, 0, 1), 4 * u), rgba(0.3f, 1, 0.8f, 1));
    if (p->offer_ticks > 0) {
      static const char *const KEYS[3] = { "X/1", "Y/2", "B/3" };
      float oy = (i & 2) ? y - 64 * u : y + h + 4 * u;
      cw_r2d_rect(cw_rect_make(x, oy, w, 60 * u), rgba(0.05f, 0.08f, 0.1f, 0.8f));
      for (int k = 0; k < 3; k++) {
        snprintf(b, sizeof b, "%s  %s", KEYS[k], sim_upgrade_name(p->offer[k]));
        text(b, x + 10 * u, oy + 3 * u + (float)k * 18 * u, 14 * u, k == 0 ? rgba(0.6f, 1, 0.85f, 1) : rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
      }
      cw_r2d_rect(cw_rect_make(x, oy + 58 * u, w * (float)p->offer_ticks / 480.0f, 2 * u), rgba(0.3f, 1, 0.8f, 1));
    }
    if (p->down) text("DOWN: a friend can pull you up", x + 12 * u, y + h + 2 * u, 12 * u, rgba(1, 0.6f, 0.5f, 1), CW_ALIGN_LEFT);
    else { /* power-ups held: name and seconds left, or charges */
      char pw[96];
      int n = 0;
      pw[0] = 0;
      if (p->pu_weapon_t > 0) n += snprintf(pw + n, sizeof pw - (size_t)n, "%s%s%s %d  ", p->pu_rarity > 0 ? sim_rarity_name(p->pu_rarity) : "", p->pu_rarity > 0 ? " " : "", sim_power_name(p->pu_weapon), (p->pu_weapon_t + 59) / 60);
      if (p->pu_ammo_t > 0) n += snprintf(pw + n, sizeof pw - (size_t)n, "%s %d  ", sim_power_name(p->pu_ammo), (p->pu_ammo_t + 59) / 60);
      if (p->saw_t > 0) n += snprintf(pw + n, sizeof pw - (size_t)n, "SAWS %d  ", (p->saw_t + 59) / 60);
      if (p->shield_t > 0) n += snprintf(pw + n, sizeof pw - (size_t)n, "SHIELD %d  ", (p->shield_t + 59) / 60);
      if (p->magnet_t > 0) n += snprintf(pw + n, sizeof pw - (size_t)n, "MAGNET %d  ", (p->magnet_t + 59) / 60);
      if (p->grenades > 0) n += snprintf(pw + n, sizeof pw - (size_t)n, "GRENADES x%d  ", p->grenades);
      if (p->mines > 0 && n < (int)sizeof pw - 16) n += snprintf(pw + n, sizeof pw - (size_t)n, "MINES x%d", p->mines);
      if (n) {
        float py = (i & 2) ? y - 18 * u : y + h + 2 * u;
        if (p->offer_ticks > 0) py = (i & 2) ? py - 66 * u : py + 62 * u;
        text(pw, x + 4 * u, py, 13 * u, rgba(1, 0.9f, 0.45f, 1), CW_ALIGN_LEFT);
      }
    }
    if (G.level_flash[i] > 0) {
      snprintf(b, sizeof b, "P%d: %s", i + 1, sim_upgrade_name(G.level_up[i]));
      /* by the player's own panel, not over the action */
      float ly = (i & 2) ? y - 40 * u : y + h + 20 * u;
      if (p->offer_ticks > 0) ly = (i & 2) ? ly - 66 * u : ly + 62 * u;
      text(b, x + 4 * u, ly, 16 * u, rgba(PC[i][0], PC[i][1], PC[i][2], cw_clampf(G.level_flash[i] / 40.0f, 0, 1)), CW_ALIGN_LEFT);
    }
  }
  for (int i = 0; i < G.S.players; i++) { /* P1..P4 over each soldier: who is who without the colours */
    const player *p = &G.S.p[i];
    float su, sv;
    if (!p->active || !view_screen((t3_vec3){ p->x, 2.3f, p->y }, &su, &sv)) continue;
    char t[8];
    snprintf(t, sizeof t, "P%d", i + 1);
    text(t, su * cs.x, sv * cs.y - 16 * u, 13 * u, rgba(0.55f + 0.45f * PC[i][0], 0.55f + 0.45f * PC[i][1], 0.55f + 0.45f * PC[i][2], 1), CW_ALIGN_CENTER);
  }
  char b[96];
  snprintf(b, sizeof b, "FLOOR %d  %s     WAVE %d / %d     %d kills     %d on the floor", G.S.level + 1, FLOOR_NAME[G.S.level], G.S.level_wave,
           sim_level_waves(G.S.level), G.S.killed_total, G.S.nenemies);
  text(b, cs.x * 0.5f, 10 * u, 16 * u, rgba(1, 0.95f, 0.8f, 1), CW_ALIGN_CENTER);
  if (G.banner_t > 0) {
    float a = cw_clampf(G.banner_t / 40.0f, 0, 1);
    if (G.banner_kind == 3) snprintf(b, sizeof b, "FLOOR %d  -  %s", G.banner_n + 1, FLOOR_NAME[CW_CLAMP(G.banner_n, 0, NLEVELS - 1)]);
    else snprintf(b, sizeof b, G.banner_kind == 2 ? "WAVE %d  -  BOSS INCOMING" : G.banner_kind == 1 ? "WAVE %d" : "HOLD THE YARD", G.banner_n);
    text(b, cs.x * 0.5f, cs.y * 0.28f, 38 * u, rgba(1, G.banner_kind == 2 ? 0.35f : 0.85f, 0.3f, a), CW_ALIGN_CENTER);
  }
  if (G.S.boss_alive && G.S.boss_maxhp > 0) {
    float bw = cs.x * 0.4f, bx = cs.x * 0.5f - bw * 0.5f, by = 34 * u;
    cw_r2d_rect(cw_rect_make(bx - 2 * u, by - 2 * u, bw + 4 * u, 12 * u), rgba(0, 0, 0, 0.7f));
    cw_r2d_rect(cw_rect_make(bx, by, bw * cw_clampf((float)G.S.boss_hp / (float)G.S.boss_maxhp, 0, 1), 8 * u), rgba(0.9f, 0.2f, 0.15f, 1));
    if (G.S.boss_phase == 1) text("THE BOSS IS ABOUT TO CHARGE", cs.x * 0.5f, by + 14 * u, 16 * u, rgba(1, 0.4f, 0.2f, 1), CW_ALIGN_CENTER);
  }
  if (G.S.state == ST_CLEAR || G.S.state == ST_HATCH) { /* the floor is won: the way down */
    if (G.banner_kind != 3) G.banner_t = 0; /* no wave banner over it */
    text(G.S.state == ST_CLEAR ? "THE FLOOR FALLS SILENT" : "THE WAY DOWN IS OPEN", cs.x * 0.5f, cs.y * 0.2f, 34 * u, rgba(1, 0.8f, 0.4f, 1), CW_ALIGN_CENTER);
    float su, sv; /* the marker: an arrow over the hatch, or at the screen's edge pointing to it */
    if (G.S.state == ST_HATCH && view_screen((t3_vec3){ 0, 3.2f + 0.3f * cw_sin((float)G.S.tick * 0.12f), 0 }, &su, &sv)) {
      float x = cw_clampf(su, 0.05f, 0.95f) * cs.x, y = cw_clampf(sv, 0.08f, 0.92f) * cs.y;
      cw_vec2 tri[3] = { cw_v2(x - 16 * u, y - 30 * u), cw_v2(x + 16 * u, y - 30 * u), cw_v2(x, y - 6 * u) }; /* an arrow down at the hatch */
      cw_r2d_polygon(tri, 3, rgba(1, 0.72f, 0.28f, 1));
      cw_r2d_polygon_lines(tri, 3, 2 * u, rgba(0, 0, 0, 0.8f));
      text("STEP IN", x, y - 58 * u, 18 * u, rgba(1, 0.85f, 0.5f, 1), CW_ALIGN_CENTER);
    }
  }
  if (G.fade_t > 0) { /* the descent: black down over the step, back up on the new floor */
    float t = (float)G.fade_t, a = t < DESCEND_TICKS ? t / (float)DESCEND_TICKS : 1.0f - (t - DESCEND_TICKS) / 45.0f;
    cw_r2d_rect(cw_rect_make(0, 0, cs.x, cs.y), rgba(0, 0, 0, cw_clampf(a, 0, 1)));
  }
  if (G.S.state == ST_WON) {
    text("THE DESCENT IS DONE", cs.x * 0.5f, cs.y * 0.36f, 56 * u, rgba(1, 0.85f, 0.35f, 1), CW_ALIGN_CENTER);
    snprintf(b, sizeof b, "%d kills in %d minutes  -  a new run in a moment", G.S.killed_total, G.S.tick / 3600);
    text(b, cs.x * 0.5f, cs.y * 0.5f, 18 * u, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
  if (G.S.state == ST_OVER) {
    text("OVERRUN", cs.x * 0.5f, cs.y * 0.4f, 52 * u, rgba(1, 0.3f, 0.25f, 1), CW_ALIGN_CENTER);
    snprintf(b, sizeof b, "the yard resets in a moment  -  best wave %d", G.best_wave);
    text(b, cs.x * 0.5f, cs.y * 0.52f, 18 * u, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
  cw_r2d_end();
}

static void render(void *u, double alpha) {
  (void)u, (void)alpha;
  cw_shell_screen sc = cw_shell_screen_now();
  g_fx_level = cw_gfx_get(CW_GFX_EFFECTS);
  if (G.built) {
    static float t;
    t += 1.0f / 60.0f;
    float sx, sy;
    fx_control_shake(&sx, &sy);
    int w, h;
    cw_window_size(&w, &h);
    view_camera(&G.S, G.camera, (float)w / (float)h, sx, sy); /* first: the horde is culled against this frame's view */
    {
      t3_vec3 p = cw_node3d_position(G.camera);
      if (G.cam_n >= 2) {
        float jx = p.x - 2 * G.cam_p[0].x + G.cam_p[1].x, jy = p.y - 2 * G.cam_p[0].y + G.cam_p[1].y, jz = p.z - 2 * G.cam_p[0].z + G.cam_p[1].z;
        if (jx * jx + jy * jy + jz * jz > 0.02f * 0.02f) G.cam_jerks++;
      }
      G.cam_p[1] = G.cam_p[0], G.cam_p[0] = p, G.cam_n++;
      G.booms = fx_get_stats().booms;
      if (view_attack_drawn() > 0) G.m_attack_frames++;
    }
    static int lm_frames;
    { /* the floor on screen: the sim's, or the yard for a floor not made yet */
      int want = G.S.fl[G.S.level].set ? G.S.level : 0;
      if (want != view_floor_now()) view_floor(want, want ? &LEVELS[want] : NULL), lm_frames = 0; /* its bake a frame later */
    }
    if (lm_frames >= 0 && ++lm_frames > 1) { /* after a frame: the sky has placed the sun, every node its transform */
      view_lightmap(cw_scene_app_tree());
      lm_frames = -1;
    }
    double t0 = cw_wall();
    { CW_PROFILE_SCOPE("horde.view_sync"); view_sync(&G.S, t); }
    T_view += (cw_wall() - t0) * 1000;
    t0 = cw_wall();
    { CW_PROFILE_SCOPE("horde.fx"); fx_events(&G.S);
    if (sc == CW_SHELL_PLAY) fx_tick(&G.S); }
    T_fx += (cw_wall() - t0) * 1000;
    if (cw_node_valid(G.sun)) { /* the sky sets the sun's direction: the blob shadows fall that way */
      const t3_mat4 *m = cw_node3d_global_transform(G.sun);
      if (m) view_set_sun((t3_vec3){ -m->e[8], -m->e[9], -m->e[10] });
    }
  }
  if (G.in_match && (sc == CW_SHELL_PLAY || sc == CW_SHELL_PAUSE)) { CW_PROFILE_SCOPE("horde.hud"); hud(); }
  cw_quality_auto_measure(sc == CW_SHELL_PLAY);
  double now = cw_wall();
  if (T_last > 0) T_frame += (now - T_last) * 1000;
  T_last = now;
  T_n++;
  if (getenv("HORDE_BENCH") && sc == CW_SHELL_PLAY) {
    static int n;
    if (++n % 120 == 0) {
      cw_render3d_stats rs;
      cw_render3d_get_stats(cw_scene_app_tree(), &rs);
      cw_particles_stats ps;
      cw_particles_get_stats(cw_scene_app_tree(), &ps);
      cw_decal_stats ds;
      cw_decals_get_stats(cw_scene_app_tree(), &ds);
      fx_stats fs = fx_get_stats();
      view_stats vs = view_get_stats();
      fprintf(stderr, "bench frame_ms=%.2f sim_ms=%.2f view_ms=%.2f fx_ms=%.2f tick=%d wave=%d enemies=%d drawn=%d crowd_ms=%.2f cpu3d_ms=%.2f draws=%d tris=%d psys=%d palive=%d psteps=%d pdraw=%d decals=%d/%d gibs=%d lights=%d booms=%d\n",
             T_frame / T_n, T_sim / T_n, T_view / T_n, T_fx / T_n, G.S.tick, G.S.wave, G.S.nenemies, vs.enemies_drawn, G.S.crowd_ms, rs.cpu_ms, rs.draw_calls, rs.triangles, ps.systems, ps.alive,
             ps.sim_steps, ps.draw_calls, ds.visible, ds.decals, fs.gibs_live, fs.lights_live, fs.booms);
      { /* the passes that cost most, GPU (CW_GPU_TIMERS=1) and CPU, averaged */
        const cw_pass_stat *ps;
        int np = cw_pipeline_pass_stats(cw_pipeline_of(cw_scene_app_tree()), &ps);
        fprintf(stderr, "bench_passes gpu_total=%.2f", cw_pipeline_gpu_ms(cw_pipeline_of(cw_scene_app_tree())));
        for (int k = 0; k < np; k++)
          if (ps[k].ran && (ps[k].gpu_ms_avg > 0.02f || ps[k].cpu_ms_avg > 0.3f)) fprintf(stderr, " %s=%.2f/%.2f", ps[k].name, ps[k].gpu_ms_avg, ps[k].cpu_ms_avg);
        fprintf(stderr, "\n");
      }
      { cw_crowd_stats cs = cw_crowd_get_stats(G.S.crowd); fprintf(stderr, "bench_crowd paths=%.3f neighbours=%.3f steer=%.3f move=%.3f step=%.3f\n", cs.phase_ms[0], cs.phase_ms[1], cs.phase_ms[2], cs.phase_ms[3], cs.step_ms); }
      fflush(stderr);
      T_sim = T_view = T_fx = T_frame = 0;
      T_n = 0;
    }
  }
  { CW_PROFILE_SCOPE("horde.shell_draw"); cw_shell_draw(); }
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .title = "Horde", .width = 1280, .height = 720, .debug = true, .init = init, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("scenes/arena.cwscene", &app);
}
