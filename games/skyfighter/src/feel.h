/* feel.h: what makes a sortie read and pay off. Bandits you can pick out of the sky (a vapour trail, a hot exhaust
 * and a bright edge), a kill that swells into a fireball and drops burning pieces, smoke and fire from a hurt jet,
 * clouds that stay in the sky on the cheap presets, and an objective cue that points at the target ahead.
 *
 * Flashes swell and fade: a blast's light rises over FLASH_RISE seconds (no step over a tenth of its peak in one
 * tick) and a new rise waits a second after the last one, so a chain of kills reads as one glow, never a strobe.
 *
 * Test switches ("sw <name> <0|1>", each one brings back the game as it was before, for the gates' controls) and
 * test hooks ("sw bandit_at <m>", "sw blasts <n>", "sw health <n>", "sw cloud_view <site>", "sw flicker <on> <off>"). */

enum { SW_OBJECTIVE_NEAREST, SW_LOCK_FLICKER, SW_PLAIN_BANDITS, SW_OLD_BLASTS, SW_NO_DAMAGE_STATES, SW_NO_CLOUD_CARDS,
       SW_LONE_BANDITS, SW_PLAIN_GUNS, SW_INSTANT_FLASH, SW_COUNT };
static const char *const sw_names[SW_COUNT] = { "objective_nearest", "lock_flicker", "plain_bandits", "old_blasts",
  "no_damage_states", "no_cloud_cards", "lone_bandits", "plain_guns", "instant_flash" };
static int32_t sw[SW_COUNT];

#define KILLS 4
#define CARD_PARTS 3
#define DEBRIS 4
#define LIGHTS 2 /* flash lights: none on Very Low, one on Low and Medium, two on High and Ultra (the "flash_lights" knob) */
#define FLASH_RISE .30f
#define FLASH_FADE .65f
typedef struct { t3_object *node; cw_node smoke; float x, y, z, vx, vy, vz, age; int active; } debris;
static struct {
  /* bandits: a vapour trail, a hot exhaust and the hp they started with */
  cw_node contrail[ENEMIES]; t3_object *glow[ENEMIES]; int32_t max_hp[ENEMIES], smoking[ENEMIES]; int rim_on, rim_ace[ENEMIES];
  /* kills: a fireball, a smoke column, a swelling light, heat ripple and burning pieces */
  cw_node fire[KILLS], core[KILLS], smoke[KILLS], light[LIGHTS], ripple[KILLS]; float light_age[LIGHTS], light_peak[LIGHTS], light_prev[LIGHTS]; int next_light;
  debris pieces[KILLS * DEBRIS]; t3_material *charred; int next_kill;
  float since_rise; int32_t rises; float min_gap, max_step; /* fx.flash_*: the gates read these */
  /* the player's jet: smoke below 60 % airframe, fire below 30 % */
  cw_node hurt_smoke, hurt_fire; int32_t hurt_state;
  /* the score pop over TARGET DESTROYED */
  int32_t pop_score; float pop_age;
  /* guns, flights, low passes */
  cw_node sparks, puff, spray; int lead, twist, inbound; int32_t flight_max, active_max, low_passes, smoked; float low_time;
  /* cumulus cards: soft lit billboards where the volumes stand, drawn when the volumetric fog is off */
  cw_node cards[8][CARD_PARTS]; float card_size[CARD_PARTS]; int32_t cards_on;
  /* the objective as the HUD shows it */
  int32_t objective; float objective_view[3]; /* bearing and elevation off the nose (degrees), distance (m) */
  /* tests */
  int32_t counts[8]; float values[9];
  int flicker_on, flicker_off, flicker_t; int32_t test_screen_ok, test_site; float test_screen[2], test_cloud[2];
} F;

static float ease_in_out(float t) { t = clampf(t, 0, 1); return .5f - .5f * cosf(t * PI); }

static cw_node fx_particles(const char *name, int count, float life, float size, const char *blend, const char *ramp, const char *curve) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_set(n, "emitting", cw_val_bool(false)); cw_node_set(n, "amount", cw_val_int(count));
  cw_node_set(n, "lifetime", cw_val_float(life)); cw_node_set(n, "lifetime_random", cw_val_float(.3f));
  cw_node_set(n, "one_shot", cw_val_bool(true)); cw_node_set(n, "explosiveness", cw_val_float(.92f));
  cw_node_set(n, "emission_shape", cw_val_enum("SPHERE")); cw_node_set(n, "emission_radius", cw_val_float(3));
  cw_node_set(n, "spread", cw_val_float(180)); cw_node_set(n, "size", cw_val_float(size)); cw_node_set(n, "size_random", cw_val_float(.45f));
  cw_node_set(n, "spin", cw_val_float(.6f)); cw_node_set(n, "soft_distance", cw_val_float(4));
  cw_node_set(n, "texture", cw_val_string("effects/billow.sprite")); cw_node_set(n, "blend", cw_val_enum(blend));
  cw_node_set(n, "color_ramp", cw_val_string(ramp)); cw_node_set(n, "size_curve", cw_val_string(curve));
  cw_node_add_child(cw_tree_root(G.tree), n); return n;
}

static void bandit_rim(int on) {
  if (on == F.rim_on) return;
  F.rim_on = on;
  cw_rim_light_desc d = cw_rim_light_default();
  d.color = (t3_color){ 1, .86f, .66f }; d.strength = 2.6f; d.power = 2.2f; d.near = 90; d.far = 380; d.close = 0;
  for (int i = 0; i < ENEMIES; i++) {
    F.rim_ace[i] = 0;
    if (on) cw_rim_light_apply_tree(G.enemies[i].modern, &d), cw_rim_light_apply_tree(G.enemies[i].vintage, &d);
    else cw_rim_light_remove_tree(G.enemies[i].modern), cw_rim_light_remove_tree(G.enemies[i].vintage);
  }
}

static void sw_cmd(int argc, const char **argv, cw_reply *r);
static void feel_make(void) {
  char name[40];
  for (int i = 0; i < ENEMIES; i++) {
    snprintf(name, sizeof name, "BanditTrail%d", i);
    cw_node t = F.contrail[i] = cw_node_new("Trail3D", name);
    cw_node_set(t, "length", cw_val_int(70)); cw_node_set(t, "width", cw_val_float(0));
    cw_node_set(t, "blend", cw_val_enum("ALPHA")); cw_node_set(t, "color", cw_val_color(.95f, .97f, 1, .55f));
    cw_node_set(t, "color_ramp", cw_val_string("0 1 1 1 0, 0.55 1 1 1 0.45, 0.92 1 1 1 0.9, 1 1 1 1 0.3"));
    cw_node_set(t, "visible", cw_val_bool(false)); cw_node_set(t, "emitting", cw_val_bool(false));
    cw_node_add_child(cw_tree_root(G.tree), t);
    /* the exhaust: a hot core over both nozzles, bright enough for bloom to carry it past a kilometre */
    t3_material *hot = t3_mesh_basic_material_new(0xffa040);
    hot->color = (t3_color){ 7, 2.6f, .7f }; hot->transparent = true; hot->opacity = .85f; hot->blending = T3_ADDITIVE_BLENDING; hot->depth_write = false;
    t3_mesh *m = t3_mesh_new(t3_sphere_geometry_new(1, 10, 7), hot);
    m->base.cast_shadow = m->base.receive_shadow = false;
    t3_object_set_position(m, 0, -.35f, 5.6f); t3_object_set_scale(m, 1.25f, .7f, 1.7f);
    t3_object_add(G.enemies[i].node, (t3_object *)m); F.glow[i] = (t3_object *)m;
  }
  bandit_rim(1);
  for (int k = 0; k < KILLS; k++) {
    /* the fireball comes up out of the flash (alpha from a quarter to full over the first tenth of its life) */
    snprintf(name, sizeof name, "KillFire%d", k);
    F.fire[k] = fx_particles(name, 70, 1.5f, 13, "ALPHA",
      "0 1 0.8 0.4 0.25, 0.1 1 0.72 0.3 1, 0.3 1 0.32 0.05 0.9, 0.55 0.18 0.11 0.08 0.75, 1 0.07 0.07 0.07 0",
      "0 0.35, 0.15 1, 0.6 1.7, 1 2.2");
    cw_node_set(F.fire[k], "energy", cw_val_float(2.6f)); cw_node_set(F.fire[k], "speed_min", cw_val_float(6)); cw_node_set(F.fire[k], "speed_max", cw_val_float(30));
    cw_node_set(F.fire[k], "damping", cw_val_float(1.6f)); cw_node_set(F.fire[k], "gravity", cw_val_vec3(0, 2, 0));
    /* a white-hot core inside the fireball, swelling with it and gone in half a second */
    snprintf(name, sizeof name, "KillCore%d", k);
    F.core[k] = fx_particles(name, 24, .55f, 9, "ADD", "0 1 0.9 0.7 0.2, 0.15 1 0.85 0.55 1, 0.5 1 0.5 0.15 0.6, 1 0.4 0.1 0.02 0", "0 0.4, 0.2 1, 1 1.6");
    cw_node_set(F.core[k], "energy", cw_val_float(5)); cw_node_set(F.core[k], "speed_min", cw_val_float(2)); cw_node_set(F.core[k], "speed_max", cw_val_float(12));
    cw_node_set(F.core[k], "damping", cw_val_float(2));
    snprintf(name, sizeof name, "KillSmoke%d", k);
    F.smoke[k] = fx_particles(name, 70, 5.5f, 15, "ALPHA",
      "0 0.15 0.13 0.12 0, 0.12 0.13 0.12 0.11 0.7, 0.6 0.22 0.21 0.2 0.5, 1 0.32 0.32 0.32 0", "0 0.3, 0.2 1, 1 2.8");
    cw_node_set(F.smoke[k], "speed_min", cw_val_float(6)); cw_node_set(F.smoke[k], "speed_max", cw_val_float(26)); /* rolls out of the fireball */
    cw_node_set(F.smoke[k], "damping", cw_val_float(.8f)); cw_node_set(F.smoke[k], "gravity", cw_val_vec3(0, 3, 0));
    cw_node_set(F.smoke[k], "explosiveness", cw_val_float(.6f));
    if (k < LIGHTS) { /* hidden while dark: a light only costs while it glows */
      snprintf(name, sizeof name, "KillLight%d", k);
      cw_node l = F.light[k] = cw_node_new("OmniLight3D", name);
      cw_node_set(l, "color", cw_val_color(1, .62f, .3f, 1)); cw_node_set(l, "energy", cw_val_float(0)); cw_node_set(l, "visible", cw_val_bool(false));
      cw_node_set(l, "range", cw_val_float(420)); cw_node_set(l, "shadow", cw_val_bool(false));
      cw_node_add_child(cw_tree_root(G.tree), l); F.light_age[k] = 99;
    }
    snprintf(name, sizeof name, "KillRipple%d", k);
    cw_node s = F.ripple[k] = cw_node_new("Shockwave3D", name);
    cw_node_set(s, "ground", cw_val_bool(false)); cw_node_set(s, "max_radius", cw_val_float(70)); cw_node_set(s, "duration", cw_val_float(.7f));
    cw_node_set(s, "thickness", cw_val_float(9)); cw_node_set(s, "strength", cw_val_float(1.4f)); cw_node_set(s, "autostart", cw_val_bool(false));
    cw_node_add_child(cw_tree_root(G.tree), s);
  }
  F.charred = t3_mesh_standard_material_new(0x2a2624); F.charred->roughness = .9f; F.charred->metalness = .3f;
  for (int i = 0; i < KILLS * DEBRIS; i++) {
    debris *d = &F.pieces[i];
    d->node = box((t3_object *)G.scene, 0, 0, 0, 2.6f + (i % 3) * 1.2f, .6f + (i % 2) * .5f, 2 + (i % 4) * 1, F.charred);
    d->node->visible = false;
    snprintf(name, sizeof name, "DebrisSmoke%d", i);
    cw_node t = d->smoke = cw_node_new("Trail3D", name);
    cw_node_set(t, "length", cw_val_int(40)); cw_node_set(t, "width", cw_val_float(0));
    cw_node_set(t, "blend", cw_val_enum("ALPHA")); cw_node_set(t, "color", cw_val_color(.16f, .15f, .14f, .7f));
    cw_node_set(t, "color_ramp", cw_val_string("0 1 1 1 0, 0.6 1 1 1 0.6, 0.95 1.6 0.9 0.5 1, 1 4 1.6 0.4 1"));
    cw_node_set(t, "visible", cw_val_bool(false)); cw_node_set(t, "emitting", cw_val_bool(false));
    cw_node_add_child(cw_tree_root(G.tree), t);
  }
  F.sparks = fx_particles("HitSparks", 28, .4f, .3f, "ADD", "0 1 0.9 0.6 1, 0.5 1 0.5 0.1 1, 1 0.3 0.05 0 0", "0 1, 1 0.4");
  cw_node_set(F.sparks, "texture", cw_val_string("")); cw_node_set(F.sparks, "draw_mode", cw_val_enum("STRETCHED")); cw_node_set(F.sparks, "stretch", cw_val_float(.05f));
  cw_node_set(F.sparks, "energy", cw_val_float(6)); cw_node_set(F.sparks, "speed_min", cw_val_float(20)); cw_node_set(F.sparks, "speed_max", cw_val_float(70)); cw_node_set(F.sparks, "emission_radius", cw_val_float(1));
  F.puff = fx_particles("HitPuff", 8, 1.1f, 3.5f, "ALPHA", "0 0.2 0.19 0.18 0, 0.12 0.2 0.19 0.18 0.65, 1 0.35 0.35 0.35 0", "0 0.4, 1 2");
  cw_node_set(F.puff, "speed_min", cw_val_float(1)); cw_node_set(F.puff, "speed_max", cw_val_float(6)); cw_node_set(F.puff, "emission_radius", cw_val_float(1));
  /* spray: a jet under 30 m over open water throws a sheet of spray behind it */
  F.spray = fx_particles("LowSpray", 120, 1.2f, 5, "ALPHA", "0 0.9 0.95 1 0, 0.1 0.9 0.95 1 0.55, 1 0.9 0.95 1 0", "0 0.4, 0.3 1, 1 2.2");
  cw_node_set(F.spray, "one_shot", cw_val_bool(false)); cw_node_set(F.spray, "explosiveness", cw_val_float(0)); cw_node_set(F.spray, "direction", cw_val_vec3(0, 1, 0));
  cw_node_set(F.spray, "spread", cw_val_float(35)); cw_node_set(F.spray, "speed_min", cw_val_float(8)); cw_node_set(F.spray, "speed_max", cw_val_float(22)); cw_node_set(F.spray, "gravity", cw_val_vec3(0, -9.81f, 0));
  cw_node_set(F.spray, "emission_shape", cw_val_enum("BOX")); cw_node_set(F.spray, "emission_extents", cw_val_vec3(5, .2f, 3));
  /* a hurt jet: grey smoke from the tail below 60 % airframe, fire from the nozzles below 30 % */
  F.hurt_smoke = fx_particles("HurtSmoke", 160, 2.6f, 3.6f, "ALPHA",
    "0 0.4 0.39 0.38 0, 0.08 0.36 0.35 0.34 0.9, 0.6 0.5 0.5 0.49 0.55, 1 0.6 0.6 0.6 0", "0 0.6, 0.3 1.8, 1 4");
  cw_node_set(F.hurt_smoke, "one_shot", cw_val_bool(false)); cw_node_set(F.hurt_smoke, "explosiveness", cw_val_float(0));
  cw_node_set(F.hurt_smoke, "emission_radius", cw_val_float(.6f)); cw_node_set(F.hurt_smoke, "speed_min", cw_val_float(1)); cw_node_set(F.hurt_smoke, "speed_max", cw_val_float(4));
  F.hurt_fire = fx_particles("HurtFire", 60, .32f, 2.1f, "ADD",
    "0 1 0.7 0.3 0, 0.15 1 0.55 0.15 0.9, 0.6 1 0.25 0.04 0.6, 1 0.3 0.05 0.02 0", "0 0.6, 0.3 1, 1 0.4");
  cw_node_set(F.hurt_fire, "one_shot", cw_val_bool(false)); cw_node_set(F.hurt_fire, "explosiveness", cw_val_float(0));
  cw_node_set(F.hurt_fire, "emission_radius", cw_val_float(.5f)); cw_node_set(F.hurt_fire, "energy", cw_val_float(3));
  cw_node_set(F.hurt_fire, "speed_min", cw_val_float(1)); cw_node_set(F.hurt_fire, "speed_max", cw_val_float(5));
  /* both ride on the aircraft node at the nozzles (it carries the jet's drawn position), the puffs left in the world */
  cw_node_reparent(F.hurt_smoke, G.jet_node); cw_node_reparent(F.hurt_fire, G.jet_node);
  cw_node3d_set_position(F.hurt_smoke, (t3_vec3){ .3f, -.5f, 7.2f }); cw_node3d_set_position(F.hurt_fire, (t3_vec3){ -.58f, -.5f, 6.9f });
  /* cumulus cards: per site a flat grey-blue base, a body and a sunlit crown, each many overlapping soft puffs at low
   * opacity (softer edges than a few big ones) that slowly turn over; all of it at the cloud's own height */
  for (int i = 0; i < 8; i++) for (int part = 0; part < CARD_PARTS; part++) {
    static const struct { int amount; float ex, ey, ez, dy, size; const char *ramp; float energy; } P[CARD_PARTS] = {
      { 20, 470, 14, 230, -5, 360, "0 0.62 0.67 0.75 0, 0.25 0.62 0.67 0.75 0.26, 0.75 0.62 0.67 0.75 0.26, 1 0.62 0.67 0.75 0", 1 },
      { 24, 420, 24, 210, 22, 340, "0 0.88 0.9 0.94 0, 0.25 0.88 0.9 0.94 0.24, 0.75 0.88 0.9 0.94 0.24, 1 0.88 0.9 0.94 0", 1.05f },
      { 18, 330, 20, 170, 48, 300, "0 1 0.98 0.95 0, 0.25 1 0.98 0.95 0.26, 0.75 1 0.98 0.95 0.26, 1 1 0.98 0.95 0", 1.2f } };
    snprintf(name, sizeof name, "CumulusCard%d_%d", i, part);
    cw_node n = F.cards[i][part] = cw_node_new("GPUParticles3D", name);
    cw_node_set(n, "amount", cw_val_int(P[part].amount)); cw_node_set(n, "lifetime", cw_val_float(40));
    cw_node_set(n, "preprocess", cw_val_float(40)); cw_node_set(n, "explosiveness", cw_val_float(0)); cw_node_set(n, "one_shot", cw_val_bool(false));
    cw_node_set(n, "emission_shape", cw_val_enum("BOX")); cw_node_set(n, "emission_extents", cw_val_vec3(P[part].ex, P[part].ey, P[part].ez));
    cw_node_set(n, "speed_min", cw_val_float(.3f)); cw_node_set(n, "speed_max", cw_val_float(1.2f)); cw_node_set(n, "gravity", cw_val_vec3(0, 0, 0));
    cw_node_set(n, "direction", cw_val_vec3(.92f, 0, .38f)); cw_node_set(n, "spread", cw_val_float(12)); /* a slow drift with the wind, tens of metres over a puff's life */
    cw_node_set(n, "size", cw_val_float(0)); cw_node_set(n, "size_random", cw_val_float(.4f)); cw_node_set(n, "spin", cw_val_float(.02f));
    cw_node_set(n, "texture", cw_val_string("effects/billow.sprite")); cw_node_set(n, "blend", cw_val_enum("ALPHA"));
    cw_node_set(n, "soft_distance", cw_val_float(80)); cw_node_set(n, "camera_fade", cw_val_float(180));
    cw_node_set(n, "color_ramp", cw_val_string(P[part].ramp)); cw_node_set(n, "energy", cw_val_float(P[part].energy));
    cw_node_set(n, "emitting", cw_val_bool(false)); cw_node_set(n, "visible", cw_val_bool(false));
    F.card_size[part] = P[part].size;
    float y = fmaxf(cloud_sites[i][1], terrain_height(cloud_sites[i][0], cloud_sites[i][2]) + 350);
    cw_node3d_set_position(n, (t3_vec3){ cloud_sites[i][0] + 30, y + P[part].dy, cloud_sites[i][2] - 10 });
    cw_node_add_child(cw_tree_root(G.tree), n);
  }
  F.cards_on = -1;
  cw_debug_command("sw", "sw <objective_nearest|lock_flicker|plain_bandits|old_blasts|no_damage_states|no_cloud_cards|lone_bandits|plain_guns> <0|1>: "
    "test switches (1: that part as it was before); sw bandit_at <m> | blasts <n> | health <n> | cloud_view <site> | flicker <on> <off>: test hooks", sw_cmd);
  /* the gates' view, in three fields (the engine holds 512 debug fields and the game is near that):
   * feel.counts: flash rises, damage state, cloud cards, objective target, largest flight, most bandits at once, low
   * passes, bandits smoking; feel.values: flash min gap, flash max step, objective bearing, elevation and distance,
   * bandit 0 on screen x y, the test cloud on screen x y (960x540 units); enemy.smoking per bandit */
  cw_debug_field("feel.counts", F.counts, CW_DBG_I32, 8); cw_debug_f32_array("feel.values", F.values, 9);
  cw_debug_field("enemy.smoking", F.smoking, CW_DBG_I32, ENEMIES);
}

static void feel_reset(void) {
  for (int i = 0; i < ENEMIES; i++) { cw_node_set(F.contrail[i], "visible", cw_val_bool(false)); cw_node_set(F.contrail[i], "emitting", cw_val_bool(false)); F.smoking[i] = 0; }
  for (int k = 0; k < LIGHTS; k++) { F.light_age[k] = 99; cw_node_set(F.light[k], "energy", cw_val_float(0)); cw_node_set(F.light[k], "visible", cw_val_bool(false)); }
  for (int i = 0; i < KILLS * DEBRIS; i++) { F.pieces[i].active = 0; F.pieces[i].node->visible = false; cw_node_set(F.pieces[i].smoke, "visible", cw_val_bool(false)); cw_node_set(F.pieces[i].smoke, "emitting", cw_val_bool(false)); }
  F.twist = 0; F.flight_max = F.active_max = F.low_passes = F.smoked = 0; F.low_time = 0; cw_node_set(F.spray, "emitting", cw_val_bool(false));
  F.since_rise = 99; F.rises = 0; F.min_gap = 99; F.max_step = 0; F.pop_age = 99; F.hurt_state = 0;
  cw_node_set(F.hurt_smoke, "emitting", cw_val_bool(false)); cw_node_set(F.hurt_fire, "emitting", cw_val_bool(false));
}

/* a bandit enters the fight: its trail starts clean and its smoke waits for a hit */
static void feel_spawn(int i) { F.max_hp[i] = G.enemies[i].hp; F.smoking[i] = 0; }

/* a kill: fireball, smoke column, a light that swells and fades, a heat ripple, burning pieces that fall.
 * vx, vy, vz: the target's motion per tick, carried by the pieces; scale: 1 a fighter, more for big targets */
static void feel_kill(float x, float y, float z, float vx, float vy, float vz, float scale) {
  if (sw[SW_OLD_BLASTS]) { explode(x, y, z); return; }
  G.audio_boom = 1.2f;
  int k = F.next_kill++ % KILLS;
  cw_node_set(F.fire[k], "size", cw_val_float(13 * scale)); cw_node_set(F.smoke[k], "size", cw_val_float(15 * scale));
  cw_node_set(F.core[k], "size", cw_val_float(9 * scale));
  start_burst(F.fire[k], x, y, z); start_burst(F.core[k], x, y, z); start_burst(F.smoke[k], x, y + 4, z);
  cw_node3d_set_position(F.ripple[k], (t3_vec3){ x, y, z }); cw_shockwave_fire(F.ripple[k]);
  /* one rise a second at most: a kill inside a second of the last flash glows with its fire, without a new light */
  int lights = (int)ceilf(LIGHTS * cw_quality_scale("flash_lights"));
  if (F.since_rise >= 1 && lights > 0) {
    int l = F.next_light++ % (lights < LIGHTS ? lights : LIGHTS);
    if (F.rises) F.min_gap = fminf(F.min_gap, F.since_rise);
    F.rises++; F.since_rise = 0; F.light_age[l] = sw[SW_INSTANT_FLASH] ? FLASH_RISE : 0; F.light_peak[l] = 90 * scale; F.light_prev[l] = 0;
    cw_node3d_set_position(F.light[l], (t3_vec3){ x, y + 6, z }); cw_node_set(F.light[l], "visible", cw_val_bool(true));
  }
  for (int j = 0; j < DEBRIS; j++) {
    debris *d = &F.pieces[k * DEBRIS + j];
    float a = j * 1.7f + rand01() * 1.2f, up = .25f + rand01() * .45f, out = .35f + rand01() * .35f;
    d->x = x; d->y = y; d->z = z; d->age = 0; d->active = 1; d->node->visible = true;
    d->vx = vx * .55f + sinf(a) * out; d->vy = vy * .4f + up; d->vz = vz * .55f + cosf(a) * out;
    t3_object_set_position(d->node, x, y, z);
    cw_node3d_set_position(d->smoke, (t3_vec3){ x, y, z }); cw_node3d_snapshot(d->smoke);
    cw_node_set(d->smoke, "visible", cw_val_bool(true)); cw_node_set(d->smoke, "emitting", cw_val_bool(true)); cw_node_set(d->smoke, "width", cw_val_float(4));
  }
}

/* guns: every fourth round a thick hot tracer with a short streak behind it */
static void feel_tracer(shot *s) {
  if (sw[SW_PLAIN_GUNS] || G.shots_fired % 4 != 3) return;
  s->tracer = 1; t3_object_set_scale(s->node, 2.6f, 2.6f, 9);
  if (CW_HANDLE_IS_NULL(s->trail)) {
    char name[32]; snprintf(name, sizeof name, "ShotTrail%d", (int)(s - G.shots));
    s->trail = cw_node_new("Trail3D", name); cw_node_set(s->trail, "blend", cw_val_enum("ADD")); cw_node_add_child(cw_tree_root(G.tree), s->trail);
  }
  cw_node_set(s->trail, "length", cw_val_int(10)); cw_node_set(s->trail, "energy", cw_val_float(3));
  cw_node_set(s->trail, "color", cw_val_color(1, .62f, .25f, .8f)); cw_node_set(s->trail, "color_ramp", cw_val_string("0 1 1 1 0, 1 1 1 1 1"));
  cw_node3d_set_position(s->trail, (t3_vec3){ s->x, s->y, s->z }); cw_node3d_snapshot(s->trail);
  cw_node_set(s->trail, "visible", cw_val_bool(true)); cw_node_set(s->trail, "emitting", cw_val_bool(true)); cw_node_set(s->trail, "width", cw_val_float(.55f));
}
/* a round that hits and does not kill: sparks, a puff of smoke, a tick in the ear and the pad */
static void feel_hit(float x, float y, float z) {
  if (sw[SW_PLAIN_GUNS]) return;
  start_burst(F.sparks, x, y, z); start_burst(F.puff, x, y, z);
  G.audio_alert = fmaxf(G.audio_alert, .05f); G.audio_tone_hz = 1900; rumble(0, .22f, 35);
}
/* the next flight: two bandits (three from the third wave) side by side, an ace among them from the second wave;
 * at five kills in the interceptor sortie a second flight comes in from the sea */
static void feel_flight(int room) {
  int n = sw[SW_LONE_BANDITS] ? 1 : G.wave >= 3 ? 3 : 2;
  if (n > room) n = room;
  float side = (rand01() - .5f) * 460;
  for (int k = 0; k < n; k++) {
    int before[ENEMIES]; for (int i = 0; i < ENEMIES; i++) before[i] = G.enemies[i].active;
    spawn_enemy();
    for (int i = 0; i < ENEMIES; i++) if (G.enemies[i].active && !before[i]) {
      enemy *e = &G.enemies[i];
      if (k > 0 && !sw[SW_LONE_BANDITS]) { /* the wingmen: off the lead's wing, a little behind */
        enemy *lead = &G.enemies[F.lead];
        float right = k == 1 ? 1 : -1;
        e->x = lead->x + cosf(lead->heading) * 90 * right - sinf(lead->heading) * 60 * k; e->z = lead->z + sinf(lead->heading) * 90 * right + cosf(lead->heading) * 60 * k;
        e->y = lead->y + 15 * k; e->heading = lead->heading; e->role = lead->role == 2 ? 0 : lead->role; e->fire_cool = 3 + k;
      } else { F.lead = i; (void)side; }
      if (F.inbound) e->heading = atan2f(G.px - e->x, e->z - G.pz); /* turned in at the player */
    }
  }
  if (n > 1) notify(n == 3 ? "CONTROL: FLIGHT OF THREE INBOUND. BREAK THEM UP." : "CONTROL: PAIR OF BANDITS INBOUND.", 3);
  F.flight_max = n > F.flight_max ? n : F.flight_max;
}
static void feel_twist(void) {
  if (G.level != 3 || F.twist || G.kills < 5 || sw[SW_LONE_BANDITS]) return;
  F.twist = 1;
  int room = 0; for (int i = 0; i < ENEMIES; i++) room += !G.enemies[i].active;
  float saved = G.heading; G.heading = PI * .5f; F.inbound = 1; /* the flight comes in off the sea, east of the island, turned in at the player */
  feel_flight(room < 3 ? room : 3); G.heading = saved; F.inbound = 0;
  notify("CONTROL: SECOND WAVE OFF THE SEA. LOW AND FAST.", 4);
}

/* the HUD's score pop over TARGET DESTROYED */
static void feel_score(int points) { F.pop_score = points; F.pop_age = 0; }

static void feel_update(float dt) {
  F.since_rise += dt; F.pop_age += dt;
  /* the kill lights: up over FLASH_RISE, down over FLASH_FADE, each tick's change kept as the gate's measure */
  for (int k = 0; k < LIGHTS; k++) {
    if (F.light_age[k] > FLASH_RISE + FLASH_FADE + 1) continue;
    F.light_age[k] += dt;
    float t = F.light_age[k], e = t < FLASH_RISE ? ease_in_out(t / FLASH_RISE) : 1 - ease_in_out((t - FLASH_RISE) / FLASH_FADE);
    float energy = F.light_peak[k] * clampf(e, 0, 1);
    float step = (energy - F.light_prev[k]) / fmaxf(F.light_peak[k], 1e-3f);
    F.max_step = fmaxf(F.max_step, step); F.light_prev[k] = energy;
    cw_node_set(F.light[k], "energy", cw_val_float(energy)); cw_node_set(F.light[k], "visible", cw_val_bool(energy > 0));
  }
  for (int i = 0; i < KILLS * DEBRIS; i++) {
    debris *d = &F.pieces[i];
    if (!d->active) continue;
    d->age += dt; d->vy -= 9.81f * dt * dt * 1.6f; /* per-tick velocity, metres per tick */
    d->vx *= .995f; d->vz *= .995f;
    d->x += d->vx * dt * 60; d->y += d->vy * dt * 60; d->z += d->vz * dt * 60;
    t3_object_set_position(d->node, d->x, d->y, d->z);
    t3_object_set_rotation(d->node, d->age * (2.1f + i % 3), d->age * (1.3f + i % 2), d->age * 1.7f);
    cw_node3d_set_position(d->smoke, (t3_vec3){ d->x, d->y, d->z });
    float ground = fmaxf(0, terrain_height(d->x, d->z));
    if (d->y < ground + .5f || d->age > 9) {
      if (d->y < ground + .5f && d->age < 9) explode(d->x, ground + 1, d->z);
      d->active = 0; d->node->visible = false; cw_node_set(d->smoke, "emitting", cw_val_bool(false)); cw_node_set(d->smoke, "width", cw_val_float(0));
    }
  }
  /* bandits: a vapour trail and a hot exhaust; a hit bandit trails smoke instead */
  bandit_rim(!sw[SW_PLAIN_BANDITS]);
  for (int i = 0; i < ENEMIES; i++) {
    enemy *e = &G.enemies[i];
    bool on = e->active && G.state == PLAY && !sw[SW_PLAIN_BANDITS];
    F.glow[i]->visible = on && G.level != 6;
    /* the ace wears a gold edge: the same rim, its own colour (each bandit's materials are its own clones) */
    int ace = on && e->role == 2;
    if (F.rim_on && ace != F.rim_ace[i]) {
      F.rim_ace[i] = ace; cw_rim_light_desc d = cw_rim_light_default();
      d.color = ace ? (t3_color){ 1, .6f, .12f } : (t3_color){ 1, .86f, .66f }; d.strength = ace ? 4.5f : 2.6f; d.power = 2.2f; d.near = 90; d.far = 380; d.close = ace ? .35f : 0;
      cw_rim_light_apply_tree(e->modern, &d); cw_rim_light_apply_tree(e->vintage, &d);
    }
    bool smoke = on && !sw[SW_NO_DAMAGE_STATES] && e->hp < F.max_hp[i];
    if (smoke != F.smoking[i]) {
      F.smoking[i] = smoke; F.smoked += smoke;
      cw_node_set(F.contrail[i], "color", smoke ? cw_val_color(.18f, .17f, .16f, .8f) : cw_val_color(.95f, .97f, 1, .55f));
    }
    cw_node_set(F.contrail[i], "visible", cw_val_bool(on)); cw_node_set(F.contrail[i], "emitting", cw_val_bool(on));
    cw_node_set(F.contrail[i], "width", cw_val_float(on ? (smoke ? 4.2f : G.level == 1 ? 1.3f : 2.2f) : 0));
    if (on) {
      t3_vec3 tail = t3_vec3_apply_quat((t3_vec3){ 0, -.4f, 6.5f }, e->node->quaternion);
      cw_node3d_set_position(F.contrail[i], (t3_vec3){ e->x + tail.x, e->y + tail.y, e->z + tail.z });
    }
  }
  /* how many bandits fight at once; the sea sortie's second wave */
  { int a = 0; for (int i = 0; i < ENEMIES; i++) a += G.enemies[i].active; if (a > F.active_max) F.active_max = a; }
  if (G.state == PLAY) feel_twist();
  /* a low pass over open water: spray behind the jet, and a bonus for holding it two seconds */
  float sea = terrain_height(G.px, G.pz);
  bool low = G.state == PLAY && sea < 0 && G.py < 30 && (G.map_style == 0 || G.map_style == 7);
  cw_node_set(F.spray, "emitting", cw_val_bool(low)); cw_node_set(F.spray, "visible", cw_val_bool(true));
  if (low) {
    t3_vec3 tail = t3_vec3_apply_quat((t3_vec3){ 0, 0, 14 }, G.flight.attitude);
    cw_node3d_set_position(F.spray, (t3_vec3){ G.px + tail.x, .5f, G.pz + tail.z });
    F.low_time += dt;
    if (F.low_time >= 2 && F.low_time - dt < 2) { F.low_passes++; G.score += 50; notify("LOW PASS +50", 2); }
  } else F.low_time = 0;
  /* the player's jet */
  int hurt = G.state == PLAY && !sw[SW_NO_DAMAGE_STATES] ? (G.health < 30 ? 2 : G.health < 60 ? 1 : 0) : 0;
  if (hurt != F.hurt_state) {
    F.hurt_state = hurt;
    cw_node_set(F.hurt_smoke, "emitting", cw_val_bool(hurt >= 1)); cw_node_set(F.hurt_fire, "emitting", cw_val_bool(hurt >= 2));
    cw_node_set(F.hurt_smoke, "visible", cw_val_bool(true)); cw_node_set(F.hurt_fire, "visible", cw_val_bool(true));
    cw_node_set(F.hurt_smoke, "size", cw_val_float(hurt >= 2 ? 4.6f : 3.2f));
  }
  /* cumulus cards on the presets without volumetric fog */
  int cards = !G.cloud_active && !sw[SW_NO_CLOUD_CARDS];
  if (cards != F.cards_on) {
    F.cards_on = cards;
    for (int i = 0; i < 8; i++) for (int p = 0; p < CARD_PARTS; p++) {
      /* particles draw while alive whatever their node's visibility: off means zero size */
      cw_node_set(F.cards[i][p], "visible", cw_val_bool(cards)); cw_node_set(F.cards[i][p], "emitting", cw_val_bool(cards)); cw_node_set(F.cards[i][p], "size", cw_val_float(cards ? F.card_size[p] : 0));
      if (cards) cw_particles_restart(F.cards[i][p]);
    }
  }
}

static void feel_publish(void) {
  int32_t c[8] = { F.rises, F.hurt_state, F.cards_on, F.objective, F.flight_max, F.active_max, F.low_passes, F.smoked };
  float v[9] = { F.min_gap, F.max_step, F.objective_view[0], F.objective_view[1], F.objective_view[2], F.test_screen[0], F.test_screen[1], F.test_cloud[0], F.test_cloud[1] };
  memcpy(F.counts, c, sizeof c); memcpy(F.values, v, sizeof v);
}
/* the objective the HUD points at: the nearest goal, a target behind counting as up to 2.5 times as far (the one
 * ahead wins unless the one behind is much closer), and the one already shown kept unless another is 15 % better */
static float objective_rating(float x, float y, float z) {
  float dx = x - G.px, dy = y - G.py, dz = z - G.pz, d = sqrtf(dx * dx + dy * dy + dz * dz);
  if (sw[SW_OBJECTIVE_NEAREST]) return d * d;
  t3_vec3 f = t3_vec3_apply_quat((t3_vec3){ 0, 0, -1 }, G.flight.attitude);
  float c = (f.x * dx + f.y * dy + f.z * dz) / fmaxf(d, 1);
  return d * (1.75f - .75f * c);
}
static void objective_view(int id) {
  F.objective = id; float x, y, z;
  if (id < 0 || !target_position(id, &x, &y, &z)) { F.objective_view[0] = F.objective_view[1] = F.objective_view[2] = 0; return; }
  t3_vec3 l = aircraft_local(x, y, z);
  F.objective_view[0] = atan2f(l.x, -l.z) * 180 / PI;
  F.objective_view[1] = atan2f(l.y, sqrtf(l.x * l.x + l.z * l.z)) * 180 / PI;
  F.objective_view[2] = t3_vec3_length(l);
}

/* test hook: put bandit 0 straight ahead at this range, flying the same way, wings level */
static void bandit_ahead(float range) {
  enemy *e = &G.enemies[0];
  if (!e->active) { e->active = e->node->visible = true; e->hp = 3; e->role = 0; e->phase = 0; e->fire_cool = 99; }
  feel_spawn(0);
  t3_vec3 f = t3_vec3_apply_quat((t3_vec3){ 0, 0, -1 }, G.flight.attitude);
  e->x = G.px + f.x * range; e->y = G.py + f.y * range + 8; e->z = G.pz + f.z * range;
  e->heading = G.heading; e->maneuver = 1; e->maneuver_time = 30; e->fire_cool = 99;
  e->modern->visible = G.level != 1 && G.level != 6; e->vintage->visible = G.level == 1; e->sleigh->visible = G.level == 6;
  cw_node3d_set_position(F.contrail[0], (t3_vec3){ e->x, e->y, e->z }); cw_node3d_snapshot(F.contrail[0]);
}

static void sw_cmd(int argc, const char **argv, cw_reply *r) {
  if (argc >= 3) {
    for (int i = 0; i < SW_COUNT; i++) if (!strcmp(argv[1], sw_names[i])) { sw[i] = atoi(argv[2]) != 0; cw_reply_printf(r, "{\"%s\":%d}", sw_names[i], (int)sw[i]); return; }
    if (!strcmp(argv[1], "bandit_at")) { bandit_ahead((float)atof(argv[2])); cw_reply_printf(r, "ok"); return; }
    if (!strcmp(argv[1], "health")) { G.health = atoi(argv[2]); cw_reply_printf(r, "ok"); return; }
    if (!strcmp(argv[1], "blasts")) {
      int n = atoi(argv[2]);
      t3_vec3 f = t3_vec3_apply_quat((t3_vec3){ 0, 0, -1 }, G.flight.attitude);
      for (int i = 0; i < n; i++) feel_kill(G.px + f.x * (500 + i * 60) + (i - 1.5f) * 40, G.py + f.y * 500 + 20, G.pz + f.z * (500 + i * 60), 0, 0, 0, 1);
      cw_reply_printf(r, "ok"); return;
    }
    if (!strcmp(argv[1], "cloud_view")) {
      int i = F.test_site = atoi(argv[2]) & 7;
      /* [height over the cloud] [range]: the jet that far off, nose on the cloud (a negative height looks up at it) */
      float dy = argc >= 4 ? (float)atof(argv[3]) : 40, range = argc >= 5 ? (float)atof(argv[4]) : 1700;
      float y = fmaxf(cloud_sites[i][1], terrain_height(cloud_sites[i][0], cloud_sites[i][2]) + 350);
      G.px = cloud_sites[i][0]; G.pz = cloud_sites[i][2] + range; G.py = y + dy; G.heading = 0; G.pitch = argc >= 4 ? -atan2f(dy - 40, range) : 0; G.roll = 0;
      sync_attitude(); follow_jet(); cw_node3d_snapshot(G.jet_node); cw_node3d_snapshot(G.camera_node);
      cw_reply_printf(r, "ok"); return;
    }
    if (!strcmp(argv[1], "place") && argc >= 5) { /* x z heading(degrees) [height]: level flight 250 m (or height) over the ground or sea there */
      G.px = (float)atof(argv[2]); G.pz = (float)atof(argv[3]); G.py = fmaxf(0, terrain_height(G.px, G.pz)) + (argc >= 6 ? (float)atof(argv[5]) : 250);
      G.heading = (float)atof(argv[4]) * PI / 180; G.pitch = G.roll = 0;
      sync_attitude(); follow_jet(); cw_node3d_snapshot(G.jet_node); cw_node3d_snapshot(G.camera_node);
      cw_reply_printf(r, "ok"); return;
    }
    if (!strcmp(argv[1], "flicker") && argc >= 4) { F.flicker_on = atoi(argv[2]); F.flicker_off = atoi(argv[3]); F.flicker_t = 0; cw_reply_printf(r, "ok"); return; }
  }
  cw_reply_error(r, "sw <switch> <0|1> | bandit_at <m> | blasts <n> | health <n> | cloud_view <site> | flicker <on> <off>");
}

/* test hook: the lock sees nothing for flicker_off ticks out of every flicker_on + flicker_off (a far target winking
 * in and out of the lock cone) */
static bool lock_blinded(void) {
  if (!F.flicker_off) return false;
  return F.flicker_t++ % (F.flicker_on + F.flicker_off) >= F.flicker_on;
}
