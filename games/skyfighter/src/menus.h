/* BikeMesa's direct track grid: visible choices, directional focus and
 * click-to-play. The same large buttons serve mouse, keyboard and pad. */
static cw_rect mission_tile(int i) {return (cw_rect){46+(i%4)*220,120+(i/4)*132,208,120};}
static bool menu_contains(cw_rect r) {return menu_ui.pointer.x>=r.x&&menu_ui.pointer.x<r.x+r.w&&menu_ui.pointer.y>=r.y&&menu_ui.pointer.y<r.y+r.h;}
static cw_rect title_button(int i) {return (cw_rect){220+i*180,465,160,40};}
static cw_rect action_button(int i) {if(G.state==WIN&&G.level==4&&capital.active&&capital.age<8)return (cw_rect){195+i*195,H-65,180,40};return (cw_rect){340,230+i*62,280,48};}
static void menu_read_pointer(void) {
  const cw_raw_input *in=cw_input_raw();menu_ui.clicked=(in->mouse_buttons&1)&&!(menu_ui.previous_buttons&1);menu_ui.previous_buttons=in->mouse_buttons;menu_ui.pointer=cw_r2d_window_to_canvas((cw_vec2){in->mouse_x,in->mouse_y});
}
static void menu_load(void) {for(int i=0;i<MISSIONS;i++){char path[64];snprintf(path,sizeof path,"ui/mission-%d.sprite",i);menu_ui.thumbnails[i]=cw_texture_load(path,NULL);}}
static void menu_open_settings(void) {G.return_state=G.state;G.settings_row=0;G.state=SETTINGS;}
static bool menu_tick(void) {
  bool ok=cw_action_just_pressed(0,G.gun),back=cw_action_just_pressed(0,G.back);
  if(G.state==CONTROLS){if(ok||back||cw_action_just_pressed(0,G.missile)||(menu_ui.clicked&&menu_contains((cw_rect){390,455,180,42})))G.state=G.return_state;return true;}
  if(G.state==TITLE){
    int before=G.level;
    if(cw_action_just_pressed(0,G.left)&&G.level%4>0)G.level--;
    if(cw_action_just_pressed(0,G.right)&&G.level%4<3)G.level++;
    if(cw_action_just_pressed(0,G.up)&&G.level>=4)G.level-=4;
    if(cw_action_just_pressed(0,G.down)&&G.level<4)G.level+=4;
    if(menu_ui.clicked)for(int i=0;i<MISSIONS;i++)if(menu_contains(mission_tile(i))){G.level=i;new_mission();return true;}
    if(menu_ui.clicked&&menu_contains(title_button(1))){G.return_state=TITLE;G.state=CONTROLS;return true;}
    if(menu_ui.clicked&&menu_contains(title_button(2))){menu_open_settings();return true;}
    if(cw_action_just_pressed(0,G.missile)){G.return_state=TITLE;G.state=CONTROLS;return true;}
    if(G.level!=before){prefs.progress=(prefs.progress&255u)|((uint32_t)G.level<<8);cw_save_commit();}
    if(ok||cw_action_just_pressed(0,G.pause)||(menu_ui.clicked&&menu_contains(title_button(0))))new_mission();
    return true;
  }
  if(G.state==PAUSED||G.state==WIN||G.state==LOST){
    if(back){G.state=G.state==PAUSED?PLAY:TITLE;G.menu_row=0;return true;}
    if(cw_action_just_pressed(0,G.up)||cw_action_just_pressed(0,G.left))G.menu_row=(G.menu_row+2)%3;
    if(cw_action_just_pressed(0,G.down)||cw_action_just_pressed(0,G.right))G.menu_row=(G.menu_row+1)%3;
    if(menu_ui.clicked)for(int i=0;i<3;i++)if(menu_contains(action_button(i))){G.menu_row=i;ok=true;}
    if(!ok)return true;
    if(G.menu_row==2){G.state=TITLE;G.audio_boost=G.audio_alert=0;return true;}
    if(G.state==PAUSED){if(G.menu_row==0)G.state=PLAY;else menu_open_settings();}
    else if(G.state==WIN){if(G.menu_row==1)G.state=LOST;new_mission();}
    else {if(G.menu_row==1)menu_open_settings();else new_mission();}
    return true;
  }
  return false;
}
static void menu_button(cw_rect r,const char *label,bool selected) {
  cw_r2d_rect((cw_rect){r.x-2,r.y-2,r.w+4,r.h+4},cw_hex(selected?0xffb36aff:0x708797aa));cw_r2d_rect(r,cw_hex(selected?0xd8883cff:0x142a3df2));
  text(G.ui_font,label,r.x+8,r.y+8,21,cw_hex(0xf4f0e8ff),r.w-16,CW_ALIGN_CENTER);
}
static void draw_mission_menu(void) {
  static const char *names[]={"COUNTRY FAIR","TIME WARP","FUEL CHAIN","COASTAL SCRAMBLE","SKY FORTRESS","RED FORTRESS","OPERATION SNOWGLOBE","STEEL TIDE"};
  static const char *settings[]={"FARMLAND","ALPINE VALLEYS","DESERT CANYON","COASTAL ISLAND","CITY / MOTHERSHIP","KREMLIN RAID","NORTH POLE","OFFSHORE RIGS"};
  static const char *brief[]={"12 balloons. Pick the envelope or the basket. Watch what falls.","Eight prop fighters. Six giant cross-marked airships.","Four fuel depots. Line up a run and trigger the secondaries.","Ten enemy interceptors. Take control of the island's airspace.","Break four shield generators. Destroy the giant mothership's core.","Strike three radars and the command palace inside the Kremlin.","Destroy three power cores, then take down giant shielded Santa.","Four offshore rigs, platform defenses and enemy aircraft."};
  cw_r2d_rect((cw_rect){0,0,W,H},cw_hex(0x061422ef));
  text(G.title_font,"SKY FIGHTER",0,22,40,cw_hex(0xffb36aff),W,CW_ALIGN_CENTER);text(G.ui_font,"CHOOSE A MISSION",0,78,19,cw_hex(0xe4eaefff),W,CW_ALIGN_CENTER);
  for(int i=0;i<MISSIONS;i++){
    cw_rect r=mission_tile(i);bool selected=i==G.level;cw_r2d_rect((cw_rect){r.x-3,r.y-3,r.w+6,r.h+6},cw_hex(selected?0xffb36aff:0x314c60ff));cw_r2d_rect(r,cw_hex(0x0c2234ff));
    cw_vec2 pos[4]={{r.x,r.y},{r.x+r.w,r.y},{r.x+r.w,r.y+65},{r.x,r.y+65}},uv[4]={{0,0},{1,0},{1,1},{0,1}};cw_r2d_quad(menu_ui.thumbnails[i],pos,uv,cw_rgba(1,1,1,selected?1:.78f));
    char number[8];snprintf(number,sizeof number,"%02d",i+1);cw_r2d_rect((cw_rect){r.x+5,r.y+5,26,22},cw_hex(0x0a172adf));text(G.ui_font,number,r.x+7,r.y+5,15,cw_hex(0xffd4a5ff),24,CW_ALIGN_CENTER);
    if(prefs.progress&(1u<<i)){cw_r2d_rect((cw_rect){r.x+r.w-65,r.y+6,59,20},cw_hex(0x163d32ed));text(G.ui_font,"CLEARED",r.x+r.w-63,r.y+7,12,cw_hex(0xa9f0bdff),55,CW_ALIGN_CENTER);}
    text(G.ui_font,names[i],r.x+9,r.y+71,16,cw_hex(selected?0xffd0a1ff:0xf0f0eaff),r.w-18,CW_ALIGN_LEFT);text(G.ui_font,settings[i],r.x+9,r.y+99,11,cw_hex(0xa0bdceff),r.w-18,CW_ALIGN_LEFT);
  }
  text(G.ui_font,brief[G.level],70,394,17,cw_hex(0xd4e4ecff),820,CW_ALIGN_CENTER);
  text(G.ui_font,"ARROWS / STICK: CHOOSE     A / ENTER: PLAY     CLICK A MISSION TO PLAY",0,429,14,cw_hex(0xa9c4d6ff),W,CW_ALIGN_CENTER);
  menu_button(title_button(0),"PLAY",true);menu_button(title_button(1),"CONTROLS",false);menu_button(title_button(2),"SETTINGS",false);
}
static void draw_action_menu(void) {
  bool win=G.state==WIN,paused=G.state==PAUSED;
  const char *labels[3]={paused?"RESUME":win?"NEXT MISSION":"RETRY MISSION",win?"REPLAY MISSION":"SETTINGS","MISSION SELECT"};
  cw_r2d_rect((cw_rect){0,0,W,H},cw_hex(0x061422b0));cw_r2d_rect((cw_rect){300,85,360,395},cw_hex(0x0c2135f2));
  text(G.title_font,paused?"PAUSED":win?"MISSION CLEAR":"JET DOWN",315,115,30,cw_hex(0xffbc7aff),330,CW_ALIGN_CENTER);text(G.ui_font,mission_name()+5,315,171,18,cw_hex(0xd4e3ebff),330,CW_ALIGN_CENTER);
  for(int i=0;i<3;i++)menu_button(action_button(i),labels[i],i==G.menu_row);
  text(G.ui_font,paused?"A SELECT    B / START RESUME":"A SELECT    B MISSION SELECT",315,440,14,cw_hex(0xb5cbd8ff),330,CW_ALIGN_CENTER);
}
static void draw_controls_menu(void) {
  cw_r2d_rect((cw_rect){0,0,W,H},cw_hex(0x061422ef));text(G.title_font,"FLIGHT CONTROLS",0,42,34,cw_hex(0xffbb78ff),W,CW_ALIGN_CENTER);
  static const char *actions[]={"BANK / PITCH","GUNS","MISSILE","AFTERBURNER","AIRBRAKE","FLARES","ROLL / LOOP","LOOK AROUND","CYCLE TARGET","PAUSE"};
  static const char *keys[]={"LEFT STICK / D-PAD       ARROWS / WASD","A / RT                            SPACE","X                                   M","B                                   SHIFT","LB                                  CTRL","R3 / LT                             F","RB + STICK             R + ARROWS / Q,E","RIGHT STICK","Y                                   I","START                             P / ESC"};
  for(int i=0;i<10;i++){float y=104+i*30;cw_r2d_rect((cw_rect){160,y-3,640,29},cw_hex(i%2?0x152c3fdf:0x0b2135df));text(G.ui_font,actions[i],178,y,17,cw_hex(0xf0e8dbff),205,CW_ALIGN_LEFT);text(G.ui_font,keys[i],395,y,16,cw_hex(0xbad5e4ff),385,CW_ALIGN_LEFT);}
  text(G.ui_font,"X / M: TAP TO QUEUE, HOLD TO FIRE ON LOCK. FLARES RECHARGE IN 7 SEC.",100,418,13,cw_hex(0xbad5e4ff),760,CW_ALIGN_CENTER);
  menu_button((cw_rect){390,455,180,42},"BACK",true);
}
