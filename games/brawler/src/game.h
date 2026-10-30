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
/* the jukebox on the docks pavement in the boss arena (world metres; its front faces the street) */
#define JUKE_X 53.8f
#define JUKE_Z -3.05f
#define JUKE_RMAX 9.5f /* the shockwave's last radius */
#define JUKE_TICKS 96 /* and how long it takes to get there (it slows as it grows, as Shockwave3D does) */

typedef enum {
  S_IDLE, S_WALK, S_ATK, S_JUMP, S_GRAB, S_GRABBED, S_THROW, S_HIT, S_FALL, S_DOWN, S_GETUP, S_DEAD, S_PICK, S_TAUNT, S_THROWN
} bstate;

/* attack kinds: what the S_ATK state is doing */
typedef enum { A_NONE, A_PUNCH1, A_PUNCH2, A_FINISH, A_KICK, A_JKICK, A_SPIN, A_SWING, A_STAB, A_KNEE, A_SLAM, A_CHARGE, A_BSPIN, A_THROW, A_BASH, A_RUSH, A_FLICK, A_JUKE, A_HAYMAKER, A_BARGE, A_FLASH, A_COUNT } akind;

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
  int juke; /* Arthur: walking to the jukebox to thump it (ticks on the way) */
  int buff; /* Arthur: ticks left on his theme (quicker to attack) */
  cw_node root, model, anim, wnode;
  const char *clipname;
  uint64_t agent;
  int fade;
  float hurtflash;
  bool hitflag;
  uint32_t look; /* a regular enemy's skin, hair and clothes (looks.c look_pick; 0: the model's own); never hashed */
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
enum { D_ROOK, D_MIKA, D_JET, D_THUG, D_PUNK, D_RUNNER, D_KNIFER, D_BRUISER, D_THROWER, D_GUARD, D_FOREMAN, D_MATRON, D_RINGMASTER, D_COLOSSUS, D_ARTHUR, D_WANKER, D_CREEP, D_COUNT };

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
    int boss_f0, boss_f1, boss_hits; /* boss_hits: blows the fighters landed on the boss; the boss fight in frames shown (ticks plus hit-stop and slow motion: what a player waits) */
    int atk_hits[A_COUNT];
  } st;
  int run_deaths; /* lives lost in the whole run */
  struct wanker { int spawned, hay, hay_hits, barge, barge_hits, kills; } wanker;
  struct creep { int spawned, flash, flash_hits, shrugged, healed, kills; } creep; /* the creep's (the gates read them) */ /* the wanker's moves (the gates read them) */
  int spawns; /* enemies spawned this match (looks.c: each spawn's look) */
  int frames; /* sim_tick calls, hit-stop and slow-motion frames included (the stats' wall clock; never hashed) */
  /* the docks jukebox (stage 2, Arthur's arena): he thumps it, it plays his theme and a shockwave runs out along the
     floor (decided here, on the fixed tick; the lights, music and ring on screen only show it) */
  struct juke { bool here, on; int cd, t, owner, fired, uses, hits, dodges; float r, r0; uint32_t mask; } juke;
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
/* pad rumble instead of screen shake (monteslu: no shaking screen): one player (-1: every fighter's pad) */
void rumble(int player, float low, float high, int ms);
extern bool g_legacy_shake; /* autopilot debug only: the old camera shake, for the camera gate's control */
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
int rig_count(void);
void sim_goto_wave(int wave);
/* fx.c */
void fx_init(void);
void fx_hit(float x, float y, float z, int kind, int face);
void fx_dust(float x, float z, float scale);
void fx_blood(float x, float y, float z, int dir, int knock); /* droplets from a blow and a spatter under a hard one */
void fx_stain(float x, float z, float size, int kind); /* a stain on the street: 0 drops, 1 a splat, 2 a pool */
void fx_blood_stats(int out[4]); /* stains placed, droplet bursts, stains showing now, same-shape stains overlapping now (0) */
void fx_debris(float x, float y, float z, uint32_t color);
void fx_flash(float x, float y, float z, float r, float g, float b, float e);
void fx_tick(void);
void sfx(int id);
enum { SFX_HIT1, SFX_HIT2, SFX_HIT3, SFX_WHIFF, SFX_BREAK, SFX_PICK, SFX_KO, SFX_THROW, SFX_SLAM, SFX_GO, SFX_HEAL, SFX_THUMP, SFX_SWAGGER, SFX_COUNT };
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
/* looks.c: per-spawn skin tones and clothes for the regular enemies */
uint32_t look_pick(int def, int self);
void looks_bind(int rig, int def, t3_object *inst);
void looks_apply(int rig, uint32_t look);
void looks_reset(void);
int looks_clones(void); /* pairs of enemies of one kind on the street that look the same (0 always) */
enum { SB_MAX = 5 };
typedef struct spring_bone { t3_object *bone; t3_vec3 rest, tip, T, V; bool live; } spring_bone;
typedef struct spring_rig { t3_object *scene; spring_bone b[SB_MAX]; } spring_rig;
void sb_bind(spring_rig *s, t3_object *scene);
t3_object *item_model(int kind); /* a pickup or weapon model (0 apple 1 bat 2 pipe 3 knife 4 coin 5 bottle), retained for the caller */
void sb_reset(spring_rig *s);
void sb_jolt(spring_rig *s, float x, float y, float z);
void sb_step(spring_rig *s, t3_vec3 pos, float yaw, float dt);
void face_express(spring_rig *s, const char *clip, float u);
enum { FACE_PLAIN, FACE_FEAR, FACE_ANGER, FACE_BLISS };
void face_temper(spring_rig *s, int temper); /* how a face takes a hit (secondary.c clip_face) */
float face_weight(const spring_rig *s, int target);
void chars_secondary(void);
void char_jolt(ent *e, float dir, float k);
/* rag.c: ragdolls for falls, throws and deaths (presentation only: never read by the simulation) */
void rag_level_build(void);
void rag_reset(void);
void rag_tick(void);
void rag_debug_fields(void);
bool rag_take_corpse(int rid, const ent *e);
bool rag_render(const ent *e, float *yaw, t3_vec3 *off);
float rag_mean_us(void);
const char *rag_status(void);
