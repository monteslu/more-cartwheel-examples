/* Sky Fighter: an arcade dogfight using a textured F-15 and three.c scenery.
 * Cartwheel supplies the GPU backend, input, HUD, audio, and debug state. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "miniaudio.h"

#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/platform.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "three.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/terrain.h"
#include "cartwheel/noise.h"
#include "cartwheel/quality.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "cartwheel/physics3d.h"
#include "cartwheel/environment.h"
#include "cartwheel/visibility.h"
#include "cartwheel/decals.h"
#include "wreck-parts.h"

#define W 960
#define H 540
#define ENEMIES 7
#define BUILDINGS 96
#define MISSIONS 8
#define BALLOONS 12
#define BLIMPS 6
#define SHIELD_FIRST (BALLOONS+BLIMPS)
#define MOTHER (SHIELD_FIRST+4)
#define SANTA (MOTHER+1)
#define AIR_TARGETS (SANTA+1)
#define TARGET_COUNT (ENEMIES+BUILDINGS+AIR_TARGETS*2)
#define SHOTS 64
#define FOE_SHOTS 14
#define FX 18
#define MISSILE_CAPACITY 18
#define MUSIC_RING 32768
#define SCENERY 20
#define PI 3.14159265358979323846f
#include "flight-controls.h"
#define SAVE_MAGIC 0x534d5333u
#define SAVE_MAGIC_V2 0x534d5332u
#define SAVE_MAGIC_V1 0x534d5331u

typedef struct { uint32_t magic; uint8_t invert_pitch, expert_controls, music_volume, sfx_volume; uint32_t graphics, progress; } preferences;
static preferences prefs;

typedef struct { t3_object *node, *modern, *vintage, *sleigh, *propeller; float x, y, z, vx, vy, vz, heading, phase, maneuver_time, fire_cool, seeker; int active, hp, maneuver, role, burst, passed; } enemy;
typedef struct { t3_object *node; cw_node trail; float x, y, z, vx, vy, vz, age, obscured, decoy[3]; int active, missile, target; } shot;
typedef struct { t3_object *node; cw_node particles; float x, y, z, age; int active; } blast;
typedef struct { t3_object *node; float x, z, span; } scenery;
typedef struct { t3_object *node, *ruin; float x,y,z,radius; int hp; t3_box3 bounds,colliders[16]; int collider_count; } structure;

typedef struct { t3_object *root,*envelope,*basket,*ropes,*shield; cw_body3d body; float x,y,z,by,vy,age; int32_t kind,hull_hp,basket_hp,state,credited; } air_target;
static struct {
  int32_t level,map_style,balloon_kills,blimp_kills,shields_left,mission_complete;
  float mission_time,boss_cool;
  t3_object *countryside, *decor[8], *horizon[8];
  air_target air_targets[AIR_TARGETS];
  cw_tree *tree;
  cw_node camera_node, jet_node, terrain_node, exhaust[2], muzzle[2], crash_fx[7], flare_fx[2], vapor[2];
  t3_object *shockwave; t3_material *shock_mat;
  structure buildings[BUILDINGS];
  float crash_age, edge_timer, impact_age, impact_x, impact_y, impact_z; int edge_armed;
  int32_t buildings_destroyed,impact_structure;
  cw_terrain *terrain;
  cw_noise noise;
  float *alpine_heights;
  uint32_t landscape_far_color;
  cw_world3d wreck_world;cw_body3d wreck_bodies[6];t3_object *wreck[6];
  float wreck_age;int32_t wreck_count,wreck_contacts;
  int32_t intact_visible,cloud_volumes,cloud_active;
  t3_material *wreck_old[10],*wreck_shared[10];
  t3_scene *scene;
  t3_camera *camera;
  cw_res jet_asset, terrain_asset, water_asset, wall_asset, roof_asset;
  t3_texture *water_tex;
  t3_object *jet, *flame_idle[2], *flame_burn[2], *flame_core[2];
  t3_object *airfield;
  enemy enemies[ENEMIES];
  shot shots[SHOTS];
  shot foe_shots[FOE_SHOTS];
  blast blasts[FX];
  scenery scenery[SCENERY];
  cw_font title_font, ui_font;
  cw_action left, right, up, down, gun, missile, boost, brake, pause, settings, back, roll_left, roll_right, aerobatics, look_left, look_right, look_up, look_down, flares_action;
  unsigned rng;
  int32_t state, return_state, ticks, kills, score, health, missiles, wave, shots_fired, hits;
  int32_t model_triangles, model_children, enemy_triangles, model_lods;
  float px, py, pz, heading, pitch, roll, speed;
  float spawn, gun_cool, missile_cool, damage_flash, world_scroll;
  float boost_fuel, hit_flash, kill_flash, missile_request;
  int32_t boost_exhausted, boost_active, missile_launches, missile_last_target;
  int lock_target, near_misses, settings_row, menu_row, cycle_target, manual_target;
  int32_t flares, incoming, decoyed, evasions, patrols, fuel_depots;
  float vapor_width, flare_cool, enemy_missile_cool, flare_timer, threat_range, threat_bearing, threat_eta, alert_cool, notice_timer;
  char notice[96];
  float lock_timer, airspeed, warning, camera_shake;
  float visual_forward[3], visual_up[3], attitude_error;
  flight_state flight; t3_vec3 camera_up,camera_forward;
  int camera_follow_ready;
  int32_t flight_wait_neutral;float flight_input[3],flight_start_input[3];
  float angle_cache[3],pitch_travel,roll_travel;
  int32_t skipped;
  volatile float audio_engine, audio_shot, audio_boom, audio_boost, audio_alert, audio_tone_hz, audio_pass;
  double audio_phase;
  float audio_rms,audio_peak;
  int16_t *burn_pcm;size_t burn_samples;
  ma_decoder music_decoder;void *music_data;
  float music_ring[MUSIC_RING*2],music_gain,music_rms,sfx_gain;
  atomic_uint music_read,music_write;
  int32_t music_ready,music_underflows,music_played;
} G;

static int art_hide_hud; /* Developer capture switch; never stored in preferences. */
enum { TITLE, PLAY, PAUSED, WIN, LOST, SETTINGS, CRASH, CONTROLS, VICTORY };
static struct {cw_texture thumbnails[MISSIONS];cw_vec2 pointer;uint32_t previous_buttons;bool clicked;} menu_ui;
static void menu_read_pointer(void);
static void menu_load(void);
static bool menu_tick(void);

static float rand01(void) {
  G.rng = G.rng * 1664525u + 1013904223u;
  return (float)(G.rng >> 8) / 16777216.0f;
}
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float smooth01(float v) { v=clampf(v,0,1); return v*v*(3-2*v); }

/* Terrain data and gameplay share metres. The terrain renderer selects
 * CDLOD patches; flight collision queries the exact sampled triangles. */
static void campaign_make(void);
static void campaign_start(void);
static void campaign_update(float dt);
static void campaign_finish(void);
static bool campaign_aircraft_collision(t3_vec3 previous);
static bool campaign_shield_hit(float ax,float ay,float az,float bx,float by,float bz);
static int campaign_waypoint(void);
static bool air_target_position(int id,float *x,float *y,float *z);
static int air_target_hit(float ax,float ay,float az,float bx,float by,float bz,bool missile);
static void air_target_damage(int id,bool missile);
static const char *mission_name(void);
static const char *mission_goal(void);
static float coast_x(float z) {
  return 780 + 240*sinf(z*0.0008f) + 90*sinf(z*0.0021f+0.7f);
}
static float landscape_height(float x, float z) {
  if(G.map_style==1)return 125+55*sinf(x*.0011f+z*.0005f)+38*cosf(z*.0015f)+22*cw_noise_2d(&G.noise,x,z);
  if(G.map_style==2){
    float valley=expf(-powf((x+600-260*sinf(z*.0006f))/720,2));
    float gx=clampf((x+4096)/16,0,511.999f),gz=clampf((z+4096)/16,0,511.999f);int ix=(int)gx,iz=(int)gz;float fx=gx-ix,fz=gz-iz;
    float *dem=G.alpine_heights;float a=dem[iz*513+ix],b=dem[iz*513+ix+1],c=dem[(iz+1)*513+ix],d=dem[(iz+1)*513+ix+1];
    float relief=(a+(b-a)*fx)*(1-fz)+(c+(d-c)*fx)*fz;
    return 165+(180+relief*.85f)*(1-valley*.92f);
  }
  if(G.map_style==3){float canyon=smooth01((fabsf(x+450*sinf(z*.0008f))-300)/350);return 70+canyon*(330+80*sinf(z*.0013f)+65*cw_noise_2d(&G.noise,x,z));}
  if(G.map_style==4)return 28+5*sinf(x*.0004f)*cosf(z*.0004f);
  if(G.map_style==5){float river=expf(-powf((x-950-220*sinf(z*.0007f))/130,2));return 45+9*cw_noise_2d(&G.noise,x,z)-river*55;}
  if(G.map_style==6)return 25+8*sinf(x*.001f)*cosf(z*.0007f)+3*cw_noise_2d(&G.noise,x,z);
  if(G.map_style==7)return -70;

  float inland = coast_x(z)-x;
  /* An asymmetric, scalloped island inside the DEM tile. Keep the existing
   * east-coast airfield, but sink every tile edge below the open sea. */
  float ix=(x+1150)/2550, iz=(z+150)/3350;
  float angle=atan2f(iz,ix);
  float radius=1+0.065f*sinf(3*angle+0.4f)+0.035f*cosf(5*angle-0.7f)
                 +0.022f*sinf(9*angle+1.1f);
  float shore=(radius-sqrtf(ix*ix+iz*iz))*2400;
  float land=fminf(smooth01((inland+80)/340),smooth01((shore+60)/500));
  float mountains = smooth01((-x+250)/1500);
  float gx=clampf((x+4096)/16,0,511.999f),gz=clampf((z+4096)/16,0,511.999f);
  int dx=(int)gx,dz=(int)gz;float fx=gx-dx,fz=gz-dz;
  float *dem=G.alpine_heights;
  float a=dem[dz*513+dx],b=dem[dz*513+dx+1],c=dem[(dz+1)*513+dx],d=dem[(dz+1)*513+dx+1];
  float relief=(a+(b-a)*fx)*(1-fz)+(c+(d-c)*fx)*fz;
  float h = -48 + land*(72+mountains*relief*smooth01((shore-180)/900));
  float town=smooth01((x+1050)/180)*smooth01((-400-x)/120)
            *smooth01((z+1250)/160)*smooth01((550-z)/160);
  h+=(60-h)*town;
  float road=smooth01((24-fabsf(z+990))/12)*smooth01((x+550)/50)*smooth01((80-x)/50);
  float road_y=23+37*smooth01((-x-30)/460);
  h+=(road_y-h)*road;
  float base = smooth01((x+480)/150)*smooth01((340-x)/160)
             * smooth01((z+1850)/180)*smooth01((620-z)/180);
  return h+(23-h)*base;
}
static float terrain_height(float x,float z) {
  return G.terrain ? cw_terrain_height_at(G.terrain,x,z) : landscape_height(x,z);
}
static void paint_landscape(bool materials) {
  const cw_terrain_def *d=cw_terrain_get_def(G.terrain);
  int nx=d->size_x+1,nz=d->size_z+1;
  float *h=malloc((size_t)nx*nz*sizeof(float));
  uint32_t *ctl=malloc((size_t)nx*nz*sizeof(uint32_t));
  for(int j=0;j<nz;j++)for(int i=0;i<nx;i++) {
    float x=d->origin.x+i*d->vertex_spacing,z=d->origin.z+j*d->vertex_spacing;
    float y=landscape_height(x,z);
    h[j*nx+i]=y;
    float dx=landscape_height(x+8,z)-landscape_height(x-8,z);
    float dz=landscape_height(x,z+8)-landscape_height(x,z-8);
    float slope=sqrtf(dx*dx+dz*dz)/16;
    float rock=smooth01((slope-0.30f)/0.55f);
    int base=0,over=3;float blend=smooth01((y-40)/90);
    if(y<12){base=2;over=0;blend=smooth01((y-2)/10);}
    else if(y>130){base=3;over=1;blend=rock;}
    float snow=smooth01((y-820-140*cw_noise_2d(&G.noise,x+700,z-300)+dz*1.5f)/180);
    if(snow>0){base=rock>0.7f?1:3;over=4;blend=snow*(1-smooth01((slope-0.8f)/0.7f));}
    if(G.map_style==1&&y>12){base=3;over=slope>.6f?1:0;blend=slope>.6f?rock:.10f+.08f*(1+cw_noise_2d(&G.noise,x+800,z));}
    if(G.map_style==2){base=4;over=1;blend=smooth01((slope-.40f)/.70f)*.67f;
      /* Wind-scoured rock seams articulate the mountain faces at flight distance. */
      blend=fmaxf(blend,smooth01((.18f+cw_noise_2d(&G.noise,x+1700,z-900)-.7f)/.35f)*.28f);
    }
    if(G.map_style==3){base=0;over=1;blend=smooth01((slope-.18f)/.6f);}
    if(G.map_style==4){
      /* Quiet planted block interiors break up the street grid at flight height. */
      float bx=x-floorf(x/440)*440,bz=z-floorf(z/440)*440;
      float edge=fminf(fminf(bx,440-bx),fminf(bz,440-bz));
      base=1;over=3;blend=smooth01((edge-26)/35)*.62f;
    }
    if(G.map_style==5||G.map_style==6){base=4;over=1;blend=smooth01((slope-.35f)/.8f)*.3f;}
    ctl[j*nx+i]=cw_terrain_control(base,over,(int)(blend*255),0,0,false);
  }
  cw_terrain_set_heights_m(G.terrain,0,0,nx,nz,h);
  cw_terrain_set_control(G.terrain,0,0,nx,nz,ctl);
  free(h);free(ctl);
  if(!materials)return;
  const char *names[]={"sand","rock","coast","grass","snow"};float means[5][3]={{0}},linear_lut[256];for(int i=0;i<256;i++)linear_lut[i]=powf(i/255.0f,2.2f)/(1024*1024);
  for(int i=0;i<5;i++) {
    char path[96];size_t alen,nlen;
    snprintf(path,sizeof path,"terrain/%s-albedo.rgba",names[i]);
    uint8_t *a=cw_asset_load(path,&alen);
    snprintf(path,sizeof path,"terrain/%s-normal.rgba",names[i]);
    uint8_t *n=cw_asset_load(path,&nlen);
    if(!a||!n||alen!=1024*1024*4||nlen!=alen)CW_FATAL("Terrain layer failed to load");
    // Seasonal material tint: green vegetation and clean alpine snow.
    // Apply in the layer upload so the far-colour bake uses the same palette.
    if(i==3||i==4)for(size_t k=0;k<alen;k+=4) {
      const float grade[2][3]={{0.53f,0.81f,0.57f},{1.25f,1.31f,1.40f}};
      for(int c=0;c<3;c++)a[k+c]=(uint8_t)clampf(a[k+c]*grade[i==4][c],0,255);
    }
    if(G.map_style==3&&(i==0||i==1))for(size_t k=0;k<alen;k+=4){a[k]=(uint8_t)clampf(a[k]*1.2f,0,255);a[k+1]=(uint8_t)(a[k+1]*.63f);a[k+2]=(uint8_t)(a[k+2]*.42f);}
    if(G.map_style==2&&i==1)for(size_t k=0;k<alen;k+=4){a[k]=(uint8_t)(a[k]*.61f);a[k+1]=(uint8_t)(a[k+1]*.72f);a[k+2]=(uint8_t)(a[k+2]*.88f);}
    if(G.map_style==4&&i!=3)for(size_t k=0;k<alen;k+=4){float grey=a[k]*.2126f+a[k+1]*.7152f+a[k+2]*.0722f;a[k]=(uint8_t)(grey*.43f);a[k+1]=(uint8_t)(grey*.49f);a[k+2]=(uint8_t)(grey*.54f);}
    for(size_t k=0;k<alen;k+=4)for(int c=0;c<3;c++)means[i][c]+=linear_lut[a[k+c]];
    cw_terrain_set_layer_pixels(G.terrain,i,a,n,1024,1024,i==1?0.018f:(i==3?0.045f:(i==4?0.2f:0.50f)));
    free(a);free(n);
  }
  int layer=G.map_style==1?3:G.map_style==3?0:G.map_style==4?1:4;
  G.landscape_far_color=0;for(int c=0;c<3;c++){float v=means[layer][c];if(G.map_style==1)v=v*.82f+means[0][c]*.18f;G.landscape_far_color=(G.landscape_far_color<<8)|(uint32_t)clampf(powf(v,1/2.2f)*255,0,255);}
}

static t3_object *box(t3_object *parent, float x, float y, float z, float w, float h, float d,
                      t3_material *mat) {
  t3_mesh *m = t3_mesh_new(t3_box_geometry_new(w, h, d, 1, 1, 1), mat);
  m->base.cast_shadow = m->base.receive_shadow = true;
  t3_object_set_position(m, x, y, z);
  t3_object_add(parent, m);
  return (t3_object *)m;
}
static t3_object *cone(t3_object *parent, float x, float y, float z, float r, float h,
                       t3_material *mat) {
  t3_mesh *m = t3_mesh_new(t3_cone_geometry_new(r, h, 12, 1, false), mat);
  m->base.cast_shadow = m->base.receive_shadow = true;
  t3_object_set_position(m, x, y, z);
  t3_object_add(parent, m);
  return (t3_object *)m;
}
static t3_object *sphere(t3_object *parent, float x, float y, float z, float r,
                         t3_material *mat) {
  t3_mesh *m = t3_mesh_new(t3_sphere_geometry_new(r, 10, 7), mat);
  m->base.cast_shadow = m->base.receive_shadow = true;
  t3_object_set_position(m, x, y, z);
  t3_object_add(parent, m);
  return (t3_object *)m;
}
static void paved_strip(t3_object *parent, float x0, float x1, float z0, float z1,
                        t3_material *mat) {
  int nx = x1-x0 > 2.0f ? 8 : 1;
  int nz = (int)((z1-z0)/2.0f)+1;
  if (nz < 2) nz=2;
  int nv=(nx+1)*(nz+1), ni=nx*nz*6;
  float *pos=malloc((size_t)nv*3*sizeof(float));
  float *nor=malloc((size_t)nv*3*sizeof(float));
  uint32_t *idx=malloc((size_t)ni*sizeof(uint32_t));
  for (int j=0;j<=nz;j++) for(int i=0;i<=nx;i++) {
    float x=x0+(x1-x0)*i/nx, z=z0+(z1-z0)*j/nz;
    int k=(j*(nx+1)+i)*3;
    pos[k]=x;pos[k+1]=terrain_height(x,z)+0.19f;pos[k+2]=z;
    t3_vec3 normal=t3_vec3_normalize((t3_vec3){terrain_height(x-4,z)-terrain_height(x+4,z),8,terrain_height(x,z-4)-terrain_height(x,z+4)});
    nor[k]=normal.x;nor[k+1]=normal.y;nor[k+2]=normal.z;
  }
  int q=0;
  for(int j=0;j<nz;j++)for(int i=0;i<nx;i++){
    uint32_t a=(uint32_t)(j*(nx+1)+i),b=a+1,c=a+(uint32_t)(nx+1),d=c+1;
    idx[q++]=a;idx[q++]=c;idx[q++]=b;
    idx[q++]=b;idx[q++]=c;idx[q++]=d;
  }
  t3_geometry *g = t3_geometry_new(); t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, pos, nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a); t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, nor, nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a); t3_release(a);
  a = t3_attribute_new(T3_UINT32, idx, ni, 1);
  t3_geometry_set_index(g, a); t3_release(a);
  free(pos);free(nor);free(idx);
  t3_object_add(parent, t3_mesh_new(g, mat)); t3_release(g);
}
static void flight_material(t3_object *o, void *ctx) {
  if(o->type!=T3_MESH)return;
  o->cast_shadow=o->receive_shadow=true;
  t3_mesh *m=(t3_mesh *)o;
  for(int i=0;i<m->material_count;i++) {
    t3_material *old=m->materials[i],*mat=t3_material_clone(old);
    m->materials[i]=mat;t3_release(old);
    mat->roughness=clampf(mat->roughness,0.25f,0.8f);
    if(ctx){mat->color.r*=0.85f;mat->color.g*=0.79f;mat->color.b*=0.71f;}
    else if(mat->color.r+mat->color.g+mat->color.b>1.35f){
      /* Player-only cloned paint: readable over snow, retaining the texture
       * panel lines and the dark glass/cockpit/engine materials. */
      mat->color.r*=.60f;mat->color.g*=.73f;mat->color.b*=.89f;
    }
  }
}
static void share_wreck_material(t3_object *o,void *ctx) {
  (void)ctx;if(o->type!=T3_MESH)return;t3_mesh *mesh=(t3_mesh *)o;
  o->cast_shadow=o->receive_shadow=true;
  for(int i=0;i<mesh->material_count;i++)for(int j=0;j<10;j++)if(mesh->materials[i]==G.wreck_old[j]) {
    t3_material *mat=t3_material_clone(G.wreck_shared[j]);t3_release(mesh->materials[i]);mesh->materials[i]=mat;
    mat->roughness=fmaxf(mat->roughness,0.5f);mat->color.r*=0.65f;mat->color.g*=0.65f;mat->color.b*=0.65f;break;
  }
}
static t3_object *jet_model(int hostile) {
  t3_object *root = t3_group_new();
  t3_material *body = t3_mesh_standard_material_new(hostile ? 0x85302b : 0xb7c4c8);
  t3_material *wing = t3_mesh_standard_material_new(hostile ? 0xbd5141 : 0xd2dfdc);
  t3_material *dark = t3_mesh_standard_material_new(hostile ? 0x251c27 : 0x18324a);
  t3_material *metal = t3_mesh_standard_material_new(hostile ? 0x292e38 : 0x444d5c);
  box(root, 0, 0, 0.25f, 0.8f, 0.37f, 3.1f, body);
  t3_object *nose = cone(root, 0, 0, -1.72f, 0.42f, 1.8f, body);
  t3_object_set_rotation(nose, -PI * 0.5f, 0, 0);
  box(root, 0, 0.28f, -0.45f, 0.52f, 0.24f, 1.05f, dark);
  box(root, -1.18f, -0.04f, 0.22f, 2.25f, 0.10f, 1.05f, wing);
  box(root,  1.18f, -0.04f, 0.22f, 2.25f, 0.10f, 1.05f, wing);
  box(root, -0.82f, 0.0f, 1.52f, 1.5f, 0.09f, 0.52f, wing);
  box(root,  0.82f, 0.0f, 1.52f, 1.5f, 0.09f, 0.52f, wing);
  box(root, -0.34f, 0.42f, 1.55f, 0.10f, 0.72f, 0.52f, wing);
  box(root,  0.34f, 0.42f, 1.55f, 0.10f, 0.72f, 0.52f, wing);
  box(root, -0.32f, -0.15f, 1.65f, 0.36f, 0.32f, 0.53f, metal);
  box(root,  0.32f, -0.15f, 1.65f, 0.36f, 0.32f, 0.53f, metal);
  return root;
}

/* Decode on the game thread into a small SPSC buffer. The audio callback
 * only consumes ready PCM, so an eight-minute MP3 never expands in memory. */
static void music_pump(void) {
  if(!G.music_ready)return;
  unsigned w=atomic_load_explicit(&G.music_write,memory_order_relaxed);
  for(int block=0;block<32;block++) {
    unsigned r=atomic_load_explicit(&G.music_read,memory_order_acquire);
    unsigned free_frames=MUSIC_RING-(w-r);
    if(free_frames<2048)break;
    unsigned count=MUSIC_RING-(w&(MUSIC_RING-1));if(count>2048)count=2048;
    ma_uint64 got=0;
    ma_decoder_read_pcm_frames(&G.music_decoder,&G.music_ring[(w&(MUSIC_RING-1))*2],count,&got);
    if(!got){if(ma_decoder_seek_to_pcm_frame(&G.music_decoder,0)!=MA_SUCCESS)break;continue;}
    w+=(unsigned)got;atomic_store_explicit(&G.music_write,w,memory_order_release);
  }
}
static void audio_fill(void *user,float *out,int frames) {
  (void)user;
  static uint32_t noise=0x5eeda11u;
  static float low=0, boost_env=0, bass=0, music_duck=1,music_fade=0;
  static double burn_pos=0;
  float shot=G.audio_shot,boom=G.audio_boom,alert=G.audio_alert,pass=G.audio_pass;
  double phase=G.audio_phase;
  int rate=cw_audio_rate();
  float block_sum=0,block_peak=0,music_sum=0;
  float boost_attack=1-expf(-1.0f/(rate*.08f)),boost_release=1-expf(-1.0f/(rate*.16f));
  unsigned music_r=atomic_load_explicit(&G.music_read,memory_order_relaxed);
  unsigned music_w=atomic_load_explicit(&G.music_write,memory_order_acquire);
  for(int i=0;i<frames;i++) {
    noise=noise*1664525u+1013904223u;
    float white=(float)(noise>>8)/8388608.0f-1;
    low+=(white-low)*0.08f;
    phase+=6.283185307179586/rate;
    if(phase>6.283185307179586)phase-=6.283185307179586;
    int bi=(int)burn_pos;
    float bf=(float)(burn_pos-bi);
    float recorded_burn=(G.burn_pcm[bi]*(1-bf)+G.burn_pcm[(bi+1)%G.burn_samples]*bf)/32768.0f;
    burn_pos+=32000.0/rate;
    if(burn_pos>=G.burn_samples)burn_pos-=G.burn_samples;
    boost_env+=(G.audio_boost-boost_env)*(G.audio_boost>boost_env?boost_attack:boost_release);
    bass+=(white-bass)*0.005f;
    float burner=boost_env*recorded_burn*.45f; /* Continuous turbine recording; no artificial pulsing tone. */
    float blast=(low*0.18f+bass*0.8f+sinf((float)phase*39)*0.30f)*boom;
    float v=sinf((float)phase*G.audio_tone_hz)*alert+recorded_burn*pass*0.28f+(white*0.07f+sinf((float)phase*97)*0.12f)*shot+blast+burner;
    float music_l=0,music_r_sample=0;
    v*=G.sfx_gain;
    float duck=1-clampf((boom*.5f+shot*.2f+alert*1.5f+pass*.25f)*G.sfx_gain,0,.65f);
    music_duck+=(duck-music_duck)*(duck<music_duck?.002f:.00003f);
    music_fade=fminf(1,music_fade+1.0f/(rate*2));
    if(music_r!=music_w) {
      float gain=G.music_gain*music_duck*music_fade;
      music_l=G.music_ring[(music_r&(MUSIC_RING-1))*2]*gain;
      music_r_sample=G.music_ring[(music_r&(MUSIC_RING-1))*2+1]*gain;
      music_r++;G.music_played++;
    } else if(G.music_ready)G.music_underflows++;
    float left=v+music_l,right=v+music_r_sample;
    block_sum+=(left*left+right*right)*.5f;block_peak=fmaxf(block_peak,fmaxf(fabsf(left),fabsf(right)));
    music_sum+=(music_l*music_l+music_r_sample*music_r_sample)*.5f;
    out[i*2]=clampf(left,-.85f,.85f);out[i*2+1]=clampf(right,-.85f,.85f);
    shot*=0.997f;boom*=0.999975f;alert*=0.9993f;pass*=0.99985f;
  }
  atomic_store_explicit(&G.music_read,music_r,memory_order_release);
  G.music_rms=sqrtf(music_sum/fmaxf(1,frames));
  G.audio_rms=sqrtf(block_sum/fmaxf(1,frames));G.audio_peak=block_peak;
  G.audio_shot=shot;G.audio_boom=boom;G.audio_alert=alert;G.audio_pass=pass;G.audio_phase=phase;
}

static cw_node burst(const char *name,int count,float life,float size,float lo,float hi,const char *blend,bool textured) {
  cw_node n=cw_node_new("GPUParticles3D",name);
  cw_node_set(n,"emitting",cw_val_bool(false));
  cw_node_set(n,"amount",cw_val_int(count));
  cw_node_set(n,"lifetime",cw_val_float(life));
  cw_node_set(n,"lifetime_random",cw_val_float(0.25f));
  cw_node_set(n,"one_shot",cw_val_bool(true));
  cw_node_set(n,"explosiveness",cw_val_float(1));
  cw_node_set(n,"direction",cw_val_vec3(0,1,0));
  cw_node_set(n,"spread",cw_val_float(75));
  cw_node_set(n,"emission_shape",cw_val_enum("SPHERE"));
  cw_node_set(n,"emission_radius",cw_val_float(2));
  cw_node_set(n,"speed_min",cw_val_float(lo));cw_node_set(n,"speed_max",cw_val_float(hi));
  cw_node_set(n,"size",cw_val_float(size));cw_node_set(n,"size_random",cw_val_float(0.5));
  cw_node_set(n,"spin",cw_val_float(0.7));
  cw_node_set(n,"soft_distance",cw_val_float(2));
  cw_node_set(n,"blend",cw_val_enum(blend));
  if(textured)cw_node_set(n,"texture",cw_val_string("effects/billow.sprite"));
  cw_node_add_child(cw_tree_root(G.tree),n);return n;
}
static void start_burst(cw_node n,float x,float y,float z) {
  cw_node3d_set_position(n,(t3_vec3){x,y,z});cw_particles_restart(n);
}
/* Identity wrappers preserve the raw mission hierarchy and give decals an
 * exact source target, avoiding unrelated cached scenery during clipping. */
static void mark_source(t3_object *object,int index,bool ruin) {
  char name[40];snprintf(name,sizeof name,"Mark%s%d",ruin?"Ruin":"Source",index);
  cw_node n=cw_node_new("Node3D",name);cw_node_add_child(cw_tree_root(G.tree),n);
  t3_object *group=cw_node3d_object(n),*parent=object->parent;
  if(group->parent)t3_object_remove(group->parent,group);if(parent)t3_object_remove(parent,object);
  t3_object_add(parent?parent:(t3_object *)G.scene,group);t3_object_add(group,object);
}
static void register_building(int i,t3_object *node,float x,float y,float z,float radius) {
  structure *b=&G.buildings[i];b->node=node;b->x=x;b->y=y;b->z=z;b->radius=radius;b->hp=8;
  t3_material *charcoal=t3_mesh_standard_material_new(0x252724);charcoal->roughness=1;
  b->ruin=box((t3_object *)G.scene,x,terrain_height(x,z)+1,z,radius*1.6f,2,radius*1.4f,charcoal);
  b->ruin->visible=false;mark_source(node,i,false);mark_source(b->ruin,i,true);
}
/* Collect each solid mesh separately: a hangar and its control tower must
 * not create an invisible wall across the open space between them. */
static void building_collider(t3_object *o,void *ctx) {
  if(o->type!=T3_MESH)return;
  structure *b=ctx;t3_geometry *g=((t3_mesh *)o)->geometry;
  if(!g->has_bounding_box)t3_geometry_compute_bounding_box(g);
  t3_box3 local=g->bounding_box;
  if(local.max.y-local.min.y<.35f)return; /* painted/apron surfaces */
  if(b->collider_count>=16)CW_FATAL("Building collider capacity exceeded");
  t3_box3 box={{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};
  for(int i=0;i<8;i++) {
    t3_vec3 p=t3_vec3_apply_mat4((t3_vec3){i&1?local.max.x:local.min.x,i&2?local.max.y:local.min.y,i&4?local.max.z:local.min.z},&o->matrix_world);
    box.min.x=fminf(box.min.x,p.x);box.min.y=fminf(box.min.y,p.y);box.min.z=fminf(box.min.z,p.z);
    box.max.x=fmaxf(box.max.x,p.x);box.max.y=fmaxf(box.max.y,p.y);box.max.z=fmaxf(box.max.z,p.z);
  }
  b->colliders[b->collider_count++]=box;
  b->bounds.min.x=fminf(b->bounds.min.x,box.min.x);b->bounds.min.y=fminf(b->bounds.min.y,box.min.y);b->bounds.min.z=fminf(b->bounds.min.z,box.min.z);
  b->bounds.max.x=fmaxf(b->bounds.max.x,box.max.x);b->bounds.max.y=fmaxf(b->bounds.max.y,box.max.y);b->bounds.max.z=fmaxf(b->bounds.max.z,box.max.z);
}
static bool sweep_box(t3_vec3 a,t3_vec3 b,t3_box3 box,float radius,float *entry) {
  float starts[]={a.x,a.y,a.z},delta[]={b.x-a.x,b.y-a.y,b.z-a.z};
  float lo[]={box.min.x-radius,box.min.y-radius,box.min.z-radius};
  float hi[]={box.max.x+radius,box.max.y+radius,box.max.z+radius};
  float near=0,far=1;
  for(int i=0;i<3;i++) {
    if(fabsf(delta[i])<1e-6f){if(starts[i]<lo[i]||starts[i]>hi[i])return false;continue;}
    float p=(lo[i]-starts[i])/delta[i],q=(hi[i]-starts[i])/delta[i];
    if(p>q){float t=p;p=q;q=t;}near=fmaxf(near,p);far=fminf(far,q);
    if(near>far)return false;
  }
  *entry=near;return true;
}
static int aircraft_building_hit(t3_vec3 previous,t3_quat attitude,float *impact) {
  static const struct {t3_vec3 position;float radius;} probes[]={
    {{0,-.6f,-6.5f},1.3f},{{0,-.6f,0},1.2f},{{0,-.5f,6.8f},.8f},
    {{-5.1f,0,3.55f},.4f},{{5.1f,0,3.55f},.4f},{{-2.6f,0,3.55f},.4f},{{2.6f,0,3.55f},.4f},
    {{-.96f,2.1f,4.5f},.55f},{{.96f,2.1f,4.5f},.55f}
  };
  t3_vec3 now={G.px,G.py,G.pz};int hit=-1;*impact=2;
  for(int i=0;i<BUILDINGS;i++)if(G.buildings[i].hp>0) {
    structure *b=&G.buildings[i];float broad;
    if(!sweep_box(previous,now,b->bounds,9,&broad))continue;
    for(size_t p=0;p<sizeof probes/sizeof probes[0];p++) {
      t3_vec3 a=t3_vec3_apply_quat(probes[p].position,attitude),z=t3_vec3_apply_quat(probes[p].position,G.flight.attitude);
      a.x+=previous.x;a.y+=previous.y;a.z+=previous.z;z.x+=now.x;z.y+=now.y;z.z+=now.z;
      for(int j=0;j<b->collider_count;j++){float t;
        if(sweep_box(a,z,b->colliders[j],probes[p].radius,&t)&&t<*impact){*impact=t;hit=i;}
      }
    }
  }
  return hit;
}
/* Paired, noisy volumes make scattered cumulus with real depth and extinction.
 * They share the engine froxel grid; no large cloud textures or collision bodies. */
static const float cloud_sites[][3]={
    {-65,650,-600},{1300,900,-1500},{-1100,1400,-2100},{2400,1100,900},
    {-2200,1750,1000},{-600,1200,2400},{1800,1550,-3100},{-2900,1650,-900}
  };
static void make_clouds(void) {
  for(int i=0;i<8;i++) {
    float y=fmaxf(cloud_sites[i][1],terrain_height(cloud_sites[i][0],cloud_sites[i][2])+350);
    for(int lobe=0;lobe<2;lobe++) {
      char name[32];snprintf(name,sizeof name,"Cumulus%d_%d",i,lobe);
      cw_node cloud=cw_node_new("FogVolume3D",name);
      cw_node_set(cloud,"shape",cw_val_enum("ELLIPSOID"));
      cw_node_set(cloud,"size",lobe?cw_val_vec3(310,290,330):cw_val_vec3(660,230,480));
      cw_node_set(cloud,"density",cw_val_float(0.018f));
      cw_node_set(cloud,"albedo",cw_val_color(0.98f,0.99f,1,1));
      cw_node_set(cloud,"edge_fade",cw_val_float(0.65f));
      cw_node_set(cloud,"noise",cw_val_float(0.85f));
      cw_node_set(cloud,"noise_scale",cw_val_float(0.012f));
      cw_node_set(cloud,"noise_wind",cw_val_vec3(0.8f,0,0.3f));
      cw_node_set(cloud,"emission",cw_val_color(0.65f,0.67f,0.70f,1));
      cw_node_set(cloud,"emission_energy",cw_val_float(.50f));
      cw_node3d_set_position(cloud,(t3_vec3){cloud_sites[i][0]+(lobe?70:0),y+(lobe?85:0),cloud_sites[i][2]+(lobe?-30:0)});
      cw_node_add_child(cw_tree_root(G.tree),cloud);
    }
  }
}
static float cloud_cover(float x,float y,float z) {
  float cover=0;
  for(int i=0;i<8;i++) {
    float cy=fmaxf(cloud_sites[i][1],terrain_height(cloud_sites[i][0],cloud_sites[i][2])+350);
    for(int l=0;l<2;l++) {
      float dx=(x-cloud_sites[i][0]-(l?70:0))/(l?155:330);
      float dy=(y-cy-(l?85:0))/(l?145:115);
      float dz=(z-cloud_sites[i][2]+(l?30:0))/(l?165:240);
      cover=fmaxf(cover,smooth01((1-sqrtf(dx*dx+dy*dy+dz*dz))/0.65f));
    }
  }
  return cover;
}
static void notify(const char *message,float seconds) {
  snprintf(G.notice,sizeof G.notice,"%s",message);G.notice_timer=seconds;
}
static bool fuel_depot(int i){return (i>=8&&i<28&&i%6==2)||(i>=44&&i<48)||(i>=88&&i<92);}
static void make_world(void) {
  G.tree=cw_scene_app_tree();
  cw_node root=cw_tree_root(G.tree);
  G.scene=cw_render3d_scene(G.tree);
  cw_node visibility=cw_node_new("VisibilityManager","SceneryVisibility");
  cw_node_set(visibility,"occlusion_cull",cw_val_bool(false));cw_node_set(visibility,"lod",cw_val_bool(false));
  cw_node_add_child(root,visibility);
  G.camera_node=cw_node_get(root,"Coast/Camera");
  G.camera=cw_camera3d_t3(G.camera_node);
  G.jet_node=cw_node_get(root,"Coast/Aircraft");
  G.exhaust[0]=cw_node_get(root,"Coast/Aircraft/ExhaustL");
  G.exhaust[1]=cw_node_get(root,"Coast/Aircraft/ExhaustR");
  G.terrain_node=cw_node_get(root,"Coast/Land");
  G.terrain=cw_terrain_of(G.terrain_node);
  G.noise=cw_noise_default();G.noise.seed=17107;
  G.noise.frequency=0.00065f;G.noise.fractal=CW_NOISE_FRACTAL_FBM;G.noise.octaves=3;
  size_t dem_bytes;G.alpine_heights=cw_asset_load("terrain/alpine-height.f32",&dem_bytes);
  if(!G.alpine_heights||dem_bytes!=513*513*sizeof(float))CW_FATAL("Alpine elevation failed to load");
  paint_landscape(true);
  make_clouds();
  cw_world3d_def physics=cw_world3d_default_def();physics.gravity=(t3_vec3){0,-9.81f,0};physics.substeps=6;
  G.wreck_world=cw_world3d_create(&physics);
  cw_shape3d_def ground=cw_shape3d_default_def();ground.material.friction=0.7f;ground.material.restitution=0.15f;
  cw_terrain_attach_physics(G.terrain,G.wreck_world,&ground);
  cw_quality_set(CW_QUALITY_MEDIUM,false);
  G.wall_asset=cw_res_load("res://textures/concrete-wall.jpg",CW_RES_TEXTURE);
  G.roof_asset=cw_res_load("res://textures/factory-wall.jpg",CW_RES_TEXTURE);
  t3_material *wall=t3_mesh_standard_material_new(0xb7aaa0);
  t3_material_set_map(wall,cw_res_texture(G.wall_asset));wall->roughness=0.85f;
  t3_material *roof=t3_mesh_standard_material_new(0x87908c);
  t3_material_set_map(roof,cw_res_texture(G.roof_asset));roof->roughness=0.65f;
  t3_material *asphalt=t3_mesh_standard_material_new(0x303436);
  asphalt->roughness=0.95f;
  t3_material *stripe=t3_mesh_standard_material_new(0xc6c0a6);
  t3_material *glass=t3_mesh_standard_material_new(0x182935);
  glass->roughness=0.15f;glass->metalness=0.6f;
  G.airfield=t3_group_new();t3_object_add((t3_object *)G.scene,G.airfield);
  paved_strip(G.airfield,-90,-40,-1500,400,asphalt);
  paved_strip(G.airfield,10,36,-1400,360,asphalt);
  paved_strip(G.airfield,-40,170,-1070,-1035,asphalt);
  paved_strip(G.airfield,-40,170,200,235,asphalt);
  paved_strip(G.airfield,35,190,-1100,250,asphalt);
  paved_strip(G.airfield,-88,-87,-1500,400,stripe);
  paved_strip(G.airfield,-43,-42,-1500,400,stripe);
  for(int n=0;n<44;n++) {
    float z=350-n*41;
    paved_strip(G.airfield,-66,-64,z-19,z,stripe);
  }
  for(int i=0;i<8;i++) {
    float x=100,z=160-i*165;
    t3_object *g=t3_group_new();t3_object_add(G.airfield,g);
    t3_object_set_position(g,x,terrain_height(x,z),z);
    box(g,0,0.1f,0,70,0.2f,70,asphalt);
    box(g,0,9,-5,44,18,40,wall);
    t3_object *r=box(g,-11,19,-5,24,0.5f,42,roof);
    t3_object_set_rotation(r,0,0,0.10f);
    r=box(g,11,19,-5,24,0.5f,42,roof);
    t3_object_set_rotation(r,0,0,-0.10f);
    box(g,0,7.5f,15.2f,37,14,0.3f,glass);
    for(int j=0;j<6;j++)box(g,-18+j*7.2f,7.5f,15.5f,0.15f,14,0.3f,roof);
    register_building(i,g,x,terrain_height(x,z)+9,z-5,24);
    if(i==2) {
      box(g,48,20,0,9,40,9,wall);
      box(g,48,41,0,16,5,16,glass);
      box(g,48,44,0,18,0.5f,18,roof);
    }
  }
  /* The service road climbs inland to a small industrial settlement. */
  paved_strip(G.airfield,-500,36,-1000,-980,asphalt);
  paved_strip(G.airfield,-510,-492,-1320,300,asphalt);
  for(int i=0;i<20;i++) {
    float x=-550-(i%4)*85,z=220-(i/4)*190;
    float y=terrain_height(x,z),h=9+(i%4)*5;
    t3_object *building=t3_group_new();t3_object_add(G.airfield,building);
    if(fuel_depot(i+8)) {
      for(int j=0;j<3;j++) {
        t3_mesh *tank=t3_mesh_new(t3_cylinder_geometry_new(9,9,18,24,1,false),wall);
        tank->base.cast_shadow=tank->base.receive_shadow=true;
        t3_object_set_position(tank,x+(j-1)*17,y+9,z+(j==1?-9:8));t3_object_add(building,tank);
      }
    } else {
      box(building,x,y+h*0.5f,z,40+(i%3)*8,h,45,wall);
      box(building,x,y+h+0.3f,z,42+(i%3)*8,0.6f,47,roof);
    }
    register_building(i+8,building,x,y+h*0.5f,z,26);
    paved_strip(G.airfield,x-25,-492,z-6,z+6,asphalt);
  }
  for(int i=0;i<28;i++) {
    structure *b=&G.buildings[i];b->bounds=(t3_box3){{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};
    t3_object_update_world_matrix(b->node,true,true);t3_object_traverse(b->node,building_collider,b);
  }
  G.jet_asset = cw_res_load("res://models/f15-eagle.glb", CW_RES_GLTF);
  G.jet = t3_group_new();
  t3_object *airframe = cw_gltf_instantiate(G.jet_asset);
  if (!airframe) CW_FATAL("F-15 model failed to load");
  G.model_triangles = cw_gltf_instance_set_lod(airframe, 0);
  G.model_children = airframe->child_count;
  G.model_lods = cw_res_gltf(G.jet_asset)->lod_levels;
  t3_object_traverse(airframe,flight_material,NULL);
  t3_object_set_scale(airframe, 0.80f, 0.80f, 0.80f);
  t3_object_set_position(airframe, 0, -1.90f, 0);
  t3_object_set_rotation(airframe, 0, PI, 0);
  t3_object_add(G.jet, airframe);
  cw_node3d_set_object(G.jet_node,G.jet);
  for (int i = 0; i < ENEMIES; ++i) {
    G.enemies[i].node = t3_group_new();
    t3_object *other = cw_gltf_instantiate(G.jet_asset);
    G.enemies[i].modern=other;
    G.enemy_triangles = cw_gltf_instance_set_lod(other, 2);
    t3_object_traverse(other,flight_material,(void *)1);
    t3_object_set_scale(other, 0.80f, 0.80f, 0.80f);
    t3_object_set_position(other, 0, -1.90f, 0);
    t3_object_set_rotation(other, 0, PI, 0);
    t3_object_add(G.enemies[i].node, other);
    G.enemies[i].node->visible = false;
    t3_object_set_rotation(G.enemies[i].node, 0, PI, 0);
    t3_object_add((t3_object *)G.scene, G.enemies[i].node);
  }
  for (int i = 0; i < SHOTS; ++i) {
    G.shots[i].node = box((t3_object *)G.scene, 0, 0, 0, 0.13f, 0.13f, 1.8f,
        t3_mesh_basic_material_new(0xffefba));
    t3_material *core=((t3_mesh *)G.shots[i].node)->materials[0];
    core->color=(t3_color){8,5,2};
    t3_material *glow=t3_mesh_basic_material_new(0xff8b32);
    glow->color=(t3_color){4,1.2f,0.2f};glow->transparent=true;glow->opacity=0.3f;
    glow->blending=T3_ADDITIVE_BLENDING;glow->depth_write=false;
    t3_object *halo=box(G.shots[i].node,0,0,0,0.42f,0.42f,2.1f,glow);halo->cast_shadow=false;
    G.shots[i].node->visible = false;G.shots[i].node->cast_shadow=false;
  }
  for (int i = 0; i < FOE_SHOTS; ++i) {
    G.foe_shots[i].node = box((t3_object *)G.scene, 0, 0, 0, 0.13f, 0.13f, 0.90f,
        t3_mesh_basic_material_new(0xff6949));
    G.foe_shots[i].node->visible = false;G.foe_shots[i].node->cast_shadow=false;
  }
  for (int i = 0; i < FX; ++i) {
    G.blasts[i].node = sphere((t3_object *)G.scene, 0, 0, 0, 0.7f,
        t3_mesh_basic_material_new(i & 1 ? 0xffcf68 : 0xff6237));
    G.blasts[i].node->visible = false;G.blasts[i].node->cast_shadow=false;
    char name[32];snprintf(name,sizeof name,"Blast%d",i);
    cw_node n=cw_node_new("GPUParticles3D",name);G.blasts[i].particles=n;
    cw_node_set(n,"emitting",cw_val_bool(false));
    cw_node_set(n,"amount",cw_val_int(120));
    cw_node_set(n,"lifetime",cw_val_float(2.6));
    cw_node_set(n,"one_shot",cw_val_bool(true));
    cw_node_set(n,"explosiveness",cw_val_float(1));
    cw_node_set(n,"spread",cw_val_float(180));
    cw_node_set(n,"speed_min",cw_val_float(8));
    cw_node_set(n,"speed_max",cw_val_float(42));
    cw_node_set(n,"gravity",cw_val_vec3(0,-4,0));
    cw_node_set(n,"texture",cw_val_string("effects/billow.sprite"));
    cw_node_set(n,"size",cw_val_float(9));
    cw_node_set(n,"size_random",cw_val_float(0.6));
    cw_node_set(n,"energy",cw_val_float(1.5f));
    cw_node_set(n,"blend",cw_val_enum("ALPHA"));
    cw_node_set(n,"size_curve",cw_val_string("0 0.3, 0.2 1, 1 2.4"));
    cw_node_set(n,"color_ramp",cw_val_string("0 1 0.85 0.4 1, 0.2 1 0.3 0.04 0.9, 0.45 0.12 0.10 0.08 0.7, 1 0.06 0.06 0.06 0"));
    cw_node_add_child(cw_tree_root(G.tree),n);
  }
  for(int i=0;i<2;i++) {
    G.vapor[i]=cw_node_new("Trail3D",i?"WingtipRight":"WingtipLeft");
    cw_node_set(G.vapor[i],"length",cw_val_int(30));cw_node_set(G.vapor[i],"width",cw_val_float(.35f));
    cw_node_set(G.vapor[i],"blend",cw_val_enum("ALPHA"));cw_node_set(G.vapor[i],"color",cw_val_color(.9f,.95f,1,.4f));
    cw_node_set(G.vapor[i],"visible",cw_val_bool(false));cw_node_set(G.vapor[i],"emitting",cw_val_bool(false));cw_node_add_child(cw_tree_root(G.tree),G.vapor[i]);
  }
  for(int j=0;j<2;j++) {
    G.muzzle[j]=burst(j?"MuzzleR":"MuzzleL",20,0.055f,0.65f,15,60,"ADD",false);
    cw_node_set(G.muzzle[j],"direction",cw_val_vec3(0,0,-1));
    cw_node_set(G.muzzle[j],"spread",cw_val_float(8));
    cw_node_set(G.muzzle[j],"energy",cw_val_float(7));
    cw_node_set(G.muzzle[j],"color",cw_val_color(1,0.65f,0.2f,0.8f));
  }
  for(int i=0;i<2;i++) {
    G.flare_fx[i]=burst(i?"FlaresRight":"FlaresLeft",24,3.2f,0.65f,12,32,"ADD",false);
    cw_node_set(G.flare_fx[i],"gravity",cw_val_vec3(0,-9.81f,0));
    cw_node_set(G.flare_fx[i],"direction",cw_val_vec3(i?1:-1,-0.3f,0.5f));
    cw_node_set(G.flare_fx[i],"spread",cw_val_float(20));
    cw_node_set(G.flare_fx[i],"energy",cw_val_float(8));
    cw_node_set(G.flare_fx[i],"color",cw_val_color(1,0.65f,0.3f,1));
    cw_node_set(G.flare_fx[i],"size_curve",cw_val_string("0 1, 0.7 0.7, 1 0"));
  }
  const int counts[]={150,100,140,100,32,55,45};
  const float lives[]={2.4f,8,3,4.8f,5,7,6};
  const float sizes[]={17,16,0.22f,13,1.2f,6,23};
  const float low[]={12,8,28,12,25,10,5},high[]={44,23,125,55,90,18,18};
  const char *names[]={"CrashFire","CrashSmoke","CrashSparks","CrashDust","CrashDebris","CloudStem","CloudCap"};
  for(int j=0;j<7;j++) {
    cw_node n=G.crash_fx[j]=burst(names[j],counts[j],lives[j],sizes[j],low[j],high[j],j==2?"ADD":"ALPHA",j!=2&&j!=4);
    cw_node_set(n,"damping",cw_val_float(j==2||j==4?0.12f:1.1f));
    cw_node_set(n,"gravity",cw_val_vec3(0,j==2||j==4?-24:j==3?-1:4,0));
    cw_node_set(n,"size_curve",cw_val_string(j==0?"0 0.9, 0.12 1.3, 0.6 2, 1 2.8":j==2||j==4?"0 1, 0.7 0.8, 1 0":"0 0.25, 0.12 1, 0.65 2, 1 3.2"));
    cw_node_set(n,"color_ramp",cw_val_string(j==0?"0 1 0.85 0.6 0, 0.04 1 0.55 0.12 0.8, 0.3 1 0.22 0.025 0.6, 1 0.25 0.025 0.01 0":
      j==2?"0 1 0.9 0.65 1, 0.6 1 0.3 0.02 1, 1 0.2 0.01 0 0":
      j==3?"0 0.6 0.48 0.32 0, 0.1 0.55 0.43 0.3 0.32, 1 0.4 0.35 0.3 0":
      "0 0.18 0.15 0.12 0, 0.1 0.12 0.11 0.1 0.75, 0.65 0.2 0.2 0.19 0.6, 1 0.3 0.3 0.3 0"));
    cw_node_set(n,"energy",cw_val_float(j==0?3:j==2?8:1));
    if(j==2){cw_node_set(n,"draw_mode",cw_val_enum("STRETCHED"));cw_node_set(n,"stretch",cw_val_float(0.04f));}
    if(j==4){cw_node_set(n,"draw_mode",cw_val_enum("MESH"));cw_node_set(n,"mesh_primitive",cw_val_enum("BOX"));
      cw_node_set(n,"color_ramp",cw_val_string("0 0.6 0.45 0.25 1, 0.2 0.15 0.14 0.13 1, 1 0.05 0.05 0.05 0"));
      cw_node_set(n,"spin",cw_val_float(12));}
    if(j==5)cw_node_set(n,"size_curve",cw_val_string("0 0.5, 0.2 1, 1 1.8"));
    if(j>=5){cw_node_set(n,"spread",cw_val_float(j==5?5:85));cw_node_set(n,"emission_radius",cw_val_float(j==5?1.5f:14));}
  }
  cw_res wreck_asset=cw_res_load("res://models/f15-wreck.glb",CW_RES_GLTF);
  t3_object *wreck_root=cw_gltf_instantiate(wreck_asset);
  if(!wreck_root)CW_FATAL("F-15 wreck sections failed to load");
  for(int i=0;i<10;i++) {char path[96];
    snprintf(path,sizeof path,"res://models/f15-wreck.glb#material/%d",i);G.wreck_old[i]=cw_res_material(cw_res_load(path,CW_RES_MATERIAL));
    snprintf(path,sizeof path,"res://models/f15-eagle.glb#material/%d",i);G.wreck_shared[i]=cw_res_material(cw_res_load(path,CW_RES_MATERIAL));
    if(!G.wreck_old[i]||!G.wreck_shared[i])CW_FATAL("Shared aircraft material missing");
  }
  t3_object_traverse(wreck_root,share_wreck_material,NULL);
  for(int i=0;i<6;i++){char name[24];snprintf(name,sizeof name,"Wreck%d",i);
    G.wreck[i]=t3_object_get_by_name(wreck_root,name);
    if(!G.wreck[i])CW_FATAL("F-15 wreck section missing");
    t3_object_add((t3_object *)G.scene,G.wreck[i]);G.wreck[i]->visible=false;
  }
  G.shock_mat=t3_mesh_basic_material_new(0xc5b998);G.shock_mat->transparent=true;
  G.shock_mat->opacity=0;G.shock_mat->depth_write=false;G.shock_mat->side=T3_DOUBLE_SIDE;
  G.shockwave=(t3_object *)t3_mesh_new(t3_ring_geometry_new(0.985f,1,128,1),G.shock_mat);
  t3_object_set_rotation(G.shockwave,-PI/2,0,0);G.shockwave->visible=false;
  t3_object_add((t3_object *)G.scene,G.shockwave);
  campaign_make();

}

/* Standard heading, pitch and bank are independent; stunt mode uses body axes. */
static t3_quat aircraft_attitude(float heading,float pitch,float bank){return flight_attitude(heading,pitch,bank);}
/* Euler values remain inspectable; orientation itself is continuous at the poles. */
static void sync_attitude(void) {
  if(G.heading!=G.angle_cache[0]||G.pitch!=G.angle_cache[1]||G.roll!=G.angle_cache[2]||(G.flight.attitude.w==0&&G.flight.attitude.x==0&&G.flight.attitude.y==0&&G.flight.attitude.z==0)) {
    G.flight.attitude=aircraft_attitude(G.heading,G.pitch,G.roll);
    G.flight.pitch_rate=G.flight.roll_rate=G.flight.bank=G.flight.pitch_idle=0;G.camera_follow_ready=0;G.camera_up=(t3_vec3){0,1,0};
  }
  G.angle_cache[0]=G.heading;G.angle_cache[1]=G.pitch;G.angle_cache[2]=G.roll;
}
static void attitude_angles(void) {
  t3_euler e=t3_euler_from_quat(G.flight.attitude,T3_YXZ);
  G.heading=-e.y;G.pitch=e.x;G.roll=e.z;
  G.angle_cache[0]=G.heading;G.angle_cache[1]=G.pitch;G.angle_cache[2]=G.roll;
}
static t3_vec3 aircraft_local(float x,float y,float z) {
  t3_quat q=G.flight.attitude;q.x=-q.x;q.y=-q.y;q.z=-q.z;
  return t3_vec3_apply_quat((t3_vec3){x-G.px,y-G.py,z-G.pz},q);
}
/* Measure bank from the actual orientation, not accumulated control inputs.
 * A loop changes the horizon frame even when no roll input was applied. */
static float horizon_bank(t3_quat q){return flight_bank(q);}
static t3_vec3 horizon_right(t3_quat q,t3_vec3 f) {
  t3_vec3 right={-f.z,0,f.x};
  if(t3_vec3_dot(right,right)<.0004f) {
    right=t3_vec3_apply_quat((t3_vec3){1,0,0},q);right.y=0;
    if(t3_vec3_dot(right,right)<.0004f)right=(t3_vec3){1,0,0};
  }
  return t3_vec3_normalize(right);
}
static void boundary_assist(float dt,float ix,float iy,float manual_roll) {
  if(fabsf(G.px)<3500&&fabsf(G.pz)<3500)G.edge_armed=1;
  if(G.edge_armed&&(fabsf(G.px)>3700||fabsf(G.pz)>3700)){G.edge_timer=1.5f;G.edge_armed=0;}
  /* Every maneuver axis takes priority, including an attempted pull-up. */
  if(fabsf(ix)>.06f||fabsf(iy)>.06f||fabsf(manual_roll)>.06f||cw_action_pressed(0,G.aerobatics))G.edge_timer=0;
  if(G.edge_timer<=0)return;
  G.edge_timer=fmaxf(0,G.edge_timer-dt);
  t3_vec3 f=t3_vec3_apply_quat((t3_vec3){0,0,-1},G.flight.attitude);
  float heading=atan2f(f.x,-f.z),home=atan2f(-G.px,G.pz);
  float correction=clampf(cw_wrap_angle(home-heading),-1.9f*dt,1.9f*dt);
  G.flight.attitude=t3_quat_multiply(t3_quat_from_axis_angle((t3_vec3){0,1,0},-correction),G.flight.attitude);
  f=t3_vec3_apply_quat((t3_vec3){0,0,-1},G.flight.attitude);
  float pitch=asinf(clampf(f.y,-1,1));
  float clearance=G.py-fmaxf(0,terrain_height(G.px,G.pz));
  float climb=clearance<180?.18f:0;
  /* Correct upward about the horizon's right axis, even when the jet is
   * knife-edge or inverted. Yaw assistance never commands a descent. */
  float lift=clampf(climb-pitch,0,1.2f*dt);
  G.flight.attitude=t3_quat_multiply(t3_quat_from_axis_angle(horizon_right(G.flight.attitude,f),lift),G.flight.attitude);
  G.flight.attitude=t3_quat_normalize(G.flight.attitude);G.flight.yaw_rate=0;
}
static void follow_jet(void) {
  sync_attitude();
  cw_node3d_set_position(G.jet_node,(t3_vec3){G.px,G.py,G.pz});
  cw_node3d_set_quaternion(G.jet_node,G.flight.attitude);
  t3_vec3 f=t3_vec3_apply_quat((t3_vec3){0,0,-1},G.flight.attitude);
  G.visual_forward[0]=f.x;G.visual_forward[1]=f.y;G.visual_forward[2]=f.z;
  float ex=sinf(G.heading)*cosf(G.pitch)-f.x,ey=sinf(G.pitch)-f.y,ez=-cosf(G.heading)*cosf(G.pitch)-f.z;
  G.attitude_error=sqrtf(ex*ex+ey*ey+ez*ez);
  /* Let the jet visibly pitch and lead its turns instead of locking its
   * silhouette to the camera. Vector damping remains continuous in loops. */
  if(!G.camera_follow_ready){G.camera_forward=f;G.camera_follow_ready=1;}
  else G.camera_forward=t3_vec3_normalize((t3_vec3){G.camera_forward.x+(f.x-G.camera_forward.x)*.045f,G.camera_forward.y+(f.y-G.camera_forward.y)*.045f,G.camera_forward.z+(f.z-G.camera_forward.z)*.045f});
  f=G.camera_forward;
  /* Parallel transport the camera up vector through vertical flight. */
  float d=t3_vec3_dot(G.camera_up,f);
  t3_vec3 up={G.camera_up.x-f.x*d,G.camera_up.y-f.y*d,G.camera_up.z-f.z*d};
  if(t3_vec3_dot(up,up)<0.001f)up=t3_vec3_apply_quat((t3_vec3){0,1,0},G.flight.attitude);
  up=t3_vec3_normalize(up);
  t3_vec3 body_up=t3_vec3_apply_quat((t3_vec3){0,1,0},G.flight.attitude);
  G.visual_up[0]=body_up.x;G.visual_up[1]=body_up.y;G.visual_up[2]=body_up.z;
  if(body_up.y>0.3f&&fabsf(f.y)<0.8f) {
    t3_vec3 horizon=t3_vec3_normalize((t3_vec3){-f.x*f.y,1-f.y*f.y,-f.z*f.y});
    /* Rotating the up vector also handles a 180-degree mismatch. A
     * normalized linear blend gets stuck when the vectors are opposite. */
    float angle=atan2f(t3_vec3_dot(t3_vec3_cross(up,horizon),f),t3_vec3_dot(up,horizon));
    up=t3_vec3_apply_quat(up,t3_quat_from_axis_angle(f,clampf(angle*.025f,-.035f,.035f)));
  }
  G.camera_up=up;
  float look_x=cw_input_axis(0,G.look_left,G.look_right)*1.25f;
  float look_y=cw_input_axis(0,G.look_up,G.look_down)*0.65f;
  t3_vec3 right=t3_vec3_normalize(t3_vec3_cross(f,up));
  t3_quat look=t3_quat_multiply(t3_quat_from_axis_angle(up,-look_x),t3_quat_from_axis_angle(right,-look_y));
  f=t3_vec3_apply_quat(f,look);up=t3_vec3_apply_quat(up,look);
  cw_node3d_set_position(G.camera_node,(t3_vec3){G.px-f.x*34+up.x*12,G.py-f.y*34+up.y*12,G.pz-f.z*34+up.z*12});
  cw_node3d_look_at(G.camera_node,(t3_vec3){G.px+f.x*170,G.py+f.y*170,G.pz+f.z*170},up);
}

static void new_mission(void) {
  if(G.state==WIN)G.level=(G.level+1)%MISSIONS;
  G.state = PLAY;G.menu_row=0;G.crash_age=0;G.impact_age=0;G.buildings_destroyed=0;G.impact_structure=-1;G.edge_timer=0;G.edge_armed=1;
  cw_node_set(G.jet_node,"visible",cw_val_bool(true));G.jet->visible=true;G.shockwave->visible=false;G.wreck_age=0;G.wreck_count=0;
  for(int i=0;i<6;i++){if(cw_body3d_valid(G.wreck_bodies[i]))cw_body3d_destroy(G.wreck_bodies[i]);G.wreck[i]->visible=false;}
  for(int i=0;i<2;i++)cw_node_set(G.exhaust[i],"emitting",cw_val_bool(true));
  for(int i=0;i<7;i++)cw_node_set(G.crash_fx[i],"visible",cw_val_bool(false));
  for(int i=0;i<28;i++){G.buildings[i].hp=8;G.buildings[i].node->visible=true;G.buildings[i].ruin->visible=false;}
  G.ticks = G.kills = G.score = G.shots_fired = G.hits = 0;
  G.health = 100; G.missiles = MISSILE_CAPACITY; G.wave = 1;
  G.flares=1;G.vapor_width=G.flare_cool=G.enemy_missile_cool=G.flare_timer=G.notice_timer=G.alert_cool=0;G.incoming=G.decoyed=G.evasions=G.patrols=G.fuel_depots=0;

  G.px = -65; G.py = 130.0f; G.pz = 550;
  G.heading = G.pitch = G.flight.yaw_rate = G.roll = 0; G.speed = 200;
  G.flight.attitude=t3_quat_identity();G.camera_up=(t3_vec3){0,1,0};G.flight.pitch_rate=G.flight.roll_rate=G.pitch_travel=G.roll_travel=G.flight.bank=G.flight.pitch_idle=0;G.camera_follow_ready=0;
  G.angle_cache[0]=G.angle_cache[1]=G.angle_cache[2]=0;G.camera_forward=(t3_vec3){0,0,-1};
  /* Direction held while choosing Retry belongs to the menu. Require its
   * release before it can steer the new aircraft. Other axes stay available. */
  G.flight_wait_neutral=0;
  G.flight_start_input[0]=cw_input_axis(0,G.left,G.right);G.flight_start_input[1]=cw_input_axis(0,G.up,G.down);G.flight_start_input[2]=cw_input_axis(0,G.roll_left,G.roll_right);
  if(fabsf(cw_input_axis(0,G.left,G.right))>.08f)G.flight_wait_neutral|=1;
  if(fabsf(cw_input_axis(0,G.up,G.down))>.08f)G.flight_wait_neutral|=2;
  if(fabsf(cw_input_axis(0,G.roll_left,G.roll_right))>.08f)G.flight_wait_neutral|=4;
  G.boost_fuel = 100; G.hit_flash = G.kill_flash = G.missile_request = 0;
  G.boost_exhausted=G.boost_active=G.missile_launches=0;G.missile_last_target=-1;
  G.lock_target=-1;G.lock_timer=0;G.warning=0;G.manual_target=G.cycle_target=0;
  G.spawn = 50; G.gun_cool = G.missile_cool = G.damage_flash = G.world_scroll = 0;
  for (int i = 0; i < ENEMIES; ++i) G.enemies[i].active = G.enemies[i].node->visible = false;
  for (int i = 0; i < SHOTS; ++i) G.shots[i].active = G.shots[i].node->visible = false;
  for (int i = 0; i < FOE_SHOTS; ++i)
    G.foe_shots[i].active = G.foe_shots[i].node->visible = false;
  for (int i = 0; i < FX; ++i){G.blasts[i].active=G.blasts[i].node->visible=false;cw_node_set(G.blasts[i].particles,"visible",cw_val_bool(false));}
  campaign_start();follow_jet();cw_node3d_snapshot(G.jet_node);cw_node3d_snapshot(G.camera_node);
}

static void spawn_enemy(void) {
  for(int i=0;i<ENEMIES;i++)if(!G.enemies[i].active) {
    enemy *e=&G.enemies[i];e->active=e->node->visible=true;
    float side=(rand01()-0.5f)*460,range=800+rand01()*420;
    float sx=sinf(G.heading),cz=cosf(G.heading);
    e->x=G.px+sx*range+cz*side;e->z=G.pz-cz*range+sx*side;
    e->y=fmaxf(terrain_height(e->x,e->z)+90,G.py+(rand01()-0.5f)*100);
    e->heading=G.heading+(i&1?0.35f:-0.35f);
    e->vx=sinf(e->heading)*2.5f;e->vz=-cosf(e->heading)*2.5f;e->vy=0;
    e->modern->visible=G.level!=1&&G.level!=6;e->vintage->visible=G.level==1;e->sleigh->visible=G.level==6;
    e->phase=rand01()*6.28f;e->hp=G.wave>=3?4:3;
    e->role=(G.wave>=2&&i%3==0)?2:i%2;e->maneuver=0;e->maneuver_time=0;e->fire_cool=3;e->seeker=0;e->burst=e->passed=0;
    /* Patrols cross the player's path; interceptors turn in from the flanks. */
    if(e->role==1)e->heading=G.heading+PI*0.65f;
    if(e->role==2)e->heading=G.heading+PI;
    notify(e->role==2?"CONTROL: ACE INTERCEPTOR INBOUND.":e->role==1?"CONTROL: CROSSING PATROL. WEAPONS FREE.":"CONTROL: BANDIT AHEAD. CLOSE TO ENGAGE.",3);
    return;
  }
}
static float segment_distance2(float ax,float ay,float az,float bx,float by,float bz,
                               float x,float y,float z) {
  float dx=bx-ax,dy=by-ay,dz=bz-az;
  float t=clampf(((x-ax)*dx+(y-ay)*dy+(z-az)*dz)/fmaxf(0.001f,dx*dx+dy*dy+dz*dz),0,1);
  dx=x-ax-dx*t;dy=y-ay-dy*t;dz=z-az-dz*t;
  return dx*dx+dy*dy+dz*dz;
}
static bool target_position(int id,float *x,float *y,float *z) {
  if(id>=0&&id<ENEMIES&&G.enemies[id].active){*x=G.enemies[id].x;*y=G.enemies[id].y;*z=G.enemies[id].z;return true;}
  if(id>=ENEMIES&&id<ENEMIES+BUILDINGS&&G.buildings[id-ENEMIES].hp>0){structure *b=&G.buildings[id-ENEMIES];*x=b->x;*y=b->y;*z=b->z;return true;}
  return air_target_position(id,x,y,z);
}
static void update_lock(float dt) {
  int best=-1,next=-1,first=-1;float rating=1e9f;bool current_valid=false;
  for(int i=0;i<TARGET_COUNT;i++) {
    float x,y,z;if(!target_position(i,&x,&y,&z))continue;
    t3_vec3 local=aircraft_local(x,y,z);float ahead=-local.z,side=local.x,vert=local.y;
    bool ground=i>=ENEMIES&&i<ENEMIES+BUILDINGS;
    if(ahead<50||ahead>(ground?2200:2400)||fabsf(side)>ahead*(ground?.18f:.32f)||fabsf(vert)>ahead*(ground?.22f:.27f))continue;
    bool blocked=false;
    for(int j=1;j<6;j++) {float t=j/6.0f;
      if(terrain_height(G.px+(x-G.px)*t,G.pz+(z-G.pz)*t)>G.py+(y-G.py)*t+4){blocked=true;break;}
    }
    if(blocked||cloud_cover(x,y,z)>.65f||cloud_cover(G.px,G.py,G.pz)>.65f)continue;
    if(i==G.lock_target)current_valid=true;
    if(first<0)first=i;if(i>G.lock_target&&next<0)next=i;
    float r=(fabsf(side)+fabsf(vert))/ahead+(ground?.14f:0)+(i!=G.lock_target?.08f:0);
    if(r<rating){best=i;rating=r;}
  }
  if(G.manual_target&&current_valid)best=G.lock_target;
  if(!current_valid)G.manual_target=0;
  if(G.cycle_target){best=next>=0?next:first;G.cycle_target=0;G.manual_target=1;}
  if(best!=G.lock_target)G.lock_timer=0;
  float old=G.lock_timer;G.lock_target=best;
  if(best>=0)G.lock_timer=fminf(1,G.lock_timer+dt*1.5f);
  if(old<=.6f&&G.lock_timer>.6f){G.audio_alert=.08f;G.audio_tone_hz=650;}
}
static void explode(float x, float y, float z) {
  G.audio_boom = 1;
  for (int i = 0; i < FX; ++i) if (!G.blasts[i].active) {
    blast *b = &G.blasts[i];
    b->active=true;b->node->visible=false;cw_node_set(b->particles,"visible",cw_val_bool(true));
    b->x = x; b->y = y; b->z = z; b->age = 0;
    cw_node3d_set_position(b->particles,(t3_vec3){x,y,z});cw_particles_restart(b->particles);
    return;
  }
}
/* Mesh MIX decals inherit their source's light, shadow and fog. Terrain
 * uses an extension shader and cannot receive mesh clips; its explosions
 * keep the existing particle treatment. The pool is reset between sorties. */
static cw_node impact_decals[48];
static int impact_cursor;
static cw_node last_scorch;static int32_t impact_scorch_triangles;
static cw_decal_stats impact_stats;
static cw_visibility_stats scenery_stats;
static void impact_clear(void) {
  for(int i=0;i<48;i++)if(cw_node_valid(impact_decals[i]))cw_node_free(impact_decals[i]);
  memset(impact_decals,0,sizeof impact_decals);impact_cursor=0;last_scorch=(cw_node){0};impact_scorch_triangles=0;
}
static void impact_stamp(t3_vec3 p,t3_vec3 direction,float size,int building,bool ruin) {
  int at=impact_cursor++%48;if(cw_node_valid(impact_decals[at]))cw_node_free(impact_decals[at]);
  char name[48];snprintf(name,sizeof name,"Impact%d",impact_cursor);
  cw_node n=impact_decals[at]=cw_node_new("Decal3D",name);if(ruin)last_scorch=n;
  char target[48];snprintf(target,sizeof target,"../Mark%s%d",ruin?"Ruin":"Source",building);cw_node_set(n,"target",cw_val_string(target));
  cw_node_set(n,"mode",cw_val_enum("MESH"));cw_node_set(n,"blend",cw_val_enum("MIX"));
  cw_node_set(n,"size",cw_val_vec3(size,size,fmaxf(4,size*.4f)));
  cw_node_set(n,"color",cw_val_color(.045f,.033f,.025f,.85f));
  cw_node_set(n,"edge_fade",cw_val_float(0));cw_node_set(n,"normal_fade",cw_val_float(.05f));
  cw_node_set(n,"mesh_offset",cw_val_float(.025f));cw_node_set(n,"max_triangles",cw_val_int(192));
  cw_node_add_child(cw_tree_root(G.tree),n);cw_node3d_set_position(n,p);
  cw_node3d_look_at(n,t3_vec3_add(p,direction),(t3_vec3){0,1,0});
}
static void ground_explosion(float x,float y,float z,bool cloud) {
  G.audio_boom=1.5f;G.impact_age=0.001f;G.impact_x=x;G.impact_y=y;G.impact_z=z;
  for(int j=0;j<7;j++)cw_node_set(G.crash_fx[j],"visible",cw_val_bool(true));
  for(int j=0;j<5;j++)start_burst(G.crash_fx[j],x,y+2,z);
  if(cloud){start_burst(G.crash_fx[5],x,y+8,z);start_burst(G.crash_fx[6],x,y+38,z);}
  G.shockwave->visible=true;t3_object_set_position(G.shockwave,x,fmaxf(0,terrain_height(x,z))+0.6f,z);
}
static void destroy_building(int i) {
  structure *b=&G.buildings[i];if(!b->node->visible)return;
  b->hp=0;b->node->visible=false;b->ruin->visible=true;impact_stamp((t3_vec3){b->x,terrain_height(b->x,b->z)+2,b->z},(t3_vec3){.001f,-1,0},b->radius*1.4f,i,true);G.buildings_destroyed++;G.score+=200;
  ground_explosion(b->x,b->y,b->z,fuel_depot(i)||i%3==0);
  if(fuel_depot(i)) {
    G.fuel_depots++;notify("FUEL DEPOT DESTROYED / SECONDARIES",4);
    for(int k=0;k<BUILDINGS;k++)if(k!=i&&G.buildings[k].hp>0) {
      structure *n=&G.buildings[k];float dx=n->x-b->x,dz=n->z-b->z;
      if(dx*dx+dz*dz<115*115){n->hp=0;n->node->visible=false;n->ruin->visible=true;G.buildings_destroyed++;G.score+=200;explode(n->x,n->y,n->z);}
    }
  }
}
static void crash_aircraft(void) {
  G.menu_row=0;
  if(G.state==CRASH)return;
  G.py=fmaxf(G.py,fmaxf(0,terrain_height(G.px,G.pz))+5);
  G.state=CRASH;G.health=0;G.crash_age=0;
  t3_quat attitude=G.flight.attitude;
  G.wreck_age=0;G.wreck_count=6;G.wreck_contacts=0;
  for(int i=0;i<6;i++) {
    if(cw_body3d_valid(G.wreck_bodies[i]))cw_body3d_destroy(G.wreck_bodies[i]);
    t3_vec3 centre=t3_vec3_apply_quat(wreck_centres[i],attitude);
    cw_body3d_def body=cw_body3d_default_def();body.position=(t3_vec3){G.px+centre.x,G.py+centre.y+2,G.pz+centre.z};body.rotation=attitude;
    body.linear_velocity=(t3_vec3){sinf(G.heading)*G.speed*0.18f+centre.x*4,16+rand01()*18,-cosf(G.heading)*G.speed*0.18f+centre.z*4};
    body.angular_velocity=(t3_vec3){(rand01()-.5f)*8,(rand01()-.5f)*8,(rand01()-.5f)*8};
    body.linear_damping=0.10f;body.angular_damping=0.15f;body.bullet=true;
    G.wreck_bodies[i]=cw_body3d_create(G.wreck_world,&body);
    cw_shape3d_def shape=cw_shape3d_default_def();shape.group=-7;shape.density=40;shape.material.restitution=.35f;shape.material.friction=.65f;
    cw_shape3d_box(G.wreck_bodies[i],&shape,wreck_halves[i],cw_xform3_identity());
    cw_body3d_bind_object(G.wreck_bodies[i],G.wreck[i]);G.wreck[i]->visible=true;
  }
  cw_world3d_sync(G.wreck_world,1);G.audio_boom=1.7f;G.audio_boost=0;G.boost_active=0;G.missile_request=0;
  cw_node_set(G.jet_node,"visible",cw_val_bool(false));G.jet->visible=false;
  for(int j=0;j<2;j++){cw_node_set(G.exhaust[j],"emitting",cw_val_bool(false));cw_node_set(G.vapor[j],"visible",cw_val_bool(false));cw_node_set(G.vapor[j],"emitting",cw_val_bool(false));cw_node_set(G.vapor[j],"width",cw_val_float(0));}
  ground_explosion(G.px,G.py,G.pz,false);
}
static void update_effects(float dt) {
  if(G.wreck_count&&G.wreck_age<20){G.wreck_age+=dt;cw_world3d_step(G.wreck_world,dt);cw_world3d_sync(G.wreck_world,1);
    for(int i=0;i<6;i++){cw_contact3d contact[8];G.wreck_contacts+=cw_body3d_contacts(G.wreck_bodies[i],contact,8);}
  }
  for(int i=0;i<FX;i++)if(G.blasts[i].active) {
    G.blasts[i].age+=dt;G.blasts[i].node->visible=false;
    if(G.blasts[i].age>2.8f)G.blasts[i].active=false;
  }
  G.damage_flash*=0.88f;
  if(G.impact_age>0){G.impact_age+=dt;float r=6+G.impact_age*90;
    t3_object_set_scale(G.shockwave,r,r,r);G.shock_mat->opacity=fmaxf(0,0.14f-G.impact_age*0.18f);
    G.shockwave->visible=G.impact_age<1.8f;}

}
/* the shake check (cam.offset): the crash camera as drawn (the camera node's global transform) against its orbit around
 * the wreck, on every crash frame. cam.offset: those frames, the ones whose drawn camera was off the orbit by over 0.1 mm,
 * and the largest offset (micrometres). Nothing moves the camera in a crash: [n, 0, 0]. */
static int32_t cam_offset[3];
static void offsetgate(t3_vec3 at) {
  const t3_mat4 *m=cw_node3d_global_transform(G.camera_node);if(!m)return;
  float dx=m->e[12]-at.x,dy=m->e[13]-at.y,dz=m->e[14]-at.z,off=sqrtf(dx*dx+dy*dy+dz*dz);
  cam_offset[0]++;if(off>1e-4f)cam_offset[1]++;
  if((int32_t)(off*1e6f)>cam_offset[2])cam_offset[2]=(int32_t)(off*1e6f);
}
static void crash_camera(float dt) {
  float old=G.crash_age;G.crash_age+=dt;float t=G.crash_age;
  if(old<0.55f&&t>=0.55f)start_burst(G.crash_fx[5],G.px,G.py+8,G.pz);
  if(old<1.1f&&t>=1.1f)start_burst(G.crash_fx[6],G.px,G.py+38,G.pz);
  if(old<0.3f&&t>=0.3f){explode(G.px+8,G.py+6,G.pz-5);G.audio_boom=1;}
  float radius=65+fminf(t,5)*15,ang=G.heading+0.3f+t*0.12f;
  float cx=G.px-sinf(ang)*radius,cz=G.pz+cosf(ang)*radius;
  float shake=expf(-t*2.2f)*2.4f;
  cw_node3d_set_position(G.camera_node,(t3_vec3){cx+sinf(t*93)*shake,
    fmaxf(G.py+25+t*9,terrain_height(cx,cz)+15)+cosf(t*71)*shake,cz});
  cw_node3d_look_at(G.camera_node,(t3_vec3){G.px,G.py+12+t*4,G.pz},(t3_vec3){0,1,0});
  offsetgate((t3_vec3){cx,fmaxf(G.py+25+t*9,terrain_height(cx,cz)+15),cz});
  cw_node_set(G.camera_node,"fov",cw_val_float(68));
  float r=6+t*90;t3_object_set_scale(G.shockwave,r,r,r);
  G.shock_mat->opacity=fmaxf(0,0.14f-t*0.18f);G.shockwave->visible=t<1.8f;
  G.audio_engine=0;
  if(t>5.5f)G.state=LOST;
}
static void fire_shot(int missile) {
  if(missile&&(G.lock_target<0||G.lock_timer<0.6f))return;
  for(int i=0;i<SHOTS;i++)if(!G.shots[i].active) {
    shot *s=&G.shots[i];s->active=s->node->visible=true;
    float side=G.shots_fired&1?1.2f:-1.2f;
    float velocity=missile?8.5f:18.0f;
    t3_vec3 muzzle=t3_vec3_apply_quat((t3_vec3){side,-0.3f,-9},G.flight.attitude);
    s->x=G.px+muzzle.x;s->z=G.pz+muzzle.z;s->y=G.py+muzzle.y;
    s->missile=missile;s->target=missile?G.lock_target:-1;
    s->vx=G.visual_forward[0]*velocity;s->vy=G.visual_forward[1]*velocity;
    s->vz=G.visual_forward[2]*velocity;
    if(missile) {
      t3_object_set_scale(s->node,2.2f,2.2f,1.7f);G.missiles--;G.missile_cool=65;
      G.missile_request=0;G.missile_launches++;G.missile_last_target=s->target;notify("MISSILE AWAY",1.2f);
      if(CW_HANDLE_IS_NULL(s->trail)) {
        char name[32];snprintf(name,sizeof name,"MissileTrail%d",i);
        s->trail=cw_node_new("Trail3D",name);
        cw_node_set(s->trail,"length",cw_val_int(45));
        cw_node_set(s->trail,"width",cw_val_float(0.7));
        cw_node_set(s->trail,"energy",cw_val_float(1));
        cw_node_set(s->trail,"blend",cw_val_enum("ALPHA"));
        cw_node_set(s->trail,"color",cw_val_color(0.85f,0.85f,0.8f,0.45f));
        cw_node_set(s->trail,"color_ramp",cw_val_string("0 1 1 1 0, 0.8 1 1 1 0.5, 1 1 1 1 1"));
        cw_node_add_child(cw_tree_root(G.tree),s->trail);
      }
      cw_node3d_set_position(s->trail,(t3_vec3){s->x,s->y,s->z});
      cw_node_set(s->trail,"visible",cw_val_bool(true));cw_node_set(s->trail,"emitting",cw_val_bool(true));cw_node_set(s->trail,"width",cw_val_float(.7f));
    } else {
      t3_object_set_scale(s->node,1,1,7);
      float best=1e9f;
      for(int j=0;j<ENEMIES;j++)if(G.enemies[j].active) {
        enemy *e=&G.enemies[j];t3_vec3 local=aircraft_local(e->x,e->y,e->z);
        float ahead=-local.z,side=local.x,vertical=local.y;
        if(ahead<12||ahead>1500||fabsf(side)>ahead*0.055f+4||fabsf(vertical)>ahead*0.05f+4)continue;
        float rating=fabsf(side)+fabsf(vertical)+ahead*0.01f;
        if(rating<best) {
          best=rating;float lead=ahead/velocity;
          float ax=e->x+e->vx*lead-s->x,ay=e->y+e->vy*lead-s->y,az=e->z+e->vz*lead-s->z;
          float inv=velocity/fmaxf(1,sqrtf(ax*ax+ay*ay+az*az));
          s->vx=ax*inv;s->vy=ay*inv;s->vz=az*inv;
        }
      }
      G.gun_cool=4;
    }
    t3_object_set_rotation(s->node,-asinf(clampf(s->vy/velocity,-1,1)),atan2f(-s->vx,-s->vz),0);
    G.shots_fired++;G.audio_shot=missile?1.5f:0.9f;
    if(!missile)start_burst(G.muzzle[G.shots_fired&1],s->x,s->y,s->z);
    return;
  }
}

/* Hostile missiles fly at 528 m/s and turn at most 28 degrees/second.
   Use an angular cap, independent of the angle to the target. The deterministic
   opposite-vector fallback also avoids an undefined axis after a head-on pass. */
static void steer_hostile_missile(shot *s,t3_vec3 aim,float step,float degrees) {
  t3_vec3 direction={s->vx/8.8f,s->vy/8.8f,s->vz/8.8f};
  t3_vec3 desired={aim.x-s->x,aim.y-s->y,aim.z-s->z};
  float distance=sqrtf(desired.x*desired.x+desired.y*desired.y+desired.z*desired.z);
  if(distance<.01f)return;
  desired.x/=distance;desired.y/=distance;desired.z/=distance;
  float dot=clampf(direction.x*desired.x+direction.y*desired.y+direction.z*desired.z,-1,1);
  float angle=acosf(dot),turn=fminf(angle,degrees*(3.14159265f/180)*step);
  if(angle<.0001f)return;
  t3_vec3 tangent={desired.x-direction.x*dot,desired.y-direction.y*dot,desired.z-direction.z*dot};
  float length=sqrtf(tangent.x*tangent.x+tangent.y*tangent.y+tangent.z*tangent.z);
  if(length<.0001f){tangent=(t3_vec3){-direction.z,0,direction.x};length=sqrtf(tangent.x*tangent.x+tangent.z*tangent.z);
    if(length<.0001f){tangent=(t3_vec3){1,0,0};length=1;}}
  float c=cosf(turn),k=sinf(turn)/length;
  s->vx=8.8f*(direction.x*c+tangent.x*k);
  s->vy=8.8f*(direction.y*c+tangent.y*k);
  s->vz=8.8f*(direction.z*c+tangent.z*k);
}

static void enemy_fire(enemy *e,bool missile) {
  for(int i=0;i<FOE_SHOTS;i++)if(!G.foe_shots[i].active) {
    shot *s=&G.foe_shots[i];float dx=G.px-e->x,dy=G.py-e->y,dz=G.pz-e->z;
    float velocity=missile?8.8f:12.0f,inv=velocity/fmaxf(1,sqrtf(dx*dx+dy*dy+dz*dz));
    s->x=e->x;s->y=e->y;s->z=e->z;s->vx=dx*inv;s->vy=dy*inv;s->vz=dz*inv;
    s->active=s->node->visible=true;s->missile=missile;s->target=1;s->age=s->obscured=0;
    t3_object_set_scale(s->node,missile?2:1,missile?2:1,missile?3:1);
    if(missile&&CW_HANDLE_IS_NULL(s->trail)) {
      char name[32];snprintf(name,sizeof name,"HostileMissile%d",i);s->trail=cw_node_new("Trail3D",name);
      cw_node_set(s->trail,"length",cw_val_int(65));cw_node_set(s->trail,"width",cw_val_float(.6f));
      cw_node_set(s->trail,"blend",cw_val_enum("ALPHA"));cw_node_set(s->trail,"color",cw_val_color(.9f,.9f,.88f,.45f));
      cw_node_add_child(cw_tree_root(G.tree),s->trail);
    }
    if(!CW_HANDLE_IS_NULL(s->trail)){cw_node3d_set_position(s->trail,(t3_vec3){s->x,s->y,s->z});cw_node_set(s->trail,"visible",cw_val_bool(missile));cw_node_set(s->trail,"emitting",cw_val_bool(missile));cw_node_set(s->trail,"width",cw_val_float(missile?.6f:0));}
    return;
  }
}

#include "campaign.h"

static void init(void *user) {
  (void)user;
  G.rng = 0x5eeda11u;
  make_world();cw_debug_i32("forest.grid",&forest_grid);
  G.left = cw_action_add("bank_left", 0.06f);
  G.right = cw_action_add("bank_right", 0.06f);
  G.up = cw_action_add("climb", 0.06f);
  G.down = cw_action_add("dive", 0.06f);
  G.gun = cw_action_add("guns", 0.35f);
  G.missile = cw_action_add("missile", 0.5f);
  G.flares_action=cw_action_add("countermeasures",0.35f);cw_action_bind_text(G.flares_action,"key:f, pad:rstick, axis:lt+@0.35");
  G.brake=cw_action_add("airbrake",0.5f);
  cw_action_bind_text(G.brake,"key:lctrl, pad:lshoulder");
  G.roll_left=cw_action_add("roll_left",0.06f);G.roll_right=cw_action_add("roll_right",0.06f);
  G.aerobatics=cw_action_add("aerobatic_roll",0.5f);
  cw_action_bind_text(G.roll_left,"key:q");
  cw_action_bind_text(G.roll_right,"key:e");
  cw_action_bind_text(G.aerobatics,"key:r, pad:rshoulder");
  G.look_left=cw_action_add("look_left",0.08f);G.look_right=cw_action_add("look_right",0.08f);
  G.look_up=cw_action_add("look_up",0.08f);G.look_down=cw_action_add("look_down",0.08f);
  cw_action_bind_text(G.look_left,"axis:rx-");cw_action_bind_text(G.look_right,"axis:rx+");
  cw_action_bind_text(G.look_up,"axis:ry-");cw_action_bind_text(G.look_down,"axis:ry+");
  G.boost = cw_action_add("afterburner", 0.5f);
  G.pause = cw_action_add("pause", 0.5f);
  G.settings = cw_action_add("settings", 0.5f);
  G.back = cw_action_add("back", 0.5f);
  cw_action_bind_text(G.left, "key:left, key:a, pad:left, axis:lx-");
  cw_action_bind_text(G.right, "key:right, key:d, pad:right, axis:lx+");
  cw_action_bind_text(G.up, "key:up, key:w, pad:up, axis:ly-");
  cw_action_bind_text(G.down, "key:down, key:s, pad:down, axis:ly+");
  cw_action_bind_text(G.gun, "key:space, key:return, pad:south, axis:rt+@0.35");
  cw_action_bind_text(G.missile, "key:m, pad:west");
  cw_action_bind_text(G.boost, "key:lshift, key:rshift, pad:east");
  cw_action_bind_text(G.pause, "key:p, key:escape, pad:start");
  cw_action_bind_text(G.settings, "key:i, pad:north");
  cw_action_bind_text(G.back, "key:escape, pad:east");
  G.title_font = cw_font_load("fonts/RussoOne-Regular.ttf", NULL);
  G.ui_font = cw_font_load("fonts/Oswald-VF.ttf", NULL);
  cw_font_preload_range(G.title_font,32,126);cw_font_preload_range(G.ui_font,32,126);menu_load();
  cw_debug_i32("mission.state", &G.state);
  cw_debug_i32("mission.level",&G.level);cw_debug_i32("menu.row",&G.menu_row);cw_debug_f32_array("menu.pointer",&menu_ui.pointer.x,2);cw_debug_i32("mission.balloons",&G.balloon_kills);cw_debug_i32("mission.blimps",&G.blimp_kills);cw_debug_i32("mission.shields",&G.shields_left);
  cw_debug_f32("mission.elapsed",&G.mission_time);cw_debug_f32("mothership.explosion_age",&capital.age);cw_debug_i32("mothership.fragments",&capital.fragments);cw_debug_f32_array("mothership.fragment_position",&capital.chunks[0]->position.x,3);
  static char air_keys[AIR_TARGETS][7][40];
  for(int i=0;i<AIR_TARGETS;i++) {
    const char *names[]={"x","y","z","basket_y","hull_hp","basket_hp","state"};
    for(int j=0;j<7;j++)snprintf(air_keys[i][j],40,"air.%d.%s",i,names[j]);
    air_target *a=&G.air_targets[i];
    cw_debug_f32(air_keys[i][0],&a->x);cw_debug_f32(air_keys[i][1],&a->y);cw_debug_f32(air_keys[i][2],&a->z);cw_debug_f32(air_keys[i][3],&a->by);
    cw_debug_i32(air_keys[i][4],&a->hull_hp);cw_debug_i32(air_keys[i][5],&a->basket_hp);cw_debug_i32(air_keys[i][6],&a->state);
  }
  cw_debug_i32("mission.ticks", &G.ticks);
  cw_debug_i32("mission.kills", &G.kills);
  cw_debug_i32("mission.score", &G.score);
  cw_debug_i32("mission.health", &G.health);
  cw_debug_i32("mission.missiles", &G.missiles);
  cw_debug_f32("missile.queued",&G.missile_request);cw_debug_f32("missile.cooldown",&G.missile_cool);cw_debug_i32("missile.launches",&G.missile_launches);cw_debug_i32("missile.last_target",&G.missile_last_target);
  cw_debug_f32("boost.fuel",&G.boost_fuel);cw_debug_i32("boost.active",&G.boost_active);cw_debug_i32("boost.exhausted",&G.boost_exhausted);
  cw_debug_i32("mission.shots", &G.shots_fired);
  static char enemy_keys[ENEMIES][8][40];
  for(int i=0;i<ENEMIES;i++){
    const char *suffix[]={"x","y","z","heading","hp","active","fire_cool","seeker"};
    for(int j=0;j<8;j++)snprintf(enemy_keys[i][j],40,"enemy.%d.%s",i,suffix[j]);
    cw_debug_f32(enemy_keys[i][0],&G.enemies[i].x);cw_debug_f32(enemy_keys[i][1],&G.enemies[i].y);
    cw_debug_f32(enemy_keys[i][2],&G.enemies[i].z);cw_debug_f32(enemy_keys[i][3],&G.enemies[i].heading);
    cw_debug_i32(enemy_keys[i][4],&G.enemies[i].hp);cw_debug_i32(enemy_keys[i][5],&G.enemies[i].active);
    cw_debug_f32(enemy_keys[i][6],&G.enemies[i].fire_cool);cw_debug_f32(enemy_keys[i][7],&G.enemies[i].seeker);
  }
  cw_debug_f32("audio.rms",&G.audio_rms);cw_debug_f32("audio.peak",&G.audio_peak);
  cw_debug_i32("mission.buildings",&G.buildings_destroyed);cw_debug_i32("crash.structure",&G.impact_structure);
  cw_debug_f32("crash.age",&G.crash_age);
  cw_debug_field("cam.offset",cam_offset,CW_DBG_I32,3);
  cw_debug_i32("crash.pieces",&G.wreck_count);
  cw_debug_i32("jet.intact_visible",&G.intact_visible);
  cw_debug_i32("cloud.volumes",&G.cloud_volumes);cw_debug_i32("cloud.active",&G.cloud_active);
  cw_debug_i32("crash.contacts",&G.wreck_contacts);
  cw_debug_f32("edge.assist",&G.edge_timer);
  cw_debug_i32("mission.hits", &G.hits);
  cw_debug_i32("defense.flares",&G.flares);cw_debug_i32("defense.incoming",&G.incoming);
  cw_debug_i32("defense.decoyed",&G.decoyed);cw_debug_i32("defense.evasions",&G.evasions);
  cw_debug_f32("defense.flare_cooldown",&G.flare_cool);cw_debug_f32("defense.launch_cooldown",&G.enemy_missile_cool);
  cw_debug_f32("defense.eta",&G.threat_eta);cw_debug_f32("defense.range",&G.threat_range);
  static char hostile_keys[3][10][40];
  for(int i=0;i<3;i++){
    const char *suffix[]={"x","y","z","vx","vy","vz","age","active","missile","target"};
    for(int j=0;j<10;j++)snprintf(hostile_keys[i][j],40,"hostile.%d.%s",i,suffix[j]);
    shot *s=&G.foe_shots[i];
    cw_debug_f32(hostile_keys[i][0],&s->x);cw_debug_f32(hostile_keys[i][1],&s->y);cw_debug_f32(hostile_keys[i][2],&s->z);
    cw_debug_f32(hostile_keys[i][3],&s->vx);cw_debug_f32(hostile_keys[i][4],&s->vy);cw_debug_f32(hostile_keys[i][5],&s->vz);
    cw_debug_f32(hostile_keys[i][6],&s->age);cw_debug_i32(hostile_keys[i][7],&s->active);
    cw_debug_i32(hostile_keys[i][8],&s->missile);cw_debug_i32(hostile_keys[i][9],&s->target);
  }
  cw_debug_f32("mission.spawn",&G.spawn);
  cw_debug_i32("mission.patrols",&G.patrols);cw_debug_i32("mission.depots",&G.fuel_depots);
  cw_debug_i32("model.triangles", &G.model_triangles);
  cw_debug_i32("model.children", &G.model_children);
  cw_debug_i32("model.lods", &G.model_lods);
  cw_debug_i32("model.enemy_triangles", &G.enemy_triangles);
  cw_debug_f32("jet.x", &G.px);
  cw_debug_f32("jet.altitude", &G.py);
  cw_debug_f32("jet.z", &G.pz);
  cw_debug_f32("jet.heading", &G.heading);
  cw_debug_f32("jet.yaw_rate", &G.flight.yaw_rate);
  cw_debug_f32("jet.roll",&G.roll);
  cw_debug_f32("jet.pitch", &G.pitch);
  cw_debug_f32("jet.pitch_travel",&G.pitch_travel);cw_debug_f32("jet.roll_travel",&G.roll_travel);
  cw_debug_f32_array("jet.visual_forward",G.visual_forward,3);
  cw_debug_f32_array("jet.visual_up",G.visual_up,3);
  cw_debug_f32_array("jet.camera_forward",&G.camera_forward.x,3);
  cw_debug_f32_array("jet.camera_up",&G.camera_up.x,3);
  cw_debug_f32("jet.attitude_error",&G.attitude_error);
  cw_debug_f32("jet.pitch_rate",&G.flight.pitch_rate);cw_debug_f32("jet.roll_rate",&G.flight.roll_rate);cw_debug_f32("jet.pitch_idle",&G.flight.pitch_idle);
  cw_debug_i32("jet.wait_neutral",&G.flight_wait_neutral);cw_debug_f32_array("jet.control_input",G.flight_input,3);
  size_t burn_bytes,music_bytes;
  G.burn_pcm=cw_asset_load("audio/engine-burn.s16",&burn_bytes);G.burn_samples=burn_bytes/2;
  if(!G.burn_pcm||G.burn_samples<32000)CW_FATAL("Afterburner audio failed to load");
  G.music_data=cw_asset_load("audio/music/GallopingRiff.mp3",&music_bytes);
  ma_decoder_config music_config=ma_decoder_config_init(ma_format_f32,2,48000);
  if(!G.music_data||ma_decoder_init_memory(G.music_data,music_bytes,&music_config,&G.music_decoder)!=MA_SUCCESS)CW_FATAL("GallopingRiff music failed to decode");
  G.music_ready=1;G.music_gain=.10f;music_pump();
  cw_debug_f32("audio.sfx_gain",&G.sfx_gain);
  cw_debug_f32("audio.music_gain",&G.music_gain);cw_debug_f32("audio.music_rms",&G.music_rms);
  cw_debug_i32("audio.music_ready",&G.music_ready);cw_debug_i32("audio.music_underflows",&G.music_underflows);cw_debug_i32("audio.music_played",&G.music_played);
  if(prefs.magic==SAVE_MAGIC_V1){prefs.magic=SAVE_MAGIC;prefs.music_volume=10;prefs.sfx_volume=100;prefs.progress=0;}
  if(prefs.magic==SAVE_MAGIC_V2){prefs.magic=SAVE_MAGIC;prefs.progress=0;}
  if (prefs.magic != SAVE_MAGIC) {
    memset(&prefs, 0, sizeof prefs);
    prefs.magic = SAVE_MAGIC;prefs.music_volume=10;prefs.sfx_volume=100;
  }
  prefs.progress=(prefs.progress&255u)|(((prefs.progress>>8)%MISSIONS)<<8);G.level=(prefs.progress>>8)%MISSIONS;
  static char building_keys[BUILDINGS][4][32];
  for(int i=0;i<BUILDINGS;i++){if(!((i>=44&&i<=47)||(i>=68&&i<=71)||(i>=84&&i<=86)||(i>=88&&i<=91)))continue;
    const char *names[]={"hp","x","y","z"};for(int j=0;j<4;j++)snprintf(building_keys[i][j],32,"building.%d.%s",i,names[j]);
    cw_debug_i32(building_keys[i][0],&G.buildings[i].hp);cw_debug_f32(building_keys[i][1],&G.buildings[i].x);cw_debug_f32(building_keys[i][2],&G.buildings[i].y);cw_debug_f32(building_keys[i][3],&G.buildings[i].z);
  }
  cw_debug_u32("campaign.progress",&prefs.progress);cw_debug_i32("map.style",&G.map_style);
  prefs.invert_pitch = !!prefs.invert_pitch;
  prefs.expert_controls=!!prefs.expert_controls;
  prefs.music_volume=(uint8_t)clampf(prefs.music_volume,0,100);prefs.sfx_volume=(uint8_t)clampf(prefs.sfx_volume,0,100);
  G.music_gain=prefs.music_volume*.01f;G.sfx_gain=prefs.sfx_volume*.01f;
  cw_quality_restore(prefs.graphics);cw_save_commit();
  cw_audio_open(48000,audio_fill,NULL);
  cw_debug_i32("render.skipped",&G.skipped);cw_debug_i32("art.hide_hud",&art_hide_hud);
  cw_debug_i32("impact.scorch_triangles",&impact_scorch_triangles);cw_debug_i32("impact.decals",&impact_stats.mesh_decals);cw_debug_i32("impact.triangles",&impact_stats.mesh_triangles);cw_debug_i32("impact.builds",&impact_stats.mesh_builds);
  cw_debug_i32("scenery.meshes",&scenery_stats.meshes);cw_debug_i32("scenery.visible",&scenery_stats.visible);cw_debug_i32("scenery.culled",&scenery_stats.frustum_culled);
  cw_debug_i32("target.index",&G.lock_target);
  cw_debug_f32("target.lock",&G.lock_timer);
  cw_debug_f32("jet.speed_mps",&G.speed);
  static const char *positions[]={"enemy.0.position","enemy.1.position","enemy.2.position",
    "enemy.3.position","enemy.4.position","enemy.5.position","enemy.6.position"};
  for(int i=0;i<ENEMIES;i++)cw_debug_f32_array(positions[i],&G.enemies[i].x,3);
  G.state = TITLE; G.health = 100; G.missiles = MISSILE_CAPACITY;G.flares=1;
  G.px = -65; G.py = 130.0f; G.pz = 550;
  G.heading = G.pitch = G.roll = 0;
  G.speed = 200; G.boost_fuel = 100; G.lock_target=-1;
  follow_jet();
}

static void fixed_update(void *user, double dt) {
  (void)user;
  float step = (float)dt;
  music_pump();menu_read_pointer();forest_frame(step);
  cw_decals_get_stats(G.tree,&impact_stats);cw_visibility_get_stats(G.tree,&scenery_stats);
  cw_decal_mesh_info scorch;cw_decal_get_mesh_info(last_scorch,&scorch);impact_scorch_triangles=scorch.triangles;
  G.intact_visible=G.jet->visible;
  cw_volumetric_fog_state clouds;cw_volumetric_fog_get_state(G.tree,&clouds);
  G.cloud_volumes=clouds.volumes;G.cloud_active=clouds.active;
  if(G.state==SETTINGS) {
    int pointer_direction=0;
    if(menu_ui.clicked){
      if(menu_ui.pointer.x>=240&&menu_ui.pointer.x<=360&&menu_ui.pointer.y>=450&&menu_ui.pointer.y<=480){G.state=G.return_state;return;}
      for(int i=0;i<5;i++)if(menu_ui.pointer.x>=230&&menu_ui.pointer.x<=730&&menu_ui.pointer.y>=169+i*47&&menu_ui.pointer.y<207+i*47){G.settings_row=i;pointer_direction=menu_ui.pointer.x<590?-1:1;}
    }
    if(cw_action_just_pressed(0,G.up))G.settings_row=(G.settings_row+4)%5;
    if(cw_action_just_pressed(0,G.down))G.settings_row=(G.settings_row+1)%5;
    if(cw_action_just_pressed(0,G.gun)||cw_action_just_pressed(0,G.left)||cw_action_just_pressed(0,G.right)||pointer_direction) {
      int direction=pointer_direction?pointer_direction:cw_action_just_pressed(0,G.left)?-1:1;
      if(G.settings_row==0)prefs.invert_pitch=!prefs.invert_pitch;
      else if(G.settings_row==1) { cw_quality q=cw_quality_get();cw_quality_set((cw_quality)((q+3+direction)%3),true);prefs.graphics=cw_quality_saved(); }
      if(G.settings_row==2){prefs.expert_controls=!prefs.expert_controls;G.flight.bank=G.flight.pitch_idle=G.flight.roll_rate=0;}
      if(G.settings_row>=3) {
        uint8_t *volume=G.settings_row==3?&prefs.music_volume:&prefs.sfx_volume;
        int value=*volume+direction*5;
        if(cw_action_just_pressed(0,G.gun)&&value>100)value=0;
        *volume=(uint8_t)clampf(value,0,100);
        G.music_gain=prefs.music_volume*.01f;G.sfx_gain=prefs.sfx_volume*.01f;
      }
      cw_save_commit();
    }
    if(cw_action_just_pressed(0,G.back)||cw_action_just_pressed(0,G.pause)||cw_action_just_pressed(0,G.settings))G.state=G.return_state;
    G.audio_engine=0.16f;return;
  }
  if ((G.state == TITLE || G.state == PAUSED || G.state==WIN || G.state==LOST) && cw_action_just_pressed(0, G.settings)) {
    G.return_state = G.state;
    G.state = SETTINGS;
    return;
  }
  if(G.state!=PAUSED){update_effects(step);campaign_update(step);}
  if(G.state==VICTORY){victory_camera(step);return;}
  if(G.state==CRASH){crash_camera(step);return;}
  bool burn_requested=cw_action_pressed(0,G.boost),braking=cw_action_pressed(0,G.brake);
  /* Do not pulse the burner around an empty tank: release and recharge to
   * five percent before relighting. Airbrake has priority over boost. */
  if(!burn_requested&&G.boost_fuel>=5)G.boost_exhausted=0;
  if(G.boost_fuel<=.5f)G.boost_exhausted=1;
  int boosting=G.state==PLAY&&burn_requested&&!G.boost_exhausted&&!braking;
  G.boost_active=boosting;
  if (cw_action_just_pressed(0, G.pause)) {
    if (G.state == PLAY){G.state = PAUSED;G.menu_row=0;G.audio_boost=0;G.boost_active=0;G.missile_request=0;return;}
    else if (G.state == PAUSED) G.state = PLAY;
  }
  if(menu_tick()){G.audio_boost=0;G.boost_active=0;G.missile_request=0;if(G.state!=PLAY)follow_jet();return;}
  if (G.state != PLAY) {
    G.audio_boost=0;G.boost_active=0;G.missile_request=0;
    G.audio_engine = G.state == PAUSED ? 0.16f : 0.30f;
    if(G.state!=LOST)follow_jet();
    return;
  }
  G.ticks++;
  G.wave = 1 + G.kills / 4;
  G.notice_timer=fmaxf(0,G.notice_timer-step);G.flare_timer=fmaxf(0,G.flare_timer-step);
  G.flare_cool=fmaxf(0,G.flare_cool-step);G.flares=G.flare_cool<=0;
  G.enemy_missile_cool=fmaxf(0,G.enemy_missile_cool-step);
  if(cw_action_just_pressed(0,G.settings))G.cycle_target=1;
  if(cw_action_just_pressed(0,G.flares_action)&&G.flares>0&&G.flare_timer<.2f) {
    G.flares=0;G.flare_cool=7;G.flare_timer=3;for(int i=0;i<2;i++)start_burst(G.flare_fx[i],G.px+(i?2:-2),G.py-1,G.pz+4);
    notify("COUNTERMEASURES AWAY",2);
  }
  float ix = cw_input_axis(0, G.left, G.right);
  float iy = cw_input_axis(0, G.up, G.down);
  float manual_roll=cw_input_axis(0,G.roll_left,G.roll_right);
  G.flight_input[0]=ix;G.flight_input[1]=iy;G.flight_input[2]=manual_roll;
  float *axes[]={&ix,&iy,&manual_roll};
  for(int axis=0;axis<3;axis++)if(G.flight_wait_neutral&(1<<axis)){
    if(fabsf(*axes[axis])<=.08f||*axes[axis]*G.flight_start_input[axis]<0)G.flight_wait_neutral&=~(1<<axis);
    else *axes[axis]=0;
  }
  /* Softer around centre for aiming, full authority at the edge. */
  ix=ix*(0.72f+0.28f*fabsf(ix));iy=iy*(0.72f+0.28f*fabsf(iy));
  G.boost_fuel=clampf(G.boost_fuel+(boosting?-1.5f:2.5f)*step,0,100);
  G.hit_flash=fmaxf(0,G.hit_flash-1);G.kill_flash=fmaxf(0,G.kill_flash-1);
  float target_speed=boosting?600:braking?135:205;
  G.speed+=(target_speed-G.speed)*step*(braking?1.8f:boosting?1.15f:0.8f);
  cw_node_set(G.camera_node,"fov",cw_val_float(64+clampf((G.speed-205)/215,0,1)*16));
  G.audio_engine=boosting?0.95f:0.56f;
  sync_attitude();
  t3_vec3 previous_position={G.px,G.py,G.pz};t3_quat previous_attitude=G.flight.attitude;
  bool stunt=prefs.expert_controls||cw_action_pressed(0,G.aerobatics)||fabsf(manual_roll)>.06f;
  if(prefs.expert_controls||cw_action_pressed(0,G.aerobatics))manual_roll=ix;
  flight_tick(&G.flight,(flight_input){.turn=ix,.pitch=prefs.invert_pitch?iy:-iy,.roll=manual_roll,.speed=G.speed,.stunt=stunt},step);
  G.pitch_travel+=G.flight.pitch_rate*step;G.roll_travel+=G.flight.roll_rate*step;
  boundary_assist(step,ix,iy,manual_roll);
  attitude_angles();
  t3_vec3 flight=t3_vec3_apply_quat((t3_vec3){0,0,-1},G.flight.attitude);
  float travel=G.speed*step;
  G.px+=flight.x*travel;G.pz+=flight.z*travel;
  G.py=clampf(G.py+flight.y*travel,1,8000);
  float impact;int building=aircraft_building_hit(previous_position,previous_attitude,&impact);
  if(building>=0) {
    G.px=previous_position.x+(G.px-previous_position.x)*impact;
    G.py=previous_position.y+(G.py-previous_position.y)*impact;
    G.pz=previous_position.z+(G.pz-previous_position.z)*impact;
    G.impact_structure=building;destroy_building(building);crash_aircraft();crash_camera(step);return;
  }
  if(campaign_aircraft_collision(previous_position)){crash_aircraft();crash_camera(step);return;}
  float floor=fmaxf(0,terrain_height(G.px,G.pz));
  G.warning=G.py-floor<45?1:0;
  if(G.py<floor+3){crash_aircraft();crash_camera(step);return;}
  follow_jet();update_lock(step);
  for(int i=0;i<2;i++) {
    cw_node_set(G.exhaust[i],"color",boosting?cw_val_color(0.85f,0.22f,0.55f,0.55f):cw_val_color(0.48f,0.19f,0.85f,0.25f));
    cw_node_set(G.exhaust[i],"energy",cw_val_float(boosting?4.5f:1.2f));
    cw_node_set(G.exhaust[i],"speed_max",cw_val_float(boosting?90:26));
    cw_node_set(G.exhaust[i],"size",cw_val_float(boosting?0.45f:0.28f));
  }
  G.audio_boost=boosting?1:0;
  float load=sqrtf(1+powf(G.flight.pitch_rate*G.speed/9.81f,2)+powf(G.flight.yaw_rate*G.speed/9.81f,2));
  bool condensation=load>3.5f||cloud_cover(G.px,G.py,G.pz)>.3f;
  G.vapor_width+=((condensation?.22f:0)-G.vapor_width)*(1-expf(-step*8));
  for(int i=0;i<2;i++) {
    t3_vec3 tip=t3_vec3_apply_quat((t3_vec3){i?6.1f:-6.1f,-.35f,.4f},G.flight.attitude);
    cw_node3d_set_position(G.vapor[i],(t3_vec3){G.px+tip.x,G.py+tip.y,G.pz+tip.z});
    cw_node_set(G.vapor[i],"visible",cw_val_bool(condensation));
    cw_node_set(G.vapor[i],"emitting",cw_val_bool(condensation));
    cw_node_set(G.vapor[i],"width",cw_val_float(G.vapor_width));
  }
  G.gun_cool -= 1; G.missile_cool -= 1;
  if (cw_action_pressed(0, G.gun) && G.gun_cool <= 0) fire_shot(0);
  /* A tap survives acquisition/reload; holding waits for readiness and
   * repeats at the normal reload rate. Never spend an unguided missile. */
  bool missile_down=cw_action_pressed(0,G.missile);
  if(cw_action_just_pressed(0,G.missile)){
    if(G.missiles<=0)notify("NO MISSILES REMAINING",1.5f);
    else {G.missile_request=1.25f;notify(G.missile_cool>0?"MISSILE QUEUED / RELOADING":G.lock_timer<.6f?"MISSILE QUEUED / KEEP TARGET IN SIGHT":"MISSILE READY",1.25f);}
  }
  if(missile_down&&G.missiles>0)G.missile_request=fmaxf(G.missile_request,.15f);
  if(G.missile_request>0){
    G.missile_request=fmaxf(0,G.missile_request-step);
    if(G.missiles>0&&G.missile_cool<=0)fire_shot(1);
    if(G.missile_request==0&&!missile_down&&G.lock_timer<.6f)notify("NO LOCK / KEEP TARGET IN SIGHT",1.5f);
  }
  G.spawn -= 1;
  if (G.spawn <= 0) {
    int active=0;for(int i=0;i<ENEMIES;i++)active+=!!G.enemies[i].active;
    int limit=G.level==0?0:G.level==1?(G.kills>=8?0:3):G.level==4?2:4;
    if(active<limit)spawn_enemy();
    G.spawn = clampf(420-G.wave*18,240,420);
  }
  bool missile_busy=false;
  for(int i=0;i<FOE_SHOTS;i++)if(G.foe_shots[i].active&&G.foe_shots[i].missile&&G.foe_shots[i].target!=-3)missile_busy=true;
  for (int i = 0; i < ENEMIES; ++i) if (G.enemies[i].active) {
    enemy *e = &G.enemies[i];
    float to_x=G.px-e->x,to_z=G.pz-e->z;
    float dist=sqrtf(to_x*to_x+to_z*to_z);
    float want=atan2f(to_x,to_z*-1);
    float error=cw_wrap_angle(want-e->heading);
    e->maneuver_time=fmaxf(0,e->maneuver_time-step);
    if(e->maneuver_time<=0)e->maneuver=0;
    for(int j=0;j<SHOTS;j++)if(G.shots[j].active&&G.shots[j].missile&&G.shots[j].target==i) {
      float dx=G.shots[j].x-e->x,dy=G.shots[j].y-e->y,dz=G.shots[j].z-e->z;
      if(dx*dx+dy*dy+dz*dz<450*450&&e->maneuver==0){e->maneuver=2;e->maneuver_time=1.4f;}
    }
    if(dist<210&&e->maneuver==0){e->maneuver=1;e->maneuver_time=1.8f;}
    float turn=e->maneuver==1?0:e->maneuver==2?(i&1?-1:1)*1.05f*step:clampf(error,-(e->role==2?.85f:.55f)*step,(e->role==2?.85f:.55f)*step);
    e->heading+=turn;
    float v=(G.level==1?95+15*(i%3):G.level==6?155:e->maneuver==1?245:e->role==2?225:175+fminf(G.wave,6)*5)*step;
    if(e->propeller)t3_object_set_rotation(e->propeller,0,0,G.ticks*.9f);
    e->vx=sinf(e->heading)*v;e->vz=-cosf(e->heading)*v;
    float target_y=fmaxf(terrain_height(e->x,e->z)+90,G.py+65*sinf(G.ticks*.007f+e->phase)+(e->maneuver==2?140:0));
    e->vy=clampf((target_y-e->y)*.4f,-65,80)*step;
    e->fire_cool=fmaxf(0,e->fire_cool-step);
    float nose=(to_x*sinf(e->heading)-to_z*cosf(e->heading))/fmaxf(dist,1);
    bool tracking=nose>.94f&&fabsf(G.py-e->y)<dist*.35f&&cloud_cover(G.px,G.py,G.pz)<.6f;
    e->seeker=tracking&&dist>900&&dist<1900?e->seeker+step:0;
    if(G.level!=1&&!missile_busy&&G.enemy_missile_cool<=0&&e->fire_cool<=0&&e->seeker>2.4f){enemy_fire(e,true);e->fire_cool=12;e->seeker=0;G.enemy_missile_cool=12;missile_busy=true;}
    else if(e->fire_cool<=0&&tracking&&nose>.985f&&dist>120&&dist<900){enemy_fire(e,false);e->burst++;e->fire_cool=e->burst%3?.18f:2.8f;}
    e->x+=e->vx;e->y+=e->vy;e->z+=e->vz;
    t3_object_set_position(e->node,e->x,e->y,e->z);
    t3_object_set_quaternion(e->node,aircraft_attitude(e->heading,
      asinf(clampf(e->vy/v,-1,1)),-clampf(error,-0.9f,0.9f)));
    float dx=e->x-G.px, dy=e->y-G.py, dz=e->z-G.pz;
    float pass_range=dx*dx+dy*dy+dz*dz;
    if(pass_range>500*500)e->passed=0;
    if(pass_range<90*90&&!e->passed){e->passed=1;G.near_misses++;G.audio_pass=.8f;}
    if (pass_range < 90.0f) {
      G.health -= 26; G.damage_flash = 1;
      explode(G.px, G.py, G.pz);
      e->active = e->node->visible = false;
      if (G.health <= 0){crash_aircraft();return;}
    } else if (dx*dx + dz*dz > 3800.0f*3800.0f) {
      e->active = e->node->visible = false;
    }
  }
  G.incoming=0;G.threat_range=G.threat_eta=1e9f;G.alert_cool-=step;
  /* Collision and closure use relative motion of BOTH aircraft and missile. */
  t3_vec3 jet_velocity={(G.px-previous_position.x)/step,(G.py-previous_position.y)/step,(G.pz-previous_position.z)/step};
  for(int i=0;i<FOE_SHOTS;i++) {
    shot *s=&G.foe_shots[i];
    if(!s->active){if(!CW_HANDLE_IS_NULL(s->trail)){cw_node_set(s->trail,"visible",cw_val_bool(false));cw_node_set(s->trail,"emitting",cw_val_bool(false));cw_node_set(s->trail,"width",cw_val_float(0));}continue;}
    s->age+=step;
    float dx=G.px-s->x,dy=G.py-s->y,dz=G.pz-s->z,range=sqrtf(dx*dx+dy*dy+dz*dz);
    if(s->missile&&s->target==1) {
      if(G.flare_timer>1&&range<1100){
        /* Flares draw the seeker to a fixed point off the jet's path. They do
           not teleport or disarm the missile: a last-instant release is late. */
        t3_vec3 decoy=t3_vec3_apply_quat((t3_vec3){i&1?180:-180,-60,100},G.flight.attitude);
        s->decoy[0]=G.px+decoy.x;s->decoy[1]=G.py+decoy.y;s->decoy[2]=G.pz+decoy.z;
        s->target=-2;s->obscured=0;G.decoyed++;notify("FLARES / SEEKER DIVERTED",2);
      } else {
        s->obscured=cloud_cover(G.px,G.py,G.pz)>.6f?s->obscured+step:0;
        float alignment=(dx*s->vx+dy*s->vy+dz*s->vz)/fmaxf(range*8.8f,1);
        /* Seeker cannot reacquire after a break, cloud escape or motor burnout. */
        if(s->obscured>.45f||alignment<.258819f||s->age>7)s->target=-1;
        else steer_hostile_missile(s,(t3_vec3){G.px,G.py,G.pz},step,28);
      }
    }
    if(s->missile&&s->target==-2){
      s->obscured+=step;
      if(s->obscured<1.2f)steer_hostile_missile(s,(t3_vec3){s->decoy[0],s->decoy[1],s->decoy[2]},step,90);
    }
    float ox=s->x,oy=s->y,oz=s->z;
    s->x+=s->vx;s->y+=s->vy;s->z+=s->vz;t3_object_set_position(s->node,s->x,s->y,s->z);
    t3_object_set_rotation(s->node,-asinf(clampf(s->vy/(s->missile?8.8f:12),-1,1)),atan2f(-s->vx,-s->vz),0);
    if(!CW_HANDLE_IS_NULL(s->trail))cw_node3d_set_position(s->trail,(t3_vec3){s->x,s->y,s->z});
    float clearance=segment_distance2(ox-previous_position.x,oy-previous_position.y,oz-previous_position.z,s->x-G.px,s->y-G.py,s->z-G.pz,0,0,0);
    if(clearance<(s->missile?64:25)) {
      s->active=s->node->visible=false;G.health-=s->missile?35:9;G.damage_flash=.8f;
      if(G.health<=0){crash_aircraft();return;}
      continue;
    }
    float closing=(dx*(s->vx/step-jet_velocity.x)+dy*(s->vy/step-jet_velocity.y)+dz*(s->vz/step-jet_velocity.z))/fmaxf(range,1);
    if(s->missile&&s->target<0&&s->target!=-3&&closing<0&&range>120){
      /* Only announce success after a clean pass, never on seeker loss alone. */
      if(s->target==-1)G.evasions++;
      s->target=-3;notify("MISSILE DEFEATED",2);
    }
    if(s->age>9||range>5000||s->y<fmaxf(0,terrain_height(s->x,s->z))+1) {
      s->active=s->node->visible=false;if(s->missile)explode(s->x,s->y,s->z);
    } else if(s->missile&&s->target!=-3) {
      G.incoming++;
      float eta=range/fmaxf(closing,1);
      if(eta<G.threat_eta){G.threat_eta=eta;G.threat_range=range;G.threat_bearing=cw_wrap_angle(atan2f(s->x-G.px,G.pz-s->z)-G.heading);}
    }
  }
  if(G.incoming&&G.alert_cool<=0){G.audio_alert=.12f;G.audio_tone_hz=G.threat_eta<=1.6f?1250:850;G.alert_cool=clampf(G.threat_eta*.2f,.12f,.65f);}
  for (int i = 0; i < SHOTS; ++i) if (G.shots[i].active) {
    shot *s = &G.shots[i];
    float tx,ty,tz;
    if (s->missile && target_position(s->target,&tx,&ty,&tz)) {
      float dx=tx-s->x, dy=ty-s->y, dz=tz-s->z;
      float inv=10.5f/fmaxf(1.0f,sqrtf(dx*dx+dy*dy+dz*dz));
      s->vx += (dx*inv-s->vx)*0.14f;
      s->vy += (dy*inv-s->vy)*0.14f;
      s->vz += (dz*inv-s->vz)*0.14f;
    }
    float ox=s->x,oy=s->y,oz=s->z;
    s->x += s->vx; s->y += s->vy; s->z += s->vz;
    t3_object_set_position(s->node, s->x, s->y, s->z);
    if(!CW_HANDLE_IS_NULL(s->trail))cw_node3d_set_position(s->trail,(t3_vec3){s->x,s->y,s->z});
    if (s->missile)
      t3_object_set_rotation(s->node, -asinf(clampf(s->vy/10.5f,-1,1)),
                             atan2f(-s->vx,-s->vz), 0);
    for (int j = 0; j < ENEMIES; ++j) if (G.enemies[j].active) {
      enemy *e = &G.enemies[j];
      float dx=e->x-s->x, dy=e->y-s->y, dz=e->z-s->z;
      if(segment_distance2(ox,oy,oz,s->x,s->y,s->z,e->x,e->y,e->z)<(s->missile?100:36)) {
        s->active = s->node->visible = false;
        G.hits++; G.hit_flash = 12; e->hp -= s->missile ? 3 : 1;
        if (e->hp <= 0) {
          e->active = e->node->visible = false;
          G.kills++; G.score += 100 + G.wave * 20; G.kill_flash = 54;
          if (G.kills % 2 == 0 && G.missiles < MISSILE_CAPACITY) G.missiles++;
          explode(e->x, e->y, e->z);
          if(G.kills%12==0){G.patrols++;G.spawn=1500;notify("CONTROL: AIRSPACE SECURED. PATROL CONTINUES.",7);}
        }
        break;
      }
    }
    if(s->active) {
      int id=air_target_hit(ox,oy,oz,s->x,s->y,s->z,s->missile);
      if(id>=0){air_target_damage(id,s->missile);s->active=s->node->visible=false;G.hits++;G.hit_flash=12;}
      else if(campaign_shield_hit(ox,oy,oz,s->x,s->y,s->z))s->active=s->node->visible=false;
    }
    if(s->active)for(int j=0;j<BUILDINGS;j++)if(G.buildings[j].hp>0) {
      structure *b=&G.buildings[j];
      if(segment_distance2(ox,oy,oz,s->x,s->y,s->z,b->x,b->y,b->z)>b->radius*b->radius)continue;
      s->active=s->node->visible=false;G.hit_flash=12;G.hits++;
      b->hp-=s->missile?12:1;
      if(b->hp<=0)destroy_building(j);
      else {static t3_raycaster *ray;const t3_intersection *hits;
        if(!ray)ray=t3_raycaster_new();t3_vec3 a={ox,oy,oz},direction=t3_vec3_normalize(t3_vec3_sub((t3_vec3){s->x,s->y,s->z},a));
        ray->near=0;ray->far=b->radius*4+100;t3_raycaster_set(ray,a,direction);t3_object_update_world_matrix(b->node,true,true);
        if(t3_raycaster_intersect_object(ray,b->node,true,&hits)>0)impact_stamp(hits[0].point,direction,2.4f,j,false);
        explode(s->x,s->y,s->z);}
      break;
    }
    if(s->active&&district_hit((t3_vec3){ox,oy,oz},(t3_vec3){s->x,s->y,s->z},s->missile?3:0,false)){s->active=s->node->visible=false;G.hits++;G.hit_flash=12;}
    if(s->active&&s->y<fmaxf(0,terrain_height(s->x,s->z))+1) {
      if(s->missile)ground_explosion(s->x,s->y,s->z,false);else explode(s->x,s->y,s->z);
      s->active=s->node->visible=false;
    }
    float dx=s->x-G.px, dz=s->z-G.pz;
    if (dx*dx + dz*dz > 3000.0f*3000.0f)
      s->active = s->node->visible = false;
  }
  for(int i=0;i<SHOTS;i++)if(!CW_HANDLE_IS_NULL(G.shots[i].trail)) {
    bool active=G.shots[i].active&&G.shots[i].missile;
    cw_node_set(G.shots[i].trail,"visible",cw_val_bool(active));cw_node_set(G.shots[i].trail,"emitting",cw_val_bool(active));
    cw_node_set(G.shots[i].trail,"width",cw_val_float(active?.7f:0));
  }
  campaign_finish();
}

static void text(cw_font f, const char *s, float x, float y, float size, cw_color color, float width,
                 cw_text_align align) {
  cw_r2d_text(f, s, (cw_vec2){ x, y }, &(cw_text_style){ .size = size, .color = color,
    .wrap_width = width, .align = align, .outline_width = size * 0.032f,
    .outline_color = cw_hex(0x07111be0) });
}
static bool hud_project(float x,float y,float z,cw_vec2 *out) {
  t3_vec3 v=t3_vec3_apply_mat4((t3_vec3){x,y,z},&G.camera->matrix_world_inverse);
  if(v.z>-1)return false;
  v=t3_vec3_apply_mat4(v,&G.camera->projection_matrix);
  out->x=(v.x*0.5f+0.5f)*W;out->y=(0.5f-v.y*0.5f)*H;
  return out->x>20&&out->x<W-20&&out->y>40&&out->y<H-45;
}
static void hud(void) {
  if(G.state==VICTORY){cw_render3d_stats stats;cw_render3d_get_stats(G.tree,&stats);G.skipped=stats.skipped;return;}
  cw_color green=cw_hex(0xa6edbcdf),muted=cw_hex(0xadddc6a8),white=cw_hex(0xe3ece7ff);
  cw_color amber=cw_hex(0xffb366ff),red=cw_hex(0xff6655ff);
  char buf[128];
  text(G.ui_font,mission_name(),28,21,15,white,0,CW_ALIGN_LEFT);
  text(G.ui_font,mission_goal(),28,47,13,amber,670,CW_ALIGN_LEFT);
  snprintf(buf,sizeof buf,"AIR KILLS %02d   SCORE %06d",G.kills,G.score);
  text(G.ui_font,buf,W-280,21,15,white,255,CW_ALIGN_RIGHT);
  int heading=((int)(G.heading*180/PI)%360+360)%360;
  snprintf(buf,sizeof buf,"%03d",heading);
  text(G.title_font,buf,W*0.5f-40,37,17,green,80,CW_ALIGN_CENTER);
  for(int i=-3;i<=3;i++) {
    float x=W*0.5f+i*37;
    cw_r2d_line((cw_vec2){x,24},(cw_vec2){x,30+(i==0?4:0)},1,muted);
  }
  snprintf(buf,sizeof buf,"%03d",(int)(G.speed*1.94384f));
  text(G.title_font,buf,155,232,25,green,80,CW_ALIGN_CENTER);
  text(G.ui_font,"KNOTS",155,267,11,muted,80,CW_ALIGN_CENTER);
  snprintf(buf,sizeof buf,"%05d",(int)(G.py*3.28084f));
  text(G.title_font,buf,W-240,232,25,green,105,CW_ALIGN_CENTER);
  text(G.ui_font,"FT MSL",W-240,267,11,muted,105,CW_ALIGN_CENTER);
  cw_r2d_line((cw_vec2){230,245},(cw_vec2){245,245},1,green);
  cw_r2d_line((cw_vec2){W-245,245},(cw_vec2){W-230,245},1,green);
  if(G.state==PLAY){
    float bank=horizon_bank(G.flight.attitude),tilt=G.pitch;
    float cy=clampf(H*.48f+tilt*H*.7f,H*.2f,H*.78f),dx=cosf(bank)*80,dy=sinf(bank)*80;
    cw_r2d_line((cw_vec2){W*.5f-dx,cy-dy},(cw_vec2){W*.5f-dx*.30f,cy-dy*.30f},1.5f,green);
    cw_r2d_line((cw_vec2){W*.5f+dx*.30f,cy+dy*.30f},(cw_vec2){W*.5f+dx,cy+dy},1.5f,green);
  }
  cw_vec2 sight={W*0.5f,H*0.44f};
  hud_project(G.px+sinf(G.heading)*cosf(G.pitch)*800,G.py+sinf(G.pitch)*800,
              G.pz-cosf(G.heading)*cosf(G.pitch)*800,&sight);
  cw_r2d_circle_lines(sight,10,1,24,green);
  cw_r2d_line((cw_vec2){sight.x-22,sight.y},(cw_vec2){sight.x-10,sight.y},1,green);
  cw_r2d_line((cw_vec2){sight.x+10,sight.y},(cw_vec2){sight.x+22,sight.y},1,green);
  cw_r2d_line((cw_vec2){sight.x,sight.y-18},(cw_vec2){sight.x,sight.y-10},1,green);
  if(G.state==PLAY) {
    for(int i=0;i<ENEMIES;i++)if(G.enemies[i].active) {
      enemy *e=&G.enemies[i];cw_vec2 p;
      if(!hud_project(e->x,e->y,e->z,&p))continue;
      bool locked=i==G.lock_target&&G.lock_timer>0.6f;
      float r=locked?17:12;cw_color c=locked?amber:muted;
      cw_r2d_line((cw_vec2){p.x-r,p.y-r},(cw_vec2){p.x+r,p.y-r},1,c);
      cw_r2d_line((cw_vec2){p.x-r,p.y+r},(cw_vec2){p.x+r,p.y+r},1,c);
      cw_r2d_line((cw_vec2){p.x-r,p.y-r},(cw_vec2){p.x-r,p.y+r},1,c);
      cw_r2d_line((cw_vec2){p.x+r,p.y-r},(cw_vec2){p.x+r,p.y+r},1,c);
      float dx=e->x-G.px,dz=e->z-G.pz;
      snprintf(buf,sizeof buf,"%s %.1f KM",locked?"LOCK":e->role==2?"ACE":"HOSTILE",sqrtf(dx*dx+dz*dz)*0.001f);
      if(locked||i==G.lock_target)text(G.ui_font,buf,p.x-70,p.y+r+6,12,c,140,CW_ALIGN_CENTER);
    }
    if(G.lock_target>=ENEMIES){float x,y,z;cw_vec2 p;
      if(target_position(G.lock_target,&x,&y,&z)&&hud_project(x,y,z,&p)) {
        cw_r2d_circle_lines(p,20,1,4,amber);
        text(G.ui_font,G.lock_timer>0.6f?(G.lock_target>=ENEMIES+BUILDINGS?(G.level==4?(G.shields_left?"SHIELD GENERATOR":"CORE EXPOSED"):G.level==6?"SANTA LOCK":((G.lock_target-ENEMIES-BUILDINGS)%2?"BASKET LOCK":"ENVELOPE LOCK")):(fuel_depot(G.lock_target-ENEMIES)?"FUEL DEPOT LOCK":"GROUND LOCK")):"ACQUIRING",p.x-60,p.y+26,12,amber,120,CW_ALIGN_CENTER);
      }
    }
    if(G.lock_target>=0&&G.lock_target<ENEMIES) {
      enemy *e=&G.enemies[G.lock_target];float dx=e->x-G.px,dy=e->y-G.py,dz=e->z-G.pz;
      float time=sqrtf(dx*dx+dy*dy+dz*dz)/1080;cw_vec2 lead;
      if(hud_project(e->x+e->vx*60*time,e->y+e->vy*60*time,e->z+e->vz*60*time,&lead))cw_r2d_circle_lines(lead,5,1,12,green);
    }
    if(G.notice_timer>0)text(G.ui_font,G.notice,28,H-79,12,white,650,CW_ALIGN_LEFT);
    if(G.incoming) {
      text(G.title_font,G.threat_eta<=1.6f?"BREAK NOW / R3 OR LT: FLARES":"MISSILE INBOUND",W/2-220,110,20,red,440,CW_ALIGN_CENTER);
      char threat[80];snprintf(threat,sizeof threat,"%d INBOUND  /  %.0f M  /  %.1f SEC CLOSING",G.incoming,G.threat_range,fminf(G.threat_eta,99.9f));
      text(G.ui_font,threat,W/2-240,136,12,white,480,CW_ALIGN_CENTER);
      float x=W/2+sinf(G.threat_bearing)*210,y=H/2-cosf(G.threat_bearing)*135;
      cw_r2d_circle_lines((cw_vec2){x,y},9,2,3,red);
    }
    if(G.warning>0)text(G.title_font,"PULL UP",W/2-90,115,24,red,180,CW_ALIGN_CENTER);
    if(G.missiles<=0)snprintf(buf,sizeof buf,"NO MISSILES");
    else if(G.missile_cool>0)snprintf(buf,sizeof buf,"MISSILE RELOAD %.1f SEC",G.missile_cool/60);
    else snprintf(buf,sizeof buf,"%s",G.lock_timer>.6f?"GUIDED MISSILE READY":G.missile_request>0?"MISSILE QUEUED / ACQUIRING":"MISSILE / AIM FOR LOCK");
    text(G.ui_font,buf,W/2-130,H-120,13,amber,260,CW_ALIGN_CENTER);
    if(G.hit_flash>0)text(G.ui_font,"HIT",sight.x+25,sight.y+15,14,white,60,CW_ALIGN_LEFT);
    if(G.kill_flash>0)text(G.title_font,"TARGET DESTROYED",W/2-150,82,19,amber,300,CW_ALIGN_CENTER);
    cw_vec2 rc={W-88,H-104};
    cw_r2d_circle(rc,48,48,cw_hex(0x05131799));
    cw_r2d_circle_lines(rc,48,1,48,muted);
    cw_r2d_line((cw_vec2){rc.x,rc.y-48},(cw_vec2){rc.x,rc.y+48},1,cw_hex(0xa6edbc33));
    cw_r2d_line((cw_vec2){rc.x-48,rc.y},(cw_vec2){rc.x+48,rc.y},1,cw_hex(0xa6edbc33));
    float sn=sinf(G.heading),cs=cosf(G.heading);
    for(int j=0;j<ENEMIES;j++)if(G.enemies[j].active) {
      float dx=G.enemies[j].x-G.px,dz=G.enemies[j].z-G.pz;
      cw_r2d_circle((cw_vec2){rc.x+clampf((dx*cs+dz*sn)*0.025f,-44,44),
                     rc.y-clampf((dx*sn-dz*cs)*0.025f,-44,44)},2.5f,8,amber);
    }
    cw_r2d_circle(rc,2,8,green);
  }
  if(G.state==PLAY) {
    for(int i=0;i<AIR_TARGETS*2;i++) {
      float x,y,z;int id=ENEMIES+BUILDINGS+i;if(!air_target_position(id,&x,&y,&z))continue;
      float distance=t3_vec3_length((t3_vec3){x-G.px,y-G.py,z-G.pz});
      /* The selected object already owns a reticle and one caption above.
       * Baskets are discoverable up close without doubling every distant marker. */
      if(id==G.lock_target||distance>4200||(i%2&&distance>450))continue;
      cw_vec2 p;if(!hud_project(x,y,z,&p))continue;
      if(p.x>W*.40f&&p.x<W*.60f&&p.y>H*.64f&&p.y<H*.86f)continue;
      cw_r2d_circle_lines(p,i%2?5:9,1,4,muted);
      const char *label=G.air_targets[i/2].kind==0?(i%2?"BASKET":"BALLOON"):G.air_targets[i/2].kind==1?(i%2?"GONDOLA":"BLIMP"):G.air_targets[i/2].kind==2?"SHIELD GEN":G.air_targets[i/2].kind==4?"SANTA":"CORE";
      if(distance<450&&i%2)text(G.ui_font,label,p.x-55,p.y+12,11,muted,110,CW_ALIGN_CENTER);
    }
    int wp=campaign_waypoint();float x,y,z;
    if(wp>=0&&!G.incoming&&target_position(wp,&x,&y,&z)) {
      t3_vec3 local=aircraft_local(x,y,z);float distance=t3_vec3_length(local);
      snprintf(buf,sizeof buf,"OBJECTIVE %.1f KM / %s",distance*.001f,local.z>0?"TURN BACK":fabsf(local.x)>-local.z*.2f?(local.x<0?"LEFT":"RIGHT"):local.y>150?"CLIMB":local.y< -150?"DESCEND":"AHEAD");
      text(G.ui_font,buf,28,73,12,muted,540,CW_ALIGN_LEFT);
      cw_vec2 marker;if(!hud_project(x,y,z,&marker)){
        float bearing=cw_wrap_angle(atan2f(x-G.px,G.pz-z)-G.heading);float sx=W*.5f+clampf(bearing/(PI*.5f),-1,1)*(W*.5f-55),sy=H*.30f;
        cw_r2d_circle_lines((cw_vec2){sx,sy},14,2,4,amber);text(G.ui_font,"OBJECTIVE",sx-65,sy+20,12,amber,130,CW_ALIGN_CENTER);
        if(fabsf(bearing)>PI*.65f)text(G.ui_font,"BEHIND",sx-65,sy+37,12,amber,130,CW_ALIGN_CENTER);
      }
      cw_vec2 rc={W-88,H-104};float dx=x-G.px,dz=z-G.pz,sn=sinf(G.heading),cs=cosf(G.heading);
      cw_r2d_circle((cw_vec2){rc.x+clampf((dx*cs+dz*sn)*.025f,-44,44),rc.y-clampf((dx*sn-dz*cs)*.025f,-44,44)},4,8,amber);
    }
  }
  cw_r2d_rect((cw_rect){24,H-47,355,25},cw_hex(0x07161a88));
  if(G.flares)snprintf(buf,sizeof buf,"AIRFRAME %03d   MSL %02d   FLARES READY [R3/LT]",G.health,G.missiles);
  else snprintf(buf,sizeof buf,"AIRFRAME %03d   MSL %02d   FLARES %.0f SEC",G.health,G.missiles,ceilf(G.flare_cool));
  text(G.ui_font,buf,31,H-44,12,white,342,CW_ALIGN_LEFT);
  text(G.ui_font,G.boost_exhausted?"BURNER: RELEASE / RECHARGE":"AFTERBURNER",W/2-(G.boost_exhausted?160:95),H-43,11,muted,G.boost_exhausted?170:110,CW_ALIGN_LEFT);
  cw_r2d_rect((cw_rect){W/2+12,H-36,88,3},cw_hex(0x263b3bbb));
  cw_r2d_rect((cw_rect){W/2+12,H-36,G.boost_fuel*0.88f,3},amber);
  if(cw_action_pressed(0,G.brake)&&G.state==PLAY)text(G.ui_font,"AIRBRAKE",W/2-60,H-67,14,amber,120,CW_ALIGN_CENTER);
  if(G.damage_flash>0.04f)cw_r2d_rect((cw_rect){0,0,W,H},cw_rgba(1,0.1f,0.07f,G.damage_flash*0.17f));
  cw_render3d_stats stats;cw_render3d_get_stats(G.tree,&stats);G.skipped=stats.skipped;
  cw_quality_auto_measure(G.state==PLAY);
}
#include "menus.h"

static void card(const char *head, const char *sub, const char *foot) {
  cw_r2d_rect((cw_rect){ 28, 92, 428, 340 }, cw_hex(0x071522d9));
  cw_r2d_rect((cw_rect){ 28, 92, 5, 340 }, cw_hex(0xffae5dff));
  text(G.title_font, head, 54, 118, 38, cw_hex(0xffb45cff), 375, CW_ALIGN_LEFT);
  text(G.ui_font, sub, 55, 194, 21, cw_hex(0xe7eff2ff), 365, CW_ALIGN_LEFT);
  if (G.state == TITLE)
    text(G.ui_font, "STICK / ARROWS  BANK + PITCH\nA / RT / SPACE  GUNS   X / M  MISSILE\nB / SHIFT  BURN   LB / CTRL  AIRBRAKE\nRB + STICK  ROLL / LOOP   R-STICK  LOOK\nY / I  TARGET   LT / F  FLARES", 55, 238, 17,
         cw_hex(0xffd8aaff), 365, CW_ALIGN_LEFT);
  text(G.ui_font, foot, 55, 361, 16, cw_hex(0x93e6edff), 365, CW_ALIGN_LEFT);
}

static void render(void *user, double alpha) {
  (void)user; (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  if(!art_hide_hud&&G.state!=CRASH&&G.state!=TITLE&&G.state!=CONTROLS&&G.state!=SETTINGS)hud();
  if(capital.active&&capital.age<.16f)cw_r2d_rect((cw_rect){0,0,W,H},cw_rgba(1,.87f,.65f,(1-capital.age/.16f)*.4f));
  if(G.state==CRASH&&G.crash_age<0.10f)cw_r2d_rect((cw_rect){0,0,W,H},cw_rgba(1,0.9f,0.76f,(1-G.crash_age/0.10f)*0.4f));
  if(G.state==TITLE)draw_mission_menu();
  else if(G.state==CONTROLS)draw_controls_menu();
  else if(G.state==PAUSED)draw_action_menu();
  else if(G.state==SETTINGS) {
    cw_color white=cw_hex(0xe3ece7ff),amber=cw_hex(0xffb366ff),muted=cw_hex(0x93b5beff);
    cw_r2d_rect((cw_rect){210,65,540,420},cw_hex(0x071522f2));
    text(G.title_font,"SETTINGS",240,88,30,amber,470,CW_ALIGN_LEFT);
    text(G.ui_font,"FLIGHT / GRAPHICS / AUDIO",241,131,14,muted,470,CW_ALIGN_LEFT);
    const char *labels[]={"PITCH","GRAPHICS","FLIGHT CONTROLS","MUSIC","SOUND EFFECTS"};
    char value[96];
    for(int i=0;i<5;i++) {
      float y=174+i*47;
      if(i==G.settings_row)cw_r2d_rect((cw_rect){230,y-5,500,38},cw_hex(0x29414edb));
      text(G.ui_font,labels[i],243,y,17,i==G.settings_row?amber:white,220,CW_ALIGN_LEFT);
      if(i==0)snprintf(value,sizeof value,"%s",prefs.invert_pitch?"INVERTED":"NORMAL");
      else if(i==1)snprintf(value,sizeof value,"%s",cw_quality_name(cw_quality_get()));
      else if(i==2)snprintf(value,sizeof value,"%s",prefs.expert_controls?"EXPERT":"STANDARD");
      else snprintf(value,sizeof value,"%d%%",i==3?prefs.music_volume:prefs.sfx_volume);
      text(G.ui_font,value,480,y,17,white,225,CW_ALIGN_RIGHT);
      if(i>=3) {
        float volume=(i==3?prefs.music_volume:prefs.sfx_volume)*.01f;
        cw_r2d_rect((cw_rect){480,y+26,225,3},cw_hex(0x43545bff));
        cw_r2d_rect((cw_rect){480,y+26,225*volume,3},amber);
      }
    }
    text(G.ui_font,"UP / DOWN SELECT   LEFT / RIGHT ADJUST",240,426,14,muted,475,CW_ALIGN_LEFT);
    menu_button((cw_rect){240,450,120,30},"BACK",true);text(G.ui_font,"CHANGES SAVE AUTOMATICALLY",390,455,13,muted,320,CW_ALIGN_LEFT);
  }
  else if (G.state == WIN&&G.level==4&&capital.active&&capital.age<8){
    text(G.title_font,"MOTHERSHIP DESTROYED",W*.5f-340,150,32,cw_hex(0xffd0a5ff),680,CW_ALIGN_CENTER);
    const char *labels[]={"NEXT MISSION","REPLAY","MISSION SELECT"};for(int i=0;i<3;i++)menu_button(action_button(i),labels[i],G.menu_row==i);
  }
  else if(G.state==WIN||G.state==LOST)draw_action_menu();
  cw_r2d_end();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc; (void)argv;
  static cw_app_desc base={ .title = "Sky Fighter", .width = W, .height = H,
    .save = &prefs, .save_size = sizeof prefs, .touch_pointer=true,
    .init = init, .fixed_update = fixed_update, .render = render, .debug = true };
  return cw_scene_app("world.cwscene",&base);
}
