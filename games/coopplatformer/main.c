/* coopplatformer (working title): the app. Shell (title, join, pause, settings, saves), input, the three.c scene
 * (lights, post, characters, effects), the HUD and map, and generated sound.
 *
 * Built twice: `coopplatformer` reads the devices; `coopplatformer_auto` plays itself with the bots in sim.c
 * (a two-player run, no input), for tests and screenshots.
 *
 * Debug fields (g.*): state, players, tick, abilities, coins, kills, deaths, rescues, secrets, checkpoint, boss_hp,
 * won, hash, pos (player 0), cam (x, y, z), hp, bubbles, signgate, signmove, labelmove, cardgate, bossgate, bossclip, endwall, labelgate. */
#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/debug.h"
#include "cartwheel/explosions.h"
#include "cartwheel/input.h"
#include "cartwheel/prompt.h"
#include "cartwheel/particles.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/platform.h"
#include "cartwheel/quality.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/rim_light.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/shell.h"
#include "cartwheel/splats.h"
#include "cartwheel/ui.h"
#include "game.h"

/* ── saves ─────────────────────────────────────────────────────────── */
typedef struct save_t {
  uint32_t abilities;
  int32_t coins, best_ticks;
  uint8_t seen[WORLD_H / 4][WORLD_W / 4];
  uint32_t reached; /* flags touched in this level (the map warps between them) */
  int32_t level;    /* where the group got to (the level a new match starts on) */
  int32_t unlocked; /* the furthest level reached: the map can go back to any before it */
  int32_t total_coins, total_deaths, total_betrayals;
  int32_t hats;     /* levels whose secrets were all found: each one better, in a sense, than the last */
  uint32_t hat_levels[2];
  uint8_t medals[32]; /* per level, bit k: big coin k + 1 has been taken there (the map counts them) */
} save_t;

#define NTORCH 4
static const float PLAYER_RGB[MAX_PLAYERS][3] = { { 0.25f, 0.6f, 1.0f }, { 1.0f, 0.4f, 0.3f }, { 0.45f, 0.85f, 0.35f }, { 1.0f, 0.8f, 0.25f } };

/* ── scene ─────────────────────────────────────────────────────────── */
static struct {
  cw_node root, world, cam, sun, env, post;
  cw_node plight[MAX_PLAYERS];
  cw_node torch[4];
  cw_node pnode[MAX_PLAYERS];
  t3_mesh *bubble[MAX_PLAYERS];
  rig *prig[MAX_PLAYERS];
  cw_node pmodel[MAX_PLAYERS];
  float yaw[MAX_PLAYERS], runph[MAX_PLAYERS];
  float spread_x[MAX_PLAYERS], spread_z[MAX_PLAYERS]; /* drawn apart when players stand in one spot */
  cw_node enode[MAX_ENEMIES], emodel[MAX_ENEMIES];
  rig *erig[MAX_ENEMIES];
  t3_mesh *eextra[MAX_ENEMIES][2]; /* the new kinds' parts: a shell and its rim, a slider's spikes, a dropper's thread */
  float ephase[MAX_ENEMIES], eyaw[MAX_ENEMIES];
  cw_node coin_node[MAX_COINS];
  t3_mesh *coin_mesh[MAX_COINS];
  cw_node pick_node[MAX_PICKUPS];
  t3_mesh *plat_mesh[MAX_PLATFORMS][2];
  int nplat_mesh[MAX_PLATFORMS];
  cw_node fx_dust[MAX_PLAYERS], fx_spark, fx_splash, fx_burst, fx_ember[4], fx_hit;
  cw_node fog, rim, fvol[8], water3d[16];
  cw_node flame[MAX_TORCHES]; /* one flame per brazier, in its basket (children of the world) */
  float flame_at[MAX_TORCHES][4];
  bool flame_on[MAX_TORCHES];
  int nflame;
  cw_node fx_boom, fx_boomsmoke, fx_boomspark, fx_wave, fx_motes, fx_haze[4], fx_bubbles, fx_foam, fx_lavapop[4], fx_debris;
  bool prev_alive[MAX_ENEMIES];
  int nwspan, nlspan;
  float wspan[16][4], lspan[48][4];
  int nember;
  float time;
  float light_x[64], light_y[64], light_z[64], light_r[64], light_g[64], light_b[64], light_e[64], light_range[64];
  int nlight_src;
  bool built;
  bool graves_due;
  t3_mesh *hat[MAX_PLAYERS][6][2];
  cw_splats *blobs;         /* a soft contact shadow under every character (they throw no sun shadow onto the backdrop) */
  int rim_biome;            /* the region the heroes' rims were last set for */
} S;

static struct {
  bool on;
  cw_bus music_bus, sfx_bus;
  cw_sound sfx[16];
  cw_sound music;
  cw_voice music_voice;
} A;
enum { SFX_JUMP, SFX_LAND, SFX_COIN, SFX_HURT, SFX_SPLASH, SFX_DASH, SFX_POWER, SFX_STOMP, SFX_BREAK, SFX_BUBBLE, SFX_POP, SFX_DIE, SFX_WIN, SFX_CHECK, SFX_WALL, SFX_COUNT };

static cw_action a_left, a_right, a_up, a_down, a_jump, a_run, a_dash, a_grab, a_map, a_retry;
static input_state INPUT[MAX_PLAYERS];
static bool g_map_open;
static int join_seats[4] = { -1, -1, -1, -1 }; /* who holds each seat on the join screen (join_seats_track) */
static bool g_attract; /* the title and join screens: four bots play the meadow behind the menu */
static int g_map_sel = -1; /* the flag the map's cursor is on */
static int g_map_level = -1; /* the level the map's cursor is on */
static int g_dev_level_keep = -1; /* travel to this level, keeping what has been found */
static cw_font g_font;
static void title_background(cw_shell_screen scr, cw_vec2 cv, void *user);
static int g_state_dbg, g_players_dbg, g_bubbles_dbg, g_hp_dbg, g_map_seen_dbg;
static float g_pos_dbg[2], g_cam_dbg[3], g_ppos_dbg[MAX_PLAYERS * 2];
static int g_idle_dbg[MAX_PLAYERS]; /* g.idle: ticks since each player's pad was touched */
/* the camera as drawn, measured every frame (the camera stability gate): g.view is x, y, z (and 0: nothing shakes it);
 * g.camgate counts frames at rest (every player still for 2.5 s), at-rest frames where the view moved more than
 * 1 mm, frames in motion, and frames in motion where the view jumped (over 0.6 m) or jerked (its step changed by
 * over 8 cm); g.camjit keeps the largest at-rest step, step in motion and step change seen (metres) and the frame of
 * the first frame in motion that broke the bound (frames since the match started) */
static float g_view_dbg[4], g_camjit_dbg[4];
static int g_camgate_dbg[4];
static bool g_cam_bug; /* COOP_CAM_BUG: the camera gates' control */
static int g_dev_warp = -1; /* dev.warp: write a column and every player is put there (romdev sweeps reach any region) */
static int g_dev_worlds;
static int g_dev_level = -1; /* dev.level: write a level and the group goes there, as a sweep or a gate does on a host with no environment */
static int g_scenery_dbg[2]; /* the scenery check on the built world: props seated, props off their ground (world_scenery_check) */
static int g_marker_over_dbg; /* markers drawn over the player panels (the rescue-tag gate) */
static int g_tags_dbg[2]; /* HUD rescue tags: frames with two or more, frames where two of them overlapped */
static bool g_hud_bug; /* COOP_HUD_BUG: the rescue-tag gate's control */
static int g_sign_dbg[3]; /* signs: frames a panel was up, of them over a player's body, of them thinner than its distance asks */
static bool g_sign_bug; /* COOP_SIGN_BUG: the sign gate's control */
static int g_card_dbg[3]; /* the results card: frames up, of them with the portraits on the line above them, levels whose closing line takes two rows */
static int g_endwall_dbg[3]; /* frames with a player in reach of a live boss, the most of the view's width the end wall took (in thousandths), frames it took over 12 percent */
static int g_bossclip_dbg[3]; /* frames a boss was drawn, of them with an arm in a wall, the deepest (mm) */
static int g_label_dbg[4]; /* frames a boss's words were up, of them on a marker, frames a player's words were up, of them on a marker */
static int g_signmove_dbg[2]; /* signs: times a panel changed place, frames it was faded or held back for that */
static float g_sign_box[4]; /* the panel up this frame: x, y, width (0: none), height; other words keep off it */
static float g_caption_box[4], g_caption_prev[4]; /* the sign's panel as drawn this frame and the last (width 0: none): the chips and the level name stand aside for it */
static int g_words_dbg[3]; /* frames with words up (a caption, a boss's words, a player's), of them with the sign's caption on a marker, of them with two boxes of words overlapping */
static bool g_words_bug; /* the words gate's control (native): captions neither keep off markers nor off each other */
static int g_labelmove_dbg[2]; /* times the boss's words and a player's words were moved off a marker */
static bool g_label_bug; /* COOP_LABEL_BUG: the label gate's control */
static bool g_card_bug; /* COOP_CARD_BUG: the card gate's control */
static int g_boss_dbg[4]; /* this match's last boss, kept into the next level: its kind + 1, frames alive in view, hits taken, frames drawn since it died */
static int g_vanish_dbg; /* frames every player on the field was hidden by the invulnerable blink */
static int g_blink_live, g_blink_hid;
static bool g_blink_bug; /* COOP_BLINK_BUG: the blink gate's control */
static bool g_coin_flat_bug; /* COOP_COIN_FLAT_BUG: the coin gate's control: the coins lie flat */
static int g_coinface_dbg[2]; /* coins checked, coins whose face does not stand upright */
static int g_uvrep_dbg;
static int g_nearcover_dbg[2]; /* players drawn, of them with the near layer in front of their feet (never: it stays low) */
static int g_pitview_dbg[2]; /* frames someone has been below the lip of a pit, of them frames the drawn camera went below the lip */
static int g_graves_dbg[2]; /* graves on the level, of them not standing on stable ground */
static int g_things_dbg[2]; /* things and signs checked on the built level, of them not on the ground */
static int g_level_dbg[2];  /* the level being played, how many there are */
static int g_gore_dbg[3];   /* deaths seen, of them played as a big gag, stains on the level */
static int g_music_dbg[3];  /* the track playing (-1 none), frames played last tick, ticks the clock moved */
static int g_surface_dbg[3]; /* world_surface_audit */
static int g_flames_dbg[2]; /* the fire check: flames burning, flames not in a seated brazier (floating fire) */
static float g_viewbot_dbg = 1e9f; /* the lowest the bottom edge of the drawn view went this match (world y; below 0 shows the underside of the world) */
static int g_underview_dbg[2]; /* frames drawn in the match, of them frames whose view reached under the world floor */
static int g_blob_dbg[3]; /* characters drawn this frame, of them with a contact blob, blobs not on a floor's top (none: nothing floats) */
static int g_charshadow_dbg; /* character meshes that throw a sun shadow (none: their shadows fell on the backdrop as detached figures) */
static bool g_shadow_old; /* COOP_SHADOW_OLD: the characters throw sun shadows again and have no blob (the shadow gate's control) */
static bool g_rim_off; /* COOP_RIM_OFF: no rim on the heroes (the rim gate's control) */
static bool g_tally_off; /* COOP_TALLY_OFF: the card without its tally (the tally check's control) */
static int g_tally_dbg[2]; /* tally columns drawn (a player each frame), frames with the awards line */
static int g_hint_dbg[2]; /* frames the hint line was drawn, the halves still up (bit 0 the running jump, bit 1 the flag) */
static int g_rim_dbg; /* materials drawn with a rim light (the heroes' own, cw_rim_light_count) */
static struct { float last[3], step[3], ppos[MAX_PLAYERS * 2]; int still, frames; } CS;
static void camgate_reset(void) {
  g_viewbot_dbg = 1e9f;
  g_underview_dbg[0] = g_underview_dbg[1] = 0;
  g_tags_dbg[0] = g_tags_dbg[1] = 0;
  memset(g_sign_dbg, 0, sizeof g_sign_dbg);
  g_marker_over_dbg = 0;
  memset(&CS, 0, sizeof CS);
  memset(g_camgate_dbg, 0, sizeof g_camgate_dbg);
  memset(g_camjit_dbg, 0, sizeof g_camjit_dbg);
}
static void camgate_frame(const float view[3], const float *pp, int np, bool snapped) {
  bool still = CS.frames > 0;
  for (int i = 0; i < np * 2; i++) {
    if (fabsf(pp[i] - CS.ppos[i]) > 1e-5f) still = false;
    CS.ppos[i] = pp[i];
  }
  CS.still = still ? CS.still + 1 : 0;
  float st[3], d2 = 0, j2 = 0;
  for (int k = 0; k < 3; k++) {
    st[k] = view[k] - CS.last[k];
    d2 += st[k] * st[k];
    j2 += (st[k] - CS.step[k]) * (st[k] - CS.step[k]);
  }
  float d = sqrtf(d2), jerk = sqrtf(j2);
  if (CS.frames > 1 && !snapped) {
    if (CS.still >= 150) {
      g_camgate_dbg[0]++;
      if (d > 0.001f) g_camgate_dbg[1]++;
      g_camjit_dbg[0] = fmaxf(g_camjit_dbg[0], d);
    } else {
      g_camgate_dbg[2]++;
      if ((d > 0.6f || jerk > 0.08f) && !g_camgate_dbg[3]++) g_camjit_dbg[3] = (float)CS.frames; /* the first one */
      g_camjit_dbg[1] = fmaxf(g_camjit_dbg[1], d);
      g_camjit_dbg[2] = fmaxf(g_camjit_dbg[2], jerk);
    }
  }
  for (int k = 0; k < 3; k++) CS.step[k] = snapped ? 0 : st[k], CS.last[k] = view[k];
  CS.frames++;
}
static int g_run_ticks;

/* ── generated audio ───────────────────────────────────────────────── */
#define SR 22050
static float *synth(float secs, float (*fn)(float t, float dur, void *u), void *u, int *frames) {
  int n = (int)(secs * SR);
  float *buf = malloc(sizeof(float) * (size_t)n);
  for (int i = 0; i < n; i++) buf[i] = fn((float)i / SR, secs, u);
  *frames = n;
  return buf;
}
typedef struct sp { float f0, f1, noise, decay, vol; int wave; } sp;
static float sfx_fn(float t, float dur, void *u) {
  const sp *s = u;
  float k = t / dur;
  float f = s->f0 + (s->f1 - s->f0) * k;
  static float ph, rs = 1;
  if (t < 1e-4f) ph = 0, rs = 1;
  ph += f / SR;
  ph -= floorf(ph);
  float w = s->wave == 0 ? sinf(ph * 6.2831853f) : s->wave == 1 ? (ph < 0.5f ? 1.0f : -1.0f) : 4.0f * fabsf(ph - 0.5f) - 1.0f;
  rs = rs * 1664525.0f + 1013904223.0f;
  rs = fmodf(rs, 65536.0f);
  float nz = (rs / 32768.0f - 1.0f);
  float env = expf(-s->decay * k) * (k < 0.02f ? k / 0.02f : 1.0f);
  return (w * (1 - s->noise) + nz * s->noise) * env * s->vol;
}
static void audio_init(void) {
  A.on = getenv("COOP_NO_AUDIO") == NULL && cw_audio_sample_rate() > 0;
  if (!A.on) return;
  A.music_bus = cw_shell_music_bus();
  A.sfx_bus = cw_shell_sfx_bus();
  static const sp P[SFX_COUNT] = {
    { 380, 760, 0.0f, 5, 0.35f, 2 }, { 140, 60, 0.4f, 8, 0.4f, 0 }, { 880, 1320, 0.0f, 6, 0.3f, 1 }, { 300, 90, 0.3f, 4, 0.5f, 1 },
    { 200, 500, 0.7f, 5, 0.35f, 0 }, { 300, 900, 0.5f, 4, 0.3f, 2 }, { 440, 1760, 0.0f, 2, 0.35f, 0 }, { 500, 200, 0.2f, 6, 0.45f, 1 },
    { 120, 40, 0.8f, 3, 0.55f, 0 }, { 600, 900, 0.0f, 3, 0.3f, 0 }, { 900, 300, 0.1f, 6, 0.35f, 0 }, { 400, 60, 0.3f, 3, 0.5f, 2 },
    { 523, 1046, 0.0f, 1.2f, 0.4f, 1 }, { 660, 990, 0.0f, 4, 0.3f, 0 }, { 300, 600, 0.3f, 6, 0.3f, 2 },
  };
  static const float DUR[SFX_COUNT] = { 0.18f, 0.12f, 0.16f, 0.3f, 0.35f, 0.18f, 0.7f, 0.15f, 0.5f, 0.3f, 0.25f, 0.5f, 1.4f, 0.4f, 0.15f };
  for (int i = 0; i < SFX_COUNT; i++) {
    int n;
    float *pcm = synth(DUR[i], sfx_fn, (void *)&P[i], &n);
    A.sfx[i] = cw_sound_from_pcm(pcm, n, 1, SR, &(cw_sound_desc){ .bus = A.sfx_bus, .max_voices = 4 });
    free(pcm);
  }
}
static void sfx(int id, float vol) {
  if (A.on) cw_audio_play(A.sfx[id], &(cw_play_desc){ .volume = vol });
}

/* ── lights, environment, post ────────────────────────────────────── */
/* a region's whole look: the key light (colour, strength, direction), a rim light from behind, the hemisphere and the
 * fog (analytic and a volumetric height fog that the key light shines through), exposure and bloom */
typedef struct mood {
  float sky[3], fog[3], sun[3], sun_e, amb, hemi, exposure, bloom, fog_near, fog_far, torch;
  float sun_pitch, sun_yaw, rim[3], rim_e, vden, vamb, vg, vcol[3], grain, ca;
} mood;
static const mood MOODS[BIOMES] = {
  /* meadow: a warm afternoon, blue shade, soft haze, shafts under the trees */
  { { 0.32f, 0.58f, 0.95f }, { 0.62f, 0.76f, 0.95f }, { 1.0f, 0.9f, 0.72f }, 4.4f, 0.5f, 1.1f, 1.0f, 0.1f, 80, 520, 0.0f,
    -0.7f, 0.55f, { 0.55f, 0.75f, 1.0f }, 0.9f, 0.0006f, 0.9f, 0.5f, { 0.85f, 0.92f, 1.0f }, 0.012f, 0.0f },
  /* caves: cold dark, the torches and crystals are the light, a cyan rim */
  { { 0.04f, 0.05f, 0.1f }, { 0.04f, 0.06f, 0.13f }, { 0.4f, 0.55f, 1.0f }, 0.4f, 0.14f, 0.35f, 1.7f, 0.2f, 24, 280, 1.0f,
    -0.9f, 0.2f, { 0.25f, 0.65f, 1.0f }, 1.6f, 0.003f, 0.5f, 0.3f, { 0.3f, 0.5f, 0.9f }, 0.02f, 0.0f },
  /* ruins: green daylight through broken stone, hard shafts */
  { { 0.22f, 0.42f, 0.46f }, { 0.4f, 0.62f, 0.6f }, { 1.0f, 0.95f, 0.78f }, 3.6f, 0.4f, 0.9f, 1.05f, 0.12f, 45, 360, 0.5f,
    -1.0f, -0.45f, { 0.7f, 1.0f, 0.85f }, 1.0f, 0.0012f, 0.7f, 0.4f, { 0.8f, 1.0f, 0.9f }, 0.014f, 0.0f },
  /* frost: a flat white winter noon, cold blue shade, clean air, a thin hard light */
  { { 0.42f, 0.56f, 0.78f }, { 0.74f, 0.84f, 0.95f }, { 0.95f, 0.97f, 1.0f }, 3.4f, 0.55f, 1.0f, 1.0f, 0.08f, 90, 480, 0.1f,
    -0.85f, 0.45f, { 0.75f, 0.88f, 1.0f }, 1.0f, 0.0008f, 0.8f, 0.45f, { 0.85f, 0.93f, 1.0f }, 0.008f, 0.0f },
  /* volcano: a low red sun behind the cone, lava orange rim, ash haze */
  { { 0.25f, 0.06f, 0.04f }, { 0.4f, 0.12f, 0.06f }, { 1.0f, 0.5f, 0.3f }, 1.8f, 0.42f, 0.95f, 1.25f, 0.24f, 34, 300, 0.8f,
    -0.35f, 0.9f, { 1.0f, 0.38f, 0.1f }, 2.4f, 0.0015f, 0.4f, 0.3f, { 1.0f, 0.5f, 0.3f }, 0.016f, 0.0015f },
};
static mood cur_mood;

static cw_value val_vec4(float x, float y, float z, float w) { cw_value v = { CW_T_VEC4 }; v.v[0] = x; v.v[1] = y; v.v[2] = z; v.v[3] = w; return v; }
static cw_node make_node(const char *type, cw_node parent, const char *name) {
  cw_node n = cw_node_new(type, name);
  cw_node_add_child(parent, n);
  return n;
}
static void particles_cfg(cw_node n, int amount, float life, float smin, float smax, float size, cw_color col, float grav, int blend) {
  cw_node_set(n, "amount", cw_val_int(amount));
  cw_node_set(n, "lifetime", cw_val_float(life));
  cw_node_set(n, "one_shot", cw_val_bool(true));
  cw_node_set(n, "explosiveness", cw_val_float(1.0));
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "speed_min", cw_val_float(smin));
  cw_node_set(n, "speed_max", cw_val_float(smax));
  cw_node_set(n, "spread", cw_val_float(180));
  cw_node_set(n, "size", cw_val_float(size));
  cw_node_set(n, "color", cw_val_color(col.r, col.g, col.b, col.a));
  cw_node_set(n, "gravity", cw_val_vec3(0, grav, 0));
  cw_node_set(n, "blend", cw_val_enum(blend ? "ADD" : "ALPHA"));
}
/* a particle system from one call: blend 0 ALPHA, 1 ADD, 2 OPAQUE (instanced meshes), 3 DISTORT */
static void pfx(cw_node n, int amount, float life, float smin, float smax, float size, cw_color col, float grav, int blend, const char *ramp,
                const char *curve, float energy) {
  particles_cfg(n, amount, life, smin, smax, size, col, grav, blend == 1);
  static const char *const B[4] = { "ALPHA", "ADD", "OPAQUE", "DISTORT" };
  cw_node_set(n, "blend", cw_val_enum(B[blend]));
  cw_node_set(n, "color_ramp", cw_val_string(ramp));
  cw_node_set(n, "size_curve", cw_val_string(curve));
  cw_node_set(n, "energy", cw_val_float(energy));
  cw_node_set(n, "size_random", cw_val_float(0.3f));
}
static void burst(cw_node n, float x, float y, float z) {
  if (!cw_node_valid(n)) return;
  cw_node3d_set_position(n, (t3_vec3){ x, y, z });
  cw_particles_restart(n);
}

static t3_mesh *part(cw_node parent, t3_geometry *g, t3_material *m, float x, float y, float z, float sx, float sy, float sz) {
  cw_node n = cw_node_new("MeshInstance3D", "m");
  cw_node_add_child(parent, n);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.position = (t3_vec3){ x, y, z };
  me->base.scale = (t3_vec3){ sx, sy, sz };
  me->base.cast_shadow = true;
  me->base.receive_shadow = true;
  cw_mesh3d_set_procedural(n, me);
  t3_release(me);
  return me;
}
static t3_material *phys(uint32_t rgb, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(rgb);
  m->roughness = rough;
  m->metalness = metal;
  return m;
}

/* ── decals: one generated sheet (tools/gen-assets.mjs), 4 x 2 cells of 256 px, all MULTIPLY so the light stays right ── */
enum { DC_DIRT, DC_SCORCH, DC_MOSS, DC_CRACK, DC_DAMP, DC_LEAVES, DC_SOOT, DC_CRAZE };
static int g_ndecals;
static float dhash(int x, int y, int s) {
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)s * 83492791u;
  h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
  return (float)(h & 0xffff) / 65535.0f;
}
/* a decal on the face that looks at the camera (down = false: projected along -Z onto it) or on a top (projected down) */
static void decal(float x, float y, float w, float h, int cell, float strength, bool down, float spin) {
  if (g_ndecals >= 220) return;
  g_ndecals++;
  cw_node d = make_node("Decal3D", S.world, "Decal");
  cw_node_set(d, "texture", cw_val_string("decals.png"));
  cw_node_set(d, "region", val_vec4((float)(cell % 4) * 256, (float)(cell / 4) * 256, 256, 256));
  cw_node_set(d, "blend", cw_val_enum("MULTIPLY"));
  cw_node_set(d, "color", cw_val_color(1, 1, 1, strength));
  cw_node_set(d, "edge_fade", cw_val_float(0.3f));
  cw_node_set(d, "normal_fade", cw_val_float(0.5f));
  if (down) {
    cw_node_set(d, "size", cw_val_vec3(w, h, 0.5f));
    cw_node3d_set_position(d, (t3_vec3){ x, y + 0.12f, 0 });
    cw_node3d_set_rotation(d, (t3_vec3){ -1.5708f, spin, 0 });
  } else {
    cw_node_set(d, "size", cw_val_vec3(w, h, 0.7f));
    cw_node3d_set_position(d, (t3_vec3){ x, y, 1.75f });
    cw_node3d_set_rotation(d, (t3_vec3){ 0, 0, spin });
  }
}
/* dirt where the turf meets the cliff, damp runs under the ruin ledges, moss, cracks on big walls, soot over the lava,
 * scorched and crazed ground beside it, leaf litter on the meadow paths */
static void place_decals(const level *L) {
  g_ndecals = 0;
  for (int x = 1; x < WORLD_W - 1; x++) {
    int bi = L->biome[x];
    for (int y = 1; y < WORLD_H - 1; y++) {
      int t = L->t[y][x];
      if (!(t == T_ROCK || t == T_GRASS || t == T_BRICK)) continue;
      bool top = !world_solid(L, x, y + 1);
      float r = dhash(x, y, 900), fx = (float)x + 0.5f, fy = (float)y + 0.5f, spin = (dhash(x, y, 901) - 0.5f) * 0.6f;
      bool lava_near = false;
      for (int k = -4; k <= 4; k++)
        if (x + k >= 0 && x + k < WORLD_W && L->t[2][x + k] == T_LAVA) lava_near = true;
      if (top) {
        if (bi == BIOME_MEADOW && r < 0.16f) decal(fx, fy - 0.15f, 2.2f, 1.1f, DC_DIRT, 0.75f, false, spin);
        else if (bi == BIOME_RUINS && r < 0.22f) decal(fx, fy - 0.9f, 1.4f, 2.0f, DC_DAMP, 0.7f, false, 0);
        else if (bi == BIOME_CAVES && r < 0.14f) decal(fx, fy - 0.9f, 1.3f, 2.0f, DC_DAMP, 0.6f, false, 0);
        if (bi == BIOME_MEADOW && r > 0.9f) decal(fx, (float)y + 1.0f, 1.6f, 1.6f, DC_LEAVES, 0.8f, true, spin * 4);
        if (bi == BIOME_VOLCANO && lava_near && r > 0.55f) decal(fx, (float)y + 1.0f, 2.0f, 2.0f, DC_SCORCH, 0.85f, true, spin * 4);
        else if (bi == BIOME_VOLCANO && r > 0.8f) decal(fx, (float)y + 1.0f, 2.2f, 2.2f, DC_CRAZE, 0.7f, true, spin * 4);
        continue;
      }
      if (y < L->ground[x] - 7 && !(bi == BIOME_VOLCANO && lava_near)) continue; /* deep rock under the surface is seldom on screen */
      if (bi == BIOME_VOLCANO && lava_near && y < 9 && r < 0.12f) decal(fx, fy + 0.6f, 1.6f, 2.6f, DC_SOOT, 0.75f, false, 0);
      else if ((bi == BIOME_RUINS || bi == BIOME_MEADOW) && r < 0.035f) decal(fx, fy, 1.8f, 1.8f, DC_MOSS, 0.6f, false, spin);
      else if (r > 0.96f) decal(fx, fy, 1.8f, 1.8f, DC_CRACK, 0.7f, false, spin * 3);
    }
  }
}

#define HAT_Y 1.32f
static bool hats_due;
static void hats_apply(void);
static void gore_build(void);
static void graves_reset(void);
static bool project(float x, float y, float z, float W, float H, float *sx, float *sy);
static void gore_step(float dt);
static void gore_death(int who, int cause, float x, float y);
static void build_scene(void) {
  game *g = &G;
  cw_tree *tree = cw_scene_app_tree();
  S.root = cw_tree_root(tree);
  /* free whatever a previous match built */
  if (cw_node_valid(S.world)) cw_node_free(S.world);
  S.world = make_node("Node3D", S.root, "World");
  S.nlight_src = 0;
  world_scene_build(g, S.world);
  world_backdrop(g);
  {
    float worst_mm;
    g_scenery_dbg[1] = world_scenery_check(&g->L, &g_scenery_dbg[0], &worst_mm);
    if (getenv("COOP_SCENERY")) printf("scenery: %d props seated, %d off the ground, worst %.1f mm\n", g_scenery_dbg[0], g_scenery_dbg[1], (double)worst_mm);
  }
  world_things_build(g);
  world_near_build(g);
  S.graves_due = true;
  g_things_dbg[1] = world_things_check(&g->L, &g_things_dbg[0]);
  place_decals(&g->L);
  gore_build();
  graves_reset();
  hats_due = true;
  cw_node wobj = S.world;
  const level *L = &g->L;
  /* environment, sun, post, camera: once */
  if (!S.built) {
    S.env = make_node("WorldEnvironment", S.root, "Env");
    S.sun = make_node("DirectionalLight3D", S.root, "Sun");
    cw_node_set(S.sun, "shadow", cw_val_bool(true));
    cw_node_set(S.sun, "shadow_size", cw_val_int(2048));
    cw_node_set(S.sun, "shadow_extent", cw_val_float(26));
    cw_node_set(S.sun, "shadow_far", cw_val_float(90));
    cw_node_set(S.sun, "shadow_bias", cw_val_float(-0.0006));
    cw_node_set(S.sun, "fog_energy", cw_val_float(2.0));
    cw_node3d_set_rotation(S.sun, (t3_vec3){ -0.9f, 0.35f, 0 });
    /* the rim light: from behind and the side, no shadow, so a character's edge catches colour */
    S.rim = make_node("DirectionalLight3D", S.root, "Rim");
    cw_node_set(S.rim, "energy", cw_val_float(1.0));
    cw_node_set(S.rim, "fog_energy", cw_val_float(0.0));
    cw_node3d_set_rotation(S.rim, (t3_vec3){ -0.35f, 3.14159f + 0.6f, 0 });
    /* a height fog the lights shine through (shafts), a few fog volumes for water, lava and mist */
    if (!getenv("COOP_NOFOG")) {
      S.fog = make_node("HeightFog", S.root, "Fog");
      cw_node_set(S.fog, "use_sky_color", cw_val_bool(false));
      cw_node_set(S.fog, "volumetric", cw_val_bool(true));
      cw_node_set(S.fog, "volumetric_distance", cw_val_float(60));
      cw_node_set(S.fog, "height_falloff", cw_val_float(0.02));
      for (int i = 0; i < 8; i++) {
        S.fvol[i] = make_node("FogVolume3D", S.root, "Volume");
        cw_node_set(S.fvol[i], "density", cw_val_float(0));
        cw_node_set(S.fvol[i], "noise", cw_val_float(0.6));
        cw_node_set(S.fvol[i], "noise_scale", cw_val_float(0.35));
        cw_node_set(S.fvol[i], "edge_fade", cw_val_float(0.35));
      }
    }
    S.post = make_node("PostProcess", S.root, "Post");
    cw_node_set(S.post, "tonemap", cw_val_enum("ACES_FILMIC"));
    cw_node_set(S.post, "bloom", cw_val_bool(true));
    cw_node_set(S.post, "bloom_intensity", cw_val_float(0.08));
    cw_node_set(S.post, "bloom_threshold", cw_val_float(0.9));
    cw_node_set(S.post, "ssao", cw_val_bool(true));
    cw_node_set(S.post, "ssao_radius", cw_val_float(0.8));
    cw_node_set(S.post, "vignette", cw_val_bool(true));
    cw_node_set(S.post, "vignette_intensity", cw_val_float(0.4));
    cw_node_set(S.post, "msaa", cw_val_int(4));
    cw_node_set(S.post, "grain", cw_val_bool(true));
    cw_node_set(S.post, "grain_intensity", cw_val_float(0.012));
    cw_node_set(S.post, "chromatic_aberration", cw_val_bool(true));
    cw_node_set(S.post, "ca_intensity", cw_val_float(0.0015));
    cw_node_set(S.post, "bloom_radius", cw_val_float(0.8));
    S.cam = make_node("Camera3D", S.root, "Camera");
    cw_node_set(S.cam, "fov", cw_val_float(40));
    cw_node_set(S.cam, "near", cw_val_float(0.5));
    cw_node_set(S.cam, "far", cw_val_float(400));
    cw_node_set(S.cam, "current", cw_val_bool(true));
    for (int i = 0; i < NTORCH; i++) {
      S.torch[i] = make_node("OmniLight3D", S.root, "Torch");
      cw_node_set(S.torch[i], "range", cw_val_float(14));
      cw_node_set(S.torch[i], "decay", cw_val_float(2));
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
      S.plight[i] = make_node("OmniLight3D", S.root, "PlayerLight");
      cw_node_set(S.plight[i], "range", cw_val_float(8));
      cw_node_set(S.plight[i], "energy", cw_val_float(0));
      cw_node_set(S.plight[i], "color", cw_val_color(PLAYER_RGB[i][0], PLAYER_RGB[i][1], PLAYER_RGB[i][2], 1));
      cw_node_set(S.plight[i], "fog_energy", cw_val_float(0));
    }
    S.built = true;
  }
  /* light sources: the braziers in the caves and ruins (world.c seats them on the floor), fire over the lava. Each
   * brazier's flame is its own emitter sitting in its basket; only lights are shared between nearby sources. */
  {
    float tor[MAX_TORCHES][4];
    S.nflame = world_torches(tor, MAX_TORCHES);
    for (int i = 0; i < S.nflame && S.nlight_src < 60; i++) {
      int k = S.nlight_src++, b = (int)tor[i][3];
      S.light_x[k] = tor[i][0], S.light_y[k] = tor[i][1] + 0.35f, S.light_z[k] = tor[i][2] + 0.6f; /* just above the coals */
      S.light_r[k] = 1.0f, S.light_g[k] = b == BIOME_CAVES ? 0.62f : 0.8f, S.light_b[k] = b == BIOME_CAVES ? 0.3f : 0.55f;
      S.light_e[k] = 14.0f, S.light_range[k] = 14;
      memcpy(S.flame_at[i], tor[i], sizeof tor[i]);
      cw_node f = make_node("GPUParticles3D", wobj, "Flame");
      pfx(f, 80, 0.5f, 0.5f, 1.1f, 0.2f, cw_rgba(1, 0.5f, 0.12f, 1), 2.4f, 1, "0 1 0.62 0.18 0, 0.08 1 0.5 0.1 1, 0.55 0.9 0.22 0.03 0.8, 1 0.3 0.04 0.01 0", "0 0.9, 0.25 1, 1 0.1", 3.0f);
      cw_node_set(f, "one_shot", cw_val_bool(false));
      cw_node_set(f, "explosiveness", cw_val_float(0));
      cw_node_set(f, "emitting", cw_val_bool(false));
      cw_node_set(f, "emission_shape", cw_val_enum("SPHERE"));
      cw_node_set(f, "emission_radius", cw_val_float(b == BIOME_RUINS ? 0.2f : 0.14f));
      cw_node_set(f, "spread", cw_val_float(12));
      cw_node_set(f, "direction", cw_val_vec3(0, 1, 0));
      cw_node_set(f, "local_coords", cw_val_bool(false));
      cw_node3d_set_position(f, (t3_vec3){ tor[i][0], tor[i][1], tor[i][2] });
      if (getenv("COOP_FLOAT_FIRE")) cw_node3d_set_position(f, (t3_vec3){ tor[i][0], tor[i][1] + 0.6f, 1.5f }); /* the fire gate's control (native): the old flame, in front of the playfield */
      cw_node_set(f, "preprocess", cw_val_float(0.6f)); /* lit already when it comes into view */
      S.flame[i] = f;
      S.flame_on[i] = false;
    }
  }
  for (int x = 0; x < WORLD_W; x += 6) {
    if (L->t[2][x] == T_LAVA && S.nlight_src < 62) {
      int k = S.nlight_src++;
      S.light_x[k] = (float)x + 0.5f, S.light_y[k] = 3.6f, S.light_z[k] = 1.5f;
      S.light_r[k] = 1.0f, S.light_g[k] = 0.32f, S.light_b[k] = 0.06f;
      S.light_e[k] = 20.0f, S.light_range[k] = 16;
    }
  }
  /* pickups: glowing crystals */
  for (int i = 0; i < L->npickups; i++) {
    const pickup *pk = &L->pickups[i];
    S.pick_node[i] = make_node("Node3D", S.world, "Pickup");
    cw_node po = S.pick_node[i];
    static const uint32_t PC[4] = { 0x55ddff, 0xaa66ff, 0x66ff99, 0xffaa33 };
    t3_material *m = phys(PC[pk->ability & 3], 0.15f, 0.1f);
    m->emissive = (t3_color){ 0.4f, 0.7f, 1.0f }, m->emissive_intensity = 2.5f;
    t3_geometry *cg = t3_cone_geometry_new(0.35f, 0.7f, 6, 1, false);
    part(po, cg, m, 0, 0.35f, 0, 1, 1, 1);
    part(po, cg, m, 0, -0.35f, 0, 1, -1, 1);
    t3_release(cg);
    if (S.nlight_src < 64) {
      int k = S.nlight_src++;
      S.light_x[k] = (float)pk->x + 0.5f, S.light_y[k] = (float)pk->y + 1.2f, S.light_z[k] = 1.5f;
      S.light_r[k] = 0.4f, S.light_g[k] = 0.7f, S.light_b[k] = 1.0f, S.light_e[k] = 10.0f, S.light_range[k] = 9;
    }
  }
  /* coins */
  /* a struck coin: a raised rim round a dished face, turned on a lathe about its axis (mesh Y), so the rim still reads as
   * a coin when the spin brings it edge on. The disc stands up: its axis lies along the view until the spin turns it */
  static const t3_vec2 COIN_PROFILE[] = { { 0, 0.038f }, { 0.17f, 0.038f }, { 0.20f, 0.062f }, { 0.27f, 0.062f }, { 0.30f, 0.03f },
                                          { 0.30f, -0.03f }, { 0.27f, -0.062f }, { 0.20f, -0.062f }, { 0.17f, -0.038f }, { 0, -0.038f } };
  t3_geometry *cg = t3_lathe_geometry_new(COIN_PROFILE, 10, 20, 0, 6.2831853f);
  t3_material *cm = phys(0xffc83a, 0.25f, 1.0f);
  cm->emissive = (t3_color){ 0.5f, 0.3f, 0.0f }, cm->emissive_intensity = 0.8f;
  t3_material *cmb = phys(0xffe07a, 0.18f, 1.0f); /* the big coins: brighter, and they glow enough to bloom */
  cmb->emissive = (t3_color){ 0.9f, 0.6f, 0.15f }, cmb->emissive_intensity = 1.6f;
  for (int i = 0; i < L->ncoins; i++) {
    S.coin_node[i] = make_node("Node3D", S.world, "Coin");
    S.coin_mesh[i] = part(S.coin_node[i], cg, L->coins[i].medal ? cmb : cm, 0, 0, 0, 1, 1, 1);
    if (L->coins[i].medal) cw_node3d_set_scale(S.coin_node[i], (t3_vec3){ 1.9f, 1.9f, 1.9f });
    /* through t3_object_set_rotation: writing base.rotation alone never reached the mesh's quaternion, so the coins lay
     * flat like plates and turned about their own axis, edge on to the camera for ever */
    if (!g_coin_flat_bug) t3_object_set_rotation(S.coin_mesh[i], 1.5707963f, 0, 0);
    cw_node3d_set_position(S.coin_node[i], (t3_vec3){ L->coins[i].x, L->coins[i].y, 0 });
  }
  t3_release(cg);
  /* enemies: skinned models, posed in sync_scene */
  for (int i = 0; i < L->nenemies; i++) {
    const enemy *e = &L->enemies[i];
    S.enode[i] = make_node("Node3D", S.world, "Enemy");
    S.emodel[i] = make_node("Node3D", S.enode[i], "Model");
    rig_free(S.erig[i]);
    S.erig[i] = rig_load(e->kind == 2 ? RIG_BOSS : e->kind == 1 ? RIG_FLYER : RIG_WALKER);
    if (S.erig[i]) rig_attach(S.erig[i], S.emodel[i]);
    if (S.erig[i]) rig_cast_shadow(S.erig[i], g_shadow_old); /* a contact blob grounds it (sync_scene) */
    float sc = e->kind == 2 ? boss_scale(L->boss_kind) : e->kind == 1 ? 0.85f : 0.95f;
    if (e->kind == 2 && S.erig[i]) {
      /* one model, a different creature each time: a colour and something on its head */
      static const float TINT[5][3] = { { 0.75f, 0.55f, 0.95f }, { 1.3f, 1.05f, 0.45f }, { 1.3f, 0.5f, 0.4f }, { 0.6f, 0.75f, 1.0f }, { 0.35f, 0.3f, 0.35f } };
      rig_shade(S.erig[i], TINT[L->boss_kind]);
      static const uint32_t HAT[5] = { 0x201018, 0xe8c040, 0xf2f2f2, 0x303848, 0xe8c040 };
      t3_geometry *brim = t3_cylinder_geometry_new(0.62f, 0.62f, 0.07f, 16, 1, false), *crown = t3_cylinder_geometry_new(0.36f, 0.4f, 0.62f, 16, 1, false);
      t3_material *hm = phys(HAT[L->boss_kind], 0.7f, L->boss_kind == 1 || L->boss_kind == 4 ? 0.9f : 0.0f);
      part(S.emodel[i], brim, hm, 0, 1.66f, 0, 1, 1, 1)->base.cast_shadow = g_shadow_old;
      part(S.emodel[i], crown, hm, 0, 2.0f, 0, 1, 1, 1)->base.cast_shadow = g_shadow_old;
      t3_release(brim), t3_release(crown);
    }
    S.eextra[i][0] = S.eextra[i][1] = NULL;
    if (e->kind >= EK_HOPPER && S.erig[i]) {
      /* each world's own creature: the walker's body in its own colours, with the one thing that says what it does */
      static const float KT[EK_KINDS][3] = { { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 0.75f, 1.25f, 0.45f }, { 0.62f, 0.42f, 0.95f }, { 0.45f, 1.0f, 0.9f }, { 1.2f, 0.95f, 0.4f } };
      rig_shade(S.erig[i], KT[e->kind]);
      if (e->kind == EK_SHELL) { /* a domed shell on its back; alone, it is what is left after a stomp */
        static const t3_vec2 DOME[] = { { 0, 0.62f }, { 0.22f, 0.6f }, { 0.4f, 0.5f }, { 0.52f, 0.32f }, { 0.58f, 0.12f }, { 0.6f, 0.0f }, { 0.0f, 0.0f } };
        t3_geometry *dg = t3_lathe_geometry_new(DOME, 7, 18, 0, 6.2831853f);
        t3_material *dm = phys(0x2f8a6e, 0.35f, 0.0f);
        S.eextra[i][0] = part(S.enode[i], dg, dm, 0, 0.12f, 0, 1, 1, 1);
        t3_geometry *rg = t3_torus_geometry_new(0.6f, 0.06f, 6, 18, 6.2831853f);
        S.eextra[i][1] = part(S.enode[i], rg, phys(0xe8d8a0, 0.6f, 0.0f), 0, 0.12f, 0, 1, 1, 1); /* its rim */
        t3_object_set_rotation(S.eextra[i][1], 1.5707963f, 0, 0);
        t3_release(dg), t3_release(rg);
      } else if (e->kind == EK_SLIDER) { /* a crown of spikes on its front: from behind or nothing */
        t3_geometry *cg2 = t3_cone_geometry_new(0.09f, 0.42f, 6, 1, false);
        t3_material *sm = phys(0xd8dce4, 0.3f, 0.9f);
        for (int k = 0; k < 2; k++) {
          S.eextra[i][k] = part(S.emodel[i], cg2, sm, 0, 0.45f + 0.28f * (float)k, 0.48f, 1, 1, 1);
          t3_object_set_rotation(S.eextra[i][k], 1.5707963f, 0, 0); /* points along the model's facing */
        }
        t3_release(cg2);
      } else if (e->kind == EK_DROPPER) { /* the thread it hangs from and climbs back up */
        t3_geometry *tg = t3_cylinder_geometry_new(0.015f, 0.015f, 1.0f, 5, 1, false);
        t3_material *tm = phys(0xd0d4e0, 0.4f, 0.0f);
        tm->emissive = (t3_color){ 0.3f, 0.32f, 0.4f }, tm->emissive_intensity = 0.6f;
        S.eextra[i][0] = part(S.world, tg, tm, e->x, e->hang_y, 0, 1, 1, 1);
        t3_release(tg);
      }
      for (int k = 0; k < 2; k++) if (S.eextra[i][k]) S.eextra[i][k]->base.cast_shadow = g_shadow_old;
    }
    cw_node3d_set_scale(S.emodel[i], (t3_vec3){ sc, sc, sc });
    S.ephase[i] = (float)i * 1.7f;
  }
  /* moving platforms */
  for (int i = 0; i < L->nplat; i++) {
    int px = (int)L->plats[i].x0 < 0 ? 0 : (int)L->plats[i].x0 >= WORLD_W ? WORLD_W - 1 : (int)L->plats[i].x0;
    S.nplat_mesh[i] = world_platform(L->biome[px], L->plats[i].w, i * 13 + 3, S.plat_mesh[i]);
  }
  /* characters: skinned heroes, one headgear each (a marker that is not a colour) */
  t3_geometry *bub = t3_sphere_geometry_new(1.0f, 32, 20);
  static const float ACCENT[MAX_PLAYERS][3] = { { 1.0f, 0.85f, 0.25f }, { 0.95f, 0.95f, 0.95f }, { 1.0f, 0.45f, 0.65f }, { 0.3f, 0.8f, 0.95f } };
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (cw_node_valid(S.pnode[i])) cw_node_free(S.pnode[i]);
    S.pnode[i] = make_node("Node3D", S.root, "Player");
    cw_node po = S.pnode[i];
    S.pmodel[i] = make_node("Node3D", po, "Model");
    rig_free(S.prig[i]);
    S.prig[i] = rig_load(RIG_HERO);
    if (S.prig[i]) {
      rig_attach(S.prig[i], S.pmodel[i]);
      float fur[3] = { PLAYER_RGB[i][0], PLAYER_RGB[i][1], PLAYER_RGB[i][2] };
      rig_tint(S.prig[i], fur, ACCENT[i], i);
      rig_cast_shadow(S.prig[i], g_shadow_old);
      /* a rim of the player's own colour, lightened, round the silhouette: the four read against the navy caves and the red
       * crater (its strength follows the region, sync_scene); every preset draws it */
      if (!g_rim_off) {
        cw_rim_light_desc rd = cw_rim_light_default();
        rd.color = (t3_color){ 0.45f + 0.55f * fur[0], 0.45f + 0.55f * fur[1], 0.45f + 0.55f * fur[2] };
        rd.strength = 0.6f, rd.power = 2.6f, rd.near = 0, rd.far = 1, rd.close = 1;
        rig_rim(S.prig[i], &rd);
      }
    }
    /* the hats. Terrible ones, and all of them sit on the head */
    {
      static const uint32_t HC[6] = { 0xff6a1a, 0x9098a0, 0xb0b8c0, 0x201a18, 0x484c52, 0x201018 };
      t3_geometry *cone = t3_cone_geometry_new(0.2f, 0.42f, 12, 1, false), *buck = t3_cylinder_geometry_new(0.21f, 0.16f, 0.24f, 12, 1, false), *small = t3_cylinder_geometry_new(0.12f, 0.1f, 0.13f, 12, 1, false);
      t3_geometry *top = t3_cylinder_geometry_new(0.14f, 0.15f, 0.32f, 12, 1, false), *brim = t3_cylinder_geometry_new(0.25f, 0.25f, 0.03f, 14, 1, false), *pan = t3_cylinder_geometry_new(0.22f, 0.2f, 0.14f, 12, 1, false), *handle = t3_box_geometry_new(0.34f, 0.03f, 0.05f, 1, 1, 1);
      for (int h = 0; h < 6; h++) {
        t3_material *hm = phys(HC[h], h == 1 || h == 2 || h == 4 ? 0.45f : 0.85f, h == 1 || h == 2 || h == 4 ? 0.8f : 0);
        t3_geometry *g0 = h == 0 ? cone : h == 1 ? buck : h == 2 ? small : h == 4 ? pan : top;
        float y = HAT_Y + (h == 0 ? 0.2f : h == 3 || h == 5 ? 0.17f : 0.08f);
        S.hat[i][h][0] = part(S.pmodel[i], g0, hm, 0, y, 0, h == 5 ? 1.3f : 1, h == 5 ? 1.3f : 1, h == 5 ? 1.3f : 1);
        S.hat[i][h][0]->base.visible = false;
        S.hat[i][h][0]->base.cast_shadow = g_shadow_old;
        S.hat[i][h][1] = NULL;
        if (h == 3 || h == 5) { S.hat[i][h][1] = part(S.pmodel[i], brim, hm, 0, HAT_Y + 0.02f, 0, h == 5 ? 1.3f : 1, 1, h == 5 ? 1.3f : 1); S.hat[i][h][1]->base.visible = false; S.hat[i][h][1]->base.cast_shadow = g_shadow_old; }
        if (h == 4) { S.hat[i][h][1] = part(S.pmodel[i], handle, hm, 0.34f, HAT_Y + 0.08f, 0, 1, 1, 1); S.hat[i][h][1]->base.visible = false; S.hat[i][h][1]->base.cast_shadow = g_shadow_old; }
      }
      t3_release(cone), t3_release(buck), t3_release(small), t3_release(top), t3_release(brim), t3_release(pan), t3_release(handle);
    }
    t3_material *bu = phys(0xaaddff, 0.05f, 0);
    bu->transparent = true, bu->opacity = 0.3f, bu->depth_write = false;
    S.bubble[i] = part(po, bub, bu, 0, 0.9f, 0, 1, 1, 1);
    S.bubble[i]->base.visible = false;
    S.bubble[i]->base.cast_shadow = false;
    cw_node3d_set_position(S.pnode[i], (t3_vec3){ 0, -100, 0 });
  }
  t3_release(bub);
  if (!S.blobs) S.blobs = cw_splats_new(S.root, "Blobs", CW_SPLATS_SHADOW, MAX_PLAYERS + MAX_ENEMIES);
  S.rim_biome = -1;
  /* water: a Water3D surface (Gerstner waves, a ripple normal map, sky reflection) over each pool, and a volume of
   * tinted fog inside it. Lava spans are listed for the glow volumes and the heat haze. */
  {
    float sp[48][4];
    S.nwspan = world_water_spans(sp, 16);
    for (int i = 0; i < S.nwspan; i++) {
      memcpy(S.wspan[i], sp[i], sizeof sp[i]);
      if (cw_node_valid(S.water3d[i])) cw_node_free(S.water3d[i]);
      S.water3d[i] = make_node("Water3D", S.world, "Pool");
      cw_node w = S.water3d[i];
      cw_node_set(w, "size", cw_val_vec2(sp[i][1] - sp[i][0], 2.9f));
      cw_node_set(w, "subdivisions", cw_val_int(96));
      cw_node_set(w, "wave_count", cw_val_int(2));
      cw_node_set(w, "wave0", val_vec4(90, 3.2f, 0.06f, 0.4f));
      cw_node_set(w, "wave1", val_vec4(0, 1.7f, 0.03f, 0.3f));
      cw_node_set(w, "color", cw_val_color(0.12f, 0.42f, 0.5f, 1));
      cw_node_set(w, "deep_color", cw_val_color(0.01f, 0.08f, 0.16f, 1));
      cw_node_set(w, "reflection", cw_val_bool(true));
      cw_node_set(w, "refraction", cw_val_bool(false));
      cw_node_set(w, "resolution_scale", cw_val_float(0.5f));
      cw_node_set(w, "flow_direction", cw_val_vec2(1, 0));
      cw_node_set(w, "flow_speed", cw_val_float(0.05f));
      cw_node3d_set_position(w, (t3_vec3){ (sp[i][0] + sp[i][1]) * 0.5f, sp[i][3] - 0.08f, 0 });
    }
    S.nlspan = world_lava_spans(sp, 48);
    for (int i = 0; i < S.nlspan; i++) memcpy(S.lspan[i], sp[i], sizeof sp[i]);
  }
  /* effects */
  if (!cw_node_valid(S.fx_spark)) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
      S.fx_dust[i] = make_node("GPUParticles3D", S.root, "Dust");
      pfx(S.fx_dust[i], 28, 0.7f, 1.0f, 3.2f, 0.4f, cw_rgba(0.85f, 0.78f, 0.66f, 0.6f), -0.5f, 0, "0 1 0.95 0.85 0.55, 1 0.8 0.75 0.65 0", "0 0.4, 0.4 1, 1 1.4", 1.0f);
      cw_node_set(S.fx_dust[i], "spread", cw_val_float(70));
      cw_node_set(S.fx_dust[i], "direction", cw_val_vec3(0, 1, 0));
      cw_node_set(S.fx_dust[i], "soft_distance", cw_val_float(0.5f));
    }
    S.fx_spark = make_node("GPUParticles3D", S.root, "Spark");
    pfx(S.fx_spark, 36, 0.7f, 2.0f, 6.5f, 0.1f, cw_rgba(1, 0.8f, 0.25f, 1), -8.0f, 1, "0 1 1 0.8 1, 0.5 1 0.7 0.2 1, 1 1 0.4 0.1 0", "0 1, 1 0.2", 5.0f);
    cw_node_set(S.fx_spark, "draw_mode", cw_val_enum("STRETCHED"));
    S.fx_splash = make_node("GPUParticles3D", S.root, "Splash");
    pfx(S.fx_splash, 90, 1.0f, 3.0f, 9.0f, 0.14f, cw_rgba(0.75f, 0.92f, 1, 0.9f), -16.0f, 1, "0 1 1 1 0.9, 0.7 0.7 0.9 1 0.6, 1 0.6 0.85 1 0", "0 1, 1 0.4", 1.6f);
    cw_node_set(S.fx_splash, "draw_mode", cw_val_enum("STRETCHED"));
    cw_node_set(S.fx_splash, "spread", cw_val_float(38));
    cw_node_set(S.fx_splash, "direction", cw_val_vec3(0, 1, 0));
    S.fx_foam = make_node("GPUParticles3D", S.root, "Foam");
    pfx(S.fx_foam, 36, 1.4f, 0.4f, 1.8f, 0.5f, cw_rgba(0.9f, 0.97f, 1, 0.7f), -0.2f, 0, "0 1 1 1 0.7, 1 0.9 1 1 0", "0 0.5, 0.3 1, 1 1.3", 1.0f);
    cw_node_set(S.fx_foam, "spread", cw_val_float(85));
    cw_node_set(S.fx_foam, "direction", cw_val_vec3(0, 1, 0));
    S.fx_bubbles = make_node("GPUParticles3D", S.root, "Bubbles");
    pfx(S.fx_bubbles, 30, 1.2f, 0.8f, 2.2f, 0.09f, cw_rgba(0.8f, 0.95f, 1, 0.8f), 2.0f, 1, "0 1 1 1 0.9, 1 0.8 0.95 1 0", "0 0.6, 0.2 1, 1 1", 1.4f);
    S.fx_burst = make_node("GPUParticles3D", S.root, "Burst");
    pfx(S.fx_burst, 90, 0.9f, 3.0f, 10.0f, 0.3f, cw_rgba(1, 0.55f, 0.2f, 1), -6.0f, 1, "0 1 0.9 0.5 1, 0.4 1 0.4 0.1 0.8, 1 0.4 0.1 0.05 0", "0 0.5, 0.3 1, 1 1.5", 3.0f);
    S.fx_hit = make_node("GPUParticles3D", S.root, "Hit");
    pfx(S.fx_hit, 48, 0.6f, 3.0f, 10.0f, 0.12f, cw_rgba(1, 1, 0.8f, 1), -5.0f, 1, "0 1 1 0.9 1, 1 1 0.5 0.2 0", "0 1, 1 0.1", 5.0f);
    cw_node_set(S.fx_hit, "draw_mode", cw_val_enum("STRETCHED"));
    /* rock debris: small instanced boxes that fall and bounce on what the camera sees */
    S.fx_debris = make_node("GPUParticles3D", S.root, "Debris");
    pfx(S.fx_debris, 24, 1.8f, 3.0f, 9.0f, 0.12f, cw_rgba(0.55f, 0.42f, 0.32f, 1), -20.0f, 2, "", "", 1.0f);
    cw_node_set(S.fx_debris, "draw_mode", cw_val_enum("MESH"));
    cw_node_set(S.fx_debris, "mesh_primitive", cw_val_enum("BOX"));
    cw_node_set(S.fx_debris, "spin", cw_val_float(6));
    cw_node_set(S.fx_debris, "size_random", cw_val_float(0.6f));
    /* the death blast of an enemy: flash, smoke, sparks, and a ring that bends the picture */
    S.fx_boom = make_node("GPUParticles3D", S.root, "Boom");
    pfx(S.fx_boom, 40, 0.55f, 0.5f, 3.0f, 1.3f, cw_rgba(1, 0.7f, 0.3f, 1), 0.0f, 1, "0 1 0.95 0.7 1, 0.2 1 0.55 0.15 0.9, 1 0.35 0.08 0.03 0", "0 0.4, 0.3 1, 1 1.5", 6.0f);
    S.fx_boomsmoke = make_node("GPUParticles3D", S.root, "BoomSmoke");
    pfx(S.fx_boomsmoke, 36, 1.5f, 0.6f, 2.5f, 1.1f, cw_rgba(0.3f, 0.27f, 0.25f, 0.8f), 1.0f, 0, "0 0.3 0.26 0.24 0, 0.15 0.28 0.25 0.23 0.8, 1 0.4 0.38 0.36 0", "0 0.5, 0.5 1, 1 1.8", 1.0f);
    cw_node_set(S.fx_boomsmoke, "soft_distance", cw_val_float(0.6f));
    S.fx_boomspark = make_node("GPUParticles3D", S.root, "BoomSparks");
    pfx(S.fx_boomspark, 90, 1.1f, 4.0f, 14.0f, 0.1f, cw_rgba(1, 0.8f, 0.4f, 1), -9.0f, 1, "0 1 0.95 0.6 1, 0.5 1 0.5 0.1 1, 1 0.6 0.1 0.05 0", "0 1, 1 0.2", 6.0f);
    cw_node_set(S.fx_boomspark, "draw_mode", cw_val_enum("STRETCHED"));
    S.fx_wave = make_node("Shockwave3D", S.root, "Wave");
    cw_node_set(S.fx_wave, "autostart", cw_val_bool(false));
    cw_node_set(S.fx_wave, "max_radius", cw_val_float(4.5f));
    cw_node_set(S.fx_wave, "duration", cw_val_float(0.6f));
    cw_node_set(S.fx_wave, "thickness", cw_val_float(0.8f));
    cw_node_set(S.fx_wave, "strength", cw_val_float(0.45f));
    /* ambient motes that follow the camera: fireflies, cave dust, pollen, ash */
    S.fx_motes = make_node("GPUParticles3D", S.root, "Motes");
    pfx(S.fx_motes, 70, 5.0f, 0.1f, 0.5f, 0.07f, cw_rgba(1, 1, 0.6f, 1), 0.0f, 1, "0 1 1 1 0, 0.2 1 1 1 1, 0.8 1 1 1 1, 1 1 1 1 0", "0 0.3, 0.5 1, 1 0.3", 3.0f);
    cw_node_set(S.fx_motes, "one_shot", cw_val_bool(false));
    cw_node_set(S.fx_motes, "explosiveness", cw_val_float(0));
    cw_node_set(S.fx_motes, "emitting", cw_val_bool(true));
    cw_node_set(S.fx_motes, "emission_shape", cw_val_enum("BOX"));
    cw_node_set(S.fx_motes, "emission_extents", cw_val_vec3(22, 9, 3));
    cw_node_set(S.fx_motes, "spread", cw_val_float(180));
    cw_node_set(S.fx_motes, "turbulence", cw_val_float(0.5f));
    cw_node_set(S.fx_motes, "preprocess", cw_val_float(3));
    /* lava: embers over every span, a heat haze above, bubbles popping on the surface */
    for (int i = 0; i < 4; i++) {
      S.fx_haze[i] = make_node("GPUParticles3D", S.root, "Haze");
      pfx(S.fx_haze[i], 14, 2.4f, 0.4f, 1.1f, 2.2f, cw_rgba(1, 1, 1, 1), 0.0f, 3, "0 1 1 1 0, 0.3 1 1 1 0.8, 1 1 1 1 0", "0 0.6, 1 1.4", 1.0f);
      cw_node_set(S.fx_haze[i], "distort_strength", cw_val_float(0.10f));
      cw_node_set(S.fx_haze[i], "one_shot", cw_val_bool(false));
      cw_node_set(S.fx_haze[i], "explosiveness", cw_val_float(0));
      cw_node_set(S.fx_haze[i], "emitting", cw_val_bool(false));
      cw_node_set(S.fx_haze[i], "emission_shape", cw_val_enum("BOX"));
      cw_node_set(S.fx_haze[i], "spread", cw_val_float(12));
      cw_node_set(S.fx_haze[i], "direction", cw_val_vec3(0, 1, 0));
      S.fx_lavapop[i] = make_node("GPUParticles3D", S.root, "LavaPop");
      pfx(S.fx_lavapop[i], 40, 1.4f, 2.5f, 7.0f, 0.2f, cw_rgba(1, 0.5f, 0.1f, 1), -9.0f, 1, "0 1 0.8 0.3 1, 0.5 1 0.4 0.08 1, 1 0.5 0.1 0.03 0", "0 1, 1 0.3", 4.0f);
      cw_node_set(S.fx_lavapop[i], "one_shot", cw_val_bool(false));
      cw_node_set(S.fx_lavapop[i], "explosiveness", cw_val_float(0));
      cw_node_set(S.fx_lavapop[i], "emitting", cw_val_bool(false));
      cw_node_set(S.fx_lavapop[i], "emission_shape", cw_val_enum("BOX"));
      cw_node_set(S.fx_lavapop[i], "spread", cw_val_float(25));
      cw_node_set(S.fx_lavapop[i], "direction", cw_val_vec3(0, 1, 0));
      cw_node_set(S.fx_lavapop[i], "draw_mode", cw_val_enum("STRETCHED"));
    }
    for (int i = 0; i < 4; i++) {
      cw_node n = make_node("GPUParticles3D", S.root, "Ember");
      pfx(n, 70, 2.8f, 0.8f, 3.0f, 0.1f, cw_rgba(1, 0.5f, 0.1f, 1), 0.8f, 1, "0 1 0.7 0.2 0, 0.15 1 0.55 0.1 1, 1 0.5 0.1 0.03 0", "0 1, 1 0.3", 4.0f);
      cw_node_set(n, "one_shot", cw_val_bool(false));
      cw_node_set(n, "explosiveness", cw_val_float(0));
      cw_node_set(n, "emitting", cw_val_bool(false));
      cw_node_set(n, "emission_shape", cw_val_enum("BOX"));
      cw_node_set(n, "spread", cw_val_float(30));
      cw_node_set(n, "turbulence", cw_val_float(0.4f));
      cw_node_set(n, "direction", cw_val_vec3(0, 1, 0));
      S.fx_ember[i] = n;
    }
    S.nember = 4;
  }
  /* enemies seen alive: a death is a blast */
  for (int i = 0; i < L->nenemies; i++) S.prev_alive[i] = L->enemies[i].alive;
}

static void apply_mood(float x) {
  int xi = (int)x < 0 ? 0 : (int)x >= WORLD_W ? WORLD_W - 1 : (int)x;
  int b = G.L.biome[xi];
  const mood *m = &MOODS[b];
  float k = 0.04f;
  float *c = (float *)&cur_mood;
  const float *t = (const float *)m;
  for (size_t i = 0; i < sizeof(mood) / sizeof(float); i++) c[i] += (t[i] - c[i]) * k;
  const mood *q = &cur_mood;
  cw_node_set(S.env, "background", cw_val_color(q->fog[0], q->fog[1], q->fog[2], 1));
  cw_node_set(S.env, "sky_color", cw_val_color(q->sky[0], q->sky[1], q->sky[2], 1));
  cw_node_set(S.env, "ground_color", cw_val_color(q->sky[0] * 0.3f, q->sky[1] * 0.25f, q->sky[2] * 0.2f, 1));
  cw_node_set(S.env, "hemi_energy", cw_val_float(q->hemi));
  cw_node_set(S.env, "ambient_energy", cw_val_float(q->amb));
  cw_node_set(S.env, "fog_mode", cw_val_enum("LINEAR"));
  cw_node_set(S.env, "fog_color", cw_val_color(q->fog[0], q->fog[1], q->fog[2], 1));
  cw_node_set(S.env, "fog_near", cw_val_float(q->fog_near));
  cw_node_set(S.env, "fog_far", cw_val_float(q->fog_far));
  cw_node_set(S.sun, "color", cw_val_color(q->sun[0], q->sun[1], q->sun[2], 1));
  cw_node_set(S.sun, "energy", cw_val_float(q->sun_e));
  cw_node3d_set_rotation(S.sun, (t3_vec3){ q->sun_pitch, q->sun_yaw, 0 });
  cw_node_set(S.rim, "color", cw_val_color(q->rim[0], q->rim[1], q->rim[2], 1));
  cw_node_set(S.rim, "energy", cw_val_float(q->rim_e));
  cw_node_set(S.post, "exposure", cw_val_float(q->exposure));
  cw_node_set(S.post, "bloom_intensity", cw_val_float(q->bloom));
  cw_node_set(S.post, "grain_intensity", cw_val_float(q->grain));
  cw_node_set(S.post, "ca_intensity", cw_val_float(q->ca));
  /* a colour grade per region (assets/grade/*.cube, tools/gen-assets.mjs): when the region changes the grade fades out,
   * the next LUT is loaded and it fades back in, so a border never snaps */
  {
    static const char *const LUTS[BIOMES] = { "grade/meadow.cube", "grade/caves.cube", "grade/ruins.cube", "grade/frost.cube", "grade/volcano.cube" };
    static int lut_b = -1;
    static float gk = 0;
    if (b != lut_b) {
      gk -= 0.04f;
      if (gk <= 0 || lut_b < 0) gk = 0, lut_b = b, cw_node_set(S.post, "lut", cw_val_string(LUTS[b]));
    } else gk += (0.9f - gk) * 0.04f;
    cw_node_set(S.post, "grade", cw_val_bool(true));
    cw_node_set(S.post, "grade_intensity", cw_val_float(gk));
  }
  if (cw_node_valid(S.fog)) {
    cw_node_set(S.fog, "density", cw_val_float(q->vden));
    cw_node_set(S.fog, "color", cw_val_color(q->vcol[0], q->vcol[1], q->vcol[2], 1));
    cw_node_set(S.fog, "volumetric_ambient", cw_val_float(q->vamb));
    cw_node_set(S.fog, "volumetric_albedo", cw_val_color(q->vcol[0], q->vcol[1], q->vcol[2], 1));
    cw_node_set(S.fog, "sun_g", cw_val_float(q->vg));
  }
}

/* the nearest water and lava spans get the fog volumes (water tint, lava glow) and the lava its haze, embers and pops */
static void update_spans(float camx) {
  int vi = 0;
  for (int pass = 0; pass < 2; pass++) {
    int n = pass == 0 ? S.nwspan : S.nlspan;
    for (int i = 0; i < n && vi < 8; i++) {
      const float *sp = pass == 0 ? S.wspan[i] : S.lspan[i];
      float cx = (sp[0] + sp[1]) * 0.5f;
      if (fabsf(cx - camx) > 46 + (sp[1] - sp[0]) * 0.5f) continue;
      cw_node v = S.fvol[vi++];
      if (!cw_node_valid(v)) continue;
      float w = sp[1] - sp[0];
      if (pass == 0) {
        cw_node_set(v, "size", cw_val_vec3(w, sp[3] - sp[2], 3.6f));
        cw_node3d_set_position(v, (t3_vec3){ cx, (sp[2] + sp[3]) * 0.5f, 0 });
        cw_node_set(v, "density", cw_val_float(0.22));
        cw_node_set(v, "albedo", cw_val_color(0.03f, 0.22f, 0.33f, 1));
        cw_node_set(v, "emission", cw_val_color(0.01f, 0.07f, 0.12f, 1));
        cw_node_set(v, "emission_energy", cw_val_float(0.35));
        cw_node_set(v, "noise_wind", cw_val_vec3(0.15f, 0.05f, 0));
      } else {
        cw_node_set(v, "size", cw_val_vec3(w + 6, 8, 5));
        cw_node3d_set_position(v, (t3_vec3){ cx, 5.5f, 0 });
        cw_node_set(v, "density", cw_val_float(0.3));
        cw_node_set(v, "albedo", cw_val_color(1.0f, 0.4f, 0.12f, 1));
        cw_node_set(v, "emission", cw_val_color(1.0f, 0.3f, 0.05f, 1));
        cw_node_set(v, "emission_energy", cw_val_float(1.6 + 0.3 * sinf(S.time * 1.7f)));
        cw_node_set(v, "height_falloff", cw_val_float(0.25));
        cw_node_set(v, "noise_wind", cw_val_vec3(0.1f, 0.5f, 0));
      }
    }
  }
  for (; vi < 8; vi++)
    if (cw_node_valid(S.fvol[vi])) cw_node_set(S.fvol[vi], "density", cw_val_float(0));
  /* lava effects ride the nearest four spans (configured when a slot changes span: setting amount restarts a system) */
  static int slot_span[4] = { -2, -2, -2, -2 };
  int nl = 0;
  for (int i = 0; i < S.nlspan && nl < 4; i++) {
    const float *sp = S.lspan[i];
    float cx = (sp[0] + sp[1]) * 0.5f;
    if (fabsf(cx - camx) > 40) continue;
    float w = sp[1] - sp[0];
    cw_node h = S.fx_haze[nl], pop = S.fx_lavapop[nl], em = S.fx_ember[nl];
    if (slot_span[nl] != i) {
      slot_span[nl] = i;
      cw_node3d_set_position(h, (t3_vec3){ cx, 4.4f, 1.0f });
      cw_node_set(h, "emission_extents", cw_val_vec3(w * 0.5f, 0.3f, 1.2f));
      cw_node_set(h, "emitting", cw_val_bool(true));
      cw_node3d_set_position(pop, (t3_vec3){ cx, 2.9f, 0.5f });
      cw_node_set(pop, "emission_extents", cw_val_vec3(w * 0.5f, 0.1f, 1.2f));
      cw_node_set(pop, "emitting", cw_val_bool(true));
      cw_node3d_set_position(em, (t3_vec3){ cx, 3.2f, 0.5f });
      cw_node_set(em, "emission_extents", cw_val_vec3(w * 0.5f, 0.2f, 1.0f));
      cw_node_set(em, "emitting", cw_val_bool(true));
    }
    nl++;
  }
  for (; nl < 4; nl++)
    if (slot_span[nl] != -1) {
      slot_span[nl] = -1;
      cw_node_set(S.fx_haze[nl], "emitting", cw_val_bool(false));
      cw_node_set(S.fx_lavapop[nl], "emitting", cw_val_bool(false));
      cw_node_set(S.fx_ember[nl], "emitting", cw_val_bool(false));
    }
  /* bubbles in the nearest pool */
  static int bub_span = -2;
  int want = -1;
  for (int i = 0; i < S.nwspan; i++) {
    const float *sp = S.wspan[i];
    if (camx > sp[0] - 30 && camx < sp[1] + 30) want = i;
  }
  if (want != bub_span) {
    bub_span = want;
    if (want >= 0) {
      const float *sp = S.wspan[want];
      cw_node3d_set_position(S.fx_bubbles, (t3_vec3){ (sp[0] + sp[1]) * 0.5f, sp[2] + 0.4f, 0.6f });
      cw_node_set(S.fx_bubbles, "one_shot", cw_val_bool(false));
      cw_node_set(S.fx_bubbles, "explosiveness", cw_val_float(0));
      cw_node_set(S.fx_bubbles, "emission_shape", cw_val_enum("BOX"));
      cw_node_set(S.fx_bubbles, "emission_extents", cw_val_vec3((sp[1] - sp[0]) * 0.45f, 0.2f, 0.8f));
      cw_node_set(S.fx_bubbles, "direction", cw_val_vec3(0, 1, 0));
      cw_node_set(S.fx_bubbles, "spread", cw_val_float(10));
      cw_node_set(S.fx_bubbles, "lifetime", cw_val_float(fmaxf(0.8f, (sp[3] - sp[2]) / 1.6f)));
    }
    cw_node_set(S.fx_bubbles, "emitting", cw_val_bool(want >= 0));
  }
}

/* the shake check (g.camoffset): the camera as drawn (the camera node's global transform) against the follow camera
 * drawn between its last two ticks, on the frames within half a second of an impact (g.impacts: a boss landing, hit or
 * blast, a small enemy's blast, a player hurt). g.camoffset: those frames, the ones whose drawn camera was off the follow
 * camera by over 0.1 mm, and the largest offset (in 0.1 mm units). Nothing moves the camera on an impact: [n, 0, 0]. */
static int g_camoffset_dbg[3];
static int32_t g_rumble_sent_dbg[MAX_PLAYERS]; /* debug rumble.sent: requests sent to each player's pad */
static void offsetgate(cw_node cam, float vx, float vy, float vz) {
  static int seen, until;
  if (G.impacts != seen) seen = G.impacts, until = G.tick + 30;
  if (G.tick > until) return;
  const t3_mat4 *m = cw_node3d_global_transform(cam);
  if (!m) return;
  float dx = m->e[12] - vx, dy = m->e[13] - vy, dz = m->e[14] - vz, off = sqrtf(dx * dx + dy * dy + dz * dz);
  g_camoffset_dbg[0]++;
  if (off > 1e-4f) g_camoffset_dbg[1]++;
  if ((int)(off * 1e4f) > g_camoffset_dbg[2]) g_camoffset_dbg[2] = (int)(off * 1e4f);
}

/* ── frame: sync the scene to the sim ─────────────────────────────── */
/* the top of what a character at (x, y) would land on: a tile, a wooden ledge or a moving platform, at most `reach` below;
 * false over a pit or a long drop */
static bool floor_under(const game *g, float x, float y, float reach, float *top) {
  const level *L = &g->L;
  int c = (int)floorf(x);
  if (c < 0 || c >= WORLD_W) return false;
  float best = -1e9f;
  for (int ty = (int)floorf(y + 0.05f); ty >= 0 && (float)ty + 1.0f >= y - reach; ty--)
    if (ty < WORLD_H && (world_solid(L, c, ty) || L->t[ty][c] == T_ONEWAY) && (float)ty + 1.0f <= y + 0.05f) { best = (float)ty + 1.0f; break; }
  for (int i = 0; i < L->nplat; i++) {
    cw_pos2 pp = cw_body2d_position(L->plats[i].body);
    float pt = (float)pp.y + 0.2f;
    if (fabsf(x - (float)pp.x) < L->plats[i].w * 0.5f && pt <= y + 0.05f && pt > best) best = pt;
  }
  for (int i = 0; i < L->nthings; i++) { /* doors and crumbling planks: their boxes as the rules have them now */
    const thing *t = &L->things[i];
    if ((t->kind != TH_DOOR && t->kind != TH_CRUMBLE) || !t->body.id || (t->kind == TH_CRUMBLE && t->state == 2)) continue;
    float hw = t->kind == TH_DOOR ? t->w * 0.5f : 0.5f, hh = t->kind == TH_DOOR ? t->h * 0.5f : 0.5f;
    cw_pos2 tp = cw_body2d_position(t->body);
    float tt = (float)tp.y + hh;
    if (fabsf(x - (float)tp.x) < hw && tt <= y + 0.05f && tt > best) best = tt;
  }
  if (best < y - reach) return false;
  *top = best;
  return true;
}
/* how each world's own creature looks this frame (after the shared walker placement): the hopper squashes before a hop
 * and stretches in the air, the dropper hangs upside down from its thread and climbs it back, the shell is a shell (and
 * spins when kicked), the slider leans into its skate */
static void enemy_look(int i, const enemy *e, float dt, float *squash) {
  (void)dt;
  t3_mesh **x = S.eextra[i];
  switch (e->kind) {
    case EK_HOPPER:
      *squash = e->vy > 1 ? 1.25f : e->vy < -1 ? 1.1f : 1.0f;
      break;
    case EK_DROPPER: {
      bool hang = e->state == 0 || e->state == 3;
      if (hang) { /* upside down, its feet on the roof */
        cw_node3d_set_position(S.enode[i], (t3_vec3){ e->x, e->y + 0.95f, 0 });
        cw_node3d_set_rotation(S.emodel[i], (t3_vec3){ 0, S.eyaw[i], 3.14159265f });
      }
      if (e->state == 2 && e->t < 45) *squash = 0.75f + 0.1f * sinf(S.time * 30.0f); /* dazed, a quick wobble */
      if (x[0]) { /* the thread from the roof down to it while it is not up there */
        float top = e->hang_y + 0.95f, bot = hang ? e->y + 0.95f : e->y + 0.85f, len = fmaxf(0.0f, top - bot);
        x[0]->base.visible = e->state != 0 && len > 0.05f;
        x[0]->base.position = (t3_vec3){ e->x, bot + 0.5f * len, 0 };
        x[0]->base.scale = (t3_vec3){ 1, fmaxf(0.01f, len), 1 };
      }
      break;
    }
    case EK_SHELL: {
      bool shut = e->state > 0;
      cw_node_set(S.emodel[i], "visible", cw_val_bool(!shut));
      if (x[0]) {
        float s2 = shut ? 1.0f : 0.8f;
        x[0]->base.scale = (t3_vec3){ s2, s2, s2 };
        x[0]->base.position = (t3_vec3){ 0, shut ? 0.0f : 0.42f, shut ? 0.0f : -0.08f * (float)e->dir };
        static float spin[MAX_ENEMIES];
        if (e->state == 2) spin[i] += dt * 14.0f * (float)e->dir;
        t3_object_set_rotation(x[0], 0, spin[i], 0);
      }
      if (x[1]) {
        float s2 = e->state > 0 ? 1.0f : 0.8f;
        x[1]->base.scale = (t3_vec3){ s2, s2, s2 };
        x[1]->base.position = (t3_vec3){ 0, e->state > 0 ? 0.04f : 0.46f, e->state > 0 ? 0.0f : -0.08f * (float)e->dir };
      }
      break;
    }
    case EK_SLIDER:
      cw_node3d_set_rotation(S.emodel[i], (t3_vec3){ 0, S.eyaw[i], -0.18f * (float)e->dir }); /* leans into the skate */
      *squash = 0.92f;
      break;
  }
}
/* a soft contact shadow under a character, fading and widening with its height over the floor */
static void blob_add(const game *g, float x, float y, float z, float size, bool feet_on_floor) {
  g_blob_dbg[0]++;
  float top;
  if (!floor_under(g, x, y + 0.3f, 6.0f, &top)) return;
  float h = fmaxf(0.0f, y - top), k = 1.0f - h / 6.0f;
  if (cw_splats_add(S.blobs, (t3_vec3){ x, top, z }, size * 0.85f * (1.0f + 0.35f * (1 - k)), size * (1.0f + 0.35f * (1 - k)), 0, 0, (t3_color){ 0, 0, 0 }, 0.62f * k * k)) {
    g_blob_dbg[1]++;
    /* on a floor's top: a tile, a ledge or a platform (floor_under found it), and right under the feet when they stand */
    if (feet_on_floor && h > 0.12f) g_blob_dbg[2]++;
  }
}

static void coin_face_check(void);
static void sync_scene(float alpha, float dt) {
  game *g = &G;
  const level *L = &g->L;
  S.time += dt;
  camera_rig *c = &g->cam;
  /* the camera moves in the fixed tick like the players: draw it between its last two ticks with the same alpha the
   * players use, or it steps while they glide (a judder on any host whose frame rate is not the tick rate) */
  float a = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
  bool snapped = fabsf(c->x - c->px) > 4 || fabsf(c->y - c->py) > 4; /* a warp or a respawn: no glide */
  float vx = snapped ? c->x : c->px + (c->x - c->px) * a, vy = snapped ? c->y : c->py + (c->y - c->py) * a;
  float vz = snapped ? c->z : c->pz + (c->z - c->pz) * a;
  /* the follow camera is the whole camera: it never shakes (a boss landing, a hit or a blast is felt in the pads) */
  float ox = 0;
  if (g_cam_bug) ox = 0.01f * sinf((float)G.tick * 0.3f); /* the camera gate's control (COOP_CAM_BUG, native): a 1 cm sway */
#ifdef COOP_AUTOPILOT
  float cam_yaw = 0, cam_pitch = -0.06f;
  { /* COOP_CAM=dx,dy,zk[,yaw,pitch]: the follow camera moved, its distance scaled and turned (key art) */
    const char *e = getenv("COOP_CAM");
    float dx, dy, zk;
    if (e && sscanf(e, "%f,%f,%f,%f,%f", &dx, &dy, &zk, &cam_yaw, &cam_pitch) >= 3) vx += dx, vy += dy, vz *= zk;
  }
#endif
  cw_node3d_set_position(S.cam, (t3_vec3){ vx + ox, vy, vz });
  g_view_dbg[0] = vx + ox, g_view_dbg[1] = vy, g_view_dbg[2] = vz, g_view_dbg[3] = 0;
  if (!g_attract) { /* fov 40: half the view is tan(20 deg) z tall */
    float bot = vy - 0.364f * vz;
    g_viewbot_dbg = fminf(g_viewbot_dbg, bot);
    g_underview_dbg[0]++;
    if (bot < 0) g_underview_dbg[1]++;
  }
  if (!g_attract) {
    float pp[MAX_PLAYERS * 2];
    for (int i = 0; i < g->nplayers; i++) {
      cw_pos2 ip = cw_mover2d_interpolated(&g->p[i].pf.body, alpha);
      pp[2 * i] = (float)ip.x, pp[2 * i + 1] = (float)ip.y;
    }
    camgate_frame(g_view_dbg, pp, g->nplayers, snapped);
  }
#ifdef COOP_AUTOPILOT
  cw_node3d_set_rotation(S.cam, (t3_vec3){ cam_pitch, cam_yaw, 0 });
#else
  cw_node3d_set_rotation(S.cam, (t3_vec3){ -0.06f, 0, 0 });
#endif
  if (!g_attract) offsetgate(S.cam, vx, vy, vz);
  cw_node3d_set_position(S.sun, (t3_vec3){ vx + 8, vy + 18, 14 });
  apply_mood(vx);
  world_update_water_mesh(S.time, g->cam.x);
  world_update_backdrop(S.time);
  update_spans(vx);
  /* the motes follow the camera and take the region's character */
  {
    int b = g->L.biome[(int)c->x < 0 ? 0 : (int)c->x >= WORLD_W ? WORLD_W - 1 : (int)c->x];
    static const float MC[BIOMES][4] = { { 1.0f, 0.95f, 0.5f, 3.0f }, { 0.4f, 0.8f, 1.0f, 2.0f }, { 1.0f, 1.0f, 0.8f, 1.2f }, { 0.95f, 0.98f, 1.0f, 2.2f }, { 1.0f, 0.4f, 0.1f, 4.0f } };
    static const float MG[BIOMES] = { 0.0f, 0.0f, -0.05f, -0.7f, 0.9f };
    static int last_b = -1;
    cw_node3d_set_position(S.fx_motes, (t3_vec3){ vx, vy, 0 });
    if (b != last_b) {
      last_b = b;
      cw_node_set(S.fx_motes, "color", cw_val_color(MC[b][0], MC[b][1], MC[b][2], 1));
      cw_node_set(S.fx_motes, "energy", cw_val_float(MC[b][3]));
      cw_node_set(S.fx_motes, "gravity", cw_val_vec3(0, MG[b], 0));
    }
  }
  /* players. Two players in one spot (they pass through each other) would draw as one merged figure: each one of a
   * crowd is drawn a little apart, staggered sideways and in depth by its rank in the crowd, eased in and out. The
   * simulation is untouched. */
  float want_sx[MAX_PLAYERS] = { 0 }, want_sz[MAX_PLAYERS] = { 0 };
  for (int i = 0; i < g->nplayers && i < MAX_PLAYERS; i++) {
    const player *p = &g->p[i];
    if (!p->joined || !p->alive || p->bubble) continue;
    int rank = 0, crowd = 1;
    for (int k = 0; k < g->nplayers && k < MAX_PLAYERS; k++) {
      const player *q = &g->p[k];
      if (k == i || !q->joined || !q->alive || q->bubble) continue;
      double dx = q->pf.body.position.x - p->pf.body.position.x, dy = q->pf.body.position.y - p->pf.body.position.y;
      if (dx * dx < 0.8 * 0.8 && dy * dy < 1.2 * 1.2) {
        crowd++;
        if (k < i) rank++;
      }
    }
    if (crowd > 1) {
      want_sx[i] = ((float)rank - 0.5f * (float)(crowd - 1)) * 0.55f;
      want_sz[i] = (rank & 1 ? -0.5f : 0.35f) * (float)(1 + rank / 2);
      /* never drawn into a wall: a sideways step that would put the body in rock goes the other way */
      float bx = (float)p->pf.body.position.x + want_sx[i] + (want_sx[i] > 0 ? 0.35f : -0.35f);
      if (world_solid(&g->L, (int)floorf(bx), (int)floorf((float)p->pf.body.position.y + 0.6f))) want_sx[i] = -want_sx[i];
    }
  }
  int bubbles = 0;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    player *p = &g->p[i];
    t3_object *po = cw_node3d_object(S.pnode[i]);
    if (!p->joined || i >= g->nplayers) {
      cw_node3d_set_position(S.pnode[i], (t3_vec3){ 0, -100, 0 });
      cw_node_set(S.plight[i], "energy", cw_val_float(0));
      continue;
    }
    cw_pos2 ip = cw_mover2d_interpolated(&p->pf.body, alpha);
    float x = (float)ip.x, y = (float)ip.y;
    if (p->bubble) {
      bubbles++;
      x = p->bubble_x, y = p->bubble_y - 0.6f + 0.1f * sinf(S.time * 3 + (float)i);
      S.bubble[i]->base.visible = true;
      p->squash = 0;
    } else S.bubble[i]->base.visible = false;
    if (!p->alive && !p->bubble) {
      cw_node3d_set_position(S.pnode[i], (t3_vec3){ x, -100, 0 });
      continue;
    }
    float ease = 1.0f - expf(-8.0f * dt);
    S.spread_x[i] += (want_sx[i] - S.spread_x[i]) * ease, S.spread_z[i] += (want_sz[i] - S.spread_z[i]) * ease;
    cw_node3d_set_position(S.pnode[i], (t3_vec3){ x + S.spread_x[i], y, S.spread_z[i] });
    float face = (float)p->pf.facing;
    /* turn toward the way it faces, smoothly: three quarters on to the camera */
    float want = face > 0 ? 1.05f : -1.05f;
    if (p->anim == 4) want = -want; /* on a wall it looks back along the wall */
    S.yaw[i] += (want - S.yaw[i]) * (1.0f - expf(-14.0f * dt));
    cw_node3d_set_rotation(S.pmodel[i], (t3_vec3){ 0, S.yaw[i], 0 });
    /* a run leans into it, more the faster it goes, and throws dust from the heels at speed */
    {
      float lean = p->anim == 1 ? -face * 0.3f * p->run : 0.0f;
      static float drawn_lean[MAX_PLAYERS];
      drawn_lean[i] += (lean - drawn_lean[i]) * (1.0f - expf(-10.0f * dt));
      cw_node3d_set_rotation(S.pnode[i], (t3_vec3){ 0, 0, drawn_lean[i] });
      static int dust_t[MAX_PLAYERS];
      if (p->anim == 1 && p->run > 0.6f && ++dust_t[i] >= 7) dust_t[i] = 0, burst(S.fx_dust[i], x - face * 0.3f, y + 0.08f, 0.2f);
    }
    /* secondary animation: squash and stretch from events, a run cycle and the pose from the bones */
    if (p->land_fx) p->squash = 0.35f;
    if (p->jump_fx) p->squash = -0.3f;
    p->squash *= 1.0f - 6.0f * dt > 0 ? 1.0f - 6.0f * dt : 0;
    float vy = p->pf.body.velocity.y;
    float stretch = p->squash < 0 ? -p->squash : 0.0f;
    float sq = p->squash > 0 ? p->squash : 0.0f;
    float st = 1.0f + stretch * 0.6f + (vy > 4 ? 0.06f : 0) - sq * 0.5f;
    float wd = 1.0f - stretch * 0.3f + sq * 0.4f;
    if (p->pancake > 0) st = 0.28f, wd = 1.5f; /* a friend has landed on this head */
    float spd = fabsf((float)p->pf.body.velocity.x);
    S.runph[i] += (p->anim == 5 ? 7.0f : 4.0f + spd * 0.9f) * dt;
    if (S.prig[i]) {
      rig_pose_hero(S.prig[i], p->anim, S.runph[i], S.time + (float)i, st, wd, vy);
      /* the blink while invulnerable: each player on its own phase, so a group that respawns together never vanishes
       * all at once (the screen showed four markers and no one under them) */
      /* phases spread evenly over the 8-tick cycle: with two players a 2-tick offset left both hidden a quarter of the time */
      bool inv = p->invuln > 0 && (((p->invuln + (g_blink_bug ? 2 * i : 8 * i / (G.nplayers > 0 ? G.nplayers : 1))) / 4) & 1);
      if (p->joined && p->alive && !p->bubble) { g_blink_live++; if (inv) g_blink_hid++; }
      cw_node_set(S.pmodel[i], "visible", cw_val_bool(!inv));
    }
    cw_node3d_set_position(S.plight[i], (t3_vec3){ x, y + 1.2f, 2.0f });
    cw_node_set(S.plight[i], "energy", cw_val_float(p->bubble ? 3.0f : 2.0f));
    /* effects from this frame's events */
    if (p->land_fx == 1) burst(S.fx_dust[i], x, y + 0.1f, 0.5f);
    if (p->land_fx == 2) burst(S.fx_splash, x, y + 0.8f, 1.0f);
    if (p->jump_fx) burst(S.fx_dust[i], x, y + 0.1f, 0.5f);
    if (p->hurt_fx) burst(S.fx_hit, x, y + 0.8f, 0.5f), p->hurt_fx = 0;
  }
  /* coins, pickups, platforms, enemies */
  for (int i = 0; i < L->ncoins; i++) {
    bool tk = L->coins[i].taken;
    cw_node_set(S.coin_node[i], "visible", cw_val_bool(!tk));
    if (!tk) cw_node3d_set_rotation(S.coin_node[i], (t3_vec3){ 0, S.time * 2.5f + (float)i, 0 });
  }
  coin_face_check();
  world_things_sync(g, S.time);
  if (hats_due) hats_apply(), hats_due = false;
  /* the near layer stands at z 7 and the camera looks through it: find where the line from the camera to a player's feet
   * crosses that plane, and ask whether the layer's top is above that point there */
  for (int i = 0; i < g->nplayers; i++) {
    const player *p = &g->p[i];
    if (!p->joined || !p->alive || p->bubble || g->cam.z <= 7.5f) continue;
    float px = (float)p->pf.body.position.x, py = (float)p->pf.body.position.y;
    float k = (g->cam.z - 7.0f) / g->cam.z, cx = g->cam.x + (px - g->cam.x) * k, cy = g->cam.y + (py - g->cam.y) * k;
    /* a body falling in a pit is on its way out of the picture; below the lip it does not count */
    if (sim_open_pit(&g->L, px) && py < sim_lip_of(&g->L, px)) continue;
    g_nearcover_dbg[0]++;
    if (world_near_height(&g->L, cx) > cy) g_nearcover_dbg[1]++;
  }
  for (int i = 0; i < g->nplayers; i++) {
    const player *p = &g->p[i];
    float px = (float)p->pf.body.position.x;
    if (p->joined && p->alive && !p->bubble && sim_open_pit(&g->L, px)) {
      float lip = sim_lip_of(&g->L, px);
      if ((float)p->pf.body.position.y < lip) {
        g_pitview_dbg[0]++;
        if (g->cam.y < lip) g_pitview_dbg[1]++;
      }
    }
  }
  /* contact blobs: every hero on the field and every live enemy (the sun throws none of their shadows: on the hills
   * behind the play they were dark figures of their own) */
  g_blob_dbg[0] = g_blob_dbg[1] = g_blob_dbg[2] = 0;
  if (S.blobs) {
    cw_splats_begin(S.blobs);
    if (!g_shadow_old) {
      for (int i = 0; i < g->nplayers && i < MAX_PLAYERS; i++) {
        const player *p = &g->p[i];
        if (!p->joined || !p->alive || p->bubble) continue;
        cw_pos2 ip = cw_mover2d_interpolated(&p->pf.body, alpha);
        blob_add(g, (float)ip.x + S.spread_x[i], (float)ip.y, S.spread_z[i], 1.0f, p->pf.body.on_floor);
      }
      for (int i = 0; i < L->nenemies; i++) {
        const enemy *e = &L->enemies[i];
        if (!e->alive) continue;
        blob_add(g, e->x, e->y, 0, e->kind == 2 ? 1.6f * boss_scale(L->boss_kind) : e->kind == 1 ? 0.8f : 0.95f, false);
      }
    }
    cw_splats_end(S.blobs);
  }
  g_rim_dbg = cw_rim_light_count();
  g_charshadow_dbg = 0;
  for (int i = 0; i < MAX_PLAYERS; i++) if (S.prig[i] && g_shadow_old) g_charshadow_dbg++;
  /* the heroes' rims: strong where the world is dark (the caves, the crater), a trace elsewhere */
  {
    int b = g->L.biome[(int)c->x < 0 ? 0 : (int)c->x >= WORLD_W ? WORLD_W - 1 : (int)c->x];
    if (b != S.rim_biome && !g_rim_off) {
      static const float RIM_K[BIOMES] = { 0.35f, 1.5f, 0.5f, 0.4f, 1.3f };
      S.rim_biome = b;
      for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!S.prig[i]) continue;
        cw_rim_light_desc rd = cw_rim_light_default();
        rd.color = (t3_color){ 0.45f + 0.55f * PLAYER_RGB[i][0], 0.45f + 0.55f * PLAYER_RGB[i][1], 0.45f + 0.55f * PLAYER_RGB[i][2] };
        rd.strength = RIM_K[b], rd.power = 2.6f, rd.near = 0, rd.far = 1, rd.close = 1;
        rig_rim(S.prig[i], &rd);
      }
    }
  }
  gore_step(dt);
  for (int i = 0; i < L->npickups; i++) {
    cw_node_set(S.pick_node[i], "visible", cw_val_bool(!L->pickups[i].taken));
    cw_node3d_set_position(S.pick_node[i], (t3_vec3){ (float)L->pickups[i].x + 0.5f, (float)L->pickups[i].y + 1.1f + 0.12f * sinf(S.time * 2.0f), 0 });
    cw_node3d_set_rotation(S.pick_node[i], (t3_vec3){ 0, S.time * 1.5f, 0 });
  }
  for (int i = 0; i < L->nplat; i++) {
    cw_pos2 pp = cw_body2d_position(L->plats[i].body);
    for (int k = 0; k < S.nplat_mesh[i]; k++) S.plat_mesh[i][k]->base.position = (t3_vec3){ (float)pp.x, (float)pp.y, 0 };
  }
  for (int i = 0; i < L->nenemies; i++) {
    const enemy *e = &L->enemies[i];
    cw_node_set(S.enode[i], "visible", cw_val_bool(e->alive));
    if (e->kind == EK_DROPPER && S.eextra[i][0] && !e->alive) S.eextra[i][0]->base.visible = false;
    if (S.prev_alive[i] && !e->alive) { /* it just died: a blast, bigger for the boss */
      float bs = e->kind == 2 ? 2.4f : 1.0f, by = e->y + (e->kind == 2 ? 1.0f : 0.5f);
      burst(S.fx_boom, e->x, by, 0.8f), burst(S.fx_boomsmoke, e->x, by, 0.6f), burst(S.fx_boomspark, e->x, by, 0.6f);
      cw_node_set(S.fx_wave, "max_radius", cw_val_float(4.5f * bs));
      cw_node3d_set_position(S.fx_wave, (t3_vec3){ e->x, by, 1.0f });
      cw_shockwave_fire(S.fx_wave);
      burst(S.fx_debris, e->x, by, 0.5f); /* its rumble comes from the sim (sim_rumble_at), where it died */
    }
    S.prev_alive[i] = e->alive;
    if (e->kind == 2 && !g_attract) {
      g_boss_dbg[0] = L->boss_kind + 1;
      g_boss_dbg[2] = 5 + L->boss_kind - (e->hp > 0 ? e->hp : 0);
      if (!e->alive) g_boss_dbg[3]++;
      else if (fabsf(e->x - vx) < 0.648f * vz) g_boss_dbg[1]++; /* half the view is tan(20 deg) z tall, 16:9 wide */
      if (e->alive) { /* how much of the view is the wall at the end of the level: its front face and the side of it facing the arena, so measured at its far edge */
        bool near = false;
        for (int k = 0; k < g->nplayers; k++) if (g->p[k].joined && (float)g->p[k].pf.body.position.x > (float)L->boss_x - 22.0f) near = true;
        if (near) {
          float half = 0.64706f * (vz + 1.5f), share = (vx + half - (float)L->width) / (2 * half);
          int mil = share > 0 ? (int)(share * 1000) : 0;
          g_endwall_dbg[0]++;
          if (mil > g_endwall_dbg[1]) g_endwall_dbg[1] = mil;
          if (mil > 120) g_endwall_dbg[2]++;
        }
      }
    }
    if (!e->alive || !S.erig[i]) continue;
    cw_node3d_set_position(S.enode[i], (t3_vec3){ e->x, e->y + (e->kind == 1 ? 0.2f * sinf(S.time * 5 + (float)i) : 0), 0 });
    float want = e->dir > 0 ? 1.2f : -1.2f;
    S.eyaw[i] += (want - S.eyaw[i]) * (1.0f - expf(-10.0f * dt));
    cw_node3d_set_rotation(S.emodel[i], (t3_vec3){ 0, S.eyaw[i], 0 });
    float esq = 1.0f; /* the walker pose's squash: the new kinds use it */
    if (e->kind >= EK_HOPPER) enemy_look(i, e, dt, &esq);
    float flash = e->hurt > 0 && (e->hurt / 3) % 2 ? 1.0f : 0.0f;
    S.ephase[i] += dt * (e->kind == 1 ? 16.0f : 11.0f);
    if (e->kind != EK_FLYER && e->kind != EK_BOSS) rig_pose_walker(S.erig[i], S.ephase[i], S.time, esq);
    else if (e->kind == 1) rig_pose_flyer(S.erig[i], S.ephase[i], S.time);
    else {
      rig_pose_boss(S.erig[i], S.time, 1.0f - (float)e->hp / 9.0f);
      rig_glow(S.erig[i], 1.0f + 0.4f * sinf(S.time * 4) + (1.0f - (float)e->hp / 9.0f));
      if (!g_attract) { /* no arm in a wall: every pose drawn, measured against the rock at the shoulder's height and the hand's */
        float reach[2];
        rig_boss_reach(S.erig[i], S.eyaw[i], boss_scale(L->boss_kind), &reach[0], &reach[1]);
        float worst = 0;
        for (int side = 0; side < 2; side++) {
          float hx = e->x + reach[side];
          for (float up = 0.6f; up < 2.4f; up += 0.45f) {
            int cx = (int)floorf(hx), cy = (int)floorf(e->y + up);
            if (!world_solid(L, cx, cy)) continue;
            float in = side ? hx - (float)cx : (float)(cx + 1) - hx;
            if (in > worst) worst = in;
          }
        }
        g_bossclip_dbg[0]++;
        if (worst > 0.005f) g_bossclip_dbg[1]++;
        if ((int)(worst * 1000) > g_bossclip_dbg[2]) g_bossclip_dbg[2] = (int)(worst * 1000);
      }
    }
    rig_flash(S.erig[i], flash);
  }
  /* the nearest few light sources to the camera */
  int pick[NTORCH], np = 0;
  for (int k = 0; k < S.nlight_src && np < 64; k++) {
    float d = fabsf(S.light_x[k] - c->x);
    if (d > 26) continue;
    int at = np++;
    while (at > 0 && fabsf(S.light_x[pick[at - 1]] - c->x) > d) pick[at] = pick[at - 1], at--;
    if (at < NTORCH) pick[at] = k; else np--;
    if (np > NTORCH) np = NTORCH;
  }
  for (int i = 0; i < NTORCH; i++) {
    if (i < np) {
      int k = pick[i];
      float flick = 1.0f + 0.12f * sinf(S.time * 11.0f + (float)k * 2.3f) + 0.06f * sinf(S.time * 23.0f + (float)k);
      cw_node3d_set_position(S.torch[i], (t3_vec3){ S.light_x[k], S.light_y[k], S.light_z[k] });
      cw_node_set(S.torch[i], "color", cw_val_color(S.light_r[k], S.light_g[k], S.light_b[k], 1));
      cw_node_set(S.torch[i], "energy", cw_val_float(S.light_e[k] * flick));
      cw_node_set(S.torch[i], "range", cw_val_float(S.light_range[k]));
    } else cw_node_set(S.torch[i], "energy", cw_val_float(0));
  }
  /* the flames burn where the camera can see them (a flame far off screen does not spend its particles) */
  world_surface_audit(g_surface_dbg);
  g_uvrep_dbg = world_uv_repeat();
  if (g_blink_live > 0 && g_blink_hid == g_blink_live) g_vanish_dbg++;
  g_blink_live = g_blink_hid = 0;
  g_flames_dbg[0] = g_flames_dbg[1] = 0;
  for (int i = 0; i < S.nflame; i++) {
    bool on = fabsf(S.flame_at[i][0] - c->x) < 45.0f;
    if (on != S.flame_on[i]) cw_node_set(S.flame[i], "emitting", cw_val_bool(on)), S.flame_on[i] = on;
    t3_vec3 fp = cw_node3d_position(S.flame[i]); /* where the emitter really is (its parent, the world, sits at 0) */
    g_flames_dbg[0]++;
    if (!world_flame_seated(fp.x, fp.y, fp.z)) g_flames_dbg[1]++;
  }
  g_bubbles_dbg = bubbles;
}

/* the coins as drawn: read each coin's disc axis (mesh Y) from the mesh's own quaternion, turned by the spin node's Y spin.
 * A coin stands up when that axis is level (no part of it along world up), so the spin shows its face to the camera */
static void coin_face_check(void) {
  int n = 0, flat = 0;
  for (int i = 0; i < G.L.ncoins; i++) {
    const t3_mesh *m = S.coin_mesh[i];
    if (!m) continue;
    n++;
    t3_quat q = m->base.quaternion;
    /* the world up part of the disc axis: the Y of q applied to (0, 1, 0). The node above only spins about Y, which keeps it */
    float up = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
    if (fabsf(up) > 0.02f) flat++;
  }
  g_coinface_dbg[0] = n, g_coinface_dbg[1] = flat;
}

/* ── input ─────────────────────────────────────────────────────────── */
static void read_input(void) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    input_state *s = &INPUT[i];
    memset(s, 0, sizeof *s);
    if (i >= G.nplayers) continue;
    s->x = cw_input_axis(i, a_left, a_right);
    s->y = -cw_input_axis(i, a_up, a_down);
    s->jump_pressed = cw_action_just_pressed(i, a_jump);
    s->jump_held = cw_action_pressed(i, a_jump);
    s->run = cw_action_pressed(i, a_run);
    s->grab_pressed = cw_action_just_pressed(i, a_grab);
    s->dash_pressed = cw_action_just_pressed(i, a_dash);
    s->down = cw_action_pressed(i, a_down);
    s->retry = cw_action_pressed(i, a_retry);
  }
}

/* ── the shell and the game flow ──────────────────────────────────── */
/* the autopilot builds that carry a start (gates on hosts that have no environment): players, start column, abilities */
#ifndef COOP_DEF_PLAYERS
#define COOP_DEF_PLAYERS 2
#endif
#ifndef COOP_DEF_START_X
#define COOP_DEF_START_X (-1)
#endif
#ifndef COOP_DEF_ABILITIES
#define COOP_DEF_ABILITIES (-1)
#endif
static int cfg_int(const char *env, int def) {
  const char *v = getenv(env);
  return v ? atoi(v) : def;
}

/* the level a build starts on (gates: a variant for a cart host, which has no environment) */
#ifndef COOP_DEF_LEVEL
#define COOP_DEF_LEVEL (-1)
#endif
static void music_for_level(void);
static void music_play(int track);
static void start_match(int players, void *user) {
  (void)user;
  g_attract = false;
  save_t *sv = cw_shell_game_save();
  G.abilities = 0;
  G.level = sv->level < NLEVELS && sv->level >= 0 && !getenv("COOP_FRESH") ? sv->level : 0;
  if (cfg_int("COOP_LEVEL", COOP_DEF_LEVEL) >= 0) G.level = cfg_int("COOP_LEVEL", COOP_DEF_LEVEL);
  G.abilities = LEVELS[G.level].abilities;
  sim_reset(&G, players);
  memset(G.rumble, 0, sizeof G.rumble); /* nothing the attract mode asked for carries into the match */
  g_cam_bug = getenv("COOP_CAM_BUG") != NULL; /* the camera gate's must-fail control (native only) */
  g_blink_bug = getenv("COOP_BLINK_BUG") != NULL; /* the blink gate's control (native only) */
  g_coin_flat_bug = getenv("COOP_COIN_FLAT_BUG") != NULL; /* the coin gate's control (native only) */
  g_vanish_dbg = 0;
  g_hud_bug = getenv("COOP_HUD_BUG") != NULL; /* the rescue-tag gate's control (native only) */
  g_sign_bug = getenv("COOP_SIGN_BUG") != NULL; /* the sign gate's control (native only) */
  g_card_bug = getenv("COOP_CARD_BUG") != NULL; /* the card gate's control (native only) */
  memset(g_card_dbg, 0, sizeof g_card_dbg);
  memset(g_boss_dbg, 0, sizeof g_boss_dbg);
  memset(g_endwall_dbg, 0, sizeof g_endwall_dbg), memset(g_bossclip_dbg, 0, sizeof g_bossclip_dbg);
  memset(g_label_dbg, 0, sizeof g_label_dbg), memset(g_signmove_dbg, 0, sizeof g_signmove_dbg), memset(g_labelmove_dbg, 0, sizeof g_labelmove_dbg);
  memset(g_words_dbg, 0, sizeof g_words_dbg);
  g_cam_endwall_bug = getenv("COOP_ENDWALL_BUG") != NULL; /* the end wall gate's control (native only) */
  g_boss_clip_bug = getenv("COOP_BOSSCLIP_BUG") != NULL; /* the clip gate's control (native only) */
  g_words_bug = getenv("COOP_WORDS_BUG") != NULL; /* the words gate's control (native only) */
  g_label_bug = getenv("COOP_LABEL_BUG") != NULL; /* the label gate's control (native only) */
  g_bot_no_boss = getenv("COOP_BOT_NO_BOSS") != NULL; /* the arena gate: the bots stay at the end wall */
  g_cam_nofloor = getenv("COOP_PIT_CAM_BUG") != NULL; /* the pit camera gate's control (native only) */
  if (sv->abilities && !getenv("COOP_FRESH")) G.abilities |= sv->abilities;
  if (sv->level == G.level) G.reached |= sv->reached;
  if (cfg_int("COOP_ABILITIES", COOP_DEF_ABILITIES) >= 0) G.abilities = (uint32_t)cfg_int("COOP_ABILITIES", COOP_DEF_ABILITIES);
#ifdef COOP_DEF_AT_BOSS
  int start_x = G.L.boss_x - 24; /* the door of the arena, wherever the road to it ends */
#else
  int start_x = cfg_int("COOP_START_X", COOP_DEF_START_X);
#endif
  if (start_x >= 0) { /* a test or a screenshot starts anywhere */
    int sx = start_x;
    if (sx < 2) sx = 2;
    if (sx > WORLD_W - 3) sx = WORLD_W - 3;
    for (int i = 0; i < G.nplayers; i++) {
      cw_platformer2d_teleport(&G.p[i].pf, (cw_pos2){ (double)sx + 0.7 * i, (double)G.L.ground[sx] + 0.05 });
      G.p[i].invuln = 120;
    }
#ifdef COOP_DEF_WALK_ONLY
    for (int i = 0; i < G.nplayers; i++) G.p[i].invuln = 0; /* the gag gate: nothing protects them from the hedge trimmer */
#endif
    G.cam.x = G.cam.tx = G.cam.px = (float)sx, G.cam.y = G.cam.ty = G.cam.py = (float)G.L.ground[sx] + 3;
    G.checkpoint = -1;
    for (int k = 0; k < G.L.nchecks; k++)
      if (G.L.checks[k].x < sx) G.checkpoint = k, G.reached |= 1u << k;
  }
  build_scene();
  camgate_reset();
  cw_info("coopplatformer: pools %d, lava spans %d", S.nwspan, S.nlspan);
  for (int i = 0; i < S.nwspan; i++) cw_info("  pool x %.0f..%.0f y %.0f..%.0f", S.wspan[i][0], S.wspan[i][1], S.wspan[i][2], S.wspan[i][3]);
  g_run_ticks = 0;
  g_map_open = false;
  music_for_level();
}
static void quit_match(void *user) {
  (void)user;
}
static void sound_hook(cw_shell_sound s, void *user) {
  (void)user;
  if (!A.on) return;
  switch (s) {
    case CW_SHELL_SND_MOVE: sfx(SFX_CHECK, 0.3f); break;
    case CW_SHELL_SND_SELECT: sfx(SFX_COIN, 0.5f); break;
    case CW_SHELL_SND_BACK: sfx(SFX_LAND, 0.5f); break;
    case CW_SHELL_SND_JOIN: sfx(SFX_POP, 0.6f); break;
    case CW_SHELL_SND_PAUSE: sfx(SFX_BUBBLE, 0.4f); break;
  }
}

#ifdef COOP_AUTOPILOT
static uint32_t fz = 12345;
static uint32_t frand(void) {
  fz = fz * 1664525u + 1013904223u;
  return fz >> 8;
}

/* a fresh run: no abilities unless COOP_ABILITIES says the player resumed a save with them */
static void soak_reset(int players) {
  G.level = getenv("COOP_LEVEL") ? atoi(getenv("COOP_LEVEL")) : 0;
  G.abilities = LEVELS[G.level].abilities;
  sim_reset(&G, players);
  if (getenv("COOP_ABILITIES")) G.abilities = (uint32_t)atoi(getenv("COOP_ABILITIES"));
}

/* COOP_SECRETS=1: scripted routes to the secret coins. A group behind a cracked block: stand 3 tiles left of the
 * block with every ability, dash into it and walk on. A group in water: dive from the surface above it, swim to it
 * and stroke back up. Each must collect every coin of its group (and come back out of the water). */
static void secret_routes(int players) {
  soak_reset(players);
  int groups = 0, ok = 0;
  for (int c0 = 0; c0 < G.L.ncoins; c0++) {
    if (!G.L.coins[c0].secret || (c0 > 0 && G.L.coins[c0 - 1].secret && G.L.coins[c0].x - G.L.coins[c0 - 1].x < 3.5f && G.L.coins[c0].y > G.L.coins[c0 - 1].y - 0.1f && G.L.coins[c0].y < G.L.coins[c0 - 1].y + 0.1f)) continue;
    int c1 = c0;
    while (c1 + 1 < G.L.ncoins && G.L.coins[c1 + 1].secret && G.L.coins[c1 + 1].x - G.L.coins[c1].x < 3.5f) c1++;
    float cx = G.L.coins[c0].x, cy = G.L.coins[c0].y;
    int ix = (int)cx, iy = (int)cy, crack_x = -1;
    for (int x = ix; x >= ix - 8 && x >= 0 && crack_x < 0; x--)
      for (int y = iy - 1; y <= iy + 1; y++)
        if (G.L.t[y][x] == T_CRACK) crack_x = x;
    bool wet = G.L.t[iy][ix] == T_WATER;
    if (crack_x < 0 && !wet) {
      /* a platforming route: the wooden ledges under the coins are waypoints (lowest first, then up to the
       * coins); a small controller walks, jumps and air-jumps from one to the next with every ability */
      float wx[4], wy[4];
      int nw = 0;
      for (int y = iy - 1; y >= iy - 10 && y >= 0 && nw < 3; y--)
        for (int x = ix - 12; x <= ix + 3 && nw < 3; x++)
          if (G.L.t[y][x] == T_ONEWAY && (x == 0 || G.L.t[y][x - 1] != T_ONEWAY)) {
            int x1 = x;
            while (G.L.t[y][x1] == T_ONEWAY) x1++;
            wx[nw] = (float)(x + x1) * 0.5f, wy[nw] = (float)y + 1.0f;
            nw++;
            break; /* one ledge per row */
          }
      for (int i = 0; i < nw / 2; i++) { /* the scan found the highest first: lowest first */
        float tx = wx[i], ty = wy[i];
        wx[i] = wx[nw - 1 - i], wy[i] = wy[nw - 1 - i], wx[nw - 1 - i] = tx, wy[nw - 1 - i] = ty;
      }
      wx[nw] = cx, wy[nw] = cy - 0.7f, nw++;
      soak_reset(players);
      G.abilities = AB_DOUBLE | AB_CLAWS | AB_DASH;
      for (int i = 1; i < G.nplayers; i++) G.p[i].joined = false;
      int sxi = (int)cx - 9;
      while (sxi > 2 && (G.L.ground[sxi] < 8 || G.L.ground[sxi - 1] != G.L.ground[sxi] || G.L.ground[sxi + 1] != G.L.ground[sxi])) sxi--; /* flat ground, not a gap */
      cw_platformer2d_teleport(&G.p[0].pf, (cw_pos2){ (double)sxi, (double)G.L.ground[sxi] + 0.05 });
      int before = G.secrets, group = c1 - c0 + 1, reached = 0;
      for (int w = 0; w < nw && G.p[0].alive; w++) {
        for (int t = 0; t < 600; t++) {
          const cw_platformer2d *pf = &G.p[0].pf;
          float x = (float)pf->body.position.x, y = (float)pf->body.position.y, vy = pf->body.velocity.y;
          memset(INPUT, 0, sizeof INPUT);
          float dx = wx[w] - x;
          INPUT[0].x = dx > 0.25f ? 1.0f : dx < -0.25f ? -1.0f : 0.0f;
          if (pf->body.on_floor && y < wy[w] - 0.5f) INPUT[0].jump_pressed = true;
          if (!pf->body.on_floor && vy < 0.5f && y < wy[w] - 0.2f && pf->air_jumps_left > 0 && (t % 5) == 0) INPUT[0].jump_pressed = true;
          INPUT[0].jump_held = vy > 0 || INPUT[0].jump_pressed;
          sim_step(&G, INPUT, 1.0f / 60.0f);
          if (!G.p[0].alive) break;
          if (pf->body.on_floor && dx < 0.6f && dx > -0.6f && y > wy[w] - 0.6f) {
            if (getenv("COOP_TRACE")) printf("  waypoint %d (%.1f,%.1f) reached at t=%d\n", w, wx[w], wy[w], t);
            reached++;
            break;
          }
          if (G.secrets - before >= group) break;
        }
      }
      if (getenv("COOP_TRACE")) printf("  waypoints %d of %d, end %.2f,%.2f\n", reached, nw, G.p[0].pf.body.position.x, G.p[0].pf.body.position.y);
      groups++;
      bool good = G.secrets - before >= group;
      printf("secret group at %.0f,%.0f (platforming, %d ledges): %d of %d coins\n", cx, cy, nw - 1, G.secrets - before, group);
      if (good) ok++;
      continue;
    }
    groups++;
    soak_reset(players);
    G.abilities = AB_DOUBLE | AB_CLAWS | AB_DASH;
    int before = G.secrets;
    for (int i = 1; i < G.nplayers; i++) G.p[i].joined = false;
    player *p = &G.p[0];
    float sx = wet ? cx : (float)crack_x - 3.0f, sy = wet ? G.L.ground[ix] + 12.0f : (float)G.L.ground[crack_x - 3 < 0 ? 0 : crack_x - 3] + 0.05f;
    if (wet) {
      int top = iy;
      while (top < WORLD_H - 1 && G.L.t[top + 1][ix] == T_WATER) top++;
      sy = (float)top + 2.5f; /* drop in from above the surface */
    }
    cw_platformer2d_teleport(&p->pf, (cw_pos2){ (double)sx, (double)sy });
    int end_t = 0;
    for (int t = 0; t < 900; t++) {
      memset(INPUT, 0, sizeof INPUT);
      if (!wet) {
        INPUT[0].x = 1;
        if (t == 20) INPUT[0].dash_pressed = true;
        if (t > 60) INPUT[0].x = t < 150 ? 1.0f : 0.0f;
      } else {
        /* dive to the nearest coin still there, then up to the surface */
        int tgt = -1;
        float bestd = 1e9f;
        for (int k = c0; k <= c1; k++) {
          if (G.L.coins[k].taken) continue;
          float ddx = G.L.coins[k].x - (float)p->pf.body.position.x, ddy = G.L.coins[k].y - ((float)p->pf.body.position.y + 0.7f);
          if (ddx * ddx + ddy * ddy < bestd) bestd = ddx * ddx + ddy * ddy, tgt = k;
        }
        if (tgt >= 0) {
          float ddx = G.L.coins[tgt].x - (float)p->pf.body.position.x, ddy = G.L.coins[tgt].y - ((float)p->pf.body.position.y + 0.7f);
          INPUT[0].x = ddx > 0.2f ? 1.0f : ddx < -0.2f ? -1.0f : 0.0f;
          INPUT[0].y = ddy > 0.25f ? 1.0f : ddy < -0.25f ? -1.0f : 0.0f;
          INPUT[0].jump_pressed = ddy > 0.25f && (t % 10) == 0;
        } else {
          INPUT[0].y = 1.0f;
          INPUT[0].jump_pressed = (t % 12) == 0;
        }
      }
      sim_step(&G, INPUT, 1.0f / 60.0f);
      end_t = t;
      if (getenv("COOP_TRACE") && t >= 15 && t < 30) printf("  t=%d x=%.2f y=%.2f vx=%.1f dash_t=%.2f ev=%x gev=%x tile=%d can=%d ab=%u\n", t, p->pf.body.position.x, p->pf.body.position.y, p->pf.body.velocity.x, p->pf.dash_t, p->pf.events, G.events, G.L.t[22][68], p->pf.can_dash, G.abilities);
      if (!p->alive) break;
    }
    int got = G.secrets - before, want = c1 - c0 + 1;
    int stop = iy;
    while (stop < WORLD_H - 1 && G.L.t[stop + 1][ix] == T_WATER) stop++;
    bool out = !wet || p->pf.body.position.y > (float)stop - 0.6f; /* back at the surface */
    printf("  start %.1f,%.1f end %.2f,%.2f crack_x=%d\n", sx, sy, p->pf.body.position.x, p->pf.body.position.y, crack_x);
    printf("secret group at %.0f,%.0f (%s): %d of %d coins%s, ended t=%d%s\n", cx, cy, wet ? "swim" : "dash through the crack", got, want, wet ? (out ? ", back out" : ", NOT back out") : "", end_t, p->alive ? "" : " (died)");
    if (got == want && out && p->alive) ok++;
  }
  printf("secret routes: %d of %d scripted groups pass\n", ok, groups);
  fflush(stdout);
  exit(ok == groups ? 0 : 1);
}

/* COOP_FUZZ=<runs>: sequence-break check. For each ability gate the player starts at the flag before it with ONLY the
 * abilities found before it and plays random input for 30 seconds: crossing the gate alive is a break. */
static void fuzz(int runs, int players) {
  soak_reset(players);
  for (int gi = 0; gi < G.L.ngates; gi++) {
    const gate gt = G.L.gates[gi];
    uint32_t have = 0;
    for (int k = 0; k < G.L.npickups; k++)
      if (G.L.pickups[k].x < gt.x0 && G.L.pickups[k].ability != gt.need) have |= (uint32_t)G.L.pickups[k].ability; /* all but the one the gate asks for */
    for (int pass = 0; pass < 2; pass++) {
    int broke = 0;
    float far = 0;
    for (int r = 0; r < runs; r++) {
      soak_reset(players);
      G.abilities = pass ? (have | (uint32_t)gt.need) : have; /* pass 1 is the control: with the ability the gate must be crossable */
      for (int k = 0; k < G.L.npickups; k++) G.L.pickups[k].taken = true; /* no pedestal may hand out the ability on the way */
      G.reached |= 1u << gt.flag;
      sim_warp(&G, gt.flag);
      int hold = 0, mx = 1, jh = 0;
      bool crossed = false;
      static float px[1800], py[1800];
      for (int t = 0; t < 1800 && !crossed; t++) {
        memset(INPUT, 0, sizeof INPUT);
        if (hold-- <= 0) hold = 20 + (int)(frand() % 40), mx = (frand() % 100) < 70 ? 1 : (frand() % 2 ? 0 : -1);
        INPUT[0].x = (float)mx;
        if (frand() % 100 < 5) INPUT[0].jump_pressed = true, jh = 10 + (int)(frand() % 30);
        INPUT[0].jump_held = jh-- > 0;
        if (frand() % 100 < 2) INPUT[0].dash_pressed = true;
        INPUT[0].down = frand() % 100 < 4;
        sim_step(&G, INPUT, 1.0f / 60.0f);
        float x = (float)G.p[0].pf.body.position.x;
        px[t] = x, py[t] = (float)G.p[0].pf.body.position.y;
        if (G.p[0].alive && x > far) far = x;
        if (getenv("COOP_TRACE") && gi == atoi(getenv("COOP_TRACE")) && r == (getenv("COOP_FUZZ_R") ? atoi(getenv("COOP_FUZZ_R")) : 0) && pass == 0 && (t % 4) == 0) printf("  r0 t=%d x=%.1f y=%.1f vy=%.1f fl=%d alive=%d bub=%d ab=%u\n", t, x, (double)G.p[0].pf.body.position.y, G.p[0].pf.body.velocity.y, G.p[0].pf.body.on_floor, G.p[0].alive, G.p[0].bubble, G.abilities);
        if (G.p[0].alive && !G.p[0].bubble && x > (float)gt.x1 + 2.0f) {
          crossed = true;
          if (getenv("COOP_TRACE") && pass == 0 && broke < 1 && gi == atoi(getenv("COOP_TRACE")))
            for (int q = (t > 300 ? t - 300 : 0); q <= t; q += 8) printf("    path t=%d %.1f,%.1f\n", q, px[q], py[q]);
          if (getenv("COOP_TRACE") && pass == 0 && broke < 3) printf("  crossing run %d t=%d at %.1f,%.1f on_floor=%d bubble_rescues=%d deaths=%d\n", r, t, x, (double)G.p[0].pf.body.position.y, G.p[0].pf.body.on_floor, G.rescues, G.deaths);
        }
      }
      broke += crossed;
    }
    printf("fuzz gate %d (x %d..%d, needs %u) %s: %d of %d runs crossed alive, furthest %.1f\n", gi, gt.x0, gt.x1, (unsigned)gt.need, pass ? "WITH it" : "without it", broke, runs, far);
    }
  }
  fflush(stdout);
  exit(0);
}

/* COOP_MAP=<x0>: print the level 48 columns from x0 (and its flags and gates) and exit */
static void print_map(int x0) {
  sim_reset(&G, 1);
  for (int y = WORLD_H - 1; y >= 0; y--) {
    printf("%2d ", y);
    for (int x = x0; x < x0 + 60 && x < WORLD_W; x++) {
      int c = G.L.t[y][x];
      putchar(c == T_AIR ? '.' : c == T_ROCK ? '#' : c == T_GRASS ? 'g' : c == T_ONEWAY ? '-' : c == T_SPIKE ? '^' : c == T_LAVA ? 'L' : c == T_WATER ? '~' : c == T_CRACK ? 'x' : c == T_BRICK ? 'b' : '?');
    }
    putchar('\n');
  }
  printf("   ");
  for (int x = x0; x < x0 + 60 && x < WORLD_W; x++) putchar('0' + G.L.biome[x]); /* the region of each column */
  putchar('\n');
  for (int k = 0; k < G.L.nthings; k++) if (G.L.things[k].x >= (float)x0 && G.L.things[k].x < (float)x0 + 60) printf("thing %d kind %d at %.1f,%.1f a=%.1f b=%.1f\n", k, G.L.things[k].kind, (double)G.L.things[k].x, (double)G.L.things[k].y, (double)G.L.things[k].a, (double)G.L.things[k].b);
  printf("width %d coins %d enemies %d things %d signs %d flags %d\n", G.L.width, G.L.ncoins, G.L.nenemies, G.L.nthings, G.L.nsigns, G.L.nchecks);
  for (int k = 0; k < G.L.nchecks; k++) printf("flag %d at %d,%d\n", k, G.L.checks[k].x, G.L.checks[k].y);
  for (int k = 0; k < G.L.ngates; k++) printf("gate %d x %d..%d needs %d flag %d\n", k, G.L.gates[k].x0, G.L.gates[k].x1, G.L.gates[k].need, G.L.gates[k].flag);
  for (int k = 0; k < G.L.nenemies; k++) printf("enemy %d kind %d at %.1f,%.1f\n", k, G.L.enemies[k].kind, (double)G.L.enemies[k].x, (double)G.L.enemies[k].y);
  for (int k = 0; k < G.L.ncoins; k++) if (G.L.coins[k].medal) printf("big coin %d at %.1f,%.1f\n", G.L.coins[k].medal, (double)G.L.coins[k].x, (double)G.L.coins[k].y);
  for (int k = 0; k < G.L.npickups; k++) printf("pickup %d at %d,%d ability %d\n", k, G.L.pickups[k].x, G.L.pickups[k].y, G.L.pickups[k].ability);
  fflush(stdout);
  exit(0);
}

/* COOP_SOAK=<ticks>: run the bots through the whole game with no rendering and report (soft-lock and crash check):
 * wins, the longest stretch with no progress, deaths per 16-column screen, the fastest win and a state hash. */
static void soak(int ticks, int players) {
  soak_reset(players);
  float best = 0;
  int last_progress = 0, longest_stall = 0, wins = 0, resets = 0, win_best = 0, run_start = 0;
  bool dumped = false;
  int boss_seen = 9;
  int death_map[WORLD_W / 16 + 1];
  memset(death_map, 0, sizeof death_map);
  bool was_alive[MAX_PLAYERS];
  for (int i = 0; i < MAX_PLAYERS; i++) was_alive[i] = true;
  /* COOP_IDLE_SEATS=n: the last n seats are joined and never touched (a pad put down); COOP_IDLE_ANCHOR=1 is the control,
   * those seats holding the group back as the game once let them */
  int idle_seats = getenv("COOP_IDLE_SEATS") ? atoi(getenv("COOP_IDLE_SEATS")) : 0;
  G.idle_anchor = getenv("COOP_IDLE_ANCHOR") != NULL;
  for (int t = 0; t < ticks; t++) {
    for (int i = 0; i < MAX_PLAYERS; i++) sim_bot_input(&G, i, &INPUT[i]);
    for (int i = players - idle_seats; i < players; i++) if (i >= 0) memset(&INPUT[i], 0, sizeof INPUT[i]);
    sim_step(&G, INPUT, 1.0f / 60.0f);
    for (int i = 0; i < G.nplayers; i++) {
      float x = (float)G.p[i].pf.body.position.x, y = (float)G.p[i].pf.body.position.y;
      if (x != x || y != y || x < -5 || x > WORLD_W + 5) {
        printf("soak BAD POSITION t=%d p%d (%f,%f)\n", t, i, x, y);
        exit(2);
      }
      if (was_alive[i] && !G.p[i].alive) {
        death_map[(int)(x < 0 ? 0 : x) / 16]++;
        if (getenv("COOP_DEATHS")) printf("death t=%d p%d at %.1f,%.1f vel %.1f,%.1f tile=%d\n", t, i, x, y, G.p[i].pf.body.velocity.x, G.p[i].pf.body.velocity.y, G.L.t[(int)y][(int)x]);
      }
      was_alive[i] = G.p[i].alive;
    }
    if (getenv("COOP_TRACE") && t >= atoi(getenv("COOP_TRACE")) && t < atoi(getenv("COOP_TRACE")) + 90) {
      int tp = getenv("COOP_TRACE_P") ? atoi(getenv("COOP_TRACE_P")) : 0;
      const player *q = &G.p[tp];
      printf("tr t=%d pos=%.2f,%.2f vel=%.2f,%.2f fl=%d wl=%d wet=%.1f in=(%.0f,%.0f,j%d%d,d%d) dr=%d dcd=%.2f al=%d ev=%x alive=%d bub=%d ab=%u\n", t, q->pf.body.position.x, q->pf.body.position.y, q->pf.body.velocity.x, q->pf.body.velocity.y, q->pf.body.on_floor, q->pf.body.on_wall, q->pf.submerged, INPUT[tp].x, INPUT[tp].y, INPUT[tp].jump_pressed, INPUT[tp].jump_held, INPUT[tp].dash_pressed, q->pf.dash_ready, q->pf.dash_cd, q->pf.air_jumps_left, q->pf.events, q->alive, q->bubble, G.abilities);
    }
    float lead = -1;
    for (int i = 0; i < G.nplayers; i++) {
      float x = (float)G.p[i].pf.body.position.x;
      if (x > lead) lead = x;
    }
    if (lead > best + 0.5f) best = lead, last_progress = t;
    if (G.boss_hp != boss_seen) boss_seen = G.boss_hp, last_progress = t; /* a boss fight is progress */
    if (t - last_progress > longest_stall) longest_stall = t - last_progress;
    if (t % 3600 == 0) printf("soak t=%d lead=%.1f best=%.1f coins=%d deaths=%d rescues=%d abil=%u ck=%d boss=%d won=%d\n", t, lead, best, G.coins_total, G.deaths, G.rescues, G.abilities, G.checkpoint, G.boss_hp, G.won);
    if (G.won && G.tick - G.win_tick > 300) {
      wins++;
      int took = t - run_start;
      if (!win_best || took < win_best) win_best = took;
      printf("soak WIN at t=%d (run %d took %d ticks = %.1f min)\n", t, wins, took, (double)took / 3600.0);
      soak_reset(players);
      for (int i = 0; i < MAX_PLAYERS; i++) was_alive[i] = true;
      best = 0, last_progress = t, run_start = t, resets++, boss_seen = 9;
    }
    if (t - last_progress > 1800) { /* 30 s of nothing: a player holds Retry */
      printf("soak STALL at t=%d x=%.1f: returning the group to the flag (the Retry rule)\n", t, best);
      for (int i = 0; i < G.nplayers; i++) printf("  p%d pos=%.2f,%.2f vel=%.2f,%.2f floor=%d wall=%d wet=%.1f alive=%d bubble=%d\n", i, G.p[i].pf.body.position.x, G.p[i].pf.body.position.y, G.p[i].pf.body.velocity.x, G.p[i].pf.body.velocity.y, G.p[i].pf.body.on_floor, G.p[i].pf.body.on_wall, G.p[i].pf.submerged, G.p[i].alive, G.p[i].bubble);
      if (getenv("COOP_DUMP") && resets == 0 && !dumped) {
        dumped = true;
        int x0 = (int)best - 20, yc = G.L.ground[(int)best < WORLD_W ? (int)best : WORLD_W - 1];
        for (int y = yc + 14; y >= yc - 12 && y >= 0; y--) {
          printf("%2d ", y);
          for (int x = x0 < 0 ? 0 : x0; x < x0 + 50 && x < WORLD_W; x++) {
            int c = G.L.t[y][x];
            char ch = c == T_AIR ? '.' : c == T_ROCK ? '#' : c == T_GRASS ? 'g' : c == T_ONEWAY ? '-' : c == T_SPIKE ? '^' : c == T_LAVA ? 'L' : c == T_WATER ? '~' : c == T_CRACK ? 'x' : c == T_BRICK ? 'b' : '?';
            for (int i = 0; i < G.nplayers; i++)
              if ((int)G.p[i].pf.body.position.x == x && (int)G.p[i].pf.body.position.y == y) ch = (char)('0' + i);
            putchar(ch);
          }
          putchar('\n');
        }
        printf("x0=%d\n", x0);
      }
      sim_return(&G); /* what a player does: hold Retry (the rule is tested in sim.c) */
      last_progress = t;
    }
  }
  int tot = 0, mx = 0, nz[WORLD_W / 16 + 1], nn = 0;
  for (int k = 0; k <= WORLD_W / 16; k++) {
    tot += death_map[k];
    if (death_map[k] > mx) mx = death_map[k];
    nz[nn++] = death_map[k];
  }
  for (int a = 0; a < nn; a++) /* sort for the median */
    for (int b = a + 1; b < nn; b++)
      if (nz[b] < nz[a]) {
        int tmp = nz[a];
        nz[a] = nz[b];
        nz[b] = tmp;
      }
  printf("deaths per 16-column screen:");
  for (int k = 0; k <= WORLD_W / 16; k++) printf(" %d", death_map[k]);
  printf("\n  total %d, median %d, max %d\n", tot, nz[nn / 2], mx);
  printf("soak done ticks=%d wins=%d resets=%d best=%.1f longest_stall=%d fastest_win=%d deaths=%d rescues=%d secrets=%d hash=%u\n", ticks, wins, resets, best, longest_stall, win_best, G.deaths, G.rescues, G.secrets, G.hash);
  fflush(stdout);
  exit(0);
}
#endif

#ifdef COOP_AUTOPILOT
/* debug channel: "warp <column>" puts every player there, what dev.warp does on a host that cannot write a field (Chromium) */
static void cmd_warp(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: warp <column>"); return; }
  g_dev_warp = atoi(argv[1]);
  cw_reply_printf(r, "{\"warp\":%d}", g_dev_warp);
}
#endif
/* the idle-seat gate's switch: "sw idle_anchor 1" makes a seat nobody plays hold the group back, as the game once did */
static void cmd_sw(int argc, const char **argv, cw_reply *r) {
  if (argc < 3 || strcmp(argv[1], "idle_anchor")) { cw_reply_error(r, "usage: sw idle_anchor <0|1>"); return; }
  G.idle_anchor = atoi(argv[2]) != 0;
  cw_reply_printf(r, "{\"idle_anchor\":%d}", G.idle_anchor);
}
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

/* A save from version 4 (before the big coins): the shell starts over on any other game version or size, settings and
 * all, so a version 4 save is carried forward here, before cw_shell_init reads it. Its game data is save_t up to
 * `medals`; the big coins start empty. Only a save whose shell header and checksum read exactly as the shell writes
 * them (magic, shell version 1, game version 4, that size, FNV-1a from the settings to the end) is touched: any other
 * block is left for the shell to judge. COOP_SAVE_FIXTURE (native) loads a save file in place of the stored one, as
 * the save gate does; COOP_SAVE_NO_MIGRATE is its control. */
static int g_migrate_dbg[9]; /* migrated, level, unlocked, coins, abilities, total deaths, hats, shake setting, music volume; then big coins taken, in g.migrate_coins */
static int g_migrate_coins;
static uint32_t shell_sum(const uint8_t *b, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 16; i < n; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}
static bool save_migrate_v4(void) {
  uint8_t *b = cw_shell_save_region();
  size_t n = cw_shell_save_region_size();
  const size_t v4_size = offsetof(save_t, medals), game_at = 32;
  if (n != game_at + CW_SHELL_GAME_SAVE_MAX) return false; /* not the shell layout this was written for */
  uint32_t magic, size, sum;
  uint16_t ver, gver;
  memcpy(&magic, b, 4), memcpy(&ver, b + 4, 2), memcpy(&gver, b + 6, 2), memcpy(&size, b + 8, 4), memcpy(&sum, b + 12, 4);
  if (magic != 0x4c485357u || ver != 1 || gver != 4 || size != v4_size || sum != shell_sum(b, n)) return false;
  memset(b + game_at + v4_size, 0, sizeof(save_t) - v4_size); /* no big coins yet */
  gver = 5, size = (uint32_t)sizeof(save_t);
  memcpy(b + 6, &gver, 2), memcpy(b + 8, &size, 4);
  sum = shell_sum(b, n);
  memcpy(b + 12, &sum, 4);
  return true;
}

static void init(void *user) {
  (void)user;
  /* touch: a stick and three buttons, made before the bindings that name them (ids 0 to 3, in this order) */
  cw_touch_add_thumbstick(NULL);
  cw_touch_add_round_button(&(cw_touch_button_desc){ .x = 90, .y = 110, .radius = 56, .label = "A" });
  cw_touch_add_round_button(&(cw_touch_button_desc){ .x = 210, .y = 90, .radius = 50, .label = "RUN" });
  cw_touch_add_round_button(&(cw_touch_button_desc){ .x = 130, .y = 230, .radius = 42, .label = "DASH" });
  cw_touch_add_round_button(&(cw_touch_button_desc){ .x = 250, .y = 210, .radius = 42, .label = "GRAB" });
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("coopplatformer: input.cfg had errors");
  cw_input_commit_defaults();
  a_left = cw_action_find("left"), a_right = cw_action_find("right"), a_up = cw_action_find("up"), a_down = cw_action_find("down");
  a_jump = cw_action_find("jump"), a_run = cw_action_find("run"), a_grab = cw_action_find("grab"), a_dash = cw_action_find("dash"), a_map = cw_action_find("map"), a_retry = cw_action_find("retry");
  {
    const char *fx = getenv("COOP_SAVE_FIXTURE");
    FILE *fp = fx && fx[0] ? fopen(fx, "rb") : NULL;
    if (fp) {
      memset(cw_shell_save_region(), 0, cw_shell_save_region_size());
      size_t got = fread(cw_shell_save_region(), 1, cw_shell_save_region_size(), fp);
      fclose(fp);
      cw_info("coopplatformer: save fixture %s, %zu bytes", fx, got);
    }
    if (!getenv("COOP_SAVE_NO_MIGRATE") && save_migrate_v4()) g_migrate_dbg[0] = 1, cw_info("coopplatformer: a version 4 save carried forward");
  }
  cw_shell_init(&(cw_shell_desc){ .title = "Co-op Platformer", .subtitle = "one to four players", .max_players = 4,
                                  .game_save_size = sizeof(save_t), .game_save_version = 5, .start = start_match,
                                  .quit = quit_match, .sound = sound_hook, .draw_background = title_background });
  g_font = cw_ui_font_sans();
  audio_init();
  {
    const save_t *sv = cw_shell_game_save();
    g_migrate_dbg[1] = sv->level, g_migrate_dbg[2] = sv->unlocked, g_migrate_dbg[3] = sv->coins, g_migrate_dbg[4] = (int)sv->abilities;
    g_migrate_dbg[5] = sv->total_deaths, g_migrate_dbg[6] = sv->hats, g_migrate_dbg[7] = (int)cw_shell_shake_level();
    g_migrate_dbg[8] = ((const uint8_t *)cw_shell_save_region())[17];
    for (int l = 0; l < 32; l++) g_migrate_coins += (sv->medals[l] & 1) + ((sv->medals[l] >> 1) & 1) + ((sv->medals[l] >> 2) & 1);
    cw_debug_field("g.migrate", g_migrate_dbg, CW_DBG_I32, 9);
    cw_debug_i32("g.migrate_coins", &g_migrate_coins);
  }
  cw_debug_field("join.seats", join_seats, CW_DBG_I32, 4);
  cw_debug_command("bind", "bind <action> <bindings>: replace an action's bindings", cmd_bind);
  cw_debug_command("sw", "sw idle_anchor <0|1>: a test switch (1: a seat nobody plays holds the group back, as before)", cmd_sw);
  cw_debug_i32("g.carried", &G.carried);
  cw_debug_field("g.idle", g_idle_dbg, CW_DBG_I32, MAX_PLAYERS);
#ifdef COOP_AUTOPILOT
  cw_debug_command("warp", "warp <column>: every player there, with every ability", cmd_warp);
#endif
  cw_debug_i32("g.state", &g_state_dbg);
  cw_debug_field("g.scenery", g_scenery_dbg, CW_DBG_I32, 2);
  cw_debug_i32("g.scenery_off", &g_scenery_dbg[1]); /* props off their ground. Not the count of props: the backdrop's are seated by float maths, and a native libm and a cart's seat a few differently */
  cw_debug_i32("g.players", &g_players_dbg);
  cw_debug_i32("g.tick", &G.tick);
  cw_debug_u32("g.abilities", &G.abilities);
  cw_debug_i32("g.coins", &G.coins_total);
  cw_debug_i32("g.kills", &G.kills);
  cw_debug_i32("g.deaths", &G.deaths);
  cw_debug_i32("g.rescues", &G.rescues);
  cw_debug_i32("g.secrets", &G.secrets);
  cw_debug_i32("g.checkpoint", &G.checkpoint);
  cw_debug_i32("g.boss_hp", &G.boss_hp);
  cw_debug_i32("g.won", &G.won);
  cw_debug_u32("g.reached", &G.reached);
  cw_debug_i32("g.hitstops", &G.hitstops);
  cw_debug_u32("g.hash", &G.hash);
  cw_debug_f32_array("g.pos", g_pos_dbg, 2);
  cw_debug_f32_array("g.ppos", g_ppos_dbg, MAX_PLAYERS * 2); /* every player, x y (0 0 when not joined) */
  cw_debug_f32_array("g.cam", g_cam_dbg, 3);
  cw_debug_f32_array("g.view", g_view_dbg, 4);
  cw_debug_field("g.camgate", g_camgate_dbg, CW_DBG_I32, 4);
  cw_debug_f32_array("g.camjit", g_camjit_dbg, 4);
  cw_debug_field("g.camoffset", g_camoffset_dbg, CW_DBG_I32, 3);
  cw_debug_i32("g.impacts", &G.impacts);
  cw_debug_field("rumble.sent", g_rumble_sent_dbg, CW_DBG_I32, MAX_PLAYERS);
  cw_debug_i32("g.hp", &g_hp_dbg);
  cw_debug_i32("g.bubbles", &g_bubbles_dbg);
  cw_debug_i32("g.map_seen", &g_map_seen_dbg);
  cw_debug_i32("dev.warp", &g_dev_warp);
  cw_debug_i32("dev.level", &g_dev_level);
  cw_debug_i32("dev.worlds", &g_dev_worlds);
  cw_debug_field("g.flames", g_flames_dbg, CW_DBG_I32, 2);
  cw_debug_f32("g.viewbot", &g_viewbot_dbg);
  cw_debug_field("g.underview", g_underview_dbg, CW_DBG_I32, 2);
  cw_debug_field("g.tags", g_tags_dbg, CW_DBG_I32, 2);
  cw_debug_field("g.signgate", g_sign_dbg, CW_DBG_I32, 3);
  cw_debug_field("g.cardgate", g_card_dbg, CW_DBG_I32, 3);
  cw_debug_field("g.endwall", g_endwall_dbg, CW_DBG_I32, 3);
  cw_debug_field("g.bossclip", g_bossclip_dbg, CW_DBG_I32, 3);
  cw_debug_field("g.labelgate", g_label_dbg, CW_DBG_I32, 4);
  cw_debug_field("g.wordsgate", g_words_dbg, CW_DBG_I32, 3);
  cw_debug_field("g.signmove", g_signmove_dbg, CW_DBG_I32, 2);
  cw_debug_field("g.labelmove", g_labelmove_dbg, CW_DBG_I32, 2);
  cw_debug_field("g.bossgate", g_boss_dbg, CW_DBG_I32, 4);
  cw_debug_field("g.surface", g_surface_dbg, CW_DBG_I32, 3);
  cw_debug_f32_array("g.jump", G.last_jump, 3); /* the last jump to land: distance, rise, speed at takeoff */
  cw_debug_f32("g.top_speed", &G.top_speed);
  cw_debug_field("g.things", g_things_dbg, CW_DBG_I32, 2);
  cw_debug_field("g.level", g_level_dbg, CW_DBG_I32, 2);
  cw_debug_field("g.gore", g_gore_dbg, CW_DBG_I32, 3);
  cw_debug_field("g.music", g_music_dbg, CW_DBG_I32, 3);
  cw_debug_i32("g.things_hit", &G.things_hit);
  cw_debug_field("g.pitview", g_pitview_dbg, CW_DBG_I32, 2);
  cw_debug_field("g.nearcover", g_nearcover_dbg, CW_DBG_I32, 2);
  cw_debug_field("g.throws", G.throws, CW_DBG_I32, MAX_PLAYERS);
  cw_debug_i32("g.bites", &G.mimic_bites);
  cw_debug_field("g.graves", g_graves_dbg, CW_DBG_I32, 2);
  cw_debug_field("g.betrayals", G.betrayals, CW_DBG_I32, MAX_PLAYERS);
  cw_debug_i32("g.width", &G.L.width);
  cw_debug_i32("g.vanish", &g_vanish_dbg);
  cw_debug_field("g.blobs", g_blob_dbg, CW_DBG_I32, 3);
  cw_debug_i32("g.charshadow", &g_charshadow_dbg);
  cw_debug_i32("g.rim", &g_rim_dbg);
  cw_debug_field("g.hint", g_hint_dbg, CW_DBG_I32, 2);
  g_shadow_old = getenv("COOP_SHADOW_OLD") != NULL; /* the shadow gate's control (native only) */
  g_rim_off = getenv("COOP_RIM_OFF") != NULL; /* the rim gate's control (native only) */
  g_tally_off = getenv("COOP_TALLY_OFF") != NULL; /* the tally check's control (native only) */
  cw_debug_field("g.tally", g_tally_dbg, CW_DBG_I32, 2);
  cw_debug_field("g.coinface", g_coinface_dbg, CW_DBG_I32, 2);
  cw_debug_i32("g.uvrep", &g_uvrep_dbg);
  cw_debug_i32("g.marker_over", &g_marker_over_dbg);
  cur_mood = MOODS[0];
#ifdef COOP_AUTOPILOT
  G.level = getenv("COOP_LEVEL") ? atoi(getenv("COOP_LEVEL")) : 0;
  g_bot_no_run = getenv("COOP_BOT_NO_RUN") != NULL;
  if (getenv("COOP_MAP")) print_map(atoi(getenv("COOP_MAP")));
  if (getenv("COOP_REACH_COUNT")) { printf("levels %d\n", NLEVELS); fflush(stdout); exit(0); }
  if (getenv("COOP_THINGS")) { int nn = 0; G.abilities = 0; sim_reset(&G, 1); int bad = world_things_check(&G.L, &nn); printf("things %d off the ground %d\n", nn, bad); fflush(stdout); exit(bad ? 1 : 0); }
  if (getenv("COOP_REACH")) exit(reach_check());
  if (getenv("COOP_SECRETS")) secret_routes(1);
  if (getenv("COOP_FUZZ")) fuzz(atoi(getenv("COOP_FUZZ")), getenv("COOP_PLAYERS") ? atoi(getenv("COOP_PLAYERS")) : 1);
  if (getenv("COOP_SOAK")) soak(atoi(getenv("COOP_SOAK")), getenv("COOP_PLAYERS") ? atoi(getenv("COOP_PLAYERS")) : 2);
  int n = cfg_int("COOP_PLAYERS", COOP_DEF_PLAYERS);
  cw_shell_start(n < 1 ? 1 : n > 4 ? 4 : n);
#endif
}

static void grief_gag(int who, int victim);
static void save_progress(void) {
  save_t *sv = cw_shell_game_save();
  sv->level = G.level;
  if (G.level > sv->unlocked) sv->unlocked = G.level;
  sv->abilities |= G.abilities;
  sv->reached |= G.reached;
  if (G.coins_total > sv->coins) sv->coins = G.coins_total;
  if (G.level >= 0 && G.level < 32) sv->medals[G.level] |= (uint8_t)G.medals;
  for (int y = 0; y < WORLD_H / 4; y++)
    for (int x = 0; x < WORLD_W / 4; x++) sv->seen[y][x] |= G.seen[y][x];
  if (G.won && (sv->best_ticks == 0 || G.win_tick < sv->best_ticks)) sv->best_ticks = G.win_tick;
  cw_shell_save_commit();
}

static void play_events(void) {
  for (int i = 0; i < G.ndeaths_q; i++) gore_death(G.deaths_q[i].who, G.deaths_q[i].cause, G.deaths_q[i].x, G.deaths_q[i].y);
  for (int i = 0; i < G.ngrief_q; i++) grief_gag(G.grief_q[i].who, G.grief_q[i].victim);
  int e = G.events;
  if (!e) return;
  static const int MAP[][2] = { { 1, SFX_JUMP }, { 2, SFX_LAND }, { 4, SFX_COIN }, { 8, SFX_HURT }, { 16, SFX_SPLASH }, { 32, SFX_DASH },
                                { 64, SFX_POWER }, { 128, SFX_STOMP }, { 256, SFX_BREAK }, { 512, SFX_BUBBLE }, { 1024, SFX_POP },
                                { 2048, SFX_DIE }, { 4096, SFX_WIN }, { 8192, SFX_CHECK }, { 16384, SFX_WALL } };
  /* coins climb a scale while you keep getting them, and start again when you stop */
  static int coin_tick, coin_run;
  if (e & 4) {
    coin_run = G.tick - coin_tick < 45 ? coin_run + 1 : 0;
    coin_tick = G.tick;
    if (A.on) cw_audio_play(A.sfx[SFX_COIN], &(cw_play_desc){ .volume = 0.7f, .pitch = powf(2.0f, (float)(coin_run < 14 ? coin_run : 14) * 2.0f / 12.0f) });
  }
  for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++)
    if ((e & MAP[i][0]) && MAP[i][1] != SFX_COIN) sfx(MAP[i][1], 0.7f);
  if (e & 4) burst(S.fx_spark, (float)G.p[0].pf.body.position.x, (float)G.p[0].pf.body.position.y + 1, 0.5f);
  if (e & 128) burst(S.fx_hit, (float)G.p[0].pf.body.position.x, (float)G.p[0].pf.body.position.y, 0.5f);
  if (e & 64) save_progress();
  { /* a big coin: the power chime and the save (the map counts them) */
    static int last_medals;
    if (G.medals != last_medals) {
      if (G.medals & ~last_medals) sfx(SFX_POWER, 0.8f);
      last_medals = G.medals;
      save_progress();
    }
  }
  if (e & 8192) save_progress();
  if (e & 256) burst(S.fx_burst, (float)G.p[0].pf.body.position.x + 1, (float)G.p[0].pf.body.position.y + 1, 0.5f);
  if (e & 4096) {
    save_progress();
    burst(S.fx_burst, G.cam.x, G.cam.y, 0.5f);
  }
}

/* the map is also the shortcut: left and right pick a flag already touched, Jump travels there (everyone goes) */
static void map_cursor(void) {
  int n = G.L.nchecks, dir = 0;
  if (cw_action_just_pressed(0, a_left)) dir = -1;
  if (cw_action_just_pressed(0, a_right)) dir = 1;
  if (dir && n) {
    int k = g_map_sel < 0 ? (dir > 0 ? -1 : n) : g_map_sel;
    for (int tries = 0; tries < n; tries++) {
      k = (k + dir + n) % n;
      if (G.reached & (1u << k)) {
        g_map_sel = k;
        break;
      }
    }
    sfx(SFX_CHECK, 0.3f);
  }
  /* up and down pick a level already reached (replaying one is allowed; nobody is watching) */
  const save_t *sv = cw_shell_game_save();
  if (g_map_level < 0) g_map_level = G.level;
  if (cw_action_just_pressed(0, a_up) && g_map_level < sv->unlocked && g_map_level < NLEVELS - 1) g_map_level++, sfx(SFX_CHECK, 0.3f);
  if (cw_action_just_pressed(0, a_down) && g_map_level > 0) g_map_level--, sfx(SFX_CHECK, 0.3f);
  bool jump = cw_action_just_pressed(0, a_jump);
  memset(&INPUT[0], 0, sizeof INPUT[0]); /* the cursor is not a step */
  if (jump && g_map_level != G.level) {
    g_dev_level_keep = g_map_level;
    g_map_open = false;
    g_map_level = -1;
    sfx(SFX_POP, 0.6f);
  } else if (jump && g_map_sel >= 0 && sim_warp(&G, g_map_sel)) {
    g_map_open = false;
    g_map_level = -1;
    sfx(SFX_POP, 0.6f);
  }
}

/* this tick's rumble to each player's pad (the sim asks, sim_rumble; nothing reads it back). A request on top of one still
 * running keeps the stronger of each motor and the longer time; the motors stop on their own after ms */
static struct { int until[MAX_PLAYERS]; float low[MAX_PLAYERS], high[MAX_PLAYERS]; int clock; } RB;
static void send_rumble(void) {
  RB.clock++;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    float low = G.rumble[i].low, high = G.rumble[i].high;
    int ms = G.rumble[i].ms, left = (RB.until[i] - RB.clock) * 1000 / 60;
    G.rumble[i].low = G.rumble[i].high = 0, G.rumble[i].ms = 0;
    if (ms <= 0 || i >= G.nplayers) continue;
    if (left > 0) {
      if (low <= RB.low[i] && high <= RB.high[i] && ms <= left) continue;
      low = fmaxf(low, RB.low[i]), high = fmaxf(high, RB.high[i]), ms = ms > left ? ms : left;
    }
    low = fminf(low, 1), high = fminf(high, 1);
    RB.low[i] = low, RB.high[i] = high, RB.until[i] = RB.clock + (ms * 60 + 999) / 1000;
    g_rumble_sent_dbg[i]++;
    cw_input_rumble(i, low, high, ms);
  }
}
static void rumble_stop_all(void) {
  for (int i = 0; i < MAX_PLAYERS; i++) if (RB.until[i] > RB.clock) cw_input_rumble_stop(i), RB.until[i] = 0;
}

static void next_level(void);
static void music_tick(void);
static void fixed_update(void *user, double dt) {
  (void)user;
  bool run = cw_shell_update();
  g_state_dbg = (int)cw_shell_screen_now(); /* even when the shell owns the tick */
  if (!run) { rumble_stop_all(); return; } /* the menus and pause: the motors stop */
  if (g_attract) return; /* the match has not started yet */
  if (G.nplayers == 0 || !G.world.id) return;
  g_run_ticks++;
  if (g_dev_worlds > 0) levels_set_worlds(g_dev_worlds), g_dev_worlds = 0;
  if (g_dev_level >= 0) {
    int lv = g_dev_level >= NLEVELS ? NLEVELS - 1 : g_dev_level, players = G.nplayers;
    g_dev_level = -1;
    G.level = lv;
    G.abilities = LEVELS[lv].abilities;
    sim_reset(&G, players);
    G.abilities = AB_DOUBLE | AB_CLAWS | AB_DASH;
    build_scene();
    camgate_reset();
    music_for_level();
  }
  if (g_dev_level_keep >= 0) {
    int lv = g_dev_level_keep, players = G.nplayers;
    uint32_t ab = G.abilities;
    g_dev_level_keep = -1;
    G.level = lv;
    sim_reset(&G, players);
    G.abilities = ab | LEVELS[lv].abilities;
    { save_t *sv = cw_shell_game_save(); sv->reached = 0; memset(sv->seen, 0, sizeof sv->seen); }
    save_progress();
    build_scene();
    camgate_reset();
    music_for_level();
  }
  if (g_dev_warp >= 0) {
    int sx = g_dev_warp < 2 ? 2 : g_dev_warp > WORLD_W - 3 ? WORLD_W - 3 : g_dev_warp;
    g_dev_warp = -1;
    G.abilities = 7;
    for (int i = 0; i < G.nplayers; i++) {
      cw_platformer2d_teleport(&G.p[i].pf, (cw_pos2){ (double)sx + 0.7 * i, (double)G.L.ground[sx] + 0.05 });
      G.p[i].invuln = 120;
    }
    G.cam.x = G.cam.tx = G.cam.px = (float)sx, G.cam.y = G.cam.ty = G.cam.py = (float)G.L.ground[sx] + 3;
    for (int k = 0; k < G.L.nchecks; k++) /* a fall comes back here, not to the start */
      if (G.L.checks[k].x < sx) G.checkpoint = k, G.reached |= 1u << k;
  }
#ifdef COOP_AUTOPILOT
  for (int i = 0; i < MAX_PLAYERS; i++) sim_bot_input(&G, i, &INPUT[i]);
#ifdef COOP_DEF_WALK_ONLY
  for (int i = 0; i < MAX_PLAYERS; i++) memset(&INPUT[i], 0, sizeof INPUT[i]), INPUT[i].x = 1;
#endif
#else
  read_input();
  if (cw_action_just_pressed(0, a_map)) {
    g_map_open = !g_map_open;
    g_map_sel = G.checkpoint;
  }
  if (g_map_open) map_cursor();
#endif
  sim_step(&G, INPUT, (float)dt);
#ifdef COOP_AUTOPILOT
  if (getenv("COOP_TEST_BUBBLES")) /* the rescue-tag gate: everyone but the first player is carried in a bubble */
    for (int i = 1; i < G.nplayers; i++) G.p[i].bubble = 1, G.p[i].bubble_x = (float)G.p[0].pf.body.position.x + 6.0f * (float)i, G.p[i].bubble_y = (float)G.p[0].pf.body.position.y + (i == 1 ? 40.0f : 4.0f); /* one off the top of the view */
#endif
  play_events();
  send_rumble();
  g_state_dbg = (int)cw_shell_screen_now();
  g_level_dbg[0] = G.level, g_level_dbg[1] = NLEVELS;
  music_tick();
  g_players_dbg = G.nplayers;
  g_pos_dbg[0] = (float)G.p[0].pf.body.position.x, g_pos_dbg[1] = (float)G.p[0].pf.body.position.y;
  g_cam_dbg[0] = G.cam.x, g_cam_dbg[1] = G.cam.y, g_cam_dbg[2] = G.cam.z;
  g_hp_dbg = G.p[0].hp;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    bool on = G.p[i].joined && i < G.nplayers;
    g_ppos_dbg[2 * i] = on ? (float)G.p[i].pf.body.position.x : 0, g_ppos_dbg[2 * i + 1] = on ? (float)G.p[i].pf.body.position.y : 0;
    g_idle_dbg[i] = on ? G.p[i].idle : 0;
  }
  /* the end of a level: the results card, then the next. Jump skips the card once it has been up for a moment */
  if (G.won) {
    bool skip = false;
#ifndef COOP_AUTOPILOT
    for (int i = 0; i < G.nplayers; i++) if (INPUT[i].jump_pressed) skip = true;
#endif
    if (G.tick - G.win_tick > 480 || (skip && G.tick - G.win_tick > 90)) next_level();
  }
}

/* ── music: one track per world, a boss has its own, the title its own. Modules loop on their own with no seam ── */
static int music_track_now = -1;
static int64_t music_last_frames;
static void music_play(int track) {
  if (!A.on || track == music_track_now) return;
  static const char *const FILES[] = { "music/world1.xm", "music/world2.xm", "music/world3.xm", "music/world4.xm", "music/world5.xm", "music/boss.xm", "music/title.xm" };
  if (cw_voice_is_playing(A.music_voice)) cw_voice_stop(A.music_voice, 0.5f);
  if (A.music.id) cw_sound_free(A.music);
  A.music = cw_sound_load(FILES[track], &(cw_sound_desc){ .mode = CW_SOUND_STREAM, .bus = A.music_bus });
  A.music_voice = cw_audio_play(A.music, &(cw_play_desc){ .loop = true, .fade_in = 0.5f, .bus = A.music_bus });
  music_track_now = track;
  music_last_frames = 0;
  g_music_dbg[0] = track;
}
static void music_for_level(void) {
  music_play(LEVELS[G.level].world);
}
static void music_tick(void) {
  if (!A.on) { g_music_dbg[0] = -1; return; }
  /* a boss's own tune, from the moment anyone is near the arena (the door of the fight, not the road to it) */
  if (G.L.boss_x > 0 && !G.won && music_track_now != 5)
    for (int i = 0; i < G.nplayers; i++)
      if (G.p[i].joined && (float)G.p[i].pf.body.position.x > (float)G.L.boss_x - 22.0f) { music_play(5); break; }
  int64_t f = cw_voice_frames_played(A.music_voice);
  if (f > music_last_frames) g_music_dbg[2]++;
  g_music_dbg[1] = (int)f;
  music_last_frames = f;
}

/* ── death gags: mostly you just go in the bubble. Sometimes it is a Bad Day ── */
#define NGIBS 24
#define NSTAINS 28
static struct { cw_node n; t3_mesh *m; float x, y, vx, vy, spin, t, life; bool on; } GIB[NGIBS];
static int gib_next;
static cw_node g_stain[NSTAINS];
static int stain_next, stain_count;
static cw_node g_fx_blood, g_fx_spurt, g_fx_draft;
static void gore_build(void) {
  /* bones and bits, tinted for whose they were. Small, brief, and they fall out of the way */
  t3_geometry *bone = t3_cylinder_geometry_new(0.06f, 0.05f, 0.42f, 8, 1, false);
  t3_geometry *blob = t3_sphere_geometry_new(0.13f, 10, 8);
  t3_material *bm = phys(0xf2ecd8, 0.75f, 0);
  for (int i = 0; i < NGIBS; i++) {
    GIB[i].n = make_node("Node3D", S.world, "Gib");
    GIB[i].m = part(GIB[i].n, i % 3 == 2 ? blob : bone, i % 3 == 2 ? phys(0xd0463a, 0.8f, 0) : bm, 0, 0, 0, 1, 1, 1);
    GIB[i].on = false;
    cw_node3d_set_position(GIB[i].n, (t3_vec3){ 0, -60, 0 });
  }
  t3_release(bone), t3_release(blob);
  for (int i = 0; i < NSTAINS; i++) {
    cw_node d = g_stain[i] = make_node("Decal3D", S.world, "Stain");
    cw_node_set(d, "texture", cw_val_string("gore.png"));
    cw_node_set(d, "blend", cw_val_enum("MULTIPLY"));
    cw_node_set(d, "edge_fade", cw_val_float(0.2f));
    cw_node_set(d, "normal_fade", cw_val_float(0.4f));
    cw_node_set(d, "color", cw_val_color(1, 1, 1, 0));
    cw_node3d_set_position(d, (t3_vec3){ 0, -60, 0 });
  }
  stain_next = stain_count = 0;
  g_gore_dbg[2] = 0;
  g_fx_blood = make_node("GPUParticles3D", S.world, "Blood");
  pfx(g_fx_blood, 60, 0.5f, 3.0f, 11.0f, 0.26f, cw_rgba(0.75f, 0.03f, 0.04f, 1), -16.0f, 0, "0 0.8 0.04 0.05 1, 1 0.45 0.02 0.03 0.9", "0 1, 1 0.6", 2.0f);
  g_fx_draft = make_node("GPUParticles3D", S.world, "Draft");
  pfx(g_fx_draft, 40, 1.1f, 6.0f, 9.0f, 0.07f, cw_rgba(0.9f, 0.95f, 1, 0.5f), 0.0f, 1, "0 1 1 1 0, 0.2 1 1 1 0.7, 1 1 1 1 0", "0 1, 1 0.5", 1.0f);
  cw_node_set(g_fx_draft, "draw_mode", cw_val_enum("STRETCHED"));
  cw_node_set(g_fx_draft, "one_shot", cw_val_bool(false));
  cw_node_set(g_fx_draft, "explosiveness", cw_val_float(0));
  cw_node_set(g_fx_draft, "emission_shape", cw_val_enum("BOX"));
  cw_node_set(g_fx_draft, "direction", cw_val_vec3(0, 1, 0));
  cw_node_set(g_fx_draft, "spread", cw_val_float(4));
  cw_node_set(g_fx_draft, "emitting", cw_val_bool(false));
  g_fx_spurt = make_node("GPUParticles3D", S.world, "Spurt");
  pfx(g_fx_spurt, 22, 0.5f, 5.0f, 15.0f, 0.42f, cw_rgba(0.85f, 0.05f, 0.05f, 1), -18.0f, 0, "0 0.85 0.05 0.05 1, 1 0.5 0.02 0.03 1", "0 1, 1 0.7", 1.0f);
}
static int gib_owner;
static float gib_size = 1.0f;
static void gib(float x, float y, float vx, float vy, float life) {
  int i = gib_next++ % NGIBS;
  cw_node3d_set_scale(GIB[i].n, (t3_vec3){ gib_size, gib_size, gib_size });
  if (i % 3 == 2 && GIB[i].m) { /* the soft bits keep the colour of who they were */
    t3_material *mt = GIB[i].m->materials[0];
    if (mt) mt->color = (t3_color){ PLAYER_RGB[gib_owner][0], PLAYER_RGB[gib_owner][1], PLAYER_RGB[gib_owner][2] };
  }
  GIB[i].x = x, GIB[i].y = y, GIB[i].vx = vx, GIB[i].vy = vy, GIB[i].spin = 6.0f + vx, GIB[i].t = 0, GIB[i].life = life, GIB[i].on = true;
}
static void gore_step(float dt) {
  { /* the draught: over whichever fan is nearest the camera, so a fan can be read as something that lifts */
    int fan = -1;
    float best = 28.0f;
    for (int k = 0; k < G.L.nthings; k++) {
      const thing *t = &G.L.things[k];
      if (t->kind == TH_FAN && fabsf(t->x - G.cam.x) < best) best = fabsf(t->x - G.cam.x), fan = k;
    }
    static int on_fan = -2;
    if (fan != on_fan) {
      on_fan = fan;
      cw_node_set(g_fx_draft, "emitting", cw_val_bool(fan >= 0));
      if (fan >= 0) {
        const thing *t = &G.L.things[fan];
        cw_node3d_set_position(g_fx_draft, (t3_vec3){ t->x, t->y + 0.5f, 0 });
        cw_node_set(g_fx_draft, "emission_extents", cw_val_vec3(t->w, 0.2f, 0.9f));
      }
    }
  }
  for (int i = 0; i < NGIBS; i++) {
    if (!GIB[i].on) continue;
    GIB[i].t += dt;
    if (GIB[i].t > GIB[i].life) { GIB[i].on = false; cw_node3d_set_position(GIB[i].n, (t3_vec3){ 0, -60, 0 }); continue; }
    GIB[i].vy -= 30.0f * dt;
    GIB[i].x += GIB[i].vx * dt, GIB[i].y += GIB[i].vy * dt;
    if (GIB[i].vy < 0 && world_solid(&G.L, (int)floorf(GIB[i].x), (int)floorf(GIB[i].y - 0.1f))) { /* a bounce and then it lies there */
      GIB[i].y = floorf(GIB[i].y - 0.1f) + 1.1f;
      GIB[i].vy = -GIB[i].vy * 0.35f;
      GIB[i].vx *= 0.5f, GIB[i].spin *= 0.4f;
    }
    cw_node3d_set_position(GIB[i].n, (t3_vec3){ GIB[i].x, GIB[i].y, 0.9f });
    cw_node3d_set_rotation(GIB[i].n, (t3_vec3){ GIB[i].t * GIB[i].spin, 0, GIB[i].t * GIB[i].spin * 0.7f });
  }
}
static void stain(float x, float y, float w, float h, int cell, bool down, float spin) {
  cw_node d = g_stain[stain_next++ % NSTAINS];
  cw_node_set(d, "region", val_vec4((float)(cell % 4) * 256, (float)(cell / 4) * 256, 256, 256));
  cw_node_set(d, "color", cw_val_color(1, 1, 1, 0.92f));
  if (down) {
    cw_node_set(d, "size", cw_val_vec3(w, h, 0.5f));
    cw_node3d_set_position(d, (t3_vec3){ x, y + 0.12f, 0 });
    cw_node3d_set_rotation(d, (t3_vec3){ -1.5708f, spin, 0 });
  } else {
    cw_node_set(d, "size", cw_val_vec3(w, h, 0.8f));
    cw_node3d_set_position(d, (t3_vec3){ x, y, 1.6f });
    cw_node3d_set_rotation(d, (t3_vec3){ 0, 0, spin });
  }
  if (stain_count < NSTAINS) stain_count++;
  g_gore_dbg[2] = stain_count;
}

/* ── graves: every death leaves one on the nearest firm ground, and goes on leaving them. The same spot again and the same
 * stone gets ruder. All of it is thrown away with the level ── */
#define NGRAVES 16
static struct { int col, count[MAX_PLAYERS], total, cause; float y; cw_node n; bool on; } GRAVE[NGRAVES];
static int cause_counts[DIE_KINDS];
static void graves_reset(void) {
  t3_geometry *stone = t3_box_geometry_new(0.62f, 0.8f, 0.14f, 1, 1, 1), *top = t3_sphere_geometry_new(0.31f, 12, 8), *paw = t3_sphere_geometry_new(0.07f, 8, 6);
  t3_material *sm = phys(0x8c8f96, 0.9f, 0), *pm = phys(0x3a3c42, 0.9f, 0);
  for (int i = 0; i < NGRAVES; i++) {
    GRAVE[i].on = false;
    GRAVE[i].total = 0;
    memset(GRAVE[i].count, 0, sizeof GRAVE[i].count);
    cw_node n = GRAVE[i].n = make_node("Node3D", S.world, "Grave");
    part(n, stone, sm, 0, 0.4f, 0, 1, 1, 1);
    part(n, top, sm, 0, 0.8f, 0, 1, 0.8f, 0.46f);
    part(n, paw, pm, 0, 0.55f, 0.08f, 1, 1, 0.3f);
    for (int k = 0; k < 3; k++) part(n, paw, pm, -0.11f + 0.11f * (float)k, 0.72f, 0.08f, 0.5f, 0.6f, 0.3f);
    cw_node_set(n, "visible", cw_val_bool(false));
    cw_node3d_set_position(n, (t3_vec3){ 0, -60, 0 });
  }
  t3_release(stone), t3_release(top), t3_release(paw);
  memset(cause_counts, 0, sizeof cause_counts);
  g_graves_dbg[0] = g_graves_dbg[1] = 0;
}
static bool firm_col(const level *L, int c, int *top) {
  if (c < 2 || c >= L->width) return false;
  int g = L->ground[c];
  if (g < 4 || g >= WORLD_H) return false;
  int t = L->t[g - 1][c], up = L->t[g][c];
  if (t != T_ROCK && t != T_GRASS && t != T_BRICK && t != T_ICE) return false;
  if (up != T_AIR) return false;
  *top = g;
  return true;
}
static void grave_add(int who, int cause, float x) {
  const level *L = &G.L;
  cause_counts[cause]++;
  int col = -1, top = 0;
  for (int d = 0; d < 40 && col < 0; d++) {
    for (int s = -1; s <= 1; s += 2) {
      int c = (int)floorf(x) + s * d;
      if (firm_col(L, c, &top)) { col = c; break; }
    }
  }
  if (col < 0) return;
  int slot = -1;
  for (int i = 0; i < NGRAVES; i++) if (GRAVE[i].on && abs(GRAVE[i].col - col) <= 1) slot = i;
  if (slot < 0) {
    static int next;
    slot = next++ % NGRAVES;
    GRAVE[slot].on = true, GRAVE[slot].col = col, GRAVE[slot].total = 0;
    memset(GRAVE[slot].count, 0, sizeof GRAVE[slot].count);
    GRAVE[slot].y = (float)top;
    cw_node_set(GRAVE[slot].n, "visible", cw_val_bool(true));
    /* at the back of the path, crooked, a little different each */
    cw_node3d_set_position(GRAVE[slot].n, (t3_vec3){ (float)col + 0.5f, (float)top, -0.8f });
    cw_node3d_set_rotation(GRAVE[slot].n, (t3_vec3){ 0, 0.3f * ((float)(col % 7) / 7.0f - 0.5f), 0.14f * ((float)(col % 5) / 5.0f - 0.5f) });
  }
  GRAVE[slot].count[who]++, GRAVE[slot].total++, GRAVE[slot].cause = cause;
  int n = 0, off = 0;
  for (int i = 0; i < NGRAVES; i++) if (GRAVE[i].on) {
    n++;
    int tp;
    if (!firm_col(L, GRAVE[i].col, &tp) || fabsf((float)tp - GRAVE[i].y) > 0.001f) off++;
  }
  g_graves_dbg[0] = n, g_graves_dbg[1] = off;
}
/* the layout of the HUD this frame, from the drawn text (the player's text size grows it): the bottom of the player
 * panels, the top of the hint line along the bottom, the top of the boss's name over its bar and the radius of a
 * player marker, in px. At 100% they are where they always were (74, H - 32, H - 82 and 11, times s) */
static float g_hud_bot, g_hint_top, g_boss_top, g_mark_r;
/* the ability chips when they sit under the player panels (four players, or large text): their left edge and bottom
 * (0: they are up beside the panels). The level name and the markers keep below them */
static float g_chip_x, g_chip_bot;
/* how much the player's text size grows text asked for at `size` (1 at 100%) */
static float text_grow(float size) { return cw_text_drawn_size(g_font, size) / size; }
/* is a box under the sign's words drawn last frame? */
/* the boxes of words already drawn this frame (the sign's caption, a boss's words, what players say): later words
 * keep off them, so two captions never stack */
static float g_boxes[12][4];
static int g_nbox;
static void box_add(float x, float y, float w, float h) {
  if (g_nbox < 12) g_boxes[g_nbox][0] = x, g_boxes[g_nbox][1] = y, g_boxes[g_nbox][2] = w, g_boxes[g_nbox][3] = h, g_nbox++;
}
static bool box_hit(float x, float y, float w, float h, float pad) {
  if (g_words_bug) return false;
  for (int i = 0; i < g_nbox; i++) {
    const float *b = g_boxes[i];
    if (b[0] - pad < x + w && b[0] + b[2] + pad > x && b[1] - pad < y + h && b[1] + b[3] + pad > y) return true;
  }
  return false;
}
static bool caption_over(float x, float y, float w, float h) {
  const float *b = g_caption_prev;
  return b[2] > 0 && b[0] < x + w && b[0] + b[2] > x && b[1] < y + h && b[1] + b[3] > y;
}

static void draw_graves(float W, float H, float s) {
  static const char *const WHO[MAX_PLAYERS] = { "BLUE", "RED", "GREEN", "YELLOW" };
  for (int i = 0; i < NGRAVES; i++) {
    if (!GRAVE[i].on) continue;
    bool near = false;
    for (int k = 0; k < G.nplayers; k++) {
      const player *p = &G.p[k];
      if (p->joined && p->alive && !p->bubble && fabsf((float)p->pf.body.position.x - ((float)GRAVE[i].col + 0.5f)) < 3.5f && fabsf((float)p->pf.body.position.y - GRAVE[i].y) < 4) near = true;
    }
    if (!near) continue;
    int who = 0, most = -1;
    for (int k = 0; k < MAX_PLAYERS; k++) if (GRAVE[i].count[k] > most) most = GRAVE[i].count[k], who = k;
    char b[96];
    int c = GRAVE[i].total;
    if (c == 1) snprintf(b, sizeof b, "HERE LIES %s. BRIEFLY.", WHO[who]);
    else if (c == 2) snprintf(b, sizeof b, "%s AGAIN. WE KEPT THE PLOT.", WHO[who]);
    else if (c == 3) snprintf(b, sizeof b, "%s, THIRD VISIT. A LOYALTY CARD IS IN THE POST.", WHO[who]);
    else if (c == 4) snprintf(b, sizeof b, "%s. ONE MORE AND YOU GET A FREE ONE.", WHO[who]);
    else if (c == 5) snprintf(b, sizeof b, "THIS ONE IS FREE.");
    else if (c < 10) snprintf(b, sizeof b, "%s x%d. THE GRASS HAS STOPPED GROWING BACK.", WHO[who], c);
    else snprintf(b, sizeof b, "%s x%d. WE HAVE STOPPED CARVING. PLEASE CARVE YOUR OWN.", WHO[who], c);
    float sx, sy;
    if (!project((float)GRAVE[i].col + 0.5f, GRAVE[i].y + 1.3f, -0.8f, W, H, &sx, &sy)) continue;
    cw_text_metrics tm = cw_font_measure(g_font, b, &(cw_text_style){ .size = 13 * s });
    cw_r2d_round_rect(cw_rect_make(sx - tm.width * 0.5f - 6 * s, sy - tm.height - 8 * s, tm.width + 12 * s, tm.height + 6 * s), 5 * s, 6, cw_rgba(0.16f, 0.17f, 0.2f, 0.78f));
    cw_r2d_text(g_font, b, (cw_vec2){ sx - tm.width * 0.5f, sy - tm.height - 5 * s }, &(cw_text_style){ .size = 13 * s, .color = cw_rgba(0.9f, 0.9f, 0.88f, 1) });
  }
}

/* what somebody says: over their head, for a moment */
static struct { const char *text; int who; float t; } SAY[6];
static void say(int who, const char *text, float secs) {
  for (int i = 0; i < 6; i++) if (SAY[i].t <= 0 || SAY[i].who == who) { SAY[i].who = who, SAY[i].text = text, SAY[i].t = secs; return; }
}
static uint32_t gag_rand(uint32_t salt) {
  uint32_t v = (uint32_t)G.tick * 2654435761u + salt * 40503u + (uint32_t)G.deaths * 977u;
  v ^= v >> 15;
  return v * 2246822519u >> 8;
}
/* a line from one of the friends who saw it, or if nobody did, from the one it happened to, on the way back */
static void react(int who, const char *const *lines, int n) {
  int witness = -1;
  float best = 1e9f;
  for (int i = 0; i < G.nplayers; i++) {
    const player *p = &G.p[i];
    if (i == who || !p->joined || !p->alive || p->bubble) continue;
    float d = fabsf((float)p->pf.body.position.x - (float)G.p[who].pf.body.position.x);
    if (d < best) best = d, witness = i;
  }
  say(witness >= 0 ? witness : who, lines[gag_rand(17) % (uint32_t)n], 2.4f);
  if (witness >= 0) G.p[witness].squash = -0.3f; /* a small hop of horror, or joy */
}
static void gore_death(int who, int cause, float x, float y) {
  grave_add(who, cause, x);
  g_gore_dbg[0]++;
  gib_owner = who;
  if (getenv("COOP_NO_GORE")) return; /* the gag gate's control (native) */
  static const char *const SEEN[] = { "oh no", "was that load-bearing?", "dibs on the coins", "i did not see anything", "he's fine", "that one was mine", "more room in the bubble",
                                      "tell my mum it was quick", "can i have your hat", "she had a family. of bees." };
  static const char *const SELF[] = { "i'm fine", "that part was spare", "i meant to", "put that on the form", "still got one good ear", "ow", "again" };
  static const char *const SAW[] = { "the trimmer is in a good mood", "she missed a bit", "that hedge is finished", "nice cut", "i found an ear" };
  static const char *const PRS[] = { "days since last incident: 0", "flat pack", "now in a smaller size", "you could post him now" };
  static const char *const PIA[] = { "that was b flat", "a bit sharp", "the piano is fine", "encore", "they did say complimentary" };
  static const char *const LAV[] = { "medium rare", "the deposit is gone", "is that a bone", "smells like lunch" };
  uint32_t roll = gag_rand(1) % 100;
  bool big = false;
  switch (cause) {
    case DIE_COIN: {
      static const char *const CN[] = { "the coin did it", "money, again", "that coin looked at me", "keep the change" };
      burst(g_fx_blood, x, y + 0.8f, 0.8f);
      react(who, CN, 4);
      big = true;
      break;
    }
    case DIE_SHELL: { /* somebody kicked that */
      static const char *const SH[] = { "who kicked that", "that was a team shell", "i was standing right there", "five stars for the kick", "it came back" };
      big = true;
      burst(g_fx_blood, x, y + 0.8f, 0.8f);
      for (int i = 0; i < 4; i++) gib(x, y + 0.9f, (float)((int)(gag_rand(70 + (uint32_t)i) % 17) - 8), 7.0f + (float)(gag_rand(80 + (uint32_t)i) % 6), 1.2f);
      stain(x, y, 2.4f, 1.6f, 5, true, 0);
      react(who, SH, 5);
      break;
    }
    case DIE_CHASER: {
      static const char *const CH[] = { "it had such a nice face", "we should have run", "i was told to run", "it is still smiling" };
      big = true;
      burst(g_fx_blood, x, y + 0.8f, 0.8f);
      burst(g_fx_spurt, x, y + 0.9f, 0.8f);
      for (int i = 0; i < 6; i++) gib(x, y + 0.9f, (float)((int)(gag_rand(40 + (uint32_t)i) % 21) + 4), 9.0f + (float)(gag_rand(60 + (uint32_t)i) % 8), 1.3f);
      stain(x, y, 3.2f, 2.4f, 5, true, 0);
      react(who, CH, 4);
      break;
    }
    case DIE_SAW: big = true; /* a saw always tells a story */
      burst(g_fx_blood, x, y + 0.8f, 0.8f);
      burst(g_fx_spurt, x, y + 0.9f, 0.8f);
      for (int i = 0; i < 6; i++) gib(x, y + 0.9f, (float)((int)(gag_rand(30 + (uint32_t)i) % 25) - 12), 6.0f + (float)(gag_rand(50 + (uint32_t)i) % 8), 1.3f);
      stain(x + (gag_rand(3) & 1 ? 0.8f : -0.8f), y, 3.2f, 1.6f, 5, true, 0);
      stain(x, y - 1.4f, 3.0f, 3.0f, 4, false, 0); /* it runs down the face of the ground */
      react(who, SAW, 5);
      break;
    case DIE_PRESS: big = true; /* a flat stain with the shape still in it */
      burst(g_fx_blood, x, y + 0.2f, 0.8f);
      stain(x, y, 3.0f, 2.6f, gag_rand(5) & 3, true, 0);
      stain(x, y - 1.2f, 2.8f, 2.8f, 4, false, 0);
      for (int i = 0; i < 3; i++) gib(x, y + 0.3f, (float)((int)(gag_rand(60 + (uint32_t)i) % 13) - 6), 8.0f, 1.0f);
      react(who, PRS, 4);
      break;
    case DIE_FALLER: big = true; /* the outline, drawn round at the scene */
      burst(g_fx_blood, x, y + 0.2f, 0.8f);
      stain(x, y - 1.3f, 2.6f, 2.6f, 7, false, 0); /* drawn round, on the nearest wall */
      stain(x, y, 2.4f, 2.4f, gag_rand(6) & 3, true, 0);
      react(who, PIA, 5);
      break;
    case DIE_LAVA:
      if (roll < 50) { /* every other swim sends the bones back up */
        big = true;
        /* big enough to read against the glare, and high enough to come back up the pit past whoever is watching */
        gib_size = 2.2f;
        for (int i = 0; i < 6; i++) gib(x, y + 0.2f, (float)((int)(gag_rand(70 + (uint32_t)i) % 7) - 3), 16.0f + (float)(gag_rand(80 + (uint32_t)i) % 9), 2.2f);
        gib_size = 1.0f;
        burst(S.fx_burst, x, y + 0.6f, 0.8f);
        react(who, LAV, 4);
      } else react(who, SELF, 7);
      break;
    case DIE_SPIKE: case DIE_ENEMY: case DIE_BOSS:
      if (roll < 34) { /* sometimes only */
        big = true;
        burst(g_fx_blood, x, y + 0.8f, 0.8f);
        for (int i = 0; i < 3; i++) gib(x, y + 0.9f, (float)((int)(gag_rand(90 + (uint32_t)i) % 15) - 7), 7.0f, 1.1f);
        stain(x, y, 2.2f, 2.2f, gag_rand(7) & 3, true, 0);
        stain(x, y - 1.1f, 2.2f, 2.2f, gag_rand(8) % 3 ? 4 : 6, false, 0);
        react(who, SEEN, 10);
      } else react(who, SELF, 7);
      break;
    case DIE_PIT: { /* the pit answers: after the fall a bone comes back up over the lip, and something is said about it */
      static const char *const PT[] = { "that was a long way", "i can see my house from down here", "...ow", "it's fine, it has a bottom", "the pit has returned a bone", "tell the bone i said hi", "gravity: 1, you: 0" };
      float lip = sim_lip_of(&G.L, x);
      gib_size = 1.6f;
      for (int i = 0; i < 2; i++) gib(x + 0.3f * (float)i, lip - 2.0f, (float)((int)(gag_rand(100 + (uint32_t)i) % 7) - 3), 15.0f + (float)i * 3.0f, 2.2f);
      gib_size = 1.0f;
      big = true;
      react(who, PT, 7);
      break;
    }
    default: react(who, SELF, 7); break;
  }
  if (big) g_gore_dbg[1]++;
}
/* whatever is the newest, on everybody: each player in a different one of the set */
static void hats_apply(void) {
  const save_t *sv = cw_shell_game_save();
  int n = sv->hats > 6 ? 6 : sv->hats;
  if (getenv("COOP_TEST_HATS")) n = atoi(getenv("COOP_TEST_HATS")); /* a look at them without earning them */
  for (int i = 0; i < MAX_PLAYERS; i++)
    for (int h = 0; h < 6; h++) {
      bool on = n > 0 && h == (n - 1 + i * 2) % n;
      if (S.hat[i][h][0]) S.hat[i][h][0]->base.visible = on;
      if (S.hat[i][h][1]) S.hat[i][h][1]->base.visible = on;
    }
}
static void grief_gag(int who, int victim) {
  if (victim < 0) { /* a throw: the one in the air has the last word */
    static const char *const T1[] = { "wheee", "i trusted you", "i regret nothing", "this was not discussed", "tell my mum", "aim for the good spikes" };
    say(-victim - 1, T1[gag_rand(11) % 6], 1.8f);
    return;
  }
  static const char *const G1[] = { "sorry", "not sorry", "friendship minus one", "you were in the way", "that head was just there" };
  say(who, G1[gag_rand(9) % 5], 1.8f);
}

/* on to the next level, or at the end of the last, back to the title */
/* every secret coin of a level found: a hat is issued. Nobody asked */
static const char *const HAT_NAMES[6] = { "A TRAFFIC CONE", "A BUCKET", "A SMALLER BUCKET", "A VERY GOOD HAT", "A POT", "THE LANDLORD'S HAT" };
static int hat_new_t;
static void hat_award(void) {
  save_t *sv = cw_shell_game_save();
  int total = 0, got = 0;
  for (int k = 0; k < G.L.ncoins; k++) if (G.L.coins[k].secret) { total++; if (G.L.coins[k].taken) got++; }
  if (total == 0 || got < total || G.level >= 64) return;
  uint32_t bit = 1u << (G.level % 32);
  if (sv->hat_levels[G.level / 32] & bit) return;
  sv->hat_levels[G.level / 32] |= bit;
  sv->hats++;
  hat_new_t = 300;
}
static void hats_apply(void);
static void next_level(void) {
  save_t *sv = cw_shell_game_save();
  hat_award();
  hats_apply();
  sv->total_coins += G.coins_total, sv->total_deaths += G.deaths;
  for (int i = 0; i < G.nplayers; i++) sv->total_betrayals += G.betrayals[i];
  int players = G.nplayers;
  uint32_t ab = G.abilities;
  if (G.level + 1 >= NLEVELS) {
    G.level = 0;
    sv->level = 0;
    sv->reached = 0;
    memset(sv->seen, 0, sizeof sv->seen);
    save_progress();
#ifdef COOP_AUTOPILOT
    G.abilities = LEVELS[0].abilities;
    sim_reset(&G, players);
    build_scene();
    music_for_level();
#else
    cw_shell_to_title();
    G.nplayers = 0;
#endif
    return;
  }
  G.level++;
  G.abilities = ab | LEVELS[G.level].abilities;
  sim_reset(&G, players);
  G.abilities = ab | LEVELS[G.level].abilities;
  sv->reached = 0;
  memset(sv->seen, 0, sizeof sv->seen);
  save_progress();
  build_scene();
  camgate_reset();
  music_for_level();
}

/* ── HUD ───────────────────────────────────────────────────────────── */
static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align al, float w) {
  /* the drop shadow is a second copy: the player's Text background goes on the words alone */
  cw_r2d_text(g_font, s, (cw_vec2){ x + 2, y + 2 }, &(cw_text_style){ .size = size, .color = cw_rgba(0, 0, 0, 0.6f * c.a), .align = al, .wrap_width = w, .own_backdrop = true });
  cw_r2d_text(g_font, s, (cw_vec2){ x, y }, &(cw_text_style){ .size = size, .color = c, .align = al, .wrap_width = w });
}
/* text with button prompts in it (cartwheel/prompt.h markup: "{jump} jump"), for every player, in the box text() would use */
static void ptext(const char *s, float x, float y, float size, cw_color c, cw_text_align al, float w) {
  float px = al == CW_ALIGN_CENTER ? x + w * 0.5f : al == CW_ALIGN_RIGHT ? x + w : x;
  cw_prompt_text(g_font, s, -1, (cw_vec2){ px, y }, size, c, al);
}
/* a hint of parts split by runs of five spaces, wrapped onto as many lines as it needs to fit maxw (a big text size,
 * a narrow screen): the text with '\n' between its lines in out. Returns the number of lines */
/* the results card's awards: who got the most of something this level (a clear winner only), in the voice of the
 * Management. Empty with one player: nobody to be compared with */
static int top_of(const int *v) {
  int best = -1, n = 0;
  for (int i = 0; i < G.nplayers; i++) {
    if (v[i] <= 0) continue;
    if (best < 0 || v[i] > v[best]) best = i, n = 1;
    else if (v[i] == v[best]) n++;
  }
  return n == 1 ? best : -1;
}
static void tally_awards(char *out, size_t cap) {
  out[0] = 0;
  if (G.nplayers < 2) return;
  int coins[MAX_PLAYERS], trust[MAX_PLAYERS];
  for (int i = 0; i < MAX_PLAYERS; i++) coins[i] = G.p[i].coins, trust[i] = G.betrayals[i] + G.throws[i];
  struct { const char *what; int who; } aw[4] = { { "Employee of the level", top_of(coins) }, { "Most violent", top_of(G.stomps) },
                                                 { "Most incidents filed", top_of(G.falls) }, { "Least trusted", top_of(trust) } };
  size_t n = 0;
  for (int k = 0; k < 4 && n + 48 < cap; k++)
    if (aw[k].who >= 0) n += (size_t)snprintf(out + n, cap - n, "%s%s: %d.", n ? "   " : "", aw[k].what, aw[k].who + 1);
}

/* the hint line at the bottom of play: how to run and jump far, and the way back to the last flag */
static const char *const HINT_FULL = "hold {run} to run, {jump} to jump: run first and jump further     hold {retry}: back to the last flag";
static const char *const HINT_RUN = "hold {run} to run, {jump} to jump: run first and jump further";
static const char *const HINT_RETRY = "hold {retry}: back to the last flag";
static bool g_hint_ran, g_hint_returned; /* this session has made a far running jump; has gone back to a flag (or got to the third level) */
static int hint_mask(void) {
  if (G.last_jump[0] > 7.5f && G.last_jump[2] > RUN_SPEED * 0.8f) g_hint_ran = true; /* a running jump: a standing one goes about 4.5 m, a walking one about 6 */
  if (G.returns > 0 || G.level >= 2) g_hint_returned = true;
  return (g_hint_ran ? 0 : 1) | (g_hint_returned ? 0 : 2);
}

static int wrap_hint(const char *s, float size, float maxw, char *out, size_t cap) {
  static const char SEP[] = "     ";
  size_t n = 0;
  int lines = 1;
  char line[256] = "";
  out[0] = 0;
  for (const char *p = s; *p;) {
    const char *e = strstr(p, SEP);
    size_t len = e ? (size_t)(e - p) : strlen(p);
    char trial[512];
    snprintf(trial, sizeof trial, "%s%s%.*s", line, line[0] ? SEP : "", (int)len, p);
    if (line[0] && cw_prompt_text_size(g_font, trial, -1, size).x > maxw) { /* this part starts a new line */
      n += (size_t)snprintf(out + n, n < cap ? cap - n : 0, "%s\n", line);
      lines++;
      snprintf(line, sizeof line, "%.*s", (int)len, p);
    } else snprintf(line, sizeof line, "%.*s", (int)sizeof line - 1, trial);
    p += len;
    while (*p == ' ') p++;
  }
  snprintf(out + n, n < cap ? cap - n : 0, "%s", line);
  return lines;
}


static cw_color rgba3(const float *c, float a) { return cw_rgba(c[0], c[1], c[2], a); }
static cw_color lerpc(cw_color a, cw_color b, float t) { return cw_rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t); }
static float g_ui_t;

/* a vertical gradient panel: a few bands between two colours, with a rounded top and bottom */
static void panel(float x, float y, float w, float h, float r, cw_color top, cw_color bot, cw_color edge) {
  cw_r2d_round_rect(cw_rect_make(x - 1.5f, y - 1.5f, w + 3, h + 3), r + 1.5f, 8, edge);
  int n = 6;
  cw_r2d_round_rect(cw_rect_make(x, y, w, h), r, 8, bot);
  for (int i = 0; i < n; i++) {
    float t0 = (float)i / (float)n, hh2 = h * 0.5f / (float)n;
    cw_color c = lerpc(top, bot, t0 * 1.6f > 1 ? 1 : t0 * 1.6f);
    c.a *= 0.8f;
    cw_r2d_rect(cw_rect_make(x + r * 0.4f, y + 2 + h * 0.5f * t0 * 0.9f, w - r * 0.8f, hh2 * 0.9f), c);
  }
}
static void heart(cw_vec2 c, float r, cw_color col) {
  cw_r2d_circle((cw_vec2){ c.x - r * 0.5f, c.y - r * 0.25f }, r * 0.56f, 12, col);
  cw_r2d_circle((cw_vec2){ c.x + r * 0.5f, c.y - r * 0.25f }, r * 0.56f, 12, col);
  cw_vec2 tri[3] = { { c.x - r * 1.02f, c.y - r * 0.05f }, { c.x + r * 1.02f, c.y - r * 0.05f }, { c.x, c.y + r * 1.0f } };
  cw_r2d_polygon(tri, 3, col);
}
/* a player's face with its headgear: ears, an antenna, a sprout or horns (the marker that is not a colour) */
static void portrait(cw_vec2 c, float r, int i, bool dim) {
  cw_color fur = rgba3(PLAYER_RGB[i], dim ? 0.45f : 1), acc = i == 0 ? cw_rgba(1, 0.85f, 0.25f, 1) : i == 1 ? cw_rgba(0.95f, 0.95f, 0.95f, 1) : i == 2 ? cw_rgba(1, 0.45f, 0.65f, 1) : cw_rgba(0.3f, 0.8f, 0.95f, 1);
  if (dim) acc.a = 0.5f;
  if (i == 1) { /* antenna */
    cw_r2d_line((cw_vec2){ c.x, c.y - r * 0.8f }, (cw_vec2){ c.x, c.y - r * 1.5f }, r * 0.12f, acc);
    cw_r2d_circle((cw_vec2){ c.x, c.y - r * 1.55f }, r * 0.22f, 8, acc);
  } else if (i == 0) { /* ears */
    for (int k = -1; k <= 1; k += 2) {
      cw_vec2 t[3] = { { c.x + k * r * 0.35f, c.y - r * 0.7f }, { c.x + k * r * 0.95f, c.y - r * 0.7f }, { c.x + k * r * 0.75f, c.y - r * 1.4f } };
      cw_r2d_polygon(t, 3, acc);
    }
  } else if (i == 2) { /* sprout */
    for (int k = -1; k <= 1; k += 2) cw_r2d_circle((cw_vec2){ c.x + k * r * 0.35f, c.y - r * 1.0f }, r * 0.3f, 8, acc);
  } else { /* horns */
    for (int k = -1; k <= 1; k += 2) {
      cw_vec2 t[3] = { { c.x + k * r * 0.55f, c.y - r * 0.6f }, { c.x + k * r * 1.0f, c.y - r * 0.5f }, { c.x + k * r * 1.1f, c.y - r * 1.3f } };
      cw_r2d_polygon(t, 3, acc);
    }
  }
  cw_r2d_circle(c, r, 18, fur);
  cw_r2d_circle((cw_vec2){ c.x, c.y + r * 0.3f }, r * 0.5f, 12, cw_rgba(0.96f, 0.9f, 0.78f, dim ? 0.5f : 1));
  cw_r2d_circle((cw_vec2){ c.x - r * 0.35f, c.y - r * 0.1f }, r * 0.22f, 8, cw_rgba(1, 1, 1, 1));
  cw_r2d_circle((cw_vec2){ c.x + r * 0.35f, c.y - r * 0.1f }, r * 0.22f, 8, cw_rgba(1, 1, 1, 1));
  cw_r2d_circle((cw_vec2){ c.x - r * 0.3f, c.y - r * 0.08f }, r * 0.1f, 6, cw_rgba(0.05f, 0.05f, 0.1f, 1));
  cw_r2d_circle((cw_vec2){ c.x + r * 0.3f, c.y - r * 0.08f }, r * 0.1f, 6, cw_rgba(0.05f, 0.05f, 0.1f, 1));
}
static void coin_icon(cw_vec2 c, float r) {
  cw_r2d_circle(c, r, 14, cw_rgba(0.75f, 0.5f, 0.1f, 1));
  cw_r2d_circle(c, r * 0.82f, 14, cw_rgba(1, 0.8f, 0.25f, 1));
  cw_r2d_circle((cw_vec2){ c.x - r * 0.2f, c.y - r * 0.2f }, r * 0.35f, 8, cw_rgba(1, 0.95f, 0.7f, 0.9f));
}

#define REGION_NAMES WORLD_NAMES

static cw_color tile_color(int t, int biome) {
  switch (t) {
    case T_ROCK: case T_GRASS: case T_BRICK: return biome == BIOME_MEADOW ? cw_rgba(0.3f, 0.6f, 0.28f, 1) : biome == BIOME_CAVES ? cw_rgba(0.35f, 0.45f, 0.7f, 1) : biome == BIOME_RUINS ? cw_rgba(0.8f, 0.7f, 0.48f, 1) : cw_rgba(0.7f, 0.28f, 0.2f, 1);
    case T_WATER: return cw_rgba(0.2f, 0.55f, 0.95f, 1);
    case T_LAVA: return cw_rgba(1, 0.45f, 0.1f, 1);
    case T_SPIKE: return cw_rgba(0.9f, 0.9f, 0.95f, 1);
    case T_ONEWAY: return cw_rgba(0.75f, 0.52f, 0.3f, 1);
    default: return cw_rgba(0, 0, 0, 0);
  }
}

/* each player has a colour AND a shape and number, so four of them stay apart for anyone: 1 triangle, 2 diamond,
 * 3 square, 4 circle */
static void glyph(int i, float cx, float cy, float r, cw_color c) {
  switch (i) {
    case 0: {
      cw_vec2 t[3] = { { cx - r, cy - r * 0.8f }, { cx + r, cy - r * 0.8f }, { cx, cy + r } };
      cw_r2d_polygon(t, 3, c);
      break;
    }
    case 1: {
      cw_vec2 d[4] = { { cx, cy - r }, { cx + r, cy }, { cx, cy + r }, { cx - r, cy } };
      cw_r2d_polygon(d, 4, c);
      break;
    }
    case 2: cw_r2d_rect(cw_rect_make(cx - r * 0.85f, cy - r * 0.85f, r * 1.7f, r * 1.7f), c); break;
    default: cw_r2d_circle((cw_vec2){ cx, cy }, r, 16, c); break;
  }
}

/* the world point as a pixel of the HUD canvas, through the drawn camera (vertical fov 40, pitched down 0.06) */
static bool project(float wx, float wy, float wz, float W, float H, float *sx, float *sy) {
  const float pitch = 0.06f, tf = 0.36397f; /* tan(20 degrees) */
  float dx = wx - g_view_dbg[0], dy = wy - g_view_dbg[1], dz = wz - g_view_dbg[2]; /* the camera as drawn this frame */
  float yv = dy * cosf(pitch) - dz * sinf(pitch), zv = dy * sinf(pitch) + dz * cosf(pitch);
  if (zv > -0.5f) return false;
  *sx = (0.5f + 0.5f * (dx / -zv) / (tf * W / H)) * W;
  *sy = (0.5f - 0.5f * (yv / -zv) / tf) * H;
  return true;
}

/* does a panel touch a player? rise: how far up from the feet counts (the body, or the body and its marker); pad: room around it */
static bool sign_over_player(float x, float y, float w, float h, float rise, float pad, float W, float H) {
  for (int i = 0; i < G.nplayers; i++) {
    const player *p = &G.p[i];
    if (!p->joined || !(p->alive || p->bubble)) continue;
    float wx = p->bubble ? p->bubble_x : (float)p->pf.body.position.x, wy = p->bubble ? p->bubble_y : (float)p->pf.body.position.y;
    float lx, ty, rx, by;
    if (!project(wx - 0.8f, wy + rise, 0, W, H, &lx, &ty) || !project(wx + 0.8f, wy, 0, W, H, &rx, &by)) continue;
    if (rx > x - pad && lx < x + w + pad && by > y - pad && ty < y + h + pad) return true;
  }
  return false;
}
static bool label_on_marker(float x, float y, float w, float h, float pad, float W, float H, float s);
static void label_clear_spot(float *x, float *y, float w, float h, bool bodies, float W, float H, float s);
/* the Management's signs: read when you are near one. Boards are in the world; their words are on a solid panel over the
 * board, and when a player is where the panel would go it steps aside, so it never hides anyone and can always be read */
static void draw_signs(float W, float H, float s) {
  const level *L = &G.L;
  /* one at a time: the nearest to anybody, on its own board */
  int pick = -1;
  float pick_d = 8.0f;
  for (int k = 0; k < L->nsigns; k++) {
    const sign_t *n = &L->signs[k];
    float best = 1e9f;
    for (int i = 0; i < G.nplayers; i++) {
      const player *p = &G.p[i];
      if (!p->joined || !p->alive || p->bubble) continue;
      float dx = fabsf((float)p->pf.body.position.x - n->x), dy = fabsf((float)p->pf.body.position.y - n->y);
      if (dy < 6 && dx < best) best = dx;
    }
    if (best < pick_d) pick_d = best, pick = k;
  }
  if (pick >= 0) {
    const sign_t *n = &L->signs[pick];
    float a = pick_d < 6.0f ? 1.0f : (8.0f - pick_d) / 2.0f;
    float sx, sy;
    if (!project(n->x, n->y + 2.9f, 0, W, H, &sx, &sy)) return;
    float ts = 15.5f * s, pw = fminf(W - 24 * s, 300 * s * text_grow(ts)); /* wider as the text grows */
    float x0 = sx - pw * 0.5f;
    if (x0 < 12 * s) x0 = 12 * s;
    if (x0 + pw > W - 12 * s) x0 = W - 12 * s - pw;
    cw_text_metrics sz = cw_font_measure(g_font, n->text, &(cw_text_style){ .size = ts, .wrap_width = pw - 20 * s });
    float ph = sz.height + 14 * s, y0 = sy - ph;
    if (y0 < g_hud_bot + 6 * s) y0 = g_hud_bot + 6 * s;
    float fade = a;
    g_sign_box[2] = 0;
    if (g_sign_bug) { /* the gate's control: over the board, and thin over a player */
      for (int i = 0; i < G.nplayers; i++) {
        const player *p = &G.p[i];
        float px, py;
        if (!p->joined || !project((float)p->pf.body.position.x, (float)p->pf.body.position.y + 0.7f, 0, W, H, &px, &py)) continue;
        if (px > x0 - 30 * s && px < x0 + pw + 30 * s && py > y0 - 30 * s && py < y0 + ph + 30 * s) a *= 0.35f;
      }
    } else {
      /* a caption with a place of its own: top centre, under the player panels, or the bottom when a player is up there. It
       * stays put while anyone runs about: it changes place only when somebody comes to it, and fades across, so it never
       * chases them round the screen. Nobody is ever behind it: over a body, it is not drawn */
      static int shown = -1, slot, hold;
      static float swap = 1; /* 0..1 how far it has faded back in; below zero, fading out to change place */
      const float slot_y[2] = { g_hud_bot + 6 * s, fminf(H - 66 * s, g_hint_top - 34 * s) - ph }, slot_x = (W - pw) * 0.5f;
      const float rise = 3.0f, near_pad = 26 * s, clear_pad = 46 * s;
      /* a player, or a player's marker (spread sideways and held under the panels, so not always over its body) */
      #define SIGN_BLOCKED(i, pad) (sign_over_player(slot_x, slot_y[i], pw, ph, rise, pad, W, H) || (!g_words_bug && label_on_marker(slot_x, slot_y[i], pw, ph, (pad) * 0.25f, W, H, s)))
      if (shown != pick) {
        shown = pick, swap = 1, hold = 0;
        slot = SIGN_BLOCKED(0, near_pad) && !SIGN_BLOCKED(1, near_pad) ? 1 : 0;
      }
      if (hold > 0) hold--;
      if (swap >= 1 && hold == 0 && SIGN_BLOCKED(slot, near_pad) && !SIGN_BLOCKED(!slot, clear_pad)) swap = -1, g_signmove_dbg[0]++;
      #undef SIGN_BLOCKED
      if (swap < 0) {
        swap += 0.2f;
        if (swap >= 0) swap = 0, slot = !slot, hold = 60;
      } else if (swap < 1) swap = fminf(1.0f, swap + 1.0f / 6.0f);
      a *= fabsf(swap);
      if (a > 0.02f && (sign_over_player(slot_x, slot_y[slot], pw, ph, 1.9f, 0, W, H) || (!g_words_bug && label_on_marker(slot_x, slot_y[slot], pw, ph, 0, W, H, s)))) a = 0;
      if (a < fade - 1e-3f) g_signmove_dbg[1]++;
      x0 = slot_x, y0 = slot_y[slot];
      if (a > 0.02f) g_sign_box[0] = x0, g_sign_box[1] = y0, g_sign_box[2] = pw, g_sign_box[3] = ph;
    }
    g_sign_dbg[0]++;
    if (a > 0.02f && sign_over_player(x0, y0, pw, ph, 1.9f, 0, W, H)) g_sign_dbg[1]++;
    if (g_sign_bug && a < fade - 1e-3f) g_sign_dbg[2]++;
    if (a > 0.02f) g_caption_box[0] = x0, g_caption_box[1] = y0, g_caption_box[2] = pw, g_caption_box[3] = ph, box_add(x0, y0, pw, ph);
    panel(x0, y0, pw, ph, 7 * s, cw_rgba(0.92f, 0.85f, 0.66f, a), cw_rgba(0.76f, 0.65f, 0.44f, a), cw_rgba(0.28f, 0.18f, 0.1f, a));
    cw_r2d_text(g_font, n->text, (cw_vec2){ x0 + 10 * s, y0 + 7 * s }, &(cw_text_style){ .size = ts, .color = cw_rgba(0.13f, 0.08f, 0.04f, a), .wrap_width = pw - 20 * s });
  }
}
/* a boss has something to say each time it is jumped on, and it is always about you */
static void draw_boss_says(float W, float H, float s) {
  static const char *const LINES[5][9] = {
    { "THE RENT IS DUE", "THAT WILL COME OUT OF YOUR DEPOSIT", "YOU ARE SCUFFING THE HEAD", "I KNOW WHERE YOU LIVE. I OWN IT", "NOTICE OF EVICTION" , "OW", "", "", "" },
    { "THAT IS DEDUCTIBLE", "I SEE YOU CLAIMED THAT COIN", "PLEASE KEEP YOUR RECEIPTS", "YOU OWE INTEREST ON THAT", "THIS IS AN AUDIT", "FORM ONE, LINE ONE: OW", "", "", "" },
    { "NO RUNNING", "NO JUMPING", "NO JUMPING ON ME", "I HAVE NEVER SAVED ANYONE", "THIS IS A WHISTLE", "I WILL BLOW IT", "NO DIVING", "", "" },
    { "LET'S CIRCLE BACK", "PER MY LAST STOMP", "THIS WILL GO IN YOUR REVIEW", "WE ARE A FAMILY HERE", "I AM DISAPPOINTED, NOT ANGRY", "LOW HANGING FRUIT", "SYNERGY", "OW", "" },
    { "I STARTED WITH NOTHING", "MY FATHER GAVE ME A VOLCANO", "YOU WILL INHERIT THIS", "I HAVE A BIGGER HAT AT HOME", "THE COINS WERE NEVER REAL", "READ THE CONTRACT", "THERE IS NO PLAN B", "THIS IS PLAN B", "TELL THE PIANO I LOVED IT" },
  };
  static int shown_hp = -1;
  static float t;
  static const char *text_now;
  for (int i = 0; i < G.L.nenemies; i++) {
    const enemy *e = &G.L.enemies[i];
    if (e->kind != 2) continue;
    int kind = G.L.boss_kind, total = 5 + kind;
    if (shown_hp < 0 || e->hp > shown_hp) shown_hp = e->hp;
    if (e->hp < shown_hp) {
      shown_hp = e->hp;
      const char *l = LINES[kind][(total - e->hp - 1) % 9];
      if (l[0]) text_now = l, t = 2.2f;
    }
    if (t > 0 && text_now && e->alive) {
      t -= 1.0f / 60.0f;
      float sx, sy;
      if (project(e->x, e->y + 4.6f, 0, W, H, &sx, &sy)) {
        cw_text_metrics tm = cw_font_measure(g_font, text_now, &(cw_text_style){ .size = 18 * s });
        float x0 = fmaxf(8 * s, fminf(sx - tm.width * 0.5f - 10 * s, W - tm.width - 28 * s)), y0 = fmaxf(g_hud_bot + 6 * s, sy - tm.height - 14 * s);
        float lw = tm.width + 20 * s, lh = tm.height + 12 * s;
        if (!g_label_bug) {
          /* a caption like a sign's, in the boss's own colours: top centre, or just over its health bar when somebody is up there.
           * In a fight everyone is over the boss's head all the time: words that dodged them would never hold still to be read */
          static const char *placed;
          static int slot, hold;
          const float cx = (W - lw) * 0.5f, ys[2] = { g_hud_bot + 6 * s, g_boss_top - 22 * s - lh };
          #define BOSS_WORDS_BLOCKED(i, pad) (label_on_marker(cx, ys[i], lw, lh, pad, W, H, s) || sign_over_player(cx, ys[i], lw, lh, 1.9f, pad, W, H) || box_hit(cx, ys[i], lw, lh, pad))
          if (placed != text_now) placed = text_now, hold = 0, slot = BOSS_WORDS_BLOCKED(0, 16 * s) && !BOSS_WORDS_BLOCKED(1, 16 * s) ? 1 : 0;
          if (hold > 0) hold--;
          if (hold == 0 && BOSS_WORDS_BLOCKED(slot, 8 * s) && !BOSS_WORDS_BLOCKED(!slot, 24 * s)) slot = !slot, hold = 45, g_labelmove_dbg[0]++;
          #undef BOSS_WORDS_BLOCKED
          x0 = cx, y0 = ys[slot];
          if (label_on_marker(x0, y0, lw, lh, 0, W, H, s) || sign_over_player(x0, y0, lw, lh, 1.9f, 0, W, H) || box_hit(x0, y0, lw, lh, 0)) { /* both places taken this moment: not drawn, never on anyone or on other words */
            g_label_dbg[0]++;
            t += 1.0f / 60.0f; /* the words wait */
            continue;
          }
        }
        g_label_dbg[0]++;
        if (label_on_marker(x0, y0, lw, lh, 0, W, H, s)) g_label_dbg[1]++;
        box_add(x0, y0, lw, lh);
        cw_r2d_round_rect(cw_rect_make(x0, y0, lw, lh), 8 * s, 8, cw_rgba(0.1f, 0.05f, 0.05f, 0.9f));
        cw_r2d_text(g_font, text_now, (cw_vec2){ x0 + 10 * s, y0 + 6 * s }, &(cw_text_style){ .size = 18 * s, .color = cw_rgba(1, 0.85f, 0.7f, 1) });
      }
    }
  }
}
/* who said what, over their head; the rest of the screen is not covered */
static void draw_says(float W, float H, float s) {
  for (int i = 0; i < 6; i++) {
    if (SAY[i].t <= 0) continue;
    SAY[i].t -= 1.0f / 60.0f;
    const player *p = &G.p[SAY[i].who];
    float wx = p->bubble ? p->bubble_x : (float)p->pf.body.position.x, wy = p->bubble ? p->bubble_y : (float)p->pf.body.position.y;
    float sx, sy;
    if (!project(wx, wy + 2.6f, 0, W, H, &sx, &sy)) continue;
    float a = SAY[i].t < 0.3f ? SAY[i].t / 0.3f : 1.0f, ts = 15 * s;
    cw_text_metrics sz = cw_font_measure(g_font, SAY[i].text, &(cw_text_style){ .size = ts });
    float pw = sz.width + 18 * s, ph = sz.height + 10 * s, x0 = sx - pw * 0.5f, y0 = sy - ph - 20 * s;
    if (x0 < 8 * s) x0 = 8 * s;
    if (x0 + pw > W - 8 * s) x0 = W - 8 * s - pw;
    if (y0 < g_hud_bot + 6 * s) y0 = g_hud_bot + 6 * s;
    float hx = x0, hy = y0; /* where it would hang over the head */
    if (!g_label_bug) {
      /* off a marker, and never on a body, and it stays that far off its place until its place is well clear: words that hop cannot be read */
      static float off[6][2];
      static const char *said[6];
      if (said[i] != SAY[i].text) said[i] = SAY[i].text, off[i][0] = off[i][1] = 0;
      if (!label_on_marker(x0, y0, pw, ph, 10 * s, W, H, s) && !sign_over_player(x0, y0, pw, ph, 1.9f, 6 * s, W, H) && !box_hit(x0, y0, pw, ph, 6 * s)) off[i][0] = off[i][1] = 0;
      else if (label_on_marker(x0 + off[i][0], y0 + off[i][1], pw, ph, 0, W, H, s) || sign_over_player(x0 + off[i][0], y0 + off[i][1], pw, ph, 1.9f, 0, W, H) ||
               box_hit(x0 + off[i][0], y0 + off[i][1], pw, ph, 0) || (off[i][0] == 0 && off[i][1] == 0)) {
        float qx = x0, qy = y0;
        label_clear_spot(&qx, &qy, pw, ph, true, W, H, s);
        off[i][0] = qx - x0, off[i][1] = qy - y0;
        g_labelmove_dbg[1]++;
      }
      x0 += off[i][0], y0 += off[i][1];
    }
    if (box_hit(x0, y0, pw, ph, 0)) { /* no clear place this moment (large text, a crowded screen): it waits, never on other words */
      SAY[i].t += 1.0f / 60.0f;
      continue;
    }
    g_label_dbg[2]++;
    if (label_on_marker(x0, y0, pw, ph, 0, W, H, s) || (!g_label_bug && sign_over_player(x0, y0, pw, ph, 1.9f, 0, W, H))) g_label_dbg[3]++;
    box_add(x0, y0, pw, ph);
    cw_r2d_round_rect(cw_rect_make(x0, y0, pw, ph), 8 * s, 8, cw_rgba(1, 1, 1, 0.92f * a));
    if (x0 == hx && y0 == hy) { /* the tail only when it still points at the head */
      cw_vec2 tri[3] = { { sx - 5 * s, y0 + ph }, { sx + 5 * s, y0 + ph }, { sx, y0 + ph + 7 * s } };
      cw_r2d_polygon(tri, 3, cw_rgba(1, 1, 1, 0.92f * a));
    }
    cw_r2d_text(g_font, SAY[i].text, (cw_vec2){ x0 + 9 * s, y0 + 5 * s }, &(cw_text_style){ .size = ts, .color = cw_rgba(0.1f, 0.1f, 0.14f, a) });
  }
}
/* project every marker first, then push markers that would overlap apart along the row (players in one spot get
 * one readable marker each, side by side, never a stack) */
static int marker_layout(float W, float H, float s, float *px, float *py, int *id, bool count) {
  int n = 0;
  float r = g_mark_r;
  for (int i = 0; i < G.nplayers && i < MAX_PLAYERS; i++) {
    const player *p = &G.p[i];
    if (!p->joined || (!p->alive && !p->bubble)) continue;
    float wx = p->bubble ? p->bubble_x : (float)p->pf.body.position.x + S.spread_x[i], wy = (p->bubble ? p->bubble_y + 0.9f : (float)p->pf.body.position.y) + 2.15f;
    float sx, sy;
    if (!project(wx, wy, 0, W, H, &sx, &sy)) continue;
    /* held under the player panels: a marker clamped to the top of the view sat on the panels (their bottom is 75 * s) */
    float sy_min = g_hud_bug ? r + 60 * s : g_hud_bot + 1 * s + 1.5f * r;
    if (sy < sy_min) sy = sy_min;
    if (!g_hud_bug && g_chip_bot > 0 && sx + r > g_chip_x - 2 * s && sy - r < g_chip_bot + 2 * s) sy = g_chip_bot + 2 * s + r; /* not on the chips */
    if (count && sy - r < g_hud_bot + 1 * s) g_marker_over_dbg++;
    px[n] = sx, py[n] = sy, id[n] = i, n++;
  }
  for (int a = 0; a < n; a++) /* by screen x, so the spreading keeps the order the players stand in */
    for (int b = a + 1; b < n; b++)
      if (px[b] < px[a]) {
        float t = px[a]; px[a] = px[b], px[b] = t;
        t = py[a]; py[a] = py[b], py[b] = t;
        int k = id[a]; id[a] = id[b], id[b] = k;
      }
  float gap = 2.5f * r;
  for (int pass = 0; pass < 4; pass++)
    for (int a = 0; a + 1 < n; a++) {
      float over = gap - (px[a + 1] - px[a]);
      if (over > 0 && fabsf(py[a + 1] - py[a]) < gap) px[a] -= over * 0.5f, px[a + 1] += over * 0.5f;
    }
  for (int k = 0; k < n; k++) {
    if (px[k] < r) px[k] = r;
    if (px[k] > W - r) px[k] = W - r;
  }
  return n;
}
/* does a box of words touch a player's marker? */
static bool label_on_marker(float x, float y, float w, float h, float pad, float W, float H, float s) {
  float px[MAX_PLAYERS], py[MAX_PLAYERS];
  int id[MAX_PLAYERS], n = marker_layout(W, H, s, px, py, id, false);
  float r = g_mark_r + 2 * s + pad;
  for (int k = 0; k < n; k++)
    if (px[k] + r > x && px[k] - r < x + w && py[k] + r > y && py[k] - r < y + h) return true;
  if (g_sign_box[2] > 0 && g_sign_box[0] + g_sign_box[2] > x && g_sign_box[0] < x + w && g_sign_box[1] + g_sign_box[3] > y && g_sign_box[1] < y + h) return true;
  return false;
}
/* the nearest place to a box of words where it is on no marker (and, when asked, on no player): its own place if it can have it */
static void label_clear_spot(float *x, float *y, float w, float h, bool bodies, float W, float H, float s) {
  const float xmin = 8 * s, xmax = W - 8 * s - w, ymin = g_hud_bot + 6 * s, ymax = g_hint_top - 28 * s - h, x0 = *x, y0 = *y;
  float best = 1e9f;
  for (int v = -14; v <= 14; v++)
    for (int u = -16; u <= 16; u++) {
      float qx = x0 + (float)u * 20 * s, qy = y0 + (float)v * 12 * s, cost = fabsf((float)u) * 20 * s + fabsf((float)v) * 12 * s;
      if (cost >= best || qx < xmin || qx > xmax || qy < ymin || qy > ymax) continue;
      if (!label_on_marker(qx, qy, w, h, 3 * s, W, H, s) && !box_hit(qx, qy, w, h, 3 * s) && !(bodies && sign_over_player(qx, qy, w, h, 1.9f, 2 * s, W, H))) best = cost, *x = qx, *y = qy;
    }
}
static void draw_markers(float W, float H, float s) {
  float px[MAX_PLAYERS], py[MAX_PLAYERS];
  int id[MAX_PLAYERS], n = marker_layout(W, H, s, px, py, id, true);
  float r = g_mark_r;
  for (int k = 0; k < n; k++) {
    int i = id[k];
    float sx = px[k], sy = py[k];
    cw_color c = cw_rgba(PLAYER_RGB[i][0], PLAYER_RGB[i][1], PLAYER_RGB[i][2], 1);
    glyph(i, sx, sy, r + 2 * s, cw_rgba(0, 0, 0, 0.75f)); /* a dark outline so it reads on any background */
    glyph(i, sx, sy, r, c);
    char b[12];
    snprintf(b, sizeof b, "%d", i + 1);
    cw_r2d_text(g_font, b, (cw_vec2){ sx - r, sy - r * 0.62f }, &(cw_text_style){ .size = 15 * s, .color = cw_rgba(0.05f, 0.05f, 0.08f, 1), .align = CW_ALIGN_CENTER, .wrap_width = 2 * r });
  }
}

static void draw_map(float W, float H) {
  float s = H / 540.0f;
  cw_r2d_rect(cw_rect_make(0, 0, W, H), cw_rgba(0.01f, 0.02f, 0.05f, 0.9f));
  int map_w = G.L.width > 0 ? G.L.width / 4 + 2 : WORLD_W / 4;
  float mw = W * 0.9f, cell = mw / (float)map_w;
  float ox = (W - mw) * 0.5f, oy = H * 0.52f - cell * (WORLD_H / 4) * 0.5f;
  /* the words round the map, measured: the level line under the title by the title's drawn height, the hint wrapped
   * to the screen along the bottom and the region names (wrapped as wide as they grow) over it; the map moves up when
   * they would meet it */
  float line30 = cw_font_measure(g_font, "M", &(cw_text_style){ .size = 30 * s }).height, lv_y = H * 0.07f + (line30 - line30 / text_grow(30 * s)) * 1.3f;
  float lv_bot = lv_y + cw_font_measure(g_font, "M", &(cw_text_style){ .size = 24 * s }).height, reg_w = 220 * s * text_grow(16 * s), reg_h = 0;
  for (int b = 0; b < BIOMES; b++) reg_h = fmaxf(reg_h, cw_font_measure(g_font, REGION_NAMES[b], &(cw_text_style){ .size = 16 * s, .wrap_width = reg_w }).height);
  char mhint[256];
  wrap_hint("{left|right} pick a flag you have touched     {up|down} a level you have reached     {jump} travels there     {map} closes", 16 * s, W - 40 * s, mhint, sizeof mhint);
  float hint_y = fminf(H * 0.93f, H - 6 * s - cw_prompt_text_size(g_font, mhint, -1, 16 * s).y), map_h = cell * (WORLD_H / 4);
  if (oy + map_h + 16 * s + reg_h > hint_y - 4 * s) oy = fmaxf(lv_bot + 16 * s, hint_y - 4 * s - reg_h - 16 * s - map_h);
  const save_t *sv = cw_shell_game_save();
  panel(ox - 10 * s, oy - 10 * s, mw + 20 * s, cell * (WORLD_H / 4) + 20 * s, 12 * s, cw_rgba(0.08f, 0.12f, 0.2f, 0.95f), cw_rgba(0.03f, 0.05f, 0.09f, 0.95f), cw_rgba(0.5f, 0.65f, 0.9f, 0.5f));
  for (int by = 0; by < WORLD_H / 4; by++)
    for (int bx = 0; bx < map_w; bx++) {
      if (!G.seen[by][bx] && !sv->seen[by][bx]) continue;
      int best = T_AIR, bc = 0, cnt[9] = { 0 };
      for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) cnt[G.L.t[by * 4 + y][bx * 4 + x]]++;
      for (int t = 1; t < 9; t++)
        if (cnt[t] > bc) bc = cnt[t], best = t;
      float px = ox + (float)bx * cell, py = oy + (float)(WORLD_H / 4 - 1 - by) * cell;
      int bi = G.L.biome[bx * 4];
      cw_color air = lerpc(cw_rgba(0.1f, 0.13f, 0.22f, 1), rgba3(MOODS[bi].fog, 1), 0.25f);
      cw_r2d_rect(cw_rect_make(px, py, cell + 0.5f, cell + 0.5f), best == T_AIR ? air : tile_color(best, bi));
    }
  for (int k = 0; k < G.L.npickups; k++) {
    const pickup *pk = &G.L.pickups[k];
    float px = ox + (float)pk->x / 4 * cell, py = oy + ((float)WORLD_H / 4 - (float)pk->y / 4) * cell;
    if (G.seen[pk->y / 4][pk->x / 4]) {
      float pulse = 1.0f + 0.2f * sinf(g_ui_t * 4);
      cw_r2d_circle((cw_vec2){ px, py }, cell * 1.0f * pulse, 10, pk->taken ? cw_rgba(0.5f, 0.5f, 0.5f, 0.7f) : cw_rgba(0.4f, 0.9f, 1, 0.45f));
      cw_r2d_circle((cw_vec2){ px, py }, cell * 0.55f, 8, pk->taken ? cw_rgba(0.6f, 0.6f, 0.6f, 1) : cw_rgba(0.7f, 1, 1, 1));
    }
  }
  for (int i = 0; i < G.nplayers; i++) {
    const player *p = &G.p[i];
    float px = ox + (float)p->pf.body.position.x / 4 * cell, py = oy + ((float)WORLD_H / 4 - (float)p->pf.body.position.y / 4) * cell;
    cw_r2d_circle((cw_vec2){ px, py }, cell * (1.5f + 0.25f * sinf(g_ui_t * 5 + (float)i)), 16, rgba3(PLAYER_RGB[i], 0.35f));
    portrait((cw_vec2){ px, py }, cell * 1.05f, i, false);
  }
  /* flags already touched: the map is also the shortcut (left and right pick one, Jump travels there) */
  for (int k = 0; k < G.L.nchecks; k++) {
    if (!(G.reached & (1u << k))) continue;
    float px = ox + (float)G.L.checks[k].x / 4 * cell, py = oy + ((float)WORLD_H / 4 - (float)G.L.checks[k].y / 4) * cell;
    bool sel = k == g_map_sel;
    cw_r2d_line((cw_vec2){ px, py }, (cw_vec2){ px, py - cell * 2.6f }, 1.5f, cw_rgba(1, 1, 1, 1));
    cw_vec2 fl[3] = { { px, py - cell * 2.6f }, { px + cell * 1.8f, py - cell * 2.0f }, { px, py - cell * 1.4f } };
    cw_r2d_polygon(fl, 3, sel ? cw_rgba(1, 0.85f, 0.2f, 1) : cw_rgba(0.9f, 0.9f, 0.95f, 1));
    if (sel) cw_r2d_circle_lines((cw_vec2){ px, py - cell }, cell * 2.6f, 2, 20, cw_rgba(1, 0.85f, 0.2f, 1));
  }
  /* region names under the map, coloured by their light */
  for (int b = 0; b < BIOMES; b++) {
    int x0 = -1, x1 = -1;
    for (int x = 0; x < WORLD_W; x++)
      if (G.L.biome[x] == b && x < WORLD_W - 4) { if (x0 < 0) x0 = x; x1 = x; }
    if (x0 < 0) continue;
    float cx = ox + ((float)(x0 + x1) * 0.5f) / 4 * cell;
    text(REGION_NAMES[b], cx - reg_w * 0.5f, oy + map_h + 16 * s, 16 * s, lerpc(rgba3(MOODS[b].sun, 1), cw_rgba(1, 1, 1, 1), 0.4f), CW_ALIGN_CENTER, reg_w);
  }
  text("MAP", 0, H * 0.035f, 30 * s, cw_rgba(1, 0.95f, 0.8f, 1), CW_ALIGN_CENTER, W);
  {
    char lv[160];
    int shown = g_map_level < 0 ? G.level : g_map_level;
    int mine = 0, all = 0; /* big coins: this level's, and the whole game's */
    for (int l = 0; l < NLEVELS && l < 32; l++) {
      int b = sv->medals[l] | (l == G.level ? G.medals : 0), n = (b & 1) + ((b >> 1) & 1) + ((b >> 2) & 1);
      all += n;
      if (l == shown) mine = n;
    }
    snprintf(lv, sizeof lv, "level %d of %d: %s%s", shown + 1, NLEVELS, LEVELS[shown].name, shown != G.level ? "   ({jump} goes there)" : "");
    char bc[96];
    snprintf(bc, sizeof bc, "big coins: %d of 3 here, %d of %d in all", mine, all, 3 * NLEVELS);
    text(bc, W * 0.62f, H * 0.035f + (line30 - cw_font_measure(g_font, "M", &(cw_text_style){ .size = 18 * s }).height) * 0.5f, 18 * s, cw_rgba(1, 0.85f, 0.4f, 1), CW_ALIGN_RIGHT, W * 0.36f);
    ptext(lv, 0, lv_y, 24 * s, cw_rgba(1, 0.92f, 0.7f, 1), CW_ALIGN_CENTER, W);
  }
  ptext(mhint, 0, hint_y, 16 * s, cw_rgba(0.8f, 0.85f, 1, 0.9f), CW_ALIGN_CENTER, W);
}

/* the title screen's picture: a painted dusk meadow with three drifting ridges, a sun, fireflies and the four heroes */
/* Who holds which seat while the join screen is up, so the heroes in the shell's slot cards follow it (the shell reports
 * the players only once the match starts): the screen's own rule, read frame by frame. South or Start takes the first free
 * seat, East leaves; the keyboard joins with Enter or Space and leaves with Esc. */
static void join_seats_track(cw_shell_screen scr) {
  static bool open;
  if (scr != CW_SHELL_JOIN) { open = false; return; }
  if (!open) { open = true; for (int i = 0; i < 4; i++) join_seats[i] = -1; }
  const cw_raw_input *raw = cw_input_raw();
  if (!raw) return;
  for (int pad = 0; pad <= CW_MAX_PADS; pad++) {
    bool kb = pad == CW_MAX_PADS, on = kb || raw->pads[pad].connected;
    uint32_t pr = !on || kb ? 0 : raw->pads[pad].pressed;
    bool in = kb ? raw->keys_pressed[40] || raw->keys_pressed[44] : pr & ((1u << CW_PAD_SOUTH) | (1u << CW_PAD_START));
    bool out = kb ? raw->keys_pressed[41] : pr & (1u << CW_PAD_EAST);
    int seat = -1, free_seat = -1;
    for (int i = 3; i >= 0; i--) {
      if (join_seats[i] == pad) seat = i;
      if (join_seats[i] < 0) free_seat = i;
    }
    if (seat >= 0 && out) join_seats[seat] = -1;
    else if (seat < 0 && in && free_seat >= 0) join_seats[free_seat] = pad;
  }
}

/* where a hero stands in its join slot card: 0.625 of the height, or lower when the shell's words in the card grow (its
 * slot label at 0.36, the pad or join line under it at 0.5 or lower, in units of a twentieth of the height) */
static float join_y(float H, float s) {
  float u = H / 20.0f, ly = fmaxf(H * 0.5f, H * 0.36f + cw_text_drawn_size(g_font, u * 1.4f) * 1.25f);
  return fmaxf(H * 0.625f, ly + cw_text_drawn_size(g_font, u) * 1.25f + 21 * s * 1.6f);
}
static void title_background(cw_shell_screen scr, cw_vec2 cv, void *user) {
  (void)user;
  join_seats_track(scr);
  float W = cv.x, H = cv.y, s = H / 540.0f, t = g_ui_t;
  if (g_attract && G.world.id && S.built) {
    /* the 3D world plays behind the menu: shade the top for the title and the bottom for the hints, nothing else */
    for (int i = 0; i < 48; i++) { /* bands on whole pixels, edge to edge, so no seam is drawn twice */
      float f = (float)i / 48.0f;
      float t0 = floorf(H * 0.42f * f), t1 = floorf(H * 0.42f * (f + 1.0f / 48.0f));
      float b0 = floorf(H - H * 0.3f * (f + 1.0f / 48.0f)), b1 = floorf(H - H * 0.3f * f);
      cw_r2d_rect(cw_rect_make(0, t0, W, t1 - t0), cw_rgba(0.02f, 0.03f, 0.08f, 0.62f * (1 - f) * (1 - f)));
      cw_r2d_rect(cw_rect_make(0, b0, W, b1 - b0), cw_rgba(0.02f, 0.03f, 0.06f, 0.55f * (1 - f) * (1 - f)));
    }
    if (scr == CW_SHELL_JOIN) {
      for (int i = 0; i < 4; i++) {
        bool in = join_seats[i] >= 0;
        float x = W * (0.125f + 0.25f * (float)i), y = join_y(H, s) - (in ? 7 * s * fabsf(sinf(t * 3.0f + (float)i * 0.9f)) : 0);
        portrait((cw_vec2){ x, y }, 21 * s, i, !in);
      }
    }
    return;
  }
  static const float SKY[6][3] = { { 0.1f, 0.12f, 0.35f }, { 0.28f, 0.2f, 0.5f }, { 0.65f, 0.3f, 0.5f }, { 0.95f, 0.5f, 0.35f }, { 1.0f, 0.75f, 0.45f }, { 1.0f, 0.88f, 0.6f } };
  for (int i = 0; i < 80; i++) {
    float f = (float)i / 80.0f * 5.0f;
    int k0 = (int)f > 4 ? 4 : (int)f;
    float fr = f - (float)k0;
    cw_color c = cw_rgba(SKY[k0][0] + (SKY[k0 + 1][0] - SKY[k0][0]) * fr, SKY[k0][1] + (SKY[k0 + 1][1] - SKY[k0][1]) * fr, SKY[k0][2] + (SKY[k0 + 1][2] - SKY[k0][2]) * fr, 1);
    cw_r2d_rect(cw_rect_make(0, H * 0.78f * (float)i / 80.0f, W, H * 0.78f / 80.0f + 1.5f), c);
  }
  cw_r2d_rect(cw_rect_make(0, H * 0.78f, W, H * 0.22f + 1), cw_rgba(0.12f, 0.2f, 0.22f, 1));
  cw_vec2 sun = { W * 0.72f, H * 0.52f };
  for (int k = 6; k >= 1; k--) cw_r2d_circle(sun, 28 * s * (float)k * 0.9f, 28, cw_rgba(1, 0.8f, 0.45f, 0.06f));
  cw_r2d_circle(sun, 34 * s, 28, cw_rgba(1, 0.95f, 0.75f, 1));
  static const float RC[3][3] = { { 0.4f, 0.3f, 0.55f }, { 0.2f, 0.25f, 0.4f }, { 0.08f, 0.14f, 0.2f } };
  for (int l = 0; l < 3; l++) {
    enum { N = 26 };
    cw_vec2 p[N + 2];
    float base = H * (0.58f + 0.1f * (float)l), amp = H * (0.12f - 0.02f * (float)l);
    for (int i = 0; i <= N; i++) {
      float x = W * (float)i / (float)N, ph = x / W * (4.0f + (float)l * 2.0f) + t * (0.02f + 0.015f * (float)l) + (float)l * 1.7f;
      p[i] = (cw_vec2){ x, base - amp * (0.5f + 0.5f * sinf(ph * 2.0f) * cosf(ph * 0.7f + 1.0f)) };
    }
    p[N + 1] = (cw_vec2){ W, H };
    cw_vec2 poly[N + 3];
    poly[0] = (cw_vec2){ 0, H };
    for (int i = 0; i <= N + 1; i++) poly[i + 1] = p[i];
    cw_r2d_polygon(poly, N + 3, rgba3(RC[l], 1));
  }
  for (int i = 0; i < 40; i++) {
    float fx = fmodf((float)i * 97.3f + t * (8.0f + (float)(i % 5) * 3.0f), W), fy = H * (0.45f + 0.5f * fabsf(sinf((float)i * 1.3f + t * 0.3f + (float)i)));
    float a = 0.4f + 0.6f * fabsf(sinf(t * 1.5f + (float)i));
    cw_r2d_circle((cw_vec2){ fx, fy }, 2.2f * s, 6, cw_rgba(1, 0.95f, 0.5f, a));
    cw_r2d_circle((cw_vec2){ fx, fy }, 6 * s, 8, cw_rgba(1, 0.9f, 0.4f, 0.12f * a));
  }
  if (scr == CW_SHELL_JOIN) {
    /* on the join screen each hero stands in its own slot card (the shell draws four cards across, 0.3 to 0.7 of the
     * height, its labels at 0.36 and 0.5), clear of the hint line at the bottom; a joined hero hops, an open slot waits
     * dimmed */
    for (int i = 0; i < 4; i++) {
      bool in = join_seats[i] >= 0;
      float x = W * (0.125f + 0.25f * (float)i), y = join_y(H, s) - (in ? 7 * s * fabsf(sinf(t * 3.0f + (float)i * 0.9f)) : 0);
      cw_r2d_circle((cw_vec2){ x, join_y(H, s) + H * 0.047f }, 22 * s, 16, cw_rgba(0, 0, 0, 0.22f));
      portrait((cw_vec2){ x, y }, 21 * s, i, !in);
    }
    return;
  }
  for (int i = 0; i < 4; i++) {
    float x = W * (0.22f + 0.19f * (float)i), y = H * 0.84f - 8 * s * fabsf(sinf(t * 2.0f + (float)i * 0.9f));
    cw_r2d_circle((cw_vec2){ x, H * 0.9f }, 30 * s, 16, cw_rgba(0, 0, 0, 0.25f));
    portrait((cw_vec2){ x, y }, 26 * s, i, false);
  }
  if (scr == CW_SHELL_PAUSE && G.nplayers) { /* the whole hint line, in pause, whatever has retired in play */
    char hw[256];
    wrap_hint(HINT_FULL, 16 * s, W - 40 * s, hw, sizeof hw);
    float hh = cw_prompt_text_size(g_font, hw, -1, 16 * s).y;
    cw_r2d_rect(cw_rect_make(0, H - hh - 26 * s, W, hh + 26 * s), cw_rgba(0.02f, 0.03f, 0.06f, 0.55f));
    ptext(hw, 20 * s, H - hh - 13 * s, 16 * s, cw_rgba(1, 1, 1, 0.85f), CW_ALIGN_LEFT, 0);
  }
}

/* attract mode behind the title, join and settings screens: the real world, four bots with every ability running
 * the meadow and the first cave, looped. The shell pauses the tree under its menus, which stops fixed_update, so this
 * runs from render, one fixed step per frame. Its sounds stay off; a match start (start_match) replaces it. */
static void attract_step(void) {
  cw_shell_screen scr = cw_shell_screen_now();
  if (scr != CW_SHELL_TITLE && scr != CW_SHELL_JOIN && scr != CW_SHELL_SETTINGS) return;
  if (!g_attract || !G.world.id || G.nplayers == 0 || G.tick > 60 * 75) {
    G.abilities = AB_DOUBLE | AB_CLAWS | AB_DASH;
    G.level = 0; /* the title always shows the meadow */
    sim_reset(&G, MAX_PLAYERS);
    G.abilities = AB_DOUBLE | AB_CLAWS | AB_DASH;
    for (int i = 0; i < G.nplayers; i++) {
      G.p[i].pf.can_dash = G.p[i].pf.can_wall_jump = true, G.p[i].pf.max_air_jumps = 1;
      G.p[i].invuln = 0;
    }
    build_scene();
    g_attract = true;
  }
  music_play(6);
  for (int i = 0; i < MAX_PLAYERS; i++) sim_bot_input(&G, i, &INPUT[i]);
  sim_step(&G, INPUT, 1.0f / 60.0f);
  G.events = 0;
}

static void render(void *user, double alpha) {
  (void)user;
  float dt = 1.0f / 60.0f;
  g_ui_t += dt;
  attract_step();
  if (G.nplayers && G.world.id && S.built) sync_scene((float)alpha, dt);
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  bool hud_on = true;
#ifdef COOP_AUTOPILOT
  { static int nohud = -1; if (nohud < 0) nohud = getenv("COOP_NOHUD") != NULL; hud_on = !nohud; } /* COOP_NOHUD: no HUD at all (key art) */
#endif
  if (hud_on && G.nplayers && G.world.id && !g_attract && cw_shell_hud_shown()) { /* not under the shell's menus */
    /* the layout from the drawn text: panels as tall as their coin count, the marker as big as its number, the hint
     * line wrapped to the screen and lifted off its bottom edge */
    memcpy(g_caption_prev, g_caption_box, sizeof g_caption_box);
    g_caption_box[2] = 0;
    g_nbox = 0;
    const float k_coin = text_grow(18 * s), ph_all = 58 * s + 22 * s * (k_coin - 1);
    g_hud_bot = 16 * s + ph_all;
    g_mark_r = 11 * s * text_grow(15 * s);
    g_boss_top = H - 58 * s - 24 * s * text_grow(17 * s);
    char hint[256];
    int hint_lines = wrap_hint(HINT_FULL, 16 * s, W - 40 * s, hint, sizeof hint); /* the layout keeps the full line's room */
    float hint_h = cw_prompt_text_size(g_font, hint, -1, 16 * s).y;
    g_hint_top = H - 13 * s - 19 * s * text_grow(16 * s) - (hint_lines > 1 ? hint_h - hint_h / (float)hint_lines : 0);
    /* a panel per player: face, hearts, coins */
    for (int i = 0; i < G.nplayers; i++) {
      const player *p = &G.p[i];
      float x0 = 20 * s + (float)i * 176 * s, y0 = 16 * s, pw = 166 * s, ph = ph_all;
      panel(x0, y0, pw, ph, 12 * s, cw_rgba(0.16f, 0.2f, 0.3f, 0.82f), cw_rgba(0.04f, 0.05f, 0.09f, 0.82f), rgba3(PLAYER_RGB[i], p->alive || p->bubble ? 0.9f : 0.3f));
      portrait((cw_vec2){ x0 + 28 * s, y0 + 34 * s }, 17 * s, i, !(p->alive || p->bubble));
      for (int h = 0; h < 3; h++) {
        cw_vec2 hc = { x0 + (70 + 26 * (float)h) * s, y0 + 19 * s };
        if (h < p->hp) {
          heart(hc, 9 * s, cw_rgba(1, 0.3f, 0.38f, 1));
          cw_r2d_circle((cw_vec2){ hc.x - 4 * s, hc.y - 4 * s }, 2.2f * s, 6, cw_rgba(1, 0.85f, 0.9f, 0.9f));
        } else heart(hc, 9 * s, cw_rgba(0.25f, 0.27f, 0.36f, 0.9f));
      }
      char b[32];
      snprintf(b, sizeof b, "%d", p->coins);
      coin_icon((cw_vec2){ x0 + 78 * s, y0 + 31 * s + 13 * s * k_coin }, 7 * s);
      text(b, x0 + 90 * s, y0 + 31 * s, 18 * s, cw_rgba(1, 0.95f, 0.8f, 1), CW_ALIGN_LEFT, 0);
    }
    /* the abilities: a chip each, lit when found, as wide as its word; below the panels when they would meet them */
    static const char *const AB[3] = { "AIR JUMP", "CLAWS", "DASH" };
    float chip_w[3], chips = 0, k_chip = text_grow(14 * s);
    for (int k = 0; k < 3; k++) chip_w[k] = fmaxf(76 * s, cw_font_measure(g_font, AB[k], &(cw_text_style){ .size = 14 * s }).width + 16 * s), chips += chip_w[k] + 6 * s;
    float panels_right = 20 * s + (float)G.nplayers * 176 * s - 10 * s, cx = W - 22 * s - chips + 6 * s;
    float chip_y = cx < panels_right + 8 * s ? g_hud_bot + 10 * s : 20 * s;
    for (int k = 0; k < 3; k++) {
      bool on = (G.abilities >> k) & 1;
      float cw_ = chip_w[k], x = cx, y = chip_y;
      cx += cw_ + 6 * s;
      if (caption_over(x, y, cw_, 30 * s * k_chip)) continue; /* a sign's words are up over it: they come first */
      if (G.won && !g_card_bug && chip_y > 20 * s) continue; /* under the panels the results card would cut them in half */
      panel(x, y, cw_, 30 * s * k_chip, 10 * s, on ? cw_rgba(0.2f, 0.45f, 0.6f, 0.9f) : cw_rgba(0.12f, 0.14f, 0.2f, 0.6f), on ? cw_rgba(0.05f, 0.12f, 0.2f, 0.9f) : cw_rgba(0.05f, 0.06f, 0.09f, 0.6f), on ? cw_rgba(0.5f, 0.9f, 1, 0.8f) : cw_rgba(0.4f, 0.4f, 0.5f, 0.3f));
      text(AB[k], x, y + 7 * s * k_chip, 14 * s, on ? cw_rgba(0.85f, 0.98f, 1, 1) : cw_rgba(0.5f, 0.55f, 0.65f, 0.8f), CW_ALIGN_CENTER, cw_);
    }
    /* chips under the panels push the words below them down by what the chips grow (nothing at 100%, where the words
     * keep their places) */
    { /* the level's three big coins, under the chips: filled when taken in this run, ringed when taken in an earlier one */
      const save_t *sv = cw_shell_game_save();
      int had = G.level >= 0 && G.level < 32 ? sv->medals[G.level] : 0;
      float my = chip_y + 30 * s * k_chip + 16 * s, mx = W - 22 * s - 9 * s;
      if (!(G.won && !g_card_bug && chip_y > 20 * s) && !caption_over(mx - 60 * s, my - 10 * s, 70 * s, 20 * s))
        for (int k = 2; k >= 0; k--, mx -= 24 * s) {
          bool got = (G.medals >> k) & 1, before = (had >> k) & 1;
          cw_r2d_circle((cw_vec2){ mx, my }, 9 * s, 16, got ? cw_rgba(1, 0.82f, 0.25f, 1) : cw_rgba(0.08f, 0.09f, 0.14f, 0.7f));
          cw_r2d_circle_lines((cw_vec2){ mx, my }, 9 * s, 1.5f * s, 16, got || before ? cw_rgba(1, 0.9f, 0.5f, 1) : cw_rgba(0.6f, 0.62f, 0.7f, 0.7f));
          if (got) cw_r2d_circle((cw_vec2){ mx - 3 * s, my - 3 * s }, 2.5f * s, 8, cw_rgba(1, 1, 0.85f, 0.9f));
        }
    }
    g_chip_x = W - 22 * s - chips + 6 * s, g_chip_bot = chip_y > 20 * s ? chip_y + 30 * s * k_chip : 0;
    if (chip_y > 20 * s) g_hud_bot += 30 * s * (k_chip - 1);
    /* the level you are on, fading in at the top when it starts */
    {
      static int last_lv = -1;
      static float lv_t = 0;
      if (G.level != last_lv) last_lv = G.level, lv_t = 4.2f;
      if (G.tick < 3) lv_t = 4.2f;
      if (lv_t > 0) {
        lv_t -= dt;
        float a = lv_t > 3.6f ? (4.2f - lv_t) / 0.6f : lv_t < 0.8f ? lv_t / 0.8f : 1.0f;
        const level_info *li = &LEVELS[G.level];
        float y1 = H * 0.17f + fmaxf(0, fmaxf(g_hud_bot, g_chip_bot) + 6 * s - H * 0.17f), y2 = y1 + H * 0.035f * text_grow(19 * s); /* under the panels, each line under the last */
        float lv_h = y2 + 48 * s * text_grow(36 * s) - y1;
        if (caption_over(0, y1, W, lv_h)) { float d = g_hint_top - 8 * s - lv_h - y1; y1 += d, y2 += d; if (caption_over(0, y1, W, lv_h)) a = 0; } /* a sign's words are up there: over the hint line instead */
        text(WORLD_NAMES[li->world], 0, y1, 19 * s, cw_rgba(1, 0.9f, 0.7f, a), CW_ALIGN_CENTER, W);
        text(li->name, 0, y2, 36 * s, cw_rgba(1, 0.96f, 0.85f, a), CW_ALIGN_CENTER, W);
        cw_r2d_rect(cw_rect_make(W * 0.5f - 90 * s, y2 + 46 * s * text_grow(36 * s), 180 * s, 2 * s), cw_rgba(1, 0.85f, 0.5f, a * 0.8f));
      }
    }
    /* boss bar */
    for (int i = 0; i < G.L.nenemies; i++)
      if (G.L.enemies[i].kind == 2 && G.L.enemies[i].alive && fabsf(G.cam.x - G.L.enemies[i].x) < 30) {
        float bw = W * 0.5f, bx = (W - bw) * 0.5f, by = H - 52 * s;
        float f = (float)G.L.enemies[i].hp / (float)(5 + G.L.boss_kind);
        panel(bx - 6 * s, by - 6 * s, bw + 12 * s, 28 * s, 10 * s, cw_rgba(0.2f, 0.1f, 0.1f, 0.9f), cw_rgba(0.06f, 0.03f, 0.03f, 0.9f), cw_rgba(1, 0.5f, 0.3f, 0.6f));
        cw_r2d_round_rect(cw_rect_make(bx, by, bw * f, 16 * s), 7 * s, 8, cw_rgba(0.8f, 0.12f, 0.1f, 1));
        cw_r2d_round_rect(cw_rect_make(bx, by, bw * f, 7 * s), 5 * s, 8, cw_rgba(1, 0.5f, 0.3f, 0.9f));
        {
          static const char *const BOSS[5] = { "THE LANDLORD", "THE TAX GOLEM", "THE LIFEGUARD", "THE REGIONAL MANAGER", "THE FOUNDER" };
          text(BOSS[G.L.boss_kind], 0, g_boss_top, 17 * s, cw_rgba(1, 0.85f, 0.7f, 1), CW_ALIGN_CENTER, W);
        }
      }
    /* the results card is up: the words that hang in the world would be cut by its edges or sit behind it */
    if (!G.won || g_card_bug) {
      draw_graves(W, H, s);
      draw_signs(W, H, s);
      draw_boss_says(W, H, s); /* before what players say: their words keep off the boss's */
      draw_says(W, H, s);
    }
    draw_markers(W, H, s);
    if (g_nbox > 0) { /* the words gate: the caption off every marker, no two boxes of words on each other */
      g_words_dbg[0]++;
      if (g_caption_box[2] > 0) {
        float px[MAX_PLAYERS], py[MAX_PLAYERS], r = g_mark_r + 2 * s;
        int id[MAX_PLAYERS], n = marker_layout(W, H, s, px, py, id, false);
        const float *b = g_caption_box;
        for (int k = 0; k < n; k++)
          if (px[k] + r > b[0] && px[k] - r < b[0] + b[2] && py[k] + r > b[1] && py[k] - r < b[1] + b[3]) { g_words_dbg[1]++; break; }
      }
      bool stack = false;
      for (int i = 0; i < g_nbox && !stack; i++)
        for (int j = i + 1; j < g_nbox && !stack; j++) {
          const float *a = g_boxes[i], *b = g_boxes[j];
          stack = a[0] < b[0] + b[2] && b[0] < a[0] + a[2] && a[1] < b[1] + b[3] && b[1] < a[1] + a[3];
        }
      if (stack) g_words_dbg[2]++;
    }
#ifndef COOP_AUTOPILOT /* the bots play the autopilot: nobody to tell, and each host would draw its own device's glyphs into the compared frames */
    if (!g_map_open) {
      /* each half of the hint retires once it has been done: a running jump that went far, a return to the flag (or
       * the third level reached without one). Both stay in the pause menu (title_background) */
      int mask = hint_mask();
      const char *h = mask == 3 ? HINT_FULL : mask == 1 ? HINT_RUN : mask == 2 ? HINT_RETRY : NULL;
      if (h) {
        char hw[256];
        wrap_hint(h, 16 * s, W - 40 * s, hw, sizeof hw);
        ptext(hw, 20 * s, g_hint_top, 16 * s, cw_rgba(1, 1, 1, 0.45f), CW_ALIGN_LEFT, 0);
        g_hint_dbg[0]++;
      }
      g_hint_dbg[1] = mask;
    }
#endif
    /* players carried in bubbles: a tag each (face and RESCUE), laid out side by side from the measured text and
     * centred, so four tags never run into each other (a fixed 90 px step by player number overlapped them) */
    {
      int who[MAX_PLAYERS], n = 0;
      for (int i = 0; i < G.nplayers; i++)
        if (G.p[i].bubble && G.p[i].joined) who[n++] = i;
      float tw = cw_font_measure(g_font, "RESCUE", &(cw_text_style){ .size = 18 * s }).width;
      float tag = 15 * s * 2 + 6 * s + tw, gap = 24 * s; /* face, a little space, the word */
      if (g_hud_bug) tag = 90 * s, gap = 0; /* the HUD gate's control (native): the old fixed step */
      float x0 = W * 0.5f - ((float)n * tag + (float)(n - 1) * gap) * 0.5f;
      float prev_end = -1e9f;
      float tag_y = fminf(H - 110 * s, g_hint_top - 8 * s - 21.6f * s * text_grow(18 * s)); /* over the hint line */
      if (n > 1) g_tags_dbg[0]++;
      bool ov = false;
      for (int k = 0; k < n; k++) {
        int i = who[k];
        float x = x0 + (float)k * (tag + gap); /* the tag's left edge */
        portrait((cw_vec2){ x + 15 * s, tag_y + 10 * s * text_grow(18 * s) }, 15 * s, i, false);
        text("RESCUE", x + 36 * s, tag_y, 18 * s, rgba3(PLAYER_RGB[i], 1), CW_ALIGN_LEFT, 0);
        if (x < prev_end) ov = true;
        prev_end = x + 36 * s + tw; /* where the drawn word really ends */
      }
      if (ov) g_tags_dbg[1]++;
    }
    if (hat_new_t > 0) {
      hat_new_t--;
      const save_t *sv = cw_shell_game_save();
      char hb[96];
      snprintf(hb, sizeof hb, "ALL THE SECRETS. YOU HAVE BEEN ISSUED: %s", HAT_NAMES[(sv->hats - 1) % 6]);
      text(hb, 0, H * 0.12f + (g_hud_bot - 74 * s), 20 * s, cw_rgba(1, 0.95f, 0.6f, (float)(hat_new_t > 30 ? 1 : hat_new_t) / (hat_new_t > 30 ? 1 : 30)), CW_ALIGN_CENTER, W);
    }
    if (G.fake_t > 0 && !G.won) { /* it looks exactly like the end of a level, for a moment */
      float k = fminf(1.0f, (float)(150 - G.fake_t) / 20.0f);
      panel(0, H * 0.16f, W, H * 0.17f * k, 0, cw_rgba(0.1f, 0.08f, 0.02f, 1), cw_rgba(0.02f, 0.02f, 0.03f, 1), cw_rgba(1, 0.8f, 0.3f, 0.7f)); /* solid, like the card it pretends to be */
      if (k > 0.9f) {
        text(G.fake_t > 70 ? "LEVEL COMPLETE" : "NO.", 0, H * 0.18f, 46 * s, cw_rgba(1, 0.85f, 0.3f, 1), CW_ALIGN_CENTER, W);
      }
    }
    if (G.won) {
      float k = fminf(1.0f, (float)(G.tick - G.win_tick) / 40.0f);
      /* the portraits sit on a row of their own, under the last line, with their hats and aerials (about 1.75 of the radius above the centre) clear of it */
      const float line_w = g_card_bug ? 0.8f : 0.92f;
      const float card_a = g_card_bug ? 0.88f : 1.0f; /* solid: nothing of the level shows through the words */
      /* the words, measured first: each row keeps the room it has at 100% and takes what its text grows by (a bigger
       * text size, a row that wraps), and the card grows to hold them (it moves up when it would run off the screen) */
      char l[200], rp[220], gr[120], t[96];
      const char *title = G.level + 1 >= NLEVELS ? "THE END. PLEASE LEAVE QUIETLY." : LEVELS[G.level].name;
      snprintf(l, sizeof l, LEVELS[G.level].line, G.coins_total, G.coins_total + 1);
      {
        /* the incident report: everything that happened here, by what did it. Whoever finished it is not excused */
        static const char *const WHAT[DIE_KINDS] = { "spikes", "lava", "gravity", "wildlife", "the management", "gardening", "health and safety", "falling objects", "a coin", "a large round colleague", "a coworker's shell" };
        int n = snprintf(rp, sizeof rp, "INCIDENT REPORT: ");
        bool any = false;
        for (int c = 0; c < DIE_KINDS; c++) if (cause_counts[c] && n < 190) n += snprintf(rp + n, sizeof rp - (size_t)n, "%s%s x%d", any ? ", " : "", WHAT[c], cause_counts[c]), any = true;
        if (!any) snprintf(rp + n, sizeof rp - (size_t)n, "nothing. We will be looking into this.");
      }
      int bet = 0, thr = 0;
      for (int i = 0; i < G.nplayers; i++) bet += G.betrayals[i], thr += G.throws[i];
      snprintf(gr, sizeof gr, "friends landed on: %d. friends thrown: %d. Nobody is keeping score. It is written here.", bet, thr);
      snprintf(t, sizeof t, "coins %d     big coins %d of 3     secrets %d     rescues %d     falls %d", G.coins_total, (G.medals & 1) + ((G.medals >> 1) & 1) + ((G.medals >> 2) & 1), G.secrets, G.rescues, G.deaths);
      /* rows: title, report, stats, closing line, friends; their offsets from the card's top at 100% (share of H) */
      const char *row_text[5] = { title, rp, t, l, gr };
      const float row_size[5] = { 46 * s, 17 * s, 24 * s, 24 * s, 15 * s }, row_wrap[5] = { W, W * 0.84f, W, W * line_w, W * 0.84f };
      const float row_off[6] = { 0.03f, 0.115f, 0.18f, 0.25f, 0.32f, 0.415f }; /* the last: the portraits' centre */
      float row_y[6], extra[5];
      for (int r = 0; r < 5; r++) {
        float h = cw_font_measure(g_font, row_text[r], &(cw_text_style){ .size = row_size[r], .wrap_width = row_wrap[r] }).height;
        float one = cw_font_measure(g_font, "M", &(cw_text_style){ .size = row_size[r] }).height / text_grow(row_size[r]); /* a line at 100% */
        extra[r] = g_card_bug || (r == 4 && bet + thr == 0) ? 0 : fmaxf(0, h - one);
      }
      float card_top = H * 0.28f;
      row_y[0] = H * row_off[0];
      for (int r = 1; r < 6; r++) row_y[r] = row_y[r - 1] + H * (row_off[r] - row_off[r - 1]) + extra[r - 1];
      if (g_card_bug) row_y[5] = H * (0.645f - 0.28f);
      /* the tally under the portraits: each player's own column, then the awards (tally_rows) */
      const float tl_size = 14 * s, tl_line = cw_font_measure(g_font, "M", &(cw_text_style){ .size = tl_size }).height * 1.1f;
      const float aw_size = 16 * s, aw_line = cw_font_measure(g_font, "M", &(cw_text_style){ .size = aw_size, .wrap_width = W * 0.9f }).height;
      char awards[200];
      tally_awards(awards, sizeof awards);
      const float aw_h = awards[0] ? cw_font_measure(g_font, awards, &(cw_text_style){ .size = aw_size, .wrap_width = W * 0.9f }).height : 0;
      const float tally_h = g_tally_off ? 0 : 22 * s + 4 * tl_line + (awards[0] ? 8 * s + aw_h : 0);
      (void)aw_line;
      float card_h = g_card_bug ? H * 0.42f : row_y[5] + H * 0.035f + tally_h;
      if (card_top + card_h > H - 4 * s) card_top = fmaxf(g_hud_bot + 4 * s, H - 4 * s - card_h); /* never over the player panels */
      for (int r = 0; r < 6; r++) row_y[r] += card_top;
      panel(0, card_top, W, card_h * k, 0, cw_rgba(0.1f, 0.08f, 0.02f, card_a), cw_rgba(0.02f, 0.02f, 0.03f, card_a), cw_rgba(1, 0.8f, 0.3f, 0.7f));
      if (k > 0.9f) {
        text(title, 0, row_y[0], 46 * s, cw_rgba(1, 0.85f, 0.3f, 1), CW_ALIGN_CENTER, W);
        {
          text(l, W * (1 - line_w) * 0.5f, row_y[3], 24 * s, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER, W * line_w);
          /* no level's closing line may take two rows: the second would run into the line below it */
          float row = cw_font_measure(g_font, "M", &(cw_text_style){ .size = 24 * s }).height;
          g_card_dbg[2] = 0;
          for (int i = 0; i < NLEVELS; i++) {
            char li[200];
            snprintf(li, sizeof li, LEVELS[i].line, 123, 124);
            if (cw_font_measure(g_font, li, &(cw_text_style){ .size = 24 * s, .wrap_width = W * line_w }).height > row * 1.5f) g_card_dbg[2]++;
          }
        }
        text(rp, W * 0.08f, row_y[1], 17 * s, cw_rgba(1, 0.75f, 0.65f, 1), CW_ALIGN_CENTER, W * 0.84f);
        if (bet + thr > 0) text(gr, W * 0.08f, row_y[4], 15 * s, cw_rgba(0.8f, 0.9f, 1, 1), CW_ALIGN_CENTER, W * 0.84f);
        float stats_w = cw_font_measure(g_font, t, &(cw_text_style){ .size = 24 * s, .wrap_width = W }).width;
        coin_icon((cw_vec2){ fmaxf(14 * s, fminf(W * 0.5f - 250 * s, W * 0.5f - stats_w * 0.5f - 18 * s)), row_y[2] + H * 0.025f * text_grow(24 * s) }, 11 * s);
        text(t, 0, row_y[2], 24 * s, cw_rgba(1, 1, 1, 1), CW_ALIGN_CENTER, W);
        g_card_dbg[0]++;
        if (row_y[5] - 15 * s * 1.75f < row_y[4] + cw_font_measure(g_font, "M", &(cw_text_style){ .size = 15 * s }).height) g_card_dbg[1]++;
        const float col = g_tally_off ? 64 * s : fminf(150 * s, W * 0.9f / (float)(G.nplayers > 0 ? G.nplayers : 1));
        for (int i = 0; i < G.nplayers; i++) {
          float px = W * 0.5f + ((float)i - (float)(G.nplayers - 1) * 0.5f) * col;
          portrait((cw_vec2){ px, row_y[5] }, 15 * s, i, false);
          if (g_tally_off) continue;
          /* the player's own count, in the player's colour: coins, stomps, friends thrown, falls */
          char ln[4][32];
          snprintf(ln[0], sizeof ln[0], "coins %d", G.p[i].coins);
          snprintf(ln[1], sizeof ln[1], "stomps %d", G.stomps[i]);
          snprintf(ln[2], sizeof ln[2], "thrown %d", G.throws[i]);
          snprintf(ln[3], sizeof ln[3], "falls %d", G.falls[i]);
          for (int r = 0; r < 4; r++) text(ln[r], px - col * 0.5f, row_y[5] + 22 * s + (float)r * tl_line, tl_size, r == 0 ? rgba3(PLAYER_RGB[i], 1) : cw_rgba(0.92f, 0.92f, 0.95f, 1), CW_ALIGN_CENTER, col);
          g_tally_dbg[0]++;
        }
        if (!g_tally_off && awards[0]) text(awards, W * 0.05f, row_y[5] + 22 * s + 4 * tl_line + 8 * s, aw_size, cw_rgba(1, 0.85f, 0.4f, 1), CW_ALIGN_CENTER, W * 0.9f), g_tally_dbg[1]++;
      }
    }
    if (g_map_open) { draw_map(W, H); g_map_seen_dbg = 1; }
  }
  cw_r2d_end();
  cw_shell_draw();
}

static void shutdown_app(void *user) {
  (void)user;
  world_destroy_physics(&G);
  if (A.on) cw_audio_shutdown();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc base = { .title = "Co-op Platformer", .width = 960, .height = 540, .debug = true, .init = init,
                              .fixed_update = fixed_update, .render = render, .shutdown = shutdown_app };
  base.save = cw_shell_save_region();
  base.save_size = cw_shell_save_region_size();
  return cw_scene_app(NULL, &base);
}
