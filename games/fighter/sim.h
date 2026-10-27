/* fighter simulation: one-on-one, 2D rules, integer only. 60 ticks a second,
 * every length is in frames, every distance in millimetres (a fighter is
 * 1750 tall, the stage 9000 wide). Nothing here touches the renderer; the
 * game and the tests drive it with ft_step(). */
#ifndef FT_SIM_H
#define FT_SIM_H

#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/motion.h"

#define FT_WALL 4400      /* fighters stay within +-FT_WALL */
#define FT_MAX_ROSTER 6
#define FT_ROUND_TIME 99  /* seconds */
#define FT_HP 1000
#define FT_METER_MAX 1000

enum { B_L = 1, B_M = 2, B_H = 4, B_T = 8, B_ATTACKS = 7 };

typedef struct ft_input { uint8_t left, right, up, down; uint16_t btn; } ft_input;
typedef struct ft_box { int16_t x0, y0, x1, y1; } ft_box; /* x forward of the fighter, y up from the feet */

enum { LV_HIGH, LV_MID, LV_LOW };
enum { FM_AIR = 1, FM_CROUCH = 2, FM_THROW = 4, FM_SPECIAL = 8, FM_SUPER = 16, FM_PROJ = 32 };
/* move slots: 3 normals x stand / crouch / air, the throw, three specials, the super */
enum { M_5L, M_5M, M_5H, M_2L, M_2M, M_2H, M_JL, M_JM, M_JH, M_THROW, M_SP1, M_SP2, M_SP3, M_SUPER, FT_NMOVES };
extern const char *const FT_MOVE_ID[FT_NMOVES];

typedef struct ft_move {
  uint8_t su, ac, rc;       /* startup, active, recovery frames (startup counts up to the first active frame) */
  uint8_t lv, hs, bs, hstop; /* level, hitstun, blockstun, hitstop (frames both fighters freeze) */
  uint8_t kd, hits, rehit, inv; /* knockdown, number of hits, frames between them, invulnerable frames from the start */
  uint8_t flags;
  int16_t dmg, pushh, pushb, launch, mv, mg; /* damage, pushback (hit, block), launch speed, forward speed while moving, meter gain */
  ft_box hit, ext;          /* active hitbox; the hurtbox the limb adds while active */
  int16_t pspeed, plife;    /* projectile: speed and life (0 = none) */
  int16_t psize;            /* projectile half size */
  uint8_t mvfrom, mvto;     /* frames of the move that carry mv */
} ft_move;

typedef struct ft_def {
  char id[12], name[16];
  int16_t hp, walk, back, jump_v, jump_fx, grav, dash, dash_back;
  int16_t w_half, h_stand, h_crouch;
  char motion[4][12];       /* sp1 sp2 sp3 super */
  cw_motion mo[4];
  ft_move mv[FT_NMOVES];
} ft_def;

typedef struct ft_roster { ft_def d[FT_MAX_ROSTER]; int n; } ft_roster;
/* Parse a moves file (see assets/data/moves.txt). Returns the fighters read, or -1 with *err set. */
int ft_roster_load(ft_roster *r, const char *text, const char **err);
/* total frames of a move */
static inline int ft_move_total(const ft_move *m) { return m->su + m->ac + m->rc; }
/* frame advantage if the first active frame hits (blocked = false) or is blocked */
int ft_advantage(const ft_move *m, bool blocked);

enum { S_INTRO, S_IDLE, S_WALKF, S_WALKB, S_CROUCH, S_PREJUMP, S_AIR, S_ATTACK, S_BLOCK, S_HIT, S_AIRHIT, S_KD, S_GETUP,
       S_THROWN, S_DASHF, S_DASHB, S_KO, S_WIN, S_LAND, S_COUNT };
extern const char *const FT_STATE_NAME[S_COUNT];

typedef struct ft_fighter {
  int32_t x, y, vx, vy, pvx;
  int8_t facing;
  uint8_t def, state, mv, connected, hit_count, air_used, crouch_block, tech_age;
  uint16_t t, stun, rehit_t;
  int16_t hp, meter, strength, combo_hits, combo_dmg, lastdmg;
  uint8_t jump_dir, invuln, wins, thrown_by_air;
  cw_cmd_buf cb;
  ft_input in;
} ft_fighter;

typedef struct ft_proj { bool live; int8_t owner; int32_t x, y, vx; int16_t life, size; uint8_t mv, hit; ft_box dummy; } ft_proj;

enum { EV_HIT, EV_BLOCK, EV_THROW, EV_KO, EV_FIRE, EV_DUST, EV_SUPER, EV_CLASH, EV_WALL, EV_TECH, EV_ROUND, EV_COUNT_ };
typedef struct ft_event { uint8_t type, who, power; int16_t pad_; int32_t x, y; uint32_t tick; } ft_event;

enum { P_INTRO, P_FIGHT, P_KO, P_ROUND_END, P_MATCH_END };
enum { T_OFF, T_STAND, T_CROUCH, T_JUMP, T_BLOCK, T_CPU };

typedef struct ft_game {
  const ft_roster *roster;
  ft_fighter f[2];
  ft_proj proj[8];
  uint8_t phase;
  uint16_t phase_t;
  int16_t timer;     /* frames left in the round */
  int8_t round_winner, match_winner, round_no;
  uint8_t hitstop;
  uint32_t tick, rng;
  ft_event ev[64];
  uint32_t ev_seq;
  /* modes */
  bool training;     /* no timer, no KO, meter and life refill, dummy settings */
  uint8_t dummy;     /* T_* what player 2 does in training */
  bool cpu[2];
  uint8_t cpu_level[2]; /* 0 easy, 1 normal, 2 hard */
  /* training readouts */
  int16_t adv;       /* last frame advantage, + = the attacker acts first */
  uint8_t adv_valid;
  int16_t last_su, last_ac, last_rc, last_dmg, last_combo;
  uint8_t last_move, last_who;
  int32_t atk_free_tick, def_free_tick; /* tick each side became actionable after the last hit */
  bool watch_adv;
  /* ai macro per fighter */
  struct { ft_input in; int16_t frames; } macro[2][12];
  int8_t macro_n[2];
} ft_game;

void ft_game_start(ft_game *g, const ft_roster *r, int def0, int def1);
void ft_round_start(ft_game *g);
/* One tick: inputs for both fighters (a CPU's input is replaced by its own). */
void ft_step(ft_game *g, const ft_input in[2]);
uint32_t ft_hash(const ft_game *g);
/* hurtboxes of a fighter in world space (body and extension); returns count */
int ft_hurtboxes(const ft_game *g, int who, ft_box out[2]);
bool ft_hitbox_active(const ft_game *g, int who, ft_box *out); /* world-space active hitbox of a move */
bool ft_actionable(const ft_fighter *f);
bool ft_airborne(const ft_fighter *f);
const ft_move *ft_cur_move(const ft_game *g, int who);
/* The AI's input for fighter `who`; macros keep motions going across ticks. */
ft_input ft_ai(ft_game *g, int who);

#endif
