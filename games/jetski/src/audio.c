/* The sound of it: the engine's rasp follows the throttle and the speed and climbs when the pump clears the water; the
 * hull's hiss follows the speed and stops in the air; wind rises with speed; the hull slaps on chop. The three nearest
 * rivals are heard where they are, with their own engines and the doppler of a pass. Every event has its sound, each
 * course its own music, the menu its ticks, and a limiter on the master keeps a pile-up from clipping. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/resource.h"
#include "cartwheel/shell.h"
#include "game.h"

enum { S_ENGINE, S_HULL, S_BIG, S_SMALL, S_OK, S_TOP, S_MISS, S_COUNT, S_GO, S_WIND, S_SLAP, S_WHOOSH, S_PASSBY, S_LAP, S_FINAL, S_STYLE, S_FINISH, S_FINISH_LOW,
       S_MENU_MOVE, S_MENU_OK, S_MENU_BACK, S_MUSIC0, S_N = S_MUSIC0 + LEVELS };
static const char *const FILES[S_N] = { "engine", "hull", "splash_big", "splash_small", "buoy_ok", "buoy_top", "buoy_miss", "count", "go", "wind", "slap", "whoosh", "pass_by", "lap", "final_lap", "style",
                                        "finish", "finish_low", "menu_move", "menu_ok", "menu_back" };
/* each course's own loop, by its id (Crater Atoll keeps the first one) */
static const char *const MUSIC[LEVELS][2] = { { "atoll", "music" }, { "sound", "music_sound" }, { "canyon", "music_canyon" }, { "harbour", "music_harbour" }, { "storm", "music_storm" } };
#define RIVAL_VOICES 3
static cw_res res[S_N];
static cw_sound snd[S_N];
static cw_voice v_engine, v_hull, v_wind, v_music, v_rival[RIVAL_VOICES];
static int music_level = -1;
static float rev, hiss, rival_rev[RACERS], slap_cool;
static int last_count = 99, last_state = -1;
static bool was_air;
static float last_vy;
int g_menu_sfx;            /* menu.c: 1 a move, 2 accept, 3 back */
int32_t g_snd_shots, g_snd_kind[S_N], g_music_on = -1, g_rival_heard;

static void load(int i, const char *name, bool stream) {
  char path[64];
  snprintf(path, sizeof path, "audio/%s.wav", name);
  res[i] = cw_res_load(path, CW_RES_AUDIO_CLIP);
  snd[i] = cw_res_audio_sound(res[i], &(cw_sound_desc){ .mode = stream ? CW_SOUND_STREAM : CW_SOUND_SAMPLE, .bus = stream ? cw_shell_music_bus() : cw_shell_sfx_bus() });
}
int g_audio_old; /* sw audio_old 1: only the sounds the game had before this pass, the sound gate control */
static cw_voice shot(int k, float vol, float pitch) {
  if (!snd[k].id || (g_audio_old && k >= S_WIND && k < S_MUSIC0)) return (cw_voice){ 0 };
  cw_voice v = cw_audio_play(snd[k], &(cw_play_desc){ .volume = vol, .bus = cw_shell_sfx_bus() });
  if (pitch != 1.0f) cw_voice_set_pitch(v, pitch);
  g_snd_shots++, g_snd_kind[k]++;
  return v;
}

void audio_init(void) {
  for (int i = 0; i < S_MUSIC0; i++) load(i, FILES[i], false);
  for (int l = 0; l < LEVELS; l++) load(S_MUSIC0 + l, MUSIC[l][1], true);
  cw_bus s = cw_shell_sfx_bus();
  v_engine = cw_audio_play(snd[S_ENGINE], &(cw_play_desc){ .loop = true, .volume = 0, .bus = s });
  v_hull = cw_audio_play(snd[S_HULL], &(cw_play_desc){ .loop = true, .volume = 0, .bus = s });
  v_wind = cw_audio_play(snd[S_WIND], &(cw_play_desc){ .loop = true, .volume = 0, .bus = s });
  /* the rivals: the same engine loop, heard from where each one is, louder close by and bent by its speed past you */
  for (int k = 0; k < RIVAL_VOICES; k++)
    v_rival[k] = cw_audio_play(snd[S_ENGINE], &(cw_play_desc){ .loop = true, .volume = 0, .bus = s, .offset = 9000 * k, .spatial = true,
      .space = { .model = CW_ATTEN_INVERSE, .ref_distance = 6, .max_distance = 260, .rolloff = 1, .doppler = 1, .mode = CW_SPATIAL_PAN, .air_absorption = 0.01f } });
  /* the master's ceiling: a crash, a splash and a sting on one frame do not clip */
  cw_bus_add_fx(0, &(cw_fx_desc){ .type = CW_FX_LIMITER, .limiter = { .ceiling_db = -1.0f, .release_s = 0.12f } });
}

static int music_index(int level) {
  for (int l = 0; l < LEVELS; l++) if (!strcmp(COURSES[level]->id, MUSIC[l][0])) return l;
  return 0;
}

void audio_tick(float dt) {
  const craft *c = &G.c[0];
  bool quiet = G.state == RACE_ATTRACT;
  /* the course's own music, crossfaded when the course changes */
  if (G.level != music_level) {
    if (music_level >= 0) cw_voice_stop(v_music, 1.2f);
    music_level = G.level;
    g_music_on = music_index(G.level);
    v_music = cw_audio_play(snd[S_MUSIC0 + g_music_on], &(cw_play_desc){ .loop = true, .fade_in = 2.0f, .volume = 0.5f, .bus = cw_shell_music_bus() });
  }
  float target = c->air ? 1.15f : 0.45f + 0.75f * (c->speed / 30.0f);
  target += 0.2f * c->in.throttle;
  rev += (target - rev) * (dt * (c->air ? 6.0f : 3.0f));
  hiss += ((c->air ? 0.0f : 0.1f + 0.7f * (c->speed / 30.0f)) - hiss) * (dt * 10.0f);
  cw_voice_set_pitch(v_engine, 0.55f + 1.25f * rev);
  cw_voice_set_volume(v_engine, quiet ? 0.0f : 0.2f + 0.26f * c->in.throttle);
  cw_voice_set_volume(v_hull, quiet ? 0.0f : hiss * 0.5f);
  cw_voice_set_pitch(v_hull, 0.75f + 0.5f * (c->speed / 30.0f));
  /* wind past the helmet: nothing at a crawl, a roar at full speed and in the air */
  { float w = c->speed > 8 ? (c->speed - 8) / 24.0f : 0;
    w = w > 1.2f ? 1.2f : w;
    cw_voice_set_volume(v_wind, quiet ? 0.0f : 0.32f * w * w + (c->air ? 0.12f : 0));
    cw_voice_set_pitch(v_wind, 0.8f + 0.35f * (c->speed / 30.0f)); }
  /* the listener rides with the camera; the rivals nearest it are heard */
  { float fx = G.look[0] - G.cam[0], fy = G.look[1] - G.cam[1], fz = G.look[2] - G.cam[2], fl = sqrtf(fx * fx + fy * fy + fz * fz);
    if (fl > 1e-3f) cw_audio_set_listener((t3_vec3){ G.cam[0], G.cam[1], G.cam[2] }, (t3_vec3){ c->vx, c->vy, c->vz }, (t3_vec3){ fx / fl, fy / fl, fz / fl }, (t3_vec3){ 0, 1, 0 }); }
  { int pick[RIVAL_VOICES], n = 0;
    float d2[RACERS];
    for (int i = 1; i < RACERS; i++) { float dx = G.c[i].x - G.cam[0], dz = G.c[i].z - G.cam[2]; d2[i] = racer_on(i) ? dx * dx + dz * dz : 1e18f; }
    for (int k = 0; k < RIVAL_VOICES; k++) {
      int best = -1;
      for (int i = 1; i < RACERS; i++) { bool used = false; for (int j = 0; j < n; j++) used |= pick[j] == i; if (!used && d2[i] < 1e17f && (best < 0 || d2[i] < d2[best])) best = i; }
      if (best >= 0) pick[n++] = best;
    }
    g_rival_heard = 0;
    for (int k = 0; k < RIVAL_VOICES; k++) {
      if (k >= n || quiet) { cw_voice_set_volume(v_rival[k], 0); continue; }
      const craft *o = &G.c[pick[k]];
      float t = o->air ? 1.15f : 0.45f + 0.75f * (o->speed / 30.0f) + 0.2f;
      rival_rev[pick[k]] += (t - rival_rev[pick[k]]) * (dt * 3.0f);
      cw_voice_set_position(v_rival[k], (t3_vec3){ o->x, o->y + 0.6f, o->z }, (t3_vec3){ o->vx, o->vy, o->vz });
      cw_voice_set_pitch(v_rival[k], (0.55f + 1.25f * rival_rev[pick[k]]) * (0.94f + 0.04f * (float)(pick[k] % 4)));
      cw_voice_set_volume(v_rival[k], o->spill > 0 ? 0.08f : 0.42f);
      if (d2[pick[k]] < 60.0f * 60.0f) g_rival_heard++;
    } }
  /* the hull on the water: a slap landing a hop too small for a splash, and on the chop when a face kicks it up */
  slap_cool -= dt;
  if (!quiet && G.state != RACE_DONE) {
    if (was_air && !c->air && !c->big_splash && c->spill <= 0) { shot(S_SLAP, 0.3f + 0.04f * fminf(fabsf(last_vy), 8.0f), 0.9f + 0.002f * (float)(G.tick % 64)); slap_cool = 0.15f; }
    else if (!c->air && c->speed > 10 && c->vy - last_vy > 0.55f && slap_cool <= 0) { shot(S_SLAP, 0.22f + 0.08f * fminf(c->vy - last_vy, 3.0f), 0.85f + 0.004f * (float)(G.tick % 64)); slap_cool = 0.22f; }
  }
  was_air = c->air, last_vy = c->vy;
  /* every verb speaks: the event list */
  { static unsigned read;
    if (g_ev_n - read > EV_RING) read = g_ev_n - EV_RING;
    for (; read != g_ev_n; read++) {
      const game_event *e = &g_ev[read % EV_RING];
      if (!g_ev_on) continue;
      bool heard = true;
      switch (e->kind) {
        case EV_PASS: shot(S_OK, 0.5f, 1.0f + 0.05f * (float)e->a); break;
        case EV_TOP: shot(S_TOP, 0.75f, 1.0f); break;
        case EV_TRICK:
          if (e->a == 5) shot(S_STYLE, 0.5f, 1.0f);
          else { shot(S_WHOOSH, 0.7f, e->a == 4 ? 1.2f : 1.0f); shot(S_TOP, 0.5f, 1.25f); }
          break;
        case EV_LAND: if (e->a) shot(S_TOP, 0.5f, 0.85f); else shot(S_SMALL, 0.45f, 1.0f); break;
        case EV_MISS: shot(S_MISS, 0.7f, 1.0f); break;
        case EV_GAIN: shot(S_PASSBY, 0.6f, 1.0f); shot(S_OK, 0.5f, 1.5f); break;
        case EV_LOSE: shot(S_PASSBY, 0.4f, 0.8f); shot(S_MISS, 0.3f, 1.3f); break;
        case EV_LAP: shot(S_LAP, 0.7f, 1.0f); break;
        case EV_FINAL: shot(S_FINAL, 0.85f, 1.0f); break;
        case EV_WALL: shot(S_SMALL, 0.6f, 0.7f); break;
        case EV_CRASH: shot(S_BIG, 1.0f, 0.75f); break;
        case EV_CHARGE: shot(S_TOP, 0.6f, 1.5f); break;
        case EV_TURBO: shot(S_BIG, 0.75f, 1.4f); shot(S_WHOOSH, 0.8f, 0.7f); break;
        case EV_ROCKET: shot(S_GO, 0.8f, 1.25f); shot(S_WHOOSH, 0.9f, 0.6f); break;
        default: heard = false; break;
      }
      if (heard) g_fb[e->kind][1]++;
    } }
  if (c->big_splash) shot(c->kick > 0.5f ? S_BIG : S_SMALL, 0.4f + 0.5f * c->kick, 1.0f);
  if (G.state == RACE_GRID) {
    int n = (int)ceilf(-G.clock);
    if (n < last_count && n <= 3) shot(S_COUNT, 0.7f, 1.0f);
    last_count = n;
  } else if (last_count != 99) {
    if (G.state == RACE_ON) shot(S_GO, 0.8f, 1.0f);
    last_count = 99;
  }
  /* the finish: a chord for the top three, a falling pair otherwise */
  if (G.state == RACE_DONE && last_state == RACE_ON) shot(c->finish > 0 && c->place <= 3 ? S_FINISH : S_FINISH_LOW, 0.8f, 1.0f);
  last_state = G.state;
  if (g_menu_sfx) { shot(g_menu_sfx == 1 ? S_MENU_MOVE : g_menu_sfx == 2 ? S_MENU_OK : S_MENU_BACK, 0.45f, 1.0f); g_menu_sfx = 0; }
}
