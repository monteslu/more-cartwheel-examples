/* Eight deliberately anachronistic sorties. Aerial targets share weapon,
 * lock-on and collision paths; their two components remain independent. */
static int mission_ground_count(int first,int last);
static const char *mission_name(void) {
  static const char *names[]={"01 / COUNTRY FAIR","02 / TIME WARP","03 / FUEL CHAIN","04 / COASTAL SCRAMBLE","05 / SKY FORTRESS","06 / RED FORTRESS","07 / OPERATION SNOWGLOBE","08 / STEEL TIDE"};
  return names[(G.level%MISSIONS+MISSIONS)%MISSIONS];
}
static const char *mission_goal(void) {
  if(G.mission_complete)return "MISSION COMPLETE";
  static char goal[160];
  switch(G.level) {
  case 0:snprintf(goal,sizeof goal,"BALLOONS %d / 12  |  ENVELOPE OR BASKET: YOUR CHOICE",G.balloon_kills);break;
  case 1:snprintf(goal,sizeof goal,"PROP FIGHTERS %d / 8    BLIMPS %d / %d",G.kills,G.blimp_kills,BLIMPS);break;
  case 2:snprintf(goal,sizeof goal,"DESTROY THE FUEL DEPOTS  %d / 4  |  CHAIN THE BLASTS",G.fuel_depots);break;
  case 3:snprintf(goal,sizeof goal,"CLEAR THE INTERCEPTORS  %d / 10",G.kills);break;
  case 5:snprintf(goal,sizeof goal,"FORTRESS RADARS + COMMAND  %d / 4",mission_ground_count(68,71));break;
  case 6:if(mission_ground_count(84,86)<3)snprintf(goal,sizeof goal,"POWER CORES %d / 3  |  SANTA IS SHIELDED",mission_ground_count(84,86));else snprintf(goal,sizeof goal,"SANTA EXPOSED  |  FINISH THE BIG GUY");break;
  case 7:snprintf(goal,sizeof goal,"OFFSHORE RIGS  %d / 4",mission_ground_count(88,91));break;
  default:snprintf(goal,sizeof goal,G.shields_left?"SHIELD GENERATORS %d / 4 REMAIN  |  CORE PROTECTED":"SHIELD DOWN  |  DESTROY THE MOTHERSHIP CORE",G.shields_left);break;
  }
  return goal;
}
static t3_material *campaign_mat(uint32_t color,float metal) {
  t3_material *m=t3_mesh_standard_material_new(color);m->roughness=.58f;m->metalness=metal;return m;
}
static void fir_tree(t3_object *parent,float x,float z,float size,bool festive);
#include "breakup.h"
#include "districts.h"
#include "kremlin.h"
static t3_object *oval(t3_object *root,float x,float y,float z,float rx,float ry,float rz,t3_material *mat) {
  t3_mesh *m=t3_mesh_new(t3_sphere_geometry_new(1,40,28),mat);
  m->base.cast_shadow=m->base.receive_shadow=true;t3_object_set_position(m,x,y,z);t3_object_set_scale(m,rx,ry,rz);t3_object_add(root,(t3_object *)m);return (t3_object *)m;
}
static void iron_cross(t3_object *root,float side,float z,t3_material *black,t3_material *white) {
  box(root,side,0,z,1,19,6,white);box(root,side,0,z,1,6,19,white);
  box(root,side+(side>0?.6f:-.6f),0,z,.3f,16,3.5f,black);
  box(root,side+(side>0?.6f:-.6f),0,z,.3f,3.5f,16,black);
}
static t3_object *balloon_envelope(t3_object *root,int palette) {
  t3_object *group=t3_group_new();t3_object_add(root,group);
  static const uint32_t colors[]={0xef3d32,0xffb923,0x287ed6,0x41a654,0x9b4bd1,0xf4f3e9};
  t3_material *fabric[6];for(int i=0;i<6;i++){fabric[i]=campaign_mat(colors[(i+palette)%6],0);fabric[i]->roughness=.85f;}
  /* Modern bulb: broad upper shoulders, round crown and tapered lower skirt.
   * Twenty colourful fabric gores, smooth outward-facing triangles. */
  for(int panel=0;panel<20;panel++) {
    float pos[41*5*3],normal[41*5*3];uint32_t index[40*4*6];int at=0;
    for(int j=0;j<=40;j++)for(int i=0;i<=4;i++) {
      float theta=.01f+(PI-.025f)*j/40,phi=2*PI*(panel+i/4.0f)/20;
      float co=cosf(theta),si=sinf(theta),t=(1+co)*.5f;
      float r=52*si*powf(t,.18f)+2*powf(1-t,4);
      /* Analytic profile derivative gives smooth fabric lighting. */
      float dr=52*(co*powf(t,.18f)-.09f*si*si*powf(fmaxf(t,.00001f),-.82f))+4*si*powf(1-t,3);
      int k=(j*5+i)*3;pos[k]=r*cosf(phi);pos[k+1]=44*co;pos[k+2]=r*sinf(phi);
      t3_vec3 n=t3_vec3_normalize((t3_vec3){44*si*cosf(phi),dr,44*si*sinf(phi)});normal[k]=n.x;normal[k+1]=n.y;normal[k+2]=n.z;
    }
    for(int j=0;j<40;j++)for(int i=0;i<4;i++){int q=j*5+i;index[at++]=q;index[at++]=q+1;index[at++]=q+5;index[at++]=q+1;index[at++]=q+6;index[at++]=q+5;}
    t3_geometry *g=t3_geometry_new();t3_attribute *p=t3_attribute_new(T3_FLOAT32,pos,41*5,3),*n=t3_attribute_new(T3_FLOAT32,normal,41*5,3),*ix=t3_attribute_new(T3_UINT32,index,at,1);
    t3_geometry_set_attribute(g,T3_ATTR_POSITION,p);t3_geometry_set_attribute(g,T3_ATTR_NORMAL,n);t3_geometry_set_index(g,ix);t3_release(p);t3_release(n);t3_release(ix);
    t3_mesh *mesh=t3_mesh_new(g,fabric[(panel/2)%6]);t3_release(g);mesh->base.cast_shadow=mesh->base.receive_shadow=true;t3_object_add(group,(t3_object *)mesh);
  }
  for(int i=0;i<6;i++)t3_release(fabric[i]);return group;
}
static void balloon_basket(air_target *a,t3_material *metal) {
  t3_material *wicker=campaign_mat(0x98723f,0),*rim=campaign_mat(0x4c3a2b,0),*skin=campaign_mat(0xd5a47e,0);
  box(a->basket,0,-1.1f,0,4.3f,.4f,3.7f,wicker);
  for(int s=-1;s<=1;s+=2){box(a->basket,s*2,0,0,.25f,2,3.7f,wicker);box(a->basket,0,0,s*1.7f,4.3f,2,.25f,wicker);box(a->basket,s*2,1.1f,0,.35f,.3f,3.8f,rim);box(a->basket,0,1.1f,s*1.7f,4.4f,.3f,.35f,rim);}
  for(int j=0;j<4;j++){float x=j&1?1.8f:-1.8f,z=j&2?1.5f:-1.5f;box(a->basket,x,3.2f,z,.09f,4.5f,.09f,metal);box(a->ropes,x,-53,z,.14f,20,.14f,metal);}
  box(a->basket,0,5.4f,0,3.8f,.2f,3.2f,metal);box(a->basket,0,5.9f,0,.7f,.8f,.7f,metal);
  oval(a->basket,0,7.3f,0,.4f,1.3f,.4f,t3_mesh_basic_material_new(0xffc161));
  for(int person=0;person<2;person++){
    float x=person?.85f:-.85f;t3_material *shirt=campaign_mat(person?0x317bb1:0xc44c36,0);
    oval(a->basket,x,1.55f,.3f,.38f,.6f,.23f,shirt);oval(a->basket,x,2.45f,.3f,.25f,.3f,.25f,skin);
    oval(a->basket,x,2.66f,.32f,.26f,.13f,.26f,campaign_mat(person?0x47352a:0xddddce,0));
    for(int side=-1;side<=1;side+=2){t3_object *arm=box(a->basket,x+side*.42f,1.42f,.15f,.16f,.75f,.18f,shirt);t3_object_set_rotation(arm,0,0,-side*.25f);oval(a->basket,x+side*.5f,1.05f,.12f,.1f,.13f,.1f,skin);}
  }
}
#include "mothership.h"
#include "santa.h"
static void make_sleigh(enemy *e,t3_material *red,t3_material *gold,t3_material *brown,t3_material *green);
static void campaign_make(void) {
  architecture_load();capital_make();
  t3_material *olive=campaign_mat(0x525a3c,.25f),*dark=campaign_mat(0x202628,.4f),*silver=campaign_mat(0x9ca8a7,.65f),*white=campaign_mat(0xe7dfcb,0),*black=campaign_mat(0x141716,0);
  for(int i=0;i<ENEMIES;i++) {
    enemy *e=&G.enemies[i];e->vintage=t3_group_new();t3_object_add(e->node,e->vintage);
    oval(e->vintage,0,0,0,1,1.1f,5.8f,olive);box(e->vintage,0,0,-.5f,12,.28f,2.3f,olive);
    box(e->vintage,0,.55f,4,4.6f,.2f,1.4f,olive);box(e->vintage,0,1.4f,4.1f,.2f,2,1.5f,olive);
    oval(e->vintage,0,.9f,.6f,.65f,.55f,1.3f,dark);
    e->propeller=t3_group_new();t3_object_set_position(e->propeller,0,0,-5.9f);t3_object_add(e->vintage,e->propeller);
    box(e->propeller,0,0,0,.17f,4,.13f,dark);box(e->propeller,0,0,0,4,.17f,.13f,dark);
    box(e->vintage,-4,.16f,-.5f,1.5f,.04f,.35f,white);box(e->vintage,-4,.18f,-.5f,.35f,.04f,1.6f,white);
    e->vintage->visible=false;make_sleigh(e,campaign_mat(0xac2929,.2f),campaign_mat(0xcdaa56,.6f),campaign_mat(0x77533b,0),olive);
  }
  for(int i=0;i<AIR_TARGETS;i++) {
    air_target *a=&G.air_targets[i];a->kind=i<BALLOONS?0:i<SHIELD_FIRST?1:i<MOTHER?2:i==SANTA?4:3;
    a->root=t3_group_new();t3_object_add((t3_object *)G.scene,a->root);
    a->basket=t3_group_new();t3_object_add((t3_object *)G.scene,a->basket);
    a->ropes=t3_group_new();t3_object_add(a->root,a->ropes);
    if(a->kind==0) {
      a->envelope=balloon_envelope(a->root,i);
      balloon_basket(a,dark);
    } else if(a->kind==1) {
      a->envelope=oval(a->root,0,0,0,34,32,104,silver);
      box(a->basket,0,0,0,13,8,35,dark);box(a->basket,-11,-1,3,10,2,8,olive);box(a->basket,11,-1,3,10,2,8,olive);
      box(a->root,0,16,82,1,43,35,olive);box(a->root,0,0,82,62,1,35,olive);
      iron_cross(a->root,33,0,black,white);iron_cross(a->root,-33,0,black,white);
      for(int j=0;j<4;j++)box(a->ropes,j&1?5:-5,-30,j&2?12:-12,.35f,14,.35f,dark);
    } else if(a->kind==2) {
      a->envelope=oval(a->root,0,0,0,17,19,17,campaign_mat(0x2fcac0,.6f));
      box(a->root,0,-65,0,14,95,14,dark);a->basket->visible=false;
    } else if(a->kind==4){santa_make(a);
    } else {
      a->envelope=t3_group_new();t3_object_add(a->root,a->envelope);
      oval(a->envelope,0,0,0,540,42,540,dark);oval(a->envelope,0,35,0,215,48,215,silver);
      for(int j=0;j<16;j++){float angle=j*2*PI/16;t3_object *rib=box(a->envelope,sinf(angle)*275,23,cosf(angle)*275,12,18,410,silver);t3_object_set_rotation(rib,0,angle,0);}
      a->basket=oval(a->root,0,-52,0,52,20,52,campaign_mat(0xf37934,.7f));
      t3_material *shield=t3_mesh_basic_material_new(0x39aafc);shield->transparent=true;shield->opacity=.13f;shield->depth_write=false;
      a->shield=oval(a->root,0,10,0,565,100,565,shield);a->shield->cast_shadow=false;a->shield->receive_shadow=false;
    }
    a->root->visible=a->basket->visible=false;
  }
}
static void country_road(float offset,float z0,float z1,t3_material *mat,float width) {
  int segments=(int)((z1-z0)/12);float pos[(600+1)*6],normal[(600+1)*6];uint32_t idx[600*6];
  if(segments>600)segments=600;
  for(int j=0;j<=segments;j++)for(int i=0;i<2;i++) {
    float z=z0+(z1-z0)*j/segments,x=offset+125*sinf(z*.0017f)+60*cosf(z*.0031f)+(i?width:-width);int k=(j*2+i)*3;
    pos[k]=x;pos[k+1]=terrain_height(x,z)+.65f;pos[k+2]=z;normal[k]=normal[k+2]=0;normal[k+1]=1;
  }
  for(int j=0;j<segments;j++){int a=j*2,k=j*6;idx[k]=a;idx[k+1]=a+2;idx[k+2]=a+1;idx[k+3]=a+1;idx[k+4]=a+2;idx[k+5]=a+3;}
  t3_geometry *g=t3_geometry_new();t3_attribute *p=t3_attribute_new(T3_FLOAT32,pos,(segments+1)*2,3),*n=t3_attribute_new(T3_FLOAT32,normal,(segments+1)*2,3),*ix=t3_attribute_new(T3_UINT32,idx,segments*6,1);
  t3_geometry_set_attribute(g,T3_ATTR_POSITION,p);t3_geometry_set_attribute(g,T3_ATTR_NORMAL,n);t3_geometry_set_index(g,ix);t3_release(p);t3_release(n);t3_release(ix);t3_object_add(G.countryside,(t3_object *)t3_mesh_new(g,mat));t3_release(g);
}
static void make_countryside(void) {
  if(G.countryside)return;G.countryside=t3_group_new();t3_object_add((t3_object *)G.scene,G.countryside);
  t3_material *road=campaign_mat(0x696357,0),*red=campaign_mat(0x934735,0),*roof=campaign_mat(0x4b5152,.25f),*trim=campaign_mat(0xd9cdb3,0),*wood=campaign_mat(0x74634c,0);
  country_road(-700,-2600,2300,road,7);country_road(-1800,-2100,2000,road,6);
  for(int i=0;i<8;i++) {
    float z=-2000+(i/2)*1120,road_x=(i%2?-1800:-700)+125*sinf(z*.0017f)+60*cosf(z*.0031f),x=road_x-90,y=terrain_height(x,z);
    t3_object *barn=t3_group_new();t3_object_add(G.countryside,barn);t3_object_set_position(barn,x,y,z);
    box(barn,0,9,0,30,18,42,red);
    for(int side=-1;side<=1;side+=2){t3_object *r=box(barn,side*8,23,0,19,1,46,roof);t3_object_set_rotation(r,0,0,-side*.52f);}
    box(barn,0,7,21.2f,12,14,.4f,wood);box(barn,-7,7,21.6f,.6f,15,.5f,trim);box(barn,7,7,21.6f,.6f,15,.5f,trim);box(barn,0,14.5f,21.6f,14,.6f,.5f,trim);
    register_building(28+i,barn,x,y+10,z,27);G.buildings[28+i].ruin->visible=false;
    paved_strip(G.countryside,x-6,road_x+8,z-6,z+6,road);
    for(int j=0;j<14;j++){float fx=x-45+j*8,fy=terrain_height(fx,z+65);box(G.countryside,fx,fy+1.7f,z+65,.45f,3.4f,.45f,wood);}
    for(int j=0;j<4;j++)oval(G.countryside,x-32+j*8,terrain_height(x-32+j*8,z-45)+3,z-45,3,3,4,campaign_mat(0xb59752,0));
    structure *b=&G.buildings[28+i];b->bounds=(t3_box3){{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};t3_object_update_world_matrix(b->node,true,true);t3_object_traverse(b->node,building_collider,b);
  }
}
/* Scenery is cached per setting; only its own buildings participate in combat. */
static int mission_ground_count(int first,int last) {
  int n=0;for(int i=first;i<=last;i++)if(G.buildings[i].node&&G.buildings[i].hp<=0)n++;return n;
}
static void finish_structure(int index,t3_object *node,float x,float y,float z,float radius) {
  register_building(index,node,x,y,z,radius);structure *b=&G.buildings[index];
  b->bounds=(t3_box3){{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};
  t3_object_update_world_matrix(node,true,true);t3_object_traverse(node,building_collider,b);
}
static void fir_tree(t3_object *parent,float x,float z,float size,bool festive) {
  forest_single(parent,x,z,size,2,G.map_style==2||G.map_style==5||G.map_style==6);
  if(festive){float y=terrain_height(x,z);t3_material *gold=t3_mesh_basic_material_new(0xffdc65);static const uint32_t colors[]={0xffbd46,0xe44038,0x4cbde2};for(int i=0;i<12;i++){float a=i*2.4f,r=size*(.20f-i*.011f);oval(parent,x+sinf(a)*r,y+size*(.30f+i*.05f),z+cosf(a)*r,size*.017f,size*.017f,size*.017f,t3_mesh_basic_material_new(colors[i%3]));}oval(parent,x,y+size*1.015f,z,size*.035f,size*.045f,size*.035f,gold);}
}
static void make_sleigh(enemy *e,t3_material *red,t3_material *gold,t3_material *brown,t3_material *green) {
  e->sleigh=t3_group_new();t3_object_add(e->node,e->sleigh);t3_object *r=e->sleigh;
  box(r,0,0,2,3.5f,1,5,red);box(r,0,1,4,3.5f,2,.4f,red);box(r,-1.7f,1,2,.3f,2,4,red);box(r,1.7f,1,2,.3f,2,4,red);
  for(int s=-1;s<=1;s+=2){box(r,s*1.5f,-1,1.3f,.25f,.25f,7,gold);oval(r,s*1.5f,-.7f,-2,.16f,.7f,.9f,gold);}
  box(r,0,1.2f,2,1.5f,1.5f,1.5f,green);box(r,0,1.3f,2,.18f,1.7f,1.7f,gold);
  oval(r,0,1.5f,.4f,.65f,.8f,.6f,red);oval(r,0,2.5f,.4f,.45f,.45f,.45f,campaign_mat(0xe2b995,0));
  for(int i=0;i<4;i++){float x=i%2?1.7f:-1.7f,z=-5-(i/2)*5;
    oval(r,x,0,z,.65f,.85f,1.5f,brown);oval(r,x,1,z-1.3f,.45f,.6f,.8f,brown);
    for(int j=0;j<4;j++)box(r,x+(j%2?.4f:-.4f),-1,z+(j/2?.9f:-.9f),.18f,1.4f,.18f,brown);
    for(int s=-1;s<=1;s+=2){t3_object *antler=box(r,x+s*.4f,2,z-1.4f,.12f,1.1f,.12f,gold);t3_object_set_rotation(antler,0,0,-s*.4f);box(r,x+s*.6f,2.2f,z-1.4f,.6f,.1f,.1f,gold);}
    box(r,x,-.1f,z+2,.08f,.08f,3,gold);
  }
  oval(r,1.7f,1,-11.1f,.2f,.2f,.2f,t3_mesh_basic_material_new(0xff3535));e->sleigh->visible=false;
}
/* Outside the flight boundary the same land continues to the fog horizon.
 * Edge vertices match the 8 m terrain grid, avoiding a square island/seam. */
static void make_land_horizon(int style) {
  if(G.horizon[style]||style==0||style==7)return;
  t3_object *root=G.horizon[style]=t3_group_new();t3_object_add((t3_object *)G.scene,root);
  t3_material *mat=campaign_mat(G.landscape_far_color,0);
  static const float rings[]={4096,6000,10000,24000,100000,1200000};
  for(int side=0;side<4;side++) {
    int count=6*1025;float *p=malloc(count*3*sizeof(float)),*n=malloc(count*3*sizeof(float));uint32_t *ix=malloc(5*1024*6*sizeof(uint32_t));int at=0;
    for(int ring=0;ring<6;ring++)for(int j=0;j<=1024;j++) {
      float a=rings[ring],along=-a+2*a*j/1024,x=side<2?(side? a:-a):along,z=side<2?along:(side==2?-a:a);int k=(ring*1025+j)*3;
      p[k]=x;p[k+1]=landscape_height(x,z);p[k+2]=z;
      t3_vec3 norm=t3_vec3_normalize((t3_vec3){landscape_height(x-4,z)-landscape_height(x+4,z),8,landscape_height(x,z-4)-landscape_height(x,z+4)});n[k]=norm.x;n[k+1]=norm.y;n[k+2]=norm.z;
    }
    for(int r=0;r<5;r++)for(int j=0;j<1024;j++){int a=r*1025+j,b=a+1025;bool reverse=side==0||side==3;ix[at++]=a;ix[at++]=reverse?b:a+1;ix[at++]=reverse?a+1:b;ix[at++]=a+1;ix[at++]=reverse?b:b+1;ix[at++]=reverse?b+1:b;}
    t3_geometry *g=t3_geometry_new();t3_attribute *pa=t3_attribute_new(T3_FLOAT32,p,count,3),*na=t3_attribute_new(T3_FLOAT32,n,count,3),*ia=t3_attribute_new(T3_UINT32,ix,at,1);
    t3_geometry_set_attribute(g,T3_ATTR_POSITION,pa);t3_geometry_set_attribute(g,T3_ATTR_NORMAL,na);t3_geometry_set_index(g,ia);t3_release(pa);t3_release(na);t3_release(ia);t3_mesh *mesh=t3_mesh_new(g,mat);mesh->base.cast_shadow=false;mesh->base.receive_shadow=true;t3_object_add(root,(t3_object *)mesh);t3_release(g);free(p);free(n);free(ix);
  }
}
static void make_distinct_map(int style) {
  if(style==0||(style==7&&G.decor[style]))return;
  if(style==1){make_countryside();G.decor[1]=G.countryside;make_land_horizon(style);return;}
  if(G.decor[style])return;
  t3_object *root=G.decor[style]=t3_group_new();t3_object_add((t3_object *)G.scene,root);
  t3_material *road=campaign_mat(0x3d4246,0),*stone=campaign_mat(0x7e878b,.15f),*snow=campaign_mat(0xdde5e9,0),*red=campaign_mat(0xa02f2b,.15f),*gold=campaign_mat(0xcda854,.65f),*green=campaign_mat(0x28705a,.1f),*dark=campaign_mat(0x303b43,.6f);
  if(style==2||style==3){t3_object *old=G.countryside;G.countryside=root;country_road(style==2?-600:0,-3500,3500,road,9);G.countryside=old;}
  int first=style==2?36:style==3?44:style==4?52:style==5?68:style==6?84:88;
  int count=style==4||style==5?16:style==6?4:8;
  if(style==4||style==5){
    if(style==5){/* A winding river separates the fortress from the old city. */
      t3_material *ice=campaign_mat(0x578597,.55f);for(int j=0;j<90;j++){float z=-4000+j*90,x=950+220*sinf(z*.0007f);box(root,x,4,z,240,2,92,ice);}
    }
  }
  if(style==6){paved_strip(root,-1400,1000,-700,-680,road);paved_strip(root,-310,-290,-2100,1500,road);}
  for(int i=0;i<count;i++) {
    float x,z,y;
    if(style==2){z=-2400+(i/2)*900;x=-600+260*sinf(z*.0006f)+(i%2?160:-170);}
    else if(style==3){z=-2300+(i/2)*900;x=-450*sinf(z*.0008f)+(i%2?230:-230);}
    else if(style==4){x=-1750+(i%4)*720;z=-2450+(i/4)*900;}
    else if(style==5){
      if(i==3){x=-650;z=-1300;}else if(i==4){x=-360;z=-30;}else if(i==5){x=500;z=-950;}else if(i>=6){float a=(i-6)*2*PI/10;x=-500+sinf(a)*1050;z=-1400+cosf(a)*1000;}else{static const float radar_sites[3][2]={{-1150,-2050},{-650,-1900},{-150,-1750}};x=radar_sites[i][0];z=radar_sites[i][1];}
    }
    else if(style==6){x=-1100+(i%2)*1100;z=-1600+(i/2)*1200;}
    else{x=-1400+(i%2)*2000;z=-2200+(i/2)*1150;}
    y=style==7?15:terrain_height(x,z);t3_object *b=t3_group_new();t3_object_add(root,b);t3_object_set_position(b,x,y,z);
    float radius=45,cy=25;
    if(style==2){box(b,0,13,0,44,26,64,stone);for(int s=-1;s<=1;s+=2){t3_object *roof=box(b,s*12,35,0,28,2,68,snow);t3_object_set_rotation(roof,0,0,-s*.5f);}box(b,0,7,32.3f,8,14,1,dark);}
    else if(style==3){
      if(i<4){oval(b,0,20,0,23,20,23,campaign_mat(0xbca68a,.6f));box(b,0,2,0,53,4,53,stone);box(b,0,41,0,3,8,3,dark);}
      else{box(b,0,17,0,58,34,42,stone);box(b,0,36,0,64,4,46,red);box(b,18,43,0,5,14,5,dark);}
      paved_strip(root,x-45,x+45,z-65,z+65,road);
    } else if(style==4){float h=110+(i*71%240);cy=h*.5f;radius=60;facade_box(b,0,h*.5f,0,75,h,95,architecture[3],1,h/90);box(b,0,h+8,0,55,16,70,stone);for(int row=0;row<6;row++)box(b,0,20+row*(h-25)/6,48,65,2,1,t3_mesh_basic_material_new(i%2?0xe8ae62:0x6dc8db));}
    else if(style==5){
      if(i<3){box(b,0,9,0,34,18,30,stone);box(b,0,32,0,5,48,5,dark);oval(b,0,58,0,17,12,5,snow);cy=35;}
      else if(i<=5){make_kremlin_landmark(i,b,x,y,z);continue;}
      else{facade_box(b,0,25,0,28,50,28,architecture[0],1.5f,3);box(b,0,52,0,34,4,34,snow);cone(b,0,72,0,22,38,green);box(b,0,95,0,1.4f,14,1.4f,gold);cy=40;}
    } else if(style==6){
      box(b,0,16,0,70,32,60,i%2?green:red);for(int s=-1;s<=1;s+=2){t3_object *r=box(b,s*18,40,0,42,3,67,snow);t3_object_set_rotation(r,0,0,-s*.45f);}
      box(b,0,12,30.5f,14,24,1,gold);box(b,23,49,-15,9,24,9,red);for(int j=0;j<3;j++)box(b,-24+j*24,24,31,5,6,1,t3_mesh_basic_material_new(0xffd580));
      if(i<3){oval(b,0,53,0,10,12,10,t3_mesh_basic_material_new(0x5ce5db));cy=35;}
      /* Elf gun crew: coat, face and pointed red hat beside a twin AA barrel. */
      box(root,x+52,y+2,z,14,4,14,dark);box(root,x+52,y+6,z,3,6,3,green);oval(root,x+52,y+11,z,2,2,2,campaign_mat(0xe1b691,0));cone(root,x+52,y+15,z,2.5f,5,red);
      for(int s=-1;s<=1;s+=2){t3_object *gun=box(root,x+55+s*2,y+9,z-5,1,1,15,dark);t3_object_set_rotation(gun,-.4f,0,0);}
      for(int j=0;j<4;j++){float gx=x-55+j*15;box(root,gx,y+2,z+55,9,4,9,j%2?red:green);box(root,gx,y+2.2f,z+55,1,4.6f,9.5f,gold);}
    } else {
      radius=80;cy=35;box(b,0,7,0,120,7,100,dark);for(int j=0;j<4;j++)box(b,j&1?46:-46,-2,j&2?36:-36,10,28,10,red);
      box(b,-25,25,0,46,30,56,stone);box(b,32,44,-20,7,70,7,gold);box(b,15,75,-20,42,4,4,gold);oval(b,26,22,22,17,13,17,red);
      box(b,-25,42,0,52,4,62,snow);for(int j=0;j<2;j++)box(b,-28+j*16,55,-8,5,24,5,dark);
    }
    if(style==4||style==5){float street=roundf(x/500)*500;paved_strip(root,fminf(x,street)-7,fmaxf(x,street)+7,z-7,z+7,road);}
    finish_structure(first+i,b,x,y+cy,z,radius);
  }
  if(style==2||style==6){for(int i=0;i<42;i++){float x=-2600+(i*937%5200),z=-3200+(i*653%6200);fir_tree(root,x,z,style==6?24+(i%4)*5:30+(i%5)*7,style==6);}}
  if(style==6){for(int i=0;i<16;i++){float x=-1400+i*150,z=-680,y=terrain_height(x,z);box(root,x,y+9,z,2,18,2,snow);for(int k=0;k<4;k++)box(root,x,y+2+k*4,z,2.2f,1.8f,2.2f,red);oval(root,x+3,y+18,z,4,4,1.2f,red);}}
  make_land_horizon(style);
}

#include "map-details.h"

static void campaign_start(void) {
  impact_clear();breakup_reset();capital_reset();santa_shielded=1;
  G.level=(G.level%MISSIONS+MISSIONS)%MISSIONS;
  prefs.progress=(prefs.progress&255u)|((uint32_t)G.level<<8);cw_save_commit();
  static const int map[]={1,2,3,0,4,5,6,7};int style=map[G.level];
  if(style!=G.map_style){G.map_style=style;paint_landscape(true);cw_terrain_sync_physics(G.terrain);}
  make_distinct_map(style);bool fresh=!districts[style].body;make_district(style);if(style==5&&fresh){make_kremlin_walls(G.decor[style]);make_kremlin_courtyard(G.decor[style]);}make_map_details(style);reset_district(style);map_details_reset();cw_debug_i32("map.district_buildings",&districts[style].count);cw_debug_i32("map.district_alive",&districts[style].alive);cw_debug_f32_array("map.first_building_bounds",&districts[style].solid[0].box.min.x,6);G.airfield->visible=style==0;
  for(int i=0;i<8;i++){if(G.decor[i])G.decor[i]->visible=i==style;if(G.horizon[i])G.horizon[i]->visible=i==style;}
  cw_node_set(cw_node_get(cw_tree_root(G.tree),"Coast/Ocean"),"visible",cw_val_bool(style==0||style==7));
  /* One photographic lighting treatment per landscape. Keep the key light
   * directional, with cool skylight in the shadows instead of white fill. */
  static const struct {float hour,north,sun,ambient,hemi,haze,mie,exposure;}
  looks[]={
    {15.1f,110,4.3f,.075f,.30f,.000040f,.32f,1.08f}, /* island: clear maritime afternoon */
    {16.0f, 65,4.4f,.065f,.30f,.000038f,.32f,1.10f}, /* fields: raking late summer light */
    { 9.2f, 85,4.5f,.065f,.36f,.000032f,.24f,1.02f}, /* alpine: cold, crystalline morning */
    {16.7f, 70,4.8f,.065f,.27f,.000045f,.42f,1.10f}, /* canyon: copper light / blue shade */
    {16.0f, 95,4.3f,.085f,.33f,.000030f,.30f,1.10f}, /* city: warm facade edges */
    { 9.6f, 80,4.2f,.080f,.34f,.000037f,.30f,1.02f}, /* winter capital */
    { 8.7f, 95,4.5f,.085f,.40f,.000040f,.36f,1.05f}, /* polar: pale dawn */
    {16.4f,105,4.5f,.070f,.32f,.000032f,.34f,1.10f}  /* open ocean */
  };
  cw_node root=cw_tree_root(G.tree),sky=cw_node_get(root,"Coast/Sky");
  cw_node_set(sky,"time_of_day",cw_val_float(looks[style].hour));
  cw_node_set(sky,"north",cw_val_float(looks[style].north));
  cw_node_set(sky,"sun_energy",cw_val_float(looks[style].sun));
  cw_node_set(sky,"mie",cw_val_float(looks[style].mie*.45f));
  cw_node_set(sky,"rayleigh",cw_val_float(style==2||style==6?.75f:.65f));
  cw_node_set(sky,"sky_energy",cw_val_float(1.45f));
  cw_node_set(sky,"ground_color",cw_val_color(.12f,.24f,.38f,1));
  cw_node env=cw_node_get(root,"Coast/Environment");
  cw_node_set(env,"ambient_energy",cw_val_float(looks[style].ambient));
  cw_node_set(env,"hemi_energy",cw_val_float(looks[style].hemi));
  cw_node_set(env,"sky_color",cw_val_color(.43f,.62f,.91f,1));
  cw_node_set(env,"ground_color",style==3?cw_val_color(.32f,.19f,.12f,1):cw_val_color(.19f,.23f,.18f,1));
  cw_node haze=cw_node_get(root,"Coast/Haze");
  cw_node_set(haze,"density",cw_val_float(looks[style].haze));
  cw_node_set(haze,"use_sky_color",cw_val_bool(false));
  cw_node_set(haze,"color",style==3?cw_val_color(.36f,.40f,.49f,1):cw_val_color(.25f,.42f,.64f,1));
  cw_node_set(haze,"volumetric_ambient",cw_val_float(.45f));
  cw_node_set(cw_node_get(root,"Coast/Post"),"exposure",cw_val_float(looks[style].exposure));
  for(int i=0;i<BUILDINGS;i++)if(G.buildings[i].node) {
    bool active=style==0?i<28:style==1?i>=28&&i<36:style==2?i>=36&&i<44:style==3?i>=44&&i<52:style==4?i>=52&&i<68:style==5?i>=68&&i<84:style==6?i>=84&&i<88:i>=88;
    G.buildings[i].hp=active?(i==71?24:8):0;G.buildings[i].node->visible=active;G.buildings[i].ruin->visible=false;
  }
  G.balloon_kills=G.blimp_kills=G.mission_complete=0;G.shields_left=G.level==4?4:0;G.mission_time=0;G.boss_cool=3;
  for(int i=0;i<AIR_TARGETS;i++) {
    air_target *a=&G.air_targets[i];if(cw_body3d_valid(a->body))cw_body3d_destroy(a->body);a->body=(cw_body3d){0};
    bool active=G.level==0?i<BALLOONS:G.level==1?i>=BALLOONS&&i<SHIELD_FIRST:G.level==4?i>=SHIELD_FIRST&&i<=MOTHER:G.level==6?i==SANTA:false;
    a->state=active?0:4;a->credited=0;a->age=a->vy=0;a->hull_hp=active?(a->kind==0?1:a->kind==1?9:a->kind==2?6:a->kind==4?30:24):0;a->basket_hp=active&&(a->kind<2)?(a->kind==0?1:3):0;
    if(i<BALLOONS){
      /* Third column is envelope centre above the ground. The low baskets
       * remain visible from a strafing pass, and strike the field quickly. */
      static const float sites[BALLOONS][3]={{-850,-650,155},{-1650,-900,125},{-450,-1150,115},{-1150,-1550,135},{-2100,-2000,240},{-700,-2350,370},{-2100,300,160},{-1400,100,140},{-300,-150,210},{-2500,-650,300},{-100,-2300,420},{-2300,1500,180}};
      a->x=sites[i][0];a->z=sites[i][1];a->y=terrain_height(a->x,a->z)+sites[i][2];a->by=a->y-62;
    }
    else if(i<SHIELD_FIRST){int n=i-BALLOONS;a->z=-2300+(n/2)*1250;a->x=-600+260*sinf(a->z*.0006f)+(n%2?450:-450);a->y=terrain_height(a->x,a->z)+260+(n%3)*140;a->by=a->y-45;}
    else if(i<MOTHER){float angle=(i-SHIELD_FIRST)*PI/2;a->x=-900+sinf(angle)*440;a->z=-1400+cosf(angle)*440;a->y=1355;a->by=a->y;}
    else if(i==SANTA){a->x=-160;a->z=-220;a->y=terrain_height(a->x,a->z)+40;a->by=a->y;}
    else{a->x=-900;a->z=-1400;a->y=1250;a->by=1198;}
    t3_object_set_rotation(a->root,0,0,0);if(a->kind<2)t3_object_set_rotation(a->basket,0,0,0);
    a->root->visible=active;a->envelope->visible=true;a->basket->visible=active&&a->basket_hp>0;a->ropes->visible=true;
    t3_object_set_position(a->root,a->x,a->y,a->z);if(a->kind<3)t3_object_set_position(a->basket,a->x,a->by,a->z);
    if(a->shield)a->shield->visible=active;
  }
  static const float starts[8][3]={{-900,350,650},{-600,900,1400},{0,550,1800},{-65,130,550},{-900,1250,1000},{-650,330,950},{-800,350,1400},{0,350,1500}};
  G.px=starts[G.level][0];G.py=starts[G.level][1];G.pz=starts[G.level][2];
  if(G.level==4){G.px=-900;G.py=1250;G.pz=1000;}
  static const char *brief[]={"CONTROL: BASKET HITS PAY A PRECISION BONUS.","CONTROL: PROP FIGHTERS AND AIRSHIPS. HAVE FUN.","CONTROL: PICK A FUEL TANK. WATCH THE SECONDARIES.","CONTROL: INTERCEPTORS INBOUND. OWN THE SKY.","CONTROL: BREAK THE GENERATORS ABOVE ITS SHIELD.","CONTROL: THREE RADARS. ONE COMMAND PALACE.","CONTROL: TAKE OUT THE CORES. THEN SANTA.","CONTROL: FOUR RIGS. MAKE THE OCEAN GLOW."};
  notify(brief[G.level],6);G.spawn=G.level==1?90:200;
}
static bool air_target_position(int id,float *x,float *y,float *z) {
  int n=id-ENEMIES-BUILDINGS;if(n<0||n>=AIR_TARGETS*2)return false;
  air_target *a=&G.air_targets[n/2];int basket=n%2;
  if(a->state!=0||!(basket?a->basket_hp:a->hull_hp))return false;
  if((a->kind==3&&G.shields_left)||(a->kind==4&&santa_protected()))return false;
  *x=a->x;*z=a->z;*y=basket?a->by:a->y+(a->kind==3?-52:0);return true;
}
static int air_target_hit(float ax,float ay,float az,float bx,float by,float bz,bool missile) {
  int best=-1;float nearest=1e9f;
  for(int i=0;i<AIR_TARGETS*2;i++) {
    int id=ENEMIES+BUILDINGS+i;float x,y,z;if(!air_target_position(id,&x,&y,&z))continue;
    air_target *a=&G.air_targets[i/2];bool basket=i%2;
    float rx=basket?(a->kind==1?9:4):a->kind==0?47:a->kind==1?35:a->kind==2?19:a->kind==4?25:56;
    float ry=basket?(a->kind==1?6:4):a->kind==0?44:a->kind==1?33:a->kind==2?20:a->kind==4?48:20;
    float rz=basket?(a->kind==1?19:4):a->kind==0?47:a->kind==1?105:a->kind==2?19:a->kind==4?25:56;
    if(missile){rx+=3;ry+=3;rz+=3;}
    float distance=segment_distance2((ax-x)/rx,(ay-y)/ry,(az-z)/rz,(bx-x)/rx,(by-y)/ry,(bz-z)/rz,0,0,0);
    float along=(x-ax)*(x-ax)+(y-ay)*(y-ay)+(z-az)*(z-az);
    if(distance<1&&along<nearest){nearest=along;best=id;}
  }
  return best;
}
static void air_target_damage(int id,bool missile) {
  int n=id-ENEMIES-BUILDINGS;if(n<0||n>=AIR_TARGETS*2)return;air_target *a=&G.air_targets[n/2];bool basket=n%2;
  if(a->state||(a->kind==3&&G.shields_left)||(a->kind==4&&santa_protected()))return;
  int32_t *hp=basket?&a->basket_hp:&a->hull_hp;*hp-=missile?3:1;
  explode(a->x,basket?a->by:a->y,a->z);if(*hp>0)return;*hp=0;
  blast_rumble(a->x,basket?a->by:a->y,a->z,a->kind>=3?1:.8f); /* a target goes down */
  if(a->kind==2){a->state=3;a->root->visible=false;G.shields_left--;G.score+=400;notify(G.shields_left?"SHIELD GENERATOR DESTROYED":"SHIELD COLLAPSED / CORE EXPOSED",4);return;}
  if(!a->credited){a->credited=1;if(a->kind==0)G.balloon_kills++;if(a->kind==1)G.blimp_kills++;G.score+=basket?350:200;}
  a->ropes->visible=false;a->age=0;
  if(a->kind==3){a->state=3;capital_explode(a);a->envelope->visible=false;a->basket->visible=false;return;}
  if(a->kind==4){a->state=3;santa_explode(a);a->root->visible=a->envelope->visible=a->shield->visible=false;G.score+=1500;ground_explosion(a->x,a->y-35,a->z,true);for(int j=0;j<6;j++)explode(a->x+sinf(j*2.4f)*17,a->y+(j%3)*14,a->z+cosf(j*2.4f)*13);return;}
  if(basket){a->state=1;a->basket->visible=false;a->vy=a->kind==0?50:15;notify("BASKET DESTROYED / ENVELOPE AWAY",3);}
  else {
    a->state=2;a->envelope->visible=false;
    if(a->kind==1)a->root->visible=false;
    cw_body3d_def body=cw_body3d_default_def();body.position=(t3_vec3){a->x,a->by,a->z};body.linear_velocity=(t3_vec3){5,-55,8};body.angular_velocity=(t3_vec3){.6f,.3f,.7f};body.bullet=true;body.linear_damping=.05f;
    a->body=cw_body3d_create(G.wreck_world,&body);cw_shape3d_def shape=cw_shape3d_default_def();shape.density=12;cw_shape3d_box(a->body,&shape,(t3_vec3){a->kind==1?7:3,2,a->kind==1?17:3},cw_xform3_identity());cw_body3d_bind_object(a->body,a->basket);
    notify("ENVELOPE BURST / BASKET FALLING",3);
  }
}
static void campaign_update(float dt) {
  if(G.state==TITLE||G.state==SETTINGS||G.state==CONTROLS||G.state==PAUSED)return;
  if(G.state==PLAY)G.mission_time+=dt;capital_update(dt);map_details_update();
  bool physics=false;
  for(int i=0;i<AIR_TARGETS;i++) {
    air_target *a=&G.air_targets[i];if(a->state==4)continue;
    if(a->state==1){a->age+=dt;a->vy=fminf(140,a->vy+dt*32);a->y+=a->vy*dt;t3_object_set_position(a->root,a->x,a->y,a->z);t3_object_set_rotation(a->root,0,0,sinf(a->age*3)*.12f);if(a->age>15){a->root->visible=false;a->state=3;}}
    if(a->state==2&&cw_body3d_valid(a->body)){physics=true;a->age+=dt;}
    if(a->state==0&&a->kind==1){a->x+=sinf(G.mission_time*.07f+i)*dt*.4f;a->z+=dt*2;a->by=a->y-45;}
    if(a->state==0){t3_object_set_position(a->root,a->x,a->y,a->z);if(a->kind<2)t3_object_set_position(a->basket,a->x,a->by,a->z);}
    if(a->kind==4)santa_update(a,dt);
    if(a->shield){a->shield->visible=a->state==0&&(a->kind==4?santa_protected():G.shields_left>0);a->age=fmaxf(0,a->age-dt);((t3_mesh *)a->shield)->materials[0]->opacity=a->kind==4?.07f+a->age*.35f:.13f+a->age;}
  }
  if((capital.active&&capital.fragments)||breakup_count)physics=true;
  if(physics) {
    /* Wreck update already stepped this world while a crash is active. */
    if(!G.wreck_count||G.wreck_age>=20){cw_world3d_step(G.wreck_world,dt);cw_world3d_sync(G.wreck_world,1);}
    for(int i=0;i<SHIELD_FIRST;i++){air_target *a=&G.air_targets[i];if(a->state!=2)continue;a->by=a->basket->position.y;
      if(a->by<terrain_height(a->basket->position.x,a->basket->position.z)+5||a->age>18){ground_explosion(a->basket->position.x,fmaxf(0,terrain_height(a->x,a->z)),a->basket->position.z,false);a->basket->visible=false;cw_body3d_destroy(a->body);a->body=(cw_body3d){0};a->state=3;}}
  }
  if((G.level==5||G.level==6||G.level==7)&&G.state==PLAY){G.boss_cool-=dt;if(G.boss_cool<=0){int first=G.level==5?68:G.level==6?84:88;for(int i=first;i<first+3;i++)if(G.buildings[i].hp>0){enemy gun={0};gun.x=G.buildings[i].x+52;gun.y=terrain_height(gun.x,G.buildings[i].z)+9;gun.z=G.buildings[i].z-12;enemy_fire(&gun,false);}G.boss_cool=G.level==6?1.6f:2.2f;}}
  if(G.level==4&&G.state==PLAY&&G.air_targets[MOTHER].state==0) {
    G.boss_cool-=dt;if(G.boss_cool<=0){enemy turret={0};turret.x=-900;turret.y=1180;turret.z=-1400;enemy_fire(&turret,false);G.boss_cool=.45f;}
  }
}
static void victory_camera(float dt) {
  victory_age+=dt;air_target *a=&G.air_targets[G.level==4?MOTHER:SANTA];
  bool ship=G.level==4;float range=ship?1600+victory_age*28:400+victory_age*24,angle=.25f+victory_age*.035f;
  t3_vec3 target={0};int count=ship?8:6;
  for(int i=0;i<count;i++){t3_vec3 p=(ship?capital.chunks[i]:santa_parts[i])->position;target.x+=p.x/count;target.y+=p.y/count;target.z+=p.z/count;}
  target.y=fmaxf(target.y,terrain_height(a->x,a->z)+28);
  cw_node3d_set_position(G.camera_node,(t3_vec3){target.x+sinf(angle)*range,target.y+(ship?360:160),target.z+cosf(angle)*range});
  cw_node3d_look_at(G.camera_node,target,(t3_vec3){0,1,0});
  G.audio_boost=G.audio_alert=G.audio_engine=0;
  if(victory_age>=(ship?18:14)){G.state=WIN;G.menu_row=0;}
}
static void campaign_finish(void) {
  bool done=G.level==0?G.balloon_kills>=BALLOONS:G.level==1?G.kills>=8&&G.blimp_kills>=BLIMPS:G.level==2?G.fuel_depots>=4:G.level==3?G.kills>=10:G.level==4?G.air_targets[MOTHER].state==3:G.level==5?mission_ground_count(68,71)==4:G.level==6?mission_ground_count(84,86)==3&&G.air_targets[SANTA].state==3:mission_ground_count(88,91)==4;
  if(done&&G.state==PLAY){G.state=(G.level==4||G.level==6)?VICTORY:WIN;victory_age=0;G.menu_row=0;G.mission_complete=1;prefs.progress|=1u<<G.level;cw_save_commit();G.score+=1000;G.audio_boost=0;G.audio_alert=0;notify("MISSION COMPLETE",6);}
}

static bool campaign_shield_hit(float ax,float ay,float az,float bx,float by,float bz) {
  air_target *a;float rx,ry,rz;
  if(G.level==4&&G.shields_left){a=&G.air_targets[MOTHER];rx=565;ry=100;rz=565;}
  else if(G.level==6&&G.air_targets[SANTA].state==0&&santa_protected()){a=&G.air_targets[SANTA];rx=34;ry=56;rz=32;}
  else return false;
  if(segment_distance2((ax-a->x)/rx,(ay-a->y)/ry,(az-a->z)/rz,(bx-a->x)/rx,(by-a->y)/ry,(bz-a->z)/rz,0,0,0)>1)return false;
  a->age=.25f;explode(bx,by,bz);return true;
}
static bool campaign_aircraft_collision(t3_vec3 p) {
  if(district_hit(p,(t3_vec3){G.px,G.py,G.pz},5,true))return true;
  for(int i=0;i<AIR_TARGETS;i++) {
    air_target *a=&G.air_targets[i];if(a->state)continue;
    float rx=a->kind==0?48:a->kind==1?40:a->kind==2?23:a->kind==4?26:545;
    float ry=a->kind==0?48:a->kind==1?38:a->kind==2?24:a->kind==4?48:55;
    float rz=a->kind==0?48:a->kind==1?110:a->kind==2?23:a->kind==4?26:545;
    if(segment_distance2((p.x-a->x)/rx,(p.y-a->y)/ry,(p.z-a->z)/rz,(G.px-a->x)/rx,(G.py-a->y)/ry,(G.pz-a->z)/rz,0,0,0)<1) {
      air_target_damage(ENEMIES+BUILDINGS+i*2,true);return true;
    }
  }
  return false;
}
static int campaign_waypoint(void) {
  int best=-1;float nearest=1e30f;
  for(int id=0;id<TARGET_COUNT;id++) {
    if(id>=ENEMIES&&id<ENEMIES+BUILDINGS){int b=id-ENEMIES;bool goal=G.level==2?b>=44&&b<=47:G.level==5?b>=68&&b<=71:G.level==6?b>=84&&b<=86:G.level==7?b>=88&&b<=91:false;if(!goal)continue;}
    if(id<ENEMIES&&(G.level==0||G.level==2||G.level>=5||(G.level==1&&G.kills>=8)||G.level==4))continue;
    if(id>=ENEMIES+BUILDINGS&&(id-ENEMIES-BUILDINGS)%2)continue;
    float x,y,z;if(!target_position(id,&x,&y,&z))continue;
    float d=(x-G.px)*(x-G.px)+(y-G.py)*(y-G.py)+(z-G.pz)*(z-G.pz);
    if(d<nearest){nearest=d;best=id;}
  }
  return best;
}
