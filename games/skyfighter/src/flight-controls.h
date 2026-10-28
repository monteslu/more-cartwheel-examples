/* Pure flight math: standard steering uses heading/pitch/bank independently.
 * Explicit stunt mode uses body axes for uninterrupted loops and rolls. */
#ifndef SKY_FLIGHT_CONTROLS_H
#define SKY_FLIGHT_CONTROLS_H
#include <math.h>
#include "three.h"
typedef struct {t3_quat attitude;float pitch_rate,roll_rate,yaw_rate,pitch_idle,bank;} flight_state;
typedef struct {float turn,pitch,roll,speed;bool stunt;} flight_input;
static float flight_clamp(float x,float a,float b){return x<a?a:x>b?b:x;}
static float flight_wrap(float x){return atan2f(sinf(x),cosf(x));}
static t3_quat flight_attitude(float heading,float pitch,float bank){
  return t3_quat_multiply(t3_quat_multiply(t3_quat_from_axis_angle((t3_vec3){0,1,0},-heading),t3_quat_from_axis_angle((t3_vec3){1,0,0},pitch)),t3_quat_from_axis_angle((t3_vec3){0,0,1},bank));
}
static float flight_bank(t3_quat q){
  t3_vec3 f=t3_vec3_apply_quat((t3_vec3){0,0,-1},q),up=t3_vec3_apply_quat((t3_vec3){0,1,0},q);
  float horizontal=sqrtf(f.x*f.x+f.z*f.z);if(horizontal<.0001f)return 0;
  t3_vec3 horizon={-f.x*f.y/horizontal,horizontal,-f.z*f.y/horizontal};
  return atan2f(t3_vec3_dot(t3_vec3_cross(up,horizon),f),t3_vec3_dot(up,horizon));
}
static float flight_authority(float speed,float command){
  speed=fmaxf(speed,100);float fine=fminf(.65f,85/speed),hard=fminf(1.05f,fmaxf(speed/840,170/speed));
  float t=flight_clamp((fabsf(command)-.25f)/.75f,0,1);return fine+(hard-fine)*t*t*(3-2*t);
}
static void flight_tick(flight_state *s,flight_input in,float dt){
  float response=1-expf(-dt*8),pitch_target=in.pitch*flight_authority(in.speed,in.pitch);
  if(in.stunt){
    s->pitch_idle=0;s->pitch_rate+=(pitch_target-s->pitch_rate)*response;
    s->roll_rate+=(-in.roll*3.6f-s->roll_rate)*response;s->yaw_rate=0;
    t3_quat p=t3_quat_from_axis_angle((t3_vec3){1,0,0},s->pitch_rate*dt),r=t3_quat_from_axis_angle((t3_vec3){0,0,1},s->roll_rate*dt);
    s->attitude=t3_quat_normalize(t3_quat_multiply(s->attitude,t3_quat_multiply(p,r)));
  }else{
    t3_vec3 f=t3_vec3_apply_quat((t3_vec3){0,0,-1},s->attitude);
    float pitch=asinf(flight_clamp(f.y,-1,1)),heading=atan2f(f.x,-f.z),bank=flight_bank(s->attitude);
    /* Releasing pitch levels the nose even while the player is turning. */
    if(fabsf(in.pitch)<.08f){s->pitch_idle+=dt;if(s->pitch_idle>.25f)pitch_target=flight_clamp(-pitch*2.5f,-1.4f,1.4f);}else s->pitch_idle=0;
    s->pitch_rate+=(pitch_target-s->pitch_rate)*response;
    const float limit=1.483529864f; /* 85 degrees; RB explicitly enables loops. */
    float next_pitch=flight_clamp(pitch+s->pitch_rate*dt,-limit,limit);
    if((next_pitch>=limit&&s->pitch_rate>0)||(next_pitch<=-limit&&s->pitch_rate<0))s->pitch_rate=0;
    float bank_target=-in.turn*1.10f;
    s->roll_rate=flight_clamp(flight_wrap(bank_target-bank)*7,-3.6f,3.6f);bank+=s->roll_rate*dt;
    float turn=-sinf(bank_target)*flight_authority(in.speed,in.turn);
    s->yaw_rate+=(turn-s->yaw_rate)*response;
    s->attitude=flight_attitude(heading+s->yaw_rate*dt,next_pitch,bank);
  }
  s->bank=flight_bank(s->attitude);
}
#endif
