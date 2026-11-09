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
#define LANE_Z 1.9f /* the street's depth band each side of its middle (2.6 until the camera came down: a shallower band reads on screen) */
/* The game camera, in one place (the view rules in sim.c, the HUD's projection in ui.c and the camera node agree): at
   (cam_x, CAM_Y, CAM_Z), pitched CAM_PITCH down, a 40 degree vertical field (CAM_T its half tangent). A fighter in the
   middle lane is about 31 percent of the picture's height, the far lane 25, the near lane 40 (it was 28, 22 and 38 with
   the near lane's feet at the bottom edge, from 3.9 m up at a 0.3 rad pitch). W.cam_back moves it back along its own axis
   when the fighters spread wider than the picture. */
extern float g_camp[3]; /* y, z, pitch: 2.9, 7.5, 0.18 (brawler_auto's BRAWLER_EXP=oldcam: the 3.9, 7.7, 0.3 camera, perf A/B only) */
#define CAM_Y g_camp[0]
#define CAM_Z g_camp[1]
#define CAM_PITCH g_camp[2]
#define CAM_T 0.36397f
/* the jukebox on the docks pavement in the boss arena (world metres; its front faces the street) */
#define JUKE_X 53.8f
#define JUKE_Z -3.05f
#define JUKE_RMAX 9.5f /* the shockwave's last radius */
#define JUKE_TICKS 96 /* and how long it takes to get there (it slows as it grows, as Shockwave3D does) */

typedef enum {
  S_IDLE, S_WALK, S_ATK, S_JUMP, S_GRAB, S_GRABBED, S_THROW, S_HIT, S_FALL, S_DOWN, S_GETUP, S_DEAD, S_PICK, S_TAUNT, S_THROWN
} bstate;

/* attack kinds: what the S_ATK state is doing */
typedef enum { A_NONE, A_PUNCH1, A_PUNCH2, A_FINISH, A_KICK, A_JKICK, A_SPIN, A_SWING, A_STAB, A_KNEE, A_SLAM, A_CHARGE, A_BSPIN, A_THROW, A_BASH, A_RUSH, A_FLICK, A_JUKE, A_HAYMAKER, A_BARGE, A_FLASH, A_DASH, A_BACK, A_LAUNCH, A_STOMP, A_COUNT } akind;

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
  float lane_dz; /* a fighter's blow: how far it still steps across the street onto the enemy it was aimed at (the lane assist) */
  bool lane_near, landed; /* the blow had an enemy within 1.5 m along the street when it began; it landed on one */
  int throwfwd; /* the thrower: the throw goes forward (three knees), else back over the head */
  float pitch; /* a body hoisted overhead: how far it is tipped (radians; presentation) */
  int thrower; /* a thrown body: the fighter who threw it (-1 none) */
  uint64_t bowled; /* a thrown body: the enemies it has already knocked over */
  int juke; /* Arthur: walking to the jukebox to thump it (ticks on the way) */
  int buff; /* Arthur: ticks left on his theme (quicker to attack) */
  cw_node root, model, anim, wnode;
  const char *clipname;
  uint64_t agent;
  int fade;
  float hurtflash;
  int dash, tapdir, taptick, lastdir, back_face; /* a fighter's run: a double tap along the street (dash: ticks running) */
  int juggle; /* a launched body: blows it can still take in the air */
  int bounced; /* a body a fighter knocked flying: 0 it can still bounce off the picture's edge or a prop, 1 it has, 2 it cannot (not a fighter's blow) */
  float refund; int refund_t; /* the special's cost a fighter can still win back by landing blows (shown green on its bar) */
  float squash; /* the struck body squashes on the hit frame and springs back (presentation, never hashed) */
  bool hitflag;
  int react; /* ticks left on a hit reaction (the temper's clip and the emote over the head; presentation, never hashed) */
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
enum { D_ROOK, D_MIKA, D_JET, D_THUG, D_PUNK, D_RUNNER, D_KNIFER, D_BRUISER, D_THROWER, D_GUARD, D_FOREMAN, D_MATRON, D_RINGMASTER, D_COLOSSUS, D_ARTHUR, D_WANKER, D_CREEP, D_DOLLY, D_COUNT };

/* the match */
typedef enum { PH_PLAY, PH_CLEAR, PH_OVER, PH_WIN, PH_INTRO } phase_t;
typedef struct world_t {
  int players, tick, stage, phase, phase_t;
  ent p[MAXP];
  ent e[MAXE];
  prop_t prop[MAXPROP];
  item_t item[MAXITEM];
  float cam_x, lock_x, scroll_goal;
  float cam_back; /* metres the camera has drawn back along its axis (spread-out fighters); 0 most of the time */
  int wave, waves_left, wave_spawned;
  int spawn_q_n;
  int spawn_k[2]; /* enemies brought on from each side so far: each takes the next of four places across the street */
  int spawn_t[2]; /* and the tick the last one came on */
  struct { int def, side, delay; } spawn_q[24];
  int hitstop, shake;
  float shake_amp;
  int combo_n, combo_t;
  int kills, go_flash, tokens_out;
  uint32_t hash;
  cw_rng rng;
  int coins;
  int boss_idx;
  int last_hit, last_hit_t; /* the regular enemy a fighter hit last and ticks its health bar stays up (2 s; presentation) */
  bool boss_active;
  int lives_pool;
  int total_score;
  int slow;
  int phase_banner; /* ticks left on a boss phase banner */
  struct proj { bool used; float x, y, z, vx; int owner, t; cw_node node; } proj[8]; /* thrown bottles */
  struct stats { /* what the rubric measures, per stage (reset by sim_reset) */
    int t0, first_hit, deaths, hits, blocks, pickups, unfair, still_max, frozen, nan, stall, stall_max;
    int boss_t0, boss_t1, boss_phase, chain_starts, finishers, waves_done, wave_t0, wave_max, wave_sum, dmg_taken;
    int bowled; /* enemies a thrown body knocked over (the throw gate) */
    int combo_max, clear_t; /* the longest hit combo this stage; the ticks it took (the grade card) */
    int lane_press, lane_miss; /* fighters' blows thrown with an enemy within 1.5 m along the street, and those that hit nobody (a lane miss) */
    int boss_f0, boss_f1, boss_hits; /* boss_hits: blows the fighters landed on the boss; the boss fight in frames shown (ticks plus hit-stop and slow motion: what a player waits) */
    int atk_hits[A_COUNT];
  } st;
  int run_deaths; /* lives lost in the whole run */
  int moves[6]; /* the combo toolbox, landed: dash attacks, back attacks, launchers, juggle blows, blows on the downed, health won back */
  int combos[5]; /* finished hit combos by length: 1, 2, 3-4, 5-7, 8 and more */
  struct wanker { int spawned, hay, hay_hits, barge, barge_hits, kills; } wanker;
  struct creep { int spawned, flash, flash_hits, shrugged, healed, kills; } creep; /* the creep's (the gates read them) */ /* the wanker's moves (the gates read them) */
  int spawns; /* enemies spawned this match (looks.c: each spawn's look) */
  int stopped; /* frames held frozen by hit-stop (the hit feel gate) */
  int bounce[3]; /* wall bounces: bodies that bounced off the picture's edge or a prop, blows landed on them after, props they broke */
  /* the stage set pieces (the docks: the crane drops cargo on a ringed spot; the factory roof: a barrel rolls down a marked
     lane, jump it): decided here on the fixed tick, level.c only shows them. kind 0 none, 1 cargo, 2 barrel */
  struct setp { int kind, t, wave, n, cd, fired, hit_p, hit_e, dodges; float x, z, vx; uint32_t mask; } sp;
  int frames; /* sim_tick calls, hit-stop and slow-motion frames included (the stats' wall clock; never hashed) */
  /* the docks jukebox (stage 2, Arthur's arena): he thumps it, it plays his theme and a shockwave runs out along the
     floor (decided here, on the fixed tick; the lights, music and ring on screen only show it) */
  struct juke { bool here, on; int cd, t, owner, fired, uses, hits, dodges; float r, r0; uint32_t mask; } juke;
} world_t;
extern world_t W;
/* debug views (set by the autopilot build's commands): camera zoom and offset, enemies and bots frozen */
extern float g_cam[3];
extern bool g_freeze;
/* who each seat plays, and the seats that joined but have not come in (no fighter: a press of attack drops them in) */
extern int g_heroes[MAXP];
extern bool g_absent[MAXP];
int crew(void); /* fighters the match is sized for: the seats that are in (absent seats raise nothing) */
void sim_set_hero(int seat, int def); /* during the character select: that seat's fighter becomes this hero where it stands */
bool sim_join_seat(int seat);         /* an absent seat drops in at the left of the picture */
void sim_pick_done(void);            /* the select is over: absent seats leave the street, the first wave is sized for who is in */
/* the character select (main.c runs it; ui.c draws it) */
#define NHEROES 4
extern const int HEROES[NHEROES];
typedef struct pick_t { bool on; int t, wait, left; bool ready[MAXP], touched[MAXP]; } pick_t;
extern pick_t g_pick;

/* sim.c */
void sim_reset(int players, int stage);
void sim_tick(const inputs *in);
int ent_spawn_enemy(int def, float x, float z);
void ent_hurt(ent *v, ent *a, float dmg, int knock, float fx);
/* pad rumble instead of screen shake: one player (-1: every fighter's pad) */
void rumble(int player, float low, float high, int ms);
extern bool g_legacy_shake; /* autopilot debug only: the old camera shake, for the camera gate's control */
/* level.c */
void level_build(int stage);
void level_keep_next(void); /* the next level_build keeps the stage already built (Start finds it ready) */
void level_apply_bake(void); /* the scenery's bake now, not at the next level_update */
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
void fx_player_ring(int i, float x, float z, float k); /* a fighter's ring on the street, in the fighter's colour */
extern const float PLAYER_COL[4][3];
void sim_sync_extras(void);
void stats_log(const char *why);
int token_leak(void);
void chars_reset(void);
float char_hand_height(const ent *e);
void char_preload(int def); /* parse a model now, so no rig waits for it later */
void sim_put_away(void); /* every rig back in its pool, out of sight */
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
enum { SFX_HIT1, SFX_HIT2, SFX_HIT3, SFX_WHIFF, SFX_BREAK, SFX_PICK, SFX_KO, SFX_THROW, SFX_SLAM, SFX_GO, SFX_HEAL, SFX_THUMP, SFX_SWAGGER, SFX_THUD, SFX_SNAP, SFX_COUNT };
/* ui.c */
void ui_draw(void);
void ui_pick(void);
int stage_grade(int *pts); /* the stage's grade (0 S, 1 A, 2 B, 3 C, 4 D) from its time, damage taken, best combo and deaths */ /* the character select over the street */
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
typedef struct spring_bone { t3_object *bone; t3_vec3 rest, tip, T, V, G; bool live; } spring_bone;
typedef struct spring_rig { t3_object *scene; spring_bone b[SB_MAX]; } spring_rig;
void sb_bind(spring_rig *s, t3_object *scene);
t3_object *item_model(int kind); /* a pickup or weapon model (0 apple 1 bat 2 pipe 3 knife 4 coin 5 bottle), retained for the caller */
void sb_reset(spring_rig *s);
bool rig_bone_xf(const spring_rig *s, const char *bone, t3_vec3 *p, t3_quat *q); /* a bone in the model's space */
t3_vec3 rig_bone_point(const spring_rig *s, const char *bone, t3_vec3 local);
void sb_jolt(spring_rig *s, float x, float y, float z);
void sb_step(spring_rig *s, t3_vec3 pos, float yaw, float dt);
void face_express(spring_rig *s, const char *clip, float u);
enum { FACE_PLAIN, FACE_FEAR, FACE_ANGER, FACE_BLISS };
void face_temper(spring_rig *s, int temper);
int temper_of(int def); /* sim.c: FACE_FEAR, FACE_ANGER, FACE_BLISS or FACE_PLAIN by character */ /* how a face takes a hit (secondary.c clip_face) */
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
bool rag_hips(const ent *e, t3_vec3 *p); /* where a loose body's hips are in the world */
float char_scale(int def);
float rag_mean_us(void);
const char *rag_status(void);
