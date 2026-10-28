/* Reuse the intact model sections as physical wreckage, preserving the pose. */
static struct {t3_object *node,*parent; t3_vec3 position,scale; t3_quat rotation;cw_body3d body;} breakup[14];
static int32_t breakup_count;
static char breakup_keys[14][48];
static float victory_age;
static t3_object *north_pole_node,*pole_sections[8];
static void breakup_reset(void) {
  for(int i=0;i<breakup_count;i++) {if(cw_body3d_valid(breakup[i].body))cw_body3d_destroy(breakup[i].body);
    t3_object *o=breakup[i].node;t3_object_add(breakup[i].parent,o);t3_object_set_position(o,breakup[i].position.x,breakup[i].position.y,breakup[i].position.z);t3_object_set_quaternion(o,breakup[i].rotation);t3_object_set_scale(o,breakup[i].scale.x,breakup[i].scale.y,breakup[i].scale.z);o->visible=true;}
  breakup_count=0;victory_age=0;cw_debug_i32("destruction.pieces",&breakup_count);cw_debug_f32("victory.age",&victory_age);
}
static void breakup_launch(t3_object *o,t3_vec3 half,t3_vec3 velocity,int spin) {
  if(breakup_count>=14)return;int i=breakup_count++;
  breakup[i].node=o;breakup[i].parent=o->parent;breakup[i].position=o->position;breakup[i].scale=o->scale;breakup[i].rotation=o->quaternion;
  t3_object_update_world_matrix(o,true,false);t3_vec3 p,s;t3_quat q;t3_mat4_decompose(&o->matrix_world,&p,&q,&s);
  t3_object_add((t3_object *)G.scene,o);t3_object_set_scale(o,s.x,s.y,s.z);
  cw_body3d_def b=cw_body3d_default_def();b.position=p;b.rotation=q;b.linear_velocity=velocity;b.angular_velocity=(t3_vec3){.45f+spin*.13f,.3f,.6f*(spin%2?1:-1)};b.linear_damping=.08f;b.bullet=true;
  snprintf(breakup_keys[i],sizeof breakup_keys[i],"destruction.%d.position",i);cw_debug_f32_array(breakup_keys[i],&o->position.x,3);
  breakup[i].body=cw_body3d_create(G.wreck_world,&b);cw_shape3d_def shape=cw_shape3d_default_def();shape.density=2;shape.material.restitution=.22f;cw_shape3d_box(breakup[i].body,&shape,half,cw_xform3_identity());cw_body3d_bind_object(breakup[i].body,o);o->visible=true;cw_world3d_sync(G.wreck_world,1);
}
static void north_pole_break(t3_object *node) {
  if(node!=north_pole_node)return;
  for(int i=0;i<8;i++)breakup_launch(pole_sections[i],(t3_vec3){i==7?22:10,i==7?27:20,i==7?22:10},(t3_vec3){18+i*3,8+i*2,(i%2?1:-1)*(12+i*2)},i);
}
