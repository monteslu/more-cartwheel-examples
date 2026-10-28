/* The capital ship has its own effects budget and rigid-body hull breakup. */
static struct {
  cw_node fx[14];t3_object *chunks[8],*rings[2];t3_material *ring_mat;
  cw_body3d bodies[8];float age;int32_t fragments,active;int secondary;
} capital;
static t3_geometry *capital_hull_sector(void) {
  float p[13*4*3],uv[13*4*2];uint32_t ix[12*4*6+12];int at=0;
  for(int j=0;j<13;j++)for(int k=0;k<4;k++){
    float angle=-PI/8*.92f+j*(PI/4*.92f)/12,r=k&1?535:100;int n=j*4+k;
    p[n*3]=sinf(angle)*r;p[n*3+1]=k&2?20:-20;p[n*3+2]=cosf(angle)*r-320;uv[n*2]=j/12.0f;uv[n*2+1]=k&1?1:0;
  }
  for(int j=0;j<12;j++){int a=j*4,b=a+4;int faces[4][4]={{a,a+1,b,b+1},{a+2,b+2,a+3,b+3},{a,a+2,b,b+2},{a+1,b+1,a+3,b+3}};for(int f=0;f<4;f++){int *q=faces[f];ix[at++]=q[0];ix[at++]=q[1];ix[at++]=q[2];ix[at++]=q[1];ix[at++]=q[3];ix[at++]=q[2];}}
  int ends[2][4]={{0,2,1,3},{48,49,50,51}};for(int e=0;e<2;e++){int *q=ends[e];ix[at++]=q[0];ix[at++]=q[1];ix[at++]=q[2];ix[at++]=q[1];ix[at++]=q[3];ix[at++]=q[2];}
  t3_geometry *g=t3_geometry_new();t3_attribute *pos=t3_attribute_new(T3_FLOAT32,p,52,3),*tex=t3_attribute_new(T3_FLOAT32,uv,52,2),*index=t3_attribute_new(T3_UINT32,ix,at,1);t3_geometry_set_attribute(g,T3_ATTR_POSITION,pos);t3_geometry_set_attribute(g,T3_ATTR_UV,tex);t3_geometry_set_index(g,index);t3_geometry_compute_vertex_normals(g);t3_release(pos);t3_release(tex);t3_release(index);return g;
}
static void capital_make(void) {
  static const int counts[]={200,260,180,320,110,70};static const float lives[]={2.8f,6,20,9,15,1.1f},sizes[]={105,110,130,5,12,260},lo[]={100,35,28,180,90,160},hi[]={260,150,100,440,240,380};
  for(int i=0;i<14;i++){
    int k=i<6?i:0;char name[40];snprintf(name,sizeof name,"CapitalShipExplosion%d",i);
    capital.fx[i]=burst(name,i<6?counts[k]:90,i<6?lives[k]:4,i<6?sizes[k]:85,i<6?lo[k]:50,i<6?hi[k]:150,i==0||i==3||i==5||i>=6?"ADD":"ALPHA",i!=3&&i!=4);
    cw_node n=capital.fx[i];cw_node_set(n,"visible",cw_val_bool(false));cw_node_set(n,"spread",cw_val_float(i==2?65:180));cw_node_set(n,"emission_radius",cw_val_float(i==5?20:i>=6?45:150));cw_node_set(n,"soft_distance",cw_val_float(25));cw_node_set(n,"damping",cw_val_float(i==2?.4f:.18f));
    cw_node_set(n,"gravity",i==2?cw_val_vec3(0,11,0):i==3||i==4?cw_val_vec3(0,-28,0):cw_val_vec3(0,8,0));
    cw_node_set(n,"size_curve",cw_val_string(i==2?"0 0.4, 0.2 1.4, 0.65 3, 1 4.8":i==4?"0 1, 1 1":i==3?"0 1, 0.7 0.7, 1 0":"0 0.3, 0.12 1, 0.7 2.3, 1 3.2"));
    cw_node_set(n,"color_ramp",cw_val_string(i==2?"0 0.38 0.33 0.29 0, 0.08 0.28 0.26 0.25 0.28, 0.6 0.44 0.43 0.42 0.2, 1 0.56 0.56 0.56 0":i==4?"0 0.7 0.42 0.16 1, 0.4 0.22 0.2 0.19 1, 1 0.1 0.1 0.1 0":i==3?"0 1 0.95 0.72 1, 0.4 1 0.48 0.06 1, 1 0.4 0.02 0 0":"0 1 1 0.85 0, 0.04 1 0.9 0.5 0.9, 0.25 1 0.34 0.035 0.75, 0.65 0.8 0.12 0.01 0.4, 1 0.25 0.025 0.01 0"));
    cw_node_set(n,"energy",cw_val_float(i==2||i==4?1:i==5?12:i==3?7:5));
    if(i==3){cw_node_set(n,"draw_mode",cw_val_enum("STRETCHED"));cw_node_set(n,"stretch",cw_val_float(.08f));}
    if(i==4){cw_node_set(n,"draw_mode",cw_val_enum("MESH"));cw_node_set(n,"mesh_primitive",cw_val_enum("BOX"));cw_node_set(n,"spin",cw_val_float(6));}
  }
  t3_geometry *sector=capital_hull_sector();t3_material *metal=campaign_mat(0x59636a,.75f);metal->side=T3_DOUBLE_SIDE;
  for(int i=0;i<8;i++){
    capital.chunks[i]=t3_group_new();t3_object_add((t3_object *)G.scene,capital.chunks[i]);t3_mesh *mesh=t3_mesh_new(sector,metal);mesh->base.cast_shadow=mesh->base.receive_shadow=true;t3_object_add(capital.chunks[i],(t3_object *)mesh);
    box(capital.chunks[i],0,25,80,55,15,170,metal);capital.chunks[i]->visible=false;
  }t3_release(sector);
  capital.ring_mat=t3_mesh_basic_material_new(0xffd8ad);capital.ring_mat->transparent=true;capital.ring_mat->depth_write=false;capital.ring_mat->side=T3_DOUBLE_SIDE;capital.ring_mat->blending=T3_ADDITIVE_BLENDING;
  for(int i=0;i<2;i++){capital.rings[i]=(t3_object *)t3_mesh_new(t3_ring_geometry_new(.965f,1,160,1),capital.ring_mat);t3_object_add((t3_object *)G.scene,capital.rings[i]);capital.rings[i]->visible=false;t3_object_set_rotation(capital.rings[i],i?-PI*.3f:-PI/2,0,0);}
}
static void capital_reset(void) {
  capital.active=capital.fragments=capital.secondary=0;capital.age=0;
  for(int i=0;i<8;i++){if(cw_body3d_valid(capital.bodies[i]))cw_body3d_destroy(capital.bodies[i]);capital.bodies[i]=(cw_body3d){0};capital.chunks[i]->visible=false;}
  for(int i=0;i<14;i++){cw_node_set(capital.fx[i],"emitting",cw_val_bool(false));cw_node_set(capital.fx[i],"visible",cw_val_bool(false));}
  for(int i=0;i<2;i++)capital.rings[i]->visible=false;
}
static void capital_explode(air_target *a) {
  capital.active=1;capital.age=.001f;capital.fragments=8;capital.secondary=0;G.audio_boom=1.8f;
  for(int i=0;i<6;i++){cw_node_set(capital.fx[i],"visible",cw_val_bool(true));start_burst(capital.fx[i],a->x,a->y,a->z);}
  for(int i=0;i<8;i++){
    float angle=i*PI/4;cw_body3d_def def=cw_body3d_default_def();def.position=(t3_vec3){a->x+sinf(angle)*320,a->y,a->z+cosf(angle)*320};def.rotation=t3_quat_from_axis_angle((t3_vec3){0,1,0},angle);def.linear_velocity=(t3_vec3){sinf(angle)*(85+i*8),30+(i%3)*35,cosf(angle)*(85+i*8)};def.angular_velocity=(t3_vec3){.12f*(i%2?1:-1),.13f,.08f};def.linear_damping=.08f;def.bullet=true;
    capital.bodies[i]=cw_body3d_create(G.wreck_world,&def);cw_shape3d_def shape=cw_shape3d_default_def();shape.density=.6f;cw_shape3d_box(capital.bodies[i],&shape,(t3_vec3){180,24,215},cw_xform3_identity());cw_body3d_bind_object(capital.bodies[i],capital.chunks[i]);capital.chunks[i]->visible=true;
  }
  for(int i=0;i<2;i++){capital.rings[i]->visible=true;t3_object_set_position(capital.rings[i],a->x,a->y,a->z);}
}
static void capital_update(float dt) {
  if(!capital.active)return;capital.age+=dt;air_target *a=&G.air_targets[MOTHER];
  while(capital.secondary<8&&capital.age>.3f+capital.secondary*.28f){int i=capital.secondary++;float angle=i*2.39996f;cw_node n=capital.fx[6+i];cw_node_set(n,"visible",cw_val_bool(true));start_burst(n,a->x+sinf(angle)*390,a->y+20+(i%3)*30,a->z+cosf(angle)*390);G.audio_boom=fmaxf(G.audio_boom,1.4f);}
  float radius=120+capital.age*510;capital.ring_mat->opacity=fmaxf(0,.48f-capital.age*.115f);for(int i=0;i<2;i++){t3_object_set_scale(capital.rings[i],radius,radius,radius);capital.rings[i]->visible=capital.age<4.2f;}
  if(capital.age>24){capital_reset();return;}
}
