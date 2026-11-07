/* crawler (working title): a 1-4 player co-op maze dungeon crawler on one shared top-down screen, on cartwheel and
 * three.c (DESIGN.md).
 *
 *   play      one stick and two buttons: move; hold fire to stand and shoot the way you face; magic uses a potion
 *             (or a firebomb). Walk into a monster to fight it. Keys open doors, food gives health, the exit goes
 *             down. A fallen player comes back with fresh health on the join button.
 *   camera    one fixed zoom over the team's middle; nobody can leave the screen.
 *   crawler_auto plays itself (for hosts that send no input); crawler_bench is the perf gate's scene.
 * Environment (development, native): CRAWLER_FLOOR=n starts on floor n; CRAWLER_PLAYERS=n the auto build's
 * players; CRAWLER_BENCH=1 prints timings every 120 frames; CRAWLER_BAKE=<file> bakes the floor's lightmap there
 * (the bake tool, tools/levelview, does it for every floor); CRAWLER_QUALITY=0..3 a graphics preset;
 * CRAWLER_CHARM=n hands every player charm n and keeps it held. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cartwheel/audio.h"
#include "cartwheel/core.h"
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
#include "game.h"

static double wall(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

typedef struct save_data { int32_t best_floor, best_score; } save_data;

enum { PH_PICK, PH_PLAY }; /* the match's phases: choosing heroes, then the floors */
static struct {
  sim S;
  maze M;
  bool in_match, built;
  int players, phase, pick_ticks, start_floor, pending_floor, over_ticks;
  int cls[MAXP];
  bool ready[MAXP];
  int32_t state, floor, ticks, enemies, kills, alive, floors_done, doors, keys_used, potions_used, spawners_down, best_floor, cut, walls_drawn, drawn;
  int32_t carried, idle_anchor; /* players left behind and carried to the team; the control switch (sw idle_anchor) */
  int32_t idle[MAXP];
  float pu[MAXP], pv[MAXP]; /* where each player stands on screen, 0 to 1 (review crops) */
  int32_t hp[MAXP], score[MAXP], px[MAXP], py[MAXP];
  uint32_t hash;
  int banner_t;
  cw_action move[4], fire, magic, join;
  cw_node camera;
  int lm_frames;
  int32_t cam_jerks;
  t3_vec3 cam_p[2];
  int cam_n;
  double t_sim, t_view, t_fx, t_frame, t_last;
  int t_n;
} G;

static cw_font font;
static cw_sound snd_boom, snd_shot[CL_KINDS], snd_kill, snd_pick, snd_key, snd_door, snd_hurt, snd_magic, snd_exit, snd_move, snd_select, snd_back, snd_music;
static cw_voice music;

/* ── sounds: synthesised, so the repository needs no audio files ───── */

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
static void play(cw_sound s, float gain) { cw_audio_play(s, &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = gain }); }
static void shell_sound(cw_shell_sound s, void *u) {
  (void)u;
  play(s == CW_SHELL_SND_MOVE ? snd_move : s == CW_SHELL_SND_BACK ? snd_back : snd_select, 1);
}
static void shell_screen(cw_shell_screen from, cw_shell_screen to, void *u) {
  (void)from, (void)u;
  bool want = to == CW_SHELL_TITLE || to == CW_SHELL_PLAY;
  if (want && !cw_voice_is_playing(music)) music = cw_audio_play(snd_music, &(cw_play_desc){ .bus = cw_shell_music_bus(), .loop = true, .volume = 0.45f });
  if (!want && cw_voice_is_playing(music)) cw_voice_stop(music, 0.3f);
}

/* ── the match ──────────────────────────────────────────────────────── */

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
  G.camera = cw_node_get(root, "Main/Camera");
  G.built = true;
  return true;
}
static void build_world(void) {
  while (!build_world_step()) {}
}

/* load floor n into the sim (and the view a frame later); false when there is no such floor */
static bool floor_go(int n) {
  int count = maze_count();
  if (count > 0 && n > count) n = count;
  if (!maze_load(n, &G.M, sim_kind_by_name)) return false;
  sim_floor_start(&G.S, &G.M);
  { /* the perf gate's scene (crawler_bench, or CRAWLER_CROWD=n): a crowd already packed into the corridors round the
     * team, of the floor's own spawners' kinds, and nobody falls (so the crowd stays for the whole run) */
#ifndef CRAWLER_BENCH_CROWD
#define CRAWLER_BENCH_CROWD 0
#endif
    int crowd = getenv("CRAWLER_CROWD") ? atoi(getenv("CRAWLER_CROWD")) : CRAWLER_BENCH_CROWD;
    if (crowd > 0) {
      G.S.god = true;
      G.S.no_cap = getenv("CRAWLER_NO_CAP") != NULL; /* the stress scene: the whole crowd on screen */
      const maze *m = &G.M;
      float sx = mz_wx(m, m->start[0][0]), sz = mz_wz(m, m->start[0][1]);
      int placed = 0, near = 0; /* (with the cap: a full screen round the team, the rest queued off it) */
      for (int ring = 3; ring < MZ_MAX && placed < crowd; ring++)
        for (int y = 0; y < m->h && placed < crowd; y++)
          for (int x = 0; x < m->w && placed < crowd; x++) {
            if (m->t[y][x] != T_FLOOR || CW_MAX(abs(x - m->start[0][0]), abs(y - m->start[0][1])) != ring) continue;
            int kind = m->nspawn ? m->spawn[(x * 7 + y * 13) % m->nspawn].kind : EK_GHOUL;
            bool in = fabsf(mz_wx(m, (float)x) - sx) < VIEW_HW + 1 && fabsf(mz_wz(m, (float)y) - sz) < VIEW_HH + 1;
            if (in && !G.S.no_cap && near >= SCREEN_CAP) continue;
            for (int k = 0; k < (G.S.no_cap ? 4 : 1) && placed < crowd; k++) /* with the cap: one a tile, as the game spaces them */
              if ((!in || G.S.no_cap || near < SCREEN_CAP) && sim_spawn(&G.S, kind, 1 + (x + y + k) % 3, mz_wx(m, (float)x) + ((float)(k & 1) - 0.5f) * 0.9f, mz_wz(m, (float)y) + ((float)(k >> 1) - 0.5f) * 0.9f) >= 0) placed++, near += in;
          }
    }
  }
  view_floor(&G.S);
  G.lm_frames = 0;
  G.banner_t = 180;
  G.floor = G.S.floor;
  save_data *sd = cw_shell_game_save();
  if (n > sd->best_floor) sd->best_floor = n, G.best_floor = n, cw_shell_save_commit();
  return true;
}

static int first_floor(void) {
#ifndef CRAWLER_BENCH_FLOOR
#define CRAWLER_BENCH_FLOOR 0
#endif
  if (getenv("CRAWLER_FLOOR")) return CW_MAX(1, atoi(getenv("CRAWLER_FLOOR")));
  if (CRAWLER_BENCH_FLOOR) return CRAWLER_BENCH_FLOOR;
  return G.start_floor > 0 ? G.start_floor : 1;
}

static void begin_play(void) {
  sim_init(&G.S, G.players, G.cls, 4242);
  G.phase = PH_PLAY;
  G.floors_done = 0, G.over_ticks = 0;
  if (!floor_go(first_floor())) floor_go(1);
}

static void start(int players, void *u) {
  (void)u;
  build_world();
  G.players = players;
  G.in_match = true;
  for (int i = 0; i < MAXP; i++) G.cls[i] = i % CL_KINDS, G.ready[i] = false;
  G.start_floor = 1;
#ifdef CRAWLER_AUTOPILOT
  begin_play(); /* the bots: straight in, the classes in seat order */
#else
  G.phase = PH_PICK, G.pick_ticks = 0;
  sim_init(&G.S, players, G.cls, 4242); /* the floor behind the hero choice */
  floor_go(1);
#endif
}

static void players_changed(int players, void *u) {
  (void)u;
  for (int i = 0; i < players && i < MAXP; i++)
    if (!G.S.p[i].active && cw_shell_seat_taken(i)) G.cls[i] = i % CL_KINDS, sim_add_player(&G.S, i, G.cls[i]);
  G.players = players;
}

static bool warmup(void *u) {
  (void)u;
  if (!build_world_step()) return false;
  if (!G.in_match) { /* a floor to draw behind the title, and to compile what the match draws */
    int cls[MAXP] = { 0, 1, 2, 3 };
    sim_init(&G.S, 1, cls, 4242);
    floor_go(1);
  }
  return true;
}

/* the gates' switches */
static void cmd_sw(int argc, const char **argv, cw_reply *r) {
  if (argc < 3 || strcmp(argv[1], "idle_anchor")) { cw_reply_error(r, "usage: sw idle_anchor <0|1>"); return; }
  G.idle_anchor = atoi(argv[2]) != 0;
  cw_reply_printf(r, "{\"idle_anchor\":%d}", G.idle_anchor);
}

#ifdef CRAWLER_AUTOPILOT
static void cmd_floor(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: floor <n>"); return; }
  int n = atoi(argv[1]);
  if (!floor_go(n)) { cw_reply_error(r, "no such floor"); return; }
  cw_reply_printf(r, "{\"floor\":%d}", n);
}
#endif

static void init(void *u) {
  (void)u;
  cw_project_load_input();
  const char *names[4] = { "move_left", "move_right", "move_up", "move_down" };
  for (int i = 0; i < 4; i++) G.move[i] = cw_action_find(names[i]);
  G.fire = cw_action_find("fire"), G.magic = cw_action_find("magic"), G.join = cw_action_find("join");
  cw_shell_init(&(cw_shell_desc){ .title = "CRAWLER", .subtitle = "down through the dungeon together", .max_players = MAXP, .shared_screen = true,
                                  .game_save_size = sizeof(save_data), .game_save_version = 1, .start = start, .players_changed = players_changed,
                                  .sound = shell_sound, .screen_changed = shell_screen });
  font = cw_ui_font_sans();
  cw_prompt_set_font(font);
  cw_quality_full_resolution(true); /* no preset draws below the output size: no upscaling */
  if (getenv("CRAWLER_QUALITY")) cw_quality_set((cw_quality)CW_CLAMP(atoi(getenv("CRAWLER_QUALITY")), 0, CW_QUALITY_COUNT - 1), true); /* 0 Very Low .. 4 Ultra */
  snd_boom = synth(0.9f, 70, 30, 0.75f, 1.0f, 3.5f);
  snd_shot[CL_BRUTE] = synth(0.18f, 180, 90, 0.5f, 0.45f, 7);
  snd_shot[CL_WARDEN] = synth(0.16f, 220, 110, 0.45f, 0.45f, 8);
  snd_shot[CL_MAGE] = synth(0.3f, 140, 380, 0.65f, 0.5f, 5);
  snd_shot[CL_RANGER] = synth(0.1f, 900, 500, 0.3f, 0.3f, 9);
  snd_kill = synth(0.12f, 200, 90, 0.7f, 0.35f, 5);
  snd_pick = synth(0.12f, 1100, 1700, 0, 0.25f, 4);
  snd_key = synth(0.35f, 660, 1320, 0.05f, 0.4f, 3);
  snd_door = synth(0.9f, 60, 140, 0.4f, 0.8f, 1.5f);
  snd_hurt = synth(0.12f, 300, 120, 0.4f, 0.35f, 6);
  snd_magic = synth(1.4f, 90, 900, 0.5f, 0.9f, 1.8f);
  snd_exit = synth(1.0f, 220, 880, 0.15f, 0.6f, 1.6f);
  snd_move = synth(0.05f, 660, 660, 0, 0.25f, 2);
  snd_select = synth(0.12f, 520, 1040, 0, 0.3f, 3);
  snd_back = synth(0.12f, 520, 260, 0, 0.3f, 3);
  snd_music = synth(2.0f, 55, 55, 0.05f, 0.3f, 0.2f);
  G.best_floor = ((save_data *)cw_shell_game_save())->best_floor;
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.floor", &G.floor);
  cw_debug_i32("game.ticks", &G.ticks);
  cw_debug_i32("game.enemies", &G.enemies);
  cw_debug_i32("game.kills", &G.kills);
  cw_debug_i32("game.alive", &G.alive);
  cw_debug_i32("game.floors_done", &G.floors_done);
  cw_debug_i32("game.doors", &G.doors);
  cw_debug_i32("game.keys_used", &G.keys_used);
  cw_debug_i32("game.potions_used", &G.potions_used);
  cw_debug_i32("game.spawners_down", &G.spawners_down);
  cw_debug_i32("game.carried", &G.carried);
  cw_debug_field("player.idle", G.idle, CW_DBG_I32, MAXP);
  cw_debug_field("player.hp", G.hp, CW_DBG_I32, MAXP);
  cw_debug_field("player.score", G.score, CW_DBG_I32, MAXP);
  cw_debug_field("player.x", G.px, CW_DBG_I32, MAXP);
  cw_debug_field("player.y", G.py, CW_DBG_I32, MAXP);
  cw_debug_u32("game.hash", &G.hash);
  cw_debug_i32("view.cam_jerks", &G.cam_jerks);
  cw_debug_i32("view.cut", &G.cut);
  cw_debug_i32("view.walls", &G.walls_drawn);
  cw_debug_i32("view.drawn", &G.drawn);
  cw_debug_i32("game.on_screen", &G.S.on_screen);
  cw_debug_f32("view.p1u", &G.pu[0]), cw_debug_f32("view.p1v", &G.pv[0]), cw_debug_f32("view.p2u", &G.pu[1]), cw_debug_f32("view.p2v", &G.pv[1]);
  cw_debug_f32("view.p3u", &G.pu[2]), cw_debug_f32("view.p3v", &G.pv[2]), cw_debug_f32("view.p4u", &G.pu[3]), cw_debug_f32("view.p4v", &G.pv[3]);
  cw_debug_f32("dev.cam_dist", &g_cam_dist);
  cw_debug_f32("dev.tilt", &g_tilt);
  cw_debug_f32("dev.rim_k", &g_rim_k);
  cw_debug_f32("dev.lean", &g_lean);
  cw_debug_command("sw", "sw idle_anchor <0|1>: a test switch (1: a player nobody plays holds the team back, as before)", cmd_sw);
#ifdef CRAWLER_AUTOPILOT
  cw_debug_command("floor", "floor <n>: go to that floor", cmd_floor);
  if (getenv("CRAWLER_CAM_DIST")) g_cam_dist = (float)atof(getenv("CRAWLER_CAM_DIST")); /* key-art captures: a closer camera */
#ifndef CRAWLER_AUTO_PLAYERS
#define CRAWLER_AUTO_PLAYERS 2
#endif
  cw_shell_start(getenv("CRAWLER_PLAYERS") ? CW_CLAMP(atoi(getenv("CRAWLER_PLAYERS")), 1, 4) : CRAWLER_AUTO_PLAYERS);
#endif
}

/* ── input ──────────────────────────────────────────────────────────── */

static void read_input(input in[MAXP]) {
  memset(in, 0, sizeof(input) * MAXP);
  for (int i = 0; i < G.S.players; i++) {
#ifdef CRAWLER_AUTOPILOT
    crawler_bot(&G.S, i, &in[i]);
#else
    cw_vec2 m = cw_input_vector(i, G.move[0], G.move[1], G.move[2], G.move[3], CW_DEADZONE_SCALED_RADIAL, 0.2f);
    in[i] = (input){ m.x, m.y, cw_action_pressed(i, G.fire), cw_action_just_pressed(i, G.magic), cw_action_pressed(i, G.magic),
                     cw_action_just_pressed(i, G.join) || (G.S.p[i].down && cw_action_just_pressed(i, G.fire)) };
#endif
  }
}

static void pick_update(void) {
  /* each player chooses a hero: left and right through the four, fire to be ready; ready all (or 20 s) starts */
  G.pick_ticks++;
  int ready = 0, n = 0;
  for (int i = 0; i < G.players; i++) {
    if (!cw_shell_seat_taken(i)) continue;
    n++;
    cw_vec2 m = cw_input_vector(i, G.move[0], G.move[1], G.move[2], G.move[3], CW_DEADZONE_RADIAL, 0.5f);
    static int held[MAXP];
    int dir = m.x > 0.5f ? 1 : m.x < -0.5f ? -1 : 0;
    if (dir && !held[i] && !G.ready[i]) {
      for (int t = 0; t < CL_KINDS; t++) { /* the next class nobody else holds */
        G.cls[i] = (G.cls[i] + dir + CL_KINDS) % CL_KINDS;
        bool taken = false;
        for (int k = 0; k < G.players; k++) taken |= k != i && G.cls[k] == G.cls[i];
        if (!taken) break;
      }
      play(snd_move, 0.8f);
    }
    held[i] = dir;
    if (cw_action_just_pressed(i, G.fire)) G.ready[i] = !G.ready[i], play(G.ready[i] ? snd_select : snd_back, 0.8f);
    if (i == 0 && cw_action_just_pressed(i, G.join) && G.best_floor > 20) { /* the furthest tileset's first floor, or the first */
      int far = ((G.best_floor - 1) / 20) * 20 + 1;
      G.start_floor = G.start_floor == 1 ? far : 1;
      play(snd_move, 0.8f);
    }
    ready += G.ready[i];
  }
  if ((n > 0 && ready == n) || G.pick_ticks > 60 * 20) begin_play();
}

static void fixed_update(void *u, double dt) {
  (void)u, (void)dt;
  if (!cw_shell_update()) return;
  if (!G.in_match) return;
  if (cw_shell_join_press()) {} /* (players_changed adds them) */
  if (G.phase == PH_PICK) { pick_update(); return; }
  input in[MAXP];
  read_input(in);
  G.S.idle_anchor = G.idle_anchor != 0;
  if (getenv("CRAWLER_CHARM") && G.S.tick % 300 == 0)
    for (int i = 0; i < G.S.players; i++) sim_give_charm(&G.S, i, atoi(getenv("CRAWLER_CHARM")));
  double t0 = wall();
  int kills = G.S.killed_total;
  sim_step(&G.S, in);
  G.t_sim += (wall() - t0) * 1000;
  for (int k = 0; k < G.S.ev_n; k++) { /* sounds */
    const event *e = &G.S.ev[k];
    switch (e->kind) {
    case EV_SHOT: if (e->a < MAXP) play(snd_shot[G.S.p[e->a].cls], 0.3f); break;
    case EV_BOOM: play(snd_boom, e->a ? 0.8f : 0.4f); break;
    case EV_PICK: play(e->b == IT_KEY ? snd_key : snd_pick, 0.6f); break;
    case EV_DOOR: case EV_TRAP: case EV_DOORS_OPEN: play(snd_door, 0.8f); break;
    case EV_AMBUSH: play(snd_magic, 0.6f), G.banner_t = 0; break;
    case EV_HURT: if (G.S.tick % 6 == 0) play(snd_hurt, 0.4f); break;
    case EV_MAGIC: play(snd_magic, 0.9f); break;
    case EV_EXIT: play(snd_exit, 0.8f); break;
    case EV_SPAWNER_DIE: play(snd_boom, 0.7f); break;
    default: break;
    }
  }
  if (G.S.killed_total != kills && G.S.tick % 3 == 0) play(snd_kill, 0.35f);
  if (G.S.state == ST_WON) { /* down to the next floor */
    G.floors_done++;
    int next = G.S.next_floor;
    if (maze_count() > 0 && next > maze_count()) next = 1; /* the bottom: round again from the top */
    floor_go(next);
  } else if (G.S.state == ST_OVER) {
    if (++G.over_ticks > 60 * 4) { /* the team fell: the floor again, everyone back with fresh health */
      G.over_ticks = 0;
      for (int i = 0; i < MAXP; i++)
        if (G.S.p[i].active) G.S.p[i].down = true;
      floor_go(G.S.floor);
    }
  }
  G.state = G.S.state, G.floor = G.S.floor, G.ticks = G.S.tick, G.enemies = G.S.nenemies, G.kills = G.S.killed_total;
  G.alive = sim_alive_players(&G.S), G.doors = G.S.doors_opened, G.keys_used = G.S.keys_used, G.potions_used = G.S.potions_used;
  G.spawners_down = G.S.spawners_killed, G.hash = G.S.hash, G.carried = G.S.carried;
  for (int i = 0; i < MAXP; i++) G.hp[i] = G.S.p[i].hp, G.score[i] = G.S.p[i].score, G.px[i] = (int)(G.S.p[i].x * 100), G.py[i] = (int)(G.S.p[i].y * 100), G.idle[i] = G.S.p[i].idle;
  if (G.banner_t > 0) G.banner_t--;
}

/* ── the HUD ────────────────────────────────────────────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
static cw_text_style tstyle(float size, cw_color c) { return (cw_text_style){ .size = size, .color = c, .outline_width = size * 0.09f, .outline_color = rgba(0, 0, 0, 0.9f) }; }
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
/* lines wrapped to a width, each aligned on its own in the box [x, x + wrap] */
static cw_text_metrics text_box(const char *s, float x, float y, float wrap, float size, cw_color c, cw_text_align al) {
  cw_text_style st = tstyle(size, c);
  st.align = al, st.wrap_width = wrap;
  return cw_r2d_text(font, s, cw_v2(x, y), &st);
}
static cw_text_metrics measure_box(const char *s, float wrap, float size) {
  cw_text_style st = tstyle(size, rgba(1, 1, 1, 1));
  st.wrap_width = wrap;
  return cw_font_measure(font, s, &st);
}
/* a prompt line split at its wide gaps ("   ") into lines no wider than maxw at this size */
static const char *fit_prompt(const char *markup, int player, float size, float maxw) {
  static char o[256];
  snprintf(o, sizeof o, "%s", markup);
  if (cw_prompt_text_size(font, o, player, size).x <= maxw) return o;
  for (char *p = strstr(o, "   "); p; p = strstr(p + 1, "   ")) p[0] = '\n', memmove(p + 1, p + 3, strlen(p + 3) + 1);
  return o;
}
static const char *const THEME_NAME[MZ_THEMES] = { "THE CRYPT", "THE SEWERS", "THE FOUNDRY", "THE GROTTO", "THE CATHEDRAL", "THE ABYSS" };

static void hud_player(int i, float u, cw_vec2 cs) {
  player pv = G.S.p[i];
#ifdef CRAWLER_AUTOPILOT
  static int show = -1; /* CRAWLER_HUD_SHOW (development, the text size contact sheet): the downed and empty seat panels, drawn only */
  if (show < 0) show = getenv("CRAWLER_HUD_SHOW") != NULL;
  if (show && i == 1) pv.down = true;
  if (show && i == 3) pv.active = false;
#endif
  const player *p = &pv;
  const float *pc = PCOL[i];
  /* body text at 26 px at 1080p (2.4% of the canvas height, the engine's readable floor), the health larger; the panel
   * is sized from its measured text, so it holds the player's text size */
  float bs = 26 * u;
  char b[96], sc[24];
  bool right = i & 1, bottom = i & 2;
  float w = 300 * u, h = 92 * u;
  const char *pm = !p->active ? "{join}: join in" : p->down ? "{join}: back in" : NULL;
  snprintf(b, sizeof b, "P%d  %s", i + 1, sim_class_name(p->cls));
  snprintf(sc, sizeof sc, "%d", p->score);
  cw_text_metrics nm = measure(b, bs), sm = measure(sc, bs);
  float row2 = fmaxf(36 * u, 4 * u + nm.height + u); /* the second row's top */
  char hp[16], keys[16], pots[16];
  snprintf(hp, sizeof hp, "%d", p->hp), snprintf(keys, sizeof keys, "%d", p->keys), snprintf(pots, sizeof pots, "%d", p->potions);
  cw_text_metrics hm = measure(hp, 40 * u), km = measure(keys, bs);
  float iu = u * cw_text_drawn_size(font, bs) / bs; /* the icons grow with the text beside them */
  float ix = fmaxf(140 * u, 16 * u + hm.width + 22 * iu), px = fmaxf(ix + 72 * u, ix + 22 * iu + km.width + 24 * iu); /* the key and potion icons' x */
  if (!p->active) {
    cw_vec2 ps = cw_prompt_text_size(font, pm, i, bs);
    w = fmaxf(w, 16 * u + ps.x + 12 * u), h = fmaxf(h, 30 * u + ps.y + 8 * u);
  } else {
    w = fmaxf(w, 16 * u + nm.width + 20 * iu + sm.width + 10 * u);
    if (p->down) {
      cw_vec2 ps = cw_prompt_text_size(font, pm, i, bs);
      w = fmaxf(w, 16 * u + ps.x + 12 * u), h = fmaxf(h, row2 + 4 * u + ps.y + 8 * u);
    } else w = fmaxf(w, px + 16 * iu + measure(pots, bs).width + 12 * u), h = fmaxf(h, row2 + hm.height + 4 * u);
  }
  float x = right ? cs.x - w - 12 * u : 12 * u, y = bottom ? cs.y - h - 12 * u : 12 * u;
  cw_r2d_rect(cw_rect_make(x, y, w, h), rgba(0.03f, 0.03f, 0.05f, 0.7f));
  cw_r2d_rect(cw_rect_make(x, y, 6 * u, h), rgba(pc[0], pc[1], pc[2], 1));
  if (!p->active) {
    cw_prompt_text(font, pm, i, cw_v2(x + 16 * u, y + 30 * u), bs, rgba(1, 1, 1, 0.85f), CW_ALIGN_LEFT);
    return;
  }
  text(b, x + 16 * u, y + 4 * u, bs, rgba(0.6f + 0.4f * pc[0], 0.6f + 0.4f * pc[1], 0.6f + 0.4f * pc[2], 1), CW_ALIGN_LEFT);
  text(sc, x + w - 10 * u, y + 4 * u, bs, rgba(1, 0.92f, 0.6f, 1), CW_ALIGN_RIGHT);
  if (p->down) {
    cw_prompt_text(font, pm, i, cw_v2(x + 16 * u, y + row2 + 4 * u), bs, rgba(1, 0.7f, 0.6f, 1), CW_ALIGN_LEFT);
    return;
  }
  /* health: the big number, red when low */
  bool low = p->hp < 150;
  text(hp, x + 16 * u, y + row2, 40 * u, low && (G.S.tick / 15) % 2 ? rgba(1, 0.25f, 0.2f, 1) : rgba(1, 1, 1, 1), CW_ALIGN_LEFT);
  /* keys and potions as counts beside little shapes, on the health number's middle */
  float mid = y + row2 + hm.height * 0.55f, cy = mid - km.height * 0.5f;
  float kx = x + ix, iy = mid - 12 * iu;
  cw_r2d_circle(cw_v2(kx, iy + 10 * iu), 7 * iu, 16, rgba(1, 0.82f, 0.3f, 1));
  cw_r2d_rect(cw_rect_make(kx + 4 * iu, iy + 8 * iu, 14 * iu, 4 * iu), rgba(1, 0.82f, 0.3f, 1));
  text(keys, kx + 22 * iu, cy, bs, rgba(1, 0.9f, 0.6f, 1), CW_ALIGN_LEFT);
  kx = x + px;
  cw_r2d_circle(cw_v2(kx, iy + 12 * iu), 8 * iu, 16, rgba(0.35f, 0.6f, 1, 1));
  cw_r2d_rect(cw_rect_make(kx - 3 * iu, iy - 2 * iu, 6 * iu, 8 * iu), rgba(0.75f, 0.8f, 0.9f, 1));
  text(pots, kx + 16 * iu, cy, bs, rgba(0.7f, 0.85f, 1, 1), CW_ALIGN_LEFT);
  /* the charms held, with their seconds, wrapped to this half of the screen, by the panel's outer side */
  char cb[200];
  int n = 0;
  cb[0] = 0;
  for (int c = 0; c < CH_KINDS && n < (int)sizeof cb - 24; c++) {
    if (p->charm[c] <= 0) continue;
    if (c == CH_FIREBOMBS) n += snprintf(cb + n, sizeof cb - (size_t)n, "%s%s x%d", n ? "  " : "", sim_charm_name(c), p->charm[c]);
    else n += snprintf(cb + n, sizeof cb - (size_t)n, "%s%s %d", n ? "  " : "", sim_charm_name(c), (p->charm[c] + 59) / 60);
  }
  if (n) {
    float wrap = cs.x * 0.5f - 24 * u;
    cw_text_metrics cm = measure_box(cb, wrap, bs);
    float cy2 = bottom ? y - 2 * u - cm.height : y + h + 4 * u;
    text_box(cb, right ? x + w - 4 * u - wrap : x + 4 * u, cy2, wrap, bs, rgba(1, 0.88f, 0.45f, 1), right ? CW_ALIGN_RIGHT : CW_ALIGN_LEFT);
  }
}

/* a seat's ready prompt, on two lines when one is wider than its box */
static const char *ready_line(int i, float size, float maxw) {
  if (!G.ready[i]) return "{fire}: ready";
  return cw_prompt_text_size(font, "READY  {fire}: change", i, size).x <= maxw ? "READY  {fire}: change" : "READY\n{fire}: change";
}

/* the two lines of a hero's role, wrapped to the box, stacked */
static float role_h(const char *const role[2], float wrap, float size) {
  float h = 0;
  for (int l = 0; l < 2; l++) h += fmaxf(34 * cw_r2d_canvas_size().y / 1080.0f, measure_box(role[l], wrap, size).height);
  return h;
}

static void hud(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float u = cs.y / 1080.0f, bs = 26 * u;
  cw_r2d_begin(NULL);
  if (G.phase == PH_PICK) {
    float y = cs.y * 0.16f;
    y += text("CHOOSE YOUR HEROES", cs.x * 0.5f, y, 60 * u, rgba(1, 0.85f, 0.45f, 1), CW_ALIGN_CENTER).height;
    static const char *const ROLE[CL_KINDS][2] = { { "fights best hand to hand,", "throws axes" }, { "the heaviest armour,", "a thrown hammer" },
                                                   { "fireballs, and potions", "that crack spawners" }, { "the fastest feet", "and the quickest bow" } };
    float by = fmaxf(cs.y * 0.34f, y + 30 * u), bw = cs.x * 0.22f, inner = bw - 24 * u, bh = cs.y * 0.4f;
    /* the boxes' height: what the tallest needs at this text size (each box measured from its own text) */
    for (int i = 0; i < G.players; i++) {
      float need = 20 * u + measure("P1", 30 * u).height + 8 * u + measure(sim_class_name(G.cls[i]), 48 * u).height + 10 * u + role_h(ROLE[G.cls[i]], inner, bs) + 30 * u +
                   cw_prompt_text_size(font, "{move_left|move_right}: choose", i, bs).y + 14 * u + cw_prompt_text_size(font, ready_line(i, bs, inner), i, bs).y + 34 * u;
      bh = fmaxf(bh, need);
    }
    for (int i = 0; i < G.players; i++) {
      float bx = cs.x * (0.5f + ((float)i - (float)(G.players - 1) * 0.5f) * 0.24f);
      cw_r2d_rect(cw_rect_make(bx - bw * 0.5f, by, bw, bh), rgba(0.03f, 0.03f, 0.05f, 0.8f));
      cw_r2d_rect(cw_rect_make(bx - bw * 0.5f, by, bw, 8 * u), rgba(PCOL[i][0], PCOL[i][1], PCOL[i][2], 1));
      char b[48];
      snprintf(b, sizeof b, "P%d", i + 1);
      float ty = by + 20 * u;
      ty += text(b, bx, ty, 30 * u, rgba(PCOL[i][0], PCOL[i][1], PCOL[i][2], 1), CW_ALIGN_CENTER).height;
      ty = fmaxf(ty + 8 * u, by + 70 * u);
      ty += text(sim_class_name(G.cls[i]), bx, ty, 48 * u, rgba(1, 1, 1, 1), CW_ALIGN_CENTER).height;
      ty = fmaxf(ty + 10 * u, by + 140 * u);
      for (int l = 0; l < 2; l++) /* each line wrapped to the box, under the last as measured */
        ty += fmaxf(34 * u, text_box(ROLE[G.cls[i]][l], bx - inner * 0.5f, ty, inner, bs, rgba(0.85f, 0.85f, 0.85f, 1), CW_ALIGN_CENTER).height);
      /* the two prompts from the box's bottom up */
      const char *ready = ready_line(i, bs, inner);
      float ry = by + bh - fmaxf(60 * u, cw_prompt_text_size(font, ready, i, bs).y + 34 * u);
      float cy = fminf(by + bh - 110 * u, ry - 14 * u - cw_prompt_text_size(font, "{move_left|move_right}: choose", i, bs).y);
      cw_prompt_text(font, "{move_left|move_right}: choose", i, cw_v2(bx, cy), bs, rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER);
      cw_prompt_text(font, ready, i, cw_v2(bx, ry), bs, G.ready[i] ? rgba(0.5f, 1, 0.5f, 1) : rgba(1, 0.9f, 0.5f, 1), CW_ALIGN_CENTER);
    }
    char b[96];
    if (G.best_floor > 20) {
      float fy = fmaxf(cs.y * 0.82f, by + bh + 16 * u);
      fy += text(G.start_floor == 1 ? "FROM FLOOR 1" : "FROM THE FURTHEST TILESET", cs.x * 0.5f, fy, bs, rgba(1, 0.9f, 0.6f, 1), CW_ALIGN_CENTER).height;
      snprintf(b, sizeof b, "{join}: start at floor %d", G.start_floor == 1 ? ((G.best_floor - 1) / 20) * 20 + 1 : 1);
      cw_prompt_text(font, b, 0, cw_v2(cs.x * 0.5f, fmaxf(fy + 4 * u, cs.y * 0.86f)), bs, rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER);
    }
    cw_r2d_end();
    return;
  }
  for (int i = 0; i < G.S.players; i++) hud_player(i, u, cs);
  for (int i = 0; i < G.S.players; i++) { /* P1..P4 over each hero: who is who without the colours */
    const player *p = &G.S.p[i];
    float su, sv;
    if (!p->active || p->down || !view_screen((t3_vec3){ p->x, 2.2f, p->y }, &su, &sv)) continue;
    char t[8];
    snprintf(t, sizeof t, "P%d", i + 1);
    text(t, su * cs.x, sv * cs.y - 4 * u - measure(t, bs).height, bs, rgba(0.6f + 0.4f * PCOL[i][0], 0.6f + 0.4f * PCOL[i][1], 0.6f + 0.4f * PCOL[i][2], 1), CW_ALIGN_CENTER);
  }
  char b[96];
  snprintf(b, sizeof b, "FLOOR %d", G.S.floor);
  float top = 12 * u + text(b, cs.x * 0.5f, 12 * u, 30 * u, rgba(1, 0.92f, 0.75f, 1), CW_ALIGN_CENTER).height;
  if (G.banner_t > 0 && cw_shell_hud_shown()) { /* each line under the last as measured; not under the pause menu's lines */
    float a = cw_clampf((float)G.banner_t / 40.0f, 0, 1), y = cs.y * 0.3f;
    snprintf(b, sizeof b, "FLOOR %d", G.S.floor);
    y += text(b, cs.x * 0.5f, y, 72 * u, rgba(1, 0.85f, 0.45f, a), CW_ALIGN_CENTER).height;
    y += text(THEME_NAME[CW_CLAMP(G.S.m.theme, 0, MZ_THEMES - 1)], cs.x * 0.5f, y, 34 * u, rgba(1, 0.95f, 0.85f, a), CW_ALIGN_CENTER).height;
    if (G.S.floor == 1 && G.floors_done == 0)
      cw_prompt_text(font, fit_prompt("{fire}: hold to shoot   {magic}: a potion   walk into a monster to fight it", 0, bs, cs.x * 0.94f), 0, cw_v2(cs.x * 0.5f, y + 14 * u), bs,
                     rgba(1, 1, 1, a), CW_ALIGN_CENTER);
  }
  if (G.S.floor_ticks > 175 * 60 && G.S.floor_ticks < 186 * 60) text("THE DOORS GIVE WAY", cs.x * 0.5f, fmaxf(cs.y * 0.22f, top + 12 * u), 40 * u, rgba(1, 0.75f, 0.4f, 1), CW_ALIGN_CENTER);
  if (G.S.state == ST_EXIT || G.S.state == ST_WON) { /* the fade down the stairs */
    float a = cw_clampf((float)G.S.state_ticks / 50.0f, 0, 1);
    cw_r2d_rect(cw_rect_make(0, 0, cs.x, cs.y), rgba(0, 0, 0, a));
  }
  if (G.S.state == ST_OVER) {
    float y = cs.y * 0.4f;
    y += text("THE TEAM HAS FALLEN", cs.x * 0.5f, y, 64 * u, rgba(1, 0.3f, 0.25f, 1), CW_ALIGN_CENTER).height;
    text("the floor begins again", cs.x * 0.5f, y + 4 * u, 28 * u, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
  }
  cw_r2d_end();
}

/* ── frame ──────────────────────────────────────────────────────────── */

static void render(void *u, double alpha) {
  (void)u, (void)alpha;
  cw_shell_screen sc = cw_shell_screen_now();
  g_fx_level = CW_MAX(0, cw_gfx_effective(CW_GFX_EFFECTS) - 1); /* Low .. Ultra as 0..3; Very Low as Low */
  if (G.built) {
    static float t;
    t += 1.0f / 60.0f;
    int w, h;
    cw_window_size(&w, &h);
    view_camera(&G.S, G.camera, (float)w / (float)h); /* first: everything is culled against this frame's view */
    {
      t3_vec3 p = cw_node3d_position(G.camera);
      if (G.cam_n >= 2 && G.S.floor_ticks > 2) {
        float jx = p.x - 2 * G.cam_p[0].x + G.cam_p[1].x, jy = p.y - 2 * G.cam_p[0].y + G.cam_p[1].y, jz = p.z - 2 * G.cam_p[0].z + G.cam_p[1].z;
        if (jx * jx + jy * jy + jz * jz > 0.02f * 0.02f) G.cam_jerks++;
      }
      G.cam_p[1] = G.cam_p[0], G.cam_p[0] = p, G.cam_n++;
    }
    if (G.lm_frames >= 0 && ++G.lm_frames > 1) { /* after a frame: the light scene has placed its sun */
      const char *bake = getenv("CRAWLER_BAKE");
      view_lightmap(cw_scene_app_tree(), bake && *bake ? bake : NULL);
      G.lm_frames = -1;
    }
    double t0 = wall();
    { CW_PROFILE_SCOPE("crawler.view_sync"); view_sync(&G.S, t); }
    G.t_view += (wall() - t0) * 1000;
    t0 = wall();
    { CW_PROFILE_SCOPE("crawler.fx"); fx_events(&G.S); if (sc == CW_SHELL_PLAY) fx_tick(&G.S, t); }
    G.t_fx += (wall() - t0) * 1000;
    view_stats vs = view_get_stats();
    G.cut = vs.cut, G.walls_drawn = vs.walls_drawn, G.drawn = vs.enemies_drawn;
    for (int i = 0; i < MAXP; i++) if (G.S.p[i].active) view_screen((t3_vec3){ G.S.p[i].x, 1.0f, G.S.p[i].y }, &G.pu[i], &G.pv[i]);
  }
  bool show_hud = true;
#ifdef CRAWLER_AUTOPILOT
  show_hud = !getenv("CRAWLER_NO_HUD"); /* key-art captures: the play with no panels and no rings */
  g_hide_rings = !show_hud;
#endif
  if (show_hud && G.in_match && cw_shell_hud_shown()) { CW_PROFILE_SCOPE("crawler.hud"); hud(); }
  cw_quality_auto_measure(sc == CW_SHELL_PLAY);
  double now = wall();
  if (G.t_last > 0) G.t_frame += (now - G.t_last) * 1000;
  G.t_last = now;
  G.t_n++;
  if (getenv("CRAWLER_BENCH") && sc == CW_SHELL_PLAY) {
    static int n;
    if (++n % 120 == 0) {
      cw_render3d_stats rs;
      cw_render3d_get_stats(cw_scene_app_tree(), &rs);
      cw_particles_stats ps;
      cw_particles_get_stats(cw_scene_app_tree(), &ps);
      view_stats vs = view_get_stats();
      fprintf(stderr, "bench frame_ms=%.2f sim_ms=%.2f view_ms=%.2f fx_ms=%.2f tick=%d floor=%d enemies=%d drawn=%d walls=%d crowd_ms=%.2f cpu3d_ms=%.2f draws=%d tris=%d psys=%d palive=%d on_screen=%d\n",
              G.t_frame / G.t_n, G.t_sim / G.t_n, G.t_view / G.t_n, G.t_fx / G.t_n, G.S.tick, G.S.floor, G.S.nenemies, vs.enemies_drawn, vs.walls_drawn, G.S.crowd_ms,
              rs.cpu_ms, rs.draw_calls, rs.triangles, ps.systems, ps.alive, G.S.on_screen);
      const cw_pass_stat *pst;
      int np = cw_pipeline_pass_stats(cw_pipeline_of(cw_scene_app_tree()), &pst);
      fprintf(stderr, "bench_passes gpu_total=%.2f", cw_pipeline_gpu_ms(cw_pipeline_of(cw_scene_app_tree())));
      for (int k = 0; k < np; k++)
        if (pst[k].ran && (pst[k].gpu_ms_avg > 0.02f || pst[k].cpu_ms_avg > 0.3f)) fprintf(stderr, " %s=%.2f/%.2f", pst[k].name, pst[k].gpu_ms_avg, pst[k].cpu_ms_avg);
      fprintf(stderr, "\n");
      fflush(stderr);
      G.t_sim = G.t_view = G.t_fx = G.t_frame = 0;
      G.t_n = 0;
    }
  }
  { CW_PROFILE_SCOPE("crawler.shell_draw"); cw_shell_draw(); }
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc app = { .title = "Crawler", .width = 1280, .height = 720, .debug = true, .init = init, .warmup = warmup, .fixed_update = fixed_update, .render = render };
  cw_project_apply_app(&app);
  app.save = cw_shell_save_region();
  app.save_size = cw_shell_save_region_size();
  return cw_scene_app("scenes/main.cwscene", &app);
}
