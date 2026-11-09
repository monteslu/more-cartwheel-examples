/* Pirates: four captains on one island map. Fixed-tick simulation is shared
 * by the native game and the dual GL/WebGPU cart. pirates_auto plays a match
 * without input for screenshots and cross-host gates. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/core.h"
#include "cartwheel/input.h"
#include "cartwheel/lightmap.h"
#include "cartwheel/splats.h"
#include "cartwheel/math.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/quality.h"
#include "cartwheel/physics_nodes.h"
#include "cartwheel/render2d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/resource.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/ui.h"
#include "cartwheel/water.h"

#include "coast.h"
#define COAST_N LAGOON_N
#define COAST LAGOON
#define COAST_HEIGHT LAGOON_HEIGHT
#define coast_height lagoon_height
#include "lagoon.h"
#undef COAST_N
#undef COAST
#undef COAST_HEIGHT
#undef coast_height
static int active_map;
static cw_body3d seabed_body;
static cw_heightfield3d seabed_field;
static float map_height(float x,float z) {return active_map?lagoon_height(x,z):coast_height(x,z);}
/* The wind blows the same way all match (toward this heading, radians, the ships' yaw convention): from the north
 * east on the Gilded Shoals, from the south west in Palm Lagoon */
static float wind_to(void) { return active_map ? 2.3f : -0.8f; }
static int32_t sw_no_wind;    /* test switch (sw no_wind 1): every heading sails at the same speed */
static int32_t sw_wind_blind; /* test switch (sw wind_blind 1): the AI routes and picks cargo by distance, blind to the wind */
/* Points of sail: a ship running before the wind makes its full speed, one with the wind on its beam a little more
 * (the fastest point), and one beating into it barely seven tenths. (Beating never pays by tacking here: 40 degrees
 * off the wind makes good 0.66 of full speed straight upwind, against 0.7 straight at it.) */
static float wind_factor(float heading) {
  if (sw_no_wind) return 1;
  float c = cw_cos(heading - wind_to());
  return 1.15f + 0.15f * c - 0.3f * c * c;
}
#define coast_height map_height
#include "sound.h"
#include "effects.h"

#define CAPTAINS 4
#define SHIPS 6
#define GOOD_SPAWNS 18
#define GOODS 48
#define SHOTS 48
#define PI 3.14159265358979323846f
#define DT (1.0f / 60.0f)

enum { TITLE, PLAYING, RESULT };
enum { VOYAGE, RACE, TEAMS };
enum { TIMBER, IRON, COIN };
enum { CAPTAIN, MERCHANT, PATROL };

typedef struct {
  float x, z, yaw, speed, hp;
  float prev_x, prev_z, prev_yaw;
  float nav_x, nav_z;
  int role, active, human, team, respawn, cooldown, ai_tick, protection;
  int idle; /* ticks since this captain's person last used a control (an autopilot sails it from IDLE_HELM) */
  int engaged; /* Voyage: this captain banked once or fired first; until then the patrol and the merchant leave it be */
  int held[3], stored[3], bank, hull, sails, guns, damage_done, sinks;
  cw_node root, sail_b, flag, gun_left, gun_right, hull_upgrade;
  t3_mesh *canvas;
} ship;

typedef struct {
  float x, z, home_x, home_z;
  int kind, active, timer, nav_cell;
  cw_node node, appearance[3];
} good;

typedef struct {
  float x, z, vx, vz;
  float aim_x, aim_z; /* the middle ball of a captain's broadside: where its landing ring was when it fired (test) */
  int owner, life, active, aimed;
  cw_node node;
} shot;

static const float MAP_PORT_X[2][4]={{-30,30,-8,27},{-32,8,-19,30}};
#define PORT_X MAP_PORT_X[active_map]
static const float MAP_PORT_Z[2][4]={{-8,-11,14,8},{-6,-16,10,15}};
#define PORT_Z MAP_PORT_Z[active_map]
static const uint32_t PLAYER_COLOR[4] = { 0xe85642ff, 0x4e97e6ff, 0xe4bd47ff, 0x79d28aff };

static struct {
  cw_node world, environment, cargo_root, water, camera, harbor_flags[CAPTAINS];
  ship ships[SHIPS];
  good goods[GOODS];
  shot shots[SHOTS];
  cw_font font;
  cw_rng rng;
  int32_t state, mode, joined, humans, tick, time_left, quota, winner, alert, shots_fired, goods_taken;
  int32_t voyage_stage, patrol_defeats, final_quota, paused;
  int32_t objective_notice_ticks;
  int32_t autopilot;  /* bit per captain: a person's ship that sails on its own while its controller is left alone */
  int32_t idle_stays; /* test switch (sw idle_stays 1): a captain left alone stays where it is, as it once did */
  int32_t voyage_hunt; /* test switch (sw voyage_hunt 1): the patrol hunts a voyage captain from the start, as it once did */
  int32_t labels_stack; /* test switch (sw labels_stack 1): every ship label at its ship, nothing kept clear */
  int32_t flat_shots;   /* test switch (sw flat_shots 1): balls skim the water at 0.7 m as they once did */
  float shot_peak;       /* the highest a ball has been drawn over the water since the match began (m) */
  float shot_aim_error;  /* the farthest a captain's middle ball has splashed from its landing ring (m) */
  int32_t label_overlaps, label_overlaps_max; /* annotation boxes over the sea that overlap: last frame, and the most since the match began */
  int32_t dbg_bank[CAPTAINS], dbg_held[CAPTAINS], dbg_hp[CAPTAINS], dbg_protection[CAPTAINS], dbg_cooldown[CAPTAINS], dbg_pos[CAPTAINS * 2];
  uint32_t hash, ripple_hash;
  char cargo_notice[CAPTAINS][80];
  int32_t cargo_notice_ticks[CAPTAINS];
  int32_t hit_ticks[SHIPS], hit_amount[SHIPS];
  float hit_previous_hp[SHIPS];
  float canvas_weights[SHIPS];
  int32_t force_target; float force_tx, force_tz; /* the wind test: every AI captain sails for this point */
  int32_t ai_good[SHIPS];   /* the cargo each AI captain last chose to sail for (-1: none), for the wind test */
  float hole_cut[SHIPS];    /* render only: the sails' alpha test, how much of the torn canvas is cut away */
  t3_texture *hole_mask;    /* the sails' holes: one mask in the canvas's UV space, cut deeper as the hull falls */
  cw_node wind_streaks;     /* thin lines of wind over the sea (Low and up) */
  float list_side[SHIPS];  /* render only: the side a damaged ship lists to (the side it was last hit on) */
  int32_t smoke_tick[SHIPS]; /* render only: the tick of its last puff of damage smoke */
  float gpu_ms;
  cw_node post;
  int lever_sun_shadows, lever_scenery_detail;
  cw_lightmap_data *lightmap; /* the map's baked light (NULL: none, everything lit in real time) */
  int no_lightmap;            /* test switch: build the maps without their lightmaps (the bake, and the before/after) */
  bool blob_shadows;          /* the ships' blob shadows stand in for the sun's shadow map */
  cw_splats *ship_shadows;
  /* the palms, undergrowth and shore rocks: the Scenery detail setting draws them at a coarser LOD (cwpack's 1/2 and 1/4) */
  t3_object *scenery_lod[512];
  int n_scenery_lod;
  t3_object *big_scenery[1 + CAPTAINS]; /* the coast and the four port buildings: the Island shadows setting */
  cw_action turn_l, turn_r, sail_u, sail_d, aim_l, aim_r, fire_l, fire_r, dock, up_hull, up_sails, up_guns, start, mode_l, mode_r, map_next, pause, leave;
  cw_action text_size, text_background; /* the player's text settings, on the title and the pause box */
} G;

enum { ROCKFALLS = 3, ERUPTION_PERIOD = 1800, ERUPTION_WARNING = 210 };
static struct {
  cw_node rocks[ROCKFALLS], plume;
  float x[ROCKFALLS], z[ROCKFALLS];
  int32_t warning, count, impacts;
} eruption;

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float dist2(float ax, float az, float bx, float bz) { float x = ax - bx, z = az - bz; return x * x + z * z; }
static int held_total(const ship *s) { return s->held[0] + s->held[1] + s->held[2]; }
static cw_color col(uint32_t rgba) { return cw_hex(rgba); }
static cw_node part(cw_node parent, const char *name, const char *shape, float x, float y, float z,
                    float sx, float sy, float sz, uint32_t rgba, bool shadow) {
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_set(n, "primitive", cw_val_enum(shape));
  cw_node_set(n, "size", cw_val_vec3(sx, sy, sz));
  cw_color c = col(rgba);
  cw_node_set(n, "color", cw_val_color(c.r * c.r, c.g * c.g, c.b * c.b, 1));
  cw_node_set(n, "roughness", cw_val_float(shape[0] == 'S' ? 0.92 : 0.7));
  cw_node_set(n, "cast_shadow", cw_val_bool(shadow));
  cw_node_set(n, "receive_shadow", cw_val_bool(true));
  cw_node_add_child(parent, n);
  cw_node3d_set_position(n, (t3_vec3){ x, y, z });
  /* Sphere size is a diameter and cylinder size uses X for both radial
   * axes. Apply the remaining proportions to the node itself. */
  if (!strcmp(shape, "SPHERE"))
    cw_node3d_set_scale(n, (t3_vec3){ 1, sy / sx, sz / sx });
  else if (!strcmp(shape, "CYLINDER"))
    cw_node3d_set_scale(n, (t3_vec3){ 1, 1, sz / sx });
  return n;
}

static float coast_distance(float x, float z);
/* a scenery model the Scenery detail setting picks the LOD of, at the LOD it picks now */
static void scenery_lod_add(t3_object *o);
static void model_shadows(t3_object *o) {
  if (o->type == T3_MESH || o->type == T3_SKINNED_MESH) o->cast_shadow = o->receive_shadow = true;
  for (int i=0;i<o->child_count;i++) model_shadows(o->children[i]);
}
static void no_cast_shadow(t3_object *o) {
  o->cast_shadow=false;
  for(int i=0;i<o->child_count;i++)no_cast_shadow(o->children[i]);
}
static int harbor_ground(float x,float z) {
  for(int i=0;i<CAPTAINS;i++)if(dist2(x,z,PORT_X[i],PORT_Z[i])<25)return 1;
  return 0;
}
/* Conservative support over the complete rendered footprint, including
 * overhangs. Sampling the centre alone misses downhill gaps. */
static void prop_bounds(t3_object *o,t3_vec3 *lo,t3_vec3 *hi) {
  if(o->type==T3_MESH || o->type==T3_SKINNED_MESH) {
    t3_geometry *g=((t3_mesh *)o)->geometry;
    if(g) {
      t3_geometry_compute_bounding_box(g);t3_box3 b=g->bounding_box;
      for(int i=0;i<8;i++) {
        t3_vec3 v=t3_vec3_apply_mat4((t3_vec3){i&1?b.max.x:b.min.x,i&2?b.max.y:b.min.y,i&4?b.max.z:b.min.z},&o->matrix_world);
        lo->x=fminf(lo->x,v.x);lo->y=fminf(lo->y,v.y);lo->z=fminf(lo->z,v.z);
        hi->x=fmaxf(hi->x,v.x);hi->y=fmaxf(hi->y,v.y);hi->z=fmaxf(hi->z,v.z);
      }
    }
  }
  for(int i=0;i<o->child_count;i++)prop_bounds(o->children[i],lo,hi);
}
static float bounds_footprint_gap(cw_node n) {
  t3_object *o=cw_node3d_object(n);
  /* Scene transforms own these objects; refresh before measuring a moved prop. */
  t3_object_set_matrix_world(o,cw_node3d_global_transform(n));
  t3_object_update_world_matrix(o,false,true);
  t3_vec3 lo={1e9f,1e9f,1e9f},hi={-1e9f,-1e9f,-1e9f};prop_bounds(o,&lo,&hi);
  float lowest=1e9f;
  for(int z=0;z<=16;z++)for(int x=0;x<=16;x++)
    lowest=fminf(lowest,coast_height(lo.x+(hi.x-lo.x)*x/16,lo.z+(hi.z-lo.z)*z/16));
  return lo.y-lowest;
}
/* The rock's underside at this X/Z: its lowest surface there, from the
 * downward-facing triangles (a closed solid's lowest crossing of a vertical
 * line is always on one). Compared with the terrain directly beneath. */
static void rock_column_gap(const t3_vec3 *tri,int count,float x,float z,float *gap,int *samples) {
  float lowest=1e9f;
  for(int i=0;i+2<count;i+=3) {
    t3_vec3 a=tri[i],b=tri[i+1],c=tri[i+2];
    float bx=b.x-a.x,bz=b.z-a.z,cx=c.x-a.x,cz=c.z-a.z;
    float det=bx*cz-bz*cx;
    if(fabsf(det)<1e-8f)continue;
    float u=((x-a.x)*cz-(z-a.z)*cx)/det;
    float v=(bx*(z-a.z)-bz*(x-a.x))/det;
    if(u<-.0001f || v<-.0001f || u+v>1.0001f)continue;
    lowest=fminf(lowest,a.y+u*(b.y-a.y)+v*(c.y-a.y));
  }
  if(lowest<1e8f){*gap=fmaxf(*gap,lowest-coast_height(x,z));(*samples)++;}
}
/* Every point of the rock's underside, across its whole footprint, must be in
 * contact with the terrain or under it: a grid over the footprint, every
 * underside vertex and a lattice inside every underside triangle. No height
 * band and no centre radius: a raised downhill edge or an overhang anywhere
 * shows up as a gap. */
static void rock_underside_gap(t3_object *o,float *gap,int *samples) {
  if(o->type==T3_MESH) {
    t3_geometry *g=((t3_mesh *)o)->geometry;
    t3_attribute *p=g?g->attributes[T3_ATTR_POSITION]:NULL;
    if(p && p->type==T3_FLOAT32 && p->item_size==3) {
      const float *v=p->array;
      int count=g->index?g->index->count:p->count,down=0;
      t3_vec3 *tri=malloc(count*sizeof(*tri));
      if(!tri){*gap=1e9f;return;}
      float xlo=1e9f,zlo=1e9f,xhi=-1e9f,zhi=-1e9f;
      for(int i=0;i+2<count;i+=3) {
        t3_vec3 t[3];
        for(int k=0;k<3;k++) {
          int j=i+k;
          if(g->index)j=g->index->type==T3_UINT16?((uint16_t *)g->index->array)[i+k]:((uint32_t *)g->index->array)[i+k];
          t[k]=t3_vec3_apply_mat4((t3_vec3){v[j*3],v[j*3+1],v[j*3+2]},&o->matrix_world);
          xlo=fminf(xlo,t[k].x);xhi=fmaxf(xhi,t[k].x);zlo=fminf(zlo,t[k].z);zhi=fmaxf(zhi,t[k].z);
        }
        /* outward normal's Y below zero: this triangle faces the ground */
        float ny=(t[1].z-t[0].z)*(t[2].x-t[0].x)-(t[1].x-t[0].x)*(t[2].z-t[0].z);
        if(ny<0){tri[down++]=t[0];tri[down++]=t[1];tri[down++]=t[2];}
      }
      for(int z=0;z<=40;z++)for(int x=0;x<=40;x++)
        rock_column_gap(tri,down,xlo+(xhi-xlo)*x/40,zlo+(zhi-zlo)*z/40,gap,samples);
      for(int i=0;i+2<down;i+=3)for(int a=0;a<=4;a++)for(int b=0;a+b<=4;b++) {
        float u=a/4.0f,w=b/4.0f;
        rock_column_gap(tri,down,tri[i].x+u*(tri[i+1].x-tri[i].x)+w*(tri[i+2].x-tri[i].x),
          tri[i].z+u*(tri[i+1].z-tri[i].z)+w*(tri[i+2].z-tri[i].z),gap,samples);
      }
      free(tri);
    }
  }
  for(int i=0;i<o->child_count;i++)rock_underside_gap(o->children[i],gap,samples);
}
static float footprint_gap(cw_node n) {
  if(strncmp(cw_node_name(n),"ShoreRock",9))return bounds_footprint_gap(n);
  t3_object *o=cw_node3d_object(n);
  t3_object_set_matrix_world(o,cw_node3d_global_transform(n));
  t3_object_update_world_matrix(o,false,true);
  float gap=-1e9f;int samples=0;rock_underside_gap(o,&gap,&samples);
  return samples?gap:1e9f;
}
static float rock_peak_clearance(t3_object *o) {
  float peak=-1e9f;
  if(o->type==T3_MESH) {
    t3_geometry *g=((t3_mesh *)o)->geometry;
    t3_attribute *a=g?g->attributes[T3_ATTR_POSITION]:NULL;
    if(a && a->type==T3_FLOAT32 && a->item_size==3) {
      const float *v=a->array;
      for(int i=0;i<a->count;i++) {
        t3_vec3 p=t3_vec3_apply_mat4((t3_vec3){v[i*3],v[i*3+1],v[i*3+2]},&o->matrix_world);
        peak=fmaxf(peak,p.y-coast_height(p.x,p.z));
      }
    }
  }
  for(int i=0;i<o->child_count;i++)peak=fmaxf(peak,rock_peak_clearance(o->children[i]));
  return peak;
}
static void ground_prop(cw_node n) {
  /* Rocks sink a little further: the drawn terrain is triangles between the
   * height samples, up to about 2 cm off the sampled heights on a shore curve. */
  float gap=footprint_gap(n),sink=strncmp(cw_node_name(n),"ShoreRock",9)?.02f:.04f;
  if(gap>-sink){t3_vec3 p=cw_node3d_position(n);p.y-=gap+sink;cw_node3d_set_position(n,p);}
}
static void palm(cw_node parent, float x, float z, float height) {
  if(harbor_ground(x,z)||coast_distance(x,z)<.5f)return;
  cw_node n=cw_node_new("Node3D","Palm");cw_node_add_child(parent,n);
  float h=coast_height(x,z);
  cw_node3d_set_position(n,(t3_vec3){x,h,z});
  float scale=height/9.0f;cw_node3d_set_scale(n,(t3_vec3){scale,scale,scale});
  cw_node3d_set_rotation(n,(t3_vec3){0,x*.8f+z,0});
  t3_object *model=cw_gltf_instantiate(cw_res_load("res://models/coconut-palm.glb",CW_RES_GLTF));
  if (model) {model_shadows(model);t3_object_add(cw_node3d_object(n),model);t3_release(model);ground_prop(n);scenery_lod_add(model);}
}

static float coast_distance(float x, float z) {
  float best = -1e6f;
  for (int k = 0; k < 3; k++) {
    int inside = 0; float d2 = 1e9f;
    int count=active_map?LAGOON_N[k]:COAST_N[k];
    for (int i = 0; i < count; i++) {
      const float (*polygon)[2]=active_map?LAGOON[k]:COAST[k];
      const float *a=polygon[i],*b=polygon[(i+1)%count];
      float dx = b[0] - a[0], dz = b[1] - a[1];
      float t = clampf(((x-a[0])*dx+(z-a[1])*dz)/(dx*dx+dz*dz),0,1);
      float d = dist2(x,z,a[0]+t*dx,a[1]+t*dz); if (d < d2) d2 = d;
      if ((a[1]>z)!=(b[1]>z) && x < dx*(z-a[1])/dz+a[0]) inside = !inside;
    }
    float d = sqrtf(d2) * (inside ? 1 : -1); if (d > best) best = d;
  }
  return best;
}

/* Harbor models share a quay and a pier extending 4.96 m seaward.
 * Use the same transform for rendering, navigation and hull collision. */
static float port_angles[CAPTAINS];
static float port_yaw(int i) { return port_angles[i]; }
static void prepare_ports(void) {
  for(int i=0;i<CAPTAINS;i++) {
    float x=PORT_X[i],z=PORT_Z[i];
    port_angles[i]=cw_atan2(coast_distance(x-.2f,z)-coast_distance(x+.2f,z),
                           coast_distance(x,z-.2f)-coast_distance(x,z+.2f));
  }
}
static float harbor_distance2(int i,float x,float z) {
  float a=port_yaw(i);
  return dist2(x,z,PORT_X[i]+cw_sin(a)*4,PORT_Z[i]+cw_cos(a)*4);
}
static int docks_clear(float x,float z,float radius) {
  for(int i=0;i<CAPTAINS;i++) {
    if(dist2(x,z,PORT_X[i],PORT_Z[i])>144)continue;
    float a=port_yaw(i),dx=x-PORT_X[i],dz=z-PORT_Z[i];
    float u=dx*cw_cos(a)-dz*cw_sin(a),v=dx*cw_sin(a)+dz*cw_cos(a);
    /* Quay, including shore buildings; then pier, piles and mooring caps. */
    float qx=fmaxf(fabsf(u)-3.3f,0),qz=fmaxf(fabsf(v+1.5f)-2.2f,0);
    float px=fmaxf(fabsf(u)-1.2f,0),pz=fmaxf(fabsf(v-2.8f)-2.2f,0);
    if(qx*qx+qz*qz<radius*radius||px*px+pz*pz<radius*radius)return 0;
  }
  return 1;
}

/* Five overlapping waterline discs follow the asymmetric imported hull:
 * stern -3.6 m, bow +2.4 m, beam about 2.2 m. Sails and bowsprit overhang
 * are deliberately excluded. Broad-phase avoids five polygon walks at sea. */
static int hull_clear(float x,float z,float yaw) {
  if(coast_distance(x,z)<-3.8f && docks_clear(x,z,3.8f))return 1;
  float sy=cw_sin(yaw),cy=cw_cos(yaw);
  for(int j=0;j<5;j++) {
    float along=-2.5f+j*.95f;
    float px=x+sy*along,pz=z+cy*along;
    if(coast_distance(px,pz)>-1.15f || !docks_clear(px,pz,1.15f))return 0;
  }
  return 1;
}

#define NAV_W 87
#define NAV_X_ORIGIN 43
#define NAV_H 45
#define NAV_N (NAV_W * NAV_H)
static unsigned char nav_open[NAV_N];
static int nav_region[NAV_N];
static void navigation_init(void) {
  for(int z=0;z<NAV_H;z++) for(int x=0;x<NAV_W;x++)
    nav_open[z*NAV_W+x]=(x>0 && x<NAV_W-1 && z>0 && z<NAV_H-1 && coast_distance(x-NAV_X_ORIGIN,z-22)<-3.8f && docks_clear(x-NAV_X_ORIGIN,z-22,3.8f));
  /* Match the pathfinder's diagonal clearance exactly. A nearby pickup
   * across a closed headland must not trap a bot in a futile route. */
  static int queue[NAV_N];
  memset(nav_region,0,sizeof nav_region);int region=0;
  for(int seed=0;seed<NAV_N;seed++)if(nav_open[seed]&&!nav_region[seed]) {
    int head=0,tail=0;queue[tail++]=seed;nav_region[seed]=++region;
    while(head<tail) {
      int cur=queue[head++],x=cur%NAV_W,z=cur/NAV_W;
      for(int dz=-1;dz<=1;dz++)for(int dx=-1;dx<=1;dx++) {
        if(!dx&&!dz)continue;
        int xx=x+dx,zz=z+dz;if(xx<0||xx>=NAV_W||zz<0||zz>=NAV_H)continue;
        int n=zz*NAV_W+xx;
        if(!nav_open[n]||nav_region[n])continue;
        if(dx&&dz&&(!nav_open[z*NAV_W+xx]||!nav_open[zz*NAV_W+x]))continue;
        nav_region[n]=region;queue[tail++]=n;
      }
    }
  }
}
static int nav_nearest(float x,float z) {
  float best=1e9f;int node=0;
  for(int i=0;i<NAV_N;i++) if(nav_open[i]) {
    float d=dist2(x,z,(i%NAV_W)-NAV_X_ORIGIN,(i/NAV_W)-22);
    if(d<best){best=d;node=i;}
  }
  return node;
}
/* The route is the quickest by sailing time, not the shortest: each step of the grid costs its length over the speed
 * the wind gives that heading (wind_factor), so a captain goes round an island on the side where the wind is on its
 * beam rather than the side where it must beat (Dijkstra from the goal back to the ship). sw wind_blind 1: every
 * step costs its length, the old breadth-first route. */
static void nav_waypoint(ship *s,float tx,float tz) {
  static int next[NAV_N],heap[NAV_N * 8];
  static float cost[NAV_N];
  static unsigned char done[NAV_N];
  float step[3][3];
  for(int dz=-1;dz<=1;dz++)for(int dx=-1;dx<=1;dx++) {
    float len=(dx&&dz)?1.41421356f:1.0f;
    /* the ship sails from the cell (x+dx, z+dz) to (x, z): its heading is the step reversed */
    step[dz+1][dx+1]=(sw_wind_blind==1||sw_wind_blind==2)?len:len/wind_factor(cw_atan2((float)-dx,(float)-dz));
  }
  int start=nav_nearest(s->x,s->z),goal=nav_nearest(tx,tz);
  for(int i=0;i<NAV_N;i++){next[i]=-1;cost[i]=1e30f;done[i]=0;}
  int heap_n=0;
  #define HEAP_LESS(a,b) (cost[a]<cost[b]||(cost[a]==cost[b]&&(a)<(b)))
  #define HEAP_PUSH(v) do{int k_=heap_n++;heap[k_]=(v);while(k_){int p_=(k_-1)/2;if(!HEAP_LESS(heap[k_],heap[p_]))break;int t_=heap[k_];heap[k_]=heap[p_];heap[p_]=t_;k_=p_;}}while(0)
  cost[goal]=0;next[goal]=goal;HEAP_PUSH(goal);
  while(heap_n && !done[start]) {
    int cur=heap[0];heap[0]=heap[--heap_n];
    for(int k=0;;) {int l=2*k+1,r=l+1,m=k;if(l<heap_n&&HEAP_LESS(heap[l],heap[m]))m=l;if(r<heap_n&&HEAP_LESS(heap[r],heap[m]))m=r;if(m==k)break;int t=heap[k];heap[k]=heap[m];heap[m]=t;k=m;}
    if(done[cur])continue;
    done[cur]=1;
    int x=cur%NAV_W,z=cur/NAV_W;
    for(int dz=-1;dz<=1;dz++)for(int dx=-1;dx<=1;dx++) {
      if(!dx&&!dz)continue;
      int xx=x+dx,zz=z+dz;if(xx<0||xx>=NAV_W||zz<0||zz>=NAV_H)continue;
      int n=zz*NAV_W+xx;
      if(!nav_open[n]||done[n])continue;
      if(dx&&dz&&(!nav_open[z*NAV_W+xx]||!nav_open[zz*NAV_W+x]))continue;
      float c=cost[cur]+step[dz+1][dx+1];
      if(c<cost[n]&&heap_n<NAV_N*8){cost[n]=c;next[n]=cur;HEAP_PUSH(n);}
    }
  }
  #undef HEAP_PUSH
  #undef HEAP_LESS
  int n=start;
  for(int step=0;step<3 && n!=goal && next[n]>=0;step++)n=next[n];
  s->nav_x=(n%NAV_W)-NAV_X_ORIGIN;s->nav_z=(n/NAV_W)-22;
}

/* Lean a shore rock with the beach: its up axis follows the terrain's slope
 * over its footprint, up to 15 degrees (the flared foot and the stone's
 * inward-leaning sides keep its outline over the foot up to about 20 degrees). */
static void rock_tilt(cw_node n,float x,float z,float yaw) {
  const float r=.8f;
  float gx=(coast_height(x+r,z)-coast_height(x-r,z))/(2*r),gz=(coast_height(x,z+r)-coast_height(x,z-r))/(2*r);
  t3_vec3 up={-gx,1,-gz};float len=sqrtf(up.x*up.x+1+up.z*up.z);up.x/=len;up.y/=len;up.z/=len;
  float angle=fminf(acosf(fminf(up.y,1)),15*(float)M_PI/180),axis_len=sqrtf(up.z*up.z+up.x*up.x);
  t3_quat spin={0,sinf(yaw/2),0,cosf(yaw/2)},lean={0,0,0,1};
  /* the axis of up x n is (n.z, 0, -n.x) */
  if(axis_len>1e-5f)lean=(t3_quat){up.z/axis_len*sinf(angle/2),0,-up.x/axis_len*sinf(angle/2),cosf(angle/2)};
  cw_node3d_set_quaternion(n,t3_quat_multiply(lean,spin));
}
static t3_mesh *first_mesh(t3_object *o) {
  if (o->type == T3_MESH) return (t3_mesh *)o;
  for (int i = 0; i < o->child_count; i++) { t3_mesh *m = first_mesh(o->children[i]); if (m) return m; }
  return NULL;
}
/* Baked lighting (cartwheel/lightmap.h). The camera never moves and the islands and harbors never move, so their
 * light is baked: the sun's soft shadows (the palms', the huts', the hills'), the sky shut out of the coves, the light
 * the sand bounces. Each map's island and harbor buildings are one mesh each, drawn from its lightmap alone (shading
 * BAKED: a few texture reads a pixel, and nothing of them goes into the sun's shadow map). A model of one mesh
 * becomes a STATIC MeshInstance3D named `name` under `parent` (the bake's geometry; the node path is how the bake
 * finds it again), in the place the model put it; *out is its mesh (NULL when the model has none). */
static void static_mesh(cw_node parent, const char *name, t3_object *model, t3_object **out) {
  t3_mesh *m = model ? first_mesh(model) : NULL;
  *out = m ? &m->base : NULL;
  if (!m) return;
  t3_object_update_world_matrix(model, false, true); /* the mesh's place within the model (the model at the origin) */
  t3_vec3 p, s; t3_quat q;
  t3_mat4_decompose(&m->base.matrix_world, &p, &q, &s);
  t3_retain(m);
  t3_object_remove(m->base.parent, m);
  m->base.position = p; m->base.quaternion = q; m->base.scale = s;
  m->base.cast_shadow = false; m->base.receive_shadow = true;
  cw_node n = cw_node_new("MeshInstance3D", name);
  cw_node_add_child(parent, n);
  cw_node_set(n, "gi_mode", cw_val_enum("STATIC"));
  cw_mesh3d_set_procedural(n, m);
  t3_release(m);
}
static void make_coast(void) {
  G.n_scenery_lod = 0; /* the environment is built again: its models are new */
  cw_node n = cw_node_new("Node3D", "Coast"); cw_node_add_child(G.environment,n);
  t3_object *model = cw_gltf_instantiate(cw_res_load(active_map?"res://models/lagoon.glb":"res://models/coast.glb", CW_RES_GLTF));
  if (model) { static_mesh(n, "Ground", model, &G.big_scenery[0]); t3_release(model); }
  /* A continuous collision heightfield matches the authored terrain. Boxes
   * introduced two-metre stair steps into the water's shoreline depth map. */
  enum { NX=145, NZ=89 };
  uint16_t heights[NX*NZ];
  for(int z=0;z<NZ;z++)for(int x=0;x<NX;x++)
    heights[z*NX+x]=(uint16_t)clampf((coast_height(x*.5f-36,z*.5f-22)+16)*2048,0,65535);
  seabed_field=cw_heightfield3d_create(heights,NX,NZ,(t3_vec3){.5f,1.0f/2048,.5f},NULL);
  cw_body3d_def body=cw_body3d_default_def();body.type=CW_BODY_STATIC;
  body.position=(t3_vec3){-36,-16,-22};
  seabed_body=cw_body3d_create(cw_physics3d_world(cw_scene_app_tree()),&body);
  cw_shape3d_heightfield(seabed_body,NULL,seabed_field);
  static const float trees[][2]={{-27,-9},{-25,-13},{-22,-15},{-14,-11},{-22,-2},{-18,1},{-7,9},{-4,8},{3,10},{5,13},{8,12},{6,-5},{9,-6},{24,3},{27,4}};
  for (int i=0;i<(int)(sizeof trees/sizeof trees[0]);i++) palm(G.environment,trees[i][0],trees[i][1],2.7f+(i%3)*.3f);
  for (int iz=-17;iz<=16;iz+=2) for (int ix=-29;ix<=30;ix+=2) {
    float x=ix+.55f*cw_sin(ix*7.1f+iz), z=iz+.55f*cw_cos(iz*5.3f-ix);
    float d=coast_distance(x,z);
    if (d<.45f || (!active_map && x>18 && z<-5) || harbor_ground(x,z)) continue;
    float slope=fabsf(coast_height(x+.5f,z)-coast_height(x-.5f,z))+fabsf(coast_height(x,z+.5f)-coast_height(x,z-.5f));
    if (slope>1.2f) continue;
    int seed=(ix+40)*31+(iz+30)*17;
    if (d>1.3f && seed%3==0) { palm(G.environment,x,z,3.3f+(seed%5)*.2f); continue; }
    /* Leave irregular clearings so the bays and terrain remain legible.
     * Low broadleaf plants form an understory rather than a solid canopy. */
    if(d>=1.0f && cw_sin(x*.7f+z*.38f)+cw_cos(z*.8f-x*.24f)>.65f)continue;
    static const char *rocks[]={"res://models/shore-rock-0.glb","res://models/shore-rock-1.glb","res://models/shore-rock-2.glb"};
    const char *path=d<1.0f?rocks[seed%3]:"res://models/undergrowth.glb";
    cw_node detail=cw_node_new("Node3D",d<1?"ShoreRock":"Undergrowth");cw_node_add_child(G.environment,detail);
    float h=coast_height(x,z);
    float scale=d<1?.46f+(seed%5)*.05f:1.1f+(seed%4)*.15f;
    cw_node3d_set_position(detail,(t3_vec3){x,h+(d<1?.35f:-.04f),z});
    cw_node3d_set_scale(detail,(t3_vec3){scale,scale,scale});
    cw_node3d_set_rotation(detail,(t3_vec3){0,seed*.7f,0});
    if(d<1)rock_tilt(detail,x,z,seed*.7f);
    t3_object *o=cw_gltf_instantiate(cw_res_load(path,CW_RES_GLTF));
    if(o){model_shadows(o);t3_object_add(cw_node3d_object(detail),o);t3_release(o);ground_prop(detail);scenery_lod_add(o);}
    /* With the foot just under the sand, its rim (0.55 model units higher)
     * would show as a skirt on the downhill side. Sink the stone by up to
     * that depth, so the sand meets it around its widest outline, while at
     * least 0.3 model units of it stay above the sand. */
    if(o && d<1) {
      t3_object *r=cw_node3d_object(detail);
      t3_object_set_matrix_world(r,cw_node3d_global_transform(detail));t3_object_update_world_matrix(r,false,true);
      float sink=fminf(.55f*scale,fmaxf(0,rock_peak_clearance(r)-.30f*scale));
      t3_vec3 p=cw_node3d_position(detail);p.y-=sink;cw_node3d_set_position(detail,p);
    }
  }
}

static cw_node make_standard(cw_node parent,int captain,float x,float y,float z) {
  static const char *paths[]={"res://models/flag-0.glb","res://models/flag-1.glb","res://models/flag-2.glb","res://models/flag-3.glb","res://models/flag-4.glb","res://models/flag-5.glb"};
  cw_node n=cw_node_new("Node3D","ClothStandard");cw_node_add_child(parent,n);
  cw_node3d_set_position(n,(t3_vec3){x,y,z});
  t3_object *o=cw_gltf_instantiate(cw_res_load(paths[captain],CW_RES_GLTF));
  if(o){model_shadows(o);t3_object_add(cw_node3d_object(n),o);t3_release(o);}
  return n;
}

static void make_port(int i) {
  static const char *names[]={"Port0","Port1","Port2","Port3"}; /* one name each: the bake finds its mesh by path */
  cw_node n = cw_node_new("Node3D", names[i]);
  cw_node_add_child(G.environment, n);
  cw_node3d_set_position(n, (t3_vec3){ PORT_X[i], 0, PORT_Z[i] });
  cw_node3d_set_rotation(n,(t3_vec3){0,port_yaw(i),0});
  static const char *ports[]={"res://models/port-fort.glb","res://models/port-watchtower.glb","res://models/port-fishery.glb","res://models/port-shipyard.glb"};
  t3_object *model=cw_gltf_instantiate(cw_res_load(ports[i],CW_RES_GLTF));
  if(model){static_mesh(n,"Buildings",model,&G.big_scenery[1+i]);t3_release(model);}
  part(n,"Flagpole","CYLINDER",1.8f,2,0,.09f,4,.09f,0x775434ff,false);
  G.harbor_flags[i]=make_standard(n,i,1.8f,3.95f,0);

}

static cw_node ship_fitting(cw_node parent,const char *name,const char *path,float x,float y,float z) {
  cw_node n=cw_node_new("Node3D",name);cw_node_add_child(parent,n);
  cw_node3d_set_position(n,(t3_vec3){x,y,z});
  t3_object *o=cw_gltf_instantiate(cw_res_load(path,CW_RES_GLTF));
  if(o){model_shadows(o);t3_object_add(cw_node3d_object(n),o);t3_release(o);}
  cw_node_set(n,"visible",cw_val_bool(false));return n;
}

static t3_mesh *find_canvas(t3_object *o) {
  if(o->type==T3_MESH && ((t3_mesh *)o)->morph_influence_count>0)return (t3_mesh *)o;
  for(int i=0;i<o->child_count;i++) {t3_mesh *m=find_canvas(o->children[i]);if(m)return m;}
  return NULL;
}

/* The sails' holes, one RGBA mask in the canvas's own UV space (the galleon's atlas), made once from the canvas mesh:
 * twelve holes in three groups of four, each centred on a point of the canvas picked by area. A texel's value is
 * 0.10 + 0.26 * group + 0.26 * (its distance on the sail from the hole's centre / the hole's radius) inside a hole
 * and 1 elsewhere, so a material alpha test of t cuts the first group's holes open as t passes 0.10, widens them to
 * full size at 0.36, and so on: the sails tear further as the hull falls (sync_visuals sets t from the damage). The
 * values grow smoothly across a hole, so filtering keeps the cut edge clean at any distance. */
enum { HOLE_TEX = 512, HOLES = 12 };
static t3_texture *build_hole_mask(t3_mesh *m) {
  t3_geometry *g = m->geometry;
  t3_attribute *pos = g ? g->attributes[T3_ATTR_POSITION] : NULL, *uv = g ? g->attributes[T3_ATTR_UV] : NULL;
  if (!pos || !uv || pos->type != T3_FLOAT32 || uv->type != T3_FLOAT32 || pos->item_size < 3 || uv->item_size < 2) {
    cw_warn("pirates: the sail canvas has no float positions and UVs; its sails will not tear");
    return NULL;
  }
  const float *P = pos->array, *U = uv->array;
  int tris = g->index ? g->index->count / 3 : pos->count / 3;
  #define TRI_VERT(t, k) (g->index ? (g->index->type == T3_UINT16 ? ((const uint16_t *)g->index->array)[(t) * 3 + (k)] \
                                                               : (int)((const uint32_t *)g->index->array)[(t) * 3 + (k)]) : (t) * 3 + (k))
  float total = 0;
  for (int t = 0; t < tris; t++) {
    const float *a = P + TRI_VERT(t, 0) * pos->item_size, *b = P + TRI_VERT(t, 1) * pos->item_size, *c = P + TRI_VERT(t, 2) * pos->item_size;
    float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    total += 0.5f * sqrtf(nx * nx + ny * ny + nz * nz);
  }
  float hx[HOLES], hy[HOLES], hz[HOLES], hr[HOLES];
  uint32_t seed = 0x5a11c0deu;
  for (int h = 0; h < HOLES; h++) {
    seed = seed * 1664525u + 1013904223u; float pick = (seed >> 8) / 16777216.0f * total;
    seed = seed * 1664525u + 1013904223u; float r1 = (seed >> 8) / 16777216.0f;
    seed = seed * 1664525u + 1013904223u; float r2 = (seed >> 8) / 16777216.0f;
    seed = seed * 1664525u + 1013904223u; hr[h] = 0.32f + 0.30f * ((seed >> 8) / 16777216.0f) + 0.06f * (h / 4);
    float acc = 0;
    for (int t = 0; t < tris; t++) {
      const float *a = P + TRI_VERT(t, 0) * pos->item_size, *b = P + TRI_VERT(t, 1) * pos->item_size, *c = P + TRI_VERT(t, 2) * pos->item_size;
      float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
      float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
      acc += 0.5f * sqrtf(nx * nx + ny * ny + nz * nz);
      if (acc >= pick || t == tris - 1) {
        if (r1 + r2 > 1) { r1 = 1 - r1; r2 = 1 - r2; }
        hx[h] = a[0] + ux * r1 + vx * r2; hy[h] = a[1] + uy * r1 + vy * r2; hz[h] = a[2] + uz * r1 + vz * r2;
        break;
      }
    }
  }
  uint8_t *px = malloc(HOLE_TEX * HOLE_TEX * 4);
  if (!px) return NULL;
  memset(px, 255, HOLE_TEX * HOLE_TEX * 4);
  for (int t = 0; t < tris; t++) {
    int vi[3] = { TRI_VERT(t, 0), TRI_VERT(t, 1), TRI_VERT(t, 2) };
    float sx[3], sy[3];
    for (int k = 0; k < 3; k++) { sx[k] = U[vi[k] * uv->item_size] * HOLE_TEX; sy[k] = U[vi[k] * uv->item_size + 1] * HOLE_TEX; }
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (fabsf(area) < 1e-6f) continue;
    int x0 = (int)floorf(fminf(sx[0], fminf(sx[1], sx[2]))) - 1, x1 = (int)ceilf(fmaxf(sx[0], fmaxf(sx[1], sx[2]))) + 1;
    int y0 = (int)floorf(fminf(sy[0], fminf(sy[1], sy[2]))) - 1, y1 = (int)ceilf(fmaxf(sy[0], fmaxf(sy[1], sy[2]))) + 1;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
      if (x < 0 || y < 0 || x >= HOLE_TEX || y >= HOLE_TEX) continue;
      float cx = x + 0.5f, cy = y + 0.5f;
      float w1 = ((sx[2] - sx[0]) * (cy - sy[0]) - (sy[2] - sy[0]) * (cx - sx[0])) / -area;
      float w2 = ((sx[1] - sx[0]) * (cy - sy[0]) - (sy[1] - sy[0]) * (cx - sx[0])) / area;
      float w0 = 1 - w1 - w2;
      /* a texel's width outside the triangle still counts (filtering reads one texel past its edge) */
      float slack = 1.5f / sqrtf(fabsf(area));
      if (w0 < -slack || w1 < -slack || w2 < -slack) continue;
      const float *a = P + vi[0] * pos->item_size, *b = P + vi[1] * pos->item_size, *c = P + vi[2] * pos->item_size;
      float X = a[0] * w0 + b[0] * w1 + c[0] * w2, Y = a[1] * w0 + b[1] * w1 + c[1] * w2, Z = a[2] * w0 + b[2] * w1 + c[2] * w2;
      float v = 1;
      for (int h = 0; h < HOLES; h++) {
        float d = sqrtf((X - hx[h]) * (X - hx[h]) + (Y - hy[h]) * (Y - hy[h]) + (Z - hz[h]) * (Z - hz[h]));
        if (d < hr[h]) v = fminf(v, 0.10f + 0.26f * (h / 4) + 0.26f * d / hr[h]);
      }
      uint8_t q = (uint8_t)(v * 255 + 0.5f), *o = px + (y * HOLE_TEX + x) * 4;
      if (q < o[0]) o[0] = o[1] = o[2] = o[3] = q;
    }
  }
  #undef TRI_VERT
  t3_texture *tex = t3_texture_new(HOLE_TEX, HOLE_TEX, px);
  free(px);
  if (tex) tex->flip_y = false; /* the glTF atlas's convention: row 0 at v = 0 */
  return tex;
}

static cw_node make_ship_model(int i) {
  ship *s = &G.ships[i];
  cw_node n = cw_node_new("Node3D", i < CAPTAINS ? "Captain" : i == 4 ? "Merchant" : "Patrol");
  cw_node_add_child(G.world, n);
  t3_object *model = cw_gltf_instantiate(cw_res_load("res://models/galleon.glb", CW_RES_GLTF));
  if (model) {
    model->quaternion = t3_quat_from_euler((t3_euler){0,PI,0,T3_YXZ});
    model_shadows(model);
    s->canvas=find_canvas(model);
    /* each ship's canvas its own material (the sails tear by its own hull), cutting the shared hole mask */
    if (s->canvas && s->canvas->material_count) {
      /* dev: PIRATES_NO_SAIL_HOLES=1 keeps the shared canvas (the cost of the holes, timed against it) */
      if (!G.hole_mask && !getenv("PIRATES_NO_SAIL_HOLES")) G.hole_mask = build_hole_mask(s->canvas);
      if (G.hole_mask) {
        t3_material *own = t3_material_clone(s->canvas->materials[0]);
        t3_material_set_texture(own, T3_ALPHA_MAP, G.hole_mask);
        own->alpha_test = 0.04f;
        t3_release(s->canvas->materials[0]);
        s->canvas->materials[0] = own;
      }
    }
    t3_object_add(cw_node3d_object(n), model);
    t3_release(model);
  }
  s->sail_b = ship_fitting(n,"ExtraStaysail","res://models/upgrade-sail.glb",0,1.4f,1.0f);
  cw_node_set(s->sail_b, "visible", cw_val_bool(false));
  s->flag = make_standard(n,i,0,5.5f,-.75f);
  s->hull_upgrade = ship_fitting(n,"HullWales","res://models/upgrade-hull.glb",0,.15f,0);
  cw_node_set(s->hull_upgrade, "visible", cw_val_bool(false));
  s->gun_left = ship_fitting(n,"PortGuns","res://models/upgrade-cannons.glb",-1.05f,.55f,0);
  cw_node3d_set_rotation(s->gun_left,(t3_vec3){0,PI,0});
  s->gun_right = ship_fitting(n,"StarboardGuns","res://models/upgrade-cannons.glb",1.05f,.55f,0);
  cw_node_set(s->gun_left, "visible", cw_val_bool(false));
  cw_node_set(s->gun_right, "visible", cw_val_bool(false));
  return n;
}

static void position_good(int i) {
  static const float P[GOOD_SPAWNS][2] = {
    { -14, 1 }, { 13, -5 }, { 5, 13 }, { -11, -16 }, { 32, 1 }, { -33, -2 },
    { 7, -15 }, { -3, 18 }, { 18, -17 }, { -18, 17 }, { 26, 3 }, { -28, -4 },
    { 3, -13 }, { -8, 11 }, { 15, 18 }, { -15, -7 }, { 30, -7 }, { -30, 7 },
  };
  good *g = &G.goods[i];
  int cell=nav_nearest(P[i%GOOD_SPAWNS][0],P[i%GOOD_SPAWNS][1]);
  g->nav_cell=cell;
  g->x = cell%NAV_W-NAV_X_ORIGIN; g->z = cell/NAV_W-22; g->kind = i % 3; g->active = i<GOOD_SPAWNS;
  g->home_x=g->x;g->home_z=g->z;g->timer=-1;
}

static void make_good(int i) {
  position_good(i);
  good *g=&G.goods[i];
  cw_node n = cw_node_new("Node3D", "Cargo");
  cw_node_add_child(G.cargo_root, n);
  static const char *models[]={"res://models/cargo-timber.glb","res://models/cargo-iron.glb","res://models/cargo-treasure.glb"};
  for(int k=0;k<3;k++) {
    g->appearance[k]=cw_node_new("Node3D","CargoAppearance");cw_node_add_child(n,g->appearance[k]);
    t3_object *model=cw_gltf_instantiate(cw_res_load(models[k],CW_RES_GLTF));
    if(model){model_shadows(model);no_cast_shadow(model);t3_object_add(cw_node3d_object(g->appearance[k]),model);t3_release(model);}
    cw_node_set(g->appearance[k],"visible",cw_val_bool(k==g->kind));
  }
  cw_node3d_set_scale(n,(t3_vec3){1.25f,1.25f,1.25f});
  g->node = n;

}

/* smooth value noise in 0..1 over the water's 1 m grid: a hashed lattice 15 m apart and a finer one 6 m apart,
 * interpolated with smoothstep (no period inside the 144 m rectangle) */
static float lattice(int x, int z, uint32_t seed) {
  uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)z * 0xd8163841u ^ seed;
  h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
  return (h & 0xffff) / 65535.0f;
}
static float value_noise(float x, float z, float cell, uint32_t seed) {
  x /= cell; z /= cell;
  int ix = (int)floorf(x), iz = (int)floorf(z);
  float fx = x - ix, fz = z - iz;
  fx = fx * fx * (3 - 2 * fx); fz = fz * fz * (3 - 2 * fz);
  float a = lattice(ix, iz, seed), b = lattice(ix + 1, iz, seed), c = lattice(ix, iz + 1, seed), d = lattice(ix + 1, iz + 1, seed);
  return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fz;
}
static float sea_patches(int x, int z) {
  float n = .7f * value_noise((float)x, (float)z, 15, 0x51a1u) + .3f * value_noise((float)x, (float)z, 6, 0x9e37u);
  n = clampf((n - .3f) / .45f, 0, 1);
  return n * n * (3 - 2 * n);
}

static void apply_graphics(void);
static void build_map_scene(void);
static void build_map(void) { build_map_scene(); apply_graphics(); }
static void build_map_scene(void) {
  if(cw_body3d_valid(seabed_body))cw_body3d_destroy(seabed_body);
  cw_heightfield3d_destroy(seabed_field);
  if(cw_node_valid(G.environment))cw_node_free(G.environment);
  G.environment=cw_node_new("Node3D","Environment");cw_node_add_child(G.world,G.environment);
  prepare_ports();make_coast();navigation_init();
  if(!active_map) {
    cw_node lava=cw_node_new("Node3D","CraterLava");cw_node_add_child(G.environment,lava);
    t3_object *model=cw_gltf_instantiate(cw_res_load("res://models/crater-lava.glb",CW_RES_GLTF));
    if(model){t3_object_add(cw_node3d_object(lava),model);t3_release(model);}
  }
  eruption.warning=0;
  eruption.plume=volcano_plume(G.environment);
  cw_node_set(eruption.plume,"gravity",cw_val_vec3(.25f*cw_sin(wind_to()),.12f,.25f*cw_cos(wind_to()))); /* the ash drifts downwind */
  cw_node_set(eruption.plume,"visible",cw_val_bool(!active_map));
  cw_node_set(eruption.plume,"emitting",cw_val_bool(!active_map));
  G.wind_streaks=wind_streaks_new(G.environment);
  for(int i=0;i<ROCKFALLS;i++) {
    cw_node n=cw_node_new("Node3D","VolcanicRock");cw_node_add_child(G.environment,n);
    t3_object *model=cw_gltf_instantiate(cw_res_load("res://models/rock0.glb",CW_RES_GLTF));
    if(model){model_shadows(model);t3_object_add(cw_node3d_object(n),model);t3_release(model);}
    cw_node3d_set_scale(n,(t3_vec3){.38f,.38f,.38f});
    cw_node_set(n,"visible",cw_val_bool(false));eruption.rocks[i]=n;
  }
  for(int i=0;i<CAPTAINS;i++)make_port(i);
  /* the map's baked light (tools: the pirates_bake command writes it); without it the island and harbors draw lit in
   * real time as before, casting into the sun's shadow map */
  cw_lightmap_free(G.lightmap);G.lightmap=NULL;
  if(!G.no_lightmap)G.lightmap=cw_lightmap_load(active_map?"lightmaps/lagoon.cwlm":"lightmaps/shoals.cwlm");
  if(G.lightmap)cw_lightmap_apply_shaded(cw_scene_app_tree(),G.world,G.lightmap,1,CW_LIGHTMAP_BAKED);
  /* The water rectangle is 144 x 108 metres. Ease the ocean swells down
   * over the last eight metres of shallow water; CPU buoyancy shares it. */
  float *shelter=malloc(145*109*sizeof(float));
  if(shelter) {
    for(int z=0;z<109;z++)for(int x=0;x<145;x++) {
      float t=clampf(-coast_distance(x-72,z-54)/8.0f,0,1);
      /* Patches of calmer and rougher sea, about 15 m across: the chop's crests (and the foam on them) gather in
       * drifts instead of lying in the waves' regular diagonal bands. */
      shelter[z*145+x]=t*t*(3-2*t)*(.55f+.45f*sea_patches(x,z));
    }
    cw_water_set_wave_mask(G.water,shelter,145,109);free(shelter);
  }
  cw_water_rebake_floor(G.water);
}

static void setup_world(void) {
  G.world = cw_node_get(cw_tree_root(cw_scene_app_tree()), "Root");
  G.water = cw_node_get(G.world, "Water");
  G.camera = cw_node_get(G.world, "Camera");
  cw_node3d_look_at(G.camera, (t3_vec3){ 0, 0, 2 }, (t3_vec3){ 0, 1, 0 });
  effects_init(G.world);
  build_map();
  for (int i = 0; i < SHIPS; i++) G.ships[i].root = make_ship_model(i);
  for (int i = 0; i < SHIPS; i++) cw_water_refraction_hide(G.water,G.ships[i].root);
  G.cargo_root=cw_node_new("Node3D","CargoGroup");cw_node_add_child(G.world,G.cargo_root);
  cw_water_refraction_hide(G.water,G.cargo_root);
  G.ship_shadows=cw_splats_new(G.world,"ShipShadows",CW_SPLATS_SHADOW,SHIPS);
  for (int i = 0; i < GOODS; i++) make_good(i);
  for (int i = 0; i < SHOTS; i++) {
    shot *b = &G.shots[i];
    b->node = cw_node_new("Node3D", "Cannonball");
    cw_node_add_child(G.world, b->node);
    part(b->node, "Ball", "SPHERE", 0, 0, 0, 0.32f, 0.32f, 0.32f, 0x323538ff, false);
    cw_node_set(b->node, "visible", cw_val_bool(false));
  }
  cw_water_rebake_floor(G.water);
}

static void place_ship(int i) {
  ship *s = &G.ships[i];
  s->x = i < 4 ? PORT_X[i] : i == 4 ? 0 : 10;
  s->z = i < 4 ? PORT_Z[i] : i == 4 ? -18 : 17;
  if (i<CAPTAINS) {
    float gx=coast_distance(s->x+.2f,s->z)-coast_distance(s->x-.2f,s->z);
    float gz=coast_distance(s->x,s->z+.2f)-coast_distance(s->x,s->z-.2f);
    float length=sqrtf(gx*gx+gz*gz)+.0001f;
    int cell=nav_nearest(s->x-gx/length*5,s->z-gz/length*5);
    s->x=cell%NAV_W-NAV_X_ORIGIN;s->z=cell/NAV_W-22;
  }
  if(i>=CAPTAINS) {int cell=nav_nearest(s->x,s->z);s->x=cell%NAV_W-NAV_X_ORIGIN;s->z=cell/NAV_W-22;}
  s->yaw = i < 4 ? (i & 1 ? -PI * 0.5f : PI * 0.5f) : i == 4 ? 0 : PI;
  if (i < CAPTAINS) s->yaw = cw_atan2(s->x-PORT_X[i],s->z-PORT_Z[i]);
  s->prev_x = s->x; s->prev_z = s->z; s->prev_yaw = s->yaw;
  s->nav_x=s->x; s->nav_z=s->z; s->ai_tick=0;
  s->speed = 0; s->hp = i==5 && G.mode==VOYAGE ? 75+25*G.humans : 100 + 18 * s->hull; s->respawn = 0; s->cooldown = 0;
  s->protection=0;
  memset(s->held, 0, sizeof s->held);
}

/* a captain whose controller is left alone this long sails on autopilot (the AI captain's helm) until any of its
 * controls is used again, while another person is sailing: a pad that joined and was put down banks cargo for the
 * fleet and fights for its team instead of holding them back from its pier (a voyage nobody plays does not play itself) */
enum { IDLE_HELM = 60 * 20 };
static void reset_match(int mode, int joined_mask) {
  G.paused=0;cw_tree_set_paused(cw_scene_app_tree(),false);
  effects_reset(G.world);
  int humans = 0;
  for (int i = 0; i < CAPTAINS; i++) humans += !!(joined_mask & (1 << i));
  G.mode = mode; G.humans = humans; G.state = PLAYING; G.tick = 0; G.alert = 0;
  memset(G.cargo_notice_ticks,0,sizeof G.cargo_notice_ticks);
  memset(G.hit_ticks,0,sizeof G.hit_ticks);
  G.label_overlaps_max=0;
  G.shot_peak=0;G.shot_aim_error=0;
  memset(G.list_side,0,sizeof G.list_side);memset(G.smoke_tick,0,sizeof G.smoke_tick);
  G.time_left = mode == VOYAGE ? 60 * 12 * 60 : 60 * 6 * 60;
  G.quota = 10 + (humans > 0 ? humans - 1 : 0) * 7;
  G.voyage_stage=0;G.patrol_defeats=0;G.final_quota=0;
  G.objective_notice_ticks=mode==VOYAGE?240:0;
  eruption.warning=eruption.count=eruption.impacts=0;
  for(int i=0;i<ROCKFALLS;i++)cw_node_set(eruption.rocks[i],"visible",cw_val_bool(false));
  G.winner = -1; G.shots_fired = 0; G.goods_taken = 0; G.autopilot = 0;
  cw_rng_seed(&G.rng, 0x50495241u);
  for (int i = 0; i < SHIPS; i++) {
    ship *s = &G.ships[i];
    s->role = i < 4 ? CAPTAIN : i == 4 ? MERCHANT : PATROL;
    s->active = i >= 4 || mode != VOYAGE || !!(joined_mask & (1 << i));
    s->human = i < CAPTAINS && !!(joined_mask & (1 << i));
#ifdef PIRATES_AUTO
    s->human = 0;
#endif
    s->team = mode == TEAMS ? i & 1 : i;
    s->bank = s->hull = s->sails = s->guns = s->damage_done = s->sinks = 0; s->idle = 0; s->engaged = 0;
    memset(s->stored, 0, sizeof s->stored);
    place_ship(i);
    cw_node_set(s->root, "visible", cw_val_bool(s->active));
    cw_node_set(s->hull_upgrade, "visible", cw_val_bool(false));
    cw_node_set(s->sail_b, "visible", cw_val_bool(false));
    cw_node_set(s->gun_left, "visible", cw_val_bool(false));
    cw_node_set(s->gun_right, "visible", cw_val_bool(false));
  }
  for (int i = 0; i < GOODS; i++) {
    good *g=&G.goods[i];g->active=i<GOOD_SPAWNS;g->timer=-1;
    g->x=g->home_x;g->z=g->home_z;g->kind=i%3;
    cw_node_set(g->node, "visible", cw_val_bool(g->active));
  }
  for (int i = 0; i < SHOTS; i++) {
    G.shots[i].active = 0;
    cw_node_set(G.shots[i].node, "visible", cw_val_bool(false));
  }
}

static void damage_ship(int target, int owner, int amount) {
  ship *s = &G.ships[target];
  if (!s->active || s->respawn || s->protection) return;
  /* Combine the three balls of a broadside into one readable hit notice.
   * Presentation only: timers follow fixed simulation time, including pause. */
  if(G.hit_ticks[target]<45) {
    G.hit_amount[target]=0;G.hit_previous_hp[target]=s->hp;
  }
  G.hit_amount[target]+=(int)fminf(s->hp,(float)amount);
  G.hit_ticks[target]=75;
  if (owner >= 0) { /* the ship lists toward the side the shot came from (render only) */
    float ex = G.ships[owner].x - s->x, ez = G.ships[owner].z - s->z;
    G.list_side[target] = ex * cw_cos(s->yaw) - ez * cw_sin(s->yaw) >= 0 ? 1.0f : -1.0f;
  } else if (!G.list_side[target]) G.list_side[target] = target & 1 ? 1.0f : -1.0f;
  s->hp = fmaxf(0, s->hp - (float)amount);
  sound_play(S_HIT,s->x);
  if (owner >= 0 && owner < CAPTAINS) G.ships[owner].damage_done += amount;
  cw_water_splash(G.water, s->x, s->z, 1.8f, 0.24f, 0.6f);
  if (s->hp > 0) return;
  if (owner >= 0 && owner < CAPTAINS) {
    G.ships[owner].sinks++;
    if(target==5)G.patrol_defeats++;
  }
  /* Salvage preserves the manifest; banked resources and upgrades survive. */
  int drop[3]={s->held[0],s->held[1],s->held[2]};
  memset(s->held,0,sizeof s->held);
  if(target==4)drop[COIN]++;
  if(target==5)drop[IRON]++;
  int sequence=0;
  for(int kind=0;kind<3;kind++)for(int count=0;count<drop[kind];count++) {
    int slot=-1;
    for(int i=GOOD_SPAWNS;i<GOODS;i++)if(!G.goods[i].active){slot=i;break;}
    if(slot<0)break;
    good *g=&G.goods[slot];
    float x=s->x+(sequence%3-1)*1.4f,z=s->z+(sequence/3)*1.2f;
    int cell=nav_nearest(x,z);
    g->nav_cell=cell;
    g->x=cell%NAV_W-NAV_X_ORIGIN;g->z=cell/NAV_W-22;
    g->kind=kind;g->active=1;g->timer=60*30;sequence++;
    cw_node_set(g->node,"visible",cw_val_bool(true));
  }
  s->respawn = target < CAPTAINS ? 120 : 240;
  /* Gameplay removes the ship immediately; its existing model can still
   * settle below the surface during the recovery countdown. */
}

/* Fixed-tick, bounded hazard. Warn first; never target harbor approaches. */
static void start_eruption(void) {
  static const float targets[6][2]={{15,-14},{15,-4},{38,-5},{35,3},{14,8},{24,17}};
  for(int i=0;i<ROCKFALLS;i++) {
    int k=(eruption.count*3+i)%6;
    int cell=nav_nearest(targets[k][0],targets[k][1]);
    eruption.x[i]=cell%NAV_W-NAV_X_ORIGIN;eruption.z[i]=cell/NAV_W-22;
  }
  eruption.count++;eruption.warning=ERUPTION_WARNING;
  cannon_effect(25,7,-10,0,0);
  sound_play(S_CANNON,25);
}
static void step_eruption(void) {
  if(active_map)return;
  if(!eruption.warning) {
    if(G.tick>=1080 && (G.tick-1080)%ERUPTION_PERIOD==0)start_eruption();
    return;
  }
  if(--eruption.warning)return;
  for(int k=0;k<ROCKFALLS;k++) {
    float x=eruption.x[k],z=eruption.z[k];
    cw_water_splash(G.water,x,z,3.2f,.65f,1.2f);
    sound_play(S_HIT,x);
    for(int i=0;i<SHIPS;i++)if(dist2(G.ships[i].x,G.ships[i].z,x,z)<3.2f*3.2f)
      damage_ship(i,-1,28);
    cw_node_set(eruption.rocks[k],"visible",cw_val_bool(false));
    eruption.impacts++;
  }
}

static int can_hit(int owner, int target) {
  if (owner == target) return 0;
  if (owner < CAPTAINS && target < CAPTAINS) {
    if (G.mode == VOYAGE) return 0;
    if (G.mode == TEAMS && G.ships[owner].team == G.ships[target].team) return 0;
  }
  if (owner == 5 && target == 4) return 0;
  if (owner >= CAPTAINS && target < CAPTAINS) {
    /* The voyage's first fight is one the captain starts: the patrol and the merchant leave a captain alone until it
     * has banked once or fired first (sw voyage_hunt 1: the old rule, for the gate's control). */
    if (G.mode == VOYAGE && !G.ships[target].engaged && !G.voyage_hunt) return 0;
    /* Home anchorages give captains room to bank, repair and learn controls: 15 m in the voyage, 7 m in a race. */
    float safe = G.mode == VOYAGE && !G.voyage_hunt ? 15 : 7;
    if (harbor_distance2(target,G.ships[target].x,G.ships[target].z) < safe * safe) return 0;
  }
  return 1;
}

/* A ball flies SHOT_LIFE ticks and lands in the sea at the end of them unless it hits a hull or the shore first. Its
 * height is drawn as a parabola under gravity over that flight, from the gun deck (0.9 m) to the water, so a broadside
 * reads as arcs whose splashes show the range. Render only: the simulation tracks the ball over the water as before. */
enum { SHOT_LIFE = 105 };
static float shot_arc_height(int life) {
  const float g = 9.8f, deck = 0.9f, T = SHOT_LIFE / 60.0f;
  const float vy = (0.5f * g * T * T - deck) / T;
  float t = (SHOT_LIFE - life) / 60.0f;
  return fmaxf(0, deck + vy * t - 0.5f * g * t * t);
}
/* where a captain's broadside on this side would splash down: the middle ball's landing point at its current speed */
static void shot_landing(const ship *s, int side, float *x, float *z) {
  float sy = cw_sin(s->yaw), cy = cw_cos(s->yaw), sx = side * cy, sz = -side * sy;
  float T = SHOT_LIFE / 60.0f, v = 14.0f + s->guns * 1.5f;
  *x = s->x + sx * 1.6f + (sx * v + sy * s->speed * 0.35f) * T;
  *z = s->z + sz * 1.6f + (sz * v + cy * s->speed * 0.35f) * T;
}

static void fire_ship(int id, int side) {
  ship *s = &G.ships[id];
  if (s->cooldown || s->respawn || !s->active) return;
  s->protection=0; /* Firing ends safe departure immediately. */
  if (id < CAPTAINS) s->engaged = 1;
  float sy = cw_sin(s->yaw), cy = cw_cos(s->yaw);
  float sx = side * cy, sz = -side * sy;
  for (int n = -1; n <= 1; n++) {
    int slot = -1;
    for (int k = 0; k < SHOTS; k++) if (!G.shots[k].active) { slot = k; break; }
    if (slot < 0) break;
    shot *b = &G.shots[slot];
    b->active = 1; b->owner = id; b->life = SHOT_LIFE;
    b->aimed = id < CAPTAINS && n == 0;
    if (b->aimed) shot_landing(s, side, &b->aim_x, &b->aim_z);
    b->x = s->x + sy * (float)n * 1.2f + sx * 1.6f;
    b->z = s->z + cy * (float)n * 1.2f + sz * 1.6f;
    b->vx = sx * (14.0f + s->guns * 1.5f) + sy * s->speed * 0.35f + sy * n * 0.8f;
    b->vz = sz * (14.0f + s->guns * 1.5f) + cy * s->speed * 0.35f + cy * n * 0.8f;
    cw_node_set(b->node, "visible", cw_val_bool(true));
    G.shots_fired++;
    cannon_effect(b->x,cw_water_height_at(G.water,b->x,b->z,cw_water_time(G.water))+.8f,b->z,sx,sz);
  }
  sound_play(S_CANNON,s->x);
  s->cooldown = 80 - s->guns * 11;
  G.alert += id < CAPTAINS ? 1 : 0;
}

static int nearest_enemy(int id, float max_d2) {
  ship *s = &G.ships[id]; int best = -1;
  for (int i = 0; i < SHIPS; i++) {
    ship *t = &G.ships[i];
    if (!t->active || t->respawn || !can_hit(id, i)) continue;
    if (id == 4 && i == 5) continue;
    float d = dist2(s->x, s->z, t->x, t->z);
    if (d < max_d2) { max_d2 = d; best = i; }
  }
  return best;
}

static void ai_input(int id, float *turn, float *sail, int *fire, int *dock) {
  ship *s = &G.ships[id];
  float tx = 0, tz = 0;
  int enemy = nearest_enemy(id, id == 5 ? 360 : 240);
  if (id == 4) {
    static const float route[4][2] = { { -29, -8 }, { 0, -19 }, { 29, 4 }, { 0, 20 } };
    int leg = (G.tick / 720) % 4;
    tx = route[leg][0]; tz = route[leg][1];
  } else if (id == 5) {
    int prey = -1; float best = 1e9f;
    for (int i = 0; i < CAPTAINS; i++) {
      ship *p = &G.ships[i]; if (!p->active || p->respawn || !can_hit(5,i)) continue;
      float d = dist2(s->x, s->z, p->x, p->z) - held_total(p) * 40.0f;
      if (d < best) { best = d; prey = i; }
    }
    if (prey >= 0) tx = G.ships[prey].x, tz = G.ships[prey].z;
  } else if (G.mode==VOYAGE && G.voyage_stage==1 && !G.ships[5].respawn) {
    tx=G.ships[5].x;tz=G.ships[5].z;
  } else if (held_total(s) >= 3 || (G.time_left < 1800 && held_total(s))) {
    tx = PORT_X[id]+cw_sin(port_yaw(id))*4; tz = PORT_Z[id]+cw_cos(port_yaw(id))*4;
  } else {
    float best = 1e9f; int found = -1;
    int region=nav_region[nav_nearest(s->x,s->z)];
    /* the cargo that pays soonest: reached and carried home quickest by the wind (sw wind_blind: the nearest) */
    for (int k = 0; k < GOODS; k++) if (G.goods[k].active && nav_region[G.goods[k].nav_cell]==region) {
      float gx = G.goods[k].x - s->x, gz = G.goods[k].z - s->z;
      float d = gx * gx + gz * gz;
      if (sw_wind_blind != 1 && sw_wind_blind != 3) {
        /* the sail there and the sail home with it, each over the speed the wind gives its heading */
        float hx = PORT_X[id] - G.goods[k].x, hz = PORT_Z[id] - G.goods[k].z;
        d = sqrtf(d) / wind_factor(cw_atan2(gx, gz)) + sqrtf(hx * hx + hz * hz) / wind_factor(cw_atan2(hx, hz));
      }
      if (d < best) { best = d; found = k; }
    }
    G.ai_good[id] = found;
    if (found >= 0) tx = G.goods[found].x, tz = G.goods[found].z;
    else { tx=PORT_X[id];tz=PORT_Z[id]; }
  }
  if (G.force_target) { tx = G.force_tx; tz = G.force_tz; }
  if(!active_map && eruption.warning)for(int k=0;k<ROCKFALLS;k++) {
    float dx=s->x-eruption.x[k],dz=s->z-eruption.z[k];
    float d=sqrtf(dx*dx+dz*dz);
    if(d<6.0f) {
      if(d<.1f){dx=cw_sin(s->yaw);dz=cw_cos(s->yaw);d=1;}
      tx=eruption.x[k]+dx/d*8;tz=eruption.z[k]+dz/d*8;
      break;
    }
  }
  if (s->ai_tick++ % 15 == 0 || dist2(s->x,s->z,s->nav_x,s->nav_z)<1.0f)
    nav_waypoint(s,tx,tz);
  float dx=s->nav_x-s->x,dz=s->nav_z-s->z;
  float wanted = cw_atan2(dx, dz);
  float delta = cw_wrap_angle(wanted - s->yaw);
  *turn = clampf(delta * 2.2f, -1, 1);
  *sail = fabsf(delta) > 1.1f ? 0.0f : fabsf(delta) > .5f ? .45f : 1.0f;
  /* Stopping to turn cannot work when the stern has no room to swing.
   * Move along the existing heading toward greater shore clearance first;
   * both candidate maneuvers must fit the complete hull and map bounds. */
  if(*sail==0 && !hull_clear(s->x,s->z,s->yaw+*turn*(.021f+s->speed*.002f))) {
    float sy=cw_sin(s->yaw),cy=cw_cos(s->yaw),best=-1e9f;
    int escape=0;
    for(int direction=-1;direction<=1;direction+=2) {
      float x=s->x+sy*.65f*direction,z=s->z+cy*.65f*direction;
      if(fabsf(x)>42||fabsf(z)>21||!hull_clear(x,z,s->yaw))continue;
      float clearance=-coast_distance(x,z);
      if(clearance>best){best=clearance;escape=direction;}
    }
    if(escape){*turn=0;*sail=escape>0?.45f:-.35f;}
  }
  *dock = id < CAPTAINS && harbor_distance2(id,s->x,s->z) < 34;
  *fire = 0;
  if (enemy >= 0) {
    ship *t = &G.ships[enemy];
    float ex = t->x - s->x, ez = t->z - s->z;
    float side = ex * cw_cos(s->yaw) - ez * cw_sin(s->yaw);
    float forward = ex * cw_sin(s->yaw) + ez * cw_cos(s->yaw);
    if (fabsf(side) > fabsf(forward) * 0.8f && dist2(s->x, s->z, t->x, t->z) < 270) *fire = side >= 0 ? 1 : -1;
  }
}

static void bank_ship(int i) {
  ship *s = &G.ships[i];
  if (harbor_distance2(i,s->x,s->z) > 36) return;
  int n = held_total(s);
  if (n) {
    s->bank += n;
    s->engaged = 1;
    sound_play(S_BANK,s->x);
    for (int k = 0; k < 3; k++) { s->stored[k] += s->held[k]; s->held[k] = 0; }
    float before=s->hp;
    s->hp = clampf(s->hp + 20, 0, 100 + s->hull * 18);
    int repaired=(int)(s->hp-before);
    if(repaired)snprintf(G.cargo_notice[i],sizeof G.cargo_notice[i],"+%d BANKED   +%d HULL REPAIRED",n,repaired);
    else snprintf(G.cargo_notice[i],sizeof G.cargo_notice[i],"+%d BANKED   CARGO SAFE IN PORT",n);
    G.cargo_notice_ticks[i]=150;
  }
}

static void upgrade_ship(int i, int kind) {
  ship *s = &G.ships[i];
  if (harbor_distance2(i,s->x,s->z) > 36) return;
  if (kind == 0 && s->hull < 2 && s->stored[TIMBER] >= 2) {
    sound_play(S_UPGRADE,s->x);
    s->stored[TIMBER] -= 2; s->hull++; s->hp += 18;
    cw_node_set(s->hull_upgrade, "visible", cw_val_bool(true));
  } else if (kind == 1 && s->sails < 2 && s->stored[COIN] >= 2) {
    sound_play(S_UPGRADE,s->x);
    s->stored[COIN] -= 2; s->sails++;
    cw_node_set(s->sail_b, "visible", cw_val_bool(true));
    cw_node3d_set_scale(s->sail_b,(t3_vec3){1,1+(s->sails-1)*.15f,1+(s->sails-1)*.15f});
  } else if (kind == 2 && s->guns < 2 && s->stored[IRON] >= 2) {
    sound_play(S_UPGRADE,s->x);
    s->stored[IRON] -= 2; s->guns++;
    cw_node_set(s->gun_left, "visible", cw_val_bool(true));
    cw_node_set(s->gun_right, "visible", cw_val_bool(true));
  }
}

static void move_ship(int i, float turn, float sail) {
  ship *s = &G.ships[i];
  s->prev_x = s->x; s->prev_z = s->z; s->prev_yaw = s->yaw;
  if (s->respawn) {
    if (--s->respawn == 0) { place_ship(i); s->protection=i<CAPTAINS?240:0; cw_node_set(s->root, "visible", cw_val_bool(true)); }
    return;
  }
  if(s->protection)s->protection--;
  float max_speed = (i == 4 ? 4.1f : i == 5 ? 5.0f : 5.6f) + s->sails * 0.65f;
  float load = i < CAPTAINS ? held_total(s) * 0.08f : 0;
  max_speed -= load;
  float wind = wind_factor(s->yaw);
  float wanted = max_speed * clampf(sail, -0.35f, 1) * wind;
  s->speed += (wanted - s->speed) * (sail < 0 ? 0.045f : 0.02f);
  s->yaw = cw_wrap_angle(s->yaw + clampf(turn, -1, 1) * (0.021f + s->speed * 0.002f));
  /* A stern blocked by the shore must not cancel forward escape along the
   * current heading. Resolve angular clearance before translating the hull. */
  if(!hull_clear(s->x,s->z,s->yaw))s->yaw=s->prev_yaw;
  s->x += cw_sin(s->yaw) * s->speed * DT;
  s->z += cw_cos(s->yaw) * s->speed * DT;
  if (s->x < -42 || s->x > 42) { s->x = clampf(s->x, -42, 42); s->speed *= 0.4f; }
  if (s->z < -21 || s->z > 21) { s->z = clampf(s->z, -21, 21); s->speed *= 0.4f; }
  if (!hull_clear(s->x,s->z,s->yaw)) {
    s->x=s->prev_x; s->z=s->prev_z; s->speed*=0.65f;
    if(!hull_clear(s->x,s->z,s->yaw))s->yaw=s->prev_yaw;
  }
  if (s->cooldown) s->cooldown--;
  if ((G.tick + i) % 4 == 0 && s->speed > 0.7f) {
    float x = s->x - cw_sin(s->yaw) * 2.6f, z = s->z - cw_cos(s->yaw) * 2.6f;
    cw_water_wake(G.water, x, z, cw_sin(s->yaw) * s->speed, cw_cos(s->yaw) * s->speed, 1.2f, 0.055f, 0.38f);
  }
}

static void step_shots(void) {
  for (int i = 0; i < SHOTS; i++) {
    shot *b = &G.shots[i]; if (!b->active) continue;
    b->x += b->vx * DT; b->z += b->vz * DT;
    int hit = -1;
    for (int j = 0; j < SHIPS; j++) {
      ship *s = &G.ships[j];
      if (!s->active || s->respawn || !can_hit(b->owner, j)) continue;
      if (dist2(b->x, b->z, s->x, s->z) < 2.4f * 2.4f) { hit = j; break; }
    }
    int land = coast_distance(b->x,b->z) > 0;
    if (hit >= 0) damage_ship(hit, b->owner, 7 + G.ships[b->owner].guns * 2);
    if (hit >= 0 || land || --b->life <= 0 || fabsf(b->x) > 46 || fabsf(b->z) > 25) {
      float water_y=cw_water_height_at(G.water,b->x,b->z,cw_water_time(G.water));
      if(hit>=0) impact_effect(IMPACT_WOOD,b->x,water_y+1.2f,b->z,b->vx,b->vz);
      else if(land) impact_effect(IMPACT_LAND,b->x,coast_height(b->x,b->z)+.15f,b->z,b->vx,b->vz);
      else {
        cw_water_splash(G.water, b->x, b->z, 0.7f, 0.12f, 0.3f);
        if (b->aimed && b->life <= 0) G.shot_aim_error = fmaxf(G.shot_aim_error, sqrtf(dist2(b->x, b->z, b->aim_x, b->aim_z)));
        impact_effect(IMPACT_WATER,b->x,water_y+.1f,b->z,b->vx,b->vz);
      }
      b->active = 0; cw_node_set(b->node, "visible", cw_val_bool(false));
    }
  }
}

static void step_goods(void) {
  for (int k = 0; k < GOODS; k++) {
    good *g = &G.goods[k];
    if (!g->active) {
      if (k<GOOD_SPAWNS && g->timer>0 && --g->timer == 0) {
        g->x=g->home_x;g->z=g->home_z;g->kind=k%3;
        g->active = 1; cw_node_set(g->node, "visible", cw_val_bool(true));
      }
      continue;
    }
    if(k>=GOOD_SPAWNS && --g->timer<=0) {g->active=0;cw_node_set(g->node,"visible",cw_val_bool(false));continue;}
    if((G.tick+k*7)%24==0)cw_water_splash(G.water,g->x,g->z,.65f,.012f,.035f);
    for (int i = 0; i < CAPTAINS; i++) {
      ship *s = &G.ships[i];
      if (!s->active || s->respawn || held_total(s) >= 6) continue;
      if (dist2(g->x, g->z, s->x, s->z) > 2.3f * 2.3f) continue;
      s->held[g->kind]++; G.goods_taken++;
      static const char *cargo_names[]={"WOOD","IRON","COIN"};
      snprintf(G.cargo_notice[i],sizeof G.cargo_notice[i],"+1 %s COLLECTED",cargo_names[g->kind]);
      G.cargo_notice_ticks[i]=90;
      sound_play(S_CARGO,s->x);
      g->active = 0; g->timer = 360 + 60 * (k % 4);
      cw_node_set(g->node, "visible", cw_val_bool(false));
      cw_water_splash(G.water, g->x, g->z, 0.8f, 0.09f, 0.32f);
      break;
    }
  }
}

static void check_end(void) {
  if (G.mode == VOYAGE) {
    int bank = 0; for (int i = 0; i < CAPTAINS; i++) bank += G.ships[i].bank;
    if(G.voyage_stage==0 && bank>=G.quota) {G.voyage_stage=1;G.objective_notice_ticks=240;sound_play(S_UPGRADE,0);}
    if(G.voyage_stage==1 && G.patrol_defeats>0) {
      G.voyage_stage=2;G.final_quota=bank+G.quota;G.objective_notice_ticks=240;sound_play(S_BANK,0);
    }
    if(G.voyage_stage==2 && bank>=G.final_quota) {G.state=RESULT;G.winner=0;}
    else if(G.time_left<=0) {G.state=RESULT;G.winner=-1;}
  } else if (G.time_left <= 0) {
    int best = -1, score = -1, tied = 0;
    int competitors = G.mode == TEAMS ? 2 : CAPTAINS;
    for (int i = 0; i < competitors; i++) {
      int v = G.ships[i].bank;
      if (G.mode == TEAMS) v += G.ships[i + 2].bank;
      if (v > score) { score = v; best = i; tied = 0; }
      else if (v == score) tied = 1;
    }
    if (tied) best = -2;
    G.winner = best; G.state = RESULT;
  }
}

static void debug_update(void) {
  uint32_t h = CW_HASH32_INIT;
  h = cw_hash32(&G.state, sizeof G.state, h);
  h = cw_hash32(&G.mode, sizeof G.mode, h);
  h = cw_hash32(&active_map, sizeof active_map, h);
  h = cw_hash32(&eruption.warning, sizeof eruption.warning, h);
  h = cw_hash32(&eruption.count, sizeof eruption.count, h);
  if(eruption.warning) {
    h = cw_hash32(eruption.x, sizeof eruption.x, h);
    h = cw_hash32(eruption.z, sizeof eruption.z, h);
  }
  for (int i = 0; i < SHIPS; i++) {
    ship *s = &G.ships[i];
    h = cw_hash32(&s->x, sizeof(float) * 5, h);
    h = cw_hash32(s->held, sizeof s->held, h);
    h = cw_hash32(s->stored, sizeof s->stored, h);
    h = cw_hash32(&s->bank, sizeof(int) * 6, h);
    h = cw_hash32(&s->protection, sizeof s->protection, h);
    if (i < CAPTAINS) {
      G.dbg_bank[i] = s->bank; G.dbg_held[i] = held_total(s);
      G.dbg_hp[i] = (int32_t)s->hp;
      G.dbg_protection[i]=s->protection;
      G.dbg_cooldown[i]=s->cooldown;
      G.dbg_pos[2 * i] = (int32_t)(s->x * 100);
      G.dbg_pos[2 * i + 1] = (int32_t)(s->z * 100);
    }
  }
  for (int i = 0; i < GOODS; i++) h = cw_hash32(&G.goods[i].active, sizeof(int), h);
  G.hash = h;
  G.ripple_hash = cw_water_ripple_hash(G.water);
}

static void fixed_update(void *user, double dt) {
  (void)user; (void)dt;
  if (G.state == TITLE) {
    if(cw_action_just_pressed(0,G.map_next)) {active_map=1-active_map;build_map();for(int i=0;i<GOODS;i++)position_good(i);}
    if (cw_action_just_pressed(0, G.mode_l)) G.mode = (G.mode + 2) % 3;
    if (cw_action_just_pressed(0, G.mode_r)) G.mode = (G.mode + 1) % 3;
    for (int p = 1; p < CAPTAINS; p++) if (cw_action_just_pressed(p, G.start)) G.joined |= 1 << p;
    if (cw_action_just_pressed(0, G.start) || cw_action_just_pressed(0, G.dock)) {
      reset_match(G.mode, G.joined);
    }
    debug_update();
    return;
  }
  if (G.state == RESULT) {
    if (cw_action_just_pressed(0, G.start)) { G.state = TITLE; G.joined = 1; }
    debug_update(); return;
  }
  G.tick++; G.time_left--;
  for(int i=0;i<CAPTAINS;i++)if(G.cargo_notice_ticks[i]>0)G.cargo_notice_ticks[i]--;
  for(int i=0;i<SHIPS;i++)if(G.hit_ticks[i]>0)G.hit_ticks[i]--;
  if(G.objective_notice_ticks>0)G.objective_notice_ticks--;
  /* every person's idle time first, so the autopilot below sees them all from the same tick */
  for (int i = 0; i < CAPTAINS; i++) {
    ship *s = &G.ships[i]; if (!s->active || !s->human) continue;
    bool touched = cw_input_axis(i, G.turn_l, G.turn_r) != 0 || cw_action_strength(i, G.sail_u) != 0 ||
      cw_action_strength(i, G.sail_d) != 0 || cw_action_pressed(i, G.fire_l) || cw_action_pressed(i, G.fire_r) ||
      cw_input_axis(i, G.aim_l, G.aim_r) != 0 || cw_action_pressed(i, G.dock) || cw_action_pressed(i, G.up_hull) ||
      cw_action_pressed(i, G.up_sails) || cw_action_pressed(i, G.up_guns);
    s->idle = touched ? 0 : s->idle + 1;
  }
  for (int i = 0; i < SHIPS; i++) {
    ship *s = &G.ships[i]; if (!s->active) continue;
    float turn = 0, sail = 0; int fire = 0, dock = 0;
    if (s->human) {
      turn = cw_input_axis(i, G.turn_l, G.turn_r);
      sail = cw_action_strength(i, G.sail_u) - cw_action_strength(i, G.sail_d) * 0.3f;
      bool fl = cw_action_pressed(i, G.fire_l), fr = cw_action_pressed(i, G.fire_r);
      if (fl || fr) { /* the left cannons, the right ones, or the side the right stick leans to; both buttons: the right */
        float aim = cw_input_axis(i, G.aim_l, G.aim_r) - (fl ? cw_action_strength(i, G.fire_l) : 0) + (fl && fr ? 1 : 0);
        fire = aim < -0.1f ? -1 : 1;
      }
      dock = cw_action_just_pressed(i, G.dock);
      bool crewed = false; /* someone else is at a helm: when everyone has put their controller down, nobody sails */
      for (int j = 0; j < CAPTAINS; j++) crewed = crewed || (j != i && G.ships[j].active && G.ships[j].human && G.ships[j].idle < IDLE_HELM);
      if (s->idle >= IDLE_HELM && crewed && !G.idle_stays) { G.autopilot |= 1 << i; ai_input(i, &turn, &sail, &fire, &dock); }
      else G.autopilot &= ~(1 << i);
    } else {
      ai_input(i, &turn, &sail, &fire, &dock);
    }
    move_ship(i, turn, sail);
    if (fire) fire_ship(i, fire);
    if (i < CAPTAINS && !s->respawn) {
      if (dock) bank_ship(i);
      if (s->human && !(G.autopilot & (1 << i))) {
        if (cw_action_just_pressed(i, G.up_hull)) upgrade_ship(i, 0);
        if (cw_action_just_pressed(i, G.up_sails)) upgrade_ship(i, 1);
        if (cw_action_just_pressed(i, G.up_guns)) upgrade_ship(i, 2);
      } else if (dock && G.tick % 60 == 0) {
        for (int k = 0; k < 3; k++) upgrade_ship(i, k);
      }
    }
  }
  step_shots(); step_goods(); step_eruption(); check_end(); debug_update();
}

static void sync_visuals(void) {
  double wt = cw_water_time(G.water);
  for(int i=0;i<CAPTAINS;i++) {
    cw_node_set(G.harbor_flags[i],"visible",cw_val_bool(G.state==TITLE||G.ships[i].active));
    /* every flag streams downwind (the standard's fly points along its +x) */
    cw_node3d_set_rotation(G.harbor_flags[i],(t3_vec3){.12f*cw_sin((float)wt*3+i),wind_to()-PI/2-port_yaw(i)+.1f*cw_sin((float)wt*1.7f+i),0});
  }
  for(int i=0;i<ROCKFALLS;i++) {
    bool flying=!active_map && eruption.warning>0 && eruption.warning<=90;
    cw_node_set(eruption.rocks[i],"visible",cw_val_bool(flying));
    if(flying) {
      float t=1-eruption.warning/90.0f;
      float x=25+(eruption.x[i]-25)*t,z=-10+(eruption.z[i]+10)*t;
      float y=7*(1-t)+18*t*(1-t);
      cw_node3d_set_position(eruption.rocks[i],(t3_vec3){x,y,z});
      cw_node3d_set_rotation(eruption.rocks[i],(t3_vec3){t*7+i,t*5,t*9});
    }
  }
  for (int i = 0; i < SHIPS; i++) {
    ship *s = &G.ships[i]; if (!s->active) continue;
    cw_water_sample w;
    cw_water_sample_at(G.water, s->x, s->z, wt, &w);
    if(s->respawn) {
      float age=(float)((i<CAPTAINS?120:240)-s->respawn);
      float t=clampf(age/90.0f,0,1);
      cw_node_set(s->root,"visible",cw_val_bool(t<1));
      cw_node3d_set_position(s->root,(t3_vec3){s->x,w.height+.15f-5.8f*t*t,s->z});
      cw_node3d_set_rotation(s->root,(t3_vec3){.2f*t,s->yaw,(i&1?1:-1)*1.1f*t});
      continue;
    }
    /* Damage shows on the hull: below full health a ship sits lower in the water (up to 0.4 m), lists toward the
     * side it was hit on (up to 9 degrees) and dips its bow; under half its hull it trails dark smoke. */
    float max_hp = i == 5 && G.mode == VOYAGE ? 75 + 25 * G.humans : 100 + 18 * s->hull;
    float dmg = clampf(1 - s->hp / max_hp, 0, 1);
    float list = (G.list_side[i] ? G.list_side[i] : 1) * 0.16f * dmg;
    cw_node3d_set_position(s->root, (t3_vec3){ s->x, w.height + 0.15f - 0.4f * dmg, s->z });
    /* Resolve the surface slope into the ship's local axes before yawing. */
    float sy = cw_sin(s->yaw), cy = cw_cos(s->yaw);
    cw_node3d_set_rotation(s->root, (t3_vec3){ -(w.normal.z * cy + w.normal.x * sy) * 0.65f + 0.05f * dmg,
                                              s->yaw, (w.normal.x * cy - w.normal.z * sy) * 0.65f + list });
    if (dmg > 0.5f && G.state == PLAYING && G.tick - G.smoke_tick[i] >= (dmg > 0.75f ? 9 : 16)) {
      G.smoke_tick[i] = G.tick;
      damage_smoke(s->x - sy * 0.4f, w.height + 2.2f, s->z - cy * 0.4f);
    }
    cw_node3d_set_rotation(s->flag,(t3_vec3){.12f*cw_sin((float)wt*3+i),cw_wrap_angle(wind_to()-PI/2-s->yaw)+.1f*cw_sin((float)wt*1.7f+i),0});
    /* The sails fill with the wind they get: full and steady running or on a reach, slack and shivering when the ship
     * beats into the wind (the canvas's one morph target is the wind-filled shape) */
    float fill=sw_no_wind?1:clampf((cw_cos(s->yaw-wind_to())+1)/.9f,0,1);
    G.canvas_weights[i]=s->canvas?.18f+.74f*fill+(.03f+.14f*(1-fill))*cw_sin((float)wt*(4.2f-1.6f*fill)+i)+.04f*cw_sin((float)wt*1.3f+i*.7f):-1;
    if(s->canvas)s->canvas->morph_influences[0]=G.canvas_weights[i];
    /* Holes torn in the sails: none above 80 % hull, then the first few open and widen, more follow down to the last */
    float cut=dmg<.2f?.04f:.12f+.78f*clampf((dmg-.2f)/.7f,0,1);
    G.hole_cut[i]=cut;
    if(s->canvas&&s->canvas->material_count&&s->canvas->materials[0]->alpha_test!=cut){s->canvas->materials[0]->alpha_test=cut;}
  }
  /* Very Low and Low: no sun shadow map, so each ship lays a soft blob shadow on the water, cast the way the sun
   * falls (its mast and sails reach about 3 m up), and nothing sails without a shadow */
  cw_splats_begin(G.ship_shadows);
  if (G.blob_shadows) {
    const t3_mat4 *sun = cw_node3d_global_transform(cw_node_get(G.world, "Sun"));
    float lx = -sun->e[8], lz = -sun->e[10], ly = -sun->e[9]; /* the direction the light travels */
    float reach = ly < -0.2f ? 3.0f / -ly : 15.0f;
    for (int i = 0; i < SHIPS; i++) {
      ship *s = &G.ships[i];
      if (!s->active || s->respawn) continue;
      float h = cw_water_height_at(G.water, s->x, s->z, wt);
      cw_splats_add(G.ship_shadows, (t3_vec3){ s->x + lx * reach * .5f, h, s->z + lz * reach * .5f }, 6.4f, 3.0f, s->yaw, 0,
                    (t3_color){ 0, 0, 0 }, .42f);
    }
  }
  cw_splats_end(G.ship_shadows);
  for (int i = 0; i < GOODS; i++) if (G.goods[i].active) {
    good *g = &G.goods[i];
    for(int k=0;k<3;k++)cw_node_set(g->appearance[k],"visible",cw_val_bool(k==g->kind));
    float h = cw_water_height_at(G.water, g->x, g->z, wt);
    cw_node3d_set_position(g->node, (t3_vec3){ g->x, h + 0.08f, g->z });
    cw_node3d_set_rotation(g->node, (t3_vec3){ .12f*cw_sin(G.tick*.025f+i), (float)(i * 2) + .12f*cw_sin(G.tick*.009f+i), .1f*cw_cos(G.tick*.02f+i) });
  }
  for (int i = 0; i < SHOTS; i++) if (G.shots[i].active) {
    shot *b = &G.shots[i];
    float h = cw_water_height_at(G.water, b->x, b->z, wt);
    float y = G.flat_shots ? 0.7f : shot_arc_height(b->life);
    if (y > G.shot_peak) G.shot_peak = y;
    cw_node3d_set_position(b->node, (t3_vec3){ b->x, h + y, b->z });
  }
}

/* The HUD is laid out from measured text: the player's text scale (cartwheel/render2d.h "text size") grows every
 * line, so panels take their size from what they hold and stack by measured heights, and lines too wide for their
 * box wrap. Every text sits on a panel of its own (own_backdrop). Sizes are a share of the canvas height: body text
 * 26 px at 1080 lines or more. */
static cw_text_style tstyle(float size, cw_color color, float width, cw_text_align align) {
  return (cw_text_style){ .size = size, .color = color, .wrap_width = width, .align = align, .own_backdrop = true };
}
/* a text in the box [x, x + width] (wrapped there); returns its measured height */
static float line(const char *str, float x, float y, float size, cw_color color, float width, cw_text_align align) {
  cw_text_style st = tstyle(size, color, width, align);
  return cw_r2d_text(G.font, str, cw_v2(x, y), &st).height;
}
static cw_text_metrics measure(const char *str, float size, float width) {
  cw_text_style st = tstyle(size, CW_WHITE, width, CW_ALIGN_LEFT);
  return cw_font_measure(G.font, str, &st);
}
/* A line with button prompts in it (cartwheel/prompt.h markup: "{dock} bank cargo"), for one captain (-1: every
 * seat, on the shared screens), flowed into the box [x, x + width]: its groups (runs of three or more spaces part
 * them) stay whole and go onto as many lines as they need; only a group wider than the box breaks between its
 * words. draw false only measures. Returns the height. */
static float pflow_out(const char *s, int player, float x, float y, float size, cw_color color, float width, cw_text_align align, bool draw) {
  float h = cw_prompt_text_size(G.font, s, player, size).y;
  if (draw) {
    float px = align == CW_ALIGN_CENTER ? x + width * 0.5f : align == CW_ALIGN_RIGHT ? x + width : x;
    cw_prompt_text(G.font, s, player, cw_v2(px, y), size, color, align);
  }
  return h;
}
static float pflow(const char *str, int player, float x, float y, float size, cw_color color, float width, cw_text_align align, bool draw) {
  char out[256] = "", cand[256];
  float top = y;
  const char *p = str;
  while (*p == ' ') p++;
  while (*p) {
    /* the next group: up to a run of 3+ spaces */
    const char *e = p;
    while (*e && !(e[0] == ' ' && e[1] == ' ' && e[2] == ' ')) e++;
    const char *next = e;
    while (*next == ' ') next++;
    snprintf(cand, sizeof cand, "%s%s%.*s", out, out[0] ? "    " : "", (int)(e - p), p);
    if (!out[0] || cw_prompt_text_size(G.font, cand, player, size).x <= width) {
      snprintf(out, sizeof out, "%s", cand);
    } else {
      y += pflow_out(out, player, x, y, size, color, width, align, draw);
      snprintf(out, sizeof out, "%.*s", (int)(e - p), p);
    }
    /* a group too wide alone: its words, as many as fit a line */
    while (cw_prompt_text_size(G.font, out, player, size).x > width) {
      char *cut = NULL;
      for (char *q = out + strlen(out) - 1; q > out; q--)
        if (*q == ' ') { *q = 0; bool fit = cw_prompt_text_size(G.font, out, player, size).x <= width; *q = ' '; if (fit) { cut = q; break; } }
      if (!cut) cut = strchr(out, ' ');
      if (!cut) break;
      *cut = 0;
      y += pflow_out(out, player, x, y, size, color, width, align, draw);
      char rest[256];
      const char *r = cut + 1;
      while (*r == ' ') r++;
      snprintf(rest, sizeof rest, "%s", r);
      snprintf(out, sizeof out, "%s", rest);
    }
    p = next;
  }
  if (out[0]) y += pflow_out(out, player, x, y, size, color, width, align, draw);
  return y - top;
}
static float pline(const char *str, int player, float x, float y, float size, cw_color color, float width, cw_text_align align) {
  return pflow(str, player, x, y, size, color, width, align, true);
}
/* a box's width: wider with the text scale, never past the canvas less its margins */
static float box_width(float base, float W, float margin) { return fminf(base * fmaxf(1, cw_text_scale()), W - 2 * margin); }
static float text_scale_k(void) { return fmaxf(1, cw_text_scale()); }

/* the captain panels at the bottom: one per active captain, side by side; each grows upward from the bottom
 * margin with what it holds (the panel, the cannon meter over it, the harbor's upgrades over that). draw false
 * only measures; returns the height of the tallest stack */
static float captain_panels(float W, float H, float scale, bool draw) {
  char buf[160];
  float m = 25 * scale, pad = 10 * scale, gap = 6 * scale, size = 26 * scale;
  int count = 0;
  for (int i = 0; i < CAPTAINS; i++) count += G.ships[i].active;
  if (!count) return 0;
  float slot = fminf((W - m * (float)(count + 1)) / (float)count, 440 * scale * text_scale_k());
  float bottom = H - 14 * scale, tallest = 0;
  int slot_i = 0;
  for (int i = 0; i < CAPTAINS; i++) {
    ship *s = &G.ships[i]; if (!s->active) continue;
    float x = m + (float)slot_i++ * (slot + m), inner = slot - 2 * pad - 8 * scale, tx = x + pad + 8 * scale;
    /* the panel: who, hull and cargo, the hold */
    char r1[96], r2[96], r3[96];
    cw_color cargo_ink = CW_WHITE;
    snprintf(r1, sizeof r1, "P%d %s     %d BANKED", i + 1, G.autopilot & (1 << i) ? "AUTOPILOT" : s->human ? "CAPTAIN" : "BOT", s->bank);
    snprintf(r2, sizeof r2, "HULL %d    CARGO %d / 6%s", (int)s->hp, held_total(s), s->respawn ? "    REPAIRING" : s->protection ? "    PROTECTED" : "");
    if (!s->respawn && held_total(s) == 6) { snprintf(r3, sizeof r3, "HOLD FULL: RETURN TO P%d HARBOR", i + 1); cargo_ink = cw_hex(0xffdf9dff); }
    else if (!s->respawn && G.cargo_notice_ticks[i] > 0) { snprintf(r3, sizeof r3, "%s", G.cargo_notice[i]); cargo_ink = cw_hex(0xa9f2c1ff); }
    else snprintf(r3, sizeof r3, "WOOD %d   IRON %d   COIN %d", s->stored[0], s->stored[1], s->stored[2]);
    float h1 = pflow(r1, i, 0, 0, size, CW_WHITE, inner, CW_ALIGN_LEFT, false);
    float h2 = pflow(r2, i, 0, 0, size, CW_WHITE, inner, CW_ALIGN_LEFT, false);
    float h3 = pflow(r3, i, 0, 0, size, CW_WHITE, inner, CW_ALIGN_LEFT, false);
    float ph = pad + h1 + h2 + h3 + pad * 0.6f, top = bottom - ph;
    if (draw) {
      cw_r2d_rect(cw_rect_make(x, top, slot, ph), cw_rgba(0.012f, 0.035f, 0.055f, 0.78f));
      cw_r2d_rect(cw_rect_make(x, top, 8 * scale, ph), col(PLAYER_COLOR[i]));
      float y = top + pad * 0.6f;
      y += pflow(r1, i, tx, y, size, CW_WHITE, inner, CW_ALIGN_LEFT, true);
      y += pflow(r2, i, tx, y, size, cw_hex(0xffdf9dff), inner, CW_ALIGN_LEFT, true);
      pflow(r3, i, tx, y, size, cargo_ink, inner, CW_ALIGN_LEFT, true);
    }
    if (G.state == PLAYING && s->human) {
      /* Both broadsides share the crew's existing reload timer. Show one
       * meter, rather than suggesting the other side can fire sooner. */
      float ready = s->respawn ? 0 : clampf(1 - s->cooldown / (float)(80 - s->guns * 11), 0, 1);
      cw_color ink = s->respawn ? cw_hex(0x9aa6afff) : s->cooldown ? cw_hex(0xffdf9dff) : cw_hex(0xa9f2c1ff);
      if (s->respawn) snprintf(buf, sizeof buf, "CANNONS UNAVAILABLE: REPAIRING");
      else if (s->cooldown) snprintf(buf, sizeof buf, "RELOAD %.1fs   {fire_left} LEFT   {fire_right} RIGHT", s->cooldown / 60.0f);
      else snprintf(buf, sizeof buf, "CANNONS READY   {fire_left} LEFT   {fire_right} RIGHT");
      float th = pflow(buf, i, 0, 0, size, ink, inner, CW_ALIGN_LEFT, false);
      float ch = pad * 0.4f + th + 9 * scale;
      top -= gap + ch;
      if (draw) {
        cw_r2d_rect(cw_rect_make(x, top, slot, ch), cw_rgba(.012f, .035f, .055f, .88f));
        pflow(buf, i, tx, top + pad * 0.4f, size, ink, inner, CW_ALIGN_LEFT, true);
        cw_r2d_rect(cw_rect_make(tx, top + ch - 6 * scale, inner, 3 * scale), cw_hex(0x42515bff));
        cw_r2d_rect(cw_rect_make(tx, top + ch - 6 * scale, inner * ready, 3 * scale), ink);
      }
    }
    if (G.state == PLAYING && s->human && !s->respawn &&
        (held_total(s) || s->stored[0] || s->stored[1] || s->stored[2]) &&
        harbor_distance2(i, s->x, s->z) < 36) {
      const int levels[] = { s->hull, s->sails, s->guns };
      const int funds[] = { s->stored[TIMBER], s->stored[COIN], s->stored[IRON] };
      const char *names[] = { "{upgrade_hull} Hull", "{upgrade_sails} Sails", "{upgrade_guns} Cannons" };
      const char *costs[] = { "2 wood", "2 coin", "2 iron" };
      char rows[3][96];
      float hh = pflow("HARBOR: {dock} bank cargo", i, 0, 0, size, CW_WHITE, inner, CW_ALIGN_LEFT, false), rh[3];
      for (int k = 0; k < 3; k++) {
        snprintf(rows[k], sizeof rows[k], "%s %d/2  %s", names[k], levels[k], levels[k] >= 2 ? "MAX" : costs[k]);
        rh[k] = pflow(rows[k], i, 0, 0, size, CW_WHITE, inner, CW_ALIGN_LEFT, false);
      }
      float bh = pad * 0.6f + hh + rh[0] + rh[1] + rh[2] + pad * 0.6f;
      top -= gap + bh;
      if (draw) {
        cw_r2d_rect(cw_rect_make(x, top, slot, bh), cw_rgba(.015f, .035f, .05f, .91f));
        float y = top + pad * 0.6f;
        y += pflow("HARBOR: {dock} bank cargo", i, tx, y, size, col(PLAYER_COLOR[i]), inner, CW_ALIGN_LEFT, true);
        for (int k = 0; k < 3; k++) {
          cw_color ink = levels[k] >= 2 ? cw_hex(0x87c5a0ff) : funds[k] >= 2 ? cw_hex(0xffdf9dff) : cw_hex(0x9aa6afff);
          y += pflow(rows[k], i, tx, y, size, ink, inner, CW_ALIGN_LEFT, true);
        }
      }
    }
    tallest = fmaxf(tallest, bottom - top);
  }
  return tallest + 14 * scale;
}

/* the bar along the top: the mode and the clock, the objective, the controls hint early in a match. draw false
 * only measures; returns its height */
static float top_bar(float W, float scale, const char *mode, bool draw) {
  char obj[160], tm[16];
  float m = 25 * scale, pad = 12 * scale;
  cw_color obj_ink = CW_WHITE;
  snprintf(tm, sizeof tm, "%d:%02d", G.time_left / 3600, (G.time_left / 60) % 60);
  if (G.mode == VOYAGE) {
    int bank = 0; for (int i = 0; i < CAPTAINS; i++) bank += G.ships[i].bank;
    if (G.voyage_stage == 0) snprintf(obj, sizeof obj, "1/3 SUPPLY THE FLEET: BANK %d / %d", bank, G.quota);
    else if (G.voyage_stage == 1) snprintf(obj, sizeof obj, "2/3 BREAK THE BLOCKADE: SINK THE PATROL");
    else snprintf(obj, sizeof obj, "3/3 BRING IT HOME: BANK %d / %d", bank, G.final_quota);
  } else if (G.mode == TEAMS) {
    snprintf(obj, sizeof obj, "P1 + P3: %d      P2 + P4: %d", G.ships[0].bank + G.ships[2].bank, G.ships[1].bank + G.ships[3].bank);
    obj_ink = cw_hex(0xffdf9dff);
  } else snprintf(obj, sizeof obj, "COLLECT  >  BANK  >  UPGRADE");
  const char *hint = G.tick < 1200 && G.humans > 0 && G.mode != VOYAGE ?
    "{turn_left|turn_right|sail_up|sail_down} SAIL     {fire_left} LEFT CANNONS     {fire_right} RIGHT CANNONS" : NULL;
  float inner = W - 2 * m;
  cw_text_metrics tmm = measure(tm, 31 * scale, 0);
  float wind_w = measure("WIND", 22 * scale, 0).width + 52 * scale; /* the wind arrow's place, left of the clock */
  cw_text_metrics mdm = measure(mode, 30 * scale, inner - tmm.width - wind_w - 2 * m);
  float row1 = fmaxf(tmm.height, mdm.height);
  cw_text_metrics om = measure(obj, 26 * scale, 0);
  float obj_w = fminf(om.width, inner);
  float obj_h = measure(obj, 26 * scale, inner).height;
  /* the hint sits right of the objective when both fit on the row, else under it */
  float hint_side = inner - obj_w - 2 * m, hint_h = 0, hint_w = 0;
  bool hint_beside = false;
  if (hint) {
    cw_vec2 hs = cw_prompt_text_size(G.font, hint, -1, 26 * scale);
    hint_w = hs.x;
    hint_beside = hs.x <= hint_side;
    hint_h = hint_beside ? hs.y : pflow(hint, -1, 0, 0, 26 * scale, CW_WHITE, inner, CW_ALIGN_RIGHT, false);
  }
  float h = pad + row1 + (hint && hint_beside ? fmaxf(obj_h, hint_h) : obj_h + hint_h) + pad;
  if (!draw) return h;
  cw_r2d_rect(cw_rect_make(0, 0, W, h), cw_rgba(0.015f, 0.04f, 0.07f, 0.73f));
  line(mode, m, pad, 30 * scale, cw_hex(0xffdb92ff), inner - tmm.width - wind_w - 2 * m, CW_ALIGN_LEFT);
  line(tm, W - m - tmm.width, pad, 31 * scale, CW_WHITE, 0, CW_ALIGN_LEFT);
  {
    /* WIND and an arrow the way it blows on the map below (the camera's view of the heading) */
    float wx = W - m - tmm.width - m - wind_w, cy = pad + row1 * 0.5f;
    cw_text_metrics wm = measure("WIND", 22 * scale, 0);
    line("WIND", wx, cy - wm.height * 0.5f, 22 * scale, cw_hex(0xc2d6e0ff), 0, CW_ALIGN_LEFT);
    float dx = cw_sin(wind_to()), dy = cw_cos(wind_to()), ax = wx + wm.width + 26 * scale, len = 17 * scale;
    t3_camera *cam = cw_camera3d_t3(G.camera);
    if (cam) {
      t3_vec3 a = t3_vec3_apply_mat4(t3_vec3_apply_mat4((t3_vec3){ 0, 0, 0 }, &cam->matrix_world_inverse), &cam->projection_matrix);
      t3_vec3 b = t3_vec3_apply_mat4(t3_vec3_apply_mat4((t3_vec3){ dx * 10, 0, dy * 10 }, &cam->matrix_world_inverse), &cam->projection_matrix);
      dx = b.x - a.x; dy = a.y - b.y;
      float l = sqrtf(dx * dx + dy * dy) + 1e-6f; dx /= l; dy /= l;
    }
    cw_color ink = cw_hex(0xe8f4faff);
    cw_vec2 tail = cw_v2(ax - dx * len, cy - dy * len), tip = cw_v2(ax + dx * len, cy + dy * len);
    cw_r2d_line(tail, tip, 3 * scale, ink);
    cw_r2d_line(tip, cw_v2(tip.x - dx * 9 * scale - dy * 7 * scale, tip.y - dy * 9 * scale + dx * 7 * scale), 3 * scale, ink);
    cw_r2d_line(tip, cw_v2(tip.x - dx * 9 * scale + dy * 7 * scale, tip.y - dy * 9 * scale - dx * 7 * scale), 3 * scale, ink);
  }
  float y = pad + row1;
  line(obj, m, y, 26 * scale, obj_ink, inner, CW_ALIGN_LEFT);
  if (hint) {
    if (hint_beside) cw_prompt_text(G.font, hint, -1, cw_v2(W - m, y), 26 * scale, cw_hex(0xd7d6caff), CW_ALIGN_RIGHT);
    else pflow(hint, -1, m, y + obj_h, 26 * scale, cw_hex(0xd7d6caff), inner, CW_ALIGN_RIGHT, true);
  }
  (void)hint_w;
  return h;
}

/* the player's own text settings, a line for the title and the pause box */
static void text_settings_line(char *buf, size_t n) {
  snprintf(buf, n, "{text_size} Text size %s    {text_background} Text background %s", cw_text_scale_label(), cw_text_backdrop_label());
}

static void draw_title(float W, float H, float scale, const char *mode) {
  char buf[160], settings[160];
  float m = 25 * scale, inner = W - 4 * m, gap = 14 * scale;
  text_settings_line(settings, sizeof settings);
  snprintf(buf, sizeof buf, "<  %s  >", mode);
  char ready[48];
  snprintf(ready, sizeof ready, "%d captain%s ready", 1 + !!(G.joined & 2) + !!(G.joined & 4) + !!(G.joined & 8), G.joined == 1 ? "" : "s");
  const char *brief = G.mode == VOYAGE ? "Supply your fleet, sink the patrol, then bank the final haul. Friendly fire is off." :
                      G.mode == TEAMS ? "P1 + P3 versus P2 + P4. The team with the most banked cargo wins." :
                      "Bank the most cargo before time runs out. Unclaimed captains are AI rivals.";
  const char *place = active_map ? "Sheltered coves and quiet waters. A calmer place to learn the ropes." :
                      "An active volcano guards the eastern passage. Clear marked water before rocks land.";
  const char *menu = "{mode_left|mode_right} mode    {map_next} map    other captains: Start joins    {start} set sail";
  const char *controls = "{turn_left|turn_right|sail_up|sail_down} sail    {fire_left} left cannons    {fire_right} right cannons    {game_pause} pause";
  const char *help = "Sail over cargo to collect it. Return to your harbor and press {dock} to bank and repair.";
  /* measure the stack, then fit it: the gaps close up first, then the title (art, drawn as asked) shrinks */
  float hm = measure(active_map ? "PALM LAGOON" : "THE GILDED SHOALS", 29 * scale, inner).height;
  float hs = measure(buf, 35 * scale, inner).height;
  float hmenu = pflow(menu, -1, 0, 0, 28 * scale, CW_WHITE, inner, CW_ALIGN_CENTER, false);
  float hr = measure(ready, 28 * scale, inner).height;
  float hb = measure(brief, 26 * scale, inner).height;
  float hc = pflow(controls, -1, 0, 0, 28 * scale, CW_WHITE, inner, CW_ALIGN_CENTER, false);
  float hh = pflow(help, -1, 0, 0, 26 * scale, CW_WHITE, inner, CW_ALIGN_CENTER, false);
  float hp = measure(place, 26 * scale, inner).height;
  float hset = pflow(settings, -1, 0, 0, 26 * scale, CW_WHITE, inner, CW_ALIGN_CENTER, false);
  float body = hm + hs + hmenu + hr + hb + hc + hh + hp + hset;
  float logo = 92 * scale, avail = H - 2 * m - 2 * gap;
  int gaps = 5;
  float g = gap;
  if (body + logo * 1.1f + gaps * g > avail) g = fmaxf(4 * scale, (avail - body - logo * 1.1f) / gaps);
  if (body + logo * 1.1f + gaps * g > avail) logo = fmaxf(40 * scale, (avail - body - gaps * g) / 1.1f);
  float total = logo * 1.1f + body + gaps * g + 2 * gap;
  float top = fmaxf(m, (H - total) * 0.5f);
  cw_r2d_rect(cw_rect_make(0, top, W, total), cw_rgba(0.015f, 0.045f, 0.07f, 0.77f));
  float y = top + gap;
  cw_text_style ts = { .size = logo, .color = cw_hex(0xffdb92ff), .wrap_width = W, .align = CW_ALIGN_CENTER, .fixed_size = true };
  cw_r2d_text(G.font, "PIRATES", cw_v2(0, y), &ts); /* the logo: art */
  y += logo * 1.1f;
  float x = (W - inner) * 0.5f;
  y += line(active_map ? "PALM LAGOON" : "THE GILDED SHOALS", x, y, 29 * scale, cw_hex(0xece1c4ff), inner, CW_ALIGN_CENTER);
  y += line(buf, x, y, 35 * scale, CW_WHITE, inner, CW_ALIGN_CENTER) + g;
  y += pline(menu, -1, x, y, 28 * scale, CW_WHITE, inner, CW_ALIGN_CENTER);
  y += line(ready, x, y, 28 * scale, cw_hex(0xffdb92ff), inner, CW_ALIGN_CENTER) + g;
  y += line(brief, x, y, 26 * scale, CW_WHITE, inner, CW_ALIGN_CENTER) + g;
  y += pline(controls, -1, x, y, 28 * scale, CW_WHITE, inner, CW_ALIGN_CENTER);
  y += pline(help, -1, x, y, 26 * scale, CW_WHITE, inner, CW_ALIGN_CENTER) + g;
  y += line(place, x, y, 26 * scale, cw_hex(0xffdb92ff), inner, CW_ALIGN_CENTER) + g;
  pline(settings, -1, x, y, 26 * scale, cw_hex(0xc2c9cdff), inner, CW_ALIGN_CENTER);
}

static void draw_result(float W, float H, float scale, float top_limit) {
  char buf[160];
  float m = 25 * scale, bw = box_width(W * .66f, W, m), bx = (W - bw) * 0.5f, inner = bw - 4 * m, ix = bx + 2 * m;
  float gap = 12 * scale;
  if (G.mode == VOYAGE) snprintf(buf, sizeof buf, "%s", G.winner >= 0 ? "VOYAGE COMPLETE" : "TIME RAN OUT");
  else if (G.winner == -2) snprintf(buf, sizeof buf, "%s", G.mode == TEAMS ? "TEAM MATCH DRAW" : "TREASURE RACE DRAW");
  else if (G.mode == TEAMS) snprintf(buf, sizeof buf, "TEAM %d WINS - P%d + P%d", G.winner + 1, G.winner + 1, G.winner + 3);
  else snprintf(buf, sizeof buf, "CAPTAIN %d WINS", G.winner + 1);
  char a[96] = "", b[96] = "";
  const char *next = NULL;
  int order[CAPTAINS] = { 0, 1, 2, 3 }, count = G.mode == TEAMS ? 2 : CAPTAINS, scores[CAPTAINS];
  char names[CAPTAINS][48], nums[CAPTAINS][16];
  float row_h[CAPTAINS] = { 0 }, name_w = inner * 0.7f;
  float hhead = measure(buf, 52 * scale, inner).height, body = 0;
  if (G.mode == VOYAGE) {
    int total = 0; for (int i = 0; i < CAPTAINS; i++) total += G.ships[i].bank;
    snprintf(a, sizeof a, "FLEET CARGO SAFELY BANKED: %d", total);
    snprintf(b, sizeof b, "PATROL SHIPS DEFEATED: %d", G.patrol_defeats);
    next = G.winner >= 0 ? "All three objectives completed." : G.voyage_stage == 0 ? "Unfinished: supply the fleet." :
           G.voyage_stage == 1 ? "Unfinished: break the patrol blockade." : "Unfinished: bring the final haul home.";
    body = measure(a, 28 * scale, inner).height + measure(b, 26 * scale, inner).height + gap + measure(next, 26 * scale, inner).height;
  } else {
    for (int i = 0; i < count; i++) scores[i] = G.ships[i].bank + (G.mode == TEAMS ? G.ships[i + 2].bank : 0);
    for (int i = 1; i < count; i++) for (int j = i; j > 0 && scores[order[j]] > scores[order[j - 1]]; j--) {
      int temp = order[j]; order[j] = order[j - 1]; order[j - 1] = temp;
    }
    body = measure("BANKED CARGO", 22 * scale, inner).height;
    for (int row = 0; row < count; row++) {
      int id = order[row];
      if (G.mode == TEAMS) snprintf(names[row], sizeof names[row], "TEAM %d   P%d + P%d", id + 1, id + 1, id + 3);
      else snprintf(names[row], sizeof names[row], "P%d   %s", id + 1, G.ships[id].human ? "CAPTAIN" : "BOT CAPTAIN");
      snprintf(nums[row], sizeof nums[row], "%d", scores[id]);
      float nw = measure(nums[row], 27 * scale, 0).width;
      name_w = fminf(name_w, inner - nw - 2 * m);
      row_h[row] = 0;
    }
    for (int row = 0; row < count; row++) {
      row_h[row] = fmaxf(measure(names[row], 26 * scale, name_w).height, measure(nums[row], 27 * scale, 0).height) + 6 * scale;
      body += row_h[row];
    }
    if (G.mode == TEAMS) body += gap + measure("Each team's score combines both captains' banked cargo.", 26 * scale, inner).height;
  }
  const char *press = "PRESS {start} TO CHOOSE A NEW MATCH";
  float hpress = pflow(press, -1, 0, 0, 26 * scale, CW_WHITE, inner, CW_ALIGN_CENTER, false);
  float bh = gap * 2 + hhead + gap + body + gap * 1.5f + hpress + gap * 1.5f;
  float by = fmaxf(top_limit + gap, (H - bh) * 0.5f);
  if (by + bh > H - m) by = fmaxf(m, H - m - bh);
  cw_r2d_rect(cw_rect_make(bx, by, bw, bh), cw_rgba(.015f, .045f, .07f, .94f));
  float y = by + gap * 2;
  y += line(buf, ix, y, 52 * scale, cw_hex(0xffdb92ff), inner, CW_ALIGN_CENTER) + gap;
  if (G.mode == VOYAGE) {
    y += line(a, ix, y, 28 * scale, CW_WHITE, inner, CW_ALIGN_CENTER);
    y += line(b, ix, y, 26 * scale, CW_WHITE, inner, CW_ALIGN_CENTER) + gap;
    y += line(next, ix, y, 26 * scale, cw_hex(0xffdf9dff), inner, CW_ALIGN_CENTER);
  } else {
    y += line("BANKED CARGO", ix, y, 22 * scale, cw_hex(0xc2c9cdff), inner, CW_ALIGN_RIGHT);
    for (int row = 0; row < count; row++) {
      int id = order[row];
      cw_color ink = col(PLAYER_COLOR[id]);
      cw_r2d_rect(cw_rect_make(ix, y + 3 * scale, 4 * scale, row_h[row] - 9 * scale), ink);
      line(names[row], ix + 14 * scale, y, 26 * scale, ink, name_w, CW_ALIGN_LEFT);
      line(nums[row], ix, y, 27 * scale, CW_WHITE, inner, CW_ALIGN_RIGHT);
      y += row_h[row];
    }
    if (G.mode == TEAMS) y += gap + line("Each team's score combines both captains' banked cargo.", ix, y + gap, 26 * scale, CW_WHITE, inner, CW_ALIGN_CENTER);
  }
  y += gap * 1.5f;
  pline(press, -1, ix, y, 26 * scale, CW_WHITE, inner, CW_ALIGN_CENTER);
}

static void draw_pause(float W, float H, float scale) {
  char settings[160];
  float m = 25 * scale, bw = box_width(W * .6f, W, m), bx = (W - bw) * 0.5f, inner = bw - 4 * m, ix = bx + 2 * m, gap = 14 * scale;
  text_settings_line(settings, sizeof settings);
  const char *note = "After sinking: 4 seconds of protection. Firing ends it early.";
  float h = gap * 2 + measure("PAUSED", 64 * scale, inner).height + gap +
            pflow("{game_pause} resumes", -1, 0, 0, 29 * scale, CW_WHITE, inner, CW_ALIGN_CENTER, false) +
            pflow("{leave_match} returns to the chart room", -1, 0, 0, 26 * scale, CW_WHITE, inner, CW_ALIGN_CENTER, false) + gap +
            measure(note, 26 * scale, inner).height + gap +
            pflow(settings, -1, 0, 0, 26 * scale, CW_WHITE, inner, CW_ALIGN_CENTER, false) + gap * 2;
  float by = fmaxf(m, (H - h) * 0.5f);
  cw_r2d_rect(cw_rect_make(0, 0, W, H), cw_rgba(.005f, .015f, .025f, .6f));
  cw_r2d_rect(cw_rect_make(bx, by, bw, h), cw_rgba(.015f, .04f, .06f, .95f));
  float y = by + gap * 2;
  y += line("PAUSED", ix, y, 64 * scale, cw_hex(0xffdb92ff), inner, CW_ALIGN_CENTER) + gap;
  y += pline("{game_pause} resumes", -1, ix, y, 29 * scale, CW_WHITE, inner, CW_ALIGN_CENTER);
  y += pline("{leave_match} returns to the chart room", -1, ix, y, 26 * scale, CW_WHITE, inner, CW_ALIGN_CENTER) + gap;
  y += line(note, ix, y, 26 * scale, cw_hex(0xa9f2e8ff), inner, CW_ALIGN_CENTER) + gap;
  pline(settings, -1, ix, y, 26 * scale, cw_hex(0xc2c9cdff), inner, CW_ALIGN_CENTER);
}

static void draw_hud(void) {
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float scale = cs.y / 1080.0f, W = cs.x, H = cs.y;
  char buf[160];
  const char *mode = G.mode == VOYAGE ? "COOPERATIVE VOYAGE" : G.mode == RACE ? "TREASURE RACE" : "TEAM TREASURE RACE";
  if (G.state == TITLE) {
    draw_title(W, H, scale, mode);
  } else {
    t3_camera *camera=cw_camera3d_t3(G.camera);
    /* paused or the results: the box over the field takes the place of the labels and the captain panels */
    bool overlay = G.paused || G.state == RESULT;
    if (overlay) camera = NULL;
    float bar_h = top_bar(W, scale, mode, false), panels_h = overlay ? 0 : captain_panels(W, H, scale, false);
    /* the boxes under the bar (the rockfall warning, the voyage's objective), then the labels below them */
    float below = bar_h + 8 * scale;
    if(!overlay && !active_map && eruption.warning) {
      snprintf(buf,sizeof buf,"VOLCANIC ROCKFALL: CLEAR THE MARKED WATER  %.1fs",eruption.warning/60.0f);
      float bw=box_width(W*.56f,W,25*scale),bx=(W-bw)*.5f;
      float th=measure(buf,26*scale,bw-20*scale).height,bh=th+12*scale;
      cw_r2d_rect(cw_rect_make(bx,below,bw,bh),cw_rgba(.12f,.035f,.015f,.88f));
      line(buf,bx+10*scale,below+6*scale,26*scale,cw_hex(0xffd59bff),bw-20*scale,CW_ALIGN_CENTER);
      below+=bh+6*scale;
    }
    if(!overlay&&G.mode==VOYAGE&&G.state==PLAYING&&G.objective_notice_ticks>0) {
      float fade=clampf(G.objective_notice_ticks/30.0f,0,1);
      float bw=box_width(W*.46f,W,25*scale),bx=(W-bw)*.5f,inner=bw-20*scale;
      const char *title=G.voyage_stage==0?"1 / 3   SUPPLY THE FLEET":G.voyage_stage==1?"2 / 3   BREAK THE BLOCKADE":"3 / 3   BRING IT HOME";
      const char *hint=G.voyage_stage==0?"Collect floating cargo. {dock} banks it at your harbor.":G.voyage_stage==1?"Turn your side toward the patrol. {fire_left} or {fire_right} fires.":"Gather the final cargo and bank it safely at your harbor.";
      float t1=measure(title,27*scale,inner).height,t2=pflow(hint,-1,0,0,26*scale,CW_WHITE,inner,CW_ALIGN_CENTER,false);
      float bh=8*scale+t1+6*scale+t2+10*scale;
      cw_r2d_rect(cw_rect_make(bx,below,bw,bh),cw_rgba(.015f,.04f,.06f,.9f*fade));
      line(title,bx+10*scale,below+8*scale,27*scale,cw_rgba(1,.86f,.57f,fade),inner,CW_ALIGN_CENTER);
      pline(hint,-1,bx+10*scale,below+8*scale+t1+6*scale,26*scale,cw_rgba(1,1,1,fade),inner,CW_ALIGN_CENTER);
      below+=bh+6*scale;
    }
    float lab_top = below, lab_bottom = H - panels_h - 6 * scale;
    float rects[64][4];int n_rects=0; /* every annotation box drawn over the sea, for the overlap count (pirates.label_overlaps) */
    float lsize = 26 * scale, lpad = 4 * scale;
    /* Lay out complete ship annotations before ports. Keep health, damage
     * and protection together, and connect displaced labels to their ship.
     * Each label's box comes from its measured text: half its width, how far it reaches above its line (the
     * protection timer, a hit notice) and below it (the hull bar). */
    float label_x[SHIPS]={0},label_y[SHIPS]={0},anchor_x[SHIPS]={0},anchor_y[SHIPS]={0};
    float label_half[SHIPS]={0},label_top[SHIPS]={0},label_down[SHIPS]={0},box_half[SHIPS]={0},box_h[SHIPS]={0};
    char label_text[SHIPS][24], prot_text[SHIPS][32];
    int label_visible[SHIPS]={0};
    /* A hit's damage number floats up over the hit ship itself (its hull, not its label, which may sit off to one
     * side), and the labels keep clear of it. Two numbers over ships side by side stack instead of covering. */
    float note_x[SHIPS]={0},note_y[SHIPS]={0},note_w[SHIPS]={0},note_h[SHIPS]={0};int note_on[SHIPS]={0};char note_text[SHIPS][16];
    if(camera)for(int i=0;i<SHIPS;i++) {
      ship *s=&G.ships[i];
      if(!s->active||s->respawn||s->protection||G.hit_ticks[i]<=0)continue;
      t3_vec3 p=t3_vec3_apply_mat4((t3_vec3){s->x,1.5f,s->z},&camera->matrix_world_inverse);
      p=t3_vec3_apply_mat4(p,&camera->projection_matrix);
      float hx=(p.x*.5f+.5f)*W,hy=(.5f-p.y*.5f)*H;
      snprintf(note_text[i],sizeof note_text[i],"-%d",G.hit_amount[i]);
      cw_text_metrics nm=measure(note_text[i],lsize,0);
      note_w[i]=nm.width+16*scale;note_h[i]=nm.height+lpad;
      float rise=(75-G.hit_ticks[i])*.25f*scale;
      note_x[i]=clampf(hx-note_w[i]*.5f,4*scale,W-note_w[i]-4*scale);
      note_y[i]=clampf(hy-note_h[i]-12*scale-rise,lab_top,lab_bottom-note_h[i]);
      for(int tries=0;tries<SHIPS;tries++) {
        int moved=0;
        for(int j=0;j<i;j++)if(note_on[j]&&note_x[i]<note_x[j]+note_w[j]+4*scale&&note_x[i]+note_w[i]+4*scale>note_x[j]&&
          note_y[i]<note_y[j]+note_h[j]+4*scale&&note_y[i]+note_h[i]+4*scale>note_y[j]) {note_y[i]=note_y[j]-note_h[i]-4*scale;moved=1;}
        if(!moved)break;
      }
      note_y[i]=fmaxf(note_y[i],lab_top);
      note_on[i]=1;
    }
    if(camera)for(int i=0;i<SHIPS;i++) {
      ship *s=&G.ships[i];if(!s->active||s->respawn)continue;
      t3_vec3 p=t3_vec3_apply_mat4((t3_vec3){s->x,6.6f,s->z},&camera->matrix_world_inverse);
      p=t3_vec3_apply_mat4(p,&camera->projection_matrix);
      float ax=(p.x*.5f+.5f)*W,ay=(.5f-p.y*.5f)*H;
      int target=G.mode==VOYAGE&&G.voyage_stage==1&&i==5;
      if(i<CAPTAINS)snprintf(label_text[i],sizeof label_text[i],G.autopilot&(1<<i)?"P%d AUTO":"P%d",i+1);
      else snprintf(label_text[i],sizeof label_text[i],"%s",target?"TARGET: PATROL":i==4?"MERCHANT":"PATROL");
      cw_text_metrics lm=measure(label_text[i],lsize,0);
      float bar_half=(i<CAPTAINS?28:45)*scale;
      box_half[i]=lm.width*.5f+8*scale;box_h[i]=lm.height+lpad;
      float half=fmaxf(box_half[i],bar_half)+4*scale;
      float top=box_h[i]*.5f;
      if(s->protection) {
        snprintf(prot_text[i],sizeof prot_text[i],"PROTECTED %.1fs",s->protection/60.0f);
        cw_text_metrics pm=measure("PROTECTED 9.9s",lsize,0);
        half=fmaxf(half,pm.width*.5f+8*scale);top+=4*scale+pm.height+lpad;
      }
      float down=box_h[i]*.5f+8*scale;
      float preferred_y=ay-(target?34*scale:0),best=1e30f;
      float lo=lab_top+top,hi=fmaxf(lo,lab_bottom-down);
      label_x[i]=clampf(ax,half+4*scale,W-half-4*scale);
      label_y[i]=clampf(preferred_y,lo,hi);
      for(int row=-4;row<=4;row++)for(int column=-4;column<=4;column++) {
        float x=clampf(ax+column*70*scale,half+4*scale,W-half-4*scale);
        float y=clampf(preferred_y+row*60*scale,lo,hi);
        float score=(x-ax)*(x-ax)+(y-preferred_y)*(y-preferred_y)*1.1f;
        if(score>=best)continue;
        int overlap=0;
        for(int j=0;j<i;j++)if(label_visible[j]&&
          x+half+6*scale>label_x[j]-label_half[j]&&x-half-6*scale<label_x[j]+label_half[j]&&
          y+down+6*scale>label_y[j]-label_top[j]&&y-top-6*scale<label_y[j]+label_down[j]) {overlap=1;break;}
        if(!G.labels_stack)for(int j=0;j<SHIPS&&!overlap;j++)if(note_on[j]&&
          x+half+6*scale>note_x[j]&&x-half-6*scale<note_x[j]+note_w[j]&&
          y+down+6*scale>note_y[j]&&y-top-6*scale<note_y[j]+note_h[j])overlap=1;
        if(G.labels_stack)overlap=column||row; /* sw labels_stack 1: every label at its ship, as if nothing else were there */
        if(!overlap){label_x[i]=x;label_y[i]=y;best=score;}
      }
      anchor_x[i]=ax;anchor_y[i]=ay;label_half[i]=half;label_top[i]=top;label_down[i]=down;label_visible[i]=1;
    }
    if(camera)for(int i=0;i<CAPTAINS;i++)if(G.ships[i].active) {
      t3_vec3 p=t3_vec3_apply_mat4((t3_vec3){PORT_X[i],0,PORT_Z[i]},&camera->matrix_world_inverse);
      p=t3_vec3_apply_mat4(p,&camera->projection_matrix);
      snprintf(buf,sizeof buf,"P%d HARBOR",i+1);
      cw_text_metrics hm=measure(buf,lsize,0);
      float hw=hm.width*.5f+10*scale,hh=hm.height+lpad;
      float x=(p.x*.5f+.5f)*W,y=(.5f-p.y*.5f)*H+26*scale;
      x=clampf(x,hw+4*scale,W-hw-4*scale);
      /* Ship identity and protection timers take priority over port names.
       * Try nearby rows before hiding a port label in a crowded fight. */
      static const float rows[]={0,-1,1,-2,2};
      float anchor_y=y;int placed=0;
      for(int row=0;row<5&&!placed;row++) {
        float candidate=anchor_y+rows[row]*(hh+8*scale);
        if(candidate<lab_top||candidate+hh>lab_bottom)continue;
        int overlap=0;
        for(int j=0;j<SHIPS;j++) {
          if(!label_visible[j])continue;
          float sx=label_x[j],sy=label_y[j],half=label_half[j];
          if(x+hw+4*scale>sx-half&&x-hw-4*scale<sx+half&&candidate+hh+4*scale>sy-label_top[j]&&candidate-4*scale<sy+label_down[j]) {overlap=1;break;}
        }
        for(int j=0;j<SHIPS&&!overlap;j++)if(note_on[j]&&x+hw+4*scale>note_x[j]&&x-hw-4*scale<note_x[j]+note_w[j]&&
          candidate+hh+4*scale>note_y[j]&&candidate-4*scale<note_y[j]+note_h[j])overlap=1;
        if(!overlap){y=candidate;placed=1;}
      }
      if(!placed)continue;
      if(n_rects<64){rects[n_rects][0]=x-hw;rects[n_rects][1]=y;rects[n_rects][2]=x+hw;rects[n_rects][3]=y+hh;n_rects++;}
      if(y!=anchor_y)cw_r2d_line(cw_v2(x,anchor_y),cw_v2(x,y+hh*.5f),1*scale,col(PLAYER_COLOR[i]));
      cw_r2d_rect(cw_rect_make(x-hw,y,hw*2,hh),cw_rgba(.012f,.035f,.055f,.88f));
      cw_r2d_rect(cw_rect_make(x-hw,y,4*scale,hh),col(PLAYER_COLOR[i]));
      line(buf,x-hw,y+lpad*.5f,lsize,cw_hex(0xffe6baff),hw*2,CW_ALIGN_CENTER);
    }
    if(camera && !active_map && eruption.warning) {
      for(int k=0;k<ROCKFALLS;k++) {
        for(int j=0;j<48;j++) {
          if(j%3==2)continue;
          cw_vec2 points[2];
          for(int e=0;e<2;e++) {
            float a=(j+e)*2*PI/48;
            t3_vec3 p={eruption.x[k]+3.2f*cw_cos(a),.15f,eruption.z[k]+3.2f*cw_sin(a)};
            p=t3_vec3_apply_mat4(p,&camera->matrix_world_inverse);
            p=t3_vec3_apply_mat4(p,&camera->projection_matrix);
            points[e]=cw_v2((p.x*.5f+.5f)*W,(.5f-p.y*.5f)*H);
          }
          cw_r2d_line(points[0],points[1],2.5f*scale,cw_hex(0xffb754ee));
        }
      }
    }
    /* Where each person's broadsides land, a small ring on the water each side (bright when the guns are loaded):
     * the splash of a miss lands on it, so a captain learns to lead a moving ship. */
    if(camera && G.state==PLAYING)for(int i=0;i<CAPTAINS;i++) {
      ship *s=&G.ships[i];
      if(!s->active||!s->human||s->respawn||(G.autopilot&(1<<i)))continue;
      cw_color c=col(PLAYER_COLOR[i]);c.a=s->cooldown?.28f:.7f;
      for(int side=-1;side<=1;side+=2) {
        float lx,lz;shot_landing(s,side,&lx,&lz);
        for(int j=0;j<16;j++) {
          if(j%2)continue;
          cw_vec2 points[2];
          for(int e=0;e<2;e++) {
            float a=(j+e)*2*PI/16;
            t3_vec3 p={lx+1.3f*cw_cos(a),.1f,lz+1.3f*cw_sin(a)};
            p=t3_vec3_apply_mat4(p,&camera->matrix_world_inverse);
            p=t3_vec3_apply_mat4(p,&camera->projection_matrix);
            points[e]=cw_v2((p.x*.5f+.5f)*W,(.5f-p.y*.5f)*H);
          }
          cw_r2d_line(points[0],points[1],2*scale,c);
        }
      }
    }
    /* Draw every connector first so none can cross over another label. */
    if(camera)for(int i=0;i<SHIPS;i++)if(label_visible[i]&&
      fabsf(label_x[i]-anchor_x[i])+fabsf(label_y[i]-anchor_y[i])>4*scale) {
      cw_color color=i<CAPTAINS?col(PLAYER_COLOR[i]):i==4?cw_hex(0xe4bd47ff):cw_hex(0xf2907aff);
      cw_r2d_line(cw_v2(anchor_x[i],anchor_y[i]),cw_v2(label_x[i],label_y[i]),1*scale,color);
    }
    if(camera) for(int i=0;i<SHIPS;i++) {
      ship *s=&G.ships[i];if(!label_visible[i])continue;
      float x=label_x[i],y=label_y[i];
      bool objective_target=G.mode==VOYAGE&&G.voyage_stage==1&&i==5;
      cw_color label_color=i<CAPTAINS?col(PLAYER_COLOR[i]):i==4?cw_hex(0xe4bd47ff):cw_hex(0xf2907aff);
      if(objective_target)label_color=cw_hex(0xffdb92ff);
      float bh=box_h[i],bhalf=box_half[i];
      cw_r2d_rect(cw_rect_make(x-bhalf,y-bh*.5f,bhalf*2,bh),cw_rgba(.01f,.025f,.035f,.87f));
      line(label_text[i],x-bhalf,y-bh*.5f+lpad*.5f,lsize,label_color,bhalf*2,CW_ALIGN_CENTER);
      if(s->protection) {
        cw_text_metrics pm=measure("PROTECTED 9.9s",lsize,0);
        float pw=pm.width*.5f+8*scale,ph=pm.height+lpad,py=y-bh*.5f-4*scale-ph;
        cw_r2d_rect(cw_rect_make(x-pw,py,pw*2,ph),cw_rgba(.015f,.09f,.11f,.9f));
        line(prot_text[i],x-pw,py+lpad*.5f,lsize,cw_hex(0xa9f2e8ff),pw*2,CW_ALIGN_CENTER);
      }
      float max_hp=i==5 && G.mode==VOYAGE?75+25*G.humans:100+18*s->hull;
      float bar_width=(i<CAPTAINS?56:90)*scale,bar_y=y+bh*.5f+2*scale;
      cw_r2d_rect(cw_rect_make(x-bar_width/2,bar_y,bar_width,5*scale),cw_hex(0x342c29ff));
      if(G.hit_ticks[i]>0&&!s->protection) {
        float fade=clampf(G.hit_ticks[i]/30.0f,0,1);
        float trail=s->hp+(G.hit_previous_hp[i]-s->hp)*fade;
        cw_r2d_rect(cw_rect_make(x-bar_width/2,bar_y,bar_width*clampf(trail/max_hp,0,1),5*scale),cw_hex(0xffe3b4ff));
      }
      cw_r2d_rect(cw_rect_make(x-bar_width/2,bar_y,bar_width*clampf(s->hp/max_hp,0,1),5*scale),label_color);
    }
    if(camera)for(int i=0;i<SHIPS;i++)if(label_visible[i]&&n_rects<64) {
      rects[n_rects][0]=label_x[i]-label_half[i];rects[n_rects][1]=label_y[i]-label_top[i];
      rects[n_rects][2]=label_x[i]+label_half[i];rects[n_rects][3]=label_y[i]+label_down[i];n_rects++;
    }
    /* the hit: a ring spreading on the water round the ship for half a second, and the number over it */
    if(camera)for(int i=0;i<SHIPS;i++)if(note_on[i]) {
      ship *s=&G.ships[i];
      float fade=clampf(G.hit_ticks[i]/30.0f,0,1);
      if(G.hit_ticks[i]>45) {
        float age=(75-G.hit_ticks[i])/30.0f,radius=2.6f+age*2.2f;
        cw_color ring=cw_rgba(1,.85f,.62f,.75f*(1-age));
        for(int j=0;j<24;j++) {
          cw_vec2 points[2];
          for(int e=0;e<2;e++) {
            float a=(j+e)*2*PI/24;
            t3_vec3 p={s->x+radius*cw_cos(a),.2f,s->z+radius*cw_sin(a)};
            p=t3_vec3_apply_mat4(p,&camera->matrix_world_inverse);
            p=t3_vec3_apply_mat4(p,&camera->projection_matrix);
            points[e]=cw_v2((p.x*.5f+.5f)*W,(.5f-p.y*.5f)*H);
          }
          cw_r2d_line(points[0],points[1],2*scale,ring);
        }
      }
      cw_r2d_rect(cw_rect_make(note_x[i],note_y[i],note_w[i],note_h[i]),cw_rgba(.07f,.025f,.015f,.85f*fade));
      line(note_text[i],note_x[i],note_y[i]+lpad*.5f,lsize,cw_rgba(1,.8f,.6f,fade),note_w[i],CW_ALIGN_CENTER);
      if(n_rects<64){rects[n_rects][0]=note_x[i];rects[n_rects][1]=note_y[i];rects[n_rects][2]=note_x[i]+note_w[i];rects[n_rects][3]=note_y[i]+note_h[i];n_rects++;}
    }
    {
      int overlaps=0;
      for(int a=0;a<n_rects;a++)for(int b=a+1;b<n_rects;b++)
        overlaps+=rects[a][0]<rects[b][2]-.5f&&rects[b][0]<rects[a][2]-.5f&&rects[a][1]<rects[b][3]-.5f&&rects[b][1]<rects[a][3]-.5f;
      G.label_overlaps=overlaps;if(overlaps>G.label_overlaps_max)G.label_overlaps_max=overlaps;
    }
    top_bar(W, scale, mode, true);
    if (!overlay) captain_panels(W, H, scale, true);
    if (G.state == RESULT) draw_result(W, H, scale, bar_h);
  }
  if(G.paused) draw_pause(W, H, scale);
  cw_r2d_end();
  t3_renderer *r = cw_render3d_renderer(cw_scene_app_tree());
  if (r) t3_renderer_reset_bindings(r);
}

static void text_settings_save(void); /* with the save data, below */

static void update(void *user, double dt) {
  (void)user; (void)dt;
  cw_audio_update();
  static int previous_state=TITLE;
  if(G.state==PLAYING && previous_state==PLAYING) {
    bool toggle=false;
    for(int i=0;i<CAPTAINS;i++)if(G.ships[i].human && cw_action_just_pressed(i,G.pause))toggle=true;
    if(toggle) {G.paused=!G.paused;cw_tree_set_paused(cw_scene_app_tree(),G.paused);}
    if(G.paused)for(int i=0;i<CAPTAINS;i++)if(G.ships[i].human && cw_action_just_pressed(i,G.leave)) {
      G.paused=0;cw_tree_set_paused(cw_scene_app_tree(),false);G.state=TITLE;G.joined=1;break;
    }
  }
  previous_state=G.state;
  /* the text settings: render only, saved with the graphics settings */
  if(G.state==TITLE||G.paused) {
    bool size=false,back=false;
    for(int i=0;i<CAPTAINS;i++)if(i==0||G.ships[i].human) {
      size|=cw_action_just_pressed(i,G.text_size);back|=cw_action_just_pressed(i,G.text_background);
    }
    if(size)cw_text_scale_cycle();
    if(back)cw_text_backdrop_cycle();
    if(size||back)text_settings_save();
  }
  if(!G.paused)sync_visuals();
}

static void render(void *user, double alpha) {
  (void)user; (void)alpha;
  G.gpu_ms = cw_pipeline_gpu_ms(cw_pipeline_of(cw_scene_app_tree()));
  cw_quality_auto_measure(G.state == PLAYING && !G.paused);
  if (!getenv("PIRATES_NO_HUD")) draw_hud(); /* dev: key art without the HUD */
}

typedef struct {int remaining,samples;} profile_wait;
static bool profile_ready(cw_reply *r,void *state) {
  profile_wait *p=state;
  if(--p->remaining>0)return false;
  char command[32];snprintf(command,sizeof command,"perf %d",p->samples);
  char *result=cw_debug_run(command);
  cw_reply_raw(r,result);cw_free(result);cw_free(p);
  return true;
}
static void cmd_profile(int argc,const char **argv,cw_reply *r) {
  int n=argc>1?atoi(argv[1]):300;
  if(n<1||n>590){cw_reply_error(r,"sample count must be 1..590");return;}
  profile_wait *p=cw_alloc(sizeof *p,CW_MEM_GAME);
  /* Read inside the running frame batch, excluding the command's initial
   * idle interval and the later HTTP round trip that retrieves the result. */
  p->samples=n;p->remaining=n+2;
  cw_reply_defer(r,profile_ready,p);
}

static void cmd_match(int argc, const char **argv, cw_reply *r) {
  int mode = argc > 1 ? atoi(argv[1]) : VOYAGE;
  int humans = argc > 2 ? atoi(argv[2]) : 1;
  humans = humans < 1 ? 1 : humans > 4 ? 4 : humans;
  reset_match(mode < 0 ? 0 : mode > 2 ? 2 : mode, (1 << humans) - 1);
  cw_reply_printf(r, "{\"mode\":%d,\"humans\":%d}", G.mode, G.humans);
}

/* a test screen: captain 0's banked wood, coin and iron, for the harbor's upgrade panel (visual inspection) */
static void cmd_stock(int argc,const char **argv,cw_reply *r) {
  ship *s=&G.ships[0];
  for(int k=0;k<3;k++)if(argc>1+k)s->stored[k==0?TIMBER:k==1?COIN:IRON]=atoi(argv[1+k]);
  cw_reply_printf(r,"{\"wood\":%d,\"coin\":%d,\"iron\":%d}",s->stored[TIMBER],s->stored[COIN],s->stored[IRON]);
}

static void cmd_sink(int argc,const char **argv,cw_reply *r) {
  int id=argc>1?atoi(argv[1]):0;
  if(id<0||id>=SHIPS){cw_reply_error(r,"ship index must be 0..5");return;}
  damage_ship(id,-1,999);
  cw_reply_printf(r,"{\"ship\":%d,\"hp\":%.0f,\"respawn\":%d}",id,G.ships[id].hp,G.ships[id].respawn);
}

/* a test screen: ship i's hull, hit from the side it would be (pirates_hull <ship> <hull> [side -1|1]), for looking at how damage shows */
static void cmd_hull(int argc,const char **argv,cw_reply *r) {
  int id=argc>1?atoi(argv[1]):0;
  if(id<0||id>=SHIPS||argc<3){cw_reply_error(r,"pirates_hull <ship 0..5> <hull> [side -1|1]");return;}
  G.ships[id].hp=clampf((float)atof(argv[2]),1,200);G.list_side[id]=argc>3&&atoi(argv[3])<0?-1.0f:1.0f;
  cw_reply_printf(r,"{\"ship\":%d,\"hp\":%.0f}",id,G.ships[id].hp);
}

static void cmd_effects(int argc,const char **argv,cw_reply *r) {
  (void)argc;(void)argv;
  cw_particles_stats stats;cw_particles_get_stats(cw_scene_app_tree(),&stats);
  cw_reply_printf(r,"{\"systems\":%d,\"capacity\":%d,\"draw_calls\":%d,\"smoke_alive\":%d,\"flash_alive\":%d,\"sea_alive\":%d,\"wood_alive\":%d,\"dust_alive\":%d,\"damage_smoke_alive\":%d}",
    stats.systems,stats.capacity,stats.draw_calls,cw_particles_alive(cannon_smoke),cw_particles_alive(cannon_flash),
    cw_particles_alive(impact_particles[0]),cw_particles_alive(impact_particles[1]),cw_particles_alive(impact_particles[2]),cw_particles_alive(damage_smoke_particles));
}

static void cmd_ai(int argc,const char **argv,cw_reply *r) {
  int id=argc>1?atoi(argv[1]):2;
  if(id<0||id>=SHIPS){cw_reply_error(r,"ship index must be 0..5");return;}
  ship *s=&G.ships[id];
  cw_reply_printf(r,"{\"ship\":%d,\"tick\":%d,\"x\":%.6f,\"z\":%.6f,\"yaw\":%.6f,\"speed\":%.6f,\"nav_x\":%.2f,\"nav_z\":%.2f,\"held\":%d,\"bank\":%d,\"shore_distance\":%.4f,\"hull_clear\":%d}",
    id,G.tick,s->x,s->z,s->yaw,s->speed,s->nav_x,s->nav_z,held_total(s),s->bank,coast_distance(s->x,s->z),hull_clear(s->x,s->z,s->yaw));
}

static void cmd_escape_test(int argc,const char **argv,cw_reply *r) {
  int map=active_map;active_map=0;build_map();reset_match(RACE,0);
  int id=argc>2&&atoi(argv[2])?0:2;
  ship *s=&G.ships[id];
  s->x=17.725109f;s->z=.358217f;s->yaw=-.832668f;s->speed=.271886f;
  if(id==0){s->x=2.943144f;s->z=18.608595f;s->yaw=2.690814f;s->speed=0;}
  s->sails=2;s->held[TIMBER]=3;s->ai_tick=0;
  for(int i=0;i<SHIPS;i++)if(i!=id) {
    G.ships[i].active=0;cw_node_set(G.ships[i].root,"visible",cw_val_bool(false));
  }
  if(argc>1&&atoi(argv[1])) {
    debug_update();cw_reply_printf(r,"{\"live\":true,\"ship\":%d}",id);return;
  }
  float sx=s->x,sz=s->z;int clear=1,ticks=0;
  for(;ticks<1800&&!s->bank;ticks++) {
    G.tick++;float turn=0,sail=0;int fire=0,dock=0;
    ai_input(id,&turn,&sail,&fire,&dock);move_ship(id,turn,sail);
    clear&=hull_clear(s->x,s->z,s->yaw);if(dock)bank_ship(id);
  }
  int bank=s->bank;float moved=sqrtf(dist2(sx,sz,s->x,s->z));
  active_map=map;build_map();for(int i=0;i<GOODS;i++)position_good(i);reset_match(VOYAGE,1);
  cw_reply_printf(r,"{\"ticks\":%d,\"distance\":%.3f,\"banked\":%d,\"hull_clear\":%d}",ticks,moved,bank,clear);
}

/* Visual fixture fires ordinary projectiles through the real collision path. */
static void cmd_impact(int argc,const char **argv,cw_reply *r) {
  int kind=argc>1?atoi(argv[1]):0;
  if(kind<0||kind>2){cw_reply_error(r,"impact kind: 0 sea, 1 hull, 2 shore");return;}
  reset_match(RACE,15);
  for(int i=1;i<SHIPS;i++) {
    G.ships[i].active=0;cw_node_set(G.ships[i].root,"visible",cw_val_bool(false));
  }
  ship *s=&G.ships[0];s->x=kind==2?-36:kind==1?7:16;s->z=kind==2?-2:kind==1?-18:-15;
  s->yaw=kind==0?PI*.5f:0;s->speed=0;s->cooldown=0;
  if(kind==1) {
    ship *t=&G.ships[1];t->active=1;t->x=15;t->z=-18;t->yaw=0;
    t->speed=0;t->protection=0;cw_node_set(t->root,"visible",cw_val_bool(true));
  }
  fire_ship(0,kind==0?-1:1);debug_update();
  cw_reply_printf(r,"{\"impact_kind\":%d,\"shots\":%d}",kind,G.shots_fired);
}

/* Runtime regression scenario: exercise the same damage/reset functions as play. */
static void cmd_salvage_test(int argc,const char **argv,cw_reply *r) {
  (void)argc;(void)argv;
  reset_match(RACE,15);
  ship *s=&G.ships[0];s->held[0]=1;s->held[1]=2;s->held[2]=3;
  s->bank=9;s->stored[0]=4;s->hull=1;s->sails=2;s->guns=1;
  damage_ship(0,5,999);
  int found[3]={0},base=0;
  for(int i=0;i<GOODS;i++)if(G.goods[i].active) {
    if(i<GOOD_SPAWNS)base++;else found[G.goods[i].kind]++;
  }
  int manifest=found[0]==1&&found[1]==2&&found[2]==3&&held_total(s)==0&&base==GOOD_SPAWNS;
  place_ship(0);
  int retained=s->bank==9&&s->stored[0]==4&&s->hull==1&&s->sails==2&&s->guns==1&&s->hp==118;
  reset_match(RACE,15);
  int clean=s->bank==0&&s->stored[0]==0&&s->hull==0&&s->sails==0&&s->guns==0;
  for(int i=0;i<GOODS;i++) {
    good *g=&G.goods[i];
    clean=clean&&g->active==(i<GOOD_SPAWNS)&&g->kind==i%3&&g->x==g->home_x&&g->z==g->home_z;
  }
  cw_reply_printf(r,"{\"manifest\":%d,\"retained\":%d,\"reset\":%d,\"salvage\":[%d,%d,%d]}",manifest,retained,clean,found[0],found[1],found[2]);
}

static void cmd_sw(int argc, const char **argv, cw_reply *r) {
  int32_t *sw = argc < 3 ? NULL : !strcmp(argv[1], "idle_stays") ? &G.idle_stays : !strcmp(argv[1], "voyage_hunt") ? &G.voyage_hunt :
                !strcmp(argv[1], "labels_stack") ? &G.labels_stack : !strcmp(argv[1], "flat_shots") ? &G.flat_shots :
                !strcmp(argv[1], "no_wind") ? &sw_no_wind : !strcmp(argv[1], "wind_blind") ? &sw_wind_blind : NULL;
  if (!sw) { cw_reply_error(r, "usage: sw idle_stays|voyage_hunt|labels_stack|flat_shots|no_wind|wind_blind <0|1>"); return; }
  *sw = sw == &sw_wind_blind ? atoi(argv[2]) : atoi(argv[2]) != 0;
  if (sw == &G.labels_stack) G.label_overlaps_max = 0;
  if (sw == &G.flat_shots) G.shot_peak = 0;
  cw_reply_printf(r, "{\"idle_stays\":%d,\"voyage_hunt\":%d,\"labels_stack\":%d}", G.idle_stays, G.voyage_hunt, G.labels_stack);
}

/* The wind, measured: (1) a ship held in open water at full sail on eight headings round the wind, its steady speed
 * on each (the points of sail); (2) an AI captain between two crates, the nearer one dead upwind and the other a
 * little farther downwind: it must sail for the downwind one (sw wind_blind 1: the nearer); (3) an AI captain
 * sailing between every pair of harbors, the ticks to arrive summed (sw wind_blind 1 routes by distance and must take
 * longer). Resets the match. */
static void cmd_wind_test(int argc,const char **argv,cw_reply *r) {
  int near_down=argc>1&&atoi(argv[1])!=0; /* 1: the nearer crate downwind (9 m) and the other upwind (11 m) */
  float dup=near_down?11:9,ddown=near_down?9:11;
  reset_match(RACE,0);
  for(int i=0;i<GOODS;i++){G.goods[i].active=0;G.goods[i].timer=-1;}
  ship *s=&G.ships[0];
  for(int i=1;i<SHIPS;i++)G.ships[i].active=0;
  /* open water: the nav cell farthest from any shore */
  int open=0;float far=1e9f;
  for(int i=0;i<NAV_N;i++)if(nav_open[i]){
    float x=i%NAV_W-NAV_X_ORIGIN,z=i/NAV_W-22,d=fmaxf(coast_distance(x,z),fmaxf(fabsf(x)-30,fabsf(z)-12));
    if(d<far){far=d;open=i;}
  }
  float ox=open%NAV_W-NAV_X_ORIGIN,oz=open/NAV_W-22;
  float speeds[8];
  for(int k=0;k<8;k++) {
    s->x=ox;s->z=oz;s->speed=0;s->yaw=cw_wrap_angle(wind_to()+k*PI/4);s->held[0]=s->held[1]=s->held[2]=0;s->sails=0;
    for(int t=0;t<420;t++){float yaw=s->yaw;move_ship(0,0,1);s->x=ox;s->z=oz;s->yaw=yaw;}
    speeds[k]=s->speed;
  }
  /* (2) the two crates: from a start in open water with open water 11 m upwind and downwind of it */
  float ux=-cw_sin(wind_to()),uz=-cw_cos(wind_to());
  float cx0=ox,cz0=oz,clear=1e9f;
  int home_region=nav_region[nav_nearest(PORT_X[0]+cw_sin(port_yaw(0))*6,PORT_Z[0]+cw_cos(port_yaw(0))*6)];
  for(int i=0;i<NAV_N;i++)if(nav_open[i]&&nav_region[i]==home_region) {
    float x=i%NAV_W-NAV_X_ORIGIN,z=i/NAV_W-22;
    if(nav_region[nav_nearest(x+ux*11,z+uz*11)]!=home_region||nav_region[nav_nearest(x-ux*11,z-uz*11)]!=home_region)continue;
    float c=fmaxf(coast_distance(x,z),fmaxf(coast_distance(x+ux*11,z+uz*11),coast_distance(x-ux*11,z-uz*11)));
    c=fmaxf(c,fmaxf(fabsf(x)-34,fabsf(z)-14));
    if(fabsf(x+ux*11)>38||fabsf(z+uz*11)>18||fabsf(x-ux*11)>38||fabsf(z-uz*11)>18)continue;
    if(c<clear){clear=c;cx0=x;cz0=z;}
  }
  ox=cx0;oz=cz0;
  s->x=ox;s->z=oz;s->speed=0;s->yaw=wind_to();s->ai_tick=0;
  good *up=&G.goods[0],*down=&G.goods[1];
  up->x=ox+ux*dup;up->z=oz+uz*dup;down->x=ox-ux*ddown;down->z=oz-uz*ddown;
  up->nav_cell=nav_nearest(up->x,up->z);down->nav_cell=nav_nearest(down->x,down->z);
  up->active=down->active=1;
  G.ai_good[0]=-1;
  {float turn=0,sail=0;int fire=0,dock=0;ai_input(0,&turn,&sail,&fire,&dock);}
  int picked=G.ai_good[0];
  up->active=down->active=0;
  /* each crate's real worth in time: the AI captain sailed from the same start to it, then home to its harbor's mouth */
  int crate_ticks[2];
  for(int k=0;k<2;k++) {
    good *g=k?down:up;
    s->x=ox;s->z=oz;s->speed=0;s->yaw=wind_to();s->ai_tick=0;
    int hc=nav_nearest(PORT_X[0]+cw_sin(port_yaw(0))*6,PORT_Z[0]+cw_cos(port_yaw(0))*6); /* the harbor's mouth, on open water */
    float hx=hc%NAV_W-NAV_X_ORIGIN,hz=hc/NAV_W-22;
    int t=0,leg=0;
    G.force_target=1;G.force_tx=g->x;G.force_tz=g->z;
    for(;t<3600;t++) {
      if(!leg&&dist2(s->x,s->z,g->x,g->z)<2.3f*2.3f){ /* within reach to take it aboard (step_goods) */leg=1;G.force_tx=hx;G.force_tz=hz;s->ai_tick=0;}
      if(leg&&dist2(s->x,s->z,hx,hz)<9)break;
      G.tick++;float turn=0,sail=0;int fire=0,dock=0;
      ai_input(0,&turn,&sail,&fire,&dock);move_ship(0,turn,sail);
    }
    G.force_target=0;crate_ticks[k]=leg?t:-1; /* -1: never reached the crate */
  }
  /* (3) harbor to harbor: the AI's steering toward the other harbor's mouth, until within 3 m (at most 40 s a leg) */
  int ticks=0,legs=0,stuck=0;char per[512];int pn=0;per[0]=0;
  for(int a=0;a<CAPTAINS;a++)for(int b=0;b<CAPTAINS;b++)if(a!=b) {
    float sx=PORT_X[a]+cw_sin(port_yaw(a))*6,sz=PORT_Z[a]+cw_cos(port_yaw(a))*6;
    float tx=PORT_X[b]+cw_sin(port_yaw(b))*6,tz=PORT_Z[b]+cw_cos(port_yaw(b))*6;
    int c=nav_nearest(sx,sz);s->x=c%NAV_W-NAV_X_ORIGIN;s->z=c/NAV_W-22;s->speed=0;s->ai_tick=0;
    int c2=nav_nearest(tx,tz);tx=c2%NAV_W-NAV_X_ORIGIN;tz=c2/NAV_W-22;
    if(nav_region[c]!=nav_region[c2])continue; /* no way by sea between these two harbors */
    s->yaw=cw_atan2(tx-s->x,tz-s->z);
    int t=0;
    G.force_target=1;G.force_tx=tx;G.force_tz=tz;
    for(;t<2400&&dist2(s->x,s->z,tx,tz)>9;t++) {
      G.tick++;float turn=0,sail=0;int fire=0,dock=0;
      ai_input(0,&turn,&sail,&fire,&dock);move_ship(0,turn,sail);
    }
    G.force_target=0;
    if(t>=2400)stuck++;
    ticks+=t;legs++;
    if(pn<(int)sizeof per-8)pn+=snprintf(per+pn,sizeof per-pn,"%s%d",legs>1?",":"",t);
  }
  reset_match(VOYAGE,1);
  cw_reply_printf(r,"{\"map\":%d,\"wind_to\":%.2f,\"speed_by_heading\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f],"
                  "\"running\":%.3f,\"beam\":%.3f,\"beating\":%.3f,\"nearer\":\"%s\",\"picked\":\"%s\",\"upwind_crate_ticks\":%d,\"downwind_crate_ticks\":%d,\"harbor_legs\":%d,\"harbor_ticks\":%d,\"stuck\":%d,\"legs\":[%s],"
                  "\"no_wind\":%d,\"wind_blind\":%d}",
                  active_map,wind_to(),speeds[0],speeds[1],speeds[2],speeds[3],speeds[4],speeds[5],speeds[6],speeds[7],
                  speeds[0],.5f*(speeds[2]+speeds[6]),speeds[4],near_down?"downwind":"upwind",picked==1?"downwind":picked==0?"upwind":"none",crate_ticks[0],crate_ticks[1],legs,ticks,stuck,per,
                  sw_no_wind,sw_wind_blind);
}

static void cmd_voyage_test(int argc,const char **argv,cw_reply *r) {
  int humans=argc>1?atoi(argv[1]):1;humans=humans<1?1:humans>4?4:humans;
  reset_match(VOYAGE,(1<<humans)-1);
  int quota=G.quota;float patrol_hp=G.ships[5].hp;
  G.ships[0].bank=quota;check_end();
  int provision=G.voyage_stage==1&&G.state==PLAYING;
  damage_ship(5,0,999);check_end();
  int blockade=G.voyage_stage==2&&G.state==PLAYING&&G.final_quota==2*quota;
  G.ships[0].bank=G.final_quota;check_end();
  int victory=G.state==RESULT&&G.winner==0;
  cw_reply_printf(r,"{\"players\":%d,\"quota\":%d,\"patrol_hp\":%.0f,\"provision\":%d,\"blockade\":%d,\"victory\":%d}",humans,quota,patrol_hp,provision,blockade,victory);
}

/* Advance the normal AI simulation for diagnosis without rendering every
 * intermediate frame. Water/particle appearance is not a visual replay. */
static void cmd_route_test(int argc,const char **argv,cw_reply *r) {
  (void)argc;(void)argv;
  int map=active_map;active_map=0;build_map();
  for(int i=0;i<GOODS;i++)position_good(i);
  reset_match(RACE,0);
  ship *s=&G.ships[0];int start=nav_nearest(-13,-5);
  s->x=start%NAV_W-NAV_X_ORIGIN;s->z=start/NAV_W-22;s->yaw=2;s->speed=0;s->held[TIMBER]=2;s->ai_tick=0;
  for(int i=1;i<SHIPS;i++)G.ships[i].active=0;
  for(int i=0;i<GOODS;i++){G.goods[i].active=0;G.goods[i].timer=-1;}
  good *blocked=&G.goods[15],*reachable=&G.goods[0];
  /* The wider hull no longer admits a navigation cell inside this cove.
   * Keep a deliberately stranded pickup there to test the reachability
   * filter, instead of silently relocating the fixture onto open water. */
  blocked->active=1;blocked->x=-15;blocked->z=-7;
  blocked->nav_cell=(-7+22)*NAV_W+(-15+NAV_X_ORIGIN);
  int cell=nav_nearest(-33,-8);reachable->nav_cell=cell;
  reachable->x=cell%NAV_W-NAV_X_ORIGIN;reachable->z=cell/NAV_W-22;reachable->active=1;
  int region=nav_region[nav_nearest(s->x,s->z)];
  int fixture=nav_region[blocked->nav_cell]!=region&&nav_region[reachable->nav_cell]==region
      &&dist2(s->x,s->z,blocked->x,blocked->z)<dist2(s->x,s->z,reachable->x,reachable->z);
  for(int t=0;t<1800 && !s->bank;t++) {
    G.tick++;float turn=0,sail=0;int fire=0,dock=0;
    ai_input(0,&turn,&sail,&fire,&dock);move_ship(0,turn,sail);step_goods();if(dock)bank_ship(0);
  }
  int bank=s->bank,unclaimed=blocked->active;
  active_map=map;build_map();for(int i=0;i<GOODS;i++)position_good(i);reset_match(VOYAGE,1);
  cw_reply_printf(r,"{\"unreachable_nearest_fixture\":%d,\"banked\":%d,\"unreachable_cargo_preserved\":%d}",fixture,bank,unclaimed);
}

/* Inspect the rendered meshes, not just the spawn coordinates. */
static void mesh_vertical_bounds(t3_object *o,float *lo,float *hi) {
  if(o->type==T3_MESH || o->type==T3_SKINNED_MESH) {
    t3_geometry *g=((t3_mesh *)o)->geometry;
    if(g) {
      t3_geometry_compute_bounding_box(g);
      t3_box3 b=g->bounding_box;
      for(int i=0;i<8;i++) {
        t3_vec3 p=t3_vec3_apply_mat4((t3_vec3){i&1?b.max.x:b.min.x,i&2?b.max.y:b.min.y,i&4?b.max.z:b.min.z},&o->matrix_world);
        *lo=fminf(*lo,p.y);*hi=fmaxf(*hi,p.y);
      }
    }
  }
  for(int i=0;i<o->child_count;i++)mesh_vertical_bounds(o->children[i],lo,hi);
}
static void cmd_placement(int argc,const char **argv,cw_reply *r) {
  int control=argc>1?atoi(argv[1]):0,control_done=0;
  int props=0,unsupported=0,cargo=0,floating=0,ships=0,overlaps=0,rocks=0,buried_rocks=0;
  float max_gap=-1000,control_bbox_gap=0;
  for(int i=0;i<cw_node_child_count(G.environment);i++) {
    cw_node n=cw_node_child(G.environment,i);const char *name=cw_node_name(n);
    if(strncmp(name,"Palm",4)&&strncmp(name,"ShoreRock",9)&&strncmp(name,"Undergrowth",11)&&strncmp(name,"Port",4))continue;
    t3_vec3 saved=cw_node3d_position(n);
    t3_quat saved_rotation=cw_node3d_quaternion(n);
    int moved=control==1 && props==0;
    if(moved)cw_node3d_set_position(n,(t3_vec3){saved.x,saved.y+20,saved.z});
    if(control==2 && !control_done && !strncmp(name,"ShoreRock",9)) {
      /* Tilt a rock on the flat outer seabed as a reproducible fixture: bury the lowest
       * corner 2 cm while leaving other points of the contact band exposed.
       * The former single-lowest-point check must miss this raised edge. */
      cw_node3d_set_rotation(n,(t3_vec3){.2f,0,0});
      cw_node3d_set_position(n,(t3_vec3){80,saved.y,80});
      float box=bounds_footprint_gap(n);
      cw_node3d_set_position(n,(t3_vec3){80,saved.y-box-.02f,80});
      control_bbox_gap=bounds_footprint_gap(n);moved=control_done=1;
    }
    float gap;
    if(!strncmp(name,"Port",4)) {
      /* The connected quay anchors the pier; its offshore piles are not plants. */
      t3_object *o=cw_node3d_object(n);t3_object_update_world_matrix(o,true,true);
      float lo=1e9f,hi=-1e9f;mesh_vertical_bounds(o,&lo,&hi);gap=lo;
    } else gap=footprint_gap(n);
    if(!strncmp(name,"ShoreRock",9)) {
      rocks++;buried_rocks+=rock_peak_clearance(cw_node3d_object(n))<.05f;
    }
    if(moved){cw_node3d_set_position(n,saved);cw_node3d_set_quaternion(n,saved_rotation);}
    max_gap=fmaxf(max_gap,gap);props++;unsupported+=gap>.001f;
  }
  for(int i=0;i<GOODS;i++)if(G.goods[i].active) {
    good *g=&G.goods[i];t3_object *o=cw_node3d_object(g->appearance[g->kind]);
    t3_object_update_world_matrix(o,true,true);float lo=1e9f,hi=-1e9f;mesh_vertical_bounds(o,&lo,&hi);
    float water=cw_water_height_at(G.water,g->x,g->z,cw_water_time(G.water));
    cargo++;floating+=lo>water+.15f || hi<water;
  }
  for(int i=0;i<SHIPS;i++)if(G.ships[i].active&&!G.ships[i].respawn) {
    ship *s=&G.ships[i];ships++;overlaps+=!hull_clear(s->x,s->z,s->yaw);
  }
  cw_reply_printf(r,"{\"props\":%d,\"unsupported\":%d,\"max_ground_gap\":%.3f,\"cargo\":%d,\"floating\":%d,\"ships\":%d,\"overlaps\":%d,\"control_bbox_gap\":%.3f,\"rocks\":%d,\"buried_rocks\":%d}",props,unsupported,max_gap,cargo,floating,ships,overlaps,control_bbox_gap,rocks,buried_rocks);
}

static void cmd_hull_test(int argc,const char **argv,cw_reply *r) {
  (void)argc;(void)argv;
  int map=active_map,cases=0,clear=1,blocked=0,escaped=0,spawn_clear=1;
  float dock_reverse[8]={0};
  for(int m=0;m<2;m++) {
    active_map=m;build_map();reset_match(RACE,0);
    for(int i=0;i<SHIPS;i++)spawn_clear&=hull_clear(G.ships[i].x,G.ships[i].z,G.ships[i].yaw);
    for(int i=0;i<CAPTAINS;i++) {
      ship *s=&G.ships[i];float a=port_yaw(i);
      int cell=nav_nearest(PORT_X[i]+cw_sin(a)*9,PORT_Z[i]+cw_cos(a)*9);
      s->x=cell%NAV_W-NAV_X_ORIGIN;s->z=cell/NAV_W-22;
      s->yaw=cw_atan2(PORT_X[i]-s->x,PORT_Z[i]-s->z);s->speed=0;s->respawn=0;
      if(!hull_clear(s->x,s->z,s->yaw)){clear=0;continue;}
      cases++;
      for(int t=0;t<240;t++){move_ship(i,0,1);clear&=hull_clear(s->x,s->z,s->yaw);}
      blocked+=s->speed<.5f;float x=s->x,z=s->z;
      for(int t=0;t<120;t++){move_ship(i,0,-1);clear&=hull_clear(s->x,s->z,s->yaw);}
      dock_reverse[m*4+i]=sqrtf(dist2(x,z,s->x,s->z));
      escaped+=dock_reverse[m*4+i]>1;
    }
    for(int k=0;k<3;k++)for(int j=0;j<4;j++) {
      const float (*poly)[2]=m?LAGOON[k]:COAST[k];
      int count=m?LAGOON_N[k]:COAST_N[k];
      float x=poly[j*count/4][0],z=poly[j*count/4][1];
      float gx=coast_distance(x+.1f,z)-coast_distance(x-.1f,z);
      float gz=coast_distance(x,z+.1f)-coast_distance(x,z-.1f);
      float len=sqrtf(gx*gx+gz*gz);if(len<.001f)continue;
      ship *s=&G.ships[0];s->x=x-gx/len*6;s->z=z-gz/len*6;
      s->yaw=cw_atan2(gx,gz);s->speed=0;s->respawn=0;
      if(!hull_clear(s->x,s->z,s->yaw))continue;
      cases++;
      for(int t=0;t<240;t++){move_ship(0,0,1);clear&=hull_clear(s->x,s->z,s->yaw);}
      blocked+=s->speed<.5f;
      float bx=s->x,bz=s->z,byaw=s->yaw,bspeed=s->speed;
      /* A turn into the shore must never sweep the stern through land. */
      for(int t=0;t<90;t++){move_ship(0,1,0);clear&=hull_clear(s->x,s->z,s->yaw);}
      /* Separately reverse out of the original bow-first contact. After a
       * quarter turn in a cove, the stern can legitimately face a beach. */
      s->x=bx;s->z=bz;s->yaw=byaw;s->speed=bspeed;
      for(int t=0;t<120;t++){move_ship(0,0,-1);clear&=hull_clear(s->x,s->z,s->yaw);}
      escaped+=dist2(s->x,s->z,bx,bz)>1;
    }
  }
  active_map=map;build_map();for(int i=0;i<GOODS;i++)position_good(i);reset_match(VOYAGE,1);
  cw_reply_printf(r,"{\"cases\":%d,\"no_penetration\":%d,\"blocked\":%d,\"reversed_clear\":%d,\"spawns_clear\":%d,\"dock_reverse\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]}",cases,clear,blocked,escaped,spawn_clear,dock_reverse[0],dock_reverse[1],dock_reverse[2],dock_reverse[3],dock_reverse[4],dock_reverse[5],dock_reverse[6],dock_reverse[7]);
}

static void cmd_simulate(int argc,const char **argv,cw_reply *r) {
  int ticks=argc>1?atoi(argv[1]):1500;ticks=ticks<1?1:ticks>21600?21600:ticks;
  reset_match(RACE,0);
  cw_reply_printf(r,"{\"checkpoints\":[");
  for(int t=1;t<=ticks;t++) {
    fixed_update(NULL,DT);
    if(t%1800==0 || t==ticks) {
      ship *s=&G.ships[0];
      cw_reply_printf(r,"%s{\"tick\":%d,\"bank\":%d,\"held\":%d,\"pos\":[%.3f,%.3f],\"waypoint\":[%.3f,%.3f],\"yaw\":%.3f,\"speed\":%.3f,\"shore\":%.3f,\"hp\":%.0f}",t>1800?",":"",G.tick,s->bank,held_total(s),s->x,s->z,s->nav_x,s->nav_z,s->yaw,s->speed,coast_distance(s->x,s->z),s->hp);
    }
  }
  int banked=0;for(int i=0;i<CAPTAINS;i++)banked+=G.ships[i].bank;
  cw_reply_printf(r,"],\"hash\":%u,\"winner\":%d,\"state\":%d,\"banked_all\":%d,\"goods\":%d}",G.hash,G.winner,G.state,banked,G.goods_taken);
}

static void cmd_recovery_test(int argc,const char **argv,cw_reply *r) {
  (void)argc;(void)argv;reset_match(RACE,15);
  ship *s=&G.ships[0];int initial=s->protection==0;
  damage_ship(0,5,999);int sunk=s->hp==0&&s->respawn==120;
  for(int i=0;i<120;i++)move_ship(0,0,0);
  int recovered=s->hp==100&&s->respawn==0&&s->protection==240;
  damage_ship(0,5,28);damage_ship(0,-1,28);
  int protected=s->hp==100;
  for(int i=0;i<239;i++)move_ship(0,0,0);
  damage_ship(0,5,28);int duration=s->hp==100&&s->protection==1;
  move_ship(0,0,0);damage_ship(0,5,28);int expired=s->hp==72&&s->protection==0;
  damage_ship(0,5,999);for(int i=0;i<120;i++)move_ship(0,0,0);
  fire_ship(0,1);damage_ship(0,5,28);int attack=s->protection==0&&s->hp==72&&G.shots_fired==3;
  reset_match(VOYAGE,1);debug_update();
  cw_reply_printf(r,"{\"initial_unshielded\":%d,\"sunk\":%d,\"recovered\":%d,\"protected\":%d,\"duration\":%d,\"expired\":%d,\"firing_ends_protection\":%d}",initial,sunk,recovered,protected,duration,expired,attack);
}

static void cmd_eruption_test(int argc,const char **argv,cw_reply *r) {
  (void)argc;(void)argv;
  int map=active_map;active_map=0;build_map();reset_match(RACE,15);
  start_eruption();
  int safe=1;
  for(int k=0;k<ROCKFALLS;k++)for(int p=0;p<CAPTAINS;p++)
    safe=safe&&dist2(eruption.x[k],eruption.z[k],PORT_X[p],PORT_Z[p])>9.2f*9.2f;
  for(int i=0;i<SHIPS;i++){G.ships[i].x=-40;G.ships[i].z=20;}
  G.ships[0].x=eruption.x[0];G.ships[0].z=eruption.z[0];
  G.ships[1].x=eruption.x[0]+3.3f;G.ships[1].z=eruption.z[0];
  for(int i=0;i<ERUPTION_WARNING-1;i++)step_eruption();
  int warning=G.ships[0].hp==100&&eruption.impacts==0&&eruption.warning==1;
  step_eruption();
  int damage=G.ships[0].hp==72&&G.ships[1].hp==100&&eruption.impacts==ROCKFALLS;
  step_eruption();int once=G.ships[0].hp==72&&eruption.impacts==ROCKFALLS;
  reset_match(RACE,15);int reset=eruption.warning==0&&eruption.count==0&&eruption.impacts==0;
  int escapes=0;
  for(int volley=0;volley<2;volley++)for(int zone=0;zone<ROCKFALLS;zone++)for(int heading=0;heading<4;heading++) {
    reset_match(RACE,1);eruption.count=volley;start_eruption();
    ship *s=&G.ships[0];s->x=eruption.x[zone];s->z=eruption.z[zone];s->yaw=heading*PI*.5f;s->speed=0;s->ai_tick=0;
    for(int tick=0;tick<ERUPTION_WARNING;tick++) {
      float turn=0,sail=0;int fire=0,dock=0;
      ai_input(0,&turn,&sail,&fire,&dock);move_ship(0,turn,sail);step_eruption();
    }
    escapes+=s->hp==100;
  }
  reset_match(RACE,15);
  active_map=1;G.tick=1080;step_eruption();int lagoon=eruption.warning==0&&eruption.count==0;
  active_map=map;build_map();for(int i=0;i<GOODS;i++)position_good(i);reset_match(VOYAGE,1);
  cw_reply_printf(r,"{\"harbors_safe\":%d,\"warning\":%d,\"damage_and_escape\":%d,\"single_impact\":%d,\"reset\":%d,\"lagoon_quiet\":%d,\"ai_escapes\":%d,\"ai_scenarios\":24}",safe,warning,damage,once,reset,lagoon,escapes);
}

/* the plants and rocks on the island shade it in the bake: each of their meshes joins it as a one-instance mesh
 * (cartwheel/lightmap.h: an instanced mesh under a STATIC MeshInstance3D occludes and bounces light but gets no
 * lightmap of its own, so nothing of them is in the .cwlm and nothing needs them at run time) */
static void bake_occluders(cw_node holder,t3_object *o,int *k) {
  if(!o->visible)return;
  if(o->type==T3_MESH && ((t3_mesh *)o)->geometry && ((t3_mesh *)o)->material_count) {
    t3_mesh *m=(t3_mesh *)o;
    t3_instanced_mesh *im=t3_instanced_mesh_new(m->geometry,m->materials[0],1);
    t3_instanced_mesh_set_matrix_at(im,0,&m->base.matrix_world);
    char name[32];snprintf(name,sizeof name,"Occluder%d",(*k)++);
    cw_node n=cw_node_new("MeshInstance3D",name);cw_node_add_child(holder,n);
    cw_node_set(n,"gi_mode",cw_val_enum("STATIC"));
    cw_mesh3d_set_procedural(n,&im->mesh);t3_release(im);
  }
  for(int i=0;i<o->child_count;i++)bake_occluders(holder,o->children[i],k);
}
/* pirates_bake <map> <file> [texel density]: bake a map's light and write the .cwlm (native; the shipped files are
 * games/pirates/assets/lightmaps/shoals.cwlm and lagoon.cwlm, baked at High's full scenery detail) */
static void cmd_bake(int argc,const char **argv,cw_reply *r) {
  if(argc<3){cw_reply_error(r,"pirates_bake <map 0|1> <file.cwlm> [texel density] [occluders 1|0] [show 0|1]");return;}
  int map=active_map;active_map=atoi(argv[1])?1:0;
  G.no_lightmap=1;build_map();
  cw_tree *tree=cw_scene_app_tree();
  cw_tree_pre_render(tree,1);
  cw_node holder=cw_node_new("Node3D","BakeOccluders");cw_node_add_child(G.environment,holder);
  int k=0;
  for(int i=0;i<G.n_scenery_lod;i++) {
    cw_gltf_instance_set_lod(G.scenery_lod[i],0);
    t3_object_update_world_matrix(G.scenery_lod[i],true,true);
    if(argc<=4||atoi(argv[4]))bake_occluders(holder,G.scenery_lod[i],&k);
  }
  cw_tree_pre_render(tree,1);
  cw_lightmap_settings s=cw_lightmap_settings_default();
  s.texel_density=argc>3?(float)atof(argv[3]):8; /* 4 leaves the harbors' thin planks and roofs too few texels: they bake black */
  s.max_size=2048;s.samples=96;s.bounces=2;s.probe_spacing=0;
  s.direct_samples=24;s.shadow_softness=.035f;s.denoise=2;
  double t0=cw_time();
  cw_lightmap_data *d=cw_lightmap_bake(tree,G.world,&s);
  double secs=cw_time()-t0;
  bool ok=d&&cw_lightmap_save(d,argv[2]);
  cw_lightmap_info info={0};if(d)cw_lightmap_get_info(d,&info);
  size_t len=0;if(d)cw_lightmap_bytes(d,&len);
  if(argc>5&&atoi(argv[5])&&d) {
    /* show this bake on the map just baked (looking at a bake before it ships) */
    build_map();for(int i=0;i<GOODS;i++)position_good(i);reset_match(RACE,0);
    cw_lightmap_free(G.lightmap);G.lightmap=d;
    cw_lightmap_apply_shaded(tree,G.world,d,1,CW_LIGHTMAP_BAKED);apply_graphics();G.no_lightmap=0;
  } else {
    cw_lightmap_free(d);
    G.no_lightmap=0;active_map=map;build_map();for(int i=0;i<GOODS;i++)position_good(i);reset_match(VOYAGE,1);
  }
  cw_reply_printf(r,"{\"saved\":%d,\"seconds\":%.1f,\"occluders\":%d,\"meshes\":%d,\"triangles\":%d,\"texels\":%d,\"width_total\":%d,\"bytes\":%zu,\"hash\":\"%016llx\"}",
    ok,secs,k,info.meshes,info.triangles,info.texels,info.width_total,len,(unsigned long long)info.hash);
}

static void cmd_map(int argc,const char **argv,cw_reply *r) {
  int requested=argc>1?atoi(argv[1]):0;active_map=requested==1?1:0;
  build_map();for(int i=0;i<GOODS;i++)position_good(i);
  reset_match(G.mode,G.joined?G.joined:1);
  cw_reply_printf(r,"{\"map\":%d}",active_map);
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

/* Graphics settings. Auto is the default: the engine steps from High down
 * through Medium and Low to Very Low while frames take too long, so a handheld
 * drops detail and a desktop keeps the full picture. Below High the engine
 * draws the scene without 4x multisampling (its FXAA or SMAA pass smooths
 * edges instead). The island, the harbors and the plants never draw into the
 * sun's shadow map: their light and shadows are baked (static_mesh above). The
 * Sun shadows setting keeps a real-time shadow map for what moves (ships, flags,
 * cargo) on Medium and up; on Very Low and Low there is none and the ships take
 * a blob shadow on the water. The level reached is saved. */
/* text: the Text size and Text background settings (cw_text_scale_saved); an older 8-byte save reads it as 0,
 * never chosen */
typedef struct { uint32_t magic, gfx; uint8_t text, reserved[3]; } save_data;
static save_data SAVE;
enum { SAVE_MAGIC = 0x50495231u };
static const char *const SCENERY_SHADOW_VALUES[] = { "Off", "On" };
/* Scenery detail: the palms, undergrowth and shore rocks at cwpack's quarter, half or full triangle count (on Very Low the
 * island's 225,000 triangles are a third of the frame there) */
static const char *const SCENERY_DETAIL_VALUES[] = { "Coarse", "Reduced", "Full" };
static int scenery_lod_now(void) { return G.lever_scenery_detail >= 0 ? 2 - cw_gfx_get(G.lever_scenery_detail) : 0; }
static void scenery_lod_add(t3_object *o) {
  if (G.n_scenery_lod < (int)(sizeof G.scenery_lod / sizeof G.scenery_lod[0])) G.scenery_lod[G.n_scenery_lod++] = o;
  cw_gltf_instance_set_lod(o, scenery_lod_now());
}

static void set_cast_shadow(t3_object *o, bool on) {
  if (o->type == T3_MESH || o->type == T3_SKINNED_MESH) o->cast_shadow = on;
  for (int i = 0; i < o->child_count; i++) set_cast_shadow(o->children[i], on);
}
static void apply_graphics(void) {
  for (int i = 0; i < G.n_scenery_lod; i++) cw_gltf_instance_set_lod(G.scenery_lod[i], scenery_lod_now());
  /* With the map's light baked, the island, the harbors and the plants on them are in the lightmap's shadows, so
   * none of them draws into the sun's shadow map; the sun's real-time shadow is for what moves (the ships, the
   * cargo, the flags), and on Very Low and Low the ships take a blob shadow on the water instead of it. */
  bool sun = cw_gfx_get(G.lever_sun_shadows) > 0;
  for (int i = 0; i < G.n_scenery_lod; i++) set_cast_shadow(G.scenery_lod[i], !G.lightmap);
  for (int i = 0; i < 1 + CAPTAINS; i++) if (G.big_scenery[i]) G.big_scenery[i]->cast_shadow = !G.lightmap && sun;
  cw_node light = cw_node_get(G.world, "Sun");
  if (cw_node_valid(light)) cw_node_set(light, "shadow", cw_val_bool(sun || !G.lightmap));
  G.blob_shadows = !sun && G.lightmap;
  bool streaks = cw_quality_get() > CW_QUALITY_VERY_LOW;
  if (cw_node_valid(G.wind_streaks)) { cw_node_set(G.wind_streaks, "visible", cw_val_bool(streaks)); cw_node_set(G.wind_streaks, "emitting", cw_val_bool(streaks)); }
}
/* the text settings changed: keep them (0 graphics: those stay never chosen) */
static void text_settings_save(void) {
  if (SAVE.magic != SAVE_MAGIC) { SAVE.magic = SAVE_MAGIC; SAVE.gfx = 0; }
  SAVE.text = cw_text_scale_saved();
  cw_save_commit();
}
static void quality_changed(cw_quality q, void *user) {
  (void)q; (void)user;
  apply_graphics();
  uint32_t word = cw_quality_saved();
  if (word != SAVE.gfx) { SAVE.magic = SAVE_MAGIC; SAVE.gfx = word; cw_save_commit(); }
}

static void init(void *user) {
  (void)user;
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("pirates: input.cfg had errors");
  G.turn_l = cw_action_find("turn_left"); G.turn_r = cw_action_find("turn_right");
  G.sail_u = cw_action_find("sail_up"); G.sail_d = cw_action_find("sail_down");
  G.aim_l = cw_action_find("aim_left"); G.aim_r = cw_action_find("aim_right");
  G.fire_l = cw_action_find("fire_left"); G.fire_r = cw_action_find("fire_right"); G.dock = cw_action_find("dock");
  G.up_hull = cw_action_find("upgrade_hull"); G.up_sails = cw_action_find("upgrade_sails");
  G.up_guns = cw_action_find("upgrade_guns"); G.start = cw_action_find("start");
  G.map_next=cw_action_find("map_next");
  G.pause=cw_action_find("game_pause");G.leave=cw_action_find("leave_match");
  G.text_size=cw_action_find("text_size");G.text_background=cw_action_find("text_background");
  G.mode_l = cw_action_find("mode_left"); G.mode_r = cw_action_find("mode_right");
  G.font = cw_ui_font_sans();
  sound_init();
  G.lever_sun_shadows = cw_quality_lever_add("Sun shadows", SCENERY_SHADOW_VALUES, 2,
                                              (cw_quality_lever_presets){ .very_low = 0, .low = 0, .medium = 1, .high = 1, .ultra = 1 });
  G.lever_scenery_detail = cw_quality_lever_add("Scenery detail", SCENERY_DETAIL_VALUES, 3,
                                                (cw_quality_lever_presets){ .very_low = 0, .low = 1, .medium = 2, .high = 2, .ultra = 2 });
  cw_quality_auto(true, 0);
  if (SAVE.magic == SAVE_MAGIC && SAVE.gfx) cw_quality_restore(SAVE.gfx);
  if (SAVE.magic == SAVE_MAGIC) cw_text_scale_restore(SAVE.text);
  cw_quality_on_change(quality_changed, NULL);
  G.no_lightmap = getenv("PIRATES_NO_LIGHTMAP") != NULL; /* dev: the maps lit in real time, for a before and after */
  setup_world();
  G.post = cw_node_get(G.world, "Post");
  apply_graphics();
  G.state = TITLE; G.mode = VOYAGE; G.joined = 1; G.humans = 1;
  cw_debug_command("pirates_test_route","Test reachable cargo selection from the stalled cove approach; resets match",cmd_route_test);
  cw_debug_command("pirates_placement","Inspect rendered prop support, cargo water contact and hull clearance",cmd_placement);
  cw_debug_command("pirates_test_hull","Check shore approach, rotation, reverse escape and spawn clearance on both maps; resets match",cmd_hull_test);
  cw_debug_command("pirates_stock","pirates_stock <wood> <coin> <iron>: captain 0's stored cargo, for inspecting the harbor panel",cmd_stock);
  cw_debug_command("pirates_sink","Sink ship 0..5 through normal damage rules for visual inspection",cmd_sink);
  cw_debug_command("pirates_hull","pirates_hull <ship> <hull> [side]: set a ship's hull, for looking at how damage shows",cmd_hull);
  cw_debug_command("pirates_effects","Read particle pool capacity, draw counts and active combat particles",cmd_effects);
  cw_debug_command("pirates_ai","Inspect a ship's steering waypoint and shoreline clearance",cmd_ai);
  cw_debug_command("pirates_test_escape","Check shoreline escape: first argument 1 starts live; second argument 1 selects P1 stop-and-turn case",cmd_escape_test);
  cw_debug_command("pirates_impact","Fire a real broadside into sea (0), hull (1), or shore (2); resets match",cmd_impact);
  cw_debug_command("pirates_simulate","Reset AI race and advance 1..21600 simulation ticks without intermediate rendering",cmd_simulate);
  cw_debug_command("pirates_test_recovery","Check respawn protection, expiry and firing; resets match",cmd_recovery_test);
  cw_debug_command("pirates_test_eruption","Test warning, damage, escape, harbor safety and map isolation; resets match",cmd_eruption_test);
  cw_debug_i32("pirates.eruption_warning",&eruption.warning);
  cw_debug_i32("pirates.eruptions",&eruption.count);
  cw_debug_i32("pirates.rock_impacts",&eruption.impacts);
  cw_debug_i32("pirates.map",&active_map);
  cw_debug_command("pirates_bake","pirates_bake <map> <file.cwlm> [texel density]: bake a map's light (native)",cmd_bake);
  cw_debug_command("pirates_map","Select map 0 volcanic / 1 lagoon; resets match",cmd_map);
  cw_debug_i32("pirates.state", &G.state);
  cw_debug_i32("pirates.paused", &G.paused);
  cw_debug_command("bind", "bind <action> <bindings>: replace an action's bindings", cmd_bind);
  cw_debug_field("pirates.protection", G.dbg_protection, CW_DBG_I32, CAPTAINS);
  cw_debug_field("pirates.cooldown", G.dbg_cooldown, CW_DBG_I32, CAPTAINS);
  cw_debug_i32("pirates.mode", &G.mode);
  cw_debug_i32("pirates.humans", &G.humans);
  cw_debug_i32("pirates.autopilot", &G.autopilot);
  cw_debug_i32("pirates.tick", &G.tick);
  cw_debug_i32("pirates.time", &G.time_left);
  cw_debug_i32("pirates.quota", &G.quota);
  cw_debug_i32("pirates.voyage_stage", &G.voyage_stage);
  cw_debug_i32("pirates.objective_notice_ticks", &G.objective_notice_ticks);
  cw_debug_i32("pirates.patrol_defeats", &G.patrol_defeats);
  cw_debug_i32("pirates.final_quota", &G.final_quota);
  cw_debug_i32("pirates.winner", &G.winner);
  for(int i=0;i<CAPTAINS;i++) {
    static const char *names[]={"pirates.score0","pirates.score1","pirates.score2","pirates.score3"};
    cw_debug_i32(names[i],&G.ships[i].bank);
  }
  cw_debug_i32("pirates.supply_wood",&G.ships[0].stored[TIMBER]);
  cw_debug_i32("pirates.supply_coin",&G.ships[0].stored[COIN]);
  cw_debug_i32("pirates.supply_iron",&G.ships[0].stored[IRON]);
  cw_debug_i32("pirates.alert", &G.alert);
  cw_debug_i32("pirates.label_overlaps", &G.label_overlaps);
  cw_debug_i32("pirates.label_overlaps_max", &G.label_overlaps_max);
  cw_debug_f32("pirates.shot_peak", &G.shot_peak);
  cw_debug_f32("pirates.shot_aim_error", &G.shot_aim_error);
  cw_debug_field("pirates.cargo_notice_ticks",G.cargo_notice_ticks,CW_DBG_I32,CAPTAINS);
  cw_debug_field("pirates.hit_ticks",G.hit_ticks,CW_DBG_I32,SHIPS);
  cw_debug_field("pirates.hit_amount",G.hit_amount,CW_DBG_I32,SHIPS);
  cw_debug_field("pirates.canvas_weights",G.canvas_weights,CW_DBG_F32,SHIPS);
  cw_debug_i32("pirates.shots", &G.shots_fired);
  cw_debug_i32("pirates.goods", &G.goods_taken);
  cw_debug_field("pirates.bank", G.dbg_bank, CW_DBG_I32, CAPTAINS);
  cw_debug_field("pirates.held", G.dbg_held, CW_DBG_I32, CAPTAINS);
  cw_debug_field("pirates.hp", G.dbg_hp, CW_DBG_I32, CAPTAINS);
  cw_debug_field("pirates.pos", G.dbg_pos, CW_DBG_I32, CAPTAINS * 2);
  cw_debug_u32("pirates.hash", &G.hash);
  cw_debug_u32("pirates.ripples", &G.ripple_hash);
  cw_debug_f32("pirates.gpu_ms", &G.gpu_ms);
  cw_debug_command("pirates_test_wind","pirates_test_wind [1: the nearer crate downwind]: speed by heading, the AI's choice of two crates against each one's measured time there and home, and harbor-to-harbor sailing; resets match",cmd_wind_test);
  cw_debug_command("pirates_test_voyage","Test three voyage stages with 1..4 players; ends match",cmd_voyage_test);
  cw_debug_command("pirates_test_salvage","Run sinking, respawn and restart regression; resets match",cmd_salvage_test);
  cw_debug_command("sw", "sw idle_stays|voyage_hunt|labels_stack|flat_shots <0|1>: test switches (an idle captain stays put; the patrol hunts a voyage captain from the start; ship labels drawn at their ships with nothing kept clear; cannonballs skim the water)", cmd_sw);
  cw_debug_command("pirates_match", "pirates_match <0 voyage, 1 race, 2 teams> <1..4 humans>", cmd_match);
  cw_debug_command("pirates_profile","Collect N continuous rendered frames before replying with perf; default 300, max 590",cmd_profile);
#ifdef PIRATES_AUTO
  reset_match(RACE, 0);
#endif
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc; (void)argv;
  static cw_app_desc app = { .title = "Pirates", .width = 1920, .height = 1080, .debug = true,
                             .init = init, .shutdown = sound_shutdown, .fixed_update = fixed_update, .update = update, .render = render,
                             .save = &SAVE, .save_size = sizeof SAVE };
  if (getenv("PIRATES_SMALL")) app.width = 480, app.height = 270;
  return cw_scene_app("main.cwscene", &app);
}
