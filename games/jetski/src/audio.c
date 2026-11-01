/* The sound of it: the engine's rasp follows the throttle and the speed and climbs when the pump clears the water; the
 * hull's hiss follows the speed and stops in the air; splashes, buoys and the countdown. Rival engines are not heard yet. */
#include <math.h>

#include "cartwheel/audio.h"
#include "cartwheel/resource.h"
#include "cartwheel/shell.h"
#include "game.h"

static cw_res res[10];
static cw_sound engine, hull, big, small, ok, top, miss, count, go, music;
static cw_voice v_engine, v_hull, v_music;
static float rev, hiss;
static int last_count = 99, last_meter, last_passes, last_misses;

static cw_sound load(int i, const char *path, cw_bus bus, bool stream) {
  res[i] = cw_res_load(path, CW_RES_AUDIO_CLIP);
  return cw_res_audio_sound(res[i], &(cw_sound_desc){ .mode = stream ? CW_SOUND_STREAM : CW_SOUND_SAMPLE, .bus = bus });
}

void audio_init(void) {
  cw_bus m = cw_shell_music_bus(), s = cw_shell_sfx_bus();
  engine = load(0, "audio/engine.wav", s, false);
  hull = load(1, "audio/hull.wav", s, false);
  big = load(2, "audio/splash_big.wav", s, false);
  small = load(3, "audio/splash_small.wav", s, false);
  ok = load(4, "audio/buoy_ok.wav", s, false);
  top = load(5, "audio/buoy_top.wav", s, false);
  miss = load(6, "audio/buoy_miss.wav", s, false);
  count = load(7, "audio/count.wav", s, false);
  go = load(8, "audio/go.wav", s, false);
  music = load(9, "audio/music.wav", m, true);
  v_music = cw_audio_play(music, &(cw_play_desc){ .loop = true, .fade_in = 2.0f, .volume = 0.5f, .bus = m });
  v_engine = cw_audio_play(engine, &(cw_play_desc){ .loop = true, .volume = 0, .bus = s });
  v_hull = cw_audio_play(hull, &(cw_play_desc){ .loop = true, .volume = 0, .bus = s });
}

void audio_tick(float dt) {
  const craft *c = &G.c[0];
  bool quiet = G.state == RACE_ATTRACT;
  float target = c->air ? 1.15f : 0.45f + 0.75f * (c->speed / 30.0f);
  target += 0.2f * c->in.throttle;
  rev += (target - rev) * (dt * (c->air ? 6.0f : 3.0f));
  hiss += ((c->air ? 0.0f : 0.1f + 0.7f * (c->speed / 30.0f)) - hiss) * (dt * 10.0f);
  cw_voice_set_pitch(v_engine, 0.55f + 1.25f * rev);
  cw_voice_set_volume(v_engine, quiet ? 0.0f : 0.2f + 0.26f * c->in.throttle);
  cw_voice_set_volume(v_hull, quiet ? 0.0f : hiss * 0.5f);
  cw_voice_set_pitch(v_hull, 0.75f + 0.5f * (c->speed / 30.0f));
  /* every verb speaks: the event list, with what sounds there are today */
  { static unsigned read;
    if (g_ev_n - read > EV_RING) read = g_ev_n - EV_RING;
    for (; read != g_ev_n; read++) {
      const game_event *e = &g_ev[read % EV_RING];
      cw_sound sn = (cw_sound){ 0 };
      float vol = 0.6f, pitch = 1.0f;
      switch (e->kind) {
        case EV_PASS: sn = ok; vol = 0.5f; pitch = 1.0f + 0.05f * (float)e->a; break;
        case EV_TOP: sn = top; vol = 0.75f; break;
        case EV_TRICK: sn = top; pitch = 1.25f; break;
        case EV_LAND: sn = e->a ? top : small; vol = e->a ? 0.5f : 0.45f; pitch = e->a ? 0.85f : 1.0f; break;
        case EV_MISS: sn = miss; vol = 0.7f; break;
        case EV_GAIN: sn = ok; pitch = 1.5f; break;
        case EV_LOSE: sn = miss; vol = 0.3f; pitch = 1.3f; break;
        case EV_LAP: case EV_FINAL: sn = go; vol = e->kind == EV_FINAL ? 0.8f : 0.55f; break;
        case EV_WALL: sn = small; vol = 0.6f; pitch = 0.7f; break;
        case EV_CRASH: sn = big; vol = 1.0f; pitch = 0.75f; break;
        case EV_CHARGE: sn = top; vol = 0.6f; pitch = 1.5f; break;
        case EV_TURBO: sn = big; vol = 0.75f; pitch = 1.4f; break;
      }
      if (g_ev_on && sn.id) { cw_voice v = cw_audio_play(sn, &(cw_play_desc){ .volume = vol, .bus = cw_shell_sfx_bus() }); cw_voice_set_pitch(v, pitch); g_fb[e->kind][1]++; }
    } }
  if (c->big_splash) cw_audio_play(c->kick > 0.5f ? big : small, &(cw_play_desc){ .volume = 0.4f + 0.5f * c->kick, .bus = cw_shell_sfx_bus() });
  if (G.state == RACE_GRID) {
    int n = (int)ceilf(-G.clock);
    if (n < last_count && n <= 3) cw_audio_play(count, &(cw_play_desc){ .volume = 0.7f, .bus = cw_shell_sfx_bus() });
    last_count = n;
  } else if (last_count != 99) {
    if (G.state == RACE_ON) cw_audio_play(go, &(cw_play_desc){ .volume = 0.8f, .bus = cw_shell_sfx_bus() });
    last_count = 99;
  }
}
