/* Bounded visual pool, following the arena example's particle workflow. */
#include "cartwheel/particles.h"
static cw_node cannon_smoke;
static cw_node cannon_flash;
static cw_node damage_smoke_particles; /* a badly hit ship trails dark smoke: one shared system, a burst a puff */
enum { IMPACT_WATER, IMPACT_WOOD, IMPACT_LAND };
static cw_node impact_particles[3];
static void impact_init(cw_node root) {
  const char *names[]={"SeaImpact","HullSplinters","ShoreDust"};
  const char *colors[]={
    "0 0.85 0.96 1 0.95, 0.4 0.7 0.88 0.95 0.85, 1 0.6 0.8 0.9 0",
    "0 0.85 0.62 0.32 1, 0.6 0.55 0.34 0.13 1, 1 0.3 0.17 0.06 0",
    "0 0.55 0.43 0.28 0.65, 0.3 0.6 0.51 0.38 0.5, 1 0.6 0.54 0.43 0"};
  for(int i=0;i<3;i++) {
    cw_node n=cw_node_new("GPUParticles3D",names[i]);impact_particles[i]=n;
    /* Sixteen simultaneous impacts share a single simulation and draw. */
    cw_node_set(n,"bursts",cw_val_int(16));
    cw_node_set(n,"amount",cw_val_int(16*(i==0?24:i==1?12:8)));
    cw_node_set(n,"lifetime",cw_val_float(i==0?1.1:i==1?.65:1.0));
    cw_node_set(n,"explosiveness",cw_val_float(1));
    cw_node_set(n,"emitting",cw_val_bool(false));
    cw_node_set(n,"spread",cw_val_float(i==0?16:65));
    cw_node_set(n,"speed_min",cw_val_float(i==0?5:i==1?2:.6));
    cw_node_set(n,"speed_max",cw_val_float(i==0?9:i==1?4.5:1.8));
    cw_node_set(n,"gravity",cw_val_vec3(0,i==2?.3:-10,0));
    cw_node_set(n,"size",cw_val_float(i==0?.14:i==1?.22:.7));
    cw_node_set(n,"size_random",cw_val_float(.5));
    cw_node_set(n,"draw_mode",cw_val_enum(i==2?"BILLBOARD":"STRETCHED"));
    cw_node_set(n,"stretch",cw_val_float(i==0?.065:.035));
    cw_node_set(n,"blend",cw_val_enum("ALPHA"));
    cw_node_set(n,"color_ramp",cw_val_string(colors[i]));
    cw_node_set(n,"size_curve",cw_val_string(i==2?"0 0.5, 0.4 1.2, 1 2":"0 1, 0.7 0.8, 1 0.25"));
    if(i==2) {
      cw_node_set(n,"texture",cw_val_string("textures/cannon-smoke.png"));
      cw_node_set(n,"hframes",cw_val_int(8));cw_node_set(n,"vframes",cw_val_int(8));
    }
    cw_node_add_child(root,n);
  }
}
static void impact_effect(int kind,float x,float y,float z,float dx,float dz) {
  float length=sqrtf(dx*dx+dz*dz)+.001f;
  t3_vec3 direction=kind==IMPACT_WOOD?(t3_vec3){-dx/length,.8f,-dz/length}:(t3_vec3){0,1,0};
  cw_particles_burst(impact_particles[kind],(t3_vec3){x,y,z},direction);
}
static void effects_init(cw_node root) {
  impact_init(root);
  {
    cw_node n=cw_node_new("GPUParticles3D","DamageSmoke");damage_smoke_particles=n;
    cw_node_set(n,"bursts",cw_val_int(48));
    cw_node_set(n,"amount",cw_val_int(48*5));
    cw_node_set(n,"lifetime",cw_val_float(2.4));
    cw_node_set(n,"explosiveness",cw_val_float(1));
    cw_node_set(n,"one_shot",cw_val_bool(true));
    cw_node_set(n,"emitting",cw_val_bool(false));
    cw_node_set(n,"spread",cw_val_float(14));
    cw_node_set(n,"speed_min",cw_val_float(.9));
    cw_node_set(n,"speed_max",cw_val_float(1.6));
    cw_node_set(n,"gravity",cw_val_vec3(.4f*cw_sin(wind_to()),.3,.4f*cw_cos(wind_to()))); /* drifts downwind */
    cw_node_set(n,"size",cw_val_float(1.1));
    cw_node_set(n,"size_random",cw_val_float(.4));
    cw_node_set(n,"color_ramp",cw_val_string("0 0.16 0.13 0.11 0, 0.12 0.18 0.16 0.14 0.75, 1 0.36 0.35 0.34 0"));
    cw_node_set(n,"size_curve",cw_val_string("0 0.5, 0.3 1.1, 1 2.4"));
    cw_node_set(n,"draw_mode",cw_val_enum("BILLBOARD"));
    cw_node_set(n,"blend",cw_val_enum("ALPHA"));
    cw_node_set(n,"texture",cw_val_string("textures/cannon-smoke.png"));
    cw_node_set(n,"hframes",cw_val_int(8));cw_node_set(n,"vframes",cw_val_int(8));
    cw_node_add_child(root,n);
  }
  {
    /* Keep overlapping broadsides from all captains in two shared draws.
     * 32 muzzle positions retain more smoke than the old 12-emitter pool. */
    cw_node n=cw_node_new("GPUParticles3D","CannonSmoke");cannon_smoke=n;
    cw_node_set(n,"bursts",cw_val_int(32));
    cw_node_set(n,"amount",cw_val_int(32*14));
    cw_node_set(n,"lifetime",cw_val_float(1.6));
    cw_node_set(n,"explosiveness",cw_val_float(1));
    cw_node_set(n,"one_shot",cw_val_bool(true));
    cw_node_set(n,"emitting",cw_val_bool(false));
    cw_node_set(n,"spread",cw_val_float(24));
    cw_node_set(n,"speed_min",cw_val_float(1.8));
    cw_node_set(n,"speed_max",cw_val_float(3.8));
    cw_node_set(n,"gravity",cw_val_vec3(.35f*cw_sin(wind_to()),.4,.35f*cw_cos(wind_to()))); /* drifts downwind */
    cw_node_set(n,"size",cw_val_float(1.3));
    cw_node_set(n,"size_random",cw_val_float(.4));
    cw_node_set(n,"color_ramp",cw_val_string("0 0.85 0.78 0.63 0.9, 0.2 0.72 0.70 0.65 0.8, 1 0.50 0.52 0.54 0"));
    cw_node_set(n,"size_curve",cw_val_string("0 0.55, 0.2 1.1, 1 1.8"));
    cw_node_set(n,"draw_mode",cw_val_enum("BILLBOARD"));
    cw_node_set(n,"blend",cw_val_enum("ALPHA"));
    cw_node_set(n,"texture",cw_val_string("textures/cannon-smoke.png"));
    cw_node_set(n,"hframes",cw_val_int(8));cw_node_set(n,"vframes",cw_val_int(8));
    cw_node_add_child(root,n);
    cw_node flash=cw_node_new("GPUParticles3D","CannonFlash");cannon_flash=flash;
    cw_node_set(flash,"bursts",cw_val_int(32));
    cw_node_set(flash,"amount",cw_val_int(32));
    cw_node_set(flash,"lifetime",cw_val_float(.13));
    cw_node_set(flash,"explosiveness",cw_val_float(1));
    cw_node_set(flash,"one_shot",cw_val_bool(true));
    cw_node_set(flash,"emitting",cw_val_bool(false));
    cw_node_set(flash,"speed_min",cw_val_float(0));
    cw_node_set(flash,"speed_max",cw_val_float(0));
    cw_node_set(flash,"gravity",cw_val_vec3(0,0,0));
    cw_node_set(flash,"size",cw_val_float(1.0));
    cw_node_set(flash,"energy",cw_val_float(3));
    cw_node_set(flash,"color_ramp",cw_val_string("0 1 0.8 0.3 1, 0.3 1 0.5 0.1 0.8, 1 0.6 0.1 0.01 0"));
    cw_node_set(flash,"size_curve",cw_val_string("0 1, 1 0.1"));
    cw_node_set(flash,"draw_mode",cw_val_enum("BILLBOARD"));
    cw_node_set(flash,"blend",cw_val_enum("ADD"));
    cw_node_add_child(root,flash);
  }
}
static void effects_reset(cw_node root) {
  /* Recreate only at match boundaries, never on a hit. This clears old GPU
   * burst slots as well as smoke when changing maps or restarting a round. */
  for(int i=0;i<3;i++)cw_node_free(impact_particles[i]);
  cw_node_free(cannon_smoke);cw_node_free(cannon_flash);cw_node_free(damage_smoke_particles);
  effects_init(root);
}
static void cannon_effect(float x,float y,float z,float dx,float dz) {
  cw_particles_burst(cannon_smoke,(t3_vec3){x+dx*.35f,y+.6f,z+dz*.35f},(t3_vec3){dx,.4f,dz});
  cw_particles_burst(cannon_flash,(t3_vec3){x+dx*.9f,y+.8f,z+dz*.9f},(t3_vec3){0,1,0});
}

static void damage_smoke(float x,float y,float z) {
  cw_particles_burst(damage_smoke_particles,(t3_vec3){x,y,z},(t3_vec3){0,1,0});
}

/* Thin lines of wind over the sea, running downwind across the whole map: the wind's direction read at a glance
 * (Low and up; on Very Low the flags and sails show it). One system, one draw; 28 streaks alive at most. */
static cw_node wind_streaks_new(cw_node parent) {
  cw_node n=cw_node_new("GPUParticles3D","WindStreaks");
  float wx=cw_sin(wind_to()),wz=cw_cos(wind_to());
  cw_node_set(n,"amount",cw_val_int(28));
  cw_node_set(n,"lifetime",cw_val_float(3.2));
  cw_node_set(n,"lifetime_random",cw_val_float(.3));
  cw_node_set(n,"emission_shape",cw_val_enum("BOX"));
  cw_node_set(n,"emission_extents",cw_val_vec3(44,0,23));
  cw_node_set(n,"direction",cw_val_vec3(wx,0,wz));
  cw_node_set(n,"spread",cw_val_float(3));
  cw_node_set(n,"speed_min",cw_val_float(5.5));
  cw_node_set(n,"speed_max",cw_val_float(7.5));
  cw_node_set(n,"gravity",cw_val_vec3(0,0,0));
  cw_node_set(n,"size",cw_val_float(.09));
  cw_node_set(n,"size_random",cw_val_float(.3));
  cw_node_set(n,"draw_mode",cw_val_enum("STRETCHED"));
  cw_node_set(n,"stretch",cw_val_float(.42));
  cw_node_set(n,"blend",cw_val_enum("ALPHA"));
  cw_node_set(n,"color_ramp",cw_val_string("0 0.95 0.97 1 0, 0.3 0.95 0.97 1 0.34, 0.7 0.95 0.97 1 0.34, 1 0.95 0.97 1 0"));
  cw_node_set(n,"preprocess",cw_val_float(3.5));
  cw_node_set(n,"emitting",cw_val_bool(true));
  cw_node_add_child(parent,n);cw_node3d_set_position(n,(t3_vec3){0,1.4f,0});
  return n;
}

/* A small continuous ash plume gives the volcanic map a visible source. */
static cw_node volcano_plume(cw_node parent) {
  cw_node n=cw_node_new("GPUParticles3D","VolcanicAsh");
  cw_node_set(n,"amount",cw_val_int(48));
  cw_node_set(n,"lifetime",cw_val_float(4));
  cw_node_set(n,"direction",cw_val_vec3(.15f,1,.05f));
  cw_node_set(n,"spread",cw_val_float(12));
  cw_node_set(n,"speed_min",cw_val_float(.6));
  cw_node_set(n,"speed_max",cw_val_float(1.4));
  cw_node_set(n,"gravity",cw_val_vec3(.18,.12,.05));
  cw_node_set(n,"size",cw_val_float(1.5));
  cw_node_set(n,"size_random",cw_val_float(.35));
  cw_node_set(n,"color_ramp",cw_val_string("0 0.24 0.20 0.16 0, 0.15 0.29 0.27 0.24 0.6, 1 0.42 0.42 0.40 0"));
  cw_node_set(n,"size_curve",cw_val_string("0 0.4, 0.4 1.1, 1 2.2"));
  cw_node_set(n,"draw_mode",cw_val_enum("BILLBOARD"));
  cw_node_set(n,"blend",cw_val_enum("ALPHA"));
  cw_node_set(n,"texture",cw_val_string("textures/cannon-smoke.png"));
  cw_node_set(n,"hframes",cw_val_int(8));cw_node_set(n,"vframes",cw_val_int(8));
  cw_node_set(n,"emitting",cw_val_bool(true));
  cw_node_add_child(parent,n);cw_node3d_set_position(n,(t3_vec3){25,5.3f,-10});
  return n;
}
