/* fighter: a one-on-one fighting game. The rules are 2D and integer (sim.c);
 * the look is 3D (chars.c, the stage scenes, fx.c). This file is the shell
 * around them: input, select screen, camera, HUD, training mode, sound, tests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static const char *const BLURB[4] = { "All-rounder. Fireball, rising strike, spin kick.", "Heavy grappler. Command grab, charge, headbutt.",
  "Fast and fragile. Slide, rising strike, overhead leap.", "Zoner. Charge a sonic blade, flip kick, slow orb." };
static const float FCOL[4][3] = { { 0.9f, 0.25f, 0.2f }, { 0.95f, 0.55f, 0.1f }, { 0.15f, 0.85f, 0.95f }, { 0.4f, 0.9f, 0.4f } };

typedef struct save_t { uint32_t magic; int32_t wins[4], matches; } save_t;

static struct {
  ft_roster roster;
  ft_game sim;
  int state, players, stage, difficulty, training, show_boxes;
  int sel[2], ready[2], sel_stage;
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
  cw_action a_left, a_right, a_up, a_down, a_l, a_m, a_h, a_t, k_left, k_right, k_up, k_down, k_l, k_m, k_h, k_t, tr_mode, tr_reset, tr_dummy, tr_boxes;
  t3_object *dbg_root;
  t3_mesh *dbg_box[10];
  int ndbg;
  float cam_t;
  /* debug fields */
  int32_t d_state, d_phase, d_hp[2], d_meter[2], d_wins[2], d_round, d_tick, d_combo, d_adv, d_boxes;
  int32_t d_pos[4], d_fstate[2], d_hitcount, d_blockcount, d_def[2];
  uint32_t d_hash;
  float d_cam[3], d_spring;
} G;

static save_t *SAVE;

/* ── sound ──────────────────────────────────────────────────────────── */

static void sfx(int id, float vol, float pitch) {
  if (CW_HANDLE_IS_NULL(G.sfx[id])) return;
  cw_audio_play(G.sfx[id], &(cw_play_desc){ .bus = G.sfx_bus, .volume = vol, .pitch = pitch });
}

/* ── input ──────────────────────────────────────────────────────────── */

static ft_input read_pad(int p) {
  ft_input in = { 0 };
  in.left = cw_action_pressed(p, G.a_left);
  in.right = cw_action_pressed(p, G.a_right);
  in.up = cw_action_pressed(p, G.a_up);
  in.down = cw_action_pressed(p, G.a_down);
  if (cw_action_pressed(p, G.a_l)) in.btn |= B_L;
  if (cw_action_pressed(p, G.a_m)) in.btn |= B_M;
  if (cw_action_pressed(p, G.a_h)) in.btn |= B_H;
  if (cw_action_pressed(p, G.a_t)) in.btn |= B_T;
  if (p == 1) { /* the shared keyboard belongs to player 0's devices: player 2's keys are their own actions */
    in.left |= cw_action_pressed(0, G.k_left);
    in.right |= cw_action_pressed(0, G.k_right);
    in.up |= cw_action_pressed(0, G.k_up);
    in.down |= cw_action_pressed(0, G.k_down);
    if (cw_action_pressed(0, G.k_l)) in.btn |= B_L;
    if (cw_action_pressed(0, G.k_m)) in.btn |= B_M;
    if (cw_action_pressed(0, G.k_h)) in.btn |= B_H;
    if (cw_action_pressed(0, G.k_t)) in.btn |= B_T;
  }
  return in;
}

/* ── stage and models ───────────────────────────────────────────────── */

static void unload_match(void) {
  for (int i = 0; i < 2; i++) { model_free(G.mdl[i]); G.mdl[i] = NULL; }
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

static void load_match(void) {
  unload_match();
  char path[64];
  snprintf(path, sizeof path, "scenes/stage%d.cwscene", G.sel_stage + 1);
  G.stage_node = cw_scene_instance_path(path);
  if (CW_HANDLE_IS_NULL(G.stage_node)) { cw_error("fighter: %s failed to load", path); return; }
  cw_node_add_child(cw_tree_root(cw_scene_app_tree()), G.stage_node);
  G.camera = cw_node_get(G.stage_node, "Camera");
  stage_load(G.sel_stage, G.stage_node);
  fx_init(G.stage_node);
  fx_bind_lights(G.stage_node);
  fx_reset();
  for (int i = 0; i < 2; i++) G.mdl[i] = model_new(G.sim.f[i].def, G.stage_node);
  make_debug_boxes();
  memset(&G.fs, 0, sizeof G.fs);
  G.ev_seen = G.sim.ev_seq;
  G.shown_hp[0] = G.shown_hp[1] = G.lag_hp[0] = G.lag_hp[1] = 1.0f;
  if (!CW_HANDLE_IS_NULL(G.music[G.sel_stage])) G.music_voice = cw_audio_play(G.music[G.sel_stage], &(cw_play_desc){ .bus = G.music_bus, .loop = true, .volume = 0.55f });
}

static int shown_sel[3] = { -1, -1, -1 }; /* the fighters the select screen shows now */
static void begin_match(void) {
  shown_sel[0] = -1;
  G.sim.training = G.training;
  G.sim.dummy = T_STAND;
  G.sim.cpu[0] = false;
  G.sim.cpu[1] = G.players < 2 || G.training == 2;
  G.sim.cpu_level[0] = 1;
  G.sim.cpu_level[1] = (uint8_t)G.difficulty;
  int d0 = G.sel[0], d1 = G.sel[1];
#ifdef FIGHTER_AUTOPILOT
  G.sim.cpu[0] = G.sim.cpu[1] = !G.training;
  if (G.training) G.sim.dummy = T_STAND;
#endif
  ft_game_start(&G.sim, &G.roster, d0, d1);
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
  G.sim.f[0].x = -800; G.sim.f[1].x = 800; /* close together, turned three quarters to the camera */
  load_match();
  for (int i = 0; i < 2; i++) model_set_showcase(G.mdl[i], 0.9f);
}
static void start(int players, void *user) {
  (void)user;
  G.players = players;
  G.state = GS_SELECT;
  G.ready[0] = G.ready[1] = 0;
  shown_sel[0] = -1;
  select_scene();
}

/* ── the select screen ──────────────────────────────────────────────── */

static int menu_pick(int p, int *v, int n) {
  if (cw_action_just_pressed(p, G.a_left) || (p == 1 && cw_action_just_pressed(0, G.k_left))) { *v = (*v + n - 1) % n; sfx(SFX_LAND, 0.4f, 1.6f); return 1; }
  if (cw_action_just_pressed(p, G.a_right) || (p == 1 && cw_action_just_pressed(0, G.k_right))) { *v = (*v + 1) % n; sfx(SFX_LAND, 0.4f, 1.6f); return 1; }
  return 0;
}
static bool confirm(int p) { return cw_action_just_pressed(p, G.a_t) || (p == 1 && cw_action_just_pressed(0, G.k_t)) || (p == 0 && cw_action_just_pressed(0, G.a_m)); }

static void update_select(void) {
  for (int p = 0; p < G.players && p < 2; p++) {
    if (G.ready[p]) {
      if (cw_action_just_pressed(p, G.a_h) || (p == 1 && cw_action_just_pressed(0, G.k_h))) G.ready[p] = 0;
      continue;
    }
    menu_pick(p, &G.sel[p], G.roster.n);
    if (p == 0) {
      if (cw_action_just_pressed(0, G.a_up)) { G.sel_stage = (G.sel_stage + STAGES - 1) % STAGES; sfx(SFX_LAND, 0.4f, 1.3f); }
      if (cw_action_just_pressed(0, G.a_down)) { G.sel_stage = (G.sel_stage + 1) % STAGES; sfx(SFX_LAND, 0.4f, 1.3f); }
      if (cw_action_just_pressed(0, G.a_l)) { G.training = (G.training + 1) % 2; sfx(SFX_BELL, 0.3f, 1.2f); }
      if (cw_action_just_pressed(0, G.a_h) && G.players < 2) { G.difficulty = (G.difficulty + 1) % 3; sfx(SFX_BELL, 0.3f, 1.0f); }
    }
    if (p == 0 ? cw_action_just_pressed(0, G.a_t) : confirm(p)) { G.ready[p] = 1; sfx(SFX_ROUND, 0.6f, 1.0f); }
  }
  select_scene();
  bool all = G.ready[0] && (G.players < 2 || G.ready[1]);
  if (all) {
    if (G.players < 2) {
      /* the computer picks someone else, by a fixed rule so a replay agrees */
      G.sel[1] = (G.sel[0] + 1 + (G.difficulty + G.sel_stage) % (G.roster.n - 1)) % G.roster.n;
    }
    begin_match();
  }
}

/* ── the tick ───────────────────────────────────────────────────────── */

static void fixed(void *u, double dt) {
  (void)u; (void)dt;
  if (!cw_shell_update()) return;
#if defined(FIGHTER_AUTOPILOT) || defined(FIGHTER_SCRIPTED)
  if (G.state == GS_SELECT && !getenv("FIGHTER_SELECT")) {
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
  if (G.state == GS_SELECT) { update_select(); G.sim.tick++; return; } /* the clock runs for blinks and idle motion */
  if (G.state == GS_RESULT) {
    G.result_t++;
    if (G.result_t > 40 && confirm(0)) begin_match();
    else if (G.result_t > 40 && cw_action_just_pressed(0, G.a_h)) { unload_match(); G.state = GS_SELECT; G.ready[0] = G.ready[1] = 0; }
#if defined(FIGHTER_AUTOPILOT)
    if (G.result_t > 200) { G.sel_stage = (G.sel_stage + 1) % STAGES; G.sel[0] = (G.sel[0] + 1) % 4; G.sel[1] = (G.sel[1] + 1) % 4; begin_match(); }
#endif
    return;
  }
  /* training controls */
  if (G.training) {
    if (cw_action_just_pressed(0, G.tr_dummy)) { G.sim.dummy = (uint8_t)((G.sim.dummy + 1) % 6); sfx(SFX_BELL, 0.3f, 1.4f); }
    if (cw_action_just_pressed(0, G.tr_boxes)) G.show_boxes = !G.show_boxes;
    if (cw_action_just_pressed(0, G.tr_reset)) { ft_round_start(&G.sim); }
    if (cw_action_just_pressed(0, G.tr_mode)) { unload_match(); G.state = GS_SELECT; G.ready[0] = G.ready[1] = 0; return; }
  }
  ft_input in[2] = { read_pad(0), G.players > 1 ? read_pad(1) : (ft_input){ 0 } };
  ft_step(&G.sim, in);
  if (G.sim.phase == P_MATCH_END) {
    G.state = GS_RESULT;
    if (G.sim.match_winner >= 0) model_set_showcase(G.mdl[G.sim.match_winner], 0.9f);
    G.result_t = 0;
    if (SAVE && G.sim.match_winner >= 0) { SAVE->wins[G.sim.f[G.sim.match_winner].def & 3]++; SAVE->matches++; cw_shell_save_commit(); }
  }
  /* debug state */
  const ft_game *g = &G.sim;
  G.d_state = G.state; G.d_phase = g->phase; G.d_round = g->round_no; G.d_hash = ft_hash(g); G.d_combo = g->f[1].combo_hits > g->f[0].combo_hits ? g->f[1].combo_hits : g->f[0].combo_hits;
  G.d_adv = g->adv_valid ? g->adv : 0;
  for (int i = 0; i < 2; i++) {
    G.d_hp[i] = g->f[i].hp; G.d_meter[i] = g->f[i].meter; G.d_wins[i] = g->f[i].wins; G.d_pos[i * 2] = g->f[i].x; G.d_pos[i * 2 + 1] = g->f[i].y; G.d_fstate[i] = g->f[i].state; G.d_def[i] = g->f[i].def;
  }
  G.d_hitcount = fx_count_hit; G.d_blockcount = fx_count_block;
}

/* ── render-rate: poses, camera, effects, sound ─────────────────────── */

static float hash01(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return (float)(x & 0xffff) / 65535.0f; }

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
    default: break;
    }
  }
  /* a whoosh when a strike starts */
  for (int i = 0; i < 2; i++) {
    const ft_fighter *f = &g->f[i];
    if (f->state == S_ATTACK && f->t == 1) sfx(f->mv >= M_2H || f->mv == M_5H || f->mv == M_JH ? SFX_WHOOSH_H : SFX_WHOOSH_L, 0.5f, 1.0f);
  }
}

/* close-up camera for screenshots (debug command "cam", FIGHTER_CAM in test builds): zoom 0 is the normal framing;
 * otherwise it frames one fighter (0 or 1) or both (2) from closer, at chest height, turned by yaw radians */
static float g_cam[3];
static void update_camera(float dt, float aspect) {
  if (!cw_node_valid(G.camera)) return;
  const ft_game *g = &G.sim;
  cw_frame_point pts[2];
  for (int i = 0; i < 2; i++) pts[i] = (cw_frame_point){ (float)g->f[i].x / 1000.0f, 0.95f + (float)g->f[i].y / 1000.0f, 0, 0.8f };
  cw_frame_params p = { .yaw = 0, .pitch = -0.04f, .fov_y = 34.0f * 0.0174533f, .aspect = aspect, .margin = 0.12f, .min_dist = 5.4f, .max_dist = 15.0f };
  int npts = 2;
  if (G.state == GS_SELECT || G.state == GS_RESULT) { /* faces: both fighters' heads on the select screen, the winner's on the result screen */
    int w = G.state == GS_RESULT ? g->match_winner : -1;
    if (w >= 0) { pts[0] = (cw_frame_point){ (float)g->f[w].x / 1000.0f, 1.45f, 0, 0.45f }; npts = 1; }
    else for (int i = 0; i < 2; i++) pts[i] = (cw_frame_point){ (float)g->f[i].x / 1000.0f, 1.3f, 0, 0.5f }; /* aimed low: the heads sit above the cards */
    p.min_dist = w >= 0 ? 2.4f : 2.6f; p.margin = 0.02f; p.yaw = w >= 0 ? (g->f[w].facing > 0 ? 0.35f : -0.35f) : 0;
    stage_set_showcase(true);
  } else stage_set_showcase(false);
  if (g_cam[0] > 0) {
    int who = (int)g_cam[1];
    if (who < 2) { pts[0] = (cw_frame_point){ (float)g->f[who].x / 1000.0f, 1.25f + (float)g->f[who].y / 1000.0f, 0, 0.55f }; npts = 1; }
    p.min_dist = 5.4f / g_cam[0]; p.margin = 0.02f; p.yaw = g_cam[2];
  }
  cw_frame_result fit, cam;
  if (!cw_frame_fit(&p, pts, npts, &fit)) return;
  cw_frame_follow(&G.fs, &p, &fit, dt, 7.0f, &cam);
  /* a settled look-at a little above the fighters so the stage reads under them */
  float sh = fx_shake();
  G.cam_t += dt;
  float sx = (hash01((uint32_t)(G.cam_t * 90.0f) * 3 + 1) - 0.5f) * sh * 0.5f, sy = (hash01((uint32_t)(G.cam_t * 90.0f) * 5 + 2) - 0.5f) * sh * 0.4f;
  float lift = g_cam[0] > 0 ? 0.45f / g_cam[0] : 0.45f;
  t3_vec3 pos = { cam.pos[0] + sx, cam.pos[1] + lift + sy, cam.pos[2] };
  t3_vec3 tgt = { cam.target[0] + sx * 0.4f, cam.target[1] + 0.15f, cam.target[2] };
  cw_node3d_set_position(G.camera, pos);
  cw_node3d_look_at(G.camera, tgt, (t3_vec3){ 0, 1, 0 });
  G.d_cam[0] = pos.x; G.d_cam[1] = pos.y; G.d_cam[2] = pos.z;
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
  if (sc != CW_SHELL_PLAY && sc != CW_SHELL_TITLE && sc != CW_SHELL_JOIN) return;
  for (int i = 0; i < 2; i++) model_pose(G.mdl[i], &G.sim, i, dt);
  G.d_spring = model_spring_speed(G.mdl[0]) + model_spring_speed(G.mdl[1]);
  stage_frame(&G.sim, dt);
  fx_frame(&G.sim, dt);
  sound_events();
  cw_vec2 cs = cw_r2d_canvas_size();
  update_camera(dt, cs.x / cs.y);
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

static void bar(float x, float y, float w, float h, float fill, float lag, cw_color c, bool right_to_left) {
  cw_r2d_round_rect(cw_rect_make(x - 3, y - 3, w + 6, h + 6), h * 0.35f, 6, rgba(0.02f, 0.02f, 0.03f, 0.9f));
  cw_r2d_round_rect(cw_rect_make(x, y, w, h), h * 0.3f, 6, rgba(0.15f, 0.12f, 0.12f, 1));
  float lw = w * lag, fw = w * fill;
  cw_r2d_rect(cw_rect_make(right_to_left ? x + w - lw : x, y, lw, h), rgba(0.95f, 0.8f, 0.2f, 1));
  cw_r2d_rect(cw_rect_make(right_to_left ? x + w - fw : x, y, fw, h), c);
  cw_r2d_rect(cw_rect_make(right_to_left ? x + w - fw : x, y, fw, h * 0.4f), rgba(1, 1, 1, 0.22f));
}

static const char *btn_name(int b) { return b == 0 ? "L" : b == 1 ? "M" : b == 2 ? "H" : "T"; }

static void draw_training(float W, float H, float s) {
  const ft_game *g = &G.sim;
  float x = W * 0.5f - 230 * s, y = H * 0.72f;
  cw_r2d_round_rect(cw_rect_make(x, y, 460 * s, 112 * s), 10 * s, 6, rgba(0.02f, 0.03f, 0.05f, 0.75f));
  char b[160];
  const ft_def *d = &g->roster->d[g->f[g->last_who].def];
  snprintf(b, sizeof b, "Last move: %s %s", d->name, FT_MOVE_ID[g->last_move]);
  text(G.font_ui, b, x + 14 * s, y + 8 * s, 17 * s, rgba(1, 0.95f, 0.7f, 1), CW_ALIGN_LEFT);
  snprintf(b, sizeof b, "Startup %d   Active %d   Recovery %d   Total %d", g->last_su, g->last_ac, g->last_rc, g->last_su + g->last_ac + g->last_rc);
  text(G.font_ui, b, x + 14 * s, y + 32 * s, 16 * s, rgba(0.85f, 0.9f, 1, 1), CW_ALIGN_LEFT);
  if (g->adv_valid) snprintf(b, sizeof b, "Frame advantage %+d   Damage %d   Combo %d", g->adv, g->last_dmg, g->last_combo);
  else snprintf(b, sizeof b, "Frame advantage --   Damage %d   Combo %d", g->last_dmg, g->last_combo);
  text(G.font_ui, b, x + 14 * s, y + 56 * s, 16 * s, g->adv_valid && g->adv > 0 ? rgba(0.5f, 1, 0.6f, 1) : rgba(1, 0.65f, 0.55f, 1), CW_ALIGN_LEFT);
  static const char *const DUMMY[6] = { "Off", "Stand", "Crouch", "Jump", "Block all", "CPU" };
  snprintf(b, sizeof b, "Dummy: %s   Hitboxes: %s", DUMMY[g->dummy], G.show_boxes ? "on" : "off");
  text(G.font_ui, b, x + 14 * s, y + 80 * s, 15 * s, rgba(0.7f, 0.8f, 0.9f, 1), CW_ALIGN_LEFT);
  /* the input history of player 1: the last frames as direction digits and buttons, newest first */
  char h[200];
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
      for (int bb = 0; bb < 4; bb++) if (f->buttons & (1 << bb)) { tmp[len++] = btn_name(bb)[0]; }
      tmp[len] = 0;
      if (f->dir != 5 || f->buttons) { strncat(h, tmp, sizeof h - strlen(h) - 2); strncat(h, " ", sizeof h - strlen(h) - 1); n++; }
      last = f->dir; lastb = f->buttons;
    }
  }
  text(G.font_ui, h, x + 14 * s, y + 98 * s - 4 * s, 14 * s, rgba(0.6f, 0.75f, 0.6f, 1), CW_ALIGN_LEFT);
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
    text(G.font_bold, d->name, i == 0 ? x : x + bw, by + bh + 6 * s, 20 * s, rgba(1, 1, 1, 1), i == 0 ? CW_ALIGN_LEFT : CW_ALIGN_RIGHT);
    /* round pips */
    for (int w = 0; w < 2; w++) {
      float px = i == 0 ? W * 0.5f - gap - 10 * s - (float)w * 22 * s : W * 0.5f + gap + 10 * s + (float)w * 22 * s;
      cw_r2d_circle((cw_vec2){ px, by + bh + 42 * s }, 7 * s, 12, w < f->wins ? rgba(1, 0.85f, 0.2f, 1) : rgba(0.2f, 0.2f, 0.2f, 0.8f));
    }
    /* meter */
    float mw = W * 0.30f, mh = 14 * s, my = H - 34 * s, mx = i == 0 ? 28 * s : W - 28 * s - mw;
    cw_r2d_round_rect(cw_rect_make(mx - 3, my - 3, mw + 6, mh + 6), mh * 0.4f, 6, rgba(0.02f, 0.02f, 0.03f, 0.85f));
    float mf = (float)f->meter / (float)FT_METER_MAX;
    bool full = f->meter >= FT_METER_MAX;
    float pulse = full ? 0.75f + 0.25f * (float)cw_sin((float)g->tick * 0.2f) : 1;
    cw_r2d_rect(cw_rect_make(i == 0 ? mx : mx + mw * (1 - mf), my, mw * mf, mh), full ? rgba(1 * pulse, 0.85f * pulse, 0.2f * pulse, 1) : rgba(0.25f, 0.6f, 1, 1));
    text(G.font_ui, full ? "SUPER READY" : "SUPER", i == 0 ? mx : mx + mw, my - 22 * s, 15 * s, rgba(1, 1, 1, 0.9f), i == 0 ? CW_ALIGN_LEFT : CW_ALIGN_RIGHT);
    /* combo */
    const ft_fighter *o = &g->f[1 - i];
    if (o->combo_hits >= 2) {
      char c[32];
      snprintf(c, sizeof c, "%d HITS", o->combo_hits);
      float cx = i == 0 ? 40 * s : W - 40 * s;
      text(G.font_score, c, cx, H * 0.32f, 34 * s, rgba(1, 0.85f, 0.25f, 1), i == 0 ? CW_ALIGN_LEFT : CW_ALIGN_RIGHT);
      snprintf(c, sizeof c, "%d damage", o->combo_dmg);
      text(G.font_ui, c, cx, H * 0.32f + 40 * s, 18 * s, rgba(1, 1, 1, 0.9f), i == 0 ? CW_ALIGN_LEFT : CW_ALIGN_RIGHT);
    }
  }
  /* timer */
  char t[16];
  if (g->training) snprintf(t, sizeof t, "--");
  else snprintf(t, sizeof t, "%d", (g->timer + 59) / 60);
  cw_r2d_round_rect(cw_rect_make(W * 0.5f - 38 * s, by - 6 * s, 76 * s, 56 * s), 12 * s, 8, rgba(0.02f, 0.02f, 0.03f, 0.85f));
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
  if (g->training) draw_training(W, H, s);
  /* super flash */
  float fl = fx_flash();
  if (fl > 0.01f) cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(1, 1, 1, fl * 0.6f));
}

static void draw_select(float W, float H, float s) {
  /* the two picked fighters stand on the stage behind this, framed on their faces (update_camera) */
  for (int i = 0; i < 32; i++) { /* a darkening toward the cards: bands that touch but never overlap (overlaps show as stripes) */
    float y0b = floorf(H * (0.6f + 0.4f * (float)i / 32.0f)), y1b = floorf(H * (0.6f + 0.4f * (float)(i + 1) / 32.0f));
    cw_r2d_rect(cw_rect_make(0, y0b, W, y1b - y0b), rgba(0.02f, 0.025f, 0.05f, 0.2f + 0.7f * (float)i / 31.0f));
  }
  cw_r2d_rect(cw_rect_make(0, 0, W, 130 * s), rgba(0.02f, 0.025f, 0.05f, 0.55f));
  text(G.font_score, "CHOOSE YOUR FIGHTER", W * 0.5f, 14 * s, 34 * s, rgba(1, 0.9f, 0.5f, 1), CW_ALIGN_CENTER);
  char b[160];
  snprintf(b, sizeof b, "Stage: %s   (up / down)", STAGE_NAME[G.sel_stage]);
  text(G.font_ui, b, W * 0.5f, 56 * s, 19 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  snprintf(b, sizeof b, "Mode: %s   (X)", G.training ? "Training" : G.players > 1 ? "Versus" : "Versus the computer");
  if (G.players < 2) { size_t l = strlen(b); snprintf(b + l, sizeof b - l, "      Computer: %s   (B)", DIFF_NAME[G.difficulty]); }
  text(G.font_ui, b, W * 0.5f, 84 * s, 17 * s, rgba(1, 0.9f, 0.6f, 1), CW_ALIGN_CENTER);
  int n = G.roster.n;
  float cw_ = 190 * s, ch = 132 * s, gap = 16 * s, total = (float)n * cw_ + (float)(n - 1) * gap, x0 = (W - total) * 0.5f, y0 = H - 176 * s;
  for (int i = 0; i < n; i++) {
    const ft_def *d = &G.roster.d[i];
    float x = x0 + (float)i * (cw_ + gap), cx = x + cw_ * 0.5f;
    const float *c = FCOL[i & 3];
    bool p1 = G.sel[0] == i, p2 = G.players > 1 && G.sel[1] == i;
    cw_r2d_round_rect(cw_rect_make(x, y0, cw_, ch), 12 * s, 8, rgba(0.06f, 0.07f, 0.1f, 0.88f));
    cw_r2d_round_rect(cw_rect_make(x + 8 * s, y0 + 8 * s, cw_ - 16 * s, 5 * s), 2 * s, 4, rgba(c[0], c[1], c[2], 1));
    text(G.font_bold, d->name, cx, y0 + 16 * s, 24 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
    snprintf(b, sizeof b, "Life %d", d->hp);
    text(G.font_ui, b, cx, y0 + 46 * s, 13 * s, rgba(0.8f, 0.85f, 0.95f, 1), CW_ALIGN_CENTER);
    cw_text_style st = { .size = 13 * s, .color = rgba(0.75f, 0.8f, 0.9f, 1), .align = CW_ALIGN_CENTER, .wrap_width = cw_ - 20 * s };
    cw_r2d_text(G.font_ui, BLURB[i & 3], (cw_vec2){ cx - (cw_ - 20 * s) * 0.5f, y0 + 66 * s }, &st);
    if (p1) cw_r2d_round_rect_lines(cw_rect_make(x - 4 * s, y0 - 4 * s, cw_ + 8 * s, ch + 8 * s), 14 * s, 4 * s, 8, G.ready[0] ? rgba(1, 0.85f, 0.2f, 1) : rgba(1, 0.4f, 0.3f, 1));
    if (p2) cw_r2d_round_rect_lines(cw_rect_make(x - 8 * s, y0 - 8 * s, cw_ + 16 * s, ch + 16 * s), 16 * s, 3 * s, 8, G.ready[1] ? rgba(1, 0.85f, 0.2f, 1) : rgba(0.3f, 0.6f, 1, 1));
    if (p1) text(G.font_bold, "P1", x + 10 * s, y0 - 24 * s, 18 * s, rgba(1, 0.5f, 0.4f, 1), CW_ALIGN_LEFT);
    if (p2) text(G.font_bold, "P2", x + cw_ - 10 * s, y0 - 24 * s, 18 * s, rgba(0.4f, 0.7f, 1, 1), CW_ALIGN_RIGHT);
  }
  text(G.font_ui, "Left / right: fighter     A: lock in     B: back out (two players) or computer level", W * 0.5f, H - 30 * s, 14 * s, rgba(0.7f, 0.75f, 0.85f, 1), CW_ALIGN_CENTER);
}

static void draw_result(float W, float H, float s) {
  draw_hud(W, H, s);
  const ft_game *g = &G.sim;
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0, 0, 0, 0.45f));
  char b[80];
  snprintf(b, sizeof b, "%s WINS", g->roster->d[g->f[g->match_winner].def].name);
  text(G.font_score, b, W * 0.5f, H * 0.32f, 64 * s, rgba(1, 0.9f, 0.4f, 1), CW_ALIGN_CENTER);
  snprintf(b, sizeof b, "%d - %d", g->f[0].wins, g->f[1].wins);
  text(G.font_bold, b, W * 0.5f, H * 0.32f + 80 * s, 40 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  text(G.font_ui, "A: rematch      B: back to fighter select", W * 0.5f, H * 0.7f, 22 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
}

static void render(void *u, double alpha) {
  (void)u; (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  if (cw_shell_screen_now() == CW_SHELL_PLAY || cw_shell_screen_now() == CW_SHELL_PAUSE) {
    if (G.state == GS_SELECT) draw_select(W, H, s);
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
  if (to == CW_SHELL_TITLE) { unload_match(); G.state = GS_SELECT; backdrop(); }
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
  cw_reply_printf(r, "{\"zoom\":%g}", (double)g_cam[0]);
}
static void cmd_match(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: match <stage 0-2> [p1 0-3] [p2 0-3]"); return; }
  G.sel_stage = atoi(argv[1]) % STAGES;
  if (argc > 2) G.sel[0] = atoi(argv[2]) & 3;
  if (argc > 3) G.sel[1] = atoi(argv[3]) & 3;
  begin_match();
  cw_reply_printf(r, "{\"stage\":%d}", G.sel_stage);
}
#endif

static void init(void *u) {
  (void)u;
#if defined(FIGHTER_AUTOPILOT) || defined(FIGHTER_SCRIPTED)
  cw_debug_command("cam", "cam <zoom> [who 0|1|2] [yaw]: close-up camera (zoom 0: normal)", cmd_cam);
  cw_debug_command("match", "match <stage 0-2> [p1] [p2]: start a match on a stage", cmd_match);
  { const char *e = getenv("FIGHTER_CAM"); if (e) sscanf(e, "%f %f %f", &g_cam[0], &g_cam[1], &g_cam[2]); }
#endif
  cw_input_load_bindings_asset("input.cfg");
  cw_input_commit_defaults();
  G.a_left = cw_action_find("mv_left"); G.a_right = cw_action_find("mv_right"); G.a_up = cw_action_find("mv_up"); G.a_down = cw_action_find("mv_down");
  G.a_l = cw_action_find("atk_l"); G.a_m = cw_action_find("atk_m"); G.a_h = cw_action_find("atk_h"); G.a_t = cw_action_find("atk_t");
  G.k_left = cw_action_find("k2_left"); G.k_right = cw_action_find("k2_right"); G.k_up = cw_action_find("k2_up"); G.k_down = cw_action_find("k2_down");
  G.k_l = cw_action_find("k2_l"); G.k_m = cw_action_find("k2_m"); G.k_h = cw_action_find("k2_h"); G.k_t = cw_action_find("k2_t");
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
                                  .sound = shell_sound, .screen_changed = screen_changed, .font = G.font_bold, .accent = rgba(1, 0.5f, 0.3f, 1) });
  SAVE = cw_shell_game_save();
#if !defined(FIGHTER_AUTOPILOT) && !defined(FIGHTER_SCRIPTED)
  backdrop();
#endif
  G.sfx_bus = cw_shell_sfx_bus();
  G.music_bus = cw_shell_music_bus();
  for (int i = 0; i < SFX_N; i++) G.sfx[i] = cw_sound_load(SFX_FILE[i], NULL);
  for (int i = 0; i < STAGES; i++) G.music[i] = cw_sound_load(MUSIC[i], &(cw_sound_desc){ .mode = CW_SOUND_STREAM });
  G.difficulty = 1;
  G.sel[0] = 0; G.sel[1] = 1;
  cw_debug_i32("game.state", &G.d_state); cw_debug_i32("game.phase", &G.d_phase); cw_debug_i32("game.round", &G.d_round);
  cw_debug_field("sim.hp", G.d_hp, CW_DBG_I32, 2); cw_debug_field("sim.meter", G.d_meter, CW_DBG_I32, 2); cw_debug_field("sim.wins", G.d_wins, CW_DBG_I32, 2);
  cw_debug_field("sim.pos", G.d_pos, CW_DBG_I32, 4); cw_debug_field("sim.fstate", G.d_fstate, CW_DBG_I32, 2); cw_debug_field("sim.def", G.d_def, CW_DBG_I32, 2);
  cw_debug_u32("sim.hash", &G.d_hash); cw_debug_i32("sim.combo", &G.d_combo); cw_debug_i32("sim.adv", &G.d_adv); cw_debug_i32("ticks", &G.d_tick);
  cw_debug_i32("fx.hits", &G.d_hitcount); cw_debug_i32("fx.blocks", &G.d_blockcount); cw_debug_i32("view.boxes", &G.d_boxes);
  cw_debug_i32("tex.nodes", &G.d_tex_nodes); cw_debug_i32("tex.objs", &G.d_tex_objs); cw_debug_i32("tex.meshes", &G.d_tex_meshes); cw_debug_i32("tex.applied", &G.tex_done);
  cw_debug_u32("tex.hash", &tex_hash); cw_debug_f32_array("cam.pos", G.d_cam, 3); cw_debug_f32("fighter.spring", &G.d_spring);
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
