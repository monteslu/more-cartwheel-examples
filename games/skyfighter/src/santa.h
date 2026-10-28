/* A giant, shielded Santa is Snowglobe's final target. Power comes from the
 * three workshop cores; cannon, missiles and collisions share that gate. */
static int32_t santa_shielded=1;
static struct {t3_object *arm[2],*elbow[2];float pose[3];} santa_rig;
static t3_object *santa_parts[6];
static bool santa_protected(void){return mission_ground_count(84,86)<3;}
static void santa_make(air_target *a) {
  t3_material *coat=campaign_mat(0xb32930,0),*fur=campaign_mat(0xeee6d6,0),*skin=campaign_mat(0xd7a18c,0),*boots=campaign_mat(0x232224,.2f),*gold=campaign_mat(0xd8ad43,.7f),*pink=campaign_mat(0xc67269,0);
  coat->roughness=.92f;fur->roughness=1;skin->roughness=.72f;
  a->envelope=t3_group_new();t3_object_add(a->root,a->envelope);t3_object *b=santa_parts[0]=t3_group_new();t3_object_add(a->envelope,b);
  /* His belly extends over his belt, with a separate coat, collar and legs. */
  oval(b,0,-7,0,18,22,15,coat);oval(b,0,12,0,14,12,11,coat);
  for(int side=-1;side<=1;side+=2){t3_object *leg=santa_parts[side>0?5:4]=t3_group_new();t3_object_add(a->envelope,leg);t3_object_set_position(leg,side*7,-28,0);oval(leg,0,0,0,5,11,6,coat);oval(leg,0,-8,4,5.5f,5,9,boots);
    int hand=side>0; t3_object *arm=santa_rig.arm[hand]=t3_group_new();santa_parts[2+hand]=arm;t3_object_add(a->envelope,arm);t3_object_set_position(arm,side*14,15,0);
    oval(arm,side*3,-6,0,5,10,5,coat);
    t3_object *elbow=santa_rig.elbow[hand]=t3_group_new();t3_object_add(arm,elbow);t3_object_set_position(elbow,side*6,-13,0);
    oval(elbow,0,-5,0,4.5f,8,4.5f,coat);oval(elbow,0,-10,0,5,2.7f,5,fur);oval(elbow,0,-14,0,4.5f,5,4.5f,fur);
    for(int knuckle=0;knuckle<4;knuckle++)oval(elbow,-2.8f+knuckle*1.8f,-13.5f,3.3f,1.3f,2.1f,1.6f,fur);
  }
  b=santa_parts[1]=t3_group_new();t3_object_add(a->envelope,b);t3_object_set_position(b,0,27,0);
  oval(b,0,0,0,9.5f,10,9,skin);oval(b,0,23,-3,10,8,8,fur);
  for(int side=-1;side<=1;side+=2){oval(b,side*9,28,0,2,3,2,skin);oval(b,side*5,26,7,3,2.5f,1.7f,pink);
    t3_object *brow=oval(b,side*3.8f,31,8.4f,2.5f,1.1f,1.2f,fur);t3_object_set_rotation(brow,0,0,side*.35f);oval(b,side*3.8f,30.4f,9.2f,.75f,.85f,.35f,boots);}
  oval(b,0,27,9.6f,2.7f,2.4f,2.5f,skin);
  /* Layered moustache and irregular beard curls give the face actual depth. */
  for(int side=-1;side<=1;side+=2){t3_object *m=oval(b,side*3,24.8f,9.2f,4,1.7f,2.1f,fur);t3_object_set_rotation(m,0,0,side*.12f);}
  oval(b,0,20.8f,9.5f,2.5f,1.8f,1,boots);
  for(int i=0;i<21;i++){float angle=i*2.39996,r=2+sqrtf(i/21.0f)*7;oval(b,sinf(angle)*r,19+cosf(angle)*r*.7f,7.7f+(i%3)*.5f,2.6f,3.8f,2.4f,fur);}
  for(int i=1;i<b->child_count;i++)b->children[i]->position.y-=27;
  b=santa_parts[0];
  for(int i=0;i<13;i++){float angle=i*2*PI/13;oval(b,sinf(angle)*11,16,cosf(angle)*9,2.3f,2.2f,2.3f,fur);}
  for(int i=0;i<14;i++){float angle=i*2*PI/14;oval(b,sinf(angle)*14,-23,cosf(angle)*11.5f,2.3f,2.4f,2.3f,fur);}
  for(int i=0;i<4;i++)oval(b,0,5-i*6,14.5f,1.1f,1.1f,.6f,gold);
  /* The broad leather belt wraps the belly rather than cutting through it. */
  for(int i=0;i<24;i++){float angle=i*2*PI/24;t3_object *segment=box(b,sinf(angle)*17.7f,-13,cosf(angle)*14.5f,4.8f,3.8f,1.2f,boots);t3_object_set_rotation(segment,0,angle,0);}box(b,0,-13,15.3f,7,5,1.5f,gold);box(b,0,-13,16.2f,4.5f,2.8f,.6f,boots);
  b=santa_parts[1];
  oval(b,0,9.5f,0,10.5f,2.8f,9.5f,fur);t3_object *hat=cone(b,0,17,0,10,17,coat);t3_object_set_rotation(hat,0,0,-.2f);oval(b,5,23,0,5,5,5,coat);oval(b,9,20,0,3.2f,3.2f,3.2f,fur);
  t3_material *shield=t3_mesh_basic_material_new(0x71d5dc);shield->transparent=true;shield->opacity=.10f;shield->depth_write=false;
  a->shield=oval(a->root,0,4,0,34,56,32,shield);a->shield->cast_shadow=false;a->shield->receive_shadow=false;
  cw_debug_i32("santa.shielded",&santa_shielded);cw_debug_f32_array("santa.pose",santa_rig.pose,3);
}
static void santa_update(air_target *a,float dt) {
  if(G.level!=6||a->state)return;
  bool protected=santa_protected();
  if(santa_shielded&&!protected){notify("POWER DOWN / SANTA IS EXPOSED",5);G.audio_alert=.18f;G.audio_tone_hz=650;}
  santa_shielded=protected;
  float desired=atan2f(G.px-a->x,G.pz-a->z),yaw=a->envelope->rotation.y;
  yaw+=clampf(cw_wrap_angle(desired-yaw),-dt*.6f,dt*.6f);
  float t=G.mission_time,shake=sinf(t*4.2f);santa_rig.pose[0]=-1.45f+shake*.18f;santa_rig.pose[1]=-1.5f+sinf(t*4.2f+.8f)*.32f;santa_rig.pose[2]=.025f+sinf(t*1.7f)*.018f;
  t3_object_set_rotation(santa_rig.arm[1],santa_rig.pose[0],.08f,-.22f);t3_object_set_rotation(santa_rig.elbow[1],santa_rig.pose[1],0,.1f);
  t3_object_set_rotation(santa_rig.arm[0],-.10f+sinf(t*1.7f)*.06f,0,-.16f);t3_object_set_rotation(santa_rig.elbow[0],-.12f,0,0);
  t3_object_set_rotation(a->envelope,santa_rig.pose[2],yaw,sinf(t*1.7f)*.012f);
  t3_object_set_scale(a->envelope,1,1+sinf(G.mission_time*2)*.004f,1);
}

static void santa_explode(air_target *a) {
  static const t3_vec3 half[]={{18,23,15},{11,22,11},{8,18,7},{8,18,7},{6,13,9},{6,13,9}};
  for(int i=0;i<6;i++){float angle=i*2.39996f;breakup_launch(santa_parts[i],half[i],(t3_vec3){sinf(angle)*(35+i*6),i==1?95:45+i*8,cosf(angle)*(35+i*6)},i);}
  cw_debug_i32("destruction.pieces",&breakup_count);cw_debug_f32("victory.age",&victory_age);
}
