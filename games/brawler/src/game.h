/* brawler: shared types. One shared screen, 1 to 4 fighters, lane movement on x (scroll) and z (depth). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/anim.h"
#include "cartwheel/core.h"
#include "cartwheel/scene.h"
#include "cartwheel/audio.h"

#define MAXP 4
#define MAXE 40
#define MAXPROP 24
#define MAXITEM 16
#define TPS 60
#define LANE_Z 2.6f

typedef enum {
  S_IDLE, S_WALK, S_ATK, S_JUMP, S_GRAB, S_GRABBED, S_THROW, S_HIT, S_FALL, S_DOWN, S_GETUP, S_DEAD, S_PICK, S_TAUNT, S_THROWN
} bstate;

/* attack kinds: what the S_ATK state is doing */
typedef enum { A_NONE, A_PUNCH1, A_PUNCH2, A_FINISH, A_KICK, A_JKICK, A_SPIN, A_SWING, A_STAB, A_KNEE, A_SLAM, A_CHARGE, A_BSPIN, A_THROW, A_BASH, A_RUSH, A_COUNT } akind;

typedef struct atk_data {
  const char *clip;
  int total, a0, a1; /* ticks: length, active window */
  float dmg, reach, width, knock; /* knock 0 flinch, 1 stagger, 2 knockdown */
  float lunge;
} atk_data;

typedef struct char_def {
  const char *name, *model;
  float speed, hp, dmg;
  bool boss, mini;
  int score;
} char_def;

typedef struct ent {
  bool used, player;
  int def, idx;
  float x, y, z, vx, vy, vz;
  int face; /* +1 faces +x */
  bstate st;
  int t, hold; /* ticks in the state; hit-stop hold */
  float hp, maxhp;
  akind atk;
  int combo, combo_t, chain_ok;
  uint32_t hitmask;
  int invuln, stun, lives, score;
  int weapon, wdur;
  int target, victim, holder; /* entity indexes */
  int token, thinkt, slot;
  int cd; /* enemy attack cooldown */
  int phase, ptimer;
  int mood, mood_t, side_pref; /* waiting enemies: what they do while they wait for a token (circle, feint, taunt, switch sides) */
  float orbit, lx, lz;
  int still; /* ticks without moving or acting (the stillness audit) */
  int guard_hits; /* the guard: blows its shield has stopped since its guard last broke */
  int ranged; /* the thrower holds a ranged token, not a melee one */
  int knees;
  cw_node root, model, anim, wnode;
  const char *clipname;
  uint64_t agent;
  int fade;
  float hurtflash;
  bool hitflag;
} ent;

typedef struct prop_t { bool used; int kind, hp; float x, z; int drop; cw_node node; int shake; } prop_t;
typedef struct item_t { bool used; int kind; float x, z, y; int t; cw_node node; } item_t; /* kind: 0 apple 1 bat 2 pipe 3 knife 4 gem */

typedef struct inputs { float mx, mz; bool atk, kick, jump, grab, special; } inputs;

typedef struct stage_t {
  const char *name;
  float length;
} stage_t;

extern const char_def DEFS[];
extern const atk_data ATK[];
enum { D_ROOK, D_MIKA, D_JET, D_THUG, D_PUNK, D_RUNNER, D_KNIFER, D_BRUISER, D_THROWER, D_GUARD, D_FOREMAN, D_MATRON, D_RINGMASTER, D_COLOSSUS, D_COUNT };

/* the match */
typedef enum { PH_PLAY, PH_CLEAR, PH_OVER, PH_WIN, PH_INTRO } phase_t;
typedef struct world_t {
  int players, tick, stage, phase, phase_t;
  ent p[MAXP];
  ent e[MAXE];
  prop_t prop[MAXPROP];
  item_t item[MAXITEM];
  float cam_x, lock_x, scroll_goal;
  int wave, waves_left, wave_spawned;
  int spawn_q_n;
  struct { int def, side, delay; } spawn_q[24];
  int hitstop, shake;
  float shake_amp;
  int combo_n, combo_t;
  int kills, go_flash, tokens_out;
  uint32_t hash;
  cw_rng rng;
  int coins;
  int boss_idx;
  bool boss_active;
  int lives_pool;
  int total_score;
  int slow;
  int phase_banner; /* ticks left on a boss phase banner */
  struct proj { bool used; float x, y, z, vx; int owner, t; cw_node node; } proj[8]; /* thrown bottles */
  struct stats { /* what the rubric measures, per stage (reset by sim_reset) */
    int t0, first_hit, deaths, hits, blocks, pickups, unfair, still_max, frozen, nan, stall, stall_max;
    int boss_t0, boss_t1, boss_phase, chain_starts, finishers, waves_done, wave_t0, wave_max, wave_sum, dmg_taken;
    int atk_hits[A_COUNT];
  } st;
  int run_deaths; /* lives lost in the whole run */
} world_t;
extern world_t W;
/* debug views (set by the autopilot build's commands): camera zoom and offset, enemies and bots frozen */
extern float g_cam[3];
extern bool g_freeze;

/* sim.c */
void sim_reset(int players, int stage);
void sim_tick(const inputs *in);
int ent_spawn_enemy(int def, float x, float z);
void ent_hurt(ent *v, ent *a, float dmg, int knock, float fx);
/* level.c */
void level_build(int stage);
void level_free(void);
void level_update(void);
void level_camera(float dt);
int stage_wave_count(int stage);
void stage_wave_load(int stage, int wave);
const char *stage_name(int stage);
void prop_hit(prop_t *p);
float level_cam_halfw(void);
/* the camera sees this far either side of cam_x at depth z and height y (the game camera, zoom 1) */
float view_halfw(float z, float y);
bool on_screen(float x, float z, float margin);
void fx_warn(int slot, float x, float z, float sx, float sz, float k);
void sim_sync_extras(void);
void stats_log(const char *why);
int token_leak(void);
void chars_reset(void);
void sim_goto_wave(int wave);
/* fx.c */
void fx_init(void);
void fx_hit(float x, float y, float z, int kind, int face);
void fx_dust(float x, float z, float scale);
void fx_debris(float x, float y, float z, uint32_t color);
void fx_flash(float x, float y, float z, float r, float g, float b, float e);
void fx_tick(void);
void sfx(int id);
enum { SFX_HIT1, SFX_HIT2, SFX_HIT3, SFX_WHIFF, SFX_BREAK, SFX_PICK, SFX_KO, SFX_THROW, SFX_SLAM, SFX_GO, SFX_HEAL, SFX_COUNT };
/* ui.c */
void ui_draw(void);
/* characters (sim.c) */
void char_attach(ent *e);
void char_play(ent *e, const char *clip, float blend);
void char_sync(ent *e);
void char_free(ent *e);
/* fx.c audio */
void audio_init(void);
cw_sound music_make(int stage);
/* secondary.c: spring bones (hair, coat tails, belly) and character shading */
#include "three.h"
enum { SB_MAX = 5 };
typedef struct spring_bone { t3_object *bone; t3_vec3 rest, tip, T, V; bool live; } spring_bone;
typedef struct spring_rig { t3_object *scene; spring_bone b[SB_MAX]; } spring_rig;
void sb_bind(spring_rig *s, t3_object *scene);
void sb_reset(spring_rig *s);
void sb_jolt(spring_rig *s, float x, float y, float z);
void sb_step(spring_rig *s, t3_vec3 pos, float yaw, float dt);
void char_shade(t3_object *scene);
void chars_secondary(void);
void char_jolt(ent *e, float dir, float k);
