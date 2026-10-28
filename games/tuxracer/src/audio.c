/* Sounds and music: the original's wavs and Impulse Tracker modules
 * (tuxracer_init.tcl's bindings): start1 on the title, race1 while racing,
 * wonrace1 on the results, options1 on the credits; fish_pickup1-3 for a
 * herring, tux_hit_tree1 on a tree, tux_on_snow1 / ice1 / rock1 looping
 * under the slide (the original plays the one of the surface; here the three
 * cross-fade by the surface weights; flying uses the snow sound). */
#include "game.h"

#include "cartwheel/audio.h"
#include "cartwheel/core.h"
#include "cartwheel/resource.h"

static struct {
  bool on;
  cw_bus music_bus, sfx_bus;
  cw_res music_res[5];
  cw_sound music[5];
  cw_voice music_voice;
  int music_now;
  cw_res pickup_res[3], tree_res, slide_res[SURF_KINDS];
  cw_sound pickup[3], tree, slide[SURF_KINDS];
  cw_voice slide_voice[SURF_KINDS];
  int pickup_next;
} A = { .music_now = MUS_NONE };

static const char *const MUSIC_FILE[5] = { NULL, "music/start1-jt.it", "music/race1-jt.it", "music/wonrace1-jt.it", "music/options1-jt.it" };
static const char *const PICKUP_FILE[3] = { "sounds/fish_pickup1.wav", "sounds/fish_pickup2.wav", "sounds/fish_pickup3.wav" };
static const char *const SLIDE_FILE[SURF_KINDS] = { [SURF_ICE] = "sounds/tux_on_ice1.wav", [SURF_ROCK] = "sounds/tux_on_rock1.wav", [SURF_SNOW] = "sounds/tux_on_snow1.wav" };

static cw_sound load_sound(cw_res *res, const char *path, cw_bus bus, bool stream) {
  *res = cw_res_load(path, CW_RES_AUDIO_CLIP);
  return cw_res_audio_sound(*res, &(cw_sound_desc){ .mode = stream ? CW_SOUND_STREAM : CW_SOUND_SAMPLE, .bus = bus });
}

void audio_init(void) {
  A.on = cw_audio_init(&(cw_audio_desc){ 0 });
  if (!A.on) { cw_warn("audio: no device, silent"); return; }
  A.music_bus = cw_bus_create("music", CW_AUDIO_MASTER);
  A.sfx_bus = cw_bus_create("sfx", CW_AUDIO_MASTER);
  cw_bus_set_volume(A.music_bus, 0.55f);
  for (int i = 1; i < 5; i++) A.music[i] = load_sound(&A.music_res[i], MUSIC_FILE[i], A.music_bus, true);
  for (int i = 0; i < 3; i++) A.pickup[i] = load_sound(&A.pickup_res[i], PICKUP_FILE[i], A.sfx_bus, false);
  A.tree = load_sound(&A.tree_res, "sounds/tux_hit_tree1.wav", A.sfx_bus, false);
  for (int i = 0; i < SURF_KINDS; i++) A.slide[i] = load_sound(&A.slide_res[i], SLIDE_FILE[i], A.sfx_bus, false);
}

void audio_music(int which) {
  if (!A.on || which == A.music_now) return;
  if (A.music_now != MUS_NONE) cw_voice_stop(A.music_voice, 0.8f);
  A.music_now = which;
  if (which != MUS_NONE) A.music_voice = cw_audio_play(A.music[which], &(cw_play_desc){ .loop = which != MUS_OVER, .fade_in = 0.5f, .bus = A.music_bus });
}

void audio_sfx(int id) {
  if (!A.on) return;
  if (id == SFX_PICKUP) { cw_audio_play(A.pickup[A.pickup_next], &(cw_play_desc){ .bus = A.sfx_bus }); A.pickup_next = (A.pickup_next + 1) % 3; }
  else if (id == SFX_TREE) cw_audio_play(A.tree, &(cw_play_desc){ .bus = A.sfx_bus });
}

void audio_slide(const float w[SURF_KINDS], float speed, bool airborne, bool on) {
  if (!A.on) return;
  float gain = on ? (speed < 3 ? 0 : speed > 25 ? 1 : (speed - 3) / 22) : 0;
  for (int i = 0; i < SURF_KINDS; i++) {
    float v = gain * (airborne ? (i == SURF_SNOW ? 0.5f : 0) : w[i]);
    if (v > 0.001f) {
      if (!cw_voice_is_playing(A.slide_voice[i])) A.slide_voice[i] = cw_audio_play(A.slide[i], &(cw_play_desc){ .loop = true, .bus = A.sfx_bus, .volume = v });
      else cw_voice_set_volume(A.slide_voice[i], v);
    } else if (cw_voice_is_playing(A.slide_voice[i])) {
      cw_voice_stop(A.slide_voice[i], 0.1f);
      A.slide_voice[i] = (cw_voice){ 0 };
    }
  }
}

void audio_update(void) { if (A.on) cw_audio_update(); }
