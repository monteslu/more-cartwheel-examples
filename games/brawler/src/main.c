/* Brawler: a 1 to 4 player co-op street brawler on one shared scrolling screen, on cartwheel and three.c.
 *
 *   move      stick / WASD / arrows (x scrolls the stage, up and down walk the lane)
 *   attack    X / J: a three hit combo; with a weapon, a swing or a stab
 *   kick      Y / K: a heavy kick that knocks down
 *   jump      A / Space; attack in the air is a flying kick
 *   grab      right bumper / L: seize a body in front, then attack to knee it (three knees throws),
 *             or hold the stick away and press grab / kick to throw it over your shoulder
 *   special   left bumper / I: a spinning clear-out that costs a little health
 * Four stages, a mini boss and a boss each, weapons and food from breakable props.
 *
 * Determinism: the whole match runs in fixed_update from the input snapshot and cw_rng; brawler_auto
 * has bots play it (no input needed). Debug fields: game.* (see init). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/platform.h"
#include "cartwheel/project.h"
#include "cartwheel/render2d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "game.h"

static cw_action a_left, a_right, a_up, a_down, a_atk, a_kick, a_jump, a_grab, a_special;
static cw_sound snd_move, snd_select, snd_back, snd_music[4];
static cw_voice music;
static int music_stage = -1;
static int32_t d_leak, d_ticks, d_players, d_stage, d_phase, d_enemies, d_kills, d_wave, d_score, d_tokens, d_boss_hp;
static float d_hp[MAXP], d_px[MAXP], d_cam;
static uint32_t d_hash;
static float d_ex[8][4];
static int32_t d_states[MAXP];

static void play_music(int stage) {
  if (music_stage == stage && cw_voice_is_playing(music)) return;
  if (cw_voice_is_playing(music)) cw_voice_stop(music, 0.3f);
  if (!snd_music[stage & 3].id) snd_music[stage & 3] = music_make(stage & 3);
  music = cw_audio_play(snd_music[stage & 3], &(cw_play_desc){ .bus = cw_shell_music_bus(), .loop = true, .volume = 0.55f });
  music_stage = stage;
}

static void shell_sound(cw_shell_sound s, void *user) {
  (void)user;
  cw_sound x = s == CW_SHELL_SND_MOVE ? snd_move : s == CW_SHELL_SND_BACK ? snd_back : snd_select;
  if (x.id) cw_audio_play(x, &(cw_play_desc){ .bus = cw_shell_sfx_bus() });
}
static void shell_screen(cw_shell_screen from, cw_shell_screen to, void *user) {
  (void)from, (void)user;
  if (to == CW_SHELL_PLAY) play_music(W.stage);
  else if (to == CW_SHELL_TITLE && cw_voice_is_playing(music)) { cw_voice_stop(music, 0.3f); music_stage = -1; }
}

static void start(int players, void *user) {
  (void)user;
  cw_scene_app_change_scene("main.cwscene", false);
  memset(&W, 0, sizeof W);
  fx_init();
  int stage0 = 0;
#ifdef BRAWLER_AUTOPILOT
  { const char *e = getenv("BRAWLER_STAGE"); if (e) stage0 = atoi(e) & 3; } /* screenshots and the program survey start on any stage */
#endif
  sim_reset(players, stage0);
  play_music(stage0);
}

#ifdef BRAWLER_AUTOPILOT
/* debug channel: "stage N" jumps to a stage (carts under romdev have no environment variables) */
static void cmd_stage(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: stage <0-3>"); return; }
  int st = atoi(argv[1]) & 3;
  sim_reset(W.players, st);
  play_music(st);
  cw_reply_printf(r, "{\"stage\":%d}", st);
}
/* cam <zoom> [dx dz]: zoom the camera toward the street (1 is the game view) */
static void cmd_cam(int argc, const char **argv, cw_reply *r) {
  g_cam[0] = argc > 1 ? (float)atof(argv[1]) : 1, g_cam[1] = argc > 2 ? (float)atof(argv[2]) : 0, g_cam[2] = argc > 3 ? (float)atof(argv[3]) : 0;
  cw_reply_printf(r, "{\"zoom\":%g}", g_cam[0]);
}
/* spawn <kind 3-11> [dx dz]: an enemy near the camera */
static void cmd_spawn(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: spawn <3-11> [dx dz]"); return; }
  int d = atoi(argv[1]);
  if (d < 3 || d >= D_COUNT) { cw_reply_error(r, "kind 3-11"); return; }
  int i = ent_spawn_enemy(d, W.cam_x + (argc > 2 ? (float)atof(argv[2]) : 0), argc > 3 ? (float)atof(argv[3]) : 0);
  cw_reply_printf(r, "{\"index\":%d}", i);
}
/* freeze <0|1>: enemies hold their place and bots stop (for screenshots of the characters) */
static void cmd_freeze(int argc, const char **argv, cw_reply *r) {
  g_freeze = argc > 1 && atoi(argv[1]) != 0;
  cw_reply_printf(r, "{\"freeze\":%d}", g_freeze);
}
/* bots: walk to the nearest enemy and fight; grab, kick and jump now and then */
static int bot_pick[MAXP] = { -1, -1, -1, -1 };
static void bot(int i, inputs *in) {
  const ent *e = &W.p[i];
  memset(in, 0, sizeof *in);
  if (g_freeze) return;
  int best = -1;
  float bd = 1e9f;
  for (int k = 0; k < MAXE; k++) {
    const ent *v = &W.e[k];
    if (!v->used || v->st == S_DEAD || v->st == S_DOWN) continue;
    if (v->x < W.cam_x - level_cam_halfw() + 0.8f || v->x > W.cam_x + level_cam_halfw() - 0.8f) continue; /* still coming on */
    float d = (v->x - e->x) * (v->x - e->x) + (v->z - e->z) * (v->z - e->z) * 3;
    for (int j = 0; j < i; j++) if (bot_pick[j] == k) d += 9; /* spread out: another bot already has this one */
    if (d < bd) bd = d, best = k;
  }
  bot_pick[i] = best;
  float tx, tz;
  if (best < 0) { /* nothing to fight: walk on when the wave is clear, else hold the middle of the screen */
    bool clear = true;
    for (int k = 0; k < MAXE; k++) if (W.e[k].used && W.e[k].st != S_DEAD) clear = false;
    tx = (clear ? W.lock_x + 3.0f : W.cam_x - 1.0f) + (float)i * 1.2f;
    tz = -1.2f + (float)i * 1.2f;
  } else {
    const ent *v = &W.e[best];
    tx = v->x - (e->x < v->x ? 0.9f : -0.9f);
    tz = v->z;
  }
  float dx = tx - e->x, dz = tz - e->z;
  in->mx = dx > 0.15f ? 1 : dx < -0.15f ? -1 : 0;
  in->mz = dz > 0.12f ? 1 : dz < -0.12f ? -1 : 0;
  if (best >= 0) {
    const ent *v = &W.e[best];
    float fx = (v->x - e->x) * (float)e->face;
    bool near = fx > -0.2f && fx < 1.2f && (v->z - e->z) * (v->z - e->z) < 0.3f;
    if (near && (W.tick + i * 7) % 12 == 0) {
      int r = (int)(W.hash ^ (uint32_t)(W.tick * 7 + i)) % 11;
      if (r == 0) in->grab = true;
      else if (r == 1) in->kick = true;
      else if (r == 2 && e->hp > e->maxhp * 0.5f) in->special = true;
      else in->atk = true;
    }
    if (e->st == S_GRAB) { in->atk = (W.tick % 14) == 0; in->mx = 0; }
    if (e->st == S_IDLE && (W.tick + i * 31) % 233 == 0) in->jump = true;
  }
}
#endif

static void init(void *user) {
  (void)user;
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("brawler: input.cfg had errors");
  a_left = cw_action_find("left"), a_right = cw_action_find("right"), a_up = cw_action_find("up"), a_down = cw_action_find("down");
  a_atk = cw_action_find("attack"), a_kick = cw_action_find("kick"), a_jump = cw_action_find("jump");
  a_grab = cw_action_find("grab"), a_special = cw_action_find("special");
  cw_shell_init(&(cw_shell_desc){
    .title = "Brawler",
    .subtitle = "four fighters, one street",
    .max_players = MAXP,
    .shared_screen = true,
    .start = start,
    .sound = shell_sound,
    .screen_changed = shell_screen,
  });
  audio_init();
  extern cw_sound music_make(int);
  {
    enum { RATE = 48000 };
    static float pcm[RATE / 8];
    for (int k = 0; k < 3; k++) {
      double ph = 0;
      float f = k == 0 ? 660 : k == 1 ? 520 : 400;
      for (int i = 0; i < RATE / 8; i++) { ph += 6.2831853 * f / RATE; pcm[i] = (float)sin(ph) * 0.25f * (1.0f - (float)i / (RATE / 8)); }
      cw_sound s = cw_sound_from_pcm(pcm, RATE / 8, 1, RATE, NULL);
      if (k == 0) snd_move = s; else if (k == 1) snd_select = s; else snd_back = s;
    }
  }
  cw_debug_i32("game.ticks", &d_ticks);
  cw_debug_i32("game.players", &d_players);
  cw_debug_i32("game.stage", &d_stage);
  cw_debug_i32("game.phase", &d_phase);
  cw_debug_i32("game.enemies", &d_enemies);
  cw_debug_i32("game.kills", &d_kills);
  cw_debug_i32("game.wave", &d_wave);
  cw_debug_i32("game.score", &d_score);
  cw_debug_i32("game.tokens", &d_tokens);
  cw_debug_i32("game.token_leak", &d_leak);
  cw_debug_i32("game.boss_hp", &d_boss_hp);
  cw_debug_field("game.hp", d_hp, CW_DBG_F32, MAXP);
  cw_debug_field("game.px", d_px, CW_DBG_F32, MAXP);
  cw_debug_field("game.state", d_states, CW_DBG_I32, MAXP);
  cw_debug_field("game.enemy", d_ex, CW_DBG_F32, 32);
  cw_debug_f32("game.cam", &d_cam);
  cw_debug_u32("game.hash", &d_hash);
  { extern float g_spring_motion; cw_debug_f32("game.spring", &g_spring_motion); }
#ifdef BRAWLER_AUTOPILOT
  cw_debug_command("stage", "stage <0-3>: jump to a stage", cmd_stage);
  cw_debug_command("cam", "cam <zoom> [dx dz]: camera zoom toward the street", cmd_cam);
  cw_debug_command("spawn", "spawn <3-11> [dx dz]: an enemy near the camera", cmd_spawn);
  cw_debug_command("freeze", "freeze <0|1>: enemies and bots hold still", cmd_freeze);
  { const char *np = getenv("BRAWLER_PLAYERS"); cw_shell_start(np ? atoi(np) : 3); } /* BRAWLER_PLAYERS: 1 to 4 bots (native perf runs) */
#endif
}

static void publish(void) {
  d_ticks = W.tick, d_players = W.players, d_stage = W.stage, d_phase = W.phase, d_kills = W.kills, d_wave = W.wave;
  d_score = W.total_score, d_tokens = W.tokens_out, d_cam = W.cam_x, d_hash = W.hash;
  int n = 0;
  for (int i = 0; i < MAXE; i++) if (W.e[i].used && W.e[i].st != S_DEAD) n++;
  d_enemies = n;
  { int held = 0; for (int i = 0; i < MAXE; i++) if (W.e[i].used && W.e[i].token) held++; d_leak = W.tokens_out - held; } /* the leak audit: zero always */
  { int k = 0; for (int i = 0; i < MAXE && k < 8; i++) if (W.e[i].used) { d_ex[k][0] = W.e[i].x; d_ex[k][1] = W.e[i].z; d_ex[k][2] = (float)W.e[i].st; d_ex[k][3] = (float)W.e[i].token * 100 + (float)W.e[i].cd; k++; } for (; k < 8; k++) memset(d_ex[k], 0, sizeof d_ex[k]); }
  d_boss_hp = (W.boss_active && W.boss_idx >= 0 && W.e[W.boss_idx].used) ? (int)W.e[W.boss_idx].hp : 0;
  for (int i = 0; i < MAXP; i++) d_hp[i] = W.p[i].used ? W.p[i].hp : 0, d_px[i] = W.p[i].x, d_states[i] = W.p[i].used ? (int)W.p[i].st : -1;
}

static void fixed_update(void *user, double dt) {
  (void)user, (void)dt;
  if (!cw_shell_update()) return;
  inputs in[MAXP];
  memset(in, 0, sizeof in);
  for (int i = 0; i < W.players && i < MAXP; i++) {
#ifdef BRAWLER_AUTOPILOT
    bot(i, &in[i]);
#else
    in[i].mx = cw_input_axis(i, a_left, a_right);
    in[i].mz = cw_input_axis(i, a_up, a_down);
    in[i].atk = cw_action_just_pressed(i, a_atk), in[i].kick = cw_action_just_pressed(i, a_kick), in[i].jump = cw_action_just_pressed(i, a_jump);
    in[i].grab = cw_action_just_pressed(i, a_grab), in[i].special = cw_action_just_pressed(i, a_special);
#endif
  }
#ifdef BRAWLER_AUTOPILOT
  { /* BRAWLER_CROWD=N (native perf runs): keep N enemies on the street, a full wave all the time */
    static int crowd_n = -1;
    if (crowd_n < 0) { const char *c = getenv("BRAWLER_CROWD"); crowd_n = c ? atoi(c) : 0; }
    int alive = 0;
    for (int k = 0; k < MAXE; k++) if (W.e[k].used && W.e[k].st != S_DEAD) alive++;
    if (crowd_n && W.phase == PH_PLAY && alive < crowd_n && W.tick % 20 == 0) ent_spawn_enemy(D_THUG + (W.tick / 20) % 5, W.cam_x + ((W.tick / 20) & 1 ? 7.5f : -7.5f), (float)((W.tick / 20) % 5) - 2.0f);
  }
#endif
  sim_tick(in);
  chars_secondary();
  /* the stage is done: the next one, or the end */
  if (W.phase == PH_CLEAR && W.phase_t > 240) {
    if (W.stage + 1 >= 4) W.phase = PH_WIN, W.phase_t = 0;
    else { sim_reset(W.players, W.stage + 1); play_music(W.stage); }
  }
  if ((W.phase == PH_OVER || W.phase == PH_WIN) && W.phase_t > 420) {
#ifdef BRAWLER_AUTOPILOT
    sim_reset(W.players, 0);
#else
    cw_shell_to_title();
#endif
  }
  publish();
}

static void render(void *user, double alpha) {
  (void)user, (void)alpha;
  if (cw_shell_screen_now() == CW_SHELL_PLAY || cw_shell_screen_now() == CW_SHELL_PAUSE) {
    for (int i = 0; i < MAXP; i++) if (W.p[i].used) char_sync(&W.p[i]);
    for (int i = 0; i < MAXE; i++) if (W.e[i].used) char_sync(&W.e[i]);
    ui_draw();
  }
  cw_shell_draw();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .width = 1280, .height = 720, .debug = true, .init = init, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("main.cwscene", &app);
}
