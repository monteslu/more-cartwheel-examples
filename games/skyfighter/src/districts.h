/* Instanced scenery keeps dense settlements affordable on the cabinet GPU.
 * Buildings retain individual hit volumes and can be shot or crashed into. */
#define DISTRICT_SOLIDS 800
static struct district {
  t3_instanced_mesh *body,*roof;
  struct {t3_box3 box; t3_mat4 body,roof;t3_object *custom;int alive;} solid[DISTRICT_SOLIDS];
  int count,alive;
} districts[8];
static t3_material *architecture[4];
static void architecture_load(void) {
  static cw_res resources[4];static const char *names[]={"kremlin-brick","palace-facade","city-stone","city-glass"};
  for(int i=0;i<4;i++){char path[128];snprintf(path,sizeof path,"res://textures/%s.jpg",names[i]);resources[i]=cw_res_load(path,CW_RES_TEXTURE);t3_texture *tex=cw_res_texture(resources[i]);tex->wrap_s=tex->wrap_t=T3_REPEAT;tex->anisotropy=8;architecture[i]=campaign_mat(0xffffff,i==3?.22f:0);t3_material_set_map(architecture[i],tex);if(i==3)architecture[i]->roughness=.52f;}
}
static t3_object *facade_box(t3_object *parent,float x,float y,float z,float w,float h,float d,t3_material *mat,float repeatx,float repeaty) {
  t3_object *o=box(parent,x,y,z,w,h,d,mat);t3_attribute *uv=((t3_mesh *)o)->geometry->attributes[T3_ATTR_UV];float *a=uv->array;for(int i=0;i<uv->count;i++){a[i*2]*=repeatx;a[i*2+1]*=repeaty;}uv->version++;return o;
}
static float district_rand(uint32_t *seed) {*seed=*seed*1664525u+1013904223u;return (*seed>>8)/16777216.0f;}
static t3_instanced_mesh *district_instances(t3_object *root,t3_geometry *g,t3_material *mat,int count,bool shadow) {
  t3_instanced_mesh *m=t3_instanced_mesh_new(g,mat,count);m->mesh.base.cast_shadow=shadow;m->mesh.base.receive_shadow=true;m->mesh.base.frustum_culled=false;t3_object_add(root,(t3_object *)m);return m;
}
static void instance_place(t3_instanced_mesh *mesh,int i,float x,float y,float z,float sx,float sy,float sz,float angle) {
  t3_mat4 matrix;t3_mat4_compose(&matrix,(t3_vec3){x,y,z},t3_quat_from_axis_angle((t3_vec3){0,1,0},angle),(t3_vec3){sx,sy,sz});t3_instanced_mesh_set_matrix_at(mesh,i,&matrix);
}
#include "forest.h"
static t3_geometry *district_gable(void) {
  static const float vertices[]={-.5f,0,.5f,.5f,0,.5f,0,1,.5f, .5f,0,-.5f,-.5f,0,-.5f,0,1,-.5f,
    -.5f,0,-.5f,-.5f,0,.5f,0,1,-.5f, -.5f,0,.5f,0,1,.5f,0,1,-.5f,
    .5f,0,.5f,.5f,0,-.5f,0,1,.5f, .5f,0,-.5f,0,1,-.5f,0,1,.5f};
  t3_geometry *g=t3_geometry_new();t3_attribute *p=t3_attribute_new(T3_FLOAT32,vertices,18,3);t3_geometry_set_attribute(g,T3_ATTR_POSITION,p);t3_release(p);t3_geometry_compute_vertex_normals(g);return g;
}
static void make_district(int style) {
  struct district *d=&districts[style];if(d->body)return;
  t3_object *root=G.decor[style];if(!root){root=G.decor[style]=t3_group_new();t3_object_add((t3_object *)G.scene,root);}
  uint32_t seed=0x5ca1e123u+style*8191;
  int desired=style==0?180:style==1?100:style==2?170:style==3?135:style==4?680:style==5?580:style==6?150:24;
  t3_geometry *geo=t3_box_geometry_new(1,1,1,1,1,1);t3_material *bodymat=style==4?architecture[3]:style==5||style==2?architecture[2]:style==6?architecture[0]:style==7?campaign_mat(0x727e85,.4f):style==3?architecture[0]:architecture[2];
  bool gabled=style==1||style==2||style==6;t3_geometry *roofgeo=gabled?district_gable():geo;
  d->body=district_instances(root,geo,bodymat,desired,true);d->roof=district_instances(root,roofgeo,campaign_mat(style==1?0x5c5e55:style==3?0x78614b:style==0?0x46525a:style==4?0x34434d:0xc2cdd2,.15f),desired,true);if(gabled)t3_release(roofgeo);t3_release(geo);
  t3_material *road=campaign_mat(style==4?0x29343b:0x44494b,0);
  if(style==4||style==5){for(int i=-7;i<=7;i++){float pos=i*440;
    if(style==5&&pos>-1750&&pos<850){paved_strip(root,pos-14,pos+14,-3600,-2700,road);paved_strip(root,pos-14,pos+14,600,3600,road);}else paved_strip(root,pos-14,pos+14,-3600,3600,road);
    if(style==5&&pos>-2700&&pos<600){paved_strip(root,-3600,-1750,pos-14,pos+14,road);paved_strip(root,850,3600,pos-14,pos+14,road);}else paved_strip(root,-3600,3600,pos-14,pos+14,road);
  }}
  for(int tries=0;d->count<desired&&tries<desired*50;tries++){
    float x,z,w,h,depth;
    if(style==4||style==5){int gx=(int)(district_rand(&seed)*14)-7,gz=(int)(district_rand(&seed)*14)-7;int quarter=(int)(district_rand(&seed)*4);x=gx*440+(quarter&1?295:120);z=gz*440+(quarter&2?295:120);w=75+district_rand(&seed)*55;depth=75+district_rand(&seed)*55;h=style==4?65+powf(district_rand(&seed),2)*360:24+district_rand(&seed)*65;
      if(style==5&&x>-1750&&x<850&&z>-2700&&z<600)continue;if(style==4&&x>-1200&&x< -600&&z>-1700&&z< -1100)h=fminf(h,160);
      if(style==5&&fabsf(x-950-220*sinf(z*.0007f))<190)continue;
    } else if(style==1||style==2||style==3||style==6){z=-3100+district_rand(&seed)*6000;float route=style==1?((tries%2?-1800:-700)+125*sinf(z*.0017f)+60*cosf(z*.0031f)):style==2?-600+260*sinf(z*.0006f):style==3?-450*sinf(z*.0008f):-300;
      x=route+(tries%2?1:-1)*(style==6?180+district_rand(&seed)*1300:60+district_rand(&seed)*200);w=style==6?25+district_rand(&seed)*25:16+district_rand(&seed)*20;depth=w*(1+district_rand(&seed)*.5f);h=style==3?12+district_rand(&seed)*23:8+district_rand(&seed)*15;
    } else if(style==0){x=-1050+district_rand(&seed)*1200;z=-1450+district_rand(&seed)*2200;w=18+district_rand(&seed)*30;depth=22+district_rand(&seed)*25;h=12+district_rand(&seed)*30;if(x>-150&&x<60)continue;}
    else{x=-2900+district_rand(&seed)*5800;z=-3200+district_rand(&seed)*5600;w=16+district_rand(&seed)*12;depth=75+district_rand(&seed)*65;h=8+district_rand(&seed)*5;}
    if(style==6&&(hypotf(x,z)<150||hypotf(x+160,z+220)<140))continue;
    float y=style==7?5:terrain_height(x,z);if(style!=7&&y<8)continue;
    if(style!=7&&fabsf(terrain_height(x+w/2,z)-terrain_height(x-w/2,z))>h*.8f)continue;
    bool overlaps=false;for(int b=0;b<BUILDINGS;b++){structure *s=&G.buildings[b];if(!s->node)continue;float dx=x-s->x,dz=z-s->z;if(style==6&&b>=84&&b<=86&&fabsf(dx)<w*.5f+75&&dz> -80&&dz<1100){overlaps=true;break;}if(fabsf(dx)<w*.5f+s->radius+12&&fabsf(dz)<depth*.5f+s->radius+12){overlaps=true;break;}}
    for(int b=0;b<d->count&&!overlaps;b++){t3_box3 q=d->solid[b].box;if(x+w*.5f+8>q.min.x&&x-w*.5f-8<q.max.x&&z+depth*.5f+8>q.min.z&&z-depth*.5f-8<q.max.z)overlaps=true;}if(overlaps)continue;
    float roofheight=gabled?w*.30f:3;int i=d->count++;d->solid[i].alive=1;
    float tint=district_rand(&seed);
    static const t3_color city_colors[]={{.28f,.48f,.66f},{.65f,.57f,.43f},{.36f,.42f,.46f},{.78f,.76f,.67f},{.25f,.48f,.47f}};
    t3_color facade=style==4?city_colors[i%5]:style==0?(t3_color){.52f+tint*.30f,.56f+tint*.25f,.56f+tint*.22f}:(t3_color){.70f+tint*.25f,.58f+tint*.34f,.43f+tint*.45f};
    t3_instanced_mesh_set_color_at(d->body,i,facade);
    if(style==4||style==0)t3_instanced_mesh_set_color_at(d->roof,i,(t3_color){.65f+tint*.30f,.69f+tint*.25f,.75f+tint*.20f});
    d->solid[i].box=(t3_box3){{x-w/2,y,z-depth/2},{x+w/2,y+h+roofheight,z+depth/2}};
    t3_mat4_compose(&d->solid[i].body,(t3_vec3){x,y+h/2,z},t3_quat_identity(),(t3_vec3){w,h,depth});t3_instanced_mesh_set_matrix_at(d->body,i,&d->solid[i].body);
    t3_mat4_compose(&d->solid[i].roof,(t3_vec3){x,y+h+(gabled?0:1.5f),z},t3_quat_identity(),(t3_vec3){w+2,roofheight,depth+2});t3_instanced_mesh_set_matrix_at(d->roof,i,&d->solid[i].roof);
    if(style==1||style==2||style==3||style==6){float route=style==1?((tries%2?-1800:-700)+125*sinf(z*.0017f)+60*cosf(z*.0031f)):style==2?-600+260*sinf(z*.0006f):style==3?-450*sinf(z*.0008f):-300;paved_strip(root,fminf(x,route)-3,fmaxf(x,route)+3,z-3,z+3,road);}
  }
  /* Hidden unused slots cannot leave origin buildings or shadow casters. */
  t3_mat4 zero;t3_mat4_compose(&zero,(t3_vec3){0,-2000,0},t3_quat_identity(),(t3_vec3){0,0,0});for(int i=d->count;i<desired;i++){t3_instanced_mesh_set_matrix_at(d->body,i,&zero);t3_instanced_mesh_set_matrix_at(d->roof,i,&zero);}
  if(style==4||style==5){
    t3_geometry *far_geo=t3_box_geometry_new(1,1,1,1,1,1);t3_instanced_mesh *far=district_instances(root,far_geo,style==4?architecture[3]:architecture[2],1400,false);t3_release(far_geo);
    for(int i=0;i<1400;i++){float x,z;do{x=-14000+district_rand(&seed)*28000;z=-14000+district_rand(&seed)*28000;}while(fabsf(x)<3700&&fabsf(z)<3700);float h=style==4?80+district_rand(&seed)*290:24+district_rand(&seed)*95;instance_place(far,i,x,landscape_height(x,z)+h/2,z,70+district_rand(&seed)*70,h,70+district_rand(&seed)*70,0);if(style==4)t3_instanced_mesh_set_color_at(far,i,(t3_color){.38f+(i%3)*.08f,.49f+(i%4)*.06f,.58f+(i%5)*.05f});}
  }
  forest_make(root,style,&seed);
  if(style==1){cw_res field_res=cw_res_load("res://textures/farm-field-v1.jpg",CW_RES_TEXTURE);t3_texture *field_map=cw_res_texture(field_res);field_map->wrap_s=field_map->wrap_t=T3_REPEAT;field_map->anisotropy=8;t3_material *field[4]={campaign_mat(0x748456,0),campaign_mat(0x9b895b,0),campaign_mat(0x627a49,0),campaign_mat(0xa39868,0)};for(int i=0;i<4;i++)t3_material_set_map(field[i],field_map);for(int i=0;i<48;i++){float z=-3000+(i/6)*760,x=-3100+(i%6)*860;paved_strip(root,x,x+420,z,z+460,field[i%4]);t3_mesh *mesh=(t3_mesh *)root->children[root->child_count-1];mesh->base.position.y=-.12f;t3_attribute *p=mesh->geometry->attributes[T3_ATTR_POSITION];float *positions=p->array,*uv=malloc(p->count*2*sizeof(float));for(int j=0;j<p->count;j++){uv[j*2]=positions[j*3]*.12f;uv[j*2+1]=positions[j*3+2]*.12f;}t3_attribute *a=t3_attribute_new(T3_FLOAT32,uv,p->count,2);t3_geometry_set_attribute(mesh->geometry,T3_ATTR_UV,a);t3_release(a);free(uv);}}
}
static void reset_district(int style) {
  struct district *d=&districts[style];d->alive=d->count;for(int i=0;i<d->count;i++){d->solid[i].alive=1;if(d->solid[i].custom){d->solid[i].custom->visible=true;continue;}t3_instanced_mesh_set_matrix_at(d->body,i,&d->solid[i].body);t3_instanced_mesh_set_matrix_at(d->roof,i,&d->solid[i].roof);}
}
static void destroy_district_building(int i) {
  struct district *d=&districts[G.map_style];d->solid[i].alive=0;d->alive--;if(d->solid[i].custom){t3_object *node=d->solid[i].custom;north_pole_break(node);node->visible=false;for(int j=0;j<d->count;j++)if(j!=i&&d->solid[j].alive&&d->solid[j].custom==node){d->solid[j].alive=0;d->alive--;}}else{t3_mat4 zero;t3_mat4_compose(&zero,(t3_vec3){0,-2000,0},t3_quat_identity(),(t3_vec3){0,0,0});t3_instanced_mesh_set_matrix_at(d->body,i,&zero);t3_instanced_mesh_set_matrix_at(d->roof,i,&zero);}
  t3_box3 b=d->solid[i].box;ground_explosion((b.min.x+b.max.x)*.5f,b.min.y+3,(b.min.z+b.max.z)*.5f,false);G.buildings_destroyed++;G.score+=50;
}
static bool district_hit(t3_vec3 a,t3_vec3 b,float radius,bool crash) {
  struct district *d=&districts[G.map_style];int best=-1;float near=2;
  for(int i=0;i<d->count;i++)if(d->solid[i].alive){float entry;if(sweep_box(a,b,d->solid[i].box,radius,&entry)&&entry<near){near=entry;best=i;}}
  if(best<0)return false;if(crash)G.impact_structure=1000+best;destroy_district_building(best);return true;
}
