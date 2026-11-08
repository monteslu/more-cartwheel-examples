/* fighter: a one-on-one fighting game. The rules are 2D and integer (sim.c);
 * the look is 3D (chars.c, the stage scenes, fx.c). This file is the shell
 * around them: input, select screen, camera, HUD, training mode, sound, tests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cartwheel/audio.h"
#include "cartwheel/quality.h"
#include "cartwheel/quality_menu.h"
#include "game.h"

enum { GS_SELECT, GS_MATCH, GS_RESULT };
enum { SFX_HIT_L, SFX_HIT_M, SFX_HIT_H, SFX_BLOCK, SFX_WHOOSH_L, SFX_WHOOSH_H, SFX_FIRE, SFX_KO, SFX_SUPER, SFX_LAND, SFX_BELL, SFX_ROUND, SFX_N };
static const char *const SFX_FILE[SFX_N] = { "audio/hit_l.wav", "audio/hit_m.wav", "audio/hit_h.wav", "audio/block.wav", "audio/whoosh_l.wav", "audio/whoosh_h.wav",
  "audio/fire.wav", "audio/ko.wav", "audio/super.wav", "audio/land.wav", "audio/bell.wav", "audio/round.wav" };
#define STAGES 3
static const char *const STAGE_NAME[STAGES] = { "Rooftop Dusk", "Neon Alley", "Dojo Courtyard" };
static const char *const MUSIC[STAGES] = { "audio/music_dusk.ogg", "audio/music_neon.ogg", "audio/music_dojo.ogg" };
static const char *const DIFF_NAME[3] = { "Easy", "Normal", "Hard" };
static const char *const ROLE[4] = { "All-rounder", "Heavy grappler", "Fast, light hands", "Zoner, long legs" };
const float FIGHTER_COL[4][3] = { { 0.9f, 0.25f, 0.2f }, { 0.95f, 0.55f, 0.1f }, { 0.15f, 0.85f, 0.95f }, { 0.4f, 0.9f, 0.4f } };
#define FCOL FIGHTER_COL

typedef struct save_t { uint32_t magic; int32_t wins[4], matches; } save_t;

static struct {
  ft_roster roster;
  ft_game sim;
  int state, players, stage, difficulty, training, show_boxes;
  /* the computer's level: a session opens on Easy and steps up to Normal after the first win against it, until the
   * player picks a level themselves (diff_picked). stepped_up: the result screen says so */
  int diff_picked, stepped_up;
  /* arcade (one player): the three other fighters on the three stages, then the mirror match a level up. arc_step is the
   * fight being played (0..3), arc_clear set when the fourth is won */
  int arcade, arc_step, arc_clear, arc_lost, arc_stage0;
  int32_t d_arc[4]; /* debug arcade: mode on, fights won, cleared, the fighters faced (a bit each; the mirror is bit 4) */
  int sw_cpu_hp;    /* test switch (sw cpu_hp 1): the computer starts every round on 1 life (the arcade gate wins quickly) */
  int sel[2], ready[2], sel_stage;
  /* a seat that does not lock in: ticks the other side has waited since it locked in, the sides the computer plays for
   * that reason, and the test switch (sw seat_wait 1) that waits for ever, as the select screen once did */
  int wait_t, cpu_seat[2], sw_seat_wait;
  int result_t;
  cw_node stage_node, camera;
  int tex_done;
  int32_t d_tex_nodes, d_tex_objs, d_tex_meshes;
  int tex_pending;
  fighter_model *mdl[2];
  cw_frame_state fs;
  cw_font font_ui, font_bold, font_score;
  cw_sound sfx[SFX_N];
  cw_sound music[STAGES];
  cw_voice music_voice;
  cw_bus music_bus, sfx_bus;
  float shown_hp[2];
  float lag_hp[2];
  uint32_t ev_seen;
  cw_action ui_start, ui_quit, ui_pause, a_left, a_right, a_up, a_down, fs_left, fs_right, fs_up, fs_down, dp_left, dp_right, dp_up, dp_down, a_p, a_k, a_g, a_pk, k_left, k_right, k_up, k_down, k_p, k_k, k_g, k_pk, tr_mode, tr_reset, tr_dummy, tr_boxes;
  t3_object *dbg_root;
  t3_mesh *dbg_box[10];
  int ndbg;
  /* debug fields */
  int32_t d_state, d_phase, d_hp[2], d_meter[2], d_wins[2], d_round, d_tick, d_combo, d_adv, d_boxes;
  int32_t d_pos[4], d_fstate[2], d_hitcount, d_blockcount, d_def[2], d_guard[2], d_stats[6];
  /* player 1 movement counters for the F-PT3 gates: walk frames, takeoffs, forward takeoffs, back takeoffs, high
   * normals started, low normals started, crouch frames, dashes, the highest jump (mm), jump attacks */
  int32_t d_ctl[10];
  uint32_t d_hash, d_end_hash;
  int32_t d_slow, d_tense;
  int no_slowmo; /* FIGHTER_NO_SLOWMO (test builds): the knockout at full speed, as before (the slow-motion gate's control) */
  float d_cam[3], d_spring;
} G;

static save_t *SAVE;
static float hash01(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return (float)(x & 0xffff) / 65535.0f; }

/* ── sound ──────────────────────────────────────────────────────────── */

static void sfx(int id, float vol, float pitch) {
  if (CW_HANDLE_IS_NULL(G.sfx[id])) return;
  cw_audio_play(G.sfx[id], &(cw_play_desc){ .bus = G.sfx_bus, .volume = vol, .pitch = pitch });
}

/* ── input ──────────────────────────────────────────────────────────── */

/* The stick in a fight: a radial dead zone (a resting stick that drifts a little does nothing), then an 8-way gate
 * whose up, down, forward and back sectors are 50 degrees wide and diagonals 40, so pushing forward a little high
 * still walks and up a little to the side still jumps straight. Opposites on the d-pad or keys: left + right is
 * neutral, up + down is up (cw_cmd_dir; the 2XKO and Evo-style rule). */
#define STICK_DEAD 0.35f
#define STICK_CARDINAL 0.466f /* tan(25 degrees): within 25 degrees of an axis is that axis alone */
static void stick8(int p, ft_input *in) {
  float x = cw_action_raw_strength(p, G.fs_right) - cw_action_raw_strength(p, G.fs_left);
  float y = cw_action_raw_strength(p, G.fs_up) - cw_action_raw_strength(p, G.fs_down);
  float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
  if (ax * ax + ay * ay < STICK_DEAD * STICK_DEAD) return;
  bool horiz = ax > 0 && ay <= ax * STICK_CARDINAL, vert = ay > 0 && ax <= ay * STICK_CARDINAL;
  if (!vert) { in->left |= x < 0; in->right |= x > 0; }
  if (!horiz) { in->up |= y > 0; in->down |= y < 0; }
}
static ft_input read_pad(int p) {
  ft_input in = { 0 };
  in.left = cw_action_pressed(p, G.dp_left);
  in.right = cw_action_pressed(p, G.dp_right);
  in.up = cw_action_pressed(p, G.dp_up);
  in.down = cw_action_pressed(p, G.dp_down);
  stick8(p, &in);
  if (cw_action_pressed(p, G.a_p)) in.btn |= B_P;
  if (cw_action_pressed(p, G.a_k)) in.btn |= B_K;
  if (cw_action_pressed(p, G.a_g)) in.btn |= B_G;
  if (cw_action_pressed(p, G.a_pk)) in.btn |= B_P | B_K; /* the throw button is punch + kick */
  if (p == 1) { /* the shared keyboard belongs to player 0's devices: player 2's keys are their own actions */
    in.left |= cw_action_pressed(0, G.k_left);
    in.right |= cw_action_pressed(0, G.k_right);
    in.up |= cw_action_pressed(0, G.k_up);
    in.down |= cw_action_pressed(0, G.k_down);
    if (cw_action_pressed(0, G.k_p)) in.btn |= B_P;
    if (cw_action_pressed(0, G.k_k)) in.btn |= B_K;
    if (cw_action_pressed(0, G.k_g)) in.btn |= B_G;
    if (cw_action_pressed(0, G.k_pk)) in.btn |= B_P | B_K;
  }
  return in;
}

/* ── stage and models ───────────────────────────────────────────────── */

/* Fighters stay resident: every fighter has one model per side, made at start-up from the preloaded glTF resources
 * (chars.c), under a node of their own that outlives stage changes. Picking a fighter only shows another model and
 * hides the last (a playtest found that switching fighters on the select screen stuttered, because every switch reloaded
 * the stage, its crowd and both fighters' glb files). The stage reloads only when the stage itself changes. */
static fighter_model *pool[2][4];
static cw_node cast_node;
static int loaded_stage = -1;
static int pool_warm; /* frames left with every pooled model drawn under the floor (start-up) */
static void make_pool(void) {
  if (cw_node_valid(cast_node)) return;
  models_preload();
  cast_node = cw_node_new("Node3D", "Cast");
  cw_node_add_child(cw_tree_root(cw_scene_app_tree()), cast_node);
  for (int side = 0; side < 2; side++)
    for (int d = 0; d < 4 && d < G.roster.n; d++) { pool[side][d] = model_new(d, cast_node); model_park(pool[side][d]); }
  pool_warm = 3;
}
/* after the warm-up frames only the two fighters in play stay visible */
static void pool_frame(void) {
  if (!pool_warm || --pool_warm) return;
  for (int side = 0; side < 2; side++)
    for (int d = 0; d < 4; d++) if (pool[side][d] && pool[side][d] != G.mdl[0] && pool[side][d] != G.mdl[1]) model_set_visible(pool[side][d], false);
}
static void show_fighters(void) {
  make_pool();
  for (int side = 0; side < 2; side++) {
    fighter_model *m = pool[side][G.sim.f[side].def & 3];
    if (G.mdl[side] != m) { if (!pool_warm) model_set_visible(G.mdl[side], false); model_reset(m); }
    model_set_showcase(m, 0);
    model_set_visible(m, true);
    G.mdl[side] = m;
  }
}

static void unload_stage(void) {
  loaded_stage = -1;
  stage_free();
  if (cw_node_valid(G.stage_node)) cw_node_free(G.stage_node);
  G.stage_node = (cw_node){ 0 };
  G.dbg_root = NULL;
  G.ndbg = 0;
  if (!CW_HANDLE_IS_NULL(G.music_voice)) cw_voice_stop(G.music_voice, 0.2f);
}

static void make_debug_boxes(void) {
  t3_object *root = t3_group_new();
  for (int i = 0; i < 10; i++) {
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->transparent = true; m->depth_test = false; m->depth_write = false; m->opacity = 0.2f;
    t3_mesh *b = t3_mesh_new(t3_box_geometry_new(1, 1, 1, 1, 1, 1), m);
    b->base.visible = false;
    b->base.render_order = 50;
    t3_object_add(root, b);
    G.dbg_box[i] = b;
    t3_release(m);
  }
  cw_node n = cw_node_new("Node3D", "DebugBoxes");
  cw_node_add_child(G.stage_node, n);
  cw_node3d_set_object(n, root);
  G.dbg_root = root;
}

static void load_stage(void) {
  if (loaded_stage == G.sel_stage && cw_node_valid(G.stage_node)) return;
  unload_stage();
  loaded_stage = G.sel_stage;
  char path[64];
  snprintf(path, sizeof path, "scenes/stage%d.cwscene", G.sel_stage + 1);
  G.stage_node = cw_scene_instance_path(path);
  if (CW_HANDLE_IS_NULL(G.stage_node)) { cw_error("fighter: %s failed to load", path); return; }
  cw_node_add_child(cw_tree_root(cw_scene_app_tree()), G.stage_node);
  G.camera = cw_node_get(G.stage_node, "Camera");
  stage_load(G.sel_stage, G.stage_node);
  fx_init(G.stage_node);
  fx_bind_lights(G.stage_node);
  make_debug_boxes();
  if (!CW_HANDLE_IS_NULL(G.music[G.sel_stage])) G.music_voice = cw_audio_play(G.music[G.sel_stage], &(cw_play_desc){ .bus = G.music_bus, .loop = true, .volume = 0.55f });
}

/* the stage (kept when it is the same one) and the two fighters of G.sim */
static void load_match(void) {
  load_stage();
  fx_reset();
  show_fighters();
  memset(&G.fs, 0, sizeof G.fs);
  G.ev_seen = G.sim.ev_seq;
  G.shown_hp[0] = G.shown_hp[1] = G.lag_hp[0] = G.lag_hp[1] = 1.0f;
}
/* everything (shutdown) */
static void unload_match(void) {
  unload_stage();
  for (int side = 0; side < 2; side++) for (int d = 0; d < 4; d++) { model_free(pool[side][d]); pool[side][d] = NULL; }
  G.mdl[0] = G.mdl[1] = NULL;
  if (cw_node_valid(cast_node)) cw_node_free(cast_node);
  cast_node = (cw_node){ 0 };
}

static int shown_sel[3] = { -1, -1, -1 }; /* the fighters the select screen shows now */
static void begin_match(void) {
  memset(G.d_ctl, 0, sizeof G.d_ctl);
  G.wait_t = 0;
  shown_sel[0] = -1;
  G.sim.training = G.training;
  G.sim.dummy = T_STAND;
  G.sim.cpu[0] = false;
  G.sim.cpu[1] = G.players < 2 || G.training == 2;
  G.sim.cpu_level[0] = 1;
  G.sim.cpu_level[1] = (uint8_t)G.difficulty;
  if (G.arcade && G.players < 2) G.sim.cpu_level[1] = (uint8_t)(G.arc_step == 3 ? (G.difficulty < 2 ? G.difficulty + 1 : 2) : G.difficulty); /* the mirror match: a level up */
  for (int p = 0; p < 2; p++) if (G.cpu_seat[p]) { G.sim.cpu[p] = true; G.sim.cpu_level[p] = (uint8_t)G.difficulty; } /* a seat nobody chose for */
  int d0 = G.sel[0], d1 = G.sel[1];
#ifdef FIGHTER_AUTOPILOT
  G.sim.cpu[0] = G.sim.cpu[1] = !G.training;
  if (G.training) G.sim.dummy = T_STAND;
#endif
  ft_game_start(&G.sim, &G.roster, d0, d1);
  memset(&ft_stat, 0, sizeof ft_stat);
  if (G.arcade && G.players < 2) G.d_arc[3] |= 1 << (G.arc_step == 3 ? 4 : d1);
  G.sim.rng = 12345u + (uint32_t)(d0 * 31 + d1 * 7 + G.sel_stage);
  load_match();
  G.state = GS_MATCH;
  G.result_t = 0;
}

static void backdrop(void) {
  /* the title and join screens sit over a stage with two fighters in their guard */
  G.sim.training = true;
  ft_game_start(&G.sim, &G.roster, 0, 2);
  G.sel_stage = 0;
  load_match();
  G.state = GS_SELECT;
}

/* the select screen: the two picked fighters face off on the chosen stage (the opponent shown is the one the computer
 * would pick in a one player game), the camera close on their faces */
static void select_scene(void) {
  int b = G.players < 2 ? (G.sel[0] + 1 + (G.difficulty + G.sel_stage) % (G.roster.n - 1)) % G.roster.n : G.sel[1];
  if (shown_sel[0] == G.sel[0] && shown_sel[1] == b && shown_sel[2] == G.sel_stage && G.mdl[0]) return;
  shown_sel[0] = G.sel[0]; shown_sel[1] = b; shown_sel[2] = G.sel_stage;
  G.sim.training = true;
  ft_game_start(&G.sim, &G.roster, G.sel[0], b);
  G.sim.f[0].x = -800; G.sim.f[1].x = 800; /* close together, turned three quarters to the camera, in their guard */
  G.sim.f[0].state = G.sim.f[1].state = S_IDLE;
  load_match();
  for (int i = 0; i < 2; i++) model_set_showcase(G.mdl[i], 0.9f);
}
/* back on the select screen: nobody locked in, every seat its player's again */
static void seats_reset(void) { G.ready[0] = G.ready[1] = 0; G.cpu_seat[0] = G.cpu_seat[1] = 0; G.wait_t = 0; }

static void start(int players, void *user) {
  (void)user;
  G.players = players;
  G.state = GS_SELECT;
  seats_reset();
  shown_sel[0] = -1;
  select_scene();
}

/* ── the select screen ──────────────────────────────────────────────── */

static int menu_pick(int p, int *v, int n) {
  if (cw_action_just_pressed(p, G.a_left) || (p == 1 && cw_action_just_pressed(0, G.k_left))) { *v = (*v + n - 1) % n; sfx(SFX_LAND, 0.4f, 1.6f); return 1; }
  if (cw_action_just_pressed(p, G.a_right) || (p == 1 && cw_action_just_pressed(0, G.k_right))) { *v = (*v + 1) % n; sfx(SFX_LAND, 0.4f, 1.6f); return 1; }
  return 0;
}
/* menus: A (kick) or Start confirms, B (block) backs out, X (punch) switches the mode */
static bool confirm(int p) { return cw_action_just_pressed(p, G.a_k) || cw_action_just_pressed(p, G.ui_start) || (p == 1 && cw_action_just_pressed(0, G.k_k)); }
static bool back_out(int p) { return cw_action_just_pressed(p, G.a_g) || (p == 1 && cw_action_just_pressed(0, G.k_g)); }
/* anything a player does on the select screen (player 2 on the keyboard is on pad 0's keys) */
static bool select_touched(int p) {
  const cw_action a[] = { G.a_left, G.a_right, G.a_up, G.a_down, G.a_p, G.a_k, G.a_g, G.ui_start };
  for (size_t i = 0; i < sizeof a / sizeof a[0]; i++) if (cw_action_just_pressed(p, a[i])) return true;
  return p == 1 && (cw_action_just_pressed(0, G.k_left) || cw_action_just_pressed(0, G.k_right) || cw_action_just_pressed(0, G.k_k) || cw_action_just_pressed(0, G.k_g));
}
/* Two seats, one locked in: the other gets SEAT_WAIT to choose (a countdown shows for the last SEAT_WARN), any press
 * on its pad starts the wait again. A seat that joined and was never played does not hold the other player on this
 * screen: the computer plays it, with the fighter it shows. */
enum { SEAT_WAIT = 60 * 12, SEAT_WARN = 60 * 6 };
static int seat_waiting(void) { return G.players > 1 && G.ready[0] != G.ready[1] ? (G.ready[0] ? 1 : 0) : -1; }

/* the arcade's fight k: the other three fighters in turn from the one after yours, then your own fighter (the mirror),
 * each on the next stage from the one chosen */
static void arcade_setup(int k) {
  G.arc_step = k;
  G.sel[1] = k < 3 ? (G.sel[0] + 1 + k) % G.roster.n : G.sel[0];
  G.sel_stage = (G.arc_stage0 + k) % STAGES;
}
/* the mode the punch button turns through on the select screen: versus, arcade (one player), training */
static void next_mode(void) {
  int m = G.training ? 2 : G.arcade ? 1 : 0;
  do m = (m + 1) % 3; while (m == 1 && G.players > 1);
  G.training = m == 2; G.arcade = m == 1;
}
static void update_select(void) {
  for (int p = 0; p < G.players && p < 2; p++) {
    if (G.ready[p]) {
      if (back_out(p)) G.ready[p] = 0;
      continue;
    }
    menu_pick(p, &G.sel[p], G.roster.n);
    if (p == 0) {
      if (cw_action_just_pressed(0, G.a_up)) { G.sel_stage = (G.sel_stage + STAGES - 1) % STAGES; sfx(SFX_LAND, 0.4f, 1.3f); }
      if (cw_action_just_pressed(0, G.a_down)) { G.sel_stage = (G.sel_stage + 1) % STAGES; sfx(SFX_LAND, 0.4f, 1.3f); }
      if (cw_action_just_pressed(0, G.a_p)) { next_mode(); sfx(SFX_BELL, 0.3f, 1.2f); }
      /* random: the cursor jumps to a fighter picked from the clock (any other than the one it is on) */
      if (cw_action_just_pressed(0, G.a_pk)) { G.sel[0] = (G.sel[0] + 1 + (int)(hash01(G.sim.tick * 2654435761u) * 2.999f)) % G.roster.n; sfx(SFX_LAND, 0.4f, 1.6f); }
      if (back_out(0) && G.players < 2) { G.difficulty = (G.difficulty + 1) % 3; G.diff_picked = 1; sfx(SFX_BELL, 0.3f, 1.0f); }
    }
    if (confirm(p)) { G.ready[p] = 1; sfx(SFX_ROUND, 0.6f, 1.0f); }
  }
  int w = seat_waiting();
  if (w < 0 || G.sw_seat_wait || select_touched(w)) G.wait_t = 0;
  else if (++G.wait_t >= SEAT_WAIT) { G.cpu_seat[w] = 1; G.ready[w] = 1; G.wait_t = 0; sfx(SFX_ROUND, 0.6f, 1.0f); }
  select_scene();
  bool all = G.ready[0] && (G.players < 2 || G.ready[1]);
  if (all) {
    if (G.players < 2) {
      /* the computer picks someone else, by a fixed rule so a replay agrees */
      G.sel[1] = (G.sel[0] + 1 + (G.difficulty + G.sel_stage) % (G.roster.n - 1)) % G.roster.n;
    }
    if (G.players > 1) G.arcade = 0;
    if (G.arcade) { G.arc_clear = G.arc_lost = 0; G.arc_stage0 = G.sel_stage; G.d_arc[3] = 0; arcade_setup(0); }
    begin_match();
  }
}

/* ── the tick ───────────────────────────────────────────────────────── */

static bool g_hold; /* test builds: "screen" holds the select or result screen for screenshots */
#if defined(FIGHTER_AUTOPILOT) || defined(FIGHTER_SCRIPTED)
static void cmd_screen(int argc, const char **argv, cw_reply *r);
#endif
static void fixed(void *u, double dt) {
  (void)u; (void)dt;
  /* the select and result panels are not play: Start confirms there (confirm), so the shell does not see that press and
   * does not pause; every other tick it runs as usual, and a pad that goes away frees its seat */
  bool panel = cw_shell_screen_now() == CW_SHELL_PLAY && G.state != GS_MATCH;
  bool pause_press = false;
  for (int p = 0; panel && p < cw_shell_players(); p++) pause_press |= cw_action_just_pressed(p, G.ui_pause);
  if (!pause_press && !cw_shell_update()) return;
#if defined(FIGHTER_AUTOPILOT) || defined(FIGHTER_SCRIPTED)
  static bool screen_env_done;
  if (!screen_env_done && getenv("FIGHTER_SCREEN")) { /* FIGHTER_SCREEN="select 1 3 0" or "result 0": the screen command at start */
    screen_env_done = true;
    static char buf[64]; const char *av[8]; int ac = 0;
    snprintf(buf, sizeof buf, "screen %s", getenv("FIGHTER_SCREEN"));
    for (char *t = strtok(buf, " _"); t && ac < 8; t = strtok(NULL, " _")) av[ac++] = t;
    if (ac > 1 && !strcmp(av[1], "result")) { G.sel[0] = 0; G.sel[1] = 1; begin_match(); }
    cmd_screen(ac, av, NULL);
  }
  if (G.state == GS_SELECT && !getenv("FIGHTER_SELECT") && !g_hold) {
    const char *e;
    G.sel[0] = (e = getenv("FIGHTER_P1")) ? atoi(e) & 3 : 0;
    G.sel[1] = (e = getenv("FIGHTER_P2")) ? atoi(e) & 3 : 2;
    G.sel_stage = (e = getenv("FIGHTER_STAGE")) ? atoi(e) % STAGES : 0;
    G.players = 2;
    G.training = getenv("FIGHTER_TRAINING") != NULL;
    G.show_boxes = getenv("FIGHTER_BOXES") != NULL;
#ifdef FIGHTER_TRAIN
    G.training = 1;
    G.show_boxes = 1;
#endif
    begin_match();
  }
#endif
  G.d_tick++;
  if (G.state == GS_SELECT) {
    if (cw_shell_join_press()) sfx(SFX_BELL, 0.4f, 1.3f); /* a pad with no seat: A takes one (players_changed) */
    else if (cw_shell_join_requested() && !G.ready[0]) { G.ready[0] = 1; sfx(SFX_ROUND, 0.6f, 1.0f); } /* its Start confirms for the seated player */
    if (cw_action_just_pressed(0, G.ui_quit)) { sfx(SFX_LAND, 0.4f, 1.0f); cw_shell_to_title(); return; } /* no pause menu on this panel */
    update_select();
    G.sim.tick++; /* the clock runs for blinks and idle motion */
    return;
  }
  if (G.state == GS_RESULT) {
    G.result_t++;
    if (G.result_t > 40 && confirm(0)) {
      if (G.arcade && G.arc_clear) { G.state = GS_SELECT; seats_reset(); shown_sel[0] = -1; } /* the ladder is done */
      else { if (G.arcade) arcade_setup(G.arc_step); begin_match(); } /* the next fight, or this one again */
    } else if (G.result_t > 40 && back_out(0)) { G.state = GS_SELECT; seats_reset(); shown_sel[0] = -1; }
#if defined(FIGHTER_AUTOPILOT)
    if (G.result_t > 200 && !g_hold) { G.sel_stage = (G.sel_stage + 1) % STAGES; G.sel[0] = (G.sel[0] + 1) % 4; G.sel[1] = (G.sel[1] + 1) % 4; begin_match(); }
#endif
    return;
  }
  /* training controls */
  if (G.training) {
    if (cw_action_just_pressed(0, G.tr_dummy)) { G.sim.dummy = (uint8_t)((G.sim.dummy + 1) % T_COUNT); sfx(SFX_BELL, 0.3f, 1.4f); }
    if (cw_action_just_pressed(0, G.tr_boxes)) G.show_boxes = !G.show_boxes;
    if (cw_action_just_pressed(0, G.tr_reset)) { ft_round_start(&G.sim); }
    if (cw_action_just_pressed(0, G.tr_mode)) { G.state = GS_SELECT; seats_reset(); shown_sel[0] = -1; return; }
  }
  ft_input in[2] = { read_pad(0), G.players > 1 ? read_pad(1) : (ft_input){ 0 } };
  /* the knockout that decides the match plays at half speed for its first 50 ticks: the sim simply takes every other
   * fixed tick off (nothing is read or decided during a knockout, so the match comes out the same tick for tick, only
   * later; ko.slow counts the ticks held, ko.end_hash is the sim at the match's end) */
  {
    const ft_game *g = &G.sim;
    bool deciding = (g->f[0].wins == 1 && g->f[1].hp <= 0) || (g->f[1].wins == 1 && g->f[0].hp <= 0);
    static int odd;
    if (g->phase == P_KO && g->phase_t < 50 && deciding && !G.training && !G.no_slowmo && (odd ^= 1)) { G.d_slow++; return; }
  }
  uint8_t was = G.sim.f[0].state, was_mv = G.sim.f[0].mv;
  if (G.sw_cpu_hp > 0 && G.sim.phase == P_INTRO) for (int i = 0; i < 2; i++) if (G.sim.cpu[i] && !G.sim.cpu[1 - i] && G.sim.f[i].hp > G.sw_cpu_hp) G.sim.f[i].hp = (int16_t)G.sw_cpu_hp;
  ft_step(&G.sim, in);
  {
    const ft_fighter *f = &G.sim.f[0];
    int32_t *c = G.d_ctl;
    c[0] += f->state == S_WALKF || f->state == S_WALKB;
    if (was != S_AIR && f->state == S_AIR && f->vy > 0) { c[1]++; if (f->jump_dir == 2) c[2]++; if (f->jump_dir == 0) c[3]++; }
    bool fresh_mv = f->state == S_ATTACK && (was != S_ATTACK || was_mv != f->mv);
    if (fresh_mv && (f->mv == M_8P || f->mv == M_8K)) c[4]++;
    if (fresh_mv && (f->mv == M_2P || f->mv == M_2K)) c[5]++;
    c[6] += f->state == S_CROUCH;
    if (f->state == S_DASHF && was != S_DASHF) c[7]++;
    if (f->y > c[8]) c[8] = f->y;
    if (fresh_mv && (f->mv == M_JP || f->mv == M_JK)) c[9]++;
  }
  if (G.sim.phase == P_MATCH_END) {
    G.state = GS_RESULT;
    G.d_end_hash = ft_hash(&G.sim);
    if (G.sim.match_winner >= 0) model_set_showcase(G.mdl[G.sim.match_winner], 0.9f);
    else for (int i = 0; i < 2; i++) model_set_showcase(G.mdl[i], 0.9f);
    G.result_t = 0;
    if (SAVE && G.sim.match_winner >= 0) { SAVE->wins[G.sim.f[G.sim.match_winner].def & 3]++; SAVE->matches++; cw_shell_save_commit(); }
    /* the first win against the easy computer: the next match is on Normal (the player can still pick Easy) */
    G.stepped_up = 0;
    int w = G.sim.match_winner;
    if (w >= 0 && !G.sim.cpu[w] && G.sim.cpu[1 - w] && !G.training && !G.arcade && !G.diff_picked && G.difficulty == 0) { G.difficulty = 1; G.stepped_up = 1; }
    /* arcade: a win moves up the ladder (the fourth clears it); a loss stays on this fight */
    if (G.arcade && G.players < 2) {
      G.arc_lost = w != 0;
      if (w == 0) { if (G.arc_step == 3) G.arc_clear = 1; else G.arc_step++; }
    }
  }
  /* debug state */
  const ft_game *g = &G.sim;
  G.d_arc[0] = G.arcade; G.d_arc[1] = G.arc_step + G.arc_clear; G.d_arc[2] = G.arc_clear;
  G.d_state = G.state; G.d_phase = g->phase; G.d_round = g->round_no; G.d_hash = ft_hash(g); G.d_combo = g->f[1].combo_hits > g->f[0].combo_hits ? g->f[1].combo_hits : g->f[0].combo_hits;
  G.d_adv = g->adv_valid ? g->adv : 0;
  for (int i = 0; i < 2; i++) {
    G.d_guard[i] = g->f[i].guard;
    G.d_hp[i] = g->f[i].hp; G.d_meter[i] = g->f[i].meter; G.d_wins[i] = g->f[i].wins; G.d_pos[i * 2] = g->f[i].x; G.d_pos[i * 2 + 1] = g->f[i].y; G.d_fstate[i] = g->f[i].state; G.d_def[i] = g->f[i].def;
  }
  G.d_hitcount = fx_count_hit; G.d_blockcount = fx_count_block;
  /* this match: hits, blocks, parries, guard breaks, highs ducked, throws */
  G.d_stats[0] = (int32_t)ft_stat.hits; G.d_stats[1] = (int32_t)ft_stat.blocks; G.d_stats[2] = (int32_t)ft_stat.parries;
  G.d_stats[3] = (int32_t)ft_stat.guard_breaks; G.d_stats[4] = (int32_t)ft_stat.whiffs_high; G.d_stats[5] = (int32_t)ft_stat.throws;
}

/* ── render-rate: poses, camera, effects, sound ─────────────────────── */


static void sound_events(void) {
  const ft_game *g = &G.sim;
  uint32_t n = g->ev_seq;
  if (n - G.ev_seen > 64) G.ev_seen = n - 64;
  for (; G.ev_seen < n; G.ev_seen++) {
    const ft_event *e = &g->ev[G.ev_seen & 63];
    switch (e->type) {
    case EV_HIT: sfx(e->power >= 3 ? SFX_HIT_H : e->power >= 1 ? SFX_HIT_M : SFX_HIT_L, 0.9f, 0.95f + 0.1f * hash01(G.ev_seen)); break;
    case EV_BLOCK: sfx(SFX_BLOCK, 0.8f, 0.9f + 0.2f * hash01(G.ev_seen)); break;
    case EV_THROW: sfx(SFX_HIT_H, 1.0f, 0.8f); break;
    case EV_FIRE: sfx(SFX_FIRE, 0.7f, e->power ? 0.7f : 1.0f); break;
    case EV_SUPER: sfx(SFX_SUPER, 1.0f, 1.0f); break;
    case EV_KO: sfx(SFX_KO, 1.0f, 1.0f); break;
    case EV_DUST: sfx(SFX_LAND, 0.35f, 1.0f); break;
    case EV_WALL: sfx(SFX_LAND, 0.7f, 0.7f); break;
    case EV_ROUND: sfx(SFX_BELL, 0.7f, 1.0f); break;
    case EV_CLASH: case EV_TECH: sfx(SFX_BLOCK, 0.8f, 1.3f); break;
    case EV_PARRY: sfx(SFX_BLOCK, 1.0f, 1.6f); sfx(SFX_BELL, 0.5f, 1.8f); break;
    case EV_GUARDBREAK: sfx(SFX_HIT_H, 1.0f, 0.7f); sfx(SFX_BLOCK, 0.9f, 0.6f); break;
    case EV_WHIFF: case EV_JUGGLE_SPENT: sfx(SFX_WHOOSH_H, 0.5f, 0.8f); break;
    default: break;
    }
  }
  /* a whoosh when a strike starts */
  for (int i = 0; i < 2; i++) {
    const ft_fighter *f = &g->f[i];
    if (f->state == S_ATTACK && f->t == 1) sfx(FT_IS_KICK(f->mv) || f->mv >= M_JK || f->mv == M_6P ? SFX_WHOOSH_H : SFX_WHOOSH_L, 0.5f, 1.0f);
  }
}

/* close-up camera for screenshots (debug command "cam", FIGHTER_CAM in test builds): zoom 0 is the normal framing;
 * otherwise it frames one fighter (0 or 1) or both (2) from closer, at chest height, turned by yaw radians */
static float g_cam[3];
/* the shake check (cam.offset): the camera as drawn (the camera node's global transform) against the framing (cw_frame_follow
 * and the settled look-at above the fighters) on the frames within half a second of an impact (a hit, block, throw, wall
 * splat, super or KO). cam.offset: those frames, the ones whose drawn camera was off the framing by over 0.1 mm or 0.1
 * milliradian, and the largest offset (in 0.1 mm and 0.1 milliradian units, eye plus look). Nothing moves the camera on an
 * impact: [n, 0, 0]. */
static int32_t cam_offset[3];
static void offsetgate(t3_vec3 pos, t3_vec3 tgt) {
  static int seen, frames;
  if (fx_count_impact != seen) seen = fx_count_impact, frames = 30;
  if (frames <= 0) return;
  frames--;
  const t3_mat4 *m = cw_node3d_global_transform(G.camera);
  if (!m) return;
  t3_vec3 d = { tgt.x - pos.x, tgt.y - pos.y, tgt.z - pos.z };
  float dl = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
  d.x /= dl, d.y /= dl, d.z /= dl;
  float fl = sqrtf(m->e[8] * m->e[8] + m->e[9] * m->e[9] + m->e[10] * m->e[10]);
  float ex = m->e[12] - pos.x, ey = m->e[13] - pos.y, ez = m->e[14] - pos.z;
  float fx = -m->e[8] / fl - d.x, fy = -m->e[9] / fl - d.y, fz = -m->e[10] / fl - d.z;
  float off = sqrtf(ex * ex + ey * ey + ez * ez) + sqrtf(fx * fx + fy * fy + fz * fz);
  cam_offset[0]++;
  if (off > 1e-4f) cam_offset[1]++;
  if ((int32_t)(off * 1e4f) > cam_offset[2]) cam_offset[2] = (int32_t)(off * 1e4f);
}
/* A super winding up: from its first frame through its startup the camera moves in on the attacker (the framing's
 * points and distance change; cw_frame_follow's spring carries the move, so it glides in and back out, never a cut or
 * a jolt) and their rim light takes their colour (stage.c). The weight eases in over 8 frames and out over 20 once the
 * super is out. Render only: it reads the sim, nothing in the sim reads it. */
int super_cine_who = -1;
float super_cine_w;
static void super_cine(float dt) {
  const ft_game *g = &G.sim;
  int who = -1;
  for (int i = 0; i < 2; i++) {
    const ft_fighter *f = &g->f[i];
    if (G.state == GS_MATCH && f->state == S_ATTACK && f->mv == M_SUPER && f->t <= g->roster->d[f->def].mv[M_SUPER].su) who = i;
  }
  if (who >= 0) { super_cine_who = who; super_cine_w = fminf(1, super_cine_w + dt * 60.0f / 8.0f); }
  else { super_cine_w = fmaxf(0, super_cine_w - dt * 60.0f / 20.0f); if (super_cine_w <= 0) super_cine_who = -1; }
}
static void update_camera(float dt, float aspect) {
  if (!cw_node_valid(G.camera)) return;
  const ft_game *g = &G.sim;
  cw_frame_point pts[2];
  for (int i = 0; i < 2; i++) pts[i] = (cw_frame_point){ (float)g->f[i].x / 1000.0f, 0.95f + (float)g->f[i].y / 1000.0f, 0, 0.8f };
  cw_frame_params p = { .yaw = 0, .pitch = -0.04f, .fov_y = 34.0f * 0.0174533f, .aspect = aspect, .margin = 0.12f, .min_dist = 5.4f, .max_dist = 15.0f };
  int npts = 2;
  if ((G.state == GS_SELECT || G.state == GS_RESULT) && g_cam[0] <= 0) {
    /* portraits: a longer lens from further back (no wide-angle swelling of heads and fists), the fighters at three
     * quarter body. The select screen frames both from the knees up with the heads in the band between the title and the
     * cards; the result screen frames the winner alone a little closer, from a little to the side of the way they face.
     * The camera sits at chest height and looks slightly up at the faces. */
    stage_set_showcase(true);
    int w = G.state == GS_RESULT ? g->match_winner : -1;
    float fov = w >= 0 ? 24.0f : 25.0f, half = w >= 0 ? 0.78f : 1.12f; /* half the frame's height in metres */
    float cx = w >= 0 ? (float)g->f[w].x / 1000.0f : ((float)g->f[0].x + (float)g->f[1].x) * 0.0005f;
    float cy = w >= 0 ? 1.5f : 1.4f, yaw = w >= 0 ? (g->f[w].facing > 0 ? 0.32f : -0.32f) : 0.0f;
    float dist = half / tanf(fov * 0.5f * 0.0174533f);
    t3_vec3 tgt = { cx, cy, 0 }, pos = { cx + sinf(yaw) * dist, 1.3f, cosf(yaw) * dist };
    cw_node_set(G.camera, "fov", cw_val_float(fov));
    /* a shallow depth of field: the fighters sharp, the crowd and the stage behind them soft */
    cw_node post = cw_node_get(G.stage_node, "Post");
    if (cw_node_valid(post)) {
      cw_node_set(post, "dof", cw_val_bool(true));
      cw_node_set(post, "dof_focus_distance", cw_val_float(dist));
      cw_node_set(post, "dof_focus_range", cw_val_float(1.3f));
      cw_node_set(post, "dof_blur", cw_val_float(7.0f));
    }
    cw_node3d_set_position(G.camera, pos);
    cw_node3d_look_at(G.camera, tgt, (t3_vec3){ 0, 1, 0 });
    G.d_cam[0] = pos.x; G.d_cam[1] = pos.y; G.d_cam[2] = pos.z;
    memset(&G.fs, 0, sizeof G.fs);
    return;
  }
  cw_node_set(G.camera, "fov", cw_val_float(34.0f));
  { cw_node post = cw_node_get(G.stage_node, "Post"); if (cw_node_valid(post)) cw_node_set(post, "dof", cw_val_bool(false)); }
  if (G.state == GS_SELECT || G.state == GS_RESULT) { /* faces: both fighters' heads on the select screen, the winner's on the result screen */
    int w = G.state == GS_RESULT ? g->match_winner : -1;
    if (w >= 0) { pts[0] = (cw_frame_point){ (float)g->f[w].x / 1000.0f, 1.62f, 0, 0.55f }; npts = 1; }
    else for (int i = 0; i < 2; i++) pts[i] = (cw_frame_point){ (float)g->f[i].x / 1000.0f, 1.3f, 0, 0.5f }; /* aimed low: the heads sit above the cards */
    p.min_dist = w >= 0 ? 2.4f : 2.6f; p.margin = 0.02f; p.yaw = w >= 0 ? (g->f[w].facing > 0 ? 0.35f : -0.35f) : 0;
    stage_set_showcase(true);
  } else stage_set_showcase(false);
  if (super_cine_who >= 0 && G.state == GS_MATCH && g_cam[0] <= 0) {
    /* the super: the attacker's upper body fills more of the picture, the other fighter still in it */
    float w = super_cine_w, ax = (float)g->f[super_cine_who].x / 1000.0f;
    pts[super_cine_who] = (cw_frame_point){ ax, 1.25f + (float)g->f[super_cine_who].y / 1000.0f, 0, 0.8f - 0.25f * w };
    p.min_dist = 5.4f - 2.0f * w;
    p.yaw = 0.22f * w * (float)g->f[super_cine_who].facing;
  }
  if (g_cam[0] > 0) {
    int who = (int)g_cam[1];
    if (who < 2) { pts[0] = (cw_frame_point){ (float)g->f[who].x / 1000.0f, 1.25f + (float)g->f[who].y / 1000.0f, 0, 0.55f }; npts = 1; }
    p.min_dist = 5.4f / g_cam[0]; p.margin = 0.02f; p.yaw = g_cam[2];
  }
  cw_frame_result fit, cam;
  if (!cw_frame_fit(&p, pts, npts, &fit)) return;
  cw_frame_follow(&G.fs, &p, &fit, dt, 7.0f, &cam);
  /* a settled look-at a little above the fighters so the stage reads under them. The framing is the whole camera: it
   * never shakes (an impact is felt in the pads, fx.c) */
  float lift = g_cam[0] > 0 ? 0.45f / g_cam[0] : 0.45f;
  t3_vec3 pos = { cam.pos[0], cam.pos[1] + lift, cam.pos[2] };
  t3_vec3 tgt = { cam.target[0], cam.target[1] + 0.15f, cam.target[2] };
  cw_node3d_set_position(G.camera, pos);
  cw_node3d_look_at(G.camera, tgt, (t3_vec3){ 0, 1, 0 });
  G.d_cam[0] = pos.x; G.d_cam[1] = pos.y; G.d_cam[2] = pos.z;
  offsetgate((t3_vec3){ cam.pos[0], cam.pos[1] + lift, cam.pos[2] }, (t3_vec3){ cam.target[0], cam.target[1] + 0.15f, cam.target[2] });
}

static void update_debug_boxes(void) {
  for (int i = 0; i < 10; i++) if (G.dbg_box[i]) G.dbg_box[i]->base.visible = false;
  if (!G.show_boxes || !G.training || !G.dbg_root) return;
  int k = 0;
  const ft_game *g = &G.sim;
  for (int i = 0; i < 2 && k < 8; i++) {
    ft_box hb[2];
    int n = ft_hurtboxes(g, i, hb);
    for (int j = 0; j < n && k < 9; j++, k++) {
      t3_mesh *m = G.dbg_box[k];
      m->base.visible = true;
      m->base.position = (t3_vec3){ (float)(hb[j].x0 + hb[j].x1) * 0.0005f, (float)(hb[j].y0 + hb[j].y1) * 0.0005f, 0.5f };
      m->base.scale = (t3_vec3){ (float)(hb[j].x1 - hb[j].x0) * 0.001f, (float)(hb[j].y1 - hb[j].y0) * 0.001f, 0.05f };
      m->materials[0]->color = (t3_color){ 0.1f, 0.4f, 1.0f };
    }
    ft_box ab;
    if (ft_hitbox_active(g, i, &ab) && k < 10) {
      t3_mesh *m = G.dbg_box[k++];
      m->base.visible = true;
      m->base.position = (t3_vec3){ (float)(ab.x0 + ab.x1) * 0.0005f, (float)(ab.y0 + ab.y1) * 0.0005f, 0.55f };
      m->base.scale = (t3_vec3){ (float)(ab.x1 - ab.x0) * 0.001f, (float)(ab.y1 - ab.y0) * 0.001f, 0.05f };
      m->materials[0]->color = (t3_color){ 1.0f, 0.15f, 0.1f };
    }
  }
  for (int p = 0; p < 8 && k < 10; p++) {
    const ft_proj *P = &g->proj[p];
    if (!P->live) continue;
    t3_mesh *m = G.dbg_box[k++];
    m->base.visible = true;
    m->base.position = (t3_vec3){ (float)P->x * 0.001f, (float)P->y * 0.001f, 0.55f };
    m->base.scale = (t3_vec3){ (float)P->size * 0.002f, (float)P->size * 0.002f, 0.05f };
    m->materials[0]->color = (t3_color){ 1.0f, 0.15f, 0.1f };
  }
  G.d_boxes = k;
}

static void update(void *u, double dtd) {
  (void)u;
  float dt = (float)dtd;
  if (!G.mdl[0]) return;
  cw_shell_screen sc = cw_shell_screen_now();
  if (sc != CW_SHELL_PLAY) fx_rumble_stop(); /* the menus and pause: the motors stop */
  if (sc != CW_SHELL_PLAY && sc != CW_SHELL_TITLE && sc != CW_SHELL_JOIN) return;
  pool_frame();
  /* a deciding round (a knockout ends the match) with a fighter under a quarter of their life: the music lifts a
   * semitone and comes up, and eases back when the round is over */
  if (!CW_HANDLE_IS_NULL(G.music_voice)) {
    const ft_game *g = &G.sim;
    bool tense = false;
    if (G.state == GS_MATCH && !g->training && (g->f[0].wins == 1 || g->f[1].wins == 1) && (g->phase == P_FIGHT || g->phase == P_KO))
      for (int i = 0; i < 2; i++) tense |= g->f[i].hp * 4 < g->roster->d[g->f[i].def].hp;
    static float lift;
    lift += ((tense ? 1.0f : 0.0f) - lift) * (1 - cw_exp(-dt * 1.5f));
    G.d_tense = tense;
    cw_voice_set_pitch(G.music_voice, 1.0f + 0.0595f * lift);
    cw_voice_set_volume(G.music_voice, 0.55f + 0.2f * lift);
  }
  for (int i = 0; i < 2; i++) model_pose(G.mdl[i], &G.sim, i, dt);
  super_cine(dt);
  G.d_spring = model_spring_speed(G.mdl[0]) + model_spring_speed(G.mdl[1]);
  stage_frame(&G.sim, dt);
  fx_frame(&G.sim, dt);
  sound_events();
  cw_vec2 cs = cw_r2d_canvas_size();
  update_camera(dt, cs.x / cs.y);
  /* the eyes: at the camera on the select and result screens, at each other in a fight */
  for (int i = 0; i < 2; i++) {
    t3_vec3 at;
    if (G.state != GS_MATCH) at = (t3_vec3){ G.d_cam[0], G.d_cam[1], G.d_cam[2] };
    else at = model_joint(G.mdl[1 - i], J_HEAD);
    model_look_at(G.mdl[i], at);
  }
  update_debug_boxes();
}

/* ── HUD ────────────────────────────────────────────────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return cw_rgba(r, g, b, a); }
static void text(cw_font f, const char *s, float x, float y, float size, cw_color c, cw_text_align al) {
  if (al != CW_ALIGN_LEFT) {
    float w = cw_font_measure(f, s, &(cw_text_style){ .size = size }).width;
    x -= al == CW_ALIGN_CENTER ? w * 0.5f : w;
  }
  cw_text_style st = { .size = size, .color = c, .align = CW_ALIGN_LEFT, .outline_color = rgba(0, 0, 0, 0.85f), .outline_width = size * 0.07f };
  cw_r2d_text(f, s, (cw_vec2){ x, y }, &st);
}
/* text with button prompts in it (cartwheel/prompt.h markup: "{atk_k} lock in") for one player, or -1 for both */
static void ptext(cw_font f, const char *s, int player, float x, float y, float size, cw_color c, cw_text_align al) {
  cw_prompt_text(f, s, player, (cw_vec2){ x, y }, size, c, al);
}
/* how much the player's text size grows text asked for at `size` (1 at 100%): the HUD and the panels space their
 * lines by it, so at 100% everything sits where it always did and at 200% lines move apart as the text grows */
static float grow(cw_font f, float size) { return cw_text_drawn_size(f, size) / size; }
/* the round pips under the health bars: under the name at 100%; beside it, on the name's row, once the text grows (the
 * names sit at the far ends, the pips by the timer) */
static float pip_y(float s) {
  float k = grow(G.font_bold, 20 * s);
  return k < 1.001f ? 90 * s : 58 * s + 13 * s * k;
}
static float text_w(cw_font f, const char *s, float size) { return cw_font_measure(f, s, &(cw_text_style){ .size = size }).width; }
/* a hint line of parts split by `sep` (runs of spaces), wrapped onto as many lines as it needs to fit `maxw`: the
 * text with '\n' between the lines in out. Returns the number of lines */
static int wrap_parts(cw_font f, const char *s, const char *sep, int player, float size, float maxw, char *out, size_t cap) {
  size_t sl = strlen(sep), n = 0;
  int lines = 1;
  char line[256] = "";
  out[0] = 0;
  const char *p = s;
  while (*p) {
    const char *e = strstr(p, sep);
    size_t len = e ? (size_t)(e - p) : strlen(p);
    char part[256];
    snprintf(part, sizeof part, "%.*s", (int)len, p);
    char trial[512];
    snprintf(trial, sizeof trial, "%s%s%s", line, line[0] ? sep : "", part);
    if (line[0] && cw_prompt_text_size(f, trial, player, size).x > maxw) { /* this part starts a new line */
      n += (size_t)snprintf(out + n, n < cap ? cap - n : 0, "%s\n", line);
      lines++;
      snprintf(line, sizeof line, "%s", part);
    } else snprintf(line, sizeof line, "%.*s", (int)sizeof line - 1, trial);
    p = e ? e + sl : p + len;
    while (e && *p == ' ') p++; /* a longer run of spaces than sep */
  }
  snprintf(out + n, n < cap ? cap - n : 0, "%s", line);
  return lines;
}

static void bar(float x, float y, float w, float h, float fill, float lag, cw_color c, bool right_to_left) {
  cw_r2d_round_rect(cw_rect_make(x - 3, y - 3, w + 6, h + 6), h * 0.35f, 6, rgba(0.02f, 0.02f, 0.03f, 0.9f));
  cw_r2d_round_rect(cw_rect_make(x, y, w, h), h * 0.3f, 6, rgba(0.15f, 0.12f, 0.12f, 1));
  float lw = w * lag, fw = w * fill;
  cw_r2d_rect(cw_rect_make(right_to_left ? x + w - lw : x, y, lw, h), rgba(0.95f, 0.8f, 0.2f, 1));
  cw_r2d_rect(cw_rect_make(right_to_left ? x + w - fw : x, y, fw, h), c);
  cw_r2d_rect(cw_rect_make(right_to_left ? x + w - fw : x, y, fw, h * 0.4f), rgba(1, 1, 1, 0.22f));
}

static const char *btn_name(int b) { return b == 0 ? "P" : b == 1 ? "K" : "G"; }

/* the move list of a fighter: every normal by its direction and button, the throw, the specials and the super. Laid
 * out from the measured text in the box (x right edge, y top) and no lower than y_max: when the rows do not all fit
 * (a big text size) it shows them a page at a time, turning every few seconds */
static float draw_move_list(const ft_def *d, float right, float y, float y_max, float s, bool draw) {
  static const int ORDER[] = { M_8P, M_5P, M_2P, M_6P, M_8K, M_5K, M_2K, M_6K, M_JP, M_JK, M_THROW };
  static const char *const INPUT[] = { "up + P", "P", "down + P", "fwd + P", "up + K", "K", "down + K", "fwd + K", "jump, P", "jump, K", "P + K" };
  enum { NN = (int)(sizeof ORDER / sizeof ORDER[0]), ROWS = NN + 5 };
  const float ts = 13 * s, k = grow(G.font_ui, ts), kh = grow(G.font_bold, 16 * s), lh = 15 * s * k;
  /* the columns: the input, the move's name, its level (normals) */
  char spec_in[4][24];
  float in_w = 0, sp_w = 0, name_w = 0, lv_w = 0;
  for (int i = 0; i < NN; i++) {
    in_w = fmaxf(in_w, text_w(G.font_ui, INPUT[i], ts));
    name_w = fmaxf(name_w, text_w(G.font_ui, d->mv[ORDER[i]].name, ts));
    if (ORDER[i] != M_THROW) lv_w = fmaxf(lv_w, text_w(G.font_ui, ft_level_name(d->mv[ORDER[i]].lv), ts));
  }
  float spname_w = 0;
  for (int i = 0; i < 4; i++) {
    ft_special_text(d, i, spec_in[i], sizeof spec_in[i]);
    sp_w = fmaxf(sp_w, text_w(G.font_ui, spec_in[i], ts));
    spname_w = fmaxf(spname_w, text_w(G.font_ui, d->mv[M_SP1 + i].name, ts));
  }
  char head[64];
  snprintf(head, sizeof head, "%s: moves", d->name);
  const char *block = "Block: G   low: down + G";
  float col_name = fmaxf(72 * s, in_w + 10 * s * k), col_sp = fmaxf(100 * s, sp_w + 10 * s * k);
  float w = fmaxf(248 * s, 20 * s + fmaxf(fmaxf(col_name + name_w + 12 * s * k + lv_w, col_sp + spname_w),
                                         fmaxf(text_w(G.font_bold, head, 16 * s) + 50 * s * k, text_w(G.font_ui, block, ts))));
  float x = right - w;
  if (!draw) return w;
  /* how many rows fit: the header, then the rows */
  float top = 30 * s * kh, avail = y_max - y - top - 10 * s;
  int fit = (int)(avail / lh);
  if (fit > ROWS) fit = ROWS;
  if (fit < 1) fit = 1;
  int pages = (ROWS + fit - 1) / fit, page = 0;
  if (pages > 1) {
    static uint32_t frames;
    frames++;
    page = (int)((frames / 240u) % (uint32_t)pages); /* four seconds a page */
  }
  int r0 = page * fit, r1 = r0 + fit < ROWS ? r0 + fit : ROWS;
  float h = pages > 1 ? top + (float)fit * lh + 10 * s : (float)(NN + 6) * lh + 34 * s + (top - 30 * s);
  cw_r2d_round_rect(cw_rect_make(x, y, w, h), 10 * s, 6, rgba(0.02f, 0.03f, 0.05f, 0.72f));
  text(G.font_bold, head, x + 10 * s, y + 6 * s, 16 * s, rgba(1, 0.9f, 0.6f, 1), CW_ALIGN_LEFT);
  if (pages > 1) {
    char pg[16];
    snprintf(pg, sizeof pg, "%d/%d", page + 1, pages);
    text(G.font_ui, pg, x + w - 10 * s, y + 6 * s, ts, rgba(0.7f, 0.8f, 0.9f, 1), CW_ALIGN_RIGHT);
  }
  float ly = y + top;
  for (int r = r0; r < r1; r++, ly += lh) {
    if (r < NN) {
      const ft_move *m = &d->mv[ORDER[r]];
      text(G.font_ui, INPUT[r], x + 10 * s, ly, ts, rgba(0.75f, 0.85f, 1, 1), CW_ALIGN_LEFT);
      text(G.font_ui, m->name, x + 10 * s + col_name, ly, ts, rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
      if (ORDER[r] != M_THROW) {
        cw_color c = m->lv == LV_HIGH ? rgba(1, 0.75f, 0.4f, 1) : m->lv == LV_LOW ? rgba(0.5f, 0.85f, 1, 1) : rgba(0.75f, 0.95f, 0.6f, 1);
        text(G.font_ui, ft_level_name(m->lv), x + w - 10 * s, ly, ts, c, CW_ALIGN_RIGHT);
      }
    } else if (r < NN + 4) {
      if (r == NN && pages == 1) ly += 4 * s;
      int sp = r - NN;
      text(G.font_ui, spec_in[sp], x + 10 * s, ly, ts, rgba(1, 0.85f, 0.45f, 1), CW_ALIGN_LEFT);
      text(G.font_ui, d->mv[M_SP1 + sp].name, x + 10 * s + col_sp, ly, ts, rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
    } else text(G.font_ui, block, x + 10 * s, ly + (pages == 1 ? 2 * s : 0), ts, rgba(0.7f, 0.8f, 0.9f, 1), CW_ALIGN_LEFT);
  }
  return w;
}

static void draw_training(float W, float H, float s) {
  const ft_game *g = &G.sim;
  char lines[5][200];
  const ft_def *d = &g->roster->d[g->f[g->last_who].def];
  const ft_move *lm = &d->mv[g->last_move];
  static const char *const RESULT[6] = { "", "hit", "blocked", "parried", "ducked under", "guard break" };
  snprintf(lines[0], sizeof lines[0], "%s %s: %s (%s)  %s", d->name, FT_MOVE_ID[g->last_move], lm->name, lm->flags & FM_THROW ? "throw" : ft_level_name(lm->lv), RESULT[g->last_result < 6 ? g->last_result : 0]);
  snprintf(lines[1], sizeof lines[1], "Startup %d   Active %d   Recovery %d   Total %d", g->last_su, g->last_ac, g->last_rc, g->last_su + g->last_ac + g->last_rc);
  if (g->adv_valid) snprintf(lines[2], sizeof lines[2], "Frame advantage %+d   Damage %d   Combo %d", g->adv, g->last_dmg, g->last_combo);
  else snprintf(lines[2], sizeof lines[2], "Frame advantage --   Damage %d   Combo %d", g->last_dmg, g->last_combo);
  static const char *const DUMMY[T_COUNT] = { "Off", "Stand", "Block", "Block low", "Jump", "Block all", "CPU" };
  snprintf(lines[3], sizeof lines[3], "Dummy: %s ({tr_dummy})   Hitboxes: %s ({tr_boxes})", DUMMY[g->dummy < T_COUNT ? g->dummy : 0], G.show_boxes ? "on" : "off");
  /* the input history of player 1: direction digits and buttons (P punch, K kick, G block), newest first */
  char *h = lines[4];
  int n = 0;
  uint8_t last = 0;
  h[0] = 0;
  const cw_cmd_buf *cb = &g->f[0].cb;
  uint16_t lastb = 0;
  for (int a = 0; a < 90 && n < 14; a++) {
    uint32_t idx = cb->count - 1 - (uint32_t)a;
    if ((int)cb->count - 1 - a < 0) break;
    const cw_cmd_frame *f = &cb->f[idx & (CW_CMD_HISTORY - 1)];
    if (f->dir != last || f->buttons != lastb || a == 0) {
      char tmp[24];
      int len = snprintf(tmp, sizeof tmp, "%d", f->dir);
      for (int bb = 0; bb < 3; bb++) if (f->buttons & (1 << bb)) { tmp[len++] = btn_name(bb)[0]; }
      tmp[len] = 0;
      if (f->dir != 5 || f->buttons) { strncat(h, tmp, 200 - strlen(h) - 2); strncat(h, " ", 200 - strlen(h) - 1); n++; }
      last = f->dir; lastb = f->buttons;
    }
  }
  /* the panel: rows spaced by their drawn size, as wide as its widest row, bottom-centred over the meters and clear of
   * the SUPER labels. The move list runs down the right: when the two would meet the panel moves left of it, and when
   * there is no room for that (a big text size) the panel takes the width left of the list and its rows wrap */
  static const float SIZE[5] = { 17, 16, 16, 16, 14 };
  static const float GAP[5] = { 24, 24, 24, 22, 22 }; /* from this row's top to the next row's top at 100% */
  const ft_def *md = &g->roster->d[g->f[0].def];
  float meter_top = H - 37 * s - 19 * s * grow(G.font_ui, 15 * s);
  float list_y = 64 * s + 32 * s * grow(G.font_bold, 20 * s) + 8 * s * (grow(G.font_bold, 20 * s) - 1), list_right = W - 14 * s;
  float list_x = list_right - draw_move_list(md, list_right, list_y, H, s, false);
  float wid = 0;
  for (int i = 0; i < 5; i++) {
    float lw = i == 3 ? cw_prompt_text_size(G.font_ui, lines[i], 0, SIZE[i] * s).x : text_w(G.font_ui, lines[i], SIZE[i] * s);
    wid = fmaxf(wid, lw);
  }
  float pw = fminf(W - 28 * s, fmaxf(500 * s, wid + 28 * s)), x = W * 0.5f - pw * 0.5f;
  if (x + pw > list_x - 8 * s) {
    if (list_x - 8 * s - pw >= 14 * s) x = list_x - 8 * s - pw;
    else x = 14 * s, pw = list_x - 22 * s;
  }
  float tw = pw - 28 * s;
  char dummy[200];
  wrap_parts(G.font_ui, lines[3], "   ", 0, SIZE[3] * s, tw, dummy, sizeof dummy);
  snprintf(lines[3], sizeof lines[3], "%s", dummy);
  float rows_h[5], ph = 8 * s;
  for (int i = 0; i < 5; i++) {
    float kk = grow(G.font_ui, SIZE[i] * s);
    float mh = i == 3 ? cw_prompt_text_size(G.font_ui, lines[i], 0, SIZE[i] * s).y : cw_font_measure(G.font_ui, lines[i], &(cw_text_style){ .size = SIZE[i] * s, .wrap_width = tw }).height;
    float line = i == 3 ? cw_prompt_text_size(G.font_ui, "M", 0, SIZE[i] * s).y : cw_font_measure(G.font_ui, "M", &(cw_text_style){ .size = SIZE[i] * s }).height;
    float one = GAP[i] * s + line - line / kk; /* its gap at 100%, and what its line grew by */
    rows_h[i] = one + fmaxf(0, mh - line); /* a wrapped row takes its extra lines */
    ph += i < 4 ? rows_h[i] : fmaxf(mh, line) + 6 * s; /* the last row: its text and a margin */
  }
  ph = fmaxf(124 * s, ph);
  float y = fminf(H * 0.70f, meter_top - 4 * s - ph), list_bottom = meter_top - 4 * s;
  float pips = pip_y(s) + 13 * s; /* under the round pips, while that leaves it over the SUPER labels */
  if (y < pips && pips + ph <= meter_top) y = pips;
  cw_r2d_round_rect(cw_rect_make(x, y, pw, ph), 10 * s, 6, rgba(0.02f, 0.03f, 0.05f, 0.75f));
  static const float LC[5][3] = { { 1, 0.95f, 0.7f }, { 0.85f, 0.9f, 1 }, { 0, 0, 0 }, { 0.7f, 0.8f, 0.9f }, { 0.6f, 0.75f, 0.6f } };
  float ly = y + 8 * s;
  for (int i = 0; i < 5; i++) {
    cw_color c = rgba(LC[i][0], LC[i][1], LC[i][2], 1);
    if (i == 2) c = g->adv_valid && g->adv > 0 ? rgba(0.5f, 1, 0.6f, 1) : rgba(1, 0.65f, 0.55f, 1);
    if (i == 3) ptext(G.font_ui, lines[i], 0, x + 14 * s, ly, SIZE[i] * s, c, CW_ALIGN_LEFT);
    else cw_r2d_text(G.font_ui, lines[i], (cw_vec2){ x + 14 * s, ly }, &(cw_text_style){ .size = SIZE[i] * s, .color = c, .wrap_width = tw, .outline_color = rgba(0, 0, 0, 0.85f), .outline_width = SIZE[i] * s * 0.07f });
    ly += rows_h[i];
  }
  draw_move_list(md, list_right, list_y, list_bottom, s, true);
}

static void draw_hud(float W, float H, float s) {
  const ft_game *g = &G.sim;
  float bw = W * 0.38f, bh = 22 * s, by = 26 * s, gap = 70 * s;
  for (int i = 0; i < 2; i++) {
    const ft_fighter *f = &g->f[i];
    const ft_def *d = &g->roster->d[f->def];
    float target = (float)f->hp / (float)d->hp;
    G.shown_hp[i] += (target - G.shown_hp[i]) * 0.5f;
    G.lag_hp[i] += (target - G.lag_hp[i]) * 0.03f;
    if (G.lag_hp[i] < target) G.lag_hp[i] = target;
    float x = i == 0 ? W * 0.5f - gap - bw : W * 0.5f + gap;
    const float *fc = FCOL[f->def & 3];
    bar(x, by, bw, bh, G.shown_hp[i], G.lag_hp[i], rgba(fc[0] * 0.85f + 0.15f, fc[1] * 0.85f + 0.15f, fc[2] * 0.85f + 0.15f, 1), i == 0);
    /* the guard gauge: a thin bar under the life bar, filled by blocked hits (full: the guard breaks) */
    if (f->guard > 0) {
      float gf = (float)f->guard / (float)FT_GUARD_MAX, gw = bw * 0.5f, gx = i == 0 ? x + bw - gw : x;
      cw_r2d_rect(cw_rect_make(gx, by + bh + 4 * s, gw, 5 * s), rgba(0.05f, 0.05f, 0.06f, 0.8f));
      cw_r2d_rect(cw_rect_make(i == 0 ? gx + gw * (1 - gf) : gx, by + bh + 4 * s, gw * gf, 5 * s), gf > 0.75f ? rgba(1, 0.3f, 0.2f, 1) : rgba(0.85f, 0.88f, 0.95f, 1));
    }
    text(G.font_bold, d->name, i == 0 ? x : x + bw, by + bh + 10 * s, 20 * s, rgba(1, 1, 1, 1), i == 0 ? CW_ALIGN_LEFT : CW_ALIGN_RIGHT);
    /* round pips */
    for (int w = 0; w < 2; w++) {
      float px = i == 0 ? W * 0.5f - gap - 10 * s - (float)w * 22 * s : W * 0.5f + gap + 10 * s + (float)w * 22 * s;
      cw_r2d_circle((cw_vec2){ px, pip_y(s) }, 7 * s, 12, w < f->wins ? rgba(1, 0.85f, 0.2f, 1) : rgba(0.2f, 0.2f, 0.2f, 0.8f));
    }
    /* meter */
    float mw = W * 0.30f, mh = 14 * s, my = H - 34 * s, mx = i == 0 ? 28 * s : W - 28 * s - mw;
    cw_r2d_round_rect(cw_rect_make(mx - 3, my - 3, mw + 6, mh + 6), mh * 0.4f, 6, rgba(0.02f, 0.02f, 0.03f, 0.85f));
    float mf = (float)f->meter / (float)FT_METER_MAX;
    bool full = f->meter >= FT_METER_MAX;
    float pulse = full ? 0.75f + 0.25f * (float)cw_sin((float)g->tick * 0.2f) : 1;
    cw_r2d_rect(cw_rect_make(i == 0 ? mx : mx + mw * (1 - mf), my, mw * mf, mh), full ? rgba(1 * pulse, 0.85f * pulse, 0.2f * pulse, 1) : rgba(0.25f, 0.6f, 1, 1));
    text(G.font_ui, full ? "SUPER READY" : "SUPER", i == 0 ? mx : mx + mw, my - 3 * s - 19 * s * grow(G.font_ui, 15 * s), 15 * s, rgba(1, 1, 1, 0.9f), i == 0 ? CW_ALIGN_LEFT : CW_ALIGN_RIGHT);
    /* combo */
    const ft_fighter *o = &g->f[1 - i];
    if (o->combo_hits >= 2) {
      char c[32];
      snprintf(c, sizeof c, "%d HITS", o->combo_hits);
      float cx = i == 0 ? 40 * s : W - 40 * s;
      text(G.font_score, c, cx, H * 0.32f, 34 * s, rgba(1, 0.85f, 0.25f, 1), i == 0 ? CW_ALIGN_LEFT : CW_ALIGN_RIGHT);
      snprintf(c, sizeof c, "%d damage", o->combo_dmg);
      text(G.font_ui, c, cx, H * 0.32f + 40 * s * grow(G.font_score, 34 * s), 18 * s, rgba(1, 1, 1, 0.9f), i == 0 ? CW_ALIGN_LEFT : CW_ALIGN_RIGHT);
    }
  }
  /* timer */
  char t[16];
  if (g->training) snprintf(t, sizeof t, "--");
  else snprintf(t, sizeof t, "%d", (g->timer + 59) / 60);
  {
    float tg = grow(G.font_score, 42 * s), tw = fmaxf(76 * s, text_w(G.font_score, t, 42 * s) + 16 * s);
    cw_r2d_round_rect(cw_rect_make(W * 0.5f - tw * 0.5f, by - 6 * s, tw, 56 * s * tg), 12 * s, 8, rgba(0.02f, 0.02f, 0.03f, 0.85f));
  }
  text(G.font_score, t, W * 0.5f, by - 2 * s, 42 * s, g->timer < 600 && !g->training ? rgba(1, 0.4f, 0.3f, 1) : rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  /* banners */
  float pt = (float)g->phase_t;
  char b[48];
  if (g->phase == P_INTRO) {
    if (pt < 60) { snprintf(b, sizeof b, "ROUND %d", g->round_no); float k = cw_clampf(pt / 12.0f, 0, 1); text(G.font_score, b, W * 0.5f, H * 0.3f, (50 + 30 * (1 - k)) * s, rgba(1, 0.9f, 0.4f, k), CW_ALIGN_CENTER); }
    else { float k = cw_clampf((pt - 60) / 8.0f, 0, 1); text(G.font_score, "FIGHT!", W * 0.5f, H * 0.3f, (80 - 20 * k) * s, rgba(1, 0.4f, 0.25f, 1), CW_ALIGN_CENTER); }
  } else if (g->phase == P_FIGHT && g->phase_t < 30 && !g->training) {
    text(G.font_score, "FIGHT!", W * 0.5f, H * 0.3f, 60 * s, rgba(1, 0.4f, 0.25f, 1 - pt / 30.0f), CW_ALIGN_CENTER);
  } else if (g->phase == P_KO) {
    float k = cw_clampf(pt / 10.0f, 0, 1);
    text(G.font_score, "K.O.", W * 0.5f, H * 0.28f, (140 - 40 * k) * s, rgba(1, 0.25f, 0.2f, 1), CW_ALIGN_CENTER);
  } else if (g->phase == P_ROUND_END) {
    if (g->round_winner >= 0) { snprintf(b, sizeof b, "%s WINS", g->roster->d[g->f[g->round_winner].def].name); text(G.font_score, b, W * 0.5f, H * 0.3f, 54 * s, rgba(1, 0.9f, 0.4f, 1), CW_ALIGN_CENTER); }
    else text(G.font_score, "DRAW", W * 0.5f, H * 0.3f, 54 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
  /* callouts: a parry, a guard break, a high ducked */
  {
    static uint32_t seen;
    static float age = 99;
    static int kind, side;
    if (seen > g->ev_seq) seen = g->ev_seq; /* a new match restarts the event count */
    if (g->ev_seq - seen > 64) seen = g->ev_seq - 64;
    for (; seen != g->ev_seq; seen++) {
      const ft_event *e = &g->ev[seen & 63];
      if (e->type == EV_PARRY || e->type == EV_GUARDBREAK || e->type == EV_WHIFF || e->type == EV_JUGGLE_SPENT) { kind = e->type; side = e->who; age = 0; }
    }
    age += 1.0f / 60.0f;
    if (age < 0.9f) {
      const char *w = kind == EV_PARRY ? "PARRY" : kind == EV_GUARDBREAK ? "GUARD BREAK" : kind == EV_JUGGLE_SPENT ? "JUGGLE OVER" : "DUCKED";
      float a = cw_clampf(1.4f - age * 1.5f, 0, 1);
      /* in training the move list covers the right side */
      text(G.font_score, w, g->training ? W * 0.5f : side == 0 ? W * 0.25f : W * 0.75f, H * 0.2f, 30 * s, kind == EV_GUARDBREAK ? rgba(1, 0.35f, 0.25f, a) : rgba(0.6f, 0.9f, 1, a), CW_ALIGN_CENTER);
    }
  }
  if (g->training) draw_training(W, H, s);
  /* a super or a knockout dims the world for a moment (fx_flash swells and lifts): a darkening, never a white wash */
  float fl = fx_flash();
  if (fl > 0.01f) cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0.02f, 0.02f, 0.05f, fl * 0.45f));
}

static void draw_select(float W, float H, float s) {
  /* the two picked fighters stand on the stage behind this, framed on their faces (update_camera) */
  for (int i = 0; i < 32; i++) { /* a darkening toward the cards: bands that touch but never overlap (overlaps show as stripes) */
    float y0b = floorf(H * (0.6f + 0.4f * (float)i / 32.0f)), y1b = floorf(H * (0.6f + 0.4f * (float)(i + 1) / 32.0f));
    cw_r2d_rect(cw_rect_make(0, y0b, W, y1b - y0b), rgba(0.02f, 0.025f, 0.05f, 0.2f + 0.7f * (float)i / 31.0f));
  }
  /* everything is laid out from the drawn text, top down and bottom up: at 100% it sits where it always did, at a
   * bigger text size the lines move apart, the long hints wrap to the screen and the cards grow round their words.
   * When the screen cannot hold it all (200% on a short screen) the cards put each special's name under its input, and
   * then the walking hint and the specials share the bottom slot with the button hint, a few seconds each (below) */
  static const char *const MOVE_HINT = "Hold {mv_left|mv_right}: walk     {mv_up}: jump (up-forward, up-back)     Hold {mv_down}: crouch     Tap twice: dash";
  static const char *const BTN_HINT = "Punch {atk_p}   Kick {atk_k}   Block {atk_g}   Up + button (together): high   Down + button: low   Punch + kick, or {atk_pk}: throw";
  static const char *const NAV_HINT = "{mv_left|mv_right} fighter     {atk_pk} random     {atk_k} or {ui_start} lock in     {atk_g} back out (two players) or computer level     {ui_quit} title";
  char stage[160], mode[160], move[320], btn[320], nav[320];
  snprintf(stage, sizeof stage, "Stage: %s   ({mv_up|mv_down})", STAGE_NAME[G.sel_stage]);
  snprintf(mode, sizeof mode, "Mode: %s   ({atk_p})", G.training ? "Training" : G.players > 1 ? "Versus" : G.arcade ? "Arcade, four fights" : "Versus the computer");
  if (G.players < 2) { size_t l = strlen(mode); snprintf(mode + l, sizeof mode - l, "      Computer: %s   ({atk_g})", DIFF_NAME[G.difficulty]); }
  const float maxw = W - 24 * s;
  wrap_parts(G.font_ui, MOVE_HINT, "     ", -1, 16 * s, maxw, move, sizeof move);
  wrap_parts(G.font_ui, BTN_HINT, "   ", -1, 16 * s, maxw, btn, sizeof btn);
  wrap_parts(G.font_ui, NAV_HINT, "     ", -1, 16 * s, maxw, nav, sizeof nav);
  float one16 = cw_prompt_text_size(G.font_ui, "M", -1, 16 * s).y; /* a line of the hints, now */
  float mode_w = cw_prompt_text_size(G.font_ui, mode, 0, 17 * s).x;
  if (mode_w > maxw) { /* the mode and the computer's level on two lines */
    char m2[160];
    snprintf(m2, sizeof m2, "%s", mode);
    char *cut = strstr(m2, "      Computer");
    if (cut) { *cut = '\n'; memmove(cut + 1, cut + 6, strlen(cut + 6) + 1); snprintf(mode, sizeof mode, "%s", m2); }
  }
  float y_title = 14 * s, y_stage = y_title + 42 * s * grow(G.font_score, 34 * s);
  float y_mode = y_stage + 28 * s * grow(G.font_ui, 19 * s);
  float y_move = y_mode + 22 * s * grow(G.font_ui, 17 * s) + (cw_prompt_text_size(G.font_ui, mode, 0, 17 * s).y - cw_prompt_text_size(G.font_ui, "M", 0, 17 * s).y);
  float h_move = cw_prompt_text_size(G.font_ui, move, -1, 16 * s).y, h_btn = cw_prompt_text_size(G.font_ui, btn, -1, 16 * s).y;
  float h_nav = cw_prompt_text_size(G.font_ui, nav, -1, 16 * s).y;
  /* bottom up: the navigation hint's last line ends where a line at 100% always did, the button hint over it */
  float y_nav = H - 22 * s + one16 / grow(G.font_ui, 16 * s) - h_nav;
  /* the cards: name, life, role and the three specials, spaced by their drawn size */
  int n = G.roster.n;
  const float k13 = grow(G.font_ui, 13 * s), gap = 16 * s;
  char sp[4][3][96];
  float content_w = 0;
  for (int i = 0; i < n; i++) {
    const ft_def *d = &G.roster.d[i];
    content_w = fmaxf(content_w, text_w(G.font_bold, d->name, 24 * s));
    char b[96];
    snprintf(b, sizeof b, "Life %d", d->hp);
    content_w = fmaxf(content_w, text_w(G.font_ui, b, 13 * s));
    content_w = fmaxf(content_w, text_w(G.font_ui, ROLE[i & 3], 13 * s));
    for (int k = 0; k < 3; k++) { /* the specials: motion, button, name */
      uint16_t mb = d->mbtn[k]; /* the motion in direction digits, then the button as a prompt */
      snprintf(sp[i][k], sizeof sp[i][k], "%s %s %s", d->motion[k], mb == B_P ? "{atk_p}" : mb == B_K ? "{atk_k}" : "{atk_p}/{atk_k}", d->mv[M_SP1 + k].name);
    }
  }
  float sp_w = 0;
  for (int i = 0; i < n; i++)
    for (int k = 0; k < 3; k++) sp_w = fmaxf(sp_w, cw_prompt_text_size(G.font_ui, sp[i][k], -1, 13 * s).x);
  float cw_ = fmaxf(190 * s, fmaxf(content_w, sp_w) + 16 * s);
  bool split = (float)n * cw_ + (float)(n - 1) * gap > W - 16 * s; /* each special on two lines: its input, then its name */
  if (split) {
    float w2 = 0;
    for (int i = 0; i < n; i++)
      for (int k = 0; k < 3; k++) {
        char *nm = strchr(strchr(sp[i][k], ' ') + 1, ' '); /* after the motion and the button */
        if (nm) *nm = '\n';
        w2 = fmaxf(w2, cw_prompt_text_size(G.font_ui, sp[i][k], -1, 13 * s).x);
      }
    cw_ = fmaxf(190 * s, fmaxf(content_w, w2) + 16 * s);
    if ((float)n * cw_ + (float)(n - 1) * gap > W - 16 * s) cw_ = (W - 16 * s - (float)(n - 1) * gap) / (float)n;
  }
  const float sp_step = 15 * s * k13 * (split ? 2 : 1);
  float y_life = 16 * s + 30 * s * grow(G.font_bold, 24 * s), y_role = y_life + 16 * s * k13, y_sp = y_role + 18 * s * k13;
  float ch = y_sp + 3 * sp_step + 7 * s;
  /* fit, when the cards would meet the top band: first the walking hint goes down to share the button hint's slot (a
   * few seconds each); then the cards leave out the specials, and the specials of each player's fighter take turns in
   * that slot too */
  const float tag_h = 24 * s * grow(G.font_bold, 18 * s); /* the P1 and P2 tags over the cards */
  int pages = 1;
  bool compact = false;
  char spec_line[2][320];
  float band = fmaxf(130 * s, y_move + h_move + 5 * s), slot_h = h_btn, y_btn = y_nav - 0.8f * s - slot_h, y0 = y_btn - 2 * s - ch;
  if (y0 - tag_h < band + 2 * s) {
    pages = 2;
    band = fmaxf(130 * s, y_move);
    slot_h = fmaxf(h_btn, h_move);
    y_btn = y_nav - 0.8f * s - slot_h;
    y0 = y_btn - 2 * s - ch;
  }
  if (y0 - tag_h < band + 2 * s) {
    compact = true;
    ch = y_sp - 18 * s * k13 + 22 * s * k13 + 7 * s; /* name, life, role */
    for (int p = 0; p < (G.players > 1 ? 2 : 1); p++) {
      const ft_def *d = &G.roster.d[G.sel[p] % n];
      char raw[320];
      int l = snprintf(raw, sizeof raw, "%s%s:", G.players > 1 ? (p ? "P2 " : "P1 ") : "", d->name);
      for (int k = 0; k < 3; k++) {
        char one[96];
        snprintf(one, sizeof one, "%s", sp[G.sel[p] % n][k]);
        for (char *q = one; *q; q++) if (*q == '\n') *q = ' ';
        l += snprintf(raw + l, sizeof raw - (size_t)l, "%s%s", k ? "     " : "   ", one);
      }
      char *colon = strchr(raw, ':'); /* the name and the first special stay together */
      if (colon) memmove(colon + 1, colon + 3, strlen(colon + 3) + 1), colon[1] = ' ';
      wrap_parts(G.font_ui, raw, "     ", -1, 16 * s, maxw, spec_line[p], sizeof spec_line[p]);
      slot_h = fmaxf(slot_h, cw_prompt_text_size(G.font_ui, spec_line[p], -1, 16 * s).y);
      pages++;
    }
    y_btn = y_nav - 0.8f * s - slot_h;
    y0 = y_btn - 2 * s - ch;
  }
  cw_r2d_rect(cw_rect_make(0, 0, W, band), rgba(0.02f, 0.025f, 0.05f, 0.55f));
  text(G.font_score, "CHOOSE YOUR FIGHTER", W * 0.5f, y_title, 34 * s, rgba(1, 0.9f, 0.5f, 1), CW_ALIGN_CENTER);
  ptext(G.font_ui, stage, 0, W * 0.5f, y_stage, 19 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  ptext(G.font_ui, mode, 0, W * 0.5f, y_mode, 17 * s, rgba(1, 0.9f, 0.6f, 1), CW_ALIGN_CENTER);
  int page = pages > 1 ? (int)((G.sim.tick / 240u) % (uint32_t)pages) : 0; /* four seconds a page */
  if (pages == 1) ptext(G.font_ui, move, -1, W * 0.5f, y_move, 16 * s, rgba(0.85f, 0.92f, 1, 1), CW_ALIGN_CENTER);
  float total = (float)n * cw_ + (float)(n - 1) * gap, x0 = (W - total) * 0.5f;
  for (int i = 0; i < n; i++) {
    const ft_def *d = &G.roster.d[i];
    float x = x0 + (float)i * (cw_ + gap), cx = x + cw_ * 0.5f;
    const float *c = FCOL[i & 3];
    bool p1 = G.sel[0] == i, p2 = G.players > 1 && G.sel[1] == i;
    cw_r2d_round_rect(cw_rect_make(x, y0, cw_, ch), 12 * s, 8, rgba(0.06f, 0.07f, 0.1f, 0.88f));
    cw_r2d_round_rect(cw_rect_make(x + 8 * s, y0 + 8 * s, cw_ - 16 * s, 5 * s), 2 * s, 4, rgba(c[0], c[1], c[2], 1));
    text(G.font_bold, d->name, cx, y0 + 16 * s, 24 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
    char b[96];
    snprintf(b, sizeof b, "Life %d", d->hp);
    text(G.font_ui, b, cx, y0 + y_life, 13 * s, rgba(0.8f, 0.85f, 0.95f, 1), CW_ALIGN_CENTER);
    text(G.font_ui, ROLE[i & 3], cx, y0 + y_role, 13 * s, rgba(0.75f, 0.8f, 0.9f, 1), CW_ALIGN_CENTER);
    for (int k = 0; k < 3 && !compact; k++) ptext(G.font_ui, sp[i][k], -1, cx, y0 + y_sp + sp_step * (float)k, 13 * s, rgba(1, 0.85f, 0.5f, 1), CW_ALIGN_CENTER);
    if (p1) cw_r2d_round_rect_lines(cw_rect_make(x - 4 * s, y0 - 4 * s, cw_ + 8 * s, ch + 8 * s), 14 * s, 4 * s, 8, G.ready[0] ? rgba(1, 0.85f, 0.2f, 1) : rgba(1, 0.4f, 0.3f, 1));
    if (p2) cw_r2d_round_rect_lines(cw_rect_make(x - 8 * s, y0 - 8 * s, cw_ + 16 * s, ch + 16 * s), 16 * s, 3 * s, 8, G.ready[1] ? rgba(1, 0.85f, 0.2f, 1) : rgba(0.3f, 0.6f, 1, 1));
    if (p1) text(G.font_bold, "P1", x + 10 * s, y0 - tag_h, 18 * s, rgba(1, 0.5f, 0.4f, 1), CW_ALIGN_LEFT);
    if (p2) text(G.font_bold, "P2", x + cw_ - 10 * s, y0 - tag_h, 18 * s, rgba(0.4f, 0.7f, 1, 1), CW_ALIGN_RIGHT);
  }
  int w = seat_waiting();
  if (w >= 0 && !G.sw_seat_wait && G.wait_t > SEAT_WAIT - SEAT_WARN) { /* the seat that has not chosen: the countdown to the computer playing it */
    char m[96];
    int left = (SEAT_WAIT - G.wait_t + 59) / 60;
    snprintf(m, sizeof m, "P%d has not locked in: the computer plays P%d in %d", w + 1, w + 1, left);
    if (text_w(G.font_bold, m, 22 * s) > W - 48 * s) snprintf(m, sizeof m, "P%d: the computer plays in %d", w + 1, left);
    float mw = text_w(G.font_bold, m, 22 * s), mh = 34 * s * grow(G.font_bold, 22 * s), my = floorf((band + y0 - tag_h) * 0.5f - mh * 0.5f);
    cw_r2d_round_rect(cw_rect_make(W * 0.5f - mw * 0.5f - 18 * s, my, mw + 36 * s, mh), 10 * s, 8, rgba(0.06f, 0.07f, 0.1f, 0.88f));
    text(G.font_bold, m, W * 0.5f, my + 5 * s, 22 * s, w ? rgba(0.5f, 0.75f, 1, 1) : rgba(1, 0.55f, 0.45f, 1), CW_ALIGN_CENTER);
  }
  if (page == 1) ptext(G.font_ui, move, -1, W * 0.5f, y_btn, 16 * s, rgba(0.85f, 0.92f, 1, 1), CW_ALIGN_CENTER);
  else if (page >= 2) ptext(G.font_ui, spec_line[page - 2], -1, W * 0.5f, y_btn, 16 * s, rgba(1, 0.85f, 0.5f, 1), CW_ALIGN_CENTER);
  else ptext(G.font_ui, btn, -1, W * 0.5f, y_btn, 16 * s, rgba(1, 0.95f, 0.8f, 1), CW_ALIGN_CENTER);
  ptext(G.font_ui, nav, -1, W * 0.5f, y_nav, 16 * s, rgba(0.7f, 0.75f, 0.85f, 1), CW_ALIGN_CENTER);
}

static void draw_result(float W, float H, float s) {
  /* the winner's face fills the middle (update_camera); the words sit in bands above and below it, each band as tall
   * as its words (the player's text size grows them; the hint wraps to the screen) */
  const ft_game *g = &G.sim;
  char hint[256], raw[200];
  if (G.arcade && G.arc_clear) snprintf(raw, sizeof raw, "{atk_k} or {ui_start} back to fighter select");
  else if (G.arcade && G.arc_lost) snprintf(raw, sizeof raw, "{atk_k} or {ui_start} try again      {atk_g} back to fighter select");
  else if (G.arcade) snprintf(raw, sizeof raw, "{atk_k} or {ui_start} next fight: %s%s      {atk_g} back to fighter select", G.arc_step == 3 ? "the mirror, " : "", G.roster.d[G.arc_step < 3 ? (G.sel[0] + 1 + G.arc_step) % G.roster.n : G.sel[0]].name);
  else snprintf(raw, sizeof raw, "{atk_k} or {ui_start} rematch      {atk_g} back to fighter select");
  wrap_parts(G.font_ui, raw, "      ", -1, 22 * s, W - 32 * s, hint, sizeof hint);
  float hh = cw_prompt_text_size(G.font_ui, hint, -1, 22 * s).y, hy = H - 50 * s + cw_prompt_text_size(G.font_ui, "M", -1, 22 * s).y / grow(G.font_ui, 22 * s) - hh; /* its last line ends where a line at 100% always did */
  float top = fmaxf(120 * s, 78 * s + 36 * s * grow(G.font_bold, 30 * s) + 6 * s), bot = fmaxf(70 * s, H - hy + 20 * s);
  cw_r2d_rect(cw_rect_make(0, 0, W, top), rgba(0.02f, 0.02f, 0.04f, 0.6f));
  cw_r2d_rect(cw_rect_make(0, H - bot, W, bot), rgba(0.02f, 0.02f, 0.04f, 0.6f));
  char b[80];
  if (g->match_winner >= 0) snprintf(b, sizeof b, "%s WINS", g->roster->d[g->f[g->match_winner].def].name);
  else snprintf(b, sizeof b, "DRAW");
  text(G.font_score, b, W * 0.5f, 14 * s, 60 * s, rgba(1, 0.9f, 0.4f, 1), CW_ALIGN_CENTER);
  snprintf(b, sizeof b, "%d - %d", g->f[0].wins, g->f[1].wins);
  text(G.font_bold, b, W * 0.5f, 14 * s + 64 * s * grow(G.font_score, 60 * s), 30 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  if (G.arcade) {
    char a[64];
    if (G.arc_clear) snprintf(a, sizeof a, "ARCADE CLEAR");
    else snprintf(a, sizeof a, "Arcade: fight %d of 4 %s", G.arc_step + (G.arc_lost ? 1 : 0), G.arc_lost ? "lost" : "won");
    if (!G.arc_lost && !G.arc_clear) snprintf(a, sizeof a, "Arcade: %d of 4 won", G.arc_step);
    text(G.font_bold, a, W * 0.5f, H - bot - 40 * s * grow(G.font_bold, 26 * s), 26 * s, G.arc_clear ? rgba(1, 0.85f, 0.3f, 1) : rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
  if (G.stepped_up) text(G.font_ui, "The computer steps up to Normal", W * 0.5f, H - bot - 34 * s * grow(G.font_ui, 20 * s), 20 * s, rgba(1, 0.85f, 0.45f, 1), CW_ALIGN_CENTER);
  ptext(G.font_ui, hint, -1, W * 0.5f, hy, 22 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
}

static void render(void *u, double alpha) {
  (void)u; (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  if (cw_shell_hud_shown()) { /* not under the shell's menus */
    bool hud_on = true;
#if defined(FIGHTER_AUTOPILOT) || defined(FIGHTER_SCRIPTED)
    hud_on = !getenv("FIGHTER_NOHUD"); /* development (key art): the match and the select and result scenes with no 2D over them */
#endif
    if (!hud_on) {
    } else if (G.state == GS_SELECT) draw_select(W, H, s);
    else if (G.state == GS_RESULT) draw_result(W, H, s);
    else draw_hud(W, H, s);
  }
  cw_quality_menu_draw(&(cw_quality_menu_style){ .font = G.font_ui, .accent = rgba(1, 0.85f, 0.3f, 1) });
  cw_r2d_end();
  cw_shell_draw();
}

/* ── setup ──────────────────────────────────────────────────────────── */

static void screen_changed(cw_shell_screen from, cw_shell_screen to, void *u) {
  (void)from; (void)u;
  if (to == CW_SHELL_TITLE) { G.state = GS_SELECT; backdrop(); }
}
/* a pad took a seat on the select panel: the computer's side is theirs now */
static void players_changed(int players, void *u) {
  (void)u;
  G.players = players;
  G.ready[1] = 0;
  G.cpu_seat[0] = G.cpu_seat[1] = 0; G.wait_t = 0;
}
static void shell_sound(cw_shell_sound s, void *u) {
  (void)u;
  switch (s) { case CW_SHELL_SND_MOVE: sfx(SFX_LAND, 0.4f, 1.5f); break; case CW_SHELL_SND_SELECT: sfx(SFX_ROUND, 0.5f, 1.0f); break; case CW_SHELL_SND_BACK: sfx(SFX_BLOCK, 0.4f, 1.2f); break;
           case CW_SHELL_SND_JOIN: sfx(SFX_BELL, 0.4f, 1.3f); break; case CW_SHELL_SND_PAUSE: sfx(SFX_BLOCK, 0.4f, 0.9f); break; }
}

#if defined(FIGHTER_AUTOPILOT) || defined(FIGHTER_SCRIPTED)
/* test builds: "cam <zoom> [who 0|1|2] [yaw]" and "match <stage> <p1> <p2>" for close-ups and stage tours on any host */
static void cmd_cam(int argc, const char **argv, cw_reply *r) {
  g_cam[0] = argc > 1 ? (float)atof(argv[1]) : 0; g_cam[1] = argc > 2 ? (float)atof(argv[2]) : 2; g_cam[2] = argc > 3 ? (float)atof(argv[3]) : 0;
  memset(&G.fs, 0, sizeof G.fs);
  if (r) cw_reply_printf(r, "{\"zoom\":%g}", (double)g_cam[0]);
}
/* "screen select <p1> <p2> <stage>" or "screen result <winner 0|1>" holds that screen; "screen off" lets the autopilot go on */
static void cmd_screen(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { if (r) cw_reply_error(r, "usage: screen select|result|off ..."); return; }
  if (!strcmp(argv[1], "off")) { g_hold = false; if (r) cw_reply_printf(r, "{\"hold\":false}"); return; }
  g_hold = true;
  if (!strcmp(argv[1], "select")) {
    if (argc > 2) G.sel[0] = atoi(argv[2]) & 3;
    int b = argc > 3 ? atoi(argv[3]) & 3 : (G.sel[0] + 1) & 3;
    if (argc > 4) G.sel_stage = atoi(argv[4]) % STAGES;
    G.players = 2; G.sel[1] = b; seats_reset();
    shown_sel[0] = -1; G.state = GS_SELECT; select_scene();
  } else {
    int w = argc > 2 ? atoi(argv[2]) & 1 : 0;
    G.sim.phase = P_MATCH_END; G.sim.match_winner = w; G.sim.f[w].state = S_WIN; G.sim.f[w].t = 0; G.sim.f[1 - w].state = S_KO; G.sim.f[1 - w].t = 0; /* as a match ends: the loser down */
    G.state = GS_RESULT; G.result_t = 0;
    model_set_showcase(G.mdl[w], 0.9f);
  }
  if (r) cw_reply_printf(r, "{\"state\":%d}", G.state);
}
static void cmd_match(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: match <stage 0-2> [p1 0-3] [p2 0-3]"); return; }
  G.sel_stage = atoi(argv[1]) % STAGES;
  if (argc > 2) G.sel[0] = atoi(argv[2]) & 3;
  if (argc > 3) G.sel[1] = atoi(argv[3]) & 3;
  g_hold = false;
  begin_match();
  if (r) cw_reply_printf(r, "{\"stage\":%d}", G.sel_stage);
}
#endif

/* debug channel: "bind <action> <bindings>" replaces an action's bindings (the text form, "key:x, pad:north"): for checking
 * that a rebound action's prompt shows its new button */
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

/* the gates' switches: sw seat_wait 1 waits for ever for a seat that never locks in (the control of
 * tests/fighter-idle-seat.mjs); sw crowd_sync 1 is the crowd as it was, every spectator answering an event at once */
static void cmd_sw(int argc, const char **argv, cw_reply *r) {
  if (argc >= 3 && !strcmp(argv[1], "seat_wait")) { G.sw_seat_wait = atoi(argv[2]) != 0; cw_reply_printf(r, "{\"seat_wait\":%d}", G.sw_seat_wait); return; }
  if (argc >= 3 && !strcmp(argv[1], "flash_cap")) { fx_flash_cap = atoi(argv[2]) != 0; cw_reply_printf(r, "{\"flash_cap\":%d}", fx_flash_cap); return; }
  if (argc >= 3 && !strcmp(argv[1], "unlit_sparks")) { fx_unlit_sparks = atoi(argv[2]) != 0; cw_reply_printf(r, "{\"unlit_sparks\":%d}", fx_unlit_sparks); return; }
  if (argc >= 3 && !strcmp(argv[1], "boom_every")) { fx_boom_every = atoi(argv[2]); cw_reply_printf(r, "{\"boom_every\":%d}", fx_boom_every); return; }
  if (argc >= 3 && !strcmp(argv[1], "cpu_hp")) { G.sw_cpu_hp = atoi(argv[2]); cw_reply_printf(r, "{\"cpu_hp\":%d}", G.sw_cpu_hp); return; }
  if (argc >= 3 && !strcmp(argv[1], "crowd_sync")) { crowd_sync = atoi(argv[2]) != 0; crowd_same_max = 0; cw_reply_printf(r, "{\"crowd_sync\":%d}", crowd_sync); return; }
  cw_reply_error(r, "usage: sw seat_wait <0|1> | sw crowd_sync <0|1> | sw cpu_hp <n> | sw flash_cap <0|1> | sw boom_every <ticks>");
}

static void init(void *u) {
  (void)u;
  cw_debug_command("sw", "sw seat_wait <0|1> | crowd_sync <0|1> | cpu_hp <n> | flash_cap <0|1> | boom_every <ticks>: test switches (explosion flashes capped at one per 24 ticks, or every one as before; a heavy blow's effects every n ticks; the select screen waits for ever for a seat that never locks in, as before; the crowd answers every event at once, as before; the computer starts each round on n life)", cmd_sw);
#if defined(FIGHTER_AUTOPILOT) || defined(FIGHTER_SCRIPTED)
  cw_debug_command("cam", "cam <zoom> [who 0|1|2] [yaw]: close-up camera (zoom 0: normal)", cmd_cam);
  cw_debug_command("match", "match <stage 0-2> [p1] [p2]: start a match on a stage", cmd_match);
  cw_debug_command("bind", "bind <action> <bindings>: replace an action's bindings", cmd_bind);
  cw_debug_command("screen", "screen select <p1> <p2> <stage> | result <winner> | off: hold a menu screen", cmd_screen);
  { const char *e = getenv("FIGHTER_CAM"); if (e) sscanf(e, "%f %f %f", &g_cam[0], &g_cam[1], &g_cam[2]); }
  G.no_slowmo = getenv("FIGHTER_NO_SLOWMO") != NULL;
  crowd_sync = getenv("FIGHTER_CROWD_SYNC") != NULL;
#endif
  cw_input_load_bindings_asset("input.cfg");
  cw_input_commit_defaults();
  G.a_left = cw_action_find("mv_left"); G.a_right = cw_action_find("mv_right"); G.a_up = cw_action_find("mv_up"); G.a_down = cw_action_find("mv_down");
  G.fs_left = cw_action_find("fs_left"); G.fs_right = cw_action_find("fs_right"); G.fs_up = cw_action_find("fs_up"); G.fs_down = cw_action_find("fs_down");
  G.dp_left = cw_action_find("dp_left"); G.dp_right = cw_action_find("dp_right"); G.dp_up = cw_action_find("dp_up"); G.dp_down = cw_action_find("dp_down");
  G.ui_start = cw_action_find("ui_start"); G.ui_quit = cw_action_find("ui_quit");
  G.a_p = cw_action_find("atk_p"); G.a_k = cw_action_find("atk_k"); G.a_g = cw_action_find("atk_g"); G.a_pk = cw_action_find("atk_pk");
  G.k_left = cw_action_find("k2_left"); G.k_right = cw_action_find("k2_right"); G.k_up = cw_action_find("k2_up"); G.k_down = cw_action_find("k2_down");
  G.k_p = cw_action_find("k2_p"); G.k_k = cw_action_find("k2_k"); G.k_g = cw_action_find("k2_g"); G.k_pk = cw_action_find("k2_pk");
  G.tr_mode = cw_action_find("tr_mode"); G.tr_reset = cw_action_find("tr_reset"); G.tr_dummy = cw_action_find("tr_dummy"); G.tr_boxes = cw_action_find("tr_boxes");
  const cw_font_desc fd = { .mode = CW_FONT_SDF, .sdf_padding = 10 };
  G.font_ui = cw_font_load("fonts/Fredoka-SemiBold.ttf", &fd);
  G.font_bold = cw_font_load("fonts/Fredoka-Bold.ttf", &fd);
  G.font_score = cw_font_load("fonts/Bungee-Regular.ttf", &fd);
  size_t len = 0;
  char *raw = cw_asset_load("data/moves.txt", &len);
  if (!raw) { cw_error("fighter: data/moves.txt is missing from the assets"); return; }
  char *txt = malloc(len + 1);
  memcpy(txt, raw, len);
  txt[len] = 0;
  free(raw);
  const char *err = NULL;
  if (ft_roster_load(&G.roster, txt, &err) < 0) cw_error("fighter: moves.txt: %s", err);
  free(txt);
  cw_shell_init(&(cw_shell_desc){ .title = "Fighter", .subtitle = "one on one", .max_players = 2, .game_save_size = sizeof(save_t), .game_save_version = 1, .start = start,
                                  .sound = shell_sound, .screen_changed = screen_changed, .players_changed = players_changed, .font = G.font_bold, .accent = rgba(1, 0.5f, 0.3f, 1) });
  G.ui_pause = cw_action_find("pause"); /* the shell's own, defined by cw_shell_init */
  SAVE = cw_shell_game_save();
#if !defined(FIGHTER_AUTOPILOT) && !defined(FIGHTER_SCRIPTED)
  backdrop();
#endif
  G.sfx_bus = cw_shell_sfx_bus();
  G.music_bus = cw_shell_music_bus();
  for (int i = 0; i < SFX_N; i++) G.sfx[i] = cw_sound_load(SFX_FILE[i], NULL);
  for (int i = 0; i < STAGES; i++) G.music[i] = cw_sound_load(MUSIC[i], &(cw_sound_desc){ .mode = CW_SOUND_STREAM });
#if defined(FIGHTER_AUTOPILOT) || defined(FIGHTER_SCRIPTED)
  G.difficulty = 1; /* the gates' pins: the self-playing match and the computer that takes an idle seat play Normal */
#else
  G.difficulty = 0; /* a session opens on Easy (the first match is a newcomer's), Normal after the first win */
#endif
  G.sel[0] = 0; G.sel[1] = 1;
  cw_debug_i32("game.difficulty", &G.difficulty);
  cw_debug_i32("game.panel", &G.state); /* live on the select and result screens too (0 select, 1 match, 2 result), where game.state is not updated */
  cw_debug_i32("game.stage", &G.sel_stage);
  cw_debug_i32("game.state", &G.d_state); cw_debug_i32("game.phase", &G.d_phase); cw_debug_i32("game.round", &G.d_round);
  cw_debug_field("sim.hp", G.d_hp, CW_DBG_I32, 2); cw_debug_field("sim.meter", G.d_meter, CW_DBG_I32, 2); cw_debug_field("sim.wins", G.d_wins, CW_DBG_I32, 2);
  cw_debug_field("sim.pos", G.d_pos, CW_DBG_I32, 4); cw_debug_field("sim.fstate", G.d_fstate, CW_DBG_I32, 2); cw_debug_field("sim.def", G.d_def, CW_DBG_I32, 2);
  cw_debug_i32("seat.wait", &G.wait_t); cw_debug_field("seat.cpu", G.cpu_seat, CW_DBG_I32, 2); /* the select screen's wait for a seat, the seats the computer plays */
  cw_debug_field("sim.guard", G.d_guard, CW_DBG_I32, 2); cw_debug_field("sim.stats", G.d_stats, CW_DBG_I32, 6);
  cw_debug_field("ctl.p1", G.d_ctl, CW_DBG_I32, 10);
  cw_debug_u32("sim.hash", &G.d_hash); cw_debug_i32("sim.combo", &G.d_combo); cw_debug_i32("sim.adv", &G.d_adv); cw_debug_i32("ticks", &G.d_tick);
  cw_debug_i32("fx.hits", &G.d_hitcount); cw_debug_i32("fx.blocks", &G.d_blockcount); cw_debug_i32("view.boxes", &G.d_boxes);
  cw_debug_i32("tex.nodes", &G.d_tex_nodes); cw_debug_i32("tex.objs", &G.d_tex_objs); cw_debug_i32("tex.meshes", &G.d_tex_meshes); cw_debug_i32("tex.applied", &G.tex_done);
  cw_debug_u32("tex.hash", &tex_hash); cw_debug_f32_array("cam.pos", G.d_cam, 3); cw_debug_f32("fighter.spring", &G.d_spring);
  cw_debug_field("cam.offset", cam_offset, CW_DBG_I32, 3);
  cw_debug_field("rumble.sent", fx_rumble_sent, CW_DBG_I32, 2);
  cw_debug_i32("crowd.same", &crowd_same_max); cw_debug_i32("fx.flashes", &fx_flashes);
  cw_debug_field("arcade", G.d_arc, CW_DBG_I32, 4);
  cw_debug_i32("ko.slow", &G.d_slow); cw_debug_i32("music.tense", &G.d_tense); cw_debug_u32("ko.end_hash", &G.d_end_hash);
#if defined(FIGHTER_AUTOPILOT) || defined(FIGHTER_SCRIPTED)
  cw_shell_start(2);
#endif
}

static void shutdown(void *u) {
  (void)u;
  unload_match();
  tex_free_all();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc; (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc base = { .title = "Fighter", .width = 1280, .height = 720, .debug = true, .init = init, .fixed_update = fixed, .update = update, .render = render,
                              .shutdown = shutdown, .max_substeps = 3 };
  base.save = cw_shell_save_region();
  base.save_size = cw_shell_save_region_size();
  return cw_scene_app(NULL, &base);
}
