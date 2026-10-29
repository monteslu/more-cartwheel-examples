/* Party Pack: the mini-game framework.
 *
 * A mini-game is one mg_def: a name, how it is played, and a few callbacks.
 * The framework (main.c) owns everything around it: the hub and board, the
 * intro card with ready-up, the 3-2-1 countdown, the clock, the player chips
 * and score strip, the finish banner, the results and the points.
 *
 * Four players always take part: players 0..humans-1 are people, the rest
 * are CPUs. A game never reads a pad itself: it reads ctx->in[p], which the
 * framework fills from the pad for a person and from the game's own think()
 * for a CPU. A game plays by itself when all four are CPUs, which is how the
 * tests and the autopilot build play it.
 *
 * Everything that changes the result runs in tick(), from ctx->in and
 * ctx->rng only, 60 times a second, so a replay is exact on every host.
 * draw() is only looking. */
#ifndef PP_MG_H
#define PP_MG_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/core.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/physics_nodes.h"
#include "cartwheel/render2d.h"
#include "cartwheel/scene.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/ui.h"

#define NP 4
#define STAGE_W 1280.0f
#define STAGE_H 720.0f
#define TAU 6.28318530718f

typedef struct mg_in {
  float x, y;           /* stick / d-pad, -1..1, y down */
  bool a, b, c, d;      /* held: south, east, west, north */
  bool pa, pb, pc, pd;  /* went down this tick */
} mg_in;

typedef struct mg_ctx {
  int humans;        /* players 0..humans-1 are people */
  bool human[NP];
  mg_in in[NP];
  cw_rng rng;
  int tick;          /* play ticks since GO */
  int skill;         /* CPU skill 0 easy, 1 medium, 2 hard (the setting; each CPU seat reads cpu_skill[p]) */
  int cpu_skill[NP]; /* each seat's skill: a CPU seat plays at this level (the settings' level, or what PP_SKILLS asks for in a test) */
  int underdog[NP];  /* 1 for a seat the board is helping this round (behind the leader): games may ease its start a little, never its score */
  float time_left;   /* seconds; the clock stops at 0 and ends the game */
  float time_total;
  float score[NP];   /* set by the game: higher ranks better */
  bool done;         /* set by the game to end early (the clock does it otherwise) */
  bool eliminated[NP]; /* drawn greyed in the strip (games that knock players out) */
  int round;         /* tournament round, 0-based (variants may use it) */
} mg_ctx;

typedef struct mg_def {
  const char *id;       /* save key and debug name */
  const char *name;
  const char *tagline;  /* the rule in one line on the card: what you do and how you win */
  int buttons;          /* buttons the game needs besides the stick (0 to 2: the card shows them as pictures) */
  const char *keys;     /* the pictogram on the card, one glyph per letter: A B X Y, or S for the stick */
  const char *how[3];   /* control lines on the intro card, NULL-terminated */
  const char *kind;     /* "2D", "3D" (shown on the card) */
  const char *scene;    /* a 3D game's scene (assets/scenes/...); NULL = drawn in 2D over the hub scene */
  float seconds;        /* the clock; 0 = the game ends itself */
  int unlock_points;    /* lifetime party points needed (0 = open from the start) */
  bool higher_is_better; /* ranking by score: true normally */
  bool team_pairs;      /* reserved */
  void (*start)(mg_ctx *c);                          /* the scene is loaded; reset everything */
  void (*tick)(mg_ctx *c);                           /* one play tick */
  void (*think)(mg_ctx *c, int p, mg_in *out);       /* a CPU's input */
  void (*draw)(mg_ctx *c, float alpha, float t);     /* the frame in stage units; 3D games draw their HUD only */
  void (*stop)(mg_ctx *c);                           /* the game is over or left: free what start made */
  void (*score_text)(mg_ctx *c, int p, char *buf, int cap); /* the chip's live line (optional) */
  float (*music_bpm)(void);                          /* optional: own music */
} mg_def;

#define MG_MAX 16
extern int MG_N;
extern const mg_def *MG_LIST[MG_MAX];
extern const mg_def mg_dodge, mg_memory, mg_beat, mg_quiz, mg_tower, mg_blast, mg_raft, mg_kart, mg_sumo, mg_coins, mg_tiles;

/* ── shared looks and sounds (common.c) ─────────────────────────────── */
extern const cw_color PCOL[NP];
extern const char *PNAME[NP];

typedef enum {
  SFX_BLIP, SFX_SELECT, SFX_BACK, SFX_COIN, SFX_HIT, SFX_BOOM, SFX_JUMP, SFX_WIN, SFX_LOSE,
  SFX_TICK, SFX_GO, SFX_SPLASH, SFX_BOUNCE, SFX_BUZZ, SFX_POP,
  SFX_NOTE0, /* eight notes of a scale follow */
  SFX_N = SFX_NOTE0 + 8
} sfx_id;
void sfx_init(void);
void sfx(int id, float vol, float pitch);
void sfx_music(int which); /* 0 off, 1 hub, 2 game */
void sfx_music_stop(void);

/* rumble (cartwheel/input.h): the feel of a hit or a blast in the pad of the player it concerns, a person's only (never a
 * CPU seat's); the view never shakes. Presentation only: nothing reads it back, so a replay is unchanged. A request on
 * top of one still running keeps the stronger of each motor and the longer time; the motors stop on their own. */
void pp_rumble(const mg_ctx *c, int p, float low, float high, int ms);
void pp_rumble_tick(void);     /* the framework, every tick: the rumble's clock */
void pp_rumble_stop(void);     /* every pad's motors off now (out of play, Rumble turned off) */
extern bool pp_rumble_off;     /* the menu's Rumble setting (saved) */
extern int32_t pp_rumble_sent[NP]; /* debug rumble.sent: requests sent to each seat's pad */

/* particles and popups in stage units */
void fx_reset(void);
void fx_burst(cw_vec2 p, int n, cw_color c, float speed, float life, float size);
void fx_ring(cw_vec2 p, float r0, float r1, float life, cw_color c);
void fx_text(cw_vec2 p, const char *s, cw_color c, float size);
void fx_confetti(cw_rect area, int n);
/* freeze the whole play for a few ticks on a big hit: a 2D game's tick is skipped, a 3D game's scene is paused,
 * the clock holds either way. It is part of the simulation (counted in ticks), so a replay agrees. The longest
 * request of a tick wins, at most 10 ticks. */
void fx_hitstop(int ticks);
void fx_impact(void); /* an impact for the shake check (cam.offset); every fx_hitstop is one */
extern int32_t cam_offset_dbg[3];
void cam_offset_3d(cw_node cam, float px, float py, float pz); /* the shake check on a 3D camera: as drawn against where the game frames it */
int fx_hitstop_take(void); /* the framework, after each play tick: the freeze asked for (0 none), and clears it */
void fx_update(float dt);
void fx_draw(void);

/* drawing helpers (stage units) */
void ui_text(const char *s, cw_vec2 p, float size, cw_color c, cw_text_align al);
void ui_text_wrap(const char *s, cw_rect r, float size, cw_color c, cw_text_align al);
float ui_text_w(const char *s, float size);
void ui_panel(cw_rect r, cw_color fill, cw_color edge);
void ui_bar(cw_rect r, float f, cw_color c);
void ui_pill(cw_rect r, cw_color c);
void ui_button_glyph(cw_vec2 p, float r, char which); /* A B X Y in pad colours */
void ui_stick_glyph(cw_vec2 p, float r);              /* the stick, as a picture */
/* the party character: a round blob with eyes. look: -1..1 gaze; squash 1 = normal; blink; hat 0 none */
void ui_blob(cw_vec2 p, float r, int player, float look_x, float look_y, float squash, int mood, bool cpu);
cw_color col_mix(cw_color a, cw_color b, float t);
cw_color col_a(cw_color c, float a);
float ease_out(float t);
float ease_back(float t);
cw_rect stage_rect(void);   /* the 16:9 stage inside the canvas */
void stage_begin(void);     /* set the transform: draw in 1280x720 */
void stage_end(void);
void ui_backdrop(float t, cw_color top, cw_color bottom);
void ui_grad(cw_rect r, cw_color top, cw_color bot);
void ui_grad_h(cw_rect r, cw_color left, cw_color right);
void ui_vignette(float k);
void ui_title(const char *s, cw_vec2 p, float size, float t);
void bg_ridge(const char *name, float scroll, float y_bottom, float h, cw_color tint);
void bg_clouds(float t, float y, float scale, float alpha);

/* baked lit sprites (art.c): lit 2D layer for the world, flat for the HUD */
extern cw_color art_ambient;
void art_world(bool lit);            /* true: draw into the lit world layer; false: the unlit HUD layer */
bool art_is_lit(void);
void art_draw(const char *name, cw_vec2 center, cw_vec2 size, float rot, cw_color tint); /* tint {0} = white */
void art_draw_ex(const char *name, cw_vec2 pos, cw_vec2 size, cw_vec2 pivot, float rot, cw_color tint, bool fx, bool fy);
void art_glow(const char *name, cw_vec2 center, float size, cw_color tint);
void art_nine(const char *name, cw_rect dst, float margin, float scale, cw_color tint);
void art_light(cw_vec2 p, float radius, cw_color c, float energy, bool shadows);
void art_preload(const char **names, int n);

/* the framework asks and the games use */
int mg_cpu_count(const mg_ctx *c);
uint32_t hash_u32(uint32_t h, uint32_t v);

/* tiny deterministic float helpers */
static inline float fclampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float flerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float fabsf_(float x) { return x < 0 ? -x : x; }

/* ── 3D helpers (common3d.c) ───────────────────────────────────────── */
cw_node n3_level(void);                 /* the scene's root node */
cw_node n3_find(const char *name);      /* a node under the root by path */
cw_node n3_box(cw_node parent, const char *name, t3_vec3 pos, t3_vec3 size, cw_color col);
cw_node n3_prim(cw_node parent, const char *name, const char *primitive, t3_vec3 pos, t3_vec3 size, cw_color col);
void n3_color(cw_node n, cw_color c);
void n3_emissive(cw_node n, cw_color c);
void n3_pos(cw_node n, float x, float y, float z);
void n3_rot(cw_node n, float rx, float ry, float rz);
void n3_scale(cw_node n, float x, float y, float z);
cw_vec2 n3_project(t3_vec3 world);      /* a world point on the stage (for labels over 3D) */
void n3_camera(float px, float py, float pz, float tx, float ty, float tz);
/* one-shot effects (GPU particles and a light flash) that remove themselves */
void n3_fx_reset(void);
void n3_fx(const char *kind, t3_vec3 pos, float scale); /* "fire" "smoke" "sparks" "splash" "dust" */
void n3_flash(t3_vec3 pos, cw_color c, float energy, float range);
void n3_fx_tick(void);
void n3_shockwave(t3_vec3 pos, float radius, float seconds, float strength);
cw_node n3_glb(cw_node parent, const char *name, const char *path, bool cast); /* a model from models/ as a node */
void n3_receive(cw_node n, bool on);
cw_node n3_decal(const char *texture, t3_vec3 pos, float size, cw_color c, const char *blend); /* projected down, blend MIX MULTIPLY ADD */

/* ── the 3D hub (hub.c): table, board tiles, tokens, trophy, podium ── */
void hub_build(int humans);               /* after the hub scene is loaded */
void hub_cells(int n);                    /* board length */
void hub_mode(int mode);                  /* 0 stage, 1 board, 2 podium */
void hub_cell(int i, int kind);           /* a board cell: 0 plain, 1 star, 2 the end */
void hub_token(int p, float cell, float hop);
void hub_podium_slot(int p, int rank, float lift);
cw_vec2 hub_cell_screen(int i);
cw_vec2 hub_podium_screen(int rank);
void hub_update(float dt);

#endif
