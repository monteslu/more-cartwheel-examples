/* Solid tree geometry with instancing, species variation and GPU wind.
 * All crowns and individual silhouette leaves have depth: no billboards. */
#include "tree-meshes.h"
#include "cartwheel/shading.h"
static t3_geometry *forest_geometry[2][8][2];
static t3_material *forest_bark,*forest_leaf[4][2];
static float forest_clock,forest_samples[4][4],forest_height_range[2];
static int32_t forest_grid=1; /* profiling override: one, two or four cells per axis */
static int32_t forest_count,forest_species,forest_near_count,forest_far_count,forest_triangles;
struct tree_placement {float x,y,z,h,sx,sz,angle,tint;int model,lod,cell;};
static struct forest_batch {struct tree_placement *trees;int count;t3_instanced_mesh *mesh[16][2][8][2];t3_sphere bounds[16];} forests[8];
static void forest_lod_update(void);
static t3_renderer *forest_renderer;
static void forest_uniforms(t3_extension_ctx *ctx,void *user){(void)user;t3_extension_set_uniform(ctx,"forestTime",&forest_clock,1);}
static void forest_frame(float dt) {
  if(G.state!=PAUSED)forest_clock+=dt;
  if(G.state==PLAY&&G.ticks%12==0)forest_lod_update();
  t3_renderer *r=cw_render3d_renderer(G.tree);
  if(r&&r!=forest_renderer){forest_renderer=r;cw_shading_part(r,"foliage",forest_uniforms,NULL);cw_shading_part_batching(r,"foliage",true);}
}
static void forest_setup(void) {
  forest_frame(0);if(forest_bark)return;
  static cw_res textures[4];static const char *paths[]={"bark","oak","needles","snow"};t3_texture *maps[4];
  for(int i=0;i<4;i++){char name[96];snprintf(name,sizeof name,"res://textures/tree-%s-v1.jpg",paths[i]);textures[i]=cw_res_load(name,CW_RES_TEXTURE);maps[i]=cw_res_texture(textures[i]);maps[i]->wrap_s=maps[i]->wrap_t=T3_REPEAT;maps[i]->anisotropy=8;}
  forest_bark=campaign_mat(0x8b8171,0);forest_bark->roughness=.94f;t3_material_set_map(forest_bark,maps[0]);
  for(int species=0;species<4;species++)for(int snowy=0;snowy<2;snowy++){
    t3_material *m=forest_leaf[species][snowy]=campaign_mat(snowy?0xffffff:species==1?0xa4b87c:species<2?0x96ad72:0xa3b69a,0);m->roughness=.9f;
    t3_material_set_map(m,maps[snowy?3:species<2?1:2]);cw_shading_add(m,"foliage");
  }
  for(int lod=0;lod<2;lod++)for(int model=0;model<8;model++)for(int part=0;part<2;part++){
    const struct tree_mesh_data *d=&tree_data[lod*16+model*2+part];t3_geometry *g=forest_geometry[lod][model][part]=t3_geometry_new();
    t3_attribute *p=t3_attribute_new(T3_FLOAT32,d->p,d->vertices,3),*n=t3_attribute_new(T3_FLOAT32,d->n,d->vertices,3),*uv=t3_attribute_new(T3_FLOAT32,d->uv,d->vertices,2),*ix=t3_attribute_new(T3_UINT32,d->ix,d->indices,1);
    t3_geometry_set_attribute(g,T3_ATTR_POSITION,p);t3_geometry_set_attribute(g,T3_ATTR_NORMAL,n);t3_geometry_set_attribute(g,T3_ATTR_UV,uv);t3_geometry_set_index(g,ix);t3_release(p);t3_release(n);t3_release(uv);t3_release(ix);
  }
  static const char *names[]={"forest.oak","forest.birch","forest.spruce","forest.pine"};for(int i=0;i<4;i++)cw_debug_f32_array(names[i],forest_samples[i],4);cw_debug_f32_array("forest.height_range",forest_height_range,2);
  cw_debug_i32("forest.trees",&forest_count);cw_debug_i32("forest.species",&forest_species);cw_debug_f32("forest.wind_time",&forest_clock);cw_debug_i32("forest.near",&forest_near_count);cw_debug_i32("forest.far",&forest_far_count);cw_debug_i32("forest.triangles",&forest_triangles);
}
static void forest_single(t3_object *parent,float x,float z,float height,int species,bool snowy) {
  forest_setup();int model=species*2+((int)fabsf(x*3+z)&1);float y=terrain_height(x,z),angle=x*.031f+z*.012f;
  for(int part=0;part<2;part++){t3_mesh *m=t3_mesh_new(forest_geometry[0][model][part],part?forest_leaf[species][snowy]:forest_bark);m->base.cast_shadow=m->base.receive_shadow=true;t3_object_set_position(m,x,y,z);t3_object_set_scale(m,height,height,height);t3_object_set_rotation(m,0,angle,0);t3_object_add(parent,(t3_object *)m);t3_release(m);}
}
static bool forest_clear(float x,float z,int style) {
  if(terrain_height(x,z)<8)return false;
  if(style==6&&(hypotf(x,z)<150||hypotf(x+160,z+220)<140))return false;
  if(style==6)for(int i=84;i<=86;i++)if(fabsf(x-G.buildings[i].x)<65&&z>G.buildings[i].z-100&&z<G.buildings[i].z+1100)return false;
  if(style==0&&x>-190&&x<200&&z>-1900&&z<720)return false;
  if(style==1){float r=125*sinf(z*.0017f)+60*cosf(z*.0031f);if(fabsf(x+700-r)<30||fabsf(x+1800-r)<28)return false;}
  if(style==2&&fabsf(x+600-260*sinf(z*.0006f))<65)return false;
  if(style==5&&((x>-1750&&x<850&&z>-2700&&z<600)||fabsf(x-950-220*sinf(z*.0007f))<210))return false;
  for(int i=0;i<BUILDINGS;i++){structure *b=&G.buildings[i];if(b->node&&fabsf(x-b->x)<b->radius+14&&fabsf(z-b->z)<b->radius+14)return false;}
  struct district *d=&districts[style];for(int i=0;i<d->count;i++){t3_box3 b=d->solid[i].box;if(x>b.min.x-10&&x<b.max.x+10&&z>b.min.z-10&&z<b.max.z+10)return false;}
  return true;
}
static t3_instanced_mesh *forest_instances(t3_object *root,t3_geometry *g,t3_material *mat,int count,bool shadow,const t3_sphere *bounds) {
  t3_instanced_mesh *m=t3_instanced_mesh_new(g,mat,count);
  m->mesh.base.cast_shadow=shadow;m->mesh.base.receive_shadow=true;
  t3_instanced_mesh_set_bounding_sphere(m,bounds);
  cw_node n=cw_node_new("MeshInstance3D","ForestCell");
  cw_node_set(n,"static",cw_val_bool(true));cw_node_set(n,"occlusion_cull",cw_val_bool(false));
  cw_mesh3d_set_procedural(n,(t3_mesh *)m);cw_node_add_child(cw_tree_root(G.tree),n);
  t3_object *group=cw_node3d_object(n);if(group->parent)t3_object_remove(group->parent,group);t3_object_add(root,group);
  t3_release(m);return m;
}
static void forest_make(t3_object *root,int style,uint32_t *seed) {
  int count=style==1?2200:style==2?4200:style==0?1400:style==6?1200:style==5?650:0;if(!count)return;
  forest_setup();struct forest_batch *f=&forests[style];struct tree_placement *trees=f->trees=malloc(count*sizeof *trees);f->count=count;int counts[16][8]={{0}};int grid=forest_grid>=4?4:forest_grid>=2?2:1;
  for(int i=0;i<count;i++){
    float x=0,z=0;bool clear=false;
    for(int attempt=0;attempt<60&&!clear;attempt++){
      x=-3400+district_rand(seed)*6800;z=-3400+district_rand(seed)*6800;
      if(style==2)x=-600+260*sinf(z*.0006f)+(district_rand(seed)>.5f?1:-1)*(220+district_rand(seed)*1900);
      if(style==1){int grove=i/100;x=-3000+(grove*1237%6000)+(district_rand(seed)-.5f)*320;z=-2800+(grove*1733%5600)+(district_rand(seed)-.5f)*380;}
      clear=forest_clear(x,z,style);
    }
    float species_roll=district_rand(seed);int species=style==2||style==6?(species_roll<.75f?2:3):style==5?(species_roll<.55f?2:1):species_roll<.55f?0:species_roll<.8f?1:3;
    int model=species*2+(district_rand(seed)>.5f);float age=district_rand(seed);float h=species==1?13+age*22:species==2?12+age*27:9+age*23;
    trees[i]=(struct tree_placement){x,clear?terrain_height(x,z)-.2f:-2000,z,h,.76f+district_rand(seed)*.50f,.80f+district_rand(seed)*.45f,district_rand(seed)*2*PI,.78f+district_rand(seed)*.30f,model,1,0};int cx=(int)clampf((x+3400)/(6800/grid),0,grid-1),cz=(int)clampf((z+3400)/(6800/grid),0,grid-1);trees[i].cell=cz*grid+cx;counts[trees[i].cell][model]++;
  }
  /* Freeze each cell's full geometry bounds before LOD subsets change.
   * Transform both LODs, including foliage sway margin; elevation comes
   * from actual placements, so alpine peaks cannot escape their cell. */
  t3_box3 boxes[16];for(int c=0;c<16;c++)boxes[c]=(t3_box3){{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};
  for(int i=0;i<count;i++){struct tree_placement *t=&trees[i];if(t->y<0)continue;t3_mat4 transform;
    t3_mat4_compose(&transform,(t3_vec3){t->x,t->y,t->z},t3_quat_from_axis_angle((t3_vec3){0,1,0},t->angle),(t3_vec3){t->h*t->sx,t->h,t->h*t->sz});
    for(int lod=0;lod<2;lod++)for(int part=0;part<2;part++){t3_geometry *g=forest_geometry[lod][t->model][part];if(!g->has_bounding_box)t3_geometry_compute_bounding_box(g);
      for(int k=0;k<8;k++){t3_vec3 p={k&1?g->bounding_box.max.x:g->bounding_box.min.x,k&2?g->bounding_box.max.y:g->bounding_box.min.y,k&4?g->bounding_box.max.z:g->bounding_box.min.z};p=t3_vec3_apply_mat4(p,&transform);t3_box3 *b=&boxes[t->cell];
        b->min.x=fminf(b->min.x,p.x-3);b->min.y=fminf(b->min.y,p.y-3);b->min.z=fminf(b->min.z,p.z-3);b->max.x=fmaxf(b->max.x,p.x+3);b->max.y=fmaxf(b->max.y,p.y+3);b->max.z=fmaxf(b->max.z,p.z+3);
      }
    }
  }
  for(int c=0;c<16;c++){t3_box3 b=boxes[c];if(b.min.x>b.max.x)f->bounds[c]=(t3_sphere){{0,0,0},1};else {t3_vec3 center=t3_vec3_scale(t3_vec3_add(b.min,b.max),.5f);f->bounds[c]=(t3_sphere){center,t3_vec3_length(t3_vec3_sub(b.max,center))};}}
  for(int cell=0;cell<16;cell++)for(int model=0;model<8;model++)if(counts[cell][model]){
    int species=model/2;bool snowy=style==2||style==5||style==6;
    for(int lod=0;lod<2;lod++){
      f->mesh[cell][lod][model][0]=forest_instances(root,forest_geometry[lod][model][0],forest_bark,counts[cell][model],lod==0,&f->bounds[cell]);
      f->mesh[cell][lod][model][1]=forest_instances(root,forest_geometry[lod][model][1],forest_leaf[species][snowy],counts[cell][model],lod==0,&f->bounds[cell]);
    }
  }
  forest_lod_update();
}
static void forest_lod_update(void) {
  int style=G.map_style;struct forest_batch *f=&forests[style];if(!f->trees)return;
  int used[16][2][8]={0};memset(forest_samples,0,sizeof forest_samples);forest_height_range[0]=1000;forest_height_range[1]=0;forest_count=f->count;forest_species=forest_near_count=forest_far_count=forest_triangles=0;
  for(int i=0;i<f->count;i++){
    struct tree_placement *t=&f->trees[i];if(t->y<0)continue;int species=t->model/2;if(t->y>0){forest_height_range[0]=fminf(forest_height_range[0],t->h);forest_height_range[1]=fmaxf(forest_height_range[1],t->h);if(!forest_samples[species][3]){float *sample=forest_samples[species];sample[0]=t->x;sample[1]=t->y;sample[2]=t->z;sample[3]=t->h;}}float dx=t->x-G.px,dz=t->z-G.pz,distance2=dx*dx+dz*dz;
    if(t->lod&&distance2<900*900)t->lod=0;else if(!t->lod&&distance2>1100*1100)t->lod=1;
    int lod=t->lod,model=t->model,at=used[t->cell][lod][model]++;forest_triangles+=(tree_data[lod*16+model*2].indices+tree_data[lod*16+model*2+1].indices)/3;forest_species|=1<<(model/2);
    if(lod)forest_far_count++;else forest_near_count++;
    float h=t->h;t3_instanced_mesh *bark=f->mesh[t->cell][lod][model][0],*leaves=f->mesh[t->cell][lod][model][1];
    instance_place(bark,at,t->x,t->y,t->z,h*t->sx,h,h*t->sz,t->angle);
    float foliage=style==5&&model/2==1?0:1;
    instance_place(leaves,at,t->x,t->y,t->z,h*t->sx*foliage,h*foliage,h*t->sz*foliage,t->angle);
    t3_instanced_mesh_set_color_at(leaves,at,(t3_color){t->tint,t->tint,t->tint*.96f});
  }
  for(int cell=0;cell<16;cell++)for(int lod=0;lod<2;lod++)for(int model=0;model<8;model++)for(int part=0;part<2;part++)if(f->mesh[cell][lod][model][part]){
    t3_instanced_mesh *m=f->mesh[cell][lod][model][part];m->count=used[cell][lod][model];m->instance_matrix->version++;if(m->instance_color)m->instance_color->version++;
  }
}
