/* Hero architecture uses facade textures, separate roofs and recognisable
 * landmarks. It is a fictional combat layout, not a surveyed city replica. */
static void landmark_begin(int index,t3_object *node,float x,float y,float z,float radius) {
  register_building(index,node,x,y,z,radius);structure *b=&G.buildings[index];b->collider_count=0;b->bounds=(t3_box3){{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};
}
static void landmark_solid(int index,float x,float y,float z,float w,float h,float depth) {
  structure *b=&G.buildings[index];if(b->collider_count>=16)CW_FATAL("Too many landmark collision parts");t3_box3 q={{x-w/2,y-h/2,z-depth/2},{x+w/2,y+h/2,z+depth/2}};b->colliders[b->collider_count++]=q;
  b->bounds.min.x=fminf(b->bounds.min.x,q.min.x);b->bounds.min.y=fminf(b->bounds.min.y,q.min.y);b->bounds.min.z=fminf(b->bounds.min.z,q.min.z);b->bounds.max.x=fmaxf(b->bounds.max.x,q.max.x);b->bounds.max.y=fmaxf(b->bounds.max.y,q.max.y);b->bounds.max.z=fmaxf(b->bounds.max.z,q.max.z);
}
static void onion_dome(t3_object *root,float x,float y,float z,float radius,float height,uint32_t color,bool striped) {
  t3_object *dome=t3_group_new();t3_object_set_position(dome,x,y,z);t3_object_add(root,dome);
  t3_material *a=campaign_mat(color,.45f),*b=campaign_mat(color==0x328b65?0xe6d390:0xe9c45e,.45f);
  for(int panel=0;panel<(striped?12:1);panel++){
    int cols=striped?4:48;float pos[49*49*3],normal[49*49*3];uint32_t idx[48*48*6];int at=0;
    for(int j=0;j<=48;j++)for(int i=0;i<=cols;i++){
      float t=j/48.0f,r=radius*(.45f*(1-t)+1.15f*powf(fmaxf(sinf(PI*t),0),.85f))*(1-.2f*t),angle=2*PI*(panel+i/(float)cols)/(striped?12:1)+(striped?t*.65f:0);
      float dt=.0002f,lo=fmaxf(0,t-dt),hi=fminf(1,t+dt),rlo=radius*(.45f*(1-lo)+1.15f*powf(fmaxf(sinf(PI*lo),0),.85f))*(1-.2f*lo),rhi=radius*(.45f*(1-hi)+1.15f*powf(fmaxf(sinf(PI*hi),0),.85f))*(1-.2f*hi);
      int k=(j*(cols+1)+i)*3;pos[k]=r*cosf(angle);pos[k+1]=t*height;pos[k+2]=r*sinf(angle);t3_vec3 n=t3_vec3_normalize((t3_vec3){cosf(angle),-(rhi-rlo)/fmaxf(.0001f,(hi-lo)*height),sinf(angle)});normal[k]=n.x;normal[k+1]=n.y;normal[k+2]=n.z;
    }
    for(int j=0;j<48;j++)for(int i=0;i<cols;i++){int q=j*(cols+1)+i;idx[at++]=q;idx[at++]=q+cols+1;idx[at++]=q+1;idx[at++]=q+1;idx[at++]=q+cols+1;idx[at++]=q+cols+2;}
    t3_geometry *g=t3_geometry_new();t3_attribute *p=t3_attribute_new(T3_FLOAT32,pos,49*(cols+1),3),*n=t3_attribute_new(T3_FLOAT32,normal,49*(cols+1),3),*ix=t3_attribute_new(T3_UINT32,idx,at,1);t3_geometry_set_attribute(g,T3_ATTR_POSITION,p);t3_geometry_set_attribute(g,T3_ATTR_NORMAL,n);t3_geometry_set_index(g,ix);t3_release(p);t3_release(n);t3_release(ix);t3_mesh *mesh=t3_mesh_new(g,panel%2?b:a);mesh->base.cast_shadow=mesh->base.receive_shadow=true;t3_object_add(dome,(t3_object *)mesh);t3_release(g);
  }t3_release(a);t3_release(b);
}
static void ruby_star(t3_object *root,float x,float y,float z,float r,t3_material *mat) {
  float vertices[11*3]={0};uint32_t index[30];for(int i=0;i<10;i++){float angle=PI/2+i*PI/5,rad=i%2?r*.42f:r;vertices[(i+1)*3]=cosf(angle)*rad;vertices[(i+1)*3+1]=sinf(angle)*rad;index[i*3]=0;index[i*3+1]=1+i;index[i*3+2]=1+(i+1)%10;}
  t3_geometry *g=t3_geometry_new();t3_attribute *p=t3_attribute_new(T3_FLOAT32,vertices,11,3),*ix=t3_attribute_new(T3_UINT32,index,30,1);t3_geometry_set_attribute(g,T3_ATTR_POSITION,p);t3_geometry_set_index(g,ix);t3_mesh *m=t3_mesh_new(g,mat);t3_object_set_position(m,x,y,z);t3_object_add(root,(t3_object *)m);t3_release(p);t3_release(ix);t3_release(g);
}
static void make_palace(t3_object *b) {
  t3_material *white=campaign_mat(0xe7e0cc,0),*green=campaign_mat(0x537c68,.35f),*gold=campaign_mat(0xe2bc62,.7f);
  facade_box(b,0,33,0,330,66,140,architecture[1],5,1);
  box(b,0,69,0,342,8,152,white);box(b,0,77,0,334,9,144,green);
  for(int side=-1;side<=1;side+=2){facade_box(b,side*156,39,0,45,78,154,architecture[1],1,1.2f);box(b,side*156,83,0,51,10,162,green);}
  facade_box(b,0,84,-10,88,36,100,architecture[1],1.4f,.6f);box(b,0,105,-10,102,7,112,green);
  onion_dome(b,0,108,-10,24,40,0xc6aa59,false);box(b,0,154,-10,1,16,1,gold);
  for(int j=0;j<25;j++){float x=-156+j*13;box(b,x,32,71.5f,1.5f,59,1.5f,white);}
  for(int floor=0;floor<3;floor++)box(b,0,8+floor*22,71.8f,334,1.4f,1.2f,gold);
  for(int side=-1;side<=1;side+=2){box(b,side*31,9,75,5,18,8,white);box(b,side*31,20,75,8,4,10,white);}box(b,0,22,76,75,4,13,white);
  for(int step=0;step<5;step++)box(b,0,step*.7f,92-step*3,86-step*4,1.4f,6,white);
}
static void make_clock_tower(t3_object *b) {
  t3_material *white=campaign_mat(0xe5dfd3,0),*green=campaign_mat(0x325d48,.3f),*gold=campaign_mat(0xe5b953,.7f),*black=t3_mesh_basic_material_new(0x20262b);
  facade_box(b,0,32,0,36,64,34,architecture[0],2,4);box(b,0,65,0,43,5,41,white);facade_box(b,0,83,0,29,32,29,architecture[0],1.5f,2);
  box(b,0,102,0,33,6,33,white);t3_object *roof=cone(b,0,125,0,24,43,green);t3_object_set_rotation(roof,0,PI/4,0);box(b,0,151,0,2,17,2,gold);
  for(int s=-1;s<=1;s+=2){box(b,s*16,85,16,2,34,2,white);cone(b,s*17,108,17,3,13,green);box(b,s*16,85,-16,2,34,2,white);}
  t3_mesh *clock=t3_mesh_new(t3_cylinder_geometry_new(10,10,.8f,48,1,false),black);t3_object_set_position(clock,0,83,15);t3_object_set_rotation(clock,PI/2,0,0);t3_object_add(b,(t3_object *)clock);
  t3_mesh *ring=t3_mesh_new(t3_torus_geometry_new(10,.5f,8,48,2*PI),gold);t3_object_set_position(ring,0,83,15.6f);t3_object_add(b,(t3_object *)ring);
  for(int i=0;i<12;i++){float angle=i*PI/6;t3_object *tick=box(b,sinf(angle)*8.5f,83+cosf(angle)*8.5f,15.8f,.5f,1.5f,.4f,gold);t3_object_set_rotation(tick,0,0,-angle);}
  t3_object *hand=box(b,-2,85,16.1f,.6f,7,.5f,gold);t3_object_set_rotation(hand,0,0,.85f);hand=box(b,2.5f,85,16.3f,.5f,9,.5f,gold);t3_object_set_rotation(hand,0,0,-.9f);
  t3_material *star=t3_mesh_basic_material_new(0xf24e45);star->side=T3_DOUBLE_SIDE;ruby_star(b,0,163,0,7,star);
}
static void make_cathedral(t3_object *b) {
  t3_material *white=campaign_mat(0xe8d6ba,0),*red=architecture[0],*green=campaign_mat(0x39765c,.2f);box(b,0,9,0,170,18,150,white);
  static const uint32_t colors[]={0x328b65,0xd74c3b,0x358ab5,0xd7ae45,0x328b65,0xc8b17f,0x518b75,0xbd4b42};
  for(int i=0;i<9;i++){float angle=i*2*PI/8,x=i==8?0:sinf(angle)*58,z=i==8?0:cosf(angle)*52,h=i==8?100:45+(i%2)*14,r=i==8?21:14;
    facade_box(b,x,18+h*.5f,z,r*2,h,r*2,red,1,3);box(b,x,18+h,z,r*2.3f,4,r*2.3f,white);onion_dome(b,x,21+h,z,r,r*2.5f,i==8?0xcda851:colors[i],i!=8);box(b,x,27+h+r*2.5f,z,.7f,12,.7f,green);box(b,x,29+h+r*2.5f,z,7,.7f,.7f,green);
  }
}
static void make_kremlin_landmark(int i,t3_object *b,float x,float y,float z) {
  if(i==3){make_palace(b);landmark_begin(71,b,x,y+40,z,180);landmark_solid(71,x,y+40,z,340,80,150);landmark_solid(71,x,y+105,z-10,100,70,110);}
  else if(i==4){make_clock_tower(b);landmark_begin(72,b,x,y+80,z,35);landmark_solid(72,x,y+34,z,38,68,36);landmark_solid(72,x,y+105,z,32,82,32);}
  else{make_cathedral(b);landmark_begin(73,b,x,y+50,z,100);landmark_solid(73,x,y+9,z,170,18,150);for(int j=0;j<9;j++){float angle=j*2*PI/8,dx=j==8?0:sinf(angle)*58,dz=j==8?0:cosf(angle)*52,h=j==8?165:103+(j%2)*14;landmark_solid(73,x+dx,y+18+h/2,z+dz,36,h,36);}}
}
static void make_kremlin_walls(t3_object *root) {
  static const float corners[3][2]={{-1550,-2350},{600,-1700},{-500,200}};
  t3_material *cap=campaign_mat(0xae624a,0);
  for(int edge=0;edge<3;edge++){
    float ax=corners[edge][0],az=corners[edge][1],bx=corners[(edge+1)%3][0],bz=corners[(edge+1)%3][1],length=hypotf(bx-ax,bz-az),angle=atan2f(bx-ax,bz-az);int count=(int)(length/55)+1;
    for(int j=0;j<count;j++){
      float x=ax+(bx-ax)*(j+.5f)/count,z=az+(bz-az)*(j+.5f)/count,y=terrain_height(x,z);t3_object *segment=t3_group_new();t3_object_add(root,segment);t3_object_set_position(segment,x,y,z);t3_object_set_rotation(segment,0,angle,0);
      facade_box(segment,0,13,0,10,26,length/count+1,architecture[0],1,2);for(int k=0;k<4;k++)box(segment,0,28,-length/count*.5f+(k+.5f)*length/count/4,11,5,6,cap);
      struct district *d=&districts[5];if(d->count<DISTRICT_SOLIDS){int n=d->count++;float dx=fabsf(sinf(angle))*length/count*.5f+fabsf(cosf(angle))*6,dz=fabsf(cosf(angle))*length/count*.5f+fabsf(sinf(angle))*6;d->solid[n].custom=segment;d->solid[n].alive=1;d->solid[n].box=(t3_box3){{x-dx,y,z-dz},{x+dx,y+31,z+dz}};}
    }
  }
}
static void courtyard_solid(t3_object *node,float x,float y,float z,float w,float h,float depth) {
  struct district *d=&districts[5];if(d->count>=DISTRICT_SOLIDS)return;int i=d->count++;d->solid[i].alive=1;d->solid[i].custom=node;d->solid[i].box=(t3_box3){{x-w/2,y,z-depth/2},{x+w/2,y+h,z+depth/2}};
}
static void make_kremlin_courtyard(t3_object *root) {
  t3_material *roof=campaign_mat(0x4b7662,.25f),*white=campaign_mat(0xe8dfca,0),*stone=campaign_mat(0xc5bda8,0),*paving=campaign_mat(0x9a9a94,0);
  /* Palace avenues, ministry blocks and cathedral courtyards replace the
   * empty snow field inside the walls. The main attack axis stays open. */
  paved_strip(root,-710,-590,-2180,140,paving);paved_strip(root,-1320,350,-1700,-1600,paving);paved_strip(root,-1280,-100,-1040,-930,paving);
  static const float ministries[][4]={{-1080,-1820,180,105},{-920,-2030,180,100},{-380,-2010,220,110},{-1130,-1450,170,100},{-300,-1490,190,110},{-910,-740,130,85},{-550,-620,140,90},{-450,-1060,170,95},{-155,-1230,160,100},{-1090,-1660,120,65}};
  for(int i=0;i<10;i++){
    float x=ministries[i][0],z=ministries[i][1],w=ministries[i][2],depth=ministries[i][3],y=terrain_height(x,z),h=29+(i%3)*8;
    t3_object *b=t3_group_new();t3_object_add(root,b);t3_object_set_position(b,x,y,z);facade_box(b,0,h/2,0,w,h,depth,architecture[1],w/60,h/66);box(b,0,h+3,0,w+4,6,depth+4,roof);box(b,0,2,0,w+7,4,depth+7,stone);
    if(i%3==0){facade_box(b,0,h+12,0,36,22,40,architecture[1],1,.5f);box(b,0,h+25,0,42,5,46,roof);}
    courtyard_solid(b,x,y,z,w+8,h+28,depth+8);
  }
  /* A cluster of white, gold-domed churches and a tall bell tower. */
  for(int i=0;i<3;i++){
    float x=-1020+i*260,z=-1130-(i%2)*350,y=terrain_height(x,z);t3_object *church=t3_group_new();t3_object_add(root,church);t3_object_set_position(church,x,y,z);
    facade_box(church,0,28,0,95,56,80,architecture[1],1.4f,.9f);box(church,0,58,0,100,5,85,white);
    for(int dome=0;dome<5;dome++){float dx=dome==4?0:(dome%2?28:-28),dz=dome==4?0:(dome/2?24:-24),h=dome==4?78:65;
      t3_mesh *drum=t3_mesh_new(t3_cylinder_geometry_new(11,11,h-58,24,1,false),white);t3_object_set_position(drum,dx,58+(h-58)*.5f,dz);t3_object_add(church,(t3_object *)drum);onion_dome(church,dx,h,dz,12,27,0xe2bb51,false);box(church,dx,h+31,dz,.7f,8,.7f,roof);
    }
    courtyard_solid(church,x,y,z,102,113,88);
  }
  float x=-400,z=-1770,y=terrain_height(x,z);t3_object *bell=t3_group_new();t3_object_add(root,bell);t3_object_set_position(bell,x,y,z);
  for(int i=0;i<3;i++){float r=20-i*4,h=45-i*5;t3_mesh *drum=t3_mesh_new(t3_cylinder_geometry_new(r,r,h,32,1,false),white);t3_object_set_position(drum,0,i*40+h/2,0);t3_object_add(bell,(t3_object *)drum);box(bell,0,i*40+h,0,r*2+5,3,r*2+5,stone);}onion_dome(bell,0,120,0,16,38,0xe2bb51,false);courtyard_solid(bell,x,y,z,48,165,48);
  uint32_t seed=0xb74c012u;
  for(int i=0;i<120;i++){
    float tx=-1350+district_rand(&seed)*1300,tz=-2200+district_rand(&seed)*1800;
    if(tx> -760&&tx< -540)continue;bool clear=true;
    struct district *d=&districts[5];for(int k=0;k<d->count;k++){t3_box3 q=d->solid[k].box;if(tx>q.min.x-25&&tx<q.max.x+25&&tz>q.min.z-25&&tz<q.max.z+25){clear=false;break;}}
    if(clear)fir_tree(root,tx,tz,16+district_rand(&seed)*12,false);
  }
}
