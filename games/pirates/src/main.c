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
  int owner, life, active;
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
  int32_t dbg_bank[CAPTAINS], dbg_held[CAPTAINS], dbg_hp[CAPTAINS], dbg_protection[CAPTAINS], dbg_cooldown[CAPTAINS], dbg_pos[CAPTAINS * 2];
  uint32_t hash, ripple_hash;
  char cargo_notice[CAPTAINS][80];
  int32_t cargo_notice_ticks[CAPTAINS];
  int32_t hit_ticks[SHIPS], hit_amount[SHIPS];
  float hit_previous_hp[SHIPS];
  float canvas_weights[SHIPS];
  float gpu_ms;
  cw_node post;
  int lever_scenery_shadows, lever_scenery_detail;
  /* the palms, undergrowth and shore rocks: the Scenery detail setting draws them at a coarser LOD (cwpack's 1/2 and 1/4) */
  t3_object *scenery_lod[512];
  int n_scenery_lod;
  t3_object *big_scenery[1 + CAPTAINS]; /* the coast and the four port buildings: the Island shadows setting */
  cw_action turn_l, turn_r, sail_u, sail_d, aim_l, aim_r, fire_l, fire_r, dock, up_hull, up_sails, up_guns, start, mode_l, mode_r, map_next, pause, leave;
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
static void nav_waypoint(ship *s,float tx,float tz) {
  static int next[NAV_N],queue[NAV_N];
  int start=nav_nearest(s->x,s->z),goal=nav_nearest(tx,tz);
  for(int i=0;i<NAV_N;i++)next[i]=-1;
  int head=0,tail=0;queue[tail++]=goal;next[goal]=goal;
  while(head<tail && next[start]<0) {
    int cur=queue[head++],x=cur%NAV_W,z=cur/NAV_W;
    for(int dz=-1;dz<=1;dz++)for(int dx=-1;dx<=1;dx++) {
      if(!dx&&!dz)continue;
      int xx=x+dx,zz=z+dz;if(xx<0||xx>=NAV_W||zz<0||zz>=NAV_H)continue;
      int n=zz*NAV_W+xx;
      if(!nav_open[n]||next[n]>=0)continue;
      if(dx&&dz&&(!nav_open[z*NAV_W+xx]||!nav_open[zz*NAV_W+x]))continue;
      next[n]=cur;queue[tail++]=n;
    }
  }
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
static void make_coast(void) {
  G.n_scenery_lod = 0; /* the environment is built again: its models are new */
  cw_node n = cw_node_new("Node3D", "Coast"); cw_node_add_child(G.environment,n);
  t3_object *model = cw_gltf_instantiate(cw_res_load(active_map?"res://models/lagoon.glb":"res://models/coast.glb", CW_RES_GLTF));
  if (model) { model_shadows(model); t3_object_add(cw_node3d_object(n),model); t3_release(model); }
  G.big_scenery[0] = model;
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
  cw_node n = cw_node_new("Node3D", "Port");
  cw_node_add_child(G.environment, n);
  cw_node3d_set_position(n, (t3_vec3){ PORT_X[i], 0, PORT_Z[i] });
  cw_node3d_set_rotation(n,(t3_vec3){0,port_yaw(i),0});
  static const char *ports[]={"res://models/port-fort.glb","res://models/port-watchtower.glb","res://models/port-fishery.glb","res://models/port-shipyard.glb"};
  t3_object *model=cw_gltf_instantiate(cw_res_load(ports[i],CW_RES_GLTF));
  if(model){model_shadows(model);t3_object_add(cw_node3d_object(n),model);t3_release(model);}
  G.big_scenery[1 + i] = model;
  part(n,"Flagpole","CYLINDER",1.8f,2,0,.09f,4,.09f,0x775434ff,true);
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

static cw_node make_ship_model(int i) {
  ship *s = &G.ships[i];
  cw_node n = cw_node_new("Node3D", i < CAPTAINS ? "Captain" : i == 4 ? "Merchant" : "Patrol");
  cw_node_add_child(G.world, n);
  t3_object *model = cw_gltf_instantiate(cw_res_load("res://models/galleon.glb", CW_RES_GLTF));
  if (model) {
    model->quaternion = t3_quat_from_euler((t3_euler){0,PI,0,T3_YXZ});
    model_shadows(model);
    s->canvas=find_canvas(model);
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
  cw_node_set(eruption.plume,"visible",cw_val_bool(!active_map));
  cw_node_set(eruption.plume,"emitting",cw_val_bool(!active_map));
  for(int i=0;i<ROCKFALLS;i++) {
    cw_node n=cw_node_new("Node3D","VolcanicRock");cw_node_add_child(G.environment,n);
    t3_object *model=cw_gltf_instantiate(cw_res_load("res://models/rock0.glb",CW_RES_GLTF));
    if(model){model_shadows(model);t3_object_add(cw_node3d_object(n),model);t3_release(model);}
    cw_node3d_set_scale(n,(t3_vec3){.38f,.38f,.38f});
    cw_node_set(n,"visible",cw_val_bool(false));eruption.rocks[i]=n;
  }
  for(int i=0;i<CAPTAINS;i++)make_port(i);
  /* The water rectangle is 144 x 108 metres. Ease the ocean swells down
   * over the last eight metres of shallow water; CPU buoyancy shares it. */
  float *shelter=malloc(145*109*sizeof(float));
  if(shelter) {
    for(int z=0;z<109;z++)for(int x=0;x<145;x++) {
      float t=clampf(-coast_distance(x-72,z-54)/8.0f,0,1);
      shelter[z*145+x]=t*t*(3-2*t);
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

static void reset_match(int mode, int joined_mask) {
  G.paused=0;cw_tree_set_paused(cw_scene_app_tree(),false);
  effects_reset(G.world);
  int humans = 0;
  for (int i = 0; i < CAPTAINS; i++) humans += !!(joined_mask & (1 << i));
  G.mode = mode; G.humans = humans; G.state = PLAYING; G.tick = 0; G.alert = 0;
  memset(G.cargo_notice_ticks,0,sizeof G.cargo_notice_ticks);
  memset(G.hit_ticks,0,sizeof G.hit_ticks);
  G.time_left = mode == VOYAGE ? 60 * 12 * 60 : 60 * 6 * 60;
  G.quota = 10 + (humans > 0 ? humans - 1 : 0) * 7;
  G.voyage_stage=0;G.patrol_defeats=0;G.final_quota=0;
  G.objective_notice_ticks=mode==VOYAGE?240:0;
  eruption.warning=eruption.count=eruption.impacts=0;
  for(int i=0;i<ROCKFALLS;i++)cw_node_set(eruption.rocks[i],"visible",cw_val_bool(false));
  G.winner = -1; G.shots_fired = 0; G.goods_taken = 0;
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
    s->bank = s->hull = s->sails = s->guns = s->damage_done = s->sinks = 0;
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
  /* Home anchorages give captains room to bank, repair and learn controls. */
  if(owner>=CAPTAINS && target<CAPTAINS &&
     harbor_distance2(target,G.ships[target].x,G.ships[target].z)<49)return 0;
  return 1;
}

static void fire_ship(int id, int side) {
  ship *s = &G.ships[id];
  if (s->cooldown || s->respawn || !s->active) return;
  s->protection=0; /* Firing ends safe departure immediately. */
  float sy = cw_sin(s->yaw), cy = cw_cos(s->yaw);
  float sx = side * cy, sz = -side * sy;
  for (int n = -1; n <= 1; n++) {
    int slot = -1;
    for (int k = 0; k < SHOTS; k++) if (!G.shots[k].active) { slot = k; break; }
    if (slot < 0) break;
    shot *b = &G.shots[slot];
    b->active = 1; b->owner = id; b->life = 105;
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
    for (int k = 0; k < GOODS; k++) if (G.goods[k].active && nav_region[G.goods[k].nav_cell]==region) {
      float d = dist2(s->x, s->z, G.goods[k].x, G.goods[k].z);
      if (d < best) { best = d; found = k; }
    }
    if (found >= 0) tx = G.goods[found].x, tz = G.goods[found].z;
    else { tx=PORT_X[id];tz=PORT_Z[id]; }
  }
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
  float wind = 1.0f + 0.14f * cw_cos(s->yaw + 0.8f);
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
    } else {
      ai_input(i, &turn, &sail, &fire, &dock);
    }
    move_ship(i, turn, sail);
    if (fire) fire_ship(i, fire);
    if (i < CAPTAINS && !s->respawn) {
      if (dock) bank_ship(i);
      if (s->human) {
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
    cw_node3d_set_rotation(G.harbor_flags[i],(t3_vec3){.12f*cw_sin((float)wt*3+i),.2f*cw_sin((float)wt*1.7f+i),0});
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
    cw_node3d_set_position(s->root, (t3_vec3){ s->x, w.height + 0.15f, s->z });
    /* Resolve the surface slope into the ship's local axes before yawing. */
    float sy = cw_sin(s->yaw), cy = cw_cos(s->yaw);
    cw_node3d_set_rotation(s->root, (t3_vec3){ -(w.normal.z * cy + w.normal.x * sy) * 0.65f,
                                              s->yaw, (w.normal.x * cy - w.normal.z * sy) * 0.65f });
    cw_node3d_set_rotation(s->flag,(t3_vec3){.12f*cw_sin((float)wt*3+i),.2f*cw_sin((float)wt*1.7f+i),0});
    G.canvas_weights[i]=s->canvas?.5f+.35f*cw_sin((float)wt*1.7f+i)+.12f*cw_sin((float)wt*3.1f+i*.7f):-1;
    if(s->canvas)s->canvas->morph_influences[0]=G.canvas_weights[i];
  }
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
    cw_node3d_set_position(b->node, (t3_vec3){ b->x, h + 0.7f, b->z });
  }
}

static void line(const char *str, float x, float y, float size, cw_color color, float width, cw_text_align align) {
  cw_r2d_text(G.font, str, cw_v2(x, y), &(cw_text_style){ .size = size, .color = color, .wrap_width = width, .align = align });
}
/* a line with button prompts in it (cartwheel/prompt.h markup: "{dock} bank cargo"), for one captain (-1: every
 * seat, on the shared screens), in the box line() would use */
static void pline(const char *str, int player, float x, float y, float size, cw_color color, float width, cw_text_align align) {
  float px = align == CW_ALIGN_CENTER ? x + width * 0.5f : align == CW_ALIGN_RIGHT ? x + width : x;
  cw_prompt_text(G.font, str, player, cw_v2(px, y), size, color, align);
}

static void draw_hud(void) {
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float scale = cs.y / 1080.0f, W = cs.x, H = cs.y;
  char buf[160];
  const char *mode = G.mode == VOYAGE ? "COOPERATIVE VOYAGE" : G.mode == RACE ? "TREASURE RACE" : "TEAM TREASURE RACE";
  if (G.state == TITLE) {
    cw_r2d_rect(cw_rect_make(0, H * 0.17f, W, H * 0.76f), cw_rgba(0.015f, 0.045f, 0.07f, 0.77f));
    line("PIRATES", 0, H * 0.19f, 92 * scale, cw_hex(0xffdb92ff), W, CW_ALIGN_CENTER);
    line(active_map?"PALM LAGOON":"THE GILDED SHOALS", 0, H * 0.31f, 29 * scale, cw_hex(0xece1c4ff), W, CW_ALIGN_CENTER);
    snprintf(buf, sizeof buf, "<  %s  >", mode);
    line(buf, 0, H * 0.42f, 35 * scale, CW_WHITE, W, CW_ALIGN_CENTER);
    pline("{mode_left|mode_right} mode    {map_next} map    other captains: Start joins    {start} set sail", -1, 0, H * 0.51f, 28 * scale, CW_WHITE, W, CW_ALIGN_CENTER);
    snprintf(buf, sizeof buf, "%d captain%s ready", 1 + !!(G.joined & 2) + !!(G.joined & 4) + !!(G.joined & 8), G.joined == 1 ? "" : "s");
    line(buf, 0, H * 0.64f, 28 * scale, cw_hex(0xffdb92ff), W, CW_ALIGN_CENTER);
    const char *brief=G.mode==VOYAGE?"Supply your fleet, sink the patrol, then bank the final haul. Friendly fire is off.":
                      G.mode==TEAMS?"P1 + P3 versus P2 + P4. The team with the most banked cargo wins.":
                      "Bank the most cargo before time runs out. Unclaimed captains are AI rivals.";
    line(brief,0,H*.71f,23*scale,CW_WHITE,W,CW_ALIGN_CENTER);
    line(active_map?"Sheltered coves and quiet waters. A calmer place to learn the ropes.":
         "An active volcano guards the eastern passage. Clear marked water before rocks land.",
         0,H*.87f,22*scale,cw_hex(0xffdb92ff),W,CW_ALIGN_CENTER);
    pline("{turn_left|turn_right|sail_up|sail_down} sail    {fire_left} left cannons    {fire_right} right cannons    {game_pause} pause",-1,0,H*.77f,28*scale,CW_WHITE,W,CW_ALIGN_CENTER);
    pline("Sail over cargo to collect it. Return to your harbor and press {dock} to bank and repair.",-1,0,H*.82f,26*scale,CW_WHITE,W,CW_ALIGN_CENTER);
  } else {
    t3_camera *camera=cw_camera3d_t3(G.camera);
    /* Lay out complete ship annotations before ports. Keep health, damage
     * and protection together, and connect displaced labels to their ship. */
    float label_x[SHIPS]={0},label_y[SHIPS]={0},anchor_x[SHIPS]={0},anchor_y[SHIPS]={0};
    float label_half[SHIPS]={0},label_top[SHIPS]={0};
    int label_visible[SHIPS]={0};
    if(camera)for(int i=0;i<SHIPS;i++) {
      ship *s=&G.ships[i];if(!s->active||s->respawn)continue;
      t3_vec3 p=t3_vec3_apply_mat4((t3_vec3){s->x,6.6f,s->z},&camera->matrix_world_inverse);
      p=t3_vec3_apply_mat4(p,&camera->projection_matrix);
      float ax=(p.x*.5f+.5f)*W,ay=(.5f-p.y*.5f)*H;
      int target=G.mode==VOYAGE&&G.voyage_stage==1&&i==5;
      float half=(target?96:i<CAPTAINS?32:52)*scale;
      if(s->protection)half=fmaxf(half,78*scale);
      float top=(s->protection?37:G.hit_ticks[i]?46:13)*scale;
      float preferred_y=ay-(target?34*scale:0),best=1e30f;
      label_x[i]=clampf(ax,half+4*scale,W-half-4*scale);
      label_y[i]=clampf(preferred_y,110*scale+top,H-120*scale);
      for(int row=-4;row<=4;row++)for(int column=-4;column<=4;column++) {
        float x=clampf(ax+column*70*scale,half+4*scale,W-half-4*scale);
        float y=clampf(preferred_y+row*60*scale,110*scale+top,H-120*scale);
        float score=(x-ax)*(x-ax)+(y-preferred_y)*(y-preferred_y)*1.1f;
        if(score>=best)continue;
        int overlap=0;
        for(int j=0;j<i;j++)if(label_visible[j]&&
          x+half+6*scale>label_x[j]-label_half[j]&&x-half-6*scale<label_x[j]+label_half[j]&&
          y+25*scale>label_y[j]-label_top[j]&&y-top-6*scale<label_y[j]+19*scale) {overlap=1;break;}
        if(!overlap){label_x[i]=x;label_y[i]=y;best=score;}
      }
      anchor_x[i]=ax;anchor_y[i]=ay;label_half[i]=half;label_top[i]=top;label_visible[i]=1;
    }
    if(camera)for(int i=0;i<CAPTAINS;i++)if(G.ships[i].active) {
      t3_vec3 p=t3_vec3_apply_mat4((t3_vec3){PORT_X[i],0,PORT_Z[i]},&camera->matrix_world_inverse);
      p=t3_vec3_apply_mat4(p,&camera->projection_matrix);
      float x=(p.x*.5f+.5f)*W,y=(.5f-p.y*.5f)*H+26*scale;
      /* Ship identity and protection timers take priority over port names.
       * Try nearby rows before hiding a port label in a crowded fight. */
      static const float rows[]={0,-34,34,-68,68};
      float anchor_y=y;int placed=0;
      for(int row=0;row<5&&!placed;row++) {
        float candidate=anchor_y+rows[row]*scale;
        if(candidate<110*scale||candidate+26*scale>H-100*scale)continue;
        int overlap=0;
        for(int j=0;j<SHIPS;j++) {
          ship *s=&G.ships[j];if(!s->active||s->respawn)continue;
          float sx=label_x[j],sy=label_y[j],half=label_half[j];
          float top=sy-label_top[j];
          if(x+62*scale>sx-half&&x-62*scale<sx+half&&candidate+30*scale>top&&candidate-4*scale<sy+18*scale) {overlap=1;break;}
        }
        if(!overlap){y=candidate;placed=1;}
      }
      if(!placed)continue;
      if(y!=anchor_y)cw_r2d_line(cw_v2(x,anchor_y),cw_v2(x,y+13*scale),1*scale,col(PLAYER_COLOR[i]));
      cw_r2d_rect(cw_rect_make(x-58*scale,y,116*scale,26*scale),cw_rgba(.012f,.035f,.055f,.88f));
      cw_r2d_rect(cw_rect_make(x-58*scale,y,4*scale,26*scale),col(PLAYER_COLOR[i]));
      snprintf(buf,sizeof buf,"P%d HARBOR",i+1);
      line(buf,x-54*scale,y+3*scale,18*scale,cw_hex(0xffe6baff),108*scale,CW_ALIGN_CENTER);
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
    /* Draw every connector first so none can cross over another label. */
    if(camera)for(int i=0;i<SHIPS;i++)if(label_visible[i]&&
      fabsf(label_x[i]-anchor_x[i])+fabsf(label_y[i]-anchor_y[i])>4*scale) {
      cw_color color=i<CAPTAINS?col(PLAYER_COLOR[i]):i==4?cw_hex(0xe4bd47ff):cw_hex(0xf2907aff);
      cw_r2d_line(cw_v2(anchor_x[i],anchor_y[i]),cw_v2(label_x[i],label_y[i]),1*scale,color);
    }
    if(camera) for(int i=0;i<SHIPS;i++) {
      ship *s=&G.ships[i];if(!s->active||s->respawn)continue;
      float x=label_x[i],y=label_y[i];
      bool objective_target=G.mode==VOYAGE&&G.voyage_stage==1&&i==5;
      float half=objective_target?96:i<CAPTAINS?20:52;
      cw_color label_color=i<CAPTAINS?col(PLAYER_COLOR[i]):i==4?cw_hex(0xe4bd47ff):cw_hex(0xf2907aff);
      cw_r2d_rect(cw_rect_make(x-half*scale,y-13*scale,half*2*scale,26*scale),cw_rgba(.01f,.025f,.035f,.87f));
      if(i<CAPTAINS)snprintf(buf,sizeof buf,"P%d",i+1);else snprintf(buf,sizeof buf,"%s",objective_target?"TARGET: PATROL":i==4?"MERCHANT":"PATROL");
      if(objective_target)label_color=cw_hex(0xffdb92ff);
      line(buf,x-half*scale,y-11*scale,20*scale,label_color,half*2*scale,CW_ALIGN_CENTER);
      if(s->protection) {
        snprintf(buf,sizeof buf,"PROTECTED %.1fs",s->protection/60.0f);
        cw_r2d_rect(cw_rect_make(x-78*scale,y-37*scale,156*scale,23*scale),cw_rgba(.015f,.09f,.11f,.9f));
        line(buf,x-78*scale,y-35*scale,17*scale,cw_hex(0xa9f2e8ff),156*scale,CW_ALIGN_CENTER);
      }
      float max_hp=i==5 && G.mode==VOYAGE?75+25*G.humans:100+18*s->hull;
      float bar_width=(i<CAPTAINS?56:90)*scale;
      cw_r2d_rect(cw_rect_make(x-bar_width/2,y+14*scale,bar_width,5*scale),cw_hex(0x342c29ff));
      if(G.hit_ticks[i]>0&&!s->protection) {
        float fade=clampf(G.hit_ticks[i]/30.0f,0,1);
        float trail=s->hp+(G.hit_previous_hp[i]-s->hp)*fade;
        cw_r2d_rect(cw_rect_make(x-bar_width/2,y+14*scale,bar_width*clampf(trail/max_hp,0,1),5*scale),cw_hex(0xffe3b4ff));
        snprintf(buf,sizeof buf,"-%d",G.hit_amount[i]);
        float rise=(75-G.hit_ticks[i])*.12f*scale;
        float notice_x=x,notice_y=y-36*scale-rise;
        if(notice_y<108*scale) {notice_x=x+(half+29)*scale;notice_y=y-11*scale;}
        cw_r2d_rect(cw_rect_make(notice_x-25*scale,notice_y,50*scale,22*scale),cw_rgba(.07f,.025f,.015f,.85f*fade));
        line(buf,notice_x-25*scale,notice_y+scale,18*scale,cw_rgba(1,.8f,.6f,fade),50*scale,CW_ALIGN_CENTER);
      }
      cw_r2d_rect(cw_rect_make(x-bar_width/2,y+14*scale,bar_width*clampf(s->hp/max_hp,0,1),5*scale),label_color);
    }
    if(!active_map && eruption.warning) {
      snprintf(buf,sizeof buf,"VOLCANIC ROCKFALL: CLEAR THE MARKED WATER  %.1fs",eruption.warning/60.0f);
      cw_r2d_rect(cw_rect_make(W*.22f,112*scale,W*.56f,36*scale),cw_rgba(.12f,.035f,.015f,.88f));
      line(buf,W*.22f,117*scale,22*scale,cw_hex(0xffd59bff),W*.56f,CW_ALIGN_CENTER);
    }
    cw_r2d_rect(cw_rect_make(0, 0, W, 104 * scale), cw_rgba(0.015f, 0.04f, 0.07f, 0.73f));
    line(mode, 25 * scale, 12 * scale, 30 * scale, cw_hex(0xffdb92ff), W * 0.5f, CW_ALIGN_LEFT);
    snprintf(buf, sizeof buf, "%d:%02d", G.time_left / 3600, (G.time_left / 60) % 60);
    line(buf, W - 185 * scale, 12 * scale, 31 * scale, CW_WHITE, 160 * scale, CW_ALIGN_RIGHT);
    if (G.mode == VOYAGE) {
      int bank = 0; for (int i = 0; i < CAPTAINS; i++) bank += G.ships[i].bank;
      if(G.voyage_stage==0)snprintf(buf,sizeof buf,"1/3 SUPPLY THE FLEET: BANK %d / %d",bank,G.quota);
      else if(G.voyage_stage==1)snprintf(buf,sizeof buf,"2/3 BREAK THE BLOCKADE: SINK THE PATROL");
      else snprintf(buf,sizeof buf,"3/3 BRING IT HOME: BANK %d / %d",bank,G.final_quota);
      line(buf, 25 * scale, 53 * scale, 24 * scale, CW_WHITE, W * 0.6f, CW_ALIGN_LEFT);
    } else if(G.mode==TEAMS) {
      snprintf(buf,sizeof buf,"P1 + P3: %d     |     P2 + P4: %d",G.ships[0].bank+G.ships[2].bank,G.ships[1].bank+G.ships[3].bank);
      line(buf,25*scale,53*scale,25*scale,cw_hex(0xffdf9dff),W*.55f,CW_ALIGN_LEFT);
    } else {
      line("COLLECT  >  BANK  >  UPGRADE", 25 * scale, 53 * scale, 25 * scale, CW_WHITE, W * 0.55f, CW_ALIGN_LEFT);
    }
    if(G.tick<1200 && G.humans>0 && G.mode!=VOYAGE)
      pline("{turn_left|turn_right|sail_up|sail_down} SAIL     {fire_left} LEFT CANNONS     {fire_right} RIGHT CANNONS",-1,W*.40f,58*scale,24*scale,cw_hex(0xd7d6caff),W*.58f,CW_ALIGN_RIGHT);
    if(G.mode==VOYAGE&&G.state==PLAYING&&G.objective_notice_ticks>0) {
      float fade=clampf(G.objective_notice_ticks/30.0f,0,1);
      float top=(!active_map&&eruption.warning?158:114)*scale;
      cw_r2d_rect(cw_rect_make(W*.27f,top,W*.46f,78*scale),cw_rgba(.015f,.04f,.06f,.9f*fade));
      const char *title=G.voyage_stage==0?"1 / 3   SUPPLY THE FLEET":G.voyage_stage==1?"2 / 3   BREAK THE BLOCKADE":"3 / 3   BRING IT HOME";
      line(title,W*.28f,top+8*scale,27*scale,cw_rgba(1,.86f,.57f,fade),W*.44f,CW_ALIGN_CENTER);
      const char *hint=G.voyage_stage==0?"Collect floating cargo. {dock} banks it at your harbor.":G.voyage_stage==1?"Turn your side toward the patrol. {fire_left} or {fire_right} fires.":"Gather the final cargo and bank it safely at your harbor.";
      pline(hint,-1,W*.28f,top+46*scale,24*scale,cw_rgba(1,1,1,fade),W*.44f,CW_ALIGN_CENTER);
    }
    float base_y = H - 94 * scale;
    for (int i = 0; i < CAPTAINS; i++) {
      ship *s = &G.ships[i]; if (!s->active) continue;
      float x = (25 + i * 472) * scale;
      cw_r2d_rect(cw_rect_make(x, base_y, 440 * scale, 80 * scale), cw_rgba(0.012f, 0.035f, 0.055f, 0.78f));
      cw_r2d_rect(cw_rect_make(x, base_y, 8 * scale, 80 * scale), col(PLAYER_COLOR[i]));
      snprintf(buf, sizeof buf, "P%d %s     %d BANKED", i + 1, s->human ? "CAPTAIN" : "BOT", s->bank);
      line(buf, x + 20 * scale, base_y + 5 * scale, 22 * scale, CW_WHITE, 410 * scale, CW_ALIGN_LEFT);
      snprintf(buf, sizeof buf, "HULL %d    CARGO %d / 6    %s", (int)s->hp, held_total(s), s->respawn ? "REPAIRING" : s->protection ? "PROTECTED" : "");
      line(buf, x + 20 * scale, base_y + 30 * scale, 19 * scale, cw_hex(0xffdf9dff), 410 * scale, CW_ALIGN_LEFT);
      cw_color cargo_ink=CW_WHITE;
      if(!s->respawn&&held_total(s)==6) {
        snprintf(buf,sizeof buf,"HOLD FULL: RETURN TO P%d HARBOR",i+1);
        cargo_ink=cw_hex(0xffdf9dff);
      } else if(!s->respawn&&G.cargo_notice_ticks[i]>0) {
        snprintf(buf,sizeof buf,"%s",G.cargo_notice[i]);cargo_ink=cw_hex(0xa9f2c1ff);
      } else snprintf(buf, sizeof buf, "WOOD %d   IRON %d   COIN %d", s->stored[0], s->stored[1], s->stored[2]);
      line(buf, x + 20 * scale, base_y + 54 * scale, 18 * scale, cargo_ink, 410 * scale, CW_ALIGN_LEFT);
      if(G.state==PLAYING && s->human) {
        /* Both broadsides share the crew's existing reload timer. Show one
         * meter, rather than suggesting the other side can fire sooner. */
        float ready=s->respawn?0:clampf(1-s->cooldown/(float)(80-s->guns*11),0,1);
        cw_color ink=s->respawn?cw_hex(0x9aa6afff):s->cooldown?cw_hex(0xffdf9dff):cw_hex(0xa9f2c1ff);
        cw_r2d_rect(cw_rect_make(x,base_y-36*scale,440*scale,34*scale),cw_rgba(.012f,.035f,.055f,.88f));
        if(s->respawn)snprintf(buf,sizeof buf,"CANNONS UNAVAILABLE: REPAIRING");
        else if(s->cooldown)snprintf(buf,sizeof buf,"RELOAD %.1fs   {fire_left} LEFT   {fire_right} RIGHT",s->cooldown/60.0f);
        else snprintf(buf,sizeof buf,"CANNONS READY   {fire_left} LEFT   {fire_right} RIGHT");
        pline(buf,i,x+20*scale,base_y-35*scale,24*scale,ink,410*scale,CW_ALIGN_LEFT);
        cw_r2d_rect(cw_rect_make(x+20*scale,base_y-7*scale,400*scale,3*scale),cw_hex(0x42515bff));
        cw_r2d_rect(cw_rect_make(x+20*scale,base_y-7*scale,400*scale*ready,3*scale),ink);
      }
      if (G.state==PLAYING && s->human && !s->respawn &&
          (held_total(s)||s->stored[0]||s->stored[1]||s->stored[2]) &&
          harbor_distance2(i,s->x,s->z)<36) {
        float top=base_y-148*scale;
        cw_r2d_rect(cw_rect_make(x,top,440*scale,108*scale),cw_rgba(.015f,.035f,.05f,.91f));
        pline("HARBOR: {dock} bank cargo",i,x+16*scale,top+5*scale,22*scale,col(PLAYER_COLOR[i]),410*scale,CW_ALIGN_LEFT);
        const int levels[]={s->hull,s->sails,s->guns};
        const int funds[]={s->stored[TIMBER],s->stored[COIN],s->stored[IRON]};
        const char *names[]={"{upgrade_hull} Hull","{upgrade_sails} Sails","{upgrade_guns} Cannons"};
        const char *costs[]={"2 wood","2 coin","2 iron"};
        for(int k=0;k<3;k++) {
          snprintf(buf,sizeof buf,"%s %d/2  %s",names[k],levels[k],levels[k]>=2?"MAX":costs[k]);
          cw_color ink=levels[k]>=2?cw_hex(0x87c5a0ff):funds[k]>=2?cw_hex(0xffdf9dff):cw_hex(0x9aa6afff);
          pline(buf,i,x+16*scale,top+(32+23*k)*scale,21*scale,ink,410*scale,CW_ALIGN_LEFT);
        }
      }
    }
    if (G.state == RESULT) {
      cw_r2d_rect(cw_rect_make(W*.17f,H*.29f,W*.66f,H*.43f),cw_rgba(.015f,.045f,.07f,.94f));
      if (G.mode == VOYAGE) snprintf(buf,sizeof buf,"%s",G.winner>=0?"VOYAGE COMPLETE":"TIME RAN OUT");
      else if (G.winner == -2) snprintf(buf,sizeof buf,"%s",G.mode==TEAMS?"TEAM MATCH DRAW":"TREASURE RACE DRAW");
      else if (G.mode == TEAMS) snprintf(buf,sizeof buf,"TEAM %d WINS - P%d + P%d",G.winner+1,G.winner+1,G.winner+3);
      else snprintf(buf,sizeof buf,"CAPTAIN %d WINS",G.winner+1);
      line(buf,W*.18f,H*.32f,52*scale,cw_hex(0xffdb92ff),W*.64f,CW_ALIGN_CENTER);
      if(G.mode==VOYAGE) {
        int total=0;for(int i=0;i<CAPTAINS;i++)total+=G.ships[i].bank;
        snprintf(buf,sizeof buf,"FLEET CARGO SAFELY BANKED: %d",total);
        line(buf,W*.2f,H*.43f,28*scale,CW_WHITE,W*.6f,CW_ALIGN_CENTER);
        snprintf(buf,sizeof buf,"PATROL SHIPS DEFEATED: %d",G.patrol_defeats);
        line(buf,W*.2f,H*.48f,26*scale,CW_WHITE,W*.6f,CW_ALIGN_CENTER);
        const char *next=G.winner>=0?"All three objectives completed.":G.voyage_stage==0?"Unfinished: supply the fleet.":G.voyage_stage==1?"Unfinished: break the patrol blockade.":"Unfinished: bring the final haul home.";
        line(next,W*.2f,H*.55f,24*scale,cw_hex(0xffdf9dff),W*.6f,CW_ALIGN_CENTER);
      } else {
        int order[CAPTAINS]={0,1,2,3},count=G.mode==TEAMS?2:CAPTAINS;
        int scores[CAPTAINS];
        for(int i=0;i<count;i++)scores[i]=G.ships[i].bank+(G.mode==TEAMS?G.ships[i+2].bank:0);
        for(int i=1;i<count;i++)for(int j=i;j>0&&scores[order[j]]>scores[order[j-1]];j--) {
          int temp=order[j];order[j]=order[j-1];order[j-1]=temp;
        }
        line("BANKED CARGO",W*.52f,H*.405f,19*scale,cw_hex(0xc2c9cdff),W*.22f,CW_ALIGN_RIGHT);
        for(int row=0;row<count;row++) {
          int id=order[row];float y=H*.445f+row*43*scale;
          cw_color ink=col(PLAYER_COLOR[id]);
          cw_r2d_rect(cw_rect_make(W*.25f,y+3*scale,4*scale,26*scale),ink);
          if(G.mode==TEAMS)snprintf(buf,sizeof buf,"TEAM %d   P%d + P%d",id+1,id+1,id+3);
          else snprintf(buf,sizeof buf,"P%d   %s",id+1,G.ships[id].human?"CAPTAIN":"BOT CAPTAIN");
          line(buf,W*.27f,y,25*scale,ink,W*.3f,CW_ALIGN_LEFT);
          snprintf(buf,sizeof buf,"%d",scores[id]);
          line(buf,W*.58f,y,27*scale,CW_WHITE,W*.16f,CW_ALIGN_RIGHT);
        }
        if(G.mode==TEAMS)line("Each team's score combines both captains' banked cargo.",W*.2f,H*.56f,22*scale,CW_WHITE,W*.6f,CW_ALIGN_CENTER);
      }
      pline("PRESS {start} TO CHOOSE A NEW MATCH",-1,W*.2f,H*.665f,25*scale,CW_WHITE,W*.6f,CW_ALIGN_CENTER);
    }
  }
  if(G.paused) {
    cw_r2d_rect(cw_rect_make(0,0,W,H),cw_rgba(.005f,.015f,.025f,.6f));
    cw_r2d_rect(cw_rect_make(W*.2f,H*.3f,W*.6f,H*.38f),cw_rgba(.015f,.04f,.06f,.95f));
    line("PAUSED",0,H*.34f,64*scale,cw_hex(0xffdb92ff),W,CW_ALIGN_CENTER);
    pline("{game_pause} resumes",-1,0,H*.45f,29*scale,CW_WHITE,W,CW_ALIGN_CENTER);
    pline("{leave_match} returns to the chart room",-1,0,H*.51f,25*scale,CW_WHITE,W,CW_ALIGN_CENTER);
    line("After sinking: 4 seconds of protection. Firing ends it early.",0,H*.59f,22*scale,cw_hex(0xa9f2e8ff),W,CW_ALIGN_CENTER);
  }
  cw_r2d_end();
  t3_renderer *r = cw_render3d_renderer(cw_scene_app_tree());
  if (r) t3_renderer_reset_bindings(r);
}

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

static void cmd_sink(int argc,const char **argv,cw_reply *r) {
  int id=argc>1?atoi(argv[1]):0;
  if(id<0||id>=SHIPS){cw_reply_error(r,"ship index must be 0..5");return;}
  damage_ship(id,-1,999);
  cw_reply_printf(r,"{\"ship\":%d,\"hp\":%.0f,\"respawn\":%d}",id,G.ships[id].hp,G.ships[id].respawn);
}

static void cmd_effects(int argc,const char **argv,cw_reply *r) {
  (void)argc;(void)argv;
  cw_particles_stats stats;cw_particles_get_stats(cw_scene_app_tree(),&stats);
  cw_reply_printf(r,"{\"systems\":%d,\"capacity\":%d,\"draw_calls\":%d,\"smoke_alive\":%d,\"flash_alive\":%d,\"sea_alive\":%d,\"wood_alive\":%d,\"dust_alive\":%d}",
    stats.systems,stats.capacity,stats.draw_calls,cw_particles_alive(cannon_smoke),cw_particles_alive(cannon_flash),
    cw_particles_alive(impact_particles[0]),cw_particles_alive(impact_particles[1]),cw_particles_alive(impact_particles[2]));
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
  cw_reply_printf(r,"],\"hash\":%u,\"winner\":%d,\"state\":%d}",G.hash,G.winner,G.state);
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
 * edges instead). On Very Low the island and the four port buildings
 * (100,000 triangles) stop casting into the sun's shadow map, which is redrawn
 * every frame because the ships move. Palms, rocks, flagpoles and ships keep
 * their shadows. The level reached is saved. */
typedef struct { uint32_t magic, gfx; } save_data;
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
  for (int i = 0; i < 1 + CAPTAINS; i++)
    if (G.big_scenery[i]) set_cast_shadow(G.big_scenery[i], cw_gfx_get(G.lever_scenery_shadows) > 0);
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
  G.mode_l = cw_action_find("mode_left"); G.mode_r = cw_action_find("mode_right");
  G.font = cw_ui_font_sans();
  sound_init();
  G.lever_scenery_shadows = cw_quality_lever_add("Island shadows", SCENERY_SHADOW_VALUES, 2,
                                                 (cw_quality_lever_presets){ .very_low = 0, .low = 1, .medium = 1, .high = 1, .ultra = 1 });
  G.lever_scenery_detail = cw_quality_lever_add("Scenery detail", SCENERY_DETAIL_VALUES, 3,
                                                (cw_quality_lever_presets){ .very_low = 0, .low = 1, .medium = 2, .high = 2, .ultra = 2 });
  cw_quality_auto(true, 0);
  if (SAVE.magic == SAVE_MAGIC && SAVE.gfx) cw_quality_restore(SAVE.gfx);
  cw_quality_on_change(quality_changed, NULL);
  setup_world();
  G.post = cw_node_get(G.world, "Post");
  apply_graphics();
  G.state = TITLE; G.mode = VOYAGE; G.joined = 1; G.humans = 1;
  cw_debug_command("pirates_test_route","Test reachable cargo selection from the stalled cove approach; resets match",cmd_route_test);
  cw_debug_command("pirates_placement","Inspect rendered prop support, cargo water contact and hull clearance",cmd_placement);
  cw_debug_command("pirates_test_hull","Check shore approach, rotation, reverse escape and spawn clearance on both maps; resets match",cmd_hull_test);
  cw_debug_command("pirates_sink","Sink ship 0..5 through normal damage rules for visual inspection",cmd_sink);
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
  cw_debug_command("pirates_map","Select map 0 volcanic / 1 lagoon; resets match",cmd_map);
  cw_debug_i32("pirates.state", &G.state);
  cw_debug_i32("pirates.paused", &G.paused);
  cw_debug_command("bind", "bind <action> <bindings>: replace an action's bindings", cmd_bind);
  cw_debug_field("pirates.protection", G.dbg_protection, CW_DBG_I32, CAPTAINS);
  cw_debug_field("pirates.cooldown", G.dbg_cooldown, CW_DBG_I32, CAPTAINS);
  cw_debug_i32("pirates.mode", &G.mode);
  cw_debug_i32("pirates.humans", &G.humans);
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
  cw_debug_command("pirates_test_voyage","Test three voyage stages with 1..4 players; ends match",cmd_voyage_test);
  cw_debug_command("pirates_test_salvage","Run sinking, respawn and restart regression; resets match",cmd_salvage_test);
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
