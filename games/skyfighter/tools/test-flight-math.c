/* WASM test wrapper uses the exact control kernel and three.c math in game. */
#include <string.h>
#include "../src/flight-controls.h"
static flight_state state;
__attribute__((export_name("reset"))) void reset(float heading,float pitch,float bank){memset(&state,0,sizeof state);state.attitude=flight_attitude(heading,pitch,bank);}
__attribute__((export_name("tick"))) void tick(float turn,float pitch,float roll,int stunt,float speed){flight_tick(&state,(flight_input){turn,pitch,roll,speed,stunt},1.0f/60);}
__attribute__((export_name("get"))) float get(int i){t3_vec3 f=t3_vec3_apply_quat((t3_vec3){0,0,-1},state.attitude);return i==0?f.y:i==1?asinf(flight_clamp(f.y,-1,1)):i==2?flight_bank(state.attitude):i==3?state.yaw_rate:i==4?state.pitch_rate:i==5?f.x:i==6?f.z:state.attitude.w;}
