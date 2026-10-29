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
static cw_sound snd_move, snd_select, snd_back, snd_music[5]; /* the four stages, and Arthur's theme (index 4, from the jukebox) */
static cw_voice music;
static int music_stage = -1;
static int32_t d_leak, d_ticks, d_players, d_stage, d_phase, d_enemies, d_kills, d_wave, d_score, d_tokens, d_boss_hp;
static float d_hp[MAXP], d_px[MAXP], d_cam;
static uint32_t d_hash;
static float d_ex[8][4];
static int32_t d_states[MAXP];
static int32_t d_unfair, d_still, d_frozen, d_blocks, d_pickups, d_bphase, d_deaths, d_first, d_hits, d_nan, d_stall, d_proj, d_weapon, d_rigs, d_overlaps;
static int32_t d_juke[6];
static int32_t d_trucker[6], d_creep[6], d_clones, d_blood[4]; /* the jukebox: uses, fighters knocked down by its shockwave, fighters who jumped it, playing, radius in cm, Arthur on his way to it */
static float d_face[8]; /* the boss's expression weights (blink snarl shout wince ko grin effort grimace) */
static int64_t run_ticks; /* fixed updates since the program started (status lines) */

static void play_music(int stage) {
  if (music_stage == stage && cw_voice_is_playing(music)) return;
  if (cw_voice_is_playing(music)) cw_voice_stop(music, 0.3f);
  int k = stage == 4 ? 4 : stage & 3;
  if (!snd_music[k].id) snd_music[k] = music_make(k);
  music = cw_audio_play(snd_music[k], &(cw_play_desc){ .bus = cw_shell_music_bus(), .loop = true, .volume = 0.55f });
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
  chars_reset(); /* the old scene's rigs went with it (a second game from the title) */
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
/* players N: start over with N bots (1 to 4) on the current stage */
static void cmd_players(int argc, const char **argv, cw_reply *r) {
  int n = argc > 1 ? atoi(argv[1]) : 0;
  if (n < 1 || n > MAXP) { cw_reply_error(r, "usage: players <1-4>"); return; }
  int st = W.stage;
  W.players = 0; /* not a stage continue: fresh fighters */
  sim_reset(n, st);
  cw_reply_printf(r, "{\"players\":%d}", n);
}
/* wave N: jump to wave N of this stage (4 is the boss) */
static void cmd_wave(int argc, const char **argv, cw_reply *r) {
  int n = argc > 1 ? atoi(argv[1]) : -1;
  if (n < 0 || n >= stage_wave_count(W.stage)) { cw_reply_error(r, "usage: wave <0-4>"); return; }
  sim_goto_wave(n);
  cw_reply_printf(r, "{\"wave\":%d}", n);
}
/* item <kind 0-4> [dx dz]: drop an item near player 1 (0 food, 1 bat, 2 pipe, 3 knife, 4 coin) */
static void cmd_item(int argc, const char **argv, cw_reply *r) {
  int k = argc > 1 ? atoi(argv[1]) : -1;
  if (k < 0 || k > 4) { cw_reply_error(r, "usage: item <0-4> [dx dz]"); return; }
  for (int i = 0; i < MAXITEM; i++)
    if (!W.item[i].used) {
      W.item[i] = (item_t){ .used = true, .kind = k, .x = W.p[0].x + (argc > 2 ? (float)atof(argv[2]) : 0), .z = W.p[0].z + (argc > 3 ? (float)atof(argv[3]) : 0), .y = 0.05f };
      cw_reply_printf(r, "{\"item\":%d}", i);
      return;
    }
  cw_reply_error(r, "no free item");
}
/* cam <zoom> [dx dz]: zoom the camera toward the street (1 is the game view) */
static void cmd_cam(int argc, const char **argv, cw_reply *r) {
  g_cam[0] = argc > 1 ? (float)atof(argv[1]) : 1, g_cam[1] = argc > 2 ? (float)atof(argv[2]) : 0, g_cam[2] = argc > 3 ? (float)atof(argv[3]) : 0;
  cw_reply_printf(r, "{\"zoom\":%g}", g_cam[0]);
}
/* spawn <kind 3-11> [dx dz]: an enemy near the camera */
static void cmd_spawn(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: spawn <3-16> [dx dz]"); return; }
  int d = atoi(argv[1]);
  if (d < D_THUG || d >= D_COUNT) { cw_reply_error(r, "kind 3-16"); return; }
  int i = ent_spawn_enemy(d, W.cam_x + (argc > 2 ? (float)atof(argv[2]) : 0), argc > 3 ? (float)atof(argv[3]) : 0);
  cw_reply_printf(r, "{\"index\":%d}", i);
}
/* botdmg <k>: the bots' damage factor (3 by default: they are poor fighters; 1 is a fighter's real damage, what the boss
   gates use so a boss fight lasts as long as it does for a player) */
static void cmd_botdmg(int argc, const char **argv, cw_reply *r) {
  extern float g_bot_dmg;
  if (argc > 1) g_bot_dmg = (float)atof(argv[1]);
  cw_reply_printf(r, "{\"botdmg\":%g}", g_bot_dmg);
}
/* hurtat <tick> <enemy> [knock 0-2]: at that fixed tick of the run, enemy slot N takes a blow (10 damage) from the left
   (screenshots of the faces a hit brings out) */
static int64_t hurt_tick = -1; static int hurt_idx, hurt_knock;
static void cmd_hurtat(int argc, const char **argv, cw_reply *r) {
  if (argc < 3) { cw_reply_error(r, "usage: hurtat <tick> <enemy> [knock]"); return; }
  hurt_tick = atoll(argv[1]), hurt_idx = atoi(argv[2]) % MAXE, hurt_knock = argc > 3 ? atoi(argv[3]) : 1;
  cw_reply_printf(r, "{\"ok\":1}");
}
/* legacyshake <0|1>: the old camera shake back (the camera gate's control only) */
static void cmd_legacyshake(int argc, const char **argv, cw_reply *r) { g_legacy_shake = argc > 1 && atoi(argv[1]); cw_reply_printf(r, "{\"legacyshake\":%d}", g_legacy_shake); }
/* blood <dx> <dz> <kind 0-2> [size]: a stain on the street near the camera (screenshots of the shapes) */
static void cmd_blood(int argc, const char **argv, cw_reply *r) {
  if (argc < 4) { cw_reply_error(r, "usage: blood <dx> <dz> <kind 0-2> [size]"); return; }
  fx_stain(W.cam_x + (float)atof(argv[1]), (float)atof(argv[2]), argc > 4 ? (float)atof(argv[4]) : 0.8f, atoi(argv[3]) % 3);
  cw_reply_printf(r, "{\"ok\":1}");
}
/* knockdown: every fighter and enemy on its feet is knocked down at once (ragdoll close-ups and the ragdoll cost) */
static void knock_all(void) {
  int n = 0;
  for (int i = 0; i < MAXP + MAXE; i++) {
    ent *e = i < MAXP ? &W.p[i] : &W.e[i - MAXP];
    if (!e->used || e->st == S_DEAD || e->st == S_FALL || e->st == S_DOWN || e->st == S_THROWN || e->hp <= 0) continue;
    e->invuln = 0;
    ent_hurt(e, NULL, 0, 2, 0);
    n++;
  }
  (void)n;
}
static void cmd_knockdown(int argc, const char **argv, cw_reply *r) {
  (void)argc, (void)argv;
  knock_all();
  cw_reply_printf(r, "{\"ok\":1}");
}
/* freeze <0|1>: enemies hold their place and bots stop (for screenshots of the characters) */
static void cmd_freeze(int argc, const char **argv, cw_reply *r) {
  g_freeze = argc > 1 && atoi(argv[1]) != 0;
  { extern float g_freeze_yaw; g_freeze_yaw = argc > 2 ? (float)atof(argv[2]) : 0.45f; }
  cw_reply_printf(r, "{\"freeze\":%d}", g_freeze);
}
/* bots: walk to the nearest enemy and fight; grab, kick and jump now and then */
static int bot_pick[MAXP] = { -1, -1, -1, -1 };
/* BRAWLER_INJECT (controls for tools/soak.mjs, which must then fail): "stall" the bots stand still, "leak" one attack
   token is lost a few seconds in */
static const char *inject(void) { static const char *v; static bool got; if (!got) { v = getenv("BRAWLER_INJECT"); got = true; } return v ? v : ""; }
static void bot(int i, inputs *in) {
  const ent *e = &W.p[i];
  memset(in, 0, sizeof *in);
  if (g_freeze || !strcmp(inject(), "stall")) return;
  int best = -1;
  float bd = 1e9f;
  for (int k = 0; k < MAXE; k++) {
    const ent *v = &W.e[k];
    if (!v->used || v->st == S_DEAD || v->st == S_DOWN) continue;
    if (!on_screen(v->x, v->z, 0.8f)) continue; /* still coming on */
    float d = (v->x - e->x) * (v->x - e->x) + (v->z - e->z) * (v->z - e->z) * 3;
    for (int j = 0; j < i; j++) if (bot_pick[j] == k) d += 9; /* spread out: another bot already has this one */
    if (d < bd) bd = d, best = k;
  }
  if (best < 0) /* nobody inside the picture: go to the nearest one anyway */
    for (int k = 0; k < MAXE; k++) {
      const ent *v = &W.e[k];
      if (!v->used || v->st == S_DEAD || v->st == S_DOWN || v->x < W.cam_x - 12 || v->x > W.cam_x + 12) continue;
      float d = (v->x - e->x) * (v->x - e->x) + (v->z - e->z) * (v->z - e->z);
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
    float off = DEFS[v->def].boss ? 1.15f : DEFS[v->def].mini ? 1.0f : 0.9f; /* big bodies are held further off (separate_bodies) */
    tx = v->x - (e->x < v->x ? off : -off);
    tz = v->z + ((i & 1) ? 0.3f : -0.3f); /* fighters on one enemy flank it a step apart in depth, not in single file */
  }
  float dx = tx - e->x, dz = tz - e->z;
  in->mx = dx > 0.15f ? 1 : dx < -0.15f ? -1 : 0;
  in->mz = dz > 0.12f ? 1 : dz < -0.12f ? -1 : 0;
  if (best >= 0) {
    const ent *v = &W.e[best];
    /* standing in place next to it but turned away (no step to turn on): a nudge toward it turns the bot round (four bots
       round a boss at the screen edge stood there back to back for minutes) */
    if (in->mx == 0 && (v->x - e->x) * (float)e->face < -0.1f && fabsf(v->x - e->x) < 2.0f) in->mx = v->x > e->x ? 0.3f : -0.3f;
    float fx = (v->x - e->x) * (float)e->face;
    bool near = fx > -0.2f && fx < (DEFS[v->def].boss ? 1.45f : DEFS[v->def].mini ? 1.32f : 1.2f) && (v->z - e->z) * (v->z - e->z) < 0.3f;
    if (near && (W.tick + i * 7) % 12 == 0) {
      int r = (int)(W.hash ^ (uint32_t)(W.tick * 7 + i)) % 11;
      if (v->def == D_GUARD && (v->x - e->x) * (float)v->face < 0) r = r < 5 ? 1 : r < 7 ? 0 : 3; /* a shield in the way: kick or grab */
      if (r == 0) in->grab = true;
      else if (r == 1) in->kick = true;
      else if (r == 2 && e->hp > e->maxhp * 0.5f) in->special = true;
      else in->atk = true;
    }
    if (e->st == S_GRAB) { in->atk = (W.tick % 14) == 0; in->mx = 0; }
    if (e->st == S_IDLE && (W.tick + i * 31) % 233 == 0) in->jump = true;
  }
  if (W.juke.t > 0 && !(W.juke.mask & (1u << i))) { /* the jukebox ring is coming: jump it when it is close */
    float jx = e->x - JUKE_X, jz = e->z - JUKE_Z, d = sqrtf(jx * jx + jz * jz);
    if (d - W.juke.r < 0.9f && d - W.juke.r > 0.2f && !strcmp(inject(), "")) in->jump = true, in->atk = in->kick = in->grab = in->special = false;
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
  rag_debug_fields();
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
  cw_debug_i32("game.unfair", &d_unfair);       /* hits on a fighter from off camera or from an enemy still walking in: 0 */
  cw_debug_i32("game.still_max", &d_still);     /* longest a waiting enemy stood frozen this stage (ticks) */
  cw_debug_i32("game.frozen", &d_frozen);       /* waiting enemies that stood frozen a full second */
  cw_debug_i32("game.blocks", &d_blocks);       /* blows a guard's shield stopped */
  cw_debug_i32("game.pickups", &d_pickups);
  cw_debug_i32("game.overlaps", &d_overlaps);   /* most pairs of standing bodies inside each other on any tick (separate_bodies): 0 */
  cw_debug_i32("game.boss_phase", &d_bphase);   /* highest boss phase reached this stage (0 to 2) */
  cw_debug_i32("game.deaths", &d_deaths);
  cw_debug_i32("game.first_hit", &d_first);     /* ticks from the stage start to the first blow that landed, -1 none */
  cw_debug_i32("game.hits", &d_hits);
  cw_debug_i32("game.nan", &d_nan);
  cw_debug_i32("game.stall_max", &d_stall);
  cw_debug_i32("game.proj", &d_proj);
  cw_debug_i32("game.weapon", &d_weapon);       /* player 1's weapon (0 none, 1 bat, 2 pipe, 3 knife) */
  cw_debug_i32("game.rigs", &d_rigs);           /* live characters with a valid rig node (a new game from the title must keep them) */
  cw_debug_field("game.hp", d_hp, CW_DBG_F32, MAXP);
  cw_debug_field("game.px", d_px, CW_DBG_F32, MAXP);
  cw_debug_field("game.state", d_states, CW_DBG_I32, MAXP);
  cw_debug_field("game.enemy", d_ex, CW_DBG_F32, 32);
  cw_debug_f32("game.cam", &d_cam);
  cw_debug_u32("game.hash", &d_hash);
  cw_debug_field("game.juke", d_juke, CW_DBG_I32, 6);
  cw_debug_field("game.trucker", d_trucker, CW_DBG_I32, 6); /* truckers spawned, haymakers, haymakers that landed, barges, barges that landed, killed */
  cw_debug_field("game.creep", d_creep, CW_DBG_I32, 6); /* creeps spawned, coat flashes, flashes that landed, light blows he leaned into, health they gave him, killed */
  { extern int g_camjit; cw_debug_i32("game.camjit", &g_camjit); } /* the largest camera shake offset on any frame, mm: 0 */
  cw_debug_i32("game.clones", &d_clones); /* most pairs of one kind of enemy on the street looking the same on any frame: 0 */
  cw_debug_field("fx.blood", d_blood, CW_DBG_I32, 4); /* stains placed, droplet bursts, stains showing, same-shape stains on top of each other (0) */
  cw_debug_field("face.boss", d_face, CW_DBG_F32, 8);
  { extern float g_spring_motion; cw_debug_f32("game.spring", &g_spring_motion); }
#ifdef BRAWLER_AUTOPILOT
  cw_debug_command("stage", "stage <0-3>: jump to a stage", cmd_stage);
  cw_debug_command("cam", "cam <zoom> [dx dz]: camera zoom toward the street", cmd_cam);
  cw_debug_command("players", "players <1-4>: start over with N bots", cmd_players);
  cw_debug_command("wave", "wave <0-4>: jump to a wave of this stage (4 is the boss)", cmd_wave);
  cw_debug_command("item", "item <0-4> [dx dz]: drop an item near player 1", cmd_item);
  { const char *v = getenv("BRAWLER_LOG"); extern bool g_log_frozen; g_log_frozen = v && atoi(v); }
  { const char *v = getenv("BRAWLER_NO_SCREEN_RULE"); extern bool g_no_screen_rule; g_no_screen_rule = v && atoi(v); }
  { const char *v = getenv("BRAWLER_BOTDMG"); extern float g_bot_dmg; if (v) g_bot_dmg = (float)atof(v); }
  { const char *v = getenv("BRAWLER_SEED"); extern uint64_t g_seed_salt; if (v) g_seed_salt = (uint64_t)strtoull(v, NULL, 10); }
  cw_debug_command("spawn", "spawn <3-15> [dx dz]: an enemy near the camera (14: Arthur, 15: the trucker, 16: the creep)", cmd_spawn);
  cw_debug_command("freeze", "freeze <0|1>: enemies and bots hold still", cmd_freeze);
  cw_debug_command("botdmg", "botdmg <k>: the bots' damage factor (3 default, 1 real)", cmd_botdmg);
  cw_debug_command("hurtat", "hurtat <tick> <enemy> [knock]: a blow on an enemy at that tick", cmd_hurtat);
  cw_debug_command("legacyshake", "legacyshake <0|1>: the old camera shake (gate control only)", cmd_legacyshake);
  cw_debug_command("blood", "blood <dx> <dz> <kind 0-2> [size]: a blood stain near the camera", cmd_blood);
  cw_debug_command("knockdown", "knockdown: knock every fighter and enemy down at once (ragdolls)", cmd_knockdown);
  { const char *np = getenv("BRAWLER_PLAYERS"); cw_shell_start(np ? atoi(np) : 3); } /* BRAWLER_PLAYERS: 1 to 4 bots (native perf runs) */
#endif
}

static void publish(void) {
  d_ticks = W.tick, d_players = W.players, d_stage = W.stage, d_phase = W.phase, d_kills = W.kills, d_wave = W.wave;
  d_score = W.total_score, d_tokens = W.tokens_out, d_cam = W.cam_x, d_hash = W.hash;
  int n = 0;
  for (int i = 0; i < MAXE; i++) if (W.e[i].used && W.e[i].st != S_DEAD) n++;
  d_enemies = n;
  d_leak = token_leak(); /* the leak audit: zero always */
  { extern int g_sep_overlaps; if (g_sep_overlaps > d_overlaps) d_overlaps = g_sep_overlaps; }
  d_unfair = W.st.unfair, d_still = W.st.still_max, d_frozen = W.st.frozen, d_blocks = W.st.blocks, d_pickups = W.st.pickups, d_bphase = W.st.boss_phase;
  d_deaths = W.st.deaths, d_first = W.st.first_hit, d_hits = W.st.hits, d_nan = W.st.nan, d_stall = W.st.stall_max, d_weapon = W.p[0].weapon;
  d_proj = 0;
  for (int i = 0; i < 8; i++) d_proj += W.proj[i].used;
  d_rigs = 0;
  for (int i = 0; i < MAXP + MAXE; i++) { const ent *e = i < MAXP ? &W.p[i] : &W.e[i - MAXP]; if (e->used && e->root.id && cw_node_valid(e->root)) d_rigs++; }
  { int k = 0; for (int i = 0; i < MAXE && k < 8; i++) if (W.e[i].used) { d_ex[k][0] = W.e[i].x; d_ex[k][1] = W.e[i].z; d_ex[k][2] = (float)W.e[i].st; d_ex[k][3] = (float)W.e[i].token * 100 + (float)W.e[i].cd; k++; } for (; k < 8; k++) memset(d_ex[k], 0, sizeof d_ex[k]); }
  d_trucker[0] = W.trucker.spawned, d_trucker[1] = W.trucker.hay, d_trucker[2] = W.trucker.hay_hits, d_trucker[3] = W.trucker.barge, d_trucker[4] = W.trucker.barge_hits, d_trucker[5] = W.trucker.kills;
  d_creep[0] = W.creep.spawned, d_creep[1] = W.creep.flash, d_creep[2] = W.creep.flash_hits, d_creep[3] = W.creep.shrugged, d_creep[4] = W.creep.healed, d_creep[5] = W.creep.kills;
  { int c = looks_clones(); if (c > d_clones) d_clones = c; }
  fx_blood_stats(d_blood);
  d_juke[0] = W.juke.uses, d_juke[1] = W.juke.hits, d_juke[2] = W.juke.dodges, d_juke[3] = W.juke.on, d_juke[4] = W.juke.t > 0 ? (int)(W.juke.r * 100) : 0;
  d_juke[5] = W.boss_active && W.boss_idx >= 0 && W.e[W.boss_idx].used ? W.e[W.boss_idx].juke : 0;
  { extern float face_weight_of_enemy(int i, int t); for (int t = 0; t < 8; t++) d_face[t] = W.boss_active && W.boss_idx >= 0 && W.e[W.boss_idx].used ? face_weight_of_enemy(W.boss_idx, t) : 0; }
  d_boss_hp = (W.boss_active && W.boss_idx >= 0 && W.e[W.boss_idx].used) ? (int)W.e[W.boss_idx].hp : 0;
  for (int i = 0; i < MAXP; i++) d_hp[i] = W.p[i].used ? W.p[i].hp : 0, d_px[i] = W.p[i].x, d_states[i] = W.p[i].used ? (int)W.p[i].st : -1;
}

static void sim_step(void);
static void fixed_update(void *user, double dt) {
  (void)user, (void)dt;
  if (!cw_shell_update()) return;
  int turbo = 1;
#ifdef BRAWLER_AUTOPILOT
  { static int tb = -1; if (tb < 0) { const char *v = getenv("BRAWLER_TURBO"); tb = v && atoi(v) > 0 ? atoi(v) : 1; } turbo = tb; } /* soaks: N ticks per frame */
#endif
  for (int k = 0; k < turbo; k++) sim_step();
  publish();
}
static void sim_step(void) {
  run_ticks++;
#ifdef BRAWLER_AUTOPILOT
  if (run_ticks == 600 && !strcmp(inject(), "leak")) W.tokens_out++;
  if (run_ticks == hurt_tick && W.e[hurt_idx].used) { W.e[hurt_idx].invuln = 0; ent_hurt(&W.e[hurt_idx], NULL, 10, hurt_knock, 0); }
#endif
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
    if (crowd_n && W.phase == PH_PLAY && alive < crowd_n && W.tick % 20 == 0) ent_spawn_enemy((W.tick / 20) % 8 == 7 ? D_TRUCKER : D_THUG + (W.tick / 20) % 8, W.cam_x + ((W.tick / 20) & 1 ? 7.5f : -7.5f), (float)((W.tick / 20) % 5) - 2.0f);
  }
#endif
#ifdef BRAWLER_AUTOPILOT
  { static int ka = -1; if (ka < 0) { const char *v = getenv("BRAWLER_KNOCKALL"); ka = v ? atoi(v) : 0; } /* BRAWLER_KNOCKALL=N: everyone down every N ticks (the ragdoll cost) */
    if (ka > 0 && run_ticks % ka == 0 && W.phase == PH_PLAY) knock_all(); }
#endif
  sim_tick(in);
  if (W.juke.on && music_stage != 4 && W.phase == PH_PLAY) play_music(4); /* the jukebox plays Arthur's theme */
  rag_tick(); /* after the rules, before the springs: the ragdolls show what the simulation decided */
  chars_secondary();
  /* the stage is done: the next one, or the end */
  if (W.phase == PH_CLEAR && W.phase_t > 240) {
    if (W.stage + 1 >= 4) W.phase = PH_WIN, W.phase_t = 0;
    else { sim_reset(W.players, W.stage + 1); play_music(W.stage); }
  }
  if ((W.phase == PH_OVER || W.phase == PH_WIN) && W.phase_t > 420) {
#ifdef BRAWLER_AUTOPILOT
    static int soak = -1;
    if (soak < 0) { const char *v = getenv("BRAWLER_SOAK"); soak = v && atoi(v); }
    if (soak && W.phase == PH_OVER) { /* a continue: the same stage with fresh lives */
      sim_reset(W.players, W.stage);
      for (int i = 0; i < W.players; i++) { W.p[i].lives = 3; W.p[i].hp = W.p[i].maxhp; }
    } else sim_reset(W.players, 0);
    play_music(W.stage);
#else
    cw_shell_to_title();
#endif
  }
#ifdef BRAWLER_AUTOPILOT
  { static int lg = -1; if (lg < 0) { const char *v = getenv("BRAWLER_LOG"); lg = v && atoi(v); }
    if (lg && run_ticks % 3600 == 0) {
      int alive = 0, en = 0;
      for (int i = 0; i < W.players; i++) alive += W.p[i].used && W.p[i].st != S_DEAD;
      for (int i = 0; i < MAXE; i++) en += W.e[i].used && W.e[i].st != S_DEAD;
      cw_info("brawler-status run=%lld stage=%d wave=%d phase=%d enemies=%d alive=%d tokens=%d leak=%d nan=%d stall=%d boss_hp=%d boss_phase=%d unfair=%d still_max=%d rigs=%d %s",
              (long long)run_ticks, W.stage, W.wave, W.phase, en, alive, W.tokens_out, token_leak(), W.st.nan, W.st.stall, d_boss_hp, W.st.boss_phase, W.st.unfair, W.st.still_max, rig_count(), rag_status());
    } }
#endif
}

static void render(void *user, double alpha) {
  (void)user, (void)alpha;
  if (cw_shell_screen_now() == CW_SHELL_PLAY || cw_shell_screen_now() == CW_SHELL_PAUSE) {
    for (int i = 0; i < MAXP; i++) if (W.p[i].used) char_sync(&W.p[i]);
    for (int i = 0; i < MAXE; i++) if (W.e[i].used) char_sync(&W.e[i]);
    sim_sync_extras();
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
