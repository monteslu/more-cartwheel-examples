/* Original generated samples: noise bursts, wood resonance and metal chimes.
 * Independent noise state keeps sound generation out of the game RNG. */
#include "cartwheel/audio.h"
enum { S_CANNON, S_HIT, S_CARGO, S_BANK, S_UPGRADE, S_COUNT };
static cw_sound pirate_sounds[S_COUNT];
static int sound_events[S_COUNT];
static cw_sound sea_sound;
static cw_voice sea_voice;
static void sound_init(void) {
  if (!cw_audio_init(NULL)) { cw_warn("pirates: audio initialization failed"); return; }
  static float pcm[48000];
  uint32_t noise_state=0x918ba31u;
  for(int kind=0;kind<S_COUNT;kind++) {
    float duration=kind==S_CANNON?.85f:kind==S_HIT?.36f:kind==S_CARGO?.22f:.65f;
    int count=(int)(duration*48000);float low=0;
    for(int i=0;i<count;i++) {
      float t=i/48000.0f;
      noise_state^=noise_state<<13;noise_state^=noise_state>>17;noise_state^=noise_state<<5;
      float noise=(noise_state&65535)/32767.5f-1;
      low+=.055f*(noise-low);
      float sample=0;
      if(kind==S_CANNON) {
        float boom=sinf(6.2831853f*(65*t-22*t*t))*expf(-t*8);
        sample=.58f*boom+.8f*low*expf(-t*4)+.22f*noise*expf(-t*65);
      } else if(kind==S_HIT) {
        sample=.32f*noise*expf(-t*35)+.3f*sinf(6.2831853f*173*t)*expf(-t*22);
      } else {
        float hz=kind==S_CARGO?790:kind==S_BANK?1046:660;
        sample=.18f*(sinf(6.2831853f*hz*t)+.4f*sinf(6.2831853f*hz*2.71f*t))*expf(-t*13);
        if(kind!=S_CARGO && t>.11f) {
          float u=t-.11f;sample+=.16f*sinf(6.2831853f*hz*1.5f*u)*expf(-u*10);
        }
      }
      float fade=fminf(1,(duration-t)*100);
      pcm[i]=sample*fminf(1,t*1800)*fade;
    }
    pirate_sounds[kind]=cw_sound_from_pcm(pcm,count,1,48000,NULL);
  }
  cw_debug_field("pirates.sound_events",sound_events,CW_DBG_I32,S_COUNT);
  sea_sound=cw_sound_load("audio/sea.wav",&(cw_sound_desc){.mode=CW_SOUND_SAMPLE,.max_voices=1});
  sea_voice=cw_audio_play(sea_sound,&(cw_play_desc){.volume=.23f,.loop=true,.fade_in=2.0f,.priority=1});
}
static void sound_play(int kind,float x) {
  sound_events[kind]++;
  cw_audio_play(pirate_sounds[kind],&(cw_play_desc){.volume=.65f,.pan=fmaxf(-.8f,fminf(.8f,x/50)),.pitch=1});
}

static void sound_shutdown(void *user) {
  (void)user;
  cw_voice_stop(sea_voice,0);
  cw_sound_free(sea_sound);
  for(int i=0;i<S_COUNT;i++)cw_sound_free(pirate_sounds[i]);
  cw_audio_shutdown();
}
