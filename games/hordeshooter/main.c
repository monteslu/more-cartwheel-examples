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
#include "cartwheel/prompt.h"
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
  cw_action move[4], aim[4], fire, bomb, pick[3], dash;
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
  int32_t dev_gun; /* development (romdev): t + 1 hands every player gun t (the power-up table order) and keeps it held */
  int32_t dev_rarity; /* with dev_gun: its rarity (0 common .. 4 legendary) */
  int32_t dev_hold; /* development (romdev, screenshots): 1 the bots stand where they are, 2 and hold their fire, so the horde reaches them */
  int32_t step_in_y0, step_in_y1, banner_y0, banner_y1;
  int32_t cards_shown;
  int32_t slow; /* the solo slow-down is on (the cards are up) */
  /* the score chase (render side, from the sim's events): each kill raises a player's multiplier a little, a hit drops it
   * back to x1; points come in at the multiplier. And for the run's summary: the best multiplier, what downed each, the guns held */
  float mult[MAXP], best_mult[MAXP];
  int32_t mscore[MAXP], mult100[MAXP], downs[MAXP];
  int last_score[MAXP], last_hurt[MAXP], downed_by[MAXP];
  uint32_t guns[MAXP]; /* frames an upgrade choice's cards were drawn (a player's each) */ /* the hatch marker's label and the floor-clear banner, rows on screen (the text check) */
  t3_vec3 cam_p[2];
  int cam_n;
} G;

static cw_font font;
static double T_sim, T_view, T_fx, T_frame, T_last; /* ms, averaged over the bench window */
static int T_n;
static cw_sound snd_dash, snd_boom, snd_shot, snd_kill, snd_gem, snd_move, snd_select, snd_back, snd_music, snd_power, snd_hatch, snd_descend;
/* the floors' names (LEVELS.md); the code reads only these until a floor's own scene lands */
static const char *const BOSS_NAME[NLEVELS] = { "KILNMAW, THE YARD TYRANT", "VHESSA, MOTHER OF THE DRAINS", "OSSIVAR, THE HOLLOW ABBOT", "ANVILGRAVE, THE FOUNDRY LORD",
                                                 "MOTHER MYCEL", "SERAPHEL, THE CHOIR BISHOP", "KHAR-VAAL, THE SHATTERED WARDEN", "THE UNMAKER" };
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

/* the guns' voices: each family its own synthesised shot, played per player as it fires (a fast gun every fourth tick
 * at most), pitch varied a little shot to shot; a kill sound for every kill, as long as one of three kill voices is
 * free (a crowd melting is a rattle, not a wall of noise) */
enum { GS_ROUND, GS_SHOTGUN, GS_MINIGUN, GS_RAIL, GS_ROCKET, GS_FLAME, GS_ARC, GS_N };
static cw_sound snd_gun[GS_N];
static cw_voice kill_voice[3];
static uint32_t snd_rng = 99;
static float snd_jitter(float k) { snd_rng = snd_rng * 1664525u + 1013904223u; return 1.0f + k * ((float)(snd_rng >> 9) / 8388608.0f - 1.0f); }
static int gun_family(const player *p, int ev_kind) {
  int w = p->pu_weapon_t > 0 ? p->pu_weapon : -1;
  if (ev_kind == EV_ROCKET || w == PU_ROCKETS || w == PU_MORTAR || w == PU_SEEKER) return GS_ROCKET;
  if (w == PU_SHOTGUN) return GS_SHOTGUN;
  if (w == PU_MINIGUN) return GS_MINIGUN;
  if (w == PU_RAIL || w == PU_STAKE || w == PU_FROST || w == PU_WHEEL) return GS_RAIL;
  if (w == PU_FLAME || w == PU_HELLFIRE) return GS_FLAME;
  if (w == PU_ARC) return GS_ARC;
  return GS_ROUND;
}
static void gun_sounds(int ev0, int kills) {
  static int last[MAXP];
  bool done[MAXP] = { 0 };
  for (int k = ev0 <= G.S.ev_n ? ev0 : 0; k < G.S.ev_n; k++) {
    const event *v = &G.S.ev[k];
    if ((v->kind != EV_SHOT && v->kind != EV_ROCKET) || v->a >= MAXP || done[v->a]) continue;
    const player *p = &G.S.p[v->a];
    int fam = gun_family(p, v->kind), gap = fam == GS_MINIGUN || fam == GS_FLAME ? 4 : 3;
    if (G.S.tick - last[v->a] < gap && G.S.tick >= last[v->a]) continue;
    done[v->a] = true, last[v->a] = G.S.tick;
    static const float VOL[GS_N] = { 0.24f, 0.4f, 0.2f, 0.34f, 0.36f, 0.16f, 0.3f };
    cw_audio_play(snd_gun[fam], &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = VOL[fam], .pitch = snd_jitter(0.06f) });
  }
  if (kills > 0)
    for (int k = 0; k < 3; k++)
      if (!cw_voice_is_playing(kill_voice[k])) {
        kill_voice[k] = cw_audio_play(snd_kill, &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = fminf(0.3f + 0.05f * (float)kills, 0.5f), .pitch = snd_jitter(0.12f) });
        break;
      }
}

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

/* The world, a piece at a time (true when it is built): the warm-up runs it under the loading screen, so Start finds it
 * ready and never sits still while it builds. */
static bool build_world_step(void) {
  if (G.built) return true;
  static bool view_done;
  cw_node root = cw_tree_root(cw_scene_app_tree());
  if (!view_done) {
    int done, total;
    view_done = view_init_step(root, &done, &total);
    cw_loading_set(done, total + 1, CW_LOAD_MESHES);
    return false;
  }
  fx_init(root);
  G.camera = cw_node_get(root, "Arena/Camera");
  G.sun = cw_node_get(root, "Arena/Sun");
  G.built = true;
  return true;
}
static void build_world(void) { /* an autopilot's start, before any warm-up: all of it now */
  while (!build_world_step()) {}
}

static int g_force_wave; /* the debug command "wave": where a cart, which reads no environment, starts over */
static void start(int players, void *u) {
  (void)u;
  build_world();
#ifndef HORDE_START_WAVE
#define HORDE_START_WAVE 1
#endif
  int wave = g_force_wave ? g_force_wave : getenv("HORDE_WAVE") ? atoi(getenv("HORDE_WAVE")) : HORDE_START_WAVE; /* hordeshooter_bench: a late wave from the start (carts read no environment) */
  sim_init(&G.S, players, 4242, wave);
  horde_floors_load(&G.S, LEVELS); /* each made floor's layout (the rest are played in the yard) */
  G.floor_stay = getenv("HORDE_FLOOR_STAY") && atoi(getenv("HORDE_FLOOR_STAY"));
  if (getenv("HORDE_BOSS_WEAK")) G.dev_boss = 1; /* the floor gates: the floor's boss wave, its boss weak (the bots fell it in seconds) */
  G.floor_level = G.S.level, G.floor_boss_tick = G.floor_clear_tick = G.floor_hatch_tick = G.floor_descend_tick = G.floor_next_tick = 0, G.floor_left_at_hatch = -1;
  G.m_test = getenv("HORDE_MELEE_TEST") ? (getenv("HORDE_MELEE_DODGE") && atoi(getenv("HORDE_MELEE_DODGE")) ? 1 + atoi(getenv("HORDE_MELEE_DODGE")) : 1) : 0;
  /* m_test: 1 stands still, 2 steps away from every wind-up, 3 dashes away in its last three ticks, 4 steps away then (the dash gate's control) */
  if (G.m_test) { /* the melee gate: one player standing still (or stepping away from each wind-up), a runner and a brute walking in */
    G.S.no_spawn = true, G.S.no_power = true;
    const player *p = &G.S.p[0];
    sim_spawn(&G.S, EK_RUNNER, p->x + 6.0f, p->y);
    sim_spawn(&G.S, EK_BRUTE, p->x - 7.0f, p->y + 1.0f);
  }
  G.players = players;
  G.in_match = true;
  G.ticks = 0;
  G.banner_t = 360, G.banner_kind = 0; /* six seconds: the controls line under it */
  if (getenv("HORDE_POWER_SHOW")) /* development (screenshots): one of each power-up in a ring round the team */
    for (int t = 0; t < PU_KINDS; t++) sim_power_drop(&G.S, cw_cos((float)t * 0.5236f) * 7.0f, cw_sin((float)t * 0.5236f) * 5.0f, t);
  if (getenv("HORDE_POWER")) /* development (screenshots, surveys): every player starts holding power-up n, refreshed as it runs out */
    for (int i = 0; i < players; i++) sim_power_give(&G.S, i, atoi(getenv("HORDE_POWER")) % PU_KINDS);
  memset(G.level_flash, 0, sizeof G.level_flash);
  for (int i = 0; i < MAXP; i++)
    G.mult[i] = G.best_mult[i] = 1, G.mscore[i] = 0, G.mult100[i] = 100, G.downs[i] = 0, G.last_score[i] = 0, G.last_hurt[i] = -1, G.downed_by[i] = -1, G.guns[i] = 0;
}

#ifdef HORDE_AUTOPILOT
static void cmd_wave(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: wave <n>"); return; }
  g_force_wave = atoi(argv[1]);
  start(G.players ? G.players : 2, NULL);
  cw_reply_printf(r, "{\"wave\":%d}", g_force_wave);
}
#endif

/* the app's warm-up: the world, then a simulation to draw it with (start replaces it), so the shader phase after this
 * compiles what the match draws */
static bool warmup(void *u) {
  (void)u;
  if (!build_world_step()) return false;
  if (!G.in_match) {
    sim_init(&G.S, 1, 4242, 1);
    horde_floors_load(&G.S, LEVELS);
  }
  return true;
}

static void init(void *u) {
  (void)u;
  cw_project_load_input();
  const char *names[4] = { "move_left", "move_right", "move_up", "move_down" }, *an[4] = { "aim_left", "aim_right", "aim_up", "aim_down" };
  for (int i = 0; i < 4; i++) G.move[i] = cw_action_find(names[i]), G.aim[i] = cw_action_find(an[i]);
  G.fire = cw_action_find("fire");
  G.bomb = cw_action_find("bomb");
  G.dash = cw_action_find("dash");
  G.pick[0] = cw_action_find("pick1"), G.pick[1] = cw_action_find("pick2"), G.pick[2] = cw_action_find("pick3");
  cw_shell_init(&(cw_shell_desc){ .title = "HORDE", .subtitle = "hold the yard together", .max_players = MAXP,
                                  .game_save_size = sizeof(save_data), .game_save_version = 1, .start = start,
                                  .sound = shell_sound, .screen_changed = shell_screen });
  font = cw_ui_font_sans();
  cw_quality_full_resolution(true); /* no preset and no Auto step draws the yard below the output size: no upscaling */
  if (getenv("HORDE_QUALITY")) cw_quality_set((cw_quality)CW_CLAMP(atoi(getenv("HORDE_QUALITY")), 0, CW_QUALITY_COUNT - 1), true); /* 0 Very Low .. 4 Ultra */
  snd_boom = synth(1.1f, 70, 28, 0.75f, 1.0f, 3.5f);
  snd_shot = synth(0.14f, 260, 55, 0.62f, 0.5f, 9); /* a low thump with a crack of noise */
  snd_gun[GS_ROUND] = snd_shot;
  snd_gun[GS_SHOTGUN] = synth(0.32f, 140, 38, 0.8f, 0.85f, 6.5f);  /* a deep boom and a spray of noise */
  snd_gun[GS_MINIGUN] = synth(0.07f, 420, 160, 0.55f, 0.45f, 14);  /* a short hard tick */
  snd_gun[GS_RAIL] = synth(0.4f, 2400, 180, 0.25f, 0.5f, 4.5f);    /* a charged zap falling away */
  snd_gun[GS_ROCKET] = synth(0.45f, 90, 300, 0.75f, 0.6f, 3.5f);   /* a whoosh rising as it leaves */
  snd_gun[GS_FLAME] = synth(0.18f, 120, 90, 0.95f, 0.5f, 2.0f);    /* a roar of noise */
  snd_gun[GS_ARC] = synth(0.2f, 1600, 900, 0.7f, 0.45f, 7);        /* a crackle */
  snd_kill = synth(0.12f, 200, 90, 0.7f, 0.4f, 5);
  snd_gem = synth(0.1f, 1200, 1800, 0, 0.25f, 4);
  snd_dash = synth(0.22f, 900, 260, 0.85f, 0.4f, 6); /* a dash: a short rush of air, falling */
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
  cw_debug_i32("hud.step_in_y0", &G.step_in_y0);
  cw_debug_field("ground.probe", g_ground_probe, CW_DBG_I32, GROUND_PROBES * 5);
  cw_debug_i32("ground.probes", &g_ground_probes);
  cw_debug_field("light.probe", g_light_probe, CW_DBG_I32, MAXP * 20);
  cw_debug_i32("sw.lamps_off", &g_lamps_off);
  if (getenv("HORDE_LAMPS_OFF")) g_lamps_off = atoi(getenv("HORDE_LAMPS_OFF"));
  cw_debug_i32("ground.deck_sunk", &g_deck_sunk);
  cw_debug_i32("ground.deck_on", &g_deck_on);
  cw_debug_i32("sw.contact_off", &g_contact_off);
  cw_debug_i32("sw.deck_off", &g_deck_off);
  if (getenv("HORDE_CONTACT_OFF")) g_contact_off = atoi(getenv("HORDE_CONTACT_OFF"));
  if (getenv("HORDE_DECK_OFF")) g_deck_off = atoi(getenv("HORDE_DECK_OFF"));
  if (getenv("HORDE_CROWD_OFF")) g_crowd_off = atoi(getenv("HORDE_CROWD_OFF"));
  g_bot_dash = getenv("HORDE_BOT_DASH") && atoi(getenv("HORDE_BOT_DASH"));
  if (getenv("HORDE_HOLD")) G.dev_hold = atoi(getenv("HORDE_HOLD")); /* dev.hold from the environment (screenshots, the grounding gate) */
  cw_debug_i32("hud.step_in_y1", &G.step_in_y1);
  cw_debug_i32("hud.banner_y0", &G.banner_y0);
  cw_debug_i32("hud.banner_y1", &G.banner_y1);
  cw_debug_i32("hud.cards_shown", &G.cards_shown);
  cw_debug_i32("game.slow", &G.slow);
  cw_debug_field("score.mult100", G.mult100, CW_DBG_I32, MAXP);
  cw_debug_field("score.points", G.mscore, CW_DBG_I32, MAXP);
  cw_debug_field("score.downs", G.downs, CW_DBG_I32, MAXP);
  cw_debug_i32("dev.gun", &G.dev_gun);
  cw_debug_i32("dev.rarity", &G.dev_rarity);
  cw_debug_i32("dev.boss", &G.dev_boss);
  cw_debug_f32("dev.cam_dist", &g_cam_dist);
  cw_debug_i32("dev.cam_boss", &g_cam_boss);
  cw_debug_i32("melee.attack_frames", &G.m_attack_frames);
  cw_debug_i32("melee.strikes", &G.m_strikes);
  cw_debug_i32("melee.hits", &G.m_hits);
  cw_debug_field("melee.hit_delay", G.m_delay, CW_DBG_I32, EK_KINDS);
#ifdef HORDE_AUTOPILOT
  cw_debug_command("wave", "wave <n>: start over at that wave of the run", cmd_wave);
  if (getenv("HORDE_CAM_DIST")) g_cam_dist = (float)atof(getenv("HORDE_CAM_DIST")); /* development (key art): dev.cam_dist from the environment */
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
    for (int i = 0; i < MAXE && G.m_test >= 2; i++) {
      const enemy *e = &G.S.e[i];
      if (!e->alive || e->tgt != 0 || e->atk <= 0 || e->atk > sim_melee_windup(e->kind)) continue;
      if (G.m_test >= 3 && e->atk < sim_melee_windup(e->kind) - 3) continue; /* late: the strike is three ticks away */
      float dx = p->x - e->x, dy = p->y - e->y, l = sqrtf(dx * dx + dy * dy);
      if (l > 0.01f) ax += dx / l, ay += dy / l;
    }
    float l = sqrtf(ax * ax + ay * ay);
    if (l > 0.01f) in[0].mx = ax / l, in[0].my = ay / l;
    else if (G.m_test >= 2 && (ax != 0 || ay != 0)) in[0].mx = -ay, in[0].my = ax; /* pincered from both sides: step out sideways */
    in[0].dash = G.m_test == 3 && (in[0].mx != 0 || in[0].my != 0);
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
    in[i] = (input){ m.x, m.y, a.x, a.y, cw_action_pressed(i, G.fire), cw_action_just_pressed(i, G.bomb), 0, cw_action_just_pressed(i, G.dash) };
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
  { /* alone, the yard slows to 30% while the level's cards are up (a choice to read, not a reflex); a pick goes through at once */
    static int acc;
    G.slow = G.S.players == 1 && G.S.p[0].active && G.S.p[0].offer_ticks > 0 && G.S.state == ST_PLAY && !G.m_test;
    if (G.slow && !in[0].pick) {
      acc += 3;
      if (acc < 10) return;
      acc -= 10;
    }
  }
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
      if (G.dev_boss <= 2 && G.S.e[i].alive && G.S.e[i].kind == EK_BOSS) G.S.e[i].hp = CW_MIN(G.S.e[i].hp, 60), G.S.boss_maxhp = 60; /* weak, but in its first phase */
    if (G.dev_boss == 2 && G.S.boss_alive) sim_dev_kill_boss(&G.S), G.dev_boss = 0;
    if (G.dev_boss == 3 && G.S.boss_alive) G.dev_boss = 0; /* 3: only skip to the boss, at full strength (its patterns) */
  }
  if (G.dev_gun > 0 && G.dev_gun <= PU_KINDS)
    for (int i = 0; i < G.S.players; i++)
      if (G.S.p[i].pu_weapon_t < 60 || G.S.p[i].pu_weapon != G.dev_gun - 1) sim_power_give(&G.S, i, G.dev_gun - 1), G.S.p[i].pu_rarity = CW_CLAMP(G.dev_rarity, 0, 4);
  if (getenv("HORDE_BOSS_LOG") && G.S.tick % 60 == 0) /* development: where the boss is */
    for (int i = 0; i < MAXE; i++)
      if (G.S.e[i].alive && G.S.e[i].kind == EK_BOSS) fprintf(stderr, "boss tick=%d at %.1f %.1f hp %d act %d atk %d; p0 %.1f %.1f\n", G.S.tick, G.S.e[i].x, G.S.e[i].y, G.S.e[i].hp, G.S.e[i].act, G.S.e[i].atk, G.S.p[0].x, G.S.p[0].y);
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
      G.dev_boss = 0; /* the skip (and HORDE_BOSS_WEAK) is for the floor it was asked on */
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
  for (int k = ev0 <= G.S.ev_n ? ev0 : 0; k < G.S.ev_n; k++) { /* the score chase and the run's record */
    const event *v = &G.S.ev[k];
    if (v->kind == EV_KILL && v->b < MAXP) G.mult[v->b] = fminf(5.0f, G.mult[v->b] + 0.02f), G.best_mult[v->b] = fmaxf(G.best_mult[v->b], G.mult[v->b]);
    else if (v->kind == EV_HURT && v->a < MAXP) G.mult[v->a] = 1, G.last_hurt[v->a] = v->b;
    else if (v->kind == EV_DOWN && v->a < MAXP) G.downed_by[v->a] = G.last_hurt[v->a], G.downs[v->a]++;
    else if (v->kind == EV_POWER && v->a < MAXP && pu_is_weapon(v->b)) G.guns[v->a] |= 1u << v->b;
    else if (v->kind == EV_BOSS) G.banner_t = 210, G.banner_kind = 4, G.banner_n = G.S.level; /* the boss's name, as it comes */
  }
  for (int i = 0; i < MAXP; i++) { /* points at the multiplier */
    int d = G.S.p[i].score - G.last_score[i];
    if (d > 0) G.mscore[i] += (int32_t)((float)d * G.mult[i] + 0.5f);
    G.last_score[i] = G.S.p[i].score;
    if (G.S.state == ST_OVER) G.mult[i] = 1;
    G.mult100[i] = (int32_t)(G.mult[i] * 100 + 0.5f);
  }
  for (int k = ev0 <= G.S.ev_n ? ev0 : 0; k < G.S.ev_n; k++) /* the events this tick added */
    if (G.S.ev[k].kind == EV_POWER) play(snd_power, cw_shell_sfx_bus(), 0.7f);
    else if (G.S.ev[k].kind == EV_DASH) play(snd_dash, cw_shell_sfx_bus(), 0.6f);
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
  gun_sounds(ev0, G.S.killed_total - kills);
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
static cw_text_style tstyle(float size, cw_color c) { return (cw_text_style){ .size = size, .color = c, .outline_width = size * 0.08f, .outline_color = rgba(0, 0, 0, 0.9f) }; }
/* the box a line takes at the player's text size (the HUD lays out from these, so it holds at 200%) */
static cw_text_metrics measure(const char *s, float size) {
  cw_text_style st = tstyle(size, rgba(1, 1, 1, 1));
  return cw_font_measure(font, s, &st);
}
static cw_text_metrics text(const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_text_style st = tstyle(size, c);
  if (al != CW_ALIGN_LEFT) {
    cw_text_metrics m = cw_font_measure(font, s, &st);
    x -= al == CW_ALIGN_CENTER ? m.width * 0.5f : m.width;
  }
  return cw_r2d_text(font, s, cw_v2(x, y), &st);
}
/* centred lines wrapped to a width */
static cw_text_metrics text_wrapped(const char *s, float cx, float y, float wrap, float size, cw_color c) {
  cw_text_style st = tstyle(size, c);
  st.align = CW_ALIGN_CENTER, st.wrap_width = wrap;
  return cw_r2d_text(font, s, cw_v2(cx - wrap * 0.5f, y), &st);
}
/* a size held to the body text floor: 26 px at 1080 lines (cartwheel/render2d.h "text size") */
static float body(float size) {
  float m = cw_r2d_canvas_size().y * CW_TEXT_FLOOR_BODY * 1.02f;
  return size > m ? size : m;
}
/* parts ("SHIELD 5") packed into lines no wider than maxw, two spaces between them on a line, '\n' between lines */
static void pack(char *out, size_t cap, const char *const *part, int n, const char *sep, float size, float maxw) {
  out[0] = 0;
  size_t line = 0; /* where the current line starts */
  for (int k = 0; k < n; k++) {
    size_t len = strlen(out);
    if (len > line) {
      char trial[256];
      snprintf(trial, sizeof trial, "%s%s%s", out + line, sep, part[k]);
      if (measure(trial, size).width <= maxw) { snprintf(out + len, cap - len, "%s%s", sep, part[k]); continue; }
      snprintf(out + len, cap - len, "\n"), line = len + 1, len++;
    }
    snprintf(out + len, cap - len, "%s", part[k]);
  }
}

static const float PC[MAXP][3] = { { 0.2f, 0.7f, 1 }, { 1, 0.55f, 0.1f }, { 0.4f, 0.95f, 0.35f }, { 0.95f, 0.3f, 0.75f } };

/* each upgrade's picture, drawn: a few strokes in a box of half size r round (cx, cy) */
static void upgrade_icon(int up, float cx, float cy, float r, cw_color c) {
  float t = r * 0.16f; /* stroke */
  cw_vec2 P[8];
  switch (up % 9) {
  case 0: /* rapid fire: three chevrons */
    for (int k = 0; k < 3; k++) {
      float x = cx - r * 0.7f + (float)k * r * 0.55f;
      cw_r2d_line(cw_v2(x, cy - r * 0.55f), cw_v2(x + r * 0.4f, cy), t, c), cw_r2d_line(cw_v2(x + r * 0.4f, cy), cw_v2(x, cy + r * 0.55f), t, c);
    }
    break;
  case 1: /* extra barrel: two barrels side by side, a round ahead of each */
    for (int k = -1; k <= 1; k += 2) {
      cw_r2d_rect(cw_rect_make(cx - r * 0.8f, cy + (float)k * r * 0.32f - t * 0.8f, r * 0.9f, t * 1.6f), c);
      cw_r2d_circle(cw_v2(cx + r * 0.55f, cy + (float)k * r * 0.32f), t * 1.3f, 12, c);
    }
    break;
  case 2: /* heavy rounds: one big round and a small one */
    cw_r2d_circle(cw_v2(cx - r * 0.15f, cy), r * 0.55f, 24, c), cw_r2d_circle(cw_v2(cx + r * 0.65f, cy), r * 0.2f, 12, c);
    break;
  case 3: /* piercing: an arrow through a wall */
    cw_r2d_rect(cw_rect_make(cx - t, cy - r * 0.8f, t * 2, r * 1.6f), c);
    cw_r2d_line(cw_v2(cx - r * 0.85f, cy), cw_v2(cx + r * 0.6f, cy), t, c);
    P[0] = cw_v2(cx + r * 0.9f, cy), P[1] = cw_v2(cx + r * 0.45f, cy - r * 0.3f), P[2] = cw_v2(cx + r * 0.45f, cy + r * 0.3f);
    cw_r2d_polygon(P, 3, c);
    break;
  case 4: /* rockets: body, nose, fins */
    cw_r2d_rect(cw_rect_make(cx - r * 0.6f, cy - r * 0.2f, r * 1.0f, r * 0.4f), c);
    P[0] = cw_v2(cx + r * 0.9f, cy), P[1] = cw_v2(cx + r * 0.4f, cy - r * 0.2f), P[2] = cw_v2(cx + r * 0.4f, cy + r * 0.2f);
    cw_r2d_polygon(P, 3, c);
    P[0] = cw_v2(cx - r * 0.6f, cy - r * 0.2f), P[1] = cw_v2(cx - r * 0.85f, cy - r * 0.55f), P[2] = cw_v2(cx - r * 0.3f, cy - r * 0.2f);
    cw_r2d_polygon(P, 3, c);
    P[0] = cw_v2(cx - r * 0.6f, cy + r * 0.2f), P[1] = cw_v2(cx - r * 0.85f, cy + r * 0.55f), P[2] = cw_v2(cx - r * 0.3f, cy + r * 0.2f);
    cw_r2d_polygon(P, 3, c);
    break;
  case 5: /* boots */
    P[0] = cw_v2(cx - r * 0.45f, cy - r * 0.8f), P[1] = cw_v2(cx + r * 0.05f, cy - r * 0.8f), P[2] = cw_v2(cx + r * 0.05f, cy + r * 0.2f), P[3] = cw_v2(cx + r * 0.75f, cy + r * 0.35f);
    P[4] = cw_v2(cx + r * 0.75f, cy + r * 0.75f), P[5] = cw_v2(cx - r * 0.45f, cy + r * 0.75f);
    cw_r2d_polygon(P, 6, c);
    break;
  case 6: /* magnet: a horseshoe */
    cw_r2d_rect(cw_rect_make(cx - r * 0.7f, cy - r * 0.7f, t * 2.4f, r * 1.0f), c), cw_r2d_rect(cw_rect_make(cx + r * 0.7f - t * 2.4f, cy - r * 0.7f, t * 2.4f, r * 1.0f), c);
    for (int k = 0; k < 8; k++) {
      float a0 = 3.14159f * (float)k / 8, a1 = 3.14159f * (float)(k + 1) / 8, rr = r * 0.7f - t * 1.2f;
      cw_r2d_line(cw_v2(cx + cw_cos(a0) * rr, cy + r * 0.3f + cw_sin(a0) * rr), cw_v2(cx + cw_cos(a1) * rr, cy + r * 0.3f + cw_sin(a1) * rr), t * 2.4f, c);
    }
    break;
  case 7: /* bigger blasts: a burst */
    for (int k = 0; k < 8; k++) {
      float a = 0.785f * (float)k, l = k & 1 ? 0.55f : 0.9f;
      cw_r2d_line(cw_v2(cx + cw_cos(a) * r * 0.3f, cy + cw_sin(a) * r * 0.3f), cw_v2(cx + cw_cos(a) * r * l, cy + cw_sin(a) * r * l), t, c);
    }
    cw_r2d_circle(cw_v2(cx, cy), r * 0.28f, 16, c);
    break;
  default: /* armor: a shield */
    P[0] = cw_v2(cx - r * 0.7f, cy - r * 0.75f), P[1] = cw_v2(cx + r * 0.7f, cy - r * 0.75f), P[2] = cw_v2(cx + r * 0.7f, cy), P[3] = cw_v2(cx, cy + r * 0.85f), P[4] = cw_v2(cx - r * 0.7f, cy);
    cw_r2d_polygon(P, 5, c);
    break;
  }
}
static const char *const UP_EFFECT[9] = { "shoots faster", "+1 round a shot", "+1 damage a round", "through +1 more", "a rocket every 4th shot",
                                          "runs 10% faster", "pulls gems from farther", "blasts reach farther", "-8% melee damage" };
/* the choice as three cards from (x, y) (the corner side: x the panel's outer edge, y its edge toward the middle), each its
 * picture, name, what it does and its level so far as dots, the button's glyph on its corner; the time left under them */
static void upgrade_cards(int pi, const player *p, float x, float y, bool right, bool bottom, float u, float *ow, float *oh) {
  const float cwid = 116 * u, gap = 6 * u, pad = 5 * u;
  float ns = body(14 * u), es = body(12 * u), icon = 15 * u, dot = 3.2f * u, glyph = 17 * u;
  float h = 0;
  for (int k = 0; k < 3; k++) { /* the tallest card sets the row's height */
    cw_text_style st = tstyle(es, rgba(1, 1, 1, 1));
    st.wrap_width = cwid - 2 * pad;
    float eh = cw_font_measure(font, UP_EFFECT[p->offer[k] % 9], &st).height;
    st.size = ns;
    float nh = cw_font_measure(font, sim_upgrade_name(p->offer[k]), &st).height;
    h = fmaxf(h, pad + glyph * 0.4f + icon * 2 + 3 * u + nh + eh + 4 * u + dot * 2 + pad);
  }
  float rw = 3 * cwid + 2 * gap, x0 = right ? x - rw : x, y0 = bottom ? y - h - 4 * u : y;
  for (int k = 0; k < 3; k++) {
    int up = p->offer[k] % 9, lv = sim_upgrade_level(p, up), mx = sim_upgrade_max(up);
    float cx = x0 + (float)k * (cwid + gap), cy = y0;
    cw_r2d_rect(cw_rect_make(cx, cy, cwid, h), rgba(0.04f, 0.06f, 0.09f, 0.86f));
    cw_r2d_rect(cw_rect_make(cx, cy, cwid, 3 * u), rgba(PC[pi][0], PC[pi][1], PC[pi][2], 1));
    float yy = cy + pad + glyph * 0.4f;
    upgrade_icon(up, cx + cwid * 0.5f, yy + icon, icon, rgba(0.62f, 1, 0.86f, 1));
    yy += icon * 2 + 3 * u;
    yy += text_wrapped(sim_upgrade_name(up), cx + cwid * 0.5f, yy, cwid - 2 * pad, ns, rgba(1, 1, 1, 1)).height;
    yy += text_wrapped(UP_EFFECT[up], cx + cwid * 0.5f, yy, cwid - 2 * pad, es, rgba(0.82f, 0.9f, 0.95f, 1)).height + 4 * u;
    float dw = (float)mx * dot * 2.6f, dx = cx + (cwid - dw) * 0.5f + dot * 1.3f;
    for (int d = 0; d < mx; d++) /* filled: the levels held; ringed: the next one this card gives; dim: the rest */
      if (d < lv) cw_r2d_circle(cw_v2(dx + (float)d * dot * 2.6f, cy + h - pad - dot), dot, 12, rgba(0.62f, 1, 0.86f, 1));
      else cw_r2d_circle_lines(cw_v2(dx + (float)d * dot * 2.6f, cy + h - pad - dot), dot, d == lv ? 1.6f * u : 0.8f * u, 12, d == lv ? rgba(1, 1, 1, 1) : rgba(1, 1, 1, 0.35f));
    cw_prompt_action(G.pick[k], pi, cw_v2(cx + 3 * u, cy + 3 * u), glyph, rgba(0, 0, 0, 0)); /* west, north, east: left to right */
  }
  float left = (float)p->offer_ticks / 480.0f;
  cw_r2d_rect(cw_rect_make(x0, bottom ? y0 - 4 * u : y0 + h + 1 * u, rw * cw_clampf(left, 0, 1), 3 * u), rgba(0.3f, 1, 0.8f, 1));
  *ow = rw, *oh = h + 4 * u;
}

static void hud(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 540.0f, cx = cs.x * 0.5f;
  cw_r2d_begin(NULL);
  /* what the top corners hold (panels and the lines under them): the centred lines keep clear of them */
  float left_r = 0, left_b = 0, right_l = cs.x, right_b = 0, panel_l = 0, panel_r = cs.x;
#ifdef HORDE_AUTOPILOT
  static int show = -1; /* HORDE_HUD_SHOW (development, the text size contact sheet): every optional panel line at once, drawn only */
  if (show < 0) show = getenv("HORDE_HUD_SHOW") != NULL;
#else
  const int show = 0;
#endif
  /* a panel per player in its own corner, sized from its measured text; its other lines stack away from the corner */
  for (int i = 0; i < G.S.players; i++) {
    player pv = G.S.p[i];
    int flash = G.level_flash[i], up = G.level_up[i];
    if (show) {
      if (i == 0 || i == 3) pv.offer_ticks = 300, pv.offer[0] = 1, pv.offer[1] = 4, pv.offer[2] = 7;
      if (i == 1) pv.down = true;
      if (i != 1) flash = 100, up = 2, pv.shield_t = 300, pv.grenades = 3, pv.mines = 2;
    }
    const player *p = &pv;
    bool right = i & 1, bottom = i & 2;
    char b[64];
    snprintf(b, sizeof b, "P%d  LV %d  %d  x%.1f", i + 1, p->level, G.mscore[i], (double)G.mult[i]);
    float ts = 15 * u;
    cw_text_metrics tm = measure(b, ts);
    float w = fmaxf(190 * u, 22 * u + tm.width), bar = fmaxf(26 * u, 4 * u + tm.height + 3 * u), h = bar + 26 * u;
    float x = right ? cs.x - w - 14 * u : 14 * u, y = bottom ? cs.y - h - 14 * u : 14 * u;
    float edge = right ? x + w - 4 * u : x + 4 * u; /* the lines beside the panel line up with its outer side */
    cw_text_align al = right ? CW_ALIGN_RIGHT : CW_ALIGN_LEFT;
    cw_r2d_rect(cw_rect_make(x, y, w, h), rgba(0.03f, 0.03f, 0.05f, 0.65f));
    cw_r2d_rect(cw_rect_make(x, y, 5 * u, h), rgba(PC[i][0], PC[i][1], PC[i][2], 1));
    text(b, x + 12 * u, y + 4 * u, ts, rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
    float f = cw_clampf(p->hp / 100.0f, 0, 1);
    cw_r2d_rect(cw_rect_make(x + 12 * u, y + bar, w - 22 * u, 9 * u), rgba(0.15f, 0.05f, 0.05f, 1));
    cw_r2d_rect(cw_rect_make(x + 12 * u, y + bar, (w - 22 * u) * f, 9 * u), p->down ? rgba(0.5f, 0.5f, 0.5f, 1) : rgba(0.25f + 0.7f * (1 - f), 0.9f * f + 0.1f, 0.2f, 1));
    float xf = (float)p->xp / (float)p->xp_next;
    cw_r2d_rect(cw_rect_make(x + 12 * u, y + bar + 14 * u, (w - 22 * u) * cw_clampf(xf, 0, 1), 4 * u), rgba(0.3f, 1, 0.8f, 1));
    float cur = bottom ? y - 2 * u : y + h + 2 * u, wide = w, maxw = cs.x * 0.5f - 24 * u; /* cur: the next line's edge nearest the panel */
    if (p->offer_ticks > 0) { /* the level's choice: three cards in this player's corner, laid out as the buttons sit (west, north, east) */
      float ow, oh;
      upgrade_cards(i, p, right ? x + w : x, bottom ? cur - 2 * u : cur + 2 * u, right, bottom, u, &ow, &oh);
      cur = bottom ? cur - oh - 4 * u : cur + oh + 4 * u, wide = fmaxf(wide, ow);
      G.cards_shown++;
    }
    if (p->down && G.S.players > 1 && G.S.state != ST_OVER) { /* alone, a fall is the end of the try: OVERRUN says it, no friend is coming */
      const char *dt = "DOWN: a friend can pull you up";
      float ds = body(12 * u);
      cw_text_metrics dm = measure(dt, ds);
      float dy = bottom ? cur - dm.height : cur;
      text(dt, right ? edge : x + 12 * u, dy, ds, rgba(1, 0.6f, 0.5f, 1), al);
      cur = bottom ? dy : dy + dm.height, wide = fmaxf(wide, dm.width + 12 * u);
    } else if (!p->down) { /* power-ups held: name and seconds left, or charges, packed into lines that keep to this half of the screen */
      char part[7][48];
      const char *pp[7];
      int n = 0;
      if (p->pu_weapon_t > 0) snprintf(part[n++], sizeof part[0], "%s%s%s %d", p->pu_rarity > 0 ? sim_rarity_name(p->pu_rarity) : "", p->pu_rarity > 0 ? " " : "", sim_power_name(p->pu_weapon), (p->pu_weapon_t + 59) / 60);
      if (p->pu_ammo_t > 0) snprintf(part[n++], sizeof part[0], "%s %d", sim_power_name(p->pu_ammo), (p->pu_ammo_t + 59) / 60);
      if (p->saw_t > 0) snprintf(part[n++], sizeof part[0], "SAWS %d", (p->saw_t + 59) / 60);
      if (p->shield_t > 0) snprintf(part[n++], sizeof part[0], "SHIELD %d", (p->shield_t + 59) / 60);
      if (p->magnet_t > 0) snprintf(part[n++], sizeof part[0], "MAGNET %d", (p->magnet_t + 59) / 60);
      if (p->grenades > 0) snprintf(part[n++], sizeof part[0], "GRENADES x%d", p->grenades);
      if (p->mines > 0) snprintf(part[n++], sizeof part[0], "MINES x%d", p->mines);
      if (n) {
        float ps = body(13 * u);
        char pw[400];
        for (int k = 0; k < n; k++) pp[k] = part[k];
        pack(pw, sizeof pw, pp, n, "  ", ps, maxw);
        cw_text_style st = tstyle(ps, rgba(1, 0.9f, 0.45f, 1));
        st.align = al;
        cw_text_metrics pm = cw_font_measure(font, pw, &st);
        float py = bottom ? cur - pm.height : cur;
        cw_r2d_text(font, pw, cw_v2(right ? edge - pm.width : edge, py), &(cw_text_style){ .size = ps, .color = st.color, .outline_width = st.outline_width, .outline_color = st.outline_color, .align = al, .wrap_width = pm.width + 1 });
        cur = bottom ? py : py + pm.height, wide = fmaxf(wide, pm.width + 4 * u);
      }
    }
    if (flash > 0) { /* the level's upgrade, by the player's own panel, not over the action */
      snprintf(b, sizeof b, "P%d: %s", i + 1, sim_upgrade_name(up));
      float ls = 16 * u;
      cw_text_metrics lm = measure(b, ls);
      float ly = bottom ? cur - lm.height : cur;
      text(b, edge, ly, ls, rgba(PC[i][0], PC[i][1], PC[i][2], cw_clampf(flash / 40.0f, 0, 1)), al);
      cur = bottom ? ly : ly + lm.height, wide = fmaxf(wide, lm.width + 4 * u);
    }
    if (!bottom) {
      if (right) right_l = fminf(right_l, x + w - wide), right_b = fmaxf(right_b, cur), panel_r = fminf(panel_r, x);
      else left_r = fmaxf(left_r, x + wide), left_b = fmaxf(left_b, cur), panel_l = fmaxf(panel_l, x + w);
    }
  }
  for (int i = 0; i < G.S.players; i++) { /* P1..P4 over each soldier: who is who without the colours */
    const player *p = &G.S.p[i];
    float su, sv;
    if (!p->active || !view_screen((t3_vec3){ p->x, 2.3f, p->y }, &su, &sv)) continue;
    char t[8];
    snprintf(t, sizeof t, "P%d", i + 1);
    float ps = body(13 * u);
    text(t, su * cs.x, sv * cs.y - 3 * u - measure(t, ps).height, ps, rgba(0.55f + 0.45f * PC[i][0], 0.55f + 0.45f * PC[i][1], 0.55f + 0.45f * PC[i][2], 1), CW_ALIGN_CENTER);
  }
  /* the top line between the top panels: its parts packed into as many lines as that room needs at this text size */
  char b[160], tp[4][48];
  snprintf(tp[0], sizeof tp[0], "FLOOR %d  %s", G.S.level + 1, FLOOR_NAME[G.S.level]);
  snprintf(tp[1], sizeof tp[1], "WAVE %d / %d", G.S.level_wave, sim_level_waves(G.S.level));
  snprintf(tp[2], sizeof tp[2], "%d kills", G.S.killed_total);
  snprintf(tp[3], sizeof tp[3], "%d on the floor", G.S.nenemies);
  const char *tpp[4] = { tp[0], tp[1], tp[2], tp[3] };
  float top_s = 16 * u, room = 2 * fminf(cx - panel_l, panel_r - cx) - 16 * u;
  char top[200];
  pack(top, sizeof top, tpp, 4, "     ", top_s, fmaxf(room, cs.x * 0.2f));
  float top_b = 10 * u + text_wrapped(top, cx, 10 * u, cs.x, top_s, rgba(1, 0.95f, 0.8f, 1)).height;
  /* a centred line of width w wanted at y: lower, under a top corner's lines it would run into */
#define CLEAR_Y(y, w) (fmaxf(fmaxf((y), cx - (w) * 0.5f < left_r ? left_b + 4 * u : 0), cx + (w) * 0.5f > right_l ? right_b + 4 * u : 0))
  float mid = top_b; /* the bottom of what is centred at the top */
  if (G.S.boss_alive && G.S.boss_maxhp > 0) {
    float bw = cs.x * 0.4f, bx = cx - bw * 0.5f, by = fmaxf(34 * u, top_b + 6 * u);
    cw_r2d_rect(cw_rect_make(bx - 2 * u, by - 2 * u, bw + 4 * u, 12 * u), rgba(0, 0, 0, 0.7f));
    cw_r2d_rect(cw_rect_make(bx, by, bw * cw_clampf((float)G.S.boss_hp / (float)G.S.boss_maxhp, 0, 1), 8 * u), rgba(0.9f, 0.2f, 0.15f, 1));
    mid = by + 10 * u;
    { /* its name under the bar */
      const char *bn = BOSS_NAME[CW_CLAMP(G.S.level, 0, NLEVELS - 1)];
      float ns = body(12 * u), ny = CLEAR_Y(mid + 2 * u, fminf(measure(bn, ns).width, cs.x * 0.94f));
      mid = ny + text_wrapped(bn, cx, ny, cs.x * 0.94f, ns, rgba(1, 0.75f, 0.65f, 1)).height;
    }
    if (G.S.boss_phase == 1) {
      const char *bt = "THE BOSS IS ABOUT TO CHARGE";
      float bs = 16 * u, ty = CLEAR_Y(mid + 4 * u, measure(bt, bs).width);
      mid = ty + text(bt, cx, ty, bs, rgba(1, 0.4f, 0.2f, 1), CW_ALIGN_CENTER).height;
    }
  }
  if (G.banner_t > 0 && cw_shell_hud_shown()) { /* not under the pause menu's lines */
    float a = cw_clampf(G.banner_t / 40.0f, 0, 1);
    if (G.banner_kind == 3) snprintf(b, sizeof b, "FLOOR %d  -  %s", G.banner_n + 1, FLOOR_NAME[CW_CLAMP(G.banner_n, 0, NLEVELS - 1)]);
    else if (G.banner_kind == 4) snprintf(b, sizeof b, "%s", BOSS_NAME[CW_CLAMP(G.banner_n, 0, NLEVELS - 1)]);
    else snprintf(b, sizeof b, G.banner_kind == 2 ? "WAVE %d  -  BOSS INCOMING" : G.banner_kind == 1 ? "WAVE %d" : "HOLD THE YARD", G.banner_n);
    float bs = 38 * u, by = CLEAR_Y(fmaxf(cs.y * 0.28f, mid + 6 * u), fminf(measure(b, bs).width, cs.x * 0.94f));
    float bh = text_wrapped(b, cx, by, cs.x * 0.94f, bs, rgba(1, G.banner_kind == 2 || G.banner_kind == 4 ? 0.35f : 0.85f, 0.3f, a)).height;
    if (G.banner_kind == 0) /* the run's first moments: what the buttons do (the dash above all: the way out of a strike) */
      cw_prompt_text(font, "{fire} shoot     {dash} dash     {bomb} blast", -1, cw_v2(cx, by + bh + 4 * u), body(15 * u), rgba(1, 0.95f, 0.85f, a), CW_ALIGN_CENTER);
  }
  if (G.S.state == ST_CLEAR || G.S.state == ST_HATCH) { /* the floor is won: the way down */
    if (G.banner_kind != 3) G.banner_t = 0; /* no wave banner over it */
    const char *ct = G.S.state == ST_CLEAR ? "THE FLOOR FALLS SILENT" : "THE WAY DOWN IS OPEN";
    float s = 34 * u, y = CLEAR_Y(fmaxf(cs.y * 0.2f, mid + 6 * u), fminf(measure(ct, s).width, cs.x * 0.94f));
    float bw = fminf(measure(ct, s).width, cs.x * 0.94f), bh = text_wrapped(ct, cx, y, cs.x * 0.94f, s, rgba(1, 0.8f, 0.4f, 1)).height;
    float su, sv; /* the marker: an arrow over the hatch pointing down at it, its label over the arrow */
    float bob = 0.3f * cw_sin((float)G.S.tick * 0.12f);
    if (G.S.state == ST_HATCH && view_screen((t3_vec3){ 0, 3.2f + bob, 0 }, &su, &sv)) {
      float ss = 18 * u, lw = measure("STEP IN", ss).width, lh = measure("STEP IN", ss).height;
      float x = cw_clampf(su, 0.05f, 0.95f) * cs.x, ty = cw_clampf(sv, 0.08f, 0.92f) * cs.y, dir = 1; /* dir 1: the arrow points down */
      /* where the label would sit over the banner (or off the top), the arrow comes up at the hatch from under it instead */
      bool clash = ty - 34 * u - lh < y + bh + 4 * u && ty - 6 * u > y - 4 * u && fabsf(x - cx) < bw * 0.5f + lw * 0.5f + 8 * u;
      if ((clash || ty - 34 * u - lh < 4 * u) && view_screen((t3_vec3){ 0, 0, 2.0f }, &su, &sv)) /* the hatch's near rim */
        x = cw_clampf(su, 0.05f, 0.95f) * cs.x, ty = cw_clampf(sv, 0.08f, 0.92f) * cs.y + (6 + 8 * bob) * u, dir = -1;
      cw_vec2 tri[3] = { cw_v2(x - 16 * u, ty - dir * 30 * u), cw_v2(x + 16 * u, ty - dir * 30 * u), cw_v2(x, ty - dir * 6 * u) };
      cw_r2d_polygon(tri, 3, rgba(1, 0.72f, 0.28f, 1));
      cw_r2d_polygon_lines(tri, 3, 2 * u, rgba(0, 0, 0, 0.8f));
      text("STEP IN", x, dir > 0 ? ty - 34 * u - lh : ty + 34 * u, ss, rgba(1, 0.85f, 0.5f, 1), CW_ALIGN_CENTER);
      G.step_in_y0 = (int32_t)(dir > 0 ? ty - 34 * u - lh : ty + 34 * u), G.step_in_y1 = G.step_in_y0 + (int32_t)lh; /* the text check */
      G.banner_y0 = (int32_t)y, G.banner_y1 = (int32_t)(y + bh);
    }
  }
#undef CLEAR_Y
  if (G.fade_t > 0) { /* the descent: black down over the step, back up on the new floor */
    float t = (float)G.fade_t, a = t < DESCEND_TICKS ? t / (float)DESCEND_TICKS : 1.0f - (t - DESCEND_TICKS) / 45.0f;
    cw_r2d_rect(cw_rect_make(0, 0, cs.x, cs.y), rgba(0, 0, 0, cw_clampf(a, 0, 1)));
  }
  if (G.S.state == ST_WON || G.S.state == ST_OVER) { /* the title, and under it as measured the line, wrapped to the screen */
    bool won = G.S.state == ST_WON;
    cw_r2d_rect(cw_rect_make(cs.x * 0.04f, cs.y * (won ? 0.33f : 0.37f), cs.x * 0.92f, cs.y * 0.3f), rgba(0.02f, 0.02f, 0.03f, 0.62f)); /* the lines read over any crowd */
    float y = cs.y * (won ? 0.36f : 0.4f), s2 = 18 * u;
    y += text_wrapped(won ? "THE DESCENT IS DONE" : "OVERRUN", cx, y, cs.x * 0.94f, won ? 56 * u : 52 * u, won ? rgba(1, 0.85f, 0.35f, 1) : rgba(1, 0.3f, 0.25f, 1)).height;
    if (won) snprintf(b, sizeof b, "%d kills in %d minutes  -  a new run in a moment", G.S.killed_total, G.S.tick / 3600);
    else { /* the floor the team fell on, a wave back (never before its first), and the deepest the save has seen */
      int bl = 0;
      while (bl < NLEVELS - 1 && G.best_wave >= sim_level_first_wave(bl + 1)) bl++;
      snprintf(b, sizeof b, "%s again from wave %d in a moment  -  deepest: floor %d, wave %d", FLOOR_NAME[CW_CLAMP(G.S.level, 0, NLEVELS - 1)],
               G.S.level_wave > 1 ? G.S.level_wave - 1 : 1, bl + 1, G.best_wave - sim_level_first_wave(bl) + 1);
    }
    float sy = fmaxf(y + 4 * u, cs.y * (won ? 0.5f : 0.52f));
    sy += text_wrapped(b, cx, sy, cs.x * 0.9f, s2, rgba(1, 1, 1, 1)).height + 8 * u;
    for (int i = 0; i < G.S.players; i++) { /* each player's line: what took them down (or the run's record when it is won) */
      char l[200], guns[120] = "";
      if (won) {
        for (int t = 0, n = 0; t < PU_KINDS; t++)
          if (G.guns[i] & (1u << t)) snprintf(guns + strlen(guns), sizeof guns - strlen(guns), "%s%s", n++ ? ", " : "  -  ", sim_power_name(t));
        snprintf(l, sizeof l, "P%d  %d points  -  %d kills  -  best x%.1f%s", i + 1, G.mscore[i], G.S.p[i].kills, (double)G.best_mult[i], guns);
      } else {
        int k = G.downed_by[i];
        const char *by = k == EK_BOSS ? BOSS_NAME[CW_CLAMP(G.S.level, 0, NLEVELS - 1)] : k >= 0 && k < EK_KINDS ? sim_kind_name(k) : "a blast or the fire underfoot";
        snprintf(l, sizeof l, "P%d  taken down by %s%s  -  best x%.1f  -  %d kills", i + 1, k >= 0 && k < EK_KINDS && k != EK_BOSS ? "a " : "", by, (double)G.best_mult[i], G.S.p[i].kills);
      }
      sy += text_wrapped(l, cx, sy, cs.x * 0.9f, body(13 * u), rgba(0.55f + 0.45f * PC[i][0], 0.55f + 0.45f * PC[i][1], 0.55f + 0.45f * PC[i][2], 1)).height + 2 * u;
    }
  }
  cw_r2d_end();
}

static void render(void *u, double alpha) {
  (void)u, (void)alpha;
  cw_shell_screen sc = cw_shell_screen_now();
  g_fx_level = CW_MAX(0, cw_gfx_effective(CW_GFX_EFFECTS) - 1); /* Low .. Ultra as 0..3; Very Low as Low */
  if (G.built) {
    static float t;
    t += (G.slow ? 0.3f : 1.0f) / 60.0f; /* the crowd's steps slow with the yard */
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
  bool hud_on = true;
#ifdef HORDE_AUTOPILOT
  hud_on = !getenv("HORDE_NOHUD"); /* development (key art): the play view with no HUD */
#endif
  if (hud_on && G.in_match && cw_shell_hud_shown()) { CW_PROFILE_SCOPE("horde.hud"); hud(); }
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
  static cw_app_desc app = { .title = "Horde", .width = 1280, .height = 720, .debug = true, .init = init, .warmup = warmup, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("scenes/arena.cwscene", &app);
}
