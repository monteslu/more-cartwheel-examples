/* BikeMesa's direct track grid: visible choices, directional focus and
 * click-to-play. The same large buttons serve mouse, keyboard and pad.
 *
 * Every screen here is laid out from measured text at the player's text size (cartwheel/render2d.h "text size"): the
 * sizes asked for are the 100% ones, panels and buttons grow from measured widths and heights, and a list that no
 * longer fits shows the rows round its selection. The pointer hit tests ask the same layouts, so a click lands on
 * what is drawn. At 100% the layouts come out where the fixed ones were. */
static const char *mission_names[]={"COUNTRY FAIR","TIME WARP","FUEL CHAIN","COASTAL SCRAMBLE","SKY FORTRESS","RED FORTRESS","OPERATION SNOWGLOBE","STEEL TIDE"};
static const char *mission_settings[]={"FARMLAND","ALPINE VALLEYS","DESERT CANYON","COASTAL ISLAND","CITY / MOTHERSHIP","KREMLIN RAID","NORTH POLE","OFFSHORE RIGS"};
static const char *mission_briefs[]={"12 balloons. Pick the envelope or the basket. Watch what falls.","Eight prop fighters. Six giant cross-marked airships.","Four fuel depots. Line up a run and trigger the secondaries.","Ten enemy interceptors. Take control of the island's airspace.","Break four shield generators. Destroy the giant mothership's core.","Strike three radars and the command palace inside the Kremlin.","Destroy three power cores, then take down giant shielded Santa.","Four offshore rigs, platform defenses and enemy aircraft."};
static const char *title_labels[3]={"{guns} PLAY","{missile} CONTROLS","{settings} SETTINGS"};
static const char *title_choose="{bank_left|bank_right|climb|dive}  CHOOSE A MISSION";

static float line_height(cw_font f,float size){return measure(f,"Ag",size,0).height;}
/* gaps shrink as the text grows, so the text gets the room */
static float menu_gap(float px){return px*fminf(1,1/cw_text_scale());}

/* the title: the logo (art, drawn as asked), the choice line, the mission grid, the selected brief, the buttons */
typedef struct {float choose_y,grid_top,tile_h,thumb_h,name_h,brief_y,button_y,button_h,button_w;int first_row,rows;} title_layout;
static title_layout title_measure(void) {
  title_layout L;
  L.choose_y=78;
  float choose_h=cw_prompt_text_size(G.ui_font,title_choose,0,19).y;
  L.grid_top=fmaxf(120,L.choose_y+choose_h+menu_gap(12));
  L.button_h=fmaxf(40,line_height(G.ui_font,21)+8);
  L.button_w=160;for(int i=0;i<3;i++)L.button_w=fmaxf(L.button_w,cw_prompt_text_size(G.ui_font,title_labels[i],0,21).x+24);
  L.button_y=H-menu_gap(35)-L.button_h;
  float brief_h=0;for(int i=0;i<MISSIONS;i++)brief_h=fmaxf(brief_h,measure(G.ui_font,mission_briefs[i],17,820).height);
  L.name_h=0; /* the tallest name and setting of one tile (either can wrap) */
  for(int i=0;i<MISSIONS;i++)L.name_h=fmaxf(L.name_h,measure(G.ui_font,mission_names[i],16,190).height+2+measure(G.ui_font,mission_settings[i],13,190).height);
  float text_h=6+L.name_h+4;
  float room=L.button_y-menu_gap(10)-brief_h-menu_gap(10)-L.grid_top; /* the grid's height */
  L.thumb_h=65;L.tile_h=fmaxf(120,L.thumb_h+text_h);L.rows=2;
  if(2*L.tile_h+12>room){L.rows=1;L.tile_h=fmaxf(text_h,fminf(text_h+150,room));L.thumb_h=L.tile_h-text_h;} /* one row: it takes the room, the thumbnail grows */
  L.first_row=L.rows==2?0:G.level/4;
  float grid_bottom=L.grid_top+L.rows*L.tile_h+(L.rows-1)*12;
  L.brief_y=grid_bottom+fmaxf(menu_gap(10),(L.button_y-grid_bottom-brief_h)*.5f);
  return L;
}
/* a mission's tile, or a zero rect when its row is scrolled away */
static cw_rect mission_tile_at(const title_layout *L,int i){
  int row=i/4-L->first_row;if(row<0||row>=L->rows)return (cw_rect){0,0,0,0};
  return (cw_rect){46+(i%4)*220,L->grid_top+row*(L->tile_h+12),208,L->tile_h};
}
static cw_rect title_button_at(const title_layout *L,int i){float gap=20,x0=(W-3*L->button_w-2*gap)*.5f;return (cw_rect){x0+i*(L->button_w+gap),L->button_y,L->button_w,L->button_h};}
static cw_rect mission_tile(int i){title_layout L=title_measure();return mission_tile_at(&L,i);}
static cw_rect title_button(int i){title_layout L=title_measure();return title_button_at(&L,i);}

/* the action menus: pause, mission clear, jet down (a panel), and the mothership's three buttons in a row */
static bool mothership_menu(void){return G.state==WIN&&((G.level==4&&capital.active&&capital.age<8)||menu_preview==2);}
static const char *action_hint(bool paused,bool split){
  if(paused)return split?"{guns} SELECT\n{back} OR {pause} RESUME":"{guns} SELECT    {back} OR {pause} RESUME";
  return split?"{guns} SELECT\n{back} MISSION SELECT":"{guns} SELECT    {back} MISSION SELECT";
}
static void action_labels(const char *labels[3]){
  bool win=G.state==WIN,paused=G.state==PAUSED;
  labels[0]=paused?"RESUME":win?"NEXT MISSION":"RETRY MISSION";labels[1]=win?"REPLAY MISSION":"SETTINGS";labels[2]="MISSION SELECT";
}
typedef struct {cw_rect panel;float title_y,name_y,button_y,button_h,button_w,pitch,hint_y;bool split;} action_layout;
static action_layout action_measure(void) {
  action_layout L;bool paused=G.state==PAUSED,win=G.state==WIN;const char *labels[3];action_labels(labels);
  float label_w=0;for(int i=0;i<3;i++)label_w=fmaxf(label_w,measure(G.ui_font,labels[i],21,0).width);
  if(mothership_menu()){
    L.button_h=fmaxf(40,line_height(G.ui_font,21)+8);L.button_w=fmaxf(180,label_w+24);
    L.button_y=H-25-L.button_h;L.pitch=L.button_w+15;L.panel=(cw_rect){(W-3*L.button_w-30)*.5f,0,0,0};
    return L;
  }
  const char *title=paused?"PAUSED":win?"MISSION CLEAR":"JET DOWN";
  cw_text_metrics tm=measure(G.title_font,title,30,0),nm=measure(G.ui_font,mission_name()+5,18,0);
  L.split=cw_prompt_text_size(G.ui_font,action_hint(paused,false),0,18).x>W-60;
  cw_vec2 hint=cw_prompt_text_size(G.ui_font,action_hint(paused,L.split),0,18);
  L.button_h=fmaxf(48,line_height(G.ui_font,21)+17);L.button_w=fmaxf(280,label_w+40);L.pitch=L.button_h+menu_gap(14);
  float w=fmaxf(360,fmaxf(fmaxf(tm.width,nm.width),fmaxf(hint.x,L.button_w))+30);
  float top=menu_gap(30),title_gap=menu_gap(20),name_gap=menu_gap(32),hint_gap=menu_gap(35),bottom=menu_gap(16);
  float h=top+tm.height+title_gap+nm.height+name_gap+3*L.button_h+2*(L.pitch-L.button_h)+hint_gap+hint.y+bottom;
  float y=h+145<=H?85:fmaxf(4,(H-h)*.5f);
  L.panel=(cw_rect){(W-w)*.5f,y,w,h};
  L.title_y=y+top;L.name_y=L.title_y+tm.height+title_gap;L.button_y=L.name_y+nm.height+name_gap;
  L.hint_y=L.button_y+3*L.button_h+2*(L.pitch-L.button_h)+hint_gap;
  return L;
}
static cw_rect action_button(int i){
  action_layout L=action_measure();
  if(mothership_menu())return (cw_rect){L.panel.x+i*L.pitch,L.button_y,L.button_w,L.button_h};
  return (cw_rect){(W-L.button_w)*.5f,L.button_y+i*L.pitch,L.button_w,L.button_h};
}

/* the controls page: a table that shows the rows that fit, scrolled by up and down */
#define CONTROL_ROWS 10
static const char *control_actions[CONTROL_ROWS]={"BANK / PITCH","GUNS","MISSILE","AFTERBURNER","AIRBRAKE","FLARES","ROLL / LOOP","LOOK AROUND","CYCLE TARGET","PAUSE"};
static int controls_scroll;
static void control_keys(const char *keys[CONTROL_ROWS]){
  /* each control as the engine draws it for the pilot's device: a pad's glyphs, or the keys (the stick look is a pad
   * control, and a keyboard rolls on Q and E) */
  const char *k[CONTROL_ROWS]={"{bank_left|bank_right|climb|dive}","{guns}","{missile}","{afterburner}","{airbrake}","{countermeasures}",
    on_pad()?"{aerobatic_roll} + {bank_left|bank_right|climb|dive}":"{aerobatic_roll} + {bank_left|bank_right|climb|dive}    {roll_left|roll_right}",
    on_pad()?"{look_left|look_right|look_up|look_down}":"PAD ONLY","{settings}","{pause}"};
  memcpy(keys,k,sizeof k);
}
static const char *controls_note(bool split){
  return split?"{missile}: TAP TO QUEUE, HOLD TO FIRE ON LOCK.\nFLARES RECHARGE IN 7 SEC.":"{missile}: TAP TO QUEUE, HOLD TO FIRE ON LOCK. FLARES RECHARGE IN 7 SEC.";
}
typedef struct {float title_y,rows_top,row_h,action_w,keys_w,table_x,table_w,note_y,back_y,back_h,back_w;int visible;bool split;} controls_layout;
static controls_layout controls_measure(void) {
  controls_layout L;const char *keys[CONTROL_ROWS];control_keys(keys);
  L.title_y=menu_gap(42);
  float title_h=measure(G.title_font,"FLIGHT CONTROLS",34,0).height;
  L.rows_top=fmaxf(104*fminf(1,1/cw_text_scale()),L.title_y+title_h+menu_gap(14));
  L.action_w=205;L.keys_w=385;float key_h=0;
  for(int i=0;i<CONTROL_ROWS;i++){
    L.action_w=fmaxf(L.action_w,measure(G.ui_font,control_actions[i],17,0).width+12);
    cw_vec2 s=cw_prompt_text_size(G.ui_font,keys[i],0,18);L.keys_w=fmaxf(L.keys_w,s.x);key_h=fmaxf(key_h,s.y);
  }
  L.row_h=fmaxf(30,fmaxf(line_height(G.ui_font,17),key_h)+2);
  L.table_w=fminf(W-20,18+L.action_w+12+L.keys_w+20);L.table_x=(W-L.table_w)*.5f;
  L.back_h=fmaxf(42,line_height(G.ui_font,21)+11);L.back_w=fmaxf(180,measure(G.ui_font,"BACK",21,0).width+24);
  L.back_y=H-menu_gap(43)-L.back_h;
  L.split=cw_prompt_text_size(G.ui_font,controls_note(false),0,16).x>W-40;
  L.note_y=L.back_y-menu_gap(14)-cw_prompt_text_size(G.ui_font,controls_note(L.split),0,16).y;
  L.visible=(int)fmaxf(1,fminf(CONTROL_ROWS,floorf((L.note_y-menu_gap(6)-L.rows_top)/L.row_h)));
  controls_scroll=(int)clampf(controls_scroll,0,CONTROL_ROWS-L.visible);
  return L;
}

/* the settings panel: the rows that fit round the selected one */
static int settings_scroll;
static const char *settings_labels[SETTINGS_ROWS]={"PITCH","GRAPHICS","FLIGHT CONTROLS","MUSIC","SOUND EFFECTS","RUMBLE","TEXT SIZE","TEXT BACKGROUND"};
static void settings_value(int i,char *value,size_t n){
  if(i==0)snprintf(value,n,"%s",prefs.invert_pitch?"INVERTED":"NORMAL");
  else if(i==1)snprintf(value,n,"%s",cw_quality_name(cw_quality_get()));
  else if(i==2)snprintf(value,n,"%s",prefs.expert_controls?"EXPERT":"STANDARD");
  else if(i==5)snprintf(value,n,"%s",prefs.rumble_off?"OFF":"ON");
  else if(i==6)snprintf(value,n,"%s",cw_text_scale_label());
  else if(i==7){snprintf(value,n,"%s",cw_text_backdrop_label());for(char *c=value;*c;c++)if(*c>='a'&&*c<='z')*c=(char)(*c-32);}
  else snprintf(value,n,"%d%%",i==3?prefs.music_volume:prefs.sfx_volume);
}
typedef struct {cw_rect panel;float title_y,sub_y,hint_y,row_x,row_w,rows_top,pitch,line_h,back_y,back_h,back_w;int visible;} settings_layout;
static settings_layout settings_measure(void) {
  settings_layout L;
  float label_w=0,value_w=measure(G.ui_font,"VERY LOW",17,0).width;char value[96];
  for(int i=0;i<SETTINGS_ROWS;i++){label_w=fmaxf(label_w,measure(G.ui_font,settings_labels[i],17,0).width);settings_value(i,value,sizeof value);value_w=fmaxf(value_w,measure(G.ui_font,value,17,0).width);}
  float w=fminf(W-16,fmaxf(540,40+13+label_w+40+value_w+25));
  float top=menu_gap(40),bottom=H-menu_gap(45);
  L.panel=(cw_rect){(W-w)*.5f,top,w,bottom-top};
  L.row_x=L.panel.x+20;L.row_w=w-40;
  L.title_y=top+menu_gap(12);
  L.sub_y=L.title_y+measure(G.title_font,"SETTINGS",30,0).height+menu_gap(4);
  L.hint_y=L.sub_y+line_height(G.ui_font,14)+menu_gap(2);
  float header_bottom=L.hint_y+line_height(G.ui_font,14);
  L.line_h=line_height(G.ui_font,17);
  L.rows_top=fmaxf(header_bottom+menu_gap(14),170*fminf(1,1/cw_text_scale()));
  L.pitch=L.line_h+9.5f;
  L.back_h=fmaxf(30,line_height(G.ui_font,21)-1);L.back_w=fmaxf(120,measure(G.ui_font,"BACK",21,0).width+24);
  L.back_y=bottom-menu_gap(15)-L.back_h;
  L.visible=(int)fmaxf(1,fminf(SETTINGS_ROWS,floorf((L.back_y-4-L.rows_top+5)/L.pitch)));
  if(G.settings_row<settings_scroll)settings_scroll=G.settings_row;
  if(G.settings_row>=settings_scroll+L.visible)settings_scroll=G.settings_row-L.visible+1;
  settings_scroll=(int)clampf(settings_scroll,0,SETTINGS_ROWS-L.visible);
  return L;
}
static cw_rect settings_row_box(const settings_layout *L,int i){return (cw_rect){L->row_x,L->rows_top+(i-settings_scroll)*L->pitch-5,L->row_w,L->pitch-4};}
static cw_rect settings_back(const settings_layout *L){return (cw_rect){L->panel.x+30,L->back_y,L->back_w,L->back_h};}
static int settings_pointer(cw_vec2 p,int *direction) {
  settings_layout L=settings_measure();cw_rect b=settings_back(&L);
  if(p.x>=b.x&&p.x<=b.x+b.w&&p.y>=b.y&&p.y<=b.y+b.h)return SETTINGS_BACK;
  for(int i=settings_scroll;i<settings_scroll+L.visible;i++){
    float y=L.rows_top+(i-settings_scroll)*L.pitch-5;
    if(p.x>=L.row_x&&p.x<=L.row_x+L.row_w&&p.y>=y&&p.y<y+L.pitch){*direction=p.x<L.row_x+L.row_w*.72f?-1:1;return i;}
  }
  return -1;
}

static void menu_read_pointer(void) {
  const cw_raw_input *in=cw_input_raw();menu_ui.clicked=(in->mouse_buttons&1)&&!(menu_ui.previous_buttons&1);menu_ui.previous_buttons=in->mouse_buttons;menu_ui.pointer=cw_r2d_window_to_canvas((cw_vec2){in->mouse_x,in->mouse_y});
}
static bool menu_contains(cw_rect r) {return menu_ui.pointer.x>=r.x&&menu_ui.pointer.x<r.x+r.w&&menu_ui.pointer.y>=r.y&&menu_ui.pointer.y<r.y+r.h;}
static void menu_load(void) {for(int i=0;i<MISSIONS;i++){char path[64];snprintf(path,sizeof path,"ui/mission-%d.sprite",i);menu_ui.thumbnails[i]=cw_texture_load(path,NULL);}}
static void menu_open_settings(void) {G.return_state=G.state;G.settings_row=0;G.state=SETTINGS;}
static bool menu_tick(void) {
  bool ok=cw_action_just_pressed(0,G.gun),back=cw_action_just_pressed(0,G.back);
  if(G.state==CONTROLS){
    controls_layout L=controls_measure();
    if(cw_action_just_pressed(0,G.up))controls_scroll=controls_scroll>0?controls_scroll-1:0;
    if(cw_action_just_pressed(0,G.down))controls_scroll=controls_scroll<CONTROL_ROWS-L.visible?controls_scroll+1:controls_scroll;
    if(ok||back||cw_action_just_pressed(0,G.missile)||(menu_ui.clicked&&menu_contains((cw_rect){(W-L.back_w)*.5f,L.back_y,L.back_w,L.back_h}))){controls_scroll=0;G.state=G.return_state;}
    return true;
  }
  if(G.state==TITLE){
    int before=G.level;
    if(cw_action_just_pressed(0,G.left)&&G.level%4>0)G.level--;
    if(cw_action_just_pressed(0,G.right)&&G.level%4<3)G.level++;
    if(cw_action_just_pressed(0,G.up)&&G.level>=4)G.level-=4;
    if(cw_action_just_pressed(0,G.down)&&G.level<4)G.level+=4;
    if(menu_ui.clicked){
      title_layout L=title_measure();
      for(int i=0;i<MISSIONS;i++){cw_rect r=mission_tile_at(&L,i);if(r.w>0&&menu_contains(r)){G.level=i;new_mission();return true;}}
      if(menu_contains(title_button_at(&L,1))){G.return_state=TITLE;G.state=CONTROLS;return true;}
      if(menu_contains(title_button_at(&L,2))){menu_open_settings();return true;}
    }
    if(cw_action_just_pressed(0,G.missile)){G.return_state=TITLE;G.state=CONTROLS;return true;}
    if(G.level!=before){prefs.progress=(uint16_t)((prefs.progress&255u)|((uint32_t)G.level<<8));cw_save_commit();}
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
/* a button: its label (button prompts allowed) centred in it by the measured size */
static void menu_button(cw_rect r,const char *label,bool selected) {
  cw_r2d_rect((cw_rect){r.x-2,r.y-2,r.w+4,r.h+4},cw_hex(selected?0xffb36aff:0x708797aa));cw_r2d_rect(r,cw_hex(selected?0xd8883cff:0x142a3df2));
  cw_vec2 s=cw_prompt_text_size(G.ui_font,label,0,21);
  ptext(G.ui_font,label,r.x,r.y+(r.h-s.y)*.5f,21,cw_hex(0xf4f0e8ff),r.w,CW_ALIGN_CENTER);
}
/* more rows above or below a list that shows only some: a small chevron at x, pointing up or down */
static void menu_more(float x,float y,bool up){
  float d=up?-8:8;cw_color c=cw_hex(0xffb36aff);
  cw_r2d_line((cw_vec2){x-11,y-d*.5f},(cw_vec2){x,y+d*.5f},2,c);cw_r2d_line((cw_vec2){x,y+d*.5f},(cw_vec2){x+11,y-d*.5f},2,c);
}
static void draw_mission_menu(void) {
  title_layout L=title_measure();
  cw_r2d_rect((cw_rect){0,0,W,H},cw_hex(0x061422ef));
  /* the logo: art, drawn as asked at every text size */
  cw_r2d_text(G.title_font,"SKY FIGHTER",(cw_vec2){0,22},&(cw_text_style){ .size=40,.color=cw_hex(0xffb36aff),.wrap_width=W,.align=CW_ALIGN_CENTER,
    .outline_width=40*0.032f,.outline_color=cw_hex(0x07111be0),.fixed_size=true });
  ptext(G.ui_font,title_choose,0,L.choose_y,19,cw_hex(0xe4eaefff),W,CW_ALIGN_CENTER);
  for(int i=0;i<MISSIONS;i++){
    cw_rect r=mission_tile_at(&L,i);if(r.w<=0)continue;
    bool selected=i==G.level;cw_r2d_rect((cw_rect){r.x-3,r.y-3,r.w+6,r.h+6},cw_hex(selected?0xffb36aff:0x314c60ff));cw_r2d_rect(r,cw_hex(0x0c2234ff));
    if(L.thumb_h>1){ /* the thumbnail, its middle band when the text leaves it less height */
      float f=L.thumb_h/65,v0=(1-fminf(f,1))*.5f,v1=1-v0,u0=f>1?(1-1/f)*.5f:0,u1=1-u0; /* a band of the picture: shorter, or narrower when taller */
      cw_vec2 pos[4]={{r.x,r.y},{r.x+r.w,r.y},{r.x+r.w,r.y+L.thumb_h},{r.x,r.y+L.thumb_h}},uv[4]={{u0,v0},{u1,v0},{u1,v1},{u0,v1}};cw_r2d_quad(menu_ui.thumbnails[i],pos,uv,cw_rgba(1,1,1,selected?1:.78f));
    }
    char number[8];snprintf(number,sizeof number,"%02d",i+1);
    cw_text_metrics nm=measure(G.ui_font,number,15,0);
    cw_r2d_rect((cw_rect){r.x+5,r.y+5,nm.width+8,nm.height},cw_hex(0x0a172adf));text(G.ui_font,number,r.x+9,r.y+5,15,cw_hex(0xffd4a5ff),0,CW_ALIGN_LEFT);
    if(prefs.progress&(1u<<i)){
      cw_text_metrics cm=measure(G.ui_font,"CLEARED",13,0);float cx=r.x+r.w-6-cm.width-8;
      cw_r2d_rect((cw_rect){cx,r.y+6,cm.width+8,cm.height},cw_hex(0x163d32ed));text(G.ui_font,"CLEARED",cx+4,r.y+6,13,cw_hex(0xa9f0bdff),0,CW_ALIGN_LEFT);
    }
    float y=r.y+L.thumb_h+6;
    text(G.ui_font,mission_names[i],r.x+9,y,16,cw_hex(selected?0xffd0a1ff:0xf0f0eaff),190,CW_ALIGN_LEFT);
    float set_h=measure(G.ui_font,mission_settings[i],13,190).height; /* the setting on the tile's bottom edge, under a name of one line or two */
    text(G.ui_font,mission_settings[i],r.x+9,r.y+r.h-4-set_h,13,cw_hex(0xa0bdceff),190,CW_ALIGN_LEFT);
  }
  if(L.rows==1){ /* one row shown: the other row is a press away */
    float mid=L.grid_top+L.tile_h*.5f;
    if(L.first_row>0)menu_more(23,mid,true);else menu_more(23,mid,false);
  }
  text(G.ui_font,mission_briefs[G.level],70,L.brief_y,17,cw_hex(0xd4e4ecff),820,CW_ALIGN_CENTER);
  for(int i=0;i<3;i++)menu_button(title_button_at(&L,i),title_labels[i],i==0);
}
static void draw_action_menu(void) {
  action_layout L=action_measure();bool win=G.state==WIN,paused=G.state==PAUSED;
  const char *labels[3];action_labels(labels);
  cw_r2d_rect((cw_rect){0,0,W,H},cw_hex(0x061422b0));cw_r2d_rect(L.panel,cw_hex(0x0c2135f2));
  ctext(G.title_font,paused?"PAUSED":win?"MISSION CLEAR":"JET DOWN",W*.5f,L.title_y,30,cw_hex(0xffbc7aff));
  ctext(G.ui_font,mission_name()+5,W*.5f,L.name_y,18,cw_hex(0xd4e3ebff));
  for(int i=0;i<3;i++)menu_button(action_button(i),labels[i],i==G.menu_row);
  ptext(G.ui_font,action_hint(paused,L.split),0,L.hint_y,18,cw_hex(0xb5cbd8ff),W,CW_ALIGN_CENTER);
}
static void draw_mothership_menu(void) {
  action_layout L=action_measure();
  ctext(G.title_font,"MOTHERSHIP DESTROYED",W*.5f,150,32,cw_hex(0xffd0a5ff));
  const char *labels[]={"NEXT MISSION","REPLAY","MISSION SELECT"};for(int i=0;i<3;i++)menu_button(action_button(i),labels[i],G.menu_row==i);
  (void)L;
}
static void draw_controls_menu(void) {
  controls_layout L=controls_measure();const char *keys[CONTROL_ROWS];control_keys(keys);
  cw_r2d_rect((cw_rect){0,0,W,H},cw_hex(0x061422ef));ctext(G.title_font,"FLIGHT CONTROLS",W*.5f,L.title_y,34,cw_hex(0xffbb78ff));
  for(int i=controls_scroll;i<controls_scroll+L.visible;i++){
    float y=L.rows_top+(i-controls_scroll)*L.row_h;
    cw_r2d_rect((cw_rect){L.table_x,y-3,L.table_w,L.row_h-1},cw_hex(i%2?0x152c3fdf:0x0b2135df));
    float th=line_height(G.ui_font,17),kh=cw_prompt_text_size(G.ui_font,keys[i],0,18).y;
    text(G.ui_font,control_actions[i],L.table_x+18,y-3+(L.row_h-1-th)*.5f,17,cw_hex(0xf0e8dbff),0,CW_ALIGN_LEFT);
    ptext(G.ui_font,keys[i],L.table_x+18+L.action_w+12,y-3+(L.row_h-1-kh)*.5f,18,cw_hex(0xbad5e4ff),L.keys_w,CW_ALIGN_LEFT);
  }
  if(controls_scroll>0)menu_more(L.table_x+L.table_w+5>W-12?W-12:L.table_x+L.table_w+12,L.rows_top+8,true);
  if(controls_scroll+L.visible<CONTROL_ROWS)menu_more(L.table_x+L.table_w+5>W-12?W-12:L.table_x+L.table_w+12,L.rows_top+L.visible*L.row_h-12,false);
  ptext(G.ui_font,controls_note(L.split),0,L.note_y,16,cw_hex(0xbad5e4ff),W,CW_ALIGN_CENTER);
  menu_button((cw_rect){(W-L.back_w)*.5f,L.back_y,L.back_w,L.back_h},"BACK",true);
}
static void draw_settings_menu(void) {
  settings_layout L=settings_measure();
  cw_color white=cw_hex(0xe3ece7ff),amber=cw_hex(0xffb366ff),muted=cw_hex(0x93b5beff);
  cw_r2d_rect(L.panel,cw_hex(0x071522f2));
  text(G.title_font,"SETTINGS",L.panel.x+30,L.title_y,30,amber,0,CW_ALIGN_LEFT);
  text(G.ui_font,"FLIGHT / GRAPHICS / AUDIO / TEXT",L.panel.x+31,L.sub_y,14,muted,0,CW_ALIGN_LEFT);
  text(G.ui_font,"UP / DOWN SELECT   LEFT / RIGHT ADJUST",L.panel.x+31,L.hint_y,14,muted,0,CW_ALIGN_LEFT);
  char value[96];
  for(int i=settings_scroll;i<settings_scroll+L.visible;i++) {
    cw_rect box=settings_row_box(&L,i);float ty=box.y+(box.h-L.line_h)*.5f;
    if(i==G.settings_row)cw_r2d_rect(box,cw_hex(0x29414edb));
    cw_text_metrics lm=text(G.ui_font,settings_labels[i],L.row_x+13,ty,17,i==G.settings_row?amber:white,0,CW_ALIGN_LEFT);
    settings_value(i,value,sizeof value);
    float right=L.row_x+L.row_w-25,vw=measure(G.ui_font,value,17,0).width;
    rtext(G.ui_font,value,right,ty,17,white);
    if(i==3||i==4) { /* the volume: a bar between the label and the value */
      float volume=(i==3?prefs.music_volume:prefs.sfx_volume)*.01f;
      float x0=fmaxf(L.row_x+250,L.row_x+13+lm.width+14),x1=right-vw-14;
      if(x1-x0>30){
        cw_r2d_rect((cw_rect){x0,box.y+box.h*.5f-1.5f,x1-x0,3},cw_hex(0x43545bff));
        cw_r2d_rect((cw_rect){x0,box.y+box.h*.5f-1.5f,(x1-x0)*volume,3},amber);
      }
    }
  }
  float mx=L.row_x+L.row_w+10;
  if(settings_scroll>0)menu_more(mx,L.rows_top+8,true);
  if(settings_scroll+L.visible<SETTINGS_ROWS)menu_more(mx,L.rows_top+L.visible*L.pitch-16,false);
  cw_rect b=settings_back(&L);menu_button(b,"BACK",true);
  cw_text_metrics sm=measure(G.ui_font,"CHANGES SAVE AUTOMATICALLY",13,0);
  text(G.ui_font,"CHANGES SAVE AUTOMATICALLY",b.x+b.w+30,b.y+(b.h-sm.height)*.5f,13,muted,0,CW_ALIGN_LEFT);
}
