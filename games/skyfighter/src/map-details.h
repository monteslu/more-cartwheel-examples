/* Landmarks support navigation and low flying; maps cache their own scenery. */
static bool map_details_built[8],bridge_passed[3];
static t3_object *bridge_decks[3];
static int32_t bridge_passes;
static float north_pole_position[3],north_pole_height=326;
static void map_detail_solid(int style,t3_object *node,float x,float y,float z,float w,float h,float depth) {
  struct district *d=&districts[style];if(d->count>=DISTRICT_SOLIDS)CW_FATAL("Map scenery exceeded district collision capacity");int i=d->count++;d->solid[i].alive=1;d->solid[i].custom=node;d->solid[i].box=(t3_box3){{x-w/2,y,z-depth/2},{x+w/2,y+h,z+depth/2}};
}
static void detail_farms(t3_object *root) {
  t3_material *siding=campaign_mat(0xd1c3a5,0),*roof=campaign_mat(0x4e5559,.2f),*metal=campaign_mat(0xa5a59b,.7f),*dark=campaign_mat(0x545955,.5f);
  for(int i=0;i<8;i++){
    structure *barn=&G.buildings[28+i];float x=barn->x-45,z=barn->z-22,y=terrain_height(x,z);
    t3_object *silo=t3_group_new();t3_object_add(root,silo);t3_object_set_position(silo,x,y,z);
    t3_object *body=(t3_object *)t3_mesh_new(t3_cylinder_geometry_new(6.5f,6.5f,28,24,1,false),metal);t3_object_set_position(body,0,14,0);t3_object_add(silo,body);cone(silo,0,30,0,6.8f,5,roof);
    for(int ring=0;ring<5;ring++){t3_mesh *band=t3_mesh_new(t3_torus_geometry_new(6.6f,.18f,6,24,2*PI),dark);t3_object_set_rotation(band,PI/2,0,0);t3_object_set_position(band,0,2+ring*5.5f,0);t3_object_add(silo,(t3_object *)band);}
    map_detail_solid(1,silo,x,y,z,14,34,14);
    x=barn->x+58;z=barn->z+22;y=terrain_height(x,z);t3_object *house=t3_group_new();t3_object_add(root,house);t3_object_set_position(house,x,y,z);
    box(house,0,6,0,22,12,28,siding);for(int side=-1;side<=1;side+=2){t3_object *r=box(house,side*5.8f,15,0,14,1,31,roof);t3_object_set_rotation(r,0,0,-side*.44f);}
    for(int j=-1;j<=1;j+=2){box(house,j*6,7,14.2f,4,4,.4f,dark);box(house,j*6,7,-14.2f,4,4,.4f,dark);}box(house,0,4,14.4f,4,8,.5f,dark);box(house,7,19,-6,2,8,2,siding);
    map_detail_solid(1,house,x,y,z,24,24,32);paved_strip(root,x-4,x+4,z+16,z+70,roof);
  }
}
static void detail_frozen_river(t3_object *root) {
  enum{N=128};float p[(N+1)*6],normal[(N+1)*6];uint32_t ix[N*6];
  t3_material *ice=campaign_mat(0x65848e,.18f),*stone=campaign_mat(0x879293,0),*road=campaign_mat(0x414c51,.05f),*gold=campaign_mat(0xa28c63,.5f);ice->roughness=.32f;
  for(int j=0;j<=N;j++)for(int side=0;side<2;side++){float z=-4096+j*(8192.0f/N),x=950+220*sinf(z*.0007f)+(side?160:-160);int k=(j*2+side)*3;p[k]=x;p[k+1]=0;p[k+2]=z;normal[k]=normal[k+2]=0;normal[k+1]=1;}
  for(int i=0;i<N;i++){int a=i*2,k=i*6;ix[k]=a;ix[k+1]=a+2;ix[k+2]=a+1;ix[k+3]=a+1;ix[k+4]=a+2;ix[k+5]=a+3;}
  t3_geometry *g=t3_geometry_new();t3_attribute *pa=t3_attribute_new(T3_FLOAT32,p,(N+1)*2,3),*na=t3_attribute_new(T3_FLOAT32,normal,(N+1)*2,3),*ia=t3_attribute_new(T3_UINT32,ix,N*6,1);t3_geometry_set_attribute(g,T3_ATTR_POSITION,pa);t3_geometry_set_attribute(g,T3_ATTR_NORMAL,na);t3_geometry_set_index(g,ia);t3_release(pa);t3_release(na);t3_release(ia);t3_mesh *river=t3_mesh_new(g,ice);river->base.receive_shadow=true;t3_object_add(root,(t3_object *)river);t3_release(g);
  for(int i=0;i<64;i++)for(int side=-1;side<=1;side+=2){float z=-3500+i*110,x=950+220*sinf(z*.0007f)+side*170;box(root,x,18,z,9,36,114,stone);}
  for(int bridge=0;bridge<3;bridge++){
    float z=-2200+bridge*2000,x=950+220*sinf(z*.0007f),deck_y=58;t3_object *deck=t3_group_new();t3_object_add(root,deck);bridge_decks[bridge]=deck;
    box(deck,x,deck_y+3,z,660,6,44,stone);box(deck,x,deck_y+6.2f,z,660,.3f,34,road);
    for(int side=-1;side<=1;side+=2){box(deck,x,deck_y+9,z+side*20,660,2,1.2f,gold);for(int j=0;j<22;j++)box(deck,x-315+j*30,deck_y+7.5f,z+side*20,1.2f,6,1.2f,stone);}
    for(int j=0;j<26;j++)box(deck,x-312+j*25,deck_y+6.5f,z,10,.15f,.8f,gold);
    map_detail_solid(5,deck,x,deck_y,z,660,12,44);
    for(int side=-1;side<=1;side+=2){float px=x+side*210;t3_object *pier=t3_group_new();t3_object_add(root,pier);box(pier,px,29,z,22,58,40,stone);map_detail_solid(5,pier,px,0,z,22,58,40);}
    paved_strip(root,x-650,x-330,z-19,z+19,road);paved_strip(root,x+330,x+650,z-19,z+19,road);
  }
}
static void detail_north_pole(t3_object *root) {
  float y=terrain_height(0,0);north_pole_position[0]=0;north_pole_position[1]=y;north_pole_position[2]=0;
  t3_material *red=campaign_mat(0xb52b35,.2f),*white=campaign_mat(0xf2eee4,.15f),*gold=campaign_mat(0xe8bd53,.9f),*stone=campaign_mat(0xbbccd3,.1f);gold->roughness=.20f;
  t3_object *pole=north_pole_node=t3_group_new();t3_object_add(root,pole);t3_object_set_position(pole,0,y,0);
  for(int i=0;i<8;i++){pole_sections[i]=t3_group_new();t3_object_add(pole,pole_sections[i]);t3_object_set_position(pole_sections[i],0,i<7?20+i*40:300,0);}
  for(int i=0;i<28;i++){t3_mesh *band=t3_mesh_new(t3_cylinder_geometry_new(10,10,10.04f,40,1,false),i%2?red:white);t3_object_set_position(band,0,5+(i%4)*10-20,0);t3_object_add(pole_sections[i/4],(t3_object *)band);}
  for(int i=0;i<3;i++){t3_mesh *base=t3_mesh_new(t3_cylinder_geometry_new(30-i*6,30-i*6,5,48,1,false),stone);t3_object_set_position(base,0,2.5f+i*5,0);t3_object_add(pole,(t3_object *)base);}
  oval(pole_sections[7],0,-8,0,22,22,22,gold);cone(pole_sections[7],0,18,0,6,16,gold);
  map_detail_solid(6,pole,0,y,0,22,280,22);map_detail_solid(6,pole,0,y+270,0,44,56,44);
  t3_material *paving=campaign_mat(0xc4d3d8,0);paved_strip(root,-100,100,-100,100,paving);paved_strip(root,-250,-70,-320,-120,paving);paved_strip(root,-175,-145,-220,-25,paving);paved_strip(root,-160,0,-55,-25,paving);paved_strip(root,-1122,-1078,-2450,1500,paving);paved_strip(root,-22,22,-2450,1500,paving);paved_strip(root,-1400,320,-425,-375,paving);
  for(int i=0;i<12;i++){float a=i*2*PI/12;fir_tree(root,sinf(a)*120,cosf(a)*120,24+(i%3)*5,true);}
  cw_debug_f32_array("map.north_pole",north_pole_position,3);cw_debug_f32("map.north_pole_height",&north_pole_height);
}
static void make_map_details(int style) {
  if(map_details_built[style])return;map_details_built[style]=true;
  if(style==1)detail_farms(G.decor[style]);
  if(style==5)detail_frozen_river(G.decor[style]);
  if(style==6)detail_north_pole(G.decor[style]);
  cw_debug_i32("map.bridge_passes",&bridge_passes);
}
static void map_details_reset(void){bridge_passes=0;memset(bridge_passed,0,sizeof bridge_passed);}
static void map_details_update(void) {
  if(G.map_style!=5||G.state!=PLAY)return;
  for(int i=0;i<3;i++){float z=-2200+i*2000,x=950+220*sinf(z*.0007f);if(!bridge_passed[i]&&bridge_decks[i]&&bridge_decks[i]->visible&&fabsf(G.pz-z)<17&&fabsf(G.px-x)<170&&G.py>12&&G.py<48){bridge_passed[i]=true;bridge_passes++;G.score+=500;G.audio_pass=.8f;notify("UNDER THE BRIDGE / +500",3);}}
}
