/* The grounding check (tests/skyfighter-grounding.mjs; native development builds, SKY_GROUND_CHECK=1): nothing a mission
 * places on the land or the sea floats. Each prop (a top-level child of the mission's scenery, each instance of an
 * instanced mesh, each ruin) is taken in world space. Its base is the band of vertices within 10 cm of its lowest point;
 * for a box-like base the four corners and a grid between them, otherwise every base vertex and their centre. A base
 * point floats when it is more than 15 cm above the ground under it and not carried by another prop (a roof on its walls,
 * an ornament in its tree's canopy). On the sea the ground is the deepest wave trough, so a hull or a leg must reach below
 * it; past the terrain tile it is the horizon mesh as drawn. Draped surfaces (roads, fields, aprons) are checked vertex by
 * vertex instead, with the small lift they keep against z-fighting. Water and canopies carry but are not checked. */
#define GROUND_TOLERANCE .15f
#define GROUND_SURFACE_LIFT .7f
#define GROUND_SEA_TROUGH -2.0f
static int32_t ground_units,ground_floating,ground_worst_mm,ground_surfaces,ground_surface_floating;
static float ground_worst[3];
typedef struct {t3_box3 box;float base;int first,count,kind;} ground_unit; /* kind 0 solid, 1 a support only (water, a canopy), 2 ruin (checked, never a support), 3 span (a bridge deck: checked at its two ends) */
static struct {ground_unit *units;int n,cap;float *pts;int npts,cappts;t3_vec3 *verts;int nverts,capverts;} gc;
static float ground_level(float x,float z) {
  float t=land_height(x,z);return G.map_style==0||G.map_style==7?fmaxf(t,GROUND_SEA_TROUGH):t;
}
static bool ground_is_leaf(const t3_geometry *g) {
  for(int lod=0;lod<2;lod++)for(int m=0;m<8;m++)if(forest_geometry[lod][m][1]==g)return true;return false;
}
static void ground_vertex(t3_vec3 v) {
  if(gc.nverts==gc.capverts){gc.capverts=gc.capverts?gc.capverts*2:4096;gc.verts=realloc(gc.verts,gc.capverts*sizeof *gc.verts);}
  gc.verts[gc.nverts++]=v;
}
static void ground_point(float x,float z) {
  if(gc.npts+2>gc.cappts){gc.cappts=gc.cappts?gc.cappts*2:8192;gc.pts=realloc(gc.pts,gc.cappts*sizeof(float));}
  gc.pts[gc.npts++]=x;gc.pts[gc.npts++]=z;
}
static void ground_geometry(const t3_geometry *g,const t3_mat4 *m) {
  t3_attribute *p=g->attributes[T3_ATTR_POSITION];if(!p)return;const float *a=p->array;
  for(int i=0;i<p->count;i++)ground_vertex(t3_vec3_apply_mat4((t3_vec3){a[i*3],a[i*3+1],a[i*3+2]},m));
}
static int ground_cmp_angle(const void *a,const void *b){float x=((const float *)a)[2],y=((const float *)b)[2];return x<y?-1:x>y;}
/* Close the vertices collected so far into one unit. */
static void ground_close(int kind) {
  if(!gc.nverts)return;
  t3_box3 box={{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};
  for(int i=0;i<gc.nverts;i++){t3_vec3 v=gc.verts[i];box.min.x=fminf(box.min.x,v.x);box.min.y=fminf(box.min.y,v.y);box.min.z=fminf(box.min.z,v.z);box.max.x=fmaxf(box.max.x,v.x);box.max.y=fmaxf(box.max.y,v.y);box.max.z=fmaxf(box.max.z,v.z);}
  if(box.min.y<-1000||box.max.x-box.min.x<1e-3f){gc.nverts=0;return;} /* parked out of sight, or scaled to nothing */
  if(gc.n==gc.cap){gc.cap=gc.cap?gc.cap*2:2048;gc.units=realloc(gc.units,gc.cap*sizeof *gc.units);}
  ground_unit *u=&gc.units[gc.n++];u->box=box;u->base=box.min.y;u->first=gc.npts/2;u->kind=kind;
  /* the base: unique positions in the bottom 10 cm */
  float (*base)[3]=malloc(gc.nverts*sizeof *base);int nb=0;
  for(int i=0;i<gc.nverts;i++){t3_vec3 v=gc.verts[i];if(v.y>box.min.y+.1f)continue;bool dup=false;for(int k=0;k<nb&&!dup;k++)dup=fabsf(base[k][0]-v.x)<.01f&&fabsf(base[k][1]-v.z)<.01f;if(!dup&&nb<4096){base[nb][0]=v.x;base[nb][1]=v.z;nb++;}}
  float cx=0,cz=0;for(int k=0;k<nb;k++){cx+=base[k][0];cz+=base[k][1];}cx/=nb;cz/=nb;
  if(nb==4){ /* a box-like base: its corners in order round the centre, and a bilinear grid between them */
    for(int k=0;k<4;k++)base[k][2]=atan2f(base[k][1]-cz,base[k][0]-cx);qsort(base,4,sizeof *base,ground_cmp_angle);
    float span=fmaxf(hypotf(base[1][0]-base[0][0],base[1][1]-base[0][1]),hypotf(base[3][0]-base[0][0],base[3][1]-base[0][1]));int n=(int)ceilf(span/4);if(n<1)n=1;if(n>40)n=40;
    for(int j=0;j<=n;j++)for(int i=0;i<=n;i++){float s=i/(float)n,t=j/(float)n;
      ground_point((1-s)*(1-t)*base[0][0]+s*(1-t)*base[1][0]+s*t*base[2][0]+(1-s)*t*base[3][0],(1-s)*(1-t)*base[0][1]+s*(1-t)*base[1][1]+s*t*base[2][1]+(1-s)*t*base[3][1]);}
  } else {
    int stride=nb>256?nb/256:1;for(int k=0;k<nb;k+=stride)ground_point(base[k][0],base[k][1]);ground_point(cx,cz);
  }
  free(base);u->count=gc.npts/2-u->first;gc.nverts=0;
  if(kind==3){ /* a span rests on its ends: keep the base points within 5 m of either end of its long axis */
    bool along_x=box.max.x-box.min.x>=box.max.z-box.min.z;float lo=along_x?box.min.x:box.min.z,hi=along_x?box.max.x:box.max.z;int kept=0;
    for(int k=0;k<u->count;k++){float *pt=&gc.pts[(u->first+k)*2],c=along_x?pt[0]:pt[1];if(c<lo+5||c>hi-5){gc.pts[(u->first+kept)*2]=pt[0];gc.pts[(u->first+kept)*2+1]=pt[1];kept++;}}
    u->count=kept;gc.npts=(u->first+kept)*2;
  }
}
/* One geometry as a prop of its own, apart from the prop being gathered. */
static void ground_apart(const t3_geometry *g,const t3_mat4 *m,int kind) {
  int keep=gc.nverts;t3_vec3 *saved=NULL;if(keep){saved=malloc(keep*sizeof *saved);memcpy(saved,gc.verts,keep*sizeof *saved);gc.nverts=0;}
  ground_geometry(g,m);ground_close(kind);
  if(keep){memcpy(gc.verts,saved,keep*sizeof *saved);gc.nverts=keep;free(saved);}
}
/* Gather the meshes of one prop. An instanced mesh becomes one prop per instance; a tree canopy is a prop of its own
 * that only carries (an ornament hangs in it). */
static void ground_gather(t3_object *o,int kind) {
  if(!o->visible)return;
  if(o->name&&!strcmp(o->name,"surface"))return; /* draped: checked apart */
  if(o->name&&!strcmp(o->name,"water"))kind=1;
  if(o->type==T3_INSTANCED_MESH){t3_instanced_mesh *im=(t3_instanced_mesh *)o;int k=ground_is_leaf(im->mesh.geometry)?1:kind;
    for(int i=0;i<im->count;i++){t3_mat4 local,world;t3_instanced_mesh_get_matrix_at(im,i,&local);t3_mat4_multiply(&world,&o->matrix_world,&local);ground_apart(im->mesh.geometry,&world,k);}
    return;}
  if(o->type==T3_MESH){const t3_geometry *g=((t3_mesh *)o)->geometry;if(ground_is_leaf(g))ground_apart(g,&o->matrix_world,1);else ground_geometry(g,&o->matrix_world);}
  for(int i=0;i<o->child_count;i++)ground_gather(o->children[i],kind);
}
static void ground_collect(t3_object *o,int kind) {
  if(o->name&&!strcmp(o->name,"water"))kind=1;
  if(o->name&&!strcmp(o->name,"span"))kind=3;
  ground_gather(o,kind);ground_close(kind);
}
static void ground_collect_children(t3_object *o) {for(int i=0;i<o->child_count;i++)ground_collect(o->children[i],0);}
static void ground_surface_walk(t3_object *o) {
  if(!o->visible)return;
  if(o->type==T3_MESH&&o->name&&!strcmp(o->name,"surface")){
    t3_attribute *p=((t3_mesh *)o)->geometry->attributes[T3_ATTR_POSITION];const float *a=p->array;float worst=-1e9f;t3_vec3 at={0};
    for(int i=0;i<p->count;i++){t3_vec3 v=t3_vec3_apply_mat4((t3_vec3){a[i*3],a[i*3+1],a[i*3+2]},&o->matrix_world);float gap=v.y-ground_level(v.x,v.z);if(gap>worst){worst=gap;at=v;}}
    ground_surfaces++;if(worst>GROUND_SURFACE_LIFT){ground_surface_floating++;fprintf(stderr,"ground: surface %.2f m above the ground at %.0f %.1f %.0f\n",worst,at.x,at.y,at.z);}
    return;
  }
  for(int i=0;i<o->child_count;i++)ground_surface_walk(o->children[i]);
}
static void ground_check(void) {
  static bool registered;if(!registered){registered=true;cw_debug_i32("ground.units",&ground_units);cw_debug_i32("ground.floating",&ground_floating);cw_debug_i32("ground.worst_mm",&ground_worst_mm);cw_debug_f32_array("ground.worst",ground_worst,3);cw_debug_i32("ground.surfaces",&ground_surfaces);cw_debug_i32("ground.surface_floating",&ground_surface_floating);}
  int style=G.map_style;gc.n=gc.npts=gc.nverts=0;ground_floating=ground_worst_mm=ground_surfaces=ground_surface_floating=0;
  t3_object *roots[3]={G.decor[style],style==0?G.airfield:NULL,NULL};
  for(int r=0;r<3;r++)if(roots[r]){t3_object_update_world_matrix(roots[r],true,true);ground_collect_children(roots[r]);ground_surface_walk(roots[r]);}
  if(style==6){air_target *a=&G.air_targets[SANTA];t3_object_update_world_matrix(a->root,true,true);ground_collect(a->root,0);}
  for(int i=0;i<BUILDINGS;i++)if(G.buildings[i].node&&G.buildings[i].hp>0&&G.buildings[i].ruin){ /* each ruin as it lies once its building falls */
    t3_object *ruin=G.buildings[i].ruin;bool shown=ruin->visible;ruin->visible=true;t3_object_update_world_matrix(ruin,true,true);ground_gather(ruin,2);ground_close(2);ruin->visible=shown;}
  ground_units=gc.n;int printed=0;
  /* A base point off the ground is carried when it lies on or in another prop, or overhangs one by at most 3 m (a roof's
   * eaves past its walls); a ruin carries nothing and is carried by nothing (its building is gone). Every point must be
   * on the ground or carried: a bridge resting on its piers still floats where its ends hang over the bank. */
  for(int u=0;u<gc.n;u++){ground_unit *a=&gc.units[u];if(a->kind==1)continue;float worst=0;float wx=0,wz=0;
    for(int k=0;k<a->count;k++){float x=gc.pts[(a->first+k)*2],z=gc.pts[(a->first+k)*2+1],gap=a->base-ground_level(x,z);if(gap<=GROUND_TOLERANCE)continue;
      bool held=false;if(a->kind!=2)for(int s=0;s<gc.n&&!held;s++){if(s==u||gc.units[s].kind==2)continue;t3_box3 b=gc.units[s].box;held=x>=b.min.x-3&&x<=b.max.x+3&&z>=b.min.z-3&&z<=b.max.z+3&&a->base>=b.min.y-GROUND_TOLERANCE&&a->base<=b.max.y+GROUND_TOLERANCE;}
      if(!held&&gap>worst){worst=gap;wx=x;wz=z;}}
    if(worst>0){ground_floating++;int mm=(int)(worst*1000);if(mm>ground_worst_mm){ground_worst_mm=mm;ground_worst[0]=wx;ground_worst[1]=a->base;ground_worst[2]=wz;}
      if(printed++<60)fprintf(stderr,"ground: %s %.2f m above the ground at %.0f %.1f %.0f (prop %.0f x %.0f x %.0f at %.0f %.0f)\n",a->kind==2?"ruin":"prop",worst,wx,a->base,wz,a->box.max.x-a->box.min.x,a->box.max.y-a->box.min.y,a->box.max.z-a->box.min.z,(a->box.min.x+a->box.max.x)*.5f,(a->box.min.z+a->box.max.z)*.5f);}
  }
  fprintf(stderr,"ground: mission %d style %d: %d props, %d floating (worst %d mm), %d surfaces, %d floating\n",G.level,style,ground_units,ground_floating,ground_worst_mm,ground_surfaces,ground_surface_floating);
}
